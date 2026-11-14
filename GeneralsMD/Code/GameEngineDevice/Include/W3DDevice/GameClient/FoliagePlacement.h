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

// FoliagePlacement.h: where the GroundFoliage option's grass tufts stand (W3DGrassBuffer).
//
// A cell's density is a byte, 255 being MAX_TUFTS_PER_CELL tufts.  It comes from the Terrain.ini
// class of the cell's tile, averaged with the classes of the blend tiles laid over it, and is nought
// on cliffs, steep cells and under water.  Every tuft is a hash of its cell and its slot, never a
// random draw: the same map grows the same grass every time, and a chunk rebuilt after a building
// went up puts every surviving tuft back where it was.  The rank is a second hash; a chunk sorts its
// tufts by it, so the first n of them are a spatially even subset and thinning is drawing fewer.
//
// The client's alone.  Nothing in GameEngine includes this or W3DGrassBuffer, and nothing here
// knows the engine: test_foliage_placement includes it bare.

#pragma once

#ifndef FOLIAGE_PLACEMENT_H
#define FOLIAGE_PLACEMENT_H

#include <math.h>

namespace FoliagePlacement
{
	const int MAX_TUFTS_PER_CELL = 3;
	const int ATLAS_COLUMNS = 4;
	const int ATLAS_ROWS = 2;
	const int SWAY_TYPES = 10;	///< Trees.vso reads c[8 + type]; c8 is the still entry, 1..10 sway
	/// Rise over run past which a cell grows nothing.
	const float MAX_SLOPE = 0.6f;

	/// What a Terrain.ini class grows.  W3DGrassBuffer maps TerrainClass onto these.
	enum Kind
	{
		KIND_NONE,
		KIND_FULL,		///< grass, accent grass, field
		KIND_REGION,	///< the Swiss and Eastern Europe sets: full where the name says grass, half otherwise
		KIND_SPARSE		///< cobblestone grass, beach park
	};

	/// Whether a texture name holds "grass", any case.
	inline bool nameHasGrass(const char *name)
	{
		if (name == 0)
			return false;
		for (const char *p = name; *p; ++p)
		{
			const char *w = "grass";
			const char *q = p;
			while (*w && *q && (*q | 0x20) == *w)
			{
				++q;
				++w;
			}
			if (*w == 0)
				return true;
		}
		return false;
	}

	inline unsigned char kindDensity(Kind kind, bool nameSaysGrass)
	{
		switch (kind)
		{
		case KIND_FULL: return 255;
		case KIND_REGION: return nameSaysGrass ? 255 : 128;
		case KIND_SPARSE: return 72;
		default: return 0;
		}
	}

	/// The mean of the base tile's density and every blend laid over it, rounded.
	inline unsigned char blendMean(int base, bool hasOverlay, int overlay, bool hasExtra, int extra)
	{
		int sum = base, layers = 1;
		if (hasOverlay)
		{
			sum += overlay;
			++layers;
		}
		if (hasExtra)
		{
			sum += extra;
			++layers;
		}
		return (unsigned char)((sum + layers / 2) / layers);
	}

	/// A cell's four corner heights in world units, against its side: too steep to grow on.
	inline bool tooSteep(float h00, float h10, float h01, float h11, float cellSize)
	{
		float lo = h00, hi = h00;
		const float h[3] = { h10, h01, h11 };
		for (int i = 0; i < 3; ++i)
		{
			if (h[i] < lo) lo = h[i];
			if (h[i] > hi) hi = h[i];
		}
		return hi - lo > MAX_SLOPE * cellSize;
	}

	/// What grows on a cell: nought where it is blocked (cliff, steep, under water), the blend mean otherwise.
	inline unsigned char cellDensity(unsigned char blended, bool blocked)
	{
		return blocked ? 0 : blended;
	}

	inline unsigned int hash(int x, int y, int k)
	{
		unsigned int h = (unsigned int)x * 0x8da6b343u ^ (unsigned int)y * 0xd8163841u ^ (unsigned int)k * 0xcb1ab31fu;
		h ^= h >> 16;
		h *= 0x7feb352du;
		h ^= h >> 15;
		h *= 0x846ca68bu;
		h ^= h >> 16;
		return h;
	}

	/// [0, 1), 24 bits of the hash.
	inline float unit(unsigned int h)
	{
		return (float)(h >> 8) * (1.0f / 16777216.0f);
	}

	enum Salt
	{
		SALT_PRESENT = 0x100,
		SALT_X = 0x200,
		SALT_Y = 0x300,
		SALT_YAW = 0x400,
		SALT_SCALE = 0x500,
		SALT_KIND = 0x600,
		SALT_RANK = 0x700
	};

	/// Whether slot k (0..MAX_TUFTS_PER_CELL-1) of cell (x, y) holds a tuft.  A tuft standing at one
	/// density stands at every higher one, so a thinner class is the same grass with gaps.
	inline bool tuftPresent(int x, int y, int k, unsigned char density)
	{
		return unit(hash(x, y, k + SALT_PRESENT)) * 255.0f < (float)density;
	}

	struct Tuft
	{
		float fx, fy;		///< where in the cell, 0..1 each way
		float yaw;			///< radians, 0..pi/2: the two crossed quads repeat after a quarter turn
		float scale;		///< 0.75..1.25 of the base size
		int atlasCell;	///< 0..7: 0-3 green, 4-5 dry, 6-7 flowering
		bool mirror;		///< the texture flipped left to right
		unsigned int rank;
	};

