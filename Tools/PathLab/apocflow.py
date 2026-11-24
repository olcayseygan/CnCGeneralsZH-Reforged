"""
Apocalypse: one A* request per zombie against a shared flow field, on a basin like the generator's.

The question (docs/plans/TODO.md, Apocalypse > Performance): a night puts up to 700 of the dead on
the map at 10 a frame, each with its own aiAttackMoveToPosition to Apocalypse_Centre
(ApocalypseDirector::makeZombie) and, once idle, its own path to the nearest enemy every two game
seconds (ApocalypseDirector::sendOn).  The path queue answers 3 to 6 of them a frame on 5000 to 20000
cells (Pathfinder::processPathfindQueue, Pathfinder_queueCellBudget), so a wave waits for its paths.

Three ways the dead get their routes, everything else the same:

    today       the game: a queued search to the centre on spawn, a queued search to a structure the
                attack-move sees within vision on the way (scanned every 2 game s), a queued search
                to the nearest structure in a straight line (sendOn) whenever one stands idle
    astar       the same queue and search, but every request goes to the nearest structure in a
                straight line from the start, so the targets match the field's and only the queue
                differs
    field       one Dijkstra field per footprint class from every cell beside an enemy structure,
                integer costs as the search's (10/14, pinched +24, the wall hug off the clearance), a
                parent per cell; a request is answered the frame it is made by walking the parents
                and running Path::optimize over the cells, and a structure's death is repaired in
                place (the cells it was the source of, and the ground it freed, flooded again from
                their rim)

The basin, at the generator's numbers (RandomMapGenerator.cpp, RMG_APOC_*), cut to the 480-cell disc
the dead ever walk: a 7-cell escarpment at 128 cells from the middle with a 3-cell wobble, six
14-cell passes (one a seat and the two seams) each with a bending 14-cell canyon outside, wilderness
of 3 to 5-cell clumps to 192 cells with the trails kept clear, and the spawns 14 cells into the fog on
each pass's line, spread as ringSpot spreads them.  Four bases at 80 cells: a 6x6 command centre,
eight buildings, two 2x2 towers that each kill the nearest of the dead within 30 cells every wall
second.  Layout `walled` adds a wall arc across each lobe with a 2-cell gap, `sealed` also closes
seat 1's pass mouth.  Walkers, runners and brutes at their INI speeds, damage and footprints (the
brute's 5.5 radius is a 2x2 footprint in getRadiusAndCenter, the others one cell); they walk through
each other as infantry do.  Following is kinematic, on the optimized path: the question is the
routing, not the crowd model.

    python apocflow.py --mode today --layout open --seed 0
    python apocflow.py --all --seeds 0,1 --jobs 6
"""

import argparse
import heapq
import math
import random
import sys
import time
from dataclasses import dataclass, field

import pathlab as pl
from pathlab import CELL, CLEAR, COST_DIAGONAL, COST_ORTHOGONAL, IMPASSABLE, OBSTACLE, Grid

TICKS_GS = pl.LOGICFRAMES_PER_GAME_SECOND      # 20 ticks a game second at 150
TICKS_WS = pl.LOGICFRAMES_PER_SECOND           # 30 a wall second

# ---- the basin (RandomMapGenerator.cpp, RMG_APOC_*) ---------------------------------------------
SIZE = 480
MID = SIZE // 2
START_R = 80            # RMG_APOC_START_LEAST
WALL_R = 128            # START_LEAST + RMG_APOC_WALL_OUT
WALL_THICK = 7          # 2 + 3 + 2
WALL_WOBBLE = 3
GATE_HALF = 7           # RMG_APOC_GATE_HALF
CANYON = 14             # RMG_APOC_PASS_RAMP
WILD_R = WALL_R + WALL_THICK + 64 - 7    # RMG_APOC_WILDERNESS past the rim's outer foot
SPAWN_R = WILD_R + 14   # RMG_APOC_SPAWN_PAST
SEAT_BEARINGS = (45, 135, 225, 315)
GATE_BEARINGS = (45, 135, 225, 315, 0, 180)     # a pass a seat, two on the seams

# ---- the dead (ApocalypseReforged.ini, thePhaseRules' night row) ---------------------------------
@dataclass(frozen=True)
class Breed:
    name: str
    speed: float        # world units a game second
    radius: float
    vision: float       # cells
    dps: float          # damage a game second
    share: int          # percent of a night wave

