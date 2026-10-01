"""Play USA against the easy computer player one macro decision at a time.

Each episode is one real skirmish on Winter Wolf: slot 0 is the local seat, America, driven over the
-control socket; slot 1 is the built-in AI at -aidiff easy, America as well. Without -observer and
-takeover the autoskirmish lobby puts the local player in slot 0 and an AI in every other slot, so one
socket and one AI share the match with no engine change. Every DECISION_FRAMES logic frames (five
seconds) the agent picks one of ACTIONS; the game is held between decisions by the lockstep `step` verb.

The match is played under -ruleset usabasic, a rule the logic holds for both seats: the four
buildings and three units ACTIONS names are all either side can make, and upgrades, generals' powers,
promotions and superweapons are off. The mirror is the agent's own action space, so the AI has
nothing the agent cannot answer.

Buildings go down with `construct`, the placement click, which the logic checks for money,
prerequisites and ground. The agent does not choose where: the env tries the next free spot on a ring
around the command center and moves on to the following spot when the ground refuses. Units come from
`produce`. mask() says which actions get past the checks act() makes before it tries anything (money,
a dozer, the prerequisite, a producer, an army, a spot left), and the trainer picks only from those.
What the game still refuses after that, mostly ground no spot fits, costs REFUSED_PENALTY and
otherwise does nothing.

Reward is the change in net-worth lead, our money plus what our objects are worth minus what the
enemy's objects are worth, in units of WORTH_SCALE, plus WIN_REWARD or -WIN_REWARD when a side has nothing left. An
object's worth is its build cost scaled by its health, so spending is neutral, income and kills pay,
losses cost. It is a potential difference, so it shapes without changing which policy is best.

With watch=True the game runs in a window with the camera locked on our command center, the game's
own interface off (-cinema nohud) and show() writing the trainer's lines in the top left corner.

The game is launched from a copy of generals.exe, generals_rl.exe, so that a build can still replace
generals.exe in Run/ while training games are running.

    python macro_env.py          # self-check of the cost table, discretize and reward, no game needed
"""

import os
import re
import shutil
import socket
import struct
import subprocess
import sys
import time

import numpy as np

TOOLS = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
sys.path.insert(0, TOOLS)
from control_client import Control, ControlError  # noqa: E402
from humvee_env import kill_with_python  # noqa: E402

RUN_FOLDER = os.environ.get("ZHR_RUN") or os.path.normpath(os.path.join(TOOLS, "..", "..", "Run"))
EXE = "generals_rl.exe"
PORT = 8811                     # 8787, 8788 and 8797 belong to other sessions
MAP = "Maps\\Winter Wolf\\Winter Wolf.map"
RULESET = "usabasic"            # both seats may make only what ACTIONS names
LOAD_TIMEOUT_SECONDS = 180

DECISION_FRAMES = 150           # five seconds of game time at 30 logic frames a second
FRAME_CAP = 27000               # fifteen minutes; a match still going then is a draw
PROBE_FRAMES = 2                # how long a construct or produce gets to show up before it is judged
PLACEMENT_TRIES = 3

WORTH_SCALE = 2000.0
WIN_REWARD = 10.0
REFUSED_PENALTY = 0.05

US, THEM = 0, 1
COMMAND_CENTER = "AmericaCommandCenter"
POWER, BARRACKS, SUPPLY, FACTORY = "AmericaPowerPlant", "AmericaBarracks", "AmericaSupplyCenter", "AmericaWarFactory"
DOZER, RANGER, CRUSADER = "AmericaVehicleDozer", "AmericaInfantryRanger", "AmericaTankCrusader"
PREREQUISITE = {POWER: None, BARRACKS: None, SUPPLY: POWER, FACTORY: SUPPLY}

# (name, kind, what, producer, count)
ACTIONS = [
    ("noop", None, None, None, 0),
    ("build power", "build", POWER, None, 1),
    ("build barracks", "build", BARRACKS, None, 1),
    ("build supply", "build", SUPPLY, None, 1),
    ("build factory", "build", FACTORY, None, 1),
    ("train dozer", "train", DOZER, COMMAND_CENTER, 1),
    ("train rangers", "train", RANGER, BARRACKS, 2),
    ("train crusader", "train", CRUSADER, FACTORY, 1),
    ("attack", "attack", None, None, 0),
    ("gather", "gather", None, None, 0),
]
N_ACTIONS = len(ACTIONS)

MONEY_EDGES = (900, 2000)
ARMY_EDGES = (1, 5, 12)
THREAT_RADIUS = 600.0
N_STATES = (len(MONEY_EDGES) + 1) * 16 * 2 * (len(ARMY_EDGES) + 1) * 2 * 2


# -- what things cost and what they are, out of the game's own INI ---------------------------------

