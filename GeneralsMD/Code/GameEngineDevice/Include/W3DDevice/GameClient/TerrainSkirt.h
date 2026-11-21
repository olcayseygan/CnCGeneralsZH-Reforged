/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2025 Electronic Arts Inc.
**
**	This program is free software: you can redistribute it and/or modify
**	it under the terms of the GNU General Public License as published by
**	the Free Software Foundation, either version 3 of the License, or
**	(at your option) any later version.
**
**	This program is distributed in the hope that it will be useful,
**	but WITHOUT ANY WARRANTY; without even the implied warranty of
**	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**	GNU General Public License for more details.
**
**	You should have received a copy of the GNU General Public License
**	along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/
// Modified 2026 by Olcay Seygan for Zero Hour Reforged; see the git history.

// TerrainSkirt.h: the ground drawn past the heightmap's last sample, where the camera used to look
// into the clear colour (BaseHeightMapRenderObjClass::drawSkirt).
//
// It is picture only.  Nothing in logic, the pick or the pathfinder knows it is there, and its
// hills come from a hash of the world position, never a game random stream.  Leaving the edge it
// starts on the edge sample's own height, slides over to a blur of the edge round it, then to the
// mean of the whole edge, and low hills grow out of that; its light fades to black toward the rim,
// so it ends in the same black the void was.  Nothing here knows the engine: test_terrain_skirt
// includes it bare.

#pragma once

#ifndef TERRAIN_SKIRT_H
#define TERRAIN_SKIRT_H

#include <math.h>
#include <vector>

namespace TerrainSkirt
{
	/** How far the skirt reaches past the heightmap's edge, in ten unit cells.  Pulled all the way out
		the camera sits 992 over the ground at 37.5 degrees, and with the look-at point 200 past a corner
		of the playable map the frame's far corners land some 1800 units out at 16:9.  The rim is black
		by then, so what lies past it is the same black. */
	const int CELLS = 160;
	const float CELL = 10.0f;	///< MAP_XY_FACTOR
	const float REACH = CELLS * CELL;
	/// Share of the reach drawn at full light before the fade starts.
	const float FADE_START = 0.25f;
	/// The tallest the hills rise over the ground under them, world units, and the first octave's size.
	const float HILL_HEIGHT = 60.0f;
	const float HILL_WAVELENGTH = 420.0f;
	/// Distance over which the hills grow out of nothing at the edge.
	const float HILL_RAMP = 250.0f;
	/// Distance over which the edge sample gives way to the blurred edge, and that to the edge's mean.
	const float PROFILE_RAMP = 150.0f;
	const float LEVEL_RAMP = 900.0f;
	/// Samples either way along the edge the blur averages.
	const int PROFILE_RADIUS = 12;
	/** The skirt starts a cell inside the map and goes under it there by this much, world units, so
		the subdivided ground's edge, which bends between its samples, never opens a crack onto the
		clear colour. */
	const float SINK = 4.0f;

	inline float smooth(float t)
	{
		if (t <= 0.0f)
			return 0.0f;
		if (t >= 1.0f)
			return 1.0f;
		return t * t * (3.0f - 2.0f * t);
	}

	/// 0 to 1 at an integer lattice point, the same on every machine.
	inline float lattice(int x, int y)
	{
		unsigned h = (unsigned)x * 374761393u + (unsigned)y * 668265263u;
		h = (h ^ (h >> 13)) * 1274126177u;
		h ^= h >> 16;
		return (float)(h & 0xffffu) / 65535.0f;
	}

	/// Value noise through the lattice, smoothly interpolated, 0 to 1.
	inline float valueNoise(float x, float y)
	{
		const float fx = floorf(x), fy = floorf(y);
		const int ix = (int)fx, iy = (int)fy;
		const float tx = smooth(x - fx), ty = smooth(y - fy);
		const float a = lattice(ix, iy), b = lattice(ix + 1, iy);
		const float c = lattice(ix, iy + 1), d = lattice(ix + 1, iy + 1);
		const float top = a + (b - a) * tx;
		const float bottom = c + (d - c) * tx;
		return top + (bottom - top) * ty;
	}

	/// The hills' shape at a world point, 0 to 1: two octaves, the second smaller and fainter.
	inline float hills(float worldX, float worldY)
	{
		const float n1 = valueNoise(worldX / HILL_WAVELENGTH, worldY / HILL_WAVELENGTH);
		const float n2 = valueNoise(worldX / (0.4f * HILL_WAVELENGTH) + 17.0f, worldY / (0.4f * HILL_WAVELENGTH) - 31.0f);
		return (n1 + 0.35f * n2) / 1.35f;
	}

	/** The skirt's height, world units, at a world point `outside` units past the edge: `edge` is the
		nearest edge sample, `blurred` the edge round it, `mean` the whole edge's.  At the edge itself it
		is the edge sample, so the skirt meets the ground. */
	inline float height(float edge, float blurred, float mean, float outside, float worldX, float worldY)
	{
		float base = edge + (blurred - edge) * smooth(outside / PROFILE_RAMP);
		base += (mean - base) * smooth(outside / LEVEL_RAMP);
		return base + HILL_HEIGHT * hills(worldX, worldY) * smooth(outside / HILL_RAMP);
	}

	/// What share of its light a point `outside` units past the edge keeps: 1 near the map, 0 at the rim.
	inline float fade(float outside)
	{
		return 1.0f - smooth((outside / REACH - FADE_START) / (1.0f - FADE_START));
	}

	/** Where sample (x, y) on the edge of a width by height map sits on the ring round it: along the
		bottom row, up the right column, back along the top row and down the left column.  -1 off the edge. */
	inline int ringIndex(int x, int y, int width, int height)
	{
		if (y == 0 && x >= 0 && x < width)
			return x;
		if (x == width - 1 && y > 0 && y < height)
			return width - 1 + y;
		if (y == height - 1 && x >= 0 && x < width - 1)
			return (width - 1) + (height - 1) + (width - 1 - x);
		if (x == 0 && y > 0 && y < height - 1)
			return 2 * (width - 1) + (height - 1) + (height - 1 - y);
		return -1;
	}

	/// How many samples the ring holds.
	inline int ringLength(int width, int height)
	{
		return 2 * (width - 1) + 2 * (height - 1);
	}

	/// Each ring sample averaged with PROFILE_RADIUS either way, round the corners.
	inline std::vector<float> blurRing(const std::vector<float> &ring)
	{
		const int n = (int)ring.size();
		std::vector<float> out(ring.size(), 0.0f);
		for (int i = 0; i < n; ++i)
		{
			float sum = 0.0f;
			for (int k = -PROFILE_RADIUS; k <= PROFILE_RADIUS; ++k)
				sum += ring[((i + k) % n + n) % n];
			out[i] = sum / (2 * PROFILE_RADIUS + 1);
		}
		return out;
	}
}

#endif
