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
import hud_command_bar as H
from hud_command_bar import Painter, Offset, shade, bar_finish, plate, shape, regular, GOLDM, BRIGHT, STEEL, DARK, INKM

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

# The common frames (2026-11-23, the owner's order): a screen every player shares (the lobby, the end screen) drawn
# in one palette no side owns, so two players in one room see the same frame.  Four blends for the owner to pick
# from; the side colours stay on what belongs to a seat (its marks, its team's segments and the tug bar).  Keys past
# shade's: wear (scratches through the paint, 0..), fitting (a key's fastener, overriding the build's), start (START's
# face; accent when absent), band ((inset, width, rgb) rings painted on the console's rim), rivets (the console's
# corners riveted).
COMMONS = ("AlliedSteel", "TricolourGunmetal", "DesertBronze", "GraphiteGold")
PALETTE.update({
    # USA's machined blue-grey steel for the structure, China's lacquer red on the rim and the lit face, GLA's brass
    # on START and its scratches for wear, China's rivets on the keys
    "AlliedSteel": dict(build="machined", steel=(126, 134, 146), glass=(14, 20, 24), glass_lo=(9, 13, 16), accent=(150, 34, 26),
                        accent2=(178, 142, 72), crest=(1.0, 0.86, 0.62), crest_k=0.45, lit=(188, 54, 40),
                        wear=0.11, fitting="rivet", start=(170, 134, 62), band=[(7.2, 2.4, (164, 36, 28))]),
    # dark gunmetal, the three sides' colours side by side as one trim band round the console, START in brass
    "TricolourGunmetal": dict(build="machined", steel=(62, 66, 72), glass=(11, 13, 15), glass_lo=(7, 8, 10), accent=(68, 89, 182),
                              accent2=(176, 90, 30), crest=(0.9, 0.9, 0.95), crest_k=0.35, lit=(196, 156, 62),
                              start=(160, 126, 54), band=[(5.6, 1.9, (78, 104, 210)), (7.6, 1.9, (196, 42, 34)), (9.6, 1.9, (204, 112, 40))]),
    # GLA-led: warm sand bronze, cast in China's terraces, steel rivets on the console's corners, red lacquer on START
    "DesertBronze": dict(build="terraced", steel=(146, 122, 86), glass=(22, 16, 10), glass_lo=(13, 10, 6), accent=(150, 34, 26),
                         accent2=(150, 154, 156), crest=(1.0, 0.84, 0.55), crest_k=0.42, lit=(198, 64, 40),
                         wear=0.08, fitting="rivet", rivets=True),
    # the most restrained: plain graphite, one gold hairline on the rim, a bronze START, no wear
    "GraphiteGold": dict(build="machined", steel=(90, 90, 90), glass=(10, 10, 11), glass_lo=(6, 6, 7), accent=(196, 156, 56),
                         accent2=(196, 156, 56), crest=(1.0, 0.95, 0.85), crest_k=0.3, lit=(214, 172, 62),
                         start=(122, 98, 44), band=[(8.0, 1.4, (214, 170, 58))]),
})
COLOUR_COMMONS = COMMONS                       # the first round's colour blends, kept for the record, not built

