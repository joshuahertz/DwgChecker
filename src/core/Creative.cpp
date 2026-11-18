#include "Creative.h"

#include "Color.h"
#include "DimText.h"
#include "Dsu.h"
#include "Format.h"
#include "SpatialGrid.h"

#include <QHash>

#include <algorithm>
#include <map>
#include <optional>

namespace dc {

namespace {

int dimOrder(const QString& t) {
    if (t == QLatin1String("LINEAR")) return 0;
    if (t == QLatin1String("ALIGNED")) return 1;
    if (t == QLatin1String("RADIUS") || t == QLatin1String("LARGE_RADIAL_DIMENSION")) return 2;
    if (t == QLatin1String("DIAMETER")) return 3;
    if (isAngularDimType(t)) return 4;
    if (t == QLatin1String("ARC_DIMENSION")) return 5;
    if (t == QLatin1String("ORDINATE")) return 6;
    return 7;
}

// The minimum of f over [a, b] (golden section), for a distance along a curve's parameter.
template <class F>
double goldenMin(F f, double a, double b) {
    const double g = (std::sqrt(5.0) - 1) / 2;
    double c = b - g * (b - a), d = a + g * (b - a);
    double fc = f(c), fd = f(d);
    for (int k = 0; k < 90; ++k) {
        if (fc < fd) {
            b = d; d = c; fd = fc;
            c = b - g * (b - a); fc = f(c);
        } else {
            a = c; c = d; fc = fd;
            d = a + g * (b - a); fd = f(d);
        }
    }
    return std::min(fc, fd);
}

// The distance to a curve traced by `at` over [t0, t1]: the nearest of n samples, refined around it.
template <class At>
double curveDistance(At at, double t0, double t1, int n, Vec2 q) {
    int best = 0;
    double bestD = 1e300;
    for (int k = 0; k <= n; ++k) {
        const double d = dist(at(t0 + (t1 - t0) * k / n), q);
        if (d < bestD) { bestD = d; best = k; }
    }
    const double lo = t0 + (t1 - t0) * std::max(0, best - 1) / n, hi = t0 + (t1 - t0) * std::min(n, best + 1) / n;
    return std::min(bestD, goldenMin([&](double t) { return dist(at(t), q); }, lo, hi));
}

// Lines, arcs, elliptical arcs and open splines: what has two ends. A spline whose ends meet
// within the tolerance is closed, flagged or not (snapped onto its start, D108).
bool hasEnds(const Primitive& p, double tol) {
    switch (p.kind) {
    case PrimKind::Line: return p.length() > 0;
    case PrimKind::Arc:
    case PrimKind::EllipseArc: return true;
    case PrimKind::Spline: return p.spline && !p.spline->closed(tol);
    default: return false;
    }
}

// An outline: geometry in a visible, hidden or other linetype. Centerlines and phantom lines
// mark axes and neighbors, not the part's edges.
bool isOutline(const Primitive& p) {
    return isGeometric(p.kind) && p.ltClass != LtClass::Center && p.ltClass != LtClass::Phantom;
}

Vec2 endOf(const Primitive& p, int e) {
    switch (p.kind) {
    case PrimKind::Arc:
    case PrimKind::EllipseArc: return e == 0 ? p.arcStart() : p.arcEnd();
    case PrimKind::Spline: return p.spline ? (e == 0 ? p.spline->start() : p.spline->end()) : p.p0;
    default: return e == 0 ? p.p0 : p.p1;
    }
}

// The object as a polyline, for where a line crosses it (not for distances).
std::vector<Vec2> flattened(const Primitive& p) {
    std::vector<Vec2> out;
    switch (p.kind) {
    case PrimKind::Line: out = {p.p0, p.p1}; break;
    case PrimKind::Circle:
    case PrimKind::Ellipse:
    case PrimKind::Arc:
    case PrimKind::EllipseArc: {
        const bool full = p.kind == PrimKind::Circle || p.kind == PrimKind::Ellipse;
        const double sw = full ? kTwoPi : p.sweep();
        const int n = std::max(8, static_cast<int>(std::ceil(256 * sw / kTwoPi)));
        for (int k = 0; k <= n; ++k) out.push_back(p.pointAt((full ? 0.0 : p.a0) + sw * k / n));
        break;
    }
    case PrimKind::Spline:
        if (p.spline) out = p.spline->sp;
        break;
    default: break;
    }
    return out;
}

// Where segment pq crosses segment ab, as the fraction along pq.
std::optional<double> crossing(Vec2 p, Vec2 q, Vec2 a, Vec2 b) {
    const Vec2 r = q - p, s = b - a;
    const double den = cross(r, s);
    if (std::fabs(den) <= 1e-15 * r.length() * s.length()) return std::nullopt;  // parallel
    const double t = cross(a - p, s) / den, u = cross(a - p, r) / den;
    if (t < 0 || t > 1 || u < -1e-12 || u > 1 + 1e-12) return std::nullopt;
    return t;
}

// How far along the line from `e` to `f` it first crosses `o`, past `from` (none: it doesn't).
// Circles and arcs exactly; the others through their polylines.
std::optional<double> firstCrossing(Vec2 e, Vec2 f, const Primitive& o, double from) {
    const double len = dist(e, f);
    std::optional<double> best;
    auto take = [&](double t) {
        const double d = t * len;
        if (d > from && (!best || d < *best)) best = d;
    };
    if (o.kind == PrimKind::Circle || o.kind == PrimKind::Arc) {
        const Vec2 dir = f - e, m = e - o.center;
        const double A = dot(dir, dir), B = 2 * dot(m, dir), C = dot(m, m) - o.radius * o.radius;
        const double disc = B * B - 4 * A * C;
        if (A <= 0 || disc < 0) return std::nullopt;
        for (const double t : {(-B - std::sqrt(disc)) / (2 * A), (-B + std::sqrt(disc)) / (2 * A)}) {
            if (t < 0 || t > 1) continue;
            if (o.kind == PrimKind::Arc && !angleInSweep((e + dir * t - o.center).angle(), o.a0, o.sweep(), 1e-9)) continue;
            take(t);
        }
        return best;
    }
    const std::vector<Vec2> pts = flattened(o);
    for (size_t k = 1; k < pts.size(); ++k)
        if (const auto t = crossing(e, f, pts[k - 1], pts[k])) take(*t);
    return best;
}

struct End {
    int prim = -1;
    int which = 0;
    Vec2 at;
};

// A center mark's cross (AutoCAD's CENTERMARK, or drawn by hand): a line centered on a circle's
// or arc's center, inside it, not in a center linetype. It marks the center: not an edge, and
// not counted.
std::vector<char> centerCrosses(const Drawing& d, double tol) {
    std::vector<char> out(d.prims.size(), 0);
    std::vector<int> rounds;
    for (int i = 0; i < static_cast<int>(d.prims.size()); ++i)
        if ((d.prims[i].kind == PrimKind::Circle || d.prims[i].kind == PrimKind::Arc) && d.prims[i].radius > 0) rounds.push_back(i);
    if (rounds.empty()) return out;
    for (int i = 0; i < static_cast<int>(d.prims.size()); ++i) {
        const Primitive& p = d.prims[i];
        if (p.kind != PrimKind::Line || p.ltClass == LtClass::Center) continue;
        const Vec2 m = p.midpoint();
        for (int c : rounds)
            if (dist(m, d.prims[c].center) <= tol && p.length() < 2 * d.prims[c].radius) {
                out[i] = 1;
                break;
            }
    }
    return out;
}

// What is counted as one item: an object, or the collinear pieces of one centerline.
struct Unit {
    CreativeKind kind;
    std::vector<int> prims;
};

bool collinear(const Primitive& a, const Primitive& b, double tol) {
    return pointLineDistance(b.p0, a.p0, a.p1) <= tol && pointLineDistance(b.p1, a.p0, a.p1) <= tol &&
           pointLineDistance(a.p0, b.p0, b.p1) <= tol && pointLineDistance(a.p1, b.p0, b.p1) <= tol;
}

std::vector<Unit> unitsOf(const Drawing& d, const CompareSettings& s) {
    const std::vector<char> cross = centerCrosses(d, s.linTol);
    std::vector<Unit> out;
    std::vector<int> center;  // centerline pieces, joined by axis below
    for (int i = 0; i < static_cast<int>(d.prims.size()); ++i) {
        if (cross[i]) continue;
        const Primitive& p = d.prims[i];
        const CreativeKind k = creativeKind(p, s);
        if (k.key.isEmpty()) continue;
        if (p.kind == PrimKind::Line && p.ltClass == LtClass::Center && p.length() > 0) center.push_back(i);
        else out.push_back({k, {i}});
    }
    const int n = static_cast<int>(center.size());
    Dsu dsu(n);
    for (int a = 0; a < n; ++a)
        for (int b = a + 1; b < n; ++b)
            if (collinear(d.prims[center[a]], d.prims[center[b]], s.linTol)) dsu.unite(a, b);
    std::map<int, Unit> axes;  // by the first piece
    for (int k = 0; k < n; ++k) {
        Unit& u = axes[dsu.find(k)];
        if (u.prims.empty()) u.kind = creativeKind(d.prims[center[k]], s);
        u.prims.push_back(center[k]);
    }
    for (auto& [first, u] : axes) out.push_back(std::move(u));
    return out;
}

// Both ends of each outline that has them, one after the other: an end's other end is the next
// or the previous (k ^ 1).
std::vector<End> outlineEnds(const Drawing& d, const std::vector<char>& skip, double tol) {
    std::vector<End> out;
    for (int i = 0; i < static_cast<int>(d.prims.size()); ++i) {
        const Primitive& p = d.prims[i];
        if (skip[i] || !isOutline(p) || !hasEnds(p, tol)) continue;
        out.push_back({i, 0, endOf(p, 0)});
        out.push_back({i, 1, endOf(p, 1)});
    }
    return out;
}

// What a kind's items must agree on, where all of the solution's items of that kind do.
struct KindRule {
    bool layer = false, color = false, lineweight = false;
    QString layerName;
    int aci = 7;
    quint32 rgb = 0;
    int lw = kLineweightDefault;
};

KindRule ruleFor(const Drawing& ref, const std::vector<int>& prims, const CompareSettings& s) {
    KindRule k;
    if (prims.empty()) return k;
    const Primitive& f = ref.prims[prims.front()];
    k.layerName = f.layer;
    k.aci = f.aci;
    k.rgb = f.rgb;
    k.lw = f.lineweight;
    auto all = [&](auto same) { return std::all_of(prims.begin(), prims.end(), [&](int i) { return same(ref.prims[i]); }); };
    k.layer = s.requireLayerMatch && all([&](const Primitive& p) { return p.layer.compare(f.layer, Qt::CaseInsensitive) == 0; });
    k.color = s.matchColor && all([&](const Primitive& p) { return colorDifference(p.aci, p.rgb, f.aci, f.rgb) <= s.colorTolerance + 1e-9; });
    k.lineweight = s.matchLineweight && all([&](const Primitive& p) { return plottedLineweight(p.lineweight) == plottedLineweight(f.lineweight); });
    return k;
}

// Why a student's item doesn't count for its kind (the first thing wrong); empty when it does.
QString problemOf(const Primitive& p, const KindRule& k, const CompareSettings& s) {
    if (k.color && colorDifference(p.aci, p.rgb, k.aci, k.rgb) > s.colorTolerance + 1e-9)
        return QStringLiteral("color %1, expected %2").arg(colorName(p.aci, p.rgb), colorName(k.aci, k.rgb));
    if (k.lineweight && plottedLineweight(p.lineweight) != plottedLineweight(k.lw))
        return QStringLiteral("lineweight %1, expected %2").arg(lineweightName(p.lineweight), lineweightName(k.lw));
    if (k.layer && p.layer.compare(k.layerName, Qt::CaseInsensitive) != 0)
        return QStringLiteral("layer %1, expected %2").arg(p.layer, k.layerName);
    if (s.requireByLayer && !(p.colorByLayer && p.ltByLayer && p.lwByLayer)) {
        QStringList what;
        if (!p.colorByLayer) what << QStringLiteral("color");
        if (!p.ltByLayer) what << QStringLiteral("linetype");
        if (!p.lwByLayer) what << QStringLiteral("lineweight");
        const QString last = what.takeLast();
        return QStringLiteral("%1 set on the object, not by layer")
            .arg(what.isEmpty() ? last : what.join(QStringLiteral(", ")) + QStringLiteral(" and ") + last);
    }
    return {};
}

}  // namespace

QString pluralKind(const QString& singular) {
    const int paren = singular.indexOf(QStringLiteral(" ("));
    const QString head = paren >= 0 ? singular.left(paren) : singular;
    const QString tail = paren >= 0 ? singular.mid(paren) : QString();
    const bool es = head.endsWith(QLatin1String("ch")) || head.endsWith(QLatin1String("sh")) || head.endsWith(QLatin1Char('s')) ||
                    head.endsWith(QLatin1Char('x'));
    return head + (es ? QStringLiteral("es") : QStringLiteral("s")) + tail;
}

QString countOf(int n, const QString& one, const QString& many) { return QStringLiteral("%1 %2").arg(n).arg(n == 1 ? one : many); }

CreativeKind creativeKind(const Primitive& p, const CompareSettings& s) {
    CreativeKind k;
    if (isGeometric(p.kind)) {
        // Visible lines say so ("lines" alone would read as all of them); other shapes are plain.
        k.key = p.ltClass == LtClass::Visible ? (p.kind == PrimKind::Line ? QStringLiteral("visible line") : kindName(p.kind))
                                              : lineDescriptor(p);
        k.group = s.matchLinetype ? k.key : kindName(p.kind);
        k.order = 100 * static_cast<int>(p.kind) + 10 * static_cast<int>(p.ltClass);
    } else if (p.kind == PrimKind::Dimension) {
        k.key = k.group = dimKindName(p.dimType) + QStringLiteral(" dimension");
        k.order = 1000 + dimOrder(p.dimType);
    } else if (p.kind == PrimKind::Hatch) {
        k.key = !p.text.isEmpty() ? QStringLiteral("hatch (%1)").arg(p.text) : QStringLiteral("hatch");
        k.group = s.checkHatchPattern ? k.key : QStringLiteral("hatch");
        k.order = 2000;
    } else {
        return k;
    }
    k.plural = pluralKind(k.key);
    k.groupPlural = pluralKind(k.group);
    return k;
}

std::vector<CreativeItems> creativeItems(const Drawing& d, const CompareSettings& s) {
    std::vector<CreativeItems> out;
    QHash<QString, int> at;
    for (const Unit& u : unitsOf(d, s)) {
        auto it = at.constFind(u.kind.key);
        if (it == at.cend()) {
            it = at.insert(u.kind.key, static_cast<int>(out.size()));
            out.push_back({u.kind, {}, 0});
        }
        CreativeItems& items = out[*it];
        items.prims.insert(items.prims.end(), u.prims.begin(), u.prims.end());
        ++items.count;
    }
    std::stable_sort(out.begin(), out.end(), [](const CreativeItems& a, const CreativeItems& b) {
        return a.kind.order != b.kind.order ? a.kind.order < b.kind.order : a.kind.key < b.kind.key;
    });
    return out;
}

std::vector<CreativeNeed> creativeNeeds(const std::vector<CreativeItems>& items, const CreativeChoices& ch) {
    std::vector<CreativeNeed> out;
    QHash<QString, int> at;
    for (const CreativeItems& it : items) {
        const CreativeKind& k = it.kind;
        if (ch.off.contains(k.key)) continue;
        const auto c = ch.counts.find(k.key);
        const int need = c != ch.counts.end() ? std::max(0, c->second) : it.count;
        if (need == 0) continue;
        auto g = at.constFind(k.group);
        if (g == at.cend()) {
            g = at.insert(k.group, static_cast<int>(out.size()));
            CreativeNeed n;
            n.key = k.group;
            n.plural = k.groupPlural;
            n.order = k.order;
            out.push_back(std::move(n));
        }
        CreativeNeed& n = out[*g];
        n.need += need;
        n.order = std::min(n.order, k.order);
        n.prims.insert(n.prims.end(), it.prims.begin(), it.prims.end());
        n.merged = n.merged || k.key != k.group;
    }
    std::stable_sort(out.begin(), out.end(),
                     [](const CreativeNeed& a, const CreativeNeed& b) { return a.order != b.order ? a.order < b.order : a.key < b.key; });
    return out;
}

CreativeSolution analyzeCreative(const Drawing& ref, const CompareSettings& s) {
    return {creativeItems(ref, s), hasCenterlines(ref), countCorners(ref, s.linTol) > 0};
}

bool hasCenterlines(const Drawing& d) {
    return std::any_of(d.prims.begin(), d.prims.end(),
                       [](const Primitive& p) { return p.kind == PrimKind::Line && p.ltClass == LtClass::Center && p.length() > 0; });
}

int countCorners(const Drawing& d, double tol) {
    const std::vector<End> ends = outlineEnds(d, centerCrosses(d, tol), tol);
    const int n = static_cast<int>(ends.size());
    SpatialGrid grid(std::max(2 * tol, 1e-9));
    for (int k = 0; k < n; ++k) grid.insert(ends[k].at, k);
    Dsu dsu(n);
    std::vector<char> met(n, 0);
    for (int k = 0; k < n; ++k)
        grid.forNear(ends[k].at, [&](int o) {
            if (o != k && ends[o].prim != ends[k].prim && dist(ends[o].at, ends[k].at) <= tol) {
                dsu.unite(k, o);
                met[k] = met[o] = 1;
            }
        });
    int corners = 0;
    for (int k = 0; k < n; ++k) corners += met[k] && dsu.find(k) == k;
    return corners;
}

double distanceToPrimitive(const Primitive& p, Vec2 q) {
    switch (p.kind) {
    case PrimKind::Line: return pointSegmentDistance(q, p.p0, p.p1);
    case PrimKind::Circle: return std::fabs(dist(q, p.center) - p.radius);
    case PrimKind::Arc: {
        if (dist(q, p.center) > 0 && angleInSweep((q - p.center).angle(), p.a0, p.sweep(), 0))
            return std::fabs(dist(q, p.center) - p.radius);
        return std::min(dist(q, p.arcStart()), dist(q, p.arcEnd()));
    }
    case PrimKind::Ellipse:
    case PrimKind::EllipseArc: {
        const bool full = p.kind == PrimKind::Ellipse;
        const double t0 = full ? 0.0 : p.a0, sw = full ? kTwoPi : p.sweep();
        return curveDistance([&](double t) { return p.pointAt(t); }, t0, t0 + sw, std::max(16, static_cast<int>(std::ceil(128 * sw / kTwoPi))), q);
    }
    case PrimKind::Spline: {
        if (!p.spline || p.spline->sp.size() < 2) return 1e300;
        const SplineData& s = *p.spline;
        // The nearest sampled piece, then along the curve's parameter around it.
        const int n = static_cast<int>(s.sp.size());
        int best = 1;
        double bestD = 1e300;
        for (int k = 1; k < n; ++k) {
            const double d = pointSegmentDistance(q, s.sp[k - 1], s.sp[k]);
            if (d < bestD) { bestD = d; best = k; }
        }
        if (static_cast<int>(s.su.size()) != n) return bestD;
        const double lo = s.su[std::max(0, best - 2)], hi = s.su[std::min(n - 1, best + 1)];
        return std::min({goldenMin([&](double u) { return dist(s.at(u), q); }, lo, hi), dist(q, s.start()), dist(q, s.end())});
    }
    default: return 1e300;
    }
}

std::vector<CenterlineProblem> centerlineProblems(const Drawing& d, double linTol) {
    std::vector<int> lines, rings;  // center-linetype lines; center-linetype circles and arcs (bolt circles)
    for (int i = 0; i < static_cast<int>(d.prims.size()); ++i) {
        const Primitive& p = d.prims[i];
        if (p.ltClass != LtClass::Center) continue;
        if (p.kind == PrimKind::Line && p.length() > 0) lines.push_back(i);
        else if ((p.kind == PrimKind::Circle || p.kind == PrimKind::Arc) && p.radius > 0) rings.push_back(i);
    }
    std::vector<CenterlineProblem> out;
    for (int c = 0; c < static_cast<int>(d.prims.size()); ++c) {
        const Primitive& p = d.prims[c];
        const bool round = p.kind == PrimKind::Circle || (p.kind == PrimKind::Arc && p.sweep() >= kPi - 1e-9);
        if (!round || !isOutline(p) || !(p.radius > 0)) continue;
        const double tol = std::max(linTol, 0.01 * p.radius);
        std::vector<int> through;
        struct Dir {
            Vec2 u;
            double lo, hi;  // how far the pieces along it reach from the center, each way
        };
        std::vector<Dir> along;
        auto addDir = [&](Vec2 u, double lo, double hi) {  // one more piece along u, from lo to hi
            const auto same = std::find_if(along.begin(), along.end(), [&](const Dir& g) { return std::fabs(cross(g.u, u)) <= 1e-3; });
            if (same == along.end()) {
                along.push_back({u, lo, hi});
                return;
            }
            if (dot(same->u, u) < 0) {  // measured the other way along it
                const double t = lo;
                lo = -hi;
                hi = -t;
            }
            same->lo = std::min(same->lo, lo);
            same->hi = std::max(same->hi, hi);
        };
        for (int i : lines) {
            const Primitive& l = d.prims[i];
            // On a line through the center, and reaching into the circle: another hole's
            // centerline along the same axis is that hole's.
            if (pointLineDistance(p.center, l.p0, l.p1) > tol || pointSegmentDistance(p.center, l.p0, l.p1) > p.radius + tol) continue;
            through.push_back(i);
            const Vec2 u = (l.p1 - l.p0).normalized();
            const double t0 = dot(l.p0 - p.center, u), t1 = dot(l.p1 - p.center, u);
            addDir(u, std::min(t0, t1), std::max(t0, t1));
        }
        // A bolt circle through the center: the direction along it there (D108).
        for (int i : rings) {
            const Primitive& g = d.prims[i];
            const double off = dist(p.center, g.center);
            if (!(off > 0) || std::fabs(off - g.radius) > tol) continue;
            const double at = (p.center - g.center).angle();
            if (g.kind == PrimKind::Arc && !angleInSweep(at, g.a0, g.sweep(), 1e-9)) continue;  // its path stops short of the center
            through.push_back(i);
            // Past the circle both ways: a whole bolt circle always; an arc by more than the
            // circle's radius each side of the center, along it.
            bool reaches = true;
            if (g.kind == PrimKind::Arc) {
                const double past = (p.radius + linTol) / g.radius, from = ccwSweep(g.a0, at);
                reaches = from > past && g.sweep() - from > past;
            }
            addDir((p.center - g.center).normalized().perp(), reaches ? -1e300 : 0, reaches ? 1e300 : 0);
        }
        std::vector<double> dirs, reaching;
        for (const Dir& g : along) {
            const double a = std::fmod(g.u.angle() + kPi, kPi);  // undirected, [0, pi)
            dirs.push_back(a);
            if (g.lo < -(p.radius + linTol) && g.hi > p.radius + linTol) reaching.push_back(a);
        }
        auto twoWays = [](const std::vector<double>& a) {
            for (size_t x = 0; x < a.size(); ++x)
                for (size_t y = x + 1; y < a.size(); ++y)
                    if (lineAngleDiff(a[x], a[y]) >= kPi / 4 - 1e-9) return true;
            return false;
        };
        if (twoWays(reaching)) continue;
        const QString what = QStringLiteral("%1 at %2").arg(describe(p), fmtPt(p.center));
        QString text;
        if (through.empty()) text = QStringLiteral("%1 has no centerlines").arg(what);
        else if (!twoWays(dirs)) text = QStringLiteral("%1 has a centerline in one direction only").arg(what);
        else text = QStringLiteral("%1: its centerlines don't reach past it on both sides").arg(what);
        out.push_back({c, through, text});
    }
    return out;
}

std::vector<CornerProblem> cornerProblems(const Drawing& d, double linTol) {
    const std::vector<char> cross = centerCrosses(d, linTol);
    const std::vector<End> ends = outlineEnds(d, cross, linTol);
    std::vector<int> outlines;
    for (int i = 0; i < static_cast<int>(d.prims.size()); ++i)
        if (!cross[i] && isOutline(d.prims[i])) outlines.push_back(i);
    const double near = std::max(0.01 * d.bbox().diagonal(), 10 * linTol);
    std::vector<BBox> boxes(d.prims.size());
    for (int i : outlines) boxes[i] = d.prims[i].bbox();

    struct Bad {
        Vec2 at;
        double size = 0;
        bool overshoot = false;
        std::vector<int> prims;
    };
    // Each end's outlines within reach, with their distances, and those it touches.
    struct Near {
        int prim;
        double dist;
    };
    std::vector<std::vector<Near>> close(ends.size());
    std::vector<std::vector<int>> touching(ends.size());
    for (size_t k = 0; k < ends.size(); ++k)
        for (int i : outlines) {
            if (i == ends[k].prim || boxes[i].distanceTo(ends[k].at) > near) continue;
            const double dd = distanceToPrimitive(d.prims[i], ends[k].at);
            close[k].push_back({i, dd});
            if (dd <= linTol) touching[k].push_back(i);
        }
    std::vector<Bad> bad;
    for (size_t k = 0; k < ends.size(); ++k) {
        if (!touching[k].empty()) continue;
        const End& e = ends[k];
        const Primitive& a = d.prims[e.prim];
        // Not what the other end is attached to (D108): a stub hanging off an edge is near that
        // edge only because it is attached there.
        const std::vector<int>& attached = touching[k ^ 1];
        double nearest = 1e300;
        int other = -1;
        std::vector<int> candidates;
        for (const Near& c : close[k]) {
            if (std::find(attached.begin(), attached.end(), c.prim) != attached.end()) continue;
            candidates.push_back(c.prim);
            if (c.dist < nearest) { nearest = c.dist; other = c.prim; }
        }
        // A line that runs past an outline near its end: the crossing nearest the end.
        if (a.kind == PrimKind::Line) {
            const Vec2 f = endOf(a, 1 - e.which);
            double over = 1e300;
            int crossed = -1;
            for (int i : candidates)
                if (const auto x = firstCrossing(e.at, f, d.prims[i], linTol); x && *x < over) { over = *x; crossed = i; }
            if (crossed >= 0 && over <= near) {
                bad.push_back({e.at, over, true, {e.prim, crossed}});
                continue;
            }
        }
        if (other < 0 || nearest > near) continue;  // an open end
        const Primitive& o = d.prims[other];
        // The convention: a hidden line that continues a visible line leaves a gap (at either's end).
        const bool continues = (a.ltClass == LtClass::Hidden && o.ltClass == LtClass::Visible) ||
                               (a.ltClass == LtClass::Visible && o.ltClass == LtClass::Hidden);
        if (continues && a.kind == PrimKind::Line && o.kind == PrimKind::Line &&
            lineAngleDiff((a.p1 - a.p0).angle(), (o.p1 - o.p0).angle()) <= 1e-6 && pointLineDistance(e.at, o.p0, o.p1) <= linTol)
            continue;
        bad.push_back({e.at, nearest, false, {e.prim, other}});
    }

    // The bad ends near each other make one corner: two lines that both stop short of it.
    const int n = static_cast<int>(bad.size());
    Dsu dsu(n);
    for (int x = 0; x < n; ++x)
        for (int y = x + 1; y < n; ++y)
            if (dist(bad[x].at, bad[y].at) <= near) dsu.unite(x, y);
    std::map<int, CornerProblem> byRoot;
    std::map<int, int> members;
    for (int x = 0; x < n; ++x) {
        const int r = dsu.find(x);
        auto [it, fresh] = byRoot.try_emplace(r);
        CornerProblem& c = it->second;
        if (fresh) c.overshoot = true;
        c.at = c.at + bad[x].at;
        ++members[r];
        // The worst gap, unless every end there runs past: then the worst overshoot.
        if (!bad[x].overshoot && c.overshoot) { c.overshoot = false; c.worst = 0; }
        if (bad[x].overshoot == c.overshoot) c.worst = std::max(c.worst, bad[x].size);
        for (int i : bad[x].prims)
            if (std::find(c.prims.begin(), c.prims.end(), i) == c.prims.end()) c.prims.push_back(i);
    }
    std::vector<CornerProblem> out;
    for (auto& [r, c] : byRoot) {
        c.at = c.at / members[r];
        out.push_back(std::move(c));
    }
    return out;
}

CompareResult compareCreative(const Drawing& ref, const Drawing& stu, const CompareSettings& s) {
    CompareResult r;
    r.mode = Mode::Creative;
    const size_t nR = ref.prims.size(), nS = stu.prims.size();
    r.refStatus.assign(nR, PrimStatus::NotCompared);
    r.stuStatus.assign(nS, PrimStatus::NotCompared);
    r.refPartner.assign(nR, -1);
    r.stuPartner.assign(nS, -1);
    r.stuT.assign(nS, Transform2D::identity());  // nothing is moved: the overlay shows the file as drawn
    r.stuView.assign(nS, -1);
    const CreativeChoices& ch = s.creative;
    std::optional<CreativeSolution> own;  // unless a batch worked it out for every student (D108)
    const CreativeSolution* sol = s.creativeSolution.get();
    if (!sol) sol = &own.emplace(analyzeCreative(ref, s));

    // The student's items by the group they are graded in. One more than needed, or of a kind
    // that isn't checked, is extra, and never counts against the student.
    QHash<QString, std::vector<Unit>> stuByKind;
    for (Unit& u : unitsOf(stu, s)) {
        for (int j : u.prims) r.stuStatus[j] = PrimStatus::Extra;
        stuByKind[u.kind.group].push_back(std::move(u));
    }

    std::vector<ErrorItem> eMissing, eRules, eWarn;
    for (const CreativeNeed& k : creativeNeeds(sol->items, ch)) {
        const int need = k.need;
        r.refCount += need;
        const KindRule rule = ruleFor(ref, k.prims, s);
        const std::vector<Unit> units = stuByKind.value(k.key);
        std::vector<int> mine, failing;  // the student's objects of this kind; those that don't count
        std::vector<const Unit*> fitting;
        QStringList reasons;
        for (const Unit& u : units) {
            mine.insert(mine.end(), u.prims.begin(), u.prims.end());
            QString why;
            for (int j : u.prims)
                if (why.isEmpty()) why = problemOf(stu.prims[j], rule, s);
            if (why.isEmpty()) {
                fitting.push_back(&u);
                continue;
            }
            failing.insert(failing.end(), u.prims.begin(), u.prims.end());
            for (int j : u.prims) r.stuStatus[j] = PrimStatus::WrongAttribute;
            if (!reasons.contains(why)) reasons << why;
        }
        const int counted = std::min(static_cast<int>(fitting.size()), need);
        for (int x = 0; x < counted; ++x)
            for (int j : fitting[x]->prims) r.stuStatus[j] = PrimStatus::Matched;
        r.matched += counted;
        for (int i : k.prims) r.refStatus[i] = counted >= need ? PrimStatus::Matched : PrimStatus::Missing;
        const int nf = static_cast<int>(units.size() - fitting.size());
        // "1 more hidden line doesn't count: layer 0, expected HIDDEN"
        auto notCounted = [&](const QString& more) {
            return QStringLiteral("%1 %2%3 %4: %5")
                .arg(nf)
                .arg(more, nf == 1 ? k.key : k.plural, nf == 1 ? QStringLiteral("doesn't count") : QStringLiteral("don't count"),
                     reasons.join(QStringLiteral("; ")));
        };
        if (counted < need) {
            QString text = QStringLiteral("%1 of %2 (%3 missing)").arg(counted).arg(countOf(need, k.key, k.plural)).arg(need - counted);
            if (nf > 0) text += QStringLiteral("; ") + notCounted(QStringLiteral("more "));
            eMissing.push_back({ErrCat::Missing, text, -1, k.prims, mine, true, QStringLiteral("too few ") + k.plural});
        } else if (nf > 0) {
            eWarn.push_back({ErrCat::Warning, QStringLiteral("warning: %1 (enough others do)").arg(notCounted(QString())), -1, {}, failing,
                             false});
        }
    }

    // ---- The drafting rules, where the solution itself has what they are about ----
    int broken = 0;
    auto mark = [&](const std::vector<int>& prims) {
        for (int j : prims)
            if (r.stuStatus[j] != PrimStatus::WrongAttribute) r.stuStatus[j] = PrimStatus::NearMiss;  // breaks a rule
    };
    if (ch.centerlines && sol->centerlines)
        for (const CenterlineProblem& p : centerlineProblems(stu, s.linTol)) {
            ++broken;
            mark({p.prim});
            std::vector<int> prims{p.prim};
            prims.insert(prims.end(), p.lines.begin(), p.lines.end());
            eRules.push_back({ErrCat::Rule, p.text, -1, {}, prims, true, QStringLiteral("missing centerlines")});
        }
    if (ch.corners && sol->corners)
        for (const CornerProblem& c : cornerProblems(stu, s.linTol)) {
            ++broken;
            mark(c.prims);
            const QString how = c.overshoot ? QStringLiteral("a line runs %1 past it").arg(fmtLen(c.worst))
                                            : QStringLiteral("a gap of %1").arg(fmtLen(c.worst));
            eRules.push_back({ErrCat::Rule, QStringLiteral("corner at %1 doesn't meet: %2").arg(fmtPt(c.at), how), -1, {}, c.prims, true,
                              QStringLiteral("corner doesn't meet")});
        }

    for (const auto& w : stu.warnings) eWarn.push_back({ErrCat::Warning, QStringLiteral("warning: ") + w.text, -1, {}, w.prims, false});
    for (auto* list : {&eMissing, &eRules, &eWarn})
        for (auto& e : *list) r.errors.push_back(std::move(e));

    const int denom = r.refCount + broken;
    r.correctness = denom > 0 ? 100.0 * r.matched / denom : 100.0;
    r.pass = r.matched == r.refCount && broken == 0;
    return r;
}

}  // namespace dc
