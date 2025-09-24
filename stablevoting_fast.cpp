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
#include <bits/stdc++.h>
using namespace std;

// ========================= Utilities =========================
static inline int popcount64(uint64_t x){ return __builtin_popcountll(x); }
static inline int ctz64(uint64_t x){ return __builtin_ctzll(x); }
static inline uint64_t lsb64(uint64_t x){ return x & -x; }

// ========================= Clause Parsing (fast) =========================
// Accepts tokens like: (x1, ~x2) or x3 or ~x10; commas/parentheses/whitespace ignored
// Output: vector of (var_index>=1, polarity=true for x, false for ~x)
static inline void parse_clause_fast(const string& s, vector<pair<int,bool>>& out, int& max_var){
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

// ========================= Tournament Template =========================
// We represent each ordered pair (u,v) with three parallel arrays:
//   dir[u,v]   = 1 if edge u->v exists, else 0 (then v->u exists)
//   group[u,v] = weight-bucket id in [0..10] for the *existing* direction
//   off[u,v]   = monotonically decreasing small integer (e.g., -1,-2,...) per group
// For fast margins under a permutation W[11]:
//   margin(u,v) = dir[u,v] ?  (W[group[u,v]] + off[u,v])
//                           : -(W[group[v,u]] + off[v,u])
struct GraphTemplate {
    int N = 0;         // total nodes
    int n = 0, m = 0;  // variables, clauses
    int idxC = 0;      // index of C
    // Optional names (only used when printing full details)
    vector<string> names; // size N

    // flattened N*N arrays
    vector<uint8_t> dir;    // 1 if u->v
    vector<uint8_t> group;  // valid only when dir[u,v]==1
    vector<int16_t> off;    // valid only when dir[u,v]==1

    inline int IDX(int u,int v) const { return u*N + v; }

    inline int margin(int u,int v, const int *W) const {
        int iuv = IDX(u,v);
        if (dir[iuv]) return W[group[iuv]] + (int)off[iuv];
        int ivu = IDX(v,u);
        return -(W[group[ivu]] + (int)off[ivu]);
    }
};

static inline void tmpl_init(GraphTemplate& T, int N){
    T.N = N;
    T.dir.assign((size_t)N*(size_t)N, 0);
    T.group.assign((size_t)N*(size_t)N, 0);
    T.off.assign((size_t)N*(size_t)N, 0);
}

static inline void tmpl_add_edge(GraphTemplate& T, int u,int v, int g, int16_t& cur_d){
    if (u==v) return; // ignore self
    int iuv=T.IDX(u,v), ivu=T.IDX(v,u);
    if (T.dir[iuv] || T.dir[ivu]) {
        // in a tournament exactly one direction should be set once
        // we keep the first and ignore accidental duplicates (safe for our template)
        return;
    }
    T.dir[iuv] = 1;
    T.group[iuv] = (uint8_t)g;
    T.off[iuv] = cur_d--;
}

// Build tournament template from a clause set (vector<literals>)
// 11 effective weight groups (g0..g10). Conceptual groups 9–11 share g8 with jumps.
static GraphTemplate build_template_from_clauses(const vector<vector<pair<int,bool>>>& clauses){
    GraphTemplate T;
    int m = (int)clauses.size();
    int n = 0; // infer max var
    for (auto& c : clauses) for (auto [v,_] : c) n = max(n, v);
    T.m = m; T.n = n;

    // index layout: [C][L1..Lm][T1..Tn][F1..Fn][X1..Xn]
    int idxC = 0;
    int baseL = 1;
    int baseT = baseL + m;
    int baseF = baseT + n;
    int baseX = baseF + n;
    int N = 1 + m + 3*n;
    tmpl_init(T, N);

    T.idxC = idxC;
    T.names.resize(N);
    T.names[idxC] = "C";
    for (int k=0;k<m;++k) T.names[baseL+k] = string("L") + to_string(k+1);
    for (int i=0;i<n;++i) T.names[baseT+i] = string("T") + to_string(i+1);
    for (int i=0;i<n;++i) T.names[baseF+i] = string("F") + to_string(i+1);
    for (int i=0;i<n;++i) T.names[baseX+i] = string("X") + to_string(i+1);

    // Per-group current delta (start at -1 and decreases)
    // g0..g10 (11 groups total); conceptual 9..11 share g8
    int16_t cur[11];
    for (int g=0; g<11; ++g) cur[g] = -1;

    auto L=[&](int k){ return baseL + k; };
    auto Tn=[&](int i){ return baseT + i; };
    auto Fn=[&](int i){ return baseF + i; };
    auto Xn=[&](int i){ return baseX + i; };

    // int CURRENT_GROUP_PRIORITY_IDX = 10;
    int CGP_IDX = 10;

    // ---- g0: C -> Fi, Ti ----
    for (int i=0;i<n;++i){
        tmpl_add_edge(T, idxC, Fn(i), CGP_IDX, cur[CGP_IDX]);
        tmpl_add_edge(T, idxC, Tn(i), CGP_IDX, cur[CGP_IDX]);
    }
    CGP_IDX--;

    // ---- g1: Fi, Ti -> Xi ----
    for (int i=0;i<n;++i){
        tmpl_add_edge(T, Fn(i), Xn(i), CGP_IDX, cur[CGP_IDX]);
        tmpl_add_edge(T, Tn(i), Xn(i), CGP_IDX, cur[CGP_IDX]);
    }
    CGP_IDX--;

    // ---- g2: {Ti,Fj in clause k} -> Lk ----
    for (int k=0;k<m;++k){
        for (auto [var,pos] : clauses[k]){
            int i = var-1;
            tmpl_add_edge(T, pos? Tn(i): Fn(i), L(k), CGP_IDX, cur[CGP_IDX]);
        }
    }
    CGP_IDX--;

    // ---- g3: Lk -> {Fi, Tj not in clause k}, opposite for those in it ----
    for (int k=0;k<m;++k){
        // mark presence
        static bool pres[128]; // n is tiny
        memset(pres, 0, sizeof(pres));
        for (auto [var,_] : clauses[k]) pres[var-1] = true;
        for (int i=0;i<n;++i){
            auto it = find_if(clauses[k].begin(), clauses[k].end(), [&](auto &p){return p.first==i+1;});
            if (it!=clauses[k].end()){
                bool pos = it->second;
                // opposite of present literal
                tmpl_add_edge(T, L(k), pos? Fn(i): Tn(i), CGP_IDX, cur[CGP_IDX]);
            }else{
                // not in clause -> both
                tmpl_add_edge(T, L(k), Fn(i), CGP_IDX, cur[CGP_IDX]);
                tmpl_add_edge(T, L(k), Tn(i), CGP_IDX, cur[CGP_IDX]);
            }
        }
    }
    CGP_IDX--;

    // ---- g4: Xj -> {Fi, Ti for all i != j} ----
    for (int j=0;j<n;++j){
        for (int i=0;i<n;++i) if (i!=j){
            tmpl_add_edge(T, Xn(j), Fn(i), CGP_IDX, cur[CGP_IDX]);
            tmpl_add_edge(T, Xn(j), Tn(i), CGP_IDX, cur[CGP_IDX]);
        }
    }
    CGP_IDX--;

    // ---- g5: Xi -> Lk for all i,k ----
    for (int i=0;i<n;++i) for (int k=0;k<m;++k){
        tmpl_add_edge(T, Xn(i), L(k), CGP_IDX, cur[CGP_IDX]);
    }
    CGP_IDX--;

    // ---- g6: Lk -> C ----
    for (int k=0;k<m;++k) tmpl_add_edge(T, L(k), idxC, CGP_IDX, cur[CGP_IDX]);
    CGP_IDX--;

    // ---- g7: Xi -> C ----
    for (int i=0;i<n;++i) tmpl_add_edge(T, Xn(i), idxC, CGP_IDX, cur[CGP_IDX]);
    CGP_IDX--;

    // ---- g8 (shared by conceptual 9..11) ----
    // 9: Fi -> Ti; Fi -> Tj (j>i); Ti -> Fj (j>i)
    for (int i=0;i<n;++i){
        tmpl_add_edge(T, Fn(i), Tn(i), CGP_IDX, cur[CGP_IDX]);
        for (int j=i+1;j<n;++j){
            tmpl_add_edge(T, Fn(i), Tn(j), CGP_IDX, cur[CGP_IDX]);
        }
        for (int j=i+1;j<n;++j){
            tmpl_add_edge(T, Tn(i), Fn(j), CGP_IDX, cur[CGP_IDX]);
        }
    }
    // small jump between 9 and 10
    cur[CGP_IDX] = (int16_t)(cur[CGP_IDX] - 10);

    // 10: Fi -> Fj (i<j)
    for (int i=0;i<n;++i) for (int j=i+1;j<n;++j){
        tmpl_add_edge(T, Fn(i), Fn(j), CGP_IDX, cur[CGP_IDX]);
    }
    // small jump between 10 and 11
    cur[CGP_IDX] = (int16_t)(cur[CGP_IDX] - 10);

    // 11: Ti -> Tj (i<j)
    for (int i=0;i<n;++i) for (int j=i+1;j<n;++j){
        tmpl_add_edge(T, Tn(i), Tn(j), CGP_IDX, cur[CGP_IDX]);
    }
    CGP_IDX--;

    // ---- g9: Xi -> Xj (i<j) ----
    for (int i=0;i<n;++i) for (int j=i+1;j<n;++j){
        tmpl_add_edge(T, Xn(i), Xn(j), CGP_IDX, cur[CGP_IDX]);
    }
    CGP_IDX--;

    // ---- g10: Li -> Lj (i<j) ----
    for (int i=0;i<m;++i) for (int j=i+1;j<m;++j){
        tmpl_add_edge(T, L(i), L(j), CGP_IDX, cur[CGP_IDX]);
    }

    return T;
}

// Convenience: from strings like "(x1, ~x2)", "(x1, x2)"
static GraphTemplate build_template_from_strings(const vector<string>& clause_strs){
    vector<vector<pair<int,bool>>> clauses;
    clauses.reserve(clause_strs.size());
    int maxv=0; vector<pair<int,bool>> tmp;
    for (auto &s: clause_strs){ parse_clause_fast(s, tmp, maxv); clauses.push_back(tmp); }
    return build_template_from_clauses(clauses);
}

// ========================= Stable Voting (fast) =========================
struct SVFast {
    const GraphTemplate& G;
    const int N;

    // Memo (winner only) using epoch-tag trick for O(1) clears
    vector<int> memo_winner;        // winner idx or -1 (none)
    vector<uint32_t> memo_epoch;    // epoch tag
    uint32_t EPOCH = 1;

    // Scratch
    vector<int> q;           // BFS queue (N)
    vector<uint8_t> vis;     // BFS visited (N)

    SVFast(const GraphTemplate& T): G(T), N(T.N){
        size_t S = 1u << N;  // N is small in these instances
        memo_winner.assign(S, -2);   // -2 = unknown
        memo_epoch.assign(S, 0);
        q.resize(N);
        vis.resize(N);
    }

    inline void reset_epoch(){
        if (++EPOCH == 0){
            // rare wrap: hard reset
            std::fill(memo_epoch.begin(), memo_epoch.end(), 0);
            EPOCH = 1;
        }
    }

    inline int margin(int u,int v, const int *W) const { return G.margin(u,v,W); }

    inline bool in_mask(uint64_t mask, int i) const { return (mask >> i) & 1ull; }

    bool exists_chain(int src, int dst, int threshold, uint64_t mask, const int *W){
        if (src==dst) return true;
        // Reset vis
        memset(vis.data(), 0, (size_t)N);
        int head=0, tail=0;
        vis[src]=1; q[tail++]=src;
        while (head<tail){
            int u=q[head++];
            // iterate all v in mask, v!=u
            uint64_t m = mask & ~(1ull<<u);
            while (m){
                uint64_t b = lsb64(m); m ^= b; int v = ctz64(b);
                if (vis[v]) continue;
                if (margin(u,v,W) >= threshold){
                    if (v==dst) return true;
                    vis[v]=1; q[tail++]=v;
                }
            }
        }
        return false;
    }

    inline bool b_defeats_a(int B, int A, uint64_t mask, const int *W){
        int t = -margin(A,B,W);
        return !exists_chain(A,B,t,mask,W);
    }

    struct Match { int A,B,m; };

    int solve_winner(uint64_t mask, const int *W){
        size_t key = (size_t)mask;
        if (memo_epoch[key]==EPOCH){ return memo_winner[key]; }

        int live = popcount64(mask);
        if (live==1){
            int w = ctz64(mask);
            memo_epoch[key]=EPOCH; memo_winner[key]=w; return w;
        }

        // Build matches (A,B) with rule; sort by margin desc
        // Upper bound edges ~ N*(N-1)
        static vector<Match> matches; matches.clear(); matches.reserve(N*(N-1));
        uint64_t ma = mask;
        while (ma){
            int A = ctz64(lsb64(ma)); ma ^= lsb64(ma);
            uint64_t mb = mask ^ (1ull<<A);
            while (mb){
                int B = ctz64(lsb64(mb)); mb ^= lsb64(mb);
                int m = margin(A,B,W);
                if (m>=0 || (m<0 && !b_defeats_a(B,A,mask,W))) matches.push_back({A,B,m});
            }
        }
        sort(matches.begin(), matches.end(), [](const Match& x, const Match& y){ return x.m>y.m; });

        for (const auto& e: matches){
            uint64_t nmask = mask & ~(1ull<<e.B);
            int w = solve_winner(nmask, W);
            if (w==e.A){ memo_epoch[key]=EPOCH; memo_winner[key]=w; return w; }
        }
        memo_epoch[key]=EPOCH; memo_winner[key]=-1; return -1;
    }

    // Reconstruct elimination order + decisive edge using memoized winners
    void reconstruct(uint64_t mask, int WN, const int *W,
                     vector<int>& elim, tuple<int,int,int>& dec){
        elim.clear(); dec = {-1,-1,0};
        if (popcount64(mask)==1) return;
        static vector<Match> matches; matches.clear(); matches.reserve(N*(N-1));
        uint64_t ma = mask;
        while (ma){ int A=ctz64(lsb64(ma)); ma^=lsb64(ma);
            uint64_t mb = mask ^ (1ull<<A);
            while (mb){ int B=ctz64(lsb64(mb)); mb^=lsb64(mb);
                int m=margin(A,B,W);
                if (m>=0 || (m<0 && !b_defeats_a(B,A,mask,W))) matches.push_back({A,B,m});
            }
        }
        sort(matches.begin(), matches.end(), [](const Match& x, const Match& y){ return x.m>y.m; });
        for (const auto& e: matches){
            uint64_t nmask = mask & ~(1ull<<e.B);
            if (solve_winner(nmask, W)==WN){
                if (get<0>(dec)==-1){ dec = {e.A,e.B,e.m}; }
                elim.push_back(e.B);
                reconstruct(nmask, WN, W, elim, dec);
                return;
            }
        }
    }
};

// ========================= Group Weights =========================
// this starts in increasing order first, and groups are assigned 0->11
static inline void fibonacci_series(int n, int seed1, int seed2, vector<int>& out){
    out.clear(); out.reserve(n);
    if (n<=0) return; if (n==1){ out.push_back(seed1); return; }
    long long a=seed1,b=seed2; out.push_back((int)a); out.push_back((int)b);
    for (int i=2;i<n;++i){ long long c=a+b; if (c>INT_MAX) c=INT_MAX; out.push_back((int)c); a=b; b=c; }
}

// ========================= Exhaustive Driver (fast) =========================
#define STABLEVOTING_MAIN true

#ifdef STABLEVOTING_MAIN
int main(){
    ios::sync_with_stdio(false); cin.tie(nullptr);

    const int STARTING_WEIGHT = 100;
    const int NUM_GROUPS = 11;        // effective weight buckets
    const size_t PRINT_EVERY = 5000;  // progress cadence
    const double TIME_EVERY_SEC = 60.0;
    const bool TRACK_DIFFS = false;   // set true to record permutations that change baseline triple

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

    SVFast solver_all(T_all);
    solver_all.reset_epoch();
    int W_all_base[NUM_GROUPS]; for (int i=0;i<NUM_GROUPS;++i) W_all_base[i]=Wbase[i];
    int base_winner = solver_all.solve_winner((T_all.N==64?~0ull:((1ull<<T_all.N)-1ull)), W_all_base);
    vector<int> base_elim; tuple<int,int,int> base_dec;
    solver_all.reconstruct((T_all.N==64?~0ull:((1ull<<T_all.N)-1ull)), base_winner, W_all_base, base_elim, base_dec);

    // Print baseline summary
    cout << "Baseline winner: " << (base_winner>=0? T_all.names[base_winner] : string("None")) << '\n';
    auto [bA,bB,bM] = base_dec; cout << "Baseline decisive edge: (" << (bA>=0?T_all.names[bA]:string("-"))
         << " -> " << (bB>=0?T_all.names[bB]:string("-")) << ")  m=" << bM << '\n';
    cout << "Baseline elim order: [";
    for (size_t i=0;i<base_elim.size();++i){ if(i) cout<<", "; cout<<T_all.names[base_elim[i]]; } cout<<"]\n\n";

    // Stats
    const size_t total_clause_runs = sat_sets.size() + unsat_sets.size();
    size_t perms_done=0, all_failures=0, perms_failed=0, diffs=0;
    double total_time=0.0, t_graph=0.0, t_solve=0.0;

    // Prepare solvers per template (reuse memory)
    vector<SVFast> sol_sat; sol_sat.reserve(T_sat.size()); for (auto &t: T_sat) sol_sat.emplace_back(t);
    vector<SVFast> sol_uns; sol_uns.reserve(T_unsat.size()); for (auto &t: T_unsat) sol_uns.emplace_back(t);

    // permutation weights buffer
    int Wperm[NUM_GROUPS];

    auto t_last = chrono::steady_clock::now();

    // Exhaustive permutations of 11 weights
    size_t total_perms=1; for(int i=2;i<=NUM_GROUPS;++i) total_perms*= (size_t)i;
    cout << "Exhaustively testing " << total_perms << " permutations...\n";

    do{
        auto t0 = chrono::steady_clock::now();
        for (int i=0;i<NUM_GROUPS;++i) Wperm[i]=W[i];
        bool success = true;


        // SAT: C must win
        for (size_t si=0; si<T_sat.size(); ++si){
            auto &T = T_sat[si]; auto &S = sol_sat[si];
            S.reset_epoch();
            uint64_t full = (T.N==64?~0ull:((1ull<<T.N)-1ull));
            int w = S.solve_winner(full, Wperm);
            if (w<0 || T.names[w] != "C") { ++all_failures; success = false; }
        }

        // UNSAT: C must NOT win
        for (size_t ui=0; ui<T_unsat.size(); ++ui){
            auto &T = T_unsat[ui]; auto &S = sol_uns[ui];
            S.reset_epoch();
            uint64_t full = (T.N==64?~0ull:((1ull<<T.N)-1ull));
            int w = S.solve_winner(full, Wperm);
            if (w>=0 && T.names[w] == "C") { ++all_failures; success = false; }
        }

        // Optional: diff against baseline (winner/order/edge) for T_all
        if (TRACK_DIFFS){
            solver_all.reset_epoch();
            uint64_t full = (T_all.N==64?~0ull:((1ull<<T_all.N)-1ull));
            int w = solver_all.solve_winner(full, Wperm);
            vector<int> elim; tuple<int,int,int> dec;
            solver_all.reconstruct(full, w, Wperm, elim, dec);
            if (!(w==base_winner && elim==base_elim && dec==base_dec)) ++diffs;
        }

        if (success) {
            cout << "FOUND ONE! perms_done=" << perms_done << " weights=[";
            for (int g = 0; g < NUM_GROUPS; ++g) {
                if (g) cout << ',';
                cout << Wperm[g];
            }
            cout << "]";

            // Summarize outcome on the full 4-clause set for this permutation
            solver_all.reset_epoch();
            uint64_t full_all = (T_all.N == 64 ? ~0ull : ((1ull << T_all.N) - 1ull));
            int w_all = solver_all.solve_winner(full_all, Wperm);

            vector<int> elim_all;
            tuple<int,int,int> dec_all;
            solver_all.reconstruct(full_all, w_all, Wperm, elim_all, dec_all);

            cout << "  all-clauses winner="
                 << (w_all >= 0 ? T_all.names[w_all] : string("None"));

            auto [AA, BB, MM] = dec_all;
            cout << "  decisive=("
                 << (AA >= 0 ? T_all.names[AA] : string("-")) << "->"
                 << (BB >= 0 ? T_all.names[BB] : string("-")) << ", m=" << MM << ")";

            cout << "  elim=[";
            for (size_t i = 0; i < elim_all.size(); ++i) {
                if (i) cout << ',';
                cout << T_all.names[elim_all[i]];
            }
            cout << "]\n";
        }


        auto t1 = chrono::steady_clock::now();
        total_time += chrono::duration<double>(t1-t0).count();
        ++perms_done;

        auto now = chrono::steady_clock::now();
        if ((perms_done % PRINT_EVERY == 0) || chrono::duration<double>(now - t_last).count() >= TIME_EVERY_SEC){
            double avg = total_time / max<size_t>(1, perms_done);
            double per_case = total_time / max<size_t>(1, perms_done*total_clause_runs);
            cout.setf(std::ios::fixed); cout<<setprecision(4);
            cout << "[PROGRESS] perms_done="<<perms_done
                 << " fails="<<all_failures
                 << " diffs="<< (TRACK_DIFFS?diffs:0)
                 << " avg_perm="<<avg<<"s"
                 << " avg_case="<<per_case<<"s\n";
            t_last = now;
        }

    } while (next_permutation(W.begin(), W.end()));

    cout << "\nDONE. perms_done="<<perms_done
         << " fails="<<all_failures
         << " diffs="<<(TRACK_DIFFS?diffs:0) << "\n";

    return 0;
}
#endif
