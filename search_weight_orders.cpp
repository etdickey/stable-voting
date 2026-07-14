/**
 * @file search_weight_orders.cpp
 * @brief Searches group-weight orderings for the Stable Voting reduction.
 *
 * This executable builds the canonical formula templates and reusable SVFast
 * solver instances, then evaluates candidate permutations of the reduction's
 * weight groups. A permutation succeeds when every formula produces its
 * declared expected outcome.
 *
 * The driver is responsible for:
 *   - enumerating or sampling weight orders;
 *   - resetting and invoking the solver for each formula;
 *   - reporting successful orders and failed cases; and
 *   - recording progress and timing statistics.
 *
 * Formula definitions and common parsing/building utilities are intentionally
 * kept in formula_suites.hpp and experiment_support.hpp. This is an experimental
 * search program rather than a unit-test executable, and exhaustive enumeration
 * may require factorial running time.
 *
 * Fast build and run:
 *   g++ -std=c++20 -O3 -march=native -flto -DNDEBUG search_weight_orders.cpp -o search_weight_orders && search_weight_orders
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
using FormulaCaseRefs = vector<const FormulaCase*>;

// ===============================BASE CLAUSE===============================
static AI void sanity_check(const int NUM_GROUPS, vector<int> Wbase){
    // Base four clauses
    // vector<string> base = {"(x1, x2)", "(x1, ~x2)", "(~x1, x2)", "(~x1, ~x2)"};
    const auto& base_case = require_case(two_sat_cases(), "2sat-false-01");
    const auto& base = base_case.clauses;
    GraphTemplate T_all = build_template_from_strings(base); // full 4-clause set

    // init solver with all 4 2-sat 2-variable clauses
    // int W_all_base[NUM_GROUPS]; for (int i=0;i<NUM_GROUPS;++i) W_all_base[i]=Wbase[i];
    vector<int> W_all_base = move(Wbase);
    if (static_cast<int>(W_all_base.size()) != NUM_GROUPS) {
        throw invalid_argument("Unexpected number of group weights");
    }
    SVFast solver_all(T_all);
    solver_all.reset_epoch(W_all_base.data());

    int base_winner = solver_all.solve_winner(T_all.full_mask);
    vector<int> base_elim;
    solver_all.reconstruct(T_all.full_mask, base_winner, base_elim);

    // Print baseline summary
    cout << "Clause: ";
    copy(base.begin(), base.end(), ostream_iterator<string>(cout, " ")); cout << '\n';
    cout << "Baseline winner: " << (base_winner>=0? T_all.names[base_winner] : string("None")) << '\n';
    cout << "Baseline elim order: " << get_elim_order_string(base_elim, T_all) << "\n\n";

    // print_graph_edges(T_all, W);              // list with margins
    // print_margin_matrix(T_all, W);            // NxN margin table
    // print_graph_dot(T_all, W);
    // print_edges_by_weight(T_all, W);
}
// ============================END BASE CLAUSE==============================

static AI void get_graphs_and_solvers(const FormulaCaseRefs& cases, vector<GraphTemplate>& templates, vector<SVFast>& solvers) {
    solvers.clear(); templates.clear();

    templates.reserve(cases.size());
    // for (const FormulaCase* test_case : cases) templates.push_back(build_template_from_strings(test_case->clauses));
    for (auto *test_case: cases) templates.push_back(build_template_from_strings(test_case->clauses));

    solvers.reserve(templates.size());
    // for (const GraphTemplate& graph : templates) solvers.emplace_back(graph);
    for (auto &graph: templates) solvers.emplace_back(graph);
}

// ========================= Exhaustive Driver (fast) =========================
#define STABLEVOTING_MAIN true
#ifdef STABLEVOTING_MAIN
int main(int argc, char** argv){
    // g++ search_weight_orders.cpp -std=c++20 -O3 -march=native -flto -DNDEBUG && a.exe
    // ios::sync_with_stdio(false); cin.tie(nullptr); //fast IO

    const size_t PRINT_EVERY = 50000;  // progress cadence
    const double TIME_EVERY_SEC = 60.0;

    // Preserve the last run's selection by default.
    // const bool include_disabled = has_flag(argc, argv, "--all-formulas");
    const bool include_disabled = include_disabled_formulas(argc, argv);

    const FormulaCaseRefs sat_cases = select_cases(two_sat_cases(), ExpectedValue::True, include_disabled);
    const FormulaCaseRefs unsat_cases = select_cases(two_sat_cases(), ExpectedValue::False, include_disabled);
    const FormulaCaseRefs sat3_cases = select_cases(three_sat_cases(), ExpectedValue::True, include_disabled);
    const FormulaCaseRefs unsat3_cases = select_cases(three_sat_cases(), ExpectedValue::False, include_disabled);

    const size_t total_clause_runs = sat_cases.size() + unsat_cases.size() + sat3_cases.size() + unsat3_cases.size();

    cout << "Selected " << total_clause_runs << " formula cases"
         << (include_disabled ? " (including disabled cases).\n"
                              : ". Use --all-formulas to include disabled cases.\n");

    // Prebuild templates (one-time)
    vector<GraphTemplate> T_sat, T_unsat, T_sat3, T_unsat3;
    // Prepare solvers per template (reuse memory)
    vector<SVFast> sol_sat, sol_uns, sol_sat3, sol_uns3;
    get_graphs_and_solvers(sat_cases, T_sat, sol_sat);
    get_graphs_and_solvers(unsat_cases, T_unsat, sol_uns);
    get_graphs_and_solvers(sat3_cases, T_sat3, sol_sat3);
    get_graphs_and_solvers(unsat3_cases, T_unsat3, sol_uns3);

    // Prepare baseline weight list (one per group)
    vector<int> Wbase; fibonacci_series(NUM_GROUPS, STARTING_WEIGHT, STARTING_WEIGHT*2, Wbase);
    // sanity_check(NUM_GROUPS, Wbase);

    // Stats
    size_t perms_done=0, all_failures=0;
    double total_time=0.0;


    // permutation weights buffer TODO: Why are these separate variables? (W and Wperm)
    vector<int> W = Wbase; sort(W.begin(), W.end()); // ensure lexicographic start for next_permutation
    int Wperm[NUM_GROUPS];

    auto t_last = chrono::steady_clock::now();
    auto START = t_last;

    // Exhaustive permutations of 11 weights
    size_t total_perms=1; for(int i=2;i<=NUM_GROUPS;++i) total_perms*= (size_t)i;
    cout << "Exhaustively testing " << total_perms << " permutations..." << endl;

    bool may_fail = SVFast::MAY_FAIL;
    if(may_fail) cout << "  WARN: functions allowed to return no answer as a valid unsatisfiable check\n";
    cout << "Running with configuration:\n  " << SVFast::static_config_string() << "\n";
    do{
        auto t0 = chrono::steady_clock::now();
        for (int i=0;i<NUM_GROUPS;++i) Wperm[i]=W[i];
        bool success = true, fatal_error = false;

        auto check_cases = [&](const FormulaCaseRefs& cases,
                               vector<GraphTemplate>& templates,
                               vector<SVFast>& solvers) {
            for (size_t i = 0; i < cases.size(); ++i) {
                const FormulaCase& test_case = *cases[i];
                GraphTemplate& graph = templates[i];
                SVFast& solver = solvers[i];

                solver.reset_epoch(Wperm);
                const int winner = solver.solve_winner(graph.full_mask);

                // A missing winner is accepted only for a false case under a
                // solver configuration that explicitly permits failure.
                if (winner < 0) {
                    if (!formula_suites::expects_c(test_case) && may_fail) {
                        continue;
                    }
                    cerr << "Unable to find a winner for " << test_case.name
                         << " at permutation " << perms_done << ": "
                         << join_clauses(test_case.clauses) << '\n';
                    cout << "========ERROR: SEE ERROR OUTPUT========" << endl;
                    fatal_error = true;
                    return false;
                }

                const bool c_won = graph.names[winner] == "C";
                if (c_won != formula_suites::expects_c(test_case)) {
                    ++all_failures;
                    return false;
                }
            }
            return true;
        };

        const bool success =
            check_cases(sat_cases, T_sat, sol_sat) && // 2SAT: C must win
            check_cases(unsat_cases, T_unsat, sol_uns) && // 2UNSAT: D must win
            check_cases(sat3_cases, T_sat3, sol_sat3) && // 3SAT: C must win
            check_cases(unsat3_cases, T_unsat3, sol_uns3);  // 3UNSAT: D must win
        if (fatal_error) return -1;

        // if (success) {
        //     // Found a permutation where all SATs pass (C wins) and all UNSATs pass (C does not win).
        //     cout << "FOUND ONE! perms_done=" << perms_done << " weights=[";
        //     for (int g = 0; g < NUM_GROUPS; ++g) { if (g) cout << ','; cout << Wperm[g]; }
        //     cout << "]\n";
        //
        //     // ---- Print cases: winner, decisive edge, and full elimination order ----
        //     print_formula_cases("2SAT true cases", sat_cases, T_sat, sol_sat, Wperm);
        //     print_formula_cases("2SAT false cases", unsat_cases, T_unsat, sol_uns, Wperm);
        //     print_formula_cases("3SAT true cases", sat3_cases, T_sat3, sol_sat3, Wperm);
        //     print_formula_cases("3SAT false cases", unsat3_cases, T_unsat3, sol_uns3, Wperm);
        // }

        auto t1 = chrono::steady_clock::now();
        total_time += chrono::duration<double>(t1-t0).count();
        ++perms_done;

        auto now = chrono::steady_clock::now();
        if ((perms_done % PRINT_EVERY == 0) || chrono::duration<double>(now - t_last).count() >= TIME_EVERY_SEC){
            double avg = total_time / max<size_t>(1, perms_done);
            double per_case = total_time / max<size_t>(1, perms_done*total_clause_runs);
            cout.setf(ios::fixed); cout<<setprecision(4);
            cout << "[PROGRESS] perms_done="<<perms_done
                 << " fails="<<all_failures
                 << " avg_perm="<<avg<<"s"
                 << " avg_case="<<per_case<<"s"<<endl;
            t_last = now;
        }
    } while (next_permutation(W.begin(), W.end()));

    auto now = chrono::steady_clock::now();
    auto sec = chrono::duration<double>(now - START).count();
    cout << "\nDONE. perms_done="<<perms_done
         << " fails="<<all_failures
         << " total time=" << sec << "s = ~" << sec/60.0 << "m = ~" << (sec/60.0)/60.0 << "hr"
         << "\n";

    return 0;
}
#endif
