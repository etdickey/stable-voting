#!/usr/bin/env python3
"""Compare this repository's C++ SV/SSV solver with pref_voting.

The same deterministic, uniquely weighted tournaments are sent to all four
runs: C++ SSV, pref_voting SSV, C++ SV, and pref_voting SV. The script checks
winner agreement, prints a timing table, and saves CSV and PNG results.

python benchmark_pref_voting.py --output benchmark_results/sv_benchmark
"""

from __future__ import annotations

import argparse
import csv
import gc
import itertools
import platform
import random
import statistics
import subprocess
import sys
import tempfile
import time
from dataclasses import dataclass
from pathlib import Path

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

try:
    import pref_voting
    from pref_voting.margin_based_methods import simple_stable_voting, stable_voting
    from pref_voting.weighted_majority_graphs import MarginGraph
except ImportError as exc:
    raise SystemExit(
        "Missing dependency. Run: python -m pip install pref_voting matplotlib"
    ) from exc


@dataclass(frozen=True)
class Tournament:
    candidates: int
    sample: int
    edges: tuple[tuple[int, int, int], ...]


def arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Benchmark the C++ SV/SSV solver against pref_voting.")

    parser.add_argument("--min-candidates", type=int, default=4)
    parser.add_argument("--max-candidates", type=int, default=14)
    parser.add_argument("--tournaments", type=int, default=5, help="tournaments per candidate count")
    parser.add_argument("--repeats", type=int, default=3, help="timed calls per implementation and tournament")
    parser.add_argument("--seed", type=int, default=20260714)
    parser.add_argument("--compiler", default="g++")
    parser.add_argument("--native", action="store_true", help="add -march=native to the C++ build")
    parser.add_argument("--pref-sv-algorithm", default="with_condorcet_check_and_early_termination",
        choices=("basic", "with_condorcet_check", "with_early_termination",
                 "with_condorcet_check_and_early_termination")
    )
    parser.add_argument("--pref-ssv-algorithm", default="basic", choices=("basic", "with_condorcet_check"))
    parser.add_argument("--output", default="sv_benchmark", help="output prefix for CSV and PNG files")
    args = parser.parse_args()

    if args.min_candidates < 2:
        parser.error("--min-candidates must be at least 2")
    if args.max_candidates < args.min_candidates:
        parser.error("--max-candidates must be at least --min-candidates")
    if args.max_candidates >= 64:
        parser.error("the current C++ implementation requires fewer than 64 candidates")
    if args.tournaments <= 0 or args.repeats <= 0:
        parser.error("--tournaments and --repeats must be positive")
    return args


def generate_tournaments(args: argparse.Namespace) -> list[Tournament]:
    rng = random.Random(args.seed)
    tournaments = []

    for n in range(args.min_candidates, args.max_candidates + 1):
        pairs = list(itertools.combinations(range(n), 2))
        for sample in range(args.tournaments):
            # Distinct margins avoid ties. All are odd, so all pairwise margins
            # have the same parity, as they do in an odd-voter profile.
            margins = list(range(1, 2 * len(pairs), 2))
            rng.shuffle(margins)

            edges = []
            for (a, b), margin in zip(pairs, margins):
                edges.append((a, b, margin) if rng.getrandbits(1)
                             else (b, a, margin))
            tournaments.append(Tournament(n, sample, tuple(edges)))

    return tournaments


def compile_helper(compiler: str, helper: Path, include_dir: Path,
                    executable: Path, stable: bool, native: bool) -> None:
    command = [
        compiler, "-std=c++20", "-O3", "-DNDEBUG",
        f"-DSV_CHECK_DEFEATS={1 if stable else 0}", f"-I{include_dir}", str(helper),
        "-o", str(executable),
    ]
    if native:
        command.insert(3, "-march=native")

    try:
        subprocess.run(command, check=True)
    except FileNotFoundError as exc:
        raise SystemExit(f"Compiler not found: {compiler}") from exc
    except subprocess.CalledProcessError as exc:
        raise SystemExit(f"C++ helper compilation failed: {' '.join(command)}") from exc


def tournament_input(tournaments: list[Tournament], repeats: int) -> str:
    lines = [f"{len(tournaments)} {repeats}"]
    for tournament in tournaments:
        lines.append(str(tournament.candidates))
        lines.extend(f"{a} {b} {margin}" for a, b, margin in tournament.edges)
    return "\n".join(lines) + "\n"


