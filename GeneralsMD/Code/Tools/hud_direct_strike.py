"""Direct Strike's sheets: every piece of the mode (the match HUD, the lobby, the end screen) named, drawn from
hud_direct_strike_theme, packed one sheet a side, and written with its mapped INI in one go.

    python Tools/hud_direct_strike.py --png DIR            the sheets and every piece as PNG, nothing shipped
    python Tools/hud_direct_strike.py --ship [--png DIR]   also the DXT5 textures and mapped INIs into Code/Data and Run

The sheet is ReforgedHud<Side>DirectStrike (2048 wide), its INI MappedImages/HandCreated/ReforgedHud<Side>DirectStrike.ini;
each piece is <side prefix><name> at its 1080 size with put()'s one texel gutter and 3 clear between.  Pieces that come
out the same pixels share one rect.  HUD_SS=3 (the default) for what ships.  docs/reference/DIRECT-STRIKE-THEME.md is
the contract the code reads.
"""
import argparse
import filecmp
import io
import os
import shutil

import numpy as np
from PIL import Image, ImageDraw, ImageFilter

import bigfile
import hud_command_bar as H
import hud_direct_strike_theme as T
from hud_command_bar import plate, rotated
from hud_sidebar import box, icon_layers

HERE = os.path.dirname(os.path.abspath(__file__))
DATA = os.path.normpath(os.path.join(HERE, "..", "Data"))
RUN = os.path.normpath(os.path.join(HERE, "..", "..", "Run"))
TEXTURES, MAPPED = os.path.join("Art", "Textures"), os.path.join("INI", "MappedImages", "HandCreated")
SHEET_W, SHEET_GAP, EDGE = 2048, 5, 2
STATES, KEY_STATES = ("", "H", "P", "I"), ("", "H", "P")

# ---------------------------------------------------------------------------------------------- shapes the pieces carry

CARD = (100, 116)                               # the hand's card
CAMEO_AT = (5, 5, 95, 77)                       # its picture well; the cameo 86x69 drawn 2 inside
PRICE_AT = (8, 84, 92, 108)                     # its price band
TOGGLE = (44, 22)
END_CARD, END_RESULT, END_HEADER, END_TIMELINE = (250, 590), (340, 452), (420, 76), (1700, 168)
END_CARD_WELLS = [(10, 10, 240, 232), (64, 278, 186, 376), (10, 388, 240, 580)]     # portrait, MVP cameo, stats
END_RESULT_WELLS = [(10, 10, 330, 300), (10, 316, 330, 442)]                       # portrait, result word and time
END_RESULT_RULE = (10, 303, 330, 305)
TIMELINE_INSET, TIMELINE_MID = 8, 64
END_KEY, STAT = (72, 56), 18
END_KEYS = {"Rematch": "rematch", "Replay": "replay", "Menu": "menu"}
STATS = {"Waves": "waves", "Placed": "placed", "Earned": "earned", "Spent": "spent", "Kills": "kills", "Damage": "damage",
         "Middle": "middle", "Time": "time", "Fortress": "fortress"}
ROW_H, ROW_CAP, ROW_BAR = 36, 12, 4               # a list row: the lobby's 36, its 3-slice ends, the gold bar's width
MASTER, WELL_AT = 160, (24, 24, 136, 136)       # the 9-slices are cut from a panel this size; the well's own from this rect
BUTTON = (432, 72)                              # the wide text key the 3-slice is cut from
LAMP, COIN = 32, 28
# a plain side's portrait: its command centre's build button picture (SAComCentr_L, SNComCentr_L, SUHeadquarters_L,
# 120x96 or less), cut from the installed EnglishZH.big and enlarged here to the portrait well's 172 square, so the
# game never stretches it at 1080
CAMEO_ARCHIVE = os.path.join(RUN, "EnglishZH.big")
CAMEOS = {"America": ("SAUserInterface512_002.tga", (367, 1, 487, 97)),
          "China": ("SNUserInterface512_003.tga", (1, 197, 109, 286)),
          "Gla": ("SUUserInterface512_003.tga", (245, 197, 365, 293))}
WELL_PICTURE = 172
PICK_TILE = (136, 112)
ISLAND, ISLAND_SMALL = (480, 480), (96, 60)


