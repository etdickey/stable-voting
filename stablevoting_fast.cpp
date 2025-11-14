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

    // Base four clauses
    vector<string> base = {"(x1, x2)", "(x1, ~x2)", "(~x1, x2)", "(~x1, ~x2)"};

    // Build clause sets: all C(4,2) and C(4,3) for SAT suite
    vector<vector<string>> sat_sets;
    for (int i=0;i<4;++i) for (int j=i+1;j<4;++j) sat_sets.push_back({base[i],base[j]});
    for (int i=0;i<4;++i) for (int j=i+1;j<4;++j) for (int k=j+1;k<4;++k) sat_sets.push_back({base[i],base[j],base[k]});

    // UNSAT suites
    vector<vector<string>> unsat_sets = {
        {"(x1, x2)", "(x1, ~x2)", "(~x1, x2)", "(~x1, ~x2)"},
        {"(x1, ~x2)", "(~x1, x2)", "(x2, ~x3)", "(~x2, x3)", "(x1, x3)", "(~x1, ~x3)"},
        {"(x1, x2)", "(x1, ~x2)", "(~x1, x3)", "(~x1, ~x3)"},
        {"(~x1, x2)", "(~x2, x3)", "(~x3, ~x1)", "(x1, ~x2)", "(x2, ~x3)", "(x3, x1)"}
    };

    // Prebuild templates (one-time)
    vector<GraphTemplate> T_sat; T_sat.reserve(sat_sets.size());
    for (auto &v: sat_sets) T_sat.push_back(build_template_from_strings(v));
    vector<GraphTemplate> T_unsat; T_unsat.reserve(unsat_sets.size());
    for (auto &v: unsat_sets) T_unsat.push_back(build_template_from_strings(v));
    GraphTemplate T_all = build_template_from_strings(base); // full 4-clause set

    // Prepare baseline triple (winner, order, edge) for T_all
    vector<int> Wbase; fibonacci_series(NUM_GROUPS, STARTING_WEIGHT, STARTING_WEIGHT*2, Wbase);
    vector<int> W = Wbase; sort(W.begin(), W.end()); // ensure lexicographic start for next_permutation

    // init solver with all 4 2-sat 2-variable clauses
    int W_all_base[NUM_GROUPS]; for (int i=0;i<NUM_GROUPS;++i) W_all_base[i]=Wbase[i];
    SVFast solver_all(T_all);
    solver_all.reset_epoch(W_all_base);
    int base_winner = solver_all.solve_winner((T_all.N==64?~0ull:((1ull<<T_all.N)-1ull)));
    vector<int> base_elim;
    solver_all.reconstruct((T_all.N==64?~0ull:((1ull<<T_all.N)-1ull)), base_winner, base_elim);

    // Print baseline summary
    cout << "Clause: ";
    copy(base.begin(), base.end(), ostream_iterator<string>(cout, " ")); cout << '\n';
    cout << "Baseline winner: " << (base_winner>=0? T_all.names[base_winner] : string("None")) << '\n';
    cout << "Baseline elim order: [";
    for (size_t i=0;i<base_elim.size();++i){
        if(i) cout<<", ";
        cout<<T_all.names[base_elim[i]];
    }
    cout<<"]\n\n";
    // print_graph_edges(T_all, W);              // list with margins
    // print_margin_matrix(T_all, W);            // NxN margin table
    // print_graph_dot(T_all, W);
    // print_edges_by_weight(T_all, W);

    // Stats
    const size_t total_clause_runs = sat_sets.size() + unsat_sets.size();
    size_t perms_done=0, all_failures=0;
    double total_time=0.0;

    // Prepare solvers per template (reuse memory)
    vector<SVFast> sol_sat; sol_sat.reserve(T_sat.size()); for (auto &t: T_sat) sol_sat.emplace_back(t);
    vector<SVFast> sol_uns; sol_uns.reserve(T_unsat.size()); for (auto &t: T_unsat) sol_uns.emplace_back(t);

    // permutation weights buffer
    int Wperm[NUM_GROUPS];

    auto t_last = chrono::steady_clock::now();
    auto START = t_last;

    // Exhaustive permutations of 11 weights
    size_t total_perms=1; for(int i=2;i<=NUM_GROUPS;++i) total_perms*= (size_t)i;
    cout << "Exhaustively testing " << total_perms << " permutations..." << endl;

    bool may_fail = solver_all.RULE == 1;
    if(may_fail) cout << "  WARN: functions allowed to return no answer as a valid unsatisfiable check" << endl;
    do{
        auto t0 = chrono::steady_clock::now();
        for (int i=0;i<NUM_GROUPS;++i) Wperm[i]=W[i];
        bool success = true;


        // SAT: C must win
        for (size_t si=0; si<T_sat.size(); ++si){
            auto &T = T_sat[si]; auto &S = sol_sat[si];
            S.reset_epoch(Wperm);
            uint64_t full = (T.N==64?~0ull:((1ull<<T.N)-1ull));
            int w = S.solve_winner(full);
            if (w<0 || T.names[w] != "C") { ++all_failures; success = false; }
            if (w<0) {
                cerr << "ERROR ERROR: Unable to find winner in graph for SAT clause: ";
                copy(sat_sets[si].begin(), sat_sets[si].end(), ostream_iterator<string>(cout, " "));
                cerr << "\n  permutation " << perms_done << endl;
                cout << "========ERROR: SEE ERROR OUTPUT========" << endl;
                return -1;
            }
        }

        // UNSAT: C must NOT win
        for (size_t ui=0; ui<T_unsat.size(); ++ui){
            auto &T = T_unsat[ui]; auto &S = sol_uns[ui];
            S.reset_epoch(Wperm);
            uint64_t full = (T.N==64?~0ull:((1ull<<T.N)-1ull));
            int w = S.solve_winner(full);
            if (w>=0 && T.names[w] == "C") { ++all_failures; success = false; }
            if (w<0 && !may_fail) {
                cerr << "ERROR ERROR: Unable to find winner in graph for UNSAT clause: ";
                copy(unsat_sets[ui].begin(), unsat_sets[ui].end(), ostream_iterator<string>(cout, " "));
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

            // ---- Print SAT cases: winner, decisive edge, and full elimination order ----
            cout << "SAT cases (" << T_sat.size() << "):\n";
            for (size_t si = 0; si < T_sat.size(); ++si) {
                const auto& T = T_sat[si];
                auto& S = sol_sat[si];
                S.reset_epoch(Wperm);

                // Full bitmask of active nodes for this template
                uint64_t full = (T.N == 64 ? ~0ull : ((1ull << T.N) - 1ull));

                // Solve and reconstruct elimination path
                int w = S.solve_winner(full);
                vector<int> elim;
                S.reconstruct(full, w, elim);

                // clause set, winner, elimination order
                cout << "  [" << si << "] " << join_clauses(sat_sets[si]) << "\n";
                cout << "     winner=" << (w >= 0 ? T.names[w] : string("None"));
                cout << "     elim=[";
                for (size_t i = 0; i < elim.size(); ++i) { if (i) cout << ','; cout << T.names[elim[i]]; }
                cout << "]\n";
            }

            // ---- Print UNSAT cases: winner, decisive edge, and full elimination order ----
            cout << "UNSAT cases (" << T_unsat.size() << "):\n";
            for (size_t ui = 0; ui < T_unsat.size(); ++ui) {
                const auto& T = T_unsat[ui];
                auto& S = sol_uns[ui];
                S.reset_epoch(Wperm);

                uint64_t full = (T.N == 64 ? ~0ull : ((1ull << T.N) - 1ull));
                int w = S.solve_winner(full);
                // clause set, winner, elimination order
                cout << "  [" << ui << "] " << join_clauses(unsat_sets[ui]) << "\n";
                cout << "     winner=" << (w >= 0 ? T.names[w] : string("None"));
                if(w>=0){
                    vector<int> elim;
                    S.reconstruct(full, w, elim);

                    cout << "     elim=[";
                    for (size_t i = 0; i < elim.size(); ++i) { if (i) cout << ','; cout << T.names[elim[i]]; }
                    cout << "]";
                }
                cout << "\n";
            }
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