BREEDS = (Breed("walker", 9.0, 4.0, 15.0, 40.0, 40),
          Breed("runner", 18.0, 4.0, 18.0, 22.0 / 0.8, 30),
          Breed("brute", 6.0, 5.5, 15.0, 220.0 / 1.5, 30))
NIGHT_CAP = 700
OUT_A_FRAME = 10
RETARGET = 2 * TICKS_GS             # APOCALYPSE_RETARGET_SECONDS
TOWER_RANGE = 30                    # cells
TOWER_EVERY = TICKS_WS              # a kill a wall second
STUCK_TICKS = 4 * TICKS_GS
RUN_TICKS = 5400                    # three wall minutes


def pl_kind(b: Breed) -> pl.UnitKind:
    speed = b.speed / TICKS_GS
    turn = math.radians(300.0) / TICKS_GS
    return pl.UnitKind(name=b.name, appearance=pl.TWO_LEGS, infantry=True, major_radius=b.radius,
                       radius=b.radius, speed=speed, speed_damaged=speed, turn_rate=turn,
                       turn_rate_damaged=turn, acceleration=1.0, acceleration_damaged=1.0, braking=1.0)


@dataclass
class Structure:
    sid: int
    kind: str
    cells: list[int]
    hp: float
    cx: float = 0.0
    cy: float = 0.0
    alive: bool = True
    died: int = -1


@dataclass
class Gate:
    bearing: float
    spawn: tuple[float, float]


