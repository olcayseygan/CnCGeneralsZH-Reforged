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
// by 1080k box centred.  The lobby's rects began as hud-artist's README-lobby.md of 2026-11-21 and are the
// Spearhead console of 2026-11-23 now (the owner's direction C, docs/reference/DIRECT-STRIKE-THEME.md): one
// console, every plate pointing at the lane, every seat a full card, the centre column square on the seat
// rows; the in-match HUD keeps its own anchoring in LaneHudLayout.h.  DirectStrikeTheme.h is what draws in
// them.
//
// A seat is a place on the console and the start the player stands on: seat n is start n, team 1 the even
// seats on the left and team 2 the odd ones on the right (LaneWar_teamOfStart), row n / 2 down.  A seat is
// not a slot: a player keeps his slot and moves by his start, so DirectStrikeLobby_seats says which slot
// shows at which seat.
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
// The lobby, the Spearhead console
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

	// the console: the title over its top rim, the team heads and three cards a team beside the centre column,
	// the tug bar under them, the whole centred down the screen.  An open or closed seat is a full card too
	// (a bay), so both columns always end at DSLOBBY_BOTTOM.
	DSLOBBY_CARD_W = 600,
	DSLOBBY_CARD_H = 148,				///< the 128 well and two rows of chips; the seat plates are drawn at this height
	DSLOBBY_CARD_GAP = 12,
	DSLOBBY_HEAD_H = 44,
	DSLOBBY_HEAD_GAP = 16,
	DSLOBBY_TITLE_H = 72,
	DSLOBBY_TITLE_GAP = 24,
	DSLOBBY_TUG_GAP = 24,				///< between the columns' foot and the tug bar
	DSLOBBY_TUG_H = 30,
	DSLOBBY_CONSOLE_FOOT = 26,	///< the console's floor and rim under the tug bar
	DSLOBBY_COLUMN_H = DSLOBBY_HEAD_H + DSLOBBY_HEAD_GAP + DSLOBBY_ROWS * DSLOBBY_CARD_H + ( DSLOBBY_ROWS - 1 ) * DSLOBBY_CARD_GAP,
	DSLOBBY_TITLE_TOP = ( 1080 - DSLOBBY_TITLE_H - DSLOBBY_TITLE_GAP - DSLOBBY_COLUMN_H - DSLOBBY_TUG_GAP - DSLOBBY_TUG_H
		- DSLOBBY_CONSOLE_FOOT ) / 2,
	DSLOBBY_TOP = DSLOBBY_TITLE_TOP + DSLOBBY_TITLE_H + DSLOBBY_TITLE_GAP,		///< the team heads' and the centre column's top
	DSLOBBY_BOTTOM = DSLOBBY_TOP + DSLOBBY_COLUMN_H,	///< a column's foot and the start key's
	DSLOBBY_TEAM_X0 = 64,				///< team 1's column; 36 clear of the centre column, which a seat's point needs
	DSLOBBY_TEAM_X1 = 1256,			///< team 2's
	DSLOBBY_SPEAR = 22,					///< a seat plate's point past its card on the inner side
	DSLOBBY_HEAD_SPEAR = 18,		///< a team head's point past its rect
	DSLOBBY_LIP_SEGS = 27,			///< a seat's lip segments, 12 x 6 every 16 from 152 in, 4 down
	DSLOBBY_LIP_X = 152,
	DSLOBBY_LIP_STEP = 16,
	DSLOBBY_LIP_Y = 4,
	DSLOBBY_SEG_W = 12,
	DSLOBBY_SEG_H = 6,
	DSLOBBY_HEAD_LEDS = 3,			///< a head's readiness lights, 16 x 10 every 22 from 468 in, 17 down
	DSLOBBY_HEAD_CHEVRONS = 3,	///< and its chevrons nearer the point, 14 x 18 every 14 from 542 in, 13 down
	DSLOBBY_TUG_SEGS = 47,			///< a team's tug segments, 14 x 18 every 18 from 96 in from its end
	DSLOBBY_TUG_SEG_X = 96,
	DSLOBBY_TUG_SEG_STEP = 18,
	DSLOBBY_TUG_SEG_W = 14,
	DSLOBBY_TUG_SEG_H = 18,
	DSLOBBY_KEY_H = 72,
	DSLOBBY_ROW_GAP = 12,				///< between the centre column's parts
	DSLOBBY_SIZE_W = 112,				///< the team size chip between the back key and the speed chip
	DSLOBBY_SIZES = 3,					///< its list: 1 v 1, 2 v 2, 3 v 3
	DSLOBBY_SPEED_W = 112,			///< the game speed chip between the team size and start (2026-11-23)
	DSLOBBY_SPEEDS = 4,					///< its list: PLAYER_GAME_SPEED_PERCENTS, 1.0x to 2.0x
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
inline int DirectStrikeLobby_seat( int team, int row ) { return row * DSLOBBY_TEAMS + team; }
inline int DirectStrikeLobby_team( int seat ) { return seat % DSLOBBY_TEAMS; }
inline int DirectStrikeLobby_row( int seat ) { return seat / DSLOBBY_TEAMS; }

