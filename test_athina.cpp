// stablevoting_fast.cpp
// High-performance rewrite (competitive-programming style)
// - No regex, minimal strings, zero heap churn in hot paths
// - Bitmask DP memo (O(2^N)) with epoch tagging (O(1) clears)
// - Prebuilt tournament template with group+offset per directed edge
// - Per-permutation margins computed on-the-fly from group weights
// - Optional "diff" reconstruction using memoized winners
//
// Build (Linux/Clang/GCC):
//   g++ -O3 -march=native -DNDEBUG -std=c++20 test_athina.cpp  -o stablevoting
//   # add -DSTABLEVOTING_MAIN to run the exhaustive driver
//
//==============================================
//USE THIS ONE:::
// g++ test_athina.cpp  -O3 -march=native -flto -fomit-frame-pointer -DNDEBUG -o sv && ./sv
//==============================================

#include <bits/stdc++.h> //includes basically everything you could need, including iomanip
#include <iostream>
using namespace std;

#include "fast_utils.hpp"
#include "graph_template.hpp"
// #include "template_builders.hpp"
#include "test_builder.hpp"
#include "sv_fast.hpp"
#include "printers.hpp"

#if defined(__GNUC__) || defined(__clang__)
  #define AI inline __attribute__((always_inline))
#else
  #define AI inline
#endif


