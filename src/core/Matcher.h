#pragma once
// One-to-one matching of reference primitives against student primitives that
// have already been transformed into the reference frame.

#include "Model.h"

#include <vector>

namespace dc {

struct MatchCriteria {
    double linTol = 1e-4;
    double angTol = 1e-6;
    bool matchLinetype = true;
    bool requireLayer = false;
    bool matchColor = false;
    double colorTol = 0;          // colorDifference allowed (0-100)
    bool requireByLayer = false;  // the student's color and linetype set by layer
    bool strictCenterlines = false;
    bool checkDimensions = false;
    bool checkDimPrecision = true;  // with checkDimensions: the shown precision must agree
};

// Geometry within tolerance, ignoring linetype class and layer.
bool geometryMatches(const Primitive& r, const Primitive& s, const MatchCriteria& c);
// Linetype class / layer / color agreement, color and linetype by layer, or a dimension's
// shown precision (as configured).
bool attributesMatch(const Primitive& r, const Primitive& s, const MatchCriteria& c);
// Centerlines are matched on position and direction only unless strictCenterlines.
bool usesLooseCenterline(const Primitive& r, const MatchCriteria& c);

struct Matching {
    std::vector<int> refToStu;  // per reference primitive, -1 = unmatched
    std::vector<int> stuToRef;  // per student primitive, -1 = unclaimed
    int matched = 0;
    int ambiguous = 0;          // matches where more than one candidate was within tolerance
    void reset(size_t nRef, size_t nStu) {
        refToStu.assign(nRef, -1);
        stuToRef.assign(nStu, -1);
        matched = ambiguous = 0;
    }
};

// Greedy one-to-one matching of refIdx against unclaimed stuIdx. Updates `m`
// (entries already matched are left alone, so passes can be chained).
// geometryOnly ignores linetype class and layer.
void matchInto(const std::vector<Primitive>& ref, const std::vector<int>& refIdx, const std::vector<Primitive>& stu,
               const std::vector<int>& stuIdx, const MatchCriteria& c, bool geometryOnly, Matching& m);

// Fast count of geometry-only matches (used to score candidate transforms).
int countMatches(const std::vector<Primitive>& ref, const std::vector<int>& refIdx, const std::vector<Primitive>& stu,
                 const std::vector<int>& stuIdx, const MatchCriteria& c);

struct Pairing {
    int ref = -1;
    int stu = -1;
    double score = 0;
    bool superset = false;  // the student line contains the reference line (merged with a projection line)
    bool displaced = false; // same size, drawn elsewhere (found beyond the search radius)
};

// A displaced copy may also be turned by up to this much (a line drawn at a slightly
// different angle from the wrong starting point).
constexpr double kDisplacedMaxTurn = 5.0 * kPi / 180.0;

// Pairs unmatched reference primitives with the closest unmatched student primitive of
// the same kind within `radius`. Superset lines may explain several reference lines.
// What is left is then paired at any distance with primitives of the same size (a line of
// the same length, a circle of the same radius, an arc of the same radius and sweep)
// turned by at most kDisplacedMaxTurn: drawn in the wrong place.
std::vector<Pairing> pairNearMisses(const std::vector<Primitive>& ref, const std::vector<int>& refUnmatched,
                                    const std::vector<Primitive>& stu, const std::vector<int>& stuUnmatched,
                                    double radius, const MatchCriteria& c);

}  // namespace dc