# The owner's second answer (2026-11-23): mix the sides' FORM and MATERIAL, not their colours.  China's sharp lines
# (form: barbed points, notched corners, a stepped lip), USA's metal (the machined shade: brushed steel, clean
# highlights, rivets), the GLA's texture (scratches, rust, grit, welded patch plates).  Colour stays restrained so the
# materials carry it.  Keys past shade's: form ("sharp"), wear (scratches), edge_wear (the wear only on bevels and
# corners, chipped to bright metal), rust, grit, patches (welded sheets on the plates), tile (the plates' and the
# console's middles tile at TILE instead of stretching, so the texture survives the code's drawing).
COMMONS = ("PolishedPagoda", "ScrapBastion", "ForgedAlloy")
PALETTE.update({
    # China's silhouette cut from USA polished steel; the GLA only where hands and boots wear it, on the edges
    "PolishedPagoda": dict(build="machined", steel=(134, 138, 144), glass=(12, 14, 17), glass_lo=(8, 9, 11), accent=(150, 124, 70),
                           accent2=(150, 124, 70), crest=(1.0, 1.0, 1.0), crest_k=0.75, lit=(196, 164, 92), start=(92, 102, 118),
                           form="sharp", wear=0.16, edge_wear=True, grit=0.02, tile=True, fitting="rivet"),
    # GLA's patched, gritty, rusted plates filling every panel inside sharp, clean steel frames
    "ScrapBastion": dict(build="machined", steel=(112, 108, 100), glass=(14, 13, 11), glass_lo=(9, 8, 7), accent=(140, 96, 54),
                         accent2=(140, 96, 54), crest=(1.0, 0.92, 0.8), crest_k=0.45, lit=(190, 134, 70), start=(128, 88, 50),
                         form="sharp", wear=0.12, rust=0.6, grit=0.08, patches=True, tile=True, fitting="bolt"),
    # an even third each: the stepped silhouette, brushed steel with rivets, moderate scratches and rust
    "ForgedAlloy": dict(build="machined", steel=(120, 120, 120), glass=(12, 13, 14), glass_lo=(8, 8, 9), accent=(150, 124, 70),
                        accent2=(150, 124, 70), crest=(1.0, 0.96, 0.88), crest_k=0.55, lit=(194, 160, 86), start=(124, 104, 62),
                        form="sharp", wear=0.10, rust=0.3, grit=0.045, tile=True, fitting="rivet"),
})
MIX_COMMONS = COMMONS                          # the three form-and-material mixes, kept as code
PREFIX.update({name: name + "Pro" for name in COLOUR_COMMONS + MIX_COMMONS})
# The owner's pick (2026-11-23): Forged Alloy is the common frame, the only one built.  Its sheet is
# ReforgedHudCommonDirectStrike and its names CommonPro<piece>, so the code names no palette.
COMMONS = ("ForgedAlloy",)
SHEET_NAME = {"ForgedAlloy": "Common"}
PREFIX["ForgedAlloy"] = "CommonPro"
TILE, TILE_BLEND = 192, 24                      # a tiled middle's period and the band its seam is blended over
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
    if any(palette.get(k) for k in ("wear", "rust", "grit")):
        img = worn(img, p, palette, w * 7 + h)
    return img.crop((PAD, PAD, PAD + w, PAD + h))


def worn(img, p, palette, seed):
    """A common frame's texture, the GLA's: scratches through the paint (only on the bevels and corners, chipped to
    bright metal, with edge_wear), rust blooms and a fine grit on the metal; the glass stays clean."""
    from scipy import ndimage
    s = H.SS
    rng = np.random.default_rng(seed)

    def down(a):
        return a.reshape(p.h_, s, p.w, s).mean(axis=(1, 3))
    metal = (p.mat != H.GLASS) & p.alpha
    m = down(metal.astype(np.float32))
    a = np.asarray(img, np.float32) / 255
    rgb, al = a[..., :3], a[..., 3]
    shape_ = (p.h_, p.w)
    sc = down(H.scratches(rng, p.h.shape) * metal)
    if palette.get("edge_wear"):                # bevels: where the height field slopes, grown a texel
        gy, gx = np.gradient(ndimage.gaussian_filter(p.h, 0.5 * s) * s)
        edge = ndimage.maximum_filter(down((np.hypot(gx, gy) > 0.35).astype(np.float32)), 3)
        chips = (ndimage.gaussian_filter(rng.standard_normal(shape_), 0.8) > 0.55) * edge
        sc = np.maximum(sc * edge, chips * 0.8)
    rgb += sc[..., None] * palette.get("wear", 0) * np.array([1.0, 0.94, 0.84])
    if palette.get("rust"):
        n = ndimage.gaussian_filter(rng.standard_normal(shape_), 5)
        n /= n.std() + 1e-6
        n += 0.5 * ndimage.gaussian_filter(rng.standard_normal(shape_), 1.2) / 0.3
        r = np.clip((n - 0.9) * 0.45, 0, 0.75) * m * palette["rust"]
        lum = rgb.mean(2, keepdims=True)
        rgb = rgb * (1 - r[..., None]) + np.array(H.RUST, np.float32) / 255 * (0.45 + 1.6 * lum) * r[..., None]
    if palette.get("grit"):
        g = ndimage.gaussian_filter(rng.standard_normal(shape_), 0.6)
        rgb *= 1 + palette["grit"] * 2.2 * g[..., None] * m[..., None]
    a[..., :3] = np.clip(rgb, 0, 1) * (al[..., None] > 0)
    return Image.fromarray((a * 255 + 0.5).astype(np.uint8), "RGBA")


