#pragma once
// Disjoint sets (union-find) over 0..n-1, used to join merged lines, views and clusters. A set's
// root is its smallest member.

#include <algorithm>
#include <numeric>
#include <vector>

namespace dc {

struct Dsu {
    std::vector<int> p;
    explicit Dsu(int n) : p(n) { std::iota(p.begin(), p.end(), 0); }
    int find(int x) {
        while (p[x] != x) x = p[x] = p[p[x]];
        return x;
    }
    void unite(int a, int b) {
        a = find(a);
        b = find(b);
        if (a != b) p[std::max(a, b)] = std::min(a, b);
    }
};

}  // namespace dc
