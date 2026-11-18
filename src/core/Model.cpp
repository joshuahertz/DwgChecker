#include "Model.h"

#include <algorithm>

#include <QDate>

#include <QTime>
#include <QTimeZone>

namespace dc {

QDateTime DwgTime::toDateTime() const {
    if (!valid) return {};
    const QDate date = QDate::fromJulianDay(day);
    return QDateTime(date, QTime(0, 0), QTimeZone::UTC).addMSecs(ms);
}

QString DwgTime::toString() const {
    if (!valid) return QStringLiteral("-");
    return toDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz"));
}

QString insUnitsName(int u) {
    switch (u) {
    case 0: return QStringLiteral("unitless");
    case 1: return QStringLiteral("inches");
    case 2: return QStringLiteral("feet");
    case 3: return QStringLiteral("miles");
    case 4: return QStringLiteral("millimeters");
    case 5: return QStringLiteral("centimeters");
    case 6: return QStringLiteral("meters");
    case 7: return QStringLiteral("kilometers");
    case 8: return QStringLiteral("microinches");
    case 9: return QStringLiteral("mils");
    case 10: return QStringLiteral("yards");
    case 13: return QStringLiteral("microns");
    case 14: return QStringLiteral("decimeters");
    default: return u < 0 ? QStringLiteral("unknown") : QStringLiteral("units code %1").arg(u);
    }
}

QString dwgVersionName(const QString& v) {
    static const QList<QPair<QString, QString>> map = {
        {"AC1009", "R12"}, {"AC1012", "R13"}, {"AC1014", "R14"}, {"AC1015", "R2000"},
        {"AC1018", "R2004"}, {"AC1021", "R2007"}, {"AC1024", "R2010"}, {"AC1027", "R2013"},
        {"AC1032", "R2018"}};
    for (const auto& [k, n] : map)
        if (k == v) return n;
    return v;
}

QString kindName(PrimKind k) {
    switch (k) {
    case PrimKind::Line: return QStringLiteral("line");
    case PrimKind::Arc: return QStringLiteral("arc");
    case PrimKind::Circle: return QStringLiteral("circle");
    case PrimKind::Ellipse: return QStringLiteral("ellipse");
    case PrimKind::EllipseArc: return QStringLiteral("elliptical arc");
    case PrimKind::Spline: return QStringLiteral("spline");
    case PrimKind::Text: return QStringLiteral("text");
    case PrimKind::Dimension: return QStringLiteral("dimension");
    case PrimKind::Hatch: return QStringLiteral("hatch");
    case PrimKind::Unsupported: return QStringLiteral("unsupported");
    }
    return {};
}

int lineweightFromCode(int code) {
    static const int mm100[24] = {0, 5, 9, 13, 15, 18, 20, 25, 30, 35, 40, 50, 53, 60, 70, 80, 90, 100, 106, 120, 140, 158, 200, 211};
    if (code >= 0 && code < 24) return mm100[code];
    if (code == 29) return kLineweightByLayer;
    if (code == 30) return kLineweightByBlock;
    return kLineweightDefault;
}

QString lineweightName(int lw) {
    const QString mm = QStringLiteral("%1 mm").arg(plottedLineweight(lw) / 100.0, 0, 'f', 2);
    return lw == kLineweightDefault ? QStringLiteral("Default (%1)").arg(mm) : mm;
}

QString ltClassName(LtClass c) {
    switch (c) {
    case LtClass::Visible: return QStringLiteral("visible");
    case LtClass::Hidden: return QStringLiteral("hidden");
    case LtClass::Center: return QStringLiteral("center");
    case LtClass::Phantom: return QStringLiteral("phantom");
    case LtClass::Other: return QStringLiteral("other");
    }
    return {};
}

bool isGeometric(PrimKind k) {
    return k == PrimKind::Line || k == PrimKind::Arc || k == PrimKind::Circle || isElliptic(k) || k == PrimKind::Spline;
}
bool isElliptic(PrimKind k) { return k == PrimKind::Ellipse || k == PrimKind::EllipseArc; }
bool isAngularDimType(const QString& t) { return t.startsWith(QLatin1String("ANG")); }

bool isLinearDimType(const QString& t) { return t == QLatin1String("LINEAR") || t == QLatin1String("ALIGNED"); }

bool sameDimFamily(const QString& a, const QString& b) { return a == b || (isLinearDimType(a) && isLinearDimType(b)); }

double Primitive::sweep() const {
    if (kind == PrimKind::Circle || kind == PrimKind::Ellipse) return kTwoPi;
    if (kind != PrimKind::Arc && kind != PrimKind::EllipseArc) return 0;
    const double s = ccwSweep(a0, a1);
    return s == 0 ? kTwoPi : s;
}

Vec2 Primitive::midpoint() const {
    switch (kind) {
    case PrimKind::Line: return (p0 + p1) * 0.5;
    case PrimKind::Arc:
    case PrimKind::EllipseArc: return pointAt(a0 + sweep() / 2);
    default: return center;
    }
}

BBox Primitive::bbox() const {
    BBox b;
    switch (kind) {
    case PrimKind::Line: b.add(p0); b.add(p1); break;
    case PrimKind::Circle:
        b.add(center - Vec2{radius, radius}); b.add(center + Vec2{radius, radius});
        break;
    case PrimKind::Arc: {
        b.add(arcStart()); b.add(arcEnd());
        const double sw = sweep();
        for (int q = 0; q < 4; ++q) {
            const double ang = q * kPi / 2;
            if (ccwSweep(a0, ang) <= sw) b.add(center + fromAngle(ang) * radius);
        }
        break;
    }
    case PrimKind::Ellipse: {
        // Half the box: where x (then y) is furthest from the center.
        const double c = std::cos(axisAngle), s = std::sin(axisAngle);
        const Vec2 h{std::hypot(radius * c, minorRadius * s), std::hypot(radius * s, minorRadius * c)};
        b.add(center - h);
        b.add(center + h);
        break;
    }
    case PrimKind::EllipseArc: {
        b.add(arcStart()); b.add(arcEnd());
        // The parameters where x, then y, turns: t and t + pi.
        const double c = std::cos(axisAngle), s = std::sin(axisAngle);
        const double tx = std::atan2(-minorRadius * s, radius * c), ty = std::atan2(minorRadius * c, radius * s);
        const double sw = sweep();
        for (double t : {tx, tx + kPi, ty, ty + kPi})
            if (ccwSweep(a0, normalizeAngle(t)) <= sw) b.add(pointAt(t));
        break;
    }
    case PrimKind::Spline:
        if (spline) b = spline->box;
        if (!b.valid()) b.add(center);
        break;
    case PrimKind::Dimension: b.add(center); b.add(p0); b.add(p1); break;
    case PrimKind::Hatch:
        if (loops)
            for (const auto& l : *loops)
                for (Vec2 q : l) b.add(q);
        if (!b.valid()) b.add(center);
        break;
    default: b.add(center); break;
    }
    return b;
}

double Primitive::size() const {
    switch (kind) {
    case PrimKind::Line: return length();
    case PrimKind::Arc: return radius * sweep();
    case PrimKind::Circle: return radius * kTwoPi;
    case PrimKind::Ellipse:
    case PrimKind::EllipseArc: {
        // Ramanujan's perimeter, in proportion to the part drawn.
        const double a = radius, b = minorRadius;
        return kPi * (3 * (a + b) - std::sqrt((3 * a + b) * (a + 3 * b))) * sweep() / kTwoPi;
    }
    case PrimKind::Spline: return spline ? spline->length : 0;
    default: return 0;
    }
}

void setEllipse(Primitive& p, Vec2 c, Vec2 U, Vec2 V, double t0, double t1, bool full) {
    // The axes: at the parameter tau where c + U cos(t) + V sin(t) is furthest from c, U and V
    // turned by tau are perpendicular, and the first is the major semi-axis. Then
    // P(t) = c + U1 cos(t - tau) + V1 sin(t - tau).
    const double A = dot(U, U), B = dot(V, V), D = dot(U, V);
    const double tau = 0.5 * std::atan2(2 * D, A - B);
    const double ct = std::cos(tau), st = std::sin(tau);
    Vec2 U1 = tau == 0 ? U : U * ct + V * st;  // tau 0 (axes given): keep the exact bits
    Vec2 V1 = tau == 0 ? V : V * ct - U * st;
    double s0 = t0 - tau, s1 = t1 - tau;
    if (cross(U1, V1) < 0) {  // traced clockwise (mirrored): P = c + U1 cos(-s) + (-V1) sin(-s)
        V1 = -V1;
        const double n0 = -s1, n1 = -s0;
        s0 = n0;
        s1 = n1;
    }
    double th = U1.angle();  // (-pi, pi]
    if (th < 0 || th >= kPi) {  // the other end of the major axis: the parameter moves by pi
        U1 = -U1;
        V1 = -V1;
        s0 += kPi;
        s1 += kPi;
        th = U1.angle();
        if (th >= kPi || th < 0) th = 0;  // -0 or rounding at the boundary
    }
    p.center = c;
    p.radius = U1.length();
    p.minorRadius = V1.length();
    p.axisAngle = th == 0 ? 0.0 : th;  // never -0
    if (full) {
        p.kind = PrimKind::Ellipse;
        p.a0 = p.a1 = 0;
    } else {
        p.kind = PrimKind::EllipseArc;
        p.a0 = normalizeAngle(s0);
        p.a1 = normalizeAngle(s1);
    }
}

bool sameEllipse(const Primitive& a, const Primitive& b, double linTol, double angTol) {
    if (a.kind != b.kind || !isElliptic(a.kind)) return false;
    if (dist(a.center, b.center) > linTol || std::fabs(a.radius - b.radius) > linTol ||
        std::fabs(a.minorRadius - b.minorRadius) > linTol)
        return false;
    const double turn = lineAngleDiff(a.axisAngle, b.axisAngle);
    if (turn > angTol && turn * (a.radius - a.minorRadius) > linTol) return false;
    return a.kind == PrimKind::Ellipse || (dist(a.arcStart(), b.arcStart()) <= linTol && dist(a.arcEnd(), b.arcEnd()) <= linTol);
}

Primitive transformed(const Primitive& p, const Transform2D& t) {
    if (t.isIdentity()) return p;  // keep the exact bits
    Primitive r = p;
    r.p0 = t.apply(p.p0);
    r.p1 = t.apply(p.p1);
    r.center = t.apply(p.center);
    if (p.hasDimCenter) r.dimCenter = t.apply(p.dimCenter);
    if (p.dimPlace) {
        auto q = std::make_shared<DimPlace>(*p.dimPlace);
        q->dir = t.applyLinear(q->dir);
        q->side0 = t.applyLinear(q->side0);
        q->side1 = t.applyLinear(q->side1);
        for (Vec2* e : {&q->edge0a, &q->edge0b, &q->edge1a, &q->edge1b}) *e = t.apply(*e);
        r.dimPlace = std::move(q);
    }
    // How a dimension is drawn is shown only where a file is shown as drawn, never moved: a
    // moved copy goes without, rather than copying and moving it for nothing (D88).
    r.dimLook.reset();
    // Uniform scale of a similarity transform (exactly 1 for rigid ones).
    double sc = std::sqrt(std::fabs(t.det()));
    if (std::fabs(sc - 1.0) < 1e-12) sc = 1.0;
    if (sc != 1.0) {
        r.radius = p.radius * sc;
        if (p.kind == PrimKind::Dimension && !isAngularDimType(p.dimType)) r.measurement = p.measurement * sc;
    }
    if (p.spline) {  // moved with its control points (D99)
        auto s = std::make_shared<SplineData>(*p.spline);
        mapSpline(*s, t, t.scaleFactor());
        r.spline = std::move(s);
    }
    if (isElliptic(p.kind)) {  // its axes moved by the linear part, then found again
        const Vec2 u = fromAngle(p.axisAngle);
        setEllipse(r, r.center, t.applyLinear(u * p.radius), t.applyLinear(u.perp() * p.minorRadius), p.a0, p.a1,
                   p.kind == PrimKind::Ellipse);
    }
    if (p.kind == PrimKind::Hatch && p.loops) {
        auto out = std::make_shared<std::vector<std::vector<Vec2>>>(*p.loops);
        for (auto& l : *out)
            for (Vec2& q : l) q = t.apply(q);
        r.loops = std::move(out);
    }
    if (p.kind == PrimKind::Arc) {
        const double phi = t.rotationAngle();
        if (t.isMirrored()) {
            // T = R(phi) * diag(-1, 1): angle t maps to phi + pi - t and orientation reverses,
            // so CCW a0->a1 becomes CCW (phi+pi-a1)->(phi+pi-a0).
            r.a0 = normalizeAngle(phi + kPi - p.a1);
            r.a1 = normalizeAngle(phi + kPi - p.a0);
        } else {
            r.a0 = normalizeAngle(p.a0 + phi);
            r.a1 = normalizeAngle(p.a1 + phi);
        }
    }
    return r;
}

bool hatchContains(const Primitive& h, Vec2 q) {
    if (!h.loops) return false;
    int crossings = 0;
    bool inAny = false;
    for (const auto& l : *h.loops) {
        bool in = false;
        const size_t n = l.size();
        for (size_t i = 0, j = n - 1; i < n; j = i++) {
            const Vec2 a = l[i], b = l[j];
            if ((a.y > q.y) != (b.y > q.y) && q.x < (b.x - a.x) * (q.y - a.y) / (b.y - a.y) + a.x) in = !in;
        }
        crossings += in;
        inAny = inAny || in;
    }
    return h.evenOdd ? (crossings % 2) == 1 : inAny;
}

double hatchBoundaryDistance(const Primitive& h, Vec2 q) {
    double d = 1e300;
    if (!h.loops) return d;
    for (const auto& l : *h.loops) {
        const size_t n = l.size();
        for (size_t i = 0, j = n - 1; i < n; j = i++) d = std::min(d, pointSegmentDistance(q, l[j], l[i]));
    }
    return d;
}

BBox Drawing::bbox(bool geometricOnly) const {
    BBox b;
    for (const auto& p : prims)
        if (!geometricOnly || isGeometric(p.kind)) b.add(p.bbox());
    return b;
}

int Drawing::count(PrimKind k) const {
    int n = 0;
    for (const auto& p : prims) n += p.kind == k;
    return n;
}

void Drawing::addWarning(WarnKind k, const QString& text, std::vector<int> prims_) {
    warnings.push_back({k, text, std::move(prims_)});
}

}  // namespace dc
