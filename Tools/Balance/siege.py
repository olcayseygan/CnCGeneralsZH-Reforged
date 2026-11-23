"""Stage every attacker against every stock base defence in the real engine and read the money lost.

One scenario per (defence, attacker, budget, approach). The attackers spend budget x the defence's
EA price (garrison included), walk at it over the open sand of generated map 1 from the west or the
north, and are ordered to attack it. HEADLESS TALLY lines at the start and the end give each side's
count, health and worth. Results land in Run/Balance/<tag>.json; compare.py reads two of them.

    python siege.py <exe name in Run> <tag> [defence filter] [attacker filter]

The exe reads Run/Data/INI/BalanceReforged.ini like the game does, so an A/B of two balance files
is two runs with the file swapped between them, never two at once.
"""

import concurrent.futures
import json
import os
import re
import subprocess
import sys
import threading
from pathlib import Path

# the game's data sits beside the exe; a worktree without the archives points this at a Run that has them
RUN_DIR = Path(os.environ.get("ZHR_RUN_DIR", Path(__file__).resolve().parents[2] / "GeneralsMD" / "Run"))
SCENARIO_DIR = RUN_DIR / "Scenarios"
OUT_DIR = RUN_DIR / "Balance"
PARALLEL_RUNS = 5
TIMEOUT_SECONDS = 600
# A scenario file counts retail's 30 Hz frames (ScenarioDrill_tick converts them) and its tally lines
# print them back, but -maxframes counts the logic's own frames, LOGICFRAMES_PER_SECOND a second, read
# from the engine's own header so this script never carries a copy of the rate
SCENARIO_FPS = 30
LOGIC_FPS = int(re.search(r"^\s*LOGICFRAMES_PER_SECOND\s*=\s*(\d+)",
                          (Path(__file__).resolve().parents[2] / "GeneralsMD" / "Code" / "GameEngine" / "Include" / "Common"
                           / "GameCommon.h").read_text(errors="replace"), re.M).group(1))
MAX_FRAMES = 3600 * LOGIC_FPS // SCENARIO_FPS
TALLY_FRAME = 3550
SPAWN_FRAME = 30
ENTER_FRAME = 40
ATTACK_FRAME = 120
# spots on generated map 1 as generator version 18 lays it out; a generator change can turn any of
# them into restricted ground, and then launch() stops the run on the scenario's "refused" line
DEFENCE_AT = (1200, 1200)
POWER_AT = (1400, 800)
# west and north: the ground from the south-west hides the infantry from a defence's line of sight.
# The west spot keeps clear of the cliff cells about (770-840, 1130-1200), where a big army's spread
# from (750,1200) put a Ranger and got the spawn refused. The north spot was (1200,1650) until a later
# generator put a cliff there; (1000,1600) is open ground.
APPROACHES = ((700, 1300), (1000, 1600))
GARRISON_AT = (1280, 1200)
BUDGETS = (1, 2)

# name: (EA's price, power plant or None, garrison template, garrison count). The army is sized on
# EA's price whatever the balance file charges, so two balance files meet the same army.
DEFENCES = {
    "AmericaPatriotBattery": (1000, "AmericaPowerPlant", None, 0),
    "ChinaGattlingCannon": (1200, "ChinaPowerPlant", None, 0),
    "GLAStingerSite": (900, None, None, 0),
    "GLATunnelNetwork": (800, None, None, 0),
    "ChinaBunker": (400, None, "ChinaInfantryTankHunter", 5),
    "AmericaFireBase": (1000, "AmericaPowerPlant", "AmericaInfantryMissileDefender", 4),
}
GARRISON_PRICE = {"ChinaInfantryTankHunter": 300, "AmericaInfantryMissileDefender": 300}
ATTACKERS = {
    "AmericaTankCrusader": 900, "AmericaTankPaladin": 1100, "AmericaVehicleHumvee": 700,
    "AmericaInfantryRanger": 225, "AmericaInfantryMissileDefender": 300, "AmericaVehicleTomahawk": 1200,
    "ChinaTankBattleMaster": 800, "ChinaTankOverlord": 2000, "ChinaTankGattling": 800, "ChinaTankDragon": 800,
    "ChinaInfantryRedguard": 300, "ChinaInfantryTankHunter": 300, "ChinaVehicleInfernoCannon": 900,
    "ChinaVehicleNukeLauncher": 1600,
    "GLATankScorpion": 600, "GLATankMarauder": 800, "GLAVehicleTechnical": 500, "GLAInfantryRebel": 150,
    "GLAInfantryTunnelDefender": 300, "GLAVehicleRocketBuggy": 900, "GLAVehicleQuadCannon": 700,
    "GLAVehicleScudLauncher": 1200,
}
# a Technical spawns as one of its chassis templates
CHASSIS_UNITS = {"GLAVehicleTechnical"}
TALLY = re.compile(r"HEADLESS TALLY: frame (\d+) slot (\d+) '(\S+)': (\d+) alive, health (\d+) of (\d+), worth (\d+)")
# what ScenarioDrill.cpp logs when the scenario did not put on the field what the file asked for
REFUSED = re.compile(r"SCENARIO: .*(refused at|no template named|is no template|cannot open|read as nothing"
                     r"|[1-9]\d* lines refused|owns nothing matching|this map has no|nobody is sitting in slot).*")
