#!/usr/bin/env python3
"""Compare this repository's C++ SV/SSV solver with pref_voting.

The same deterministic, uniquely weighted tournaments are sent to all four
runs: C++ SSV, pref_voting SSV, C++ SV, and pref_voting SV. The script checks
winner agreement, prints solver and wall-clock timing tables, and saves CSV and
PNG results.

python benchmark_pref_voting.py --output benchmark_results/sv_benchmark
python benchmark_pref_voting.py --max-candidates 24 --cpp-only-above 20
python benchmark_pref_voting.py --min-candidates 21 --max-candidates 30 --cpp-only-above 20 --repeats 9 --output benchmark_results/sv_benchmark_cpp_21_to_30
"""

from __future__ import annotations

import time
SCRIPT_START = time.perf_counter()

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


@dataclass(frozen=True)
class TimingResult:
    winner: int
    median_seconds: float
    warmup_seconds: float
    timed_total_seconds: float
    wall_seconds: float
    setup_seconds: float = 0.0

    @property
    def overhead_seconds(self) -> float:
        measured = self.warmup_seconds + self.timed_total_seconds + self.setup_seconds
        return max(0.0, self.wall_seconds - measured)


@dataclass(frozen=True)
class CppRunTiming:
    input_seconds: float
    process_seconds: float
    parse_seconds: float
    case_wall_seconds: float
    process_overhead_seconds: float


@dataclass(frozen=True)
class PrefRunTiming:
    graph_seconds: float
    ssv_seconds: float
    sv_seconds: float
    loop_overhead_seconds: float
    total_seconds: float


def arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Benchmark the C++ SV/SSV solver against pref_voting.")

    parser.add_argument("--min-candidates", type=int, default=4)
    parser.add_argument("--max-candidates", type=int, default=14)
    parser.add_argument("--cpp-only-above", type=int, default=None, metavar="N",
        help="run pref_voting through N candidates and only C++ above N")
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
    if args.cpp_only_above is not None and args.cpp_only_above < 1:
        parser.error("--cpp-only-above must be positive")
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
                    executable: Path, stable: bool, native: bool) -> float:
    command = [
        compiler, "-std=c++20", "-O3", "-DNDEBUG",
        f"-DSV_CHECK_DEFEATS={1 if stable else 0}", f"-I{include_dir}", str(helper),
        "-o", str(executable),
    ]
    if native:
        command.insert(3, "-march=native")

    start = time.perf_counter()
    try:
        subprocess.run(command, check=True)
    except FileNotFoundError as exc:
        raise SystemExit(f"Compiler not found: {compiler}") from exc
    except subprocess.CalledProcessError as exc:
        raise SystemExit(f"C++ helper compilation failed: {' '.join(command)}") from exc
    return time.perf_counter() - start


def tournament_input(tournaments: list[Tournament], repeats: int) -> str:
    lines = [f"{len(tournaments)} {repeats}"]
    for tournament in tournaments:
        lines.append(str(tournament.candidates))
        lines.extend(f"{a} {b} {margin}" for a, b, margin in tournament.edges)
    return "\n".join(lines) + "\n"


