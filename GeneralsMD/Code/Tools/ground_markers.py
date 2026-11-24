"""Machined Reticle ground markers: the atlas W3DGroundMarkers draws every targeting marker from.

Every piece is drawn in texels from signed distance fields with the HUD's light from the top left,
packed into one 4096x2048 sheet with an 8 texel gutter of each piece's own edge, and written as

    ReforgedGroundMarkers.png      the sheet, for a look
    ReforgedGroundMarkers.dds      DXT5, three mip levels, what the game reads (Data/Art/Textures)
    ReforgedGroundMarkers.ini      MappedImage blocks (Data/INI/MappedImages/HandCreated)
    pieces.json                    rect, size, anchor and blend of every piece
    glyphs.png                     every glyph on one sheet, tinted, for a look

Units: k is the marker's band unit in world units. Band, stud, chevron, tick, segment and bracket
pieces are drawn at 4 texels a k; darts and order pieces at 3; glyphs at 125 texels for the glyph
size S, so the glyph of the widest zone is drawn at about 1.5 screen pixels a texel at the closest
zoom and keeps its machining: an engraved line inside every edge and lathe rings on the face, which
the mips average away at the default zoom. Lit pieces carry their light baked: band and segment
pieces as a pair lit from the outer (A) and inner (B) side, darts and spinning glyphs once per
heading.

Every power has its own glyph (GLYPHS); W3DGroundMarkers picks it by the power's name.

    python ground_markers.py --out DIR [--dds Data/Art/Textures] [--ini Data/INI/MappedImages/HandCreated]

Needs Pillow 11 or later (10.x writes an uncompressed DDS, and this refuses that), numpy and scipy.
"""
import argparse
import io
import json
import math
import os
import struct

import numpy as np
from PIL import Image
from scipy.ndimage import distance_transform_edt, gaussian_filter1d

W, H = 4096, 2048
T = 4.0                                    # texels per k
GS = 125.0                                 # texels per glyph size S
DARK = (0.035, 0.04, 0.045)
STEEL = np.array((0.58, 0.59, 0.60))
LIGHT = np.array([-0.5, -0.62, 0.6]) / np.linalg.norm([-0.5, -0.62, 0.6])   # (col, row, z)
LP = math.hypot(LIGHT[0], LIGHT[1])


# ---------------------------------------------------------------- distance functions (S = 1 for glyphs)

def sd_seg(x, y, ax, ay, bx, by):
    pax, pay = x - ax, y - ay
    bax, bay = bx - ax, by - ay
    h = np.clip((pax * bax + pay * bay) / max(bax * bax + bay * bay, 1e-12), 0, 1)
    return np.hypot(pax - bax * h, pay - bay * h)


def sd_poly(x, y, pts):
    n = len(pts)
    d = (x - pts[0][0]) ** 2 + (y - pts[0][1]) ** 2
    s = np.ones_like(x)
    j = n - 1
    for i in range(n):
        vix, viy = pts[i]
        vjx, vjy = pts[j]
        ex, ey = vjx - vix, vjy - viy
        wx, wy = x - vix, y - viy
        h = np.clip((wx * ex + wy * ey) / max(ex * ex + ey * ey, 1e-12), 0, 1)
        bx, by = wx - ex * h, wy - ey * h
        d = np.minimum(d, bx * bx + by * by)
        c1 = y >= viy
        c2 = y < vjy
        c3 = ex * wy > ey * wx
        s = np.where((c1 & c2 & c3) | (~c1 & ~c2 & ~c3), -s, s)
        j = i
    return s * np.sqrt(d)


def sd_oct(x, y, r):
    kx, ky, kz = -0.9238795325, 0.3826834323, 0.4142135623
    px, py = np.abs(x), np.abs(y)
    dd = np.minimum(kx * px + ky * py, 0)
    px, py = px - 2 * dd * kx, py - 2 * dd * ky
    dd = np.minimum(-kx * px + ky * py, 0)
    px, py = px + 2 * dd * kx, py - 2 * dd * ky
    px = px - np.clip(px, -kz * r, kz * r)
    py = py - r
    return np.hypot(px, py) * np.sign(py)


def sd_box(x, y, hx, hy, r=0.0):
    qx, qy = np.abs(x) - hx + r, np.abs(y) - hy + r
    return np.hypot(np.maximum(qx, 0), np.maximum(qy, 0)) + np.minimum(np.maximum(qx, qy), 0) - r


def sd_circle(x, y, cx, cy, r):
    return np.hypot(x - cx, y - cy) - r


def polar_rep(x, y, n, phase=0.0):
    r = np.hypot(x, y)
    a = np.arctan2(y, x) - phase
    step = 2 * np.pi / n
    loc = (a + step / 2) % step - step / 2
    return r, loc


def rot(x, y, a):
    c, s = math.cos(a), math.sin(a)
    return c * x + s * y, -s * x + c * y


def chev(sl, q, a, b, w):
    """V pointing along +s, tip at sl=0, arms back to (-a, +-b)."""
    return sd_seg(sl, np.abs(q), 0, 0, -a, b) - w / 2


def U(*ds):
    out = ds[0]
    for d in ds[1:]:
        out = np.minimum(out, d)
    return out


def cut(a, b):
    return np.maximum(a, -b)


def sd_arc(x, y, r, a0, a1, w):
    """A band of half width w round radius r from angle a0 to a1 (radians, counter-clockwise), round ends."""
    am = 0.5 * (a0 + a1)
    ha = 0.5 * (a1 - a0)
    xr, yr = rot(x, y, am)
    ang = np.abs(np.arctan2(yr, xr))
    rr = np.hypot(xr, yr)
    inside = np.abs(rr - r) - w
    ex, ey = r * math.cos(ha), r * math.sin(ha)
    end = np.hypot(xr - ex, np.abs(yr) - ey) - w
    return np.where(ang <= ha, inside, end)


def missile(x, y, cx, cy, ang, L=0.78, w=0.11, fin=3.2):
    """A finned missile, nose along `ang` from its middle (cx, cy), L long, w half wide."""
    xr, yr = rot(x - cx, y - cy, ang)
    body = sd_box(xr + 0.06 * L, yr, 0.38 * L, w, w * 0.6)
    nose = sd_poly(xr, yr, [(0.30 * L, w), (0.56 * L, 0), (0.30 * L, -w)])
    fins = sd_poly(xr, np.abs(yr), [(-0.44 * L, 0), (-0.22 * L, w), (-0.52 * L, fin * w), (-0.52 * L, 0)])
    return U(body, nose, fins)


def bomb(x, y, cx, cy, s=1.0, ang=-math.pi / 2):
    """A finned bomb, nose along `ang`."""
    xr, yr = rot(x - cx, y - cy, ang)
    xr, yr = xr / s, yr / s
    body = (np.hypot(xr / 1.75, yr) - 0.17) * 1.0
    tail = sd_box(xr + 0.36, yr, 0.08, 0.06)
    fins = sd_poly(xr, np.abs(yr), [(-0.34, 0), (-0.30, 0.07), (-0.50, 0.21), (-0.52, 0)])
    return U(body, tail, fins) * s


def flame_poly(cx, cy, h, w, lean, n=40):
    pts = []
    for i in range(n + 1):
        u = i / n
        hw = w * math.sin(math.pi * (0.08 + 0.92 * u)) ** 0.7 * (1 - u) ** 0.55
        c = lean * u * u
        pts.append((cx + c + hw, cy - 0.45 * h + h * u))
    for i in range(n, -1, -1):
        u = i / n
        hw = w * math.sin(math.pi * (0.08 + 0.92 * u)) ** 0.7 * (1 - u) ** 0.55
        c = lean * u * u
        pts.append((cx + c - hw, cy - 0.45 * h + h * u))
    return pts


