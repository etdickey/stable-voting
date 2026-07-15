# Fast Stable Voting and Simple Stable Voting

This repository contains a C++20 implementation of **Stable Voting (SV)** and **Simple Stable Voting (SSV)**, together with the tournament construction used in our reduction from quantified Boolean formulas. The solver can also be used directly on any complete weighted tournament; the QBF construction is only one application.

## Table of Contents

* [Repository layout](#repository-layout)
* [Build and run](#build-and-run)
* [QBF input convention](#qbf-input-convention)
* [Running an arbitrary tournament](#running-an-arbitrary-tournament)
* [Benchmark against `pref_voting`](#benchmark-against-pref_voting)
  * [Reference benchmark result](#reference-benchmark-result)
* [Candidate limit](#candidate-limit)
* [Visualization](#visualization)

## Repository layout

```text
search_weight_orders.cpp                   searches permutations of the 12 weight groups
test_qbf_reduction.cpp                     fixed-weight reduction tests and diagnostics
include/                                   shared C++ headers
benchmark/benchmark_pref_voting.py         comparison with the published pref_voting code
benchmark/benchmark_cpp.cpp                helper compiled automatically by the benchmark
tools/stablevoting_customvisualization.py  visual plotter for the gadget
examples/run_tournament.cpp                small non-QBF example
extras/diagnostics/                        occasional diagnostic programs
extras/legacy/                             earlier Python prototype
```

The two primary C++ programs remain in the repository root. Files under `extras/` are not needed for the main experiments.

## Build and run

A C++20 compiler is required. The default build runs SSV; define `SV_CHECK_DEFEATS=1` to run SV.

```bash
# Simple Stable Voting
g++ -std=c++20 -O3 -DNDEBUG -Iinclude \
    -DSV_CHECK_DEFEATS=0 test_qbf_reduction.cpp -o test_qbf_ssv

# Stable Voting
g++ -std=c++20 -O3 -DNDEBUG -Iinclude \
    -DSV_CHECK_DEFEATS=1 test_qbf_reduction.cpp -o test_qbf_sv

./test_qbf_ssv
./test_qbf_sv
```

`search_weight_orders.cpp` uses the same build flags, but it searches permutations of 12 weight groups and can take factorial time. It is an experiment driver, not the recommended first program to run.

## QBF input convention

The input stores only the CNF matrix. Variable indices implicitly specify the alternating quantifier prefix:

\[
\exists x_1\;\forall x_2\;\exists x_3\;\forall x_4\;\cdots\;Q_nx_n.
\]

Thus odd-indexed variables are existential and even-indexed variables are universal. For example,

```text
(x1, x2), (~x1, x3)
```

is interpreted as the matrix of `exists x1, forall x2, exists x3`.

This convention keeps the parser and tournament builder small without reducing generality. Any prenex QBF prefix can be converted in polynomial time to this alternating form: rename variables in prefix order, insert fresh unused variables between adjacent quantifiers of the same type, and prepend an unused existential variable when the original prefix begins universally. Quantifying a variable that does not occur in the matrix does not change the formula's truth value. Conversely, alternating-prefix TQBF is already a special case of general TQBF, so the two representations are polynomially equivalent.

For a formula with `n` variables and `m` clauses, the reduction creates

\[
N = 2 + 3n + m
\]

candidates: `C`, `D`, one `Ti`, `Fi`, and `Xi` per variable, and one `Lk` per clause. Under the reduction, `C` is the intended winner for a true quantified formula and `D` for a false one.

## Running an arbitrary tournament

`examples/run_tournament.cpp` shows the complete setup. Each unordered pair must appear once; `edge(a, b, m)` means that candidate `a` has positive margin `m` over candidate `b`. This one-group example stores the margins directly in the 16-bit offset field.

```cpp
#include <bits/stdc++.h>
using namespace std;
#include "fast_utils.hpp"
#include "graph_template.hpp"
#include "sv_fast.hpp"

int main() {
    GraphTemplate G;
    G.N = 4;
    G.names = {"A", "B", "C", "D"};
    G.dir.assign(G.N * G.N, 0);
    G.group.assign(G.N * G.N, 0);
    G.off.assign(G.N * G.N, 0);
    G.full_mask = (1ull << G.N) - 1ull;

    auto edge = [&](int a, int b, int m) {
        G.dir[G.IDX(a, b)] = 1;
        G.off[G.IDX(a, b)] = m;
    };
    edge(0,1,6); edge(1,2,5); edge(2,0,4);
    edge(0,3,3); edge(3,1,2); edge(2,3,1);

    int W[1] = {0};
    SVFast solver(G); solver.reset_epoch(W);
    cout << G.names[solver.solve_winner(G.full_mask)] << '\n';
}
```

Compile it as SSV or SV:

```bash
g++ -std=c++20 -O3 -DNDEBUG -Iinclude examples/run_tournament.cpp -o run_ssv
g++ -std=c++20 -O3 -DNDEBUG -Iinclude -DSV_CHECK_DEFEATS=1 examples/run_tournament.cpp -o run_sv
```

## Benchmark against `pref_voting`

The comparison target is Holliday and Pacuit's published `pref_voting` package (Journal of Open Source Software, 2025). The benchmark requires Python 3.10 or newer.

The benchmark gives both implementations exactly the same deterministic random tournaments with distinct odd margins. Distinct margins avoid tie-breaking ambiguity, and using odd margins keeps all pairwise margins on one parity. Tournament generation and conversion are outside the primary solver timing; each timed call includes the implementation's own solver allocation, memo setup, preprocessing, and winner calculation. The script also checks winner agreement before reporting any timings.

For each tournament, the benchmark first performs one warm-up call and then records the median of the requested timed repetitions. For each candidate count, the summary reports both the **median** and the **arithmetic mean** of those per-tournament medians. The median is robust to unusually hard instances; the mean better reflects the total runtime contributed by them. Warm-up time, the sum of all timed repetitions, graph construction, compilation, subprocess or Python-harness overhead, and total wall-clock time are recorded separately.

```bash
python -m pip install pref_voting==1.16.28 matplotlib
python tools/benchmark_pref_voting.py
```

It prints a table and writes:

```text
sv_benchmark.csv          median and mean results by candidate count
sv_benchmark_raw.csv      one row per generated tournament, including warm-up and wall times
sv_benchmark_timing.csv   compilation, harness, output, and full-run wall-clock accounting
sv_benchmark.png          log-scale timing plot
```

By default, the benchmark uses the current `pref_voting` defaults: `basic` for SSV and `with_condorcet_check_and_early_termination` for SV. For a direct comparison of the basic recursive implementations, run:

```bash
python tools/benchmark_pref_voting.py --pref-sv-algorithm basic
```

Candidate range, number of tournaments, repetitions, random seed, compiler, and `-march=native` are command-line options; run with `--help` for the full list.

### Reference benchmark result

A reference run used deterministic uniquely weighted tournaments with 4–20 candidates, five tournaments per candidate count, and nine timed calls per tournament. Each tournament was represented by the median of its nine calls; the table reports the median and arithmetic mean across the five tournament-level medians. The C++ and `pref_voting` implementations agreed on every SV and SSV winner.

**Results at 20 candidates**

| Rule | Median: C++ / `pref_voting` | Median ratio | Mean: C++ / `pref_voting` | Mean ratio |
|---|---:|---:|---:|---:|
| SSV | 47.6 ms / 5.34 s | 112× | 73.2 ms / 9.04 s | 123× |
| SV | 52.8 ms / 2.68 s | 50.7× | 82.4 ms / 7.00 s | 84.9× |

The run used Windows 11, Python 3.13.7, `pref_voting` 1.16.28, and MinGW `g++` 16.1.0. Including warm-ups and all repetitions, it took 1,666.7 seconds (27.8 minutes). Runtime varied substantially across tournaments: at 20 candidates, the largest `pref_voting` tournament-level medians were 18.7 seconds for SSV and 14.1 seconds for SV. These results are machine-, version-, and instance-specific and should be interpreted as a reproducible implementation comparison on this fixed sample, not as an estimate of average-case complexity. More information can be found in `benchmark/benchmark_results`

## Candidate limit

Candidate sets are represented by a `uint64_t` mask, and memo entries are indexed directly by that mask. On the intended 64-bit builds, the current solver therefore requires **fewer than 64 candidates** (`N <= 63`). This is a representational ceiling, not a practical one: a dense table for 63 candidates cannot be allocated. For reduction instances, the corresponding representational condition is `2 + 3n + m <= 63`.

The practical limit is much lower because the standard memo uses dense `O(2^N)` arrays. The winner and epoch arrays alone use about `8 * 2^N` bytes: approximately 128 MiB at 24 candidates, 256 MiB at 25, and 512 MiB at 26, before the remaining graph and process memory. Running time is exponential as well, although individual tournaments may terminate much sooner.

Increasing the hard 63-candidate limit is conceptually possible with a multiword bitset, but retaining the current speed would require more than changing the integer type. The dense mask-indexed memo would need to become sparse or structurally compressed, which introduces hashing and allocation costs and does not remove the exponential worst case. Supporting substantially larger tournaments while remaining fast would therefore be a significant solver redesign rather than a small compatibility edit.

## Visualization

```bash
python -m pip install networkx matplotlib
python tools/stablevoting_customvisualization.py
```

The visualization implements the current `C`, `D`, `Lk`, `Ti`, `Fi`, and `Xi` construction. The older Python SAT prototype is retained only under `extras/legacy/`.
