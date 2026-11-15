// test_terrain_subdivide.cpp: the subdivided ground's heights (W3DDevice/GameClient/TerrainSubdivide.h).
// The drawn ground passes through every sample, stays near the old two triangle plane that logic
// stands units on, follows a smooth hill between the samples, and cuts its small quads the way the
// tiles have always cut a cell.

#include "test_harness.h"

#include "W3DDevice/GameClient/TerrainSubdivide.h"

#include <math.h>
#include <vector>

using namespace TerrainSubdivide;

namespace {

// A repeatable map of byte heights: rolling ground with a cliff band and noise.
std::vector<unsigned char> rollingMap(int width, int height, unsigned seed)
{
	std::vector<unsigned char> data((size_t)width * height);
	unsigned state = seed;
	for (int y = 0; y < height; ++y)
		for (int x = 0; x < width; ++x)
		{
			state = state * 1664525u + 1013904223u;
			const float hill = 60.0f + 25.0f * (float)sin(x * 0.21) * (float)cos(y * 0.17);
			const float cliff = (x > width / 2 && x < width / 2 + 2) ? 0.0f : (x >= width / 2 + 2 ? 70.0f : 0.0f);
			const float noise = (float)((state >> 24) % 5);
			int value = (int)(hill + cliff + noise);
			if (value > 255) value = 255;
			data[(size_t)y * width + x] = (unsigned char)value;
		}
	return data;
}

// The plane logic stands a unit on at half cell (x2, y2): TerrainHeightSampling's fixed diagonal.
float logicPlane(const Heights &h, int x2, int y2)
{
	const int x = x2 >> 1, y = y2 >> 1;
	const float c[4] = { (float)h.at(x, y), (float)h.at(x + 1, y), (float)h.at(x + 1, y + 1), (float)h.at(x, y + 1) };
	return plane(c, false, x2 & 1, y2 & 1);
}

}  // namespace

TEST(the_drawn_ground_passes_through_every_sample)
{
	const int w = 40, hgt = 33;
	std::vector<unsigned char> data = rollingMap(w, hgt, 7);
	const Heights h = { &data[0], w, hgt };
	for (int y = 0; y < hgt; ++y)
		for (int x = 0; x < w; ++x)
		{
			CHECK_EQ(height(h, 2 * x, 2 * y, false), (float)data[(size_t)y * w + x]);
			CHECK_EQ(height(h, 2 * x, 2 * y, true), (float)data[(size_t)y * w + x]);
		}
}

TEST(between_the_samples_it_stays_within_a_unit_of_the_plane_and_inside_the_corners)
{
	const int w = 48, hgt = 48;
	int moved = 0, points = 0;
	float worst = 0.0f;
	for (unsigned seed = 1; seed <= 6; ++seed)
	{
		std::vector<unsigned char> data = rollingMap(w, hgt, seed);
		const Heights h = { &data[0], w, hgt };
		for (int y2 = 0; y2 <= 2 * (hgt - 1); ++y2)
			for (int x2 = 0; x2 <= 2 * (w - 1); ++x2)
			{
				if (!((x2 | y2) & 1))
					continue;
				const int x = (x2 >> 1) < w - 1 ? (x2 >> 1) : w - 2;
				const int y = (y2 >> 1) < hgt - 1 ? (y2 >> 1) : hgt - 2;
				const float drawn = height(h, x2, y2, false);
				const float off = (float)fabs(drawn - logicPlane(h, x2, y2));
				if (off > worst) worst = off;
				if (off > 0.0f) ++moved;
				++points;
				CHECK(off <= CAP + 1e-4f);
				const Limit limit = cellLimit(h, x, y);
				CHECK(drawn >= limit.lo - 1e-4f);
				CHECK(drawn <= limit.hi + 1e-4f);
			}
	}
	// the patch did move the ground
	CHECK(moved > points / 4);
	CHECK(worst > 0.25f);
}