def read_templates(run_folder=RUN_FOLDER):
    """{template: (build cost, kinds)} for every Object in INIZH.big's Object/*.ini.

    ponytail: INIZH.big only; a cost overridden by PatchINI.big or a loose Run/Data/INI file is read
    at its retail value, which only skews the shaping."""
    templates, reskins = {}, {}
    with open(os.path.join(run_folder, "INIZH.big"), "rb") as f:
        f.read(8)
        count, = struct.unpack(">I", f.read(4))
        f.seek(0x10)
        index = []
        for _ in range(count):
            offset, size = struct.unpack(">II", f.read(8))
            name = b""
            while not name.endswith(b"\0"):
                name += f.read(1)
            index.append((name[:-1].decode("latin-1").lower().replace("\\", "/"), offset, size))
        for path, offset, size in index:
            if not path.startswith("data/ini/object/"):
                continue
            f.seek(offset)
            current = None
            for line in f.read(size).decode("latin-1").splitlines():
                line = line.split(";")[0]
                header = re.match(r"(Object|ObjectReskin)\s+(\S+)(?:\s+(\S+))?", line)
                if header:
                    current = header.group(2)
                    templates[current] = [0, set()]
                    if header.group(1) == "ObjectReskin":
                        reskins[current] = header.group(3)
                    continue
                if current is None:
                    continue
                field = re.match(r"\s+(BuildCost|KindOf)\s*=\s*(.*)", line)
                if field and field.group(1) == "BuildCost":
                    templates[current][0] = int(float(field.group(2)))
                elif field:
                    templates[current][1] |= set(field.group(2).split())
    for name, base in reskins.items():
        if base in templates and not templates[name][0]:
            templates[name][0] = templates[base][0]
        if base in templates and not templates[name][1]:
            templates[name][1] = templates[base][1]
    return {name: (cost, frozenset(kinds)) for name, (cost, kinds) in templates.items()}


def is_army(kinds):
    return not kinds & {"STRUCTURE", "DOZER", "HARVESTER"}


def worth(units, templates):
    return sum(templates.get(u["template"], (0, ()))[0] * u["health"] / u["maxHealth"] for u in units)


def discretize(money, have, dozers, army, army_lead, threat):
    """Table row: money band x which of the four buildings stand x a dozer x army band x lead x threat."""
    row = int(np.searchsorted(MONEY_EDGES, money, side="right"))
    for building in (POWER, BARRACKS, SUPPLY, FACTORY):
        row = row * 2 + int(building in have)
    row = row * 2 + int(dozers > 0)
    row = row * (len(ARMY_EDGES) + 1) + int(np.searchsorted(ARMY_EDGES, army, side="right"))
    row = row * 2 + int(army_lead)
    return row * 2 + int(threat)


def placement_spots(centre, enemy):
    """Spots on rings around the command center, nearest first, the side facing the enemy last so
    the base does not grow into the attack lane."""
    toward = (enemy - centre) / max(1.0, np.linalg.norm(enemy - centre))
    spots = []
    for radius in (170, 260, 350, 440):
        for k in range(int(2 * np.pi * radius / 110)):
            angle = 2 * np.pi * k / int(2 * np.pi * radius / 110)
            offset = radius * np.array([np.cos(angle), np.sin(angle)])
            spots.append((radius + 60 * float(offset @ toward) / radius, tuple(centre + offset)))
    return [spot for _, spot in sorted(spots)]


def prepare_exe(run_folder=RUN_FOLDER):
    """Refresh Run/generals_rl.exe from generals.exe when the build has replaced it."""
    source, copy = os.path.join(run_folder, "generals.exe"), os.path.join(run_folder, EXE)
    if not os.path.exists(copy) or os.path.getmtime(copy) < os.path.getmtime(source):
        shutil.copy2(source, copy)