def build_map(seed: int, layout: str) -> tuple[Grid, list[Gate], list[Structure]]:
    rng = random.Random(seed * 7919 + 1)
    g = Grid(SIZE, SIZE)
    kind = g.kind
    phase = rng.uniform(0, 2 * math.pi)
    gate_dirs = [(math.cos(math.radians(b)), math.sin(math.radians(b))) for b in GATE_BEARINGS]

    def gate_lateral(x: float, y: float) -> tuple[float, float] | None:
        """(distance out along the nearest pass's line, offset off it), its canyon bending."""
        best = None
        for ux, uy in gate_dirs:
            along = x * ux + y * uy
            if along <= 0:
                continue
            side = -x * uy + y * ux
            bend = max(0.0, along - (WALL_R + WALL_THICK)) * 0.25
            off = side - bend
            if best is None or abs(off) < abs(best[1]):
                best = (along, off)
        return best

    for cy in range(SIZE):
        for cx in range(SIZE):
            x, y = cx + 0.5 - MID, cy + 0.5 - MID
            r = math.hypot(x, y)
            wob = WALL_WOBBLE * math.sin(5 * math.atan2(y, x) + phase)
            inner = WALL_R + wob
            if inner <= r <= inner + WALL_THICK + 2 * CANYON:
                gl = gate_lateral(x, y)
                in_wall = r <= inner + WALL_THICK
                if in_wall and not (gl and abs(gl[1]) <= GATE_HALF):
                    kind[cy * SIZE + cx] = IMPASSABLE
                elif not in_wall and gl and r <= inner + WALL_THICK + CANYON and GATE_HALF < abs(gl[1]) <= GATE_HALF + 3:
                    kind[cy * SIZE + cx] = IMPASSABLE           # the canyon's sides
    # wilderness clumps, the trails kept clear
    placed = 0
    while placed < 260:
        a = rng.uniform(0, 2 * math.pi)
        r = rng.uniform(WALL_R + WALL_THICK + CANYON + 4, WILD_R + 16)
        x, y = r * math.cos(a), r * math.sin(a)
        gl = gate_lateral(x, y)
        if gl and abs(gl[1]) < GATE_HALF + 5:
            continue
        s = rng.randint(3, 5)
        bx, by = int(MID + x), int(MID + y)
        for j in range(by, by + s):
            for i in range(bx, bx + s):
                if 0 <= i < SIZE and 0 <= j < SIZE and kind[j * SIZE + i] == CLEAR:
                    kind[j * SIZE + i] = OBSTACLE
        placed += 1

    structures: list[Structure] = []

    def add(kind_name: str, cells: list[tuple[int, int]], hp: float) -> bool:
        idx = [j * SIZE + i for i, j in cells]
        if any(kind[k] != CLEAR for k in idx):
            return False
        for k in idx:
            kind[k] = OBSTACLE
        s = Structure(len(structures), kind_name, idx, hp)
        s.cx = sum(i for i, _ in cells) / len(cells) + 0.5
        s.cy = sum(j for _, j in cells) / len(cells) + 0.5
        structures.append(s)
        return True

    def box(x0: int, y0: int, w: int, h: int) -> list[tuple[int, int]]:
        return [(i, j) for j in range(y0, y0 + h) for i in range(x0, x0 + w)]

    def clear_around(cells: list[tuple[int, int]], pad: int) -> bool:
        return all(0 <= i + a < SIZE and 0 <= j + b < SIZE and kind[(j + b) * SIZE + i + a] == CLEAR
                   for i, j in cells for a in range(-pad, pad + 1, pad) for b in range(-pad, pad + 1, pad))

    for seat, bearing in enumerate(SEAT_BEARINGS):
        ux, uy = math.cos(math.radians(bearing)), math.sin(math.radians(bearing))
        sx, sy = MID + ux * START_R, MID + uy * START_R
        add("cc", box(int(sx) - 3, int(sy) - 3, 6, 6), 5000.0)
        n = tries = 0
        while n < 8 and tries < 400:
            tries += 1
            w, h = rng.randint(3, 5), rng.randint(3, 5)
            x0, y0 = int(sx + rng.uniform(-22, 22)), int(sy + rng.uniform(-22, 22))
            cells = box(x0, y0, w, h)
            if math.hypot(x0 - MID, y0 - MID) > WALL_R - 12 or not clear_around(cells, 2):
                continue
            n += add("building", cells, 2000.0)
        for d in (-10, 10):
            a = math.radians(bearing + d)
            add("tower", box(int(MID + 105 * math.cos(a)), int(MID + 105 * math.sin(a)), 2, 2), 1500.0)
        if layout in ("walled", "sealed"):
            # a wall arc across the lobe, 3-cell pieces, one 2-cell gap off its middle
            r = 112
            last = None
            for k in range(-140, 141):
                a = math.radians(bearing + k * 0.25)
                c = (int(MID + r * math.cos(a)), int(MID + r * math.sin(a)))
                if c == last:
                    continue
                last = c
                if 76 <= k <= 84:
                    continue                                    # the gap, 2 cells or so
                add("wall", [c], 600.0)
        if layout == "sealed" and seat == 0:
            # seat 1's pass mouth closed by a wall across it, inside the escarpment
            r = WALL_R - 3
            px, py = -uy, ux
            last = None
            for k in range(-2 * (GATE_HALF + 4), 2 * (GATE_HALF + 4) + 1):
                c = (int(MID + ux * r + px * k / 2), int(MID + uy * r + py * k / 2))
                if c != last:
                    last = c
                    add("wall", [c], 600.0)
                    if kind[c[1] * SIZE + c[0] + 1] == CLEAR:   # thick enough that no diagonal slips it
                        add("wall", [(c[0] + 1, c[1])], 600.0)
    gates = []
    for b, (ux, uy) in zip(GATE_BEARINGS, gate_dirs):
        bend = (SPAWN_R - WALL_R - WALL_THICK) * 0.25
        gates.append(Gate(b, (MID + ux * SPAWN_R - uy * bend, MID + uy * SPAWN_R + ux * bend)))
    g.set_kind(0, 0, kind[0])     # drop the caches
    return g, gates, structures


# ---- the field -----------------------------------------------------------------------------------
INF = 1 << 30
DELTAS = pl.DELTAS
ADJ = pl.ADJACENT


