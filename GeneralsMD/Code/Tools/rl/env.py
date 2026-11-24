"""A Gymnasium environment that learns an America build order against the Easy AI.

One episode is one offline skirmish of Zero Hour, slot 0 America against one AI seat. The env drives
generals.exe through its -control WebSocket (control_client.py, one folder up) with the training
verbs `step`, `objects`, `produce`, `construct` and `buildspot`, and makes one decision every few
game seconds: wait, train a dozer, put up a building, queue a unit, or order the army out against the
enemy base or back to the front of our own (`attackmove`, the scenario grammar). MaskablePPO only
ever picks from action_masks(), which says what the state allows (a finished producer, the money, an
idle dozer, a short queue, the caps, an army big enough to send).

`mock=True` swaps the game for mock_game.py, a small simulation that answers the same verbs, so the
env and the training loop run without the game.

    python env.py --check        gymnasium's env checker, the mask tests and a masked random episode

Paths come from arguments or the environment, never from this file: ZHR_GENERALS_EXE names the exe
(default: GeneralsMD/Run/generals.exe of this checkout) and ZHR_RL_USER_DIR the folder under which
each game gets its own ZH_USER_DATA_DIR (default: the system temp folder).
"""

import argparse
import json
import math
import os
import re
import subprocess
import sys
import tempfile
import time
from collections import namedtuple

import gymnasium as gym
import numpy as np

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from control_client import OPCODE_TEXT, Control, ControlError  # noqa: E402

# LOGICFRAMES_PER_SECOND 30 * 100 / GAME_SPEED_PERCENT 150 (GameCommon.h): a game second is 20
# logic frames, and INI build times are game seconds.
FRAMES_PER_GAME_SECOND = 20
MAX_STEP_FRAMES = 300
LOGICFRAMES_PER_SECOND = 30  # GameCommon.h: the wall pace of a match
WATCH_CHUNK_FRAMES = 3       # a tenth of a wall second between steps when paced for watching
DEFAULT_MAP = r"Maps\Tournament Desert\Tournament Desert.map"
DEFAULT_PORT = 8800

# source: for a unit the building that trains it, for a building its prerequisite. Costs, build times
# and power are retail INIZH.big Data\INI\Object\FactionBuilding.ini, AmericaVehicle.ini,
# AmericaInfantry.ini and AmericaAir.ini; BalanceReforged.ini changes none of these templates.
Item = namedtuple("Item", "template cost seconds source power cap")
ITEMS = {
    "cc":       Item("AmericaCommandCenter",           2000, 45, None,       0,  1),
    "dozer":    Item("AmericaVehicleDozer",            1000,  5, "cc",       0,  5),
    "power":    Item("AmericaPowerPlant",               800, 10, None,       5,  8),
    "barracks": Item("AmericaBarracks",                 600, 10, None,       0,  2),
    "supply":   Item("AmericaSupplyCenter",            2000, 10, "power",   -1,  3),
    "factory":  Item("AmericaWarFactory",              2000, 15, "supply",  -1,  2),
    "patriot":  Item("AmericaPatriotBattery",          1000, 25, "power",   -3,  4),
    "chinook":  Item("AmericaVehicleChinook",          1200, 10, "supply",   0,  6),
    "ranger":   Item("AmericaInfantryRanger",           225,  5, "barracks", 0, 40),
    "missile":  Item("AmericaInfantryMissileDefender",  300,  5, "barracks", 0, 40),
    "humvee":   Item("AmericaVehicleHumvee",            700, 10, "factory",  0, 40),
    "crusader": Item("AmericaTankCrusader",             900, 10, "factory",  0, 40),
}
ACTIONS = ["wait", "dozer", "power", "barracks", "supply", "factory", "patriot",
           "chinook", "ranger", "missile", "humvee", "crusader", "attack", "defend"]