def run_cpp(executable: Path, tournaments: list[Tournament], repeats: int) -> list[tuple[int, float]]:
    completed = subprocess.run(
        [str(executable)],
        input=tournament_input(tournaments, repeats),
        text=True,
        capture_output=True,
        check=True,
    )

    results = []
    for expected_index, line in enumerate(completed.stdout.splitlines()):
        index, candidates, winner, seconds = line.split("\t")
        index = int(index)
        if index != expected_index:
            raise RuntimeError("C++ helper returned tournaments out of order")
        if int(candidates) != tournaments[index].candidates:
            raise RuntimeError("C++ helper returned the wrong candidate count")
        results.append((int(winner), float(seconds)))

    if len(results) != len(tournaments):
        raise RuntimeError("C++ helper returned an incomplete result set")
    return results


def timed_pref_call(call, repeats: int) -> tuple[int, float]:
    winners = call()  # warm-up
    gc.collect()

    times = []
    gc.disable()
    try:
        for _ in range(repeats):
            start = time.perf_counter()
            winners = call()
            times.append(time.perf_counter() - start)
    finally:
        gc.enable()

    if len(winners) != 1:
        raise RuntimeError(f"Expected one winner, received {winners}")
    return winners[0], statistics.median(times)


def run_pref_voting(tournaments: list[Tournament], repeats: int,
                    sv_algorithm: str, ssv_algorithm: str) -> tuple[list[tuple[int, float]], list[tuple[int, float]]]:
    ssv_results = []
    sv_results = []

    for index, tournament in enumerate(tournaments, start=1):
        graph = MarginGraph(list(range(tournament.candidates)), list(tournament.edges))
        ssv_results.append(timed_pref_call(
            lambda: simple_stable_voting(graph, algorithm=ssv_algorithm), repeats
        ))
        sv_results.append(timed_pref_call(
            lambda: stable_voting(graph, algorithm=sv_algorithm), repeats
        ))
        print(f"\rbenchmarked {index}/{len(tournaments)} tournaments",
              end="", file=sys.stderr, flush=True)

    print(file=sys.stderr)
    return ssv_results, sv_results


def combine_results(
    tournaments: list[Tournament],
    cpp_ssv: list[tuple[int, float]],
    pref_ssv: list[tuple[int, float]],
    cpp_sv: list[tuple[int, float]],
    pref_sv: list[tuple[int, float]],
) -> list[dict]:
    rows = []

    for i, tournament in enumerate(tournaments):
        if cpp_ssv[i][0] != pref_ssv[i][0]:
            raise RuntimeError(
                f"SSV winner mismatch at N={tournament.candidates}, "
                f"sample={tournament.sample}: C++={cpp_ssv[i][0]}, "
                f"pref_voting={pref_ssv[i][0]}"
            )
        if cpp_sv[i][0] != pref_sv[i][0]:
            raise RuntimeError(
                f"SV winner mismatch at N={tournament.candidates}, "
                f"sample={tournament.sample}: C++={cpp_sv[i][0]}, "
                f"pref_voting={pref_sv[i][0]}"
            )

        rows.append({
            "candidates": tournament.candidates,
            "sample": tournament.sample,
            "cpp_ssv_seconds": cpp_ssv[i][1],
            "pref_ssv_seconds": pref_ssv[i][1],
            "cpp_sv_seconds": cpp_sv[i][1],
            "pref_sv_seconds": pref_sv[i][1],
            "ssv_winner": cpp_ssv[i][0],
            "sv_winner": cpp_sv[i][0],
        })

    return rows


def summarize(raw_rows: list[dict]) -> list[dict]:
    summary = []
    for n in sorted({row["candidates"] for row in raw_rows}):
        group = [row for row in raw_rows if row["candidates"] == n]
        row = {
            "candidates": n,
            "tournaments": len(group),
            "cpp_ssv_ms": 1000 * statistics.median(r["cpp_ssv_seconds"] for r in group),
            "pref_ssv_ms": 1000 * statistics.median(r["pref_ssv_seconds"] for r in group),
            "cpp_sv_ms": 1000 * statistics.median(r["cpp_sv_seconds"] for r in group),
            "pref_sv_ms": 1000 * statistics.median(r["pref_sv_seconds"] for r in group),
        }
        row["ssv_speedup"] = row["pref_ssv_ms"] / row["cpp_ssv_ms"]
        row["sv_speedup"] = row["pref_sv_ms"] / row["cpp_sv_ms"]
        summary.append(row)
    return summary


def print_table(summary: list[dict]) -> None:
    print(f"{'N':>3} | {'cases':>5} | {'C++ SSV ms':>11} | {'pref SSV ms':>11} | "
          f"{'SSV x':>7} | {'C++ SV ms':>10} | {'pref SV ms':>10} | {'SV x':>7}")
    print("----+-------+-------------+-------------+---------+------------+------------+--------")
    for row in summary:
        print(
            f"{row['candidates']:>3} | {row['tournaments']:>5} | "
            f"{row['cpp_ssv_ms']:>11.6g} | {row['pref_ssv_ms']:>11.6g} | "
            f"{row['ssv_speedup']:>7.3g} | {row['cpp_sv_ms']:>10.6g} | "
            f"{row['pref_sv_ms']:>10.6g} | {row['sv_speedup']:>7.3g}"
        )


