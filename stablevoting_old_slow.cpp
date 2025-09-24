// stablevoting.cpp
// C++ port of stablevoting.py core logic (no visualization / external lib).
// - SAT clause parsing -> graph builder with 13 groups of weighted edges
// - Tournament verification (exactly one directed edge per unordered pair)
// - Stable Voting solver with memoization, returning winner, removal order, decisive edge
//
// Build: g++ -O3 -std=c++20 stablevoting.cpp -o stablevoting
// Demo:  g++ -O3 -std=c++20 -DSTABLEVOTING_MAIN stablevoting.cpp -o stablevoting

#include <bits/stdc++.h>
using namespace std;

namespace sv {

// ---------- Clause parsing ----------
struct ClausePolarity {
    // var index -> true for 'pos' (x_i), false for 'neg' (~x_i)
    unordered_map<int,bool> pol;
    int max_var = 0;
};

static ClausePolarity parse_clause(const string& clause_str) {
    // Matches (~)?x(\d+)
    static const regex lit_re(R"((~)?x(\d+))", std::regex_constants::icase);
    smatch m;
    ClausePolarity cp;
    auto begin = sregex_iterator(clause_str.begin(), clause_str.end(), lit_re);
    auto end   = sregex_iterator();
    if (begin == end) throw runtime_error("No literals found; expected like '(x1, ~x2)'.");
    for (auto it = begin; it != end; ++it) {
        string tilde = (*it)[1].str();
        int idx = stoi((*it)[2].str());
        bool pos = tilde.empty(); // true if 'x', false if '~x'
        cp.max_var = max(cp.max_var, idx);
        auto f = cp.pol.find(idx);
        if (f != cp.pol.end()) {
            if (f->second != pos) {
                throw runtime_error("Contradictory literals (x_i and ~x_i) in a single clause.");
            }
        } else {
            cp.pol[idx] = pos;
        }
    }
    return cp;
}

static tuple<int,int,vector<ClausePolarity>>
parse_clauses(const string& clauses_text) {
    // Extract all (...) groups; tolerate separators outside parentheses.
    static const regex group_re(R"(\(([^)]*)\))");
    vector<string> clause_strs;
    auto begin = sregex_iterator(clauses_text.begin(), clauses_text.end(), group_re);
    auto end   = sregex_iterator();
    if (begin == end) {
        // Treat entire string as a single clause if no parentheses groups found
        clause_strs.push_back(clauses_text);
    } else {
        for (auto it = begin; it != end; ++it) {
            clause_strs.push_back("(" + (*it)[1].str() + ")");
        }
    }

    if (clause_strs.empty()) throw runtime_error("No clauses provided.");

    vector<ClausePolarity> cps;
    int n_max = 0;
    for (auto& s : clause_strs) {
        auto cp = parse_clause(s);
        n_max = max(n_max, cp.max_var);
        cps.push_back(std::move(cp));
    }
    return {n_max, (int)cps.size(), cps};
}

// Overload: already separated clauses
static tuple<int,int,vector<ClausePolarity>>
parse_clauses(const vector<string>& clauses_vec) {
    if (clauses_vec.empty()) throw runtime_error("No clauses provided.");
    vector<ClausePolarity> cps;
    int n_max = 0;
    for (auto& s : clauses_vec) {
        auto cp = parse_clause(s);
        n_max = max(n_max, cp.max_var);
        cps.push_back(std::move(cp));
    }
    return {n_max, (int)cps.size(), cps};
}

// ---------- Fibonacci weights ----------
static vector<int> fibonacci_series(int n, int seed1, int seed2) {
    vector<int> a;
    if (n <= 0) return a;
    if (n == 1) { a.push_back(seed1); return a; }
    a.reserve(n);
    a.push_back(seed1);
    a.push_back(seed2);
    while ((int)a.size() < n) {
        long long nxt = (long long)a.back() + a[a.size()-2];
        // Bound to int range safely (weights are only relative/significant by order)
        if (nxt > INT_MAX) nxt = INT_MAX;
        a.push_back((int)nxt);
    }
    return a;
}

// ---------- Graph (tournament) ----------
struct Graph {
    // Node naming is string-based (C, L1.., T1.., F1.., X1..)
    vector<string> names;
    unordered_map<string,int> id;
    // adjacency weights: INT_MIN == no edge; otherwise weight is present for u->v
    vector<vector<int>> w;

