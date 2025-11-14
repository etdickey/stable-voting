// printers.h
#pragma once

#include <vector>
#include <iostream>
// self
#include "graph_template.hpp"
#include "fast_utils.hpp"

#if defined(__GNUC__) || defined(__clang__)
  #define AI inline __attribute__((always_inline))
#else
  #define AI inline
#endif


// ==================================== Printers ==============================
// // Assume you have GraphTemplate T and an int Wperm[11] in scope:
// print_graph_edges(T, Wperm);              // list with margins
// // print_graph_edges(T);                  // list with group+off only
// print_margin_matrix(T, Wperm);            // NxN margin table
// print_graph_dot(T, Wperm);                // DOT you can pipe to dot -Tpng

string get_elim_order_string(const vector<int>& elim, const GraphTemplate& T){
    string out = "";
    out += "[";
    for (size_t i = 0; i < elim.size(); ++i) {
        if (i) out += ",";
        out += T.names[elim[i]];
    }
    // If incomplete: elim should contain (K − 1) elements when mask had K survivors
    if ((int)elim.size() + 1 != T.N) {
        out += " (incomplete)";
    }
    out += "]";
    return out;
}


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

template<size_t K>
AI string join_assignment(const array<int,K>& a) {
    // UNSAT-style or intentionally NULL → all -1
    bool all_neg1 = true;
    for (size_t i = 0; i < K; ++i) {
        if (a[i] != -1) { all_neg1 = false; break; }
    }
    if (all_neg1) return "UNSAT";

    // proper assignment → format (1,0,1)
    string out; out.reserve(3*K + 2);

    out.push_back('('); out.push_back(a[0] ? '1' : '0');
    for (size_t i = 1; i < K; ++i) {
        out.push_back(','); out.push_back(a[i] ? '1' : '0');
    }
    out.push_back(')');

    return out;
}

template<size_t K>
void print_sat_cases(const vector<GraphTemplate>& T_sat, vector<SVFast>& sol_sat,
            const vector<vector<string>>& sat_sets,
            const vector<array<int,K>>& sat_assign,
            const int Wperm[/*NUM_GROUPS*/], bool UNSAT = false){
    for (size_t si = 0; si < T_sat.size(); ++si) {
        const auto& T = T_sat[si];
        auto& S = sol_sat[si];

        S.reset_epoch(Wperm);

        // Solve and reconstruct elimination order
        int w = S.solve_winner(T.full_mask);// Full bitmask of active nodes for this template

        // Print clause set, winner, elimination order
        cout << "  [" << si << "]";
        cout << "  assign=" << (sat_assign.size() ? join_assignment(sat_assign[si]) : "UNINITIALIZED");
        cout << "  " << join_clauses(sat_sets[si]);
        cout << "\n";
        cout << "     winner=" << (w >= 0 ? T.names[w] : string("None"));
        if(!UNSAT || w>=0){ //print for SAT, if UNSAT, print if winner is valid
            vector<int> elim;
            S.reconstruct(T.full_mask, w, elim);
            cout << "\n     elim=" << get_elim_order_string(elim, T);
        }
        cout << "\n";
    }
}


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
}
// ---------- Edges sorted by weight ----------
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
