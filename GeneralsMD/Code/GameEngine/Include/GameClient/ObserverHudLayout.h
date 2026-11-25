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

// FILE: ObserverHudLayout.h //////////////////////////////////////////////////////////////////////
//
// Where the watcher's HUD stands, in plain integers at 1920x1080 so test_observerhud checks it alone.  The owner's
// pick of 2026-11-24, the watcher's HUD as one more side bar (the wrapper's
// experiments/proofs/2026-11-24/spirit/b-bar/README-spec.md): America's radar box bottom left with five keys beside
// its square glass, China's centre panel for the followed player where the command grid stands, and Turkiye's box
// bottom right holding the roster or the standings, as tall as its list.  The paintings are
// Tools/hud_command_bar.py's (paint_obs_*), drawn by InGameUI.cpp, which maps these numbers onto the screen: the
// radar box hangs from the left edge, the right box from the right, the panel from the middle, all on the foot.
//
///////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#ifndef __OBSERVERHUDLAYOUT_H_
#define __OBSERVERHUDLAYOUT_H_

struct ObsRect
{
	int x0, y0, x1, y1;
	int w( void ) const { return x1 - x0; }
	int h( void ) const { return y1 - y0; }
};

inline ObsRect ObsHud_rect( int x0, int y0, int x1, int y1 ) { ObsRect r = { x0, y0, x1, y1 }; return r; }

enum
{
	OBSHUD_KEYS					= 5,			///< standings, vision, chat, options, quit
	OBSHUD_KEY_STANDINGS = 0, OBSHUD_KEY_VISION, OBSHUD_KEY_CHAT, OBSHUD_KEY_OPTIONS, OBSHUD_KEY_QUIT,
	OBSHUD_STATS				= 7,			///< money, income, army, kills, power, builds, super
	OBSHUD_STAT_ARMY		= 2,
	OBSHUD_CROWN				= 78,			///< the right box's top to its list well
	OBSHUD_MOUNT_H			= 58,			///< the mount's rise over the box
	OBSHUD_LIST_FOOT		= 1068,		///< the list well's foot
	OBSHUD_BOX_CUT			= 24,			///< the box's right and bottom slices (ReforgedHudObsSpec)
	OBSHUD_BOX_REF_W		= 860,		///< the box as drawn once, the largest it gets
	OBSHUD_BOX_REF_H		= 880,
	OBSHUD_FLOOR				= 14,			///< no text under 14 px at 1080 (9 at 720)
	OBSHUD_STAND_COL		= 66,			///< a standings column; the picked one 74
	OBSHUD_STAND_PICKED	= 74,
	OBSHUD_NAME_MIN			= 120,		///< the standings' name column, as the longest name needs between these
	OBSHUD_NAME_MAX			= 230,
	OBSHUD_ROSTER_X1		= 1556,		///< the right box's left edge, one roster column (to 16 seats)
	OBSHUD_ROSTER_X2		= 1316,		///< two columns
	OBSHUD_STAND_X_MAX	= 1536		///< the standings box is never narrower than 384
};

/// the radar box with its ledge (BlockRadar), its square glass (RadarUL/LR) and the five keys down its recess
inline ObsRect ObsHud_radarBox( void ) { return ObsHud_rect( 0, 795, 345, 1080 ); }
inline ObsRect ObsHud_radarGlass( void ) { return ObsHud_rect( 5, 828, 249, 1072 ); }
inline ObsRect ObsHud_key( int i ) { return ObsHud_rect( 270, 858 + 44 * i, 328, 894 + 44 * i ); }
/// the key's tooltip card on the radar's ledge, as wide as its words
inline ObsRect ObsHud_tip( int w ) { return ObsHud_rect( 0, 704, w, 790 ); }

