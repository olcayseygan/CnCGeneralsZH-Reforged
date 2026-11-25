"""A Gymnasium environment that learns to command America against the Easy AI.

One episode is one offline skirmish of Zero Hour, slot 0 America against one AI seat. The env drives
generals.exe through its -control WebSocket (control_client.py, one folder up) with the training
verbs `step`, `objects`, `produce`, `construct`, `buildspot` and `sell` and the world verbs
`attackmove`, `attack` and `move` on `#id` selectors, and makes one decision every few game seconds.

The economy is not the policy's: fixed rules keep the dozers, the power, a supply centre on every
claimed pile (rebuilt after a raid) and its Chinooks. The policy chooses strategy: the barracks,
factory, airfield, Strategy Center and Patriots, a supply centre at the next free pile, units in
ones or fives, selling what has stopped paying, and three groups it fills from a reserve and sends
at a kind of target (production, economy, army, the Command Center), to the front or home. New
units wait in the reserve. MaskablePPO only ever picks from action_masks(), which is exact: what the
state and the money left after the economy's own orders allow.

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
DEFAULT_EVAL_PORT = 8900     # eval and --watch: never the ports a training run holds
OUR_SLOT, ENEMY_SLOT, NEUTRAL_SLOT = 0, 1, -1

# Costs, build times, power and prerequisites are retail INIZH.big Data\INI\Object\FactionBuilding.ini,
# AmericaVehicle.ini, AmericaInfantry.ini and AmericaAir.ini; BalanceReforged.ini changes none of
# them. source: the building that trains a unit. needs: any one of these finished first.
# arm: how a group filter sees a unit.
Item = namedtuple("Item", "template cost seconds source needs power cap arm")
ITEMS = {
    "cc":       Item("AmericaCommandCenter",           2000, 45, None,       (),                     0,  1, None),
    "dozer":    Item("AmericaVehicleDozer",            1000,  5, "cc",       (),                     0,  4, None),
    "power":    Item("AmericaPowerPlant",               800, 10, None,       (),                     5, 10, None),
    "barracks": Item("AmericaBarracks",                 600, 10, None,       (),                     0,  2, None),
    "supply":   Item("AmericaSupplyCenter",            2000, 10, None,       ("power",),            -1,  4, None),
    "factory":  Item("AmericaWarFactory",              2000, 15, None,       ("supply",),           -1,  2, None),
    "airfield": Item("AmericaAirfield",                1000, 30, None,       ("supply",),           -1,  2, None),
    "strategy": Item("AmericaStrategyCenter",          2500, 60, None,       ("factory", "airfield"), -2, 1, None),
    "patriot":  Item("AmericaPatriotBattery",          1000, 25, None,       ("power",),            -3,  4, None),
    "chinook":  Item("AmericaVehicleChinook",          1200, 10, "supply",   (),                     0,  8, None),
    "ranger":   Item("AmericaInfantryRanger",           225,  5, "barracks", (),                     0, 40, "infantry"),
    "missile":  Item("AmericaInfantryMissileDefender",  300,  5, "barracks", (),                     0, 40, "infantry"),
    "humvee":   Item("AmericaVehicleHumvee",            700, 10, "factory",  (),                     0, 40, "tank"),
    "crusader": Item("AmericaTankCrusader",             900, 10, "factory",  (),                     0, 40, "tank"),
    "paladin":  Item("AmericaTankPaladin",             1100, 12, "factory",  (),                     0, 40, "tank"),
    "tomahawk": Item("AmericaVehicleTomahawk",         1200, 20, "factory",  ("strategy",),          0, 40, "tank"),
    "comanche": Item("AmericaVehicleComanche",         1500, 20, "airfield", (),                     0, 40, "air"),
}
# the Paladin also needs a general's science; the script buys it with the first point the seat has
NEEDS_SCIENCE = {"paladin": "SCIENCE_PaladinTank"}
SCIENCE_RETRY_DECISIONS = 6  # how often the script asks again while no point is free (half a minute)
STRUCTURES = [key for key, item in ITEMS.items() if item.source is None and key != "cc"]
PRODUCERS = ["cc", "barracks", "supply", "factory", "airfield"]
ARMY_UNITS = [key for key, item in ITEMS.items() if item.arm]
TRAINED = ["dozer", "chinook"] + ARMY_UNITS
KEY_OF_TEMPLATE = {item.template: key for key, item in ITEMS.items()}

# -- the policy's actions, one flat Discrete -----------------------------------------------------------
POLICY_BUILDINGS = ["barracks", "factory", "airfield", "strategy", "patriot"]
BATCHES = (1, 5)
GROUPS = 3
ADD_FILTERS = ("all", "tank", "infantry", "air")
REMOVE_FILTERS = ("damaged", "tank", "infantry", "air")
TARGETS = ("production", "economy", "army", "cc")
ACTIONS = ([("wait",)] + [("build", key) for key in POLICY_BUILDINGS] + [("expand",)]
           + [("train", key, count) for key in ARMY_UNITS for count in BATCHES]
           + [("add", k, f) for k in range(GROUPS) for f in ADD_FILTERS]
           + [("remove", k, f) for k in range(GROUPS) for f in REMOVE_FILTERS]
           + [("attack", k, c) for k in range(GROUPS) for c in TARGETS]
           + [("defend", k) for k in range(GROUPS)] + [("retreat", k) for k in range(GROUPS)] + [("sell",)])
ACTION_NAMES = [" ".join(str(part) for part in action) for action in ACTIONS]

# -- rules --------------------------------------------------------------------------------------------
QUEUE_MAX = 2              # a producer with this many queued takes no more
ATTACK_MIN = 3             # units in a group before it may attack
ATTACK_VEHICLES = 2        # of which this many vehicles or aircraft: infantry alone does not take a GLA base
DAMAGED_PERCENT = 50       # "remove damaged" takes units under this much health, when objects reports health
HOME_RADIUS = 700.0        # a unit further than this from our CC counts as away
FRONT_DISTANCE = 350.0     # the defence point and the Patriot line, out from our CC toward theirs
STRIKE_DISTANCE = 500.0    # a group this near its target attacks it by id; further out it attack-moves there
RETARGET_DISTANCE = 400.0  # an enemy army target that moved this far is chased again
COMMIT_DECISIONS = 12      # an attack may not be recalled for this many decisions (a minute at 5 s)
COOLDOWN_DECISIONS = 6     # and the group attacks again no sooner than this after a recall
THREAT_RADIUS = 1400.0     # enemy army inside this of our CC is at the door...
THREAT_SHARE = 0.4         # ...and a threat once it is worth this share of our army
# the scripted economy
DOZERS = 2                 # dozers kept, one more once a second supply centre stands
CHINOOKS_PER_SUPPLY = 2    # the free one and one more
POWER_MARGIN = 2           # spare power the script keeps, counting what is being built
PILE_RADIUS = 500.0        # a supply centre this near a pile works it
ENEMY_PILE_RADIUS = 900.0  # a pile this near an enemy structure is theirs
SPIRAL_RADIUS, SPIRAL_LAP, SPIRAL_ANGLE, SPIRAL_PER_LAP = 220.0, 60.0, 2.39996, 8
BUILDSPOT_RADIUS = 400.0
PATRIOT_SPACING = 140.0

# The enemy by what a group can be sent at. A GLA hole (GLAHole) counts for victory and rebuilds
# what stood there, so the "cc" target takes the holes once the CC is gone, then any structure left.
ENEMY_CC = re.compile(r"CommandCenter")
ENEMY_ECONOMY = re.compile(r"Supply|BlackMarket|Chinook|Hacker")
ENEMY_PRODUCTION = re.compile(r"Barracks|WarFactory|ArmsDealer|Airfield|AirField")
ENEMY_HOLE = re.compile(r"(?<!No)Hole$")
ENEMY_CATEGORIES = ("production", "economy", "army", "cc", "hole", "defence")
# ponytail: the enemy's templates have no cost table here, so an object we cannot price counts by its
# kind; a table of GLA and China costs from INIZH.big would make damage dealt exact
NOMINAL_VALUE = {"structure": 1000, "builder": 600, "unit": 300}

# Per 1000 credits: what we finish (the scripted economy a quarter, the army full), income, the enemy
# value we destroy and ours that is destroyed; a kill, a refused order and the end of the match are flat.
ARMY_WEIGHT = {"ranger": 0.4, "missile": 0.4, "humvee": 1.0, "crusader": 1.25, "paladin": 1.4,
               "tomahawk": 1.25, "comanche": 1.4}
REWARD = {"economy": 0.25, "army": 1.0, "income": 0.5, "razed": 3.0, "razed_each": 0.5, "dealt": 0.5,
          "taken": 1.0, "hoard": 0.01, "refused": 0.05, "loss": 20.0, "win": 50.0, "win_early": 150.0}
# win_early is paid for the share of the episode a win leaves unplayed: the Easy AI rebuilds what is
# razed, and without it farming its base for the rest of the match pays more than finishing it
HOARD_FREE = 3000          # money above this costs "hoard" per 1000 per game second

GROUP_OBS, RESERVE_OBS = 10, 5
OBS_SIZE = 4 + 2 * len(STRUCTURES) + len(TRAINED) + len(PRODUCERS) + 2 + 3 + GROUPS * GROUP_OBS + RESERVE_OBS \
    + 2 * len(ENEMY_CATEGORIES) + 3


def base_template(template):
    """AirF_AmericaBarracks -> AmericaBarracks: a general's copy counts as the stock building."""
    return template.split("_", 1)[1] if "_" in template else template