    int add_node(const string& s) {
        auto it = id.find(s);
        if (it != id.end()) return it->second;
        int idx = (int)names.size();
        names.push_back(s);
        id[s] = idx;
        // expand matrix
        for (auto& row : w) row.push_back(INT_MIN);
        w.emplace_back(names.size(), INT_MIN);
        return idx;
    }

    void add_edge(const string& u, const string& v, int weight) {
        int iu = add_node(u);
        int iv = add_node(v);
        if (iu == iv) return; // ignore self
        if (w[iu][iv] != INT_MIN) {
            throw runtime_error("Duplicate directed edge " + u + "->" + v);
        }
        if (w[iv][iu] != INT_MIN) {
            // Would create two edges between the same pair -> invalid tournament
            throw runtime_error("Both directions present for pair {" + u + "," + v + "}");
        }
        w[iu][iv] = weight;
    }

    void verify_tournament() const {
        int n = (int)names.size();
        for (int i = 0; i < n; ++i) {
            for (int j = i+1; j < n; ++j) {
                bool ij = (w[i][j] != INT_MIN);
                bool ji = (w[j][i] != INT_MIN);
                if (ij == ji) {
                    throw runtime_error("Not a tournament: pair {" + names[i] + "," + names[j] + "} has " +
                                        (ij ? "both directions" : "no edge"));
                }
            }
        }
    }
};

// ---------- Weight cursors (grouped) ----------
struct WeightCursor {
    // Matches Python semantics: we walk weights from the last group to the first,
    // and within each group generate descending (weight + curr_delta - 1), curr_delta-- each call.
    vector<int> weights;   // length == num_groups
    int curr_group = -1;   // index into weights
    int curr_delta = 0;

    explicit WeightCursor(const vector<int>& group_weights)
        : weights(group_weights)
    {
        curr_group = (int)weights.size() - 1;
        curr_delta = 0;
    }

    void next_group() {
        if (curr_group < 0) throw runtime_error("next_group called too many times");
        curr_group--;
        curr_delta = 0;
    }

    int next_weight() {
        if (curr_group < 0) throw runtime_error("next_weight called after groups finished");
        // Python: curr_delta -= 1; return weights[curr_group] + curr_delta - 1
        curr_delta -= 1;
        long long val = (long long)weights[curr_group] + curr_delta - 1;
        if (val < INT_MIN) val = INT_MIN;
        if (val > INT_MAX) val = INT_MAX;
        return (int)val;
    }

