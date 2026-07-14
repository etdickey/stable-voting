#pragma once

#include <algorithm>
#include <cstddef>
#include <iomanip>
#include <iostream>
#include <limits>
#include <ostream>
#include <string>
#include <vector>

#include "formula_suites.hpp"
#include "graph_template.hpp"
#include "sv_fast.hpp"
#include "tqbf_tournament_builder.hpp"
#include "fast_utils.hpp"

// ==================================== Printers ==============================
// // Assume you have GraphTemplate T and an int Wperm[11] in scope:
// print_graph_edges(T, Wperm);              // list with margins
// // print_graph_edges(T);                  // list with group+off only
// print_margin_matrix(T, Wperm);            // NxN margin table
// print_graph_dot(T, Wperm);                // DOT you can pipe to dot -Tpng

AI string elim_string(const vector<int>& elim, const GraphTemplate& T, size_t initial_candidate_count) {
    string out = "[";
    for (size_t i = 0; i < elim.size(); ++i) {
        if (i) out += ',';
        out += T.names.at(elim[i]);
    }
    if (elim.size() + 1 != initial_candidate_count) {
        out += " (incomplete)";
    }
    out += ']';
    return out;
}

// AI string elim_string(const vector<int>& elim, const GraphTemplate& T) {
//     return elim_string(elim, T, T.N);
// }
AI string get_elim_order_string(const vector<int>& elim, const GraphTemplate& T) {
    return elim_string(elim, T, T.N);
}

AI string join_clauses(const vector<string>& clauses) {
    string out;
    for (size_t i = 0; i < clauses.size(); ++i) {
        if (i) out.push_back(',');
        out += clauses[i];
    }
    return out;
}

AI void print_formula_cases(const char* heading,
            const vector<const formula_suites::FormulaCase*>& cases,
            const vector<GraphTemplate>& templates,
            vector<SVFast>& solvers, const int* weights, ostream& out = cout) {
    out << heading << " (" << cases.size() << "):\n";
    for (size_t i = 0; i < cases.size(); ++i) {
        const auto& test_case = *cases[i];
        const auto& graph = templates[i];
        auto& solver = solvers[i]; solver.reset_epoch(weights);

        // Solve and reconstruct elimination order
        const int winner = solver.solve_winner(graph.full_mask);// Full bitmask of active nodes for this template

        // Print clause set, winner, elimination order
        out << "  [" << test_case.name << "] expected="
               << formula_suites::expected_outcome_name(test_case) << '\n'
               << "    assignment=" << assignment_string(test_case) << '\n'
               << "    " << join_clauses(test_case.clauses) << '\n'
               << "    winner=" << (winner >= 0 ? graph.names[winner] : string("None"));
        if (winner >= 0) {
            vector<int> elim;
            solver.reconstruct(graph.full_mask, winner, elim);
            out << "\n     elimination="
                   << get_elim_order_string(elim, graph);
        }
        out << '\n';
    }
}

// Edge list: prints exactly the directed edges that exist in the tournament.
// If W != nullptr, also prints the realized margin W[group] + off for that edge.
AI void print_graph_edges(const GraphTemplate& graph, const int* weights = nullptr,
                          ostream& out = cout) {
    const int N = graph.N;
    for (int u = 0; u < N; ++u) {
        for (int v = 0; v < N; ++v) {
            if (u == v) continue;
            const int edge = graph.IDX(u, v);
            if (!graph.dir[edge]) continue;
            const int internal_group = graph.group[edge];
            out << graph.names[u] << " -> " << graph.names[v]
                   << " [g" << index_to_paper_group(internal_group)
                   << ", index=" << internal_group
                   << ", offset=" << graph.off[edge];
            if (weights != nullptr) {
                out << ", margin="
                       << weights[internal_group] + graph.off[edge];
            }
            out << "]\n";
        }
    }
}

