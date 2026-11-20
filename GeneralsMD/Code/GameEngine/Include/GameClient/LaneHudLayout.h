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

// FILE: LaneHudLayout.h //////////////////////////////////////////////////////////////////////////
//
// Where the Lane War HUD's boxes stand on a screen (LaneHud.cpp draws them).  Authored at 1920x1080
// and scaled by the screen's height: a box against the left edge keeps its distance from it, one
// against the right edge from that, and the top band and the strip map stay centred.  The mode is
// laid out for 16:9 and wider only (the owner's call, 2026-11-20): a squarer screen gets the same
// boxes by its height, and the strip map may meet the panels beside it there.  Plain integers and no
// engine headers, so test_lanehud checks it alone.
//
///////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#ifndef __LANEHUDLAYOUT_H_
#define __LANEHUDLAYOUT_H_

struct LaneHudBox
{
	int x, y, w, h;
	bool contains( int px, int py ) const { return px >= x && px < x + w && py >= y && py < y + h; }
};

enum
{
	LANEHUD_ENEMY_BOXES = 3,
	LANEHUD_TABS = 4,				///< the three tiers and the upgrades
	LANEHUD_CELL_COLUMNS = 5,
	LANEHUD_CELL_ROWS = 3,
	LANEHUD_CELLS = LANEHUD_CELL_COLUMNS * LANEHUD_CELL_ROWS,
	LANEHUD_ECONOMY_KEYS = 7,	///< income tier, sell, abilities; then plot, lane, menu, diplomacy
};

enum LaneHudEconomyKey
{
	LANEHUD_KEY_INCOME,
	LANEHUD_KEY_SELL,
	LANEHUD_KEY_ABILITIES,
	LANEHUD_KEY_PLOT,
	LANEHUD_KEY_LANE,
	LANEHUD_KEY_MENU,
	LANEHUD_KEY_DIPLOMACY,
};

struct LaneHudLayout
{
	float scale;												///< the screen's height over 1080
	LaneHudBox top;											///< the wave order and the countdown
	LaneHudBox middle;									///< who holds the middle, under the top band
	LaneHudBox enemy[ LANEHUD_ENEMY_BOXES ];	///< each enemy's plot, down the right edge
	LaneHudBox map;											///< the strip map's panel
	LaneHudBox lane;										///< the lane inside it, 8 to 1
	LaneHudBox buy;
	LaneHudBox tabs[ LANEHUD_TABS ];
	LaneHudBox cells[ LANEHUD_CELLS ];
	LaneHudBox hero;										///< the side's hero, bought once a match, beside the cells
	LaneHudBox info;										///< the buy panel's foot: the unit under the pointer
	LaneHudBox economy;
	LaneHudBox money;										///< the economy box's first row
	LaneHudBox keys[ LANEHUD_ECONOMY_KEYS ];
	LaneHudBox abilities;								///< the abilities panel, over the economy box, when open
};

inline LaneHudBox LaneHud_box( int x, int y, int w, int h )
{
	LaneHudBox b;
	b.x = x;
	b.y = y;
	b.w = w;
	b.h = h;
	return b;
}

/// a 1920x1080 box on a width by height screen: anchor -1 against the left edge, +1 the right, 0 centred
inline LaneHudBox LaneHud_place( float x0, float y0, float x1, float y1, int anchor, int width, int height )
{
	const float s = (float)height / 1080.0f;
	float left;
	if( anchor < 0 )
		left = x0 * s;
	else if( anchor > 0 )
		left = (float)width - ( 1920.0f - x0 ) * s;
	else
		left = (float)width * 0.5f + ( x0 - 960.0f ) * s;
	const int lx = (int)( left + 0.5f ), ly = (int)( y0 * s + 0.5f );
	const int hx = (int)( left + ( x1 - x0 ) * s + 0.5f ), hy = (int)( y1 * s + 0.5f );
	return LaneHud_box( lx, ly, hx - lx, hy - ly );
}

/// a box inside parent at its 1080 offsets, scaled the same way
inline LaneHudBox LaneHud_inside( const LaneHudBox &parent, float x0, float y0, float x1, float y1, float s )
{
	const int lx = parent.x + (int)( x0 * s + 0.5f ), ly = parent.y + (int)( y0 * s + 0.5f );
	const int hx = parent.x + (int)( x1 * s + 0.5f ), hy = parent.y + (int)( y1 * s + 0.5f );
	return LaneHud_box( lx, ly, hx - lx, hy - ly );
}

inline void LaneHud_layout( int width, int height, LaneHudLayout *out )
{
	const float s = (float)height / 1080.0f;
	out->scale = s;
	out->top = LaneHud_place( 580, 0, 1340, 60, 0, width, height );
	out->middle = LaneHud_place( 700, 60, 1220, 88, 0, width, height );
	for( int i = 0; i < LANEHUD_ENEMY_BOXES; ++i )
		out->enemy[ i ] = LaneHud_place( 1700, 100.0f + 88.0f * i, 1912, 180.0f + 88.0f * i, 1, width, height );

	out->buy = LaneHud_place( 0, 744, 560, 1080, -1, width, height );
	for( int t = 0; t < LANEHUD_TABS; ++t )
		out->tabs[ t ] = LaneHud_inside( out->buy, 8.0f + 137.0f * t, 6, 8.0f + 137.0f * t + 133.0f, 46, s );
	for( int c = 0; c < LANEHUD_CELLS; ++c )
	{
		const int col = c % LANEHUD_CELL_COLUMNS, row = c / LANEHUD_CELL_COLUMNS;
		const float x0 = 8.0f + 88.0f * col, y0 = 52.0f + 82.0f * row;
		out->cells[ c ] = LaneHud_inside( out->buy, x0, y0, x0 + 84.0f, y0 + 78.0f, s );
	}
	out->hero = LaneHud_inside( out->buy, 452, 52, 552, 294, s );
	out->info = LaneHud_inside( out->buy, 8, 300, 552, 330, s );

	out->economy = LaneHud_place( 1360, 880, 1920, 1080, 1, width, height );
	out->money = LaneHud_inside( out->economy, 8, 6, 552, 56, s );
	out->keys[ LANEHUD_KEY_INCOME ] = LaneHud_inside( out->economy, 8, 62, 276, 124, s );
	out->keys[ LANEHUD_KEY_SELL ] = LaneHud_inside( out->economy, 284, 62, 414, 124, s );
	out->keys[ LANEHUD_KEY_ABILITIES ] = LaneHud_inside( out->economy, 422, 62, 552, 124, s );
	for( int k = 0; k < 4; ++k )
		out->keys[ LANEHUD_KEY_PLOT + k ] = LaneHud_inside( out->economy, 8.0f + 137.0f * k, 132, 8.0f + 137.0f * k + 131.0f, 192, s );
	out->abilities = LaneHud_place( 1360, 452, 1920, 872, 1, width, height );

	// the strip map: 744 across at 1080, at the bottom between the buy panel and the economy box
	const int mapW = (int)( 744.0f * s + 0.5f );
	const int pad = (int)( 6.0f * s + 0.5f ) > 2 ? (int)( 6.0f * s + 0.5f ) : 2;
	const int laneW = mapW - 2 * pad;
	const int laneH = laneW / 8;
	const int mapH = laneH + 2 * pad + (int)( 18.0f * s + 0.5f );
	out->map = LaneHud_box( ( width - mapW ) / 2, height - mapH, mapW, mapH );
	out->lane = LaneHud_box( out->map.x + pad, out->map.y + out->map.h - pad - laneH, laneW, laneH );
}

#endif // __LANEHUDLAYOUT_H_
