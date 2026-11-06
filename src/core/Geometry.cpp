#include "Geometry.h"

#include <algorithm>

namespace dc {

Affine3 Affine3::scale(double sx, double sy, double sz) {
    Affine3 a;
    a.m[0][0] = sx; a.m[1][1] = sy; a.m[2][2] = sz;
    return a;
}

Affine3 Affine3::rotationZ(double ang) {
    Affine3 a;
    const double c = std::cos(ang), s = std::sin(ang);
    a.m[0][0] = c; a.m[0][1] = -s;
    a.m[1][0] = s; a.m[1][1] = c;
    return a;
}

Affine3 Affine3::ocsToWcs(Vec3 n) {
    n = n.normalized();
    Affine3 a;
    if (std::fabs(n.x) < 1e-12 && std::fabs(n.y) < 1e-12 && n.z > 0) return a;  // exact identity for +Z
    Vec3 ax;
    if (std::fabs(n.x) < 1.0 / 64.0 && std::fabs(n.y) < 1.0 / 64.0)
        ax = cross(Vec3{0, 1, 0}, n).normalized();
    else
        ax = cross(Vec3{0, 0, 1}, n).normalized();
    const Vec3 ay = cross(n, ax).normalized();
    // Columns are the OCS axes expressed in WCS.
    a.m[0][0] = ax.x; a.m[0][1] = ay.x; a.m[0][2] = n.x;
    a.m[1][0] = ax.y; a.m[1][1] = ay.y; a.m[1][2] = n.y;
    a.m[2][0] = ax.z; a.m[2][1] = ay.z; a.m[2][2] = n.z;
    return a;
}

Vec3 Affine3::applyLinear(Vec3 v) const {
    return {m[0][0] * v.x + m[0][1] * v.y + m[0][2] * v.z,
            m[1][0] * v.x + m[1][1] * v.y + m[1][2] * v.z,
            m[2][0] * v.x + m[2][1] * v.y + m[2][2] * v.z};
}

Vec3 Affine3::apply(Vec3 p) const { return applyLinear(p) + t; }

Affine3 Affine3::operator*(const Affine3& o) const {
    Affine3 r;
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) {
            double s = 0;
            for (int k = 0; k < 3; ++k) s += m[i][k] * o.m[k][j];
            r.m[i][j] = s;
        }
    r.t = applyLinear(o.t) + t;
    return r;
}

bool Affine3::isIdentity() const {
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            if (m[i][j] != (i == j ? 1.0 : 0.0)) return false;
    return t.x == 0 && t.y == 0 && t.z == 0;
}

Transform2D Transform2D::rotation(double theta) {
    Transform2D t;
    const double c = std::cos(theta), s = std::sin(theta);
    t.a = c; t.b = -s; t.c = s; t.d = c;
    return t;
}

Transform2D Transform2D::mirrorX() {
    Transform2D t;
    t.a = -1;
    return t;
}

Transform2D Transform2D::rigid(double theta, double dx, double dy, bool mirror) {
    Transform2D r = rotation(theta);
    if (mirror) r = r * mirrorX();
    r.tx = dx; r.ty = dy;
    return r;
}

Transform2D Transform2D::operator*(const Transform2D& o) const {
    Transform2D r;
    r.a = a * o.a + b * o.c; r.b = a * o.b + b * o.d;
    r.c = c * o.a + d * o.c; r.d = c * o.b + d * o.d;
    r.tx = a * o.tx + b * o.ty + tx;
    r.ty = c * o.tx + d * o.ty + ty;
    return r;
}

Transform2D Transform2D::inverse() const {
    const double D = det();
    Transform2D r;
    r.a = d / D; r.b = -b / D; r.c = -c / D; r.d = a / D;
    r.tx = -(r.a * tx + r.b * ty);
    r.ty = -(r.c * tx + r.d * ty);
    return r;
}

double Transform2D::rotationAngle() const {
    // rigid(theta, mirror) = R(theta) * diag(-1, 1) = [-cos -sin; -sin cos]
    if (isMirrored()) return std::atan2(-c, -a);
    return std::atan2(c, a);
}

double BBox::gap(const BBox& o) const {
    const double dx = std::max({0.0, o.x0 - x1, x0 - o.x1});
    const double dy = std::max({0.0, o.y0 - y1, y0 - o.y1});
    return std::hypot(dx, dy);
}

double BBox::distanceTo(Vec2 p) const {
    const double dx = std::max({0.0, x0 - p.x, p.x - x1});
    const double dy = std::max({0.0, y0 - p.y, p.y - y1});
    return std::hypot(dx, dy);
}

double normalizeAngle(double a) {
    if (!std::isfinite(a)) return 0;
    double r = std::fmod(a, kTwoPi);
    if (r < 0) r += kTwoPi;
    if (r >= kTwoPi) r -= kTwoPi;
    return r;
}

double ccwSweep(double from, double to) { return normalizeAngle(to - from); }

double angleDiff(double a, double b) {
    double d = std::fabs(normalizeAngle(a) - normalizeAngle(b));
    return d > kPi ? kTwoPi - d : d;
}

double lineAngleDiff(double a, double b) {
    double d = std::fmod(std::fabs(a - b), kPi);
    return d > kPi / 2 ? kPi - d : d;
}

bool angleInSweep(double a, double start, double sweep, double tol) {
    const double rel = ccwSweep(start, a);
    return rel <= sweep + tol || rel >= kTwoPi - tol;
}

double pointLineDistance(Vec2 p, Vec2 a, Vec2 b) {
    const Vec2 ab = b - a;
    const double l = ab.length();
    if (l == 0) return dist(p, a);
    return std::fabs(cross(ab, p - a)) / l;
}

double pointSegmentDistance(Vec2 p, Vec2 a, Vec2 b) {
    const Vec2 ab = b - a;
    const double l2 = dot(ab, ab);
    if (l2 == 0) return dist(p, a);
    const double t = std::clamp(dot(p - a, ab) / l2, 0.0, 1.0);
    return dist(p, a + ab * t);
}

bool bulgeToArc(Vec2 p0, Vec2 p1, double bulge, BulgeArc* out) {
    const Vec2 chord = p1 - p0;
    const double d = chord.length();
    if (std::fabs(bulge) < 1e-12 || d == 0) return false;
    // Center = chord midpoint + perp(chord) * (1 - b^2) / (4b). Left of the chord for b > 0.
    const Vec2 c = p0 + chord * 0.5 + chord.perp() * ((1.0 - bulge * bulge) / (4.0 * bulge));
    out->center = c;
    out->radius = d * (1.0 + bulge * bulge) / (4.0 * std::fabs(bulge));
    const double a0 = (p0 - c).angle(), a1 = (p1 - c).angle();
    if (bulge > 0) { out->startAngle = normalizeAngle(a0); out->endAngle = normalizeAngle(a1); }
    else { out->startAngle = normalizeAngle(a1); out->endAngle = normalizeAngle(a0); }
    return true;
}

}  // namespace dc
