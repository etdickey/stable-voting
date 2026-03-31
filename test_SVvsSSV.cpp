// test_SVvsSSV.cpp
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

using namespace std;

// Your sv_fast.hpp uses these as if they already exist.
static inline int popcount64(uint64_t x) {
#if defined(__GNUC__) || defined(__clang__)
    return __builtin_popcountll(x);
#else
    int c = 0;
    while (x) { x &= (x - 1); ++c; }
    return c;
#endif
}

static inline int ctz64(uint64_t x) {
#if defined(__GNUC__) || defined(__clang__)
    return __builtin_ctzll(x);
#else
    int c = 0;
    while ((x & 1ULL) == 0ULL) { x >>= 1; ++c; }
    return c;
#endif
}

#include "graph_template.hpp"
#include "sv_fast.hpp"

static void init_custom_graph(GraphTemplate& G, const vector<string>& names) {
    G.N = (int)names.size();
    G.n = 0;
    G.m = 0;
    G.idxC = -1;

    G.names = names;
    G.dir.assign(G.N * G.N, 0);
    G.group.assign(G.N * G.N, 0);
    G.off.assign(G.N * G.N, 0);

    G.tf_mask = 0;
    if (G.N == 64) G.full_mask = ~0ULL;
    else G.full_mask = (1ULL << G.N) - 1ULL;
}

static void add_edge(GraphTemplate& G, int u, int v, int margin) {
    if (u == v) {
        cerr << "Self-edge not allowed.\n";
        exit(1);
    }
    if (margin <= 0) {
        cerr << "Stored edge margin must be positive.\n";
        exit(1);
    }

    int iuv = G.IDX(u, v);
    int ivu = G.IDX(v, u);

    if (G.dir[iuv] || G.dir[ivu]) {
        cerr << "Edge between " << u << " and " << v << " already specified.\n";
        exit(1);
    }

    G.dir[iuv] = 1;          // u -> v
    G.group[iuv] = 0;        // only one dummy weight group
    G.off[iuv] = (int16_t)margin;
}

static void check_tournament_complete(const GraphTemplate& G) {
    for (int u = 0; u < G.N; ++u) {
        for (int v = u + 1; v < G.N; ++v) {
            int iuv = G.IDX(u, v);
            int ivu = G.IDX(v, u);
            if ((int)G.dir[iuv] + (int)G.dir[ivu] != 1) {
                cerr << "Missing or duplicated orientation on pair {"
                     << u << "," << v << "}.\n";
                exit(1);
            }
        }
    }
}

int main() {
    GraphTemplate G;
    init_custom_graph(G, {"a", "b", "c", "d", "e","f","g"});

    // Define the tournament by hand.
    // Exactly one direction per unordered pair.
    // weight 1..21
    add_edge(G, 3, 6,  1);  // d -> g
    add_edge(G, 6, 0,  2);  // g -> a
    add_edge(G, 3, 4,  3);  // d -> e
    add_edge(G, 5, 3,  4);  // f -> d
    add_edge(G, 3, 2,  5);  // d -> c
    add_edge(G, 2, 4,  6);  // c -> e
    add_edge(G, 5, 0,  7);  // f -> a
    add_edge(G, 1, 3,  8);  // b -> d
    add_edge(G, 3, 0,  9);  // d -> a
    add_edge(G, 5, 1, 10);  // f -> b
    add_edge(G, 0, 2, 11);  // a -> c
    add_edge(G, 1, 0, 12);  // b -> a
    add_edge(G, 4, 1, 13);  // e -> b
    add_edge(G, 2, 1, 14);  // c -> b
    add_edge(G, 1, 6, 15);  // b -> g
    add_edge(G, 6, 2, 16);  // g -> c
    add_edge(G, 5, 6, 17);  // f -> g
    add_edge(G, 2, 5, 18);  // c -> f
    add_edge(G, 4, 5, 19);  // e -> f
    add_edge(G, 0, 4, 20);  // a -> e
    add_edge(G, 6, 4, 21);  // g -> e

    check_tournament_complete(G);

    // One dummy group: realized margin = W[0] + off = off
    vector<int> W = {0};

    SVFast solver(G);
    solver.reset_epoch(W.data());

    int winner = solver.solve_winner_standard(G.full_mask);

    cout << "Winner index: " << winner << "\n";
    cout << "Winner name : " << G.names[winner] << "\n";

    vector<int> elim;
    solver.reconstruct_standard(G.full_mask, winner, elim);

    cout << "Elimination order: ";
    for (int x : elim) cout << G.names[x] << ' ';
    cout << "\n";

    return 0;
}