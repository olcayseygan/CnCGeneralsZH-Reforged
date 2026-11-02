#!/usr/bin/env python3
"""Fill a generated map's skeleton with a shipped map's own ground: patch quilting guided by height.

The generator lays out the map: the starts, the ring of plateaus, one ramp down each, the ridges
between them and the bowl in the middle. This prototype keeps that layout, heights and cliffs
included, and lays the exemplar's local character over it (Twilight Flame unless told otherwise):
its height detail, its painted textures and blends, its trees, rocks, tents and sounds.

How. Both maps get one guide channel, the height normalised so the bowl's floor is 0 and the bases'
plateau is 1 (blurred, so the guide is the layout rather than the roll), plus the cliff flag. The
target is cut into 48 cell patches overlapping by 12, in rows. Each patch picks the exemplar patch
whose guide has the same shape (a whole-patch lift is allowed, at a price, so a slope can come from
any level) and whose overlap agrees best with what is already placed: image quilting (Efros and
Freeman 2001) guided the way texture-by-numbers is. The seam inside the overlap is a minimum cut.
The search tries every fourth offset over every fourth cell, then every offset round the sixteen
best over every cell. What comes out is one source cell per target cell, and every layer is read
through that map:

  height   the skeleton's, plus the exemplar's detail (its normalised height less a 21 cell blur of
           it), none within RIM_CELLS of a skeleton cliff, where an exemplar edge laid a cell off
           would double the wall. The pads round the starts, docks and derricks and the skeleton's
           walkable slopes (its ramps, its ways up a ridge) keep the skeleton's height, feathered.
  texture  the exemplar's class a cell, its tile worked out again for where the cell landed (the
           exemplar's tiles are exactly tileIndexForCell of class and cell), the same for both blend
           layers, so the tiling runs on across a seam. Its own texture table goes in with them.
  cliffs   from the final heights: the exemplar's painted cliff bits are exactly its heights'.
  scenery  every tree, rock, tent, sound and hut whose source cell landed off the pads.

Before the quilting the skeleton is given the exemplar's large features, which no 48 cell patch
holds, at the sizes mapfeatures.py measures them and in a layout of the seed's (compose): its high
ground and ways down when both maps raise their bases, a canyon through the middle with a mesa in it
and the mesa's bridges, a river with its bridges (each the exemplar's own pair of bridge objects),
its towns copied whole with every building, road and tree in their boxes, its tech buildings. What
it cannot build yet it names. The trees are counted: the quilting pays for a patch whose trees miss
the exemplar's density on the ground where trees may stand (off pads, ramps, AI paths, feature
edges and water), and clumps of the exemplar's own trees make up what is still short.

The piles go where the exemplar keeps them when it keeps them on high ground (Twilight's, between
two bases with a tent): a pair at the bearing halfway to the next seat ("mid") and a pair a third of
the way ("field"), instead of the ramp's foot.

A generated map's name carries the CRC of the exemplar's stored bytes, so two machines can tell
they built from the same file.

Why not wave function collapse: `wfcprobe` measures it. Cut to six height bands and four texture
groups the exemplar already shows 7101 distinct 3x3 patterns (11302 with its 21 textures), an
adjacency table of 200 MB, and with the skeleton's band as the constraint a 48x48 crop contradicts
before the first collapse, every try: 0.5% of the skeleton's cells hold a band pattern Twilight never
shows, and one of them is enough. Softening the constraint turns it into the optimisation quilting
already is, and WFC hands back quantised symbols, so the height would want synthesising again.
Quilting copies real cells and is a fixed amount of work.

    python synthmap.py <skeleton.map> [...] [-o outdir] [--source maps.big] [--seed N]
    python synthmap.py table <exemplar.map> <map> [...]
    python synthmap.py render <out.png> <map> [...]
    python synthmap.py wfcprobe <skeleton.map>
    python synthmap.py selfcheck

A skeleton is what `test_gameengine.exe write_the_maps_RMG_DUMP_names` writes with RMG_DUMP set.
Output per skeleton, in a folder <name>_<exemplar>_<crc> that drops into Maps as it is: the .map
(raw CkMp, BlendTileData version 8 with the exemplar's texture table and the cliff bits, the
exemplar's lighting, a water area per river, a mapName), the map.str naming it, and the same name
.png, each cell its texture's average colour under the map's sun. The line it prints
says whether every start still reaches every other, every dock, derrick and tech building, over
the bridges and round the water, and whether it would without the bridges.
"""

import argparse
import copy
import math
import os
import re
import struct
import sys
import time
import zlib
from collections import deque

import numpy as np
from PIL import Image, ImageDraw
from scipy import ndimage, sparse
from scipy.sparse import csgraph

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, "..", "..", "GeneralsMD", "Code", "Tools"))

import bigfile                                       # noqa: E402
import mapfeatures                                   # noqa: E402
import mapstyle                                      # noqa: E402
from mapscore import MAP_HEIGHT_SCALE, MAP_XY_FACTOR, CLIFF_WORLD_SPAN  # noqa: E402

RUN = os.path.join(HERE, "..", "..", "GeneralsMD", "Run")
SOURCE_BIG = os.path.join(RUN, "ZH_Generals", "maps.big")
SOURCE_MAP = "maps\\Twilight Flame\\Twilight Flame.map"

PATCH = 48                  # cells a side
OVERLAP = 12                # cells two neighbouring patches share
COARSE = 4                  # the first pass tries every fourth offset over every fourth cell
REFINE = 16                 # best coarse offsets whose neighbours the full comparison tries
DETAIL_RADIUS = 10          # cells: the exemplar's height finer than this blur is its detail
RIM_CELLS = 3               # cells round a skeleton cliff that take none of it
SHIFT_WEIGHT = 0.5          # lifting a source patch by the plateau's height, against one unit of guide
CLIFF_WEIGHT = 0.6          # a cliff where the guide has none, against one unit of height
OVERLAP_WEIGHT = 2.0        # disagreeing with what is already placed, against the guide
CLASS_WEIGHT = 0.15         # a texture class that differs in the overlap
REUSE_WEIGHT = 0.02         # every earlier use of the same source patch
JITTER = 0.04               # the seed's say among near-equal candidates
SEAM_BAND = 2               # cells either side of a seam whose height is smoothed
PAD_RADIUS = (24, 34)       # a start's pad keeps the skeleton's height inside the first, fades out by the second
SITE_RADIUS = (7, 12)       # the same round a dock or a derrick
SLOPE_REACH = 100           # cells from a start the skeleton's walkable slopes are kept
PILE_FLAT = 4               # cells round a pile levelled
TECH_FLAT = 8               # cells round a tech building levelled
MIN_RELIEF = 60.0           # bytes: the unit of t where the bases are not raised

# How the exemplar's features are built; their sizes come from its inventory (mapfeatures.py)
RAMP_BANK = 10.0            # cells a widened way down takes to meet the ground beside it
WALL_CELLS = 3.0            # a canyon or mesa wall, which is a cliff
MESA_RAMP = 22.0            # the slope off an end of a mesa
END_CELLS = 25.0            # the slope out of a canyon arm's end
FORD_AT = 0.6               # of the open middle's radius, where an arm's ford is
FORD_CELLS = 8.0            # half a ford's width
SHORE_CELLS = 6.0           # a river's bed to its waterline
VALLEY_CELLS = 20.0         # the waterline up to the banks
RIVER_TRIES = 6             # river layouts as good for the starts tried for the towns and tech they leave room for
RIVER_MIN = 0.3             # of the map's shorter side a river runs at least to be built as one
JUNCTION_CLEAR = 40.0       # cells between a branch's junction and a bridge
BRANCH_TURN = 0.6           # radians a branch leaves its main river at, at least
BRANCH_TRIES = 6            # branch courses tried, clearest of the sites first
BRANCH_SETTLE = 40.0        # cells off the junction before a branch winds
LAKE_CELLS = 200            # a smaller lake is a pond, not built
LAKE_POINTS = 24            # corners of a lake's water area
SEA_FLOOR = 8.0             # bytes: the lowest sea level, a bed under it to dig
COAST_WOBBLE = 8.0          # cells a coastline wanders off its straight course
CAUSEWAY_CELLS = 12.0       # half the width of the land joining an island start to the middle
ROAD_CLIMB = 0.5            # a road's cost a cell for each byte a cell the ground climbs, against 1 for flat
ROAD_LEAST = 6              # cells: a shorter piece of road is not laid
ROAD_SPACING = 2            # cells round a laid road where another road joins it and stops
ROAD_TOLERANCE = 1.5        # cells a simplified road may stray off the way it was found
ROAD_CLEAR = 3              # cells round a road no tree or rock stands
ROAD_MIN = 60.0             # cells of the exemplar's roads outside its towns, scaled, worth a network
COAST_EDGE = mapfeatures.COAST_EDGE
FULL_EDGE = mapfeatures.FULL_EDGE
RIVER_SLOPE = 3.0          # bytes a cell the ground past a river's banks may rise (a cliff is 15.7)
TOWN_MARGIN = 10            # cells round a town's buildings copied with it
TOWN_STEP = 8               # cells between the places tried for a town
TOWN_RELIEF = 50            # bytes of rise under a town's box that levelling it may take away
SCENERY_WEIGHT = 0.05       # a tree off the patch's share of the exemplar's density, against the guide
PATH_CELLS = 4              # no tree this close to an AI path
BRIDGE_CLEAR = 6            # no scenery this close to a bridge's end
CLUMP_CELLS = 5             # the exemplar's trees this close to one go down together
TREE_TRIES = 40             # clump placements tried a tree still wanted

SCENERY = re.compile(r"^(Tree|Rocks|RockCluster|Tent|Amb_|AmbientWind|WoodTower|QuonsetHut)")
SKELETON_SCENERY = re.compile(r"^(Tree|Rocks|RockCluster|AmbientWaves)")   # its lakes' waves go with its lakes
STANDING = re.compile(r"^(Tent|WoodTower|QuonsetHut)")      # scenery that is a building, not off a cliff


#-----------------------------------------------------------------------------------------------
# Chunks
#-----------------------------------------------------------------------------------------------

class RawMap:
    """A CkMp file as its name table and its top level chunks, bodies untouched."""

    def __init__(self, data):
        data = mapstyle.unpack_map(data)
        if data[:4] != b"CkMp":
            raise ValueError("not a CkMp map")
        count = struct.unpack_from("<i", data, 4)[0]
        pos = 8
        self.ids = {}
        for _ in range(count):
            length = data[pos]
            name = data[pos + 1:pos + 1 + length].decode("latin-1")
            pos += 1 + length
            self.ids[name] = struct.unpack_from("<I", data, pos)[0]
            pos += 4
        self.header = data[:pos]
        self.chunks = split_chunks(data, pos, len(data))
        self.names = {v: k for k, v in self.ids.items()}

    def body(self, name):
        for cid, version, body in self.chunks:
            if self.names[cid] == name:
                return version, body
        raise KeyError(name)

    def replace(self, name, version, body):
        self.chunks = [(cid, version, body) if self.names[cid] == name else (cid, v, b)
                       for cid, v, b in self.chunks]

    def id_of(self, name):
        """A name's id in the table, a new one at the end if the file has none."""
        if name not in self.ids:
            self.ids[name] = max(self.ids.values()) + 1
            self.names[self.ids[name]] = name
        return self.ids[name]

    def bytes(self):
        table = b"".join(struct.pack("<B", len(name)) + name.encode("latin-1") + struct.pack("<I", i)
                         for name, i in sorted(self.ids.items(), key=lambda kv: kv[1]))
        return b"CkMp" + struct.pack("<i", len(self.ids)) + table + join_chunks(self.chunks)


DICT_SIZES = {0: 1, 1: 4, 2: 4}                      # bool, int, real; ascii and unicode carry a length


def dict_entries(body, pos, names):
    """A Dict as (key, type, value bytes) entries and where it ends."""
    entries = []
    count = struct.unpack_from("<H", body, pos)[0]
    pos += 2
    for _ in range(count):
        key, kind = divmod(struct.unpack_from("<I", body, pos)[0], 256)
        pos += 4
        if kind in DICT_SIZES:
            size = DICT_SIZES[kind]
        else:
            size = 2 + struct.unpack_from("<H", body, pos)[0] * (2 if kind == 4 else 1)
        entries.append((names[key], kind, body[pos:pos + size]))
        pos += size
    return entries, pos


def encode_dict(entries, raw):
    return struct.pack("<H", len(entries)) + b"".join(
        struct.pack("<I", raw.id_of(key) << 8 | kind) + value for key, kind, value in entries)


def object_entries(body, names):
    return dict_entries(body, ascii_at(body, 20)[1], names)[0]


def ascii_value(text):
    return struct.pack("<H", len(text)) + text.encode("latin-1")


def moved_object(body, names, raw, cx, cy, serial):
    """An Object body from another file, standing at cell (cx, cy) of this one's playable area,
    neutral, nameless (no script of ours knows it) and with a unique id of its own."""
    template, end = ascii_at(body, 20)
    entries, _ = dict_entries(body, end, names)
    fresh = {"uniqueID": ascii_value("%s synth%d" % (template, serial)),
             "originalOwner": ascii_value("team"), "objectName": ascii_value("")}
    entries = [(key, kind, fresh.get(key, value)) for key, kind, value in entries]
    return (raw.id_of("Object"), 3, struct.pack("<2f", cx * MAP_XY_FACTOR, cy * MAP_XY_FACTOR) +
            body[8:end] + encode_dict(entries, raw))


def waypoint_paths(raw, shape, border):
    """Cells on the lines between linked waypoints (WaypointsList), the AI's ways about the map."""
    _, objects = raw.body("ObjectsList")
    where = {}
    for _, _, body in split_chunks(objects, 0, len(objects)):
        x, y, _, _ = object_head(body)
        for key, _, value in object_entries(body, raw.names):
            if key == "waypointID":
                where[struct.unpack("<i", value)[0]] = (y / MAP_XY_FACTOR + border, x / MAP_XY_FACTOR + border)
    _, links = raw.body("WaypointsList")
    out = np.zeros(shape, bool)
    for i in range(struct.unpack_from("<i", links, 0)[0]):
        first, second = struct.unpack_from("<2i", links, 4 + 8 * i)
        out |= line_cells(where[first], where[second], shape)
    return out


def line_cells(a, z, shape):
    out = np.zeros(shape, bool)
    steps = max(int(math.hypot(z[0] - a[0], z[1] - a[1])), 1)
    for s in range(steps + 1):
        y = int(a[0] + (z[0] - a[0]) * s / steps)
        x = int(a[1] + (z[1] - a[1]) * s / steps)
        out[min(max(y, 0), shape[0] - 1), min(max(x, 0), shape[1] - 1)] = True
    return out


def water_triggers(version, body, areas, border):
    """PolygonTriggers without the skeleton's own water areas (compose filled its lakes in) and
    with a water area per (points (y, x) in grid cells, surface bytes) added, each with an id none
    of the others has. The AI's perimeters and every other trigger stay as they are."""
    if version != 4:
        raise ValueError("PolygonTriggers version %d, expected 4" % version)
    pos, ids, out, count = 4, [], b"", 0
    for _ in range(struct.unpack_from("<i", body, 0)[0]):
        begin = pos
        pos = ascii_at(body, pos)[1]
        pos = ascii_at(body, pos)[1]
        ids.append(struct.unpack_from("<i", body, pos)[0])
        is_water = body[pos + 4]
        pos += 4 + 1 + 1 + 4
        pos += 4 + 12 * struct.unpack_from("<i", body, pos)[0]
        if not is_water:
            out += body[begin:pos]
            count += 1
    for number, (points, surface) in enumerate(areas):
        out += ascii_value("Water%d" % (number + 1)) + ascii_value("")
        out += struct.pack("<iBBii", max(ids + [0]) + 1 + number, 1, 0, 0, len(points))
        z = int(surface * MAP_HEIGHT_SCALE + 0.5)
        out += b"".join(struct.pack("<3i", int((x - border) * MAP_XY_FACTOR + 0.5),
                                    int((y - border) * MAP_XY_FACTOR + 0.5), z) for y, x in points)
    return struct.pack("<i", count + len(areas)) + out


def split_chunks(data, pos, end):
    out = []
    while pos + 10 <= end:
        cid, version, size = struct.unpack_from("<IHi", data, pos)
        out.append((cid, version, data[pos + 10:pos + 10 + size]))
        pos += 10 + size
    return out


