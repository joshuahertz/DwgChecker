#pragma once
// Number and primitive formatting for human-readable error messages.

#include "Model.h"

#include <QString>

#include <limits>

namespace dc {

// Fixed 3 decimals, with more digits when needed so that `v` and `other` print differently.
QString fmtLen(double v, double other = std::numeric_limits<double>::quiet_NaN());
// Up to 3 decimals, trailing zeros trimmed but keeping one decimal: (3.0, 2.5), (0.125, 0.0).
// With `other`, more digits (up to 8) until the two print differently.
QString fmtCoord(double v, double other = std::numeric_limits<double>::quiet_NaN());
QString fmtPt(Vec2 p);
// An offset between two places: each part with enough digits not to print as 0 when it is
// not, "(0.0, -0.00003)", however tight the tolerance.
QString fmtOffset(Vec2 d);
// Degrees with 2 decimals; with `other`, more (up to 6) until the two print differently.
// fmtDeg(turn, 0) for a small rotation: "0.0006°", not "0.00°".
QString fmtDeg(double radians, double other = std::numeric_limits<double>::quiet_NaN());

// "hidden edge", "visible line", "centerline", "hidden arc", "circle" ...
QString lineDescriptor(const Primitive& p, bool forLinetypeError = false);
// Short description with size: "hidden line 1.250 long", "circle Ø0.750", "arc R0.500".
QString describe(const Primitive& p);
// A representative location for messages: line midpoint, arc/circle center.
Vec2 messageLocation(const Primitive& p);
// Full geometry of a primitive for inspection: "(0.0, 0.0) -> (4.0, 0.0), length 4.000".
QString describeGeometry(const Primitive& p);

}  // namespace dc
