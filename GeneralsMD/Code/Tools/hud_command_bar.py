"""The in-game command bar and its button sheet, drawn from nothing: shapes, a height field and a light.

The bar is one 2048x512 texture whose left 1920 columns are the bar at 1920x1080, one texel to a screen
pixel, its top row at screen y 568 (the ImagePart of ControlBarProReforged.ini, mapped by
MappedImages/HandCreated/ReforgedHud<Side>CommandBar.ini).  ControlBarSchemeManager::drawPainting cuts
it at design x 182 and 616, texel 437 and 1478, and draws each piece where its panel stands, so nothing
may be painted near those two columns.  Every opening the bar's windows draw into (the fourteen
command cells or the wide grid's eighteen, money, power rail, selection window, rank bar, the toolbar,
the idle column and the two tabs above the right panel) is read from the same numbers ControlBar.wnd
and the scheme use, below in layout(); the art only frames them.  The left panel is empty: the radar
takes the map's shape, and the code draws its frame from nine pieces of the button sheet.  The plates reach past ControlBar.wnd's input
blockers, so the scheme names its own (Block<Name>UL/LR); check() holds the art and those rectangles
to each other.

The button sheet is ReforgedHud<Side>Powers, 1024x1024: the general's powers window, the toolbar, idle
and tab buttons in their four states, the rank bar's frame, the shortcut tray, the radar corner's hover
bracket, the selection window's emblem and the promotion banner, and the pieces the code lays out
itself (the radar's frame, the powers tray and the pieces the queue's and superweapons' trays take
mirrored or flipped, the readout plate), each where MappedImages/HandCreated/ReforgedHud<Side>Powers.ini maps it.

Nothing here is traced from another painting.  Each side is built its own way (BUILDS): its own
silhouette, how its plates stand on each other, the profile of their edges, the corners of every
window and button, the surface of the metal and what is fixed to it.  America is machined steel cut
at 45 degrees with the emblem's chevrons; China is cast armour in square terraces held by rivets; the
GLA is scrap sheet welded and bolted over a ragged frame; Turkiye is composite armour of triangular
facets with 30 degree cuts.  The emblem in every side's selection window is the launcher's
(launcher/ui/emblem.svg beside this checkout, or --emblem).  The observer's bar is a different layout
and is not drawn here.

    python Tools/hud_command_bar.py --out DIR [--side all|America|China|Gla|Turkiye] [--dds TEXTURES]
        writes DIR/ReforgedHud<Side>CommandBar.png, ...CommandBarWide.png and ...Powers.png, and with
        --dds the same as DXT5 .dds into TEXTURES (Data/Art/Textures)
"""
import argparse
import os
import re

import numpy as np
from PIL import Image, ImageDraw
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

# materials
STEEL, GLASS, ACCENT, ACCENT2, GOLDM, INKM, BRIGHT, DARK, PAPERM, DIM, HILITE = range(11)
METAL = (STEEL, DARK, BRIGHT)


