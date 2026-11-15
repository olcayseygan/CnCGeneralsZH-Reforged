// test_foliage_placement.cpp: where the GroundFoliage option's grass stands
// (W3DDevice/GameClient/FoliagePlacement.h).  Blends average, blocked cells grow nothing, a tuft is a
// function of its cell alone, the rank's prefix is an even subset, a turned footprint clears exactly
// the cells whose centres it covers, and every sway type is one Trees.vso has a constant for.

#include "test_harness.h"

#include "W3DDevice/GameClient/FoliagePlacement.h"

#include <math.h>
#include <algorithm>
#include <vector>

using namespace FoliagePlacement;

TEST(foliage_class_density_follows_the_name_only_for_region_sets)
{
	CHECK_EQ((int)kindDensity(KIND_FULL, false), 255);
	CHECK_EQ((int)kindDensity(KIND_REGION, true), 255);
	CHECK_EQ((int)kindDensity(KIND_REGION, false), 128);
	CHECK((int)kindDensity(KIND_SPARSE, false) > 0);
	CHECK((int)kindDensity(KIND_SPARSE, false) < 128);
	CHECK_EQ((int)kindDensity(KIND_NONE, true), 0);
	CHECK(nameHasGrass("TSwissGrassLight"));
	CHECK(nameHasGrass("GRASS"));
	CHECK(!nameHasGrass("SwissRock"));
	CHECK(!nameHasGrass("gras"));
	CHECK(!nameHasGrass(0));
}

TEST(foliage_blend_is_the_mean_of_every_layer)
{
	CHECK_EQ((int)blendMean(255, false, 0, false, 0), 255);
	CHECK_EQ((int)blendMean(255, true, 0, false, 0), 128);
	CHECK_EQ((int)blendMean(0, true, 255, false, 0), 128);
	CHECK_EQ((int)blendMean(255, true, 255, true, 0), 170);
	CHECK_EQ((int)blendMean(90, false, 255, false, 255), 90);	// absent layers do not count
}

TEST(foliage_cliff_slope_and_water_grow_nothing)
{
	CHECK_EQ((int)cellDensity(255, true), 0);
	CHECK_EQ((int)cellDensity(200, false), 200);
	CHECK(!tooSteep(10.0f, 12.0f, 11.0f, 13.0f, 10.0f));
	CHECK(tooSteep(10.0f, 17.0f, 10.0f, 10.0f, 10.0f));
	CHECK(tooSteep(0.0f, 0.0f, 0.0f, -6.5f, 10.0f));
	for (int k = 0; k < MAX_TUFTS_PER_CELL; ++k)
		CHECK(!tuftPresent(3, 4, k, 0));
}

TEST(foliage_placement_depends_on_the_cell_alone)
{
	int differ = 0;
	for (int y = 0; y < 20; ++y)
		for (int x = 0; x < 20; ++x)
			for (int k = 0; k < MAX_TUFTS_PER_CELL; ++k)
			{
				const Tuft a = placeTuft(x, y, k), b = placeTuft(x, y, k);
				CHECK(a.fx == b.fx && a.fy == b.fy && a.yaw == b.yaw && a.rank == b.rank && a.atlasCell == b.atlasCell);
				CHECK(a.fx > 0.0f && a.fx < 1.0f && a.fy > 0.0f && a.fy < 1.0f);
				CHECK(a.atlasCell >= 0 && a.atlasCell < ATLAS_SHRUB_LEAFY);	// a tuft is one of the grass cells
				CHECK(a.scale >= 0.75f && a.scale <= 1.25f);
				const Tuft n = placeTuft(x + 1, y, k);
				if (n.fx != a.fx || n.fy != a.fy)
					++differ;
				// a tuft present at one density stays at every higher one
				for (int d = 0; d < 255; d += 17)
					if (tuftPresent(x, y, k, (unsigned char)d))
						CHECK(tuftPresent(x, y, k, (unsigned char)(d + 17)));
			}
	CHECK(differ > 20 * 20 * MAX_TUFTS_PER_CELL - 4);
	// full density fills every slot, half density about half of them
	int half = 0, all = 0;
	for (int y = 0; y < 64; ++y)
		for (int x = 0; x < 64; ++x)
			for (int k = 0; k < MAX_TUFTS_PER_CELL; ++k)
			{
				all += tuftPresent(x, y, k, 255) ? 1 : 0;
				half += tuftPresent(x, y, k, 128) ? 1 : 0;
			}
	CHECK_EQ(all, 64 * 64 * MAX_TUFTS_PER_CELL);
	CHECK(half > all * 45 / 100 && half < all * 55 / 100);
}

