"""Type every console cheat into a running skirmish and check that each one did what it says.

    cd GeneralsMD/Code/Tools
    python cheat_check.py            # launches Run/generals.exe, plays one skirmish, quits
    python cheat_check.py --attach   # uses a game already started with -control 8787

The cheats are typed the way a player types them: the key above Tab, the letters, Enter, all as
key presses over the -control socket, so the console's parsing, the message it posts and the
logic's handling of it are the path under test. Each check reads the local player's line out of
"status" before and after and prints PASS or FAIL with both values; the exit code is the number of
failures.

The match is two seats on Alpine Assault with -takeover, so the computer player never builds or
attacks and every unit on the field is one this script spawned.
"""

import argparse
import os
import re
import subprocess
import sys
import time

from control_client import Control

RUN_FOLDER = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "Run"))
MAP = r"Maps\Alpine Assault\Alpine Assault.map"
PORT = 8787
LOAD_TIMEOUT_SECONDS = 180
CONNECT_ATTEMPTS = 120
# LOGICFRAMES_PER_SECOND, read from the engine's own header so this script never carries a copy of it
with open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "GameEngine", "Include", "Common", "GameCommon.h"),
          encoding="utf-8", errors="replace") as _header:
    LOGIC_FRAMES_PER_SECOND = int(re.search(r"^\s*LOGICFRAMES_PER_SECOND\s*=\s*(\d+)", _header.read(), re.M).group(1))
MATCH_SETTLE_FRAME = 2 * LOGIC_FRAMES_PER_SECOND

CHEAT_BITS = {
    "power": 1 << 5,
    "nocooldown": 1 << 6,
    "god": 1 << 7,
    "instantbuild": 1 << 8,
    "onehitkill": 1 << 9,
}


