#pragma once
// Relative-mode alignment: finds the rigid transform T (student -> reference)
// that maximizes the number of matching primitives.

#include "Matcher.h"
#include "Model.h"

#include <vector>

namespace dc {

struct AlignOptions {
    bool allowRotation = true;
    bool allowMirror = false;
    int topK = 1;          // how many distinct best candidates to return (Orthographic mode uses K)
    int maxAnchors = 4;    // anchors pooled (the spec's single rarest anchor, plus fallbacks)
    int maxCandidates = 600;
    MatchCriteria criteria;  // tolerances; scoring is geometry-only
};

struct AlignCandidate {
    Transform2D T;
    int score = 0;
    double theta = 0;
    bool mirrored = false;
};

struct AlignResult {
    AlignCandidate best;            // identity with score 0 when nothing aligns
    std::vector<AlignCandidate> top;  // best first, at most topK
    int candidatesTried = 0;
    bool anchorFound = false;
};

// Signature used for anchoring: (kind, length or radius, arc sweep).
struct Signature {
    PrimKind kind;
    double size;   // line length / radius
    double sweep;  // arcs only
};
Signature signatureOf(const Primitive& p);
bool signaturesMatch(const Signature& a, const Signature& b, double linTol, double angTol);

AlignResult alignDrawings(const std::vector<Primitive>& ref, const std::vector<int>& refIdx,
                          const std::vector<Primitive>& stu, const std::vector<int>& stuIdx, const AlignOptions& o);

// Applies T to the listed student primitives; other entries are copied unchanged.
std::vector<Primitive> transformAll(const std::vector<Primitive>& stu, const std::vector<int>& idx, const Transform2D& T);

}  // namespace dc
