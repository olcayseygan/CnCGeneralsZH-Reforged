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
// Where the Direct Strike screens stand, in plain integers so test_directstrikehud checks them alone.  A screen
// is authored at 1920x1080 and put on any screen by the frame: k = min(W / 1920, H / 1080), the 1920k
// by 1080k box centred.  The lobby's rects are the menu language's of 2026-11-25 (the wrapper's
// experiments/proofs/2026-11-24/lobbies/direct-strike/README-ds-lobby.md, ds_lobby_menu2.py): frame A's
// panel, the two teams' wings of three seat cards, the island, chat and START in the centre column and the
// readiness lane along the foot; the in-match HUD keeps its own anchoring in DirectStrikeHudLayout.h.
//
// A seat is a place on the screen and the start the player stands on: seat n is start n, team 1 the even
// seats on the left and team 2 the odd ones on the right (DirectStrike_teamOfStart), row n / 2 down.  A seat is
// not a slot: a player keeps his slot and moves by his start, so DirectStrikeLobby_seats says which slot
// shows at which seat.
//
///////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#ifndef __DIRECTSTRIKELAYOUT_H_
#define __DIRECTSTRIKELAYOUT_H_

#include "GameClient/DirectStrikeHudLayout.h"

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
inline DirectStrikeHudBox DirectStrike_box( const DirectStrikeFrame &f, const DirectStrikeRect &r )
{
	const int lx = f.ox + (int)( r.x0 * f.k + 0.5f ), ly = f.oy + (int)( r.y0 * f.k + 0.5f );
	const int hx = f.ox + (int)( r.x1 * f.k + 0.5f ), hy = f.oy + (int)( r.y1 * f.k + 0.5f );
	return DirectStrikeHud_box( lx, ly, hx - lx, hy - ly );
}

/// a screen point back in the design's space
inline void DirectStrike_toDesign( const DirectStrikeFrame &f, int x, int y, float *dx, float *dy )
{
	*dx = ( (float)( x - f.ox ) ) / f.k;
	*dy = ( (float)( y - f.oy ) ) / f.k;
}

//-------------------------------------------------------------------------------------------------
// The lobby, in the menu language
//-------------------------------------------------------------------------------------------------

enum
{
	DSLOBBY_TEAMS = 2,
	DSLOBBY_ROWS = 3,						///< seats a team
	DSLOBBY_SEATS = DSLOBBY_TEAMS * DSLOBBY_ROWS,
	DSLOBBY_SEAT_ROWS = 6,			///< the host's seat control: Open, Closed and four levels
	DSLOBBY_HUMAN_SEAT_ROWS = 2,	///< on a human's seat only Open and Closed (the kick)
	DSLOBBY_LEVELS = 4,					///< the computer levels an open seat's keys add: Easy, Medium, Hard, Brutal
	DSLOBBY_ARMY_COLUMNS = 3,
	DSLOBBY_ARMY_TILES = 4,			///< down a column: the side, then its three generals
	DSLOBBY_COLOR_COLUMNS = 4,
	DSLOBBY_SIZES = 3,					///< the team size list: 1 v 1, 2 v 2, 3 v 3
	DSLOBBY_SPEEDS = 4,					///< the speed list: PLAYER_GAME_SPEED_PERCENTS, 1.0x to 2.0x

	// frame A's panel and the end screens' content inset
	DSLOBBY_PANEL_X0 = 32,
	DSLOBBY_PANEL_Y0 = 28,
	DSLOBBY_PANEL_X1 = 1888,
	DSLOBBY_PANEL_Y1 = 1052,
	DSLOBBY_X0 = 60,
	DSLOBBY_X1 = 1860,
	DSLOBBY_TITLE_Y0 = 46,
	DSLOBBY_TITLE_Y1 = 94,
	DSLOBBY_RULE_Y = 112,				///< the header rule, 2 high; each team's ready share under it
	DSLOBBY_HEAD_Y = 128,
	DSLOBBY_HEAD_H = 44,
	DSLOBBY_SEAT_Y = 188,
	DSLOBBY_CARD_W = 550,
	DSLOBBY_CARD_H = 250,
	DSLOBBY_CARD_GAP = 12,
	DSLOBBY_TEAM_X0 = 60,				///< team 1's wing
	DSLOBBY_TEAM_X1 = 1310,			///< team 2's, laid out like team 1's
	DSLOBBY_CENTRE_X0 = 640,
	DSLOBBY_CENTRE_X1 = 1280,
	DSLOBBY_FOOT = DSLOBBY_SEAT_Y + DSLOBBY_ROWS * DSLOBBY_CARD_H + ( DSLOBBY_ROWS - 1 ) * DSLOBBY_CARD_GAP,	///< 962: the seats' and START's foot
	DSLOBBY_PICTURE = 226,			///< a seat's picture, PAD in from its card's corner
	DSLOBBY_PAD = 12,
	DSLOBBY_LINE_H = 56,				///< the seat line, the army and the colour beside the picture
	DSLOBBY_SETUP_GAP = 16,			///< between the team size chip and the speed chip