def sharp(side):
    """Whether the side's frame takes China's sharp form (the common frames)."""
    return look(side).get("form") == "sharp"


def stepped_lip(o, polys, inset):
    """China's stepped bevel on top of a lip: a second step raised a little inside it, square-measured."""
    o.raise_plate(polys, LIP_TOP + 1.6, 1.0, None, "chessboard", H.terraces((0, 0.55), (2.2, 0.45)))


def patches(o, w, h, x0, x1, seed):
    """The GLA's welded sheets laid on a plate between x0 and x1, a little out of square, beads along their seams and
    a bolt in a corner."""
    rng = np.random.default_rng(seed)
    x = x0
    while x < x1 - 30:
        pw = rng.uniform(34, 70)
        top = rng.uniform(3, max(h * 0.25, 4))
        bot = h - rng.uniform(3, max(h * 0.25, 4))
        deg = rng.uniform(-2.5, 2.5)
        q = H.sheet(x, top, min(x + pw, x1), bot, deg)
        o.bump([q], 0.9, 0.7)
        o.weld(q[0], q[1], int(seed + x), 0.9)
        o.weld(q[2], q[3], int(seed + x) + 1, 0.9)
        bx, by = q[0][0] + 4, q[0][1] + 4
        o.bolt(bx, by, 1.5, int(x))
        x += pw + rng.uniform(16, 60)


def panel(side, w, h, wells=(), lit=False, rule=None, bands=(), recess=None):
    """Every plate of the mode: the side's outline, the near-black shell, the lip (bright steel when lit, the winner's
    and the hero's), glass wells sunk into it, square band wells, a recessed face, a gold rule.  Rects in the panel's
    own pixels."""
    build = build_of(side)
    c = min(CORNER[build], h / 4)
    sharp_form = sharp(side)
    outline = "terraced" if sharp_form else build     # a common frame takes China's notched corners

    def draw(o, b):
        o.tiers([shape(outline, 0, 0, w, h, c)], CLOSED, SHELL_TOP, LIP_TOP, LIP_INSET, LIP_BEVEL[build], SHELL_BEVEL,
                lip_mat=BRIGHT if lit else None)
        if sharp_form:
            k = LIP_INSET + 1.5
            stepped_lip(o, [shape(outline, k, k, w - k, h - k, max(c - k * 0.5, 1.5))], k)
            if look(side).get("patches") and min(w, h) >= 60:
                patches(o, w, h, 8, w - 8, w * 3 + h)
        if recess:
            o.recess([shape(outline, *recess, c * RECESS_CORNER)], CLOSED, RECESS_DEPTH, RECESS_EDGE)
        for r in wells:
            o.sink([shape(outline, *r, c * WELL_CORNER)], WELL_DEPTH, WELL_LIP)
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

# ---------------------------------------------------------------------------------------------- the Spearhead Front
# The lobby's look, the owner's pick of 2026-11-23 (direction C): every plate an arrow toward the lane, the two teams'
# points facing across the centre column, power-bar segments on the seat lips lit in the team's colour, a readiness
# tug bar along the foot.  A bar's end comes to a point d deep, or is square with 45 degree corners SPEAR_CUT.

SPEAR_CUT = 6                                   # a square end's corner
SPEAR_CAP = 26                                  # a 3-slice end past its point's depth (the lip and the well's own point)
CONSOLE_NINE = 24                               # the lobby console's 9-slice corner: the rim and the floor's wall
CONSOLE_SHELL, CONSOLE_LIP, CONSOLE_INSET, CONSOLE_FLOOR = 4, 7, 5.0, 12
GLOW = 4                                        # a lit piece's halo past its core, each side