    void small_jump_next_weight() {
        for (int i = 0; i < 10; ++i) (void)next_weight();
    }
};

// ---------- Build graph from spec ----------
struct BuildResult {
    Graph G;
    int n_vars = 0;
    vector<ClausePolarity> clauses;
};

static BuildResult build_graph_from_spec(
    const string& clause_str_or_groups,
    int starting_weight = 1000,
    const vector<int>* weight_perm = nullptr,
    int NUM_GROUPS = 11 // as in Python (but we actually use 13 groups; 9-11 share weights)
) {
    // Parse clauses
    int n, m;
    vector<ClausePolarity> clause_pols;
    tie(n, m, clause_pols) = parse_clauses(clause_str_or_groups);

    BuildResult out;
    out.n_vars = n;
    out.clauses = clause_pols;

    Graph& G = out.G;

    // Nodes
    const string C = "C";
    vector<string> L(m), T(n), F(n), X(n);
    for (int k = 0; k < m; ++k) L[k] = "L" + to_string(k+1);
    for (int i = 0; i < n; ++i) {
        T[i] = "T" + to_string(i+1);
        F[i] = "F" + to_string(i+1);
        X[i] = "X" + to_string(i+1);
    }
    // ensure nodes exist
    G.add_node(C);
    for (auto& s : L) G.add_node(s);
    for (auto& s : T) G.add_node(s);
    for (auto& s : F) G.add_node(s);
    for (auto& s : X) G.add_node(s);

    // Group weights (Fibonacci by default)
    const int num_groups = 11; // Python uses NUM_GROUPS=11 for group-weight buckets
    vector<int> base_w;
    if (weight_perm == nullptr) {
        base_w = fibonacci_series(num_groups, starting_weight, starting_weight * 2);
    } else {
        if ((int)weight_perm->size() != num_groups)
            throw runtime_error("weight_perm must have length " + to_string(num_groups));
        base_w = *weight_perm;
    }
    WeightCursor wc(base_w);

    // 1) C -> Fi, Ti
    for (int i = 0; i < n; ++i) {
        G.add_edge(C, F[i], wc.next_weight());
        G.add_edge(C, T[i], wc.next_weight());
    }
    wc.next_group();

    // 2) Fi, Ti -> Xi
    for (int i = 0; i < n; ++i) {
        G.add_edge(F[i], X[i], wc.next_weight());
        G.add_edge(T[i], X[i], wc.next_weight());
    }
    wc.next_group();

    // 3) {Ti, Fj in clause k} -> Lk
    for (int k = 0; k < m; ++k) {
        for (auto& [var, pos] : clause_pols[k].pol) {
            int i = var - 1;
            if (pos) G.add_edge(T[i], L[k], wc.next_weight());
            else     G.add_edge(F[i], L[k], wc.next_weight());
        }
    }
    wc.next_group();

    // 4) Lk -> {Fi, Tj not in clause k}, opposite for those in it
    for (int k = 0; k < m; ++k) {
        const auto& pol = clause_pols[k].pol;
        for (int i = 0; i < n; ++i) {
            int var = i + 1;
            auto it = pol.find(var);
            if (it != pol.end()) {
                bool pos = it->second;
                if (pos) G.add_edge(L[k], F[i], wc.next_weight()); // opposite of T_i
                else     G.add_edge(L[k], T[i], wc.next_weight()); // opposite of F_i
            } else {
                G.add_edge(L[k], F[i], wc.next_weight());
                G.add_edge(L[k], T[i], wc.next_weight());
            }
        }
    }
    wc.next_group();

    // 5) Xj -> {Fi, Ti for all i != j}
    for (int i = 0; i < n; ++i) {
        for (int j = 0; j < n; ++j) if (j != i) {
            G.add_edge(X[i], F[j], wc.next_weight());
            G.add_edge(X[i], T[j], wc.next_weight());
        }
    }
    wc.next_group();

    // 6) Xi -> Lk for all i,k
    for (int i = 0; i < n; ++i) for (int k = 0; k < m; ++k) {
        G.add_edge(X[i], L[k], wc.next_weight());
    }
    wc.next_group();

    // 7) Lk -> C
    for (int k = 0; k < m; ++k) G.add_edge(L[k], C, wc.next_weight());
    wc.next_group();

    // 8) Xi -> C
    for (int i = 0; i < n; ++i) G.add_edge(X[i], C, wc.next_weight());
    wc.next_group();

    // 9–11) treated as the SAME group in weight ordering (with small jumps)
    // 9: Fi -> Ti; Fi -> Tj (j>i); Ti -> Fj (j>i)
    for (int i = 0; i < n; ++i) {
        G.add_edge(F[i], T[i], wc.next_weight());
        for (int j = i+1; j < n; ++j) {
            G.add_edge(F[i], T[j], wc.next_weight());
        }
        for (int j = i+1; j < n; ++j) {
            G.add_edge(T[i], F[j], wc.next_weight());
        }
    }
    wc.small_jump_next_weight();

    // 10: Fi -> Fj (i<j)
    for (int i = 0; i < n; ++i) for (int j = i+1; j < n; ++j) {
        G.add_edge(F[i], F[j], wc.next_weight());
    }
    wc.small_jump_next_weight();

    // 11: Ti -> Tj (i<j)
    for (int i = 0; i < n; ++i) for (int j = i+1; j < n; ++j) {
        G.add_edge(T[i], T[j], wc.next_weight());
    }
    wc.next_group(); // end 9–11

    // 12: Xi -> Xj (i<j)
    for (int i = 0; i < n; ++i) for (int j = i+1; j < n; ++j) {
        G.add_edge(X[i], X[j], wc.next_weight());
    }
    wc.next_group();

    // 13: Li -> Lj (i<j)
    for (int i = 0; i < m; ++i) for (int j = i+1; j < m; ++j) {
        G.add_edge(L[i], L[j], wc.next_weight());
    }
    wc.next_group();

    // Verify tournament property
    G.verify_tournament();

    return out;
}

// ---------- Stable Voting ----------
struct SVResult {
    // winner name (empty if none), elimination order, decisive edge (A,B,margin)
    string winner;
    vector<string> removal_order;
    tuple<string,string,int> decisive_edge = {"","",0};
};

struct StableVoting {
    const Graph& G;
    int N;
    const vector<vector<int>>& W; // alias to G.w

