"""The Red Alert style layout's art (Options, Layout = Red Alert Style), drawn from nothing in the command bar
generator's own tools (hud_command_bar: Painter, shade, bar_finish, the key bezels), in round 18's three tiers and
each side's build.  The owner picked it from the preview of 2026-11-15
(experiments/proofs/2026-11-15/hud-sidebar-art in the wrapper, whose preview.py imports this).  Every size is the
1920x1080 rect of HudLayoutRects (GameClient/Display.h); the code scales by k = H/1080.

Each side has two textures and one mapped INI, MappedImages/HandCreated/ReforgedHud<Side>Sidebar.ini:
    ReforgedHud<Side>Sidebar       512x2048: Plate 320x1080 at the sidebar's rect, Card 464x120, CameoWell 140x86,
                                   Tab<Kind>{,H,P,I} and TabIcon<Kind> 66x36 (Buildings, Defenses, Infantry,
                                   Vehicles), ArrowUp/ArrowDown{,H,P,I} 140x28, Chip{,P} 40x28, ChipWide{,P} 66x28,
                                   SuperweaponOwn/Enemy 288x28, KeyMenu/KeyDiplomacy{,H,P,I} 72x32,
                                   KeySell/KeyRepair{,H,P,I} 74x36, KeyGeneral{,H,P,I} 140x36
    ReforgedHud<Side>SidebarStrip  2048x128: Strip 1600x64
named <Prefix>Sidebar<Piece> (AmericaPro, ChinaPro, GLAPro, TurkiyePro).  The pieces' places are SHEET_AT, the same on
every side; the INI is written by hand from them and check_mapped() holds it to them.

What the code has to know about the art:
    the cameo is drawn CAMEO_INSET (3) inside its well, so the well's bezel shows round it
    the money's "$" is painted on the plate at screen x MONEY_SIGN_X (1700), the readout starts at MONEY_TEXT_X (1712)
    a superweapon row's countdown fills SW_GROOVE (34..270 x 19..24 in the row), name and clock in SW_TEXT

    python Tools/hud_sidebar.py --out DIR [--side all|America|China|Gla|Turkiye] [--dds Data/Art/Textures]
"""
import argparse
import os
import re

import numpy as np
from PIL import Image, ImageDraw, ImageFont
from scipy import ndimage

import hud_command_bar as H
from hud_command_bar import (Painter, Offset, shade, bar_finish, plate, shape, grow, regular, rotated, sheet, curve,
                             star8, led, icon, page_arrow_glyph, STEEL, ACCENT, GOLDM, INKM, BRIGHT, DARK, HILITE,
                             BAR_LOOKS, COMMAND_TAB_OPEN, MOUNT_GLYPH, MOUNT_GLYPH_BUILD, BEZEL_SCALE, PAPER, GOLD,
                             TURQ, AMBER, SS)

SIDES = ("America", "China", "Gla", "Turkiye")
PREFIX = {"America": "AmericaPro", "China": "ChinaPro", "Gla": "GLAPro", "Turkiye": "TurkiyePro"}
FONT_DIR = os.path.join(os.environ.get("WINDIR", "C:/Windows"), "Fonts")


def look_of(side):
    look = dict(BAR_LOOKS[side])
    if side == "Gla":                       # the bar's GLA has no paint; the sidebar's hazard slashes are sprayed rust-orange
        look["accent"] = (176, 90, 30)
    return look


# ---------------------------------------------------------------------------------------------- the rects (1920x1080)

SB = 1600
MENU, MONEY, DIPLO = (1608, 8, 1680, 40), (1688, 8, 1832, 40), (1840, 8, 1912, 40)
RADAR = (1608, 56, 1912, 360)
SELL, REPAIR, GENERAL = (1608, 368, 1682, 404), (1690, 368, 1764, 404), (1772, 368, 1912, 404)
TABS = [(1624 + 74 * i, 420, 1690 + 74 * i, 456) for i in range(4)]
RAIL = (1604, 420, 1620, 1072)
PICK_ALL = (1624, 464, 1690, 492)
CHIPS = [(int(1698 + 43.5 * i), 464, int(1698 + 43.5 * i) + 40, 492) for i in range(5)]
CAMEOS = [(1624 + 148 * c, int(496 + 90.8 * r), 1764 + 148 * c, int(496 + 90.8 * r) + 86) for r in range(6) for c in range(2)]
ARROWS = [(1624, 1044, 1764, 1072), (1772, 1044, 1912, 1072)]
CELLS = [(8 + 64 * i, 1020, 64 + 64 * i, 1076) for i in range(14)]
IDLE = [(1100 + 64 * i, 1020, 1156 + 64 * i, 1076) for i in range(6)]
CLOCK = (1500, 1024, 1596, 1072)
CARD = (8, 888, 472, 1008)
PORTRAIT = (16, 896, 176, 1000)
GRID = [(184 + 40 * i, 896 + 40 * j, 220 + 40 * i, 924 + 40 * j) for j in range(2) for i in range(7)]
INFO = (184, 972, 460, 1000)                    # name and health under the grid (not in the plan's table yet)
SW_ROWS = [(1304, 980 - 32 * n, 1592, 1008 - 32 * n) for n in range(3)]
SW_ICON, SW_TEXT, SW_GROOVE, SW_LAMP = (4, 3, 30, 25), (34, 3, 270, 17), (34, 19, 270, 24), (273, 3, 284, 25)   # in the row
STRIP_ORN = (906, 1016, 1092, 1080)             # the strip between the last command and the idle keys
CAMEO_INSET = 3                                 # the cameo drawn this far inside its well, so the well's bezel shows
MONEY_SIGN_X, MONEY_TEXT_X = 1700, 1712         # the painted "$"'s middle, the readout's left (left-aligned)
# The general's powers wing (the owner's order of 2026-11-15): welded to the plate's left edge at the radar's top,
# cells of POWERS_CELL square growing left and down from (1600, 56), the Joint over the plate's margin at x 1600..1608,
# a Top cap over the first row and a Bottom foot under the last (8 high), the Edge left of the leftmost column (8 wide)
POWERS_AT, POWERS_CELL, POWERS_RIM = (1600, 56), 72, 8
POWERS_WELL = (4, 4, 68, 68)                    # the button's well in a cell

OPEN_COL = (True, True, False, True)            # the sidebar: top, bottom and right are the screen's edges
OPEN_STRIP = (False, True, True, True)          # the strip: bottom, left and right (under the sidebar)
CLOSED = (False, False, False, False)


def seat(o, r, build, c, grow_by=2, depth=4, lip=1.6):
    o.sink([shape(build, *grow(r, grow_by), c)], depth, lip)


def glass(o, r, build, c, grow_by=1, depth=6, lip=2.0):
    o.sink([shape(build, *grow(r, grow_by), c) if c else plate(*grow(r, grow_by))], depth, lip)