class MacroEnv(object):
    def __init__(self, port=PORT, run_folder=RUN_FOLDER, templates=None, log_prefix="rlmacro_", watch=False):
        self.port, self.run_folder, self.log_prefix, self.watch = port, run_folder, log_prefix, watch
        self.templates = templates or read_templates(run_folder)
        self.process = self.game = None

    # -- the match ----------------------------------------------------------

    def _launch(self, seed):
        with socket.socket() as probe:
            if probe.connect_ex(("127.0.0.1", self.port)) == 0:
                raise RuntimeError("port %d already has a game listening; pass another --port" % self.port)
        self.process = subprocess.Popen(
            [os.path.join(self.run_folder, EXE), "-win" if self.watch else "-headless", "-quickstart", "-noshellmap", "-multiInstance",
             "-noFPSLimit", "-map", MAP, "-autoskirmish", "2", "-aidiff", "easy",
             "-side", "0", "FactionAmerica", "-side", "1", "FactionAmerica", "-ruleset", RULESET, "-seed", str(seed),
             "-control", str(self.port), "-logPrefix", self.log_prefix]
            + (["-cinema", "nohud"] if self.watch else []),
            cwd=self.run_folder)
        self.job = kill_with_python(self.process)
        deadline = time.time() + LOAD_TIMEOUT_SECONDS
        while self.game is None:
            if self.process.poll() is not None:
                raise RuntimeError("generals.exe exited with %s before the socket opened" % self.process.returncode)
            try:
                self.game = Control(self.port, timeout=LOAD_TIMEOUT_SECONDS)
            except OSError:
                if time.time() > deadline:
                    raise
                time.sleep(0.1)
        # the logic runs on the wall clock until the first step, so take it as soon as the match is up
        while not self.game.status()["inGame"]:
            if time.time() > deadline:
                raise RuntimeError("the match did not start in %d s" % LOAD_TIMEOUT_SECONDS)
            time.sleep(0.02)
        self.frame = self.game.step(1)

    def _step(self, frames):
        """Advance; False when the game ended the match under the step."""
        reply = self.game.send("step %d" % frames)
        if reply.get("ok"):
            self.frame = reply["frame"]
        return reply.get("ok", False)

    def _read(self):
        players = {p["slot"]: p for p in self.game.status()["players"]}
        self.money, self.enemy_money = players[US]["money"], players[THEM]["money"]
        self.ours, self.theirs = self.game.units(US), self.game.units(THEM)

    def _count(self, template):
        return sum(u["template"] == template for u in self.ours)

    def _kinds(self, unit):
        return self.templates.get(unit["template"], (0, frozenset()))[1]

    def _army(self, units):
        return [u for u in units if is_army(self._kinds(u))]

    def potential(self):
        # the enemy's bank is left out: the easy AI hoards tens of thousands it never spends, and that
        # drift would swamp every signal the agent can act on
        return (self.money + worth(self.ours, self.templates) - worth(self.theirs, self.templates)) / WORTH_SCALE

    def observe(self):
        centre = self.base()
        army, enemy_army = self._army(self.ours), self._army(self.theirs)
        threat = any(np.hypot(u["x"] - centre[0], u["y"] - centre[1]) < THREAT_RADIUS for u in enemy_army)
        lead = worth(army, self.templates) >= worth(enemy_army, self.templates)
        have = {u["template"] for u in self.ours}
        return discretize(self.money, have, self._count(DOZER), len(army), lead, threat)

    def base(self):
        centres = [u for u in self.ours if u["template"] == COMMAND_CENTER] or self.ours
        return np.array([centres[0]["x"], centres[0]["y"]]) if centres else self.home

    # -- the actions --------------------------------------------------------

    def valid(self, action):
        """Whether act(action) gets past what the env can see before trying it: money, a dozer, the
        prerequisite, a producer, an army, a placement spot left. The game can still refuse after."""
        name, kind, what, producer, count = ACTIONS[action]
        if kind == "build":
            need = PREREQUISITE[what]
            return bool(self.money >= self.templates[what][0] and self._count(DOZER)
                        and (need is None or self._count(need)) and self.spots)
        if kind == "train":
            return bool(self.money >= self.templates[what][0] * count and self._count(producer))
        if kind in ("attack", "gather"):
            return bool(self._army(self.ours))
        return True

    def mask(self):
        """The actions valid() lets through now; noop always is."""
        return np.array([self.valid(action) for action in range(N_ACTIONS)])

    def _build(self, template):
        before = self._count(template)
        for _ in range(PLACEMENT_TRIES):
            if not self.spots:
                return False
            x, y = self.spots.pop(0)
            self.game.send("construct %d %s %g %g" % (US, template, x, y))
            self._step(PROBE_FRAMES)
            self._read()
            if self._count(template) > before:
                return True
        return False

    def _train(self, template, producer, count):
        cost = self.templates[template][0] * count
        before = self.money
        self.game.send("produce %d %s %s %d" % (US, producer, template, count))
        self._step(PROBE_FRAMES)
        self._read()
        return self.money <= before - cost / 2

    def _order(self, verb, point):
        templates = {u["template"] for u in self._army(self.ours)}
        for template in templates:
            self.game.send("%s %d %s %g %g" % (verb, US, template, point[0], point[1]))
        return bool(templates)

    def _target(self):
        """Their army when it is at our door, else their structure nearest our base, else anything of theirs."""
        if not self.theirs:
            return self.enemy_start
        centre = self.base()
        near = [u for u in self._army(self.theirs) if np.hypot(u["x"] - centre[0], u["y"] - centre[1]) < THREAT_RADIUS]
        near = near or [u for u in self.theirs if "STRUCTURE" in self._kinds(u)] or self.theirs
        nearest = min(near, key=lambda u: np.hypot(u["x"] - centre[0], u["y"] - centre[1]))
        return np.array([nearest["x"], nearest["y"]])

    def act(self, action):
        name, kind, what, producer, count = ACTIONS[action]
        if not self.valid(action):
            return False
        if kind == "build":
            return self._build(what)
        if kind == "train":
            return self._train(what, producer, count)
        if kind == "attack":
            return self._order("attackmove", self._target())
        if kind == "gather":
            return self._order("move", self.home)
        return True

    # -- the gym API --------------------------------------------------------

    def reset(self, seed=0):
        self.close()
        self._launch(seed)
        self._read()
        self.start_frame = self.frame
        self.enemy_start = np.mean([[u["x"], u["y"]] for u in self.theirs], axis=0)
        centre = self.base()
        self.home = centre + 0.25 * (self.enemy_start - centre)
        self.spots = placement_spots(centre, self.enemy_start)
        self.phi = self.potential()
        if self.watch:
            self.game.follow([u for u in self.ours if u["template"] == COMMAND_CENTER][0]["id"])
        return self.observe(), {"frame": self.frame, "mask": self.mask()}

    def step(self, action):
        started = self.frame
        done_ok = self.act(action)
        alive = self._step(max(1, DECISION_FRAMES - (self.frame - started)))
        if alive:
            self._read()
        phi = self.potential()
        r = phi - self.phi - (0.0 if done_ok else REFUSED_PENALTY)
        self.phi = phi
        result = None
        if not self.theirs:
            result = "win"
        elif not self.ours:
            result = "loss"
        elif not alive or self.frame >= FRAME_CAP:
            result = "draw"
        r += {"win": WIN_REWARD, "loss": -WIN_REWARD}.get(result, 0.0)
        info = {"frame": self.frame, "result": result, "refused": not done_ok, "lead": phi,
                "army": len(self._army(self.ours)), "enemy_army": len(self._army(self.theirs)), "mask": self.mask()}
        return self.observe() if self.ours else 0, r, result in ("win", "loss"), result == "draw", info

    def show(self, lines):
        """The overlay in the top left corner, in a watched game only."""
        if self.watch:
            self.game.overlay(lines)

    def close(self):
        if self.process is not None and self.process.poll() is None:
            try:
                self.game.send("quit")
                self.process.wait(10)
            except (OSError, ControlError, subprocess.TimeoutExpired, AttributeError):
                pass  # the socket may already be gone; the kill below is what matters
            if self.process.poll() is None:
                self.process.kill()
                self.process.wait()
        if self.game is not None:
            self.game.close()
        self.process = self.game = None


