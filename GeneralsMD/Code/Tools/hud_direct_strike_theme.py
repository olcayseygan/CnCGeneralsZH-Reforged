"""Direct Strike's look in one place: each side's palette, the type, the metrics every plate is made with, and the
primitives every piece of the mode is drawn from (the match HUD, the lobby and the end screen alike): the plate
painter, the panel, the key, the well, the coin, the lamps, the flat glyph masks and the 9-slice and 3-slice cuts.
hud_direct_strike.py names the pieces and builds the sheets from these; the prototypes' mocks import both, so a
change here moves every screen at once.  The contract the game's code reads is docs/reference/DIRECT-STRIKE-THEME.md.

A palette tried without editing this file: DS_PALETTE=overrides.json, {"America": {"accent": [r, g, b], "lit": [...]}},
merged over PALETTE when the module loads.  The build refuses to ship while it is set.
"""
import json
import os

import numpy as np
from PIL import Image, ImageDraw, ImageFont

import hud_sidebar as S
from hud_command_bar import Painter, Offset, shade, bar_finish, plate, shape, regular, GOLDM, BRIGHT

SIDES = ("America", "China", "Gla")
PREFIX = {"America": "AmericaPro", "China": "ChinaPro", "Gla": "GLAPro"}

# ---------------------------------------------------------------------------------------------- palette

# The plate's look per side (hud_command_bar.shade reads build .. crest_k) and the lit key face (lit).  The values
# are the command bar's (BAR_LOOKS, COMMAND_TAB_OPEN, the sidebar's rust-orange GLA accent) copied here, so Direct
# Strike moves only when this table does.
PALETTE = {
    "America": dict(build="machined", steel=(84, 110, 142), glass=(12, 32, 40), glass_lo=(8, 21, 27), accent=(0x44, 0x59, 0xb6),
                    accent2=(30, 128, 98), crest=(0.35, 0.85, 1.0), crest_k=0.55, lit=(70, 122, 232)),
    "China": dict(build="terraced", steel=(112, 116, 102), glass=(44, 15, 12), glass_lo=(26, 8, 7), accent=(156, 30, 22),
                  accent2=(150, 28, 20), crest=(1.0, 0.36, 0.22), crest_k=0.5, lit=(204, 46, 30)),
    "Gla": dict(build="scavenged", steel=(152, 130, 90), glass=(8, 12, 6), glass_lo=(30, 40, 14), accent=(176, 90, 30),
                accent2=(150, 154, 156), crest=(1.0, 0.84, 0.52), crest_k=0.4, lit=(214, 122, 40)),
}
PALETTE_ENV = "DS_PALETTE"

# flat colours: the code's text and the glyphs it tints, the two teams, the coin's metal
INK = dict(white=(236, 238, 242), grey=(176, 182, 192), dim=(120, 126, 136), paper=(232, 234, 240), gold=(242, 194, 48),
           green=(60, 186, 72), on_lit=(14, 16, 22), keyline=(12, 14, 18), shadow=(0, 0, 0, 210), us=(66, 112, 236),
           them=(214, 44, 36), muted=(150, 156, 166))
COIN = dict(rim=(20, 16, 6), face=(242, 194, 48), inner=(226, 172, 36), deep=(176, 128, 24), brass=(214, 168, 64),
            brass_inner=(190, 140, 44), random=(120, 126, 136), random_mark=(24, 26, 30))


def load_overrides(path):
    with open(path, encoding="utf-8") as f:
        for side, keys in json.load(f).items():
            PALETTE[side].update({k: tuple(v) if isinstance(v, list) else v for k, v in keys.items()})


if os.environ.get(PALETTE_ENV):
    load_overrides(os.environ[PALETTE_ENV])

# ---------------------------------------------------------------------------------------------- type (1080 pixels)

