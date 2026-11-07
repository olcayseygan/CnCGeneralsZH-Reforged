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

// AniCursor.h: Windows animated cursor files (.ANI) decoded to RGBA frames (C3).
//
// Win32Mouse gives the game its default cursors (RM_WINDOWS) with LoadCursorFromFile over the 52
// Data/Cursors/*.ANI files.  Off Windows SdlMouse makes SDL cursors from what this decodes.  The format:
// a RIFF "ACON" file with an 'anih' header, optional 'rate' (per-step durations, in jiffies of 1/60 s)
// and 'seq ' (per-step frame indices) chunks, and a LIST 'fram' of 'icon' chunks, each a whole .CUR or
// .ICO file holding a device-independent bitmap: a BITMAPINFOHEADER, a palette at 8 bits or fewer, the
// colour (XOR) rows and a 1-bit AND mask, both bottom-up.  EA's frames are 32 x 32 at 4 bits; the
// Reforged ones (Tools/cursors.py) hold a 48 x 48 at 32 bits first and a 32 x 32 after it, and the
// first image is the one read.  This reads 1, 4, 8, 24 and 32 bits.  No SDL here: bytes in, pixels out.
//
// What a bitmap cursor cannot become: an AND bit set over a non-black colour inverts the screen under
// it on Windows.  There is no such pixel in an SDL colour cursor; it is made transparent, and counted.
// PNG-compressed frames (Vista icons) are refused; the game has none.

#pragma once

#ifndef __ANICURSOR_H
#define __ANICURSOR_H

#include "Lib/BaseType.h"

#include <string>
#include <vector>

struct AniCursorFrame
{
	Int width;
	Int height;
	Int hotX;
	Int hotY;
	std::vector<UnsignedByte> rgba;		///< width * height * 4, top row first, straight alpha
	Int invertedPixels;								///< AND-set over non-black: screen inversion on Windows, transparent here
};

struct AniCursorStep
{
	Int frame;					///< index into frames
	UnsignedInt durationMs;
};

struct AniCursor
{
	std::vector<AniCursorFrame> frames;
	std::vector<AniCursorStep> steps;		///< the animation, in order: 'seq ' and 'rate' applied
};

/// Decodes a whole .ANI file; FALSE, with the reason in why, when it is not one this can read
Bool AniCursor_decode( const UnsignedByte *data, size_t size, AniCursor &out, std::string *why = NULL );

/// One .CUR or .ICO file's first image; the hotspot is 0,0 for an .ICO
Bool AniCursor_decodeCursorFile( const UnsignedByte *data, size_t size, AniCursorFrame &out, std::string *why = NULL );

/// A jiffy count, 1/60 s each, in milliseconds as SDL's frame durations take them
inline UnsignedInt AniCursor_jiffiesToMs( UnsignedInt jiffies ) { return (jiffies * 1000u + 30u) / 60u; }

#endif // __ANICURSOR_H
