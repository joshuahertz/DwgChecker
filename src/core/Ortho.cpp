#include "Ortho.h"

#include "Dsu.h"

#include <QFile>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>
#include <bit>
#include <limits>
#include <cstdint>
#include <numeric>

namespace dc {

QString viewNameString(ViewName n) {
    switch (n) {
    case ViewName::Front: return QStringLiteral("Front");
    case ViewName::Top: return QStringLiteral("Top");
    case ViewName::Right: return QStringLiteral("Right");
    case ViewName::Left: return QStringLiteral("Left");
    case ViewName::Bottom: return QStringLiteral("Bottom");
    case ViewName::Rear: return QStringLiteral("Rear");
    case ViewName::Other: return QStringLiteral("Other");
    case ViewName::Ignored: return QStringLiteral("Ignored");
    }
    return {};
}

bool parseViewName(const QString& s, ViewName* out) {
    for (ViewName n : allViewNames())
        if (viewNameString(n).compare(s, Qt::CaseInsensitive) == 0) { *out = n; return true; }
    return false;
}

const QList<ViewName>& allViewNames() {
    static const QList<ViewName> all = {ViewName::Front, ViewName::Top, ViewName::Right, ViewName::Left,
                                        ViewName::Bottom, ViewName::Rear, ViewName::Other, ViewName::Ignored};
    return all;
}

int ViewSet::indexOfName(ViewName n) const {
    for (int i = 0; i < static_cast<int>(views.size()); ++i)
        if (views[i].name == n) return i;
    return -1;
}

QString ViewSet::label(int idx) const {
    if (idx < 0 || idx >= static_cast<int>(views.size())) return QString();
    const ViewDef& v = views[idx];
    int same = 0, ord = 0;
    for (int i = 0; i < static_cast<int>(views.size()); ++i)
        if (views[i].name == v.name) { ++same; if (i < idx) ++ord; }
    QString l = v.name == ViewName::Ignored ? QStringLiteral("Ignored") : viewNameString(v.name) + QStringLiteral(" view");
    if (same > 1) l += QStringLiteral(" %1").arg(ord + 1);
    return l;
}

void ViewSet::rebuild(const Drawing& ref) {
    const int nv = static_cast<int>(views.size());
    const int np = static_cast<int>(ref.prims.size());
    primView.resize(np, -1);
    for (int& v : primView)
        if (v >= nv) v = -1;
    // Boxes from the assigned primitives, then every unassigned primitive goes to the
    // nearest view, so nothing is counted without belonging to a view. Not to an Ignored
    // view (unless there is no other): its box may enclose everything, and an item put
    // there would silently never be graded.
    for (auto& v : views) v.bbox = BBox();
    for (int i = 0; i < np; ++i)
        if (primView[i] >= 0) views[primView[i]].bbox.add(ref.prims[i].bbox());
    if (nv > 0)
        for (int i = 0; i < np; ++i) {
            if (primView[i] >= 0) continue;
            const BBox pb = ref.prims[i].bbox();
            int best = -1, bestIgn = -1;
            double bestD = 1e300, bestIgnD = 1e300;
            for (int v = 0; v < nv; ++v) {
                if (!views[v].bbox.valid()) continue;
                const double d = views[v].bbox.gap(pb);
                if (views[v].name == ViewName::Ignored) {
                    if (d < bestIgnD) { bestIgnD = d; bestIgn = v; }
                } else if (d < bestD) {
                    bestD = d;
                    best = v;
                }
            }
            primView[i] = best >= 0 ? best : bestIgn >= 0 ? bestIgn : 0;
        }
    for (auto& v : views) { v.prims.clear(); v.bbox = BBox(); }
    primIds.clear();
    for (int i = 0; i < np; ++i) {
        primIds << ref.prims[i].sourceId;
        const int v = primView[i];
        if (v < 0) continue;
        views[v].prims.push_back(i);
        views[v].bbox.add(ref.prims[i].bbox());
    }
}

ViewSet ViewSet::rebindTo(const Drawing& ref) const {
    ViewSet out = *this;
    QStringList ids;
    for (const auto& p : ref.prims) ids << p.sourceId;
    if (ids != primIds) {
        // The primitive list changed (different canonicalization settings): map by source id,
        // falling back to the nearest view box.
        QHash<QString, int> idToView;
        for (int i = 0; i < primIds.size() && i < static_cast<int>(primView.size()); ++i)
            if (primView[i] >= 0) idToView.insert(primIds[i], primView[i]);
        out.primView.assign(ref.prims.size(), -1);
        for (int i = 0; i < static_cast<int>(ref.prims.size()); ++i) {
            const Primitive& p = ref.prims[i];
            int v = idToView.value(p.sourceId, -1);
            for (int k = 0; v < 0 && k < p.mergedFrom.size(); ++k) v = idToView.value(p.mergedFrom[k], -1);
            out.primView[i] = v;  // -1: rebuild() assigns it to the nearest view
        }
    }
    out.rebuild(ref);
    return out;
}

namespace {

bool isCore(const Primitive& p) {
    return isGeometric(p.kind) && (p.ltClass == LtClass::Visible || p.ltClass == LtClass::Hidden || p.ltClass == LtClass::Other);
}

}  // namespace

ViewSet segmentViews(const Drawing& ref, double gapFraction) {
    ViewSet vs;
    vs.gapFraction = gapFraction;
    vs.refSha256 = ref.meta.sha256;
    vs.source = QStringLiteral("auto");
    vs.primView.assign(ref.prims.size(), -1);

    std::vector<int> core;
    for (int i = 0; i < static_cast<int>(ref.prims.size()); ++i)
        if (isCore(ref.prims[i])) core.push_back(i);
    if (core.empty())
        for (int i = 0; i < static_cast<int>(ref.prims.size()); ++i)
            if (isGeometric(ref.prims[i].kind)) core.push_back(i);
    const double gap = gapFraction * ref.bbox().diagonal();

    const int n = static_cast<int>(core.size());
    std::vector<BBox> bb(n);
    for (int k = 0; k < n; ++k) bb[k] = ref.prims[core[k]].bbox();
    std::vector<int> order(n);
    std::iota(order.begin(), order.end(), 0);
    std::sort(order.begin(), order.end(), [&](int a, int b) { return bb[a].x0 < bb[b].x0; });
    Dsu dsu(n);
    for (int oi = 0; oi < n; ++oi)
        for (int oj = oi + 1; oj < n; ++oj) {
            const int a = order[oi], b = order[oj];
            if (bb[b].x0 > bb[a].x1 + gap) break;
            if (bb[a].gap(bb[b]) <= gap) dsu.unite(a, b);
        }
    // A hole in the middle of a view touches none of the outline primitives, so also
    // merge clusters whose bounding boxes overlap or come within the gap; repeat until stable.
    for (bool changed = true; changed;) {
        changed = false;
        QHash<int, BBox> cbox;
        for (int k = 0; k < n; ++k) cbox[dsu.find(k)].add(bb[k]);
        const QList<int> roots = cbox.keys();
        for (int x = 0; x < roots.size() && !changed; ++x)
            for (int y = x + 1; y < roots.size() && !changed; ++y)
                if (cbox[roots[x]].gap(cbox[roots[y]]) <= gap) {
                    dsu.unite(roots[x], roots[y]);
                    changed = true;
                }
    }
    QHash<int, int> rootToView;
    for (int k = 0; k < n; ++k) {
        const int r = dsu.find(k);
        if (!rootToView.contains(r)) {
            rootToView.insert(r, static_cast<int>(vs.views.size()));
            ViewDef v;
            v.id = static_cast<int>(vs.views.size());
            vs.views.push_back(v);
        }
        vs.primView[core[k]] = rootToView.value(r);
    }
    for (int k = 0; k < n; ++k) vs.views[vs.primView[core[k]]].bbox.add(bb[k]);

    // Attach centerlines, phantom lines, dimensions, text and unsupported entities to the nearest view.
    for (int i = 0; i < static_cast<int>(ref.prims.size()); ++i) {
        if (vs.primView[i] >= 0 || vs.views.empty()) continue;
        const BBox pb = ref.prims[i].bbox();
        int best = 0;
        double bestD = 1e300;
        for (int v = 0; v < static_cast<int>(vs.views.size()); ++v) {
            const double d = vs.views[v].bbox.gap(pb) + 1e-9 * dist(vs.views[v].bbox.center(), pb.center());
            if (d < bestD) { bestD = d; best = v; }
        }
        vs.primView[i] = best;
    }
    vs.rebuild(ref);
    guessViewNames(vs);
    return vs;
}

void guessViewNames(ViewSet& vs) {
    const int n = static_cast<int>(vs.views.size());
    for (auto& v : vs.views) v.name = ViewName::Other;
    if (n == 0) return;
    if (n == 1) { vs.views[0].name = ViewName::Front; return; }
    auto xOverlap = [](const BBox& a, const BBox& b) { return std::min(a.x1, b.x1) > std::max(a.x0, b.x0); };
    auto yOverlap = [](const BBox& a, const BBox& b) { return std::min(a.y1, b.y1) > std::max(a.y0, b.y0); };
    int front = 0;
    double bestScore = -std::numeric_limits<double>::infinity();
    for (int i = 0; i < n; ++i) {
        const BBox& a = vs.views[i].bbox;
        bool above = false, right = false;
        for (int j = 0; j < n; ++j) {
            if (i == j) continue;
            const BBox& b = vs.views[j].bbox;
            if (xOverlap(a, b) && b.center().y > a.center().y) above = true;
            if (yOverlap(a, b) && b.center().x > a.center().x) right = true;
        }
        // Prefer views with both neighbours, then the bottom-left-most, then the largest.
        const double score = (above ? 2 : 0) + (right ? 2 : 0) + a.width() * a.height() * 1e-12 -
                             (a.center().x + a.center().y) * 1e-9;
        if (score > bestScore) { bestScore = score; front = i; }
    }
    vs.views[front].name = ViewName::Front;
    const BBox& f = vs.views[front].bbox;
    std::vector<int> others;
    for (int i = 0; i < n; ++i) if (i != front) others.push_back(i);
    std::sort(others.begin(), others.end(), [&](int a, int b) {
        return dist(vs.views[a].bbox.center(), f.center()) < dist(vs.views[b].bbox.center(), f.center());
    });
    for (int i : others) {
        const BBox& b = vs.views[i].bbox;
        ViewName want = ViewName::Other;
        if (xOverlap(f, b)) want = b.center().y > f.center().y ? ViewName::Top : ViewName::Bottom;
        else if (yOverlap(f, b)) want = b.center().x > f.center().x ? ViewName::Right : ViewName::Left;
        if (want == ViewName::Right && vs.indexOfName(ViewName::Right) >= 0) want = ViewName::Rear;
        if (want != ViewName::Other && vs.indexOfName(want) >= 0) want = ViewName::Other;
        vs.views[i].name = want;
    }
}

QString sidecarPath(const QString& refPath) { return refPath + QStringLiteral(".views.json"); }

bool saveSidecar(const QString& path, const ViewSet& vs, const Drawing& ref, QString* error) {
    QJsonArray views;
    for (const auto& v : vs.views) {
        QJsonArray ids;
        for (int i : v.prims) ids.append(ref.prims[i].sourceId);
        const QJsonValue box = v.bbox.valid() ? QJsonValue(QJsonArray{v.bbox.x0, v.bbox.y0, v.bbox.x1, v.bbox.y1})
                                              : QJsonValue(QJsonValue::Null);  // empty view
        views.append(QJsonObject{{"id", v.id}, {"name", viewNameString(v.name)}, {"bbox", box}, {"prims", ids}});
    }
    const QJsonObject root{{"format", "dwgcheck-views/1"}, {"refSha256", QString::fromLatin1(ref.meta.sha256)},
                           {"refFile", ref.meta.fileName}, {"gapFraction", vs.gapFraction}, {"views", views}};
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error) *error = QStringLiteral("cannot write %1: %2").arg(path, f.errorString());
        return false;
    }
    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    return true;
}

