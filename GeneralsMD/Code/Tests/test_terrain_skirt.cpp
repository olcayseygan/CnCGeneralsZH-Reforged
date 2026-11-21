// test_terrain_skirt.cpp: the ground past the heightmap's edge (W3DDevice/GameClient/TerrainSkirt.h).
// It meets the edge at the edge's own height, keeps its hills low and the same on every run, goes
// black by the rim, and its ring walks every edge sample once.

#include "test_harness.h"

#include "W3DDevice/GameClient/TerrainSkirt.h"

#include <vector>

using namespace TerrainSkirt;

TEST(the_skirt_meets_the_edge_at_the_edge_height)
{
	for (int i = 0; i < 50; ++i)
	{
		const float edge = 10.0f + 3.0f * i;
		CHECK_NEAR(height(edge, 80.0f, 40.0f, 0.0f, 37.0f * i, -11.0f * i), edge, 1e-4f);
	}
}

TEST(far_out_it_settles_on_the_edge_mean_plus_low_hills)
{
	for (int i = 0; i < 200; ++i)
	{
		const float x = -500.0f - 13.7f * i, y = 2000.0f + 7.3f * i;
		const float z = height(150.0f, 120.0f, 50.0f, REACH, x, y);
		CHECK(z >= 50.0f - 1e-3f);
		CHECK(z <= 50.0f + HILL_HEIGHT + 1e-3f);
		CHECK_EQ(z, height(150.0f, 120.0f, 50.0f, REACH, x, y));
	}
}

TEST(past_a_sea_edge_it_stays_under_the_water)
{
	const float waterZ = 20.0f;
	for (int i = 0; i <= 160; ++i)
	{
		const float outside = 10.0f * i;
		// a blurred edge up on the beach still keeps the floor under the water...
		const float z = seaHeight(12.0f, 35.0f, waterZ, outside);
		CHECK(z <= waterZ - SEA_MARGIN + 1e-4f || outside == 0.0f);
		// ...and a deep one stays deep, with no hills and no climb to any mean
		CHECK(seaHeight(3.0f, 2.0f, waterZ, outside) <= 3.0f + 1e-4f);
	}
	CHECK_NEAR(seaHeight(12.0f, 35.0f, waterZ, 0.0f), 12.0f, 1e-4f);
	CHECK_NEAR(seaHeight(12.0f, 35.0f, waterZ, REACH), waterZ - SEA_MARGIN, 1e-4f);
}

TEST(a_coast_mixes_land_and_sea_by_the_sea_share)
{
	const float waterZ = 20.0f;
	CHECK_EQ(coastHeight(80.0f, 10.0f, 0.0f, waterZ), 80.0f);
	CHECK_NEAR(coastHeight(80.0f, 10.0f, 0.5f, waterZ), 45.0f, 1e-4f);
	// no sea strip over it, so lowered land stops just over the water instead of a dry trench...
	CHECK_NEAR(coastHeight(80.0f, 10.0f, 1.0f, waterZ), waterZ + SHORE_RISE, 1e-4f);
	CHECK_NEAR(coastHeight(80.0f, 10.0f, 0.9f, waterZ), waterZ + SHORE_RISE, 1e-4f);
	// ...and land already lower than that is left where it is
	CHECK_EQ(coastHeight(15.0f, 10.0f, 0.5f, waterZ), 15.0f);
	// the sea flag blurred along the ring turns over a coast, never in one step
	std::vector<float> sea(60, 0.0f);
	for (int i = 30; i < 60; ++i)
		sea[i] = 1.0f;
	std::vector<float> share = blurRing(sea);
	for (int i = 1; i < 60; ++i)
		CHECK(fabsf(share[i] - share[i - 1]) <= 1.0f / (2 * PROFILE_RADIUS + 1) + 1e-5f);
}

TEST(the_hills_have_no_steps)
{
	// one cell apart, the noise moves a little: a lattice seam would jump
	for (int i = 0; i < 2000; ++i)
	{
		const float x = -3000.0f + 3.1f * i, y = 1234.0f - 1.7f * i;
		const float step = fabsf(hills(x + CELL, y) - hills(x, y));
		CHECK(step < 0.1f);
	}
}

