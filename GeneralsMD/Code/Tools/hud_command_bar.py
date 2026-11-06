"""The in-game command bar and its button sheet, drawn from nothing: shapes, a height field and a light.

The bar is one 2048x512 texture whose left 1920 columns are the bar at 1920x1080, one texel to a screen
pixel, its top row at screen y 568 (the ImagePart of ControlBarProReforged.ini, mapped by
MappedImages/HandCreated/ReforgedHud<Side>CommandBar.ini).  ControlBarSchemeManager::drawPainting cuts
it at design x 182 and 616, texel 437 and 1478, and draws each piece where its panel stands, so nothing
may be painted near those two columns.  Every opening the bar's windows draw into (the fourteen
command cells, money, power rail, selection window, rank bar, the toolbar,
the idle column and the two tabs above the right panel) is read from the same numbers ControlBar.wnd
and the scheme use, below in layout(); the art only frames them.  The left panel is the radar's own
box round LeftHUD's square, where the radar stands and letterboxes the map (the side schemes name no
RadarFrameImage).  The plates reach past ControlBar.wnd's input blockers, so the scheme names its own
(Block<Name>UL/LR), each the bounding box of its plate; check() holds every scheme of the side to them.

The button sheet is ReforgedHud<Side>Powers, 1024x1024: the general's powers window, the toolbar, idle
and tab buttons in their four states, the rank bar's frame, the shortcut tray, the radar corner's hover
bracket, the selection window's emblem and the promotion banner, and the pieces the code lays out
itself (the radar's frame, the powers tray and the pieces the queue's and superweapons' trays take
mirrored or flipped, the readout plate, the build card's and the pointer tooltip's frames and the card's stat
icons), each where MappedImages/HandCreated/ReforgedHud<Side>Powers.ini maps it.  The tooltips' console set,
for the shell and any scheme without its own, is ReforgedHudHelpbox (written with the observer's sheets).

Nothing here is traced from another painting.  Each side is built its own way (BUILDS): its own
silhouette, how its plates stand on each other, the profile of their edges, the corners of every
window and button, the surface of the metal and what is fixed to it.  America is machined steel cut
at 45 degrees with the emblem's chevrons; China is cast armour in square terraces held by rivets; the
GLA is scrap sheet welded and bolted over a ragged frame; Turkiye is composite armour of triangular
facets with 30 degree cuts.  The emblem in every side's selection window is the launcher's
(launcher/ui/emblem.svg beside this checkout, or --emblem).  The observer has a build of its own, the
desk (round 18's three tiers in plain graphite, grilles, screws, the emblem's feathers and a tally lamp):
its bar, the watched player's panel, a sheet of pieces (toolbar buttons, the rows' rings, the radar
frame's nine, every side's and general's emblem) and the spectator's rack (ReforgedHudObsSpec).  Each
side and the observer also have a painting for the Tab screen (ReforgedHud<Side>TabPanel).

    python Tools/hud_command_bar.py --out DIR [--side all|Observer|Lobby|America|China|Gla|Turkiye] [--dds TEXTURES]
        writes DIR/ReforgedHud<Side>CommandBar.png, ...Powers.png and ...TabPanel.png for each side,
        the observer's ReforgedHudObsCommandBar, ...ObsPlayerWindow, ...ObsPieces, ...ObsSpec, ...ObsTabPanel and ReforgedHudHelpbox, and the
        front end's ReforgedFactionEmblems (Lobby), and with --dds the same as DXT5 .dds into TEXTURES
        (Data/Art/Textures)
"""
import argparse
import os
import re

import numpy as np
from PIL import Image, ImageDraw, ImageFilter
from scipy import ndimage

SS = int(os.environ.get("HUD_SS", "3"))     # supersampling; drawn at SS times and box filtered down
HERE = os.path.dirname(os.path.abspath(__file__))
SCHEME_INI = os.path.join(HERE, "..", "Data", "INI", "ControlBarProReforged.ini")
EMBLEM_SVG = os.path.join(HERE, "..", "..", "..", "..", "launcher", "ui", "emblem.svg")

BLUE, RED, GOLD = (0x44, 0x59, 0xb6), (0xb8, 0x26, 0x1f), (0xf2, 0xc2, 0x30)
INK, PAPER, TEXT_BLUE = (0x0c, 0x12, 0x20), (0xe8, 0xea, 0xf0), (0x80, 0x95, 0xea)
RUST = (112, 62, 34)

#   build    how the side's plates are made (BUILDS)
#   scheme   the ControlBarProReforged.ini scheme whose Block keys check() holds the plates to
# No side carries a flag, a national emblem or a national colour pair: the two accents are the shield's
# split for America and each side's team colours otherwise (Turkiye: its team red and dark steel, never
# red with white).
SIDES = {
    "America": dict(build="machined", steel=(132, 142, 156), glass=(8, 12, 19), glass_lo=(14, 22, 33),
                    accent=BLUE, accent2=RED, scheme="America8x6"),
    "China": dict(build="terraced", steel=(128, 120, 116), glass=(14, 9, 9), glass_lo=(30, 16, 15),
                  accent=(0xb8, 0x26, 0x1f), accent2=(0xd0, 0x8a, 0x22), scheme="China8x6"),
    "Gla": dict(build="scavenged", steel=(140, 130, 108), glass=(14, 12, 8), glass_lo=(30, 25, 16),
                accent=(0xb0, 0x5a, 0x1e), accent2=(0x86, 0x84, 0x3c), scheme="GLA8x6"),
    "Turkiye": dict(build="faceted", steel=(118, 126, 124), glass=(10, 12, 14), glass_lo=(22, 25, 28),
                    accent=(0xc0, 0x2a, 0x2a), accent2=(0x50, 0x58, 0x62), scheme="Turkiye8x6"),
}


def terraces(*steps):
    """An edge profile of square steps: (inset, share of the height) each, from the outside in."""
    def profile(d, scale):
        return sum(share * np.clip(d - inset * scale, 0, 1) for inset, share in steps)
    return profile


#   metric   how far from the edge is measured: round (edt) or square (chessboard, for terraces)
#   bevel    width of a straight chamfer in texels, when there is no stepped profile
#   corner   the corner every window, button and plate of the side's sheet takes (corner_points)
BUILDS = {
    "machined": dict(metric="edt", bevel=3.0, corner="cut"),
    "terraced": dict(metric="chessboard", profile=terraces((0, 0.4), (6, 0.3), (14, 0.3)), corner="notch"),
    "scavenged": dict(metric="edt", bevel=1.2, corner="rough"),
    "faceted": dict(metric="edt", bevel=7.0, corner="slant"),
}


def rounded(r):
    """An edge profile like a bullnose: a quarter circle r texels wide, steep at the edge and flat on top."""
    def profile(d, scale):
        return np.sqrt(np.clip(1 - (1 - np.clip(d / (r * scale), 0, 1)) ** 2, 0, 1))
    return profile


# The observer's console (round 9 prototype, --observer): no side's build, a broadcast desk in the same
# steel.  Every corner a radius, every edge a bullnose, anodised graphite with perforated grilles, cross
# head screws, the emblem's wing feathers and a red tally lamp.  Not in SIDES, so nothing ships with it.
BUILDS["console"] = dict(metric="edt", profile=rounded(5.0), corner="round")
OBSERVER = dict(build="console", steel=(112, 118, 128), glass=(6, 9, 15), glass_lo=(12, 16, 24),
                accent=BLUE, accent2=RED, scheme="Observer8x6")

# materials
STEEL, GLASS, ACCENT, ACCENT2, GOLDM, INKM, BRIGHT, DARK, PAPERM, DIM, HILITE = range(11)
METAL = (STEEL, DARK, BRIGHT)


def layout():
    """The bar's windows in texels (x0, y0, x1, y1); screen y is texel y + 568 at 1920x1080."""
    return dict(
        radar=(5, 264, 327, 496),               # RadarUL/LR, LeftHUD, sunk as the radar box's glass
        uattack=(276, 251, 340, 315),           # the under-attack lamp over the glass's top right corner
        money=(924, 272, 1051, 300),            # MoneyUL/LR, in the money tab right of the painted sign
        power=(646, 320, 1304, 330),            # PowerBarUL/LR, in the rail's channel
        tools=[(579, y, 633, y + 35) for y in (354, 400, 468)],                 # options, beacon, chat
        # the idle column at the grid's right end: worker, infantry, vehicle, aircraft (Idle<Kind>UL/LR)
        idle=[(1312, y, 1366, y + 35) for y in (357, 394, 431, 468)],
        cells=[(668 + 92 * c, y, 668 + 92 * c + 88, y + 70) for c in range(7) for y in (358, 432)],
        minmax=(1829, 234, 1920, 265),
        general=(1829, 272, 1920, 303),
        portrait=(1642, 319, 1871, 503),        # RightHUD
        exp=(1875, 319, 1911, 503),             # ExpBarForeground
    )


SEAMS = (437, 1478)
TOP = 568


# ---------------------------------------------------------------------------------------------- shapes

def corner_points(kind, c):
    """The top left corner from the left edge to the top edge, as offsets from the corner."""
    if c <= 0:
        return [(0, 0)]
    if kind == "cut":
        return [(0, c), (c, 0)]
    if kind == "notch":
        return [(0, c), (c, c), (c, 0)]
    if kind == "slant":                         # a 30 degree cut, long along the top
        return [(0, c), (c * 1.73, 0)]
    if kind == "round":                         # a radius, the observer's console (prototype)
        return [(c - c * np.cos(a), c - c * np.sin(a)) for a in np.linspace(0, np.pi / 2, 8)]
    return [(0, 0)]


def plate(x0, y0, x1, y1, tl=("none", 0), tr=("none", 0), br=("none", 0), bl=("none", 0)):
    pts = [(x0 + dx, y0 + dy) for dx, dy in corner_points(*tl)]
    pts += [(x1 - dx, y0 + dy) for dx, dy in reversed(corner_points(*tr))]
    pts += [(x1 - dx, y1 - dy) for dx, dy in corner_points(*br)]
    pts += [(x0 + dx, y1 - dy) for dx, dy in reversed(corner_points(*bl))]
    return pts


def octagon(x0, y0, x1, y1, c):
    return plate(x0, y0, x1, y1, ("cut", c), ("cut", c), ("cut", c), ("cut", c))


def shape(build, x0, y0, x1, y1, c):
    """A rectangle with the side's corners: 45 degree cuts, square notches, a 30 degree parallelogram, or
    for the GLA whatever each corner happened to be cut with (fixed by the rectangle's place)."""
    kind = BUILDS[build]["corner"]
    if kind == "rough":
        rng = np.random.default_rng(int(x0 * 7 + y0 * 13 + x1))
        picks = [("cut", c * rng.uniform(0.5, 1.4)), ("notch", c * rng.uniform(0.3, 0.8)), ("none", 0)]
        return plate(x0, y0, x1, y1, *[picks[i] for i in rng.choice(3, 4, p=(0.5, 0.25, 0.25))])
    if kind == "slant":
        return plate(x0, y0, x1, y1, ("slant", c), ("cut", c * 0.5), ("slant", c), ("cut", c * 0.5))
    return plate(x0, y0, x1, y1, (kind, c), (kind, c), (kind, c), (kind, c))


def grow(r, d):
    return (r[0] - d, r[1] - d, r[2] + d, r[3] + d)