# the games running now, so a stopped sweep can kill them by their own handles, never by name
LIVE = set()
LIVE_LOCK = threading.Lock()
STOPPED = threading.Event()


class ScenarioRefused(RuntimeError):
    """A spawn or line the engine would not take: whatever the run tallies afterwards is not the fight asked for."""


def seat_faction(template: str, default: str) -> str:
    """A Turkiye template spawned for a seat of another faction is gone before the first tally, so a
    Turkiye unit or defence takes a Turkiye seat; every other template fights from the default one."""
    return "FactionTurkiye" if template.startswith("Turkiye") else default


def launch(exe: str, name: str, lines: list[str], max_frames: int, seed: int = 1,
           sides: tuple[str, str] = ("FactionAmerica", "FactionChina")) -> str | None:
    """Play one scenario headless on generated map 1 and return its debug log, None when it ran out of time."""
    scenario = SCENARIO_DIR / (name + ".txt")
    scenario.write_text("\n".join(lines) + "\n", encoding="ascii")
    log = RUN_DIR / (name + "_DebugLogFile.txt")
    log.unlink(missing_ok=True)
    arguments = [str(RUN_DIR / exe), "-headless", "-quickstart", "-noshellmap", "-observer", "-multiInstance",
                 "-noFPSLimit", "-randommap", "1", "2", "small", "-autoskirmish", "2", "-takeover",
                 "-side", "0", sides[0], "-side", "1", sides[1], "-seed", str(seed),
                 "-maxframes", str(max_frames), "-scenario", name, "-logPrefix", name + "_"]
    with LIVE_LOCK:
        if STOPPED.is_set():
            scenario.unlink()
            raise RuntimeError("%s: the sweep was stopped" % name)
        process = subprocess.Popen(arguments, cwd=RUN_DIR, creationflags=subprocess.ABOVE_NORMAL_PRIORITY_CLASS)
        LIVE.add(process)
    try:
        process.wait(timeout=TIMEOUT_SECONDS)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait()
        print("%s: no end in %d s, killed; its log is %s" % (name, TIMEOUT_SECONDS, log), file=sys.stderr, flush=True)
        return None
    finally:
        with LIVE_LOCK:
            LIVE.discard(process)
        scenario.unlink(missing_ok=True)
    text = log.read_text(encoding="latin-1", errors="replace") if log.exists() else ""
    log.unlink(missing_ok=True)
    refused = [match.group(0) for match in REFUSED.finditer(text)]
    if refused:
        raise ScenarioRefused("%s: %s" % (name, "; ".join(refused)))
    return text


def run_all(work, jobs):
    """Yield work(job) for every job as it finishes, PARALLEL_RUNS at a time; a refused scenario stops the
    whole run, since the same spot is refused in every job that uses it."""
    with concurrent.futures.ThreadPoolExecutor(PARALLEL_RUNS) as pool:
        futures = [pool.submit(work, *job) for job in jobs]
        try:
            for future in concurrent.futures.as_completed(futures):
                yield future.result()
        except BaseException:
            pool.shutdown(wait=False, cancel_futures=True)
            with LIVE_LOCK:
                STOPPED.set()
                for process in LIVE:
                    process.kill()
            raise


def selector(template: str) -> str:
    return template + "*" if template in CHASSIS_UNITS else template


