"""
PathLab - the game's ground pathfinding and group movement, in Python, at the game's own numbers.

Everything here is a port of what main runs today, read from GameEngine/Source/GameLogic: the A*
of AIPathfind.cpp with its flow charge (wall hug, traffic, crossing claims), its turn charge and its
9/4 estimate; Path::optimize; the group order of AIGroup::groupMoveToPosition (floodGroupGoals,
the vehicle trunk and its lanes); and the following and blocking of AIUpdate.cpp (blockedBy, the
blocked speed cap, crowdSteer, the stuck ladder).  The units are the game's own, read from the
install at run time by unitdata.py, and drive Locomotor.cpp's treads, wheels and legs under
PhysicsUpdate.cpp's friction.  Each piece names the C++ function it came from.  Where the sandbox has
to stand in for something it does not have (terrain heights, a resumable search) the comment says
so.  The only switches the game lacks are the proposed stuck ladder (--proposed) and steering by
time to collision (--ttc), both off by default.

    python pathlab.py                              # window: click to order, right drag to select
    python pathlab.py --headless 4 --map choke --units 12
    python pathlab.py --headless 4 --map open --group crusader:8,humvee:4,overlord:2
    python pathlab.py --headless 4 --scenario cross --map open --units 16
    python pathlab.py --headless 4 --sweep PF_TRAFFIC_NUM=0,4,8
    python pathlab.py --headless 8 --map edges --pairs 8 --units 0
    python pathlab.py --headless 4 --map choke --units 12 --proposed --ttc
    python pathlab.py --headless 1 --gamemap "Tournament Desert" --group crusader:8,ranger:8 --scenario cross

Controls
    left click          order the selection to a point (a player's move: floodGroupGoals)
    right drag          select units (right click on empty ground selects all)
    r                   draw the line checks Path::optimize made
    p                   draw the traffic map
    t                   draw the crossing claims for the moment now
    space               pause
    n                   new map
    tab                 re-issue the last order
    esc                 quit
"""

import argparse
import functools
import heapq
import itertools
import math
import os
import random
import sys
from collections.abc import Iterator, Sequence
from contextlib import contextmanager
from dataclasses import dataclass, field

Cell = tuple[int, int]
Point = tuple[float, float]

# ---------------------------------------------------------------------------------------------
# Time.  GameCommon.h: the logic ticks 30 times a wall second and each tick advances
# GAME_SPEED_PERCENT/100 of a retail tick, so a game second is 20 ticks at 150.
# ---------------------------------------------------------------------------------------------
LOGICFRAMES_PER_SECOND: int = 30
LOGICFRAMES_PER_RETAIL_FRAME: int = 1
GAME_SPEED_PERCENT: int = 150
LOGICFRAMES_PER_GAME_SECOND: int = LOGICFRAMES_PER_SECOND * 100 // GAME_SPEED_PERCENT
PER_FRAME_RATE: float = GAME_SPEED_PERCENT / (100.0 * LOGICFRAMES_PER_RETAIL_FRAME)
RETAIL_FRAME_RATIO: float = 100.0 * LOGICFRAMES_PER_RETAIL_FRAME / GAME_SPEED_PERCENT


def retail_frames_real(frames: float) -> float:
    """ConvertRetailFrames(Real): scaled by the ratio, never under one tick for a count of one or more."""
    wall = frames * LOGICFRAMES_PER_RETAIL_FRAME
    return max(frames * RETAIL_FRAME_RATIO, min(wall, 1.0))


def retail_frames(frames: int) -> int:
    """ConvertRetailFrames(Int): the wall count, then divided by the speed, rounded half away from
       zero in integers, and never 0 for a nonzero count."""
    wall = frames * LOGICFRAMES_PER_RETAIL_FRAME
    scaled = (wall * 100 + GAME_SPEED_PERCENT // 2) // GAME_SPEED_PERCENT
    return scaled if scaled else (1 if wall > 0 else 0)


def per_frame_rate(per_frame: float) -> float:
    """ConvertRetailPerFrameRate: an amount added every frame scales with speed over rate."""
    return per_frame * PER_FRAME_RATE


def per_frame_factor(factor: float) -> float:
    """ConvertRetailPerFrameFactor: a factor multiplied in every frame is raised to speed over rate."""
    return factor ** PER_FRAME_RATE


# ---------------------------------------------------------------------------------------------
# The grid and the search (AIPathfind.cpp / AIPathfind.h)
# ---------------------------------------------------------------------------------------------
CELL: float = 10.0                          # PATHFIND_CELL_SIZE_F
COST_ORTHOGONAL: int = 10
COST_DIAGONAL: int = 14
PATHFIND_CLOSE_ENOUGH: float = 1.0
ZONE_BLOCK_SIZE: int = 10                   # PathfindZoneManager::ZONE_BLOCK_SIZE
MAX_INTERNAL_CELL_COUNT: int = 40000        # continueFindPath
MAX_GROUND_PATH_CELLS: int = 3000           # findGroundPath
PATCH_CELL_LIMIT: int = 2000                # patchPath's CELL_LIMIT
MAX_OPTIMIZE_LOOKAHEAD: int = 32            # Path::optimize
STEER_CHECK_CELLS: float = 20.0             # computePointOnPath's line-check clip
MAX_RADIUS: int = 2                         # getRadiusAndCenter
PF_HEURISTIC_FLOW_NUM, PF_HEURISTIC_FLOW_DEN = 9, 4     # beginFlowSearch with an object
PF_HEURISTIC_RETAIL_NUM, PF_HEURISTIC_RETAIL_DEN = 3, 2  # ... and without one
PF_TURN_CAP_STEP: int = 28
PF_TURN_CAP_START: int = 60
PF_CLEARANCE_ORTHO: int = 5
PF_CLEARANCE_DIAG: int = 7
PF_CLEARANCE_MAX: int = 60
PF_CLEARANCE_SOFT: int = 13
PF_WALLHUG_NUM, PF_WALLHUG_DEN = 8, 5
PF_TRAFFIC_MAX: int = 255
PF_TRAFFIC_DEPOSIT: int = 24 // LOGICFRAMES_PER_RETAIL_FRAME
PF_TRAFFIC_DECAY_NUM, PF_TRAFFIC_DECAY_DEN = 54, 64
PF_TRAFFIC_DECAY_FRAMES: int = 3 * LOGICFRAMES_PER_RETAIL_FRAME     # wall frames, not game
PF_TRAFFIC_FULL: int = 192
PF_TRAFFIC_NUM, PF_TRAFFIC_DEN = 4, 1
PF_TRAFFIC_CELLS: int = 4096
PF_CLAIM_BUCKETS: int = 24
PF_CLAIM_UNITS_MAX: int = 4
PF_CLAIM_SAMPLES_MAX: int = 32
PF_CROSSING_NUM, PF_CROSSING_DEN = 1, 2
PF_CLAIM_BUCKET_FRAMES: int = LOGICFRAMES_PER_GAME_SECOND // 2
PF_CLAIM_REFRESH_FRAMES: int = LOGICFRAMES_PER_GAME_SECOND
PF_LANE_PROBE_CELLS: int = 16
FLOW_PLAN_SHARE: float = 0.8                # flowSpeedOf: plans at four fifths of top speed
# the path queue: processPathfindQueue and Pathfinder_queueCellBudget
PATHFIND_CELLS_PER_FRAME: int = 5000 // LOGICFRAMES_PER_RETAIL_FRAME
PATHFIND_REQUESTS_PER_FRAME: int = 3
PATHFIND_REQUESTS_PER_FRAME_DEEP: int = 6
PATHFIND_DEEP_QUEUE: int = 3 * LOGICFRAMES_PER_GAME_SECOND
PATHFIND_DEEP_STEP: int = LOGICFRAMES_PER_GAME_SECOND
PATHFIND_DEEP_CELLS: int = 4
MIN_REPATH_TIME: int = 10 * LOGICFRAMES_PER_RETAIL_FRAME     # AIStateMachine.h

# Group order (AIGroup.cpp, CrowdModel.h)
CROWD_TRUNK_DIAMETER: int = 6
CROWD_PLAN_STEPS: int = 4
CROWD_MAX_LANES: int = 5
CROWD_LANE_MIN_COS: float = 0.5
LANE_SPACING_AIR: float = 1.15              # groupMoveToPosition: spacing *= 1.15
MIN_FLOOD_CELLS: int = 400                  # floodGroupGoals
FLOOD_CELLS_PER_MEMBER: int = 32
MAX_MEMBERS_TRIED_PER_CELL: int = 8
MIN_MEMBER_SPEED: float = 1.0

# Following and blocking (AIUpdate.cpp:1666-1867)
FAST_AS_POSSIBLE: float = 999999.0
HEAD_ON_DOT: float = -0.5
HEAD_ON_PASS_FRAMES: int = retail_frames(8)
BUMP_RECOVERY: float = per_frame_factor(1.05)
MARCHING_SOLDIER_COS: float = 0.5           # blockedBy: a soldier this close to our heading is no block
MARCHING_SOLDIER_CRAWL: float = 0.33        # processCollision: a vehicle behind him keeps this share
CROWD_AIR: float = 4.0
CROWD_SEP_STEP: float = 1.5
CROWD_PASS_CLEAR: float = 6.0
CROWD_PASS_TIGHT: float = 1.5
CROWD_PASS_RETRY: int = retail_frames(10)
CROWD_TAPER_DIST: float = CELL * 8.0
CROWD_PLAN_END_SLACK: float = CELL * 3.0
CROWD_BRAKE_FRAMES: int = retail_frames(8)
CROWD_FAN_FRAMES: int = retail_frames(12)
CROWD_FAN_RATE: float = per_frame_rate(0.8)
CROWD_LANE_RATE_MIN: float = per_frame_rate(0.4)
CROWD_SEP_FILTER: float = 1.0 - per_frame_factor(1.0 - 0.13)
CROWD_RELEASE_FILTER: float = 1.0 - per_frame_factor(1.0 - 0.15)
CROWD_AIM_CRUISE: float = 1.0 - per_frame_factor(1.0 - 0.23)
CROWD_AIM_URGENT: float = 1.0 - per_frame_factor(1.0 - 0.67)
CROWD_AIM_DEAD: float = 0.035
CROWD_MERGE_FRAMES: int = retail_frames(45)
CROWD_TTC_FRAMES: int = retail_frames(21)
CROWD_REPATH_QUEUE: int = retail_frames(24)
CROWD_SCAN_FRAMES: int = retail_frames(2)
CROWD_HOLD_FRAMES: int = 3 * LOGICFRAMES_PER_GAME_SECOND // 2
CROWD_SCAN_NEAR_MAX: int = 16
CROWD_PROBE_CELLS: int = 16
CROWD_MAX_SAMPLES: int = 160
CROWD_LOOKAHEAD_CELLS: int = 2
STUCK_TURN_EPSILON: float = per_frame_rate(0.004)
STUCK_PRESS_FRAMES: int = retail_frames(10)
STUCK_REPATH_FRAMES: int = retail_frames(45)
STUCK_BACKOUT_FRAMES: int = retail_frames(66)
STUCK_COOL_FRAMES: int = retail_frames(90)
STUCK_CYCLE_FRAMES: int = retail_frames(150)
REPATH_DEBOUNCE_FRAMES: int = retail_frames(3)      # requestPath's "very quickly"
# The stuck ladder as designed, not yet in C++.  The values here are main's; PROPOSED_LADDER holds
# the ones measured to work, and each switches alone.
LADDER_REPATH_FLAG: bool = False        # AIStates.cpp:1908 also reads a pending flag crowdRepath raises
LADDER_STAGE_HOLD: int = 0              # AIUpdate.cpp:2824-2829, 2869-2873: the rung survives the path
                                        # wait and drops after this many frames of progress; 0 drops at once
LADDER_ALLY_FILTER: bool = False        # AIUpdate.cpp stuckRescue rung 2 leaves moving allies alone
PROPOSED_LADDER: dict[str, object] = {
    "LADDER_REPATH_FLAG": True,
    "LADDER_STAGE_HOLD": retail_frames(22),
    "LADDER_ALLY_FILTER": True,
}
# Anticipatory steering by time to collision (Karamouzas, Skinner and Guy 2014), not in C++.  Each
# neighbour the desired velocity would hit inside the horizon pushes away from the predicted contact,
# harder the sooner; the sum bends the aim and trims the speed after crowdSteer.  PROPOSED_TTC turns
# it on and drops the contact speed cap it replaces; the cap alone dropped is the control.
CROWD_TTC_STEER: bool = False           # doLocomotor would call ttcSteer right after crowdSteer
CONTACT_SPEED_CAP: bool = True          # processCollision writes m_curMaxBlockedSpeed
TTC_HORIZON_FRAMES: float = 3.0 * LOGICFRAMES_PER_GAME_SECOND
TTC_NEAR_FRAMES: float = 0.5 * LOGICFRAMES_PER_GAME_SECOND     # the push falls off as a square past this
TTC_STRENGTH: float = 0.75              # the push at contact, in shares of the unit's top speed
TTC_AIR: float = 2.0                    # added to the radius sum between two moving units
TTC_PARKED_SLACK: float = 4.0           # overlap allowed against a parked unit before it pushes
TTC_SLOT_REACH: float = CELL            # a parked unit this far past contact from my goal never pushes
TTC_KEEP_RIGHT: float = 0.6             # share of a push against the heading turned to the right
TTC_MAX_BEND: float = math.radians(60.0)
TTC_CREEP: float = 0.2                  # the speed is never trimmed below this share, the bump floor
TTC_GOAL_FADE: float = CELL * 2.0       # the push fades out over this last stretch of the route
TTC_WALL_LOOK: float = CELL * 1.5       # rock probed this far past the hull before a bend is taken
TTC_WALL_PROBES: tuple[float, ...] = (1.0, 0.5)         # shares of that reach probed
TTC_BEND_TRIES: tuple[float, ...] = (1.0, 0.5, 0.0)     # shares of the bend tried against rock
TTC_MIN_AIM: float = 0.01               # an aim point closer than this is not steered
TTC_EPSILON: float = 1e-6
TTC_EPSILON_FINE: float = 1e-9
PROPOSED_TTC: dict[str, object] = {
    "CROWD_TTC_STEER": True,
    "CONTACT_SPEED_CAP": False,
}

# Locomotion: Locomotor.cpp's EA drives for treads, wheels and legs, and PhysicsUpdate.cpp under them
TREADS, FOUR_WHEELS, MOTORCYCLE, TWO_LEGS = "TREADS", "FOUR_WHEELS", "MOTORCYCLE", "TWO_LEGS"
GROUND_DRIVE: frozenset[str] = frozenset((TREADS, FOUR_WHEELS, MOTORCYCLE))     # isGroundDrive
GROUND_TURN_RATE_FACTOR: float = 1.5        # getMaxTurnRate, Locomotor.cpp:1017
GROUND_MIN_TURN_RATE: float = math.radians(100.0) / LOGICFRAMES_PER_GAME_SECOND
TURN_NONE, TURN_POSITIVE, TURN_NEGATIVE = 0, 1, -1
ON_TARGET: float = 0.1                      # rotateObjAroundLocoPivot and the legs: rounding noise
TREAD_SLOW_ANGLE: float = math.pi / 4.0     # moveTowardsPositionTreads' QUAETERPI
TREAD_PIVOT_ANGLE: float = math.pi * 0.75
SLOW_DOWN_SPEED_DIVISOR: float = 1.5        # slowDownDist = (actualSpeed/1.5) * slowDownTime
TREAD_NEAR_GOAL: float = 2.0 * CELL
TREAD_NEAR_ANGLE_COEFF: float = 0.05
TREAD_NEAR_SPEED_FACTOR: float = per_frame_factor(0.6)
BRAKING_FACTOR_LATCHED: float = 1.1
MAX_BRAKING_FACTOR: float = 5.0
BRAKING_HALF_SHARE: float = 0.75            # slowDownDist past this share of the way: half braking
WHEEL_TURN_SPEED_SHARE: float = 0.25        # MinTurnSpeed raised to a quarter of top speed
WHEEL_SMALL_TURN: float = math.pi / 20.0
WHEEL_PROBE_TURN: float = math.pi / 12.0
WHEEL_PROBE_FRAMES: int = LOGICFRAMES_PER_GAME_SECOND // 2
WHEEL_PROBE_SWING_SHARE: float = 0.25
WHEEL_REACTION_FRAMES: float = RETAIL_FRAME_RATIO       # ConvertRetailFrameRatio(1.0f)
WHEEL_THREE_POINT_RADII: float = 5.0
DONUT_DISTANCE: float = 4.0 * CELL
DONUT_FRAMES: int = int(2.5 * LOGICFRAMES_PER_GAME_SECOND)
INFANTRY_YAW_EASE_FRAMES: float = retail_frames_real(2.0)
INFANTRY_STOP_FRAMES: float = retail_frames_real(3.0)
BRAKING_SLIDE_MIN: float = CELL / LOGICFRAMES_PER_GAME_SECOND  # locoUpdate_moveTowardsPosition's MIN_VEL
MOTIVE_FRAMES: int = LOGICFRAMES_PER_GAME_SECOND // 3          # PhysicsUpdate.cpp:83
VELOCITY_ZERO: float = per_frame_rate(0.001)
MIN_FRICTION: float = 0.01
MAX_FRICTION: float = 0.99

# The stand-in tank the tests drive and the run falls back to with no install: the old cut-down
# model's numbers (30 world units and 90 degrees a game second, a stop in 0.8 s), none of them EA's.
TANK_RADIUS: float = 11.0
TANK_SPEED: float = 30.0 / LOGICFRAMES_PER_GAME_SECOND          # per tick
TANK_TURN: float = math.radians(90.0) / LOGICFRAMES_PER_GAME_SECOND
TANK_ACCELERATION: float = TANK_SPEED / (0.8 * LOGICFRAMES_PER_GAME_SECOND)
DEFAULT_GROUP_KIND: str = "crusader"


@dataclass(frozen=True)
class UnitKind:
    """One unit's locomotor, footprint and friction in ticks: speeds per tick, turns in radians a
       tick, accelerations per tick squared, frictions as the INI's share per retail frame."""
    name: str
    appearance: str
    infantry: bool
    major_radius: float
    radius: float                           # the bounding circle
    speed: float
    speed_damaged: float
    turn_rate: float                        # the template's, before getMaxTurnRate's ground scale
    turn_rate_damaged: float
    acceleration: float
    acceleration_damaged: float
    braking: float
    min_speed: float = 0.0
    min_turn_speed: float = FAST_AS_POSSIBLE
    turn_pivot_offset: float = 0.0
    can_move_backwards: bool = False
    forward_friction: float = 0.15
    lateral_friction: float = 0.15
    extra_friction: float = 0.0
    close_enough: float = PATHFIND_CLOSE_ENOUGH
    geometry: str = "CYLINDER"
    minor_radius: float = 0.0

    @property
    def half_extents(self) -> Point:
        """The rectangle the partition's collision test uses: a box's own, a circle's bounding square
           (xy_collideTest_Rect_Circle sets its minor radius to its major)."""
        if self.geometry == "BOX":
            return self.major_radius, self.minor_radius
        return self.major_radius, self.major_radius

    def max_speed(self, damaged: bool) -> float:
        """getMaxSpeedForCondition."""
        return self.speed_damaged if damaged else self.speed

    def max_turn_rate(self, damaged: bool) -> float:
        """getMaxTurnRate with the object: a ground drive turns half as fast again, a healthy one no
           slower than 100 degrees a second, one scale for both rates."""
        turn = self.turn_rate_damaged if damaged else self.turn_rate
        if self.appearance in GROUND_DRIVE:
            scale = GROUND_TURN_RATE_FACTOR
            if 0.0 < self.turn_rate and self.turn_rate * scale < GROUND_MIN_TURN_RATE:
                scale = GROUND_MIN_TURN_RATE / self.turn_rate
            turn *= scale
        return turn

    def max_acceleration(self, damaged: bool) -> float:
        return self.acceleration_damaged if damaged else self.acceleration

    @property
    def forward_friction_per_tick(self) -> float:
        """PhysicsBehavior::getForwardFriction: clamped, then retailFrictionToFrame."""
        return friction_per_tick(self.forward_friction + self.extra_friction)

    @property
    def lateral_friction_per_tick(self) -> float:
        return friction_per_tick(self.lateral_friction + self.extra_friction)


def friction_per_tick(share_per_retail_frame: float) -> float:
    clamped = min(max(share_per_retail_frame, MIN_FRICTION), MAX_FRICTION)
    return 1.0 - per_frame_factor(1.0 - clamped)


STANDIN_TANK: UnitKind = UnitKind(
    name="standin", appearance=TREADS, infantry=False, major_radius=TANK_RADIUS, radius=TANK_RADIUS,
    speed=TANK_SPEED, speed_damaged=TANK_SPEED, turn_rate=TANK_TURN, turn_rate_damaged=TANK_TURN,
    acceleration=TANK_ACCELERATION, acceleration_damaged=TANK_ACCELERATION, braking=TANK_ACCELERATION)

# Map and measurement
MAP_WIDTH_CELLS: int = 96
MAP_HEIGHT_CELLS: int = 72
MAP_KINDS: tuple[str, ...] = ("choke", "open", "twogaps", "maze", "edges")
SCENARIOS: tuple[str, ...] = ("column", "cross")
ROCK_RADIUS_CELLS: int = 2
OPEN_MAP_ROCKS: int = 18
OPEN_MAP_MARGIN: int = 8
CHOKE_MAP_ROCKS: int = 10
CHOKE_MAP_MARGIN: int = 6
CHOKE_GAP_CELLS: int = 4
SCATTER_KEEP_CLEAR_CELLS: int = 6
NARROW_GAP_HALF_CELLS: int = 2
WIDE_GAP_ROWS: tuple[int, int] = (6, 13)
MAZE_SIZE: tuple[int, int] = (160, 120)
MAZE_PITCH: int = 6
MAZE_PASSAGE: int = 4
MAZE_LOOPS: float = 0.15
MAZE_ROOM: tuple[int, int] = (24, 26)
EDGES_SIZE: tuple[int, int] = (128, 96)
EDGES_WALL_EVERY: int = 2
EDGES_FIRST_WALL: int = 2
EDGES_NARROW: int = 2
EDGES_WIDE: int = 8
SPAWN_ROW_WIDTH: int = 5
SPAWN_STRIDE_CELLS: int = 3
SPAWN_CELL: Cell = (6, 12)
CROSS_SPAWN_TOP: Cell = (6, 10)
CROSS_SPAWN_BOTTOM_INSET: int = 16
GOAL_INSET_X: int = 8
GOAL_INSET_Y: int = 12
MEASURE_MAX_FRAMES: int = 3000
SEED_RANGE: int = 1 << 30
UNREACHABLE: int = 1 << 30

# Cell types (PathfindCell::CellType, the ones a flat map has) and occupancy flags (AIPathfind.h:368)
CLEAR, OBSTACLE, IMPASSABLE = 0, 1, 2
NO_UNITS, UNIT_GOAL, UNIT_PRESENT_MOVING, UNIT_PRESENT_FIXED, UNIT_GOAL_OTHER_MOVING = range(5)

# examineNeighboringCells' neighbour order; a diagonal needs one of adjacent[i-4], adjacent[i-3]
DELTAS: tuple[Cell, ...] = ((1, 0), (0, 1), (-1, 0), (0, -1), (1, 1), (-1, 1), (-1, -1), (1, -1))
ADJACENT: tuple[int, ...] = (0, 1, 2, 3, 0)


def cell_of(x: float, y: float) -> Cell:
    """worldToCell: floor."""
    return int(math.floor(x / CELL)), int(math.floor(y / CELL))


def cell_pos(cx: int, cy: int, center: bool) -> Point:
    """adjustCoordToCell: the cell's centre for a centred body, a point just inside its low corner
       for the rest.  0.5 rather than the game's 0.05 of a cell keeps the nodes on exact integers, so
       Path::optimize's integer step test reads them the way the C++ does."""
    if center:
        return (cx + 0.5) * CELL, (cy + 0.5) * CELL
    return cx * CELL + 0.5, cy * CELL + 0.5


def radius_and_center(radius: float) -> tuple[int, bool]:
    """Pathfinder::getRadiusAndCenter: the footprint in cells from the bounding circle."""
    diameter = 2.0 * radius
    if CELL < diameter < 2.0 * CELL:
        diameter = 2.0 * CELL
    iradius = int(math.floor(diameter / CELL + 0.3))
    center = False
    if iradius == 0:
        iradius += 1
    if iradius & 1:
        center = True
    iradius //= 2
    if iradius > MAX_RADIUS:
        iradius, center = MAX_RADIUS, True
    return iradius, center


def unit_cell(x: float, y: float, center: bool) -> Cell:
    """updatePos/updateGoal: a centred body sits in the cell it is over, the rest on the nearest
       cell corner."""
    if center:
        return int(math.floor(x / CELL)), int(math.floor(y / CELL))
    return int(math.floor(0.5 + x / CELL)), int(math.floor(0.5 + y / CELL))


def line_cells(sx: int, sy: int, ex: int, ey: int) -> Iterator[tuple[Cell | None, Cell]]:
    """Pathfinder::iterateCellsAlongLine: EA's Bresenham, which visits the side cell at every step
       of the minor axis, so the walk is 4-connected.  Yields (from, to); `from` is None for the first
       cell.  A zero-length line also visits the cell one step up the y axis, as the C++ does."""
    delta_x, delta_y = abs(ex - sx), abs(ey - sy)
    x, y = sx, sy
    xinc1 = xinc2 = 1 if ex >= sx else -1
    yinc1 = yinc2 = 1 if ey >= sy else -1
    if delta_x >= delta_y:
        xinc1, yinc2 = 0, 0
        den, num, numadd, pixels = delta_x, delta_x // 2, delta_y, delta_x
    else:
        xinc2, yinc1 = 0, 0
        den, num, numadd, pixels = delta_y, delta_y // 2, delta_x, delta_y
    previous: Cell | None = None
    for _ in range(pixels + 1):
        current = (x, y)
        yield previous, current
        num += numadd
        if num >= den:
            num -= den
            x += xinc1
            y += yinc1
            side = (x, y)
            yield current, side
            current = side
        x += xinc2
        y += yinc2
        previous = current


def normalize_angle(angle: float) -> float:
    while angle > math.pi:
        angle -= 2.0 * math.pi
    while angle <= -math.pi:
        angle += 2.0 * math.pi
    return angle


# ---------------------------------------------------------------------------------------------
# Cost rules, each a pure function so a test can hold it against the C++
# ---------------------------------------------------------------------------------------------

def cost_to_goal(dx: int, dy: int, num: int, den: int) -> int:
    """PathfindCell::costToGoal: 10 per cell of the long axis, 5 per cell of the short, scaled."""
    dx, dy = abs(dx), abs(dy)
    if dx > dy:
        cost = COST_ORTHOGONAL * dx + (COST_ORTHOGONAL * dy) // 2
    else:
        cost = COST_ORTHOGONAL * dy + (COST_ORTHOGONAL * dx) // 2
    return (cost * num) // den


def turn_cost(radians: float, chassis: int, cap: int) -> int:
    """Pathfinder_turnCost: one radian costs the hull's speed over its turn rate, capped."""
    if chassis <= 0 or radians <= 0.0:
        return 0
    return min(int(math.floor(radians * chassis)), cap)


def wall_hug_cost(base: int, clearance: int, need: int) -> int:
    """Pathfinder_wallHugCost: the body's slack inside 13, squared, up to 8/5 of a step."""
    slack = clearance - need
    if slack >= PF_CLEARANCE_SOFT:
        return 0
    slack = max(slack, 0)
    nearness = ((PF_CLEARANCE_SOFT - slack) * 256) // PF_CLEARANCE_SOFT
    squared = (nearness * nearness) >> 8
    return (base * PF_WALLHUG_NUM * squared) // (PF_WALLHUG_DEN * 256)


def traffic_cost(base: int, traffic: int) -> int:
    """Pathfinder_trafficCost: up to four steps, full at 192."""
    if traffic <= 0:
        return 0
    traffic = min(traffic, PF_TRAFFIC_FULL)
    return (base * PF_TRAFFIC_NUM * traffic) // (PF_TRAFFIC_DEN * PF_TRAFFIC_FULL)


def crossing_cost(base: int, claim_halves: int) -> int:
    """Pathfinder_crossingCost: half a step per whole unit wanting the cell at that moment, 4 at most."""
    units = claim_halves // 2
    if units <= 0:
        return 0
    units = min(units, PF_CLAIM_UNITS_MAX)
    return (base * PF_CROSSING_NUM * units) // PF_CROSSING_DEN


def lane_count(span: float, spacing: float, members: int) -> int:
    """Crowd_laneCount."""
    if spacing < 0.001:
        return 1
    lanes = min(int(span / spacing) + 1, CROWD_MAX_LANES)
    if 0 < members < lanes:
        lanes = members
    return max(lanes, 1)


def lane_corner(a: Point, c: Point, b: Point, offset: float) -> Point | None:
    """Crowd_laneCorner: where a lane `offset` left of a route turns at corner c."""
    ix, iy = c[0] - a[0], c[1] - a[1]
    ox, oy = b[0] - c[0], b[1] - c[1]
    il, ol = math.hypot(ix, iy), math.hypot(ox, oy)
    if il < 0.01 or ol < 0.01:
        return None
    ix, iy, ox, oy = ix / il, iy / il, ox / ol, oy / ol
    mx, my = -iy - oy, ix + ox
    stretch = 1.0
    ml = math.hypot(mx, my)
    if ml < 0.01:
        mx, my = -iy, ix
    else:
        mx, my = mx / ml, my / ml
        cos_half = mx * -iy + my * ix
        stretch = 1.0 / max(cos_half, CROWD_LANE_MIN_COS)
    return mx * offset * stretch, my * offset * stretch


def brake_speed(speed: float, blocker_speed: float, gap: float, frames: int) -> float:
    """Crowd_brakeSpeed."""
    if frames <= 0:
        return speed
    want = max(blocker_speed + gap / frames, 0.0)
    return min(want, speed)


def release_cap(held: float, want: float, filt: float) -> float:
    """Crowd_releaseCap: a brake at once, a release as a ramp."""
    return want if want <= held else held + (want - held) * filt


def stuck_rung(no_progress: int, stage_done: int) -> int:
    """AIUpdate_stuckRung: 0 nothing, 1..3 the rung to fire, -1 start the ladder again."""
    if stage_done >= 3:
        return -1 if no_progress > STUCK_CYCLE_FRAMES else 0
    if no_progress < STUCK_REPATH_FRAMES:
        return 0
    return stage_done + 1


# ---------------------------------------------------------------------------------------------
# The map
# ---------------------------------------------------------------------------------------------

class Grid:
    """The classified map: cell types, the pinched flag, the clearance field and the zones."""

    def __init__(self, width: int, height: int) -> None:
        self.width = width
        self.height = height
        self.kind = bytearray(width * height)
        self._pinched: bytearray | None = None
        self._clearance: bytearray | None = None
        self._zones: list[int] | None = None
        self._block_zones: dict[int, list[int]] = {}

    def in_bounds(self, cx: int, cy: int) -> bool:
        return 0 <= cx < self.width and 0 <= cy < self.height

    def set_kind(self, cx: int, cy: int, kind: int) -> None:
        self.kind[cy * self.width + cx] = kind
        self._pinched = self._clearance = self._zones = None
        self._block_zones = {}

    def block(self, cx: int, cy: int) -> None:
        self.set_kind(cx, cy, IMPASSABLE)

    def unblock(self, cx: int, cy: int) -> None:
        self.set_kind(cx, cy, CLEAR)

    def open_span(self, cx: int, first_row: int, last_row: int) -> None:
        for y in range(first_row, last_row + 1):
            self.unblock(cx, y)
            self.unblock(cx + 1, y)

    def passable(self, cx: int, cy: int) -> bool:
        """validMovementPosition for a ground locomotor: only CLEAR (and rubble, which this map has
           none of) carries it."""
        return 0 <= cx < self.width and 0 <= cy < self.height and self.kind[cy * self.width + cx] == CLEAR

    def passable_world(self, x: float, y: float) -> bool:
        return self.passable(*cell_of(x, y))

    @property
    def pinched(self) -> bytearray:
        """Pathfinder::classifyMap: a clear cell with fewer than 2 clear orthogonal neighbours or 4
           clear neighbours in all, and (MARK_BORDER_PINCHED) any clear cell orthogonally beside an
           obstacle.  Off-map neighbours are skipped, not counted."""
        if self._pinched is None:
            w, h = self.width, self.height
            pinched = bytearray(w * h)
            for j in range(h):
                for i in range(w):
                    if self.kind[j * w + i] != CLEAR:
                        continue
                    total = ortho = 0
                    beside_obstacle = False
                    for k in range(i - 1, i + 2):
                        if not 0 <= k < w:
                            continue
                        for m in range(j - 1, j + 2):
                            if not 0 <= m < h or (k == i and m == j):
                                continue
                            kind = self.kind[m * w + k]
                            if kind == CLEAR:
                                total += 1
                                if k == i or m == j:
                                    ortho += 1
                            if kind == OBSTACLE and (k == i or m == j):
                                beside_obstacle = True
                    if ortho < 2 or total < 4 or beside_obstacle:
                        pinched[j * w + i] = 1
            self._pinched = pinched
        return self._pinched

    @property
    def clearance(self) -> bytearray:
        """Pathfinder::buildClearance: a 5/7 chamfer distance to the nearest cell no ground unit can
           stand in, the map edge counted as a wall, capped at 60."""
        if self._clearance is None:
            w, h = self.width, self.height
            c = bytearray(PF_CLEARANCE_MAX if k == CLEAR else 0 for k in self.kind)
            for x in range(w):
                c[x] = c[(h - 1) * w + x] = 0
            for y in range(h):
                c[y * w] = c[y * w + w - 1] = 0

            def relax(dst: int, src: int, add: int) -> None:
                if c[src] + add < c[dst]:
                    c[dst] = c[src] + add

            # the C++ walks x-major columns; the passes are the same pairs of neighbours
            for x in range(1, w - 1):
                for y in range(1, h - 1):
                    i = y * w + x
                    if c[i] == 0:
                        continue
                    relax(i, i - w, PF_CLEARANCE_ORTHO)
                    relax(i, i - 1, PF_CLEARANCE_ORTHO)
                    relax(i, i - w - 1, PF_CLEARANCE_DIAG)
                    relax(i, i + w - 1, PF_CLEARANCE_DIAG)
            for x in range(w - 2, 0, -1):
                for y in range(h - 2, 0, -1):
                    i = y * w + x
                    if c[i] == 0:
                        continue
                    relax(i, i + w, PF_CLEARANCE_ORTHO)
                    relax(i, i + 1, PF_CLEARANCE_ORTHO)
                    relax(i, i + w + 1, PF_CLEARANCE_DIAG)
                    relax(i, i - w + 1, PF_CLEARANCE_DIAG)
            self._clearance = c
        return self._clearance

    @property
    def zones(self) -> list[int]:
        """Connected ground, 4-connected.  Stands in for the zone manager's effective zones, which is
           all clientSafeQuickDoesPathExist and internalFindPath's zone screen read."""
        if self._zones is None:
            w = self.width
            zones = [0] * (w * self.height)
            label = 0
            for start in range(w * self.height):
                if zones[start] or self.kind[start] != CLEAR:
                    continue
                label += 1
                zones[start] = label
                todo = [start]
                while todo:
                    i = todo.pop()
                    x, y = i % w, i // w
                    for nx, ny in ((x + 1, y), (x - 1, y), (x, y + 1), (x, y - 1)):
                        if self.passable(nx, ny) and not zones[ny * w + nx]:
                            zones[ny * w + nx] = label
                            todo.append(ny * w + nx)
            self._zones = zones
        return self._zones

    def zone_at(self, x: float, y: float) -> int:
        cx, cy = cell_of(x, y)
        return self.zones[cy * self.width + cx] if self.in_bounds(cx, cy) else 0

    def block_zones(self, size: int) -> list[int]:
        """PathfindZoneManager's raw zones: each clear cell labelled with its 4-connected component
           inside its own size x size block."""
        if size in self._block_zones:
            return self._block_zones[size]
        zones = [0] * (self.width * self.height)
        next_zone = 1
        for cy in range(self.height):
            for cx in range(self.width):
                if zones[cy * self.width + cx] or not self.passable(cx, cy):
                    continue
                bx, by = cx // size, cy // size
                zones[cy * self.width + cx] = next_zone
                todo = [(cx, cy)]
                while todo:
                    x, y = todo.pop()
                    for nx, ny in ((x + 1, y), (x - 1, y), (x, y + 1), (x, y - 1)):
                        if (nx // size == bx and ny // size == by and self.passable(nx, ny)
                                and not zones[ny * self.width + nx]):
                            zones[ny * self.width + nx] = next_zone
                            todo.append((nx, ny))
                next_zone += 1
        self._block_zones[size] = zones
        return zones


def scatter_blocks(grid: Grid, rng: random.Random, count: int, margin: int,
                   keep_clear_of: int | None = None) -> None:
    """`count` five-by-five rocks.  They are OBSTACLE cells, the class a structure has, so the ring of
       ground round each is pinched the way the ground round a building is."""
    for _ in range(count):
        cx = rng.randrange(margin, grid.width - margin)
        cy = rng.randrange(margin, grid.height - margin)
        if keep_clear_of is not None and abs(cx - keep_clear_of) < SCATTER_KEEP_CLEAR_CELLS:
            continue
        for x in range(cx - ROCK_RADIUS_CELLS, cx + ROCK_RADIUS_CELLS + 1):
            for y in range(cy - ROCK_RADIUS_CELLS, cy + ROCK_RADIUS_CELLS + 1):
                grid.set_kind(x, y, OBSTACLE)


def carve_maze(grid: Grid, rng: random.Random) -> None:
    for cy in range(1, grid.height - 1):
        for cx in range(1, grid.width - 1):
            grid.block(cx, cy)
    columns = (grid.width - 2) // MAZE_PITCH
    rows = (grid.height - 2) // MAZE_PITCH

    def carve(x0: int, y0: int, x1: int, y1: int) -> None:
        for cy in range(y0, y1):
            for cx in range(x0, x1):
                grid.unblock(cx, cy)

    def origin(node: Cell) -> Cell:
        return 2 + node[0] * MAZE_PITCH, 2 + node[1] * MAZE_PITCH

    def join(first: Cell, second: Cell) -> None:
        (ax, ay), (bx, by) = origin(first), origin(second)
        carve(min(ax, bx), min(ay, by), max(ax, bx) + MAZE_PASSAGE, max(ay, by) + MAZE_PASSAGE)

    for node in itertools.product(range(columns), range(rows)):
        x0, y0 = origin(node)
        carve(x0, y0, x0 + MAZE_PASSAGE, y0 + MAZE_PASSAGE)
    seen = {(0, 0)}
    stack = [(0, 0)]
    walls: list[tuple[Cell, Cell]] = []
    while stack:
        node = stack[-1]
        around = [(node[0] + dx, node[1] + dy) for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1))
                  if 0 <= node[0] + dx < columns and 0 <= node[1] + dy < rows]
        fresh = [other for other in around if other not in seen]
        if not fresh:
            stack.pop()
            continue
        chosen = rng.choice(fresh)
        walls.extend((node, other) for other in fresh if other != chosen)
        join(node, chosen)
        seen.add(chosen)
        stack.append(chosen)
    for first, second in walls:
        if rng.random() < MAZE_LOOPS:
            join(first, second)
    room_w, room_h = MAZE_ROOM
    for x0 in (1, grid.width - 1 - room_w):
        for y0 in (1, grid.height - 1 - room_h):
            carve(x0, y0, x0 + room_w, y0 + room_h)


