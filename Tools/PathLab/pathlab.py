"""
PathLab - a sandbox for the group-movement work in GameEngine/Source/GameLogic/AI.

The engine's pathfinder is a 10-unit grid, 8-way A*, cost 10 orthogonal / 14 diagonal, with a
congestion map of other units' remaining paths.  Group orders either take one shared corridor split
into lanes (AIGroup::friend_computeGroundPath + friend_moveVehicleToPos) or fall back to one A* per
unit.  All of that is reproduced here at the same numbers, so a change can be looked at - with the
line-of-sight rays every check actually makes drawn on screen - before it is written in C++.

The congestion map has no clock in it, so it cannot tell a shared road from a crossing.  On top of
it there is a reservation map - *when* each unit expects to be in each cell - and a second cost paid
only when two units want the same cell at the same moment.  That is the expensive case: a road two
columns use one after the other is fine, a junction they reach together is not.

    python pathlab.py                 # corridor mode, the engine's default
    python pathlab.py --mode single   # -nogrouppath: one path per unit, congestion the whole way
    python pathlab.py --mode band     # the ribbon: one line, a lateral position each, density
    python pathlab.py --mode flat     # ... and the same band with the pressure term off
    python pathlab.py --map open      # no chokepoint, to see lanes on open ground
    python pathlab.py --headless 6 --scenario cross --map open --units 16 --sweep 20/4/0,20/4/60
    python pathlab.py --headless 16 --map edges --corridor all --pairs 8 --units 0 --hweight 1.5

Two units are only counted as crossing when they want a cell at the same moment *and* are not
going the same way - traffic following the same road is a queue, and a queue is what a road is
for.  Nor is it charged in a cell with no room to dodge: pricing a two-cell doorway does not
spread anyone out, it sends them to queue at the next doorway instead.

Controls
    left click          order the selection to a point
    right drag          select units (right click empty ground selects all)
    1 / 2 / 3           corridor mode / individual mode / band mode (the ribbon)
    c                   congestion cost on/off
    t                   crossing cost on/off (the time dimension)
    r                   draw the line-of-sight rays planning used
    p                   draw the congestion map
    space               pause
    n                   new map, same mode
    tab                 re-issue the last order (replan with what is on the map now)
    esc                 quit

Red squares are cells two units still plan to be in within PATH_CROSSING_WINDOW frames of each
other - the clashes the crossing cost is there to buy out.
"""

import argparse
import heapq
import itertools
import math
import random
import sys
from collections import Counter
from collections.abc import Iterable, Sequence
from contextlib import contextmanager
from dataclasses import dataclass, field
from fractions import Fraction
from functools import partial
from enum import IntEnum
from typing import NamedTuple
import pygame

Cell = tuple[int, int]
Point = tuple[float, float]
Heading = tuple[float, float]
Ray = tuple[float, float, float, float, bool]
Colour = tuple[int, int, int]
Rect = tuple[float, float, float, float]

# Engine constants: keep these equal to the C++ or the sandbox is measuring something else.
CELL: float = 10.0                          # PATHFIND_CELL_SIZE_F
COST_ORTHOGONAL: int = 10
COST_DIAGONAL: int = 14

# AIPathfind.cpp charges 5 per path with a group corridor and 20 without one; the sandbox has no
# corridor congestion at all, so 20 is the value it runs at.  --cost overrides, --sweep compares.
PATH_CONGESTION_COST: int = 20
PATH_CONGESTION_RANGE_NO_GROUP: int = 10000
PATH_CONGESTION_MAX_PATHS: int = 4          # cap, so a mob cannot make a cell infinitely expensive
GROUP_PATH_DIAMETERS: tuple[int, ...] = (6, 4, 3)   # what friend_computeGroundPath tries in order
STRAIGHTEN_MAX_RUN: int = 16                # raycast simplify: points dropped in a row
MAP_KINDS: tuple[str, ...] = ("choke", "open", "twogaps", "maze", "edges")
MODES: tuple[str, ...] = ("corridor", "flat", "band", "single")
SCENARIOS: tuple[str, ...] = ("column", "cross")
UNIT_RADIUS: float = 11.0                   # a Crusader's bounding circle, near enough
UNIT_SPEED: float = 2.4                     # world units per logic frame

# The congestion map above has no clock in it: a cell another unit will drive over in ten seconds
# costs the same as one it is sitting in now.  The reservation map does have a clock - it stores
# *when* each unit expects to be in a cell, and charges only for an overlap in time.  That is what
# makes a crossing expensive without making a shared road expensive.
PATH_CROSSING_COST: int = 60                # per conflicting reservation - six cells of detour
PATH_CROSSING_WINDOW: int = 12              # frames either side of an arrival that still clash
PATH_CROSSING_MAX: int = 3                  # cap, same reason the congestion cost has one
SAME_WAY: float = 0.6                       # headings this aligned are a queue, not a crossing

# Both maps above describe a plan.  The congestion map says somebody's route runs through a cell and
# the reservation map says when, and neither remembers anything that has already happened: a doorway
# four tanks have been wedged in for two seconds is priced exactly like empty ground, because the
# units in it have stopped and stopped units have no route left to claim.  A unit repathing out of
# the back of that queue therefore picks the same doorway, every time, at the instant its search
# runs.
#
# The jam map is the memory.  A blocked unit stamps the cell it is standing in, once a frame; the
# whole map loses one everywhere every JAM_DECAY_FRAMES, so a cell carries roughly the blocked
# unit-frames spent in it over the last JAM_MAX*JAM_DECAY_FRAMES frames.  AIPathfind.cpp's m_jamMap,
# at its constants and with its two gates: only a repath reads it, and only within JAM_RANGE cells
# of where that repath starts.  A search that is not leaving a jam keeps the plain shortest route.
PATH_JAM_COST: int = 3 * COST_ORTHOGONAL    # three cells of detour per stamp over the floor
PATH_JAM_FLOOR: int = 3                     # one unit pausing stamps 1 or 2: that is traffic
PATH_JAM_MAX: int = 10                      # the stamp saturates here
PATH_JAM_DECAY_FRAMES: int = 8              # every cell loses one this often
PATH_JAM_MAX_CHARGED: int = 4               # cap over the floor, same reason the others have one
PATH_JAM_RANGE: int = 10                    # cells from the start the penalty is read within

# The block corridor.  Before the cell search the engine runs a coarse one over ZONE_BLOCK_SIZE
# blocks (internal_findHierarchicalPath), and the blocks on that route plus their eight neighbours
# (buildHierachicalPath) are the corridor the cell search is held to.  How hard it is held differs
# by caller: findGroundPath, the group corridor, refuses a cell outside unless a block 3 cells away
# diagonally is inside (`hard`); internalFindPath, every single unit, charges 100 cells of detour
# per cell outside (`flat`).  `soft` is the graded alternative: free near the coarse route, a price
# that grows with distance beyond a threshold, and a small rebate for following the route forward.
CORRIDOR_MODES: tuple[str, ...] = ("none", "hard", "flat", "soft")
CORRIDOR: str = "none"                      # --corridor; read inside astar like the costs above
ZONE_BLOCK_SIZE: int = 10                   # PathfindZoneManager::ZONE_BLOCK_SIZE
CORRIDOR_WIDEN: int = 1                     # blocks either side of each route block also open
CORRIDOR_SLACK: int = 3                     # findGroundPath's diagonal look for an open block
CORRIDOR_OUTSIDE_COST: int = 100 * COST_ORTHOGONAL      # internalFindPath, per cell outside
CORRIDOR_PINCHED_COST: int = 2 * COST_ORTHOGONAL        # coarse step through a pinched edge cell
SOFT_COST: int = 100                        # per cell of distance beyond the threshold, per cell
SOFT_THRESHOLD: int = 10                    # cells from the coarse route that cost nothing
SOFT_NEAR: int = 3                          # this close and moving along it forward ...
SOFT_BONUS: int = 5                         # ... takes this off the step
ENGINE_CELL_CAP: int = 20000                # internalFindPath's MAX_INTERNAL_CELL_COUNT
GROUND_CELL_CAP: int = 3000                 # findGroundPath's MAX_GROUND_PATH_CELLS
# A* estimate scale.  The sandbox has always searched at plain octile (1/1); retail scales its own
# estimate by 3/2, which finds a route a few percent longer for far fewer cells.
HEURISTIC_NUM: int = 1
HEURISTIC_DEN: int = 1

# Two points closer than this are the same point: a zero-length segment has no direction to take.
EPSILON: float = 1e-3
# Larger than any route this grid can produce, so it reads as "not reached yet" in the A* tables.
UNREACHABLE_COST: int = 1 << 30
DEFAULT_CELL_BUDGET: int = 60000            # the engine's "out of pathfind cells" ceiling
SEED_RANGE: int = 1 << 30

# Terrain
MAP_WIDTH_CELLS: int = 96
MAP_HEIGHT_CELLS: int = 72
ROCK_RADIUS_CELLS: int = 2                  # rocks are five cells across
OPEN_MAP_ROCKS: int = 18
OPEN_MAP_MARGIN: int = 8
CHOKE_MAP_ROCKS: int = 10
CHOKE_MAP_MARGIN: int = 6
CHOKE_GAP_CELLS: int = 4                    # narrower than three lanes of tanks
SCATTER_KEEP_CLEAR_CELLS: int = 6           # a rock this near the wall column would seal its gap
NARROW_GAP_HALF_CELLS: int = 2
WIDE_GAP_ROWS: tuple[int, int] = (6, 13)
# The two layouts made for the block corridor, both larger than the rest so a search can be long
# enough for the abstraction to matter.  A maze is passages on a lattice that does not line up with
# the blocks, with some walls knocked out so there is more than one way; `edges` is a row of thin
# walls standing on block boundaries, each with a narrow gap and a wide one, every gap straddling a
# block boundary, so the coarse route (which ignores footprint) picks gaps a group cannot use.
MAZE_SIZE: tuple[int, int] = (160, 120)
MAZE_PITCH: int = 6                         # lattice step: a 4-cell passage and a 2-cell wall
MAZE_PASSAGE: int = 4                       # one tank fits, two abreast do not
MAZE_LOOPS: float = 0.15                    # share of the remaining walls knocked through
MAZE_ROOM: tuple[int, int] = (24, 26)       # cleared in each corner, for the spawns and the goals
EDGES_SIZE: tuple[int, int] = (128, 96)
EDGES_WALL_EVERY: int = 2                   # a wall on every second block boundary ...
EDGES_FIRST_WALL: int = 2                   # ... starting at this one
EDGES_NARROW: int = 2                       # cells: a single unit passes, no group corridor does
EDGES_WIDE: int = 8                         # cells: the 6-wide group corridor fits
RAYCAST_STEP: float = CELL * 0.5            # isLinePassable samples twice per cell
ROOM_CLEARANCE: int = 1                     # a clear ring one cell deep is "somewhere to dodge to"

# Units and collision
COLLISION_SLACK: float = 0.95               # units may touch this close before they count as one
GOAL_SPACING_SLACK: float = 0.95
GOAL_SEARCH_RINGS: int = 24
YIELD_REACH: float = 1.3                    # radii within which a parked unit is in the way
FINAL_ARRIVE_RADII: float = 0.9             # the last waypoint is reached this loosely ...
WAYPOINT_ARRIVE_RADII: float = 0.25         # ... and the ones on the way, this tightly
PROGRESS_EPSILON_SPEEDS: float = 0.25       # closing by less than this is inching, not progress
REPATH_AFTER_STUCK_FRAMES: int = 60
PARK_RADII: float = 3.0                     # jammed this near the goal counts as arrived ...
PARK_RADII_AFTER_RETRIES: float = 6.0       # ... and the tolerance grows the longer it fails
PARK_RETRY_LIMIT: int = 3
# (turn in radians, fraction of full speed) tried in order: straight, crawl, then wider swerves
SWERVE_ATTEMPTS: tuple[tuple[float, float], ...] = (
    (0.0, 1.0), (0.0, 0.4), (0.6, 0.8), (-0.6, 0.8), (1.2, 0.6), (-1.2, 0.6))

# Lane changing.  The engine's only answer to being blocked is to go slower, and a swerve that
# lasts one frame is not an overtake - the unit re-aims at its waypoint next frame and files back
# in behind the same tank.  A lane is a sideways shift of the goal that is held long enough to get
# past somebody, and it is only taken when there is a lane to take: the probe below is the
# sandbox's isLinePassable, drawn like every other check this file makes.
LANE_CHANGE: bool = False                   # --lanes
LANE_CHANGE_AFTER_FRAMES: int = 15          # blocked this long before looking for a way round
LANE_CHANGE_OFFSETS: tuple[float, ...] = (1.0, -1.0, 2.0, -2.0)     # footprints, nearest side first
LANE_CHANGE_LOOKAHEAD: float = 6.0          # footprints of clear line a lane has to have
LANE_CHANGE_REACH: float = 2.5              # radii within which somebody counts as "in front"
LANE_CHANGE_AHEAD: float = 0.5              # how far off our nose he may be and still be in the way
LANE_CHANGE_SAME_WAY: float = 0.5           # headings this aligned are traffic, not a head-on
LANE_HOLD_FRAMES: int = 45                  # hold a lane this long, or it flickers every frame
LANE_CHANGE_STALLED_ONLY: bool = True       # overtake a queue that is stopped, not one that flows
LANE_RAYS_KEPT: int = 24                    # how many lane probes stay drawable per unit

# The ribbon.  The corridor stops being a line and becomes a band: every unit keeps a continuous
# lateral position inside it, clamped to the band's half width, and each frame slides that position
# away from whichever side of the band in front of it is busier.  A queue is a density hump and the
# units behind it drain sideways on their own; where the band is too narrow to hold two of them the
# clamp gathers them back into single file, which is the doorway case Grid.has_room exists for,
# expressed as geometry rather than as a special case.
#
# It shares the corridor search with column mode and adds no search of its own: the per-frame work
# is counting units in a slice in front, in buckets a footprint wide, and one clearance probe.
BAND_STEP: float = 0.15                     # footprints of lateral drift per frame
BAND_LOOKAHEAD: float = 4.0                 # footprints of band read in front of the unit
BAND_BUCKET: float = 1.0                    # footprints per bucket: a cell is 10 and a tank 22, so
                                            # a narrower one measures noise and the drift chatters
