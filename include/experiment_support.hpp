// shared parsing/build/weight helpers
/**
 * @file experiment_support.hpp
 * @brief Shared construction utilities for Stable Voting reduction experiments.
 *
 * This header contains the lightweight helpers used by multiple experiment
 * drivers:
 *
 *   - parsing clauses written as strings such as "(x1, ~x2)";
 *   - converting parsed clauses into a GraphTemplate;
 // *   - removing insignificant whitespace from formula specifications; and
 *   - generating the group-weight sequence used by the reduction.
 *
 * It contains no formula suites, solver policy, experiment loop, or main()
 * function. Functions defined here should be inline so that this header can be
 * included safely by multiple translation units.
 */

#include "fast_utils.hpp"
#include "graph_template.hpp"
#include "tqbf_tournament_builder.hpp"

inline constexpr int STARTING_WEIGHT = 100;
inline constexpr int NUM_GROUPS = GraphTemplate::kGroupCount; // effective weight buckets

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

// static bool has_flag(int argc, char** argv, string_view flag) {
//     for (int i = 1; i < argc; ++i) {
//         if (string_view(argv[i]) == flag) return true;
//     }
//     return false;
// }
static AI bool include_disabled_formulas(int argc, char** argv) {
    bool include_disabled = false;
    for (int i = 1; i < argc; ++i) {
        const string_view arg(argv[i]);
        if (arg == "--all-formulas") {
            include_disabled = true;
        } else if (arg == "--help") {
            cout << "Usage: " << argv[0] << " [--all-formulas]\n"
                 << "  --all-formulas  include all disabled cases\n";
            exit(0);
        } else {
            throw invalid_argument("Unknown argument: " + string(arg));
        }
    }
    return include_disabled;
}
