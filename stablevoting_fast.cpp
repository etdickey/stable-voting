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
#include <iostream>
#include <iomanip>
using namespace std;

#if defined(__GNUC__) || defined(__clang__)
  #define AI inline __attribute__((always_inline))
#else
  #define AI inline
#endif

// ========================= Utilities =========================
// lsb64(x) returns the mask of x’s least-significant 1-bit (x & −x)
// ctz64(x) returns the number of trailing zero bits (the index of that bit).
// ctz64(lsb64(x)) returns the index of the first active candidate with ~speed~
static AI int popcount64(uint64_t x){ return __builtin_popcountll(x); }
static AI int ctz64(uint64_t x){ return __builtin_ctzll(x); }
static AI uint64_t lsb64(uint64_t x){ return x & -x; }

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

    AI int IDX(int u,int v) const { return u*N + v; }

    AI int margin(int u,int v, const int *W) const {
        int iuv = IDX(u,v);
        if (dir[iuv]) return W[group[iuv]] + (int)off[iuv];
        int ivu = IDX(v,u);
        return -(W[group[ivu]] + (int)off[ivu]);
    }

    // Add this margin overload inside GraphTemplate if you like:
    AI int margin(int u, int v, const vector<int>& W) const {
        return margin(u, v, W.data());//.data is constant time
    }
};

static AI void tmpl_init(GraphTemplate& T, int N){
    T.N = N;
    T.dir.assign((size_t)N*(size_t)N, 0);
    T.group.assign((size_t)N*(size_t)N, 0);
    T.off.assign((size_t)N*(size_t)N, 0);
}

