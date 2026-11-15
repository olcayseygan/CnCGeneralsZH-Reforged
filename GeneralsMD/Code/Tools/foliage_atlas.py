"""The ground foliage atlas (the GroundFoliage option), painted from scratch: no retail pixels, no photos.

    python Tools/foliage_atlas.py                      # writes Data/Art/Textures/ReforgedFoliage.tga
    python Tools/foliage_atlas.py out.tga preview.png  # elsewhere, plus a look at it on a ground

The atlas is 1024x1024 RGBA, 4 rows x 4 cells of 256x256, straight alpha.  The preview is the atlas
alpha-tested at 0.5 over a ground, the 64 and 32 px-per-cell mips, the rock cells on lit spheres and
a scattered field.

Cells (index = row * 4 + column):
    0 short green clump   1 tall green clump   2 wide green spread   3 sparse green mix
    4 dry straw tuft      5 olive-brown weeds  6 green + yellow flowers  7 green + white/violet flowers
    8 leafy green bush    9 dry thorny scrub   10 bush with red berries  11 twigs and branches, top-down
    12 granite + lichen   13 sandstone         14 basalt, wet patches    15 pebbles and gravel, top-down

Cells 0-10 root at the bottom centre of the cell, cells 11 and 15 are centred and seen from above
(a flat ground card).  Cells 12-14 are opaque rock surfaces for small rock meshes: the whole cell
minus the margin, tileable over that 248 px square, no lighting but a little darkening in cracks.
Everything stays 4 px clear of the cell edges.  Blades and twigs are at least 2 px wide at 256.
Fully transparent pixels carry the nearest opaque colour so mips do not fringe dark.

Palette: chosen against the mean, 10th and 90th percentile colours of the retail Art/Terrain
T*Gras*, T*Rock* and T*Cliff* textures (statistics only: grass median mean #77814c, green tufts
#4c5a1e..#8e934f, dry #94966c..#c0a874; grey cliffs about #858580, warm cliffs #b69a66, dark
cliffs #747678 with a 10th percentile near #1d1f27).

Rows 0-1 draw from one random stream in order and every later cell from a stream of its own, so a
new cell never changes the grass.

The seed is fixed, so a run writes the same bytes every time.
"""
import os
import sys

import numpy as np
from PIL import Image, ImageDraw

OUT_PATH = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "Data", "Art", "Textures",
                        "ReforgedFoliage.tga")
SEED = 20261114
CELL = 256
SS = 4                      # supersample factor while painting
MARGIN = 4                  # px clear of every cell edge, final scale
ROOT_Y = CELL - MARGIN - 6  # root line, final scale (room for a leaning blade's base corner)
MIN_W = 2.4                 # narrowest blade, final px (>= 2 after shaping)


def hexc(h):
    return np.array([int(h[i:i + 2], 16) for i in (1, 3, 5)], float)


# root -> mid -> tip
GREEN = [hexc('#28330c'), hexc('#566628'), hexc('#97a557')]
GREEN_ALT = [hexc('#2c3410'), hexc('#66702c'), hexc('#a9ad5c')]   # yellower
GREEN_COOL = [hexc('#25311a'), hexc('#526632'), hexc('#8c9f5e')]  # a little bluer
STRAW = [hexc('#4a3e1c'), hexc('#8f7c45'), hexc('#cdb880')]
OLIVE = [hexc('#3a3714'), hexc('#6c6a30'), hexc('#a29b5a')]
BROWN = [hexc('#3e2c14'), hexc('#76592f'), hexc('#a98a5c')]


def lerp3(stops, t):
    if t < 0.5:
        return stops[0] + (stops[1] - stops[0]) * (t / 0.5)
    return stops[1] + (stops[2] - stops[1]) * ((t - 0.5) / 0.5)


def blade_path(x0, lean, bend, length, kink=None, n=28):
    """Centre line from the root, angle from vertical grows toward the tip."""
    pts = [(x0, ROOT_Y * SS)]
    step = length * SS / n
    x, y = pts[0]
    for i in range(n):
        t = (i + 0.5) / n
        a = lean + bend * t ** 1.6
        if kink and t > kink[0]:
            a += kink[1]
        x += np.sin(a) * step
        y -= np.cos(a) * step
        pts.append((x, y))
    return np.array(pts)


def fits(pts, half_w):
    lo = (MARGIN + 1) * SS + half_w
    hi = (CELL - MARGIN - 1) * SS - half_w
    return pts[:, 0].min() >= lo and pts[:, 0].max() <= hi and pts[:, 1].min() >= lo


