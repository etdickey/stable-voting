# Visualizes the current TQBF tournament construction.
# This file intentionally does not import extras/legacy/stablevoting.py.
import re
import networkx as nx
from typing import Dict, Tuple, List, Optional, Iterable, Union
import matplotlib.pyplot as plt
from matplotlib.lines import Line2D
from matplotlib.patches import Patch

NUM_GROUPS = 12


def compact_clause_repr(clause_spec: Union[str, Iterable[str]], max_len: int = 120) -> str:
    """
    Build a compact human-readable representation of the SAT formula:

      - If given a single string, pulls out (...) groups and joins them with ' ∧ '.
      - If given an iterable of clause strings, joins each stripped clause with ' ∧ '.
      - Removes internal whitespace to keep things compact.
      - Truncates with '...' if longer than max_len.
    """
    # Normalize to clause string(s)
    if isinstance(clause_spec, str):
        # Extract each (...) group if present
        groups = re.findall(r'\(([^)]*)\)', clause_spec)
        parts = [f"({g.replace(' ', '')})" for g in groups] if groups else [re.sub(r'\s+', '', clause_spec)]
    else:
        parts = [re.sub(r'\s+', '', str(c).strip()) for c in clause_spec]

    clause_str = " ∧ ".join(parts)
    if len(clause_str) > max_len:
        return clause_str[:max_len - 3] + "..."
    return clause_str


def parse_clause(clause: str) -> List[Tuple[int, bool]]:
    literals = []
    for negated, number in re.findall(r'(~)?[xX](\d+)', clause):
        literals.append((int(number), negated != '~'))
    if not literals:
        raise ValueError(f"No literals found in clause: {clause}")
    return literals


def parse_clauses(clause_spec: Union[str, Iterable[str]]) -> Tuple[List[List[Tuple[int, bool]]], int]:
    if isinstance(clause_spec, str):
        groups = re.findall(r'\(([^)]*)\)', clause_spec)
        clause_strings = [f"({group})" for group in groups] if groups else [clause_spec]
    else:
        clause_strings = list(clause_spec)

    if not clause_strings:
        raise ValueError("No clauses provided")

    clauses = [parse_clause(clause) for clause in clause_strings]
    n = max(var for clause in clauses for var, _ in clause)
    return clauses, n


def fibonacci_series(n: int, seed1: int, seed2: int) -> List[int]:
    if n <= 0:
        return []
    if n == 1:
        return [seed1]

    series = [seed1, seed2]
    while len(series) < n:
        series.append(series[-1] + series[-2])
    return series


