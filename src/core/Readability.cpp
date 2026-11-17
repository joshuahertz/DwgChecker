#include "Readability.h"

#include "Format.h"

#include <algorithm>
#include <cmath>

namespace dc {

ReadLimits readLimits(ReadStrictness s) {
    // The upper ends (D102): AutoCAD's own defaults printed 1:1 (0.18" text is 4.6 mm, ANSI31
    // lines 3.2 mm apart, HIDDEN's gaps 3.2 mm) are within every level.
    switch (s) {
    case ReadStrictness::Loose: return {1, 0.3, 8, 1, 100, 0.5, 10, 2, 100, 2, 2.0, 10, 5, 150, 1.0 / 3, 3};
    case ReadStrictness::Strict: return {2, 1.0, 4, 4, 50, 2.0, 4, 5, 50, 5, 3.0, 5, 15, 60, 0.75, 1.25};
    case ReadStrictness::Medium: break;
    }
    return {1, 0.5, 6, 2, 75, 1.0, 6, 3, 75, 3, 2.5, 7, 10, 100, 0.5, 1.5};
}

namespace {

QString num(double v) { return QString::number(v, 'g', 3); }
QString times(double v) { return v == 1 ? QStringLiteral("once") : QStringLiteral("%1 times").arg(num(v)); }

// The objects failing one check, and the worst of them.
struct Group {
    std::vector<int> prims;
    int worst = -1;
    double worstKey = 0;
    void add(int i, double key) {  // a larger key is worse
        if (prims.empty() || key > worstKey) {
            worst = i;
            worstKey = key;
        }
        prims.push_back(i);
    }
};

}  // namespace

QString readLimitsText(ReadStrictness s) {
    const ReadLimits L = readLimits(s);
    auto range = [](double lo, double hi) { return QStringLiteral("%1\u2013%2").arg(num(lo), num(hi)); };
    return QStringLiteral("dashed lines hold their dash pattern at least %1, with gaps of %2 mm on paper or %3 px in model "
                          "view; hatch lines %4 mm or %5 px apart, with at least %6 across each region; dimension text %7 mm "
                          "or %8 px high, with arrowheads %9 times the text height")
        .arg(times(L.dashRepeats), range(L.gapMm, L.gapMmMax), range(L.gapPx, L.gapPxMax), range(L.hatchMm, L.hatchMmMax),
             range(L.hatchPx, L.hatchPxMax), num(L.hatchLines), range(L.textMm, L.textMmMax), range(L.textPx, L.textPxMax),
             range(L.arrowMin, L.arrowMax));
}

std::vector<ReadIssue> readabilityIssues(const Drawing& d, const ReadOptions& o, const std::vector<char>* skip) {
    std::vector<ReadIssue> out;
    if (!o.any()) return out;
    const ReadLimits L = readLimits(o.strictness);
    const BBox all = d.bbox();
    const double extent = std::max(all.width(), all.height());
    const double px = extent > 0 ? kModelViewPixels / extent : 0;  // model-view pixels per drawing unit
    const bool paper = o.onPaper && !d.viewports.empty();
    if (o.onPaper && d.viewports.empty())
        out.push_back({QString(), QStringLiteral("no layout viewport shows the drawing, so its readability was judged in model view"), {}});
    // On paper: the largest viewport showing the object (a main view rather than a detail).
    auto viewportOf = [&](const Primitive& p) -> const LayoutViewport* {
        const Vec2 c = p.bbox().center();
        const LayoutViewport* best = nullptr;
        for (const LayoutViewport& v : d.viewports)
            if (v.window.contains(c) && (!best || v.area > best->area)) best = &v;
        return best;
    };
    // A size as seen: in mm on the sheet through vp, else in model-view pixels.
    auto seen = [&](double size, const LayoutViewport* vp) { return vp ? size * vp->scale * vp->mmPerUnit : size * px; };
    const QString unit = paper ? QStringLiteral("mm") : QStringLiteral("px");
    const QString where = paper ? QStringLiteral("on paper") : QStringLiteral("in model view (the whole drawing 1000 px across)");
    auto fmt = [&](double v) { return QString::number(v, 'f', paper ? 2 : 1) + QLatin1Char(' ') + unit; };

    Group continuous, gaps, longGaps, dense, coarse, sparse, small, large, arrows;
    int unseen = 0;
    for (int i = 0; i < static_cast<int>(d.prims.size()); ++i) {
        if (skip && i < static_cast<int>(skip->size()) && (*skip)[i]) continue;
        const Primitive& p = d.prims[i];
        const bool dashed = o.dashes && isGeometric(p.kind) && p.ltPattern > 0;
        const bool hatch = o.hatches && p.kind == PrimKind::Hatch && p.hatchLines && !p.hatchLines->empty();
        const bool dim = o.dimensions && p.kind == PrimKind::Dimension && p.dimTextHeight > 0;
        if (!dashed && !hatch && !dim) continue;
        const LayoutViewport* vp = paper ? viewportOf(p) : nullptr;
        if (paper && !vp) {  // not printed: nothing to judge
            ++unseen;
            continue;
        }
        if (dashed) {
            // With PSLTSCALE a viewport keeps the dashes their size on the sheet while the line
            // shrinks with the viewport's scale: the pattern then measures less in model units.
            const double pattern = vp && d.psltscale ? p.ltPattern / vp->scale : p.ltPattern;
            const double repeats = std::max(p.size(), p.ltRun) / pattern;
            if (repeats < L.dashRepeats) continuous.add(i, -repeats);
            const double gap = vp ? (d.psltscale ? p.ltGap : p.ltGap * vp->scale) * vp->mmPerUnit : p.ltGap * px;
            if (gap < (vp ? L.gapMm : L.gapPx)) gaps.add(i, -gap);
            else if (gap > (vp ? L.gapMmMax : L.gapPxMax)) longGaps.add(i, gap);
        }
        if (hatch) {
            double spacing = 1e300, across = 1e300;
            for (const HatchFamily& f : *p.hatchLines) {
                spacing = std::min(spacing, f.spacing);
                double lo = 1e300, hi = -1e300;  // the region's extent across this family's lines
                if (p.loops)
                    for (const auto& l : *p.loops)
                        for (Vec2 q : l) {
                            const double t = dot(q, f.dir.perp());
                            lo = std::min(lo, t);
                            hi = std::max(hi, t);
                        }
                if (hi > lo) across = std::min(across, (hi - lo) / f.spacing);
            }
            const double sp = seen(spacing, vp);
            if (sp < (vp ? L.hatchMm : L.hatchPx)) dense.add(i, -sp);
            else if (sp > (vp ? L.hatchMmMax : L.hatchPxMax)) coarse.add(i, sp);
            if (across < 1e300 && across < L.hatchLines) sparse.add(i, -across);
        }
        if (dim) {
            const double h = seen(p.dimTextHeight, vp);
            if (h < (vp ? L.textMm : L.textPx)) small.add(i, -h);
            else if (h > (vp ? L.textMmMax : L.textPxMax)) large.add(i, h);
            const double ratio = p.dimArrowSize / p.dimTextHeight;
            if (ratio < L.arrowMin - 1e-9 || ratio > L.arrowMax + 1e-9) arrows.add(i, std::fabs(std::log(std::max(ratio, 1e-9))));
        }
    }

    auto what = [&](const Group& g) {  // the worst case, named
        const Primitive& p = d.prims[g.worst];
        return QStringLiteral("%1%2 at %3")
            .arg(g.prims.size() > 1 ? QStringLiteral("worst: ") : QString(), describe(p), fmtPt(messageLocation(p)));
    };
    auto many = [](const Group& g, const char* one, const char* more) {
        return g.prims.size() == 1 ? QStringLiteral("1 %1").arg(QLatin1String(one)) : QStringLiteral("%1 %2").arg(g.prims.size()).arg(QLatin1String(more));
    };
    if (!continuous.prims.empty())
        out.push_back({QStringLiteral("dashes don't show"),
                       QStringLiteral("%1 %2 continuous: %3 too short to hold %4 dash pattern %5 (%6 holds it %7 times)")
                           .arg(many(continuous, "dashed line", "dashed lines"),
                                QLatin1String(continuous.prims.size() == 1 ? "looks" : "look"),
                                QLatin1String(continuous.prims.size() == 1 ? "it is" : "they are"),
                                QLatin1String(continuous.prims.size() == 1 ? "its" : "their"), times(L.dashRepeats), what(continuous))
                           .arg(QString::number(-continuous.worstKey, 'f', 2)),
                       continuous.prims});
    if (!gaps.prims.empty())
        out.push_back({QStringLiteral("dashes too small"),
                       QStringLiteral("%1 solid: gaps as short as %2 %3, at least %4 %5 needed (%6)")
                           .arg(many(gaps, "dashed line looks", "dashed lines look"), fmt(-gaps.worstKey), where,
                                num(paper ? L.gapMm : L.gapPx), unit, what(gaps)),
                       gaps.prims});
    if (!longGaps.prims.empty())
        out.push_back({QStringLiteral("dashes too long"),
                       QStringLiteral("%1 broken up: gaps as long as %2 %3, at most %4 %5 (%6)")
                           .arg(many(longGaps, "dashed line looks", "dashed lines look"), fmt(longGaps.worstKey), where,
                                num(paper ? L.gapMmMax : L.gapPxMax), unit, what(longGaps)),
                       longGaps.prims});
    if (!dense.prims.empty())
        out.push_back({QStringLiteral("hatch too dense"),
                       QStringLiteral("%1 solid: lines as close as %2 %3, at least %4 %5 apart needed (%6)")
                           .arg(many(dense, "hatch looks", "hatches look"), fmt(-dense.worstKey), where, num(paper ? L.hatchMm : L.hatchPx),
                                unit, what(dense)),
                       dense.prims});
    if (!coarse.prims.empty())
        out.push_back({QStringLiteral("hatch too coarse"),
                       QStringLiteral("%1 too coarse: lines as far apart as %2 %3, at most %4 %5 (%6)")
                           .arg(many(coarse, "hatch looks", "hatches look"), fmt(coarse.worstKey), where,
                                num(paper ? L.hatchMmMax : L.hatchPxMax), unit, what(coarse)),
                       coarse.prims});
    if (!sparse.prims.empty())
        out.push_back({QStringLiteral("hatch too sparse"),
                       QStringLiteral("%1 hardly hatched: as few as %2 lines across the region, at least %3 needed (%4)")
                           .arg(many(sparse, "hatch looks", "hatches look"), QString::number(-sparse.worstKey, 'f', 1), num(L.hatchLines),
                                what(sparse)),
                       sparse.prims});
    if (!small.prims.empty())
        out.push_back({QStringLiteral("text too small"),
                       QStringLiteral("%1 text too small to read: as small as %2 high %3, at least %4 %5 needed (%6)")
                           .arg(many(small, "dimension has", "dimensions have"), fmt(-small.worstKey), where, num(paper ? L.textMm : L.textPx),
                                unit, what(small)),
                       small.prims});
    if (!large.prims.empty())
        out.push_back({QStringLiteral("text too large"),
                       QStringLiteral("%1 text too large: as large as %2 high %3, at most %4 %5 (%6)")
                           .arg(many(large, "dimension has", "dimensions have"), fmt(large.worstKey), where,
                                num(paper ? L.textMmMax : L.textPxMax), unit, what(large)),
                       large.prims});
    if (!arrows.prims.empty()) {
        const Primitive& w = d.prims[arrows.worst];
        out.push_back({QStringLiteral("arrows out of proportion"),
                       QStringLiteral("%1 arrowheads out of proportion: %2 times the text height, %3 to %4 times expected (%5)")
                           .arg(many(arrows, "dimension has", "dimensions have"), QString::number(w.dimArrowSize / w.dimTextHeight, 'f', 2),
                                num(L.arrowMin), num(L.arrowMax), what(arrows)),
                       arrows.prims});
    }
    if (unseen > 0)
        out.push_back({QString(),
                       QStringLiteral("%1 not shown in any layout viewport, so %2 not judged for readability")
                           .arg(unseen == 1 ? QStringLiteral("1 object is") : QStringLiteral("%1 objects are").arg(unseen),
                                QLatin1String(unseen == 1 ? "it was" : "they were")),
                       {}});
    return out;
}

}  // namespace dc
