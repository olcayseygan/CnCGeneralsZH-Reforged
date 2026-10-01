"""Drive one Humvee to a point: the smallest reinforcement-learning task against the real game.

The game is launched once, headless, on a generated two-player map with -takeover so no computer
player moves anything. Slot 0 gets one AmericaVehicleHumvee. Every episode teleports it back to
the same home point and picks a random target around it; every action is a short move order in
one of eight directions (or a stop), followed by a lockstep `step` of FRAMES_PER_ACTION logic
frames over the -control socket.

With watch=True the game runs in a window instead: the camera is locked on the Humvee (the
`follow` verb) and a Comanche hovers over the target, so a person can watch it learn.

The API is gymnasium's without the dependency: reset() -> (obs, info) and
step(action) -> (obs, reward, terminated, truncated, info). The observation is the target's
position relative to the unit, (dx, dy) in world units; discretize() turns it into a table row.

    python humvee_env.py        # self-check of discretize/reward, no game needed
"""

import atexit
import ctypes
import math
import os
import socket
import subprocess
import sys
import time
from ctypes import wintypes

import numpy as np

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
from control_client import Control  # noqa: E402

RUN_FOLDER = os.environ.get("ZHR_RUN") or os.path.normpath(
    os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "..", "Run"))
PORT = 8797                     # not 8787: other sessions' games sit there
LOAD_TIMEOUT_SECONDS = 180
MATCH_SETTLE_FRAME = 60

UNIT = "AmericaVehicleHumvee"
# --watch only: a helicopter hovering over the target. It flies, so nothing on the ground collides
# with it. The system arrows (VerticalArrow, RallyPointMarker) spawn ownerless and teleport refuses them.
MARKER = "AmericaVehicleComanche"
FRAMES_PER_ACTION = 15          # half a second of game time at 30 logic frames a second
MOVE_DISTANCE = 80.0            # how far ahead each directional order points
STEP_LIMIT = 60                 # 900 frames, 30 s of game time
TARGET_MIN, TARGET_MAX = 120.0, 300.0
HOME_TOWARD_ENEMY = 0.3         # home sits this far along the line from our base to theirs
ARRIVE_RADIUS = 30.0

PROGRESS_SCALE = 20.0           # world units of progress worth 1 reward
STEP_COST = 0.05
ARRIVE_BONUS = 10.0

N_ACTIONS = 9                   # 0..7 = direction k * 45 degrees, 8 = stop
DISTANCE_EDGES = (60.0, 150.0, 300.0)
N_SECTORS = 8
N_STATES = N_SECTORS * (len(DISTANCE_EDGES) + 1)


def discretize(dx, dy):
    """Table row for a target at (dx, dy) from the unit: bearing sector x distance band."""
    sector = int(round(math.atan2(dy, dx) / (math.pi / 4))) % N_SECTORS
    band = int(np.searchsorted(DISTANCE_EDGES, math.hypot(dx, dy)))
    return sector * (len(DISTANCE_EDGES) + 1) + band


def reward(previous_distance, distance, arrived):
    return (previous_distance - distance) / PROGRESS_SCALE - STEP_COST + (ARRIVE_BONUS if arrived else 0.0)


def order_point(x, y, action):
    angle = action * math.pi / 4
    return x + MOVE_DISTANCE * math.cos(angle), y + MOVE_DISTANCE * math.sin(angle)


class _JobLimits(ctypes.Structure):
    """JOBOBJECT_EXTENDED_LIMIT_INFORMATION; only LimitFlags is set."""
    _fields_ = [("PerProcessUserTimeLimit", ctypes.c_int64), ("PerJobUserTimeLimit", ctypes.c_int64),
                ("LimitFlags", wintypes.DWORD), ("MinimumWorkingSetSize", ctypes.c_size_t),
                ("MaximumWorkingSetSize", ctypes.c_size_t), ("ActiveProcessLimit", wintypes.DWORD),
                ("Affinity", ctypes.c_size_t), ("PriorityClass", wintypes.DWORD),
                ("SchedulingClass", wintypes.DWORD), ("IoCounters", ctypes.c_uint64 * 6),
                ("ProcessMemoryLimit", ctypes.c_size_t), ("JobMemoryLimit", ctypes.c_size_t),
                ("PeakProcessMemoryUsed", ctypes.c_size_t), ("PeakJobMemoryUsed", ctypes.c_size_t)]


def kill_with_python(process):
    """Put the game in a job that Windows kills when this Python dies, however it dies: a closed
    console or a terminated trainer runs no finally and no atexit."""
    kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel32.CreateJobObjectW.restype = wintypes.HANDLE
    job = kernel32.CreateJobObjectW(None, None)
    limits = _JobLimits(LimitFlags=0x2000)  # JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE
    if not job or not kernel32.SetInformationJobObject(wintypes.HANDLE(job), 9, ctypes.byref(limits), ctypes.sizeof(limits)) \
            or not kernel32.AssignProcessToJobObject(wintypes.HANDLE(job), wintypes.HANDLE(int(process._handle))):
        raise ctypes.WinError(ctypes.get_last_error())
    return job  # the handle has to stay open for as long as the game should live