	// the readiness lane along the panel's foot: the track and its ticks, the network status above its middle
	// (the design's box over the track hid team 2's third segment, so the status stands over it instead)
	DSLOBBY_LANE_Y0 = 976,
	DSLOBBY_LANE_Y1 = 1036,
	DSLOBBY_TRACK_X0 = 100,
	DSLOBBY_TRACK_X1 = 1820,
	DSLOBBY_TRACK_Y0 = 1000,
	DSLOBBY_TRACK_Y1 = 1020,
	DSLOBBY_TRACK_SEG_GAP = 6,
	DSLOBBY_STATUS_Y0 = 979,
	DSLOBBY_STATUS_Y1 = 997,

	DSLOBBY_CHAT_LINE = 30,			///< chat lines this far apart
	DSLOBBY_LIST_GAP = 4,				///< an open list stands this far from its chip
	DSLOBBY_LIST_ROW = 40,
	DSLOBBY_LIST_PAD = 8,
	DSLOBBY_PICKER_PAD = 12,
	DSLOBBY_PICKER_RANDOM_H = 44,
	DSLOBBY_TILE_GAP = 8,
	DSLOBBY_TILE_W = ( DSLOBBY_CARD_W - 2 * DSLOBBY_PICKER_PAD - ( DSLOBBY_ARMY_COLUMNS - 1 ) * DSLOBBY_TILE_GAP ) / DSLOBBY_ARMY_COLUMNS,
	DSLOBBY_TILE_H = 146,
	DSLOBBY_TILE_PICTURE = 104,
	DSLOBBY_PICKER_H = 2 * DSLOBBY_PICKER_PAD + DSLOBBY_PICKER_RANDOM_H + 10 + DSLOBBY_ARMY_TILES * DSLOBBY_TILE_H
		+ ( DSLOBBY_ARMY_TILES - 1 ) * DSLOBBY_TILE_GAP,
	DSLOBBY_COLOR_CELL_W = 64,
	DSLOBBY_COLOR_CELL_H = 48,
	DSLOBBY_COLOR_GAP = 8,
	DSLOBBY_COLOR_PAD = 12,
	DSLOBBY_COLOR_NAME_H = 30,	///< the hovered colour's name under the cells
	DSLOBBY_LEVEL_GAP = 6,			///< between an open seat's level keys
};

