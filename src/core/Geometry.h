#pragma once
// Small 2D/3D geometry toolkit used by canonicalization and matching.

#include <cmath>
#include <numbers>

namespace dc {

constexpr double kPi = std::numbers::pi;
constexpr double kTwoPi = 2.0 * std::numbers::pi;

struct Vec2 {
    double x = 0, y = 0;
    constexpr Vec2() = default;
    constexpr Vec2(double x_, double y_) : x(x_), y(y_) {}
    Vec2 operator+(Vec2 o) const { return {x + o.x, y + o.y}; }
    Vec2 operator-(Vec2 o) const { return {x - o.x, y - o.y}; }
    Vec2 operator*(double s) const { return {x * s, y * s}; }
    Vec2 operator/(double s) const { return {x / s, y / s}; }
    Vec2 operator-() const { return {-x, -y}; }
    bool operator==(const Vec2&) const = default;
    double length() const { return std::hypot(x, y); }
    double angle() const { return std::atan2(y, x); }
    Vec2 normalized() const { double l = length(); return l > 0 ? Vec2{x / l, y / l} : Vec2{}; }
    Vec2 perp() const { return {-y, x}; }  // rotated +90 degrees
};
inline double dot(Vec2 a, Vec2 b) { return a.x * b.x + a.y * b.y; }
inline double cross(Vec2 a, Vec2 b) { return a.x * b.y - a.y * b.x; }
inline double dist(Vec2 a, Vec2 b) { return (a - b).length(); }
inline Vec2 fromAngle(double a) { return {std::cos(a), std::sin(a)}; }

struct Vec3 {
    double x = 0, y = 0, z = 0;
    Vec3 operator+(Vec3 o) const { return {x + o.x, y + o.y, z + o.z}; }
    Vec3 operator-(Vec3 o) const { return {x - o.x, y - o.y, z - o.z}; }
    Vec3 operator*(double s) const { return {x * s, y * s, z * s}; }
    double length() const { return std::sqrt(x * x + y * y + z * z); }
    Vec3 normalized() const { double l = length(); return l > 0 ? Vec3{x / l, y / l, z / l} : Vec3{0, 0, 1}; }
    Vec2 xy() const { return {x, y}; }
};
inline Vec3 cross(Vec3 a, Vec3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }

// Affine map in 3D: p' = L p + t (L row-major 3x3).
struct Affine3 {
    double m[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    Vec3 t;
    static Affine3 identity() { return {}; }
    static Affine3 translation(Vec3 v) { Affine3 a; a.t = v; return a; }
    static Affine3 scale(double sx, double sy, double sz);
    static Affine3 rotationZ(double a);
    // AutoCAD "arbitrary axis algorithm": the OCS -> WCS rotation for an extrusion.
    static Affine3 ocsToWcs(Vec3 extrusion);
    Vec3 apply(Vec3 p) const;
    Vec3 applyLinear(Vec3 v) const;
    Affine3 operator*(const Affine3& o) const;  // (this * o)(p) = this(o(p))
    bool isIdentity() const;
};

// Similarity-or-better transform in 2D: p' = A p + t. Used to move a student
// drawing into the reference frame (rigid, optionally mirrored).
struct Transform2D {
    double a = 1, b = 0, c = 0, d = 1;  // [a b; c d]
    double tx = 0, ty = 0;
    static Transform2D identity() { return {}; }
    static Transform2D translation(double dx, double dy) { Transform2D t; t.tx = dx; t.ty = dy; return t; }
    static Transform2D rotation(double theta);
    static Transform2D mirrorX();  // x -> -x
    static Transform2D scaling(double s) { Transform2D t; t.a = s; t.d = s; return t; }  // about the origin
    // Rigid transform that rotates by theta (optionally after mirroring x) and then translates.
    static Transform2D rigid(double theta, double dx, double dy, bool mirror = false);
    Vec2 apply(Vec2 p) const { return {a * p.x + b * p.y + tx, c * p.x + d * p.y + ty}; }
    Vec2 applyLinear(Vec2 v) const { return {a * v.x + b * v.y, c * v.x + d * v.y}; }
    Transform2D operator*(const Transform2D& o) const;  // this after o
    Transform2D inverse() const;
    double det() const { return a * d - b * c; }
    bool isMirrored() const { return det() < 0; }
    // Rotation angle of the linear part (after removing any mirror and scale), in (-pi, pi].
    double rotationAngle() const;
    double scaleFactor() const { return std::sqrt(std::fabs(det())); }
    bool isIdentity() const { return a == 1 && b == 0 && c == 0 && d == 1 && tx == 0 && ty == 0; }
};

struct BBox {
    double x0 = 1e300, y0 = 1e300, x1 = -1e300, y1 = -1e300;
    bool valid() const { return x0 <= x1 && y0 <= y1; }
    void add(Vec2 p) { if (p.x < x0) x0 = p.x; if (p.y < y0) y0 = p.y; if (p.x > x1) x1 = p.x; if (p.y > y1) y1 = p.y; }
    void add(const BBox& o) { if (o.valid()) { add(Vec2{o.x0, o.y0}); add(Vec2{o.x1, o.y1}); } }
    Vec2 center() const { return {(x0 + x1) / 2, (y0 + y1) / 2}; }
    double width() const { return valid() ? x1 - x0 : 0; }
    double height() const { return valid() ? y1 - y0 : 0; }
    double diagonal() const { return valid() ? std::hypot(x1 - x0, y1 - y0) : 0; }
    BBox translated(Vec2 v) const { BBox b = *this; b.x0 += v.x; b.x1 += v.x; b.y0 += v.y; b.y1 += v.y; return b; }
    bool contains(Vec2 p, double tol = 0) const { return p.x >= x0 - tol && p.x <= x1 + tol && p.y >= y0 - tol && p.y <= y1 + tol; }
    // Euclidean distance between boxes (0 when they touch or overlap).
    double gap(const BBox& o) const;
    double distanceTo(Vec2 p) const;
};

// Angle helpers. All angles are radians.
double normalizeAngle(double a);                  // -> [0, 2pi)
double ccwSweep(double from, double to);          // CCW sweep from `from` to `to`, in [0, 2pi)
double angleDiff(double a, double b);             // smallest |a-b| on the circle, in [0, pi]
double lineAngleDiff(double a, double b);         // for undirected lines, in [0, pi/2]
bool angleInSweep(double a, double start, double sweep, double tol);

// Distance from point p to the infinite line through a and b.
double pointLineDistance(Vec2 p, Vec2 a, Vec2 b);
// Distance from point p to the segment ab.
double pointSegmentDistance(Vec2 p, Vec2 a, Vec2 b);

// Polyline bulge segment -> arc (in the polyline's own plane). Returns false for
// a straight segment (bulge ~ 0) or a degenerate chord.
struct BulgeArc { Vec2 center; double radius; double startAngle; double endAngle; };  // CCW start->end
bool bulgeToArc(Vec2 p0, Vec2 p1, double bulge, BulgeArc* out);

}  // namespace dc