class FlowField:
    """Dijkstra from every passable cell beside a live structure, the search's step costs, a parent
       (the next cell towards the nearest source) and a label (which structure) per cell.  Integer
       costs, sources pushed in structure order and an insertion counter on ties: two machines build
       the same field."""

    def __init__(self, grid: Grid, radius: int) -> None:
        self.grid = grid
        self.need = pl.PF_CLEARANCE_ORTHO * radius + pl.PF_CLEARANCE_ORTHO // 2
        n = grid.width * grid.height
        self.dist = [INF] * n
        self.parent = [-1] * n
        self.label = [-1] * n

    def enter_cost(self, cell: int, step: int) -> int:
        """costSoFar's step and pinched charge, the expansion's pinched +10 and flowStepCost's wall
           hug, for the cell stepped onto (no turn and no traffic: the field has no heading)."""
        g = self.grid
        cost = step
        if g.pinched[cell]:
            cost += COST_DIAGONAL + COST_ORTHOGONAL
        clearance = g.clearance[cell]
        if clearance - self.need < pl.PF_CLEARANCE_SOFT:
            cost += pl.wall_hug_cost(step, clearance, self.need)
        return cost

    def sources(self, structures: list[Structure]) -> list[tuple[int, int]]:
        g, w = self.grid, self.grid.width
        out = []
        for s in structures:
            if not s.alive:
                continue
            for k in s.cells:
                x, y = k % w, k // w
                for dx, dy in DELTAS:
                    if g.passable(x + dx, y + dy):
                        out.append(((y + dy) * w + x + dx, s.sid))
        return out

    def _flood(self, heap: list, seq: int) -> int:
        g, w, h = self.grid, self.grid.width, self.grid.height
        dist, parent, label, kind = self.dist, self.parent, self.label, g.kind
        cells = 0
        while heap:
            d, _, u = heapq.heappop(heap)
            if d != dist[u]:
                continue
            cells += 1
            ux, uy = u % w, u // w
            open_ = [False] * 8
            for i, (dx, dy) in enumerate(DELTAS):
                if i >= 4 and not open_[ADJ[i - 4]] and not open_[ADJ[i - 3]]:
                    continue
                vx, vy = ux + dx, uy + dy
                if not (0 <= vx < w and 0 <= vy < h):
                    continue
                v = vy * w + vx
                if kind[v] != CLEAR:
                    continue
                open_[i] = True
                # the zombie steps v -> u, so it pays to enter u
                nd = d + self.enter_cost(u, COST_DIAGONAL if i >= 4 else COST_ORTHOGONAL)
                if nd < dist[v]:
                    dist[v], parent[v], label[v] = nd, u, label[u]
                    seq += 1
                    heapq.heappush(heap, (nd, seq, v))
        return cells

    def build(self, structures: list[Structure]) -> int:
        n = len(self.dist)
        self.dist, self.parent, self.label = [INF] * n, [-1] * n, [-1] * n
        heap, seq = [], 0
        for cell, sid in self.sources(structures):
            if self.dist[cell] == 0:
                continue
            self.dist[cell], self.label[cell] = 0, sid
            seq += 1
            heap.append((0, seq, cell))
        heapq.heapify(heap)
        return self._flood(heap, seq)

    def repair(self, dead: Structure, structures: list[Structure]) -> int:
        """The structure is gone from the grid already.  Every cell it was the source of, its own
           cells and the ring whose pinched flag or clearance it moved are cleared, then flooded again
           from the rim of that region and from the sources still standing inside it; a cell outside
           takes a cheaper value through the opened ground the same way."""
        g, w, h = self.grid, self.grid.width, self.grid.height
        dist, parent, label = self.dist, self.parent, self.label
        region = set(k for k in range(len(dist)) if label[k] == dead.sid)
        reach = (pl.PF_CLEARANCE_SOFT + self.need) // pl.PF_CLEARANCE_ORTHO + 2
        for k in dead.cells:
            x, y = k % w, k // w
            for j in range(max(0, y - reach), min(h, y + reach + 1)):
                for i in range(max(0, x - reach), min(w, x + reach + 1)):
                    region.add(j * w + i)
        for k in region:
            dist[k], parent[k], label[k] = INF, -1, -1
        heap, seq = [], 0
        for cell, sid in self.sources(structures):
            if cell in region and dist[cell] != 0:
                dist[cell], label[cell] = 0, sid
                seq += 1
                heap.append((0, seq, cell))
        # the rim: every settled cell beside the region relaxes into it on the flood's first pops
        for k in sorted(region):
            x, y = k % w, k // w
            for dx, dy in DELTAS:
                vx, vy = x + dx, y + dy
                if 0 <= vx < w and 0 <= vy < h:
                    v = vy * w + vx
                    if v not in region and dist[v] < INF:
                        seq += 1
                        heap.append((dist[v], seq, v))
        heapq.heapify(heap)
        return self._flood(heap, seq) + len(region)

    def route(self, cell: int) -> list[int]:
        out = [cell]
        while self.dist[out[-1]] > 0 and self.parent[out[-1]] >= 0:
            out.append(self.parent[out[-1]])
        return out


# ---- the simulation ------------------------------------------------------------------------------
@dataclass
class Zombie:
    zid: int
    breed: Breed
    x: float
    y: float
    unit: pl.Unit
    born: int
    points: list = field(default_factory=list)
    leg: int = 0
    state: str = "wait"           # wait (no path yet), move, attack, idle, dead
    target: int = -1              # structure id, -1 the centre
    first_move: int = -1
    reached: int = -1
    asked: int = -1               # frame of the request in the queue, -1 none
    ask_no: int = 0
    last_x: float = 0.0
    last_y: float = 0.0
    last_t: int = 0
    stuck: bool = False


