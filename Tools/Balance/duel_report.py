"""Read a mirrored duel run: each pair fought both ways round on every seed, the money margins averaged.

    python duel_report.py <tag>

A share is a margin on each side's money kept as a fraction of what it fielded, (1 + kept - other's
kept) / 2, so the few hundred dollars whole units leave between the armies do not count and a wipe
still says what it cost: 1.0 wiped the other army without loss, 0.5 is even.

The two orientations of one seed share their spots and spread, so they are a pair: the seed's share
is the mean of the two, which cancels whichever side the terrain favours, and the pair's share and
standard error come from those per-seed means. A fight is clear when its share is further from 0.5
than t(0.975, seeds - 1) standard errors, the error floored at SE_FLOOR, and a draw otherwise; on one
seed there is no error and it takes CLEAR_WITHOUT_SE. A pair whose two orientations name different
winners on average is one the ground decided.
"""

import json
import statistics
import sys
from collections import defaultdict

from scipy.stats import t

import duel
from siege import OUT_DIR
EVEN = 0.5
# three seeds of the same two armies can agree to the hundredth; that is not a measurement that precise
SE_FLOOR = 0.02
CLEAR_WITHOUT_SE = 0.15


def margin(left: float, right: float) -> float:
    """1.0 is left wiping right without loss, 0.5 even, 0.0 the reverse; each side's input is the
    fraction of its army it kept."""
    return (1 + left - right) / 2


def share(row: dict) -> float:
    """Left's margin on money kept over money fielded: the armies are whole units, so 4800 buys five
    Crusaders (4500) against six Battlemasters (4800)."""
    return margin(row["leftLeft"] / row["leftSpent"], row["rightLeft"] / row["rightSpent"])


def counts(row: dict) -> tuple:
    """The whole units each side fielded: from the row's budget, or from what its start tally cost."""
    if row.get("budget"):
        return duel.count(row["left"], row["budget"]), duel.count(row["right"], row["budget"])
    return round(row["leftSpent"] / duel.UNITS[row["left"]]), round(row["rightSpent"] / duel.UNITS[row["right"]])


def pairs(tag: str) -> dict:
    """(left, right) -> share, se (None on one seed), clear, seeds, flipped, the two orientation means, counts."""
    rows = json.loads((OUT_DIR / ("duel_" + tag + ".json")).read_text())
    runs = defaultdict(dict)
    sizes = {}
    for row in rows:
        if row["leftLeft"] is None or row["rightLeft"] is None or not row["leftSpent"] or not row["rightSpent"]:
            print("dropped, no start or end tally: %s vs %s seed %s%s" % (row["left"], row["right"], row.get("seed", 1),
                                                                          " swapped" if row["swapped"] else ""))
            continue
        key = (row["left"], row["right"])
        runs[key][(row.get("seed", 1), row["swapped"])] = share(row)
        sizes[key] = counts(row)
    result = {}
    for pair, by_run in runs.items():
        seeds = sorted({seed for seed, _ in by_run if (seed, False) in by_run and (seed, True) in by_run})
        if not seeds:
            continue
        straight = [by_run[(seed, False)] for seed in seeds]
        swapped = [by_run[(seed, True)] for seed in seeds]
        per_seed = [(a + b) / 2 for a, b in zip(straight, swapped)]
        mean = statistics.fmean(per_seed)
        means = (statistics.fmean(straight), statistics.fmean(swapped))
        se = statistics.stdev(per_seed) / len(seeds) ** 0.5 if len(seeds) > 1 else None
        reach = t.ppf(0.975, len(seeds) - 1) * max(se, SE_FLOOR) if se is not None else CLEAR_WITHOUT_SE
        result[pair] = {"share": mean, "se": se, "clear": abs(mean - EVEN) > reach, "seeds": len(seeds),
                        "flipped": (means[0] - EVEN) * (means[1] - EVEN) < 0, "orientations": means,
                        "counts": sizes[pair]}
    return result


def main() -> int:
    matchups = pairs(sys.argv[1])
    per_unit = defaultdict(list)
    for (left, right), pair in matchups.items():
        per_unit[left].append(pair["share"])
        per_unit[right].append(1 - pair["share"])

    print("unit                             mean share  clear wins  pairs")
    for unit, shares in sorted(per_unit.items(), key=lambda kv: -statistics.fmean(kv[1])):
        wins = sum(1 for (left, right), pair in matchups.items() if pair["clear"]
                   and unit == (left if pair["share"] > EVEN else right))
        print("%-32s %10.2f  %10d  %5d" % (unit, statistics.fmean(shares), wins, len(shares)))
    print("\n%d of %d pairs flipped winner with the ground" % (sum(p["flipped"] for p in matchups.values()), len(matchups)))
    print("\nevery pair, most one-sided first (winner's share, SE over seeds, then the first-named unit's"
          " share in each orientation):")
    for (left, right), pair in sorted(matchups.items(), key=lambda kv: -abs(kv[1]["share"] - EVEN)):
        mean = pair["share"]
        winner, loser = (left, right) if mean > EVEN else (right, left)
        se = "+-%.2f" % pair["se"] if pair["se"] is not None else "  -  "
        print("  %-30s %s %-30s %.2f %s  (%.2f / %.2f)%s" % (winner, "beats" if pair["clear"] else "draws", loser,
                                                           max(mean, 1 - mean), se, *pair["orientations"],
                                                           "  ground" if pair["flipped"] else ""))
    return 0


if __name__ == "__main__":
    sys.exit(main())
