/**
 * @file fig1_tournament.cpp
 * @brief Reproduces the 15-candidate tournament of Figure 1 / Table 1 from
 *        "Stable Voting is PSPACE-Complete".
 *
 * Table 1 in the paper lists RANKS, not margins. Entry (i,j) is the rank of the
 * margin between c_i and c_j among all 105 pairs, where rank 1 is the LARGEST
 * margin; a positive sign means c_i defeats c_j. The matrix below is a verbatim
 * copy of that table, so a reader can check it against the paper row by row.
 *
 * Since SV and SSV depend only on the ORDER of the margins, any strictly
 * decreasing map from ranks to positive margins yields the same winner. We use
 *
 *      margin = 106 - rank,
 *
 * so rank 1 becomes margin 105 and rank 105 becomes margin 1. (Reversing this
 * convention -- reading rank 1 as the smallest margin -- gives a different
 * winner, c6, which is why the caption of Table 1 states the convention.)
 *
 * Expected output:
 *       c5 wins, under both SSV and the SV build.
 *
 * Build (SSV is the default; -DSV_CHECK_DEFEATS=1 selects SV):
 *   g++ -std=c++20 -O3 -DNDEBUG -Iinclude fig1_tournament.cpp -o figure1_ssv
 *   g++ -std=c++20 -O3 -DNDEBUG -Iinclude -DSV_CHECK_DEFEATS=1 fig1_tournament.cpp -o figure1_sv
 */

#include <bits/stdc++.h>
using namespace std;

#include "include/fast_utils.hpp"
#include "include/graph_template.hpp"
#include "include/sv_fast.hpp"

// ===================== Table 1 of the paper, verbatim =======================

static const int NC = 15;
static const int NUM_PAIRS = NC * (NC - 1) / 2;   // 105

static const int RANK[NC][NC] = {
//           c1   c2   c3   c4   c5   c6   c7   c8   c9  c10  c11  c12  c13  c14  c15
/* c1  */ {   0,  75,  81,  76,  77,   1, -39, -43, -47, -17,   6,   8,  11,  13,  15},
/* c2  */ { -75,   0,  78,  82,  83, -35,   2, -44, -48, -18,   7,   9, -26, -29,  16},
/* c3  */ { -81, -78,   0,  79,  80, -36, -40,   3, -49, -19, -21,  10, -27,  14, -32},
/* c4  */ { -76, -82, -79,   0,  84, -37, -41, -45,   4, -20, -22, -24,  12, -30, -33},
/* c5  */ { -77, -83, -80, -84,   0, -38, -42, -46, -50,   5, -23, -25, -28, -31, -34},
/* c6  */ {  -1,  35,  36,  37,  38,   0,  85,  86,  87,  51,  52,  53,  54,  55,  56},
/* c7  */ {  39,  -2,  40,  41,  42, -85,   0,  88,  89,  57,  58,  59,  60,  61,  62},
/* c8  */ {  43,  44,  -3,  45,  46, -86, -88,   0,  90,  63,  64,  65,  66,  67,  68},
/* c9  */ {  47,  48,  49,  -4,  50, -87, -89, -90,   0,  69,  70,  71,  72,  73,  74},
/* c10 */ {  17,  18,  19,  20,  -5, -51, -57, -63, -69,   0,  91,  92,  93,  94,  95},
/* c11 */ {  -6,  -7,  21,  22,  23, -52, -58, -64, -70, -91,   0,  96,  97,  98,  99},
/* c12 */ {  -8,  -9, -10,  24,  25, -53, -59, -65, -71, -92, -96,   0, 100, 101, 102},
/* c13 */ { -11,  26,  27, -12,  28, -54, -60, -66, -72, -93, -97,-100,   0, 103, 104},
/* c14 */ { -13,  29, -14,  30,  31, -55, -61, -67, -73, -94, -98,-101,-103,   0, 105},
/* c15 */ { -15, -16,  32,  33,  34, -56, -62, -68, -74, -95, -99,-102,-104,-105,   0},
};

