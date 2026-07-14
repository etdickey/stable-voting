// Internal helper for benchmark_pref_voting.py.
// The Python driver compiles this file twice:
//   -DSV_CHECK_DEFEATS=0 for Simple Stable Voting
//   -DSV_CHECK_DEFEATS=1 for Stable Voting

#include <bits/stdc++.h>
using namespace std;

#include "fast_utils.hpp"
#include "graph_template.hpp"
#include "sv_fast.hpp"

static GraphTemplate read_tournament(istream& in) {
    int N;
    in >> N;
    if (!in) throw runtime_error("Unable to read candidate count");
    if (N <= 0 || N >= 64) throw runtime_error("The benchmark requires 1 <= N < 64");

    GraphTemplate G;
    G.N = N;
    G.n = 0;
    G.m = 0;
    G.idxC = -1;
    G.names.resize(N);
    for (int i = 0; i < N; ++i) G.names[i] = to_string(i);

    G.dir.assign((size_t)N * N, 0);
    G.group.assign((size_t)N * N, 0);
    G.off.assign((size_t)N * N, 0);
    G.tf_mask = 0;
    G.full_mask = (1ull << N) - 1ull;

    const int edge_count = N * (N - 1) / 2;
    for (int e = 0; e < edge_count; ++e) {
        int u, v, margin;
        in >> u >> v >> margin;
        if (!in) throw runtime_error("Unable to read tournament edge");
        if (u < 0 || u >= N || v < 0 || v >= N || u == v)
            throw runtime_error("Invalid edge endpoint");
        if (margin <= 0 || margin > INT16_MAX)
            throw runtime_error("Margins must be in [1, 32767]");

        int iuv = G.IDX(u, v);
        int ivu = G.IDX(v, u);
        if (G.dir[iuv] || G.dir[ivu])
            throw runtime_error("Duplicate unordered candidate pair");

        G.dir[iuv] = 1;
        G.group[iuv] = 0;
        G.off[iuv] = (int16_t)margin;
    }

    return G;
}

static double median(vector<double>& values) {
    sort(values.begin(), values.end());
    const int n = (int)values.size();
    if (n % 2) return values[n / 2];
    return (values[n / 2 - 1] + values[n / 2]) / 2.0;
}

int main() {
    ios::sync_with_stdio(false);
    cin.tie(nullptr);

    int tournament_count, repeats;
    cin >> tournament_count >> repeats;
    if (!cin || tournament_count <= 0 || repeats <= 0) {
        cerr << "Expected: <number of tournaments> <repeats>\n";
        return 1;
    }

    vector<GraphTemplate> tournaments;
    tournaments.reserve(tournament_count);
    try {
        for (int i = 0; i < tournament_count; ++i)
            tournaments.push_back(read_tournament(cin));
    } catch (const exception& e) {
        cerr << "Input error: " << e.what() << '\n';
        return 1;
    }

    int W[1] = {0};
    cout << setprecision(12);

    for (int i = 0; i < tournament_count; ++i) {
        GraphTemplate& G = tournaments[i];
        int winner = -1;

        // One untimed call warms instruction/data caches for this tournament size.
        {
            SVFast solver(G);
            solver.reset_epoch(W);
            winner = solver.solve_winner(G.full_mask);
        }

        vector<double> times;
        times.reserve(repeats);
        for (int r = 0; r < repeats; ++r) {
            auto start = chrono::steady_clock::now();
            {
                // Include solver allocation and preprocessing, just as one normal
                // C++ use includes constructing and preparing the solver.
                SVFast solver(G);
                solver.reset_epoch(W);
                winner = solver.solve_winner(G.full_mask);
            }
            auto stop = chrono::steady_clock::now();
            times.push_back(chrono::duration<double>(stop - start).count());
        }

        cout << i << '\t' << G.N << '\t' << winner << '\t' << median(times) << '\n';
    }

    return 0;
}
