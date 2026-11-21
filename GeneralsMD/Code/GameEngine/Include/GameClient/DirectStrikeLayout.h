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

// FILE: DirectStrikeLayout.h /////////////////////////////////////////////////////////////////////
//
// Where the Direct Strike screens stand, in plain integers so test_lanehud checks them alone.  A screen
// is authored at 1920x1080 and put on any screen by the frame: k = min(W / 1920, H / 1080), the 1920k
// by 1080k box centred.  The lobby's rects follow hud-artist's README-lobby.md of 2026-11-21 in the
// wrapper (experiments/proofs/2026-11-21/ds-lobby); the in-match HUD keeps its own anchoring in
// LaneHudLayout.h.  DirectStrikeTheme.h is what draws in them.
//
///////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#ifndef __DIRECTSTRIKELAYOUT_H_
#define __DIRECTSTRIKELAYOUT_H_

#include "GameClient/LaneHudLayout.h"

/// a 1920x1080 screen put on a real one
struct DirectStrikeFrame
{
	float k;			///< min( W / 1920, H / 1080 )
	int ox, oy;		///< where the design's top left lands
};

/// a rect in the design's 1920x1080 space
struct DirectStrikeRect
{
	float x0, y0, x1, y1;
	bool contains( float x, float y ) const { return x >= x0 && x < x1 && y >= y0 && y < y1; }
};

inline DirectStrikeRect DirectStrike_rect( float x0, float y0, float x1, float y1 )
{
	DirectStrikeRect r;
	r.x0 = x0;
	r.y0 = y0;
	r.x1 = x1;
	r.y1 = y1;
	return r;
}

inline DirectStrikeFrame DirectStrike_frame( int width, int height )
{
	DirectStrikeFrame f;
	const float kx = (float)width / 1920.0f, ky = (float)height / 1080.0f;
	f.k = kx < ky ? kx : ky;
	f.ox = (int)( ( (float)width - 1920.0f * f.k ) * 0.5f + 0.5f );
	f.oy = (int)( ( (float)height - 1080.0f * f.k ) * 0.5f + 0.5f );
	return f;
}

/// a design length on the screen, one pixel at least
inline int DirectStrike_px( const DirectStrikeFrame &f, float design )
{
	const int v = (int)( design * f.k + 0.5f );
	return v < 1 ? 1 : v;
}

/// a design rect on the screen; both corners are rounded, so neighbours share their edge
inline LaneHudBox DirectStrike_box( const DirectStrikeFrame &f, const DirectStrikeRect &r )
{
	const int lx = f.ox + (int)( r.x0 * f.k + 0.5f ), ly = f.oy + (int)( r.y0 * f.k + 0.5f );
	const int hx = f.ox + (int)( r.x1 * f.k + 0.5f ), hy = f.oy + (int)( r.y1 * f.k + 0.5f );
	return LaneHud_box( lx, ly, hx - lx, hy - ly );
}

/// a screen point back in the design's space
inline void DirectStrike_toDesign( const DirectStrikeFrame &f, int x, int y, float *dx, float *dy )
{
	*dx = ( (float)( x - f.ox ) ) / f.k;
	*dy = ( (float)( y - f.oy ) ) / f.k;
}

//-------------------------------------------------------------------------------------------------
// The lobby, README-lobby.md's tables
//-------------------------------------------------------------------------------------------------

enum
{
	DSLOBBY_TEAMS = 2,
	DSLOBBY_ROWS = 3,						///< seats a team
	DSLOBBY_SEATS = DSLOBBY_TEAMS * DSLOBBY_ROWS,
	DSLOBBY_SEAT_ROWS = 6,			///< the host's seat control: Open, Closed and four levels
	DSLOBBY_ARMY_COLUMNS = 3,
	DSLOBBY_ARMY_TILES = 4,			///< down a column: the side, then its three generals
	DSLOBBY_COLOR_COLUMNS = 4,
};

/// the part of a seat card, README-lobby.md "The seat card"
enum DirectStrikeSeatPart
{
	DSSEAT_PORTRAIT,
	DSSEAT_LINE,
	DSSEAT_LAMP,
	DSSEAT_ARMY,
	DSSEAT_COLOR,
	DSSEAT_PING,
	DSSEAT_PARTS
};

/// the seat a team's row is: seat parity is the team, as FitDirectStrikeRoom seats them
inline int DirectStrikeLobby_slot( int team, int row ) { return row * DSLOBBY_TEAMS + team; }
inline int DirectStrikeLobby_team( int slot ) { return slot % DSLOBBY_TEAMS; }
inline int DirectStrikeLobby_row( int slot ) { return slot / DSLOBBY_TEAMS; }

inline DirectStrikeRect DirectStrikeLobby_title( void ) { return DirectStrike_rect( 700, 86, 1220, 158 ); }

