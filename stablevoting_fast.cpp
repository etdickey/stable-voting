// stablevoting_fast.cpp
// High-performance rewrite (competitive-programming style)
// - No regex, minimal strings, zero heap churn in hot paths
// - Bitmask DP memo (O(2^N)) with epoch tagging (O(1) clears)
// - Prebuilt tournament template with group+offset per directed edge
// - Per-permutation margins computed on-the-fly from group weights
// - Optional "diff" reconstruction using memoized winners
//
// Build (Linux/Clang/GCC):
//   g++ -O3 -march=native -DNDEBUG -std=c++20 stablevoting_fast.cpp -o stablevoting
//   # add -DSTABLEVOTING_MAIN to run the exhaustive driver
//
//==============================================
//USE THIS ONE:::
// g++ stablevoting_fast.cpp -O3 -march=native -flto -fomit-frame-pointer -DNDEBUG && a.exe
//==============================================

#include <bits/stdc++.h> //includes basically everything you could need, including iomanip
#include <iostream>
using namespace std;

#include "fast_utils.hpp"
#include "graph_template.hpp"
#include "template_builders.hpp"
#include "sv_fast.hpp"
#include "printers.hpp"

#if defined(__GNUC__) || defined(__clang__)
  #define AI inline __attribute__((always_inline))
#else
  #define AI inline
#endif

// ========================= Clause Parsing (fast) =========================
// Accepts tokens like: (x1, ~x2) or x3 or ~x10; commas/parentheses/whitespace ignored
// Output: vector of (var_index>=1, polarity=true for x, false for ~x)
static AI void parse_clause_fast(const string& s, vector<pair<int,bool>>& out, int& max_var){
    out.clear();
    const char* p = s.c_str();
    while (*p){
        while (*p && (*p==' '||*p=='\t'||*p=='\n'||*p=='\r'||*p==','||*p=='('||*p==')')) ++p;
        if (!*p) break;
        bool pos = true;
        if (*p=='~') { pos=false; ++p; }
        if (*p=='x' || *p=='X') {
            ++p; // skip 'x'
            int v=0; bool any=false;
            while (*p>='0' && *p<='9'){ v = v*10 + (*p-'0'); ++p; any=true; }
            if (any && v>=1){ out.emplace_back(v,pos); max_var = max(max_var, v); }
            // else ignore malformed tokens silently
        } else {
            // skip token
            while (*p && *p!=',' && *p!=')' && *p!='(') ++p;
        }
    }
}

// Convenience: from strings like "(x1, ~x2)", "(x1, x2)"
static GraphTemplate build_template_from_strings(const vector<string>& clause_strs){
    vector<vector<pair<int,bool>>> clauses;
    clauses.reserve(clause_strs.size());
    int maxv=0; vector<pair<int,bool>> tmp;
    for (auto &s: clause_strs){ parse_clause_fast(s, tmp, maxv); clauses.push_back(tmp); }
    return build_template_from_clauses(clauses);
}

// ========================= Group Weights =========================
// this starts in increasing order first, and groups are assigned 0->11
static AI void fibonacci_series(int n, int seed1, int seed2, vector<int>& out){
    out.clear(); out.reserve(n);
    if (n<=0) return;
    if (n==1){ out.push_back(seed1); return; }
    long long a=seed1,b=seed2; out.push_back((int)a); out.push_back((int)b);
    for (int i=2;i<n;++i){ long long c=a+b; if (c>INT_MAX) c=INT_MAX; out.push_back((int)c); a=b; b=c; }
}

