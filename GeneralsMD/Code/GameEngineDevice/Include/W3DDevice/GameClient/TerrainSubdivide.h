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

// TerrainSubdivide.h: the drawn ground between the heightmap's samples, when each ten unit cell is
// drawn as two by two smaller ones (HeightMapRenderObjClass's subdivided tiles).
//
// A cell's nine vertices sit on a grid of half cells.  The four corners are the map's own samples,
// untouched.  The other five take a Catmull-Rom patch through the samples, the same patch the sun
// per pixel takes its normal from (TERRAIN_RELIGHT_SAMPLING in ffshader.h), so the light and the
// shape agree.  Logic never reads any of it: units stand on the old two triangle plane
// (TerrainHeightSampling), so the patch is held close to that plane.  It may leave the cell's own
// plane by at most CAP byte steps (one world unit), never rises above the cell's highest corner
// or below its lowest, and on steep cells it goes back to the plane altogether.  An edge's
// midpoint takes the tighter of its two cells' limits, so both cells give it the same height and
// no crack opens between them.
//
// Everything that lays its own triangles on the ground (the 3-way tiles, the shoreline, the
// scorches, the decals, picking) asks height() for the same points, so it lies on the
// same surface.  Nothing here knows the engine: test_terrain_subdivide includes it bare.

#pragma once

#ifndef TERRAIN_SUBDIVIDE_H
#define TERRAIN_SUBDIVIDE_H

namespace TerrainSubdivide
{
	/// Most the drawn ground may leave its cell's plane, in byte steps: 1.6 * 0.625 = one world unit.
	const float CAP = 1.6f;
	/// A cell whose corners span more byte steps than this starts back toward its plane...
	const float STEEP_START = 7.84f;
	/** ...and from this span on it is the plane: a cliff keeps EA's faces and diagonal.  It is the
		pathfinder's cliff, PATHFIND_CLIFF_SLOPE_LIMIT_F's 9.8 world units over a cell in byte steps,
		so every cell the game calls a cliff is drawn as it always was. */
	const float STEEP_END = 15.68f;

	/// The map's heights, one byte a sample, row major; a read off the map takes the nearest edge.
	struct Heights
	{
		const unsigned char *data;
		int width;
		int height;

		int at(int x, int y) const
		{
			if (x < 0) x = 0;
			if (y < 0) y = 0;
			if (x > width - 1) x = width - 1;
			if (y > height - 1) y = height - 1;
			return data[y * width + x];
		}
	};

	/// How far a point of cell (x, y) may leave the cell's plane, and the corners' range.
	struct Limit
	{
		float cap;
		float lo;
		float hi;
	};

	inline Limit cellLimit(const Heights &h, int x, int y)
	{
		if (x > h.width - 2) x = h.width - 2;
		if (y > h.height - 2) y = h.height - 2;
		if (x < 0) x = 0;
		if (y < 0) y = 0;
		const int c0 = h.at(x, y), c1 = h.at(x + 1, y), c2 = h.at(x + 1, y + 1), c3 = h.at(x, y + 1);
		int lo = c0, hi = c0;
		if (c1 < lo) lo = c1;	if (c1 > hi) hi = c1;
		if (c2 < lo) lo = c2;	if (c2 > hi) hi = c2;
		if (c3 < lo) lo = c3;	if (c3 > hi) hi = c3;
		float keep = (STEEP_END - (float)(hi - lo)) / (STEEP_END - STEEP_START);
		if (keep < 0.0f) keep = 0.0f;
		if (keep > 1.0f) keep = 1.0f;
		Limit limit = { CAP * keep, (float)lo, (float)hi };
		return limit;
	}

	/// The tighter of two cells' limits: what both can take.
	inline Limit tighter(const Limit &a, const Limit &b)
	{
		Limit limit = { a.cap < b.cap ? a.cap : b.cap, a.lo > b.lo ? a.lo : b.lo, a.hi < b.hi ? a.hi : b.hi };
		return limit;
	}

	/// The plane's value, its offset bounded by the limit.
	inline float held(float plane, float spline, const Limit &limit)
	{
		float offset = spline - plane;
		if (offset > limit.cap) offset = limit.cap;
		if (offset < -limit.cap) offset = -limit.cap;
		float value = plane + offset;
		if (value > limit.hi) value = limit.hi;
		if (value < limit.lo) value = limit.lo;
		return value;
	}

	/** A cell's two triangle plane at (a, b) in half cells (0, 1 or 2 each way), from its corners in
		the order the tiles keep them: c[0] at (0,0), c[1] at (2,0), c[2] at (2,2), c[3] at (0,2).  The
		diagonal runs c[0] to c[2], or c[1] to c[3] on a flipped cell.  The tiles' texture coordinates
		and colours go through the same blend, so a smaller triangle shows what the bigger one did. */
	inline float plane(const float c[4], bool flip, int a, int b)
	{
		// a corner is its own value to the bit, not one rounded through the blend
		if (a == 0 && b == 0) return c[0];
		if (a == 2 && b == 0) return c[1];
		if (a == 2 && b == 2) return c[2];
		if (a == 0 && b == 2) return c[3];
		const float s = 0.5f * (float)a, t = 0.5f * (float)b;
		if (!flip)
		{
			if (s >= t)
				return c[0] + s * (c[1] - c[0]) + t * (c[2] - c[1]);
			return c[0] + t * (c[3] - c[0]) + s * (c[2] - c[3]);
		}
		if (s + t <= 1.0f)
			return c[0] + s * (c[1] - c[0]) + t * (c[3] - c[0]);
		return c[2] + (1.0f - s) * (c[3] - c[2]) + (1.0f - t) * (c[1] - c[2]);
	}