def build_graph_from_spec(
    clause_spec,
    starting_weight: int = 100,
    weight_perm: Optional[List[int]] = None,
):
    """Python copy of tqbf_tournament_builder.hpp for visualization."""
    clauses, n = parse_clauses(clause_spec)
    m = len(clauses)

    if weight_perm is None:
        weights = fibonacci_series(NUM_GROUPS, starting_weight, starting_weight * 2)
    else:
        if len(weight_perm) != NUM_GROUPS:
            raise ValueError(f"weight_perm must have length {NUM_GROUPS}, got {len(weight_perm)}")
        weights = list(weight_perm)

    G = nx.DiGraph()
    C, D = "C", "D"
    L = [f"L{k}" for k in range(1, m + 1)]
    T = [f"T{i}" for i in range(1, n + 1)]
    F = [f"F{i}" for i in range(1, n + 1)]
    X = [f"X{i}" for i in range(1, n + 1)]
    G.add_nodes_from([C, D] + L + T + F + X)

    current_offset = [-1] * NUM_GROUPS

    def add_edge(u: str, v: str, group: int):
        # Match tmpl_add_edge: keep the first orientation and do not consume an
        # offset when the pair was already assigned.
        if u == v or G.has_edge(u, v) or G.has_edge(v, u):
            return
        group_index = NUM_GROUPS - group
        offset = current_offset[group_index]
        current_offset[group_index] -= 1
        G.add_edge(u, v, paper_group=group, group=group_index, off=offset,
                    weight=weights[group_index] + offset)

    # g1: C,D -> Fi,Ti. The insertion order alternates by variable parity.
    for i in range(1, n + 1):
        first, second = (C, D) if (i - 1) % 2 == 0 else (D, C)
        add_edge(first, f"F{i}", 1)
        add_edge(first, f"T{i}", 1)
        add_edge(second, f"F{i}", 1)
        add_edge(second, f"T{i}", 1)

    # g2: Fi,Ti -> Xi.
    for i in range(1, n + 1):
        add_edge(f"F{i}", f"X{i}", 2)
        add_edge(f"T{i}", f"X{i}", 2)

    # g3: literals in clause k -> Lk.
    for k, clause in enumerate(clauses, start=1):
        for var, positive in clause:
            add_edge(f"{'T' if positive else 'F'}{var}", f"L{k}", 3)

    # g4: Lk -> the opposite literal, or both literals when xi is absent.
    # The C++ builder uses the first occurrence when both polarities occur.
    for k, clause in enumerate(clauses, start=1):
        for i in range(1, n + 1):
            literal = next(((var, positive) for var, positive in clause if var == i), None)
            if literal is None:
                add_edge(f"L{k}", f"F{i}", 4)
                add_edge(f"L{k}", f"T{i}", 4)
            elif literal[1]:
                add_edge(f"L{k}", f"F{i}", 4)
            else:
                add_edge(f"L{k}", f"T{i}", 4)

    # g5: Xj -> Fi,Ti for i != j.
    for j in range(1, n + 1):
        for i in range(1, n + 1):
            if i != j:
                add_edge(f"X{j}", f"F{i}", 5)
                add_edge(f"X{j}", f"T{i}", 5)

    # g6: Xi -> Lk.
    for i in range(1, n + 1):
        for k in range(1, m + 1):
            add_edge(f"X{i}", f"L{k}", 6)

    # g7: Lk -> C and D -> Lk.
    for k in range(1, m + 1):
        add_edge(f"L{k}", C, 7)
    for k in range(1, m + 1):
        add_edge(D, f"L{k}", 7)

    # g8: Xi -> C,D.
    for i in range(1, n + 1):
        add_edge(f"X{i}", C, 8)
    for i in range(1, n + 1):
        add_edge(f"X{i}", D, 8)

    # g9a: Fi -> Ti; Fi -> Tj and Ti -> Fj for j > i.
    for i in range(1, n + 1):
        add_edge(f"F{i}", f"T{i}", 9)
        for j in range(i + 1, n + 1):
            add_edge(f"F{i}", f"T{j}", 9)
        for j in range(i + 1, n + 1):
            add_edge(f"T{i}", f"F{j}", 9)
    current_offset[NUM_GROUPS - 9] -= 10

    # g9b: Fi -> Fj for i < j.
    for i in range(1, n + 1):
        for j in range(i + 1, n + 1):
            add_edge(f"F{i}", f"F{j}", 9)
    current_offset[NUM_GROUPS - 9] -= 10

    # g9c: Ti -> Tj for i < j.
    for i in range(1, n + 1):
        for j in range(i + 1, n + 1):
            add_edge(f"T{i}", f"T{j}", 9)

    # g10: Xi -> Xj for i < j.
    for i in range(1, n + 1):
        for j in range(i + 1, n + 1):
            add_edge(f"X{i}", f"X{j}", 10)

    # g11: Li -> Lj for i < j.
    for i in range(1, m + 1):
        for j in range(i + 1, m + 1):
            add_edge(f"L{i}", f"L{j}", 11)

    # g12: C -> D.
    add_edge(C, D, 12)

    expected_edges = G.number_of_nodes() * (G.number_of_nodes() - 1) // 2
    if G.number_of_edges() != expected_edges:
        raise ValueError(f"Graph is not a tournament: expected {expected_edges} edges, found {G.number_of_edges()}")

    return G, n, clauses