// ===============================BASE CLAUSE===============================
static AI void sanity_check(const int NUM_GROUPS, vector<int> Wbase){
    // Base four clauses
    vector<string> base = {"(x1, x2)", "(x1, ~x2)", "(~x1, x2)", "(~x1, ~x2)"};
    GraphTemplate T_all = build_template_from_strings(base); // full 4-clause set

    // init solver with all 4 2-sat 2-variable clauses
    int W_all_base[NUM_GROUPS]; for (int i=0;i<NUM_GROUPS;++i) W_all_base[i]=Wbase[i];
    SVFast solver_all(T_all);
    solver_all.reset_epoch(W_all_base);

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

static AI void get_2sat_clauses(vector<vector<string>>& sat_sets, vector<vector<string>>& unsat_sets, vector<array<int,2>>& sat_assignments, vector<array<int,2>>& unsat_assignments){
    sat_sets.clear(); unsat_sets.clear(); sat_assignments.clear(); unsat_assignments.clear();
    // Base four clauses
    vector<string> base = {"(x1, x2)", "(x1, ~x2)", "(~x1, x2)", "(~x1, ~x2)"};

    // Build clause sets: all C(4,2) and C(4,3) for SAT suite
    for (int i=0;i<4;++i) for (int j=i+1;j<4;++j) sat_sets.push_back({base[i],base[j]});
    for (int i=0;i<4;++i) for (int j=i+1;j<4;++j) for (int k=j+1;k<4;++k) sat_sets.push_back({base[i],base[j],base[k]});
    // UNSAT suites
    unsat_sets = {
        {"(x1, x2)", "(x1, ~x2)", "(~x1, x2)", "(~x1, ~x2)"},
        {"(x1, ~x2)", "(~x1, x2)", "(x2, ~x3)", "(~x2, x3)", "(x1, x3)", "(~x1, ~x3)"},
        {"(x1, x2)", "(x1, ~x2)", "(~x1, x3)", "(~x1, ~x3)"},
        {"(~x1, x2)", "(~x2, x3)", "(~x3, ~x1)", "(x1, ~x2)", "(x2, ~x3)", "(x3, x1)"}
    };
}

static AI void get_3sat_clauses(vector<vector<string>>& sat_sets, vector<vector<string>>& unsat_sets, vector<array<int,3>>& sat_assignments, vector<array<int,3>>& unsat_assignments){
    sat_sets.clear(); unsat_sets.clear(); sat_assignments.clear(); unsat_assignments.clear();
    // 3-SAT test suite: first 8 are SAT with designated (x1,x2,x3), next 7 are UNSAT (assignment = -1),
    // and the last one is a mixed-size SAT clause set with its own designated assignment.
    sat_sets = {
        {"(~x1, ~x2, ~x3)", "(~x1, ~x2, x3)", "(~x1, x2, ~x3)"},   //  0 SAT,  (x1,x2,x3) = (0,0,0)
        {"(~x1, ~x2, x3)", "(~x1, x2, x3)", "(x1, ~x2, x3)"},      //  1 SAT,  (x1,x2,x3) = (0,0,1)
        {"(~x1, x2, ~x3)", "(~x1, x2, x3)", "(x1, x2, ~x3)"},      //  2 SAT,  (x1,x2,x3) = (0,1,0)
        {"(~x1, x2, x3)", "(~x1, x2, ~x3)", "(x1, x2, x3)"},       //  3 SAT,  (x1,x2,x3) = (0,1,1)
        {"(x1, ~x2, ~x3)", "(x1, ~x2, x3)", "(x1, x2, ~x3)"},      //  4 SAT,  (x1,x2,x3) = (1,0,0)
        {"(x1, ~x2, x3)", "(x1, ~x2, ~x3)", "(~x1, ~x2, x3)"},     //  5 SAT,  (x1,x2,x3) = (1,0,1)
        {"(x1, x2, ~x3)", "(x1, x2, x3)", "(~x1, x2, ~x3)"},       //  6 SAT,  (x1,x2,x3) = (1,1,0)
        {"(x1, x2, x3)", "(x1, x2, ~x3)", "(x1, ~x2, x3)"},        //  7 SAT,  (x1,x2,x3) = (1,1,1)

        {"(x1, ~x2, x3)", "(~x1, x2, x3)", "(x1, x2, ~x3)", "(~x1, ~x2, ~x3)"},  // 15 SAT mixed-size (4 clauses), (x1,x2,x3) = (1,0,1)
        {"(~x1, x2, x3)", "(x1, x2, x3)"},                                      // 16 SAT mixed-size (2 clauses), assignment (0,1,1)
        {"(x1, ~x2, ~x3)", "(x1, ~x2, x3)", "(x1, x2, ~x3)"},                   // 17 SAT mixed-size (3 clauses), assignment (1,0,0)
        {"(~x1, x2, ~x3)", "(~x1, x2, x3)", "(x1, x2, ~x3)"},                   // 18 SAT mixed-size (3 clauses), assignment (0,1,0)
        {"(x1, x2, x3)", "(x1, ~x2, x3)"},                                      // 19 SAT mixed-size (2 clauses), assignment (1,1,1)
        {"(~x1, ~x2, x3)", "(x1, ~x2, x3)"},                                    // 20 SAT mixed-size (2 clauses), assignment (0,0,1)
        {"(x1, x2, ~x3)", "(x1, x2, x3)", "(~x1, x2, ~x3)", "(x1, ~x2, ~x3)"},  // 21 SAT mixed-size (4 clauses), assignment (1,1,0)
        {"(~x1, x2, x3)", "(~x1, x2, ~x3)", "(x1, x2, x3)", "(~x1, ~x2, x3)"},  // 22 SAT mixed-size (4 clauses), assignment (0,1,1)
        {"(x1, ~x2, x3)", "(~x1, ~x2, x3)", "(x1, x2, x3)"},                    // 23 SAT mixed-size (3 clauses), assignment (1,0,1)
        {"(~x1, x2, ~x3)", "(~x1, x2, x3)", "(x1, ~x2, ~x3)", "(x1, x2, ~x3)"}, // 24 SAT mixed-size (4 clauses), assignment (0,1,0)
        {"(x1, ~x2, x3)", "(x1, x2, x3)", "(~x1, ~x2, x3)"},                    // 25 SAT mixed-size (3 clauses), assignment (1,0,1)
        {"(~x1, ~x2, ~x3)", "(x1, ~x2, ~x3)", "(~x1, x2, ~x3)"},                // 26 SAT mixed-size (3 clauses), assignment (0,0,0)
        {"(x1, x2, x3)", "(x1, x2, ~x3)", "(x1, ~x2, x3)", "(~x1, x2, x3)"}     // 27 SAT mixed-size (4 clauses), assignment (1,1,1)
    };

    unsat_sets = {
        {"(x1, x2, x3)","(~x1, x2, x3)","(x1, ~x2, x3)","(x1, x2, ~x3)","(~x1, ~x2, x3)","(~x1, x2, ~x3)","(x1, ~x2, ~x3)","(~x1, ~x2, ~x3)"}, //  8 UNSAT full cube
        {"(x2, x1, x3)","(x2, ~x1, x3)","(~x2, x1, x3)","(x2, x1, ~x3)","(~x2, ~x1, x3)","(x2, ~x1, ~x3)","(~x2, x1, ~x3)","(~x2, ~x1, ~x3)"}, //  9 UNSAT permuted literals
        {"(x1, x3, x2)","(~x1, x3, x2)","(x1, x3, ~x2)","(x1, ~x3, x2)","(~x1, x3, ~x2)","(~x1, ~x3, x2)","(x1, ~x3, ~x2)","(~x1, ~x3, ~x2)"}, // 10 UNSAT permuted literals
        {"(~x1, x2, x3)","(x1, x2, x3)","(~x1, ~x2, x3)","(~x1, x2, ~x3)","(x1, ~x2, x3)","(x1, x2, ~x3)","(~x1, ~x2, ~x3)","(x1, ~x2, ~x3)"}, // 11 UNSAT base (~x1,x2,x3)
        {"(x1, ~x2, x3)","(~x1, ~x2, x3)","(x1, x2, x3)","(x1, ~x2, ~x3)","(~x1, x2, x3)","(~x1, ~x2, ~x3)","(x1, x2, ~x3)","(~x1, x2, ~x3)"}, // 12 UNSAT base (x1,~x2,x3)
        {"(x1, x2, ~x3)","(~x1, x2, ~x3)","(x1, ~x2, ~x3)","(x1, x2, x3)","(~x1, ~x2, ~x3)","(~x1, x2, x3)","(x1, ~x2, x3)","(~x1, ~x2, x3)"}, // 13 UNSAT base (x1,x2,~x3)
        {"(~x1, ~x2, x3)","(x1, ~x2, x3)","(~x1, x2, x3)","(~x1, ~x2, ~x3)","(x1, x2, x3)","(x1, ~x2, ~x3)","(~x1, x2, ~x3)","(x1, x2, ~x3)"}, // 14 UNSAT base (~x1,~x2,x3)
        // UNSAT #0: implication cycle x1 -> x2 -> x3 -> ¬x1, plus tautology on x4,x5
        {"(~x1,x2)","(~x2,x3)","(~x3,~x1)","(x1)","(x4,~x4,x5)"},
        // UNSAT #1: force x2,x3,x4,x5 to 0, then contradict x1 via two clauses
        {"(x1,x2,x3)","(~x2,x4)","(~x2,~x4)","(~x3,x5)","(~x3,~x5)","(~x1,x4,x5)","(~x4,x2)","(~x4,~x2)","(~x5,x3)","(~x5,~x3)"},
        // UNSAT #2: equivalence cycle x1 ↔ x2 ↔ x3 ↔ ¬x1, plus tautology on x4,x5
        {"(~x1,x2)","(x1,~x2)","(~x2,x3)","(x2,~x3)","(~x3,~x1)","(x3,x1)","(x4,~x4,x5)"},
    };
    // vector<vector<string>> more_5var_unsat_clause_sets = {

    //     // UNSAT #3: equality + inequality on (x1,x2), plus tautologies for x3,x4,x5
    //     {"(~x1,x2,x2)",   //  x1 → x2
    //      "(x1,~x2,~x2)",  //  x2 → x1
    //      "(x1,x2,x2)",    //  x1 ∨ x2
    //      "(~x1,~x2,~x2)", //  ¬x1 ∨ ¬x2
    //      "(x3,~x3,x4)","(x4,~x4,x5)"},
    //
    //     // UNSAT #4: implication cycle on x2,x4,x5 with x2 forced true, plus x1,x3 tautology
    //     {"(~x2,x4,x4)",   // x2 → x4
    //      "(~x4,x5,x5)",   // x4 → x5
    //      "(~x5,~x2,~x2)", // x5 → ¬x2
    //      "(x2,x2,x2)",    // x2 forced true
    //      "(x1,~x1,x3)"},
    //
    //     // UNSAT #5: equality + inequality on (x3,x4), plus x1,x5
    //     {"(~x3,x4,x4)",   // x3 → x4
    //      "(x3,~x4,~x4)",  // x4 → x3
    //      "(x3,x4,x4)",    // x3 ∨ x4
    //      "(~x3,~x4,~x4)", // ¬x3 ∨ ¬x4
    //      "(x1,~x1,x5)"},
    //
    //     // UNSAT #6: eq chain x1↔x2↔x3 plus inequality x1≠x3, tautology on x4,x5
    //     {"(~x1,x2,x2)","(x1,~x2,~x2)","(~x2,x3,x3)","(x2,~x3,~x3)","(x1,x3,x3)",     // x1 ∨ x3
    //      "(~x1,~x3,~x3)",  // ¬x1 ∨ ¬x3
    //      "(x4,~x4,x5)"},
    //
    //     // UNSAT #7: equivalence cycle on x2,x4,x5 plus tautology on x1,x3
    //     {"(~x2,x4,x4)",   // x2 ↔ x4
    //      "(x2,~x4,~x4)",
    //      "(~x4,x5,x5)",   // x4 ↔ x5
    //      "(x4,~x5,~x5)",
    //      "(~x5,~x2,~x2)", // x5 ↔ ¬x2
    //      "(x5,x2,x2)",
    //      "(x1,~x1,x3)"},
    //
    //     // UNSAT #8: equality + inequality on (x1,x3), plus x2,x4,x5
    //     {"(~x1,x3,x3)",   // x1 ↔ x3
    //      "(x1,~x3,~x3)",
    //      "(x1,x3,x3)",    // x1 ∨ x3
    //      "(~x1,~x3,~x3)", // ¬x1 ∨ ¬x3
    //      "(x2,~x2,x4)",
    //      "(x4,~x4,x5)"},
    //
    //     // UNSAT #9: force x4=0 and x5=0, then (x1 ∨ x4 ∨ x5) and (¬x1 ∨ x4 ∨ x5)
    //     {"(~x4,x1,x1)",   // force x4 = 0 via x1
    //      "(~x4,~x1,~x1)",
    //      "(~x5,x2,x2)",   // force x5 = 0 via x2
    //      "(~x5,~x2,~x2)",
    //      "(x1,x4,x5)",
    //      "(~x1,x4,x5)",
    //      "(x3,~x3,x4)"}   // bring in x3
    // };

    // vector<array<int,5>> more_5var_unsat_assignments = {
    //     {-1,-1,-1,-1,-1},  // UNSAT #0
    //     {-1,-1,-1,-1,-1},  // UNSAT #1
    //     {-1,-1,-1,-1,-1},  // UNSAT #2
    //     {-1,-1,-1,-1,-1},  // UNSAT #3
    //     {-1,-1,-1,-1,-1},  // UNSAT #4
    //     {-1,-1,-1,-1,-1},  // UNSAT #5
    //     {-1,-1,-1,-1,-1},  // UNSAT #6
    //     {-1,-1,-1,-1,-1},  // UNSAT #7
    //     {-1,-1,-1,-1,-1},  // UNSAT #8
    //     {-1,-1,-1,-1,-1}   // UNSAT #9
    // };


    sat_assignments = {
        {0,0,0},  //  0 SAT  (x1,x2,x3) = (0,0,0)
        {0,0,1},  //  1 SAT  (x1,x2,x3) = (0,0,1)
        {0,1,0},  //  2 SAT  (x1,x2,x3) = (0,1,0)
        {0,1,1},  //  3 SAT  (x1,x2,x3) = (0,1,1)
        {1,0,0},  //  4 SAT  (x1,x2,x3) = (1,0,0)
        {1,0,1},  //  5 SAT  (x1,x2,x3) = (1,0,1)
        {1,1,0},  //  6 SAT  (x1,x2,x3) = (1,1,0)
        {1,1,1},  //  7 SAT  (x1,x2,x3) = (1,1,1)

        {1,0,1},   // 15 SAT mixed-size, (x1,x2,x3) = (1,0,1)
        {0,1,1},   // 16 SAT (x1,x2,x3) = (0,1,1)
        {1,0,0},   // 17 SAT (x1,x2,x3) = (1,0,0)
        {0,1,0},   // 18 SAT (x1,x2,x3) = (0,1,0)
        {1,1,1},   // 19 SAT (x1,x2,x3) = (1,1,1)
        {0,0,1},   // 20 SAT (x1,x2,x3) = (0,0,1)
        {1,1,0},   // 21 SAT (x1,x2,x3) = (1,1,0)
        {0,1,1},   // 22 SAT (x1,x2,x3) = (0,1,1)
        {1,0,1},   // 23 SAT (x1,x2,x3) = (1,0,1)
        {0,1,0},   // 24 SAT (x1,x2,x3) = (0,1,0)
        {1,0,1},   // 25 SAT (x1,x2,x3) = (1,0,1)
        {0,0,0},   // 26 SAT (x1,x2,x3) = (0,0,0)
        {1,1,1},   // 27 SAT (x1,x2,x3) = (1,1,1)
    };

    unsat_assignments = {
        {-1,-1,-1}, //  8 UNSAT
        {-1,-1,-1}, //  9 UNSAT
        {-1,-1,-1}, // 10 UNSAT
        {-1,-1,-1}, // 11 UNSAT
        {-1,-1,-1}, // 12 UNSAT
        {-1,-1,-1}, // 13 UNSAT
        {-1,-1,-1}, // 14 UNSAT
        {-1,-1,-1}, // 5VAR UNSAT
        {-1,-1,-1}, // 5VAR UNSAT
        {-1,-1,-1}, // 5VAR UNSAT
    };
}

static AI void get_graphs_and_solvers(const vector<vector<string>>& sat_sets, vector<GraphTemplate>& T_sat, vector<SVFast>& sol_sat){
    T_sat.clear(); sol_sat.clear();
    // graph templates
    T_sat.reserve(sat_sets.size());
    for (auto &v: sat_sets) T_sat.push_back(build_template_from_strings(v));
    // solvers
    sol_sat.reserve(T_sat.size());
    for (auto &t: T_sat) sol_sat.emplace_back(t);
}


// ========================= Exhaustive Driver (fast) =========================
#define STABLEVOTING_MAIN true

#ifdef STABLEVOTING_MAIN
int main(){
    // g++ stablevoting_fast.cpp -O3 -march=native -flto -fomit-frame-pointer -DNDEBUG && a.exe
    // ios::sync_with_stdio(false); cin.tie(nullptr);

    const int STARTING_WEIGHT = 100;
    const int NUM_GROUPS = 11;        // effective weight buckets
    const size_t PRINT_EVERY = 50000;  // progress cadence
    const double TIME_EVERY_SEC = 60.0;

    vector<vector<string>> sat_sets,  unsat_sets;  vector<array<int,2>> sat_assignments,  unsat_assignments;
    vector<vector<string>> sat3_sets, unsat3_sets; vector<array<int,3>> sat3_assignments, unsat3_assignments;
    get_2sat_clauses(sat_sets,  unsat_sets,  sat_assignments,  unsat_assignments);
    get_3sat_clauses(sat3_sets, unsat3_sets, sat3_assignments, unsat3_assignments);

    // Prebuild templates (one-time)
    // vector<GraphTemplate> T_sat; T_sat.reserve(sat_sets.size());
    // for (auto &v: sat_sets) T_sat.push_back(build_template_from_strings(v));
    // vector<GraphTemplate> T_unsat; T_unsat.reserve(unsat_sets.size());
    // for (auto &v: unsat_sets) T_unsat.push_back(build_template_from_strings(v));
    // vector<SVFast> sol_sat; sol_sat.reserve(T_sat.size()); for (auto &t: T_sat) sol_sat.emplace_back(t);
    // vector<SVFast> sol_uns; sol_uns.reserve(T_unsat.size()); for (auto &t: T_unsat) sol_uns.emplace_back(t);

    // Prebuild templates (one-time)
    vector<GraphTemplate> T_sat, T_unsat, T_sat3, T_unsat3;
    // Prepare solvers per template (reuse memory)
    vector<SVFast> sol_sat, sol_uns, sol_sat3, sol_uns3;
    get_graphs_and_solvers(sat_sets, T_sat, sol_sat);
    get_graphs_and_solvers(unsat_sets, T_unsat, sol_uns);
    get_graphs_and_solvers(sat3_sets, T_sat3, sol_sat3);
    get_graphs_and_solvers(unsat3_sets, T_unsat3, sol_uns3);

    // Prepare baseline weight list (one per group)
    vector<int> Wbase; fibonacci_series(NUM_GROUPS, STARTING_WEIGHT, STARTING_WEIGHT*2, Wbase);
    sanity_check(NUM_GROUPS, Wbase);

    // Stats
    const size_t total_clause_runs = sat_sets.size() + unsat_sets.size();
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

    bool may_fail = (sol_sat[0].RULE == 1);
    if(may_fail) cout << "  WARN: functions allowed to return no answer as a valid unsatisfiable check" << endl;
    do{
        auto t0 = chrono::steady_clock::now();
        for (int i=0;i<NUM_GROUPS;++i) Wperm[i]=W[i];
        bool success = true;

        // 2SAT: C must win
        for (size_t si=0; si<T_sat.size(); ++si){
            auto &T = T_sat[si]; auto &S = sol_sat[si];
            S.reset_epoch(Wperm);
            int w = S.solve_winner(T.full_mask);
            if (w<0 || T.names[w] != "C") { ++all_failures; success = false; }
            if (w<0) {
                cerr << "ERROR ERROR: Unable to find winner in graph for SAT clause: ";
                copy(sat_sets[si].begin(), sat_sets[si].end(), ostream_iterator<string>(cout, " "));
                cerr << "\n  permutation " << perms_done << endl;
                cout << "========ERROR: SEE ERROR OUTPUT========" << endl;
                return -1;
            }
        }
        // 3SAT: C must win
        for (size_t si=0; si<T_sat3.size(); ++si){
            auto &T = T_sat3[si]; auto &S = sol_sat3[si];
            S.reset_epoch(Wperm);
            int w = S.solve_winner(T.full_mask);
            if (w<0 || T.names[w] != "C") { ++all_failures; success = false; }
            if (w<0) {
                cerr << "ERROR ERROR: Unable to find winner in graph for 3SAT clause: ";
                copy(sat3_sets[si].begin(), sat3_sets[si].end(), ostream_iterator<string>(cout, " "));
                cerr << "\n  permutation " << perms_done << endl;
                cout << "========ERROR: SEE ERROR OUTPUT========" << endl;
                return -1;
            }
        }

        // 2UNSAT: C must NOT win
        for (size_t ui=0; ui<T_unsat.size(); ++ui){
            auto &T = T_unsat[ui]; auto &S = sol_uns[ui];
            S.reset_epoch(Wperm);
            int w = S.solve_winner(T.full_mask);
            if (w>=0 && T.names[w] == "C") { ++all_failures; success = false; }
            if (w<0 && !may_fail) {
                cerr << "ERROR ERROR: Unable to find winner in graph for UNSAT clause: ";
                copy(unsat_sets[ui].begin(), unsat_sets[ui].end(), ostream_iterator<string>(cout, " "));
                cerr << "\n  permutation " << perms_done << endl;
                cout << "========ERROR: SEE ERROR OUTPUT========" << endl;
                return -1;
            }
        }
        // 3UNSAT: C must NOT win
        for (size_t ui=0; ui<T_unsat3.size(); ++ui){
            auto &T = T_unsat3[ui]; auto &S = sol_uns3[ui];
            S.reset_epoch(Wperm);
            int w = S.solve_winner(T.full_mask);
            if (w>=0 && T.names[w] == "C") { ++all_failures; success = false; }
            if (w<0 && !may_fail) {
                cerr << "ERROR ERROR: Unable to find winner in graph for 3UNSAT clause: ";
                copy(unsat3_sets[ui].begin(), unsat_sets[ui].end(), ostream_iterator<string>(cout, " "));
                cerr << "\n  permutation " << perms_done << endl;
                cout << "========ERROR: SEE ERROR OUTPUT========" << endl;
                return -1;
            }
        }

        if (success) {
            // Found a permutation where all SATs pass (C wins) and all UNSATs pass (C does not win).
            cout << "FOUND ONE! perms_done=" << perms_done << " weights=[";
            for (int g = 0; g < NUM_GROUPS; ++g) { if (g) cout << ','; cout << Wperm[g]; }
            cout << "]\n";

            // ---- Print 2SAT cases: winner, decisive edge, and full elimination order ----
            cout << "2SAT cases (" << T_sat.size() << "):\n";
            print_sat_cases<2>(T_sat, sol_sat, sat_sets, sat_assignments, Wperm, /*UNSAT=*/false);
            // ---- Print 2UNSAT cases: winner, decisive edge, and full elimination order ----
            cout << "2UNSAT cases (" << T_unsat.size() << "):\n";
            print_sat_cases<2>(T_unsat, sol_uns, unsat_sets, unsat_assignments, Wperm, /*UNSAT=*/true);
            // ---- Print 3SAT cases: winner, decisive edge, and full elimination order ----
            cout << "3SAT cases (" << T_sat3.size() << "):\n";
            print_sat_cases<3>(T_sat3, sol_sat3, sat3_sets, sat3_assignments, Wperm, /*UNSAT=*/false);
            // ---- Print 3UNSAT cases: winner, decisive edge, and full elimination order ----
            cout << "3UNSAT cases (" << T_unsat3.size() << "):\n";
            print_sat_cases<3>(T_unsat3, sol_uns3, unsat3_sets, unsat3_assignments, Wperm, /*UNSAT=*/true);
        }



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

        cout << "terminating after one iteration because spam" << endl; break;
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