@dataclass
class Request:
    zid: int
    ask_no: int
    goal: tuple[float, float]
    target: int
    remaining: int = -1
    path: object = None


class Night:
    def __init__(self, seed: int, layout: str, mode: str) -> None:
        self.mode = mode
        self.grid, self.gates, self.structures = build_map(seed, layout)
        self.pf = pl.Pathfinder(self.grid)
        self.rng = random.Random(seed * 104729 + 3)      # the spawns: the same in every mode
        self.zombies: list[Zombie] = []
        self.queue: list[Request] = []
        self.frame = 0
        self.centre = (MID * CELL + CELL / 2, MID * CELL + CELL / 2)
        self.kinds = [pl_kind(b) for b in BREEDS]
        self.sealed_gate = 0 if layout == "sealed" else -1
        # measures
        self.cells_frame: list[int] = []
        self.queue_peak = 0
        self.waits: list[int] = []
        self.searches = self.search_cells = 0
        self.routes = self.route_cells = 0
        self.failed = 0
        self.repairs: list[int] = []
        self.rebuild_cells = 0
        self.stack_samples: list[tuple[int, int]] = []
        self.sealed_wall_hits = 0
        self.sealed_detours = 0
        self.cpu_route = 0.0
        self.fields: list[FlowField] = []
        if mode == "field":
            t = time.perf_counter()
            self.fields = [FlowField(self.grid, 0), FlowField(self.grid, 1)]
            self.rebuild_cells = sum(f.build(self.structures) for f in self.fields)
            self.cpu_build = time.perf_counter() - t

    # -- the director's side --
    def spawn(self) -> None:
        rng = self.rng
        roll = rng.randrange(100)
        b = 0 if roll < BREEDS[0].share else 1 if roll < BREEDS[0].share + BREEDS[1].share else 2
        gi = rng.randrange(len(self.gates))
        gate = self.gates[gi]
        ux, uy = gate.spawn[0] - MID, gate.spawn[1] - MID
        reach = math.hypot(ux, uy)
        ux, uy = ux / reach, uy / reach
        pos = gate.spawn
        for _ in range(16):                                     # ringSpot
            inward = rng.uniform(-0.12 * reach, 0.06 * reach)
            aside = rng.uniform(-0.25 * reach, 0.25 * reach)
            x, y = gate.spawn[0] - ux * inward - uy * aside, gate.spawn[1] - uy * inward + ux * aside
            if math.hypot(x - MID, y - MID) > WILD_R + 2 and self.grid.passable(int(x), int(y)):
                pos = (x, y)
                break
        z = Zombie(len(self.zombies), BREEDS[b], pos[0] * CELL, pos[1] * CELL,
                   pl.Unit(pos[0] * CELL, pos[1] * CELL, id=len(self.zombies) + 1, kind=self.kinds[b]),
                   self.frame)
        z.last_x, z.last_y, z.last_t = z.x, z.y, self.frame
        z.gate = gi
        self.zombies.append(z)
        if self.mode == "today":
            self.ask(z, self.centre, -1)
        else:
            self.ask(z, *self.nearest(z))

    def nearest(self, z: Zombie) -> tuple[tuple[float, float], int]:
        """sendOn: the closest live structure from centre to centre, in a straight line."""
        best, sid = None, -1
        for s in self.structures:
            if s.alive:
                d = (s.cx * CELL - z.x) ** 2 + (s.cy * CELL - z.y) ** 2
                if best is None or d < best:
                    best, sid = d, s.sid
        if sid < 0:
            return self.centre, -1
        return self.beside(z, self.structures[sid]), sid

    def beside(self, z: Zombie, s: Structure) -> tuple[float, float]:
        """The cell within two of the structure nearest the zombie whose goal footprint is all ground
           (checkDestination refuses a brute's 2x2 that overlaps the structure): the attack's goal,
           as the point findPath's adjustCoordToCell lands on that cell."""
        w = self.grid.width
        r, centre = z.unit.cell_radius, z.unit.center
        above = r + 1 if centre else r
        best, at = None, None
        for k in s.cells:
            x, y = k % w, k // w
            for dx in range(-2, 3):
                for dy in range(-2, 3):
                    cx, cy = x + dx, y + dy
                    if not all(self.grid.passable(i, j) for i in range(cx - r, cx + above)
                               for j in range(cy - r, cy + above)):
                        continue
                    px, py = pl.cell_pos(cx, cy, True)
                    d = (max(abs(dx), abs(dy)), (px - z.x) ** 2 + (py - z.y) ** 2)
                    if best is None or d < best:
                        best, at = d, pl.cell_pos(cx, cy, centre)
        return at if at else (s.cx * CELL, s.cy * CELL)

    def ask(self, z: Zombie, goal: tuple[float, float], target: int) -> None:
        z.ask_no += 1
        z.target = target
        if self.mode == "field":
            t = time.perf_counter()
            f = self.fields[1 if z.breed.radius > 5 else 0]
            start = pl.cell_of(z.x, z.y)
            cells = f.route(start[1] * SIZE + start[0])
            self.cells_frame[-1] += len(cells)
            self.routes += 1
            self.route_cells += len(cells)
            if f.dist[cells[-1]] != 0:
                self.failed += 1
                z.state = "idle"
                return
            z.target = f.label[cells[-1]]
            nodes = [(z.x, z.y)] + [pl.cell_pos(c % SIZE, c // SIZE, True) for c in cells]
            route = self.pf.optimize(z.unit, nodes, False)
            self.cpu_route += time.perf_counter() - t
            self.deliver(z, [nodes[i] for i in route], 0)
            return
        z.asked = self.frame
        if z.state != "move":
            z.state = "wait"
        self.queue.append(Request(z.zid, z.ask_no, goal, target))

    def deliver(self, z: Zombie, points: list, waited: int) -> None:
        self.waits.append(waited)
        z.points, z.leg, z.state, z.asked = points, 1, "move", -1
        z.last_x, z.last_y, z.last_t = z.x, z.y, self.frame      # progress counts from the new path
        if z.first_move < 0:
            z.first_move = self.frame

    def serve_queue(self) -> None:
        """processPathfindQueue: 3 requests a frame, one more for every 20 waiting past 60, to 6; the
           cells 5000 a frame, 5000 more for every 20 past 60, to 20000; a search that runs out of
           cells holds the head of the queue and carries on next frame (ResumableSearch)."""
        depth = len(self.queue)
        self.queue_peak = max(self.queue_peak, depth)
        cap, budget = pl.PATHFIND_REQUESTS_PER_FRAME, pl.PATHFIND_CELLS_PER_FRAME
        if depth > pl.PATHFIND_DEEP_QUEUE:
            steps = (depth - pl.PATHFIND_DEEP_QUEUE) // pl.PATHFIND_DEEP_STEP
            cap += steps
            budget += pl.PATHFIND_CELLS_PER_FRAME * steps
        cap = min(cap, pl.PATHFIND_REQUESTS_PER_FRAME_DEEP)
        budget = min(budget, pl.PATHFIND_CELLS_PER_FRAME * pl.PATHFIND_DEEP_CELLS)
        spent = started = 0
        while self.queue and spent < budget:
            req = self.queue[0]
            z = self.zombies[req.zid]
            if req.ask_no != z.ask_no or z.state == "dead":
                self.queue.pop(0)                 # superseded or dead: dropped unsearched
                continue
            if req.remaining < 0:
                if started >= cap:
                    break
                started += 1
                t = time.perf_counter()
                before = self.pf.cells_total
                self.pf.frame = self.frame
                req.path = self.pf.find_path(z.unit, (z.x, z.y), req.goal)
                self.cpu_route += time.perf_counter() - t
                req.remaining = max(1, self.pf.cells_total - before)
                self.searches += 1
                self.search_cells += req.remaining
            take = min(req.remaining, budget - spent)
            spent += take
            req.remaining -= take
            if req.remaining > 0:
                break
            self.queue.pop(0)
            if req.path is None:
                self.failed += 1
                z.state, z.asked = "idle", -1
                continue
            self.deliver(z, req.path.points, self.frame - z.asked)
        self.cells_frame[-1] += spent

    def kill_structure(self, s: Structure) -> None:
        s.alive, s.died = False, self.frame
        for k in s.cells:
            self.grid.kind[k] = CLEAR
        self.grid.set_kind(0, 0, self.grid.kind[0])      # drop the pinched, clearance and zone caches
        if self.mode == "field":
            cells = sum(f.repair(s, self.structures) for f in self.fields)
            self.repairs.append(cells)
            self.cells_frame[-1] += cells

    # -- one tick --
    def step(self) -> None:
        self.cells_frame.append(0)
        if self.frame < NIGHT_CAP // OUT_A_FRAME:
            for _ in range(OUT_A_FRAME):
                self.spawn()
        if self.mode != "field":
            self.serve_queue()
        alive = [s for s in self.structures if s.alive]
        moving_cells: dict[int, int] = {}
        for z in self.zombies:
            if z.state == "dead":
                continue
            look = (self.frame + z.zid) % RETARGET == 0
            if z.state == "idle" and look:
                self.ask(z, *self.nearest(z))
            elif z.state == "move" and look and self.mode == "today" and z.target < 0:
                # the attack-move's scan: a structure within vision
                vr = (z.breed.vision * CELL) ** 2
                seen = [s for s in alive if (s.cx * CELL - z.x) ** 2 + (s.cy * CELL - z.y) ** 2 <= vr]
                if seen:
                    s = min(seen, key=lambda s: ((s.cx * CELL - z.x) ** 2 + (s.cy * CELL - z.y) ** 2, s.sid))
                    self.ask(z, self.beside(z, s), s.sid)
            if z.state == "attack":
                s = self.structures[z.target]
                if not s.alive:
                    z.state = "idle"
                    continue
                s.hp -= z.breed.dps / TICKS_GS
                if s.hp <= 0:
                    self.kill_structure(s)
                continue
            if z.state != "move":
                continue
            speed = z.breed.speed / TICKS_GS
            while speed > 0 and z.leg < len(z.points):
                tx, ty = z.points[z.leg]
                d = math.hypot(tx - z.x, ty - z.y)
                if d <= speed:
                    z.x, z.y, speed = tx, ty, speed - d
                    z.leg += 1
                else:
                    z.x += (tx - z.x) * speed / d
                    z.y += (ty - z.y) * speed / d
                    speed = 0
            z.unit.x, z.unit.y = z.x, z.y
            if self.sealed_gate >= 0 and getattr(z, "gate") == self.sealed_gate and z.reached < 0:
                r = math.hypot(z.x / CELL - MID, z.y / CELL - MID)
                if r < WALL_R - 8 and not getattr(z, "counted", False):
                    z.counted = True
                    self.sealed_detours += 1
            if z.leg >= len(z.points):
                s = self.structures[z.target] if z.target >= 0 else None
                if s is not None and s.alive and self.touches(z, s):
                    z.state = "attack"
                    if z.reached < 0:
                        z.reached = self.frame
                        if self.sealed_gate >= 0 and z.gate == self.sealed_gate and s.kind == "wall" \
                                and math.hypot(s.cx - MID, s.cy - MID) > WALL_R - 6:
                            self.sealed_wall_hits += 1
                else:
                    z.state = "idle"
                continue
            if math.hypot(z.x - z.last_x, z.y - z.last_y) > CELL:
                z.last_x, z.last_y, z.last_t = z.x, z.y, self.frame
            elif self.frame - z.last_t > STUCK_TICKS:
                z.stuck = True
            c = int(z.y // CELL) * SIZE + int(z.x // CELL)
            moving_cells[c] = moving_cells.get(c, 0) + 1
        if self.frame % 10 == 0 and moving_cells:
            self.stack_samples.append((max(moving_cells.values()),
                                       sum(n for n in moving_cells.values() if n >= 4)))
        # the towers: each kills the nearest of the dead within range every wall second
        for s in alive:
            if s.kind == "tower" and s.alive and (self.frame + s.sid) % TOWER_EVERY == 0:
                rr = (TOWER_RANGE * CELL) ** 2
                best = None
                for z in self.zombies:
                    if z.state != "dead":
                        d = (z.x - s.cx * CELL) ** 2 + (z.y - s.cy * CELL) ** 2
                        if d <= rr and (best is None or d < best[0]):
                            best = (d, z)
                if best:
                    best[1].state = "dead"
        self.frame += 1

    def touches(self, z: Zombie, s: Structure) -> bool:
        w = self.grid.width
        return any(abs((k % w + 0.5) * CELL - z.x) <= 2.6 * CELL and abs((k // w + 0.5) * CELL - z.y) <= 2.6 * CELL
                   for k in s.cells)

    def report(self) -> dict:
        zs = self.zombies
        firsts = sorted(z.first_move - z.born for z in zs if z.first_move >= 0)
        reach = sorted(z.reached - z.born for z in zs if z.reached >= 0)
        cf = self.cells_frame

        def pct(v: list, p: float) -> float:
            return v[min(len(v) - 1, int(p * len(v)))] if v else -1

        def mean(v: list) -> float:
            return sum(v) / len(v) if v else -1
        busy = [c for c in cf[:700] if c]
        return {
            "zombies": len(zs), "never_moved": sum(1 for z in zs if z.first_move < 0),
            "first_mean_s": mean(firsts) / TICKS_WS, "first_p95_s": pct(firsts, 0.95) / TICKS_WS,
            "first_max_s": (firsts[-1] if firsts else -1) / TICKS_WS,
            "reached": len(reach), "reach_mean_s": mean(reach) / TICKS_WS,
            "reach_p50_s": pct(reach, 0.5) / TICKS_WS, "reach_p90_s": pct(reach, 0.9) / TICKS_WS,
            "searches": self.searches, "failed": self.failed,
            "cells_per_search": self.search_cells / self.searches if self.searches else -1.0,
            "route_cells": self.route_cells / self.routes if self.routes else -1.0, "queue_peak": self.queue_peak,
            "wait_mean_s": mean(self.waits) / TICKS_WS, "wait_max_s": max(self.waits, default=0) / TICKS_WS,
            "cells_total": sum(cf), "cells_frame_mean": sum(cf) / len(cf),
            "cells_frame_mean_first700": mean(cf[:700]), "cells_frame_max": max(cf),
            "frames_over_5000": sum(1 for c in cf if c > 5000), "busy_frames_first700": len(busy),
            "rebuild_cells": self.rebuild_cells, "repairs": len(self.repairs),
            "repair_mean": mean(self.repairs), "repair_max": max(self.repairs, default=0),
            "structures_lost": sum(1 for s in self.structures if not s.alive),
            "structures": len(self.structures),
            "stuck": sum(1 for z in zs if z.stuck),
            "idle_end": sum(1 for z in zs if z.state == "idle"),
            "stack_max": max((a for a, _ in self.stack_samples), default=0),
            "stacked_mean": mean([b for _, b in self.stack_samples]),
            "sealed_wall_hits": self.sealed_wall_hits, "sealed_inside": self.sealed_detours,
            "sealed_spawned": sum(1 for z in zs if getattr(z, "gate", -1) == self.sealed_gate),
            "cpu_route_s": self.cpu_route,
        }


def run(seed: int, layout: str, mode: str) -> dict:
    night = Night(seed, layout, mode)
    while night.frame < RUN_TICKS:
        night.step()
    out = night.report()
    out.update(seed=seed, layout=layout, mode=mode)
    return out


COLUMNS = ("mode", "layout", "seed", "first_mean_s", "first_p95_s", "first_max_s", "reached", "reach_p50_s",
           "reach_p90_s", "searches", "cells_per_search", "route_cells", "failed", "queue_peak", "cells_frame_mean_first700", "cells_frame_max",
           "frames_over_5000", "rebuild_cells", "repairs", "repair_mean", "repair_max", "structures_lost",
           "stuck", "idle_end", "stack_max", "stacked_mean", "sealed_spawned", "sealed_wall_hits",
           "sealed_inside", "cpu_route_s")


def _job(args: tuple) -> dict:
    return run(*args)


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--mode", default="today", choices=("today", "astar", "field"))
    ap.add_argument("--layout", default="open", choices=("open", "walled", "sealed"))
    ap.add_argument("--seed", type=int, default=0)
    ap.add_argument("--all", action="store_true", help="every mode and layout")
    ap.add_argument("--seeds", default="0")
    ap.add_argument("--jobs", type=int, default=1)
    a = ap.parse_args()
    if a.all:
        jobs = [(s, lay, m) for s in map(int, a.seeds.split(",")) for lay in ("open", "walled", "sealed")
                for m in ("today", "astar", "field")]
    else:
        jobs = [(a.seed, a.layout, a.mode)]
    print("\t".join(COLUMNS))
    if a.jobs > 1:
        import multiprocessing
        with multiprocessing.Pool(a.jobs) as pool:
            rows = pool.imap(_job, jobs)
            for r in rows:
                print("\t".join(f"{r[c]:.2f}" if isinstance(r[c], float) else str(r[c]) for c in COLUMNS), flush=True)
    else:
        for j in jobs:
            r = run(*j)
            print("\t".join(f"{r[c]:.2f}" if isinstance(r[c], float) else str(r[c]) for c in COLUMNS), flush=True)


if __name__ == "__main__":
    sys.exit(main())