def write_csv(path: Path, rows: list[dict]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=list(rows[0]))
        writer.writeheader()
        writer.writerows(rows)


def plot(summary: list[dict], path: Path, sv_algorithm: str, ssv_algorithm: str) -> None:
    n = [row["candidates"] for row in summary]
    figure, axis = plt.subplots(figsize=(8, 5))
    axis.plot(n, [row["cpp_ssv_ms"] for row in summary], marker="o", label="C++ SSV")
    axis.plot(n, [row["pref_ssv_ms"] for row in summary], marker="o",
              label=f"pref_voting SSV ({ssv_algorithm})")
    axis.plot(n, [row["cpp_sv_ms"] for row in summary], marker="o", label="C++ SV")
    axis.plot(n, [row["pref_sv_ms"] for row in summary], marker="o",
              label=f"pref_voting SV ({sv_algorithm})")
    axis.set_yscale("log")
    axis.set_xlabel("Number of candidates")
    axis.set_ylabel("Median time per tournament (ms, log scale)")
    axis.set_title("Stable Voting implementation benchmark")
    axis.grid(True, which="both", linewidth=0.4)
    axis.legend()
    figure.tight_layout()
    path.parent.mkdir(parents=True, exist_ok=True)
    figure.savefig(path, dpi=180)
    plt.close(figure)


def compiler_version(compiler: str) -> str:
    try:
        output = subprocess.run(
            [compiler, "--version"], text=True, capture_output=True, check=True
        ).stdout.splitlines()
        return output[0] if output else compiler
    except (OSError, subprocess.CalledProcessError):
        return compiler


def main() -> int:
    args = arguments()
    tools_dir = Path(__file__).resolve().parent
    repo_root = tools_dir.parent
    helper = tools_dir / "benchmark_cpp.cpp"
    include_dir = repo_root / "include"

    required = [helper, include_dir / "fast_utils.hpp",
                include_dir / "graph_template.hpp", include_dir / "sv_fast.hpp"]
    missing = [path for path in required if not path.exists()]
    if missing:
        raise SystemExit(f"Required file not found: {missing[0]}")

    tournaments = generate_tournaments(args)
    print(f"Platform: {platform.platform()}")
    print(f"Python: {platform.python_version()}; compiler: {compiler_version(args.compiler)}")

    print("\nParameters:")
    print(f"  pref_voting {getattr(pref_voting, '__version__', 'unknown')}; "
          f"seed={args.seed}; candidates={args.min_candidates}..{args.max_candidates}; "
          f"{args.tournaments} tournaments/size; {args.repeats} timed calls/tournament")
    print(f"  pref_voting algorithms: SSV={args.pref_ssv_algorithm}, SV={args.pref_sv_algorithm}\n")

    print(f"  (median of medians: we take the median of the {args.repeats} repeats "
          f"for each tournament, then the median of the {args.tournaments} tournaments for each size)")

    with tempfile.TemporaryDirectory(prefix="sv-benchmark-") as temporary:
        temporary = Path(temporary)
        ssv_executable = temporary / "benchmark_cpp_ssv"
        sv_executable = temporary / "benchmark_cpp_sv"
        compile_helper(args.compiler, helper, include_dir, ssv_executable,
                       stable=False, native=args.native)
        compile_helper(args.compiler, helper, include_dir, sv_executable,
                       stable=True, native=args.native)
        cpp_ssv = run_cpp(ssv_executable, tournaments, args.repeats)
        cpp_sv = run_cpp(sv_executable, tournaments, args.repeats)

    pref_ssv, pref_sv = run_pref_voting(
        tournaments, args.repeats,
        args.pref_sv_algorithm, args.pref_ssv_algorithm,
    )
    raw_rows = combine_results(tournaments, cpp_ssv, pref_ssv, cpp_sv, pref_sv)
    summary = summarize(raw_rows)
    print_table(summary)

    prefix = Path(args.output)
    summary_path = prefix.with_suffix(".csv")
    raw_path = prefix.with_name(prefix.name + "_raw.csv")
    plot_path = prefix.with_suffix(".png")
    write_csv(summary_path, summary)
    write_csv(raw_path, raw_rows)
    plot(summary, plot_path, args.pref_sv_algorithm, args.pref_ssv_algorithm)

    print(f"\nSummary CSV: {summary_path}")
    print(f"Raw CSV:     {raw_path}")
    print(f"Plot:        {plot_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