def rag(poly, seed, amp=2.2, step=12, border=lambda a, b: False):
    """jitter() with the border given: an edge on the screen's border stays straight, the rest is bitten in."""
    rng = np.random.default_rng(seed)
    area = sum(x0 * y1 - x1 * y0 for (x0, y0), (x1, y1) in zip(poly, poly[1:] + poly[:1]))
    out = []
    for a, b in zip(poly, poly[1:] + poly[:1]):
        out.append(a)
        length = np.hypot(b[0] - a[0], b[1] - a[1])
        if border(a, b) or length < step:
            continue
        dx, dy = (b[0] - a[0]) / length, (b[1] - a[1]) / length
        nx, ny = (-dy, dx) if area > 0 else (dy, -dx)
        n = int(length // step)
        for k in range(1, n):
            t, bite = k / n, rng.uniform(0, amp)
            out.append((a[0] + (b[0] - a[0]) * t + nx * bite, a[1] + (b[1] - a[1]) * t + ny * bite))
    return out


def welded(o, poly, seed, r=1.1):
    for i, (a, b) in enumerate(zip(poly, poly[1:] + poly[:1])):
        o.weld(a, b, seed=seed + i, r=r)


def fastener(o, x, y, r=2.6):
    o.inlay([regular(x, y, r, 12)], DARK, 1.0)
    o.bump([regular(x, y, r * 0.55, 6)], 0.6, 0.6, STEEL)


def camo(p, seed=5):
    """Turkiye's faint pixel camouflage on the steel, 4 texel cells."""
    h, w = p.h.shape
    n = ndimage.gaussian_filter(np.random.default_rng(seed).standard_normal((h // (4 * SS) + 1, w // (4 * SS) + 1)), 1.4)
    n /= n.std()
    tone = np.kron(np.where(n < -0.5, 0.92, np.where(n > 0.6, 1.07, 1.0)).astype(np.float32), np.ones((4 * SS, 4 * SS), np.float32))[:h, :w]
    sel = (p.mat == STEEL) & p.alpha
    p.tint[sel] *= tone[sel]


def swept(x, y0, y1, w, t, s):
    """The bar's swept chevron, pointing along x (s 1 right, -1 left)."""
    ym = (y0 + y1) / 2
    return [(x, y0), (x + s * t, y0), (x + s * (t + w), ym), (x + s * t, y1), (x, y1), (x + s * w, ym)]


def hazard(o, x0, x1, y, h=4, pitch=8):
    o.inlay([[(x, y + h), (x + h, y), (x + h + 3, y), (x + 3, y + h)] for x in np.arange(x0, x1 - h - 3, pitch)], ACCENT, 0.3)


# ---------------------------------------------------------------------------------------------- the sidebar plate

PL = 8                                          # the plate is drawn 8 texels in from its canvas's left, so its world edge is lit


#   each side's tiers: shell top, lip top, the lip's inset from the outline and its bevel.  America a thin satin lip
#   on a wide shell, China a tall lip stepped up sharply (a terrace), the GLA a sheet edge barely bent, Turkiye one
#   wide flat chamfer
TIERS = {"America": (4, 7, 5, 2.6), "China": (4, 9, 6, 0.9), "Gla": (4, 7, 3, 0.9), "Turkiye": (4, 7, 6, 5.0)}
FLOOR = (1622, 418, 1916, 1076)                 # the production well: tabs, picker, cameos, arrows
GAP_X = 1768                                    # the gap between the two cameo columns
JUNCTIONS = [int(496 + 90.8 * r) - 2.4 for r in range(1, 6)]     # the gap's crossings between cameo rows


def sidebar_common(o, build, c, floor=True):
    for r in (MENU, DIPLO, SELL, REPAIR, GENERAL):
        seat(o, r, build, c * 0.6)
    glass(o, MONEY, build, c * 0.6)
    if floor:
        o.recess([shape(build, *FLOOR, c * 1.5)], CLOSED, 3.0, 2.0)
    o.sink([plate(*grow(RAIL, 1))], 6, 1.6)                                          # the power rail's channel


def radar_glass(o):
    o.sink([plate(*grow(RADAR, 2))], 5, 2)                                           # square: the radar fits into it


def sb_america(o, p):
    """Aerospace: 45 degree V cuts in the world edge, an arrow-point ledge between the radar and production, blue
    swept chevrons and a hairline, octagon studs, an octagon-cut radar bezel."""
    col = [(1600, 0), (1920, 0), (1920, 1080), (1600, 1080), (1600, 418), (1607, 411), (1600, 404),
           (1600, 54), (1606, 48), (1600, 42)]
    o.tiers([col], OPEN_COL, *TIERS["America"])
    o.tiers([plate(1600, 0, 1920, 49, bl=("cut", 8))], (True, False, False, True), 6, 9, 3.4, 2.0)   # the header, a satin plate
    o.bump([shape("machined", *grow(RADAR, 5), 9)], 2.4, 1.4, STEEL)                  # the bezel, cut at 45
    radar_glass(o)
    sidebar_common(o, "machined", 6)
    o.inlay([plate(1612, 44.6, 1908, 45.6)], ACCENT, 0.5)
    o.tiers([[(1600, 411.5), (1606, 405.5), (1920, 405.5), (1920, 418), (1606, 418)]], (False, False, False, True), 9, 12, 3.4, 1.8)
    o.inlay([plate(1640, 410.5, 1904, 411.5)], ACCENT, 0.5)                          # the ledge's blue hairline
    o.inlay([swept(1618 + 9 * i, 407.5, 414.5, 5, 2.4, -1) for i in range(3)], ACCENT, 0.6)
    for x in (1612, 1910):
        o.stud(x, 411, 2.0)
    o.bump([plate(GAP_X - 3, 500, GAP_X + 3, 1032)], 2.0, 1.0, STEEL)                # a satin spine down the cameo columns' gap
    for y in JUNCTIONS:                                                              # chevron pairs climbing toward the money
        o.inlay(H.chevrons(GAP_X, y - 7, 5.6, n=2), ACCENT, 0.6)
    for y in (90, 208, 326):
        o.stud(1916, y, 1.7)
    o.inlay([plate(1600, 362.5, 1606, 363.5), plate(1914, 362.5, 1920, 363.5)], INKM, 0.8)    # panel seams
    o.stud(GAP_X, 1058, 2.4)


def sb_china(o, p):
    """A pagoda: the world edge stepped in square terraces, an eaved ledge flicking up at its tip, two red lacquer
    bands, gold studs, rivet rows down the cameo columns' gap, a terraced radar rim."""
    col = [(1600, 0), (1920, 0), (1920, 1080), (1600, 1080), (1600, 420), (1603, 420), (1603, 400),
           (1603, 60), (1603, 50), (1600, 50)]
    o.tiers([col], OPEN_COL, *TIERS["China"])
    roof = [(1600, 0), (1920, 0), (1920, 50), (1618, 50)] + curve((1618, 50), (1604, 50), (1600, 38), 6)[1:]
    o.tiers([roof], (True, False, False, True), 7, 11, 3.4, 0.9)                     # the header, a roof eaved at its end
    o.bump([shape("terraced", *grow(RADAR, 5), 5)], 1.4, 0.8, STEEL)                  # the rim in two terraces
    o.bump([shape("terraced", *grow(RADAR, 3.4), 3.5)], 1.4, 0.8, STEEL)
    radar_glass(o)
    sidebar_common(o, "terraced", 5, floor=False)
    o.recess([shape("terraced", *FLOOR, 8)], CLOSED, 1.6, 0.8)                        # the production well in two terraces
    o.recess([shape("terraced", *grow(FLOOR, -3), 6)], CLOSED, 1.6, 0.8)
    eave = [(1920, 405), (1618, 405)] + curve((1618, 405), (1606, 405), (1600, 396), 6)[1:]
    eave += curve((1601, 400), (1606, 417), (1620, 418), 6)[1:] + [(1920, 418)]
    o.tiers([eave], (False, False, False, True), 9, 12, 3.4, 0.9)
    o.inlay([plate(1622, 408, 1914, 410.5), plate(1612, 44.5, 1914, 46.5)], ACCENT, 0.3)   # red lacquer bands
    o.inlay([plate(GAP_X - 1.6, 500, GAP_X + 1.6, 1032)], ACCENT, 0.3)                    # and one down the cameo columns' gap
    for x in range(1630, 1912, 16):
        o.rivet(x, 414.5, 1.3)
    o.bump([regular(x, 411, 2.2, 12) for x in (1700, 1842)], 2.0, 1.3, GOLDM)
    o.bump([regular(GAP_X, y, 2.6, 12) for y in JUNCTIONS + [1058]], 2.2, 1.4, GOLDM)    # gold studs where the rows cross it
    for y in range(66, 356, 18):
        o.rivet(1916.5, y, 1.2)


def sb_gla(o, p):
    """Welded scrap: a ragged world edge with a bite out of it, two crooked patch sheets welded over the gap between
    the radar and production, hazard slashes along the left patch's top edge only, odd bolts, scrap strips welded
    round the radar, a weld bead partway down the cameo columns' gap."""
    col = [(1920, 0), (1920, 1080), (1600, 1080), (1601, 700), (1604, 690), (1604, 640), (1600, 628), (1601, 300),
           (1603, 290), (1603, 250), (1600, 240), (1600, 0)]
    border = lambda a, b: a[0] == b[0] == 1920 or a[1] == b[1] == 0 or a[1] == b[1] == 1080      # noqa: E731
    o.tiers([rag(col, 5, 1.6, 14, border)], OPEN_COL, *TIERS["Gla"])
    head = sheet(1596, -4, 1924, 48.5, 0.4)                                          # the header, a sheet laid on crooked
    o.tiers([rag(head, 31, 1.2, 16)], (True, False, False, True), 6, 9, 2.4, 0.9)
    strips = [sheet(1603, 49.5, 1917, 53.5, 0.12), sheet(1603, 362.5, 1912, 366, -0.1), sheet(1601.5, 54, 1605.5, 362, 0.2),
              sheet(1914.5, 52, 1918.5, 364, -0.15)]
    o.bump(strips, 1.6, 0.6, STEEL)
    radar_glass(o)
    sidebar_common(o, "scavenged", 5, floor=False)
    for q, s in ((sheet(1621, 418.5, 1766.5, 1075.5, 0.08), 0), (sheet(1769.5, 417.5, 1917, 1076, -0.1), 1)):
        o.recess([rag(q, 50 + s, 1.0, 20)], CLOSED, 3.0, 1.0)                        # each column its own scrap tray
    welded(o, [(1600, 48.5), (1700, 49)], 60)
    welded(o, strips[0][:2], 70)
    o.weld((1603, 362.5), (1660, 362.5), 77, 1.0)
    left, right = sheet(1601, 407.5, 1702, 417, -1.0), sheet(1846, 406.5, 1918, 416.5, 1.6)
    o.bump([left, right], 1.6, 0.8, STEEL)
    welded(o, left, 80)
    welded(o, right, 90)
    hazard(o, 1608, 1700, 408.5)
    for i, (x, y, r) in enumerate(((1606, 413, 1.8), (1694, 411, 2.4), (1856, 411.5, 2.0), (1910, 410, 2.7), (1768, 1058, 2.3),
                                   (1916, 1074, 2.0), (1606, 30, 1.7))):
        o.bolt(x, y, r, seed=i)
    o.weld((1768, 505), (1768, 690), 101, 1.0)
    o.weld((1768, 880), (1768, 1030), 102, 1.0)
    for i, (y0, y1, deg) in enumerate(((598, 646, 3.0), (930, 994, -3.5))):        # scrap bent over the gap, bolted
        q = sheet(GAP_X - 3, y0, GAP_X + 3, y1, deg)
        o.bump([q], 1.6, 0.6, STEEL)
        o.bolt(GAP_X, y0 + 6, 1.5 + 0.4 * i, seed=40 + i)
    for i, (y, r) in enumerate(((122, 1.8), (291, 2.4))):
        o.bolt(1917, y, r, seed=50 + i)


def sb_turkiye(o, p):
    """Modern defence armour: wide 30 degree chamfers, an armour ledge with panel gaps and recessed fasteners, an
    optic housing at its end, range ticks round the radar and down the cameo columns' gap, one faint girih etch,
    turquoise LED lines (sidebar_lights) and a faint pixel camouflage."""
    col = [(1600, 0), (1920, 0), (1920, 1080), (1600, 1080), (1600, 418), (1606, 408), (1606, 404), (1600, 400),
           (1600, 52), (1605, 44), (1600, 40)]
    o.tiers([col], OPEN_COL, *TIERS["Turkiye"])
    o.tiers([[(1600, 0), (1920, 0), (1920, 50), (1614, 50), (1600, 42)]], (True, False, False, True), 6, 9, 4.0, 3.5)   # an armour header
    o.bump([shape("faceted", *grow(RADAR, 5), 6)], 2.4, 1.6, STEEL)
    radar_glass(o)
    sidebar_common(o, "faceted", 4)
    o.inlay([plate(1622, y, 1916, y + 1) for y in (JUNCTIONS[1], JUNCTIONS[3])], INKM, 0.9)   # the floor's armour tiles
    o.inlay([plate(1915, y, 1917 if i % 5 else 1918.5, y + 1) for i, y in enumerate(range(62, 358, 8))], INKM, 0.6)
    o.tiers([[(1600, 418), (1600, 413), (1612, 405), (1920, 405), (1920, 418)]], (False, False, False, True), 9, 12, 3.4, 1.8)
    o.inlay([plate(x, 406, x + 1, 418) for x in (1690, 1800, 1872)], INKM, 0.9)         # the armour panels' gaps
    for x in (1618, 1700, 1792, 1864):
        fastener(o, x, 411, 2.2)
    o.tiers([plate(1880, 403, 1912, 419, tl=("cut", 3), tr=("cut", 5))], CLOSED, 5, 8, 2.2, 1.2, 1.6)     # the optic
    o.sink([regular(1902, 411, 3.6, 16)], 3, 1.0)
    o.inlay([star8(x, 411, 3.0) for x in range(1716, 1790, 12)], DARK, 0.25)              # the one girih etch
    ticks = [plate(x, 50.5, x + 1, 52.5 if i % 5 else 53.5) for i, x in enumerate(range(1626, 1900, 8))]
    ticks += [plate(1766, y, 1768 if i % 5 else 1770.5, y + 1) for i, y in enumerate(range(504, 1032, 8))]
    o.inlay(ticks, INKM, 0.6)
    fastener(o, 1768, 1058)
    camo(p)


def sidebar_lights(img):
    S = lambda x, y: (x - SB + PL, y)         # noqa: E731
    lines = [(S(1622, 415.5), S(1688, 415.5)), (S(1702, 415.5), S(1790, 415.5)), (S(1616, 49.5), S(1906, 49.5)),
             (S(1612, 365), S(1626, 365)), (S(1898, 365), S(1912, 365))]
    lines += [(S(GAP_X + 2.5, y0 + 6), S(GAP_X + 2.5, y1 - 6)) for y0, y1 in zip([500] + JUNCTIONS, JUNCTIONS + [1032])]
    lamps = [(*S(1902, 411), TURQ), (*S(1874, 411), AMBER)]
    return led(img, lines, lamps)


SIDEBARS = {"America": sb_america, "China": sb_china, "Gla": sb_gla, "Turkiye": sb_turkiye}


def money_sign(img, x, y, size=20):
    font = ImageFont.truetype(os.path.join(FONT_DIR, "arialbd.ttf"), size)
    lay = Image.new("RGBA", img.size, (0, 0, 0, 0))
    d = ImageDraw.Draw(lay)
    d.text((x + 1, y + 1), "$", font=font, fill=(0, 0, 0, 200), anchor="mm")
    d.text((x, y), "$", font=font, fill=GOLD + (255,), anchor="mm")
    img.alpha_composite(lay)
    return img


def mount(o, build, x, y, r):
    """The side's fitting at a point: an octagon stud, a gold rivet, a hex bolt or a recessed fastener."""
    if build == "machined":
        o.stud(x, y, r)
    elif build == "terraced":
        o.bump([regular(x, y, r, 12)], 2.0, r * 0.6, GOLDM)
    elif build == "scavenged":
        o.bolt(x, y, r, seed=int(y))
    else:
        fastener(o, x, y, r * 1.1)


def powers_socket(o, build):
    """Where the general's powers wing joins (POWERS_AT): a slot open to the world edge down the radar band, with a
    mounting point at each row's middle, so the plate reads as having a place for the wing when there is none."""
    o.recess([plate(1594, POWERS_AT[1] + 2, 1605.5, POWERS_AT[1] + 4 * POWERS_CELL - 2)], CLOSED, 2.4, 1.0)
    for r in range(4):
        mount(o, build, 1602.6, POWERS_AT[1] + POWERS_CELL * r + POWERS_CELL / 2, 1.6)


def sidebar_plate(side):
    look = look_of(side)
    p = Painter(320 + PL, 1080)
    o = Offset(p, PL - SB, 0)
    SIDEBARS[side](o, p)
    powers_socket(o, look["build"])
    img = bar_finish(shade(p, look, shadow=True), p, look)
    if side == "Turkiye":
        img = sidebar_lights(img)
    img = img.crop((PL, 0, PL + 320, 1080))
    return money_sign(img, MONEY_SIGN_X - SB, (MONEY[1] + MONEY[3]) / 2)


# ---------------------------------------------------------------------------------------------- the strip plate

PT = 8


def strip_common(o, build, c):
    for r in CELLS:
        glass(o, r, build, c * 0.6, 2, 5, 2)
    for r in IDLE:
        seat(o, r, build, c * 0.6)
    glass(o, CLOCK, build, c * 0.6)


def st_america(o, p):
    top = [(0, 1016), (896, 1016), (902, 1022), (908, 1016), (1086, 1016), (1092, 1022), (1098, 1016), (1600, 1016),
           (1600, 1080), (0, 1080)]
    o.tiers([top], OPEN_STRIP, 4, 7, 3, 2.6)
    strip_common(o, "machined", 6)
    o.inlay([plate(8, 1016.6, 890, 1017.6), plate(1104, 1016.6, 1590, 1017.6)], ACCENT, 0.4)   # a blue hairline along the top
    for i in range(1, 13, 2):
        o.stud(68 + 64 * i, 1048, 1.8)
    o.recess([shape("machined", 914, 1024, 1080, 1072, 6)], CLOSED, 3, 2)
    H.vents(o, 936, 1052, 1058, 1066, 7)
    o.inlay([plate(926, 1037.5, 1040, 1038.5)], ACCENT, 0.5)
    o.inlay([swept(1046 + 9 * i, 1032, 1044, 6, 3.0, 1) for i in range(3)], ACCENT, 0.7)
    for x, y in ((921, 1031), (1073, 1031), (921, 1065), (1073, 1065), (1488, 1048)):
        o.stud(x, y, 2.2)


def st_china(o, p):
    top = [(0, 1016), (894, 1016), (894, 1020), (910, 1020)]
    top += curve((910, 1020), (912, 1016), (918, 1014), 4)[1:] + curve((1078, 1014), (1084, 1016), (1086, 1020), 4)
    top += [(1102, 1020), (1102, 1016), (1600, 1016), (1600, 1080), (0, 1080)]
    o.tiers([[(x, max(y, 1016)) for x, y in top]], OPEN_STRIP, 4, 9, 4, 0.9)
    strip_common(o, "terraced", 5)
    o.inlay([plate(0, 1016.2, 890, 1017.8), plate(1104, 1016.2, 1600, 1017.8)], ACCENT, 0.3)   # red lacquer along the top
    o.tiers([shape("terraced", 912, 1022, 1084, 1076, 4)], CLOSED, 8, 11, 3, 1.6)       # a raised plaque between the keys
    o.inlay([plate(918, 1027, 1078, 1029.5), plate(918, 1067, 1078, 1069.5)], ACCENT, 0.3)
    o.bump([regular(x, 1048, 2.6, 12) for x in (928, 1068, 1488)], 2.2, 1.4, GOLDM)
    for x in range(944, 1056, 14):
        o.rivet(x, 1048, 1.6)
    for i in range(13):
        for y in (1030, 1066):
            o.rivet(68 + 64 * i, y, 1.4)


def st_gla(o, p):
    top = [(0, 1017), (300, 1016.5), (620, 1018), (898, 1017), (930, 1019.5), (1060, 1019), (1094, 1016.5), (1600, 1017),
           (1600, 1080), (0, 1080)]
    border = lambda a, b: a[0] == b[0] == 1600 or a[1] == b[1] == 1080 or a[0] == b[0] == 0     # noqa: E731
    o.tiers([rag(top, 9, 1.4, 14, border)], OPEN_STRIP, 4, 7, 2.4, 0.9)
    strip_common(o, "scavenged", 5)
    patch = sheet(914, 1026, 1080, 1072, -1.6)
    o.bump([patch], 1.8, 0.8, STEEL)
    welded(o, patch, 120)
    hazard(o, 918, 1076, 1028.5, 4, 9)
    for i, (x, y, r) in enumerate(((920, 1040, 2.6), (1072, 1064, 1.9), (1490, 1030, 2.2), (1488, 1063, 2.8), (644, 1044, 2.0))):
        o.bolt(x, y, r, seed=10 + i)
    for i in (3, 7):
        o.weld((68 + 64 * i, 1024), (68 + 64 * i, 1060), 140 + i, 1.0)
    for i, (g, deg) in enumerate(((5, 4.0), (9, -3.0), (12, 2.5))):                # scrap bent over a gap, bolted
        x = 68 + 64 * g
        o.bump([sheet(x - 3.4, 1026, x + 3.4, 1068 - 6 * i, deg)], 1.6, 0.6, STEEL)
        o.bolt(x, 1032, 1.5 + 0.3 * i, seed=60 + i)


def st_turkiye(o, p):
    top = [(0, 1016), (896, 1016), (904, 1022), (906, 1022), (906, 1016), (1092, 1016), (1092, 1022), (1094, 1022),
           (1102, 1016), (1600, 1016), (1600, 1080), (0, 1080)]
    o.tiers([top], OPEN_STRIP, 4, 7, 4, 3.5)
    strip_common(o, "faceted", 4)
    o.tiers([[(912, 1076), (912, 1032), (926, 1024), (1084, 1024), (1084, 1076)]], CLOSED, 6, 9, 2.6, 1.6)  # an armour panel
    o.inlay([plate(x, 1026, x + 1, 1076) for x in (980, 1040)], INKM, 0.9)
    for x, y in ((922, 1066), (1074, 1034), (1074, 1066), (1488, 1048)):
        fastener(o, x, y, 2.4)
    o.inlay([plate(x, 1070, x + 1, 1073 if i % 5 else 1075) for i, x in enumerate(range(930, 976, 6))], INKM, 0.6)
    o.inlay([plate(66 + 64 * i, y, 66 + 64 * i + (3 if (y - 1030) % 18 else 4.5), y + 1) for i in range(13)
             for y in range(1030, 1068, 6)], INKM, 0.6)                              # range ticks in every gap
    camo(p, 6)


STRIPS = {"America": st_america, "China": st_china, "Gla": st_gla, "Turkiye": st_turkiye}


def strip_plate(side):
    look = look_of(side)
    p = Painter(1600, 64 + PT)
    o = Offset(p, 0, PT - 1016)
    STRIPS[side](o, p)
    img = bar_finish(shade(p, look, shadow=True), p, look)
    if side == "Turkiye":
        img = led(img, [((924, PT + 8), (1076, PT + 8)), ((8, PT + 1), (890, PT + 1))], [(1488, PT + 18, TURQ)])
    return img.crop((0, PT, 1600, PT + 64))


# ---------------------------------------------------------------------------------------------- the selection card

def card_common(o, build, c):
    glass(o, PORTRAIT, build, c, 2, 6, 2.5)
    for r in GRID:
        glass(o, r, build, c * 0.4, 1, 4, 1.4)
    glass(o, INFO, build, c * 0.5, 1, 5, 1.6)


def cd_america(o, p):
    o.tiers([plate(10, 890, 470, 1006, tl=("cut", 6), tr=("cut", 24), br=("cut", 8), bl=("cut", 4))], CLOSED, 4, 7, 3.4, 2.6)
    card_common(o, "machined", 6)
    o.inlay([plate(179.5, 900, 180.5, 996)], ACCENT, 0.5)
    o.inlay(H.chevrons(466, 930, 6, n=2), ACCENT, 0.6)
    for y in (962, 994):
        o.stud(466, y, 2.0)


def cd_china(o, p):
    out = [(10, 1006), (10, 900), (12, 900)] + curve((12, 900), (14, 894), (10, 889), 4)[1:]
    out += curve((10, 889), (20, 893.5), (32, 893.5), 4)[1:] + [(448, 893.5)]
    out += curve((448, 893.5), (460, 893.5), (470, 889), 4)[1:] + curve((470, 889), (466, 894), (468, 900), 4)[1:]
    out += [(470, 900), (470, 1002), (466, 1002), (466, 1006)]
    o.tiers([out], CLOSED, 4, 7, 3.4, 2.0)
    card_common(o, "terraced", 4)
    o.inlay([plate(178.5, 902, 181.5, 994)], ACCENT, 0.3)
    for x in range(28, 456, 16):
        o.rivet(x, 1003, 1.3)
    o.bump([regular(465.5, y, 2.2, 12) for y in (912, 986)], 2.0, 1.3, GOLDM)


def cd_gla(o, p):
    out = [(10, 891), (300, 890), (458, 891), (470, 900), (469, 1006), (16, 1006), (10, 1001)]
    o.tiers([rag(out, 21, 2.0, 14)], CLOSED, 4, 7, 2.4, 0.9)
    card_common(o, "scavenged", 4)
    patch = sheet(330, 889.5, 446, 894.5, 0.6)
    o.bump([patch], 1.4, 0.6, STEEL)
    welded(o, patch, 160, 0.9)
    hazard(o, 334, 444, 890.2, 3.6, 8)
    o.weld((180, 902), (180, 952), 170, 1.0)
    for i, (x, y, r) in enumerate(((465, 914, 2.2), (14, 1000.5, 1.7), (466, 988, 2.6), (170, 1003, 1.9))):
        o.bolt(x, y, r, seed=20 + i)


def cd_turkiye(o, p):
    o.tiers([plate(10, 890, 470, 1006, tr=("slant", 14), bl=("slant", 8))], CLOSED, 4, 7, 4.4, 3.5)   # a parallelogram
    card_common(o, "faceted", 4)
    o.inlay([plate(179.5, 896, 180.5, 1004)], INKM, 0.9)
    for x, y in ((465.5, 920), (465.5, 990), (180, 1002.5)):
        fastener(o, x, y, 2.1)
    o.inlay([plate(x, 1002, x + 1, 1004 if i % 5 else 1005) for i, x in enumerate(range(196, 456, 8))], INKM, 0.6)
    camo(p, 7)


CARDS = {"America": cd_america, "China": cd_china, "Gla": cd_gla, "Turkiye": cd_turkiye}


def card_frame(side):
    look = look_of(side)
    pad = 24
    p = Painter(464 + 2 * pad, 120 + 2 * pad)
    o = Offset(p, pad - CARD[0], pad - CARD[1])
    CARDS[side](o, p)
    img = bar_finish(shade(p, look, shadow=True, falloff=False), p, look, gloss=False)
    if side == "Turkiye":
        S = lambda x, y: (x - CARD[0] + pad, y - CARD[1] + pad)   # noqa: E731
        img = led(img, [(S(26, 892), S(300, 892))], [(*S(465.5, 955), TURQ)])
    return img.crop((pad, pad, pad + 464, pad + 120))


# ---------------------------------------------------------------------------------------------- small pieces

def key(side, w, h, state, layers=(), k=1.0, corner=1.0, dx=0.0, dy=0.0):
    """hud_command_bar.key_piece with any number of glyph layers ((polys, fill), in a 22 box round 11, 11) or none,
    for the code's text.  '' up, 'H' hover, 'P' lit (the face in the side's open colour), 'I' disabled."""
    look = look_of(side)
    build = look["build"]
    edge = MOUNT_GLYPH_BUILD.get(build, build)
    o = 32
    p = Painter(w + 2 * o, h + 2 * o)
    down = state == "P"
    r = H.COMMAND_TAB_RIM
    p.frame([shape(build, o, o, o + w, o + h, 4 * corner)], 2.2, edge, DARK, BEZEL_SCALE)
    p.frame([shape(build, o + r, o + r, o + w - r, o + h - r, 3.2 * corner)], 2.4 if down else 3.6, edge,
            {"": STEEL, "H": BRIGHT, "P": BRIGHT, "I": DARK}[state], BEZEL_SCALE)
    i = r + 3.2
    face = shape(build, o + i, o + i, o + w - i, o + h - i, 2 * corner)
    p.sink([face], 2.8 if down else 1.6, 1.0)
    if state == "H":
        p.inlay([plate(o + i + 4, o + h - i - 2.5, o + w - i - 4, o + h - i - 1)], HILITE, 0.2)
    # each build's fitting on the bezel, as the button sheets have them: America a blue hairline over the face, China a
    # rivet in each wing, the GLA one bolt in its top right, Turkiye triangle markers pointing in from the wings
    my = o + h / 2
    if build == "machined" and w >= 40:
        p.inlay([plate(o + w / 2 - min(16, w / 5), o + 2.0, o + w / 2 + min(16, w / 5), o + 2.8)], ACCENT, 0.3)
    elif build == "terraced":
        for x in (o + 3.0, o + w - 3.0):
            p.rivet(x, my, 1.2)
    elif build == "scavenged":
        p.bolt(o + w - 3.2, o + 3.2, 1.3, w)
    elif build == "faceted":
        p.inlay([H.triangle(o + 3.0, my, 2.4, 4.4, "right"), H.triangle(o + w - 3.0, my, 2.4, 4.4, "left")], INKM, 0.6)
    img = shade(p, look, shadow=False, falloff=False).crop((o, o, o + w, o + h))
    big = 8

    def mask_of(polys_fills):
        m = Image.new("L", (w * big, h * big), 0)
        d = ImageDraw.Draw(m)
        for polys, fill in polys_fills:
            for q in polys:
                d.polygon([(x * big, y * big) for x, y in q], fill=fill)
        return m.resize((w, h), Image.BOX)

    if down:
        lit = np.asarray(COMMAND_TAB_OPEN[side], np.float32)
        yy = np.arange(h, dtype=np.float32)[:, None, None]
        ramp = 0.62 + 0.5 * (yy - i) / (h - 2 * i)
        rgb = np.broadcast_to(lit * ramp * np.where(yy % 2 == 0, 0.9, 1.0), (h, w, 3)).copy()
        rgb[int(h - i - 3):int(h - i)] = np.minimum(lit * 1.25 + 60, 255)
        layer = Image.fromarray(np.clip(rgb, 0, 255).astype(np.uint8), "RGB").convert("RGBA")
        layer.putalpha(mask_of([([[(x - o, y - o) for x, y in face]], 255)]))
        img.alpha_composite(layer)
    if layers:
        off = 0.8 if down else 0.0
        colour = {"": MOUNT_GLYPH[side] if side == "Turkiye" else PAPER, "H": GOLD, "P": (14, 16, 22), "I": (92, 96, 100)}[state]
        img.alpha_composite(flat_glyph(w, h, layers, (h - 2 * i) / 14.5 * k, w / 2 + dx + off, h / 2 + dy + off - (1.2 if down else 0),
                                       colour))
    return img


def flat_glyph(w, h, layers, kk, cx, cy, colour, big=8):
    """Glyph layers ((polys, fill) in a 22 box round 11, 11) scaled by kk round (cx, cy), flat colour, antialiased."""
    m = Image.new("L", (w * big, h * big), 0)
    d = ImageDraw.Draw(m)
    for polys, fill in layers:
        for q in polys:
            d.polygon([((cx + (x - 11) * kk) * big, (cy + (y - 11) * kk) * big) for x, y in q], fill=fill)
    glyph = Image.new("RGBA", (w, h), colour + (255,))
    glyph.putalpha(m.resize((w, h), Image.BOX))
    return glyph


# The four tabs' glyphs (the owner's order of 2026-11-15: an icon on each tab, no word), each side in its own language,
# in a 22 box round (11, 11): (polys, 255) raised, (polys, 0) cut back out.  America machined, 45 degree shoulders and
# octagons; China terraced, stepped blocks and roofs whose eaves flick up; the GLA scrap, lopsided and crooked, a
# technical for its vehicle; Turkiye faceted, wedges and 30 degree chamfers.  The key letter keeps the top left, so
# the glyph stands right of the middle (TAB_GLYPH).
TAB_KINDS = ("Buildings", "Defenses", "Infantry", "Vehicles")
TAB_KEYS = dict(Buildings="Q", Defenses="W", Infantry="E", Vehicles="R")
TAB_GLYPH = dict(k=0.74, dx=5.0, dy=1.0)


def box(a, b, c, d):
    return [(a, b), (c, b), (c, d), (a, d)]


def tab_glyph(build, kind):
    R, I = 255, 0
    if kind == "Buildings":
        if build == "machined":          # a hangar, its shoulders cut at 45, a door and two vents
            return [([[(2.5, 18), (2.5, 10), (6.5, 6), (15.5, 6), (19.5, 10), (19.5, 18)]], R),
                    ([box(8.5, 12, 13.5, 18), box(5, 9.6, 7.5, 10.6), box(14.5, 9.6, 17, 10.6)], I)]
        if build == "terraced":          # a hall under two roofs whose eaves flick up
            return [([box(5, 11.2, 17, 18), box(7.5, 7, 14.5, 9.6),
                      [(1.5, 8.6), (3.6, 9.8), (18.4, 9.8), (20.5, 8.6), (19.4, 11.6), (2.6, 11.6)],
                      [(5, 4.4), (6.8, 5.4), (15.2, 5.4), (17, 4.4), (16.2, 7.2), (5.8, 7.2)]], R),
                     ([box(9.5, 13.4, 12.5, 18)], I)]
        if build == "scavenged":         # a shack: crooked walls, a lopsided roof, a door hung askew
            return [([[(4, 18), (4.6, 10.4), (17.2, 9.4), (18.2, 18)],
                      [(1.6, 11.6), (9.4, 4.2), (20.4, 8.6), (19.6, 10.6), (9.2, 6.8), (2.8, 13)]], R),
                     ([[(8, 18), (8.4, 12.8), (12.2, 12.6), (12.6, 18)], [(14, 12.4), (16.4, 12.2), (16.5, 14.2), (14.1, 14.4)]], I)]
        return [([[(2, 18), (2, 12), (12, 5.6), (20, 9.2), (20, 18)]], R),          # a wedge of a hangar, facets grooved
                ([[(9, 18), (10.4, 13), (14, 13), (12.6, 18)], [(11.6, 6.8), (12.4, 6.8), (12.4, 11.6), (11.6, 11.6)]], I)]
    if kind == "Defenses":
        if build == "machined":          # an octagon turret on a cut base
            return [([octagon_(3, 15, 19, 18.6, 1.4), octagon_(5.6, 7.4, 15.4, 15.4, 2.8), box(14, 9.4, 21, 11.2)], R),
                    ([box(7.6, 10, 11, 11.2)], I)]
        if build == "terraced":          # a bunker in three square terraces, a slit and a gun
            return [([box(2.5, 15.4, 19.5, 18.6), box(4.8, 12, 17.2, 15.4), box(7.2, 8.4, 14.8, 12), box(14, 9.2, 21, 10.8)], R),
                    ([box(7.4, 13.2, 11, 14.4), box(12, 13.2, 14.6, 14.4)], I)]
        if build == "scavenged":         # a heap of sandbags, a scrap shield, a gun aimed crooked
            return [([[(1.6, 18.6), (2.6, 14.6), (6, 12.8), (10.6, 12.2), (15.4, 13), (19.4, 15.2), (20.4, 18.6)],
                      [(6.6, 13), (7.4, 8), (12.8, 7.2), (13.6, 12.6)], [(12.4, 8.6), (20.6, 6.2), (21, 7.6), (12.8, 10.2)]], R),
                     ([[(4.6, 15.6), (9.6, 15), (9.7, 15.8), (4.7, 16.4)], [(11.4, 15.4), (17, 16), (16.9, 16.8), (11.3, 16.2)]], I)]
        return [([[(1.6, 18.6), (3.6, 15), (18.4, 15), (20.4, 18.6)],                # a faceted turret, a long gun
                  [(4.8, 15), (7, 9.4), (13.6, 8.2), (16.8, 10.8), (16, 15)], box(15, 9.4, 21.6, 10.8)], R),
                ([[(7, 11.6), (15.4, 11), (15.5, 11.8), (7.1, 12.4)]], I)]
    if kind == "Infantry":
        shoulders = [(3, 19), (3.8, 15.2), (8, 13.4), (14, 13.4), (18.2, 15.2), (19, 19)]
        head = octagon_(8.5, 7.2, 13.5, 12.6, 1.6)
        if build == "machined":          # helmet dome cut at 45, rifle over the shoulder
            return [([shoulders, head, [(7.2, 9), (7.4, 6), (9.2, 4.2), (12.8, 4.2), (14.6, 6), (14.8, 9)],
                      [(16.2, 19), (19.4, 3.6), (20.8, 3.9), (17.6, 19.2)]], R),
                    ([[(4.6, 15.4), (5.4, 14.8), (16.6, 18.6), (15.8, 19.2)]], I)]
        if build == "terraced":          # a peaked cap in steps, collar tabs, rifle upright
            return [([shoulders, head, box(8.2, 3.6, 13.8, 6.4), box(7.2, 6.4, 14.8, 7.8), box(9.4, 7.8, 14.8, 8.8),
                      box(17.4, 2.6, 18.8, 19)], R),
                    ([box(6.2, 15, 8, 16.6), box(14, 15, 15.8, 16.6)], I)]
        if build == "scavenged":         # a wrapped head, the slit for the eyes, a rifle slung crooked
            return [([[(3, 19), (4.4, 14.6), (7.2, 13.2), (6.6, 9), (7.4, 5), (10.2, 3.4), (13.2, 3.8), (15, 6.2), (15.2, 10.6),
                       (14.4, 13.2), (18, 15), (19.2, 19)], [(15.8, 19), (20.6, 5.4), (21.6, 6), (17.2, 19.2)],
                      [(17.6, 12.6), (20, 13.4), (19.4, 15.2), (17.2, 14.4)]], R),
                     ([[(8.2, 7.2), (14, 6.6), (14, 9.4), (8.2, 9.8)]], I)]
        return [([[(3, 19), (4.6, 15), (8.4, 13.4), (13.6, 13.4), (17.4, 15), (19, 19)],      # an angular helmet, a visor
                  [(8.6, 12.6), (8, 8.6), (7.4, 6.8), (10, 4), (14.4, 4.6), (15.6, 7.4), (14.6, 12.6)],
                  [(16.2, 19), (19.4, 3.6), (20.8, 3.9), (17.6, 19.2)]], R),
                ([[(8.6, 7.6), (14.8, 7.4), (14.6, 9), (8.8, 9.2)]], I)]
    # Vehicles
    if build == "machined":              # a tank side on, its hull cut at 45
        return [([octagon_(2, 13.4, 20, 18.6, 2.4), [(3, 13.6), (5, 10.4), (17, 10.4), (19.4, 13.6)],
                  [(6.4, 10.4), (8, 6.6), (13.6, 6.6), (15, 10.4)], box(13.4, 7.6, 21.6, 8.8)], R),
                ([octagon_(x - 1.3, 14.8, x + 1.3, 17.4, 0.6) for x in (5, 8.6, 12.2, 15.8)], I)]
    if build == "terraced":              # a heavy tank in steps: tracks, two hull tiers, a block turret, a long gun
        return [([box(2, 14, 20, 18.6), box(3.4, 11.4, 18.6, 14), box(5.6, 9.4, 16.4, 11.4), box(7, 6, 13.4, 9.4),
                  box(13.4, 7, 21.6, 8.4)], R),
                ([box(3.6, 15.4, 18.4, 16.2)], I)]
    if build == "scavenged":             # a technical: the cab, the bed, a gun on a post aimed high
        return [([[(3.4, 13.6), (4.6, 8.4), (9.8, 8.2), (11, 11.2), (11, 13.6)], [(10.8, 11), (19.6, 11.4), (19.4, 13.6), (10.8, 13.6)],
                  [(1.6, 13.4), (20.6, 13.6), (20.4, 15.8), (1.8, 15.6)], regular(5.6, 16.4, 2.4, 8), regular(16.4, 16.6, 2.4, 8),
                  box(14, 6.6, 15.2, 11.2), [(13.4, 7.4), (20.6, 4.4), (21, 5.6), (13.8, 8.6)]], R),
                 ([[(5.4, 9.4), (9.2, 9.3), (9.9, 11), (5.3, 11.1)], regular(5.6, 16.4, 0.9, 6), regular(16.4, 16.6, 0.9, 6)], I)]
    return [([[(2, 16), (3.6, 13.2), (18.4, 13.2), (20, 16), (18.4, 18.8), (3.6, 18.8)],      # a faceted hull, a wedge turret
              [(3, 13.2), (5.4, 10), (17.4, 10), (20, 13.2)], [(6, 10), (8.4, 6.4), (13.2, 6), (15.4, 8), (14.4, 10)],
              box(13.6, 7, 21.6, 8.4)], R),
            ([H.triangle(x, 16, 2.6, 2.4, "up") for x in (6, 9.4, 12.8, 16.2)], I)]


def octagon_(x0, y0, x1, y1, c):
    return H.octagon(x0, y0, x1, y1, c)


def icon_layers(kind, build):
    out = icon(kind, 11, 11, build)
    return [(polys, 255 if n % 2 == 0 else 0) for n, polys in enumerate(out)]


def arrow_layers(build, up):
    raised, ink = page_arrow_glyph(build, True)
    turn = lambda polys: [rotated(q, 11, 11, -90 if up else 90) for q in polys]   # noqa: E731
    return [(turn(raised), 255), (turn(ink), 0)]


def cameo_well(side):
    """The cell under a cameo: the bezel 3 texels wide in the side's corner, a scanline well inside."""
    look = look_of(side)
    build = look["build"]
    w, h, o = 140, 86, 24
    p = Painter(w + 2 * o, h + 2 * o)
    c = {"machined": 6, "terraced": 4, "scavenged": 5, "faceted": 5}[build]
    p.tiers([shape(build, o, o, o + w, o + h, c)], CLOSED, 3.0, 5.0, 1.6, 1.0, 1.6)
    p.sink([shape(build, o + CAMEO_INSET, o + CAMEO_INSET, o + w - CAMEO_INSET, o + h - CAMEO_INSET, c * 0.6)], 4, 1.0)
    if build == "terraced":
        for x, y in ((o + 2, o + h / 2), (o + w - 2, o + h / 2)):
            p.rivet(x, y, 1.0)
    elif build == "scavenged":
        p.bolt(o + w - 2.5, o + 2.5, 1.4, 3)
    elif build == "machined":
        p.inlay([plate(o + w / 2 - 14, o + 0.8, o + w / 2 + 14, o + 1.6)], ACCENT, 0.3)
    img = bar_finish(shade(p, look, shadow=False, falloff=False), p, look, gloss=False)
    return img.crop((o, o, o + w, o + h))


def sw_row(side, enemy):
    """A superweapon timer row: the weapon's icon well, the name and clock's glass, a groove for the countdown and a
    lamp socket, the lamp round in the side's colour for one's own, a red warning triangle on slashes for an enemy's."""
    look = look_of(side)
    build = look["build"]
    w, h, o = 288, 28, 24
    p = Painter(w + 2 * o, h + 2 * o)
    c = {"machined": 5, "terraced": 4, "scavenged": 4, "faceted": 4}[build]
    P = Offset(p, o, o)
    P.tiers([shape(build, 0, 0, w, h, c)], CLOSED, 3.0, 5.0, 2.0, 1.2, 1.6)
    P.sink([shape(build, *SW_ICON, 2)], 4, 1.2)
    P.sink([plate(*SW_TEXT)], 4, 1.2)
    P.sink([plate(*SW_GROOVE)], 5, 1.0)
    P.sink([shape(build, *SW_LAMP, 1.5)], 3, 1.0)
    if enemy:
        x0, y0, x1, y1 = SW_LAMP
        P.inlay([[(x, y1 - 1), (x + 3, y0 + 1), (x + 4.5, y0 + 1), (x + 1.5, y1 - 1)] for x in np.arange(x0 + 0.5, x1 - 3, 3.2)],
                (ACCENT if side != "Turkiye" else INKM), 0.2)
    img = bar_finish(shade(p, look, shadow=False, falloff=False), p, look, scan=False, gloss=False).crop((o, o, o + w, o + h))
    cx, cy = (SW_LAMP[0] + SW_LAMP[2]) / 2, (SW_LAMP[1] + SW_LAMP[3]) / 2
    lay = Image.new("RGBA", (w * 4, h * 4), (0, 0, 0, 0))
    d = ImageDraw.Draw(lay)
    if enemy:
        d.polygon([((cx - 4.6) * 4, (cy + 3.6) * 4), (cx * 4, (cy - 4.6) * 4), ((cx + 4.6) * 4, (cy + 3.6) * 4)], fill=(222, 44, 34, 255))
        d.rectangle(((cx - 0.5) * 4, (cy - 1.6) * 4, (cx + 0.5) * 4, (cy + 1.2) * 4), fill=(20, 8, 6, 255))
    else:
        d.ellipse(((cx - 3.4) * 4, (cy - 3.4) * 4, (cx + 3.4) * 4, (cy + 3.4) * 4), fill=COMMAND_TAB_OPEN[side] + (255,))
        d.ellipse(((cx - 2.2) * 4, (cy - 2.6) * 4, (cx - 0.6) * 4, (cy - 1.0) * 4), fill=(255, 255, 255, 170))
    img.alpha_composite(lay.resize((w, h), Image.BOX))
    return img


# ---------------------------------------------------------------------------------------------- the powers wing

WING_N, WING_PAD = 3, 24                        # the reference wing: 3x3 cells, every piece cut from its middle


def wing_outline(build, e, t, b, W):
    """The wing in the reference, open on its right (the Joint and past it), its two outer corners the side's.  The GLA's
    edges are bitten the same in every cell's length, so a cut piece tiles."""
    c0 = e + POWERS_RIM
    if build != "scavenged":
        kind, c = {"machined": ("cut", 6), "terraced": ("notch", 4), "faceted": ("slant", 4.5)}[build]
        return plate(e, t, W, b, tl=(kind, c), bl=(kind, c))
    bite = [(0, 0), (14, 1.4), (31, 0.5), (47, 1.8), (60, 0.6)]
    top = [(c0 + POWERS_CELL * r + x, t + d) for r in range(WING_N) for x, d in bite]
    foot = [(c0 + POWERS_CELL * r + x, b - d) for r in reversed(range(WING_N)) for x, d in reversed(bite)]
    left = [(e + d, c0 + POWERS_CELL * r + y) for r in reversed(range(WING_N)) for y, d in reversed(bite)]
    return ([(e, c0 - 0.5), (e + 2.6, t + 3.2)] + top + [(W, t), (W, b)] + foot + [(c0 - 0.4, b - 0.4), (e + 1.2, b - 4.6)]
            + left[:-1])


def powers_wing(side):
    """The general's powers wing drawn whole, three cells by three, lit from the top left with no falloff, and every
    piece cut from its middle so it tiles: Cell, Top, Bottom, Edge, Joint, CornerTop, CornerBottom."""
    look = look_of(side)
    build = look["build"]
    C, R = POWERS_CELL, POWERS_RIM
    e = t = WING_PAD
    c0 = e + R
    j = c0 + WING_N * C
    end = b = j + R
    W = end + WING_PAD
    p = Painter(W, b + WING_PAD)
    p.tiers([wing_outline(build, e, t, b, W)], (False, False, False, True), 4, 7, 4, TIERS[side][3])
    cw = {"machined": 5, "terraced": 4, "scavenged": 4, "faceted": 4}[build]
    for r in range(WING_N):
        for k in range(WING_N):
            x, y = c0 + C * k, c0 + C * r
            p.sink([shape(build, x + POWERS_WELL[0], y + POWERS_WELL[1], x + POWERS_WELL[2], y + POWERS_WELL[3], cw)], 5, 1.6)
            if build == "terraced":
                for dx, dy in ((2, 2), (C - 2, 2), (2, C - 2), (C - 2, C - 2)):
                    p.rivet(x + dx, y + dy, 1.1)
            elif build == "scavenged":
                p.bolt(x + C - 2.4, y + 2.4, 1.2, 7)
    for r in range(WING_N):                     # the joint, the same in every row
        y = c0 + C * r
        if build == "machined":
            p.bump([plate(j + 1, y, end - 1, y + C)], 2.0, 1.0, STEEL)
            p.inlay([plate(j + 3.5, y + 8, j + 4.5, y + 26), plate(j + 3.5, y + 46, j + 4.5, y + 64)], ACCENT, 0.5)
            p.stud(j + 4, y + C / 2, 2.0)
        elif build == "terraced":
            p.bump([plate(j + 1, y, end - 1, y + C)], 2.0, 0.6, STEEL)
            p.inlay([plate(j + 2.4, y + 4, j + 5.6, y + C - 4)], ACCENT, 0.3)
            p.bump([regular(j + 4, y + C / 2, 2.2, 12)], 2.2, 1.3, GOLDM)
            for dy in (12, C - 12):
                p.rivet(j + 4, y + dy, 1.2)
        elif build == "scavenged":
            p.bump([plate(j + 1.5, y, end - 1, y + C)], 1.6, 0.6, STEEL)
            p.weld((j + 1.2, y), (j + 1.2, y + C), 300, 1.0)
            p.weld((end - 1.2, y + 10), (end - 1.2, y + 50), 301, 1.0)
            p.bolt(j + 4.4, y + C / 2 + 6, 1.8, 3)
        else:
            p.bump([plate(j + 1, y + 1, end - 1, y + C - 1)], 2.2, 2.0, STEEL)
            fastener(p, j + 4, y + C / 2, 2.0)
    if build == "faceted":
        camo(p, 8)
    img = bar_finish(shade(p, look, shadow=False, falloff=False), p, look, gloss=False)
    if build == "faceted":
        img = led(img, [((j + 4, c0 + C * r + a), (j + 4, c0 + C * r + z)) for r in range(WING_N) for a, z in ((8, 26), (46, 64))], [])
    m = c0 + C                                   # the middle cell's left and top
    cuts = {"PowersCell": (m, m, m + C, m + C), "PowersTop": (m, t, m + C, c0), "PowersBottom": (m, b - R, m + C, b),
            "PowersEdge": (e, m, c0, m + C), "PowersJoint": (j, m, end, m + C),
            "PowersCornerTop": (e, t, c0, c0), "PowersCornerBottom": (e, b - R, c0, b)}
    return {name: img.crop(box) for name, box in cuts.items()}


STATES = ("", "H", "P", "I")


def pieces(side):
    """Every piece the sheets carry, by its name after <Prefix>Sidebar."""
    build = look_of(side)["build"]
    out = {"Plate": sidebar_plate(side), "Strip": strip_plate(side), "Card": card_frame(side), "CameoWell": cameo_well(side),
           "SuperweaponOwn": sw_row(side, False), "SuperweaponEnemy": sw_row(side, True)}
    for kind in TAB_KINDS:
        g = tab_glyph(build, kind)
        for s in ("", "H", "P", "I"):
            out["Tab" + kind + s] = key(side, 66, 36, s, g, **TAB_GLYPH)
        kk = (36 - 2 * (H.COMMAND_TAB_RIM + 3.2)) / 14.5 * TAB_GLYPH["k"]          # the glyph alone, white, for the code to tint
        out["TabIcon" + kind] = flat_glyph(66, 36, g, kk, 33 + TAB_GLYPH["dx"], 18 + TAB_GLYPH["dy"], (255, 255, 255))
    for s in ("", "H", "P", "I"):
        out["ArrowUp" + s] = key(side, 140, 28, s, arrow_layers(build, True), 0.8, 0.8)
        out["ArrowDown" + s] = key(side, 140, 28, s, arrow_layers(build, False), 0.8, 0.8)
    for s in ("", "P"):
        out["ChipWide" + s] = key(side, 66, 28, s, corner=0.8)
        out["Chip" + s] = key(side, 40, 28, s, corner=0.8)
    for s in STATES:
        out["KeyMenu" + s] = key(side, 72, 32, s, icon_layers("Options", build), 0.55)
        out["KeyDiplomacy" + s] = key(side, 72, 32, s, icon_layers("Chat", build), 0.55)
        out["KeySell" + s] = key(side, 74, 36, s)
        out["KeyRepair" + s] = key(side, 74, 36, s)
        out["KeyGeneral" + s] = key(side, 140, 36, s, icon_layers("General", build), 0.55)
    out.update(powers_wing(side))
    return out


def idle_keys(side):
    """The strip's six idle keys in the tabs' family, 56x56.  Not on the sheets: the strip draws the scheme's own idle
    buttons; the preview shows these."""
    build = look_of(side)["build"]
    return {"Idle" + kind: key(side, 56, 56, "", icon_layers(kind, build), kk)
            for kind, kk in (("Worker", 0.36), ("Infantry", 0.36), ("Vehicle", 0.34), ("Aircraft", 0.36), ("SelectArmy", 0.3),
                             ("Beacon", 0.4))}


# ---------------------------------------------------------------------------------------------- the sheets

SHEET = (512, 2048)                             # ReforgedHud<Side>Sidebar
STRIP_SHEET = (2048, 128)                       # ReforgedHud<Side>SidebarStrip
SHEET_GAP = 5                                   # between two boxes: put()'s one texel gutter each and 3 clear


def sheet_names():
    """The pieces in the order they are packed, with their sizes."""
    names = [("Plate", (320, 1080)), ("Card", (464, 120)), ("CameoWell", (140, 86)),
             ("SuperweaponOwn", (288, 28)), ("SuperweaponEnemy", (288, 28))]
    names += [("Tab" + kind + s, (66, 36)) for kind in TAB_KINDS for s in STATES] + [("TabIcon" + kind, (66, 36)) for kind in TAB_KINDS]
    names += [("Arrow" + d + s, (140, 28)) for d in ("Up", "Down") for s in STATES]
    names += [("ChipWide", (66, 28)), ("ChipWideP", (66, 28)), ("Chip", (40, 28)), ("ChipP", (40, 28))]
    names += [("Key" + k + s, size) for k, size in (("Menu", (72, 32)), ("Diplomacy", (72, 32)), ("Sell", (74, 36)),
                                                     ("Repair", (74, 36)), ("General", (140, 36))) for s in STATES]
    C, R = POWERS_CELL, POWERS_RIM              # appended, so nothing before them moved
    names += [("PowersCell", (C, C)), ("PowersJoint", (R, C)), ("PowersEdge", (R, C)), ("PowersTop", (C, R)),
              ("PowersBottom", (C, R)), ("PowersCornerTop", (R, R)), ("PowersCornerBottom", (R, R))]
    return names


def sheet_at():
    """Every piece's box (x0, y0, x1, y1): the Strip alone on its sheet, the rest packed in shelves down the main one,
    the Plate first.  The same on every side."""
    at = {"Strip": (2, 2, 1602, 66)}
    x, y, shelf = 2, 2, 0
    for name, (w, h) in sheet_names():
        if x + w + 2 > SHEET[0]:
            x, y, shelf = 2, y + shelf + SHEET_GAP, 0
        at[name] = (x, y, x + w, y + h)
        x += w + SHEET_GAP
        shelf = max(shelf, h)
    assert y + shelf + 2 <= SHEET[1], "the pieces overrun the sheet"
    return at


SHEET_AT = sheet_at()


def texture_name(side, piece):
    return "ReforgedHud%sSidebar%s" % (side, "Strip" if piece == "Strip" else "")


def paint_sheets(side):
    P = pieces(side)
    assert set(P) == set(SHEET_AT), "pieces and places differ: %s" % sorted(set(P) ^ set(SHEET_AT))
    main, strip = Image.new("RGBA", SHEET, (0, 0, 0, 0)), Image.new("RGBA", STRIP_SHEET, (0, 0, 0, 0))
    for name, img in P.items():
        H.put(strip if name == "Strip" else main, img, SHEET_AT[name])
    return {texture_name(side, "Plate"): main, texture_name(side, "Strip"): strip}


def check_mapped(side):
    """The side's mapped INI names exactly the pieces, each at its place on its own texture."""
    path = os.path.join(H.HERE, "..", "Data", "INI", "MappedImages", "HandCreated", "ReforgedHud%sSidebar.ini" % side)
    text = open(path, encoding="latin-1").read()
    got = {}
    for m in re.finditer(r"MappedImage\s+(\S+)\s+Texture\s*=\s*(\S+)\s+TextureWidth\s*=\s*(\d+)\s+TextureHeight\s*=\s*(\d+)\s+"
                         r"Coords\s*=\s*Left:(\d+)\s+Top:(\d+)\s+Right:(\d+)\s+Bottom:(\d+)", text):
        got[m.group(1)] = (m.group(2), int(m.group(3)), int(m.group(4))) + tuple(int(v) for v in m.groups()[4:])
    want = {}
    for piece, box in SHEET_AT.items():
        size = STRIP_SHEET if piece == "Strip" else SHEET
        want[PREFIX[side] + "Sidebar" + piece] = (texture_name(side, piece) + ".tga",) + size + box
    assert got == want, "ReforgedHud%sSidebar.ini: %s" % (side, sorted(set(got.items()) ^ set(want.items()))[:6])


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", required=True, help="the PNGs")
    ap.add_argument("--side", default="all")
    ap.add_argument("--dds", help="Data/Art/Textures, to write the DXT5 textures too")
    ap.add_argument("--no-check", action="store_true", help="skip the mapped INI check (while it is being written)")
    args = ap.parse_args()
    os.makedirs(args.out, exist_ok=True)
    for side in (SIDES if args.side == "all" else (args.side,)):
        if not args.no_check:
            check_mapped(side)
        for name, img in paint_sheets(side).items():
            img.save(os.path.join(args.out, name + ".png"))
            if args.dds:
                img.save(os.path.join(args.dds, name + ".dds"), pixel_format="DXT5")
                with open(os.path.join(args.dds, name + ".dds"), "rb") as f:
                    assert f.read(88)[84:88] == b"DXT5", "%s.dds is not DXT5" % name
        print(side, "done", flush=True)


if __name__ == "__main__":
    main()