FONT_DIR = os.path.join(os.environ.get("WINDIR", "C:/Windows"), "Fonts")
FONT_BOLD, FONT_REGULAR = "arialbd.ttf", "arial.ttf"
TYPE = dict(tick=11, small=13, price=14, body=16, label=18, name=22, figure=28, title=34, result=40)
TRACKING = dict(title=0.30, result=0.32, button=0.21, team=0.2)   # letter spacing of the spaced capitals, times the size

# ---------------------------------------------------------------------------------------------- metrics

CLOSED = (False, False, False, False)           # no side of a piece is the screen's edge
PAD = 24                                        # the painter's room round a piece (shade needs it)
CORNER = {"machined": 9, "terraced": 6, "scavenged": 7}     # a panel's corner, capped at a quarter of its height
SHELL_TOP, LIP_TOP, LIP_INSET, SHELL_BEVEL = 3, 6, 3.0, 2.4
LIP_BEVEL = {"machined": 2.0, "terraced": 0.9, "scavenged": 0.9}
INSET = 5                                       # a one-well panel's glass from its edge
WELL_DEPTH, WELL_LIP, WELL_CORNER = 4, 1.3, 0.5  # a glass well; its corner a share of the panel's
BAND_DEPTH, BAND_LIP = 3, 1.0                   # a square band well (a card's price)
RULE_DEPTH = 0.4                                # the gold rule inlaid under a winner's picture
RECESS_CORNER, RECESS_DEPTH, RECESS_EDGE = 0.6, 2.4, 1.6

# slices: a 9-slice's corners NINE square and its edges and centre SLICE_EDGE long, every row (column) of an edge the
# same along its stretch; a 3-slice's ends CAP wide and its middle SLICE_EDGE wide, every column the same
NINE, WELL_NINE, SLICE_EDGE, CAP = 16, 8, 8, 24
NINE_PARTS = ("TL", "T", "TR", "L", "C", "R", "BL", "B", "BR")
DROP_H, DROP_L, DROP_C, DROP_R, FIELD_R = 48, 16, 8, 44, 16
BIG = 8                                         # flat glyphs are drawn at 8x and box filtered


def look(side):
    return PALETTE[side]


def build_of(side):
    return PALETTE[side]["build"]

# ---------------------------------------------------------------------------------------------- type


def font(size, bold=True):
    return ImageFont.truetype(os.path.join(FONT_DIR, FONT_BOLD if bold else FONT_REGULAR), TYPE.get(size, size))


def text(d, xy, s, size, fill=INK["white"], anchor="la", bold=True, shadow=True):
    """size a TYPE role or pixels; a one pixel drop shadow under it unless shadow is False."""
    f = font(size, bold)
    if shadow:
        d.text((xy[0] + 1, xy[1] + 1), s, font=f, fill=INK["shadow"], anchor=anchor)
    d.text(xy, s, font=f, fill=fill, anchor=anchor)


def spaced(d, xy, s, size, fill, spacing, anchor="mm"):
    """Capitals with spacing pixels between them, centred on xy ("mm") or starting at it ("lm")."""
    f = font(size)
    widths = [f.getlength(ch) for ch in s]
    total = sum(widths) + spacing * (len(s) - 1)
    x = xy[0] - total / 2 if anchor == "mm" else xy[0]
    for ch, w in zip(s, widths):
        text(d, (x, xy[1]), ch, size, fill, "lm")
        x += w + spacing

# ---------------------------------------------------------------------------------------------- plates


def small(side, w, h, draw, shadow=False, scan=False):
    """A w x h piece drawn by draw(offset_painter, build) in its own coordinates on a padded canvas, lit with the
    side's palette and cropped back."""
    palette = look(side)
    p = Painter(w + 2 * PAD, h + 2 * PAD)
    draw(Offset(p, PAD, PAD), palette["build"])
    img = bar_finish(shade(p, palette, shadow=shadow, falloff=False), p, palette, scan=scan, gloss=False)
    return img.crop((PAD, PAD, PAD + w, PAD + h))