def drop_poly(cx, cy, r, tip, ang=math.pi / 2, n=32):
    """A teardrop: a round end of radius r, its point `tip` from the centre along `ang`."""
    pts = []
    t = math.asin(min(0.999, r / tip))
    for i in range(n + 1):
        a = ang + (math.pi / 2 + t) + (2 * math.pi - 2 * (math.pi / 2 + t)) * i / n
        pts.append((cx + r * math.cos(a), cy + r * math.sin(a)))
    pts.append((cx + tip * math.cos(ang), cy + tip * math.sin(ang)))
    return pts


def mine(x, y, s=1.0):
    x, y = x / s, y / s
    r = np.hypot(x, y)
    _, loc = polar_rep(x, y, 8, math.pi / 8)
    spikes = sd_seg(r * np.cos(loc), np.abs(r * np.sin(loc)), 0.40, 0, 0.70, 0) - 0.075
    knobs = sd_circle(r * np.cos(loc), r * np.sin(loc), 0.72, 0, 0.1)
    body = r - 0.44
    d = U(body, spikes, knobs)
    d = cut(d, np.abs(r - 0.25) - 0.035)              # the fuse ring cut into the face
    return d * s


def glyph_sdf(kind, x, y, spin=0.0):
    """Every glyph in S = 1 units, inside a radius of about 1."""
    if spin:
        x, y = rot(x, y, spin)
    r = np.hypot(x, y)
    if kind == "guard":
        return sd_poly(x, y, [(-.62, .70), (.62, .70), (.62, .02), (0, -.86), (-.62, .02)])
    if kind == "barrage":
        d = np.minimum(sd_seg(np.abs(x), y, .40, 0, 1, 0), sd_seg(x, np.abs(y), 0, .40, 0, 1)) - .06
        return np.minimum(d, sd_oct(x, y, .16))
    if kind == "nuke":
        a = np.arctan2(y, x) - np.pi / 2
        step = 2 * np.pi / 3
        loc = (a + step / 2) % step - step / 2
        blade = np.maximum(np.abs(r - .66) - .34, r * np.sin(np.clip(np.abs(loc) - np.pi / 6, -1.5, 1.5)))
        return np.minimum(blade, r - .17)
    if kind == "drop":
        return np.minimum(chev(-y - .05, x, .5, .62, .17), chev(-y + .45, x, .5, .62, .17))
    if kind == "strike":
        d = np.abs((np.abs(x) + np.abs(y)) - .8) / 1.4142 - .08
        return np.minimum(d, r - .13)
    if kind == "reticle":
        ring = np.abs(r - .72) - .07
        _, loc = polar_rep(x, y, 4, np.pi / 4)
        ring = np.maximum(ring, -(np.abs(r * np.sin(loc)) - .32))
        ticks = np.minimum(sd_seg(np.abs(x), y, .42, 0, 1, 0), sd_seg(x, np.abs(y), 0, .42, 0, 1)) - .05
        return np.minimum(np.minimum(ring, ticks), r - .08)

    # ---- the powers' own
    if kind == "para":          # a canopy on three risers over its load
        dome = np.maximum(np.hypot(x, (y + 0.02) * 1.25) - 0.86, 0.02 - y)
        for sx in (-0.57, 0.0, 0.57):
            dome = cut(dome, sd_circle(x, y, sx, -0.02, 0.23))
        dome = cut(dome, U(sd_seg(x, y, -0.29, 0.10, -0.40, 0.62), sd_seg(x, y, 0.29, 0.10, 0.40, 0.62)) - 0.025)
        risers = U(sd_seg(x, y, -0.80, 0.02, 0, -0.56), sd_seg(x, y, 0.80, 0.02, 0, -0.56),
                   sd_seg(x, y, 0, 0.02, 0, -0.56)) - 0.045
        load = sd_box(x, y + 0.72, 0.16, 0.14, 0.03)
        return U(dome, risers, load)
    if kind == "crate":         # a braced crate
        frame = np.abs(sd_box(x, y, 0.64, 0.64, 0.06)) - 0.085
        planks = sd_box(x, y, 0.62, 0.05)
        brace = cut(sd_seg(x, y, -0.5, -0.5, 0.5, 0.5) - 0.08, sd_box(x, y, 0.62, 0.11))
        return cut(U(frame, planks, brace), -sd_box(x, y, 0.72, 0.72))
    if kind == "leaflet":       # three sheets fanned, lines of print on the front one
        def sheet(cx, cy, a):
            xr, yr = rot(x - cx, y - cy, a)
            return sd_box(xr, yr, 0.34, 0.46, 0.04)
        front = sheet(0.0, -0.05, 0.0)
        back = U(sheet(-0.36, 0.08, math.radians(16)), sheet(0.36, 0.08, math.radians(-16)))
        back = cut(back, front - 0.07)
        lines = U(*[sd_box(x, y - yy, 0.22, 0.03) for yy in (0.24, 0.08, -0.08, -0.24)])
        front = cut(front, cut(lines, sd_box(x - 0.22, y + 0.24, 0.12, 0.05)))
        return U(front, back)
    if kind == "volley":        # A-10: three missiles diving in, the middle one leading
        return U(missile(x, y, -0.50, 0.14, -math.pi / 2, 0.84, 0.12, 2.0), missile(x, y, 0.0, -0.10, -math.pi / 2, 1.0, 0.14, 2.0),
                 missile(x, y, 0.50, 0.14, -math.pi / 2, 0.84, 0.12, 2.0))
    if kind == "bombs":         # a stick of three bombs
        return U(bomb(x, y, -0.48, 0.40, 0.95), bomb(x, y, 0.0, 0.05, 0.95), bomb(x, y, 0.48, -0.30, 0.95))
    if kind == "blast":         # one heavy bomb in its blast ring
        _, loc = polar_rep(x, y, 12, 0.0)
        rays = sd_seg(r * np.cos(loc), r * np.sin(loc), 0.72, 0, 0.96, 0) - 0.055
        return U(bomb(x, y, 0.0, 0.02, 1.65), rays, np.abs(r - 0.62) - 0.03)
    if kind == "flame":         # a flame with its inner tongue
        outer = sd_poly(x, y, flame_poly(0.0, -0.08, 1.75, 0.62, 0.10))
        inner = sd_poly(x, y, flame_poly(0.02, -0.30, 0.95, 0.27, -0.06))
        side = sd_poly(x, y, flame_poly(-0.52, -0.38, 0.85, 0.22, -0.12))
        side2 = sd_poly(x, y, flame_poly(0.55, -0.42, 0.7, 0.19, 0.10))
        body = U(cut(outer, inner + 0.06), side, side2)
        return U(body, inner + 0.02 - 0.0)
    if kind == "scud":          # a salvo of three, fanned
        return U(missile(x, y, -0.44, -0.05, math.radians(118), 0.82, 0.125), missile(x, y, 0.0, 0.05, math.pi / 2, 0.98, 0.14),
                 missile(x, y, 0.44, -0.05, math.radians(62), 0.82, 0.125))
    if kind == "beam":          # the beam coming down into its burn
        beam = sd_poly(x, y, [(-0.05, 0.0), (-0.20, 1.02), (0.20, 1.02), (0.05, 0.0)])
        beam = cut(beam, sd_poly(x, y, [(-0.015, 0.30), (-0.07, 1.1), (0.07, 1.1), (0.015, 0.30)]))
        _, loc = polar_rep(x, y, 10, math.pi / 2)
        rays = sd_seg(r * np.cos(loc), r * np.sin(loc), 0.34, 0, 0.58, 0) - 0.045
        burn = cut(U(np.abs(r - 0.78) - 0.04, rays), sd_box(x, y - 0.6, 0.27, 0.5))
        return U(beam, burn, cut(r - 0.24, r - 0.10))
    if kind == "missile":       # a cruise missile, wings out, diving
        xr, yr = rot(x, y, math.radians(-45))
        body = sd_box(xr + 0.05, yr, 0.66, 0.13, 0.07)
        nose = sd_poly(xr, yr, [(0.60, 0.13), (0.94, 0), (0.60, -0.13)])
        wings = sd_poly(xr, np.abs(yr), [(0.16, 0), (0.0, 0.58), (-0.16, 0.58), (-0.08, 0)])
        tail = sd_poly(xr, np.abs(yr), [(-0.50, 0), (-0.62, 0.24), (-0.72, 0.24), (-0.70, 0)])
        return U(body, nose, wings, tail)
    if kind == "ballistic":     # a ballistic missile coming straight down, its trail arcs over it
        m = missile(x, y, 0.0, -0.05, -math.pi / 2, 1.30, 0.13)
        arcs = U(sd_arc(x - 0.36, y - 0.55, 0.30, math.radians(100), math.radians(180), 0.035),
                 sd_arc(x + 0.36, y - 0.55, 0.30, math.radians(0), math.radians(80), 0.035))
        ground = U(sd_box(x - 0.48, y + 0.86, 0.20, 0.035), sd_box(x + 0.48, y + 0.86, 0.20, 0.035))
        return U(m, arcs, ground)
    if kind == "artillery":     # shells falling in, the burst under them
        def shell(cx, cy, s):
            xs, ys = (x - cx) / s, (y - cy) / s
            body = sd_box(xs, ys - 0.12, 0.13, 0.22, 0.02)
            nose = U(sd_poly(xs, ys, [(-0.13, -0.10), (0.0, -0.40), (0.13, -0.10)]), sd_circle(xs, ys, 0, -0.18, 0.13))
            band = sd_box(xs, ys - 0.30, 0.15, 0.03)
            return U(body, nose, band) * s
        burst = U(sd_arc(x, y + 0.62, 0.34, math.radians(20), math.radians(160), 0.045),
                  sd_arc(x, y + 0.62, 0.56, math.radians(35), math.radians(145), 0.04))
        return U(shell(0.0, 0.16, 1.15), shell(-0.48, 0.42, 0.85), shell(0.48, 0.42, 0.85), burst)
    if kind == "swarm":         # loitering munitions closing on the middle
        ds = []
        for i in range(6):
            a = math.pi / 2 + i * math.pi / 3
            cx, cy = 0.70 * math.cos(a), 0.70 * math.sin(a)
            xr, yr = rot(x - cx, y - cy, a + math.pi)
            ds.append(sd_poly(xr, yr, [(0.16, 0), (-0.12, 0.13), (-0.05, 0), (-0.12, -0.13)]))
        return U(*ds, np.abs(r - 0.22) - 0.05, r - 0.07)
    if kind == "drones":        # a quadcopter seen from above
        arms = U(sd_seg(x, y, -0.5, -0.5, 0.5, 0.5), sd_seg(x, y, -0.5, 0.5, 0.5, -0.5)) - 0.07
        rotors = U(*[np.abs(sd_circle(x, y, sx * 0.55, sy * 0.55, 0.30)) - 0.05 for sx in (-1, 1) for sy in (-1, 1)])
        hubs = U(*[sd_circle(x, y, sx * 0.55, sy * 0.55, 0.08) for sx in (-1, 1) for sy in (-1, 1)])
        body = sd_box(x, y, 0.20, 0.26, 0.07)
        return U(arms, rotors, hubs, body)
    if kind == "bolt":          # a bolt between two pulse arcs
        bolt = sd_poly(x, y, [(0.10, 0.96), (-0.40, 0.02), (-0.04, 0.02), (-0.20, -0.96), (0.42, 0.10), (0.06, 0.10), (0.34, 0.96)])
        arcs = U(sd_arc(x, y, 0.80, math.radians(140), math.radians(220), 0.05),
                 sd_arc(x, y, 0.80, math.radians(-40), math.radians(40), 0.05))
        return U(bolt, arcs)
    if kind == "jam":           # an emitter whose waves are struck through
        waves = U(*[U(sd_arc(x, y, rr, math.radians(-42), math.radians(42), 0.045),
                      sd_arc(x, y, rr, math.radians(138), math.radians(222), 0.045)) for rr in (0.36, 0.58, 0.80)])
        slash = sd_seg(x, y, -0.62, -0.78, 0.62, 0.78)
        return U(cut(U(waves, r - 0.15), slash - 0.12), slash - 0.055)
    if kind == "mine":
        return mine(x, y, 1.15)
    if kind == "frenzy":        # an eight-point burst, hollow
        _, loc = polar_rep(x, y, 8, math.pi / 2)
        lr = r * np.cos(np.abs(loc))
        star = sd_poly(r * np.cos(loc), np.abs(r * np.sin(loc)), [(0.0, 0.0), (0.98, 0.0), (0.40, 0.17), (0.0, 0.17)])
        star = U(star, r - 0.42)
        del lr
        return cut(star, r - 0.20)
    if kind == "wrench":        # an open-end wrench across the face
        xr, yr = rot(x, y, math.radians(45))
        handle = sd_box(xr + 0.18, yr, 0.52, 0.12, 0.05)
        head = sd_circle(xr, yr, 0.58, 0, 0.34)
        jaw = sd_box(xr - 0.82, yr, 0.30, 0.13)
        hole = sd_circle(xr, yr, -0.58, 0, 0.06)
        return cut(cut(U(handle, head), jaw), hole)
    if kind == "satellite":     # a body between two gridded panels
        xr, yr = rot(x, y, math.radians(-30))
        body = sd_box(xr, yr, 0.17, 0.22, 0.04)
        panels = U(sd_box(xr - 0.62, yr, 0.32, 0.20), sd_box(xr + 0.62, yr, 0.32, 0.20))
        grid = U(sd_box(xr - 0.62, yr, 0.33, 0.022), sd_box(xr + 0.62, yr, 0.33, 0.022),
                 *[sd_box(xr - cx, yr, 0.022, 0.21) for cx in (-0.62, 0.62)])
        struts = sd_box(xr, yr, 0.32, 0.035)
        dish = sd_arc(xr, yr + 0.25, 0.20, math.radians(200), math.radians(340), 0.04)
        return U(body, cut(panels, grid), struts, dish)
    if kind == "uav":           # a long-winged drone seen from above
        fus = sd_box(x, y - 0.02, 0.11, 0.66, 0.09)
        nose = sd_circle(x, y, 0, 0.56, 0.14)
        wing = sd_poly(x, y, [(-0.98, 0.26), (0.98, 0.26), (0.94, 0.06), (-0.94, 0.06)])
        tail = U(sd_seg(x, y, 0, -0.58, -0.36, -0.78), sd_seg(x, y, 0, -0.58, 0.36, -0.78)) - 0.065
        prop = sd_box(x, y + 0.74, 0.20, 0.03)
        return U(fus, nose, wing, tail, prop)
    if kind == "eye":           # an eye with its iris
        lens = np.maximum(sd_circle(x, y, 0, -0.52, 0.90), sd_circle(x, y, 0, 0.52, 0.90))
        rim = np.abs(lens) - 0.06
        iris = cut(r - 0.27, r - 0.10)
        return U(rim, iris)
    if kind == "radar":         # the scope, its sweep and the ring under it
        a = np.arctan2(y, x)
        wedge = np.maximum(r - 0.76, np.maximum(-(a - math.radians(18)), a - math.radians(72)) * r)
        return U(np.abs(r - 0.86) - 0.05, cut(np.abs(r - 0.46) - 0.03, wedge - 0.05), wedge, r - 0.10)
    if kind == "scramble":      # a broken, staggered ring with a cross in it
        a = np.arctan2(y, x)
        seg = np.floor((a + math.pi) / (math.pi / 3)).astype(int)
        rr = np.where(seg % 2 == 0, 0.66, 0.84)
        _, loc = polar_rep(x, y, 6, math.pi / 6)
        ring = np.maximum(np.abs(r - rr) - 0.055, -(np.abs(r * np.sin(loc)) - 0.07))
        cross = U(sd_seg(x, y, -0.26, -0.26, 0.26, 0.26), sd_seg(x, y, -0.26, 0.26, 0.26, -0.26)) - 0.06
        return U(ring, cross)
    if kind == "ambush":        # three open chevrons closing in
        ds = []
        for i in range(3):
            a = math.pi / 2 + i * 2 * math.pi / 3
            cx, cy = 0.62 * math.cos(a), 0.62 * math.sin(a)
            xr, yr = rot(x - cx, y - cy, a + math.pi)
            ds.append(chev(xr - 0.16, yr, 0.30, 0.32, 0.11))
        return U(*ds, r - 0.13)
    if kind == "toxin":         # three drops round a ring
        ds = [sd_poly(x, y, drop_poly(cx, cy, 0.25, 0.52, math.pi / 2))
              for cx, cy in ((0.0, 0.30), (-0.46, -0.40), (0.46, -0.40))]
        mist = U(sd_circle(x, y, -0.20, -0.02, 0.07), sd_circle(x, y, 0.22, -0.06, 0.06), sd_circle(x, y, 0.0, -0.62, 0.07))
        return U(*ds, mist)
    if kind == "clean":         # one drop with a crescent of light cut in it
        dropd = sd_poly(x, y, drop_poly(0.0, -0.18, 0.58, 1.08, math.pi / 2))
        cres = cut(sd_circle(x, y, -0.04, -0.20, 0.40), sd_circle(x, y, 0.08, -0.12, 0.40))
        return cut(dropd, cres - 0.0)
    if kind == "demine":        # a mine struck through
        slash = sd_seg(x, y, -0.78, 0.78, 0.78, -0.78)
        return U(cut(mine(x, y, 0.95), slash - 0.13), slash - 0.06)
    raise KeyError(kind)