/** Which slot shows at which seat.  start[ slot ] is an occupied slot's start, -1 for an empty slot (open or
	* closed) and for a player who has none yet.  An occupied slot stands on its start when that is a seat no slot
	* before it claimed; every other slot stands on its own number when that seat is still free, and the rest
	* take the seats left, in slot order.  A player who moves leaves the empty slot he took the seat from on the
	* seat he left, and a slot that fills (a joiner, an AI) stays where it was shown, so FitDirectStrikeRoom
	* gives it that seat as its start and the picture does not move.  Every machine reads the same answer from
	* the slot list. */
inline void DirectStrikeLobby_seats( const int start[ DSLOBBY_SEATS ], int seatOf[ DSLOBBY_SEATS ], int slotAt[ DSLOBBY_SEATS ] )
{
	bool taken[ DSLOBBY_SEATS ];
	for( int i = 0; i < DSLOBBY_SEATS; ++i )
	{
		taken[ i ] = false;
		seatOf[ i ] = slotAt[ i ] = -1;
	}
	for( int slot = 0; slot < DSLOBBY_SEATS; ++slot )
	{
		const int seat = start[ slot ];
		if( seat >= 0 && seat < DSLOBBY_SEATS && !taken[ seat ] )
		{
			seatOf[ slot ] = seat;
			taken[ seat ] = true;
		}
	}
	for( int slot = 0; slot < DSLOBBY_SEATS; ++slot )
		if( seatOf[ slot ] < 0 && !taken[ slot ] )
		{
			seatOf[ slot ] = slot;
			taken[ slot ] = true;
		}
	int next = 0;
	for( int slot = 0; slot < DSLOBBY_SEATS; ++slot )
		if( seatOf[ slot ] < 0 )
		{
			while( taken[ next ] )
				++next;
			seatOf[ slot ] = next;
			taken[ next ] = true;
		}
	for( int slot = 0; slot < DSLOBBY_SEATS; ++slot )
		slotAt[ seatOf[ slot ] ] = slot;
}

/// a rect given from the left of `box` for team 1, mirrored about the box for team 2 (x becoming its width - x)
inline DirectStrikeRect DirectStrikeLobby_mirrored( const DirectStrikeRect &box, int team, float x0, float y0, float x1, float y1 )
{
	if( team == 0 )
		return DirectStrike_rect( box.x0 + x0, box.y0 + y0, box.x0 + x1, box.y0 + y1 );
	return DirectStrike_rect( box.x1 - x1, box.y0 + y0, box.x1 - x0, box.y0 + y1 );
}

/// a rect grown by `by` on its team's inner side, toward the lane: the pointed plate drawn behind it
inline DirectStrikeRect DirectStrikeLobby_pointed( const DirectStrikeRect &r, int team, float by )
{
	return team == 0 ? DirectStrike_rect( r.x0, r.y0, r.x1 + by, r.y1 ) : DirectStrike_rect( r.x0 - by, r.y0, r.x1, r.y1 );
}

/// the console behind everything, from the title's middle to under the tug bar
inline DirectStrikeRect DirectStrikeLobby_console( void )
{
	return DirectStrike_rect( 24, DSLOBBY_TITLE_TOP + DSLOBBY_TITLE_H / 2, 1896,
		DSLOBBY_BOTTOM + DSLOBBY_TUG_GAP + DSLOBBY_TUG_H + DSLOBBY_CONSOLE_FOOT );
}

inline DirectStrikeRect DirectStrikeLobby_title( void )
{
	return DirectStrike_rect( 700, DSLOBBY_TITLE_TOP, 1220, DSLOBBY_TITLE_TOP + DSLOBBY_TITLE_H );
}

/// the title's chevrons, 14 x 18 on its middle: team 1's two pointing right at its left, team 2's at its right
inline DirectStrikeRect DirectStrikeLobby_titleChevron( int team, int i )
{
	const float x = team == 0 ? 726.0f + 14.0f * (float)i : 1920.0f - 726.0f - 14.0f * (float)i - 14.0f;
	const float y = DSLOBBY_TITLE_TOP + DSLOBBY_TITLE_H / 2 - 9.0f;
	return DirectStrike_rect( x, y, x + 14.0f, y + 18.0f );
}