def join_chunks(chunks):
    return b"".join(struct.pack("<IHi", cid, version, len(body)) + body
                    for cid, version, body in chunks)


def ascii_at(data, pos):
    length = struct.unpack_from("<H", data, pos)[0]
    return data[pos + 2:pos + 2 + length].decode("latin-1"), pos + 2 + length


def object_head(body):
    """x, y (world), angle, template of an Object chunk body."""
    x, y, _, angle = struct.unpack_from("<4f", body, 0)
    template, _ = ascii_at(body, 20)
    return x, y, angle, template


def blend_cells(version, body, width, height):
    """The per-cell arrays of a BlendTileData body and the rest of it, which is the texture table."""
    size = struct.unpack_from("<i", body, 0)[0]
    pos = 4
    layers = []
    for present in (True, True, version >= 6, version >= 5):
        if present:
            layers.append(np.frombuffer(body, "<i2", size, pos).reshape(height, width).copy())
            pos += size * 2
        else:
            layers.append(np.zeros((height, width), np.int16))
    cliff = np.zeros((height, width), bool)
    if version >= 7:
        row = (width + 1) // 8 if version == 7 else (width + 7) // 8
        bits = np.frombuffer(body, np.uint8, row * height, pos).reshape(height, row)
        cliff = np.unpackbits(bits, axis=1, bitorder="little")[:, :width].astype(bool)
        if cliff.shape[1] < width:
            cliff = np.pad(cliff, ((0, 0), (0, width - cliff.shape[1])))
        pos += row * height
    return layers, cliff, body[pos:]


def texture_table(tail):
    """WorldHeightMap::ParseBlendTileData past the cells: the table up to the blend entries, and
    each blend entry as (tile, the five flag bytes and the long diagonal, custom edge class)."""
    bitmap, blended, cliffs, classes = struct.unpack_from("<4i", tail, 0)
    pos = 16
    for _ in range(classes):
        pos += 16
        pos = ascii_at(tail, pos)[1]
    edge_tiles, edge_classes = struct.unpack_from("<2i", tail, pos)
    pos += 8
    for _ in range(edge_classes):
        pos += 12
        pos = ascii_at(tail, pos)[1]
    head = tail[12:pos]                         # from the class count on
    entries = [(0, b"\0" * 6, -1)]
    for _ in range(blended - 1):
        tile, flags, custom, _ = struct.unpack_from("<i6sii", tail, pos)
        entries.append((tile, flags, custom))
        pos += 18
    return bitmap, head, entries


