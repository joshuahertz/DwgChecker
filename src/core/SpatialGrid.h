#pragma once
// Uniform hash grid over 2D points. Queries return every item whose point lies in
// the 3x3 block of cells around the query point; with cell >= 2 * radius this
// covers all items within `radius`.

#include "Geometry.h"

#include <cmath>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace dc {

class SpatialGrid {
public:
    explicit SpatialGrid(double cell) : m_cell(cell > 0 ? cell : 1e-9) {}

    void insert(Vec2 p, int item) { m_cells[key(cellOf(p.x), cellOf(p.y))].push_back(item); }

    template <typename F>
    void forNear(Vec2 p, F&& f) const {
        const int64_t cx = cellOf(p.x), cy = cellOf(p.y);
        for (int64_t dx = -1; dx <= 1; ++dx)
            for (int64_t dy = -1; dy <= 1; ++dy) {
                const auto it = m_cells.find(key(cx + dx, cy + dy));
                if (it == m_cells.end()) continue;
                for (int item : it->second) f(item);
            }
    }

private:
    int64_t cellOf(double v) const {
        const double c = std::floor(v / m_cell);
        if (!(c > -4e15)) return -4000000000000000LL;
        if (!(c < 4e15)) return 4000000000000000LL;
        return static_cast<int64_t>(c);
    }
    static uint64_t key(int64_t x, int64_t y) {
        // Mix the two cell indices into one 64-bit key.
        uint64_t h = static_cast<uint64_t>(x) * 0x9E3779B97F4A7C15ULL;
        h ^= static_cast<uint64_t>(y) + 0x7F4A7C159E3779B9ULL + (h << 6) + (h >> 2);
        return h;
    }
    double m_cell;
    std::unordered_map<uint64_t, std::vector<int>> m_cells;
};

}  // namespace dc