static AI void tmpl_add_edge(GraphTemplate& T, int u,int v, int g, int16_t& cur_d){
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

    struct Match { int A,B,m; };
    vector<Match> edges_sorted;

    // Per-permutation realized margins and sorted adjacency
    bool prepared = false;
    vector<int> M;                  // size N*N, M[u*N+v] = margin(u->v)
    vector<vector<int>> nbrs;  // nbrs[u] = v's sorted by M[u,v] desc
    AI int MIDX(int u,int v) const { return u*N + v; }

    // Scratch
    vector<int> q;           // BFS queue (N)
    vector<uint8_t> vis;     // BFS visited (N)

    SVFast(const GraphTemplate& T): G(T), N(T.N) {
        size_t S = size_t{1} << N;  // N is small in these instances
        memo_winner.assign(S, -2);   // -2 = unknown
        memo_epoch.assign(S, 0);
        q.resize(N);
        vis.resize(N);
    }

    void prepare_for_weights(const int* W) {
        if ((int)M.size() != N*N) M.assign(N*N, 0);
        if ((int)nbrs.size() != N) nbrs.assign(N, {});
        // Fill realized margins for the tournament (both directions).
        edges_sorted.clear();
        edges_sorted.reserve((size_t)N * (N - 1));
        for (int u=0; u<N; ++u) {
            for (int v=0; v<N; ++v) if (u!=v) {
                int iuv = G.IDX(u,v);
                if (G.dir[iuv]) {
                    int m = W[(int)G.group[iuv]] + (int)G.off[iuv];
                    M[MIDX(u,v)] = m;
                    M[MIDX(v,u)] = -m;
                    edges_sorted.push_back({u, v, m}); // only the existing arc a->b
                    edges_sorted.push_back({v, u, -m});
                }
            }
        }
        // global sort by margin descending (stable order for this permutation)
        sort(edges_sorted.begin(), edges_sorted.end(),
            [](const Match& x, const Match& y){ return x.m > y.m; });

        // Build adjacency lists sorted by realized margin
        for (int u=0; u<N; ++u) {
            auto& L = nbrs[u];
            L.clear(); L.reserve(N-1);
            for (int v=0; v<N; ++v) if (u!=v) L.push_back(v);
            sort(L.begin(), L.end(),
                      [&](int a,int b){ return M[MIDX(u,a)] > M[MIDX(u,b)]; });
        }

        prepared = true;
    }

    AI void reset_epoch(const int* W){
        if (++EPOCH == 0){
            // rare wrap: hard reset
            fill(memo_epoch.begin(), memo_epoch.end(), 0);
            EPOCH = 1;
        }
        prepare_for_weights(W);
    }

    AI int margin(int u,int v, const int *W) const { return G.margin(u,v,W); }
    AI int margin_fast(int u,int v) const { return M[MIDX(u,v)]; }// assume prepared==true
    AI bool in_mask(uint64_t mask, int i) const { return (mask >> i) & 1ull; }

    // In SVFast:
    void print_mask_names(uint64_t mask, const char* sep = ",") const {
        bool first = true;
        while (mask) {
            uint64_t b = mask & -mask;        // lsb64(mask)
            int i = ctz64(b);                 // index of that bit
            if (!first) cout << sep;
            cout << G.names[i];
            first = false;
            mask ^= b;                        // pop that bit
        }
        cout << '\n';
    }

    AI bool exists_chain(int src, int dst, int threshold, uint64_t mask){
        if (src==dst) return true;
        // Reset vis
        fill(vis.begin(), vis.end(), uint8_t{0});
        int head=0, tail=0;
        vis[src]=1; q[tail++]=src;
        while (head<tail){
            int u=q[head++];
            const auto& L = nbrs[u];             // already sorted by M[u,v] desc
            for (int v : L) {
                int m = margin_fast(u, v);
                if (m < threshold) break;        // early cutoff, neighbors now too weak
                if (!((mask >> v) & 1ull)) continue; // v not in subtournament
                if (vis[v]) continue;
                if (v == dst) return true;
                vis[v] = 1; q[tail++] = v;
            }
            // // iterate all v in mask, v!=u
            // uint64_t m = mask & ~(1ull<<u);
            // while (m){
            //     uint64_t b = lsb64(m); m ^= b; int v = ctz64(b);
            //     if (vis[v]) continue;
            //     if (margin_fast(u,v,W) >= threshold){
            //         if (v==dst) return true;
            //         vis[v]=1; q[tail++]=v;
            //     }
            // }
        }
        return false;
    }

    AI bool b_defeats_a(int B, int A, uint64_t mask){
        int t = margin_fast(B,A);
        return !exists_chain(A,B,t,mask);
    }

    int solve_winner(uint64_t mask) {
        // is the current graph permutation the one where memo_winner was set?
        size_t key = (size_t)mask;
        if (memo_epoch[key]==EPOCH) return memo_winner[key];

        if (popcount64(mask)==1){// single survivor?
            int w = ctz64(mask);
            memo_epoch[key]=EPOCH; memo_winner[key]=w; return w;
        }

        // vector<string> ansS = {"F1", "X2", "X1", "T1", "L4", "L3", "L2", "F2", "L1", "T2", "C"};
        //                     // ['F1', 'X2', 'X1', 'T1', 'L4', 'L3', 'L2', 'F2', 'L1', 'T2']
        // vector<int> ans(N);
        // for(int i=0; i<N; i++){
        //     ans[i] = find(G.names.begin(), G.names.end(), ansS[i]) - G.names.begin();
        //     // if(find(G.names.begin(), G.names.end(), ansS[i]) == G.names.end()) cout << "Couldn't find this guy: \'" << ansS[i] << "\'" << endl;
        //     // else cout << "Found \'" << ansS[i] << "\' at " << ans[i] << endl;
        // }
        // int tmpidx = popcount64(mask);
        // cout << "Curr cand left: " << tmpidx << " [";
        // for (int i = 0; i < N; ++i) if (mask & (1ull << i)) cout << G.names[i] << ", ";
        // cout << "], curr removed: [";
        // for (int i = 0; i < N; ++i) if (!(mask & (1ull << i))) cout << G.names[i] << ", ";
        // cout << "], winner should be:" << ans[N - tmpidx] << ": " << ansS[N - tmpidx] << '\n';

        // Scan edges in global descending order; try the first valid one whose endpoints are active.
        for (const auto& e : edges_sorted) {
            int A = e.A, B = e.B, m = e.m;
            if (((mask >> A) & 1ull) == 0 || ((mask >> B) & 1ull) == 0) continue;  // edge not in subtournament

            if (m > 0 || (m <= 0 && !b_defeats_a(B, A, mask))) {
                uint64_t nmask = mask & ~(1ull << B);  // eliminate B
                // cout << space << G.names[A] << "->" << G.names[B] << " START\n";
                // space += "| ";
                int w = solve_winner(nmask);
                // space = space.substr(0, space.length() - 2);

                if (w == A) {
                    // cout << space << G.names[A] << "->" << G.names[B] << " SUCCEEDED (" << G.names[w] << " won)\n";
                    memo_epoch[key] = EPOCH; memo_winner[key] = w; return w;
                }
                // else{
                //     cout << space << G.names[A] << "->" << G.names[B] << " Failed (" << G.names[w] << " won)\n";
                //
                // }
            }
        }
        memo_epoch[key] = EPOCH; memo_winner[key] = -1; return -1;
    }

    //new 3
    AI void reconstruct(uint64_t mask, int WN, vector<int>& elim) {
        elim.clear();
        if (popcount64(mask) == 1) return;

        while (true) {
            bool progressed = false;

            for (const auto& e : edges_sorted) {
                int A = e.A, B = e.B, m = e.m;
                if (A != WN || ((mask >> A) & 1ull) == 0 || ((mask >> B) & 1ull) == 0) continue;

                if (m > 0 || (m <= 0 && !b_defeats_a(B, A, mask))) {
                    uint64_t nmask = mask & ~(1ull << B);
                    if (solve_winner(nmask) == WN) {
                        elim.push_back(B);
                        mask = nmask;
                        progressed = true;
                        if (popcount64(mask) == 1) return;
                        break;  // restart from the strongest edge on the reduced mask
                    }
                }
            }

            if (!progressed){
                cerr << "WARN WARN: Reconstruction failed" << endl;
                return; // no consistent step found (shouldn't happen if WN is valid)
            }
        }
    }
};

