#pragma once
// Readability (D101): whether a drawing's dashed lines, hatching and dimensions can be seen.
// Judged in the file on its own, not against the solution: in model view (the whole drawing
// fitted to a screen kModelViewPixels across) or as printed (through the layout viewport
// that shows each object), at one of three strictness levels.

#include "Model.h"

#include <QString>

#include <vector>

namespace dc {

enum class ReadStrictness { Loose, Medium, Strict };

// One level's limits: sizes within a range, on paper (mm) and in model view (px) (D102).
struct ReadLimits {
    double dashRepeats;               // a dashed line holds its dash pattern at least this many times...
    double gapMm, gapMmMax;           // ...with gaps this long on paper...
    double gapPx, gapPxMax;           // ...or in model view
    double hatchMm, hatchMmMax;       // hatch lines this far apart
    double hatchPx, hatchPxMax;
    double hatchLines;                // ...and at least this many across each hatched region
    double textMm, textMmMax;         // dimension text this high
    double textPx, textPxMax;
    double arrowMin, arrowMax;        // arrowheads this many times the text height
};
ReadLimits readLimits(ReadStrictness s);
// One level's limits in words, for the setting's tooltip.
QString readLimitsText(ReadStrictness s);

// Model view: the whole drawing, its longer side this many pixels across.
constexpr double kModelViewPixels = 1000;

struct ReadOptions {
    bool dashes = false, hatches = false, dimensions = false;
    ReadStrictness strictness = ReadStrictness::Medium;
    bool onPaper = false;  // as printed through the layout viewports, else in model view
    bool any() const { return dashes || hatches || dimensions; }
};

struct ReadIssue {
    QString label;           // "dashes don't show", ...; empty for a note
    QString text;
    std::vector<int> prims;  // the drawing's objects it is about
};

// What in d is hard to see: one issue per problem, naming the worst case. skip (optional, per
// object): objects not to judge (an Ignored view's).
std::vector<ReadIssue> readabilityIssues(const Drawing& d, const ReadOptions& o, const std::vector<char>* skip = nullptr);

}  // namespace dc
