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
#include <vector>

namespace FoliagePlacement
{
	const int MAX_TUFTS_PER_CELL = 3;
	const int ATLAS_COLUMNS = 4;
	const int ATLAS_ROWS = 8;		///< 1024 by 2048, cells of 256

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
		ATLAS_PEBBLES = 15,			///< seen from above, with alpha
		ATLAS_BUSH_GREEN = 16,	///< a bush's canopy from above, laid over the dome meshes
		ATLAS_BUSH_DRY = 17,
		ATLAS_FERN = 18,				///< rooted like a tuft
		ATLAS_SAPLING = 19,			///< a young conifer, rooted
		ATLAS_LEAF_LITTER = 20,	///< from above, under broadleaf trees
		ATLAS_NEEDLE_LITTER = 21,	///< from above, under conifers
		ATLAS_LOG = 22,					///< a mossy fallen log from above, lying corner to corner
		ATLAS_STUMP = 23,				///< rooted
		ATLAS_DEAD_BARK = 24,		///< bleached dead wood, tiling; the firs' trunks too
		// 25 to 29 unused
		ATLAS_FIR_SPIRE = 30,		///< the crown tuft, bottom rooted
		ATLAS_FIR_WHORL = 31		///< a tier from above: a starburst of needles over the whole cell, centred
	};

	/// A cell's bytes.
	enum CellByte
	{
		BYTE_GRASS,
		BYTE_STONES,
		BYTE_TWIGS,
		BYTE_FLAGS,
		BYTE_CANOPY,		///< batched trees within GROVE_RADIUS of the cell's middle, 255 at most
		BYTE_KIND,			///< the base tile's Bare in the low bits, and KIND_BORDER, KIND_HUG
		BYTE_LIGHT,			///< how bright the ground's painting is, its tiles' average luminance, 0..255
		CELL_BYTES
	};

	/// Ground this bright is open white snow: no undergrowth on it, which would sit on it as black
	/// specks.  Patchy snow among grass stays under it and keeps its floor: Alpine Assault's grove
	/// floor is 85% under 160, every Winter Wolf snow cell over it.
	const int WHITE_GROUND = 160;

	inline bool whiteGround(const unsigned char *cell)
	{
		return cell[BYTE_LIGHT] > WHITE_GROUND;
	}

	enum KindBits
	{
		KIND_BARE_MASK = 0x07,
		KIND_BORDER = 0x08,		///< outside the playable map, in the border strip no unit reaches
		KIND_HUG = 0x10				///< beside an impassable cell, above it or below
	};

	/// How far a tree's floor reaches, in world units.
	const float GROVE_RADIUS = 40.0f;

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
		CF_STEEP = 0x40,				///< passable but too steep to hold anything: a face's foot is past it, like a cliff's
		CF_CONIFER = 0x80				///< most of the trees over the cell are conifers: needles, not leaves
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

	/// A tree model that drops needles: its name says pine, fir, spruce, conifer, or a snowy or alpine tree.
	inline bool coniferName(const char *name)
	{
		return nameHas(name, "pine") || nameHas(name, "fir") || nameHas(name, "spruce") || nameHas(name, "conifer")
			|| nameHas(name, "alpine") || nameHas(name, "snow");
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
		PIECE_SHRUB,		///< a dome mesh with a canopy painted over it from above
		PIECE_TWIGS,		///< a flat card on the ground
		PIECE_PEBBLES,	///< a flat card on the ground
		PIECE_STONE,		///< a small rock mesh, knee high at most
		PIECE_BOULDER,	///< a big rock mesh, resting at the foot of a cliff
		PIECE_LITTER,		///< leaves or needles under trees, a flat card
		PIECE_FERN,			///< rooted cards, under trees
		PIECE_SAPLING,	///< a young conifer at a grove's edge, rooted cards
		PIECE_LOG,			///< a fallen log in a grove, a flat card
		PIECE_STUMP,		///< rooted cards, in a grove
		PIECE_DEAD_TREE,	///< a bleached leafless tree, where no unit goes, on dry ground
		PIECE_FIR,				///< a snowy fir, where no unit goes, on snow
		PIECE_KINDS
	};

	/// Cards thin out with distance; solid things (bushes, rocks, logs, saplings) stand at any
	/// distance, so nothing a unit could seem to drive through comes and goes.
	inline bool pieceThins(Piece piece)
	{
		return piece == PIECE_TUFT || piece == PIECE_TWIGS || piece == PIECE_PEBBLES || piece == PIECE_LITTER
			|| piece == PIECE_FERN;
	}

	/// Whether a cell has ground something can lie on: grass, dirt, sand or rock, not a cliff,
	/// a steep face, water or pavement.
	inline bool hasGround(const unsigned char *cell)
	{
		return cell[BYTE_GRASS] != 0 || cell[BYTE_STONES] != 0 || cell[BYTE_TWIGS] != 0;
	}

	/// The tuft density under the trees: the tall grass thins where the canopy is thick.
	inline unsigned char groveGrass(const unsigned char *cell)
	{
		const int trees = cell[BYTE_CANOPY];
		if (trees >= 3)
			return (unsigned char)(cell[BYTE_GRASS] * 2 / 5);
		if (trees == 2)
			return (unsigned char)(cell[BYTE_GRASS] * 7 / 10);
		return cell[BYTE_GRASS];
	}

	/// Bare ground a dry scrub grows on: dirt or desert (their stones), no grass, not rock or snow.
	inline bool scrubGround(const unsigned char *cell)
	{
		return cell[BYTE_GRASS] == 0 && cell[BYTE_STONES] >= 60 && cell[BYTE_STONES] < 200;
	}

	/// Whether the 3 by 3 block of cells (x, y) is in holds boulders at a cliff's foot: two in five do.
	inline bool blockActive(int x, int y, int size, int salt, float share)
	{
		const int bx = x >= 0 ? x / size : (x - size + 1) / size, by = y >= 0 ? y / size : (y - size + 1) / size;
		return unit(hash(bx, by, salt)) < share;
	}

	inline bool boulderBlock(int x, int y)
	{
		return blockActive(x, y, 3, SALT_PIECE + 0x50, 0.4f);
	}

	inline Bare cellBare(const unsigned char *cell)
	{
		return (Bare)(cell[BYTE_KIND] & KIND_BARE_MASK);
	}

	/// Ground no unit reaches: an impassable cell or the border strip.  Big trees stand only here, so
	/// nothing drives through a trunk or hides under a fir.  The lane beside a cliff (KIND_HUG) is open
	/// ground and is left to the stones.
	inline bool unreachable(const unsigned char *cell)
	{
		return (cell[BYTE_FLAGS] & CF_CLIFF) != 0 || (cell[BYTE_KIND] & KIND_BORDER) != 0;
	}

	/// The pieces drawn out to the far radius and never folded: big enough to be seen from as far as
	/// the map's own trees.
	inline bool pieceFar(Piece piece)
	{
		return piece == PIECE_DEAD_TREE || piece == PIECE_FIR;
	}

	/// The cards lying flat on the ground.  Everything else stands up and casts into the sun's map;
	/// a flat card's shadow would fall on the ground it already lies on.
	inline bool pieceFlat(Piece piece)
	{
		return piece == PIECE_TWIGS || piece == PIECE_PEBBLES || piece == PIECE_LITTER || piece == PIECE_LOG;
	}

	/// Where a piece goes in a chunk's buffers, in this order: the big trees, the rest that never
	/// thins, the flat ones of those, then the upright cards and the flat cards, each by rank.  A
	/// chunk past the near ring draws the first group, the screen everything up to its thinning, the
	/// sun's map every upright group.
	enum PieceGroup
	{
		GROUP_FAR,
		GROUP_FIXED,
		GROUP_FIXED_FLAT,
		GROUP_THIN,
		GROUP_THIN_FLAT,
		PIECE_GROUPS
	};

	inline PieceGroup pieceGroup(Piece piece)
	{
		if (pieceFar(piece))
			return GROUP_FAR;
		const bool flat = pieceFlat(piece);
		if (pieceThins(piece))
			return flat ? GROUP_THIN_FLAT : GROUP_THIN;
		return flat ? GROUP_FIXED_FLAT : GROUP_FIXED;
	}

	/// The first piece of a group in a chunk laid out by PieceGroup.
	inline int groupStart(const int groups[PIECE_GROUPS], int group)
	{
		int start = 0;
		for (int g = 0; g < group; ++g)
			start += groups[g];
		return start;
	}

	struct PieceRange
	{
		int first, count;
	};

	/// What the screen draws of a chunk: everything that never thins, the first upright of its
	/// upright cards and the first flat of its flat ones; past the near ring the big trees alone.
	inline void screenRanges(const int groups[PIECE_GROUPS], bool farOnly, int upright, int flat, PieceRange out[2])
	{
		if (farOnly)
		{
			out[0].first = 0; out[0].count = groups[GROUP_FAR];
			out[1].first = 0; out[1].count = 0;
			return;
		}
		out[0].first = 0;
		out[0].count = groupStart(groups, GROUP_THIN) + upright;
		out[1].first = groupStart(groups, GROUP_THIN_FLAT);
		out[1].count = flat;
	}

	/// What the sun's map takes of it: the screen's upright pieces, no flat one.
	inline void casterRanges(const int groups[PIECE_GROUPS], bool farOnly, int upright, PieceRange out[2])
	{
		out[0].first = 0;
		out[0].count = farOnly ? groups[GROUP_FAR] : groups[GROUP_FAR] + groups[GROUP_FIXED];
		out[1].first = groupStart(groups, GROUP_THIN);
		out[1].count = farOnly ? 0 : upright;
	}

	/// Dry ground a dead tree belongs on: desert and sand, and rock or dirt the class calls sandstone.
	inline bool dryGround(const unsigned char *cell)
	{
		const Bare bare = cellBare(cell);
		const bool sandstone = (cell[BYTE_FLAGS] & CF_ROCK_MASK) == ROCK_SANDSTONE;
		return bare == BARE_DESERT || (sandstone && (bare == BARE_ROCKY || bare == BARE_DIRT));
	}

	/// The chance cell (x, y)'s bytes give a piece other than a tuft.
	inline float pieceChance(Piece piece, const unsigned char *cell, int x, int y)
	{
		const unsigned char flags = cell[BYTE_FLAGS];
		const bool foot = (flags & CF_CLIFF_FOOT) != 0;
		const int trees = cell[BYTE_CANOPY];
		// open white snow keeps its floor bare: no bush, fern, sapling, litter, log or stump on it, only
		// firs and stones, or they sit on it as black specks; patchy snow among grass keeps them
		const bool ground = hasGround(cell) && !whiteGround(cell);
		switch (piece)
		{
		// thickest where a field gives way to bare ground, in what grows at a cliff's foot and under
		// a grove, and the odd dry scrub on desert and dirt, more where they meet grass
		case PIECE_SHRUB:
			if (whiteGround(cell))
				return 0.0f;
			return cell[BYTE_GRASS] / 255.0f * ((flags & CF_EDGE) ? 0.45f : 0.08f)
				+ ((foot && (cell[BYTE_GRASS] != 0 || cell[BYTE_TWIGS] != 0)) ? 0.20f : 0.0f)
				+ (scrubGround(cell) ? ((flags & CF_EDGE) ? 0.06f : 0.008f) : 0.0f)
				+ (ground ? (trees >= 3 ? 0.25f : (trees == 2 ? 0.10f : 0.0f)) : 0.0f);
		// a grove's floor, a thicket under the trees: bushes and ferns, saplings where it thins out,
		// litter only under the canopy of three trees or more (round a lone trunk it read as a rust
		// spot), the odd log and stump where it is thick
		case PIECE_LITTER:
			return (ground && trees >= 3) ? 0.45f : 0.0f;
		case PIECE_FERN:
			return (ground && trees > 0) ? (trees >= 3 ? 0.35f : 0.06f * trees) : 0.0f;
		case PIECE_SAPLING:
			return ground ? (trees == 2 ? 0.12f : (trees >= 3 ? 0.03f : 0.0f)) : 0.0f;
		case PIECE_LOG:
			return (ground && trees >= 3) ? 0.012f : 0.0f;
		case PIECE_STUMP:
			return (ground && trees >= 2) ? 0.012f : 0.0f;
		// big trees where no unit goes: dead ones in loose groups of one to three on dry ground,
		// snowy firs in clumps of a few on snow
		case PIECE_DEAD_TREE:
			return (unreachable(cell) && dryGround(cell) && blockActive(x, y, 5, SALT_PIECE + 0x51, 0.3f)) ? 0.06f : 0.0f;
		case PIECE_FIR:
			return (unreachable(cell) && cellBare(cell) == BARE_SNOW && blockActive(x, y, 4, SALT_PIECE + 0x52, 0.35f))
				? 0.25f : 0.0f;
		case PIECE_TWIGS: return cell[BYTE_TWIGS] / 255.0f * ((flags & CF_EDGE) ? 0.05f : 0.02f);
		case PIECE_PEBBLES: return cell[BYTE_STONES] / 255.0f * 0.15f;
		// on open ground a unit drives through a stone, so few of them there; the foot of a cliff is
		// where they gather, and a ledge gentle enough holds the odd one
		case PIECE_STONE:
			if (flags & CF_CLIFF)
				return (flags & CF_LEDGE) ? 0.03f : 0.0f;
			return cell[BYTE_STONES] / 255.0f * 0.035f + (foot ? 0.30f : 0.0f);
		// resting at a cliff's foot, never on its face: a few hundred a map
		// in loose clusters: two blocks of 3 by 3 cells in five hold some, 22% of their foot cells
		// each, so a cliff's foot shows a line of them with gaps and never its face
		case PIECE_BOULDER:
			return (foot && boulderBlock(x, y)) ? 0.22f : 0.0f;
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

	/// A bush's look: dry scrub (the upright cards of ATLAS_SHRUB_SCRUB, which read as scrub on sand
	/// where a dark dome read as a burnt hole) where the class is dry (grass blended with dirt or sand,
	/// thin grass, or none), a green dome on a field and its edges or under trees.
	inline int shrubCell(int x, int y, const unsigned char *cell)
	{
		const float u = unit(hash(x, y, SALT_KIND + 0x40));
		const bool dry = (cell[BYTE_GRASS] < 100 || cell[BYTE_STONES] >= 60) && cell[BYTE_CANOPY] < 2;
		if (dry)
			return u < 0.75f ? ATLAS_SHRUB_SCRUB : ATLAS_BUSH_GREEN;
		return u < 0.95f ? ATLAS_BUSH_GREEN : ATLAS_SHRUB_SCRUB;
	}

	/// The bush meshes: a squashed dome of three rings of BUSH_RING corners (on the ground, half way up
	/// turned half a step, near the top) and a crown, each corner pulled a little by a fixed hash.  Open
	/// underneath; nothing sees it.  The canopy painting is laid over it from above out to its cell's
	/// edge, so the painted alpha cuts the rim ragged.
	const int BUSH_RING = 12;
	const int BUSH_CORNERS = BUSH_RING * 3 + 1;
	const int BUSH_FACES = BUSH_RING * 5;
	const int BUSH_VARIANTS = 6;
	const int BUSH_TOP = BUSH_RING * 3;		///< the crown's corner

	struct BushFaceTable
	{
		int faces[BUSH_FACES][3];
		BushFaceTable(void)
		{
			int n = 0;
			for (int band = 0; band < 2; ++band)
			{
				const int lo = band * BUSH_RING, hi = lo + BUSH_RING;
				for (int i = 0; i < BUSH_RING; ++i)
				{
					const int j = (i + 1) % BUSH_RING;
					// the middle ring sits half a step round from the other two
					const int a = lo + i, b = lo + j;
					const int c = band == 0 ? hi + i : hi + j, d = band == 0 ? hi + j : hi + i;
					if (band == 0)
					{
						faces[n][0] = a; faces[n][1] = b; faces[n][2] = c; ++n;
						faces[n][0] = b; faces[n][1] = d; faces[n][2] = c; ++n;
					}
					else
					{
						faces[n][0] = a; faces[n][1] = c; faces[n][2] = d; ++n;
						faces[n][0] = a; faces[n][1] = b; faces[n][2] = c; ++n;
					}
				}
			}
			for (int i = 0; i < BUSH_RING; ++i)
			{
				faces[n][0] = BUSH_RING * 2 + i;
				faces[n][1] = BUSH_RING * 2 + (i + 1) % BUSH_RING;
				faces[n][2] = BUSH_TOP;
				++n;
			}
		}
	};

	inline const int (*bushFaces(void))[3]
	{
		static const BushFaceTable table;
		return table.faces;
	}

	/// Variant v's corners: radius about one on the ground, the crown about 0.85 up.
	inline void bushCorners(int variant, float out[BUSH_CORNERS][3])
	{
		const float step = 6.2831853f / BUSH_RING;
		for (int i = 0; i < BUSH_RING; ++i)
		{
			const float a = i * step, m = (i + 0.5f) * step;
			const float r = 0.9f + 0.2f * unit(hash(variant, i, SALT_ROCK + 2));
			const float rm = 0.84f + 0.16f * unit(hash(variant, i + 16, SALT_ROCK + 2));
			const float zm = 0.42f + 0.1f * unit(hash(variant, i + 32, SALT_ROCK + 2));
			const float ru = 0.5f + 0.12f * unit(hash(variant, i + 48, SALT_ROCK + 2));
			const float zu = 0.72f + 0.08f * unit(hash(variant, i + 64, SALT_ROCK + 2));
			out[i][0] = cosf(a) * r;
			out[i][1] = sinf(a) * r;
			out[i][2] = 0.0f;
			out[BUSH_RING + i][0] = cosf(m) * rm;
			out[BUSH_RING + i][1] = sinf(m) * rm;
			out[BUSH_RING + i][2] = zm;
			out[BUSH_RING * 2 + i][0] = cosf(a) * ru;
			out[BUSH_RING * 2 + i][1] = sinf(a) * ru;
			out[BUSH_RING * 2 + i][2] = zu;
		}
		out[BUSH_TOP][0] = 0.12f * (unit(hash(variant, 80, SALT_ROCK + 2)) - 0.5f);
		out[BUSH_TOP][1] = 0.12f * (unit(hash(variant, 81, SALT_ROCK + 2)) - 0.5f);
		out[BUSH_TOP][2] = 0.86f + 0.08f * unit(hash(variant, 82, SALT_ROCK + 2));
	}

	/// A tree of our own, about one unit tall, standing on its origin.  Each vertex carries the atlas
	/// cell its u, v (0..1 inside the cell) belong to and an outward normal.
	struct TreeVertex
	{
		float x, y, z;
		float nx, ny, nz;
		float u, v;
		int cell;
	};

	struct TreeMesh
	{
		std::vector<TreeVertex> vertices;
		std::vector<unsigned short> indices;
	};

	const int DEAD_TREE_VARIANTS = 6;
	const int FIR_VARIANTS = 4;

	/// A tapered prism from a to b, radius ra to rb, sides round, the cell wrapped once round it and
	/// once along it.
	inline void addLimb(TreeMesh &m, const float a[3], const float b[3], float ra, float rb, int sides, int cell)
	{
		float d[3] = { b[0] - a[0], b[1] - a[1], b[2] - a[2] };
		const float len = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
		if (len <= 0.0f)
			return;
		d[0] /= len; d[1] /= len; d[2] /= len;
		const float side[3] = { fabsf(d[2]) < 0.9f ? 0.0f : 1.0f, 0.0f, fabsf(d[2]) < 0.9f ? 1.0f : 0.0f };
		float e1[3] = { d[1] * side[2] - d[2] * side[1], d[2] * side[0] - d[0] * side[2], d[0] * side[1] - d[1] * side[0] };
		const float l1 = sqrtf(e1[0] * e1[0] + e1[1] * e1[1] + e1[2] * e1[2]);
		e1[0] /= l1; e1[1] /= l1; e1[2] /= l1;
		const float e2[3] = { d[1] * e1[2] - d[2] * e1[1], d[2] * e1[0] - d[0] * e1[2], d[0] * e1[1] - d[1] * e1[0] };
		const unsigned short first = (unsigned short)m.vertices.size();
		for (int end = 0; end < 2; ++end)
			for (int i = 0; i <= sides; ++i)
			{
				const float t = 6.2831853f * i / sides, c = cosf(t), s = sinf(t);
				const float n[3] = { e1[0] * c + e2[0] * s, e1[1] * c + e2[1] * s, e1[2] * c + e2[2] * s };
				const float *at = end ? b : a;
				const float r = end ? rb : ra;
				TreeVertex v = { at[0] + n[0] * r, at[1] + n[1] * r, at[2] + n[2] * r, n[0], n[1], n[2],
					(float)i / sides, end ? 0.0f : 1.0f, cell };
				m.vertices.push_back(v);
			}
		for (int i = 0; i < sides; ++i)
		{
			const unsigned short a0 = (unsigned short)(first + i), a1 = (unsigned short)(a0 + 1);
			const unsigned short b0 = (unsigned short)(a0 + sides + 1), b1 = (unsigned short)(b0 + 1);
			const unsigned short tri[6] = { a0, a1, b1, a0, b1, b0 };
			m.indices.insert(m.indices.end(), tri, tri + 6);
		}
	}

	/// A dead tree: a trunk in three bends, three to five gnarled branches off its upper half, each
	/// in two bends with one or two twigs off it.  Six sided trunk, five sided limbs, 130 to 240 faces.
	inline void deadTreeMesh(int variant, TreeMesh &m)
	{
		m.vertices.clear();
		m.indices.clear();
		int salt = 0;
		#define DT_U() unit(hash(variant, salt++, SALT_ROCK + 3))
		float joints[4][3] = { { 0.0f, 0.0f, -0.05f } };
		float dir[3] = { 0.0f, 0.0f, 1.0f };
		float radius[4];
		radius[0] = 0.055f + 0.015f * DT_U();
		for (int s = 0; s < 3; ++s)
		{
			dir[0] += 0.5f * (DT_U() - 0.5f);
			dir[1] += 0.5f * (DT_U() - 0.5f);
			const float l = sqrtf(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);
			dir[0] /= l; dir[1] /= l; dir[2] /= l;
			const float len = 0.26f + 0.06f * DT_U();
			for (int k = 0; k < 3; ++k)
				joints[s + 1][k] = joints[s][k] + dir[k] * len;
			radius[s + 1] = radius[0] * (0.8f - 0.17f * s);
			addLimb(m, joints[s], joints[s + 1], radius[s], radius[s + 1], 6, ATLAS_DEAD_BARK);
		}
		const int branches = 3 + (int)(hash(variant, 99, SALT_ROCK + 3) % 3);
		for (int k = 0; k < branches; ++k)
		{
			// off the upper two bends
			const int seg = 1 + (int)(DT_U() * 2.0f) % 2;
			const float f = 0.3f + 0.6f * DT_U();
			float p[3];
			for (int i = 0; i < 3; ++i)
				p[i] = joints[seg][i] + (joints[seg + 1][i] - joints[seg][i]) * f;
			const float r0 = (radius[seg] + (radius[seg + 1] - radius[seg]) * f) * 0.6f;
			const float az = 6.2831853f * (k + 0.3f * (DT_U() - 0.5f)) / branches;
			float tilt = 0.6f + 0.5f * DT_U();
			float bend[3] = { cosf(az) * sinf(tilt), sinf(az) * sinf(tilt), cosf(tilt) };
			const float length = 0.3f + 0.2f * DT_U();
			float q[3];
			for (int i = 0; i < 3; ++i)
				q[i] = p[i] + bend[i] * length * 0.55f;
			addLimb(m, p, q, r0, r0 * 0.6f, 5, ATLAS_DEAD_BARK);
			// the gnarl: the second bend turns away and droops or climbs
			tilt += 0.5f * (DT_U() - 0.3f);
			const float az2 = az + 0.6f * (DT_U() - 0.5f);
			const float bend2[3] = { cosf(az2) * sinf(tilt), sinf(az2) * sinf(tilt), cosf(tilt) };
			float tip[3];
			for (int i = 0; i < 3; ++i)
				tip[i] = q[i] + bend2[i] * length * 0.45f;
			addLimb(m, q, tip, r0 * 0.6f, r0 * 0.15f, 5, ATLAS_DEAD_BARK);
			const int twigs = 1 + (int)(DT_U() * 2.0f) % 2;
			for (int j = 0; j < twigs; ++j)
			{
				const float azt = az + (j == 0 ? 0.9f : -0.9f) + 0.3f * (DT_U() - 0.5f);
				const float tt = tilt * 0.7f;
				const float td[3] = { cosf(azt) * sinf(tt), sinf(azt) * sinf(tt), cosf(tt) };
				float twig[3];
				for (int i = 0; i < 3; ++i)
					twig[i] = q[i] + td[i] * length * 0.4f;
				addLimb(m, q, twig, r0 * 0.4f, r0 * 0.1f, 5, ATLAS_DEAD_BARK);
			}
		}
		#undef DT_U
	}

	/// The fir in units of its height, which the buffer makes 46 to 52 like the retail pines.
	const float FIR_TRUNK_RADIUS = 0.012f;	///< about 0.6 units at the foot, to a point at the top
	const float FIR_LEAN = 0.02f;						///< the trunk's top off the axis at most
	const float FIR_BARE_LOW = 0.25f;				///< bare trunk under the lowest tier's hanging corners, 25 to 40%
	const float FIR_BARE_SPREAD = 0.15f;
	const float FIR_TIER_STEP = 0.065f;			///< a tier every 3.2 units
	const int FIR_TIERS_MIN = 10;
	const int FIR_TIERS_MAX = 14;
	const float FIR_TOP_TIER = 0.93f;
	const float FIR_REACH_LOW = 0.21f;			///< a tier's corner from the trunk, lowest tier: about 1 wide to 3 high
	const float FIR_REACH_TOP = 0.05f;			///< and top tier, linear between
	const float FIR_DROOP_LOW = 0.349f;			///< the hanging corners, 20 degrees at the lowest tier
	const float FIR_DROOP_TOP = 0.593f;			///< to 34 at the top
	const float FIR_TIER_OFFSET = 0.03f;		///< a tier's centre off the trunk at most, 1.5 units
	const float FIR_TUFT = 0.08f;						///< the crown tuft's height and width

	/// One whole atlas cell as a quad: the cell's point (anchorU, anchorV) at anchor, across along
	/// the cell's u, down along its v, every corner lit by normal (a unit vector).
	inline void addCellQuad(TreeMesh &m, const float anchor[3], float anchorU, float anchorV, const float across[3],
		const float down[3], const float normal[3], int cell)
	{
		const unsigned short first = (unsigned short)m.vertices.size();
		for (int corner = 0; corner < 4; ++corner)
		{
			const float u = (corner == 1 || corner == 2) ? 1.0f : 0.0f, v = corner >= 2 ? 1.0f : 0.0f;
			const float du = u - anchorU, dv = v - anchorV;
			TreeVertex t = { anchor[0] + across[0] * du + down[0] * dv, anchor[1] + across[1] * du + down[1] * dv,
				anchor[2] + across[2] * du + down[2] * dv, normal[0], normal[1], normal[2], u, v, cell };
			m.vertices.push_back(t);
		}
		const unsigned short tri[6] = { first, (unsigned short)(first + 1), (unsigned short)(first + 2),
			first, (unsigned short)(first + 2), (unsigned short)(first + 3) };
		m.indices.insert(m.indices.end(), tri, tri + 6);
	}

	/// A snowy fir as the retail pines are made: a pale bark spike, a bare stretch of trunk, then
	/// ten tiers a step or less apart, each one square whorl card centred on the trunk and folded along a diagonal
	/// into a tent, the other two corners hanging 20 degrees at the bottom to 34 at the top, and a
	/// tuft of two crossed cards on top.  32 to 36 faces.
	inline void firMesh(int variant, TreeMesh &m)
	{
		m.vertices.clear();
		m.indices.clear();
		int salt = 0;
		#define FIR_U() unit(hash(variant, salt++, SALT_ROCK + 4))
		const float leanAngle = 6.2831853f * FIR_U(), lean = FIR_LEAN * FIR_U();
		const float trunkBase[3] = { 0.0f, 0.0f, -0.05f };
		const float trunkTop[3] = { cosf(leanAngle) * lean, sinf(leanAngle) * lean, FIR_TOP_TIER + FIR_TUFT * 0.5f };
		addLimb(m, trunkBase, trunkTop, FIR_TRUNK_RADIUS, 0.0f, 4, ATLAS_DEAD_BARK);
		// the lowest tier's hanging corners at the bare height, so the droop hides none of it
		const float bare = FIR_BARE_LOW + FIR_BARE_SPREAD * FIR_U() + FIR_REACH_LOW * tanf(FIR_DROOP_LOW);
		int tiers = (int)((FIR_TOP_TIER - bare) / FIR_TIER_STEP + 1.5f);
		tiers = tiers < FIR_TIERS_MIN ? FIR_TIERS_MIN : (tiers > FIR_TIERS_MAX ? FIR_TIERS_MAX : tiers);
		for (int k = 0; k < tiers; ++k)
		{
			const float rise = (float)k / (tiers - 1);
			const float z = bare + (FIR_TOP_TIER - bare) * rise;
			const float reach = FIR_REACH_LOW + (FIR_REACH_TOP - FIR_REACH_LOW) * rise;
			const float droop = FIR_DROOP_LOW + (FIR_DROOP_TOP - FIR_DROOP_LOW) * rise;
			// the trunk leans, so each tier follows it, plus its own small offset
			const float offAngle = 6.2831853f * FIR_U(), off = FIR_TIER_OFFSET * FIR_U();
			const float cx = trunkTop[0] * z + cosf(offAngle) * off, cy = trunkTop[1] * z + sinf(offAngle) * off;
			const float yaw = 6.2831853f * FIR_U();
			const unsigned short first = (unsigned short)m.vertices.size();
			for (int corner = 0; corner < 4; ++corner)
			{
				// corners 0 and 2 are the fold, 1 and 3 hang
				const float a = yaw + 1.5707963f * corner, c = cosf(a), s = sinf(a);
				const bool hangs = (corner & 1) != 0;
				const float drop = hangs ? reach * tanf(droop) : 0.0f;
				const float tilt = hangs ? droop : 0.0f;
				TreeVertex t = { cx + c * reach, cy + s * reach, z - drop, c * sinf(tilt), s * sinf(tilt), cosf(tilt),
					(corner == 1 || corner == 2) ? 1.0f : 0.0f, corner >= 2 ? 1.0f : 0.0f, ATLAS_FIR_WHORL };
				m.vertices.push_back(t);
			}
			const unsigned short tri[6] = { first, (unsigned short)(first + 1), (unsigned short)(first + 2),
				first, (unsigned short)(first + 2), (unsigned short)(first + 3) };
			m.indices.insert(m.indices.end(), tri, tri + 6);
		}
		const float tuftYaw = 6.2831853f * FIR_U();
		const float tuftRoot[3] = { trunkTop[0], trunkTop[1], FIR_TOP_TIER - FIR_TUFT * 0.25f };
		for (int card = 0; card < 2; ++card)
		{
			const float a = tuftYaw + 1.5707963f * card, c = cosf(a), s = sinf(a);
			const float across[3] = { c * FIR_TUFT, s * FIR_TUFT, 0.0f }, down[3] = { 0.0f, 0.0f, -FIR_TUFT };
			const float normal[3] = { -s * 0.6f, c * 0.6f, 0.8f };
			addCellQuad(m, tuftRoot, 0.5f, 1.0f, across, down, normal, ATLAS_FIR_SPIRE);
		}
		#undef FIR_U
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
