// sv_fast.h
#pragma once

#include <vector>
#include <sstream>

// self
#include "graph_template.hpp"

#if defined(__GNUC__) || defined(__clang__)
  #define AI inline __attribute__((always_inline))
#else
  #define AI inline
#endif


// ========================= Stable Voting (fast) =========================
struct SVFast {
    const GraphTemplate& G;
    const int N;
    //-------choose solver-------
    // Which SV rule to use?
    // 0 = Standard SV
    // 1 = Prioritized: eliminate n/2 TF nodes first
    static const int RULE = 1;
    //-------choose SV or SSV-------
    // true = Stable Voting
    // false = Simple Stable Voting
    static const bool CHECK_DEFEATS = false;
    // If prioritized and SV (not simple), may fail to produce a winner
    static const bool MAY_FAIL = (RULE == 1 && CHECK_DEFEATS == true);
    // Mask of T/F nodes (only needed if RULE == 1)
    uint64_t TF_MASK = 0;
    //-------choose solver end-------
    static string static_config_string() {
        ostringstream o;
        o << "Rule: "
          << (RULE==0 ? "Standard Stable Voting"
                      : RULE==1 ? "TF-prioritized (eliminate half of TF first)"
                                : "Unknown")
          << ", Mode: " << (CHECK_DEFEATS ? "Stable Voting" : "Simple Stable Voting")
          << ", MayFail: " << (MAY_FAIL ? "may fail to produce a winner" : "always produces a winner");

        return o.str();
    }
    string config_string() const {
        ostringstream o;
        o << static_config_string();   // reuse the shared descriptive text
        o << ", N=" << N;

        if (RULE == 1) {
            int tf = popcount64(TF_MASK);
            o << ", TF_total=" << tf
              << ", must_eliminate=" << (tf/2);
        }

        return o.str();
    }



    //----------------------ACTUAL SOLVER STUFF---------------------------------
    // Memo (winner only) using epoch-tag trick for O(1) clears
    vector<int> memo_winner;        // winner idx or -1 (none)
    vector<uint32_t> memo_epoch;    // epoch tag
    uint32_t EPOCH = 1;

    // memo for constrained rule
    vector<int> memo_winner_tf;
    vector<uint32_t> memo_epoch_tf;
    int max_tf_state = 0;

    struct Match { int A,B,m; };
    vector<Match> edges_sorted;
    string matchStr(const Match& m){
        ostringstream oss;
        oss << "{A (" << m.A << "): " << G.names[m.A]
            << ", B (" << m.B << "): " << G.names[m.B]
            << ", m: " << m.m << "}";
        return oss.str();
    }

    // Per-permutation realized margins and sorted adjacency
    bool prepared = false;
    vector<int> M;                  // size N*N, M[u*N+v] = margin(u->v) NOT sorted
    vector<vector<int>> nbrs;  // nbrs[u] = v's sorted by M[u,v] desc "neighbors"
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

        TF_MASK = G.tf_mask; //load the TF mask directly from the template
        // tf_total is the *initial* number of T/F nodes we care about
        max_tf_state = popcount64(TF_MASK);
        size_t total_states = S * size_t(max_tf_state + 1);
        memo_winner_tf.assign(total_states, -2);
        memo_epoch_tf.assign(total_states, 0);
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
            fill(memo_epoch_tf.begin(), memo_epoch_tf.end(), 0);
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

    // Public dispatcher solve_winner(mask)
    // Decides which solver to call based on RULE.
    int solve_winner(uint64_t mask) {
        switch (RULE) {
            case 0: return solve_winner_standard(mask);// Standard Stable Voting
            case 1: return solve_winner_tf_half(mask, TF_MASK);// TF-half prioritized rule
            default:
                cerr << "ERROR: Unknown SV rule: " << RULE << endl;
                return -1;
        }
    }