	/// Tuft k of cell (x, y): a function of the three alone.
	inline Tuft placeTuft(int x, int y, int k)
	{
		Tuft t;
		t.fx = 0.08f + 0.84f * unit(hash(x, y, k + SALT_X));
		t.fy = 0.08f + 0.84f * unit(hash(x, y, k + SALT_Y));
		t.yaw = 1.5707963f * unit(hash(x, y, k + SALT_YAW));
		t.scale = 0.75f + 0.5f * unit(hash(x, y, k + SALT_SCALE));
		const unsigned int kh = hash(x, y, k + SALT_KIND);
		const float pick = unit(kh);
		if (pick < 0.70f)
			t.atlasCell = (int)(kh & 3);
		else if (pick < 0.88f)
			t.atlasCell = 4 + (int)(kh & 1);
		else
			t.atlasCell = 6 + (int)(kh & 1);
		t.mirror = (kh & 4) != 0;
		t.rank = hash(x, y, k + SALT_RANK);
		return t;
	}

	/// The sway constant a tuft at (px, py) reads, 1..SWAY_TYPES: bands across the wind waveLen long,
	/// so the phase stepping from one type to the next walks a gust over the field.
	inline int swayType(float px, float py, float dirX, float dirY, float waveLen)
	{
		const float along = (px * dirX + py * dirY) / waveLen * (float)SWAY_TYPES;
		int band = (int)floorf(along) % SWAY_TYPES;
		if (band < 0)
			band += SWAY_TYPES;
		return 1 + band;
	}

	/// A density grid over world space: cell (i, j) is centred on origin + (i + 0.5, j + 0.5) * cellSize.
	struct Grid
	{
		unsigned char *cells;
		int width;
		int height;
		float cellSize;
		float originX;
		float originY;
	};

	/// The cells a clear looked at, inclusive; empty when lo > hi.
	struct Range
	{
		int loX, loY, hiX, hiY;
	};

	inline Range cellRange(const Grid &g, float minX, float minY, float maxX, float maxY)
	{
		Range r;
		r.loX = (int)ceilf((minX - g.originX) / g.cellSize - 0.5f);
		r.loY = (int)ceilf((minY - g.originY) / g.cellSize - 0.5f);
		r.hiX = (int)floorf((maxX - g.originX) / g.cellSize - 0.5f);
		r.hiY = (int)floorf((maxY - g.originY) / g.cellSize - 0.5f);
		if (r.loX < 0) r.loX = 0;
		if (r.loY < 0) r.loY = 0;
		if (r.hiX > g.width - 1) r.hiX = g.width - 1;
		if (r.hiY > g.height - 1) r.hiY = g.height - 1;
		return r;
	}

	/// Clears every cell whose centre lies inside the box of half sides (halfX, halfY) centred on
	/// (cx, cy) and turned by angle radians.  Returns the cells cleared, and in range what was looked at.
	inline int clearBox(Grid &g, float cx, float cy, float halfX, float halfY, float angle, Range *range = 0)
	{
		const float c = cosf(angle), s = sinf(angle);
		const float ex = fabsf(c) * halfX + fabsf(s) * halfY;
		const float ey = fabsf(s) * halfX + fabsf(c) * halfY;
		const Range r = cellRange(g, cx - ex, cy - ey, cx + ex, cy + ey);
		if (range)
			*range = r;
		int cleared = 0;
		for (int j = r.loY; j <= r.hiY; ++j)
			for (int i = r.loX; i <= r.hiX; ++i)
			{
				const float dx = g.originX + (i + 0.5f) * g.cellSize - cx;
				const float dy = g.originY + (j + 0.5f) * g.cellSize - cy;
				const float u = dx * c + dy * s;
				const float v = -dx * s + dy * c;
				if (fabsf(u) <= halfX && fabsf(v) <= halfY && g.cells[j * g.width + i] != 0)
				{
					g.cells[j * g.width + i] = 0;
					++cleared;
				}
			}
		return cleared;
	}

	/// Clears every cell whose centre lies within radius of the segment a-b (a road, or a circle when a == b).
	inline int clearCapsule(Grid &g, float ax, float ay, float bx, float by, float radius, Range *range = 0)
	{
		const Range r = cellRange(g, fminf(ax, bx) - radius, fminf(ay, by) - radius,
			fmaxf(ax, bx) + radius, fmaxf(ay, by) + radius);
		if (range)
			*range = r;
		const float sx = bx - ax, sy = by - ay;
		const float len2 = sx * sx + sy * sy;
		int cleared = 0;
		for (int j = r.loY; j <= r.hiY; ++j)
			for (int i = r.loX; i <= r.hiX; ++i)
			{
				const float px = g.originX + (i + 0.5f) * g.cellSize - ax;
				const float py = g.originY + (j + 0.5f) * g.cellSize - ay;
				float t = len2 > 0.0f ? (px * sx + py * sy) / len2 : 0.0f;
				if (t < 0.0f) t = 0.0f;
				if (t > 1.0f) t = 1.0f;
				const float dx = px - t * sx, dy = py - t * sy;
				if (dx * dx + dy * dy <= radius * radius && g.cells[j * g.width + i] != 0)
				{
					g.cells[j * g.width + i] = 0;
					++cleared;
				}
			}
		return cleared;
	}
}

#endif
