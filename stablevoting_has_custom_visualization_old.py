# Recreate dynamic plotting pipeline and render with wider canvas + wrapped legends
import re
import networkx as nx
import matplotlib.pyplot as plt
from matplotlib.lines import Line2D
from matplotlib.patches import Patch

# from __future__ import annotations
import sys
from typing import Dict, List, Tuple, Iterable

# --- Parsing helpers ---
def parse_clause(clause_str):
    """
    Parse a clause string like "(x1, ~x2, ~x3)" into:
      - n: max variable index observed
      - polarity: dict {i: 'pos'|'neg'} for literals appearing in the clause
    """
    lits = re.findall(r'(~)?x(\d+)', clause_str.replace(" ", ""))
    if not lits:
        raise ValueError("No literals found. Expected format like '(x1, ~x2, x5)'.")
    idxs = [int(j) for _, j in lits]
    n = max(idxs)
    polarity = {int(j): ('neg' if s == '~' else 'pos') for s, j in lits}
    return n, polarity
# --- Graph construction from spec (dynamic in n) ---
def build_graph_from_spec(clause_str):
    n, polarity = parse_clause(clause_str)
    G = nx.DiGraph()
    C, L1 = "C", "L1"
    T = [f"T{i}" for i in range(1, n+1)]
    F = [f"F{i}" for i in range(1, n+1)]
    X = [f"X{i}" for i in range(1, n+1)]
    G.add_nodes_from([C, L1] + T + F + X)
    # 1) C -> Ti, Fi
    for i in range(1, n+1):
        G.add_edge(C, f"T{i}")
        G.add_edge(C, f"F{i}")
    # 2) L1 edges driven by clause + L1 -> C
    G.add_edge(L1, C)
    for i in range(1, n+1):
        sense = polarity.get(i, None)
        if sense == 'pos':
            G.add_edge(f"T{i}", L1)
            G.add_edge(L1, f"F{i}")
        elif sense == 'neg':
            G.add_edge(f"F{i}", L1)
            G.add_edge(L1, f"T{i}")
    # 3) Xi edges
    for i in range(1, n+1):
        Xi = f"X{i}"
        # to Tj, Fj for j != i
        for j in range(1, n+1):
            if j != i:
                G.add_edge(Xi, f"T{j}")
                G.add_edge(Xi, f"F{j}")
        # Xi -> Xj for i < j (forward)
        for j in range(i+1, n+1):
            G.add_edge(Xi, f"X{j}")
        # to L1 and C
        G.add_edge(Xi, L1)
        G.add_edge(Xi, C)
    # 4) Ti edges: to prior {Tj, Fj} for j < i, and to Xi
    for i in range(1, n+1):
        Ti = f"T{i}"
        for j in range(1, i):
            G.add_edge(Ti, f"T{j}")
            G.add_edge(Ti, f"F{j}")
        G.add_edge(Ti, f"X{i}")
    # 5) Fi edges: to prior {Tj, Fj} for j < i, and to Ti and Xi
    for i in range(1, n+1):
        Fi = f"F{i}"
        for j in range(1, i):
            G.add_edge(Fi, f"T{j}")
            G.add_edge(Fi, f"F{j}")
        G.add_edge(Fi, f"T{i}")
        G.add_edge(Fi, f"X{i}")
    return G, n, polarity
