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

// FoliagePlacement.h: where the GroundFoliage option's grass, shrubs, twigs, pebbles and rocks stand
// (W3DGrassBuffer).
//
// A cell is CELL_BYTES bytes: grass density (255 being MAX_TUFTS_PER_CELL tufts), stone density,
// twig density and flags (the rock its stones are made of, cliff, cliff foot, grass edge).  The
// densities come from the Terrain.ini class of the cell's tile, averaged with the classes of the
// blend tiles laid over it; cliffs, steep cells and water grow nothing, and only an impassable cliff
// gathers boulders at its foot.  Everything is a hash of its cell and its slot, never a random draw: the same
// map grows the same ground every time, and a chunk rebuilt after a building went up puts every
// survivor back where it was.  The rank is a second hash; a chunk sorts its cards by it, so the first
// n of them are a spatially even subset and thinning is drawing fewer.
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
	const int ATLAS_ROWS = 4;		///< 1024 square, cells of 256

	/// Atlas cells past the grass's 0-7 (0-3 green, 4-5 dry, 6-7 flowering).
	enum AtlasCell
	{
		ATLAS_SHRUB_LEAFY = 8,
		ATLAS_SHRUB_SCRUB = 9,
		ATLAS_SHRUB_BERRY = 10,
		ATLAS_TWIGS = 11,				///< seen from above, centred
		ATLAS_GRANITE = 12,			///< opaque rock faces for the stone meshes
		ATLAS_SANDSTONE = 13,
		ATLAS_BASALT = 14,
		ATLAS_PEBBLES = 15			///< seen from above, with alpha
	};

	/// A cell's bytes.
	enum CellByte
	{
		BYTE_GRASS,
		BYTE_STONES,
		BYTE_TWIGS,
		BYTE_FLAGS,
		CELL_BYTES
	};

	enum Rock
	{
		ROCK_GRANITE,
		ROCK_SANDSTONE,
		ROCK_BASALT
	};

	enum CellFlag
	{
		CF_ROCK_MASK = 0x03,		///< Rock
		CF_CLIFF = 0x04,				///< impassable: nothing grows, a rare small stone on a ledge
		CF_CLIFF_FOOT = 0x08,		///< open ground beside a cliff or steep cell: stones, boulders and bushes gather
		CF_EDGE = 0x10,					///< grass meets bare ground: shrubs gather
		CF_LEDGE = 0x20,				///< a cliff cell no steeper than LEDGE_SPAN: a small stone can lie on it
		CF_STEEP = 0x40					///< passable but too steep to hold anything: a face's foot is past it, like a cliff's
	};

	/// A cliff cell whose corners span no more than this many cell sides can hold a small stone.
	const float LEDGE_SPAN = 1.5f;

	/// What bare ground a class is, for its stones and twigs.  W3DGrassBuffer maps TerrainClass onto these.
	enum Bare
	{
		BARE_NONE,		///< grass, or nothing that takes stones
		BARE_DIRT,
		BARE_ROCKY,
		BARE_DESERT,
		BARE_SNOW,
		BARE_PAVED		///< concrete, asphalt, town: nothing
	};
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

	/// Whether a name holds word (lower case letters), any case.
	inline bool nameHas(const char *name, const char *word)
	{
		if (name == 0)
			return false;
		for (const char *p = name; *p; ++p)
		{
			const char *w = word;
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

	/// Whether a texture name holds "grass", any case.
	inline bool nameHasGrass(const char *name)
	{
		return nameHas(name, "grass");
	}

	/// A class the engine's table leaves vague (the Swiss and Eastern Europe sets, transitions), by name.
	inline Bare bareByName(const char *name, Bare otherwise)
	{
		if (nameHas(name, "snow"))
			return BARE_SNOW;
		if (nameHas(name, "rock") || nameHas(name, "stone") || nameHas(name, "gravel") || nameHas(name, "cliff")
			|| nameHas(name, "rubble"))
			return BARE_ROCKY;
		if (nameHas(name, "sand") || nameHas(name, "desert") || nameHas(name, "dune"))
			return BARE_DESERT;
		if (nameHas(name, "dirt") || nameHas(name, "mud") || nameHas(name, "soil") || nameHas(name, "earth")
			|| nameHas(name, "path") || nameHas(name, "forest"))
			return BARE_DIRT;
		if (nameHas(name, "concrete") || nameHas(name, "asphalt") || nameHas(name, "road") || nameHas(name, "tile")
			|| nameHas(name, "brick"))
			return BARE_PAVED;
		return otherwise;
	}

	inline unsigned char stoneDensity(Bare bare)
	{
		switch (bare)
		{
		case BARE_DIRT: return 110;
		case BARE_ROCKY: return 255;
		case BARE_DESERT: return 90;
		case BARE_SNOW: return 25;
		default: return 0;
		}
	}

	/// Fallen twigs: under grass, and on dirt.
	inline unsigned char twigDensity(unsigned char grass, Bare bare)
	{
		const int fromGrass = grass * 90 / 255;
		const int fromBare = bare == BARE_DIRT ? 60 : 0;
		return (unsigned char)(fromGrass > fromBare ? fromGrass : fromBare);
	}

	/// The stone a class's rocks are cut from: sandstone in the desert, basalt where the name says dark
	/// or burnt, granite everywhere else (snow and the mountains included).
	inline Rock rockOf(Bare bare, const char *name)
	{
		if (nameHas(name, "lava") || nameHas(name, "volcan") || nameHas(name, "basalt") || nameHas(name, "black")
			|| nameHas(name, "dark") || nameHas(name, "burnt") || nameHas(name, "scorch"))
			return ROCK_BASALT;
		if (bare == BARE_DESERT || nameHas(name, "sand") || nameHas(name, "desert"))
			return ROCK_SANDSTONE;
		return ROCK_GRANITE;
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
		SALT_RANK = 0x700,
		SALT_PIECE = 0x800,		///< + the piece: whether it stands
		SALT_ROCK = 0x900			///< the rock meshes' corners
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

	/// What a chunk lays down.  A cell holds up to MAX_TUFTS_PER_CELL tufts and at most one of each other.
	enum Piece
	{
		PIECE_TUFT,
		PIECE_SHRUB,		///< three crossed cards, rooted like a tuft
		PIECE_TWIGS,		///< a flat card on the ground
		PIECE_PEBBLES,	///< a flat card on the ground
		PIECE_STONE,		///< a small rock mesh, knee high at most
		PIECE_BOULDER,	///< a big rock mesh, resting at the foot of a cliff
		PIECE_KINDS
	};

	/// Cards thin out with distance; solid things (shrubs, rocks) stand at any distance, so nothing a
	/// unit could seem to drive through comes and goes.
	inline bool pieceThins(Piece piece)
	{
		return piece == PIECE_TUFT || piece == PIECE_TWIGS || piece == PIECE_PEBBLES;
	}

	/// Bare ground a dry scrub grows on: dirt or desert (their stones), no grass, not rock or snow.
	inline bool scrubGround(const unsigned char *cell)
	{
		return cell[BYTE_GRASS] == 0 && cell[BYTE_STONES] >= 60 && cell[BYTE_STONES] < 200;
	}

	/// Whether the 3 by 3 block of cells (x, y) is in holds boulders at a cliff's foot: two in five do.
	inline bool boulderBlock(int x, int y)
	{
		const int bx = x >= 0 ? x / 3 : (x - 2) / 3, by = y >= 0 ? y / 3 : (y - 2) / 3;
		return unit(hash(bx, by, SALT_PIECE + 0x50)) < 0.4f;
	}

	/// The chance cell (x, y)'s bytes give a piece other than a tuft.
	inline float pieceChance(Piece piece, const unsigned char *cell, int x, int y)
	{
		const unsigned char flags = cell[BYTE_FLAGS];
		const bool foot = (flags & CF_CLIFF_FOOT) != 0;
		switch (piece)
		{
		// thickest where a field gives way to bare ground, and in what grows at a cliff's foot
		// and the odd dry scrub on desert and dirt, more where they meet grass
		case PIECE_SHRUB:
			return cell[BYTE_GRASS] / 255.0f * ((flags & CF_EDGE) ? 0.45f : 0.08f)
				+ ((foot && (cell[BYTE_GRASS] != 0 || cell[BYTE_TWIGS] != 0)) ? 0.20f : 0.0f)
				+ (scrubGround(cell) ? ((flags & CF_EDGE) ? 0.06f : 0.008f) : 0.0f);
		case PIECE_TWIGS: return cell[BYTE_TWIGS] / 255.0f * ((flags & CF_EDGE) ? 0.05f : 0.02f);
		case PIECE_PEBBLES: return cell[BYTE_STONES] / 255.0f * 0.15f;
		// on open ground a unit drives through a stone, so few of them there; the foot of a cliff is
		// where they gather, and a ledge gentle enough holds the odd one
		case PIECE_STONE:
			if (flags & CF_CLIFF)
				return (flags & CF_LEDGE) ? 0.03f : 0.0f;
			return cell[BYTE_STONES] / 255.0f * 0.035f + (foot ? 0.30f : 0.0f);
		// resting at a cliff's foot, never on its face: a few hundred a map
		// in loose clusters: two blocks of 3 by 3 cells in five hold some, 15% of their foot cells
		// each, so a cliff's foot shows a line of them with gaps and never its face
		case PIECE_BOULDER:
			return (foot && boulderBlock(x, y)) ? 0.15f : 0.0f;
		default: return 0.0f;
		}
	}

	/// Whether cell (x, y) holds the piece (not a tuft: tuftPresent is theirs).
	inline bool piecePresent(int x, int y, Piece piece, const unsigned char *cell)
	{
		return unit(hash(x, y, SALT_PIECE + piece)) < pieceChance(piece, cell, x, y);
	}

	/// The slot a piece's position, turn, size and rank are hashed from, past the tufts' own.
	inline int pieceSlot(Piece piece)
	{
		return MAX_TUFTS_PER_CELL + piece;
	}

	/// A shrub's atlas cell: dry scrub leads on thin or edge grass, the leafy bush on a full field.
	inline int shrubCell(int x, int y, const unsigned char *cell)
	{
		// dry where the class is: grass blended with dirt or sand, thin grass, or no grass at all; a
		// green field's edge stays green, or its bushes read as dead patches
		const float u = unit(hash(x, y, SALT_KIND + 0x40));
		const bool dry = cell[BYTE_GRASS] < 100 || cell[BYTE_STONES] >= 60;
		if (dry)
			return u < 0.75f ? ATLAS_SHRUB_SCRUB : ATLAS_SHRUB_LEAFY;
		return u < 0.7f ? ATLAS_SHRUB_LEAFY : (u < 0.95f ? ATLAS_SHRUB_BERRY : ATLAS_SHRUB_SCRUB);
	}

	inline int rockCell(Rock rock)
	{
		return rock == ROCK_SANDSTONE ? ATLAS_SANDSTONE : (rock == ROCK_BASALT ? ATLAS_BASALT : ATLAS_GRANITE);
	}

	/// The stone meshes: an icosahedron, its corners pulled in and out by a fixed hash and flattened.
	const int ROCK_CORNERS = 12;
	const int ROCK_FACES = 20;
	const int ROCK_VARIANTS = 8;
	const float ROCK_FLATTEN = 0.62f;	///< of the radius, upward
	const float ROCK_BELOW = 0.6f;		///< what is left of that under the middle: buried anyway

	inline const int (*rockFaces(void))[3]
	{
		static const int faces[ROCK_FACES][3] =
		{
			{ 0, 11, 5 }, { 0, 5, 1 }, { 0, 1, 7 }, { 0, 7, 10 }, { 0, 10, 11 },
			{ 1, 5, 9 }, { 5, 11, 4 }, { 11, 10, 2 }, { 10, 7, 6 }, { 7, 1, 8 },
			{ 3, 9, 4 }, { 3, 4, 2 }, { 3, 2, 6 }, { 3, 6, 8 }, { 3, 8, 9 },
			{ 4, 9, 5 }, { 2, 4, 11 }, { 6, 2, 10 }, { 8, 6, 7 }, { 9, 8, 1 }
		};
		return faces;
	}

	const float ROCK_MAX_RADIUS = 1.25f;	///< rockCorners' farthest corner

	/// Variant v's corners, radius one give or take: x and y out to ROCK_MAX_RADIUS, z up to
	/// ROCK_FLATTEN of that.  Jittered hard enough that no two faces read as one prism.
	inline void rockCorners(int variant, float out[ROCK_CORNERS][3])
	{
		const float t = 1.6180340f;
		static const float base[ROCK_CORNERS][3] =
		{
			{ -1, t, 0 }, { 1, t, 0 }, { -1, -t, 0 }, { 1, -t, 0 },
			{ 0, -1, t }, { 0, 1, t }, { 0, -1, -t }, { 0, 1, -t },
			{ t, 0, -1 }, { t, 0, 1 }, { -t, 0, -1 }, { -t, 0, 1 }
		};
		for (int i = 0; i < ROCK_CORNERS; ++i)
		{
			const float len = sqrtf(base[i][0] * base[i][0] + base[i][1] * base[i][1] + base[i][2] * base[i][2]);
			const float r = (0.65f + (ROCK_MAX_RADIUS - 0.65f) * unit(hash(variant, i, SALT_ROCK))) / len;
			out[i][0] = base[i][0] * r;
			out[i][1] = base[i][1] * r;
			float z = base[i][2] * r * ROCK_FLATTEN;
			if (z < 0.0f)
				z *= ROCK_BELOW;
			out[i][2] = z;
		}
	}

	/// The point a detailed rock (a boulder) splits edge a-b at: the edge's middle pushed out by a
	/// hashed share.  The same for (a, b) and (b, a), so the two faces on an edge meet with no crack.
	inline void rockMidpoint(int variant, int a, int b, const float corners[ROCK_CORNERS][3], float out[3])
	{
		const int lo = a < b ? a : b, hi = a < b ? b : a;
		const float push = 1.04f + 0.2f * unit(hash(variant, lo * ROCK_CORNERS + hi, SALT_ROCK + 1));
		for (int i = 0; i < 3; ++i)
			out[i] = (corners[lo][i] + corners[hi][i]) * 0.5f * push;
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

	/// A grid over world space: cell (i, j) is centred on origin + (i + 0.5, j + 0.5) * cellSize and is
	/// stride bytes from cells + (j * width + i) * stride.
	struct Grid
	{
		unsigned char *cells;
		int width;
		int height;
		float cellSize;
		float originX;
		float originY;
		int stride;
	};

	/// Zeroes cell (i, j); TRUE if it held anything.
	inline bool clearCell(Grid &g, int i, int j)
	{
		unsigned char *c = g.cells + (j * g.width + i) * g.stride;
		bool held = false;
		for (int b = 0; b < g.stride; ++b)
		{
			held = held || c[b] != 0;
			c[b] = 0;
		}
		return held;
	}

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
				if (fabsf(u) <= halfX && fabsf(v) <= halfY && clearCell(g, i, j))
					++cleared;
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
				if (dx * dx + dy * dy <= radius * radius && clearCell(g, i, j))
					++cleared;
			}
		return cleared;
	}
}

#endif