    // memo key -> (winner index or -1, removal order (indices), decisive edge (i,j,m))
    struct MemoVal {
        int winner_idx;
        vector<int> removal_order;
        tuple<int,int,int> decisive;
    };
    unordered_map<string, MemoVal> memo;

    explicit StableVoting(const Graph& g)
        : G(g), N((int)g.names.size()), W(g.w)
    {}

    inline int margin(int u, int v) const {
        int uv = W[u][v], vu = W[v][u];
        if (uv == INT_MIN && vu == INT_MIN) throw runtime_error("Not a tournament (missing both).");
        if (uv != INT_MIN && vu != INT_MIN) throw runtime_error("Not a tournament (both directions).");
        return (uv != INT_MIN) ? uv : -vu;
    }

    bool exists_chain(int start, int goal, int threshold,
                      const vector<char>& in_set) const {
        if (start == goal) return true;
        vector<char> vis(N, 0);
        deque<int> dq;
        dq.push_back(start);
        vis[start] = 1;
        while (!dq.empty()) {
            int u = dq.front(); dq.pop_front();
            // Explore only nodes currently in candidate set
            for (int v = 0; v < N; ++v) {
                if (!in_set[v] || vis[v] || v == u) continue;
                int m = margin(u, v);
                if (m >= threshold) {
                    if (v == goal) return true;
                    vis[v] = 1;
                    dq.push_back(v);
                }
            }
        }
        return false;
    }

    inline bool b_defeats_a(int B, int A, const vector<char>& in_set) const {
        int t = -margin(A, B);
        return !exists_chain(A, B, t, in_set);
    }

    static string key_from_set(const vector<char>& in_set) {
        // Stable, compact key: list sorted indices joined by ','
        // (vector<char> is already indexed; collect ones with value 1)
        string k;
        k.reserve(in_set.size()*2);
        for (int i = 0; i < (int)in_set.size(); ++i) if (in_set[i]) {
            k.append(to_string(i));
            k.push_back(',');
        }
        return k;
    }

    MemoVal solve_rec(vector<char>& in_set, int live_count) {
        string key = key_from_set(in_set);
        auto it = memo.find(key);
        if (it != memo.end()) return it->second;

        if (live_count == 1) {
            int single = -1;
            for (int i = 0; i < N; ++i) if (in_set[i]) { single = i; break; }
            MemoVal mv;
            mv.winner_idx = single;
            mv.removal_order.clear();
            mv.decisive = {-1,-1,0};
            memo.emplace(key, mv);
            return mv;
        }

        // Build "valid" matches list (A->B) with margin and sort by margin desc
        struct Edge { int A,B,m; };
        vector<Edge> matches;
        matches.reserve(live_count*(live_count-1));
        for (int A = 0; A < N; ++A) if (in_set[A]) {
            for (int B = 0; B < N; ++B) if (in_set[B] && A != B) {
                int m = margin(A, B);
                if (m >= 0 || (m < 0 && !b_defeats_a(B, A, in_set))) {
                    matches.push_back({A,B,m});
                }
            }
        }
        sort(matches.begin(), matches.end(),
             [](const Edge& x, const Edge& y){ return x.m > y.m; });

        for (const auto& e : matches) {
            // Try removing B
            in_set[e.B] = 0;
            MemoVal sub = solve_rec(in_set, live_count - 1);
            in_set[e.B] = 1;
            if (sub.winner_idx == e.A) {
                MemoVal mv;
                mv.winner_idx = e.A;
                mv.removal_order.reserve(1 + sub.removal_order.size());
                mv.removal_order.push_back(e.B);
                mv.removal_order.insert(mv.removal_order.end(),
                                        sub.removal_order.begin(),
                                        sub.removal_order.end());
                mv.decisive = {e.A, e.B, e.m};
                memo.emplace(key, mv);
                return mv;
            }
        }

        MemoVal mv;
        mv.winner_idx = -1;
        mv.removal_order.clear();
        mv.decisive = {-1,-1,0};
        memo.emplace(key, mv);
        return mv;
    }

