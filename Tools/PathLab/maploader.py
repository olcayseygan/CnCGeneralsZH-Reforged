#!/usr/bin/env python3
"""A shipped Zero Hour map as PathLab's ground grid, read from the player's own install at run time.

Nothing derived from EA's data is kept in the repository: the map comes out of MapsZH.big and the
object footprints out of INIZH.big each time this runs. The grid is built the way the engine builds
it when a match starts (AIPathfind.cpp): Pathfinder::classifyMap over the terrain, then
classifyObjectFootprint for every map object in file order, as GameLogic::startNewGame adds them.

    python maploader.py ["Tournament Desert"] [--game-dir DIR] [--png out.png]

The install is GeneralsMD/Run beside this tool unless --game-dir or PATHLAB_GAME_DIR says otherwise.
A path to a loose .map file works in place of a map name.

Engine cell types fold into pathlab's three: water and cliff are IMPASSABLE to a ground unit, a
structure's footprint is OBSTACLE.
"""

from __future__ import annotations

import argparse
import dataclasses
import math
import os
import re
import struct
import sys
import time
import zlib
from collections import Counter
from collections.abc import Iterable
from dataclasses import dataclass, field

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "RandomMapLab"))

import mapstyle                                       # noqa: E402  (puts bigfile and mapscore on the path)
from mapscore import MAP_HEIGHT_SCALE                 # noqa: E402

Point = tuple[float, float]

# pathlab's cell types (pathlab.py: CLEAR, OBSTACLE, IMPASSABLE)
CLEAR, OBSTACLE, IMPASSABLE = 0, 1, 2

# PathfindCell::CellType (AIPathfind.h:357), the ones a map load can produce
ENGINE_CLEAR, ENGINE_WATER, ENGINE_CLIFF, ENGINE_OBSTACLE = 0x00, 0x01, 0x02, 0x04
TO_PATHLAB = {ENGINE_CLEAR: CLEAR, ENGINE_WATER: IMPASSABLE, ENGINE_CLIFF: IMPASSABLE,
              ENGINE_OBSTACLE: OBSTACLE}

GAME_DIR_VARIABLE = "PATHLAB_GAME_DIR"
DEFAULT_GAME_DIR = os.path.join(HERE, "..", "..", "GeneralsMD", "Run")
MAPS_ARCHIVE = "MapsZH.big"
INI_ARCHIVE = "INIZH.big"
DEFAULT_MAP = "Tournament Desert"
DEFAULT_TEMPLATE_INI = "data\\ini\\default\\object.ini"
OBJECT_INI_FOLDER = "data\\ini\\object\\"
DEFAULT_TEMPLATE_NAME = "DefaultThingTemplate"

CELL_SIZE = 10.0                          # PATHFIND_CELL_SIZE_F
CLIFF_SLOPE_LIMIT = 9.8                   # PATHFIND_CLIFF_SLOPE_LIMIT_F
FOOTPRINT_STEP = CELL_SIZE * 0.5          # STEP_SIZE in classifyObjectFootprint and classifyFence
FENCE_HALF_DEPTH = CELL_SIZE / 10.0       # classifyFence's halfsizeY
CIRCLE_SLACK = 0.4                        # added to the radius in cells by the cylinder rasteriser
PINCH_MARGIN = 2                          # cells round a footprint whose pinched flags are redone
MIN_CLEAR_ORTHOGONAL = 2                  # fewer clear orthogonal neighbours than this: pinched
MIN_CLEAR_NEIGHBOURS = 4                  # fewer clear neighbours in all than this: pinched
SNAP = 0.5                                # REAL_TO_INT_FLOOR((x + 0.5) / cell) in the footprint code

# INI lines that open a nested block inside an Object, so their fields are not the object's. Checked
# against every retail object file: with these the End lines balance in all of them.
BLOCK_OPENERS = {"behavior", "body", "draw", "clientupdate", "clientbehavior", "conditionstate",
                 "defaultconditionstate", "transitionstate", "weaponset", "armorset",
                 "unitspecificsounds", "unitspecificfx", "prerequisites", "inheritablemodule",
                 "overrideablebylikekind", "addmodule", "replacemodule"}
