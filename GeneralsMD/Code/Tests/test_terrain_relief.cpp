// test_terrain_relief.cpp: the Ground Relief option's relief (W3DDevice/GameClient/TerrainRelief.h).
// Flat ground stays flat, a bright ridge stands up with its slopes facing away from it, a slow stain
// is no dent, and the border repeats the far side so a class tiles without a seam.

#include "test_harness.h"

#include "W3DDevice/GameClient/TerrainRelief.h"

#include <vector>

using namespace TerrainRelief;

namespace {

const int W = 144;
const int H = 136;
const int N = 128;			// one HD tile
const int X0 = 8;
const int Y0 = 4;

// A B, G, R, A atlas of one grey, with one class square at X0, Y0.
std::vector<unsigned char> greyAtlas(unsigned char grey)
{
	std::vector<unsigned char> atlas((size_t)W * H * 4, grey);
	return atlas;
}

void setTexel(std::vector<unsigned char> &atlas, int x, int y, unsigned char grey)
{
	unsigned char *texel = &atlas[((size_t)y * W + x) * 4];
	texel[0] = texel[1] = texel[2] = grey;
}

const unsigned char *at(const std::vector<unsigned char> &relief, int x, int y)
{
	return &relief[((size_t)y * W + x) * 4];
}

std::vector<unsigned char> run(const std::vector<unsigned char> &atlas)
{
	std::vector<Region> regions(1);
	regions[0].x = X0;
	regions[0].y = Y0;
	regions[0].size = N;
	std::vector<unsigned char> relief;
	build(atlas.data(), W, H, W * 4, true, regions, relief);
	return relief;
}

}	// namespace

TEST(terrain_relief_of_flat_ground_is_flat)
{
	const std::vector<unsigned char> relief = run(greyAtlas(120));
	for (int y = 0; y < H; ++y)
		for (int x = 0; x < W; ++x) {
			CHECK_EQ(at(relief, x, y)[0], 128);
			CHECK_EQ(at(relief, x, y)[1], 128);
			CHECK_EQ(at(relief, x, y)[2], 128);
			CHECK_EQ(at(relief, x, y)[3], 255);
		}
}

TEST(terrain_relief_raises_a_bright_ridge_and_slopes_away_from_it)
{
	std::vector<unsigned char> atlas = greyAtlas(100);
	for (int y = 0; y < N; ++y)
		setTexel(atlas, X0 + 16, Y0 + y, 200);
	const std::vector<unsigned char> relief = run(atlas);
	const int y = Y0 + 10;
	CHECK(at(relief, X0 + 16, y)[2] > 160);		// the ridge stands up
	CHECK(at(relief, X0 + 15, y)[0] > 140);		// rising toward it from the left
	CHECK(at(relief, X0 + 17, y)[0] < 116);		// falling away to the right
	CHECK(at(relief, X0 + 16, y)[1] == 128);	// nothing along it
	CHECK(at(relief, X0 + 4, y)[2] < 128);		// the blur's dip round it, never a rise
}

TEST(terrain_relief_takes_a_slow_stain_out)
{
	// one smooth wave across the tile, dark half and light half, a bright stain's shape: no texel's
	// height moves more than a seventh of the way the ridge above stands up
	std::vector<unsigned char> atlas = greyAtlas(100);
	for (int y = 0; y < N; ++y)
		for (int x = 0; x < N; ++x) {
			const float wave = (float)sin(6.2831853 * x / N);
			setTexel(atlas, X0 + x, Y0 + y, (unsigned char)(100 + 40 * wave));
		}
	const std::vector<unsigned char> relief = run(atlas);
	for (int x = 0; x < N; ++x) {
		const int height = at(relief, X0 + x, Y0 + 5)[2];
		CHECK(height > 110 && height < 146);
	}
}

TEST(terrain_relief_border_repeats_the_far_side)
{
	std::vector<unsigned char> atlas = greyAtlas(90);
	for (int y = 0; y < N; ++y)
		for (int x = 0; x < N; ++x)
			setTexel(atlas, X0 + x, Y0 + y, (unsigned char)(90 + ((x * 7 + y * 13) % 11) * 9));
	const std::vector<unsigned char> relief = run(atlas);
	for (int k = 1; k <= BORDER; ++k)
		for (int c = 0; c < 4; ++c) {
			CHECK_EQ(at(relief, X0 - k, Y0 + 7)[c], at(relief, X0 + N - k, Y0 + 7)[c]);
			CHECK_EQ(at(relief, X0 + N - 1 + k, Y0 + 7)[c], at(relief, X0 + k - 1, Y0 + 7)[c]);
			CHECK_EQ(at(relief, X0 + 9, Y0 - k)[c], at(relief, X0 + 9, Y0 + N - k)[c]);
		}
	// and outside the class's border nothing is touched
	CHECK_EQ(at(relief, X0 - BORDER - 1, Y0 + 7)[2], 128);
}