bool loadSidecar(const QString& path, const Drawing& ref, ViewSet* out, QString* error) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error) *error = QStringLiteral("no sidecar at %1").arg(path);
        return false;
    }
    const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
    if (root.value("refSha256").toString().toLatin1() != ref.meta.sha256) {
        if (error) *error = QStringLiteral("sidecar %1 belongs to a different reference file (SHA-256 differs)").arg(path);
        return false;
    }
    ViewSet vs;
    vs.refSha256 = ref.meta.sha256;
    vs.gapFraction = root.value("gapFraction").toDouble(0.02);
    vs.source = path;
    QHash<QString, int> idToView;
    for (const auto& vv : root.value("views").toArray()) {
        const QJsonObject vo = vv.toObject();
        ViewDef v;
        v.id = static_cast<int>(vs.views.size());
        parseViewName(vo.value("name").toString(), &v.name);
        const QJsonArray b = vo.value("bbox").toArray();
        if (b.size() == 4 && b[0].toDouble() <= b[2].toDouble() && b[1].toDouble() <= b[3].toDouble()) {
            v.bbox.add(Vec2{b[0].toDouble(), b[1].toDouble()});
            v.bbox.add(Vec2{b[2].toDouble(), b[3].toDouble()});
        }
        for (const auto& id : vo.value("prims").toArray()) idToView.insert(id.toString(), v.id);
        vs.views.push_back(v);
    }
    if (vs.views.empty()) {
        if (error) *error = QStringLiteral("sidecar %1 has no views").arg(path);
        return false;
    }
    vs.primView.assign(ref.prims.size(), -1);
    for (int i = 0; i < static_cast<int>(ref.prims.size()); ++i) {
        const Primitive& p = ref.prims[i];
        int v = idToView.value(p.sourceId, -1);
        for (int k = 0; v < 0 && k < p.mergedFrom.size(); ++k) v = idToView.value(p.mergedFrom[k], -1);
        if (v < 0) {  // settings changed the primitive list: nearest saved (non-empty) box
            double best = 1e300;
            for (const auto& vd : vs.views) {
                if (!vd.bbox.valid()) continue;
                const double d = vd.bbox.gap(p.bbox());
                if (d < best) { best = d; v = vd.id; }
            }
        }
        vs.primView[i] = v;  // still -1 only when no box is usable; rebuild() then picks the nearest view
    }
    vs.rebuild(ref);
    *out = vs;
    return true;
}