def make_map(kind: str, width: int = MAP_WIDTH_CELLS, height: int = MAP_HEIGHT_CELLS,
             seed: int | None = None) -> Grid:
    rng = random.Random(seed)
    if kind == "maze":
        width, height = MAZE_SIZE
    elif kind == "edges":
        width, height = EDGES_SIZE
    grid = Grid(width, height)
    for x in range(width):
        grid.block(x, 0)
        grid.block(x, height - 1)
    for y in range(height):
        grid.block(0, y)
        grid.block(width - 1, y)
    if kind == "open":
        scatter_blocks(grid, rng, OPEN_MAP_ROCKS, OPEN_MAP_MARGIN)
    elif kind == "choke":
        wall, gap_y = width // 2, height // 2
        for y in range(1, height - 1):
            if abs(y - gap_y) * 2 > CHOKE_GAP_CELLS:
                grid.block(wall, y)
                grid.block(wall + 1, y)
        scatter_blocks(grid, rng, CHOKE_MAP_ROCKS, CHOKE_MAP_MARGIN, keep_clear_of=wall)
    elif kind == "twogaps":
        # hand-made: the seed changes nothing, so a run over N seeds is one match N times
        wall = width // 2
        for y in range(1, height - 1):
            grid.block(wall, y)
            grid.block(wall + 1, y)
        grid.open_span(wall, height // 2 - NARROW_GAP_HALF_CELLS, height // 2 + NARROW_GAP_HALF_CELLS)
        grid.open_span(wall, *WIDE_GAP_ROWS)
    elif kind == "maze":
        carve_maze(grid, rng)
    elif kind == "edges":
        size = ZONE_BLOCK_SIZE
        boundaries = range(1, (height - 1) // size)
        for boundary in range(EDGES_FIRST_WALL, (width - 1) // size - 1, EDGES_WALL_EVERY):
            wall = boundary * size - 1
            for y in range(1, height - 1):
                grid.block(wall, y)
                grid.block(wall + 1, y)
            narrow, wide = rng.sample(boundaries, 2)
            for row, gap in ((narrow, EDGES_NARROW), (wide, EDGES_WIDE)):
                first = row * size - gap // 2
                grid.open_span(wall, max(1, first), min(height - 2, first + gap - 1))
    else:
        raise ValueError(f"unknown map kind {kind!r}, expected one of {', '.join(MAP_KINDS)}")
    return grid


# ---------------------------------------------------------------------------------------------
# The hierarchical corridor (internal_findHierarchicalPath, buildHierachicalPath)
# ---------------------------------------------------------------------------------------------

def find_block_route(grid: Grid, start: Cell, goal: Cell, size: int = ZONE_BLOCK_SIZE
                     ) -> tuple[list[Cell], int] | None:
    """Pathfinder::internal_findHierarchicalPath on the ground layer: the start, the cell each block
       is entered by, and the goal, plus how many entry cells it queued; None when there is no route,
       which is the engine's setAllPassable.  Each side of a block is scanned middle out, one exit per
       neighbouring zone per side unless the crossing is pinched, and a step costs the straight
       distance between entry cells."""
    zones = grid.block_zones(size)
    pinched = grid.pinched
    width = grid.width
    start_zone = zones[start[1] * width + start[0]] if grid.in_bounds(*start) else 0
    goal_zone = zones[goal[1] * width + goal[0]] if grid.in_bounds(*goal) else 0
    if not start_zone or not goal_zone:
        return None
    goal_block = (goal[0] // size, goal[1] // size)
    blocks_x = (grid.width + size - 1) // size
    blocks_y = (grid.height + size - 1) // size
    cost: dict[Cell, int] = {start: 0}
    parent: dict[Cell, Cell | None] = {start: None}
    visited = {start}
    tie = itertools.count()
    open_heap: list[tuple[int, int, Cell]] = [(0, next(tie), start)]
    pushes = 0
    while open_heap:
        _, _, cell = heapq.heappop(open_heap)
        zone = zones[cell[1] * width + cell[0]]
        bx, by = cell[0] // size, cell[1] // size
        if zone == goal_zone and (bx, by) == goal_block:
            nodes: list[Cell] = []
            step: Cell | None = cell
            while step is not None:
                nodes.append(step)
                step = parent[step]
            nodes.reverse()
            if nodes[-1] != goal:
                nodes.append(goal)
            return nodes, pushes
        middle = size // 2
        sides: list[tuple[Cell, Cell, Cell]] = []
        if bx > 0:
            sides.append(((bx * size, by * size + middle), (0, 1), (-1, 0)))
        if bx < blocks_x - 1:
            sides.append(((bx * size + size - 1, by * size + middle), (0, 1), (1, 0)))
        if by > 0:
            sides.append(((bx * size + middle, by * size), (1, 0), (0, -1)))
        if by < blocks_y - 1:
            sides.append(((bx * size + middle, by * size + size - 1), (1, 0), (0, 1)))
        for (ax, ay), (sx, sy), (dx, dy) in sides:
            examined: list[int] = []
            for i in range(1, size + 1):
                offset = -(i >> 1) if i & 1 else i >> 1
                scan = (ax + sx * offset, ay + sy * offset)
                if not grid.in_bounds(*scan):
                    continue
                scan_zone = zones[scan[1] * width + scan[0]]
                if scan in visited:
                    if scan_zone == zone:
                        break
                    continue
                if scan_zone != zone:
                    continue
                across = (scan[0] + dx, scan[1] + dy)
                if not grid.passable(*across) or across in visited:
                    continue
                across_zone = zones[across[1] * width + across[0]]
                if across_zone in examined:
                    continue
                visited.add(scan)
                visited.add(across)
                reached = cost[cell] + int(COST_ORTHOGONAL * math.hypot(across[0] - cell[0],
                                                                        across[1] - cell[1]) + 0.5)
                if pinched[across[1] * width + across[0]] or pinched[scan[1] * width + scan[0]]:
                    reached += 2 * COST_ORTHOGONAL
                else:
                    examined.append(across_zone)
                remaining = int(COST_ORTHOGONAL * math.hypot(across[0] - goal[0],
                                                             across[1] - goal[1]) + 0.5)
                cost[across] = reached
                parent[across] = cell
                heapq.heappush(open_heap, (reached + remaining, next(tie), across))
                pushes += 1
    return None


class BlockCorridor:
    """The blocks the zone manager marks passable: every block the coarse route enters by, and its
       eight neighbours (setPassableWithNeighbors)."""

    def __init__(self, nodes: Sequence[Cell], size: int = ZONE_BLOCK_SIZE) -> None:
        self.size = size
        self.open: set[Cell] = set()
        for x, y in nodes:
            bx, by = x // size, y // size
            for ox in (-1, 0, 1):
                for oy in (-1, 0, 1):
                    self.open.add((bx + ox, by + oy))

    def is_open(self, cx: int, cy: int) -> bool:
        return (cx // self.size, cy // self.size) in self.open

    def admits_ground(self, cx: int, cy: int) -> bool:
        """findGroundPath: inside, or a block three cells away diagonally is (clipIsPassable)."""
        if self.is_open(cx, cy):
            return True
        return any(self.is_open(cx + sx, cy + sy) for sx, sy in ((3, 3), (-3, 3), (3, -3), (-3, -3)))


def block_corridor(grid: Grid, start: Cell, goal: Cell) -> BlockCorridor | None:
    route = find_block_route(grid, start, goal)
    return BlockCorridor(route[0]) if route else None


# ---------------------------------------------------------------------------------------------
# Occupancy (PathfindCell's pos and goal unit, setPosUnit / setGoalUnit)
# ---------------------------------------------------------------------------------------------

class Occupancy:
    def __init__(self) -> None:
        self.pos: dict[int, int] = {}
        self.goal: dict[int, int] = {}

    def flags(self, index: int) -> int:
        p, g = self.pos.get(index), self.goal.get(index)
        if p is None and g is None:
            return NO_UNITS
        if p is None:
            return UNIT_GOAL
        if g is None:
            return UNIT_PRESENT_MOVING
        return UNIT_PRESENT_FIXED if p == g else UNIT_GOAL_OTHER_MOVING

    def set_pos(self, index: int, uid: int) -> None:
        g = self.goal.get(index)
        if g is not None and g == self.pos.get(index):
            return                  # a unit is already occupying this cell
        self.pos[index] = uid

    def clear_pos(self, index: int, uid: int) -> None:
        if self.pos.get(index) == uid:
            del self.pos[index]

    def clear_goal(self, index: int, uid: int) -> None:
        if self.goal.get(index) == uid:
            del self.goal[index]


# ---------------------------------------------------------------------------------------------
# Paths and the crowd corridor
# ---------------------------------------------------------------------------------------------

@dataclass
class Path:
    """Every node the search laid, and the optimized chain through them (getNextOptimized)."""
    nodes: list[Point]
    route: list[int]
    blocked_by_ally: bool = False

    @property
    def points(self) -> list[Point]:
        return [self.nodes[i] for i in self.route]

    @property
    def last(self) -> Point:
        return self.nodes[-1]

    def length(self) -> float:
        pts = self.points
        return sum(math.hypot(b[0] - a[0], b[1] - a[1]) for a, b in zip(pts, pts[1:]))


@dataclass
class Sample:
    x: float
    y: float
    tx: float
    ty: float
    left: float
    right: float
    along: float


class CrowdCorridor:
    """CrowdCorridor: the route sampled a cell apart, with the drivable ground either side."""

    def __init__(self) -> None:
        self.samples: list[Sample] = []

    def build(self, grid: Grid, radius: float, points: Sequence[Point]) -> bool:
        """CrowdCorridor::build: crowdExtent probes half a cell at a time to 16 cells, less the body's
           own radius, then two passes of a 1-2-1 smoothing."""
        self.samples = []
        if len(points) < 2:
            return False
        total = sum(math.hypot(b[0] - a[0], b[1] - a[1]) for a, b in zip(points, points[1:]))
        if total < CELL:
            return False
        step = CELL
        if total / step > CROWD_MAX_SAMPLES - 1:
            step = total / (CROWD_MAX_SAMPLES - 1)
        walked = base = 0.0
        for a, b in zip(points, points[1:]):
            seg = math.hypot(b[0] - a[0], b[1] - a[1])
            if seg < 0.001:
                continue
            dx, dy = (b[0] - a[0]) / seg, (b[1] - a[1]) / seg
            while walked <= base + seg and len(self.samples) < CROWD_MAX_SAMPLES:
                t = walked - base
                x, y = a[0] + dx * t, a[1] + dy * t
                self.samples.append(Sample(x, y, dx, dy, crowd_extent(grid, radius, x, y, -dy, dx),
                                           crowd_extent(grid, radius, x, y, dy, -dx), walked))
                walked += step
            base += seg
        if len(self.samples) < 2:
            self.samples = []
            return False
        n = len(self.samples)
        for _ in range(2):
            lefts = [(self.samples[max(i - 1, 0)].left + 2 * self.samples[i].left
                      + self.samples[min(i + 1, n - 1)].left) * 0.25 for i in range(n)]
            rights = [(self.samples[max(i - 1, 0)].right + 2 * self.samples[i].right
                       + self.samples[min(i + 1, n - 1)].right) * 0.25 for i in range(n)]
            for s, lv, rv in zip(self.samples, lefts, rights):
                s.left, s.right = lv, rv
        return True

    def length(self) -> float:
        return self.samples[-1].along if self.samples else 0.0

    def nearest(self, x: float, y: float, hint: int) -> int:
        n = len(self.samples)
        lo, hi = max(hint - 4, 0), min(hint + 12, n - 1)
        best, best_sqr = lo, 1e30
        for i in range(lo, hi + 1):
            s = self.samples[i]
            d = (x - s.x) ** 2 + (y - s.y) ** 2
            if d < best_sqr:
                best, best_sqr = i, d
        if best_sqr > (CELL * 6) ** 2 and (lo > 0 or hi < n - 1):
            for i, s in enumerate(self.samples):
                d = (x - s.x) ** 2 + (y - s.y) ** 2
                if d < best_sqr:
                    best, best_sqr = i, d
        return best

    def curvature(self, i: int) -> float:
        n = len(self.samples)
        i = min(max(i, 0), n - 1)
        j = min(i + 4, n - 1)
        a, b = self.samples[i], self.samples[j]
        return a.tx * b.ty - a.ty * b.tx

    def clamp_lat(self, i: int, lat: float) -> float:
        s = self.samples[i]
        return max(min(lat, s.left), -s.right)

    def lat_of(self, i: int, x: float, y: float) -> float:
        s = self.samples[i]
        return (x - s.x) * -s.ty + (y - s.y) * s.tx

    def along_of(self, i: int, x: float, y: float) -> float:
        s = self.samples[i]
        return min(max(s.along + (x - s.x) * s.tx + (y - s.y) * s.ty, 0.0), self.length())

    def bracket(self, along: float) -> tuple[int, float]:
        n = len(self.samples)
        lo, hi = 0, n - 1
        while lo + 1 < hi:
            mid = (lo + hi) // 2
            if self.samples[mid].along <= along:
                lo = mid
            else:
                hi = mid
        span = self.samples[lo + 1].along - self.samples[lo].along
        f = (along - self.samples[lo].along) / span if span > 0.001 else 0.0
        return lo, min(max(f, 0.0), 1.0)

    def point_at(self, along: float, lat: float) -> Point:
        i, f = self.bracket(along)
        a, b = self.samples[i], self.samples[min(i + 1, len(self.samples) - 1)]
        tx, ty = a.tx + (b.tx - a.tx) * f, a.ty + (b.ty - a.ty) * f
        tl = math.hypot(tx, ty)
        if tl > 0.001:
            tx, ty = tx / tl, ty / tl
        else:
            tx, ty = a.tx, a.ty
        return a.x + (b.x - a.x) * f - ty * lat, a.y + (b.y - a.y) * f + tx * lat

    def clamp_lat_at(self, along: float, lat: float) -> float:
        i, f = self.bracket(along)
        a, b = self.samples[i], self.samples[min(i + 1, len(self.samples) - 1)]
        left = a.left + (b.left - a.left) * f
        right = a.right + (b.right - a.right) * f
        return max(min(lat, left), -right)

    def clamp_lat_narrowest(self, frm: float, to: float, lat: float) -> float:
        if to < frm:
            frm, to = to, frm
        lat = self.clamp_lat_at(to, self.clamp_lat_at(frm, lat))
        for s in self.samples:
            if s.along <= frm:
                continue
            if s.along >= to:
                break
            lat = max(min(lat, s.left), -s.right)
        return lat


def crowd_extent(grid: Grid, radius: float, x: float, y: float, dx: float, dy: float) -> float:
    """crowdExtent / Pathfinder::laneExtent: drivable ground one way, half a cell at a time."""
    step = CELL * 0.5
    room = 0.0
    s = step
    while s <= CELL * CROWD_PROBE_CELLS:
        if not grid.passable_world(x + dx * s, y + dy * s):
            break
        room = s
        s += step
    return max(room - radius, 0.0)


# ---------------------------------------------------------------------------------------------
# Units
# ---------------------------------------------------------------------------------------------

@dataclass
class Unit:
    x: float
    y: float
    id: int
    kind: UnitKind = STANDIN_TANK
    damaged: bool = False                   # body at MovementPenaltyDamageState (REALLYDAMAGED) or worse
    facing: float = 0.0
    vx: float = 0.0
    vy: float = 0.0
    is_selected: bool = False
    radius: float = field(init=False)
    max_speed: float = field(init=False)
    turn_rate: float = field(init=False)
    # the locomotor's flags and the physics module's state
    ax: float = 0.0                         # PhysicsBehavior::m_accel, forces over the mass
    ay: float = 0.0
    motive_until: int = 0                   # m_motiveForceExpires
    is_braking: bool = False                # Locomotor IS_BRAKING
    braking_factor: float = 1.0
    braking_status: bool = False            # OBJECT_STATUS_BRAKING: physics leaves x and y alone
    moving_backwards: bool = False
    three_point_turn: bool = False
    donut_until: int = 0
    yaw_rate: float = 0.0
    side_step_x: float = 0.0                # processCollision's m_sideStep, a soldier against a hull
    side_step_y: float = 0.0
    side_step_seen: bool = False
    # orders and paths
    moving: bool = False                    # in a move state (locomotor goal on path)
    goal: Point | None = None               # the state machine's goal position
    path: Path | None = None
    waiting_for_path: bool = False
    queued: bool = False
    queue_at: int = 0
    path_timestamp: int = -1000
    planned: list[Point] = field(default_factory=list)      # m_crowdPlanned
    move_away_from: int | None = None       # AI_MOVE_OUT_OF_THE_WAY, from whom
    resume_goal: Point | None = None        # the order a move out of the way interrupted
    dist_to_goal: float = 0.0               # computePointOnPath's distAlongPath, cached
    loco_goal: Point = (0.0, 0.0)           # m_locomotorGoalData
    # blocking (processCollision, doLocomotor)
    is_blocked: bool = False
    blocked_frames: int = 0
    is_blocked_and_stuck: bool = False
    cur_max_blocked_speed: float = FAST_AS_POSSIBLE
    bump_speed_limit: float = FAST_AS_POSSIBLE
    head_on_seen: bool = False
    head_on_frames: int = 0
    repath_asked: bool = False
    ignore_collisions_until: int = 0
    # the stuck ladder (updateProgress, stuckRescue)
    last_progress: Point = (0.0, 0.0)
    last_progress_angle: float = 0.0
    no_progress: int = 0
    rescue_stage: int = 0
    rescue_until: int = 0
    rescue_cool: int = 0
    rescue_to: Point = (0.0, 0.0)
    repath_pending: bool = False            # LADDER_REPATH_FLAG
    progress_frames: int = 0                # LADDER_STAGE_HOLD
    # the crowd model (crowdSteer)
    pending_crowd_lat: float = 0.0
    has_pending_crowd_lat: bool = False
    crowd_lat_valid: bool = False
    crowd_lat: float = 0.0
    crowd_side: int = 1
    crowd_sample: int = 0
    corridor: CrowdCorridor | None = None
    crowd_lane_idx: int = 0
    crowd_lane_of: int = 0
    crowd_lane_space: float = 0.0
    crowd_fit: int = 0
    crowd_queued: int = 0
    crowd_hold_frame: int = 0
    crowd_sep_smooth: float = 0.0
    crowd_cap: float = 0.0
    crowd_cap_valid: bool = False
    crowd_aim: float = 0.0
    crowd_aim_valid: bool = False
    crowd_scan: dict | None = None
    # measurement
    blocked_total: int = 0
    gave_up: bool = False
    repaths: int = 0
    escapes: int = 0
    rungs_fired: list[int] = field(default_factory=lambda: [0, 0, 0])
    arrived: bool = False                   # reached its own order's goal at least once
    churn: int = 0                          # ticks moving again after that
    first_length: float = 0.0
    first_waypoints: int = 0
    rays: list[tuple[float, float, float, float, bool]] = field(default_factory=list)

    def __post_init__(self) -> None:
        self.radius = self.kind.radius
        self.max_speed = self.kind.max_speed(self.damaged)
        self.turn_rate = self.kind.max_turn_rate(self.damaged)
        self.cell_radius, self.center = radius_and_center(self.radius)
        self.last_progress = (self.x, self.y)
        self.pos_cell: Cell | None = None
        self.goal_cell: Cell | None = None

    @property
    def pos(self) -> Point:
        return self.x, self.y

    @property
    def dir(self) -> Point:
        return math.cos(self.facing), math.sin(self.facing)

    @property
    def chassis(self) -> int:
        """beginFlowSearch: world units driven in the time the hull turns one radian."""
        if self.max_speed > 0.01 and self.turn_rate > 0.0001:
            return int(math.floor(self.max_speed / self.turn_rate))
        return 0

    @property
    def is_moving(self) -> bool:
        return self.moving

    @property
    def is_infantry(self) -> bool:
        return self.kind.infantry

    @property
    def acceleration(self) -> float:
        return self.kind.max_acceleration(self.damaged)

    @property
    def on_path(self) -> bool:
        """m_locomotorGoalType == POSITION_ON_PATH."""
        return self.moving and self.path is not None


# ---------------------------------------------------------------------------------------------
# The pathfinder
# ---------------------------------------------------------------------------------------------

@dataclass
class MoveCheck:
    ally_fixed: int = 0
    ally_moving: bool = False
    ally_goal: bool = False
    enemy_fixed: bool = False


class SearchCell:
    __slots__ = ("cost", "total", "travel", "parent", "state", "seq", "blocked_by_ally")

    def __init__(self) -> None:
        self.cost = 0
        self.total = 0
        self.travel = 0
        self.parent: int | None = None
        self.state = 0                      # 0 none, 1 open, 2 closed
        self.seq = 0
        self.blocked_by_ally = False


class Pathfinder:
    def __init__(self, grid: Grid) -> None:
        self.grid = grid
        self.occ = Occupancy()
        self.infantry: set[int] = set()
        self.frame = 0
        self.traffic = bytearray(grid.width * grid.height)
        self.traffic_cells: list[int] = []
        self.traffic_decay_frame = 0
        self.claims: dict[tuple[int, int], int] = {}
        self.claim_touch = [0] * (grid.width * grid.height)
        self.queue: list[Unit] = []
        self.cells_total = 0
        self.searches = 0
        # beginFlowSearch's state for the search now running
        self.flow_on = False
        self.flow_now = 0
        self.flow_now_bucket = 0
        self.flow_speed = 0
        self.h_num, self.h_den = PF_HEURISTIC_RETAIL_NUM, PF_HEURISTIC_RETAIL_DEN
        self.chassis = 0
        self.start_dir: Point | None = None

    # ---- occupancy --------------------------------------------------------------------------
    def footprint(self, cx: int, cy: int, r: int, center: bool) -> Iterator[Cell]:
        above = r + 1 if center else r
        for i in range(cx - r, cx + above):
            for j in range(cy - r, cy + above):
                yield i, j

    def update_pos(self, unit: Unit) -> None:
        """Pathfinder::updatePos."""
        new = unit_cell(unit.x, unit.y, unit.center)
        if new == unit.pos_cell:
            return
        if unit.is_infantry:
            self.infantry.add(unit.id)
        w = self.grid.width
        if unit.pos_cell is not None:
            for i, j in self.footprint(*unit.pos_cell, unit.cell_radius, unit.center):
                if self.grid.in_bounds(i, j):
                    self.occ.clear_pos(j * w + i, unit.id)
        unit.pos_cell = new
        for i, j in self.footprint(*new, unit.cell_radius, unit.center):
            if self.grid.in_bounds(i, j):
                self.occ.set_pos(j * w + i, unit.id)

    def remove_goal(self, unit: Unit) -> None:
        if unit.goal_cell is None:
            return
        w = self.grid.width
        for i, j in self.footprint(*unit.goal_cell, unit.cell_radius, unit.center):
            if self.grid.in_bounds(i, j):
                self.occ.clear_goal(j * w + i, unit.id)
        unit.goal_cell = None

    def update_goal(self, unit: Unit, goal: Point) -> None:
        """Pathfinder::updateGoal."""
        new = unit_cell(goal[0], goal[1], unit.center)
        if new == unit.goal_cell:
            return
        self.remove_goal(unit)
        unit.goal_cell = new
        w = self.grid.width
        for i, j in self.footprint(*new, unit.cell_radius, unit.center):
            if self.grid.in_bounds(i, j):
                self.occ.goal[j * w + i] = unit.id

    def check_for_movement(self, unit: Unit | None, cx: int, cy: int, transient: bool
                           ) -> MoveCheck | None:
        """Pathfinder::checkForMovement over the footprint; None is "off the map".  Every unit in
           the sandbox is an ally with an AI and none can crush another."""
        info = MoveCheck()
        if unit is None:
            return info
        grid, occ, w = self.grid, self.occ, self.grid.width
        r = unit.cell_radius
        above = r + 1 if unit.center else r
        allies: set[int] = set()
        for i in range(cx - r, cx + above):
            for j in range(cy - r, cy + above):
                if not (0 <= i < w and 0 <= j < grid.height):
                    return None
                index = j * w + i
                flags = occ.flags(index)
                if flags == UNIT_GOAL or flags == UNIT_GOAL_OTHER_MOVING:
                    info.ally_goal = True
                if flags == NO_UNITS:
                    continue
                pos_unit = occ.pos.get(index)
                if pos_unit is None or pos_unit == unit.id:
                    continue        # nobody standing here, or us (INVALID_ID is the ignore id)
                check = False
                if flags == UNIT_PRESENT_MOVING or flags == UNIT_GOAL_OTHER_MOVING:
                    info.ally_moving = True
                    check = transient
                if flags == UNIT_PRESENT_FIXED:
                    check = True
                if check and unit.is_infantry and pos_unit in self.infantry:
                    continue        # AIPathfind.cpp:6695, INFANTRY_MOVES_THROUGH_INFANTRY
                if check and pos_unit not in allies:
                    allies.add(pos_unit)
                    info.ally_fixed += 1
        return info

    def check_destination(self, unit: Unit, cx: int, cy: int) -> bool:
        """Pathfinder::checkDestination: no terrain and no ally's goal under the footprint."""
        grid, w = self.grid, self.grid.width
        for i, j in self.footprint(cx, cy, unit.cell_radius, unit.center):
            if not grid.in_bounds(i, j):
                return False
            if grid.kind[j * w + i] != CLEAR:
                return False
            g = self.occ.goal.get(j * w + i)
            if g is not None and g != unit.id:
                return False        # don't usurp your allies' goals
        return True

    def path_exists(self, a: Point, b: Point) -> bool:
        """clientSafeQuickDoesPathExist, read as "same zone"."""
        za = self.grid.zone_at(*a)
        return za != 0 and za == self.grid.zone_at(*b)

    # ---- the flow maps ----------------------------------------------------------------------
    def note_traffic(self, x: float, y: float, radius: int) -> None:
        """Pathfinder::noteTraffic: +24 a frame over the body's square, two cells at most."""
        cx, cy = cell_of(x, y)
        cx = min(max(cx, 0), self.grid.width - 1)
        cy = min(max(cy, 0), self.grid.height - 1)
        r = min(max(radius, 0), 2)
        w = self.grid.width
        for dx in range(-r, r + 1):
            for dy in range(-r, r + 1):
                x2, y2 = cx + dx, cy + dy
                if not self.grid.in_bounds(x2, y2):
                    continue
                i = y2 * w + x2
                if self.traffic[i] == 0:
                    if len(self.traffic_cells) >= PF_TRAFFIC_CELLS:
                        continue
                    self.traffic_cells.append(i)
                self.traffic[i] = min(self.traffic[i] + PF_TRAFFIC_DEPOSIT, PF_TRAFFIC_MAX)

    def decay_traffic(self) -> None:
        """Pathfinder::decayTraffic: x54/64 every three wall frames."""
        if not self.traffic_cells or self.frame - self.traffic_decay_frame < PF_TRAFFIC_DECAY_FRAMES:
            return
        self.traffic_decay_frame = self.frame
        keep = []
        for i in self.traffic_cells:
            v = (self.traffic[i] * PF_TRAFFIC_DECAY_NUM) // PF_TRAFFIC_DECAY_DEN
            self.traffic[i] = v
            if v > 0:
                keep.append(i)
        self.traffic_cells = keep

    def add_claim(self, cell: int, bucket: int, weight: int, now_bucket: int) -> None:
        """claimInto: a live claim adds, a dead one is overwritten.  A dict, so nothing is ever
           dropped for a full probe window, which the 32768-slot table can do."""
        key = (cell, bucket)
        live = self.claims.get(key)
        if live is not None and bucket >= now_bucket:
            self.claims[key] = min(live + weight, 0xFFFF)
        else:
            self.claims[key] = weight

    def stamp_claim_at(self, x: float, y: float, frame: int, dilation: int) -> None:
        """Pathfinder::stampClaimAt: whole weight in its bucket, half either side."""
        cx, cy = cell_of(x, y)
        bucket = frame // PF_CLAIM_BUCKET_FRAMES
        now_bucket = self.frame // PF_CLAIM_BUCKET_FRAMES
        w = self.grid.width
        for dx in range(-dilation, dilation + 1):
            for dy in range(-dilation, dilation + 1):
                x2, y2 = cx + dx, cy + dy
                if not self.grid.in_bounds(x2, y2):
                    continue
                cell = y2 * w + x2
                self.add_claim(cell, bucket, 2, now_bucket)
                if bucket > 0:
                    self.add_claim(cell, bucket - 1, 1, now_bucket)
                self.add_claim(cell, bucket + 1, 1, now_bucket)
                self.claim_touch[cell] = max(self.claim_touch[cell], bucket + 1)

    def get_claims(self, cx: int, cy: int, frame: int) -> int:
        if not self.grid.in_bounds(cx, cy):
            return 0
        cell = cy * self.grid.width + cx
        bucket = frame // PF_CLAIM_BUCKET_FRAMES
        if self.claim_touch[cell] < bucket:
            return 0
        return self.claims.get((cell, bucket), 0)

    def claim_path_timing(self, unit: Unit) -> None:
        """Pathfinder::claimPathTiming: from where the unit is, along its route, at its top speed."""
        if unit.path is None or unit.max_speed < 0.01:
            return
        speed = unit.max_speed
        dilation = min(max(unit.cell_radius, 0), 1)
        stride = dilation if dilation > 0 else 1
        pts = unit.path.points
        start = min(range(len(pts)), key=lambda k: (pts[k][0] - unit.x) ** 2 + (pts[k][1] - unit.y) ** 2)
        horizon = speed * PF_CLAIM_BUCKETS * PF_CLAIM_BUCKET_FRAMES
        cx, cy = unit.x, unit.y
        travelled = 0.0
        sample = stamped = 0
        for tx, ty in pts[start:]:
            dx, dy = tx - cx, ty - cy
            seg = math.hypot(dx, dy)
            if seg < 0.01:
                cx, cy = tx, ty
                continue
            steps = int(seg / CELL) + 1
            for s in range(1, steps + 1):
                t = s / steps
                travelled += seg / steps
                if travelled > horizon:
                    return
                sample += 1
                if (sample - 1) % stride != 0:
                    continue
                stamped += 1
                if stamped > PF_CLAIM_SAMPLES_MAX:
                    return
                self.stamp_claim_at(cx + dx * t, cy + dy * t, self.frame + int(travelled / speed),
                                    dilation)
            cx, cy = tx, ty

    def prune_claims(self) -> None:
        """Dead claims are reused in place in the C++ table; the dict drops them instead."""
        now_bucket = self.frame // PF_CLAIM_BUCKET_FRAMES
        self.claims = {k: v for k, v in self.claims.items() if k[1] >= now_bucket - 1}

    def flow_step_cost(self, base: int, cx: int, cy: int, radius: int, travel: int) -> int:
        """Pathfinder::flowStepCost: wall hug, then traffic, then the crossing claim at the moment
           the search expects to be here.  The searcher's own claims count too, as in the C++."""
        grid = self.grid
        if not grid.in_bounds(cx, cy):
            return 0
        cell = cy * grid.width + cx
        extra = 0
        need = PF_CLEARANCE_ORTHO * radius + PF_CLEARANCE_ORTHO // 2
        clearance = grid.clearance[cell]
        if clearance - need < PF_CLEARANCE_SOFT:
            extra += wall_hug_cost(base, clearance, need)
        if self.traffic_cells:
            jam = self.traffic[cell]
            if jam > 0:
                extra += traffic_cost(base, jam)
        if self.flow_speed > 0 and self.claim_touch[cell] >= self.flow_now_bucket:
            eta = self.flow_now + (travel * 256) // self.flow_speed
            bucket = eta // PF_CLAIM_BUCKET_FRAMES
            if self.claim_touch[cell] >= bucket:
                weight = self.claims.get((cell, bucket))
                if weight is not None:
                    extra += crossing_cost(base, weight)
        return extra

    def begin_flow_search(self, unit: Unit | None) -> None:
        """Pathfinder::beginFlowSearch."""
        self.flow_now = self.frame
        self.flow_now_bucket = self.frame // PF_CLAIM_BUCKET_FRAMES
        self.flow_on = unit is not None
        self.flow_speed = int(unit.max_speed * FLOW_PLAN_SHARE * COST_ORTHOGONAL / CELL * 256) if unit else 0
        if self.flow_on:
            self.h_num, self.h_den = PF_HEURISTIC_FLOW_NUM, PF_HEURISTIC_FLOW_DEN
        else:
            self.h_num, self.h_den = PF_HEURISTIC_RETAIL_NUM, PF_HEURISTIC_RETAIL_DEN
        self.chassis = unit.chassis if unit else 0
        self.start_dir = unit.dir if unit is not None and self.chassis > 0 else None

    # ---- the cell search --------------------------------------------------------------------
    def _cost_so_far(self, st: dict[int, SearchCell], index: int, parent: int | None) -> int:
        """PathfindCell::costSoFar: the step, +14 for a pinched cell, and the turn."""
        if parent is None:
            return 0
        w = self.grid.width
        cx, cy = index % w, index // w
        px, py = parent % w, parent // w
        pdx, pdy = px - cx, py - cy
        p = st[parent]
        cost = p.cost + (COST_ORTHOGONAL if pdx == 0 or pdy == 0 else COST_DIAGONAL)
        if self.grid.pinched[index]:
            cost += COST_DIAGONAL
        turns = 0
        if p.parent is not None:
            gx, gy = p.parent % w, p.parent // w
            dx, dy = gx - px, gy - py
            if dx != pdx or dy != pdy:
                dot = dx * pdx + dy * pdy
                if dot > 0:
                    turns, radians = 4, math.pi / 4
                elif dot == 0:
                    turns, radians = 8, math.pi / 2
                else:
                    turns, radians = 16, 3 * math.pi / 4
                turns = max(turns, turn_cost(radians, self.chassis, PF_TURN_CAP_STEP))
        elif self.start_dir is not None:
            length = math.hypot(pdx, pdy)
            if length > 0.0:
                dot = -(pdx * self.start_dir[0] + pdy * self.start_dir[1]) / length
                turns = turn_cost(math.acos(min(max(dot, -1.0), 1.0)), self.chassis, PF_TURN_CAP_START)
        return cost + turns

    def search(self, unit: Unit | None, start: Cell, goal: Cell | None, *,
               corridor: BlockCorridor | None, cap: int, partial: bool,
               found=None, examine_cap: int | None = None) -> tuple[int | None, dict[int, SearchCell], int]:
        """internalFindPath's loop (continueFindPath + examineNeighboringCells + the straight-line
           probe).  Returns (reached cell or None, the cells, cells allocated).  With `goal` None it is
           patchPath's and getMoveAwayFromPath's goal-less search, stopped by `found`.

           The open list is EA's bucket list: lowest total first, ties first in, first out.  A cell
           already on a list is never re-priced by an expansion (retail), only by the probe.  The
           search runs to the end in one call; the game's stops at the frame's cell budget and carries
           on next frame (ResumableSearch), which changes when the answer lands, not what it is."""
        grid, w = self.grid, self.grid.width
        pinched, passable = grid.pinched, grid.passable
        st: dict[int, SearchCell] = {}
        heap: list[tuple[int, int, int]] = []
        seq = itertools.count()
        r = unit.cell_radius if unit else 0
        sx, sy = start
        goal_index = goal[1] * w + goal[0] if goal else None

        def push(index: int, cell: SearchCell) -> None:
            cell.state = 1
            cell.seq = next(seq)
            heapq.heappush(heap, (cell.total, cell.seq, index))

        start_index = sy * w + sx
        first = SearchCell()
        first.total = cost_to_goal(sx - goal[0], sy - goal[1], self.h_num, self.h_den) if goal else 0
        st[start_index] = first
        push(start_index, first)
        cell_count = 0
        probe_best = UNREACHABLE
        closest, closest_dist = None, UNREACHABLE
        last_popped = start_index
        # m_isTunneling: a search that starts inside rock or a structure may cross invalid cells,
        # at 100 a cell, until it reaches a valid cell that is not pinched
        tunneling = not passable(sx, sy)

        def probe(parent: int) -> None:
            """examineCellsCallback along the straight line from `parent` to the goal."""
            if tunneling:
                return
            for frm, to in line_cells(parent % w, parent // w, goal[0], goal[1]):
                if not grid.in_bounds(*to):
                    return
                if frm is None:
                    continue
                tx, ty = to
                if not passable(tx, ty):
                    return
                if corridor is not None and not corridor.is_open(tx, ty):
                    return
                ti = ty * w + tx
                cell = st.get(ti)
                on_list = cell is not None and cell.state != 0
                if pinched[ti]:
                    return
                info = self.check_for_movement(unit, tx, ty, False)
                if info is None or info.enemy_fixed or info.ally_fixed:
                    return
                fcell = st[frm[1] * w + frm[0]]
                new_cost = fcell.cost + COST_ORTHOGONAL // 2
                if cell is None:
                    cell = SearchCell()
                    st[ti] = cell
                cell.blocked_by_ally = False
                travel = fcell.travel + COST_ORTHOGONAL
                cell.travel = travel           # written before the on-list test, as the C++ does
                if self.flow_on:
                    new_cost += self.flow_step_cost(COST_ORTHOGONAL, tx, ty, r, travel)
                remaining = cost_to_goal(tx - goal[0], ty - goal[1], self.h_num, self.h_den)
                if on_list and cell.cost <= new_cost:
                    continue
                cell.cost = new_cost
                cell.parent = frm[1] * w + frm[0]
                cell.total = new_cost + remaining
                push(ti, cell)

        while heap:
            if cell_count > cap:
                break
            total, cseq, index = heapq.heappop(heap)
            parent_cell = st[index]
            if parent_cell.state != 1 or parent_cell.seq != cseq:
                continue
            last_popped = index
            if goal_index is not None and index == goal_index:
                self.cells_total += cell_count
                return index, st, cell_count
            if found is not None and found(index):
                self.cells_total += cell_count
                return index, st, cell_count
            parent_cell.state = 2
            px, py = index % w, index // w
            if goal is not None:
                d = (goal[0] - px) ** 2 + (goal[1] - py) ** 2
                if d < closest_dist:
                    closest, closest_dist = index, d
            if examine_cap is not None and cell_count >= examine_cap:
                continue
            if goal is not None:
                d = (goal[0] - px) ** 2 + (goal[1] - py) ** 2
                if d < probe_best:
                    probe_best = d
                    probe(index)
            flags = [False] * 8
            for i, (dx, dy) in enumerate(DELTAS):
                if i >= 4 and not flags[ADJACENT[i - 4]] and not flags[ADJACENT[i - 3]]:
                    continue
                nx, ny = px + dx, py + dy
                if not (0 <= nx < w and 0 <= ny < grid.height):
                    continue
                ni = ny * w + nx
                cell = st.get(ni)
                if cell is not None and cell.state != 0:
                    continue
                valid = passable(nx, ny)
                if not valid and not tunneling:
                    continue
                flags[i] = True
                info = self.check_for_movement(unit, nx, ny, False)
                if info is None or info.enemy_fixed:
                    if not tunneling:
                        continue
                    valid = False
                    info = info or MoveCheck()
                if valid and not pinched[ni]:
                    tunneling = False
                if cell is None:
                    cell = SearchCell()
                    st[ni] = cell
                    cell_count += 1
                new_cost = self._cost_so_far(st, ni, index)
                step = COST_DIAGONAL if i >= 4 else COST_ORTHOGONAL
                travel = parent_cell.travel + step
                cell.travel = travel
                if self.flow_on:
                    new_cost += self.flow_step_cost(step, nx, ny, r, travel)
                if info.ally_moving and abs(nx - sx) < 10 and abs(ny - sy) < 10:
                    new_cost += 3 * COST_DIAGONAL
                if pinched[ni]:
                    new_cost += COST_ORTHOGONAL
                cell.blocked_by_ally = False
                if info.ally_fixed > 0:
                    new_cost += 3 * COST_DIAGONAL
                    cell.blocked_by_ally = True
                remaining = (cost_to_goal(nx - goal[0], ny - goal[1], self.h_num, self.h_den)
                             if goal else 0)
                if corridor is not None and not corridor.is_open(nx, ny):
                    new_cost += 100 * COST_ORTHOGONAL
                if grid.kind[ni] == OBSTACLE:
                    new_cost += 100 * COST_ORTHOGONAL
                if tunneling:
                    if not passable(nx, ny):
                        new_cost += 10 * COST_ORTHOGONAL
                    remaining = 0
                cell.cost = new_cost
                cell.parent = index
                cell.total = new_cost + remaining
                push(ni, cell)
        self.cells_total += cell_count
        if partial and cell_count > cap and closest is not None and closest != last_popped:
            return closest, st, cell_count
        return None, st, cell_count

    def build_path(self, unit: Unit | None, from_pos: Point, st: dict[int, SearchCell], goal: int,
                   center: bool, blocked: bool) -> Path:
        """Pathfinder::buildActualPath + prependCells + Path::optimize."""
        w = self.grid.width
        pinched = self.grid.pinched
        g = st[goal]
        if pinched[goal] and g.parent is not None and not pinched[g.parent]:
            goal = g.parent
        chain: list[int] = []
        index = goal
        while st[index].parent is not None:
            chain.append(index)
            index = st[index].parent
        if not chain:
            chain.append(index)
        nodes = [cell_pos(i % w, i // w, center) for i in reversed(chain)]
        blocked_by_ally = any(st[i].blocked_by_ally for i in chain)
        if from_pos != nodes[0]:
            nodes.insert(0, from_pos)
        rays: list = []
        route = self.optimize(unit, nodes, blocked, rays)
        if unit is not None:
            unit.rays = rays
        return Path(nodes, route, blocked_by_ally)

    def is_line_passable(self, unit: Unit | None, a: Point, b: Point, *, blocked: bool,
                         allow_pinched: bool) -> bool:
        """Pathfinder::isLinePassable: the Bresenham cells between, each one free of parked units
           over the footprint (moving ones too when `blocked`), not pinched unless allowed, and
           ground.  Only the centre line is tested for terrain."""
        grid = self.grid
        sx, sy = cell_of(*a)
        ex, ey = cell_of(*b)
        pinched = grid.pinched
        for _, (tx, ty) in line_cells(sx, sy, ex, ey):
            if not grid.in_bounds(tx, ty):
                return True
            info = self.check_for_movement(unit, tx, ty, blocked)
            if info is None or info.ally_fixed or info.enemy_fixed:
                return False
            if not allow_pinched and pinched[ty * grid.width + tx]:
                return False
            if not grid.passable(tx, ty):
                return False
        return True

    def optimize(self, unit: Unit | None, nodes: list[Point], blocked: bool,
                 rays: list | None = None) -> list[int]:
        """Path::optimize: from each anchor, the farthest of the next 32 nodes it has a clear line
           to, else the next node.  A run that is straight along an axis or a diagonal counts as
           passable whatever the line says, with EA's variable reuse kept."""
        route = [0]
        anchor, last = 0, len(nodes) - 1
        while anchor != last:
            count = 0
            node = anchor + 1
            while node < last:
                count += 1
                if count >= MAX_OPTIMIZE_LOOKAHEAD:
                    break
                node += 1
            placed = False
            while node != anchor:
                a, n = nodes[anchor], nodes[node]
                ok = self.is_line_passable(unit, a, n, blocked=blocked, allow_pinched=False)
                if rays is not None:
                    rays.append((a[0], a[1], n[0], n[1], ok))
                if not ok:
                    dx, dy = int(n[0] - a[0]), int(n[1] - a[1])
                    might = False
                    if abs(dx) == int(CELL) and abs(dy) == int(CELL):
                        ok = True
                    if dx == 0:
                        might = True
                        for t in range(node - 1, anchor, -1):
                            dx = int(nodes[t + 1][0] - nodes[t][0])
                            if dx != 0:
                                might = False
                    if dy == 0:
                        might = True
                        for t in range(node - 1, anchor, -1):
                            dy = int(nodes[t + 1][1] - nodes[t][1])
                            if dy != 0:
                                might = False
                    if dx == dy:
                        might = True
                        for t in range(node - 1, anchor, -1):
                            dx = int(nodes[t + 1][0] - nodes[t][0])
                            dy = int(nodes[t + 1][1] - nodes[t][1])
                            if dy != dx:
                                might = False
                    if dx == -dy:
                        might = True
                        for t in range(node - 1, anchor, -1):
                            dx = int(nodes[t + 1][0] - nodes[t][0])
                            dy = int(nodes[t + 1][1] - nodes[t][1])
                            if dy != -dx:
                                might = False
                    if might:
                        ok = True
                if ok:
                    route.append(node)
                    anchor = node
                    placed = True
                    break
                node -= 1
            if not placed:
                anchor += 1
                route.append(anchor)
        return route

    def find_path(self, unit: Unit, frm: Point, to: Point) -> Path | None:
        """Pathfinder::findPath -> internalFindPath."""
        grid, w = self.grid, self.grid.width
        self.begin_flow_search(unit)
        adjust = to if unit.center else (to[0] + CELL / 2, to[1] + CELL / 2)
        goal = cell_of(*adjust)
        if not grid.in_bounds(*goal) or not self.check_destination(unit, *goal):
            return None
        start = cell_of(*frm)
        if not grid.in_bounds(*start):
            return None
        sz, gz = grid.zones[start[1] * w + start[0]], grid.zones[goal[1] * w + goal[0]]
        # a start inside rock has no ground zone here; the C++ reads the terrain zone, which the
        # sandbox does not keep, so the screen is skipped and the tunneling search answers it
        if (sz and sz != gz) or not grid.passable(*goal):
            return None
        corridor = block_corridor(grid, start, goal)
        self.searches += 1
        reached, st, _ = self.search(unit, start, goal, corridor=corridor,
                                     cap=MAX_INTERNAL_CELL_COUNT, partial=True)
        if reached is None:
            return None
        return self.build_path(unit, frm, st, reached, unit.center, False)

    def patch_path(self, unit: Unit) -> Path | None:
        """Pathfinder::patchPath: a goal-less search from where the unit is to the first node of its
           old path past which nothing parked is in the way, at most 2000 cells examined."""
        old = unit.path
        if old is None:
            return None
        self.begin_flow_search(unit)
        grid, w = self.grid, self.grid.width
        start_pos = unit.pos if unit.center else (unit.x + CELL * 0.5, unit.y)    # EA adds to x twice
        start = cell_of(*start_pos)
        k = len(old.nodes) - 1
        while k > 0:
            cx, cy = cell_of(*old.nodes[k])
            info = self.check_for_movement(unit, cx, cy, False)
            if info is None or info.ally_fixed or info.enemy_fixed:
                break
            k -= 1
        start_node = k
        if start_node == len(old.nodes) - 1:
            return None
        targets = {cell_pos_key(old.nodes[m]): m for m in range(len(old.nodes) - 1, start_node, -1)}

        def found(index: int) -> bool:
            return cell_pos_key(cell_pos(index % w, index // w, unit.center)) in targets

        self.searches += 1
        reached, st, _ = self.search(unit, start, None, corridor=None, cap=UNREACHABLE,
                                     partial=False, found=found, examine_cap=PATCH_CELL_LIMIT)
        if reached is None:
            return None
        match = targets[cell_pos_key(cell_pos(reached % w, reached // w, unit.center))]
        chain: list[int] = []
        index = reached
        while st[index].parent is not None:
            chain.append(index)
            index = st[index].parent
        if not chain:
            chain.append(index)
        nodes = [cell_pos(i % w, i // w, unit.center) for i in reversed(chain)]
        nodes.extend(old.nodes[match + 1:])
        if unit.pos != nodes[0]:
            nodes.insert(0, unit.pos)
        return Path(nodes, self.optimize(unit, nodes, True))

    def move_away_path(self, unit: Unit, other: Unit) -> Path | None:
        """Pathfinder::getMoveAwayFromPath: the nearest cell whose box clears the other's route, is
           not where we stand, and checkDestination accepts.  Capped at 2000 cells examined, which
           the C++ does not do (ponytail: the search drains the map when it fails; lift it if a
           wedge shows up that a longer search would have answered)."""
        avoid = other.path.points if other.path else [other.pos]
        self.begin_flow_search(None)
        w = self.grid.width
        start = cell_of(*unit.pos)
        half = unit.cell_radius * CELL - CELL / 4.0 + (CELL / 2 if unit.center else 0.0)
        half += other.cell_radius * CELL + (CELL / 2 if other.center else 0.0)

        def found(index: int) -> bool:
            cx, cy = index % w, index // w
            if (cx, cy) == start:
                return False
            px, py = cell_pos(cx, cy, unit.center)
            box = (px - half, py - half, px + half, py + half)
            if len(avoid) == 1:
                if box[0] < avoid[0][0] < box[2] and box[1] < avoid[0][1] < box[3]:
                    return False
            for a, b in zip(avoid, avoid[1:]):
                if segment_hits_box(a, b, box):
                    return False
            return self.check_destination(unit, cx, cy)

        reached, st, _ = self.search(unit, start, None, corridor=None, cap=UNREACHABLE,
                                     partial=False, found=found, examine_cap=PATCH_CELL_LIMIT)
        if reached is None:
            return None
        return self.build_path(unit, unit.pos, st, reached, unit.center, False)

    # ---- the group trunk (findGroundPath) -----------------------------------------------------
    def clear_for_diameter(self, cx: int, cy: int, diameter: int) -> int:
        """Pathfinder::clearCellForDiameter: the widest even diameter that fits, outside corners cut
           from radius 2 up; a parked unit closes the cell for any diameter of 2 or more."""
        grid, w = self.grid, self.grid.width
        radius = diameter // 2
        above = radius + 1 if radius == 0 else radius
        cut = radius > 1
        clear = True
        for i in range(cx - radius, cx + above):
            xedge = i == cx - radius or i == cx + above - 1
            for j in range(cy - radius, cy + above):
                yedge = j == cy - radius or j == cy + above - 1
                if xedge and yedge and cut:
                    continue
                if not grid.in_bounds(i, j):
                    return 0
                if grid.kind[j * w + i] != CLEAR:
                    clear = False
                if diameter >= 2 and self.occ.flags(j * w + i) == UNIT_PRESENT_FIXED:
                    clear = False
                if not clear:
                    break
            if not clear:
                break
        if clear:
            return 1 if radius == 0 else 2 * radius
        if diameter < 2:
            return 0
        return self.clear_for_diameter(cx, cy, diameter - 2)

    def find_ground_path(self, frm: Point, to: Point, diameter: int) -> Path | None:
        """Pathfinder::findGroundPath: the group corridor.  No flow charge and no object, so the
           retail 3/2 estimate and turn prices; a cell narrower than the diameter costs 6 a missing
           cell rather than being refused; the straight-line probe runs from every expansion."""
        grid, w = self.grid, self.grid.width
        self.begin_flow_search(None)
        start = cell_of(*frm)
        cell = cell_of(*to)
        corridor = block_corridor(grid, start, cell)
        if self.clear_for_diameter(*cell, diameter) != diameter:
            offset = 1
            while offset < 8:
                base = cell
                hit = False
                for ddx, ddy in ((offset, 0), (0, offset), (-offset, 0), (-offset, 0), (0, -offset),
                                 (0, -offset), (offset, 0), (offset, 0)):
                    cell = (cell[0] + ddx, cell[1] + ddy)
                    if self.clear_for_diameter(*cell, diameter) == diameter:
                        hit = True
                        break
                if hit:
                    break
                offset += 1
                cell = base
            if offset >= 8:
                return None
        goal = cell
        if not grid.in_bounds(*goal) or not grid.in_bounds(*start):
            return None
        if grid.zones[start[1] * w + start[0]] != grid.zones[goal[1] * w + goal[0]]:
            return None
        self.searches += 1
        st: dict[int, SearchCell] = {}
        heap: list[tuple[int, int, int]] = []
        seq = itertools.count()
        goal_index = goal[1] * w + goal[0]

        def push(index: int, c: SearchCell) -> None:
            c.state = 1
            c.seq = next(seq)
            heapq.heappush(heap, (c.total, c.seq, index))

        s = SearchCell()
        s.total = cost_to_goal(start[0] - goal[0], start[1] - goal[1], self.h_num, self.h_den)
        st[start[1] * w + start[0]] = s
        push(start[1] * w + start[0], s)
        st.setdefault(goal_index, SearchCell())
        cell_count = 0
        new_cost = 0        # uninitialised in the C++ and read stale for the goal; carried here
        while heap:
            if cell_count > MAX_GROUND_PATH_CELLS:
                break
            _, cseq, index = heapq.heappop(heap)
            pc = st[index]
            if pc.state != 1 or pc.seq != cseq:
                continue
            if index == goal_index:
                self.cells_total += cell_count
                return self.build_ground_path(frm, st, index, diameter)
            pc.state = 2
            px, py = index % w, index // w
            for frm_cell, to_cell in line_cells(px, py, goal[0], goal[1]):     # groundCellsCallback
                if not grid.in_bounds(*to_cell):
                    break
                if frm_cell is None:
                    continue
                ti = to_cell[1] * w + to_cell[0]
                c = st.get(ti)
                if c is not None and c.state != 0:
                    break
                if self.clear_for_diameter(*to_cell, diameter) != diameter:
                    break
                if c is None:
                    c = SearchCell()
                    st[ti] = c
                c.cost = st[frm_cell[1] * w + frm_cell[0]].cost + COST_ORTHOGONAL // 2
                c.parent = frm_cell[1] * w + frm_cell[0]
                c.total = c.cost + cost_to_goal(to_cell[0] - goal[0], to_cell[1] - goal[1],
                                                self.h_num, self.h_den)
                push(ti, c)
            flags = [False] * 8
            for i, (dx, dy) in enumerate(DELTAS):
                nx, ny = px + dx, py + dy
                if not grid.in_bounds(nx, ny):
                    continue
                if corridor is not None and not corridor.admits_ground(nx, ny):
                    continue
                ni = ny * w + nx
                c = st.get(ni)
                on_list = c is not None and c.state != 0
                if ni != goal_index:
                    if i >= 4 and not flags[ADJACENT[i - 4]] and not flags[ADJACENT[i - 3]]:
                        continue
                    clear = self.clear_for_diameter(nx, ny, diameter)
                    if grid.kind[ni] != CLEAR or grid.pinched[ni]:
                        continue
                    flags[i] = True
                    if c is None:
                        c = SearchCell()
                        st[ni] = c
                        cell_count += 1
                    new_cost = self._cost_so_far(st, ni, index)
                    if clear < diameter:
                        new_cost = int(new_cost + 0.6 * ((diameter - clear) * COST_ORTHOGONAL))
                if on_list and c.cost <= new_cost:
                    continue
                c.cost = new_cost
                c.parent = index
                c.total = new_cost + cost_to_goal(nx - goal[0], ny - goal[1], self.h_num, self.h_den)
                push(ni, c)
        self.cells_total += cell_count
        return None

    def build_ground_path(self, frm: Point, st: dict[int, SearchCell], goal: int, diameter: int) -> Path:
        """buildGroundPath: prependCells off-centre, then Path::optimizeGroundPath."""
        w = self.grid.width
        chain: list[int] = []
        index = goal
        while st[index].parent is not None:
            chain.append(index)
            index = st[index].parent
        if not chain:
            chain.append(index)
        nodes = [cell_pos(i % w, i // w, False) for i in reversed(chain)]
        if frm != nodes[0]:
            nodes.insert(0, frm)
        return Path(nodes, self.optimize_ground(nodes, diameter))

    def ground_line_passable(self, a: Point, b: Point, diameter: int) -> bool:
        """Pathfinder::isGroundPathPassable."""
        for _, (tx, ty) in line_cells(*cell_of(*a), *cell_of(*b)):
            if not self.grid.in_bounds(tx, ty):
                return True
            if self.clear_for_diameter(tx, ty, diameter) != diameter:
                return False
        return True

    def optimize_ground(self, nodes: list[Point], diameter: int) -> list[int]:
        """Path::optimizeGroundPath: Path::optimize with the width test, then any leg under two
           cells that is not the last is dropped (the jig/jog pass)."""
        route = [0]
        anchor, last = 0, len(nodes) - 1
        while anchor != last:
            count = 0
            node = anchor + 1
            while node < last:
                count += 1
                if count >= MAX_OPTIMIZE_LOOKAHEAD:
                    break
                node += 1
            placed = False
            while node != anchor:
                a, n = nodes[anchor], nodes[node]
                ok = self.ground_line_passable(a, n, diameter)
                if not ok:
                    dx, dy = int(n[0] - a[0]), int(n[1] - a[1])
                    might = False
                    if dx == 0:
                        might = True
                        for t in range(node - 1, anchor, -1):
                            dx = int(nodes[t + 1][0] - nodes[t][0])
                            if dx != 0:
                                might = False
                    if dy == 0:
                        might = True
                        for t in range(node - 1, anchor, -1):
                            dy = int(nodes[t + 1][1] - nodes[t][1])
                            if dy != 0:
                                might = False
                    if dx == dy:
                        might = True
                        for t in range(node - 1, anchor, -1):
                            dx = int(nodes[t + 1][0] - nodes[t][0])
                            dy = int(nodes[t + 1][1] - nodes[t][1])
                            if dy != dx:
                                might = False
                    if dx == -dy:
                        might = True
                        for t in range(node - 1, anchor, -1):
                            dx = int(nodes[t + 1][0] - nodes[t][0])
                            dy = int(nodes[t + 1][1] - nodes[t][1])
                            if dy != -dx:
                                might = False
                    ok = might
                if ok:
                    route.append(node)
                    anchor = node
                    placed = True
                    break
                node -= 1
            if not placed:
                anchor += 1
                route.append(anchor)
        k = 0
        while k < len(route):
            if k + 2 < len(route):
                a, n = nodes[route[k]], nodes[route[k + 1]]
                if (n[0] - a[0]) ** 2 + (n[1] - a[1]) ** 2 < CELL * CELL * 3.9:
                    del route[k + 1]
            k += 1
        return route

    # ---- the queue (processPathfindQueue) ---------------------------------------------------
    def process_queue(self, sim: "Simulation") -> None:
        depth = len(self.queue)
        cap = PATHFIND_REQUESTS_PER_FRAME
        budget = PATHFIND_CELLS_PER_FRAME
        if depth > PATHFIND_DEEP_QUEUE:
            cap += (depth - PATHFIND_DEEP_QUEUE) // PATHFIND_DEEP_STEP
            budget += PATHFIND_CELLS_PER_FRAME * ((depth - PATHFIND_DEEP_QUEUE) // PATHFIND_DEEP_STEP)
        cap = min(cap, PATHFIND_REQUESTS_PER_FRAME_DEEP)
        budget = min(budget, PATHFIND_CELLS_PER_FRAME * PATHFIND_DEEP_CELLS)
        spent = started = 0
        waiting = []
        for unit in self.queue:
            if not unit.waiting_for_path:
                unit.queued = False             # answered some other way meanwhile
                continue
            if unit.queue_at > self.frame or started >= cap or spent >= budget:
                waiting.append(unit)
                continue
            before = self.cells_total
            started += 1
            unit.queued = False
            sim.compute_path(unit)
            spent += self.cells_total - before
        self.queue = waiting


def cell_pos_key(p: Point) -> tuple[int, int]:
    return int(round(p[0] * 2)), int(round(p[1] * 2))


def segment_hits_box(a: Point, b: Point, box: tuple[float, float, float, float]) -> bool:
    """LineInRegion: does the segment touch the axis-aligned box (Liang-Barsky)."""
    x0, y0, x1, y1 = box
    t0, t1 = 0.0, 1.0
    dx, dy = b[0] - a[0], b[1] - a[1]
    for p, q in ((-dx, a[0] - x0), (dx, x1 - a[0]), (-dy, a[1] - y0), (dy, y1 - a[1])):
        if p == 0:
            if q < 0:
                return False
            continue
        t = q / p
        if p < 0:
            t0 = max(t0, t)
        else:
            t1 = min(t1, t)
        if t0 > t1:
            return False
    return True


# ---------------------------------------------------------------------------------------------
# The simulation: orders, the move state, doLocomotor, collisions
# ---------------------------------------------------------------------------------------------

@dataclass
class OrderStats:
    trunk: list[Point] = field(default_factory=list)
    trunk_diameter: int = 0
    lanes: int = 0
    notes: list[str] = field(default_factory=list)


class Simulation:
    def __init__(self, grid: Grid, units: list[Unit]) -> None:
        self.grid = grid
        self.pf = Pathfinder(grid)
        self.units = units
        self.by_id = {unit.id: unit for unit in units}
        self.stats = OrderStats()
        self.overlap_ticks = 0
        self.overlap_depth = 0.0
        self.widest_radius = max((unit.radius for unit in units), default=TANK_RADIUS)
        self.fastest_speed = max((unit.max_speed for unit in units), default=TANK_SPEED)
        for unit in units:
            self.pf.update_pos(unit)
            self.pf.update_goal(unit, unit.pos)       # parked where it stands

    @property
    def frame(self) -> int:
        return self.pf.frame

    # ---- orders -----------------------------------------------------------------------------
    def flood_group_goals(self, dest: Point, members: Sequence[Unit]) -> list[Point]:
        """Pathfinder::floodGroupGoals: a flood out of the clicked cell; members queue by straight
           time to the click and each cell goes to whichever of the first eight would reach it
           soonest, never to one it would reach only past the click."""
        pf, grid = self.pf, self.grid
        goals = [dest] * len(members)
        if not members:
            return goals
        start = cell_of(*dest)
        if not grid.in_bounds(*start):
            return goals
        arrivals = []
        for k, m in enumerate(members):
            ax, ay = m.x - dest[0], m.y - dest[1]
            length = math.hypot(ax, ay)
            speed = max(m.max_speed, MIN_MEMBER_SPEED)
            if length > CELL:
                ax, ay = ax / length, ay / length
            arrivals.append([length / speed, k, m, speed, ax, ay, length])
        arrivals.sort(key=lambda a: (a[0], a[1]))
        opened = {start}
        wave = [start]
        placed = [False] * len(members)
        unplaced = len(members)
        limit = MIN_FLOOD_CELLS + FLOOD_CELLS_PER_MEMBER * len(members)
        head = 0
        while head < len(wave) and head < limit and unplaced > 0:
            cx, cy = wave[head]
            standable = grid.passable(cx, cy)
            if standable:
                wx, wy = (cx + 0.5) * CELL, (cy + 0.5) * CELL
                ox, oy = wx - dest[0], wy - dest[1]
                candidates = []
                for arrival, k, m, speed, ax, ay, length in arrivals:
                    if len(candidates) >= MAX_MEMBERS_TRIED_PER_CELL:
                        break
                    if placed[k]:
                        continue
                    if length > CELL and -(ox * ax + oy * ay) > CELL:
                        continue
                    candidates.append((math.hypot(m.x - wx, m.y - wy) / speed, k, m))
                candidates.sort(key=lambda c: (c[0], c[1]))
                for _, k, m in candidates:
                    goal = self.check_for_adjust(m, cx, cy, dest)
                    if goal is None:
                        continue
                    if len(members) == 1 and head == 0:
                        goal = dest
                    pf.update_goal(m, goal)
                    goals[k] = goal
                    placed[k] = True
                    unplaced -= 1
                    break
            if not standable and unplaced < len(members):
                head += 1
                continue
            for dy in (-1, 0, 1):
                for dx in (-1, 0, 1):
                    nxt = (cx + dx, cy + dy)
                    if grid.in_bounds(*nxt) and nxt not in opened:
                        opened.add(nxt)
                        wave.append(nxt)
            head += 1
        return goals

    def check_for_adjust(self, unit: Unit, cx: int, cy: int, dest: Point) -> Point | None:
        """Pathfinder::checkForAdjust without a group destination."""
        pf = self.pf
        if not pf.check_destination(unit, cx, cy):
            return None
        adjust = cell_pos(cx, cy, unit.center)
        if not pf.path_exists(unit.pos, dest) and pf.path_exists(dest, adjust):
            return adjust
        return adjust if pf.path_exists(unit.pos, adjust) else None

    def adjust_destination(self, unit: Unit, dest: Point) -> Point:
        """Pathfinder::adjustDestination, as AIInternalMoveToState::onEnter calls it: the goal cell
           if checkForAdjust takes it, else a square spiral out of it, 400 cells at most."""
        cx, cy = cell_of(*(dest if unit.center else (dest[0] + CELL / 2, dest[1] + CELL / 2)))
        found = self.check_for_adjust(unit, cx, cy, dest)
        if found is not None:
            return found
        limit, delta = 400, 1
        while limit > 0:
            for ddx, ddy, steps in ((1, 0, delta), (0, 1, delta), (-1, 0, delta + 1), (0, -1, delta + 1)):
                for _ in range(steps):
                    cx, cy = cx + ddx, cy + ddy
                    limit -= 1
                    found = self.check_for_adjust(unit, cx, cy, dest)
                    if found is not None:
                        return found
            delta += 2
        return dest

    def plan_lane(self, unit: Unit, trunk: list[Point], offset: float, goal: Point) -> list[Point] | None:
        """crowdPlanLane: every corner of the trunk moved `offset` left along its bisector, closing
           in quarters where the ground or a parked unit refuses it, each corner no wider than its
           neighbours; then cut back to the corner nearest the member's own spot, and home."""
        pf, grid = self.pf, self.grid
        n = len(trunk)
        shifts: list[Point] = [(0.0, 0.0)] * n
        fits = [CROWD_PLAN_STEPS] * n
        for k in range(1, n - 1):
            fits[k] = 0
            shift = lane_corner(trunk[k - 1], trunk[k], trunk[k + 1], offset)
            if shift is None:
                continue
            shifts[k] = shift
            c = trunk[k]
            for q in range(CROWD_PLAN_STEPS, 0, -1):
                p = (c[0] + shift[0] * q / CROWD_PLAN_STEPS, c[1] + shift[1] * q / CROWD_PLAN_STEPS)
                if grid.passable_world(*p) and pf.is_line_passable(unit, c, p, blocked=False,
                                                                   allow_pinched=True):
                    fits[k] = q
                    break
        shares = list(fits)
        for k in range(1, n - 1):
            shares[k] = min(shares[k], fits[k - 1], fits[k + 1])
        out: list[Point] = []
        prev = unit.pos
        prev_corner = -1
        for k in range(1, n - 1):
            a, c = trunk[k - 1], trunk[k]
            ix, iy = c[0] - a[0], c[1] - a[1]
            if (c[0] - prev[0]) * ix + (c[1] - prev[1]) * iy <= 0.0:
                continue
            placed = False
            for q in range(shares[k], -1, -1):
                p = (c[0] + shifts[k][0] * q / CROWD_PLAN_STEPS, c[1] + shifts[k][1] * q / CROWD_PLAN_STEPS)
                group_leg = q == 0 and prev == a
                if not group_leg and not pf.is_line_passable(unit, prev, p, blocked=False,
                                                             allow_pinched=True):
                    continue
                out.append(p)
                prev, prev_corner, placed = p, k, True
                break
            if not placed and (prev_corner == k - 1 or pf.is_line_passable(unit, prev, a, blocked=False,
                                                                            allow_pinched=True)):
                out.extend((a, c))
                prev, prev_corner, placed = c, k, True
            if not placed:
                return None
        while len(out) >= 2:
            last, before = out[-1], out[-2]
            if (before[0] - goal[0]) ** 2 + (before[1] - goal[1]) ** 2 >= (last[0] - goal[0]) ** 2 + (last[1] - goal[1]) ** 2:
                break
            out.pop()
        while out and not pf.is_line_passable(unit, out[-1], goal, blocked=False, allow_pinched=True):
            out.pop()
        if not out:
            return None
        out.append(goal)
        return out

    def group_move(self, members: Sequence[Unit], click: Point) -> OrderStats:
        """AIGroup::groupMoveToPosition for a player's move (gatherOnPoint): lanes across the travel
           direction, goals from the flood, the trunk at 6, 4, 2 cells from the member nearest the
           middle, each vehicle's lane of it, then a move order each, nearest the click first."""
        pf = self.pf
        stats = OrderStats()
        if not members:
            return stats
        cx = sum(m.x for m in members) / len(members)
        cy = sum(m.y for m in members) / len(members)
        ordered = sorted(members, key=lambda m: ((m.x - click[0]) ** 2 + (m.y - click[1]) ** 2, m.id))
        for m in ordered:
            pf.remove_goal(m)
        gx, gy = click[0] - cx, click[1] - cy
        gl = math.hypot(gx, gy)
        lane_offsets: dict[int, float] = {}
        spacing = 0.0
        for m in members:
            clear_crowd_lane(m)
        if gl > 1.0 and len(ordered) > 1:
            dx, dy = gx / gl, gy / gl
            across = sorted(((m.x - cx) * -dy + (m.y - cy) * dx, m.id, m) for m in ordered)
            widest = max(ordered, key=lambda m: m.radius)
            spacing = 2.0 * widest.radius * LANE_SPACING_AIR
            left = crowd_extent(self.grid, widest.radius, cx, cy, -dy, dx)
            right = crowd_extent(self.grid, widest.radius, cx, cy, dy, -dx)
            bias = (left - right) * 0.5
            lanes = min(lane_count(left + right, spacing, 0), len(across))
            stats.lanes = lanes
            for i, (_, _, m) in enumerate(across):
                lane = (i * lanes) // len(across)
                offset = bias + (lane - (lanes - 1) * 0.5) * spacing
                m.pending_crowd_lat, m.has_pending_crowd_lat = offset, True
                m.crowd_lane_idx, m.crowd_lane_of, m.crowd_lane_space = i, len(across), spacing
                lane_offsets[m.id] = (lane - (lanes - 1) * 0.5) * spacing
        goals = self.flood_group_goals(click, ordered)
        trunk: list[Point] = []
        if lane_offsets:
            vehicles = [m for m in ordered if not m.is_infantry]
            trunk_start = (min(vehicles, key=lambda m: (m.x - cx) ** 2 + (m.y - cy) ** 2).pos
                           if vehicles else (cx, cy))
            for aim in (click, goals[0]):
                for diameter in range(CROWD_TRUNK_DIAMETER, 1, -2):
                    path = pf.find_ground_path(trunk_start, aim, diameter)
                    if path is not None:
                        trunk = path.points
                        stats.trunk_diameter = diameter
                        break
                if trunk:
                    break
        stats.trunk = trunk
        for m, dest in zip(ordered, goals):
            if trunk and m.id in lane_offsets and not m.is_infantry:     # a foot soldier walks through the crowd
                lane = self.plan_lane(m, trunk, lane_offsets[m.id], dest)
                if lane is not None:
                    m.planned = lane
                    m.pending_crowd_lat, m.has_pending_crowd_lat = 0.0, True
                    m.crowd_lane_idx, m.crowd_lane_of = 0, 1
            self.move_to(m, dest)
        self.stats = stats
        return stats

    def move_to(self, unit: Unit, dest: Point) -> None:
        """aiMoveToPosition: the move state, which asks for a path."""
        dest = self.adjust_destination(unit, dest)
        self.pf.update_goal(unit, dest)
        unit.goal = dest
        unit.moving = True
        unit.gave_up = False
        unit.move_away_from = None
        unit.resume_goal = None
        unit.donut_until = self.frame + DONUT_FRAMES            # Locomotor::startMove
        self.request_path(unit)

    def request_path(self, unit: Unit) -> None:
        """AIUpdateInterface::requestPath: into the queue, held a tenth of a second when the last path
           was that recent."""
        unit.waiting_for_path = True
        delay = 0
        if unit.path_timestamp > self.frame - REPATH_DEBOUNCE_FRAMES:
            delay = REPATH_DEBOUNCE_FRAMES
            if unit.path is not None and unit.is_blocked_and_stuck:
                unit.ignore_collisions_until = self.frame + 2 * LOGICFRAMES_PER_GAME_SECOND
                unit.blocked_frames = 0
                unit.is_blocked = False
                unit.is_blocked_and_stuck = False
        unit.queue_at = self.frame + delay
        if not unit.queued:
            unit.queued = True
            self.pf.queue.append(unit)

    def compute_path(self, unit: Unit) -> None:
        """AIUpdateInterface::computePath: the planned lane when the order still matches it, a patch
           when blocked and stuck, else findPath.  findClosestPath, the C++'s fallback for no path at
           all, is not modelled: the unit gives the order up."""
        if not unit.waiting_for_path or unit.goal is None:
            return
        pf = self.pf
        planned, unit.planned = unit.planned, []
        stuck = unit.is_blocked_and_stuck
        if not stuck:
            destroy_path(unit)
        new: Path | None = None
        dest = unit.goal
        if planned and not stuck:
            end = planned[-1]
            if ((end[0] - dest[0]) ** 2 + (end[1] - dest[1]) ** 2 <= CROWD_PLAN_END_SLACK ** 2
                    and pf.is_line_passable(unit, unit.pos, planned[0], blocked=False, allow_pinched=True)):
                nodes = [unit.pos, *planned[:-1], dest]
                new = Path(nodes, list(range(len(nodes))))
        if new is None:
            if stuck:
                new = pf.patch_path(unit)
            else:
                new = pf.find_path(unit, unit.pos, dest)
        unit.waiting_for_path = False
        unit.repath_pending = False
        if new is not None:
            destroy_path(unit)
            unit.path = new
            unit.dist_to_goal = new.length()
            if unit.first_length == 0.0:
                unit.first_length = new.length()
                unit.first_waypoints = len(new.route)
            self.pf.update_goal(unit, new.last)
            if new.blocked_by_ally:
                self.move_allies(unit, new)
        elif unit.path is None:
            unit.moving = False                 # the order fails; the unit parks where it stands
            unit.gave_up = True
            self.pf.update_goal(unit, unit.pos)
        else:
            destroy_path(unit)
            unit.is_blocked = False
            self.request_path(unit)
            unit.queue_at = self.frame + LOGICFRAMES_PER_GAME_SECOND
        unit.path_timestamp = self.frame
        unit.blocked_frames = 0
        unit.is_blocked_and_stuck = False

    def move_allies(self, unit: Unit, path: Path) -> None:
        """Pathfinder::moveAllies: parked allies under the route's footprint are asked to move."""
        pf, w = self.pf, self.grid.width
        asked: list[int] = []
        for node in path.nodes[1:]:
            cx, cy = cell_of(*node)
            for i, j in pf.footprint(cx, cy, unit.cell_radius, unit.center):
                p = pf.occ.pos.get(j * w + i) if self.grid.in_bounds(i, j) else None
                if p is not None and p != unit.id and p not in asked:
                    asked.append(p)
        for uid in asked:
            other = self.by_id[uid]
            if unit.is_infantry and other.is_infantry:
                continue        # AIPathfind.cpp:12614: infantry walks through infantry
            if not other.is_moving:
                self.move_away(other, unit)

    def move_away(self, unit: Unit, other: Unit) -> None:
        """aiMoveAwayFromUnit: a temporary state; the order it interrupts resumes when it ends."""
        path = self.pf.move_away_path(unit, other)
        if path is None:
            return
        if unit.moving and unit.move_away_from is None and unit.goal is not None:
            unit.resume_goal = unit.goal
        destroy_path(unit)
        unit.path = path
        unit.dist_to_goal = path.length()
        unit.goal = path.last
        unit.moving = True
        unit.waiting_for_path = False
        unit.move_away_from = other.id
        unit.path_timestamp = self.frame
        unit.donut_until = self.frame + DONUT_FRAMES
        self.pf.update_goal(unit, path.last)

    # ---- one frame --------------------------------------------------------------------------
    def step(self) -> None:
        pf = self.pf
        pf.process_queue(self)
        pf.decay_traffic()
        if self.frame % PF_CLAIM_REFRESH_FRAMES == 0:
            pf.prune_claims()
        for unit in self.units:
            self.move_state(unit)
            self.do_locomotor(unit)
        for unit in self.units:
            self.physics_update(unit)
            pf.update_pos(unit)
            if unit.arrived and unit.moving:
                unit.churn += 1
        self.collide()
        pf.frame += 1

    def move_state(self, unit: Unit) -> None:
        """AIInternalMoveToState::update: arrival on the locomotor's distance, and a repath when
           blocked and stuck or blocked for over two game seconds, or (LADDER_REPATH_FLAG) when
           crowdRepath asked for one."""
        if not unit.moving or unit.path is None:
            return
        if unit.dist_to_goal < unit.kind.close_enough:
            last = unit.path.last
            if math.hypot(unit.x - last[0], unit.y - last[1]) <= 4 * CELL:
                self.arrive(unit)
                return
        if unit.waiting_for_path:
            return
        if (unit.is_blocked_and_stuck or unit.blocked_frames > 2 * LOGICFRAMES_PER_GAME_SECOND
                or unit.repath_pending):
            unit.repath_pending = False
            unit.repaths += 1
            self.request_path(unit)

    def arrive(self, unit: Unit) -> None:
        resume = unit.resume_goal
        if resume is None and unit.move_away_from is None:
            unit.arrived = True
        unit.repath_pending = False
        destroy_path(unit)
        unit.moving = False
        unit.move_away_from = None
        unit.resume_goal = None
        if resume is not None:
            self.move_to(unit, resume)

    def do_locomotor(self, unit: Unit) -> None:
        """AIUpdateInterface::doLocomotor for POSITION_ON_PATH."""
        pf = self.pf
        unit.head_on_frames = unit.head_on_frames + 1 if unit.head_on_seen else 0
        unit.head_on_seen = False
        if unit.is_blocked:
            unit.blocked_frames += 1
            unit.blocked_total += 1
            pf.note_traffic(unit.x, unit.y, unit.cell_radius)
        else:
            unit.blocked_frames = 0
        if unit.path is not None and (self.frame + unit.id) % PF_CLAIM_REFRESH_FRAMES == 0:
            pf.claim_path_timing(unit)
        self.update_progress(unit)
        self.stuck_rescue(unit)
        unit.is_blocked = False
        side_step = unit.side_step_seen
        side_away = (unit.side_step_x, unit.side_step_y)
        unit.side_step_seen = False
        unit.side_step_x = unit.side_step_y = 0.0
        blocked = unit.blocked_frames > 0
        if unit.moving and unit.path is None:
            return              # waiting for its path: the locomotor is not called and physics coasts
        if not unit.on_path:
            self.maintain_position(unit)
            unit.repath_asked = False
            unit.cur_max_blocked_speed = FAST_AS_POSSIBLE
            return
        goal_pos, dist, _, _ = self.point_on_path(unit)
        unit.dist_to_goal = dist
        unit.loco_goal = goal_pos
        speed = unit.max_speed
        if blocked and speed > unit.cur_max_blocked_speed:
            speed = unit.cur_max_blocked_speed
            if unit.bump_speed_limit > speed:
                unit.bump_speed_limit = speed
            else:
                unit.bump_speed_limit = max(unit.bump_speed_limit, speed * 0.2) * BUMP_RECOVERY
                unit.bump_speed_limit = min(unit.bump_speed_limit, speed)
            speed = unit.bump_speed_limit * 0.95
        else:
            blocked = False
            if unit.bump_speed_limit < FAST_AS_POSSIBLE:
                unit.bump_speed_limit = max(unit.bump_speed_limit, speed * 0.2) * BUMP_RECOVERY
            speed = min(speed, unit.bump_speed_limit)
        rescued = self.rescue_steer(unit)
        if rescued is not None:
            goal_pos = rescued
        else:
            goal_pos, speed = self.crowd_steer(unit, goal_pos, speed)
            if CROWD_TTC_STEER:
                goal_pos, speed = self.ttc_steer(unit, goal_pos, speed, dist)
        if side_step:
            goal_pos = side_step_goal(unit, goal_pos, side_away)
        blocked = self.locomote(unit, goal_pos, speed, dist, blocked)
        if not blocked and unit.blocked_frames > 1 and not unit.repath_asked:
            unit.blocked_frames = 1
        unit.repath_asked = False
        unit.cur_max_blocked_speed = FAST_AS_POSSIBLE

    def point_on_path(self, unit: Unit) -> tuple[Point, float, float, float]:
        """Path::computePointOnPath without its cache: the closest point, then the next node if the
           line to it is clear, half way on to the one after when well along the leg, else a blend
           by how far off the route the unit is.  The lane offset it adds for a seeded lane is left
           out: every member that has one is steered by crowdSteer, which replaces the point."""
        pf = self.pf
        pts = unit.path.points
        x, y = unit.x, unit.y
        pos_on = pts[-1]
        close = -1
        best = 1e30
        total = along_to_pos = 0.0
        for k in range(len(pts) - 1):
            a, b = pts[k], pts[k + 1]
            seg = math.hypot(b[0] - a[0], b[1] - a[1])
            if seg < 1e-6:
                continue
            dx, dy = (b[0] - a[0]) / seg, (b[1] - a[1]) / seg
            along = dx * (x - a[0]) + dy * (y - a[1])
            if along < 0.0:
                along, p = 0.0, a
            elif along > seg:
                if k + 2 < len(pts):
                    total += seg
                    continue
                along, p = seg, b
            else:
                p = (a[0] + along * dx, a[1] + along * dy)
            d = (x - p[0]) ** 2 + (y - p[1]) ** 2
            if d < best:
                best, close, pos_on, along_to_pos = d, k, p, total + along
            total += seg
        bend_dist, bend_cos = 0.0, 1.0
        if close >= 0:
            a, b = pts[close], pts[close + 1]
            seg = math.hypot(b[0] - a[0], b[1] - a[1])
            dx, dy = (b[0] - a[0]) / seg, (b[1] - a[1]) / seg
            along = max(dx * (x - a[0]) + dy * (y - a[1]), 0.0)
            if close + 2 < len(pts):
                c = pts[close + 2]
                ox, oy = c[0] - b[0], c[1] - b[1]
                ol = math.hypot(ox, oy)
                if ol > 0.001:
                    bend_cos = (dx * ox + dy * oy) / ol
                    bend_dist = max(seg - along, 0.0)
            off = math.sqrt(max((x - a[0]) ** 2 + (y - a[1]) ** 2 - along * along, 0.0))
            k = min(off / (3.0 * CELL), 1.0)
            got = False
            if pf.is_line_passable(unit, unit.pos, steer_check(unit.pos, b), blocked=False, allow_pinched=True):
                pos_on, got = b, True
                try_ahead = along > seg * 0.5
                very_close = seg - along < 1.0
                if very_close:
                    try_ahead = True
                if try_ahead and close + 2 < len(pts):
                    c = pts[close + 2]
                    t = ((b[0] + c[0]) * 0.5, (b[1] + c[1]) * 0.5)
                    if very_close or pf.is_line_passable(unit, unit.pos, steer_check(unit.pos, t),
                                                         blocked=False, allow_pinched=True):
                        pos_on = t
            elif k > 0.5:
                t_along = along + 0.5 * (seg - along)
                p = (a[0] + t_along * dx, a[1] + t_along * dy)
                pos_on = p
                if pf.is_line_passable(unit, unit.pos, steer_check(unit.pos, p), blocked=False, allow_pinched=True):
                    k, got = 0.5, True
            along += (1.0 - k) * (seg - along)
            if not got:
                if along > seg:
                    pos_on = b
                else:
                    pos_on = (a[0] + along * dx, a[1] + along * dy)
                    if abs(x - pos_on[0]) < 1 and abs(y - pos_on[1]) < 1 and close + 2 < len(pts):
                        pos_on = pts[close + 2]
        dist = total - along_to_pos
        off_by = math.hypot(pos_on[0] - x, pos_on[1] - y)
        if off_by > dist and dist > PATHFIND_CLOSE_ENOUGH:
            dist = off_by
        return pos_on, dist, bend_dist, bend_cos

    # ---- the locomotor and the physics under it ---------------------------------------------
    def apply_motive(self, unit: Unit, accel_x: float, accel_y: float) -> None:
        """PhysicsBehavior::applyMotiveForce, over the mass, which every caller multiplied in: taken
           whole, and the body counts as driven for MOTIVE_FRAMES (no forward friction)."""
        unit.ax += accel_x
        unit.ay += accel_y
        unit.motive_until = self.frame + MOTIVE_FRAMES

    def drive_along_heading(self, unit: Unit, speed_delta: float, acceleration: float) -> None:
        """The drives' "maintain goal speed": the force asked, never more than the speed it closes."""
        if speed_delta == 0.0:
            return
        if abs(acceleration) > abs(speed_delta):
            acceleration = speed_delta
        dx, dy = unit.dir
        self.apply_motive(unit, acceleration * dx, acceleration * dy)

    def locomote(self, unit: Unit, goal: Point, speed: float, dist: float, blocked: bool) -> bool:
        """Locomotor::locoUpdate_moveTowardsPosition (Locomotor.cpp:1249) for a ground unit: the
           braking latch off at a distance, the blocked scrub and turn, the drive its Appearance
           names, then the braking slide onto the goal (1443-1486).  Returns the blocked flag back."""
        kind = unit.kind
        desired = min(speed, unit.max_speed)
        if dist > CELL and dist > (unit.max_speed / kind.braking) * unit.max_speed / 2.0:
            unit.is_braking = False
            unit.braking_factor = 1.0
        dx, dy = goal[0] - unit.x, goal[1] - unit.y
        straight = math.hypot(dx, dy)
        if straight > dist:
            if straight > 2.0 * dist:
                unit.is_braking = True
            dist = straight
        self.apply_motive(unit, 0.0, 0.0)
        if blocked and desired > math.hypot(unit.vx, unit.vy):
            blocked = False
        if blocked:
            scrub_velocity(unit, desired)
            return rotate_around_pivot(unit, goal, unit.turn_rate)[0] != TURN_NONE
        was_braking = unit.braking_status
        if kind.appearance == TWO_LEGS:
            self.move_legs(unit, goal, dist, desired)
        elif kind.appearance in (FOUR_WHEELS, MOTORCYCLE):
            self.move_wheels(unit, goal, dist, desired)
        else:
            self.move_treads(unit, goal, dist, desired)
        unit.braking_status = unit.is_braking
        if kind.appearance in GROUND_DRIVE:
            was_braking = unit.is_braking
        if was_braking and straight > 0.001:
            slide = abs(retail_forward_speed(unit) if kind.appearance in GROUND_DRIVE else forward_speed(unit))
            slide = min(max(slide, BRAKING_SLIDE_MIN), straight)
            unit.x += dx / straight * slide
            unit.y += dy / straight * slide
        return False

    def move_treads(self, unit: Unit, goal: Point, dist: float, desired: float) -> None:
        """Locomotor::moveTowardsPositionTreads (Locomotor.cpp:1497)."""
        braking = unit.kind.braking
        _, rel = rotate_around_pivot(unit, goal, unit.turn_rate)
        angle_coeff = min(abs(rel) / TREAD_SLOW_ANGLE, 1.0)
        roll_coeff = min(abs(rel) / TREAD_PIVOT_ANGLE, 1.0)
        goal_speed = (1.0 - roll_coeff) * desired
        actual = retail_forward_speed(unit)
        slow_down_dist = (actual / SLOW_DOWN_SPEED_DIVISOR) * (actual / braking)
        if ((unit.x - goal[0]) ** 2 + (unit.y - goal[1]) ** 2 < TREAD_NEAR_GOAL ** 2
                and angle_coeff > TREAD_NEAR_ANGLE_COEFF):
            goal_speed = actual * TREAD_NEAR_SPEED_FACTOR
        if dist < slow_down_dist and not unit.is_braking:
            unit.is_braking = True
            unit.braking_factor = BRAKING_FACTOR_LATCHED
        if dist > CELL and dist > 2.0 * slow_down_dist:
            unit.is_braking = False
        if unit.is_braking:
            factor = (slow_down_dist / dist) ** 2 if dist > 0.0 else MAX_BRAKING_FACTOR
            unit.braking_factor = min(factor, MAX_BRAKING_FACTOR)
            goal_speed = braked_speed(actual, braking, slow_down_dist, dist)
        delta = goal_speed - actual
        self.drive_along_heading(unit, delta, unit.acceleration if delta > 0.0 else -unit.braking_factor * braking)

    def move_wheels(self, unit: Unit, goal: Point, dist: float, desired: float) -> None:
        """Locomotor::moveTowardsPositionWheels (Locomotor.cpp:1633): turns only while moving, at
           MinTurnSpeed through a bend, backs up to a goal behind from rest, and stops to turn on the
           spot where the arc it would drive runs onto rock."""
        kind = unit.kind
        braking = kind.braking
        max_turn = unit.turn_rate
        turn_speed = max(kind.min_turn_speed, unit.max_speed * WHEEL_TURN_SPEED_SHARE)
        desired_angle = math.atan2(goal[1] - unit.y, goal[0] - unit.x)
        rel = normalize_angle(desired_angle - unit.facing)
        backwards = three_point = False
        actual = retail_forward_speed(unit)
        if actual == 0.0:
            unit.moving_backwards = False
            if kind.can_move_backwards and abs(rel) > math.pi / 2:
                unit.moving_backwards = True
                unit.three_point_turn = dist > WHEEL_THREE_POINT_RADII * kind.major_radius
        if unit.moving_backwards:
            if abs(rel) < math.pi / 2:
                unit.moving_backwards = False
            else:
                backwards = True
                unit.three_point_turn = dist > WHEEL_THREE_POINT_RADII * kind.major_radius
                three_point = unit.three_point_turn
                if not three_point:
                    desired_angle = normalize_angle(desired_angle - math.pi)
                    rel = normalize_angle(desired_angle - unit.facing)
        if abs(rel) > WHEEL_SMALL_TURN and desired > turn_speed:
            desired = turn_speed
        goal_speed = desired
        if backwards:
            actual = -actual
        slow_down_dist = ((actual / SLOW_DOWN_SPEED_DIVISOR) * (actual / braking + WHEEL_REACTION_FRAMES)
                          + actual * WHEEL_REACTION_FRAMES)
        effective_slow_down_dist = max(slow_down_dist, CELL)
        if abs(rel) > WHEEL_PROBE_TURN:
            mean_speed = (goal_speed + actual) / 2.0
            distance = WHEEL_PROBE_FRAMES * mean_speed
            swing = WHEEL_PROBE_FRAMES * min(mean_speed / turn_speed, 1.0) * max_turn * WHEEL_PROBE_SWING_SHARE
            heading = unit.facing - swing if rel < 0.0 else unit.facing + swing
            off_x, off_y = math.cos(heading) * distance, math.sin(heading) * distance
            if (not self.grid.passable_world(unit.x + off_x / 2.0, unit.y + off_y / 2.0)
                    or not self.grid.passable_world(unit.x + off_x, unit.y + off_y)):
                rotate_around_pivot(unit, goal, max_turn)
                self.apply_motive(unit, 0.0, 0.0)
                return
        if dist < effective_slow_down_dist and not unit.is_braking:
            unit.is_braking = True
            unit.braking_factor = BRAKING_FACTOR_LATCHED
        if dist > CELL and dist > 2.0 * slow_down_dist:
            unit.is_braking = False
        if dist > DONUT_DISTANCE:
            unit.donut_until = self.frame + DONUT_FRAMES
        elif unit.donut_until < self.frame:
            unit.is_braking = True
        if unit.is_braking:
            unit.braking_factor = 1.0
            goal_speed = braked_speed(actual, braking, slow_down_dist, dist)
        turn_amount = min(abs(actual / turn_speed), 1.0) * max_turn
        if backwards and not three_point:
            rotate_around_pivot(unit, (2.0 * unit.x - goal[0], 2.0 * unit.y - goal[1]), turn_amount)
        else:
            rotate_around_pivot(unit, goal, turn_amount)
        if backwards:
            delta = actual - goal_speed
            acceleration = -unit.acceleration if delta < 0.0 else unit.braking_factor * braking
        else:
            delta = goal_speed - actual
            acceleration = unit.acceleration if delta > 0.0 else -unit.braking_factor * braking
        self.drive_along_heading(unit, delta, acceleration)

    def move_legs(self, unit: Unit, goal: Point, dist: float, desired: float) -> None:
        """Locomotor::moveTowardsPositionLegs (Locomotor.cpp:2465): the turn eased over
           INFANTRY_YAW_EASE_FRAMES with the walk carried round the nose, the speed the cosine of what
           is left to turn, and never faster than a stop at the route's end allows.  No braking latch:
           only locoUpdate's own sets it.  WanderWidthFactor is not ported; none of the units has one."""
        kind = unit.kind
        actual = forward_speed(unit)
        rel = normalize_angle(math.atan2(goal[1] - unit.y, goal[0] - unit.x) - unit.facing)
        if abs(goal[0] - unit.x) < ON_TARGET and abs(goal[1] - unit.y) < ON_TARGET:
            rel = 0.0
        turn_rate = unit.turn_rate
        self.turn_vehicle(unit, ease_yaw(unit, rel, turn_rate, turn_rate / INFANTRY_YAW_EASE_FRAMES))
        goal_speed = max(desired * math.cos(rel), 0.0)
        stop_decel = legs_stop_decel(unit)
        goal_speed = min(goal_speed, math.sqrt(2.0 * stop_decel * max(dist, 0.0)))
        goal_speed = max(goal_speed, kind.min_speed)
        delta = goal_speed - actual
        self.drive_along_heading(unit, delta, unit.acceleration if delta > 0.0 else -stop_decel)

    def turn_vehicle(self, unit: Unit, yaw: float) -> None:
        """Locomotor::turnVehicle (Locomotor.cpp:1943): the speed along the nose turns with it."""
        if yaw == 0.0:
            return
        forward = forward_speed(unit)
        before_x, before_y = unit.dir
        turn_around_pivot(unit, yaw)
        after_x, after_y = unit.dir
        self.apply_motive(unit, forward * (after_x - before_x), forward * (after_y - before_y))

    def maintain_position(self, unit: Unit) -> None:
        """locoUpdate_maintainCurrentPosition (Locomotor.cpp:3731): the latch and the slide off; a
           vehicle still driven stops dead (maintainCurrentPositionOther), a soldier brakes (brakeLegs)."""
        unit.is_braking = False
        unit.braking_status = False
        unit.donut_until = self.frame + DONUT_FRAMES
        driven = unit.motive_until > self.frame
        if unit.kind.appearance == TWO_LEGS:
            unit.yaw_rate = 0.0
            if driven:
                scrub_velocity(unit, max(math.hypot(unit.vx, unit.vy) - legs_stop_decel(unit), 0.0))
        elif driven:
            scrub_velocity(unit, 0.0)

    def physics_update(self, unit: Unit) -> None:
        """PhysicsBehavior::update (PhysicsUpdate.cpp:648) on flat ground: friction takes the sideways
           part of the velocity off, and the forward part too once nothing drives the body; the forces
           go into the velocity, a crumb of it is zero, and the position moves unless the locomotor
           slid it itself.  Velocity is not turned with the hull: a tread or a tyre skids."""
        if unit.vx or unit.vy:
            dx, dy = unit.dir
            lateral = unit.vx * -dy + unit.vy * dx
            lateral_share = unit.kind.lateral_friction_per_tick
            unit.ax -= lateral_share * lateral * -dy
            unit.ay -= lateral_share * lateral * dx
            if unit.motive_until <= self.frame:
                forward = unit.vx * dx + unit.vy * dy
                forward_share = unit.kind.forward_friction_per_tick
                unit.ax -= forward_share * forward * dx
                unit.ay -= forward_share * forward * dy
        unit.vx += unit.ax
        unit.vy += unit.ay
        if abs(unit.vx) < VELOCITY_ZERO:
            unit.vx = 0.0
        if abs(unit.vy) < VELOCITY_ZERO:
            unit.vy = 0.0
        if not unit.braking_status:
            unit.x += unit.vx
            unit.y += unit.vy
        unit.ax = unit.ay = 0.0

    # ---- the stuck ladder -------------------------------------------------------------------
    def update_progress(self, unit: Unit) -> None:
        """AIUpdateInterface::updateProgress: under a fifth of the asked speed and not coming about
           is a frame of no progress.  The dither counter is a count only in the C++ and is left out."""
        moved = math.hypot(unit.x - unit.last_progress[0], unit.y - unit.last_progress[1])
        turned = abs(normalize_angle(unit.facing - unit.last_progress_angle))
        unit.last_progress = unit.pos
        unit.last_progress_angle = unit.facing
        if unit.path is None or unit.waiting_for_path or not unit.on_path:
            unit.no_progress = unit.progress_frames = 0
            if not (LADDER_STAGE_HOLD and unit.moving):
                unit.rescue_stage = 0
            return
        asked = unit.max_speed
        bar = max(STUCK_TURN_EPSILON, unit.turn_rate * 0.5)
        coming_about = False
        if turned >= bar:
            gx, gy = unit.loco_goal[0] - unit.x, unit.loco_goal[1] - unit.y
            gl = math.hypot(gx, gy)
            dx, dy = unit.dir
            coming_about = ((dx * gx + dy * gy) / gl) < 0.87 if gl > 0.01 else True
        if moved < asked * 0.2 and not coming_about:
            unit.no_progress += 1
            unit.progress_frames = 0
        else:
            unit.no_progress = 0
            unit.progress_frames += 1
            if unit.progress_frames >= LADDER_STAGE_HOLD:
                unit.rescue_stage = 0

    def stuck_rescue(self, unit: Unit) -> None:
        """AIUpdateInterface::stuckRescue: a new route, then the neighbours asked to move with
           collisions off for a second, then backing out sideways or back."""
        if unit.rescue_until > 0:
            return
        rung = stuck_rung(unit.no_progress, unit.rescue_stage)
        if rung == -1:
            unit.no_progress = unit.rescue_stage = 0
            return
        if rung == 0 or self.frame < unit.rescue_cool:
            return
        unit.rungs_fired[rung - 1] += 1
        if rung == 1:
            unit.rescue_stage = 1
            self.crowd_repath(unit)
            return
        if rung == 2:
            unit.rescue_stage = 2
            reach = unit.radius * 3.0 + CELL
            for other in self.units:
                if other is unit or (LADDER_ALLY_FILTER and other.moving):
                    continue
                if math.hypot(other.x - unit.x, other.y - unit.y) <= reach:
                    self.move_away(other, unit)
            unit.ignore_collisions_until = self.frame + LOGICFRAMES_PER_GAME_SECOND
            unit.rescue_cool = self.frame + LOGICFRAMES_PER_GAME_SECOND
            return
        unit.rescue_stage = 3
        tx, ty = unit.loco_goal[0] - unit.x, unit.loco_goal[1] - unit.y
        tl = math.hypot(tx, ty)
        if tl < 0.01:
            tx, ty = unit.dir
        else:
            tx, ty = tx / tl, ty / tl
        escape = self.find_escape(unit, tx, ty, 1 if unit.id & 1 else -1)
        if escape is not None:
            self.ask_behind_to_back_off(unit, tx, ty)
            unit.escapes += 1
            unit.rescue_to = escape
            unit.ignore_collisions_until = self.frame + LOGICFRAMES_PER_GAME_SECOND
            unit.rescue_until = self.frame + STUCK_BACKOUT_FRAMES
            unit.no_progress = 0
        else:
            self.crowd_repath(unit)

    def find_escape(self, unit: Unit, tx: float, ty: float, first_side: int) -> Point | None:
        """crowdFindEscape, sideways first."""
        across = unit.radius * 2.0 + CELL
        behind = unit.radius + CELL * 0.5
        sx, sy = -ty, tx
        for t in range(4):
            side = -first_side if t == 1 else first_side
            lat = across if t < 3 else 0.0
            back = behind if t < 2 else (0.0 if t == 2 else across)
            p = (unit.x + sx * lat * side - tx * back, unit.y + sy * lat * side - ty * back)
            if self.grid.passable_world(*p):
                return p
        return None

    def ask_behind_to_back_off(self, unit: Unit, tx: float, ty: float) -> None:
        """crowdAskBehindToBackOff: whoever is right behind, in our lane, two bodies back at most."""
        reach = unit.radius * 4.0 + CELL
        for other in self.units:
            if other is unit:
                continue
            dx, dy = other.x - unit.x, other.y - unit.y
            if math.hypot(dx, dy) > reach:
                continue
            if dx * tx + dy * ty > -unit.radius:
                continue
            if abs(-dx * ty + dy * tx) > unit.radius + other.radius:
                continue
            self.move_away(other, unit)

    def rescue_steer(self, unit: Unit) -> Point | None:
        """AIUpdateInterface::rescueSteer."""
        if unit.rescue_until == 0:
            return None
        arrived = math.hypot(unit.rescue_to[0] - unit.x, unit.rescue_to[1] - unit.y) < unit.radius + 2.0
        if arrived or self.frame >= unit.rescue_until:
            unit.rescue_until = 0
            unit.rescue_cool = self.frame + STUCK_COOL_FRAMES
            unit.no_progress = unit.rescue_stage = 0
            self.crowd_repath(unit)
            return None
        return unit.rescue_to

    def crowd_repath(self, unit: Unit) -> None:
        """AIUpdateInterface::crowdRepath: told through the blocked count, so the move state repaths
           from where the unit is with findPath."""
        unit.blocked_frames = 2 * LOGICFRAMES_PER_GAME_SECOND + 1
        unit.repath_asked = True
        unit.repath_pending = LADDER_REPATH_FLAG
        unit.is_blocked_and_stuck = False
        unit.crowd_queued = 0
        unit.no_progress = 0
        unit.rescue_cool = self.frame + STUCK_COOL_FRAMES

    # ---- the crowd model --------------------------------------------------------------------
    def remaining(self, unit: Unit) -> float:
        """Crowd_remaining: along the route when there is one."""
        if unit.path is not None:
            return unit.dist_to_goal
        if unit.goal is None:
            return 0.0
        return math.hypot(unit.goal[0] - unit.x, unit.goal[1] - unit.y)

    def bend_bonus(self, unit: Unit) -> float:
        """Crowd_bendBonus."""
        corr = unit.corridor
        if corr is None or not corr.samples:
            return 0.0
        curv = corr.curvature(unit.crowd_sample)
        if abs(curv) < 0.08:
            return 0.0
        inside = unit.crowd_lat if curv > 0.0 else -unit.crowd_lat
        if inside <= 0.0:
            return 0.0
        return inside * min(abs(curv) * 2.0, 1.0)

    def outranks_me(self, unit: Unit, other: Unit) -> bool:
        """crowdOutranksMe: the bigger body (2 world units of margin), then the less route left, then
           the lower id."""
        if other.radius > unit.radius + 2.0:
            return True
        if unit.radius > other.radius + 2.0:
            return False
        mine = self.remaining(unit) - self.bend_bonus(unit)
        his = self.remaining(other) - self.bend_bonus(other)
        if abs(mine - his) > 1.0:
            return his < mine
        return other.id < unit.id

    def crowd_steer(self, unit: Unit, goal_pos: Point, speed: float) -> tuple[Point, float]:
        """AIUpdateInterface::crowdSteer (AIUpdate.cpp:3205).  A foot soldier reads nobody and rides
           the centre of his band, so every traffic rule stands down for him; a vehicle's scan skips
           infantry, which it drives through."""
        foot = unit.is_infantry
        if unit.path is None:
            return goal_pos, speed
        if not unit.has_pending_crowd_lat and not unit.crowd_lat_valid:
            return goal_pos, speed
        if unit.corridor is None:
            if unit.crowd_sample < 0:
                return goal_pos, speed
            corr = CrowdCorridor()
            if not corr.build(self.grid, unit.radius, unit.path.points):
                unit.crowd_sample = -1
                return goal_pos, speed
            unit.corridor = corr
            unit.crowd_sample = 0
        corr = unit.corridor
        i = corr.nearest(unit.x, unit.y, unit.crowd_sample)
        unit.crowd_sample = i
        here = corr.samples[i]
        my_r = unit.radius
        if corr.length() - here.along < CELL:
            return goal_pos, speed
        if not unit.crowd_lat_valid:
            unit.crowd_lat = unit.pending_crowd_lat if unit.has_pending_crowd_lat else corr.lat_of(i, unit.x, unit.y)
            if not unit.has_pending_crowd_lat:
                unit.crowd_lane_of = 0
            unit.has_pending_crowd_lat = False
            unit.crowd_lat_valid = True
            unit.crowd_side = 1 if here.left >= here.right else -1
        now = self.frame
        pressing = unit.no_progress > STUCK_PRESS_FRAMES
        scan_range = CELL * 4.0 + my_r
        look_ahead = CELL * CROWD_LOOKAHEAD_CELLS + my_r
        scan_now = foot or pressing or unit.crowd_scan is None or (now + unit.id) % CROWD_SCAN_FRAMES == 0
        tx, ty = here.tx, here.ty
        blocker: Unit | None = None
        blocker_fwd = blocker_gap = blocker_speed = 0.0
        blocker_moving = False
        if scan_now:
            sep = 0.0
            give_way, give_way_lat = False, 0.0
            rank = 0
            touching = crossing = False
            joiner: Unit | None = None
            joiner_side = joiner_r = 0.0
            joiner_when = 1.0e9
            near: list[tuple[float, float]] = []
            pack_lo = pack_hi = 0.0
            pack_any = False
            for o in () if foot else self.units:
                if o is unit or o.is_infantry:
                    continue
                dx, dy = o.x - unit.x, o.y - unit.y
                dist = math.hypot(dx, dy)
                if dist > scan_range:
                    continue
                fwd = dx * tx + dy * ty
                side = -dx * ty + dy * tx
                his_r = o.radius
                gap = dist - my_r - his_r
                comfort = my_r + his_r + CROWD_AIR
                his_moving = o.is_moving and math.hypot(o.vx, o.vy) > per_frame_rate(0.05)
                if gap < 0.0:
                    touching = True
                if his_moving and abs(side) > (my_r + his_r) * 0.5:
                    hs = math.hypot(o.vx, o.vy)
                    if hs > per_frame_rate(0.01):
                        align = (o.vx * tx + o.vy * ty) / hs
                        if 0.35 < align < 0.8:
                            rvx, rvy = o.vx - unit.vx, o.vy - unit.vy
                            rvv = rvx * rvx + rvy * rvy
                            if rvv > 1.0e-6 * PER_FRAME_RATE * PER_FRAME_RATE:
                                when = -(dx * rvx + dy * rvy) / rvv
                                if 0.0 <= when < CROWD_MERGE_FRAMES and when < joiner_when:
                                    mx, my = dx + rvx * when, dy + rvy * when
                                    if math.hypot(mx, my) < my_r + his_r + 0.8 and self.outranks_me(unit, o):
                                        joiner, joiner_side, joiner_r, joiner_when = o, side, his_r, when
                if gap < comfort and comfort > 0.01:
                    if dist > 0.01 and abs(side) > 0.01:
                        d = -side / dist
                    elif abs(fwd) < my_r + his_r:
                        d = -1.0 if o.id < unit.id else 1.0
                    else:
                        d = 0.0
                    wgt = (comfort - max(gap, 0.0)) / comfort
                    sep += d * wgt * wgt * comfort
                if not give_way and his_moving and fwd < 0.0 and o.radius > my_r + 2.0 and abs(side) < my_r + his_r + CROWD_AIR:
                    away = my_r + his_r + CROWD_AIR
                    d = (-1.0 if side > 0.0 else 1.0) if abs(side) > 0.01 else float(unit.crowd_side)
                    give_way_lat = corr.lat_of(i, o.x, o.y) + d * away
                    give_way = True
                if -my_r < fwd < look_ahead + his_r:
                    his_lat = corr.lat_of(i, o.x, o.y)
                    if len(near) < CROWD_SCAN_NEAR_MAX:
                        near.append((his_lat, his_r))
                    lo, hi = his_lat - his_r, his_lat + his_r
                    if not pack_any or lo < pack_lo:
                        pack_lo = lo
                    if not pack_any or hi > pack_hi:
                        pack_hi = hi
                    pack_any = True
                if fwd > 0.0 and abs(side) >= my_r + his_r + 1.0 and dist > 0.01:
                    closing = ((unit.vx - o.vx) * dx + (unit.vy - o.vy) * dy) / dist
                    if closing > per_frame_rate(0.05) and gap < closing * CROWD_TTC_FRAMES:
                        crossing = True
                if 0.0 < fwd < look_ahead + his_r and abs(side) < my_r + his_r + 1.0:
                    if blocker is None or fwd < blocker_fwd:
                        blocker, blocker_fwd = o, fwd
                if gap < comfort * 2.0 and self.outranks_me(unit, o):
                    rank += 1
            unit.crowd_scan = dict(sep=sep, blocker=blocker.id if blocker else None,
                                   joiner=joiner.id if joiner else None, joiner_side=joiner_side,
                                   joiner_r=joiner_r, give_way=give_way, give_way_lat=give_way_lat,
                                   rank=rank, touching=touching, crossing=crossing, near=near,
                                   pack_any=pack_any, pack_lo=pack_lo, pack_hi=pack_hi)
        else:
            sc = unit.crowd_scan
            sep, joiner_side, joiner_r = sc["sep"], sc["joiner_side"], sc["joiner_r"]
            give_way, give_way_lat, rank = sc["give_way"], sc["give_way_lat"], sc["rank"]
            touching, crossing, near = sc["touching"], sc["crossing"], sc["near"]
            pack_any, pack_lo, pack_hi = sc["pack_any"], sc["pack_lo"], sc["pack_hi"]
            blocker = self.by_id.get(sc["blocker"]) if sc["blocker"] is not None else None
            joiner = self.by_id.get(sc["joiner"]) if sc["joiner"] is not None else None
        if blocker is not None:
            dx, dy = blocker.x - unit.x, blocker.y - unit.y
            blocker_fwd = dx * tx + dy * ty
            side = -dx * ty + dy * tx
            blocker_gap = math.hypot(dx, dy) - my_r - blocker.radius
            if not (0.0 < blocker_fwd < look_ahead + blocker.radius and abs(side) < my_r + blocker.radius + 1.0):
                blocker = None
        if blocker is not None:
            blocker_moving = blocker.is_moving and math.hypot(blocker.vx, blocker.vy) > per_frame_rate(0.05)
            blocker_speed = max(blocker.vx * tx + blocker.vy * ty, 0.0)
        lat = 0.0 if foot else unit.crowd_lat
        if not foot and now >= unit.crowd_hold_frame and unit.crowd_lane_of > 1 and unit.crowd_lane_space > 0.001:
            span = here.left + here.right
            want = lane_count(span, unit.crowd_lane_space, unit.crowd_lane_of)
            if unit.crowd_fit <= 0 or want < unit.crowd_fit:
                unit.crowd_fit = want
            elif want > unit.crowd_fit and span >= ((want - 1) + 0.5) * unit.crowd_lane_space:
                unit.crowd_fit = want
            fit = unit.crowd_fit
            mine = (unit.crowd_lane_idx * fit) // unit.crowd_lane_of
            bias = (here.left - here.right) * 0.5
            lat = bias + (mine - (fit - 1) * 0.5) * unit.crowd_lane_space
        cap = speed
        queued = merged = False
        if give_way and not pressing:
            lat = give_way_lat
            unit.crowd_hold_frame = now + CROWD_HOLD_FRAMES
        if joiner is not None and not pressing:
            shift = my_r + joiner_r + 0.5
            want = lat - (shift if joiner_side > 0.0 else -shift)
            if abs(corr.clamp_lat(i, want) - want) < 0.5:
                lat = want
                unit.crowd_hold_frame = now + CROWD_HOLD_FRAMES
                merged = True
        if blocker is not None:
            if not self.outranks_me(unit, blocker):
                if blocker_moving and blocker_gap < my_r and blocker_speed < cap and not pressing:
                    cap = blocker_speed
            else:
                his_r = blocker.radius
                his_lat = corr.lat_of(i, blocker.x, blocker.y)
                curv = corr.curvature(i)
                first = (-1 if curv > 0.0 else 1) if abs(curv) > 0.08 else unit.crowd_side
                took = False
                if now >= unit.crowd_hold_frame or unit.crowd_queued > CROWD_PASS_RETRY:
                    clear_shift = my_r + his_r + CROWD_PASS_CLEAR
                    tight_shift = my_r + his_r + CROWD_PASS_TIGHT
                    outside = my_r + CROWD_PASS_TIGHT
                    cands = [his_lat + first * clear_shift, his_lat - first * clear_shift]
                    if pack_any:
                        cands.append(pack_hi + outside if first > 0 else pack_lo - outside)
                        cands.append(pack_lo - outside if first > 0 else pack_hi + outside)
                    cands += [his_lat + first * tight_shift, his_lat - first * tight_shift]
                    for pass_ in range(2):
                        for want in cands:
                            if abs(corr.clamp_lat(i, want) - want) >= 0.5:
                                continue
                            if pass_ == 0 and any(abs(nl - want) < my_r + nr for nl, nr in near):
                                continue
                            lat = want
                            unit.crowd_side = 1 if want >= his_lat else -1
                            unit.crowd_hold_frame = now + CROWD_HOLD_FRAMES
                            took = True
                            break
                        if took:
                            break
                if not took:
                    queued = True
                    if blocker_moving and not pressing:
                        cap = min(cap, brake_speed(speed, blocker_speed, blocker_gap, CROWD_BRAKE_FRAMES))
        if queued:
            unit.crowd_queued += 1
        elif unit.crowd_queued > 0:
            unit.crowd_queued -= 1
        if (unit.crowd_queued > CROWD_REPATH_QUEUE and blocker is not None and not blocker_moving
                and now >= unit.rescue_cool and self.remaining(unit) > CELL * 3.0):
            self.crowd_repath(unit)
        if unit.crowd_queued > CROWD_FAN_FRAMES and not pressing:
            step = min(rank + 1, 2)
            target = unit.crowd_side * (2.0 * my_r + CROWD_PASS_CLEAR) * step
            if target > lat + 0.1:
                lat += CROWD_FAN_RATE
            elif target < lat - 0.1:
                lat -= CROWD_FAN_RATE
        unit.crowd_sep_smooth += (sep - unit.crowd_sep_smooth) * CROWD_SEP_FILTER
        sep = max(-CROWD_SEP_STEP, min(CROWD_SEP_STEP, unit.crowd_sep_smooth))
        lat += per_frame_rate(sep)
        lane_rate = max(speed * 0.25, CROWD_LANE_RATE_MIN)
        move = max(-lane_rate, min(lane_rate, corr.clamp_lat(i, lat) - unit.crowd_lat))
        unit.crowd_lat = corr.clamp_lat(i, unit.crowd_lat + move)
        if crossing:
            cap = min(cap, speed * 0.6)
        my_along = corr.along_of(i, unit.x, unit.y)
        route_len = corr.length()
        want_along = min(my_along + look_ahead, route_len)
        while True:
            out_lat = unit.crowd_lat
            left_to_run = route_len - want_along
            if left_to_run < CROWD_TAPER_DIST:
                out_lat *= left_to_run / CROWD_TAPER_DIST
            goal_pos = corr.point_at(want_along, corr.clamp_lat_narrowest(my_along, want_along, out_lat))
            ahead = (goal_pos[0] - unit.x) * tx + (goal_pos[1] - unit.y) * ty
            if ahead >= my_r or want_along >= route_len:
                break
            want_along = min(want_along + CELL, route_len)
        ax, ay = goal_pos[0] - unit.x, goal_pos[1] - unit.y
        aim_dist = math.hypot(ax, ay)
        if aim_dist > 0.01:
            want_aim = math.atan2(ay, ax)
            if not unit.crowd_aim_valid:
                unit.crowd_aim, unit.crowd_aim_valid = want_aim, True
            err = normalize_angle(want_aim - unit.crowd_aim)
            urgent = (pressing or touching or queued or give_way or merged
                      or (blocker is not None and blocker_gap < my_r) or abs(err) > 1.0)
            unit.crowd_aim = normalize_angle(unit.crowd_aim + err * (CROWD_AIM_URGENT if urgent else CROWD_AIM_CRUISE))
            if not urgent and abs(normalize_angle(unit.crowd_aim - unit.facing)) < CROWD_AIM_DEAD:
                unit.crowd_aim = unit.facing
            goal_pos = (unit.x + math.cos(unit.crowd_aim) * aim_dist, unit.y + math.sin(unit.crowd_aim) * aim_dist)
        if not unit.crowd_cap_valid:
            unit.crowd_cap, unit.crowd_cap_valid = cap, True
        else:
            unit.crowd_cap = release_cap(unit.crowd_cap, cap, CROWD_RELEASE_FILTER)
        return goal_pos, min(speed, unit.crowd_cap)

    def ttc_steer(self, unit: Unit, goal_pos: Point, speed: float, dist: float) -> tuple[Point, float]:
        """Not in C++; it would be an AIUpdateInterface::ttcSteer called in doLocomotor right after
           crowdSteer.  Neighbours in list order, which is id order, so the float sum is fixed."""
        aim_x, aim_y = goal_pos[0] - unit.x, goal_pos[1] - unit.y
        aim_dist = math.hypot(aim_x, aim_y)
        if aim_dist < TTC_MIN_AIM or speed <= TTC_EPSILON:
            return goal_pos, speed
        dir_x, dir_y = aim_x / aim_dist, aim_y / aim_dist
        want_vx, want_vy = dir_x * speed, dir_y * speed
        my_r = unit.radius
        reach = my_r + self.widest_radius + TTC_AIR + TTC_HORIZON_FRAMES * (speed + self.fastest_speed)
        push_x = push_y = 0.0
        for other in self.units:
            if other is unit or (unit.is_infantry and other.is_infantry):
                continue
            to_x, to_y = other.x - unit.x, other.y - unit.y
            if abs(to_x) > reach or abs(to_y) > reach:
                continue
            contact = my_r + other.radius + TTC_AIR
            if not other.moving:
                contact = my_r + other.radius - TTC_PARKED_SLACK
                if (unit.goal is not None and (other.x - unit.goal[0]) ** 2 + (other.y - unit.goal[1]) ** 2
                        < (contact + TTC_SLOT_REACH) ** 2):
                    continue
            slack = to_x * to_x + to_y * to_y - contact * contact
            closing_x, closing_y = want_vx - other.vx, want_vy - other.vy
            if slack < 0.0:
                gap = math.sqrt(to_x * to_x + to_y * to_y)
                if gap < TTC_EPSILON or to_x * dir_x + to_y * dir_y <= 0.0:
                    continue        # the one behind does the yielding
                time_to_contact = 0.0
                away_x, away_y = -to_x / gap, -to_y / gap
            else:
                closing_sq = closing_x * closing_x + closing_y * closing_y
                approach = to_x * closing_x + to_y * closing_y
                if approach <= 0.0 or closing_sq < TTC_EPSILON_FINE:
                    continue
                disc = approach * approach - closing_sq * slack
                if disc <= 0.0:
                    continue
                time_to_contact = (approach - math.sqrt(disc)) / closing_sq
                if time_to_contact > TTC_HORIZON_FRAMES:
                    continue
                away_x = -(to_x - closing_x * time_to_contact)
                away_y = -(to_y - closing_y * time_to_contact)
                away_len = math.hypot(away_x, away_y)
                if away_len < TTC_EPSILON:
                    continue
                away_x, away_y = away_x / away_len, away_y / away_len
            strength = (TTC_STRENGTH * unit.max_speed * math.exp(-time_to_contact / TTC_HORIZON_FRAMES)
                        * (TTC_NEAR_FRAMES / (time_to_contact + TTC_NEAR_FRAMES)) ** 2)
            against = away_x * dir_x + away_y * dir_y
            if against < 0.0:
                away_x += -against * TTC_KEEP_RIGHT * dir_y
                away_y += -against * TTC_KEEP_RIGHT * -dir_x
            push_x += away_x * strength
            push_y += away_y * strength
        if push_x == 0.0 and push_y == 0.0:
            return goal_pos, speed
        if dist < TTC_GOAL_FADE:
            fade = max(dist, 0.0) / TTC_GOAL_FADE
            push_x, push_y = push_x * fade, push_y * fade
        new_vx, new_vy = want_vx + push_x, want_vy + push_y
        new_speed = max(speed * TTC_CREEP, min(speed, new_vx * dir_x + new_vy * dir_y))
        heading = math.atan2(dir_y, dir_x)
        bend = 0.0
        if math.hypot(new_vx, new_vy) > TTC_EPSILON_FINE:
            bend = normalize_angle(math.atan2(new_vy, new_vx) - heading)
        bend = max(-TTC_MAX_BEND, min(TTC_MAX_BEND, bend))
        look = my_r + TTC_WALL_LOOK
        for share in TTC_BEND_TRIES:
            angle = heading + bend * share
            out_x, out_y = math.cos(angle), math.sin(angle)
            if share == 0.0 or all(self.grid.passable_world(unit.x + out_x * look * probe,
                                                            unit.y + out_y * look * probe)
                                   for probe in TTC_WALL_PROBES):
                break
        return (unit.x + out_x * aim_dist, unit.y + out_y * aim_dist), new_speed

    # ---- collisions -------------------------------------------------------------------------
    def collide(self) -> None:
        """PhysicsBehavior::onCollide -> processCollision for every touching pair, both ways, touching
           by the partition's own test (PartitionManager.cpp: two circles, else xy_collideTest_Rect_Rect).
           processCollision returns FALSE for every unit-on-unit case here, so no force is applied:
           bodies may overlap, and the only answer to a collision is the speed cap.  The overlap
           counted for the bench is the bounding circles' depth over touching pairs."""
        units = self.units
        for a, b in itertools.combinations(units, 2):
            distance = math.hypot(b.x - a.x, b.y - a.y)
            if distance > a.radius + b.radius or not bodies_touch(a, b):
                continue
            if distance < a.radius + b.radius:
                self.overlap_ticks += 1
                self.overlap_depth += a.radius + b.radius - distance
            self.process_collision(a, b)
            self.process_collision(b, a)

    def process_collision(self, unit: Unit, other: Unit) -> None:
        """AIUpdateInterface::processCollision for a moving unit (AIUpdate.cpp:1880).  A soldier against
           a vehicle only notes which way is out; a vehicle held up by a soldier on the march creeps at
           a third of its speed, and tells one standing or crossing to step aside."""
        if unit.ignore_collisions_until > self.frame or not unit.is_moving:
            return
        if unit.is_infantry and not other.is_infantry:
            ax, ay = unit.x - other.x, unit.y - other.y
            length = math.hypot(ax, ay)
            if length > SIDE_STEP_MIN:
                unit.side_step_x += ax / length
                unit.side_step_y += ay / length
                unit.side_step_seen = True
                return
        if not self.blocked_by(unit, other):
            return
        unit.is_blocked = True
        if other.is_moving and other.waiting_for_path:
            return
        marching_soldier = (not unit.is_infantry and other.is_infantry and other.is_moving
                            and other.no_progress < STUCK_PRESS_FRAMES)
        if CONTACT_SPEED_CAP:
            allowed = self.max_blocked_speed(unit, other)
            if marching_soldier:
                allowed = max(allowed, unit.max_speed * MARCHING_SOLDIER_CRAWL)
            unit.cur_max_blocked_speed = min(unit.cur_max_blocked_speed, allowed)
        if other.move_away_from != unit.id and other.is_infantry and not unit.is_infantry:
            if not marching_soldier:
                self.move_away(other, unit)
            return
        if unit.blocked_frames == 0:
            unit.blocked_frames = 1
        if self.need_to_rotate(unit):
            unit.blocked_frames = 1
            return
        if not other.is_moving:
            if (unit.crowd_queued > CROWD_FAN_FRAMES * 2 and self.remaining(unit) > CELL * 3.0
                    and not self.outranks_me(unit, other) and other.move_away_from != unit.id):
                self.move_away(other, unit)
                unit.crowd_queued = 0
                return
            unit.is_blocked_and_stuck = True
            return
        if self.blocked_by(other, unit) and not self.need_to_rotate(other):
            if self.outranks_me(unit, other):
                self.move_away(unit, other)

    def blocked_by(self, unit: Unit, other: Unit) -> bool:
        """AIUpdateInterface::blockedBy (AIUpdate.cpp:1670): infantry never blocks infantry, and a
           soldier marching our way does not block a vehicle."""
        if unit.goal is not None and unit.goal_cell is not None and unit.goal_cell[0] > 0 and unit.goal_cell[1] > 0:
            if abs(unit.goal[0] - unit.x) < CELL and abs(unit.goal[1] - unit.y) < CELL:
                return False
        if unit.moving_backwards:
            return False
        other_moving = other.on_path
        dx, dy = unit.x - other.x, unit.y - other.y
        dsq = dx * dx + dy * dy
        (ux, uy), (ox, oy) = unit.dir, other.dir
        dot = ux * ox + uy * oy
        if not unit.is_infantry and other.is_infantry and other_moving and dot > MARCHING_SOLDIER_COS:
            return False
        if unit.is_infantry and other.is_infantry:
            return False
        if dsq < CELL * CELL * 0.0001:
            return self.higher_priority(unit, other)
        if unit.blocked_frames > LOGICFRAMES_PER_GAME_SECOND and dot <= 0.0:
            return False
        if other_moving and dot <= HEAD_ON_DOT:
            unit.head_on_seen = True
            if unit.head_on_frames > HEAD_ON_PASS_FRAMES:
                return False
        collision = normalize_angle(math.atan2(other.y - unit.y, other.x - unit.x) - unit.facing)
        other_angle = normalize_angle(math.atan2(unit.y - other.y, unit.x - other.x) - other.facing)
        limit = math.pi / 4
        if abs(collision) > math.pi / 2:
            return False
        if not other_moving:
            limit *= 0.75
        if abs(collision) > limit:
            if dot <= 0.0:
                return False
            if other_moving and abs(other_angle) > limit:
                dx += ux - ox
                dy += uy - oy
                if dsq > dx * dx + dy * dy:
                    if self.higher_priority(unit, other):
                        return False
                else:
                    return False
            else:
                return False
        return True

    def higher_priority(self, unit: Unit, other: Unit) -> bool:
        """AIUpdateInterface::hasHigherPathPriority: a vehicle over a soldier, then the one in front."""
        if unit.is_infantry != other.is_infantry:
            return other.is_infantry
        (ux, uy), (ox, oy) = unit.dir, other.dir
        if ux * ox + uy * oy <= 0:
            return unit.id < other.id
        dot = (ux + ox) * (other.x - unit.x) + (uy + oy) * (other.y - unit.y)
        if dot > 0:
            return False
        if dot < 0:
            return True
        return unit.id < other.id

    def max_blocked_speed(self, unit: Unit, other: Unit) -> float:
        """AIUpdateInterface::calculateMaxBlockedSpeed: the other's speed along the line to him, over
           how much of our heading points at him."""
        vx, vy = other.x - unit.x, other.y - unit.y
        vl = math.hypot(vx, vy)
        if vl > 0:
            vx, vy = vx / vl, vy / vl
        ox, oy = other.dir
        dot = vx * ox + vy * oy
        if dot < 0:
            return 0.0
        away = math.hypot(other.vx, other.vy) * dot
        ux, uy = unit.dir
        dot = vx * ux + vy * uy
        if dot <= 0:
            return unit.cur_max_blocked_speed
        return min(away / dot, unit.cur_max_blocked_speed)

    def need_to_rotate(self, unit: Unit) -> bool:
        """AIUpdateInterface::needToRotate: more than 6 degrees off the point on the path."""
        if unit.waiting_for_path:
            return True
        if unit.path is None:
            return False
        p = unit.loco_goal
        return abs(normalize_angle(math.atan2(p[1] - unit.y, p[0] - unit.x) - unit.facing)) > math.pi / 30

    @property
    def is_settled(self) -> bool:
        return not any(unit.moving or unit.waiting_for_path for unit in self.units)


def steer_check(frm: Point, to: Point) -> Point:
    """steerCheckTarget: a line check looks no further than 20 cells."""
    max_dist = STEER_CHECK_CELLS * CELL
    dx, dy = to[0] - frm[0], to[1] - frm[1]
    d = math.hypot(dx, dy)
    if d <= max_dist:
        return to
    return frm[0] + dx * max_dist / d, frm[1] + dy * max_dist / d


def corners_inside(a: Unit, b: Unit) -> bool:
    """testRotatedPointsAgainstRect over rectToFourPoints: is a corner of a's rectangle inside b's."""
    a_major, a_minor = a.kind.half_extents
    b_major, b_minor = b.kind.half_extents
    ax, ay = a.dir
    bx, by = b.dir
    for along, across in ((-a_major, a_minor), (a_major, a_minor), (-a_major, -a_minor), (a_major, -a_minor)):
        px = a.x + along * ax - across * ay - b.x
        py = a.y + along * ay + across * ax - b.y
        if abs(px * bx + py * by) <= b_major and abs(-px * by + py * bx) <= b_minor:
            return True
    return False


def bodies_touch(a: Unit, b: Unit) -> bool:
    """The partition's xy collision test: circle on circle when neither is a box, else a corner of
       either rectangle inside the other (a circle as its bounding square)."""
    if a.kind.geometry != "BOX" and b.kind.geometry != "BOX":
        return math.hypot(b.x - a.x, b.y - a.y) <= a.kind.major_radius + b.kind.major_radius
    return corners_inside(a, b) or corners_inside(b, a)


def forward_speed(unit: Unit) -> float:
    """PhysicsBehavior::getForwardSpeed2D: the velocity along the nose."""
    dx, dy = unit.dir
    return unit.vx * dx + unit.vy * dy


def retail_forward_speed(unit: Unit) -> float:
    """calcRetailForwardSpeed (PhysicsUpdate.h:172), EA's measure the tread and wheel drives keep:
       0.707 of the true speed at 45 degrees, so they settle up to 1.41 times Speed off the axes."""
    dx, dy = unit.dir
    along_x, along_y = unit.vx * dx, unit.vy * dy
    speed = math.sqrt(along_x * along_x + along_y * along_y)
    return speed if along_x + along_y >= 0.0 else -speed


def scrub_velocity(unit: Unit, desired: float) -> None:
    """PhysicsBehavior::scrubVelocity2D: no faster than `desired`, a crumb of it stops."""
    if desired < VELOCITY_ZERO:
        unit.vx = unit.vy = 0.0
        return
    current = math.hypot(unit.vx, unit.vy)
    if desired > current:
        return
    unit.vx *= desired / current
    unit.vy *= desired / current


def turn_around_pivot(unit: Unit, amount: float) -> None:
    """Locomotor::turnObjAroundLocoPivot (Locomotor.cpp:3484): about the point TurnPivotOffset
       bounding radii along the nose, which moves the body; about its centre while braking."""
    offset = 0.0 if unit.is_braking else unit.kind.turn_pivot_offset
    if offset != 0.0:
        reach = offset * unit.radius
        dx, dy = unit.dir
        pivot_x, pivot_y = unit.x + dx * reach, unit.y + dy * reach
        unit.facing = normalize_angle(unit.facing + amount)
        dx, dy = unit.dir
        unit.x, unit.y = pivot_x - dx * reach, pivot_y - dy * reach
        return
    unit.facing = normalize_angle(unit.facing + amount)


def rotate_around_pivot(unit: Unit, goal: Point, max_turn: float) -> tuple[int, float]:
    """Locomotor::rotateObjAroundLocoPivot (Locomotor.cpp:3404): onto the goal, at most max_turn,
       instantly.  Returns which way it was still turning and the angle it was off."""
    offset = 0.0 if unit.is_braking else unit.kind.turn_pivot_offset
    from_x, from_y = unit.x, unit.y
    if offset != 0.0:
        dx, dy = unit.dir
        from_x, from_y = unit.x + dx * offset * unit.radius, unit.y + dy * offset * unit.radius
        if abs(goal[0] - from_x) < ON_TARGET and abs(goal[1] - from_y) < ON_TARGET:
            return TURN_NONE, 0.0
    rel = normalize_angle(math.atan2(goal[1] - from_y, goal[0] - from_x) - unit.facing)
    amount, turn = rel, TURN_NONE
    if rel > max_turn:
        amount, turn = max_turn, TURN_POSITIVE
    elif rel < -max_turn:
        amount, turn = -max_turn, TURN_NEGATIVE
    turn_around_pivot(unit, amount)
    return turn, rel


def ease_yaw(unit: Unit, error: float, max_rate: float, yaw_accel: float) -> float:
    """Locomotor::easeYaw (Locomotor.cpp:3091): as fast as max_rate allows and still wind down onto
       the heading, the rate changing by yaw_accel a tick at most."""
    rate = min(math.sqrt(2.0 * yaw_accel * abs(error)), max_rate, abs(error))
    want = -rate if error < 0.0 else rate
    unit.yaw_rate += max(-yaw_accel, min(yaw_accel, want - unit.yaw_rate))
    return unit.yaw_rate


def legs_stop_decel(unit: Unit) -> float:
    """Locomotor::getLegsStopDecel: Braking, or whatever stops him from top speed in INFANTRY_STOP_FRAMES."""
    return max(unit.max_speed / INFANTRY_STOP_FRAMES, unit.kind.braking)


def braked_speed(actual: float, braking: float, slow_down_dist: float, dist: float) -> float:
    """The tread and wheel drives under IS_BRAKING: a full step of Braking off when the stop runs
       past the goal, half a step inside the last quarter, else hold the speed."""
    if slow_down_dist > dist:
        return max(actual - braking, 0.0)
    if slow_down_dist > dist * BRAKING_HALF_SHARE:
        return max(actual - braking / 2.0, 0.0)
    return actual


SIDE_STEP_MIN: float = 0.01
SIDE_STEP_DEAD_SHARE: float = 0.1
SIDE_STEP_OUT_SHARE: float = 0.5


def side_step_goal(unit: Unit, goal: Point, away: Point) -> Point:
    """doLocomotor's side step (AIUpdate.cpp:4380-4414): a soldier touching a vehicle walks along
       its side, never into it, and half a step out from it."""
    length = math.hypot(*away)
    gx, gy = goal[0] - unit.x, goal[1] - unit.y
    dist = math.hypot(gx, gy)
    if length <= SIDE_STEP_MIN or dist <= SIDE_STEP_MIN:
        return goal
    nx, ny = away[0] / length, away[1] / length
    into = gx * nx + gy * ny
    if into < 0.0:
        gx, gy = gx - into * nx, gy - into * ny
        along = math.hypot(gx, gy)
        if along < SIDE_STEP_DEAD_SHARE * dist:
            side = 1.0 if unit.id & 1 else -1.0
            gx, gy, along = -ny * side, nx * side, 1.0
        goal = (unit.x + gx * dist / along, unit.y + gy * dist / along)
    return goal[0] + nx * dist * SIDE_STEP_OUT_SHARE, goal[1] + ny * dist * SIDE_STEP_OUT_SHARE


def clear_crowd_lane(unit: Unit) -> None:
    """crowdClearLanes."""
    unit.has_pending_crowd_lat = False
    unit.crowd_lane_idx = unit.crowd_lane_of = 0
    unit.crowd_lane_space = 0.0
    unit.planned = []


def destroy_path(unit: Unit) -> None:
    """AIUpdateInterface::destroyPath + crowdReleaseCorridor."""
    unit.path = None
    unit.waiting_for_path = False
    unit.corridor = None
    unit.crowd_lat_valid = False
    unit.crowd_sample = 0
    unit.crowd_fit = 0
    unit.crowd_queued = 0
    unit.crowd_hold_frame = 0
    unit.crowd_sep_smooth = 0.0
    unit.crowd_cap = 0.0
    unit.crowd_cap_valid = False
    unit.crowd_aim_valid = False
    unit.crowd_scan = None
    unit.dist_to_goal = 0.0
    if unit.crowd_lane_of >= 1:
        unit.pending_crowd_lat = 0.0
        unit.has_pending_crowd_lat = True


# ---------------------------------------------------------------------------------------------
# Scenarios and measurement
# ---------------------------------------------------------------------------------------------

def spawn_stride(kinds: Sequence[UnitKind]) -> int:
    """Cells between spawn slots, so the widest body in the group clears its neighbours."""
    widest = max((kind.radius for kind in kinds), default=TANK_RADIUS)
    return max(SPAWN_STRIDE_CELLS, math.ceil(2.0 * widest / CELL))


def spawn_block(grid: Grid, kinds: Sequence[UnitKind], cx: int, cy: int, first_id: int = 0) -> list[Unit]:
    """Rows of five from (cx, cy), spawn_stride apart."""
    stride = spawn_stride(kinds)
    units: list[Unit] = []
    row = cy
    while len(units) < len(kinds) and row < grid.height:
        for column in range(min(len(kinds) - len(units), SPAWN_ROW_WIDTH)):
            cell_x = cx + column * stride
            if grid.passable(cell_x, row) and not grid.pinched[row * grid.width + cell_x]:
                units.append(Unit(*cell_pos(cell_x, row, False), id=first_id + len(units) + 1,
                                  kind=kinds[len(units)]))
        row += stride
    return units


def build_scenario(grid: Grid, kinds: Sequence[UnitKind], scenario: str
                   ) -> tuple[list[Unit], list[tuple[list[Unit], Point]]]:
    """`column` is one group across the map; `cross` two groups to each other's corner, which is
       the case the crossing claims are for.  The group is a list of unit kinds."""
    far_x = grid.width - GOAL_INSET_X
    if scenario == "cross":
        half = len(kinds) // 2
        top = spawn_block(grid, kinds[:half], *CROSS_SPAWN_TOP)
        bottom = spawn_block(grid, kinds[len(top):], CROSS_SPAWN_TOP[0],
                             grid.height - CROSS_SPAWN_BOTTOM_INSET, first_id=len(top))
        return top + bottom, [(top, ((far_x + 0.5) * CELL, (grid.height - GOAL_INSET_Y + 0.5) * CELL)),
                              (bottom, ((far_x + 0.5) * CELL, (GOAL_INSET_Y + 0.5) * CELL))]
    units = spawn_block(grid, kinds, *SPAWN_CELL)
    return units, [(units, ((far_x + 0.5) * CELL, (grid.height - GOAL_INSET_Y + 0.5) * CELL))]


@functools.cache
def loaded_game_map(name: str) -> "maploader.LoadedMap":
    """A retail map read from the install once (maploader.load_map)."""
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import maploader
    return maploader.load_map(name)


def game_map_grid(name: str) -> Grid:
    """The retail map's grid, with the engine's own pinched flags rather than pathlab's guess."""
    loaded = loaded_game_map(name)
    grid = Grid(loaded.width, loaded.height)
    grid.kind[:] = loaded.kind
    grid._pinched = bytearray(loaded.pinched)
    return grid


def spawn_around(grid: Grid, kinds: Sequence[UnitKind], centre: Point, first_id: int = 0) -> list[Unit]:
    """spawn_block centred on a world point, a player's start."""
    stride = spawn_stride(kinds)
    rows = -(-len(kinds) // SPAWN_ROW_WIDTH)
    cx, cy = cell_of(*centre)
    return spawn_block(grid, kinds, cx - (SPAWN_ROW_WIDTH - 1) * stride // 2, cy - (rows - 1) * stride // 2,
                       first_id)


def parse_route(text: str) -> tuple[Point, Point] | None:
    """--route AX,AY,BX,BY in cells: the two ends a --gamemap scenario uses instead of the starts."""
    if not text:
        return None
    ax, ay, bx, by = (int(v) for v in text.split(","))
    return cell_pos(ax, ay, True), cell_pos(bx, by, True)


def build_game_scenario(grid: Grid, name: str, kinds: Sequence[UnitKind], scenario: str,
                        route: tuple[Point, Point] | None = None
                        ) -> tuple[list[Unit], list[tuple[list[Unit], Point]]]:
    """On a retail map: `column` takes the group from player 1's start to player 2's (or from one end
       of `route` to the other); `cross` sends half of it each way between the two."""
    first, second = route or loaded_game_map(name).starts[:2]
    if scenario == "cross":
        one = spawn_around(grid, kinds[:len(kinds) // 2], first)
        two = spawn_around(grid, kinds[len(one):], second, first_id=len(one))
        return one + two, [(one, second), (two, first)]
    units = spawn_around(grid, kinds, first)
    return units, [(units, second)]


def scenario_setup(map_kind: str, game_map: str, seed: int, kinds: Sequence[UnitKind], scenario: str,
                   route: tuple[Point, Point] | None = None
                   ) -> tuple[Grid, list[Unit], list[tuple[list[Unit], Point]]]:
    """The grid, the units and their orders: a retail map when `game_map` names one (the seed then
       changes nothing), else a generated `map_kind`."""
    if game_map:
        grid = game_map_grid(game_map)
        return grid, *build_game_scenario(grid, game_map, kinds, scenario, route)
    grid = make_map(map_kind, seed=seed)
    return grid, *build_scenario(grid, kinds, scenario)


def unit_kind(data: "unitdata.UnitData") -> UnitKind:
    """The INI's values per game second, in ticks: SPEED_FIELD's velocity, acceleration and angular
       velocity conversions (GameCommon.h), SpeedScale on both speeds (getMaxSpeedForCondition)."""
    loco = data.locomotor
    per_tick = 1.0 / LOGICFRAMES_PER_GAME_SECOND
    per_tick_squared = per_tick * per_tick
    radians_per_tick = math.pi / 180.0 * per_tick
    return UnitKind(
        name=data.key, appearance=loco.appearance, infantry=data.infantry,
        major_radius=data.major_radius, radius=data.bounding_radius,
        speed=loco.speed * per_tick * data.speed_scale,
        speed_damaged=loco.speed_damaged * per_tick * data.speed_scale,
        turn_rate=loco.turn_rate * radians_per_tick, turn_rate_damaged=loco.turn_rate_damaged * radians_per_tick,
        acceleration=loco.acceleration * per_tick_squared,
        acceleration_damaged=loco.acceleration_damaged * per_tick_squared,
        braking=loco.braking * per_tick_squared, min_speed=loco.min_speed * per_tick,
        min_turn_speed=loco.min_turn_speed * per_tick, turn_pivot_offset=loco.turn_pivot_offset,
        can_move_backwards=loco.can_move_backwards, forward_friction=data.forward_friction,
        lateral_friction=data.lateral_friction, extra_friction=loco.extra_2d_friction,
        close_enough=loco.close_enough_dist, geometry=data.geometry, minor_radius=data.minor_radius)


@functools.cache
def game_kinds() -> dict[str, UnitKind]:
    """Every unit unitdata knows, read once from the install; empty when there is none."""
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import unitdata
    try:
        return {key: unit_kind(data) for key, data in unitdata.load_units().items()}
    except unitdata.InstallMissing as missing:
        print(f"pathlab: {missing}; the stand-in tank drives instead", file=sys.stderr)
        return {}


def default_kind() -> UnitKind:
    """--units' tank: the Crusader, the stand-in's speed with its real body; the stand-in itself
       with no install."""
    return game_kinds().get(DEFAULT_GROUP_KIND, STANDIN_TANK)


def parse_group(spec: str, kinds: dict[str, UnitKind]) -> list[UnitKind]:
    """NAME:COUNT,... into one list, the kinds spread through it in proportion so every row of the
       spawn and both halves of `cross` get a share of each."""
    entries: list[tuple[float, int, UnitKind]] = []
    for order, part in enumerate(p for p in spec.split(",") if p):
        name, _, count = part.partition(":")
        name = name.strip().lower()
        if name not in kinds:
            known = ", ".join(kinds) or "none: no install found"
            raise SystemExit(f"--group: {name!r} is not a unit PathLab can read ({known})")
        number = int(count) if count else 1
        entries += [((k + 0.5) / number, order, kinds[name]) for k in range(number)]
    return [kind for _, _, kind in sorted(entries, key=lambda e: (e[0], e[1]))]


def describe_group(kinds: Sequence[UnitKind]) -> str:
    counts: dict[str, int] = {}
    for kind in kinds:
        counts[kind.name] = counts.get(kind.name, 0) + 1
    return ",".join(f"{name}:{count}" for name, count in counts.items())


@dataclass
class Measurement:
    frames: float = 0.0
    blocked: float = 0.0
    stuck: int = 0
    cells: float = 0.0
    searches: float = 0.0
    repaths: float = 0.0
    escapes: float = 0.0
    waypoints: float = 0.0
    length: float = 0.0
    lanes: float = 0.0
    trunk: float = 0.0


def measure(map_kind: str, kinds: Sequence[UnitKind], seeds: int, scenario: str, first_seed: int = 0,
            game_map: str = "", route: tuple[Point, Point] | None = None) -> Measurement:
    """The scenario's orders played out, averaged over `seeds` maps.  `stuck` stays a count of units
       still short of their goal when the clock ran out."""
    total = Measurement()
    for seed in range(first_seed, first_seed + seeds):
        grid, units, orders = scenario_setup(map_kind, game_map, seed, kinds, scenario, route)
        sim = Simulation(grid, units)
        for group, goal in orders:
            stats = sim.group_move(group, goal)
            total.lanes += stats.lanes / len(orders)
            total.trunk += stats.trunk_diameter / len(orders)
        frames = 0
        while frames < MEASURE_MAX_FRAMES and not sim.is_settled:
            sim.step()
            frames += 1
        total.frames += frames
        total.blocked += sum(u.blocked_total for u in units)
        total.stuck += sum(1 for u in units if u.moving or u.gave_up)
        total.cells += sim.pf.cells_total
        total.searches += sim.pf.searches
        total.repaths += sum(u.repaths for u in units)
        total.escapes += sum(u.escapes for u in units)
        total.waypoints += sum(u.first_waypoints for u in units) / max(len(units), 1)
        total.length += sum(u.first_length for u in units) / max(len(units), 1)
    for name in ("frames", "blocked", "cells", "searches", "repaths", "escapes", "waypoints", "length",
                 "lanes", "trunk"):
        setattr(total, name, getattr(total, name) / seeds)
    return total


def print_head() -> None:
    print(f"{'setting':<24} {'frames':>7} {'blocked':>8} {'stuck':>6} {'cells':>8} {'search':>7} "
          f"{'repath':>7} {'escape':>7} {'waypts':>7} {'length':>7} {'lanes':>6} {'trunk':>6}")


def print_row(label: str, m: Measurement) -> None:
    print(f"{label:<24} {m.frames:>7.0f} {m.blocked:>8.0f} {m.stuck:>6d} {m.cells:>8.0f} "
          f"{m.searches:>7.1f} {m.repaths:>7.1f} {m.escapes:>7.1f} {m.waypoints:>7.1f} "
          f"{m.length:>7.0f} {m.lanes:>6.1f} {m.trunk:>6.1f}")


@contextmanager
def overriding(**values: object):
    """Set module constants for one row and hand them back."""
    module = sys.modules[__name__]
    before = {name: getattr(module, name) for name in values}
    for name, value in values.items():
        setattr(module, name, value)
    try:
        yield
    finally:
        for name, value in before.items():
            setattr(module, name, value)


SWEEPABLE: tuple[str, ...] = (
    "PF_WALLHUG_NUM", "PF_TRAFFIC_NUM", "PF_TRAFFIC_DEPOSIT", "PF_CROSSING_NUM", "PF_CLAIM_UNITS_MAX",
    "PF_HEURISTIC_FLOW_NUM", "PF_TURN_CAP_STEP", "PF_TURN_CAP_START", "PF_CLEARANCE_SOFT",
    "CROWD_PASS_CLEAR", "CROWD_AIR", "CROWD_MAX_LANES", "STUCK_REPATH_FRAMES", "CROWD_REPATH_QUEUE",
    "TTC_HORIZON_FRAMES", "TTC_STRENGTH", "TTC_AIR", "TTC_PARKED_SLACK", "TTC_CREEP", "TTC_MAX_BEND",
    "TTC_GOAL_FADE")


def parse_sweep(text: str) -> tuple[str, list[float]]:
    """NAME=v1,v2,...: one of the game's own constants and the values to try."""
    name, _, values = text.partition("=")
    if name not in SWEEPABLE:
        raise SystemExit(f"--sweep: {name!r} is not one of {', '.join(SWEEPABLE)}")
    kind = type(getattr(sys.modules[__name__], name))
    return name, [kind(v) for v in values.split(",") if v]


def travel_cost(cells: Sequence[Cell]) -> int:
    return sum(COST_DIAGONAL if a[0] != b[0] and a[1] != b[1] else COST_ORTHOGONAL
               for a, b in zip(cells, cells[1:]))


def optimum(grid: Grid, start: Cell, goal: Cell) -> int | None:
    """Plain octile A*, no extras and no cap: the yardstick the game's search is measured against."""
    best = {start: 0}
    heap = [(0, 0, start)]
    while heap:
        _, cost, cell = heapq.heappop(heap)
        if cell == goal:
            return cost
        if cost > best[cell]:
            continue
        flags = [False] * 8
        for i, (dx, dy) in enumerate(DELTAS):
            if i >= 4 and not flags[ADJACENT[i - 4]] and not flags[ADJACENT[i - 3]]:
                continue
            n = (cell[0] + dx, cell[1] + dy)
            if not grid.passable(*n):
                continue
            flags[i] = True
            c = cost + (COST_DIAGONAL if i >= 4 else COST_ORTHOGONAL)
            if c < best.get(n, UNREACHABLE):
                best[n] = c
                dx, dy = abs(goal[0] - n[0]), abs(goal[1] - n[1])
                octile = COST_DIAGONAL * min(dx, dy) + COST_ORTHOGONAL * abs(dx - dy)
                heapq.heappush(heap, (c + octile, c, n))
    return None


def measure_searches(map_kind: str, seeds: int, pairs: int) -> None:
    """Single searches on an empty map: the game's findPath for a tank facing the goal (the goal put
       through adjustDestination first, as an order is), and the group trunk at 6 and 2 cells.
       `excess` is the straightened route's length over the octile optimum, so it can be negative."""
    found = partial = failed = 0
    excess = worst = 0.0
    cells = 0
    ground = {6: [0, 0], 2: [0, 0]}
    searched = 0
    for seed in range(seeds):
        grid = make_map(map_kind, seed=seed)
        rng = random.Random(seed)
        clear = [(x, y) for y in range(grid.height) for x in range(grid.width)
                 if grid.passable(x, y) and not grid.pinched[y * grid.width + x]]
        span = 0.5 * math.hypot(grid.width, grid.height)
        done = 0
        while done < pairs:
            s, g = rng.choice(clear), rng.choice(clear)
            if math.hypot(g[0] - s[0], g[1] - s[1]) < span:
                continue
            done += 1
            best = optimum(grid, s, g)
            if best is None:
                continue
            searched += 1
            sim = Simulation(grid, [])
            probe = Unit(*cell_pos(*s, False), id=1)
            probe.facing = math.atan2(g[1] - s[1], g[0] - s[0])
            before = sim.pf.cells_total
            target = sim.adjust_destination(probe, cell_pos(*g, False))
            path = sim.pf.find_path(probe, probe.pos, target)
            cells += sim.pf.cells_total - before
            if path is None:
                failed += 1
            elif cell_of(*path.last) != cell_of(*target):
                partial += 1
            else:
                found += 1
                length = path.length() / CELL * COST_ORTHOGONAL
                e = 100.0 * (length - best) / max(best, 1)
                excess += e
                worst = max(worst, e)
            for diameter in ground:
                ground[diameter][1] += 1
                ground[diameter][0] += sim.pf.find_ground_path(probe.pos, cell_pos(*g, False), diameter) is not None
    print(f"searches on {map_kind}: {searched} pairs over {seeds} seeds")
    print(f"{'found':>6} {'partial':>8} {'fail':>5} {'excess%':>8} {'worst%':>7} {'cells':>7} "
          f"{'trunk6':>9} {'trunk2':>9}")
    print(f"{found:>6} {partial:>8} {failed:>5} {excess / max(found, 1):>8.2f} {worst:>7.1f} "
          f"{cells / max(searched, 1):>7.0f} {ground[6][0]:>4}/{ground[6][1]:<4} {ground[2][0]:>4}/{ground[2][1]:<4}")


def setting_rows(proposed: bool, ttc: bool) -> list[tuple[str, dict[str, object]]]:
    """The game, then the proposed ladder, TTC steering and both, as the flags ask."""
    rows: list[tuple[str, dict[str, object]]] = [("game", {})]
    if proposed:
        rows.append(("proposed", PROPOSED_LADDER))
    if ttc:
        rows.append(("ttc", PROPOSED_TTC))
    if proposed and ttc:
        rows.append(("proposed+ttc", {**PROPOSED_LADDER, **PROPOSED_TTC}))
    return rows


def run_headless(args: argparse.Namespace) -> None:
    if args.pairs:
        measure_searches(args.map, args.headless, args.pairs)
        if not args.units and not args.group:
            return
    kinds = group_kinds(args)
    print(f"map {args.gamemap or args.map}  scenario {args.scenario}  units {describe_group(kinds)}  seeds {args.headless}  "
          f"{LOGICFRAMES_PER_SECOND} Hz at {GAME_SPEED_PERCENT}%")
    print_head()
    for label, ladder in setting_rows(args.proposed, args.ttc):
        if not args.sweep:
            with overriding(**ladder):
                print_row(label, measure(args.map, kinds, args.headless, args.scenario, game_map=args.gamemap,
                                         route=parse_route(args.route)))
            continue
        name, values = parse_sweep(args.sweep)
        prefix = f"{label} " if ladder else ""
        for value in values:
            with overriding(**ladder, **{name: value}):
                print_row(f"{prefix}{name}={value:g}",
                          measure(args.map, kinds, args.headless, args.scenario, game_map=args.gamemap,
                                  route=parse_route(args.route)))


def group_kinds(args: argparse.Namespace) -> list[UnitKind]:
    """--group's mix, else --units of the default tank."""
    if args.group:
        return parse_group(args.group, game_kinds())
    return [default_kind()] * args.units


# ---------------------------------------------------------------------------------------------
# The window
# ---------------------------------------------------------------------------------------------
BG = (24, 26, 30)
WALL = (58, 62, 70)
OBSTACLE_COL = (88, 76, 64)
PINCHED_COL = (34, 36, 42)
UNIT_COL = (120, 190, 255)
UNIT_SEL = (255, 214, 110)
INFANTRY_COL = (150, 220, 140)
KIND_COLOURS: dict[str, tuple[int, int, int]] = {
    "crusader": (120, 190, 255), "humvee": (90, 230, 230), "overlord": (240, 110, 100),
    "battlemaster": (240, 160, 110), "technical": (235, 200, 90), "scorpion": (200, 150, 80),
    "ranger": (150, 220, 140)}
PATH_COL = (90, 220, 160)
TRUNK_COL = (70, 90, 140)
RAY_OK = (60, 200, 120)
RAY_BAD = (220, 90, 90)
HEAT = (200, 120, 60)
CLAIM_COL = (150, 60, 150)
HUD_TEXT = (200, 205, 215)
HUD_HEIGHT = 60
RENDER_FPS = LOGICFRAMES_PER_SECOND


def run_window(args: argparse.Namespace) -> None:
    import pygame

    seed = args.seed
    kinds = group_kinds(args)

    def new_sim(seed: int) -> tuple[Grid, Simulation]:
        """A generated map waits for a click with everyone selected; a retail map (--gamemap) plays
           the scenario's orders at once and selects no one."""
        if not args.gamemap:
            grid = make_map(args.map, seed=seed)
            sim = Simulation(grid, spawn_block(grid, kinds, *SPAWN_CELL))
            for unit in sim.units:
                unit.is_selected = True
            return grid, sim
        grid, units, orders = scenario_setup(args.map, args.gamemap, seed, kinds, args.scenario,
                                             parse_route(args.route))
        sim = Simulation(grid, units)
        for group, goal in orders:
            sim.group_move(group, goal)
        return grid, sim

    grid, sim = new_sim(seed)
    scale = args.scale
    pygame.init()
    screen = pygame.display.set_mode((int(grid.width * CELL * scale), int(grid.height * CELL * scale) + HUD_HEIGHT))
    pygame.display.set_caption("PathLab")
    font = pygame.font.SysFont("consolas", 14)
    clock = pygame.time.Clock()
    show_rays, show_heat, show_claims, paused = True, False, False, False
    last_goal: Point | None = None
    drag: Point | None = None

    def to_screen(x: float, y: float) -> tuple[int, int]:
        return int(x * scale), int(y * scale)

    def order(goal: Point) -> None:
        nonlocal last_goal
        last_goal = goal
        selected = [u for u in sim.units if u.is_selected] or sim.units
        st = sim.group_move(selected, goal)
        print(f"[order] units={len(selected)} lanes={st.lanes} trunk={st.trunk_diameter} "
              f"searches={sim.pf.searches} cells={sim.pf.cells_total}")

    try:
        while True:
            for event in pygame.event.get():
                if event.type == pygame.QUIT or (event.type == pygame.KEYDOWN and event.key == pygame.K_ESCAPE):
                    return
                if event.type == pygame.KEYDOWN:
                    if event.key == pygame.K_r:
                        show_rays = not show_rays
                    elif event.key == pygame.K_p:
                        show_heat = not show_heat
                    elif event.key == pygame.K_t:
                        show_claims = not show_claims
                    elif event.key == pygame.K_SPACE:
                        paused = not paused
                    elif event.key == pygame.K_n:
                        seed = random.randrange(SEED_RANGE)
                        grid, sim = new_sim(seed)
                    elif event.key == pygame.K_TAB and last_goal:
                        order(last_goal)
                if event.type == pygame.MOUSEBUTTONDOWN:
                    wx, wy = event.pos[0] / scale, event.pos[1] / scale
                    if event.button == 1 and wy < grid.height * CELL:
                        order((wx, wy))
                    elif event.button == 3:
                        drag = (wx, wy)
                if event.type == pygame.MOUSEBUTTONUP and event.button == 3 and drag:
                    x1, y1 = event.pos[0] / scale, event.pos[1] / scale
                    left, right = sorted((drag[0], x1))
                    top, bottom = sorted((drag[1], y1))
                    box = right - left > CELL and bottom - top > CELL
                    for unit in sim.units:
                        unit.is_selected = (left <= unit.x <= right and top <= unit.y <= bottom) if box else True
                    drag = None
            if not paused:
                sim.step()
            screen.fill(BG)
            size = CELL * scale
            w = grid.width
            for cy in range(grid.height):
                for cx in range(w):
                    i = cy * w + cx
                    kind = grid.kind[i]
                    rect = (cx * size, cy * size, size + 1, size + 1)
                    if kind == IMPASSABLE:
                        pygame.draw.rect(screen, WALL, rect)
                    elif kind == OBSTACLE:
                        pygame.draw.rect(screen, OBSTACLE_COL, rect)
                    elif grid.pinched[i]:
                        pygame.draw.rect(screen, PINCHED_COL, rect)
            if show_heat:
                for i in sim.pf.traffic_cells:
                    level = min(sim.pf.traffic[i], PF_TRAFFIC_FULL) / PF_TRAFFIC_FULL
                    pygame.draw.rect(screen, tuple(int(c * level) for c in HEAT),
                                     ((i % w) * size, (i // w) * size, size, size))
            if show_claims:
                bucket = sim.frame // PF_CLAIM_BUCKET_FRAMES
                for (cell, b), weight in sim.pf.claims.items():
                    if b == bucket and weight >= 2:
                        level = min(weight // 2, PF_CLAIM_UNITS_MAX) / PF_CLAIM_UNITS_MAX
                        pygame.draw.rect(screen, tuple(int(c * level) for c in CLAIM_COL),
                                         ((cell % w) * size, (cell // w) * size, size, size), 1)
            if len(sim.stats.trunk) > 1:
                pygame.draw.lines(screen, TRUNK_COL, False, [to_screen(*p) for p in sim.stats.trunk],
                                  max(1, int(sim.stats.trunk_diameter * size / 2)))
            if show_rays:
                for want in (True, False):
                    for unit in sim.units:
                        if unit.is_selected:
                            for ax, ay, bx, by, ok in unit.rays:
                                if ok == want:
                                    pygame.draw.line(screen, RAY_OK if ok else RAY_BAD, to_screen(ax, ay),
                                                     to_screen(bx, by), 1)
            for unit in sim.units:
                if unit.path is not None:
                    pts = [to_screen(unit.x, unit.y)] + [to_screen(*p) for p in unit.path.points[1:]]
                    if len(pts) > 1:
                        pygame.draw.lines(screen, PATH_COL, False, pts, 1)
            for unit in sim.units:
                centre = to_screen(unit.x, unit.y)
                colour = UNIT_SEL if unit.is_selected else KIND_COLOURS.get(
                    unit.kind.name, INFANTRY_COL if unit.is_infantry else UNIT_COL)
                if unit.kind.geometry == "BOX":         # the footprint the collision test uses
                    major, minor = unit.kind.half_extents
                    dx, dy = unit.dir
                    pygame.draw.polygon(screen, colour, [
                        to_screen(unit.x + a * dx - b * dy, unit.y + a * dy + b * dx)
                        for a, b in ((major, minor), (major, -minor), (-major, -minor), (-major, minor))])
                else:
                    pygame.draw.circle(screen, colour, centre, max(2, int(unit.kind.major_radius * scale)))
                tip = to_screen(unit.x + unit.dir[0] * unit.radius, unit.y + unit.dir[1] * unit.radius)
                pygame.draw.line(screen, BG, centre, tip, 2)
                if unit.blocked_frames:
                    pygame.draw.circle(screen, RAY_BAD, centre, int(unit.radius * scale), 1)
            top = int(grid.height * size) + 4
            moving = sum(1 for u in sim.units if u.moving)
            lines = [f"frame {sim.frame}  moving {moving}  blocked {sum(u.blocked_total for u in sim.units)}  "
                     f"repaths {sum(u.repaths for u in sim.units)}  escapes {sum(u.escapes for u in sim.units)}  "
                     f"searches {sim.pf.searches}  cells {sim.pf.cells_total}",
                     f"lanes {sim.stats.lanes}  trunk {sim.stats.trunk_diameter}  rays[r] {show_rays}  "
                     f"traffic[p] {show_heat}  claims[t] {show_claims}  [space] [n] [tab] [esc]"]
            for k, text in enumerate(lines):
                screen.blit(font.render(text, True, HUD_TEXT), (8, top + k * 18))
            pygame.display.flip()
            clock.tick(RENDER_FPS)
    finally:
        pygame.quit()


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--map", choices=MAP_KINDS, default="choke")
    parser.add_argument("--units", type=int, default=8,
                        help=f"a group of this many {DEFAULT_GROUP_KIND}s, read from the install")
    parser.add_argument("--group", default="",
                        help="a mix instead of --units, NAME:COUNT,... e.g. crusader:8,humvee:4,overlord:2; "
                             "the names are unitdata.UNIT_OBJECTS'")
    parser.add_argument("--gamemap", default="", metavar="NAME",
                        help="a retail map from the install instead of --map, e.g. \"Tournament Desert\": "
                             "the group starts at player 1's start and is ordered to player 2's "
                             "(cross: half of it each way)")
    parser.add_argument("--route", default="", metavar="AX,AY,BX,BY",
                        help="with --gamemap: the two ends in cells instead of the first two starts")
    parser.add_argument("--scenario", choices=SCENARIOS, default="column")
    parser.add_argument("--scale", type=float, default=1.0)
    parser.add_argument("--seed", type=int, default=7)
    parser.add_argument("--headless", type=int, metavar="SEEDS",
                        help="no window: play the scenario on SEEDS maps and print the averages")
    parser.add_argument("--sweep", default="",
                        help="headless: NAME=v1,v2,... one of the game's constants, one row per value")
    parser.add_argument("--pairs", type=int, default=0,
                        help="headless: also time this many single searches per seed")
    parser.add_argument("--proposed", action="store_true",
                        help="the stuck ladder's PROPOSED_LADDER values: a second row headless, "
                             "the only behaviour in the window")
    parser.add_argument("--ttc", action="store_true",
                        help="PROPOSED_TTC, steering by time to collision without the contact speed cap: "
                             "another row headless (and one with --proposed), the only behaviour in the window")
    args = parser.parse_args()
    if args.headless:
        run_headless(args)
        return
    _, settings = setting_rows(args.proposed, args.ttc)[-1]
    with overriding(**settings):
        run_window(args)


if __name__ == "__main__":
    main()