TEST(foliage_rank_prefix_is_an_even_subset)
{
	struct Ranked { unsigned int rank; int x, y; };
	for (int chunk = 0; chunk < 4; ++chunk)
	{
		const int ox = chunk * 16, oy = chunk * 32;
		std::vector<Ranked> all;
		for (int y = oy; y < oy + 16; ++y)
			for (int x = ox; x < ox + 16; ++x)
				for (int k = 0; k < MAX_TUFTS_PER_CELL; ++k)
				{
					Ranked r = { placeTuft(x, y, k).rank, x - ox, y - oy };
					all.push_back(r);
				}
		std::sort(all.begin(), all.end(), [](const Ranked &a, const Ranked &b) { return a.rank < b.rank; });
		// the first quarter, split into four quadrants of 8x8 cells: 48 each on average
		int quadrant[4] = { 0, 0, 0, 0 };
		for (size_t i = 0; i < all.size() / 4; ++i)
			++quadrant[(all[i].x >= 8 ? 1 : 0) + (all[i].y >= 8 ? 2 : 0)];
		for (int q = 0; q < 4; ++q)
			CHECK(quadrant[q] > 28 && quadrant[q] < 68);
	}
}

namespace {

// Inside the turned box, by its four edges as half planes: the corners, then a cross product per edge.
bool insideTurnedBox(float px, float py, float cx, float cy, float hx, float hy, float angle)
{
	const float c = (float)cos(angle), s = (float)sin(angle);
	float corner[4][2];
	const float lx[4] = { -hx, hx, hx, -hx }, ly[4] = { -hy, -hy, hy, hy };
	for (int i = 0; i < 4; ++i)
	{
		corner[i][0] = cx + lx[i] * c - ly[i] * s;
		corner[i][1] = cy + lx[i] * s + ly[i] * c;
	}
	for (int i = 0; i < 4; ++i)
	{
		const float *a = corner[i], *b = corner[(i + 1) % 4];
		if ((b[0] - a[0]) * (py - a[1]) - (b[1] - a[1]) * (px - a[0]) < -1e-3f)
			return false;
	}
	return true;
}

}

TEST(foliage_turned_footprint_clears_the_cells_whose_centres_it_covers)
{
	const float angles[] = { 0.0f, 0.3f, 0.785398f, 1.2f, 2.9f, -0.7f };
	for (size_t a = 0; a < sizeof(angles) / sizeof(angles[0]); ++a)
	{
		std::vector<unsigned char> cells(40 * 40, 255);
		Grid g = { &cells[0], 40, 40, 10.0f, -30.0f, -30.0f, 1 };
		const float cx = 101.0f, cy = 88.0f, hx = 47.0f, hy = 22.0f;	// no centre on an edge unturned
		Range range;
		const int cleared = clearBox(g, cx, cy, hx, hy, angles[a], &range);
		int expect = 0, wrong = 0;
		for (int j = 0; j < 40; ++j)
			for (int i = 0; i < 40; ++i)
			{
				const float px = -30.0f + (i + 0.5f) * 10.0f, py = -30.0f + (j + 0.5f) * 10.0f;
				const bool in = insideTurnedBox(px, py, cx, cy, hx, hy, angles[a]);
				expect += in ? 1 : 0;
				if (in != (cells[j * 40 + i] == 0))
					++wrong;
				if (in)
					CHECK(i >= range.loX && i <= range.hiX && j >= range.loY && j <= range.hiY);
			}
		CHECK_EQ(wrong, 0);
		CHECK_EQ(cleared, expect);
		CHECK(expect > 30);
		CHECK_EQ(clearBox(g, cx, cy, hx, hy, angles[a]), 0);	// cleared once
	}
}

TEST(foliage_road_clears_a_capsule)
{
	std::vector<unsigned char> cells(30 * 30, 200);
	Grid g = { &cells[0], 30, 30, 10.0f, 0.0f, 0.0f, 1 };
	clearCapsule(g, 50.0f, 50.0f, 250.0f, 50.0f, 15.0f);
	CHECK_EQ((int)cells[5 * 30 + 15], 0);		// on the line
	CHECK_EQ((int)cells[6 * 30 + 15], 0);		// 15 off it, on the radius
	CHECK_EQ((int)cells[7 * 30 + 15], 200);	// 25 off it
	CHECK_EQ((int)cells[5 * 30 + 27], 200);	// past the far end
	clearCapsule(g, 150.0f, 200.0f, 150.0f, 200.0f, 12.0f);	// a circle
	CHECK_EQ((int)cells[19 * 30 + 14], 0);
	CHECK_EQ((int)cells[19 * 30 + 16], 200);
}