def panel(side, w, h, wells=(), lit=False, rule=None, bands=(), recess=None):
    """Every plate of the mode: the side's outline, the near-black shell, the lip (bright steel when lit, the winner's
    and the hero's), glass wells sunk into it, square band wells, a recessed face, a gold rule.  Rects in the panel's
    own pixels."""
    build = build_of(side)
    c = min(CORNER[build], h / 4)

    def draw(o, b):
        o.tiers([shape(build, 0, 0, w, h, c)], CLOSED, SHELL_TOP, LIP_TOP, LIP_INSET, LIP_BEVEL[build], SHELL_BEVEL,
                lip_mat=BRIGHT if lit else None)
        if recess:
            o.recess([shape(build, *recess, c * RECESS_CORNER)], CLOSED, RECESS_DEPTH, RECESS_EDGE)
        for r in wells:
            o.sink([shape(build, *r, c * WELL_CORNER)], WELL_DEPTH, WELL_LIP)
        for r in bands:
            o.sink([plate(*r)], BAND_DEPTH, BAND_LIP)
        if rule:
            o.inlay([plate(*rule)], GOLDM, RULE_DEPTH)
    return small(side, w, h, draw)


def chip(side, w, h, inset=INSET, lit=False):
    """A panel with one glass well inset from its edge."""
    return panel(side, w, h, [(inset, inset, w - inset, h - inset)], lit)


def key(side, w, h, state, layers=(), k=1.0, corner=1.0, dx=0.0, dy=0.0):
    """The mode's key: the sidebar's bezel in this palette.  state '' up, 'H' hover, 'P' lit, 'I' disabled; layers
    the glyph ((polys, fill) in a 22 box round 11, 11) or none for the code's text."""
    palette = look(side)
    return S.key(side, w, h, state, layers, k, corner, dx, dy, look=palette, lit=palette["lit"])

# ---------------------------------------------------------------------------------------------- flat glyphs


def mask(size, draw, colour=INK["white"]):
    """A flat glyph: draw(ImageDraw, k) paints 255 on an 8x mask (k the scale), box filtered into colour."""
    m = Image.new("L", (size[0] * BIG, size[1] * BIG), 0)
    draw(ImageDraw.Draw(m), BIG)
    g = Image.new("RGBA", size, colour + (255,))
    g.putalpha(m.resize(size, Image.BOX))
    return g


def mark(w, h, polys, outline=1.0):
    """White polygons with a dark keyline round them, for the code to tint."""
    from PIL import ImageFilter

    def alpha(grow_by):
        m = Image.new("L", (w * BIG, h * BIG), 0)
        d = ImageDraw.Draw(m)
        for q in polys:
            d.polygon([(x * BIG, y * BIG) for x, y in q], fill=255)
        if grow_by:
            m = m.filter(ImageFilter.MaxFilter(int(grow_by * 2 * BIG) | 1))
        return m.resize((w, h), Image.BOX)
    out = Image.new("RGBA", (w, h), INK["keyline"] + (255,))
    out.putalpha(alpha(outline))
    fill = Image.new("RGBA", (w, h), (255, 255, 255, 255))
    fill.putalpha(alpha(0))
    out.alpha_composite(fill)
    return out


flat_glyph = S.flat_glyph                       # layers in a 22 box, scaled round a point


def ring(size, r, width, colour=(255, 255, 255)):
    """A ring centred in a square, white by default, for the code to tint and wipe."""
    c = size / 2

    def draw(d, k):
        d.ellipse(((c - r) * k, (c - r) * k, (c + r) * k, (c + r) * k), fill=255)
        d.ellipse(((c - r + width) * k, (c - r + width) * k, (c + r - width) * k, (c + r - width) * k), fill=0)
    return mask((size, size), draw, colour)

# ---------------------------------------------------------------------------------------------- coins and lamps


