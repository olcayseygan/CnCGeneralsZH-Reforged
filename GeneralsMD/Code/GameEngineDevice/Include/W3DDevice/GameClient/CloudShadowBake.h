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

// CloudShadowBake.h: the cloud shadow's texture, made larger than EA's so its repeat cannot be seen.
//
// EA's TSCloudMed is 128 texels laid over 315 world units, and pulled out to the camera's ceiling
// the ground shows it as a grid. scatter() builds a texture REPEAT times wider out of the same
// texels: copies of the retail tile at a random whole texel offset, mirrored or not on either axis,
// one copy round each point of a jittered grid one tile apart, each running into its
// neighbours over a seam. Away from a seam a copy is EA's texels, so up close the clouds have the
// shapes the game always had. A seam is summed the variance-keeping way
// (mean + sum w(x - mean) / sqrt(sum w^2)), so it is neither paler nor flatter than a copy.
//
// Copies alone left the far view an even speckle with nothing larger than a tile, so cover() lays a
// slow noise over the whole texture: the shade is thinned to under half where the cover opens and
// kept where it is full, masses and gaps two to four tiles wide, and then scaled back so the sum of
// the shade is what EA's tile gives. The ground is as dark on average; inside a mass a texel is EA's
// shade about a sixth darker (the shade's 95th percentile 85 against the retail tile's 73), and in
// a gap under half of it.
//
// Everything is integer arithmetic on a fixed seed, so every machine and every compiler bakes the
// same bytes; the DXT1 decode is here for the same reason (D3DX's and PosixImageOps' would each
// round their own way). Pixels are four bytes in the order they were given, A8R8G8B8's B, G, R, A
// in the game. Nothing here knows the engine: test_cloud_shadow_bake includes it bare.

#pragma once

#ifndef CLOUD_SHADOW_BAKE_H
#define CLOUD_SHADOW_BAKE_H

#include <stddef.h>
#include <vector>

