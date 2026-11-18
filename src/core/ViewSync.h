#pragma once
// The detail view's drawing tabs follow each other's zoom (D107): a tab's view position (the
// point at its center and its zoom) moved into another tab's coordinates. The overlay and the
// solution tab are in the reference frame; the student's file is in its own, where each view
// (Orthographic) or the whole drawing was moved, rotated or rescaled onto the solution's.
// No GUI here, so the tests check the mapping.

#include "Comparator.h"
#include "Ortho.h"

#include <optional>

namespace dc {

// A view position: the drawing point at the view's center and the zoom, in pixels per
// drawing unit.
struct Camera {
    Vec2 center;
    double scale = 0;
};

// The same place in the coordinates `t` takes points to: the center moved by `t`, the zoom
// changed so that a length shows as long as before. Rotation and mirroring are not followed:
// each tab keeps its own orientation.
Camera mapCamera(const Camera& c, const Transform2D& t);

// The transform a view was graded with, reference frame -> the student's file: the inverse of
// the transform of a student item graded in it (one paired with a reference item preferred).
// `view` is -1 outside Orthographic. None when no student item was graded in it.
std::optional<Transform2D> refToStudentOfView(const CompareResult& r, bool ortho, int view);

// Reference frame -> the student's file at reference point `p`: Orthographic, the transform of
// the view whose box holds `p` (the smallest, when several do), else the nearest view that was
// found. Ignored views count: they are located like the others (D108).
std::optional<Transform2D> refToStudentAt(const CompareResult& r, const ViewSet* views, Vec2 p);
// The student's file -> reference frame at student point `p`: Orthographic, the transform of the
// view whose student items' box holds `p` (else the nearest).
std::optional<Transform2D> studentToRefAt(const CompareResult& r, const ViewSet* views, const Drawing& stu, Vec2 p);

}  // namespace dc
