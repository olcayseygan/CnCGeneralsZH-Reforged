"""The Reforged mouse cursors, drawn by script and written as Windows .ANI files.

They follow EA's 2003 set mark for mark: four green arrowheads closing in for move, four red
triangles round a yellow dot for attack, red with green between them for attack move, white
corner brackets for select, a white dart for the pointer, green block arrows at the screen edge,
stacked triangles for enter, a red ring for anything that cannot be done, and a small pictogram
with a white arrow tip for each unit ability. Orders EA never drew a mark for (guard, garrison,
board, unload, hijack, sabotage, car bomb, the refused build) get one in the same hand.

The hand is EA's too: only the 16 colour Windows palette (13 of its colours), flat fills, a 1 px
black line round every shape, and at most a lit half and a half-bright half per shape. No
gradients, no soft edges; every pixel is opaque or not there, and the self-check fails the run
on any pixel that is neither or off the palette. Nothing is copied from EA's files: every mark
is built here from polygons, lines and circles and rasterised straight at each size.

Each frame is a .CUR holding two 32-bit images, 48x48 first and 32x32 second, the same design
drawn natively at each size. The game draws the one nearest the system's cursor size, so 32 at
100 % DPI and 48 from about 150 %:

    Win32Mouse   LoadImage(.., 0, 0, LR_LOADFROMFILE | LR_DEFAULTSIZE), nearest SM_CXCURSOR
    SdlMouse     AniCursor_decode(.., 32 x the display's content scale), nearest that width

Hotspots are in the 48 frame; the 32 entry's are scaled with it. Order marks put the hotspot in
the middle of the mark, the pointer and scroll arrows on their tip, pictograms on their arrow tip,
pins and flags on their foot.

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
# Drawing. Coordinates are in a 48-unit frame, (0, 0) top left; each size rasterises the same
# shapes at its own scale with no anti-aliasing, so pixel i of the 48 sits at unit i and pixel i
# of the 32 at unit 1.5 i.

SIZES = (48, 32)
# The colours EA's cursors are painted in, all from the 16 colour Windows palette
K, W, LG, DG = (0, 0, 0), (255, 255, 255), (192, 192, 192), (128, 128, 128)
R, DR, G, DGR = (255, 0, 0), (128, 0, 0), (0, 255, 0), (0, 128, 0)
B, DB, C, Y, OL = (0, 0, 255), (0, 0, 128), (0, 255, 255), (255, 255, 0), (128, 128, 0)
PALETTE = {K, W, LG, DG, R, DR, G, DGR, B, DB, C, Y, OL}
HALF = {W: LG, LG: DG, R: DR, G: DGR, B: DB, Y: OL, C: B}   # the shaded half of each colour
CX = CY = 24


def _w(w, k):
    return max(1, round(w * k))


def poly(*pts):
    return lambda d, k: d.polygon([(x * k, y * k) for x, y in pts], fill=255)


def circle(cx, cy, r):
    return lambda d, k: d.ellipse([(cx - r) * k, (cy - r) * k, (cx + r) * k, (cy + r) * k], fill=255)


def ring(cx, cy, r, w):
    """A band of width w inside radius r."""
    return lambda d, k: d.ellipse([(cx - r) * k, (cy - r) * k, (cx + r) * k, (cy + r) * k], outline=255, width=_w(w, k))


def oval_ring(x0, y0, x1, y1, w):
    """A flat ellipse's band, the ground at the game's camera angle."""
    return lambda d, k: d.ellipse([x0 * k, y0 * k, x1 * k, y1 * k], outline=255, width=_w(w, k))


def arc(cx, cy, r, w, a0, a1):
    """Band of width w inside radius r, degrees clockwise from 3 o'clock."""
    return lambda d, k: d.arc([(cx - r) * k, (cy - r) * k, (cx + r) * k, (cy + r) * k], a0, a1, fill=255, width=_w(w, k))


def rect(x0, y0, x1, y1):
    def draw(d, k):
        a, b = round(x0 * k), round(y0 * k)
        d.rectangle([a, b, max(a, round(x1 * k) - 1), max(b, round(y1 * k) - 1)], fill=255)
    return draw


def line(pts, w):
    return lambda d, k: d.line([(x * k, y * k) for x, y in pts], fill=255, width=_w(w, k))