TEST(the_cap_holds_a_sharp_bend_to_a_unit)
{
	// along x the samples run 0, 0, 6, 60: the patch wants 3.375 steps under the chord between the
	// 0 and the 6, and is held to CAP under it (a 6 step cell is gentle enough to take the whole cap)
	const int w = 6, hgt = 4;
	std::vector<unsigned char> data((size_t)w * hgt);
	const unsigned char row[6] = { 0, 0, 6, 60, 60, 60 };
	for (int y = 0; y < hgt; ++y)
		for (int x = 0; x < w; ++x)
			data[(size_t)y * w + x] = row[x];
	const Heights h = { &data[0], w, hgt };
	CHECK_NEAR(halfway(0, 0, 6, 60), 3.0f - 3.375f, 1e-5f);
	CHECK_NEAR(height(h, 3, 2, false), 3.0f - CAP, 1e-5f);
	// the cliff the pathfinder draws: 9.8 world units over a cell, and the plane from there on
	CHECK_NEAR(STEEP_END * 0.625f, 9.8f, 1e-4f);
	data[(size_t)1 * w + 2] = data[(size_t)2 * w + 2] = 16;
	data[(size_t)1 * w + 1] = data[(size_t)2 * w + 1] = 0;
	CHECK_EQ(cellLimit(h, 1, 1).cap, 0.0f);
}

TEST(a_cliff_keeps_its_plane)
{
	// a 100 step wall across one cell: the cells it crosses go back to EA's faces exactly
	const int w = 8, hgt = 8;
	std::vector<unsigned char> data((size_t)w * hgt);
	for (int y = 0; y < hgt; ++y)
		for (int x = 0; x < w; ++x)
			data[(size_t)y * w + x] = (unsigned char)(x < 4 ? 10 + y : 110 + y);
	const Heights h = { &data[0], w, hgt };
	const float c[4] = { (float)h.at(3, 2), (float)h.at(4, 2), (float)h.at(4, 3), (float)h.at(3, 3) };
	for (int b = 0; b <= 2; ++b)
		for (int a = 0; a <= 2; ++a)
			CHECK_EQ(height(h, 6 + a, 4 + b, true), plane(c, true, a, b));
	CHECK_EQ(cellLimit(h, 3, 2).cap, 0.0f);
}

TEST(a_smooth_hill_is_drawn_round_between_its_samples)
{
	// Catmull-Rom is exact on a parabola: a gentle bowl's midpoints land on the bowl, not on the
	// chord the two triangles drew through them
	const int w = 16, hgt = 16;
	std::vector<unsigned char> data((size_t)w * hgt);
	for (int y = 0; y < hgt; ++y)
		for (int x = 0; x < w; ++x)
			data[(size_t)y * w + x] = (unsigned char)(40 + (x - 8) * (x - 8));
	const Heights h = { &data[0], w, hgt };
	for (int x = 5; x < 10; ++x)
	{
		const float bowl = 40.0f + (x + 0.5f - 8.0f) * (x + 0.5f - 8.0f);
		const float drawn = height(h, 2 * x + 1, 12, false);
		const float chord = 0.5f * (float)(h.at(x, 6) + h.at(x + 1, 6));
		CHECK_NEAR(drawn, bowl, 1e-4f);
		CHECK(drawn < chord);	// the chord sits above a bowl's bottom
	}
}

TEST(an_edge_midpoint_does_not_care_which_cell_asks_or_how_it_is_cut)
{
	const int w = 24, hgt = 24;
	std::vector<unsigned char> data = rollingMap(w, hgt, 3);
	const Heights h = { &data[0], w, hgt };
	for (int y = 1; y < hgt - 1; ++y)
		for (int x = 1; x < w - 1; ++x)
		{
			CHECK_EQ(height(h, 2 * x + 1, 2 * y, false), height(h, 2 * x + 1, 2 * y, true));
			CHECK_EQ(height(h, 2 * x, 2 * y + 1, false), height(h, 2 * x, 2 * y + 1, true));
		}
}

namespace {
struct CheckerFlip
{
	bool operator()(int x, int y) const { return ((x * 7 + y * 3) % 5) < 2; }
};
}