std::vector<AlignmentCheck> orthoChecks(const ViewSet& vs, const std::vector<const Transform2D*>& viewT, double alignTol,
                                        bool withText) {
    std::vector<AlignmentCheck> out;
    const int fi = vs.indexOfName(ViewName::Front);
    if (fi < 0 || !viewT[fi]) return out;
    const Transform2D& tf = *viewT[fi];
    const Vec2 fRef = vs.views[fi].bbox.center();
    const Vec2 fStu = fRef - Vec2{tf.tx, tf.ty};
    for (int v = 0; v < static_cast<int>(vs.views.size()); ++v) {
        if (v == fi) continue;
        const ViewName n = vs.views[v].name;
        const bool vertical = n == ViewName::Top || n == ViewName::Bottom;
        const bool horizontal = n == ViewName::Right || n == ViewName::Left || n == ViewName::Rear;
        if (!vertical && !horizontal) continue;
        AlignmentCheck al;
        al.view = v;
        al.applicable = viewT[v] != nullptr;
        if (al.applicable) {
            const Transform2D& tv = *viewT[v];
            if (vertical) {
                const double d = tf.tx - tv.tx;  // student view shifted right by d relative to front
                al.passed = std::fabs(d) <= alignTol;
                if (!al.passed && withText)
                    al.text = QStringLiteral("%1 is not aligned with the Front view: shifted %2 %3")
                                  .arg(vs.label(v), QString::number(std::fabs(d), 'f', 3), d > 0 ? "right" : "left");
            } else {
                const double d = tf.ty - tv.ty;
                al.passed = std::fabs(d) <= alignTol;
                if (!al.passed && withText)
                    al.text = QStringLiteral("%1 is not aligned with the Front view: shifted %2 %3")
                                  .arg(vs.label(v), QString::number(std::fabs(d), 'f', 3), d > 0 ? "up" : "down");
            }
        }
        out.push_back(al);

        AlignmentCheck ar;
        ar.view = v;
        ar.isArrangement = true;
        ar.applicable = viewT[v] != nullptr;
        if (ar.applicable) {
            const Vec2 dRef = vs.views[v].bbox.center() - fRef;
            const Vec2 vStu = vs.views[v].bbox.center() - Vec2{viewT[v]->tx, viewT[v]->ty};
            const Vec2 dStu = vStu - fStu;
            const bool useY = std::fabs(dRef.y) >= std::fabs(dRef.x);
            const double r = useY ? dRef.y : dRef.x, s = useY ? dStu.y : dStu.x;
            ar.passed = (r > 0) == (s > 0) && std::fabs(s) > 0;
            if (!ar.passed && withText) {
                auto side = [&](double val) {
                    return useY ? (val > 0 ? QStringLiteral("above") : QStringLiteral("below"))
                                : (val > 0 ? QStringLiteral("right of") : QStringLiteral("left of"));
                };
                ar.text = QStringLiteral("%1 is %2 the Front view; the reference has it %3 (first-angle vs third-angle?)")
                              .arg(vs.label(v), side(s), side(r));
            }
        }
        out.push_back(ar);
    }
    return out;
}