/// the part of a seat card, README-ds-lobby.md "The seat card"; every team's card is laid out alike
enum DirectStrikeSeatPart
{
	DSSEAT_PORTRAIT,
	DSSEAT_LINE,
	DSSEAT_ARMY,
	DSSEAT_COLOR,
	DSSEAT_TAG,		///< a network seat's readiness tag on the picture's top, its word right (team 1) or left (team 2) aligned
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

inline DirectStrikeRect DirectStrikeLobby_panel( void )
{
	return DirectStrike_rect( DSLOBBY_PANEL_X0, DSLOBBY_PANEL_Y0, DSLOBBY_PANEL_X1, DSLOBBY_PANEL_Y1 );
}

/// the header: the title at the top left, the facts after it, BACK at the right
inline DirectStrikeRect DirectStrikeLobby_title( void ) { return DirectStrike_rect( DSLOBBY_X0, DSLOBBY_TITLE_Y0, 520, DSLOBBY_TITLE_Y1 ); }
inline DirectStrikeRect DirectStrikeLobby_back( void ) { return DirectStrike_rect( DSLOBBY_X1 - 156, DSLOBBY_TITLE_Y0 + 2, DSLOBBY_X1, DSLOBBY_TITLE_Y1 - 2 ); }

inline float DirectStrikeLobby_teamX( int team ) { return team == 0 ? (float)DSLOBBY_TEAM_X0 : (float)DSLOBBY_TEAM_X1; }

inline DirectStrikeRect DirectStrikeLobby_teamHead( int team )
{
	const float x = DirectStrikeLobby_teamX( team );
	return DirectStrike_rect( x, DSLOBBY_HEAD_Y, x + DSLOBBY_CARD_W, DSLOBBY_HEAD_Y + DSLOBBY_HEAD_H );
}

/// a seat's card: the team's three stack under its head
inline DirectStrikeRect DirectStrikeLobby_card( int seat )
{
	const float x = DirectStrikeLobby_teamX( DirectStrikeLobby_team( seat ) );
	const float y = (float)( DSLOBBY_SEAT_Y + DirectStrikeLobby_row( seat ) * ( DSLOBBY_CARD_H + DSLOBBY_CARD_GAP ) );
	return DirectStrike_rect( x, y, x + DSLOBBY_CARD_W, y + DSLOBBY_CARD_H );
}

/// the three lines beside the picture, 29 apart: 0 the seat line, 1 the army, 2 the colour
inline DirectStrikeRect DirectStrikeLobby_line( int seat, int line )
{
	const DirectStrikeRect c = DirectStrikeLobby_card( seat );
	const float x0 = c.x0 + DSLOBBY_PAD + DSLOBBY_PICTURE + 18, x1 = c.x1 - DSLOBBY_PAD;
	const float y = c.y0 + DSLOBBY_PAD + (float)( line * ( ( DSLOBBY_PICTURE - DSLOBBY_LINE_H ) / 2 ) );
	return DirectStrike_rect( x0, y, x1, y + DSLOBBY_LINE_H );
}

/// a part of a seat's card in the design's space
inline DirectStrikeRect DirectStrikeLobby_part( int seat, DirectStrikeSeatPart part )
{
	const DirectStrikeRect c = DirectStrikeLobby_card( seat );
	const DirectStrikeRect picture = DirectStrike_rect( c.x0 + DSLOBBY_PAD, c.y0 + DSLOBBY_PAD, c.x0 + DSLOBBY_PAD + DSLOBBY_PICTURE,
		c.y0 + DSLOBBY_PAD + DSLOBBY_PICTURE );
	switch( part )
	{
		case DSSEAT_LINE: return DirectStrikeLobby_line( seat, 0 );
		case DSSEAT_ARMY: return DirectStrikeLobby_line( seat, 1 );
		case DSSEAT_COLOR: return DirectStrikeLobby_line( seat, 2 );
		case DSSEAT_TAG: return DirectStrike_rect( picture.x0 + 6, picture.y0 + 6, picture.x1 - 6, picture.y0 + 32 );
		default: return picture;
	}
}

/// a client's TAKE SEAT key on an open seat, over the army and colour lines
inline DirectStrikeRect DirectStrikeLobby_takeSeat( int seat )
{
	const DirectStrikeRect army = DirectStrikeLobby_line( seat, 1 ), color = DirectStrikeLobby_line( seat, 2 );
	return DirectStrike_rect( army.x0, army.y0 + 14, color.x1, color.y1 );
}

/** The host's level key `level` on an open or closed seat, in the colour line's place: each key its word's
	* width (words[ i ], in 1080 pixels) and the slack shared out, DSLOBBY_LEVEL_GAP between them. */
inline DirectStrikeRect DirectStrikeLobby_levelKey( int seat, int level, const float words[ DSLOBBY_LEVELS ] )
{
	const DirectStrikeRect row = DirectStrikeLobby_line( seat, 2 );
	float sum = 0;
	for( int i = 0; i < DSLOBBY_LEVELS; ++i )
		sum += words[ i ];
	const float slack = ( row.x1 - row.x0 - DSLOBBY_LEVEL_GAP * ( DSLOBBY_LEVELS - 1 ) - sum ) / DSLOBBY_LEVELS;
	float x = row.x0;
	for( int i = 0; i < level; ++i )
		x += words[ i ] + slack + DSLOBBY_LEVEL_GAP;
	return DirectStrike_rect( x, row.y0, x + words[ level ] + slack, row.y1 );
}

// The centre column: offline the island over the caption, the team size and speed chips and START; in a network
// room a shorter island, then the chat and its entry, the chips and START

inline DirectStrikeRect DirectStrikeLobby_island( bool network )
{
	return DirectStrike_rect( DSLOBBY_CENTRE_X0, DSLOBBY_HEAD_Y, DSLOBBY_CENTRE_X1, network ? 344.0f : 708.0f );
}
/// the island's key strip, its last 34
inline DirectStrikeRect DirectStrikeLobby_islandKey( bool network )
{
	const DirectStrikeRect island = DirectStrikeLobby_island( network );
	return DirectStrike_rect( island.x0, island.y1 - 34, island.x1, island.y1 );
}
inline DirectStrikeRect DirectStrikeLobby_caption( bool network )
{
	return network ? DirectStrike_rect( DSLOBBY_CENTRE_X0, 352, DSLOBBY_CENTRE_X1, 384 ) : DirectStrike_rect( DSLOBBY_CENTRE_X0, 720, DSLOBBY_CENTRE_X1, 756 );
}
/// the host's Mixed box at the caption's right end: its 20 square and its name, which ends at the column's edge
inline DirectStrikeRect DirectStrikeLobby_mixed( bool network )
{
	const DirectStrikeRect caption = DirectStrikeLobby_caption( network );
	return DirectStrike_rect( DSLOBBY_CENTRE_X1 - 240, caption.y0, DSLOBBY_CENTRE_X1, caption.y1 );
}
inline DirectStrikeRect DirectStrikeLobby_size( bool network )
{
	const float y0 = network ? 788.0f : 772.0f, y1 = network ? 836.0f : 824.0f;
	const float half = ( DSLOBBY_CENTRE_X1 - DSLOBBY_CENTRE_X0 - DSLOBBY_SETUP_GAP ) * 0.5f;
	return DirectStrike_rect( DSLOBBY_CENTRE_X0, y0, DSLOBBY_CENTRE_X0 + half, y1 );
}
inline DirectStrikeRect DirectStrikeLobby_speed( bool network )
{
	const DirectStrikeRect size = DirectStrikeLobby_size( network );
	return DirectStrike_rect( size.x1 + DSLOBBY_SETUP_GAP, size.y0, DSLOBBY_CENTRE_X1, size.y1 );
}
inline DirectStrikeRect DirectStrikeLobby_start( bool network )
{
	return DirectStrike_rect( DSLOBBY_CENTRE_X0, network ? 852.0f : 840.0f, DSLOBBY_CENTRE_X1, DSLOBBY_FOOT );
}
inline DirectStrikeRect DirectStrikeLobby_chat( void ) { return DirectStrike_rect( DSLOBBY_CENTRE_X0, 396, DSLOBBY_CENTRE_X1, 720 ); }
inline DirectStrikeRect DirectStrikeLobby_chatInput( void ) { return DirectStrike_rect( DSLOBBY_CENTRE_X0, 728, 1150, 774 ); }
inline DirectStrikeRect DirectStrikeLobby_send( void ) { return DirectStrike_rect( 1160, 728, DSLOBBY_CENTRE_X1, 774 ); }

/// the readiness lane, its track, and team `team`'s segment `i` of `size` from its own fortress toward the middle
inline DirectStrikeRect DirectStrikeLobby_lane( void ) { return DirectStrike_rect( DSLOBBY_X0, DSLOBBY_LANE_Y0, DSLOBBY_X1, DSLOBBY_LANE_Y1 ); }
inline DirectStrikeRect DirectStrikeLobby_track( void )
{
	return DirectStrike_rect( DSLOBBY_TRACK_X0, DSLOBBY_TRACK_Y0, DSLOBBY_TRACK_X1, DSLOBBY_TRACK_Y1 );
}
inline DirectStrikeRect DirectStrikeLobby_laneSeg( int team, int i, int size )
{
	const float mid = 0.5f * ( DSLOBBY_TRACK_X0 + DSLOBBY_TRACK_X1 );
	const float half = mid - DSLOBBY_TRACK_X0 - 10.0f;
	const float w = ( half - DSLOBBY_TRACK_SEG_GAP * (float)( size - 1 ) ) / (float)( size > 0 ? size : 1 );
	const float x = team == 0 ? DSLOBBY_TRACK_X0 + 4.0f + (float)i * ( w + DSLOBBY_TRACK_SEG_GAP )
		: DSLOBBY_TRACK_X1 - 4.0f - (float)( i + 1 ) * w - (float)i * DSLOBBY_TRACK_SEG_GAP;
	return DirectStrike_rect( x, DSLOBBY_TRACK_Y0 + 4, x + w, DSLOBBY_TRACK_Y1 - 4 );
}
/// a network room's status, centred over the track's middle
inline DirectStrikeRect DirectStrikeLobby_status( void )
{
	return DirectStrike_rect( DSLOBBY_CENTRE_X0, DSLOBBY_STATUS_Y0, DSLOBBY_CENTRE_X1, DSLOBBY_STATUS_Y1 );
}

/** An open list of height h at its chip: under it, 4 off, unless it would pass the panel's foot (or `up` asks),
	* then over it; one that fits neither way starts at the head row, over its own chip. */
inline DirectStrikeRect DirectStrikeLobby_place( const DirectStrikeRect &chip, float w, float h, bool up )
{
	if( !up && chip.y1 + DSLOBBY_LIST_GAP + h <= (float)( DSLOBBY_PANEL_Y1 - 8 ) )
		return DirectStrike_rect( chip.x0, chip.y1 + DSLOBBY_LIST_GAP, chip.x0 + w, chip.y1 + DSLOBBY_LIST_GAP + h );
	if( chip.y0 - DSLOBBY_LIST_GAP - h >= (float)DSLOBBY_HEAD_Y )
		return DirectStrike_rect( chip.x0, chip.y0 - DSLOBBY_LIST_GAP - h, chip.x0 + w, chip.y0 - DSLOBBY_LIST_GAP );
	return DirectStrike_rect( chip.x0, DSLOBBY_HEAD_Y, chip.x0 + w, DSLOBBY_HEAD_Y + h );
}

/// a list's row `row` of DSLOBBY_LIST_ROW, DSLOBBY_LIST_PAD in
inline DirectStrikeRect DirectStrikeLobby_listRow( const DirectStrikeRect &list, int row )
{
	const float y = list.y0 + DSLOBBY_LIST_PAD + DSLOBBY_LIST_ROW * (float)row;
	return DirectStrike_rect( list.x0 + DSLOBBY_LIST_PAD, y, list.x1 - DSLOBBY_LIST_PAD, y + DSLOBBY_LIST_ROW );
}
inline float DirectStrikeLobby_listHeight( int rows ) { return 2.0f * DSLOBBY_LIST_PAD + (float)rows * DSLOBBY_LIST_ROW; }

/// the host's seat control at a seat line, `rows` long (DSLOBBY_SEAT_ROWS, a human's DSLOBBY_HUMAN_SEAT_ROWS)
inline DirectStrikeRect DirectStrikeLobby_seatList( int seat, int rows = DSLOBBY_SEAT_ROWS )
{
	const DirectStrikeRect line = DirectStrikeLobby_part( seat, DSSEAT_LINE );
	return DirectStrikeLobby_place( line, line.x1 - line.x0, DirectStrikeLobby_listHeight( rows ), false );
}

/// the host's team size and speed lists, both over their chips
inline DirectStrikeRect DirectStrikeLobby_sizeList( bool network )
{
	const DirectStrikeRect chip = DirectStrikeLobby_size( network );
	return DirectStrikeLobby_place( chip, chip.x1 - chip.x0, DirectStrikeLobby_listHeight( DSLOBBY_SIZES ), true );
}
inline DirectStrikeRect DirectStrikeLobby_speedList( bool network )
{
	const DirectStrikeRect chip = DirectStrikeLobby_speed( network );
	return DirectStrikeLobby_place( chip, chip.x1 - chip.x0, DirectStrikeLobby_listHeight( DSLOBBY_SPEEDS ), true );
}

/// the army picker, the wing's width: down from the first row's army, over the last row's, from the head row for the middle
inline DirectStrikeRect DirectStrikeLobby_picker( int seat )
{
	const DirectStrikeRect chip = DirectStrikeLobby_part( seat, DSSEAT_ARMY );
	const float x0 = DirectStrikeLobby_teamX( DirectStrikeLobby_team( seat ) );
	return DirectStrikeLobby_place( DirectStrike_rect( x0, chip.y0, x0 + DSLOBBY_CARD_W, chip.y1 ), DSLOBBY_CARD_W, DSLOBBY_PICKER_H,
		DirectStrikeLobby_row( seat ) == DSLOBBY_ROWS - 1 );
}
inline DirectStrikeRect DirectStrikeLobby_pickerRandom( int seat )
{
	const DirectStrikeRect p = DirectStrikeLobby_picker( seat );
	return DirectStrike_rect( p.x0 + DSLOBBY_PICKER_PAD, p.y0 + DSLOBBY_PICKER_PAD, p.x1 - DSLOBBY_PICKER_PAD,
		p.y0 + DSLOBBY_PICKER_PAD + DSLOBBY_PICKER_RANDOM_H );
}
/// tile `tile` (0 the side, 1 to 3 its generals) of column `column` (USA, China, GLA)
inline DirectStrikeRect DirectStrikeLobby_pickerTile( int seat, int column, int tile )
{
	const DirectStrikeRect p = DirectStrikeLobby_picker( seat );
	const float x = p.x0 + DSLOBBY_PICKER_PAD + (float)column * ( DSLOBBY_TILE_W + DSLOBBY_TILE_GAP );
	const float y = DirectStrikeLobby_pickerRandom( seat ).y1 + 10 + (float)tile * ( DSLOBBY_TILE_H + DSLOBBY_TILE_GAP );
	return DirectStrike_rect( x, y, x + DSLOBBY_TILE_W, y + DSLOBBY_TILE_H );
}

/// the colour list at the colour line: `colors` cells of 64x48 in rows of four, then the hovered one's name
inline DirectStrikeRect DirectStrikeLobby_colorList( int seat, int colors )
{
	const DirectStrikeRect chip = DirectStrikeLobby_part( seat, DSSEAT_COLOR );
	const int rows = ( colors + DSLOBBY_COLOR_COLUMNS - 1 ) / DSLOBBY_COLOR_COLUMNS;
	const float w = 2.0f * DSLOBBY_COLOR_PAD + DSLOBBY_COLOR_COLUMNS * DSLOBBY_COLOR_CELL_W + ( DSLOBBY_COLOR_COLUMNS - 1 ) * DSLOBBY_COLOR_GAP;
	const float h = 2.0f * DSLOBBY_COLOR_PAD + (float)rows * DSLOBBY_COLOR_CELL_H + (float)( rows > 0 ? rows - 1 : 0 ) * DSLOBBY_COLOR_GAP
		+ DSLOBBY_COLOR_NAME_H;
	return DirectStrikeLobby_place( DirectStrike_rect( chip.x0, chip.y0, chip.x0 + w, chip.y1 ), w, h, false );
}
inline DirectStrikeRect DirectStrikeLobby_colorCell( int seat, int colors, int index )
{
	const DirectStrikeRect list = DirectStrikeLobby_colorList( seat, colors );
	const float x = list.x0 + DSLOBBY_COLOR_PAD + (float)( index % DSLOBBY_COLOR_COLUMNS ) * ( DSLOBBY_COLOR_CELL_W + DSLOBBY_COLOR_GAP );
	const float y = list.y0 + DSLOBBY_COLOR_PAD + (float)( index / DSLOBBY_COLOR_COLUMNS ) * ( DSLOBBY_COLOR_CELL_H + DSLOBBY_COLOR_GAP );
	return DirectStrike_rect( x, y, x + DSLOBBY_COLOR_CELL_W, y + DSLOBBY_COLOR_CELL_H );
}

/** Where the lane map's fortress stands: its distance from the map's middle as a share of the team's starts'
	* distance from it.  The generator's numbers (RandomMapGenerator.cpp, RMG_LANE_*), its fortress
	* RMG_LANE_FORTRESS_AT of the half-lane (180) from the middle and a start (a plot's middle) RMG_LANE_PLOT_GAP plus
	* half a plot (14 + 12) past the lane's end: 0.7 * 180 / ( 180 + 26 ).  The lobby has the starts from the map's
	* waypoints and no fortress. */
inline float DirectStrikeLobby_fortressShare( void ) { return 0.70f * 180.0f / ( 180.0f + 14.0f + 12.0f ); }

#endif // __DIRECTSTRIKELAYOUT_H_