BARE_BLOCK_OPENERS = {"turret", "altturret"}          # a block only without "=", a bone name with it
BARE_BLOCK_SUFFIXES = ("decal", "decaltemplate")
INI_COMMENT = re.compile(r";|//")
INI_SEPARATORS = re.compile(r"[\s=]+")
LEADING_NUMBER = re.compile(r"[-+]?(\d+\.?\d*|\.\d+)([eE][-+]?\d+)?")

# MapObject flags (MapObject.h:66)
FLAG_ROAD_FLAGS = 0x02 | 0x04
FLAG_BRIDGE_FLAGS = 0x10 | 0x20
START_PREFIX, START_SUFFIX = "Player_", "_Start"


# -------------------------------------------------------------------------------------------------
# Object templates: the few ThingTemplate fields the footprint code reads
# -------------------------------------------------------------------------------------------------

@dataclass
class Template:
    kinds: set[str] = field(default_factory=set)
    geometry: str = "SPHERE"
    major_radius: float = 1.0
    minor_radius: float = 1.0
    is_small: bool = True
    fence_width: float = 0.0
    fence_offset: float = 0.0
    is_bridge: bool = False

    def copy(self) -> Template:
        return dataclasses.replace(self, kinds=set(self.kinds))


def parse_real(token: str) -> float:
    """INI::scanReal: atof, which reads the leading number and stops."""
    match = LEADING_NUMBER.match(token)
    return float(match.group(0)) if match else 0.0


def parse_kinds(kinds: set[str], tokens: list[str]) -> None:
    """BitFlags::parse (BitFlagsIO.h:65): NONE clears, +X and -X edit, a plain list replaces."""
    replaced = False
    for token in tokens:
        name = token.upper()
        if name == "NONE":
            kinds.clear()
            return
        if name[0] == "+":
            kinds.add(name[1:])
        elif name[0] == "-":
            kinds.discard(name[1:])
        else:
            if not replaced:
                kinds.clear()
                replaced = True
            kinds.add(name)


def apply_field(template: Template, key: str, values: list[str]) -> None:
    """ThingTemplate's field table (ThingTemplate.cpp:130-244), the footprint fields only."""
    if not values:
        return
    if key == "kindof":
        parse_kinds(template.kinds, values)
    elif key == "geometry":
        template.geometry = values[0].upper()
    elif key == "geometrymajorradius":
        template.major_radius = parse_real(values[0])
    elif key == "geometryminorradius":
        template.minor_radius = parse_real(values[0])
    elif key == "geometryissmall":
        template.is_small = values[0].lower() in ("yes", "true", "1")
    elif key == "fencewidth":
        template.fence_width = parse_real(values[0])
    elif key == "fencexoffset":
        template.fence_offset = parse_real(values[0])
    elif key == "isbridge":
        template.is_bridge = values[0].lower() in ("yes", "true", "1")


def opens_block(first: str, line: str) -> bool:
    if first in BLOCK_OPENERS:
        return True
    return "=" not in line and (first in BARE_BLOCK_OPENERS or first.endswith(BARE_BLOCK_SUFFIXES))


def parse_templates(texts: Iterable[str], templates: dict[str, Template] | None = None
                    ) -> dict[str, Template]:
    """Object and ObjectReskin blocks into templates keyed by lower-case name (NameKeys ignore case).
    A new Object starts from DefaultThingTemplate once that has been read (ThingFactory::newTemplate);
    an ObjectReskin copies its original and is dropped when the original is missing
    (ThingFactory::parseObjectDefinition)."""
    templates = {} if templates is None else templates
    for text in texts:
        depth = 0
        current: Template | None = None
        for raw in text.splitlines():
            line = INI_COMMENT.split(raw, 1)[0]
            tokens = INI_SEPARATORS.split(line.strip())
            if not tokens[0]:
                continue
            first = tokens[0].lower()
            if depth == 0:
                if first == "object" and len(tokens) > 1:
                    name = tokens[1].lower()
                    if name not in templates:
                        base = templates.get(DEFAULT_TEMPLATE_NAME.lower())
                        templates[name] = base.copy() if base else Template()
                    current, depth = templates[name], 1
                elif first == "objectreskin" and len(tokens) > 2:
                    original = templates.get(tokens[2].lower())
                    current = original.copy() if original else None
                    if current is not None:
                        templates[tokens[1].lower()] = current
                    depth = 1
            elif first == "end":
                depth -= 1
            elif opens_block(first, line):
                depth += 1
            elif depth == 1 and current is not None:
                apply_field(current, first, tokens[1:])
    return templates


