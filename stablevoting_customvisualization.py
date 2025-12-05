# hopefully real plotting
import os, re, random
import networkx as nx
from typing import Dict, Tuple, Set, List, Optional, Iterable, Union
import matplotlib.pyplot as plt
from matplotlib.lines import Line2D
from matplotlib.patches import Patch

from stablevoting import build_graph_from_spec #unsat_clauses

# helpful printer
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
        if groups:
            parts = [f"({g.replace(' ', '')})" for g in groups]
            clause_str = " ∧ ".join(parts)
        else:
            # No parentheses structure; just compact whitespace
            clause_str = re.sub(r'\s+', '', clause_spec.strip())
    else:
        parts = []
        for c in clause_spec:
            c_str = str(c).strip()
            c_str = re.sub(r'\s+', '', c_str)
            parts.append(c_str)
        clause_str = " ∧ ".join(parts)

    clause_str = clause_str.strip()
    if len(clause_str) > max_len:
        return clause_str[: max_len - 3] + "..."
    return clause_str


# --- Structured SAT-graph visualization (Ethan's fancy drawing) ---

def _infer_n_m_from_graph(G: nx.DiGraph) -> tuple[int, int]:
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
    n = 0  # max index among T/F/X nodes
    m = 0  # max index among L nodes
    for v in G.nodes():
        if not isinstance(v, str) or len(v) < 2:
            continue
        prefix, suffix = v[0], v[1:]
        if not suffix.isdigit():
            continue
        idx = int(suffix)
        if prefix in ("T", "F", "X"):
            n = max(n, idx)
        elif prefix == "L":
            m = max(m, idx)
    return n, m


def classify_edges_from_spec(G: nx.DiGraph) -> dict[str, list[tuple[str, str]]]:
    """
    Classify edges of the SAT tournament built by build_graph_from_spec
    into the same eight relationship types used in the stand‑alone visualizer
    (stablevoting_has_custom_visualization.py). :contentReference[oaicite:1]{index=1}

    The classification is purely by node labels, so it works even if:
      - some nodes have been removed, or
      - the edge weights / weight permutation have changed.

    Categories:
      * "C_to_TF"           : C -> Ti / Fi
      * "L_edges"           : all edges touching an Lk, except Xi -> Lk (which go to X_to_LC)
      * "TF_to_X"           : Ti / Fi -> Xi
      * "X_to_other_TF"     : Xi -> Tj / Fj, j != i
      * "X_to_Xforward"     : Xi -> Xj with i < j
      * "X_to_LC"           : Xi -> any Lk or C
      * "T_to_prior_TF"     : all remaining edges with source Ti and target T* or F*
      * "F_to_prior_TF_Ti"  : all remaining edges with source Fi and target T* or F*
    """
    cats: dict[str, set[tuple[str, str]]] = {
        "C_to_TF": set(),
        "L_edges": set(),
        "TF_to_X": set(),
        "X_to_other_TF": set(),
        "X_to_Xforward": set(),
        "X_to_LC": set(),
        "T_to_prior_TF": set(),
        "F_to_prior_TF_Ti": set(),
    }

    def _is_T(node: str) -> bool:
        return isinstance(node, str) and node.startswith("T") and node[1:].isdigit()

    def _is_F(node: str) -> bool:
        return isinstance(node, str) and node.startswith("F") and node[1:].isdigit()

    def _is_X(node: str) -> bool:
        return isinstance(node, str) and node.startswith("X") and node[1:].isdigit()

    def _is_L(node: str) -> bool:
        return isinstance(node, str) and node.startswith("L") and node[1:].isdigit()

    for u, v in G.edges():
        assigned = None

        # 1) C -> Ti / Fi
        if u == "C" and (_is_T(v) or _is_F(v)):
            assigned = "C_to_TF"

        # 2) Xi -> {Lk, C}
        elif _is_X(u) and (v == "C" or _is_L(v)):
            assigned = "X_to_LC"

        # 3) Ti / Fi -> Xi  (same index in the construction)
        elif (_is_T(u) or _is_F(u)) and _is_X(v):
            assigned = "TF_to_X"

        # 4) Xi -> Tj/Fj  (j != i in the construction)
        elif _is_X(u) and (_is_T(v) or _is_F(v)):
            assigned = "X_to_other_TF"

        # 5) Xi -> Xj, i < j
        elif _is_X(u) and _is_X(v):
            try:
                iu, iv = int(u[1:]), int(v[1:])
            except ValueError:
                iu = iv = 0
            if iu < iv:
                assigned = "X_to_Xforward"

        # 6) Anything touching an Lk (except Xi -> Lk, already caught above)
        elif _is_L(u) or _is_L(v):
            assigned = "L_edges"

        # 7) Remaining T* -> T*/F* edges
        elif _is_T(u) and (_is_T(v) or _is_F(v)):
            assigned = "T_to_prior_TF"

        # 8) Remaining F* -> T*/F* edges
        elif _is_F(u) and (_is_T(v) or _is_F(v)):
            assigned = "F_to_prior_TF_Ti"

        if assigned is not None:
            cats[assigned].add((u, v))

    # Convert sets to sorted lists for deterministic output
    return {k: sorted(list(v)) for k, v in cats.items()}

