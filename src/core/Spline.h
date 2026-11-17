#pragma once
// Splines (D99). Every spline is kept as the NURBS curve it is (degree, knots, control
// points, weights), whether it was drawn through fit points (AutoCAD's default: the file
// keeps only those, and the curve is rebuilt here as AutoCAD builds it), by its control
// points, or as a spline-fit polyline (PEDIT Spline). The curve is sampled once, for
// drawing it, its box, and comparing it with another.

#include "Geometry.h"

#include <memory>
#include <vector>

namespace dc {

enum class SplineFrom { FitPoints, ControlPoints, Polyline };

struct SplineData {
    SplineFrom from = SplineFrom::ControlPoints;
    int degree = 3;
    std::vector<double> knots;
    std::vector<Vec2> ctrl;
    std::vector<double> weights;  // one per control point; empty when it is not rational
    // FitPoints: the points it was drawn through. Polyline: the polyline's own vertices.
    std::vector<Vec2> fit;
    // The curve sampled from start to end: parameters, and the points on it there.
    std::vector<double> su;
    std::vector<Vec2> sp;
    double length = 0;

    double u0() const { return knots[degree]; }
    double u1() const { return knots[ctrl.size()]; }
    Vec2 at(double u) const;  // the point at parameter u, kept within [u0, u1]
    Vec2 start() const { return sp.front(); }
    Vec2 end() const { return sp.back(); }
    bool closed(double tol) const { return dist(start(), end()) <= tol; }
    // A point on the curve halfway along it.
    Vec2 middle() const;
    // How many points it was drawn through: a closed one keeps its first point again at the end.
    size_t fitCount() const { return fit.size() > 2 && fit.front() == fit.back() ? fit.size() - 1 : fit.size(); }
};

// AutoCAD's fit-point spline: the cubic through the points, with knots at the cumulative
// chord lengths between them (knotParam 0; 1: their square roots; 2: uniform). A given end
// tangent (a unit vector, as the file keeps it) sets the curve's direction there; with none
// (zero), the curve does not bend at that end (second derivative 0). Checked against
// AutoCAD's own control points to 1e-14 (D99). Null when there are fewer than 2 points.
std::shared_ptr<SplineData> splineThroughPoints(const std::vector<Vec2>& fit, int knotParam, Vec2 startTangent,
                                                Vec2 endTangent);
// A spline given by its control points. Null when they do not make a curve (counts that do
// not agree, knots that go back, a weight that is not positive).
std::shared_ptr<SplineData> splineFromControlPoints(int degree, std::vector<double> knots, std::vector<Vec2> ctrl,
                                                    std::vector<double> weights);
// A spline-fit polyline: the uniform B-spline of degree 2 or 3 of its vertices, from the
// first to the last (open), or round all of them (closed). Checked against the points
// AutoCAD computes for it (D99).
std::shared_ptr<SplineData> splineFromFrame(const std::vector<Vec2>& frame, int degree, bool closed);

// Moves the spline by t (any affine map: a spline is moved with its control points). Its
// length is multiplied by lengthScale, or measured again when that is negative.
void mapSpline(SplineData& s, const Transform2D& t, double lengthScale);
// Distance from q to the curve.
double distanceToSpline(const SplineData& s, Vec2 q);
// The same curve within tol, however it was defined: the same ends (either way round; a
// closed one may start anywhere), and every sampled point of each within tol of the other.
bool splinesMatch(const SplineData& a, const SplineData& b, double tol);
// How far apart two curves are: the largest distance from a sampled point of either to the
// other curve.
double splineDeviation(const SplineData& a, const SplineData& b);

}  // namespace dc