/// the followed player's panel: the painting, its blocker, its wells
inline ObsRect ObsHud_panel( void ) { return ObsHud_rect( 540, 800, 1410, 1080 ); }
inline ObsRect ObsHud_panelBlock( void ) { return ObsHud_rect( 556, 820, 1394, 1080 ); }
inline ObsRect ObsHud_panelMoney( void ) { return ObsHud_rect( 899, 840, 1051, 868 ); }
inline ObsRect ObsHud_panelPower( void ) { return ObsHud_rect( 646, 888, 1304, 898 ); }
inline ObsRect ObsHud_panelPortrait( void ) { return ObsHud_rect( 584, 924, 708, 1048 ); }
/// the four fact wells: name, income, kills and lost, power
inline ObsRect ObsHud_panelFact( int i )
{
	static const int Y[ 4 ][ 2 ] = { { 922, 952 }, { 958, 986 }, { 992, 1020 }, { 1026, 1054 } };
	return ObsHud_rect( 718, Y[ i ][ 0 ], 928, Y[ i ][ 1 ] );
}
inline ObsRect ObsHud_panelCells( void ) { return ObsHud_rect( 940, 918, 1374, 1074 ); }
/// a cameo in the cells' well: rows 0 and 1 the army (seven each), row 2 the queue (three)
inline ObsRect ObsHud_panelCell( int row, int i ) { const int x = 940 + 66 + i * 50, y = 918 + 6 + row * 50; return ObsHud_rect( x, y, x + 44, y + 44 ); }
/// the powers' word and its two cells, the powers tray's shape, on row 2 after the queue
inline ObsRect ObsHud_panelPowersWord( void ) { const int x = 940 + 66 + 3 * 50 + 6, y = 918 + 6 + 100; return ObsHud_rect( x, y, x + 64, y + 44 ); }
inline ObsRect ObsHud_panelPower( int i ) { const int x = ObsHud_panelPowersWord().x0 + 68 + i * 56, y = 918 + 6 + 100; return ObsHud_rect( x, y, x + 52, y + 44 ); }

/// how far the panel and everything in it steps left of its place so it clears a right box starting at rightX0:
/// the two-column roster past 16 seats starts at 1316, inside the panel's 1410
inline int ObsHud_panelShift( int rightX0 ) { return rightX0 < ObsHud_panel().x1 ? rightX0 - ObsHud_panel().x1 : 0; }

/// a roster's sizes by seat count: the row, a team head's height, the name's size and the columns
struct ObsRosterTier { int row, head, nameSize, columns; };
inline ObsRosterTier ObsHud_rosterTier( int seats )
{
	ObsRosterTier t;
	if( seats <= 8 ) { t.row = 30; t.head = 26; t.nameSize = 16; t.columns = 1; }
	else if( seats <= 16 ) { t.row = 24; t.head = 24; t.nameSize = 14; t.columns = 1; }
	else { t.row = 22; t.head = 24; t.nameSize = 14; t.columns = 2; }
	return t;
}

/// the roster's list well's height: its column heads, the taller column's team heads and rows, the gaps between
/// teams and the legend's line; head 0 when every team is one seat
inline int ObsHud_rosterBody( int teamsInColumn, int rowsInColumn, int row, int head )
{
	return 26 + teamsInColumn * head + rowsInColumn * row + ( teamsInColumn > 0 && head > 0 ? ( teamsInColumn - 1 ) * 6 : 0 ) + 30;
}

/// the standings: rows 30 high and names at 15 to 8 seats, 22 and 14 past
inline int ObsHud_standRow( int seats ) { return seats <= 8 ? 30 : 22; }
inline int ObsHud_standNameSize( int seats ) { return seats <= 8 ? 15 : 14; }
/// its list well's height: the heads, a row a seat, a 4 pixel break every 8 places, the legend
inline int ObsHud_standBody( int seats ) { return 30 + seats * ObsHud_standRow( seats ) + 4 * ( seats > 0 ? ( seats - 1 ) / 8 : 0 ) + 30; }
/// its left edge for the longest name: the place, the tick, the picture, the swatch, the name, seven columns
inline int ObsHud_standX0( int seats, int nameW )
{
	const int name = nameW < OBSHUD_NAME_MIN ? OBSHUD_NAME_MIN : nameW > OBSHUD_NAME_MAX ? OBSHUD_NAME_MAX : nameW;
	const int inner = 8 + 34 + ( ObsHud_standRow( seats ) - 6 ) + 7 + 15 + name + 10 + OBSHUD_STAND_PICKED + 6 * OBSHUD_STAND_COL + 12;
	const int x0 = 1920 - 14 - 20 - inner;
	return x0 < OBSHUD_STAND_X_MAX ? x0 : OBSHUD_STAND_X_MAX;
}