def layout_left_mid_right(G: nx.DiGraph) -> Dict[str, Tuple[float, float]]:
    """
    Layout with:
      - C on the far left,
      - T/F/X columns in the middle (one column per variable index),
      - all Lk nodes stacked vertically in a single column to the RIGHT
        of the last T/F/X column.

    Columns are spaced evenly between C and the L column, so small n (e.g. n=2)
    doesn't produce a giant gap between x1 and x2.
    """
    n, m = _infer_n_m_from_graph(G)
    pos: Dict[str, Tuple[float, float]] = {}

    x_left, x_right = 0.0, 10.0  # C at x_left, L's at x_right

    # Y coordinates for T/F/X rows
    y_T = 3.0
    y_F = 2.0
    y_X = 1.0

    # C node
    if "C" in G:
        pos["C"] = (x_left, y_F)

    # Evenly spaced variable columns between C and L:
    #   columns at x_left + step, x_left + 2*step, ..., x_left + n*step
    #   L column stays at x_right.
    xs_var: List[float] = []
    if n > 0:
        step = (x_right - x_left) / float(n + 1)
        xs_var = [x_left + i * step for i in range(1, n + 1)]

    # Place T/F/X nodes (only if present)
    for i, x in enumerate(xs_var, start=1):
        T_i, F_i, X_i = f"T{i}", f"F{i}", f"X{i}"
        if T_i in G:
            pos[T_i] = (x, y_T)
        if F_i in G:
            pos[F_i] = (x, y_F)
        if X_i in G:
            pos[X_i] = (x, y_X)

    # L nodes: single vertical column on the far right
    L_nodes_ordered = [f"L{k}" for k in range(1, m + 1) if f"L{k}" in G]
    if L_nodes_ordered:
        x_L = x_right
        # Spread from just above T down to just below X
        y_top_L = y_T + 0.5     # 3.5
        y_bottom_L = y_X - 0.5  # 0.5

        if len(L_nodes_ordered) == 1:
            y_positions = [(y_top_L + y_bottom_L) / 2.0]
        else:
            span = y_top_L - y_bottom_L
            step_L = span / float(len(L_nodes_ordered) - 1)
            y_positions = [y_top_L - i * step_L for i in range(len(L_nodes_ordered))]

        for node, y in zip(L_nodes_ordered, y_positions):
            pos[node] = (x_L, y)

    return pos



def edge_counts_summary(categories: dict[str, list[tuple[str, str]]]) -> dict[str, int]:
    """Return a dict of {category_name: edge_count}."""
    return {k: len(v) for k, v in categories.items()}