inline float DirectStrikeLobby_teamX( int team ) { return team == 0 ? (float)DSLOBBY_TEAM_X0 : (float)DSLOBBY_TEAM_X1; }

inline DirectStrikeRect DirectStrikeLobby_teamHead( int team )
{
	const float x = DirectStrikeLobby_teamX( team );
	return DirectStrike_rect( x, DSLOBBY_TOP, x + DSLOBBY_CARD_W, DSLOBBY_TOP + DSLOBBY_HEAD_H );
}

/// a head's readiness light `i`, counted from its outer end, and its chevron `i`
inline DirectStrikeRect DirectStrikeLobby_headLed( int team, int i )
{
	const float x = 468.0f + 22.0f * (float)i;
	return DirectStrikeLobby_mirrored( DirectStrikeLobby_teamHead( team ), team, x, 17, x + 16, 27 );
}
inline DirectStrikeRect DirectStrikeLobby_headChevron( int team, int i )
{
	const float x = 542.0f + 14.0f * (float)i;
	return DirectStrikeLobby_mirrored( DirectStrikeLobby_teamHead( team ), team, x, 13, x + 14, 31 );
}

/// a seat's card: the team's three stack under its head
inline DirectStrikeRect DirectStrikeLobby_card( int seat )
{
	const float x = DirectStrikeLobby_teamX( DirectStrikeLobby_team( seat ) );
	const float y = (float)( DSLOBBY_TOP + DSLOBBY_HEAD_H + DSLOBBY_HEAD_GAP + DirectStrikeLobby_row( seat ) * ( DSLOBBY_CARD_H + DSLOBBY_CARD_GAP ) );
	return DirectStrike_rect( x, y, x + DSLOBBY_CARD_W, y + DSLOBBY_CARD_H );
}

/// a lip segment of a seat, `k` from the portrait's side
inline DirectStrikeRect DirectStrikeLobby_lipSeg( int seat, int k )
{
	const float x = (float)( DSLOBBY_LIP_X + DSLOBBY_LIP_STEP * k );
	return DirectStrikeLobby_mirrored( DirectStrikeLobby_card( seat ), DirectStrikeLobby_team( seat ), x, DSLOBBY_LIP_Y, x + DSLOBBY_SEG_W,
		DSLOBBY_LIP_Y + DSLOBBY_SEG_H );
}

/// a network seat's lights strip, the well round its ready lamp and ping
inline DirectStrikeRect DirectStrikeLobby_strip( int seat )
{
	return DirectStrikeLobby_mirrored( DirectStrikeLobby_card( seat ), DirectStrikeLobby_team( seat ), 542, 14, 588, 134 );
}

/// a part of a seat's card in the design's space; team 2's card is team 1's mirrored, x becoming 600 - x.
/// `lights` is a network room, with a ready lamp and a ping at the inner edge; offline the line and the chips
/// run to that edge instead.
inline DirectStrikeRect DirectStrikeLobby_part( int seat, DirectStrikeSeatPart part, bool lights = true )
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
	const float *p = FULL[ lights ? 1 : 0 ][ part ];
	return DirectStrikeLobby_mirrored( DirectStrikeLobby_card( seat ), DirectStrikeLobby_team( seat ), p[ 0 ], p[ 1 ], p[ 2 ], p[ 3 ] );
}

/// the tug bar along the console's foot, and a team's segment `i` from its own end
inline DirectStrikeRect DirectStrikeLobby_tug( void )
{
	const float y = (float)( DSLOBBY_BOTTOM + DSLOBBY_TUG_GAP );
	return DirectStrike_rect( 80, y, 1840, y + DSLOBBY_TUG_H );
}
inline DirectStrikeRect DirectStrikeLobby_tugSeg( int team, int i )
{
	const float x = team == 0 ? (float)( DSLOBBY_TUG_SEG_X + DSLOBBY_TUG_SEG_STEP * i )
		: 1920.0f - (float)( DSLOBBY_TUG_SEG_X + DSLOBBY_TUG_SEG_STEP * i + DSLOBBY_TUG_SEG_W );
	const float y = DirectStrikeLobby_tug().y0 + ( DSLOBBY_TUG_H - DSLOBBY_TUG_SEG_H ) / 2;
	return DirectStrike_rect( x, y, x + DSLOBBY_TUG_SEG_W, y + DSLOBBY_TUG_SEG_H );
}

