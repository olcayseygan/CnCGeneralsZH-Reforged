#!/usr/bin/env python3
"""Explosion, fire and smoke flipbooks drawn in EA's own look, for ParticleSystem FlipbookGrid.

EA's explosion and smoke cards (EXexplo03, EXFire01, EXSmokePuff) are single pre-rendered frames: a
cauliflower of round billows lit from the upper left, coloured along a short ramp. These atlases are
the same kind of picture set in motion, drawn here in numpy rather than simulated, because the
fluid-sim flipbooks of 2026-11 read as photographs in the game and were taken out. Nothing of EA's
is copied: the billows are spheres and noise, and only each ramp's colours were measured off EA's
texture (brightness percentiles, scratch work of 2026-11-16) so the INI's retail colour keys tint
these exactly as they tint the card they replace.

Looks:
    blast       ReforgedBlastV.dds, 8 by 8 frames of 256 pixels: four different clusters of 16
                frames each, back to back (frames 0-15, 16-31, 32-47, 48-63, counted row by row),
                for FlipbookVariants = 4, so every particle picks one cluster at birth and plays it
                once over its life (FlipbookRate 0). A cluster of billows bursts out, rolls, cools
                and breaks into lumps. Additive (black ground), EXexplo03's ramp.
    smoke       ReforgedSmokeLoop.dds, 8 by 8 frames of 256 pixels, one churning puff whose 64
                frames loop. Alpha, the mid grey of EXCloud01 and EXSmokePuff. FlipbookRate with
                FlipbookRandomStart: every puff starts on its own frame, so no two look alike.
    fire        ReforgedFireLoop.dds, 8 by 8 frames of 128 pixels, a round flickering flame that
                licks upward and loops. Additive, EXFire01's ramp, played the same way.

    python sprite_flipbook.py build [preview folder]   writes the atlases into Data/Art/Textures
    python sprite_flipbook.py selfcheck"""
import os
import struct
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
TEXTURES = os.path.join(HERE, "..", "Data", "Art", "Textures")
SUPER = 2					# frames are drawn this many times larger and box filtered down
SMALLEST_CELL = 8			# the smallest mip keeps frames this many pixels a side
THROWAWAY_LEVELS = 2		# DDSFileClass drops a file's two smallest levels (ddsfile.cpp)

# Brightness to colour, measured off EA's cards: for each look the mean colour of the texels at a
# brightness percentile. The top stop of the blast ramp is held below EXexplo03's brightest (240,192,
# 178) so a stack of additive cards does not run to white sooner than EA's did.
RAMP_BLAST = ((0.0, (0, 0, 0)), (0.08, (34, 15, 10)), (0.18, (69, 36, 25)), (0.3, (103, 58, 46)),
			  (0.42, (134, 81, 73)), (0.65, (188, 138, 128)), (1.0, (236, 186, 170)))
RAMP_FIRE = ((0.0, (0, 0, 0)), (0.1, (35, 21, 15)), (0.22, (62, 38, 27)), (0.38, (106, 67, 48)),
			 (0.58, (164, 112, 71)), (0.82, (229, 186, 84)), (1.0, (250, 210, 89)))
RAMP_SMOKE = ((0.0, (50, 50, 50)), (0.3, (83, 82, 83)), (0.6, (126, 124, 126)), (0.85, (157, 155, 157)),
			  (1.0, (181, 178, 181)))		# EXCloud01's and EXSmokePuff's grey, which the INI's keys darken

BLAST_SPREAD = 1.3			# blast billows' reach: EXexplo03's ball fills about two thirds of its card
LIGHT = np.array((-0.45, -0.55, 0.70)) / np.linalg.norm((-0.45, -0.55, 0.70))	# from the upper left, image y down


def ramp(value, stops):
	value = np.clip(value, 0.0, 1.0)
	xs = np.array([s[0] for s in stops])
	out = np.empty(value.shape + (3,))
	for c in range(3):
		out[..., c] = np.interp(value, xs, np.array([s[1][c] for s in stops], float)) / 255.0
	return out