TEST(a_tile_past_the_edge_takes_its_texture_from_its_patch)
{
	const int w = 129, h = 97;
	int x = 0, y = 0;
	// on the map, and right beside its edge, a tile is the map's own
	CHECK_EQ(patchCell(10, 20, w, h, &x, &y), 0u);
	CHECK_EQ(x, 20);
	CHECK_EQ(y, 40);
	CHECK_EQ(patchCell(30, -1, w, h, &x, &y), 0u);
	CHECK_EQ(x, 60);
	CHECK_EQ(y, 0);
	// from the third tile out (the edge carried on no further) straight out from the bottom edge the
	// cell it reads changes from patch to patch, and a patch reads one cell, along the edge and never
	// deeper in than PATCH_SHIFT
	unsigned lastPatch = 0;
	int lastX = -1, lastY = -1, patches = 0, moved = 0;
	for (int s = -3; s >= -80; --s)
	{
		const unsigned patch = patchCell(30, s, w, h, &x, &y);
		CHECK(patch != 0u);
		CHECK(x >= 0 && x <= w - 2);
		CHECK(y >= 0 && y <= PATCH_SHIFT);
		if (patch == lastPatch)
		{
			CHECK_EQ(x, lastX);
			CHECK_EQ(y, lastY);
		}
		else
		{
			patches++;
			if (x != lastX)
				moved++;
		}
		lastPatch = patch;
		lastX = x;
		lastY = y;
	}
	CHECK(patches >= 4);
	CHECK(moved >= 3);
}

TEST(a_patch_keeps_its_class_whole_and_moves_the_block)
{
	const int width = 4;
	int a = -1, d = -1;
	// patch 0 is the map's own order
	scatterTile(13, -6, width, 0u, &a, &d);
	CHECK_EQ(a, 1);
	CHECK_EQ(d, 2);
	// inside one patch, neighbours are the class's neighbours
	int shifted = 0;
	for (unsigned patch = 1; patch < 40; ++patch)
	{
		int a1 = -1, d1 = -1, a2 = -1, d2 = -1;
		scatterTile(20, -30, width, patch * 2654435761u, &a1, &d1);
		scatterTile(21, -30, width, patch * 2654435761u, &a2, &d2);
		CHECK_EQ(a2, (a1 + 1) % width);
		CHECK_EQ(d2, d1);
		if (a1 != 0 || d1 != ((-30 % width) + width) % width)
			shifted++;
	}
	CHECK(shifted > 20);
}

TEST(the_baked_shade_is_the_maps_and_averages_one)
{
	// the same hash as the shader's terrain_hash: the cell (0, 0) hashes to nought, (1, 0) does not
	CHECK_EQ(variationHash(0, 0), 0.0f);
	CHECK(variationHash(1, 0) > 0.0f && variationHash(1, 0) < 1.0f);
	double sum = 0.0;
	const int samples = 4000;
	for (int i = 0; i < samples; ++i)
	{
		float rgb[3];
		variation(-2000.0f + 37.3f * i, 900.0f - 23.9f * i, rgb);
		for (int c = 0; c < 3; ++c)
			CHECK(rgb[c] > 0.6f && rgb[c] < 1.4f);
		sum += rgb[1];
	}
	CHECK_NEAR(sum / samples, 1.0, 0.05);
}

TEST(the_light_is_whole_near_the_map_and_gone_at_the_rim)
{
	CHECK_EQ(fade(0.0f), 1.0f);
	CHECK_EQ(fade(FADE_START * REACH), 1.0f);
	CHECK_EQ(fade(REACH), 0.0f);
	float last = 1.0f;
	for (int i = 0; i <= 100; ++i)
	{
		const float f = fade(REACH * i / 100.0f);
		CHECK(f <= last + 1e-6f);
		last = f;
	}
}

TEST(the_ring_takes_every_edge_sample_once)
{
	const int w = 9, h = 6;
	std::vector<int> seen(ringLength(w, h), 0);
	for (int y = 0; y < h; ++y)
		for (int x = 0; x < w; ++x)
		{
			const int i = ringIndex(x, y, w, h);
			const bool edge = x == 0 || y == 0 || x == w - 1 || y == h - 1;
			CHECK_EQ(i >= 0, edge);
			if (i >= 0 && i < (int)seen.size())
				seen[i]++;
		}
	for (size_t i = 0; i < seen.size(); ++i)
		CHECK_EQ(seen[i], 1);
	// and neighbours on the ring are neighbours on the map, round the corners too
	CHECK_EQ(ringIndex(w - 1, 1, w, h), ringIndex(w - 1, 0, w, h) + 1);
	CHECK_EQ(ringIndex(w - 2, h - 1, w, h), ringIndex(w - 1, h - 1, w, h) + 1);
	CHECK_EQ(ringIndex(0, h - 2, w, h), ringIndex(0, h - 1, w, h) + 1);
}

TEST(the_blur_keeps_a_flat_edge_flat_and_wraps)
{
	std::vector<float> ring(40, 30.0f);
	std::vector<float> out = blurRing(ring);
	for (size_t i = 0; i < out.size(); ++i)
		CHECK_NEAR(out[i], 30.0f, 1e-4f);
	ring[0] = 30.0f + (2 * PROFILE_RADIUS + 1);
	out = blurRing(ring);
	CHECK_NEAR(out[39], 31.0f, 1e-4f);
	CHECK_NEAR(out[PROFILE_RADIUS], 31.0f, 1e-4f);
	CHECK_NEAR(out[PROFILE_RADIUS + 1], 30.0f, 1e-4f);
}