TEST(foliage_sway_type_is_one_of_the_ten_and_walks_with_the_wind)
{
	const float dirs[][2] = { { 1.0f, 0.0f }, { 0.0f, -1.0f }, { 0.6f, 0.8f }, { -0.707f, 0.707f } };
	for (int d = 0; d < 4; ++d)
	{
		int seen[SWAY_TYPES + 2] = { 0 };
		for (int i = -500; i < 500; ++i)
		{
			const float px = i * 7.3f, py = i * -3.1f + 40.0f;
			const int t = swayType(px, py, dirs[d][0], dirs[d][1], 200.0f);
			CHECK(t >= 1 && t <= SWAY_TYPES);
			if (t >= 1 && t <= SWAY_TYPES)
				++seen[t];
		}
		for (int t = 1; t <= SWAY_TYPES; ++t)
			CHECK(seen[t] > 0);
	}
	// one band, a twentieth of a wave downwind, is the next type
	CHECK_EQ(swayType(25.0f, 0.0f, 1.0f, 0.0f, 200.0f), 2);
	CHECK_EQ(swayType(45.0f, 0.0f, 1.0f, 0.0f, 200.0f), 3);
	CHECK_EQ(swayType(-5.0f, 0.0f, 1.0f, 0.0f, 200.0f), 10);
}

namespace {

int countPiece(Piece piece, const unsigned char cell[CELL_BYTES])
{
	int n = 0;
	for (int y = 0; y < 100; ++y)
		for (int x = 0; x < 100; ++x)
			n += piecePresent(x, y, piece, cell) ? 1 : 0;
	return n;
}

}

TEST(foliage_boulders_rest_at_cliff_feet_and_never_on_the_face)
{
	const unsigned char open[CELL_BYTES] = { 255, 255, 255, 0 };
	const unsigned char cliff[CELL_BYTES] = { 0, 0, 0, CF_CLIFF };
	const unsigned char ledge[CELL_BYTES] = { 0, 0, 0, CF_CLIFF | CF_LEDGE };
	const unsigned char foot[CELL_BYTES] = { 0, 0, 0, CF_CLIFF_FOOT };
	const unsigned char grassyFoot[CELL_BYTES] = { 255, 0, 90, CF_CLIFF_FOOT };
	const unsigned char bare[CELL_BYTES] = { 0, 0, 0, 0 };
	CHECK_EQ(countPiece(PIECE_BOULDER, open), 0);
	CHECK_EQ(countPiece(PIECE_BOULDER, cliff), 0);
	CHECK_EQ(countPiece(PIECE_BOULDER, ledge), 0);
	const int boulders = countPiece(PIECE_BOULDER, foot);
	CHECK(boulders > 550 && boulders < 1300);	// 9% of 10000, in clusters
	// clustered: the boulders of a 3 by 3 block all fall in the blocks that hold some
	int inQuietBlocks = 0;
	for (int y = 0; y < 100; ++y)
		for (int x = 0; x < 100; ++x)
			if (!boulderBlock(x, y) && piecePresent(x, y, PIECE_BOULDER, foot))
				++inQuietBlocks;
	CHECK_EQ(inQuietBlocks, 0);
	CHECK_EQ(countPiece(PIECE_STONE, cliff), 0);		// a steep face holds nothing
	CHECK(countPiece(PIECE_STONE, ledge) > 150);		// a gentle one the odd small stone
	CHECK_EQ(countPiece(PIECE_PEBBLES, cliff), 0);
	CHECK_EQ(countPiece(PIECE_SHRUB, cliff), 0);
	CHECK_EQ(countPiece(PIECE_SHRUB, foot), 0);			// nothing grows on bare rock
	CHECK(countPiece(PIECE_SHRUB, grassyFoot) > countPiece(PIECE_SHRUB, open));
	CHECK_EQ(countPiece(PIECE_STONE, bare), 0);
	CHECK(countPiece(PIECE_STONE, foot) > 2500);
	CHECK(countPiece(PIECE_STONE, foot) > countPiece(PIECE_STONE, open) * 5);	// few on open ground
	for (int p = 0; p < PIECE_KINDS; ++p)
		if (p != PIECE_TUFT)
			CHECK_EQ(countPiece((Piece)p, bare), 0);
}