    SVResult solve_all() {
        vector<char> in_set(N, 1);
        MemoVal mv = solve_rec(in_set, N);
        SVResult out;
        if (mv.winner_idx >= 0) out.winner = G.names[mv.winner_idx];
        out.removal_order.reserve(mv.removal_order.size());
        for (int idx : mv.removal_order) out.removal_order.push_back(G.names[idx]);
        auto [ai, bi, m] = mv.decisive;
        if (ai >= 0 && bi >= 0) out.decisive_edge = {G.names[ai], G.names[bi], m};
        return out;
    }
};

// Top-level API analogous to stable_voting_winner_order_top
static SVResult stable_voting_winner_order_top(const Graph& G) {
    StableVoting sv(G);
    return sv.solve_all();
}

} // namespace sv

// // ---------- Optional demo main ----------
// #ifdef STABLEVOTING_MAIN
// int main() {
//     using namespace sv;
//     try {
//         // Example (same style as the Python driver’s small tests):
//         // Combine a few 2-SAT-style clauses:
//         string clause_text = "(x1, x2),(x1,~x2),(~x1,x2),(~x1,~x2)";
//
//         // Build with default Fibonacci group weights
//         auto br = build_graph_from_spec(clause_text, /*starting_weight=*/100);
//
//         // Solve Stable Voting on the built tournament
//         SVResult res = stable_voting_winner_order_top(br.G);
//
//         cout << "Winner: " << (res.winner.empty()? string("None") : res.winner) << "\n";
//         cout << "Removal order (" << res.removal_order.size() << "): ";
//         for (size_t i = 0; i < res.removal_order.size(); ++i) {
//             if (i) cout << ", ";
//             cout << res.removal_order[i];
//         }
//         cout << "\n";
//         auto [A,B,m] = res.decisive_edge;
//         if (!A.empty() && !B.empty()) {
//             cout << "Decisive edge: (" << A << " -> " << B << ")  margin=" << m << "\n";
//         } else {
//             cout << "Decisive edge: (none)\n";
//         }
//     } catch (const exception& e) {
//         cerr << "Error: " << e.what() << "\n";
//         return 1;
//     }
//     return 0;
// }
// #endif
#define STABLEVOTING_MAIN True

