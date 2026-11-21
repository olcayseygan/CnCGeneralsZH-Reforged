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
// mean of the edge's land, and low hills grow out of that.  Past an edge under water it stays sea
// floor under the water instead (seaHeight), and a coast mixes the two.  Toward the rim it fades
// into the horizon's colour, which drawSkirtFade also lays over the void past it in the tactical view,
// so it ends in what lies past it.
// Nothing here knows the engine: test_terrain_skirt includes it bare.

#pragma once

#ifndef TERRAIN_SKIRT_H
#define TERRAIN_SKIRT_H

#include <math.h>
#include <vector>

namespace TerrainSkirt
{
	/** How far the skirt reaches past the heightmap's edge, in ten unit cells.  Pulled all the way out
		the camera sits 992 over the ground at 37.5 degrees, and with the look-at point 200 past a corner
		of the playable map the frame's far corners land some 1800 units out at 16:9.  The rim is all
		horizon colour by then, so what lies past it is the same colour. */
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
	const float LEVEL_RAMP = 450.0f;	///< 900 carried the edge's own ridges out as long parallel ribs
	/// Samples either way along the edge the blur averages.
	const int PROFILE_RADIUS = 12;
	/** The texture past the edge (patchCell).  Beside the edge, a band PATCH_NEAR tiles deep give or
		take half carries the edge on as it is.  Past it the ground is patches about PATCH_TILES tiles
		across, each one texture class, taken from the edge near the patch's centre and moved along the
		edge by up to PATCH_SHIFT cells plus PATCH_SPREAD of the centre's distance out, in cells; never
		further in than PATCH_SHIFT. */
	const float PATCH_NEAR = 1.5f;
	const float PATCH_TILES = 12.0f;
	const float PATCH_SHIFT = 12.0f;
	const float PATCH_SPREAD = 0.6f;
	/** The skirt starts a cell inside the map and goes under it there, edge samples included, by this
		much, world units, so the subdivided ground's edge, which bends between its samples, never opens
		a crack onto the clear colour and the skirt's long quads never poke up through the first cell. */
	const float SINK = 4.0f;
	/// Past an edge under water the sea floor stays at least this far below the water, world units.
	const float SEA_MARGIN = 6.0f;
	/** How far over the skirt its fade into the rim's colour is laid, world units: over the sea on it
		and its waves too, which write depth above the floor.  The fade is too slow to show the shift. */
	const float FADE_LIFT = 40.0f;

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

	inline int clampCell(int cell, int last)
	{
		return cell < 0 ? 0 : (cell > last ? last : cell);
	}

	inline int cellsOut(int cell, int last)
	{
		return cell < 0 ? -cell : (cell > last ? cell - last : 0);
	}

