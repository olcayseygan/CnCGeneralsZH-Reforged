/*
**	Copyright 2026 İlyas Akın
**	Additional terms under GNU GPL section 7 apply: see LICENSE.md.
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

// AniCursor.cpp: see AniCursor.h.

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "SdlDevice/GameClient/AniCursor.h"

#include <stdlib.h>
#include <string.h>

namespace {

UnsignedInt le16( const UnsignedByte *p ) { return (UnsignedInt)p[0] | ((UnsignedInt)p[1] << 8); }
UnsignedInt le32( const UnsignedByte *p ) { return le16( p ) | (le16( p + 2 ) << 16); }

Bool fail( std::string *why, const char *text )
{
	if (why != NULL)
		*why = text;
	return FALSE;
}

enum { ANIH_SIZE = 36, AF_ICON = 0x1, AF_SEQUENCE = 0x2 };

}  // namespace

Bool AniCursor_decodeCursorFile( const UnsignedByte *data, size_t size, AniCursorFrame &out, std::string *why, Int preferredSize )
{
	// ICONDIR, then the entry wanted: the first, or the one whose width is nearest preferredSize
	if (size < 6 + 16)
		return fail( why, "shorter than an icon directory" );
	const UnsignedInt type = le16( data + 2 ), count = le16( data + 4 );
	if (le16( data ) != 0 || (type != 1 && type != 2) || count == 0)
		return fail( why, "not an .ICO or .CUR directory" );
	const UnsignedByte *entry = data + 6;
	for (UnsignedInt i = 1; preferredSize > 0 && i < count && 6 + 16 * (size_t)(i + 1) <= size; ++i)
	{
		const UnsignedByte *other = data + 6 + 16 * i;
		const Int best = entry[0] ? entry[0] : 256, width = other[0] ? other[0] : 256;		// 0 is 256
		if (abs( width - preferredSize ) < abs( best - preferredSize ))
			entry = other;
	}
	const UnsignedInt imageSize = le32( entry + 8 ), imageOffset = le32( entry + 12 );
	if (imageOffset > size || imageSize > size - imageOffset || imageSize < 40)
		return fail( why, "the image lies outside the file" );
	const UnsignedByte *image = data + imageOffset;
	if (memcmp( image, "\x89PNG", 4 ) == 0)
		return fail( why, "a PNG-compressed frame" );

	// BITMAPINFOHEADER; its height counts the colour rows and the mask's together
	const UnsignedInt headerSize = le32( image );
	const Int width = (Int)le32( image + 4 );
	const Int doubleHeight = (Int)le32( image + 8 );
	const UnsignedInt bits = le16( image + 14 ), compression = le32( image + 16 );
	UnsignedInt colours = le32( image + 32 );
	if (headerSize < 40 || headerSize > imageSize || width <= 0 || width > 256 || doubleHeight <= 0
			|| doubleHeight % 2 != 0 || doubleHeight / 2 > 256 || compression != 0)
		return fail( why, "an unreadable bitmap header" );
	if (bits != 1 && bits != 4 && bits != 8 && bits != 24 && bits != 32)
		return fail( why, "a bit depth this does not read" );
	const Int height = doubleHeight / 2;
	if (bits <= 8 && colours == 0)
		colours = 1u << bits;
	if (bits > 8)
		colours = 0;
	const UnsignedInt colourStride = ((UnsignedInt)width * bits + 31) / 32 * 4;
	const UnsignedInt maskStride = ((UnsignedInt)width + 31) / 32 * 4;
	const UnsignedInt paletteAt = headerSize, colourAt = paletteAt + colours * 4;
	const UnsignedInt maskAt = colourAt + colourStride * height;
	if ((size_t)maskAt + (size_t)maskStride * height > imageSize)
		return fail( why, "the rows run past the image" );

	out.width = width;
	out.height = height;
	out.hotX = type == 2 ? (Int)le16( entry + 4 ) : 0;
	out.hotY = type == 2 ? (Int)le16( entry + 6 ) : 0;
	out.invertedPixels = 0;
	out.rgba.assign( (size_t)width * height * 4, 0 );
	for (Int y = 0; y < height; ++y)
	{
		const Int row = height - 1 - y;		// bottom-up
		const UnsignedByte *colourRow = image + colourAt + colourStride * row;
		const UnsignedByte *maskRow = image + maskAt + maskStride * row;
		for (Int x = 0; x < width; ++x)
		{
			UnsignedByte b, g, r, a = 255;
			if (bits <= 8)
			{
				const UnsignedInt bit = (UnsignedInt)x * bits;
				const UnsignedInt index = (colourRow[ bit / 8 ] >> (8 - bits - bit % 8)) & ((1u << bits) - 1);
				if (index >= colours)
					return fail( why, "a palette index past the palette" );
				const UnsignedByte *quad = image + paletteAt + index * 4;
				b = quad[0]; g = quad[1]; r = quad[2];
			}
			else
			{
				const UnsignedByte *p = colourRow + x * (bits / 8);
				b = p[0]; g = p[1]; r = p[2];
				if (bits == 32)
					a = p[3];
			}
			const Bool masked = (maskRow[ x / 8 ] >> (7 - x % 8)) & 1;
			if (masked)
			{
				if (r != 0 || g != 0 || b != 0)
					++out.invertedPixels;
				r = g = b = a = 0;
			}
			UnsignedByte *o = &out.rgba[ ((size_t)y * width + x) * 4 ];
			o[0] = r; o[1] = g; o[2] = b; o[3] = a;
		}
	}
	return TRUE;
}

Bool AniCursor_decode( const UnsignedByte *data, size_t size, AniCursor &out, std::string *why, Int preferredSize )
{
	out.frames.clear();
	out.steps.clear();
	if (size < 12 || memcmp( data, "RIFF", 4 ) != 0 || memcmp( data + 8, "ACON", 4 ) != 0)
		return fail( why, "not a RIFF ACON file" );
	const size_t end = 8 + (size_t)le32( data + 4 ) < size ? 8 + (size_t)le32( data + 4 ) : size;

	UnsignedInt frames = 0, steps = 0, displayRate = 0, flags = 0;
	Bool haveHeader = FALSE;
	std::vector<UnsignedInt> rates, sequence;
	for (size_t at = 12; at + 8 <= end; )
	{
		const UnsignedByte *chunk = data + at;
		const size_t length = le32( chunk + 4 );
		if (length > end - at - 8)
			return fail( why, "a chunk runs past the file" );
		const UnsignedByte *body = chunk + 8;
		if (memcmp( chunk, "anih", 4 ) == 0 && length >= ANIH_SIZE)
		{
			frames = le32( body + 4 );
			steps = le32( body + 8 );
			displayRate = le32( body + 28 );
			flags = le32( body + 32 );
			haveHeader = TRUE;
		}
		else if (memcmp( chunk, "rate", 4 ) == 0 || memcmp( chunk, "seq ", 4 ) == 0)
		{
			std::vector<UnsignedInt> &list = chunk[0] == 'r' ? rates : sequence;
			for (size_t i = 0; i + 4 <= length; i += 4)
				list.push_back( le32( body + i ) );
		}
		else if (memcmp( chunk, "LIST", 4 ) == 0 && length >= 4 && memcmp( body, "fram", 4 ) == 0)
		{
			for (size_t f = 4; f + 8 <= length; )
			{
				const UnsignedByte *sub = body + f;
				const size_t subLength = le32( sub + 4 );
				if (subLength > length - f - 8)
					return fail( why, "a frame runs past its list" );
				if (memcmp( sub, "icon", 4 ) == 0)
				{
					AniCursorFrame frame;
					std::string reason;
					if (!AniCursor_decodeCursorFile( sub + 8, subLength, frame, &reason, preferredSize ))
						return fail( why, ("frame: " + reason).c_str() );
					out.frames.push_back( frame );
				}
				f += 8 + subLength + (subLength & 1);
			}
		}
		at += 8 + length + (length & 1);
	}
	if (!haveHeader)
		return fail( why, "no 'anih' header" );
	if ((flags & AF_ICON) == 0)
		return fail( why, "raw frames, not icons" );
	if (out.frames.empty() || out.frames.size() != frames)
		return fail( why, "the frame count differs from the header's" );
	if (steps == 0)
		steps = frames;
	for (UnsignedInt i = 0; i < steps; ++i)
	{
		AniCursorStep step;
		step.frame = (flags & AF_SEQUENCE) && i < sequence.size() ? (Int)sequence[i] : (Int)(i % frames);
		if (step.frame < 0 || step.frame >= (Int)frames)
			return fail( why, "a sequence step names a frame that is not there" );
		step.durationMs = AniCursor_jiffiesToMs( i < rates.size() ? rates[i] : displayRate );
		out.steps.push_back( step );
	}
	return TRUE;
}
