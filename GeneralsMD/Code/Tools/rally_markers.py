"""The rally point: the atlas W3DRallyMarkers draws a production building's rally point from.

The rally point is one of the ground markers (ground_markers.py) and since 2026-11-26 is drawn in the
mouse cursors' hand (cursors.py): signed distance fields in texels, every shape one flat colour with
a dark line round it and at most a half-bright half toward the bottom right, no steel bevel. It has
a sheet of its own so it never waits on the other one.

    ReforgedRallyMarkers.png      the sheet, for a look
    ReforgedRallyMarkers.dds      DXT5, four mip levels, what the game reads (Data/Art/Textures)
    ReforgedRallyMarkers.ini      MappedImage blocks (Data/INI/MappedImages/HandCreated)
    pieces.json                   rect, size and anchor of every piece

The pennant (owner's pick of three, 2026-11-26, experiments/rally-refined): a hairline pole with a slim
pennant on it, a fine ring on the ground round its foot and a dashed rail from the building. The first
cut, a side's frame and flag with block arrows on a double rail, read as heavy. A stroke is the band's
two pieces stretched to whatever the code asks; the pennant is the only shaped piece, one cut a side,
the silhouette alone telling them apart (America swallow-tailed, China stepped, the GLA torn, Turkiye
cut at 30 degrees), grey for the code to tint in the owner's colour.

Units: k is the rally point's unit in world units (W3DRallyMarkers' K_WORLD, K_BASE times the closest
zoom, the same at every zoom), about 2.24 pixels at 1080 across the screen at the closest zoom, so the
cursors' one pixel dark line is KEYLINE k there.
Everything is drawn at 4 texels a k, in four mip levels.

    python rally_markers.py --out DIR [--dds Data/Art/Textures] [--ini Data/INI/MappedImages/HandCreated]

Needs Pillow 11 or later, numpy and scipy, and ground_markers.py beside it (its distance functions).
"""
import argparse
import json
import os
import shutil

import numpy as np
from PIL import Image

from ground_markers import DARK, Layer, cov, grid, sd_poly, write_dds

W, H = 512, 512
T = 4.0             # texels per k
SIDES = ("America", "China", "Gla", "Turkiye")
PIECES = {}
KEYLINE = 0.45      # k, the cursors' one pixel dark line round a face at 1080
HALF = 0.75         # the half-bright, of the lit half (the cursors' white to light grey)


def piece(name, layer, **meta):
    PIECES[name] = dict(img=layer.rgba(), meta=meta)


def strip_d(across):
    h = int(across * T)
    return (h / 2 - (np.arange(h) + 0.5))[:, None] / T * np.ones((1, 8)), h


# ---------------------------------------------------------------- strips

def bands():
    # the cursors' stroke: the dark line under, a flat fill on it that the code tints. Drawn 6k across and
    # stretched to the stroke's width: the fill is 0.47 of the dark, as the prototype's 1.8 pixel fill in
    # its 1 pixel line either side. The ring, the pole and the rail's dashes are all these two.
    d, h = strip_d(6)
    px = 1 / T
    L = Layer(8, h)
    L.over(DARK, cov(np.abs(d) - 2.85, px))
    piece("RallyBandShell", L, kind="strip", across_k=6, tint=False)
    L = Layer(8, h)
    L.over((1, 1, 1), cov(np.abs(d) - 1.35, px))
    piece("RallyBandInlay", L, kind="strip", across_k=6, tint=True)
    # the click's ping, added
    L = Layer(8, h)
    L.over((1, 1, 1), cov(np.abs(d) - 2.2, px))
    piece("RallyPing", L, kind="strip", across_k=6, tint=True, blend="add")


# ---------------------------------------------------------------- the foot and the pennants