def tab_lock():
    def draw(d, k):
        d.ellipse((4 * k, 1.5 * k, 14 * k, 13 * k), fill=255)
        d.ellipse((6.2 * k, 3.8 * k, 11.8 * k, 11 * k), fill=0)
        d.rectangle((2.5 * k, 8.5 * k, 15.5 * k, 18.5 * k), fill=255)
        d.rectangle((8.2 * k, 11.5 * k, 9.8 * k, 15.5 * k), fill=0)
    return T.mask((18, 20), draw, T.INK["paper"])


def map_fortress(build):
    if build == "machined":
        return T.mark(16, 16, [H.octagon(2, 2, 14, 14, 3.6)])
    if build == "terraced":
        return T.mark(16, 16, [plate(2, 2, 14, 14, ("notch", 3), ("notch", 3), ("notch", 3), ("notch", 3))])
    return T.mark(16, 16, [[(2.4, 4), (7, 1.8), (13.8, 3.4), (14, 12), (9, 14.2), (2, 12.8)]])


def map_tower(build):
    if build == "machined":
        return T.mark(10, 10, [[(5, 1.5), (8.5, 5), (5, 8.5), (1.5, 5)]])
    if build == "terraced":
        return T.mark(10, 10, [plate(1.8, 1.8, 8.2, 8.2, ("notch", 1.4), ("notch", 1.4), ("notch", 1.4), ("notch", 1.4))])
    return T.mark(10, 10, [[(1.6, 8.4), (5.6, 1.4), (8.6, 8)]])


def income_glyph(build):
    coins = [H.octagon(1.5, 15, 12.5, 20, 1.6), H.octagon(1.5, 10, 12.5, 15, 1.6), H.octagon(1.5, 5, 12.5, 10, 1.6)]
    rims = [box(2.5, 14.4, 11.5, 15.6), box(2.5, 9.4, 11.5, 10.6)]
    arrow = [[(15.8, 20.5), (15.8, 9), (13.2, 9), (17.5, 2), (21.8, 9), (19.2, 9), (19.2, 20.5)]]
    if build == "terraced":
        arrow = [box(15.8, 12, 19.2, 20), box(14.2, 8, 20.8, 12), box(15.8, 4, 19.2, 8), box(16.8, 1.5, 18.2, 4)]
    elif build == "scavenged":
        arrow = [rotated(arrow[0], 17.5, 11, 6)]
    return [(coins + arrow, 255), (rims, 0)]


def abilities_glyph(build):
    """A lightning bolt in a 22 box, cut the build's way."""
    bolt = [(12.5, 1.5), (4.5, 12.5), (10, 12.5), (8.5, 20.5), (17.5, 8.5), (12, 8.5)]
    if build == "terraced":
        bolt = [(12, 1.5), (5, 12), (10, 12), (10, 20.5), (17, 9), (12, 9)]
    if build == "scavenged":
        bolt = rotated(bolt, 11, 11, 7)
    return [([bolt], 255)]


def tier_glyph(build, n):
    """n marks for the tier: America's chevrons, China's stacked bars, the GLA's crooked slashes."""
    if build == "machined":
        polys = list(H.chevrons(11, 11 - 3.2 * (n - 1) - 3, 12, n=n, up=True))
    elif build == "terraced":
        polys = [plate(5, 15 - 4.6 * i, 17, 18 - 4.6 * i) for i in range(n)]
    else:
        mid = [9 + 5 * i - 5 * (n - 1) / 2 for i in range(n)]
        polys = [rotated(plate(x - 1.3, 4, x + 1.3, 18), x, 11, 18) for x in mid]
    return [(polys, 255)]


def air_mark():
    """A jet seen from above, white with a dark keyline, for a card's picture's corner."""
    raised, _ = H.icon("Aircraft", 9, 9.5, "machined")
    return T.mark(18, 18, [[(x * 0.62 + 3.4, y * 0.62 + 3.6) for x, y in q] for q in raised])


def toggle(side, on):
    """A switch: dark with its knob left when off, lit in the side's colour with its knob right when on."""
    w, h = TOGGLE
    img = T.key(side, w, h, "P" if on else "", corner=0.5)
    knob = Image.new("RGBA", (w * T.BIG, h * T.BIG), (0, 0, 0, 0))
    d = ImageDraw.Draw(knob)
    k = T.BIG
    cx = (w - 12) if on else 12
    d.ellipse(((cx - 6.5) * k, 4.5 * k, (cx + 6.5) * k, 17.5 * k), fill=T.INK["keyline"] + (255,))
    d.ellipse(((cx - 5.5) * k, 5.5 * k, (cx + 5.5) * k, 16.5 * k), fill=(240, 242, 246, 255) if on else T.INK["dim"] + (255,))
    img.alpha_composite(knob.resize(TOGGLE, Image.BOX))
    return img


