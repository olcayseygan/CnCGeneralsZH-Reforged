"""
bench - one fixed, deterministic score for PathLab's movement model.

Every map kind and scenario at a few group sizes of the default tank (a Crusader read from the
install), each over the same seeds, then MIXED_CASES: mixed groups and infantry.  A case's free-flow
bound is the slowest member walking to its own assigned goal alone on the map, so the ratio of the
group's frames to it is what the crowd cost.  Same input, same numbers; with no install the units
are the stand-in tank and the numbers are not baseline.json's.

    python bench.py                         # the full suite, about two minutes
    python bench.py --quick                 # a subset, under twenty seconds
    python bench.py --module idea.py        # score a modified copy of pathlab.py
    python bench.py --json baseline.json    # also write the numbers
    python bench.py --seeds 2-7             # held-out maps; two seeds carry about 0.03 of noise
    python bench.py --proposed              # the stuck ladder's PROPOSED_LADDER values
    python bench.py --ttc                   # PROPOSED_TTC, steering by time to collision; mixes with --proposed
    python bench.py --dither                # PROPOSED_DITHER, the ladder seeing a turn on the spot; mixes with both
    python bench.py --spot                  # PROPOSED_FROM_SPOT, a lane starting from the member's spot; mixes with all

    python bench.py --set PF_TRAFFIC_NUM=0 --sizes 4,12,20   # one of the model's constants, other sizes
    python bench.py --maps "map:Tournament Desert" --sizes 12 # a retail map from the install, played once

`overlap` counts pair-ticks where two bodies overlap and `depth` is their mean overlap in world units.
The last four columns say whether the group went where it was clicked, in world units and per order:
`cent` the final centroid's distance from the click, `worst` the farthest member's, `lat` the mean of
each member's worst sideways or backward excursion off its own straight line to the click while it
covers the first third of that line, and `detour` the mean distance driven.  The last two are against
the same member driving alone to its own goal, so a rock in the way, which turns a lone unit as much
as a crowd, cancels: `lat` is the excess in world units and `detour` the ratio, 1.0 when the crowd
cost nothing.
"""

import argparse
import ast
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
# after the map x scenario x size grid of the default tank: mixed groups and infantry, NAME:COUNT,...
MIXED_CASES: tuple[tuple[str, str, str], ...] = (
    ("open", "column", "crusader:8,humvee:4,overlord:2"),
    ("twogaps", "cross", "scorpion:6,technical:4,battlemaster:4,humvee:2"),
    ("choke", "column", "ranger:16"),
    ("open", "cross", "ranger:8,crusader:8"),
)
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
    churn: int
    rungs: list[int]
    overlap: int
    overlap_depth: float
    # where the order took the group, against where the player clicked (world units, means per order)
    centroid_off: float = 0.0       # the group's final centroid from the click
    worst_off: float = 0.0          # its member ending farthest from the click
    lateral: float = 0.0            # mean worst sideways or backward excursion off the member's own
                                    # line to the click over its first third, less the same member's alone
    detour: float = 0.0             # mean distance driven over what the member drives alone to its goal


GAME_MAP_PREFIX: str = "map:"       # --maps map:Tournament Desert plays a retail map from the install
EARLY_SHARE: float = 1.0 / 3.0     # how much of the line to the click counts as setting off
SOLO_MIN_DRIVEN: float = 10.0       # a member whose goal is where it stands drives this far, not 0