TEST(the_table_the_ground_reads_is_height_point_for_point)
{
	const int w = 37, hgt = 29;
	std::vector<unsigned char> data = rollingMap(w, hgt, 5);
	const Heights h = { &data[0], w, hgt };
	const int stride = 2 * w - 1, rows = 2 * hgt - 1;
	const CheckerFlip flip;
	// the whole map, then a patch redone in the middle over a poisoned copy
	std::vector<float> table((size_t)stride * rows, -1.0f);
	fill(h, 0, 0, w, hgt, flip, 0.625f, &table[0], stride);
	for (int y2 = 0; y2 < rows; ++y2)
		for (int x2 = 0; x2 < stride; ++x2)
			CHECK_EQ(table[(size_t)y2 * stride + x2], 0.625f * height(h, x2, y2, flip(x2 >> 1, y2 >> 1)));
	std::vector<float> patch((size_t)stride * rows, -1.0f);
	fill(h, 10, 8, 14, 11, flip, 0.625f, &patch[0], stride);
	int filled = 0;
	for (int y2 = 0; y2 < rows; ++y2)
		for (int x2 = 0; x2 < stride; ++x2)
		{
			const float got = patch[(size_t)y2 * stride + x2];
			const bool inside = x2 >= 20 && x2 <= 30 && y2 >= 16 && y2 <= 24;
			CHECK_EQ(got != -1.0f, inside);
			if (inside)
			{
				CHECK_EQ(got, table[(size_t)y2 * stride + x2]);
				++filled;
			}
		}
	CHECK_EQ(filled, 11 * 9);
}

TEST(the_slope_at_a_sample_is_the_central_difference_the_light_used)
{
	const int w = 20, hgt = 20;
	std::vector<unsigned char> data = rollingMap(w, hgt, 11);
	const Heights h = { &data[0], w, hgt };
	for (int y = 1; y < hgt - 1; ++y)
		for (int x = 1; x < w - 1; ++x)
		{
			float dx, dy;
			slope(h, 2 * x, 2 * y, dx, dy);
			CHECK_NEAR(dx, 0.5f * (float)(h.at(x + 1, y) - h.at(x - 1, y)), 1e-4f);
			CHECK_NEAR(dy, 0.5f * (float)(h.at(x, y + 1) - h.at(x, y - 1)), 1e-4f);
		}
}

TEST(the_plane_blend_is_the_old_two_triangles)
{
	const float c[4] = { 0.0f, 10.0f, 30.0f, 4.0f };
	for (int f = 0; f < 2; ++f)
	{
		CHECK_EQ(plane(c, f != 0, 0, 0), c[0]);
		CHECK_EQ(plane(c, f != 0, 2, 0), c[1]);
		CHECK_EQ(plane(c, f != 0, 2, 2), c[2]);
		CHECK_EQ(plane(c, f != 0, 0, 2), c[3]);
		// an edge's midpoint is the edge's own average whichever way the cell is cut
		CHECK_EQ(plane(c, f != 0, 1, 0), 5.0f);
		CHECK_EQ(plane(c, f != 0, 2, 1), 20.0f);
		CHECK_EQ(plane(c, f != 0, 1, 2), 17.0f);
		CHECK_EQ(plane(c, f != 0, 0, 1), 2.0f);
	}
	// the middle lies on the diagonal: c[0]-c[2], or c[1]-c[3] flipped
	CHECK_EQ(plane(c, false, 1, 1), 15.0f);
	CHECK_EQ(plane(c, true, 1, 1), 7.0f);
}

TEST(a_cell_is_four_quads_cut_along_its_diagonal_and_wound_as_before)
{
	for (int f = 0; f < 2; ++f)
	{
		const bool flip = f != 0;
		unsigned short ib[24];
		CHECK(cell(ib, 100, flip) == ib + 24);
		int diagonalEdges = 0;
		for (int t = 0; t < 8; ++t)
		{
			int px[3], py[3];
			for (int k = 0; k < 3; ++k)
			{
				const int v = ib[t * 3 + k] - 100;
				CHECK(v >= 0 && v < 9);
				px[k] = v % 3;
				py[k] = v / 3;
			}
			// counter-clockwise from above, as EA's (0,2,3) and (0,1,2) are
			CHECK((px[1] - px[0]) * (py[2] - py[0]) - (py[1] - py[0]) * (px[2] - px[0]) > 0);
			for (int k = 0; k < 3; ++k)
			{
				const int a = k, b = (k + 1) % 3;
				const int dx = px[b] - px[a], dy = py[b] - py[a];
				// an edge along the cell's diagonal, (0,0)-(2,2) or (2,0)-(0,2) when flipped
				if (!flip && dx == dy && dx != 0 && px[a] == py[a] && px[b] == py[b]) ++diagonalEdges;
				if (flip && dx == -dy && dx != 0 && px[a] + py[a] == 2 && px[b] + py[b] == 2) ++diagonalEdges;
			}
		}
		// the cell's diagonal is two small edges, each shared by two triangles
		CHECK_EQ(diagonalEdges, 4);
	}
}
