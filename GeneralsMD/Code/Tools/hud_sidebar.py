"""The Red Alert style layout's art (Options, Layout = Red Alert Style), drawn from nothing in the command bar
generator's own tools (hud_command_bar: Painter, shade, bar_finish, the key bezels), in round 18's three tiers and
each side's build.  The owner picked it from the preview of 2026-11-15
(experiments/proofs/2026-11-15/hud-sidebar-art in the wrapper, whose preview.py imports this).  Every size is the
1920x1080 rect of HudLayoutRects (GameClient/Display.h); the code scales by k = H/1080.

Each side has two textures and one mapped INI, MappedImages/HandCreated/ReforgedHud<Side>Sidebar.ini:
    ReforgedHud<Side>Sidebar       512x2048: Plate 320x1080 at the sidebar's rect, Card 470x120, CameoWell 92x75,
                                   Tab<Kind>{,H,P,I} and TabIcon<Kind> 70x36 (Buildings, Defenses, Infantry,
                                   Vehicles), ArrowUp/ArrowDown{,H,P,I} 140x28, Chip{,P} 50x28, ChipWide{,P} 66x28,
                                   SuperweaponOwn/Enemy 288x40, KeyMenu/KeyDiplomacy{,H,P,I} 72x32,
                                   KeySell/KeyRepair{,H,P,I} 74x36, KeyGeneral{,H,P,I} 140x36, the powers wing's
                                   PowersCell 78x64, PowersTop/Bottom 78x8, PowersEdge 8x64, PowersJoint 8x8 and
                                   PowersCornerTop/Bottom 8x8
    ReforgedHud<Side>SidebarStrip  2048x128: Strip 1600x64
named <Prefix>Sidebar<Piece> (AmericaPro, ChinaPro, GLAPro, TurkiyePro).  The pieces' places are SHEET_AT, the same on
every side; the INI is written by hand from them and check_mapped() holds it to them.

What the code has to know about the art:
    the plates go on in slices (W3DControlBar.cpp drawPlateRun): the Plate as head 0..1040 from the top, tail 1044..1080
    to the foot and rows PLATE_BAND repeated between; the Strip as head 0..904 from the area's left, tail 1496..1600 to
    the sidebar and columns STRIP_BAND repeated between.  Both bands are made exactly uniform along the run (band())
    the plate has no cameo wells: the code draws CameoWell under each of the 3x7 cells
    the strip has no idle seats: the idle row moves with the screen's width, over the plain band
    the tab glyph is baked TAB_GLYPH_H high (80% of the face, which is TAB_FACE in from each edge), its box centred
    on TAB_GLYPH_AT (a pressed tab's a little down and right, as key() moves it)
    the powers wing's Joint is 8x8 and tiles down any run that is a multiple of 8; the Edge is one cell high
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
# Down to the tabs one grid (2026-11-17): four columns 70 wide 8 apart over the radar's 1608..1912, a wide piece two
def col(first, span, y0, y1):
    return (1608 + 78 * first, y0, 1608 + 78 * first + 70 + 78 * (span - 1), y1)


MENU, MONEY, DIPLO = col(0, 1, 8, 40), col(1, 2, 8, 40), col(3, 1, 8, 40)
RADAR = (1608, 56, 1912, 360)
SELL, REPAIR, GENERAL = col(0, 1, 368, 404), col(1, 1, 368, 404), col(2, 2, 368, 404)
TAB_W, TAB_H = 70, 36
TABS = [col(i, 1, 420, 456) for i in range(4)]
RAIL = (1608, 459, 1620, 1072)                  # under the tabs, from the picker's top
CHIPS = [(1624 + 56 * i, 459, 1674 + 56 * i, 487) for i in range(5)]   # "all" first, then each factory, 6 apart as the wells
KEY_SIZES = {"Menu": MENU, "Diplomacy": DIPLO, "Sell": SELL, "Repair": REPAIR, "General": GENERAL}
KEY_SIZES = {k: (r[2] - r[0], r[3] - r[1]) for k, r in KEY_SIZES.items()}
CAMEO_W, CAMEO_H = 92, 75                       # 3x7 wells, the cameo 3 inside at 86x69, the art's 5:4
CAMEOS = [(1624 + 98 * c, 491 + 79 * r, 1624 + 98 * c + CAMEO_W, 491 + 79 * r + CAMEO_H) for r in range(7) for c in range(3)]
ARROWS = [(1624, 1044, 1764, 1072), (1772, 1044, 1912, 1072)]
CELLS = [(8 + 64 * i, 1024, 68 + 64 * i, 1072) for i in range(14)]   # the command buttons at the art's 60x48
GAPS = [68 + 64 * i + 2 for i in range(13)]     # the middle of the 4 between two command wells
IDLE = [(1100 + 60 * i, 1030, 1156 + 60 * i, 1066) for i in range(6)]   # not baked: the row moves with the screen
CLOCK = (1500, 1024, 1596, 1072)
PLATE_BAND = (1040, 1044)                       # the Plate's rows drawPlateRun repeats (SIDEBAR_PLATE_HEAD/TAIL)
PLATE_FLAT = (1036, 1048)                       # made uniform: the band and a block each side, whole DXT blocks on the sheet
STRIP_BAND = (904, 1496)                        # the Strip's columns it repeats (STRIP_PLATE_HEAD/TAIL)
# The card (the owner's redesign of 2026-11-15): welded to the strip, its left the screen's edge and its foot the
# strip's top, the selected units in a row standing on the strip, the upgrades in a column up from its top left
# The card is V1, the medallion (the owner's pick of 2026-11-15, experiments/proofs/2026-11-15/hud-bottomleft-variants): a
# portrait medallion standing proud at the screen's edge, a label plate (name, health) and a tray of six tiles beside
# it, all one plate welded to the strip.  The tray holds the single unit's upgrades, the selected types or the queue.
CARD = (0, 896, 470, 1016)
MEDALLION = (0, 896, 136, 1016)
PORTRAIT = (8, 904, 124, 994)
RANK = (10, 998, 122, 1012)                     # the rank chevrons' recess under the portrait
LABEL = (136, 932, 470, 1016)
NAME = (146, 935, 420, 953)                     # the name, on the plate (no glass)
HEALTH = (146, 956, 462, 962)                   # the health groove
TRAY = (140, 966, 466, 1016)
TILES = [(146 + 53 * i, 970, 196 + 53 * i, 1012) for i in range(6)]
TILE_INSET = 2                                  # the tile's button 46x38 inside its well
SW_W, SW_H = 288, 40                            # a superweapon timer row (40 high since 2026-11-15: 28 was unreadable)
SW_ROWS = [(1304, 968 - 44 * n, 1592, 1008 - 44 * n) for n in range(3)]
SW_ICON, SW_TEXT, SW_GROOVE, SW_LAMP = (4, 4, 46, 36), (52, 4, 270, 26), (52, 29, 270, 35), (274, 14, 284, 26)   # in the row
CAMEO_INSET = 3                                 # the cameo drawn this far inside its well, so the well's bezel shows
MONEY_SIGN_X, MONEY_TEXT_X = MONEY[0] + 12, MONEY[0] + 24   # the painted "$"'s middle, the readout's left (1698, 1710)
# The general's powers wing (ControlBar::arrangePowersWing): against the plate's left edge from the radar's top, the
# Joint at x 1592..1600 on the seam, cells of POWERS_CW x POWERS_CH growing left and down under a Top cap (8 high) from
# y 56, a Bottom foot under the last row, the Edge left of the outermost column (8 wide)
POWERS_AT, POWERS_CW, POWERS_CH, POWERS_RIM = (1600, 56), 78, 64, 8
POWERS_WELL = (4, 4, 74, 60)                    # the button's well in a cell: 70x56, 4 in

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
FLOOR = (1606, 418, 1914, 1076)                 # the production well: tabs, rail, picker, cameos, arrows, 2 round the
                                                # tab row; plain under the grid, where the code lays CameoWell under
                                                # every cell
GAP_X = 1768                                    # the gap between the two arrows


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
    for x in range(1630, 1912, 16):
        o.rivet(x, 414.5, 1.3)
    o.bump([regular(x, 411, 2.2, 12) for x in (1700, 1842)], 2.0, 1.3, GOLDM)
    o.bump([regular(GAP_X, 1058, 2.6, 12)], 2.2, 1.4, GOLDM)                          # a gold stud between the arrows
    for y in range(66, 356, 18):
        o.rivet(1916.5, y, 1.2)


def sb_gla(o, p):
    """Welded scrap: a ragged world edge with a bite out of it, two crooked patch sheets welded over the gap between
    the radar and production, hazard slashes along the left patch's top edge only, odd bolts, scrap strips welded
    round the radar, one scrap tray under the cameos."""
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
    # one scrap tray under the cameos, its top and foot bitten, its sides straight down so the rows the code repeats
    # (PLATE_BAND) are the same
    tray = [(FLOOR[0], 1076), (FLOOR[0] - 0.6, 418.6), (FLOOR[2] + 1, 417.6), (FLOOR[2], 1075.6)]
    o.recess([rag(tray, 50, 1.0, 20, lambda a, b: abs(a[0] - b[0]) < 2)], CLOSED, 3.0, 1.0)
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
    for i, (y, r) in enumerate(((122, 1.8), (291, 2.4))):
        o.bolt(1917, y, r, seed=50 + i)


def sb_turkiye(o, p):
    """Modern defence armour: wide 30 degree chamfers, an armour ledge with panel gaps and recessed fasteners, an
    optic housing at its end, range ticks round the radar, one faint girih etch, turquoise LED lines (sidebar_lights)
    and a faint pixel camouflage."""
    col = [(1600, 0), (1920, 0), (1920, 1080), (1600, 1080), (1600, 418), (1606, 408), (1606, 404), (1600, 400),
           (1600, 52), (1605, 44), (1600, 40)]
    o.tiers([col], OPEN_COL, *TIERS["Turkiye"])
    o.tiers([[(1600, 0), (1920, 0), (1920, 50), (1614, 50), (1600, 42)]], (True, False, False, True), 6, 9, 4.0, 3.5)   # an armour header
    o.bump([shape("faceted", *grow(RADAR, 5), 6)], 2.4, 1.6, STEEL)
    radar_glass(o)
    sidebar_common(o, "faceted", 4)
    o.inlay([plate(1915, y, 1917 if i % 5 else 1918.5, y + 1) for i, y in enumerate(range(62, 358, 8))], INKM, 0.6)
    o.tiers([[(1600, 418), (1600, 413), (1612, 405), (1920, 405), (1920, 418)]], (False, False, False, True), 9, 12, 3.4, 1.8)
    o.inlay([plate(x, 406, x + 1, 418) for x in (1690, 1800, 1872)], INKM, 0.9)         # the armour panels' gaps
    for x in (1618, 1700, 1792, 1864):
        fastener(o, x, 411, 2.2)
    o.tiers([plate(1880, 403, 1912, 419, tl=("cut", 3), tr=("cut", 5))], CLOSED, 5, 8, 2.2, 1.2, 1.6)     # the optic
    o.sink([regular(1902, 411, 3.6, 16)], 3, 1.0)
    o.inlay([star8(x, 411, 3.0) for x in range(1716, 1790, 12)], DARK, 0.25)              # the one girih etch
    o.inlay([plate(x, 50.5, x + 1, 52.5 if i % 5 else 53.5) for i, x in enumerate(range(1626, 1900, 8))], INKM, 0.6)
    fastener(o, 1768, 1058)
    camo(p)


def sidebar_lights(img):
    S = lambda x, y: (x - SB + PL, y)         # noqa: E731
    lines = [(S(1622, 415.5), S(1688, 415.5)), (S(1702, 415.5), S(1790, 415.5)), (S(1616, 49.5), S(1906, 49.5)),
             (S(1612, 365), S(1626, 365)), (S(1898, 365), S(1912, 365))]
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
    top = POWERS_AT[1] + POWERS_RIM               # the wing's first row
    o.recess([plate(1594, POWERS_AT[1] + 2, 1605.5, top + 4 * POWERS_CH - 2)], CLOSED, 2.4, 1.0)
    for r in range(4):
        mount(o, build, 1602.6, top + POWERS_CH * r + POWERS_CH / 2, 1.6)


def band(img, axis, lo, hi):
    """The run [lo, hi) along axis (0 rows, 1 columns) made exactly uniform along it, each line the run's mean, so
    drawPlateRun can repeat it and cut its last copy anywhere with no seam."""
    a = np.asarray(img, np.float32).copy()
    sl = [slice(None)] * 2
    sl[axis] = slice(lo, hi)
    a[tuple(sl)] = a[tuple(sl)].mean(axis=axis, keepdims=True)
    return Image.fromarray((a + 0.5).astype(np.uint8))


def is_band(img, axis, lo, hi):
    a = np.asarray(img)
    run = a[lo:hi] if axis == 0 else a[:, lo:hi]
    first = run[:1] if axis == 0 else run[:, :1]
    return bool((run == first).all())


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
    img = band(img, 0, *PLATE_FLAT)
    return money_sign(img, MONEY_SIGN_X - SB, (MONEY[1] + MONEY[3]) / 2)


# ---------------------------------------------------------------------------------------------- the strip plate

PT = 8


# The strip's band (STRIP_BAND) repeats along a long strip and is cut anywhere against the tail, so nothing but plate
# stands in it: every fitting is in the head, over the command wells' gaps and in the margin under them, or in the tail.
# STRIP_FLAT, a little wider than the band, is made uniform, so the head's last columns and the tail's first meet the
# band's copies as they meet each other; each side's cut in the top edge frames the band from just outside it.
STRIP_FLAT = (902, 1498)
TRAY_Y = (1021, 1075)                           # the strip's floor, the third tier, the whole strip long under the wells
FOOT_Y = 1077.5                                 # the margin under the floor, where the fittings stand


def strip_common(o, build, c, tray=None):
    """The floor along the whole strip, open at both ends (the area's edge, the sidebar), the command wells and the
    clock's glass sunk in it, so the band between them is shell, lip and floor like the rest."""
    o.recess([tray or plate(-8, TRAY_Y[0], 1608, TRAY_Y[1])], (False, False, True, True), 3.0, 2.0)
    for r in CELLS:
        glass(o, r, build, c * 0.6, 1, 5, 1.6)
    glass(o, CLOCK, build, c * 0.6)


def st_america(o, p):
    """Aerospace: a 45 degree V cut each side of the band, a blue hairline along the top, octagon studs under every
    other gap."""
    top = [(0, 1016), (889, 1016), (892, 1019), (895, 1016), (1509, 1016), (1512, 1019), (1515, 1016),
           (1600, 1016), (1600, 1080), (0, 1080)]
    o.tiers([welded_top(top)], OPEN_STRIP, 4, 7, 3, 2.6)
    strip_common(o, "machined", 6)
    o.inlay([plate(CARD[2] + 6, 1016.6, 886, 1017.6), plate(898, 1016.6, 1506, 1017.6), plate(1518, 1016.6, 1592, 1017.6)],
            ACCENT, 0.4)                                                             # a blue hairline along the top
    for i in range(1, 13, 2):
        o.stud(GAPS[i], FOOT_Y, 1.4)


def st_china(o, p):
    """A pagoda: a notch in two square steps each side of the band, red lacquer along the top, the floor in two
    terraces, a row of rivets under the wells, gold studs."""
    def notch(x):
        return [(x, 1016), (x, 1017.5), (x + 3, 1017.5), (x + 3, 1019), (x + 13, 1019), (x + 13, 1017.5), (x + 16, 1017.5),
                (x + 16, 1016)]
    top = [(0, 1016)] + notch(880) + notch(1504) + [(1600, 1016), (1600, 1080), (0, 1080)]
    o.tiers([welded_top(top)], OPEN_STRIP, 4, 9, 4, 0.9)
    o.recess([plate(-8, TRAY_Y[0] - 1, 1608, TRAY_Y[1] + 1)], (False, False, True, True), 1.6, 0.8)   # the upper terrace
    strip_common(o, "terraced", 5, plate(-8, TRAY_Y[0] + 1, 1608, TRAY_Y[1] - 1))
    o.inlay([plate(CARD[2] + 6, 1016.2, 878, 1017.8), plate(898, 1016.2, 1502, 1017.8), plate(1522, 1016.2, 1600, 1017.8)],
            ACCENT, 0.3)                                                             # red lacquer along the top
    gold = (GAPS[3], GAPS[9])
    for x in [38 + 64 * i for i in range(14)] + GAPS:
        if x not in gold:
            o.rivet(x, FOOT_Y, 1.2)
    o.bump([regular(x, FOOT_Y, 2.0, 12) for x in gold] + [regular(3.6, 1048, 2.0, 12)], 2.2, 1.3, GOLDM)


def st_gla(o, p):
    """Welded scrap: a top edge bitten in the head and the tail (straight along the band), hazard slashes along one
    stretch of it, beads welded down two gaps, crooked patches welded on the margin, odd bolts."""
    top = [(0, 1017), (300, 1016.5), (620, 1017.6), (878, 1017), (884, 1018.8), (896, 1018.6), (900, 1017), (902, 1017),
           (1498, 1017), (1504, 1017.3), (1508, 1019), (1521, 1018.6), (1526, 1016.6), (1600, 1017), (1600, 1080), (0, 1080)]
    in_band = lambda a, b: a[1] == b[1] and min(a[0], b[0]) >= STRIP_FLAT[0] and max(a[0], b[0]) <= STRIP_FLAT[1]   # noqa: E731
    border = lambda a, b: a[0] == b[0] or a[1] == b[1] == 1080 or in_band(a, b)      # noqa: E731
    o.tiers([rag(welded_top(top), 9, 1.2, 14, border)], OPEN_STRIP, 4, 7, 2.4, 0.9)
    (y0, y1), (f0, f1) = TRAY_Y, STRIP_FLAT
    tray = [(-8, y0), (f0, y0), (f1, y0), (1608, y0), (1608, y1), (f1, y1), (f0, y1), (-8, y1)]
    strip_common(o, "scavenged", 5, rag(tray, 19, 1.0, 18, border))                  # its edges bitten off the band
    hazard(o, 480, 620, 1018.4, 3.0, 8)
    for i in (3, 7):
        o.weld((GAPS[i], 1026), (GAPS[i], 1062), 140 + i, 1.0)
    for i, (g, deg) in enumerate(((4, -2.2), (11, 1.8))):                           # scrap welded on the margin crooked
        q = sheet(GAPS[g] - 11, 1074.4, GAPS[g] + 13, 1079.6, deg)
        o.bump([q], 1.6, 0.6, STEEL)
        welded(o, q[:2], 150 + i)
    for i, (x, y, r) in enumerate(((3.4, 1040, 1.8), (GAPS[1], FOOT_Y, 2.0), (GAPS[6], 1076, 1.6), (GAPS[9], 1077, 2.2),
                                   (GAPS[4] + 8, 1076.6, 1.4))):
        o.bolt(x, y, r, seed=10 + i)


def st_turkiye(o, p):
    """Modern armour: a 30 degree chamfered notch each side of the band, range ticks down every gap, recessed
    fasteners under the wells, the LED line along the top (strip_plate), the camouflage."""
    top = [(0, 1016), (881, 1016), (886.2, 1019), (891.8, 1019), (897, 1016), (1507, 1016), (1512.2, 1019), (1517.8, 1019),
           (1523, 1016), (1600, 1016), (1600, 1080), (0, 1080)]
    o.tiers([welded_top(top)], OPEN_STRIP, 4, 7, 4, 3.5)
    strip_common(o, "faceted", 4)
    o.inlay([plate(x - 0.5, y, x + 0.5, y + (1 if (y - 1030) % 18 else 2)) for x in GAPS for y in range(1030, 1068, 6)],
            INKM, 0.6)                                                               # range ticks down every gap
    for i in (2, 6, 10):
        fastener(o, GAPS[i], FOOT_Y, 1.6)
    o.inlay([plate(x, 1076, x + 1, 1077.6 if i % 5 else 1079.2) for i, x in enumerate(range(486, 600, 6))], INKM, 0.6)
    camo(p, 6)


STRIPS = {"America": st_america, "China": st_china, "Gla": st_gla, "Turkiye": st_turkiye}


def strip_plate(side):
    look = look_of(side)
    p = Painter(1600, 64 + PT)
    o = Offset(p, 0, PT - 1016)
    STRIPS[side](o, p)
    img = bar_finish(shade(p, look, shadow=True, falloff=False), p, look)      # no falloff: the card's foot meets it level
    if side == "Turkiye":
        img = led(img, [((CARD[2] + 6, PT + 1), (1594, PT + 1))], [(3.5, PT + 32, TURQ)])
    return band(img.crop((0, PT, 1600, PT + 64)), 1, *STRIP_FLAT)


def welded_top(top):
    """A strip outline whose top runs up past the crop under the card (x 0..CARD's right), so no edge shows where the
    card's foot stands on it: the two read as one plate."""
    return [(0, 1016 - PT), (CARD[2], 1016 - PT), (CARD[2], 1016.5)] + [(x, y) for x, y in top if x > CARD[2] or y > 1040]


# ---------------------------------------------------------------------------------------------- the selection card

CARD_PAD = 24
CARD_LO, CARD_HI = CARD[1] - CARD_PAD, CARD[3] + CARD_PAD      # the canvas's top and foot, in screen pixels
OPEN_CARD = (False, True, True, False)          # the card: its foot (the strip) and its left (the screen) run on


L_, F_ = -CARD_PAD, CARD_HI                     # past the screen's left, past the card's foot


def card_common(o, build, c):
    """What every side's card sinks: the portrait's glass, the rank's recess, the health groove, the tray's floor and
    its six tiles, open at the foot so they stand on the strip."""
    glass(o, PORTRAIT, build, c, 1, 6, 2.0)
    o.recess([plate(*RANK, tl=("cut", 2), tr=("cut", 2))], CLOSED, 1.6, 1.0)
    o.sink([plate(*grow(HEALTH, 1))], 6, 1.0)
    o.recess([shape(build, TRAY[0], TRAY[1], TRAY[2], F_ + 4, 3)], (False, True, False, False), 3, 1.6)
    for r in TILES:
        o.sink([shape(build, r[0], r[1], r[2], F_ + 4, c * 0.5)], 5, 1.4)


def medallion(o, outline, profile=(7, 11, 4, 2.2)):
    o.tiers([outline], OPEN_CARD, *profile)


def cd_america(o, p):
    o.tiers([[(L_, F_), (L_, 896), (124, 896), (136, 908), (136, 932), (446, 932), (470, 956), (470, F_)]], OPEN_CARD, 4, 7, 4, 2.6)
    medallion(o, plate(L_, 896, 136, F_, tr=("cut", 12)))
    card_common(o, "machined", 6)
    o.inlay([plate(146, 934.6, 420, 935.6)], ACCENT, 0.5)                            # the label's blue hairline
    o.inlay([swept(424 + 9 * i, 936, 950, 5, 2.4, 1) for i in range(3)], ACCENT, 0.6)
    o.stud(130, 1008, 1.8)
    o.stud(465, 990, 1.6)


def cd_china(o, p):
    top = [(L_, F_), (L_, 896), (126, 896), (126, 900), (131, 900), (131, 905), (136, 905), (136, 932), (140, 932), (140, 935),
           (440, 935)]
    top += curve((440, 935), (458, 935), (470, 929), 4)[1:] + [(470, 938), (467, 941), (467, F_)]   # the label's eave
    o.tiers([top], OPEN_CARD, 4, 9, 4, 0.9)
    medallion(o, plate(L_, 896, 136, F_, tr=("notch", 5)), (7, 12, 4, 0.9))
    card_common(o, "terraced", 4)
    o.inlay([plate(146, 935.2, 436, 937.0)], ACCENT, 0.3)                            # red lacquer under the eave
    o.bump([regular(x, y, 2.0, 12) for x, y in ((130, 1008), (130, 912))], 2.0, 1.2, GOLDM)
    for y in range(926, 1000, 12):
        o.rivet(130.5, y, 1.0)


def cd_gla(o, p):
    border = lambda a, b: a[0] == b[0] == L_ or a[1] == b[1] == F_      # noqa: E731
    base = [(L_, F_), (L_, 896.5), (70, 895.8), (118, 897), (136, 909), (135.4, 932), (260, 931.6), (452, 933.2), (470, 944),
            (469.6, F_)]
    o.tiers([rag(base, 21, 1.6, 14, border)], OPEN_CARD, 4, 7, 2.4, 0.9)
    medallion(o, rag(plate(L_, 896, 134, F_, tr=("cut", 9)), 33, 1.2, 14, border), (7, 11, 3, 0.9))
    card_common(o, "scavenged", 4)
    hazard(o, 6, 110, 897.6, 3.4, 8)                                                 # the medallion's top edge only
    o.weld((136, 934), (136, 1014), 170, 1.0)                                         # welded to the label
    for i, (x, y, r) in enumerate(((130, 1008, 1.6), (466, 950, 1.8), (465.6, 1004, 1.4))):
        o.bolt(x, y, r, seed=20 + i)


def cd_turkiye(o, p):
    o.tiers([[(L_, F_), (L_, 896), (118, 896), (136, 906), (136, 932), (430, 932), (470, 955), (470, F_)]], OPEN_CARD, 4, 7, 4.4, 3.5)
    medallion(o, plate(L_, 896, 136, F_, tr=("slant", 7)), (7, 11, 5, 3.5))
    card_common(o, "faceted", 4)
    for x, y in ((130, 1008), (465.4, 996)):
        fastener(o, x, y, 1.8)
    o.inlay([plate(x, 963.2, x + 1, 964.4 if i % 5 else 965.4) for i, x in enumerate(range(146, 462, 8))], INKM, 0.6)
    camo(p, 7)


CARDS = {"America": cd_america, "China": cd_china, "Gla": cd_gla, "Turkiye": cd_turkiye}
CARD_W, CARD_H = CARD[2] - CARD[0], CARD[3] - CARD[1]


def card_frame(side):
    look = look_of(side)
    pad = CARD_PAD
    p = Painter(CARD_W + 2 * pad, CARD_H + 2 * pad)
    o = Offset(p, pad - CARD[0], pad - CARD[1])
    CARDS[side](o, p)
    img = bar_finish(shade(p, look, shadow=True, falloff=False), p, look, gloss=False)     # level with the strip
    if side == "Turkiye":
        S = lambda x, y: (x - CARD[0] + pad, y - CARD[1] + pad)   # noqa: E731
        img = led(img, [(S(146, 934.6), S(410, 934.6))], [(*S(130, 912), TURQ)])
    return img.crop((pad, pad, pad + CARD_W, pad + CARD_H))


TILE_LIT = (50, 42)


def tile_lit(side):
    """A tray tile's lit frame for the hovered or selected tile: a ring in the side's open colour in the build's corners
    with a soft glow inward, the middle clear for the tile's picture."""
    build = look_of(side)["build"]
    w, h = TILE_LIT
    big = 8
    m = Image.new("L", (w * big, h * big), 0)
    d = ImageDraw.Draw(m)
    d.polygon([(x * big, y * big) for x, y in shape(build, 0, 0, w, h, 3)], fill=255)
    d.polygon([(x * big, y * big) for x, y in shape(build, 2.2, 2.2, w - 2.2, h - 2.2, 2)], fill=0)
    ring = m.resize((w, h), Image.BOX)
    glow = ring.filter(H.ImageFilter.GaussianBlur(2.2))
    colour = tuple(min(255, int(c * 1.15 + 40)) for c in COMMAND_TAB_OPEN[side])
    out = Image.new("RGBA", (w, h), colour + (0,))
    out.putalpha(Image.fromarray(np.clip(np.asarray(glow, np.float32) * 0.55, 0, 255).astype(np.uint8)))
    top = Image.new("RGBA", (w, h), colour + (255,))
    top.putalpha(ring)
    out.alpha_composite(top)
    return out


# ---------------------------------------------------------------------------------------------- small pieces

def key(side, w, h, state, layers=(), k=1.0, corner=1.0, dx=0.0, dy=0.0, look=None, lit=None):
    """hud_command_bar.key_piece with any number of glyph layers ((polys, fill), in a 22 box round 11, 11) or none,
    for the code's text.  '' up, 'H' hover, 'P' lit (the face in the side's open colour), 'I' disabled.  look and lit
    stand in for the side's look and open colour (hud_direct_strike_theme passes its own palette)."""
    look = look or look_of(side)
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
        lit = np.asarray(lit or COMMAND_TAB_OPEN[side], np.float32)
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
# the glyph stands right of the middle (TAB_GLYPH_AT).
TAB_KINDS = ("Buildings", "Defenses", "Infantry", "Vehicles")
TAB_KEYS = dict(Buildings="Q", Defenses="W", Infantry="E", Vehicles="R")
# The glyph fills 80% of the face's height (the owner, 2026-11-16: at 40% of the tab they read tiny), its box centred
# right of the middle so the key badge the code lays in the top left keeps clear of it; no wider than TAB_GLYPH_W.  The
# badge (ControlBarSidebar.cpp Sidebar_drawKeyBadge) stands 6 in, a pixel inside the face, a square of the key font's
# 15 px line: 6..21 at 1080, so the glyph's box starts past 22.
TAB_FACE = H.COMMAND_TAB_RIM + 3.2              # key()'s face, this far in from each edge of the piece
TAB_GLYPH_H = 0.8 * (TAB_H - 2 * TAB_FACE)
TAB_GLYPH_W = 38.0
TAB_GLYPH_AT = (43.0, TAB_H / 2)                # the glyph box's centre on the 70x36 piece; P is 0.8 right, 0.4 up


def tab_fit(layers):
    """key()'s k, dx, dy that stand the raised parts of `layers` TAB_GLYPH_H high (or TAB_GLYPH_W wide) on
    TAB_GLYPH_AT."""
    pts = np.array([pt for polys, fill in layers if fill for q in polys for pt in q], np.float32)
    (x0, y0), (x1, y1) = pts.min(0), pts.max(0)
    kk = min(TAB_GLYPH_H / (y1 - y0), TAB_GLYPH_W / (x1 - x0))
    k = kk / ((TAB_H - 2 * TAB_FACE) / 14.5)    # key()'s own scale is the face's height / 14.5
    return dict(k=k, dx=TAB_GLYPH_AT[0] - TAB_W / 2 - ((x0 + x1) / 2 - 11) * kk, dy=TAB_GLYPH_AT[1] - TAB_H / 2 - ((y0 + y1) / 2 - 11) * kk)


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
    if kind == "Defenses":               # the W tab, upgrades and powers (the piece keeps its name): a rising arrow
        if build == "machined":          # its head cut at 45, a slot down the shaft
            return [([[(11, 2.5), (19, 10.5), (14, 10.5), (14, 19), (8, 19), (8, 10.5), (3, 10.5)]], R),
                    ([box(9.6, 13, 12.4, 17.4)], I)]
        if build == "terraced":          # a stepped pyramid on a square shaft
            return [([box(9.6, 3, 12.4, 5.4), box(7.2, 5.4, 14.8, 7.8), box(4.8, 7.8, 17.2, 10.2), box(8.4, 10.2, 13.6, 19)], R),
                    ([box(10, 12.4, 12, 17)], I)]
        if build == "scavenged":         # bent out of scrap, a weld across the shaft
            return [([[(10.4, 2.4), (19.6, 9.6), (14.6, 10.6), (15.4, 19), (8.2, 19.4), (8.6, 11), (2.8, 11.4)]], R),
                    ([[(9.6, 14), (13.6, 13.2), (13.7, 14.2), (9.7, 15)]], I)]
        return [([[(11, 2.4), (20, 12), (14.6, 11), (14.6, 19), (7.4, 19), (7.4, 11), (2, 12)]], R),   # a wedge, 30 degree barbs
                ([H.triangle(11, 15.6, 3, 2.6, "up")], I)]
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


# The picker chips' factory icons (the coordinator's order of 2026-11-15: a shrunk building cameo smudged): flat
# white glyphs the code tints, CHIP_ICON at 1080 on the chip's left, each side in its language as the tabs' are.
CHIP_ICON_KINDS = ("Factory", "Airfield", "Barracks", "Generic")
CHIP_ICON = (24, 20)


def chip_glyph(build, kind):
    """(polys, fill) layers in a 22 box round (11, 11)."""
    R, I = 255, 0
    if kind == "Factory":                       # a hall with a sawtooth roof and a wide door
        teeth = {"machined": [(2, 9), (6, 5), (6, 9), (10, 5), (10, 9), (14, 5), (14, 9)],
                 "terraced": [(2, 9), (2, 6), (6, 6), (6, 9), (8, 9), (8, 5), (12, 5), (12, 9), (14, 9), (14, 6)],
                 "scavenged": [(2.4, 9.4), (5.6, 5.2), (6.4, 9), (10.2, 4.6), (10.6, 9.4), (14.6, 5.6), (14.2, 9.2)],
                 "faceted": [(2, 9), (8, 5.2), (8, 9), (14, 5.2), (14, 9)]}[build]
        hall = [(2, 18)] + teeth + [(17, 9), (17, 3.5), (19.5, 3.5), (19.5, 9), (20, 9), (20, 18)]
        return [([hall], R), ([box(6.5, 12, 16.5, 18)], I)]
    if kind == "Airfield":                      # a control tower over a runway
        cab = {"machined": [(10, 7), (10.4, 4.6), (12, 3), (17, 3), (18.6, 4.6), (19, 7)],
               "terraced": [(9.6, 7), (9.6, 5), (11, 5), (11, 3), (18, 3), (18, 5), (19.4, 5), (19.4, 7)],
               "scavenged": [(10.2, 7.4), (11, 3.6), (18.6, 2.8), (19.2, 6.8)],
               "faceted": [(10, 7), (11.6, 3), (19, 3), (19, 7)]}[build]
        return [([cab, box(12.8, 7, 16.2, 16), [(1, 16), (21, 16), (21, 19.4), (1, 19.4)]], R),
                ([box(11.4, 4.4, 17.6, 5.8)] + [box(x, 17.2, x + 2.6, 18.2) for x in (2.6, 7, 11.4, 15.8)], I)]
    if kind == "Barracks":                      # a long hut, a row of windows, a door
        body = {"machined": [(2, 18), (2, 11), (5, 7), (17, 7), (20, 11), (20, 18)],
                "terraced": [(3, 18), (3, 10.4), (1, 10.4), (2.6, 8.6), (6, 8.6), (6, 6.4), (16, 6.4), (16, 8.6), (19.4, 8.6),
                             (21, 10.4), (19, 10.4), (19, 18)],
                "scavenged": [(2.4, 18), (3, 10.6), (11.4, 5.2), (19.6, 10.2), (19.2, 18)],
                "faceted": [(2, 18), (2, 11), (9, 7), (20, 9), (20, 18)]}[build]
        return [([body], R), ([box(9.4, 13, 12.6, 18), box(4.4, 12.4, 7.4, 14.2), box(14.6, 12.4, 17.6, 14.2)], I)]
    n, r_out, r_in = {"machined": (8, 8.6, 6.4), "terraced": (4, 8.6, 6.6), "scavenged": (7, 8.6, 6.2), "faceted": (6, 9, 6)}[build]
    pts = []                                    # any other producer: a cog in the build's teeth
    for i in range(n):
        a = 2 * np.pi * i / n - np.pi / 2
        w = np.pi / n * (0.45 if build != "faceted" else 0.0)
        jig = (0.0 if build != "scavenged" else (0.6 if i % 2 else -0.4))
        if build == "faceted":
            pts += [(11 + r_out * np.cos(a), 11 + r_out * np.sin(a)), (11 + r_in * np.cos(a + np.pi / n), 11 + r_in * np.sin(a + np.pi / n))]
        else:
            for aa, rr in ((a - w, r_out + jig), (a + w, r_out + jig), (a + np.pi / n - w, r_in), (a + np.pi / n + w, r_in)):
                pts.append((11 + rr * np.cos(aa), 11 + rr * np.sin(aa)))
    return [([pts], R), ([regular(11, 11, 2.8, 12)], I)]


def sell_glyph(build):
    """Sell, in a 22 box: America a block dollar sign cut at 45, China one in square steps on a plinth, the GLA a price
    tag hung crooked, Turkiye a hexagon coin with the dollar sign cut through it."""
    R, I = 255, 0

    def dollar(c=0.0):
        bars = [(6, 4.2, 16, 6.8), (6, 4.2, 8.6, 11.2), (6, 9.6, 16, 12.2), (13.4, 9.6, 16, 17.8), (6, 15.2, 16, 17.8)]
        return [H.octagon(*b, c) if c else box(*b) for b in bars] + [box(9.8, 1.8, 12.2, 20.2)]
    if build == "machined":
        return [(dollar(0.9), R)]
    if build == "terraced":                     # the dollar sign in square steps, on a stepped plinth
        return [(dollar() + [box(4.6, 19.4, 17.4, 21), box(6.4, 18.6, 15.6, 19.4)], R)]
    if build == "scavenged":
        tag = rotated([(2.6, 7), (7, 2.6), (19.4, 2.8), (19.6, 19.2), (7.2, 19.4), (2.6, 15)], 11, 11, -7)
        return [([tag], R), ([regular(6.4, 11, 1.6, 8)] + [rotated(q, 11, 11, -7) for q in
                                                            [box(10, 6, 16.6, 7.8), box(10, 10.1, 16.6, 11.9), box(10, 14.2, 16.6, 16)]], I)]
    return [([regular(11, 11, 10, 6, np.pi / 6)], R), ([[(x * 0.62 + 4.2, y * 0.62 + 4.2) for x, y in q] for q in dollar()], I)]


def repair_glyph(build):
    """Repair, a wrench across the key at 45 degrees, its head the build's: an octagon, a stepped square, a crooked
    heptagon, a hexagon; the jaw cut open toward the top left."""
    R, I = 255, 0
    head = {"machined": regular(6, 11, 5.2, 8, np.pi / 8), "terraced": None, "scavenged": regular(6, 11.3, 5.2, 7, 0.3),
            "faceted": regular(6, 11, 5.4, 6)}[build]
    if build == "terraced":
        head = [(1.4, 7.6), (3.4, 7.6), (3.4, 5.8), (8.6, 5.8), (8.6, 7.6), (10.6, 7.6), (10.6, 14.4), (8.6, 14.4), (8.6, 16.2),
                (3.4, 16.2), (3.4, 14.4), (1.4, 14.4)]
    handle = {"machined": [(9, 9.6), (19.4, 9.6), (20.6, 11), (19.4, 12.4), (9, 12.4)],
              "terraced": [(9, 9.4), (17, 9.4), (17, 8.6), (20.6, 8.6), (20.6, 13.4), (17, 13.4), (17, 12.6), (9, 12.6)],
              "scavenged": [(9, 9.8), (20.4, 9.2), (20.8, 12.2), (9, 12.6)],
              "faceted": [(9, 9.6), (18, 9.6), (20.8, 11), (18, 12.4), (9, 12.4)]}[build]
    jaw = [(-1, 9.6), (6.4, 9.6), (6.4, 12.4), (-1, 12.4)]
    turn = -45 if build != "scavenged" else -38
    t = lambda q: rotated(q, 11, 11, turn)     # noqa: E731
    return [([t(head), t(handle)], R), ([t(jaw)], I)]


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
    w, h, o = CAMEO_W, CAMEO_H, 24
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
        p.inlay([plate(o + w / 2 - 10, o + 0.8, o + w / 2 + 10, o + 1.6)], ACCENT, 0.3)
    img = bar_finish(shade(p, look, shadow=False, falloff=False), p, look, gloss=False)
    return img.crop((o, o, o + w, o + h))


def sw_row(side, enemy):
    """A superweapon timer row: the weapon's icon well, the name and clock's glass, a groove for the countdown and a
    lamp socket, the lamp round in the side's colour for one's own, a red warning triangle on slashes for an enemy's."""
    look = look_of(side)
    build = look["build"]
    w, h, o = SW_W, SW_H, 24
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
        d.polygon([((cx - 4.4) * 4, (cy + 3.8) * 4), (cx * 4, (cy - 4.6) * 4), ((cx + 4.4) * 4, (cy + 3.8) * 4)], fill=(222, 44, 34, 255))
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
    CW, CH = POWERS_CW, POWERS_CH
    if build != "scavenged":
        kind, c = {"machined": ("cut", 6), "terraced": ("notch", 4), "faceted": ("slant", 4.5)}[build]
        return plate(e, t, W, b, tl=(kind, c), bl=(kind, c))
    across = [(0, 0), (16, 1.4), (37, 0.5), (58, 1.8), (70, 0.6)]      # the same bites in every cell's width and height
    down = [(0, 0), (14, 1.4), (31, 0.5), (47, 1.8), (56, 0.6)]
    top = [(c0 + CW * r + x, t + d) for r in range(WING_N) for x, d in across]
    foot = [(c0 + CW * r + x, b - d) for r in reversed(range(WING_N)) for x, d in reversed(across)]
    left = [(e + d, c0 + CH * r + y) for r in reversed(range(WING_N)) for y, d in reversed(down)]
    return ([(e, c0 - 0.5), (e + 2.6, t + 3.2)] + top + [(W, t), (W, b)] + foot + [(c0 - 0.4, b - 0.4), (e + 1.2, b - 4.6)]
            + left[:-1])


def powers_wing(side):
    """The general's powers wing drawn whole, three cells by three, lit from the top left with no falloff, and every
    piece cut from its middle so it tiles: Cell, Top, Bottom, Edge, Joint, CornerTop, CornerBottom.  The Joint runs
    down the column's whole height, cap to foot (drawTiledDown), so it is 8 high and its fittings repeat every 8."""
    look = look_of(side)
    build = look["build"]
    CW, CH, R = POWERS_CW, POWERS_CH, POWERS_RIM
    e = t = WING_PAD
    c0 = e + R
    j = c0 + WING_N * CW
    end = j + R
    b = c0 + WING_N * CH + R
    W = end + WING_PAD
    p = Painter(W, b + WING_PAD)
    p.tiers([wing_outline(build, e, t, b, W)], (False, False, False, True), 4, 7, 4, TIERS[side][3])
    cw = {"machined": 5, "terraced": 4, "scavenged": 4, "faceted": 4}[build]
    for r in range(WING_N):
        for k in range(WING_N):
            x, y = c0 + CW * k, c0 + CH * r
            p.sink([shape(build, x + POWERS_WELL[0], y + POWERS_WELL[1], x + POWERS_WELL[2], y + POWERS_WELL[3], cw)], 5, 1.6)
            if build == "terraced":
                for dx, dy in ((2, 2), (CW - 2, 2), (2, CH - 2), (CW - 2, CH - 2)):
                    p.rivet(x + dx, y + dy, 1.1)
            elif build == "scavenged":
                p.bolt(x + CW - 2.4, y + 2.4, 1.2, 7)
    # the joint, cap to foot in one piece so its middle is the same all the way down, its fittings every 8
    if build == "machined":                      # a satin bar with a blue hairline
        p.bump([plate(j + 1, t - 4, end - 1, b + 4)], 2.0, 1.0, STEEL)
        p.inlay([plate(j + 3.5, t - 4, j + 4.5, b + 4)], ACCENT, 0.5)
    elif build == "terraced":                    # red lacquer and a rivet every 8
        p.bump([plate(j + 1, t - 4, end - 1, b + 4)], 2.0, 0.6, STEEL)
        p.inlay([plate(j + 2.4, t - 4, j + 3.6, b + 4)], ACCENT, 0.3)
        for y in range(t, b, R):
            p.rivet(j + 5.4, y + 4, 0.9)
    elif build == "scavenged":                   # a seam welded down it, a bead every 8
        p.bump([plate(j + 1.5, t - 4, end - 1, b + 4)], 1.6, 0.6, STEEL)
        for y in range(t - 8, b + 8, R):
            p.bump([regular(j + 1.4, y + 2.2, 1.1, 7, 0.3), regular(j + 1.0, y + 6.0, 1.0, 7, 1.1)], 1.1, 0.9, DARK)
    else:                                        # a recessed groove for the LED line
        p.bump([plate(j + 1, t - 4, end - 1, b + 4)], 2.2, 2.0, STEEL)
        p.inlay([plate(j + 3, t - 4, j + 5, b + 4)], INKM, 0.8)
    if build == "faceted":
        camo(p, 8)
    img = bar_finish(shade(p, look, shadow=False, falloff=False), p, look, gloss=False)
    if build == "faceted":
        img = led(img, [((j + 4, t - 4), (j + 4, b + 4))], [])
    mx, my = c0 + CW, c0 + CH                    # the middle cell's left and top
    cuts = {"PowersCell": (mx, my, mx + CW, my + CH), "PowersTop": (mx, t, mx + CW, c0), "PowersBottom": (mx, b - R, mx + CW, b),
            "PowersEdge": (e, my, c0, my + CH), "PowersJoint": (j, my, end, my + R),
            "PowersCornerTop": (e, t, c0, c0), "PowersCornerBottom": (e, b - R, c0, b)}
    assert (my - t) % R == 0, "the Joint's cut is off its 8 texel period"
    return {name: img.crop(box) for name, box in cuts.items()}


STATES = ("", "H", "P", "I")


def pieces(side):
    """Every piece the sheets carry, by its name after <Prefix>Sidebar."""
    build = look_of(side)["build"]
    out = {"Plate": sidebar_plate(side), "Strip": strip_plate(side), "Card": card_frame(side), "CameoWell": cameo_well(side),
           "SuperweaponOwn": sw_row(side, False), "SuperweaponEnemy": sw_row(side, True)}
    for kind in TAB_KINDS:
        g = tab_glyph(build, kind)
        fit = tab_fit(g)
        for s in ("", "H", "P", "I"):
            out["Tab" + kind + s] = key(side, TAB_W, TAB_H, s, g, **fit)
        kk = (TAB_H - 2 * TAB_FACE) / 14.5 * fit["k"]          # the glyph alone, white, where the piece bakes it
        out["TabIcon" + kind] = flat_glyph(TAB_W, TAB_H, g, kk, TAB_W / 2 + fit["dx"], TAB_H / 2 + fit["dy"], (255, 255, 255))
    for s in ("", "H", "P", "I"):
        out["ArrowUp" + s] = key(side, 140, 28, s, arrow_layers(build, True), 0.8, 0.8)
        out["ArrowDown" + s] = key(side, 140, 28, s, arrow_layers(build, False), 0.8, 0.8)
    for s in ("", "P"):
        out["ChipWide" + s] = key(side, 66, 28, s, corner=0.8)
        out["Chip" + s] = key(side, 50, 28, s, corner=0.8)
    for s in STATES:
        out["KeyMenu" + s] = key(side, *KEY_SIZES["Menu"], s, icon_layers("Options", build), 0.55)
        out["KeyDiplomacy" + s] = key(side, *KEY_SIZES["Diplomacy"], s, icon_layers("Chat", build), 0.55)
        out["KeySell" + s] = key(side, *KEY_SIZES["Sell"], s, sell_glyph(build), 0.6)
        out["KeyRepair" + s] = key(side, *KEY_SIZES["Repair"], s, repair_glyph(build), 0.74)
        out["KeyGeneral" + s] = key(side, *KEY_SIZES["General"], s, icon_layers("General", build), 0.55)
    out.update(powers_wing(side))
    out["TileLit"] = tile_lit(side)
    for kind in CHIP_ICON_KINDS:
        out["ChipIcon" + kind] = flat_glyph(*CHIP_ICON, chip_glyph(build, kind), 1.08, CHIP_ICON[0] / 2, CHIP_ICON[1] / 2 - 0.4,
                                            (255, 255, 255))
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
    names = [("Plate", (320, 1080)), ("_was Card", (464, 120)), ("_was CameoWell", (140, 86)),
             ("SuperweaponOwn", (SW_W, SW_H)), ("_was SuperweaponEnemy", (288, 28))]    # its old room kept, so nothing moves
    names += [("Tab" + kind + s, (66, 36)) for kind in TAB_KINDS for s in STATES] + [("TabIcon" + kind, (66, 36)) for kind in TAB_KINDS]
    names += [("Arrow" + d + s, (140, 28)) for d in ("Up", "Down") for s in STATES]
    names += [("ChipWide", (66, 28)), ("ChipWideP", (66, 28)), ("Chip", (50, 28)), ("ChipP", (50, 28))]
    # the keys' rooms at their old sizes, so nothing after them moves: sheet_at() stands the narrower ones of 2026-11-17
    # in the top left of their rooms and placed() the wider General
    names += [(("_was " if k == "General" else "") + "Key" + k + s, size)
              for k, size in (("Menu", (72, 32)), ("Diplomacy", (72, 32)), ("Sell", (74, 36)),
                              ("Repair", (74, 36)), ("General", (140, 36))) for s in STATES]
    R = POWERS_RIM                              # appended, so nothing before them moved; the 72 square cell's room kept
    names += [("_was PowersCell", (72, 72)), ("_was PowersJoint", (R, 72)), ("_was PowersEdge", (R, 72)), ("_was PowersTop", (72, R)),
              ("_was PowersBottom", (72, R)), ("PowersCornerTop", (R, R)), ("PowersCornerBottom", (R, R))]
    names += [("ChipIcon" + kind, CHIP_ICON) for kind in CHIP_ICON_KINDS]
    names += [("_was UpgradeCell", (48, 40)), ("_was UpgradeCap", (48, 8)),
              ("_was UpgradeJoin", (48, 4)), ("SuperweaponEnemy", (SW_W, SW_H)), ("Card", (CARD_W, CARD_H))]
    return [("_was " + n, s) if n.startswith(("Tab", "TabIcon")) else (n, s) for n, s in names]   # the 66 wide tabs' room


def placed():
    """The pieces placed by hand into room the packing left free, so nothing packed moves: the 70x36 tabs (18 in the
    old card's room, 1087..1207, the rest under the card), the lit tile frame, the 92x75 cameo well in the old 140x86
    one's room and the 78x64 powers wing's pieces right of the Plate."""
    names = ["Tab" + kind + s for kind in TAB_KINDS for s in STATES] + ["TabIcon" + kind for kind in TAB_KINDS]
    slots = [(2 + (TAB_W + SHEET_GAP) * c, 1087 + (TAB_H + SHEET_GAP) * r) for r in range(3) for c in range(6)]
    slots += [(2 + (TAB_W + SHEET_GAP) * c, 1940) for c in range(2)]
    at = {n: (x, y, x + TAB_W, y + TAB_H) for n, (x, y) in zip(names, slots)}
    at["TileLit"] = (152, 1940, 152 + TILE_LIT[0], 1940 + TILE_LIT[1])
    at["Plate"] = (2, 4, 322, 1084)             # 2 down from its packed place: PLATE_FLAT on whole 4x4 DXT blocks
    at["CameoWell"] = (2, 1212, 2 + CAMEO_W, 1212 + CAMEO_H)
    CW, CH, R, x = POWERS_CW, POWERS_CH, POWERS_RIM, 327
    at["PowersCell"] = (x, 2, x + CW, 2 + CH)
    at["PowersJoint"] = (x + CW + SHEET_GAP, 2, x + CW + SHEET_GAP + R, 2 + R)
    at["PowersEdge"] = (x + CW + 2 * SHEET_GAP + R, 2, x + CW + 2 * SHEET_GAP + 2 * R, 2 + CH)
    at["PowersTop"] = (x, 2 + CH + SHEET_GAP, x + CW, 2 + CH + SHEET_GAP + R)
    at["PowersBottom"] = (x, 2 + CH + 2 * SHEET_GAP + R, x + CW, 2 + CH + 2 * SHEET_GAP + 2 * R)
    # the General key two grid columns wide (148x36, 2026-11-17), in a column under the powers pieces right of the Plate
    gw, gh = KEY_SIZES["General"]
    for i, s in enumerate(STATES):
        at["KeyGeneral" + s] = (x, 97 + (gh + SHEET_GAP) * i, x + gw, 97 + (gh + SHEET_GAP) * i + gh)
    return at


def sheet_at():
    """Every piece's box (x0, y0, x1, y1): the Strip alone on its sheet, the rest packed in shelves down the main one,
    the Plate first, then placed()'s into the room left free.  The same on every side."""
    at = {"Strip": (4, 2, 1604, 66)}            # x 4: STRIP_BAND starts and ends on whole 4x4 DXT blocks
    x, y, shelf = 2, 2, 0
    for name, (w, h) in sheet_names():
        if x + w + 2 > SHEET[0]:
            x, y, shelf = 2, y + shelf + SHEET_GAP, 0
        at[name] = (x, y, x + w, y + h)
        x += w + SHEET_GAP
        shelf = max(shelf, h)
    assert y + shelf + 2 <= SHEET[1], "the pieces overrun the sheet"
    at = {name: box for name, box in at.items() if not name.startswith("_")}     # "_..." is room left free
    for name, box in list(at.items()):          # a key narrower than its room: its top left corner
        if name.startswith("Key"):
            w, h = KEY_SIZES[name[3:].rstrip("HPI")]       # KeySellH -> Sell
            at[name] = (box[0], box[1], box[0] + w, box[1] + h)
    at.update(placed())
    boxes = [(n, b) for n, b in at.items() if n != "Strip"]
    for i, (n, a) in enumerate(boxes):           # one texel of gutter each and 3 clear between
        assert a[2] + 2 <= SHEET[0] and a[3] + 2 <= SHEET[1], n
        for m, b in boxes[i + 1:]:
            assert a[2] + 3 <= b[0] or b[2] + 3 <= a[0] or a[3] + 3 <= b[1] or b[3] + 3 <= a[1], "%s meets %s" % (n, m)
    return at


SHEET_AT = sheet_at()


def texture_name(side, piece):
    return "ReforgedHud%sSidebar%s" % (side, "Strip" if piece == "Strip" else "")


def paint_sheets(side):
    P = pieces(side)
    assert set(P) == set(SHEET_AT), "pieces and places differ: %s" % sorted(set(P) ^ set(SHEET_AT))
    assert is_band(P["Plate"], 0, *PLATE_BAND) and is_band(P["Strip"], 1, *STRIP_BAND), "a band drawPlateRun repeats is not uniform"
    for name, img in P.items():
        if name in SHEET_AT:
            b = SHEET_AT[name]
            assert img.size == (b[2] - b[0], b[3] - b[1]), "%s is %s, its place %s" % (name, img.size, b)
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