# --- Classification of edges by relationship type (dynamic in n) ---
def classify_edges_from_spec(G, n, polarity):
    cat = {
        "C_to_TF": [],
        "L_edges": [],
        "TF_to_X": [],
        "X_to_other_TF": [],
        "X_to_Xforward": [],    # i < j (forward)
        "X_to_LC": [],
        "T_to_prior_TF": [],
        "F_to_prior_TF_Ti": [],
    }
    # C -> TF
    for i in range(1, n+1):
        if G.has_edge("C", f"T{i}"): cat["C_to_TF"].append(("C", f"T{i}"))
        if G.has_edge("C", f"F{i}"): cat["C_to_TF"].append(("C", f"F{i}"))
    # L edges (clause-driven) + L1->C
    if G.has_edge("L1", "C"): cat["L_edges"].append(("L1", "C"))
    for i in range(1, n+1):
        sense = polarity.get(i, None)
        if sense == 'pos':
            if G.has_edge(f"T{i}", "L1"): cat["L_edges"].append((f"T{i}", "L1"))
            if G.has_edge("L1", f"F{i}"): cat["L_edges"].append(("L1", f"F{i}"))
        elif sense == 'neg':
            if G.has_edge(f"F{i}", "L1"): cat["L_edges"].append((f"F{i}", "L1"))
            if G.has_edge("L1", f"T{i}"): cat["L_edges"].append(("L1", f"T{i}"))
    # TF -> Xi
    for i in range(1, n+1):
        if G.has_edge(f"T{i}", f"X{i}"): cat["TF_to_X"].append((f"T{i}", f"X{i}"))
        if G.has_edge(f"F{i}", f"X{i}"): cat["TF_to_X"].append((f"F{i}", f"X{i}"))
    # Xi -> Tj/Fj (j != i)
    for i in range(1, n+1):
        for j in range(1, n+1):
            if j != i:
                if G.has_edge(f"X{i}", f"T{j}"): cat["X_to_other_TF"].append((f"X{i}", f"T{j}"))
                if G.has_edge(f"X{i}", f"F{j}"): cat["X_to_other_TF"].append((f"X{i}", f"F{j}"))
    # Xi -> Xj, i < j (forward)
    for i in range(1, n+1):
        for j in range(i+1, n+1):
            if G.has_edge(f"X{i}", f"X{j}"): cat["X_to_Xforward"].append((f"X{i}", f"X{j}"))
    # Xi -> {L1, C}
    for i in range(1, n+1):
        if G.has_edge(f"X{i}", "L1"): cat["X_to_LC"].append((f"X{i}", "L1"))
        if G.has_edge(f"X{i}", "C"):  cat["X_to_LC"].append((f"X{i}", "C"))
    # Ti -> prior T/F
    for i in range(2, n+1):
        for j in range(1, i):
            if G.has_edge(f"T{i}", f"T{j}"): cat["T_to_prior_TF"].append((f"T{i}", f"T{j}"))
            if G.has_edge(f"T{i}", f"F{j}"): cat["T_to_prior_TF"].append((f"T{i}", f"F{j}"))
    # Fi -> prior T/F and Fi -> Ti
    for i in range(1, n+1):
        if G.has_edge(f"F{i}", f"T{i}"): cat["F_to_prior_TF_Ti"].append((f"F{i}", f"T{i}"))
        for j in range(1, i):
            if G.has_edge(f"F{i}", f"T{j}"): cat["F_to_prior_TF_Ti"].append((f"F{i}", f"T{j}"))
            if G.has_edge(f"F{i}", f"F{j}"): cat["F_to_prior_TF_Ti"].append((f"F{i}", f"F{j}"))
    return cat
# --- Dynamic layout (C left, L1 right, TF middle stacked, Xi below) ---
def layout_left_mid_right(n):
    pos = {}
    x_left, x_right = 0.0, 10.0  # widen canvas
    # even spacing across middle region
    xs = [2.0 + (i-1) * ((x_right - 2.0 - 2.0) / max(1, n-1)) for i in range(1, n+1)]
    y_top, y_mid, y_low = 3.0, 2.0, 1.0
    pos["C"] = (x_left, y_mid)
    pos["L1"] = (x_right, y_mid)
    for i, x in enumerate(xs, start=1):
        pos[f"T{i}"] = (x, y_top)
        pos[f"F{i}"] = (x, y_mid)
        pos[f"X{i}"] = (x, y_low)
    return pos
