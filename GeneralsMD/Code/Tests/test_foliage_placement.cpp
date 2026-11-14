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
				CHECK(a.atlasCell >= 0 && a.atlasCell < ATLAS_COLUMNS * ATLAS_ROWS);
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
		Grid g = { &cells[0], 40, 40, 10.0f, -30.0f, -30.0f };
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
	Grid g = { &cells[0], 30, 30, 10.0f, 0.0f, 0.0f };
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