	/** The map cell (the tile's lower left, on a width by height heightmap) a tile takes its texture
		from, and the patch it lies in: 0 for a tile on the map or in the band that carries the edge on,
		which keep the map's own order, and otherwise a hash that is the same over the whole patch.  The
		patches are the tiles nearest each of a grid of centres jittered by a hash, so their borders run
		every way.  The straight nearest cell carried each edge cell out to the rim as a stripe, grass and
		rock side by side down the whole skirt. */
	inline unsigned patchCell(int tileX, int tileY, int width, int height, int *cellX, int *cellY)
	{
		const int x = 2 * tileX, y = 2 * tileY;
		*cellX = clampCell(x, width - 2);
		*cellY = clampCell(y, height - 2);
		const int out = cellsOut(x, width - 2) > cellsOut(y, height - 2) ? cellsOut(x, width - 2) : cellsOut(y, height - 2);
		if (out == 0 || out < 2.0f * PATCH_NEAR * (0.5f + valueNoise(tileX / 5.0f, tileY / 5.0f)))
			return 0;
		const int px = (int)floorf(tileX / PATCH_TILES), py = (int)floorf(tileY / PATCH_TILES);
		float best = 1.0e30f, centreX = 0.0f, centreY = 0.0f;
		int bestX = px, bestY = py;
		for (int j = py - 1; j <= py + 1; ++j)
			for (int i = px - 1; i <= px + 1; ++i)
			{
				const float cx = (i + lattice(i, j)) * PATCH_TILES, cy = (j + lattice(i + 31, j - 17)) * PATCH_TILES;
				const float d = (cx - tileX) * (cx - tileX) + (cy - tileY) * (cy - tileY);
				if (d < best)
				{
					best = d;
					centreX = cx;
					centreY = cy;
					bestX = i;
					bestY = j;
				}
			}
		const int cx = (int)floorf(2.0f * centreX + 0.5f), cy = (int)floorf(2.0f * centreY + 0.5f);
		const int outX = cellsOut(cx, width - 2), outY = cellsOut(cy, height - 2);
		const float along = (lattice(bestX + 5, bestY + 9) - 0.5f) * 2.0f
			* (PATCH_SHIFT + PATCH_SPREAD * (float)(outX > outY ? outX : outY));
		const int inward = (int)(lattice(bestX - 3, bestY + 11) * PATCH_SHIFT);
		int sx = clampCell(cx, width - 2), sy = clampCell(cy, height - 2);
		// along the edge the tile lies past, a little in from it; past a corner, along one of the two
		const bool alongX = cellsOut(y, height - 2) > 0 && (cellsOut(x, width - 2) == 0 || lattice(bestX, bestY - 7) < 0.5f);
		if (alongX)
		{
			sx += (int)floorf(along + 0.5f);
			sy = y < 0 ? inward : height - 2 - inward;
		}
		else
		{
			sy += (int)floorf(along + 0.5f);
			sx = x < 0 ? inward : width - 2 - inward;
		}
		*cellX = clampCell(sx, width - 2);
		*cellY = clampCell(sy, height - 2);
		unsigned patch = (unsigned)bestX * 2654435761u ^ (unsigned)bestY * 40503u;
		return patch == 0 ? 1u : patch;
	}

	/** Which tile of a texture class `width` tiles square a tile wears, across and down: the map's own
		order, as WorldBuilder lays it, with the whole block moved by its patch (patchCell).  So a patch
		keeps the class's picture whole and the seams fall only where patches meet; the same block again
		every `width` tiles out to the rim laid rows of the same boulders. */
	inline void scatterTile(int tileX, int tileY, int width, unsigned patch, int *across, int *down)
	{
		const int shiftX = patch == 0 ? 0 : (int)((patch >> 3) % (unsigned)width);
		const int shiftY = patch == 0 ? 0 : (int)((patch >> 11) % (unsigned)width);
		*across = ((tileX + shiftX) % width + width) % width;
		*down = ((tileY + shiftY) % width + width) % width;
	}

	/// terrain_variation's hash (ffshader.h TERRAIN_VARIATION_SAMPLING), 0 to 1, bit for bit.
	inline float variationHash(int x, int y)
	{
		unsigned h = (unsigned)x * 0x8da6b343u ^ (unsigned)y * 0xd8163841u;
		h ^= h >> 15;
		h *= 0x2c1b3c6du;
		h ^= h >> 12;
		h *= 0x297a2d39u;
		h ^= h >> 15;
		return (float)(h >> 8) * (1.0f / 16777216.0f);
	}

	inline float variationNoise(float x, float y)
	{
		const float fx = floorf(x), fy = floorf(y);
		const int ix = (int)fx, iy = (int)fy;
		float tx = x - fx, ty = y - fy;
		tx = tx * tx * (3.0f - 2.0f * tx);
		ty = ty * ty * (3.0f - 2.0f * ty);
		const float top = variationHash(ix, iy) + (variationHash(ix + 1, iy) - variationHash(ix, iy)) * tx;
		const float bottom = variationHash(ix, iy + 1) + (variationHash(ix + 1, iy + 1) - variationHash(ix, iy + 1)) * tx;
		return top + (bottom - top) * ty;
	}

