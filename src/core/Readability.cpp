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
    // Model view: the drawing's geometry fitted to the screen, or, with none, everything in it (D105).
    BBox all = d.bbox();
    if (!(std::max(all.width(), all.height()) > 0)) all = d.bbox(false);
    const double extent = std::max(all.width(), all.height());
    const double px = extent > 0 ? kModelViewPixels / extent : 0;  // model-view pixels per drawing unit
    const bool paper = o.onPaper && !d.viewports.empty();
    if (o.onPaper && d.viewports.empty())
        out.push_back({QString(), QStringLiteral("no layout viewport shows the drawing, so its readability was judged in model view"), {}});
    if (!paper && !(extent > 0)) {
        out.push_back({QString(), QStringLiteral("the drawing spans no distance to fit to a screen, so its readability was not judged"), {}});
        return out;
    }
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
            const double repeats = (p.ltRun > 0 ? p.ltRun : p.size()) / pattern;
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
            // An ordinate dimension has no arrowheads (D105).
            const double ratio = p.dimArrowSize / p.dimTextHeight;
            if (p.dimType != QLatin1String("ORDINATE") && (ratio < L.arrowMin - 1e-9 || ratio > L.arrowMax + 1e-9))
                arrows.add(i, std::fabs(std::log(std::max(ratio, 1e-9))));
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
    // The size checks' one shape (D105): how many, how they look, the worst size, where, the limit.
    auto sizeIssue = [&](const Group& g, const char* label, const char* one, const char* more, const char* text, double limit) {
        if (g.prims.empty()) return;
        out.push_back({QLatin1String(label),
                       QString::fromLatin1(text).arg(many(g, one, more), fmt(std::fabs(g.worstKey)), where, num(limit), unit, what(g)),
                       g.prims});
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
    sizeIssue(gaps, "dashes too small", "dashed line looks", "dashed lines look",
              "%1 solid: gaps as short as %2 %3, at least %4 %5 needed (%6)", paper ? L.gapMm : L.gapPx);
    sizeIssue(longGaps, "dashes too long", "dashed line looks", "dashed lines look",
              "%1 broken up: gaps as long as %2 %3, at most %4 %5 (%6)", paper ? L.gapMmMax : L.gapPxMax);
    sizeIssue(dense, "hatch too dense", "hatch looks", "hatches look",
              "%1 solid: lines as close as %2 %3, at least %4 %5 apart needed (%6)", paper ? L.hatchMm : L.hatchPx);
    sizeIssue(coarse, "hatch too coarse", "hatch looks", "hatches look",
              "%1 too coarse: lines as far apart as %2 %3, at most %4 %5 (%6)", paper ? L.hatchMmMax : L.hatchPxMax);
    if (!sparse.prims.empty())
        out.push_back({QStringLiteral("hatch too sparse"),
                       QStringLiteral("%1 hardly hatched: as few as %2 lines across the region, at least %3 needed (%4)")
                           .arg(many(sparse, "hatch looks", "hatches look"), QString::number(-sparse.worstKey, 'f', 1), num(L.hatchLines),
                                what(sparse)),
                       sparse.prims});
    sizeIssue(small, "text too small", "dimension has", "dimensions have",
              "%1 text too small to read: as small as %2 high %3, at least %4 %5 needed (%6)", paper ? L.textMm : L.textPx);
    sizeIssue(large, "text too large", "dimension has", "dimensions have",
              "%1 text too large: as large as %2 high %3, at most %4 %5 (%6)", paper ? L.textMmMax : L.textPxMax);
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
