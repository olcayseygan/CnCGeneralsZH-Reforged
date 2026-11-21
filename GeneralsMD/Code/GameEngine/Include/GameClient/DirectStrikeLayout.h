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
// by 1080k box centred.  The lobby's rects began as hud-artist's README-lobby.md of 2026-11-21 and were
// made denser the same day (shorter cards, slim open seats, one block centred down the screen); the
// in-match HUD keeps its own anchoring in LaneHudLayout.h.  DirectStrikeTheme.h is what draws in them.
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

	// the block, 2026-11-21 second round: title, then team heads and three cards a team beside the centre
	// column, the whole centred down the screen; an open or closed seat is a slim row
	DSLOBBY_CARD_W = 600,
	DSLOBBY_CARD_H = 148,				///< a seat with a player: the 128 well and two rows of chips
	DSLOBBY_SLIM_H = 72,				///< an open or closed seat: its line and a small mark
	DSLOBBY_CARD_GAP = 12,
	DSLOBBY_HEAD_H = 44,
	DSLOBBY_HEAD_GAP = 16,
	DSLOBBY_TITLE_H = 72,
	DSLOBBY_TITLE_GAP = 24,
	DSLOBBY_COLUMN_H = DSLOBBY_HEAD_H + DSLOBBY_HEAD_GAP + DSLOBBY_ROWS * DSLOBBY_CARD_H + ( DSLOBBY_ROWS - 1 ) * DSLOBBY_CARD_GAP,
	DSLOBBY_TITLE_TOP = ( 1080 - DSLOBBY_TITLE_H - DSLOBBY_TITLE_GAP - DSLOBBY_COLUMN_H ) / 2,
	DSLOBBY_TOP = DSLOBBY_TITLE_TOP + DSLOBBY_TITLE_H + DSLOBBY_TITLE_GAP,		///< the team heads' and the centre column's top
	DSLOBBY_BOTTOM = DSLOBBY_TOP + DSLOBBY_COLUMN_H,	///< a full column's foot and the start key's
	DSLOBBY_KEY_H = 72,
	DSLOBBY_ROW_GAP = 12,				///< between the centre column's parts
	DSLOBBY_SIZE_W = 136,				///< the team size chip between the back and start keys
	DSLOBBY_SIZES = 3,					///< its list: 1 v 1, 2 v 2, 3 v 3
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

inline DirectStrikeRect DirectStrikeLobby_title( void )
{
	return DirectStrike_rect( 700, DSLOBBY_TITLE_TOP, 1220, DSLOBBY_TITLE_TOP + DSLOBBY_TITLE_H );
}

inline DirectStrikeRect DirectStrikeLobby_teamHead( int team )
{
	const float x = team == 0 ? 80.0f : 1240.0f;
	return DirectStrike_rect( x, DSLOBBY_TOP, x + DSLOBBY_CARD_W, DSLOBBY_TOP + DSLOBBY_HEAD_H );
}

/// whether seat `slot` is a slim row; `slim` has bit n set for each open or closed seat n
inline bool DirectStrikeLobby_isSlim( int slot, unsigned slim ) { return ( slim >> slot & 1u ) != 0; }

/// a seat's card: the team's cards stack under its head, each a full card or a slim row
inline DirectStrikeRect DirectStrikeLobby_card( int slot, unsigned slim = 0 )
{
	const int team = DirectStrikeLobby_team( slot );
	float y = (float)( DSLOBBY_TOP + DSLOBBY_HEAD_H + DSLOBBY_HEAD_GAP );
	for( int row = 0; row < DirectStrikeLobby_row( slot ); ++row )
		y += ( DirectStrikeLobby_isSlim( DirectStrikeLobby_slot( team, row ), slim ) ? DSLOBBY_SLIM_H : DSLOBBY_CARD_H ) + DSLOBBY_CARD_GAP;
	const float x = team == 0 ? 80.0f : 1240.0f;
	return DirectStrike_rect( x, y, x + DSLOBBY_CARD_W, y + ( DirectStrikeLobby_isSlim( slot, slim ) ? DSLOBBY_SLIM_H : DSLOBBY_CARD_H ) );
}