TEST(foliage_shrubs_gather_where_grass_meets_bare_ground)
{
	const unsigned char field[CELL_BYTES] = { 255, 0, 90, 0 };
	const unsigned char edge[CELL_BYTES] = { 255, 0, 90, CF_EDGE };
	const int inField = countPiece(PIECE_SHRUB, field), atEdge = countPiece(PIECE_SHRUB, edge);
	CHECK(inField > 550 && inField < 1100);	// 8%
	CHECK(atEdge > inField * 3);
	CHECK(countPiece(PIECE_TWIGS, edge) > countPiece(PIECE_TWIGS, field));
	CHECK(countPiece(PIECE_TWIGS, edge) < 250);	// 1.8%: a few, not a carpet
	CHECK_EQ(countPiece(PIECE_PEBBLES, field), 0);	// no gravel on the lawn
	// a green field's bushes are green, even at its edge; dirt and sand get the dry scrub
	const unsigned char dirtyGrass[CELL_BYTES] = { 180, 110, 90, CF_EDGE };
	const unsigned char desertEdge[CELL_BYTES] = { 0, 90, 0, CF_EDGE };
	const unsigned char desert[CELL_BYTES] = { 0, 90, 0, 0 };
	const unsigned char rock[CELL_BYTES] = { 0, 255, 0, CF_EDGE };
	int scrubGreen = 0, scrubDry = 0;
	for (int y = 0; y < 40; ++y)
		for (int x = 0; x < 40; ++x)
		{
			scrubGreen += shrubCell(x, y, edge) == ATLAS_SHRUB_SCRUB ? 1 : 0;
			scrubDry += shrubCell(x, y, dirtyGrass) == ATLAS_SHRUB_SCRUB ? 1 : 0;
		}
	CHECK(scrubGreen < 1600 / 10);
	CHECK(scrubDry > 1600 / 2);
	CHECK(countPiece(PIECE_SHRUB, desertEdge) > countPiece(PIECE_SHRUB, desert));
	CHECK(countPiece(PIECE_SHRUB, desert) > 0);
	CHECK_EQ(countPiece(PIECE_SHRUB, rock), 0);	// bare rock grows nothing
	for (int y = 0; y < 30; ++y)
		for (int x = 0; x < 30; ++x)
		{
			const int c = shrubCell(x, y, edge);
			CHECK(c == ATLAS_BUSH_GREEN || c == ATLAS_SHRUB_SCRUB);
		}
	CHECK(pieceThins(PIECE_TUFT) && pieceThins(PIECE_TWIGS) && pieceThins(PIECE_PEBBLES));
	CHECK(!pieceThins(PIECE_SHRUB) && !pieceThins(PIECE_STONE) && !pieceThins(PIECE_BOULDER));
}

TEST(foliage_bare_ground_and_rock_follow_the_class)
{
	CHECK_EQ((int)bareByName("SwissRockyPath", BARE_NONE), (int)BARE_ROCKY);
	CHECK_EQ((int)bareByName("TEEuropeMud", BARE_NONE), (int)BARE_DIRT);
	CHECK_EQ((int)bareByName("AlpineSnow01", BARE_NONE), (int)BARE_SNOW);
	CHECK_EQ((int)bareByName("Whatever", BARE_DIRT), (int)BARE_DIRT);
	CHECK((int)stoneDensity(BARE_ROCKY) > (int)stoneDensity(BARE_DIRT));
	CHECK((int)stoneDensity(BARE_DIRT) > (int)stoneDensity(BARE_SNOW));
	CHECK_EQ((int)stoneDensity(BARE_PAVED), 0);
	CHECK_EQ((int)stoneDensity(BARE_NONE), 0);
	CHECK((int)twigDensity(255, BARE_NONE) > 0);
	CHECK((int)twigDensity(0, BARE_DIRT) > 0);
	CHECK_EQ((int)twigDensity(0, BARE_DESERT), 0);
	CHECK_EQ((int)rockOf(BARE_DESERT, "TDesert1"), (int)ROCK_SANDSTONE);
	CHECK_EQ((int)rockOf(BARE_ROCKY, "SwissCliff"), (int)ROCK_GRANITE);
	CHECK_EQ((int)rockOf(BARE_ROCKY, "LavaRock"), (int)ROCK_BASALT);
	CHECK_EQ(rockCell(ROCK_GRANITE), (int)ATLAS_GRANITE);
	CHECK_EQ(rockCell(ROCK_SANDSTONE), (int)ATLAS_SANDSTONE);
	CHECK_EQ(rockCell(ROCK_BASALT), (int)ATLAS_BASALT);
}