def _infer_n_m_from_graph(G: nx.DiGraph) -> Tuple[int, int]:
    """
    Infer the number of variables n and the number of clauses m from the node labels
    of a graph produced by build_graph_from_spec.

    Nodes are expected to be of the form:
      - "C"
      - "Lk"   for clause nodes
      - "Ti"   for positive literal nodes
      - "Fi"   for negative literal nodes
      - "Xi"   for variable gadget nodes
    """
    n = 0
    m = 0
    for node in G.nodes():
        if not isinstance(node, str) or len(node) < 2 or not node[1:].isdigit():
            continue
        index = int(node[1:])
        if node[0] in ("T", "F", "X"):
            n = max(n, index)
        elif node[0] == "L":
            m = max(m, index)
    return n, m


def classify_edges_from_spec(G: nx.DiGraph) -> Dict[str, List[Tuple[str, str]]]:
    # """
    # Classify edges of the SAT tournament built by build_graph_from_spec
    # into the same eight relationship types used in the stand‑alone visualizer
    # (stablevoting_has_custom_visualization.py). :contentReference[oaicite:1]{index=1}
    #
    # The classification is purely by node labels, so it works even if:
    #   - some nodes have been removed, or
    #   - the edge weights / weight permutation have changed.
    #
    # Categories:
    #   * "C_to_TF"           : C -> Ti / Fi
    #   * "L_edges"           : all edges touching an Lk, except Xi -> Lk (which go to X_to_LC)
    #   * "TF_to_X"           : Ti / Fi -> Xi
    #   * "X_to_other_TF"     : Xi -> Tj / Fj, j != i
    #   * "X_to_Xforward"     : Xi -> Xj with i < j
    #   * "X_to_LC"           : Xi -> any Lk or C
    #   * "T_to_prior_TF"     : all remaining edges with source Ti and target T* or F*
    #   * "F_to_prior_TF_Ti"  : all remaining edges with source Fi and target T* or F*
    # """
    categories = {f"g{i}": [] for i in range(1, NUM_GROUPS + 1)}
    for u, v, data in G.edges(data=True):
        categories[f"g{data['paper_group']}"].append((u, v))
    for group in categories:
        categories[group].sort()
    return categories


def layout_left_mid_right(G: nx.DiGraph) -> Dict[str, Tuple[float, float]]:
    """
    Layout with:
      - C/D on the far left,
      - T/F/X columns in the middle (one column per variable index),
      - all Lk nodes stacked vertically in a single column to the RIGHT
        of the last T/F/X column.

    Columns are spaced evenly between C and the L column, so small n (e.g. n=2)
    doesn't produce a giant gap between x1 and x2.
    """
    n, m = _infer_n_m_from_graph(G)
    pos = {}
    x_left, x_right = 0.0, 10.0

    if "C" in G:
        pos["C"] = (x_left, 2.8)
    if "D" in G:
        pos["D"] = (x_left, 1.2)

    # Evenly spaced variable columns between C and L:
    #   columns at x_left + step, x_left + 2*step, ..., x_left + n*step
    #   L column stays at x_right.
    if n > 0:
        step = (x_right - x_left) / float(n + 1)
        # Place T/F/X nodes (only if present)
        for i in range(1, n + 1):
            x = x_left + i * step
            if f"T{i}" in G:
                pos[f"T{i}"] = (x, 3.2)
            if f"F{i}" in G:
                pos[f"F{i}"] = (x, 2.0)
            if f"X{i}" in G:
                pos[f"X{i}"] = (x, 0.7)

    # L nodes: single vertical column on the far right
    L_nodes = [f"L{k}" for k in range(1, m + 1) if f"L{k}" in G]
    if L_nodes:
        if len(L_nodes) == 1:
            y_positions = [2.0]
        else:
            y_top, y_bottom = 3.7, 0.3
            step = (y_top - y_bottom) / float(len(L_nodes) - 1)
            y_positions = [y_top - i * step for i in range(len(L_nodes))]
        for node, y in zip(L_nodes, y_positions):
            pos[node] = (x_right, y)

    return pos

# Return a dict of {category_name: edge_count}.
def edge_counts_summary(categories: Dict[str, List[Tuple[str, str]]]) -> Dict[str, int]:
    return {group: len(edges) for group, edges in categories.items()}