# -------------------------------------------------------------------------------------------------
# The map file
# -------------------------------------------------------------------------------------------------

@dataclass
class Placement:
    template: str
    x: float
    y: float
    z: float                                  # above the ground, as the map stores it
    angle: float
    flags: int


class PathMap(mapstyle.StyleMap):
    """StyleMap's height map, cliff bits and chunk walk, keeping what the pathfinder reads and
    StyleMap drops: each object's height and angle, every named waypoint, and the water areas with
    the integer points PolygonTrigger::pointInTrigger tests."""

    def __init__(self, data: bytes) -> None:
        self.placements: list[Placement] = []
        self.waypoints: dict[str, Point] = {}
        self.water_polygons: list[tuple[list[tuple[int, int]], int]] = []
        super().__init__(data)

    def _read_objects(self, reader: mapstyle.ChunkReader, end: int) -> None:
        for name, version, _ in reader.chunks(end):
            if name != "Object":
                continue
            x, y, z, angle = (reader.read_real() for _ in range(4))
            flags = reader.read_int()
            template = reader.read_ascii()
            entries = reader.read_dict() if version >= 2 else {}
            if "waypointID" in entries:
                waypoint = entries.get("waypointName", "")
                self.waypoints[waypoint] = (x, y)
                if waypoint.startswith(START_PREFIX) and waypoint.endswith(START_SUFFIX):
                    self.starts.append((int(waypoint.split("_")[1]), (x, y)))
                continue
            self.placements.append(Placement(template, x, y, z, angle, flags))

    def _read_triggers(self, reader: mapstyle.ChunkReader, version: int) -> None:
        """PolygonTrigger::ParsePolygonTriggersDataChunk; the water areas, river ones included."""
        for _ in range(reader.read_int()):
            reader.read_ascii()                                    # name
            if version >= 4:
                reader.read_ascii()                                # layer
            reader.read_int()                                      # id
            water = reader.read_byte() != 0 if version >= 2 else False
            if version >= 3:
                reader.read_byte()                                 # river
                reader.read_int()                                  # river start
            points = [(reader.read_int(), reader.read_int(), reader.read_int())
                      for _ in range(reader.read_int())]
            if water and points:
                self.water_polygons.append(([(px, py) for px, py, _ in points], points[0][2]))

    def cliff_state(self, x: int, y: int) -> bool:
        """WorldHeightMapData::getCliffState. A version 7 blend chunk stored rows (width+1)/8 bytes
        long and parseBlendTileCells copies them into wider rows, so bits past the stored row are
        clear. A map with no cliff bits gets initCliffFlagsFromHeights."""
        if self.cliff_bits is None:
            if x >= self.width - 1 or y >= self.height - 1:
                return False
            return self.cell_span_world(x, y) > CLIFF_SLOPE_LIMIT
        bits, row = self.cliff_bits
        if x >> 3 >= row:
            return False
        return bool(bits[y * row + (x >> 3)] & (1 << (x & 7)))


