"""The Reforged mouse cursors, drawn by script and written as Windows .ANI files.

One silhouette per order, coloured the way EA's own set is: green for moving and your own units'
orders (move, scroll, waypoint, enter, heal), red for anything hostile, blue for the powers,
bare steel for tools and neutral marks (pointer, select, target, guard, wrench), construction
yellow for placing a building, grey under a red bar for an order that cannot be given here. EA
drew in the 4-bit palette, each shape lit on one half and dark on the other inside a 1 px black
line; this draws the same hues as painted and brushed metal under a light from the top left, with
weathered paint on the edges, and only lamps, screens and beams glow.

Each frame is a .CUR holding two 32-bit images, 48x48 first and 32x32 second:

    SDL (AniCursor.h)            reads the first entry only, so it gets 48
    LoadCursorFromFile           scales to SM_CXCURSOR, which picks the 32 entry at 100 % DPI
    LoadImage(.., 0, 0, LR_LOADFROMFILE)   keeps the first entry's size, 48

Hotspots are in the 48 frame; the 32 entry's are scaled with it. Reticles and area orders put the
hotspot in the middle of the mark, pointers and tools on their tip, pins and flags on their foot.

    python Tools/cursors.py [--sheet contact.png]   # writes Data/Cursors/Reforged*.ani, then
                                                    # reads every file back and checks it
"""
import argparse
import math
import os
import struct
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageFont
from scipy import ndimage


def _le16(b, o):
    return b[o] | (b[o + 1] << 8)


def _le32(b, o):
    return _le16(b, o) | (_le16(b, o + 2) << 16)