def enemy_category(o):
    template = base_template(o["template"])
    if ENEMY_CC.search(template):
        return "cc"
    if o["kind"] == "builder" or ENEMY_ECONOMY.search(template):
        return "economy"
    if o["kind"] != "structure":
        return "army"
    if ENEMY_PRODUCTION.search(template):
        return "production"
    return "hole" if ENEMY_HOLE.search(template) else "defence"


def distance(a, b):
    return math.hypot(a[0] - b[0], a[1] - b[1])


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
        """Send every command, then read one reply each, in order. produce, construct and sell answer
        only once a logic frame has run them, which in lockstep is inside the step sent after them."""
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


def new_group():
    return {"ids": set(), "order": None, "category": None, "target": None, "point": None, "struck": set(), "decision": 0}


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
        self.game = None
        self.broken = False
        self.episodes = 0
        self.action_space = gym.spaces.Discrete(len(ACTIONS))
        self.observation_space = gym.spaces.Box(-1.0, 1.0, (OBS_SIZE,), np.float32)

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
        if self.mock:  # never the real game, broken or not
            if self.game is None:
                from mock_game import MockGame
                self.game = MockGame(passive=self.difficulty == "passive")
            return self._skirmish(seed)
        if self.game is not None and not self.broken:
            try:
                return self._skirmish(seed)
            except (OSError, ControlError, TimeoutError, KeyError):
                pass
        self.close()  # relaunch: none yet, or the one we had stopped answering
        self.game = GameProcess(self.port, self.exe, self.opponent, self.difficulty, self.user_dir, self.headless)
        self.broken = False
        self._skirmish(seed)

    def _advance(self, orders=()):
        """Run one decision's frames; the orders, (key, command) pairs, go ahead of the first step.
        Returns (taken, why) per order, and True for ended when the match went away mid-step."""
        left, results, ended = self.step_frames, None, False
        while left > 0 and not ended:
            frames = min(WATCH_CHUNK_FRAMES if self.watch_speed > 0 else MAX_STEP_FRAMES, left)
            due = time.monotonic() + frames / (LOGICFRAMES_PER_SECOND * self.watch_speed) if self.watch_speed > 0 else 0
            commands = ([command for _, command in orders] if results is None else []) + ["step %d" % frames]
            replies = self.game.exchange(commands)
            if results is None:
                results = [self._taken(key, reply) for (key, _), reply in zip(orders, replies)]
            reply = replies[-1]
            if not reply.get("ok"):
                if "ended" not in str(reply.get("error")):
                    raise ControlError("step refused: %s" % reply.get("error"))
                ended = True
            left -= frames
            time.sleep(max(0.0, due - time.monotonic()))
        return results or [], ended

    def _taken(self, key, reply):
        taken = bool(reply.get("ok")) and reply.get("queued", 1) > 0
        if taken and key in ITEMS:
            self.spent += reply.get("cost") or ITEMS[key].cost * reply.get("queued", 1)
        return taken, None if taken else reply.get("error")

    def _read(self):
        reply = self.send("objects %d" % OUR_SLOT)
        if not reply.get("ok"):
            raise ControlError("objects refused: %s" % reply.get("error"))
        power = reply.get("power") or {}
        s = {"frame": reply["frame"], "money": reply["money"], "defeated": bool(reply.get("defeated")),
             "power": power.get("produced", 0) - power.get("consumed", 0),
             "built": dict.fromkeys(ITEMS, 0), "uc": dict.fromkeys(ITEMS, 0), "units": dict.fromkeys(ITEMS, 0),
             "queue": dict.fromkeys(PRODUCERS, 0), "producers": {key: [] for key in PRODUCERS},
             "sites": {key: [] for key in STRUCTURES},  # (id, x, y, finished)
             "idle": 0, "army_value": 0, "cc": None,
             "ids": {},          # every finished object: id -> (value, "economy" or "army")
             "army": {}}         # id -> (key, x, y, health percent or None)
        for o in reply["objects"]:
            key = KEY_OF_TEMPLATE.get(base_template(o["template"]))
            if key is None:
                continue
            item = ITEMS[key]
            if o["kind"] == "structure":
                finished = not o.get("underConstruction")
                if key in s["sites"]:
                    s["sites"][key].append((o["id"], o["x"], o["y"], finished))
                if not finished:
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
            else:
                s["units"][key] += 1
                if item.arm:
                    # an army id carries its value weighted by type: it is only read for the "army built"
                    # reward (our combat losses cost nothing), and tanks are what take a base
                    s["ids"][o["id"]] = (item.cost * ARMY_WEIGHT[key], "army")
                    s["army"][o["id"]] = (key, o["x"], o["y"], o.get("health"))
                    s["army_value"] += item.cost
                else:
                    s["ids"][o["id"]] = (item.cost, "economy")
                if o["kind"] == "builder" and o.get("idle"):
                    s["idle"] += 1
        home = (s["cc"]["x"], s["cc"]["y"]) if s["cc"] else getattr(self, "home", (0.0, 0.0))
        s["away"] = sum(distance((x, y), home) > HOME_RADIUS for _, x, y, _ in s["army"].values())
        s["enemy"] = self._read_enemy(home)
        s["piles"] = self._read_piles()
        return s

    def _read_enemy(self, home):
        """What the AI seat has, by target category, and how near and how strong its army is."""
        e = {"structures": 0, "structure_value": 0, "army_value": 0, "ids": {}, "nearest": None, "near_value": 0,
             "defeated": False, "by": {category: [] for category in ENEMY_CATEGORIES}}
        reply = self.send("objects %d" % ENEMY_SLOT)
        if not reply.get("ok"):
            return e
        e["defeated"] = bool(reply.get("defeated"))
        for o in reply["objects"]:
            # BuildCost from the game's INI (a general's copy shares the stock price), else by kind
            value = self.costs.get(o["template"]) or self.costs.get(base_template(o["template"])) \
                or NOMINAL_VALUE.get(o["kind"], 300)
            structure = o["kind"] == "structure"
            e["ids"][o["id"]] = (value, structure and not o.get("underConstruction"))
            category = enemy_category(o)
            e["by"][category].append((o["id"], o["x"], o["y"], value, structure))
            if structure:
                e["structures"] += 1
                e["structure_value"] += value
            elif category == "army":
                e["army_value"] += value
                away = distance((o["x"], o["y"]), home)
                e["nearest"] = away if e["nearest"] is None else min(e["nearest"], away)
                if away < THREAT_RADIUS:
                    e["near_value"] += value
        return e

    def _read_piles(self):
        """The map's supply piles with boxes left, id -> (x, y), from the neutral seat; None when that is
        refused. A spent dock may stay on the map with "boxes" 0, and counts as gone."""
        reply = self.send("objects %d" % NEUTRAL_SLOT)
        if not reply.get("ok"):
            return None
        return {o["id"]: (o["x"], o["y"]) for o in reply.get("objects", [])
                if "Supply" in o.get("template", "") and o.get("boxes", 1) > 0}

    def _outcome(self):
        status = self.send("status")
        players = [p for p in status.get("players", []) if p.get("slot", -1) >= 0]
        mine = [p for p in players if p["slot"] == OUR_SLOT]
        others = [p for p in players if p["slot"] != OUR_SLOT]
        # a match that is no longer running says nothing here; step() ends the episode on it anyway
        self.others_left = sum(p.get("units", 0) for p in others) if others else self.others_left
        lost = self.state["defeated"] or (bool(mine) and mine[0].get("units", 0) == 0)
        won = not lost and (self.state["enemy"]["defeated"] or (bool(others) and all(p.get("units", 0) == 0 for p in others)))
        return lost, won

    # -- the scripted economy -----------------------------------------------------------------------

    def _power_ahead(self):
        """Spare power once everything under construction is finished."""
        s = self.state
        return s["power"] + sum(ITEMS[key].power * s["uc"][key] for key in STRUCTURES)

    def _worked_piles(self):
        s = self.state
        centres = [(x, y) for _, x, y, _ in s["sites"]["supply"]]
        return {pile for pile, xy in (s["piles"] or {}).items() if any(distance(xy, c) <= PILE_RADIUS for c in centres)}

    def _plan(self):
        """The economy's orders for this decision, cheapest first to fall away: a CC when it is gone, the
        dozers, power, a supply centre on each claimed pile without one, the Chinooks. Leaves what is
        left over for the policy in self.budget and self.idle_left."""
        s = self.state
        plan, budget, idle = [], s["money"], s["idle"]

        def afford(key):
            return budget >= ITEMS[key].cost

        if s["cc"] is None:
            if idle and afford("cc"):
                plan.append(("cc", self.home))
                budget, idle = budget - ITEMS["cc"].cost, idle - 1
            self.plan, self.budget, self.idle_left = plan, budget, idle
            return
        want = DOZERS + (s["built"]["supply"] >= 2)
        if s["units"]["dozer"] + s["queue"]["cc"] < want and s["queue"]["cc"] < QUEUE_MAX and afford("dozer"):
            plan.append(("dozer", None))
            budget -= ITEMS["dozer"].cost
        if idle and self._power_ahead() < POWER_MARGIN and s["built"]["power"] + s["uc"]["power"] < ITEMS["power"].cap \
                and afford("power"):
            plan.append(("power", None))
            budget, idle = budget - ITEMS["power"].cost, idle - 1
        if idle and s["built"]["power"] and afford("supply") and s["built"]["supply"] + s["uc"]["supply"] < ITEMS["supply"].cap:
            spot = None
            if s["piles"]:
                worked = self._worked_piles()
                bare = [pile for pile in sorted(self.claimed) if pile not in worked and pile in s["piles"]]
                spot = s["piles"][bare[0]] if bare else None
            elif s["built"]["supply"] + s["uc"]["supply"] == 0:
                spot = self.home  # ponytail: no pile known, so the supply centre goes beside the CC
            if spot:
                plan.append(("supply", spot))
                budget, idle = budget - ITEMS["supply"].cost, idle - 1
        chinooks = s["units"]["chinook"] + s["queue"]["supply"]
        queues = [queue for _, queue in s["producers"]["supply"]]
        if chinooks < CHINOOKS_PER_SUPPLY * s["built"]["supply"] and queues and min(queues) < QUEUE_MAX and afford("chinook"):
            plan.append(("chinook", None))
            budget -= ITEMS["chinook"].cost
        wanted = [science for science in NEEDS_SCIENCE.values() if science not in self.sciences]
        if wanted and self.decisions % SCIENCE_RETRY_DECISIONS == 0:
            plan.append(("science", wanted[0]))
        self.plan, self.budget, self.idle_left = plan, budget, idle

    def _unclaimed_pile(self):
        """The nearest pile to our CC that nobody works and no enemy structure stands by."""
        s = self.state
        if not s["piles"]:
            return None
        worked = self._worked_piles()
        enemy = [(t[1], t[2]) for category in ENEMY_CATEGORIES for t in s["enemy"]["by"][category] if t[4]]
        free = [pile for pile, xy in s["piles"].items() if pile not in self.claimed and pile not in worked
                and all(distance(xy, spot) > ENEMY_PILE_RADIUS for spot in enemy)]
        return min(free, key=lambda pile: distance(s["piles"][pile], self.home)) if free else None

    def _sell_candidate(self):
        """The structure least worth keeping: a supply centre whose pile is gone, else a power plant
        the base can spare. None when nothing qualifies."""
        s = self.state
        if s["piles"] is not None:
            for site, x, y, finished in s["sites"]["supply"]:
                if finished and site not in self.sold and all(distance((x, y), xy) > PILE_RADIUS for xy in s["piles"].values()):
                    return site
        plants = [site for site, _, _, finished in s["sites"]["power"] if finished and site not in self.sold]
        if len(plants) > 1 and self._power_ahead() - ITEMS["power"].power >= POWER_MARGIN:
            return plants[-1]
        return None

    # -- orders -------------------------------------------------------------------------------------

    def _construct(self, key, spot=None):
        """The construct line for one building, placed by buildspot; None when no spot is found."""
        template = self.prefix + ITEMS[key].template
        if spot is not None:
            x, y = spot
        elif key == "patriot":
            # across the line from our CC to theirs, FRONT_DISTANCE out: centre, then left, right, ...
            n = self.state["built"]["patriot"] + self.state["uc"]["patriot"]
            side = PATRIOT_SPACING * ((n + 1) // 2) * (1 if n % 2 else -1)
            x = self.front[0] - self.facing[1] * side
            y = self.front[1] + self.facing[0] * side
        else:
            # golden-angle spiral round the CC, one more lap every eight buildings
            angle = self.placed * SPIRAL_ANGLE
            ring = SPIRAL_RADIUS + SPIRAL_LAP * (self.placed // SPIRAL_PER_LAP)
            x = self.home[0] + ring * math.cos(angle)
            y = self.home[1] + ring * math.sin(angle)
        found = self.send("buildspot %d %s %.0f %.0f %.0f" % (OUR_SLOT, template, x, y, BUILDSPOT_RADIUS))
        if not found.get("ok"):
            self.why = "buildspot: %s" % found.get("error")
            return None
        self.placed += 1
        return "construct %d %s %.0f %.0f" % (OUR_SLOT, template, found["x"], found["y"])

    def _produce(self, key, count):
        producers = self.state["producers"][ITEMS[key].source]
        building = min(producers, key=lambda p: p[1])[0]
        return "produce %d %s %s %d" % (OUR_SLOT, building, self.prefix + ITEMS[key].template, count)

    def _economy_orders(self):
        orders = []
        for key, spot in self.plan:
            if key == "science":
                orders.append((spot, "science %d %s" % (OUR_SLOT, spot)))
                continue
            command = self._produce(key, 1) if ITEMS[key].source else self._construct(key, spot)
            if command:
                orders.append((key, command))
        return orders

    def _policy_order(self, action):
        """The produce, construct or sell line for a build, expand, train or sell action."""
        verb = action[0]
        if verb == "build":
            return action[1], self._construct(action[1])
        if verb == "expand":
            pile = self._unclaimed_pile()
            self.claimed.add(pile)  # the script rebuilds it from now on, and drops it once it runs dry
            return "supply", self._construct("supply", self.state["piles"][pile])
        if verb == "train":
            return action[1], self._produce(action[1], action[2])
        self.selling = self._sell_candidate()
        return "sell", "sell %d %d" % (OUR_SLOT, self.selling)

    # -- groups -------------------------------------------------------------------------------------

    def _reserve(self):
        grouped = set().union(*(g["ids"] for g in self.groups))
        return [i for i in self.state["army"] if i not in grouped]

    def _matching(self, ids, which):
        army = self.state["army"]
        if which == "all":
            return sorted(ids)
        if which == "damaged":
            return sorted(i for i in ids if army[i][3] is not None and army[i][3] < DAMAGED_PERCENT)
        return sorted(i for i in ids if ITEMS[army[i][0]].arm == which)

    def _centre(self, ids):
        army = self.state["army"]
        points = [(army[i][1], army[i][2]) for i in ids]
        return (sum(p[0] for p in points) / len(points), sum(p[1] for p in points) / len(points)) if points else self.home

    def _target(self, category, near):
        """The enemy object a group sent at this category goes for: (id, x, y, category) or None. The
        CC target falls back to the holes, then to any structure left, so a group can finish a base."""
        by = self.state["enemy"]["by"]
        if category == "cc":
            found = by["cc"] or by["hole"] or [t for each in ENEMY_CATEGORIES for t in by[each] if t[4]]
        else:
            found = by[category]
        if not found:
            return None
        best = min(found, key=lambda t: distance((t[1], t[2]), near))
        return best[0], best[1], best[2]

    def _command(self, verb, ids, *rest):
        line = "%s %d #%s %s" % (verb, OUR_SLOT, ",".join(str(i) for i in sorted(ids)), " ".join(rest))
        reply = self.send(line.strip())
        if not reply.get("ok"):
            self.why = "%s: %s" % (verb, reply.get("error"))
        return bool(reply.get("ok"))

    def _send_order(self, g, ids):
        """Send group g's standing order to these of its units."""
        if not ids or g["order"] is None:
            return True
        if g["order"] == "defend":
            return self._command("attackmove", ids, "%.0f" % self.front[0], "%.0f" % self.front[1])
        if g["order"] == "retreat":
            return self._command("move", ids, "%.0f" % self.home[0], "%.0f" % self.home[1])
        # an attack: units still on the way attack-move to the target, fighting what they meet; a
        # unit close in attacks it by id (an army target is only ever attack-moved on)
        near = self._near(g, ids)
        taken = True
        if near:
            taken = self._command("attack", near, str(ENEMY_SLOT), "#%d" % g["target"])
            g["struck"] |= near
        far = set(ids) - near
        if far:
            taken &= self._command("attackmove", far, "%.0f" % g["point"][0], "%.0f" % g["point"][1])
        return taken

    def _near(self, g, ids):
        if g["category"] == "army":
            return set()
        army = self.state["army"]
        return {i for i in ids if distance(army[i][1:3], g["point"]) <= STRIKE_DISTANCE}

    def _aim(self, g):
        """Pick group g's target: the one it has while that stands (an army target is chased once it
        has moved), else the nearest of its kind. True when it changed."""
        alive = g["target"] in self.state["enemy"]["ids"]
        if alive and g["category"] != "army":
            return False
        found = self._target(g["category"], self._centre(g["ids"]))
        if found is None or (alive and distance(found[1:3], g["point"]) < RETARGET_DISTANCE):
            return False
        g["target"], g["point"], g["struck"] = found[0], found[1:3], set()
        return True

    def _drive_groups(self):
        """Every decision an attacking group follows its target, and its units that have arrived strike."""
        for g in self.groups:
            if g["order"] != "attack" or not g["ids"]:
                continue
            if self._aim(g):
                self._send_order(g, g["ids"])
            else:
                self._send_order(g, self._near(g, g["ids"]) - g["struck"])

    def _group_action(self, action):
        verb, k = action[0], action[1]
        g = self.groups[k]
        if verb == "add":
            ids = self._matching(self._reserve(), action[2])
            g["ids"].update(ids)
            retargeted = g["order"] == "attack" and self._aim(g)
            return self._send_order(g, g["ids"] if retargeted else ids)
        if verb == "remove":
            ids = self._matching(g["ids"], action[2])
            g["ids"].difference_update(ids)
            return self._command("move", ids, "%.0f" % self.home[0], "%.0f" % self.home[1])
        g["order"], g["decision"] = verb, self.decisions
        if verb == "attack":
            g["category"], g["target"] = action[2], None
            self._aim(g)
        return self._send_order(g, g["ids"])

    # -- the mask -----------------------------------------------------------------------------------

    def _allowed(self, action):
        s, verb = self.state, action[0]
        if verb == "wait":
            return True
        if verb in ("build", "expand"):
            key = "supply" if verb == "expand" else action[1]
            item = ITEMS[key]
            ready = (self.budget >= item.cost and self.idle_left > 0 and s["cc"] is not None
                     and s["built"][key] + s["uc"][key] < item.cap and (not item.needs or any(s["built"][n] for n in item.needs)))
            return ready and (verb == "build" or self._unclaimed_pile() is not None)
        if verb == "train":
            key, count = action[1], action[2]
            item = ITEMS[key]
            queues = [queue for _, queue in s["producers"][item.source]]
            return (NEEDS_SCIENCE.get(key, "") in self.sciences | {""} and bool(queues) and min(queues) < QUEUE_MAX
                    and (not item.needs or any(s["built"][n] for n in item.needs))
                    and self.budget >= count * item.cost and s["units"][key] + s["queue"][item.source] + count <= item.cap)
        if verb == "sell":
            return self._sell_candidate() is not None
        g = self.groups[action[1]]
        if verb == "add":
            return bool(self._matching(self._reserve(), action[2]))
        if verb == "remove":
            return bool(self._matching(g["ids"], action[2]))
        if not g["ids"]:
            return False
        since = self.decisions - g["decision"]
        attacking = g["order"] == "attack"
        if verb == "attack":
            # an attack is a commitment: no second order to the same target kind, no recall for
            # COMMIT_DECISIONS unless a real threat is at our door, and no new attack for
            # COOLDOWN_DECISIONS after a recall; otherwise a policy flips between them and the
            # group never arrives anywhere
            vehicles = sum(ITEMS[s["army"][i][0]].arm != "infantry" for i in g["ids"])
            cooling = g["order"] in ("defend", "retreat") and since < COOLDOWN_DECISIONS
            return (len(g["ids"]) >= ATTACK_MIN and vehicles >= ATTACK_VEHICLES and not cooling
                    and not (attacking and g["category"] == action[2]) and self._target(action[2], self._centre(g["ids"])) is not None)
        threatened = s["enemy"]["near_value"] > THREAT_SHARE * max(s["army_value"], 1)
        committed = attacking and since < COMMIT_DECISIONS
        return g["order"] != verb and (threatened or not committed)

    def action_masks(self):
        return np.array([self._allowed(action) for action in ACTIONS], dtype=bool)

    # -- gymnasium ----------------------------------------------------------------------------------

    def _group_obs(self, ids):
        army = self.state["army"]
        size = len(ids)
        arms = [ITEMS[army[i][0]].arm for i in ids]
        value = sum(ITEMS[army[i][0]].cost for i in ids)
        share = [arms.count(arm) / max(1, size) for arm in ("tank", "infantry", "air")]
        return size, value, share

    def _obs(self):
        s, e = self.state, self.state["enemy"]
        v = [s["money"] / 10000.0, self.income / 5000.0, s["power"] / 20.0,
             (s["frame"] - self.start_frame) / self.episode_frames]
        v += [s["built"][k] / 4.0 for k in STRUCTURES] + [s["uc"][k] / 2.0 for k in STRUCTURES]
        v += [s["units"][k] / 10.0 for k in TRAINED] + [s["queue"][k] / 5.0 for k in PRODUCERS]
        v += [s["idle"] / 3.0, self.budget / 10000.0]
        v += [len(self.claimed) / 4.0, float(self._unclaimed_pile() is not None), float(self._sell_candidate() is not None)]
        for g in self.groups:
            size, value, share = self._group_obs(g["ids"])
            centre = self._centre(g["ids"])
            since = self.decisions - g["decision"]
            v += [size / 20.0, value / 10000.0, share[0], share[2], (centre[0] - self.home[0]) / 5000.0,
                  (centre[1] - self.home[1]) / 5000.0, float(g["order"] == "attack"), float(g["order"] == "defend"),
                  float(g["order"] == "retreat"), min(1.0, since / COMMIT_DECISIONS)]
        size, value, share = self._group_obs(self._reserve())
        v += [size / 20.0, value / 10000.0] + share
        for category in ENEMY_CATEGORIES:
            v += [len(e["by"][category]) / 10.0, sum(t[3] for t in e["by"][category]) / 10000.0]
        v += [1.0 if e["nearest"] is None else e["nearest"] / 4000.0, e["near_value"] / 5000.0, s["army_value"] / 15000.0]
        return np.clip(np.asarray(v, dtype=np.float32), -1.0, 1.0)

    def _refresh(self):
        """After every read: dead units leave their groups, dry piles their claim, and the plan is made."""
        for g in self.groups:
            g["ids"] &= self.state["army"].keys()
        if self.state["piles"] is not None:
            self.claimed &= self.state["piles"].keys()
        self._plan()

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
            raise ControlError("no command center for slot %d" % OUR_SLOT)
        self.prefix = self.state["cc"]["template"][:-len(base_template(self.state["cc"]["template"]))]
        self.home = (self.state["cc"]["x"], self.state["cc"]["y"])
        self.groups = [new_group() for _ in range(GROUPS)]
        self.decisions, self.placed, self.income, self.others_left = 0, 0, 0.0, None
        self.sold, self.selling, self.sciences = set(), None, set()
        piles = self.state["piles"] or {}
        self.claimed = {min(piles, key=lambda pile: distance(piles[pile], self.home))} if piles else set()
        by = self.state["enemy"]["by"]
        enemy = next(((t[1], t[2]) for category in ("cc", "production", "economy", "defence") for t in by[category]), None)
        dx, dy = (enemy[0] - self.home[0], enemy[1] - self.home[1]) if enemy else (1.0, 0.0)
        length = math.hypot(dx, dy) or 1.0
        self.facing = (dx / length, dy / length)
        self.front = (self.home[0] + self.facing[0] * FRONT_DISTANCE, self.home[1] + self.facing[1] * FRONT_DISTANCE)
        self.start_frame = self.state["frame"]
        self._refresh()
        return self._obs(), {"frame": self.start_frame, "seed": match_seed}

    def step(self, action):
        before = self.state
        self.spent = 0
        self.decisions += 1
        act = ACTIONS[int(action)]
        info = {"action": ACTION_NAMES[int(action)]}
        self.why = None
        try:
            self._drive_groups()
            orders = self._economy_orders()
            ordered, policy = True, None
            if act[0] in ("add", "remove", "attack", "defend", "retreat"):
                ordered = self._group_action(act)  # world verbs answer at once and run on the next frame
            elif act[0] != "wait":
                policy = self._policy_order(act)
                ordered = policy[1] is not None
            results, ended = self._advance(orders + ([policy] if policy and policy[1] else []))
            if policy and policy[1]:
                ordered, why = results[-1]
                self.why = self.why or why
                if ordered and act[0] == "sell":
                    self.sold.add(self.selling)
            info["script"] = [key for (key, _), (taken, _) in zip(orders, results) if taken]
            self.sciences.update(key for (key, _), (taken, why) in zip(orders, results)
                                 if key.startswith("SCIENCE_") and (taken or "already owned" in str(why)))
            refused = not ordered
            if refused:
                info["why"] = self.why
            if ended:
                # the match went away mid-step and took its objects with it. Against an AI that still had
                # units at the last look that is our defeat; the end screen is all that is left.
                won = self.others_left == 0
                info.update(ended=True, refused=refused, lost=not won, won=won, frame=self.state["frame"],
                            dealt=0, taken=0, razed=0, kills=0)
                w = self.reward_weights
                return self._obs(), -w["refused"] * refused - w["loss"] * (not won) + self._win_reward() * won, True, False, info
            self.state = self._read()
            self._refresh()
            lost, won = self._outcome()
        except (OSError, ControlError, KeyError, ValueError) as error:
            # a game that died or answered nonsense ends the episode; the next reset relaunches it.
            # The mock never dies: there it is a bug in this file
            if self.mock:
                raise
            self.broken = True
            # every key a caller logs (Monitor's info_keywords) is here too, or one dead game kills training
            info.update(error=str(error), refused=False, lost=False, won=False, dealt=0, taken=0, razed=0, kills=0)
            return self._obs(), 0.0, False, True, info
        s, w = self.state, self.reward_weights
        income = s["money"] - before["money"] + self.spent
        self.income = 0.7 * self.income + 0.3 * income * 60.0 / self.decision_seconds
        # by object id, so a loss is not mistaken for spending: new ids are what we finished, ids gone
        # are what was destroyed (a structure counts once it is finished; what we sold is not a loss)
        new = [s["ids"][i] for i in s["ids"].keys() - before["ids"].keys()]
        # combat units are spent to be lost; structures, dozers and Chinooks are what a raid costs us
        lost_value = sum(value for value, part in (before["ids"][i] for i in before["ids"].keys() - s["ids"].keys() - self.sold)
                         if part == "economy")
        hoard = w["hoard"] * max(0, s["money"] - HOARD_FREE) / 1000.0 * self.decision_seconds
        gone = [before["enemy"]["ids"][i] for i in before["enemy"]["ids"].keys() - s["enemy"]["ids"].keys()]
        razed = [value for value, finished_structure in gone if finished_structure]
        dealt = sum(value for value, finished_structure in gone if not finished_structure)
        economy = sum(value for value, part in new if part == "economy")
        army = sum(value for value, part in new if part == "army")
        reward = (w["economy"] * economy + w["army"] * army + w["income"] * max(income, 0)
                  + w["razed"] * sum(razed) + w["dealt"] * dealt - w["taken"] * lost_value) / 1000.0
        reward += w["razed_each"] * len(razed) - hoard - w["refused"] * refused - w["loss"] * lost + self._win_reward() * won
        truncated = s["frame"] - self.start_frame >= self.episode_frames
        info.update(frame=s["frame"], money=s["money"], refused=refused, lost=lost, won=won,
                    dealt=dealt, taken=lost_value, razed=len(razed), kills=len(gone) - len(razed), hoard=hoard)
        return self._obs(), float(reward), lost or won, truncated and not (lost or won), info

    def _win_reward(self):
        left = max(0.0, 1.0 - (self.state["frame"] - self.start_frame) / self.episode_frames)
        return self.reward_weights["win"] + self.reward_weights["win_early"] * left

    def close(self):
        if self.game is not None and not self.mock:
            self.game.close()
        self.game = None


def check():
    from gymnasium.utils.env_checker import check_env

    env = BuildOrderEnv(mock=True, episode_minutes=8)
    check_env(env, skip_render_check=True)

    def masks():
        return dict(zip(ACTION_NAMES, env.action_masks()))

    def play(name):
        assert masks()[name], (name, {k: v for k, v in masks().items() if v})
        return env.step(ACTION_NAMES.index(name))

    def wait_until(condition, limit=60):
        for _ in range(limit):
            if condition():
                return
            env.step(0)
        raise AssertionError("condition never came true")

    env.reset(seed=1)
    # the script starts the economy itself: a second dozer, power (which takes the only dozer) and
    # an ask for the Paladin's science
    assert [key for key, _ in env.plan] == ["dozer", "power", "science"], env.plan
    assert [k for k, v in masks().items() if v] == ["wait"], masks()
    # the economy runs itself: power, a supply centre on the home pile, its Chinooks, a second dozer
    wait_until(lambda: env.state["built"]["supply"] and env.state["units"]["chinook"] >= CHINOOKS_PER_SUPPLY
               and env.state["units"]["dozer"] >= DOZERS)
    assert env._worked_piles() == env.claimed and len(env.claimed) == 1, (env.claimed, env.state["sites"]["supply"])
    assert masks()["expand"] and masks()["build factory"] and masks()["build airfield"]

    play("build barracks")
    wait_until(lambda: masks()["train ranger 5"])
    play("train ranger 5")
    play("build factory")
    wait_until(lambda: masks()["train crusader 5"])
    assert not masks()["train tomahawk 1"]  # no Strategy Center
    assert not masks()["train paladin 1"] or "SCIENCE_PaladinTank" in env.sciences, env.state["frame"]
    play("train crusader 5")
    wait_until(lambda: len(env._reserve()) >= 8)
    assert not any(env.groups[k]["ids"] for k in range(GROUPS))  # new units wait in the reserve
    play("add 0 infantry")
    assert not masks()["attack 0 cc"]  # infantry only: no attack before the vehicles
    play("add 0 tank")
    assert masks()["attack 0 cc"] and masks()["attack 0 production"] and not masks()["attack 1 cc"]
    _, _, _, _, info = play("attack 0 production")
    g = env.groups[0]
    assert not info["refused"] and g["order"] == "attack" and g["category"] == "production", (info, g)
    assert not masks()["attack 0 production"] and masks()["attack 0 cc"]  # no second order to the same kind
    sent = [o for o in env.game.objects if o["id"] in g["ids"]]
    assert all(o["goal"] for o in sent), sent  # every unit of the group went
    for _ in range(COMMIT_DECISIONS - 1):
        threat = env.state["enemy"]["near_value"] > THREAT_SHARE * max(env.state["army_value"], 1)
        assert threat or not (masks()["defend 0"] or masks()["retreat 0"])  # committed
        env.step(0)
    # a unit trained now waits in the reserve, and one "add" sends it after the attack
    wait_until(lambda: masks()["train crusader 1"])
    play("train crusader 1")
    wait_until(lambda: env._matching(env._reserve(), "tank"))
    late = env._matching(env._reserve(), "tank")
    play("add 0 tank")
    assert set(late) <= g["ids"] and all(o["goal"] or o.get("target") for o in env.game.objects if o["id"] in late)
    play("remove 0 infantry")
    assert not env._matching(g["ids"], "infantry")
    while env.decisions - g["decision"] < COMMIT_DECISIONS:
        env.step(0)
    if g["ids"]:
        play("retreat 0")
        assert g["order"] == "retreat"
        for _ in range(COOLDOWN_DECISIONS - 1):  # no new attack straight after a recall
            assert not masks()["attack 0 cc"]
            env.step(0)

    # damaged: the mock reports health, and a hurt unit can be pulled out
    if not masks()["add 1 all"]:
        wait_until(lambda: masks()["train ranger 1"])
        play("train ranger 1")
        wait_until(lambda: masks()["add 1 all"])
    play("add 1 all")
    if env.groups[1]["ids"]:
        hurt = sorted(env.groups[1]["ids"])[0]
        next(o for o in env.game.objects if o["id"] == hurt)["hp"] = 1.0
        env.step(0)
        if hurt in env.groups[1]["ids"]:
            play("remove 1 damaged")
            assert hurt not in env.groups[1]["ids"]

    # expand: a second supply centre at the next free pile, kept up by the script
    _, _, _, _, info = play("expand")
    assert not info["refused"] and len(env.claimed) == 2, (info, env.claimed)
    wait_until(lambda: env._worked_piles() >= env.claimed)

    # sell: the home pile runs dry, its claim lapses, its supply centre is sold and the sale is no loss
    assert not masks()["sell"]
    home_pile = min(env.game.piles, key=lambda p: distance((p["x"], p["y"]), env.home))
    home_pile["money"] = 1.0
    env.step(0)
    assert home_pile["id"] not in env.claimed and masks()["sell"], (env.claimed, env._sell_candidate())
    dry = env._sell_candidate()
    _, reward, _, _, info = play("sell")
    assert not info["refused"] and info["taken"] == 0 and dry in env.sold, info
    env.step(0)
    assert dry not in {site for site, *_ in env.state["sites"]["supply"]} and not masks()["sell"]
    assert all(key != "supply" or spot != (home_pile["x"], home_pile["y"]) for key, spot in env.plan)  # not rebuilt
    spare = -1  # a second plant and power to spare: the plant is the candidate
    env.state["sites"]["power"].append((spare, 0.0, 0.0, True))
    env.state["power"] += 2 * ITEMS["power"].power
    assert env._sell_candidate() == spare
    env.state["power"] -= env._power_ahead() - (POWER_MARGIN + ITEMS["power"].power - 1)
    assert env._sell_candidate() is None  # one short of the margin once it is gone

    # the script buys the Paladin's science with the first point, and the Paladin opens
    wait_until(lambda: "SCIENCE_PaladinTank" in env.sciences, 80)
    wait_until(lambda: masks()["train paladin 1"])
    _, _, _, _, info = play("train paladin 1")
    assert not info["refused"], info
    env.close()

    # the passive seat: a factory, tanks in one group and one attack take its base
    passive = BuildOrderEnv(mock=True, difficulty="passive")
    passive.reset(seed=3)
    done = False
    while not done:
        m = dict(zip(ACTION_NAMES, passive.action_masks()))
        group = passive.groups[0]
        if m["build factory"]:
            name = "build factory"
        elif len(group["ids"]) >= 5 and m["attack 0 cc"]:
            name = "attack 0 cc"
        elif m["add 0 tank"]:
            name = "add 0 tank"
        else:
            name = "train crusader 1" if m["train crusader 1"] else "wait"
        _, _, terminated, truncated, info = passive.step(ACTION_NAMES.index(name))
        done = terminated or truncated
    assert info["won"], info
    passive.close()

    # a masked random episode: the mask and the game must agree, so nothing masked-in is refused
    rng = np.random.default_rng(0)
    env = BuildOrderEnv(mock=True, episode_minutes=8)
    env.reset(seed=2)
    total, steps, done, dealt, taken, razed = 0.0, 0, False, 0, 0, 0
    while not done:
        mask = env.action_masks()
        assert mask[0]
        obs, reward, terminated, truncated, info = env.step(rng.choice(np.flatnonzero(mask)))
        assert env.observation_space.contains(obs) and not info["refused"], info
        total, steps, done = total + reward, steps + 1, terminated or truncated
        dealt, taken, razed = dealt + info["dealt"], taken + info["taken"], razed + info["razed"]
    print("check passed: %d actions, masked random episode of %d decisions, return %.2f, %s, razed %d, dealt %d taken %d, %s" % (
        len(ACTIONS), steps, total, "lost" if info["lost"] else "won" if info["won"] else "time up", razed, dealt, taken,
        {k: env.state["built"][k] for k in STRUCTURES} | {k: env.state["units"][k] for k in TRAINED}))
    env.close()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--check", action="store_true", help="run the self-test against the mock game")
    if parser.parse_args().check:
        check()
    else:
        parser.print_help()