class Match(object):
    def __init__(self, game):
        self.game = game
        self.failures = 0

    def status(self):
        return self.game.send("status")

    def local(self):
        return next(player for player in self.status()["players"] if player["slot"] == 0)

    def enemy(self):
        return next(player for player in self.status()["players"] if player["slot"] == 1)

    def wait_frames(self, count):
        target = self.status()["frame"] + count
        while self.status()["frame"] < target:
            time.sleep(0.05)

    def world(self, command):
        reply = self.game.send(command)
        if not reply.get("ok"):
            raise RuntimeError("%s was refused: %s" % (command, reply))

    def type_line(self, text):
        self.game.send("key KEY_TICK")
        for letter in text:
            self.game.send("key KEY_SPACE" if letter == " " else "key KEY_" + letter.upper())
        self.game.send("key KEY_ENTER")
        self.game.send("key KEY_TICK")
        self.wait_frames(LOGIC_FRAMES_PER_SECOND // 2)

    def check(self, name, passed, before, after):
        print("%s  %-26s before %-8s after %s" % ("PASS" if passed else "FAIL", name, before, after))
        if not passed:
            self.failures += 1


def check_counters(match):
    for line, field, gain in (("money", "money", 10000), ("money 777", "money", 777),
                              ("points", "points", 1), ("points 3", "points", 3), ("rankup", "rank", 1)):
        before = match.local()[field]
        match.type_line(line)
        after = match.local()[field]
        match.check(line, after - before == gain, before, after)


def check_toggles_flip(match):
    """Every toggle on, then off, read back from the player's own cheat bits."""
    for name, bit in CHEAT_BITS.items():
        match.type_line(name)
        on = bool(match.local()["cheats"] & bit)
        match.type_line(name)
        off = not match.local()["cheats"] & bit
        match.check(name + " on and off", on and off, "on" if on else "stayed off", "off" if off else "stayed on")


def check_heroic(match):
    match.world("spawn 0 AmericaInfantryRanger 4 start0:250:0")
    match.wait_frames(LOGIC_FRAMES_PER_SECOND)
    before = match.local()["heroic"]
    match.type_line("heroic")
    after = match.local()["heroic"]
    match.check("heroic", after >= before + 4, before, after)


def check_reveal(match):
    before = match.local()["revealed"]
    match.type_line("reveal")
    after = match.local()["revealed"]
    match.check("reveal", before < 100 and after == 100, before, after)


def check_power(match):
    match.world("spawn 0 AmericaWarFactory 3 start0:-350:0 120")
    match.wait_frames(LOGIC_FRAMES_PER_SECOND)
    before = match.local()["powered"]
    match.type_line("power")
    during = match.local()["powered"]
    match.type_line("power")
    after = match.local()["powered"]
    match.check("power keeps the lights on", not before and during and not after, before, "%s then %s" % (during, after))


def check_nocooldown(match):
    match.world("spawn 0 AmericaParticleCannonUplink 1 start0:0:400")
    match.wait_frames(LOGIC_FRAMES_PER_SECOND)
    before = match.local()["powersReady"]
    match.type_line("nocooldown")
    during = match.local()["powersReady"]
    match.type_line("nocooldown")
    match.check("nocooldown readies powers", during > before, before, during)


def check_instantbuild(match):
    match.world("spawn 0 AmericaBarracks 1 start0:0:-400")
    match.wait_frames(LOGIC_FRAMES_PER_SECOND)
    match.type_line("instantbuild")
    before = match.local()["units"]
    match.world("produce 0 AmericaBarracks AmericaInfantryRanger 3")
    match.wait_frames(LOGIC_FRAMES_PER_SECOND)
    after = match.local()["units"]
    match.type_line("instantbuild")
    match.check("instantbuild trains at once", after >= before + 3, before, after)


def check_god(match):
    match.type_line("god")
    match.world("spawn 1 GLATankScorpion 3 start0:250:250 60")
    match.world("attack 1 GLATankScorpion 0 AmericaInfantryRanger")
    before = match.local()
    match.wait_frames(8 * LOGIC_FRAMES_PER_SECOND)
    after = match.local()
    match.type_line("god")
    match.check("god takes no damage", after["hurt"] == 0 and after["units"] >= before["units"],
                "%d units" % before["units"], "%d units, %d hurt" % (after["units"], after["hurt"]))


def check_onehitkill(match):
    match.world("spawn 1 GLABarracks 1 start0:500:-250")
    match.wait_frames(LOGIC_FRAMES_PER_SECOND)
    before = match.enemy()["units"]
    match.type_line("onehitkill")
    match.world("attack 0 AmericaInfantryRanger 1 GLABarracks")
    match.wait_frames(10 * LOGIC_FRAMES_PER_SECOND)
    after = match.enemy()["units"]
    match.type_line("onehitkill")
    match.check("onehitkill kills the enemy", after < before, before, after)


CHECKS = (check_counters, check_toggles_flip, check_heroic, check_reveal, check_power,
          check_nocooldown, check_instantbuild, check_onehitkill, check_god)


def launch():
    return subprocess.Popen(
        [os.path.join(RUN_FOLDER, "generals.exe"), "-win", "-quickstart", "-noshellmap", "-multiInstance",
         "-control", str(PORT), "-map", MAP, "-autoskirmish", "2", "-takeover",
         "-side", "0", "FactionAmerica", "-side", "1", "FactionGLA", "-seed", "1", "-logPrefix", "cheatcheck_"],
        cwd=RUN_FOLDER)


def connect():
    for _ in range(CONNECT_ATTEMPTS):
        try:
            return Control(PORT, timeout=LOAD_TIMEOUT_SECONDS)
        except OSError:
            time.sleep(1)
    raise RuntimeError("nothing answered on port %d" % PORT)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--attach", action="store_true", help="use a game already listening on the port")
    arguments = parser.parse_args()

    process = None if arguments.attach else launch()
    try:
        with connect() as game:
            match = Match(game)
            while not (match.status()["inGame"] and match.status()["frame"] > MATCH_SETTLE_FRAME):
                time.sleep(1)
            for check in CHECKS:
                check(match)
            print("%d failed" % match.failures)
            if process:
                game.send("quit")
            return match.failures
    finally:
        if process:
            try:
                process.wait(10)
            except subprocess.TimeoutExpired:
                process.kill()


if __name__ == "__main__":
    sys.exit(main())