def point_in_polygon(points: list[tuple[int, int]], x: int, y: int) -> bool:
    """PolygonTrigger::pointInTrigger (PolygonTrigger.cpp:530), bounds test included."""
    xs = [p[0] for p in points]
    ys = [p[1] for p in points]
    if x < min(xs) or y < min(ys) or x > max(xs) or y > max(ys):
        return False
    inside = False
    for index, (x1, y1) in enumerate(points):
        x2, y2 = points[(index + 1) % len(points)]
        if y1 == y2:
            continue
        if (y1 < y and y2 < y) or (y1 >= y and y2 >= y) or (x1 < x and x2 < x):
            continue
        if x1 + ((x2 - x1) * (y - y1)) / (y2 - y1) >= x:
            inside = not inside
    return inside


def terrain_cells(path_map: PathMap, width: int, height: int) -> tuple[bytearray, bytearray]:
    """Per cell, Pathfinder::classifyMapCell's two terrain questions (AIPathfind.cpp:6014): a cliff
    by the cliff bit of the cell's low corner (TerrainHeightSampling::isCliffCell, :423), water when
    any of its four corners is under the highest water area holding it (TerrainLogic::getWaterHandle
    and isUnderwater, TerrainLogic.cpp:2218)."""
    border = path_map.border
    last_x, last_y = path_map.width - 2, path_map.height - 2
    cliff = bytearray(width * height)
    for j in range(height):
        hy = min(max(j + border, 0), last_y)
        for i in range(width):
            cliff[j * width + i] = path_map.cliff_state(min(max(i + border, 0), last_x), hy)

    # ponytail: no grid water (TerrainLogic's m_waterGrid) and no water a script moves later
    underwater = bytearray((width + 1) * (height + 1))
    if path_map.water_polygons:
        for vy in range(height + 1):
            for vx in range(width + 1):
                wx, wy = int(vx * CELL_SIZE), int(vy * CELL_SIZE)
                surface, found = 0, False
                for points, z in path_map.water_polygons:
                    if z >= surface and point_in_polygon(points, wx, wy):
                        surface, found = z, True
                if not found:
                    continue
                hx = min(max(vx + border, 0), path_map.width - 1)
                hy = min(max(vy + border, 0), path_map.height - 1)
                if path_map.height_at(hx, hy) * MAP_HEIGHT_SCALE < surface:
                    underwater[vy * (width + 1) + vx] = 1
    water = bytearray(width * height)
    row = width + 1
    for j in range(height):
        for i in range(width):
            v = j * row + i
            water[j * width + i] = underwater[v] | underwater[v + 1] | underwater[v + row] | underwater[v + row + 1]
    return cliff, water


# -------------------------------------------------------------------------------------------------
# The engine's classification passes
# -------------------------------------------------------------------------------------------------

def classify_map(cliff: bytearray, water: bytearray, width: int, height: int
                 ) -> tuple[bytearray, bytearray]:
    """Pathfinder::classifyMap (AIPathfind.cpp:6117): classifyMapCell (water beats cliff), then the
    clear ring round every cliff is pinched, the pinched cells become cliff, and the clear ring round
    the grown cliff is pinched again."""
    types = bytearray(ENGINE_WATER if water[index] else ENGINE_CLIFF if cliff[index] else ENGINE_CLEAR
                      for index in range(width * height))
    pinched = bytearray(width * height)

    def pinch_round_cliffs() -> None:
        for j in range(height):
            for i in range(width):
                if not types[j * width + i] & ENGINE_CLIFF:
                    continue
                for k in range(max(i - 1, 0), min(i + 2, width)):
                    for m in range(max(j - 1, 0), min(j + 2, height)):
                        if types[m * width + k] == ENGINE_CLEAR:
                            pinched[m * width + k] = 1

    pinch_round_cliffs()
    for index in range(width * height):
        if pinched[index] and types[index] == ENGINE_CLEAR:
            types[index] = ENGINE_CLIFF
    pinch_round_cliffs()
    return types, pinched


@dataclass
class Bounds:
    lo_x: int
    lo_y: int
    hi_x: int
    hi_y: int

    def take(self, cx: int, cy: int) -> None:
        self.lo_x, self.lo_y = min(self.lo_x, cx), min(self.lo_y, cy)
        self.hi_x, self.hi_y = max(self.hi_x, cx), max(self.hi_y, cy)


