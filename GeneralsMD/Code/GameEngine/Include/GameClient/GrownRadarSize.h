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

// FILE: GrownRadarSize.h /////////////////////////////////////////////////////////////////////////
//
// How tall Minimap Size draws a grown radar in a side's own frame (ControlBar::fitSquareRadar), in plain
// integers so test_grown_radar checks it alone.
//
// The frame is laid round the picture joined with the painted glass, so the picture is at least the glass's
// width, or a band of bare glass shows down its right side.  On a tall map that floor sits above 100: a 3:4
// map needs 133 to fill the glass.  The setting ramps from that floor at 101 to twice the box at 200, so
// every step grows the picture.  A map too tall to fill the glass even at 200 (1:2) keeps its band and ramps
// from the box, as a wide map does.
//
///////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

// boxHeight: the inside of the radar box; glassHeight: the inside height that fills the glass's width at the
// map's shape; percent: Minimap Size, held to 100..200.  Rounded half up.
inline int GrownRadar_insideHeight( int boxHeight, int glassHeight, int percent )
{
	const int p = percent < 100 ? 100 : ( percent > 200 ? 200 : percent );
	const int full = boxHeight * 2;
	const int start = ( glassHeight > boxHeight && glassHeight < full ) ? glassHeight : boxHeight;
	return ( start * ( 200 - p ) + full * ( p - 100 ) + 50 ) / 100;
}