def run_cpp(executable: Path, tournaments: list[Tournament], repeats: int) -> tuple[list[TimingResult], CppRunTiming]:
    input_start = time.perf_counter()
    input_text = tournament_input(tournaments, repeats)
    input_seconds = time.perf_counter() - input_start

    process_start = time.perf_counter()
    completed = subprocess.run(
        [str(executable)],
        input=input_text,
        text=True,
        # capture_output=True,
        stdout=subprocess.PIPE,
        check=True,
    )
    process_seconds = time.perf_counter() - process_start

    parse_start = time.perf_counter()
    results = []
    for expected_index, line in enumerate(completed.stdout.splitlines()):
        fields = line.split("\t")
        if len(fields) != 7:
            raise RuntimeError(f"Unexpected C++ helper output: {line}")

        index, candidates, winner, median_seconds, warmup_seconds, timed_total_seconds, wall_seconds = fields
        index = int(index)
        if index != expected_index:
            raise RuntimeError("C++ helper returned tournaments out of order")
        if int(candidates) != tournaments[index].candidates:
            raise RuntimeError("C++ helper returned the wrong candidate count")

        results.append(TimingResult(
            int(winner), float(median_seconds), float(warmup_seconds),
            float(timed_total_seconds), float(wall_seconds),
        ))

    if len(results) != len(tournaments):
        raise RuntimeError("C++ helper returned an incomplete result set")

    parse_seconds = time.perf_counter() - parse_start
    case_wall_seconds = sum(result.wall_seconds for result in results)
    process_overhead_seconds = max(0.0, process_seconds - case_wall_seconds)
    timing = CppRunTiming(
        input_seconds, process_seconds, parse_seconds,
        case_wall_seconds, process_overhead_seconds,
    )
    return results, timing


def timed_pref_call(call, repeats: int) -> TimingResult:
    wall_start = time.perf_counter()

    warmup_start = time.perf_counter()
    winners = call()
    warmup_seconds = time.perf_counter() - warmup_start

    setup_start = time.perf_counter()
    gc.collect()
    setup_seconds = time.perf_counter() - setup_start

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

    wall_seconds = time.perf_counter() - wall_start
    return TimingResult(
        winners[0], statistics.median(times), warmup_seconds,
        sum(times), wall_seconds, setup_seconds,
    )


def run_pref_voting(tournaments: list[Tournament], repeats: int,
                    sv_algorithm: str, ssv_algorithm: str,
                    max_candidates: int | None) -> tuple[
                        list[TimingResult | None], list[TimingResult | None],
                        list[float | None], PrefRunTiming
                    ]:
    ssv_results = []
    sv_results = []
    graph_times = []
    total_start = time.perf_counter()
    selected = sum(
        max_candidates is None or tournament.candidates <= max_candidates
        for tournament in tournaments
    )
    completed = 0

    for tournament in tournaments:
        if max_candidates is not None and tournament.candidates > max_candidates:
            ssv_results.append(None)
            sv_results.append(None)
            graph_times.append(None)
            continue

        completed += 1
        case_start = time.perf_counter()

        graph_start = time.perf_counter()
        graph = MarginGraph(list(range(tournament.candidates)), list(tournament.edges))
        graph_seconds = time.perf_counter() - graph_start
        graph_times.append(graph_seconds)

        ssv_result = timed_pref_call(
            lambda: simple_stable_voting(graph, algorithm=ssv_algorithm), repeats
        )
        sv_result = timed_pref_call(
            lambda: stable_voting(graph, algorithm=sv_algorithm), repeats
        )
        ssv_results.append(ssv_result)
        sv_results.append(sv_result)

        case_seconds = time.perf_counter() - case_start
        print(
            f"benchmarked {completed}/{selected}: N={tournament.candidates}, "
            f"sample={tournament.sample}, wall={case_seconds:.3f}s "
            f"(graph={graph_seconds:.3f}s, SSV={ssv_result.wall_seconds:.3f}s, "
            f"SV={sv_result.wall_seconds:.3f}s)",
            file=sys.stderr, flush=True,
        )

    total_seconds = time.perf_counter() - total_start
    graph_seconds = sum(seconds for seconds in graph_times if seconds is not None)
    ssv_seconds = sum(result.wall_seconds for result in ssv_results if result is not None)
    sv_seconds = sum(result.wall_seconds for result in sv_results if result is not None)
    loop_overhead_seconds = max(0.0, total_seconds - graph_seconds - ssv_seconds - sv_seconds)
    timing = PrefRunTiming(
        graph_seconds, ssv_seconds, sv_seconds,
        loop_overhead_seconds, total_seconds,
    )
    return ssv_results, sv_results, graph_times, timing