    // const bool DEBUG_STANDARD = false;
    int solve_winner_standard(uint64_t mask) {
        // is the current graph permutation the one where memo_winner was set?
        size_t key = (size_t)mask;
        if (memo_epoch[key]==EPOCH) return memo_winner[key];

        if (popcount64(mask)==1){// single survivor?
            int w = ctz64(mask);
            memo_epoch[key]=EPOCH; memo_winner[key]=w; return w;
        }

        // if(DEBUG_STANDARD){
        //     // vector<string> ansS = {"F1", "X2", "X1", "T1", "L4", "L3", "L2", "F2", "L1", "T2", "C"};
        //     //                     // ['F1', 'X2', 'X1', 'T1', 'L4', 'L3', 'L2', 'F2', 'L1', 'T2']
        //     // vector<int> ans(N);
        //     // for(int i=0; i<N; i++){
        //     //     ans[i] = find(G.names.begin(), G.names.end(), ansS[i]) - G.names.begin();
        //     //     // if(find(G.names.begin(), G.names.end(), ansS[i]) == G.names.end()) cout << "Couldn't find this guy: \'" << ansS[i] << "\'" << endl;
        //     //     // else cout << "Found \'" << ansS[i] << "\' at " << ans[i] << endl;
        //     // }
        //     int tmpidx = popcount64(mask);
        //     cout << "[SV]Curr cand left: " << tmpidx << " [";
        //     for (int i = 0; i < N; ++i) if (mask & (1ull << i)) cout << G.names[i] << ", ";
        //     cout << "], curr removed: [";
        //     for (int i = 0; i < N; ++i) if (!(mask & (1ull << i))) cout << G.names[i] << ", ";
        //     cout << "]\n";
        //     // ", winner should be:" << ans[N - tmpidx] << ": " << ansS[N - tmpidx] << '\n';
        // }

        // Scan edges in global descending order; try the first valid one whose endpoints are active.
        for (const auto& e : edges_sorted) {
            int A = e.A, B = e.B, m = e.m;
            if (((mask >> A) & 1ull) == 0 || ((mask >> B) & 1ull) == 0) continue; // edge not in subtournament

            if (!CHECK_DEFEATS || (m > 0 || (m <= 0 && !b_defeats_a(B, A, mask)))) {
                uint64_t nmask = mask & ~(1ull << B); // eliminate B

                // if(DEBUG_STANDARD) cout << space << G.names[A] << "->" << G.names[B] << " START\n";
                // if(DEBUG_STANDARD) space += "| ";
                int w = solve_winner_standard(nmask);
                // if(DEBUG_STANDARD) space.resize(space.size() - 2);   // shrink indent on unwind

                if (w == A) {
                    // if(DEBUG_STANDARD) cout << space << G.names[A] << "->" << G.names[B] << " SUCCEEDED (" << G.names[w] << " won)\n";
                    memo_epoch[key] = EPOCH; memo_winner[key] = w; return w;
                }
                // else if(DEBUG_STANDARD) {
                    // cout << space << G.names[A] << "->" << G.names[B] << " Failed (" << G.names[w] << " won)\n";
                // }
            }
        }
        // if(DEBUG_STANDARD) cout << space << "returning NONE!!\n";
        memo_epoch[key] = EPOCH; memo_winner[key] = -1; return -1;
    }

    //---------------------------------SOLVER 2---------------------------------
    // NEW small helpers for constrained rule:
    AI size_t tf_state_index(uint64_t mask, int tf_left) const {
        // assumes N <= 20 or so; mask fits in size_t
        size_t base = size_t{1} << N;   // number of masks
        return size_t(tf_left) * base + size_t(mask);
    }


    // string space = "- ";
    // const bool DEBUG_PRIORITIZED = false;
    // Core constrained solver (only *extra* logic is tf_left & tf_mask)
    int solve_winner_prioritized(uint64_t mask, uint64_t priority_mask, int tf_left){
        // If we’ve already satisfied the “eliminate K TF nodes first” requirement,
        // the rest is just standard SV.
        if (tf_left == 0){
            // if(DEBUG_PRIORITIZED){
            //     // int tmpidx = popcount64(mask);
            //     // cout << "[PER-SV]Curr cand left: " << tmpidx << " [";
            //     // for (int i = 0; i < N; ++i) if (mask & (1ull << i)) cout << G.names[i] << ", ";
            //     // cout << "], curr removed: [";
            //     // for (int i = 0; i < N; ++i) if (!(mask & (1ull << i))) cout << G.names[i] << ", ";
            //     // cout << "]\n";
            // }
            return solve_winner_standard(mask);
        }

        // is the current graph permutation the one where memo_winner was set?
        size_t key = tf_state_index(mask, tf_left);
        if (memo_epoch_tf[key] == EPOCH) return memo_winner_tf[key];

        if (popcount64(mask) == 1) {// single survivor?
            int w = ctz64(mask);
            memo_epoch_tf[key] = EPOCH; memo_winner_tf[key] = w; return w;
        }


        // Scan edges in global descending order; try the first valid one whose endpoints are active.
        for (const auto& e : edges_sorted) {
            int A = e.A, B = e.B, m = e.m;
            if (((mask >> A) & 1ull) == 0 || ((mask >> B) & 1ull) == 0) continue; // edge not in subtournament

            bool B_is_priority = ((priority_mask >> B) & 1ull) != 0;
            // tf_left > 0 is already true by the first function check
            if (!B_is_priority) continue; //skip if B not in priority list and still have to select some from that set

            if (!CHECK_DEFEATS || (m > 0 || (m <= 0 && !b_defeats_a(B, A, mask)))) {
                uint64_t nmask = mask & ~(1ull << B); // eliminate B
                int next_tf_left = tf_left - (B_is_priority ? 1 : 0);//check is not necessary, continued above

                // if(DEBUG_PRIORITIZED) cout << space << G.names[A] << "->" << G.names[B] << " START\n";
                // if(DEBUG_PRIORITIZED) space += "| ";
                int w = solve_winner_prioritized(nmask, priority_mask, next_tf_left);
                // if(DEBUG_PRIORITIZED) space.resize(space.size() - 2);   // shrink indent on unwind

                // string winnerName = (w >=0 ? G.names[w] : "NONE");
                if (w == A) {
                    // if(DEBUG_PRIORITIZED) cout << space << G.names[A] << "->" << G.names[B] << " SUCCEEDED (" << winnerName << " won)\n";
                    memo_epoch_tf[key] = EPOCH; memo_winner_tf[key] = w; return w;
                }
                // else if(DEBUG_PRIORITIZED) {
                //     cout << space << G.names[A] << "->" << G.names[B] << " Failed (" << winnerName << " won)\n";
                // }
            }
        }

        // if(DEBUG_PRIORITIZED) cout << space << "returning NONE!!\n";
        memo_epoch_tf[key]  = EPOCH; memo_winner_tf[key] = -1; return -1;
    }