def stat_glyph(kind, size, colour=T.INK["white"]):
    """The end screen's stat marks in a 20 box."""
    def draw(d, kk):
        k = size / 20 * kk

        def poly(pts, fill=255):
            d.polygon([(x * k, y * k) for x, y in pts], fill=fill)

        def rect(x0, y0, x1, y1, fill=255):
            d.rectangle((x0 * k, y0 * k, x1 * k, y1 * k), fill=fill)

        def hoop(cx, cy, r, w):
            d.ellipse(((cx - r) * k, (cy - r) * k, (cx + r) * k, (cy + r) * k), fill=255)
            d.ellipse(((cx - r + w) * k, (cy - r + w) * k, (cx + r - w) * k, (cy + r - w) * k), fill=0)

        if kind == "waves":                     # three chevrons marching right
            for i in range(3):
                x = 1.5 + 5.6 * i
                poly([(x, 3.5), (x + 3.2, 3.5), (x + 9, 10), (x + 3.2, 16.5), (x, 16.5), (x + 5.8, 10)])
        elif kind == "placed":                  # a plot's grid with one cell taken
            for v in (2, 7.4, 12.6, 18):
                rect(v - 0.8, 1.2, v + 0.8, 18.8)
                rect(1.2, v - 0.8, 18.8, v + 0.8)
            rect(7.4, 7.4, 12.6, 12.6)
        elif kind in ("earned", "spent"):       # a coin and an arrow up or down
            hoop(8, 11, 6.5, 2.0)
            rect(6.8, 8, 9.2, 14)
            up = kind == "earned"
            rect(16, 6 if up else 3, 18, 15 if up else 12)
            poly([(13.5, 7), (17, 2), (20.5, 7)] if up else [(13.5, 11), (17, 16), (20.5, 11)])
        elif kind == "kills":                   # a reticle
            hoop(10, 10, 7, 1.8)
            for r in ((9.1, 0.5, 10.9, 5.5), (9.1, 14.5, 10.9, 19.5), (0.5, 9.1, 5.5, 10.9), (14.5, 9.1, 19.5, 10.9)):
                rect(*r)
            d.ellipse((8.4 * k, 8.4 * k, 11.6 * k, 11.6 * k), fill=255)
        elif kind == "damage":                  # a tower split by a crack
            poly([(4, 19), (16, 19), (13.5, 8), (6.5, 8)])
            rect(4, 3, 16, 8)
            rect(4, 1, 6.5, 3)
            rect(8.75, 1, 11.25, 3)
            rect(13.5, 1, 16, 3)
            poly([(11.5, 3), (8.6, 8), (11.4, 11.5), (8.6, 15.5), (10, 19), (12.2, 19), (10.6, 15.5), (13.2, 11.5), (10.6, 8), (13.4, 3)], 0)
        elif kind == "middle":                  # the bars chip: ours from the left, theirs from the right, the line
            rect(0.5, 8, 8.2, 12)
            rect(11.8, 8, 19.5, 12)
            rect(9.2, 2.5, 10.8, 17.5)
        elif kind == "time":
            hoop(10, 10, 8.5, 1.9)
            rect(9.1, 4.5, 10.9, 10.6)
            rect(9.1, 9.1, 14.5, 10.9)
        elif kind == "fortress":
            rect(2, 6, 18, 18)
            for x in (2, 7.25, 12.5):
                rect(x, 3, x + 5.5, 6)
            rect(8, 12, 12, 18, 0)
    return T.mask((size, size), draw, colour)


def arc(cx, cy, r0, r1, a0, a1, n=24):
    """A ring segment from angle a0 to a1 (degrees, clockwise from 3 o'clock, y down) as one polygon."""
    a = np.radians(np.linspace(a0, a1, n))
    return [(cx + r1 * np.cos(t), cy + r1 * np.sin(t)) for t in a] + [(cx + r0 * np.cos(t), cy + r0 * np.sin(t)) for t in a[::-1]]


def head(cx, cy, r, a, size=3.4):
    """An arrow head at the end of a clockwise arc at angle a."""
    t = np.radians(a)
    tx, ty, nx, ny = -np.sin(t), np.cos(t), np.cos(t), np.sin(t)
    px, py = cx + r * nx, cy + r * ny
    return [(px + nx * size, py + ny * size), (px + tx * size * 1.3, py + ty * size * 1.3), (px - nx * size, py - ny * size)]