def combine_results(
    tournaments: list[Tournament],
    cpp_ssv: list[TimingResult],
    pref_ssv: list[TimingResult | None],
    cpp_sv: list[TimingResult],
    pref_sv: list[TimingResult | None],
    graph_times: list[float | None],
) -> list[dict]:
    rows = []

    for i, tournament in enumerate(tournaments):
        pref_available = pref_ssv[i] is not None and pref_sv[i] is not None
        if pref_available and cpp_ssv[i].winner != pref_ssv[i].winner:
            raise RuntimeError(
                f"SSV winner mismatch at N={tournament.candidates}, "
                f"sample={tournament.sample}: C++={cpp_ssv[i].winner}, "
                f"pref_voting={pref_ssv[i].winner}"
            )
        if pref_available and cpp_sv[i].winner != pref_sv[i].winner:
            raise RuntimeError(
                f"SV winner mismatch at N={tournament.candidates}, "
                f"sample={tournament.sample}: C++={cpp_sv[i].winner}, "
                f"pref_voting={pref_sv[i].winner}"
            )

        rows.append({
            # Keep the original primary columns first for compatibility.
            "candidates": tournament.candidates,
            "sample": tournament.sample,
            "cpp_ssv_seconds": cpp_ssv[i].median_seconds,
            "pref_ssv_seconds": pref_ssv[i].median_seconds if pref_available else None,
            "cpp_sv_seconds": cpp_sv[i].median_seconds,
            "pref_sv_seconds": pref_sv[i].median_seconds if pref_available else None,
            "ssv_winner": cpp_ssv[i].winner,
            "sv_winner": cpp_sv[i].winner,

            # Wall-clock accounting. The primary columns above remain the
            # per-tournament medians used in the comparison table and plot.
            "margin_graph_seconds": graph_times[i],
            "cpp_ssv_warmup_seconds": cpp_ssv[i].warmup_seconds,
            "cpp_ssv_timed_total_seconds": cpp_ssv[i].timed_total_seconds,
            "cpp_ssv_benchmark_wall_seconds": cpp_ssv[i].wall_seconds,
            "cpp_ssv_harness_overhead_seconds": cpp_ssv[i].overhead_seconds,
            "pref_ssv_warmup_seconds": pref_ssv[i].warmup_seconds if pref_available else None,
            "pref_ssv_gc_seconds": pref_ssv[i].setup_seconds if pref_available else None,
            "pref_ssv_timed_total_seconds": pref_ssv[i].timed_total_seconds if pref_available else None,
            "pref_ssv_benchmark_wall_seconds": pref_ssv[i].wall_seconds if pref_available else None,
            "pref_ssv_harness_overhead_seconds": pref_ssv[i].overhead_seconds if pref_available else None,
            "cpp_sv_warmup_seconds": cpp_sv[i].warmup_seconds,
            "cpp_sv_timed_total_seconds": cpp_sv[i].timed_total_seconds,
            "cpp_sv_benchmark_wall_seconds": cpp_sv[i].wall_seconds,
            "cpp_sv_harness_overhead_seconds": cpp_sv[i].overhead_seconds,
            "pref_sv_warmup_seconds": pref_sv[i].warmup_seconds if pref_available else None,
            "pref_sv_gc_seconds": pref_sv[i].setup_seconds if pref_available else None,
            "pref_sv_timed_total_seconds": pref_sv[i].timed_total_seconds if pref_available else None,
            "pref_sv_benchmark_wall_seconds": pref_sv[i].wall_seconds if pref_available else None,
            "pref_sv_harness_overhead_seconds": pref_sv[i].overhead_seconds if pref_available else None,
        })

    return rows


