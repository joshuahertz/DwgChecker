#pragma once
// Optional hatch check: is each hatched region of the reference also hatched in
// the student drawing (and, optionally, with the same pattern)?
//
// Regions are compared by area, not by boundary entities, so a region hatched by
// picking a point, by selecting objects, by drawing a boundary, or in two pieces
// all compare equal. Coverage is measured on a grid of interior sample points
// kept a small margin away from the region's boundary (tolerance and arc
// flattening never decide the result).

#include "Model.h"

#include <QStringList>

#include <vector>

namespace dc {

struct HatchRefResult {
    int ref = -1;
    bool sampled = false;          // false: region too small or thin to sample
    double coverage = 0;           // fraction of the region hatched in the student drawing
    std::vector<int> contributors; // student hatches covering at least 5% of the region
    QStringList patterns;          // their (normalized) pattern names
    Vec2 centroid;                 // of the sampled area, for messages
};

struct HatchStuResult {
    int stu = -1;
    bool sampled = false;
    double outside = 0;  // fraction of this student hatch outside every reference region
    Vec2 centroid;
};

struct HatchCheckResult {
    std::vector<HatchRefResult> refs;
    std::vector<HatchStuResult> stus;
};

// Thresholds (see DECISIONS.md).
constexpr double kHatchFilled = 0.995;   // coverage at or above: filled
constexpr double kHatchAbsent = 0.02;    // coverage below: not hatched at all
constexpr double kHatchSpill = 0.005;    // outside fraction above: spills outside the region
constexpr double kHatchContributor = 0.05;

double hatchMargin(const Primitive& hatch, double linTol);
std::vector<Vec2> hatchSamples(const Primitive& hatch, double margin, int target = 2500);

// `ref`/`stu` hold primitives already in the reference frame; refH/stuH index their hatches.
HatchCheckResult checkHatchRegions(const std::vector<Primitive>& ref, const std::vector<int>& refH,
                                   const std::vector<Primitive>& stu, const std::vector<int>& stuH, double linTol);

}  // namespace dc