def flat(L, d, base, split):
    """A face over its distance field d (k) as the cursors draw it: their dark line round it, one flat
    colour, and the half lying the way of split (a function of X, Y about the face's middle) at the
    half-bright."""
    px = 1 / T
    L.over(DARK, cov(d - KEYLINE, px) * 0.85)
    inside = d < 0
    X, Y = np.meshgrid(np.arange(d.shape[1]), -np.arange(d.shape[0]))
    cx, cy = X[inside].mean(), Y[inside].mean()
    face = base * np.where(split(X - cx, Y - cy), HALF, 1.0)
    L.over(np.clip(face, 0, 1)[..., None] * np.ones(3), cov(d, px))


def foot():
    # the white dot the pole stands on, its bottom right half light grey
    s = int(4 * T)
    X, Y = grid(s, s, s / 2, s / 2, T)
    L = Layer(s, s)
    flat(L, np.hypot(X, Y) - 1.15, 0.96, lambda x, y: x - y > 0)
    piece("RallyFoot", L, kind="quad", size_k=[4, 4], anchor=[0.5, 0.5], tint=False)


PENNANT_W, PENNANT_H, PENNANT_PAD = 15.0, 9.0, 1.0     # k, whole texels at T; the hoist's top corner is PENNANT_PAD in from top left

# x along the fly from the hoist, y up from the top edge, in k: 13 long and 7.1 deep, about the prototype's
# 29 by 16 pixels. Each side a silhouette of its own and nothing on it.
PENNANTS = {
    "America": [(0, 0), (12.9, -2.7), (8.9, -3.8), (12.9, -5.4), (0, -7.1)],                       # swallow-tail
    "China": [(0, 0), (13.0, -3.0), (13.0, -3.9), (9.0, -3.9), (9.0, -5.3), (4.5, -5.3), (4.5, -7.1),
              (0, -7.1)],                                                                            # stepped
    "Gla": [(0, 0), (12.6, -2.1), (10.6, -2.9), (12.8, -3.7), (10.0, -4.6), (11.4, -5.5), (0, -6.8)],  # torn
    "Turkiye": [(0, 0), (13.0, -1.5), (10.5, -5.8), (0, -6.6)],                                      # 30 degrees
}


def pennant(side):
    w, h = int(PENNANT_W * T), int(PENNANT_H * T)
    X, Y = grid(w, h, PENNANT_PAD * T, PENNANT_PAD * T, T)
    L = Layer(w, h)
    # pointing along +x, so the half below its middle is the one turned from the light (cursors.side)
    flat(L, sd_poly(X, Y, PENNANTS[side]), 0.9, lambda x, y: y < 0)
    piece("RallyPennant" + side, L, kind="quad", size_k=[PENNANT_W, PENNANT_H],
          anchor=[PENNANT_PAD / PENNANT_W, PENNANT_PAD / PENNANT_H], tint=True)


# ---------------------------------------------------------------- pack, write

G = 8           # gutter of each piece's own edge: one texel at the fourth level
SLOT = 32       # slots on a multiple of the fourth level's DXT block (4 texels there)
MIPS = 4        # a k is about 2.2 pixels on the screen at 1080, so the 4 texel k is minified about twice


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
    foot()
    for s in SIDES:
        pennant(s)
    sheet = pack()
    assert np.isfinite(sheet).all(), "a piece came out with NaN texels"
    Image.fromarray((np.clip(sheet, 0, 1) * 255 + 0.5).astype(np.uint8)).save(os.path.join(args.out, "ReforgedRallyMarkers.png"))
    dds = os.path.join(args.out, "ReforgedRallyMarkers.dds")
    write_dds(dds, mip_chain(sheet))
    meta = {n: PIECES[n]["meta"] for n in sorted(PIECES)}
    with open(os.path.join(args.out, "pieces.json"), "w") as f:
        json.dump(dict(texture="ReforgedRallyMarkers.dds", width=W, height=H, texels_per_k=T,
                       mips=MIPS, gutter=G, pieces=meta), f, indent=1)
    lines = ["; Rally point (ReforgedRallyMarkers.dds, %dx%d DXT5, %d mip levels)." % (W, H, MIPS),
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