/// the right box at its left edge and its list's height: the box from its top to the screen's foot, the mount
/// over its top right with the clock's well, the shares' channel, the two dropdowns and the list's well
struct ObsRightBox
{
	ObsRect box, mount, clock, channel, drop[ 2 ], well, block;
};
inline ObsRightBox ObsHud_rightBox( int x0, int body )
{
	ObsRightBox b;
	const int top = OBSHUD_LIST_FOOT - body - OBSHUD_CROWN;
	b.box = ObsHud_rect( x0, top, 1920, 1080 );
	b.mount = ObsHud_rect( 1690, top - OBSHUD_MOUNT_H, 1920, top + 16 );
	b.clock = ObsHud_rect( 1712, top - 46, 1856, top - 10 );
	b.channel = ObsHud_rect( x0 + 30, top + 20, 1898, top + 30 );
	const int dw = ( 1906 - x0 - 30 ) / 2 < 160 ? ( 1906 - x0 - 30 ) / 2 : 160;
	b.drop[ 0 ] = ObsHud_rect( x0 + 22, top + 38, x0 + 22 + dw, top + 70 );
	b.drop[ 1 ] = ObsHud_rect( x0 + 30 + dw, top + 38, x0 + 30 + 2 * dw, top + 70 );
	b.well = ObsHud_rect( x0 + 20, top + OBSHUD_CROWN, 1906, OBSHUD_LIST_FOOT );
	b.block = ObsHud_rect( x0, b.mount.y0 < top ? b.mount.y0 : top, 1920, 1080 );
	return b;
}

/// the Tab screen (Diplomacy.wnd's window at 1379x714): the titles, a column a team in the table, the facts
inline ObsRect ObsHud_tabTitles( void ) { return ObsHud_rect( 16, 14, 1362, 58 ); }
inline ObsRect ObsHud_tabTable( void ) { return ObsHud_rect( 16, 66, 1362, 640 ); }
inline ObsRect ObsHud_tabFacts( void ) { return ObsHud_rect( 16, 650, 1130, 702 ); }
/// the table holds at most four columns of eight: a team takes a column for every eight of its seats while that
/// fits, and past it (a free-for-all of nine or more) the seats run on down four columns in team order
enum { OBSHUD_TAB_COLUMNS = 4, OBSHUD_TAB_ROWS = 8, OBSHUD_TAB_ROW_H = 70 };
/// column c of n in the table (and its title over it), and seat j of rows in it, never taller than 70; four rows
/// or fewer take twice that, so a small match fills the table with bigger pictures instead of empty glass
inline ObsRect ObsHud_tabColumn( int c, int n, const ObsRect &band )
{
	const int cw = ( band.w() - 16 ) / ( n > 0 ? n : 1 );
	const int x0 = band.x0 + 8 + c * cw;
	return ObsHud_rect( x0, band.y0, x0 + cw - 8, band.y1 );
}
inline ObsRect ObsHud_tabRow( const ObsRect &column, int j, int rows )
{
	const ObsRect t = ObsHud_tabTable();
	const int fit = ( t.h() - 12 ) / ( rows > 0 ? rows : 1 );
	const int cap = rows <= OBSHUD_TAB_ROWS / 2 ? 2 * OBSHUD_TAB_ROW_H : OBSHUD_TAB_ROW_H;
	const int rh = fit < cap ? fit : cap;
	const int y0 = t.y0 + 6 + j * rh;
	return ObsHud_rect( column.x0, y0, column.x1, y0 + rh );
}
/// a row's mute key, 6 in from the column's right edge, centred on the row
inline ObsRect ObsHud_tabMute( const ObsRect &row ) { const int cy = ( row.y0 + row.y1 ) / 2; return ObsHud_rect( row.x1 - 40, cy - 16, row.x1 - 6, cy + 16 ); }

#endif // __OBSERVERHUDLAYOUT_H_