def button_layers(kind, build):
    """The end keys' glyphs in the 22 box: rematch two arrows chasing round, replay a screen with a play mark, the
    menu the HUD's own menu glyph."""
    if kind == "rematch":
        return [([arc(11, 11, 5.2, 7.6, 200, 330), head(11, 11, 6.4, 330), arc(11, 11, 5.2, 7.6, 20, 150), head(11, 11, 6.4, 150)], 255)]
    if kind == "replay":
        return [([plate(2.5, 4.5, 19.5, 17.5)], 255), ([plate(4.4, 6.4, 17.6, 15.6)], 0), ([[(9, 7.8), (9, 14.2), (14.4, 11)]], 255)]
    return icon_layers("Options", build)


def arrow_layers(kind):
    if kind == "back":
        return [([[(3.5, 11), (10, 4.5), (10, 8.6), (18.5, 8.6), (18.5, 13.4), (10, 13.4), (10, 17.5)]], 255)]
    return [([[(17, 4), (19.4, 4), (19.4, 14.2), (8.5, 14.2), (8.5, 17.8), (2.6, 12.8), (8.5, 7.8), (8.5, 11.4), (17, 11.4)]], 255)]


def lobby_glyphs():
    """The marks the lobby lays on its pieces: ping, AI, an open seat, a closed one, Random's portrait, the list's
    check."""
    out = {}
    green = T.INK["green"]
    for n, col in ((1, (214, 60, 44)), (2, (236, 150, 40)), (3, (200, 214, 60)), (4, green)):
        def bars(d, k, n=n):
            for i in range(4):
                h = 6 + 5 * i
                d.rectangle(((1 + 7 * i) * k, (24 - h) * k, (6 + 7 * i) * k, 24 * k), fill=255 if i < n else 70)
        out["LobbyPing%d" % n] = T.mask((28, 24), bars, col)

    def chip(d, k):
        d.rectangle((6 * k, 6 * k, 22 * k, 22 * k), fill=255)
        d.rectangle((9 * k, 9 * k, 19 * k, 19 * k), fill=0)
        d.rectangle((11.5 * k, 11.5 * k, 16.5 * k, 16.5 * k), fill=255)
        for t in (9.5, 13.25, 17):
            for r in ((t, 1, t + 1.6, 6), (t, 22, t + 1.6, 27), (1, t, 6, t + 1.6), (22, t, 27, t + 1.6)):
                d.rectangle(tuple(v * k for v in r), fill=255)
    out["LobbyAI"] = T.mask((28, 28), chip, T.INK["grey"])

    def open_seat(d, k):
        d.ellipse((4 * k, 4 * k, 68 * k, 68 * k), fill=255)
        d.ellipse((8 * k, 8 * k, 64 * k, 64 * k), fill=0)
        d.rectangle((33.5 * k, 20 * k, 38.5 * k, 52 * k), fill=255)
        d.rectangle((20 * k, 33.5 * k, 52 * k, 38.5 * k), fill=255)
    out["LobbyOpen"] = T.mask((72, 72), open_seat, T.INK["muted"])

    def lock(d, k):
        d.rounded_rectangle((8 * k, 21 * k, 40 * k, 44 * k), 3 * k, fill=255)
        d.ellipse((13 * k, 4 * k, 35 * k, 30 * k), fill=255)
        d.ellipse((17.5 * k, 8.5 * k, 30.5 * k, 25.5 * k), fill=0)
        d.rectangle((17.5 * k, 16 * k, 30.5 * k, 21 * k), fill=0)
        d.ellipse((21 * k, 28 * k, 27 * k, 34 * k), fill=0)
        d.rectangle((22.6 * k, 31 * k, 25.4 * k, 39 * k), fill=0)
    out["LobbyClosed"] = T.mask((48, 48), lock, T.INK["dim"])

    def hoop(d, k):
        d.ellipse((2 * k, 2 * k, 70 * k, 70 * k), fill=255)
        d.ellipse((6 * k, 6 * k, 66 * k, 66 * k), fill=0)
    q = T.mask((72, 72), hoop, T.INK["muted"])
    ImageDraw.Draw(q).text((36, 38), "?", font=T.font(44), fill=T.INK["muted"] + (255,), anchor="mm")
    out["LobbyRandom"] = q

    def check(d, k):
        d.line([(3 * k, 10 * k), (8 * k, 15 * k), (17 * k, 4 * k)], fill=255, width=int(3 * k), joint="curve")
    out["LobbyCheck"] = T.mask((20, 20), check)
    return out