def spear(x0, y0, x1, y1, dl=0.0, dr=0.0, c=SPEAR_CUT, i=0.0, form=None):
    """The Spearhead outline: a bar whose left and right ends come to points dl and dr deep (0: square, its corners
    cut c at 45 degrees), inset by i with the same slopes, so a well sunk at i follows the plate's edge."""
    h = y1 - y0

    def end(d):                                 # how far the inset moves the end's tip, and the inset point's depth
        if d:
            return i * np.hypot(d, h / 2) / (h / 2), d * (h - 2 * i) / h
        return i, 0.0
    sl, dl2 = end(dl)
    sr, dr2 = end(dr)
    x0, x1, y0, y1 = x0 + sl, x1 - sr, y0 + i, y1 - i
    ym = (y0 + y1) / 2
    k = max(c - i * 0.41, 0.0)
    if form == "sharp":
        return sharp_spear(x0, y0, x1, y1, dl2 if dl else 0, dr2 if dr else 0, max(c - i * 0.5, 1.5), barbs=i == 0)
    pts = [(x0 + (dl2 if dl else k), y0), (x1 - (dr2 if dr else k), y0)]
    pts += [(x1, ym)] if dr else [(x1, y0 + k), (x1, y1 - k)]
    pts += [(x1 - (dr2 if dr else k), y1), (x0 + (dl2 if dl else k), y1)]
    pts += [(x0, ym)] if dl else [(x0, y1 - k), (x0, y0 + k)]
    return pts


def sharp_spear(x0, y0, x1, y1, dl, dr, c, barbs=True):
    """China's sharp form of a Spearhead bar: square ends notched like the terraced corners, and each point barbed, a
    step on both of its slopes as a pagoda's eave steps, so the tip reads as cut, not drawn."""
    ym = (y0 + y1) / 2
    h = y1 - y0

    def tip(xb, d, sgn):                        # from the top of the end at xb to the bottom, through a point d out
        if not barbs or h < 30:
            return [(xb + sgn * d, ym)]
        f, back = 0.42, min(d * 0.28, 5.0)
        return [(xb + sgn * d * f, y0 + h * f / 2), (xb + sgn * (d * f - back), y0 + h * f / 2 + 0.01), (xb + sgn * d, ym),
                (xb + sgn * (d * f - back), y1 - h * f / 2 - 0.01), (xb + sgn * d * f, y1 - h * f / 2)]
    pts = [(x0 + dl, y0)] if dl else [(x0 + c, y0)]
    pts += [(x1 - dr, y0)] if dr else [(x1 - c, y0), (x1 - c, y0 + c), (x1, y0 + c)]
    if dr:
        pts += tip(x1 - dr, dr, 1)
    else:
        pts += [(x1, y1 - c), (x1 - c, y1 - c), (x1 - c, y1)]
    pts += [(x1 - dr, y1)] if dr else []
    pts += [(x0 + dl, y1)] if dl else [(x0 + c, y1), (x0 + c, y1 - c), (x0, y1 - c)]
    if dl:
        pts += list(reversed(tip(x0 + dl, dl, -1)))
    else:
        pts += [(x0, y0 + c), (x0 + c, y0 + c)]
    return pts


def spear_plate(side, w, h, dl=0.0, dr=0.0, lit=False, well=None, bay=False):
    """A Spearhead plate: the shell and the lip round the outline (bright when lit, the local seat and the title), a
    glass well sunk `well` in when given; a bay is the empty seat's slot, a low dark rim round a recessed floor."""
    build = build_of(side)
    form = look(side).get("form")

    def draw(o, b):
        outline = [spear(0, 0, w, h, dl, dr, form=form)]
        if bay:
            o.tiers(outline, CLOSED, 1.6, 2.4, 2.0, LIP_BEVEL[build], 1.4)
            o.recess([spear(0, 0, w, h, dl, dr, SPEAR_CUT, 4, form)], CLOSED, RECESS_DEPTH + 1.0, RECESS_EDGE)
            return
        o.tiers(outline, CLOSED, SHELL_TOP, LIP_TOP, LIP_INSET, LIP_BEVEL[build], SHELL_BEVEL, lip_mat=BRIGHT if lit else None)
        if form == "sharp":
            stepped_lip(o, [spear(0, 0, w, h, dl, dr, SPEAR_CUT, LIP_INSET + 1.5, form)], LIP_INSET + 1.5)
            if look(side).get("patches") and h >= 40:
                patches(o, w, h, spear_cap(dl) + 4, w - spear_cap(dr) - 4, w * 3 + h)
        if well is not None:
            o.sink([spear(0, 0, w, h, dl, dr, SPEAR_CUT, well, form)], WELL_DEPTH, WELL_LIP)
    return small(side, w, h, draw)