# ---------------------------------------------------------------- painting

def grid(w, h, cx, cy, scale):
    """Texel centres in piece units: x right, y up, origin at texel (cx, cy), scale texels per unit."""
    X, Y = np.meshgrid(np.arange(w) + 0.5 - cx, -(np.arange(h) + 0.5 - cy))
    return X / scale, Y / scale


def cov(d, px):
    return np.clip(0.5 - d / px, 0, 1)


def bevel(Hf, light=LIGHT):
    gy, gx = np.gradient(Hf)
    n = np.sqrt(gx * gx + gy * gy + 1)
    return (-gx * light[0] - gy * light[1] + light[2]) / n / light[2]


class Layer:
    def __init__(self, w, h):
        self.c = np.zeros((h, w, 3))
        self.a = np.zeros((h, w))

    def over(self, col, a):                    # self.c is premultiplied
        col = np.broadcast_to(np.asarray(col, float), self.c.shape) if np.ndim(col) < 3 else col
        self.c = self.c * (1 - a[..., None]) + col * a[..., None]
        self.a = self.a * (1 - a) + a

    def rgba(self):
        c = np.where(self.a[..., None] > 1e-4, self.c / np.maximum(self.a[..., None], 1e-4), 0)
        # colour under zero alpha takes the nearest piece colour, so bilinear never pulls in black
        empty = self.a <= 1e-3
        if empty.any() and not empty.all():
            _, (iy, ix) = distance_transform_edt(empty, return_indices=True)
            c = c[iy, ix]
        return np.dstack([np.clip(c, 0, 1), np.clip(self.a, 0, 1)])