BAND_DEADBAND: int = 1                      # a side must be this much busier before anybody moves

# Ordering
LANES_MAX: int = 3
LANES_MIN: int = 2
LANES_MIN_UNITS: int = 5                    # below this, three lanes are wider than the group
LANE_GAP: float = 0.5 * CELL                # slack between neighbouring lanes
LANE_SHRINKS: tuple[float, ...] = (1.0, 0.5, 0.0)   # full offset, half, then the corridor line
INDIVIDUAL_OFFSET_RADII: float = 6.0        # computeIndividualDestination's cap on the spread
SPAWN_ROW_WIDTH: int = 5
SPAWN_STRIDE_CELLS: int = 3
SPAWN_CELL: Cell = (6, 12)                  # the column scenario's single block
CROSS_SPAWN_TOP: Cell = (6, 10)             # the cross scenario's high block ...
CROSS_SPAWN_BOTTOM_INSET: int = 16          # ... and the low one, this far up from the bottom
GOAL_INSET_X: int = 8                       # goals sit this far in from the right edge ...
GOAL_INSET_Y: int = 12                      # ... and this far from the top or bottom

# Measurement and window
MEASURE_MAX_FRAMES: int = 4000
RENDER_FPS: int = 60
HUD_HEIGHT: int = 96
HUD_TOP_PAD: int = 4
HUD_TEXT_X: int = 8
HUD_LINE_HEIGHT: int = 18
HUD_FONT_SIZE: int = 14
HUD_NOTES_SHOWN: int = 2
WAYPOINT_DOT: int = 2
BLOCKED_RING_WIDTH: int = 1
CROSSING_OUTLINE_WIDTH: int = 1
TERRAIN_OVERDRAW: int = 1                   # one pixel, so neighbouring wall cells do not seam
MOUSE_LEFT: int = 1
MOUSE_RIGHT: int = 3


def frames_for_cost(cost: float) -> float:
    """A* cost back to logic frames: 10 of cost is one cell, one cell is CELL/UNIT_SPEED frames."""
    return (cost / float(COST_ORTHOGONAL)) * (CELL / UNIT_SPEED)