def draw_coin(d, k, build, cx, cy, r):
    """The side's coin on an RGBA draw at scale k: America a gold octagon with a chevron struck in it, China a round
    coin with a square hole, the GLA a crimped brass cap."""
    def s(pts):
        return [(x * k, y * k) for x, y in pts]

    def box(x0, y0, x1, y1):
        return (x0 * k, y0 * k, x1 * k, y1 * k)
    rim, face, inner, deep = (COIN[n] + (255,) for n in ("rim", "face", "inner", "deep"))
    if build == "machined":
        d.polygon(s(regular(cx, cy, r + 1, 8, np.pi / 8)), fill=rim)
        d.polygon(s(regular(cx, cy, r, 8, np.pi / 8)), fill=face)
        d.polygon(s(regular(cx, cy, r * 0.74, 8, np.pi / 8)), fill=inner)
        w = r * 0.52
        d.polygon(s([(cx - w, cy + w * 0.35), (cx, cy - w * 0.45), (cx + w, cy + w * 0.35), (cx + w, cy + w * 0.75), (cx, cy - w * 0.05),
                     (cx - w, cy + w * 0.75)]), fill=deep)
    elif build == "terraced":
        d.ellipse(box(cx - r - 1, cy - r - 1, cx + r + 1, cy + r + 1), fill=rim)
        d.ellipse(box(cx - r, cy - r, cx + r, cy + r), fill=face)
        d.ellipse(box(cx - r * 0.78, cy - r * 0.78, cx + r * 0.78, cy + r * 0.78), fill=inner)
        h = r * 0.3
        d.rectangle(box(cx - h - 1.2, cy - h - 1.2, cx + h + 1.2, cy + h + 1.2), fill=deep)
        d.rectangle(box(cx - h, cy - h, cx + h, cy + h), fill=rim)
    else:
        crimp = [(cx + (r + 1 if i % 2 == 0 else r * 0.86) * np.cos(np.pi * i / 11), cy + (r + 1 if i % 2 == 0 else r * 0.86) * np.sin(np.pi * i / 11))
                 for i in range(22)]
        d.polygon(s(crimp), fill=rim)
        d.polygon(s([(cx + (x - cx) * 0.93, cy + (y - cy) * 0.93) for x, y in crimp]), fill=COIN["brass"] + (255,))
        d.ellipse(box(cx - r * 0.64, cy - r * 0.64, cx + r * 0.64, cy + r * 0.64), fill=COIN["brass_inner"] + (255,))


COIN_K = 4                                      # coins are drawn at 4x


def coin(side, size):
    """A side's coin size across, or Random's grey one with a question mark."""
    if side == "Random":
        def disc(d, k):
            d.ellipse((0, 0, size * k - 1, size * k - 1), fill=255)
        img = mask((size, size), disc, COIN["random"])
        ImageDraw.Draw(img).text((size / 2, size / 2 + 1), "?", font=font(int(size * 0.7)), fill=COIN["random_mark"] + (255,), anchor="mm")
        return img
    big = Image.new("RGBA", (size * COIN_K, size * COIN_K), (0, 0, 0, 0))
    draw_coin(ImageDraw.Draw(big), COIN_K, build_of(side), size / 2, size / 2, size / 2 - 1)
    return big.resize((size, size), Image.BOX)


LAMPS = ("ready", "waiting", "host")


def lamp(kind, size):
    """One lamp family, drawn in a 20 box: a dark rim round a face.  ready a green face with a white check (a placed
    hero, an owned upgrade, a ready seat), waiting an unlit face in a grey ring, host a gold ring and dot."""
    big = Image.new("RGBA", (size * BIG, size * BIG), (0, 0, 0, 0))
    d = ImageDraw.Draw(big)
    k = size / 20 * BIG

    def disc(r0, r1, fill):
        d.ellipse((r0 * k, r0 * k, r1 * k, r1 * k), fill=fill)
    disc(0.5, 19.5, INK["keyline"] + (255,))
    if kind == "ready":
        disc(1.6, 18.4, INK["green"] + (255,))
        d.line([(5.4 * k, 10.2 * k), (8.6 * k, 13.6 * k), (14.8 * k, 6.4 * k)], fill=(255, 255, 255, 255), width=int(2.4 * k))
    else:
        colour = INK["dim"] if kind == "waiting" else INK["gold"]
        disc(1.9, 18.1, colour + (255,))
        disc(3.9, 16.1, INK["keyline"] + (255,))
        if kind == "host":
            disc(6.6, 13.4, colour + (255,))
    return big.resize((size, size), Image.BOX)