PIECES = {}          # name -> dict(img, meta)


def piece(name, layer, **meta):
    PIECES[name] = dict(img=layer.rgba(), meta=meta)


# ---------------------------------------------------------------- band (zone ring, arrow arms, shells)
# Cross-section 20k tall, row 0 the outer edge. Columns are all the same: sample the middle one.

def band_d(h):
    y = np.arange(h) + 0.5
    return (h / 2 - y)[:, None] / T * np.ones((1, 8))       # k, + toward the outer edge


def band_pieces():
    h = int(20 * T)
    d = band_d(h)
    px = 1 / T
    L = Layer(8, h)
    L.over(DARK, cov(np.abs(d) - 9, px) * 0.74)
    piece("MarkerBandShell", L, kind="strip", across_k=20, blend="alpha", tint=False)
    dl = np.abs(d) - 5
    Hf = 2.6 * T * np.clip(-dl / 3, 0, 1) ** 0.6
    for tag, ly in (("A", -1), ("B", 1)):                 # A: light from the outer side
        lt = np.array([0, ly * LP, LIGHT[2]])
        L = Layer(8, h)
        L.over(STEEL[None, None] * bevel(Hf, lt)[..., None], cov(dl, px))
        piece("MarkerBandSteel" + tag, L, kind="strip", across_k=20, blend="alpha", tint=False)
    L = Layer(8, h)
    L.over((1, 1, 1), cov(np.abs(d) - 1.3, px))
    piece("MarkerBandInlay", L, kind="strip", across_k=20, blend="alpha", tint=True)
    # end cap of a band used as a straight bar: 12k long, the band's profile round a half disc
    w = int(12 * T)
    X, Y = grid(w, h, 0, h / 2, T)
    xe = 2.0
    de = np.where(X < xe, np.abs(Y), np.hypot(X - xe, Y))
    L = Layer(w, h)
    L.over(DARK, cov(de - 9, px) * 0.74)
    piece("MarkerBandCapShell", L, kind="cap", across_k=20, along_k=12, blend="alpha", tint=False)
    dl = de - 5
    Hf = 2.6 * T * np.clip(-dl / 3, 0, 1) ** 0.6
    for tag, ly in (("A", -1), ("B", 1)):
        lt = np.array([0, ly * LP, LIGHT[2]])
        L = Layer(w, h)
        L.over(STEEL[None, None] * bevel(Hf, lt)[..., None], cov(dl, px))
        piece("MarkerBandCapSteel" + tag, L, kind="cap", across_k=20, along_k=12, blend="alpha", tint=False)
    L = Layer(w, h)
    L.over((1, 1, 1), cov(de - 1.3, px))
    piece("MarkerBandCapInlay", L, kind="cap", across_k=20, along_k=12, blend="alpha", tint=True)


# ---------------------------------------------------------------- small flat and lit pieces