// ========================= Group Weights =========================
// this starts in increasing order first, and groups are assigned 0->11
static AI void fibonacci_series(int n, int seed1, int seed2, vector<int>& out){
    out.clear(); out.reserve(n);
    if (n<=0) return;
    if (n==1){ out.push_back(seed1); return; }
    long long a=seed1,b=seed2; out.push_back((int)a); out.push_back((int)b);
    for (int i=2;i<n;++i){ long long c=a+b; if (c>INT_MAX) c=INT_MAX; out.push_back((int)c); a=b; b=c; }
}





// ==================================== Printers ==============================
// // Assume you have GraphTemplate T and an int Wperm[11] in scope:
// print_graph_edges(T, Wperm);              // list with margins
// // print_graph_edges(T);                  // list with group+off only
// print_margin_matrix(T, Wperm);            // NxN margin table
// print_graph_dot(T, Wperm);                // DOT you can pipe to dot -Tpng


// Edge list: prints exactly the directed edges that exist in the tournament.
// If W != nullptr, also prints the realized margin W[group] + off for that edge.
static AI void print_graph_edges(const GraphTemplate& T,
                                     const int* W = nullptr,
                                     ostream& out = cout) {
    const int N = T.N;
    for (int u = 0; u < N; ++u) {
        for (int v = 0; v < N; ++v) {
            if (u == v) continue;
            int iuv = T.IDX(u, v);
            if (!T.dir[iuv]) continue; // only print the actual directed edge
            out << T.names[u] << " -> " << T.names[v]
                << " [g=" << int(T.group[iuv]) << ", off=" << int(T.off[iuv]);
            if (W) {
                int w = W[T.group[iuv]] + int(T.off[iuv]);
                out << ", w=" << w;
            }
            out << "]\n";
        }
    }
}

// Margin matrix: prints m(u,v) in a compact NxN table.
// Requires W (since margins depend on weights).
static AI void print_margin_matrix(const GraphTemplate& T,
                                       const int* W,
                                       ostream& out = cout,
                                       int field_width = 6) {
    const int N = T.N;
    // header
    out << setw(field_width) << "";
    for (int v = 0; v < N; ++v) out << setw(field_width) << T.names[v];
    out << '\n';
    // rows
    for (int u = 0; u < N; ++u) {
        out << setw(field_width) << T.names[u];
        for (int v = 0; v < N; ++v) {
            if (u == v) {
                out << setw(field_width) << ".";
            } else {
                int m = T.margin(u, v, W);
                out << setw(field_width) << m;
            }
        }
        out << '\n';
    }
}

// Graphviz DOT: minimal digraph with (optional) margin labels.
// If W == nullptr, labels show "g:off". If W provided, shows "m=...".
static AI void print_graph_dot(const GraphTemplate& T,
                                   const int* W = nullptr,
                                   ostream& out = cout) {
    const int N = T.N;
    out << "digraph G {\n";
    out << "  rankdir=LR;\n";
    for (int i = 0; i < N; ++i) {
        out << "  \"" << T.names[i] << "\";\n";
    }
    for (int u = 0; u < N; ++u) {
        for (int v = 0; v < N; ++v) {
            if (u == v) continue;
            int iuv = T.IDX(u, v);
            if (!T.dir[iuv]) continue;
            out << "  \"" << T.names[u] << "\" -> \"" << T.names[v] << "\"";
            if (W) {
                int m = W[T.group[iuv]] + int(T.off[iuv]);
                out << " [label=\"m=" << m << "\"]";
            } else {
                out << " [label=\"g=" << int(T.group[iuv])
                    << ",off=" << int(T.off[iuv]) << "\"]";
            }
            out << ";\n";
        }
    }
    out << "}\n";
}// ---------- Edges sorted by weight ----------
struct EdgeRec {
    int u, v;
    int g;       // group id
    int off;     // offset
    int w;       // realized weight = W[g] + off
};