def scenario_text(defence: str, attacker: str, count: int, approach: int) -> list[str]:
    price, power, garrison, garrison_count = DEFENCES[defence]
    lines = ["%d spawn 1 %s 1 %d %d" % (SPAWN_FRAME, defence, *DEFENCE_AT)]
    if power:
        lines.append("%d spawn 1 %s 1 %d %d" % (SPAWN_FRAME, power, *POWER_AT))
    if garrison:
        lines.append("%d spawn 1 %s %d %d %d 15" % (SPAWN_FRAME, garrison, garrison_count, *GARRISON_AT))
        lines.append("%d enter 1 %s 1 %s" % (ENTER_FRAME, garrison, defence))
    lines.append("%d spawn 0 %s %d %d %d 35" % (SPAWN_FRAME, attacker, count, *APPROACHES[approach]))
    lines.append("%d attack 0 %s 1 %s" % (ATTACK_FRAME, selector(attacker), defence))
    for frame in (ATTACK_FRAME, TALLY_FRAME):
        lines.append("%d tally 0 %s" % (frame, selector(attacker)))
        lines.append("%d tally 1 %s" % (frame, defence))
        if garrison:
            lines.append("%d tally 1 %s" % (frame, garrison))
    return lines


def run_one(exe: str, tag: str, index: int, defence: str, attacker: str, budget: int, approach: int) -> dict:
    price, _power, garrison, garrison_count = DEFENCES[defence]
    total = price + (GARRISON_PRICE[garrison] * garrison_count if garrison else 0)
    count = max(1, round(budget * total / ATTACKERS[attacker]))
    # the log prefix is cut at about thirty characters, so the name has to be short and unique
    name = "sg%s%03d" % (tag, index)
    sides = (seat_faction(attacker, "FactionAmerica"), seat_faction(defence, "FactionChina"))
    text = launch(exe, name, scenario_text(defence, attacker, count, approach), MAX_FRAMES, sides=sides)
    if text is None:
        return {"defence": defence, "attacker": attacker, "budget": budget, "approach": approach, "error": "timeout",
                "tallies": ""}
    tallies = {}
    for match in TALLY.finditer(text):
        frame, name = int(match.group(1)), match.group(3)
        tallies[(frame, name)] = {"alive": int(match.group(4)), "health": int(match.group(5)),
                                      "maxHealth": int(match.group(6)), "worth": int(match.group(7))}
    start = lambda name: tallies.get((ATTACK_FRAME, selector(name)))
    end = lambda name: tallies.get((TALLY_FRAME, selector(name)))
    if not (start(attacker) and end(attacker) and start(defence) and end(defence)):
        return {"defence": defence, "attacker": attacker, "budget": budget, "error": "missing tally", "tallies": str(tallies)}
    garrison_lost = 0
    if garrison:
        garrison_lost = start(garrison)["worth"] - end(garrison)["worth"] if start(garrison) and end(garrison) else 0
    return {
        "defence": defence, "attacker": attacker, "budget": budget, "approach": approach, "count": count, "defencePrice": total,
        # counted in units, not worth: a Technical's chassis templates carry no build cost
        "attackerSpent": start(attacker)["alive"] * ATTACKERS[attacker],
        "attackerLost": (start(attacker)["alive"] - end(attacker)["alive"]) * ATTACKERS[attacker],
        "defenceStanding": end(defence)["alive"], "defenceHealth": end(defence)["health"] / max(1, start(defence)["maxHealth"]),
        "garrisonLost": garrison_lost, "defenceWorth": start(defence)["worth"],
    }


def main() -> int:
    exe, tag = sys.argv[1], sys.argv[2]
    defence_filter = re.compile(sys.argv[3] if len(sys.argv) > 3 else ".")
    attacker_filter = re.compile(sys.argv[4] if len(sys.argv) > 4 else ".")
    jobs = [(d, a, b, approach) for d in DEFENCES if defence_filter.search(d) for a in ATTACKERS if attacker_filter.search(a)
            for b in BUDGETS for approach in range(len(APPROACHES))]
    OUT_DIR.mkdir(exist_ok=True)
    results = []
    try:
        for row in run_all(lambda index, *job: run_one(exe, tag, index, *job),
                           [(index, *job) for index, job in enumerate(jobs)]):
            results.append(row)
            if "error" in row:
                print("%-24s %-32s x%d ERROR %s %s" % (row["defence"], row["attacker"], row["budget"], row["error"],
                                                       row["tallies"]), flush=True)
            else:
                print("%-24s %-32s x%d  %2d units  lost $%5d of $%5d  defence %s %.0f%%" % (
                    row["defence"], row["attacker"], row["budget"], row["count"], row["attackerLost"],
                    row["attackerSpent"], "STANDS" if row["defenceStanding"] else "dead  ", row["defenceHealth"] * 100),
                    flush=True)
    finally:
        # a stopped sweep keeps what it finished
        (OUT_DIR / (tag + ".json")).write_text(json.dumps(results, indent=1), encoding="utf-8")
    return 0


if __name__ == "__main__":
    sys.exit(main())