def studs():
    s = int(18 * T)
    X, Y = grid(s, s, s / 2, s / 2, T)
    px = 1 / T
    L = Layer(s, s)
    L.over(DARK, cov(sd_oct(X, Y, 8.5), px) * 0.9)
    so2 = sd_oct(X, Y, 5.8)
    Hf = 2.2 * T * np.clip(-so2 / 2.5, 0, 1)
    L.over(STEEL[None, None] * bevel(Hf)[..., None], cov(so2, px))
    piece("MarkerStud", L, kind="quad", size_k=[18, 18], anchor=[0.5, 0.5], blend="alpha", tint=False)
    s = int(6 * T)
    X, Y = grid(s, s, s / 2, s / 2, T)
    L = Layer(s, s)
    L.over((1, 1, 1), cov(sd_oct(X, Y, 2.4), px))
    piece("MarkerStudDot", L, kind="quad", size_k=[6, 6], anchor=[0.5, 0.5], blend="alpha", tint=True)


def flat_keyed(name, d, key, ka, fa, size_k, anchor, scale):
    px = 1 / scale
    L = Layer(d.shape[1], d.shape[0])
    L.over(DARK, cov(d - key, px) * ka)
    L.over((1, 1, 1), cov(d, px) * fa)
    piece(name, L, kind="quad", size_k=size_k, anchor=anchor, blend="alpha", tint=True)


def chevrons_ticks():
    # Chevron: tip at the anchor, pointing along +u (right). Lane marks are two of these 8k apart.
    w, h = int(18 * T), int(28 * T)
    tipx = 4 * T
    X, Y = grid(w, h, w - tipx, h / 2, T)
    flat_keyed("MarkerChevron", chev(X, Y, 10, 10, 3.2), 2.2, 0.75, 0.95, [18, 28],
               [(w - tipx) / w, 0.5], T)
    # range scale ticks: radial along v (rows), centre at the anchor
    for name, ln in (("MarkerTick", 4), ("MarkerTickMajor", 9)):
        w, h = int(6 * T), int((2 * ln + 4) * T)
        X, Y = grid(w, h, w / 2, h / 2, T)
        d = np.maximum(np.abs(X) - 0.9, np.abs(Y) - ln)
        flat_keyed(name, d, 1.6, 0.65, 0.9, [6, 2 * ln + 4], [0.5, 0.5], T)
    # bracket: drawn at R 200 (0.2R = 30.8k a side), stretched along u by R/200; v is radial
    half = 30.8
    w, h = int((2 * half + 8) * T), int(14 * T)
    X, Y = grid(w, h, w / 2, h / 2, T)
    d = np.maximum(np.abs(Y) - 3, np.abs(X) - half)
    d = np.maximum(d, -(np.abs(X) - 9.2))
    flat_keyed("MarkerBracket", d, 3, 0.8, 1.0, [2 * half + 8, 14], [0.5, 0.5], T)
    # order pieces at 3 texels a k
    s3 = 3.0
    w = int(54 * s3)
    X, Y = grid(w, w, w / 2, w / 2, s3)
    flat_keyed("MarkerOrderRing", np.abs(sd_oct(X, Y, 22)) - 2, 2.2, 0.8, 1.0, [54, 54], [0.5, 0.5], s3)
    w = int(14 * T)
    X, Y = grid(w, w, w / 2, w / 2, T)
    flat_keyed("MarkerOrderDot", sd_oct(X, Y, 4), 2, 1.0, 1.0, [14, 14], [0.5, 0.5], T)
    w = int(28 * T)
    X, Y = grid(w, w, w / 2, w / 2, T)
    flat_keyed("MarkerOrderCross", sd_seg(np.abs(X), np.abs(Y), 4, 4, 11, 11) - 1.6, 1.6, 0.8, 1.0, [28, 28],
               [0.5, 0.5], T)
    # rail: a steel hairline with its keyline, cross-section 8k, columns all the same
    h = int(8 * T)
    d = (h / 2 - (np.arange(h) + 0.5))[:, None] / T * np.ones((1, 8))
    L = Layer(8, h)
    L.over(DARK, cov(np.abs(d) - 3.6, 1 / T) * 0.7)
    L.over(STEEL, cov(np.abs(d) - 1.2, 1 / T) * 0.9)
    piece("MarkerRail", L, kind="strip", across_k=8, blend="alpha", tint=False)
    # ping: a thin line drawn additively, cross-section 6k
    h = int(6 * T)
    d = (h / 2 - (np.arange(h) + 0.5))[:, None] / T * np.ones((1, 8))
    L = Layer(8, h)
    L.over((1, 1, 1), cov(np.abs(d) - 2.2, 1 / T))
    piece("MarkerPing", L, kind="strip", across_k=6, blend="add", tint=True)


def segments_dash():
    # countdown segment: a bevelled box, u stretched along its arc (drawn for 96k), v 11k radial
    lh = 48.0
    w, h = int(2 * (lh + 2) * 2), int(14 * T)           # u at 2 texels a k, v at 4
    X, _ = np.meshgrid((np.arange(w) + 0.5 - w / 2) / 2, np.zeros(h))
    Y = (h / 2 - (np.arange(h) + 0.5))[:, None] / T * np.ones((1, w))
    qx, qy = np.abs(X) - (lh - 1), np.abs(Y) - 4.5
    d = np.hypot(np.maximum(qx, 0), np.maximum(qy, 0)) + np.minimum(np.maximum(qx, qy), 0) - 1.0
    Hf = 2.2 * T * np.clip(-d / 3, 0, 1)
    for tag, ly in (("A", -1), ("B", 1)):
        lt = np.array([0, ly * LP, LIGHT[2]])
        L = Layer(w, h)
        L.over(STEEL[None, None] * bevel(Hf, lt)[..., None], cov(d, 0.25) * 0.9)
        piece("MarkerSegmentSteel" + tag, L, kind="arc", size_k=[2 * lh + 4, 14], blend="alpha", tint=False)
    L = Layer(w, h)
    L.over((1, 1, 1), cov(d + 2.5, 0.25))
    piece("MarkerSegmentInlay", L, kind="arc", size_k=[2 * lh + 4, 14], blend="alpha", tint=True)
    # orbit dash: flat steel, drawn for 34k, stretched along the orbit
    lh = 17.0
    w, h = int(2 * (lh + 1) * T), int(7 * T)
    X, Y = grid(w, h, w / 2, h / 2, T)
    L = Layer(w, h)
    L.over(STEEL, cov(np.maximum(np.abs(X) - lh, np.abs(Y) - 2.5), 1 / T) * 0.85)
    piece("MarkerOrbitDash", L, kind="arc", size_k=[2 * lh + 2, 7], blend="alpha", tint=False)


def turn_pieces():
    # Estimated turn: a translucent band between the tightest and the widest turn the plane can fly.
    h = 64
    y = (np.arange(h) + 0.5) / h
    edge = np.minimum(y, 1 - y) * h
    a = 0.22 + 0.48 * np.clip(1.6 - edge, 0, 1)
    L = Layer(8, h)
    L.over((1, 1, 1), np.repeat(a[:, None], 8, 1))
    piece("MarkerTurnBand", L, kind="strip", across="rmax - rmin", blend="alpha", tint=True)
    w, h = int(14 * T), int(8 * T)
    X, Y = grid(w, h, w / 2, h / 2, T)
    flat_keyed("MarkerTurnDash", np.maximum(np.abs(X) - 5, np.abs(Y) - 1.2), 1.6, 0.7, 0.95, [14, 8],
               [0.5, 0.5], T)
    ds = T * 1.5
    w = int(16 * ds)
    X, Y = grid(w, w, w / 2, w / 2, ds)
    dd = (np.abs(X) + np.abs(Y)) / 1.4142
    flat_keyed("MarkerDrop", dd - 3.4, 1.8, 0.8, 1.0, [24, 24], [0.5, 0.5], ds)
    flat_keyed("MarkerDropEstimate", np.abs(dd - 3.4) - 0.8, 1.4, 0.7, 1.0, [24, 24], [0.5, 0.5], ds)