def console(side, s=None):
    """The lobby console, square, for the 9-slice: a heavy rim in the side's outline round a recessed floor.  A tiled
    frame's master is big enough for its middles to hold a whole TILE."""
    palette = look(side)
    s = s or (2 * CONSOLE_NINE + TILE + TILE_BLEND + 16 if palette.get("tile") else 192)
    build = build_of(side)
    outline = "terraced" if palette.get("form") == "sharp" else build   # China's notched corners on a sharp frame
    c = CORNER[build]

    def draw(o, b):
        o.tiers([shape(outline, 0, 0, s, s, c)], CLOSED, CONSOLE_SHELL, CONSOLE_LIP, CONSOLE_INSET, LIP_BEVEL[build], SHELL_BEVEL + 0.6)
        f = CONSOLE_FLOOR
        if outline != build:
            o.raise_plate([shape(outline, 6.5, 6.5, s - 6.5, s - 6.5, c * 0.7)], CONSOLE_LIP + 1.6, 1.0, None, "chessboard",
                          H.terraces((0, 0.55), (2.2, 0.45)))
        o.recess([shape(outline, f, f, s - f, s - f, c * RECESS_CORNER)], CLOSED, RECESS_DEPTH, RECESS_EDGE)
        if palette.get("patches"):              # deck plates welded on the floor, inside the tiled middle
            rng = np.random.default_rng(s)
            for y in (f + 8, s / 2 + 6):
                x = f + 6
                while x < s - f - 40:
                    pw = rng.uniform(50, 110)
                    q = H.sheet(x, y + rng.uniform(0, 8), min(x + pw, s - f - 6), y + rng.uniform(56, 86), rng.uniform(-2, 2))
                    o.bump([q], 0.8, 0.7)
                    o.weld(q[0], q[1], int(x + y), 0.9)
                    o.weld(q[1], q[2], int(x + y) + 3, 0.9)
                    o.bolt(q[3][0] + 4, q[3][1] - 4, 1.5, int(x))
                    x += pw + rng.uniform(10, 40)
        if palette.get("fitting") == "rivet" and outline != build:      # rivets in the corners, on the stepped lip
            for x, y in ((9.0, 18.0), (18.0, 9.0)):
                for cx, cy in ((x, y), (s - x, y), (x, s - y), (s - x, s - y)):
                    o.rivet(cx, cy, 1.7)
        if palette.get("rivets"):               # in the corners only: a 9-slice's edges stretch
            for x, y in ((9.5, 9.5), (9.5, 19), (19, 9.5)):
                for cx, cy in ((x, y), (s - x, y), (x, s - y), (s - x, s - y)):
                    o.rivet(cx, cy, 1.9)
    img = small(side, s, s, draw)
    for inset, width, rgb in palette.get("band", ()):     # painted trim rings along the rim, the same all round
        k = 4
        m = Image.new("L", (s * k, s * k), 0)
        d = ImageDraw.Draw(m)
        d.polygon([(x * k, y * k) for x, y in shape(build, inset, inset, s - inset, s - inset, max(c - inset * 0.41, 1))], fill=255)
        d.polygon([(x * k, y * k) for x, y in shape(build, inset + width, inset + width, s - inset - width, s - inset - width,
                                                       max(c - (inset + width) * 0.41, 1))], fill=0)
        a = np.asarray(m.resize((s, s), Image.BOX), np.float32)[..., None] / 255 * 0.9
        base = np.asarray(img, np.float32)
        lum = base[..., :3].mean(2, keepdims=True) / 140.0           # the paint keeps the metal's light
        base[..., :3] = base[..., :3] * (1 - a) + np.clip(np.asarray(rgb, np.float32) * (0.55 + 0.6 * lum), 0, 255) * a
        img = Image.fromarray(base.astype(np.uint8), "RGBA")
    return img