def draw_blade(dr, ox, b):
    """b: dict with x, lean, bend, length, width, stops, shade, kink, head."""
    length = b['length']
    edge = (MARGIN + 3) * SS + b['width'] * SS / 2
    b['x'] = float(np.clip(b['x'], edge, CELL * SS - edge))
    while True:
        pts = blade_path(b['x'], b['lean'], b['bend'], length, b.get('kink'))
        if fits(pts, (b['width'] / 2 + 1 + b.get('pad', 0)) * SS) or length < 20:
            break
        length *= 0.92
    n = len(pts) - 1
    d = np.diff(pts, axis=0)
    d = np.vstack([d, d[-1:]])
    nrm = np.stack([-d[:, 1], d[:, 0]], 1)
    nrm /= np.linalg.norm(nrm, axis=1, keepdims=True)
    ts = np.linspace(0, 1, n + 1)
    w = b['width'] * (1 - ts) ** 0.75 + MIN_W * ts ** 0.75
    w = np.maximum(w, MIN_W)
    if b.get('head'):  # seed head: bulge over the top quarter
        w += b['head'] * np.clip((ts - 0.72) / 0.28, 0, 1) ** 0.5 * (1 - ts * 0.6)
    half = (w * SS / 2)[:, None] * nrm
    left, right = pts - half, pts + half
    ph = b['phase']
    for i in range(n):
        t = ts[i]
        c = lerp3(b['stops'], t ** 0.85) * b['shade']
        c *= 1 + 0.05 * np.sin(t * 9 + ph)          # painterly banding
        cl = np.clip(c * 1.09, 0, 255)               # lit side (sun upper left)
        cr = np.clip(c * 0.90, 0, 255)
        q = [tuple(left[i] + ox), tuple(left[i + 1] + ox), tuple(pts[i + 1] + ox), tuple(pts[i] + ox)]
        dr.polygon(q, fill=tuple(int(v) for v in cl) + (255,))
        q = [tuple(pts[i] + ox), tuple(pts[i + 1] + ox), tuple(right[i + 1] + ox), tuple(right[i] + ox)]
        dr.polygon(q, fill=tuple(int(v) for v in cr) + (255,))
    # blunt cap so the tip never drops under MIN_W
    r = w[-1] * SS / 2
    cx, cy = pts[-1] + ox
    c = lerp3(b['stops'], 1.0) * b['shade']
    dr.ellipse([cx - r, cy - r, cx + r, cy + r], fill=tuple(int(v) for v in np.clip(c, 0, 255)) + (255,))
    return pts[-1]


def tuft(rng, count, h, lean_max, spread, width, palettes, bend=0.35, droop=0.6):
    """A list of blade dicts, back to front."""
    blades = []
    for _ in range(count):
        lean = rng.uniform(-lean_max, lean_max)
        edge = abs(lean) / max(lean_max, 1e-6)
        length = rng.uniform(*h) * (1 - 0.45 * edge)       # outer blades shorter
        depth = rng.uniform(0, 1)
        stops = palettes[rng.integers(len(palettes))]
        jitter = rng.normal(1, 0.06, 3)
        blades.append(dict(
            x=(CELL / 2 + rng.normal(0, spread / 2) + lean * spread * 0.5) * SS,
            lean=lean,
            bend=np.sign(lean) * rng.uniform(0, droop) * edge + rng.normal(0, bend * 0.3),
            length=length,
            width=rng.uniform(*width),
            stops=[s * jitter for s in stops],
            shade=0.72 + 0.28 * depth,                      # back blades darker
            depth=depth,
            phase=rng.uniform(0, 6.28),
        ))
    return blades


def flower(dr, ox, at, petal, centre, rng, size=1.0):
    cx, cy = at + ox
    pr = 2.1 * size * SS
    ring = 2.3 * size * SS
    k = 5
    a0 = rng.uniform(0, 6.28)
    for j in range(k):
        a = a0 + j * 2 * np.pi / k
        px, py = cx + np.cos(a) * ring, cy + np.sin(a) * ring * 0.65   # seen from above at a slant
        c = petal * (0.86 if np.sin(a) > 0.3 else 1.0)
        dr.ellipse([px - pr, py - pr * 0.8, px + pr, py + pr * 0.8],
                   fill=tuple(int(v) for v in np.clip(c, 0, 255)) + (255,))
    r = 1.4 * size * SS
    dr.ellipse([cx - r, cy - r, cx + r, cy + r], fill=tuple(int(v) for v in centre) + (255,))


