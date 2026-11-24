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
    python rejoin_check.py --checkpoint none                 # the rejoin checkpoints deleted: the whole match is played
    python rejoin_check.py --checkpoint corrupt              # every one's random seed spoiled: it plays out other
                                                             # than recorded, and the copy starts again from frame 0

With REJOIN_FROM_CHECKPOINT on (Network.cpp, off until save/load parity closes) every copy writes a rejoin
checkpoint once a minute; the copy coming back loads the newest one 200 s older than its newest and plays
only the frames after it (--checkpoint keep). With it off, --checkpoint none (the default) is the case:
no copy writes one and the copy coming back plays from frame 0. keep and corrupt need a match long enough for a
checkpoint 200 s before the join, such as --kill-at 15000 --rejoin-at 16500 --frames 18000.

PASS needs the CRC at the limit the same on every copy that finished, no CRC mismatch in any log, none of
the rejoined copy's recorded CRCs differing from the ones it computed, the seat dropped with its side kept
(no DestroyPlayer in a survivor's log) and taken back (a REJOIN ... is back line), and the path the
--checkpoint asked for: a join from the checkpoint, a join from frame 0 with no checkpoint load, or a load,
a fall back to frame 0 and a join from frame 0. With --forfeit it needs the router's FORFEIT line and the
DestroyPlayer it sends on every survivor instead.
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
NOT_SHOWN = ("appendNextCommand", "REJOIN CHECKPOINT frame", "sendRejoinRequest", "processRejoinLog - REJOIN")
PLAYED_LINE = re.compile(r"REJOIN: played frames (\d+) to (\d+) (from this seat's checkpoint|from the start) in (\d+) ms")
BACK_LINE = re.compile(r"REJOIN player \d+ is back, playing from frame (\d+), (\d+) ms after it was readmitted")
DROP_TIMEOUT_SECONDS = 180.0
# a control socket that went away; a closed one has no socket left to send on
LOST = (OSError, ControlError, AttributeError)
# Network.cpp, writeRejoinCheckpointMeta: magic, version and eleven 32-bit fields come before the random state
CHECKPOINT_RANDOM_OFFSET = 52


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
    parser.add_argument("--exe", default=net_check.EXE_NAME, help="the exe every copy runs, in Run/ (a copy of its own while other runs hold generals.exe)")
    parser.add_argument("--checkpoint", choices=("keep", "none", "corrupt"), default="none",
                        help="what the copy coming back finds of its rejoin checkpoints")
    parser.add_argument("--tag", default="rejoin")
    arguments = parser.parse_args()
    if arguments.forfeit:
        # the drop (30 s of silence) and then 5 minutes of the room playing, and a little over
        arguments.frames = max(arguments.frames, arguments.kill_at + (30 + 5 * 60 + 20) * net_check.LOGIC_FPS)
    return arguments


def copy_line(arguments: argparse.Namespace, slot: int, tag: str) -> list:
    """net_check's line for this slot, its log under tag."""
    shaped = argparse.Namespace(copies=arguments.copies, teams=0, map=arguments.map, seed=arguments.seed,
                                each=arguments.each, slot_args=[], slot_run=[], tag=tag, relay=0, capped=False,
                                exe=arguments.exe)
    return net_check.command_line(shaped, slot) + ["-maxframes", str(arguments.frames)]


def start(line: list, user_data: str) -> subprocess.Popen:
    return subprocess.Popen(line, cwd=net_check.RUN_DIRECTORY, env=dict(os.environ, ZH_USER_DATA_DIR=user_data))


class Room:
    def __init__(self, games: dict):
        self.games = games
        self.count = 0
        self.fragile = None    # the copy coming back: loading its checkpoint resets the engine, the control socket with it
        self.last = {}

    def reconnect(self, slot: int) -> None:
        try:
            self.games[slot].close()
        except LOST:
            pass
        try:
            self.games[slot] = net_check.Control(net_check.FIRST_CONTROL_PORT + slot, timeout=2.0)
        except LOST:
            pass

    def frames(self) -> dict:
        frames = {}
        for slot, game in list(self.games.items()):
            try:
                frames[slot] = game.status()["frame"]
            except LOST:
                if slot != self.fragile:
                    raise
                self.reconnect(slot)
                frames[slot] = self.last.get(slot, 0)
        self.last = frames
        return frames

    def press(self) -> dict:
        for slot, game in list(self.games.items()):
            try:
                game.send("key " + net_check.PRESSES[self.count % len(net_check.PRESSES)])
            except LOST:
                if slot != self.fragile:
                    raise
                self.reconnect(slot)
        self.count += 1
        time.sleep(net_check.PRESS_INTERVAL_SECONDS)
        return self.frames()


def prepare_checkpoints(folder: str, how: str) -> str:
    """The rejoin checkpoints in the copy's user data as --checkpoint asks; says what was done."""
    root = os.path.join(folder, "Save", "RejoinCheckpoints")
    metas = []
    for directory, _, names in os.walk(root):
        metas += [os.path.join(directory, name) for name in names if name.endswith(".ckpt")]
    if how == "none":
        shutil.rmtree(root, ignore_errors=True)
        return "deleted %d checkpoints" % len(metas)
    if not metas:
        return "found no checkpoint"
    newest = max(metas, key=lambda path: int(os.path.basename(path).split(".")[0]))
    if how == "corrupt":
        # every one: the copy loads the newest old enough to verify, which need not be the newest
        for path in metas:
            with open(path, "r+b") as meta:
                meta.seek(CHECKPOINT_RANDOM_OFFSET)
                seed = bytearray(meta.read(4))
                seed[0] ^= 0x5A
                meta.seek(CHECKPOINT_RANDOM_OFFSET)
                meta.write(bytes(seed))
        return "spoiled the random seed of all %d" % len(metas)
    return "kept %d, the newest %s" % (len(metas), newest)


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
            print("REJOIN CHECK: checkpoints of slot %d: %s" % (arguments.rejoiner, prepare_checkpoints(folders[arguments.rejoiner], arguments.checkpoint)))
            room.fragile = arguments.rejoiner
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
            except LOST:
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
            except LOST:
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
    played = None
    loaded = 0
    fell_back = 0
    freeze_ms = []
    for name, path in logs.items():
        lines = read_log(path)
        found = [match for match in (LIMIT_LINE.search(line) for line in lines) if match]
        mismatch = sum(net_check.MISMATCH_LINE in line for line in lines)
        mismatches += mismatch
        if found:
            limits[name] = found[-1].group(1)
        print("REJOIN CHECK %s: %s, %d mismatches, %s" % (name, "CRC %s at frame %s" % (found[-1].group(1), found[-1].group(2)) if found else "no CRC at the limit", mismatch, path))
        for line in lines:
            if any(token in line for token in SHOWN) and not any(token in line for token in NOT_SHOWN):
                print("  " + line.rstrip()[:240])
            counted = re.search(r"(\d+) differ", line) if "REJOIN: played" in line else None
            if counted:
                differ = int(counted.group(1))
            if name == back_name:
                found_played = PLAYED_LINE.search(line)
                if found_played:
                    played = found_played
                loaded += "REJOIN: loaded this seat's checkpoint" in line
                fell_back += "REJOIN: from frame 0 after all" in line
            if name != back_name and name != "slot %d" % arguments.rejoiner:
                destroyed += "Saw DestroyPlayer" in line
                kept += "timed out; the seat leaves" in line
                returned += "REJOIN player %d is back" % arguments.rejoiner in line
                forfeited += "FORFEIT slot %d" % arguments.rejoiner in line
                found_back = BACK_LINE.search(line)
                if found_back:
                    freeze_ms.append(int(found_back.group(2)))
    finished = [name for name in logs if name != "slot %d" % arguments.rejoiner and (name != back_name or not arguments.forfeit)]
    agreed = all(name in limits for name in finished) and len(set(limits[name] for name in finished)) == 1
    path_ok = True
    if not arguments.forfeit and arguments.log == "net":
        from_checkpoint = played is not None and played.group(3) == "from this seat's checkpoint"
        if arguments.checkpoint == "keep":
            path_ok = loaded >= 1 and fell_back == 0 and from_checkpoint
        elif arguments.checkpoint == "none":
            path_ok = loaded == 0 and fell_back == 0 and played is not None and not from_checkpoint
        else:
            path_ok = loaded >= 1 and fell_back >= 1 and played is not None and not from_checkpoint
        if played is not None:
            print("REJOIN CHECK: the copy played frames %s to %s %s, %s ms after its new start; the room stood %s ms"
                  % (played.group(1), played.group(2), played.group(3), played.group(4), freeze_ms))
    if arguments.forfeit:
        passed = mismatches == 0 and forfeited == 1 and destroyed == len(others)
    else:
        passed = agreed and mismatches == 0 and differ == 0 and destroyed == 0 and returned == len(others) and path_ok
    print("REJOIN CHECK: %s (CRCs at the limit %s, %d mismatches, %s rejoin CRCs differing, side kept on drop: %s, "
          "DestroyPlayer seen %d, back on %d of %d, forfeit sent %d, checkpoint %s: %d loads, %d falls back to frame 0, %s)"
          % ("PASS" if passed else "FAIL", "agree" if agreed else "DISAGREE " + str(limits), mismatches, differ,
             "yes" if kept else "no drop line", destroyed, returned, len(others), forfeited,
             arguments.checkpoint, loaded, fell_back, "as asked" if path_ok else "NOT the path asked for"))
    return 0 if passed else 1


if __name__ == "__main__":
    sys.exit(main())
