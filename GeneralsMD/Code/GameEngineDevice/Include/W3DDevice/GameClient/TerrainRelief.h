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

// TerrainRelief.h: the Ground Relief option's relief, made from the terrain atlas itself as a map
// loads.  Header only, so test_terrain_relief runs it without a device.
//
// No tile has authored relief and nothing derived from the retail art may ship, so the relief is
// made on the player's machine from the atlas TerrainTextureClass::update has just filled.  Each
// texture class is one square of the atlas whose four texel border repeats its far side, so the
// class tiles itself; the relief is worked over each square with its indices wrapped and written
// over the border the same way, which keeps it seamless where the class repeats on the ground.
//
// The height is the luminance with its slow part taken out: a two pass box blur 2 * BLUR_RADIUS + 1
// texels wide is subtracted, so a stain, a painted patch or a lit side of a dune wider than that is no
// dent (the reason the colour-guessed normal maps of 2026-10-06 were taken out).  What is left is
// smoothed over three texels, scaled by GAIN and squashed by a soft clamp, so flat sand stays nearly
// flat and only rock and grit stand up.  Out: RGBA8 per texel, each of red, green and blue a value
// from -1 to 1 stored as 128 plus 127 times it (signedByte): red and green the slope of the 0 to 1
// height per texel along the row and down the column over SLOPE_RANGE, blue the height from -1 to
// 1; alpha 255.  Everything outside a class is flat (128, 128, 128).  SLOPE_RANGE is
// TERRAIN_RELIEF_SLOPE in ffshader.h.

#pragma once

#include <math.h>
#include <string.h>
#include <vector>

namespace TerrainRelief
{

const int BORDER = 4;					// TerrainTextureClass::update's border round each class
const int BLUR_RADIUS = 8;
const float GAIN = 3.0f;
const float SLOPE_RANGE = 0.25f;

// One texture class's square in the atlas: its top left texel inside the border and its side.
struct Region
{
	int x;
	int y;
	int size;
};

// -1 to 1 as 1 to 255 about 128, so nought is a byte exactly and a sum's last bit cannot round it
// either way.
inline unsigned char signedByte(float value)
{
	const float scaled = (float)floor(128.0f + value * 127.0f + 0.5f);
	return (unsigned char)(scaled < 1.0f ? 1.0f : (scaled > 255.0f ? 255.0f : scaled));
}

inline int wrap(int at, int n)
{
	at %= n;
	return at < 0 ? at + n : at;
}

// One box pass of radius r along rows (step 1) or columns (step n) of an n by n square, wrapped.
inline void boxPass(const std::vector<float> &in, std::vector<float> &out, int n, int r, bool rows)
{
	const float scale = 1.0f / (float)(2 * r + 1);
	for (int line = 0; line < n; ++line) {
		float sum = 0.0f;
		for (int k = -r; k <= r; ++k)
			sum += rows ? in[line * n + wrap(k, n)] : in[wrap(k, n) * n + line];
		for (int i = 0; i < n; ++i) {
			if (rows) out[line * n + i] = sum * scale;
			else out[i * n + line] = sum * scale;
			const int leaving = wrap(i - r, n);
			const int entering = wrap(i + r + 1, n);
			sum += rows ? in[line * n + entering] - in[line * n + leaving]
				: in[entering * n + line] - in[leaving * n + line];
		}
	}
}

inline void blur(std::vector<float> &data, int n, int r)
{
	std::vector<float> scratch(data.size());
	boxPass(data, scratch, n, r, true);
	boxPass(scratch, data, n, r, false);
}

// The relief of one square: lum is its luminance, 0 to 1, row after row.  height comes back 0 to 1,
// slopeX and slopeY per texel.
inline void square(const std::vector<float> &lum, int n, std::vector<float> &height,
	std::vector<float> &slopeX, std::vector<float> &slopeY)
{
	std::vector<float> low(lum);
	blur(low, n, BLUR_RADIUS);
	blur(low, n, BLUR_RADIUS);
	std::vector<float> detail(lum.size());
	for (size_t i = 0; i < lum.size(); ++i)
		detail[i] = lum[i] - low[i];
	blur(detail, n, 1);
	height.resize(lum.size());
	for (size_t i = 0; i < lum.size(); ++i)
		height[i] = 0.5f + 0.5f * (float)tanh(GAIN * detail[i]);
	slopeX.resize(lum.size());
	slopeY.resize(lum.size());
	for (int y = 0; y < n; ++y)
		for (int x = 0; x < n; ++x) {
			slopeX[y * n + x] = 0.5f * (height[y * n + wrap(x + 1, n)] - height[y * n + wrap(x - 1, n)]);
			slopeY[y * n + x] = 0.5f * (height[wrap(y + 1, n) * n + x] - height[wrap(y - 1, n) * n + x]);
		}
}

// The atlas's relief.  atlas is the atlas's top level, pitch bytes a row, four bytes a texel
// (B, G, R, A) or two (A1R5G5B5); relief comes back width * height * 4 bytes, RGBA.
inline void build(const unsigned char *atlas, int width, int height, int pitch, bool fullColour,
	const std::vector<Region> &regions, std::vector<unsigned char> &relief)
{
	relief.assign((size_t)width * height * 4, 128);
	for (size_t i = 3; i < relief.size(); i += 4)
		relief[i] = 255;
	std::vector<float> lum, h, sx, sy;
	for (size_t r = 0; r < regions.size(); ++r) {
		const Region &region = regions[r];
		const int n = region.size;
		if (n <= 0 || region.x - BORDER < 0 || region.y - BORDER < 0
				|| region.x + n + BORDER > width || region.y + n + BORDER > height)
			continue;
		lum.resize((size_t)n * n);
		for (int y = 0; y < n; ++y) {
			const unsigned char *row = atlas + (size_t)(region.y + y) * pitch;
			for (int x = 0; x < n; ++x) {
				float red, green, blue;
				if (fullColour) {
					const unsigned char *texel = row + (size_t)(region.x + x) * 4;
					blue = texel[0] / 255.0f;
					green = texel[1] / 255.0f;
					red = texel[2] / 255.0f;
				} else {
					const unsigned short texel = (unsigned short)(row[(region.x + x) * 2] | (row[(region.x + x) * 2 + 1] << 8));
					red = ((texel >> 10) & 31) / 31.0f;
					green = ((texel >> 5) & 31) / 31.0f;
					blue = (texel & 31) / 31.0f;
				}
				lum[y * n + x] = 0.3f * red + 0.59f * green + 0.11f * blue;
			}
		}
		square(lum, n, h, sx, sy);
		for (int y = -BORDER; y < n + BORDER; ++y)
			for (int x = -BORDER; x < n + BORDER; ++x) {
				const int from = wrap(y, n) * n + wrap(x, n);
				unsigned char *out = &relief[((size_t)(region.y + y) * width + region.x + x) * 4];
				out[0] = signedByte(sx[from] / SLOPE_RANGE);
				out[1] = signedByte(sy[from] / SLOPE_RANGE);
				out[2] = signedByte(2.0f * h[from] - 1.0f);
			}
	}
}

}	// namespace TerrainRelief