def render_clause_graph(
    clause_spec,
    removed_nodes: Optional[Iterable[str]] = None,
    starting_weight: int = 100,
    weight_perm: Optional[List[int]] = None,
    save_path: Optional[str] = "visualized_clause",
    show: bool = False,
):
    """
    High‑level helper: build the SAT tournament for the given clauses using
    build_graph_from_spec, optionally delete some nodes, and visualize the result
    using the structured layout / color scheme from stablevoting_has_custom_visualization.

    Parameters
    ----------
    clause_spec :
        Either a single string containing one or more parenthesized clauses, e.g.
            "(x1, ~x2, x3) (~x1, x2)"
        or an iterable of clause strings, e.g.
            ["(x1, ~x2, x3)", "(~x1, x2)"].

    removed_nodes :
        Iterable of node labels to remove before drawing, e.g.
        ["X1", "L2"].  Any labels not present in the graph are silently ignored.

    starting_weight, weight_perm :
        Passed straight through to build_graph_from_spec.  If you only care
        about visualization you can just use the defaults.

    save_path :
        If not None, save the figure as a PNG at this path (or filename).

    show :
        If True (default), call plt.show() at the end.  If you are scripting
        things and only care about saving, you can set show=False.

    Returns
    -------
    G :
        The (possibly node‑deleted) DiGraph that was drawn.
    cats :
        The edge‑category dictionary returned by classify_edges_from_spec.
    """
    G_full, n, clauses = build_graph_from_spec(
        clause_spec,
        starting_weight=starting_weight,
        weight_perm=weight_perm,
    )
    G = G_full.copy()

    if removed_nodes:
        G.remove_nodes_from([node for node in removed_nodes if node in G])

    categories = classify_edges_from_spec(G)
    counts = edge_counts_summary(categories)

    print(f"Formula: {compact_clause_repr(clause_spec)}")
    print(f"Variables: {n}")
    print("Edge counts by group:")
    for group in categories:
        print(f"  {group:4s}: {counts[group]}")
    print(f"Total edges shown: {G.number_of_edges()}")

    pos = layout_left_mid_right(G)
    fig, ax = plt.subplots(figsize=(max(14, 4 + 2.5 * max(1, n)), 7))

    C_nodes = [node for node in ["C"] if node in G]
    D_nodes = [node for node in ["D"] if node in G]
    L_nodes = [node for node in G if isinstance(node, str) and node.startswith("L")]
    TF_nodes = [node for node in G if isinstance(node, str) and node.startswith(("T", "F"))]
    X_nodes = [node for node in G if isinstance(node, str) and node.startswith("X")]

    if C_nodes:
        nx.draw_networkx_nodes(G, pos, nodelist=C_nodes, node_color="tab:cyan", node_size=240, edgecolors="black", ax=ax)
    if D_nodes:
        nx.draw_networkx_nodes(G, pos, nodelist=D_nodes, node_color="khaki", node_size=240, edgecolors="black", ax=ax)
    if L_nodes:
        nx.draw_networkx_nodes(G, pos, nodelist=L_nodes, node_color="tab:pink", node_size=240, edgecolors="black", ax=ax)
    if TF_nodes:
        nx.draw_networkx_nodes(G, pos, nodelist=TF_nodes, node_color="lightgray", node_size=220, edgecolors="black", ax=ax)
    if X_nodes:
        nx.draw_networkx_nodes(G, pos, nodelist=X_nodes, node_color="yellowgreen", node_size=220, edgecolors="black", ax=ax)

    nx.draw_networkx_labels(G, pos, font_size=8, font_weight="bold", ax=ax)

    colors = {
        "g1": "tab:blue", "g2": "tab:red", "g3": "tab:green", "g4": "yellowgreen",
        "g5": "tab:orange", "g6": "tab:brown", "g7": "tab:cyan", "g8": "teal",
        "g9": "tab:pink", "g10": "tab:purple", "g11": "tab:gray", "g12": "black",
    }
    styles = {
        "g1": "solid", "g2": "solid", "g3": "solid", "g4": "dashed",
        "g5": "dashed", "g6": "dotted", "g7": "solid", "g8": "dashed",
        "g9": (0, (1, 2)), "g10": "dotted", "g11": "dashed", "g12": "solid",
    }

    for group, edges in categories.items():
        if not edges:
            continue
        for i, edge in enumerate(edges):
            offset = ((i % 7) - 3) / 3.0
            nx.draw_networkx_edges(
                G, pos, edgelist=[edge], edge_color=colors[group], style=styles[group],
                width=0.9, alpha=0.9,
                arrows=True, arrowstyle='-|>', arrowsize=8,
                connectionstyle=f"arc3,rad={0.10 * offset}", ax=ax,
            )

    # legends
    descriptions = {
        "g1": "C,D → Ti,Fi",
        "g2": "Ti,Fi → Xi",
        "g3": "clause literal → Lk",
        "g4": "Lk → opposite/absent literal",
        "g5": "Xj → Ti,Fi (i≠j)",
        "g6": "Xi → Lk",
        "g7": "Lk → C; D → Lk",
        "g8": "Xi → C,D",
        "g9": "ordered T/F edges",
        "g10": "Xi → Xj",
        "g11": "Li → Lj",
        "g12": "C → D",
    }
    edge_handles = [
        Line2D([0], [0], color=colors[group], linestyle=styles[group], lw=1.0,
               label=f"{group}: {descriptions[group]}")
        for group in categories
    ]
    node_handles = [
        Patch(facecolor="tab:cyan", edgecolor="black", label="C"),
        Patch(facecolor="khaki", edgecolor="black", label="D"),
        Patch(facecolor="tab:pink", edgecolor="black", label="Lk"),
        Patch(facecolor="lightgray", edgecolor="black", label="Ti/Fi"),
        Patch(facecolor="yellowgreen", edgecolor="black", label="Xi"),
    ]

    first_legend = ax.legend(handles=edge_handles, loc="upper left", bbox_to_anchor=(-0.01, 1.02),#(0.01, 1.02),
                             frameon=False, title="Weight groups", fontsize=8, title_fontsize=10)
    ax.add_artist(first_legend)
    ax.legend(handles=node_handles, loc="upper right", bbox_to_anchor=(1.02, 1.02),
              frameon=False, title="Candidates", fontsize=8, title_fontsize=10)

    ax.set_title(f"Stable-Voting TQBF tournament for Φ = {compact_clause_repr(clause_spec)}", fontsize=13)
    ax.set_axis_off()
    plt.tight_layout()

    if save_path is not None:
        output = save_path if save_path.lower().endswith(".png") else save_path + ".png"
        plt.savefig(output, bbox_inches="tight")

    if show:
        plt.show()
    else:
        plt.close(fig)

    return G, categories


if __name__ == "__main__":
    img_prefix = "img/"
    # Single SAT example
    _, _ = render_clause_graph("(x1, x2),(x1,~x2),(~x1,x2),(~x1,~x2)", save_path=f"{img_prefix}basic_2sat_UNSAT_qbf")
    # UNSAT example:
    formula = ["(x1,x2,x3)","(~x1,x2,x3)","(x1,~x2,x3)","(x1,x2,~x3)","(~x1,~x2,x3)","(~x1,x2,~x3)","(x1,~x2,~x3)","(~x1,~x2,~x3)",]
    _, _ = render_clause_graph(formula, save_path=f"{img_prefix}basic_3sat_UNSAT_qbf")
    # qbf example
    formula = ["(~x3,x2)", "(x3,~x2)", "(x1,x4)", "(x1,~x4)", "(x1,x2)", "(x1,~x2)"]
    render_clause_graph(formula, save_path=f"{img_prefix}2sat_qbf_tournament")

    removed_nodes = ["X1", "L2"]
    removed_suffix = "_".join(removed_nodes)
    render_clause_graph(
        formula,
        save_path=f"{img_prefix}2sat_qbf_tournament_minus_{removed_suffix}",
        removed_nodes=removed_nodes,
    )