class OrderTrack:
    """One order's members from their start toward the click: the sideways and backward excursion
       from that line while the member covers its first third, and the distance driven."""

    def __init__(self, members: list[object], click: tuple[float, float]) -> None:
        self.members = members
        self.click = click
        self.lines = []
        for unit in members:
            dx, dy = click[0] - unit.x, click[1] - unit.y
            length = math.hypot(dx, dy)
            self.lines.append((unit.x, unit.y, dx / max(length, 1e-9), dy / max(length, 1e-9), length))
        self.excursion = [0.0] * len(members)
        self.early = [True] * len(members)
        self.driven = [0.0] * len(members)
        self.last = [unit.pos for unit in members]

    def sample(self) -> None:
        for k, unit in enumerate(self.members):
            lx, ly = self.last[k]
            self.driven[k] += math.hypot(unit.x - lx, unit.y - ly)
            self.last[k] = unit.pos
            if not self.early[k]:
                continue
            sx, sy, ux, uy, length = self.lines[k]
            along = (unit.x - sx) * ux + (unit.y - sy) * uy
            if along >= length * EARLY_SHARE:
                self.early[k] = False
                continue
            off = max(abs((unit.x - sx) * -uy + (unit.y - sy) * ux), -along)
            self.excursion[k] = max(self.excursion[k], off)

    def result(self, solos: list["OrderTrack"]) -> tuple[float, float, float, float]:
        """`solos` holds each member's walk alone to its own goal, so the map's own rocks, which push
           a lone unit off the line as much as a crowd, cancel out of `lateral` and `detour`."""
        n = max(len(self.members), 1)
        mx = sum(unit.x for unit in self.members) / n
        my = sum(unit.y for unit in self.members) / n
        cx, cy = self.click
        centroid = math.hypot(mx - cx, my - cy)
        worst = max((math.hypot(unit.x - cx, unit.y - cy) for unit in self.members), default=0.0)
        lateral = sum(own - solo.excursion[0] for own, solo in zip(self.excursion, solos)) / n
        detour = sum(own / max(solo.driven[0], SOLO_MIN_DRIVEN) for own, solo in zip(self.driven, solos)) / n
        return centroid, worst, lateral, detour


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


def solo_run(model: ModuleType, grid: object, unit_id: int, kind: object, start: tuple[float, float],
             goal: tuple[float, float] | None, click: tuple[float, float]) -> tuple[int, OrderTrack]:
    """The member alone on the map, driving to its own goal: its frames, and its track toward the click."""
    walker = model.Unit(*start, id=unit_id, kind=kind)
    simulation = model.Simulation(grid, [walker])
    track = OrderTrack([walker], click)
    if goal is None:
        return 0, track
    simulation.move_to(walker, goal)
    frames = 0
    while frames < model.MEASURE_MAX_FRAMES and not simulation.is_settled:
        simulation.step()
        frames += 1
        track.sample()
    return frames, track


def group_of(model: ModuleType, group: str) -> list[object]:
    """A bare count is that many of the model's default tank, else a NAME:COUNT,... mix."""
    if group.isdigit():
        return [model.default_kind()] * int(group)
    return model.parse_group(group, model.game_kinds())


def run_case(model: ModuleType, map_kind: str, scenario: str, group: str,
             seeds: tuple[int, ...]) -> CaseResult:
    frames = free_flow = blocked = stuck = cells = repaths = churn = overlap = 0
    overlap_depth = 0.0
    rungs = [0, 0, 0]
    placement = [0.0, 0.0, 0.0, 0.0]
    order_count = 0
    kinds = group_of(model, group)
    game_map = map_kind[len(GAME_MAP_PREFIX):] if map_kind.startswith(GAME_MAP_PREFIX) else ""
    for seed in seeds[:1] if game_map else seeds:      # a retail map is the same map every seed
        grid, units, orders = model.scenario_setup(map_kind, game_map, seed, kinds, scenario)
        starts = {unit.id: (unit.kind, unit.pos) for unit in units}
        simulation = model.Simulation(grid, units)
        tracks = [OrderTrack(list(members), click) for members, click in orders]
        for members, click in orders:
            simulation.group_move(members, click)
        goals = {unit.id: unit.goal for unit in units}
        played = 0
        while played < model.MEASURE_MAX_FRAMES and not simulation.is_settled:
            simulation.step()
            played += 1
            for track in tracks:
                track.sample()
        frames += played
        slowest = 0
        for track in tracks:
            solos = []
            for unit in track.members:
                solo_frames, solo = solo_run(model, grid, unit.id, *starts[unit.id], goals[unit.id], track.click)
                slowest = max(slowest, solo_frames)
                solos.append(solo)
            placement = [total + value for total, value in zip(placement, track.result(solos))]
            order_count += 1
        free_flow += slowest
        blocked += sum(unit.blocked_total for unit in units)
        stuck += sum(1 for unit in units if unit.moving or unit.gave_up)
        cells += simulation.pf.cells_total
        repaths += sum(unit.repaths for unit in units)
        churn += sum(unit.churn for unit in units)
        overlap += simulation.overlap_ticks
        overlap_depth += simulation.overlap_depth
        for unit in units:
            rungs = [total + fired for total, fired in zip(rungs, unit.rungs_fired)]
    return CaseResult(name=f"{map_kind}/{scenario}/{group}", frames=frames, free_flow=free_flow,
                      ratio=frames / max(free_flow, 1), blocked=blocked, stuck=stuck, cells=cells,
                      repaths=repaths, churn=churn, rungs=rungs, overlap=overlap,
                      overlap_depth=overlap_depth / max(overlap, 1),
                      **dict(zip(("centroid_off", "worst_off", "lateral", "detour"),
                                 (value / max(order_count, 1) for value in placement))))


