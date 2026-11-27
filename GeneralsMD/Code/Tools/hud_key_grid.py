"""The Key Grid layout's art (Options, Layout = Key Grid), the owner's pick of 2026-11-27: V2.1 with round 4's
silhouettes (the owner's V21 silhouette mockup), drawn with the command bar
generator's painter (hud_command_bar: Painter.tiers, floor, well, button, key_piece, shade, bar_finish) in each side's
bar steel (BAR_LOOKS) and build.  Only the Key Grid layout draws these; every other layout keeps its own art.

The left console stands round the radar's glass, which takes the map's own aspect at today's glass area (322x232 =
74,704 px at 1080, glass_for), so the code builds it from pieces round any glass:

    G = (5, 1064 - gh, 5 + gw, 1064)     the glass, the radar picture exactly (no letterbox)
    bw = gw + 23, btop = G.y0 - 20       the radar box (0, btop, bw, 1080)
    stop = btop - 48, sw = max(bw, 345)  the shelf (0, stop, sw, btop + 2): signal keys and tool keys
    sx0 = bw - 6, sx1 = sx0 + 40         the spine, out of the box's right side; the deck hangs off it

and draws, back to front (compose() is the rule, line for line; every size comes off a piece):

    Shoulder  stretched over (sw - 152, btop - 8, sw + 8, 790), when btop < 790: the body plate between the shelf's
              foot and the money head, seen right of the spine when the box is narrower than the shelf
    Box       a nine-slice over (0, btop - 20, bw + 12, 1080): corners BoxTL 16x60, BoxTR 72x60, BoxBL 16x24,
              BoxBR 72x24 at their size, BoxT and BoxB stretched across, BoxL and BoxR down, BoxC both ways; BoxC is
              the glass, which the radar covers
    Spine     stretched over (sx0 - 8, btop, sx1 + 8, 790), when btop < 790
    Deck      at its size (820x320), its top left at (sx0 - 44, 760): the spine from the head down, the money head
              with its gold "$", the three key rows, the meter, the 18 key sockets and the side's fittings
    ShelfLeft at (0, stop - 8); then ShelfMid stretched over (320, stop - 8, max(320, sw - 40) + 12, stop + 58), under
              ShelfRight at (sw - 40, stop - 8), whose first 12 columns fade in; all three 66 high
    Queue     the production queue's tray standing on the shelf, n >= 1 entries: QueueCap at (0, stop - 82), QueueCell
              at (5 + 80 i, stop - 82) for i < n - 1, QueueLast at (5 + 80 (n - 1), stop - 82); entry i's cameo cell is
              (6 + 80 i, stop - 66, 83 + 80 i, stop - 7)
    Right     at its size (370x310), its bottom right on the screen's: the selection box (RightHUD and the rank bar's
              wells where ControlBar.wnd has them), the mount for minimise and promotion and the body under both

Everything the code writes or lays on goes in the rects of rects(): the cells, money text, the meter's fill, signal
keys (the side's <Prefix>Signal* of its TabPanel sheet), tool keys, the switch, minimise and promotion (the side's
<Prefix>Minimize and General of its Powers sheet).  The sockets are painted in the Deck; KeyEmpty is a socket's face
cut out of it, for an empty key drawn anywhere else, and KeyCount and KeyHealth the tray view's readouts on a key.
Past aspect 1.55 the shelf stands lower than the money head, and past 0.81 (btop + 2 > 742) the switch has no room on
the spine between the shelf's foot and the head (the mock's place; where it goes then is the code's).

Pieces are named <Prefix>KeyGrid<Piece>, Prefix the scheme's TooltipImage (AmericaPro, ChinaPro, GlaPro, TurkiyePro),
on ReforgedHud<Side>KeyGrid (2048x512, DXT5), mapped by MappedImages/HandCreated/ReforgedHud<Side>KeyGrid.ini, which
check_mapped() holds to the sheet; --ini-text writes its text for the master to be rewritten whole from.

    python Tools/hud_key_grid.py --out DIR [--side all|America|China|Gla|Turkiye] [--dds Data/Art/Textures] [--ini-text DIR]
"""
import argparse
import math
import os
import re

import numpy as np
from PIL import Image, ImageDraw, ImageFont

import hud_command_bar as H
from hud_command_bar import (plate, grow, curve, regular, rotated, sheet, jag, T, TOP, STEEL, ACCENT, GOLDM, INKM, DARK,
                             BAR_LOOKS, TURQ, AMBER)

SIDES = ("America", "China", "Gla", "Turkiye")
PREFIX = {"America": "AmericaPro", "China": "ChinaPro", "Gla": "GlaPro", "Turkiye": "TurkiyePro"}
CORNER = {"machined": "cut", "terraced": "notch", "scavenged": "cut", "faceted": "slant"}
FONT_DIR = os.path.join(os.environ.get("WINDIR", "C:/Windows"), "Fonts")

AREA = 322 * 232                                # today's glass at 1080
CW, CH, PX, PY, STAG = 90, 72, 100, 88, (0, 25, 75)     # a key's cell, the pitch, the A and Z rows' stagger
ROWS = ("QWERTY", "ASDFGH", "ZXCVBN")
LIGHT, BAR = (3, 5, 2.5, 1.6), (5, 9, 3, 2.0)   # the body's tiers, and a part's standing a lip higher on it
REF = (239, 313)                                # the reference glass (Tournament Desert, 176x230) the pieces are cut from
TOOLS = ("Options", "Beacon", "Worker", "Chat")
STATES = ("", "H", "P", "I")
SIGNALS = ("Attack", "Defend", "Attention")


def glass_for(aspect):
    """The glass for a map of aspect w/h, at today's area."""
    w = round(math.sqrt(AREA * aspect))
    return w, round(AREA / w)


class Geo:
    """V2.1's geometry round a gw x gh glass, in screen pixels at 1080 (mock_v21.v21)."""

    def __init__(self, gw, gh, sw_min=345):
        self.gw, self.gh = gw, gh
        self.glass = (5, 1064 - gh, 5 + gw, 1064)
        self.bw, self.btop = gw + 23, self.glass[1] - 20
        self.stop, self.sw = self.btop - 48, max(self.bw, sw_min)
        self.sx0, self.sx1 = self.bw - 6, self.bw + 34
        self.head = (self.sx0, 780, self.sx1 + 186, 822)
        self.hx1 = self.head[2]
        self.kx0 = self.sx1 + 8
        self.cells = {ch: (self.kx0 + STAG[r] + c * PX, 830 + r * PY, self.kx0 + STAG[r] + c * PX + CW, 830 + r * PY + CH)
                      for r, row in enumerate(ROWS) for c, ch in enumerate(row)}
        self.rows = [(self.cells[row[0]][0] - 10, self.cells[row[0]][1] - 6, self.cells[row[-1]][2] + 10,
                      self.cells[row[-1]][3] + 6) for row in ROWS]
        (_, self.qt, self.x1q, _), (_, at, self.x1a, _), (_, zt, self.x1z, _) = self.rows
        self.b1, self.b2 = at + 2, zt + 2
        self.money = (self.sx1 + 18, 787, self.sx1 + 170, 815)
        self.meter = (self.sx0 + 15, self.head[3] + 10, self.sx0 + 25, 1066)
        self.keys_at = (6, self.stop + 7)


