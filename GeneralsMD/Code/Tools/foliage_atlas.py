"""The ground foliage atlas (the GroundFoliage option), painted from scratch: no retail pixels, no photos.

    python Tools/foliage_atlas.py                      # writes Data/Art/Textures/ReforgedFoliage.tga
    python Tools/foliage_atlas.py out.tga preview.png  # elsewhere, plus a look at it on a ground

The atlas is 1024x512 RGBA, 2 rows x 4 cells of 256x256, straight alpha.  The preview is the atlas
alpha-tested at 0.5 over a ground, a 64-px-per-cell (mip 2) strip and a scattered field.

Cells (index = row * 4 + column):
    0 short green clump   1 tall green clump   2 wide green spread   3 sparse green mix
    4 dry straw tuft      5 olive-brown weeds  6 green + yellow flowers  7 green + white/violet flowers

Every tuft roots at the bottom centre of its cell and stays 4 px clear of the
cell edges. Blades are at least 2 px wide at 256. Fully transparent pixels carry
the nearest opaque colour so mips do not fringe dark.

Palette: chosen against the mean, 10th and 90th percentile colours of the
retail Art/Terrain/T*Gras* textures (statistics only, median mean #77814c,
green tufts #4c5a1e..#8e934f, dry #94966c..#c0a874).

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


def build():
    rng = np.random.default_rng(SEED)
    atlas = np.zeros((CELL * 2, CELL * 4, 4))
    for idx in range(8):
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


def preview(atlas):
    rng = np.random.default_rng(SEED + 1)
    W = CELL * 4
    out = ground(512 + 160 + 320, W, rng)
    over_tested(out, atlas, 0, 0)
    small = mip(atlas, 4)                      # 64 px per cell, mip 2
    over_tested(out, small, 16, 512 + 24)
    smaller = mip(atlas, 8)                    # 32 px per cell, mip 3
    over_tested(out, smaller, 16 + 256 + 32, 512 + 24 + 16)
    # scattered field: random cells at 24..64 px, back to front
    cells64 = [small[r * 64:(r + 1) * 64, c * 64:(c + 1) * 64] for r in range(2) for c in range(4)]
    weights = np.array([3, 2, 3, 3, 1.2, 1.2, 0.5, 0.5])
    picks = []
    for _ in range(260):
        y = rng.integers(512 + 160, 512 + 160 + 320 - 64)
        picks.append((y, rng.integers(0, W - 64), rng.choice(8, p=weights / weights.sum())))
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