def read_cur(data):
    """First image of a .CUR/.ICO as (w, h, hotx, hoty, rgba bytes); mirrors AniCursor_decodeCursorFile."""
    if len(data) < 22:
        raise ValueError("shorter than an icon directory")
    kind = _le16(data, 2)
    if _le16(data, 0) != 0 or kind not in (1, 2) or _le16(data, 4) == 0:
        raise ValueError("not an .ICO or .CUR directory")
    size, at = _le32(data, 6 + 8), _le32(data, 6 + 12)
    if at > len(data) or size > len(data) - at or size < 40:
        raise ValueError("the image lies outside the file")
    img = data[at:at + size]
    if img[:4] == b"\x89PNG":
        raise ValueError("a PNG-compressed frame")
    hdr, w, h2 = _le32(img, 0), _le32(img, 4), _le32(img, 8)
    bits, comp, colours = _le16(img, 14), _le32(img, 16), _le32(img, 32)
    if hdr < 40 or hdr > size or not 0 < w <= 256 or h2 <= 0 or h2 % 2 or h2 // 2 > 256 or comp:
        raise ValueError("an unreadable bitmap header")
    if bits not in (1, 4, 8, 24, 32):
        raise ValueError("a bit depth this does not read")
    h = h2 // 2
    if bits <= 8 and colours == 0:
        colours = 1 << bits
    if bits > 8:
        colours = 0
    cstride, mstride = (w * bits + 31) // 32 * 4, (w + 31) // 32 * 4
    colour_at = hdr + colours * 4
    mask_at = colour_at + cstride * h
    if mask_at + mstride * h > size:
        raise ValueError("the rows run past the image")
    out, inverted = bytearray(w * h * 4), 0
    for y in range(h):
        row = h - 1 - y
        crow, mrow = colour_at + cstride * row, mask_at + mstride * row
        for x in range(w):
            a = 255
            if bits <= 8:
                bit = x * bits
                idx = (img[crow + bit // 8] >> (8 - bits - bit % 8)) & ((1 << bits) - 1)
                if idx >= colours:
                    raise ValueError("a palette index past the palette")
                b, g, r = img[hdr + idx * 4:hdr + idx * 4 + 3]
            else:
                p = crow + x * (bits // 8)
                b, g, r = img[p:p + 3]
                if bits == 32:
                    a = img[p + 3]
            if (img[mrow + x // 8] >> (7 - x % 8)) & 1:
                inverted += (r or g or b) and 1
                r = g = b = a = 0
            out[(y * w + x) * 4:(y * w + x) * 4 + 4] = bytes((r, g, b, a))
    hot = (_le16(data, 6 + 4), _le16(data, 6 + 6)) if kind == 2 else (0, 0)
    return w, h, hot[0], hot[1], bytes(out), inverted


def read_ani(data):
    """(frames, steps) like AniCursor_decode: frames from read_cur, steps as (frame, ms)."""
    if len(data) < 12 or data[:4] != b"RIFF" or data[8:12] != b"ACON":
        raise ValueError("not a RIFF ACON file")
    end = min(8 + _le32(data, 4), len(data))
    frames, rates, seq, head = [], [], [], None
    at = 12
    while at + 8 <= end:
        cid, length = data[at:at + 4], _le32(data, at + 4)
        if length > end - at - 8:
            raise ValueError("a chunk runs past the file")
        body = data[at + 8:at + 8 + length]
        if cid == b"anih" and length >= 36:
            head = struct.unpack_from("<9I", body)
        elif cid in (b"rate", b"seq "):
            (rates if cid == b"rate" else seq).extend(struct.unpack_from("<%dI" % (length // 4), body))
        elif cid == b"LIST" and body[:4] == b"fram":
            f = 4
            while f + 8 <= length:
                sub_len = _le32(body, f + 4)
                if sub_len > length - f - 8:
                    raise ValueError("a frame runs past its list")
                if body[f:f + 4] == b"icon":
                    frames.append(read_cur(body[f + 8:f + 8 + sub_len]))
                f += 8 + sub_len + (sub_len & 1)
        at += 8 + length + (length & 1)
    if head is None:
        raise ValueError("no 'anih' header")
    _, count, nsteps, _, _, _, _, rate, flags = head
    if not flags & 1:
        raise ValueError("raw frames, not icons")
    if not frames or len(frames) != count:
        raise ValueError("the frame count differs from the header's")
    steps = []
    for i in range(nsteps or count):
        frame = seq[i] if flags & 2 and i < len(seq) else i % count
        if not 0 <= frame < count:
            raise ValueError("a sequence step names a frame that is not there")
        steps.append((frame, ((rates[i] if i < len(rates) else rate) * 1000 + 30) // 60))
    return frames, steps


def cur_bytes(images, hot):
    """A .CUR holding one 32-bit image per size; images are RGBA PIL images, largest first.
    The SDL decoder reads only the first entry, Windows picks the size nearest the one asked for."""
    head = struct.pack("<3H", 0, 2, len(images))
    blobs = []
    for im in images:
        w, h = im.size
        px = im.tobytes()
        rows, mask = [], []
        mstride = (w + 31) // 32 * 4
        for y in range(h - 1, -1, -1):		# bottom-up
            row, bits = bytearray(), bytearray(mstride)
            for x in range(w):
                r, g, b, a = px[(y * w + x) * 4:(y * w + x) * 4 + 4]
                if a == 0:
                    r = g = b = 0
                    bits[x // 8] |= 0x80 >> (x % 8)
                row += bytes((b, g, r, a))
            rows.append(bytes(row))
            mask.append(bytes(bits))
        bmi = struct.pack("<IiiHHIIiiII", 40, w, h * 2, 1, 32, 0, 0, 0, 0, 0, 0)
        blobs.append(bmi + b"".join(rows) + b"".join(mask))
    offset = 6 + 16 * len(images)
    entries = b""
    for im, blob in zip(images, blobs):
        w, h = im.size
        scale = w / images[0].size[0]
        entries += struct.pack("<BBBBHHII", w % 256, h % 256, 0, 0,
                               round(hot[0] * scale), round(hot[1] * scale), len(blob), offset)
        offset += len(blob)
    return head + entries + b"".join(blobs)


def _chunk(cid, body):
    return cid + struct.pack("<I", len(body)) + body + (b"\0" if len(body) & 1 else b"")


def ani_bytes(frames, jiffies):
    """RIFF ACON from a list of .CUR byte strings, every frame shown for the same jiffies (1/60 s)."""
    w = frames[0][6]  # first entry's width
    anih = struct.pack("<9I", 36, len(frames), len(frames), w, w, 32, 1, jiffies, 1)
    fram = b"fram" + b"".join(_chunk(b"icon", f) for f in frames)
    body = b"ACON" + _chunk(b"anih", anih) + _chunk(b"LIST", fram)
    return b"RIFF" + struct.pack("<I", len(body)) + body


# ---------------------------------------------------------------------------------------------
# Drawing. Coordinates are in a 48-unit frame, (0, 0) top left; SS times supersampled.

SS = 8
SIZES = (48, 32)
# EA's cursors are the 4-bit Windows palette: #00ff00/#008000, #ff0000/#800000, #c0c0c0/#808080/white,
# #0000ff and a #ffff00 pip, each shape lit on one half and dark on the other under a 1 px black line.
# These are the same hues taken down to painted and bare metal, so the light does the two halves.
OUTLINE = (8, 9, 12)
STEEL = (170, 176, 184)
DARK_STEEL = (86, 92, 100)
RED = (192, 46, 34)
GREEN = (92, 176, 62)
GOLD = (210, 166, 52)
BLUE = (66, 104, 206)
WHITE = (214, 216, 218)
GREY = (118, 122, 126)
FLAME = (206, 98, 34)
BEAM = (150, 196, 255)
SCREEN = (70, 200, 96)
OLIVE = (96, 102, 66)
PAINT_WEAR = (150, 152, 150)   # what shows through chipped paint on an edge


def poly(*pts):
    return lambda d, k: d.polygon([(x * k, y * k) for x, y in pts], fill=255)


def ellipse(cx, cy, rx, ry):
    return lambda d, k: d.ellipse([(cx - rx) * k, (cy - ry) * k, (cx + rx) * k, (cy + ry) * k], fill=255)


def circle(cx, cy, r):
    return ellipse(cx, cy, r, r)


def rect(x0, y0, x1, y1):
    return lambda d, k: d.rectangle([x0 * k, y0 * k, x1 * k - 1, y1 * k - 1], fill=255)


def line(pts, w, caps=True):
    def draw(d, k):
        d.line([(x * k, y * k) for x, y in pts], fill=255, width=round(w * k), joint="curve")
        if caps:
            for x, y in (pts[0], pts[-1]):
                d.ellipse([(x - w / 2) * k, (y - w / 2) * k, (x + w / 2) * k, (y + w / 2) * k], fill=255)
    return draw


def arc(cx, cy, r, w, a0, a1):
    """Band of width w centred on radius r, degrees clockwise from 3 o'clock."""
    return lambda d, k: d.arc([(cx - r - w / 2) * k, (cy - r - w / 2) * k, (cx + r + w / 2) * k,
                               (cy + r + w / 2) * k], a0, a1, fill=255, width=round(w * k))


def ring(cx, cy, r, w):
    return arc(cx, cy, r, w, 0, 360)


def oval_ring(x0, y0, x1, y1, w):
    """A flat ellipse's band, the ground seen at the game's camera angle."""
    return lambda d, k: d.ellipse([x0 * k, y0 * k, x1 * k, y1 * k], outline=255, width=round(w * k))


def rot(pts, deg, c=(24, 24), move=(0, 0)):
    s, co = math.sin(math.radians(deg)), math.cos(math.radians(deg))
    return [(c[0] + (x - c[0]) * co - (y - c[1]) * s + move[0],
             c[1] + (x - c[0]) * s + (y - c[1]) * co + move[1]) for x, y in pts]


def head(tip, deg, length=8, half=7):
    """Arrowhead whose tip is at tip, pointing deg (clockwise from right)."""
    a = math.radians(deg)
    ux, uy = math.cos(a), math.sin(a)
    bx, by = tip[0] - ux * length, tip[1] - uy * length
    return poly(tip, (bx - uy * half, by + ux * half), (bx + uy * half, by - ux * half))


def arrow(start, tip, w=4, length=8, half=7):
    a = math.degrees(math.atan2(tip[1] - start[1], tip[0] - start[0]))
    back = (tip[0] - math.cos(math.radians(a)) * (length - 1), tip[1] - math.sin(math.radians(a)) * (length - 1))
    return [line([start, back], w), head(tip, a, length, half)]


class L:
    """One layer: shapes added, cuts taken away, outlined and lit on its own. glow is a lamp, a
    screen or a beam: unlit, with a soft halo, the only thing in the set that shines."""

    def __init__(self, colour, shapes, cuts=(), outline=True, glow=False):
        self.colour, self.shapes, self.cuts, self.outline, self.glow = colour, list(shapes), list(cuts), outline, glow


def _shift(a, dx, dy):
    out = np.zeros_like(a)
    h, w = a.shape
    out[max(dy, 0):h + min(dy, 0), max(dx, 0):w + min(dx, 0)] = a[max(-dy, 0):h - max(dy, 0), max(-dx, 0):w - max(dx, 0)]
    return out


def _dilate(a, r):
    # ponytail: alternating 8- and 4-neighbour steps make an octagon, round enough at SS 8
    for i in range(r):
        steps = ((1, 0), (-1, 0), (0, 1), (0, -1)) + (((1, 1), (1, -1), (-1, 1), (-1, -1)) if i % 2 == 0 else ())
        a = np.maximum.reduce([a] + [_shift(a, dx, dy) for dx, dy in steps])
    return a


_SURFACE = {}


def _surface(n):
    """Brushed grain (streaks along x) and weathering (broad blotches), both unit variance, fixed seed."""
    if n not in _SURFACE:
        rng = np.random.default_rng(1003)
        brushed = ndimage.gaussian_filter(rng.standard_normal((n, n)), (0.3 * SS, 3.0 * SS))
        weather = ndimage.gaussian_filter(rng.standard_normal((n, n)), 2.2 * SS)
        _SURFACE[n] = (brushed / brushed.std(), weather / weather.std())
    return _SURFACE[n]


LIGHT = np.array((-0.5, -0.65, 0.9)) / np.linalg.norm((-0.5, -0.65, 0.9))   # top left, above
HALF = (LIGHT + (0, 0, 1)) / np.linalg.norm(LIGHT + (0, 0, 1))
METALS = (STEEL, DARK_STEEL, WHITE, GREY)


def render(layers, size):
    n = size * SS
    k = n / 48
    rgb = np.zeros((n, n, 3))
    alpha = np.zeros((n, n))
    width = round((1.25 if size >= 48 else 1.0) * SS)   # EA's line is one pixel at 32
    bevel = 2.2 if size >= 48 else 1.6                  # output pixels of rounded shoulder
    brushed, weather = _surface(n)
    ys = np.arange(n)[:, None]

    def over(colour, a):
        nonlocal rgb, alpha
        rgb = colour * a[..., None] + rgb * (1 - a[..., None])
        alpha = a + alpha * (1 - a)

    for layer in layers:
        m = _mask(layer, n, k)
        if layer.glow:
            over(np.array(layer.colour, float) / 255, np.clip(ndimage.gaussian_filter(m, 1.6 * SS) * 0.8, 0, 1))
        if layer.outline:
            over(np.array(OUTLINE, float) / 255, _dilate(m, width))
        col = np.array(layer.colour, float) / 255
        if layer.colour == OUTLINE:   # a slot or a dial cut into what is under it
            over(col, m)
            continue
        rows =np.nonzero(m.max(axis=1) > 0.5)[0]
        y0, y1 = (rows[0], rows[-1] + 1) if len(rows) else (0, n)
        ygrad = np.clip(1.08 - 0.26 * (ys - y0) / max(y1 - y0, 1), 0.78, 1.08)[..., None]
        if layer.glow:   # a light: flat, brighter in the middle of its width
            core = np.clip(ndimage.distance_transform_edt(m > 0.5) / (1.5 * SS), 0, 1)[..., None]
            over(np.clip(col * (0.85 + 0.15 * ygrad) + (1 - col) * core * 0.35, 0, 1), m)
            continue
        dist = ndimage.distance_transform_edt(m > 0.5) / SS
        t = np.clip(dist / bevel, 0, 1)
        h = (1 - (1 - t) ** 2) * bevel
        h = ndimage.gaussian_filter(h, 0.6 * SS)
        gy, gx = np.gradient(h * SS)
        nz = np.ones_like(h)
        norm = np.sqrt(gx * gx + gy * gy + 1)
        nx, ny, nz = -gx / norm, -gy / norm, nz / norm
        ndl = np.clip(nx * LIGHT[0] + ny * LIGHT[1] + nz * LIGHT[2], 0, 1)
        diffuse = 0.32 + 0.68 * ndl / LIGHT[2]
        metal = layer.colour in METALS
        spec = np.clip(nx * HALF[0] + ny * HALF[1] + nz * HALF[2], 0, 1) ** (18 if metal else 30) * (0.45 if metal else 0.22)
        albedo = col * (1 + (0.07 if metal else 0.03) * brushed[..., None]) * (1 - 0.06 * np.clip(weather, 0, 3)[..., None])
        if not metal:   # paint worn off the crests where the weather is worst
            wear = ((dist < 0.9) & (weather > 0.7) & (ndl > LIGHT[2]))[..., None] * 0.55
            albedo = albedo * (1 - wear) + np.array(PAINT_WEAR, float) / 255 * wear
        lit = albedo * diffuse[..., None] * ygrad + spec[..., None] * 0.9
        over(np.clip(lit, 0, 1), m)

    # box filter on premultiplied colour, then back to straight alpha
    pre = rgb.reshape(size, SS, size, SS, 3).mean(axis=(1, 3))
    a = alpha.reshape(size, SS, size, SS).mean(axis=(1, 3))
    straight = np.where(a[..., None] > 0, pre / np.maximum(a[..., None], 1e-9), 0)
    out = np.dstack([straight, a]) * 255
    out = np.round(out).astype(np.uint8)
    out[out[..., 3] == 0] = 0
    return Image.fromarray(out, "RGBA")


def _mask(layer, n, k):
    im = Image.new("L", (n, n))
    d = ImageDraw.Draw(im)
    for s in layer.shapes:
        s(d, k)
    if layer.cuts:
        cut = Image.new("L", (n, n))
        dc = ImageDraw.Draw(cut)
        for s in layer.cuts:
            s(dc, k)
        im = Image.fromarray(np.minimum(np.asarray(im), 255 - np.asarray(cut)).astype(np.uint8))
    return np.asarray(im).astype(np.float64) / 255


# ---------------------------------------------------------------------------------------------
# The set. Each entry: name -> (hotspot in the 48 frame, list of frames as layer lists, jiffies)

def no_bar():
    return L(RED, [ring(24, 24, 18, 4), line([(11.5, 11.5), (36.5, 36.5)], 4.5)])


def greyed(layers):
    out = []
    for l in layers:
        v = sum(l.colour) / 3
        out.append(L((v * 0.55 + 60,) * 3, l.shapes, l.cuts, l.outline))
    return out


def chevrons(d, colour):
    out = []
    for a in (45, 135, 225, 315):
        tip = (24 + d * math.cos(math.radians(a)), 24 + d * math.sin(math.radians(a)))
        arms = rot([(tip[0] + 7, tip[1] - 7), tip, (tip[0] + 7, tip[1] + 7)], a, c=tip)
        out.append(line(arms, 4))
    return L(colour, out)


def reticle(colour, r=10, tick=17):
    shapes = [ring(24, 24, r, 3.5)]
    for x0, y0, x1, y1 in ((24, 24 - r + 4, 24, 24 - tick), (24, 24 + r - 4, 24, 24 + tick),
                           (24 - r + 4, 24, 24 - tick, 24), (24 + r - 4, 24, 24 + tick, 24)):
        shapes.append(line([(x0, y0), (x1, y1)], 3.5))
    return [L(colour, shapes), pip()]


def pip():
    """EA's yellow centre pixel on move and attack, here a small brass rivet."""
    return L(GOLD, [circle(24, 24, 2.4)])


def brackets(lo, hi, arm, w):
    return [line([(lo, lo + arm), (lo, lo), (lo + arm, lo)], w), line([(hi - arm, lo), (hi, lo), (hi, lo + arm)], w),
            line([(lo, hi - arm), (lo, hi), (lo + arm, hi)], w), line([(hi - arm, hi), (hi, hi), (hi, hi - arm)], w)]


def pointer():
    return [L(STEEL, [poly((3, 2), (3, 34), (11, 27), (16, 39), (22, 36.5), (17, 25), (27, 25))])]


def scroll(i):
    pts = [(45, 24), (31, 10), (31, 18), (12, 18), (12, 30), (31, 30), (31, 38)]
    pts = rot(pts, 45 * i)
    tip = pts[0]
    dx = 0 if abs(tip[0] - 24) < 1 else (45 - tip[0] if tip[0] > 24 else 3 - tip[0])
    dy = 0 if abs(tip[1] - 24) < 1 else (45 - tip[1] if tip[1] > 24 else 3 - tip[1])
    pts = [(x + dx, y + dy) for x, y in pts]
    return pts[0], [L(GREEN, [poly(*pts)])]   # EA's scroll arrows are green


def enter(colour):
    # the doorway is steel so the arrow alone carries the intent and the two never read as one glyph
    return [L(STEEL, [rect(8, 20, 40, 46)], [rect(14, 26, 34, 47)]), L(colour, arrow((24, 2), (24, 36), 6, 12, 10))]


def seven_segment(digit, x, y, w, h, t):
    """One digit's lit segments as rectangles, top left at x, y."""
    on = {0: "abcdef", 1: "bc", 2: "abdeg", 3: "abcdg"}[digit]
    m = y + h / 2
    seg = {"a": (x, y, x + w, y + t), "b": (x + w - t, y, x + w, m), "c": (x + w - t, m, x + w, y + h),
           "d": (x, y + h - t, x + w, y + h), "e": (x, m, x + t, y + h), "f": (x, y, x + t, m),
           "g": (x, m - t / 2, x + w, m + t / 2)}
    return [rect(*seg[c]) for c in on]


def box(x0, y0, x1, y1):
    return [(x0, y0), (x1, y0), (x1, y1), (x0, y1)]


def disc_pts(cx, cy, r, n=24):
    return [(cx + r * math.cos(2 * math.pi * i / n), cy + r * math.sin(2 * math.pi * i / n)) for i in range(n)]


def build_set():
    s = {}
    s["Pointer"] = ((3, 2), [pointer()], 0)
    for i in range(8):
        tip, layers = scroll(i)
        s["Scroll%d" % i] = ((round(tip[0]), round(tip[1])), [layers], 0)
    s["Select"] = ((24, 24), [[L(STEEL, brackets(9, 39, 9, 4)), L(STEEL, [circle(24, 24, 2.6)])]], 0)
    s["Target"] = ((24, 24), [[L(STEEL, [line([(24, 5), (24, 18)], 3.5), line([(24, 30), (24, 43)], 3.5),
                                          line([(5, 24), (18, 24)], 3.5), line([(30, 24), (43, 24)], 3.5)]),
                                L(STEEL, [circle(24, 24, 2.2)])]], 0)
    # EA's pair: green arrows for move, the same green with red for attack move
    s["Move"] = ((24, 24), [[chevrons(d, GREEN), pip()] for d in (13, 11, 9, 11)], 6)
    s["AttackMove"] = ((24, 24), [[chevrons(d, GREEN), L(RED, [ring(24, 24, 5.5, 3)]), pip()] for d in (14, 12, 10, 12)], 6)
    s["Attack"] = ((24, 24), [reticle(RED)], 0)
    s["ForceAttack"] = ((24, 24), [reticle(RED, 8, 13) + [L(RED, brackets(4, 44, 10, 3.5))]], 0)
    s["ForceAttackGround"] = ((24, 33), [[L(RED, [oval_ring(7, 26, 41, 40, 3.5)]),
                                          L(RED, [circle(24, 33, 2.2)]),
                                          L(RED, arrow((24, 2), (24, 28), 4.5, 9, 7.5))]], 0)
    s["OutRange"] = ((24, 24), [[L(RED, [arc(24, 24, 13, 3.5, a + r - 20, a + r + 20) for a in (45, 135, 225, 315)]),
                                 L(RED, [circle(24, 24, 2.2)])] for r in (0, 22.5, 45, 67.5)], 6)

    # guard: a steel shield over the ring of ground it holds, the brand's double chevron on it
    s["Guard"] = ((24, 40), [[L(STEEL, [oval_ring(6, 34.5, 42, 45.5, 3)]),
                              L(STEEL, [poly((12, 5), (36, 5), (36, 20), (33, 28), (24, 36), (15, 28), (12, 20))]),
                              L(DARK_STEEL, [line([(17, 20), (24, 14), (31, 20)], 3.5),
                                             line([(17, 27), (24, 21), (31, 27)], 3.5)], outline=False)]], 0)
    s["Waypoint"] = ((24, 44), [[L(GREEN, [circle(24, 16, 11), poly((14.5, 20), (33.5, 20), (24, 44))],
                                   [circle(24, 16, 4.5)])]], 0)
    s["Beacon"] = ((24, 44), [[L(STEEL, [poly((24, 12), (16, 44), (32, 44))], [poly((24, 22), (20, 40), (28, 40))]),
                               L(BLUE, [circle(24, 10, 3.6)], glow=True)] +
                              [L(BLUE, [arc(24, 10, r, 2.8, -40, 40), arc(24, 10, r, 2.8, 140, 220)], glow=True) for r in rs]
                              for rs in ((8, 14, 20), (), (8,), (8, 14))], 10)

    # friendly and support: green
    s["Enter"] = ((24, 36), [enter(GREEN)], 0)
    s["Garrison"] = ((24, 30), [[L(STEEL, [poly((8, 46), (8, 24), (13, 24), (13, 19), (19, 19), (19, 24), (29, 24),
                                                (29, 19), (35, 19), (35, 24), (40, 24), (40, 46))],
                                   [rect(19, 34, 29, 47)]),
                                 L(GREEN, arrow((24, 0), (24, 30), 4.5, 10, 8))]], 0)
    s["Board"] = ((24, 26), [[L(DARK_STEEL, [circle(12, 38, 5), circle(24, 38, 5), circle(36, 38, 5)]),
                              L(GREEN, [poly((3, 37), (3, 30), (10, 24), (37, 24), (45, 31), (45, 37))]),
                              L(GREEN, arrow((24, 0), (24, 21), 4.5, 9, 7.5))]], 0)
    s["Unload"] = ((24, 30), [[L(STEEL, [rect(8, 3, 40, 16)], [rect(13, 8, 35, 17)]),
                               L(GREEN, arrow((24, 19), (24, 44), 4, 9, 7) + arrow((19, 19), (7, 40), 4, 9, 7)
                                 + arrow((29, 19), (41, 40), 4, 9, 7))]], 0)
    # dock: a steel bay open to the left with its two mouth lamps, the green arrow running in
    s["Dock"] = ((24, 24), [[L(STEEL, [rect(16, 8, 45, 40)], [rect(14, 15, 39, 33)]),
                             L(GOLD, [circle(19.5, 11.5, 1.8), circle(19.5, 36.5, 1.8)], outline=False, glow=True),
                             L(GREEN, arrow((2, 24), (34, 24), 5, 11, 8.5))]], 0)
    s["Rally"] = ((11, 44), [[L(STEEL, [line([(11, 5), (11, 44)], 3), circle(11, 4, 2.4)]),
                              L(GREEN, [poly((13, 6), (25, 6 + w), (40, 6), (33, 14.5), (40, 23), (25, 23 + w), (13, 23))])]
                             for w in (0, 2, 0, -2)], 8)
    s["Defector"] = ((24, 24), [[L(GREEN, [arc(24, 24, 14, 4.5, 200, 330), head(rot([(38, 24)], -18)[0], 115, 10, 8),
                                           arc(24, 24, 14, 4.5, 20, 150), head(rot([(10, 24)], -18)[0], 295, 10, 8)])]], 0)
    # EA's heal is a cross on a white disc; the cross here is the friendly green
    s["GetHealed"] = ((24, 24), [[L(WHITE, [circle(24, 24, 18)]), L(GREEN, [rect(19.5, 10, 28.5, 38), rect(10, 19.5, 38, 28.5)])]], 0)
    teeth = []
    for i in range(16):
        r = 18 if i % 2 == 0 else 13.5
        for da in (-9, 9):
            a = math.radians(i * 22.5 + da)
            teeth.append((24 + r * math.cos(a), 24 + r * math.sin(a)))
    s["GetRepaired"] = ((24, 24), [[L(STEEL, [poly(*teeth)], [circle(24, 24, 9)]),
                                    L(GREEN, [rect(21, 13, 27, 35), rect(13, 21, 35, 27)])]], 0)

    # building: construction yellow, EA's placement colour; the wrench stays bare steel as EA's is
    wrench_pts = [p for p in disc_pts(10, 24, 8.5)]
    s["Repair"] = ((5, 5), [[L(STEEL, [poly(*rot(wrench_pts, 45, move=(-7, -7))),
                                      poly(*rot(box(14, 20.5, 43, 27.5), 45, move=(-7, -7))),
                                      circle(*rot([(43, 24)], 45, move=(-7, -7))[0], 3.5)],
                               [poly(*rot(box(0, 21, 10, 27), 45, move=(-7, -7)))])]], 0)
    s["ResumeBuild"] = ((13, 13),[[L(DARK_STEEL, [poly(*rot(box(21.5, 14, 26.5, 46), -45, move=(-1, -1)))]),
                                  L(GOLD, [poly(*rot(box(10, 5, 38, 15), -45, move=(-1, -1)))])]], 0)
    s["Build"] = ((24, 24), [[L(GOLD, [poly((24, 6), (45, 24), (24, 42), (3, 24))],
                                [poly((24, 12), (38, 24), (24, 36), (10, 24))]),
                              L(GOLD, [rect(21.5, 16, 26.5, 32), rect(16, 21.5, 32, 26.5)])]], 0)
    s["BuildInvalid"] = ((24, 24), [greyed(s["Build"][1][0]) + [no_bar()]], 0)
    s["Invalid"] = ((24, 24), [[L(GREY, [circle(24, 24, 15)]), no_bar()]], 0)

    # hostile: red
    s["EnterHostile"] = ((24, 36), [enter(RED)], 0)
    s["Capture"] = ((24, 30), [[L(STEEL, [line([(30, 3), (30, 27)], 3)]),
                                L(STEEL, [rect(6, 26, 36, 46)], [rect(17, 34, 25, 47)]),
                                L(RED, [poly((32, 3), (46, 8), (32, 14))])]], 0)
    s["Hijack"] = ((24, 24), [[L(RED, [ring(24, 24, 15, 5), circle(24, 24, 5), line([(9, 24), (39, 24)], 4),
                                       line([(24, 24), (24, 39)], 4)])]], 0)
    s["Sabotage"] = ((24, 24), [[L(RED, [poly((29, 2), (11, 27), (22, 27), (16, 46), (37, 19), (26, 19), (34, 2))])]], 0)
    # car bomb: a steel sedan with a red charge strapped to its roof, its arming lamp lit
    s["CarBomb"] = ((24, 28), [[L(DARK_STEEL, [circle(12, 37, 5), circle(36, 37, 5)]),
                                L(STEEL, [poly((3, 36), (3, 29), (9, 27.5), (15, 20), (32, 20), (38, 27.5), (45, 29.5), (45, 36))],
                                  [poly((17, 22.5), (23, 22.5), (23, 27.5), (12.5, 27.5)),
                                   poly((25, 22.5), (31, 22.5), (35, 27.5), (25, 27.5))]),
                                L(RED, [rect(14, 9, 34, 19)]),
                                L(DARK_STEEL, [rect(14, 12.5, 34, 15)], outline=False),
                                L(RED, [circle(30, 7, 2)], glow=True)]], 0)
    s["Snipe"] = ((24, 24), [[L(RED, [ring(24, 24, 15, 2.6), line([(24, 3), (24, 20)], 2.6), line([(24, 28), (24, 45)], 2.6),
                                      line([(3, 24), (20, 24)], 2.6), line([(28, 24), (45, 24)], 2.6)]),
                              L(RED, [circle(24, 24, 1.8), circle(24, 33, 1.6), circle(24, 38, 1.6)], outline=False)]], 0)
    s["LaserMissile"] = ((17, 31), [[L(RED, [ring(17, 31, 7, 3)]), L(RED, [circle(17, 31, 2.4)], glow=True),
                                     L(DARK_STEEL, [poly(*rot([(36, 9), (32, 3), (40, 3)], 0)),
                                                    poly((39, 12), (45, 8), (45, 16))]),
                                     L(STEEL, [poly(*rot(box(26, 9, 42, 15), -45, c=(34, 12))),
                                               poly(*rot([(26, 9), (20, 12), (26, 15)], -45, c=(34, 12)))])]], 0)
    s["TNT"] = ((24, 26), [[L(RED, [rect(11, 16, 19, 44), rect(20, 16, 28, 44), rect(29, 16, 37, 44)]),
                            L(DARK_STEEL, [rect(9, 26, 39, 32)]),
                            L(STEEL, [line([(24, 16), (27, 9), (32, 6)], 2.2)]),
                            L(FLAME, [circle(33, 5.5, 2.6)], glow=True)]], 0)
    knife = [L(RED, [poly(*rot(box(29, 20, 46, 28), 45, move=(-5, -5)))]),
             L(DARK_STEEL, [poly(*rot(box(26, 14, 30, 34), 45, move=(-5, -5)))]),
             L(STEEL, [poly(*rot([(3, 24), (11, 19), (26, 19), (26, 29), (13, 29)], 45, move=(-5, -5)))])]
    s["Stab"] = ((4, 4), [knife], 0)
    s["StabInvalid"] = ((24, 24), [greyed(knife) + [no_bar()]], 0)
    charge = [L(RED, [rect(7, 18, 41, 42)]), L(DARK_STEEL, [rect(5, 27, 43, 32)]),
              L(STEEL, [line([(33, 18), (38, 5)], 2.5)]), L(RED, [circle(38, 5, 2.6)], glow=True)]
    s["RemoteCharge"] = ((24, 30), [charge], 0)
    s["ChargeInvalid"] = ((24, 24), [greyed(charge) + [no_bar()]], 0)
    # timed charge: EA's green countdown digits on a charge block, here one seven-segment digit counting 3 to 0
    s["TimedCharge"] = ((24, 30), [[L(RED, [rect(6, 14, 42, 44)]), L(STEEL, [rect(12, 9, 18, 14), rect(30, 9, 36, 14)]),
                                    L(OUTLINE, [rect(14, 20, 34, 39)]),
                                    L(SCREEN, seven_segment(digit, 19.5, 22.5, 9, 14, 2.2), outline=False, glow=True)]
                                   for digit in (3, 2, 1, 0)], 30)
    # flame: straight-cut tongues like the emblem's feathers, the hot core the one bright part
    s["Flame"] = ((24, 30), [[L(FLAME, [poly((24, 2), (29, 12), (36, 7), (35, 20), (41, 24), (37, 35), (31, 42), (24, 45),
                                            (17, 42), (11, 35), (7, 24), (13, 19), (12, 8), (19, 13))]),
                              L(GOLD, [poly((24, 17), (28, 25), (32, 23), (31, 33), (27, 39), (24, 41), (21, 39), (17, 33),
                                            (16, 23), (20, 26))], outline=False, glow=True)]], 0)
    # an olive drab bomb with its red nose falling onto the spot it marks
    s["FireBomb"] = ((24, 44), [[L(RED, [oval_ring(12, 40, 36, 47.5, 2.6)]),
                                 L(DARK_STEEL, [poly((15, 10), (12, 1), (36, 1), (33, 10))]),
                                 L(OLIVE, [ellipse(24, 23, 9, 16)]),
                                 L(RED, [ellipse(24, 33.5, 6.4, 5.5)], outline=False),
                                 L(DARK_STEEL, [rect(15, 13, 33, 15.5)], outline=False)]], 0)
    # a full face mask on its tie: brow, slanted eye holes, nose ridge, mouth slit, pointed chin
    s["Disguise"] = ((24, 24), [[L(DARK_STEEL, [line([(10, 17), (2, 13)], 2.6), line([(38, 17), (46, 13)], 2.6)]),
                                 L(RED, [poly((10, 10), (16, 5), (24, 4), (32, 5), (38, 10), (39, 22), (36, 32),
                                              (30, 40), (24, 44), (18, 40), (12, 32), (9, 22))],
                                   [poly((12.5, 16), (21, 18.5), (19.5, 23), (14, 22)),
                                    poly((35.5, 16), (27, 18.5), (28.5, 23), (34, 22)),
                                    ellipse(24, 34, 5.5, 1.8)]),
                                 L(DARK_STEEL, [poly((24, 19), (26, 29), (22, 29))], outline=False)]], 0)
    # hack: EA's field laptop with its green screen; the antenna lamp is red because the order is
    s["Hack"] = ((24, 24), [[L(STEEL, [line([(11, 9), (5, 2)], 2)]), L(RED, [circle(5, 2.5, 2.2)], glow=True),
                             L(DARK_STEEL, [rect(8, 7, 40, 33)]),
                             L(SCREEN, [rect(11.5, 10.5, 36.5, 29.5)], outline=False, glow=True),
                             L(OUTLINE, [rect(14, 14, 30, 16), rect(14, 19, 34, 21), rect(14, 24, 25, 26)], outline=False),
                             L(STEEL, [poly((7, 34), (41, 34), (46, 43), (2, 43))])]], 0)
    # the beam from above in three glow bands (blue, pale, white core) widening into a splash on the ring
    s["Uplink"] = ((24, 38), [[L(RED, [oval_ring(4, 31, 44, 45, 3.5)]),
                               L(BLUE, [poly((24 - w, 0), (24 + w, 0), (24 + w + 3, 37), (24 - w - 3, 37)),
                                        ellipse(24, 38, w + 7, 4.5)], glow=True),
                               L(BEAM, [poly((24 - w * 0.65, 0), (24 + w * 0.65, 0), (24 + w * 0.65 + 2, 37),
                                             (24 - w * 0.65 - 2, 37)), ellipse(24, 38, w + 3, 3)], outline=False, glow=True),
                               L(WHITE, [poly((24 - w * 0.3, 0), (24 + w * 0.3, 0), (24 + w * 0.3 + 1, 37),
                                              (24 - w * 0.3 - 1, 37)), ellipse(24, 38, w, 2)], outline=False, glow=True)]
                              for w in (7, 8.5, 6)], 4)
    return s


# The set as the game asks for it: MouseCursor names (Mouse.ini) and the file each takes. Orders
# with no MouseCursor of their own yet are listed by the file alone.
MOUSE_INI = {
    "Normal": "Pointer", "Arrow": "Pointer", "Scroll": "Scroll", "Target": "Target", "Move": "Move",
    "AttackMove": "AttackMove", "AttackObj": "Attack", "ForceAttackObj": "ForceAttack",
    "ForceAttackGround": "ForceAttackGround", "Build": "Build", "InvalidBuild": "BuildInvalid",
    "GenericInvalid": "Invalid", "Select": "Select", "EnterFriendly": "Enter", "EnterAggressive": "EnterHostile",
    "SetRallyPoint": "Rally", "GetRepaired": "GetRepaired", "GetHealed": "GetHealed", "DoRepair": "Repair",
    "ResumeConstruction": "ResumeBuild", "CaptureBuilding": "Capture", "SnipeVehicle": "Snipe",
    "LaserGuidedMissiles": "LaserMissile", "TankHunterTNTAttack": "TNT", "StabAttack": "Stab",
    "PlaceRemoteCharge": "RemoteCharge", "PlaceTimedCharge": "TimedCharge", "Defector": "Defector",
    "Dock": "Dock", "FireFlame": "Flame", "FireBomb": "FireBomb", "PlaceBeacon": "Beacon",
    "DisguiseAsVehicle": "Disguise", "Waypoint": "Waypoint", "OutRange": "OutRange",
    "StabAttackInvalid": "StabInvalid", "PlaceChargeInvalid": "ChargeInvalid", "Hack": "Hack",
    "ParticleUplinkCannon": "Uplink",
}
# What EA's Mouse.ini (INIZH.big) gives each of those names
RETAIL = {
    "Normal": "SCCPointer", "Arrow": "SCCPointer", "Scroll": "SCCScroll", "Target": "SCCAttack", "Move": "SCCMove",
    "AttackMove": "SCCAttMov", "AttackObj": "SCCAttack", "ForceAttackObj": "SCCAttack",
    "ForceAttackGround": "SCCAttack", "GenericInvalid": "SCCNoAction", "Select": "SCCSelect",
    "EnterFriendly": "SCCEnter", "EnterAggressive": "SCCExit", "SetRallyPoint": "SCCRallyPnt", "GetRepaired": "SCCEnter",
    "GetHealed": "SCCEnter", "DoRepair": "SCCRepair", "ResumeConstruction": "SCCResumeC", "CaptureBuilding": "SCCHostile2",
    "SnipeVehicle": "SCCSniper", "LaserGuidedMissiles": "SCCHostile", "TankHunterTNTAttack": "SCCTNTAttack",
    "StabAttack": "SCCKnifeAttack", "PlaceRemoteCharge": "SCCRemoteChg", "PlaceTimedCharge": "SCCTimedChg",
    "Defector": "SCCFriendly", "Dock": "SCCEnter", "FireFlame": "SCCHostile", "FireBomb": "SCCHostile",
    "PlaceBeacon": "SCCPlaceBeacon", "DisguiseAsVehicle": "SCCHostile3", "Waypoint": "SCCWaypoint",
    "OutRange": "SCCAttack", "StabAttackInvalid": "SCCNoKnife", "PlaceChargeInvalid": "SCCNoBomb", "Hack": "SCCCashHack",
    "ParticleUplinkCannon": "SCCSDIUplink",
}
# Orders that share a MouseCursor today, each with its own file here, and what EA showed for them
NEW_ONLY = {"Guard": "Target", "Garrison": "EnterFriendly", "Board": "EnterFriendly", "Unload": "Move",
            "Hijack": "EnterAggressive", "Sabotage": "EnterAggressive", "CarBomb": "EnterAggressive"}
PREFIX = "Reforged"
HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.normpath(os.path.join(HERE, "..", "Data", "Cursors"))
RETAIL_DIR = os.path.normpath(os.path.join(HERE, "..", "..", "Run", "Data", "Cursors"))


def terrain(w, h, base, seed):
    rng = np.random.default_rng(seed)
    noise = Image.fromarray((rng.random((h // 4 + 1, w // 4 + 1)) * 255).astype(np.uint8)).resize((w, h), Image.BICUBIC)
    n = np.asarray(noise, float)[..., None] / 255 - 0.5
    fine = rng.random((h, w, 1)) - 0.5
    px = np.clip(np.array(base, float) * (1 + 0.35 * n + 0.08 * fine), 0, 255).astype(np.uint8)
    return Image.fromarray(px, "RGB").convert("RGBA")


def contact_sheet(rendered, path):
    names = list(rendered)
    cols, tw, th = 3, 372, 122
    rows = (len(names) + cols - 1) // cols
    sheet = Image.new("RGBA", (cols * tw, rows * th), (24, 26, 30, 255))
    dark = terrain(sheet.width, sheet.height, (58, 66, 44), 1)
    light = terrain(sheet.width, sheet.height, (200, 180, 140), 2)
    font = ImageFont.load_default(12)
    d = ImageDraw.Draw(sheet)
    for i, name in enumerate(names):
        hot, frames48, frames32 = rendered[name]
        x0, y0 = (i % cols) * tw + 6, (i // cols) * th + 4
        d.text((x0, y0), "%s%s  hot %d,%d  %d frame%s" % (PREFIX, name, hot[0], hot[1], len(frames48),
                                                       "" if len(frames48) == 1 else "s"), fill=(232, 234, 240), font=font)
        y = y0 + 18
        views = [(dark, frames48[0], 1, 56), (light, frames48[0], 1, 56), (dark, frames48[0], 2, 100),
                 (light, frames48[0], 2, 100), (dark, frames32[0], 1, 40)]
        x = x0
        for bg, im, scale, box_w in views:
            region = bg.crop((x, y, x + box_w, y + 100))
            pic = im.resize((im.width * scale, im.height * scale), Image.NEAREST) if scale > 1 else im
            ox, oy = (box_w - pic.width) // 2, (100 - pic.height) // 2
            region.alpha_composite(pic, (ox, oy))
            if scale == 2:  # the hotspot, one magenta pixel pair
                hx, hy = ox + hot[0] * 2, oy + hot[1] * 2
                region.putpixel((min(hx, box_w - 1), min(hy, 99)), (255, 0, 255, 255))
            sheet.alpha_composite(region, (x, y))
            x += box_w + 2
    sheet.convert("RGB").save(path)


def comparison(rendered, retail_dir, path):
    """EA's cursor and ours for every Mouse.ini name, each at its own size and at 2x, on both grounds."""
    rows = [(key, RETAIL[key], MOUSE_INI[key] if key != "Scroll" else "Scroll0") for key in RETAIL]
    rows += [(name + " (new)", RETAIL[shared], name) for name, shared in NEW_ONLY.items()]
    rows += [("Build (no EA art)", None, "Build"), ("InvalidBuild (no EA art)", None, "BuildInvalid")]
    files = {f.lower(): f for f in os.listdir(retail_dir)}
    cols, tw, th = 2, 560, 120
    sheet = Image.new("RGBA", (cols * tw, ((len(rows) + cols - 1) // cols) * th + 24), (24, 26, 30, 255))
    dark = terrain(sheet.width, sheet.height, (58, 66, 44), 1)
    light = terrain(sheet.width, sheet.height, (200, 180, 140), 2)
    font = ImageFont.load_default(12)
    d = ImageDraw.Draw(sheet)
    d.text((6, 4), "each row: EA retail 32 px (1x dark, 1x light, 2x)  |  Reforged 48 px (1x dark, 1x light, 2x)",
           fill=(232, 234, 240), font=font)
    for i, (key, retail, ours) in enumerate(rows):
        x0, y0 = (i % cols) * tw + 6, (i // cols) * th + 24
        d.text((x0, y0), "%s   EA %s  ->  %s%s" % (key, retail, PREFIX, ours), fill=(232, 234, 240), font=font)
        theirs = Image.new("RGBA", (32, 32))
        if retail:
            name = retail + ("0" if retail == "SCCScroll" else "")
            with open(os.path.join(retail_dir, files[name.lower() + ".ani"]), "rb") as f:
                w, h, _, _, px, _ = read_ani(f.read())[0][0]
            theirs = Image.frombytes("RGBA", (w, h), px)
        mine = rendered[ours][1][0]
        x = x0
        for im, gap in ((theirs, 14), (mine, 40)):
            for bg, scale, box_w in ((dark, 1, 56), (light, 1, 56), (dark, 2, 100)):
                region = bg.crop((x, y0 + 16, x + box_w, y0 + 16 + 100))
                pic = im.resize((im.width * scale, im.height * scale), Image.NEAREST) if scale > 1 else im
                region.alpha_composite(pic, ((box_w - pic.width) // 2, (100 - pic.height) // 2))
                sheet.alpha_composite(region, (x, y0 + 16))
                x += box_w + 2
            x += gap
    sheet.convert("RGB").save(path)


MOUSE_INI_MASTER = os.path.normpath(os.path.join(HERE, "..", "Data", "INI", "Mouse.ini"))


def check_mouse_ini(folder):
    """Every Texture the Mouse.ini master names has its .ani here (Scroll as 0 to 7), and every cursor
    this set draws for a MouseCursor name is named there: a gap is an invisible pointer in the game."""
    blocks, name = {}, None
    with open(MOUSE_INI_MASTER) as f:
        for raw in f:
            words = raw.split(";")[0].split()
            if words[:1] == ["MouseCursor"]:
                name = words[1]
            elif name and words[:2] == ["Texture", "="]:
                blocks[name] = words[2]
    on_disk = {f.lower() for f in os.listdir(folder)}
    for key, texture in blocks.items():
        wanted = [texture + "%d" % i for i in range(8)] if key == "Scroll" else [texture]
        for w in wanted:
            assert (w + ".ani").lower() in on_disk, "Mouse.ini %s names %s, which is not in %s" % (key, w, folder)
    for key, ours in MOUSE_INI.items():
        assert blocks.get(key) == PREFIX + ours, "Mouse.ini %s should name %s%s" % (key, PREFIX, ours)
    for ours in NEW_ONLY:
        assert blocks.get(ours) == PREFIX + ours, "Mouse.ini has no %s" % ours


def _windows_load(path):
    import ctypes
    u = ctypes.windll.user32
    u.LoadCursorFromFileW.restype = ctypes.c_void_p
    h = u.LoadCursorFromFileW(path)
    if h:
        u.DestroyCursor(ctypes.c_void_p(h))
    return bool(h)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--sheet", help="write a contact sheet PNG here")
    ap.add_argument("--compare", help="write EA's set beside this one to this PNG")
    ap.add_argument("--retail", default=RETAIL_DIR, help="EA's Data/Cursors, for --compare")
    ap.add_argument("--out", default=OUT)
    args = ap.parse_args()
    os.makedirs(args.out, exist_ok=True)

    rendered = {}
    for name, (hot, frames, jiffies) in build_set().items():
        big = [render(f, 48) for f in frames]
        small = [render(f, 32) for f in frames]
        curs = [cur_bytes([b, s], hot) for b, s in zip(big, small)]
        path = os.path.join(args.out, PREFIX + name + ".ani")
        with open(path, "wb") as f:
            f.write(ani_bytes(curs, jiffies or 60))
        rendered[name] = (hot, big, small)

        # read it back the way AniCursor.cpp does
        with open(path, "rb") as f:
            data = f.read()
        got, steps = read_ani(data)
        assert len(got) == len(frames) and len(steps) == len(frames), name
        for (w, h, hx, hy, px, inverted), im in zip(got, big):
            assert (w, h, hx, hy) == (48, 48) + tuple(hot), (name, w, h, hx, hy)
            assert inverted == 0 and px == im.tobytes(), name
        assert all(_le16(c, 4) == 2 and c[6 + 16] == 32 for c in curs), name  # second entry is the 32
        if sys.platform == "win32":
            assert _windows_load(path), "LoadCursorFromFile refused " + path

    missing = {v for v in MOUSE_INI.values() if v != "Scroll"} - set(rendered)
    assert not missing, missing
    check_mouse_ini(args.out)
    print("%d cursors, %d files in %s" % (len(rendered) - 7, len(rendered), args.out))
    if args.sheet:
        os.makedirs(os.path.dirname(os.path.abspath(args.sheet)), exist_ok=True)
        contact_sheet(rendered, args.sheet)
        print("contact sheet", args.sheet)
    if args.compare:
        comparison(rendered, args.retail, args.compare)
        print("comparison", args.compare)


if __name__ == "__main__":
    main()
