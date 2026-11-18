#include "ViewSync.h"

#include <algorithm>

namespace dc {

Camera mapCamera(const Camera& c, const Transform2D& t) {
    const double s = t.scaleFactor();
    return {t.apply(c.center), s > 0 ? c.scale / s : c.scale};
}

namespace {
bool isOrtho(const CompareResult& r, const ViewSet* views) { return r.mode == Mode::Orthographic && views; }

// The student's file -> reference frame, as the student items graded in `view` were moved.
std::optional<Transform2D> gradedWith(const CompareResult& r, bool ortho, int view) {
    for (int pass = 0; pass < 2; ++pass)
        for (int j = 0; j < static_cast<int>(r.stuT.size()); ++j) {
            if (ortho && (j >= static_cast<int>(r.stuView.size()) || r.stuView[j] != view)) continue;
            if (pass == 0 && (j >= static_cast<int>(r.stuPartner.size()) || r.stuPartner[j] < 0)) continue;
            return r.stuT[j];
        }
    return std::nullopt;
}

// The views to try at `p`, best first: those whose box holds it (the smallest first), then the
// others by distance. Views without a box are left out.
std::vector<int> viewsNear(const std::vector<BBox>& boxes, Vec2 p) {
    std::vector<int> order;
    for (int v = 0; v < static_cast<int>(boxes.size()); ++v)
        if (boxes[v].valid()) order.push_back(v);
    std::stable_sort(order.begin(), order.end(), [&](int a, int b) {
        const double da = boxes[a].distanceTo(p), db = boxes[b].distanceTo(p);
        if (da != db) return da < db;
        return boxes[a].width() * boxes[a].height() < boxes[b].width() * boxes[b].height();
    });
    return order;
}
}  // namespace

std::optional<Transform2D> refToStudentOfView(const CompareResult& r, bool ortho, int view) {
    if (const auto t = gradedWith(r, ortho, view)) return t->inverse();
    return std::nullopt;
}

std::optional<Transform2D> refToStudentAt(const CompareResult& r, const ViewSet* views, Vec2 p) {
    if (!isOrtho(r, views)) return refToStudentOfView(r, false, -1);
    std::vector<BBox> boxes(views->views.size());
    // Ignored views too: located like the others, their student items moved with them (D108).
    for (int v = 0; v < static_cast<int>(boxes.size()); ++v) boxes[v] = views->views[v].bbox;
    for (int v : viewsNear(boxes, p))
        if (const auto t = refToStudentOfView(r, true, v)) return t;
    return std::nullopt;
}

std::optional<Transform2D> studentToRefAt(const CompareResult& r, const ViewSet* views, const Drawing& stu, Vec2 p) {
    if (!isOrtho(r, views)) return gradedWith(r, false, -1);
    std::vector<BBox> boxes(views->views.size());
    const int n = static_cast<int>(std::min(stu.prims.size(), r.stuView.size()));
    for (int j = 0; j < n; ++j) {
        const int v = r.stuView[j];
        if (v >= 0 && v < static_cast<int>(boxes.size())) boxes[v].add(stu.prims[j].bbox());
    }
    for (int v : viewsNear(boxes, p))
        if (const auto t = gradedWith(r, true, v)) return t;
    return std::nullopt;
}

}  // namespace dc