def island_art(w, h, seed=11):
    """A random island: sea, an organic coast with a lane of earth across it, a question mark over the middle."""
    from scipy import ndimage
    k = 3
    rng = np.random.default_rng(seed)
    yy, xx = np.mgrid[0:h * k, 0:w * k].astype(np.float32)
    n = ndimage.gaussian_filter(rng.standard_normal((h * k, w * k)), w * k * 0.05)
    n /= n.std()
    r = np.sqrt(((xx - w * k / 2) / (w * k * 0.42)) ** 2 + ((yy - h * k / 2) / (h * k * 0.34)) ** 2) + 0.11 * n
    img = np.zeros((h * k, w * k, 3), np.float32)
    img[:] = (16, 34, 54)
    img[r < 1.06] = (26, 52, 74)
    land = r < 1.0
    img[land] = (48, 70, 40)
    lane = land & (r < 0.9) & (np.abs(yy - h * k / 2 - h * k * 0.03 * np.sin(xx / (w * k) * 6.0)) < h * k * 0.07)
    img[lane] = (92, 78, 58)
    img *= (1 + 0.07 * ndimage.gaussian_filter(rng.standard_normal((h * k, w * k)), 2))[..., None]
    out = Image.fromarray(np.clip(img, 0, 255).astype(np.uint8)).resize((w, h), Image.BOX).convert("RGBA")
    d = ImageDraw.Draw(out)
    for y in range(int(h * 0.36), int(h * 0.64), 10):
        d.line([(w / 2, y), (w / 2, y + 5)], fill=T.INK["gold"] + (230,), width=2)
    if h >= 100:                                # the chip's small island is too small for the mark
        T.text(d, (w / 2, h / 2), "?", int(h * 0.24), T.INK["white"] + (220,), "mm")
    return out

# ---------------------------------------------------------------------------------------------- the pieces


def slices(prefix, cut, states=("",)):
    """A dict of slice pieces: prefix + part + state."""
    return {prefix + part + state: img for state in states for part, img in cut(state).items()}


def drop_part(side, state):
    full = T.key(side, 200, T.DROP_H, state)
    right = full.crop((200 - T.DROP_R, 0, 200, T.DROP_H))
    colour = T.INK["on_lit"] if state == "P" else T.INK["gold"] if state == "H" else T.INK["white"]
    # a divider off the field, a dark groove with a lit lip, then the caret centred in the cap past it: a chevron
    # 14 across with a 2.75 stroke (1.8 at 720, still a solid line), pointing down, up while its list is open
    divider, cx, cy, half, rise, stroke = 8, 25.5, T.DROP_H / 2, 7.0, 4.0, 2.75
    groove = ImageDraw.Draw(right)
    groove.line([(divider, 11), (divider, T.DROP_H - 12)], fill=T.INK["keyline"] + (230 if state != "P" else 110,), width=1)
    groove.line([(divider + 1, 11), (divider + 1, T.DROP_H - 12)], fill=(255, 255, 255, 34 if state != "P" else 70), width=1)
    tip = -1 if state == "P" else 1

    def chevron(dx, dy):
        def draw(d, k):
            arm = [(cx - half + dx, cy - tip * rise / 2 + dy), (cx + dx, cy + tip * rise / 2 + dy), (cx + half + dx, cy - tip * rise / 2 + dy)]
            d.line([(x * k, y * k) for x, y in arm], fill=255, width=int(stroke * k), joint="curve")
        return draw

    if state != "P":
        right.alpha_composite(T.mask((T.DROP_R, T.DROP_H), chevron(0.8, 0.8), T.INK["keyline"]))
    right.alpha_composite(T.mask((T.DROP_R, T.DROP_H), chevron(0, 0), colour))
    return {"L": full.crop((0, 0, T.DROP_L, T.DROP_H)), "C": T.uniform(full.crop((96, 0, 96 + T.DROP_C, T.DROP_H)), 1), "R": right}