def parse_seeds(text: str) -> tuple[int, ...]:
    """FIRST-LAST, both ends played, or a comma list."""
    first, dash, last = text.partition("-")
    if dash:
        return tuple(range(int(first), int(last) + 1))
    return tuple(int(seed) for seed in text.split(","))


worker_model: ModuleType | None = None


def parse_setting(text: str) -> tuple[str, object]:
    """NAME=VALUE, the value read as a Python literal."""
    name, _, value = text.partition("=")
    return name.strip(), ast.literal_eval(value.strip())


def start_worker(module_path: Path, proposed: bool, ttc: bool, dither: bool, spot: bool,
                 extra: tuple[tuple[str, object], ...] = ()) -> None:
    global worker_model
    worker_model = load_model(module_path)
    settings = {**(worker_model.PROPOSED_LADDER if proposed else {}),
                **(worker_model.PROPOSED_TTC if ttc else {}),
                **(worker_model.PROPOSED_DITHER if dither else {}),
                **(worker_model.PROPOSED_FROM_SPOT if spot else {}),
                **dict(extra)}
    for name in settings:
        if not hasattr(worker_model, name):
            raise SystemExit(f"--set: the model has no {name}")
    for name, value in settings.items():
        setattr(worker_model, name, value)


def run_case_in_worker(case: tuple[str, str, str, tuple[int, ...]]) -> CaseResult:
    return run_case(worker_model, *case)


def summarize(cases: list[CaseResult]) -> dict[str, object]:
    worst = sorted(cases, key=lambda case: (-case.ratio, case.name))[:WORST_CASE_COUNT]
    overlap = sum(case.overlap for case in cases)
    return {
        "ratio_geometric_mean": math.exp(sum(math.log(case.ratio) for case in cases) / len(cases)),
        "stuck": sum(case.stuck for case in cases),
        "blocked": sum(case.blocked for case in cases),
        "cells": sum(case.cells for case in cases),
        "worst": [(case.name, case.ratio) for case in worst],
        "churn": sum(case.churn for case in cases),
        "rungs": [sum(case.rungs[rung] for case in cases) for rung in range(3)],
        "overlap": overlap,
        "overlap_depth": sum(case.overlap * case.overlap_depth for case in cases) / max(overlap, 1),
        **{name: sum(getattr(case, name) for case in cases) / len(cases)
           for name in ("centroid_off", "worst_off", "lateral", "detour")},
        "frames": sum(case.frames for case in cases),
    }


def print_case(case: CaseResult) -> None:
    print(f"{case.name:<20} {case.frames:>6} {case.free_flow:>6} {case.ratio:>6.3f} {case.blocked:>7} "
          f"{case.stuck:>5} {case.cells:>8} {case.repaths:>6} {case.churn:>6} "
          f"{'/'.join(map(str, case.rungs)):>12} {case.overlap:>7} {case.overlap_depth:>5.2f} "
          f"{case.centroid_off:>6.1f} {case.worst_off:>6.1f} {case.lateral:>6.1f} {case.detour:>6.3f}", flush=True)