def stamp_cell(types: bytearray, index: int) -> None:
    """PathfindCell::setTypeAsObstacle (AIPathfind.cpp:1991): only clear (or impassable) ground."""
    if types[index] == ENGINE_CLEAR:
        types[index] = ENGINE_OBSTACLE


def stamp_rectangle(types: bytearray, width: int, height: int, x: float, y: float, angle: float,
                    half_x: float, half_y: float, offset_x: float, bounds: Bounds) -> None:
    """The stepped rotated box of internal_classifyObjectFootprint's GEOMETRY_BOX (:5717) and of
    classifyFence (:5508), which is the same walk with the fence's offset along its length. The
    last column and row of the map are never stamped (cx < m_extent.hi.x)."""
    # ponytail: math.cos/sin and doubles where the engine has DetTrig tables and floats; a sample
    # that lands on a cell edge can fall the other way
    c, s = math.cos(angle), math.sin(angle)
    ydx, ydy = s * FOOTPRINT_STEP, -c * FOOTPRINT_STEP
    xdx, xdy = c * FOOTPRINT_STEP, s * FOOTPRINT_STEP
    steps_x = math.ceil(2.0 * half_x / FOOTPRINT_STEP)
    steps_y = math.ceil(2.0 * half_y / FOOTPRINT_STEP)
    row_x = x - offset_x * c - half_y * s
    row_y = y + half_y * c - offset_x * s
    for _ in range(steps_y):
        px, py = row_x, row_y
        for _ in range(steps_x):
            cx = math.floor((px + SNAP) / CELL_SIZE)
            cy = math.floor((py + SNAP) / CELL_SIZE)
            if 0 <= cx < width - 1 and 0 <= cy < height - 1:
                stamp_cell(types, cy * width + cx)
                bounds.take(cx, cy)
            px += xdx
            py += xdy
        row_x += ydx
        row_y += ydy


def stamp_circle(types: bytearray, width: int, height: int, x: float, y: float, radius: float,
                 bounds: Bounds) -> None:
    """GEOMETRY_SPHERE and GEOMETRY_CYLINDER (:5775): every cell whose centre is within the radius
    plus 0.4 of a cell. bottomRight is an Int assigned a Real, so it truncates toward zero."""
    left = math.floor(0.5 + (x - radius) / CELL_SIZE) - 1
    top = math.floor(0.5 + (y - radius) / CELL_SIZE) - 1
    size = radius / CELL_SIZE + CIRCLE_SLACK
    reach = size * size
    right, bottom = int(left + 2 * size + 2), int(top + 2 * size + 2)
    centre_x, centre_y = x / CELL_SIZE, y / CELL_SIZE
    for j in range(top, bottom):
        for i in range(left, right):
            dx, dy = i + 0.5 - centre_x, j + 0.5 - centre_y
            if dx * dx + dy * dy <= reach and 0 <= i < width - 1 and 0 <= j < height - 1:
                stamp_cell(types, j * width + i)
                bounds.take(i, j)


def repinch(types: bytearray, pinched: bytearray, width: int, height: int, bounds: Bounds) -> None:
    """The end of internal_classifyObjectFootprint (:5836-6001): two cells round the footprint, every
    pinched flag is redone - a clear cell with fewer than 2 clear orthogonal or 4 clear neighbours
    in all - and MARK_BORDER_PINCHED adds any clear cell orthogonally beside an obstacle."""
    lo_x, lo_y = max(bounds.lo_x - PINCH_MARGIN, 0), max(bounds.lo_y - PINCH_MARGIN, 0)
    hi_x, hi_y = min(bounds.hi_x + PINCH_MARGIN, width - 1), min(bounds.hi_y + PINCH_MARGIN, height - 1)
    for j in range(lo_y, hi_y + 1):
        for i in range(lo_x, hi_x + 1):
            index = j * width + i
            pinched[index] = 0
            if types[index] != ENGINE_CLEAR:
                continue
            total = orthogonal = 0
            for k in range(max(i - 1, 0), min(i + 2, width)):
                for m in range(max(j - 1, 0), min(j + 2, height)):
                    if (k != i or m != j) and types[m * width + k] == ENGINE_CLEAR:
                        total += 1
                        orthogonal += k == i or m == j
            if orthogonal < MIN_CLEAR_ORTHOGONAL or total < MIN_CLEAR_NEIGHBOURS:
                pinched[index] = 1
    for j in range(lo_y, hi_y + 1):
        for i in range(lo_x, hi_x + 1):
            index = j * width + i
            if types[index] != ENGINE_CLEAR:
                continue
            for k, m in ((i - 1, j), (i + 1, j), (i, j - 1), (i, j + 1)):
                if 0 <= k < width and 0 <= m < height and types[m * width + k] == ENGINE_OBSTACLE:
                    pinched[index] = 1
                    break