def list_row(side, state):
    """A row of an open list, as a 3-slice: P the current choice, the side's lit colour as a glass band with a gold
    bar on its leading edge; H the row under the pointer, a plain light band without the bar."""
    w, h = 200, ROW_H
    out = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    d = ImageDraw.Draw(out)
    if state == "P":
        lit = tuple(T.look(side)["lit"])
        for y in range(h):
            d.line([(0, y), (w, y)], fill=lit + (int(176 - 40 * y / (h - 1)),))
        d.line([(0, 0), (w, 0)], fill=tuple(min(255, c + 70) for c in lit) + (220,))
        d.line([(0, h - 1), (w, h - 1)], fill=T.INK["keyline"] + (150,))
        d.rectangle([(0, 0), (ROW_BAR - 1, h - 1)], fill=T.INK["gold"] + (255,))
        d.line([(ROW_BAR, 0), (ROW_BAR, h - 1)], fill=T.INK["keyline"] + (170,))
    else:
        d.rectangle([(0, 0), (w - 1, h - 1)], fill=(255, 255, 255, 30))
        d.line([(0, 0), (w, 0)], fill=(255, 255, 255, 70))
    return T.three_cut(out, ROW_CAP, 8)


def shared(side):
    """The pieces every screen draws: the panel, the lit panel, the bare plate and the glass well as 9-slices, the
    wide text key and the dropdown as 3-slices, the coins and the lamps."""
    s = MASTER
    well = T.panel(side, s, s, [WELL_AT]).crop(WELL_AT)
    out = {}
    out.update(slices("DSPanel", lambda st: T.nine_cut(T.chip(side, s, s))))
    out.update(slices("DSPanelLit", lambda st: T.nine_cut(T.chip(side, s, s, lit=True))))
    out.update(slices("DSPlate", lambda st: T.nine_cut(T.panel(side, s, s))))
    out.update(slices("DSWell", lambda st: T.nine_cut(well, T.WELL_NINE)))
    out.update(slices("DSButton", lambda st: T.three_cut(T.key(side, *BUTTON, st)), STATES))
    out.update(slices("DSDrop", lambda st: drop_part(side, st), KEY_STATES))
    out["DSFieldR"] = T.key(side, 200, T.DROP_H, "").crop((200 - T.FIELD_R, 0, 200, T.DROP_H))
    out.update(slices("DSRow", lambda st: list_row(side, st), ("H", "P")))
    for s_ in T.SIDES + ("Random",):
        out["DSCoin" + s_] = T.coin(s_, COIN)
    for kind in T.LAMPS:
        out["DSLamp" + kind.capitalize()] = T.lamp(kind, LAMP)
    return out


def card(side, hero=False, back=False):
    w, h = CARD
    if back:
        return T.panel(side, w, h, recess=(8, 8, w - 8, h - 8))
    rule = (CAMEO_AT[0] - 1.8, CAMEO_AT[3] + 1.2, CAMEO_AT[2] + 1.8, CAMEO_AT[3] + 2.4) if hero else None
    return T.panel(side, w, h, [CAMEO_AT], hero, rule, bands=[PRICE_AT])


def hud(side):
    """The match HUD's pieces, the names LaneHud.cpp reads."""
    build = T.build_of(side)
    out = {"LaneCard": card(side), "LaneCardHero": card(side, hero=True), "LaneCardBack": card(side, back=True),
           "LaneDone": T.lamp("ready", 20), "LaneTabLock": tab_lock(), "LaneAirMark": air_mark(),
           "LaneToggleOn": toggle(side, True), "LaneToggleOff": toggle(side, False),
           "LaneMapFortress": map_fortress(build), "LaneMapTower": map_tower(build),
           "LaneEnemyPlotFrame": T.chip(side, 212, 80), "LaneAbilitiesPanel": T.chip(side, 192, 304, 8),
           "LaneIncomeIcon": T.flat_glyph(64, 54, income_glyph(build), 2.0, 32, 27, T.INK["gold"]),
           "LaneMoneyChip": T.chip(side, 200, 66), "LaneLaneChip": T.chip(side, 490, 66), "LaneYourChip": T.chip(side, 96, 66),
           "LaneBarChip": T.chip(side, 490, 14, 4), "LaneCountChip": T.chip(side, 84, 36),
           "LaneCoin": T.coin(side, COIN),"LaneYourRing": T.ring(24, 11, 3)}
    for n in (1, 2, 3):
        for s in STATES:
            out["LaneTier%d%s" % (n, s)] = T.key(side, 68, 30, s, tier_glyph(build, n) if n < 3 or s != "I" else (), 0.62)
    for s in STATES:
        out["LaneAbilitiesKey" + s] = T.key(side, 44, 44, s, abilities_glyph(build), 0.7)
    return out