BUILDINGS = ["power", "barracks", "supply", "factory", "patriot"]
UNITS = ["dozer", "chinook", "ranger", "missile", "humvee", "crusader"]
PRODUCERS = ["cc", "barracks", "supply", "factory"]
ARMY = {"ranger", "missile", "humvee", "crusader"}
KEY_OF_TEMPLATE = {item.template: key for key, item in ITEMS.items()}
QUEUE_MAX = 2
ENEMY_SLOT = 1
ATTACK_MIN = 3             # army units before the army may be sent at the enemy base
POWER_SURPLUS_MAX = 10     # no more power plants once this much power is spare (built and building)
HOME_RADIUS = 700.0        # an army unit further than this from our CC counts as away
FRONT_DISTANCE = 350.0     # the defence point and the Patriot line, out from our CC toward theirs
COMMIT_DECISIONS = 12      # an attack may not be recalled for this many decisions (a minute at 5 s)
COOLDOWN_DECISIONS = 6     # and no new attack for this many after a recall (half a minute)
THREAT_RADIUS = 1400.0     # enemy army inside this of our CC is at the door...
THREAT_SHARE = 0.4         # ...and a threat once it is worth this share of our army
# ponytail: the enemy's templates have no cost table here, so an object we cannot price counts by its
# kind; a table of GLA and China costs from INIZH.big would make damage dealt exact
NOMINAL_VALUE = {"structure": 1000, "builder": 600, "unit": 300}  # for what load_costs cannot price
# `objects` lists every object a seat owns, shots and effects included (ToxinTruckStreamProjectile,
# SCUDMissile, PoisonFieldSmall, ReforgedTankWreckFire...), and each one that expires would count
# as a kill. Our side needs no filter (only ITEMS templates are read); theirs does.
NOT_A_UNIT = re.compile(r"Projectile|Missile$|Field|Wreck|Debris|Shell$|Bomb$|Rocket$|Flare|Crater")

# Per 1000 credits: what we finish (economy full, army half), income, the enemy value we destroy and
# ours that is destroyed; a kill, a refused order and the end of the match are flat. Combat outweighs
# economy on purpose: a base is only worth what it lets the army do.
REWARD = {"economy": 1.0, "army": 0.75, "income": 0.5, "razed": 3.0, "razed_each": 0.5, "dealt": 0.5,
          "taken": 1.0, "hoard": 0.01, "refused": 0.05, "loss": 20.0, "win": 50.0}
HOARD_FREE = 3000          # money above this costs "hoard" per 1000 per game second: 10000 held for
                           # ten minutes is -42, about what a good economy earns in that time


def base_template(template):
    """AirF_AmericaBarracks -> AmericaBarracks: a general's copy counts as the stock building."""
    return template.split("_", 1)[1] if "_" in template else template


def load_costs(run_dir):
    """BuildCost of every Object the game knows: the retail Object INIs in INIZH.big, then the loose
    Data/INI files beside the exe (the fork's Reforged edits). Empty when the Run folder has neither."""
    import bigfile
    texts = []
    try:
        with open(os.path.join(run_dir, "INIZH.big"), "rb") as archive:
            members = [(path, offset, size) for path, offset, size in bigfile._read_index(archive)
                       if path.lower().replace("/", "\\").startswith("data\\ini\\object\\")]
            for _, offset, size in members:
                archive.seek(offset)
                texts.append(archive.read(size).decode("latin-1"))
    except OSError:
        pass
    loose = os.path.join(run_dir, "Data", "INI")
    if os.path.isdir(loose):
        for name in sorted(os.listdir(loose)):
            if name.lower().endswith(".ini"):
                with open(os.path.join(loose, name), "r", encoding="latin-1") as f:
                    texts.append(f.read())
    costs, name = {}, None
    for text in texts:
        for line in text.splitlines():
            start = re.match(r"(Object|ObjectReskin)\s+(\S+)(?:\s+(\S+))?", line)
            if start:
                name = start.group(2)
                if start.group(1) == "ObjectReskin" and start.group(3) in costs:
                    costs[name] = costs[start.group(3)]
            elif re.match(r"End\b", line):
                name = None
            elif name:
                cost = re.match(r"\s*BuildCost\s*=\s*(\d+)", line)
                if cost:
                    costs[name] = int(cost.group(1))
    return costs


def default_exe():
    here = os.path.dirname(os.path.abspath(__file__))
    return os.environ.get("ZHR_GENERALS_EXE") or os.path.normpath(os.path.join(here, "..", "..", "..", "Run", "generals.exe"))


def wait_for(predicate, timeout, what):
    deadline = time.monotonic() + timeout
    while True:
        try:
            if predicate():
                return
        except (OSError, ControlError):
            pass
        if time.monotonic() > deadline:
            raise TimeoutError(what)
        time.sleep(0.1)


