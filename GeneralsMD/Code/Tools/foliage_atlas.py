"""The ground foliage atlas (the GroundFoliage option), painted from scratch: no retail pixels, no photos.

    python Tools/foliage_atlas.py                      # writes Data/Art/Textures/ReforgedFoliage.tga
    python Tools/foliage_atlas.py out.tga preview.png  # elsewhere, plus a look at it on a ground

The atlas is 1024x2048 RGBA, 8 rows x 4 cells of 256x256, straight alpha.  The preview is the atlas
alpha-tested at 0.5 over a ground, the 64 and 32 px-per-cell mips, the rock and bark cells on lit
spheres and cylinders, the canopies on domes, a grove floor, a scattered field and firs of twelve
tent-folded whorl cards.

Cells (index = row * 4 + column):
    0 short green clump   1 tall green clump   2 wide green spread   3 sparse green mix
    4 dry straw tuft      5 olive-brown weeds  6 green + yellow flowers  7 green + white/violet flowers
    8 leafy green bush    9 dry thorny scrub   10 bush with red berries  11 fallen branch, top-down
    12 granite + lichen   13 sandstone         14 basalt, wet patches    15 pebbles and gravel, top-down
    16 green canopy, top  17 dry canopy, top   18 fern               19 young fir
    20 leaf litter, top   21 needle litter, top  22 mossy log, top   23 sawn stump
    24 dead-wood bark     25 empty             26 empty             27 empty
    28 empty              29 empty             30 fir top tuft       31 frosted fir whorl, top

Cells 0-10, 18, 19, 23 and 30 root at the bottom centre of the cell; 11, 15-17 and 20-22 are
centred and seen from above; 31 fills the cell corner to corner from above (the mesh folds it
along a diagonal, one card per tier of a fir).  Cells
12-14 and 24 are opaque surfaces for meshes: the whole cell minus the margin, tileable over that
248 px square, no lighting but a little darkening in cracks.  Everything stays 4 px clear of the
cell edges.  Blades, twigs and needles are at least 2 px wide at 256.  Fully transparent pixels
carry the nearest opaque colour so mips do not fringe dark.

Palette: chosen against the mean, 10th and 90th percentile colours of the retail Art/Terrain
T*Gras*, T*Rock*, T*Cliff* and PTPine/PTSpruce textures (statistics only: grass median mean
#77814c, green tufts #4c5a1e..#8e934f, dry #94966c..#c0a874; grey cliffs about #858580, warm cliffs
#b69a66, dark cliffs #747678 with a 10th percentile near #1d1f27; pine needles mean about #757c50,
luminance 115, #2a3418..#b8c79e).

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


# ---------------------------------------------------------------- rows 4-5: forest floor

CANOPY = [hexc('#1c2a0e'), hexc('#45592a'), hexc('#869a4e')]
CANOPY_DRY = [hexc('#33301a'), hexc('#625c34'), hexc('#978e58')]
FERN = [hexc('#22351a'), hexc('#4a6a2a'), hexc('#88a453')]
CONIFER = [hexc('#15241a'), hexc('#2b452c'), hexc('#5b764a')]
SOIL, SOIL_RED = hexc('#352a1d'), hexc('#3b2a1c')
LITTER = [hexc(c) for c in ('#6b4a26', '#93703a', '#66652f', '#4a3520', '#84461f', '#a2844a')]
NEEDLES = [hexc(c) for c in ('#8a5530', '#6e4426', '#9c673a', '#5a3a22')]
LOG_BARK, LOG_DARK = hexc('#5a4632'), hexc('#2a2018')
MOSS = [hexc('#3e5220'), hexc('#5d7130'), hexc('#7f8f42')]
LIGHT = np.array([-0.5, -0.5, 0.7]) / np.linalg.norm([-0.5, -0.5, 0.7])   # image x, image y, toward camera


class Layer:
    """Premultiplied paint at final scale, so numpy shapes and downsampled PIL drawing mix."""

    def __init__(self):
        self.p = np.zeros((CELL, CELL, 3))
        self.a = np.zeros((CELL, CELL))

    def paint(self, cov, col):
        cov = np.clip(cov, 0, 1)
        col = np.broadcast_to(col, (CELL, CELL, 3))
        self.p = self.p * (1 - cov[..., None]) + col * cov[..., None]
        self.a = self.a * (1 - cov) + cov

    def paint_px(self, px):
        """A supersampled PIL layer (0..1 floats) on top."""
        d = downsample(px, SS)
        self.paint(d[..., 3], d[..., :3] * 255)

    def cell(self):
        rgb = np.where(self.a[..., None] > 0, self.p / np.maximum(self.a, 1e-8)[..., None], 0)
        return np.concatenate([np.clip(rgb / 255, 0, 1), self.a[..., None]], -1)


def grid():
    yy, xx = np.mgrid[0:CELL, 0:CELL].astype(float)
    return xx + 0.5, yy + 0.5


def capsule(xx, yy, p0, p1, r0, r1, flat=False):
    """Inside depth (px), across coordinate (-1..1) and along t of a tapered capsule."""
    d = np.subtract(p1, p0, dtype=float)
    ln = np.hypot(*d)
    t = ((xx - p0[0]) * d[0] + (yy - p0[1]) * d[1]) / (ln * ln)
    tc = np.clip(t, 0, 1)
    r = r0 + (r1 - r0) * tc
    depth = r - np.hypot(xx - p0[0] - tc * d[0], yy - p0[1] - tc * d[1])
    if flat:
        depth = np.minimum(depth, np.minimum(t, 1 - t) * ln)
    across = ((xx - p0[0]) * -d[1] + (yy - p0[1]) * d[0]) / ln / np.maximum(r, 1e-3)
    return depth, np.clip(across, -1, 1), t


def cyl_light(across, axis):
    """Lambert on a lying cylinder seen from above, axis a unit image-space vector."""
    perp = np.array([-axis[1], axis[0]])
    z = np.sqrt(np.clip(1 - across ** 2, 0, 1))
    return np.clip(across * (perp[0] * LIGHT[0] + perp[1] * LIGHT[1]) + z * LIGHT[2], 0, 1)


def stretched(rng, sx, sy):
    """Periodic noise sampled at stretched cell coordinates (grain along one axis)."""
    n = pnoise(rng, 1.6, 2)
    xx, yy = grid()
    return lambda u, v: n[(v * sy).astype(int) % N, (u * sx).astype(int) % N]


def paint_canopy(rng, dry=False):
    """A crown from directly above: leaves over a dark core, low leaves dark, rim dark, lit top left."""
    img = Image.new('RGBA', (CELL * SS, CELL * SS), (0, 0, 0, 0))
    dr = ImageDraw.Draw(img)
    c = CELL / 2
    R = 100.0
    ph = rng.uniform(0, 6.28, 4)

    def rim(th):
        return R * (1 + 0.05 * np.sin(3 * th + ph[0]) + 0.03 * np.sin(5 * th + ph[1])
                    + 0.03 * np.sin(7 * th + ph[2]) + 0.02 * np.sin(11 * th + ph[3]))

    th = np.linspace(0, 2 * np.pi, 64, endpoint=False)
    core = 0.93 if not dry else 0.7
    dr.polygon([((c + np.cos(t) * rim(t) * core) * SS, (c + np.sin(t) * rim(t) * core) * SS) for t in th],
               fill=rgba(CANOPY[0] * 0.8 if not dry else hexc('#2c2717')))
    pal = CANOPY if not dry else CANOPY_DRY
    if dry:   # twigs radiating from the centre show in the gaps
        for _ in range(16):
            a = rng.uniform(0, 2 * np.pi)
            pts = [np.array([c, c]) + rng.normal(0, 4, 2)]
            for _ in range(5):
                a += rng.normal(0, 0.25)
                pts.append(pts[-1] + np.array([np.cos(a), np.sin(a)]) * rim(a) * 0.165)
            stroke(dr, pts, 4.0, 2.3, BARK * 0.8)
            for _ in range(2):
                q = pts[rng.integers(2, 5)]
                b = a + rng.choice([-1, 1]) * rng.uniform(0.5, 0.9)
                stroke(dr, [q, q + np.array([np.cos(b), np.sin(b)]) * rng.uniform(12, 24)], 2.6, 2.3, BARK * 0.7)
    items = []
    n = 3200 if not dry else 1500
    clusters = rng.uniform(0, 1, (56, 2)) if dry else None
    for _ in range(n):
        if dry:   # leaves bunched in clumps, gaps between
            k = rng.integers(56)
            th0, r0 = clusters[k, 0] * 2 * np.pi, np.sqrt(clusters[k, 1]) * 0.85
            v = np.array([np.cos(th0) * r0, np.sin(th0) * r0]) + rng.normal(0, 0.09, 2)
        else:
            v = rng.normal(0, 1, 2)
            v = v / np.linalg.norm(v) * rng.uniform(0, 1) ** 0.42
        rr = np.hypot(*v)
        if rr >= 1:
            continue
        z = np.sqrt(1 - rr * rr)
        thv = np.arctan2(v[1], v[0])
        x, y = c + v[0] * rim(thv), c + v[1] * rim(thv)
        nrm = np.array([v[0], v[1], z])
        lam = max(0.0, float(nrm @ LIGHT))
        t = np.clip(0.1 + 0.95 * lam * (0.45 + 0.55 * z), 0, 1)
        col = lerp3(pal, t) * rng.normal(1, 0.05, 3)
        ln = rng.uniform(8, 12)
        items.append((z + rng.normal(0, 0.05), x, y, ln, max(2.6, ln * 0.48), rng.uniform(0, np.pi), col))
    lo, hi = MARGIN + 7, CELL - MARGIN - 7
    for z, x, y, ln, wd, ang, col in sorted(items, key=lambda i: i[0]):
        dr.polygon(leaf_poly(np.clip(x, lo, hi), np.clip(y, lo, hi), ln, wd, ang), fill=rgba(col))
    return np.asarray(img, float) / 255.0


def fit_about(points, anchor, top_room, side_room):
    d = np.asarray(points) - anchor
    return min(1.0, side_room / max(1e-3, np.abs(d[:, 0]).max()), top_room / max(1e-3, -d[:, 1].min()))


def paint_fern(rng):
    """Fronds arching out of a crown, leaflets paired along each, smaller at both ends."""
    img = Image.new('RGBA', (CELL * SS, CELL * SS), (0, 0, 0, 0))
    dr = ImageDraw.Draw(img)
    root = np.array([CELL / 2, ROOT_Y + 0.0])
    fronds = []
    for _ in range(12):
        a0 = rng.uniform(-1.0, 1.0)
        length = rng.uniform(135, 185) * (1 - 0.3 * abs(a0))
        bend = np.sign(a0) * rng.uniform(0.5, 1.0) + rng.normal(0, 0.1)
        pts, ang = [root + rng.normal(0, 3, 2) * (1, 0)], []
        for i in range(24):
            a = float(np.clip(a0 + bend * ((i + 0.5) / 24) ** 1.5, -1.9, 1.9))
            ang.append(a)
            pts.append(pts[-1] + np.array([np.sin(a), -np.cos(a)]) * length / 24)
        fronds.append((rng.uniform(0, 1), np.array(pts), np.array(ang)))
    s = fit_about(np.vstack([f[1] for f in fronds]), root, ROOT_Y - MARGIN - 22, CELL / 2 - MARGIN - 20)
    for depth, pts, ang in sorted(fronds, key=lambda f: f[0]):
        pts = root + (pts - root) * s
        shade = 0.7 + 0.3 * depth
        stroke(dr, pts, 3.0, 2.3, FERN[0] * 1.2 * shade)
        for i in range(2, 24):
            t = i / 24
            size = 17 * s * np.sin(np.pi * min(1, t * 1.15)) ** 0.7 * (1 - 0.35 * t) + 3
            for side in (-1, 1):
                pa = ang[i - 1] + side * (np.pi / 2 - 0.45) - np.pi / 2
                d = np.array([np.cos(pa), np.sin(pa)])
                col = lerp3(FERN, np.clip(0.3 + 0.6 * t + (0.12 if side < 0 else -0.05), 0, 1)) * shade
                q = pts[i] + d * size / 2
                dr.polygon(leaf_poly(q[0], q[1], size, max(2.6, size * 0.32), pa), fill=rgba(col * rng.normal(1, 0.04)))
    return np.asarray(img, float) / 255.0


def paint_conifer(rng):
    """A young fir: a trunk and whorls of drooping needle-clad branches, a triangle overall."""
    img = Image.new('RGBA', (CELL * SS, CELL * SS), (0, 0, 0, 0))
    dr = ImageDraw.Draw(img)
    cx, base, top = CELL / 2, ROOT_Y, ROOT_Y - 222
    branches = []
    for k in range(10):
        h = 38 + k * 18 + rng.normal(0, 2)
        L = 92 * (1 - (h - 38) / 186) ** 0.9 + 9
        for j in range(6):
            phi = (j + rng.uniform(-0.3, 0.3)) * 2 * np.pi / 6 + k * 0.5
            branches.append((np.sin(phi), h, L * rng.uniform(0.85, 1.05) * np.cos(phi), L))

    def draw_branch(depth, h, dx, L):
        p0 = np.array([cx, base - h])
        p1 = p0 + np.array([dx, 0.28 * L])
        mid = (p0 + p1) / 2 + np.array([0, 0.08 * L])
        pts = [p0, mid, p1 + np.array([0, -0.04 * L])]
        shade = 0.62 + 0.38 * (depth + 1) / 2
        stroke(dr, [p0, p0 + (mid - p0) * 0.9], 3.0, 2.4, BARK_DARK * shade)
        n = max(3, int(np.hypot(dx, 0.28 * L) / 3))
        bd = np.arctan2(p1[1] - p0[1], p1[0] - p0[0])
        for i in range(n):
            t = (i + 0.5) / n
            q = p0 + (p1 - p0) * t + np.array([0, 0.08 * L * 4 * t * (1 - t)]) + rng.normal(0, 1.0, 2)
            for side in (-1, 1):
                for _ in range(2):
                    a = bd + side * rng.uniform(0.5, 1.1) + 0.3
                    lit = 0.45 + 0.3 * (side < 0) + 0.3 * t + rng.normal(0, 0.08)
                    col = lerp3(CONIFER, np.clip(lit * shade, 0, 1)) * rng.normal(1, 0.05)
                    ln = rng.uniform(8, 11) * (1 - 0.3 * t)
                    e = q + np.array([np.cos(a), np.sin(a)]) * ln * 0.4
                    dr.polygon(leaf_poly(e[0], e[1], ln, 2.7, a, 8), fill=rgba(col))

    for b in sorted(branches, key=lambda b: b[0]):
        if b[0] < 0:
            draw_branch(*b)
    stroke(dr, [np.array([cx, base + 1.0]), np.array([cx, top + 10])], 6.0, 2.4, BARK)
    for b in sorted(branches, key=lambda b: b[0]):
        if b[0] >= 0:
            draw_branch(*b)
    for i in range(12):   # leader at the top
        y = top + 4 + i * 2.5
        for side in (-1, 1):
            a = -np.pi / 2 + side * 0.6
            dr.polygon(leaf_poly(cx + side * 2.5, y + 3, 8, 2.6, a, 8), fill=rgba(lerp3(CONIFER, 0.75)))
    return np.asarray(img, float) / 255.0


def soil_patch(rng, lay, col, radius, edge=22):
    """A dark soil disc with a ragged soft edge, grain inside."""
    xx, yy = grid()
    r = np.hypot(xx - CELL / 2, yy - CELL / 2)
    n = pnoise(rng, 2.4, 2)
    nn = np.zeros((CELL, CELL))
    nn[MARGIN:-MARGIN, MARGIN:-MARGIN] = n
    cov = smoothstep(radius, radius - edge, r + 12 * nn)
    g = np.zeros((CELL, CELL))
    g[MARGIN:-MARGIN, MARGIN:-MARGIN] = pnoise(rng, 1.0, 8)
    lay.paint(cov, col * (1 + 0.08 * g)[..., None])


def broadleaf(dr, cx, cy, length, width, ang, col):
    """A pointed leaf lying flat: lit half, shaded half, a darker midrib."""
    t = np.linspace(0, 1, 9)
    w = width / 2 * np.sin(np.pi * t) ** 0.75
    xs = (t - 0.5) * length
    ca, sa = np.cos(ang), np.sin(ang)

    def pt(x, y):
        return ((cx + x * ca - y * sa) * SS, (cy + x * sa + y * ca) * SS)
    left = [pt(xs[i], -w[i]) for i in range(9)]
    right = [pt(xs[i], w[i]) for i in range(9)]
    mid = [pt(xs[i], 0) for i in range(9)]
    dr.polygon(left + mid[::-1], fill=rgba(col * 1.08))
    dr.polygon(right + mid[::-1], fill=rgba(col * 0.86))
    dr.line([mid[1], mid[-1]], fill=rgba(col * 0.62), width=SS)


def paint_leaf_litter(rng):
    lay = Layer()
    soil_patch(rng, lay, SOIL, 108)
    img = Image.new('RGBA', (CELL * SS, CELL * SS), (0, 0, 0, 0))
    dr = ImageDraw.Draw(img)
    for _ in range(75):
        a, r = rng.uniform(0, 2 * np.pi), 92 * np.sqrt(rng.uniform(0, 1))
        ln = rng.uniform(14, 24)
        col = LITTER[rng.integers(len(LITTER))] * rng.normal(1, 0.06)
        broadleaf(dr, CELL / 2 + np.cos(a) * r, CELL / 2 + np.sin(a) * r, ln, ln * rng.uniform(0.45, 0.6),
                  rng.uniform(0, 2 * np.pi), col)
    lay.paint_px(np.asarray(img, float) / 255.0)
    return lay.cell()


def paint_needle_litter(rng):
    lay = Layer()
    soil_patch(rng, lay, SOIL_RED, 108)
    img = Image.new('RGBA', (CELL * SS, CELL * SS), (0, 0, 0, 0))
    dr = ImageDraw.Draw(img)
    for _ in range(1100):
        a, r = rng.uniform(0, 2 * np.pi), 96 * np.sqrt(rng.uniform(0, 1))
        x, y = CELL / 2 + np.cos(a) * r, CELL / 2 + np.sin(a) * r
        b, ln = rng.uniform(0, np.pi), rng.uniform(6, 12)
        d = np.array([np.cos(b), np.sin(b)]) * ln / 2
        col = NEEDLES[rng.integers(len(NEEDLES))] * rng.normal(1, 0.07)
        dr.line([((x - d[0]) * SS, (y - d[1]) * SS), ((x + d[0]) * SS, (y + d[1]) * SS)], fill=rgba(col), width=int(2.2 * SS))
    for _ in range(4):   # cones: a body and rows of lighter scales
        a, r = rng.uniform(0, 2 * np.pi), 70 * np.sqrt(rng.uniform(0, 1))
        x, y, ang = CELL / 2 + np.cos(a) * r, CELL / 2 + np.sin(a) * r, rng.uniform(0, np.pi)
        dr.polygon(leaf_poly(x, y, 20, 11, ang, 16), fill=rgba(hexc('#4a3220')))
        for i in range(-3, 4):
            for j in (-1, 0, 1):
                ox = i * 2.6
                oy = j * 3.0 + (i % 2) * 1.5 - 0.75
                if (ox / 9.5) ** 2 + (oy / 5) ** 2 > 1:
                    continue
                px = x + ox * np.cos(ang) - oy * np.sin(ang)
                py = y + ox * np.sin(ang) + oy * np.cos(ang)
                dr.polygon(leaf_poly(px, py, 3.0, 2.4, ang, 6), fill=rgba(hexc('#7a5434') * (1.1 - 0.1 * j)))
    lay.paint_px(np.asarray(img, float) / 255.0)
    return lay.cell()


def paint_log(rng):
    """A mossy log from above along one diagonal: bark grain along the axis, moss on the top,
    a jagged broken end, three short broken stubs."""
    lay = Layer()
    xx, yy = grid()
    a = -np.pi / 4 + rng.normal(0, 0.05)
    axis = np.array([np.cos(a), np.sin(a)])
    c = np.array([CELL / 2, CELL / 2])
    p0, p1 = c - axis * 102, c + axis * 102
    grain = stretched(rng, 0.35, 2.0)
    moss_n = pnoise(rng, 2.2, 2)
    u = (xx - c[0]) * axis[0] + (yy - c[1]) * axis[1]
    v = (xx - c[0]) * -axis[1] + (yy - c[1]) * axis[0]
    for k in range(3):   # stubs under the log, so the log covers their joint
        t = (0.2, 0.5, 0.78)[k] + rng.normal(0, 0.03)
        side = (1, -1, 1)[k]
        b = a + side * rng.uniform(1.0, 1.3)
        q0 = p0 + (p1 - p0) * t
        q1 = q0 + np.array([np.cos(b), np.sin(b)]) * rng.uniform(30, 40)
        d, ac, _ = capsule(xx, yy, q0, q1, 6.5, 4.5, flat=True)
        lit = cyl_light(ac, np.array([np.cos(b), np.sin(b)]))
        lay.paint(np.clip(d + 0.5, 0, 1), LOG_BARK[None, None] * (0.45 + 0.6 * lit)[..., None])
    d, ac, t = capsule(xx, yy, p0, p1, 18, 15, flat=True)
    jag = np.abs(np.sin(ac * 7 + 1.3)) * 6 + np.abs(np.sin(ac * 13)) * 3     # broken far end
    d = np.minimum(d, (1 - t) * 204 - jag)
    lit = cyl_light(ac, axis)
    g = grain(u + 400, v + 400)
    bark = LOG_BARK * (1 + 0.12 * g)[..., None]
    bark = mixc(bark, LOG_DARK, 0.6 * smoothstep(0.6, 1.6, -g))            # grooves along the grain
    mm = np.zeros((CELL, CELL))
    mm[MARGIN:-MARGIN, MARGIN:-MARGIN] = moss_n
    moss = smoothstep(-0.2, 0.5, mm + 0.9 * (1 - np.abs(ac)) - 0.6)
    mcol = MOSS[1] + (MOSS[2] - MOSS[1]) * smoothstep(-1, 1.5, g)[..., None]
    col = mixc(bark, mcol, 0.9 * moss)
    lay.paint(np.clip(d + 0.5, 0, 1), col * (0.45 + 0.65 * lit)[..., None])
    return lay.cell()


def paint_stump(rng):
    """A sawn stump from the side at the RTS slant: bark body, flared roots, ringed top face."""
    lay = Layer()
    xx, yy = grid()
    cx, base, H, r = CELL / 2, ROOT_Y, 104, 40.0
    grain = stretched(rng, 1.4, 0.22)
    ytop = base - H
    for k, (dx, dy, rr) in enumerate(((-100, -2, 13), (96, -3, 13), (-62, 5, 11), (70, 4, 12), (8, 7, 10))):
        q0 = np.array([cx + np.sign(dx) * r * 0.5 + (dx if abs(dx) < 20 else 0) * 0.3, base - 16])
        q1 = np.array([cx + dx, base + dy * 0.4 - 2])
        d, ac, t = capsule(xx, yy, q0, q1, rr, 3.0)
        lit = np.clip(0.55 - 0.45 * ac * np.sign(q1[0] - q0[0] + 1e-3) + 0.1, 0, 1)
        g = grain(xx * 0.4 + yy, yy * 3 + xx)
        lay.paint(np.clip(d + 0.5, 0, 1), LOG_BARK[None, None] * (0.5 + 0.55 * lit + 0.08 * g)[..., None] * 0.9)
    h = base - yy
    w = r * (1 + 0.5 * np.exp(-np.clip(h, 0, None) / 13))
    s = (xx - cx) / w
    body = np.minimum(w - np.abs(xx - cx), np.minimum(base - yy + 2, yy - ytop + 0.0))
    z = np.sqrt(np.clip(1 - s * s, 0, 1))
    lit = np.clip(s * LIGHT[0] * 1.2 + z * 0.75, 0, 1)
    g = grain(xx, yy)
    bark = LOG_BARK * (1 + 0.12 * g)[..., None]
    bark = mixc(bark, LOG_DARK, 0.65 * smoothstep(0.5, 1.5, -g))
    moss = smoothstep(0.2, 0.9, pnoise_cell(rng) + 1.2 * smoothstep(base - 40, base, yy) - 0.8)
    bark = mixc(bark, MOSS[1], 0.85 * moss)
    lay.paint(np.clip(body + 0.5, 0, 1), bark * (0.42 + 0.65 * lit)[..., None])
    ex, ey = (xx - cx) / r, (yy - ytop) / (r * 0.36)
    rho = np.hypot(ex, ey)
    rings = 0.5 + 0.5 * np.sin(rho * 2 * np.pi * 8 + 0.8 * pnoise_cell(rng))
    wood = hexc('#a4855c') + (hexc('#7d5f3c') - hexc('#a4855c')) * (0.35 * rings + 0.5 * rho ** 2)[..., None]
    crack = smoothstep(0.06, 0.0, np.abs(np.arctan2(ey, ex) - 0.9)) * (rho > 0.15)
    wood = mixc(wood, LOG_DARK, 0.7 * crack)
    wood = mixc(wood, LOG_BARK * 0.8, smoothstep(0.86, 0.94, rho))
    lay.paint(np.clip((1 - rho) * r * 0.36 + 0.5, 0, 1), wood)
    return lay.cell()


def pnoise_cell(rng):
    n = np.zeros((CELL, CELL))
    n[MARGIN:-MARGIN, MARGIN:-MARGIN] = pnoise(rng, 2.4, 2)
    return n


def paint_forest(idx):
    rng = np.random.default_rng(SEED + 1000 + idx)
    if idx in (20, 21, 22, 23):
        cell = {20: paint_leaf_litter, 21: paint_needle_litter, 22: paint_log, 23: paint_stump}[idx](rng)
        if idx != 20:   # the leaf litter keeps its soft soil edge
            cell[..., 3] = shape_alpha(cell[..., 3])
        return cell
    px = {16: lambda: paint_canopy(rng), 17: lambda: paint_canopy(rng, dry=True),
          18: lambda: paint_fern(rng), 19: lambda: paint_conifer(rng)}[idx]()
    return finish_alpha_cell(px, rng)


# ---------------------------------------------------------------- row 6: dead wood and snowy firs

BLEACHED = [hexc('#5e5850'), hexc('#9b968b'), hexc('#c8c2b6')]


def anoise(rng, beta, ax, ay, cut=1.0, hi=1e9):
    """Periodic noise over N x N with its frequencies weighted ax across and ay down: ay > ax
    gives streaks running down the cell (wood grain, bark plates).  A crack is drawn on the zero
    line of band-limited noise (hi), or it breaks up into dots."""
    f = np.fft.fftfreq(N) * N
    k = np.sqrt((f[:, None] * ay) ** 2 + (f[None, :] * ax) ** 2)
    amp = np.where((k >= cut) & (k <= hi), np.maximum(k, 1) ** (-beta / 2), 0)
    out = np.real(np.fft.ifft2(np.fft.fft2(rng.normal(0, 1, (N, N))) * amp))
    return (out - out.mean()) / out.std()


def bark_deadwood(rng):
    """Sun-bleached silver wood: fine grain and long cracks down the cell, darker weathered streaks."""
    grain = anoise(rng, 1.4, 1, 9)
    broad = anoise(rng, 2.6, 1, 4)
    t = np.clip(0.55 + 0.18 * broad + 0.1 * grain, 0, 1)
    col = (BLEACHED[1] + (BLEACHED[2] - BLEACHED[1]) * np.clip((t - 0.5) * 2, 0, 1)[..., None]
           + (BLEACHED[0] - BLEACHED[1]) * np.clip((0.5 - t) * 2, 0, 1)[..., None])
    col = mixc(col, hexc('#7f7466'), 0.45 * smoothstep(0.8, 1.6, anoise(rng, 2.2, 1, 6)))   # brown streaks
    cn = np.abs(anoise(rng, 2.0, 1, 12, 2, 40))
    gate = smoothstep(-0.4, 0.3, anoise(rng, 2.8, 1, 3))
    crack = smoothstep(0.1, 0.0, cn) * gate
    fine = smoothstep(0.06, 0.0, np.abs(anoise(rng, 1.6, 1, 10, 4, 60))) * 0.5
    col = mixc(col, hexc('#3d3730'), 0.85 * crack)
    return col * (1 - 0.22 * fine - 0.12 * smoothstep(0.35, 0.0, cn) * gate)[..., None]


def paint_row6(idx):
    rng = np.random.default_rng(SEED + 1000 + idx)
    rgb = bark_deadwood(rng)
    cell = np.zeros((CELL, CELL, 4))
    cell[MARGIN:-MARGIN, MARGIN:-MARGIN, :3] = np.clip(rgb / 255.0, 0, 1)
    cell[MARGIN:-MARGIN, MARGIN:-MARGIN, 3] = 1
    return cell


# ---------------------------------------------------------------- row 7: fir branch cards

FIR = [hexc('#212c0a'), hexc('#5f6e1f'), hexc('#bccd4c')]   # dark olive-green to bright yellow-green
FROST = [hexc('#c9c1cf'), hexc('#aca5b9')]                  # grey-lilac frost speckle


def lerp3v(stops, t):
    t = t[..., None]
    lo = stops[0] + (stops[1] - stops[0]) * np.clip(t / 0.5, 0, 1)
    return np.where(t < 0.5, lo, stops[1] + (stops[2] - stops[1]) * np.clip((t - 0.5) / 0.5, 0, 1))


def soft_noise(rng, scale):
    """Unit noise with features about scale px across."""
    n = int(CELL / scale) + 1
    a = np.asarray(Image.fromarray(rng.normal(0, 1, (n, n)).astype(np.float32)).resize((CELL, CELL), Image.BICUBIC))
    return (a - a.mean()) / a.std()


def needle_cluster(rng, lumps, sticks, frost_up, rim=None, frost_base=0.06):
    """Retail-style needle mass: a union of lumpy ellipses, a ragged noisy outline, small holes,
    short diagonal needle streaks in a high-contrast olive mottle, frost speckle over all of it.
    lumps: (cx, cy, rx, ry, angle).  sticks: capsules drawn under the needles.  rim: 0..1 per pixel,
    where 1 is the outside of the card (more holes, rougher edge)."""
    xx, yy = grid()
    best = np.full((CELL, CELL), -9.0)
    nx, ny = np.zeros((CELL, CELL)), np.zeros((CELL, CELL))
    for cx, cy, rx, ry, ang in lumps:
        ca, sa = np.cos(ang), np.sin(ang)
        u = ((xx - cx) * ca + (yy - cy) * sa) / rx
        v = (-(xx - cx) * sa + (yy - cy) * ca) / ry
        f = 1 - u * u - v * v
        m = f > best
        best = np.where(m, f, best)
        nx = np.where(m, u * ca - v * sa, nx)
        ny = np.where(m, u * sa + v * ca, ny)
    rim = np.zeros((CELL, CELL)) if rim is None else rim
    val = best + (0.25 + 0.25 * rim) * soft_noise(rng, 7) + (0.3 + 0.2 * rim) * soft_noise(rng, 1.6)
    holes = soft_noise(rng, 1.6) > 1.6 - 1.3 * rim
    a = smoothstep(-0.06, 0.06, val) * ~holes
    nz = np.sqrt(np.clip(best, 0, 1))
    shade = np.clip(-0.45 * nx - 0.6 * ny + 0.66 * nz, 0, 1)
    w = rng.normal(0, 1, (CELL, CELL))
    s1 = sum(np.roll(w, (k, k), (0, 1)) for k in range(4)) / 2
    s2 = sum(np.roll(w, (k, -k), (0, 1)) for k in range(4)) / 2
    streak = np.where(soft_noise(rng, 9) > 0, s1, s2)
    # lump domes only lean the light a little, or the clump reads as a string of beads
    t = np.clip(0.3 + 0.28 * shade + 0.36 * streak + 0.1 * np.clip(best, 0, 1) + 0.1 * soft_noise(rng, 14), 0, 1)
    col = lerp3v(FIR, t)
    p = frost_base + frost_up * 0.16 * np.clip(-ny, 0, 1) + 0.06 * shade + 0.14 * rim
    coarse = np.asarray(Image.fromarray(rng.uniform(0, 1, (CELL // 2, CELL // 2)).astype(np.float32))
                        .resize((CELL, CELL), Image.NEAREST))
    frost = (coarse < p) | (rng.uniform(0, 1, (CELL, CELL)) < p * 0.5)
    fcol = np.where((rng.uniform(0, 1, (CELL, CELL)) < 0.5)[..., None], FROST[0], FROST[1])
    col = np.where(frost[..., None], fcol * (0.85 + 0.2 * shade)[..., None], col)
    sa = np.zeros((CELL, CELL))
    for p0, p1, r0, r1 in sticks:
        d, _, _ = capsule(xx, yy, p0, p1, r0, r1)
        sa = np.maximum(sa, np.clip(d + 0.5, 0, 1))
    rgb = np.where((a >= 0.5)[..., None], col, BARK_DARK)
    alpha = np.maximum(a, sa)
    keep = np.zeros((CELL, CELL))
    keep[MARGIN:-MARGIN, MARGIN:-MARGIN] = 1
    alpha = shape_alpha(alpha * keep)
    return np.concatenate([np.clip(rgb / 255, 0, 1), alpha[..., None]], -1)


def fir_crown(rng):
    """Cell 30: the top tuft, side view, bottom-rooted: a blunt ragged lumpy cone of needles."""
    cx, base, top = CELL / 2, ROOT_Y - 6, MARGIN + 24
    lumps = []
    for _ in range(90):   # small lumps scattered through a cone, wide low, a blunt crowd at the top
        y = rng.uniform(top, base - 8)
        hw = 52 * (y - top) / (base - top) + 12
        lumps.append((cx + rng.uniform(-0.8, 0.8) * hw, y, rng.uniform(12, 20), rng.uniform(9, 15),
                      rng.uniform(-0.6, 0.6)))
    for _ in range(4):
        lumps.append((cx + rng.normal(0, 6), top + rng.uniform(-4, 8), rng.uniform(9, 13), rng.uniform(8, 11),
                      rng.uniform(-0.5, 0.5)))
    sticks = [(np.array([cx, base + 5.0]), np.array([cx, top + 10.0]), 3.0, 1.3)]
    return needle_cluster(rng, lumps, sticks, frost_up=1.0, frost_base=0.1)


def fir_whorl(rng):
    """Cell 31, the fir itself: one tier seen from above, needle sprays radiating from the trunk to
    the cell's corners (the mesh folds the card along a diagonal and those tips droop), dense in the
    middle, ragged and gappy toward the rim."""
    c = CELL / 2
    lumps, sticks = [(c, c, 26, 26, 0.0)], [(np.array([c, c]), np.array([c + 1.0, c]), 5.0, 5.0)]
    for j in range(13):
        th = j * 2 * np.pi / 13 + rng.normal(0, 0.08)
        d = np.array([np.cos(th), np.sin(th)])
        R = (CELL / 2 - MARGIN - 7) / max(abs(d[0]), abs(d[1])) * rng.uniform(0.84, 0.97)
        r = 20.0
        while r < R - 6:
            q = c + d * r
            w = 10 * (1 - 0.55 * r / R) + 3.5
            lumps.append((q[0], q[1], 21, w * rng.uniform(0.75, 1.05), th + rng.normal(0, 0.12)))
            if rng.uniform() < 0.5:   # a side spray
                b = th + rng.choice([-1, 1]) * rng.uniform(0.35, 0.6)
                e = q + np.array([np.cos(b), np.sin(b)]) * w * 1.3
                lumps.append((e[0], e[1], 14, w * 0.55, b))
            r += rng.uniform(11, 15)
        sticks.append((np.array([c, c]), c + d * R * 0.85, 2.2, 1.2))
    xx, yy = grid()
    rim = np.clip((np.hypot(xx - c, yy - c) - 45) / 115, 0, 1)
    return needle_cluster(rng, lumps, sticks, frost_up=0.0, rim=rim, frost_base=0.1)


def paint_row7(idx):
    rng = np.random.default_rng(SEED + 1000 + idx)
    return fir_crown(rng) if idx == 30 else fir_whorl(rng)


ROWS = 8
UNUSED = (25, 26, 27, 28, 29)   # reserved, fully transparent


def build():
    rng = np.random.default_rng(SEED)
    atlas = np.zeros((CELL * ROWS, CELL * 4, 4))
    for idx in [i for i in range(32) if i not in UNUSED]:
        if idx >= 8:
            cell = (paint_extra(idx) if idx < 16 else paint_forest(idx) if idx < 24
                    else paint_row6(idx) if idx < 28 else paint_row7(idx))
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


def dome(dst, cell, cx, cy, R):
    """A canopy cell laid straight down on a dome mesh, lit from upper left, alpha-tested."""
    yy, xx = np.mgrid[0:2 * R, 0:2 * R]
    x, y = (xx + 0.5 - R) / R, (yy + 0.5 - R) / R
    z = np.sqrt(np.clip(1 - x * x - y * y, 0, 1))
    lit = 0.35 + 0.8 * np.clip(x * LIGHT[0] + y * LIGHT[1] + z * LIGHT[2], 0, 1)
    n = cell.shape[0]
    u = np.clip(((x + 1) / 2 * n).astype(int), 0, n - 1)
    v = np.clip(((y + 1) / 2 * n).astype(int), 0, n - 1)
    tex = cell[v, u].astype(float)
    reg = dst[cy - R:cy + R, cx - R:cx + R]
    dst[cy - R:cy + R, cx - R:cx + R] = np.where(tex[..., 3:4] >= 128, tex[..., :3] * lit[..., None], reg)


def cylinder(dst, tex, x0, y0, w, h):
    """A trunk mock: the texture wrapped half way round a vertical cylinder, lit from the left."""
    n = tex.shape[0]
    s = (np.arange(w) + 0.5) / w * 2 - 1
    th = np.arcsin(s)
    lit = 0.3 + 0.8 * np.clip(np.cos(th + 0.7), 0, 1)
    u = ((th / np.pi + 0.5) * n * 0.5).astype(int) % n
    v = (np.arange(h) * n / (w * 1.57)).astype(int) % n
    dst[y0:y0 + h, x0:x0 + w] = tex[v[:, None], u[None, :], :3] * lit[None, :, None]


def row6(out, atlas, y0):
    """The bark cell: the row on sand, its 64 px mip, the bark on trunks."""
    W = CELL * 4
    yy, xx = np.mgrid[0:700, 0:W]
    sand = hexc('#c2a676') * (1 + 0.05 * np.sin(xx / 23.0) * np.sin(yy / 17.0))[..., None]
    out[y0:y0 + 700] = sand
    row = atlas[CELL * 6:CELL * 7]
    over_tested(out, row, 0, y0)
    small = mip(row, 4)
    over_tested(out, small, 16, y0 + 280)
    cells = dict(enumerate(cells_of(row, CELL, 1), 24))
    cells64 = dict(enumerate(cells_of(small, 64, 1), 24))
    m = MARGIN
    cylinder(out, cells[24][m:-m, m:-m].astype(float), 40, y0 + 370, 90, 300)
    cylinder(out, cells64[24][1:-1, 1:-1].astype(float), 160, y0 + 370, 24, 80)


def card(dst, cell, x, y, w, h, flip=False):
    """An alpha-tested card resized to w x h with its top left at x, y (clipped to dst)."""
    im = Image.fromarray(cell).resize((max(1, int(w)), max(1, int(h))), Image.BILINEAR)
    if flip:
        im = im.transpose(Image.FLIP_LEFT_RIGHT)
    a = np.asarray(im)
    x, y = int(x), int(y)
    x0, y0 = max(x, 0), max(y, 0)
    x1, y1 = min(x + a.shape[1], dst.shape[1]), min(y + a.shape[0], dst.shape[0])
    if x1 > x0 and y1 > y0:
        over_tested(dst[y0:y1, x0:x1], a[y0 - y:y1 - y, x0 - x:x1 - x], 0, 0)


def tent_fir(dst, cells, cx, base, H, rng, tiers=12, pitch=0.7):
    """A fir the way the game builds it: a trunk in the bark cell (24), then per tier the whorl card
    (31) folded along a diagonal like a tent so two corners droop 20-34 degrees, turned at random,
    tapering linearly up the trunk, seen from an RTS pitch; the top tuft card (30) on top."""
    m = max(1, cells[24].shape[0] * MARGIN // CELL)
    tw = max(4, int(H * 0.05))
    cylinder(dst, cells[24][m:-m, m:-m].astype(float), cx - tw // 2, base - int(H * 0.3), tw, int(H * 0.3))
    tex = cells[31]
    n = tex.shape[0]
    L = np.array([-0.5, 0.35, 0.8])
    L /= np.linalg.norm(L)
    cp, sp = np.cos(pitch), np.sin(pitch)
    for i in range(tiers):
        f = i / (tiers - 1)
        S = H * 0.4 * (1 - 0.82 * f)
        yc = base - H * (0.16 + 0.7 * f)
        yaw, k = rng.uniform(0, 2 * np.pi), np.tan(np.radians(rng.uniform(20, 34)))
        g = int(max(64, 3 * S))
        u, v = np.meshgrid(np.linspace(-1, 1, g), np.linspace(-1, 1, g))
        d2 = (u - v) / np.sqrt(2)
        X, Y, Z = u * S, v * S, -np.abs(d2) * S * k
        cy, sy = np.cos(yaw), np.sin(yaw)
        Xr, Yr = X * cy - Y * sy, X * sy + Y * cy
        sx = (cx + Xr).astype(int)
        syy = (yc - Z * cp + Yr * sp).astype(int)
        depth = Yr * cp + Z * sp
        s = np.sign(d2) * k / np.sqrt(2)          # the plane's normal before the turn: (s, -s, 1)
        nrm = np.stack([s * cy + s * sy, s * sy - s * cy, np.ones_like(u)], -1)
        nrm /= np.linalg.norm(nrm, axis=-1, keepdims=True)
        lit = 0.55 + 0.5 * np.clip(nrm @ L, 0, 1)
        ti = np.clip(((v + 1) / 2 * n).astype(int), 0, n - 1)
        tj = np.clip(((u + 1) / 2 * n).astype(int), 0, n - 1)
        texel = tex[ti, tj].astype(float)
        ok = (texel[..., 3] >= 128) & (sx >= 0) & (sx < dst.shape[1]) & (syy >= 0) & (syy < dst.shape[0])
        order = np.argsort(depth[ok], kind='stable')
        dst[syy[ok][order], sx[ok][order]] = (texel[..., :3] * lit[..., None])[ok][order]
    cw = H * 0.22
    card(dst, cells[30], cx - cw / 2, base - H * 0.86 - cw * 0.85, cw, cw)


def row7(out, atlas, y0):
    """The fir cards: the row itself, then firs of twelve tent-folded whorls built from the cards at
    full size and from the 64 px mips, on snow."""
    out[y0:y0 + 760] = hexc('#dfe6ec')
    out[y0:y0 + 262] = hexc('#8d99a3')
    over_tested(out, atlas[CELL * 7:CELL * 8], 0, y0 + 3)
    full = dict(enumerate(cells_of(atlas[CELL * 6:CELL * 8], CELL, 2), 24))
    small = dict(enumerate(cells_of(mip(atlas[CELL * 6:CELL * 8], 4), 64, 2), 24))
    rng = np.random.default_rng(SEED + 7)
    tent_fir(out, full, 300, y0 + 690, 400, rng)
    tent_fir(out, small, 640, y0 + 745, 150, rng)
    tent_fir(out, small, 800, y0 + 745, 110, rng)
    tent_fir(out, small, 920, y0 + 745, 80, rng)


def cells_of(img, size, rows):
    return [img[r * size:(r + 1) * size, c * size:(c + 1) * size] for r in range(rows) for c in range(4)]


def squash(cell, f=0.6):
    """A flat ground card as the RTS camera sees it: shorter than wide."""
    h, w = cell.shape[:2]
    return np.asarray(Image.fromarray(cell).resize((w, int(h * f)), Image.BILINEAR))


def preview(atlas):
    rng = np.random.default_rng(SEED + 1)
    W = CELL * 4
    yB, yC, yG, yD, H = 1552, 1960, 2230, 2660, 3040
    out = ground(H + 700 + 760, W, rng)
    row6(out, atlas, H)
    row7(out, atlas, H + 700)
    over_tested(out, atlas[:CELL * 6], 0, 0)
    small = mip(atlas[:CELL * 6], 4)           # 64 px per cell, mip 2
    over_tested(out, small, 16, yB)
    smaller = mip(atlas[:CELL * 6], 8)         # 32 px per cell, mip 3
    over_tested(out, smaller, 16 + 256 + 24, yB)
    cells = cells_of(atlas, CELL, 6)
    cells64 = cells_of(small, 64, 6)
    for i, k in enumerate((12, 13, 14)):       # rock spheres and canopy domes at 64 px textures, then at 256
        sphere(out, rock_tex(cells64[k], 1), 500 + i * 120, yB + 70, 50)
        sphere(out, rock_tex(cells[k], MARGIN), 105 + i * 203, yC + 125, 96)
    for i, k in enumerate((16, 17)):
        dome(out, cells64[k], 500 + i * 120, yB + 250, 50)
        dome(out, cells[k], 105 + (i + 3) * 203, yC + 125, 96)
    # grove floor at 128 px per cell: darker soil, a canopy's soft shadow, cards then upright pieces
    yy, xx = np.mgrid[0:yD - yG, 0:W]
    shadow = 1 - 0.42 * smoothstep(1.0, 0.3, np.hypot((xx - W / 2) / 520, (yy - 200) / 260))
    out[yG:yD] = out[yG:yD] * np.array([0.72, 0.7, 0.62]) * shadow[..., None]
    c128 = cells_of(mip(atlas[:CELL * 6], 2), 128, 6)
    for k, x, y in ((20, 110, 30), (21, 590, 20), (22, 330, 110), (20, 800, 210), (21, 40, 250), (22, 860, 40)):
        over_tested(out, squash(c128[k]), x, yG + y)
    for k, x, y in sorted(((23, 420, 220), (18, 150, 150), (18, 690, 270), (19, 540, 90), (19, 880, 120),
                           (8, 250, 280), (3, 30, 290), (18, 960 - 128, 290), (19, 20, 60)), key=lambda q: q[2]):
        over_tested(out, c128[k], x, yG + y)
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