	/// Catmull-Rom halfway between the middle two of four samples.
	inline float halfway(int h0, int h1, int h2, int h3)
	{
		return (float)(-h0 + 9 * h1 + 9 * h2 - h3) * (1.0f / 16.0f);
	}

	/** The drawn ground at half cell (x2, y2) of the whole map, border included, in byte steps.  flip
		is the diagonal of the cell holding the point when it is a cell's middle (both odd); the
		ground's own cell, HeightMapRenderObjClass's, is the one every caller hands in. */
	inline float height(const Heights &h, int x2, int y2, bool flip)
	{
		const int x = x2 >> 1, y = y2 >> 1;
		const bool midX = (x2 & 1) != 0, midY = (y2 & 1) != 0;
		if (!midX && !midY)
			return (float)h.at(x, y);
		if (midX && !midY)
		{
			const float spline = halfway(h.at(x - 1, y), h.at(x, y), h.at(x + 1, y), h.at(x + 2, y));
			const float edge = 0.5f * (float)(h.at(x, y) + h.at(x + 1, y));
			return held(edge, spline, tighter(cellLimit(h, x, y - 1), cellLimit(h, x, y)));
		}
		if (!midX && midY)
		{
			const float spline = halfway(h.at(x, y - 1), h.at(x, y), h.at(x, y + 1), h.at(x, y + 2));
			const float edge = 0.5f * (float)(h.at(x, y) + h.at(x, y + 1));
			return held(edge, spline, tighter(cellLimit(h, x - 1, y), cellLimit(h, x, y)));
		}
		float spline = 0.0f;
		static const float w[4] = { -1.0f / 16.0f, 9.0f / 16.0f, 9.0f / 16.0f, -1.0f / 16.0f };
		for (int j = 0; j < 4; ++j)
			spline += w[j] * halfway(h.at(x - 1, y - 1 + j), h.at(x, y - 1 + j), h.at(x + 1, y - 1 + j), h.at(x + 2, y - 1 + j));
		const float c[4] = { (float)h.at(x, y), (float)h.at(x + 1, y), (float)h.at(x + 1, y + 1), (float)h.at(x, y + 1) };
		return held(plane(c, flip, 1, 1), spline, cellLimit(h, x, y));
	}