/// a part of a seat's card in the design's space; team 2's card is team 1's mirrored, x becoming 600 - x.
/// `lights` is a network room, with a ready lamp and a ping at the inner edge; offline the line and the chips
/// run to that edge instead.  A slim row has its mark and its line; its other parts are empty rects.
inline DirectStrikeRect DirectStrikeLobby_part( int slot, DirectStrikeSeatPart part, unsigned slim = 0, bool lights = true )
{
	static const float FULL[ 2 ][ DSSEAT_PARTS ][ 4 ] =
	{
		{	// offline
			{ 10, 10, 138, 138 },			// portrait well
			{ 152, 18, 588, 66 },			// seat line
			{ 548, 26, 580, 58 },			// ready lamp, not drawn
			{ 152, 82, 480, 130 },		// army chip
			{ 492, 82, 588, 130 },		// colour chip
			{ 552, 94, 580, 118 },		// ping, not drawn
		},
		{	// a network room
			{ 10, 10, 138, 138 },
			{ 152, 18, 532, 66 },
			{ 548, 26, 580, 58 },
			{ 152, 82, 428, 130 },
			{ 440, 82, 536, 130 },
			{ 552, 94, 580, 118 },
		},
	};
	static const float SLIM[ DSSEAT_PARTS ][ 4 ] =
	{
		{ 10, 10, 62, 62 },
		{ 76, 12, 588, 60 },
		{ 0, 0, 0, 0 },
		{ 0, 0, 0, 0 },
		{ 0, 0, 0, 0 },
		{ 0, 0, 0, 0 },
	};
	const DirectStrikeRect card = DirectStrikeLobby_card( slot, slim );
	const float *p = DirectStrikeLobby_isSlim( slot, slim ) ? SLIM[ part ] : FULL[ lights ? 1 : 0 ][ part ];
	if( DirectStrikeLobby_team( slot ) == 0 )
		return DirectStrike_rect( card.x0 + p[ 0 ], card.y0 + p[ 1 ], card.x0 + p[ 2 ], card.y0 + p[ 3 ] );
	return DirectStrike_rect( card.x0 + DSLOBBY_CARD_W - p[ 2 ], card.y0 + p[ 1 ], card.x0 + DSLOBBY_CARD_W - p[ 0 ], card.y0 + p[ 3 ] );
}

/// the keys row at the centre column's foot, level with a full team column's: back, the team size chip, start
inline DirectStrikeRect DirectStrikeLobby_back( void ) { return DirectStrike_rect( 700, DSLOBBY_BOTTOM - DSLOBBY_KEY_H, 772, DSLOBBY_BOTTOM ); }
inline DirectStrikeRect DirectStrikeLobby_size( void )
{
	return DirectStrike_rect( 784, DSLOBBY_BOTTOM - DSLOBBY_KEY_H, 784 + DSLOBBY_SIZE_W, DSLOBBY_BOTTOM );
}
inline DirectStrikeRect DirectStrikeLobby_start( void )
{
	return DirectStrike_rect( 784 + DSLOBBY_SIZE_W + DSLOBBY_ROW_GAP, DSLOBBY_BOTTOM - DSLOBBY_KEY_H, 1220, DSLOBBY_BOTTOM );
}

// offline: the island panel, the picture and its caption, down to the keys row
inline DirectStrikeRect DirectStrikeLobby_islandPanel( void )
{
	return DirectStrike_rect( 700, DSLOBBY_TOP, 1220, DSLOBBY_BOTTOM - DSLOBBY_KEY_H - DSLOBBY_ROW_GAP );
}
inline DirectStrikeRect DirectStrikeLobby_island( void ) { return DirectStrike_rect( 780, DSLOBBY_TOP + 14, 1140, DSLOBBY_TOP + 374 ); }