inline DirectStrikeRect DirectStrikeLobby_teamHead( int team )
{
	return team == 0 ? DirectStrike_rect( 80, 182, 680, 226 ) : DirectStrike_rect( 1240, 182, 1840, 226 );
}

inline DirectStrikeRect DirectStrikeLobby_card( int slot )
{
	const float x = DirectStrikeLobby_team( slot ) == 0 ? 80.0f : 1240.0f;
	const float y = 242.0f + 216.0f * (float)DirectStrikeLobby_row( slot );
	return DirectStrike_rect( x, y, x + 600.0f, y + 200.0f );
}

/// a part of a seat's card in the design's space; team 2's card is team 1's mirrored, x becoming 600 - x
inline DirectStrikeRect DirectStrikeLobby_part( int slot, DirectStrikeSeatPart part )
{
	static const float PARTS[ DSSEAT_PARTS ][ 4 ] =
	{
		{ 10, 10, 190, 190 },			// portrait well
		{ 204, 12, 532, 60 },			// seat line
		{ 548, 20, 580, 52 },			// ready lamp
		{ 204, 76, 588, 124 },		// army chip
		{ 204, 140, 300, 188 },		// colour chip
		{ 548, 152, 576, 176 },		// ping
	};
	const DirectStrikeRect card = DirectStrikeLobby_card( slot );
	const float *p = PARTS[ part ];
	if( DirectStrikeLobby_team( slot ) == 0 )
		return DirectStrike_rect( card.x0 + p[ 0 ], card.y0 + p[ 1 ], card.x0 + p[ 2 ], card.y0 + p[ 3 ] );
	return DirectStrike_rect( card.x0 + 600.0f - p[ 2 ], card.y0 + p[ 1 ], card.x0 + 600.0f - p[ 0 ], card.y0 + p[ 3 ] );
}

inline DirectStrikeRect DirectStrikeLobby_back( void ) { return DirectStrike_rect( 700, 922, 772, 994 ); }
inline DirectStrikeRect DirectStrikeLobby_start( void ) { return DirectStrike_rect( 788, 922, 1220, 994 ); }

// offline: the island panel under the title
inline DirectStrikeRect DirectStrikeLobby_islandPanel( void ) { return DirectStrike_rect( 700, 182, 1220, 874 ); }
inline DirectStrikeRect DirectStrikeLobby_island( void ) { return DirectStrike_rect( 720, 202, 1200, 682 ); }

// a network room: the island chip, the chat, its input and the send key
inline DirectStrikeRect DirectStrikeLobby_islandChip( void ) { return DirectStrike_rect( 700, 182, 1220, 262 ); }
inline DirectStrikeRect DirectStrikeLobby_islandSmall( void ) { return DirectStrike_rect( 710, 192, 806, 252 ); }
inline DirectStrikeRect DirectStrikeLobby_chat( void ) { return DirectStrike_rect( 700, 278, 1220, 810 ); }
inline DirectStrikeRect DirectStrikeLobby_chatInput( void ) { return DirectStrike_rect( 700, 826, 1160, 874 ); }
inline DirectStrikeRect DirectStrikeLobby_send( void ) { return DirectStrike_rect( 1172, 826, 1220, 874 ); }

enum
{
	DSLOBBY_CHAT_LINE = 26,			///< chat lines this far apart
	DSLOBBY_LIST_GAP = 4,				///< an open list stands this far from its chip
	DSLOBBY_LIST_ROW = 36,			///< a row of the seat control
	DSLOBBY_LIST_PAD = 8,				///< the seat control's rows start this far in
	DSLOBBY_PICKER_W = 456,
	DSLOBBY_PICKER_H = 564,
	DSLOBBY_TILE_W = 136,
	DSLOBBY_TILE_H = 112,
	DSLOBBY_TILE_GAP = 8,
	DSLOBBY_COLOR_CELL_W = 48,
	DSLOBBY_COLOR_CELL_H = 40,
	DSLOBBY_COLOR_PAD = 16,
};

/// an open list of height h under its chip, or above it when it would run past the screen's foot
inline DirectStrikeRect DirectStrikeLobby_dropBelow( const DirectStrikeRect &chip, float w, float h, bool alignRight )
{
	const float x0 = alignRight ? chip.x1 - w : chip.x0;
	if( chip.y1 + DSLOBBY_LIST_GAP + h > 1080.0f )
		return DirectStrike_rect( x0, chip.y0 - DSLOBBY_LIST_GAP - h, x0 + w, chip.y0 - DSLOBBY_LIST_GAP );
	return DirectStrike_rect( x0, chip.y1 + DSLOBBY_LIST_GAP, x0 + w, chip.y1 + DSLOBBY_LIST_GAP + h );
}

/// the host's seat control under a seat line
inline DirectStrikeRect DirectStrikeLobby_seatList( int slot )
{
	const DirectStrikeRect line = DirectStrikeLobby_part( slot, DSSEAT_LINE );
	return DirectStrikeLobby_dropBelow( line, line.x1 - line.x0, 2.0f * DSLOBBY_LIST_PAD + DSLOBBY_SEAT_ROWS * DSLOBBY_LIST_ROW, false );
}