OrthoResolution resolveViews(const std::vector<Primitive>& ref, const ViewSet& vs, const std::vector<Primitive>& stu,
                             const std::vector<int>& stuIdx, const CompareSettings& s, const MatchCriteria& crit) {
    OrthoResolution res;
    const int nv = static_cast<int>(vs.views.size());
    res.candidates.resize(nv);
    res.chosen.assign(nv, -1);
    const size_t words = (stu.size() + 63) / 64;
    std::vector<std::vector<std::vector<uint64_t>>> bits(nv);

    // 1. Per-view candidate translations against the whole student drawing.
    for (int v = 0; v < nv; ++v) {
        std::vector<int> idx;
        for (int i : vs.views[v].prims)
            if (isGeometric(ref[i].kind)) idx.push_back(i);
        if (idx.empty()) continue;
        AlignOptions ao;
        ao.allowRotation = false;
        ao.allowMirror = false;
        ao.topK = std::max(1, s.orthoTopK);
        ao.criteria = crit;
        const AlignResult ar = alignDrawings(ref, idx, stu, stuIdx, ao);
        for (const auto& c : ar.top) {
            if (c.score <= 0) continue;
            ViewCandidate vc;
            vc.T = c.T;
            vc.score = c.score;
            const auto st = transformAll(stu, stuIdx, c.T);
            Matching m;
            m.reset(ref.size(), stu.size());
            matchInto(ref, idx, st, stuIdx, crit, true, m);
            for (int j : stuIdx)
                if (m.stuToRef[j] >= 0) vc.claims.push_back(j);
            vc.score = static_cast<int>(vc.claims.size());
            res.candidates[v].push_back(vc);
            std::vector<uint64_t> b(words, 0);
            for (int j : vc.claims) b[j / 64] |= uint64_t(1) << (j % 64);
            bits[v].push_back(std::move(b));
        }
    }

    // 2. Joint resolution: every combination of candidates (plus "not found"), one-to-one
    //    claims across views. Graded views come first: the most items claimed by graded views,
    //    then the most that Ignored views add on top (so that a student's copy of one is set
    //    aside, not extra), then the fewest failed alignment checks. An Ignored view often
    //    repeats a graded one (the same part again, with its dimensions): counted alike, the
    //    two tied on a student's single copy, the first in the list won, and an Ignored view
    //    listed first took the drawing, which then scored 0 against its own solution.
    const double alignTol = s.alignTol > 0 ? s.alignTol : s.linTol;
    long long combos = 1;
    for (int v = 0; v < nv && combos <= 200000; ++v) combos *= static_cast<long long>(res.candidates[v].size()) + 1;
    struct Score {
        int graded = -1, total = -1, failed = 1 << 30;
        bool beats(const Score& o) const {
            if (graded != o.graded) return graded > o.graded;
            if (total != o.total) return total > o.total;
            return failed < o.failed;
        }
    };
    auto evaluate = [&](const std::vector<int>& pick) {
        Score sc;
        sc.total = 0;
        std::vector<uint64_t> seen(words, 0);
        for (int pass = 0; pass < 2; ++pass) {  // graded views, then Ignored ones
            for (int v = 0; v < nv; ++v) {
                if (pick[v] < 0 || vs.isIgnored(v) != (pass == 1)) continue;
                const auto& b = bits[v][pick[v]];
                for (size_t w = 0; w < words; ++w) {
                    sc.total += std::popcount(b[w] & ~seen[w]);
                    seen[w] |= b[w];
                }
            }
            if (pass == 0) sc.graded = sc.total;
        }
        std::vector<const Transform2D*> ts(nv, nullptr);
        for (int v = 0; v < nv; ++v)
            if (pick[v] >= 0) ts[v] = &res.candidates[v][pick[v]].T;
        sc.failed = 0;
        for (const auto& c : orthoChecks(vs, ts, alignTol, false)) sc.failed += c.applicable && !c.passed;  // no text (D76)
        return sc;
    };
    std::vector<int> pick(nv, -1), best(nv, -1);
    if (combos <= 200000) {
        Score bestScore;
        std::vector<int> cur(nv, -1);
        // Odometer over (-1 .. size-1) per view.
        while (true) {
            const Score sc = evaluate(cur);
            ++res.combinationsTried;
            if (sc.beats(bestScore)) {
                bestScore = sc;
                best = cur;
            }
            int v = 0;
            while (v < nv) {
                if (++cur[v] < static_cast<int>(res.candidates[v].size())) break;
                cur[v] = -1;
                ++v;
            }
            if (v == nv) break;
        }
    } else {
        // Too many views for brute force: greedy, graded views first, then the larger first.
        std::vector<int> order(nv);
        std::iota(order.begin(), order.end(), 0);
        std::sort(order.begin(), order.end(), [&](int a, int b) {
            if (vs.isIgnored(a) != vs.isIgnored(b)) return !vs.isIgnored(a);
            return vs.views[a].prims.size() > vs.views[b].prims.size();
        });
        Score bestScore = evaluate(pick);
        for (int v : order) {
            int bv = -1;
            for (int k = 0; k < static_cast<int>(res.candidates[v].size()); ++k) {
                pick[v] = k;
                const Score sc = evaluate(pick);
                if (sc.beats(bestScore)) {
                    bestScore = sc;
                    bv = k;
                }
            }
            pick[v] = bv;
        }
        best = pick;
    }
    res.chosen = best;
    return res;
}

}  // namespace dc
