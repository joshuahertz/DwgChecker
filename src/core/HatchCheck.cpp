#include "HatchCheck.h"

#include <algorithm>
#include <cmath>
#include <map>

namespace dc {

double hatchMargin(const Primitive& h, double linTol) {
    const BBox b = h.bbox();
    return std::max(10 * linTol, 0.005 * std::min(b.width(), b.height()));
}

std::vector<Vec2> hatchSamples(const Primitive& h, double margin, int target) {
    std::vector<Vec2> out;
    const BBox b = h.bbox();
    const double area = b.width() * b.height();
    if (!h.loops || !(area > 0)) return out;
    double step = std::sqrt(area / std::max(1, target));
    // Thin regions get a finer grid (at most two refinements, i.e. 16x the points).
    for (int attempt = 0; attempt < 3 && out.size() < 30; ++attempt, step /= 2) {
        out.clear();
        for (double y = b.y0 + step / 2; y < b.y1; y += step)
            for (double x = b.x0 + step / 2; x < b.x1; x += step) {
                const Vec2 q{x, y};
                if (hatchContains(h, q) && hatchBoundaryDistance(h, q) >= margin) out.push_back(q);
            }
    }
    return out;
}

static Vec2 meanOf(const std::vector<Vec2>& v) {
    Vec2 m;
    for (Vec2 q : v) m = m + q;
    return v.empty() ? m : m / static_cast<double>(v.size());
}

HatchCheckResult checkHatchRegions(const std::vector<Primitive>& ref, const std::vector<int>& refH,
                                   const std::vector<Primitive>& stu, const std::vector<int>& stuH, double linTol) {
    HatchCheckResult res;
    std::vector<BBox> stuBox, refBox;
    for (int j : stuH) stuBox.push_back(stu[j].bbox());
    for (int i : refH) refBox.push_back(ref[i].bbox());

    for (int i : refH) {
        HatchRefResult rr;
        rr.ref = i;
        const std::vector<Vec2> samples = hatchSamples(ref[i], hatchMargin(ref[i], linTol));
        rr.sampled = !samples.empty();
        rr.centroid = samples.empty() ? ref[i].bbox().center() : meanOf(samples);
        std::map<int, int> hits;  // student hatch -> samples it covers
        int covered = 0;
        for (Vec2 q : samples) {
            bool any = false;
            for (size_t k = 0; k < stuH.size(); ++k) {
                if (!stuBox[k].contains(q) || !hatchContains(stu[stuH[k]], q)) continue;
                any = true;
                hits[stuH[k]]++;
            }
            covered += any;
        }
        if (rr.sampled) rr.coverage = static_cast<double>(covered) / samples.size();
        // Contributors, most coverage first.
        std::vector<std::pair<int, int>> byHits(hits.begin(), hits.end());
        std::sort(byHits.begin(), byHits.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
        for (const auto& [j, n] : byHits)
            if (n >= kHatchContributor * samples.size()) {
                rr.contributors.push_back(j);
                if (!rr.patterns.contains(stu[j].text)) rr.patterns << stu[j].text;
            }
        res.refs.push_back(rr);
    }

    for (size_t k = 0; k < stuH.size(); ++k) {
        HatchStuResult sr;
        sr.stu = stuH[k];
        const Primitive& s = stu[stuH[k]];
        const std::vector<Vec2> samples = hatchSamples(s, hatchMargin(s, linTol));
        sr.sampled = !samples.empty();
        sr.centroid = samples.empty() ? s.bbox().center() : meanOf(samples);
        int outside = 0;
        for (Vec2 q : samples) {
            bool inRef = false;
            for (size_t r = 0; r < refH.size() && !inRef; ++r)
                inRef = refBox[r].contains(q) && hatchContains(ref[refH[r]], q);
            outside += !inRef;
        }
        if (sr.sampled) sr.outside = static_cast<double>(outside) / samples.size();
        res.stus.push_back(sr);
    }
    return res;
}

}  // namespace dc
