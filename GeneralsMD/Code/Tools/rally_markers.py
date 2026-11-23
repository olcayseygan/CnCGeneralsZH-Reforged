"""Machined Reticle rally point: the atlas W3DRallyMarkers draws a production building's rally point from.

The rally point is one of the Machined Reticle ground markers (ground_markers.py) and is drawn the
same way: signed distance fields in texels, the HUD's light from the top left, a near-black shell, a
lit steel lip and a tinted inlay. It has a sheet of its own so it never waits on the other one.

    ReforgedRallyMarkers.png      the sheet, for a look
    ReforgedRallyMarkers.dds      DXT5, three mip levels, what the game reads (Data/Art/Textures)
    ReforgedRallyMarkers.ini      MappedImage blocks (Data/INI/MappedImages/HandCreated)
    pieces.json                   rect, size and anchor of every piece

Units: k is the rally point's band unit in world units (W3DRallyMarkers: 0.65 times the zoom).
Bands, rails, studs, chevrons and ticks are drawn at 4 texels a k, the flags at 8, so a flag keeps
its machining at the closest zoom. Every side plants its own flag: a pole with its own foot and
finial, and a cloth of its own cut (America an arrow point with swept chevrons, China a stepped fly
with a lacquer band and rivets, the GLA a torn, out of square rag with a bullet hole and a welded
patch, Turkiye a 30 degree parallelogram with an LED hairline). The cloth is grey and takes the
owner's colour from the vertex colour; the game ripples it.

    python rally_markers.py --out DIR [--dds Data/Art/Textures] [--ini Data/INI/MappedImages/HandCreated]

Needs Pillow 11 or later, numpy and scipy, and ground_markers.py beside it (its distance functions).
"""
import argparse
import json
import math
import os
import shutil

import numpy as np
from PIL import Image

from ground_markers import (DARK, LIGHT, LP, STEEL, Layer, bevel, chev, cov, grid, sd_box, sd_oct, sd_poly,
                            sd_seg, write_dds)

W, H = 1024, 1024
T = 4.0             # texels per k: bands, rails, studs, chevrons, ticks
F = 8.0             # texels per k: poles and cloths
SIDES = ("America", "China", "Gla", "Turkiye")
PIECES = {}


def piece(name, layer, **meta):
    PIECES[name] = dict(img=layer.rgba(), meta=meta)


def strip_d(across):
    h = int(across * T)
    return (h / 2 - (np.arange(h) + 0.5))[:, None] / T * np.ones((1, 8)), h


# ---------------------------------------------------------------- strips

def bands():
    # the frame's band, 10k across: shell, a lit pair of steel lips, the inlay groove
    d, h = strip_d(10)
    px = 1 / T
    L = Layer(8, h)
    L.over(DARK, cov(np.abs(d) - 4.6, px) * 0.78)
    piece("RallyBandShell", L, kind="strip", across_k=10, tint=False)
    dl = np.abs(d) - 3.1
    Hf = 2.2 * T * np.clip(-dl / 2.4, 0, 1) ** 0.6
    for tag, ly in (("A", -1), ("B", 1)):
        L = Layer(8, h)
        L.over(STEEL[None, None] * bevel(Hf, np.array([0, ly * LP, LIGHT[2]]))[..., None], cov(dl, px))
        piece("RallyBandSteel" + tag, L, kind="strip", across_k=10, tint=False)
    L = Layer(8, h)
    L.over((1, 1, 1), cov(np.abs(d) - 0.95, px))
    piece("RallyBandInlay", L, kind="strip", across_k=10, tint=True)
    # the lane: two steel rails in a dark keyline either side of a groove the inlay lies in, 6k across
    d, h = strip_d(6)
    L = Layer(8, h)
    L.over(DARK, cov(np.abs(d) - 2.8, px) * 0.72)
    rail = np.abs(np.abs(d) - 1.75) - 0.55
    L.over(STEEL * 1.12, cov(rail, px) * 0.95)
    piece("RallyRail", L, kind="strip", across_k=6, tint=False)
    # the ping, added
    d, h = strip_d(6)
    L = Layer(8, h)
    L.over((1, 1, 1), cov(np.abs(d) - 2.2, px))
    piece("RallyPing", L, kind="strip", across_k=6, tint=True, blend="add")


# ---------------------------------------------------------------- small pieces