TEST(foliage_rock_mesh_is_closed_and_low)
{
	const int (*faces)[3] = rockFaces();
	// every edge of a closed mesh is shared by exactly two faces: 30 of them
	int edges[ROCK_CORNERS][ROCK_CORNERS] = { { 0 } };
	for (int f = 0; f < ROCK_FACES; ++f)
		for (int e = 0; e < 3; ++e)
		{
			const int a = faces[f][e], b = faces[f][(e + 1) % 3];
			CHECK(a >= 0 && a < ROCK_CORNERS && b >= 0 && b < ROCK_CORNERS && a != b);
			++edges[a < b ? a : b][a < b ? b : a];
		}
	int distinct = 0;
	for (int a = 0; a < ROCK_CORNERS; ++a)
		for (int b = a + 1; b < ROCK_CORNERS; ++b)
			if (edges[a][b])
			{
				CHECK_EQ(edges[a][b], 2);
				++distinct;
			}
	CHECK_EQ(distinct, 30);
	for (int v = 0; v < ROCK_VARIANTS; ++v)
	{
		float c[ROCK_CORNERS][3], d[ROCK_CORNERS][3];
		rockCorners(v, c);
		rockCorners(v, d);
		bool differs = false;
		float other[ROCK_CORNERS][3];
		rockCorners(v + 1, other);
		for (int i = 0; i < ROCK_CORNERS; ++i)
		{
			const float r = sqrtf(c[i][0] * c[i][0] + c[i][1] * c[i][1]);
			CHECK(r <= ROCK_MAX_RADIUS + 0.001f);
			CHECK(c[i][2] <= (ROCK_MAX_RADIUS + 0.001f) * ROCK_FLATTEN
				&& c[i][2] >= -(ROCK_MAX_RADIUS + 0.001f) * ROCK_FLATTEN * ROCK_BELOW);
			CHECK(c[i][0] == d[i][0] && c[i][1] == d[i][1] && c[i][2] == d[i][2]);	// a fixed hash
			differs = differs || other[i][0] != c[i][0];
		}
		CHECK(differs);
		// a detailed rock's edge splits at one point whichever face asks: no crack
		for (int f = 0; f < ROCK_FACES; ++f)
			for (int e = 0; e < 3; ++e)
			{
				const int a = faces[f][e], b = faces[f][(e + 1) % 3];
				float ab[3], ba[3];
				rockMidpoint(v, a, b, c, ab);
				rockMidpoint(v, b, a, c, ba);
				CHECK(ab[0] == ba[0] && ab[1] == ba[1] && ab[2] == ba[2]);
				const float m = sqrtf(ab[0] * ab[0] + ab[1] * ab[1] + ab[2] * ab[2]);
				CHECK(m < ROCK_MAX_RADIUS * 1.3f);
			}
	}
}

TEST(foliage_a_grove_has_a_thicket_and_a_lone_tree_a_fern)
{
	const unsigned char field[CELL_BYTES] = { 255, 0, 90, 0, 0 };
	const unsigned char lone[CELL_BYTES] = { 255, 0, 90, 0, 1 };
	const unsigned char edgeOfGrove[CELL_BYTES] = { 255, 0, 90, 0, 2 };
	const unsigned char grove[CELL_BYTES] = { 255, 0, 90, 0, 5 };
	const unsigned char cliffUnderTrees[CELL_BYTES] = { 0, 0, 0, CF_CLIFF, 5 };
	const Piece floor[] = { PIECE_LITTER, PIECE_FERN, PIECE_SAPLING, PIECE_LOG, PIECE_STUMP };
	for (size_t i = 0; i < sizeof(floor) / sizeof(floor[0]); ++i)
	{
		CHECK_EQ(countPiece(floor[i], field), 0);					// no trees, no floor
		CHECK_EQ(countPiece(floor[i], cliffUnderTrees), 0);	// and nothing on a cliff
	}
	CHECK_EQ(countPiece(PIECE_LITTER, lone), 0);				// no rust spot round a lone trunk
	CHECK_EQ(countPiece(PIECE_LITTER, edgeOfGrove), 0);	// litter only under a real canopy
	CHECK(countPiece(PIECE_FERN, lone) > 0);
	CHECK_EQ(countPiece(PIECE_LOG, lone), 0);
	CHECK(countPiece(PIECE_LITTER, grove) > 3000);
	CHECK(countPiece(PIECE_FERN, grove) > countPiece(PIECE_FERN, lone));
	CHECK(countPiece(PIECE_SAPLING, edgeOfGrove) > countPiece(PIECE_SAPLING, grove));	// saplings at the edge
	CHECK(countPiece(PIECE_LOG, grove) > 0);
	CHECK(countPiece(PIECE_SHRUB, grove) > countPiece(PIECE_SHRUB, field));
	CHECK((int)groveGrass(grove) < (int)groveGrass(edgeOfGrove));	// the tall grass thins under the canopy
	CHECK_EQ((int)groveGrass(field), 255);
	CHECK(pieceThins(PIECE_LITTER) && pieceThins(PIECE_FERN));
	CHECK(!pieceThins(PIECE_SAPLING) && !pieceThins(PIECE_LOG) && !pieceThins(PIECE_STUMP));
	// a grove's bushes are green even on dry ground
	const unsigned char dryGrove[CELL_BYTES] = { 60, 110, 60, 0, 4 };
	int dry = 0;
	for (int y = 0; y < 30; ++y)
		for (int x = 0; x < 30; ++x)
			dry += shrubCell(x, y, dryGrove) == ATLAS_SHRUB_SCRUB ? 1 : 0;
	CHECK(dry < 900 / 10);
	CHECK(coniferName("PTPine01") && coniferName("SnowFirTree") && !coniferName("PTOak01"));
	// open white snow keeps its floor bare: nothing of a grove's floor, and no bush, even at a cliff's
	// foot (Winter Wolf); patchy snow among grass under trees, as on Alpine, keeps its undergrowth
	const unsigned char whiteSnow[CELL_BYTES] = { 120, 25, 40, CF_CLIFF_FOOT | CF_EDGE, 5, BARE_SNOW, 225 };
	const unsigned char patchySnow[CELL_BYTES] = { 120, 25, 40, CF_CLIFF_FOOT | CF_EDGE, 5, BARE_SNOW, 150 };
	const Piece undergrowth[] = { PIECE_SHRUB, PIECE_LITTER, PIECE_FERN, PIECE_SAPLING, PIECE_LOG, PIECE_STUMP };
	for (size_t i = 0; i < sizeof(undergrowth) / sizeof(undergrowth[0]); ++i)
	{
		CHECK_EQ(countPiece(undergrowth[i], whiteSnow), 0);
		CHECK(countPiece(undergrowth[i], patchySnow) > 0);
	}
	CHECK(countPiece(PIECE_STONE, whiteSnow) > 0);
	CHECK(whiteGround(whiteSnow) && !whiteGround(patchySnow));
}

