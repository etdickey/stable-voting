// template_builders.h
#pragma once

// self
#include "graph_template.hpp"

AI constexpr int paper_group_to_index(int paper_group) {
    return GraphTemplate::kGroupCount - paper_group;
}

AI constexpr int index_to_paper_group(int group_index) {
    return GraphTemplate::kGroupCount - group_index;
}

static AI void tmpl_init(GraphTemplate& T, int N){
    T.N = N;
    T.dir.assign((size_t)N*(size_t)N, 0);
    T.group.assign((size_t)N*(size_t)N, 0);
    T.off.assign((size_t)N*(size_t)N, 0);
    T.full_mask = (N == 64 ? ~0ull : ((1ull << N) - 1ull));// all candidates in this tournament
    // names is reset in build_template_from_clauses
}

static AI void tmpl_add_edge(GraphTemplate& T, int u,int v, int g, int16_t& cur_d){
    // cur_d = current delta (for group assignments)
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
// 12 effective weight groups (g1..g12). Groups 9a..9c share g9 with jumps.
static GraphTemplate build_template_from_clauses(const vector<vector<pair<int,bool>>>& clauses){
    GraphTemplate T;
    int m = (int)clauses.size();
    int n = 0; // infer max var
    for (auto& c : clauses) for (auto [v,_] : c) n = max(n, v);
    T.m = m; T.n = n;

    // index layout: [C][D][L1..Lm][T1..Tn][F1..Fn][X1..Xn]
    int idxC = 0;
    int idxD = 1;
    int baseL = 2;
    int baseT = baseL + m;
    int baseF = baseT + n;
    int baseX = baseF + n;
    int N = 2 + m + 3*n;
    tmpl_init(T, N);

    T.idxC = idxC;
    T.names.resize(N);
    T.names[idxC] = "C";
    T.names[idxD] = "D";
    for (int k=0;k<m;++k) T.names[baseL+k] = string("L") + to_string(k+1);
    for (int i=0;i<n;++i) T.names[baseT+i] = string("T") + to_string(i+1);
    for (int i=0;i<n;++i) T.names[baseF+i] = string("F") + to_string(i+1);
    for (int i=0;i<n;++i) T.names[baseX+i] = string("X") + to_string(i+1);
    // Compute T/F mask once
    T.tf_mask = 0;
    for (int i = 0; i < n; ++i) {
        T.tf_mask |= (1ull << (baseT + i)); // Ti
        T.tf_mask |= (1ull << (baseF + i)); // Fi
    }


    // Per-group current delta (start at -1 and decreases)
    // g1..g12 (12 groups total); 9a..9c share g9
    int16_t cur[12];
    for (int g=0; g<12; ++g) cur[g] = -1;

    auto L=[&](int k){ return baseL + k; };
    auto Tn=[&](int i){ return baseT + i; };
    auto Fn=[&](int i){ return baseF + i; };
    auto Xn=[&](int i){ return baseX + i; };

    int CGP_IDX = 11;

    // ---- g1: C -> Fi, Ti ----
    for (int i=0;i<n;++i){
        //if i is odd then first goes to C then to D, else first to D then to C
        if (i%2==0){
            tmpl_add_edge(T, idxC, Fn(i), CGP_IDX, cur[CGP_IDX]);
            tmpl_add_edge(T, idxC, Tn(i), CGP_IDX, cur[CGP_IDX]);
            tmpl_add_edge(T, idxD, Fn(i), CGP_IDX, cur[CGP_IDX]);
            tmpl_add_edge(T, idxD, Tn(i), CGP_IDX, cur[CGP_IDX]);
        }else{
            tmpl_add_edge(T, idxD, Fn(i), CGP_IDX, cur[CGP_IDX]);
            tmpl_add_edge(T, idxD, Tn(i), CGP_IDX, cur[CGP_IDX]);
            tmpl_add_edge(T, idxC, Fn(i), CGP_IDX, cur[CGP_IDX]);
            tmpl_add_edge(T, idxC, Tn(i), CGP_IDX, cur[CGP_IDX]);
        }
    }
    CGP_IDX--;


    // ---- g2: Fi, Ti -> Xi ----
    for (int i=0;i<n;++i){
        tmpl_add_edge(T, Fn(i), Xn(i), CGP_IDX, cur[CGP_IDX]);
        tmpl_add_edge(T, Tn(i), Xn(i), CGP_IDX, cur[CGP_IDX]);
    }
    CGP_IDX--;


    // ---- g3: {Ti,Fj in clause k} -> Lk ---- //ORIGINAL
    // NO---- g3: {Ti,Fj in clause k} <- Lk ----
    for (int k=0;k<m;++k){
        for (auto [var,pos] : clauses[k]){
            int i = var-1;
            tmpl_add_edge(T, pos? Tn(i): Fn(i), L(k), CGP_IDX, cur[CGP_IDX]); //ORIGINAL
            //tmpl_add_edge(T, pos? Fn(i): Tn(i), L(k), CGP_IDX, cur[CGP_IDX]);
        }
    }
    CGP_IDX--;

    // ---- g4: Lk -> {Fi, Tj not in clause k}, opposite for those in it ---- //ORIGINAL
    // NO ---- g4: Lk <- {Fi, Tj not in clause k}, opposite for those in it ----
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
                tmpl_add_edge(T, L(k), pos? Fn(i): Tn(i), CGP_IDX, cur[CGP_IDX]); //ORIGINAL
                //tmpl_add_edge(T, L(k), pos? Tn(i): Fn(i), CGP_IDX, cur[CGP_IDX]);
            }else{
                // not in clause -> both
                tmpl_add_edge(T, L(k), Fn(i), CGP_IDX, cur[CGP_IDX]);
                tmpl_add_edge(T, L(k), Tn(i), CGP_IDX, cur[CGP_IDX]);
            }
        }
    }
    CGP_IDX--;

    // ---- g5: Xj -> {Fi, Ti for all i != j} ----
    for (int j=0;j<n;++j){
        for (int i=0;i<n;++i) if (i!=j){
            tmpl_add_edge(T, Xn(j), Fn(i), CGP_IDX, cur[CGP_IDX]);
            tmpl_add_edge(T, Xn(j), Tn(i), CGP_IDX, cur[CGP_IDX]);
        }
    }
    CGP_IDX--;

    // ---- g6: Xj -> Lk for all i,k ----
    for (int i=0;i<n;++i) for (int k=0;k<m;++k){
        tmpl_add_edge(T, Xn(i), L(k), CGP_IDX, cur[CGP_IDX]);
    }
    CGP_IDX--;

    // ---- g7: Lk -> C ---- D -> Lk
    for (int k=0;k<m;++k) tmpl_add_edge(T, L(k), idxC, CGP_IDX, cur[CGP_IDX]);
    for (int k=0;k<m;++k) tmpl_add_edge(T, idxD, L(k), CGP_IDX, cur[CGP_IDX]);
    CGP_IDX--;

    // ---- g8: Xi -> C ----
    for (int i=0;i<n;++i) tmpl_add_edge(T, Xn(i), idxC, CGP_IDX, cur[CGP_IDX]);
    for (int i=0;i<n;++i) tmpl_add_edge(T, Xn(i), idxD, CGP_IDX, cur[CGP_IDX]);
    CGP_IDX--;


    // ---- g9 (shared by 9a..9c) ----
    // -- g9a: Fi -> Ti; Fi -> Tj (j>i); Ti -> Fj (j>i) --
    for (int i=0;i<n;++i){
        tmpl_add_edge(T, Fn(i), Tn(i), CGP_IDX, cur[CGP_IDX]);
        for (int j=i+1;j<n;++j){
            tmpl_add_edge(T, Fn(i), Tn(j), CGP_IDX, cur[CGP_IDX]);
        }
        for (int j=i+1;j<n;++j){
            tmpl_add_edge(T, Tn(i), Fn(j), CGP_IDX, cur[CGP_IDX]);
        }
    }
    cur[CGP_IDX] = (int16_t)(cur[CGP_IDX] - 10); // small jump between 9a and 9b

    // -- g9b: Fi -> Fj (i<j) --
    for (int i=0;i<n;++i) for (int j=i+1;j<n;++j){
        tmpl_add_edge(T, Fn(i), Fn(j), CGP_IDX, cur[CGP_IDX]);
    }
    cur[CGP_IDX] = (int16_t)(cur[CGP_IDX] - 10); // small jump between 9b and 9c

    // -- g9c: Ti -> Tj (i<j) --
    for (int i=0;i<n;++i) for (int j=i+1;j<n;++j){
        tmpl_add_edge(T, Tn(i), Tn(j), CGP_IDX, cur[CGP_IDX]);
    }
    CGP_IDX--;
    // ---- END g9 ----


    // ---- g10: Xi -> Xj (i<j) ----
    for (int i=0;i<n;++i) for (int j=i+1;j<n;++j){
        tmpl_add_edge(T, Xn(i), Xn(j), CGP_IDX, cur[CGP_IDX]);
    }
    CGP_IDX--;

    // ---- g11: Li -> Lj (i<j) ----
    for (int i=0;i<m;++i) for (int j=i+1;j<m;++j){
        tmpl_add_edge(T, L(i), L(j), CGP_IDX, cur[CGP_IDX]);
    }
    CGP_IDX--;

    // ---- g12: C -> D ----
    tmpl_add_edge(T, idxC, idxD, CGP_IDX, cur[CGP_IDX]);

    return T;
}