static AI void print_edges_by_weight(const GraphTemplate& T,
                                         const int* W,
                                         ostream& out = cout,
                                         bool descending = true,
                                         size_t max_edges = (size_t)-1) {
    const int N = T.N;
    vector<EdgeRec> E;
    E.reserve((size_t)N * (size_t)(N - 1) / 2);

    // Collect each existing directed edge exactly once.
    for (int u = 0; u < N; ++u) {
        for (int v = 0; v < N; ++v) {
            if (u == v) continue;
            int iuv = T.IDX(u, v);
            if (!T.dir[iuv]) continue;
            int g = (int)T.group[iuv];
            int off = (int)T.off[iuv];
            int w = W ? (W[g] + off) : off;  // W must be provided for true margins
            E.push_back({u, v, g, off, w});
        }
    }

    // Sort by realized weight (margin)
    sort(E.begin(), E.end(), [&](const EdgeRec& a, const EdgeRec& b){
        return descending ? (a.w > b.w) : (a.w < b.w);
    });

    // Print (optionally only top max_edges)
    size_t cnt = min(max_edges, E.size());
    for (size_t i = 0; i < cnt; ++i) {
        const auto& e = E[i];
        out << T.names[e.u] << " -> " << T.names[e.v]
            << "  [w=" << e.w << ", g=" << e.g << ", off=" << e.off << "]\n";
    }
}

// ---- Vector-friendly wrappers for the printers ----
// (They forward to the pointer versions without copying.)

static AI void print_graph_edges(const GraphTemplate& T,
                                     const vector<int>& W,
                                     ostream& out = cout) {
    print_graph_edges(T, W.data(), out);
}

static AI void print_margin_matrix(const GraphTemplate& T,
                                       const vector<int>& W,
                                       ostream& out = cout,
                                       int field_width = 6) {
    print_margin_matrix(T, W.data(), out, field_width);
}

static AI void print_graph_dot(const GraphTemplate& T,
                                   const vector<int>& W,
                                   ostream& out = cout) {
    print_graph_dot(T, W.data(), out);
}

static AI void print_edges_by_weight(const GraphTemplate& T,
                                         const vector<int>& W,
                                         ostream& out = cout,
                                         bool descending = true,
                                         size_t max_edges = (size_t)-1) {
    print_edges_by_weight(T, W.data(), out, descending, max_edges);
}
// // Visual Validation
// for (int i =0; i< T_sat.size(); i++){
//     copy(sat_sets[i].begin(), sat_sets[i].end(), ostream_iterator<string>(cout, " "));
//     cout << '\n';
//     print_graph_edges(T_sat[i], W);              // list with margins
//     // print_graph_edges(T);                  // list with group+off only
//     // print_margin_matrix(t, W);            // NxN margin table
//     // print_graph_dot(t, W);                // DOT you can pipe to dot -Tpng
//     cout << '\n';
// }
// vector<string> tempstr = {"(x1, x2, x3)", "(~x1, x2, x3)", "(~x1, x2, ~x3)", "(~x1, ~x2, x3)", "(~x1, ~x2, ~x3)"};
// GraphTemplate temp = build_template_from_strings(tempstr);
// copy(tempstr.begin(), tempstr.end(), ostream_iterator<string>(cout, " "));
// cout << '\n';
// print_graph_edges(temp, W);              // list with margins
// // print_graph_edges(T);                  // list with group+off only
// // print_margin_matrix(t, W);            // NxN margin table
// // print_graph_dot(t, W);                // DOT you can pipe to dot -Tpng
// cout << '\n';
// return 0;
// // W as vector
// print_edges_by_weight(T, W);                    // descending
// print_edges_by_weight(T, W, cout, false);  // ascending
// // or top-K
// print_edges_by_weight(T, W, cout, true, 20);

AI string join_clauses(const vector<string>& v) {
    if (v.empty()) return {};
    size_t total = 0;
    for (const auto& s : v) total += s.size();
    string out;
    out.reserve(total + (v.size() - 1)); // commas
    out += v[0];
    for (size_t i = 1; i < v.size(); ++i) {
        out.push_back(',');
        out += v[i];
    }
    return out;
}





// ========================= Exhaustive Driver (fast) =========================
#define STABLEVOTING_MAIN true

#ifdef STABLEVOTING_MAIN
int main(){
    // -O3 -march=native -flto -fomit-frame-pointer -DNDEBUG
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

    // Exhaustive permutations of 11 weights
    size_t total_perms=1; for(int i=2;i<=NUM_GROUPS;++i) total_perms*= (size_t)i;
    cout << "Exhaustively testing " << total_perms << " permutations..." << endl;

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
            if (w<0) {
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
                vector<int> elim;
                S.reconstruct(full, w, elim);

                // clause set, winner, elimination order
                cout << "  [" << ui << "] " << join_clauses(unsat_sets[ui]) << "\n";
                cout << "     winner=" << (w >= 0 ? T.names[w] : string("None"));
                cout << "     elim=[";
                for (size_t i = 0; i < elim.size(); ++i) { if (i) cout << ','; cout << T.names[elim[i]]; }
                cout << "]\n";
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

    cout << "\nDONE. perms_done="<<perms_done
         << " fails="<<all_failures
         << "\n";

    return 0;
}
#endif