def flat_keyed(name, d, key, ka, fa, scale, **meta):
    px = 1 / scale
    L = Layer(d.shape[1], d.shape[0])
    L.over(DARK, cov(d - key, px) * ka)
    L.over((1, 1, 1), cov(d, px) * fa)
    piece(name, L, kind="quad", tint=True, **meta)


def smalls():
    # chevron: tip at the anchor, pointing along +u
    w, h = int(12 * T), int(16 * T)
    tipx = 3 * T
    X, Y = grid(w, h, w - tipx, h / 2, T)
    flat_keyed("RallyChevron", chev(X, Y, 6.5, 6.0, 2.4), 1.8, 0.8, 0.95, T, size_k=[12, 16],
               anchor=[(w - tipx) / w, 0.5])
    # range tick, long axis along v
    w, h = int(4 * T), int(10 * T)
    X, Y = grid(w, h, w / 2, h / 2, T)
    flat_keyed("RallyTick", np.maximum(np.abs(X) - 0.6, np.abs(Y) - 3.4), 1.2, 0.7, 0.9, T, size_k=[4, 10],
               anchor=[0.5, 0.5])
    # octagon stud, steel, and the dot in it, tinted
    s = int(12 * T)
    X, Y = grid(s, s, s / 2, s / 2, T)
    L = Layer(s, s)
    L.over(DARK, cov(sd_oct(X, Y, 5.6), 1 / T) * 0.9)
    so = sd_oct(X, Y, 3.9)
    L.over(STEEL[None, None] * bevel(1.8 * T * np.clip(-so / 1.8, 0, 1))[..., None], cov(so, 1 / T))
    piece("RallyStud", L, kind="quad", size_k=[12, 12], anchor=[0.5, 0.5], tint=False)
    s = int(5 * T)
    X, Y = grid(s, s, s / 2, s / 2, T)
    L = Layer(s, s)
    L.over((1, 1, 1), cov(sd_oct(X, Y, 1.9), 1 / T))
    piece("RallyStudDot", L, kind="quad", size_k=[5, 5], anchor=[0.5, 0.5], tint=True)


# ---------------------------------------------------------------- flags

def machined(L, d, base, Hk=1.4, scale=F, groove=None):
    """A face lit from the top left over its distance field d (k): keyline, bevel, optional groove."""
    px = 1 / scale
    L.over(DARK, cov(d - 0.9, px) * 0.85)
    Hf = 2.4 * scale * np.clip(-d / Hk, 0, 1) ** 0.7
    face = base * (0.30 + 0.70 * bevel(Hf)) / 1.05
    if groove is not None:
        face = face * (1.0 - 0.5 * cov(np.abs(groove) - 0.35, px))
    L.over(np.clip(face, 0, 1)[..., None] * np.ones(3), cov(d, px))


POLE_W, POLE_H, POLE_FOOT = 12.0, 44.0, 3.0      # k; the foot is POLE_FOOT over the bottom edge
CLOTH_W, CLOTH_H, CLOTH_PAD = 31.0, 20.0, 2.5    # k; the hoist's top corner is CLOTH_PAD in from top left
HOIST_Y = 36.0                                     # k up the pole the cloth's top edge hangs from