def skip_reason(placement: Placement, template: Template | None) -> str | None:
    """Why a map object leaves no footprint, or None when it stamps one. GameLogic::startNewGame's
    creation loop (GameLogic.cpp:2035) in a network game, then classifyObjectFootprint's screen
    (AIPathfind.cpp:5623)."""
    if placement.flags & (FLAG_BRIDGE_FLAGS | FLAG_ROAD_FLAGS):
        return "road or bridge piece"
    if template is None:
        return "no template"
    kinds = template.kinds
    # ponytail: bridges and wall tops are pathfind layers of their own; the ground under them is
    # left as the terrain made it, water and all
    if template.is_bridge or "WALK_ON_TOP_OF_WALL" in kinds:
        return "bridge layer"
    if "OPTIMIZED_TREE" in kinds:
        return "tree"
    if "PROP" in kinds:
        return "prop"
    if "CLEARED_BY_BUILD" in kinds and template.fence_width == 0.0:
        return "fluff"                     # forceFluffToProp, always on in a network game
    for kind, reason in (("MINE", "mine"), ("PROJECTILE", "projectile"), ("BRIDGE_TOWER", "bridge tower")):
        if kind in kinds:
            return reason
    if template.fence_width > 0.0 and "DEFENSIVE_WALL" not in kinds:
        return None
    if "STRUCTURE" not in kinds:
        return "not a structure"
    if "IMMOBILE" not in kinds:
        return "mobile"                    # Object::isMobile, nothing is disabled at map load
    if template.is_small:
        return "small"
    if placement.z > CELL_SIZE and "BLAST_CRATER" not in kinds:
        return "in the air"
    return None


def stamp_object(types: bytearray, pinched: bytearray, width: int, height: int,
                 placement: Placement, template: Template) -> None:
    """classifyFence for a fence, internal_classifyObjectFootprint and its pinched pass otherwise."""
    bounds_x = math.floor((placement.x + SNAP) / CELL_SIZE)
    bounds_y = math.floor((placement.y + SNAP) / CELL_SIZE)
    bounds = Bounds(bounds_x, bounds_y, bounds_x, bounds_y)
    if template.fence_width > 0.0 and "DEFENSIVE_WALL" not in template.kinds:
        stamp_rectangle(types, width, height, placement.x, placement.y, placement.angle,
                        template.fence_width / 2.0, FENCE_HALF_DEPTH, template.fence_offset, bounds)
        return
    if template.geometry == "BOX":
        stamp_rectangle(types, width, height, placement.x, placement.y, placement.angle,
                        template.major_radius, template.minor_radius, template.major_radius, bounds)
    else:
        stamp_circle(types, width, height, placement.x, placement.y, template.major_radius, bounds)
    repinch(types, pinched, width, height, bounds)


# -------------------------------------------------------------------------------------------------
# Loading
# -------------------------------------------------------------------------------------------------