# ---------------------------------------------------------------------------------------------- slices


def uniform(img, axis):
    """img made the same along axis (0: every row of a column equal, 1: every column of a row equal), so a piece the
    code stretches that way shows no smear."""
    a = np.asarray(img, np.float32).mean(axis, keepdims=True)
    return Image.fromarray(np.repeat(np.round(a), img.size[1 - axis] if axis == 0 else img.size[0], axis).astype(np.uint8))


def nine_cut(full, corner=NINE, edge=SLICE_EDGE):
    """A square piece cut as a 9-slice: four corners corner square, four edges and a centre edge long, each made
    uniform along its stretch."""
    s = full.width
    m0, m1 = s // 2 - edge // 2, s // 2 + edge // 2
    far = s - corner
    return {"TL": full.crop((0, 0, corner, corner)), "TR": full.crop((far, 0, s, corner)),
            "BL": full.crop((0, far, corner, s)), "BR": full.crop((far, far, s, s)),
            "T": uniform(full.crop((m0, 0, m1, corner)), 1), "B": uniform(full.crop((m0, far, m1, s)), 1),
            "L": uniform(full.crop((0, m0, corner, m1)), 0), "R": uniform(full.crop((far, m0, s, m1)), 0),
            "C": uniform(uniform(full.crop((m0, m0, m1, m1)), 0), 1)}


def three_cut(full, cap=CAP, edge=SLICE_EDGE):
    """A piece cut as a horizontal 3-slice: ends cap wide, the middle edge wide and uniform along the row."""
    w, h = full.size
    return {"L": full.crop((0, 0, cap, h)), "C": uniform(full.crop((w // 2 - edge // 2, 0, w // 2 + edge // 2, h)), 1),
            "R": full.crop((w - cap, 0, w, h))}


def draw_nine(lay, pieces, prefix, r, corner=NINE):
    """What the code does with a 9-slice: corners as they are, edges and centre stretched to fill r."""
    x0, y0, x1, y1 = r
    n = corner
    for name, at in (("TL", (x0, y0)), ("TR", (x1 - n, y0)), ("BL", (x0, y1 - n)), ("BR", (x1 - n, y1 - n))):
        lay.alpha_composite(pieces[prefix + name], at)
    lay.alpha_composite(pieces[prefix + "T"].resize((x1 - x0 - 2 * n, n)), (x0 + n, y0))
    lay.alpha_composite(pieces[prefix + "B"].resize((x1 - x0 - 2 * n, n)), (x0 + n, y1 - n))
    lay.alpha_composite(pieces[prefix + "L"].resize((n, y1 - y0 - 2 * n)), (x0, y0 + n))
    lay.alpha_composite(pieces[prefix + "R"].resize((n, y1 - y0 - 2 * n)), (x1 - n, y0 + n))
    lay.alpha_composite(pieces[prefix + "C"].resize((x1 - x0 - 2 * n, y1 - y0 - 2 * n)), (x0 + n, y0 + n))


def draw_three(lay, pieces, prefix, r, state=""):
    """What the code does with a 3-slice: the ends as they are, the middle stretched between them, the height the
    piece's own."""
    x0, y0, x1, _ = r
    left, right = pieces[prefix + "L" + state], pieces[prefix + "R" + state]
    middle = pieces[prefix + "C" + state]
    lay.alpha_composite(left, (x0, y0))
    lay.alpha_composite(middle.resize((x1 - x0 - left.width - right.width, middle.height)), (x0 + left.width, y0))
    lay.alpha_composite(right, (x1 - right.width, y0))
