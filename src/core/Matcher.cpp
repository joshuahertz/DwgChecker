#include "Matcher.h"

#include "Color.h"

#include "DimText.h"
#include "SpatialGrid.h"

#include <algorithm>
#include <limits>

namespace dc {

namespace {

bool arcAnglesMatch(const Primitive& r, const Primitive& s, double linTol, double angTol) {
    const bool startOk = angleDiff(r.a0, s.a0) <= angTol || dist(r.arcStart(), s.arcStart()) <= linTol;
    const bool endOk = angleDiff(r.a1, s.a1) <= angTol || dist(r.arcEnd(), s.arcEnd()) <= linTol;
    return startOk && endOk;
}

// Loose centerline test: s lies on r's infinite line and the two overlap.
bool looseLineMatch(const Primitive& r, const Primitive& s, double tol) {
    if (pointLineDistance(s.p0, r.p0, r.p1) > tol || pointLineDistance(s.p1, r.p0, r.p1) > tol) return false;
    const Vec2 d = (r.p1 - r.p0).normalized();
    const double r0 = std::min(dot(r.p0, d), dot(r.p1, d)), r1 = std::max(dot(r.p0, d), dot(r.p1, d));
    const double s0 = std::min(dot(s.p0, d), dot(s.p1, d)), s1 = std::max(dot(s.p0, d), dot(s.p1, d));
    return std::min(r1, s1) - std::max(r0, s0) > tol;
}

double lineEndpointError(const Primitive& r, const Primitive& s) {
    return std::min(dist(r.p0, s.p0) + dist(r.p1, s.p1), dist(r.p0, s.p1) + dist(r.p1, s.p0));
}

double matchCost(const Primitive& r, const Primitive& s) {
    switch (r.kind) {
    case PrimKind::Line: return lineEndpointError(r, s);
    case PrimKind::Circle: return dist(r.center, s.center) + std::fabs(r.radius - s.radius);
    case PrimKind::Arc: return dist(r.center, s.center) + std::fabs(r.radius - s.radius) + dist(r.arcStart(), s.arcStart());
    case PrimKind::Dimension: return std::fabs(r.measurement - s.measurement) * 1e6 + dist(r.center, s.center);
    default: return 0;
    }
}

Vec2 keyPoint(const Primitive& p) { return p.kind == PrimKind::Line ? p.midpoint() : p.center; }

}  // namespace

bool usesLooseCenterline(const Primitive& r, const MatchCriteria& c) {
    return r.kind == PrimKind::Line && r.ltClass == LtClass::Center && !c.strictCenterlines;
}

bool geometryMatches(const Primitive& r, const Primitive& s, const MatchCriteria& c) {
    if (r.kind != s.kind) return false;
    switch (r.kind) {
    case PrimKind::Line:
        if (usesLooseCenterline(r, c)) return looseLineMatch(r, s, c.linTol);
        return (dist(r.p0, s.p0) <= c.linTol && dist(r.p1, s.p1) <= c.linTol) ||
               (dist(r.p0, s.p1) <= c.linTol && dist(r.p1, s.p0) <= c.linTol);
    case PrimKind::Circle:
        return dist(r.center, s.center) <= c.linTol && std::fabs(r.radius - s.radius) <= c.linTol;
    case PrimKind::Arc:
        return dist(r.center, s.center) <= c.linTol && std::fabs(r.radius - s.radius) <= c.linTol &&
               arcAnglesMatch(r, s, c.linTol, c.angTol);
    case PrimKind::Dimension:
        if (r.dimType != s.dimType) return false;
        return std::fabs(r.measurement - s.measurement) <= (isAngularDimType(r.dimType) ? c.angTol : c.linTol);
    default: return false;
    }
}

bool attributesMatch(const Primitive& r, const Primitive& s, const MatchCriteria& c) {
    if (r.kind == PrimKind::Dimension) return !(c.checkDimensions && c.checkDimPrecision) || !dimPrecisionDiffers(r, s);
    if (c.matchLinetype && r.ltClass != s.ltClass) return false;
    if (c.requireLayer && r.layer.compare(s.layer, Qt::CaseInsensitive) != 0) return false;
    if (c.matchColor && colorDifference(r.rgb, s.rgb) > c.colorTol + 1e-9) return false;
    if (c.requireByLayer && !(s.colorByLayer && s.ltByLayer)) return false;
    return true;
}

void matchInto(const std::vector<Primitive>& ref, const std::vector<int>& refIdx, const std::vector<Primitive>& stu,
               const std::vector<int>& stuIdx, const MatchCriteria& c, bool geometryOnly, Matching& m) {
    if (m.refToStu.size() != ref.size() || m.stuToRef.size() != stu.size()) m.reset(ref.size(), stu.size());
    SpatialGrid grid(std::max(4 * c.linTol, 1e-9));
    std::vector<int> lines, dims;
    for (int j : stuIdx) {
        if (m.stuToRef[j] >= 0) continue;
        const Primitive& s = stu[j];
        if (isGeometric(s.kind)) grid.insert(keyPoint(s), j);
        if (s.kind == PrimKind::Line) lines.push_back(j);
        if (s.kind == PrimKind::Dimension) dims.push_back(j);
    }
    // Strict primitives first, then loose centerlines (which have more freedom).
    std::vector<int> order;
    order.reserve(refIdx.size());
    for (int i : refIdx)
        if (!usesLooseCenterline(ref[i], c)) order.push_back(i);
    for (int i : refIdx)
        if (usesLooseCenterline(ref[i], c)) order.push_back(i);

    for (int i : order) {
        if (m.refToStu[i] >= 0) continue;
        const Primitive& r = ref[i];
        if (r.kind == PrimKind::Dimension && !c.checkDimensions) continue;
        if (!isGeometric(r.kind) && r.kind != PrimKind::Dimension) continue;
        int best = -1, candidates = 0;
        double bestCost = std::numeric_limits<double>::max();
        auto consider = [&](int j) {
            if (m.stuToRef[j] >= 0) return;
            const Primitive& s = stu[j];
            if (!geometryMatches(r, s, c)) return;
            if (!geometryOnly && !attributesMatch(r, s, c)) return;
            ++candidates;
            const double cost = matchCost(r, s);
            if (cost < bestCost) { bestCost = cost; best = j; }
        };
        if (r.kind == PrimKind::Dimension) for (int j : dims) consider(j);
        else if (usesLooseCenterline(r, c)) {
            const BBox rb = r.bbox();
            for (int j : lines)
                if (rb.gap(stu[j].bbox()) <= c.linTol) consider(j);
        } else grid.forNear(keyPoint(r), consider);
        if (best >= 0) {
            m.refToStu[i] = best;
            m.stuToRef[best] = i;
            ++m.matched;
            if (candidates > 1) ++m.ambiguous;
        }
    }
}

int countMatches(const std::vector<Primitive>& ref, const std::vector<int>& refIdx, const std::vector<Primitive>& stu,
                 const std::vector<int>& stuIdx, const MatchCriteria& c) {
    Matching m;
    m.reset(ref.size(), stu.size());
    matchInto(ref, refIdx, stu, stuIdx, c, true, m);
    return m.matched;
}

std::vector<Pairing> pairNearMisses(const std::vector<Primitive>& ref, const std::vector<int>& refUnmatched,
                                    const std::vector<Primitive>& stu, const std::vector<int>& stuUnmatched,
                                    double radius, const MatchCriteria& c) {
    std::vector<Pairing> out;
    std::vector<char> stuUsed(stu.size(), 0), refUsed(ref.size(), 0);

    // 1. Superset lines: a longer collinear student line that contains the reference
    //    line (typically an edge merged with a projection line). Many-to-one.
    for (int i : refUnmatched) {
        const Primitive& r = ref[i];
        if (r.kind != PrimKind::Line) continue;
        const Vec2 d = (r.p1 - r.p0).normalized();
        const double r0 = std::min(dot(r.p0, d), dot(r.p1, d)), r1 = std::max(dot(r.p0, d), dot(r.p1, d));
        for (int j : stuUnmatched) {
            const Primitive& s = stu[j];
            if (s.kind != PrimKind::Line) continue;
            if (pointLineDistance(r.p0, s.p0, s.p1) > c.linTol || pointLineDistance(r.p1, s.p0, s.p1) > c.linTol) continue;
            const double s0 = std::min(dot(s.p0, d), dot(s.p1, d)), s1 = std::max(dot(s.p0, d), dot(s.p1, d));
            if (s0 <= r0 + c.linTol && s1 >= r1 - c.linTol && (s1 - s0) > (r1 - r0) + c.linTol) {
                out.push_back({i, j, 0, true});
                refUsed[i] = 1;
                stuUsed[j] = 1;
                break;
            }
        }
    }

    // 2. Closest same-kind candidates within the search radius, greedy by score.
    std::vector<Pairing> cand;
    for (int i : refUnmatched) {
        if (refUsed[i]) continue;
        const Primitive& r = ref[i];
        if (!isGeometric(r.kind) && !(r.kind == PrimKind::Dimension && c.checkDimensions)) continue;
        for (int j : stuUnmatched) {
            if (stuUsed[j]) continue;
            const Primitive& s = stu[j];
            if (s.kind != r.kind) continue;
            double score = 0;
            switch (r.kind) {
            case PrimKind::Line: {
                score = lineEndpointError(r, s) / 2;
                if (score > radius) continue;
                break;
            }
            case PrimKind::Circle:
                if (dist(r.center, s.center) > radius) continue;
                score = dist(r.center, s.center) + std::fabs(r.radius - s.radius);
                break;
            case PrimKind::Arc:
                if (dist(r.center, s.center) > radius) continue;
                score = dist(r.center, s.center) + std::fabs(r.radius - s.radius) +
                        (dist(r.arcStart(), s.arcStart()) + dist(r.arcEnd(), s.arcEnd())) / 2;
                break;
            case PrimKind::Dimension:
                if (r.dimType != s.dimType || dist(r.center, s.center) > radius) continue;
                score = std::fabs(r.measurement - s.measurement) + dist(r.center, s.center) * 0.01;
                break;
            default: continue;
            }
            cand.push_back({i, j, score, false});
        }
    }
    std::sort(cand.begin(), cand.end(), [](const Pairing& a, const Pairing& b) {
        if (a.score != b.score) return a.score < b.score;
        if (a.ref != b.ref) return a.ref < b.ref;
        return a.stu < b.stu;
    });
    for (const Pairing& p : cand) {
        if (refUsed[p.ref] || stuUsed[p.stu]) continue;
        refUsed[p.ref] = stuUsed[p.stu] = 1;
        out.push_back(p);
    }

    // 3. Displaced copies at any distance: what is left and has the reference size (a
    //    line of the same length, a circle of the same radius, an arc of the same radius
    //    and sweep), turned by at most kDisplacedMaxTurn, was drawn in the wrong place.
    //    Typical when an earlier line is wrong and the rest of the chain starts from its
    //    end, often at a slightly different angle. Nearest first; less turn wins ties.
    cand.clear();
    for (int i : refUnmatched) {
        if (refUsed[i]) continue;
        const Primitive& r = ref[i];
        if (!isGeometric(r.kind)) continue;
        for (int j : stuUnmatched) {
            if (stuUsed[j]) continue;
            const Primitive& s = stu[j];
            if (s.kind != r.kind) continue;
            double turn = -1;
            switch (r.kind) {
            case PrimKind::Line:
                if (r.length() > c.linTol && std::fabs(r.length() - s.length()) <= c.linTol)
                    turn = lineAngleDiff((r.p1 - r.p0).angle(), (s.p1 - s.p0).angle());
                break;
            case PrimKind::Circle:
                if (std::fabs(r.radius - s.radius) <= c.linTol) turn = 0;
                break;
            case PrimKind::Arc:
                if (std::fabs(r.radius - s.radius) <= c.linTol && std::fabs(r.sweep() - s.sweep()) * r.radius <= c.linTol)
                    turn = angleDiff(r.a0, s.a0);
                break;
            default: break;
            }
            if (turn < 0 || turn > kDisplacedMaxTurn) continue;
            cand.push_back({i, j, dist(keyPoint(r), keyPoint(s)) + turn * r.size(), false, true});
        }
    }
    std::sort(cand.begin(), cand.end(), [](const Pairing& a, const Pairing& b) {
        if (a.score != b.score) return a.score < b.score;
        if (a.ref != b.ref) return a.ref < b.ref;
        return a.stu < b.stu;
    });
    for (const Pairing& p : cand) {
        if (refUsed[p.ref] || stuUsed[p.stu]) continue;
        refUsed[p.ref] = stuUsed[p.stu] = 1;
        out.push_back(p);
    }
    return out;
}

}  // namespace dc
