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
