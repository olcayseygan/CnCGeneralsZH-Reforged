"""
bench - one fixed, deterministic score for PathLab's movement model.

Every map kind and scenario at a few group sizes, each over the same seeds.  A case's free-flow
bound is the slowest member walking to its own assigned goal alone on the map, so the ratio of the
group's frames to it is what the crowd cost.  Same input, same numbers.

    python bench.py                         # the full suite, about two minutes
    python bench.py --quick                 # a subset, under twenty seconds
    python bench.py --module idea.py        # score a modified copy of pathlab.py
    python bench.py --json baseline.json    # also write the numbers
"""

import argparse
import importlib.util
import json
import math
import os
import sys
import time
from concurrent.futures import ProcessPoolExecutor
from dataclasses import asdict, dataclass
from pathlib import Path
from types import ModuleType

DEFAULT_MODULE: Path = Path(__file__).with_name("pathlab.py")
LOADED_MODULE_NAME: str = "pathlab_under_bench"

FULL_GROUP_SIZES: tuple[int, ...] = (8, 16, 32)
FULL_SEEDS: tuple[int, ...] = (0, 1)
QUICK_MAP_KINDS: tuple[str, ...] = ("choke", "twogaps")
QUICK_GROUP_SIZES: tuple[int, ...] = (8, 16)
QUICK_SEEDS: tuple[int, ...] = (0,)
WORST_CASE_COUNT: int = 5


@dataclass
class CaseResult:
    name: str
    frames: int
    free_flow: int
    ratio: float
    blocked: int
    stuck: int
    cells: int
    repaths: int


def load_model(path: Path) -> ModuleType:
    """Under its own name, registered, because the model's `overriding` and dataclasses look
       themselves up in sys.modules."""
    spec = importlib.util.spec_from_file_location(LOADED_MODULE_NAME, path)
    if spec is None or spec.loader is None:
        raise SystemExit(f"--module: cannot load {path}")
    module = importlib.util.module_from_spec(spec)
    sys.modules[LOADED_MODULE_NAME] = module
    spec.loader.exec_module(module)
    return module


def run_until_settled(model: ModuleType, simulation: object) -> int:
    frames = 0
    while frames < model.MEASURE_MAX_FRAMES and not simulation.is_settled:
        simulation.step()
        frames += 1
    return frames


def solo_frames(model: ModuleType, grid: object, unit_id: int, start: tuple[float, float],
                goal: tuple[float, float]) -> int:
    walker = model.Unit(*start, id=unit_id)
    simulation = model.Simulation(grid, [walker])
    simulation.move_to(walker, goal)
    return run_until_settled(model, simulation)


def run_case(model: ModuleType, map_kind: str, scenario: str, unit_count: int,
             seeds: tuple[int, ...]) -> CaseResult:
    frames = free_flow = blocked = stuck = cells = repaths = 0
    for seed in seeds:
        grid = model.make_map(map_kind, seed=seed)
        units, orders = model.build_scenario(grid, unit_count, scenario)
        starts = {unit.id: unit.pos for unit in units}
        simulation = model.Simulation(grid, units)
        for group, click in orders:
            simulation.group_move(group, click)
        goals = {unit.id: unit.goal for unit in units}
        frames += run_until_settled(model, simulation)
        blocked += sum(unit.blocked_total for unit in units)
        stuck += sum(1 for unit in units if unit.moving or unit.gave_up)
        cells += simulation.pf.cells_total
        repaths += sum(unit.repaths for unit in units)
        free_flow += max((solo_frames(model, grid, unit_id, starts[unit_id], goal)
                          for unit_id, goal in goals.items() if goal is not None), default=0)
    return CaseResult(name=f"{map_kind}/{scenario}/{unit_count}", frames=frames, free_flow=free_flow,
                      ratio=frames / max(free_flow, 1), blocked=blocked, stuck=stuck, cells=cells,
                      repaths=repaths)


worker_model: ModuleType | None = None


def start_worker(module_path: Path) -> None:
    global worker_model
    worker_model = load_model(module_path)


def run_case_in_worker(case: tuple[str, str, int, tuple[int, ...]]) -> CaseResult:
    return run_case(worker_model, *case)


def summarize(cases: list[CaseResult]) -> dict[str, object]:
    worst = sorted(cases, key=lambda case: (-case.ratio, case.name))[:WORST_CASE_COUNT]
    return {
        "ratio_geometric_mean": math.exp(sum(math.log(case.ratio) for case in cases) / len(cases)),
        "stuck": sum(case.stuck for case in cases),
        "blocked": sum(case.blocked for case in cases),
        "cells": sum(case.cells for case in cases),
        "worst": [(case.name, case.ratio) for case in worst],
    }


def print_case(case: CaseResult) -> None:
    print(f"{case.name:<20} {case.frames:>6} {case.free_flow:>6} {case.ratio:>6.3f} {case.blocked:>7} "
          f"{case.stuck:>5} {case.cells:>8} {case.repaths:>6}", flush=True)


def print_totals(totals: dict[str, object], seconds: float) -> None:
    print(f"ratio geomean {totals['ratio_geometric_mean']:.4f}  stuck {totals['stuck']}  "
          f"blocked {totals['blocked']}  cells {totals['cells']}  ({seconds:.0f} s)")
    print("worst: " + "  ".join(f"{name} {ratio:.3f}" for name, ratio in totals["worst"]))


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--module", type=Path, default=DEFAULT_MODULE,
                        help="a copy of pathlab.py to score instead of the one beside this file")
    parser.add_argument("--quick", action="store_true", help="a subset, under twenty seconds")
    parser.add_argument("--json", type=Path, help="also write cases and totals to this file")
    parser.add_argument("--jobs", type=int, default=os.cpu_count(),
                        help="cases played at once; the numbers do not depend on it")
    args = parser.parse_args()

    model = load_model(args.module)
    map_kinds = QUICK_MAP_KINDS if args.quick else model.MAP_KINDS
    group_sizes = QUICK_GROUP_SIZES if args.quick else FULL_GROUP_SIZES
    seeds = QUICK_SEEDS if args.quick else FULL_SEEDS

    began = time.perf_counter()
    print(f"{'case':<20} {'frames':>6} {'free':>6} {'ratio':>6} {'blocked':>7} {'stuck':>5} "
          f"{'cells':>8} {'repath':>6}")
    suite = [(map_kind, scenario, unit_count, seeds) for map_kind in map_kinds
             for scenario in model.SCENARIOS for unit_count in group_sizes]
    cases: list[CaseResult] = []
    with ProcessPoolExecutor(max_workers=args.jobs, initializer=start_worker,
                             initargs=(args.module.resolve(),)) as pool:
        for case in pool.map(run_case_in_worker, suite):
            cases.append(case)
            print_case(case)
    totals = summarize(cases)
    print_totals(totals, time.perf_counter() - began)
    if args.json:
        report = {"suite": "quick" if args.quick else "full", "seeds": list(seeds),
                  "cases": [asdict(case) for case in cases], "totals": totals}
        args.json.write_text(json.dumps(report, indent=1) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