// a network room: the island chip, the chat, its input and the send key
inline DirectStrikeRect DirectStrikeLobby_islandChip( void ) { return DirectStrike_rect( 700, DSLOBBY_TOP, 1220, DSLOBBY_TOP + 80 ); }
inline DirectStrikeRect DirectStrikeLobby_islandSmall( void ) { return DirectStrike_rect( 710, DSLOBBY_TOP + 10, 806, DSLOBBY_TOP + 70 ); }
inline DirectStrikeRect DirectStrikeLobby_chatInput( void )
{
	const float y1 = DSLOBBY_BOTTOM - DSLOBBY_KEY_H - DSLOBBY_ROW_GAP;
	return DirectStrike_rect( 700, y1 - 48, 1160, y1 );
}
inline DirectStrikeRect DirectStrikeLobby_send( void )
{
	const DirectStrikeRect input = DirectStrikeLobby_chatInput();
	return DirectStrike_rect( 1172, input.y0, 1220, input.y1 );
}
inline DirectStrikeRect DirectStrikeLobby_chat( void )
{
	return DirectStrike_rect( 700, DirectStrikeLobby_islandChip().y1 + DSLOBBY_ROW_GAP, 1220, DirectStrikeLobby_chatInput().y0 - DSLOBBY_ROW_GAP );
}

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
	DSLOBBY_PICKER_INNER = DSLOBBY_ARMY_COLUMNS * DSLOBBY_TILE_W + ( DSLOBBY_ARMY_COLUMNS - 1 ) * DSLOBBY_TILE_GAP,
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
inline DirectStrikeRect DirectStrikeLobby_seatList( int slot, unsigned slim = 0, bool lights = true )
{
	const DirectStrikeRect line = DirectStrikeLobby_part( slot, DSSEAT_LINE, slim, lights );
	return DirectStrikeLobby_dropBelow( line, line.x1 - line.x0, 2.0f * DSLOBBY_LIST_PAD + DSLOBBY_SEAT_ROWS * DSLOBBY_LIST_ROW, false );
}

inline DirectStrikeRect DirectStrikeLobby_seatRow( int slot, int row, unsigned slim = 0, bool lights = true )
{
	const DirectStrikeRect list = DirectStrikeLobby_seatList( slot, slim, lights );
	const float y = list.y0 + DSLOBBY_LIST_PAD + DSLOBBY_LIST_ROW * (float)row;
	return DirectStrike_rect( list.x0 + DSLOBBY_LIST_PAD, y, list.x1 - DSLOBBY_LIST_PAD, y + DSLOBBY_LIST_ROW );
}

/// the host's team size list under its chip, and its row for `size` - 1
inline DirectStrikeRect DirectStrikeLobby_sizeList( void )
{
	const DirectStrikeRect chip = DirectStrikeLobby_size();
	return DirectStrikeLobby_dropBelow( chip, chip.x1 - chip.x0, 2.0f * DSLOBBY_LIST_PAD + DSLOBBY_SIZES * DSLOBBY_LIST_ROW, false );
}

inline DirectStrikeRect DirectStrikeLobby_sizeRow( int row )
{
	const DirectStrikeRect list = DirectStrikeLobby_sizeList();
	const float y = list.y0 + DSLOBBY_LIST_PAD + DSLOBBY_LIST_ROW * (float)row;
	return DirectStrike_rect( list.x0 + DSLOBBY_LIST_PAD, y, list.x1 - DSLOBBY_LIST_PAD, y + DSLOBBY_LIST_ROW );
}