def layout(wide):
    """The bar's windows in texels (x0, y0, x1, y1); screen y is texel y + 568 at 1920x1080."""
    s = 92 if wide else 0                       # COMMAND_GRID_WIDE_STEP: one column more at each end
    cols = 9 if wide else 7
    x0 = 668 - s
    return dict(
        radar=(5, 185, 327, 507),               # LeftHUD's square, the largest the map-shaped radar gets
        uattack=(276, 172, 340, 236),
        money=(894, 311, 1026, 336),            # MoneyUL/LR
        power=(605 - s, 338, 1313 + s, 348),    # PowerBarUL/LR
        tools=[(609 - s, y, 663 - s, y + 35) for y in (357, 412, 468)],         # options, beacon, chat
        # the idle column at the grid's right end: worker, infantry, vehicle, aircraft (Idle<Kind>UL/LR)
        idle=[(1312 + s, y, 1366 + s, y + 35) for y in (357, 394, 431, 468)],
        cells=[(x0 + 92 * c, y, x0 + 92 * c + 88, y + 70) for c in range(cols) for y in (358, 432)],
        minmax=(1829, 234, 1920, 265),
        general=(1829, 272, 1920, 303),
        portrait=(1642, 319, 1871, 503),        # RightHUD
        exp=(1875, 319, 1911, 503),             # ExpBarForeground
        wide=s,
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


def spread(pts, s):
    """The centre panel's outline for the wide grid: everything left of the money tab moves s left,
    everything right of it s right."""
    return [(x - s if x < 832 else x + s if x > 1088 else x, y) for x, y in pts]


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

def windows(p, L, build):
    """Every opening the bar's windows draw into, sunk with the side's corners and lip."""
    lip, depth, c = {"machined": (3, 6, 0), "terraced": (1.2, 8, 4), "scavenged": (2, 5, 4), "faceted": (4, 6, 5)}[build]
    if build == "terraced":                             # a heavy square rim round every well
        for r in L["cells"] + [L["portrait"], L["money"]]:
            p.bump([shape(build, *grow(r, 5), 4)], 2.0, 1.0)
    window(p, L["money"], depth - 1, lip, 2, c * 0.6, build=build)
    window(p, L["power"], 3.5, 2, 2)
    for r in L["cells"]:
        window(p, r, depth, lip, 1, c, build=build)
    for r in L["tools"] + L["idle"]:
        window(p, r, 3.5, 2, 1, c * 0.6, build=build)
    window(p, L["portrait"], depth, lip, 2, c, build=build)
    window(p, L["exp"], depth, lip, 1)
    for r in (L["minmax"], L["general"]):
        window(p, (r[0], r[1], r[2] + 4, r[3]), 3, 2, 1, c * 0.6, STEEL, build)


def paint_bar(side_name="America", wide=False):
    side = SIDES[side_name]
    L = layout(wide)
    p = Painter(2048, 512)
    BARS[side["build"]](p, L, L["wide"])
    return shade(p, side, SEAMS)


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


def bar_machined(p, L, s):
    """America: two tiers of machined plate, a dark flange carrying the silhouette and a satin frame on
    it, every corner cut at 45 degrees, the shield's chevrons up the wings, octagon studs."""
    # the left panel is the radar's frame now (radar_frame), drawn round the map's shape
    flanges = [spread([(560, 512), (560, 348), (592, 316), (832, 316), (862, 286), (1058, 286), (1088, 316),
                       (1366, 316), (1383, 333), (1383, 512)], s),
               [(1590, 512), (1590, 322), (1622, 290), (1806, 290), (1806, 244), (1828, 222), (1920, 222), (1920, 512)]]
    frames = [plate(592 - s, 326, 1376 + s, 512, tl=("cut", 10), tr=("cut", 10)),
              plate(868, 302, 1052, 340, tl=("cut", 10), tr=("cut", 10)),
              plate(1625, 302, 1920, 512, tl=("cut", 10)), plate(1821, 228, 1920, 316, tl=("cut", 8))]
    for f in flanges:
        p.silhouette([f], 4.0, "machined", screen=True)
    p.raise_plate(frames, 7.0, 3.0, STEEL)
    windows(p, L, "machined")
    # the shield split: the first accent's chevrons up the left wing, the second's up the right panel's;
    # the idle column fills the grid's right wing, so the second accent runs there as a hairline only
    wing_chevrons(p, 577 - s, 370, 490, 16, ACCENT)
    wing_chevrons(p, 1609, 348, 490, 18, ACCENT2)
    p.inlay([plate(612 - s, 329, 858, 331)], ACCENT, 0.4)
    p.inlay([plate(1062, 329, 1356 + s, 331)], ACCENT2, 0.4)
    p.inlay([plate(1645, 305, 1810, 307)], ACCENT2, 0.4)
    vents(p, 1650, 1790, 293, 300)
    for x, y in ((577 - s, 358), (577 - s, 500), (1609, 336), (1609, 500), (1814, 282)):
        p.stud(x, y, 3.0)


def bar_terraced(p, L, s):
    """China: cast armour in three square terraces that follow a stepped silhouette, a heavy rim round
    every well, rows of round rivets, and the accents as stacked ingots and painted bands."""
    outlines = [spread([(560, 512), (560, 340), (576, 340), (576, 328), (592, 328), (592, 316), (832, 316), (832, 300),
                        (848, 300), (848, 286), (1072, 286), (1072, 300), (1088, 300), (1088, 316), (1351, 316),
                        (1351, 328), (1367, 328), (1367, 340), (1383, 340), (1383, 512)], s),
                [(1590, 512), (1590, 314), (1604, 314), (1604, 302), (1618, 302), (1618, 290), (1806, 290), (1806, 236),
                 (1818, 236), (1818, 222), (1920, 222), (1920, 512)]]
    for o in outlines:
        sl, m, d = p.silhouette([o], 11.0, "terraced", STEEL, screen=True)
        p.mat[sl][m & (d < 6)] = DARK
    windows(p, L, "terraced")
    # ingots up the wings, the first accent left of the grid and the second up the right panel; the
    # idle column fills the grid's right wing
    for cx, y0, y1, w, mat in ((575 - s, 360, 500, 16, ACCENT), (1610, 330, 500, 22, ACCENT2)):
        for y in np.arange(y0, y1 - 8, 20):
            p.bump([plate(cx - w / 2, y, cx + w / 2, y + 9)], 1.6, 0.8, mat)
    # painted bands on the outer terrace's shoulders
    p.inlay([plate(600 - s, 318, 824, 322)], ACCENT, 0.3)
    p.inlay([plate(1096, 318, 1343 + s, 322)], ACCENT2, 0.3)
    p.inlay([plate(1626, 292, 1798, 296)], ACCENT2, 0.3)
    # rivet rows along the outer terrace
    rows = [((598 - s, 320), (826, 320)), ((854, 290), (1066, 290)), ((1094, 320), (1345 + s, 320)),
            ((564 - s, 346), (564 - s, 506)), ((1379 + s, 346), (1379 + s, 506)),
            ((1624, 294), (1800, 294)), ((1594, 320), (1594, 506)), ((1824, 226), (1914, 226)), ((1810, 242), (1810, 286))]
    for (x0, y0), (x1, y1) in rows:
        n = max(2, int(np.hypot(x1 - x0, y1 - y0) // 18) + 1)
        for t in np.linspace(0, 1, n):
            p.rivet(x0 + (x1 - x0) * t, y0 + (y1 - y0) * t, 2.2)


def bar_scavenged(p, L, s):
    """GLA: a ragged frame cut from whatever was there, scrap sheets laid over it out of square and
    welded where they meet, bolts where they would hold, and the colours sprayed on by hand.  No two
    sides of it match."""
    outlines = [spread([(560, 512), (560, 360), (574, 330), (600, 316), (832, 316), (832, 296), (846, 286), (1070, 286),
                        (1088, 300), (1088, 316), (1355, 316), (1355, 326), (1383, 326), (1383, 512)], s),
                [(1590, 512), (1590, 330), (1600, 300), (1640, 290), (1806, 290), (1810, 250), (1806, 236), (1830, 222),
                 (1920, 222), (1920, 512)]]
    for i, o in enumerate(outlines):
        p.silhouette([jitter(o, 12 + i)], 3.0, "scavenged", screen=True)
    # each sheet's edges fall between windows, never across one, or the glass would show the step
    sheets = [((588 - s, 320, 1380 + s, 352), 0.1, 1.03),
              ((588 - s, 352, 1034, 524), 0.3, 0.95), ((1034, 352, 1386 + s, 524), -0.25, 1.08),
              ((838, 288, 1082, 306), -1.1, 0.90), ((1336 + s, 318, 1384 + s, 350), 2.0, 0.80),
              ((1596, 296, 1930, 524), -0.3, 1.04), ((1812, 226, 1930, 314), 0.9, 0.92), ((1592, 412, 1636, 508), -2.2, 0.82)]
    for k, ((x0, y0, x1, y1), deg, tint) in enumerate(sheets):
        poly = sheet(x0, y0, x1, y1, deg)
        sl, m, _ = p.raise_plate([poly], 5.0 + (k % 3) * 0.9, 1.2, STEEL)
        p.tint[sl][m] = tint
        top = sorted(poly, key=lambda q: q[1])[:2]
        p.weld(min(top), max(top), 100 + k)
        corners = [rotated([(x0 + 6, y0 + 6)], (x0 + x1) / 2, (y0 + y1) / 2, deg)[0],
                   rotated([(x1 - 6, y1 - 6)], (x0 + x1) / 2, (y0 + y1) / 2, deg)[0]]
        for j, (bx, by) in enumerate(corners):
            p.bolt(bx, by, 2.8, k + j)
    windows(p, L, "scavenged")
    # hazard slashes along the grid plate's top left shoulder, and nowhere else
    for x in range(612 - s, 814, 16):
        p.inlay([[(x, 328), (x + 8, 319), (x + 15, 319), (x + 7, 328)]], ACCENT, 0.3)
    # colours sprayed on by hand: a ragged stripe in each wing
    for i, (r, mat) in enumerate((((567 - s, 360, 590 - s, 470), ACCENT), ((1369 + s, 372, 1381 + s, 470), ACCENT2),
                                  ((1600, 324, 1632, 404), ACCENT2))):
        p.inlay([jitter(plate(*r), 41 + i, 3.0, 5)], mat, 0.25)
    for i, (x, y) in enumerate(((1374 + s, 500), (1600, 440), (1600, 360))):
        p.bolt(x, y, 3.2, 71 + i)


def bar_faceted(p, L, s):
    """Turkiye: composite armour.  A silhouette cut at 30 degrees, a frame with one wide flat facet for an
    edge, its face laid in triangular tiles each tilted its own way, triangle markers pointing in from
    the wings, flush hex bolts."""
    outlines = [spread([(560, 512), (560, 346), (612, 316), (832, 316), (832, 310), (874, 286), (1046, 286), (1088, 310),
                        (1088, 316), (1331, 316), (1383, 346), (1383, 512)], s),
                [(1590, 512), (1590, 320), (1642, 290), (1806, 290), (1806, 236), (1830, 222), (1920, 222), (1920, 512)]]
    for o in outlines:
        p.silhouette([o], 4.0, "machined", screen=True)
    frames = [plate(598 - s, 326, 1376 + s, 512, tl=("slant", 14), tr=("slant", 24)),
              plate(880, 298, 1040, 340, tl=("slant", 8), tr=("slant", 8)),
              plate(1630, 302, 1920, 512, tl=("slant", 14)), plate(1822, 230, 1920, 316, tl=("slant", 8))]
    p.frame(frames, 9.0, "faceted")
    windows(p, L, "faceted")
    p.facets(46, 0.06, 0.45, 23)                        # after the windows, so the glass stays flat
    # triangle markers in the wings, pointing in at the grid and the selection window
    for cx, y0, y1, w, point, mat in ((576 - s, 362, 500, 14, "right", ACCENT), (1610, 330, 500, 18, "left", ACCENT2)):
        for y in np.arange(y0 + 10, y1 - 6, 26):
            p.inlay([triangle(cx, y, w, w * 1.15, point)], mat, 0.6)
    # thin lines along the frames' tops, each broken by a 30 degree kink
    for (x0, x1, y), mat in (((640 - s, 860, 332), ACCENT), ((1060, 1320 + s, 332), ACCENT2),
                             ((1660, 1800, 308), ACCENT2)):
        mid = (x0 + x1) / 2
        p.inlay([plate(x0, y, mid - 6, y + 2), [(mid - 6, y), (mid + 4, y + 6), (mid + 4, y + 8), (mid - 6, y + 2)],
                 plate(mid + 4, y + 6, x1, y + 8)], mat, 0.4)
    for i, (x, y) in enumerate(((577 - s, 352), (577 - s, 502), (1609, 336), (1609, 502), (1814, 282))):
        p.bump([regular(x, y, 3.0, 6, np.pi / 6)], 0.8, 1.2, BRIGHT)


BARS = {"machined": bar_machined, "terraced": bar_terraced, "scavenged": bar_scavenged, "faceted": bar_faceted}


# ---------------------------------------------------------------------------------------------- the button sheet

def read_mapped(side_name):
    """Every MappedImage of the side's Powers sheet: name -> (x0, y0, x1, y1)."""
    path = os.path.join(HERE, "..", "Data", "INI", "MappedImages", "HandCreated", "ReforgedHud%sPowers.ini" % side_name)
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
    raise ValueError(kind)


def star(cx, cy, r, inner=0.42):
    """A five-pointed star, point up, straight cuts."""
    pts = []
    for i in range(10):
        a = np.radians(-90 + 36 * i)
        k = r if i % 2 == 0 else r * inner
        pts.append((cx + k * np.cos(a), cy + k * np.sin(a)))
    return pts


def button(p, r, kind, state, build):
    """A bezel in the side's build round a dark face carrying the glyph; state is '', 'H', 'P' or 'I'."""
    x0, y0, x1, y1 = r
    bezel = {"": STEEL, "H": BRIGHT, "P": STEEL, "I": DARK}[state]
    glyph = {"": PAPERM, "H": GOLDM, "P": HILITE, "I": DIM}[state]
    p.frame([shape(build, x0, y0, x1, y1, 5)], 2.0 if state == "P" else 3.5, build, bezel, 0.67)
    p.sink([shape(build, x0 + 4, y0 + 4, x1 - 4, y1 - 4, 3)], 2.5 if state == "P" else 1.4, 1.0)
    if build == "terraced":
        for x in (x0 + 2.2, x1 - 2.2):
            p.rivet(x, (y0 + y1) / 2, 1.3)
    elif build == "scavenged":
        p.bolt(x1 - 3, y0 + 3, 1.6, x0)
    if state == "H":
        p.inlay([plate(x0 + 6, y1 - 6, x1 - 6, y1 - 5)], HILITE, 0.2)
    off = 1 if state == "P" else 0
    raised, ink, *again = icon(kind.replace("Idle", ""), (x0 + x1) / 2 + off, (y0 + y1) / 2 + off, build)
    p.bump(raised, 1.1, 0.6, glyph)
    if ink:
        p.inlay(ink, GLASS, 0.9)
    for polys in again:
        p.bump(polys, 1.1, 0.6, glyph)


def power_window(p, x0, y0, build):
    """The general's powers window, 542x598, its openings where GeneralsExpPoints.wnd puts them."""
    def r(a, b, c, d):
        return (x0 + a, y0 + b, x0 + c, y0 + d)
    outer = shape(build, *r(0, 0, 542, 598), 18)
    p.silhouette([jitter(outer, 5, 2.0) if build == "scavenged" else outer], 4.0, build)
    p.frame([shape(build, *r(7, 7, 535, 591), 13)], 7.0, build, STEEL)
    for band in ((13, 17, 529, 45), (17, 141, 525, 169), (17, 429, 525, 457), (13, 553, 529, 581)):
        window(p, r(*band), 3, 2, 0, 4, build=build)
    cells = [(18 + 102 * i, 54, 116 + 102 * i, 132) for i in range(4)]
    cells += [(18 + 102 * i, 178 + 82 * j, 116 + 102 * i, 256 + 82 * j) for i in range(5) for j in range(3)]
    cells += [(18 + 102 * i, 466, 116 + 102 * i, 544) for i in range(4)]
    for c in cells:
        window(p, r(*c), 6, 3 if build != "terraced" else 1.2, 2, 3 if build != "machined" else 0, build=build)
    window(p, r(450, 68, 510, 118), 5, 3, 2, 6, build=build)                 # rank points available
    window(p, r(450, 480, 510, 530), 3, 2, 2, 6, STEEL, build)               # the close button's socket
    if build == "faceted":
        p.facets(40, 0.06, 0.45, 29)
    for y in (137, 425, 549):                                   # grooves between the ranks
        p.inlay([plate(*r(16, y - 1, 526, y + 1))], INKM, 1.2)
    if build == "machined":
        for x in (436, 523):
            p.bump(chevrons(x0 + x, y0 + 84, 13), 0.8, 0.6, GOLDM)
        for x, y in ((14, 300), (528, 300), (300, 9.5), (240, 588.5)):
            p.stud(x0 + x, y0 + y, 3.0)
    elif build == "terraced":
        for t in np.linspace(40, 500, 12):
            p.rivet(x0 + t, y0 + 3.5, 2.0)
            p.rivet(x0 + t, y0 + 594.5, 2.0)
        for t in np.linspace(40, 560, 14):
            p.rivet(x0 + 3.5, y0 + t, 2.0)
            p.rivet(x0 + 538.5, y0 + t, 2.0)
    elif build == "scavenged":
        p.weld((x0 + 14, y0 + 172.5), (x0 + 528, y0 + 172.5), 9)       # in the gap under the band, clear of the cells
        for i, (x, y) in enumerate(((14, 160), (528, 220), (300, 9.5), (120, 588.5), (528, 470))):
            p.bolt(x0 + x, y0 + y, 3.0, i)
    else:
        for x, point in ((436, "right"), (524, "left")):
            p.inlay([triangle(x0 + x, y0 + 93, 10, 12, point)], ACCENT, 0.6)
        for x, y in ((14, 300), (528, 300)):
            p.bump([regular(x0 + x, y0 + y, 3.0, 6, np.pi / 6)], 0.8, 1.2, BRIGHT)
    p.inlay([plate(*r(30, 9, 260, 11))], ACCENT, 0.4)
    p.inlay([plate(*r(282, 587, 512, 589))], ACCENT2, 0.4)


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
# The edges and the glass stretch (in practice they only ever shrink: the square is the largest the
# radar gets), so they are drawn even along their length and every ornament sits in a corner.
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


def radar_pieces(frame):
    """The nine pieces, by suffix, out of radar_frame's picture."""
    l, t, r, b = RADAR_EDGE
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
# owner's underline, drawn additively and multiplied by the owner's colour scaled by the breath (0.55 to
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
    it, its top edge climbing from the underline to 85% of the cameo once a breath and fading as it
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
    cameo's height from the underline, and how bright it is there."""
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


def paint_powers(side_name="America", svg_path=EMBLEM_SVG, frame=None, glow="B"):
    """The side's button sheet; frame is its radar_frame when the caller already drew it."""
    side = SIDES[side_name]
    build = side["build"]
    M = read_mapped(side_name)
    prefix = side_name + "Pro"
    p = Painter(1024, 1024)
    power_window(p, *M[prefix + "GeneralsPowerWindow"][:2], build)
    for kind in ("Chat", "Options", "Worker", "Beacon", "General", "Minimize", "Maximize", "IdleInfantry",
                 "IdleVehicle", "IdleAircraft"):
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
    for name, img in pieces.items():
        put(sheet_img, img, M.get(prefix + name, READY_GLOW_AT.get(name)))
    return sheet_img


# ---------------------------------------------------------------------------------------------- checks

def scheme_blocks(scheme="America8x6"):
    """The Block<Name>UL/LR rectangles one scheme of ControlBarProReforged.ini names, in screen pixels."""
    text = open(SCHEME_INI, encoding="latin-1").read()
    body = re.search(r"ControlBarScheme %s\b(.*?)(?=\nControlBarScheme |\Z)" % scheme, text, re.S).group(1)
    v = {k + c: (int(x), int(y)) for k, c, x, y in re.findall(r"Block(\w+?)(UL|LR) X:(\d+) Y:(\d+)", body)}
    return {k: v[k + "UL"] + v[k + "LR"] for k in ("Radar", "Production", "Money", "Upgrades", "GenPower")}


def check(img, wide, side_name="America", frame=None):
    """The two cuts stay clear, the left panel is empty (the radar's frame is pieces of the sheet now),
    every window the bar draws into stands on the plate, and the plates, with the radar's frame at its
    largest round the square radar, and the side's scheme's input blockers cover each other to within a
    few pixels."""
    a = np.asarray(img)[..., 3]
    for x in SEAMS:
        assert not a[:, x - 2:x + 3].any(), "art across the cut at texel %d" % x
    assert not a[:, :SEAMS[0]].any(), "art on the left panel, which is the radar's frame now"
    L = layout(wide)
    for r in [L[n] for n in ("money", "power", "portrait", "exp", "minmax", "general")] + L["cells"] + L["tools"] + L["idle"]:
        assert (a[r[1]:r[3], r[0]:r[2]] == 255).all(), "window %s is not on the plate" % (r,)
    s = L["wide"]
    solid = a >= 250
    if frame is not None:                       # the frame round the square radar, where the code stands it
        l, t, _, _ = RADAR_EDGE
        fx, fy = L["radar"][0] - l, L["radar"][1] - t
        fa = np.asarray(frame)[..., 3]
        r = L["radar"]
        assert (fa[t:t + RADAR_INSIDE, l:l + RADAR_INSIDE] == 255).all(), "the radar's glass has a hole"
        assert (r[2] - r[0], r[3] - r[1]) == (RADAR_INSIDE, RADAR_INSIDE), "LeftHUD is not the frame's square"
        solid[fy:fy + fa.shape[0], fx:fx + fa.shape[1]] |= fa >= 250
    blocked = np.zeros(a.shape, bool)
    for name, (x0, y0, x1, y1) in scheme_blocks(SIDES[side_name]["scheme"]).items():
        if name == "Production":
            x0, x1 = x0 - s, x1 + s
        y0, y1 = y0 - TOP, y1 - TOP
        blocked[y0:y1, x0:x1] = True
        ys, xs = np.nonzero(solid[y0:y1, x0:x1])
        assert xs.min() <= 3 and xs.max() >= x1 - x0 - 4 and ys.min() <= 3 and ys.max() >= y1 - y0 - 4, \
            "the %s blocker reaches past its plate" % name
    assert not (solid & ~blocked).any(), "plate outside the scheme's blockers at %s" % (np.argwhere(solid & ~blocked)[0],)


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
    for n, wh in want.items():
        assert pre + n in M, "%s%s is not on the sheet" % (pre, n)
        assert size(n) == wh, "%s%s is %s, the code wants %s" % (pre, n, size(n), wh)
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


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--side", default="all", choices=["all"] + sorted(SIDES))
    ap.add_argument("--out", required=True, help="where the PNGs go")
    ap.add_argument("--dds", help="Data/Art/Textures, to write the DXT5 textures too")
    ap.add_argument("--emblem", default=EMBLEM_SVG)
    ap.add_argument("--ready-glow", choices=("A", "B"), default="B",
                    help="a charged superweapon's light: B, the breath and a rising band (default, ships), or A, the breath alone")
    args = ap.parse_args()
    os.makedirs(args.out, exist_ok=True)
    for side in (list(SIDES) if args.side == "all" else [args.side]):
        made = {}
        frame = radar_frame(side)
        for wide in (False, True):
            img = paint_bar(side, wide)
            check(img, wide, side, frame)
            made["ReforgedHud%sCommandBar%s" % (side, "Wide" if wide else "")] = img
        made["ReforgedHud%sPowers" % side] = sheet_img = paint_powers(side, args.emblem, frame, args.ready_glow)
        check_sheet(sheet_img, side, args.ready_glow)
        for name, img in made.items():
            img.save(os.path.join(args.out, name + ".png"))
            if args.dds:
                img.save(os.path.join(args.dds, name + ".dds"), pixel_format="DXT5")
            print("wrote", name)


if __name__ == "__main__":
    main()