#ifdef STABLEVOTING_MAIN
int main() {
    using namespace sv;
    try {
        // -------- Config --------
        const int STARTING_WEIGHT = 100;
        const int NUM_GROUPS = 11; // matches builder's group buckets (9–11 share a bucket internally)
        const size_t PRINT_EVERY = 5000;
        const double TIME_EVERY_SEC = 60.0;

        auto join_clauses = [](const vector<string>& v) {
            string s;
            for (size_t i = 0; i < v.size(); ++i) {
                if (i) s.push_back(',');
                s += v[i];
            }
            return s;
        };

        // -------- Clause suites --------
        // Base four 2-SAT-style clauses
        vector<string> base_clauses = {
            "(x1, x2)", "(x1, ~x2)", "(~x1, x2)", "(~x1, ~x2)"
        };
        string clause_all = join_clauses(base_clauses);

        // All size-2 and size-3 combinations (SAT: C must win)
        vector<vector<string>> clause_sets_2_3;
        for (size_t i = 0; i < base_clauses.size(); ++i)
            for (size_t j = i + 1; j < base_clauses.size(); ++j)
                clause_sets_2_3.push_back({base_clauses[i], base_clauses[j]});
        for (size_t i = 0; i < base_clauses.size(); ++i)
            for (size_t j = i + 1; j < base_clauses.size(); ++j)
                for (size_t k = j + 1; k < base_clauses.size(); ++k)
                    clause_sets_2_3.push_back({base_clauses[i], base_clauses[j], base_clauses[k]});

        // Some UNSAT suites (C must NOT win)
        vector<vector<string>> unsat_clauses = {
            {"(x1, x2)", "(x1, ~x2)", "(~x1, x2)", "(~x1, ~x2)"},
            {"(x1, ~x2)", "(~x1, x2)", "(x2, ~x3)", "(~x2, x3)", "(x1, x3)", "(~x1, ~x3)"},
            {"(x1, x2)", "(x1, ~x2)", "(~x1, x3)", "(~x1, ~x3)"},
            {"(~x1, x2)", "(~x2, x3)", "(~x3, ~x1)", "(x1, ~x2)", "(x2, ~x3)", "(x3, x1)"}
        };

        // -------- Baseline run (unpermuted weights) on clause_all --------
        {
            auto br0 = build_graph_from_spec(clause_all, STARTING_WEIGHT);
            auto t0 = std::chrono::steady_clock::now();
            auto res0 = stable_voting_winner_order_top(br0.G);
            auto t1 = std::chrono::steady_clock::now();

            // Show unique weights used (sanity)
            std::set<int> uniq;
            for (int u = 0; u < (int)br0.G.names.size(); ++u)
                for (int v = 0; v < (int)br0.G.names.size(); ++v)
                    if (br0.G.w[u][v] != INT_MIN) uniq.insert(br0.G.w[u][v]);
            std::cout << "weights used:\n";
            int c = 0;
            for (int w : uniq) std::cout << w << ((++c % 10) ? ' ' : '\n');
            if (c % 10) std::cout << "\n";

            std::cout << "Baseline clause set: " << clause_all << "\n";
            std::cout << "Baseline winner: " << (res0.winner.empty() ? std::string("None") : res0.winner) << "\n";
            std::cout << "Baseline elimination order: [";
            for (size_t i = 0; i < res0.removal_order.size(); ++i) {
                if (i) std::cout << ", ";
                std::cout << res0.removal_order[i];
            }
            std::cout << "]\n";
            auto [BA, BB, BM] = res0.decisive_edge;
            std::cout << "Baseline decisive edge: (" << BA << " -> " << BB << ")  margin=" << BM << "\n";
            std::cout << "Baseline duration (sec): "
                      << std::chrono::duration<double>(t1 - t0).count() << "\n\n";
        }

        // -------- Exhaustive permutations of group weights --------
        vector<int> weights = fibonacci_series(NUM_GROUPS, STARTING_WEIGHT, STARTING_WEIGHT * 2);
        std::sort(weights.begin(), weights.end()); // ensure lexicographic start for next_permutation

        // Precompute baseline for clause_all to detect "diffs"
        auto baseline = stable_voting_winner_order_top(build_graph_from_spec(clause_all, STARTING_WEIGHT).G);

        const size_t total_clause_runs = clause_sets_2_3.size() + unsat_clauses.size() + 1; // +1 for clause_all diff check

        // Stats
        size_t num_done = 0;
        size_t num_all_failures = 0;
        size_t num_perms_failed = 0;
        double total_time = 0.0;
        double total_our_time = 0.0;
        double total_graph_build_time = 0.0;
        double total_post_overhead_time = 0.0;
        double total_their_time = 0.0; // placeholder to mirror your printouts

        struct DiffRec {
            vector<int> perm;
            string winner;
            vector<string> order;
            tuple<string,string,int> edge;
        };
        vector<DiffRec> all_diffs;

        auto last_report = std::chrono::steady_clock::now();

        // Test a single permutation
        auto test_perm = [&](const vector<int>& perm) {
            auto perm_start = std::chrono::steady_clock::now();

            bool correct = true;
            int failures = 0;

            // 0) Diff check on the full 4-clause set vs baseline
            {
                auto gb_t0 = std::chrono::steady_clock::now();
                auto br = build_graph_from_spec(clause_all, STARTING_WEIGHT, &perm);
                auto gb_t1 = std::chrono::steady_clock::now();
                total_graph_build_time += std::chrono::duration<double>(gb_t1 - gb_t0).count();

                auto sv_t0 = std::chrono::steady_clock::now();
                auto r = stable_voting_winner_order_top(br.G);
                auto sv_t1 = std::chrono::steady_clock::now();
                total_our_time += std::chrono::duration<double>(sv_t1 - sv_t0).count();

                bool same =
                    (r.winner == baseline.winner) &&
                    (r.removal_order == baseline.removal_order) &&
                    (r.decisive_edge == baseline.decisive_edge);
                if (!same) {
                    all_diffs.push_back({perm, r.winner, r.removal_order, r.decisive_edge});
                }
            }

            // 1) SAT cases: "C" must win
            for (const auto& setv : clause_sets_2_3) {
                auto gb_t0 = std::chrono::steady_clock::now();
                auto br = build_graph_from_spec(join_clauses(setv), STARTING_WEIGHT, &perm);
                auto gb_t1 = std::chrono::steady_clock::now();
                total_graph_build_time += std::chrono::duration<double>(gb_t1 - gb_t0).count();

                auto sv_t0 = std::chrono::steady_clock::now();
                auto r = stable_voting_winner_order_top(br.G);
                auto sv_t1 = std::chrono::steady_clock::now();
                total_our_time += std::chrono::duration<double>(sv_t1 - sv_t0).count();

                if (r.winner != "C") { correct = false; ++failures; }
            }

            // 2) UNSAT cases: "C" must NOT win
            for (const auto& setv : unsat_clauses) {
                auto gb_t0 = std::chrono::steady_clock::now();
                auto br = build_graph_from_spec(join_clauses(setv), STARTING_WEIGHT, &perm);
                auto gb_t1 = std::chrono::steady_clock::now();
                total_graph_build_time += std::chrono::duration<double>(gb_t1 - gb_t0).count();

                auto sv_t0 = std::chrono::steady_clock::now();
                auto r = stable_voting_winner_order_top(br.G);
                auto sv_t1 = std::chrono::steady_clock::now();
                total_our_time += std::chrono::duration<double>(sv_t1 - sv_t0).count();

                if (r.winner == "C") { correct = false; ++failures; }
            }

            auto perm_end = std::chrono::steady_clock::now();
            double perm_time = std::chrono::duration<double>(perm_end - perm_start).count();
            total_time += perm_time;

            // attribute leftover time as "post_overhead"
            total_post_overhead_time = total_time - total_our_time - total_graph_build_time - total_their_time;

            ++num_done;
            if (!correct) { num_all_failures += failures; ++num_perms_failed; }

            auto now = std::chrono::steady_clock::now();
            if ((num_done % PRINT_EVERY == 0) ||
                (std::chrono::duration<double>(now - last_report).count() >= TIME_EVERY_SEC)) {
                double avg = total_time / std::max<size_t>(1, num_done);
                double avg_per_perm = total_time / std::max<size_t>(1, num_done * total_clause_runs);
                double avg_our_time = total_our_time / std::max<size_t>(1, num_done);
                double avg_their_time = total_their_time / std::max<size_t>(1, num_done);
                double avg_graph_build_time = total_graph_build_time / std::max<size_t>(1, num_done);
                double avg_post_overhead_time = total_post_overhead_time / std::max<size_t>(1, num_done);

                std::cout << std::fixed << std::setprecision(4);
                std::cout << "[PROGRESS] perms_done=" << num_done
                          << "  diffs_so_far=" << all_diffs.size()
                          << "  fails_so_far=" << num_all_failures
                          << "  perms_failed_so_far=" << num_perms_failed
                          << "  (avg_time=" << avg
                          << "s, avg_per_perm=" << avg_per_perm
                          << "s, avg_our_time=" << avg_our_time
                          << "s, avg_their_time=" << avg_their_time
                          << "s, avg_graph_build_time=" << avg_graph_build_time
                          << "s, avg_post_overhead_time=" << avg_post_overhead_time
                          << "s)\n";
                last_report = now;
            }
        };

        // Exhaustive enumeration (11! permutations)
        size_t total_perms = 1;
        for (int i = 2; i <= NUM_GROUPS; ++i) total_perms *= (size_t)i;
        std::cout << "Exhaustively testing " << total_perms << " permutations...\n";

        do { test_perm(weights); } while (std::next_permutation(weights.begin(), weights.end()));

        std::cout << "\nDONE. perms_done=" << num_done
                  << "  diffs=" << all_diffs.size()
                  << "  fails=" << num_all_failures
                  << "  perms_failed=" << num_perms_failed << "\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
}
#endif