def rects(gw, gh):
    """Every rect the code lays something into, screen px at 1080 (the contract with ControlBar's Key Grid)."""
    g = Geo(gw, gh)
    s = g.stop
    out = {"Glass": g.glass, "Money": g.money, "MoneyText": (g.money[0] + 25, g.money[1], g.money[2], g.money[3]),
           "Meter": g.meter, "Switch": (g.sx0 + 2, 742, g.sx0 + 38, 776),
           "Minimize": (1829, 802, 1920, 833), "General": (1829, 840, 1920, 871)}
    out.update({"Cell" + ch: r for ch, r in g.cells.items()})
    out.update({"Signal" + k: (11 + 44 * i, s + 10, 51 + 44 * i, s + 40) for i, k in enumerate(SIGNALS)})
    out.update({"Tool" + k: (156 + 46 * i, s + 9, 200 + 46 * i, s + 43) for i, k in enumerate(TOOLS)})
    return out


# ---------------------------------------------------------------------------------------------- shapes (the mock's)

def S(r):
    return (r[0], r[1] - TOP, r[2], r[3] - TOP)


def screen_edge(pt):
    return pt[1] >= 1080 or pt[0] >= 1920 or pt[0] <= 0


def chamfer(poly, c, keep=lambda pt: False):
    """Every convex corner of a clockwise outline cut at 45 degrees by c; corners keep() names stay square."""
    out, n = [], len(poly)
    for i, b in enumerate(poly):
        a, d = poly[i - 1], poly[(i + 1) % n]
        cross = (b[0] - a[0]) * (d[1] - b[1]) - (b[1] - a[1]) * (d[0] - b[0])
        if cross <= 0 or keep(b) or c <= 0:
            out.append(b)
            continue
        for e in (a, d):
            L = np.hypot(e[0] - b[0], e[1] - b[1])
            out.append((b[0] + (e[0] - b[0]) * c / L, b[1] + (e[1] - b[1]) * c / L))
    return out


def slant(poly, k=18, j=10):
    """Turkiye's rake: every convex corner off the screen's edge cut 30 degrees."""
    out, n = [], len(poly)
    for i, b in enumerate(poly):
        a, d = poly[i - 1], poly[(i + 1) % n]
        cross = (b[0] - a[0]) * (d[1] - b[1]) - (b[1] - a[1]) * (d[0] - b[0])
        if cross <= 0 or screen_edge(b):
            out.append(b)
            continue
        for e, kk in ((a, k), (d, j)):
            L = max(np.hypot(e[0] - b[0], e[1] - b[1]), 1e-6)
            kk = min(kk, L * 0.45)
            out.append((b[0] + (e[0] - b[0]) * kk / L, b[1] + (e[1] - b[1]) * kk / L))
    return out


def swept(x, y0, y1, w, t, s):
    ym = (y0 + y1) / 2
    return [(x, y0), (x + s * t, y0), (x + s * (t + w), ym), (x + s * t, y1), (x, y1), (x + s * w, ym)]


def cut4(r, c):
    return plate(*r, tl=("cut", c), tr=("cut", c), br=("cut", c), bl=("cut", c))


def flick(x, y, w=18, out=12, up=12):
    """A pagoda eave's tip at a corner whose top edge runs right into (x, y) and whose side runs down from it."""
    return [(x - w, y)] + curve((x - w, y), (x - 2, y), (x + out, y - up), 5)[1:] + [(x - 1, y + 9), (x, y + 13)]


def fit(p, b, x, y, r=2.4):
    """The build's fitting (y in texels): an octagon stud, a rivet, an odd hex bolt, a recessed fastener."""
    if b == "terraced":
        p.rivet(x, y, r * 0.9)
    elif b == "scavenged":
        p.bolt(x, y, r * 1.1, seed=int(x + y))
    elif b == "faceted":
        p.inlay([regular(x, y, r * 1.1, 12)], DARK, 1.0)
        p.bump([regular(x, y, r * 0.6, 6)], 0.6, 0.6, STEEL)
    else:
        p.stud(x, y, r)


def weld_round(p, sh, seed, step=1):
    for e0, e1 in list(zip(sh, sh[1:] + sh[:1]))[::step]:
        p.weld((e0[0], e0[1] - TOP), (e1[0], e1[1] - TOP), seed=seed + int(e0[0]), r=1.2)


def patch(p, x0, y0, x1, y1, deg, seed):
    """A scrap welded flush onto the body (a bump, not a new silhouette)."""
    sh = jag(sheet(x0, y0, x1, y1, deg), seed, 1.6, 12)
    p.bump([T(sh)], 1.6, 0.8, STEEL)
    weld_round(p, sh, seed * 5)


def crooked(p, x0, y0, x1, y1, deg, seed):
    sh = sheet(x0, y0, x1, y1, deg)
    p.bump([T(sh)], 1.6, 0.8, STEEL)
    weld_round(p, sh, seed)


def eave_roof(p, x0, x1, base, ridge, tip_out=14, tip_up=14, lip=(6, 11, 2.5, 1.8)):
    """A low pagoda eave from x0 to x1, its ends sweeping up into short sharp tips, red tile courses, a gold ridge."""
    left_tip, right_tip = (x0 - tip_out, base - tip_up), (x1 + tip_out, base - tip_up)
    inset = (base - ridge) * 1.1
    poly = curve((x0 + 8, base), (x0 - tip_out * 0.4, base), left_tip, 8)
    poly += curve(left_tip, (x0 + 6, base - tip_up * 0.55), (x0 + inset, ridge), 8)[1:]
    poly += [(x1 - inset, ridge)]
    poly += curve((x1 - inset, ridge), (x1 - 6, base - tip_up * 0.55), right_tip, 8)[1:]
    poly += curve(right_tip, (x1 + tip_out * 0.4, base), (x1 - 8, base), 8)[1:]
    H.stack(p, poly, *lip)
    for y in np.arange(ridge + 8, base - 4, 9):
        p.inlay([T(plate(x0 + (base - y) * 0.6, y, x1 - (base - y) * 0.6, y + 2))], ACCENT, 0.35)
    p.bump([T(plate(x0 + inset - 4, ridge - 3, x1 - inset + 4, ridge + 3))], 2.2, 1.2, GOLDM)
    p.bump([T(regular(x, y, 3.2, 12)) for x, y in (left_tip, right_tip)], 2.0, 1.4, GOLDM)