def paint_cell(idx, rng):
    img = Image.new('RGBA', (CELL * SS, CELL * SS), (0, 0, 0, 0))
    dr = ImageDraw.Draw(img)
    ox = np.zeros(2)
    G = [GREEN, GREEN, GREEN_ALT, GREEN_COOL]
    flowers = []
    if idx == 0:    # short clump
        bl = tuft(rng, 60, (55, 110), 0.95, 34, (4.0, 6.5), G)
    elif idx == 1:  # tall clump
        bl = tuft(rng, 34, (150, 235), 0.45, 14, (4.0, 6.0), G, droop=0.9)
    elif idx == 2:  # wide spread
        bl = tuft(rng, 56, (55, 120), 1.25, 50, (4.0, 6.5), G, droop=0.7)
    elif idx == 3:  # sparse mix
        bl = tuft(rng, 9, (130, 210), 0.55, 30, (3.5, 5.5), G, droop=0.8)
        bl += tuft(rng, 10, (40, 90), 1.0, 40, (4.0, 6.0), G)
    elif idx == 4:  # dry straw, thinner and stiffer, a few broken
        bl = tuft(rng, 40, (80, 190), 0.7, 22, (3.0, 4.5), [STRAW, STRAW, OLIVE], bend=0.2, droop=0.35)
        for b in bl[::6]:
            b['kink'] = (rng.uniform(0.45, 0.75), np.sign(b['lean'] or 1) * rng.uniform(0.35, 0.75))
    elif idx == 5:  # olive-brown weeds: broad low leaves, grass, two seed stalks
        bl = tuft(rng, 26, (70, 150), 0.8, 26, (3.0, 5.0), [OLIVE, BROWN, STRAW])
        bl += tuft(rng, 9, (45, 85), 1.2, 30, (9.0, 13.0), [OLIVE, OLIVE, BROWN], droop=0.9)
        for _ in range(2):
            bl.append(dict(x=(CELL / 2 + rng.normal(0, 8)) * SS, lean=rng.uniform(-0.25, 0.25),
                           bend=rng.normal(0, 0.15), length=rng.uniform(170, 220), width=2.8,
                           stops=[s * 0.85 for s in BROWN], shade=1.0, depth=0.9, phase=0, head=7.0))
    else:           # 6, 7: green tuft with a few tiny flowers above it
        bl = tuft(rng, 40, (60, 125), 0.8, 22, (4.0, 6.0), G)
        if idx == 6:
            heads = [(hexc('#e2c53c'), hexc('#a8702a'))] * 4
        else:
            heads = [(hexc('#ebe7ee'), hexc('#d8b840'))] * 2 + [(hexc('#9a80c6'), hexc('#e6d66a'))] * 2
        for petal, centre in heads:
            flowers.append(dict(x=(CELL / 2 + rng.uniform(-40, 40)) * SS, lean=rng.uniform(-0.45, 0.45),
                                bend=rng.normal(0, 0.15), length=rng.uniform(80, 130), width=2.6,
                                stops=GREEN, shade=0.95, depth=1.0, phase=0, petal=petal, centre=centre,
                                pad=6))
    bl.sort(key=lambda b: b['depth'])
    # flower stems go in among the front third, heads on top of everything
    tips = []
    for b in bl[:len(bl) * 2 // 3]:
        draw_blade(dr, ox, b)
    for f in flowers:
        tips.append((draw_blade(dr, ox, f), f))
    for b in bl[len(bl) * 2 // 3:]:
        draw_blade(dr, ox, b)
    for tip, f in tips:
        flower(dr, ox, tip, f['petal'], f['centre'], rng)
    return np.asarray(img, float) / 255.0


def downsample(px, f):
    """Box filter on premultiplied colour, then back to straight alpha."""
    h, w = px.shape[0] // f, px.shape[1] // f
    a = px[..., 3:4]
    pm = np.concatenate([px[..., :3] * a, a], -1)
    pm = pm.reshape(h, f, w, f, 4).mean((1, 3))
    a = pm[..., 3:4]
    rgb = np.where(a > 0, pm[..., :3] / np.maximum(a, 1e-8), 0)
    return np.concatenate([rgb, a], -1)


def shape_alpha(a):
    """Hard-ish edge around 0.5: alpha test sees the same silhouette, A2C a little AA."""
    return np.clip((a - 0.5) * 4 + 0.5, 0, 1)


def pad_edges(px):
    """Fill RGB of a == 0 pixels with the mean of already-filled neighbours, ring by ring."""
    rgb = px[..., :3].copy()
    known = px[..., 3] > 0
    while not known.all():
        s = np.zeros_like(rgb)
        c = np.zeros(known.shape)
        kp = np.pad(known, 1)
        rp = np.pad(rgb * known[..., None], ((1, 1), (1, 1), (0, 0)))
        H, W = known.shape
        for dy in (-1, 0, 1):
            for dx in (-1, 0, 1):
                if dy or dx:
                    s += rp[1 + dy:1 + dy + H, 1 + dx:1 + dx + W]
                    c += kp[1 + dy:1 + dy + H, 1 + dx:1 + dx + W]
        new = ~known & (c > 0)
        rgb[new] = s[new] / c[new][:, None]
        known |= new
    px[..., :3] = rgb
    return px


# ---------------------------------------------------------------- row 2: bushes and twigs

LEAF = [hexc('#26330f'), hexc('#4f6326'), hexc('#8e9f52')]
LEAF_BLUE = [hexc('#22301a'), hexc('#4a5f33'), hexc('#83985c')]
SCRUB_LEAF = [hexc('#3e3f22'), hexc('#6a6a3c'), hexc('#999559')]
BARK_DARK, BARK = hexc('#33281c'), hexc('#5e4b36')
BRANCHES = [hexc('#6b553b'), hexc('#7d766a'), hexc('#6e4a33')]


def rgba(c):
    return tuple(int(v) for v in np.clip(c, 0, 255)) + (255,)


def leaf_poly(cx, cy, length, width, ang, n=10):
    """A rotated ellipse, final px in, supersampled points out."""
    t = np.linspace(0, 2 * np.pi, n, endpoint=False)
    x, y = np.cos(t) * length / 2, np.sin(t) * width / 2
    ca, sa = np.cos(ang), np.sin(ang)
    return [((cx + x[i] * ca - y[i] * sa) * SS, (cy + x[i] * sa + y[i] * ca) * SS) for i in range(n)]


def stroke(dr, pts, w0, w1, col, offset=(0.0, 0.0)):
    """Polyline in final px, width tapering w0 -> w1, round joints."""
    pts = np.asarray(pts, float) + offset
    ws = np.linspace(w0, w1, len(pts))
    r = ws[0] * SS / 2
    dr.ellipse([pts[0][0] * SS - r, pts[0][1] * SS - r, pts[0][0] * SS + r, pts[0][1] * SS + r], fill=rgba(col))
    for i in range(len(pts) - 1):
        a, b = pts[i] * SS, pts[i + 1] * SS
        dr.line([tuple(a), tuple(b)], fill=rgba(col), width=max(1, int(round(ws[i + 1] * SS))))
        r = ws[i + 1] * SS / 2
        dr.ellipse([b[0] - r, b[1] - r, b[0] + r, b[1] + r], fill=rgba(col))


def paint_bush(rng, n_leaves, width, height, leaf_len, palettes, berries=0):
    """Leaves on the front of a few ellipsoid lobes, lit from upper left, darker inside and low."""
    img = Image.new('RGBA', (CELL * SS, CELL * SS), (0, 0, 0, 0))
    dr = ImageDraw.Draw(img)
    cx, base = CELL / 2, ROOT_Y
    lobes = [(cx, base - height * 0.55, width * 0.30, height * 0.42, 0.0)]
    for s in (-1, 1):
        lobes.append((cx + s * width * 0.28 + rng.normal(0, 4), base - height * 0.32,
                      width * 0.22, height * 0.30, rng.uniform(-10, 15)))
    for _ in range(2):
        lobes.append((cx + rng.uniform(-0.18, 0.18) * width, base - height * rng.uniform(0.6, 0.72),
                      width * 0.16, height * 0.22, rng.uniform(0, 20)))
    for lx, ly, rx, ry, lz in lobes[:4]:          # a few stems from the root into the lobes
        tgt = np.array([lx, ly + ry * 0.3])
        root = np.array([cx + rng.normal(0, 5), base + 1.0])
        stroke(dr, [root, root + (tgt - root) * 0.5 + rng.normal(0, 3, 2), tgt], 4.5, 2.5, BARK_DARK)
    L = np.array([-0.45, 0.55, 0.7])
    L /= np.linalg.norm(L)
    area = np.array([rx * ry for _, _, rx, ry, _ in lobes])
    items = []
    for i in range(n_leaves + berries):
        lx, ly, rx, ry, lz = lobes[rng.choice(len(lobes), p=area / area.sum())]
        u = rng.normal(0, 1, 3)
        u /= np.linalg.norm(u)
        berry = i >= n_leaves
        if u[2] < (0.25 if berry else -0.25):
            continue
        r = rng.uniform(0.55, 1.0) ** 0.5 if not berry else rng.uniform(0.9, 1.0)
        x, y, z = lx + u[0] * rx * r, ly - u[1] * ry * r, lz + u[2] * rx * r
        if y > base - 3:
            continue
        lam = max(0.0, float(u @ L))
        hf = 0.78 + 0.22 * np.clip((base - y) / height, 0, 1)
        t = np.clip(0.12 + 0.88 * lam * (0.5 + 0.5 * r), 0, 1)
        if berry:
            items.append((z, 'berry', x, y, hf * (0.75 + 0.35 * lam)))
            continue
        pal = palettes[rng.integers(len(palettes))]
        col = lerp3(pal, t) * rng.normal(1, 0.05, 3) * hf
        ang = np.arctan2(-u[1], u[0]) + rng.normal(0, 0.6)
        ln = leaf_len * rng.uniform(0.8, 1.2)
        items.append((z, 'leaf', x, y, (col, ln, max(2.6, ln * 0.45), ang)))
    lo, hi = MARGIN + 6, CELL - MARGIN - 6
    for z, kind, x, y, p in sorted(items, key=lambda it: it[0]):
        x, y = np.clip(x, lo, hi), max(y, lo)
        if kind == 'leaf':
            col, ln, wd, ang = p
            dr.polygon(leaf_poly(x, y, ln, wd, ang), fill=rgba(col))
        else:
            rr = 2.4 * SS
            c = hexc('#9c2426') * p
            dr.ellipse([x * SS - rr, y * SS - rr, x * SS + rr, y * SS + rr], fill=rgba(c))
            hr = 0.9 * SS
            hx, hy = x * SS - 0.8 * SS, y * SS - 0.8 * SS
            dr.ellipse([hx - hr, hy - hr, hx + hr, hy + hr], fill=rgba(hexc('#d36a5c') * p))
    return np.asarray(img, float) / 255.0


def paint_scrub(rng):
    """Dry thorny scrub: zigzag twigs fanning from the root, a few small olive and dry leaves."""
    img = Image.new('RGBA', (CELL * SS, CELL * SS), (0, 0, 0, 0))
    dr = ImageDraw.Draw(img)
    segs, leaves = [], []

    def branch(p, ang, length, w, depth, shade):
        pts = [p]
        for _ in range(4):
            ang = float(np.clip(ang + rng.normal(0, 0.28), -1.35, 1.35))   # never down into the ground
            pts.append(pts[-1] + np.array([np.sin(ang), -np.cos(ang)]) * length / 4)
        segs.append((shade, pts, w, max(2.3, w * 0.7)))
        if depth >= 2:
            for _ in range(rng.integers(1, 4)):
                q = pts[rng.integers(2, 5)]
                leaves.append((shade, q + rng.normal(0, 3, 2), rng.uniform(0, 6.28)))
        if depth < 5:
            for _ in range(rng.integers(2, 4)):
                branch(pts[-1], ang + rng.uniform(-0.9, 0.9), length * rng.uniform(0.6, 0.8),
                       max(2.3, w * 0.72), depth + 1, shade)

    root = np.array([CELL / 2, ROOT_Y + 0.0])
    for _ in range(7):
        branch(root + (rng.normal(0, 4), 0), rng.uniform(-1.15, 1.15), rng.uniform(40, 62),
               5.0, 0, rng.uniform(0.7, 1.0))
    allp = np.vstack([np.vstack(s[1]) for s in segs] + [l[1][None] for l in leaves])
    d = allp - root
    s = min(1.0, (CELL / 2 - MARGIN - 8) / np.abs(d[:, 0]).max(), (ROOT_Y - MARGIN - 8) / max(1, -d[:, 1].min()))
    for shade, pts, w0, w1 in sorted(segs, key=lambda q: q[0]):
        pts = [root + (q - root) * s for q in pts]
        stroke(dr, pts, w0, w1, (BARK_DARK + (BARK - BARK_DARK) * shade) * shade)
    for shade, q, ang in sorted(leaves, key=lambda q: q[0]):
        q = root + (q - root) * s
        pal = SCRUB_LEAF if rng.uniform() < 0.7 else STRAW
        col = lerp3(pal, rng.uniform(0.35, 0.95)) * shade * rng.normal(1, 0.05, 3)
        dr.polygon(leaf_poly(q[0], q[1], rng.uniform(4.5, 6.5), 2.8, ang, 8), fill=rgba(col))
    return np.asarray(img, float) / 255.0


def paint_twigs(rng):
    """One fallen branch seen from straight above, along a single diagonal, centred: short side
    twigs leave it at shallow angles toward the tip, so nothing crosses the middle, and a few dead
    leaves lie beside it.  Darker bark than the bushes so it sits on grass and on dirt."""
    img = Image.new('RGBA', (CELL * SS, CELL * SS), (0, 0, 0, 0))
    dr = ImageDraw.Draw(img)
    centre = np.array([CELL / 2, CELL / 2])
    lines, leaves = [], []

    def curve(p, ang, length, n, wiggle):
        pts = [p]
        for _ in range(n):
            ang += rng.normal(0, wiggle)
            pts.append(pts[-1] + np.array([np.cos(ang), np.sin(ang)]) * length / n)
        return pts, ang

    bark = hexc('#503d2a') * rng.normal(1, 0.03, 3)
    a = -np.pi / 4 + rng.normal(0, 0.08)            # lower left to upper right
    length = 215.0
    pts, _ = curve(centre - np.array([np.cos(a), np.sin(a)]) * length / 2, a, length, 16, 0.035)
    pts = [q - (np.mean(pts, 0) - centre) for q in pts]
    lines.append((pts, 12.0, 5.0, bark))
    for i, side in ((4, 1), (7, -1), (10, 1), (12, -1), (14, 1)):   # all off the middle third's crossing
        tw = curve(pts[i], a + side * rng.uniform(0.35, 0.65), rng.uniform(22, 42), 4, 0.1)[0]
        lines.append((tw, 3.6 if i < 10 else 3.0, 2.3, bark * 0.95))
    for i, side in ((3, -1), (6, 1), (9, -1), (11, 1), (13, -1)):
        nrm = np.array([-np.sin(a), np.cos(a)]) * side
        leaves.append((pts[i] + nrm * rng.uniform(13, 20), rng.uniform(12, 15), rng.uniform(6, 7.5),
                       a + rng.normal(0, 0.8),
                       [hexc('#7a5c32'), hexc('#8c6c3c'), hexc('#634c2c')][rng.integers(3)] * rng.normal(1, 0.05, 3)))
    allp = np.vstack([np.vstack(l[0]) for l in lines] + [l[0][None] for l in leaves])
    s = min(1.0, (CELL / 2 - MARGIN - 9) / np.abs(allp - centre).max())
    for c, ln, wd, ang, col in leaves:
        c = centre + (c - centre) * s
        dr.polygon(leaf_poly(c[0], c[1], ln, wd, ang), fill=rgba(col))
        dr.line([tuple(np.array(leaf_poly(c[0], c[1], ln * 0.8, 0, ang, 2))[i]) for i in (0, 1)],
                fill=rgba(col * 0.7), width=SS)
    for pts, w0, w1, bark in lines:               # dark rim, bark, lit ridge toward upper left
        pts = [centre + (q - centre) * s for q in pts]
        stroke(dr, pts, w0, w1, bark * 0.55)
        stroke(dr, pts, w0 * 0.72, w1 * 0.72, bark, (-0.12 * w0, -0.12 * w0))
        if w0 > 3:
            stroke(dr, pts, w0 * 0.28, max(1.0, w1 * 0.28), np.minimum(bark * 1.35, 255), (-0.22 * w0, -0.22 * w0))
    return np.asarray(img, float) / 255.0


# ---------------------------------------------------------------- row 3: rock surfaces and gravel

N = CELL - 2 * MARGIN   # rock surfaces tile over this square


def smoothstep(e0, e1, x):
    t = np.clip((x - e0) / (e1 - e0), 0, 1)
    return t * t * (3 - 2 * t)


def pnoise(rng, beta, cut=1.0, hi=N):
    """Periodic 1/f^beta noise over N x N between cut and hi cycles, zero mean, unit deviation."""
    f = np.fft.fftfreq(N) * N
    k = np.sqrt(f[:, None] ** 2 + f[None, :] ** 2)
    amp = np.where((k >= cut) & (k <= hi), np.maximum(k, 1) ** (-beta / 2), 0)
    out = np.real(np.fft.ifft2(np.fft.fft2(rng.normal(0, 1, (N, N))) * amp))
    return (out - out.mean()) / out.std()


def warp(rng, ax, ay):
    """Smooth crack wander plus a little sub-pixel roughness, so a crack is a line, not dots."""
    return tuple(a * pnoise(rng, 3.0, hi=14) + 0.5 * pnoise(rng, 1.0, 14, 40) for a in (ax, ay))


def voronoi(rng, k, shift):
    """Periodic cells: distance to the border (F2 - F1) and the cell id, coordinates shifted."""
    yy, xx = np.mgrid[0:N, 0:N].astype(float)
    xx, yy = xx + shift[0], yy + shift[1]
    p = rng.uniform(0, N, (k, 2))
    dx = (xx[..., None] - p[:, 0] + N / 2) % N - N / 2
    dy = (yy[..., None] - p[:, 1] + N / 2) % N - N / 2
    d = np.sqrt(dx * dx + dy * dy)
    two = np.partition(d, 1, axis=-1)
    return two[..., 1] - two[..., 0], d.argmin(-1)


def mixc(col, c, m):
    return col * (1 - m[..., None]) + c * m[..., None]


def rock_granite(rng):
    h, g = pnoise(rng, 3.0), pnoise(rng, 1.2, 20)
    e, _ = voronoi(rng, 8, warp(rng, 7, 7))
    hair = smoothstep(0.06, 0.0, np.abs(pnoise(rng, 2.0, 3))) * smoothstep(0.2, 0.6, pnoise(rng, 3.0))
    t = 1 / (1 + np.exp(-h * 0.9))
    col = hexc('#7b7871') + (hexc('#a19e96') - hexc('#7b7871')) * t[..., None]
    col *= (1 + 0.04 * g)[..., None]
    col = mixc(col, hexc('#45423e'), 0.45 * (pnoise(rng, 0.4, 30) > 2.0))      # dark mica
    col = mixc(col, hexc('#bdb9b0'), 0.35 * (pnoise(rng, 0.4, 30) > 1.9))      # quartz
    col = mixc(col, hexc('#aa9686'), 0.4 * (pnoise(rng, 0.4, 30) > 2.0))       # feldspar
    lich = pnoise(rng, 2.6) + 0.35 * pnoise(rng, 1.0, 10)
    col = mixc(col, hexc('#a2a05a'), 0.85 * smoothstep(1.05, 1.3, lich))
    lich2 = pnoise(rng, 2.6) + 0.35 * pnoise(rng, 1.0, 10)
    col = mixc(col, hexc('#b3b59c'), 0.8 * smoothstep(1.25, 1.5, lich2))
    col = mixc(col, hexc('#b8853e'), 0.9 * ((pnoise(rng, 0.4, 30) > 2.3) & (lich2 > 1.0)))
    ao = crevice(e, 0.55) * (1 - 0.3 * hair) * (1 - 0.05 * np.clip(-h, 0, 2))
    return col * ao[..., None]


def crevice(e, depth, width=1.6):
    """Mild AO along a crack: dark core, soft shoulder, never below about half."""
    return (1 - depth * smoothstep(width, 0.3, e)) * (1 - 0.15 * smoothstep(7, 1, e))


def rock_sandstone(rng):
    h, g, bend = pnoise(rng, 3.0), pnoise(rng, 1.2, 20), pnoise(rng, 2.8)
    yy = np.mgrid[0:N, 0:N][0].astype(float)
    band = 0.5 + 0.5 * np.sin(2 * np.pi * (yy * 4 / N + 0.12 * bend + 0.05 * h))
    col = hexc('#a8865a') + (hexc('#c6a678') - hexc('#a8865a')) * band[..., None]
    col = mixc(col, hexc('#b07a52'), 0.5 * smoothstep(0.8, 1.6, pnoise(rng, 2.5)))
    line = smoothstep(0.9, 0.97, np.sin(2 * np.pi * (yy * 11 / N + 0.1 * bend + 0.03 * g)))
    col *= (1 + 0.035 * g - 0.12 * line)[..., None]
    e, _ = voronoi(rng, 5, warp(rng, 9, 4))
    pit = pnoise(rng, 2.2, 8)
    ao = crevice(e, 0.5, 1.8) * (1 - 0.25 * smoothstep(2.0, 2.6, pit))
    return col * ao[..., None]


def rock_basalt(rng):
    h, g = pnoise(rng, 3.0), pnoise(rng, 1.2, 20)
    e, cid = voronoi(rng, 22, warp(rng, 4, 4))
    tone = rng.normal(0, 0.06, 22)[cid]
    col = hexc('#4a4b4c') * (1 + tone + 0.07 * h + 0.06 * g)[..., None]
    col = mixc(col, hexc('#1f1f21'), 0.85 * (pnoise(rng, 0.8, 30) > 2.2))     # vesicles
    wet = smoothstep(0.5, 1.3, pnoise(rng, 3.0))
    col *= (1 - wet[..., None] * (1 - np.array([0.72, 0.75, 0.82])))
    return col * crevice(e, 0.62, 2.2)[..., None]


def paint_pebbles(rng):
    """Stones and gravel from straight above, centred: dome-shaded rings, small ones under big ones."""
    img = Image.new('RGBA', (CELL * SS, CELL * SS), (0, 0, 0, 0))
    dr = ImageDraw.Draw(img)
    pal = [hexc(c) for c in ('#8a8780', '#948a78', '#a39882', '#6b6760', '#b3ada2', '#7c7568')]
    stones = []
    for n, (r0, r1), spread in ((480, (1.7, 3.2), 0.7), (90, (3.5, 7.0), 0.8), (16, (7.0, 14.0), 1.0)):
        for _ in range(n):
            r = rng.uniform(r0, r1)
            R = (CELL / 2 - MARGIN - 4 - r * 1.25) * rng.uniform(0, 1) ** spread
            a = rng.uniform(0, 2 * np.pi)
            stones.append((CELL / 2 + np.cos(a) * R, CELL / 2 + np.sin(a) * R, r))
    for x, y, r in stones:
        base = pal[rng.integers(len(pal))] * rng.normal(1, 0.08) * rng.normal(1, 0.015, 3)
        t = np.linspace(0, 2 * np.pi, 18, endpoint=False)
        ph = rng.uniform(0, 6.28, 2)
        rad = r * (1 + 0.14 * np.sin(2 * t + ph[0]) + 0.07 * np.sin(3 * t + ph[1]))
        el, rot = rng.uniform(0.7, 1.0), rng.uniform(0, np.pi)
        px, py = np.cos(t) * rad, np.sin(t) * rad * el
        px, py = px * np.cos(rot) - py * np.sin(rot), px * np.sin(rot) + py * np.cos(rot)
        for s, k in ((1.0, 0.66), (0.8, 0.82), (0.62, 0.93), (0.45, 1.0), (0.3, 1.06)):
            o = -0.14 * r * (1 - s)
            dr.polygon([((x + o + px[i] * s) * SS, (y + o + py[i] * s) * SS) for i in range(18)], fill=rgba(base * k))
    return np.asarray(img, float) / 255.0


def finish_alpha_cell(px, rng):
    """Supersampled paint -> 256 cell, hard-ish alpha, grain, margin check, edge padding."""
    cell = downsample(px, SS)
    cell[..., 3] = shape_alpha(cell[..., 3])
    n = rng.normal(0, 1, (CELL // 4, CELL // 4)).astype(np.float32)
    n = np.asarray(Image.fromarray(n).resize((CELL, CELL), Image.BICUBIC))
    cell[..., :3] = np.clip(cell[..., :3] * (1 + 0.05 * n[..., None]), 0, 1)
    return cell


def paint_extra(idx):
    rng = np.random.default_rng(SEED + 1000 + idx)
    if idx in (12, 13, 14):
        rgb = {12: rock_granite, 13: rock_sandstone, 14: rock_basalt}[idx](rng)
        cell = np.zeros((CELL, CELL, 4))
        cell[MARGIN:-MARGIN, MARGIN:-MARGIN, :3] = np.clip(rgb / 255.0, 0, 1)
        cell[MARGIN:-MARGIN, MARGIN:-MARGIN, 3] = 1
        return cell
    if idx == 8:
        px = paint_bush(rng, 1700, 200, 170, 9.0, [LEAF, LEAF, LEAF_BLUE, GREEN_ALT])
    elif idx == 9:
        px = paint_scrub(rng)
    elif idx == 10:
        px = paint_bush(rng, 1000, 170, 140, 8.5, [LEAF_BLUE, LEAF, GREEN_COOL], berries=60)
    elif idx == 11:
        px = paint_twigs(rng)
    else:
        px = paint_pebbles(rng)
    return finish_alpha_cell(px, rng)


def build():
    rng = np.random.default_rng(SEED)
    atlas = np.zeros((CELL * 4, CELL * 4, 4))
    for idx in range(16):
        if idx >= 8:
            cell = paint_extra(idx)
            m = MARGIN
            a = cell[..., 3]
            assert a[:m].max() == 0 and a[-m:].max() == 0 and a[:, :m].max() == 0 and a[:, -m:].max() == 0, idx
            cell = pad_edges(cell)
            r, c = divmod(idx, 4)
            atlas[r * CELL:(r + 1) * CELL, c * CELL:(c + 1) * CELL] = cell
            continue
        cell = downsample(paint_cell(idx, rng), SS)
        cell[..., 3] = shape_alpha(cell[..., 3])
        # painterly grain: soft value noise, a few percent
        n = rng.normal(0, 1, (CELL // 4, CELL // 4)).astype(np.float32)
        n = np.asarray(Image.fromarray(n).resize((CELL, CELL), Image.BICUBIC))
        cell[..., :3] = np.clip(cell[..., :3] * (1 + 0.05 * n[..., None]), 0, 1)
        a = cell[..., 3]
        m = MARGIN
        assert a[:m].max() == 0 and a[-m:].max() == 0 and a[:, :m].max() == 0 and a[:, -m:].max() == 0, idx
        cell = pad_edges(cell)
        r, c = divmod(idx, 4)
        atlas[r * CELL:(r + 1) * CELL, c * CELL:(c + 1) * CELL] = cell
    return np.round(atlas * 255).astype(np.uint8)


def ground(h, w, rng):
    """Mid green-brown dirt-grass ground, our own value noise."""
    base = hexc('#6f6c44')
    g = np.zeros((h, w))
    for s, amp in ((64, 0.10), (16, 0.06), (4, 0.04)):
        n = rng.normal(0, 1, (h // s + 2, w // s + 2))
        g += amp * np.asarray(Image.fromarray(n.astype(np.float32)).resize((w + 2 * s, h + 2 * s), Image.BILINEAR))[s:s + h, s:s + w]
    tint = np.stack([1 + g * 1.1, 1 + g, 1 + g * 0.7], -1)
    return np.clip(base * tint, 0, 255)


def over_tested(dst, src, x, y):
    """Alpha test at 0.5, like the renderer will."""
    h, w = src.shape[:2]
    reg = dst[y:y + h, x:x + w]
    m = src[..., 3:4] >= 128
    dst[y:y + h, x:x + w] = np.where(m, src[..., :3], reg)


def mip(img_u8, f):
    return np.round(downsample(img_u8 / 255.0, f) * 255).astype(np.uint8)


def sphere(dst, tex, cx, cy, R):
    """A rock mesh mock: the cell's surface projected straight on a sphere, lit from upper left."""
    yy, xx = np.mgrid[0:2 * R, 0:2 * R]
    x, y = (xx + 0.5 - R) / R, (yy + 0.5 - R) / R
    m = x * x + y * y < 1
    z = np.sqrt(np.clip(1 - x * x - y * y, 0, 1))
    L = np.array([-0.5, -0.55, 0.67])
    L /= np.linalg.norm(L)
    lit = 0.28 + 0.85 * np.clip(x * L[0] + y * L[1] + z * L[2], 0, 1)
    n = tex.shape[0]
    u = ((x * 0.65 + 1) * n).astype(int) % n
    v = ((y * 0.65 + 1) * n).astype(int) % n
    reg = dst[cy - R:cy + R, cx - R:cx + R]
    dst[cy - R:cy + R, cx - R:cx + R] = np.where(m[..., None], tex[v, u] * lit[..., None], reg)


def rock_tex(cell, m, flat=None):
    """The opaque part of a rock cell; cell 15 laid over a plain ground first."""
    t = cell[m:-m, m:-m].astype(float)
    if flat is not None:
        t = np.where(t[..., 3:4] >= 128, t[..., :3], flat)
    return t[..., :3]


def preview(atlas):
    rng = np.random.default_rng(SEED + 1)
    W = CELL * 4
    yB, yC, yD, H = 1040, 1312, 1580, 1960
    out = ground(H, W, rng)
    over_tested(out, atlas, 0, 0)
    small = mip(atlas, 4)                      # 64 px per cell, mip 2
    over_tested(out, small, 16, yB)
    smaller = mip(atlas, 8)                    # 32 px per cell, mip 3
    over_tested(out, smaller, 16 + 256 + 24, yB)
    cells = [atlas[r * CELL:(r + 1) * CELL, c * CELL:(c + 1) * CELL] for r in range(4) for c in range(4)]
    cells64 = [small[r * 64:(r + 1) * 64, c * 64:(c + 1) * 64] for r in range(4) for c in range(4)]
    flat = hexc('#6f6c44')
    for i, k in enumerate((12, 13, 14, 15)):   # rock spheres at 64 px textures, then at 256
        sphere(out, rock_tex(cells64[k], 1, flat if k == 15 else None), 480 + i * 140, yB + 64, 56)
        sphere(out, rock_tex(cells[k], MARGIN, flat if k == 15 else None), 128 + i * 256, yC + 128, 118)
    # scattered field at 64 px: flat cards (11, 15) squashed by the camera angle first, then upright tufts
    flats = [np.asarray(Image.fromarray(cells64[k]).resize((64, 38), Image.BILINEAR)) for k in (11, 15)]
    weights = np.array([3, 2, 3, 3, 1.2, 1.2, 0.5, 0.5, 0.7, 0.6, 0.35])
    for _ in range(26):
        over_tested(out, flats[rng.integers(2)], rng.integers(0, W - 64), rng.integers(yD, H - 40))
    picks = []
    for _ in range(240):
        y = rng.integers(yD, H - 64)
        picks.append((y, rng.integers(0, W - 64), rng.choice(11, p=weights / weights.sum())))
    for y, x, k in sorted(picks):
        over_tested(out, cells64[k], x, y)
    return np.clip(out, 0, 255).astype(np.uint8)


def main():
    out_path = sys.argv[1] if len(sys.argv) > 1 else OUT_PATH
    atlas = build()
    Image.fromarray(atlas, 'RGBA').save(out_path)
    print('wrote', out_path)
    if len(sys.argv) > 2:
        Image.fromarray(preview(atlas), 'RGB').save(sys.argv[2])
        print('wrote', sys.argv[2])


if __name__ == '__main__':
    main()