    // Public “separate rule” entry:
    int solve_winner_tf_half(uint64_t mask, uint64_t tf_mask) {
        uint64_t tf_active = mask & tf_mask;
        int tf_total = popcount64(tf_active);
        int must_eliminate = tf_total / 2;  // your “n/2 first” requirement
        return solve_winner_prioritized(mask, tf_mask, must_eliminate);
    }
    //-----------------------------END SOLVER 2---------------------------------




    // RULE: 0 = standard SV, 1 = TF-prioritized
    // TF_MASK: already loaded from G.tf_mask in constructor
    AI void reconstruct(uint64_t mask, int WN, vector<int>& elim) {
        switch (RULE) {
            case 0: reconstruct_standard(mask, WN, elim); break;
            case 1: {
                uint64_t tf_active = mask & TF_MASK;
                int tf_total = popcount64(tf_active);
                int must_eliminate = tf_total / 2;  // your “eliminate n/2 TF nodes first” rule
                reconstruct_prioritized(mask, WN, elim, TF_MASK, must_eliminate);
                break;
            }
            default:
                cerr << "WARN WARN: reconstruct called with unknown RULE = " << RULE << endl;
                elim.clear();
                break;
        }
    }

    //solve_winner_standard, RULE==0
    AI void reconstruct_standard(uint64_t mask, int WN, vector<int>& elim) {
        elim.clear();
        if (popcount64(mask) == 1) return;

        while (true) {
            bool progressed = false;

            for (const auto& e : edges_sorted) {
                int A = e.A, B = e.B, m = e.m;
                if (A != WN || ((mask >> A) & 1ull) == 0 || ((mask >> B) & 1ull) == 0) continue;

                if (!CHECK_DEFEATS || (m > 0 || (m <= 0 && !b_defeats_a(B, A, mask)))) {
                    uint64_t nmask = mask & ~(1ull << B);
                    if (solve_winner_standard(nmask) == WN) {
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

    // Reconstruction for prioritized rule (RULE == 1)
    // priority_mask = TF_MASK; tf_left = how many priority nodes must still be eliminated
    AI void reconstruct_prioritized(uint64_t mask, int WN, vector<int>& elim,
                                    uint64_t priority_mask, int tf_left){
        elim.clear();
        if (popcount64(mask) == 1) return;

        // If the constraint is already satisfied, just fall back to standard reconstruction
        if (tf_left == 0) { reconstruct_standard(mask, WN, elim); return; }

        while (true) {
            bool progressed = false;

            for (const auto& e : edges_sorted) {
                int A = e.A, B = e.B, m = e.m;
                if (A != WN || ((mask >> A) & 1ull) == 0 || ((mask >> B) & 1ull) == 0) continue;

                bool B_is_priority = ((priority_mask >> B) & 1ull) != 0;
                // tf_left is modified below
                if (tf_left > 0 && !B_is_priority) continue;  // can't eliminate non-TF while constraint active

                if (!CHECK_DEFEATS || (m > 0 || (m <= 0 && !b_defeats_a(B, A, mask)))) {
                    uint64_t nmask = mask & ~(1ull << B);
                    int next_tf_left = tf_left - (B_is_priority ? 1 : 0);

                    if (solve_winner_prioritized(nmask, priority_mask, next_tf_left) == WN) {
                        elim.push_back(B);
                        mask = nmask;
                        tf_left = next_tf_left;
                        progressed = true;

                        if (popcount64(mask) == 1) return;
                        if (tf_left == 0) {
                            // From here on, prefix constraint is satisfied: switch to standard.
                            vector<int> tail;
                            reconstruct_standard(mask, WN, tail);
                            elim.insert(elim.end(), tail.begin(), tail.end());
                            return;
                        }
                        break;  // restart from strongest edge on reduced mask
                    }
                }
            }

            if (!progressed) {
                // *** IMPORTANT CHANGE ***
                // For RULE = 1, failing to reconstruct is OK.
                // Do NOT warn—just return with what we have.
                if(!MAY_FAIL) cerr << "WARN WARN: Reconstruction failed" << endl;
                return;
            }
        }
    }

};