def blade(p, x, y, out=10, h=7):
    """China's short blade out of an edge, pointing at the battlefield."""
    s = 1 if out > 0 else -1
    H.stack(p, [(x - 4 * s, y - h), (x + out, y + 2), (x - 4 * s, y + h)], 4, 7, 1.5, 1.2)


def line(p, a, c, mat=INKM, w=0.7, depth=1.0):
    L = np.hypot(c[0] - a[0], c[1] - a[1])
    deg = np.degrees(np.arctan2(c[1] - a[1], c[0] - a[0])) - 90
    p.inlay([T(rotated(plate(a[0] - w, a[1], a[0] + w, a[1] + L), a[0], a[1], deg))], mat, depth)


# ---------------------------------------------------------------------------------------------- the left console's parts

def body_outline(g, b):
    """One body under everything, its right edge stepping with the rows (mock_sil.v21s)."""
    sw, stop, hx1, head = g.sw, g.stop, g.hx1, g.head
    if b == "terraced":
        body = [(0, stop), (sw, stop), (sw, head[1] + 4)] + flick(hx1, head[1] + 4)
        body += [(hx1, g.qt)] + flick(g.x1q, g.qt) + [(g.x1q, g.b1)] + flick(g.x1a, g.b1) + [(g.x1a, g.b2)] + flick(g.x1z, g.b2)
        return body + [(g.x1z, 1080), (0, 1080)]
    body = [(0, stop), (sw, stop), (sw, head[1]), (hx1, head[1]), (hx1, g.qt), (g.x1q, g.qt), (g.x1q, g.b1), (g.x1a, g.b1),
            (g.x1a, g.b2), (g.x1z, g.b2), (g.x1z, 1080), (0, 1080)]
    if b == "machined":
        return chamfer(body, 8, screen_edge)
    if b == "faceted":
        return slant(body, 44, 26)
    body = [(x + (6 if i % 2 else -3), y + (8 if i % 3 == 1 else -6 if i % 3 == 2 else 0)) if 0 < x < 1920 and y < 1080
            else (x, y) for i, (x, y) in enumerate(body)]
    return jag(body, 3, 3.4, 11)


def paint_box(p, g, b):
    bw, btop = g.bw, g.btop
    if b == "machined":
        box = plate(0, btop, bw, 1080, tr=("cut", 28))
    elif b == "terraced":
        box = [(0, btop), (bw - 24, btop)] + curve((bw - 24, btop), (bw - 6, btop), (bw + 6, btop - 14), 5)[1:]
        box += [(bw - 6, btop + 8), (bw - 6, btop + 18), (bw, btop + 18), (bw, 1080), (0, 1080)]
    elif b == "scavenged":
        box = jag([(0, btop + 10), (bw * 0.42, btop - 4), (bw * 0.44, btop + 6), (bw - 30, btop + 2), (bw + 4, btop + 34),
                   (bw, 1080), (0, 1080)], 5, 3.0, 12)
    else:
        box = [(0, btop), (bw - 60, btop), (bw, btop + 35), (bw, 1080), (0, 1080)]
    H.stack(p, box, *BAR)
    gl = g.glass
    H.floor(p, plate(0, gl[1] - 5, gl[2] + 5, 1080, tr=(CORNER[b], 6)))
    H.well(p, plate(*grow(gl, 2)), 5, 2)


def paint_spine(p, g, b):
    sx0, sx1, stop = g.sx0, g.sx1, g.stop
    if b == "machined":
        H.stack(p, plate(sx0, stop + 24, sx1, 1080, tr=("cut", 8)), 5, 8, 3, 1.8)
    elif b == "terraced":
        H.stack(p, plate(sx0 - 5, 1000, sx1 + 2, 1080, tl=("notch", 5)), 5, 7, 3, 1.8)       # a wider foot terrace
        H.stack(p, plate(sx0, stop + 24, sx1, 1080, tl=("notch", 4), tr=("notch", 4)), 5, 8, 3, 1.8)
        for y in (g.b1, g.b2):                                              # an eave where each row hangs on
            H.stack(p, [(sx0 + 10, y - 2), (sx0 + 10, y + 6), (sx0 - 6, y + 6)]
                    + curve((sx0 - 6, y + 6), (sx0 - 12, y), (sx0 - 18, y - 12), 4)[1:] + [(sx0 - 4, y - 4)], 5, 9, 2, 1.6)
    elif b == "scavenged":
        H.stack(p, jag(sheet(sx0 - 2, stop + 26, sx1 - 9, 1084, -0.8), 9, 1.6, 14), 5, 8, 3, 1.8)
        H.stack(p, jag(plate(sx0 + 7, 930, sx1 + 3, 1080), 10, 1.6, 14), 5, 9, 3, 1.8)
        p.weld((sx0 + 7, 930 - TOP), (sx1 - 9, 930 - TOP), seed=77, r=1.2)
    else:
        H.stack(p, [(sx0 + 10, stop + 24), (sx1, stop + 44), (sx1, 1080), (sx0, 1080)], 5, 8, 3, 1.8)


def paint_head(p, g, b):
    sx0, sx1, hx1, head = g.sx0, g.sx1, g.hx1, g.head
    if b == "machined":
        H.stack(p, [(sx0, head[1]), (hx1 - 8, head[1]), (hx1 + 18, (head[1] + head[3]) / 2), (hx1 - 8, head[3]), (sx0, head[3])],
                5, 8, 3, 1.8)
    elif b == "terraced":
        H.stack(p, [(sx0, head[1] + 6)] + flick(hx1, head[1] + 6)[1:] + [(hx1, head[3]), (sx0, head[3])], 5, 8, 3, 1.8)
        H.stack(p, [(sx1 + 30, head[1] + 7)] + curve((sx1 + 12, head[1] - 4), (sx1 + 20, head[1] - 2), (sx1 + 30, head[1] - 2), 3)
                + [(hx1 - 40, head[1] - 2)] + curve((hx1 - 40, head[1] - 2), (hx1 - 30, head[1] - 2), (hx1 - 22, head[1] - 4), 3)
                + [(hx1 - 30, head[1] + 7)], 5, 10, 2, 1.6)                 # the tab's second step, tipped at both ends
    elif b == "scavenged":
        sh = jag(sheet(sx0, head[1] - 1, hx1, head[3] + 1, -2.2), 12, 1.8, 14)
        H.stack(p, sh, 5, 8, 3, 1.8)
        p.weld((sh[0][0], sh[0][1] - TOP), (sh[1][0], sh[1][1] - TOP), seed=55, r=1.2)
    else:
        H.stack(p, [(sx0, head[1]), (hx1 - 14, head[1]), (hx1 + 50, head[3]), (sx0, head[3])], 5, 8, 3, 1.8)