def end_card(side, won):
    return T.panel(side, *END_CARD, END_CARD_WELLS, won)


def end_result(side, won):
    return T.panel(side, *END_RESULT, END_RESULT_WELLS, won, END_RESULT_RULE if won else None)


def end_header(side):
    return T.chip(side, *END_HEADER, lit=True)


def end_timeline(side):
    """The timeline chip drawn whole and cut in three: caps CAP wide, a middle TIMELINE_MID wide the code stretches."""
    return T.three_cut(T.chip(side, *END_TIMELINE, TIMELINE_INSET), T.CAP, TIMELINE_MID)


def end(side):
    """The end screen's pieces, the names LaneWarEndScreen.cpp reads."""
    build = T.build_of(side)
    out = {"LaneEndCard": end_card(side, False), "LaneEndCardWon": end_card(side, True),
           "LaneEndResult": end_result(side, False), "LaneEndResultWon": end_result(side, True), "LaneEndHeader": end_header(side)}
    out.update({"LaneEndTimeline" + part: img for part, img in end_timeline(side).items()})
    for name, kind in END_KEYS.items():
        for s in KEY_STATES:
            out["LaneEnd%s%s" % (name, s)] = T.key(side, *END_KEY, s, button_layers(kind, build), 0.62)
    for name, kind in STATS.items():
        out["LaneStat" + name] = stat_glyph(kind, STAT)
    return out


def lobby(side):
    """The lobby's own pieces; its plates, keys, dropdowns, coins and lamps are the shared ones."""
    out = {"LobbyIsland": island_art(*ISLAND), "LobbyIslandSmall": island_art(*ISLAND_SMALL)}
    for s in KEY_STATES:
        out["LobbyPickTile" + s] = T.key(side, *PICK_TILE, s)
        out["LobbyBack" + s] = T.key(side, 72, 72, s, arrow_layers("back"), 0.62)
        out["LobbySend" + s] = T.key(side, 48, 48, s, arrow_layers("send"), 0.62)
    out.update(lobby_glyphs())
    for s in T.SIDES:
        out["LobbyHeadquarters" + s] = headquarters(s)
    return out


def headquarters(side):
    """The side's command centre button picture as a square the well's size, like a general's portrait: the cameo's
    centre square (its sides cut), enlarged with Lanczos and a light unsharp mask, which held its edges where a plain
    resize went soft."""
    texture, crop = CAMEOS[side]
    member = "data\\english\\art\\textures\\" + texture.lower()
    with open(CAMEO_ARCHIVE, "rb") as archive:
        found = [(offset, size) for path, offset, size in list(bigfile._read_index(archive)) if path.lower() == member]
        assert found, "%s has no %s" % (CAMEO_ARCHIVE, member)
        archive.seek(found[0][0])
        cameo = Image.open(io.BytesIO(archive.read(found[0][1]))).convert("RGBA").crop(crop)
    side_length = min(cameo.size)
    left, top = (cameo.width - side_length) // 2, (cameo.height - side_length) // 2
    square = cameo.crop((left, top, left + side_length, top + side_length))
    return square.resize((WELL_PICTURE, WELL_PICTURE), Image.LANCZOS).filter(
        ImageFilter.UnsharpMask(radius=1.2, percent=70, threshold=2))


def pieces(side):
    """Every piece of the side's sheet by its name after the side's prefix."""
    out = shared(side)
    for part in (hud(side), end(side), lobby(side)):
        assert not set(part) & set(out), "a name is drawn twice: %s" % sorted(set(part) & set(out))
        out.update(part)
    return out

# ---------------------------------------------------------------------------------------------- the sheet


def texture_name(side):
    return "ReforgedHud%sDirectStrike" % side


