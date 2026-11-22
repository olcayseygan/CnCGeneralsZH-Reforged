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
	/** The texture past the edge (tileLayers).  Beside the edge, a band PATCH_NEAR tiles deep give or
		take half carries the edge on as it is.  Past it the ground is patches about PATCH_TILES tiles
		across, each one texture class, taken from the edge near the patch's centre and moved along the
		edge by up to PATCH_SHIFT cells plus PATCH_SPREAD of the centre's distance out, in cells; never
		further in than PATCH_SHIFT.  Neighbours blend over 2 PATCH_BLEND tiles along borders wandering by
		PATCH_WOBBLE tiles on a PATCH_WOBBLE_SIZE lattice. */
	const float PATCH_NEAR = 1.5f;
	const float PATCH_TILES = 12.0f;
	const float PATCH_SHIFT = 12.0f;
	const float PATCH_SPREAD = 0.6f;
	const float PATCH_BLEND = 1.5f;
	const float PATCH_WOBBLE = 1.5f;
	const float PATCH_WOBBLE_SIZE = 3.0f;
	/** The skirt starts a cell inside the map and goes under it there, edge samples included, by this
		much, world units, so the subdivided ground's edge, which bends between its samples, never opens
		a crack onto the clear colour and the skirt's long quads never poke up through the first cell. */
	const float SINK = 4.0f;
	/// Past an edge under water the sea floor stays at least this far below the water, world units.
	const float SEA_MARGIN = 6.0f;
	/** How many cells out from an edge sample the sea past it is asked for: past the side of a polygon
		drawn a little outside the heightmap.  One cell fell short of Tournament Island's west side at
		x -715, so its whole west skirt stood as land and its hills came up through the sea. */
	const int SEA_PROBE = 8;
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

	/** One texture layer of a skirt tile: the map cell (lower left, on a width by height heightmap) its
		texture class comes from, and its patch, 0 for the map's own order from the nearest cell and
		otherwise a hash that is the same over the whole patch. */
	struct PatchLayer
	{
		unsigned patch;
		int cellX, cellY;
	};

	/// One of the jittered grid of patch centres, in tiles, and its place in the grid.
	struct PatchSite
	{
		int i, j;
		float x, y;
	};

	/// Tiles outside the heightmap's tiles (0 to width/2 across), the larger of the two ways.
	inline float tilesOutside(float x, float y, int width, int height)
	{
		const float highX = (width - 2) * 0.5f + 1.0f, highY = (height - 2) * 0.5f + 1.0f;
		const float outX = x < 0.0f ? -x : (x > highX ? x - highX : 0.0f);
		const float outY = y < 0.0f ? -y : (y > highY ? y - highY : 0.0f);
		return outX > outY ? outX : outY;
	}

	/// Below nought inside the band that carries the edge on in the map's own order, above it past.
	inline float bandField(float x, float y, int width, int height)
	{
		return tilesOutside(x, y, width, height) - PATCH_NEAR * (0.5f + valueNoise(x / 5.0f, y / 5.0f));
	}

	/// The centre of the patch in grid cell (i, j), in tiles, jittered by a hash.
	inline PatchSite patchSite(int i, int j)
	{
		PatchSite site;
		site.i = i;
		site.j = j;
		site.x = (i + lattice(i, j)) * PATCH_TILES;
		site.y = (j + lattice(i + 31, j - 17)) * PATCH_TILES;
		return site;
	}

	/** The grid cells either way round a tile's own whose centres can take a share of its corners: a
		centre two cells off is at least 24 tiles away, past any nearest centre plus the blend. */
	const int SITE_SEARCH = 2;
	const int SITE_COUNT = (2 * SITE_SEARCH + 1) * (2 * SITE_SEARCH + 1);
	/// The most layers one point of the skirt mixes (pointShares).
	const int POINT_LAYERS = 3;
	/** The most layers one skirt tile wears, each laid over those before it: its corners' together.  A
		tile that dropped one its neighbour kept would cut a seam between them. */
	const int MOST_LAYERS = 6;

	inline unsigned siteHash(const PatchSite &site)
	{
		const unsigned patch = (unsigned)site.i * 2654435761u ^ (unsigned)site.j * 40503u;
		return patch == 0 ? 1u : patch;
	}

	/** The cell a patch reads: from the edge its centre lies past (or, over the map, nearest), a little
		in from it, moved along the edge by up to PATCH_SHIFT plus PATCH_SPREAD of the centre's distance
		out; past a corner, along one of the two.  The centre alone decides, so a patch is one picture on
		every tile it covers; decided by the tile, a patch over a corner's diagonal changed class across it. */
	inline PatchLayer siteLayer(const PatchSite &site, int width, int height)
	{
		const int cx = (int)floorf(2.0f * site.x + 0.5f), cy = (int)floorf(2.0f * site.y + 0.5f);
		const int outX = cellsOut(cx, width - 2), outY = cellsOut(cy, height - 2);
		const float along = (lattice(site.i + 5, site.j + 9) - 0.5f) * 2.0f
			* (PATCH_SHIFT + PATCH_SPREAD * (float)(outX > outY ? outX : outY));
		const int inward = (int)(lattice(site.i - 3, site.j + 11) * PATCH_SHIFT);
		int sx = clampCell(cx, width - 2), sy = clampCell(cy, height - 2);
		const int toX = sx < width - 2 - sx ? sx : width - 2 - sx, toY = sy < height - 2 - sy ? sy : height - 2 - sy;
		const bool alongX = outX == 0 && outY == 0 ? toY < toX : outY > 0 && (outX == 0 || lattice(site.i, site.j - 7) < 0.5f);
		if (alongX)
		{
			sx += (int)floorf(along + 0.5f);
			sy = 2 * sy < height - 2 ? inward : height - 2 - inward;
		}
		else
		{
			sy += (int)floorf(along + 0.5f);
			sx = 2 * sx < width - 2 ? inward : width - 2 - inward;
		}
		PatchLayer layer;
		layer.patch = siteHash(site);
		layer.cellX = clampCell(sx, width - 2);
		layer.cellY = clampCell(sy, height - 2);
		return layer;
	}

	/** Each of `count` patch centres' share at point (x, y), in tiles, into `shares`, and the band's after
		them.  The band takes the whole edge line and fades out across its field over 2 PATCH_BLEND tiles.
		A centre takes more of what the band leaves the nearer it is to the nearest one, nothing once it is
		2 PATCH_BLEND tiles further, measured from the point moved by up to PATCH_WOBBLE so the borders
		wander.  Then every share gives up the POINT_LAYERS + 1st largest, so no point mixes more than
		POINT_LAYERS and none jumps.  The point alone decides, so every tile round a corner takes the same
		mix there and three patches meet as softly as two. */
	inline void pointShares(float x, float y, int width, int height, const PatchSite *sites, int count, float *shares)
	{
		const float band = 1.0f - smooth(0.5f + 0.5f * bandField(x, y, width, height) / PATCH_BLEND)
			* smooth(tilesOutside(x, y, width, height));
		const float movedX = x + (valueNoise(x / PATCH_WOBBLE_SIZE + 7.1f, y / PATCH_WOBBLE_SIZE - 3.3f) - 0.5f) * 2.0f * PATCH_WOBBLE;
		const float movedY = y + (valueNoise(x / PATCH_WOBBLE_SIZE - 5.7f, y / PATCH_WOBBLE_SIZE + 2.9f) - 0.5f) * 2.0f * PATCH_WOBBLE;
		float nearest = 1.0e30f;
		for (int s = 0; s < count; ++s)
		{
			const float dx = sites[s].x - movedX, dy = sites[s].y - movedY;
			shares[s] = sqrtf(dx * dx + dy * dy);
			nearest = shares[s] < nearest ? shares[s] : nearest;
		}
		float sum = 0.0f;
		for (int s = 0; s < count; ++s)
		{
			shares[s] = 1.0f - smooth((shares[s] - nearest) / (2.0f * PATCH_BLEND));
			sum += shares[s];
		}
		for (int s = 0; s < count; ++s)
			shares[s] *= (1.0f - band) / sum;
		shares[count] = band;
		float largest[POINT_LAYERS + 1];
		for (int l = 0; l <= POINT_LAYERS; ++l)
			largest[l] = 0.0f;
		for (int s = 0; s <= count; ++s)
			for (int l = 0; l <= POINT_LAYERS; ++l)
				if (shares[s] > largest[l])
				{
					for (int m = POINT_LAYERS; m > l; --m)
						largest[m] = largest[m - 1];
					largest[l] = shares[s];
					break;
				}
		const float cut = largest[POINT_LAYERS];
		sum = 0.0f;
		for (int s = 0; s <= count; ++s)
		{
			shares[s] = shares[s] > cut ? shares[s] - cut : 0.0f;
			sum += shares[s];
		}
		for (int s = 0; s <= count; ++s)
			shares[s] /= sum;
	}

	/** The texture past the edge: the layers a tile wears, the first under the rest, and how much of
		each over those before it (alpha) at the tile's four corners, (x, y), (x+1, y), (x+1, y+1),
		(x, y+1).  Beside the edge a band PATCH_NEAR tiles deep give or take half carries the edge on as it
		is; past it the ground is patches about PATCH_TILES tiles across, the tiles nearest each of a grid
		of centres jittered by a hash, each one texture class, blending into one another over a few tiles
		(pointShares) as the map's own blend tiles do.  Two layers a tile cut a hard edge wherever a third
		patch came near, as grass rectangles with stair-step corners in the rock.  The layers go in one
		order every tile keeps, the band then by patch, so two tiles laying different sets agree along
		the side they share.  Returns how many, the band alone on the map. */
	inline int tileLayers(int tileX, int tileY, int width, int height, PatchLayer layers[MOST_LAYERS], float alpha[MOST_LAYERS][4])
	{
		PatchLayer band;
		band.patch = 0;
		band.cellX = clampCell(2 * tileX, width - 2);
		band.cellY = clampCell(2 * tileY, height - 2);
		layers[0] = band;
		for (int k = 0; k < 4; ++k)
			alpha[0][k] = 1.0f;
		const float centreX = tileX + 0.5f, centreY = tileY + 0.5f;
		if (tilesOutside(centreX, centreY, width, height) <= 0.0f)
			return 1;
		PatchSite sites[SITE_COUNT];
		const int cellI = (int)floorf(centreX / PATCH_TILES), cellJ = (int)floorf(centreY / PATCH_TILES);
		int count = 0;
		for (int j = cellJ - SITE_SEARCH; j <= cellJ + SITE_SEARCH; ++j)
			for (int i = cellI - SITE_SEARCH; i <= cellI + SITE_SEARCH; ++i)
				sites[count++] = patchSite(i, j);
		// the band's share last, after the centres'
		const float cornerX[4] = { (float)tileX, tileX + 1.0f, tileX + 1.0f, (float)tileX };
		const float cornerY[4] = { (float)tileY, (float)tileY, tileY + 1.0f, tileY + 1.0f };
		float shares[4][SITE_COUNT + 1];
		float most[SITE_COUNT + 1] = {};
		for (int k = 0; k < 4; ++k)
		{
			pointShares(cornerX[k], cornerY[k], width, height, sites, count, shares[k]);
			for (int s = 0; s <= SITE_COUNT; ++s)
				most[s] = shares[k][s] > most[s] ? shares[k][s] : most[s];
		}
		int chosen[SITE_COUNT + 1];
		int layerCount = 0;
		for (int s = 0; s <= SITE_COUNT; ++s)
			if (most[s] > 0.0f)
				chosen[layerCount++] = s;
		while (layerCount > MOST_LAYERS)	// the least taken goes
		{
			int least = 0;
			for (int l = 1; l < layerCount; ++l)
				least = most[chosen[l]] < most[chosen[least]] ? l : least;
			chosen[least] = chosen[--layerCount];
		}
		unsigned order[SITE_COUNT + 1];
		for (int l = 0; l < layerCount; ++l)
			order[l] = chosen[l] == SITE_COUNT ? 0u : siteHash(sites[chosen[l]]);
		for (int l = 1; l < layerCount; ++l)
			for (int m = l; m > 0 && order[m] < order[m - 1]; --m)
			{
				const unsigned key = order[m];
				order[m] = order[m - 1];
				order[m - 1] = key;
				const int index = chosen[m];
				chosen[m] = chosen[m - 1];
				chosen[m - 1] = index;
			}
		// each layer over those before it so every corner comes out at its shares
		float below[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
		for (int l = 0; l < layerCount; ++l)
		{
			layers[l] = chosen[l] == SITE_COUNT ? band : siteLayer(sites[chosen[l]], width, height);
			for (int k = 0; k < 4; ++k)
			{
				const float share = shares[k][chosen[l]];
				below[k] += share;
				alpha[l][k] = below[k] > 0.0f ? share / below[k] : 1.0f;
			}
		}
		return layerCount;
	}

	/** Which tile of a texture class `width` tiles square a tile wears, across and down, and where each
		of its four corners (as tileLayers orders them) falls in that tile, s across and t up, 0 or 1:
		the map's own order, as WorldBuilder lays it, with the whole block moved by its patch and turned a
		quarter some times, mirrored or not.  So a patch keeps the class's picture whole and the seams fall
		only where patches meet; the same block again every `width` tiles out to the rim laid rows of the
		same boulders, and moved alone it laid the same chevrons in rows patch after patch. */
	inline void scatterTile(int tileX, int tileY, int width, unsigned patch, int *across, int *down, float cornerS[4], float cornerT[4])
	{
		const int shiftX = patch == 0 ? 0 : (int)((patch >> 3) % (unsigned)width);
		const int shiftY = patch == 0 ? 0 : (int)((patch >> 11) % (unsigned)width);
		const unsigned turn = patch == 0 ? 0u : (patch >> 19) & 7u;
		const int blockX = ((tileX + shiftX) % width + width) % width;
		const int blockY = ((tileY + shiftY) % width + width) % width;
		float turnedX[4], turnedY[4];
		float lowX = 1.0e30f, lowY = 1.0e30f;
		for (int k = 0; k < 4; ++k)
		{
			float x = (float)(blockX + (k == 1 || k == 2 ? 1 : 0)), y = (float)(blockY + (k >= 2 ? 1 : 0));
			if (turn & 4u)
				x = width - x;
			for (unsigned r = 0; r < (turn & 3u); ++r)
			{
				const float t = x;
				x = y;
				y = width - t;
			}
			turnedX[k] = x;
			turnedY[k] = y;
			lowX = x < lowX ? x : lowX;
			lowY = y < lowY ? y : lowY;
		}
		*across = (int)lowX;
		*down = (int)lowY;
		for (int k = 0; k < 4; ++k)
		{
			cornerS[k] = turnedX[k] - lowX;
			cornerT[k] = turnedY[k] - lowY;
		}
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
		from the map's edge.  The 41 unit octave stays at its mean: a 20 unit vertex grid only aliased it
		into blotches. */
	inline void variation(float worldX, float worldY, float rgb[3])
	{
		const float ax = worldX * 0.866f + worldY * 0.5f, ay = worldX * -0.5f + worldY * 0.866f;
		const float bx = worldX * 0.423f + worldY * -0.906f, by = worldX * 0.906f + worldY * 0.423f;
		const float FINE_OCTAVE_MEAN = 0.25f * 0.5f;
		const float n = 0.4f * variationNoise(ax / 290.0f, ay / 290.0f)
			+ 0.35f * variationNoise(bx / 113.0f + 17.3f, by / 113.0f + 17.3f)
			+ FINE_OCTAVE_MEAN;
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

	// The scenery on the skirt (BaseHeightMapRenderObjClass::buildSkirtScenery): woods, brush and rocks
	// standing on it, and big crags further out.  Where each goes is a hash of its grid cell and a slow
	// noise of the world position, so it is the same on every run and every machine, and nothing logic
	// reads.  What it is (which tree, which rock) is the map's to say; this only says where and how big.

	/** None within SCENERY_NEAR of the heightmap's edge, where the skirt still carries the edge on; the
		density grows to full over SCENERY_RAMP past that, falls with the fade into the horizon, and stops
		at SCENERY_FAR, where the fade has taken most of the ground's light. */
	const float SCENERY_NEAR = 40.0f;
	const float SCENERY_RAMP = 300.0f;
	const float SCENERY_FAR = 1250.0f;
	/// The grids trees, rocks and crags are tried on, world units.  A stand's trees are about TREE_STEP apart.
	const float TREE_STEP = 24.0f;
	const float ROCK_STEP = 85.0f;
	const float CRAG_STEP = 210.0f;
	/// Crags only this far out, growing in over CRAG_RAMP.
	const float CRAG_NEAR = 350.0f;
	const float CRAG_RAMP = 350.0f;
	/// The share of the skirt's land the woods hold, and how many trees stand alone in the open.
	const float WOODS_COVER = 0.42f;
	const float LONE_TREES = 0.035f;
	/// The woods' and the rocks' fields, world units a lattice cell.
	const float WOODS_WAVELENGTH = 640.0f;
	const float ROCK_WAVELENGTH = 560.0f;
	/// How wide a stand of one kind of tree is.
	const float STAND_WAVELENGTH = 280.0f;

	enum Growth
	{
		GROWTH_NONE,
		GROWTH_TREE,
		GROWTH_BUSH
	};

	/// What share of its density a point `outside` units past the edge keeps.
	inline float sceneryKeep(float outside)
	{
		if (outside <= SCENERY_NEAR || outside >= SCENERY_FAR)
			return 0.0f;
		const float ramp = smooth((outside - SCENERY_NEAR) / SCENERY_RAMP);
		const float tail = 1.0f - smooth((outside - (SCENERY_FAR - 250.0f)) / 250.0f);
		return ramp * (0.3f + 0.7f * fade(outside)) * tail;
	}

	/// The woods' field, 0 to 1: two octaves on turned lattices, through a warp, so the woods have ragged edges.
	inline float woods(float x, float y)
	{
		const float warpX = (turnedNoise(x + 811.0f, y - 2207.0f, 0.866f, 0.5f, 0.9f * WOODS_WAVELENGTH) - 0.5f) * 0.5f * WOODS_WAVELENGTH;
		const float warpY = (turnedNoise(x - 3121.0f, y + 1409.0f, 0.5f, -0.866f, 0.9f * WOODS_WAVELENGTH) - 0.5f) * 0.5f * WOODS_WAVELENGTH;
		const float n1 = turnedNoise(x + warpX, y + warpY, 0.906f, 0.423f, WOODS_WAVELENGTH);
		const float n2 = turnedNoise(x + warpX + 517.0f, y + warpY - 131.0f, -0.259f, 0.966f, 0.31f * WOODS_WAVELENGTH);
		return (n1 + 0.45f * n2) / 1.45f;
	}

	/// The rocks' field, 0 to 1.
	inline float rockiness(float x, float y)
	{
		const float n1 = turnedNoise(x - 1777.0f, y + 2903.0f, 0.616f, 0.788f, ROCK_WAVELENGTH);
		const float n2 = turnedNoise(x + 4049.0f, y + 613.0f, 0.951f, -0.309f, 0.37f * ROCK_WAVELENGTH);
		return (n1 + 0.5f * n2) / 1.5f;
	}

	/// The value of the woods' field above which the woods stand: the WOODS_COVER share of a noise
	/// that sits mostly round its middle.  0.5 holds about half.
	inline float woodsLine(void)
	{
		return 0.5f + (0.5f - WOODS_COVER) * 0.55f;
	}

	/** What grows at a tree site `outside` units past the edge, for a hash u in [0, 1): a tree inside the
		woods (the odd one in the open), brush along the woods' margin. */
	inline Growth growth(float x, float y, float outside, float u)
	{
		const float keep = sceneryKeep(outside);
		if (keep <= 0.0f)
			return GROWTH_NONE;
		const float w = woods(x, y), line = woodsLine();
		const float inside = smooth((w - line) / 0.05f);
		const float tree = keep * (LONE_TREES + (0.8f - LONE_TREES) * inside);
		if (u < tree)
			return GROWTH_TREE;
		const float margin = 1.0f - smooth(fabsf(w - (line - 0.03f)) / 0.06f);
		const float bush = keep * (0.02f + 0.2f * margin);
		return u < tree + bush ? GROWTH_BUSH : GROWTH_NONE;
	}

	/// Whether a rock site holds a rock: where the rocks' field is high, fewer deep in the woods.
	inline bool rockAt(float x, float y, float outside, float u)
	{
		const float keep = sceneryKeep(outside);
		const float inWoods = smooth((woods(x, y) - woodsLine()) / 0.08f);
		return u < keep * (0.04f + 0.6f * smooth((rockiness(x, y) - 0.55f) / 0.1f)) * (1.0f - 0.7f * inWoods);
	}

	/// Whether a crag site holds a crag: far out, where the rocks' field is high, out of the woods.
	inline bool cragAt(float x, float y, float outside, float u)
	{
		const float outer = smooth((outside - CRAG_NEAR) / CRAG_RAMP) * sceneryKeep(outside);	// not "far": windef.h defines it
		const float inWoods = smooth((woods(x, y) - woodsLine()) / 0.08f);
		return u < outer * (0.1f + 0.85f * smooth((rockiness(x, y) - 0.5f) / 0.12f)) * (1.0f - 0.8f * inWoods);
	}

	/// A grid site: its point, jittered most of a step, and four hashes in [0, 1) for what stands there.
	struct ScenerySite
	{
		float x, y;
		float u, yaw, size, rank;
	};

	inline ScenerySite scenerySite(int i, int j, float step, int salt)
	{
		ScenerySite site;
		site.x = (i + 0.1f + 0.8f * lattice(i + salt, j - 3 * salt)) * step;
		site.y = (j + 0.1f + 0.8f * lattice(i - 7 * salt, j + salt + 101)) * step;
		site.u = lattice(i + 13 * salt, j + 29);
		site.yaw = lattice(i - 41, j + 17 * salt) * 6.2831853f;
		site.size = lattice(i + 97 + salt, j - 59);
		site.rank = lattice(i - 211, j + 307 - salt);
		return site;
	}

	/** One of `count` kinds by their weights, mostly one kind over a stand: the stand's slow noise picks,
		and the site's own hash moves it a little, so the odd tree of another kind stands among them. */
	inline int pickKind(const float *weights, int count, float x, float y, float u)
	{
		float total = 0.0f;
		for (int k = 0; k < count; ++k)
			total += weights[k];
		if (count <= 0 || total <= 0.0f)
			return -1;
		const float stand = valueNoise(x / STAND_WAVELENGTH + 41.3f, y / STAND_WAVELENGTH - 17.9f);
		float p = stand * 2.3f + 0.25f * u;
		p -= floorf(p);
		p *= total;
		for (int k = 0; k < count; ++k)
		{
			if (p < weights[k])
				return k;
			p -= weights[k];
		}
		return count - 1;
	}
}

#endif