def paint_shelf(p, g, b):
    """The shelf and everything on it (no tool keys: those are the code's windows)."""
    sw, stop, btop = g.sw, g.stop, g.btop
    if b == "machined":
        shelf = [(0, stop), (sw - 8, stop), (sw + 24, (stop + btop) / 2), (sw - 8, btop + 2), (0, btop + 2)]
    elif b == "terraced":
        shelf = [(0, stop), (sw - 22, stop)] + curve((sw - 22, stop), (sw - 2, stop + 2), (sw + 16, stop + 1), 5)[1:]
        shelf += [(sw - 2, stop + 12), (sw, stop + 16), (sw, btop + 2), (0, btop + 2)]
    elif b == "scavenged":
        shelf = jag([(0, stop + 6), (60, stop + 4), (64, stop), (148, stop + 1), (152, stop + 7), (250, stop + 3), (sw + 8, stop + 12),
                     (sw - 4, btop + 2), (0, btop + 2)], 7, 2.0, 13)
    else:
        shelf = [(0, stop), (sw - 10, stop), (sw + 72, btop + 2), (0, btop + 2)]
    H.stack(p, shelf, 6, 9, 3, 1.8)
    k = g.keys_at
    H.well(p, cut4((k[0] - 2, k[1] - 2, k[0] + 142, k[1] + 39), 4), 4, 1.6)
    if b == "machined":
        p.inlay([T(plate(12, stop + 3, sw - 11, stop + 4))], ACCENT, 0.5)
    elif b == "terraced":                       # a red lacquer band, a gold stud and a row of rivets along the foot
        p.inlay([T(plate(10, stop + 40, sw - 10, stop + 43))], ACCENT, 0.3)
        p.bump([regular(sw - 14, stop + 25 - TOP, 2.4, 12)], 2.2, 1.4, GOLDM)
        for x in range(20, 300, 26):
            p.rivet(x, stop + 47 - TOP, 1.8)
    elif b == "scavenged":                      # crooked scraps welded on
        crooked(p, 150, stop + 8, 236, stop + 40, -4, 91)
        patch(p, sw - 38, btop - 10, sw - 4, btop + 2, -4, 42)


def paint_deck(p, g, b):
    """The spine from the head down, the money head, the three rows and all that is fixed to them."""
    sx0, sx1, head, hx1, cells = g.sx0, g.sx1, g.head, g.hx1, g.cells
    H.stack(p, body_outline(g, b), *LIGHT)
    p.cut([T(plate(-4, 500, sx0 + 2, 1084)), T(plate(-4, 500, g.sw + 12, 779))])      # the shoulder's and the box's
    paint_spine(p, g, b)
    p.cut([T(plate(sx0 - 24, 500, sx1 + 12, 778))])                                   # the Spine piece's rows
    paint_head(p, g, b)
    if b == "terraced":                         # a low eave along the deck's top edge and over the head, tipped
        eave_roof(p, hx1 + 10, g.x1q - 12, g.qt + 2, g.qt - 14)
        eave_roof(p, sx0 + 8, hx1 - 10, head[1] + 10, head[1] - 4)
    elif b == "scavenged":
        patch(p, sx0 - 6, 900, sx1 + 6, 950, 6, 41)
        patch(p, g.x1q - 70, g.qt + 2, g.x1q - 8, g.qt + 14, 3, 43)
        patch(p, g.x1z - 66, 1060, g.x1z - 6, 1076, -3, 44)
    elif b == "faceted":                        # a glacis no taller than the head, raked at both ends
        x1q, qt = g.x1q, g.qt
        H.stack(p, [(sx0, qt + 2), (sx0, head[1] + 8), (sx0 + 26, head[1] - 4), (x1q - 90, head[1] - 4), (x1q + 30, qt - 10),
                    (x1q + 44, qt + 2)], 6, 11, 4, 2.4)
        for a, c in (((sx0 + 26, head[1] - 4), (sx0 + 60, qt)), ((x1q - 90, head[1] - 4), (x1q - 50, qt))):
            line(p, a, c)
    for r, (x0, y0, x1, y1) in enumerate(g.rows):
        last = r == 2
        y1 = 1080 if last else y1
        if b == "machined":
            bar = plate(x0, y0, x1, y1, tr=("cut", 12), br=("none" if last else "cut", 12))
        elif b == "terraced":
            bar = [(x0, y0 + 6), (x0 + 6, y0 + 6), (x0 + 6, y0)] + flick(x1, y0) + [(x1, y1 - (0 if last else 6)),
                                                                                    (x1 - (0 if last else 6), y1), (x0, y1)]
        elif b == "scavenged":
            skew = (-7, 6, -4)[r]
            bar = jag([(x0, y0 + max(skew, 0)), (x1 - 6, y0 - min(skew, 0)), (x1 + 3, y0 + 9), (x1, y1), (x0, y1)], 11 + r, 2.0, 14)
        else:
            bar = [(x0, y0), (x1 - 8, y0), (x1 + 24, y0 + 30), (x1 + 24, y1), (x0, y1)] if last else \
                [(x0, y0), (x1 - 8, y0), (x1 + 30, y1), (x0 + 24, y1), (x0, y1)]
        H.stack(p, bar, *BAR)
        row = ROWS[r]
        H.floor(p, plate(cells[row[0]][0] - 5, cells[row[0]][1] - 2, cells[row[-1]][2] + 5, 1080 if last else cells[row[-1]][3] + 2,
                         tl=(CORNER[b], 6), tr=(CORNER[b], 6)))
        if b != "terraced":
            for yy in (y0 + 14, y1 - 14):                                   # bolted to the spine
                fit(p, b, sx1 - 7, yy - TOP, 2.2)
        fit(p, b, cells[row[-1]][2] + 5, cells[row[0]][1] + 36 - TOP, 2.2)
    if b == "terraced":
        for x, y in ((g.x1q, (g.qt + g.b1) / 2 + 6), (g.x1a, (g.b1 + g.b2) / 2 + 6), (g.x1z, (g.b2 + 1080) / 2 + 6)):
            blade(p, x, y)
    if b == "scavenged":
        crooked(p, g.x1z - 70, g.b2 + 30, g.x1z + 4, g.b2 + 60, 5, 98)
    for r in cells.values():                                                # the sockets
        H.well(p, cut4(grow(r, 2), 6), 6, 2.5)
    H.well(p, cut4(grow(g.money, 1), 6), 6, 2)
    m = g.meter
    H.well(p, plate(*grow(m, 2)), 5, 1.6)
    for y in range(m[1] + 20, m[3], 20):                                   # the meter's ticks
        p.inlay([T(plate(sx0 + 29, y, sx0 + 33, y + 1))], INKM, 0.6)
    if b == "machined":
        p.inlay([T(plate(sx1 + 10, head[1] + 3, sx1 + 176, head[1] + 4))], ACCENT, 0.5)
        for i in range(3):                                                  # chevrons swept toward the battlefield
            p.inlay([T(swept(g.x1z - 40 + 11 * i, 1050, 1066, 7, 3.2, 1))], ACCENT, 0.7)
    elif b == "terraced":
        p.inlay([T(plate(sx0 + 4, head[3] - 6, head[2] - 12, head[3] - 3))], ACCENT, 0.3)
    elif b == "scavenged":
        crooked(p, sx0 - 30, 1020, sx0 + 30, 1050, -6, 31)
        crooked(p, head[2] - 60, 782, head[2] - 10, 800, 4, 38)
    else:                                                                   # range ticks under the Z row
        x1 = cells["N"][2] + 6
        p.inlay([T(plate(x, 1070, x + 1, 1074 if i % 5 else 1077)) for i, x in enumerate(range(cells["Z"][0], x1, 8))], INKM, 0.6)


