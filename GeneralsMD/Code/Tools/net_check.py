"""Play one network match between copies of the game on this machine and say whether they agreed.

Each copy is started with -netgame over loopback addresses and its own -control port. Once the
match is up, every copy is sent Ctrl+A, X, S, X in turn: `key` is the one control command that
travels through the network, so these become orders both machines execute on a stamped frame.
World commands (spawn, move) run on one machine only, which is what --prove uses to force a
desync and show the check is looking.

    python net_check.py                                   # two copies, 6000 frames (100 s)
    python net_check.py --copies 3 --teams 2
    python net_check.py --each "-latAvg 120 -latNoise 60 -packetloss 5"
    python net_check.py --slot-args 1 "-drawdelay 100"    # one slow renderer
    python net_check.py --prove                           # must report a mismatch
    python net_check.py --each "-scenario netstance" --keys "KEY_A CTRL,KEY_H"   # the stance key, both ways

It prints one line per copy and exits 1 when any log holds "CRC Mismatch" (0 with --prove means the
detector did not fire, which is the failure then). docs: the multiplayer reference in the wrapper.
"""

import argparse
import os
import re
import shlex
import subprocess
import sys
import time

from control_client import Control, ControlError

RUN_DIRECTORY = os.path.normpath(os.path.join(os.path.dirname(__file__), "..", "..", "Run"))
EXE_NAME = "generals.exe"
FIRST_CONTROL_PORT = 8787
MAXIMUM_COPIES = 8  # eight ran clean on 2026-10-24; the map needs a start spot per copy (Iron Dragon has 8)
CONNECT_TIMEOUT_SECONDS = 120.0
MATCH_START_TIMEOUT_SECONDS = 180.0
PRESS_INTERVAL_SECONDS = 0.5
PRESSES = ("KEY_A CTRL", "KEY_X", "KEY_S", "KEY_X")
MISMATCH_LINE = "CRC Mismatch"
CENSUS_LINES = ("HEADLESS LOGICTHREAD OVERLAP", "HEADLESS LOGICTHREAD CENSUS", "HEADLESS LOGICTHREAD WINDOW")
LOGIC_FPS = 60  # LOGICFRAMES_PER_SECOND: frame numbers here and in the logs count these
PROVE_FRAME = 10 * LOGIC_FPS
PROVE_SPAWN = "spawn 0 AmericaVehicleHumvee 2 %s"
QUIT_GRACE_SECONDS = 10.0


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--copies", type=int, default=2)
    parser.add_argument("--teams", type=int, default=0, help="-teams for the slot list; 0 is free for all")
    parser.add_argument("--map", default="Maps\\Golden Oasis\\Golden Oasis.map")
    parser.add_argument("--seed", type=int, default=3)
    parser.add_argument("--frames", type=int, default=100 * LOGIC_FPS,
                        help="stop once every copy has passed this logic frame (%d a second)" % LOGIC_FPS)
    parser.add_argument("--each", default="", help="extra switches for every copy")
    parser.add_argument("--slot-args", nargs=2, action="append", default=[], metavar=("SLOT", "SWITCHES"),
                        help="extra switches for one copy, repeatable")
    parser.add_argument("--slot-options", nargs=2, action="append", default=[], metavar=("SLOT", "FOLDER"),
                        help="one copy's ZH_USER_DATA_DIR, for a match between two setups, repeatable")
    parser.add_argument("--tag", default="net", help="log prefix; copy n logs to Run/<tag><n>_DebugLogFile.txt")
    parser.add_argument("--prove", action="store_true", help="spawn on copy 0 alone at frame %d" % PROVE_FRAME)
    parser.add_argument("--prove-at", default="2192 3537", help="map position for --prove")
    parser.add_argument("--keys", default=",".join(PRESSES),
                        help="the presses, comma separated, each as the control socket's key command takes it")
    return parser.parse_args()


def command_line(arguments: argparse.Namespace, slot: int) -> list:
    hosts = ",".join("127.0.0.%d" % (index + 1) for index in range(arguments.copies))
    line = [os.path.join(RUN_DIRECTORY, EXE_NAME), "-multiInstance", "-quickstart", "-win",
            "-xres", "800", "-yres", "600", "-msaa", "0", "-noFPSLimit",
            "-netgame", hosts, "-netslot", str(slot), "-control", str(FIRST_CONTROL_PORT + slot),
            "-map", arguments.map, "-seed", str(arguments.seed),
            "-logPrefix", "%s%d_" % (arguments.tag, slot)]
    if arguments.teams:
        line += ["-teams", str(arguments.teams)]
    line += shlex.split(arguments.each, posix=False)
    for target, switches in arguments.slot_args:
        if int(target) == slot:
            line += shlex.split(switches, posix=False)
    return line