# --- Renderer with wider figure + wrapped legends ---
def render(clause_str):
    G, n, polarity = build_graph_from_spec(clause_str)
    cats = classify_edges_from_spec(G, n, polarity)
    pos = layout_left_mid_right(n)
    fig, ax = plt.subplots(figsize=(max(14, 4 + 2.5*n), 7))

    # Font sizes
    node_text_size = 8
    legend_text_size = 9
    legend_title_text_size = 11
    title_text_size = 13

    # Node sizes
    base_big, base_small = 250, 230
    size_big = max(90, base_big - 4*n)
    size_small = max(85, base_small - 4*n)
    # Draw nodes
    TF_nodes = [f"T{i}" for i in range(1, n+1)] + [f"F{i}" for i in range(1, n+1)]
    X_nodes  = [f"X{i}" for i in range(1, n+1)]
    nx.draw_networkx_nodes(G, pos, nodelist=["C"],  node_color="tab:cyan",  node_size=size_big, edgecolors="black", linewidths=0.9, ax=ax)
    nx.draw_networkx_nodes(G, pos, nodelist=["L1"], node_color="tab:pink",  node_size=size_big, edgecolors="black", linewidths=0.9, ax=ax)
    nx.draw_networkx_nodes(G, pos, nodelist=TF_nodes, node_color="lightgray", node_size=size_small, edgecolors="black", linewidths=0.8, ax=ax)
    nx.draw_networkx_nodes(G, pos, nodelist=X_nodes,  node_color="yellowgreen", node_size=size_small, edgecolors="black", linewidths=0.8, ax=ax)
    nx.draw_networkx_labels(G, pos, font_size=node_text_size, font_weight="bold", ax=ax)  # Half the previous size
    def draw_group(edgelist, color, style="solid", width=0.9, base_rad=0.08, spread=6, arrowsize=9, alpha=0.95):
        if not edgelist:
            return
        offsets = [((k % spread) - (spread-1)/2) / ((spread-1)/2) for k in range(len(edgelist))]
        for off, e in zip(offsets, edgelist):
            rad = base_rad * off
            nx.draw_networkx_edges(
                G, pos, edgelist=[e], edge_color=color, style=style, width=width, alpha=alpha,
                arrows=True, arrowstyle='-|>', arrowsize=arrowsize, connectionstyle=f'arc3,rad={rad}', ax=ax
            )
    # Draw edge groups
    # arrowsize = 8
    draw_group(cats["X_to_Xforward"],      color="tab:purple", style="dotted", width=0.9, base_rad=0.22, spread=9, arrowsize=8)
    draw_group(cats["T_to_prior_TF"],      color="tab:pink",   style="solid",  width=0.9, base_rad=0.18, spread=9, arrowsize=8)
    draw_group(cats["F_to_prior_TF_Ti"],   color="tab:gray",   style=(0,(1,2)),width=0.9, base_rad=0.18, spread=9, arrowsize=8)
    draw_group(cats["C_to_TF"],            color="tab:blue",   width=0.9, base_rad=0.10, spread=5, arrowsize=9)
    draw_group(cats["L_edges"],            color="tab:green",  width=0.9, base_rad=0.10, spread=5, arrowsize=9)
    draw_group(cats["TF_to_X"],            color="tab:red",    width=0.9, base_rad=0.08, spread=7, arrowsize=8)
    draw_group(cats["X_to_other_TF"],      color="tab:orange", style="dashed", width=0.9, base_rad=0.10, spread=7, arrowsize=8)
    draw_group(cats["X_to_LC"],            color="tab:brown",  style=(0,(4,2)),width=0.9, base_rad=0.12, spread=7, arrowsize=8)
    # Legends with adjusted font sizes and spacing
    edge_handles = [
        Line2D([0],[0], color="tab:blue",   lw=0.9, label="C → Ti/Fi"),
        Line2D([0],[0], color="tab:green",  lw=0.9, label="L1 edges (from clause)"),
        Line2D([0],[0], color="tab:red",    lw=0.9, label="Ti/Fi → Xi"),
        Line2D([0],[0], color="tab:orange", lw=0.9, linestyle="dashed", label="Xi → Tj/Fj (j≠i)"),
        Line2D([0],[0], color="tab:purple", lw=0.9, linestyle="dotted", label="Xi → Xj (i<j)"),
        Line2D([0],[0], color="tab:brown",  lw=0.9, linestyle=(0,(4,2)), label="Xi → {L1,C}"),
        Line2D([0],[0], color="tab:pink",   lw=0.9, label="Ti → prior T/F"),
        Line2D([0],[0], color="tab:gray",   lw=0.9, linestyle=(0,(1,2)), label="Fi → prior T/F, Ti"),
    ]
    node_handles = [
        Patch(facecolor="tab:cyan",  edgecolor="black", label="C node"),
        Patch(facecolor="tab:pink",  edgecolor="black", label="L node"),
        Patch(facecolor="lightgray", edgecolor="black", label="T/F nodes"),
        Patch(facecolor="yellowgreen", edgecolor="black", label="X nodes"),
    ]

    first_legend = ax.legend(handles=edge_handles, loc="upper left",
                             bbox_to_anchor=(0.01, 1.02), ncol=1, frameon=False,
                             title="Edge Relationship Types", fontsize=legend_text_size, title_fontsize=legend_title_text_size, handlelength=2.5, columnspacing=1.5)
    ax.add_artist(first_legend)
    ax.legend(handles=node_handles, loc="upper right",
              bbox_to_anchor=(0.99, 1.02), ncol=1, frameon=False,
              title="Node Types", fontsize=legend_text_size, title_fontsize=legend_title_text_size, handlelength=2.5, columnspacing=1.5)
    name = f"Dynamic Graph from Clause {clause_str}"
    ax.set_title(name, fontsize=title_text_size)
    ax.set_axis_off()
    plt.tight_layout()

    # Save the plot as a PNG file
    # plt.show()
    plt.savefig(f"{name}.png")


# ---------------------------
# Utilities
# ---------------------------
def edge_counts_summary(categories: Dict[str, List[Tuple[str, str]]]) -> Dict[str, int]:
    """Return a dict of {category_name: edge_count}."""
    return {k: len(v) for k, v in categories.items()}


#Main
if __name__ == "__main__":
    default_clause = ["(x1, x2, x3)","(~x1, x2, x3)","(x1, ~x2, x3)","(x1, x2, ~x3)","(~x1, ~x2, x3)","(~x1, x2, ~x3)","(x1, ~x2, ~x3)","(~x1, ~x2, ~x3)"]
    clause = sys.argv[1] if len(sys.argv) > 1 else "(x1, ~x2, ~x3)"
    # --- Plot the requested clause with the wider layout and wrapped legends ---
    render(clause)

    G, n, polarity = build_graph_from_spec(clause)
    cats = classify_edges_from_spec(G, n, polarity)
    counts = edge_counts_summary(cats)

    print(f"Clause: {clause}")
    print(f"Variables (n): {n}")
    print(f"Polarity: {polarity}")
    print("\nEdge counts by category:")
    for k in sorted(counts.keys()):
        print(f"  {k:20s}: {counts[k]}")
    print(f"\nTotal edges: {G.number_of_edges()}")
