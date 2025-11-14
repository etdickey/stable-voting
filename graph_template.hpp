// graph_template.hpp
#pragma once

#include <vector>

#if defined(__GNUC__) || defined(__clang__)
  #define AI inline __attribute__((always_inline))
#else
  #define AI inline
#endif

// ========================= Tournament Template =========================
// We represent each ordered pair (u,v) with three parallel arrays:
//   dir[u,v]   = 1 if edge u->v exists, else 0 (then v->u exists)
//   group[u,v] = weight-bucket id in [0..10] for the *existing* direction
//   off[u,v]   = monotonically decreasing small integer (e.g., -1,-2,...) per group
// For fast margins under a permutation W[11]:
//   margin(u,v) = dir[u,v] ?  (W[group[u,v]] + off[u,v])
//                           : -(W[group[v,u]] + off[v,u])
struct GraphTemplate {
    int N = 0;         // total nodes
    int n = 0, m = 0;  // variables, clauses
    int idxC = 0;      // index of C
    // Optional names (only used when printing full details)
    vector<string> names; // size N

    // flattened N*N arrays
    vector<uint8_t> dir;    // 1 if u->v
    vector<uint8_t> group;  // valid only when dir[u,v]==1
    vector<int16_t> off;    // valid only when dir[u,v]==1

    uint64_t tf_mask = 0;   // bitmask of all Ti and Fi
    uint64_t full_mask = 0; // all nodes (C, L, T, F, X)

    AI int IDX(int u,int v) const { return u*N + v; }

    AI int margin(int u,int v, const int *W) const {
        int iuv = IDX(u,v);
        if (dir[iuv]) return W[group[iuv]] + (int)off[iuv];
        int ivu = IDX(v,u);
        return -(W[group[ivu]] + (int)off[ivu]);
    }

    // Add this margin overload inside GraphTemplate if you like:
    AI int margin(int u, int v, const vector<int>& W) const {
        return margin(u, v, W.data());//.data is constant time
    }
};