class HumveeEnv(object):
    def __init__(self, port=PORT, seed=None, run_folder=RUN_FOLDER, watch=False):
        self.rng = np.random.default_rng(seed)
        self.game = None
        self.watch = watch
        # another session's game on this port would answer instead of ours
        with socket.socket() as probe:
            if probe.connect_ex(("127.0.0.1", port)) == 0:
                raise RuntimeError("port %d already has a game listening; pass another --port" % port)
        self.process = subprocess.Popen(
            [os.path.join(run_folder, "generals.exe"), "-win" if watch else "-headless", "-quickstart", "-noshellmap",
             "-multiInstance", "-noFPSLimit", "-randommap", "1", "2", "small", "-autoskirmish", "2",
             "-takeover", "-side", "0", "FactionAmerica", "-side", "1", "FactionGLA", "-seed", "1",
             "-control", str(port), "-logPrefix", "rl_"],
            cwd=run_folder)
        self.job = kill_with_python(self.process)
        atexit.register(self.close)
        self.game = self._connect(port)
        while not self._wait_match():
            time.sleep(1)

        ours, theirs = self._centre(0), self._centre(1)
        self.home = ours + HOME_TOWARD_ENEMY * (theirs - ours)
        self._ok(self.game.spawn(0, UNIT, 1, self.home[0], self.home[1]))
        if watch:
            self._ok(self.game.spawn(0, MARKER, 1, self.home[0], self.home[1]))
        self.game.step(2)
        if watch:
            self._ok(self.game.follow(self.game.units(0, UNIT)[0]["id"]))

    # -- the socket -----------------------------------------------------------

    def _connect(self, port):
        deadline = time.time() + LOAD_TIMEOUT_SECONDS
        while True:
            if self.process.poll() is not None:
                raise RuntimeError("generals.exe exited with %s before the socket opened" % self.process.returncode)
            try:
                return Control(port, timeout=LOAD_TIMEOUT_SECONDS)
            except OSError:
                if time.time() > deadline:
                    raise
                time.sleep(1)

    def _wait_match(self):
        status = self._ok(self.game.status())
        return status["inGame"] and status["frame"] > MATCH_SETTLE_FRAME

    @staticmethod
    def _ok(reply):
        if not reply.get("ok"):
            raise RuntimeError("refused: %s" % reply)
        return reply

    def _centre(self, slot):
        units = self.game.units(slot)
        return np.array([np.mean([u["x"] for u in units]), np.mean([u["y"] for u in units])])

    def _position(self):
        unit, = self.game.units(0, UNIT)
        return np.array([unit["x"], unit["y"]])

    # -- the gym API ----------------------------------------------------------

    def _observe(self):
        self.position = self._position()
        relative = self.target - self.position
        return relative, float(np.hypot(*relative))

    def reset(self, seed=None):
        if seed is not None:
            self.rng = np.random.default_rng(seed)
        angle = self.rng.uniform(0, 2 * math.pi)
        radius = self.rng.uniform(TARGET_MIN, TARGET_MAX)
        self.target = self.home + radius * np.array([math.cos(angle), math.sin(angle)])
        self._ok(self.game.teleport(0, UNIT, self.home[0], self.home[1]))
        if self.watch:
            self._ok(self.game.teleport(0, MARKER, self.target[0], self.target[1]))
        self.game.step(2)
        self.steps = 0
        obs, self.distance = self._observe()
        return obs, {"distance": self.distance, "target": self.target}

    def step(self, action):
        if action == N_ACTIONS - 1:
            self._ok(self.game.stop(0, UNIT))
        else:
            self._ok(self.game.move(0, UNIT, *order_point(self.position[0], self.position[1], action)))
        self.game.step(FRAMES_PER_ACTION)
        self.steps += 1
        obs, distance = self._observe()
        arrived = distance < ARRIVE_RADIUS
        r = reward(self.distance, distance, arrived)
        self.distance = distance
        return obs, r, arrived, self.steps >= STEP_LIMIT and not arrived, {"distance": distance}

    def close(self):
        if self.process.poll() is None:
            if self.game is not None:
                try:
                    self.game.send("quit")
                    self.process.wait(10)
                except (OSError, RuntimeError, subprocess.TimeoutExpired):
                    pass  # the socket may already be gone; the kill below is what matters
            if self.process.poll() is None:
                self.process.kill()
                self.process.wait()
        elif self.game is not None:
            print("generals.exe had already exited, code %d" % self.process.returncode, file=sys.stderr)
        self.game = None


def self_check():
    bands = len(DISTANCE_EDGES) + 1
    assert discretize(100, 0) // bands == 0
    assert discretize(0, 100) // bands == 2
    assert discretize(-100, -100) // bands == 5
    assert discretize(100, -100) // bands == 7
    assert discretize(-100, -1) // bands == 4      # wraps at +-180 degrees
    assert [discretize(d, 0) % bands for d in (10, 100, 200, 1000)] == [0, 1, 2, 3]
    assert all(0 <= discretize(*xy) < N_STATES for xy in np.random.default_rng(0).uniform(-500, 500, (1000, 2)))

    # the action named by a target's sector moves toward it
    for sector in range(N_SECTORS):
        angle = sector * math.pi / 4
        dx, dy = 200 * math.cos(angle), 200 * math.sin(angle)
        assert discretize(dx, dy) // bands == sector
        x, y = order_point(0, 0, sector)
        assert math.hypot(dx - x, dy - y) < 200 - MOVE_DISTANCE + 1e-6

    assert reward(100, 80, False) > 0 > reward(100, 120, False)
    assert reward(100, 100, False) == -STEP_COST
    assert reward(40, 20, True) > ARRIVE_BONUS
    print("self-check passed: %d states x %d actions" % (N_STATES, N_ACTIONS))


if __name__ == "__main__":
    self_check()