class GameProcess:
    """One generals.exe with its control socket, started here and stopped by its own PID only."""

    def __init__(self, port, exe, opponent, difficulty, user_dir, headless):
        self.port = port
        user_dir = os.path.join(user_dir or os.environ.get("ZHR_RL_USER_DIR") or os.path.join(tempfile.gettempdir(), "zhr-rl"), str(port))
        os.makedirs(user_dir, exist_ok=True)
        exe = exe or default_exe()
        # no -autoskirmish: the shell comes up and every match, the first included, starts with the
        # socket's "skirmish", which takes these -side, -aidiff and -takeover. "passive" is the AI seat
        # left driverless (-takeover 1): a base that never builds or fights, to learn razing on
        seat = ["-takeover", "1"] if difficulty == "passive" else ["-aidiff", difficulty]
        line = [exe, "-quickstart", "-noshellmap", "-multiInstance", "-noFPSLimit"] + seat + [
                "-side", "0", "FactionAmerica", "-side", "1", opponent, "-control", str(port), "-logPrefix", "rl%d_" % port]
        line += ["-headless"] if headless else ["-win", "-xres", "800", "-yres", "600"]
        # the game starts only from its Run folder; ZH_UNATTENDED turns a waiting message box into an exit
        self.process = subprocess.Popen(line, cwd=os.path.dirname(exe),
                                        env=dict(os.environ, ZH_USER_DATA_DIR=user_dir, ZH_UNATTENDED="1"))
        self.control = None

        def connect():
            if self.process.poll() is not None:
                raise RuntimeError("generals.exe on port %d exited with %s" % (port, self.process.returncode))
            self.control = Control(port, timeout=60.0)
            return True
        wait_for(connect, 120, "no control socket on port %d" % port)

    def send(self, command):
        return self.control.send(command)

    def exchange(self, commands):
        """Send every command, then read one reply each, in order. produce and construct answer only
        once a logic frame has run them, which in lockstep is inside the step sent after them."""
        for command in commands:
            self.control._send_frame(OPCODE_TEXT, command.encode("utf-8"))
        return [json.loads(self.control._read_frame()) for _ in commands]

    def close(self):
        try:
            self.control.send("quit")
            self.process.wait(10)
        except Exception:
            pass
        if self.process.poll() is None:
            self.process.kill()
        if self.control:
            self.control.close()