def deck_lights(img, g, ox, oy):
    """Turkiye's turquoise LED lines along the money head and the glacis' edges, its lamps."""
    x1q = g.x1q
    L = lambda pt: (pt[0] - ox, pt[1] - oy)     # noqa: E731
    lines = [((g.sx1 + 12, 818), (g.sx1 + 176, 818)), ((g.sx0 + 30, g.head[1] - 2), (x1q - 92, g.head[1] - 2))]
    lines = [(L(a), L(c)) for a, c in lines]
    return H.led(img, lines, [L((g.sx0 + 20, 1072)) + (TURQ,), L((x1q + 40, 812)) + (TURQ,)])


def paint_right(p, b):
    """The selection box, the mount and the body under both (mock_sil.v21s' right side)."""
    if b == "machined":
        rbody = chamfer([(1610, 870), (1626, 854), (1802, 854), (1802, 788), (1920, 788), (1920, 1080), (1610, 1080)], 10, screen_edge)
        rbox, mount = plate(1626, 863, 1920, 1080, tl=("cut", 36)), plate(1816, 794, 1920, 878, tl=("cut", 20))
    elif b == "terraced":
        rbody = [(1610, 1080), (1610, 872)] + curve((1596, 852), (1612, 860), (1626, 856), 4) + [(1800, 856), (1800, 806)]
        rbody += curve((1786, 784), (1800, 794), (1814, 790), 4) + [(1920, 790), (1920, 1080)]
        rbox = [(1626, 1080), (1626, 884)] + curve((1612, 860), (1626, 870), (1640, 866), 4) + [(1650, 863), (1920, 863), (1920, 1080)]
        mount = [(1816, 878), (1816, 812)] + curve((1802, 788), (1816, 798), (1830, 796), 4) + [(1920, 794), (1920, 878)]
    elif b == "scavenged":
        rbody = jag([(1610, 1080), (1612, 870), (1690, 858), (1798, 862), (1800, 792), (1870, 786), (1920, 790), (1920, 1080)], 21, 2.4, 13)
        rbox = jag([(1622, 1080), (1628, 876), (1700, 866), (1704, 854), (1790, 858), (1794, 868), (1920, 862), (1920, 1080)], 13, 3.0, 12)
        mount = jag(rotated([(1812, 878), (1818, 806), (1846, 790), (1924, 796), (1924, 878)], 1866, 834, 3), 17, 2.0, 12)
    else:
        rbody = [(1560, 1080), (1610, 880), (1650, 854), (1796, 854), (1796, 830), (1846, 784), (1920, 784), (1920, 1080)]
        rbox = [(1586, 1080), (1628, 884), (1662, 863), (1920, 863), (1920, 1080)]
        mount = [(1816, 878), (1816, 834), (1858, 794), (1920, 794), (1920, 878)]
    H.stack(p, rbody, *LIGHT)
    H.stack(p, rbox, *BAR)
    lay = H.layout()
    portrait, exp = (lambda r: (r[0], r[1] + TOP, r[2], r[3] + TOP))(lay["portrait"]), (lambda r: (r[0], r[1] + TOP, r[2], r[3] + TOP))(lay["exp"])
    H.floor(p, plate(1638, 875, 1916, 1080, tl=(CORNER[b], 9)))
    H.well(p, plate(*grow(portrait, 2)), 6, 2.5)
    H.well(p, plate(*exp), 6, 2)
    H.stack(p, mount, 6, 10, 3, 1.8)
    if b == "terraced":
        eave_roof(p, 1652, 1960, 869, 855)
        blade(p, 1626, 960, -10, 7)
    elif b == "scavenged":
        patch(p, 1630, 1050, 1700, 1074, 4, 51)
        patch(p, 1820, 840, 1900, 860, -5, 52)
        p.bump([T(sheet(1660, 1060, 1720, 1076, 3))], 1.4, 0.8, STEEL)
    elif b == "faceted":
        H.stack(p, [(1566, 1080), (1596, 910), (1636, 866), (1740, 856), (1924, 842), (1924, 878), (1634, 878)], 6, 11, 4, 2.4)
        for a, c in (((1636, 866), (1650, 878)), ((1740, 856), (1760, 878))):
            p.inlay([T(plate(a[0], a[1], a[0] + 1.4, c[1]))], INKM, 1.0)


def paint_queue(p, g, b, n=3):
    """The production queue's tray standing on the shelf, n cells of 77x59 at an 80 pitch, as hudTray's."""
    stop = g.stop
    H.stack(p, plate(0, stop - 74, 8 + 80 * n, stop + 4, tr=(CORNER[b], 10)), *BAR)
    for i in range(n):
        H.well(p, cut4(grow((6 + 80 * i, stop - 66, 83 + 80 * i, stop - 7), 1), 4), 5, 1.8)


# ---------------------------------------------------------------------------------------------- rendering the pieces

DECK_AT = (44, 760)                             # the Deck's top left: sx0 less 44, screen y 760
DECK_W = 820
RIGHT_AT = (1550, 770)                          # the Right piece's top left; it runs to the screen's right and foot
BOX_EDGE = (16, 60, 72, 24)                     # the box's nine-slice: left, top, right, bottom
BOX_OUT = (20, 12)                              # the slice's rect runs this far over the box's top and past its right
SHELF_LEFT, SHELF_RIGHT, SHELF_UP, SHELF_H = 320, 40, 8, 66
FEATHER = 12                                    # ShelfRight's first columns fade in over the run
SHELF_W_REF = 520                               # the shelf drawn this wide for its pieces: a run of Mid between the ends
SHOULDER = (152, 8)                             # the shoulder piece from sw - 152 to sw + 8
SPINE_PAD = 8
QUEUE_UP, QUEUE_H, QUEUE_PITCH = 82, 94, 80
SWITCH = (36, 34)
TOOL = (44, 34)
COUNT, HEALTH = (36, 18), (86, 6)