def render_clause_graph(
    clause_spec,
    removed_nodes: Optional[Iterable[str]] = None,
    starting_weight: int = 1000,
    weight_perm: Optional[list[int]] = None,
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
    # Build the full graph from the SAT reduction
    G_full, n, clause_polarities = build_graph_from_spec(
        clause_spec,
        starting_weight=starting_weight,
        weight_perm=weight_perm,
    )

    G = G_full.copy()

    # Remove any requested nodes
    if removed_nodes:
        to_remove = [v for v in removed_nodes if v in G]
        G.remove_nodes_from(to_remove)

    # Classify edges
    cats = classify_edges_from_spec(G)
    counts = edge_counts_summary(cats)

    # --- print the same diagnostic info as in the original visualizer ---
    print(f"Clause: {clause_spec}")
    print(f"Variables (n): {n}")
    print(f"Polarity: {clause_polarities}")
    print("\nEdge counts by category:")
    for k in sorted(counts.keys()):
        print(f"  {k:20s}: {counts[k]}")
    print(f"\nTotal edges: {G.number_of_edges()}")


    # build layout
    n, _ = _infer_n_m_from_graph(G)
    pos = layout_left_mid_right(G)

    # --- Figure / axes setup (ported from stand‑alone visualizer) ---
    fig, ax = plt.subplots(figsize=(max(14, 4 + 2.5 * max(1, n)), 7))

    # Font sizes
    node_text_size = 8
    legend_text_size = 9
    legend_title_text_size = 11
    title_text_size = 13

    # Node sizes (shrink a bit as n grows)
    base_big, base_small = 250, 230
    size_big = max(90, base_big - 4 * max(1, n))
    size_small = max(85, base_small - 4 * max(1, n))

    # Identify node groups that are actually present
    TF_nodes = [v for v in G.nodes()
                if isinstance(v, str) and (v.startswith("T") or v.startswith("F"))]
    X_nodes  = [v for v in G.nodes()
                if isinstance(v, str) and v.startswith("X")]
    L_nodes  = [v for v in G.nodes()
                if isinstance(v, str) and v.startswith("L")]

    # Draw nodes
    if "C" in G:
        nx.draw_networkx_nodes(
            G, pos, nodelist=["C"],
            node_color="tab:cyan",
            node_size=size_big, edgecolors="black", linewidths=0.9, ax=ax,
        )
    if L_nodes:
        nx.draw_networkx_nodes(
            G, pos, nodelist=L_nodes,
            node_color="tab:pink",
            node_size=size_big, edgecolors="black", linewidths=0.9, ax=ax,
        )
    if TF_nodes:
        nx.draw_networkx_nodes(
            G, pos, nodelist=TF_nodes,
            node_color="lightgray",
            node_size=size_small, edgecolors="black", linewidths=0.8, ax=ax,
        )
    if X_nodes:
        nx.draw_networkx_nodes(
            G, pos, nodelist=X_nodes,
            node_color="yellowgreen",
            node_size=size_small, edgecolors="black", linewidths=0.8, ax=ax,
        )

    nx.draw_networkx_labels(G, pos, font_size=node_text_size, font_weight="bold", ax=ax)

    def draw_group(edgelist, color, style="solid", width=0.9,
                   base_rad=0.08, spread=6, arrowsize=9, alpha=0.95):
        if not edgelist:
            return
        offsets = [((k % spread) - (spread - 1) / 2)
                   / max(1.0, (spread - 1) / 2)
                   for k in range(len(edgelist))]
        for off, e in zip(offsets, edgelist):
            rad = base_rad * off
            nx.draw_networkx_edges(
                G, pos, edgelist=[e], edge_color=color, style=style,
                width=width, alpha=alpha,
                arrows=True, arrowstyle='-|>', arrowsize=arrowsize,
                connectionstyle=f'arc3,rad={rad}', ax=ax,
            )

    # Draw edge groups (same colors/linestyles as in the original visualizer)
    draw_group(cats["X_to_Xforward"],      color="tab:purple", style="dotted",
               width=0.9, base_rad=0.22, spread=9, arrowsize=8)
    draw_group(cats["T_to_prior_TF"],      color="tab:pink",   style="solid",
               width=0.9, base_rad=0.18, spread=9, arrowsize=8)
    draw_group(cats["F_to_prior_TF_Ti"],   color="tab:gray",   style=(0, (1, 2)),
               width=0.9, base_rad=0.18, spread=9, arrowsize=8)
    draw_group(cats["C_to_TF"],            color="tab:blue",
               width=0.9, base_rad=0.10, spread=5, arrowsize=9)
    draw_group(cats["L_edges"],            color="tab:green",
               width=0.9, base_rad=0.10, spread=5, arrowsize=9)
    draw_group(cats["TF_to_X"],            color="tab:red",
               width=0.9, base_rad=0.08, spread=7, arrowsize=8)
    draw_group(cats["X_to_other_TF"],      color="tab:orange", style="dashed",
               width=0.9, base_rad=0.10, spread=7, arrowsize=8)
    draw_group(cats["X_to_LC"],            color="tab:brown",  style=(0, (4, 2)),
               width=0.9, base_rad=0.12, spread=7, arrowsize=8)

    # Legends
    edge_handles = [
        Line2D([0], [0], color="tab:blue",   lw=0.9, label="C → Ti/Fi"),
        Line2D([0], [0], color="tab:green",  lw=0.9, label="Lk edges (clauses)"),
        Line2D([0], [0], color="tab:red",    lw=0.9, label="Ti/Fi → Xi"),
        Line2D([0], [0], color="tab:orange", lw=0.9, linestyle="dashed",
               label="Xi → Tj/Fj (j≠i)"),
        Line2D([0], [0], color="tab:purple", lw=0.9, linestyle="dotted",
               label="Xi → Xj (i<j)"),
        Line2D([0], [0], color="tab:brown",  lw=0.9, linestyle=(0, (4, 2)),
               label="Xi → {Lk,C}"),
        Line2D([0], [0], color="tab:pink",   lw=0.9, label="Ti → T/F edges"),
        Line2D([0], [0], color="tab:gray",   lw=0.9, linestyle=(0, (1, 2)),
               label="Fi → T/F edges"),
    ]
    node_handles = [
        Patch(facecolor="tab:cyan",  edgecolor="black", label="C node"),
        Patch(facecolor="tab:pink",  edgecolor="black", label="Lk nodes"),
        Patch(facecolor="lightgray", edgecolor="black", label="T/F nodes"),
        Patch(facecolor="yellowgreen", edgecolor="black", label="X nodes"),
    ]

    first_legend = ax.legend(
        handles=edge_handles, loc="upper left",
        bbox_to_anchor=(0.01, 1.02), ncol=1, frameon=False,
        title="Edge Relationship Types",
        fontsize=legend_text_size, title_fontsize=legend_title_text_size,
        handlelength=2.5, columnspacing=1.5,
    )
    ax.add_artist(first_legend)

    ax.legend(
        handles=node_handles, loc="upper right",
        bbox_to_anchor=(1.02, 1.02), # push slightly right + higher to avoid L's (original: (0.99, 1.02))
        ncol=1, frameon=False, title="Node Types",
        fontsize=legend_text_size, title_fontsize=legend_title_text_size,
        handlelength=2.5, columnspacing=1.5,
    )

    # Title
    # --- Title with filename + compact formula ---
    clause_str_compact = compact_clause_repr(clause_spec)
    title = f"Stable‑Voting SAT gadget for Φ = {clause_str_compact}"
    ax.set_title(title, fontsize=title_text_size)
    ax.set_axis_off()
    plt.tight_layout()

    if save_path is not None:
        plt.savefig(f"{save_path}.png", bbox_inches="tight")

    if show:
        plt.show()
    else:
        plt.close(fig)

    return G, cats

if __name__ == "__main__":
    # Single SAT example
    _, _ = render_clause_graph("(x1, x2),(x1,~x2),(~x1,x2),(~x1,~x2)", save_path="basic_2sat_UNSAT")
    # UNSAT example:
    clause = ["(x1,x2,x3)","(~x1,x2,x3)","(x1,~x2,x3)","(x1,x2,~x3)","(~x1,~x2,x3)","(~x1,x2,~x3)","(x1,~x2,~x3)","(~x1,~x2,~x3)",]
    _, _ = render_clause_graph(clause, save_path="basic_3sat_UNSAT")



    removed_nodes=["X1", "L2"]
    _, _ = render_clause_graph(clause,
        save_path=f"basic_3sat_UNSAT_minus{"_".join(removed_nodes)}",
        removed_nodes=removed_nodes,   # any node labels; missing ones are ignored
    )
