"""Drop one copy of a loopback network match, play on without it, bring it back with -rejoin; say whether all agreed.

Two copies play a -netgame match as net_check.py starts it, each with a user data folder of its own so
each writes its own replay. At --kill-at frames copy --rejoiner is killed outright, as a crash or a
closed window would end it. The room waits out the disconnect screen (NetworkSilentPlayerDropTime), the
router drops the seat from the lockstep without destroying its side, and the match goes on. Once the
others pass --rejoin-at the copy is started again with the same line plus -rejoinfrom <a survivor's slot>
(--log net, the default, the way an online rejoin gets the match) or -rejoin <that survivor's replay file>
(--log file): it asks the room back in, the router names a frame, every seat stops on the frame after it,
the survivor sends its replay, the copy plays the match up to there and joins, and everyone plays on. Every
copy is pressed Ctrl+A, X, S, X the whole time and stops at -maxframes' frame with a HEADLESS CRC AT LIMIT
line.

    python rejoin_check.py                               # slot 1 killed at 30 s, back once the room passes 60 s
    python rejoin_check.py --kill-at 1800 --rejoin-at 3600 --frames 6000
    python rejoin_check.py --rejoiner 0 --each=-headless     # the packet router goes and comes back
    python rejoin_check.py --copies 3 --each=-headless       # three seats
    python rejoin_check.py --forfeit --each=-headless        # nobody comes back: the side left forfeits after 5 minutes

PASS needs the CRC at the limit the same on every copy that finished, no CRC mismatch in any log, none of
the rejoined copy's recorded CRCs differing from the ones it computed, the seat dropped with its side kept
(no DestroyPlayer in a survivor's log) and taken back (a REJOIN ... is back line). With --forfeit it needs
the router's FORFEIT line and the DestroyPlayer it sends on every survivor instead.
"""

import argparse
import os
import re
import shutil
import subprocess
import sys
import time

from control_client import ControlError
import net_check

LIMIT_LINE = re.compile(r"HEADLESS CRC AT LIMIT: (0x[0-9A-F]{8}) at frame (\d+)")
SHOWN = ("REJOIN", "RECONNECT HOLD", "timed out; the seat leaves", "Saw DestroyPlayer", "disconnect screen on", "FORFEIT")
DROP_TIMEOUT_SECONDS = 180.0


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--copies", type=int, default=2)
    parser.add_argument("--rejoiner", type=int, default=1, help="the slot killed and brought back")
    parser.add_argument("--map", default="Maps\\Golden Oasis\\Golden Oasis.map")
    parser.add_argument("--seed", type=int, default=3)
    parser.add_argument("--kill-at", type=int, default=30 * net_check.LOGIC_FPS, help="frame the rejoiner is killed on or after")
    parser.add_argument("--rejoin-at", type=int, default=60 * net_check.LOGIC_FPS, help="the others' frame when it starts again")
    parser.add_argument("--frames", type=int, default=120 * net_check.LOGIC_FPS, help="-maxframes for every copy")
    parser.add_argument("--each", default="", help="extra switches for every copy")
    parser.add_argument("--log", choices=("net", "file"), default="net", help="where the copy coming back gets the match from")
    parser.add_argument("--forfeit", action="store_true", help="never start the copy again; play on past the 5 minute forfeit")
    parser.add_argument("--tag", default="rejoin")
    arguments = parser.parse_args()
    if arguments.forfeit:
        # the drop (30 s of silence) and then 5 minutes of the room playing, and a little over
        arguments.frames = max(arguments.frames, arguments.kill_at + (30 + 5 * 60 + 20) * net_check.LOGIC_FPS)
    return arguments


def copy_line(arguments: argparse.Namespace, slot: int, tag: str) -> list:
    """net_check's line for this slot, its log under tag."""
    shaped = argparse.Namespace(copies=arguments.copies, teams=0, map=arguments.map, seed=arguments.seed,
                                each=arguments.each, slot_args=[], slot_run=[], tag=tag, relay=0, capped=False)
    return net_check.command_line(shaped, slot) + ["-maxframes", str(arguments.frames)]