def print_totals(totals: dict[str, object], seconds: float) -> None:
    print(f"ratio geomean {totals['ratio_geometric_mean']:.4f}  stuck {totals['stuck']}  "
          f"blocked {totals['blocked']}  cells {totals['cells']}  churn {totals['churn']}  "
          f"rungs {'/'.join(map(str, totals['rungs']))}  overlap {totals['overlap']}  "
          f"depth {totals['overlap_depth']:.2f}  ({seconds:.0f} s)")
    print(f"click: centroid {totals['centroid_off']:.1f}  worst {totals['worst_off']:.1f}  "
          f"lateral {totals['lateral']:.1f}  detour {totals['detour']:.3f}  frames {totals['frames']}")
    print("worst: " + "  ".join(f"{name} {ratio:.3f}" for name, ratio in totals["worst"]))


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--module", type=Path, default=DEFAULT_MODULE,
                        help="a copy of pathlab.py to score instead of the one beside this file")
    parser.add_argument("--quick", action="store_true", help="a subset, under twenty seconds")
    parser.add_argument("--json", type=Path, help="also write cases and totals to this file")
    parser.add_argument("--jobs", type=int, default=os.cpu_count(),
                        help="cases played at once; the numbers do not depend on it")
    parser.add_argument("--seeds", type=parse_seeds,
                        help="FIRST-LAST or a,b,c: the maps to play; two seeds carry about 0.03 of noise "
                             "in the geomean, so argue a small change on held-out ones")
    parser.add_argument("--proposed", action="store_true",
                        help="play with the model's PROPOSED_LADDER values instead of the game's")
    parser.add_argument("--ttc", action="store_true",
                        help="play with the model's PROPOSED_TTC values: time-to-collision steering, "
                             "no contact speed cap")
    parser.add_argument("--dither", action="store_true",
                        help="play with the model's PROPOSED_DITHER: a turn on the spot or back and forth "
                             "is not progress to the stuck ladder")
    parser.add_argument("--spot", action="store_true",
                        help="play with the model's PROPOSED_FROM_SPOT: a lane starts from the member's own "
                             "spot instead of closing to the middle member first")
    parser.add_argument("--set", type=parse_setting, action="append", default=[], metavar="NAME=VALUE",
                        help="one of the model's constants, as a Python literal; repeat for more")
    parser.add_argument("--sizes", help="group sizes for the grid instead of 8,16,32 (drops MIXED_CASES)")
    parser.add_argument("--maps", help="map kinds for the grid, comma list; map:NAME is a retail map")
    args = parser.parse_args()

    model = load_model(args.module)
    map_kinds = (tuple(args.maps.split(",")) if args.maps
                 else QUICK_MAP_KINDS if args.quick else model.MAP_KINDS)
    group_sizes = (tuple(int(size) for size in args.sizes.split(",")) if args.sizes
                   else QUICK_GROUP_SIZES if args.quick else FULL_GROUP_SIZES)
    seeds = args.seeds or (QUICK_SEEDS if args.quick else FULL_SEEDS)

    began = time.perf_counter()
    print(f"ladder {'proposed' if args.proposed else 'game'}  steering {'ttc' if args.ttc else 'game'}  "
          f"dither {'fixed' if args.dither else 'game'}  lanes {'spot' if args.spot else 'game'}  seeds {','.join(map(str, seeds))}")
    print(f"{'case':<20} {'frames':>6} {'free':>6} {'ratio':>6} {'blocked':>7} {'stuck':>5} "
          f"{'cells':>8} {'repath':>6} {'churn':>6} {'rungs 1/2/3':>12} {'overlap':>7} {'depth':>5} "
          f"{'cent':>6} {'worst':>6} {'lat':>6} {'detour':>6}")
    if args.set:
        print("set " + "  ".join(f"{name}={value!r}" for name, value in args.set))
    suite = [(map_kind, scenario, str(unit_count), seeds) for map_kind in map_kinds
             for scenario in model.SCENARIOS for unit_count in group_sizes]
    if not (args.quick or args.sizes or args.maps):
        suite += [(map_kind, scenario, group, seeds) for map_kind, scenario, group in MIXED_CASES]
    cases: list[CaseResult] = []
    with ProcessPoolExecutor(max_workers=args.jobs, initializer=start_worker,
                             initargs=(args.module.resolve(), args.proposed, args.ttc, args.dither, args.spot,
                                       tuple(args.set))) as pool:
        for case in pool.map(run_case_in_worker, suite):
            cases.append(case)
            print_case(case)
    totals = summarize(cases)
    print_totals(totals, time.perf_counter() - began)
    if args.json:
        report = {"suite": "quick" if args.quick else "full", "seeds": list(seeds),
                  "cases": [asdict(case) for case in cases], "totals": totals,
                  "ladder": "proposed" if args.proposed else "game",
                  "steering": "ttc" if args.ttc else "game",
                  "dither": "fixed" if args.dither else "game",
                  "lanes": "spot" if args.spot else "game",
                  "set": {name: value for name, value in args.set}}
        args.json.write_text(json.dumps(report, indent=1) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
