#include "Comparator.h"

#include "Aligner.h"
#include "DimText.h"
#include "Format.h"
#include "HatchCheck.h"

#include <QHash>

#include <algorithm>

namespace dc {

MatchCriteria criteriaFrom(const CompareSettings& s) {
    MatchCriteria c;
    c.linTol = s.linTol;
    c.angTol = s.angTol;
    c.matchLinetype = s.matchLinetype;
    c.requireLayer = s.requireLayerMatch;
    c.strictCenterlines = s.strictCenterlines;
    c.checkDimensions = s.checkDimensions;
    c.checkDimPrecision = s.checkDimPrecision;
    return c;
}

std::vector<int> comparableIndices(const Drawing& d, const CompareSettings& s) {
    std::vector<int> out;
    for (int i = 0; i < static_cast<int>(d.prims.size()); ++i) {
        const PrimKind k = d.prims[i].kind;
        if (isGeometric(k) || (k == PrimKind::Dimension && s.checkDimensions) || (k == PrimKind::Hatch && s.checkHatches))
            out.push_back(i);
    }
    return out;
}

QStringList CompareResult::errorStrings() const {
    QStringList l;
    for (const auto& e : errors) l << e.text;
    return l;
}

QString CompareResult::errorSummary() const {
    QStringList order;
    QHash<QString, int> counts;
    int warnings = 0;
    for (const auto& e : errors) {
        const QString label = !e.label.isEmpty() ? e.label : e.failing ? QStringLiteral("other") : QString();
        if (label.isEmpty()) {
            ++warnings;
            continue;
        }
        if (!counts.contains(label)) order << label;
        ++counts[label];
    }
    QStringList parts;
    for (const QString& l : order) parts << QStringLiteral("%1 %2").arg(counts.value(l)).arg(l);
    QString out = parts.isEmpty() ? QStringLiteral("no errors") : parts.join(QStringLiteral(", "));
    if (warnings > 0) out += QStringLiteral("; %1 warning%2").arg(warnings).arg(warnings == 1 ? "" : "s");
    return out;
}

std::vector<int> CompareResult::studentPrimsOfView(int view) const {
    std::vector<int> out;
    for (int j = 0; j < static_cast<int>(stuStatus.size()); ++j)
        if (stuStatus[j] != PrimStatus::NotCompared && (view < 0 || stuView[j] == view)) out.push_back(j);
    return out;
}

namespace {

QString sizeText(const Primitive& p) {
    switch (p.kind) {
    case PrimKind::Line: return fmtLen(p.length()) + QStringLiteral(" long");
    case PrimKind::Circle: return QChar(0x00D8) + fmtLen(2 * p.radius);
    case PrimKind::Arc: return QStringLiteral("R") + fmtLen(p.radius);
    case PrimKind::Dimension: return isAngularDimType(p.dimType) ? fmtDeg(p.measurement) : fmtLen(p.measurement);
    default: return {};
    }
}

// The message for a near-miss pair, and its short label for the summary ("shifted"...).
QString nearMissText(const Primitive& r, const Primitive& s, bool superset, bool displaced, double tol, double angTol,
                     QString* label) {
    const QString at = fmtPt(messageLocation(r));
    switch (r.kind) {
    case PrimKind::Circle: {
        const Vec2 off = s.center - r.center;
        if (std::fabs(s.radius - r.radius) > tol) {
            *label = QStringLiteral("wrong size");
            QString t = QStringLiteral("%1 %2%3 drawn as %2%4").arg(lineDescriptor(r), QChar(0x00D8), fmtLen(2 * r.radius),
                                                                    fmtLen(2 * s.radius, 2 * r.radius));
            if (off.length() > tol) t += QStringLiteral(" and center off by %1").arg(fmtPt(off));
            return t;
        }
        *label = QStringLiteral("shifted");
        const QString what = r.ltClass == LtClass::Visible ? QStringLiteral("hole") : lineDescriptor(r);
        return QStringLiteral("%1 %2%3 center off by %4").arg(what, QChar(0x00D8), fmtLen(2 * r.radius), fmtPt(off));
    }
    case PrimKind::Arc: {
        if (displaced) {  // same radius and sweep, elsewhere, perhaps turned a little
            *label = QStringLiteral("shifted");
            QString t = QStringLiteral("%1 R%2 center off by %3").arg(lineDescriptor(r), fmtLen(r.radius), fmtPt(s.center - r.center));
            const double turn = angleDiff(s.a0, r.a0);
            if (turn > angTol && turn * r.radius > tol) t += QStringLiteral(" and rotated by %1").arg(fmtDeg(turn));
            return t;
        }
        if (std::fabs(s.radius - r.radius) > tol) {
            *label = QStringLiteral("wrong size");
            return QStringLiteral("%1 R%2 at %3 drawn as R%4").arg(lineDescriptor(r), fmtLen(r.radius), at, fmtLen(s.radius, r.radius));
        }
        if (std::fabs(s.sweep() - r.sweep()) > angTol && std::fabs(s.sweep() - r.sweep()) * r.radius > tol) {
            *label = QStringLiteral("wrong arc angle");
            return QStringLiteral("%1 R%2 at %3: sweep %4 drawn as %5")
                .arg(lineDescriptor(r), fmtLen(r.radius), at, fmtDeg(r.sweep()), fmtDeg(s.sweep()));
        }
        const Vec2 off = s.center - r.center;
        if (off.length() > tol) {
            *label = QStringLiteral("shifted");
            return QStringLiteral("%1 R%2 center off by %3").arg(lineDescriptor(r), fmtLen(r.radius), fmtPt(off));
        }
        *label = QStringLiteral("rotated");
        return QStringLiteral("%1 R%2 at %3 is rotated by %4")
            .arg(lineDescriptor(r), fmtLen(r.radius), at, fmtDeg(angleDiff(s.a0, r.a0)));
    }
    case PrimKind::Line: {
        const QString desc = QStringLiteral("%1 %2 long at %3").arg(lineDescriptor(r), fmtLen(r.length()), at);
        if (superset) {
            *label = QStringLiteral("extended line");
            return QStringLiteral("%1 is part of a longer line %2 long (extended, or merged with a projection line)")
                .arg(desc, fmtLen(s.length(), r.length()));
        }
        if (displaced) {  // same length, elsewhere, perhaps turned a little
            *label = QStringLiteral("shifted");
            QString t = QStringLiteral("%1 is shifted by %2").arg(desc, fmtPt(s.midpoint() - r.midpoint()));
            const double turn = lineAngleDiff((r.p1 - r.p0).angle(), (s.p1 - s.p0).angle());
            if (turn > angTol && turn * r.length() > tol) t += QStringLiteral(" and rotated by %1").arg(fmtDeg(turn));
            return t;
        }
        if (std::fabs(s.length() - r.length()) > tol) {
            *label = QStringLiteral("wrong length");
            return QStringLiteral("%1 is %2 long").arg(desc, fmtLen(s.length(), r.length()));
        }
        const double da = lineAngleDiff((r.p1 - r.p0).angle(), (s.p1 - s.p0).angle());
        if (da > angTol && da * r.length() > tol) {
            *label = QStringLiteral("rotated");
            return QStringLiteral("%1 is rotated by %2").arg(desc, fmtDeg(da));
        }
        *label = QStringLiteral("shifted");
        return QStringLiteral("%1 is shifted by %2").arg(desc, fmtPt(s.midpoint() - r.midpoint()));
    }
    case PrimKind::Dimension: {
        *label = QStringLiteral("wrong dimension value");
        const QString rv = sizeText(r);
        QString sv = isAngularDimType(s.dimType) ? fmtDeg(s.measurement) : fmtLen(s.measurement, r.measurement);
        return QStringLiteral("dimension %1 shows %2").arg(rv, sv);
    }
    default: *label = QStringLiteral("other"); return {};
    }
}

QString attributeText(const Primitive& r, const Primitive& s, const MatchCriteria& c) {
    QString t = QStringLiteral("%1 %2 at %3").arg(lineDescriptor(r, true), sizeText(r), fmtPt(messageLocation(r)));
    const bool ltWrong = c.matchLinetype && r.ltClass != s.ltClass;
    const bool layerWrong = c.requireLayer && r.layer.compare(s.layer, Qt::CaseInsensitive) != 0;
    if (ltWrong) t += QStringLiteral(" drawn as %1").arg(lineDescriptor(s, true));
    if (layerWrong) t += QStringLiteral(" is on layer %1, expected %2").arg(s.layer, r.layer);
    return t;
}

// "linear dimension at (2.0, -0.6) reads 4.00, expected 4.000 (2 decimal places instead of 3)"
QString precisionText(const Primitive& r, const Primitive& s) {
    QString t = QStringLiteral("%1 dimension at %2 reads %3, expected %4")
                    .arg(dimKindName(r.dimType), fmtPt(r.center), dimShownText(s), dimShownText(r));
    const QString why = dimPrecisionReason(r, s);
    if (!why.isEmpty()) t += QStringLiteral(" (%1)").arg(why);
    return t;
}

struct Group {
    int view = -1;                 // -1 = whole drawing
    QString prefix;                // "Front view: " or ""
    std::vector<int> refIdx;       // comparable reference primitives of this group
    Transform2D T;                 // student -> reference for this group
    std::vector<Primitive> st;     // student primitives transformed by T (full size)
};

// Hatch regions of one group (whole drawing, or one view): coverage, pattern, extras.
void checkHatchGroup(const Drawing& ref, const Group& g, const std::vector<int>& refU, const std::vector<int>& stuU,
                     const CompareSettings& s, CompareResult& r, std::vector<ErrorItem>& eNear, std::vector<ErrorItem>& eMissExtra,
                     std::vector<ErrorItem>& eWarn) {
    std::vector<int> refH, stuH;
    for (int i : refU)
        if (ref.prims[i].kind == PrimKind::Hatch) refH.push_back(i);
    for (int j : stuU)
        if (g.st[j].kind == PrimKind::Hatch && r.stuStatus[j] == PrimStatus::Extra) stuH.push_back(j);
    if (refH.empty() && stuH.empty()) return;
    const HatchCheckResult hr = checkHatchRegions(ref.prims, refH, g.st, stuH, s.linTol);
    for (const HatchRefResult& rr : hr.refs) {
        const Primitive& R = ref.prims[rr.ref];
        const QString at = fmtPt(rr.centroid);
        if (!rr.contributors.empty()) r.refPartner[rr.ref] = rr.contributors.front();
        std::vector<int> stuPrims(rr.contributors.begin(), rr.contributors.end());
        if (!rr.sampled) {  // too thin to measure: give the benefit of the doubt, but say so
            r.refStatus[rr.ref] = PrimStatus::Matched;
            eWarn.push_back({ErrCat::Warning, g.prefix + QStringLiteral("warning: hatched region at %1 is too small to check").arg(at),
                                g.view, {rr.ref}, {}, false});
            continue;
        }
        if (rr.coverage < kHatchAbsent) {
            const QString expect = s.checkHatchPattern ? QStringLiteral(" (expected %1)").arg(R.text) : QString();
            eMissExtra.push_back({ErrCat::Missing, g.prefix + QStringLiteral("region at %1 is not hatched%2").arg(at, expect), g.view,
                                  {rr.ref}, {}, true, QStringLiteral("missing hatch")});
        } else if (rr.coverage < kHatchFilled) {
            r.refStatus[rr.ref] = PrimStatus::NearMiss;
            eNear.push_back({ErrCat::NearMiss,
                             g.prefix + QStringLiteral("region at %1 is only %2% hatched").arg(at).arg(std::floor(rr.coverage * 100), 0, 'f', 0),
                             g.view, {rr.ref}, stuPrims, true, QStringLiteral("partly hatched")});
        } else {
            QStringList wrong;
            for (const QString& pat : rr.patterns)
                if (pat != R.text) wrong << pat;
            if (s.checkHatchPattern && !wrong.isEmpty()) {
                r.refStatus[rr.ref] = PrimStatus::WrongAttribute;
                eNear.push_back({ErrCat::NearMiss,
                                 g.prefix + QStringLiteral("region at %1 is hatched with %2, expected %3").arg(at, wrong.join(", "), R.text),
                                 g.view, {rr.ref}, stuPrims, true, QStringLiteral("wrong hatch pattern")});
            } else {
                r.refStatus[rr.ref] = PrimStatus::Matched;
            }
        }
    }
    for (const HatchStuResult& sr : hr.stus) {
        const Primitive& S = g.st[sr.stu];
        r.stuT[sr.stu] = g.T;
        r.stuView[sr.stu] = g.view;
        if (!sr.sampled || sr.outside <= kHatchSpill) {
            r.stuStatus[sr.stu] = PrimStatus::Matched;  // lies inside the expected regions
            continue;
        }
        // Stays Extra (penalized like any extra primitive).
        const QString at = fmtPt(sr.centroid);
        const QString t = sr.outside >= kHatchFilled
                              ? QStringLiteral("extra hatching (%1) at %2").arg(S.text, at)
                              : QStringLiteral("hatching (%1) at %2 also fills %3% outside the expected region")
                                    .arg(S.text, at).arg(std::ceil(sr.outside * 100), 0, 'f', 0);
        eMissExtra.push_back({ErrCat::Extra, g.prefix + t, g.view, {}, {sr.stu}, s.extras == ExtrasPolicy::Penalize,
                              QStringLiteral("extra hatch")});
    }
}

}  // namespace

// The comparison at the student's own scale (compareDrawings adds the scale fallback).
static CompareResult compareCore(const Drawing& ref, const Drawing& stu, const CompareSettings& s, const ViewSet* views) {
    CompareResult r;
    r.mode = s.mode;
    const MatchCriteria crit = criteriaFrom(s);
    const std::vector<int> refIdx = comparableIndices(ref, s);
    const std::vector<int> stuIdx = comparableIndices(stu, s);
    const size_t nR = ref.prims.size(), nS = stu.prims.size();
    r.refCount = static_cast<int>(refIdx.size());
    r.refStatus.assign(nR, PrimStatus::NotCompared);
    r.stuStatus.assign(nS, PrimStatus::NotCompared);
    for (int i : refIdx) r.refStatus[i] = PrimStatus::Missing;
    for (int j : stuIdx) r.stuStatus[j] = PrimStatus::Extra;
    r.refPartner.assign(nR, -1);
    r.stuPartner.assign(nS, -1);
    r.stuT.assign(nS, Transform2D::identity());
    r.stuView.assign(nS, -1);

    std::vector<int> allStu(nS);
    for (int j = 0; j < static_cast<int>(nS); ++j) allStu[j] = j;

    // Absolute result is always computed (duplicate detection uses it).
    r.absoluteMatched = countMatches(ref.prims, refIdx, stu.prims, stuIdx, crit);

    const double diag = ref.bbox().diagonal();
    const double radius = s.nearMissRadius > 0 ? s.nearMissRadius : std::max(0.1 * diag, 10 * s.linTol);
    std::vector<ErrorItem> eNear, eAttr, eMissExtra, eChecks, eWarn;

    // ---- Build groups (one per view in Orthographic mode, otherwise one) ----
    std::vector<Group> groups;
    const bool ortho = s.mode == Mode::Orthographic && views && !views->views.empty();
    OrthoResolution res;
    if (!ortho) {
        Group g;
        g.refIdx = refIdx;
        if (s.mode == Mode::Relative) {
            AlignOptions ao;
            ao.allowRotation = s.allowRotation;
            ao.allowMirror = s.allowMirror;
            ao.criteria = crit;
            const AlignResult ar = alignDrawings(ref.prims, refIdx, stu.prims, stuIdx, ao);
            g.T = ar.best.T;
            r.alignCandidates = ar.candidatesTried;
            r.aligned = ar.best.score > 0 || refIdx.empty();
            if (!r.aligned)
                eWarn.push_back({ErrCat::Warning, QStringLiteral("warning: could not align the drawing to the reference "
                                                                 "(no matching geometry); compared in place"),
                                 -1, {}, {}, false});
        }
        // Absolute mode never moves the drawing (T stays identity).
        r.T = g.T;
        r.theta = g.T.rotationAngle();
        r.dx = g.T.tx;
        r.dy = g.T.ty;
        r.mirrored = g.T.isMirrored();
        g.st = transformAll(stu.prims, allStu, g.T);
        for (int j = 0; j < static_cast<int>(nS); ++j) r.stuT[j] = g.T;
        groups.push_back(std::move(g));
    } else {
        res = resolveViews(ref.prims, *views, stu.prims, stuIdx, s, crit);
        for (int v = 0; v < static_cast<int>(views->views.size()); ++v) {
            Group g;
            g.view = v;
            g.prefix = views->label(v) + QStringLiteral(": ");
            for (int i : views->views[v].prims)
                if (r.refStatus[i] != PrimStatus::NotCompared) g.refIdx.push_back(i);
            const int pick = res.chosen[v];
            ViewResult vr;
            vr.view = v;
            vr.label = views->label(v);
            vr.found = pick >= 0;
            if (pick >= 0) g.T = res.candidates[v][pick].T;
            vr.T = g.T;
            vr.refCount = static_cast<int>(g.refIdx.size());
            g.st = transformAll(stu.prims, allStu, g.T);
            r.views.push_back(vr);
            groups.push_back(std::move(g));
        }
    }

    // ---- Pass A: exact matches (geometry + linetype class + layer) ----
    Matching m;
    m.reset(nR, nS);
    // Larger groups first so that shared sizes cannot steal from the bigger view.
    std::vector<int> gorder(groups.size());
    for (int k = 0; k < static_cast<int>(groups.size()); ++k) gorder[k] = k;
    std::stable_sort(gorder.begin(), gorder.end(),
                     [&](int a, int b) { return groups[a].refIdx.size() > groups[b].refIdx.size(); });
    for (int k : gorder) {
        Group& g = groups[k];
        if (ortho && !r.views[k].found) continue;
        matchInto(ref.prims, g.refIdx, g.st, stuIdx, crit, false, m);
    }
    for (int i : refIdx)
        if (m.refToStu[i] >= 0) {
            const int j = m.refToStu[i];
            r.refStatus[i] = r.stuStatus[j] = PrimStatus::Matched;
            r.refPartner[i] = j;
            r.stuPartner[j] = i;
        }
    r.ambiguous = m.ambiguous;
    auto groupOfRef = [&](int i) {
        if (!ortho) return 0;
        return views->primView[i] >= 0 ? views->primView[i] : 0;
    };
    // Student primitives matched by a view belong to it.
    for (int j : stuIdx)
        if (m.stuToRef[j] >= 0) {
            const int gk = groupOfRef(m.stuToRef[j]);
            r.stuView[j] = ortho ? gk : -1;
            r.stuT[j] = groups[gk].T;
        }

    // ---- Pass B: geometry matches with the wrong linetype class / layer / dimension precision ----
    if (crit.matchLinetype || crit.requireLayer || (crit.checkDimensions && crit.checkDimPrecision)) {
        for (int k : gorder) {
            Group& g = groups[k];
            if (ortho && !r.views[k].found) continue;
            matchInto(ref.prims, g.refIdx, g.st, stuIdx, crit, true, m);
        }
        for (int k = 0; k < static_cast<int>(groups.size()); ++k) {
            for (int i : groups[k].refIdx) {
                const int j = m.refToStu[i];
                if (j < 0 || r.refStatus[i] == PrimStatus::Matched) continue;
                r.refStatus[i] = r.stuStatus[j] = PrimStatus::WrongAttribute;
                r.refPartner[i] = j;
                r.stuPartner[j] = i;
                r.stuView[j] = groups[k].view;
                r.stuT[j] = groups[k].T;
                const Primitive& R = ref.prims[i];
                const Primitive& S = groups[k].st[j];
                if (R.kind == PrimKind::Dimension) {
                    eAttr.push_back({ErrCat::Precision, groups[k].prefix + precisionText(R, S), groups[k].view, {i}, {j}, true,
                                     QStringLiteral("wrong precision")});
                    continue;
                }
                const bool ltWrong = crit.matchLinetype && R.ltClass != S.ltClass;
                eAttr.push_back({ltWrong ? ErrCat::Linetype : ErrCat::Layer, groups[k].prefix + attributeText(R, S, crit),
                                 groups[k].view, {i}, {j}, true, ltWrong ? QStringLiteral("wrong linetype") : QStringLiteral("wrong layer")});
            }
        }
    }

    // ---- Attribute unclaimed student primitives to the nearest view (Orthographic) ----
    std::vector<int> unclaimed;
    for (int j : stuIdx)
        if (r.stuStatus[j] == PrimStatus::Extra) unclaimed.push_back(j);
    if (ortho) {
        for (int j : unclaimed) {
            int best = -1;
            double bestD = 1e300;
            const BBox sb = stu.prims[j].bbox();
            for (int k = 0; k < static_cast<int>(groups.size()); ++k) {
                if (!r.views[k].found) continue;
                const BBox vb = views->views[k].bbox.translated(Vec2{-groups[k].T.tx, -groups[k].T.ty});
                const double d = vb.gap(sb);
                if (d < bestD) { bestD = d; best = k; }
            }
            if (best < 0) best = 0;
            r.stuView[j] = best;
            r.stuT[j] = groups[best].T;
        }
    }

    // ---- Near-miss pairing, then whatever remains is missing / extra ----
    for (int k = 0; k < static_cast<int>(groups.size()); ++k) {
        Group& g = groups[k];
        std::vector<int> refU, stuU;
        for (int i : g.refIdx)
            if (r.refStatus[i] == PrimStatus::Missing) refU.push_back(i);
        for (int j : unclaimed)
            if (!ortho || r.stuView[j] == k) stuU.push_back(j);
        if (s.checkHatches) checkHatchGroup(ref, g, refU, stuU, s, r, eNear, eMissExtra, eWarn);
        const auto pairs = pairNearMisses(ref.prims, refU, g.st, stuU, radius, crit);
        for (const Pairing& p : pairs) {
            r.refStatus[p.ref] = PrimStatus::NearMiss;
            r.stuStatus[p.stu] = PrimStatus::NearMiss;
            r.refPartner[p.ref] = p.stu;
            r.stuPartner[p.stu] = p.ref;
            r.stuT[p.stu] = g.T;
            QString label;
            const QString text = nearMissText(ref.prims[p.ref], g.st[p.stu], p.superset, p.displaced, s.linTol, s.angTol, &label);
            eNear.push_back({ErrCat::NearMiss, g.prefix + text, g.view, {p.ref}, {p.stu}, true, label});
        }
        for (int i : refU) {
            if (r.refStatus[i] != PrimStatus::Missing || ref.prims[i].kind == PrimKind::Hatch) continue;
            const Primitive& p = ref.prims[i];
            QString t = p.kind == PrimKind::Dimension
                            ? QStringLiteral("missing dimension %1 (%2) at %3").arg(sizeText(p), p.dimType, fmtPt(p.center))
                            : QStringLiteral("missing %1 at %2").arg(describe(p), fmtPt(messageLocation(p)));
            eMissExtra.push_back({ErrCat::Missing, g.prefix + t, g.view, {i}, {}, true, QStringLiteral("missing")});
        }
        for (int j : stuU) {
            if (r.stuStatus[j] != PrimStatus::Extra || g.st[j].kind == PrimKind::Hatch) continue;
            const Primitive& p = g.st[j];
            QString t = p.kind == PrimKind::Dimension
                            ? QStringLiteral("extra dimension %1 (%2) at %3").arg(sizeText(p), p.dimType, fmtPt(p.center))
                            : QStringLiteral("extra %1 at %2 on layer %3").arg(describe(p), fmtPt(messageLocation(p)), p.layer);
            eMissExtra.push_back({ErrCat::Extra, g.prefix + t, g.view, {}, {j}, s.extras == ExtrasPolicy::Penalize,
                                  QStringLiteral("extra")});
        }
    }

    // ---- Counts and per-view results ----
    for (int i : refIdx) r.matched += r.refStatus[i] == PrimStatus::Matched;
    for (int j : stuIdx) r.penalizedExtras += r.stuStatus[j] == PrimStatus::Extra;
    if (s.extras == ExtrasPolicy::ReportOnly) r.penalizedExtras = 0;
    const int denom = r.refCount + r.penalizedExtras;
    r.correctness = denom > 0 ? 100.0 * r.matched / denom : 100.0;

    if (ortho) {
        for (auto& vr : r.views) {
            for (int i : groups[vr.view].refIdx) vr.matched += r.refStatus[i] == PrimStatus::Matched;
            for (int j : stuIdx) vr.extras += r.stuView[j] == vr.view && r.stuStatus[j] == PrimStatus::Extra;
            const int ex = s.extras == ExtrasPolicy::Penalize ? vr.extras : 0;
            const int d = vr.refCount + ex;
            vr.percent = d > 0 ? 100.0 * vr.matched / d : 100.0;
            vr.pass = vr.matched == vr.refCount && ex == 0;
        }
        // Alignment and arrangement checks from the per-view translations.
        std::vector<const Transform2D*> ts(groups.size(), nullptr);
        for (int k = 0; k < static_cast<int>(groups.size()); ++k)
            if (r.views[k].found) ts[k] = &groups[k].T;
        r.checks = orthoChecks(*views, ts, s.alignTol > 0 ? s.alignTol : s.linTol);
        for (const auto& c : r.checks) {
            if (!c.applicable || c.passed) continue;
            const Severity sev = c.isArrangement ? s.arrangementSeverity : s.alignmentSeverity;
            if (sev == Severity::Off) continue;
            const bool fails = sev == Severity::Fail;
            eChecks.push_back({c.isArrangement ? ErrCat::Arrangement : ErrCat::Alignment,
                               (fails ? QString() : QStringLiteral("warning: ")) + c.text, c.view, {}, {}, fails,
                               !fails ? QString() : c.isArrangement ? QStringLiteral("wrong view arrangement") : QStringLiteral("misaligned view")});
        }
        for (const auto& vr : r.views)
            if (!vr.found && vr.refCount > 0)
                eChecks.push_back({ErrCat::Alignment, vr.label + QStringLiteral(": view not found in the drawing"), vr.view, {}, {}, true,
                                   QStringLiteral("view not found")});
    }

    // ---- Warnings ----
    if (ref.meta.insUnits >= 0 && stu.meta.insUnits >= 0 && ref.meta.insUnits != stu.meta.insUnits)
        eWarn.push_back({ErrCat::Warning,
                         QStringLiteral("warning: units mismatch: drawing is in %1, reference is in %2")
                             .arg(insUnitsName(stu.meta.insUnits), insUnitsName(ref.meta.insUnits)),
                         -1, {}, {}, false});
    for (const auto& w : stu.warnings) eWarn.push_back({ErrCat::Warning, QStringLiteral("warning: ") + w.text, -1, {}, w.prims, false});
    if (r.ambiguous > 0)
        eWarn.push_back({ErrCat::Warning,
                         QStringLiteral("warning: %1 match%2 had more than one candidate within tolerance")
                             .arg(r.ambiguous).arg(r.ambiguous == 1 ? "" : "es"),
                         -1, {}, {}, false});

    // Spec order: near-miss pairs, wrong linetypes, missing/extra, alignment/arrangement, warnings.
    for (auto* list : {&eNear, &eAttr, &eMissExtra, &eChecks, &eWarn})
        for (auto& e : *list) r.errors.push_back(std::move(e));

    bool failedCheck = false;
    for (const auto& e : r.errors)
        if ((e.cat == ErrCat::Alignment || e.cat == ErrCat::Arrangement) && e.failing) failedCheck = true;
    r.pass = r.matched == r.refCount && r.penalizedExtras == 0 && !failedCheck;
    return r;
}

double insUnitsMeters(int u) {
    switch (u) {
    case 1: return 0.0254;
    case 2: return 0.3048;
    case 3: return 1609.344;
    case 4: return 0.001;
    case 5: return 0.01;
    case 6: return 1.0;
    case 7: return 1000.0;
    case 8: return 2.54e-8;
    case 9: return 2.54e-5;
    case 10: return 0.9144;
    case 11: return 1e-10;
    case 12: return 1e-9;
    case 13: return 1e-6;
    case 14: return 0.1;
    case 15: return 10.0;
    case 16: return 100.0;
    default: return 0.0;
    }
}

namespace {

// Factors a scaled submission most likely has: unit conversions and simple multiples.
const std::vector<double>& niceFactors() {
    static const std::vector<double> f = [] {
        std::vector<double> v;
        for (double k : {25.4, 2.54, 10.0, 12.0, 100.0, 1000.0, 304.8, 36.0})
            v.push_back(k), v.push_back(1.0 / k);
        for (int k = 2; k <= 20; ++k) v.push_back(k), v.push_back(1.0 / k);
        return v;
    }();
    return f;
}

double snapFactor(double f) {
    for (double n : niceFactors())
        if (std::fabs(f / n - 1.0) < 2e-3) return n;
    return f;
}

std::vector<double> largestSizes(const Drawing& d, PrimKind kind, int count) {
    std::vector<double> v;
    for (const auto& p : d.prims)
        if (p.kind == kind) v.push_back(kind == PrimKind::Line ? p.length() : p.radius);
    std::sort(v.rbegin(), v.rend());
    if (static_cast<int>(v.size()) > count) v.resize(count);
    return v;
}

Drawing scaledCopy(const Drawing& d, double factor) {
    Drawing out = d;
    const Transform2D t = Transform2D::scaling(factor);
    for (auto& p : out.prims) p = transformed(p, t);
    return out;
}

QString factorText(double f) {
    auto num = [](double v) { return QString::number(v, 'g', 6); };
    if (f >= 1) return num(f) + QChar(0x00D7);
    const double inv = 1.0 / f;
    if (std::fabs(snapFactor(inv) - inv) <= 1e-12 * inv) return QStringLiteral("1/") + num(inv) + QChar(0x00D7);
    return num(f) + QChar(0x00D7);
}

// Matches a scaled copy reaches with the mode's own alignment (cheap ranking of candidates).
int scaledScore(const Drawing& ref, const Drawing& stuScaled, const CompareSettings& s) {
    const MatchCriteria crit = criteriaFrom(s);
    std::vector<int> refIdx, stuIdx;
    for (int i = 0; i < static_cast<int>(ref.prims.size()); ++i)
        if (isGeometric(ref.prims[i].kind)) refIdx.push_back(i);
    for (int j = 0; j < static_cast<int>(stuScaled.prims.size()); ++j)
        if (isGeometric(stuScaled.prims[j].kind)) stuIdx.push_back(j);
    if (s.mode == Mode::Absolute) return countMatches(ref.prims, refIdx, stuScaled.prims, stuIdx, crit);
    AlignOptions ao;
    ao.allowRotation = s.mode == Mode::Relative && s.allowRotation;
    ao.allowMirror = s.mode == Mode::Relative && s.allowMirror;
    ao.criteria = crit;
    return alignDrawings(ref.prims, refIdx, stuScaled.prims, stuIdx, ao).best.score;
}

}  // namespace

std::vector<double> scaleCandidates(const Drawing& ref, const Drawing& stu) {
    std::vector<double> c;
    auto add = [&c](double f) {
        if (!(f > 0) || !std::isfinite(f) || std::fabs(f - 1.0) < 1e-3) return;
        for (double e : c)
            if (std::fabs(e / f - 1.0) < 1e-9) return;
        c.push_back(f);
    };
    // 1. The units the two files declare.
    const double mr = insUnitsMeters(ref.meta.insUnits), ms = insUnitsMeters(stu.meta.insUnits);
    if (mr > 0 && ms > 0 && ref.meta.insUnits != stu.meta.insUnits) add(mr / ms);
    // 2. Sizes measured in the drawings, snapped to a nice factor when within 0.2%.
    const double dr = ref.bbox().diagonal(), ds = stu.bbox().diagonal();
    if (dr > 0 && ds > 0) add(snapFactor(ds / dr));
    for (PrimKind k : {PrimKind::Line, PrimKind::Circle, PrimKind::Arc}) {
        const auto a = largestSizes(ref, k, 3), b = largestSizes(stu, k, 3);
        for (double x : a)
            for (double y : b)
                if (x > 0) add(snapFactor(y / x));
    }
    // 3. Common conversions and multiples.
    for (double n : niceFactors()) add(n);
    return c;
}

CompareResult compareDrawings(const Drawing& ref, const Drawing& stu, const CompareSettings& s, const ViewSet* views) {
    CompareResult base = compareCore(ref, stu, s, views);
    if (!s.detectScale || base.refCount == 0 || base.matched >= 0.5 * base.refCount) return base;
    int geoRef = 0, geoStu = 0;
    for (const auto& p : ref.prims) geoRef += isGeometric(p.kind);
    for (const auto& p : stu.prims) geoStu += isGeometric(p.kind);
    if (geoRef == 0 || geoStu == 0) return base;

    // Rank the candidate factors cheaply, then run the full comparison for the best one.
    // Precision is not checked once the drawing has been rescaled: its numbers are in
    // other units, which normally carry a different number of decimal places.
    CompareSettings ss = s;
    ss.checkDimPrecision = false;
    double bestF = 1;
    int bestScore = 0;
    for (double f : scaleCandidates(ref, stu)) {
        const int sc = scaledScore(ref, scaledCopy(stu, 1.0 / f), ss);
        if (sc > bestScore) { bestScore = sc; bestF = f; }
    }
    const int needed = std::max(3, static_cast<int>(std::ceil(0.5 * geoRef)));
    if (bestF == 1 || bestScore < needed || bestScore <= base.matched + 2) return base;

    CompareResult r = compareCore(ref, scaledCopy(stu, 1.0 / bestF), ss, views);
    if (r.matched <= base.matched) return base;
    r.scaled = true;
    r.scaleFactor = bestF;
    r.absoluteMatched = base.absoluteMatched;  // duplicate detection works on the file as submitted
    // Student primitive -> reference frame now includes undoing the scale.
    const Transform2D undo = Transform2D::scaling(1.0 / bestF);
    for (auto& t : r.stuT) t = t * undo;
    r.T = r.T * undo;
    // The scale error itself: worth scalePenaltyErrors in the correctness value, never a pass.
    const double mr = insUnitsMeters(ref.meta.insUnits), ms = insUnitsMeters(stu.meta.insUnits);
    QString why;
    if (mr > 0 && ms > 0 && ref.meta.insUnits != stu.meta.insUnits && std::fabs((mr / ms) / bestF - 1.0) < 1e-6)
        why = QStringLiteral(" (drawn in %1, the reference is in %2)").arg(insUnitsName(stu.meta.insUnits), insUnitsName(ref.meta.insUnits));
    const QString text = QStringLiteral("drawing is at %1 the reference scale%2; after undoing that, %3 of %4 reference items "
                                        "match, and the errors below are measured at the reference scale")
                             .arg(factorText(bestF), why)
                             .arg(r.matched)
                             .arg(r.refCount);
    if (!why.isEmpty())  // the scale error already explains the units mismatch
        r.errors.erase(std::remove_if(r.errors.begin(), r.errors.end(),
                                      [](const ErrorItem& e) { return e.text.startsWith(QStringLiteral("warning: units mismatch")); }),
                       r.errors.end());
    r.errors.insert(r.errors.begin(), ErrorItem{ErrCat::Scale, text, -1, {}, {}, true, QStringLiteral("wrong scale")});
    const int denom = r.refCount + r.penalizedExtras + std::max(0, s.scalePenaltyErrors);
    r.correctness = denom > 0 ? 100.0 * r.matched / denom : 0.0;
    r.pass = false;
    return r;
}

}  // namespace dc

