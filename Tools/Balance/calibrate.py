"""Where the AI's matchup arithmetic and the real fight disagree: each duel pair's measured share next
to the share unitmatrix.py's frames-to-kill predicts for the same two equal-money armies.

    python calibrate.py <duel tag>

The prediction is Lanchester's square law on the matrix's kill times: a side's strength is
n^2 / kill[a][b] seconds, the stronger wipes the other and keeps sqrt(1 - weaker / stronger) of its
army, and that goes through the same margin duel_report.py measures, (1 + kept - other's kept) / 2.
A pair is flagged when the two name different winners of a fight duel_report.py calls clear (further
from 0.5 than t(0.975, seeds - 1) floored standard errors). The prediction counts the whole units the
run really fielded, read from the budget each duel row stores (five Crusaders at 4800, 4500 of it
spent), and the measured margin is on each side's fraction kept, so both read the same uneven fight
in the same terms.
"""

import json
import subprocess
import sys
import tempfile
from pathlib import Path

import duel_report

EVEN = duel_report.EVEN


def kill_seconds() -> dict:
    """(attacker, target) -> seconds, None where the attacker cannot kill it, from unitmatrix.py --json."""
    with tempfile.TemporaryDirectory() as scratch:
        out = Path(scratch) / "matrix.json"
        subprocess.run([sys.executable, str(Path(__file__).with_name("unitmatrix.py")), "--json", str(out)],
                       check=True, stdout=subprocess.DEVNULL)
        matrix = json.loads(out.read_text(encoding="utf-8"))
    ids = [unit["id"] for unit in matrix["units"]]
    return {(ids[a], ids[t]): seconds for a, row in enumerate(matrix["kill"]) for t, seconds in enumerate(row)}


def predicted(left: str, right: str, counts: tuple, kills: dict) -> float | None:
    """Square law: strength n^2 / kill seconds; the winner keeps sqrt(1 - loser's / winner's) of its army."""
    if (left, right) not in kills or (right, left) not in kills:
        return None
    left_strength = counts[0] ** 2 / kills[(left, right)] if kills[(left, right)] else 0.0
    right_strength = counts[1] ** 2 / kills[(right, left)] if kills[(right, left)] else 0.0
    if left_strength == right_strength:
        return None if left_strength == 0 else EVEN
    if left_strength > right_strength:
        return duel_report.margin((1 - right_strength / left_strength) ** 0.5, 0.0)
    return duel_report.margin(0.0, (1 - left_strength / right_strength) ** 0.5)


def main() -> int:
    kills = kill_seconds()
    rows = []
    for (left, right), pair in duel_report.pairs(sys.argv[1]).items():
        guess = predicted(left, right, pair["counts"], kills)
        if guess is None:
            print("no matrix prediction for %s vs %s" % (left, right))
            continue
        measured = pair["share"]
        wrong = pair["clear"] and (measured - EVEN) * (guess - EVEN) < 0
        rows.append((wrong, abs(measured - guess), left, right, measured, pair["se"], guess))

    rows.sort(key=lambda row: (not row[0], -row[1]))
    print("%-30s %-30s %8s %6s %9s %6s" % ("first", "second", "measured", "SE", "predicted", "gap"))
    for wrong, gap, left, right, measured, se, guess in rows:
        print("%-30s %-30s %8.2f %6s %9.2f %6.2f%s" % (left, right, measured, "%.2f" % se if se is not None else "-",
                                                      guess, gap, "  WRONG WINNER" if wrong else ""))
    print("\n%d of %d pairs: the matrix names the wrong winner of a clear fight" % (sum(row[0] for row in rows), len(rows)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