def rot(pts, deg, c=(CX, CY)):
    s, co = math.sin(math.radians(deg)), math.cos(math.radians(deg))
    return [(c[0] + (x - c[0]) * co - (y - c[1]) * s, c[1] + (x - c[0]) * s + (y - c[1]) * co) for x, y in pts]


def unit(deg):
    return math.cos(math.radians(deg)), math.sin(math.radians(deg))


def dart(tip, deg, length, half, notch=0.0):
    """Arrowhead with its tip at tip pointing deg (clockwise from right); notch cuts its back in."""
    ux, uy = unit(deg)
    bx, by = tip[0] - ux * length, tip[1] - uy * length
    m = (tip[0] - ux * length * (1 - notch), tip[1] - uy * length * (1 - notch))
    return [tip, (bx - uy * half, by + ux * half), m, (bx + uy * half, by - ux * half)]


def toward(pts, o, f):
    return [(o[0] + (x - o[0]) * f, o[1] + (y - o[1]) * f) for x, y in pts]


def side(deg):
    """The half of a shape pointing deg that is in shadow: the one facing down and right."""
    ux, uy = unit(deg)
    v = (-uy, ux)
    s = v[0] + v[1]
    return (-v[0], -v[1]) if s < -1e-6 or (abs(s) <= 1e-6 and v[0] < 0) else v


class L:
    """One layer: shapes added, cuts taken away, clipped to clip if given, a 1 px black line
    round it, and if shade is a direction, the half of each piece lying that way in HALF's colour."""

    def __init__(self, colour, shapes, cuts=(), outline=True, shade=None, clip=None):
        self.colour, self.shapes, self.cuts, self.outline, self.shade, self.clip = colour, list(shapes), list(cuts), outline, shade, clip


def _mask(shapes, size, k):
    im = Image.new("L", (size, size))
    d = ImageDraw.Draw(im)
    for s in shapes:
        s(d, k)
    return np.asarray(im) > 127


EIGHT = np.ones((3, 3), bool)


def render(layers, size):
    k = size / 48
    out = np.zeros((size, size, 4), np.uint8)
    ys, xs = np.mgrid[0:size, 0:size]
    for layer in layers:
        m = _mask(layer.shapes, size, k)
        if layer.cuts:
            m &= ~_mask(layer.cuts, size, k)
        if layer.clip:
            m &= _mask(layer.clip, size, k)
        if not m.any():
            continue
        if layer.outline:
            out[ndimage.binary_dilation(m, EIGHT) & ~m] = K + (255,)
        out[m] = layer.colour + (255,)
        if layer.shade and layer.colour in HALF:
            vx, vy = layer.shade
            lab, n = ndimage.label(m)
            for i in range(1, n + 1):
                piece = lab == i
                cx, cy = xs[piece].mean(), ys[piece].mean()
                out[piece & ((xs - cx) * vx + (ys - cy) * vy > 0.01)] = HALF[layer.colour] + (255,)
    return Image.fromarray(out, "RGBA")


class T:
    """Places a pictogram drawn in the 48 frame smaller and elsewhere (inside a no-sign)."""

    def __init__(self, sc=1.0, dx=0.0, dy=0.0):
        self.sc, self.dx, self.dy = sc, dx, dy

    def __call__(self, x, y):
        return (x * self.sc + self.dx, y * self.sc + self.dy)

    def ps(self, pts):
        return [self(x, y) for x, y in pts]

    def w(self, v):
        return v * self.sc


ID = T()


# ---------------------------------------------------------------------------------------------
# The marks

def centre_dot():
    """EA's yellow pip in the middle of move and attack."""
    return L(Y, [poly((24, 21.5), (26.5, 24), (24, 26.5), (21.5, 24))])


def tip_arrow():
    """The small white arrow EA puts at the top left of every ability pictogram; the hotspot is its tip."""
    return L(W, [poly((2, 2), (13, 6), (9.5, 9.5), (6, 13))], shade=(1, -1))


def attack_tris(d, colour=R, length=12, half=8):
    """Four triangles on the axes, pointing in, their tips d from the centre."""
    shapes = []
    for deg in (90, 270, 0, 180):
        ux, uy = unit(deg)
        shapes.append(poly(*dart((CX - d * ux, CY - d * uy), deg, length, half)))
    return L(colour, shapes)