def kill_skirt(img, boxes):
    """Inside each box (texel coords of img), drop the shadow skirt a cut leaves: a part drawn by another piece
    shows there, not the cut's shadow."""
    a = np.asarray(img).copy()
    for x0, y0, x1, y1 in boxes:
        sub = a[max(y0, 0):y1, max(x0, 0):x1, 3]
        sub[sub < 128] = 0
    return Image.fromarray(a, "RGBA")


def even(piece, axis):
    return H.even(piece, axis)


def crop(img, r):
    """A screen rect out of a 2048x512 bar-space image."""
    return img.crop((r[0], r[1] - TOP, r[2], r[3] - TOP))


def feather(img, n):
    """The piece's first n columns faded in, so it lies over the shelf's run before it without a seam in the grain."""
    a = np.asarray(img).copy()
    a[:, :n, 3] = (a[:, :n, 3] * ((np.arange(n) + 1) / (n + 1))[None, :]).astype(np.uint8)
    return Image.fromarray(a, "RGBA")


def left_pieces(side):
    look = BAR_LOOKS[side]
    b = look["build"]
    g = Geo(*REF)
    out = {}
    # the box, nine-slice
    p = H.Painter(1100, 512)
    paint_box(p, g, b)
    img = H.bar_finish(H.shade(p, look), p, look, scan=False)
    L, Tt, R, B = BOX_EDGE
    x0, y0, x1, y1 = 0, g.btop - BOX_OUT[0], g.bw + BOX_OUT[1], 1080
    box = crop(img, (x0, y0, x1, y1))
    W, Hh = box.size
    mx, my = (L + W - R) // 2, (Tt + Hh - B) // 2
    out["BoxTL"], out["BoxTR"] = box.crop((0, 0, L, Tt)), box.crop((W - R, 0, W, Tt))
    out["BoxBL"], out["BoxBR"] = box.crop((0, Hh - B, L, Hh)), box.crop((W - R, Hh - B, W, Hh))
    out["BoxT"] = even(box.crop((mx, 0, mx + 8, Tt)), 1)
    out["BoxB"] = even(box.crop((mx, Hh - B, mx + 8, Hh)), 1)
    out["BoxL"] = even(box.crop((0, my, L, my + 8)), 0)
    out["BoxR"] = even(box.crop((W - R, my, W, my + 8)), 0)
    out["BoxC"] = even(even(box.crop((mx, my, mx + 8, my + 8)), 0), 1)
    # the spine's run between the shelf's foot and the head
    p = H.Painter(1100, 512)
    paint_spine(p, g, b)
    img = H.bar_finish(H.shade(p, look), p, look)
    out["Spine"] = even(crop(img, (g.sx0 - SPINE_PAD, 750, g.sx1 + SPINE_PAD, 758)), 0)
    # the shoulder: the body's right edge under the shelf
    p = H.Painter(1100, 512)
    H.stack(p, body_outline(g, b), *LIGHT)
    img = H.bar_finish(H.shade(p, look), p, look)
    out["Shoulder"] = even(crop(img, (g.sw - SHOULDER[0], 750, g.sw + SHOULDER[1], 758)), 0)
    # the shelf, drawn wide: Left, a run of Mid, Right
    gs = Geo(*REF, sw_min=SHELF_W_REF)
    p = H.Painter(1100, 512)
    paint_shelf(p, gs, b)
    img = H.bar_finish(H.shade(p, look), p, look)
    if b == "faceted":
        img = H.led(img, [((12, gs.stop + 46 - TOP), (gs.sw - 16, gs.stop + 46 - TOP))], [])
    y0 = gs.stop - SHELF_UP
    sw = gs.sw
    shelf = crop(img, (0, y0, sw + 96, y0 + SHELF_H))
    out["ShelfLeft"] = shelf.crop((0, 0, SHELF_LEFT, SHELF_H))
    out["ShelfMid"] = even(shelf.crop((SHELF_LEFT + 40, 0, SHELF_LEFT + 48, SHELF_H)), 1)
    out["ShelfRight"] = feather(shelf.crop((sw - SHELF_RIGHT, 0, sw + 96, SHELF_H)), FEATHER)
    # the queue's tray: three cells, cut into the cap, a cell and the last cell with the end
    p = H.Painter(1100, 512)
    paint_queue(p, g, b, 3)
    img = H.bar_finish(H.shade(p, look), p, look)
    q = crop(img, (0, g.stop - QUEUE_UP, 400, g.stop - QUEUE_UP + QUEUE_H))
    end = int(np.nonzero(np.asarray(q)[..., 3].any(axis=0))[0][-1]) + 1
    out["QueueCap"] = q.crop((0, 0, 5, QUEUE_H))
    out["QueueCell"] = q.crop((5 + QUEUE_PITCH, 0, 5 + 2 * QUEUE_PITCH, QUEUE_H))
    out["QueueLast"] = q.crop((5 + 2 * QUEUE_PITCH, 0, end, QUEUE_H))
    # the deck
    p = H.Painter(1100, 512)
    paint_deck(p, g, b)
    img = H.bar_finish(H.shade(p, look), p, look)
    ox, oy = g.sx0 - DECK_AT[0], DECK_AT[1]
    deck = crop(img, (ox, oy, ox + DECK_W, 1080))
    deck = kill_skirt(deck, [(0, 0, g.sx0 + 2 - ox, 1080 - oy)])
    if b == "faceted":
        deck = deck_lights(deck, g, ox, oy)
    deck = money_sign(deck, g.money[0] + 13 - ox, (g.money[1] + g.money[3]) / 2 - oy)
    out["Deck"] = deck
    c = g.cells["Y"]
    out["KeyEmpty"] = deck.crop((c[0] - ox, c[1] - oy, c[2] - ox, c[3] - oy))
    return out


def money_sign(img, x, y, size=22):
    """The gold currency sign at the money well's left, as the bar's (MoneySignPainted)."""
    font = ImageFont.truetype(os.path.join(FONT_DIR, "arialbd.ttf"), size)
    lay = Image.new("RGBA", img.size, (0, 0, 0, 0))
    d = ImageDraw.Draw(lay)
    d.text((x + 1, y + 1), "$", font=font, fill=(0, 0, 0, 200), anchor="mm")
    d.text((x, y), "$", font=font, fill=H.GOLD + (255,), anchor="mm")
    out = img.copy()
    out.alpha_composite(lay)
    return out


def right_piece(side):
    look = BAR_LOOKS[side]
    b = look["build"]
    p = H.Painter(2048, 512)
    paint_right(p, b)
    img = H.bar_finish(H.shade(p, look), p, look)
    if b == "faceted":
        img = H.led(img, [((1640, 866 - TOP), (1738, 848 - TOP))], [(1600, 1000 - TOP, AMBER)])
    return crop(img, (RIGHT_AT[0], RIGHT_AT[1], 1920, 1080))