namespace CloudShadowBake {

// How many retail tiles wide the baked texture is: 8 is 2520 world units, wider than the ground
// the highest camera shows in one frame.
enum { REPEAT = 8 };

// How wide, in texels, the band is where two copies run into each other: 40 is about 100 world units,
// wide enough that a cloud edge runs on across it instead of stopping short, narrow enough that most
// of the ground is one copy alone.
enum { SEAM = 40 };

// Mip 0 of a DXT1 .dds, as B, G, R, A bytes a pixel, rows top down. False for anything else.
inline bool decodeDXT1(const unsigned char *file, size_t size, int &width, int &height,
	std::vector<unsigned char> &pixels)
{
	if (file == NULL || size < 128 || file[0] != 'D' || file[1] != 'D' || file[2] != 'S' || file[3] != ' ')
		return false;
	if (file[84] != 'D' || file[85] != 'X' || file[86] != 'T' || file[87] != '1')
		return false;
	height = (int)(file[12] | (file[13] << 8) | (file[14] << 16) | ((unsigned)file[15] << 24));
	width = (int)(file[16] | (file[17] << 8) | (file[18] << 16) | ((unsigned)file[19] << 24));
	if (width <= 0 || height <= 0 || (width & 3) || (height & 3) || width > 4096 || height > 4096)
		return false;
	const int blocksWide = width / 4, blocksHigh = height / 4;
	if (size < 128 + (size_t)blocksWide * blocksHigh * 8)
		return false;

	pixels.assign((size_t)width * height * 4, 0);
	const unsigned char *block = file + 128;
	for (int by = 0; by < blocksHigh; ++by)
		for (int bx = 0; bx < blocksWide; ++bx, block += 8)
		{
			const unsigned c[2] = { (unsigned)(block[0] | (block[1] << 8)), (unsigned)(block[2] | (block[3] << 8)) };
			unsigned char colour[4][4];
			for (int i = 0; i < 2; ++i)
			{
				const unsigned r = (c[i] >> 11) & 31, g = (c[i] >> 5) & 63, b = c[i] & 31;
				colour[i][0] = (unsigned char)((b << 3) | (b >> 2));
				colour[i][1] = (unsigned char)((g << 2) | (g >> 4));
				colour[i][2] = (unsigned char)((r << 3) | (r >> 2));
				colour[i][3] = 255;
			}
			for (int ch = 0; ch < 3; ++ch)
			{
				if (c[0] > c[1])
				{
					colour[2][ch] = (unsigned char)((2 * colour[0][ch] + colour[1][ch]) / 3);
					colour[3][ch] = (unsigned char)((colour[0][ch] + 2 * colour[1][ch]) / 3);
				}
				else
				{
					colour[2][ch] = (unsigned char)((colour[0][ch] + colour[1][ch]) / 2);
					colour[3][ch] = 0;
				}
			}
			colour[2][3] = 255;
			colour[3][3] = (unsigned char)(c[0] > c[1] ? 255 : 0);

			for (int y = 0; y < 4; ++y)
			{
				const unsigned row = block[4 + y];
				for (int x = 0; x < 4; ++x)
				{
					const unsigned char *from = colour[(row >> (2 * x)) & 3];
					unsigned char *to = &pixels[((size_t)(by * 4 + y) * width + bx * 4 + x) * 4];
					to[0] = from[0]; to[1] = from[1]; to[2] = from[2]; to[3] = from[3];
				}
			}
		}
	return true;
}

// A 32 bit hash of a few integers; the bake's only source of chance.
inline unsigned hash(unsigned a, unsigned b, unsigned c)
{
	unsigned h = a * 0x9E3779B1u ^ (b + 0x7F4A7C15u) * 0x85EBCA77u ^ (c + 0x165667B1u) * 0xC2B2AE3Du;
	h ^= h >> 16; h *= 0x7FEB352Du;
	h ^= h >> 15; h *= 0x846CA68Bu;
	h ^= h >> 16;
	return h;
}

inline unsigned long long isqrt(unsigned long long n)
{
	unsigned long long root = 0, bit = 1ull << 62;
	while (bit > n) bit >>= 2;
	while (bit != 0)
	{
		if (n >= root + bit) { n -= root + bit; root = (root >> 1) + bit; }
		else root >>= 1;
		bit >>= 2;
	}
	return root;
}

// The source tiled side by side, unchanged: Classic's texture, and the bake's fallback.
inline void tile(const unsigned char *source, int sourceWidth, int sourceHeight,
	unsigned char *dest, int destSide, int destPitch)
{
	for (int y = 0; y < destSide; ++y)
		for (int x = 0; x < destSide; ++x)
		{
			const unsigned char *from = source + ((size_t)(y % sourceHeight) * sourceWidth + (x % sourceWidth)) * 4;
			unsigned char *to = dest + (size_t)y * destPitch + x * 4;
			to[0] = from[0]; to[1] = from[1]; to[2] = from[2]; to[3] = from[3];
		}
}

// Smoothstep in 16.16: f and the result in [0, 65536].
inline long long fade(long long f)
{
	return (f * f * (3 * 65536 - 2 * f)) >> 32;
}

// Smooth value noise on a torus of `side` texels with `lattice` cells a side, in [0, 65535].
inline long long latticeNoise(int x, int y, int side, int lattice, unsigned seed)
{
	const int cell = side / lattice;
	const int ix = x / cell, iy = y / cell;
	const long long fx = fade((long long)(x % cell) * 65536 / cell);
	const long long fy = fade((long long)(y % cell) * 65536 / cell);
	const long long a = hash(ix % lattice, iy % lattice, seed) & 0xFFFF;
	const long long b = hash((ix + 1) % lattice, iy % lattice, seed) & 0xFFFF;
	const long long c = hash(ix % lattice, (iy + 1) % lattice, seed) & 0xFFFF;
	const long long d = hash((ix + 1) % lattice, (iy + 1) % lattice, seed) & 0xFFFF;
	const long long top = a + (((b - a) * fx) >> 16);
	const long long bottom = c + (((d - c) * fx) >> 16);
	return top + (((bottom - top) * fy) >> 16);
}

// The cloud cover over the whole texture: the shade (255 less the texel) is thinned to CLEAR of
// itself where the cover is open and kept where it is full, by a smooth noise COVER cells across
// the texture (a cell is two retail tiles, so the masses and the gaps run two to four tiles), and
// then every channel's shade is scaled so its sum is what it was before: the ground is as dark on
// average as under EA's tile, the masses a little darker than one retail tile to pay for the gaps.
enum { COVER = 4 };
enum { CLEAR = 29491 };				// 0.45 of the shade, in 16.16
enum { COVER_LOW = 20000, COVER_HIGH = 34000 };	// the noise's open and full ends, of 65535

inline void cover(unsigned char *dest, int side, int pitch, unsigned seed)
{
	if (side < COVER * 2)
		return;
	std::vector<unsigned short> gain((size_t)side * side);
	long long before[3] = { 0, 0, 0 }, after[3] = { 0, 0, 0 };
	for (int y = 0; y < side; ++y)
		for (int x = 0; x < side; ++x)
		{
			const long long m = (2 * latticeNoise(x, y, side, COVER, seed + 11)
				+ latticeNoise(x, y, side, COVER * 2, seed + 12)) / 3;
			long long t = (m - COVER_LOW) * 65536 / (COVER_HIGH - COVER_LOW);
			t = t < 0 ? 0 : (t > 65536 ? 65536 : t);
			const long long g = CLEAR + (((65536 - CLEAR) * fade(t)) >> 16);	// up to 65536
			gain[(size_t)y * side + x] = (unsigned short)(g >> 1);	// 15 bits, 32768 is one
			const unsigned char *p = dest + (size_t)y * pitch + x * 4;
			for (int ch = 0; ch < 3; ++ch)
			{
				before[ch] += 255 - p[ch];
				after[ch] += (long long)(255 - p[ch]) * (g >> 1);
			}
		}

	for (int ch = 0; ch < 3; ++ch)
	{
		// The scale that brings the thinned sum back to the whole, in 16.16 over the 15 bit gain.
		const long long scale = after[ch] > 0 ? (before[ch] * 32768 * 65536 + after[ch] / 2) / after[ch] : 65536;
		for (int y = 0; y < side; ++y)
			for (int x = 0; x < side; ++x)
			{
				unsigned char *p = dest + (size_t)y * pitch + x * 4;
				const long long shade = ((long long)(255 - p[ch]) * gain[(size_t)y * side + x] * scale + (1ll << 30)) >> 31;
				p[ch] = (unsigned char)(shade > 255 ? 0 : 255 - shade);
			}
	}
}

// The scattered texture, destSide texels a side (a whole number of source tiles, power of two),
// wrapping at its edges like the source. destPitch is in bytes.
inline void scatter(const unsigned char *source, int sourceWidth, int sourceHeight,
	unsigned char *dest, int destSide, int destPitch, unsigned seed = 0x434C4F55u)
{
	const int cell = sourceWidth > sourceHeight ? sourceWidth : sourceHeight;
	const int cells = destSide / cell;
	if (cells < 2)
	{
		tile(source, sourceWidth, sourceHeight, dest, destSide, destPitch);
		return;
	}

	// The retail tile's own mean, per channel, which the copies are spread about.
	long long mean[4] = { 0, 0, 0, 0 };
	const long long count = (long long)sourceWidth * sourceHeight;
	for (long long i = 0; i < count; ++i)
		for (int ch = 0; ch < 4; ++ch)
			mean[ch] += source[i * 4 + ch];
	for (int ch = 0; ch < 4; ++ch)
		mean[ch] = (mean[ch] + count / 2) / count;

	// One copy a cell: its centre jittered by up to a quarter of a cell, where in the source it
	// starts, and which of its four mirrorings it wears. EA's clouds are drawn stretched east to
	// west, and quarter turns among them wove the ground into a basket.
	struct Splat { int cx, cy, ox, oy, mirror; };
	std::vector<Splat> splats((size_t)cells * cells);
	for (int j = 0; j < cells; ++j)
		for (int i = 0; i < cells; ++i)
		{
			Splat &s = splats[(size_t)j * cells + i];
			s.cx = i * cell + cell / 2 + (int)(hash(i, j, seed) % (unsigned)(cell / 2 + 1)) - cell / 4;
			s.cy = j * cell + cell / 2 + (int)(hash(i, j, seed + 1) % (unsigned)(cell / 2 + 1)) - cell / 4;
			s.ox = (int)(hash(i, j, seed + 2) % (unsigned)sourceWidth);
			s.oy = (int)(hash(i, j, seed + 3) % (unsigned)sourceHeight);
			s.mirror = (int)(hash(i, j, seed + 4) & 3);
		}

	// Each texel belongs to its nearest copy and takes it alone, EA's texels exactly, except within
	// a seam SEAM texels wide along the line halfway to the next copy, where the two run into each
	// other: a copy's weight is how far inside the band (in squared texels) it falls of being the
	// nearest. Two centres about a cell apart change their squared distances by two cells a texel
	// across that line, so a band of SEAM cells is SEAM texels of seam. With the jitter the nearest centre is never further than
	// 1.06 cells, so it is among the nine cells round the texel's own, and no copy further out is
	// within the band.
	const long long band = (long long)SEAM * cell;
	const int half = destSide / 2;
	for (int y = 0; y < destSide; ++y)
		for (int x = 0; x < destSide; ++x)
		{
			const int ci = x / cell, cj = y / cell;
			const Splat *copies[9];
			int copyX[9], copyY[9];
			long long distance2[9], nearest = -1;
			for (int k = 0; k < 9; ++k)
			{
				const int di = k % 3 - 1, dj = k / 3 - 1;
				const Splat &s = splats[(size_t)((cj + dj + cells) % cells) * cells + (ci + di + cells) % cells];
				int dx = x - s.cx, dy = y - s.cy;
				dx = ((dx + half) % destSide + destSide) % destSide - half;
				dy = ((dy + half) % destSide + destSide) % destSide - half;
				copies[k] = &s;
				copyX[k] = dx;
				copyY[k] = dy;
				distance2[k] = (long long)dx * dx + (long long)dy * dy;
				if (nearest < 0 || distance2[k] < nearest)
					nearest = distance2[k];
			}

			long long sum[4] = { 0, 0, 0, 0 };
			unsigned long long weight2 = 0;
			for (int k = 0; k < 9; ++k)
			{
				const long long q = band - (distance2[k] - nearest);
				if (q <= 0)
					continue;
				const long long w = q * q;	// at most (SEAM * 128)^2 for the retail tile
				weight2 += (unsigned long long)w * (unsigned long long)w;

				const Splat &s = *copies[k];
				const int dx = copyX[k], dy = copyY[k];
				int u = (s.mirror & 1) ? -dx : dx, v = (s.mirror & 2) ? -dy : dy;
				u = ((s.ox + u) % sourceWidth + sourceWidth) % sourceWidth;
				v = ((s.oy + v) % sourceHeight + sourceHeight) % sourceHeight;
				const unsigned char *texel = source + ((size_t)v * sourceWidth + u) * 4;
				for (int ch = 0; ch < 4; ++ch)
					sum[ch] += w * (texel[ch] - mean[ch]);
			}

			unsigned char *to = dest + (size_t)y * destPitch + x * 4;
			const long long norm = (long long)isqrt(weight2);
			for (int ch = 0; ch < 4; ++ch)
			{
				long long value = mean[ch];
				if (norm > 0)
					value += (sum[ch] >= 0 ? sum[ch] + norm / 2 : sum[ch] - norm / 2) / norm;
				to[ch] = (unsigned char)(value < 0 ? 0 : (value > 255 ? 255 : value));
			}
		}

	cover(dest, destSide, destPitch, seed);
}

}	// namespace CloudShadowBake

#endif	// CLOUD_SHADOW_BAKE_H