# ---------------------------------------------------------------- the sides' finishes and the shapes every power is built of
# Each power's marker is its own frame (2026-11-24, the decals' unique/DESIGNS.md): bands run round
# circles, polygons and radius profiles in the finish of the side that owns the power, and the parts
# are a handful of shapes laid by the code. America keeps the band above.

SIDE_STEEL = {"China": (0.62, 0.58, 0.53), "Gla": (0.52, 0.50, 0.46), "Tur": (0.50, 0.53, 0.48)}
RUST = np.array((0.46, 0.25, 0.12))


def side_bands():
    h = int(20 * T)
    d = band_d(h)
    px = 1 / T
    for side, steel in SIDE_STEEL.items():
        steel = np.array(steel)
        if side == "China":                 # a narrow steep chamfer and a second terrace: crisp
            dl = np.abs(d) - 5.5
            Hf = 2.6 * T * np.clip(-dl / 1.2, 0, 1) + 1.2 * T * np.clip((-dl - 2.6) / 0.4, 0, 1)
            shell, inlay = 9.5, 1.6
            colour = steel[None, None] * np.ones((h, 8, 1))
        elif side == "Gla":                 # a soft uneven bevel, rust toward the edges, a scratch
            dl = np.abs(d) - 5.0
            Hf = 2.4 * T * np.clip(-dl / 2.2, 0, 1) ** 0.8
            shell, inlay = 9.0, 1.0
            m = np.clip((np.abs(d) - 2.6) / 2.0, 0, 1)[..., None]
            colour = steel[None, None] * (1 - 0.75 * m) + RUST[None, None] * (0.75 * m)
            colour = colour * (1 - 0.35 * cov(np.abs(d - 3.1) - 0.25, px))[..., None]
        else:                               # Turkiye: one wide flat facet, matte
            dl = np.abs(d) - 5.5
            Hf = 1.5 * T * np.clip(-dl / 3.6, 0, 1)
            shell, inlay = 9.0, 1.1
            colour = steel[None, None] * np.ones((h, 8, 1))
        L = Layer(8, h)
        L.over(DARK, cov(np.abs(d) - shell, px) * 0.76)
        piece("MarkerBandShell" + side, L, kind="strip", across_k=20, blend="alpha", tint=False)
        for tag, ly in (("A", -1), ("B", 1)):
            lt = np.array([0, ly * LP, LIGHT[2]])
            L = Layer(8, h)
            L.over(colour * bevel(Hf, lt)[..., None], cov(dl, px))
            piece("MarkerBandSteel" + tag + side, L, kind="strip", across_k=20, blend="alpha", tint=False)
        L = Layer(8, h)
        L.over((1, 1, 1), cov(np.abs(d) - inlay, px))
        piece("MarkerBandInlay" + side, L, kind="strip", across_k=20, blend="alpha", tint=True)
    # a tinted hairline with its keyline, 4k across: tracks, fire lines, the Turkiye LED line
    h = int(4 * T)
    d = (h / 2 - (np.arange(h) + 0.5))[:, None] / T * np.ones((1, 8))
    L = Layer(8, h)
    L.over(DARK, cov(np.abs(d) - 1.3, px) * 0.85)       # the cursors' dark line round every mark, thin (2026-11-24)
    L.over((1, 1, 1), cov(np.abs(d) - 0.7, px))
    piece("MarkerHair", L, kind="strip", across_k=4, blend="alpha", tint=True)
    L = Layer(8, 8)
    L.over((1, 1, 1), np.ones((8, 8)))
    piece("MarkerSolid", L, kind="quad", size_k=[1, 1], anchor=[0.5, 0.5], blend="alpha", tint=True)


def side_studs():
    s = int(18 * T)
    X, Y = grid(s, s, s / 2, s / 2, T)
    px = 1 / T
    r = np.hypot(X, Y)
    # China: a round gold rivet
    L = Layer(s, s)
    L.over(DARK, cov(r - 6.6, px) * 0.9)
    dome = r - 4.8
    L.over(np.array((0.86, 0.66, 0.26))[None, None] * bevel(3.0 * T * np.sqrt(np.clip(-dome / 4.8, 0, 1)))[..., None],
           cov(dome, px))
    piece("MarkerStudChina", L, kind="quad", size_k=[18, 18], anchor=[0.5, 0.5], blend="alpha", tint=False)
    # GLA: a hex bolt turned off square, rusty
    c, sn = math.cos(0.35), math.sin(0.35)
    hx, hy = c * X + sn * Y, -sn * X + c * Y
    kx, ky, kz = -0.866025404, 0.5, 0.577350269
    qx, qy = np.abs(hx), np.abs(hy)
    dd = np.minimum(kx * qx + ky * qy, 0)
    qx, qy = qx - 2 * dd * kx, qy - 2 * dd * ky
    qx = qx - np.clip(qx, -kz * 5.0, kz * 5.0)
    hexd = np.hypot(qx, qy - 5.0) * np.sign(qy - 5.0)
    L = Layer(s, s)
    L.over(DARK, cov(hexd - 1.8, px) * 0.9)
    colour = np.array(SIDE_STEEL["Gla"]) * 0.7 + RUST * 0.3
    L.over(colour[None, None] * bevel(2.2 * T * np.clip(-hexd / 1.8, 0, 1))[..., None], cov(hexd, px))
    piece("MarkerStudGla", L, kind="quad", size_k=[18, 18], anchor=[0.5, 0.5], blend="alpha", tint=False)
    # Turkiye: flush, a dark ring round a recessed hex
    L = Layer(s, s)
    L.over(np.array(SIDE_STEEL["Tur"]), cov(r - 5.6, px))
    L.over(DARK, cov(np.abs(r - 5.0) - 0.8, px) * 0.85)
    qx, qy = np.abs(X), np.abs(Y)
    dd = np.minimum(kx * qx + ky * qy, 0)
    qx, qy = qx - 2 * dd * kx, qy - 2 * dd * ky
    qx = qx - np.clip(qx, -kz * 2.6, kz * 2.6)
    hexd = np.hypot(qx, qy - 2.6) * np.sign(qy - 2.6)
    L.over(np.array(SIDE_STEEL["Tur"])[None, None] * 0.8 * bevel(-1.2 * T * np.clip(-hexd / 1.2, 0, 1))[..., None],
           cov(hexd, px))
    piece("MarkerStudTur", L, kind="quad", size_k=[18, 18], anchor=[0.5, 0.5], blend="alpha", tint=False)