TEST(foliage_bush_dome_is_open_only_underneath)
{
	const int (*faces)[3] = bushFaces();
	int edges[BUSH_CORNERS][BUSH_CORNERS] = { { 0 } };
	for (int f = 0; f < BUSH_FACES; ++f)
		for (int e = 0; e < 3; ++e)
		{
			const int a = faces[f][e], b = faces[f][(e + 1) % 3];
			CHECK(a >= 0 && a < BUSH_CORNERS && b >= 0 && b < BUSH_CORNERS && a != b);
			++edges[a < b ? a : b][a < b ? b : a];
		}
	int rim = 0;
	for (int a = 0; a < BUSH_CORNERS; ++a)
		for (int b = a + 1; b < BUSH_CORNERS; ++b)
			if (edges[a][b] == 1)
			{
				CHECK(a < BUSH_RING && b < BUSH_RING);	// only the ring on the ground is open
				++rim;
			}
			else if (edges[a][b] != 0)
				CHECK_EQ(edges[a][b], 2);
	CHECK_EQ(rim, BUSH_RING);
	for (int v = 0; v < BUSH_VARIANTS; ++v)
	{
		float c[BUSH_CORNERS][3];
		bushCorners(v, c);
		for (int i = 0; i < BUSH_RING; ++i)
		{
			CHECK(c[i][2] == 0.0f);
			CHECK(c[BUSH_RING * 2 + i][2] > c[BUSH_RING + i][2]);	// rings climb toward the crown
			CHECK(c[BUSH_TOP][2] > c[BUSH_RING * 2 + i][2]);
		}
		for (int i = 0; i < BUSH_CORNERS; ++i)
			CHECK(sqrtf(c[i][0] * c[i][0] + c[i][1] * c[i][1]) <= 1.16f);
	}
}