def jitter(poly, seed, amp=2.5, step=12):
    """A ragged copy of a polygon: each edge broken every step texels and bitten in by up to amp.  Edges
    on the screen's border (x 0, x 1920, y 512) stay straight, and nothing moves outward."""
    rng = np.random.default_rng(seed)
    area = sum(x0 * y1 - x1 * y0 for (x0, y0), (x1, y1) in zip(poly, poly[1:] + poly[:1]))
    out = []
    for a, b in zip(poly, poly[1:] + poly[:1]):
        out.append(a)
        on_border = any(a[i] == b[i] == v for i, v in ((0, 0), (0, 1920), (1, 512)))
        length = np.hypot(b[0] - a[0], b[1] - a[1])
        if on_border or length < step:
            continue
        dx, dy = (b[0] - a[0]) / length, (b[1] - a[1]) / length
        nx, ny = (-dy, dx) if area > 0 else (dy, -dx)
        n = int(length // step)
        for k in range(1, n):
            t, bite = k / n, rng.uniform(0, amp)
            out.append((a[0] + (b[0] - a[0]) * t + nx * bite, a[1] + (b[1] - a[1]) * t + ny * bite))
    return out


def chevrons(cx, top, w, n=2, up=True):
    """The emblem's stacked chevrons: 19.82 wide, 8.11 rise, 4.96 thick, 10.82 apart."""
    k = w / 19.82
    rise, t, gap = 8.11 * k, 4.96 * k, 10.82 * k
    span = (n - 1) * gap + rise + t
    out = []
    for i in range(n):
        y = top + i * gap
        p = [(cx - w / 2, y + rise), (cx, y), (cx + w / 2, y + rise), (cx + w / 2, y + rise + t), (cx, y + t), (cx - w / 2, y + rise + t)]
        if not up:
            p = [(x, 2 * top + span - yy) for x, yy in p]
        out.append(p)
    return out


def triangle(cx, cy, w, h, pointing):
    """An isosceles triangle centred on (cx, cy), its point toward 'right', 'left', 'up' or 'down'."""
    pts = {"right": [(-w / 2, -h / 2), (w / 2, 0), (-w / 2, h / 2)], "left": [(w / 2, -h / 2), (-w / 2, 0), (w / 2, h / 2)],
           "up": [(-w / 2, h / 2), (0, -h / 2), (w / 2, h / 2)], "down": [(-w / 2, -h / 2), (w / 2, -h / 2), (0, h / 2)]}[pointing]
    return [(cx + x, cy + y) for x, y in pts]


def regular(cx, cy, r, n, phase=0.0):
    return [(cx + r * np.cos(phase + 2 * np.pi * i / n), cy + r * np.sin(phase + 2 * np.pi * i / n)) for i in range(n)]


def rotated(poly, cx, cy, deg):
    a = np.radians(deg)
    c, s = np.cos(a), np.sin(a)
    return [(cx + (x - cx) * c - (y - cy) * s, cy + (x - cx) * s + (y - cy) * c) for x, y in poly]


def sheet(x0, y0, x1, y1, deg):
    """A rectangle turned deg about its middle: a scrap sheet laid on not quite square."""
    return rotated(plate(x0, y0, x1, y1), (x0 + x1) / 2, (y0 + y1) / 2, deg)


# ---------------------------------------------------------------------------------------------- drawing

class Painter:
    """A height field, a coverage mask, a material and a tint per supersampled texel."""

    def __init__(self, w, h):
        self.w, self.h_ = w, h
        self.h = np.zeros((h * SS, w * SS), np.float32)
        self.alpha = np.zeros(self.h.shape, bool)
        self.limit = np.zeros(self.h.shape, bool)   # the silhouettes; nothing is opaque outside them
        self.mat = np.zeros(self.h.shape, np.uint8)
        self.tint = np.ones(self.h.shape, np.float32)

    def local(self, polys, pad=4):
        """The polygons drawn into the smallest window that holds them, and that window's slices."""
        xs = [x for p in polys for x, _ in p]
        ys = [y for p in polys for _, y in p]
        x0 = max(int(min(xs) - pad) * SS, 0)
        y0 = max(int(min(ys) - pad) * SS, 0)
        x1 = min(int(max(xs) + pad + 1) * SS, self.h.shape[1])
        y1 = min(int(max(ys) + pad + 1) * SS, self.h.shape[0])
        im = Image.new("L", (x1 - x0, y1 - y0), 0)
        d = ImageDraw.Draw(im)
        for p in polys:
            d.polygon([(x * SS - x0, y * SS - y0) for x, y in p], fill=255)
        m = np.asarray(im) > 0
        return (slice(y0, y1), slice(x0, x1)), m

    @staticmethod
    def inside(m, metric="edt", open_sides=(False, False, False, False)):
        """Distance in texels from each set texel to the nearest unset one; an open side (top, bottom,
        left, right) is not an edge, as the screen's border is not one."""
        pad = ((0 if open_sides[0] else 1, 0 if open_sides[1] else 1), (0 if open_sides[2] else 1, 0 if open_sides[3] else 1))
        pm = np.pad(m, pad)
        if metric == "chessboard":
            d = ndimage.distance_transform_cdt(pm, metric="chessboard").astype(np.float32)
        else:
            d = ndimage.distance_transform_edt(pm)
        return d[pad[0][0]:pad[0][0] + m.shape[0], pad[1][0]:pad[1][0] + m.shape[1]] / SS

    def raise_plate(self, polys, top, bevel, material=None, metric="edt", profile=None, scale=1.0, screen=False):
        sl, m = self.local(polys)
        open_sides = (False, sl[0].stop == self.h.shape[0], sl[1].start == 0, sl[1].stop == self.h.shape[1]) if screen else (False,) * 4
        d = self.inside(m, metric, open_sides)
        level = profile(d, scale) if profile else np.clip(d / bevel, 0, 1)
        self.h[sl] = np.maximum(self.h[sl], level * top * m)
        self.alpha[sl] |= m
        if material is not None:
            self.mat[sl][m] = material
        return sl, m, d

    def frame(self, polys, top, build, material=STEEL, scale=1.0, screen=False):
        """A plate with the side's edge: its chamfer, its terraces or its thin sheet edge."""
        b = BUILDS[build]
        return self.raise_plate(polys, top, b.get("bevel", 1.0) * scale, material, b["metric"], b.get("profile"), scale, screen)

    def silhouette(self, polys, top, build, material=DARK, screen=False):
        """A panel's outline, the limit of everything opaque.  On the bar (screen) its sides along the
        screen's border take no edge: those run on to the texture's end and are cut back at x 1920."""
        if screen:
            polys = [[(2048 if x == 1920 else x, y) for x, y in poly] for poly in polys]
        sl, m, d = self.frame(polys, top, build, material, screen=screen)
        self.limit[sl] |= m
        if screen:
            self.limit[:, 1920 * SS:] = False
        return sl, m, d

    def open_sides(self, sl):
        """Which sides of a local window run off the canvas (top, bottom, left, right)."""
        H, W = self.h.shape
        return (sl[0].start == 0, sl[0].stop == H, sl[1].start == 0, sl[1].stop == W)

    def tiers(self, polys, open4=None, shell_top=4.0, lip_top=7.0, inset=5.0, lip_bevel=2.6, shell_bevel=4.0, lip_mat=None):
        """Round 18's first two tiers from one outline: the near-black shell, and the lip, the shell eroded
        by inset, bevelled up (in lip_mat when given, as a hovered bezel's bright steel).  open4 (top,
        bottom, left, right) are the sides that are the screen's edge: no rim and no bevel there, the frame
        runs off; None takes the canvas's own border for it."""
        sl, m = self.local(polys)
        d = self.inside(m, "edt", self.open_sides(sl) if open4 is None else open4)
        self.h[sl] = np.maximum(self.h[sl], np.clip(d / shell_bevel, 0, 1) * shell_top * m)
        self.alpha[sl] |= m
        self.limit[sl] |= m
        self.mat[sl][m] = STEEL
        self.tint[sl][m] = SHELL_TINT
        ml = d > inset
        self.h[sl] = np.where(ml, np.maximum(self.h[sl], np.clip((d - inset) / lip_bevel, 0, 1) * lip_top), self.h[sl])
        self.tint[sl][ml] = 1.0
        if lip_mat is not None:
            self.mat[sl][ml] = lip_mat
        return sl, m, d

    def recess(self, polys, open4=None, depth=3.0, edge=2.0):
        """The third tier: a recessed dark floor, its walls in shadow, open where it runs off the screen."""
        sl, m = self.local(polys)
        self.h[sl] -= np.clip(self.inside(m, "edt", self.open_sides(sl) if open4 is None else open4) / edge, 0, 1) * m * depth
        self.tint[sl][m] = FLOOR_TINT
        return sl, m

    def sink(self, polys, depth, lip, material=GLASS):
        sl, m = self.local(polys)
        r = np.clip(self.inside(m) / lip, 0, 1) * m
        self.h[sl] -= r * depth
        self.mat[sl][r > 0.999] = material
        return sl, m

    def bump(self, polys, height, bevel, material=None):
        sl, m = self.local(polys)
        self.h[sl] += np.clip(self.inside(m) / bevel, 0, 1) * m * height
        if material is not None:
            self.mat[sl][m] = material
        return sl, m

    def inlay(self, polys, material, depth=0.6):
        sl, m = self.local(polys)
        self.h[sl] -= np.clip(self.inside(m) / 0.7, 0, 1) * m * depth
        self.mat[sl][m] = material
        return sl, m

    def cut(self, polys):
        sl, m = self.local(polys)
        self.alpha[sl] &= ~m
        return sl, m

    def ramp(self, polys, left, right):
        """Tilt what is inside the polygons: left added at their left edge, right at their right."""
        sl, m = self.local(polys)
        xs = np.nonzero(m.any(axis=0))[0]
        t = np.clip((np.arange(m.shape[1]) - xs[0]) / max(xs[-1] - xs[0], 1), 0, 1)[None, :]
        self.h[sl] += (left + (right - left) * t) * m
        return sl, m

    def stud(self, cx, cy, r, height=1.8):
        self.bump([octagon(cx - r, cy - r, cx + r, cy + r, r * 0.42)], height, r * 0.6, BRIGHT)

    def rivet(self, cx, cy, r=2.4):
        self.bump([regular(cx, cy, r, 12)], 1.6, r * 0.9, BRIGHT)

    def bolt(self, cx, cy, r=2.8, seed=0):
        self.bump([regular(cx, cy, r, 6, seed * 0.7)], 1.8, r * 0.45, BRIGHT)

    def weld(self, a, b, seed, r=1.5):
        """A bead of overlapping blobs from a to b, each a little off the line."""
        rng = np.random.default_rng(seed)
        n = max(2, int(np.hypot(b[0] - a[0], b[1] - a[1]) / (r * 1.3)))
        beads = [regular(a[0] + (b[0] - a[0]) * t + rng.normal(0, 0.35), a[1] + (b[1] - a[1]) * t + rng.normal(0, 0.35),
                         r * rng.uniform(0.8, 1.2), 7) for t in np.linspace(0, 1, n)]
        self.bump(beads, 1.1, r * 0.9, DARK)

    def facets(self, size, amp, groove, seed):
        """Composite armour: every steel texel tilted with the triangle of a lattice it falls in, and a
        groove cut along the lattice's lines."""
        sel = (self.mat == STEEL) & self.alpha
        ys, xs = np.nonzero(sel)
        x, y = xs / SS, ys / SS
        H = size * 0.866
        j = y / H
        i = x / size - j * 0.5
        ci, cj = np.floor(i), np.floor(j)
        fi, fj = i - ci, j - cj
        upper = (fi + fj > 1).astype(np.int64)
        table = np.random.default_rng(seed).uniform(-1, 1, (4096, 2)).astype(np.float32)
        key = (ci.astype(np.int64) * 73856093 ^ cj.astype(np.int64) * 19349663 ^ upper * 83492791) % 4096
        tilt = table[key]
        self.h[ys, xs] += (tilt[:, 0] * (fi - 0.5) + tilt[:, 1] * (fj - 0.5)) * amp * size
        line = np.minimum(np.minimum(np.minimum(fi, 1 - fi), np.minimum(fj, 1 - fj)), np.abs(fi + fj - 1)) * H
        g = line < groove
        self.h[ys[g], xs[g]] -= 0.8
        self.mat[ys[g], xs[g]] = INKM


def shade(p, side, seams=(), shadow=True, seed=7, falloff=True):
    """falloff darkens the steel and the glass from top to bottom, as down a panel; a piece the game
    tiles (the tray's) goes without, or each copy would step against the next."""
    shape_ = p.h.shape
    build = side["build"]
    rng = np.random.default_rng(seed)
    hs = ndimage.gaussian_filter(p.h, 0.6 * SS)
    gy, gx = np.gradient(hs * SS)                      # heights are texels, steps are texels/SS
    n = np.dstack([-gx, -gy, np.ones_like(gx)])
    n /= np.linalg.norm(n, axis=2, keepdims=True)
    light = np.array([-0.5, -0.72, 0.8])
    light /= np.linalg.norm(light)
    half = light + np.array([0, 0, 1.0])
    half /= np.linalg.norm(half)
    ndl = np.clip(n @ light, 0, 1)
    ndh = np.clip(n @ half, 0, 1)

    def noise(sigma, block=1):
        f = ndimage.gaussian_filter(rng.standard_normal((-(-shape_[0] // block), -(-shape_[1] // block))).astype(np.float32), sigma)
        f /= f.std() + 1e-6
        return np.kron(f, np.ones((block, block), np.float32))[: shape_[0], : shape_[1]] if block > 1 else f

    m = p.mat
    metal = np.isin(m, METAL)
    mottle = noise(6, 8)
    fine = noise(0.5 * SS)
    spec_base, rough_base = 0.55, 36.0
    if build == "machined":                            # brushed: long horizontal streaks
        streak = ndimage.gaussian_filter(rng.standard_normal(shape_).astype(np.float32), (0.5 * SS, 40 * SS))
        streak /= streak.std() + 1e-6
        grain = 1.0 + 0.05 * streak + 0.06 * mottle + 0.025 * fine
    elif build == "terraced":                          # cast: a heavy mottle, coarse grain and pits
        grain = 1.0 + 0.10 * mottle + 0.05 * noise(1.2 * SS)
        grain[noise(0.7 * SS) > 2.7] *= 0.55
        spec_base, rough_base = 0.38, 22.0
    elif build == "scavenged":                         # mill scale and wear, no grain direction
        grain = 1.0 + 0.09 * mottle + 0.045 * fine + 0.04 * noise(3 * SS)
        spec_base, rough_base = 0.32, 18.0
    elif build in ("console", "desk"):                 # anodised: even, a faint grain, semi-gloss
        grain = 1.0 + 0.03 * mottle + 0.02 * fine
        spec_base, rough_base = 0.5, 30.0
    else:                                              # composite: matte and even
        grain = 1.0 + 0.035 * mottle + 0.03 * fine
        spec_base, rough_base = 0.25, 16.0
    ao = 1.0 - np.clip((ndimage.gaussian_filter(p.h, 4 * SS) - p.h) * 0.13, 0, 0.6)

    def col(c):
        return np.array(c, np.float32) / 255.0

    albedo = np.empty(shape_ + (3,), np.float32)
    albedo[:] = col(side["steel"])
    albedo *= (grain * np.where(metal, p.tint, 1.0))[..., None]
    up = np.clip(-n[..., 1] * 1.6, 0, 1)               # a sky above: top-facing steel a little lighter
    albedo *= (1.0 + 0.10 * up)[..., None]
    ygrad = np.linspace(1.08, 0.88, shape_[0], dtype=np.float32)[:, None] if falloff else np.full((shape_[0], 1), 0.98, np.float32)
    for mid, k in ((STEEL, 1.0), (DARK, 0.66), (BRIGHT, 1.3)):
        sel = m == mid
        albedo[sel] *= (k * np.broadcast_to(ygrad, shape_)[sel])[:, None]
    if build == "scavenged":                           # rust where the sheets are oldest
        rust = np.clip((noise(5, 4) - 0.7) * 0.7, 0, 0.65) * metal
        albedo = albedo * (1 - rust[..., None]) + col(RUST) * grain[..., None] * rust[..., None]

    yy = np.linspace(0, 1, shape_[0], dtype=np.float32)[:, None, None] * (1.0 if falloff else 0.0) + (0.0 if falloff else 0.5)
    glass =col(side["glass"]) * (1 - yy) + col(side["glass_lo"]) * yy
    glass = np.broadcast_to(glass, shape_ + (3,))
    xx = np.arange(shape_[1])[None, :]
    yi = np.arange(shape_[0])[:, None]
    hatch = (((xx + yi) // SS) % 6 == 0).astype(np.float32) * 0.012   # a faint 45 degree line in the glass

    spec_k = np.full(shape_, spec_base, np.float32)
    rough = np.full(shape_, rough_base, np.float32)
    flat = {ACCENT: side["accent"], ACCENT2: side["accent2"], GOLDM: GOLD, INKM: INK, PAPERM: PAPER,
            DIM: (78, 86, 100), HILITE: TEXT_BLUE}
    for mid, c in flat.items():
        sel = m == mid
        albedo[sel] = col(c) * grain[sel][:, None] ** 0.4
    spec_k[(m == ACCENT) | (m == ACCENT2) | (m == HILITE)] = 0.35
    spec_k[(m == GOLDM) | (m == BRIGHT)] = 0.9
    spec_k[(m == INKM) | (m == DIM)] = 0.15
    g = m == GLASS
    albedo[g] = glass[g] + hatch[g][:, None]
    spec_k[g] = 0.12
    rough[g] = 60

    lit = albedo * (0.16 + 1.25 * ndl ** 1.4)[..., None] * ao[..., None]
    # paper and enamel read as paint under the light, not as dark metal
    for mid in (PAPERM, HILITE, GOLDM):
        sel = m == mid
        lit[sel] = albedo[sel] * (0.62 + 0.5 * ndl[sel])[:, None]
    spec_col = np.where((m == GOLDM)[..., None], col(GOLD), np.float32(1.0))
    lit += (spec_k * ndh ** rough)[..., None] * spec_col * 0.75
    # worn crests: every convex edge catches a bright hairline, the lit ones most
    crest = np.clip(-ndimage.laplace(ndimage.gaussian_filter(p.h, 0.7 * SS)) * SS * SS * 1.2, 0, 1) * (m != GLASS)
    lit += (crest * (0.35 + 0.65 * ndl))[..., None] * 0.35
    if build == "scavenged":                           # scratches through the paint and the scale
        im = Image.new("L", (shape_[1], shape_[0]), 0)
        d = ImageDraw.Draw(im)
        for _ in range(shape_[0] * shape_[1] // (2500 * SS * SS)):
            x, y, a, length = rng.uniform(0, shape_[1]), rng.uniform(0, shape_[0]), rng.uniform(0, np.pi), rng.uniform(4, 22) * SS
            d.line([(x, y), (x + np.cos(a) * length, y + np.sin(a) * length)], fill=255, width=max(1, SS // 2))
        lit += (np.asarray(im, np.float32) / 255.0 * (m != GLASS) * 0.10)[..., None]

    # an ink keyline round the outside, so the plate parts from bright ground
    alpha_mask = p.alpha & p.limit if p.limit.any() else p.alpha
    edge = ndimage.distance_transform_edt(alpha_mask) / SS
    key = np.clip(1.6 - edge, 0, 1)[..., None]
    lit = lit * (1 - key) + col(INK) * 0.5 * key

    a = alpha_mask.astype(np.float32)
    rgb = np.clip(lit, 0, 1) * a[..., None]
    alpha = a
    if shadow:                                          # a short shadow cast onto the world
        out = ndimage.distance_transform_edt(~alpha_mask) / SS
        alpha = a + np.clip(1 - out / 7.0, 0, 1) ** 2 * 0.45 * (~alpha_mask)
    for x in seams:                                     # nothing may cross a cut
        alpha[:, max(0, (x - 3) * SS):(x + 3) * SS] = 0
        rgb[:, max(0, (x - 3) * SS):(x + 3) * SS] = 0
    return downsample(np.dstack([rgb, alpha]), p.w, p.h_)


def downsample(rgba_premul, w, h):
    small = rgba_premul.reshape(h, SS, w, SS, 4).mean(axis=(1, 3))
    a = small[..., 3:4]
    small[..., :3] = np.where(a > 0, small[..., :3] / np.maximum(a, 1e-6), 0)
    return Image.fromarray((np.clip(small, 0, 1) * 255 + 0.5).astype(np.uint8), "RGBA")


def window(p, r, depth, lip, grow_by=0, c=0, material=GLASS, build="machined"):
    r = grow(r, grow_by)
    return p.sink([shape(build, *r, c) if c else plate(*r)], depth, lip, material)


# ---------------------------------------------------------------------------------------------- the bar

#
# Round 18, the owner's pick on 2026-11-04 (experiments/hud-sides-modern, sides4/6/7): the retail bar's
# depth in the compact layout.  Three boxes flush with the screen's edges, square where they meet one: the
# radar's own box with a ledge over its top, the centre panel whose rail rises into the money tab with the
# power bar sunk in a channel along it, and the selection window's box with a mount above it for minimise
# and promotion.  Every box is three tiers, a near-black shell, a bevelled lip and a recessed floor or a
# scanline well, and each side has its own silhouette and ornament: America's aerospace cuts and swept
# chevrons, China's eaves, stepped corners, lacquer bands and studs, the GLA's ragged welded scrap (no pipes
# or hoses), Turkiye's modern defence-industry armour with LED lines, optics and a faint girih etching.
# Nothing is a flag, crescent, arch, dome or other national or religious sign.
#
# The art is drawn in screen pixels (T moves it onto the texture) round the windows in layout().  The radar
# stands square in LeftHUD (no RadarFrameImage) and letterboxes a map of any shape itself; the powers tray,
# the buttons and the rank bar are the sheet's pieces, drawn by the code over the bar.
#
X0, X1 = 566, 1384                              # the centre panel
TAB0, TAB2 = 883, 1067                          # the money tab's foot
RIGHT_BOX = (1626, 863, 1920, 1080)
MOUNT = (1816, 794, 1920, 878)
RADAR_GLASS_AT = (5, 832, 327, 1064)            # the scheme's RadarUL/LR: LeftHUD, letterboxing the map
MONEY_WELL = (899, 840, 1051, 868)              # the readout's glass; the painted sign at its left, MoneyUL after it
MONEY_SIGN_AT, MONEY_SIGN_SIZE = (912, 854), 22
SHELL_TINT, FLOOR_TINT = 0.42, 0.30
BAR_LOOKS = {   # the bar's own steel, apart from SIDES, which the sheets, cards and emblems keep
    "America": dict(build="machined", steel=(84, 110, 142), glass=(12, 32, 40), glass_lo=(8, 21, 27),
                    accent=(0x44, 0x59, 0xb6), accent2=(30, 128, 98), crest=(0.35, 0.85, 1.0), crest_k=0.55),
    "China": dict(build="terraced", steel=(112, 116, 102), glass=(44, 15, 12), glass_lo=(26, 8, 7),
                  accent=(156, 30, 22), accent2=(150, 28, 20), crest=(1.0, 0.36, 0.22), crest_k=0.5),
    "Gla": dict(build="scavenged", steel=(152, 130, 90), glass=(8, 12, 6), glass_lo=(30, 40, 14),
                accent=(52, 46, 32), accent2=(150, 154, 156), crest=(1.0, 0.84, 0.52), crest_k=0.4),
    "Turkiye": dict(build="faceted", steel=(100, 106, 94), glass=(8, 16, 18), glass_lo=(5, 10, 12),
                    accent=(40, 200, 190), accent2=(66, 72, 70), crest=(0.5, 1.0, 0.95), crest_k=0.3),
}
# The watcher's HUD and the console tooltips (round 19, 2026-11-05): no side's, so the neutral steel family,
# round 18's three tiers in the desk build.  Plain graphite steel with no hue, square on the screen's edges and
# small 45 degree cuts elsewhere, and what a broadcast desk carries: perforated grilles, cross-head screws, the
# emblem's wing feathers in dark steel and a red tally lamp.  The only colour is the tally's red.
#
# Round 20 (2026-11-06, the owner's pick A of experiments/hud-sides-modern/observer/round20): the watcher belongs
# to no side, so each of his boxes is built the way one side builds, in the same neutral graphite.  The radar and
# the players' column are America's box under an arrow-point ledge, the followed player's panel China's stepped
# rail with its rivets and dark lacquer band, the selection box Turkiye's wide chamfer, armour panel, range
# ticks and recessed fasteners under a mount that holds the tally, and the drawers on the right the GLA's scrap,
# welded patches and odd bolts.  The Tab screen and the tooltips stand beside the radar box and take its build
# (skin); the shading stays the desk's even anodising, so no side's rust or brushing comes with a build.
BUILDS["desk"] = dict(metric="edt", bevel=2.0, corner="cut")
BAR_LOOKS["Observer"] = dict(build="desk", skin="machined", steel=(116, 120, 127), glass=(20, 24, 30), glass_lo=(11, 14, 18),
                             accent=(70, 76, 86), accent2=(0xb8, 0x26, 0x1f), crest=(0.85, 0.9, 1.0), crest_k=0.5)
OBS_LOOK = BAR_LOOKS["Observer"]


def T(poly):
    """Screen pixels to the bar's texels; a point on the screen's right edge goes to the texture's, so the
    edge reads as open (Painter.inside)."""
    return [(2048 if x >= 1920 else x, y - TOP) for x, y in poly]


def curve(p0, p1, p2, n=10):
    t = np.linspace(0, 1, n)[:, None]
    return [tuple(q) for q in (1 - t) ** 2 * np.array(p0) + 2 * (1 - t) * t * np.array(p1) + t ** 2 * np.array(p2)]


def mirror(poly):
    """A left half mirrored about the money tab's middle and walked back, so the two halves join."""
    cx = (TAB0 + TAB2) / 2
    return [(2 * cx - x, y) for x, y in reversed(poly)]


def jag(poly, seed, amp=2.5, step=14):
    """jitter in screen pixels."""
    return [(x, y + TOP) for x, y in jitter([(min(x, 1920), y - TOP) for x, y in poly], seed, amp, step)]


def stack(p, poly, shell_top=4.0, lip_top=7.0, inset=5.0, lip_bevel=2.6):
    """A box's first two tiers from one outline in screen pixels (Painter.tiers), open along the screen's
    edges."""
    p.tiers([T(poly)], None, shell_top, lip_top, inset, lip_bevel)
    p.limit[:, 1920 * SS:] = False


def shell(p, poly, top):
    """A dark piece standing proud of the rest (a fitting, a housing): a silhouette of its own."""
    sl, m, _ = p.silhouette([T(poly)], top, "machined", screen=True)
    p.tint[sl][m] = SHELL_TINT


def floor(p, poly, depth=3.0, edge=2.0):
    """The third tier in screen pixels (Painter.recess)."""
    p.recess([T(poly)], None, depth, edge)


def well(p, poly, depth=6, lip=2.5):
    p.sink([T(poly)], depth, lip)


def openings(p, kind, c):
    """Every opening, the same on every side; kind and c are the side's corner (corner_points)."""
    L = layout()
    S = lambda r: (r[0], r[1] + TOP, r[2], r[3] + TOP)  # noqa: E731
    g = RADAR_GLASS_AT
    floor(p, plate(0, g[1] - 5, g[2] + 5, 1080, tr=(kind, c)))
    well(p, plate(*grow(g, 2)), 5, 2)
    floor(p, plate(X0 + 9, 912, X1 - 9, 1080, tl=(kind, c), tr=(kind, c)))
    floor(p, plate(576, 916, 638, 1080, tl=(kind, c * 0.5), tr=(kind, c * 0.5)))     # the toolbar's own recess
    well(p, plate(646, 916, 1374, 1077, tl=(kind, c * 0.6), tr=(kind, c * 0.6)), 6, 2.5)
    well(p, plate(*grow(S(L["power"]), 2)), 5, 1.6)
    well(p, plate(*grow(MONEY_WELL, 1), tl=(kind, c), tr=(kind, c), br=(kind, c), bl=(kind, c)), 6, 2)
    floor(p, plate(RIGHT_BOX[0] + 12, RIGHT_BOX[1] + 12, 1916, 1080, tl=(kind, c * 1.5)))
    well(p, plate(*grow(S(L["portrait"]), 2)), 6, 2.5)
    well(p, plate(*S(L["exp"])), 6, 2)


def bar_america(p):
    """Aerospace plate: 45 degree cuts, a rail whose shoulders sweep down like a wing's leading edge, blue
    chevrons swept toward the money, a ledge ending in an arrow point, panel seams, octagon studs."""
    stack(p, [(0, 812), (317, 812), (345, 840), (345, 1080), (0, 1080)])
    stack(p, [(0, 795), (226, 795), (262, 809), (226, 823), (0, 823)], 9, 12, 4, 2.0)
    stack(p, plate(X0, 898, X1, 1080, tl=("cut", 22), tr=("cut", 22)))
    left = [(X0, 922), (X0 + 70, 878), (TAB0 - 28, 878), (TAB0 - 4, 840), (TAB0 + 8, 830)]
    stack(p, left + mirror(left), 9, 13, 4, 2.2)
    stack(p, plate(*RIGHT_BOX, tl=("cut", 36)))
    stack(p, plate(*MOUNT, tl=("cut", 20)), 9, 12, 4, 2.2)
    openings(p, "cut", 6)

    def swept(x, y0, y1, w, t, s):
        ym = (y0 + y1) / 2
        return [(x, y0), (x + s * t, y0), (x + s * (t + w), ym), (x + s * t, y1), (x, y1), (x + s * w, ym)]
    for i in range(3):
        p.inlay([T(swept(X0 + 34 + 13 * i, 898, 914, 7, 3.2, 1)), T(swept(X1 - 34 - 13 * i, 898, 914, 7, 3.2, -1))], ACCENT, 0.7)
    seams = [plate(x, 902, x + 1, 914) for x in (TAB0 - 90, TAB0 - 180, TAB2 + 90, TAB2 + 180)]
    seams += [plate(333, y, 345, y + 1) for y in (900, 1000)] + [plate(x, 866, x + 1, 875) for x in (1700, 1780)]
    seams += [plate(30, 798, 31, 820), plate(120, 798, 121, 820)]
    p.inlay([T(s) for s in seams], INKM, 0.8)
    p.inlay([T(plate(40, 804, 200, 806))], ACCENT, 0.5)
    for x, y in ((338, 1066), (1632, 1066), (1822, 870), (TAB0 + 8, 854), (TAB2 - 8, 854)):
        p.stud(x, y - TOP, 2.6)


def bar_china(p):
    """A pagoda: the money tab in two eaved steps, the rail's ends flicking up like eaves, stepped corners,
    red lacquer bands, gold studs and rows of rivets on heavy cast steel."""
    stack(p, [(0, 812), (321, 812), (321, 822), (333, 822), (333, 832), (345, 832), (345, 1080), (0, 1080)])
    ledge = [(0, 795), (214, 795)]
    ledge += (curve((214, 795), (238, 795), (262, 781), 8) + curve((256, 787), (244, 815), (226, 815), 8))[1:] + [(0, 815)]
    stack(p, ledge, 9, 12, 4, 2.0)
    stack(p, plate(X0, 898, X1, 1080, tl=("notch", 12), tr=("notch", 12)))
    left = [(X0 + 6, 922), (X0 + 12, 894)] + curve((X0 + 2, 882), (X0 - 4, 872), (X0 - 10, 862), 5)
    left += curve((X0 + 2, 870), (X0 + 30, 878), (X0 + 74, 880), 6)
    left += [(TAB0 - 40, 880)] + curve((TAB0 - 52, 866), (TAB0 - 58, 858), (TAB0 - 68, 850), 4)
    left += curve((TAB0 - 52, 856), (TAB0 - 36, 860), (TAB0 - 6, 860), 4)
    left += [(TAB0 - 6, 844)] + curve((TAB0 - 14, 834), (TAB0 - 20, 828), (TAB0 - 28, 820), 4)
    left += curve((TAB0 - 12, 826), (TAB0, 831), (TAB0 + 12, 832), 4)
    stack(p, left + mirror(left), 9, 13, 4, 2.2)
    stack(p, [(1626, 1080), (1626, 879), (1634, 879), (1634, 871), (1642, 871), (1642, 863), (1920, 863), (1920, 1080)])
    stack(p, [(1816, 878), (1816, 808), (1824, 808), (1824, 794), (1920, 794), (1920, 878)], 9, 12, 4, 2.2)
    openings(p, "notch", 4)
    L = layout()
    px0, px1 = L["power"][0], L["power"][2]
    bands = [plate(X0 + 16, 906, px0 - 6, 911), plate(px1 + 6, 906, X1 - 16, 911), plate(X0 + 30, 903, X1 - 30, 905),
             plate(TAB0 - 36, 874, TAB2 + 36, 878), plate(10, 802, 200, 807), plate(1665, 867, 1900, 871),
             plate(1840, 798, 1910, 800)]
    p.inlay([T(b) for b in bands], ACCENT, 0.3)
    for x in range(X0 + 26, X1 - 20, 34):
        if not px0 - 10 < x < px1 + 10:
            p.rivet(x, 894 - TOP, 2.2)
    for x in list(range(TAB0 - 30, TAB0 + 1, 15)) + list(range(TAB2, TAB2 + 31, 15)):
        p.rivet(x, 870 - TOP, 2.0)
    for x in range(1670, 1910, 30):
        p.rivet(x, 1076 - TOP, 2.0)
    for y in range(842, 1070, 26):
        p.rivet(339, y - TOP, 2.2)
    gold = [regular(x, 907 - TOP, 2.6, 12) for x in (X0 + 30, X1 - 30)]
    gold += [regular(x, 836 - TOP, 2.4, 12) for x in (TAB0 + 6, TAB2 - 6)]
    gold += [regular(x, 869 - TOP, 2.4, 12) for x in range(1670, 1900, 44)]
    p.bump(gold, 2.2, 1.4, GOLDM)


def bar_gla(p):
    """Welded scrap: nothing square or symmetric, a bent rail, a lopsided tab, ragged edges, patch plates
    laid on out of square and welded, odd bolts.  No pipes or hoses."""
    stack(p, jag([(0, 816), (180, 812), (296, 806), (340, 820), (349, 852), (347, 1080), (0, 1080)], 5))
    stack(p, jag([(0, 799), (130, 795), (200, 787), (238, 791), (244, 809), (0, 817)], 7, 1.5), 9, 12, 4, 2.0)
    stack(p, jag([(X0, 1080), (X0, 910), (X0 + 10, 898), (X1 - 4, 902), (X1, 1080)], 9))
    rail = [(X0 + 2, 920), (X0 + 16, 888), (TAB0 - 46, 884), (TAB0 - 12, 836), (TAB2 + 4, 832), (TAB2 + 50, 876),
            (X1 - 100, 878), (X1 - 34, 874), (X1, 914)]
    stack(p, jag(rail, 11, 1.6, 18), 9, 13, 4, 2.2)
    stack(p, jag([(1626, 1080), (1628, 885), (1648, 866), (1780, 863), (1920, 867), (1920, 1080)], 13))
    stack(p, jag([(1814, 878), (1818, 808), (1842, 794), (1920, 798), (1920, 878)], 17, 1.5), 9, 12, 4, 2.2)
    openings(p, "rough", 4)
    patches = [(574, 894, 616, 914, -7), (1316, 884, 1374, 904, 4), (333, 897, 349, 957, 3), (1660, 866, 1740, 876, -2),
               (60, 795, 120, 813, -3), (1830, 1062, 1912, 1078, 2)]
    for i, (a, b, c, e, deg) in enumerate(patches):
        sh = sheet(a, b, c, e, deg)
        p.bump([T(sh)], 1.4, 0.8, STEEL)
        for e0, e1 in zip(sh, sh[1:] + sh[:1]):
            p.weld((e0[0], e0[1] - TOP), (e1[0], e1[1] - TOP), seed=31 + i * 7 + int(e0[0]), r=1.2)
    for i, (x, y) in enumerate(((340, 1066), (10, 1068), (X0 + 5, 1040), (X1 - 5, 1040), (1632, 1068), (1912, 1064),
                                (1822, 866), (588, 904), (1350, 894), (90, 805))):
        p.bolt(x, y - TOP, 2.6 + (i % 3) * 0.4, seed=i)


def star8(cx, cy, r):
    """The eight-point girih star, two squares turned 45 degrees on each other."""
    return [(cx + (r if i % 2 == 0 else r * 0.765) * np.cos(np.pi / 8 * i - np.pi / 2),
             cy + (r if i % 2 == 0 else r * 0.765) * np.sin(np.pi / 8 * i - np.pi / 2)) for i in range(16)]


def ring(cx, cy, r, w=1.0, n=28):
    a = np.linspace(0, 2 * np.pi, n + 1)
    return [[(cx + (r - w / 2) * np.cos(u), cy + (r - w / 2) * np.sin(u)), (cx + (r + w / 2) * np.cos(u), cy + (r + w / 2) * np.sin(u)),
             (cx + (r + w / 2) * np.cos(v), cy + (r + w / 2) * np.sin(v)), (cx + (r - w / 2) * np.cos(v), cy + (r - w / 2) * np.sin(v))]
            for u, v in zip(a, a[1:])]


def bar_turkiye(p):
    """The modern Turkish defence industry: clean angular armour with wide chamfers, a flat-topped money tab,
    armour panels laid over the rail with gaps and recessed fasteners, an optic housing on the ledge, range
    ticks and sensor windows, a faint pixel camouflage, and one low-contrast girih etching on the ledge and
    along the rail.  Its LED lines and lamps are bar_lights."""
    stack(p, [(0, 812), (321, 812), (345, 836), (345, 1080), (0, 1080)])
    stack(p, [(0, 795), (224, 795), (248, 819), (0, 821)], 9, 12, 4, 2.0)
    stack(p, plate(X0, 898, X1, 1080, tl=("cut", 18), tr=("cut", 18)))
    left = [(X0, 920), (X0 + 44, 880), (TAB0 - 42, 880), (TAB0 - 12, 850), (TAB0 + 8, 832)]
    stack(p, left + mirror(left), 9, 13, 4, 2.2)
    stack(p, plate(*RIGHT_BOX, tl=("cut", 30)))
    stack(p, plate(*MOUNT, tl=("cut", 16)), 9, 12, 4, 2.2)
    panels = [[(X0 + 12, 914), (X0 + 44, 884), (X0 + 76, 884), (X0 + 76, 914)],
              [(X1 - 12, 914), (X1 - 44, 884), (X1 - 76, 884), (X1 - 76, 914)],
              [(TAB0 - 30, 878), (TAB0 - 8, 856), (TAB0 + 2, 856), (TAB0 + 2, 878)],
              [(TAB2 + 30, 878), (TAB2 + 8, 856), (TAB2 - 2, 856), (TAB2 - 2, 878)],
              [(1640, 877), (1660, 871), (1740, 871), (1740, 877)]]
    p.bump([T(q) for q in panels], 2.2, 1.0, STEEL)
    shell(p, plate(186, 785, 218, 805, tl=("cut", 4), tr=("cut", 6)), 9.0)
    p.bump([T(plate(189, 787, 215, 803, tl=("cut", 3), tr=("cut", 5)))], 4.0, 1.4, STEEL)
    well(p, regular(205, 795, 4.2, 16), 3, 1.2)
    openings(p, "cut", 5)
    for x in (X0 + 22, X1 - 40):                                     # sensor windows
        well(p, plate(x, 902, x + 18, 907), 3, 1.0)
    gaps = [plate(x, 882, x + 1, 914) for x in (X0 + 78, X1 - 79, TAB0 - 70, TAB2 + 69)]
    gaps += [plate(333, y, 345, y + 1) for y in (900, 980)] + [plate(x, 866, x + 1, 875) for x in (1760, 1840)]
    gaps += [plate(120, 796, 121, 820)]
    p.inlay([T(q) for q in gaps], INKM, 0.9)
    ticks = [plate(x, 866, x + 1, 869 if i % 5 else 872) for i, x in enumerate(range(1750, 1910, 8))]
    ticks += [plate(337, y, 340 if i % 5 else 343, y + 1) for i, y in enumerate(range(990, 1066, 8))]
    p.inlay([T(q) for q in ticks], INKM, 0.6)
    L = layout()
    etch = [T(star8(x, 808, 5.5)) for x in range(20, 116, 16)]
    etch += [T(q) for x in range(20, 116, 16) for q in ring(x, 808, 7.2, 0.6, 20)]
    etch += [T(star8(x, 907.5, 3.6)) for x in np.arange(L["power"][0] + 8, L["power"][2] - 4, 14)]
    p.inlay(etch, DARK, 0.25)
    for x, y in ((X0 + 5, 1000), (X1 - 5, 1000), (339, 1068), (1632, 1068), (X0 + 60, 910), (X1 - 60, 910),
                 (1906, 874), (14, 816), (236, 817), (1822, 866)):
        p.inlay([T(regular(x, y, 2.8, 12))], DARK, 1.0)              # a recessed fastener
        p.bump([T(regular(x, y, 1.5, 6))], 0.6, 0.6, STEEL)
    rng = np.random.default_rng(5)                                   # the plates' pixel camouflage
    n = ndimage.gaussian_filter(rng.standard_normal((512 // 4, 2048 // 4)), 1.4)
    n /= n.std()
    tone = np.kron(np.where(n < -0.5, 0.92, np.where(n > 0.6, 1.07, 1.0)).astype(np.float32), np.ones((4 * SS, 4 * SS), np.float32))
    sel = (p.mat == STEEL) & p.alpha
    p.tint[sel] *= tone[sel]


TURQ, AMBER = (64, 226, 214), (255, 176, 48)


def bar_lights(img, p):
    """Turkiye's turquoise LED edge lines and status lamps and its amber indicators, with a little glow kept
    on the plate."""
    lay = Image.new("RGBA", img.size, (0, 0, 0, 0))
    d = ImageDraw.Draw(lay)
    for (a, b) in (((X0 + 46, 883), (X0 + 74, 883)), ((X1 - 46, 883), (X1 - 74, 883)), ((TAB0 - 38, 877), (TAB0 - 10, 850)),
                   ((TAB2 + 38, 877), (TAB2 + 10, 850)), ((1660, 869), (1740, 869)), ((6, 818), (110, 818)),
                   ((1834, 875), (1908, 875))):
        d.line([(a[0], a[1] - TOP), (b[0], b[1] - TOP)], fill=TURQ + (245,), width=1)
    for x, y, c in ((X0 + 31, 904.5, TURQ), (X1 - 31, 904.5, TURQ), (1640, 1066, AMBER), (1650, 1066, TURQ),
                    (1822, 806, TURQ), (1822, 840, AMBER), (205, 795, TURQ)):
        d.ellipse((x - 1.8, y - TOP - 1.8, x + 1.8, y - TOP + 1.8), fill=c + (255,))
    glow = np.asarray(lay.filter(ImageFilter.GaussianBlur(3)), np.float32)
    a = np.asarray(img, np.float32)
    for layer, k in ((glow, 1.6), (np.asarray(lay, np.float32), 1.0)):
        w = np.clip(layer[..., 3:4] / 255 * k, 0, 1) * (a[..., 3:4] / 255)
        a[..., :3] = a[..., :3] * (1 - w) + layer[..., :3] * w
    return Image.fromarray(a.astype(np.uint8), "RGBA")


def led(img, lines, lamps):
    """Lit lines one texel wide and round lamps with a little glow kept on the plate (bar_lights' way), for
    the pieces drawn off the bar: Turkiye's card and Tab screen."""
    lay = Image.new("RGBA", img.size, (0, 0, 0, 0))
    d = ImageDraw.Draw(lay)
    for a, b in lines:
        d.line([a, b], fill=TURQ + (235,), width=1)
    for x, y, c in lamps:
        d.ellipse((x - 1.6, y - 1.6, x + 1.6, y + 1.6), fill=c + (255,))
    glow = np.asarray(lay.filter(ImageFilter.GaussianBlur(2)), np.float32)
    a = np.asarray(img, np.float32)
    for layer, k in ((glow, 1.4), (np.asarray(lay, np.float32), 1.0)):
        w = np.clip(layer[..., 3:4] / 255 * k, 0, 1) * (a[..., 3:4] / 255)
        a[..., :3] = a[..., :3] * (1 - w) + layer[..., :3] * w
    return Image.fromarray(a.astype(np.uint8), "RGBA")


BARS = {"America": bar_america, "China": bar_china, "Gla": bar_gla, "Turkiye": bar_turkiye}
BAR_POST = {"Turkiye": bar_lights}


def bar_finish(img, p, look, scan=True, gloss=True):
    """Retail's scanlines and a soft gloss in the glass, a glint the side's colour on the crests that face the
    light, and the hottest highlights eased (a knee at 0.68, slope 0.6).  Any painter's size; a piece the
    code stretches goes without the scanlines, and one it tiles without the gloss, which varies down the
    page."""
    H, W = p.h_, p.w
    gm = (p.mat == GLASS).astype(np.float32).reshape(H, SS, W, SS).mean(axis=(1, 3))
    hs = ndimage.gaussian_filter(p.h, 0.7 * SS)
    gy, gx = np.gradient(hs)
    crest = np.clip(-ndimage.laplace(hs) * SS * SS * 1.4, 0, 1) * np.clip((gy + gx * 0.6) * SS * 0.9, 0, 1)
    crest = (crest * (p.mat != GLASS) * p.alpha).reshape(H, SS, W, SS).mean(axis=(1, 3))
    a = np.asarray(img, np.float32) / 255
    rgb, al = a[..., :3], a[..., 3:]
    yy = np.arange(H)[:, None]
    scan = np.where(yy % 2 == 0, 0.74, 1.0)[..., None] if scan else np.ones((H, 1, 1))
    gloss = (1 + 0.14 * np.exp(-(((yy - 300) % 200) / 70.0)))[..., None] if gloss else np.ones((H, 1, 1))
    g = gm[..., None]
    rgb = rgb * (1 - g) + np.clip(rgb * scan * gloss + np.array([0.0, 0.012, 0.016]), 0, 1) * g
    rgb = np.clip(rgb + crest[..., None] * np.array(look["crest"]) * look["crest_k"], 0, 1)
    lum = rgb.max(2, keepdims=True)
    rgb = rgb * np.where(lum > 0.68, (0.68 + (lum - 0.68) * 0.6) / np.maximum(lum, 1e-6), 1.0)
    return Image.fromarray((np.dstack([rgb, al]) * 255 + 0.5).astype(np.uint8), "RGBA")


def money_sign(img):
    """The gold currency sign at the money well's left (the schemes' MoneySignPainted): the readout
    is one colour, so the number is the code's white and the sign is painted."""
    from PIL import ImageFont
    font = ImageFont.truetype(os.path.join(os.environ.get("WINDIR", "C:/Windows"), "Fonts", "arialbd.ttf"), MONEY_SIGN_SIZE)
    lay = Image.new("RGBA", img.size, (0, 0, 0, 0))
    d = ImageDraw.Draw(lay)
    x, y = MONEY_SIGN_AT[0], MONEY_SIGN_AT[1] - TOP
    d.text((x + 1, y + 1), "$", font=font, fill=(0, 0, 0, 200), anchor="mm")
    d.text((x, y), "$", font=font, fill=GOLD + (255,), anchor="mm")
    out = img.copy()
    out.alpha_composite(lay)
    return out


def paint_bar(side_name="America"):
    look = BAR_LOOKS[side_name]
    p = Painter(2048, 512)
    BARS[side_name](p)
    img = money_sign(bar_finish(shade(p, look, SEAMS), p, look))
    return BAR_POST[side_name](img, p) if side_name in BAR_POST else img


def wing_chevrons(p, cx, y0, y1, w, material, gap=9):
    """As many chevron pairs as fit between y0 and y1, spread evenly with the slack split top and bottom."""
    ph = (10.82 + 8.11 + 4.96) * w / 19.82
    n = int((y1 - y0 + gap) // (ph + gap))
    y = y0 + (y1 - y0 - (n * ph + (n - 1) * gap)) / 2
    for _ in range(n):
        p.inlay(chevrons(cx, y, w, n=2), material, 0.7)
        y += ph + gap


def vents(p, x0, x1, y0, y1, pitch=7):
    """A row of 45 degree slots cut into the flange."""
    t = y1 - y0
    p.inlay([[(x, y1), (x + t, y0), (x + t + 2.4, y0), (x + 2.4, y1)] for x in np.arange(x0, x1 - t, pitch)], INKM, 1.0)


# ---------------------------------------------------------------------------------------------- the button sheet

def read_mapped(side_name, sheet="Powers", name=None):
    """Every MappedImage of ReforgedHud<side><sheet>.ini (or of <name>.ini): name -> (x0, y0, x1, y1)."""
    path = os.path.join(HERE, "..", "Data", "INI", "MappedImages", "HandCreated", (name or "ReforgedHud%s%s" % (side_name, sheet)) + ".ini")
    text = open(path, encoding="latin-1").read()
    out = {}
    for m in re.finditer(r"MappedImage\s+(\S+).*?Left:(\d+)\s+Top:(\d+)\s+Right:(\d+)\s+Bottom:(\d+)", text, re.S):
        out[m.group(1)] = tuple(int(v) for v in m.groups()[1:])
    return out


def icon(kind, cx, cy, build):
    """The glyph a button carries, centred: raised polygons, ink polygons cut into them, and any
    polygons raised again inside the ink."""
    if kind == "Chat":
        bubble = octagon(cx - 12, cy - 8, cx + 12, cy + 5, 3)
        tail = [(cx - 9, cy + 4), (cx - 2, cy + 4), (cx - 10, cy + 11)]
        return [bubble, tail], [plate(cx - 8, cy - 4.5, cx + 8, cy - 2.5), plate(cx - 8, cy - 0.5, cx + 3, cy + 1.5)]
    if kind == "Options":
        bars = [plate(cx - 12, y - 1, cx + 12, y + 1) for y in (cy - 6, cy, cy + 6)]
        knobs = [octagon(x - 3.2, y - 3.2, x + 3.2, y + 3.2, 1.4) for x, y in ((cx - 5, cy - 6), (cx + 6, cy), (cx - 1, cy + 6))]
        return bars + knobs, []
    # the idle column's four, one set: a figure from the chest up for the two on foot, a tank side on and
    # a jet from above.  The worker's hard hat has a brim all round and a ridge, the soldier's helmet
    # neither, and each carries his tool over the right shoulder
    def bust(c):
        return [octagon(c[0] - 3.6, c[1] - 2.5, c[0] + 3.6, c[1] + 3, 1.6),
                [(c[0] - 10, c[1] + 11), (c[0] - 9, c[1] + 6), (c[0] - 4, c[1] + 4), (c[0] + 4, c[1] + 4),
                 (c[0] + 9, c[1] + 6), (c[0] + 10, c[1] + 11)]]
    dome = [(cx - 5.6, cy - 3), (cx - 5, cy - 7), (cx - 2.6, cy - 9.6), (cx + 2.6, cy - 9.6), (cx + 5, cy - 7), (cx + 5.6, cy - 3)]
    if kind == "Infantry":
        rifle = [(cx + 6.2, cy + 9.5), (cx + 11.2, cy - 10), (cx + 12.8, cy - 9.6), (cx + 7.8, cy + 9.9)]
        strap = [(cx - 7, cy + 5.4), (cx - 6, cy + 4.6), (cx + 7, cy + 10.2), (cx + 6, cy + 11)]
        return bust((cx - 1, cy)) + [[(x - 1, y) for x, y in dome], rifle], [strap]
    if kind == "Worker":
        hat = [(x - 1, y) for x, y in dome] + [(cx + 6.6, cy - 3)]
        brim = plate(cx - 9.5, cy - 3.6, cx + 7.5, cy - 1.8)
        handle = [(cx + 6.6, cy + 9.5), (cx + 10, cy - 3), (cx + 11.6, cy - 2.6), (cx + 8.2, cy + 9.9)]
        head = octagon(cx + 7.6, cy - 9.6, cx + 14.4, cy - 2.4, 2)
        ridge = plate(cx - 1.7, cy - 9.6, cx - 0.3, cy - 4)
        jaw = plate(cx + 10.2, cy - 10, cx + 11.8, cy - 6)
        return bust((cx - 1, cy)) + [hat, brim, handle, head], [ridge, jaw]
    if kind == "Vehicle":
        x = cx - 1
        tracks = octagon(x - 13, cy + 2, x + 13, cy + 9, 3)
        hull = [(x - 12, cy + 2.5), (x - 10, cy - 2), (x + 11, cy - 2), (x + 13, cy + 2.5)]
        turret = [(x - 7, cy - 1.5), (x - 5.5, cy - 7), (x + 3, cy - 7), (x + 5, cy - 1.5)]
        barrel = plate(x + 3, cy - 5.6, x + 15.5, cy - 4)
        wheels = [octagon(x + dx - 1.5, cy + 4, x + dx + 1.5, cy + 7, 0.7) for dx in (-9, -4.5, 0, 4.5, 9)]
        return [tracks, hull, turret, barrel], wheels
    if kind == "Aircraft":
        half = [(0, -12), (1.6, -8), (1.8, -3), (12.5, 3), (12.5, 5.6), (1.8, 3), (1.6, 7.5), (5.6, 10.2), (5.6, 12)]
        jet = [(cx + x, cy + y) for x, y in half] + [(cx, cy + 11)] + [(cx - x, cy + y) for x, y in reversed(half[1:])]
        canopy = plate(cx - 0.6, cy - 8.5, cx + 0.6, cy - 4.5)
        return [jet], [canopy]
    if kind == "Beacon":
        # an exclamation inside a ring: the ring is the plate with its middle cut back out
        ring = [octagon(cx - 9, cy - 9, cx + 9, cy + 9, 5.2)], [octagon(cx - 6.6, cy - 6.6, cx + 6.6, cy + 6.6, 3.8)]
        mark = [plate(cx - 1.5, cy - 5.5, cx + 1.5, cy + 1.5), plate(cx - 1.5, cy + 3, cx + 1.5, cy + 5.8)]
        return ring[0], ring[1], mark
    if kind in ("Maximize", "Minimize"):
        up = kind == "Maximize"
        if build == "machined":
            return chevrons(cx, cy - 6.5, 22, n=1, up=up), []
        if build == "terraced":                         # a stepped pyramid
            rows = [plate(cx - 4 - 5 * i, cy - 6 + 4.5 * i, cx + 4 + 5 * i, cy - 3 + 4.5 * i) for i in range(3)]
            return (rows if up else [[(x, 2 * cy - y) for x, y in r] for r in rows]), []
        if build == "scavenged":
            return [triangle(cx, cy, 20, 11, "up" if up else "down")], []
        return [triangle(cx, cy, 24, 13, "up" if up else "down")], [triangle(cx, cy + (1.6 if up else -1.6), 13, 6, "up" if up else "down")]
    if kind == "General":
        # three stars set on a shallow chevron, the middle one larger and higher
        return [star(cx - 21, cy + 1.5, 6.5), star(cx, cy - 0.5, 9.0), star(cx + 21, cy + 1.5, 6.5)], []
    if kind == "Back":                                  # the observer's way back to the list (prototype)
        return [[(cx - 11, cy), (cx - 3, cy - 8), (cx - 3, cy - 3), (cx + 11, cy - 3), (cx + 11, cy + 3), (cx - 3, cy + 3),
                 (cx - 3, cy + 8)]], []
    if kind == "Close":                                 # the powers window's close (prototype)
        bar = plate(cx - 13, cy - 2.6, cx + 13, cy + 2.6)
        return [rotated(bar, cx, cy, 45), rotated(bar, cx, cy, -45)], []
    raise ValueError(kind)


def star(cx, cy, r, inner=0.42):
    """A five-pointed star, point up, straight cuts."""
    pts = []
    for i in range(10):
        a = np.radians(-90 + 36 * i)
        k = r if i % 2 == 0 else r * inner
        pts.append((cx + k * np.cos(a), cy + k * np.sin(a)))
    return pts


BEZEL_SCALE = 0.67                                      # a button's bezel chamfer against its build's plates
BEZEL_FACE_INSET = 4                                    # texels from the button's edge to its sunk face


def button(p, r, kind, state, build):
    """A bezel in the side's build round a dark face carrying the glyph; state is '', 'H', 'P' or 'I'."""
    x0, y0, x1, y1 = r
    bezel = {"": STEEL, "H": BRIGHT, "P": STEEL, "I": DARK}[state]
    glyph = {"": PAPERM, "H": GOLDM, "P": HILITE, "I": DIM}[state]
    p.frame([shape(build, x0, y0, x1, y1, 5)], 2.0 if state == "P" else 3.5, build, bezel, BEZEL_SCALE)
    inner = (x0 + BEZEL_FACE_INSET, y0 + BEZEL_FACE_INSET, x1 - BEZEL_FACE_INSET, y1 - BEZEL_FACE_INSET)
    p.sink([shape(build, *inner, 3)], 2.5 if state == "P" else 1.4, 1.0)
    if build == "terraced":
        for x in (x0 + 2.2, x1 - 2.2):
            p.rivet(x, (y0 + y1) / 2, 1.3)
    elif build == "scavenged":
        p.bolt(x1 - 3, y0 + 3, 1.6, x0)
    if state == "H":
        # the close button's rule is two texels over the face's lower lip and 8 in from each side, so
        # each DXT5 block it touches holds only the dark face and the blue: one texel of it, sharing a
        # block with the lip's crest or the bezel, lost its blue in most blocks
        inset, foot = (8, 4) if kind == "Close" else (6, 5)
        sl, m = p.inlay([plate(x0 + inset, y1 - 6, x1 - inset, y1 - foot)], HILITE, 0.2)
        if kind == "Close" and BUILDS[build].get("bevel", 1.0) * BEZEL_SCALE > BEZEL_FACE_INSET:
            # laid flat where the bezel's chamfer runs on under the face (the faceted build's 4.7
            # texels): on the slope the rule lit as a grey-to-pale gradient that DXT5 smeared into the
            # white lip below, and in game it read as part of the lip
            p.h[sl][m] = p.h[sl][m].max()
    if kind is None:                            # a plate the code writes its text on
        return
    off = 1 if state == "P" else 0
    raised, ink, *again = icon(kind.replace("Idle", ""), (x0 + x1) / 2 + off, (y0 + y1) / 2 + off, build)
    p.bump(raised, 1.1, 0.6, glyph)
    if ink:
        p.inlay(ink, GLASS, 0.9)
    for polys in again:
        p.bump(polys, 1.1, 0.6, glyph)


# The general's promotion window (round 9's B, picked by the owner on 2026-11-03).  Same 542x598 picture
# and every opening where GeneralsExpPoints.wnd puts it: the four text bands, the rank 1, 3 and 8 cells,
# the points readout and the close button.  The powers' icons cover their cells whole, so what shows is
# the frame, the gaps, the margins, the right column's two sockets and any slot a side leaves empty.  The
# outline steps in round the points and close sockets, which stand on bosses of their own; each rank is
# one tray parted by mullions, its empty slots dark wells; the rank's stars run down the left margin
# (one, three and five: rank 8 is the five-star general).  The close button is the side's own
# (<S>ProGenPowerClose{,H,I}, 60x50, where the Powers INI maps it).
PROMO_R1 = [(18 + 102 * i, 54, 116 + 102 * i, 132) for i in range(4)]
PROMO_R3 = [(18 + 102 * i, 178 + 82 * j, 116 + 102 * i, 256 + 82 * j) for i in range(5) for j in range(3)]
PROMO_R8 = [(18 + 102 * i, 466, 116 + 102 * i, 544) for i in range(4)]
PROMO_BANDS = [(13, 17, 529, 45), (17, 141, 525, 169), (17, 429, 525, 457), (13, 553, 529, 581)]
PROMO_POINTS, PROMO_CLOSE = (450, 68, 510, 118), (450, 480, 510, 530)
PROMO_STARS = ((93, 1), (299, 3), (505, 5))


def promo_window(p, x0, y0, build):
    def r(a, b, c, d):
        return (x0 + a, y0 + b, x0 + c, y0 + d)

    def R(rect, g=0):
        return grow(r(*rect), g)
    W, H = 542, 598
    # the outline cuts the right column back beside the rank 1 and rank 8 rows (x 430 on), leaving the
    # points and close sockets on bosses of their own
    notch = 8 if build != "terraced" else 6
    body = [(0, 0), (W, 0), (W, 50), (430, 50), (430, 136), (W, 136), (W, 462), (430, 462), (430, 548), (W, 548), (W, H), (0, H)]
    k = BUILDS[build]["corner"]
    if k == "slant":
        body = [(0, 10), (17, 0), (W - 10, 0), (W, 6), (W, 46), (434, 50), (430, 56), (430, 130), (434, 136), (W, 140), (W, 458),
                (434, 462), (430, 468), (430, 542), (434, 548), (W, 552), (W, H - 6), (W - 10, H), (17, H), (0, H - 10)]
    elif k == "notch":
        body = [(0, notch), (notch, notch), (notch, 0), (W - notch, 0), (W - notch, notch), (W, notch), (W, 50), (430, 50), (430, 136),
                (W, 136), (W, 462), (430, 462), (430, 548), (W, 548), (W, H - notch), (W - notch, H - notch), (W - notch, H),
                (notch, H), (notch, H - notch), (0, H - notch)]
    else:
        c = 14
        body = [(0, c), (c, 0), (W - c, 0), (W, c), (W, 44), (W - 6, 50), (430, 50), (430, 136), (W - 6, 136), (W, 142), (W, 456),
                (W - 6, 462), (430, 462), (430, 548), (W - 6, 548), (W, 554), (W, H - c), (W - c, H), (c, H), (0, H - c)]
    body = [(x0 + x, y0 + y) for x, y in body]
    # each boss on a short bracket back to the body
    bosses = [shape(build, *R(PROMO_POINTS, 14), 9), shape(build, *R(PROMO_CLOSE, 14), 9),
              plate(*r(424, 86, 440, 100)), plate(*r(424, 498, 440, 512))]
    if build == "scavenged":
        body = jitter(body, 5, 2.0)
        bosses = [jitter(b, 6 + i, 1.4, 8) for i, b in enumerate(bosses)]

    def inner(i, c):
        """The plate laid on the frame, i texels in, its outline stepped like the body's."""
        k = BUILDS[build]["corner"] if BUILDS[build]["corner"] != "rough" else "cut"
        return [plate(*r(i, i, W - i, 50 - i), tl=(k, c), tr=(k, c)), plate(*r(i, i, 430 - i, H - i), tl=(k, c), bl=(k, c)),
                plate(*r(i, 136 + i, W - i, 462 - i), tr=(k, c * 0.5), br=(k, c * 0.5)),
                plate(*r(i, 548 + i, W - i, H - i), bl=(k, c), br=(k, c))] + \
            [shape(build, *R(b, 14 - i), 7) for b in (PROMO_POINTS, PROMO_CLOSE)]

    # the frame, each build its own way
    if build == "machined":                     # a dark flange, a satin plate on it
        p.silhouette([body] + bosses, 4.0, build)
        p.raise_plate(inner(6, 12), 7.0, 3.0, STEEL)
    elif build == "terraced":                   # cast in square terraces, the outer one dark
        sl, m, d = p.silhouette([body] + bosses, 11.0, build, STEEL)
        p.mat[sl][m & (d < 5)] = DARK
    elif build == "scavenged":                  # a ragged frame, scrap sheets laid round it
        p.silhouette([body] + bosses, 3.0, build)
        for k, ((a, b, c, e), deg, tint) in enumerate((((-4, -4, W + 4, 15), 0.12, 1.04), ((-4, 583, W + 4, H + 4), -0.1, 0.94),
                                                       ((-4, 12, 12, 586), 0.15, 0.9), ((530, 12, W + 4, 586), -0.12, 1.07))):
            poly = sheet(*r(a, b, c, e), deg)
            sl, m, _ = p.raise_plate([poly], 5.0 + (k % 2) * 0.9, 1.2, STEEL)
            p.tint[sl][m] = tint
        p.raise_plate(inner(12, 6), 4.2, 1.2, STEEL)
    else:                                       # composite with one wide flat facet for an edge
        p.silhouette([body] + bosses, 4.0, "machined")
        p.frame(inner(5, 8), 9.0, build)
    # nothing opaque past the outline: the sheets run on past the ragged edge, and the steps stay open
    win = (slice(max(y0 - 6, 0) * SS, (y0 + H + 6) * SS), slice(max(x0 - 6, 0) * SS, (x0 + W + 6) * SS))
    p.alpha[win] &= p.limit[win]

    # the openings
    lip, depth, c = {"machined": (3, 6, 0), "terraced": (1.2, 8, 4), "scavenged": (2, 5, 4), "faceted": (4, 6, 5)}[build]
    if build == "terraced":                     # a heavy square rim round every band and socket
        for band in PROMO_BANDS:
            p.bump([shape(build, *R(band, 4), 3)], 2.0, 1.0)
        for b in (PROMO_POINTS, PROMO_CLOSE):
            p.bump([shape(build, *R(b, 5), 4)], 2.0, 1.0)
    for band in PROMO_BANDS:
        window(p, R(band), 3, 2, 0, c * 0.8, build=build)
    # each rank one tray, the cells parted by mullions
    for tray in ((18, 54, 422, 132), (18, 178, 524, 420), (18, 466, 422, 544)):
        window(p, R(tray, 3), depth, lip, 0, c, build=build)
    for tray, cols, rows in (((18, 54, 422, 132), 4, 1), ((18, 178, 524, 420), 5, 3), ((18, 466, 422, 544), 4, 1)):
        for i in range(1, cols):
            x = tray[0] + 102 * i - 2
            p.raise_plate([plate(*r(x - 1.3, tray[1] - 2, x + 1.3, tray[3] + 2))], 1.5, 0.6, STEEL)
        for j in range(1, rows):
            y = tray[1] + 82 * j - 2
            p.raise_plate([plate(*r(tray[0] - 2, y - 1.3, tray[2] + 2, y + 1.3))], 1.5, 0.6, STEEL)
    window(p, R(PROMO_POINTS, 2), depth - 1, lip, 0, 6, build=build)
    window(p, R(PROMO_CLOSE, 2), 3, 2, 0, 6, STEEL, build)
    if build == "faceted":
        p.facets(40, 0.06, 0.45, 29)            # after the openings, so the glass stays flat

    # the rank's stars down the left margin: one, three, five
    star_mat = GOLDM if build == "machined" else PAPERM if build in ("terraced", "faceted") else ACCENT2
    for cy, n in PROMO_STARS:
        for k in range(n):
            yy = cy + (k - (n - 1) / 2) * 13
            pts = star(x0 + 11, y0 + yy, 4.6)
            if build == "scavenged":                            # stencilled by hand, none quite level
                pts = rotated(pts, x0 + 11, y0 + yy, 7 * (k - 2))
            if build == "terraced":
                p.bump([pts], 0.9, 0.5, star_mat)
            else:
                p.inlay([pts], star_mat, 0.5)

    # what each build fixes to it
    if build == "machined":
        # the shield's chevrons up the right margin (blue) and in the points boss (gold), studs at the corners
        wing_chevrons(p, x0 + 534, y0 + 186, y0 + 412, 10, ACCENT, gap=7)
        for x in (442.5, 517.5):
            p.bump(chevrons(x0 + x, y0 + 84, 7), 0.8, 0.5, GOLDM)
        for x, y in ((14, 9.5), (W - 14, 9.5), (14, H - 9.5), (W - 14, H - 9.5), (W / 2, 9.5), (W / 2, H - 9.5)):
            p.stud(x0 + x, y0 + y, 2.8)
        p.inlay([plate(*r(40, 8.2, 250, 10))], ACCENT, 0.4)
        p.inlay([plate(*r(292, 588, 502, 589.8))], ACCENT2, 0.4)
        vents(p, x0 + 40, x0 + 230, y0 + 584, y0 + 592)
    elif build == "terraced":
        for t in np.linspace(40, 500, 13):
            p.rivet(x0 + t, y0 + 4.5, 1.9)
            p.rivet(x0 + t, y0 + H - 4.5, 1.9)
        for y in np.arange(190, 410, 18):                       # ingots up the right margin
            p.bump([plate(*r(530, y, 538, y + 9))], 1.6, 0.8, ACCENT2)
        p.inlay([plate(*r(40, 9, 500, 12))], ACCENT, 0.3)
    elif build == "scavenged":
        for x in range(30, 220, 15):                            # hazard slashes, the top edge only
            p.inlay([[(x0 + x, y0 + 13), (x0 + x + 7, y0 + 5), (x0 + x + 13, y0 + 5), (x0 + x + 6, y0 + 13)]], ACCENT, 0.3)
        p.inlay([jitter(plate(*r(531, 190, 539, 400)), 47, 2.0, 5)], ACCENT2, 0.25)
        p.weld((x0 + 12, y0 + 14), (x0 + 530, y0 + 14), 151, 1.1)
        p.weld((x0 + 12, y0 + 584), (x0 + 530, y0 + 584), 152, 1.1)
        for i, (x, y) in enumerate(((6, 6), (W - 7, 8), (5, H - 6), (W - 6, H - 7), (270, 7), (150, H - 6))):
            p.bolt(x0 + x, y0 + y, 2.8, 30 + i)
    else:
        for y in np.arange(196, 410, 26):                       # triangle markers pointing in from the right
            p.inlay([triangle(x0 + 534, y0 + y, 9, 10, "left")], ACCENT, 0.6)
        for x, y in ((12, 10), (W - 12, 10), (12, H - 10), (W - 12, H - 10)):
            p.bump([regular(x0 + x, y0 + y, 3.0, 6, np.pi / 6)], 0.8, 1.2, BRIGHT)
        p.inlay([plate(*r(40, 9, 200, 11)), [(x0 + 200, y0 + 9), (x0 + 210, y0 + 14), (x0 + 210, y0 + 16), (x0 + 200, y0 + 11)],
                 plate(*r(210, 14, 260, 16))], ACCENT, 0.4)


def exp_bar(p, x0, y0, build):
    """The rank bar's frame, 36x184; the fill (ControlBar.wnd:GeneralsExp) shows through 5..31 x 36..176."""
    p.frame([shape(build, x0, y0, x0 + 36, y0 + 184, 5)], 5.0, build, STEEL, 0.83)
    p.sink([plate(x0 + 5, y0 + 36, x0 + 31, y0 + 176)], 3, 1.5)
    p.cut([plate(x0 + 6, y0 + 37, x0 + 30, y0 + 175)])
    p.bump([shape(build, x0 + 8, y0 + 5, x0 + 28, y0 + 31, 5)], 1.5, 1.0, ACCENT)
    if build == "machined":
        p.inlay(chevrons(x0 + 18, y0 + 9.5, 12), PAPERM, 0.4)
    elif build == "terraced":
        p.inlay([plate(x0 + 12, y0 + y, x0 + 24, y0 + y + 3) for y in (10, 16.5, 23)], PAPERM, 0.4)
    elif build == "scavenged":
        p.inlay([[(x0 + 11, y0 + 24), (x0 + 18, y0 + 10), (x0 + 25, y0 + 24), (x0 + 21, y0 + 24), (x0 + 18, y0 + 17), (x0 + 15, y0 + 24)]], PAPERM, 0.4)
    else:
        p.inlay([triangle(x0 + 18, y0 + 18, 12, 12, "up")], PAPERM, 0.4)
    for k in range(1, 10):                                      # rungs across the fill, every tenth
        y = y0 + 36 + 14 * k
        w = 26 if k % 5 == 0 else 8
        p.raise_plate([plate(x0 + 5, y - 1, x0 + 5 + w, y + 1)], 2.5, 0.6, BRIGHT)
        if w < 26:
            p.raise_plate([plate(x0 + 31 - w, y - 1, x0 + 31, y + 1)], 2.5, 0.6, BRIGHT)


def shortcut_tray(p, x0, y0, build):
    """One shortcut button's plate, 119x110 (the top 92 rows are the stacked pieces' TraySmallTop)."""
    p.silhouette([shape(build, x0 + 20, y0 + 10, x0 + 119, y0 + 100, 9)], 4.0, build)
    p.frame([shape(build, x0 + 27, y0 + 16, x0 + 119, y0 + 94, 6)], 7.0, build, STEEL)
    window(p, (x0 + 35, y0 + 25, x0 + 110, y0 + 85), 6, 3, 1)
    if build == "machined":
        p.inlay(chevrons(x0 + 24, y0 + 48, 6, n=2), ACCENT, 0.5)
    elif build == "terraced":
        for y in (30, 55, 80):
            p.rivet(x0 + 24, y0 + y, 1.8)
    elif build == "scavenged":
        p.bolt(x0 + 24, y0 + 40, 2.4, 3)
        p.inlay([jitter(plate(x0 + 21, y0 + 50, x0 + 27, y0 + 88), 8, 1.2, 4)], ACCENT, 0.3)
    else:
        p.inlay([triangle(x0 + 24, y0 + 55, 6, 8, "right")], ACCENT, 0.5)


def hover_bracket(p, x0, y0, build):
    """The radar corner's hover mark, 64x64: a bracket round the top right."""
    pts = [(18, 3), (52, 3), (61, 12), (61, 46), (56, 46), (56, 14), (50, 8), (18, 8)]
    if build == "terraced":
        pts = [(18, 3), (61, 3), (61, 46), (56, 46), (56, 8), (18, 8)]
    p.raise_plate([[(x0 + x, y0 + y) for x, y in pts]], 2.0, 1.0, ACCENT2)
    if build == "machined":
        p.raise_plate(chevrons(x0 + 47, y0 + 14, 9, n=1, up=True), 1.5, 0.7, ACCENT2)


def emblem_polys(svg_path):
    """The launcher's emblem: every path of emblem.svg as (colour, polygon) in its 128 unit box."""
    out = []
    for m in re.finditer(r'<path fill="#([0-9a-fA-F]{6})" d="([^"]+)"', open(svg_path, encoding="utf-8").read()):
        pts = [float(v) for v in re.findall(r"-?\d+(?:\.\d+)?", m.group(2))]
        out.append((tuple(int(m.group(1)[i:i + 2], 16) for i in (0, 2, 4)), list(zip(pts[0::2], pts[1::2]))))
    return out


def flat(size, shapes, scale=4):
    """Flat fills, drawn large and box filtered: shapes are (rgba, polygon) in texels."""
    w, h = size
    im = Image.new("RGBA", (w * scale, h * scale), (0, 0, 0, 0))
    for rgba, poly in shapes:
        layer = Image.new("RGBA", im.size, (0, 0, 0, 0))
        ImageDraw.Draw(layer).polygon([(x * scale, y * scale) for x, y in poly], fill=rgba)
        im.alpha_composite(layer)
    return im.resize((w, h), Image.BOX)


def logo(size, svg_path, pad=26, opacity=0.6, mute=0.45, grey=(96, 104, 120)):
    """The emblem the selection window shows when nothing is selected, every side: set well inside the
    window, its colours pulled toward the glass's grey and half see-through, so it reads as a watermark."""
    w, h = size
    e = h - 2 * pad
    k = e / 128.0
    dx, dy = (w - e) / 2, pad
    shapes = []
    for c, poly in emblem_polys(svg_path):
        c = tuple(int(a * (1 - mute) + g * mute) for a, g in zip(c, grey))
        shapes.append((c + (255,), [(dx + x * k, dy + y * k) for x, y in poly]))
    im = flat(size, shapes)                             # opaque first, so the layers do not show through
    im.putalpha(im.getchannel("A").point(lambda a: int(a * opacity)))
    return im


def promotion_banner(size):
    """The banner that drops onto the general's button at a promotion: a pale steel pennant whose
    point and chevrons aim down at the button."""
    w, h = size
    pale, rail, mark = (196, 210, 232, 150), (226, 234, 246, 175), (250, 252, 255, 235)
    body = [(30, 0), (w - 30, 0), (w - 30, h - 70), (w / 2, h - 2), (30, h - 70)]
    rails = [[(0, 0), (20, 0), (20, h - 110), (0, h - 90)], [(w - 20, 0), (w, 0), (w, h - 90), (w - 20, h - 110)]]
    shapes = [(pale, body)] + [(rail, r) for r in rails]
    for i in range(4):
        shapes += [(mark, c) for c in chevrons(w / 2, 90 + i * 172, 100, n=2, up=False)]
    return flat(size, shapes)


# ---------------------------------------------------------------------------------------------- pieces the code lays out

# The radar's frame (RadarFrameImage): nine pieces round wherever ControlBar::fitRadarToMap stands the
# map-shaped radar, so the left panel of the bar is empty.  The corners' sizes are the frame's thickness
# (ControlBarSchemeManager::radarFrameRect): 5 at the screen's left and bottom edges, 29 over the top
# and 45 down the right, which at the square map is exactly the old left panel, 0..372 x 724..1080.
# The edges and the glass stretch: the height is fixed at the scheme's radar block, never above the
# square, and the top and bottom grow along a long map, so they are drawn even along their length
# and every ornament sits in a corner.
RADAR_EDGE = (5, 29, 45, 5)                     # left, top, right, bottom
RADAR_INSIDE = 322                              # LeftHUD's square, 5..327 x 753..1075
RADAR_GLASS = 64                                # the centre piece, the glass, shrunk from the square


def radar_machined(p, o, w):
    x0, y0, x1, y1 = o
    p.silhouette([[(x0, y0), (x1 - 14, y0), (x1, y0 + 14), (x1, y1), (x0, y1)]], 4.0, "machined", screen=True)
    p.raise_plate([plate(x0, y0 + 8, x1 - 8, y1, tr=("cut", 10))], 7.0, 3.0, STEEL, screen=True)
    p.sink([plate(*grow(w, 3))], 6, 3)
    p.inlay([plate(x0, y0 + 13, x1 - 39, y0 + 15)], ACCENT, 0.4)               # along the top, into the corner
    p.inlay(chevrons(x1 - 28, y0 + 10, 12, n=2), ACCENT, 0.7)
    p.stud(x1 - 14.5, y0 + 15, 3.0)
    p.inlay([plate(x1 - 15.5, y0 + 22, x1 - 13.5, y1)], ACCENT, 0.4)           # hung from the stud, down the side


def radar_terraced(p, o, w):
    x0, y0, x1, y1 = o
    sl, m, d = p.silhouette([[(x0, y0), (x1 - 12, y0), (x1 - 12, y0 + 8), (x1, y0 + 8), (x1, y1), (x0, y1)]],
                            11.0, "terraced", STEEL, screen=True)
    p.mat[sl][m & (d < 6)] = DARK
    p.bump([plate(*grow(w, 5))], 2.0, 1.0)                                       # the heavy rim, square
    p.sink([plate(*grow(w, 1.2))], 8, 1.2)
    p.inlay([plate(x0, y0 + 8, x1 - 22, y0 + 11)], ACCENT, 0.3)                 # a painted band, into the corner
    p.inlay([plate(x1 - 11, y0 + 22, x1 - 8, y1)], ACCENT, 0.3)
    p.bump([plate(x1 - 40, y0 + 15, x1 - 24, y0 + 21.5)], 1.6, 0.8, ACCENT2)    # an ingot
    for x in (x1 - 41, x1 - 32, x1 - 23):
        p.rivet(x, y0 + 3, 2.0)
    p.rivet(x1 - 3, y0 + 13, 2.0)
    p.rivet(x1 - 3, y0 + 22, 2.0)


def radar_scavenged(p, o, w):
    x0, y0, x1, y1 = o
    outline = [(x0, y0 + 4), (x1 - 34, y0 + 4), (x1 - 25, y0), (x1 - 7, y0 + 2), (x1, y0 + 13), (x1, y1), (x0, y1)]
    p.silhouette([jitter(outline, 31, 2.2, 11)], 3.0, "scavenged", screen=True)
    # a sheet along the top and one down the side, out of square, welded on and bolted in the corner
    for k, ((a, b, c, e), deg, tint) in enumerate((((x0 - 8, y0 + 6, x1 - 32, y0 + 23), 0.35, 0.92),
                                                   ((x1 - 36, y0 + 15, x1 + 6, y1 + 8), -0.3, 1.06))):
        poly = sheet(a, b, c, e, deg)
        sl, m, _ = p.raise_plate([poly], 5.0 + k, 1.2, STEEL, screen=True)
        p.tint[sl][m] = tint
        top = sorted(poly, key=lambda q: q[1])[:2]
        p.weld(min(top), max(top), 130 + k)
    p.sink([plate(*grow(w, 2))], 5, 2)
    p.inlay([jitter(plate(x1 - 26, y0 + 26, x1 - 17, y1 + 4), 44, 2.5, 6)], ACCENT, 0.25)
    for i, (x, y) in enumerate(((x1 - 38, y0 + 12), (x1 - 30, y0 + 21), (x1 - 8, y0 + 19))):
        p.bolt(x, y, 2.8, 80 + i)


def radar_faceted(p, o, w):
    x0, y0, x1, y1 = o
    p.silhouette([[(x0, y0), (x1 - 26, y0), (x1, y0 + 15), (x1, y1), (x0, y1)]], 4.0, "machined", screen=True)
    p.frame([plate(x0, y0 + 6, x1 - 6, y1, tr=("slant", 12))], 9.0, "faceted", screen=True)
    p.sink([plate(*grow(w, 4))], 6, 4)
    p.facets(46, 0.06, 0.45, 23)
    p.inlay([plate(x0, y0 + 10, x1 - 46, y0 + 12), [(x1 - 46, y0 + 10), (x1 - 36, y0 + 16), (x1 - 36, y0 + 18), (x1 - 46, y0 + 12)]],
            ACCENT, 0.4)
    p.inlay([triangle(x1 - 22, y0 + 18, 9, 10, "left")], ACCENT, 0.6)
    p.inlay([plate(x1 - 15, y0 + 26, x1 - 13, y1)], ACCENT2, 0.4)
    p.bump([regular(x1 - 13.5, y0 + 18, 3.0, 6, np.pi / 6)], 0.8, 1.2, BRIGHT)


RADARS = {"machined": radar_machined, "terraced": radar_terraced, "scavenged": radar_scavenged, "faceted": radar_faceted}


def radar_frame(side_name):
    """The radar's frame round the square radar, 372x356, the picture radar_pieces cuts.  The left and
    bottom edges are the screen's and take no edge; a margin right and above keeps the others off the
    canvas's border."""
    side = SIDES[side_name]
    l, t, r, b = RADAR_EDGE
    W, H = l + RADAR_INSIDE + r, t + RADAR_INSIDE + b
    p = Painter(W + 8, H + 8)
    RADARS[side["build"]](p, (0, 8, W, H + 8), (l, 8 + t, l + RADAR_INSIDE, 8 + t + RADAR_INSIDE))
    return shade(p, side, shadow=False).crop((0, 8, W, H + 8))


def radar_pieces(frame, edge=RADAR_EDGE):
    """The nine pieces, by suffix, out of radar_frame's picture."""
    l, t, r, b = edge
    xs, ys = (0, l, l + RADAR_INSIDE, l + RADAR_INSIDE + r), (0, t, t + RADAR_INSIDE, t + RADAR_INSIDE + b)
    out = {}
    for i, name in enumerate(("TL", "T", "TR", "L", "C", "R", "BL", "B", "BR")):
        col, row = i % 3, i // 3
        out[name] = frame.crop((xs[col], ys[row], xs[col + 1], ys[row + 1]))
    out["C"] = out["C"].resize((RADAR_GLASS, RADAR_GLASS), Image.BOX)
    return out


# The general's powers tray (PowerTrayImage, ControlBar::arrangeSpecialPowerShortcutGrid): rows of three
# 80x70 cells against the screen's right edge, climbing from the general's tab, a short row stepping in.
# The code draws a cell under every power and the outline's pieces where the outline runs: an 8 texel
# edge over the top and down the inside, 6 under the foot, the two outer corners and the step where a
# short row's inside edge meets the top of the longer row under it.  The pieces are cut from one tray
# drawn whole (three cells under two), so they meet as they were drawn.  Cells tile, so nothing in a
# cell may reach its border but the plate; no falloff either.
TRAY_CELL, TRAY_LEFT, TRAY_TOP, TRAY_FOOT = (80, 70), 8, 8, 6
TRAY_CUTS = {"Cell": (100, 88, 180, 158), "EdgeTop": (100, 10, 180, 18), "EdgeLeft": (12, 88, 20, 158),
             "EdgeBottom": (100, 158, 180, 164), "CornerTopLeft": (12, 80, 20, 88), "CornerBottomLeft": (12, 158, 20, 164),
             "Step": (92, 80, 100, 88)}
# The production queue's tray (InGameUI.cpp, drawHudTray) is the powers tray mirrored, against the
# screen's left edge with its open side right; the superweapon tray stands against the right edge with
# its rows going down, so row 0's foot is the 8 texel EdgeTop, the far edges are the 6 texel EdgeBottom
# and its step sits at the top of the short row's inside edge.  Those trays reuse every powers piece
# that reads the same way and need five more (mirroredTrayPiece).  Each is cut from its own tray drawn
# whole in that shape and lit from the top left like everything else: a mirrored copy of the pixels
# would carry its light from the top right and its bevels would read inverted.
TRAY_MIRROR_CUTS = {"EdgeRight": (240, 88, 248, 158), "CornerTopRight": (240, 80, 248, 88),
                    "CornerBottomRight": (240, 158, 248, 164), "StepRight": (160, 80, 168, 88)}
TRAY_DOWN_CUTS = {"StepDown": (92, 86, 100, 92)}
TRAY_CORNERS = {"machined": [("cut", 5), ("cut", 5), ("cut", 4)], "terraced": [("notch", 3), ("notch", 3), ("notch", 2.5)],
                "scavenged": [("cut", 6), ("notch", 2.5), ("cut", 2.5)], "faceted": [("slant", 4), ("slant", 4), ("slant", 3)]}


def _tl(x, y, kind):
    return [(x + dx, y + dy) for dx, dy in reversed(corner_points(*kind))]


def _bl(x, y, kind):
    return [(x + dx, y - dy) for dx, dy in corner_points(*kind)]


def tray_outline(build, inset=0.0, layout="powers"):
    """A reference tray's outline, its outer corners in the side's cut and the step's inner corner
    square.  powers: screen edge at x 260, three cells under two.  production: the same mirrored, the
    screen edge at x 0.  superweapon: screen edge at x 260, three cells over two, 8 over the top and 6
    under each row's foot."""
    kinds = TRAY_CORNERS[build]
    if layout == "superweapon":
        i = inset
        return ([(260, 8 + i)] + _tl(12 + i, 8 + i, kinds[1]) + _bl(12 + i, 92 - i, kinds[2]) + [(92 + i, 92 - i)]
                + _bl(92 + i, 162 - i, kinds[2]) + [(260, 162 - i)])
    a, bx, cy, top, foot = 92 + inset, 12 + inset, 164 - inset, 10 + inset, 80 + inset
    pts = [(260, top)] + _tl(a, top, kinds[0]) + [(a, foot)] + _tl(bx, foot, kinds[1]) + _bl(bx, cy, kinds[2]) + [(260, cy)]
    return [(260 - x, y) for x, y in pts] if layout == "production" else pts


def tray_reference(side_name, layout="powers"):
    side = SIDES[side_name]
    build = side["build"]
    p = Painter(260, 172)
    if layout == "superweapon":
        cells = [(260 - 80 * (k + 1), 16, 260 - 80 * k, 86) for k in range(3)] + [(260 - 80 * (k + 1), 86, 260 - 80 * k, 156) for k in range(2)]
    else:
        cells = [(260 - 80 * (k + 1), 88, 260 - 80 * k, 158) for k in range(3)] + [(260 - 80 * (k + 1), 18, 260 - 80 * k, 88) for k in range(2)]
    mx = (lambda x: 260 - x) if layout == "production" else (lambda x: x)   # the powers tray's x, mirrored for the queue's
    if layout == "production":
        cells = [(260 - x1, y0, 260 - x0, y1) for x0, y0, x1, y1 in cells]

    def outline(inset=0.0):
        return tray_outline(build, inset, layout)
    if build == "machined":                         # a dark flange carrying the outline, the satin plate on it
        p.silhouette([outline()], 4.0, build, screen=True)
        p.raise_plate([outline(3)], 7.0, 2.0, STEEL, screen=True)
    elif build == "terraced":                       # two square terraces inside the edge
        sl, m, d = p.raise_plate([outline()], 8.0, 1.0, STEEL, "chessboard", terraces((0, 0.5), (3.5, 0.5)), screen=True)
        p.limit[sl] |= m
        p.mat[sl][m & (d < 3.5)] = DARK
    elif build == "scavenged":
        p.silhouette([jitter(outline(), 17, 1.4, 9)], 5.0, build, STEEL, screen=True)
    else:                                           # one wide flat facet for an edge
        p.silhouette([outline()], 7.0, build, STEEL, screen=True)
    for x0, y0, x1, y1 in cells:
        if build == "faceted":                      # each cell a tile of two triangles tilted their own ways
            ys, xs = np.mgrid[y0 * SS:y1 * SS, x0 * SS:x1 * SS] / SS
            u, v = (xs - x0) / 80.0, (ys - y0) / 70.0
            upper = u + v < 1
            p.h[y0 * SS:y1 * SS, x0 * SS:x1 * SS] += np.where(upper, 0.9 * u - 0.6 * v, -0.5 * u + 0.8 * v)
        if build == "terraced":
            p.bump([shape(build, x0 + 1, y0 + 2, x1 - 1, y1 - 2, 3)], 1.5, 0.8)
        p.sink([shape(build, x0 + 2, y0 + 4, x1 - 2, y1 - 4, 3)], 4.0, 1.5)
        if build == "faceted":                      # a groove along each tile's top and inside edge
            p.inlay([plate(x0, y0, x0 + 1, y1), plate(x0, y0, x1, y0 + 1)], INKM, 0.6)
    # what the edges carry, once per edge piece so each copy brings its own (the superweapon tray only
    # gives its step, which carries nothing)
    if layout == "superweapon":
        pass
    elif build == "terraced":
        for x, y in ((140, 13.5), (16, 123), (140, 161), (16, 84)):
            p.rivet(mx(x), y, 1.3)
    elif build == "scavenged":
        p.weld((mx(100), 15), (mx(180), 15), 61, 1.0)
        for i, (x, y) in enumerate(((122, 13.5), (16, 104), (16, 84))):
            p.bolt(mx(x), y, 1.7, 90 + i)
    elif build == "faceted":
        p.bump([regular(mx(16), 123, 2.2, 6, np.pi / 6)], 0.6, 1.0, BRIGHT)
    return shade(p, side, shadow=False, falloff=False)


def tray_pieces(side_name):
    """The powers tray's seven pieces and the queue's and superweapons' five, each from its own tray."""
    out = {}
    for layout, cuts in (("powers", TRAY_CUTS), ("production", TRAY_MIRROR_CUTS), ("superweapon", TRAY_DOWN_CUTS)):
        ref = tray_reference(side_name, layout)
        out.update({name: ref.crop(box) for name, box in cuts.items()})
    return out


# The plate under a production or superweapon cell's readout (InGameUI.cpp hudCellStyle), stretched to
# its text: flat glass with a one texel rim of dark steel.  The cells themselves stand in the trays.
HUD_PLATE = (24, 12)


# The "+N" tab a full tray wears (InGameUI.cpp hudTab, drawHudTab): a tag of its own pinned to the
# tray at the screen edge, not more tray plate.  TabDown hangs from the superweapon tray against the
# right edge, its rows 0..5 over the tray's EdgeBottom where the pin or hinge goes, the code's text in
# x 4..36 y 8..26; TabUp sticks up from the queue's tray against the left edge, rows 22..27 over
# EdgeTop, text in x 4..36 y 2..20.  TabUp is the same tag turned end for end and drawn again, so its
# light still falls from the top left.
TRAY_TAB, TRAY_TAB_JOIN = (40, 28), 6


def tray_tab(side_name, up=False):
    side = SIDES[side_name]
    build = side["build"]
    w, h = TRAY_TAB
    p = Painter(w + 8, h + 8)

    def T(poly):                                    # the down tab's coordinates, turned for the up one
        return [(4 + (w - x if up else x), 4 + (h - y if up else y)) for x, y in poly]

    def pt(x, y):
        return T([(x, y)])[0]
    if build == "machined":                         # a satin tag, 45 degree cuts, on a dark hinge strap
        p.silhouette([T([(5, 5), (35, 5), (35, 22), (30, 27), (10, 27), (5, 22)])], 4.0, build, STEEL)
        p.sink([T(plate(8, 9.5, 32, 24.5, bl=("cut", 3), br=("cut", 3)))], 2.2, 1.0)
        p.inlay([T(plate(13, 25.4, 27, 26.2))], ACCENT, 0.3)
        p.silhouette([T(plate(10, 0, 30, 10, br=("cut", 3), bl=("cut", 3)))], 7.0, build, DARK)
        for x in (14.5, 25.5):
            p.stud(*pt(x, 4.5), 2.4)
    elif build == "terraced":                       # a cast tag in two terraces, hung on a riveted hasp
        sl, m, d = p.raise_plate([T(shape(build, 4, 6, 36, 27, 3))], 6.0, 1.0, STEEL, "chessboard", terraces((0, 0.5), (2, 0.5)))
        p.limit[sl] |= m
        p.mat[sl][m & (d < 2)] = DARK
        p.sink([T(shape(build, 7.5, 10, 32.5, 23.5, 2))], 2.5, 0.8)
        p.inlay([T(plate(10, 24.6, 30, 25.6))], ACCENT, 0.3)
        p.silhouette([T(plate(8, 0, 32, 7.5))], 7.5, build, STEEL)
        for x in (12, 28):
            p.rivet(*pt(x, 3.8), 1.6)
    elif build == "scavenged":                      # a scrap of sheet bolted to a strap welded on, out of square
        body = rotated(plate(5, 6, 35, 27), 20, 16.5, 3.5)
        p.silhouette([T(jitter(body, 23, 0.9, 5))], 3.0, build, STEEL)
        p.sink([T(rotated(plate(8.5, 10, 31.5, 24), 20, 16.5, 3.5))], 2.0, 0.9)
        strap = rotated(plate(14, 0, 27, 9), 20.5, 4.5, -4)
        p.silhouette([T(strap)], 5.0, build, STEEL)
        p.weld(pt(14.5, 8.6), pt(26.5, 7.8), 77, 1.0)
        p.bolt(*pt(20.5, 3.6), 2.4, 5)
    else:                                           # a parallelogram of composite on a triangular bracket
        p.silhouette([T(shape(build, 4, 6, 36, 27, 5))], 5.0, "faceted", STEEL)
        p.sink([T(shape(build, 7.5, 10, 32.5, 24, 3))], 2.4, 1.0)
        p.inlay([T(triangle(33, 24.5, 3, 3.4, "left"))], ACCENT, 0.4)
        p.silhouette([T([(11, 0), (29, 0), (20, 9)])], 6.0, "faceted", STEEL)
        p.bump([T(regular(20, 3.2, 2.2, 6, np.pi / 6))], 0.8, 1.0, BRIGHT)
    return shade(p, side).crop((4, 4, w + 4, h + 4))


# A charged superweapon's light (InGameUI.cpp drawReadyGlow): stretched over the cameo, its foot on the
# foot of the owner's ring, drawn additively and multiplied by the owner's colour scaled by the breath (0.55 to
# 1).  So it is grey light on black, opaque: white along the bottom row, falling off on a square curve to
# black about 85% of the way up, faint rays rising through it (a few texels wide, unevenly spaced, each
# 20 to 30% brighter or darker) and a faint scanline every third row.  Each build seeds its own rays.
READY_GLOW = (72, 48)
READY_RAYS = {"machined": (11, 1.0, 1.8), "terraced": (23, 1.8, 2.6), "scavenged": (37, 1.2, 3.2), "faceted": (41, 1.4, 2.2)}


def ready_glow(build):
    w, h = READY_GLOW
    seed, lo, hi = READY_RAYS[build]
    rng = np.random.default_rng(seed)
    y = np.arange(h, dtype=np.float32)[:, None]
    x = np.arange(w, dtype=np.float32)[None, :]
    light = np.clip(1 - (h - 1 - y) / (0.85 * h), 0, 1) ** 2
    rays = np.zeros((1, w), np.float32)
    at = rng.uniform(4, 8)
    while at < w - 2:                               # uneven spacing, alternating brighter and darker
        rays += rng.choice((-1, 1)) * rng.uniform(0.2, 0.3) * np.exp(-0.5 * ((x - at) / rng.uniform(lo, hi)) ** 2)
        at += rng.uniform(6, 15)
    rise = np.clip((h - 1 - y) / 4.0, 0, 1)        # the rays leave the bottom row white
    glow = light * (1 + rays * rise) * np.where((h - 1 - y) % 3 == 2, 0.94, 1.0)
    glow[-1] = 1.0
    return grey_light(glow)


def grey_light(v):
    v = (np.clip(v, 0, 1) * 255 + 0.5).astype(np.uint8)
    return Image.fromarray(np.dstack([v, v, v, np.full_like(v, 255)]), "RGBA")


def ready_shimmer(build):
    """Variant B's second piece: a band of light, brightest along its top edge and trailing off below it
    over 18 rows, the rest black.  It is drawn over the glow, additive and in the owner's colour like
    it, its top edge climbing from the cameo's foot to 85% of the cameo once a breath and fading as it
    goes (shimmer_rise), as InGameUI.cpp drawReadyGlow draws it."""
    w, h = READY_GLOW
    seed, lo, hi = READY_RAYS[build]
    rng = np.random.default_rng(seed + 1)
    y = np.arange(h, dtype=np.float32)[:, None]
    x = np.arange(w, dtype=np.float32)[None, :]
    band = np.where(y < 2, (y + 1) / 3, np.clip(1 - (y - 2) / 18, 0, 1) ** 1.6) * 0.8
    grain = np.zeros((1, w), np.float32)
    for at in np.cumsum(rng.uniform(5, 13, 12)):
        grain += rng.uniform(-0.25, 0.25) * np.exp(-0.5 * ((x - at) / rng.uniform(lo, hi)) ** 2)
    return grey_light(band * (1 + grain))


def shimmer_rise(phase):
    """Where variant B's band stands at a phase of the breath (0..1): its top edge as a share of the
    cameo's height from its foot, and how bright it is there."""
    return 0.85 * phase, (1 - phase) ** 0.7


# The owner picked B (the breath and a rising band) over A (the breath alone) from the round 8
# prototype; B is the default and ships.  Every sheet's INI maps both pieces at these places, and A
# stays selectable with --ready-glow A, which leaves the shimmer's rect unpainted.
READY_GLOW_AT = {"HudReadyGlow": (548, 496, 620, 544), "HudReadyShimmer": (632, 496, 704, 544)}


def ready_glow_pieces(build, variant):
    out ={"HudReadyGlow": ready_glow(build)}
    if variant == "B":
        out["HudReadyShimmer"] = ready_shimmer(build)
    return out


def hud_cell_pieces(side_name):
    side = SIDES[side_name]
    pw, ph = HUD_PLATE
    rim = tuple(int(v * 0.5) for v in side["steel"])
    plate_img = Image.new("RGBA", (pw, ph), rim + (255,))
    plate_img.paste(Image.new("RGBA", (pw - 2, ph - 2), tuple(side["glass"]) + (255,)), (1, 1))
    return {"Plate": plate_img}


def put(sheet_img, img, box):
    """img into its MappedImage box with a one texel gutter of its own edge round it, so a stretched
    piece filters against itself and not its neighbour."""
    x0, y0, x1, y1 = box
    assert img.size == (x1 - x0, y1 - y0), "piece %s is %s, its box %s" % (box, img.size, (x1 - x0, y1 - y0))
    g = np.pad(np.asarray(img.convert("RGBA")), ((1, 1), (1, 1), (0, 0)), mode="edge")
    sheet_img.paste(Image.fromarray(g), (x0 - 1, y0 - 1))


# Minimise and promotion stand on the bar's mount (round 18) and wear the bar's colour: a raised face
# in the side's second accent of BAR_LOOKS and a flat glyph on it, as the prototypes drew them.  The
# glyph is laid on after the light, antialiased: engraved and lit at this size it came out mottled.
MOUNT_KINDS = ("General", "Minimize", "Maximize")
MOUNT_GLYPH = {"America": INK, "China": INK, "Gla": INK, "Turkiye": (64, 226, 214)}
MOUNT_GLYPH_BUILD = {"terraced": "machined"}                    # China's minimise is the chevron and its bezel plain
MOUNT_STATE = {"": 1.0, "H": 1.3, "P": 0.85, "I": 0.45}         # the face's brightness in each state


def mount_button(side_name, kind, state, w, h):
    look = dict(BAR_LOOKS[side_name])
    build = look["build"]
    k = MOUNT_STATE[state]
    look["accent"] = tuple(min(255, int(c * k)) for c in look["accent2"])
    o = 32                                                      # drawn on a wider canvas: shade's noise
    p = Painter(w + 2 * o, h + 2 * o)                           # fields go bad on one 31 texels high
    # China's terraces would draw a ridge across the face; its notched corners stay
    p.frame([shape(build, o, o, o + w, o + h, 5)], 3.5, MOUNT_GLYPH_BUILD.get(build, build), DARK, BEZEL_SCALE)
    p.bump([shape(build, o + 3, o + 3, o + w - 3, o + h - 3, 3)], 1.0 if state == "P" else 2.2, 1.4, ACCENT)
    img = shade(p, look, shadow=False, falloff=False).crop((o, o, o + w, o + h))
    off = 1 if state == "P" else 0
    raised, ink, *again = icon(kind, w / 2 + off, h / 2 + off, MOUNT_GLYPH_BUILD.get(build, build))
    big = 4
    mask = Image.new("L", (w * big, h * big), 0)
    d = ImageDraw.Draw(mask)
    for polys, fill in ((raised, 255), (ink, 0), ([q for polys in again for q in polys], 255)):
        for q in polys:
            d.polygon([(x * big, y * big) for x, y in q], fill=fill)
    mask = mask.resize((w, h), Image.BOX)
    colour = (96, 100, 104) if state == "I" else MOUNT_GLYPH[side_name]
    glyph = Image.new("RGBA", (w, h), colour + (255,))
    glyph.putalpha(mask)
    img.alpha_composite(glyph)
    return img


def paint_powers(side_name="America", svg_path=EMBLEM_SVG, frame=None, glow="B"):
    """The side's button sheet; frame is its radar_frame when the caller already drew it."""
    side = SIDES[side_name]
    build = side["build"]
    M = read_mapped(side_name)
    prefix = side_name + "Pro"
    p = Painter(1024, 1024)
    promo_window(p, *M[prefix + "GeneralsPowerWindow"][:2], build)
    for state in ("", "H", "I"):                                # the window's close button, 60x50
        button(p, M[prefix + "GenPowerClose" + state], "Close", state, build)
    for kind in ("Chat", "Options", "Worker", "Beacon", "IdleInfantry", "IdleVehicle", "IdleAircraft"):
        for state in ("", "H", "P", "I"):
            button(p, M[prefix + kind + state], kind, state, build)
    exp_bar(p, *M[prefix + "ExpBar"][:2], build)
    shortcut_tray(p, *M[prefix + "TraySmall"][:2], build)
    hover_bracket(p, *M[prefix + "UAttackH"][:2], build)
    p.limit[:] = True                                           # the sheet's pieces are not one silhouette
    sheet_img = shade(p, side, shadow=False)
    for name, draw in (("Logo", lambda s: logo(s, svg_path)), ("LevelUP", promotion_banner)):
        x0, y0, x1, y1 = M[prefix + name]
        if name == "Logo" and not os.path.exists(svg_path):
            continue                                            # the launcher is not beside this checkout
        sheet_img.paste(draw((x1 - x0, y1 - y0)), (x0, y0))
    pieces = {"Radar" + k: v for k, v in radar_pieces(frame or radar_frame(side_name)).items()}
    pieces.update({"Tray" + k: v for k, v in tray_pieces(side_name).items()})
    pieces.update({"HudCell" + k: v for k, v in hud_cell_pieces(side_name).items()})
    pieces.update({"TrayTabDown": tray_tab(side_name), "TrayTabUp": tray_tab(side_name, up=True)})
    pieces.update(ready_glow_pieces(build, glow))
    pieces.update(tip_pieces(side_name))
    for kind in MOUNT_KINDS:
        for state in ("", "H", "P", "I"):
            x0, y0, x1, y1 = M[prefix + kind + state]
            pieces[kind + state] = mount_button(side_name, kind, state, x1 - x0, y1 - y0)
    for name, img in pieces.items():
        put(sheet_img, img, M.get(prefix + name, READY_GLOW_AT.get(name)))
    return sheet_img


# ---------------------------------------------------------------------------------------------- the tooltips

# Approved by the owner on 2026-11-03 (round 17).  Two pieces of nine each and nine icons, the code
# looking them up by the scheme's TooltipImage prefix: <prefix>Tip<TL..BR>, <prefix>CursorTip<TL..BR> and
# <prefix>TipIcon<Kind>.  The frames are cut like the radar's and stretched by the code the same way, so
# whatever runs along an edge is even along its length and every fitting sits in a corner, mostly top right.
#   Tip        the build card (ControlBarPopupDescription.wnd), hung on the screen's left edge, its left
#              side open like the radar's.  Its top row holds the name slot, the stat row's well, the low
#              rib between them and the description well's top lip, so the card grows by stretching the
#              middle row alone.  TIP_TEXT is where the card's texts stand, from its top left (a right or
#              bottom edge below zero is from the card's right or bottom): the name left in the slot, the
#              price right of it (money icon and figure, clock and build time), the unit's figures as icon
#              and value cells in the second row, the description under them.
#   CursorTip  the pointer's tooltip (Mouse::drawTooltip): a small framed well, closed all round, the
#              text inside it with TIP_CURSOR_PAD of glass round it.
#   TipIcon    the stat icons, TIP_ICON square, one set of glyphs lit in each side's steel.
# Each side draws all three in its own build on its Powers sheet; the observer's console build is the
# default for the shell and any scheme without its own (prefix Helpbox, on ReforgedHudHelpbox).  Where
# each piece goes is its sheet's MappedImages INI.
TIP_EDGE = (16, 68, 48, 14)                     # left, top, right, bottom: the card's corners
TIP_REF = (192, 146)                            # the card drawn once, its middle 128 wide and 64 high
TIP_TEXT = dict(name=(12, 5, -190, 27), cost=(-186, 5, -46, 27), stats=(8, 31, -18, 57), description=(8, 66, -18, -14))
# a card with no stat row (a general's power, an upgrade) takes TipHeadTL/T/TR, TIP_HEAD high, for its top row:
# the name slot and the description well's top lip, the description from TIP_TEXT_HEAD's top
TIP_HEAD = 38
TIP_TEXT_HEAD = dict(TIP_TEXT, stats=None, description=(8, 36, -18, -14))
TIP_ICON = 16                                   # every stat icon, at 1080
TIP_ICONS = ("Money", "Time", "Health", "Armor", "Damage", "Rate", "Range", "Speed", "Power")
TIP_CURSOR_EDGE, TIP_CURSOR_REF, TIP_CURSOR_PAD = 8, (48, 32), 6
TIP_HELPBOX_SIZE = (512, 256)


def tip_wells(W, H, head=False):
    """The name slot, the stat row's well and the description well, in the reference card; the short
    head (TipHead) has no stat well, the description well right under the name slot."""
    if head:
        return (6, 5, W - 40, 27), None, (4, 31, W - 14, H - 8)
    return (6, 5, W - 40, 27), (4, 31, W - 14, 57), (4, 61, W - 14, H - 8)


def tip_soften(p, W, H, rib, head=False):
    """The breaks inside the card made quieter (round 16, the owner's "less conspicuous"): the wells run
    together under the name slot and between cost and description, and a low dark rib rib texels high
    parts them where the steel bar and its two lips stood.  The wells' ends keep their lips, in the corners."""
    wells = [w for w in tip_wells(W, H, head) if w]
    floor = p.h[(wells[-1][1] + 12) * SS, 100 * SS]
    for a, b in zip(wells, wells[1:]):
        x0, x1, y0, y1 = max(a[0], b[0]) + 4, min(a[2], b[2]) - 4, a[3] - 4, b[1] + 4
        sl = (slice(int(y0 * SS), int(y1 * SS)), slice(int(x0 * SS), int(x1 * SS)))
        p.h[sl], p.mat[sl] = floor, GLASS
        mid = (a[3] + b[1]) / 2
        p.bump([plate(x0, mid - 1, x1, mid + 1)], rib, 0.8, DARK)


def tip_card(side, head=False):
    """The reference card, TIP_REF, every build its own way; side is a BAR_LOOKS name (Observer for the
    console set).  head draws the short variant for a card with no stat row (a general's power, an
    upgrade): the name slot, then the description well straight under it, TIP_HEAD high, with only the
    fittings that fit beside the name; TipHeadTL/T/TR are its top row and join the same middle and bottom
    pieces."""
    look = BAR_LOOKS[side]
    build = look.get("skin", look["build"])
    W, H = (TIP_REF[0], TIP_HEAD + TIP_REF[1] - TIP_EDGE[1]) if head else TIP_REF
    p = Painter(W + 4, H + 4)                       # room right and below, so only the left side is the screen's
    name, cost, desc = tip_wells(W, H, head)
    full = not head                                # the stat well's rule and the fittings below the name slot

    def sink(corners, depth, lip):
        for r, c in zip((name, cost, desc), corners):
            if r:
                p.sink([shape(build, *r, c)], depth, lip(r) if callable(lip) else lip)
    # round 19: round 18's three tiers in a card's few texels.  The shell is 2.6 texels of near-black steel
    # round the outline, open along the screen's edge on the left, the lip bevels up inside it, and the
    # wells are sunk through the lip as the bar's windows are; each side's outline and fittings are its
    # bar's.  Everything that is not plain plate stands in the top right corner, the right column or along
    # an edge, since TL, TR, BL and BR keep their size and the rest stretches.
    p.tiers([CARD_OUTLINES[build](W, H)], (False, False, True, False), 4.0, 7.0, 2.6, 1.6, 2.6)
    sink((3, 4, 4), 6, 2.2)
    tip_soften(p, W, H, 1.5, head)
    if full:
        p.inlay([plate(8, 58.6, W - 44, 59.4)], INKM, 0.4)
    if build == "machined":                         # America: swept blue chevrons, a panel seam, octagon studs
        def swept(x, y0, y1, w, t):
            ym = (y0 + y1) / 2
            return [(x, y0), (x - t, y0), (x - t - w, ym), (x - t, y1), (x, y1), (x - w, ym)]
        p.inlay([swept(W - 18 - 9 * i, 9, 22, 6, 2.6) for i in range(3)], ACCENT, 0.7)
        p.inlay([plate(W - 7, 24, W - 6, H - 16)], INKM, 0.8)
        if full:
            p.stud(W - 9, 44, 2.6)
        p.stud(W - 9, H - 9, 2.4)
    elif build == "terraced":                       # China: a stepped corner, a red lacquer band, gold studs, rivets
        p.inlay([plate(8, H - 3.6, W - 18, H - 2.2), plate(W - 36, 8, W - 14, 11)], ACCENT, 0.3)
        p.bump([regular(x, 19, 2.2, 12) for x in (W - 33, W - 17)], 2.2, 1.4, GOLDM)
        for y in (34, 46) if full else ():
            p.rivet(W - 7, y, 1.8)
        p.rivet(W - 7, H - 8, 1.8)
    elif build == "scavenged":                      # the GLA: a patch sheet welded on crooked, odd bolts
        sh = sheet(W - 38, 3, W - 12, 26, -6)
        p.bump([sh], 1.4, 0.8, STEEL)
        for a, b in zip(sh, sh[1:] + sh[:1]):
            p.weld(a, b, seed=150 + int(a[0]), r=1.0)
        p.bolt(W - 25, 14.5, 2.2, 91)
        if full:
            p.bolt(W - 7, 44, 2.6, 92)
        p.bolt(W - 9, H - 6, 2.0, 93)
    elif build == "faceted":                        # Turkiye: an armour panel with a gap, range ticks, recessed fasteners
        p.bump([[(W - 52, 4), (W - 30, 4), (W - 14, 13), (W - 52, 13)]], 2.0, 0.9, STEEL)
        p.inlay([plate(W - 53, 4, W - 52, 13)], INKM, 0.9)
        p.inlay([plate(x, 23, x + 1, 26 if i % 2 else 28) for i, x in enumerate(range(W - 38, W - 13, 5))], INKM, 0.6)
        for x, y in ((W - 8, 44), (W - 8, H - 8)) if full else ((W - 8, H - 8),):
            p.inlay([regular(x, y, 2.6, 12)], DARK, 1.0)
            p.bump([regular(x, y, 1.4, 6)], 0.6, 0.6, STEEL)
    else:                                           # the observer's desk: a grille, the tally, cross-head screws
        grille(p, W - 37, 7, W - 17, 22, 3.2)
        tally(p, W - 9, 14, 2.2)
        if full:
            screw(p, W - 8, 44, 2.0)
        screw(p, W - 8, H - 8, 1.8)
    img = bar_finish(shade(p, look, shadow=False, falloff=False), p, look, scan=False, gloss=False)
    if build == "faceted":                          # the LED line along the top lip and a lamp, Turkiye's turquoise
        img = led(img, [((16, 3), (W - 60, 3))], [(W - 22, 33 if full else 18, TURQ)])
    return img.crop((0, 0, W, H))


# each build's card outline, the bar's silhouette at a card's size; the left side is the screen's
CARD_OUTLINES = {
    "machined": lambda W, H: [(0, 0), (W - 16, 0), (W, 16), (W, H - 6), (W - 6, H), (0, H)],
    "terraced": lambda W, H: [(0, 0), (W - 20, 0), (W - 20, 6), (W - 10, 6), (W - 10, 12), (W, 12), (W, H - 6),
                              (W - 6, H - 6), (W - 6, H), (0, H)],
    "scavenged": lambda W, H: [(0, 0), (W - 30, 0), (W - 22, 3), (W - 9, 1.5), (W, 13), (W, H - 9), (W - 4, H - 9),
                               (W - 4, H), (0, H)],
    "faceted": lambda W, H: [(0, 0), (W - 34, 0), (W, 20), (W, H - 5), (W - 5, H), (0, H)],
    "desk": lambda W, H: [(0, 0), (W - 10, 0), (W, 10), (W, H - 5), (W - 5, H), (0, H)],
}


def tip_cursor(side):
    """The pointer's tooltip, TIP_CURSOR_REF: a framed well closed all round, three tiers in eight texels
    (a 1.6 texel shell, the lip, the glass), the build's corners and one fitting of its bar's."""
    look = BAR_LOOKS[side]
    build = look.get("skin", look["build"])
    W, H = TIP_CURSOR_REF
    p = Painter(W + 4, H + 4)
    o = 2                                           # off the canvas's border, so no side reads as the screen's
    c = {"machined": 4, "terraced": 3, "scavenged": 4, "faceted": 4, "desk": 3}[build]
    p.tiers([shape(build, o, o, o + W, o + H, c)], (False,) * 4, 3.0, 5.0, 1.6, 1.2, 1.6)
    p.sink([shape(build, o + 3, o + 3, o + W - 3, o + H - 3, max(c - 2, 1.5))], 2.6, 1.4)
    if build == "machined":
        p.inlay([plate(o + 6, o + 1.9, o + W - 6, o + 2.5)], ACCENT, 0.15)
    elif build == "terraced":
        p.inlay([plate(o + 6, o + H - 2.5, o + W - 6, o + H - 1.9)], ACCENT, 0.15)
    elif build == "scavenged":
        p.bolt(o + W - 3.6, o + 3.6, 1.6, 3)
    elif build == "desk":
        tally(p, o + W - 4, o + 4, 1.2)
    img = bar_finish(shade(p, look, shadow=False, falloff=False), p, look, scan=False, gloss=False)
    if build == "faceted":
        img = led(img, [((o + 6, o + 2), (o + W - 6, o + 2))], [])
    return img.crop((o, o, o + W, o + H))


def tip_icon(side, kind):
    """A stat icon, TIP_ICON square: the glyph raised in bright steel on a dark rim a texel and a half wide,
    so it reads on the card's glass and on bright ground alike.  The same glyphs on every side, lit in the
    side's own steel with an even grain.  No cross, no flag, nothing a nation or the Red Cross owns."""
    n, o = TIP_ICON, 2
    p = Painter(n + 2 * o, n + 2 * o)
    im = Image.new("L", (p.h.shape[1], p.h.shape[0]), 0)
    d = ImageDraw.Draw(im)

    def P(x, y):
        return ((x + o) * SS, (y + o) * SS)

    def poly(pts):
        d.polygon([P(x, y) for x, y in pts], fill=255)

    def line(pts, w):
        d.line([P(x, y) for x, y in pts], fill=255, width=int(w * SS), joint="curve")

    def ring(x0, y0, x1, y1, w):
        d.ellipse([P(x0, y0), P(x1, y1)], outline=255, width=int(w * SS))

    if kind == "Money":                             # the money window's own sign
        from PIL import ImageFont
        f = ImageFont.truetype(os.path.join(os.environ.get("WINDIR", "C:/Windows"), "Fonts", "arialbd.ttf"), 17 * SS)
        d.text(P(8, 8.4), "$", font=f, fill=255, anchor="mm")
    elif kind == "Time":                            # a clock face, two hands
        ring(1, 1, 15, 15, 2.2)
        line([(8, 3.8), (8, 8), (11.6, 8)], 2.0)
    elif kind == "Health":                          # a plus in steel, never red
        poly(plate(5.8, 1.5, 10.2, 14.5))
        poly(plate(1.5, 5.8, 14.5, 10.2))
    elif kind == "Armor":                           # a plain plate shield
        poly([(2, 1.5), (14, 1.5), (14, 8), (8, 15), (2, 8)])
    elif kind == "Damage":                          # a crosshair
        ring(2.2, 2.2, 13.8, 13.8, 1.9)
        for a, b in (((8, 0), (8, 5)), ((8, 11), (8, 16)), ((0, 8), (5, 8)), ((11, 8), (16, 8))):
            line([a, b], 1.9)
        d.ellipse([P(6.8, 6.8), P(9.2, 9.2)], fill=255)
    elif kind == "Rate":                            # three rounds side by side, damage a second
        for x in (1.5, 6.5, 11.5):
            poly([(x, 6), (x + 1.5, 2), (x + 3, 6), (x + 3, 14.5), (x, 14.5)])
    elif kind == "Range":                           # a span with two heads
        line([(3, 8), (13, 8)], 2.0)
        poly([(0.5, 8), (5, 3.5), (5, 12.5)])
        poly([(15.5, 8), (11, 3.5), (11, 12.5)])
    elif kind == "Speed":                           # two chevrons going right
        line([(2, 2.5), (7.5, 8), (2, 13.5)], 2.6)
        line([(8, 2.5), (13.5, 8), (8, 13.5)], 2.6)
    elif kind == "Power":                           # a bolt
        poly([(9.5, 0.5), (3, 9), (7.5, 9), (5.5, 15.5), (13, 6.5), (8.5, 6.5), (11, 0.5)])
    glyph = np.asarray(im) > 127
    rim = ndimage.binary_dilation(glyph, iterations=int(1.5 * SS))
    p.alpha |= rim
    p.limit |= rim
    p.mat[rim] = DARK
    p.h += rim * 0.6
    p.h += np.clip(Painter.inside(glyph) / 0.9, 0, 1) * glyph * 2.2
    p.mat[glyph] = BRIGHT
    look = BAR_LOOKS[side]                          # the bar's steel, lifted to read on the card's glass
    look = dict(look, build="desk", steel=tuple(min(255, int(c * 1.45)) for c in look["steel"]))
    return shade(p, look, shadow=False, falloff=False).crop((o, o, o + n, o + n))


def nine(img, edge, middle=None):
    """img cut into TL, T, TR, L, C, R, BL, B, BR at edge (left, top, right, bottom); C resized to middle."""
    l, t, r, b = edge
    W, H = img.size
    xs, ys = (0, l, W - r, W), (0, t, H - b, H)
    out = {}
    for i, name in enumerate(("TL", "T", "TR", "L", "C", "R", "BL", "B", "BR")):
        col, row = i % 3, i // 3
        out[name] = img.crop((xs[col], ys[row], xs[col + 1], ys[row + 1]))
    if middle:
        out["C"] = out["C"].resize(middle, Image.BOX)
    return out


def tip_pieces(side):
    """Both tooltips' pieces by their name without the side's prefix (TipTL, ..., CursorTipBR)."""
    e = TIP_CURSOR_EDGE
    l, _, r, b = TIP_EDGE
    out = {"Tip" + k: v for k, v in nine(tip_card(side), TIP_EDGE, (64, 64)).items()}
    out.update({"TipHead" + k: v for k, v in nine(tip_card(side, head=True), (l, TIP_HEAD, r, b)).items()
                if k in ("TL", "T", "TR")})
    out.update({"CursorTip" + k: v for k, v in nine(tip_cursor(side), (e, e, e, e), (16, 16)).items()})
    out.update({"TipIcon" + k: tip_icon(side, k) for k in TIP_ICONS})
    return out


def tip_sizes():
    """Each tooltip piece's size, by its name without the prefix, as the code assumes it."""
    l, t, r, b = TIP_EDGE
    e, n = TIP_CURSOR_EDGE, TIP_ICON
    cw, ch = TIP_CURSOR_REF[0] - 2 * e, TIP_CURSOR_REF[1] - 2 * e
    mw, mh = TIP_REF[0] - l - r, TIP_REF[1] - t - b
    out = {"TipTL": (l, t), "TipT": (mw, t), "TipTR": (r, t), "TipL": (l, mh), "TipC": (64, 64), "TipR": (r, mh),
           "TipBL": (l, b), "TipB": (mw, b), "TipBR": (r, b),
           "TipHeadTL": (l, TIP_HEAD), "TipHeadT": (mw, TIP_HEAD), "TipHeadTR": (r, TIP_HEAD),
           "CursorTipTL": (e, e), "CursorTipT": (cw, e), "CursorTipTR": (e, e), "CursorTipL": (e, ch),
           "CursorTipC": (16, 16), "CursorTipR": (e, ch), "CursorTipBL": (e, e), "CursorTipB": (cw, e), "CursorTipBR": (e, e)}
    out.update({"TipIcon" + k: (n, n) for k in TIP_ICONS})
    return out


def paint_helpbox():
    """ReforgedHudHelpbox in the observer's desk build: the tooltips for the shell and any scheme without its
    own, laid out by ReforgedHudHelpbox.ini (prefix Helpbox)."""
    M = read_mapped(None, name="ReforgedHudHelpbox")
    sheet_img = Image.new("RGBA", TIP_HELPBOX_SIZE, (0, 0, 0, 0))
    for name, img in tip_pieces("Observer").items():
        put(sheet_img, img, M["Helpbox" + name])
    return sheet_img


def check_tip_head(sheet_img, M, pre):
    """TipHead's bottom row is the description well's top, as TipTL/T/TR's is, so either top row meets the
    same middle row (TipL, TipC, TipR): the head's foot no further off the middle's top than the tall top
    row's own foot is, give or take a few levels (the middle glass is shrunk, so T is held to C by mean)."""
    lum = np.asarray(sheet_img.convert("L"), np.float32)

    def row(n, last):
        x0, y0, x1, y1 = M[pre + n]
        return lum[y1 - 1 if last else y0, x0:x1]
    for k, mid in (("TL", "L"), ("T", "C"), ("TR", "R")):
        top = row("Tip" + mid, False)
        if k == "T":
            off = [abs(row(n, True).mean() - top.mean()) for n in ("Tip" + k, "TipHead" + k)]
        else:
            off = [np.abs(row(n, True) - top).mean() for n in ("Tip" + k, "TipHead" + k)]
        assert off[1] <= off[0] + 4, "%sTipHead%s meets %sTip%s %.1f levels off, %sTip%s %.1f" % (
            pre, k, pre, mid, off[1], pre, k, off[0])


def check_helpbox(sheet_img):
    """Every tooltip piece on ReforgedHudHelpbox at its size, none overlapping another (gutter included)."""
    M = read_mapped(None, name="ReforgedHudHelpbox")
    assert sheet_img.size == TIP_HELPBOX_SIZE
    for n, wh in tip_sizes().items():
        x0, y0, x1, y1 = M["Helpbox" + n]
        assert (x1 - x0, y1 - y0) == wh, "Helpbox%s is %s, the code wants %s" % (n, (x1 - x0, y1 - y0), wh)
    boxes = [(n, grow(M[n], 1)) for n in ("Helpbox" + k for k in tip_sizes())]
    for i, (n, p) in enumerate(boxes):
        for m, q in boxes[i + 1:]:
            assert p[2] <= q[0] or q[2] <= p[0] or p[3] <= q[1] or q[3] <= p[1], "%s and %s overlap" % (n, m)
    check_tip_head(sheet_img, M, "Helpbox")


# ---------------------------------------------------------------------------------------------- the Tab screen

# Round 19 (2026-11-05): the Tab screen (Diplomacy.wnd) in the bar's three tiers, one painting per side and
# the observer's (<prefix>TabPanel, the TooltipImage prefix's, on ReforgedHud<Side>TabPanel), stretched by
# the window's default draw over Diplomacy.wnd:Parent, which Diplomacy.cpp stands at the screen's top left
# corner when the scheme has one.  The layout is fitted (one scale, the smaller, 1.8 at 1080) and anchored
# top left, so at 1080 the window is 1379x714 and every child sits at int(u * 1.8) - 7 of its 800x600
# number u: the column titles' band, eight player rows 57.6 apart, the Main Menu button bottom right.  The
# box is square on the screen's top and left edges and cut at its one corner toward the field; under the
# lip the titles and the rows stand in scanline wells, the Main Menu button in a socket of its own.
TAB_PANEL = (1379, 714)
TAB_SHEET = (2048, 1024)
TAB_ZEBRA = True                                # round 19's B: the rows' bands alternate (A: plain glass)


def tab_u(u):
    """A Diplomacy.wnd number in the window at 1080: fitted by 1.8, less the window's own 7."""
    return int(u * 1.8) - 7


TAB_TITLES = (16, tab_u(58) - 4, 1362, tab_u(88) + 3)
TAB_ROWS = [(tab_u(94 + 32 * i), tab_u(118 + 32 * i)) for i in range(8)]
TAB_TABLE = (16, TAB_ROWS[0][0] - 6, 1362, TAB_ROWS[-1][1] + 6)
# the Main Menu button, which Diplomacy.wnd puts hard in the bottom right corner (1200, 659), is moved by
# Diplomacy.cpp to TAB_HIDE's top left (as a share of the window) so the corner's cut clears its socket
TAB_HIDE = (1160, 650, 1160 + tab_u(763) - tab_u(671), 650 + tab_u(397) - tab_u(370))
TAB_COLUMNS = [tab_u(u) for u in (46, 178, 319, 364, 408, 460, 560)]   # between mute, player, side, ping, fps, team, status, money


def tab_panel(side, solo=False):
    """The Tab screen's painting on its sheet, the buttons beside it; solo is the campaign's (<prefix>TabSolo on
    ReforgedHud<Side>TabSolo), where the briefing list stands: the titles' and the table's wells are one, with
    no bands or rulings for its lines to fall across, and its sheet holds the painting alone."""
    look = BAR_LOOKS[side]
    build = look.get("skin", look["build"])
    W, H = TAB_PANEL
    p = Painter(W + 9, H + 6)
    kind = BUILDS[build]["corner"]
    corner = {"machined": ("cut", 40), "terraced": ("notch", 22), "scavenged": ("cut", 34), "faceted": ("slant", 20),
              "desk": ("cut", 26)}[build]
    outline = plate(0, 0, W, H, br=corner)
    if build == "scavenged":                        # ragged toward the field, the top kept on the screen's edge
        outline = [(x, 0.0 if y < 3.2 else y) for x, y in jitter(outline, 23, 3.0, 16)]
    if build == "terraced":                         # a second step inside the first
        outline = [(0, 0), (W, 0), (W, H - 34), (W - 12, H - 34), (W - 12, H - 12), (W - 34, H - 12), (W - 34, H), (0, H)]
    p.tiers([outline], (True, False, True, False))
    floor_r = {"machined": ("cut", 30), "terraced": ("notch", 10), "scavenged": ("cut", 26), "faceted": ("slant", 20),
               "desk": ("cut", 20)}[build]
    p.recess([plate(10, 10, W - 11, H - 13, br=floor_r) if build != "terraced" else
              [(10, 10), (W - 11, 10), (W - 11, H - 44), (W - 22, H - 44), (W - 22, H - 22), (W - 44, H - 22), (W - 44, H - 13),
               (10, H - 13)]], (True, False, True, False))
    c = 6 if kind != "rough" else 5
    if solo:
        p.sink([shape(build, TAB_TITLES[0], TAB_TITLES[1], TAB_TITLES[2], TAB_TABLE[3], c)], 6, 2.5)
    else:
        p.sink([shape(build, *TAB_TITLES, c)], 6, 2.5)
        p.sink([shape(build, *TAB_TABLE, c)], 6, 2.5)
        # the table's rulings: a hairline between rows and down the columns, faint in the glass
        rule = [plate(TAB_TABLE[0] + 8, (a[1] + b[0]) / 2, TAB_TABLE[2] - 8, (a[1] + b[0]) / 2 + 1) for a, b in zip(TAB_ROWS, TAB_ROWS[1:])]
        rule += [plate(x, TAB_TITLES[1] + 6, x + 1, TAB_TITLES[3] - 6) for x in TAB_COLUMNS]
        p.inlay(rule, INKM, 0.3)
    p.sink([shape(build, *grow(TAB_HIDE, 5), c)], 5, 2)
    top, foot = (12, TAB_TITLES[1] - 6), (TAB_TABLE[3] + 6, H - 15)    # the plain strips above and below the wells
    tab_fittings(p, build, W, H, top, foot)
    img = bar_finish(shade(p, look), p, look)
    if TAB_ZEBRA and not solo:                      # every other row's band of glass a shade lighter
        a = np.asarray(img).astype(np.float32)
        for y0, y1 in TAB_ROWS[1::2]:
            band = a[y0 - 3:y1 + 4, TAB_TABLE[0] + 6:TAB_TABLE[2] - 6, :3]
            band[:] = np.clip(band * 1.18 + 3, 0, 255)
        img = Image.fromarray(a.astype(np.uint8), "RGBA")
    if build == "faceted":
        img = led(img, [((30, 22), (330, 22)), ((W - 420, foot[0] + 9), (W - 200, foot[0] + 9))],
                  [(344, 22, TURQ), (W - 186, foot[0] + 9, AMBER)])
    sheet_img = Image.new("RGBA", TAB_SHEET, (0, 0, 0, 0))
    sheet_img.paste(img.crop((0, 0, W, H)), (0, 0))
    if solo:
        return sheet_img
    boxes = tab_button_boxes()
    for kind in TAB_BUTTONS:
        for state in TAB_STATES:
            put(sheet_img, tab_button(side, kind, state), boxes["Tab" + kind + state])
    for name, piece in tab_track(side).items():
        put(sheet_img, piece, boxes["Tab" + name])
    return sheet_img


def tab_fittings(p, build, W, H, top, foot):
    """Each side's bar language on the Tab screen's two plain strips; never on the glass."""
    t0, t1 = top
    f0, f1 = foot
    mid = (f0 + f1) / 2
    fx1 = TAB_HIDE[0] - 16                          # the foot strip stops short of the Main Menu socket
    if build == "machined":
        def swept(x, y0, y1, w, t):
            ym = (y0 + y1) / 2
            return [(x, y0), (x + t, y0), (x + t + w, ym), (x + t, y1), (x, y1), (x + w, ym)]
        p.inlay([swept(30 + 16 * i, t0 + 8, t1 - 8, 9, 4) for i in range(3)], ACCENT, 0.7)
        p.inlay([swept(W - 120 - 16 * i, f0 + 6, f1 - 6, -9, -4) for i in range(3)], ACCENT, 0.7)
        p.inlay([plate(x, t0 + 4, x + 1, t1 - 4) for x in (420, 840, 1260)] + [plate(x, f0 + 4, x + 1, f1 - 4) for x in (380, 760)],
                INKM, 0.8)
        p.inlay([plate(100, mid - 1, 340, mid + 1)], ACCENT, 0.5)
        for x, y in ((W - 16, 22), (20, f1 - 8), (fx1, mid), (W - 16, TAB_TABLE[1] + 20)):
            p.stud(x, y, 3.0)
    elif build == "terraced":
        p.inlay([plate(20, t0 + 6, W - 24, t0 + 11), plate(20, f1 - 9, fx1, f1 - 4)], ACCENT, 0.3)
        for x in range(40, W - 30, 36):
            p.rivet(x, t1 - 7, 2.2)
        for x in range(40, fx1, 36):
            p.rivet(x, f0 + 8, 2.2)
        p.bump([regular(x, t0 + 8.5, 2.6, 12) for x in (24, W - 28)] + [regular(fx1 + 2, mid, 2.6, 12)], 2.2, 1.4, GOLDM)
    elif build == "scavenged":
        patches = [(40, t0 + 4, 260, t1 - 4, -1.5), (W - 380, t0 + 6, W - 160, t1 - 3, 1.2), (90, f0 + 5, 330, f1 - 5, 1.0)]
        for i, (a, b, c, e, deg) in enumerate(patches):
            sh = sheet(a, b, c, e, deg)
            p.bump([sh], 1.4, 0.8, STEEL)
            for e0, e1 in zip(sh, sh[1:] + sh[:1]):
                p.weld(e0, e1, seed=61 + i * 7 + int(e0[0]), r=1.3)
        for i, (x, y) in enumerate(((22, 24), (300, t1 - 8), (W - 20, 30), (W - 140, t0 + 12), (60, f1 - 8), (fx1 - 6, f0 + 10),
                                    (W - 22, TAB_TABLE[1] + 40))):
            p.bolt(x, y, 2.6 + (i % 3) * 0.4, seed=i)
    elif build == "faceted":
        panels = [[(20, t0 + 4), (380, t0 + 4), (396, t1 - 4), (20, t1 - 4)],
                  [(W - 420, t0 + 4), (W - 40, t0 + 4), (W - 24, t1 - 4), (W - 436, t1 - 4)],
                  [(20, f0 + 4), (300, f0 + 4), (316, f1 - 4), (20, f1 - 4)]]
        p.bump(panels, 2.2, 1.0, STEEL)
        gaps = [plate(x, t0 + 4, x + 1, t1 - 4) for x in (400, W - 440)] + [plate(320, f0 + 4, 321, f1 - 4)]
        p.inlay(gaps, INKM, 0.9)
        ticks = [plate(x, f0 + 5, x + 1, f0 + (9 if i % 5 else 13)) for i, x in enumerate(range(340, fx1 - 200, 8))]
        p.inlay(ticks, INKM, 0.6)
        etch = [star8(x, (t0 + t1) / 2, 5.5) for x in range(W - 380, W - 80, 16)]
        etch += [q for x in range(W - 380, W - 80, 16) for q in ring(x, (t0 + t1) / 2, 7.2, 0.6, 20)]
        p.inlay(etch, DARK, 0.25)
        for x, y in ((W - 14, 24), (14, f1 - 8), (fx1, mid), (W - 14, TAB_TABLE[1] + 24)):
            p.inlay([regular(x, y, 2.8, 12)], DARK, 1.0)
            p.bump([regular(x, y, 1.5, 6)], 0.6, 0.6, STEEL)
        rng = np.random.default_rng(5)
        n = ndimage.gaussian_filter(rng.standard_normal((p.h_ // 4 + 1, p.w // 4 + 1)), 1.4)
        n /= n.std()
        tone = np.kron(np.where(n < -0.5, 0.92, np.where(n > 0.6, 1.07, 1.0)).astype(np.float32),
                       np.ones((4 * SS, 4 * SS), np.float32))[:p.h.shape[0], :p.h.shape[1]]
        sel = (p.mat == STEEL) & p.alpha
        p.tint[sel] *= tone[sel]
    else:                                           # the observer's desk
        grille(p, 30, t0 + 6, 330, t1 - 6, 3.4)
        grille(p, W - 360, t0 + 6, W - 60, t1 - 6, 3.4)
        feathers(p, W / 2 - 10, t0 + 8, 3, 120, DARK, left=True, gap=3.4, t=1.6)
        feathers(p, W / 2 + 10, t0 + 8, 3, 120, DARK, left=False, gap=3.4, t=1.6)
        screw(p, W / 2, t0 + 11, 2.6)
        tally(p, W - 30, (t0 + t1) / 2, 3.0)
        grille(p, 40, f0 + 6, 400, f1 - 6, 3.4)
        for x, y in ((18, 22), (18, f1 - 8), (fx1, mid)):
            screw(p, x, y, 2.4)


# The Tab screen's buttons, on the painting's sheet to its right: Diplomacy.cpp stretches each over its
# button of Diplomacy.wnd, so each is drawn at its size there at 1080.  Send, the money's - and + (Square) and
# Main Menu (Exit) are the toolbar's bezel round a dark face the code writes its text on; Mute (the player can
# be heard) and Unmute (he is muted) carry a speaker, its sound in two arcs or struck through.  D is disabled.
TAB_BUTTONS = dict(Send=(tab_u(760) - tab_u(700), tab_u(118) - tab_u(94)), Square=(tab_u(590) - tab_u(566), tab_u(118) - tab_u(94)),
                   Mute=(tab_u(44) - tab_u(20), tab_u(118) - tab_u(94)), Unmute=(tab_u(44) - tab_u(20), tab_u(118) - tab_u(94)),
                   Exit=(TAB_HIDE[2] - TAB_HIDE[0], TAB_HIDE[3] - TAB_HIDE[1]),
                   Up=(21, 22), Down=(21, 22), Thumb=(21, 64))
# The campaign briefing's scroll bar (GadgetListboxCreateScrollbar: arrows 21x22 in screen pixels at every
# resolution, the slider 21 wide): Up and Down carry an arrow, the Thumb is a bezel the code stretches along
# the track, and the track is a groove in four pieces the slider lays end, repeats and end
# (TAB_TRACK: Top and Bottom, Center and Small repeated between them, even down their length so they tile).
TAB_BUTTON_X = dict(Send=1384, Square=1496, Mute=1544, Unmute=1592, Exit=1640, Up=1812, Down=1840, Thumb=1868)
TAB_STATES = ("", "H", "P", "D")
TAB_BUTTON_STEP = 54
TAB_TRACK = dict(TrackTop=(21, 8), TrackBottom=(21, 8), TrackCenter=(21, 16), TrackSmall=(21, 2))
TAB_TRACK_X = 1896


def tab_button_boxes():
    """Tab<kind><state> -> its box on the sheet, the states one under the other, and the track's pieces."""
    boxes = {}
    for kind, (w, h) in TAB_BUTTONS.items():
        step = TAB_BUTTON_STEP if h <= TAB_BUTTON_STEP - 6 else h + 8      # the thumb's states need more
        for i, state in enumerate(TAB_STATES):
            boxes["Tab" + kind + state] = (TAB_BUTTON_X[kind], 4 + step * i, TAB_BUTTON_X[kind] + w, 4 + step * i + h)
    y = 4
    for name, (w, h) in TAB_TRACK.items():
        boxes["Tab" + name] = (TAB_TRACK_X, y, TAB_TRACK_X + w, y + h)
        y += h + 4
    return boxes


def arc(cx, cy, r, t, sweep=0.8, n=10):
    angles = np.linspace(-sweep, sweep, n)
    return [(cx + (r + t / 2) * np.cos(a), cy + (r + t / 2) * np.sin(a)) for a in angles] + \
        [(cx + (r - t / 2) * np.cos(a), cy + (r - t / 2) * np.sin(a)) for a in angles[::-1]]


def speaker(cx, cy, muted):
    """A speaker centred on cx, cy: its sound in two arcs, or struck through by a cross."""
    x = cx - 1
    parts = [plate(x - 8, cy - 4, x - 4, cy + 4), [(x - 4, cy - 4), (x + 2, cy - 9), (x + 2, cy + 9), (x - 4, cy + 4)]]
    if muted:
        parts += stroke([(x + 5, cy - 4), (x + 12, cy + 4)], 2.0) + stroke([(x + 5, cy + 4), (x + 12, cy - 4)], 2.0)
    else:
        parts += [arc(x + 2, cy, 5, 1.8), arc(x + 2, cy, 9.5, 1.8)]
    return parts


def tab_button(side, kind, state):
    look = BAR_LOOKS[side]
    build = look.get("skin", look["build"])
    w, h = TAB_BUTTONS[kind]
    o = 32                                          # a padded canvas, as mount_button's: shade's noise fields
    p = Painter(w + 2 * o, h + 2 * o)               # go bad on one 31 texels high
    st = "I" if state == "D" else state
    button(p, (o, o, o + w, o + h), None, st, build)
    off = 1 if state == "P" else 0
    cx, cy = o + w / 2 + off, o + h / 2 + off
    glyph = {"Mute": speaker(cx, cy, False), "Unmute": speaker(cx, cy, True),
             "Up": [[(cx, cy - 4), (cx + 4.5, cy + 3), (cx - 4.5, cy + 3)]],
             "Down": [[(cx - 4.5, cy - 3), (cx + 4.5, cy - 3), (cx, cy + 4)]]}.get(kind)
    if glyph:
        p.bump(glyph, 1.1, 0.6, {"": PAPERM, "H": GOLDM, "P": HILITE, "I": DIM}[st])
    return shade(p, look, shadow=False, falloff=False).crop((o, o, o + w, o + h))


def tab_track(side):
    """The scroll bar's groove: one drawn 21x64 and cut, Center and Small its middle rows averaged down the
    length, so a repeat of them shows no seam."""
    look = BAR_LOOKS[side]
    build = look.get("skin", look["build"])
    w, h, o = 21, 64, 32
    p = Painter(w + 2 * o, h + 2 * o)
    p.frame([shape(build, o, o, o + w, o + h, 3)], 2.0, build, DARK, BEZEL_SCALE)
    p.sink([plate(o + 5, o + 4, o + w - 5, o + h - 4)], 2.0, 1.0)
    groove = shade(p, look, shadow=False, falloff=False).crop((o, o, o + w, o + h))
    middle = np.asarray(groove)[24:40].astype(np.float32).mean(axis=0, keepdims=True).round().astype(np.uint8)
    return {"TrackTop": groove.crop((0, 0, w, 8)), "TrackBottom": groove.crop((0, h - 8, w, h)),
            "TrackCenter": Image.fromarray(np.repeat(middle, 16, axis=0), "RGBA"),
            "TrackSmall": Image.fromarray(np.repeat(middle, 2, axis=0), "RGBA")}


def check_tab(sheet_img, side_name, prefix, solo_img=None):
    """The paintings and the buttons are mapped where the generator put them, each painting opaque along the
    screen's two edges it stands on, and the rows, the titles and the Main Menu button stand on it."""
    M = read_mapped(side_name, "TabPanel")
    want = {prefix + "TabPanel": (0, 0) + TAB_PANEL}
    if solo_img is not None:
        want[prefix + "TabSolo"] = (0, 0) + TAB_PANEL
    want.update({prefix + name: box for name, box in tab_button_boxes().items()})
    assert M == want, "ReforgedHud%sTabPanel.ini maps %s, the generator draws %s" % (side_name, sorted(set(M) ^ set(want)) or M, want)
    W, H = TAB_PANEL
    for img in [sheet_img] + ([solo_img] if solo_img is not None else []):
        a = np.asarray(img)[..., 3]
        assert (a[0, :W - 60] == 255).all() and (a[:H - 60, 0] == 255).all(), "the Tab screen leaves the screen's edge"
        for x0, y0, x1, y1 in [TAB_TITLES, TAB_TABLE, TAB_HIDE]:
            assert (a[y0:y1, x0:x1] == 255).all(), "the Tab screen has a hole under %s" % ((x0, y0, x1, y1),)
    a = np.asarray(sheet_img)[..., 3]
    for name, (x0, y0, x1, y1) in tab_button_boxes().items():
        assert (a[y0 + 8:y1 - 8, x0 + 8:x1 - 8] == 255).all(), "%s%s has a hole in its face" % (prefix, name)


# ---------------------------------------------------------------------------------------------- the observer

# The watcher's bar, its layout approved by the owner on 2026-11-03 with the rows' colour lamps (round 9's
# A), the radar framed to the map's shape and the rack; since round 19 (2026-11-05) drawn as round 18 draws
# the sides' bars, three tiers in the desk build (OBS_LOOK): every box flush with the screen's edges and
# square there, its corners toward the field cut, a near-black shell, a bevelled lip, a recessed floor and
# scanline wells.  Since round 20 (2026-11-06) each box is one side's build in that steel (BAR_LOOKS'
# Observer note).  Every window it frames is ControlBar.wnd's and the Observer8x6 scheme's, in screen pixels
# at 1920x1080; the scheme names BlockRadar for the radar box's ledge and BlockObserverInfo for the followed
# player's panel, which runs to the screen's foot; the rest stands inside the .wnd's own blockers (the column's,
# the selection's and, round the mount, the general's button's).  Its pieces are on two sheets of their own,
# ReforgedHudObsPieces (the toolbar's buttons, the rows' hover and watched rings, the radar's nine) and
# ReforgedHudObsSpec (the rack), each laid out by its MappedImages/HandCreated INI.
OBS_LIST = [(334, 755 + 32 * i, 379, 785 + 32 * i) for i in range(8)]  # ButtonPlayer0..7, each a side's picture
OBS_OPTIONS, OBS_CHAT = (334, 1011, 379, 1041), (334, 1043, 379, 1073)  # OptionsUL/LR, ChatUL/LR; ButtonCancel on chat
OBS_SELECTION, OBS_EXP = (1642, 887, 1871, 1071), (1875, 887, 1911, 1071)   # RightHUD and the rank bar's place
# the column's, the selection's and the general's button's (round the mount); the radar's follows its frame
OBS_BLOCKS = [(332, 740, 394, 1080), (1625, 870, 1920, 1080), (1821, 794, 1920, 870)]
OBS_MOUNT = (1840, 836, 1920, 878)              # the selection box's mount, the tally on it
# ObserverPlayerInfoWindow: its picture (WinGeneralPortrait, ObserverProPlayerWindow) is 512x256 at 327,824
OBS_PANEL_AT = (327, 824)
OBS_POWER = (332, 895, 822, 905)                # PowerBarUL/LR
OBS_NAME, OBS_MONEY = (334, 912, 698, 946), (700, 912, 820, 946)        # StaticTextPlayerName, MoneyUL/LR
OBS_LABELS = [(334 + 122 * i, 948, 454 + 122 * i, 998) for i in range(4)]   # Buildings, Units, Units Lost, Units Killed
OBS_NUMBERS = [(334, 1000, 454, 1034), (454, 1000, 574, 1034), (578, 1000, 698, 1034), (700, 1000, 820, 1034)]
OBS_PANEL_BLOCKS = [(394, 879, 833, 1080), (332, 740, 394, 1080)]   # Observer8x6's BlockObserverInfo, the column's
OBS_PIECES_SIZE, OBS_SPEC_SIZE = (512, 512), (1024, 256)


def to_bar(r):
    return (r[0], r[1] - TOP, r[2], r[3] - TOP)


def to_panel(r):
    return (r[0] - OBS_PANEL_AT[0], r[1] - OBS_PANEL_AT[1], r[2] - OBS_PANEL_AT[0], r[3] - OBS_PANEL_AT[1])


def screw(p, cx, cy, r=2.6):
    """A round head with a cross slot."""
    p.bump([regular(cx, cy, r, 16)], 1.4, r * 0.8, BRIGHT)
    k = r * 0.68
    p.inlay([plate(cx - k, cy - 0.4, cx + k, cy + 0.4), plate(cx - 0.4, cy - k, cx + 0.4, cy + k)], INKM, 0.6)


def grille(p, x0, y0, x1, y1, pitch=3.4, r=0.85):
    """A perforated speaker grille, the rows staggered."""
    holes = []
    for j, y in enumerate(np.arange(y0 + pitch / 2, y1, pitch)):
        for x in np.arange(x0 + pitch / 2 + (j % 2) * pitch / 2, x1, pitch):
            holes.append(regular(x, y, r, 8))
    p.inlay(holes, INKM, 1.0)


def feathers(p, x, y, n, length, mat, left=True, gap=2.6, t=1.4):
    """The emblem's wing: n blades stacked, each shorter, their roots at x, tapering to a point outward."""
    s = -1 if left else 1
    polys = []
    for i in range(n):
        L = length * (1 - 0.24 * i)
        yy = y + i * gap
        polys.append([(x, yy), (x + s * L * 0.9, yy), (x + s * L, yy + t * 0.5), (x + s * L * 0.9, yy + t), (x, yy + t)])
    p.inlay(polys, mat, 0.5)


def tally(p, cx, cy, r):
    """The red tally lamp a broadcast camera wears while it is on air: a lens in a dark bezel."""
    p.bump([regular(cx, cy, r + 1.3, 16)], 1.0, 0.8, DARK)
    p.inlay([regular(cx, cy, r, 16)], ACCENT2, 0.2)
    p.bump([regular(cx - r * 0.35, cy - r * 0.35, r * 0.3, 8)], 0.4, 0.3, PAPERM)


# each row's colour lamp beside its button, empty glass until the code lights it in the player's colour
OBS_LAMPS = [(383, r[1] + 4, 389.5, r[3] - 4) for r in OBS_LIST]


def fastener(p, cx, cy):
    """Turkiye's recessed fastener: a dark cup with a hex head low in it."""
    p.inlay([regular(cx, cy, 2.8, 12)], DARK, 1.0)
    p.bump([regular(cx, cy, 1.5, 6)], 0.6, 0.6, STEEL)


def paint_observer_bar():
    """The bar: the players' column on the left, a lamp beside each row, the selection window under its mount
    on the right and nothing in the middle.  The column is the right end of the radar's box (America's): the
    radar's nine piece frame (observer_radar_frame) stands round the map left of it, its ledge running over
    the radar to an arrow point on the column's top, and the column takes the box's 45 degree cut toward the
    field.  The selection box is Turkiye's."""
    b = "machined"
    p = Painter(2048, 512)
    # the column, open on its left where the frame's right edge meets it: drawn from further left and that
    # part cut away, so its lip runs on into the frame's
    stack(p, plate(300, 740, 394, 1080, tr=("cut", 30)))
    floor(p, plate(300, 751, 389, 1080, tr=("cut", 22)))
    for row, lamp in zip(OBS_LIST, OBS_LAMPS):
        well(p, shape(b, *grow(row, 2), 3), 4, 2)
        p.bump([T(shape(b, *grow(lamp, 1.4), 2))], 1.0, 0.7, DARK)
        well(p, shape(b, *lamp, 2), 1.5, 0.8)
    for r in (OBS_OPTIONS, OBS_CHAT):
        well(p, shape(b, *grow(r, 1), 4), 3.5, 2)
    p.inlay([T(plate(389, 1006, 394, 1007))], INKM, 0.8)
    for x, y in ((342, 746), (388, 1076)):
        p.stud(x, y - TOP, 2.2)
    p.cut([T(plate(290, 730, 330, 1080))])

    # the selection's box: Turkiye's wide chamfer, an armour panel with a gap, range ticks and recessed
    # fasteners, and a mount above as on every side's bar, holding a grille and the tally
    stack(p, plate(1625, 870, 1920, 1080, tl=("slant", 26)))
    stack(p, plate(*OBS_MOUNT, tl=("slant", 12)), 9, 12, 4, 2.2)
    floor(p, plate(1850, 846, 1920, 872))
    grille(p, 1856, 850 - TOP, 1892, 868 - TOP, 3.2)
    tally(p, 1906, 859 - TOP, 3.0)
    floor(p, plate(1638, 883, 1916, 1080, tl=("slant", 14)))
    well(p, plate(*grow(OBS_SELECTION, 2)), 6, 2.5)
    well(p, plate(*grow(OBS_EXP, 1)), 6, 2)
    p.bump([T([(1690, 877), (1704, 872), (1790, 872), (1790, 877)])], 2.2, 1.0, STEEL)
    p.inlay([T(plate(1792, 871, 1793, 880))], INKM, 0.9)
    p.inlay([T(plate(x, 873, x + 1, 876 if i % 5 else 879)) for i, x in enumerate(range(1798, 1834, 6))], INKM, 0.6)
    for x, y in ((1632, 1070), (1912, 1070), (1680, 876)):
        fastener(p, x, y - TOP)
    return bar_finish(shade(p, OBS_LOOK, SEAMS), p, OBS_LOOK)


def paint_observer_panel():
    """The watched player's panel (ObserverProPlayerWindow), China's: a rail over it carrying the power
    channel, stepped at its end like the pagoda's eaves, with a row of rivets and a dark lacquer band, the
    name and money wells, the four counts' labels and numbers, and the options and back buttons where it
    covers the column.  Square on the screen's foot, open on the left where it meets the radar's frame."""
    b = "terraced"
    p = Painter(512, 256)
    p.tiers([plate(-12, 78, 506, 256)], (False, True, True, False))
    p.tiers([plate(-12, 55, 506, 86, tr=("step2", 14))], (False, False, True, False), 9.0, 13.0, 4.0, 2.2)
    p.recess([plate(-12, 90, 496, 256)], (False, True, True, False))
    p.sink([plate(*grow(to_panel(OBS_POWER), 2))], 5, 1.6)
    p.sink([shape(b, *grow(to_panel(OBS_NAME), 1), 4)], 6, 2.5)
    p.sink([shape(b, *grow(to_panel(OBS_MONEY), 1), 4)], 6, 2.5)
    for lab in OBS_LABELS:
        x0, y0, x1, y1 = to_panel(lab)
        p.sink([shape(b, x0 + 3, y0 + 4, x1 - 3, y1 - 4, 3)], 3, 1.4)
    for num in OBS_NUMBERS:
        x0, y0, x1, y1 = to_panel(num)
        x0 = max(x0, 56) + 2                    # the first count stands right of the options button
        p.sink([shape(b, x0, y0, x1 - 2, y1, 3)], 6, 2.5)
    p.sink([shape(b, *grow(to_panel(OBS_OPTIONS), 1), 4)], 3.5, 2)
    p.sink([shape(b, *grow(to_panel(OBS_CHAT), 1), 4)], 3.5, 2)
    p.inlay([plate(30, 64.5, 470, 66.5)], DARK, 0.3)
    for x in range(20, 480, 30):
        p.rivet(x, 61.5, 1.7)
    for y in range(104, 250, 26):
        p.rivet(500.5, y, 1.8)
    p.cut([plate(-20, 0, 2, 256)])
    return bar_finish(shade(p, OBS_LOOK), p, OBS_LOOK, gloss=False)


def paint_observer_pieces():
    """ReforgedHudObsPieces, 512x512: the toolbar's three buttons in four states, the hovered (RowH) and
    the watched (RowS) row's rings that stand 4 px round a 45x30 player button, the radar's nine and the
    sides' emblems (paint_side_emblems, variant A)."""
    b = "machined"                              # the column's, America's like the radar box it belongs to
    M = read_mapped("Obs", "Pieces")
    p = Painter(*OBS_PIECES_SIZE)
    for name, r in M.items():
        kind = name[len("ObserverPro"):]
        if not name.startswith("ObserverPro") or kind.startswith("Radar"):
            continue
        st = kind[-1] if kind[-1] in "HPIS" else ""
        base = kind[:-1] if st else kind
        if base in ("Options", "Chat", "Back"):
            button(p, r, base, st, b)
        else:                                   # a ring round the button, its middle open: shell and lip
            x0, y0, x1, y1 = r
            sl, m, d = p.tiers([shape(b, x0, y0, x1, y1, 5)], (False,) * 4, 3.0, 5.0, 1.4, 1.0, 1.4)
            if st == "H":
                p.mat[sl][m & (d > 1.4)] = BRIGHT
                p.inlay([plate(x0 + 9, y1 - 2.6, x1 - 9, y1 - 1.4)], HILITE, 0.2)
            else:                               # the watched one: on air, red bars top and bottom
                p.inlay([plate(x0 + 9, y0 + 1.4, x1 - 9, y0 + 2.6), plate(x0 + 9, y1 - 2.6, x1 - 9, y1 - 1.4)], ACCENT2, 0.2)
            p.cut([shape(b, x0 + 4, y0 + 4, x1 - 4, y1 - 4, 2)])
    p.limit[:] = True
    sheet_img = bar_finish(shade(p, OBS_LOOK, shadow=False), p, OBS_LOOK, scan=False, gloss=False)
    for name, img in radar_pieces(observer_radar_frame(), OBS_RADAR_EDGE).items():
        put(sheet_img, img, M["ObserverProRadar" + name])
    for name, img in paint_side_emblems("A").items():
        put(sheet_img, img, M[name])
    check_mapped(M, OBS_PIECES_SIZE)
    return sheet_img


# ---------------------------------------------------------------------------------------------- the sides' emblems

# The picture a player's row carries on the observer's bar (ButtonPlayer0..7, 45x30, the PlayerTemplate's
# EnabledImage) and in the rack's standings (the same image drawn at 30x20), round 13; the owner picked A
# on 2026-11-03 and it ships on ReforgedHudObsPieces, where its INI maps each name.  Each side has a base mark in its own build (America twin chevrons, China a stepped pyramid, the GLA two crossed
# scrap bars on a hex bolt, Turkiye a faceted arrowhead) and each general his side's mark beside a glyph
# of his own.  No flag, national star, crescent or eagle, and no flag's colour pair: Turkiye's red sits
# on dark steel only.  A is a badge: the side's plate lit as the bar is, the mark on an accent field and
# the general's glyph raised in a sunk well.  B is the glyph alone in flat paint on a small plate in the
# side's silhouette and colour, kept for the record.  Names: the three bases keep retail's EnabledImage
# names (SSObserverUSA, China, GLA, which every side and general of PlayerTemplate.ini gives), the rest
# are SSObserver and the template's name without Faction and General.
SIDE_EMBLEM = (45, 30)
SIDE_EMBLEMS = [    # MappedImage, the side whose build it takes, glyph
    ("SSObserverUSA", "America", None), ("SSObserverAmericaSuperWeapon", "America", "reticle"),
    ("SSObserverAmericaLaser", "America", "laser"), ("SSObserverAmericaAirForce", "America", "jet"),
    ("SSObserverChina", "China", None), ("SSObserverChinaTank", "China", "tread"),
    ("SSObserverChinaInfantry", "China", "helmet"), ("SSObserverChinaNuke", "China", "atom"),
    ("SSObserverBoss", "China", "crown"), ("SSObserverGLA", "Gla", None),
    ("SSObserverGLAToxin", "Gla", "trefoil"), ("SSObserverGLADemolition", "Gla", "charge"),
    ("SSObserverGLAStealth", "Gla", "shroud"), ("SSObserverTurkiye", "Turkiye", None),
]


def spark(cx, cy, r):
    """A four-pointed burst, straight cuts."""
    return [(cx + (r if i % 2 == 0 else r * 0.3) * np.cos(np.pi / 4 * i), cy + (r if i % 2 == 0 else r * 0.3) * np.sin(np.pi / 4 * i))
            for i in range(8)]


def stroke(path, w):
    """A polyline w wide: a bar along each segment and a square at each joint."""
    out = []
    for (x0, y0), (x1, y1) in zip(path, path[1:]):
        length = np.hypot(x1 - x0, y1 - y0)
        nx, ny = -(y1 - y0) / length * w / 2, (x1 - x0) / length * w / 2
        out.append([(x0 + nx, y0 + ny), (x1 + nx, y1 + ny), (x1 - nx, y1 - ny), (x0 - nx, y0 - ny)])
    return out + [plate(x - w / 2, y - w / 2, x + w / 2, y + w / 2) for x, y in path[1:-1]]


def wedge(r0, r1, a, w0, w1):
    """A blade from radius r0 to r1 along angle a (degrees), w0 and w1 its half widths in degrees."""
    def at(r, d):
        t = np.radians(d)
        return (r * np.cos(t), r * np.sin(t))
    return [at(r0, a - w0), at(r1, a - w1), at(r1, a + w1), at(r0, a + w0)]


def emblem_glyph(kind):
    """A side's mark or a general's glyph round (0, 0), about 24 units across, as parts (sign, polygons,
    holes): a '+' part is added, a '-' part is cut out of what is there, and each part's holes are its own."""
    if kind == "machined":                              # the emblem's twin chevrons
        return [("+", chevrons(0, -10, 22, n=2, up=True), [])]
    if kind == "terraced":                              # a stepped pyramid, three courses
        return [("+", [plate(-3.5, -9, 3.5, -4), plate(-8, -2.6, 8, 2.4), plate(-12, 3.8, 12, 8.8)], [])]
    if kind == "scavenged":                             # two scrap bars crossed out of square on a hex bolt
        long_ = rotated(plate(-12.5, -2.4, 12.5, 2.4), 0, 0, 34)
        short = rotated(plate(-10, -2.1, 9.5, 2.1), 0, 0, -41)
        bite = rotated([(10.5, -2.6), (13, -2.6), (13, 0.8)], 0, 0, 34)
        return [("+", [long_, short], []), ("-", [bite, regular(0, 0, 4.2, 6, 0.3)], []),
                ("+", [regular(0, 0, 3.4, 6, 0.3)], [regular(0, 0, 1.2, 6)])]
    if kind == "faceted":                               # an arrowhead of two facets, a groove up its spine
        return [("+", [[(0, -11), (12, 9), (0, 3.5), (-12, 9)]], []), ("-", [plate(-0.55, -7.5, 0.55, 2.8)], [])]
    if kind == "reticle":                               # the superweapon general: a target ring and ticks
        return [("+", [octagon(-10, -10, 10, 10, 4.2)], [octagon(-7.2, -7.2, 7.2, 7.2, 3.0)]),
                ("+", [plate(-1.1, -12.5, 1.1, -4.2), plate(-1.1, 4.2, 1.1, 12.5), plate(-12.5, -1.1, -4.2, 1.1),
                       plate(4.2, -1.1, 12.5, 1.1)], []), ("+", [octagon(-2, -2, 2, 2, 0.9)], [])]
    if kind == "laser":                                 # a beam struck down from the corner, burst where it lands
        beam = rotated([(-12, -2.8), (3, -1.6), (3, 1.6), (-12, 2.8)], 0, 0, 38)
        return [("+", [beam, rotated(plate(-13, -4.6, -9.5, 4.6), 0, 0, 38), spark(4.6, 3.6, 8.5)], []),
                ("-", [regular(4.6, 3.6, 1.6, 8)], [])]
    if kind == "jet":
        raised, ink = icon("Aircraft", 0, 0, "machined")
        return [("+", raised, ink)]
    if kind == "tread":
        raised, ink = icon("Vehicle", 0, 0, "terraced")
        return [("+", raised, ink)]
    if kind == "helmet":                                # a helmet side on, its band and chin strap
        dome = [(-11, 3), (-10, -2), (-7, -6.5), (-2, -8.5), (4, -8), (8.5, -5), (10.5, -0.5), (12.5, 2.5), (12.5, 4.6),
                (-11, 4.6)]
        return [("+", [dome, [(-6.5, 4.6), (-4.2, 4.6), (1.5, 10.5), (-0.8, 10.5)]], []), ("-", [plate(-10.6, -0.6, 11, 0.9)], [])]
    if kind == "atom":                                  # three straight-cut orbits and a nucleus
        parts = [("+", [rotated(octagon(-12.5, -5, 12.5, 5, 3.4), 0, 0, d)], [rotated(octagon(-10.2, -2.7, 10.2, 2.7, 2.0), 0, 0, d)])
                 for d in (0, 60, 120)]
        return parts + [("+", [octagon(-2.7, -2.7, 2.7, 2.7, 1.1)], [])]
    if kind == "trefoil":                               # the toxin general: three clawed blades round a hub
        blades = [wedge(3.9, 12, a, 30, 36) for a in (-90, 30, 150)]
        claws = [regular(9.6 * np.cos(np.radians(a)), 9.6 * np.sin(np.radians(a)), 3.4, 8, np.pi / 8) for a in (-90, 30, 150)]
        return [("+", blades, []), ("-", claws, []), ("+", [octagon(-2.4, -2.4, 2.4, 2.4, 1.0)], [])]
    if kind == "charge":                                # the demolition general: an octagon bomb, its fuse lit
        return [("+", [octagon(-12, -6, 6, 12, 5.4), rotated(plate(-2.6, -9.4, 2.6, -5), -3, -3, 45)], []),
                ("-", [plate(-7, -1.4, -3.4, 0.4)], []),
                ("+", [rotated(plate(1.5, -8.6, 7.5, -7.4), 1.5, -8, -35), spark(8.6, -10.6, 4.4)], [])]
    if kind == "shroud":                                # the stealth general: a cut eye, struck through
        eye = [(-12.5, 0), (-5, -7), (5, -7), (12.5, 0), (5, 7), (-5, 7)]
        lid = [(-9, 0), (-3.8, -4.4), (3.8, -4.4), (9, 0), (3.8, 4.4), (-3.8, 4.4)]
        return [("+", [eye], [lid]), ("+", [octagon(-2.8, -2.8, 2.8, 2.8, 1.2)], []),
                ("-", [rotated(plate(-15, -2.3, 15, 2.3), 0, 0, -38)], []), ("+", [rotated(plate(-13.5, -1, 13.5, 1), 0, 0, -38)], [])]
    if kind == "eye":                                   # the observer: the cut eye, open
        eye = [(-12.5, 0), (-5, -7), (5, -7), (12.5, 0), (5, 7), (-5, 7)]
        lid = [(-9, 0), (-3.8, -4.4), (3.8, -4.4), (9, 0), (3.8, 4.4), (-3.8, 4.4)]
        return [("+", [eye], [lid]), ("+", [octagon(-3, -3, 3, 3, 1.3)], [])]
    if kind == "random":                                # a question mark of straight strokes
        path = [(-6, -6.5), (-6, -11), (6, -11), (6, -3.5), (0, 0.5), (0, 4)]
        return [("+", stroke(path, 3.6) + [plate(-1.9, 7, 1.9, 11)], [])]
    if kind == "crown":                                 # the boss: three spikes on a band
        return [("+", [[(-11.5, 7), (-11.5, -5.5), (-5.6, 0.4), (0, -8.5), (5.6, 0.4), (11.5, -5.5), (11.5, 7)],
                       octagon(-13.2, -8.8, -9.8, -5.4, 1.2), octagon(-1.7, -12.3, 1.7, -8.9, 1.2), octagon(9.8, -8.8, 13.2, -5.4, 1.2)], []),
                ("-", [plate(-11.5, 2.6, 11.5, 3.9)], [])]
    raise ValueError(kind)


def glyph_mask(parts, w, h, box, scale):
    """parts fitted into box (x0, y0, x1, y1 in texels, aspect kept, centred), drawn scale times: a mask."""
    xs = [x for s, polys, _ in parts if s == "+" for q in polys for x, _ in q]
    ys = [y for s, polys, _ in parts if s == "+" for q in polys for _, y in q]
    k = min((box[2] - box[0]) / (max(xs) - min(xs)), (box[3] - box[1]) / (max(ys) - min(ys)))
    ox, oy = (box[0] + box[2]) / 2 - k * (min(xs) + max(xs)) / 2, (box[1] + box[3]) / 2 - k * (min(ys) + max(ys)) / 2
    m = np.zeros((h * scale, w * scale), bool)
    for sign, polys, holes in parts:
        im = Image.new("L", (w * scale, h * scale), 0)
        d = ImageDraw.Draw(im)
        for fill, qs in ((255, polys), (0, holes)):
            for q in qs:
                d.polygon([((ox + k * x) * scale, (oy + k * y) * scale) for x, y in q], fill=fill)
        pm = np.asarray(im) > 0
        m = (m | pm) if sign == "+" else (m & ~pm)
    return m


def emblem_body(build, box):
    """The badge's outline in the side's build."""
    x0, y0, x1, y1 = box
    if build == "scavenged":                            # a sheet laid on out of square, its edges bitten
        return rotated(jitter(shape(build, x0 + 0.5, y0 + 0.5, x1 - 0.5, y1 - 0.5, 4), 13, amp=0.9, step=5), (x0 + x1) / 2, (y0 + y1) / 2, 2.2)
    return shape(build, x0, y0, x1, y1, {"machined": 6, "terraced": 3, "faceted": 4}[build])


def emblem_badge(side_name, glyph, k=1.0):
    """Variant A, 45x30 times k: the side's plate lit like the bar, its mark on an accent field and the
    general's glyph raised in a sunk well beside it (a side alone has the mark large across the field).
    Drawn in the 45x30 badge's coordinates and scaled whole, so the score screen's 60x40 (k 4/3) is the
    same picture."""
    side = SIDES[side_name]
    b = side["build"]
    W, H = round(SIDE_EMBLEM[0] * k), round(SIDE_EMBLEM[1] * k)
    pad = 8
    p = Painter(W + 2 * pad, H + 2 * pad)

    def o(polys):
        return [[(x * k + pad, y * k + pad) for x, y in q] for q in polys]

    def at(x, y):
        return x * k + pad, y * k + pad

    def raise_mask(parts, box, height, bevel, material):
        m = glyph_mask(parts, W + 2 * pad, H + 2 * pad, (*at(*box[:2]), *at(*box[2:])), SS)
        d = ndimage.distance_transform_edt(np.pad(m, 1))[1:-1, 1:-1] / SS
        p.h += np.clip(d / (bevel * k), 0, 1) * m * height * k
        p.mat[m] = material

    body = emblem_body(b, (1, 1, 44, 29))
    sl, m, _ = p.frame(o([body]), 3.0 * k, b, DARK, scale=(0.35 if b != "scavenged" else 1.0) * k)
    p.limit[sl] |= m
    if b == "machined":                                 # two tiers: the satin frame on the dark flange
        p.frame(o([shape(b, 2.6, 2.6, 42.4, 27.4, 4.8)]), 4.4 * k, b, STEEL, scale=0.4 * k)
    elif b == "scavenged":                              # a second scrap laid over the top left, welded
        p.frame(o([sheet(2.5, 2.5, 20, 15, -5)]), 3.8 * k, b, STEEL, scale=k)
        p.frame(o([sheet(14, 3, 43, 27.5, 1.5)]), 3.6 * k, b, STEEL, scale=k)
    elif b == "faceted":
        p.frame(o([shape(b, 2.4, 2.4, 42.6, 27.6, 3.2)]), 4.4 * k, b, STEEL, scale=0.25 * k)
    else:
        p.mat[sl][m] = STEEL
    field_mat, mark_mat = (ACCENT2, ACCENT) if b == "faceted" else (ACCENT, PAPERM)
    glyph_mat = BRIGHT if b == "faceted" else PAPERM
    if glyph is None:
        field, mark_box = shape(b, 6, 4.5, 39, 25.5, 2.6), (12, 6.5, 33, 23.5)
    else:
        field, mark_box = shape(b, 4.5, 4.5, 16, 25.5, 2.2), (6, 9, 14.5, 21)
        p.sink(o([shape(b, 17.5, 4.5, 41, 25.5, 2.6)]), 1.8 * k, 0.8 * k)
    p.inlay(o([field]), field_mat, 0.3)
    raise_mask(emblem_glyph(b), mark_box, 1.0, 0.45, mark_mat)
    if glyph is not None:
        raise_mask(emblem_glyph(glyph), (19.5, 6.5, 39, 23.5), 1.0, 0.45, glyph_mat)
    if b == "machined":                                 # the second accent: a red hairline under the field
        p.inlay(o([plate(*((7, 26.2, 38, 26.9) if glyph is None else (5.5, 26.2, 15, 26.9)))]), ACCENT2, 0.2)
    elif b == "terraced":
        for x, y in ((3.6, 3.6), (41.4, 3.6), (3.6, 26.4), (41.4, 26.4)):
            p.rivet(*at(x, y), 1.1 * k)
    elif b == "scavenged":
        p.weld(at(14, 15), at(20, 15.5), 5, 0.6 * k)
        p.bolt(*at(41.2, 26), 1.2 * k, 3)
        for i in range(3):                              # hazard slashes along the top edge only
            x = 28 + 4 * i
            p.inlay(o([[(x, 1.8), (x + 1.8, 1.8), (x + 0.6, 3.8), (x - 1.2, 3.8)]]), ACCENT2 if i % 2 else INKM, 0.2)
    else:
        p.facets(5.0 * k, 0.05, 0.18 * k, 41)
        p.bolt(*at(42, 15), 1.1 * k, 0)
    return shade(p, side, shadow=False).crop((pad, pad, pad + W, pad + H))


# B's flat paint: the plate, its second tone, the glyph and its ink
EMBLEM_FLAT = {
    "America": dict(plate=BLUE, tone=(0x5a, 0x6e, 0xc8), line=RED, glyph=PAPER),
    "China": dict(plate=RED, tone=(0x93, 0x1e, 0x19), line=(0xd0, 0x8a, 0x22), glyph=PAPER),
    "Gla": dict(plate=(0x6c, 0x6a, 0x30), tone=(0xb0, 0x5a, 0x1e), line=INK, glyph=PAPER),
    "Turkiye": dict(plate=(0x3e, 0x45, 0x4d), tone=(0x52, 0x5a, 0x63), line=(0xc0, 0x2a, 0x2a), glyph=(0xb4, 0xbc, 0xc2)),
}


def emblem_flat(side_name, glyph):
    """Variant B, 45x30: the glyph alone in flat paint, ink round it, on a small plate in the side's
    silhouette and colours (a side alone carries its mark)."""
    b = SIDES[side_name]["build"]
    c = EMBLEM_FLAT[side_name]
    W, H = SIDE_EMBLEM
    S = 4
    yy, xx = np.mgrid[0:H * S, 0:W * S] / S

    def poly_mask(polys):
        im = Image.new("L", (W * S, H * S), 0)
        d = ImageDraw.Draw(im)
        for q in polys:
            d.polygon([(x * S, y * S) for x, y in q], fill=255)
        return np.asarray(im) > 0

    out = np.zeros((H * S, W * S, 4), np.float32)

    def paint(m, rgb):
        out[m] = tuple(v / 255.0 for v in rgb) + (1.0,)

    box = (8, 2, 37, 28)
    plate_m = poly_mask([emblem_body(b, box)])
    paint(plate_m, c["plate"])
    if b == "machined":                                 # a satin band over the top, a red hairline at the foot
        paint(plate_m & (yy < box[1] + 5), c["tone"])
        paint(plate_m & (yy > box[3] - 3.2) & (yy < box[3] - 2.2) & (xx > box[0] + 5) & (xx < box[2] - 5), c["line"])
    elif b == "terraced":                               # a terrace inset round the face, rivets on the rim
        paint(plate_m & ~poly_mask([shape(b, box[0] + 2.5, box[1] + 2.5, box[2] - 2.5, box[3] - 2.5, 2)]), c["tone"])
        for x, y in ((box[0] + 1.3, box[1] + 1.3), (box[2] - 1.3, box[1] + 1.3), (box[0] + 1.3, box[3] - 1.3), (box[2] - 1.3, box[3] - 1.3)):
            paint(poly_mask([regular(x, y, 0.8, 8)]), c["line"])
    elif b == "scavenged":                              # a rust patch and hazard slashes on the top edge
        paint(plate_m & poly_mask([sheet(box[2] - 11, box[3] - 9, box[2] + 2, box[3] + 2, -8)]), c["tone"])
        for i in range(3):
            x = box[0] + 15 + 3.4 * i
            paint(plate_m & poly_mask([[(x, box[1]), (x + 1.6, box[1]), (x + 0.2, box[1] + 3.2), (x - 1.4, box[1] + 3.2)]]),
                  c["line"] if i % 2 == 0 else c["tone"])
    else:                                               # two facets and a red marker pointing in from the left
        paint(plate_m & poly_mask([[(box[0], box[3]), (box[2], box[1]), (box[2], box[3])]]), c["tone"])
        paint(plate_m & poly_mask([triangle(box[0] + 2.2, (box[1] + box[3]) / 2, 3.2, 5, "right")]), c["line"])
    g = glyph_mask(emblem_glyph(glyph or b), W, H, (10, 4.5, 35, 25.5) if glyph else (12, 5.5, 33, 24.5), S)
    ink = ndimage.binary_dilation(g, iterations=int(S * 0.9)) & ~g
    paint(ink, INK)
    paint(g, c["glyph"])
    keyline = ndimage.binary_dilation(plate_m | g, iterations=S) & ~(plate_m | g | ink)
    paint(keyline, INK)
    small = out.reshape(H, S, W, S, 4).mean(axis=(1, 3))
    a = small[..., 3:4]
    small[..., :3] = np.where(a > 0, small[..., :3] / np.maximum(a, 1e-6), 0)
    return Image.fromarray((np.clip(small, 0, 1) * 255 + 0.5).astype(np.uint8), "RGBA")


def paint_side_emblems(variant):
    """Every side's and general's emblem in variant A or B: MappedImage name -> 45x30 piece."""
    draw = emblem_badge if variant == "A" else emblem_flat
    return {name: draw(side, glyph) for name, side, glyph in SIDE_EMBLEMS}


# ---------------------------------------------------------------------------------------------- the front end's emblems

# The same family where the front end shows a side (round 14, approved by the owner on 2026-11-03):
# ReforgedFactionEmblems, 1024x1024, laid out by its MappedImages/HandCreated INI.  A card, 240x240, for
# each army the army screen lists, the Boss and the observer, which FactionPicker draws as a 100 px tile,
# a 96 px pool icon and the focused portrait (FactionPicker<template>, in place of EA's persona faces), and
# which the retail GeneralImage names (USA_Logo, USA_Laser, ...) map too, for PopupPlayerInfo's 86 px
# picture; a 48x44 side icon for the GameinfoXXX names (SideIconImage, the LAN game list's 22x25); and the
# score screen's row emblems, SSScore<suffix>, the SSObserver badges at 60x40.  A card is the side's plate
# in its build round a sunk well with the general's glyph (a side alone: its mark large on the accent
# field), over a side plate carrying the mark and the build's fittings.  The score screen's watermark is
# the local player's card, which the code draws faint.
CARD = 240
FACTION_CARDS = [   # FactionPicker image, the side whose build it takes, glyph
    ("FactionPickerFactionAmerica", "America", None), ("FactionPickerFactionAmericaSuperWeaponGeneral", "America", "reticle"),
    ("FactionPickerFactionAmericaLaserGeneral", "America", "laser"), ("FactionPickerFactionAmericaAirForceGeneral", "America", "jet"),
    ("FactionPickerFactionChina", "China", None), ("FactionPickerFactionChinaTankGeneral", "China", "tread"),
    ("FactionPickerFactionChinaInfantryGeneral", "China", "helmet"), ("FactionPickerFactionChinaNukeGeneral", "China", "atom"),
    ("FactionPickerFactionBossGeneral", "China", "crown"), ("FactionPickerFactionGLA", "Gla", None),
    ("FactionPickerFactionGLAToxinGeneral", "Gla", "trefoil"), ("FactionPickerFactionGLADemolitionGeneral", "Gla", "charge"),
    ("FactionPickerFactionGLAStealthGeneral", "Gla", "shroud"), ("FactionPickerFactionTurkiye", "Turkiye", None),
    ("FactionPickerObserver", "Observer", "eye"),
]
# the retail GeneralImage names the same cards can stand for
GENERAL_IMAGE = {"USA_Logo": "FactionAmerica", "USA_Superweapon": "FactionAmericaSuperWeaponGeneral", "USA_Laser": "FactionAmericaLaserGeneral",
                 "USA_Air": "FactionAmericaAirForceGeneral", "China_Logo": "FactionChina", "China_Tank": "FactionChinaTankGeneral",
                 "China_Infantry": "FactionChinaInfantryGeneral", "China_Nuke": "FactionChinaNukeGeneral", "GLA_Logo": "FactionGLA",
                 "GLA_Toxin": "FactionGLAToxinGeneral", "GLA_Demo": "FactionGLADemolitionGeneral", "GLA_Stealth": "FactionGLAStealthGeneral"}
SIDE_ICON = (48, 44)
SIDE_ICONS = [("GameinfoAMRCA", "America", None), ("GameinfoCHINA", "China", None), ("GameinfoGLA", "Gla", None),
              ("GameinfoTURKIYE", "Turkiye", None), ("GameinfoBOSS", "China", "crown"), ("GameinfoOBSRVR", "Observer", "eye"),
              ("GameinfoRANDOM", "Observer", "random")]
SCORE_EMBLEMS = [("SSScore" + n[len("SSObserver"):], side, glyph) for n, side, glyph in SIDE_EMBLEMS]
SCORE_EMBLEM_K = 4 / 3                          # 45x30 to 60x40
FACTION_EMBLEMS_SIZE = (1024, 1024)


def palette_of(side_name):
    return OBSERVER if side_name == "Observer" else SIDES[side_name]


def mark_of(build):
    """The side mark a build carries: the observer's console has the eye."""
    return "eye" if build == "console" else build


def faction_card(side_name, glyph):
    """A 240x240 card: the side's plate in its build, the glyph raised in a sunk well (a side alone: its
    mark on the accent field), and a side plate under it with the mark and the build's fittings."""
    side = palette_of(side_name)
    b = side["build"]
    pad = 16
    p = Painter(CARD + 2 * pad, CARD + 2 * pad)

    def o(polys):
        return [[(x + pad, y + pad) for x, y in q] for q in polys]

    def raise_mask(parts, box, height, bevel, material):
        m = glyph_mask(parts, CARD + 2 * pad, CARD + 2 * pad, (box[0] + pad, box[1] + pad, box[2] + pad, box[3] + pad), SS)
        d = ndimage.distance_transform_edt(np.pad(m, 1))[1:-1, 1:-1] / SS
        p.h += np.clip(d / bevel, 0, 1) * m * height
        p.mat[m] = material

    c = {"machined": 30, "terraced": 12, "scavenged": 16, "faceted": 18, "console": 18}[b]
    if b == "scavenged":
        body = jitter(shape(b, 6, 6, 234, 234, c), 29, amp=3.0, step=16)
    else:
        body = shape(b, 4, 4, 236, 236, c)
    sl, m, _ = p.frame(o([body]), 9.0, b, DARK)
    p.limit[sl] |= m
    if b == "machined":                                 # the satin frame on the dark flange
        p.frame(o([shape(b, 12, 12, 228, 228, 24)]), 13.0, b, STEEL)
    elif b == "scavenged":                              # scrap sheets laid on out of square
        for poly in (sheet(12, 12, 150, 126, -2.5), sheet(96, 16, 228, 150, 2.0), sheet(14, 116, 226, 226, -1.2)):
            p.frame(o([poly]), 12.0, b, STEEL)
    elif b in ("faceted", "console"):
        p.frame(o([shape(b, 12, 12, 228, 228, c - 6)]), 13.0, b, STEEL)
    else:
        p.mat[sl][m] = STEEL
    field_mat, mark_mat = (ACCENT2, ACCENT) if b == "faceted" else (ACCENT, PAPERM)
    glyph_mat = BRIGHT if b == "faceted" else PAPERM
    well = (26, 24, 214, 170)
    wsl, wm = p.local(o([shape(b, *grow(well, 3), 12)]))   # no sheet's or terrace's step reaches the glass
    p.h[wsl][wm] = 12.0
    if glyph is None:
        p.inlay(o([shape(b, *well, 10)]), field_mat, 0.8)
        raise_mask(emblem_glyph(mark_of(b)), (60, 36, 180, 158), 3.0, 1.4, mark_mat)
    else:
        p.sink(o([shape(b, *well, 10)]), 6.0, 3.0)
        raise_mask(emblem_glyph(glyph), (52, 36, 188, 158), 3.0, 1.4, glyph_mat)
    plate_r = (26, 180, 214, 216)                       # the side plate: the mark on a chip, then the fittings
    p.inlay(o([shape(b, *plate_r, 6)]), field_mat if b != "console" else DARK, 0.6)
    p.frame(o([shape(b, 30, 183, 62, 213, 4)]), 11.0, b, STEEL)
    raise_mask(emblem_glyph(mark_of(b)), (34, 187, 58, 209), 1.6, 0.8, mark_mat if b != "console" else PAPERM)
    if b == "machined":
        for i in range(4):
            p.inlay(o(chevrons(84 + 34 * i, 189, 22, n=1, up=True)), PAPERM, 0.3)
        for x, y in ((40, 18), (200, 18), (40, 222), (200, 222)):
            p.stud(x + pad, y + pad, 3.4)
    elif b == "terraced":
        for i in range(4):                              # stacked ingots
            x = 88 + 32 * i
            p.bump(o([plate(x, 186, x + 22, 196), plate(x + 4, 198, x + 18, 208)]), 1.6, 0.8, BRIGHT)
        for x in range(36, 210, 24):
            p.rivet(x + pad, 10 + pad, 2.0)
            p.rivet(x + pad, 230 + pad, 2.0)
    elif b == "scavenged":
        for i in range(7):                              # hazard slashes along the side plate
            x = 80 + 18 * i
            p.inlay(o([[(x, 182), (x + 9, 182), (x + 2, 214), (x - 7, 214)]]), INKM if i % 2 else ACCENT2, 0.3)
        p.weld((97 + pad, 13 + pad), (97 + pad, 21 + pad), 3, 1.6)       # welds in the margins, off the glass
        p.weld((14 + pad, 117 + pad), (23 + pad, 117 + pad), 4, 1.6)
        p.weld((217 + pad, 116 + pad), (226 + pad, 116 + pad), 5, 1.6)
        for x, y, s in ((20, 20, 1), (222, 24, 2), (18, 222, 3), (220, 224, 4)):
            p.bolt(x + pad, y + pad, 3.6, s)
    elif b == "faceted":
        for i in range(4):                              # triangle markers pointing in along the plate
            p.inlay(o([triangle(92 + 30 * i, 198, 16, 22, "right")]), ACCENT, 0.3)
        for x, y in ((20, 120), (220, 120)):
            p.inlay(o([triangle(x, y, 10, 18, "right" if x < 120 else "left")]), ACCENT, 0.3)
        p.facets(22.0, 0.035, 0.5, 43)
        for x, y in ((40, 18), (200, 222)):
            p.bolt(x + pad, y + pad, 3.2, 0)
    else:                                               # the console: a grille, screws and the tally lamp
        p.raise_plate(o([plate(78, 188, 190, 208)]), 0.5, 0.5, DARK)
        for x in np.arange(84, 186, 6.5):
            for y in (193, 203):
                p.sink(o([regular(x, y, 1.6, 8)]), 1.5, 0.6, INKM)
        for x, y in ((24, 18), (216, 18), (24, 222), (216, 222)):
            screw(p, x + pad, y + pad, 3.0)
        p.raise_plate(o([regular(202, 198, 5, 16)]), 15.0, 1.0, ACCENT2)
    return shade(p, side, shadow=False).crop((pad, pad, pad + CARD, pad + CARD))


def side_icon(side_name, glyph):
    """A 48x44 side icon: the badge's plate in the build, the accent field and the mark (or the glyph)."""
    side = palette_of(side_name)
    b = side["build"]
    W, H = SIDE_ICON
    pad = 8
    p = Painter(W + 2 * pad, H + 2 * pad)

    def o(polys):
        return [[(x + pad, y + pad) for x, y in q] for q in polys]

    body = emblem_body(b, (1, 1, 47, 43)) if b != "console" else shape(b, 1, 1, 47, 43, 6)
    sl, m, _ = p.frame(o([body]), 3.0, b, DARK, scale=0.4 if b != "scavenged" else 1.0)
    p.limit[sl] |= m
    p.frame(o([shape(b, 3, 3, 45, 41, 4)]), 4.4, b, STEEL, scale=0.35)
    field_mat, mark_mat = (ACCENT2, ACCENT) if b == "faceted" else (DARK, PAPERM) if b == "console" else (ACCENT, PAPERM)
    p.inlay(o([shape(b, 6, 5.5, 42, 38.5, 3)]), field_mat, 0.3)
    mk = glyph_mask(emblem_glyph(glyph or mark_of(b)), W + 2 * pad, H + 2 * pad, (11 + pad, 8.5 + pad, 37 + pad, 35.5 + pad), SS)
    d = ndimage.distance_transform_edt(np.pad(mk, 1))[1:-1, 1:-1] / SS
    p.h += np.clip(d / 0.5, 0, 1) * mk * 1.1
    p.mat[mk] = mark_mat
    if b == "faceted":
        p.facets(6.0, 0.05, 0.2, 47)
    return shade(p, side, shadow=False).crop((pad, pad, pad + W, pad + H))


def paint_faction_emblems():
    """ReforgedFactionEmblems: every card, side icon and score emblem where its INI maps it; the
    GeneralImage names stand on their cards."""
    M = read_mapped(None, name="ReforgedFactionEmblems")
    out = Image.new("RGBA", FACTION_EMBLEMS_SIZE, (0, 0, 0, 0))
    for name, side, glyph in FACTION_CARDS:
        put(out, faction_card(side, glyph), M[name])
    for name, side, glyph in SIDE_ICONS:
        put(out, side_icon(side, glyph), M[name])
    for name, side, glyph in SCORE_EMBLEMS:
        put(out, emblem_badge(side, glyph, SCORE_EMBLEM_K), M[name])
    for alias, template in GENERAL_IMAGE.items():
        assert M[alias] == M["FactionPicker" + template], "%s is not on %s's card" % (alias, template)
    check_mapped({n: r for n, r in M.items() if n not in GENERAL_IMAGE}, FACTION_EMBLEMS_SIZE)
    return out


def check_mapped(M, size):
    """No two mapped pieces share a texel, their one texel gutters included, and all are on the sheet."""
    boxes = sorted((grow(r, 1), n) for n, r in M.items())
    for i, (p, n) in enumerate(boxes):
        assert p[0] >= 0 and p[1] >= 0 and p[2] <= size[0] and p[3] <= size[1], "%s is off its %s sheet" % (n, size)
        for q, m in boxes[i + 1:]:
            assert p[2] <= q[0] or q[2] <= p[0] or p[3] <= q[1] or q[3] <= p[1], "%s and %s overlap" % (n, m)


def check_observer(bar, panel):
    """The cuts stay clear, every window the bar and the panel frame stands on plate, nothing is opaque more
    than 3 px past its blockers (the column's, the selection's and the mount's; the radar's follows its frame
    at runtime), and Observer8x6 names the radar frame's and the panel's."""
    a = np.asarray(bar)[..., 3]
    for x in SEAMS:
        assert not a[:, x - 2:x + 3].any(), "observer art across the cut at texel %d" % x
    for r in [OBS_OPTIONS, OBS_CHAT, OBS_SELECTION, OBS_EXP] + OBS_LIST:
        x0, y0, x1, y1 = to_bar(r)
        assert (a[y0:y1, x0:x1] == 255).all(), "observer window %s is not on the plate" % (r,)
    for img, blocks, at in ((a[:, :1920], OBS_BLOCKS, (0, TOP)), (np.asarray(panel)[..., 3], OBS_PANEL_BLOCKS, OBS_PANEL_AT)):
        ok = np.zeros(img.shape, bool)
        for x0, y0, x1, y1 in blocks:
            ok[max(y0 - at[1] - 3, 0):max(y1 - at[1] + 3, 0), max(x0 - at[0] - 3, 0):max(x1 - at[0] + 3, 0)] = True
        assert not ((img >= 250) & ~ok).any(), "observer plate past its blockers at %s" % (np.argwhere((img >= 250) & ~ok)[0],)
    pa = np.asarray(panel)[..., 3]
    for r in [OBS_POWER, OBS_NAME, OBS_MONEY, OBS_OPTIONS, OBS_CHAT] + OBS_LABELS + OBS_NUMBERS:
        x0, y0, x1, y1 = to_panel(r)
        assert (pa[y0:y1, x0:x1] == 255).all(), "panel window %s is not on the plate" % (r,)
    # Observer8x6 names the radar's frame at the square map (fitRadarToMap stands the radar on its top) and
    # the panel's blocker, which runs to the screen's foot with the art
    text = open(SCHEME_INI, encoding="latin-1").read()
    body = re.search(r"ControlBarScheme Observer8x6\b(.*?)(?=\nControlBarScheme |\Z)", text, re.S).group(1)
    named = {k + c: (int(x), int(y)) for k, c, x, y in re.findall(r"Block(\w+?)(UL|LR) X:(\d+) Y:(\d+)", body)}
    l, t, r, b = OBS_RADAR_EDGE
    want = {"Radar": (0, RADAR_GLASS_SQUARE[1] - t, RADAR_GLASS_SQUARE[2] + r, 1080), "ObserverInfo": OBS_PANEL_BLOCKS[0]}
    for k, v in want.items():
        assert named.get(k + "UL", ()) + named.get(k + "LR", ()) == v, \
            "Observer8x6 wants Block%sUL X:%d Y:%d and Block%sLR X:%d Y:%d" % (k, v[0], v[1], k, v[2], v[3])


# The observer's radar sized to the map like the sides' (RadarFrameImage ObserverProRadar on
# Observer8x6): the left part of America's radar box, round 20.  5 down the left and the foot, which are the
# screen's; 39 over the top, where the ledge runs over the box (714..742, the box itself from 731); 33 on the
# right, where the ledge ends in its arrow point over the players' column.  Below the ledge the right edge is
# the box's lip five texels wide and the rest of it is empty: the column (the bar's left piece) stands there,
# open on its side, so the two read as one box.  At the square map the frame is 0..360 x 714..1080, which is
# Observer8x6's BlockRadar (fitRadarToMap stands the radar on it, so the square keeps its place).  The ledge
# runs the frame's whole width, so T stretches it evenly; its fittings (two swept chevrons) are in TR.  Drawn
# on the bar's 512 rows (screen y 568..1080) with the bar's top to bottom falloff, so the ledge's lip and the
# column's meet at the same shade.
OBS_RADAR_EDGE = (5, 39, 33, 5)
RADAR_GLASS_SQUARE = (5, 753, 327, 1075)       # Observer8x6's RadarUL/LR, LeftHUD's square


def observer_radar_frame():
    l, t, r, b = OBS_RADAR_EDGE
    W, H = l + RADAR_INSIDE + r, t + RADAR_INSIDE + b
    top, right = 1080 - H, l + RADAR_INSIDE
    p = Painter(W + 40, 512)                    # the right side runs off the canvas, so it takes no rim
    stack(p, plate(0, top + 17, W + 40, 1080))
    tip = right + r - 2
    stack(p, [(0, top), (right, top), (tip, top + 14), (right, top + 28), (0, top + 28)], 9, 12, 4, 2.0)
    floor(p, plate(0, top + 34, W + 40, 1080))
    well(p, plate(*grow((l, top + t, right, 1080 - b), 2)), 5, 2)
    p.inlay([T([(right + 1, top + 8), (right + 4, top + 8), (right + 10, top + 14), (right + 4, top + 20), (right + 1, top + 20),
                (right + 7, top + 14)])], DARK, 0.7)      # a swept chevron in the point, America's
    edge = right + l                            # the box's lip ends here; the column stands right of it
    rise = 14 * (edge - right) / (tip - right)  # how far the arrow's edges have closed in at the lip's end
    p.cut([T(plate(edge, top + 28, W + 40, 1080)),
           T([(edge, top - 2), (W + 40, top - 2), (W + 40, top + 14), (tip, top + 14), (edge, top + rise)]),
           T([(edge, top + 28 - rise), (tip, top + 14), (W + 40, top + 14), (W + 40, top + 28), (edge, top + 28)])])
    img = bar_finish(shade(p, OBS_LOOK, shadow=False), p, OBS_LOOK, scan=False, gloss=False)
    return img.crop((0, top - TOP, W, 512))


# ---------------------------------------------------------------------------------------------- the spectator's rack

# The spectator's controls (InGameUI's runSpectatorAction, gatherSpectatorStats), round 11's concept 2,
# approved on 2026-11-03: drawers hung off the right edge 8 px under the superweapon tray's foot.  A watch
# drawer with a three way camera lever and a fog rocker, a standings drawer with seven stat keys (round
# 21, owner's pick C, in place of round 11's rotary dial) and its rows, an index tab out of each row to follow that player, and an armies drawer sliding out left on
# two runners.  Each drawer is drawn whole round two reference rows, lit with no top to bottom falloff,
# and cut into the slices the code stacks (a cap, a head, a row it repeats, a plain body, a foot); every
# control is a piece of its own in each state.  The code writes every word and number and tints the
# white lenses in the player's colour.  Since round 20 the drawers and their controls are the GLA's scrap
# (rack_reference), in the observer's neutral steel.


class Offset:
    """A Painter seen with its origin moved: every polygon argument is shifted by (dx, dy)."""

    def __init__(self, painter, dx, dy):
        self.painter, self.dx, self.dy = painter, dx, dy

    def __getattr__(self, name):
        method = getattr(self.painter, name)
        if name in ("stud", "rivet", "bolt"):           # a fitting at a point
            return lambda cx, cy, *args, **kwargs: method(cx + self.dx, cy + self.dy, *args, **kwargs)
        if name == "weld":                              # a bead between two points
            return lambda a, b, *args, **kwargs: method((a[0] + self.dx, a[1] + self.dy), (b[0] + self.dx, b[1] + self.dy),
                                                        *args, **kwargs)

        def shifted(polys, *args, **kwargs):
            return method([[(x + self.dx, y + self.dy) for x, y in poly] for poly in polys], *args, **kwargs)
        return shifted


def lit(w, h, draw, shadow=False, pad=24):
    """A w x h piece drawn by draw() in its own coordinates on a canvas pad wider each way (shade's mottle
    needs the room, and a side on the screen's edge runs on past the crop so it takes no bevel), lit
    with no falloff so a cut row tiles, and cropped back."""
    p = Painter(w + 2 * pad, h + 2 * pad)
    draw(Offset(p, pad, pad))
    p.limit[:] = True
    img = bar_finish(shade(p, OBS_LOOK, shadow=shadow, falloff=False), p, OBS_LOOK, gloss=False)
    return img.crop((pad, pad, pad + w, pad + h))


def lens(p, cx, cy, r, mat=GLASS):
    """A round lamp in a dark bezel, raised so it stands on an otherwise empty overlay too."""
    p.raise_plate([regular(cx, cy, r + 1.3, 16)], 1.0, 0.8, DARK)
    p.raise_plate([regular(cx, cy, r, 16)], 1.2, 0.5, mat)
    if mat != GLASS:
        p.raise_plate([regular(cx - r * 0.35, cy - r * 0.35, r * 0.3, 8)], 1.6, 0.3, PAPERM)


def white_lens(w, h):
    """White, lit along its top, for the code to tint in a player's colour."""
    v = np.linspace(255, 150, h).astype(np.uint8)[:, None].repeat(w, 1)
    return Image.fromarray(np.dstack([v, v, v, np.full_like(v, 255)]), "RGBA")


RACK_W, RACK_TOP, RACK_HEAD, RACK_PITCH, RACK_FOOT = 306, 28, 68, 30, 14
RACK_AT = 1920 - RACK_W
# a standings row (round 21, owner's pick C): one well, the player's colour as a stripe at its left, his place,
# emblem, name and number, a meter of his share under the name; the code paints and writes all of it
RACK_ROW = dict(well=(20, 2, 296, 28), stripe=(22, 4, 25, 26), place=(26, 2, 40, 28), emblem=(42, 5, 72, 25),
                name=(76, 2, 190, 22), value=(180, 2, 294, 22), meter=(76, 23, 292, 25))
# the head: seven stat keys in a row, the picked stat's name as a heading, the column heads over a hairline
RACK_STAT_KEYS = "QWERTYA"
RACK_STAT_W, RACK_STAT_PITCH = 36, 39
RACK_STAT_AT = [(23 + RACK_STAT_PITCH * i, 3, 23 + RACK_STAT_PITCH * i + RACK_STAT_W, 29) for i in range(len(RACK_STAT_KEYS))]
RACK_HEAD_AT = dict(heading=(22, 32, 294, 48), columns=(22, 51, 294, 65), rule=(22, 65, 294, 66))
# the code's text colours (InGameUI drawSpectatorRack), which the keys' glyphs and rims match
RACK_GREY, RACK_WHITE, RACK_LIT, RACK_GOLD = (138, 148, 159), (240, 244, 246), (128, 149, 234), (242, 194, 48)
RACK_LABEL = (24, 9, 180, 23)
DRAWER_W, DRAWER_TOP = 276, 12
DRAWER_CAMEOS = [(22 + 39 * i, 3, 58 + 39 * i, 27) for i in range(6)]   # all six clear of the standings' edge
# the follow tab: its size, and where its body starts in it by state (the rest tucks under the drawer)
RACK_TAB = (40, 26)
RACK_TAB_OUT = {"": 22, "H": 15, "On": 2}


def rack_reference(rows=2, body=12):
    """A drawer with everything a slice is cut from: the cap with its label, the standings' head with the
    stat keys' wells and the column heads' rule, rows, a plain body for the watch drawer, and the foot.  The GLA's (round 20): the cap a
    ragged ledge of scrap standing out past the drawer toward the field with a patch welded on crooked, the
    drawer's corners cut unevenly, its head's edge bitten, odd bolts.  The rows and the body keep a straight
    edge, since the code repeats and stretches them."""
    b = "scavenged"
    H = RACK_TOP + RACK_HEAD + rows * RACK_PITCH + body + RACK_FOOT

    def draw(p):
        # shell and lip, the right side running on past the crop (the screen's edge); under the cap the
        # standings and the watch drawer's body are one recessed floor
        rows_at = RACK_TOP + RACK_HEAD
        side = [(12, H - 14), (12, rows_at)] + [(12 + bite, y) for y, bite in ((80, 1.6), (62, 0.6), (47, 2.2), (34, 0.8))]
        p.tiers([[(12, 24), (32, 4), (RACK_W + 20, 4), (RACK_W + 20, H - 6), (20, H - 6), (14, H - 10)] + side],
                (False,) * 4)
        p.recess([plate(18, RACK_TOP + 1, RACK_W + 20, H - RACK_FOOT + 1)], (False,) * 4)
        cap = jitter([(0, 5), (14, 0), (RACK_W + 20, 0), (RACK_W + 20, 26), (4, 25)], 41, 1.4, 13)
        p.tiers([cap], (False,) * 4, 9.0, 12.0, 4.0, 2.0)
        p.sink([shape(b, RACK_LABEL[0] - 4, RACK_LABEL[1] - 2, RACK_LABEL[2] + 2, RACK_LABEL[3] + 2, 3)], 4, 1.6)
        sh = sheet(236, 5, 292, 21, -3)
        p.bump([sh], 1.4, 0.8, STEEL)
        for a, c in zip(sh, sh[1:] + sh[:1]):
            p.weld(a, c, seed=171 + int(a[0]), r=1.1)
        p.bolt(200, 13, 2.4, 9)
        hy = RACK_TOP
        for x0, y0, x1, y1 in RACK_STAT_AT:
            p.sink([shape(b, x0 + 1.5, hy + y0, x1 - 1.5, hy + y1, 3)], 3, 1.2)
        r = RACK_HEAD_AT["rule"]
        p.inlay([plate(r[0], hy + r[1], r[2], hy + r[3])], INKM, 0.4)
        for k in range(rows):
            y0 = RACK_TOP + RACK_HEAD + k * RACK_PITCH
            r = RACK_ROW["well"]
            p.sink([shape(b, r[0], r[1] + y0, r[2], r[3] + y0, 3)], 4, 1.6)
        p.weld((60, H - 10.5), (250, H - 10.5), 181)
        p.bolt(30, H - 11, 2.6, 10)
        p.bolt(282, H - 11, 3.0, 11)
    return lit(RACK_W, H, draw, shadow=True), H


def drawer_reference(rows=2):
    """The armies drawer, out to the left of the standings on two runners, its rows level with theirs: the
    GLA's scrap like the rack, its corners cut unevenly."""
    b = "scavenged"
    H = DRAWER_TOP + rows * RACK_PITCH + RACK_FOOT

    def draw(p):
        p.tiers([plate(6, 3, DRAWER_W + 20, H - 6, tl=("cut", 8), bl=("notch", 3))], (False,) * 4, 3.0, 6.0, 4.0, 2.0)
        p.recess([plate(18, DRAWER_TOP - 1, DRAWER_W + 20, H - RACK_FOOT + 1)], (False,) * 4, 2.5, 1.5)
        for y in (6, H - 11):                   # the runners, a dark channel with a bright slide in it
            p.sink([plate(20, y, DRAWER_W + 20, y + 4)], 2.0, 0.6, DARK)
            p.bump([plate(28, y + 1.2, DRAWER_W + 20, y + 2.8)], 1.4, 0.5, BRIGHT)
        p.bump([shape(b, 11, 9, 16, H - 12, 2)], 2.6, 1.0, BRIGHT)   # the pull, down the front
        for k in range(rows):
            y0 = DRAWER_TOP + k * RACK_PITCH
            for c in DRAWER_CAMEOS:
                p.sink([shape(b, c[0], c[1] + y0, c[2], c[3] + y0, 3)], 3, 1.2)
    return lit(DRAWER_W, H, draw, shadow=True), H


def lever(position, state):
    """The camera's three way lever: a gate with a ball in it at Free, Director or Player."""
    w, h = 156, 32
    cx = (22, 78, 134)[position]

    def draw(p):                                # the GLA's gate, every corner cut its own way
        p.tiers([shape("scavenged", 0, 2, w, 30, 8)], (False,) * 4, 3.0, 5.0, 2.0, 1.2, 2.0)
        p.sink([shape("scavenged", 6, 8, w - 6, 24, 5)], 3.5, 1.2, GLASS)
        for x in (22, 78, 134):                 # a detent at each stop
            p.sink([plate(x - 2, 24, x + 2, 27.5)], 1.2, 0.5, DARK)
        if state == "H":
            p.raise_plate([regular(cx, 16, 11.5, 24)], 1.5, 0.6, HILITE)
        p.raise_plate([regular(cx, 16, 9.5, 24)], 4.5, 1.0, BRIGHT, profile=rounded(4.5))
    return lit(w, h, draw)


def rocker(on, state):
    """The fog's rocker, the side it is pressed down: I is on, O is off.  Under the pointer the bezel
    brightens, the paddle turns from dark to bare steel and its marks to gold."""
    w, h = 46, 32
    hover = state == "H"

    def draw(p):
        b = "scavenged"
        p.tiers([shape(b, 0, 0, w, h, 6)], (False,) * 4, 3.0, 5.0, 1.8, 1.2, 1.8, BRIGHT if hover else None)
        p.sink([shape(b, 4, 4, w - 4, h - 4, 4)], 3.0, 1.0, DARK)
        p.bump([shape(b, 7, 7, w - 7, h - 7, 3)], 2.6, 1.0, STEEL if hover else DARK)
        p.ramp([shape(b, 7, 7, w - 7, h - 7, 3)], -1.6 if on else 1.6, 1.6 if on else -1.6)
        mark = GOLDM if hover else PAPERM
        p.inlay([plate(14.4, 11, 16, 21)], HILITE if on else mark, 0.3)
        p.inlay([regular(31, 16, 4.2, 16)], mark, 0.3)
        p.bump([regular(31, 16, 2.8, 16)], 0.3, 0.2, STEEL if hover else DARK)
    return lit(w, h, draw)


def index_tab(state):
    """A follow tab out of a row's left end, tucked under the drawer: short, longer when hovered, pulled
    right out with its lamp on air when that player is followed.  The code lights the lens in his colour."""
    w, h = RACK_TAB
    x0 = RACK_TAB_OUT[state]

    def draw(p):
        b = "scavenged"
        p.tiers([plate(x0, 1, w + 10, h - 1, tl=("cut", 6), bl=("notch", 2))], (False,) * 4, 3.0, 5.0, 1.8, 1.2, 1.8,
                BRIGHT if state == "H" else None)
        p.sink([shape(b, x0 + 4, 7, x0 + 9, h - 7, 1.5)], 1.5, 0.6)
        if state == "On":
            lens(p, x0 + 16, h / 2, 2.8, ACCENT2)
    return lit(w, h, draw)


def stat_glyph(stat, cx, cy):
    """A stat's mark, about 18 x 14 round (cx, cy), as polygons: net worth a stack of coins, cash a note,
    income a rising arrow, army a tank side on, kills a reticle, rank three chevrons, power a bolt."""
    if stat == 0:
        return [plate(cx - 7 + i, cy + 4 - 4.5 * i, cx + 7 - i, cy + 6.5 - 4.5 * i) for i in range(3)]
    if stat == 1:
        return [plate(cx - 9, cy - 5, cx + 9, cy - 3), plate(cx - 9, cy + 3, cx + 9, cy + 5), plate(cx - 9, cy - 5, cx - 7, cy + 5),
                plate(cx + 7, cy - 5, cx + 9, cy + 5), regular(cx, cy, 2.4, 10)]
    if stat == 2:
        return stroke([(cx - 8, cy + 5), (cx - 2, cy - 1), (cx + 2, cy + 3), (cx + 7, cy - 3)], 2.2) + [
            [(cx + 9, cy - 6), (cx + 9, cy + 1), (cx + 2, cy - 6)]]
    if stat == 3:
        return [plate(cx - 9, cy + 1, cx + 9, cy + 6), plate(cx - 5, cy - 3, cx + 4, cy + 1), plate(cx + 3, cy - 2, cx + 10, cy - 1)]
    if stat == 4:
        return ring(cx, cy, 5.5, 1.6, 20) + [plate(cx - 9, cy - 0.8, cx - 3, cy + 0.8), plate(cx + 3, cy - 0.8, cx + 9, cy + 0.8),
                                             plate(cx - 0.8, cy - 8, cx + 0.8, cy - 3), plate(cx - 0.8, cy + 3, cx + 0.8, cy + 8)]
    if stat == 5:
        return [[(cx - 7, cy - 4 + 4 * i), (cx, cy - 8 + 4 * i), (cx + 7, cy - 4 + 4 * i), (cx + 7, cy - 2 + 4 * i),
                 (cx, cy - 6 + 4 * i), (cx - 7, cy - 2 + 4 * i)] for i in range(3)]
    return [[(cx + 2, cy - 8), (cx - 5, cy + 1), (cx, cy + 1), (cx - 2, cy + 8), (cx + 5, cy - 1), (cx, cy - 1)]]


def stat_key(ref, stat, state):
    """One of the standings' seven stat keys, cut from the head where its well is so it meets the plate round
    it, its glyph laid on flat: grey, gold under the pointer, and white in a lit rim with a lit underline when
    it is the picked stat.  The code writes the key's letter in its bottom right corner."""
    x0, y0, x1, y1 = RACK_STAT_AT[stat]
    w, h = x1 - x0, y1 - y0
    img = ref.crop((x0, RACK_TOP + y0, x1, RACK_TOP + y1))
    ss = 4                                      # the glyph drawn 4 times over and box filtered
    mask = Image.new("L", (w * ss, h * ss), 0)
    d = ImageDraw.Draw(mask)
    for poly in stat_glyph(stat, w / 2, h / 2 - 1):
        d.polygon([(x * ss, y * ss) for x, y in poly], fill=255)
    colour = {"": RACK_GREY, "H": RACK_GOLD, "On": RACK_WHITE}[state]
    img.paste(Image.new("RGBA", (w, h), colour + (255,)), (0, 0), mask.resize((w, h), Image.BOX))
    if state == "On":
        d = ImageDraw.Draw(img)
        d.rectangle((2, 1, w - 3, h - 2), outline=RACK_LIT + (255,))
        d.rectangle((6, h - 4, w - 7, h - 3), fill=RACK_LIT + (255,))
    return img


def rack_pieces():
    ref, H = rack_reference()
    rows_at = RACK_TOP + RACK_HEAD
    body_at = rows_at + 2 * RACK_PITCH
    drawer, DH = drawer_reference()
    out = {"RackTop": ref.crop((0, 0, RACK_W, RACK_TOP)), "RackHead": ref.crop((0, RACK_TOP, RACK_W, rows_at)),
           "RackRow": ref.crop((0, rows_at, RACK_W, rows_at + RACK_PITCH)), "RackBody": ref.crop((0, body_at + 4, RACK_W, body_at + 8)),
           "RackBottom": ref.crop((0, H - RACK_FOOT, RACK_W, H)),
           "DrawerTop": drawer.crop((0, 0, DRAWER_W, DRAWER_TOP)),
           "DrawerRow": drawer.crop((0, DRAWER_TOP, DRAWER_W, DRAWER_TOP + RACK_PITCH)),
           "DrawerBottom": drawer.crop((0, DH - RACK_FOOT, DRAWER_W, DH)), "RackLens": white_lens(5, 12),
           "RackMeter": white_lens(16, 4)}
    for i, pos in enumerate(("Free", "Director", "Player")):
        for st in ("", "H"):
            out["RackLever%s%s" % (pos, st)] = lever(i, st)
    for on in (False, True):
        for st in ("", "H"):
            out["RackRocker%s%s" % ("On" if on else "Off", st)] = rocker(on, st)
    for i, key in enumerate(RACK_STAT_KEYS):
        for st in ("", "H", "On"):
            out["RackStat%s%s" % (key, st)] = stat_key(ref, i, st)
    for st in ("", "H", "On"):
        out["RackTab" + st] = index_tab(st)

    def tally_piece(p):
        lens(p, 7, 7, 3.2, ACCENT2)
    out["RackTally"] = lit(14, 14, tally_piece)
    return out


RACK_TILES = ("RackRow", "RackBody", "DrawerRow")


def paint_rack():
    """ReforgedHudObsSpec, 1024x256: every rack piece where ReforgedHudObsSpec.ini maps it.  A slice the
    code repeats has to meet itself: its first and last rows cover the same texels."""
    pieces = rack_pieces()
    M = read_mapped("Obs", "Spec")
    assert set(M) == {"ObserverPro" + n for n in pieces}, "ReforgedHudObsSpec.ini and the rack's pieces differ: %s" % \
        sorted(set(M) ^ {"ObserverPro" + n for n in pieces})
    sheet_img = Image.new("RGBA", OBS_SPEC_SIZE, (0, 0, 0, 0))
    for n, im in pieces.items():
        put(sheet_img, im, M["ObserverPro" + n])
    check_mapped(M, OBS_SPEC_SIZE)
    for n in RACK_TILES:
        a = np.asarray(pieces[n])[..., 3] > 127
        assert (a[0] == a[-1]).all(), "%s does not tile: its ends differ" % n
    for left, right in zip(RACK_STAT_AT, RACK_STAT_AT[1:]):
        assert right[0] - left[2] >= 3, "stat keys %s and %s are under 3 apart" % (left, right)
    heading = RACK_HEAD_AT["heading"]
    assert RACK_STAT_AT[0][0] >= heading[0] and RACK_STAT_AT[-1][2] <= heading[2], "the stat keys run past the heading's width"
    assert max(r[3] for r in RACK_STAT_AT) <= heading[1], "the stat keys reach into the heading"
    well = RACK_ROW["well"]
    for name, r in RACK_ROW.items():
        assert well[0] <= r[0] and r[2] <= well[2] and well[1] <= r[1] and r[3] <= well[3], \
            "the row's %s %s is outside its well %s" % (name, r, well)
    return sheet_img


# ---------------------------------------------------------------------------------------------- checks

def scheme_blocks(scheme="America8x6"):
    """The Block<Name>UL/LR rectangles one scheme of ControlBarProReforged.ini names, in screen pixels."""
    text = open(SCHEME_INI, encoding="latin-1").read()
    body = re.search(r"ControlBarScheme %s\b(.*?)(?=\nControlBarScheme |\Z)" % scheme, text, re.S).group(1)
    v = {k + c: (int(x), int(y)) for k, c, x, y in re.findall(r"Block(\w+?)(UL|LR) X:(\d+) Y:(\d+)", body)}
    return {k: v[k + "UL"] + v[k + "LR"] for k in ("Radar", "Production", "Money", "Upgrades", "GenPower")}


def side_schemes(side_name):
    """Every scheme that draws the side's bar: the side's own and its generals'."""
    text = open(SCHEME_INI, encoding="latin-1").read()
    return [m.group(1) for m in re.finditer(r"ControlBarScheme (\w+)(.*?)(?=\nControlBarScheme |\Z)", text, re.S)
            if re.search(r"ImageName %sProCommandBar\b" % side_name, m.group(2))]


def bar_blocks(solid):
    """The input blockers the art asks for, in screen pixels: each the bounding box of its plate.  The
    centre parts where the rail's top is highest outside the money tab (Money above, Production below),
    the right where the selection window's box begins (GenPower, the mount, above; Upgrades below)."""
    def box(m, x_off, y_off):
        ys, xs = np.nonzero(m)
        return (int(xs.min()) + x_off, int(ys.min()) + y_off + TOP, int(xs.max()) + 1 + x_off, int(ys.max()) + 1 + y_off + TOP)
    tops = lambda m: np.argmax(m, axis=0)       # noqa: E731
    centre = solid[:, SEAMS[0]:SEAMS[1]]
    cols = [x - SEAMS[0] for x in range(SEAMS[0], SEAMS[1]) if not TAB0 - 70 <= x < TAB2 + 70 and solid[:, x].any()]
    split = int(tops(centre[:, cols]).min())
    right = solid[:, SEAMS[1]:]
    rsplit = int(tops(right[:, RIGHT_BOX[0] + 24 - SEAMS[1]:MOUNT[0] - 4 - SEAMS[1]]).min())
    return {"Radar": box(solid[:, :SEAMS[0]], 0, 0), "Production": box(centre[split:], SEAMS[0], split),
            "Money": box(centre[:split], SEAMS[0], 0), "Upgrades": box(right[rsplit:], SEAMS[1], rsplit),
            "GenPower": box(right[:rsplit], SEAMS[1], 0)}


def check(img, side_name="America"):
    """The two cuts stay clear, every window the bar or the code draws into stands on the plate (the radar
    square included: it stands in LeftHUD and letterboxes the map), and the side's schemes' input blockers
    are the plates' bounding boxes."""
    a = np.asarray(img)[..., 3]
    for x in SEAMS:
        assert not a[:, x - 2:x + 3].any(), "art across the cut at texel %d" % x
    L = layout()
    for r in [L[n] for n in ("radar", "money", "power", "portrait", "exp", "minmax", "general")] + L["cells"] + L["tools"] + L["idle"]:
        assert (a[r[1]:r[3], r[0]:r[2]] == 255).all(), "window %s is not on the plate" % (r,)
    want = bar_blocks(a >= 250)
    for scheme in side_schemes(side_name):
        have = scheme_blocks(scheme)
        assert have == want, "%s's blockers %s, the art asks for %s" % (scheme, have, want)


def check_sheet(sheet_img, side_name, glow="B"):
    """Every piece the code looks up is on the sheet at the size the code assumes, no two pieces share a
    texel (with a stretched piece's gutter), the tray's cell is plate under its power, and the
    mirrored and flipped tray pieces were lit again rather than flipped."""
    M = read_mapped(side_name)
    pre = side_name + "Pro"
    a = np.asarray(sheet_img)[..., 3]

    def size(n):
        x0, y0, x1, y1 = M[pre + n]
        return x1 - x0, y1 - y0

    l, t, r, b = RADAR_EDGE
    want = {"RadarTL": (l, t), "RadarT": (RADAR_INSIDE, t), "RadarTR": (r, t), "RadarL": (l, RADAR_INSIDE),
            "RadarC": (RADAR_GLASS, RADAR_GLASS), "RadarR": (r, RADAR_INSIDE), "RadarBL": (l, b), "RadarB": (RADAR_INSIDE, b),
            "RadarBR": (r, b), "TrayCell": TRAY_CELL, "TrayEdgeTop": (TRAY_CELL[0], TRAY_TOP),
            "TrayEdgeLeft": (TRAY_LEFT, TRAY_CELL[1]), "TrayEdgeBottom": (TRAY_CELL[0], TRAY_FOOT),
            "TrayCornerTopLeft": (TRAY_LEFT, TRAY_TOP), "TrayCornerBottomLeft": (TRAY_LEFT, TRAY_FOOT),
            "TrayStep": (TRAY_LEFT, TRAY_TOP), "TrayEdgeRight": (TRAY_LEFT, TRAY_CELL[1]),
            "TrayCornerTopRight": (TRAY_LEFT, TRAY_TOP), "TrayCornerBottomRight": (TRAY_LEFT, TRAY_FOOT),
            "TrayStepRight": (TRAY_LEFT, TRAY_TOP), "TrayStepDown": (TRAY_LEFT, TRAY_FOOT), "HudCellPlate": HUD_PLATE,
            "TrayTabDown": TRAY_TAB, "TrayTabUp": TRAY_TAB, "HudReadyGlow": READY_GLOW, "HudReadyShimmer": READY_GLOW}
    for k in ("Infantry", "Vehicle", "Aircraft"):
        for st in ("", "H", "P", "I"):
            want["Idle" + k + st] = (54, 35)
    for st in ("", "H", "I"):
        want["GenPowerClose" + st] = (60, 50)
    want.update(tip_sizes())
    for n, wh in want.items():
        assert pre + n in M, "%s%s is not on the sheet" % (pre, n)
        assert size(n) == wh, "%s%s is %s, the code wants %s" % (pre, n, size(n), wh)
    check_tip_head(sheet_img, M, pre)
    boxes = [(n, grow(v, 1)) for n, v in M.items() if not n.endswith("TraySmallTop")]
    for i, (n, p) in enumerate(boxes):
        for m, q in boxes[i + 1:]:
            assert p[2] <= q[0] or q[2] <= p[0] or p[3] <= q[1] or q[3] <= p[1], "%s and %s overlap" % (n, m)
    x0, y0, x1, y1 = M[pre + "TrayCell"]
    bx, by = (TRAY_CELL[0] - 75) // 2, (TRAY_CELL[1] - 60) // 2
    assert (a[y0 + by:y0 + by + 60, x0 + bx:x0 + bx + 75] == 255).all(), "the tray's cell has a hole under its power"
    # the mirrored and flipped pieces are lit from the top left too: a bevel facing away from the light
    # is darker than the one it mirrors, which a flipped copy of the pixels would not be
    lum = np.asarray(sheet_img.convert("L"), np.float32)

    def piece(n):
        x0, y0, x1, y1 = M[pre + n]
        return lum[y0:y1, x0:x1]
    assert piece("TrayEdgeRight")[:, -3:].mean() < piece("TrayEdgeLeft")[:, :3].mean() - 4, "TrayEdgeRight is lit from the right"
    assert piece("TrayStepDown")[-3:, :3].mean() < piece("TrayStep")[:3, :3].mean() - 4, "TrayStepDown is lit from below"
    # the "+N" tabs: something solid over the tray's edge to pin it there, and the code's light text on
    # a dark, even field (InGameUI.cpp drawHudTab's room, a texel in from each side)
    j = TRAY_TAB_JOIN
    for n, pin, room in (("TrayTabDown", slice(0, j), (slice(11, 23), slice(10, 30))),
                         ("TrayTabUp", slice(TRAY_TAB[1] - j, TRAY_TAB[1]), (slice(5, 17), slice(10, 30)))):
        x0, y0, x1, y1 = M[pre + n]
        assert (a[y0:y1, x0:x1][pin] == 255).any(axis=1).all(), "%s%s has nothing to pin it in its join rows" % (pre, n)
        field = piece(n)[room]
        assert field.mean() < 70 and field.std() < 22, "%s%s's text field is %.0f +- %.0f, too bright or busy" % (pre, n, field.mean(), field.std())
    # the ready glow: added and tinted by the code, so grey on black, opaque, white at the foot, near
    # black over its top 15%; B's band is opaque grey too
    for n in [k for k in READY_GLOW_AT if glow == "B" or k == "HudReadyGlow"]:
        x0, y0, x1, y1 = M[pre + n]
        g = np.asarray(sheet_img)[y0:y1, x0:x1].astype(np.int32)
        assert (g[..., 3] == 255).all() and (g[..., 0] == g[..., 1]).all() and (g[..., 1] == g[..., 2]).all(), \
            "%s%s is not opaque grey" % (pre, n)
        if n == "HudReadyGlow":
            assert g[:int(READY_GLOW[1] * 0.15) + 1, :, 0].max() <= 8, "%sHudReadyGlow's top 15%% is not near black" % pre
            assert g[-1, :, 0].min() >= 240, "%sHudReadyGlow's bottom row is not white" % pre


# ---------------------------------------------------------------------------------------------- the guard shield

# The shield over a guarding unit's head (W3DInGameUI::drawGuardMarkers, GitHub #165).  It used to be filled one
# screen row at a time, three shapes of covered spans, 60 to 180 rectangles a unit a picture; now it is one
# picture, GuardShield<h> for every whole height h the code asks for, drawn texel for pixel.  guard_shield
# rebuilds the old fill exactly: the same rows, the same covered ends, the same colours laid on in the same
# order, so the picture over the field is the one the rectangles made.  A sprite is 2K+1 wide round the
# pixel column the shield is centred on (K = ceil(rim + width / 2)), h + inset high from the shield's top.
GUARD_HEIGHTS = range(6, 41)                    # 6 x UI scale (1.0) to 10 x 4K's 3.6, rounded up
GUARD_SHOULDER = 0.4
GUARD_SHADOW, GUARD_EDGE, GUARD_LIT, GUARD_SHADED = 0x60000000, 0xE00C1014, 0xF0E8EEF4, 0xF0939FAC
GUARD_SHEET = (512, 256)


def guard_geometry(h):
    """The shield's width, rim and inset at height h, as drawGuardMarkers works them out."""
    width = h * 0.8
    rim = max(1.0, h / 10.0)
    return width, rim, int(rim)


def guard_shield(h):
    width, rim, inset = guard_geometry(h)
    k = int(np.ceil(rim + width / 2))
    w, hh = 2 * k + 1, h + inset
    premul = np.zeros((hh, w, 4), np.float64)       # rgb times alpha, and alpha, laid on rectangle by rectangle

    def fill(x, y, n, colour, alpha):
        if alpha <= 0:
            return
        a = alpha / 255.0
        rgb = np.array([(colour >> 16) & 255, (colour >> 8) & 255, colour & 255], np.float64) / 255.0
        cell = premul[y, x:x + n]
        cell[:, :3] = rgb * a + cell[:, :3] * (1 - a)
        cell[:, 3] = a + cell[:, 3] * (1 - a)

    def span(left, right, y, colour):                # drawCoveredSpan
        if right <= left:
            return
        alpha = colour >> 24
        first, last = int(np.floor(left)), int(np.floor(right))
        if first == last:
            fill(first, y, 1, colour, int(alpha * (right - left)))
            return
        fill(first, y, 1, colour, int(alpha * (first + 1 - left)))
        if last > first + 1:
            fill(first + 1, y, last - first - 1, colour, alpha)
        if right > last:
            fill(last, y, 1, colour, int(alpha * (right - last)))

    def shape(centre, top, wd, ht, left_colour, right_colour):     # drawShieldShape
        for row in range(ht):
            t = (row + 0.5) / ht
            half = wd * 0.5
            if t > GUARD_SHOULDER:
                half *= np.sqrt((1.0 - t) / (1.0 - GUARD_SHOULDER))
            span(centre - half, centre, top + row, left_colour)
            span(centre, centre + half, top + row, right_colour)

    centre = k + 0.5
    shape(centre + rim, inset, width, h, GUARD_SHADOW, GUARD_SHADOW)
    shape(centre, 0, width, h, GUARD_EDGE, GUARD_EDGE)
    shape(centre, inset, width - 2.0 * rim, h - 3 * inset, GUARD_LIT, GUARD_SHADED)
    a = premul[..., 3:]
    rgb = np.where(a > 0, premul[..., :3] / np.maximum(a, 1e-9), 0)
    return Image.fromarray((np.concatenate([rgb, a], axis=2) * 255).round().astype(np.uint8), "RGBA")


def guard_boxes():
    """GuardShield<h> -> its box on the sheet, shelf-packed left to right, 4 texels apart."""
    boxes, x, y, row = {}, 4, 4, 0
    for h in GUARD_HEIGHTS:
        width, rim, inset = guard_geometry(h)
        w, hh = 2 * int(np.ceil(rim + width / 2)) + 1, h + inset
        if x + w + 4 > GUARD_SHEET[0]:
            x, y, row = 4, y + row + 4, 0
        boxes["GuardShield%d" % h] = (x, y, x + w, y + hh)
        x, row = x + w + 4, max(row, hh)
    assert y + row + 4 <= GUARD_SHEET[1], "the guard shields overflow their sheet"
    return boxes


def paint_guard_shields():
    sheet_img = Image.new("RGBA", GUARD_SHEET, (0, 0, 0, 0))
    for name, box in guard_boxes().items():
        put(sheet_img, guard_shield(int(name[len("GuardShield"):])), box)
    assert read_mapped(None, name="ReforgedGuardShield") == guard_boxes(), "ReforgedGuardShield.ini is not the generator's layout"
    return sheet_img


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--side", default="all", choices=["all", "Observer", "Lobby", "Guard"] + sorted(SIDES))
    ap.add_argument("--out", required=True, help="where the PNGs go")
    ap.add_argument("--dds", help="Data/Art/Textures, to write the DXT5 textures too")
    ap.add_argument("--emblem", default=EMBLEM_SVG)
    ap.add_argument("--ready-glow", choices=("A", "B"), default="B",
                    help="a charged superweapon's light: B, the breath and a rising band (default, ships), or A, the breath alone")
    args = ap.parse_args()
    os.makedirs(args.out, exist_ok=True)
    for side in (list(SIDES) + ["Observer", "Lobby", "Guard"] if args.side == "all" else [args.side]):
        made = {}
        if side == "Guard":                         # uncompressed: DXT5 would smear its covered edges
            shields = paint_guard_shields()
            shields.save(os.path.join(args.out, "ReforgedGuardShield.png"))
            if args.dds:
                shields.save(os.path.join(args.dds, "ReforgedGuardShield.tga"))
            print("wrote ReforgedGuardShield")
        elif side == "Lobby":
            made = {"ReforgedFactionEmblems": paint_faction_emblems()}
        elif side == "Observer":
            bar, panel = paint_observer_bar(), paint_observer_panel()
            check_observer(bar, panel)
            made = {"ReforgedHudObsCommandBar": bar, "ReforgedHudObsPlayerWindow": panel,
                    "ReforgedHudObsPieces": paint_observer_pieces(), "ReforgedHudObsSpec": paint_rack()}
            made["ReforgedHudHelpbox"] = helpbox = paint_helpbox()
            check_helpbox(helpbox)
            made["ReforgedHudObsTabPanel"] = tab = tab_panel("Observer")
            check_tab(tab, "Obs", "Helpbox")
        else:
            frame = radar_frame(side)
            made["ReforgedHud%sCommandBar" % side] = img = paint_bar(side)
            check(img, side)
            made["ReforgedHud%sPowers" % side] = sheet_img = paint_powers(side, args.emblem, frame, args.ready_glow)
            check_sheet(sheet_img, side, args.ready_glow)
            made["ReforgedHud%sTabPanel" % side] = tab = tab_panel(side)
            made["ReforgedHud%sTabSolo" % side] = solo = tab_panel(side, solo=True)
            check_tab(tab, side, side + "Pro", solo)
        for name, img in made.items():
            img.save(os.path.join(args.out, name + ".png"))
            if args.dds:
                img.save(os.path.join(args.dds, name + ".dds"), pixel_format="DXT5")
            print("wrote", name)


if __name__ == "__main__":
    main()