def summarize(raw_rows: list[dict]) -> list[dict]:
    summary = []
    for n in sorted({row["candidates"] for row in raw_rows}):
        group = [row for row in raw_rows if row["candidates"] == n]
        cpp_ssv = [r["cpp_ssv_seconds"] for r in group]
        pref_ssv = [r["pref_ssv_seconds"] for r in group if r["pref_ssv_seconds"] is not None]
        cpp_sv = [r["cpp_sv_seconds"] for r in group]
        pref_sv = [r["pref_sv_seconds"] for r in group if r["pref_sv_seconds"] is not None]

        row = {
            "candidates": n,
            "tournaments": len(group),
            "cpp_ssv_ms": 1000 * statistics.median(cpp_ssv),
            "pref_ssv_ms": 1000 * statistics.median(pref_ssv) if pref_ssv else None,
            "cpp_sv_ms": 1000 * statistics.median(cpp_sv),
            "pref_sv_ms": 1000 * statistics.median(pref_sv) if pref_sv else None,
            "cpp_ssv_mean_ms": 1000 * statistics.mean(cpp_ssv),
            "pref_ssv_mean_ms": 1000 * statistics.mean(pref_ssv) if pref_ssv else None,
            "cpp_sv_mean_ms": 1000 * statistics.mean(cpp_sv),
            "pref_sv_mean_ms": 1000 * statistics.mean(pref_sv) if pref_sv else None,
            "cpp_ssv_max_ms": 1000 * max(cpp_ssv),
            "pref_ssv_max_ms": 1000 * max(pref_ssv) if pref_ssv else None,
            "cpp_sv_max_ms": 1000 * max(cpp_sv),
            "pref_sv_max_ms": 1000 * max(pref_sv) if pref_sv else None,
        }
        row["ssv_speedup"] = row["pref_ssv_ms"] / row["cpp_ssv_ms"] if pref_ssv else None
        row["sv_speedup"] = row["pref_sv_ms"] / row["cpp_sv_ms"] if pref_sv else None
        row["ssv_mean_speedup"] = row["pref_ssv_mean_ms"] / row["cpp_ssv_mean_ms"] if pref_ssv else None
        row["sv_mean_speedup"] = row["pref_sv_mean_ms"] / row["cpp_sv_mean_ms"] if pref_sv else None
        summary.append(row)
    return summary


def print_table(summary: list[dict]) -> None:
    def value(value: float | None, width: int, precision: int) -> str:
        if value is None:
            return f"{'--':>{width}}"
        return f"{value:>{width}.{precision}g}"
    def fixed_value(value: float | None, width: int) -> str:
        if value is None:
            return f"{'--':>{width}}"
        return f"{value:>{width}.{'1f' if value >= 10 else '3g'}}"

    def print_statistic(title: str, suffix: str) -> None:
        print(f"\n{title}:")
        print(f"{'N':>3} | {'cases':>5} | {'C++ SSV ms':>11} | {'pref SSV ms':>11} | "
              f"{'SSV x':>7} | {'C++ SV ms':>10} | {'pref SV ms':>10} | {'SV x':>7}")
        print("----+-------+-------------+-------------+---------+------------+------------+--------")
        for row in summary:
            print(
                f"{row['candidates']:>3} | {row['tournaments']:>5} | "
                f"{value(row[f'cpp_ssv{suffix}_ms'], 11, 6)} | "
                f"{value(row[f'pref_ssv{suffix}_ms'], 11, 6)} | "
                f"{fixed_value(row[f'ssv{suffix}_speedup'], 7)} | "
                # f"{row[f'ssv{suffix}_speedup']:>7.{'1f' if row[f'ssv{suffix}_speedup'] >= 10 else '3g'}} | "
                f"{value(row[f'cpp_sv{suffix}_ms'], 10, 6)} | "
                f"{value(row[f'pref_sv{suffix}_ms'], 10, 6)} | "
                f"{fixed_value(row[f'sv{suffix}_speedup'], 7)}"
                # f"{row[f'sv{suffix}_speedup']:>7.{'1f' if row[f'ssv{suffix}_speedup'] >= 10 else '3g'}}"
            )

    print_statistic("Median of per-tournament medians", "")
    print_statistic("Arithmetic mean of per-tournament medians", "_mean")

    last_pref = next((row for row in reversed(summary) if row["pref_ssv_max_ms"] is not None), None)
    if last_pref is not None:
        print(
            f"\nAt N={last_pref['candidates']}, the largest per-tournament medians were "
            f"{last_pref['pref_ssv_max_ms'] / 1000:.6g}s for pref_voting SSV and "
            f"{last_pref['pref_sv_max_ms'] / 1000:.6g}s for pref_voting SV."
        )