def pole(side):
    w, h = int(POLE_W * F), int(POLE_H * F)
    X, Y = grid(w, h, w / 2, h - POLE_FOOT * F, F)
    px = 1 / F
    L = Layer(w, h)
    rod_top = 37.0
    if side == "Gla":            # a length of rebar, kinked near the top
        rod = np.minimum(sd_seg(X, Y, 0, 1, 0, 30) - 0.8, sd_seg(X, Y, 0, 30, 1.6, rod_top) - 0.8)
    else:
        rod = sd_seg(X, Y, 0, 1, 0, rod_top) - 0.85
    # the rod as a lathe-turned cylinder: lit across its width
    L.over(DARK, cov(rod - 0.7, px) * 0.85)
    cyl = np.clip(1 - (X / 0.85) ** 2, 0, 1) ** 0.5
    shade = 0.45 + 0.55 * np.clip(1 - np.abs(X + 0.35) / 0.9, 0, 1) + 0.1 * cyl
    L.over(np.clip(STEEL * shade[..., None] * 1.1, 0, 1), cov(rod, px))
    if side == "America":        # octagon foot, octagon finial
        foot = sd_oct(X, Y, 3.2)
        top = sd_oct(X, Y - 38.6, 2.4)
    elif side == "China":        # stepped foot, a two-terrace finial with a ball
        foot = np.minimum(sd_box(X, Y + 0.6, 3.8, 1.2), sd_box(X, Y - 1.2, 2.4, 1.0))
        top = np.minimum(np.minimum(sd_box(X, Y - 37.6, 2.2, 0.8), sd_box(X, Y - 39.0, 1.4, 0.7)),
                         np.hypot(X, Y - 40.6) - 1.0)
    elif side == "Gla":          # a crooked plate, a hex nut on the kink
        xr, yr = X * math.cos(0.14) + Y * math.sin(0.14), -X * math.sin(0.14) + Y * math.cos(0.14)
        foot = sd_box(xr, yr, 3.6, 1.3)
        a = np.arctan2(Y - 30, X)
        r = np.hypot(X, Y - 30)
        hexa = r * np.cos((a + math.pi / 6) % (math.pi / 3) - math.pi / 6) - 1.5
        top = np.minimum(hexa, np.hypot(X - 1.6, Y - rod_top) - 1.1)
    else:                        # Turkiye: a flat hexagon foot, a faceted spear tip
        a = np.arctan2(Y, X)
        r = np.hypot(X, Y)
        foot = r * np.cos(a % (math.pi / 3) - math.pi / 6) - 3.0
        top = sd_poly(X, Y, [(-1.9, 36.4), (1.9, 36.4), (0, 42.0)])
    for d in (foot, top):
        machined(L, d, STEEL[0] * 1.2, Hk=1.0)
    if side == "Turkiye":        # the spear's two facets
        right = (X > 0) & (top < 0)
        L.c[right] *= 0.72
    piece("RallyPole" + side, L, kind="quad", size_k=[POLE_W, POLE_H], anchor=[0.5, 1 - POLE_FOOT / POLE_H],
          tint=False, hoist_k=[0.85, HOIST_Y])


def cloth(side):
    """The cloth in its own k, x along the fly from the hoist (0), y up from its top edge (0)."""
    w, h = int(CLOTH_W * F), int(CLOTH_H * F)
    X, Y = grid(w, h, CLOTH_PAD * F, CLOTH_PAD * F, F)
    L = Layer(w, h)
    groove = None
    if side == "America":
        d = sd_poly(X, Y, [(0, 0), (20, 0), (27.5, -7.5), (20, -15), (0, -15)])
        sx = X - 6.0
        groove = np.minimum(chev(sx - 6, Y + 7.5, 5.5, 5.5, 0.01), chev(sx, Y + 7.5, 5.5, 5.5, 0.01))
        machined(L, d, 0.9, groove=groove)
    elif side == "China":
        d = sd_poly(X, Y, [(0, 0), (26, 0), (26, -5), (23, -5), (23, -10), (20, -10), (20, -15), (0, -15)])
        machined(L, d, 0.9)
        band = (np.abs(Y + 7.5) < 1.6) & (d < -0.3)
        L.c[band] *= 0.5            # the lacquer band: the owner's colour, deep
        for yy in (-2.6, -12.4):
            for xx in np.arange(3.0, 19.0, 4.0):
                rv = np.hypot(X - xx, Y - yy) - 0.6
                L.over(np.ones(3) * 1.0, cov(rv, 1 / F) * 0.9)
                L.over(DARK, cov(np.abs(rv + 0.25) - 0.18, 1 / F) * 0.5 * (Y > yy))
    elif side == "Gla":
        rag = [(0, 0), (25, 1.2), (23, -2), (26, -4.5), (22.5, -7), (24.5, -10), (21, -12.5), (22, -15.5),
               (0, -14.6)]
        d = sd_poly(X, Y, rag)
        d = np.maximum(d, -(np.hypot(X - 17.0, Y + 6.2) - 1.3))     # a bullet hole
        machined(L, d, 0.86)
        patch = sd_poly(X, Y, [(5.5, -3.0), (12.8, -2.2), (12.2, -9.6), (5.0, -10.4)])
        inside = (patch < 0) & (d < 0)
        L.c[inside] *= 1.12
        L.over(DARK, cov(np.abs(patch) - 0.3, 1 / F) * 0.6 * (d < 0))       # the weld seam
        for (bx, by) in ((6.6, -4.0), (11.6, -8.6)):
            bolt = np.hypot(X - bx, Y - by) - 0.75
            machined(L, bolt, 1.0, Hk=0.5)
    else:
        d = sd_poly(X, Y, [(0, 0), (26, 0), (17.34, -15), (0, -15)])
        inner = sd_poly(X, Y, [(1.6, -1.6), (23.2, -1.6), (16.4, -13.4), (1.6, -13.4)])
        machined(L, d, 0.78)
        led = cov(np.abs(inner) - 0.32, 1 / F) * (d < 0)
        L.over(np.ones(3), led)      # the LED hairline: full white, so the owner's colour lights it
    piece("RallyCloth" + side, L, kind="quad", size_k=[CLOTH_W, CLOTH_H],
          anchor=[CLOTH_PAD / CLOTH_W, CLOTH_PAD / CLOTH_H], tint=True)


