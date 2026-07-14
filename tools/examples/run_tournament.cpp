// g++ -std=c++20 -O3 -march=native -DNDEBUG run_tournament.cpp -o run_tourn && run_tourn
#include <bits/stdc++.h>
using namespace std;

#include "../../include/fast_utils.hpp"
#include "../../include/graph_template.hpp"
#include "../../include/sv_fast.hpp"
#include "../../include/printers.hpp"

void init_template(GraphTemplate& G, vector<string> &names){
    G.N = names.size();
    G.names = names;
    G.dir.assign(G.N * G.N, 0);  //direction of edges
    G.group.assign(G.N * G.N, 0);//groups are used in the QBF gadget and are heavily integrated
    G.off.assign(G.N * G.N, 0);  //weight/margin of edges
    G.full_mask = (1ull << G.N) - 1ull;//max number of candidates: 63
}

GraphTemplate getBasicExample(){
    GraphTemplate G;
    vector<string> names = {"A", "B", "C", "D"};
    init_template(G, names);

    auto edge = [&](int a, int b, int margin) {
        G.dir[G.IDX(a, b)] = 1;
        G.off[G.IDX(a, b)] = margin;
    };
    edge(0, 1, 6); edge(1, 2, 5); edge(2, 0, 4);
    edge(0, 3, 3); edge(3, 1, 2); edge(2, 3, 1);

    return G;
}

// also see extras/diagnostics/test_sv_ssv_distinguishing_instance.cpp
GraphTemplate getSVSSVDistinguishingExample(){
    GraphTemplate G;
    vector<string> names = {"a", "b", "c", "d", "e", "f", "g"};
    init_template(G, names);

    auto edge = [&](int a, int b, int margin) {
        G.dir[G.IDX(a, b)] = 1;
        G.off[G.IDX(a, b)] = margin;
    };

    edge(3, 6,  1);/* d->g */ edge(6, 0,  2);/* g->a */ edge(3, 4,  3);/* d->e */
    edge(5, 3,  4);/* f->d */ edge(3, 2,  5);/* d->c */ edge(2, 4,  6);/* c->e */
    edge(5, 0,  7);/* f->a */ edge(1, 3,  8);/* b->d */ edge(3, 0,  9);/* d->a */
    edge(5, 1, 10);/* f->b */ edge(0, 2, 11);/* a->c */ edge(1, 0, 12);/* b->a */
    edge(4, 1, 13);/* e->b */ edge(2, 1, 14);/* c->b */ edge(1, 6, 15);/* b->g */
    edge(6, 2, 16);/* g->c */ edge(5, 6, 17);/* f->g */ edge(2, 5, 18);/* c->f */
    edge(4, 5, 19);/* e->f */ edge(0, 4, 20);/* a->e */ edge(6, 4, 21);/* g->e */
    cout << "=================================================================================\n";
    cout << "----- Note: running SV vs SSV distinguishing instance" << '\n';
    cout << "-------- Expected Winner: a (SSV), b (SV)\n";
    cout << "-------- Compile with flag -DSV_CHECK_DEFEATS=1 for SV (SSV is default)\n";
    cout << "=================================================================================\n";

    return G;
}

int main() {
    // GraphTemplate G = getBasicExample();
    GraphTemplate G = getSVSSVDistinguishingExample();

    int W[1] = {0};//weight groups, we only have 1 since it's not a QBF gadget
    SVFast solver(G);
    solver.reset_epoch(W);//set up the DP tables

    // visualize
    // cout << "--- Tournament Margin Matrix (Row defeats Column) ---\n";
    print_margin_matrix(G, W, cout, 4, true, true);
    cout << "Mode: " << (SVFast::CHECK_DEFEATS ? "SV" : "SSV") << '\n';

    int winner = solver.solve_winner(G.full_mask);
    cout << "Winner: " << G.names[winner] << '\n';

    if(winner >= 0){
        // elimination order
        vector<int> elim;
        solver.reconstruct_standard(G.full_mask, winner, elim);
        cout << "Elimination Order: " << get_elim_order_string(elim, G) << "\n";
    }
}