def diag_darts(d, colour=G, length=12, half=6.5, notch=0.12, shade=True):
    """Four arrowheads on the diagonals, pointing in."""
    out = []
    for deg in (45, 135, 225, 315):
        ux, uy = unit(deg)
        out.append(L(colour, [poly(*dart((CX - d * ux, CY - d * uy), deg, length, half, notch))],
                     shade=side(deg) if shade else None))
    return out


def swept_darts(d, colour, sweep, f, length=12, half=6, notch=0.12):
    """Out of range and the uplink: the diagonal darts, a second colour filling each from its tip."""
    out = [L(colour, [poly(*dart((CX - d * unit(deg)[0], CY - d * unit(deg)[1]), deg, length, half, notch))
                      for deg in (45, 135, 225, 315)])]
    if f > 0:
        shapes = []
        for deg in (45, 135, 225, 315):
            tip = (CX - d * unit(deg)[0], CY - d * unit(deg)[1])
            shapes.append(poly(*toward(dart(tip, deg, length, half, notch), tip, f)))
        out.append(L(sweep, shapes, outline=False))
    return out


def brackets(h, arm, w, c=(CX, CY)):
    lo_x, lo_y, hi_x, hi_y = c[0] - h, c[1] - h, c[0] + h, c[1] + h
    out = []
    for x, sx in ((lo_x, 1), (hi_x, -1)):
        for y, sy in ((lo_y, 1), (hi_y, -1)):
            out.append(rect(*_box(x, y, x + sx * arm, y + sy * w)))
            out.append(rect(*_box(x, y, x + sx * w, y + sy * arm)))
    return out


def _box(x0, y0, x1, y1):
    return min(x0, x1), min(y0, y1), max(x0, x1), max(y0, y1)


def no_ring(cross=False):
    """EA's refusal: a thick red ring with a slash, or with an X for the plain one."""
    bars = ([line([(15, 15), (33, 33)], 5), line([(33, 15), (15, 33)], 5)] if cross
            else [line([(10, 10), (38, 38)], 5)])
    return L(R, [ring(CX, CY, 21, 5)] + bars, shade=(-1, 1))


def stack(colour, tops, bright, half=11, h=9, up=False, x=CX):
    """EA's enter mark: triangles stacked one under the other, the one at bright lit, the others dark."""
    out = []
    for i, top in enumerate(tops):
        pts = [(x - half, top + h), (x + half, top + h), (x, top)] if up else [(x - half, top), (x + half, top), (x, top + h)]
        out.append(L(colour if i == bright else HALF[colour], [poly(*pts)]))
    return out


def pointer():
    return [L(W, [poly((2, 2), (16, 30), (18, 18), (30, 16))], shade=(1, -1))]