def spear_key(side, w, h, dl, dr, state, layers=(), k=1.0, face=None):
    """The mode's key (the sidebar bezel, as key() draws it) round a Spearhead outline.  state '' up, 'H' hover, 'P'
    lit (the face the side's lit colour), 'I' disabled; face an RGB to paint the up and hover face with (START's
    accent), none for the dark glass the code writes on."""
    palette = look(side)
    build = palette["build"]
    edge = H.MOUNT_GLYPH_BUILD.get(build, build)
    o = 32
    p = Painter(w + 2 * o, h + 2 * o)
    down = state == "P"
    r = H.COMMAND_TAB_RIM

    form = palette.get("form")

    def at(i):
        return [(x + o, y + o) for x, y in spear(0, 0, w, h, dl, dr, SPEAR_CUT, i, form)]
    p.frame([at(0)], 2.2, edge, DARK, H.BEZEL_SCALE)
    p.frame([at(r)], 2.4 if down else 3.6, edge, {"": STEEL, "H": BRIGHT, "P": BRIGHT, "I": DARK}[state], H.BEZEL_SCALE)
    i = r + 3.2
    p.sink([at(i)], 2.8 if down else 1.6, 1.0)
    fit = palette.get("fitting") or {"terraced": "rivet", "scavenged": "bolt"}.get(build, "hairline")
    if fit == "rivet":                          # each build's fitting on the bezel, as key() has them
        for x in (o + max(dl, 3.0) * 0.5 + 3.0, o + w - max(dr, 3.0) * 0.5 - 3.0):
            p.rivet(x, o + h / 2, 1.2)
    elif fit == "bolt":
        p.bolt(o + w - dr - 4.0, o + 3.4, 1.3, w)
    elif w >= 40:
        p.inlay([plate(o + w / 2 - min(16, w / 5), o + 2.0, o + w / 2 + min(16, w / 5), o + 2.8)], H.ACCENT, 0.3)
    img = shade(p, palette, shadow=False, falloff=False)
    if any(palette.get(k) for k in ("wear", "rust", "grit")):
        img = worn(img, p, palette, w * 5 + h)
    img = img.crop((o, o, o + w, o + h))
    colour = palette["lit"] if down else (face if state in ("", "H") else None)
    if colour is not None:
        m = Image.new("L", (w * BIG, h * BIG), 0)
        ImageDraw.Draw(m).polygon([(x * BIG, y * BIG) for x, y in spear(0, 0, w, h, dl, dr, SPEAR_CUT, i, form)], fill=255)
        yy = np.arange(h, dtype=np.float32)[:, None, None]
        ramp = 0.62 + 0.5 * (yy - i) / (h - 2 * i)
        rgb = np.broadcast_to(np.asarray(colour, np.float32) * ramp * np.where(yy % 2 == 0, 0.9, 1.0), (h, w, 3)).copy()
        rgb[int(h - i - 3):int(h - i)] = np.minimum(np.asarray(colour, np.float32) * 1.25 + 60, 255)
        layer = Image.fromarray(np.clip(rgb, 0, 255).astype(np.uint8), "RGB").convert("RGBA")
        layer.putalpha(m.resize((w, h), Image.BOX))
        img.alpha_composite(layer)
    if state == "I":                            # a disabled key's steel and face sit back
        a = np.asarray(img, np.float32)
        a[..., :3] *= 0.62
        img = Image.fromarray(a.astype(np.uint8), "RGBA")
    if layers:
        off = 0.8 if down else 0.0
        colour = {"": INK["paper"], "H": INK["gold"], "P": INK["on_lit"], "I": INK["dim"]}[state]
        img.alpha_composite(flat_glyph(w, h, layers, (h - 2 * i) / 14.5 * k, w / 2 + (dl - dr) / 4 + off,
                                       h / 2 + off - (1.2 if down else 0), colour))
    return img