/// the army picker: team 1's right edge under its chip's right edge, team 2's left under the left; the last
/// row opens up.  One that fits neither under nor over its chip covers the centre column's width level with
/// the chip, over the island and the chat, its tiles centred in it.
inline DirectStrikeRect DirectStrikeLobby_picker( int slot, unsigned slim = 0, bool lights = true )
{
	const DirectStrikeRect chip = DirectStrikeLobby_part( slot, DSSEAT_ARMY, slim, lights );
	const bool left = DirectStrikeLobby_team( slot ) == 0;
	const float x0 = left ? chip.x1 - DSLOBBY_PICKER_W : chip.x0;
	const float above = chip.y0 - DSLOBBY_LIST_GAP - DSLOBBY_PICKER_H;
	if( DirectStrikeLobby_row( slot ) == DSLOBBY_ROWS - 1 )
		return DirectStrike_rect( x0, above, x0 + DSLOBBY_PICKER_W, chip.y0 - DSLOBBY_LIST_GAP );
	if( chip.y1 + DSLOBBY_LIST_GAP + DSLOBBY_PICKER_H > 1080.0f && above < 0.0f )
	{
		const float y0 = ( chip.y0 + chip.y1 - DSLOBBY_PICKER_H ) * 0.5f;
		return DirectStrike_rect( 700, y0, 1220, y0 + DSLOBBY_PICKER_H );
	}
	return DirectStrikeLobby_dropBelow( chip, DSLOBBY_PICKER_W, DSLOBBY_PICKER_H, left );
}

/// where the picker's random key and tiles start across: centred in whatever width the picker has
inline float DirectStrikeLobby_pickerInset( const DirectStrikeRect &picker )
{
	return ( picker.x1 - picker.x0 - DSLOBBY_PICKER_INNER ) * 0.5f;
}

inline DirectStrikeRect DirectStrikeLobby_pickerRandom( int slot, unsigned slim = 0, bool lights = true )
{
	const DirectStrikeRect p = DirectStrikeLobby_picker( slot, slim, lights );
	const float x = p.x0 + DirectStrikeLobby_pickerInset( p );
	return DirectStrike_rect( x, p.y0 + 16, x + DSLOBBY_PICKER_INNER, p.y0 + 16 + 48 );
}

/// tile `tile` (0 the side, 1 to 3 its generals) of column `column` (USA, China, GLA)
inline DirectStrikeRect DirectStrikeLobby_pickerTile( int slot, int column, int tile, unsigned slim = 0, bool lights = true )
{
	const DirectStrikeRect p = DirectStrikeLobby_picker( slot, slim, lights );
	const float x = p.x0 + DirectStrikeLobby_pickerInset( p ) + (float)column * ( DSLOBBY_TILE_W + DSLOBBY_TILE_GAP );
	const float y = p.y0 + 76 + (float)tile * ( DSLOBBY_TILE_H + DSLOBBY_TILE_GAP );
	return DirectStrike_rect( x, y, x + DSLOBBY_TILE_W, y + DSLOBBY_TILE_H );
}

/// the colour list under the colour chip: cells of 48x40 in rows of four, 16 padding round them
inline DirectStrikeRect DirectStrikeLobby_colorList( int slot, int colors, unsigned slim = 0, bool lights = true )
{
	const DirectStrikeRect chip = DirectStrikeLobby_part( slot, DSSEAT_COLOR, slim, lights );
	const int rows = ( colors + DSLOBBY_COLOR_COLUMNS - 1 ) / DSLOBBY_COLOR_COLUMNS;
	const float w = 2.0f * DSLOBBY_COLOR_PAD + DSLOBBY_COLOR_COLUMNS * DSLOBBY_COLOR_CELL_W;
	const float h = 2.0f * DSLOBBY_COLOR_PAD + (float)rows * DSLOBBY_COLOR_CELL_H;
	return DirectStrikeLobby_dropBelow( chip, w, h, DirectStrikeLobby_team( slot ) != 0 );
}

inline DirectStrikeRect DirectStrikeLobby_colorCell( int slot, int colors, int index, unsigned slim = 0, bool lights = true )
{
	const DirectStrikeRect list = DirectStrikeLobby_colorList( slot, colors, slim, lights );
	const float x = list.x0 + DSLOBBY_COLOR_PAD + (float)( index % DSLOBBY_COLOR_COLUMNS ) * DSLOBBY_COLOR_CELL_W;
	const float y = list.y0 + DSLOBBY_COLOR_PAD + (float)( index / DSLOBBY_COLOR_COLUMNS ) * DSLOBBY_COLOR_CELL_H;
	return DirectStrike_rect( x, y, x + DSLOBBY_COLOR_CELL_W, y + DSLOBBY_COLOR_CELL_H );
}

#endif // __DIRECTSTRIKELAYOUT_H_
