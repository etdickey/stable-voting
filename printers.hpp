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