TEST(foliage_big_trees_stand_only_where_no_unit_goes)
{
	// grass, stones, twigs, flags, canopy, kind
	const unsigned char desertOpen[CELL_BYTES] = { 0, 90, 0, ROCK_SANDSTONE, 0, BARE_DESERT };
	const unsigned char desertBorder[CELL_BYTES] = { 0, 90, 0, ROCK_SANDSTONE, 0, BARE_DESERT | KIND_BORDER };
	const unsigned char desertCliff[CELL_BYTES] = { 0, 0, 0, ROCK_SANDSTONE | CF_CLIFF, 0, BARE_ROCKY };
	const unsigned char snowHug[CELL_BYTES] = { 0, 25, 0, ROCK_GRANITE, 0, BARE_SNOW | KIND_HUG };
	const unsigned char snowBorder[CELL_BYTES] = { 0, 25, 0, ROCK_GRANITE, 0, BARE_SNOW | KIND_BORDER };
	const unsigned char snowOpen[CELL_BYTES] = { 0, 25, 0, ROCK_GRANITE, 0, BARE_SNOW };
	const unsigned char grassBorder[CELL_BYTES] = { 255, 0, 90, 0, 0, BARE_NONE | KIND_BORDER };
	CHECK_EQ(countPiece(PIECE_DEAD_TREE, desertOpen), 0);
	CHECK_EQ(countPiece(PIECE_FIR, snowOpen), 0);
	CHECK(countPiece(PIECE_DEAD_TREE, desertBorder) > 0);
	CHECK(countPiece(PIECE_DEAD_TREE, desertCliff) > 0);	// sandstone rock counts as dry
	CHECK_EQ(countPiece(PIECE_FIR, snowHug), 0);	// the lane beside a cliff is open ground
	CHECK(countPiece(PIECE_FIR, snowBorder) > countPiece(PIECE_DEAD_TREE, desertBorder));	// clumps, not loose groups
	CHECK(pieceFar(PIECE_FIR) && pieceFar(PIECE_DEAD_TREE) && !pieceFar(PIECE_BOULDER));
	// every upright piece casts into the sun's map, no flat card does
	CHECK_EQ((int)pieceGroup(PIECE_FIR), (int)GROUP_FAR);
	CHECK_EQ((int)pieceGroup(PIECE_SHRUB), (int)GROUP_FIXED);
	CHECK_EQ((int)pieceGroup(PIECE_BOULDER), (int)GROUP_FIXED);
	CHECK_EQ((int)pieceGroup(PIECE_LOG), (int)GROUP_FIXED_FLAT);
	CHECK_EQ((int)pieceGroup(PIECE_TUFT), (int)GROUP_THIN);
	CHECK_EQ((int)pieceGroup(PIECE_FERN), (int)GROUP_THIN);
	CHECK_EQ((int)pieceGroup(PIECE_LITTER), (int)GROUP_THIN_FLAT);
	CHECK_EQ((int)pieceGroup(PIECE_PEBBLES), (int)GROUP_THIN_FLAT);
	CHECK_EQ((int)pieceGroup(PIECE_TWIGS), (int)GROUP_THIN_FLAT);
	CHECK_EQ(countPiece(PIECE_FIR, desertBorder), 0);
	CHECK_EQ(countPiece(PIECE_DEAD_TREE, snowHug), 0);
	CHECK_EQ(countPiece(PIECE_DEAD_TREE, grassBorder), 0);
	CHECK(!pieceThins(PIECE_DEAD_TREE) && !pieceThins(PIECE_FIR));
}

TEST(foliage_tree_meshes_are_whole_and_stand_on_their_root)
{
	for (int v = 0; v < DEAD_TREE_VARIANTS + FIR_VARIANTS; ++v)
	{
		TreeMesh m;
		const bool fir = v >= DEAD_TREE_VARIANTS;
		if (fir)
			firMesh(v - DEAD_TREE_VARIANTS, m);
		else
			deadTreeMesh(v, m);
		const int faces = (int)m.indices.size() / 3;
		CHECK_EQ((int)m.indices.size() % 3, 0);
		if (fir)
			CHECK(faces >= 32 && faces <= 36);
		else
			CHECK(faces >= 120 && faces <= 320);
		for (size_t i = 0; i < m.indices.size(); ++i)
			CHECK(m.indices[i] < m.vertices.size());
		float top = -1.0f, low = 1.0f;
		for (size_t i = 0; i < m.vertices.size(); ++i)
		{
			const TreeVertex &t = m.vertices[i];
			if (t.z > top) top = t.z;
			if (t.z < low) low = t.z;
			CHECK(t.u >= 0.0f && t.u <= 1.0f && t.v >= 0.0f && t.v <= 1.0f);
			CHECK(fir ? (t.cell == ATLAS_DEAD_BARK || t.cell == ATLAS_FIR_SPIRE
				|| t.cell == ATLAS_FIR_WHORL) : t.cell == ATLAS_DEAD_BARK);
			CHECK(fabsf(t.nx * t.nx + t.ny * t.ny + t.nz * t.nz - 1.0f) < 0.01f);
		}
		CHECK(top > 0.7f && top < 1.4f);
		CHECK(low > -0.15f && low < 0.0f);	// the root just under the ground
		if (!fir)
			continue;
		// the retail scheme: tent folded whorls from the bare trunk up, drooping more and reaching
		// less toward the top, each about the trunk; the tuft on top
		int whorls = 0, spires = 0;
		float lastZ = -1.0f, lastReach = 1.0f, firstZ = 0.0f, firstReach = 0.0f;
		size_t trunk = 0;
		while (trunk < m.vertices.size() && m.vertices[trunk].cell == ATLAS_DEAD_BARK)
			++trunk;
		CHECK_EQ((m.vertices.size() - trunk) % 4, (size_t)0);
		for (size_t q = trunk; q + 4 <= m.vertices.size(); q += 4)
		{
			const TreeVertex *c = &m.vertices[q];
			if (c[0].cell == ATLAS_FIR_SPIRE)
			{
				++spires;
				CHECK(c[0].z > c[3].z);
				continue;
			}
			CHECK_EQ(c[0].cell, (int)ATLAS_FIR_WHORL);
			const float cx = (c[0].x + c[2].x) * 0.5f, cy = (c[0].y + c[2].y) * 0.5f;
			const float reach = sqrtf((c[0].x - cx) * (c[0].x - cx) + (c[0].y - cy) * (c[0].y - cy));
			const float droop = atanf((c[0].z - c[1].z) / reach) * 57.29578f;
			CHECK(c[0].z == c[2].z && c[1].z == c[3].z);
			CHECK(droop > 19.5f && droop < 34.5f);
			CHECK(sqrtf(cx * cx + cy * cy) <= FIR_TIER_OFFSET + FIR_LEAN + 1e-4f);
			CHECK(c[0].z > lastZ && reach < lastReach);
			if (whorls == 0)
			{
				firstZ = c[1].z;	// the bare trunk ends where the lowest tier's corners hang
				firstReach = reach;
			}
			lastZ = c[0].z;
			lastReach = reach;
			++whorls;
		}
		CHECK(whorls >= FIR_TIERS_MIN && whorls <= FIR_TIERS_MAX);
		CHECK(firstZ >= FIR_BARE_LOW - 1e-4f && firstZ <= FIR_BARE_LOW + FIR_BARE_SPREAD + 1e-4f);
		CHECK_NEAR(firstReach, FIR_REACH_LOW, 1e-4f);
		CHECK_NEAR(lastReach, FIR_REACH_TOP, 1e-4f);
		CHECK_EQ(spires, 2);
	}
}

