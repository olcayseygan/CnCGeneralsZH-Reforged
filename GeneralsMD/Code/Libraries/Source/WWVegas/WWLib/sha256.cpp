/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2026 Olcay Seygan
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

// FILE: sha256.cpp ///////////////////////////////////////////////////////////////////////////////

#include "sha256.h"

#include <string.h>

static const uint32_t K[64] = {
	0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
	0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
	0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
	0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
	0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
	0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
	0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
	0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

static inline uint32_t Rotr( uint32_t x, int n ) { return (x >> n) | (x << (32 - n)); }

Sha256::Sha256() : Length( 0 ), Buffered( 0 )
{
	static const uint32_t initial[8] = {
		0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19,
	};
	memcpy( State, initial, sizeof(State) );
}

void Sha256::Block( const unsigned char *p )
{
	uint32_t w[64];
	for (int i = 0; i < 16; ++i)
		w[i] = ((uint32_t)p[i*4] << 24) | ((uint32_t)p[i*4+1] << 16) | ((uint32_t)p[i*4+2] << 8) | p[i*4+3];
	for (int i = 16; i < 64; ++i)
	{
		const uint32_t s0 = Rotr( w[i-15], 7 ) ^ Rotr( w[i-15], 18 ) ^ (w[i-15] >> 3);
		const uint32_t s1 = Rotr( w[i-2], 17 ) ^ Rotr( w[i-2], 19 ) ^ (w[i-2] >> 10);
		w[i] = w[i-16] + s0 + w[i-7] + s1;
	}
	uint32_t a = State[0], b = State[1], c = State[2], d = State[3];
	uint32_t e = State[4], f = State[5], g = State[6], h = State[7];
	for (int i = 0; i < 64; ++i)
	{
		const uint32_t t1 = h + (Rotr( e, 6 ) ^ Rotr( e, 11 ) ^ Rotr( e, 25 )) + ((e & f) ^ (~e & g)) + K[i] + w[i];
		const uint32_t t2 = (Rotr( a, 2 ) ^ Rotr( a, 13 ) ^ Rotr( a, 22 )) + ((a & b) ^ (a & c) ^ (b & c));
		h = g; g = f; f = e; e = d + t1;
		d = c; c = b; b = a; a = t1 + t2;
	}
	State[0] += a; State[1] += b; State[2] += c; State[3] += d;
	State[4] += e; State[5] += f; State[6] += g; State[7] += h;
}

void Sha256::Update( const void *data, size_t length )
{
	const unsigned char *p = (const unsigned char *)data;
	Length += length;
	if (Buffered)
	{
		const size_t take = length < 64 - Buffered ? length : 64 - Buffered;
		memcpy( Buffer + Buffered, p, take );
		Buffered += take;
		p += take;
		length -= take;
		if (Buffered < 64)
			return;
		Block( Buffer );
		Buffered = 0;
	}
	for (; length >= 64; p += 64, length -= 64)
		Block( p );
	memcpy( Buffer, p, length );
	Buffered = length;
}

void Sha256::Final( unsigned char digest[32] )
{
	const uint64_t bits = Length * 8;
	const unsigned char one = 0x80, zero = 0;
	Update( &one, 1 );
	while (Buffered != 56)
		Update( &zero, 1 );
	unsigned char size[8];
	for (int i = 0; i < 8; ++i)
		size[i] = (unsigned char)(bits >> (56 - i * 8));
	Update( size, 8 );
	for (int i = 0; i < 8; ++i)
	{
		digest[i*4] = (unsigned char)(State[i] >> 24);
		digest[i*4+1] = (unsigned char)(State[i] >> 16);
		digest[i*4+2] = (unsigned char)(State[i] >> 8);
		digest[i*4+3] = (unsigned char)State[i];
	}
}

void Sha256::Hex( const unsigned char digest[32], char out[65] )
{
	static const char digits[] = "0123456789abcdef";
	for (int i = 0; i < 32; ++i)
	{
		out[i*2] = digits[digest[i] >> 4];
		out[i*2+1] = digits[digest[i] & 15];
	}
	out[64] = 0;
}