class BuildOrderEnv(gym.Env):
    metadata = {"render_modes": []}

    def __init__(self, port=DEFAULT_PORT, mock=False, decision_seconds=5.0, episode_minutes=20.0,
                 map_name=DEFAULT_MAP, seed=0, opponent="FactionGLA", difficulty="easy", exe=None,
                 user_dir=None, headless=True, frames_per_game_second=FRAMES_PER_GAME_SECOND, reward=None,
                 watch_speed=0.0):
        # watch_speed > 0 paces the steps for a person watching: 1 is the game's own pace, 30 logic
        # frames a wall second (LOGICFRAMES_PER_SECOND); 0 runs as fast as the game can step
        self.watch_speed = watch_speed
        self.port, self.mock, self.map_name, self.base_seed = port, mock, map_name, seed
        self.opponent, self.difficulty, self.exe, self.user_dir, self.headless = opponent, difficulty, exe, user_dir, headless
        self.decision_seconds = decision_seconds
        self.step_frames = max(1, round(decision_seconds * frames_per_game_second))
        self.episode_frames = round(episode_minutes * 60 * frames_per_game_second)
        self.reward_weights = dict(REWARD, **(reward or {}))
        # the enemy's prices; the mock has no Run folder and prices by kind
        self.costs = {} if mock else load_costs(os.path.dirname(exe or default_exe()))
        self.slot = 0
        self.game = None
        self.broken = False
        self.episodes = 0
        self.action_space = gym.spaces.Discrete(len(ACTIONS))
        size = 4 + 2 * len(BUILDINGS) + len(UNITS) + len(PRODUCERS) + 1 + 9
        self.observation_space = gym.spaces.Box(-1.0, 1.0, (size,), np.float32)

    # -- the game -----------------------------------------------------------------------------------

    def send(self, command):
        reply = self.game.send(command)
        if not isinstance(reply, dict):
            raise ControlError("%s: no JSON object back" % command)
        return reply

    def _skirmish(self, seed):
        """leave drops lockstep and the match; then a new one through the socket."""
        self.send("leave")
        wait_for(lambda: not self.send("status")["inGame"], 60, "match did not end on port %d" % self.port)
        reply = self.send("skirmish 2 %d %s" % (seed, self.map_name))
        if not reply.get("ok"):
            raise ControlError("skirmish refused: %s" % reply.get("error"))
        wait_for(lambda: self.send("status")["inGame"], 180, "match did not start on port %d" % self.port)

    def _new_match(self, seed):
        if self.mock and self.game is None:
            from mock_game import MockGame
            self.game = MockGame(passive=self.difficulty == "passive")
        if self.game is not None and not self.broken:
            try:
                return self._skirmish(seed)
            except (OSError, ControlError, TimeoutError, KeyError):
                if self.mock:
                    raise
        self.close()  # relaunch: none yet, or the one we had stopped answering
        self.game = GameProcess(self.port, self.exe, self.opponent, self.difficulty, self.user_dir, self.headless)
        self.broken = False
        self._skirmish(seed)

    def _advance(self, order=None):
        """Run one decision's frames; an order (key, command) goes ahead of the first step. Returns
        whether the order was taken, and False for ended when the match went away mid-step."""
        left, taken, ended = self.step_frames, None, False
        while left > 0 and not ended:
            frames = min(WATCH_CHUNK_FRAMES if self.watch_speed > 0 else MAX_STEP_FRAMES, left)
            due = time.monotonic() + frames / (LOGICFRAMES_PER_SECOND * self.watch_speed) if self.watch_speed > 0 else 0
            commands = ([order[1]] if order and taken is None else []) + ["step %d" % frames]
            replies = self.game.exchange(commands)
            if len(replies) == 2:
                self.why = replies[0].get("error")
                taken = bool(replies[0].get("ok")) and replies[0].get("queued", 1) > 0
                if taken:
                    self.spent += ITEMS[order[0]].cost * (replies[0].get("queued", 1))
            reply = replies[-1]
            if not reply.get("ok"):
                if "ended" not in str(reply.get("error")):
                    raise ControlError("step refused: %s" % reply.get("error"))
                ended = True
            left -= frames
            time.sleep(max(0.0, due - time.monotonic()))
        return taken, ended

    def _read(self):
        reply = self.send("objects %d" % self.slot)
        if not reply.get("ok"):
            raise ControlError("objects refused: %s" % reply.get("error"))
        power = reply.get("power") or {}
        s = {"frame": reply["frame"], "money": reply["money"], "defeated": bool(reply.get("defeated")),
             "power": power.get("produced", 0) - power.get("consumed", 0),
             "built": dict.fromkeys(ITEMS, 0), "uc": dict.fromkeys(ITEMS, 0), "units": dict.fromkeys(ITEMS, 0),
             "queue": dict.fromkeys(ITEMS, 0), "producers": {key: [] for key in PRODUCERS},
             "idle": 0, "economy": 0, "army": 0, "cc": None,
             "ids": {},          # every finished object: id -> (value, "economy" or "army")
             "soldiers": [],     # the army: (template, x, y)
             "army_ids": {}}     # the army: id -> template
        for o in reply["objects"]:
            key = KEY_OF_TEMPLATE.get(base_template(o["template"]))
            if key is None:
                continue
            item = ITEMS[key]
            if o["kind"] == "structure":
                if o.get("underConstruction"):
                    s["uc"][key] += 1
                    continue
                s["built"][key] += 1
                queue = o.get("queue") or 0
                queue = len(queue) if isinstance(queue, list) else int(queue)
                if key in s["producers"]:
                    s["producers"][key].append((str(o["id"]), queue))  # produce takes the id: this very building
                    s["queue"][key] += queue
                if key == "cc":
                    s["cc"] = o
                s["ids"][o["id"]] = (item.cost, "economy")
                if key != "cc":
                    s["economy"] += item.cost
            else:
                s["units"][key] += 1
                part = "army" if key in ARMY else "economy"
                s[part] += item.cost
                s["ids"][o["id"]] = (item.cost, part)
                if part == "army":
                    s["soldiers"].append((o["template"], o["x"], o["y"]))
                    s["army_ids"][o["id"]] = o["template"]
                if o["kind"] == "builder" and o.get("idle"):
                    s["idle"] += 1
        home = (s["cc"]["x"], s["cc"]["y"]) if s["cc"] else getattr(self, "home", (0.0, 0.0))
        s["away"] = sum(math.hypot(x - home[0], y - home[1]) > HOME_RADIUS for _, x, y in s["soldiers"])
        s["enemy"] = self._read_enemy(home)
        return s

    def _read_enemy(self, home):
        """What the AI seat has: structures and army by count and value, its CC, and how near its army is."""
        e = {"structures": 0, "structure_value": 0, "army": 0, "army_value": 0, "ids": {}, "cc": None,
             "targets": [], "nearest": None, "near_value": 0, "defeated": False}
        reply = self.send("objects %d" % ENEMY_SLOT)
        if not reply.get("ok"):
            return e
        e["defeated"] = bool(reply.get("defeated"))
        for o in reply["objects"]:
            if NOT_A_UNIT.search(o["template"]):
                continue
            # BuildCost from the game's INI (a general's copy shares the stock price), else by kind
            value = self.costs.get(o["template"]) or self.costs.get(base_template(o["template"])) \
                or NOMINAL_VALUE.get(o["kind"], 300)
            e["ids"][o["id"]] = (value, o["kind"] == "structure" and not o.get("underConstruction"))
            if o["kind"] == "structure":
                e["structures"] += 1
                e["structure_value"] += value
                e["targets"].append((o["x"], o["y"]))
                if o["template"].endswith("CommandCenter") and not o.get("underConstruction"):
                    e["cc"] = (o["x"], o["y"])
            elif o["kind"] == "unit":  # builders (workers, dozers) are no threat
                e["army"] += 1
                e["army_value"] += value
                distance = math.hypot(o["x"] - home[0], o["y"] - home[1])
                e["nearest"] = distance if e["nearest"] is None else min(e["nearest"], distance)
                if distance < THREAT_RADIUS:
                    e["near_value"] += value
        return e

    def _outcome(self):
        status = self.send("status")
        players = [p for p in status.get("players", []) if p.get("slot", -1) >= 0]
        mine = [p for p in players if p["slot"] == self.slot]
        others = [p for p in players if p["slot"] != self.slot]
        # a match that is no longer running says nothing here; step() ends the episode on it anyway
        self.others_left = sum(p.get("units", 0) for p in others) if others else self.others_left
        lost = self.state["defeated"] or (bool(mine) and mine[0].get("units", 0) == 0)
        won = not lost and (self.state["enemy"]["defeated"] or (bool(others) and all(p.get("units", 0) == 0 for p in others)))
        return lost, won

    def _find_supply(self):
        """The supply pile nearest the CC, from the neutral player's objects; the CC when that is refused."""
        cc = (self.state["cc"]["x"], self.state["cc"]["y"])
        try:
            reply = self.send("objects -1")
        except (OSError, ControlError):
            return cc
        piles = [o for o in reply.get("objects", []) if "Supply" in o.get("template", "")] if reply.get("ok") else []
        if not piles:
            # ponytail: no pile known, so the supply centre goes beside the CC; income suffers on maps with far piles
            return cc
        pile = min(piles, key=lambda o: math.hypot(o["x"] - cc[0], o["y"] - cc[1]))
        return pile["x"], pile["y"]

    # -- actions ------------------------------------------------------------------------------------

    def _order(self, key):
        """The produce or construct line for one action, or None when it cannot even be placed."""
        item = ITEMS[key]
        if key in UNITS:
            producers = self.state["producers"][item.source]
            if not producers:
                return None
            building = min(producers, key=lambda p: p[1])[0]
            return "produce %d %s %s 1" % (self.slot, building, self.prefix + item.template)
        else:
            template = self.prefix + item.template
            if key == "supply":
                x, y = self.supply_xy
            elif key == "patriot":
                # across the line from our CC to theirs, FRONT_DISTANCE out: centre, then left, right, ...
                n = self.state["built"]["patriot"] + self.state["uc"]["patriot"]
                side = 140.0 * ((n + 1) // 2) * (1 if n % 2 else -1)
                x = self.front[0] - self.facing[1] * side
                y = self.front[1] + self.facing[0] * side
            else:
                # golden-angle spiral round the CC, one more lap every eight buildings
                angle = self.placed * 2.39996
                ring = 220.0 + 60.0 * (self.placed // 8)
                x = self.state["cc"]["x"] + ring * math.cos(angle)
                y = self.state["cc"]["y"] + ring * math.sin(angle)
            spot = self.send("buildspot %d %s %.0f %.0f %.0f" % (self.slot, template, x, y, 400.0))
            if not spot.get("ok"):
                self.why = "buildspot: %s" % spot.get("error")
                return None
            self.placed += 1
            return "construct %d %s %.0f %.0f" % (self.slot, template, spot["x"], spot["y"])

    def _attack_target(self):
        """The enemy CC, else their structure nearest our base; None when we know of none."""
        e = self.state["enemy"]
        if e["cc"]:
            return e["cc"]
        if not e["targets"]:
            return None
        return min(e["targets"], key=lambda t: math.hypot(t[0] - self.home[0], t[1] - self.home[1]))

    def _army_order(self, key):
        """attack: every army template attack-moves on the enemy base. defend: back to our front, fighting
        on the way. One line per template: a selector is one template or a prefix, and America's
        prefixes would take the dozers and Chinooks along. True when every line was taken."""
        target = self._attack_target() if key == "attack" else self.front
        if target is None:
            self.why = "no enemy structure known"
            return False
        templates = sorted({template for template, _, _ in self.state["soldiers"]})
        if not templates:
            self.why = "no army"
            return False
        for template in templates:
            reply = self.send("attackmove %d %s %.0f %.0f" % (self.slot, template, target[0], target[1]))
            if not reply.get("ok"):
                self.why = "attackmove: %s" % reply.get("error")
                return False
        self.army_order = 1 if key == "attack" else -1
        self.order_target = target
        self.order_decision = self.decisions
        self.sent_ids = set(self.state["army_ids"])
        return True

    def _reinforce(self):
        """While an attack stands, units trained since go after it (and the target follows the base:
        their CC, else their nearest structure)."""
        new = self.state["army_ids"].keys() - self.sent_ids
        target = self._attack_target()
        if not new or target is None:
            return
        for template in sorted({self.state["army_ids"][i] for i in new}):
            self.send("attackmove %d %s %.0f %.0f" % (self.slot, template, target[0], target[1]))
        self.sent_ids = set(self.state["army_ids"])

    def action_masks(self):
        s = self.state
        mask = np.zeros(len(ACTIONS), dtype=bool)
        mask[0] = True
        army = len(s["soldiers"])
        for index, key in enumerate(ACTIONS[1:], 1):
            # an attack is a commitment: no second order to the same target, no recall for
            # COMMIT_DECISIONS unless a real threat is at our door, and no new attack for
            # COOLDOWN_DECISIONS after a recall; otherwise a policy flips between the two, trickles units
            # forward and the army never arrives anywhere
            attacking = self.army_order == 1
            since = self.decisions - self.order_decision
            if key == "attack":
                target = self._attack_target()
                cooling = self.army_order == -1 and since < COOLDOWN_DECISIONS
                mask[index] = (army >= ATTACK_MIN and target is not None and not cooling
                               and not (attacking and target == self.order_target))
                continue
            if key == "defend":
                threatened = s["enemy"]["near_value"] > THREAT_SHARE * max(s["army"], 1)
                committed = attacking and since < COMMIT_DECISIONS
                mask[index] = army > 0 and (s["away"] > 0 or attacking) and (threatened or not committed)
                continue
            item = ITEMS[key]
            if s["money"] < item.cost:
                continue
            if key in UNITS:
                queues = [queue for _, queue in s["producers"][item.source]]
                mask[index] = bool(queues) and min(queues) < QUEUE_MAX and s["units"][key] + s["queue"][item.source] < item.cap
            else:
                mask[index] = (s["idle"] > 0 and s["cc"] is not None and s["built"][key] + s["uc"][key] < item.cap
                               and (item.source is None or s["built"][item.source] > 0))
                if key == "power":
                    mask[index] &= s["power"] + ITEMS["power"].power * s["uc"]["power"] < POWER_SURPLUS_MAX
        return mask

    # -- gymnasium ----------------------------------------------------------------------------------

    def _obs(self):
        s = self.state
        v = [s["money"] / 10000.0, self.income / 5000.0, s["power"] / 20.0,
             (s["frame"] - self.start_frame) / self.episode_frames]
        v += [s["built"][k] / 4.0 for k in BUILDINGS] + [s["uc"][k] / 2.0 for k in BUILDINGS]
        v += [s["units"][k] / 10.0 for k in UNITS] + [s["queue"][k] / 5.0 for k in PRODUCERS] + [s["idle"] / 3.0]
        e = s["enemy"]
        v += [e["structures"] / 20.0, e["structure_value"] / 20000.0, e["army"] / 30.0, e["army_value"] / 15000.0,
              1.0 if e["nearest"] is None else e["nearest"] / 4000.0, e["near_value"] / 5000.0,
              s["army"] / 15000.0, s["away"] / max(1, len(s["soldiers"])), float(self.army_order)]
        return np.clip(np.asarray(v, dtype=np.float32), -1.0, 1.0)

    def reset(self, *, seed=None, options=None):
        super().reset(seed=seed)
        match_seed = seed if seed is not None else self.base_seed + self.episodes
        self.episodes += 1
        self.spent = 0
        for attempt in range(3):
            try:
                self._new_match(match_seed)
                self._advance()  # the first step puts the game in lockstep
                self.state = self._read()
                break
            except (OSError, ControlError, TimeoutError, KeyError, ValueError):
                if self.mock or attempt == 2:
                    raise
                self.broken = True  # relaunch on the next try
        if self.state["cc"] is None:
            raise ControlError("no command center for slot %d" % self.slot)
        self.prefix = self.state["cc"]["template"][:-len(base_template(self.state["cc"]["template"]))]
        self.supply_xy = self._find_supply()
        self.home = (self.state["cc"]["x"], self.state["cc"]["y"])
        self.army_order = 0  # 1 the army was last sent to attack, -1 to defend
        self.order_target, self.order_decision, self.decisions, self.sent_ids = None, 0, 0, set()
        enemy = self.state["enemy"]["cc"] or (self.state["enemy"]["targets"] or [None])[0]
        dx, dy = (enemy[0] - self.home[0], enemy[1] - self.home[1]) if enemy else (1.0, 0.0)
        length = math.hypot(dx, dy) or 1.0
        self.facing = (dx / length, dy / length)
        self.front = (self.home[0] + self.facing[0] * FRONT_DISTANCE, self.home[1] + self.facing[1] * FRONT_DISTANCE)
        self.start_frame = self.state["frame"]
        self.income = 0.0
        self.placed = 0
        self.others_left = None
        return self._obs(), {"frame": self.start_frame, "seed": match_seed}

    def step(self, action):
        before = self.state
        self.spent = 0
        self.decisions += 1
        key = ACTIONS[int(action)]
        info = {"action": key}
        self.why = None
        try:
            if key in ("attack", "defend"):
                # world verbs answer at once and run on the next step's first frame
                ordered = self._army_order(key)
                _, ended = self._advance()
                refused = not ordered
            else:
                if self.army_order == 1:
                    self._reinforce()
                command = self._order(key) if key != "wait" else None
                taken, ended = self._advance((key, command) if command else None)
                refused = key != "wait" and not taken
            if refused:
                info["why"] = self.why
            if ended:
                # the match went away mid-step and took its objects with it. Against an AI that still had
                # units at the last look that is our defeat; the end screen is all that is left.
                won = self.others_left == 0
                info.update(ended=True, refused=refused, lost=not won, won=won, frame=self.state["frame"])
                w = self.reward_weights
                return self._obs(), -w["refused"] * refused - w["loss"] * (not won) + w["win"] * won, True, False, info
            self.state = self._read()
            lost, won = self._outcome()
        except (OSError, ControlError, KeyError, ValueError) as error:
            # a game that died or answered nonsense ends the episode; the next reset relaunches it
            self.broken = True
            # every key a caller logs (Monitor's info_keywords) is here too, or one dead game kills training
            info.update(error=str(error), refused=False, lost=False, won=False, dealt=0, taken=0, razed=0, kills=0)
            return self._obs(), 0.0, False, True, info
        s, w = self.state, self.reward_weights
        income = s["money"] - before["money"] + self.spent
        self.income = 0.7 * self.income + 0.3 * income * 60.0 / self.decision_seconds
        # by object id, so a loss is not mistaken for spending: new ids are what we finished, ids gone
        # are what was destroyed (a structure counts once it is finished)
        new = [s["ids"][i] for i in s["ids"].keys() - before["ids"].keys()]
        # combat units are spent to be lost; structures, dozers and Chinooks are what a raid costs us
        lost_value = sum(value for value, part in (before["ids"][i] for i in before["ids"].keys() - s["ids"].keys())
                         if part == "economy")
        hoard = w["hoard"] * max(0, s["money"] - HOARD_FREE) / 1000.0 * self.decision_seconds
        gone = [before["enemy"]["ids"][i] for i in before["enemy"]["ids"].keys() - s["enemy"]["ids"].keys()]
        razed = [value for value, finished_structure in gone if finished_structure]
        dealt = sum(value for value, finished_structure in gone if not finished_structure)
        economy = sum(value for value, part in new if part == "economy")
        army = sum(value for value, part in new if part == "army")
        # a rebuilt dozer or supply centre is a new id and earns the economy term again, so a raided
        # economy is worth restoring; the base is the target, the units in front of it only half count
        reward = (w["economy"] * economy + w["army"] * army + w["income"] * max(income, 0)
                  + w["razed"] * sum(razed) + w["dealt"] * dealt - w["taken"] * lost_value) / 1000.0
        reward += w["razed_each"] * len(razed) - hoard - w["refused"] * refused - w["loss"] * lost + w["win"] * won
        truncated = s["frame"] - self.start_frame >= self.episode_frames
        info.update(frame=s["frame"], money=s["money"], refused=refused, lost=lost, won=won,
                    dealt=dealt, taken=lost_value, razed=len(razed), kills=len(gone) - len(razed), hoard=hoard)
        return self._obs(), float(reward), lost or won, truncated and not (lost or won), info

    def close(self):
        if self.game is not None and not self.mock:
            self.game.close()
        self.game = None


def check():
    from gymnasium.utils.env_checker import check_env

    env = BuildOrderEnv(mock=True, episode_minutes=6)
    check_env(env, skip_render_check=True)

    def masks():
        return dict(zip(ACTIONS, env.action_masks()))

    env.reset(seed=1)
    mask = masks()
    assert mask["wait"] and mask["dozer"] and mask["power"] and mask["barracks"], mask
    assert not any(mask[k] for k in ("supply", "factory", "patriot", "chinook", "ranger", "humvee", "attack", "defend")), mask
    assert env.state["enemy"]["structures"] > 0 and env.state["enemy"]["cc"], env.state["enemy"]
    _, reward, _, _, info = env.step(ACTIONS.index("supply"))  # no power plant yet: the game refuses
    assert info["refused"] and reward < 0, (info, reward)
    env.step(ACTIONS.index("power"))
    for _ in range(3):  # ten game seconds of building
        env.step(0)
    mask = masks()
    assert env.state["built"]["power"] == 1 and mask["supply"] and mask["patriot"], env.state["built"]
    env.state["power"] = POWER_SURPLUS_MAX  # spare power: no more plants
    assert not masks()["power"]
    env.state["power"] = 5

    env.step(ACTIONS.index("barracks"))
    while len(env.state["soldiers"]) < ATTACK_MIN:
        env.step(ACTIONS.index("ranger") if masks()["ranger"] else 0)
    assert masks()["attack"] and not masks()["defend"]
    _, _, _, _, info = env.step(ACTIONS.index("attack"))
    assert not info["refused"] and env.army_order == 1 and not masks()["attack"], info  # no second order
    assert info["hoard"] > 0, info  # still sitting on most of the starting 10000
    for _ in range(COMMIT_DECISIONS):
        threat = env.state["enemy"]["near_value"] > THREAT_SHARE * max(env.state["army"], 1)
        assert threat or not masks()["defend"]  # committed
        env.step(0)
    sent = set(env.sent_ids)
    while not env.state["army_ids"].keys() - sent:  # one more ranger, trained after the order
        env.step(ACTIONS.index("ranger") if masks()["ranger"] else 0)
    env.step(0)
    late = [o for o in env.game.objects if o["id"] in env.state["army_ids"].keys() - sent]
    assert late and all(o["goal"] or o["x"] > 1300 for o in late), late  # it went after the attack too
    while not env.state["soldiers"]:  # the first wave may be gone by now
        env.step(ACTIONS.index("ranger") if masks()["ranger"] else 0)
    assert masks()["defend"], env.state["away"]  # the commitment is over, the attack still stands
    _, _, _, _, info = env.step(ACTIONS.index("defend"))
    assert not info["refused"] and env.army_order == -1, info
    for _ in range(COOLDOWN_DECISIONS - 1):  # no new attack straight after a recall
        assert not masks()["attack"]
        env.step(0)

    # the passive seat: an economy, a handful of tanks and one attack take its base
    passive = BuildOrderEnv(mock=True, difficulty="passive")
    passive.reset(seed=3)
    plan, done = ["power", "supply", "chinook", "factory"], False
    while not done:
        m = dict(zip(ACTIONS, passive.action_masks()))
        key = next((k for k in plan if m[k]), None)
        if key:
            plan.remove(key)
        else:
            key = "attack" if len(passive.state["soldiers"]) >= 5 and m["attack"] else "crusader" if m["crusader"] else "wait"
        _, _, terminated, truncated, info = passive.step(ACTIONS.index(key))
        done = terminated or truncated
    assert info["won"], info
    passive.close()

    # a masked random episode: the mask and the game must agree, so nothing masked-in is refused
    rng = np.random.default_rng(0)
    env.reset(seed=2)
    total, steps, done, dealt, taken, razed = 0.0, 0, False, 0, 0, 0
    while not done:
        mask = env.action_masks()
        assert mask[0]
        obs, reward, terminated, truncated, info = env.step(rng.choice(np.flatnonzero(mask)))
        assert env.observation_space.contains(obs) and not info["refused"], info
        total, steps, done = total + reward, steps + 1, terminated or truncated
        dealt, taken, razed = dealt + info["dealt"], taken + info["taken"], razed + info["razed"]
    print("check passed: masked random episode of %d decisions, return %.2f, %s, razed %d, dealt %d taken %d, %s" % (
        steps, total, "lost" if info["lost"] else "won" if info["won"] else "time up", razed, dealt, taken,
        {k: env.state["built"][k] for k in BUILDINGS} | {k: env.state["units"][k] for k in UNITS}))
    env.close()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--check", action="store_true", help="run the self-test against the mock game")
    if parser.parse_args().check:
        check()
    else:
        parser.print_help()