def small(side, w, h, draw, pad=32):
    """A small piece on a padded canvas (shade's noise goes bad on a narrow one), lit without falloff."""
    look = BAR_LOOKS[side]
    p = H.Painter(w + 2 * pad, h + 2 * pad)
    draw(p, pad, look["build"])
    return H.shade(p, look, shadow=False, falloff=False).crop((pad, pad, pad + w, pad + h))


def tool_key(side, kind, state):
    w, h = TOOL
    return small(side, w, h, lambda p, o, b: H.button(p, (o, o, o + w, o + h), kind, state, b))


def heads():
    """Three heads and shoulders, the middle one in front: the squad (in a 22 box round 11, 11)."""
    out = []
    for dx, dy, s in ((-6.2, 0.8, 0.8), (6.2, 0.8, 0.8), (0, 0, 1.0)):
        x, y = 11 + dx, 11 + dy
        out.append(regular(x, y - 4.6 * s, 2.9 * s, 12))
        out.append([(x - 6 * s, y + 7.5 * s), (x - 5.4 * s, y + 2.4 * s), (x - 2.4 * s, y + 0.6 * s), (x + 2.4 * s, y + 0.6 * s),
                    (x + 5.4 * s, y + 2.4 * s), (x + 6 * s, y + 7.5 * s)])
    return out, []


def grid9():
    """A 3x3 grid of squares: the keys (in a 22 box round 11, 11)."""
    return [plate(3.5 + 5.5 * j, 3.5 + 5.5 * i, 7.5 + 5.5 * j, 7.5 + 5.5 * i) for i in range(3) for j in range(3)], []


def switch_pieces(side):
    """The switch on the spine: SwitchGroup (in the command view, three heads, to the squad's tray) and SwitchBack
    (in the tray view, latched lit, a 3x3 grid, back to the commands)."""
    w, h = SWITCH
    out = {"SwitchGroup" + s: H.key_piece(side, w, h, s, heads(), glyph_scale=0.95) for s in STATES}
    out["SwitchBack"] = H.key_piece(side, w, h, "P", grid9(), glyph_scale=0.9)
    out["SwitchBackH"] = H.key_piece(side, w, h, "P", grid9(), glyph_scale=0.9, lit_k=1.25)
    out["SwitchBackP"] = H.key_piece(side, w, h, "P", grid9(), glyph_scale=0.9, lit_k=0.8)
    out["SwitchBackI"] = H.key_piece(side, w, h, "I", grid9(), glyph_scale=0.9)
    return out


def badge_pieces(side):
    """The tray view's readouts on a key: KeyCount, the plate the code writes "xN" on (stretches across), and
    KeyHealth, the groove its health bar fills along the key's foot."""
    cw, ch = COUNT
    hw, hh = HEALTH

    def count(p, o, b):
        r = (o, o, o + cw, o + ch)
        p.frame([H.shape(b, *r, 3)], 2.4, b, DARK, H.BEZEL_SCALE)
        p.sink([H.shape(b, *grow(r, -2.2), 2)], 1.4, 0.8)

    def health(p, o, b):
        p.frame([plate(o, o, o + hw, o + hh)], 1.6, "machined", DARK, 0.5)
        p.sink([plate(o + 1, o + 1, o + hw - 1, o + hh - 1)], 1.2, 0.6)
    return {"KeyCount": small(side, cw, ch, count), "KeyHealth": small(side, hw, hh, health)}


def pieces(side):
    out = left_pieces(side)
    out["Right"] = right_piece(side)
    out.update({"Tool" + k + s: tool_key(side, k, s) for k in TOOLS for s in STATES})
    out.update(switch_pieces(side))
    out.update(badge_pieces(side))
    return out


# ---------------------------------------------------------------------------------------------- the rule the code follows