@dataclass
class LoadedMap:
    """What pathlab's Grid needs (width, height, kind, pinched; index cy * width + cx, a cell of
    CELL_SIZE world units, world (0, 0) at the playable corner) and what a scenario is staged from."""
    name: str
    width: int
    height: int
    kind: bytearray
    pinched: bytearray
    engine_kind: bytearray                     # PathfindCell::CellType, to tell water from cliff
    starts: list[Point]                        # world units, player 1 first
    waypoints: dict[str, Point]
    stamped: Counter[str]
    skipped: Counter[str]
    skip_reasons: Counter[str]
    seconds: float


def game_dir_of(game_dir: str | None) -> str:
    return game_dir or os.environ.get(GAME_DIR_VARIABLE) or DEFAULT_GAME_DIR


def read_members(archive: str, wanted: Iterable[str]) -> dict[str, bytes]:
    """The archive members whose lower-cased, backslashed path is in `wanted` or starts with one
    ending in a backslash."""
    wanted = list(wanted)
    found: dict[str, bytes] = {}
    with open(archive, "rb") as handle:
        for stored, offset, size in list(mapstyle.bigfile._read_index(handle)):
            path = stored.lower().replace("/", "\\")
            if any(path == want or (want.endswith("\\") and path.startswith(want)) for want in wanted):
                handle.seek(offset)
                found[path] = handle.read(size)
    return found


def load_templates(game_dir: str) -> dict[str, Template]:
    """Default Object.ini, then Data\\INI\\Object in sorted order, as INI::loadDirectory reads it."""
    # ponytail: retail INIZH.big only; the fork's loose patches in Run/Data/INI touch faction units,
    # no map object, and Patch*.big carries no object INI
    members = read_members(os.path.join(game_dir, INI_ARCHIVE), [DEFAULT_TEMPLATE_INI, OBJECT_INI_FOLDER])
    order = [DEFAULT_TEMPLATE_INI] + sorted(path for path in members if path.startswith(OBJECT_INI_FOLDER))
    return parse_templates(members[path].decode("latin-1") for path in order if path in members)


def read_map_bytes(name: str, game_dir: str) -> bytes:
    if name.lower().endswith(".map") and os.path.isfile(name):
        with open(name, "rb") as handle:
            return handle.read()
    member = "maps\\%s\\%s.map" % (name.lower(), name.lower())
    found = read_members(os.path.join(game_dir, MAPS_ARCHIVE), [member])
    if member not in found:
        raise FileNotFoundError("%s has no map %r" % (MAPS_ARCHIVE, name))
    return found[member]


def load_map(name: str = DEFAULT_MAP, game_dir: str | None = None) -> LoadedMap:
    """Read map `name` from the install and classify it the way Pathfinder::newMap and the map
    object loop do at the start of a network game."""
    started = time.perf_counter()
    game_dir = game_dir_of(game_dir)
    path_map = PathMap(read_map_bytes(name, game_dir))
    templates = load_templates(game_dir)

    # ponytail: the first boundary; newMap takes the largest of several, which only campaign maps have
    width, height = path_map.playable_x, path_map.playable_y
    cliff, water = terrain_cells(path_map, width, height)
    types, pinched = classify_map(cliff, water, width, height)

    stamped: Counter[str] = Counter()
    skipped: Counter[str] = Counter()
    reasons: Counter[str] = Counter()
    # ponytail: objects a script or an object's own creation list spawns later are not on the map
    for placement in path_map.placements:
        template = templates.get(placement.template.lower())
        reason = skip_reason(placement, template)
        if reason is None:
            stamp_object(types, pinched, width, height, placement, template)
            stamped[placement.template] += 1
        else:
            skipped[placement.template] += 1
            reasons[reason] += 1

    kind = bytearray(TO_PATHLAB[engine_type] for engine_type in types)
    # classifyMap leaves the flag on the ring it turned to cliff; only a clear cell's flag means anything
    clear_pinched = bytearray(flag if engine_type == ENGINE_CLEAR else 0 for flag, engine_type in zip(pinched, types))
    return LoadedMap(os.path.splitext(os.path.basename(name))[0], width, height, kind, clear_pinched, types,
                     list(path_map.starts), path_map.waypoints, stamped, skipped, reasons,
                     time.perf_counter() - started)