def shapes():
    """Small parts, 24k a side, +u to the right: tinted ones with a keyline and a bevelled white face,
    and two of plain steel."""
    s = int(24 * T)
    X, Y = grid(s, s, s / 2, s / 2, T)
    px = 1 / T
    r = np.hypot(X, Y)
    xr, yr = (X + Y) * 0.70710678, (Y - X) * 0.70710678
    box = lambda x, y, hx, hy: np.hypot(np.maximum(np.abs(x) - hx, 0), np.maximum(np.abs(y) - hy, 0)) + \
        np.minimum(np.maximum(np.abs(x) - hx, np.abs(y) - hy), 0)
    tinted = {
        "Tile": np.maximum(box(X, Y, 9, 9), (np.abs(X) + np.abs(Y) - 14) * 0.70710678),
        "Disc": r - 9,
        "Tri": sd_poly(X, Y, [(10, 0), (-8, 9), (-8, -9)]),
        "Para": sd_poly(X, Y, [(-8 - 3.5, -6), (8 - 3.5, -6), (8 + 3.5, 6), (-8 + 3.5, 6)]),
        "Plus": np.minimum(box(X, Y, 9, 2.6), box(X, Y, 2.6, 9)),
        "Cross": np.minimum(sd_seg(X, Y, -7, -7, 7, 7), sd_seg(X, Y, -7, 7, 7, -7)) - 2.2,
        "Diamond": np.minimum(box(xr, yr, 7.5, 5.0), box(xr, yr, 5.0, 7.5)),
        "Ring": np.abs(r - 8) - 1.0,
        "Tongue": np.minimum(np.hypot(X + 4, Y) - 5.2, sd_poly(X, Y, [(10, 0), (-4, 5.2), (-4, -5.2)])),
        "Tank": U(box(X + 1, Y, 8, 5.5), r - 3.6, sd_seg(X, Y, 0, 0, 10.5, 0) - 1.2),
    }
    for name, d in tinted.items():
        L = Layer(s, s)
        L.over(DARK, cov(d - 1.0, px) * 0.85)               # a hairline keyline as dark as the cursors' line round a mark

        Hf = 1.6 * T * np.clip(-d / 1.8, 0, 1)
        L.over(np.ones(3)[None, None] * (0.55 + 0.45 * bevel(Hf))[..., None], cov(d, px))
        piece("MarkerShape" + name, L, kind="quad", size_k=[24, 24], anchor=[0.5, 0.5], blend="alpha", tint=True)
    steel = {"Block": box(X, Y, 9, 6), "Lid": r - 9}
    for name, d in steel.items():
        L = Layer(s, s)
        L.over(DARK, cov(d - 1.2, px) * 0.5)
        Hf = 2.2 * T * np.clip(-d / 2.4, 0, 1)
        face = STEEL[None, None] * bevel(Hf)[..., None]
        if name == "Lid":
            weld = cov(np.minimum(np.abs(X), np.abs(Y)) - 0.5, px) * (r < 6.5)
            face = face * (1 - 0.45 * weld[..., None])
        L.over(face, cov(d, px))
        piece("MarkerShape" + name, L, kind="quad", size_k=[24, 24], anchor=[0.5, 0.5], blend="alpha", tint=False)


# ---------------------------------------------------------------- darts and glyphs, light baked per heading

DART = [(16, 0), (-12, 11), (-6, 0), (-12, -11)]


def darts():
    s3 = 3.0
    w = int(42 * s3)
    X, Y = grid(w, w, w / 2, w / 2, s3)
    px = 1 / s3
    for i in range(16):
        a = i * math.pi / 8
        c, s = math.cos(a), math.sin(a)
        pts = [(x * c - y * s, x * s + y * c) for x, y in DART]
        d = sd_poly(X, Y, pts)
        L = Layer(w, w)
        L.over(DARK, cov(d - 3, px) * 0.85)
        Hf = 2.4 * s3 * np.clip(-d / 3, 0, 1)
        L.over(STEEL[None, None] * bevel(Hf)[..., None], cov(d, px))
        piece("MarkerDart%02d" % i, L, kind="quad", size_k=[42, 42], anchor=[0.5, 0.5], heading_deg=i * 22.5,
              blend="alpha", tint=False)
    d = sd_poly(X, Y, DART)
    L = Layer(w, w)
    L.over((1, 1, 1), cov(d + 3.5, px))
    piece("MarkerDartInlay", L, kind="quad", size_k=[42, 42], anchor=[0.5, 0.5], heading_deg=0, blend="alpha",
          tint=True)


# name, kind, baked steps, degrees a step. Every piece is MarkerGlyph<name>, or <name>00.. when it spins.
GLYPHS = [("Guard", "guard", 1, 0), ("Drop", "drop", 1, 0), ("Strike", "strike", 1, 0),
          ("Nuke", "nuke", 8, 15.0), ("Barrage", "barrage", 4, 22.5), ("Reticle", "reticle", 4, 22.5),
          ("Para", "para", 1, 0), ("Crate", "crate", 1, 0), ("Leaflet", "leaflet", 1, 0),
          ("Volley", "volley", 1, 0), ("Bombs", "bombs", 1, 0), ("Blast", "blast", 1, 0),
          ("Flame", "flame", 1, 0), ("Scud", "scud", 1, 0), ("Beam", "beam", 1, 0),
          ("Missile", "missile", 1, 0), ("Ballistic", "ballistic", 1, 0), ("Artillery", "artillery", 1, 0),
          ("Swarm", "swarm", 1, 0), ("Drones", "drones", 1, 0), ("Bolt", "bolt", 1, 0),
          ("Jam", "jam", 1, 0), ("Mine", "mine", 1, 0), ("Frenzy", "frenzy", 1, 0),
          ("Wrench", "wrench", 1, 0), ("Satellite", "satellite", 1, 0), ("Uav", "uav", 1, 0),
          ("Eye", "eye", 1, 0), ("Radar", "radar", 1, 0), ("Scramble", "scramble", 1, 0),
          ("Ambush", "ambush", 1, 0), ("Toxin", "toxin", 1, 0), ("Clean", "clean", 1, 0),
          ("Demine", "demine", 1, 0)]

GLYPH_CANVAS = 2.4                      # the quad is 2.4 S a side (W3DGroundMarkers drawGlyph)


def glyph_layer(kind, spin):
    """One glyph, machined: a dark keyline, a bevelled face lit from the top left, an engraved line
    0.065 S inside every edge wide enough to take it, and fine lathe rings round the middle."""
    w = int(round(GLYPH_CANVAS * GS))
    X, Y = grid(w, w, w / 2, w / 2, GS)
    d = glyph_sdf(kind, X, Y, spin) * GS                 # texels
    px = 1.0
    L = Layer(w, w)
    L.over(DARK, cov(d - 0.13 * GS, px) * 0.8)
    Hf = 7.2 * (GS / 80.0) * np.clip(-d / (0.15 * GS), 0, 1) ** 0.7
    face = (0.25 + 0.75 * bevel(Hf)) / 1.15
    r = np.hypot(X, Y)
    lathe = 1.0 + 0.045 * np.sin(r * 2 * math.pi / 0.034)            # a ring every 4.25 texels
    rng = np.random.default_rng(len(kind) * 7919 + int(spin * 1000))
    grain = gaussian_filter1d(rng.standard_normal((w, w)), 6.0, axis=1)
    grain /= max(1e-6, grain.std())
    face = face * (lathe + 0.025 * grain)
    inner = -d
    groove = cov(np.abs(inner - 0.065 * GS) - 0.011 * GS, px)
    face = face * (1.0 - 0.55 * groove)
    # the edge just outside the groove catches the light: a hairline highlight on its lit side
    lip = cov(np.abs(inner - 0.042 * GS) - 0.006 * GS, px) * np.clip(bevel(Hf * 3.0) - 1.0, 0, 1)
    face = face + 0.25 * lip
    face = np.clip(face, 0, 1)
    L.over(face[..., None] * np.ones(3), cov(d, px))
    return L