def compose(P, gw, gh, queue=0):
    """The console laid from the pieces round a gw x gh glass: a 1920x1080 RGBA (the code's drawKeyGrid, line for line;
    the pieces' sizes are all it reads besides the glass)."""
    g = Geo(gw, gh)
    out = Image.new("RGBA", (1920, 1080), (0, 0, 0, 0))

    def put_(name, x, y):
        out.alpha_composite(P[name], (int(x), int(y)))

    def stretch(name, x0, y0, x1, y1):
        if x1 > x0 and y1 > y0:
            out.alpha_composite(P[name].resize((int(x1 - x0), int(y1 - y0)), Image.BILINEAR), (int(x0), int(y0)))

    sw, btop, stop = g.sw, g.btop, g.stop
    if btop < 790:
        stretch("Shoulder", sw - SHOULDER[0], btop - 8, sw + SHOULDER[1], 790)
    # the box's nine-slice
    x0, y0, x1, y1 = 0, btop - BOX_OUT[0], g.bw + BOX_OUT[1], 1080
    L, Tt, R, B = P["BoxTL"].width, P["BoxTL"].height, P["BoxTR"].width, P["BoxBL"].height
    stretch("BoxT", x0 + L, y0, x1 - R, y0 + Tt)
    stretch("BoxB", x0 + L, y1 - B, x1 - R, y1)
    stretch("BoxL", x0, y0 + Tt, x0 + L, y1 - B)
    stretch("BoxR", x1 - R, y0 + Tt, x1, y1 - B)
    stretch("BoxC", x0 + L, y0 + Tt, x1 - R, y1 - B)
    put_("BoxTL", x0, y0)
    put_("BoxTR", x1 - R, y0)
    put_("BoxBL", x0, y1 - B)
    put_("BoxBR", x1 - R, y1 - B)
    if btop < 790:
        w = P["Spine"].width
        stretch("Spine", g.sx0 - (w - (g.sx1 - g.sx0)) // 2, btop, g.sx0 - (w - (g.sx1 - g.sx0)) // 2 + w, 790)
    put_("Deck", g.sx0 - DECK_AT[0], DECK_AT[1])
    sy = stop - SHELF_UP
    put_("ShelfLeft", 0, sy)
    lw = P["ShelfLeft"].width                   # Mid runs on under ShelfRight's faded-in edge
    stretch("ShelfMid", lw, sy, max(lw, sw - SHELF_RIGHT) + FEATHER, sy + P["ShelfMid"].height)
    put_("ShelfRight", sw - SHELF_RIGHT, sy)
    if queue:
        qy = stop - QUEUE_UP
        put_("QueueCap", 0, qy)
        for i in range(queue - 1):
            put_("QueueCell", 5 + QUEUE_PITCH * i, qy)
        put_("QueueLast", 5 + QUEUE_PITCH * (queue - 1), qy)
    put_("Right", 1920 - P["Right"].width, 1080 - P["Right"].height)
    return out


def check_compose(P, side):
    """At every map aspect the code may meet: every window it lays something into stands on the painting, nothing
    stands over the shelf's top on the left or over the mount's on the right, and each console is one piece."""
    from scipy import ndimage
    for aspect in (0.55, 0.65, 0.765, 0.9, 1.0, 1.15, 1.31, 1.5, 1.8):
        gw, gh = glass_for(aspect)
        img = compose(P, gw, gh, queue=0)
        a = np.asarray(img)[..., 3]
        g = Geo(gw, gh)
        for name, r in rects(gw, gh).items():
            if name == "Switch" and g.btop + 2 > r[1]:
                continue                        # no room between the shelf and the head on a wide map
            sub = a[r[1]:r[3], r[0]:r[2]]
            assert sub.size and (sub > 250).mean() > (0.95 if name.startswith("Tool") else 0.98), "%s %.2f: %s %s is not on the painting (%.2f)" % (
                side, aspect, name, r, (sub > 250).mean())
        left = a[:, :1500] > 128
        top = int(np.nonzero(left.any(axis=1))[0][0])
        # past aspect 1.55 the shelf stands lower than the money head, which is then the top
        assert top >= min(g.stop, 770) - 1, "%s %.2f: the left console stands over the shelf (%d < %d)" % (side, aspect, top, g.stop)
        right = a[:, 1500:] > 128
        assert int(np.nonzero(right.any(axis=1))[0][0]) >= 776, "%s: the right console stands too high" % side
        for part, m in (("left", left), ("right", right)):
            n = ndimage.label(m)[1]
            assert n == 1, "%s %.2f: the %s console is %d pieces (floating)" % (side, aspect, part, n)


# ---------------------------------------------------------------------------------------------- the sheet

SHEET = (2048, 512)
GAP = 5


def sheet_at(P):
    """Every piece's box on ReforgedHud<Side>KeyGrid: shelves left to right, tallest first, 5 apart.  The sizes are
    the same on every side, so are the places (asserted by the caller)."""
    order = sorted(P, key=lambda n: (-P[n].height, -P[n].width, n))
    at, x, y, shelf = {}, 2, 2, 0
    for name in order:
        w, h = P[name].size
        if x + w + 2 > SHEET[0]:
            x, y, shelf = 2, y + shelf + GAP, 0
        at[name] = (x, y, x + w, y + h)
        x += w + GAP
        shelf = max(shelf, h)
    assert y + shelf + 2 <= SHEET[1], "the pieces overrun the sheet"
    return at


def texture_name(side):
    return "ReforgedHud%sKeyGrid" % side


def ini_path(side):
    return os.path.join(H.HERE, "..", "Data", "INI", "MappedImages", "HandCreated", "ReforgedHud%sKeyGrid.ini" % side)


def ini_text(side, at):
    blocks = []
    for name in sorted(at):
        x0, y0, x1, y1 = at[name]
        blocks.append("MappedImage %sKeyGrid%s\r\n  Texture = %s.tga\r\n  TextureWidth = %d\r\n  TextureHeight = %d\r\n"
                      "  Coords = Left:%d Top:%d Right:%d Bottom:%d\r\n  Status = NONE\r\nEnd\r\n"
                      % (PREFIX[side], name, texture_name(side), SHEET[0], SHEET[1], x0, y0, x1, y1))
    return "\r\n".join(blocks)


def check_mapped(side, at):
    text = open(ini_path(side), encoding="latin-1").read()
    got = {}
    for m in re.finditer(r"MappedImage\s+(\S+)\s+Texture\s*=\s*(\S+)\s+TextureWidth\s*=\s*(\d+)\s+TextureHeight\s*=\s*(\d+)\s+"
                         r"Coords\s*=\s*Left:(\d+)\s+Top:(\d+)\s+Right:(\d+)\s+Bottom:(\d+)", text):
        got[m.group(1)] = (m.group(2), int(m.group(3)), int(m.group(4))) + tuple(int(v) for v in m.groups()[4:])
    want = {PREFIX[side] + "KeyGrid" + n: (texture_name(side) + ".tga",) + SHEET + b for n, b in at.items()}
    assert got == want, "ReforgedHud%sKeyGrid.ini: %s" % (side, sorted(set(got.items()) ^ set(want.items()))[:6])


def check_pieces(P):
    for name in ("BoxT", "BoxB", "ShelfMid"):
        a = np.asarray(P[name])
        assert (a == a[:, :1]).all(), name + " is not the same all across"
    for name in ("BoxL", "BoxR", "Spine", "Shoulder"):
        a = np.asarray(P[name])
        assert (a == a[:1]).all(), name + " is not the same all down"
    for name in ("BoxC", "KeyEmpty"):
        assert (np.asarray(P[name])[..., 3] == 255).all(), name + " is not opaque"
    for name in ("Deck", "Right"):
        a = np.asarray(P[name])[..., 3]
        assert not a[0].any() and not a[:, -1 if name == "Deck" else 0].any(), name + " is cut off at its border"


def paint_sheet(side, ref_at=None):
    P = pieces(side)
    check_pieces(P)
    check_compose(P, side)
    at = sheet_at(P)
    if ref_at is not None:
        assert at == ref_at, "%s's pieces differ in size from the first side's" % side
    img = Image.new("RGBA", SHEET, (0, 0, 0, 0))
    for name, piece in P.items():
        H.put(img, piece, at[name])
    return img, at, P


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--out", required=True, help="the PNGs")
    ap.add_argument("--side", default="all")
    ap.add_argument("--dds", help="Data/Art/Textures, to write the DXT5 textures too")
    ap.add_argument("--ini-text", help="write each side's mapped INI's text into this folder, for the master to be rewritten "
                                       "whole from (it is never written in place); without it the masters are checked")
    args = ap.parse_args()
    os.makedirs(args.out, exist_ok=True)
    for side in (SIDES if args.side == "all" else (args.side,)):
        img, at, P = paint_sheet(side)
        if args.ini_text:
            os.makedirs(args.ini_text, exist_ok=True)
            with open(os.path.join(args.ini_text, "ReforgedHud%sKeyGrid.ini" % side), "w", encoding="latin-1", newline="") as f:
                f.write(ini_text(side, at))
        else:
            check_mapped(side, at)
        name = texture_name(side)
        img.save(os.path.join(args.out, name + ".png"))
        for piece, im in P.items():
            os.makedirs(os.path.join(args.out, side), exist_ok=True)
            im.save(os.path.join(args.out, side, piece + ".png"))
        if args.dds:
            path = os.path.join(args.dds, name + ".dds")
            img.save(path, pixel_format="DXT5")
            with open(path, "rb") as f:
                assert f.read(88)[84:88] == b"DXT5", "%s.dds is not DXT5" % name
            H.dxt5_touch_up(path, img)
        print(side, "done", flush=True)


if __name__ == "__main__":
    main()