TEST(foliage_screen_draws_every_piece_written_and_the_sun_every_upright_one)
{
	// group sizes with empty groups in every position, as chunks have them
	const int layouts[][PIECE_GROUPS] = {
		{ 0, 5, 2, 40, 30 }, { 3, 0, 0, 12, 0 }, { 0, 0, 0, 0, 7 }, { 2, 9, 0, 0, 4 }, { 0, 6, 1, 0, 0 },
		{ 4, 0, 3, 8, 0 }, { 0, 0, 0, 0, 0 }, { 1, 1, 1, 1, 1 }
	};
	for (size_t l = 0; l < sizeof(layouts) / sizeof(layouts[0]); ++l)
	{
		const int *groups = layouts[l];
		const int total = groupStart(groups, PIECE_GROUPS);
		// the screen at full share: every piece once
		std::vector<int> drawn(total, 0);
		PieceRange screen[2];
		screenRanges(groups, false, groups[GROUP_THIN], groups[GROUP_THIN_FLAT], screen);
		for (int r = 0; r < 2; ++r)
			for (int i = screen[r].first; i < screen[r].first + screen[r].count; ++i)
			{
				CHECK(i >= 0 && i < total);
				if (i >= 0 && i < total)
					++drawn[i];
			}
		for (int i = 0; i < total; ++i)
			CHECK_EQ(drawn[i], 1);
		// the bushes and rocks (GROUP_FIXED) are drawn whatever the thinning
		screenRanges(groups, false, 0, 0, screen);
		CHECK(screen[0].count >= groupStart(groups, GROUP_THIN));
		// the sun: every upright piece the screen drew, no flat one
		PieceRange sun[2];
		casterRanges(groups, false, groups[GROUP_THIN], sun);
		std::vector<int> cast(total, 0);
		for (int r = 0; r < 2; ++r)
			for (int i = sun[r].first; i < sun[r].first + sun[r].count; ++i)
				if (i >= 0 && i < total)
					++cast[i];
		for (int g = 0; g < PIECE_GROUPS; ++g)
		{
			const bool flat = g == GROUP_FIXED_FLAT || g == GROUP_THIN_FLAT;
			for (int i = groupStart(groups, g); i < groupStart(groups, g + 1); ++i)
				CHECK_EQ(cast[i], flat ? 0 : 1);
		}
		// the far ring: the big trees alone, in both
		screenRanges(groups, true, 0, 0, screen);
		CHECK_EQ(screen[0].first, 0);
		CHECK_EQ(screen[0].count, groups[GROUP_FAR]);
		CHECK_EQ(screen[1].count, 0);
	}
}

TEST(foliage_clear_zeroes_every_byte_of_a_cell)
{
	std::vector<unsigned char> cells(10 * 10 * CELL_BYTES, 0);
	cells[(5 * 10 + 5) * CELL_BYTES + BYTE_FLAGS] = CF_CLIFF;		// a boulder cell with no grass
	cells[(5 * 10 + 6) * CELL_BYTES + BYTE_STONES] = 90;
	Grid g = { &cells[0], 10, 10, 10.0f, 0.0f, 0.0f, CELL_BYTES };
	CHECK_EQ(clearCapsule(g, 60.0f, 55.0f, 60.0f, 55.0f, 6.0f), 2);
	for (size_t i = 0; i < cells.size(); ++i)
		CHECK_EQ((int)cells[i], 0);
}