def packing(sizes, width=SHEET_W):
    """Bottom left on a skyline, tallest first, names breaking ties, so every side packs the same: each piece goes
    where its top is highest, leftmost of those, SHEET_GAP clear of every piece above and beside it."""
    at = {}
    skyline = [EDGE] * width
    for name, (w, h) in sorted(sizes.items(), key=lambda item: (-item[1][1], -item[1][0], item[0])):
        y, x = min((max(skyline[x:x + w]), x) for x in range(EDGE, width - EDGE - w + 1))
        at[name] = (x, y, x + w, y + h)
        for c in range(max(0, x - SHEET_GAP + 1), min(width, x + w + SHEET_GAP - 1)):
            skyline[c] = max(skyline[c], y + h + SHEET_GAP)
    height = 1 << (max(r[3] for r in at.values()) + EDGE - 1).bit_length()
    rects = list(at.items())
    for i, (n, a) in enumerate(rects):
        for m, b in rects[i + 1:]:
            assert a[2] + 3 <= b[0] or b[2] + 3 <= a[0] or a[3] + 3 <= b[1] or b[3] + 3 <= a[1], "%s meets %s" % (n, m)
    return at, (width, height)


def layout(named):
    """The side's packing: names whose pictures come out the same pixels share the first one's rect.  Returns every
    name's rect, the name whose picture each one shows, and the sheet's size."""
    first, of = {}, {}
    for name in sorted(named):
        fingerprint = (named[name].size, named[name].tobytes())
        of[name] = first.setdefault(fingerprint, name)
    at, size = packing({n: named[n].size for n in first.values()})
    return {n: at[of[n]] for n in named}, of, size


def ini_text(side, rects, size):
    blocks = []
    for name in sorted(rects):
        x0, y0, x1, y1 = rects[name]
        blocks.append("MappedImage %s%s\n  Texture = %s.tga\n  TextureWidth = %d\n  TextureHeight = %d\n"
                      "  Coords = Left:%d Top:%d Right:%d Bottom:%d\n  Status = NONE\nEnd\n"
                      % (T.PREFIX[side], name, texture_name(side), size[0], size[1], x0, y0, x1, y1))
    return "\n".join(blocks)


def write_dds(sheet, path):
    sheet.save(path, pixel_format="DXT5")
    with open(path, "rb") as f:
        assert f.read(88)[84:88] == b"DXT5", "%s was not written as DXT5" % path


def ship(side, sheet, rects, size):
    """The texture and the mapped INI into Code/Data, and the same files into Run, each compared after the copy."""
    name = texture_name(side)
    dds = os.path.join(DATA, TEXTURES, name + ".dds")
    ini = os.path.join(DATA, MAPPED, name + ".ini")
    write_dds(sheet, dds)
    with open(ini, "w", encoding="ascii", newline="\r\n") as f:
        f.write(ini_text(side, rects, size))
    for src, sub in ((dds, TEXTURES), (ini, MAPPED)):
        dst = os.path.join(RUN, "Data" if sub == MAPPED else "", sub, os.path.basename(src))
        shutil.copyfile(src, dst)
        assert filecmp.cmp(src, dst, shallow=False), "%s differs from %s after the copy" % (dst, src)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--png", help="write the sheets and every piece as PNG here")
    ap.add_argument("--ship", action="store_true", help="write the textures and mapped INIs into Code/Data and Run")
    ap.add_argument("--side", choices=T.SIDES, help="one side only (all three by default)")
    args = ap.parse_args()
    assert args.png or args.ship, "nothing to write: give --png DIR, --ship or both"
    assert not (args.ship and os.environ.get(T.PALETTE_ENV)), "%s is set: a trial palette does not ship" % T.PALETTE_ENV
    for side in [args.side] if args.side else T.SIDES:
        named = pieces(side)
        rects, of, size = layout(named)
        print("%s: sheet %dx%d, %d names on %d pictures; %s" % ((side,) + size + (len(rects), len(set(of.values())),
              ", ".join("%s shows %s" % (n, of[n]) for n in sorted(rects) if of[n] != n))))
        for name in sorted(rects, key=lambda n: (rects[n][1], rects[n][0])):
            r = rects[name]
            print("  %-24s %4dx%-4d Left:%d Top:%d Right:%d Bottom:%d" % ((name, r[2] - r[0], r[3] - r[1]) + r))
        sheet = Image.new("RGBA", size, (0, 0, 0, 0))
        for name in set(of.values()):
            H.put(sheet, named[name], rects[name])
        if args.png:
            os.makedirs(os.path.join(args.png, side), exist_ok=True)
            sheet.save(os.path.join(args.png, texture_name(side) + ".png"))
            for name, img in named.items():
                img.save(os.path.join(args.png, side, T.PREFIX[side] + name + ".png"))
        if args.ship:
            ship(side, sheet, rects, size)
        print(side, "done", flush=True)


if __name__ == "__main__":
    main()