/// the keys row at the centre column's foot, level with a full team column's: back, the team size chip, the
/// game speed chip, start
inline DirectStrikeRect DirectStrikeLobby_back( void ) { return DirectStrike_rect( 700, DSLOBBY_BOTTOM - DSLOBBY_KEY_H, 772, DSLOBBY_BOTTOM ); }
inline DirectStrikeRect DirectStrikeLobby_size( void )
{
	return DirectStrike_rect( 784, DSLOBBY_BOTTOM - DSLOBBY_KEY_H, 784 + DSLOBBY_SIZE_W, DSLOBBY_BOTTOM );
}
inline DirectStrikeRect DirectStrikeLobby_speed( void )
{
	const float x = 784 + DSLOBBY_SIZE_W + DSLOBBY_ROW_GAP;
	return DirectStrike_rect( x, DSLOBBY_BOTTOM - DSLOBBY_KEY_H, x + DSLOBBY_SPEED_W, DSLOBBY_BOTTOM );
}
inline DirectStrikeRect DirectStrikeLobby_start( void )
{
	return DirectStrike_rect( DirectStrikeLobby_speed().x1 + DSLOBBY_ROW_GAP, DSLOBBY_BOTTOM - DSLOBBY_KEY_H, 1220, DSLOBBY_BOTTOM );
}

// The centre column, square on the seat rows: the map band on the head row, the chat over the first and second
// seat rows, its input and send key on the top of the third, the keys at its foot

// a network room: the input and the send key level with the third row's top, 48 high
inline DirectStrikeRect DirectStrikeLobby_chatInput( void )
{
	const float y0 = DirectStrikeLobby_card( DirectStrikeLobby_seat( 0, 2 ) ).y0;
	return DirectStrike_rect( 700, y0, 1160, y0 + 48 );
}
inline DirectStrikeRect DirectStrikeLobby_send( void )
{
	const DirectStrikeRect input = DirectStrikeLobby_chatInput();
	return DirectStrike_rect( 1172, input.y0, 1220, input.y1 );
}

// offline: the island panel, the head row down to where the input's foot would be, the picture and its caption
inline DirectStrikeRect DirectStrikeLobby_islandPanel( void )
{
	return DirectStrike_rect( 700, DSLOBBY_TOP, 1220, DirectStrikeLobby_chatInput().y1 );
}
inline DirectStrikeRect DirectStrikeLobby_island( void ) { return DirectStrike_rect( 780, DSLOBBY_TOP + 14, 1140, DSLOBBY_TOP + 374 ); }

