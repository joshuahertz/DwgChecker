#include "Aligner.h"

#include <algorithm>

namespace dc {

Signature signatureOf(const Primitive& p) {
    switch (p.kind) {
    case PrimKind::Line: return {p.kind, p.length(), 0};
    case PrimKind::Arc: return {p.kind, p.radius, p.sweep()};
    case PrimKind::Circle: return {p.kind, p.radius, kTwoPi};
    default: return {p.kind, 0, 0};
    }
}

bool signaturesMatch(const Signature& a, const Signature& b, double linTol, double angTol) {
    if (a.kind != b.kind) return false;
    // A line whose endpoints are each within tol can differ in length by 2*tol.
    const double sizeTol = a.kind == PrimKind::Line ? 2 * linTol : linTol;
    if (std::fabs(a.size - b.size) > sizeTol * 1.0000001) return false;
    if (a.kind == PrimKind::Arc) {
        const double swTol = angTol + 2 * linTol / std::max(a.size, 1e-12);
        if (std::fabs(a.sweep - b.sweep) > swTol) return false;
    }
    return true;
}

std::vector<Primitive> transformAll(const std::vector<Primitive>& stu, const std::vector<int>& idx, const Transform2D& T) {
    std::vector<Primitive> out = stu;
    if (T.isIdentity()) return out;
    for (int j : idx) out[j] = transformed(stu[j], T);
    return out;
}

namespace {

Vec2 keyPoint(const Primitive& p) { return p.kind == PrimKind::Line ? p.midpoint() : p.center; }

struct CandidateList {
    std::vector<AlignCandidate> items;
    double linTol, angTol;
    int cap;
    void add(const Transform2D& T, double theta, bool mirrored) {
        if (static_cast<int>(items.size()) >= cap) return;
        for (const auto& c : items)
            if (c.mirrored == mirrored && angleDiff(c.theta, theta) <= angTol * 10 &&
                std::hypot(c.T.tx - T.tx, c.T.ty - T.ty) <= linTol)
                return;
        items.push_back({T, 0, normalizeAngle(theta), mirrored});
    }
};

// T = translate * R(theta) * pre, chosen so that `from` maps onto `to`.
Transform2D mapPoint(Vec2 from, Vec2 to, double theta, const Transform2D& pre) {
    const Transform2D R = Transform2D::rotation(theta) * pre;
    const Vec2 moved = R.apply(from);
    Transform2D t = R;
    t.tx += to.x - moved.x;
    t.ty += to.y - moved.y;
    return t;
}

}  // namespace

AlignResult alignDrawings(const std::vector<Primitive>& ref, const std::vector<int>& refIdx,
                          const std::vector<Primitive>& stu, const std::vector<int>& stuIdx, const AlignOptions& o) {
    AlignResult res;
    const double linTol = o.criteria.linTol, angTol = o.criteria.angTol;
    CandidateList cands{{}, linTol, angTol, o.maxCandidates};
    cands.add(Transform2D::identity(), 0, false);

    // 1. Signatures and rarity of each reference primitive in the student drawing.
    std::vector<int> refGeo, stuGeo;
    for (int i : refIdx) if (isGeometric(ref[i].kind)) refGeo.push_back(i);
    for (int j : stuIdx) if (isGeometric(stu[j].kind)) stuGeo.push_back(j);
    std::vector<Signature> stuSig(stu.size());
    for (int j : stuGeo) stuSig[j] = signatureOf(stu[j]);
    struct Anchor { int ref; std::vector<int> matches; };
    std::vector<Anchor> anchors;
    for (int i : refGeo) {
        const Signature rs = signatureOf(ref[i]);
        Anchor a{i, {}};
        for (int j : stuGeo)
            if (signaturesMatch(rs, stuSig[j], linTol, angTol)) a.matches.push_back(j);
        if (!a.matches.empty()) anchors.push_back(std::move(a));
    }
    res.anchorFound = !anchors.empty();

    // 2. Rarest first; lines and arcs before circles (they fix rotation); larger first.
    std::stable_sort(anchors.begin(), anchors.end(), [&](const Anchor& a, const Anchor& b) {
        const bool ac = ref[a.ref].kind == PrimKind::Circle, bc = ref[b.ref].kind == PrimKind::Circle;
        if (ac != bc) return !ac;
        if (a.matches.size() != b.matches.size()) return a.matches.size() < b.matches.size();
        return ref[a.ref].size() > ref[b.ref].size();
    });
    if (static_cast<int>(anchors.size()) > o.maxAnchors) anchors.resize(o.maxAnchors);

    // 3. Candidate transforms from each anchor / student pair.
    std::vector<Transform2D> pres{Transform2D::identity()};
    if (o.allowMirror) pres.push_back(Transform2D::mirrorX());
    for (const Anchor& an : anchors) {
        const Primitive& r = ref[an.ref];
        for (int j : an.matches) {
            for (size_t pi = 0; pi < pres.size(); ++pi) {
                const Transform2D& pre = pres[pi];
                const bool mir = pi == 1;
                const Primitive s = transformed(stu[j], pre);
                if (r.kind == PrimKind::Line) {
                    if (o.allowRotation) {
                        for (int ord = 0; ord < 2; ++ord) {
                            const Vec2 q0 = ord ? s.p1 : s.p0, q1 = ord ? s.p0 : s.p1;
                            const double th = (r.p1 - r.p0).angle() - (q1 - q0).angle();
                            cands.add(mapPoint(ord ? stu[j].p1 : stu[j].p0, r.p0, th, pre), th, mir);
                        }
                    } else {
                        const double len = std::max(r.length(), 1e-12);
                        if (lineAngleDiff((r.p1 - r.p0).angle(), (s.p1 - s.p0).angle()) <= angTol + 2 * linTol / len) {
                            const Vec2 d = r.midpoint() - s.midpoint();
                            Transform2D T = pre;
                            T.tx += d.x; T.ty += d.y;
                            cands.add(T, 0, mir);
                        }
                    }
                } else if (r.kind == PrimKind::Arc) {
                    if (o.allowRotation) {
                        const double th = r.a0 - s.a0;
                        cands.add(mapPoint(stu[j].center, r.center, th, pre), th, mir);
                    } else if (angleDiff(r.a0, s.a0) <= angTol + 2 * linTol / std::max(r.radius, 1e-12)) {
                        const Vec2 d = r.center - s.center;
                        Transform2D T = pre;
                        T.tx += d.x; T.ty += d.y;
                        cands.add(T, 0, mir);
                    }
                } else {  // circle: translation, plus a second anchor to fix rotation
                    {
                        const Vec2 d = r.center - s.center;
                        Transform2D T = pre;
                        T.tx += d.x; T.ty += d.y;
                        cands.add(T, 0, mir);
                    }
                    if (!o.allowRotation) continue;
                    const Anchor* second = nullptr;
                    double far = 0;
                    for (const Anchor& b : anchors) {
                        if (b.ref == an.ref) continue;
                        const double dd = dist(keyPoint(ref[b.ref]), r.center);
                        if (dd > far) { far = dd; second = &b; }
                    }
                    // Without a second anchor (no other reference primitive has student matches)
                    // the translation-only candidate above is all a circle can give.
                    if (!second || far <= linTol) continue;
                    const Vec2 vr = keyPoint(ref[second->ref]) - r.center;
                    int used = 0;
                    for (int j2 : second->matches) {
                        if (j2 == j || ++used > 60) continue;
                        const Vec2 vs = keyPoint(transformed(stu[j2], pre)) - s.center;
                        if (std::fabs(vs.length() - vr.length()) > 2 * linTol) continue;
                        const double th = vr.angle() - vs.angle();
                        cands.add(mapPoint(stu[j].center, r.center, th, pre), th, mir);
                    }
                }
            }
        }
    }

    // 4. Score each candidate by its match count.
    for (auto& c : cands.items) {
        const auto st = transformAll(stu, stuGeo, c.T);
        c.score = countMatches(ref, refGeo, st, stuGeo, o.criteria);
    }
    res.candidatesTried = static_cast<int>(cands.items.size());
    std::stable_sort(cands.items.begin(), cands.items.end(), [](const AlignCandidate& a, const AlignCandidate& b) {
        if (a.score != b.score) return a.score > b.score;
        if (a.mirrored != b.mirrored) return !a.mirrored;
        const double ta = std::min(a.theta, kTwoPi - a.theta), tb = std::min(b.theta, kTwoPi - b.theta);
        if (std::fabs(ta - tb) > 1e-12) return ta < tb;
        return std::hypot(a.T.tx, a.T.ty) < std::hypot(b.T.tx, b.T.ty);
    });
    res.best = cands.items.front();
    for (const auto& c : cands.items) {
        if (static_cast<int>(res.top.size()) >= std::max(1, o.topK)) break;
        if (c.score <= 0 && !res.top.empty()) break;
        res.top.push_back(c);
    }
    return res;
}

}  // namespace dc