def tile_for(cls, first, width, y, x):
    """The tile WorldBuilder paints a class with at a cell (the generator's tileIndexForCell)."""
    w = width[cls]
    return ((first[cls] + (x // 2) % w + w * ((y // 2) % w)) << 2) + 2 * (y & 1) + (x & 1)


#-----------------------------------------------------------------------------------------------
# The two maps
#-----------------------------------------------------------------------------------------------

def box(a, r):
    """Mean over a (2r+1) square, edges clamped."""
    k = 2 * r + 1
    c = np.pad(np.pad(a.astype(np.float64), r, mode="edge").cumsum(0).cumsum(1), ((1, 0), (1, 0)))
    return (c[k:, k:] - c[:-k, k:] - c[k:, :-k] + c[:-k, :-k]) / (k * k)


def span_cliff(h):
    """WorldHeightMapData::initCliffFlagsFromHeights: a cell whose corners differ by more than the
    cliff span (mapscore's CLIFF_WORLD_SPAN) is a cliff."""
    h = h.astype(np.int32)
    corners = np.stack([h[:-1, :-1], h[:-1, 1:], h[1:, :-1], h[1:, 1:]])
    span = (corners.max(0) - corners.min(0)) * MAP_HEIGHT_SCALE
    out = np.zeros(h.shape, bool)
    out[:-1, :-1] = span > CLIFF_WORLD_SPAN
    return out


def walkable(cliff):
    """Pathfinder's one-cell growth of every cliff."""
    grown = cliff.copy()
    grown[1:] |= cliff[:-1]
    grown[:-1] |= cliff[1:]
    g2 = grown.copy()
    g2[:, 1:] |= grown[:, :-1]
    g2[:, :-1] |= grown[:, 1:]
    return ~g2


def reach(passable, start):
    """Cells walkable from one cell, four ways."""
    h, w = passable.shape
    seen = np.zeros_like(passable)
    sy, sx = start
    seen[sy, sx] = True
    queue = deque([(sy, sx)])
    while queue:
        y, x = queue.popleft()
        for ny, nx in ((y + 1, x), (y - 1, x), (y, x + 1), (y, x - 1)):
            if 0 <= ny < h and 0 <= nx < w and passable[ny, nx] and not seen[ny, nx]:
                seen[ny, nx] = True
                queue.append((ny, nx))
    return seen


class Grid:
    """A map's heights and its two levels: the bowl's floor (the median of the middle, as
    mapstyle measures it) and the bases' plateau (the median start). t is the height over the
    bowl's floor in units of the plateau's rise, or of MIN_RELIEF where the bases are not raised."""

    def __init__(self, data):
        self.m = mapstyle.StyleMap(data)
        m = self.m
        self.h = np.frombuffer(bytes(m.heights), np.uint8).reshape(m.height, m.width).astype(np.int32)
        self.starts = [(int(y + 0.5) + m.border, int(x + 0.5) + m.border) for x, y in m.starts]
        self.plateau = float(np.median([np.median(self.h[y - 3:y + 4, x - 3:x + 4])
                                        for y, x in self.starts]))
        cy = m.border + m.playable_y / 2.0
        cx = m.border + m.playable_x / 2.0
        yy, xx = np.mgrid[0:m.height, 0:m.width]
        middle = np.hypot(xx + 0.5 - cx, yy + 0.5 - cy) <= 0.25 * m.playable
        self.bowl = float(np.median(self.h[middle]))
        if self.plateau - self.bowl <= MIN_RELIEF:
            # bases not raised: the ground level is the map's, not its middle's, which on Tournament
            # Lake is a mountain 160 bytes over the grass and put the quilting's level on its rock
            play = (slice(m.border, m.border + m.playable_y), slice(m.border, m.border + m.playable_x))
            self.bowl = float(np.median(self.h[play]))
        self.centre = (cy, cx)
        self.scale = max(self.plateau - self.bowl, MIN_RELIEF)
        self.t = np.clip((self.h - self.bowl) / self.scale, -0.6, 2.2)
        self.guide = box(self.t, 2)

    def set_heights(self, h):
        self.h = np.clip(np.rint(h), 0, 255).astype(np.int32)
        self.t = np.clip((self.h - self.bowl) / self.scale, -0.6, 2.2)
        self.guide = box(self.t, 2)


def load_exemplar(path, inner):
    data = mapstyle.load_bytes(path, inner)
    grid = Grid(data)
    raw = RawMap(data)
    # what a generated map's name carries, so two machines agree on the exemplar: the stored bytes
    grid.crc = zlib.crc32(data)
    grid.label = re.sub(r"\W+", "", os.path.splitext(os.path.basename((inner or path).replace("\\", "/")))[0])
    version, body = raw.body("BlendTileData")
    grid.layers, grid.cliff, grid.tail = blend_cells(version, body, grid.m.width, grid.m.height)
    owner = np.zeros(4096, np.int32)
    for index, (name, first, count) in enumerate(grid.m.texture_classes):
        owner[first:first + count] = index
    grid.cls = owner[grid.layers[0] >> 2]
    grid.class_names = [name for name, _, _ in grid.m.texture_classes]
    grid.first = np.array([first for _, first, _ in grid.m.texture_classes])
    grid.width = np.array([math.isqrt(count) for _, _, count in grid.m.texture_classes])
    grid.bitmap_tiles, grid.table_head, entries = texture_table(grid.tail)
    # a blend is the class laid over the cell and how; its tile follows from where it lands
    grid.blend_class = np.array([owner[tile >> 2] for tile, _, _ in entries])
    shapes = sorted(set((flags, custom) for _, flags, custom in entries[1:]))
    grid.shapes = shapes
    grid.blend_shape = np.array([0] + [shapes.index((f, c)) for _, f, c in entries[1:]])

    # the scenery that moves with the ground, by the cell it stands on, and its trees counted
    grid.scenery = {}
    grid.trees = np.zeros(grid.h.shape, np.float64)
    _, objects = raw.body("ObjectsList")
    grid.raw = raw
    grid.objects = split_chunks(objects, 0, len(objects))
    for cid, _, body in grid.objects:
        x, y, angle, template = object_head(body)
        if SCENERY.match(template):
            cell = (int(y / MAP_XY_FACTOR) + grid.m.border, int(x / MAP_XY_FACTOR) + grid.m.border)
            grid.scenery.setdefault(cell, []).append(
                (template, x / MAP_XY_FACTOR % 1.0, y / MAP_XY_FACTOR % 1.0, angle))
            grid.trees[cell] += template.startswith("Tree")
    # trees a cell where trees may stand: the playable ground off the pads and the water
    b = grid.m.border
    open_ground = np.zeros(grid.h.shape, bool)
    open_ground[b:b + grid.m.playable_y, b:b + grid.m.playable_x] = True
    yy, xx = np.mgrid[0:grid.h.shape[0], 0:grid.h.shape[1]]
    for y, x in grid.starts:
        open_ground &= np.hypot(yy - y, xx - x) > PAD_RADIUS[1] - 9
    grid.m.passable_grid()
    open_ground &= ~np.frombuffer(bytes(grid.m.water), np.uint8).reshape(grid.h.shape).astype(bool)
    grid.tree_density = grid.trees[open_ground].sum() / max(open_ground.sum(), 1)
    grid.inventory = mapfeatures.inventory(data, inner or path)
    # a town's ground goes only with its stamp: smeared over open ground it paints streets anywhere
    grid.town = np.zeros(grid.h.shape, bool)
    for town in grid.inventory["towns"]:
        x0, y0, x1, y1 = town["box"]
        grid.town[max(int(y0) + b - TOWN_MARGIN, 0):int(y1) + b + TOWN_MARGIN + 1,
                  max(int(x0) + b - TOWN_MARGIN, 0):int(x1) + b + TOWN_MARGIN + 1] = True
    return grid


#-----------------------------------------------------------------------------------------------
# Hashing, the generator's (RandomMapGenerator.cpp hashCell), so a port draws the same numbers
#-----------------------------------------------------------------------------------------------

def hash_cell(seed, x, y):
    m = 0xffffffff
    h = (seed * 374761393) & m
    h = (h + x * 668265263) & m
    h ^= h >> 13
    h = (h + y * 2246822519) & m
    h ^= h >> 16
    h = (h * 2654435761) & m
    return h ^ (h >> 15)


def hash_unit(seed, x, y):
    return hash_cell(seed, x, y) / 4294967296.0


#-----------------------------------------------------------------------------------------------
# The skeleton, given the exemplar's large features
#-----------------------------------------------------------------------------------------------

def fade(x):
    x = np.clip(x, 0.0, 1.0)
    return x * x * (3.0 - 2.0 * x)


class Plan:
    """What compose() decided beside the heights."""

    def __init__(self, shape):
        self.gentle = np.zeros(shape, bool)     # slopes it made, kept from the detail like a ramp
        self.keepout = np.zeros(shape, bool)    # no copied scenery: bridge ends, towns, the canyon walls
        self.wet = np.zeros(shape, bool)        # inside a water polygon
        self.level = np.full(shape, -1.0)       # that polygon's surface, bytes: under it is water
        self.water = []                         # (points (y, x) in cells of the grid, surface bytes)
        self.lines = []                         # each river's centre line, for its branches and roads
        self.roads = []                         # (template, [(y, x), ...]) polylines in cells of the grid
        self.bridges = []                       # (template, (y, x), (y, x)) in cells of the grid
        self.stamps = []                        # (ty, tx, sy, sx, rows, cols): an exemplar rectangle copied whole
        self.tech = []                          # (template, (y, x))
        self.mesa = None                        # the mesa's middle, (y, x)
        self.notes = []                         # what the exemplar has that was not built


class Frame:
    """The skeleton's coordinates: cell grids, its middle, its seats and sites, how big the open
    middle is, and the guard that keeps every start pad, dock and derrick on its own ground."""

    def __init__(self, skel):
        m = skel.m
        self.yy, self.xx = np.mgrid[0:m.height, 0:m.width].astype(np.float64)
        self.cy, self.cx = skel.centre
        self.k = min(1.0, max(0.6, m.playable / 356.0))         # the generator's m_styleScale
        self.half = m.playable / 2.0
        self.play = np.zeros(skel.h.shape, bool)
        self.play[m.border:m.border + m.playable_y, m.border:m.border + m.playable_x] = True
        self.seat_ring = float(np.mean([math.hypot(y - self.cy, x - self.cx) for y, x in skel.starts]))
        self.bowl = skel.plateau - skel.bowl > MIN_RELIEF       # the skeleton is a style's bowl
        # the open middle: a bowl's floor inside its rim (RMGLayout::placeBowlStarts' m_bowlRim)
        self.middle = (self.seat_ring - (0.85 * 53.75 + 8.0) * self.k if self.bowl else 0.55 * self.seat_ring)
        docks = [(y + m.border, x + m.border) for x, y in m.supplies]
        derricks = [(y + m.border, x + m.border) for x, y in m.derricks]
        self.sites = list(skel.starts) + docks + derricks
        self.guard = np.zeros(skel.h.shape)
        for (y, x), r in [(p, PAD_RADIUS[1]) for p in skel.starts] + [(p, SITE_RADIUS[1]) for p in docks + derricks]:
            self.guard = np.maximum(self.guard, fade((r + 10.0 - np.hypot(self.yy - y, self.xx - x)) / 10.0))

    def axis(self, seed, salt, offset=0.0):
        """Of 32 bearings turned by the seed, the line through the middle (moved offset cells to its
        side) passing furthest from any start, dock or derrick."""
        turn = hash_unit(seed, salt, 1)
        best = None
        for q in range(32):
            angle = math.pi * (q + turn) / 32.0
            clear = min(abs((y - self.cy) * math.cos(angle) - (x - self.cx) * math.sin(angle) - offset)
                        for y, x in self.sites)
            if best is None or clear > best[0]:
                best = (clear, angle)
        return best[1]

    def along(self, angle, cy, cx):
        """s along a line through (cy, cx) at angle, c across it."""
        s = (self.yy - cy) * math.sin(angle) + (self.xx - cx) * math.cos(angle)
        c = (self.xx - cx) * math.sin(angle) - (self.yy - cy) * math.cos(angle)
        return s, c

    @staticmethod
    def point(angle, cy, cx, s, c):
        return (cy + s * math.sin(angle) - c * math.cos(angle), cx + s * math.cos(angle) + c * math.sin(angle))


def compose(skel, src, seed):
    """The exemplar's large features (its mapfeatures inventory) built into the skeleton before the
    quilting, at the sizes the inventory measured, in a layout of the seed's: high ground, ways
    down, the canyon and mesa of the middle with the mesa's bridges, a river with its bridges, its
    towns (copied whole), its tech buildings. Changes the skeleton's heights; returns the Plan."""
    inv = src.inventory
    f = Frame(skel)
    plan = Plan(skel.h.shape)
    h = fill_lakes(skel)
    shortest = min(inv["size_cells"])

    if f.bowl and inv["starts_raised"]:
        if inv["high_ground"]:
            h = raise_high(h, skel, f, inv["high_ground"]["dz_start"])
        if inv["base"]["ramp_down_width_cells"]:
            h = widen_ramps(h, skel, f, 0.5 * inv["base"]["ramp_down_width_cells"])
    elif inv["high_ground"]:
        plan.notes.append("high ground %+d over %d%% (the skeleton has no plateaus to raise)" % (
            inv["high_ground"]["dz_start"], 100 * inv["high_ground"]["share"]))

    canyons = [c for c in inv["canyons"] if c["length"] >= 0.5 * shortest and c["centre_share"] < 0.3]
    mesas = [x for x in inv["mesas"] if x["centre_share"] < 0.3]
    if canyons or mesas:
        h = carve_middle(h, skel, f, plan, src, canyons[0] if canyons else None,
                         mesas[0] if mesas else None, seed)
    plan.notes += ["canyon %d long off the middle" % c["length"] for c in inv["canyons"] if c not in canyons]
    plan.notes += ["mesa r%d off the middle" % x["radius"] for x in inv["mesas"] if x not in mesas]

    # the water: the sea first, as the rivers run into it, then the rivers longest first and their
    # branches, then the lakes, each clear of what is already there
    waters = inv["water"]
    built = set()
    for index, body in enumerate(waters):
        if body["kind"] in ("sea", "coast"):
            h = carve_sea(h, skel, f, plan, src, index, seed)
            built.add(index)
    rivers = [i for i, w in enumerate(waters) if w["kind"] == "river" and w["length"] >= RIVER_MIN * shortest]
    mains = sorted([i for i in rivers if waters[i]["joins"] not in rivers], key=lambda i: -waters[i]["length"])
    for order, index in enumerate(mains):
        h = place_river(h, skel, f, plan, src, index, order, seed)
    for index in rivers:
        if index not in mains:
            h = place_branch(h, skel, f, plan, src, index, seed)
    built.update(rivers)
    for index, body in enumerate(waters):
        if body["kind"] == "lake" and body["area"] >= LAKE_CELLS:
            h = carve_lake(h, skel, f, plan, src, index, seed)
            built.add(index)
    plan.notes += ["%s of %d cells" % (w["kind"], w["area"]) for i, w in enumerate(waters) if i not in built]

    h = stamp_towns(h, skel, f, plan, src, seed)
    place_tech(h, skel, f, plan, inv, seed)
    lay_roads(h, skel, f, plan, src, seed)
    skel.set_heights(h)
    return plan


def raise_high(h, skel, f, dz_start):
    """The skeleton's ridges (its ground well above the plateau) up to the exemplar's high ground."""
    ridge = np.median(h[f.play & (h > skel.plateau + 0.25 * skel.scale)])
    lift = skel.plateau + 0.1 * skel.scale
    high = h > lift
    h = h.copy()
    h[high] = lift + (h[high] - lift) * (skel.plateau + dz_start - lift) / max(ridge - lift, 1.0)
    return h


def widen_ramps(h, skel, f, half):
    """Each seat's way down, its height profile along the spoke laid out half cells to a side."""
    t = (h - skel.bowl) / skel.scale
    slope = walkable(span_cliff(skel.h)) & (t > 0.15) & (t < 0.85)
    rim = f.middle + 8.0 * f.k
    half *= f.k
    bank = RAMP_BANK * f.k
    for sy, sx in skel.starts:
        d = math.hypot(sy - f.cy, sx - f.cx)
        uy, ux = (sy - f.cy) / d, (sx - f.cx) / d
        along = (f.yy - f.cy) * uy + (f.xx - f.cx) * ux
        across = np.abs((f.xx - f.cx) * uy - (f.yy - f.cy) * ux)
        core = slope & (across < 6) & (along > rim - 70) & (along < d - 10)
        if core.sum() < 20:
            continue
        a = np.rint(along[core]).astype(int)
        a0, a1 = a.min(), a.max()
        sums = np.bincount(a - a0, h[core], a1 - a0 + 1)
        counts = np.bincount(a - a0, None, a1 - a0 + 1)
        known = counts > 0
        profile = np.interp(np.arange(a1 - a0 + 1), np.nonzero(known)[0], (sums / np.maximum(counts, 1))[known])
        profile = np.maximum.accumulate(profile)        # down the spoke only ever down
        w = fade((half + bank - across) / bank) * fade((along - a0 + 8) / 8) * fade((a1 + 8 - along) / 8)
        profile = box(profile[None, :], 3)[0]            # no steps down the ramp
        h = w * np.interp(along - a0, np.arange(a1 - a0 + 1), profile) + (1.0 - w) * h
    return h


def carve_middle(h, skel, f, plan, src, canyon, mesa, seed):
    """A canyon through the middle and a mesa in it, as the inventory has them. The canyon runs on
    an axis clear of every start, dock and derrick, winding, its walls cliffs, out to the map's edge
    if the exemplar's does, else ending on a slope short of the rim; it has a ford in each arm and
    a moat round the mesa. The mesa is a cliff but where it slopes into the moat at the ends of the
    axis (two ways up if the exemplar's has two or more), which is how Twilight's mesa is reached on
    foot; its bridges go across the moat, half each side."""
    angle = f.axis(seed, 77)
    shift = (hash_unit(seed, 77, 2) - 0.5) * 16.0 * f.k
    my, mx = f.cy + shift * math.sin(angle), f.cx + shift * math.cos(angle)
    s, c = f.along(angle, my, mx)
    shaped = np.zeros(h.shape)
    out = h.copy()
    mesa_a = mesa_b = moat = 0.0
    if mesa:
        r = min(mesa["radius"] * f.k, 0.3 * f.middle)
        mesa_a, mesa_b = 1.15 * r, 0.87 * r
    if canyon:
        half = 0.45 * canyon["width"] * f.k
        moat = 0.55 * canyon["width"] * f.k if mesa else 0.0
        reach = 0.75 * min(skel.h.shape) if canyon["reaches_edge"] else f.middle - END_CELLS * f.k
        phase = hash_unit(seed, 77, 3) * 2.0 * math.pi
        wander = (1.4 * canyon["winding"] * np.sin(2.0 * math.pi * s / max(0.5 * canyon["length"], 120.0) + phase) *
                  fade((np.abs(s) - mesa_a) / max(2.0 * moat, 1.0)))
        ford = np.abs(np.abs(s) - FORD_AT * f.middle)
        arm = fade((half + WALL_CELLS - np.abs(c - wander)) / WALL_CELLS) * (1.0 - 0.9 * fade((FORD_CELLS - ford) / 6.0))
        if not canyon["reaches_edge"]:
            arm *= fade((reach - np.abs(s)) / (END_CELLS * f.k))
        cut = arm
        if mesa:
            ellipse = np.hypot(s / (mesa_a + moat), c / (mesa_b + moat))
            cut = np.maximum(arm, fade((1.0 - ellipse) * (mesa_b + moat) / WALL_CELLS))
        cut *= 1.0 - f.guard
        floor = skel.bowl + canyon["floor_dz_middle"] + 0.3 * (h - skel.bowl)
        out = (1.0 - cut) * out + cut * floor
        shaped = np.maximum(shaped, cut)
        plan.keepout |= (cut > 0.05) & (cut < 0.95)
    if mesa:
        e = np.hypot(s / mesa_a, c / mesa_b)
        ends = np.abs(s) / np.maximum(np.hypot(s, c), 1e-6)
        if mesa["exits"] < 2:
            ends = np.where(s > 0, ends, 0.0)
        width = WALL_CELLS + (MESA_RAMP - WALL_CELLS) * ends ** 8
        lift = fade((1.0 - e) * mesa_b / width + 1.0) * (1.0 - f.guard)
        out = (1.0 - lift) * out + lift * (skel.bowl + mesa["top_dz_middle"] + 0.3 * (h - skel.bowl))
        shaped = np.maximum(shaped, lift)
        plan.mesa = (int(round(my)), int(round(mx)))
        top = skel.bowl + mesa["top_dz_middle"]
        ours = [b for b in src.inventory["bridges"] if b["on_mesa"] == src.inventory["mesas"].index(mesa)]
        for i, bridge in enumerate(ours):
            side = 1.0 if i % 2 == 0 else -1.0
            at = ((i // 2) - (len(ours) - 1) // 4) * 14.0
            a = Frame.point(angle, my, mx, at, side * (mesa_b - 4.0))
            z = Frame.point(angle, my, mx, at, side * (mesa_b - 4.0 + bridge["length"]))
            ground = float(skel.bowl + bridge["ends_dz_middle"][1])
            for (y, x), level_to in ((a, top), (z, ground)):
                disc = np.hypot(f.yy - y, f.xx - x) <= 4.0
                out[disc] = level_to
            plan.bridges.append((bridge["template"], a, z))
    plan.gentle |= (shaped > 0.05) & (shaped < 0.95)
    return out


def fill_lakes(skel):
    """The skeleton's heights with its own lakes filled to their rims: the exemplar's water takes
    their place (water_triggers drops their areas)."""
    m = skel.m
    h = skel.h.astype(np.float64)
    areas = m.water_areas
    filled = np.zeros(h.shape, bool)
    for area in areas:
        m.water_areas = [area]
        wet = np.frombuffer(bytes(m.water_grid()), np.uint8).reshape(h.shape).astype(bool)
        if wet.any():
            rim = ndimage.binary_dilation(wet, iterations=3) & ~wet
            h[wet] = np.median(h[rim])
            filled |= wet
    m.water_areas = []
    near = box(filled, 4) > 0
    h[near] = box(h, 3)[near]
    return h


def carve_lake(h, skel, f, plan, src, index, seed):
    """A lake as the exemplar's: an ellipse of its length and width, as far from the middle as it
    was (no further than 0.8 of the way to an edge), on the bearing the seed turns first that keeps
    it, its shore and its valley clear of the sites, the water and the canyon. It stands at the
    exemplar's level against the middle, or lower where its rim is lower, so no shore is a dam."""
    inv = src.inventory
    lake = inv["water"][index]
    a = max(0.5 * lake["length"] * f.k, 6.0)
    b = max(0.5 * lake["width"] * f.k, 5.0)
    tilt = hash_unit(seed, 90 + index, 2) * math.pi
    taken = (box(plan.wet, 6) > 0) | plan.keepout | ~f.play
    count = np.pad(taken.astype(np.int32).cumsum(0).cumsum(1), ((1, 0), (1, 0)))
    reach = int(max(a, b) + SHORE_CELLS) + 1
    # every clear place on an eight cell grid, the nearest the lake's own distance from the middle
    # first, whose water and shore keep off every pad, dock and derrick (which may stand on its
    # banks: the carving spares their ground)
    places = []
    for my in range(reach, h.shape[0] - reach, TOWN_STEP):
        for mx in range(reach, h.shape[1] - reach, TOWN_STEP):
            if count[my + reach, mx + reach] - count[my - reach, mx + reach] - \
                    count[my + reach, mx - reach] + count[my - reach, mx - reach]:
                continue
            share = math.hypot(my - f.cy, mx - f.cx) / f.half
            places.append((abs(share - min(lake["centre_share"], 0.8)) +
                           0.05 * hash_unit(seed, 90 + index, my * 4096 + mx), my, mx))
    for _, my, mx in sorted(places):
        s, c = f.along(tilt, my, mx)
        inland = (1.0 - np.hypot(s / a, c / b)) * b       # cells in from the waterline, about
        if not (f.guard > 0.01)[inland > -SHORE_CELLS].any():
            break
    else:
        plan.notes.append("lake of %d cells (no clear ground)" % lake["area"])
        return h
    rim = (inland < -1.0) & (inland > -SHORE_CELLS)
    surface = min(skel.bowl + lake["surface_bytes"] - src.bowl, float(h[rim].min()) - 2.0)
    bed = max(surface - lake["depth_bytes"], 1.0)
    target = np.where(inland >= 0.0, surface + 3.0 - (surface + 3.0 - bed) * fade(inland / SHORE_CELLS),
                      surface + 3.0 + RIVER_SLOPE * np.maximum(-inland - SHORE_CELLS, 0.0))
    out = h - (1.0 - f.guard) * np.maximum(h - target, 0.0)
    band = inland > -2.0
    plan.wet |= band
    plan.level[band] = np.maximum(plan.level[band], surface)
    plan.gentle |= (inland < 0.0) & (out < h - 0.5)
    points = [Frame.point(tilt, my, mx, (a + 2.0) * math.cos(t), (b + 2.0) * math.sin(t))
              for t in np.linspace(0.0, 2.0 * math.pi, LAKE_POINTS, endpoint=False)]
    plan.water.append((points, surface))
    return out


def carve_sea(h, skel, f, plan, src, index, seed):
    """The sea along the edges the exemplar's touches, turned a quarter at a time to the turn that
    keeps it furthest off the sites (an island map's sea is on every edge): a coast a wet share as
    big as the exemplar's in, no nearer a start, dock or derrick than its shore and valley, a
    wandering line with a beach up from the water. Its area is a strip along each edge it holds,
    or round the corner it holds, as WorldBuilder's sea is one plane over everything low."""
    inv = src.inventory
    sea = inv["water"][index]
    m = skel.m
    bd = m.border
    y0, x0, y1, x1 = bd, bd, bd + m.playable_y - 1, bd + m.playable_x - 1
    edge_distance = [f.yy - y0, x1 - f.xx, y1 - f.yy, f.xx - x0]            # low y, high x, high y, low x
    corners = {(0, 1): (y0, x1), (1, 2): (y1, x1), (2, 3): (y1, x0), (0, 3): (y0, x0)}
    margin = SHORE_CELLS + VALLEY_CELLS
    if min(sea["edges"]) >= FULL_EDGE:
        return carve_islands(h, skel, f, plan, src, index, seed)
    best = None
    for turn in range(4):
        wet_edges = [(i + turn) % 4 for i in range(4) if sea["edges"][i] >= COAST_EDGE]
        full = [(i + turn) % 4 for i in range(4) if sea["edges"][i] >= FULL_EDGE]
        if full:
            d = np.min([edge_distance[i] for i in full], axis=0)
            corner = None
        else:
            pair = tuple(sorted(wet_edges[:2])) if len(wet_edges) >= 2 else None
            corner = corners.get(pair) if pair else None
            d = (np.hypot(f.yy - corner[0], f.xx - corner[1]) if corner else edge_distance[wet_edges[0]])
        wobble = COAST_WOBBLE * np.sin(2.0 * math.pi * (f.yy + f.xx) / 97.0 + hash_unit(seed, 95, turn) * 6.283)
        d = d + wobble
        # the widest coast as wet as the exemplar's that keeps its valley off every site
        cap = float(d[f.guard > 0.01].min()) - margin if (f.guard > 0.01).any() else float(d.max())
        order = np.sort(d[f.play])
        width = min(float(order[min(int(sea["share"] * len(order)), len(order) - 1)]), cap)
        score = (-width, hash_unit(seed, 96, turn))
        if best is None or score < best[0]:
            best = (score, turn, d, width, full, corner, wet_edges)
    _, turn, d, width, full, corner, wet_edges = best
    if width <= 0:
        plan.notes.append("sea of %d cells (no coast clear of the sites)" % sea["area"])
        return h
    reach = width + COAST_WOBBLE + margin + 10.0
    # the area: a strip along each full edge, or the corner's square, out past the beach
    strips = [((0, 0), (0, x1 + bd), (reach + y0, x1 + bd), (reach + y0, 0)),
              ((0, x1 - reach), (0, x1 + bd), (y1 + bd, x1 + bd), (y1 + bd, x1 - reach)),
              ((y1 - reach, 0), (y1 - reach, x1 + bd), (y1 + bd, x1 + bd), (y1 + bd, 0)),
              ((0, 0), (0, reach + x0), (y1 + bd, reach + x0), (y1 + bd, 0))]
    if full:
        boxes = [strips[i] for i in full]
    elif corner:
        cy, cx = corner
        sy, sx = (1.0 if cy > f.cy else -1.0), (1.0 if cx > f.cx else -1.0)
        far = reach + bd
        boxes = [((cy + sy * bd, cx + sx * bd), (cy + sy * bd, cx - sx * far),
                  (cy - sy * far, cx - sx * far), (cy - sy * far, cx + sx * bd))]
    else:
        boxes = [strips[wet_edges[0]]]
    return lay_sea(h, skel, f, plan, src, sea, d - width, boxes)


def carve_islands(h, skel, f, plan, src, index, seed):
    """An island map's sea (Tournament Island's): land round every start, dock and derrick and the
    middle, as wide as leaves the exemplar's share of water but never under a pad's own ground
    and its shore, the starts joined to the middle by causeways unless the exemplar's starts reach
    each other only over bridges. The sea's area is the whole map, as WorldBuilder lays one."""
    sea = src.inventory["water"][index]

    def distance_from(points):
        cores = np.zeros(h.shape, bool)
        for y, x in points:
            cores[int(y), int(x)] = True
        return ndimage.distance_transform_edt(~cores)

    from_starts = distance_from(skel.starts)
    from_sites = distance_from(f.sites[len(skel.starts):] + [(f.cy, f.cx)])
    roads = np.zeros(h.shape, bool)
    if not src.inventory.get("split_without_bridges", False):
        for y, x in skel.starts:
            roads |= line_cells((y, x), (f.cy, f.cx), h.shape)
    from_roads = ndimage.distance_transform_edt(~roads) if roads.any() else np.full(h.shape, 1e9)
    wobble = COAST_WOBBLE * np.sin(2.0 * math.pi * (f.yy + f.xx) / 97.0 + hash_unit(seed, 95, 0) * 6.283) * \
        np.sin(2.0 * math.pi * (f.yy - f.xx) / 131.0)
    # a start's island holds its pad and the shore round it; a dock's or derrick's, its own ground
    shore = SHORE_CELLS + VALLEY_CELLS
    site_land = SITE_RADIUS[1] + 10.0 + shore

    def land(radius):
        return np.maximum(np.maximum(radius - from_starts, site_land - from_sites),
                          CAUSEWAY_CELLS - from_roads) + wobble

    least = PAD_RADIUS[1] + 10.0 + shore
    radius = least
    for trial in np.arange(least, 0.5 * min(h.shape), 2.0):
        if float((land(trial)[f.play] < 0.0).mean()) < sea["share"]:
            break
        radius = trial
    inland = land(radius)
    corner = np.array(h.shape, np.float64) - 1.0
    whole = [(0.0, 0.0), (0.0, corner[1]), (corner[0], corner[1]), (corner[0], 0.0)]
    return lay_sea(h, skel, f, plan, src, sea, inland, [whole])


def lay_sea(h, skel, f, plan, src, sea, inland, boxes):
    """The sea where inland (cells from the coastline, under 0 at sea) is under 0: its bed, a beach
    up from the waterline, higher ground cut back to a walkable slope, ground the plane would flood
    inside its areas raised to the beach, and the areas themselves."""
    # src.bowl on an island map is half sea bed; the exemplar's bank over its water bounds the surface
    surface = max(min(skel.bowl + sea["surface_bytes"] - src.bowl, skel.bowl - max(sea["bank_bytes"], 4.0)), SEA_FLOOR)
    bed = max(surface - max(sea["depth_bytes"], 6.0), 0.0)
    target = np.where(inland < 0.0, surface - (surface - bed) * fade(-inland / SHORE_CELLS),
                      surface + 2.0 + RIVER_SLOPE * np.maximum(inland - 2.0, 0.0))
    out = h - (1.0 - f.guard) * np.maximum(h - target, 0.0)
    shape = np.array(h.shape, np.float64) - 1.0
    footprint = np.zeros(h.shape, bool)
    for corners_ in boxes:
        points = [tuple(np.clip(p, 0, shape)) for p in corners_]
        plan.water.append((points, surface))
        ys, xs = [p[0] for p in points], [p[1] for p in points]
        footprint[int(min(ys)):int(max(ys)) + 1, int(min(xs)):int(max(xs)) + 1] = True
    # inland ground the plane would flood is raised to the beach
    low = footprint & (inland >= 0.0) & (out < surface + 2.0)
    out[low] = surface + 2.0
    band = footprint & (inland < 1.0)
    plan.wet |= band
    plan.level[footprint] = np.maximum(plan.level[footprint], surface)
    plan.gentle |= footprint & (inland >= 0.0) & (out < h - 0.5)
    got = float((band & f.play).sum()) / f.play.sum()
    if got < 0.8 * sea["share"]:
        plan.notes.append("sea at %d%% of the map against the exemplar's %d%% (the starts stand nearer the edge)" % (
            100 * got, 100 * sea["share"]))
    return out


def judge(out, trial, starts):
    """How a trial layout of water treats the starts: the nearest start's distance to the water in
    start spacings, and whether without the bridges the starts no longer share their ground."""
    spacing = min(math.hypot(p[0] - q[0], p[1] - q[1]) for i, p in enumerate(starts) for q in starts[i + 1:])
    wet = trial.wet & (out < trial.level)
    gap = ndimage.distance_transform_edt(~wet)
    ground, _ = ndimage.label(walkable(span_cliff(out)) & ~wet)
    return min(float(gap[p]) for p in starts) / spacing, len(set(int(ground[p]) for p in starts)) > 1


def river_bridges(inv, index):
    """The exemplar's bridges over its water number index."""
    return [b for b in inv["bridges"] if b["over"] == "water" and b.get("water", index) == index]


def place_river(h, skel, f, plan, src, index, order, seed):
    """carve_river where its nearest start stands as far from the water, in start spacings, as the
    exemplar's does, and where without the bridges the starts still share their ground when the
    exemplar's do: of the offsets round the exemplar's own, either side, edge to edge or the
    exemplar's length, the nearest to its offset that meets both, else the clearest that splits
    nothing. A river laid by its offset alone came 45 cells from a Heartland start (0.16 spacings
    against the exemplar's 0.38), and every wave of that side filed over a bridge to gather. A
    second river (order 1 on) is laid clear of the water already there; its branches come later."""
    inv = src.inventory
    river = inv["water"][index]
    salt = 78 + 7 * order
    want = river.get("start_clear", 0.0)
    may_split = inv.get("split_without_bridges", True)
    first = 1.0 if hash_unit(seed, salt, 4) < 0.5 else -1.0
    bridges = river_bridges(inv, index)
    ranked = []
    for step in (0.0, 0.1, -0.1, 0.2, -0.2, 0.3, -0.3, 0.45, -0.45):
        share = river["centre_share"] + step
        if not 0.0 <= share <= 0.9:
            continue
        for sign in (first, -first):
            for edge in (river["reaches_edge"], not river["reaches_edge"]):
                trial = copy.deepcopy(plan)
                line = river_line(skel, f, river, seed, salt, sign * share * f.half, edge)
                out = carve_river(h, skel, f, trial, src, river, index, seed, line, bridges)
                clear, split = judge(out, trial, skel.starts)
                meets = bool((trial.lines[-1]["wet"] & plan.wet).any())
                score = (meets, split and not may_split, clear < want,
                         abs(step) + 0.05 * (edge != river["reaches_edge"]), -clear)
                ranked.append((score, line))
    # and of the best few, the one that leaves the fewest bridges, towns and tech buildings unbuilt:
    # a river moved for the starts' sake can take the only ground a town or a refinery had
    ranked.sort(key=lambda r: r[0])
    best = None
    for rank, (score, line) in enumerate(ranked[:RIVER_TRIES]):
        if score[:3] != ranked[0][0][:3]:
            break
        trial = copy.deepcopy(plan)
        out = stamp_towns(carve_river(h, skel, f, trial, src, river, index, seed, line, bridges), skel, f, trial, src, seed)
        place_tech(out, skel, f, trial, inv, seed)
        if best is None or (len(trial.notes), rank) < best[0]:
            best = ((len(trial.notes), rank), line)
    return carve_river(h, skel, f, plan, src, river, index, seed, best[1], bridges)


def river_line(skel, f, river, seed, salt, side, edge):
    """A river's axis side cells off the middle, on the bearing clearest of every start, dock and
    derrick, across the map when edge is set, else the exemplar's length:
    (angle, y, x, from, to, anchored, salt) in the terms of Frame.along."""
    angle = f.axis(seed, salt, side)
    my, mx = Frame.point(angle, f.cy, f.cx, 0.0, side)
    length = 0.5 * math.hypot(*skel.h.shape) + 20.0 if edge else 0.5 * river["length"] * f.k
    return (angle, my, mx, -length, length, False, salt)


def place_branch(h, skel, f, plan, src, index, seed):
    """A river that runs into a longer one, as the exemplar's does: from a junction on the main
    river away from its bridges, out on the bearing that keeps the nearest start as far off as the
    exemplar's, splits the starts only if the exemplar's do and is not near parallel to the main,
    to the edge if the exemplar's reaches it. Its water stands at the main river's level."""
    inv = src.inventory
    river = inv["water"][index]
    main = next((line for line in plan.lines if line["index"] == river["joins"]), None)
    if main is None:
        plan.notes.append("river branch of %d cells (its main river was not built)" % river["area"])
        return h
    want = river.get("start_clear", 0.0)
    may_split = inv.get("split_without_bridges", True)
    bridges = river_bridges(inv, index)
    points = main["points"]
    quarter = len(points) // 5
    ranked = []
    for k in range(quarter, len(points) - quarter, 3):
        jy, jx = points[k]
        if min([math.hypot(jy - y, jx - x) for y, x in main["sites"]] + [1e9]) < JUNCTION_CLEAR:
            continue
        ty, tx = points[k + 1][0] - points[k - 1][0], points[k + 1][1] - points[k - 1][1]
        course = math.atan2(ty, tx)
        for q in range(16):
            angle = 2.0 * math.pi * (q + hash_unit(seed, 85, k)) / 16.0
            turn = abs((angle - course + math.pi) % (2.0 * math.pi) - math.pi)
            if not BRANCH_TURN <= turn <= math.pi - BRANCH_TURN:
                continue
            length = 0.0
            while length < (2.0 * min(skel.h.shape) if river["reaches_edge"] else river["length"] * f.k):
                y, x = jy + (length + 4.0) * math.sin(angle), jx + (length + 4.0) * math.cos(angle)
                if not (0 <= int(y) < h.shape[0] and 0 <= int(x) < h.shape[1]) or not f.play[int(y), int(x)]:
                    break
                length += 4.0
            if length < 0.5 * river["length"] * f.k:
                continue
            clear = min(abs((y - jy) * math.cos(angle) - (x - jx) * math.sin(angle))
                        if 0.0 <= (y - jy) * math.sin(angle) + (x - jx) * math.cos(angle) <= length
                        else math.hypot(y - jy, x - jx) for y, x in f.sites)
            ranked.append((-clear, k, angle, length))
    ranked.sort()
    best = None
    for clearness, k, angle, length in ranked[:BRANCH_TRIES]:
        jy, jx = points[k]
        line = (angle, jy, jx, -main["half"] - SHORE_CELLS, length + 20.0, True, 85)
        trial = copy.deepcopy(plan)
        out = carve_river(h, skel, f, trial, src, river, index, seed, line, bridges, main["surface"])
        clear, split = judge(out, trial, skel.starts)
        score = (split and not may_split, clear < want, len(trial.notes), clearness)
        if best is None or score < best[0]:
            best = (score, line)
    if best is None:
        plan.notes.append("river branch of %d cells (no way off the main river clear of the sites)" % river["area"])
        return h
    return carve_river(h, skel, f, plan, src, river, index, seed, best[1], bridges, main["surface"])


def carve_river(h, skel, f, plan, src, river, index, seed, line, bridges, surface=None):
    """A river as the inventory has it along line (river_line's, or a branch's from its junction):
    its bed, a shore up to the waterline and a valley up to its banks, winding, with the given
    bridges evenly along it where it narrows to their length, and a water area over it. A branch
    is anchored: it winds only once clear of the junction and runs on into the main river's bed."""
    angle, my, mx, s_lo, s_hi, anchored, salt = line
    if surface is None:
        # as far under the ground level as the exemplar's, but never so far that the bed would go below 0
        surface = max(skel.bowl + river["surface_bytes"] - src.bowl, river["depth_bytes"] + 2.0)
    bed = surface - river["depth_bytes"]
    bank = surface + max(river["bank_bytes"], 4.0)
    s, c = f.along(angle, my, mx)
    phase = hash_unit(seed, salt, 3) * 2.0 * math.pi
    wave = max(river["length"] / 1.5, 120.0)
    amp = 1.4 * river["winding"] * f.k

    def wander(at):
        settle = fade(np.asarray(at) / BRANCH_SETTLE) if anchored else 1.0
        return amp * np.sin(2.0 * math.pi * np.asarray(at) / wave + phase) * settle

    ours = list(bridges)
    inside = [v for v in np.arange(s_lo, s_hi, 2.0)
              if f.play[tuple(np.clip(np.rint(Frame.point(angle, my, mx, v, float(wander(v)))).astype(int), 0,
                                      np.array(skel.h.shape) - 1))]]
    s0, s1 = (min(inside), max(inside)) if inside else (s_lo, s_hi)
    if anchored:
        s0 = max(s0, JUNCTION_CLEAR)
    # evenly along it, each moved to the nearest place clear of every start, dock and derrick and of
    # the water already there
    sites = []
    for i, bridge in enumerate(bridges):
        even = s0 + (i + 1) * (s1 - s0) / (len(bridges) + 1)
        for nudge in sorted(range(-48, 52, 4), key=abs):
            y, x = Frame.point(angle, my, mx, even + nudge, float(wander(even + nudge)))
            clear = np.hypot(f.yy - y, f.xx - x) <= 0.5 * bridge["length"] + 8.0
            # (a crossing off the playable ground, which a river laid far off the middle can ask
            # for, is none)
            if clear.any() and f.play[clear].all() and f.guard[clear].max() < 0.01 and \
                    not plan.wet[clear].any() and all(abs(even + nudge - s) > 30.0 for s in sites):
                sites.append(even + nudge)
                break
        else:
            plan.notes.append("%s bridge (no crossing clear of the sites)" % bridge["template"])
            ours = [b for b in ours if b is not bridge]
    narrow = np.ones(h.shape)
    # the exemplar's width is its wet cells', and the wet band is the bed and half the shore each side
    half = max(0.5 * (river["width"] * f.k - SHORE_CELLS), 3.0)
    for at, bridge in zip(sites, ours):
        to = min(1.0, max(bridge["length"] - 2.0 * SHORE_CELLS - 4.0, 6.0) / (2.0 * half))
        narrow = np.minimum(narrow, 1.0 - (1.0 - to) * fade((24.0 - np.abs(s - at)) / 12.0))
    inner = half * narrow
    d = np.abs(c - wander(s))
    within = (s >= s_lo) & (s <= s_hi)
    # past the banks the ground may rise RIVER_SLOPE a cell, so higher ground is cut back to a
    # walkable valley side and not to a wall
    outside = d - inner - SHORE_CELLS - VALLEY_CELLS
    target = np.where(d < inner, bed,
                      np.where(d < inner + SHORE_CELLS, bed + (surface + 3.0 - bed) * (d - inner) / SHORE_CELLS,
                               np.where(outside < 0.0,
                                        surface + 3.0 + (bank - surface - 3.0) * fade((d - inner - SHORE_CELLS) / VALLEY_CELLS),
                                        bank + RIVER_SLOPE * outside)))
    ends = np.minimum(np.where(anchored, VALLEY_CELLS, s - s_lo), s_hi - s)
    w = fade(ends / VALLEY_CELLS) * (1.0 - f.guard)
    out = np.minimum(h, h - w * np.maximum(h - target, 0.0))
    band = (d < inner + 0.5 * SHORE_CELLS) & within
    plan.wet |= band
    plan.level[band] = np.maximum(plan.level[band], surface)
    plan.gentle |= (d >= inner) & (out < h - 0.5)

    # the water area: the waterline either side of the bed, every eight cells along
    shape = np.array(skel.h.shape, np.float64) - 1.0
    left, right = [], []
    for v in np.arange(s_lo, s_hi + 8.0, 8.0):
        width = half + 0.5 * SHORE_CELLS + 2.0
        for out_list, sign in ((left, 1.0), (right, -1.0)):
            out_list.append(tuple(np.clip(Frame.point(angle, my, mx, v, float(wander(v)) + sign * width), 0, shape)))
    plan.water.append((left + right[::-1], surface))
    plan.lines.append({"index": index, "surface": surface, "half": half, "wet": band,
                       "points": [Frame.point(angle, my, mx, v, float(wander(v))) for v in np.arange(s0, s1, 4.0)],
                       "sites": [Frame.point(angle, my, mx, at, float(wander(at))) for at in sites]})

    for at, bridge in zip(sites, ours):
        centre = float(wander(at))
        tangent = float(wander(at + 1.0) - wander(at - 1.0)) / 2.0
        # across the river where it runs, not across the axis
        turn = math.atan(tangent)
        reach = 0.5 * bridge["length"]
        a = Frame.point(angle, my, mx, at - reach * math.sin(turn), centre + reach * math.cos(turn))
        z = Frame.point(angle, my, mx, at + reach * math.sin(turn), centre - reach * math.cos(turn))
        for y, x in (a, z):
            disc = np.hypot(f.yy - y, f.xx - x) <= 4.0
            out[disc] = bank
            plan.gentle |= np.hypot(f.yy - y, f.xx - x) <= 10.0
        plan.bridges.append((bridge["template"], a, z))
    return out


def stamp_towns(h, skel, f, plan, src, seed):
    """Each of the exemplar's towns copied whole, its ground, textures and every object in its box,
    to a spot as far from the middle as it was, clear of the sites, the water and the canyon, on
    ground the copy can level; the ground under it is levelled to its median, feathered."""
    b = src.m.border
    taken = plan.keepout | plan.wet | (f.guard > 0.01) | ~f.play | (protection(skel)[0] > 0.05)
    for number, town in enumerate(sorted(src.inventory["towns"], key=lambda t: -t["buildings"])):
        x0, y0, x1, y1 = town["box"]
        sy, sx = int(y0) + b - TOWN_MARGIN, int(x0) + b - TOWN_MARGIN
        rows, cols = int(y1 - y0) + 2 * TOWN_MARGIN + 1, int(x1 - x0) + 2 * TOWN_MARGIN + 1
        sy, sx = max(sy, 0), max(sx, 0)
        rows, cols = min(rows, src.h.shape[0] - sy), min(cols, src.h.shape[1] - sx)
        # every clear place on an eight cell grid, the one nearest the town's own distance from the
        # middle first, the seed breaking ties
        count = np.pad(taken.astype(np.int32).cumsum(0).cumsum(1), ((1, 0), (1, 0)))
        spot, best = None, None
        for ty in range(6, h.shape[0] - rows - 6, TOWN_STEP):
            for tx in range(6, h.shape[1] - cols - 6, TOWN_STEP):
                y0_, x0_, y1_, x1_ = ty - 6, tx - 6, ty + rows + 6, tx + cols + 6
                if count[y1_, x1_] - count[y0_, x1_] - count[y1_, x0_] + count[y0_, x0_]:
                    continue
                share = math.hypot(ty + rows / 2.0 - f.cy, tx + cols / 2.0 - f.cx) / f.half
                score = abs(share - town["centre_share"]) + 0.05 * hash_unit(seed, 79 + number, ty * 4096 + tx)
                if (best is None or score < best) and np.ptp(h[ty:ty + rows, tx:tx + cols]) <= TOWN_RELIEF:
                    spot, best = (ty, tx), score
        if not spot:
            plan.notes.append("town of %d buildings (no clear ground %d x %d)" % (town["buildings"], cols, rows))
            continue
        ty, tx = spot
        base = float(np.median(h[ty:ty + rows, tx:tx + cols]))
        inside = np.minimum(np.minimum(f.yy - ty, ty + rows - 1 - f.yy), np.minimum(f.xx - tx, tx + cols - 1 - f.xx))
        w = fade((inside + 8.0) / 8.0)
        h = w * base + (1.0 - w) * h
        plan.stamps.append((ty, tx, sy, sx, rows, cols))
        plan.keepout[ty:ty + rows, tx:tx + cols] = True
        taken[max(ty - 10, 0):ty + rows + 10, max(tx - 10, 0):tx + cols + 10] = True
    return h


def place_tech(h, skel, f, plan, inv, seed):
    """The exemplar's tech buildings: one that stood on a mesa on ours, the rest as far from the
    middle as they were, on the nearest flat ground, out of the water and off the bridges' ends,
    whose ramps the levelling would flatten."""
    barred = box(plan.wet, 2) > 0
    for _, a, z in plan.bridges:
        barred |= box(line_cells(a, z, h.shape), BRIDGE_CLEAR) > 0
    for number, tech in enumerate(inv["tech"]):
        if tech["on_mesa"] is not None and plan.mesa:
            plan.tech.append((tech["template"], plan.mesa))
            continue
        turn = hash_unit(seed, 80, number)
        # no further out than 0.8 of the way to an edge: past the inscribed circle is a corner, where
        # Winding River's hospital went and no army ever passes
        radius = min(tech["centre_share"], 0.8) * f.half
        for q in range(64):
            angle = 2.0 * math.pi * (q + turn) / 64.0
            y = int(f.cy + radius * math.sin(angle))
            x = int(f.cx + radius * math.cos(angle))
            if min(y, x) < TECH_FLAT:          # a corner's tech building: the circle leaves the grid
                continue
            near = (slice(y - TECH_FLAT, y + TECH_FLAT + 1), slice(x - TECH_FLAT, x + TECH_FLAT + 1))
            if (f.play[near].all() and not (plan.keepout[near].any() or plan.wet[near].any() or barred[near].any())
                    and f.guard[near].max() < 0.01 and np.ptp(h[near]) <= 12):
                plan.tech.append((tech["template"], (y, x)))
                plan.keepout[near] = True
                break
        else:
            plan.notes.append("%s (no flat ground)" % tech["template"])


def lay_roads(h, skel, f, plan, src, seed):
    """The exemplar's road network outside its towns, rebuilt between our places: every start, town
    and tech building joined by the cheapest way over walkable dry ground (a climb costs, a cliff
    and the water bar it, a bridge carries it across a river), the joins of a minimum spanning tree
    first and then the next cheapest until the roads are as long as the exemplar's, scaled to the
    map. Each road is cut where it reaches a base's pad, a town's box or a tech building, and where
    it runs onto a road already laid, then simplified to the exemplar's segment length; the
    templates go to the longest roads first in the exemplar's mix."""
    roads = [r for r in src.inventory["roads"] if r.get("outside_length", 0.0) > 0.0]
    if not roads:
        return
    scale = 2.0 * f.half / float(np.mean(src.inventory["size_cells"]))
    wanted = scale * sum(r["outside_length"] for r in roads)
    if wanted < ROAD_MIN:
        plan.notes.append("roads outside the towns (the exemplar's are %d cells long)" % wanted)
        return
    shape = h.shape
    cliff = span_cliff(np.rint(h))
    open_ = walkable(cliff) & ~(box(plan.wet, 2) > 0) & f.play
    # the places: starts, towns, tech, docks, and as many ways off the map as the exemplar's roads
    # have, evenly round the edge from a bearing the seed turns, each the nearest open edge cell
    docks = [(int(y) + skel.m.border, int(x) + skel.m.border) for x, y in skel.m.supplies]
    nodes = list(skel.starts) + [(ty + rows // 2, tx + cols // 2) for ty, tx, _, _, rows, cols in plan.stamps] + \
        [p for _, p in plan.tech] + docks
    rim = f.play & ~(box(f.play.astype(float), 1) > 0.99) & open_
    exits = src.inventory.get("road_exits", 0)
    if exits and rim.any():
        ys, xs = np.nonzero(rim)
        bearings = np.arctan2(ys - f.cy, xs - f.cx)
        for k in range(exits):
            angle = (2.0 * math.pi * (k + hash_unit(seed, 97, 0)) / exits + math.pi) % (2.0 * math.pi) - math.pi
            pick = int(np.argmin(np.abs((bearings - angle + math.pi) % (2.0 * math.pi) - math.pi)))
            nodes.append((int(ys[pick]), int(xs[pick])))
    if len(nodes) < 2:
        return
    for _, a, z in plan.bridges:
        for y, x in (a, z):
            open_[int(y) - 2:int(y) + 3, int(x) - 2:int(x) + 3] = True
    for y, x in nodes:
        open_[int(y) - 2:int(y) + 3, int(x) - 2:int(x) + 3] = True
    gy, gx = np.gradient(h)
    cost = 1.0 + ROAD_CLIMB * np.hypot(gy, gx)
    index = np.arange(h.size).reshape(shape)
    rows_, cols_, weights = [], [], []
    for dy, dx in ((0, 1), (1, 0), (1, 1), (1, -1)):
        a = (slice(0, shape[0] - dy), slice(max(-dx, 0), shape[1] - max(dx, 0)))
        b = (slice(dy, shape[0]), slice(max(dx, 0), shape[1] + min(dx, 0)))
        both = open_[a] & open_[b]
        rows_.append(index[a][both])
        cols_.append(index[b][both])
        weights.append((0.5 * (cost[a] + cost[b]) * math.hypot(dy, dx))[both])
    for _, a, z in plan.bridges:
        rows_.append(np.array([index[int(a[0]), int(a[1])]]))
        cols_.append(np.array([index[int(z[0]), int(z[1])]]))
        weights.append(np.array([math.hypot(z[0] - a[0], z[1] - a[1])]))
    graph = sparse.csr_matrix((np.concatenate(weights), (np.concatenate(rows_), np.concatenate(cols_))),
                              shape=(h.size, h.size))
    starts_ = [index[int(y), int(x)] for y, x in nodes]
    distance, previous = csgraph.dijkstra(graph, directed=False, indices=starts_, return_predecessors=True)
    between = distance[:, starts_]
    tree = csgraph.minimum_spanning_tree(np.where(np.isfinite(between), between + 1e-6, 0.0)).toarray()
    joins = sorted(((i, j) for i in range(len(nodes)) for j in range(len(nodes)) if tree[i, j] > 0),
                   key=lambda ij: between[ij])
    others = sorted((between[i, j], i, j) for i in range(len(nodes)) for j in range(i + 1, len(nodes))
                    if np.isfinite(between[i, j]) and (i, j) not in joins and (j, i) not in joins)

    # where a road stops: a pad, a town's box, a tech building's ground
    stop = np.zeros(shape, bool)
    for y, x in skel.starts:
        stop |= np.hypot(f.yy - y, f.xx - x) < PAD_RADIUS[0]
    for ty, tx, _, _, rows, cols in plan.stamps:
        stop[ty:ty + rows, tx:tx + cols] = True
    for _, (y, x) in plan.tech:
        stop |= np.hypot(f.yy - y, f.xx - x) < TECH_FLAT + 2
    for y, x in docks:
        stop |= np.hypot(f.yy - y, f.xx - x) < SITE_RADIUS[0]
    used = np.zeros(shape, bool)
    polylines = []
    laid = 0.0
    for i, j in joins + [(i, j) for _, i, j in others]:
        if laid >= wanted:
            break
        cell, run = starts_[j], []
        while cell != starts_[i] and cell >= 0:
            run.append(divmod(int(cell), shape[1]))
            cell = previous[i, cell]
        run.append(divmod(int(starts_[i]), shape[1]))
        # cut it into the pieces between stops, bridges (a jump) and roads already laid
        piece = []
        for k, (y, x) in enumerate(run):
            jump = k and max(abs(y - run[k - 1][0]), abs(x - run[k - 1][1])) > 1
            if stop[y, x] or used[y, x] or jump:
                if len(piece) > ROAD_LEAST:
                    polylines.append(piece)
                piece = [] if (stop[y, x] or used[y, x]) else [(y, x)]
                continue
            piece.append((y, x))
        if len(piece) > ROAD_LEAST:
            polylines.append(piece)
        for y, x in run:
            if not stop[y, x]:
                used[max(y - ROAD_SPACING, 0):y + ROAD_SPACING + 1, max(x - ROAD_SPACING, 0):x + ROAD_SPACING + 1] = True
        laid = sum(len(p) for p in polylines)
    # the templates by share of length, the longest roads to the most used
    polylines.sort(key=len, reverse=True)
    total = sum(r["outside_length"] for r in roads)
    owed = {r["template"]: r["outside_length"] / total * laid for r in roads}
    mask = np.zeros(shape, bool)
    for piece in polylines:
        template = max(owed, key=lambda t: owed[t])
        owed[template] -= len(piece)
        step = next(r["segment_length"] for r in roads if r["template"] == template)
        points = simplify(piece, ROAD_TOLERANCE)
        points = [p for a, z in zip(points, points[1:]) for p in subdivide(a, z, step)] + [points[-1]]
        plan.roads.append((template, [(float(y), float(x)) for y, x in points]))
        for y, x in piece:
            mask[y, x] = True
    plan.keepout |= box(mask, ROAD_CLEAR) > 0


def simplify(points, tolerance):
    """Douglas and Peucker: the fewest of the points that keep the line within tolerance cells."""
    if len(points) < 3:
        return list(points)
    (ay, ax), (zy, zx) = points[0], points[-1]
    span = math.hypot(zy - ay, zx - ax) or 1.0
    far = [abs((zy - ay) * (x - ax) - (zx - ax) * (y - ay)) / span for y, x in points]
    k = int(np.argmax(far))
    if far[k] <= tolerance:
        return [points[0], points[-1]]
    return simplify(points[:k + 1], tolerance)[:-1] + simplify(points[k:], tolerance)


def subdivide(a, z, step):
    """a and the points between a and z no further apart than step (z itself left out)."""
    pieces = max(int(math.ceil(math.hypot(z[0] - a[0], z[1] - a[1]) / max(step, 1.0))), 1)
    return [(a[0] + (z[0] - a[0]) * k / pieces, a[1] + (z[1] - a[1]) * k / pieces) for k in range(pieces)]


#-----------------------------------------------------------------------------------------------
# Quilting
#-----------------------------------------------------------------------------------------------

def min_cut(error):
    """Column per row of the cheapest top-to-bottom path through error (rows x cols)."""
    rows, cols = error.shape
    total = error.copy()
    for r in range(1, rows):
        prev = total[r - 1]
        left = np.concatenate(([np.inf], prev[:-1]))
        right = np.concatenate((prev[1:], [np.inf]))
        total[r] += np.minimum(prev, np.minimum(left, right))
    path = np.zeros(rows, np.int32)
    path[-1] = int(np.argmin(total[-1]))
    for r in range(rows - 2, -1, -1):
        c = path[r + 1]
        lo, hi = max(c - 1, 0), min(c + 2, cols)
        path[r] = lo + int(np.argmin(total[r, lo:hi]))
    return path


def positions(size):
    out = list(range(0, size - PATCH, PATCH - OVERLAP)) + [size - PATCH]
    return sorted(set(out))


def protection(skel):
    """1 where the skeleton's own height stays: the pads, the sites, the walkable slopes."""
    m = skel.m
    yy, xx = np.mgrid[0:m.height, 0:m.width]

    def ring(points, inner, outer):
        out = np.zeros((m.height, m.width))
        for y, x in points:
            d = np.hypot(yy - y, xx - x)
            out = np.maximum(out, np.clip((outer - d) / float(outer - inner), 0, 1))
        return out

    near = np.full((m.height, m.width), np.inf)
    for y, x in skel.starts:
        near = np.minimum(near, np.hypot(yy - y, xx - x))
    slope = (walkable(span_cliff(skel.h)) & (near < SLOPE_REACH) &
             (((skel.t > 0.15) & (skel.t < 0.85)) | ((skel.t > 1.1) & (skel.t < 1.45))))
    slope = np.clip(box(slope, 2) * 2.0 - 0.5, 0, 1)
    sites = [(int(y + 0.5) + m.border, int(x + 0.5) + m.border) for x, y in m.supplies + m.derricks]
    pads = np.maximum(ring(skel.starts, *PAD_RADIUS), ring(sites, *SITE_RADIUS))
    return np.maximum(pads, slope), pads


def quilt(src, skel, keep, seed, field, stamps, trees_allowed, tree_rate):
    """One source cell for every target cell, (sy, sx) arrays, and field read through them. The
    stamps are placed first and never written over, so the patches round them fit them. A patch
    also pays for holding more or fewer trees on the ground where trees may stand than tree_rate
    a cell of it."""
    th, tw = skel.h.shape
    sh, sw = src.h.shape
    weight = 1.0 + 4.0 * keep
    target_cliff = span_cliff(skel.h).astype(np.float64)
    source_cliff = src.cliff.astype(np.float64)
    windows = np.lib.stride_tricks.sliding_window_view
    src_guide = windows(src.guide, (PATCH, PATCH))
    src_cliff = windows(source_cliff, (PATCH, PATCH))
    src_t = windows(field, (PATCH, PATCH))
    src_cls = windows(src.cls, (PATCH, PATCH))
    src_trees = windows(src.trees, (PATCH, PATCH))
    # source patches touching a town are never picked
    count = np.pad(src.town.astype(np.int32).cumsum(0).cumsum(1), ((1, 0), (1, 0)))
    banned = (count[PATCH:, PATCH:] - count[:-PATCH, PATCH:] - count[PATCH:, :-PATCH] + count[:-PATCH, :-PATCH]) > 0

    sy_map = np.zeros((th, tw), np.int32)
    sx_map = np.zeros((th, tw), np.int32)
    t_out = np.zeros((th, tw))
    cls_out = np.zeros((th, tw), np.int32)
    filled = np.zeros((th, tw), bool)
    locked = np.zeros((th, tw), bool)
    for ty, tx, sy, sx, rows, cols in stamps:
        yy, xx = np.mgrid[0:rows, 0:cols]
        sy_map[ty + yy, tx + xx] = sy + yy
        sx_map[ty + yy, tx + xx] = sx + xx
        t_out[ty:ty + rows, tx:tx + cols] = field[sy:sy + rows, sx:sx + cols]
        cls_out[ty:ty + rows, tx:tx + cols] = src.cls[sy:sy + rows, sx:sx + cols]
        filled[ty:ty + rows, tx:tx + cols] = True
        locked[ty:ty + rows, tx:tx + cols] = True
    used = np.zeros((sh, sw), np.int32)
    samples = range(0, PATCH, COARSE)
    ny = (sh - PATCH) // COARSE + 1
    nx = (sw - PATCH) // COARSE + 1
    near = np.arange(-COARSE // 2, COARSE // 2 + 1)

    xs, ys = positions(tw), positions(th)
    patch_no = 0
    for row, ty in enumerate(ys):
        for col, tx in enumerate(xs):
            # coarse: every fourth source offset, every fourth cell of the guide and cliff flag. A
            # source patch may be lifted or lowered as a whole (shift), at a price, so the skeleton
            # sets the level and the exemplar the shape.
            s1 = np.zeros((ny, nx))
            s2 = np.zeros((ny, nx))
            cliffs = np.zeros((ny, nx))
            total = 0.0
            for i in samples:
                for j in samples:
                    g = skel.guide[ty + i, tx + j]
                    c = target_cliff[ty + i, tx + j]
                    w = weight[ty + i, tx + j]
                    d = g - src.guide[i:i + COARSE * ny:COARSE, j:j + COARSE * nx:COARSE]
                    cs = source_cliff[i:i + COARSE * ny:COARSE, j:j + COARSE * nx:COARSE]
                    s1 += w * d
                    s2 += w * d * d
                    cliffs += w * (cs - c) ** 2
                    total += w
            shift = s1 / total
            cost = (s2 / total - shift ** 2) + SHIFT_WEIGHT * shift ** 2 + CLIFF_WEIGHT * cliffs / total
            cost[banned[::COARSE, ::COARSE][:ny, :nx]] = np.inf
            order = np.argsort(cost, axis=None, kind="stable")[:REFINE]

            # full: every offset round the best coarse ones, every cell, the overlap with what is placed
            sy = (COARSE * (order // nx))[:, None, None] + near[None, :, None]
            sx = (COARSE * (order % nx))[:, None, None] + near[None, None, :]
            sy, sx = np.broadcast_arrays(sy, sx)
            pick = (sy >= 0) & (sx >= 0) & (sy <= sh - PATCH) & (sx <= sw - PATCH)
            flat = np.unique(sy[pick] * sw + sx[pick])
            sy, sx = flat // sw, flat % sw
            tg = skel.guide[ty:ty + PATCH, tx:tx + PATCH]
            tc = target_cliff[ty:ty + PATCH, tx:tx + PATCH]
            tw_ = weight[ty:ty + PATCH, tx:tx + PATCH]
            have = filled[ty:ty + PATCH, tx:tx + PATCH]
            shift = (tw_ * (tg - src_guide[sy, sx])).sum((1, 2)) / tw_.sum()
            lifted = src_guide[sy, sx] + shift[:, None, None]
            c = (tw_ * ((lifted - tg) ** 2 +
                        CLIFF_WEIGHT * (src_cliff[sy, sx] - tc) ** 2)).sum((1, 2)) / tw_.sum()
            c += SHIFT_WEIGHT * shift ** 2
            if have.any():
                c += OVERLAP_WEIGHT * (
                    ((src_t[sy, sx] - t_out[ty:ty + PATCH, tx:tx + PATCH]) ** 2 +
                     CLASS_WEIGHT * (src_cls[sy, sx] != cls_out[ty:ty + PATCH, tx:tx + PATCH]))
                    * have).sum((1, 2)) / have.sum()
            free = trees_allowed[ty:ty + PATCH, tx:tx + PATCH] & ~have
            expected = tree_rate * free.sum()
            held = (src_trees[sy, sx] * free).sum((1, 2))
            c += SCENERY_WEIGHT * (held - expected) ** 2 / max(expected, 1.0)
            c += REUSE_WEIGHT * used[sy, sx]
            c[banned[sy, sx]] = np.inf
            c *= 1.0 + JITTER * np.array([hash_unit(seed, patch_no, int(f)) for f in flat])
            best = int(np.argmin(c))
            sy, sx = int(sy[best]), int(sx[best])
            used[max(sy - 4, 0):sy + 5, max(sx - 4, 0):sx + 5] += 1
            window = (slice(sy, sy + PATCH), slice(sx, sx + PATCH))

            # the seam: a minimum cut through the left overlap and one through the top
            new = np.ones((PATCH, PATCH), bool)
            error = ((field[window] - t_out[ty:ty + PATCH, tx:tx + PATCH]) ** 2 +
                     CLASS_WEIGHT * (src.cls[window] != cls_out[ty:ty + PATCH, tx:tx + PATCH]))
            if col > 0:
                width = xs[col - 1] + PATCH - tx
                path = min_cut(error[:, :width])
                new &= np.arange(PATCH)[None, :] >= path[:, None]
            if row > 0:
                height = ys[row - 1] + PATCH - ty
                path = min_cut(error[:height, :].T)
                new &= np.arange(PATCH)[:, None] >= path[None, :]
            new |= ~have
            new &= ~locked[ty:ty + PATCH, tx:tx + PATCH]

            yy, xx = np.nonzero(new)
            sy_map[ty + yy, tx + xx] = sy + yy
            sx_map[ty + yy, tx + xx] = sx + xx
            t_out[ty + yy, tx + xx] = field[sy + yy, sx + xx]
            cls_out[ty + yy, tx + xx] = src.cls[sy + yy, sx + xx]
            filled[ty + yy, tx + xx] = True
            patch_no += 1
    return sy_map, sx_map, t_out, patch_no


#-----------------------------------------------------------------------------------------------
# Assembly
#-----------------------------------------------------------------------------------------------

def seams(sy_map, sx_map):
    th, tw = sy_map.shape
    dy = sy_map - np.arange(th)[:, None]
    dx = sx_map - np.arange(tw)[None, :]
    seam = np.zeros((th, tw), bool)
    seam[:, :-1] |= (dy[:, :-1] != dy[:, 1:]) | (dx[:, :-1] != dx[:, 1:])
    seam[:-1, :] |= (dy[:-1, :] != dy[1:, :]) | (dx[:-1, :] != dx[1:, :])
    return box(seam, SEAM_BAND) > 0


def level(h, y, x, radius):
    """Flatten a disc to its median, the way a site's footprint wants it."""
    yy, xx = np.ogrid[0:h.shape[0], 0:h.shape[1]]
    disc = (yy - y) ** 2 + (xx - x) ** 2 <= radius ** 2
    h[disc] = int(np.median(h[disc]))


def blend_chunk(src, sy_map, sx_map, cliff):
    """BlendTileData, version 8: every cell's class and blends from its source cell, the tiles
    worked out again for where the cell landed, the exemplar's texture table, a blend table of the
    blends that occur, no cliff texture mapping (Twilight's cells use none) and the cliff bits."""
    th, tw = sy_map.shape
    yy, xx = np.mgrid[0:th, 0:tw]
    cls = src.cls[sy_map, sx_map]
    layers = [tile_for(cls, src.first, src.width, yy, xx)]
    keys = []
    for layer in (1, 2):
        entry = src.layers[layer][sy_map, sx_map].astype(np.int64)
        over = src.blend_class[entry]
        tile = tile_for(over, src.first, src.width, yy, xx)
        keys.append(np.where(entry > 0, (tile * len(src.shapes) + src.blend_shape[entry]) + 1, 0))
    table, index = np.unique(np.concatenate([k.ravel() for k in keys]), return_inverse=True)
    if table[0] != 0:                           # entry 0, "no blend", is always there
        table = np.concatenate(([0], table))
        index = index + 1
    if len(table) > 16192:                      # NUM_BLEND_TILES
        raise ValueError("%d blends, the renderer holds 16192" % len(table))
    layers += [index[:th * tw].reshape(th, tw), index[th * tw:].reshape(th, tw)]
    layers.append(np.zeros((th, tw), np.int64))

    out = struct.pack("<i", th * tw) + b"".join(l.astype("<i2").tobytes() for l in layers)
    row = (tw + 7) // 8
    out += np.packbits(np.pad(cliff, ((0, 0), (0, row * 8 - tw))), axis=1, bitorder="little").tobytes()
    out += struct.pack("<3i", src.bitmap_tiles, len(table), 1) + src.table_head
    for key in table[1:]:
        tile, shape = divmod(int(key) - 1, len(src.shapes))
        flags, custom = src.shapes[shape]
        out += struct.pack("<i6sii", tile, flags, custom, 0x7ADA0000)
    return out


def more_trees(src, sy_map, sx_map, allowed, wanted, seed):
    """Clumps of the exemplar's trees laid on allowed ground until wanted trees stand, for what the
    quilting could not bring: a clump is every tree within CLUMP_CELLS of one of the exemplar's,
    put down on a cell of the same texture class, on cells that hold no tree yet."""
    trees = sorted(cell for cell, objects in src.scenery.items() if any(o[0].startswith("Tree") for o in objects))
    ground = src.cls[sy_map, sx_map]
    cells = np.argwhere(allowed)
    free = allowed.copy()
    out = []
    for attempt in range(TREE_TRIES * max(wanted, 1)):
        if len(out) >= wanted or not len(cells):
            break
        y, x = cells[hash_cell(seed, 501, attempt) % len(cells)]
        sy, sx = trees[hash_cell(seed, 502, attempt) % len(trees)]
        if src.cls[sy, sx] != ground[y, x]:
            continue
        clump = [(ty - sy, tx - sx, o) for ty, tx in trees
                 if abs(ty - sy) <= CLUMP_CELLS and abs(tx - sx) <= CLUMP_CELLS
                 for o in src.scenery[(ty, tx)] if o[0].startswith("Tree")]
        spots = [(y + dy, x + dx) for dy, dx, _ in clump]
        if not all(0 <= py < free.shape[0] and 0 <= px < free.shape[1] and free[py, px] for py, px in spots):
            continue
        for (py, px), (_, _, (template, fx, fy, angle)) in zip(spots, clump):
            out.append((template, px + fx, py + fy, angle))
            free[max(py - 1, 0):py + 2, max(px - 1, 0):px + 2] = False
    return out


def bearing_of(centre, point):
    return math.atan2(point[0] - centre[0], point[1] - centre[1])


def place_on_bearing(h, ok, centre, angle, inner, outer):
    """The furthest cell out along a bearing, between two radii, where ok holds over a 5x5."""
    for r in np.arange(outer, inner, -1.0):
        y = int(centre[0] + r * math.sin(angle) + 0.5)
        x = int(centre[1] + r * math.cos(angle) + 0.5)
        if ok[y - 2:y + 3, x - 2:x + 3].all() and np.ptp(h[y - 2:y + 3, x - 2:x + 3]) <= 4:
            return y, x
    return None


def synthesise(skeleton_path, src, out_dir, seed):
    began = time.time()
    with open(skeleton_path, "rb") as handle:
        data = handle.read()
    skel = Grid(data)
    raw = RawMap(data)
    m = skel.m
    th, tw = skel.h.shape
    play = np.zeros((th, tw), bool)
    play[m.border:m.border + m.playable_y, m.border:m.border + m.playable_x] = True

    # the exemplar's large features first, then what of the skeleton keeps its own height
    plan = compose(skel, src, seed)
    for _, a, z in plan.bridges:
        plan.keepout |= box(line_cells(a, z, (th, tw)), BRIDGE_CLEAR) > 0
    keep, pads = protection(skel)
    gentle = plan.gentle & walkable(span_cliff(skel.h))
    keep = np.maximum(keep, np.clip(box(gentle, 2) * 2.0 - 0.5, 0, 1))
    keep = np.maximum(keep, box(plan.wet, 2) > 0)
    paths = box(waypoint_paths(raw, (th, tw), m.border), PATH_CELLS) > 0
    trees_allowed = play & (pads < 0.9) & (keep < 0.5) & ~paths & ~plan.keepout & ~(box(plan.wet, 2) > 0)
    # as many trees as the exemplar's density gives the open ground, all of them where trees may stand
    # less the trees the towns bring with them, which the exemplar's density counted too
    in_towns = sum(float(src.trees[sy:sy + rows, sx:sx + cols].sum()) for _, _, sy, sx, rows, cols in plan.stamps)
    trees_wanted = max(src.tree_density * (play & (pads < 0.9) & ~plan.wet).sum() - in_towns, 0.0)

    # the exemplar's detail: what its height has that its own blur does not
    sy_map, sx_map, detail, patches = quilt(src, skel, keep, seed, src.t - box(src.t, DETAIL_RADIUS),
                                            plan.stamps, trees_allowed, trees_wanted / trees_allowed.sum())
    quilted = time.time()
    stamped = np.zeros((th, tw), bool)
    for ty, tx, _, _, rows, cols in plan.stamps:
        stamped[ty:ty + rows, tx:tx + cols] = True

    # heights: the skeleton's, with the detail on top, seams smoothed. The skeleton's cliffs take
    # none (an exemplar edge laid a cell or two off one doubles it) and the kept ground none either.
    detail = np.where(seams(sy_map, sx_map) & ~stamped, box(detail, 1), detail)
    rim = np.clip(box(span_cliff(skel.h), RIM_CELLS) * 4.0, 0, 1)
    t = skel.t + (1.0 - rim) * detail
    h = skel.bowl + t * skel.scale
    h = keep * skel.h + (1.0 - keep) * h
    # ground under a sea's plane but not the sea's (the detail can dip it) would be a pond
    dry = (plan.level >= 0.0) & ~plan.wet
    h = np.where(dry, np.maximum(h, plan.level + 1.0), h)
    h = np.clip(np.rint(h), 0, 255).astype(np.int32)
    cliff = span_cliff(h)       # Twilight's painted bits are exactly its heights' (selfcheck)

    # the skeleton's money and seats stay, its scenery goes; the piles move
    _, objects = raw.body("ObjectsList")
    kept, piles = [], []
    civil = mapfeatures.civilian_buildings()
    road = mapfeatures.FLAG_ROAD_POINT1 | mapfeatures.FLAG_ROAD_POINT2
    for chunk in split_chunks(objects, 0, len(objects)):
        template = object_head(chunk[2])[3]
        # the generator's dressing goes, its towns and roads with its trees: the exemplar's come instead
        if SKELETON_SCENERY.match(template) or template.lower() in civil or \
                struct.unpack_from("<i", chunk[2], 16)[0] & road:
            continue
        (piles if template == "SupplyPileSmall" else kept).append(chunk)

    wet = plan.wet & (h < plan.level)
    decks = np.zeros((th, tw), bool)
    for _, a, z in plan.bridges:
        decks |= box(line_cells(a, z, (th, tw)), 1) > 0
    passable = walkable(cliff) & ~wet | decks
    reached = reach(passable, skel.starts[0])
    open_ground = passable & reached & ~plan.keepout & ~wet

    # the piles: where the exemplar keeps its own when they stand on high ground (Twilight's, up
    # between two bases, a pair halfway to the next seat and a pair a third of the way), with what
    # stands beside the exemplar's; otherwise where the skeleton put them
    piles_of = src.inventory["piles"]
    company = [t for t, share in piles_of["company"].items() if share >= 0.5]
    spots = []
    if piles_of["count"] and piles_of["dz_start"] >= mapfeatures.HIGH_RISE:
        centre = skel.centre
        seats = sorted(range(len(skel.starts)), key=lambda i: bearing_of(centre, skel.starts[i]))
        angles = [bearing_of(centre, skel.starts[i]) for i in seats]
        seat_ring = np.mean([math.hypot(y - centre[0], x - centre[1]) for y, x in skel.starts])
        outer = m.playable / 2.0 - 12.0
        high = open_ground & (h > skel.plateau + 0.25 * skel.scale)
        for k in range(len(seats)):
            a = angles[k]
            gap = (angles[(k + 1) % len(seats)] - a) % (2 * math.pi)
            for share in (0.5, 0.38 if k % 2 == 0 else 0.62):       # the mid pair, then the field pair
                for ground in (high, open_ground):
                    for nudge in (0.0, 0.05, -0.05, 0.1, -0.1):
                        angle = a + (share + nudge) * gap
                        spot = place_on_bearing(h, ground, centre, angle, 0.6 * seat_ring, outer)
                        if spot:
                            break
                    if spot:
                        spots.append((spot, angle))
                        break
    sites = []
    added = []
    for (y, x), angle in spots:
        ty, tx = math.cos(angle) * 4.0, -math.sin(angle) * 4.0
        sites.extend([(y + ty, x + tx), (y - ty, x - tx)])
        for number, template in enumerate(company):
            ry, rx = math.sin(angle) * (6.0 + 6.0 * number), math.cos(angle) * (6.0 + 6.0 * number)
            added.append((template, x + rx + 0.5, y + ry + 0.5, angle + math.pi))
    for index, chunk in enumerate(piles):
        cid, version, body = chunk
        if index < len(sites):
            y, x = sites[index]
            body = struct.pack("<2f", (x - m.border) * MAP_XY_FACTOR,
                               (y - m.border) * MAP_XY_FACTOR) + body[8:]
            level(h, int(y), int(x), PILE_FLAT)
        kept.append((cid, version, body))

    # the tech buildings compose() placed
    for template, (y, x) in plan.tech:
        level(h, y, x, TECH_FLAT)
        added.append((template, x + 0.5, y + 0.5, 0.0))

    # the skeleton's AI paths, and a pile left where the skeleton put it: one the river, a new
    # cliff or a pocket off the ground the starts share now holds goes to the nearest cell they do
    # share, or a wave walks at a bank or a wall it cannot reach and stands there wedged. Clear of
    # the water by a few cells, as the water area's polygon is drawn round the bed loosely, and
    # after the levelling, which can ring a pad with cliff.
    level_wet = plan.wet & (h < plan.level)
    cliff = span_cliff(h)
    ground = reach(walkable(cliff) & ~level_wet | decks, skel.starts[0])
    ground &= ~(box(level_wet, 3) > 0) & ~decks
    _, (near_y, near_x) = ndimage.distance_transform_edt(~ground, return_indices=True)
    snapped = 0
    for index, (cid, version, body) in enumerate(kept):
        x, y, _, template = object_head(body)
        cy, cx = int(y / MAP_XY_FACTOR) + m.border, int(x / MAP_XY_FACTOR) + m.border
        if template not in ("*Waypoints/Waypoint", "SupplyPileSmall") or ground[cy, cx]:
            continue
        ty, tx = near_y[cy, cx], near_x[cy, cx]
        body = struct.pack("<2f", (tx - m.border + 0.5) * MAP_XY_FACTOR,
                           (ty - m.border + 0.5) * MAP_XY_FACTOR) + body[8:]
        kept[index] = (cid, version, body)
        snapped += 1

    # the exemplar's scenery, wherever its cell landed off the pads, the kept ways down, the
    # features' edges and the water; a tree only where the quilting counted trees
    hit = np.zeros(src.h.shape, bool)
    for cell in src.scenery:
        hit[cell] = True
    landed = hit[sy_map, sx_map] & play & (pads < 0.9) & (keep < 0.95) & ~plan.keepout & ~(box(plan.wet, 2) > 0) & ~stamped
    taken = [(int(p[0]), int(p[1])) for p in sites] + [p for _, p in plan.tech]
    for y, x in zip(*np.nonzero(landed)):
        if any(abs(y - py) < 6 and abs(x - px) < 6 for py, px in taken):
            continue
        for template, fx, fy, angle in src.scenery[(sy_map[y, x], sx_map[y, x])]:
            if template.startswith("Tree") and not trees_allowed[y, x]:
                continue
            added.append((template, x + fx, y + fy, angle))
    standing = np.zeros((th, tw), bool)
    for template, cx, cy, _ in added:
        standing[int(cy), int(cx)] |= template.startswith("Tree")
    free = trees_allowed & ~(box(standing, 1) > 0)
    for py, px in taken:
        free[max(py - 6, 0):py + 7, max(px - 6, 0):px + 7] = False
    added += more_trees(src, sy_map, sx_map, free, int(round(trees_wanted)) - int(standing.sum()), seed)

    counter = [0]

    def neutral(template, cx, cy, angle):
        counter[0] += 1
        ids = raw.ids
        body = struct.pack("<4fi", (cx - m.border) * MAP_XY_FACTOR, (cy - m.border) * MAP_XY_FACTOR,
                           0.0, angle, 0)
        body += ascii_value(template)
        entries = [("objectInitialHealth", 1, struct.pack("<i", 100)),
                   ("objectEnabled", 0, b"\1"), ("objectIndestructible", 0, b"\0"),
                   ("objectUnsellable", 0, b"\0"), ("objectPowered", 0, b"\1"),
                   ("objectRecruitableAI", 0, b"\1"), ("objectName", 3, b""),
                   ("originalOwner", 3, b"team"),
                   ("uniqueID", 3, ("%s synth%d" % (template, counter[0])).encode("latin-1")),
                   ("objectLayer", 3, b""), ("objectSelectable", 0, b"\1")]
        body += struct.pack("<H", len(entries))
        for key, kind, value in entries:
            body += struct.pack("<I", ids[key] << 8 | kind)
            body += struct.pack("<H", len(value)) + value if kind == 3 else value
        return (raw.ids["Object"], 3, body)

    # no tower, hut or tent where the new ground has a cliff (rocks may stand there): the AI sends
    # infantry to garrison a tower, and on Heartland ten Tank Hunters stood wedged at the foot of
    # the cliff under one for 1354 of the match's 1359 wedges
    kept.extend(neutral(*entry) for entry in added
                if not (STANDING.match(entry[0]) and cliff[int(entry[2]), int(entry[1])]))

    # the bridges, each the exemplar's own pair of objects for its template, moved to its two ends
    pairs = {}
    for (_, _, first), (_, _, second) in zip(src.objects, src.objects[1:]):
        flags = struct.unpack_from("<i", first, 16)[0], struct.unpack_from("<i", second, 16)[0]
        if flags[0] & mapfeatures.FLAG_BRIDGE_POINT1 and flags[1] & mapfeatures.FLAG_BRIDGE_POINT2:
            pairs.setdefault(object_head(first)[3], (first, second))
    for template, a, z in plan.bridges:
        for body, (y, x) in zip(pairs[template], (a, z)):
            counter[0] += 1
            kept.append(moved_object(body, src.raw.names, raw, x - m.border, y - m.border, counter[0]))

    # the roads, a pair of objects a segment, each the exemplar's own pair for its template with the
    # end flags (the corner type) most of its pairs carry, the segments of a road sharing their ends
    road_pairs = {}
    common = {r["template"]: tuple(r.get("flags", ())) for r in src.inventory["roads"]}
    for (_, _, first), (_, _, second) in zip(src.objects, src.objects[1:]):
        flags = struct.unpack_from("<i", first, 16)[0], struct.unpack_from("<i", second, 16)[0]
        if flags[0] & mapfeatures.FLAG_ROAD_POINT1 and flags[1] & mapfeatures.FLAG_ROAD_POINT2:
            template = object_head(first)[3]
            if template not in road_pairs or (flags == common.get(template) and
                                              road_pairs[template][2] != flags):
                road_pairs[template] = (first, second, flags)
    segments = 0
    for template, points in plan.roads:
        for a, z in zip(points, points[1:]):
            for body, (y, x) in zip(road_pairs[template][:2], (a, z)):
                counter[0] += 1
                kept.append(moved_object(body, src.raw.names, raw, x - m.border, y - m.border, counter[0]))
            segments += 1

    # the towns: every object of the exemplar inside each stamped box, moved with it; a road only
    # with both its ends inside
    copied = 0
    for ty, tx, sy, sx, rows, cols in plan.stamps:
        def inside(body):
            x, y, _, _ = object_head(body)
            cy, cx = y / MAP_XY_FACTOR + src.m.border, x / MAP_XY_FACTOR + src.m.border
            return sy <= cy < sy + rows and sx <= cx < sx + cols, (cy - sy + ty, cx - sx + tx)

        objects_ = src.objects
        index = 0
        while index < len(objects_):
            body = objects_[index][2]
            template = object_head(body)[3]
            flags = struct.unpack_from("<i", body, 16)[0]
            group = [body]
            if flags & mapfeatures.FLAG_ROAD_POINT1 and index + 1 < len(objects_):
                group.append(objects_[index + 1][2])
            index += len(group)
            places = [inside(b) for b in group]
            if not all(ok for ok, _ in places) or flags & (mapfeatures.FLAG_BRIDGE_POINT1 | mapfeatures.FLAG_BRIDGE_POINT2):
                continue
            if any(key == "waypointID" for key, _, _ in object_entries(body, src.raw.names)) or \
                    re.match(r"(?i)(supply|tech|.*waypoint)", template):
                continue
            # the levelled box feathers into the ground round it, and a house in that rim can sit on a cliff
            if any(cliff[int(cy), int(cx)] for _, (cy, cx) in places):
                continue
            for b, (_, (cy, cx)) in zip(group, places):
                counter[0] += 1
                copied += 1
                kept.append(moved_object(b, src.raw.names, raw, cx - m.border, cy - m.border, counter[0]))
    raw.replace("ObjectsList", 3, join_chunks(kept))

    version, body = raw.body("HeightMapData")
    raw.replace("HeightMapData", version, body[:-th * tw] + h.astype(np.uint8).tobytes())
    cliff = span_cliff(h)
    raw.replace("BlendTileData", 8, blend_chunk(src, sy_map, sx_map, cliff))
    version, body = raw.body("PolygonTriggers")
    raw.replace("PolygonTriggers", version, water_triggers(version, body, plan.water, m.border))
    raw.replace("GlobalLighting", *src.raw.body("GlobalLighting"))

    skeleton_name = os.path.splitext(os.path.basename(skeleton_path))[0]
    name = "%s_%s_%08x" % (skeleton_name, src.label, src.crc)
    # the shell names a map by WorldInfo's mapName, a label the map.str beside the map holds; with
    # none it logs "Missing TheKey_mapName!" and shows the file name
    version, body = raw.body("WorldInfo")
    entries = [e for e in dict_entries(body, 0, raw.names)[0] if e[0] != "mapName"]
    raw.replace("WorldInfo", version, encode_dict(entries + [("mapName", 3, ascii_value("MAP:" + name))], raw))
    # a folder of its own, as the map cache wants a map: Maps\<name>\<name>.map
    folder = os.path.join(out_dir, name)
    os.makedirs(folder, exist_ok=True)
    out = os.path.join(folder, name + ".map")
    with open(out, "wb") as handle:
        handle.write(raw.bytes())
    with open(os.path.join(folder, "map.str"), "w", newline="\r\n") as handle:
        handle.write('MAP:%s\n"%s synth %s"\nEND\n' % (name, src.label, skeleton_name))
    finished = time.time()

    # every start walks to every other one, and to every dock, derrick and tech building, over
    # the bridges and round the water
    passable = walkable(cliff) & ~(plan.wet & (h < plan.level)) | decks
    reached = reach(passable, skel.starts[0])
    docks = [(int(y + 0.5) + m.border, int(x + 0.5) + m.border) for x, y in m.supplies + m.derricks]

    def cut_off(points):
        return sum(1 for y, x in points if not reached[y - 2:y + 3, x - 2:x + 3].any())

    dry = reach(passable & ~decks, skel.starts[0])
    unbridged = [sum(1 for y, x in points if not dry[y - 2:y + 3, x - 2:x + 3].any())
                 for points in (skel.starts, [p for _, p in plan.tech])]
    trees = sum(1 for entry in added if entry[0].startswith("Tree"))
    print("%s: %d patches, quilt %.1f s, total %.1f s, %d scenery (%d trees, want %d), piles %d/%d, "
          "tech %d, bridges %d, roads %d (%d segments), towns %d (%d objects), water %d, waypoints moved %d, cut off: starts %d "
          "sites %d tech %d, without the bridges: starts %d tech %d" % (
              name, patches, quilted - began, finished - began, len(added) - len(plan.tech), trees,
              round(trees_wanted), min(len(piles), len(sites)), len(piles),
              len(plan.tech), len(plan.bridges), len(plan.roads), segments, len(plan.stamps), copied,
              len(plan.water), snapped,
              cut_off(skel.starts), cut_off(docks), cut_off([p for _, p in plan.tech]), *unbridged))
    for note in plan.notes:
        print("  not built: " + note)
    return out


#-----------------------------------------------------------------------------------------------
# Pictures
#-----------------------------------------------------------------------------------------------

def big_member(paths, member):
    wanted = member.lower()
    for path in paths:
        with open(path, "rb") as handle:
            for stored, offset, size in list(bigfile._read_index(handle)):
                if stored.lower() == wanted:
                    handle.seek(offset)
                    return handle.read(size)
    return None


_palette = {}


def texture_colour(name):
    """The average colour of a terrain class's texture, read off Terrain.ini and the art."""
    if not _palette:
        ini = big_member([os.path.join(RUN, "INIZH.big"), os.path.join(RUN, "ZH_Generals", "INI.big")],
                         "data\\ini\\terrain.ini").decode("latin-1")
        for terrain, texture in re.findall(r"^\s*Terrain\s+(\S+)\s.*?^\s*Texture\s*=\s*(\S+)",
                                           ini, re.M | re.S):
            _palette[terrain] = texture
    texture = _palette.get(name)
    if isinstance(texture, tuple):
        return texture
    colour = (128, 112, 90)
    if texture:
        art = big_member([os.path.join(RUN, "TerrainZH.big"), os.path.join(RUN, "ZH_Generals", "Terrain.big")],
                         "art\\terrain\\" + texture.lower())
        if art:
            import io
            colour = tuple(int(c) for c in np.asarray(Image.open(io.BytesIO(art)).convert("RGB"))
                           .reshape(-1, 3).mean(0))
    _palette[name] = colour
    return colour


def render(path):
    """Top-down picture of a map: each cell its texture's average colour, lit by the map's sun,
    water blue, starts white, docks blue, derricks black, piles yellow, tech magenta, trees dark
    green, bridges red lines, roads grey lines, civilian buildings dark squares."""
    with open(path, "rb") as handle:
        data = handle.read()
    m = mapstyle.StyleMap(data)
    h = np.frombuffer(bytes(m.heights), np.uint8).reshape(m.height, m.width).astype(np.float64)
    owner = np.zeros(4096, np.int32)
    for index, (name, first, count) in enumerate(m.texture_classes):
        owner[first:first + count] = index
    colours = np.array([texture_colour(name) for name, _, _ in m.texture_classes], np.float64)
    tiles = np.array(m.tiles, np.int32).reshape(m.height, m.width)
    rgb = colours[owner[tiles >> 2]]

    z = h * MAP_HEIGHT_SCALE
    gy, gx = np.gradient(z, MAP_XY_FACTOR)
    normal = np.stack([-gx, -gy, np.ones_like(z)])
    normal /= np.linalg.norm(normal, axis=0)
    light = m.lighting["terrain"]["direction"] if m.lighting else [-0.894, -0.19, -0.407]
    sun = -np.array(light)[:, None, None]
    shade = 0.45 + 0.75 * np.clip((normal * sun).sum(0) / np.linalg.norm(sun), 0, 1)
    rgb = np.clip(rgb * shade[..., None], 0, 255)
    wet = np.frombuffer(bytes(m.water_grid()), np.uint8).reshape(m.height, m.width).astype(bool)
    rgb[wet] = 0.35 * rgb[wet] + 0.65 * np.array([40.0, 80.0, 150.0])

    b = m.border
    rgb = rgb[b:b + m.playable_y, b:b + m.playable_x]
    image = Image.fromarray(rgb.astype(np.uint8)[::-1].copy())
    draw = ImageDraw.Draw(image)
    top = m.playable_y - 1

    def dot(x, y, r, colour):
        draw.ellipse((x - r, top - y - r, x + r, top - y + r), fill=colour, outline=(0, 0, 0))

    civil = mapfeatures.civilian_buildings()
    for template, x, y in m.objects:
        if template.lower() in civil:
            draw.rectangle((x - 1.5, top - y - 1.5, x + 1.5, top - y + 1.5), fill=(45, 40, 35))
        elif template.startswith("Tree"):
            draw.point((x, top - y), fill=(30, 60, 25))
        elif template.startswith("Rock"):
            draw.point((x, top - y), fill=(70, 70, 70))
    objects = mapfeatures.objects_in_order(data)
    for (_, x1, y1, f1), (_, x2, y2, f2) in zip(objects, objects[1:]):
        if f1 & mapfeatures.FLAG_ROAD_POINT1 and f2 & mapfeatures.FLAG_ROAD_POINT2:
            draw.line((x1, top - y1, x2, top - y2), fill=(150, 150, 150), width=1)
        elif f1 & mapfeatures.FLAG_BRIDGE_POINT1 and f2 & mapfeatures.FLAG_BRIDGE_POINT2:
            draw.line((x1, top - y1, x2, top - y2), fill=(230, 50, 40), width=3)
    for x, y in m.piles:
        dot(x, y, 2, (240, 210, 40))
    for x, y in m.supplies:
        dot(x, y, 3, (60, 120, 230))
    for x, y in m.derricks:
        dot(x, y, 3, (10, 10, 10))
    for template, x, y in m.objects:
        if template.startswith("Tech") and template != "TechOilDerrick":
            dot(x, y, 4, (220, 40, 200))
    for x, y in m.starts:
        dot(x, y, 4, (255, 255, 255))
    return image


def side_by_side(out_path, paths, labels=None):
    images = [render(p) for p in paths]
    size = max(max(i.size) for i in images)
    sheet = Image.new("RGB", (len(images) * (size + 8) + 8, size + 30), (24, 24, 24))
    draw = ImageDraw.Draw(sheet)
    for k, image in enumerate(images):
        x = 8 + k * (size + 8)
        sheet.paste(image, (x, 24 + (size - image.size[1]) // 2))
        label = labels[k] if labels else os.path.basename(paths[k])
        draw.text((x, 6), label, fill=(230, 230, 230))
    sheet.save(out_path)


#-----------------------------------------------------------------------------------------------
# Measures
#-----------------------------------------------------------------------------------------------

def texture_distance(a, b):
    """Half the summed difference of the two maps' texture shares: 0 the same mix, 1 none shared."""
    shares = [{t["name"]: t["share"] for t in p["textures"]["classes"]} for p in (a, b)]
    return 0.5 * sum(abs(shares[0].get(n, 0) - shares[1].get(n, 0)) for n in set(shares[0]) | set(shares[1]))


def table_row(result, exemplar):
    g = result["generator"]
    ground = result["ground"]
    objects = result["objects"]
    top = result["textures"]["classes"][:3]
    trees = sum(v for k, v in objects["trees"].items() if not k.startswith("Amb_"))
    tents = sum(v for k, v in objects["top_other"] if k.startswith("Tent"))
    return [
        ("cliff", "%.1f%%" % (100 * ground["cliff_share"])),
        ("mid cliff", "%.1f%%" % (100 * ground["middle_cliff_share"])),
        ("mid stdev", "%.1f" % ground["middle_height_stdev"]),
        ("mid step", "%.2f" % ground["middle_step_bytes"]),
        ("tex off exemplar", "%d%%" % round(100 * texture_distance(result, exemplar))),
        ("top tex", " ".join("%s %d" % (t["name"], round(100 * t["share"])) for t in top)),
        ("money", " ".join("%s %d" % (k, v) for k, v in sorted(result["money"]["classes"].items()))),
        ("trees", "%d (1/%s)" % (trees, g["cells_per_tree"])),
        ("tents/ref", "%d/%d" % (tents, objects["tech"].get("TechOilRefinery", 0))),
        ("raise", "%s" % g["start_raise_bytes"]),
        ("ramps", "%s (%s down, %s wide)" % (g["ramps_per_base"], g["ramps_down_per_base"],
                                             g["ramp_down_width_cells"])),
        ("plateau r", "%s" % g["base_plateau_radius_cells"]),
    ] + feature_rows(result["inventory"])


def feature_rows(inv):
    """The large features as mapfeatures finds them, for the same table."""
    high = inv["high_ground"]
    canyon = max(inv["canyons"], key=lambda c: c["length"], default=None)
    mesa = max(inv["mesas"], key=lambda x: x["area"], default=None)
    river = max([w for w in inv["water"] if w["kind"] == "river"], key=lambda w: w["length"], default=None)
    rivers = [w for w in inv["water"] if w["kind"] == "river"]
    others = [w for w in inv["water"] if w["kind"] != "river"]
    outside = [r for r in inv["roads"] if r.get("outside_segments")]
    return [
        ("high ground", "%+d, %d%%" % (high["dz_start"], 100 * high["share"]) if high else "-"),
        ("canyon", "w%.0f l%.0f wind %.0f%s" % (canyon["width"], canyon["length"], canyon["winding"],
                                                " edge" if canyon["reaches_edge"] else "") if canyon else "-"),
        ("mesa", "r%.0f %+d, %d exits" % (mesa["radius"], mesa["top_dz_middle"], mesa["exits"]) if mesa else "-"),
        ("river", "w%.0f l%.0f, %d deep" % (river["width"], river["length"], river["depth_bytes"]) if river else "-"),
        ("rivers", "%d, %d branch" % (len(rivers), sum(1 for w in rivers if w["joins"] is not None))
         if rivers else "-"),
        ("lake/sea", " ".join("%s %d%% z%d" % (w["kind"], round(100 * w["share"]), w["surface_bytes"]) for w in others)
         or "-"),
        ("roads out", "%d seg %d long: %s" % (sum(r["outside_segments"] for r in outside),
                                              sum(r["outside_length"] for r in outside),
                                              " ".join("%s %d" % (r["template"], r["outside_segments"]) for r in outside))
         if outside else "-"),
        ("bridges", "%d %s" % (len(inv["bridges"]), " ".join(sorted(set(b["template"] for b in inv["bridges"]))))
         if inv["bridges"] else "-"),
        ("towns", " ".join(str(t["buildings"]) for t in inv["towns"]) or "-"),
        ("tech", " ".join(sorted(t["template"][4:] for t in inv["tech"])) or "-"),
    ]


def table(profiles):
    rows = [table_row(p, profiles[0][1]) for _, p in profiles]
    names = [name for name, _ in profiles]
    print("| measure | " + " | ".join(names) + " |")
    print("|---" * (len(names) + 1) + "|")
    for k in range(len(rows[0])):
        print("| %s | %s |" % (rows[0][k][0], " | ".join(r[k][1] for r in rows)))


#-----------------------------------------------------------------------------------------------
# The wave function collapse probe
#-----------------------------------------------------------------------------------------------

def band_of(t):
    return np.clip(np.floor(t * 2.0 + 0.5) + 1, 0, 5).astype(np.int32)


def wfc_probe(skeleton_path, src, crop=48, tries=5):
    """Overlapping WFC, 3x3, over the exemplar's height band and texture group, constrained to the
    skeleton's band: how many patterns, and whether a crop solves."""
    b = src.m.border
    play = (slice(b, b + src.m.playable_y), slice(b, b + src.m.playable_x))
    band = band_of(src.t)[play]
    group_of = np.array([0 if n.startswith(("Cliff", "Rocks")) else 1 if n.startswith("Grass")
                         else 2 if n.startswith("Dirt") else 3 for n in src.class_names])
    groups = group_of[src.cls][play]
    for label, symbols in (("band x 21 classes", band * 64 + src.cls[play]),
                           ("band x 4 groups", band * 4 + groups), ("band only", band)):
        windows = np.lib.stride_tricks.sliding_window_view(symbols, (3, 3)).reshape(-1, 9)
        patterns, counts = np.unique(windows, axis=0, return_counts=True)
        print("%-18s %6d distinct 3x3 patterns" % (label, len(patterns)))

    with open(skeleton_path, "rb") as handle:
        skel = Grid(handle.read())
    seen = set(map(bytes, np.lib.stride_tricks.sliding_window_view(band, (3, 3)).reshape(-1, 9)
                   .astype(np.uint8)))
    b = skel.m.border
    wanted = np.lib.stride_tricks.sliding_window_view(band_of(skel.t)[b:-b, b:-b], (3, 3)).reshape(-1, 9)
    unseen = np.mean([bytes(row) not in seen for row in wanted.astype(np.uint8)])
    print("skeleton cells whose 3x3 band pattern the exemplar never shows: %.1f%%" % (100 * unseen))

    windows = np.lib.stride_tricks.sliding_window_view(band * 4 + groups, (3, 3)).reshape(-1, 9)
    patterns, counts = np.unique(windows, axis=0, return_counts=True)
    p = patterns.reshape(-1, 3, 3)
    n = len(p)
    if n > 8000:
        print("%d patterns: the adjacency alone is %.0f MB, no solve tried" % (n, 4 * n * n / 1e6))
        return
    compat = {}
    for d, (dy, dx) in enumerate(((0, 1), (1, 0), (0, -1), (-1, 0))):
        a = p[:, max(dy, 0):3 + min(dy, 0), max(dx, 0):3 + min(dx, 0)].reshape(n, -1)
        c = p[:, max(-dy, 0):3 + min(-dy, 0), max(-dx, 0):3 + min(-dx, 0)].reshape(n, -1)
        compat[(dy, dx)] = (a[:, None, :] == c[None, :, :]).all(2)
    print("adjacency: %d x %d x 4 = %.0f MB as bytes" % (n, n, 4 * n * n / 1e6))

    cy, cx = int(skel.centre[0]), int(skel.starts[0][1] + skel.centre[1]) // 2
    goal = band_of(skel.t)[cy - crop // 2:cy + crop // 2, cx - crop // 2:cx + crop // 2]
    centre_band = p[:, 1, 1] // 4
    for attempt in range(tries):
        began = time.time()
        wave = centre_band[None, None, :] == goal[:, :, None]
        queue = deque((y, x) for y in range(crop) for x in range(crop))
        ok = propagate(wave, compat, queue)
        step = 0
        while ok:
            sizes = wave.sum(2)
            open_ = sizes > 1
            if not open_.any():
                break
            noise = np.array([[hash_unit(attempt, y * crop + x, step) for x in range(crop)]
                              for y in range(crop)])
            y, x = np.unravel_index(np.argmin(np.where(open_, sizes + noise, np.inf)), sizes.shape)
            allowed = np.nonzero(wave[y, x])[0]
            weights = counts[allowed].cumsum()
            pick = allowed[np.searchsorted(weights, hash_unit(attempt, step, 7) * weights[-1])]
            wave[y, x] = False
            wave[y, x, pick] = True
            ok = propagate(wave, compat, deque([(y, x)]))
            step += 1
        print("crop %dx%d try %d: %s after %d collapses, %.1f s" % (
            crop, crop, attempt, "solved" if ok else "contradiction", step, time.time() - began))


def propagate(wave, compat, queue):
    size = wave.shape[0]
    while queue:
        y, x = queue.popleft()
        here = wave[y, x]
        if not here.any():
            return False
        for (dy, dx), table_ in compat.items():
            ny, nx = y + dy, x + dx
            if 0 <= ny < size and 0 <= nx < size:
                support = table_[here].any(0)
                narrowed = wave[ny, nx] & support
                if (narrowed != wave[ny, nx]).any():
                    if not narrowed.any():
                        return False
                    wave[ny, nx] = narrowed
                    queue.append((ny, nx))
    return True


#-----------------------------------------------------------------------------------------------
# Exemplar size, for the port
#-----------------------------------------------------------------------------------------------

def exemplar_size(src):
    """What a port has to bake in, over the whole grid (the search reads the border too): the
    heights, the class a cell, each blend layer as its overlay class and shape, the scenery. The
    tiles and the cliff bits follow from those."""
    templates = sorted(set(o[0] for v in src.scenery.values() for o in v))
    scenery = b"".join(struct.pack("<BHHBBB", templates.index(t), x, y, int(fx * 255), int(fy * 255),
                                   int(angle % (2 * math.pi) / (2 * math.pi) * 255))
                       for (y, x), v in sorted(src.scenery.items()) for t, fx, fy, angle in v)
    parts = {
        "heights, byte a cell": src.h.astype(np.uint8).tobytes(),
        "texture class, byte a cell": src.cls.astype(np.uint8).tobytes(),
        "blend layers, class+shape, 2x2 bytes": b"".join(
            (src.blend_class[layer] * 32 + src.blend_shape[layer]).astype("<u2").tobytes()
            for layer in src.layers[1:3]),
        "scenery, 7 bytes each + names": scenery + "\0".join(templates).encode("latin-1"),
    }
    total = [0, 0]
    for name, blob in parts.items():
        packed = len(zlib.compress(blob, 9))
        total = [total[0] + len(blob), total[1] + packed]
        print("%-38s %8d raw %8d zlib" % (name, len(blob), packed))
    print("%-38s %8d raw %8d zlib" % ("exemplar", total[0], total[1]))


#-----------------------------------------------------------------------------------------------
# Self check and CLI
#-----------------------------------------------------------------------------------------------

def selfcheck():
    error = np.ones((6, 5))
    error[:, 3] = 0.0
    assert list(min_cut(error)) == [3] * 6
    error[2:, 3] = 1.0
    error[2:, 4] = 0.0
    assert list(min_cut(error)) == [3, 3, 4, 4, 4, 4], min_cut(error)
    assert np.allclose(box(np.full((7, 9), 5.0), 2), 5.0)
    assert positions(483)[-1] == 483 - PATCH and positions(483)[1] == PATCH - OVERLAP
    assert hash_cell(1, 2, 3) == hash_cell(1, 2, 3) != hash_cell(1, 2, 4)
    step = np.zeros((6, 6), np.int32)
    step[:, 3:] = 40
    assert span_cliff(step)[:5, 2].all() and not span_cliff(step)[:5, 0].any()
    assert simplify([(0, k) for k in range(10)], 1.0) == [(0, 0), (0, 9)]
    assert simplify([(0, 0), (0, 5), (5, 5)], 1.0) == [(0, 0), (0, 5), (5, 5)]
    assert len(subdivide((0, 0), (0, 25), 10.0)) == 3

    # what the synthesis leans on: the exemplar's cliff bits are its heights', and its tiles and
    # blend tiles are a function of class and cell
    if os.path.exists(SOURCE_BIG):
        src = load_exemplar(SOURCE_BIG, SOURCE_MAP)
        b = src.m.border
        play = (slice(b, b + src.m.playable_y), slice(b, b + src.m.playable_x))
        assert (src.cliff[play] == span_cliff(src.h)[play]).all()     # the border is not painted
        yy, xx = np.mgrid[0:src.h.shape[0], 0:src.h.shape[1]]
        assert (tile_for(src.cls, src.first, src.width, yy, xx) == src.layers[0]).all()
        tiles = np.array([tile for tile, _, _ in texture_table(src.tail)[2]])
        for layer in src.layers[1:3]:
            blended = layer > 0
            over = tile_for(src.blend_class[layer], src.first, src.width, yy, xx)
            assert (over[blended] == tiles[layer][blended]).all()
    print("synthmap selfcheck OK")


def main():
    if sys.argv[1:] == ["selfcheck"]:
        selfcheck()
        return 0
    if sys.argv[1:2] == ["table"]:
        # the first map is the exemplar, the one the texture distance is taken against
        profiles = []
        for path in sys.argv[2:]:
            with open(path, "rb") as handle:
                data = handle.read()
            result = mapstyle.profile(data, path)
            result["inventory"] = mapfeatures.inventory(data, path)
            profiles.append((os.path.splitext(os.path.basename(path))[0], result))
        table(profiles)
        return 0
    if sys.argv[1:2] == ["render"]:
        side_by_side(sys.argv[2], sys.argv[3:])
        return 0
    if sys.argv[1:2] == ["wfcprobe"]:
        src = load_exemplar(SOURCE_BIG, SOURCE_MAP)
        wfc_probe(sys.argv[2], src)
        exemplar_size(src)
        return 0

    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("skeletons", nargs="+", help="generated .map files (RMG_DUMP)")
    parser.add_argument("-o", "--out", default=".", help="directory for the .map and .png")
    parser.add_argument("--source", default=SOURCE_BIG, help="the .big holding the exemplar")
    parser.add_argument("--inner", default=SOURCE_MAP, help="the exemplar's path inside it")
    parser.add_argument("--seed", type=int, help="the jitter's seed (default: from the file name)")
    arguments = parser.parse_args()

    src = load_exemplar(arguments.source, arguments.inner)
    for path in arguments.skeletons:
        found = re.search(r"rmg_(\d+)_", os.path.basename(path))
        seed = arguments.seed if arguments.seed is not None else int(found.group(1)) if found else 0
        out = synthesise(path, src, arguments.out, seed)
        render(out).save(out[:-4] + ".png")
    return 0


if __name__ == "__main__":
    sys.exit(main())