def cell_of(x: float, y: float) -> Cell:
    return int(x // CELL), int(y // CELL)


def world_of(cx: int, cy: int) -> Point:
    return (cx + 0.5) * CELL, (cy + 0.5) * CELL


class Grid:
    def __init__(self, width: int, height: int) -> None:
        self.width = width
        self.height = height
        self.blocked = bytearray(width * height)
        self._room: bytearray | None = None
        self._zones: dict[int, list[int]] = {}
        self._clear: dict[int, bytearray] = {}

    def in_bounds(self, cx: int, cy: int) -> bool:
        return 0 <= cx < self.width and 0 <= cy < self.height

    def _changed(self) -> None:
        self._room = None
        self._zones = {}
        self._clear = {}

    def block(self, cx: int, cy: int) -> None:
        self.blocked[cy * self.width + cx] = 1
        self._changed()

    def unblock(self, cx: int, cy: int) -> None:
        self.blocked[cy * self.width + cx] = 0
        self._changed()

    def clear_cells(self, clearance: int) -> bytearray:
        """clear_for over the whole map, once per clearance: a search asks it eight times a cell."""
        if clearance not in self._clear:
            self._clear[clearance] = bytearray(
                1 if self.clear_for(x, y, clearance) else 0
                for y in range(self.height) for x in range(self.width))
        return self._clear[clearance]

    def open_span(self, cx: int, first_row: int, last_row: int) -> None:
        """Cut a two-cell-wide gap through a wall at column cx, rows first_row..last_row."""
        for y in range(first_row, last_row + 1):
            self.unblock(cx, y)
            self.unblock(cx + 1, y)

    def block_zones(self, size: int) -> list[int]:
        """PathfindZoneManager's raw zones: each clear cell labelled with its 4-connected component
           inside its own size x size block, numbered across the whole map; 0 is a wall.  A block
           whose clear ground is split by a wall is two zones, so the coarse search never sees a
           block as connected when its interior is not."""
        if size in self._zones:
            return self._zones[size]
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
        self._zones[size] = zones
        return zones

    def has_room(self, cx: int, cy: int) -> bool:
        """True when the whole ring around this cell is clear - somewhere to dodge to.  In a
           two-cell doorway there is nowhere to go, so charging a crossing there does not spread
           anyone out, it just sends them to the next doorway to queue at that one instead."""
        if not self.in_bounds(cx, cy):
            return False
        if self._room is None:
            self._room = bytearray(
                1 if self.clear_for(x, y, ROOM_CLEARANCE) else 0
                for y in range(self.height) for x in range(self.width))
        return bool(self._room[cy * self.width + cx])

    def passable(self, cx: int, cy: int) -> bool:
        return self.in_bounds(cx, cy) and not self.blocked[cy * self.width + cx]

    def passable_world(self, x: float, y: float) -> bool:
        return self.passable(*cell_of(x, y))

    def clear_for(self, cx: int, cy: int, clearance: int) -> bool:
        """Every cell within `clearance` of this one is passable - the engine's path diameter."""
        return all(self.passable(x, y)
                   for x in range(cx - clearance, cx + clearance + 1)
                   for y in range(cy - clearance, cy + clearance + 1))

    def line_passable(self, ax: float, ay: float, bx: float, by: float, *,
                      radius: float = 0.0) -> bool:
        """Pathfinder::isLinePassable - walk the line, every cell on it has to be drivable."""
        dx, dy = bx - ax, by - ay
        distance = math.hypot(dx, dy)
        if distance < EPSILON:
            return self.passable_world(ax, ay)
        steps = int(distance / RAYCAST_STEP) + 1
        nx, ny = -dy / distance, dx / distance
        for step in range(steps + 1):
            fraction = step / steps
            x, y = ax + dx * fraction, ay + dy * fraction
            if not self.passable_world(x, y):
                return False
            if radius > 0.0 and not (self.passable_world(x + nx * radius, y + ny * radius)
                                     and self.passable_world(x - nx * radius, y - ny * radius)):
                return False
        return True


def scatter_blocks(grid: Grid, rng: random.Random, count: int, margin: int,
                   keep_clear_of: int | None = None) -> None:
    """`count` five-by-five rocks at random, never within `margin` of the edge and never straddling
       the column a map wants left open."""
    for _ in range(count):
        cx = rng.randrange(margin, grid.width - margin)
        cy = rng.randrange(margin, grid.height - margin)
        if keep_clear_of is not None and abs(cx - keep_clear_of) < SCATTER_KEEP_CLEAR_CELLS:
            continue
        for x in range(cx - ROCK_RADIUS_CELLS, cx + ROCK_RADIUS_CELLS + 1):
            for y in range(cy - ROCK_RADIUS_CELLS, cy + ROCK_RADIUS_CELLS + 1):
                grid.block(x, y)


def carve_maze(grid: Grid, rng: random.Random) -> None:
    """Fill the map and carve a lattice maze into it: depth-first, so one way through, then a share
       of the walls left standing knocked out, so there are several and the shortest is not the
       obvious one."""
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
        return grid
    if kind == "choke":
        # one wall across the middle with a gap the group cannot cross abreast
        wall = width // 2
        gap_y = height // 2
        for y in range(1, height - 1):
            if abs(y - gap_y) * 2 > CHOKE_GAP_CELLS:
                grid.block(wall, y)
                grid.block(wall + 1, y)
        scatter_blocks(grid, rng, CHOKE_MAP_ROCKS, CHOKE_MAP_MARGIN, keep_clear_of=wall)
        return grid
    if kind == "twogaps":
        # the case the whole exercise is about: a cheap narrow way and a wider way round.
        # Nothing here reads the seed, on purpose - it is a hand-made map, so a headless run over
        # sixteen seeds is one match played sixteen times and its columns are one data point
        wall = width // 2
        for y in range(1, height - 1):
            grid.block(wall, y)
            grid.block(wall + 1, y)
        grid.open_span(wall, height // 2 - NARROW_GAP_HALF_CELLS,
                       height // 2 + NARROW_GAP_HALF_CELLS)
        grid.open_span(wall, *WIDE_GAP_ROWS)
        return grid
    if kind == "maze":
        carve_maze(grid, rng)
        return grid
    if kind == "edges":
        size = ZONE_BLOCK_SIZE
        boundaries = range(1, (height - 1) // size)
        for boundary in range(EDGES_FIRST_WALL, (width - 1) // size - 1, EDGES_WALL_EVERY):
            wall = boundary * size - 1          # the last column of one block, the first of the next
            for y in range(1, height - 1):
                grid.block(wall, y)
                grid.block(wall + 1, y)
            narrow, wide = rng.sample(boundaries, 2)
            for row, gap in ((narrow, EDGES_NARROW), (wide, EDGES_WIDE)):
                first = row * size - gap // 2
                grid.open_span(wall, max(1, first), min(height - 2, first + gap - 1))
        return grid
    raise ValueError(f"unknown map kind {kind!r}, expected one of {', '.join(MAP_KINDS)}")


class UsageMap:
    """Cells covered by units' *remaining* paths.  A unit releases what it has driven past.
       Pathfinder::registerPathUsage / releasePathUsageBefore."""

    def __init__(self) -> None:
        self.count: dict[Cell, int] = {}
        self.owner: dict[int, list[Cell]] = {}

    def register(self, owner: int, cells: Sequence[Cell]) -> None:
        self.unregister(owner)
        self.owner[owner] = list(cells)
        for cell in cells:
            self.count[cell] = self.count.get(cell, 0) + 1

    def _drop_cells(self, cells: Iterable[Cell]) -> None:
        """Every cell here was counted by register, so the count is there to be decremented."""
        for cell in cells:
            remaining = self.count[cell] - 1
            if remaining <= 0:
                del self.count[cell]
            else:
                self.count[cell] = remaining

    def unregister(self, owner: int) -> None:
        self._drop_cells(self.owner.get(owner, ()))
        # the key stays, empty: a unit whose repath failed still gets released as it drives on
        self.owner[owner] = []

    def release_before(self, owner: int, index: int) -> None:
        cells = self.owner[owner]
        self._drop_cells(cells[:index])
        self.owner[owner] = cells[index:]

    def count_paths_on(self, cell: Cell, exclude_owner: int | None = None) -> int:
        paths = self.count.get(cell, 0)
        if paths and exclude_owner is not None and cell in self.owner.get(exclude_owner, ()):
            paths -= 1
        return paths


def price_congestion(paths: int) -> int:
    """One orthogonal step costs 10, so a cost of 5 buys at most half a cell of detour per cell -
       which is why the default barely bends a path.  This is the knob to turn."""
    if paths <= 0:
        return 0
    return min(paths, PATH_CONGESTION_MAX_PATHS) * PATH_CONGESTION_COST


def count_path_overlap(units: Sequence["Unit"]) -> int:
    """Cells claimed by more than one unit at plan time - 'ayni yerde ayni anda', counted."""
    claims = Counter(cell for unit in units for cell in unit.cells)
    return sum(claimants - 1 for claimants in claims.values() if claimants > 1)


class JamMap:
    """Where units actually got stuck, as opposed to where they said they would drive.
       Pathfinder::noteJam / decayJamMap, on a dict because the sandbox has no map array.

       The decay runs off the frame number rather than a call count, which is what makes it
       identical on two machines in the engine and reproducible between two sweep rows here."""

    def __init__(self) -> None:
        self.jam: dict[Cell, int] = {}
        self.decayed_at: int = 0
        self.peak: int = 0              # most cells jammed at once, which no engine counter reports

    def note(self, cell: Cell) -> None:
        stamp = self.jam.get(cell, 0)
        if stamp < PATH_JAM_MAX:
            self.jam[cell] = stamp + 1
        self.peak = max(self.peak, len(self.jam))

    def at(self, cell: Cell) -> int:
        return self.jam.get(cell, 0)

    def decay(self, frame: int) -> None:
        """A match with nothing jammed anywhere - which is most of a match - touches nothing."""
        if not self.jam or frame - self.decayed_at < PATH_JAM_DECAY_FRAMES:
            return
        self.decayed_at = frame
        self.jam = {cell: stamp - 1 for cell, stamp in self.jam.items() if stamp > 1}

    @property
    def blocking_cells(self) -> int:
        """Cells jammed hard enough to cost a repath anything, which is the number that matters -
           a map full of stamps of 1 is a map with no jam on it."""
        return sum(1 for stamp in self.jam.values() if stamp > PATH_JAM_FLOOR)


def price_jam(jam: int) -> int:
    """The floor is the whole design: below it a cell is traffic and costs nothing, above it the
       cost is capped, because the A* heuristic cannot see this penalty and an uncapped one turns
       the search into a flood."""
    over = jam - PATH_JAM_FLOOR
    if over <= 0:
        return 0
    return min(over, PATH_JAM_MAX_CHARGED) * PATH_JAM_COST


class Reservation(NamedTuple):
    """One unit's claim on one cell: when it expects to be there, and which way it is going."""
    arrival: float
    owner: int
    hx: float
    hy: float


class ReservationTrack(NamedTuple):
    """The cells one drive line passes through, in order, with an arrival and a heading each."""
    cells: list[Cell]
    times: list[float]
    headings: list[Heading]


class ReservationMap:
    """When each unit expects to be where, and going which way.

       The congestion map answers 'is this ground spoken for'; this one answers 'will we be on it
       at the same moment'.  Two units driving the same road nose to tail never clash here, because
       their arrival times at each cell are far apart - which is right, that road is fine.  Two
       units whose routes cross do clash, in exactly the one or two cells where they cross, and
       that is where the cost has to land.

       The heading is why it stores more than a time.  Units nose to tail on the same road at the
       same moment are a queue, and a queue is what a road is for; the expensive case is two of
       them arriving from different directions.  Only the second is charged for."""

    def __init__(self) -> None:
        self.at: dict[Cell, list[Reservation]] = {}
        self.owner: dict[int, list[tuple[Cell, Reservation]]] = {}

    def reserve(self, owner: int, cells: Sequence[Cell], times: Sequence[float],
                headings: Sequence[Heading]) -> None:
        self.release(owner)
        entries = [(cell, Reservation(arrival, owner, hx, hy))
                   for cell, arrival, (hx, hy) in zip(cells, times, headings)]
        self.owner[owner] = entries
        for cell, reservation in entries:
            self.at.setdefault(cell, []).append(reservation)

    def _drop(self, cell: Cell, reservation: Reservation) -> None:
        """`reserve` put this entry in this cell's slot and nothing else removes it, so both are
           there to be found - a miss is a book-keeping bug, not a case to absorb."""
        slot = self.at[cell]
        slot.remove(reservation)
        if not slot:
            del self.at[cell]

    def release(self, owner: int) -> None:
        for cell, reservation in self.owner.get(owner, ()):
            self._drop(cell, reservation)
        # the key stays, empty: a unit whose repath failed still gets released as it drives on
        self.owner[owner] = []

    def release_before(self, owner: int, now: float) -> None:
        """Drop what is already in the past - a unit cannot collide with where it has been."""
        keep: list[tuple[Cell, Reservation]] = []
        for cell, reservation in self.owner[owner]:
            if reservation.arrival >= now:
                keep.append((cell, reservation))
            else:
                self._drop(cell, reservation)
        self.owner[owner] = keep

    def count_conflicts(self, cell: Cell, when: float, exclude_owner: int | None = None,
                        heading: Heading | None = None) -> int:
        conflicts = 0
        for other in self.at.get(cell, ()):
            if other.owner == exclude_owner:
                continue
            if abs(other.arrival - when) >= PATH_CROSSING_WINDOW:
                continue
            if heading is not None and heading[0] * other.hx + heading[1] * other.hy > SAME_WAY:
                continue                       # following, not crossing - that is just a queue
            conflicts += 1
        return conflicts


def price_crossing(conflicts: int) -> int:
    if conflicts <= 0:
        return 0
    return min(conflicts, PATH_CROSSING_MAX) * PATH_CROSSING_COST


def find_crossing_cells(reservations: ReservationMap) -> dict[Cell, int]:
    """cell -> how many pairs of units still plan to be in it inside the clash window.  Drawn on
       screen and counted in the headless table; both want the same answer."""
    crossings: dict[Cell, int] = {}
    for cell, slot in reservations.at.items():
        # same way at the same time is a queue, not a clash
        pairs = sum(1 for first, second in itertools.combinations(slot, 2)
                    if abs(first.arrival - second.arrival) < PATH_CROSSING_WINDOW
                    and first.hx * second.hx + first.hy * second.hy <= SAME_WAY)
        if pairs:
            crossings[cell] = pairs
    return crossings


def count_planned_crossings(reservations: ReservationMap) -> int:
    return sum(find_crossing_cells(reservations).values())


@dataclass
class PathPricing:
    """What a search pays for beyond distance.  A default-constructed one prices nothing, which is
       the plain shortest path - so a search that is not meant to pay passes no pricing at all
       rather than five separate arguments saying so."""
    # an empty usage map, not None: `congestion_range` is the switch, and a range of 0 never reads it
    usage: UsageMap = field(default_factory=UsageMap)
    congestion_range: int = 0
    owner: int | None = None
    reservations: ReservationMap | None = None
    start_frame: float = 0.0
    # not None only for a search leaving a jam: an ordinary order keeps the plain shortest route
    jam: JamMap | None = None


NO_PRICING = PathPricing()


@dataclass
class TrafficModel:
    """The two shared maps a group order plans against, and which of their costs it is charged.

       The switches live next to the maps because they are always chosen together - turning one
       cost off is the A/B this sandbox exists to run, and carrying them as two loose booleans put
       a bare `True, False` at every call site instead."""
    usage: UsageMap
    reservations: ReservationMap
    jam: JamMap = field(default_factory=JamMap)
    is_congestion_charged: bool = True
    is_crossing_charged: bool = True
    is_jam_charged: bool = True

    @property
    def congestion_range(self) -> int:
        return PATH_CONGESTION_RANGE_NO_GROUP if self.is_congestion_charged else 0

    @property
    def priced_reservations(self) -> ReservationMap | None:
        return self.reservations if self.is_crossing_charged else None

    def release(self, owner: int) -> None:
        """Take one unit's old plan off both maps, so its own claims cannot price its next one."""
        self.usage.unregister(owner)
        self.reservations.release(owner)

    def pricing_for(self, owner: int, now: float) -> PathPricing:
        """What one unit's own search pays: everyone else's ground, and everyone else's moment."""
        return PathPricing(usage=self.usage, congestion_range=self.congestion_range, owner=owner,
                           reservations=self.priced_reservations, start_frame=now)

    def repath_pricing(self, owner: int, now: float) -> PathPricing:
        """The same, plus the jam map - and this is the only search that reads it.  A unit only
           repaths here because it stopped making progress, which is the sandbox's whole notion of
           leaving a queue; the engine gates the same cost on isQueuedBehindUnits."""
        pricing = self.pricing_for(owner, now)
        pricing.jam = self.jam if self.is_jam_charged else None
        return pricing

    def corridor_pricing(self, now: float) -> PathPricing:
        """The group corridor is one search for the whole group, so it pays no congestion - its own
           units are the congestion.  It still dodges other groups' moments."""
        return PathPricing(reservations=self.priced_reservations, start_frame=now)


# Pathfinder::findPath, near enough for the question being asked.
NEIGHBOURS: tuple[tuple[int, int, int], ...] = (
    (1, 0, COST_ORTHOGONAL), (-1, 0, COST_ORTHOGONAL),
    (0, 1, COST_ORTHOGONAL), (0, -1, COST_ORTHOGONAL),
    (1, 1, COST_DIAGONAL), (1, -1, COST_DIAGONAL),
    (-1, 1, COST_DIAGONAL), (-1, -1, COST_DIAGONAL))

# a step's length in cells, keyed by its cost, to turn (dx, dy) into a unit heading
STEP_LEN: dict[int, float] = {COST_ORTHOGONAL: 1.0, COST_DIAGONAL: math.sqrt(2.0)}


def heuristic(start: Cell, goal: Cell) -> int:
    dx, dy = abs(start[0] - goal[0]), abs(start[1] - goal[1])
    shorter, longer = (dx, dy) if dx < dy else (dy, dx)
    octile = COST_DIAGONAL * shorter + COST_ORTHOGONAL * (longer - shorter)
    return octile * HEURISTIC_NUM // HEURISTIC_DEN


def find_block_route(grid: Grid, start: Cell, goal: Cell,
                     size: int) -> tuple[list[Cell], int] | None:
    """Pathfinder::internal_findHierarchicalPath on the ground layer.  Returns (nodes, pushes):
       the start, the cell each block is entered by, and the goal, plus how many entry cells the
       search queued.  None when it finds no route, which is the engine's setAllPassable.

       A node is a cell.  From the block a node is in, each of the four sides is scanned from its
       middle outwards; a scan cell in the node's zone whose neighbour across the edge is clear
       queues that neighbour, at most once per neighbouring zone per side unless the crossing is
       pinched.  The step costs the straight distance between the two entry cells, so the walk
       inside a block is never priced - that is where this route can be wrong about cost.  The
       scan offsets are the engine's own: i>>1 alternating sides for i = 1..size, which reaches
       one cell past the block on one end and never the first cell on the other."""
    zones = grid.block_zones(size)
    width = grid.width
    start_zone = zones[start[1] * width + start[0]]
    goal_zone = zones[goal[1] * width + goal[0]]
    if not start_zone or not goal_zone:
        return None
    goal_block = (goal[0] // size, goal[1] // size)
    blocks_x = (grid.width + size - 1) // size
    blocks_y = (grid.height + size - 1) // size
    cost: dict[Cell, int] = {start: 0}
    parent: dict[Cell, Cell | None] = {start: None}
    visited = {start}                   # on the open or the closed list, in the engine's terms
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
        sides: list[tuple[Cell, Cell, Cell]] = []       # (scan anchor, scan axis, step across)
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
                        break                   # this side has already been left through
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
                if not (grid.has_room(*across) and grid.has_room(*scan)):
                    reached += CORRIDOR_PINCHED_COST
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
    """The blocks a cell search is held to, and for `soft` how far each cell is from the coarse
       route.  The distance is taken from the route drawn as straight lines between its nodes, in
       cells (8-way steps), walls ignored: it measures how far a cell strays, not how far it is to
       walk back."""

    def __init__(self, grid: Grid, nodes: Sequence[Cell], size: int) -> None:
        self.size = size
        self.nodes = list(nodes)
        self.open: set[Cell] = set()
        for x, y in nodes:
            bx, by = x // size, y // size
            for ox in range(-CORRIDOR_WIDEN, CORRIDOR_WIDEN + 1):
                for oy in range(-CORRIDOR_WIDEN, CORRIDOR_WIDEN + 1):
                    self.open.add((bx + ox, by + oy))
        self.width = grid.width
        self.distance: list[int] = []
        self.order: list[int] = []
        if CORRIDOR == "soft":
            self._measure(grid)

    def _measure(self, grid: Grid) -> None:
        unreached = UNREACHABLE_COST
        self.distance = [unreached] * (grid.width * grid.height)
        self.order = [0] * (grid.width * grid.height)
        frontier: list[Cell] = []
        along = 0
        for (ax, ay), (bx, by) in zip(self.nodes, self.nodes[1:]):
            steps = max(abs(bx - ax), abs(by - ay), 1)
            for step in range(steps + 1):
                cx = ax + round((bx - ax) * step / steps)
                cy = ay + round((by - ay) * step / steps)
                index = cy * grid.width + cx
                if grid.in_bounds(cx, cy) and self.distance[index] == unreached:
                    self.distance[index] = 0
                    self.order[index] = along
                    frontier.append((cx, cy))
                along += 1
        distance = 0
        while frontier:
            distance += 1
            reached: list[Cell] = []
            for x, y in frontier:
                source = self.order[y * grid.width + x]
                for dx, dy, _ in NEIGHBOURS:
                    nx, ny = x + dx, y + dy
                    if grid.in_bounds(nx, ny) and self.distance[ny * grid.width + nx] == unreached:
                        self.distance[ny * grid.width + nx] = distance
                        self.order[ny * grid.width + nx] = source
                        reached.append((nx, ny))
            frontier = reached

    def is_open(self, cell: Cell) -> bool:
        return (cell[0] // self.size, cell[1] // self.size) in self.open

    def admits(self, cell: Cell) -> bool:
        """findGroundPath: inside an open block, or 3 cells diagonally from one."""
        if self.is_open(cell):
            return True
        x, y = cell
        slack = CORRIDOR_SLACK
        return any(self.is_open((x + sx, y + sy))
                   for sx, sy in ((slack, slack), (-slack, slack), (slack, -slack), (-slack, -slack)))

    def soft_cost(self, current: Cell, cell: Cell) -> int:
        index = cell[1] * self.width + cell[0]
        distance = self.distance[index]
        if distance > SOFT_THRESHOLD:
            return SOFT_COST * (distance - SOFT_THRESHOLD)
        if (distance <= SOFT_NEAR
                and self.order[index] > self.order[current[1] * self.width + current[0]]):
            return -SOFT_BONUS
        return 0


def block_corridor(grid: Grid, start: Cell, goal: Cell) -> BlockCorridor | None:
    """The corridor findPath lays before its cell search, or None for none: the mode is off, or the
       coarse search found no route and the engine opens every block."""
    if CORRIDOR == "none":
        return None
    route = find_block_route(grid, start, goal, ZONE_BLOCK_SIZE)
    if route is None:
        return None
    return BlockCorridor(grid, route[0], ZONE_BLOCK_SIZE)


def astar(grid: Grid, start: Cell, goal: Cell, *, clearance: int = 0,
          pricing: PathPricing = NO_PRICING,
          cell_budget: int = DEFAULT_CELL_BUDGET, partial: bool = False) -> tuple[list[Cell], int]:
    """Returns (cells, expansions).  cells is [] when there is no path, or the budget ran out -
       which is the engine's 'out of pathfind cells', the failure congestion cost can cause.  With
       `partial` a search stopped by the budget hands back the route to the cell it got nearest
       the goal instead, as internalFindPath does; the caller tells by the last cell.

       `travelled` is the cost without any penalty in it - the penalties are money, not distance,
       and a detour taken to dodge someone must not also make us think we arrive later than we
       do."""
    if not grid.passable(*goal) or not grid.passable(*start):
        return [], 0
    corridor = block_corridor(grid, start, goal)
    clear = grid.clear_cells(clearance) if clearance else None
    open_heap: list[tuple[int, int, int, Cell]] = [(heuristic(start, goal), 0, 0, start)]
    came_from: dict[Cell, Cell | None] = {start: None}
    best_cost: dict[Cell, int] = {start: 0}
    expansions = 0
    closest, closest_distance = start, UNREACHABLE_COST

    def route_to(cell: Cell) -> list[Cell]:
        path: list[Cell] = []
        step: Cell | None = cell
        while step is not None:
            path.append(step)
            step = came_from[step]
        path.reverse()
        return path

    while open_heap:
        _, cost, travelled, current = heapq.heappop(open_heap)
        if cost > best_cost.get(current, UNREACHABLE_COST):
            continue
        if current == goal:
            return route_to(current), expansions
        expansions += 1
        if expansions > cell_budget:
            if partial and closest != start:
                return route_to(closest), expansions
            return [], expansions
        if partial:
            distance = (goal[0] - current[0]) ** 2 + (goal[1] - current[1]) ** 2
            if distance < closest_distance:
                closest, closest_distance = current, distance
        for dx, dy, step_cost in NEIGHBOURS:
            neighbour = (current[0] + dx, current[1] + dy)
            if clear is not None:
                if (not grid.in_bounds(*neighbour)
                        or not clear[neighbour[1] * grid.width + neighbour[0]]):
                    continue
            elif not grid.passable(*neighbour):
                continue
            new_cost = cost + step_cost
            if corridor is not None:
                if CORRIDOR == "hard":
                    if not corridor.admits(neighbour):
                        continue
                elif CORRIDOR == "flat":
                    if not corridor.is_open(neighbour):
                        new_cost += CORRIDOR_OUTSIDE_COST
                else:
                    new_cost += corridor.soft_cost(current, neighbour)
            new_travel = travelled + step_cost
            if pricing.congestion_range:
                if (abs(neighbour[0] - start[0]) < pricing.congestion_range
                        and abs(neighbour[1] - start[1]) < pricing.congestion_range):
                    new_cost += price_congestion(
                        pricing.usage.count_paths_on(neighbour, pricing.owner))
            if pricing.jam is not None:
                if (abs(neighbour[0] - start[0]) < PATH_JAM_RANGE
                        and abs(neighbour[1] - start[1]) < PATH_JAM_RANGE):
                    new_cost += price_jam(pricing.jam.at(neighbour))
            if pricing.reservations is not None and grid.has_room(*neighbour):
                when = pricing.start_frame + frames_for_cost(new_travel)
                heading = (dx / STEP_LEN[step_cost], dy / STEP_LEN[step_cost])
                new_cost += price_crossing(pricing.reservations.count_conflicts(
                    neighbour, when, pricing.owner, heading))
            if new_cost < best_cost.get(neighbour, UNREACHABLE_COST):
                best_cost[neighbour] = new_cost
                came_from[neighbour] = current
                heapq.heappush(open_heap,
                               (new_cost + heuristic(neighbour, goal), new_cost, new_travel,
                                neighbour))
    return [], expansions


def track_reservations(start: Point, points: Sequence[Point],
                       start_frame: float = 0.0) -> ReservationTrack:
    """The cells a unit driving these waypoints passes through, the frame it reaches each, and the
       direction it is going when it does.

       It walks the actual drive line rather than the A* cell list: after straightening, the
       waypoints are far apart and the cells between them are exactly the ground nobody would
       otherwise know is claimed.  Corridor lanes have no cell list at all."""
    cells: list[Cell] = []
    times: list[float] = []
    headings: list[Heading] = []
    arrival = start_frame
    px, py = start
    last: Cell | None = None
    for qx, qy in points:
        length = math.hypot(qx - px, qy - py)
        heading = ((qx - px) / length, (qy - py) / length) if length > EPSILON else (0.0, 0.0)
        steps = max(1, int(length / RAYCAST_STEP))
        for step in range(1, steps + 1):
            fraction = step / float(steps)
            cell = cell_of(px + (qx - px) * fraction, py + (qy - py) * fraction)
            if cell != last:
                cells.append(cell)
                times.append(arrival + length * fraction / UNIT_SPEED)
                headings.append(heading)
                last = cell
        arrival += length / UNIT_SPEED
        px, py = qx, qy
    return ReservationTrack(cells, times, headings)


@dataclass
class Unit:
    # the id comes from whoever spawned the group: the usage and reservation maps are keyed by it,
    # so two groups spawned separately must not both start counting from zero
    x: float
    y: float
    id: int
    radius: float = UNIT_RADIUS
    path: list[Point] = field(default_factory=list)         # world-space waypoints
    index: int = 0
    is_selected: bool = False
    blocked_frames: int = 0                 # had to slow, swerve or stop for another unit
    blocked_run: int = 0                    # consecutive such frames, which is what a lane needs
    lane_dx: float = 0.0                    # sideways shift of the goal while overtaking ...
    lane_dy: float = 0.0
    lane_frames: int = 0                    # ... and how much longer it is held
    lane_changes: int = 0
    band_half: float = 0.0                  # w(s)/2 of the band this unit drives, 0 = not on one
    band_t: float = 0.0                     # ... and where it sits across it, clamped to +-half
    band_step: float = 0.0                  # ... and how far the pressure term may slide it in a
                                            # frame.  Zero is the control arm: the same band, the
                                            # same shared line, nobody drifting
    band_line: list[Point] = field(default_factory=list)    # the band's centre, shared, from the
                                            # corridor start: segment i is band_line[i:i+2]
    band_drifts: int = 0                    # frames the pressure term actually moved this unit
    stuck_run: int = 0                      # consecutive frames going nowhere -> repath
    jam_priced: int = 0                     # repaths that had jammed ground in front of them at all
    jam_detours: int = 0                    # ... and of those, the ones that came out avoiding it
    last_dist: float | None = None          # distance to the current waypoint, to notice inching
    repaths: int = 0
    goal: Point | None = None               # where this unit was actually sent, after the spread
    rays: list[Ray] = field(default_factory=list)           # what planning actually checked
    cells: list[Cell] = field(default_factory=list)         # cells its path claims, for congestion

    @property
    def pos(self) -> Point:
        return self.x, self.y

    @property
    def has_arrived(self) -> bool:
        return self.index >= len(self.path)


def is_occupied(units: Sequence[Unit], mover: Unit, nx: float, ny: float, *,
                slack: float = COLLISION_SLACK) -> bool:
    """Two units cannot share a spot.  PhysicsUpdate's collision, minus the mass."""
    return any(other is not mover
               and math.hypot(other.x - nx, other.y - ny) < (mover.radius + other.radius) * slack
               for other in units)


def resolve_overlaps(units: Sequence[Unit], grid: Grid) -> None:
    """The engine shoves overlapping units apart rather than stopping them dead; without this the
       sandbox deadlocks at every chokepoint and measures the sandbox, not the pathing."""
    for index, first in enumerate(units):
        for second in units[index + 1:]:
            dx, dy = second.x - first.x, second.y - first.y
            distance = math.hypot(dx, dy)
            overlap = (first.radius + second.radius) - distance
            if overlap <= 0.0:
                continue
            if distance < EPSILON:
                dx, dy, distance = 1.0, 0.0, 1.0
            push = overlap * 0.5
            ux, uy = dx / distance * push, dy / distance * push
            if grid.passable_world(first.x - ux, first.y - uy):
                first.x -= ux
                first.y -= uy
            if grid.passable_world(second.x + ux, second.y + uy):
                second.x += ux
                second.y += uy


def blocker_ahead(units: Sequence[Unit], mover: Unit, hx: float, hy: float) -> Unit | None:
    """Whoever we are stuck behind: the nearest unit within a couple of radii, in front of the
       nose rather than beside it.  This is the sandbox's blockedBy."""
    found: Unit | None = None
    reach = LANE_CHANGE_REACH * (mover.radius * 2.0)
    for other in units:
        if other is mover:
            continue
        ox, oy = other.x - mover.x, other.y - mover.y
        distance = math.hypot(ox, oy)
        if distance < EPSILON or distance > reach:
            continue
        if (ox * hx + oy * hy) / distance < LANE_CHANGE_AHEAD:
            continue
        found, reach = other, distance
    return found


def unit_heading(unit: Unit) -> Heading | None:
    """Where a unit is going this frame, which for a lane change is more useful than where it is."""
    if unit.has_arrived:
        return None
    tx, ty = unit.path[unit.index]
    dx, dy = tx - unit.x, ty - unit.y
    distance = math.hypot(dx, dy)
    if distance < EPSILON:
        return None
    return dx / distance, dy / distance


def pick_lane(grid: Grid, units: Sequence[Unit], mover: Unit,
              hx: float, hy: float) -> Point | None:
    """Sideways, not slower.  Try one footprint out on each side, then two, and take the first that
       has a clear line three footprints ahead and nobody already sitting in it.  Returns the shift
       to add to the goal, or None when there is no lane - a doorway, a cliff, a full road - and the
       unit has to fall back on following the man in front."""
    footprint = mover.radius * 2.0
    ahead = LANE_CHANGE_LOOKAHEAD * footprint
    for offset in LANE_CHANGE_OFFSETS:
        side = offset * footprint
        px, py = -hy * side, hx * side
        tip = (mover.x + px + hx * ahead, mover.y + py + hy * ahead)
        entry = (mover.x + px + hx * footprint, mover.y + py + hy * footprint)
        is_clear = (grid.line_passable(mover.x, mover.y, tip[0], tip[1], radius=mover.radius)
                    and not is_occupied(units, mover, entry[0], entry[1]))
        # drawn like the planner's own checks, but a run makes thousands of these and only the
        # last few are worth looking at
        mover.rays.append((mover.x, mover.y, tip[0], tip[1], is_clear))
        if len(mover.rays) > LANE_RAYS_KEPT:
            del mover.rays[:-LANE_RAYS_KEPT]
        if is_clear:
            return px, py
    return None


def update_lane(grid: Grid, units: Sequence[Unit], mover: Unit, hx: float, hy: float) -> None:
    """One unit's lane, one frame.  A lane is dropped the moment the road ahead clears, which is
       the sandbox's version of patching back onto the path when blockedBy goes false."""
    ahead = blocker_ahead(units, mover, hx, hy)
    if mover.lane_frames > 0:
        mover.lane_frames -= 1
        if ahead is None:
            mover.lane_frames = 0
        if mover.lane_frames == 0:
            mover.lane_dx = mover.lane_dy = 0.0
        return
    if mover.blocked_run < LANE_CHANGE_AFTER_FRAMES or ahead is None:
        return
    # somebody coming the other way is a collision, and going round him is how two columns swap
    # sides and jam worse; this reflex is for traffic that is going where we are going, slower
    other_heading = unit_heading(ahead)
    if other_heading is None or other_heading[0] * hx + other_heading[1] * hy < LANE_CHANGE_SAME_WAY:
        return
    # a queue that is moving is a queue doing its job; leaving it costs distance and gains nothing.
    # Only a stalled one is worth going round, which is also the case the player complains about
    if LANE_CHANGE_STALLED_ONLY and ahead.blocked_run <= 0:
        return
    lane = pick_lane(grid, units, mover, hx, hy)
    if lane is None:
        return
    mover.lane_dx, mover.lane_dy = lane
    mover.lane_frames = LANE_HOLD_FRAMES
    mover.lane_changes += 1
    mover.blocked_run = 0


def band_frame(unit: Unit) -> tuple[Heading, Heading] | None:
    """The tangent and normal of the band segment this unit is driving, or None when it is not on
       a band.  Taken from the band's own centre line rather than from where the unit happens to
       be: a unit that has drifted sideways must not tilt its own idea of sideways."""
    if unit.band_half <= 0.0 or unit.has_arrived or len(unit.band_line) < 2:
        return None
    first = min(unit.index, len(unit.band_line) - 2)
    (ax, ay), (bx, by) = unit.band_line[first], unit.band_line[first + 1]
    dx, dy = bx - ax, by - ay
    length = math.hypot(dx, dy)
    if length < EPSILON:
        return None
    return (dx / length, dy / length), (-dy / length, dx / length)


def band_target(unit: Unit, target: Point) -> Point:
    """Where a unit on the band actually drives: its waypoint, pushed out by its lateral place in
       the band.  The last waypoint is exempt - the goals were spread and reserved when the order
       was given, and shoving them sideways again lands two units on one spot."""
    axes = band_frame(unit)
    if axes is None or unit.index >= len(unit.path) - 1:
        return target
    (_, _), (nx, ny) = axes
    return target[0] + nx * unit.band_t, target[1] + ny * unit.band_t


def band_pressure(units: Sequence[Unit], mover: Unit, tangent: Heading,
                  normal: Heading) -> float:
    """Which way the band is emptier in front of this unit: -1, 0 or +1.

       The slice ahead is split into buckets a footprint wide - narrower and it counts noise - and
       only the two beside the mover's own line are compared.  Nothing in front means nothing to
       drain around, and the unit holds its line."""
    footprint = mover.radius * 2.0
    ahead = BAND_LOOKAHEAD * footprint
    bucket = BAND_BUCKET * footprint
    reach = mover.band_half + bucket
    counts = {-1: 0, 0: 0, 1: 0}
    for other in units:
        if other is mover:
            continue
        ox, oy = other.x - mover.x, other.y - mover.y
        along = ox * tangent[0] + oy * tangent[1]
        if along <= 0.0 or along > ahead:
            continue
        lateral = ox * normal[0] + oy * normal[1]
        if abs(lateral) > reach:
            continue
        side = int(math.floor(lateral / bucket + 0.5))
        if -1 <= side <= 1:
            counts[side] += 1
    if not counts[0]:
        return 0.0
    if counts[-1] - counts[1] >= BAND_DEADBAND:
        return 1.0
    if counts[1] - counts[-1] >= BAND_DEADBAND:
        return -1.0
    # both sides equally busy: keep drifting the way this unit was already going rather than
    # picking a side at random, which would put two units into the same gap on alternate frames
    return math.copysign(1.0, mover.band_t) if mover.band_t else 0.0


def update_band(grid: Grid, units: Sequence[Unit], mover: Unit) -> None:
    """One unit's lateral place in the band, one frame.  Continuous, small and uncommitted: there
       is no lane to latch onto and nothing to hold, so the moment the ground in front clears the
       unit stops drifting and stays where it is."""
    axes = band_frame(mover)
    if axes is None or mover.band_step <= 0.0:
        return
    tangent, normal = axes
    direction = band_pressure(units, mover, tangent, normal)
    if direction == 0.0:
        return
    wanted = min(mover.band_half,
                 max(-mover.band_half,
                     mover.band_t + direction * mover.band_step * mover.radius * 2.0))
    if wanted == mover.band_t:
        return
    # the band's width came from the corridor's clearance, which was measured on the centre line;
    # a lateral offset is still a blind sideways step, so probe it the way the lane ladder does
    tx, ty = mover.path[mover.index]
    px, py = tx + normal[0] * wanted, ty + normal[1] * wanted
    if not (grid.passable_world(px, py)
            and grid.passable_world(px + normal[0] * mover.radius, py + normal[1] * mover.radius)
            and grid.passable_world(px - normal[0] * mover.radius, py - normal[1] * mover.radius)):
        return
    mover.band_t = wanted
    mover.band_drifts += 1


def yield_for(units: Sequence[Unit], grid: Grid, mover: Unit, hx: float, hy: float) -> None:
    """A unit that has arrived is parked, not welded down: when someone still driving cannot get
       past it, it steps aside.  The engine does this too, and without it a tank that has finished
       its order can wedge one that has not for the rest of the run."""
    for other in units:
        if other is mover or not other.has_arrived:
            continue
        ox, oy = other.x - mover.x, other.y - mover.y
        if math.hypot(ox, oy) > (mover.radius + other.radius) * YIELD_REACH:
            continue
        if ox * hx + oy * hy <= 0:                      # behind us, not in the way
            continue
        for sx, sy in ((-hy, hx), (hy, -hx), (hx, hy)):
            nx, ny = other.x + sx * UNIT_SPEED, other.y + sy * UNIT_SPEED
            if grid.passable_world(nx, ny) and not is_occupied(units, other, nx, ny):
                other.x, other.y = nx, ny
                return


def step_units(units: Sequence[Unit], grid: Grid, *, traffic: TrafficModel, now: float) -> None:
    traffic.jam.decay(int(now))
    for unit in units:
        if unit.has_arrived:
            continue
        if unit.band_half > 0.0:
            update_band(grid, units, unit)
        tx, ty = band_target(unit, unit.path[unit.index])
        dx, dy = tx - unit.x, ty - unit.y
        distance = math.hypot(dx, dy)
        is_final = unit.index == len(unit.path) - 1
        # a unit shoved sideways every frame still "moves", and counting only frames where it did
        # not move at all misses it entirely - it inches around the goal for the rest of the run
        is_closing = (unit.last_dist is None
                      or distance < unit.last_dist - UNIT_SPEED * PROGRESS_EPSILON_SPEEDS)
        unit.last_dist = distance
        arrive_radii = FINAL_ARRIVE_RADII if is_final else WAYPOINT_ARRIVE_RADII
        if distance < UNIT_SPEED + unit.radius * arrive_radii:
            unit.index += 1
            unit.last_dist = None
            # everything behind us stops costing other units anything
            if unit.cells:
                done = min(len(unit.cells) - 1,
                           int(unit.index * len(unit.cells) / len(unit.path)))
                traffic.usage.release_before(unit.id, done)
            traffic.reservations.release_before(unit.id, now)
            continue
        # sideways first: a unit that has been stuck behind slower traffic for a few frames looks
        # for a lane beside it, and only files in behind him when there is no lane to take
        ax, ay = dx, dy
        if LANE_CHANGE:
            update_lane(grid, units, unit, dx / distance, dy / distance)
            if unit.lane_frames > 0:
                ax, ay = dx + unit.lane_dx, dy + unit.lane_dy
        # slow down behind whoever is in front, and only sidestep if that is not enough
        has_moved = False
        for angle, speed in SWERVE_ATTEMPTS:
            cos_angle, sin_angle = math.cos(angle), math.sin(angle)
            rx, ry = ax * cos_angle - ay * sin_angle, ax * sin_angle + ay * cos_angle
            turned_length = math.hypot(rx, ry)
            nx = unit.x + rx / turned_length * UNIT_SPEED * speed
            ny = unit.y + ry / turned_length * UNIT_SPEED * speed
            if grid.passable_world(nx, ny) and not is_occupied(units, unit, nx, ny):
                unit.x, unit.y = nx, ny
                has_moved = True
                unit.stuck_run = 0 if is_closing else unit.stuck_run + 1
                if angle or speed < 1.0:
                    unit.blocked_frames += 1
                    unit.blocked_run += 1
                    traffic.jam.note(cell_of(unit.x, unit.y))
                else:
                    unit.blocked_run = 0
                break
        if not has_moved:
            unit.blocked_frames += 1
            unit.blocked_run += 1
            unit.stuck_run += 1
            traffic.jam.note(cell_of(unit.x, unit.y))
            yield_for(units, grid, unit, dx / distance, dy / distance)
    resolve_overlaps(units, grid)


def apply_path(unit: Unit, points: list[Point], cells: list[Cell], traffic: TrafficModel,
               now: float) -> None:
    """Give a unit a freshly planned path and tell both shared maps about it.  Every planner ends
       this way, and they must end it the same way or a unit drives ground it never claimed."""
    unit.path = points
    unit.index = 0
    unit.cells = cells
    # a fresh path is a line until somebody says otherwise; order_band says so straight after, and
    # a unit that repathed out of a jam is off the band and back on its own route
    unit.band_half = 0.0
    unit.band_t = 0.0
    unit.band_line = []
    traffic.usage.register(unit.id, cells)
    traffic.reservations.reserve(unit.id, *track_reservations(unit.pos, points, now))


def repath_stuck(grid: Grid, units: Sequence[Unit], *, traffic: TrafficModel, now: float) -> None:
    """AIInternalMoveToState gives up and asks the pathfinder again when a unit stops making
       progress.  Without it one wedged tank owns the whole measurement."""
    for unit in units:
        if unit.has_arrived or unit.stuck_run < REPATH_AFTER_STUCK_FRAMES or unit.goal is None:
            continue
        traffic.release(unit.id)
        # jammed in the destination cluster: the engine parks rather than shuffling for ever, and
        # the tolerance grows the longer it fails.  Without this one such unit is the whole run.
        park_radii = PARK_RADII if unit.repaths < PARK_RETRY_LIMIT else PARK_RADII_AFTER_RETRIES
        if math.hypot(unit.x - unit.goal[0], unit.y - unit.goal[1]) < park_radii * unit.radius:
            unit.index = len(unit.path)
            unit.stuck_run = 0
            continue
        start_cell = cell_of(unit.x, unit.y)
        # what the jam map is charging this search, recorded before the search so the route can be
        # checked against it: the ground within reach that somebody is stuck on right now
        hot = {cell for cell, stamp in traffic.jam.jam.items()
               if stamp > PATH_JAM_FLOOR
               and abs(cell[0] - start_cell[0]) < PATH_JAM_RANGE
               and abs(cell[1] - start_cell[1]) < PATH_JAM_RANGE}
        cells, _ = astar(grid, start_cell, cell_of(*unit.goal),
                         pricing=traffic.repath_pricing(unit.id, now))
        unit.stuck_run = 0
        unit.repaths += 1
        if not cells:
            continue
        # the exit condition, counted, as a funnel: repaths that had jammed ground in front of them
        # at all, and then the ones whose new route does not drive over any of it.  The two numbers
        # answer different questions - the first says whether the cost ever fires, the second
        # whether it changes anything when it does.
        if hot:
            unit.jam_priced += 1
            if not hot.intersection(cells):
                unit.jam_detours += 1
        points = [world_of(*cell) for cell in cells[1:]] or [unit.goal]
        points[-1] = unit.goal
        points, unit.rays = simplify_raycast(grid, unit.pos, points, radius=unit.radius,
                                             lane_spacing=2.0 * unit.radius)
        apply_path(unit, points, cells, traffic, now)


def compute_centroid(units: Sequence[Unit]) -> Point:
    return (sum(unit.x for unit in units) / len(units),
            sum(unit.y for unit in units) / len(units))


def reserve_goal(grid: Grid, taken: set[Point], gx: float, gy: float, *, radius: float) -> Point:
    """Pathfinder::adjustDestination - spiral out from the clicked point to a goal spot no one else
       has claimed.  Reserving a *cell* is not enough: a cell is 10 units and a tank is 22 across,
       so neighbouring cells still overlap and the group shoves itself for ever on arrival."""
    start = cell_of(gx, gy)
    spacing = 2.0 * radius * GOAL_SPACING_SLACK
    for ring in range(GOAL_SEARCH_RINGS):
        for cx in range(start[0] - ring, start[0] + ring + 1):
            for cy in range(start[1] - ring, start[1] + ring + 1):
                if ring and abs(cx - start[0]) != ring and abs(cy - start[1]) != ring:
                    continue
                if not grid.passable(cx, cy):
                    continue
                wx, wy = world_of(cx, cy)
                if any(math.hypot(wx - tx, wy - ty) < spacing for tx, ty in taken):
                    continue
                if not grid.line_passable(wx, wy, wx, wy, radius=radius):
                    continue
                taken.add((wx, wy))
                return wx, wy
    return gx, gy


def simplify_raycast(grid: Grid, start: Point, points: list[Point], *, radius: float,
                     lane_spacing: float) -> tuple[list[Point], list[Ray]]:
    """AIGroup's straightening: drop a point when the line past it is one this unit can drive.

       Returns (points, rays).  Every check made is recorded as a ray, which is the whole reason
       this file exists - handed back rather than appended to the unit, so a caller that wants the
       straightened line without redrawing the screen can have it."""
    if len(points) < 2:
        return points, []
    simplified: list[Point] = []
    rays: list[Ray] = []
    anchor = start
    pending = points[0]
    run = 0
    for point in points[1:]:
        deviation = point_line_distance(anchor, point, pending)
        can_drop = False
        if deviation < lane_spacing and run < STRAIGHTEN_MAX_RUN:
            # even a deviation under half a cell is worth the ray: the unit is 22 wide, so a
            # shortcut that looks straight can still put its flank through a rock.
            is_clear = grid.line_passable(anchor[0], anchor[1], point[0], point[1], radius=radius)
            rays.append((anchor[0], anchor[1], point[0], point[1], is_clear))
            can_drop = is_clear
        if can_drop and run < STRAIGHTEN_MAX_RUN:
            pending = point
            run += 1
        else:
            simplified.append(pending)
            anchor = pending
            pending = point
            run = 0
    simplified.append(pending)
    return simplified, rays


def point_line_distance(start: Point, end: Point, point: Point) -> float:
    dx, dy = end[0] - start[0], end[1] - start[1]
    if dx * dx + dy * dy < 1.0:
        return math.hypot(point[0] - start[0], point[1] - start[1])
    cross = (point[0] - start[0]) * dy - (point[1] - start[1]) * dx
    return abs(cross) / math.hypot(dx, dy)


@dataclass
class OrderStats:
    """What one order reports about itself.  Both the window and the headless runner start here, so
       a column added for one of them shows up in the other."""
    expansions: int = 0
    searches: int = 0
    avg_waypoints: float = 0.0
    corridor: list[Point] = field(default_factory=list)
    diameter: int = 0
    crossings: int = 0
    notes: list[str] = field(default_factory=list)


def summarise_order(stats: OrderStats, units: Sequence[Unit], traffic: TrafficModel) -> None:
    """Both planners finish the same way, and a column counted for one has to be counted for the
       other or the two halves of the comparison are not measuring the same thing."""
    stats.avg_waypoints = sum(len(unit.path) for unit in units) / len(units)
    stats.crossings = count_planned_crossings(traffic.reservations)


def plan_corridor(grid: Grid, units: Sequence[Unit], goal: Point, *, traffic: TrafficModel,
                  stats: OrderStats, now: float) -> tuple[list[Cell], int]:
    """The one search both group planners start from: a corridor for the whole selection,
       narrowing 6 -> 4 -> 3 before anybody falls back to solving for themselves.  It is the
       group's lane corridor, not the block corridor: that one, when --corridor asks for it, is
       laid by astar underneath every search including this one.
       Returns (cells, diameter), or ([], 0) when the ground carries none of the three widths."""
    cx, cy = compute_centroid(units)
    # this group is about to be re-planned, so its own old reservations must not price the corridor
    for unit in units:
        traffic.reservations.release(unit.id)
    for candidate in GROUP_PATH_DIAMETERS:
        corridor, expansions = astar(grid, cell_of(cx, cy), cell_of(*goal),
                                     clearance=candidate // 2,
                                     pricing=traffic.corridor_pricing(now))
        stats.expansions += expansions
        stats.searches += 1
        if corridor:
            stats.corridor = [world_of(*cell) for cell in corridor]
            stats.diameter = candidate
            return corridor, candidate
    return [], 0


def order_corridor(grid: Grid, units: Sequence[Unit], goal: Point, *, traffic: TrafficModel,
                   stats: OrderStats, now: float) -> None:
    """AIGroup::friend_computeGroundPath + friend_moveVehicleToPos."""
    cx, cy = compute_centroid(units)
    start_cell = cell_of(cx, cy)
    goal_cell = cell_of(*goal)
    corridor, _ = plan_corridor(grid, units, goal, traffic=traffic, stats=stats, now=now)
    if not corridor:
        stats.notes.append("no corridor at any width - falling back to individual paths")
        order_individual(grid, units, goal, traffic=traffic, stats=stats, now=now)
        return
    if start_cell == goal_cell:
        # ordered onto the group's own centre: there is no corridor to lay lanes along
        stats.notes.append("goal is the group's own cell - nothing to move to")
        return
    # heading of the corridor, and the normal the lanes are laid out along
    sx, sy = world_of(*corridor[0])
    ex, ey = world_of(*corridor[-1])
    hx, hy = ex - sx, ey - sy
    hlen = math.hypot(hx, hy)
    hx, hy = hx / hlen, hy / hlen
    nx, ny = -hy, hx
    lane_spacing = 2.0 * max(unit.radius for unit in units) + LANE_GAP
    ordered = sorted(units, key=lambda unit: (unit.x - cx) * nx + (unit.y - cy) * ny)
    lanes = LANES_MAX if len(ordered) >= LANES_MIN_UNITS else LANES_MIN
    per_lane = (len(ordered) + lanes - 1) // lanes
    corridor_world = [world_of(*cell) for cell in corridor]
    taken: set[Point] = set()
    for index, unit in enumerate(ordered):
        lane_index = min(lanes - 1, index // per_lane)
        delta = lane_index - (lanes - 1) / 2.0
        if lanes == LANES_MIN:
            delta *= 2.0                     # -1, +1 rather than -0.5, +0.5
        unit.rays = []
        points: list[Point] = []
        for wx, wy in corridor_world[1:]:
            lateral = lane_spacing * delta
            placed = (wx, wy)
            for shrink in LANE_SHRINKS:
                px, py = wx + lateral * shrink * nx, wy + lateral * shrink * ny
                if shrink == 0.0:
                    placed = (px, py)
                    break
                is_clear = (grid.passable_world(px, py)
                            and grid.passable_world(px + nx * unit.radius, py + ny * unit.radius)
                            and grid.passable_world(px - nx * unit.radius, py - ny * unit.radius))
                unit.rays.append((px + nx * unit.radius, py + ny * unit.radius,
                                  px - nx * unit.radius, py - ny * unit.radius, is_clear))
                if is_clear:
                    placed = (px, py)
                    break
            points.append(placed)
        gx, gy = reserve_goal(grid, taken, goal[0] + lane_spacing * delta * nx,
                              goal[1] + lane_spacing * delta * ny, radius=unit.radius)
        points.append((gx, gy))
        unit.goal = (gx, gy)
        # lanes are parallel, so they clash with each other almost nowhere - which is the
        # corridor's whole point, and worth being able to see next to individual mode
        points, straighten_rays = simplify_raycast(grid, unit.pos, points, radius=unit.radius,
                                                   lane_spacing=lane_spacing)
        unit.rays.extend(straighten_rays)
        apply_path(unit, points, [cell_of(*point) for point in points], traffic, now)
    summarise_order(stats, units, traffic)


def order_band(grid: Grid, units: Sequence[Unit], goal: Point, *, traffic: TrafficModel,
               stats: OrderStats, now: float, step: float | None = None) -> None:
    """The ribbon: the same corridor as column mode, driven as a band rather than as three lanes.

       Every unit gets the one straightened centre line and a lateral position on it, taken from
       where it is standing when the order is given, so the group keeps the shape it had.  From
       there the pressure term in update_band owns that position: nothing is latched, nothing is
       held for a fixed number of frames, and the clamp to the band's half width is what stops it
       trying to spread a column out inside a doorway."""
    # None means "whatever --band-step left in the global", so a sweep row still reaches this;
    # the flat control arm passes 0.0 and means it
    step = BAND_STEP if step is None else step
    cx, cy = compute_centroid(units)
    start_cell = cell_of(cx, cy)
    corridor, diameter = plan_corridor(grid, units, goal, traffic=traffic, stats=stats, now=now)
    if not corridor:
        stats.notes.append("no corridor at any width - falling back to individual paths")
        order_individual(grid, units, goal, traffic=traffic, stats=stats, now=now)
        return
    if start_cell == cell_of(*goal):
        stats.notes.append("goal is the group's own cell - nothing to move to")
        return
    radius = max(unit.radius for unit in units)
    centre = [world_of(*cell) for cell in corridor]
    # straightened once for the whole group rather than once per unit: on a band every unit drives
    # the same line, and the offset that tells them apart is not in the line
    straight, rays = simplify_raycast(grid, centre[0], centre[1:], radius=radius,
                                      lane_spacing=2.0 * radius)
    band_line = [centre[0], *straight]
    # w(s)/2, fixed for now.  The corridor was searched with a clearance of diameter//2, so every
    # cell that near the centre line is drivable: that is (clearance + half a cell) of room either
    # side, less the unit's own radius, which is how far off the line it can sit and still be in
    half = max(0.0, (diameter // 2 + 0.5) * CELL - radius)
    hx, hy = straight[0][0] - centre[0][0], straight[0][1] - centre[0][1]
    hlen = math.hypot(hx, hy) or 1.0
    nx, ny = -hy / hlen, hx / hlen
    taken: set[Point] = set()
    # nearest the goal first, so the units in front claim the spots in front
    for unit in sorted(units, key=lambda unit: math.hypot(unit.x - goal[0], unit.y - goal[1])):
        lateral = min(half, max(-half, (unit.x - cx) * nx + (unit.y - cy) * ny))
        gx, gy = reserve_goal(grid, taken, goal[0] + lateral * nx, goal[1] + lateral * ny,
                              radius=unit.radius)
        unit.goal = (gx, gy)
        unit.rays = list(rays)
        points = [*straight[:-1], (gx, gy)]
        apply_path(unit, points, [cell_of(*point) for point in points], traffic, now)
        unit.band_half = half
        unit.band_t = lateral
        unit.band_step = step
        unit.band_line = band_line
    summarise_order(stats, units, traffic)


def order_individual(grid: Grid, units: Sequence[Unit], goal: Point, *, traffic: TrafficModel,
                     stats: OrderStats, now: float) -> None:
    """-nogrouppath: one A* per unit, and the congestion cost is what holds them apart.

       With the crossing cost on, the search pays twice: the congestion cost for ground someone
       else's remaining path covers, and the crossing cost for ground someone else will be standing
       on at the same moment.  The first spreads the column out, the second is what makes it go
       round a junction instead of through it - and, unlike the first, it charges nothing for
       following the same road a few seconds behind."""
    taken: set[Point] = set()
    cx, cy = compute_centroid(units)
    # nearest the goal first, so the ones in front claim their line first
    ordered = sorted(units, key=lambda unit: math.hypot(unit.x - goal[0], unit.y - goal[1]))
    for unit in ordered:
        unit.rays = []
        # computeIndividualDestination: keep the offset from the group centre, capped
        ox, oy = unit.x - cx, unit.y - cy
        offset_length = math.hypot(ox, oy)
        cap = INDIVIDUAL_OFFSET_RADII * unit.radius
        if offset_length > cap:
            ox, oy = ox / offset_length * cap, oy / offset_length * cap
        gx, gy = reserve_goal(grid, taken, goal[0] + ox, goal[1] + oy, radius=unit.radius)
        unit.goal = (gx, gy)
        traffic.release(unit.id)
        cells, expansions = astar(grid, cell_of(unit.x, unit.y), cell_of(gx, gy),
                                  pricing=traffic.pricing_for(unit.id, now))
        stats.expansions += expansions
        stats.searches += 1
        if not cells:
            stats.notes.append(f"unit {unit.id}: no path (search budget)")
            unit.path = []
            unit.cells = []
            continue
        points = [world_of(*cell) for cell in cells[1:]]
        points[-1] = (gx, gy)
        points, unit.rays = simplify_raycast(grid, unit.pos, points, radius=unit.radius,
                                             lane_spacing=2.0 * unit.radius)
        # reserve the straightened line, not the A* cells: the shortcut is where it drives
        apply_path(unit, points, cells, traffic, now)
    summarise_order(stats, units, traffic)


# what --mode and the headless table's first column name.  MODES is the order they are compared in.
# `flat` is the band without the pressure term - the same shared line and the same clamp, nobody
# drifting - because a band that measures differently from a column has to say which half did it
PLANNERS = {"corridor": order_corridor, "flat": partial(order_band, step=0.0),
            "band": order_band, "single": order_individual}


def spawn_block(grid: Grid, count: int, cx: int, cy: int, first_id: int = 0) -> list[Unit]:
    """`first_id` keeps a second block's ids clear of the first: both blocks share one usage map and
       one reservation map, and those are keyed by id."""
    units: list[Unit] = []
    row = cy
    while len(units) < count:
        for column in range(min(count - len(units), SPAWN_ROW_WIDTH)):
            cell_x = cx + column * SPAWN_STRIDE_CELLS
            if grid.passable(cell_x, row):
                units.append(Unit(*world_of(cell_x, row), id=first_id + len(units)))
        row += SPAWN_STRIDE_CELLS
    return units


def build_scenario(grid: Grid, unit_count: int,
                   scenario: str) -> tuple[list[Unit], list[tuple[list[Unit], Point]]]:
    """(units, [(group, goal), ...]) - what gets ordered where.

       `column` is one group crossing the map, which is what the congestion cost was tuned on.
       `cross` is two groups going to each other's corner: their routes have to meet in the middle,
       and that is the case the time dimension exists for.  A cost that only looks good on `column`
       has not been tested."""
    far_x = grid.width - GOAL_INSET_X
    if scenario == "cross":
        top = spawn_block(grid, unit_count // 2, *CROSS_SPAWN_TOP)
        bottom = spawn_block(grid, unit_count - len(top), CROSS_SPAWN_TOP[0],
                             grid.height - CROSS_SPAWN_BOTTOM_INSET, first_id=len(top))
        return top + bottom, [(top, world_of(far_x, grid.height - GOAL_INSET_Y)),
                              (bottom, world_of(far_x, GOAL_INSET_Y))]
    units = spawn_block(grid, unit_count, *SPAWN_CELL)
    return units, [(units, world_of(far_x, grid.height - GOAL_INSET_Y))]


@dataclass
class Measurement:
    """One row of the headless table: what every seed of one setting added up to, then averaged."""
    waypoints: float = 0.0
    length: float = 0.0
    frames: float = 0.0
    blocked: float = 0.0
    overlap: float = 0.0
    crossings: float = 0.0
    stuck: int = 0
    expansions: float = 0.0
    repaths: float = 0.0
    lanes: float = 0.0
    drift: float = 0.0          # band mode: frames the pressure term actually moved somebody.
                                # A flat result with this at zero says nothing about the idea
    jam_peak: float = 0.0       # most cells jammed at once, whether or not the cost is charged
    jam_priced: float = 0.0     # repaths with jammed ground in front of them ...
    jam_detours: float = 0.0    # ... and the ones that came back avoiding it

    def average_over(self, seeds: int, unit_count: int) -> None:
        """`stuck` is a count of units and stays whole; everything else is per seed, and the path
           length is per unit on top of that."""
        divisor = float(seeds)
        self.waypoints /= divisor
        self.length /= divisor * unit_count
        self.frames /= divisor
        self.blocked /= divisor
        self.overlap /= divisor
        self.crossings /= divisor
        self.expansions /= divisor
        self.repaths /= divisor
        self.lanes /= divisor
        self.drift /= divisor
        self.jam_peak /= divisor
        self.jam_priced /= divisor
        self.jam_detours /= divisor


def measure(map_kind: str, unit_count: int, seeds: int, mode: str, *,
            is_congestion_charged: bool, is_crossing_charged: bool,
            scenario: str, is_jam_charged: bool = True, first_seed: int = 0) -> Measurement:
    """The scenario's orders, played out, averaged over `seeds` maps.

       `first_seed` is for pairing rather than averaging: an average of sixteen seeds hides the one
       seed that carries the whole difference, and this file's own history has an idea that looked
       decisive on the mean and went ten better against six worse when the maps were counted one at
       a time.  A caller that wants that count asks for one seed at a time."""
    total = Measurement()
    for seed in range(first_seed, first_seed + seeds):
        grid = make_map(map_kind, seed=seed)
        traffic = TrafficModel(UsageMap(), ReservationMap(),
                               is_congestion_charged=is_congestion_charged,
                               is_crossing_charged=is_crossing_charged,
                               is_jam_charged=is_jam_charged)
        units, orders = build_scenario(grid, unit_count, scenario)
        stats = OrderStats()
        planner = PLANNERS[mode]
        for group, goal in orders:
            planner(grid, group, goal, traffic=traffic, stats=stats, now=0.0)
        total.overlap += count_path_overlap(units)
        total.crossings += count_planned_crossings(traffic.reservations)
        for unit in units:
            previous = unit.pos
            for point in unit.path:
                total.length += math.hypot(point[0] - previous[0], point[1] - previous[1])
                previous = point
        frames = 0
        while frames < MEASURE_MAX_FRAMES and any(
                not unit.has_arrived and unit.path for unit in units):
            step_units(units, grid, traffic=traffic, now=float(frames))
            repath_stuck(grid, units, traffic=traffic, now=float(frames))
            frames += 1
        total.waypoints += stats.avg_waypoints
        total.frames += frames
        total.blocked += sum(unit.blocked_frames for unit in units)
        total.stuck += sum(1 for unit in units if not unit.has_arrived)
        total.expansions += stats.expansions
        total.repaths += sum(unit.repaths for unit in units)
        total.lanes += sum(unit.lane_changes for unit in units)
        total.drift += sum(unit.band_drifts for unit in units)
        total.jam_peak += traffic.jam.peak
        total.jam_priced += sum(unit.jam_priced for unit in units)
        total.jam_detours += sum(unit.jam_detours for unit in units)
    total.average_over(seeds, unit_count)
    return total


def print_table_head() -> None:
    print(f"{'corridor':<14} {'mode':<9} {'lane':<5} {'jam':<4} {'cost/cap/x':<11} {'waypts':>8} "
          f"{'length':>8} "
          f"{'frames':>8} {'blocked':>9} {'overlap':>8} {'cross':>6} {'stuck':>7} {'cells':>8} "
          f"{'lanes':>6} {'drift':>7} {'jamcell':>8} {'priced':>7} {'round':>6}")


def print_row(corridor: str, mode: str, lane: str, jam: str, label: str,
              result: Measurement) -> None:
    print(f"{corridor:<14} {mode:<9} {lane:<5} {jam:<4} {label:<11} {result.waypoints:>8.1f} "
          f"{result.length:>8.0f} "
          f"{result.frames:>8.0f} {result.blocked:>9.0f} {result.overlap:>8.0f} "
          f"{result.crossings:>6.0f} {result.stuck:>7d} {result.expansions:>8.0f} "
          f"{result.lanes:>6.0f} {result.drift:>7.0f} {result.jam_peak:>8.0f} "
          f"{result.jam_priced:>7.1f} "
          f"{result.jam_detours:>6.1f}")


@contextmanager
def tuning(*, congestion_cost: int, congestion_cap: int, crossing_cost: int,
           crossing_window: int):
    """The penalty constants are read deep inside the search, so a sweep row sets them for the
       duration of its own measurement and hands them back - a row that raised the cost must not
       leave it raised for the next one."""
    global PATH_CONGESTION_COST, PATH_CONGESTION_MAX_PATHS, PATH_CROSSING_COST, PATH_CROSSING_WINDOW
    before = (PATH_CONGESTION_COST, PATH_CONGESTION_MAX_PATHS, PATH_CROSSING_COST,
              PATH_CROSSING_WINDOW)
    PATH_CONGESTION_COST, PATH_CONGESTION_MAX_PATHS = congestion_cost, congestion_cap
    PATH_CROSSING_COST, PATH_CROSSING_WINDOW = crossing_cost, crossing_window
    try:
        yield
    finally:
        (PATH_CONGESTION_COST, PATH_CONGESTION_MAX_PATHS, PATH_CROSSING_COST,
         PATH_CROSSING_WINDOW) = before


@contextmanager
def lane_changing(is_on: bool):
    """Same idea as tuning(): the reflex is read inside the step loop, so a row turns it on for the
       length of its own measurement and hands it back."""
    global LANE_CHANGE
    before = LANE_CHANGE
    LANE_CHANGE = is_on
    try:
        yield
    finally:
        LANE_CHANGE = before


@contextmanager
def overriding(**values: object):
    """Set module constants for one measurement and hand them back, for the knobs that only ever
       change together with a row of a table - the block corridor's mode, size and soft price,
       and the A* estimate scale."""
    module = sys.modules[__name__]
    before = {name: getattr(module, name) for name in values}
    for name, value in values.items():
        setattr(module, name, value)
    try:
        yield
    finally:
        for name, value in before.items():
            setattr(module, name, value)


class SoftPoint(NamedTuple):
    """One soft corridor price: per cell of distance past the threshold, the threshold, and the
       rebate for following the route forward within `near` cells of it."""
    cost: int
    threshold: int
    near: int
    bonus: int

    @property
    def label(self) -> str:
        return f"soft {self.cost}/{self.threshold}/{self.near}/{self.bonus}"

    def settings(self) -> dict[str, object]:
        return {"CORRIDOR": "soft", "SOFT_COST": self.cost, "SOFT_THRESHOLD": self.threshold,
                "SOFT_NEAR": self.near, "SOFT_BONUS": self.bonus}


def parse_soft(text: str) -> list[SoftPoint]:
    """cost[/threshold[/near[/bonus]]], comma separated; what is left out keeps its default."""
    points: list[SoftPoint] = []
    defaults = (SOFT_COST, SOFT_THRESHOLD, SOFT_NEAR, SOFT_BONUS)
    for spec in text.split(","):
        parts = [int(part) for part in spec.split("/") if part]
        points.append(SoftPoint(*parts, *defaults[len(parts):]))
    return points


def corridor_rows(mode: str, soft: Sequence[SoftPoint]) -> list[tuple[str, dict[str, object]]]:
    """(label, constants) for each corridor a table compares; `all` is every mode and every soft
       price asked for."""
    modes = CORRIDOR_MODES if mode == "all" else (mode,)
    rows: list[tuple[str, dict[str, object]]] = []
    for name in modes:
        if name == "soft":
            rows.extend((point.label, point.settings()) for point in soft)
        else:
            rows.append((name, {"CORRIDOR": name}))
    return rows


def travel_cost(cells: Sequence[Cell]) -> int:
    return sum(COST_DIAGONAL if first[0] != second[0] and first[1] != second[1]
               else COST_ORTHOGONAL for first, second in zip(cells, cells[1:]))


SEARCH_MIN_SPAN: float = 0.5                # a pair is at least this share of the diagonal apart
SEARCH_DIAMETERS: tuple[int, ...] = (6, 3)  # the group corridor's widest and narrowest tries


@dataclass
class SearchMeasurement:
    """One corridor's row of the search table.  `excess` is per cent over the shortest route on
       the open map, summed over the searches that reached the goal; a partial route is not
       compared, it is counted."""
    found: int = 0
    partial: int = 0
    failed: int = 0
    excess: float = 0.0
    worst: float = 0.0
    cells: int = 0
    wide_found: list[int] = field(default_factory=lambda: [0] * len(SEARCH_DIAMETERS))
    wide_cells: list[int] = field(default_factory=lambda: [0] * len(SEARCH_DIAMETERS))


def pick_pairs(grid: Grid, rng: random.Random, count: int) -> list[tuple[Cell, Cell]]:
    """`count` start/goal pairs far enough apart to cross the map's structure, both on clear ground
       a unit can stand on.  Reachability is the caller's question."""
    clear = [(x, y) for y in range(grid.height) for x in range(grid.width) if grid.has_room(x, y)]
    span = SEARCH_MIN_SPAN * math.hypot(grid.width, grid.height)
    pairs: list[tuple[Cell, Cell]] = []
    while len(pairs) < count:
        start, goal = rng.choice(clear), rng.choice(clear)
        if math.hypot(goal[0] - start[0], goal[1] - start[1]) >= span:
            pairs.append((start, goal))
    return pairs


def measure_searches(map_kind: str, seeds: int, pair_count: int,
                     rows: Sequence[tuple[str, dict[str, object]]]) -> list[SearchMeasurement]:
    """Single searches, no units: each pair once per corridor at the engine's caps.  A unit's search
       is internalFindPath (ENGINE_CELL_CAP, partial route on the cap); a group's is findGroundPath
       at each of SEARCH_DIAMETERS (GROUND_CELL_CAP, nothing on the cap).  The yardstick is the
       uncapped, unrestricted, unweighted search, so `excess` is what the corridor and the
       estimate scale cost together."""
    results = [SearchMeasurement() for _ in rows]
    eligible = [0] * len(SEARCH_DIAMETERS)
    searched = 0
    hier = 0
    for seed in range(seeds):
        grid = make_map(map_kind, seed=seed)
        rng = random.Random(seed)
        for start, goal in pick_pairs(grid, rng, pair_count):
            with overriding(CORRIDOR="none", HEURISTIC_NUM=1, HEURISTIC_DEN=1):
                best, _ = astar(grid, start, goal, cell_budget=UNREACHABLE_COST)
                if not best:
                    continue
                wide = [bool(astar(grid, start, goal, clearance=diameter // 2,
                                   cell_budget=UNREACHABLE_COST)[0])
                        for diameter in SEARCH_DIAMETERS]
            searched += 1
            route = find_block_route(grid, start, goal, ZONE_BLOCK_SIZE)
            hier += route[1] if route else 0
            optimum = travel_cost(best)
            for index, is_wide in enumerate(wide):
                eligible[index] += is_wide
            for result, (_, settings) in zip(results, rows):
                with overriding(**settings):
                    cells, expansions = astar(grid, start, goal, cell_budget=ENGINE_CELL_CAP,
                                              partial=True)
                    result.cells += expansions
                    if not cells:
                        result.failed += 1
                    elif cells[-1] != goal:
                        result.partial += 1
                    else:
                        result.found += 1
                        excess = 100.0 * (travel_cost(cells) - optimum) / max(optimum, 1)
                        result.excess += excess
                        result.worst = max(result.worst, excess)
                    for index, diameter in enumerate(SEARCH_DIAMETERS):
                        if not wide[index]:
                            continue
                        cells, expansions = astar(grid, start, goal, clearance=diameter // 2,
                                                  cell_budget=GROUND_CELL_CAP)
                        result.wide_found[index] += bool(cells)
                        result.wide_cells[index] += expansions
    print(f"searches on {map_kind}: {searched} pairs over {seeds} seeds, "
          f"blocks {ZONE_BLOCK_SIZE}, estimate {HEURISTIC_NUM}/{HEURISTIC_DEN}, "
          f"coarse search {hier / max(searched, 1):.0f} entry cells a pair")
    wide_head = "".join(f" {f'd{d} found/of':>13} {f'd{d} cells':>9}" for d in SEARCH_DIAMETERS)
    print(f"{'corridor':<18} {'found':>6} {'partial':>8} {'fail':>5} {'excess%':>8} "
          f"{'worst%':>7} {'cells':>7}{wide_head}")
    for result, (label, _) in zip(results, rows):
        wide_cols = "".join(
            f" {f'{result.wide_found[i]}/{eligible[i]}':>13} "
            f"{result.wide_cells[i] / max(eligible[i], 1):>9.0f}"
            for i in range(len(SEARCH_DIAMETERS)))
        print(f"{label:<18} {result.found:>6} {result.partial:>8} {result.failed:>5} "
              f"{result.excess / max(result.found, 1):>8.2f} {result.worst:>7.1f} "
              f"{result.cells / max(searched, 1):>7.0f}{wide_cols}")
    return results


class SweepPoint(NamedTuple):
    """One row of --sweep.  A crossing cost of 0 is the time dimension off."""
    cost: int
    cap: int
    crossing: int


def parse_sweep(text: str) -> list[SweepPoint]:
    """cost[/cap[/crossing]] - a crossing cost of 0 (the default) is the time dimension off."""
    points: list[SweepPoint] = []
    for spec in text.split(","):
        parts = spec.split("/")
        cap = int(parts[1]) if len(parts) > 1 and parts[1] else PATH_CONGESTION_MAX_PATHS
        crossing = int(parts[2]) if len(parts) > 2 and parts[2] else 0
        points.append(SweepPoint(int(parts[0]), cap, crossing))
    return points


def run_headless(args: argparse.Namespace, sweep: Sequence[SweepPoint]) -> None:
    """No window: play the same order under each setting and print the numbers to compare.
       This is ai-batch.ps1 for the sandbox - a claim about a change is argued with this.

       `overlap` counts cells two plans share at all, `cross` only the ones they share at the same
       moment.  A change that trades the first for the second is doing what it was asked to."""
    rows = corridor_rows(args.corridor, parse_soft(args.soft))
    if args.pairs:
        measure_searches(args.map, args.headless, args.pairs, rows)
        if not args.units:
            return
    print(f"map {args.map}  scenario {args.scenario}  units {args.units}  "
          f"seeds {args.headless}  window {args.window} frames")
    print_table_head()
    lane_settings = (False, True) if args.lanes == "both" else (args.lanes == "on",)
    jam_settings = (False, True) if args.jam == "both" else (args.jam == "on",)
    modes = args.modes.split(",") if args.modes else MODES
    for corridor, settings in rows:
        with overriding(**settings):
            for mode in modes:
                for is_lane_changing in lane_settings:
                    lane = "on" if is_lane_changing else "off"
                    for is_jam_charged in jam_settings:
                        jam = "on" if is_jam_charged else "off"
                        run_sweep_rows(args, sweep, corridor, mode, lane, jam, is_lane_changing,
                                       is_jam_charged)


def run_sweep_rows(args: argparse.Namespace, sweep: Sequence[SweepPoint], corridor: str, mode: str,
                   lane: str, jam: str, is_lane_changing: bool, is_jam_charged: bool) -> None:
    """One mode's rows under one corridor: the costs off, then each sweep point."""
    with lane_changing(is_lane_changing):
        if not args.no_off:
            with tuning(congestion_cost=args.cost, congestion_cap=args.cap,
                        crossing_cost=args.cross, crossing_window=args.window):
                print_row(corridor, mode, lane, jam, "off",
                          measure(args.map, args.units, args.headless, mode,
                                  is_congestion_charged=False, is_crossing_charged=False,
                                  is_jam_charged=is_jam_charged, scenario=args.scenario))
        for point in sweep:
            with tuning(congestion_cost=point.cost, congestion_cap=point.cap,
                        crossing_cost=point.crossing or args.cross,
                        crossing_window=args.window):
                print_row(corridor, mode, lane, jam,
                          f"{point.cost}/{point.cap}/{point.crossing}",
                          measure(args.map, args.units, args.headless, mode,
                                  is_congestion_charged=True,
                                  is_crossing_charged=point.crossing > 0,
                                  is_jam_charged=is_jam_charged,
                                  scenario=args.scenario))


class Simulation:
    """The map, the units on it and the two shared traffic maps - everything a headless run needs.
       Held apart from the window so ordering and stepping are the same code in both."""

    def __init__(self, map_kind: str, seed: int, unit_count: int, mode: str) -> None:
        self.map_kind = map_kind
        self.unit_count = unit_count
        self.mode = mode
        self.frames = 0
        self.last_goal: Point | None = None
        self.stats = OrderStats()
        self.seed = seed
        self.grid = make_map(map_kind, seed=seed)
        self.traffic = TrafficModel(UsageMap(), ReservationMap())
        self.units = self._spawn()

    def _spawn(self) -> list[Unit]:
        units = spawn_block(self.grid, self.unit_count, *SPAWN_CELL)
        for unit in units:
            unit.is_selected = True
        return units

    def regenerate(self, seed: int) -> None:
        """A new map under the same settings.  The traffic maps go with the old map: they are keyed
           by cell, and every cell they name has just moved."""
        self.seed = seed
        self.grid = make_map(self.map_kind, seed=seed)
        self.traffic = TrafficModel(UsageMap(), ReservationMap())
        self.units = self._spawn()

    @property
    def selected_units(self) -> list[Unit]:
        selected = [unit for unit in self.units if unit.is_selected]
        return selected or self.units

    def order(self, goal: Point) -> None:
        self.last_goal = goal
        self.stats = OrderStats()
        selected = self.selected_units
        for unit in selected:
            unit.blocked_frames = 0
        planner = PLANNERS[self.mode]
        planner(self.grid, selected, goal, traffic=self.traffic, stats=self.stats,
                now=float(self.frames))
        print(f"[order] mode={self.mode} units={len(selected)} searches={self.stats.searches} "
              f"expansions={self.stats.expansions} "
              f"avg waypoints={self.stats.avg_waypoints:.1f} "
              f"crossings={self.stats.crossings} {' '.join(self.stats.notes)}")

    def step(self) -> None:
        now = float(self.frames)
        step_units(self.units, self.grid, traffic=self.traffic, now=now)
        repath_stuck(self.grid, self.units, traffic=self.traffic, now=now)
        self.frames += 1


BG: Colour = (24, 26, 30)
WALL: Colour = (58, 62, 70)
UNIT_COL: Colour = (120, 190, 255)
UNIT_SEL: Colour = (255, 214, 110)
PATH_COL: Colour = (90, 220, 160)
CORRIDOR_COL: Colour = (70, 90, 140)
RAY_OK: Colour = (60, 200, 120)
RAY_BAD: Colour = (220, 90, 90)
HEAT: Colour = (200, 120, 60)
HEAT_FLOOR: Colour = (50, 28, 24)                   # an unused cell is still darker than the walls
HEAT_GAIN: tuple[float, float, float] = (0.8, 0.7, 0.5)     # warm, so a busy cell reads as orange
JAM_COL: Colour = (150, 60, 150)    # where units are stuck, not where they plan to drive: not orange
HUD_TEXT: Colour = (200, 205, 215)


class RayDetail(IntEnum):
    """Whose line-of-sight checks the window draws."""
    OFF = 0
    SELECTED = 1
    ALL = 2


class Renderer:
    """The window.  It reads a Simulation and owns nothing the simulation needs back."""

    def __init__(self, grid: Grid, scale: float) -> None:
        pygame.init()
        self.scale = scale
        self.screen = pygame.display.set_mode(
            (int(grid.width * CELL * scale), int(grid.height * CELL * scale) + HUD_HEIGHT))
        pygame.display.set_caption("PathLab")
        self.font = pygame.font.SysFont("consolas", HUD_FONT_SIZE)
        self.ray_detail = RayDetail.SELECTED
        self.show_heat = False
        self.show_jam = True        # on by default: it is empty until something actually jams

    def close(self) -> None:
        pygame.quit()

    def to_screen(self, x: float, y: float) -> tuple[int, int]:
        return int(x * self.scale), int(y * self.scale)

    def _cell_rect(self, cx: int, cy: int, pad: int = 0) -> Rect:
        size = CELL * self.scale
        return cx * size, cy * size, size + pad, size + pad

    def draw(self, sim: Simulation) -> None:
        self.screen.fill(BG)
        self._draw_terrain(sim.grid)
        if self.show_heat:
            self._draw_heat(sim)
        if self.show_jam:
            self._draw_jam(sim)
        self._draw_crossings(sim)
        self._draw_corridor(sim.stats)
        if self.ray_detail is not RayDetail.OFF:
            self._draw_rays(sim.units)
        self._draw_paths(sim.units)
        self._draw_units(sim.units)
        self._draw_hud(sim)
        pygame.display.flip()

    def _draw_terrain(self, grid: Grid) -> None:
        for cy in range(grid.height):
            for cx in range(grid.width):
                if not grid.passable(cx, cy):
                    pygame.draw.rect(self.screen, WALL,
                                     self._cell_rect(cx, cy, pad=TERRAIN_OVERDRAW))

    def _draw_heat(self, sim: Simulation) -> None:
        for (cx, cy), paths in sim.traffic.usage.count.items():
            if not sim.grid.in_bounds(cx, cy):
                continue
            level = min(paths, PATH_CONGESTION_MAX_PATHS) / PATH_CONGESTION_MAX_PATHS
            colour = (int(HEAT_FLOOR[0] + HEAT[0] * level * HEAT_GAIN[0]),
                      int(HEAT_FLOOR[1] + HEAT[1] * level * HEAT_GAIN[1]),
                      int(HEAT_FLOOR[2] + HEAT[2] * level * HEAT_GAIN[2]))
            pygame.draw.rect(self.screen, colour, self._cell_rect(cx, cy))

    def _draw_jam(self, sim: Simulation) -> None:
        """Ground units are stuck on, drawn only once a cell is over the floor - a map speckled
           with stamps of 1 is a map with no jam on it and outlining every one of them says
           nothing.  This is the cost a repath is paying, made visible."""
        for (cx, cy), stamp in sim.traffic.jam.jam.items():
            if stamp <= PATH_JAM_FLOOR or not sim.grid.in_bounds(cx, cy):
                continue
            level = min(stamp - PATH_JAM_FLOOR, PATH_JAM_MAX_CHARGED) / PATH_JAM_MAX_CHARGED
            pygame.draw.rect(self.screen, (int(JAM_COL[0] * level), int(JAM_COL[1] * level),
                                           int(JAM_COL[2] * level)),
                             self._cell_rect(cx, cy))

    def _draw_crossings(self, sim: Simulation) -> None:
        # where two units still plan to be at the same moment - what the crossing cost buys out
        for cx, cy in find_crossing_cells(sim.traffic.reservations):
            if sim.grid.in_bounds(cx, cy):
                pygame.draw.rect(self.screen, RAY_BAD, self._cell_rect(cx, cy),
                                 CROSSING_OUTLINE_WIDTH)

    def _draw_corridor(self, stats: OrderStats) -> None:
        points = [self.to_screen(x, y) for x, y in stats.corridor]
        if len(points) > 1:
            pygame.draw.lines(self.screen, CORRIDOR_COL, False, points,
                              max(1, int(stats.diameter * CELL * self.scale / 2)))

    def _draw_rays(self, units: Sequence[Unit]) -> None:
        # failures last, so the one check that rejected a lane is not buried under the rest
        for want_clear in (True, False):
            for unit in units:
                if self.ray_detail is RayDetail.SELECTED and not unit.is_selected:
                    continue
                for ax, ay, bx, by, is_clear in unit.rays:
                    if is_clear != want_clear:
                        continue
                    pygame.draw.line(self.screen, RAY_OK if is_clear else RAY_BAD,
                                     self.to_screen(ax, ay), self.to_screen(bx, by), 1)

    def _draw_paths(self, units: Sequence[Unit]) -> None:
        for unit in units:
            if not unit.path or unit.has_arrived:
                continue
            remaining = unit.path[unit.index:]
            points = [self.to_screen(unit.x, unit.y)]
            points.extend(self.to_screen(x, y) for x, y in remaining)
            if len(points) > 1:
                pygame.draw.lines(self.screen, PATH_COL, False, points, 1)
            for x, y in remaining:
                pygame.draw.circle(self.screen, PATH_COL, self.to_screen(x, y), WAYPOINT_DOT)

    def _draw_units(self, units: Sequence[Unit]) -> None:
        for unit in units:
            colour = UNIT_SEL if unit.is_selected else UNIT_COL
            centre = self.to_screen(unit.x, unit.y)
            pygame.draw.circle(self.screen, colour, centre, int(unit.radius * self.scale))
            if unit.blocked_frames:
                pygame.draw.circle(self.screen, RAY_BAD, centre, int(unit.radius * self.scale),
                                   BLOCKED_RING_WIDTH)

    def _draw_hud(self, sim: Simulation) -> None:
        top = int(sim.grid.height * CELL * self.scale) + HUD_TOP_PAD
        blocked = sum(unit.blocked_frames for unit in sim.units)
        moving = sum(1 for unit in sim.units if not unit.has_arrived)
        congestion = str(PATH_CONGESTION_COST) if sim.traffic.is_congestion_charged else "off"
        crossing = str(PATH_CROSSING_COST) if sim.traffic.is_crossing_charged else "off"
        jam = str(PATH_JAM_COST) if sim.traffic.is_jam_charged else "off"
        heat = "on" if self.show_heat else "off"
        lines = [
            f"mode {sim.mode:<9} congestion {congestion:<3} crossing {crossing:<3} "
            f"jam {jam:<3} rays {self.ray_detail.name.lower():<8} heat {heat:<3}   "
            f"[1][2][3] [c][t][j][k][r][p] [n]map [tab]replan",
            f"corridor width {sim.stats.diameter} cells   searches {sim.stats.searches}   "
            f"cell expansions {sim.stats.expansions}   "
            f"avg waypoints/unit {sim.stats.avg_waypoints:.1f}",
            f"units {len(sim.units)}  still moving {moving}  blocked unit-frames {blocked}  "
            f"repaths {sum(unit.repaths for unit in sim.units)}  "
            f"jammed cells {sim.traffic.jam.blocking_cells} "
            f"(routed round {sum(unit.jam_detours for unit in sim.units)})  "
            f"crossings {count_planned_crossings(sim.traffic.reservations)} now {sim.frames}  "
            f"{'; '.join(sim.stats.notes[:HUD_NOTES_SHOWN])}",
        ]
        for line_index, text in enumerate(lines):
            self.screen.blit(self.font.render(text, True, HUD_TEXT),
                             (HUD_TEXT_X, top + line_index * HUD_LINE_HEIGHT))


class Lab:
    """Keys and mouse in, one simulation step and one drawn frame out."""

    def __init__(self, args: argparse.Namespace) -> None:
        self.sim = Simulation(map_kind=args.map, seed=args.seed, unit_count=args.units,
                              mode=args.mode)
        self.view = Renderer(self.sim.grid, args.scale)
        self.is_paused = False
        self.drag_start: Point | None = None

    def __enter__(self) -> "Lab":
        return self

    def __exit__(self, exc_type: object, exc: object, traceback: object) -> None:
        self.view.close()

    def run(self) -> None:
        clock = pygame.time.Clock()
        while True:
            for event in pygame.event.get():
                if event.type == pygame.QUIT:
                    return
                if event.type == pygame.KEYDOWN:
                    if event.key == pygame.K_ESCAPE:
                        return
                    self._on_key(event.key)
                if event.type == pygame.MOUSEBUTTONDOWN:
                    self._on_press(event)
                if (event.type == pygame.MOUSEBUTTONUP and event.button == MOUSE_RIGHT
                        and self.drag_start):
                    self._on_drag_end(self.drag_start, event)
            if not self.is_paused:
                self.sim.step()
            self.view.draw(self.sim)
            clock.tick(RENDER_FPS)

    def _on_key(self, key: int) -> None:
        if key == pygame.K_1:
            self.sim.mode = "corridor"
        elif key == pygame.K_2:
            self.sim.mode = "single"
        elif key == pygame.K_3:
            self.sim.mode = "band"
        elif key == pygame.K_c:
            self.sim.traffic.is_congestion_charged = not self.sim.traffic.is_congestion_charged
        elif key == pygame.K_t:
            self.sim.traffic.is_crossing_charged = not self.sim.traffic.is_crossing_charged
        elif key == pygame.K_r:
            self.view.ray_detail = RayDetail((self.view.ray_detail + 1) % len(RayDetail))
        elif key == pygame.K_p:
            self.view.show_heat = not self.view.show_heat
        elif key == pygame.K_j:
            self.sim.traffic.is_jam_charged = not self.sim.traffic.is_jam_charged
        elif key == pygame.K_k:
            self.view.show_jam = not self.view.show_jam
        elif key == pygame.K_SPACE:
            self.is_paused = not self.is_paused
        elif key == pygame.K_n:
            self.sim.regenerate(random.randrange(SEED_RANGE))
        elif key == pygame.K_TAB and self.sim.last_goal:
            self.sim.order(self.sim.last_goal)

    def _world_pos(self, screen_pos: tuple[int, int]) -> Point:
        return screen_pos[0] / self.view.scale, screen_pos[1] / self.view.scale

    def _on_press(self, event: pygame.event.Event) -> None:
        wx, wy = self._world_pos(event.pos)
        if event.button == MOUSE_LEFT and wy < self.sim.grid.height * CELL:
            self.sim.order((wx, wy))
        if event.button == MOUSE_RIGHT:
            self.drag_start = (wx, wy)

    def _on_drag_end(self, drag_start: Point, event: pygame.event.Event) -> None:
        x0, y0 = drag_start
        x1, y1 = self._world_pos(event.pos)
        left, right = min(x0, x1), max(x0, x1)
        top, bottom = min(y0, y1), max(y0, y1)
        # a right click rather than a drag: that selects everything, not nothing
        is_box = (right - left) > CELL and (bottom - top) > CELL
        for unit in self.sim.units:
            unit.is_selected = (left <= unit.x <= right and top <= unit.y <= bottom
                                if is_box else True)
        self.drag_start = None


def main() -> None:
    global BAND_STEP
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--mode", choices=MODES, default="corridor")
    parser.add_argument("--map", choices=MAP_KINDS, default="choke")
    parser.add_argument("--units", type=int, default=8)
    parser.add_argument("--scenario", choices=SCENARIOS, default="column",
                        help="headless only: one group crossing the map, or two crossing each other")
    parser.add_argument("--scale", type=float, default=1.0)
    parser.add_argument("--seed", type=int, default=7)
    parser.add_argument("--headless", type=int, metavar="SEEDS",
                        help="no window: run SEEDS orders per mode and print the comparison")
    parser.add_argument("--cost", type=int, default=PATH_CONGESTION_COST,
                        help="cost per path already claiming a cell (engine default 5, step is 10)")
    parser.add_argument("--cap", type=int, default=PATH_CONGESTION_MAX_PATHS,
                        help="how many paths on one cell still add cost (engine default 4)")
    parser.add_argument("--cross", type=int, default=PATH_CROSSING_COST,
                        help="cost per unit that will be in a cell at the same moment we are")
    parser.add_argument("--window", type=int, default=PATH_CROSSING_WINDOW,
                        help="frames either side of an arrival that still count as the same moment")
    parser.add_argument("--sweep", default="5/4/0,20/4/0,20/4/60,20/4/120",
                        help="headless only: cost/cap/crossing triples to compare (crossing 0 = off)")
    parser.add_argument("--lanes", choices=("off", "on", "both"), default="off",
                        help="overtake instead of queueing; 'both' measures it against itself")
    parser.add_argument("--band-step", type=float, default=BAND_STEP, dest="band_step",
                        help="band mode: footprints of lateral drift per frame (0 = the flat band)")
    parser.add_argument("--jam", choices=("off", "on", "both"), default="on",
                        help="charge a repath for ground somebody is stuck on; 'both' compares")
    parser.add_argument("--corridor", choices=(*CORRIDOR_MODES, "all"), default="none",
                        help="block corridor every search lays first; 'all' compares the four")
    parser.add_argument("--soft", default=f"{SOFT_COST}/{SOFT_THRESHOLD}/{SOFT_NEAR}/{SOFT_BONUS}",
                        help="soft corridor prices to compare: cost/threshold/near/bonus, ...")
    parser.add_argument("--blocks", type=int, default=ZONE_BLOCK_SIZE,
                        help="cells per block side for the corridor's coarse search (engine 10)")
    parser.add_argument("--widen", type=int, default=CORRIDOR_WIDEN,
                        help="blocks opened either side of each route block (engine 1)")
    parser.add_argument("--hweight", type=float, default=HEURISTIC_NUM / HEURISTIC_DEN,
                        help="A* estimate scale (sandbox 1.0, retail 1.5)")
    parser.add_argument("--pairs", type=int, default=0,
                        help="headless only: also time this many single searches per seed")
    parser.add_argument("--modes", default="",
                        help="headless only: comma list of modes for the group table (all)")
    parser.add_argument("--no-off", action="store_true", dest="no_off",
                        help="headless only: leave out the costs-off row")
    args = parser.parse_args()
    BAND_STEP = args.band_step
    weight = Fraction(args.hweight).limit_denominator(16)
    soft = parse_soft(args.soft)[0]
    sweep = parse_sweep(args.sweep)
    with overriding(ZONE_BLOCK_SIZE=args.blocks, CORRIDOR_WIDEN=args.widen,
                    HEURISTIC_NUM=weight.numerator, HEURISTIC_DEN=weight.denominator):
        if args.headless:
            run_headless(args, sweep)
            return
        # the window runs one corridor; 'all' is a table's word and draws as none
        corridor = args.corridor if args.corridor != "all" else "none"
        with overriding(**(soft.settings() if corridor == "soft" else {"CORRIDOR": corridor})):
            run_window(args)


def run_window(args: argparse.Namespace) -> None:
    with lane_changing(args.lanes != "off"), \
            tuning(congestion_cost=args.cost, congestion_cap=args.cap, crossing_cost=args.cross,
                   crossing_window=args.window), Lab(args) as lab:
        lab.run()


if __name__ == "__main__":
    main()
    sys.exit(0)
