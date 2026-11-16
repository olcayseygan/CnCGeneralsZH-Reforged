"""Equal-money armies of two unit types meet on open ground; the money each side has left is the answer.

Every cross-faction pair fights with the two starting points swapped, once per game seed: on generated
map 1 one start point won 118 of 183 decided fights in the first round, so one orientation measures the
ground, and a third of the pairs flip winner with it. duel_report.py averages the orientations and
gives the spread over the seeds.

    python duel.py <exe name in Run> <tag> [budget] [pair filter] [--seeds N]
"""

import argparse
import itertools
import json
import random
import re
import sys

import siege

BUDGET = 4800
SEEDS = 3
MAX_FRAMES = 3000 * siege.LOGIC_FPS // siege.SCENARIO_FPS  # logic frames; the rest are scenario frames
TALLY_FRAME = 2950
TALLY_EVERY = 30
SPAWN_FRAME = 30
ATTACK_FRAME = 60
# open ground on generated map 1, generator version 18, wide enough for 32 Rebels at spread 35:
# from (1200,1200) that many reach the cliff cells about (1112,1112) and the spawn is refused
LEFT_AT = (960, 825)
RIGHT_AT = (1300, 1170)
# The game seed alone barely moves a fight (Ranger against Rebel came out identical on seeds 1 and 2),
# so each seed also shifts both spots and the formation spread. This envelope passed 32 Rebels and
# 3 Overlords on seeds 1 to 12. Cliff cells near (990,915), (1370,1270), (1155,1055) and (830,705)
# refused spawns from wider ones, so a bigger envelope or a seed past 12 wants probing again.
JITTER = 20
SPREADS = (32, 37)


def layout(seed: int) -> tuple:
    """The seed's left spot, right spot and formation spread, the same in both orientations."""
    rng = random.Random(seed)
    shift = lambda at: (at[0] + rng.randint(-JITTER, JITTER), at[1] + rng.randint(-JITTER, JITTER))
    return shift(LEFT_AT), shift(RIGHT_AT), rng.randint(*SPREADS)

# the direct-fire ground core each side builds most of
UNITS = {
    "AmericaTankCrusader": 900, "AmericaTankPaladin": 1100, "AmericaVehicleHumvee": 700,
    "AmericaInfantryRanger": 225, "AmericaInfantryMissileDefender": 300,
    "ChinaTankBattleMaster": 800, "ChinaTankOverlord": 2000, "ChinaTankGattling": 800, "ChinaTankDragon": 800,
    "ChinaInfantryRedguard": 300, "ChinaInfantryTankHunter": 300,
    "GLATankScorpion": 600, "GLATankMarauder": 800, "GLAVehicleTechnical": 500, "GLAInfantryRebel": 150,
    "GLAInfantryTunnelDefender": 300, "GLAVehicleQuadCannon": 800, "GLAVehicleRocketBuggy": 900,
}


def count(unit: str, budget: int) -> int:
    return max(1, round(budget / UNITS[unit]))


def run_one(exe: str, tag: str, index: int, left: str, right: str, budget: int, swapped: bool, seed: int) -> dict:
    west, east, spread = layout(seed)
    left_at, right_at = (east, west) if swapped else (west, east)
    name = "dl%s%04d" % (tag, index)
    lines = [
        "%d spawn 0 %s %d %d %d %d" % (SPAWN_FRAME, left, count(left, budget), *left_at, spread),
        "%d spawn 1 %s %d %d %d %d" % (SPAWN_FRAME, right, count(right, budget), *right_at, spread),
        "%d attackmove 0 %s %d %d" % (ATTACK_FRAME, siege.selector(left), *right_at),
        "%d attackmove 1 %s %d %d" % (ATTACK_FRAME, siege.selector(right), *left_at),
    ]
    # The duel ends on the first tally where one army is gone. A winner that walks on into the loser's
    # start base can lose units to it or kill its command centre, which ends the match as decided
    # before TALLY_FRAME; neither is the duel. With both armies alive, the last tally both got.
    for frame in range(ATTACK_FRAME, TALLY_FRAME + 1, TALLY_EVERY):
        lines.append("%d tally 0 %s" % (frame, siege.selector(left)))
        lines.append("%d tally 1 %s" % (frame, siege.selector(right)))
    text = siege.launch(exe, name, lines, MAX_FRAMES, seed)
    tallies = {}
    for match in siege.TALLY.finditer(text or ""):
        # counted in units, not worth: a Technical's chassis templates carry no build cost
        slot = int(match.group(2))
        tallies[(int(match.group(1)), slot)] = int(match.group(4)) * UNITS[left if slot == 0 else right]
    common = sorted(frame for frame, slot in tallies if slot == 0 and (frame, 1) in tallies)
    end = next((frame for frame in common if 0 in (tallies[(frame, 0)], tallies[(frame, 1)])), common[-1] if common else None)
    return {"left": left, "right": right, "swapped": swapped, "seed": seed, "budget": budget, "endFrame": end,
            "leftAt": left_at, "rightAt": right_at, "spread": spread, "timedOut": text is None,
            "leftSpent": tallies.get((ATTACK_FRAME, 0)), "rightSpent": tallies.get((ATTACK_FRAME, 1)),
            "leftLeft": tallies.get((end, 0)), "rightLeft": tallies.get((end, 1))}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n", 1)[0])
    parser.add_argument("exe", help="exe name in Run, a copy so a rebuild does not pull it away")
    parser.add_argument("tag")
    parser.add_argument("budget", nargs="?", type=int, default=BUDGET)
    parser.add_argument("pair", nargs="?", default=".", help="regex over 'left right'")
    parser.add_argument("--seeds", type=int, default=SEEDS, help="game seeds 1..N, each pair both ways per seed")
    args = parser.parse_args()
    faction = lambda name: re.match(r"America|China|GLA", name).group(0)
    jobs = [(a, b, args.budget, swapped, seed) for a, b in itertools.combinations(UNITS, 2)
            if faction(a) != faction(b) and re.search(args.pair, a + " " + b)
            for swapped in (False, True) for seed in range(1, args.seeds + 1)]
    results = []
    siege.OUT_DIR.mkdir(exist_ok=True)
    try:
        for row in siege.run_all(lambda index, *job: run_one(args.exe, args.tag, index, *job),
                                 [(index, *job) for index, job in enumerate(jobs)]):
            results.append(row)
            print("%-32s %s/%s  vs  %-32s %s/%s  seed %d%s%s" % (row["left"], row["leftLeft"], row["leftSpent"], row["right"],
                                                               row["rightLeft"], row["rightSpent"], row["seed"],
                                                               "  swapped" if row["swapped"] else "",
                                                               "  TIMED OUT" if row["timedOut"] else ""), flush=True)
    finally:
        # a stopped sweep keeps what it finished
        (siege.OUT_DIR / ("duel_" + args.tag + ".json")).write_text(json.dumps(results, indent=1), encoding="utf-8")
    return 0


if __name__ == "__main__":
    sys.exit(main())
