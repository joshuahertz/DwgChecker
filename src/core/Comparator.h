#pragma once
// Per-student comparison: alignment, matching, near-miss pairing, the ordered
// error list, per-view results (Orthographic) and the correctness value.

#include "Matcher.h"
#include "Model.h"
#include "Ortho.h"
#include "Settings.h"

#include <QStringList>

#include <vector>

namespace dc {

enum class ErrCat { Scale, NearMiss, Linetype, Layer, Precision, Missing, Extra, Alignment, Arrangement, Warning };
enum class PrimStatus { Matched, Missing, Extra, NearMiss, WrongAttribute, NotCompared };

struct ErrorItem {
    ErrCat cat = ErrCat::Warning;
    QString text;              // full message, view-prefixed in Orthographic mode
    int view = -1;
    std::vector<int> refPrims;  // indices into reference Drawing::prims
    std::vector<int> stuPrims;  // indices into student Drawing::prims
    bool failing = true;       // false for warnings and Warn-severity checks
    QString label;             // short kind for summaries: "shifted", "missing", "wrong size"...; empty for warnings
};

struct ViewResult {
    int view = -1;
    QString label;
    bool found = false;
    Transform2D T;
    int refCount = 0, matched = 0, extras = 0;
    double percent = 0;
    bool pass = false;
};

struct CompareResult {
    Mode mode = Mode::Relative;
    // Relative / Absolute transform (student -> reference).
    Transform2D T;
    bool aligned = true;
    double theta = 0, dx = 0, dy = 0;
    bool mirrored = false;
    int alignCandidates = 0;
    // Per student primitive: transform into the reference frame, view (Orthographic), status.
    std::vector<Transform2D> stuT;
    std::vector<int> stuView;
    std::vector<PrimStatus> refStatus, stuStatus;
    std::vector<int> refPartner, stuPartner;
    std::vector<ErrorItem> errors;
    std::vector<ViewResult> views;
    std::vector<AlignmentCheck> checks;
    int refCount = 0, matched = 0, penalizedExtras = 0, ambiguous = 0;
    double correctness = 0;
    bool pass = false;
    // Absolute (identity) match count, always computed; used by duplicate detection.
    int absoluteMatched = 0;
    // Wrong scale: the student drawing matched only after multiplying it by 1/scaleFactor.
    bool scaled = false;
    double scaleFactor = 1;  // student size / reference size (25.4: millimeters for an inch reference)

    QStringList errorStrings() const;
    // Counts by label, in the order the errors are listed: "2 shifted, 1 missing; 1 warning".
    QString errorSummary() const;
    // Student primitives of one view (Orthographic), or all compared ones.
    std::vector<int> studentPrimsOfView(int view) const;
};

MatchCriteria criteriaFrom(const CompareSettings& s);
// Indices of the primitives that take part in matching (geometry, plus dimensions when checked).
std::vector<int> comparableIndices(const Drawing& d, const CompareSettings& s);

// Compares at scale 1; if less than half of the reference matches and s.detectScale is on,
// also tries uniform scale factors and keeps a scaled result that matches clearly better
// (reported as one error worth s.scalePenaltyErrors).
CompareResult compareDrawings(const Drawing& ref, const Drawing& stu, const CompareSettings& s, const ViewSet* views);

// Scale factors (student size / reference size) worth trying: the INSUNITS ratio,
// common unit conversions and size ratios measured in the two drawings.
std::vector<double> scaleCandidates(const Drawing& ref, const Drawing& stu);
// Meters per drawing unit for an INSUNITS code (0 when unitless or unknown).
double insUnitsMeters(int insunits);

}  // namespace dc