def scroll(i):
    """Right, then clockwise in 45 degree steps. The diagonals are drawn as EA drew them, a square
    corner at the tip with a slanted shaft, not the straight arrow turned (that rasterises ragged)."""
    if i % 2 == 0:
        pts = [(45, 24), (33, 12), (33, 19), (22, 19), (22, 29), (33, 29), (33, 36)]
    else:
        pts = [(45, 45), (45, 28), (39.7, 33.3), (30.7, 24.3), (24.3, 30.7), (33.3, 39.7), (28, 45)]
    pts = rot(pts, 90 * (i // 2))
    return pts[0], [L(G, [poly(*pts)])]   # EA's scroll arrows are flat green


def pattee(r, inner, outer, c=(CX, CY)):
    """A cross whose four arms flare outward: EA's waypoint and placement mark."""
    arm = [(c[0] - inner, c[1]), (c[0] + inner, c[1]), (c[0] + outer, c[1] - r), (c[0] - outer, c[1] - r)]
    return [poly(*rot(arm, a, c)) for a in (0, 90, 180, 270)]


def block_arrow(d, deg):
    """A white rally arrow pointing deg at the centre, its tip d away."""
    pts = [(24, 24 - d), (31, 17 - d), (27, 17 - d), (27, 11 - d), (21, 11 - d), (21, 17 - d), (17, 17 - d)]
    return L(W, [poly(*rot(pts, deg))], shade=side(deg + 90))


SEGMENTS = {0: "abcdef", 1: "bc", 2: "abdeg", 3: "abcdg", 4: "bcfg", 5: "acdfg", 6: "acdefg", 7: "abc",
            8: "abcdefg", 9: "abcdfg"}


def seven_segment(digit, x, y, w, h, t):
    m = y + h / 2
    seg = {"a": (x, y, x + w, y + t), "b": (x + w - t, y, x + w, m), "c": (x + w - t, m, x + w, y + h),
           "d": (x, y + h - t, x + w, y + h), "e": (x, m, x + t, y + h), "f": (x, y, x + t, m),
           "g": (x, m - t / 2, x + w, m + t / 2)}
    return [rect(*seg[c]) for c in SEGMENTS[digit]]


# pictograms -----------------------------------------------------------------------------------

def knife(t=ID, glint=None):
    blade = t.ps([(3, 3), (10.8, 6.5), (26.7, 21.7), (21.7, 26.7), (6.5, 10.8)])
    out = [L(W, [poly(*blade)], shade=(-1, 1)),
           L(G, [line(t.ps([(25.5, 25.5), (38, 38)]), t.w(5))], shade=(-1, 1)),
           L(DG, [line(t.ps([(29.7, 18.7), (18.7, 29.7)]), t.w(3.5))]),
           L(DG, [circle(*t(39.5, 39.5), t.w(3))])]
    if glint is not None:   # a white spark running down the blade's shaded edge
        gx, gy = t(5 + glint - 1.2, 5 + glint + 1.2)
        out.append(L(W, [poly((gx, gy - t.w(2)), (gx + t.w(2), gy), (gx, gy + t.w(2)), (gx - t.w(2), gy))], outline=False))
    return out


def sticks(t, top, bottom=46):
    """Three red dynamite sticks bound by a white band."""
    return [L(R, [rect(*t(15, top), *t(33, bottom))]),
            L(DR, [rect(*t(20.5, top), *t(22, bottom)), rect(*t(26.5, top), *t(28, bottom))], outline=False),
            L(W, [rect(*t(14, bottom - 10), *t(34, bottom - 6.5))], shade=(0, 1))]


def wires(t):
    return [L(B, [arc(*t(32, 30), t.w(6), t.w(1.5), -90, 90)], outline=False),
            L(DGR, [arc(*t(32, 37), t.w(5), t.w(1.5), -90, 90)], outline=False)]


def remote_charge(t=ID, lamp=True):
    out = sticks(t, 22) + wires(t) + [
        L(DG, [rect(*t(18, 14), *t(30, 22))]),
        L(G, [rect(*t(22.5, 15.5), *t(25.5, 20.5))], outline=False),
        L(K, [line(t.ps([(24, 3), (24, 14)]), t.w(1.5)), line(t.ps([(21, 6), (27, 6)]), t.w(1.5))], outline=False)]
    if lamp:
        out.append(L(Y, [rect(*t(19, 12), *t(21.5, 14)), rect(*t(26.5, 12), *t(29, 14))], outline=False))
    return out


def building(colour=LG):
    """A low crenellated block with a door, a building at the cursor's size."""
    return L(colour, [poly((8, 46), (8, 32), (12, 32), (12, 28), (18, 28), (18, 32), (30, 32), (30, 28), (36, 28),
                           (36, 32), (40, 32), (40, 46))], [rect(20, 38, 28, 47)], shade=(1, 0))


def car():
    return [L(LG, [poly((3, 40), (3, 33), (10, 31), (16, 24), (32, 24), (38, 31), (45, 33), (45, 40))],
              [poly((18, 26.5), (23, 26.5), (23, 31), (13.5, 31)), poly((25, 26.5), (30, 26.5), (34.5, 31), (25, 31))],
              shade=(0, 1)),
            L(DG, [circle(12, 41, 4.5), circle(36, 41, 4.5)])]


def build_cross(colour):
    lobes = L(W if colour == Y else LG, [circle(24 + sx * 8.5, 24 + sy * 8.5, 7.5) for sx in (-1, 1) for sy in (-1, 1)], shade=(1, 1))
    cross = L(colour, [rect(22, 6, 26, 42), rect(6, 22, 42, 26)] +
              [poly(*rot([(17, 4), (31, 4), (27, 9), (21, 9)], a)) for a in (0, 90, 180, 270)])
    return [lobes, cross]


def wrench(deg=0.0, c=(4, 4)):
    def p(pts):
        return rot(pts, deg, c)
    u, n = (0.7071, 0.7071), (0.7071, -0.7071)
    head = (10, 10)
    slot = [(head[0] + u[0] * s + n[0] * h, head[1] + u[1] * s + n[1] * h) for s, h in ((1, 2.6), (1, -2.6), (-12, -2.6), (-12, 2.6))]
    return [L(W, [circle(*p([head])[0], 7.5), line(p([head, (36, 36)]), 5.5), circle(*p([(37, 37)])[0], 6)],
              [poly(*p(slot)), circle(*p([(37, 37)])[0], 2.6)], shade=rot([(-1, 1)], deg, (0, 0))[0])]


def bomb_ball(spark):
    out = [L(R, [circle(24, 14, 7)], shade=(1, 1)),
           L(K, [line([(28, 9), (32, 4)], 1.5)], outline=False)]
    if spark:
        out.append(L(Y, [rect(31, 1, 35, 3), rect(32, 0, 34, 4)], outline=False))
    return out


# ---------------------------------------------------------------------------------------------
# The set. Each entry: name -> (hotspot in the 48 frame, list of frames as layer lists, jiffies)

def build_set():
    s = {}
    s["Pointer"] = ((2, 2), [pointer()], 0)
    for i in range(8):
        tip, layers = scroll(i)
        s["Scroll%d" % i] = ((round(tip[0]), round(tip[1])), [layers], 0)
    s["Select"] = ((24, 24), [[L(W, brackets(h, 7, 3))] for h in (10, 12, 14, 16, 18)], 5)

    # the attack family: red triangles on the axes round the yellow pip, spreading
    spread = (4, 5.5, 7, 8.5)
    s["Target"] = ((24, 24), [[attack_tris(6), centre_dot()]], 0)
    s["Attack"] = ((24, 24), [[attack_tris(d), centre_dot()] for d in spread], 6)
    s["ForceAttack"] = ((24, 24), [[L(R, brackets(21, 7, 3)), attack_tris(d), centre_dot()] for d in spread], 6)
    # attack on bare ground: the triangles close on a patch of ground instead of the pip
    s["ForceAttackGround"] = ((24, 24), [[L(R, [oval_ring(15.5, 20, 32.5, 28, 2.5)]), attack_tris(d, length=10, half=6.5)]
                                         for d in (9.5, 11, 12.5)], 6)
    s["Move"] = ((24, 24), [diag_darts(d) + [centre_dot()] for d in (3, 5, 7, 9)], 5)
    s["AttackMove"] = ((24, 24), [diag_darts(g, length=10, half=6) + [attack_tris(g + 6.5, length=10, half=7), centre_dot()]
                                  for g in (3, 4.5, 6)], 6)
    sweep = (0, 0.3, 0.55, 0.8, 0.55, 0.3)
    s["OutRange"] = ((24, 24), [swept_darts(7.5, R, G, f) for f in sweep], 5)
    s["Uplink"] = ((24, 24), [swept_darts(7.5, B, C, f) for f in sweep], 5)
    snipe = []
    for lit in (0, 1, 2, 3, 3):
        ticks = {r: (Y if i < lit else R) for i, r in enumerate((16, 12, 8))}
        layers = [L(R, [ring(24, 24, 21, 3)], shade=(-1, 1)),
                  L(R, [rect(23, 4, 25, 44), rect(4, 23, 44, 25)])]
        for r, col in ticks.items():
            layers.append(L(col, [rect(21, 24 - r - 1, 27, 24 - r + 1), rect(21, 24 + r - 1, 27, 24 + r + 1),
                                  rect(24 - r - 1, 21, 24 - r + 1, 27), rect(24 + r - 1, 21, 24 + r + 1, 27)], outline=False))
        snipe.append(layers)
    s["Snipe"] = ((24, 24), snipe, 6)
    s["LaserMissile"] = ((24, 24), [[L(R, [poly(*dart((24 + d * unit(a)[0], 24 + d * unit(a)[1]), a + 180, 7, 3))
                                           for a in range(0, 360, 45)]), centre_dot()] for d in (7, 9, 11, 13)], 6)

    # places on the map
    veins = [rect(23.5, 14, 24.5, 34), rect(14, 23.5, 34, 24.5)]
    s["Waypoint"] = ((24, 24), [[L(G, pattee(11, 3, 7)), L(DGR, veins, outline=False)],
                                [L(DGR, pattee(11, 3, 7)), L(G, veins, outline=False)]], 30)
    s["Build"] = ((24, 24), [build_cross(Y), build_cross(OL)], 30)
    s["BuildInvalid"] = ((24, 24), [build_cross(DG) + [no_ring()]], 0)
    s["Invalid"] = ((24, 24), [[no_ring(cross=True)]], 0)
    s["Rally"] = ((24, 24), [[block_arrow(d, a) for a in (0, 90, 180, 270)] for d in (9, 7, 5, 3)], 6)
    beacon = []
    for r in (0, 4, 8, 12, 16):
        layers = [L(W, [rect(22, 19, 26, 39)], shade=(1, 0)),
                  L(W, [poly((24, 46), (16, 40), (16, 37), (32, 37), (32, 40))], shade=(1, 0)),
                  L(DR, [circle(24, 18, 3.5)])]
        if r:
            dots = 8 if r < 8 else 12
            layers.append(L(R, [rect(24 + r * unit(a)[0] - 1, 18 + r * unit(a)[1] - 1, 24 + r * unit(a)[0] + 1, 18 + r * unit(a)[1] + 1)
                                for a in np.arange(dots) * 360 / dots + 360 / dots / 2], outline=False))
        beacon.append(layers)
    s["Beacon"] = ((24, 46), beacon, 6)
    s["Guard"] = ((2, 2), [[tip_arrow(),
                            L(W, [poly((14, 8), (44, 8), (44, 24), (39, 35), (29, 44), (19, 35), (14, 24))], shade=(1, 0)),
                            L(G, [line([(46, 6), (14, 38)], 7)], outline=False,
                              clip=[poly((15, 9), (43, 9), (43, 24), (38.5, 34.5), (29, 43), (19.5, 34.5), (15, 24))])]], 0)

    # going in and out: EA's stacked triangles, the lit one running down the stack
    tops = (10, 20, 30)
    s["Enter"] = ((24, 24), [stack(G, tops, i) for i in range(3)], 6)
    s["EnterHostile"] = ((24, 24), [stack(R, tops, i) for i in range(3)], 6)
    # unload: EA's exit stack turned to point out, as chevrons rising off the hull's floor
    s["Unload"] = ((24, 24), [[L(G if j == i else DGR, [poly((11, top + 8), (24, top), (37, top + 8), (37, top + 12), (24, top + 4),
                                                         (11, top + 12))]) for j, top in enumerate((26, 16, 6))] +
                              [L(W, [rect(8, 40, 40, 44)], shade=(0, 1))] for i in range(3)], 6)
    s["Garrison"] = ((24, 24), [stack(G, (8, 18), i, half=10, h=8) + [building()] for i in range(2)], 15)
    s["Board"] = ((24, 24), [stack(G, (6, 15), i, half=10, h=8) +
                             [L(LG, [poly((4, 38), (4, 32), (10, 27), (38, 27), (44, 32), (44, 38))], shade=(0, 1)),
                              L(DG, [circle(12, 40, 4), circle(24, 40, 4), circle(36, 40, 4)])] for i in range(2)], 15)
    s["Hijack"] = ((24, 24), [stack(R, (2, 11), i, half=9, h=8) + car() for i in range(2)], 15)
    s["CarBomb"] = ((24, 28), [bomb_ball(on) + car() for on in (True, False)], 15)
    bolt = [(28, 2), (12, 26), (22, 26), (17, 46), (36, 19), (26, 19), (33, 2)]
    s["Sabotage"] = ((24, 24), [[L(Y, [poly(*bolt)], shade=(1, 0))], [L(W, [poly(*bolt)], shade=(1, 0))]], 20)
    s["Dock"] = ((24, 24), [[L(LG, [rect(32, 8, 44, 40)], [rect(30, 14, 39, 34)], shade=(0, 1))] +
                            [L(G if j == i else DGR, [poly((x, 15), (x, 33), (x + 9, 24))]) for j, x in enumerate((4, 13, 22))]
                            for i in range(3)], 6)
    s["GetRepaired"] = ((24, 29), [stack(G, (19, 30), i, half=13, h=9) +
                                   [L(W, [circle(10, 9, 5), rect(13, 7, 34, 11), circle(37, 9, 4.5)],
                                      [rect(2, 7, 9, 11), circle(37, 9, 2)], shade=(0, 1))] for i in range(2)], 15)
    s["GetHealed"] = ((2, 2), [[tip_arrow(), L(W, [circle(28, 28, 17)], shade=(1, 1)),
                                L(R, [rect(24, 17, 32, 39), rect(17, 24, 39, 32)])]], 0)
    # EA's defector mark: a ring of blue beads round a cyan one, a cyan bead running round it
    s["Defector"] = ((24, 24), [[L(C if j == i else B, [circle(x, y, 2.5)])
                                 for j, (x, y) in enumerate((round(24 + 17 * unit(a)[0]), round(24 + 17 * unit(a)[1])) for a in range(-90, 270, 45))] +
                                [L(C, [rect(22, 19.5, 26, 28.5), rect(19.5, 22, 28.5, 26)])] for i in range(8)], 5)
    s["Capture"] = ((30, 46), [[L(W, [rect(28.5, 6, 31.5, 40)], shade=(1, 0)), L(W, [circle(30, 5, 2.5)]),
                                L(R, [poly((28, 7), (18, 8 + a), (9, 11 + a), (18, 14 + a), (28, 17))], shade=(0, 1)),
                                L(W, [poly(*dart((30, 46), 90, 10, 8, 0.35))], shade=(1, 0))] for a in (0, 2, 0, -2)], 6)

    # tools and abilities: a pictogram with EA's arrow tip
    s["Repair"] = ((4, 4), [wrench(a) for a in (0, -7, 0, 7)], 6)
    resume = []
    for rows in (1, 2, 3, 4, 5, 5):
        layers = [tip_arrow(), L(W, [ring(27, 27, 18, 3)], shade=(1, 1)), L(DR, [circle(27, 27, 15)], outline=False)]
        bricks = []
        for j in range(rows):
            y1 = 40.5 - 6 * j
            off = 0 if j % 2 == 0 else 4
            bricks += [rect(x, y1 - 4.5, x + 6.5, y1) for x in range(9 - off, 46, 8)]
        if bricks:
            layers.append(L(R, bricks, [ring(27, 27, 32, 17)], outline=False))
        resume.append(layers)
    s["ResumeBuild"] = ((2, 2), resume, 6)
    s["Stab"] = ((3, 3), [knife(glint=g) for g in (None, 4, 10, 16, None)], 5)
    s["StabInvalid"] = ((24, 24), [[no_ring()] + knife(T(0.62, 9.7, 9.7))], 0)
    s["RemoteCharge"] = ((2, 2), [[tip_arrow()] + remote_charge(lamp=on) for on in (True, False)], 20)
    s["ChargeInvalid"] = ((24, 24), [[no_ring()] + remote_charge(T(0.7, 7.2, 5.5))], 0)
    s["TimedCharge"] = ((2, 2), [[tip_arrow()] + sticks(ID, 24) + wires(ID) +
                                 [L(DG, [rect(12, 7, 36, 24)]), L(K, [rect(14.5, 9.5, 33.5, 21.5)], outline=False),
                                  L(G, seven_segment(digit, 19.5, 10.5, 9, 10, 2.2), outline=False)]
                                 for digit in range(9, -1, -1)], 30)
    tnt = []
    for spark in range(4):
        n = (3.54, -3.54)
        layers = [L(R, [line([(17 + n[0] * o, 17 + n[1] * o), (39 + n[0] * o, 39 + n[1] * o)], 5) for o in (-1.4, 0, 1.4)]),
                  L(DR, [line([(17 + n[0] * o, 17 + n[1] * o), (39 + n[0] * o, 39 + n[1] * o)], 1.2) for o in (-0.7, 0.7)],
                    outline=False),
                  L(W, [line([(28 + n[0] * 2, 28 + n[1] * 2), (28 - n[0] * 2, 28 - n[1] * 2)], 3.5)]),
                  L(K, [line([(17, 17), (9, 9)], 1.5)], outline=False)]
        if spark % 2 == 0:
            layers.append(L(Y, [rect(6, 4, 10, 12), rect(4, 6, 12, 10)], outline=False))
        else:
            layers.append(L(W, [rect(7, 7, 9, 9)] + [rect(7 + dx, 7 + dy, 9 + dx, 9 + dy) for dx, dy in ((-3, -3), (3, -3), (-3, 3), (3, 3))],
                            outline=False))
        tnt.append(layers)
    s["TNT"] = ((8, 8), tnt, 4)
    flames = []
    for a, b in ((0, 0), (-2, 2), (2, -1)):
        flames.append([tip_arrow(),
                       L(R, [poly((28, 46), (19, 42), (16, 33), (19, 24 + b), (22, 28), (23, 17 + a), (28, 22), (31, 9 + b),
                                  (35, 20), (38, 26 + a), (40, 34), (37, 42))], shade=(1, 0)),
                       L(Y, [poly((28, 44), (23, 40), (22, 34), (25, 30 + a), (27, 34), (29, 26 + b), (32, 32), (34, 37), (32, 42))],
                         outline=False)])
    s["Flame"] = ((2, 2), flames, 5)
    s["FireBomb"] = ((24, 40), [[L(R, [oval_ring(11, 35, 37, 45, 2.5)], shade=(0, 1)),
                                 L(DG, [poly((18, y + 1), (30, y + 1), (27, y + 7), (21, y + 7))]),
                                 L(LG, [circle(24, y + 15, 7), poly((17, y + 15), (31, y + 15), (24, y + 26))], shade=(1, 0)),
                                 L(R, [rect(17, y + 12, 31, y + 15)], outline=False)] for y in (1, 4, 7, 10)], 6)
    s["Disguise"] = ((17, 17), [[L(R, [ring(17, 17, 12, 4)], shade=(-1, 1)),
                                 L(Y, [arc(17, 17, 12, 4, a, a + 80)], outline=False),
                                 L(R, [poly(*dart((17, 17), 225, 24, 9, 0.25))], shade=side(225))] for a in range(0, 360, 45)], 4)
    # the field laptop's screen scrolls lines of code, one more each frame
    lines = ((19, 34), (19, 28), (23, 37), (19, 31), (23, 30))
    s["Hack"] = ((2, 2), [[tip_arrow(), L(K, [line([(9, 9), (16, 13)], 1.5)], outline=False),
                           L(DG, [rect(14, 11, 44, 32)]), L(G, [rect(17, 14, 41, 29)], outline=False),
                           L(DGR, [rect(x0, 16 + 4 * j, x1, 18 + 4 * j) for j, (x0, x1) in enumerate(lines[i:i + 3])], outline=False),
                           L(LG, [poly((12, 33), (44, 33), (46, 42), (10, 42))], shade=(0, 1))] for i in range(3)], 8)
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
    d.text((6, 4), "each row: EA retail 32 px (1x dark, 1x light, 2x)  |  Reforged 32 px (1x dark, 1x light, 2x)",
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
        mine = rendered[ours][2][0]
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


def check_palette(name, im):
    """EA's rule: every pixel is one of the palette's colours at full opacity, or not there at all."""
    px = np.asarray(im)
    a = px[..., 3]
    assert set(np.unique(a)) <= {0, 255}, "%s %dpx has semi-transparent pixels" % (name, im.width)
    assert not px[a == 0][:, :3].any(), "%s %dpx has colour under a transparent pixel" % (name, im.width)
    off = {tuple(c) for c in np.unique(px[a == 255][:, :3], axis=0)} - PALETTE
    assert not off, "%s %dpx has off-palette colours %s" % (name, im.width, sorted(off))


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
        for im in big + small:
            check_palette(name, im)
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
    print("%d cursors, %d files in %s, every pixel on EA's palette" % (len(rendered) - 7, len(rendered), args.out))
    if args.sheet:
        os.makedirs(os.path.dirname(os.path.abspath(args.sheet)), exist_ok=True)
        contact_sheet(rendered, args.sheet)
        print("contact sheet", args.sheet)
    if args.compare:
        comparison(rendered, args.retail, args.compare)
        print("comparison", args.compare)


if __name__ == "__main__":
    main()
