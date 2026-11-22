// test_cloud_shadow_bake.cpp: the cloud shadow's larger texture (W3DDevice/GameClient/CloudShadowBake.h).
// The baked texture keeps the retail tile's mean darkness, no longer repeats a tile apart, has cloud
// masses and clear gaps several tiles wide, comes out the same bytes every time, and the DXT1 decode
// reads a block the way the format says.

#include "test_harness.h"

#include "W3DDevice/GameClient/CloudShadowBake.h"

#include <math.h>
#include <string.h>
#include <vector>

using namespace CloudShadowBake;

namespace {

const int SOURCE = 128;
const int SIDE = SOURCE * REPEAT;

// A cloud-like retail stand-in: smooth blobs over a tile that wraps, pale with grey patches.
std::vector<unsigned char> cloudTile()
{
	std::vector<unsigned char> tile((size_t)SOURCE * SOURCE * 4);
	const double turn = 6.283185307179586 / SOURCE;
	for (int y = 0; y < SOURCE; ++y)
		for (int x = 0; x < SOURCE; ++x)
		{
			const double v = sin(x * turn * 2 + 1.3) * cos(y * turn * 3) + 0.6 * sin((x + 2 * y) * turn * 5 + 0.4)
				+ 0.3 * cos((3 * x - y) * turn * 7);
			const int grey = (int)(215 + 22 * v);
			unsigned char *p = &tile[((size_t)y * SOURCE + x) * 4];
			p[0] = (unsigned char)(grey + 4 > 255 ? 255 : grey + 4);
			p[1] = (unsigned char)(grey > 255 ? 255 : grey);
			p[2] = (unsigned char)(grey - 5);
			p[3] = 255;
		}
	return tile;
}

void stats(const unsigned char *pixels, size_t count, int channel, double &mean, double &deviation)
{
	double sum = 0, squares = 0;
	for (size_t i = 0; i < count; ++i)
	{
		const double v = pixels[i * 4 + channel];
		sum += v;
		squares += v * v;
	}
	mean = sum / count;
	deviation = sqrt(squares / count - mean * mean);
}

// The correlation of the baked texture's green with itself moved by (sx, sy) texels.
double correlation(const std::vector<unsigned char> &baked, int sx, int sy)
{
	double mean, deviation;
	stats(&baked[0], (size_t)SIDE * SIDE, 1, mean, deviation);
	double sum = 0;
	for (int y = 0; y < SIDE; ++y)
		for (int x = 0; x < SIDE; ++x)
		{
			const double a = baked[((size_t)y * SIDE + x) * 4 + 1] - mean;
			const double b = baked[((size_t)((y + sy) % SIDE) * SIDE + (x + sx) % SIDE) * 4 + 1] - mean;
			sum += a * b;
		}
	return sum / ((double)SIDE * SIDE * deviation * deviation);
}

std::vector<unsigned char> bake(const std::vector<unsigned char> &source)
{
	std::vector<unsigned char> baked((size_t)SIDE * SIDE * 4);
	scatter(&source[0], SOURCE, SOURCE, &baked[0], SIDE, SIDE * 4);
	return baked;
}

}	// namespace

TEST(cloud_bake_keeps_the_retail_mean_and_contrast)
{
	const std::vector<unsigned char> source = cloudTile();
	const std::vector<unsigned char> baked = bake(source);
	for (int ch = 0; ch < 3; ++ch)
	{
		double sourceMean, sourceDeviation, bakedMean, bakedDeviation;
		stats(&source[0], (size_t)SOURCE * SOURCE, ch, sourceMean, sourceDeviation);
		stats(&baked[0], (size_t)SIDE * SIDE, ch, bakedMean, bakedDeviation);
		CHECK_NEAR(bakedMean, sourceMean, 1.5);
		// The cover adds contrast between tiles; inside a tile the copies keep EA's.
		CHECK(bakedDeviation / sourceDeviation > 0.95);
		CHECK(bakedDeviation / sourceDeviation < 1.5);
	}
}