def write_csv(path: Path, rows: list[dict]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=list(rows[0]))
        writer.writeheader()
        writer.writerows(rows)


def plot(summary: list[dict], path: Path, sv_algorithm: str, ssv_algorithm: str) -> None:
    n = [row["candidates"] for row in summary]
    pref_rows = [row for row in summary if row["pref_ssv_ms"] is not None]
    figure, axis = plt.subplots(figsize=(8, 5))
    axis.plot(n, [row["cpp_ssv_ms"] for row in summary], marker="o", label="C++ SSV")
    if pref_rows:
        axis.plot([row["candidates"] for row in pref_rows],
                  [row["pref_ssv_ms"] for row in pref_rows], marker="o",
                  label=f"pref_voting SSV ({ssv_algorithm})")
    axis.plot(n, [row["cpp_sv_ms"] for row in summary], marker="o", label="C++ SV")
    if pref_rows:
        axis.plot([row["candidates"] for row in pref_rows],
                  [row["pref_sv_ms"] for row in pref_rows], marker="o",
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


def timing_row(phase: str, seconds: float, notes: str = "") -> dict:
    return {"phase": phase, "seconds": seconds, "notes": notes}


def print_timing_table(rows: list[dict]) -> None:
    print("\nWall-clock accounting (warm-ups and all repeats included):")
    for row in rows:
        if row["phase"] == "total benchmark through primary outputs":
            print("  " + "-" * 58)
        print(f"  {row['phase']:<43} {row['seconds']:>12.3f}s")


def main() -> int:
    main_start = time.perf_counter()
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

    platform_name = platform.platform()
    python_version = platform.python_version()
    compiler_name = compiler_version(args.compiler)
    setup_seconds = time.perf_counter() - main_start

    generation_start = time.perf_counter()
    tournaments = generate_tournaments(args)
    pref_count = sum(
        args.cpp_only_above is None or tournament.candidates <= args.cpp_only_above
        for tournament in tournaments
    )
    generation_seconds = time.perf_counter() - generation_start

    if pref_count:
        load_pref_voting()

    print(f"Platform: {platform_name}")
    print(f"Python: {python_version}; compiler: {compiler_name}")

    print("\nParameters:")
    print(f"  pref_voting {getattr(pref_voting, '__version__', 'not run')}; "
          f"seed={args.seed}; candidates={args.min_candidates}..{args.max_candidates}; "
          f"{args.tournaments} tournaments/size; {args.repeats} timed calls/tournament")
    print(f"  pref_voting algorithms: SSV={args.pref_ssv_algorithm}, SV={args.pref_sv_algorithm}")
    if args.cpp_only_above is not None:
        print(f"  pref_voting through N={args.cpp_only_above}; C++ only above that "
              "(winner agreement is checked only where both run)")
    print()

    print(f"  (each tournament is represented by the median of its {args.repeats} repeats;\n"
          f"   for each size we report both the median and arithmetic mean across "
          f"the {args.tournaments} tournaments)\n")
    print("  Warm-ups, all repeats, compilation, graph construction, and harness overhead "
          "are reported separately below.\n")

    with tempfile.TemporaryDirectory(prefix="sv-benchmark-") as temporary:
        temporary = Path(temporary)
        ssv_executable = temporary / "benchmark_cpp_ssv"
        sv_executable = temporary / "benchmark_cpp_sv"
        compile_ssv_seconds = compile_helper(
            args.compiler, helper, include_dir, ssv_executable,
            stable=False, native=args.native,
        )
        compile_sv_seconds = compile_helper(
            args.compiler, helper, include_dir, sv_executable,
            stable=True, native=args.native,
        )
        cpp_ssv, cpp_ssv_timing = run_cpp(ssv_executable, tournaments, args.repeats)
        cpp_sv, cpp_sv_timing = run_cpp(sv_executable, tournaments, args.repeats)

    pref_ssv, pref_sv, graph_times, pref_timing = run_pref_voting(
        tournaments, args.repeats,
        args.pref_sv_algorithm, args.pref_ssv_algorithm,
        args.cpp_only_above,
    )

    processing_start = time.perf_counter()
    raw_rows = combine_results(
        tournaments, cpp_ssv, pref_ssv, cpp_sv, pref_sv, graph_times,
    )
    summary = summarize(raw_rows)
    processing_seconds = time.perf_counter() - processing_start
    print_table(summary)

    prefix = Path(args.output)
    summary_path = prefix.with_suffix(".csv")
    raw_path = prefix.with_name(prefix.name + "_raw.csv")
    timing_path = prefix.with_name(prefix.name + "_timing.csv")
    plot_path = prefix.with_suffix(".png")

    csv_start = time.perf_counter()
    write_csv(summary_path, summary)
    write_csv(raw_path, raw_rows)
    csv_seconds = time.perf_counter() - csv_start

    plot_start = time.perf_counter()
    plot(summary, plot_path, args.pref_sv_algorithm, args.pref_ssv_algorithm)
    plot_seconds = time.perf_counter() - plot_start

    total_before_timing_report = time.perf_counter() - SCRIPT_START
    timing_rows = [
        timing_row("script initialization and imports", main_start - SCRIPT_START),
        timing_row("argument parsing and environment checks", setup_seconds),
        timing_row("tournament generation", generation_seconds),
        timing_row("C++ SSV compilation", compile_ssv_seconds),
        timing_row("C++ SV compilation", compile_sv_seconds),
        timing_row("C++ SSV input serialization", cpp_ssv_timing.input_seconds),
        timing_row("C++ SSV benchmark subprocess", cpp_ssv_timing.process_seconds,
                   "includes helper input parsing, warm-ups, repeats, and process overhead"),
        timing_row("C++ SSV output parsing", cpp_ssv_timing.parse_seconds),
        timing_row("C++ SV input serialization", cpp_sv_timing.input_seconds),
        timing_row("C++ SV benchmark subprocess", cpp_sv_timing.process_seconds,
                   "includes helper input parsing, warm-ups, repeats, and process overhead"),
        timing_row("C++ SV output parsing", cpp_sv_timing.parse_seconds),
        timing_row("pref_voting MarginGraph construction", pref_timing.graph_seconds),
        timing_row("pref_voting SSV benchmark calls", pref_timing.ssv_seconds,
                   "includes one warm-up, garbage collection, and all timed repeats"),
        timing_row("pref_voting SV benchmark calls", pref_timing.sv_seconds,
                   "includes one warm-up, garbage collection, and all timed repeats"),
        timing_row("pref_voting loop/progress overhead", pref_timing.loop_overhead_seconds),
        timing_row("winner validation and aggregation", processing_seconds),
        timing_row("summary and raw CSV output", csv_seconds),
        timing_row("plot output", plot_seconds),
    ]
    accounted_seconds = sum(row["seconds"] for row in timing_rows)
    timing_rows.append(timing_row(
        "other benchmark overhead",
        max(0.0, total_before_timing_report - accounted_seconds),
        "temporary-directory handling, printing, and other small unmeasured work",
    ))
    timing_rows.append(timing_row(
        "total benchmark through primary outputs",
        total_before_timing_report,
        "excludes only writing this timing report and final status lines",
    ))

    write_csv(timing_path, timing_rows)
    print_timing_table(timing_rows)

    final_total = time.perf_counter() - SCRIPT_START
    print(f"\nSummary CSV: {summary_path}")
    print(f"Raw CSV:     {raw_path}")
    print(f"Timing CSV:  {timing_path}")
    print(f"Plot:        {plot_path}")
    print(f"Final script wall time: {final_total:.3f}s")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