	/** The ground's slow shade and warm-cool tilt at a world point, as terrain_variation multiplies the
		map's light under the sun per pixel, fully faded in (the skirt is never under the closest zoom's
		ground).  Baked into the skirt's vertices under the same mode, or the skirt stood a tone apart
		from the map's edge. */
	inline void variation(float worldX, float worldY, float rgb[3])
	{
		const float ax = worldX * 0.866f + worldY * 0.5f, ay = worldX * -0.5f + worldY * 0.866f;
		const float bx = worldX * 0.423f + worldY * -0.906f, by = worldX * 0.906f + worldY * 0.423f;
		const float n = 0.4f * variationNoise(ax / 290.0f, ay / 290.0f)
			+ 0.35f * variationNoise(bx / 113.0f + 17.3f, by / 113.0f + 17.3f)
			+ 0.25f * variationNoise(ax / 41.0f + 53.7f, ay / 41.0f + 53.7f);
		const float tilt = variationNoise(bx / 197.0f + 91.1f, by / 197.0f + 91.1f) - 0.5f;
		const float shade = 1.0f + 0.6f * (n - 0.5f);
		rgb[0] = shade * (1.0f + 0.08f * tilt);
		rgb[1] = shade;
		rgb[2] = shade * (1.0f - 0.08f * tilt);
	}

	/// Value noise on a lattice turned by the angle whose cosine and sine are c and s, cells a wavelength wide.
	inline float turnedNoise(float x, float y, float c, float s, float wavelength)
	{
		return valueNoise((c * x - s * y) / wavelength, (s * x + c * y) / wavelength);
	}

	/** The hills' shape at a world point, 0 to 1: three octaves, each smaller and fainter, on lattices
		turned 37, 71 and 133 degrees, read through a slow warp of the point.  Two octaves square to the
		world's axes laid their lattice rows out along the map's edges as a corrugation that repeated
		down the whole skirt on Alpine Assault. */
	inline float hills(float worldX, float worldY)
	{
		const float WARP = 0.8f * HILL_WAVELENGTH;
		const float warpX = (turnedNoise(worldX, worldY, 0.951f, 0.309f, 1.7f * HILL_WAVELENGTH) - 0.5f) * WARP;
		const float warpY = (turnedNoise(worldX + 5711.0f, worldY - 3203.0f, 0.588f, -0.809f, 1.7f * HILL_WAVELENGTH) - 0.5f) * WARP;
		const float x = worldX + warpX, y = worldY + warpY;
		const float n1 = turnedNoise(x, y, 0.799f, 0.602f, HILL_WAVELENGTH);
		const float n2 = turnedNoise(x + 917.0f, y - 1453.0f, 0.326f, 0.946f, 0.43f * HILL_WAVELENGTH);
		const float n3 = turnedNoise(x - 2333.0f, y + 409.0f, -0.682f, 0.731f, 0.23f * HILL_WAVELENGTH);
		return (n1 + 0.4f * n2 + 0.12f * n3) / 1.52f;
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

	/** Past an edge under water: the edge sample sliding to the blurred edge as on land, but held under
		the water and with no hills and no pull to the land's mean, so a sea map's skirt stays sea floor
		where it would otherwise climb out as a beach. */
	inline float seaHeight(float edge, float blurred, float waterZ, float outside)
	{
		const float floorZ = blurred < waterZ - SEA_MARGIN ? blurred : waterZ - SEA_MARGIN;
		return edge + (floorZ - edge) * smooth(outside / PROFILE_RAMP);
	}

	/// How far over the water a coast beside a sea strip comes down to, world units.
	const float SHORE_RISE = 2.0f;

	/** Land beside the sea past the edge (no sea strip over it): the two heights mixed by the share of
		sea along the edge round the point (blurRing of 1 for an edge sample with sea past it, 0 for
		land), so a coast turns from one into the other over the blur and no wall stands where the edge
		goes into the water, but never under the water's level, which has no water drawn over it here:
		low land stays as it is, and lowered land stops just over the water as a beach, not a dry trench. */
	inline float coastHeight(float land, float sea, float seaShare, float waterZ)
	{
		const float mixed = land + (sea - land) * seaShare;
		const float floorZ = land < waterZ + SHORE_RISE ? land : waterZ + SHORE_RISE;
		return mixed > floorZ ? mixed : floorZ;
	}

	/// What share of its own colour a point `outside` units past the edge keeps against the horizon's: 1 near the map, 0 at the rim.
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