TEST(cloud_bake_does_not_repeat_a_tile_apart)
{
	const std::vector<unsigned char> source = cloudTile();
	std::vector<unsigned char> tiled((size_t)SIDE * SIDE * 4);
	tile(&source[0], SOURCE, SOURCE, &tiled[0], SIDE, SIDE * 4);
	// EA's tiling is itself one tile over: the grid the eye picks out.
	CHECK_NEAR(correlation(tiled, SOURCE, 0), 1.0, 1e-9);

	const std::vector<unsigned char> baked = bake(source);
	CHECK(correlation(baked, SOURCE, 0) < 0.35);
	CHECK(correlation(baked, 0, SOURCE) < 0.35);
	CHECK(correlation(baked, SOURCE, SOURCE) < 0.35);
	// And it wraps at its own edge, as the sampler does.
	CHECK_NEAR(correlation(baked, SIDE, 0), 1.0, 1e-9);
}

// The mean shade (255 less green) of each tile-sized block, against the mean of them all: the
// darkest block's share and the lightest's.
void blockShade(const std::vector<unsigned char> &pixels, double &darkest, double &lightest)
{
	const int blocks = SIDE / SOURCE;
	std::vector<double> shade((size_t)blocks * blocks, 0.0);
	double total = 0;
	for (int y = 0; y < SIDE; ++y)
		for (int x = 0; x < SIDE; ++x)
		{
			const double s = 255 - pixels[((size_t)y * SIDE + x) * 4 + 1];
			shade[(size_t)(y / SOURCE) * blocks + x / SOURCE] += s;
			total += s;
		}
	const double mean = total / shade.size();
	darkest = 0;
	lightest = 1e30;
	for (size_t i = 0; i < shade.size(); ++i)
	{
		darkest = shade[i] / mean > darkest ? shade[i] / mean : darkest;
		lightest = shade[i] / mean < lightest ? shade[i] / mean : lightest;
	}
}

TEST(cloud_bake_has_masses_and_gaps_tiles_wide)
{
	// EA's tiling has none: every tile is as dark as the next.
	const std::vector<unsigned char> source = cloudTile();
	std::vector<unsigned char> tiled((size_t)SIDE * SIDE * 4);
	tile(&source[0], SOURCE, SOURCE, &tiled[0], SIDE, SIDE * 4);
	double darkest, lightest;
	blockShade(tiled, darkest, lightest);
	CHECK_NEAR(darkest, 1.0, 1e-9);
	CHECK_NEAR(lightest, 1.0, 1e-9);

	// The bake thins the cloud to near clear over whole tiles and keeps it full over others.
	blockShade(bake(source), darkest, lightest);
	// The retail tile reads 1.48 and 0.47 here; this stand-in, whose blobs are smoother, a little less.
	CHECK(darkest > 1.25);
	CHECK(lightest < 0.7);
}

TEST(cloud_bake_is_the_same_bytes_every_time)
{
	const std::vector<unsigned char> source = cloudTile();
	const std::vector<unsigned char> first = bake(source);
	const std::vector<unsigned char> second = bake(source);
	CHECK(first == second);
}

TEST(cloud_bake_decodes_a_dxt1_block)
{
	// One 4x4 block: colour 0 pure red, colour 1 pure blue, so four colour mode, and the rows hold
	// indices 0, 1, 2, 3 left to right.
	unsigned char file[128 + 8];
	memset(file, 0, sizeof(file));
	memcpy(file, "DDS ", 4);
	file[12] = 4;	// height
	file[16] = 4;	// width
	memcpy(file + 84, "DXT1", 4);
	unsigned char *block = file + 128;
	block[0] = 0x00; block[1] = 0xF8;	// 0xF800 red
	block[2] = 0x1F; block[3] = 0x00;	// 0x001F blue
	for (int y = 0; y < 4; ++y)
		block[4 + y] = 0xE4;	// 3 2 1 0 from the top bits: x0 = 0, x1 = 1, x2 = 2, x3 = 3

	int width = 0, height = 0;
	std::vector<unsigned char> pixels;
	CHECK(decodeDXT1(file, sizeof(file), width, height, pixels));
	CHECK_EQ(width, 4);
	CHECK_EQ(height, 4);
	const unsigned char expected[4][4] = {
		{ 0, 0, 255, 255 }, { 255, 0, 0, 255 }, { 85, 0, 170, 255 }, { 170, 0, 85, 255 } };
	for (int x = 0; x < 4; ++x)
		CHECK_MEM(&pixels[(size_t)(2 * 4 + x) * 4], expected[x], 4);

	file[87] = '5';
	CHECK(!decodeDXT1(file, sizeof(file), width, height, pixels));
}
