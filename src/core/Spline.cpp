#include "Spline.h"

#include <algorithm>
#include <limits>

namespace dc {

namespace {

constexpr int kMaxDegree = 15;

// The span k (degree <= k < n) with U[k] <= u < U[k+1]; at the end, the last non-empty one.
int findSpan(const std::vector<double>& U, int p, int n, double u) {
    if (u >= U[n]) {
        int k = n - 1;
        while (k > p && U[k] >= U[k + 1]) --k;
        return k;
    }
    if (u < U[p]) u = U[p];
    const int k = static_cast<int>(std::upper_bound(U.begin() + p, U.begin() + n, u) - U.begin()) - 1;
    return std::clamp(k, p, n - 1);
}

// The der-th derivative of the basis function N_{i,p} at u, by its recursive definition
// (degree 3 here, so the recursion is small). The last span is closed at the end.
double basisDer(const std::vector<double>& U, int p, int i, double u, int der) {
    if (der == 0) {
        if (p == 0) {
            if (U[i] <= u && u < U[i + 1]) return 1.0;
            return u == U.back() && U[i] < U[i + 1] && U[i + 1] == U.back() ? 1.0 : 0.0;
        }
        double a = 0, b = 0;
        if (U[i + p] != U[i]) a = (u - U[i]) / (U[i + p] - U[i]) * basisDer(U, p - 1, i, u, 0);
        if (U[i + p + 1] != U[i + 1]) b = (U[i + p + 1] - u) / (U[i + p + 1] - U[i + 1]) * basisDer(U, p - 1, i + 1, u, 0);
        return a + b;
    }
    double a = 0, b = 0;
    if (U[i + p] != U[i]) a = p / (U[i + p] - U[i]) * basisDer(U, p - 1, i, u, der - 1);
    if (U[i + p + 1] != U[i + 1]) b = p / (U[i + p + 1] - U[i + 1]) * basisDer(U, p - 1, i + 1, u, der - 1);
    return a - b;
}

// Solves A x = b for two right-hand sides (x and y), by elimination with partial pivoting.
bool solve(std::vector<double>& A, std::vector<Vec2>& b) {
    const int n = static_cast<int>(b.size());
    for (int c = 0; c < n; ++c) {
        int piv = c;
        for (int r = c + 1; r < n; ++r)
            if (std::fabs(A[r * n + c]) > std::fabs(A[piv * n + c])) piv = r;
        if (std::fabs(A[piv * n + c]) < 1e-300) return false;
        if (piv != c) {
            for (int k = 0; k < n; ++k) std::swap(A[c * n + k], A[piv * n + k]);
            std::swap(b[c], b[piv]);
        }
        for (int r = c + 1; r < n; ++r) {
            const double f = A[r * n + c] / A[c * n + c];
            if (f == 0) continue;
            for (int k = c; k < n; ++k) A[r * n + k] -= f * A[c * n + k];
            b[r] = b[r] - b[c] * f;
        }
    }
    for (int r = n - 1; r >= 0; --r) {
        Vec2 s = b[r];
        for (int k = r + 1; k < n; ++k) s = s - b[k] * A[r * n + k];
        b[r] = s / A[r * n + r];
    }
    return true;
}

// |C'(u)| by a central difference, for measuring the length.
double speed(const SplineData& s, double u, double h) { return dist(s.at(u + h), s.at(u - h)) / (2 * h); }

// The samples, 16 or more per knot span (at least 32 in all), and the length along them
// (Gauss-Legendre, 4 points per sample interval).
void finish(SplineData& s) {
    std::vector<double> breaks;
    for (size_t k = s.degree; k <= s.ctrl.size(); ++k)
        if (breaks.empty() || s.knots[k] > breaks.back()) breaks.push_back(s.knots[k]);
    const int spans = std::max(1, static_cast<int>(breaks.size()) - 1);
    const int per = std::max(16, (32 + spans - 1) / spans);
    s.su.clear();
    s.sp.clear();
    for (size_t k = 0; k + 1 < breaks.size(); ++k)
        for (int q = 0; q < per; ++q) s.su.push_back(breaks[k] + (breaks[k + 1] - breaks[k]) * q / per);
    s.su.push_back(s.u1());
    for (double u : s.su) s.sp.push_back(s.at(u));
    static const double x[4] = {-0.8611363115940526, -0.3399810435848563, 0.3399810435848563, 0.8611363115940526};
    static const double w[4] = {0.3478548451374538, 0.6521451548625461, 0.6521451548625461, 0.3478548451374538};
    s.length = 0;
    for (size_t i = 0; i + 1 < s.su.size(); ++i) {
        const double a = s.su[i], b = s.su[i + 1], m = (a + b) / 2, r = (b - a) / 2, h = 1e-4 * (b - a);
        for (int g = 0; g < 4; ++g) s.length += w[g] * r * speed(s, m + r * x[g], h);
    }
}

}  // namespace

Vec2 SplineData::at(double u) const {
    const int p = degree, n = static_cast<int>(ctrl.size());
    u = std::clamp(u, u0(), u1());
    const int k = findSpan(knots, p, n, u);
    double dx[kMaxDegree + 1], dy[kMaxDegree + 1], dw[kMaxDegree + 1];
    for (int j = 0; j <= p; ++j) {  // homogeneous points (w x, w y, w)
        const int i = k - p + j;
        const double wt = weights.empty() ? 1.0 : weights[i];
        dx[j] = ctrl[i].x * wt;
        dy[j] = ctrl[i].y * wt;
        dw[j] = wt;
    }
    for (int r = 1; r <= p; ++r)  // de Boor
        for (int j = p; j >= r; --j) {
            const int i = k - p + j;
            const double den = knots[i + p - r + 1] - knots[i];
            const double a = den > 0 ? (u - knots[i]) / den : 0.0;
            dx[j] = (1 - a) * dx[j - 1] + a * dx[j];
            dy[j] = (1 - a) * dy[j - 1] + a * dy[j];
            dw[j] = (1 - a) * dw[j - 1] + a * dw[j];
        }
    return {dx[p] / dw[p], dy[p] / dw[p]};
}

Vec2 SplineData::middle() const {
    double total = 0;
    for (size_t i = 0; i + 1 < sp.size(); ++i) total += dist(sp[i], sp[i + 1]);
    double run = 0;
    for (size_t i = 0; i + 1 < sp.size(); ++i) {
        run += dist(sp[i], sp[i + 1]);
        if (run >= total / 2) return sp[i + 1];
    }
    return sp.front();
}

std::shared_ptr<SplineData> splineThroughPoints(const std::vector<Vec2>& in, int knotParam, Vec2 t0, Vec2 t1) {
    std::vector<Vec2> Q;
    for (Vec2 q : in)
        if (Q.empty() || dist(Q.back(), q) > 1e-12) Q.push_back(q);
    const int n = static_cast<int>(Q.size());
    if (n < 2) return nullptr;
    // Parameters: cumulative chord lengths (or their square roots, or 0, 1, 2...).
    std::vector<double> u(n, 0.0);
    double chord = 0;
    for (int i = 1; i < n; ++i) {
        const double d = dist(Q[i], Q[i - 1]);
        chord += d;
        u[i] = u[i - 1] + (knotParam == 1 ? std::sqrt(d) : knotParam == 2 ? 1.0 : d);
    }
    // A tangent is a unit vector per unit of chord length: the same per unit of parameter
    // for chord knots (as checked), and in proportion for the others.
    const double scale = chord / (u[n - 1] - u[0]);
    const int p = 3, m = n + 2;
    auto s = std::make_shared<SplineData>();
    s->from = SplineFrom::FitPoints;
    s->degree = p;
    s->fit = Q;
    s->knots.assign(p + 1, u[0]);
    for (int i = 1; i + 1 < n; ++i) s->knots.push_back(u[i]);
    s->knots.insert(s->knots.end(), p + 1, u[n - 1]);
    const std::vector<double>& U = s->knots;
    // Rows: the start point, the start condition, the inner points, the end condition, the end.
    std::vector<double> A(static_cast<size_t>(m) * m, 0.0);
    std::vector<Vec2> b(m);
    auto row = [&](int r, double uu, int der, Vec2 rhs) {
        const int k = findSpan(U, p, m, uu);
        for (int j = k - p; j <= k; ++j) A[static_cast<size_t>(r) * m + j] = basisDer(U, p, j, uu, der);
        b[r] = rhs;
    };
    const bool hasT0 = t0.length() > 0, hasT1 = t1.length() > 0;
    row(0, u[0], 0, Q[0]);
    row(1, u[0], hasT0 ? 1 : 2, hasT0 ? t0 * scale : Vec2{});
    for (int i = 1; i + 1 < n; ++i) row(i + 1, u[i], 0, Q[i]);
    row(n, u[n - 1], hasT1 ? 1 : 2, hasT1 ? t1 * scale : Vec2{});
    row(n + 1, u[n - 1], 0, Q[n - 1]);
    if (!solve(A, b)) return nullptr;
    s->ctrl = b;
    // It starts and ends on its first and last points: exactly, not to the solver's rounding,
    // so that it meets the lines there as drawn.
    s->ctrl.front() = Q.front();
    s->ctrl.back() = Q.back();
    finish(*s);
    return s;
}

std::shared_ptr<SplineData> splineFromControlPoints(int degree, std::vector<double> knots, std::vector<Vec2> ctrl,
                                                    std::vector<double> weights) {
    const size_t n = ctrl.size();
    if (degree < 1 || degree > kMaxDegree || n < static_cast<size_t>(degree) + 1 || knots.size() != n + degree + 1) return nullptr;
    for (size_t k = 1; k < knots.size(); ++k)
        if (!(knots[k] >= knots[k - 1])) return nullptr;
    if (!(knots[n] > knots[degree])) return nullptr;
    if (!weights.empty()) {
        if (weights.size() != n) return nullptr;
        bool allOne = true;
        for (double w : weights) {
            if (!(w > 0)) return nullptr;
            allOne = allOne && w == 1.0;
        }
        if (allOne) weights.clear();
    }
    auto s = std::make_shared<SplineData>();
    s->from = SplineFrom::ControlPoints;
    s->degree = degree;
    s->knots = std::move(knots);
    s->ctrl = std::move(ctrl);
    s->weights = std::move(weights);
    finish(*s);
    return s;
}

std::shared_ptr<SplineData> splineFromFrame(const std::vector<Vec2>& frame, int degree, bool closed) {
    const int n = static_cast<int>(frame.size());
    if (n < 2) return nullptr;
    auto s = std::make_shared<SplineData>();
    s->from = SplineFrom::Polyline;
    s->fit = frame;
    if (closed && n > degree) {  // periodic: the first `degree` vertices again, uniform knots
        s->degree = degree;
        s->ctrl = frame;
        for (int i = 0; i < degree; ++i) s->ctrl.push_back(frame[i]);
        for (int k = 0; k < static_cast<int>(s->ctrl.size()) + degree + 1; ++k) s->knots.push_back(k);
    } else {  // clamped at both ends; a lower degree when there are too few vertices
        const int p = std::min(degree, n - 1);
        s->degree = p;
        s->ctrl = frame;
        s->knots.assign(p + 1, 0.0);
        for (int k = 1; k < n - p; ++k) s->knots.push_back(k);
        s->knots.insert(s->knots.end(), p + 1, static_cast<double>(n - p));
    }
    finish(*s);
    return s;
}

void mapSpline(SplineData& s, const Transform2D& t, double lengthScale) {
    for (Vec2& q : s.ctrl) q = t.apply(q);
    for (Vec2& q : s.fit) q = t.apply(q);
    for (Vec2& q : s.sp) q = t.apply(q);  // a point of the curve moves to the moved curve's
    if (lengthScale >= 0)
        s.length *= lengthScale;
    else
        finish(s);  // sampled and measured again (an uneven scale changes the length)
}

double distanceToSpline(const SplineData& s, Vec2 q) {
    const int n = static_cast<int>(s.sp.size());
    if (n == 1) return dist(q, s.sp[0]);
    int bi = 0;
    double best = std::numeric_limits<double>::max();
    for (int i = 0; i + 1 < n; ++i) {
        const double d = pointSegmentDistance(q, s.sp[i], s.sp[i + 1]);
        if (d < best) {
            best = d;
            bi = i;
        }
    }
    // Then on the curve itself near there (golden-section search).
    double lo = s.su[std::max(0, bi - 1)], hi = s.su[std::min(n - 1, bi + 2)];
    const double g = 0.6180339887498949;
    double c = hi - g * (hi - lo), d = lo + g * (hi - lo);
    double fc = dist(s.at(c), q), fd = dist(s.at(d), q);
    for (int it = 0; it < 60; ++it) {
        if (fc < fd) {
            hi = d;
            d = c;
            fd = fc;
            c = hi - g * (hi - lo);
            fc = dist(s.at(c), q);
        } else {
            lo = c;
            c = d;
            fc = fd;
            d = lo + g * (hi - lo);
            fd = dist(s.at(d), q);
        }
    }
    return std::min({fc, fd, dist(q, s.sp[bi]), dist(q, s.sp[bi + 1])});
}

bool splinesMatch(const SplineData& a, const SplineData& b, double tol) {
    const bool ca = a.closed(tol), cb = b.closed(tol);
    if (ca != cb) return false;
    if (!ca) {
        const bool same = dist(a.start(), b.start()) <= tol && dist(a.end(), b.end()) <= tol;
        const bool reversed = dist(a.start(), b.end()) <= tol && dist(a.end(), b.start()) <= tol;
        if (!same && !reversed) return false;
    }
    for (Vec2 q : a.sp)
        if (distanceToSpline(b, q) > tol) return false;
    for (Vec2 q : b.sp)
        if (distanceToSpline(a, q) > tol) return false;
    return true;
}

double splineDeviation(const SplineData& a, const SplineData& b) {
    double d = 0;
    for (Vec2 q : a.sp) d = std::max(d, distanceToSpline(b, q));
    for (Vec2 q : b.sp) d = std::max(d, distanceToSpline(a, q));
    return d;
}

}  // namespace dc