// Margin matrix: prints m(u,v) in a compact NxN table.
// Requires W (since margins depend on weights).
AI void print_margin_matrix(const GraphTemplate& graph, const int* weights,
                            ostream& out = cout, int field_width = 7,
                            bool use_pipes = false, bool hide_neg = false) {
    string pipe = use_pipes ? " |" : "";

    out << setw(field_width) << "" << pipe;
    for (const string& name : graph.names) out << setw(field_width) << name << pipe;
    out << '\n';

    if (use_pipes) {
        for (int i = 0; i <= graph.N; ++i) out << string(field_width + 1, '-') << "|";
        out << '\n';
    }

    for (int u = 0; u < graph.N; ++u) {
        out << setw(field_width) << graph.names[u] << pipe;
        for (int v = 0; v < graph.N; ++v) {
            int m = graph.margin(u, v, weights);
            if (u == v) out << setw(field_width) << '.' << pipe;
            else out << setw(field_width) << ((hide_neg && m < 0) ? "" : to_string(m)) << pipe;
        }
        out << '\n';
    }
}

// Graphviz DOT: minimal digraph with (optional) margin labels.
// If W == nullptr, labels show "g:off". If W provided, shows "m=...".
AI void print_graph_dot(const GraphTemplate& graph, const int* weights = nullptr,
                        ostream& out = cout) {
    const int N = graph.N;
    out << "digraph G {\n  rankdir=LR;\n";
    for (const string& name : graph.names) {
        out << "  \"" << name << "\";\n";
    }
    for (int u = 0; u < N; ++u) {
        for (int v = 0; v < N; ++v) {
            const int edge = graph.IDX(u, v);
            if (!graph.dir[edge]) continue;
            const int internal_group = graph.group[edge];
            out << "  \"" << graph.names[u] << "\" -> \"" << graph.names[v] << "\" [label=\"g"
                << index_to_paper_group(internal_group);
            if (weights != nullptr) {
                out << ":" << weights[internal_group] + graph.off[edge];
            }
            out << "\"];\n";
        }
    }
    out << "}\n";
}

// ---------- Edges sorted by weight ----------
struct EdgeRecord {
    int u, v;
    int internal_group; // group id
    int offset;         // offset
    int margin;         // realized weight = W[g] + off
};

AI void print_edges_by_weight(const GraphTemplate& graph,
                              const int* weights,
                              ostream& out = cout,
                              bool descending = true,
                              size_t max_edges = numeric_limits<size_t>::max()) {
    const int N = graph.N;
    vector<EdgeRecord> edges;
    edges.reserve((size_t)N * (N - 1) / 2);

    // Collect each existing directed edge exactly once.
    for (int u = 0; u < N; ++u) {
        for (int v = 0; v < N; ++v) {
            const int edge = graph.IDX(u, v);
            if (!graph.dir[edge]) continue;

            const int internal_group = graph.group[edge];
            const int offset = graph.off[edge];
            edges.push_back({u, v, internal_group, offset,
                             weights[internal_group] + offset});
        }
    }

    // Sort by realized weight (margin)
    sort(edges.begin(), edges.end(), [descending](const auto& a, const auto& b) {
        if (a.margin != b.margin) return descending ? a.margin > b.margin : a.margin < b.margin;
        if (a.u != b.u) return a.u < b.u;
        return a.v < b.v;
    });

    // Print (optionally only top max_edges)
    const size_t count = min(max_edges, edges.size());
    for (size_t i = 0; i < count; ++i) {
        const auto& edge = edges[i];
        out << graph.names[edge.u] << " -> " << graph.names[edge.v]
               << " [margin=" << edge.margin
               << ", g" << index_to_paper_group(edge.internal_group)
               << ", index=" << edge.internal_group
               << ", offset=" << edge.offset << "]\n";
    }
}

// ---- Vector-friendly wrappers for the printers ----
// (They forward to the pointer versions without copying.)
AI void print_graph_edges(const GraphTemplate& T, const vector<int>& weights, ostream& out = cout) {
    print_graph_edges(T, weights.data(), out);
}

AI void print_margin_matrix(const GraphTemplate& T, const vector<int>& weights, ostream& out = cout, int field_width = 7) {
    print_margin_matrix(T, weights.data(), out, field_width);
}

AI void print_graph_dot(const GraphTemplate& T, const vector<int>& weights, ostream& out = cout) {
    print_graph_dot(T, weights.data(), out);
}

AI void print_edges_by_weight(const GraphTemplate& T, const vector<int>& weights, ostream& out = cout, bool descending = true, size_t max_edges = numeric_limits<size_t>::max()) {
    print_edges_by_weight(T, weights.data(), out, descending, max_edges);
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