uint64_t mask_from_names(const GraphTemplate& T,
                         const std::vector<std::string>& keep) {
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
    const int NUM_GROUPS = 12;        // effective weight buckets

    
    
   
    vector<int> Wbase;
    fibonacci_series(NUM_GROUPS, STARTING_WEIGHT, STARTING_WEIGHT*2, Wbase);
    
    int W[NUM_GROUPS];
    for (int i=0;i<NUM_GROUPS;++i) W[i]=Wbase[i];
    
    // print the output of the fibonacci series
    for (int i=0;i<NUM_GROUPS;++i) cout << W[i] << " "; cout << "\n";  

   
    vector<string> formula =  {"(x1, x2, x3)","(~x1, x2, x3)","(x1, ~x2, x3)","(x1, x2, ~x3)","(~x1, ~x2, x3)","(~x1, x2, ~x3)","(x1, ~x2, ~x3)","(~x1, ~x2, ~x3)"};
    // {"(x1, x2, x3)","(~x1, x2, x3)","(x1, ~x2, x3)","(x1, x2, ~x3)","(~x1, ~x2, x3)","(~x1, x2, ~x3)","(x1, ~x2, ~x3)","(~x1, ~x2, ~x3)"};
    
    // {"(x1, x2, x3)", "(~x1, x2, x3)","(x4,x5,x6)","(~x3,~x4,~x5)"}; //
    //{"(x1, x2, x3)","(~x1, x2, x3)","(x1, ~x2, x3)","(x1, x2, ~x3)","(~x1, ~x2, x3)","(~x1, x2, ~x3)","(x1, ~x2, ~x3)","(~x1, ~x2, ~x3)"};
    
    
    //{"(x1, x2)","(~x1, x2)","(x1, ~x2)","(~x1, ~x2)"}; //,"(~x1, ~x2)"

    //{"(x1, x2, x3)","(~x1, x2, x3)","(x1, ~x2, x3)","(x1, x2, ~x3)","(~x1, ~x2, x3)","(~x1, x2, ~x3)","(x1, ~x2, ~x3)", "(~x1, ~x2, ~x3)"};
    
        // {"(x1, x2, x3)","(x1, x2, x4)","(x1, x2, ~x5)","(x1, x2, x6)" };
    
        //{"(x1, x2, x3)","(~x1, x2, x3)","(x1, ~x2, x3)","(x1, x2, ~x3)","(~x1, ~x2, x3)","(~x1, x2, ~x3)","(x1, ~x2, ~x3)","(~x1, ~x2, ~x3)"}; //  8 UNSAT full cube
        //{"(x1, x2, x3)","(x1, ~x2, x3)", "(~x1, x2,~x3)", "(~x1, ~x2, ~x3)"};

        
     
    
    
    GraphTemplate T = build_template_from_strings(formula);
    SVFast solver(T);

    solver.reset_epoch(W);
    int winner = solver.solve_winner_standard(T.full_mask);



    //print_graph_edges(T, W);              // list with margins
    //print_margin_matrix(T, W);            // NxN margin table
    // print_graph_dot(T, W);
    print_edges_by_weight(T, W);


    if (winner>=0){
        cout << "Winner: " << T.names[winner] << "\n";
        vector<int> elim_order;
        solver.reconstruct_standard(T.full_mask, winner, elim_order);
        cout << "Elimination Order: " << get_elim_order_string(elim_order, T) << "\n";
    } else {
        cout << "No winner found.\n";
    }

    cout << "\n--- Subtournament Tests ---\n";

    cout << "Valid assignmets:\n";

    vector<vector<string>> keepValid = {
    {"C","R","T1","T2","T3","X1","X2","X3","L1","L2","L3","L4","L5","L6","L7"},
    {"C","R","T1","T2","T3","X2","X3","L1","L2","L3","L4","L5","L6","L7"},
    {"C","R","T1","T2","T3","X2","X3","L1","L2","L3","L4","L5","L6","L7"},
    {"C","R","T1","T2","T3","X3","L1","L2","L3","L4","L5","L6","L7"},
    {"C","R","T1","T2","T3","L1","L2","L3","L4","L5","L6","L7"},

    {"C","T1","T2","T3","X2","X3","L1","L2","L3","L4","L5","L6","L7"},
    {"C","T1","T2","T3","X2","X3","L1","L2","L3","L4","L5","L6","L7"},
    {"C","T1","T2","T3","X3","L1","L2","L3","L4","L5","L6","L7"},
    {"C","T1","T2","T3","L1","L2","L3","L4","L5","L6","L7"},


    {"C","R","T1","T2","T3","X2","X3","L1","L2","L6"},
    {"C","R","T1","T2","T3","X2","X3","L1","L2","L6"},
    {"C","R","T1","T2","T3","X2","L1","L2","L6"},
    {"C","R","T1","T2","T3","L1","L2","L6"},
    


    
    
    // {"C","R","T1","T2","T3","T4","T5","T6","X1","X2","X3","X4","X5","X6"},
    // {"C","R","F1","F2","F3","X1","X2","X3","L1"},
    
    
    

    // {"C","R","F1","F2","F3","X1","X2","X3","L1","L2","L3","L4","L5","L7","L8"},
    // {"C","R","T1","F2","F3","X1","X2","X3","L1","L2","L3","L4","L5","L6","L7","L8"},
    // {"C","R","T1","T2","T3","F1","F2","F3","X1","X2","X3","L1","L2","L3","L4","L5","L6","L7","L8"},
    // {"C","R","T1","T2","T3","F1","F2","F3","X1","X2","X3","L1","L2","L3","L4","L5","L6","L7","L8"},
    // {"C","R","T1","T2","T3","F1","F2","F3","X1","X2","X3","L1","L2","L3","L4","L5","L6","L7","L8"},
    // {"C","R","T1","T2","T3","F1","F2","F3","X1","X2","X3","L1","L2","L3","L4","L5","L6","L7","L8"},
     
    // {"C","R","F1","T2","F3","F4","F5","X1","X2","X3","X4","X5","X6","L1","L2","L3"},
    // {"C","R","T1","T2","T3","F1","F2","F3","X1","X2","X3","L2","L3","L4","L5","L6","L7","L8"},
    // {"C","R","F1","T2","T3","X1","X2","X3","L2","L3","L4","L5","L6","L7","L8"},
    // {"C","R","T1","F1","X1","X2","X3","L2","L3","L4","L5","L6","L7","L8"},
    // {"C","R","T1","T2","T3","F1","F2","F3","X1","X2","X3","L1","L3","L4","L5","L6","L7","L8"},
    // {"C","R","T1","T2","T3","F1","F2","F3","X1","X2","X3","L1","L2","L4","L5","L6","L7","L8"},
    // {"C","R","T1","T2","T3","F1","F2","F3","X1","X2","X3","L1","L2","L3","L5","L6","L7","L8"},
    // {"C","R","T1","T2","T3","F1","F2","F3","X1","X2","X3","L1","L2","L3","L4","L6","L7","L8"},
    // {"C","R","T1","T2","T3","F1","F2","F3","X1","X2","X3","L1","L2","L3","L4","L5","L7","L8"},
    // {"C","R","T1","T2","T3","F1","F2","F3","X1","X2","X3","L1","L2","L3","L4","L5","L6","L8"},
    // {"C","R","T1","T2","T3","F1","F2","F3","X1","X2","X3","L1","L2","L3","L4","L5","L6","L7"},
    


        // {"C", "T1", "F3", "F4", "F5", "F6", "L1", "L2", "L3", "L4","X1", "X2","X3", "X4", "X5", "X6"},
        // {"C", "T1", "T3", "F3", "T4", "F4", "T5", "F5", "T6", "F6", "L1", "L2", "L3", "L4","X1", "X2", "X3", "X4", "X5", "X6"},
        // {"C", "T1", "F3", "F5", "F6", "L1", "L2", "L3", "L4","X1", "X2", "X3", "X4", "X5", "X6"},
        // {"C", "T1", "F3", "F4", "L1", "L2", "L3", "L4","X1", "X2", "X3", "X4", "X5", "X6"},
        // {"C", "T1", "F3", "F4", "F5", "F6",  "L4","X1", "X2", "X3", "X4", "X5", "X6"},
        // {"C", "T1", "F3", "F4", "F5", "F6", "L1", "L2", "L3", "L4","X1", "X2", "X5", "X6"},
        // {"C", "T1", "F3", "F4", "F5", "L3", "L4","X1", "X2", "X3", "X4", "X5", "X6"},
    };   

    for (const auto& keep : keepValid) {
        uint64_t submask = mask_from_names(T, keep);

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
    
    // cout << "\nInvalid assignmets:\n";
    // vector<vector<string>> keepInvalid = {
    //     {"C", "T2", "F2", "F3", "L1", "L2", "L3", "L4", "L5", "L6", "L7", "L8", "X1", "X2", "X3"}, //Remove 1
    //     {"C", "T2", "F2", "T3", "L1", "L2", "L3", "L4", "L5", "L6", "L7", "L8", "X1", "X2", "X3"}, //Remove 1
    //     {"C", "T3", "F3", "F2", "L1", "L2", "L3", "L4", "L5", "L6", "L7", "L8", "X1", "X2", "X3"}, //Remove 1
    //     {"C", "T3", "F3", "T2", "L1", "L2", "L3", "L4", "L5", "L6", "L7", "L8", "X1", "X2", "X3"}, //Remove 1

    //     {"C", "T1", "F1", "F3", "L1", "L2", "L3", "L4", "L5", "L6", "L7", "L8", "X1", "X2", "X3"}, //Remove 2
    //     {"C", "T1", "F1", "T3", "L1", "L2", "L3", "L4", "L5", "L6", "L7", "L8", "X1", "X2", "X3"}, //Remove 2  
    //     {"C", "T3", "F3", "F1", "L1", "L2", "L3", "L4", "L5", "L6", "L7", "L8", "X1", "X2", "X3"}, //Remove 2
    //     {"C", "T3", "F3", "T1", "L1", "L2", "L3", "L4", "L5", "L6", "L7", "L8", "X1", "X2", "X3"}, //Remove 2


    //     {"C", "T2", "F2", "T1", "L1", "L2", "L3", "L4", "L5", "L6", "L7", "L8", "X1", "X2", "X3"},//Remove 3
    //     {"C", "T2", "F2", "F1", "L1", "L2", "L3", "L4", "L5", "L6", "L7", "L8", "X1", "X2", "X3"},//Remove 3
    //     {"C", "T1", "F1", "T2", "L1", "L2", "L3", "L4", "L5", "L6", "L7", "L8", "X1", "X2", "X3"},//Remove 3
    //     {"C", "T2", "F1", "F2", "L1", "L2", "L3", "L4", "L5", "L6", "L7", "L8", "X1", "X2", "X3"} //Remove 3
    // };

    // for (const auto& keep : keepInvalid) {
    //     uint64_t submask = mask_from_names(T, keep);

    //     cout << "Subtournament candidates: ";
    //     solver.print_mask_names(submask, ",");

    //     int w_sub = solver.solve_winner_standard(submask);
    //     if (w_sub < 0) {
    //         cout << "No winner found in subtournament!\n";
    //         continue;
    //     }
    //     cout << "Subtournament winner: " << T.names[w_sub] << "\n";
    // }

    

    

    return 0;
        

}
#endif