def start(line: list, user_data: str) -> subprocess.Popen:
    return subprocess.Popen(line, cwd=net_check.RUN_DIRECTORY, env=dict(os.environ, ZH_USER_DATA_DIR=user_data))


class Room:
    def __init__(self, games: dict):
        self.games = games
        self.count = 0

    def frames(self) -> dict:
        return {slot: game.status()["frame"] for slot, game in self.games.items()}

    def press(self) -> dict:
        for game in self.games.values():
            game.send("key " + net_check.PRESSES[self.count % len(net_check.PRESSES)])
        self.count += 1
        time.sleep(net_check.PRESS_INTERVAL_SECONDS)
        return self.frames()


def read_log(path: str) -> list:
    if not os.path.exists(path):
        return []
    with open(path, "r", errors="replace") as log:
        return log.readlines()


def main() -> int:
    arguments = parse_arguments()
    if not 2 <= arguments.copies <= 8 or not 0 <= arguments.rejoiner < arguments.copies:
        raise SystemExit("--copies 2 to 8 and --rejoiner one of them")
    run = net_check.RUN_DIRECTORY
    folders = {}
    for slot in range(arguments.copies):
        folder = os.path.join(run, "%s_ud%d" % (arguments.tag, slot)) + os.sep
        shutil.rmtree(folder, ignore_errors=True)
        os.makedirs(folder)
        folders[slot] = folder
    others = [slot for slot in range(arguments.copies) if slot != arguments.rejoiner]
    source = others[0]

    processes = {}
    back = None
    room = Room({})
    tag_back = "%sb" % arguments.tag
    try:
        for slot in range(arguments.copies):
            processes[slot] = start(copy_line(arguments, slot, arguments.tag), folders[slot])
        room.games = {slot: net_check.connect(net_check.FIRST_CONTROL_PORT + slot, arguments.copies) for slot in processes}
        net_check.wait_for_match(list(room.games.values()))

        frames = room.frames()
        while frames[arguments.rejoiner] < arguments.kill_at:
            frames = room.press()
        room.games.pop(arguments.rejoiner).close()
        processes[arguments.rejoiner].kill()
        processes[arguments.rejoiner].wait()
        killed_at = time.monotonic()
        print("REJOIN CHECK: killed slot %d on frame %d" % (arguments.rejoiner, frames[arguments.rejoiner]))

        stood = None
        frames = room.frames()
        while min(frames.values()) < arguments.rejoin_at:
            if time.monotonic() - killed_at > DROP_TIMEOUT_SECONDS:
                raise ControlError("the room never got past frame %d without the seat: %s" % (arguments.rejoin_at, frames))
            before = frames
            frames = room.press()
            if stood is None and frames == before:
                stood = dict(frames)
        print("REJOIN CHECK: the room stood on %s, played on without the seat %.0f s after the kill, now %s"
              % (stood, time.monotonic() - killed_at, frames))

        began = time.monotonic()
        if not arguments.forfeit:
            if arguments.log == "net":
                coming_back = ["-rejoinfrom", str(source)]
            else:
                coming_back = ["-rejoin", os.path.join(folders[source], "Replays", "00000000.rep")]
            back = start(copy_line(arguments, arguments.rejoiner, tag_back) + coming_back, folders[arguments.rejoiner])
            room.games[arguments.rejoiner] = net_check.connect(net_check.FIRST_CONTROL_PORT + arguments.rejoiner, arguments.copies)
            print("REJOIN CHECK: slot %d started again with %s" % (arguments.rejoiner, " ".join(coming_back)))

        paused = None
        resumed = None
        last = room.frames()
        while min(last.values()) < arguments.frames:
            # a forfeit decides the match, and a decided network match drops to the shell soon after
            if arguments.forfeit and any("Saw DestroyPlayer" in line for line in
                                         read_log(os.path.join(run, "%s%d_DebugLogFile.txt" % (arguments.tag, others[0])))):
                time.sleep(2.0)
                break
            try:
                frames = room.press()
            except (OSError, ControlError):
                if max(last.values()) >= arguments.frames - 10 * net_check.LOGIC_FPS:
                    break  # a -headless copy quits at -maxframes' frame and takes its socket with it
                raise
            survivors = [frames[slot] for slot in others]
            if paused is None and survivors == [last[slot] for slot in others]:
                paused = dict(frames)
                print("REJOIN CHECK: the room paused on %s, %.1f s after the new start" % (paused, time.monotonic() - began))
            if paused is not None and resumed is None and min(survivors) > max(paused[slot] for slot in others):
                resumed = time.monotonic() - began
                print("REJOIN CHECK: everyone plays again %.1f s after the new start, frames %s" % (resumed, frames))
            last = frames
        for game in room.games.values():
            try:
                game.send("quit")
            except (OSError, ControlError):
                pass
        deadline = time.monotonic() + net_check.QUIT_GRACE_SECONDS
        for process in list(processes.values()) + ([back] if back else []):
            try:
                process.wait(max(0.0, deadline - time.monotonic()))
            except subprocess.TimeoutExpired:
                pass
    except (OSError, ControlError) as error:
        print("REJOIN CHECK: %s" % error)
    finally:
        for game in room.games.values():
            game.close()
        for process in list(processes.values()) + ([back] if back else []):
            if process.poll() is None:
                process.kill()

    logs = {"slot %d" % slot: os.path.join(run, "%s%d_DebugLogFile.txt" % (arguments.tag, slot)) for slot in range(arguments.copies)}
    back_name = "slot %d back" % arguments.rejoiner
    logs[back_name] = os.path.join(run, "%s%d_DebugLogFile.txt" % (tag_back, arguments.rejoiner))
    limits = {}
    mismatches = 0
    differ = None
    destroyed = 0
    kept = 0
    returned = 0
    forfeited = 0
    for name, path in logs.items():
        lines = read_log(path)
        found = [match for match in (LIMIT_LINE.search(line) for line in lines) if match]
        mismatch = sum(net_check.MISMATCH_LINE in line for line in lines)
        mismatches += mismatch
        if found:
            limits[name] = found[-1].group(1)
        print("REJOIN CHECK %s: %s, %d mismatches, %s" % (name, "CRC %s at frame %s" % (found[-1].group(1), found[-1].group(2)) if found else "no CRC at the limit", mismatch, path))
        for line in lines:
            if any(token in line for token in SHOWN):
                print("  " + line.rstrip()[:240])
            counted = re.search(r"(\d+) differ", line) if "REJOIN: played" in line else None
            if counted:
                differ = int(counted.group(1))
            if name != back_name and name != "slot %d" % arguments.rejoiner:
                destroyed += "Saw DestroyPlayer" in line
                kept += "timed out; the seat leaves" in line
                returned += "REJOIN player %d is back" % arguments.rejoiner in line
                forfeited += "FORFEIT slot %d" % arguments.rejoiner in line
    finished = [name for name in logs if name != "slot %d" % arguments.rejoiner and (name != back_name or not arguments.forfeit)]
    agreed = all(name in limits for name in finished) and len(set(limits[name] for name in finished)) == 1
    if arguments.forfeit:
        passed = mismatches == 0 and forfeited == 1 and destroyed == len(others)
    else:
        passed = agreed and mismatches == 0 and differ == 0 and destroyed == 0 and returned == len(others)
    print("REJOIN CHECK: %s (CRCs at the limit %s, %d mismatches, %s rejoin CRCs differing, side kept on drop: %s, "
          "DestroyPlayer seen %d, back on %d of %d, forfeit sent %d)"
          % ("PASS" if passed else "FAIL", "agree" if agreed else "DISAGREE " + str(limits), mismatches, differ,
             "yes" if kept else "no drop line", destroyed, returned, len(others), forfeited))
    return 0 if passed else 1


if __name__ == "__main__":
    sys.exit(main())
