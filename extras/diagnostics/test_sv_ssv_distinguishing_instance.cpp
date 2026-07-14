/**
 * @file test_sv_ssv_distinguishing_instance.cpp
 * @brief Regression instance on which SSV elects a and SV elects b.
 *
 * Build both modes:
 *   g++ -std=c++20 -O2 -DSV_CHECK_DEFEATS=0 test_sv_ssv_distinguishing_instance.cpp -o test_ssv
 *   g++ -std=c++20 -O2 -DSV_CHECK_DEFEATS=1 test_sv_ssv_distinguishing_instance.cpp -o test_sv
 */

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

using namespace std;

#include "fast_utils.hpp"
#include "graph_template.hpp"
#include "sv_fast.hpp"

namespace {
    void initialize_graph(GraphTemplate& G, const vector<string>& names) {
        if (names.empty() || names.size() > 64) {
            throw invalid_argument("The diagnostic requires 1..64 candidates");
        }
        G.N = (int)names.size();
        G.n = 0;
        G.m = 0;
        G.idxC = -1;
        G.names = names;
        G.dir.assign((size_t)G.N * G.N, 0);
        G.group.assign((size_t)G.N * G.N, 0);
        G.off.assign((size_t)G.N * G.N, 0);
        G.tf_mask = 0;
        G.full_mask = G.N == 64
            ? ~uint64_t{0}
            : (uint64_t{1} << G.N) - 1;
    }

    void add_edge(GraphTemplate& G, int source, int target, int margin) {
        if (source == target || margin <= 0) {
            throw invalid_argument("Edges require distinct endpoints and positive margins");
        }
        const int forward = G.IDX(source, target);
        const int reverse = G.IDX(target, source);
        if (G.dir[forward] || G.dir[reverse]) {
            throw logic_error("Duplicate orientation in distinguishing instance");
        }
        G.dir[forward] = 1;          // u -> v
        G.group[forward] = 0;        // only one dummy weight group
        G.off[forward] = (int16_t)(margin);
    }

    void validate_tournament(const GraphTemplate& G) {
        for (int source = 0; source < G.N; ++source) {
            for (int target = source + 1; target < G.N; ++target) {
                const int orientations =
                    (int)G.dir[G.IDX(source, target)] +
                    (int)G.dir[G.IDX(target, source)];
                if (orientations != 1) {
                    throw logic_error("Distinguishing instance is not a tournament");
                }
            }
        }
    }
}  // namespace

int main() {
    try {
        GraphTemplate G;
        initialize_graph(G, {"a", "b", "c", "d", "e", "f", "g"});

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
        validate_tournament(G);

        // One dummy group: realized margin = W[0] + off = off
        const int weights[] = {0};
        SVFast solver(G); solver.reset_epoch(weights);

        const int winner = solver.solve_winner_standard(G.full_mask);
        if (winner < 0) throw runtime_error("Solver returned no winner");

        constexpr string_view expected = SVFast::CHECK_DEFEATS ? "b" : "a";
        const string& observed = G.names[winner];
        cout << "Mode: " << (SVFast::CHECK_DEFEATS ? "SV" : "SSV") << '\n'
             << "Expected winner: " << expected << '\n'
             << "Observed winner: " << observed << '\n';

        vector<int> elim_order;
        solver.reconstruct_standard(G.full_mask, winner, elim_order);

        cout << "Elimination order:";
        for (const int candidate : elim_order) cout << ' ' << G.names[candidate];
        cout << '\n';

        return observed == expected ? 0 : 1;
    } catch (const exception& error) {
        cerr << "error: " << error.what() << '\n';
        return 2;
    }
}
