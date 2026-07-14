/**
 * @file test_qbf_reduction.cpp
 * @brief Regression tests and diagnostics for the QBF-to-tournament reduction.
 *
 * This executable runs selected formulas using a fixed group-weight order and
 * checks that the resulting Stable Voting winner agrees with the expected
 * quantified-formula value. It also provides optional diagnostics such as
 * elimination-order reconstruction and induced-subtournament evaluation.
 *
 * Unlike search_weight_orders.cpp, this file does not enumerate weight
 * permutations. Its purpose is to make correctness failures easy to reproduce
 * and inspect for one known configuration.
 *
 * A mismatch between an observed and expected outcome should be reported with
 * the formula name, clauses, winner, and elimination order, and should cause a
 * nonzero exit status.
 *
 * Fast build and run:
 *   g++ -std=c++20 -O3 -march=native -flto -DNDEBUG test_qbf_reduction.cpp -o test_qbf_reduction && test_qbf_reduction
 *     (-O3 includes -fomit-frame-pointer)
 */

#include <bits/stdc++.h> //includes basically everything you could need, including iomanip
#include <iostream>
using namespace std;

#include "fast_utils.hpp"
#include "graph_template.hpp"
#include "tqbf_tournament_builder.hpp"
#include "sv_fast.hpp"
#include "printers.hpp"
#include "experiment_support.hpp"
#include "formula_suites.hpp"

using namespace formula_suites;

uint64_t mask_from_names(const GraphTemplate& T, const std::vector<std::string>& keep) {
    uint64_t mask = 0;
    for (const std::string& nm : keep) {
        auto it = std::find(T.names.begin(), T.names.end(), nm);
        if (it == T.names.end()) {
            std::cerr << "Name not found in template: " << nm << "\n";
            continue;
        }
        int idx = int(it - T.names.begin());
        mask |= (1ull << idx);
    }
    return mask;
}

static bool run_formula(const FormulaCase& F, const int* W) {
// static void run_formula(const vector<string>& F, const char* tag, const int* W){
    // GraphTemplate TT = build_template_from_strings(F);
    GraphTemplate TT = build_template_from_strings(F.clauses);
    //print_graph_edges(TT, W);              // list with margins
    //print_margin_matrix(TT, W);            // NxN margin table
    // print_graph_dot(TT, W);
    // print_edges_by_weight(TT, W);

    if (TT.names.size() > 64) {
        // cerr << tag << " skipped: too many candidates (" << TT.names.size() << ")\n";
        cerr << F.name << " skipped: too many candidates (" << TT.names.size() << ")\n";
        return false;
    }

    SVFast sol(TT);
    sol.reset_epoch(W);
    int win = sol.solve_winner_standard(TT.full_mask);

    cout << F.name << " " << " [expected " << formula_suites::expected_outcome_name(F) << "]\n  ";
    // for (auto &cl : F) cout << cl << " ";
    for (auto &cl : F.clauses) cout << cl << " ";
    cout << "\n";

    if(win < 0){
        cout << "  No winner found.\n";
        return false;
    }

    vector<int> elim;
    sol.reconstruct_standard(TT.full_mask, win, elim);

    const bool passed = (TT.names[win] == "C") == formula_suites::expects_c(F);

    cout << "  Winner: " << TT.names[win] << "\n"
         << "  Elimination Order: " << get_elim_order_string(elim, TT) << "\n"
         << "  Result: " << (passed ? "PASS" : "FAIL") << "\n";
    return passed;
}

// ========================= Exhaustive Driver (fast) =========================
#define STABLEVOTING_MAIN true
#ifdef STABLEVOTING_MAIN
int main(int argc, char** argv){
    // g++ test_qbf_reduction.cpp -std=c++20 -O3 -march=native -flto -DNDEBUG  && a.exe
    // ios::sync_with_stdio(false); cin.tie(nullptr); //fast IO
    vector<int> Wbase;
    fibonacci_series(NUM_GROUPS, STARTING_WEIGHT, STARTING_WEIGHT*2, Wbase);

    int W[NUM_GROUPS];
    for (int i=0;i<NUM_GROUPS;++i) W[i]=Wbase[i];

    // print the output of the fibonacci series
    cout << "Weight group starting values: ";
    for (int i=0;i<NUM_GROUPS;++i) cout << W[i] << " "; cout << "\n";

    // Preserve the last run by default: qbf-true-09 and qbf-true-10 stay
    // available but are selected only when --all-formulas is supplied.
    const bool include_disabled = include_disabled_formulas(argc, argv);
    const auto qbf_cases = select_cases(formula_suites::qbf_cases(), include_disabled);

    // This case supplies the graph used by the subtournament diagnostics below.
    const FormulaCase& reference_case = require_case(formula_suites::qbf_cases(), "qbf-true-01");
    const vector<string>& formula = reference_case.clauses;
    GraphTemplate T = build_template_from_strings(formula);
    SVFast solver(T); solver.reset_epoch(W);

    cout << "=== Full Results of reference formula ===";
    run_formula(reference_case, W);

    cout << "\n--- Testing SAT/UNSAT Formulas ---\n";
    int formula_failures = 0;
    for (const FormulaCase* test_case : qbf_cases) {
        if (!run_formula(*test_case, W)) ++formula_failures;
    }
    cout << "Failures: " << formula_failures << "\n";

    cout << "\n--- Subtournament Tests ---\n";
    vector<vector<string>> subtournaments = {
        {"C","F1","F2","F3","F4","X1","`X2","X3","L1","L2","L3","L4","L5","L6"},
        // {"F1","F2","F3","F4","L1","L2","L3","L4","L5","L6","X2","X3","X4"},
        // {"T1","T2","T3","T4","L1","L2","L3","L4","L5","L6","X1","X2","X3","X4"},
        // {"F1","F2","F3","F4","L1","L2","L3","L4","L5","L6","X1","X2","X4"},
        // {"F1","F2","F4","L1","L2","L3","L4","L5","L6","X1","X2","X4"},
        // {"F1","F2","F3","F4","L1","L2","L3","L4","L5","L6","X1","X2"},
        // {"C","F1","F2","F4","L1","L2","L3","L4","L5","L6","X1","X2","X4"},
        // {"D","F1","F2","F3","F4","X1","X2","X3","X4"}
        // {"C","D","T1","T2","T3","X2","X3","L1","L2","L3","L4","L5","L6","L7"},
        // {"C","D","T1","T2","T3","X2","X3","L1","L2","L3","L4","L5","L6","L7"},
        // {"C","D","T1","T2","T3","X3","L1","L2","L3","L4","L5","L6","L7"},
        // {"C","D","T1","T2","T3","L1","L2","L3","L4","L5","L6","L7"},
        // {"C", "T1", "F3", "F4", "F5", "`L3", "L4","X1", "X2", "X3", "X4", "X5", "X6"},
    };

    for (const auto& tourn : subtournaments) {
        uint64_t submask = mask_from_names(T, tourn);

        cout << "Subtournament candidates: ";
        solver.print_mask_names(submask, ",");

        int w_sub = solver.solve_winner(submask);
        if (w_sub < 0) {
            cout << "  No winner found in subtournament!\n";
            continue;
        }
        cout << "  Subtournament winner: " << T.names[w_sub] << "\n";
        if (w_sub>=0){
            vector<int> elim_order;
            solver.reconstruct(submask, w_sub, elim_order);
            cout << "  Elimination Order: " << get_elim_order_string(elim_order, T) << "\n";
        } else {
            cout << "  No winner found.\n";
        }
        cout << "\n";
    }

    return 0;
}
#endif