def glyphs():
    for name, kind, n, step in GLYPHS:
        for i in range(n):
            spin = math.radians(i * step)
            L = glyph_layer(kind, spin)
            nm = "MarkerGlyph%s" % name + ("%02d" % i if n > 1 else "")
            piece(nm, L, kind="quad", size_S=[GLYPH_CANVAS, GLYPH_CANVAS], anchor=[0.5, 0.5], heading_deg=i * step,
                  blend="alpha", tint=True)


# ---------------------------------------------------------------- pack, write

G = 8           # gutter of each piece's own edge: 4 texels at the second mip, 2 at the third
SLOT = 16       # slots start and end on a multiple of the third mip's DXT block (4 texels there), so no
                # 4x4 block of any level holds two pieces and no box of the mip chain spans two
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


def write_dds(path, levels):
    """DXT5 with a mip chain: Pillow compresses each level on its own (it writes no chain), the levels'
    blocks go one after another under the first header, which then says how many there are."""
    head, body = None, b""
    for s in levels:
        buf = io.BytesIO()
        Image.fromarray((np.clip(s, 0, 1) * 255 + 0.5).astype(np.uint8)).save(buf, format="DDS", pixel_format="DXT5")
        data = buf.getvalue()
        assert len(data) == 128 + s.shape[0] * s.shape[1], "Pillow wrote no DXT5 (needs Pillow 11 or later)"
        head = head or bytearray(data[:128])
        body += data[128:]
    flags, = struct.unpack_from("<I", head, 8)
    caps, = struct.unpack_from("<I", head, 108)
    struct.pack_into("<I", head, 8, flags | 0x20000)                 # DDSD_MIPMAPCOUNT
    struct.pack_into("<I", head, 28, len(levels))
    struct.pack_into("<I", head, 108, caps | 0x8 | 0x400000 | 0x1000)  # COMPLEX | MIPMAP | TEXTURE
    with open(path, "wb") as f:
        f.write(bytes(head) + body)


TINTS = {"warm": (0.95, 0.90, 0.80), "gold": (0.97, 0.79, 0.24), "amber": (0.99, 0.62, 0.17),
         "orange": (0.98, 0.43, 0.12), "red": (0.93, 0.19, 0.12)}
SHEET_TINT = {"Guard": "warm", "Drop": "gold", "Strike": "orange", "Nuke": "red", "Barrage": "amber",
              "Reticle": "orange", "Para": "gold", "Crate": "gold", "Leaflet": "gold", "Volley": "orange",
              "Bombs": "orange", "Blast": "orange", "Flame": "orange", "Scud": "red", "Beam": "red",
              "Missile": "red", "Ballistic": "red", "Artillery": "amber", "Swarm": "amber", "Drones": "orange",
              "Bolt": "amber", "Jam": "amber", "Mine": "amber", "Frenzy": "amber", "Wrench": "warm",
              "Satellite": "warm", "Uav": "warm", "Eye": "warm", "Radar": "warm", "Scramble": "warm",
              "Ambush": "amber", "Toxin": "orange", "Clean": "warm", "Demine": "warm"}


def glyph_sheet(path):
    """Every glyph tinted as the game tints it, on dark ground, eight to a row, with its name."""
    from PIL import ImageDraw, ImageFont
    cell = 220
    cols = 8
    names = [g[0] for g in GLYPHS]
    rows = (len(names) + cols - 1) // cols
    out = Image.new("RGB", (cols * cell, rows * cell), (54, 50, 42))
    try:
        font = ImageFont.truetype("arialbd.ttf", 16)
    except OSError:
        font = ImageFont.load_default()
    draw = ImageDraw.Draw(out)
    for i, n in enumerate(names):
        key = "MarkerGlyph" + n + ("00" if "MarkerGlyph" + n not in PIECES else "")
        img = PIECES[key]["img"]
        tint = np.array(TINTS[SHEET_TINT[n]])
        rgb = img[..., :3] * tint
        a = img[..., 3:4]
        x0, y0 = (i % cols) * cell, (i // cols) * cell
        bg = np.asarray(out.crop((x0, y0, x0 + cell, y0 + cell)), float) / 255
        g = Image.fromarray((np.clip(np.dstack([rgb, a[..., 0]]), 0, 1) * 255).astype(np.uint8)).resize((190, 190), Image.LANCZOS)
        ga = np.asarray(g, float) / 255
        ox, oy = 15, 4
        sub = bg[oy:oy + 190, ox:ox + 190]
        sub[:] = sub * (1 - ga[..., 3:4]) + ga[..., :3] * ga[..., 3:4]
        out.paste(Image.fromarray((bg * 255).astype(np.uint8)), (x0, y0))
        draw.text((x0 + 8, y0 + cell - 24), n, fill=(230, 226, 214), font=font)
    out.save(path)


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--out", required=True, help="folder for the PNG, DDS, INI and pieces.json")
    ap.add_argument("--dds", help="also copy the DDS here (Code/Data/Art/Textures)")
    ap.add_argument("--ini", help="also copy the INI here (Code/Data/INI/MappedImages/HandCreated)")
    args = ap.parse_args()
    os.makedirs(args.out, exist_ok=True)
    band_pieces()
    studs()
    chevrons_ticks()
    segments_dash()
    turn_pieces()
    side_bands()
    side_studs()
    shapes()
    darts()
    glyphs()
    sheet = pack()
    assert np.isfinite(sheet).all(), "a piece came out with NaN texels"
    Image.fromarray((np.clip(sheet, 0, 1) * 255 + 0.5).astype(np.uint8)).save(os.path.join(args.out, "ReforgedGroundMarkers.png"))
    glyph_sheet(os.path.join(args.out, "glyphs.png"))
    dds = os.path.join(args.out, "ReforgedGroundMarkers.dds")
    write_dds(dds, mip_chain(sheet))
    meta = {n: PIECES[n]["meta"] for n in sorted(PIECES)}
    with open(os.path.join(args.out, "pieces.json"), "w") as f:
        json.dump(dict(texture="ReforgedGroundMarkers.dds", width=W, height=H, texels_per_k=T, glyph_texels_per_S=GS,
                       mips=MIPS, gutter=G, pieces=meta), f, indent=1)
    lines = ["; Machined Reticle ground markers (ReforgedGroundMarkers.dds, %dx%d DXT5, %d mip levels)." % (W, H, MIPS),
             "; Generated by Tools/ground_markers.py; W3DGroundMarkers.cpp lays every piece on the ground.",
             "; Right and Bottom are exclusive. Every rect has a %d texel gutter of its own edge round it." % G, ""]
    for n, m in meta.items():
        l, t, r, b = m["rect"]
        lines += ["MappedImage %s" % n, "  Texture = ReforgedGroundMarkers.tga", "  TextureWidth = %d" % W,
                  "  TextureHeight = %d" % H, "  Coords = Left:%d Top:%d Right:%d Bottom:%d" % (l, t, r, b),
                  "  Status = NONE", "End", ""]
    ini = os.path.join(args.out, "ReforgedGroundMarkers.ini")
    with open(ini, "w", newline="\n") as f:
        f.write("\n".join(lines))
    import shutil
    if args.dds:
        shutil.copyfile(dds, os.path.join(args.dds, "ReforgedGroundMarkers.dds"))
    if args.ini:
        shutil.copyfile(ini, os.path.join(args.ini, "ReforgedGroundMarkers.ini"))
    print(len(meta), "pieces")


if __name__ == "__main__":
    main()