# -------------------------------------------------------------------------------------------------
# Looking at it
# -------------------------------------------------------------------------------------------------

COLOURS = {ENGINE_CLEAR: (196, 178, 140), ENGINE_WATER: (52, 96, 170), ENGINE_CLIFF: (92, 84, 78),
           ENGINE_OBSTACLE: (178, 60, 50)}
PINCHED_COLOUR = (150, 136, 104)
START_COLOUR = (40, 230, 90)
WAYPOINT_COLOUR = (250, 240, 60)
START_MARK_CELLS = 3
PNG_SCALE = 3


def write_png(loaded: LoadedMap, path: str, scale: int = PNG_SCALE) -> None:
    """Cell types in colour, pinched clear ground darker, starts as green squares and the other named
    waypoints as yellow dots; north (+y) up, as WorldBuilder shows it."""
    width, height = loaded.width, loaded.height
    pixels = [[COLOURS[loaded.engine_kind[j * width + i]]
               if not (loaded.pinched[j * width + i] and loaded.engine_kind[j * width + i] == ENGINE_CLEAR)
               else PINCHED_COLOUR for i in range(width)] for j in range(height)]

    def mark(point: Point, reach: int, colour: tuple[int, int, int]) -> None:
        cx, cy = int(point[0] // CELL_SIZE), int(point[1] // CELL_SIZE)
        for j in range(cy - reach, cy + reach + 1):
            for i in range(cx - reach, cx + reach + 1):
                if 0 <= i < width and 0 <= j < height:
                    pixels[j][i] = colour

    for point in loaded.waypoints.values():
        mark(point, 0, WAYPOINT_COLOUR)
    for point in loaded.starts:
        mark(point, START_MARK_CELLS, START_COLOUR)

    rows = bytearray()
    for j in reversed(range(height)):
        line = b"".join(bytes(pixel) * scale for pixel in pixels[j])
        rows += (b"\0" + line) * scale

    def chunk(tag: bytes, body: bytes) -> bytes:
        return struct.pack(">I", len(body)) + tag + body + struct.pack(">I", zlib.crc32(tag + body))

    header = struct.pack(">IIBBBBB", width * scale, height * scale, 8, 2, 0, 0, 0)
    with open(path, "wb") as handle:
        handle.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header) + chunk(b"IDAT", zlib.compress(bytes(rows)))
                     + chunk(b"IEND", b""))


def report(loaded: LoadedMap) -> str:
    counts = Counter(loaded.kind)
    lines = ["%s: %d x %d cells, %.2f s" % (loaded.name, loaded.width, loaded.height, loaded.seconds),
             "  clear %d  obstacle %d  impassable %d (water %d, cliff %d)  pinched %d" % (
                 counts[CLEAR], counts[OBSTACLE], counts[IMPASSABLE], loaded.engine_kind.count(ENGINE_WATER),
                 loaded.engine_kind.count(ENGINE_CLIFF), sum(loaded.pinched)),
             "  stamped %d objects: %s" % (sum(loaded.stamped.values()),
                                           ", ".join("%s %d" % item for item in loaded.stamped.most_common(6))),
             "  skipped %d: %s" % (sum(loaded.skipped.values()),
                                   ", ".join("%s %d" % item for item in loaded.skip_reasons.most_common())),
             "  most skipped: " + ", ".join("%s %d" % item for item in loaded.skipped.most_common(6)),
             "  starts: " + ", ".join("(%.0f, %.0f)" % start for start in loaded.starts),
             "  waypoints: %d" % len(loaded.waypoints)]
    return "\n".join(lines)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("map", nargs="?", default=DEFAULT_MAP)
    parser.add_argument("--game-dir", help="the folder holding MapsZH.big and INIZH.big")
    parser.add_argument("--png", help="write the grid as a picture here")
    args = parser.parse_args()
    loaded = load_map(args.map, args.game_dir)
    print(report(loaded))
    if args.png:
        write_png(loaded, args.png)


if __name__ == "__main__":
    main()