def connect(port: int) -> Control:
    deadline = time.monotonic() + CONNECT_TIMEOUT_SECONDS
    while True:
        try:
            return Control(port)
        except (OSError, ControlError):
            if time.monotonic() > deadline:
                raise ControlError("no control socket on port %d after %.0f s" % (port, CONNECT_TIMEOUT_SECONDS))
            time.sleep(1.0)


def wait_for_match(games: list) -> None:
    deadline = time.monotonic() + MATCH_START_TIMEOUT_SECONDS
    while not all(game.status()["inGame"] for game in games):
        if time.monotonic() > deadline:
            raise ControlError("the match did not start on every copy within %.0f s" % MATCH_START_TIMEOUT_SECONDS)
        time.sleep(1.0)


def count_mismatches(log_path: str) -> int:
    if not os.path.exists(log_path):
        return -1
    with open(log_path, "r", errors="replace") as log:
        return sum(MISMATCH_LINE in line for line in log)


def report_census(log_path: str) -> int:
    """Print the thread's report, which a copy writes on "quit", and return how many of its counts are not 0."""
    if not os.path.exists(log_path):
        return 0
    with open(log_path, "r", errors="replace") as log:
        lines = [line.strip() for line in log if line.startswith(CENSUS_LINES)]
    for line in lines:
        print("  " + line)
    if not lines:
        print("  no thread report: the copy never took the quit")
    return sum(count != "0" for line in lines if not line.startswith(CENSUS_LINES[0])
               for count in re.findall(r"[|:] \w+ (\d+)", line))


def play(arguments: argparse.Namespace, games: list) -> list:
    """Press keys until every copy passes the target frame; returns (first frame, frame, seconds) per copy."""
    start = [game.status()["frame"] for game in games]
    began = time.monotonic()
    frames = list(start)
    proved = False
    press = 0
    presses = arguments.keys.split(",")
    try:
        while min(frames) < arguments.frames:
            for game in games:
                game.send("key " + presses[press % len(presses)])
            press += 1
            if arguments.prove and not proved and frames[0] >= PROVE_FRAME:
                games[0].send(PROVE_SPAWN % arguments.prove_at)
                proved = True
            time.sleep(PRESS_INTERVAL_SECONDS)
            frames = [game.status()["frame"] for game in games]
    except (OSError, ControlError):
        print("NET CHECK: a copy dropped its control socket, which is what a desync's score screen does")
    elapsed = time.monotonic() - began
    return [(first, last, elapsed) for first, last in zip(start, frames)]


def main() -> int:
    arguments = parse_arguments()
    if not 2 <= arguments.copies <= MAXIMUM_COPIES:
        raise SystemExit("--copies is 2 to %d, got %d" % (MAXIMUM_COPIES, arguments.copies))

    folders = {int(slot): folder for slot, folder in arguments.slot_options}
    processes = [subprocess.Popen(command_line(arguments, slot), cwd=RUN_DIRECTORY,
                                  env=dict(os.environ, ZH_USER_DATA_DIR=folders[slot]) if slot in folders else None)
                 for slot in range(arguments.copies)]
    games = []
    try:
        games = [connect(FIRST_CONTROL_PORT + slot) for slot in range(arguments.copies)]
        wait_for_match(games)
        results = play(arguments, games)
        for game in games:
            try:
                game.send("quit")
            except (OSError, ControlError):
                pass  # already gone; killed below
        deadline = time.monotonic() + QUIT_GRACE_SECONDS
        for process in processes:
            try:
                process.wait(max(0.0, deadline - time.monotonic()))
            except subprocess.TimeoutExpired:
                pass  # a network copy can sit on the disconnect screen after quit; killed below
    finally:
        for game in games:
            game.close()
        for process in processes:
            if process.poll() is None:
                process.kill()

    total = 0
    reads = 0
    for slot, (first, last, seconds) in enumerate(results):
        log_path = os.path.join(RUN_DIRECTORY, "%s%d_DebugLogFile.txt" % (arguments.tag, slot))
        mismatches = count_mismatches(log_path)
        total += max(mismatches, 0)
        print("NET CHECK slot %d: frame %d, %.1f logic fps, %s mismatches, %s"
              % (slot, last, (last - first) / seconds, "no log" if mismatches < 0 else mismatches, log_path))
        reads += report_census(log_path)
    if arguments.prove:
        print("NET CHECK: detector %s" % ("fired" if total else "DID NOT FIRE"))
        return 0 if total else 1
    print("NET CHECK: %s, %d census or window counts not 0" % ("agreed" if total == 0 else "DESYNC", reads))
    return 0 if total == 0 and reads == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