def periodic_noise(size, rng, falloff=1.6, low=2):
	"""fBm that tiles: white noise shaped by 1/f^falloff in Fourier space, scaled to -1..1. Being
	periodic on the grid, a whole-tile shift of it is seamless, which is what makes the loops loop."""
	fx = np.fft.fftfreq(size) * size
	radius = np.sqrt(fx[:, None] ** 2 + fx[None, :] ** 2)
	shape = np.where(radius >= low, 1.0 / np.maximum(radius, 1.0) ** falloff, 0.0)
	field = np.real(np.fft.ifft2(np.fft.fft2(rng.standard_normal((size, size))) * shape))
	return field / np.abs(field).max()


def sample(field, x, y):
	"""Bilinear lookup of a periodic field at pixel coordinates, wrapping."""
	size = field.shape[0]
	x0 = np.floor(x).astype(int)
	y0 = np.floor(y).astype(int)
	fx = x - x0
	fy = y - y0
	x0 %= size
	y0 %= size
	x1 = (x0 + 1) % size
	y1 = (y0 + 1) % size
	top = field[y0, x0] * (1 - fx) + field[y0, x1] * fx
	bottom = field[y1, x0] * (1 - fx) + field[y1, x1] * fx
	return top * (1 - fy) + bottom * fy


def smoothstep(a, b, x):
	t = np.clip((x - a) / (b - a), 0.0, 1.0)
	return t * t * (3 - 2 * t)