def three_cut_ends(full, left, right, edge=SLICE_EDGE):
    """three_cut with ends of their own widths: a pointed end is wider than a square one."""
    w, h = full.size
    return {"L": full.crop((0, 0, left, h)), "C": uniform(full.crop((w // 2 - edge // 2, 0, w // 2 + edge // 2, h)), 1),
            "R": full.crop((w - right, 0, w, h))}


def periodic(img, axis, period=TILE, blend=TILE_BLEND):
    """A crop period + blend long along axis (0 down, 1 across) made to repeat at period: its first blend texels are
    faded into the texels one period on, so the tile's end runs on into its start."""
    a = np.asarray(img, np.float32)
    if axis == 0:
        a = a.transpose(1, 0, 2)
    out = a[:, :period].copy()
    t = (np.arange(blend, dtype=np.float32) / blend)[None, :, None]
    out[:, :blend] = a[:, :blend] * t + a[:, period:period + blend] * (1 - t)
    if axis == 0:
        out = out.transpose(1, 0, 2)
    return Image.fromarray(np.clip(out + 0.5, 0, 255).astype(np.uint8), "RGBA")


def three_tile(full, left, right, period=TILE, blend=TILE_BLEND):
    """A 3-slice whose middle tiles at period (a textured plate): the code repeats C between the ends, it never
    stretches it."""
    w, h = full.size
    x0 = (w - period - blend) // 2
    return {"L": full.crop((0, 0, left, h)), "C": periodic(full.crop((x0, 0, x0 + period + blend, h)), 1, period, blend),
            "R": full.crop((w - right, 0, w, h))}


def nine_tile(full, corner, period=TILE, blend=TILE_BLEND):
    """A 9-slice whose edges tile along their length and whose centre tiles both ways."""
    s = full.width
    a, far = (s - period - blend) // 2, s - corner
    along, both = period + blend, (a, a, a + period + blend, a + period + blend)
    return {"TL": full.crop((0, 0, corner, corner)), "TR": full.crop((far, 0, s, corner)),
            "BL": full.crop((0, far, corner, s)), "BR": full.crop((far, far, s, s)),
            "T": periodic(full.crop((a, 0, a + along, corner)), 1, period, blend),
            "B": periodic(full.crop((a, far, a + along, s)), 1, period, blend),
            "L": periodic(full.crop((0, a, corner, a + along)), 0, period, blend),
            "R": periodic(full.crop((far, a, s, a + along)), 0, period, blend),
            "C": periodic(periodic(full.crop(both), 1, period, blend).crop((0, 0, period, along)), 0, period, blend)}


def spear_cap(d):
    """A 3-slice end's width for an end d deep (0 square)."""
    return int(d) + SPEAR_CAP if d else CAP


def lit_piece(core_w, core_h, polys=None):
    """A white lamp for the code to tint: the core (a rect, or polys in the core's own pixels) and a soft halo GLOW
    round it, both in the alpha."""
    from PIL import ImageFilter
    W, Hh = core_w + 2 * GLOW, core_h + 2 * GLOW
    m = Image.new("L", (W * BIG, Hh * BIG), 0)
    d = ImageDraw.Draw(m)
    for q in polys or [[(0, 0), (core_w, 0), (core_w, core_h), (0, core_h)]]:
        d.polygon([((x + GLOW) * BIG, (y + GLOW) * BIG) for x, y in q], fill=255)
    core = m.resize((W, Hh), Image.BOX)
    halo = np.asarray(core.filter(ImageFilter.GaussianBlur(2.2)), np.float32) * 0.6
    a = np.maximum(np.asarray(core, np.float32), halo)
    out = Image.new("RGBA", (W, Hh), (255, 255, 255, 255))
    out.putalpha(Image.fromarray(np.clip(a, 0, 255).astype(np.uint8)))
    return out


def slot_piece(w, h, face=(30, 34, 42)):
    """An unlit segment or lamp: a keyline rim, a dark face with its top row in shadow and its foot catching light."""
    img = Image.new("RGBA", (w, h), INK["keyline"] + (255,))
    d = ImageDraw.Draw(img)
    d.rectangle((1, 1, w - 2, h - 2), fill=face + (255,))
    d.line([(1, 1), (w - 2, 1)], fill=tuple(int(c * 0.6) for c in face) + (255,))
    d.line([(1, h - 2), (w - 2, h - 2)], fill=tuple(min(255, int(c * 1.5) + 8) for c in face) + (255,))
    return img