	/** height() for every half cell of cells x0..x1 by y0..y1 (inclusive, their far edges too), times
		scale, into out[y2 * stride + x2].  The same arithmetic in the same order, with each cell's
		limit worked out once instead of once a point: a whole map's table is a million points and
		more, and the tiles' rebuild waits on it.  flip(x, y) answers a cell's diagonal. */
	template <class Flip>
	void fill(const Heights &h, int x0, int y0, int x1, int y1, const Flip &flip, float scale, float *out, int stride)
	{
		const int lastCellX = h.width - 2, lastCellY = h.height - 2;
		if (lastCellX < 0 || lastCellY < 0)
			return;
		// the cells any point here reads a limit from, as cellLimit clamps them
		int lx0 = x0 - 1, ly0 = y0 - 1, lx1 = x1 + 1, ly1 = y1 + 1;
		if (lx0 < 0) lx0 = 0;
		if (ly0 < 0) ly0 = 0;
		if (lx1 > lastCellX) lx1 = lastCellX;
		if (ly1 > lastCellY) ly1 = lastCellY;
		if (lx0 > lx1 || ly0 > ly1)
			return;
		const int lw = lx1 - lx0 + 1;
		Limit *limits = new Limit[(size_t)lw * (ly1 - ly0 + 1)];
		for (int y = ly0; y <= ly1; ++y)
			for (int x = lx0; x <= lx1; ++x)
				limits[(size_t)(y - ly0) * lw + (x - lx0)] = cellLimit(h, x, y);
		struct At
		{
			const Limit *limits; int lx0, ly0, lx1, ly1, lw;
			const Limit &operator()(int x, int y) const
			{
				if (x < lx0) x = lx0;
				if (y < ly0) y = ly0;
				if (x > lx1) x = lx1;
				if (y > ly1) y = ly1;
				return limits[(size_t)(y - ly0) * lw + (x - lx0)];
			}
		} limitAt = { limits, lx0, ly0, lx1, ly1, lw };
		static const float w[4] = { -1.0f / 16.0f, 9.0f / 16.0f, 9.0f / 16.0f, -1.0f / 16.0f };
		int x2lo = 2 * x0, y2lo = 2 * y0, x2hi = 2 * x1 + 2, y2hi = 2 * y1 + 2;
		if (x2lo < 0) x2lo = 0;
		if (y2lo < 0) y2lo = 0;
		if (x2hi > 2 * (h.width - 1)) x2hi = 2 * (h.width - 1);
		if (y2hi > 2 * (h.height - 1)) y2hi = 2 * (h.height - 1);
		for (int y2 = y2lo; y2 <= y2hi; ++y2)
		{
			const int y = y2 >> 1;
			const bool midY = (y2 & 1) != 0;
			float *row = out + (size_t)y2 * stride;
			for (int x2 = x2lo; x2 <= x2hi; ++x2)
			{
				const int x = x2 >> 1;
				const bool midX = (x2 & 1) != 0;
				float value;
				if (!midX && !midY)
					value = (float)h.at(x, y);
				else if (midX && !midY)
				{
					const float spline = halfway(h.at(x - 1, y), h.at(x, y), h.at(x + 1, y), h.at(x + 2, y));
					const float edge = 0.5f * (float)(h.at(x, y) + h.at(x + 1, y));
					value = held(edge, spline, tighter(limitAt(x, y - 1), limitAt(x, y)));
				}
				else if (!midX && midY)
				{
					const float spline = halfway(h.at(x, y - 1), h.at(x, y), h.at(x, y + 1), h.at(x, y + 2));
					const float edge = 0.5f * (float)(h.at(x, y) + h.at(x, y + 1));
					value = held(edge, spline, tighter(limitAt(x - 1, y), limitAt(x, y)));
				}
				else
				{
					float spline = 0.0f;
					for (int j = 0; j < 4; ++j)
						spline += w[j] * halfway(h.at(x - 1, y - 1 + j), h.at(x, y - 1 + j), h.at(x + 1, y - 1 + j), h.at(x + 2, y - 1 + j));
					const float c[4] = { (float)h.at(x, y), (float)h.at(x + 1, y), (float)h.at(x + 1, y + 1), (float)h.at(x, y + 1) };
					value = held(plane(c, flip(x, y), 1, 1), spline, limitAt(x, y));
				}
				row[x2] = scale * value;
			}
		}
		delete[] limits;
	}

	/** The patch's slope at half cell (x2, y2), byte steps per cell each way.  At a sample it is the
		central difference doTheLight's normal is made of. */
	inline void slope(const Heights &h, int x2, int y2, float &dx, float &dy)
	{
		const int x = x2 >> 1, y = y2 >> 1;
		static const float value0[4] = { 0.0f, 1.0f, 0.0f, 0.0f };
		static const float slope0[4] = { -0.5f, 0.0f, 0.5f, 0.0f };
		static const float valueHalf[4] = { -1.0f / 16.0f, 9.0f / 16.0f, 9.0f / 16.0f, -1.0f / 16.0f };
		static const float slopeHalf[4] = { 0.125f, -1.375f, 1.375f, -0.125f };
		const float *vx = (x2 & 1) ? valueHalf : value0;
		const float *sx = (x2 & 1) ? slopeHalf : slope0;
		const float *vy = (y2 & 1) ? valueHalf : value0;
		const float *sy = (y2 & 1) ? slopeHalf : slope0;
		dx = 0.0f;
		dy = 0.0f;
		for (int j = 0; j < 4; ++j)
			for (int i = 0; i < 4; ++i)
			{
				const float sample = (float)h.at(x - 1 + i, y - 1 + j);
				dx += vy[j] * sx[i] * sample;
				dy += sy[j] * vx[i] * sample;
			}
	}

	/** The six indices of one quad of four vertices tl, tr, br, bl, as the tiles have always cut a cell:
		tl-br is the diagonal, or tr-bl when flipped, wound the same way. */
	inline unsigned short *quad(unsigned short *ib, int tl, int tr, int br, int bl, bool flip)
	{
		if (flip)
		{
			ib[0] = (unsigned short)tr; ib[1] = (unsigned short)bl; ib[2] = (unsigned short)tl;
			ib[3] = (unsigned short)tr; ib[4] = (unsigned short)br; ib[5] = (unsigned short)bl;
		}
		else
		{
			ib[0] = (unsigned short)tl; ib[1] = (unsigned short)br; ib[2] = (unsigned short)bl;
			ib[3] = (unsigned short)tl; ib[4] = (unsigned short)tr; ib[5] = (unsigned short)br;
		}
		return ib + 6;
	}

	/** The 24 indices of a cell's four quads, its nine vertices row by row from first: every quad
		takes the cell's diagonal, so the cell's own diagonal is two of their edges. */
	inline unsigned short *cell(unsigned short *ib, int first, bool flip)
	{
		for (int b = 0; b < 2; ++b)
			for (int a = 0; a < 2; ++a)
			{
				const int tl = first + b * 3 + a;
				ib = quad(ib, tl, tl + 1, tl + 4, tl + 3, flip);
			}
		return ib;
	}
}

#endif // TERRAIN_SUBDIVIDE_H