def billows(centres, radii, heats, size, detail, displace):
	"""The front surface of a cluster of spheres seen from the camera, in a size-pixel frame spanning
	-1..1: coverage 0..1, a light term and the heat of the billow on top, blended across seams."""
	axis = (np.arange(size) + 0.5) / size * 2.0 - 1.0
	x, y = np.meshgrid(axis, axis)
	k = 14.0
	zsum = np.zeros((size, size))
	heat = np.zeros((size, size))
	reach = np.zeros((size, size))
	zmax = np.full((size, size), -9.0)
	for (cx, cy, cz), r, h in zip(centres, radii, heats):
		s = r * r - (x - cx) ** 2 - (y - cy) ** 2
		inside = s > 0
		z = np.where(inside, cz + np.sqrt(np.maximum(s, 0.0)), -9.0)
		zmax = np.maximum(zmax, z)
		reach = np.maximum(reach, np.where(inside, s / (r * r), 0.0))
		w = np.where(inside, np.exp(k * np.minimum(z, 3.0)), 0.0)
		zsum += w
		heat += w * h
	covered = zsum > 0
	height = np.where(covered, np.log(np.maximum(zsum, 1e-30)) / k, zmax)
	heat = np.where(covered, heat / np.maximum(zsum, 1e-30), 0.0)
	height = np.where(covered, height + displace * detail, -1.0)
	gy, gx = np.gradient(height, 2.0 / size)
	normal = np.stack((-gx, -gy, np.ones_like(gx)), axis=-1)
	normal /= np.linalg.norm(normal, axis=-1, keepdims=True)
	light = np.clip(normal @ LIGHT, 0.0, 1.0)
	# the creases between billows: how far the surface sits below its own blurred self
	crease = np.clip(1.0 - 5.0 * np.maximum(blur(height, size // 40) - height, 0.0), 0.3, 1.0)
	coverage = smoothstep(0.0, 0.35, reach + 0.4 * detail - 0.08) * covered		# ragged, wispy rims
	return coverage, light, heat, crease, normal[..., 2]


def blur(image, radius):
	"""Three box passes each way, near enough a gaussian."""
	out = image
	for axis in (0, 1):
		for _ in range(3):
			width = ((radius + 1, radius), (0, 0)) if axis == 0 else ((0, 0), (radius + 1, radius))
			pad = np.pad(out, width, mode="edge")
			total = np.cumsum(pad, axis=axis)
			if axis == 0:
				out = (total[2 * radius + 1:] - total[:-2 * radius - 1]) / (2 * radius + 1)
			else:
				out = (total[:, 2 * radius + 1:] - total[:, :-2 * radius - 1]) / (2 * radius + 1)
	return out


def downsample(image, factor):
	h, w = image.shape[:2]
	return image.reshape(h // factor, factor, w // factor, factor, *image.shape[2:]).mean(axis=(1, 3))


def blast_frames(seed, cell=256, frames=16):
	"""One explosion: billows thrown out of a hot centre, swelling fast then slowly, cooling from
	the outside in, and in the last third broken into lumps that burn down. Additive frames
	(RGB on black) with alpha as the brightness, so the mips keep the colour."""
	rng = np.random.default_rng(seed)
	size = cell * SUPER
	count = rng.integers(14, 20)
	direction = rng.standard_normal((count, 3))
	direction /= np.linalg.norm(direction, axis=1, keepdims=True)
	direction[:, 1] -= 0.25							# a little more of it upward (image y down)
	start = rng.uniform(0.0, 0.2, count)
	speed = rng.uniform(0.12, 0.28, count)
	radius = rng.uniform(0.15, 0.26, count)
	heat0 = rng.uniform(0.8, 1.0, count) * (1.0 - 0.45 * start / 0.2)
	cool = rng.uniform(0.5, 1.0, count) * (0.7 + speed)	# the far-flung billows cool first; the INI fades the rest
	# each billow carries smaller ones on its face, the cauliflower of EA's cards
	children = rng.integers(4, 7, count)
	parent = np.repeat(np.arange(count), children)
	offset = rng.standard_normal((parent.size, 3))
	offset[:, 2] = np.abs(offset[:, 2]) + 0.3			# toward the camera
	offset /= np.linalg.norm(offset, axis=1, keepdims=True)
	share = rng.uniform(0.35, 0.55, parent.size)
	heat_child = rng.uniform(0.85, 1.1, parent.size)
	coarse = periodic_noise(size, rng, 1.9)
	fine = periodic_noise(size, rng, 1.15, low=6)
	detail = 0.65 * coarse + 0.35 * fine
	lumps = periodic_noise(size, rng, 2.3, low=3)
	cloud = periodic_noise(size, rng, 1.8, low=4)		# the darker smoky patches inside EA's balls
	out = []
	for f in range(frames):
		t = f / (frames - 1)
		grow = 1.0 - (1.0 - t) ** 2.4
		# half grown on the first frame, the particle's own SizeRate doing the rest as with EA's card,
		# and as much of the frame filled as EXexplo03 fills, so EA's sizes stay EA's on screen
		centres = BLAST_SPREAD * direction * (start + speed * (0.35 + 0.65 * grow))[:, None]
		radii = BLAST_SPREAD * radius * (0.65 + 0.5 * grow)
		heats = heat0 * np.exp(-cool * t)
		small = centres[parent] + offset * (radii[parent] * 0.72)[:, None]
		coverage, light, heat, crease, facing = billows(np.concatenate((centres, small)),
												np.concatenate((radii, radii[parent] * share)),
												np.concatenate((heats, heats[parent] * heat_child)),
												size, detail, 0.07)
		heat = heat * (0.85 + 0.25 * coarse)
		# lumps: from half way the thin, cool parts burn through first and leave pieces
		burn = smoothstep(0.45, 1.0, t)
		coverage *= smoothstep(burn - 0.12, burn + 0.12, 0.5 + 0.45 * lumps + 0.3 * heat - 0.1 * (1 - crease))
		# a flame gives its own light: the billows' roundness and creases are a shading on top of the
		# heat, not the source of it, or the ball reads as modelling clay
		# EA's balls burn brightest where a billow turns away from the camera and are smoky and dark
		# in their middles
		rim = (1.0 - facing) ** 1.5
		value = heat * (0.2 + 0.8 * rim + 0.3 * light) * crease ** 1.5 * (0.2 + 0.8 * smoothstep(-0.4, 0.4, cloud)) * (0.85 + 0.25 * fine)
		value = value * (1.0 - 0.3 * t)			# the whole ball dims toward the end
		out.append((value, coverage))
	# one exposure for the whole run: the brightest frames' top texels at the ramp's top stop
	early = np.concatenate([v[c > 0.5] for v, c in out[1:5]])
	gain = 0.97 / np.percentile(early, 97)
	axis = (np.arange(size) + 0.5) / size * 2.0 - 1.0
	edge = smoothstep(1.0, 0.86, np.sqrt(axis[None, :] ** 2 + axis[:, None] ** 2))	# nothing cut by the frame's rim
	frames_out = []
	for value, coverage in out:
		rgb = ramp(value * gain, RAMP_BLAST) * coverage[..., None]
		halo = np.stack([blur(rgb[..., c], size // 18) for c in range(3)], axis=-1)	# the glow round the edge
		rgb = rgb + 0.8 * halo * (1.0 - coverage[..., None])
		rgb *= edge[..., None]
		alpha = np.clip(rgb.max(axis=-1) * 2.5, 0.0, 1.0)
		frames_out.append(downsample(np.concatenate((rgb, alpha[..., None]), axis=-1), SUPER))
	return np.stack(frames_out)


def loop_phase(f, frames):
	return 2.0 * np.pi * f / frames


def smoke_frames(seed, cell=256, frames=64):
	"""A puff churning in place: billows circle their places, swell and shrink a little, the noise on
	them drifts round a closed path, all on whole cycles of the 64 frames so the last runs into the
	first. Smoke is soft, so it is a density (a sum of round gaussians broken by noise) rather than
	a surface: the cover is 1 - exp(-density), the light falls off down its gradient. Straight
	colour, alpha the cover."""
	rng = np.random.default_rng(seed)
	size = cell * SUPER
	count = 30
	place = rng.standard_normal((count, 2)) * np.array((0.22, 0.2))
	radius = rng.uniform(0.12, 0.22, count)
	orbit = rng.uniform(0.08, 0.17, count)
	turns = rng.choice((1, 2, 3), count) * rng.choice((-1, 1), count)
	phase = rng.uniform(0, 2 * np.pi, count)
	# each lump swells and thins on its own beat, so the outline a random start lands on differs
	beat = rng.choice((1, 2, 3), count)
	beat_phase = rng.uniform(0, 2 * np.pi, count)
	coarse = periodic_noise(size, rng, 1.9)
	fine = periodic_noise(size, rng, 1.3, low=5)
	axis = (np.arange(size) + 0.5) / size * 2.0 - 1.0
	x, y = np.meshgrid(axis, axis)
	pix = np.arange(size, dtype=float)
	px, py = np.meshgrid(pix, pix)
	out = []
	for f in range(frames):
		a = loop_phase(f, frames)
		cx = place[:, 0] + orbit * np.cos(turns * a + phase)
		cy = place[:, 1] + orbit * np.sin(turns * a + phase)
		radii = radius * (1.0 + 0.1 * np.sin(turns * a + 2 * phase))
		density = np.zeros((size, size))
		lit = np.zeros((size, size))
		weight = np.zeros((size, size))
		for i in range(count):
			g = np.exp(-((x - cx[i]) ** 2 + (y - cy[i]) ** 2) / (2 * radii[i] ** 2))
			g *= 0.25 + 0.75 * (0.5 + 0.5 * np.sin(beat[i] * a + beat_phase[i]))
			density += g
			# each lump lit on its own upper left side, the strongest lump at a texel winning
			side = np.clip(((x - cx[i]) * LIGHT[0] + (y - cy[i]) * LIGHT[1]) / (1.6 * radii[i]), -1.0, 1.0)
			lit += g ** 3 * (0.5 + 0.5 * side)
			weight += g ** 3
		shift = 0.1 * size
		detail = 0.65 * sample(coarse, px + shift * np.cos(a), py + shift * np.sin(a)) + \
			0.35 * sample(fine, px - shift * np.cos(2 * a), py + shift * np.sin(2 * a))
		density = np.maximum(density * (1.0 + 0.6 * detail) - 0.15, 0.0)
		alpha = (1.0 - np.exp(-2.2 * density)) * 0.9 * smoothstep(0.98, 0.75, np.sqrt(x * x + y * y))
		lit = lit / np.maximum(weight, 1e-12)
		value = (0.2 + 0.8 * lit) * (0.75 + 0.4 * detail)
		rgb = ramp(value, RAMP_SMOKE)
		out.append(downsample(np.concatenate((rgb * alpha[..., None], alpha[..., None]), axis=-1), SUPER))
	return straight(np.stack(out))


def fire_frames(seed, cell=128, frames=64):
	"""A round flame licking upward: two periodic noise fields scrolled up by whole tiles over the
	loop (one twice as fast), bent sideways by a third, cut by a round mask that leans up."""
	rng = np.random.default_rng(seed)
	size = cell * SUPER
	slow = periodic_noise(size, rng, 1.5, low=3)
	fast = periodic_noise(size, rng, 1.3, low=5)
	bend = periodic_noise(size, rng, 2.0)
	axis = np.arange(size, dtype=float)
	px, py = np.meshgrid(axis, axis)
	u = (px + 0.5) / size * 2 - 1
	v = (py + 0.5) / size * 2 - 1
	lean = np.sqrt(u ** 2 * 1.1 + (v - 0.15) ** 2 * np.where(v < 0.15, 0.55, 1.5))	# taller above
	mask = smoothstep(0.98, 0.1, lean)
	out = []
	for f in range(frames):
		a = loop_phase(f, frames)
		sway = 0.06 * size * sample(bend, px + size * f / frames, py)
		# sampled at half height, so the licks come out twice as tall as wide
		n = 0.6 * sample(slow, px + sway, 0.5 * py + size * f / frames) + 0.4 * sample(fast, px - sway, 0.5 * py + 2 * size * f / frames)
		value = mask * (0.4 + 1.0 * n) + 0.6 * mask ** 5 - 0.1		# EXFire01's yellow heart
		value = np.clip(value, 0.0, 1.0)
		value *= 0.93 + 0.07 * np.sin(a * 2 + 1.3 * seed)
		rgb = ramp(value, RAMP_FIRE)
		alpha = np.clip(rgb.max(axis=-1) * 2.5, 0.0, 1.0)
		out.append(downsample(np.concatenate((rgb, alpha[..., None]), axis=-1), SUPER))
	return np.stack(out)


def straight(frames):
	"""Premultiplied frames to straight colour, the cover's mean colour where there is none."""
	alpha = frames[..., 3:]
	mean = frames[..., :3].sum(axis=(0, 1, 2)) / max(alpha.sum(), 1e-6)
	rgb = np.where(alpha > 1e-3, frames[..., :3] / np.maximum(alpha, 1e-3), mean)
	return np.concatenate((np.clip(rgb, 0.0, 1.0), alpha), axis=-1)


def sheet(frames):
	count, cell = frames.shape[0], frames.shape[1]
	side = int(round(count ** 0.5))
	return frames.reshape(side, side, cell, cell, 4).transpose(0, 2, 1, 3, 4).reshape(side * cell, side * cell, 4)


# ---------------------------------------------------------------------------------------------
# DXT5 with mips, as the fork's earlier flipbooks were written.

def to565(rgb):
	r = np.round(rgb[..., 0] * 31.0 / 255.0).astype(np.uint32)
	g = np.round(rgb[..., 1] * 63.0 / 255.0).astype(np.uint32)
	b = np.round(rgb[..., 2] * 31.0 / 255.0).astype(np.uint32)
	return (r << 11) | (g << 5) | b


def from565(c):
	r = (c >> 11) & 31
	g = (c >> 5) & 63
	b = c & 31
	return np.stack(((r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2)), axis=-1).astype(np.float32)


def blocks(image):
	h, w, c = image.shape
	return image.reshape(h // 4, 4, w // 4, 4, c).transpose(0, 2, 1, 3, 4).reshape(-1, 16, c)


def colour_blocks(rgb):
	"""rgb (n, 16, 3) in 0..255: DXT colour blocks in four colour mode, endpoints on the principal
	axis inset by a sixteenth."""
	mean = rgb.mean(axis=1, keepdims=True)
	centred = rgb - mean
	axis = np.linalg.eigh(np.einsum("nki,nkj->nij", centred, centred))[1][:, :, 2]
	along = np.einsum("nki,ni->nk", centred, axis)
	low = np.clip(mean[:, 0] + axis * along.min(axis=1, keepdims=True), 0.0, 255.0)
	high = np.clip(mean[:, 0] + axis * along.max(axis=1, keepdims=True), 0.0, 255.0)
	inset = (high - low) / 16.0
	c0 = to565(high - inset)
	c1 = to565(low + inset)
	swap = c0 < c1
	c0, c1 = np.where(swap, c1, c0), np.where(swap, c0, c1)
	p0 = from565(c0)
	p1 = from565(c1)
	palette = np.stack((p0, p1, (2.0 * p0 + p1) / 3.0, (p0 + 2.0 * p1) / 3.0), axis=1)
	distance = ((rgb[:, :, None, :] - palette[:, None, :, :]) ** 2).sum(axis=-1)
	index = distance.argmin(axis=-1).astype(np.uint32)
	index[c0 == c1] = 0
	bits = (index << (2 * np.arange(16, dtype=np.uint32))).sum(axis=1, dtype=np.uint64).astype(np.uint32)
	out = np.zeros((rgb.shape[0], 8), np.uint8)
	out[:, 0:2] = c0.astype("<u2").view(np.uint8).reshape(-1, 2)
	out[:, 2:4] = c1.astype("<u2").view(np.uint8).reshape(-1, 2)
	out[:, 4:8] = bits.astype("<u4").view(np.uint8).reshape(-1, 4)
	return out


def alpha_blocks(alpha):
	"""alpha (n, 16) in 0..255: DXT5 alpha blocks, eight value mode."""
	a0 = alpha.max(axis=1).round().astype(np.uint32)
	a1 = alpha.min(axis=1).round().astype(np.uint32)
	weights = np.array([0, 7, 1, 2, 3, 4, 5, 6], np.float32)
	palette = np.floor(a0[:, None] + (a1[:, None].astype(np.float32) - a0[:, None]) * weights / 7.0 + 0.5)
	index = np.abs(alpha[:, :, None] - palette[:, None, :]).argmin(axis=-1).astype(np.uint64)
	index[a0 == a1] = 0
	bits = (index << (3 * np.arange(16, dtype=np.uint64))).sum(axis=1, dtype=np.uint64)
	out = np.zeros((alpha.shape[0], 8), np.uint8)
	out[:, 0] = a0
	out[:, 1] = a1
	out[:, 2:8] = bits.astype("<u8").view(np.uint8).reshape(-1, 8)[:, :6]
	return out


def clear_cell_rings(level, cell):
	"""No colour or cover on a frame's outer ring, so no mip or filter pulls a neighbour in."""
	for edge in range(0, level.shape[0], cell):
		level[edge, :] = level[edge + cell - 1, :] = 0.0
		level[:, edge] = level[:, edge + cell - 1] = 0.0
	return level


def mips(image, cell, additive):
	"""Down to SMALLEST_CELL frames plus THROWAWAY_LEVELS. Additive frames are box filtered as they
	are (black is nothing); alpha frames weight the colour by the cover."""
	levels = [clear_cell_rings(image.copy(), cell)]
	while cell > SMALLEST_CELL >> THROWAWAY_LEVELS:
		top = levels[-1]
		if additive:
			level = downsample(top, 2)
		else:
			weighted = np.concatenate((top[..., :3] * top[..., 3:], top[..., 3:]), axis=-1)
			level = downsample(weighted, 2)
			alpha = level[..., 3:]
			level = np.concatenate((np.where(alpha > 1e-4, level[..., :3] / np.maximum(alpha, 1e-4), top[::2, ::2, :3]), alpha), axis=-1)
		cell //= 2
		levels.append(clear_cell_rings(level, cell))
	return levels


def dds(image, cell, additive):
	"""Straight-alpha image (h, w, 4) in 0..1 as DXT5 DDS bytes with mips."""
	levels = mips(image, cell, additive)
	payload = []
	for level in levels:
		if level.shape[0] < 4:
			level = np.pad(level, ((0, 4 - level.shape[0]), (0, 4 - level.shape[1]), (0, 0)), mode="edge")
		texels = blocks(np.clip(np.round(level * 255.0), 0.0, 255.0))
		payload.append(np.concatenate((alpha_blocks(texels[:, :, 3]), colour_blocks(texels[:, :, :3])), axis=1).tobytes())
	h, w = image.shape[:2]
	flags = 0x1 | 0x2 | 0x4 | 0x1000 | 0x20000 | 0x80000
	header = b"DDS " + struct.pack("<7I", 124, flags, h, w, len(payload[0]), 0, len(levels)) + bytes(44)
	header += struct.pack("<2I4s5I", 32, 0x4, b"DXT5", 0, 0, 0, 0, 0)
	header += struct.pack("<5I", 0x8 | 0x1000 | 0x400000, 0, 0, 0, 0)
	assert len(header) == 128
	return header + b"".join(payload)


# ---------------------------------------------------------------------------------------------

BLAST_SEEDS = (11, 23, 37, 52)		# the four clusters, once ReforgedBlastA..D, now the runs of ReforgedBlastV

LOOKS = (
	("ReforgedBlastV", lambda: np.concatenate([blast_frames(seed) for seed in BLAST_SEEDS]), True),
	("ReforgedSmokeLoop", lambda: smoke_frames(7), False),
	("ReforgedFireLoop", lambda: fire_frames(5), True),
)


def seam(frames):
	"""The loop's last-to-first step against the largest step inside it (mean texel change)."""
	steps = np.abs(np.diff(frames, axis=0)).mean(axis=(1, 2, 3))
	return np.abs(frames[0] - frames[-1]).mean(), steps.max()


def previews(name, frames, additive, folder):
	"""The atlas over the battlefield's sand: the sheet, and every frame in a row for a clip."""
	from PIL import Image
	ground = np.array((0.47, 0.40, 0.29))
	image = sheet(frames)
	halves = []
	for back in (np.zeros(3), ground):
		if additive:
			halves.append(np.clip(back + image[..., :3], 0, 1))
		else:
			halves.append(image[..., :3] * image[..., 3:] + back * (1 - image[..., 3:]))
	rgb = np.concatenate(halves, axis=1)
	Image.fromarray((rgb * 255).astype(np.uint8)).save(os.path.join(folder, name + ".png"))


def build(folder=None):
	for name, make, additive in LOOKS:
		frames = make()
		cell = frames.shape[1]
		image = sheet(frames)
		if name.endswith("Loop"):
			last, largest = seam(frames)
			print("%s seam %.4f, largest step %.4f" % (name, last, largest))
		with open(os.path.join(TEXTURES, name + ".dds"), "wb") as out:
			out.write(dds(image, cell, additive))
		if folder:
			os.makedirs(folder, exist_ok=True)
			np.save(os.path.join(folder, name + ".npy"), frames.astype(np.float16))
			previews(name, frames, additive, folder)
		print("%s %dx%d, %d frames of %d" % (name, image.shape[1], image.shape[0], frames.shape[0], cell))


def selfcheck():
	rng = np.random.default_rng(1)
	field = periodic_noise(64, rng)
	assert np.allclose(sample(field, np.array([3.0 + 64]), np.array([5.0])), field[5, 3])		# tiles
	assert abs(field).max() <= 1.0 + 1e-9
	frames = blast_frames(3, cell=32, frames=4)
	assert frames.shape == (4, 32, 32, 4) and frames[..., :3].max() > 0.2
	assert frames[0, 0, 0, :3].max() == 0.0											# black ground
	runs = np.arange(64, dtype=float)[:, None, None, None] * np.ones((1, 2, 2, 4))
	atlas = sheet(runs)
	assert atlas[4, 0, 0] == 16 and atlas[0, 15, 0] == 7 and atlas[12, 2, 0] == 49	# variants run row by row
	loop = fire_frames(3, cell=32, frames=8)
	last, largest = seam(loop)
	assert last <= largest * 1.25, (last, largest)			# the seam is one more step, not a jump
	image = np.zeros((32, 32, 4))
	image[4:12, 4:12] = (1.0, 0.5, 0.25, 1.0)
	data = dds(image, 16, True)
	assert data[:4] == b"DDS " and data[84:88] == b"DXT5"
	print("selfcheck ok")


def main():
	if len(sys.argv) >= 2 and sys.argv[1] == "build":
		build(sys.argv[2] if len(sys.argv) > 2 else None)
	elif len(sys.argv) >= 2 and sys.argv[1] == "selfcheck":
		selfcheck()
	else:
		print(__doc__)
		return 1
	return 0


if __name__ == "__main__":
	sys.exit(main())