def self_check():
    templates = read_templates()
    assert templates[POWER][0] == 800 and "STRUCTURE" in templates[POWER][1]
    assert templates[CRUSADER][0] == 900 and is_army(templates[CRUSADER][1])
    assert not is_army(templates[DOZER][1]) and not is_army(templates["AmericaVehicleChinook"][1])
    assert not is_army(templates[COMMAND_CENTER][1]) and is_army(templates[RANGER][1])

    rows = {discretize(m, have, d, a, l, t)
            for m in (0, 900, 5000) for d in (0, 2) for a in (0, 1, 5, 40) for l in (0, 1) for t in (0, 1)
            for have in ({POWER}, {POWER, BARRACKS, SUPPLY, FACTORY}, set(), {BARRACKS, FACTORY})}
    assert len(rows) == 3 * 2 * 4 * 2 * 2 * 4 and all(0 <= row < N_STATES for row in rows)

    tank = [{"template": CRUSADER, "health": 240.0, "maxHealth": 480.0}]
    assert worth(tank, templates) == 450.0
    spots = placement_spots(np.array([0.0, 0.0]), np.array([1000.0, 0.0]))
    assert np.hypot(*spots[0]) < 200 and spots[0][0] < 0          # nearest ring, back of the base first

    env = MacroEnv(templates=templates)
    env.money, env.ours, env.spots = 0, [], spots
    assert list(env.mask()) == [True] + [False] * (N_ACTIONS - 1)    # noop is never masked
    env.money = 5000
    env.ours = [{"template": t} for t in (COMMAND_CENTER, DOZER, POWER, BARRACKS, RANGER)]
    names = [a[0] for a, ok in zip(ACTIONS, env.mask()) if ok]
    assert names == ["noop", "build power", "build barracks", "build supply", "train dozer", "train rangers",
                     "attack", "gather"]
    env.spots = []
    assert not env.mask()[1:5].any()                                 # no spot left, no building
    print("self-check passed: %d templates, %d states x %d actions" % (len(templates), N_STATES, N_ACTIONS))


if __name__ == "__main__":
    self_check()