inline DirectStrikeRect DirectStrikeLobby_seatRow( int slot, int row )
{
	const DirectStrikeRect list = DirectStrikeLobby_seatList( slot );
	const float y = list.y0 + DSLOBBY_LIST_PAD + DSLOBBY_LIST_ROW * (float)row;
	return DirectStrike_rect( list.x0 + DSLOBBY_LIST_PAD, y, list.x1 - DSLOBBY_LIST_PAD, y + DSLOBBY_LIST_ROW );
}

/// the army picker: team 1's right edge under its chip's right edge, team 2's left under the left; seat 3
/// opens up.  The middle row's fits neither under nor over its chip, so it stands in the centre column
/// level with the chip, over the island and the chat.
inline DirectStrikeRect DirectStrikeLobby_picker( int slot )
{
	const DirectStrikeRect chip = DirectStrikeLobby_part( slot, DSSEAT_ARMY );
	const bool left = DirectStrikeLobby_team( slot ) == 0;
	const float x0 = left ? chip.x1 - DSLOBBY_PICKER_W : chip.x0;
	const float above = chip.y0 - DSLOBBY_LIST_GAP - DSLOBBY_PICKER_H;
	if( DirectStrikeLobby_row( slot ) == DSLOBBY_ROWS - 1 )
		return DirectStrike_rect( x0, above, x0 + DSLOBBY_PICKER_W, chip.y0 - DSLOBBY_LIST_GAP );
	if( chip.y1 + DSLOBBY_LIST_GAP + DSLOBBY_PICKER_H > 1080.0f && above < 0.0f )
	{
		const float y0 = ( chip.y0 + chip.y1 - DSLOBBY_PICKER_H ) * 0.5f;
		return DirectStrike_rect( 960.0f - DSLOBBY_PICKER_W * 0.5f, y0, 960.0f + DSLOBBY_PICKER_W * 0.5f, y0 + DSLOBBY_PICKER_H );
	}
	return DirectStrikeLobby_dropBelow( chip, DSLOBBY_PICKER_W, DSLOBBY_PICKER_H, left );
}

inline DirectStrikeRect DirectStrikeLobby_pickerRandom( int slot )
{
	const DirectStrikeRect p = DirectStrikeLobby_picker( slot );
	return DirectStrike_rect( p.x0 + 16, p.y0 + 16, p.x0 + 16 + 424, p.y0 + 16 + 48 );
}

/// tile `tile` (0 the side, 1 to 3 its generals) of column `column` (USA, China, GLA)
inline DirectStrikeRect DirectStrikeLobby_pickerTile( int slot, int column, int tile )
{
	const DirectStrikeRect p = DirectStrikeLobby_picker( slot );
	const float x = p.x0 + 16 + (float)column * ( DSLOBBY_TILE_W + DSLOBBY_TILE_GAP );
	const float y = p.y0 + 76 + (float)tile * ( DSLOBBY_TILE_H + DSLOBBY_TILE_GAP );
	return DirectStrike_rect( x, y, x + DSLOBBY_TILE_W, y + DSLOBBY_TILE_H );
}

/// the colour list under the colour chip: cells of 48x40 in rows of four, 16 padding round them
inline DirectStrikeRect DirectStrikeLobby_colorList( int slot, int colors )
{
	const DirectStrikeRect chip = DirectStrikeLobby_part( slot, DSSEAT_COLOR );
	const int rows = ( colors + DSLOBBY_COLOR_COLUMNS - 1 ) / DSLOBBY_COLOR_COLUMNS;
	const float w = 2.0f * DSLOBBY_COLOR_PAD + DSLOBBY_COLOR_COLUMNS * DSLOBBY_COLOR_CELL_W;
	const float h = 2.0f * DSLOBBY_COLOR_PAD + (float)rows * DSLOBBY_COLOR_CELL_H;
	return DirectStrikeLobby_dropBelow( chip, w, h, DirectStrikeLobby_team( slot ) != 0 );
}

inline DirectStrikeRect DirectStrikeLobby_colorCell( int slot, int colors, int index )
{
	const DirectStrikeRect list = DirectStrikeLobby_colorList( slot, colors );
	const float x = list.x0 + DSLOBBY_COLOR_PAD + (float)( index % DSLOBBY_COLOR_COLUMNS ) * DSLOBBY_COLOR_CELL_W;
	const float y = list.y0 + DSLOBBY_COLOR_PAD + (float)( index / DSLOBBY_COLOR_COLUMNS ) * DSLOBBY_COLOR_CELL_H;
	return DirectStrike_rect( x, y, x + DSLOBBY_COLOR_CELL_W, y + DSLOBBY_COLOR_CELL_H );
}

#endif // __DIRECTSTRIKELAYOUT_H_