# ---------------------------------------------------------------- pack, write

G = 8
SLOT = 16
MIPS = 3


def pack():
    names = sorted(PIECES, key=lambda n: (-PIECES[n]["img"].shape[0], n))
    up = lambda v: (v + SLOT - 1) // SLOT * SLOT
    sx, sy, row = 0, 0, 0
    sheet = np.zeros((H, W, 4))
    for n in names:
        img = PIECES[n]["img"]
        h, w = img.shape[:2]
        sw, sh = up(w + 2 * G), up(h + 2 * G)
        if sx + sw > W:
            sx, sy, row = 0, sy + row, 0
        assert sy + sh <= H, "atlas full at %s" % n
        sheet[sy:sy + sh, sx:sx + sw] = np.pad(img, ((G, sh - h - G), (G, sw - w - G), (0, 0)), mode="edge")
        PIECES[n]["meta"]["rect"] = [sx + G, sy + G, sx + G + w, sy + G + h]
        sx += sw
        row = max(row, sh)
    return sheet


def mip_chain(sheet):
    levels = [sheet]
    for _ in range(MIPS - 1):
        s = levels[-1]
        levels.append(0.25 * (s[0::2, 0::2] + s[1::2, 0::2] + s[0::2, 1::2] + s[1::2, 1::2]))
    return levels


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--out", required=True)
    ap.add_argument("--dds")
    ap.add_argument("--ini")
    args = ap.parse_args()
    os.makedirs(args.out, exist_ok=True)
    bands()
    smalls()
    for s in SIDES:
        pole(s)
        cloth(s)
    sheet = pack()
    assert np.isfinite(sheet).all(), "a piece came out with NaN texels"
    Image.fromarray((np.clip(sheet, 0, 1) * 255 + 0.5).astype(np.uint8)).save(os.path.join(args.out, "ReforgedRallyMarkers.png"))
    dds = os.path.join(args.out, "ReforgedRallyMarkers.dds")
    write_dds(dds, mip_chain(sheet))
    meta = {n: PIECES[n]["meta"] for n in sorted(PIECES)}
    with open(os.path.join(args.out, "pieces.json"), "w") as f:
        json.dump(dict(texture="ReforgedRallyMarkers.dds", width=W, height=H, texels_per_k=T, flag_texels_per_k=F,
                       mips=MIPS, gutter=G, pieces=meta), f, indent=1)
    lines = ["; Machined Reticle rally point (ReforgedRallyMarkers.dds, %dx%d DXT5, %d mip levels)." % (W, H, MIPS),
             "; Generated by Tools/rally_markers.py; W3DRallyMarkers.cpp lays every piece on the ground.",
             "; Right and Bottom are exclusive. Every rect has a %d texel gutter of its own edge round it." % G, ""]
    for n, m in meta.items():
        l, t, r, b = m["rect"]
        lines += ["MappedImage %s" % n, "  Texture = ReforgedRallyMarkers.tga", "  TextureWidth = %d" % W,
                  "  TextureHeight = %d" % H, "  Coords = Left:%d Top:%d Right:%d Bottom:%d" % (l, t, r, b),
                  "  Status = NONE", "End", ""]
    ini = os.path.join(args.out, "ReforgedRallyMarkers.ini")
    with open(ini, "w", newline="\n") as f:
        f.write("\n".join(lines))
    if args.dds:
        shutil.copyfile(dds, os.path.join(args.dds, "ReforgedRallyMarkers.dds"))
    if args.ini:
        shutil.copyfile(ini, os.path.join(args.ini, "ReforgedRallyMarkers.ini"))
    print(len(meta), "pieces")


if __name__ == "__main__":
    main()