// a network room: the map band on the head row, its island in a well, the chat over the first two seat rows
inline DirectStrikeRect DirectStrikeLobby_islandChip( void ) { return DirectStrike_rect( 700, DSLOBBY_TOP, 1220, DSLOBBY_TOP + DSLOBBY_HEAD_H ); }
inline DirectStrikeRect DirectStrikeLobby_islandWell( void ) { return DirectStrike_rect( 708, DSLOBBY_TOP + 6, 784, DSLOBBY_TOP + 38 ); }
inline DirectStrikeRect DirectStrikeLobby_islandSmall( void ) { return DirectStrike_rect( 710, DSLOBBY_TOP + 7, 782, DSLOBBY_TOP + 37 ); }
inline DirectStrikeRect DirectStrikeLobby_chat( void )
{
	return DirectStrike_rect( 700, DirectStrikeLobby_card( DirectStrikeLobby_seat( 0, 0 ) ).y0, 1220,
		DirectStrikeLobby_card( DirectStrikeLobby_seat( 0, 1 ) ).y1 );
}
/// the chat box's glass, the log clipped to it: 11 lines of DSLOBBY_CHAT_LINE
inline DirectStrikeRect DirectStrikeLobby_chatGlass( void )
{
	const DirectStrikeRect chat = DirectStrikeLobby_chat();
	return DirectStrike_rect( chat.x0 + 12, chat.y0 + 6, chat.x1 - 12, chat.y1 - 6 );
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
inline DirectStrikeRect DirectStrikeLobby_seatList( int seat, bool lights = true )
{
	const DirectStrikeRect line = DirectStrikeLobby_part( seat, DSSEAT_LINE, lights );
	return DirectStrikeLobby_dropBelow( line, line.x1 - line.x0, 2.0f * DSLOBBY_LIST_PAD + DSLOBBY_SEAT_ROWS * DSLOBBY_LIST_ROW, false );
}

inline DirectStrikeRect DirectStrikeLobby_seatRow( int seat, int row, bool lights = true )
{
	const DirectStrikeRect list = DirectStrikeLobby_seatList( seat, lights );
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

/// the host's game speed list under its chip, and its row for PLAYER_GAME_SPEED_PERCENTS[ row ]
inline DirectStrikeRect DirectStrikeLobby_speedList( void )
{
	const DirectStrikeRect chip = DirectStrikeLobby_speed();
	return DirectStrikeLobby_dropBelow( chip, chip.x1 - chip.x0, 2.0f * DSLOBBY_LIST_PAD + DSLOBBY_SPEEDS * DSLOBBY_LIST_ROW, false );
}

inline DirectStrikeRect DirectStrikeLobby_speedRow( int row )
{
	const DirectStrikeRect list = DirectStrikeLobby_speedList();
	const float y = list.y0 + DSLOBBY_LIST_PAD + DSLOBBY_LIST_ROW * (float)row;
	return DirectStrike_rect( list.x0 + DSLOBBY_LIST_PAD, y, list.x1 - DSLOBBY_LIST_PAD, y + DSLOBBY_LIST_ROW );
}

/// the army picker: team 1's right edge under its chip's right edge, team 2's left under the left; the last
/// row opens up.  One that fits neither under nor over its chip covers the centre column's width level with
/// the chip, over the island and the chat, its tiles centred in it.
inline DirectStrikeRect DirectStrikeLobby_picker( int seat, bool lights = true )
{
	const DirectStrikeRect chip = DirectStrikeLobby_part( seat, DSSEAT_ARMY, lights );
	const bool left = DirectStrikeLobby_team( seat ) == 0;
	const float x0 = left ? chip.x1 - DSLOBBY_PICKER_W : chip.x0;
	const float above = chip.y0 - DSLOBBY_LIST_GAP - DSLOBBY_PICKER_H;
	if( DirectStrikeLobby_row( seat ) == DSLOBBY_ROWS - 1 )
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

inline DirectStrikeRect DirectStrikeLobby_pickerRandom( int seat, bool lights = true )
{
	const DirectStrikeRect p = DirectStrikeLobby_picker( seat, lights );
	const float x = p.x0 + DirectStrikeLobby_pickerInset( p );
	return DirectStrike_rect( x, p.y0 + 16, x + DSLOBBY_PICKER_INNER, p.y0 + 16 + 48 );
}

/// tile `tile` (0 the side, 1 to 3 its generals) of column `column` (USA, China, GLA)
inline DirectStrikeRect DirectStrikeLobby_pickerTile( int seat, int column, int tile, bool lights = true )
{
	const DirectStrikeRect p = DirectStrikeLobby_picker( seat, lights );
	const float x = p.x0 + DirectStrikeLobby_pickerInset( p ) + (float)column * ( DSLOBBY_TILE_W + DSLOBBY_TILE_GAP );
	const float y = p.y0 + 76 + (float)tile * ( DSLOBBY_TILE_H + DSLOBBY_TILE_GAP );
	return DirectStrike_rect( x, y, x + DSLOBBY_TILE_W, y + DSLOBBY_TILE_H );
}

/// the colour list under the colour chip: cells of 48x40 in rows of four, 16 padding round them
inline DirectStrikeRect DirectStrikeLobby_colorList( int seat, int colors, bool lights = true )
{
	const DirectStrikeRect chip = DirectStrikeLobby_part( seat, DSSEAT_COLOR, lights );
	const int rows = ( colors + DSLOBBY_COLOR_COLUMNS - 1 ) / DSLOBBY_COLOR_COLUMNS;
	const float w = 2.0f * DSLOBBY_COLOR_PAD + DSLOBBY_COLOR_COLUMNS * DSLOBBY_COLOR_CELL_W;
	const float h = 2.0f * DSLOBBY_COLOR_PAD + (float)rows * DSLOBBY_COLOR_CELL_H;
	return DirectStrikeLobby_dropBelow( chip, w, h, DirectStrikeLobby_team( seat ) != 0 );
}

inline DirectStrikeRect DirectStrikeLobby_colorCell( int seat, int colors, int index, bool lights = true )
{
	const DirectStrikeRect list = DirectStrikeLobby_colorList( seat, colors, lights );
	const float x = list.x0 + DSLOBBY_COLOR_PAD + (float)( index % DSLOBBY_COLOR_COLUMNS ) * DSLOBBY_COLOR_CELL_W;
	const float y = list.y0 + DSLOBBY_COLOR_PAD + (float)( index / DSLOBBY_COLOR_COLUMNS ) * DSLOBBY_COLOR_CELL_H;
	return DirectStrike_rect( x, y, x + DSLOBBY_COLOR_CELL_W, y + DSLOBBY_COLOR_CELL_H );
}

#endif // __DIRECTSTRIKELAYOUT_H_