// Rank 1 is the LARGEST margin, so ranks map to margins in reverse.
static inline int margin_from_rank(int rank) { return NUM_PAIRS + 1 - rank; }

// ===================== Graph setup (one dummy weight group) =================

static void init_custom_graph(GraphTemplate& G, const vector<string>& names) {
    G.N    = (int)names.size();
    G.n    = 0;
    G.m    = 0;
    G.idxC = -1;

    G.names = names;
    G.dir.assign(G.N * G.N, 0);
    G.group.assign(G.N * G.N, 0);
    G.off.assign(G.N * G.N, 0);

    G.tf_mask   = 0;
    G.full_mask = (G.N == 64) ? ~0ULL : ((1ULL << G.N) - 1ULL);
}

static void add_edge(GraphTemplate& G, int u, int v, int margin) {
    if (u == v)      { cerr << "Self-edge not allowed.\n"; exit(1); }
    if (margin <= 0) { cerr << "Stored edge margin must be positive.\n"; exit(1); }

    const int iuv = G.IDX(u, v);
    const int ivu = G.IDX(v, u);
    if (G.dir[iuv] || G.dir[ivu]) {
        cerr << "Edge between " << u << " and " << v << " already specified.\n";
        exit(1);
    }
    G.dir[iuv]   = 1;                 // u -> v
    G.group[iuv] = 0;                 // single dummy group: margin = W[0] + off = off
    G.off[iuv]   = (int16_t)margin;
}

// The table must be skew-symmetric and its absolute values must be exactly
// 1..105, each once; otherwise the tournament is not uniquely weighted and the
// winner is not well defined.
static void check_rank_table() {
    vector<int> seen(NUM_PAIRS + 1, 0);
    for (int i = 0; i < NC; ++i) {
        for (int j = 0; j < NC; ++j) {
            if (i == j) continue;
            if (RANK[i][j] != -RANK[j][i]) {
                cerr << "Table is not skew-symmetric at (" << i+1 << "," << j+1 << ").\n";
                exit(1);
            }
            if (i < j) {
                const int r = abs(RANK[i][j]);
                if (r < 1 || r > NUM_PAIRS) { cerr << "Rank out of range: " << r << "\n"; exit(1); }
                if (seen[r]++)              { cerr << "Duplicate rank: " << r << "\n";   exit(1); }
            }
        }
    }
    for (int r = 1; r <= NUM_PAIRS; ++r)
        if (!seen[r]) { cerr << "Missing rank: " << r << "\n"; exit(1); }
}

static void check_tournament_complete(const GraphTemplate& G) {
    for (int u = 0; u < G.N; ++u)
        for (int v = u + 1; v < G.N; ++v)
            if ((int)G.dir[G.IDX(u, v)] + (int)G.dir[G.IDX(v, u)] != 1) {
                cerr << "Missing or duplicated orientation on pair {" << u << "," << v << "}.\n";
                exit(1);
            }
}

// ===================== Main =================================================

int main() {
    check_rank_table();

    vector<string> names(NC);
    for (int i = 0; i < NC; ++i) names[i] = "c" + to_string(i + 1);

    GraphTemplate G;
    init_custom_graph(G, names);

    for (int i = 0; i < NC; ++i)
        for (int j = i + 1; j < NC; ++j) {
            const int r = RANK[i][j];
            if (r > 0) add_edge(G, i, j, margin_from_rank(r));   // c_i defeats c_j
            else       add_edge(G, j, i, margin_from_rank(-r));  // c_j defeats c_i
        }
    check_tournament_complete(G);

    vector<int> W = {0};              // one dummy group: realized margin = off
    SVFast solver(G);
    solver.reset_epoch(W.data());

    cout << solver.config_string() << "\n\n";


    const int winner = solver.solve_winner_standard(G.full_mask);
    if (winner < 0) { cout << "No winner found.\n"; return 1; }

    cout << "Winner: " << G.names[winner] << "\n";

    vector<int> elim;
    solver.reconstruct_standard(G.full_mask, winner, elim);
    cout << "Elimination order: ";
    for (int x : elim) cout << G.names[x] << ' ';
    cout << "\n";
}