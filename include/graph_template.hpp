// graph_template.hpp
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "fast_utils.hpp"

/** Compact tournament whose realized margins depend on 12 group weights. */
struct GraphTemplate {
    static constexpr int kGroupCount = 12;

    int N = 0;         // total nodes
    int n = 0, m = 0;  // variables, clauses
    int idxC = -1, idxD = -1; //index of winner, loser

    // flattened N*N arrays
    vector<string> names;
    vector<uint8_t> dir;   // 1 if u->v
    vector<uint8_t> group; // valid only when dir[u,v]==1
    vector<int16_t> off;   // valid only when dir[u,v]==1

    uint64_t tf_mask = 0;    // bitmask of all Ti and Fi
    uint64_t full_mask = 0;  // all nodes (C, L, T, F, X)

    AI int IDX(int u, int v) const noexcept { return u * N + v; }

    /**
     * Internal group index 11 is paper group g1; index 0 is paper group g12.
     */
    AI int margin(int u, int v, const int* weights) const noexcept {
        const int forward = IDX(u, v);
        if (dir[forward]) {
            return weights[group[forward]] + (int)off[forward];
        }
        const int reverse = IDX(v, u);
        return -(weights[group[reverse]] + (int)off[reverse]);
    }

    AI int margin(int u, int v, const vector<int>& weights) const noexcept {
        return margin(u, v, weights.data());//.data is constant time
    }
};
