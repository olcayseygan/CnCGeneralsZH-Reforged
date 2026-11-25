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

// FILE: DirectStrikeHudLayout.h //////////////////////////////////////////////////////////////////////////
//
// Where the Direct Strike HUD's boxes stand on a screen (DirectStrikeHud.cpp draws them): the owner's concept C of
// 2026-11-21, a hand of cards.  At the top centre float the money chip, the lane chip with the strip
// map and the your-wave chip, with the wave bar and the countdown under the lane; along the bottom stand
// the tier keys, the hero's gold card, the tier's cards in a flat row, the upgrades deck and the
// abilities key; the upgrades row and the abilities panel open over the deck.  Authored at 1920x1080
// (hud-artist's README-v4.md for the bottom, README-v6.md option B for the top) and scaled by the screen's
// height: the top chips and the bottom row stay centred, the menu keys keep their distance from the
// right edge.  16:9 and wider only (the owner's call, 2026-11-20).  Plain integers and no engine
// headers, so test_directstrikehud checks it alone.
//
///////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#ifndef __DIRECTSTRIKEHUDLAYOUT_H_
#define __DIRECTSTRIKEHUDLAYOUT_H_

struct DirectStrikeHudBox
{
	int x, y, w, h;
	bool contains( int px, int py ) const { return px >= x && px < x + w && py >= y && py < y + h; }
};

enum
{
	DSHUD_TIERS = 3,
	DSHUD_CARDS = 8,							///< a tier's units; no side fields more than eight in one
	DSHUD_UPGRADE_CELLS = 13,			///< the income tier and the most upgrades a side or general sells
	DSHUD_ABILITY_ROWS = 16,			///< one a roster unit's button the director presses; the art holds six, more grow the panel up
	DSHUD_PLOT_FRAMES = 5,				///< the other seats of a 3v3
	DSHUD_CARD_RISE = 12,					///< at 1080: a card under the pointer stands this much higher
};

struct DirectStrikeHudLayout
{
	float scale;												///< the screen's height over 1080
	DirectStrikeHudBox money;										///< the money chip: coin, money, income
	DirectStrikeHudBox map;											///< the lane chip
	DirectStrikeHudBox lane;										///< its glass, where the strip map is drawn
	DirectStrikeHudBox yourWave;								///< the your-wave chip: the player's own turn
	DirectStrikeHudBox waveBar;									///< the bar chip under the lane: each team's next wave filling to the middle
	DirectStrikeHudBox countdown;								///< the countdown chip under the bar
	DirectStrikeHudBox matchClock;							///< under the your-wave chip: the time since the match began
	DirectStrikeHudBox menu, diplomacy;					///< top right, left of the Tab frames
	DirectStrikeHudBox tiers[ DSHUD_TIERS ];	///< the tier keys, I over II over III
	DirectStrikeHudBox hero;										///< the hero's gold card, bought once a match
	DirectStrikeHudBox cards[ DSHUD_CARDS ];
	DirectStrikeHudBox hand;										///< the hero and the cards with their rise: where a plot unit dragged back is sold
	DirectStrikeHudBox deck;										///< the upgrades, a stack that opens on a click
	DirectStrikeHudBox abilitiesKey;
	DirectStrikeHudBox revealKey;								///< the satellite scan, right of the abilities key
	DirectStrikeHudBox upgradeRow;							///< the open deck's row of cards, the most it holds, its right end on the deck's
	int abilitiesRight, abilitiesBottom, abilitiesWidth;	///< the abilities panel grows up from here by its rows
	int abilityRowH, abilityPad;
	DirectStrikeHudBox abilityCameo, abilityIcon, abilityToggle;	///< inside a row, from its corner
	DirectStrikeHudBox plotFrame[ DSHUD_PLOT_FRAMES ];	///< Tab held: every other seated player, down the right edge from the top
};

inline DirectStrikeHudBox DirectStrikeHud_box( int x, int y, int w, int h )
{
	DirectStrikeHudBox b;
	b.x = x;
	b.y = y;
	b.w = w;
	b.h = h;
	return b;
}

/// a 1920x1080 box on a width by height screen: anchor -1 against the left edge, +1 the right, 0 centred
inline DirectStrikeHudBox DirectStrikeHud_place( float x0, float y0, float x1, float y1, int anchor, int width, int height )
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
	return DirectStrikeHud_box( lx, ly, hx - lx, hy - ly );
}

/// a box inside parent at its 1080 offsets, scaled the same way
inline DirectStrikeHudBox DirectStrikeHud_inside( const DirectStrikeHudBox &parent, float x0, float y0, float x1, float y1, float s )
{
	const int lx = parent.x + (int)( x0 * s + 0.5f ), ly = parent.y + (int)( y0 * s + 0.5f );
	const int hx = parent.x + (int)( x1 * s + 0.5f ), hy = parent.y + (int)( y1 * s + 0.5f );
	return DirectStrikeHud_box( lx, ly, hx - lx, hy - ly );
}

inline int DirectStrikeHud_px( float at1080, float s )
{
	const int v = (int)( at1080 * s + 0.5f );
	return v < 1 ? 1 : v;
}

inline void DirectStrikeHud_layout( int width, int height, DirectStrikeHudLayout *out )
{
	const float s = (float)height / 1080.0f;
	out->scale = s;

	// the top: floating chips like the cards, no plate behind them (README-v6, the owner's option B)
	out->money = DirectStrikeHud_place( 505, 8, 705, 74, 0, width, height );
	out->map = DirectStrikeHud_place( 715, 8, 1205, 74, 0, width, height );
	out->lane = DirectStrikeHud_inside( out->map, 5, 5, 485, 61, s );
	out->yourWave = DirectStrikeHud_place( 1215, 8, 1311, 74, 0, width, height );
	out->waveBar = DirectStrikeHud_place( 715, 82, 1205, 96, 0, width, height );
	out->countdown = DirectStrikeHud_place( 918, 104, 1002, 140, 0, width, height );
	out->matchClock = DirectStrikeHud_place( 1221, 82, 1305, 118, 0, width, height );	// the countdown chip's size, centred under the your-wave chip
	// the menu keys stand left of the Tab frames' column (README-v4 put them over its first frame)
	out->menu = DirectStrikeHud_place( 1552, 8, 1622, 40, 1, width, height );
	out->diplomacy = DirectStrikeHud_place( 1626, 8, 1696, 40, 1, width, height );

	// the bottom row, centred: tier keys, hero, eight cards, the deck, the abilities key
	const float left = 353.0f, handY = 958.0f;
	for( int t = 0; t < DSHUD_TIERS; ++t )
		out->tiers[ t ] = DirectStrikeHud_place( left, 966.0f + 34.0f * t, left + 68.0f, 996.0f + 34.0f * t, 0, width, height );
	out->hero = DirectStrikeHud_place( left + 80.0f, handY, left + 180.0f, handY + 116.0f, 0, width, height );
	for( int c = 0; c < DSHUD_CARDS; ++c )
		out->cards[ c ] = DirectStrikeHud_place( left + 196.0f + 106.0f * c, handY, left + 296.0f + 106.0f * c, handY + 116.0f, 0, width, height );
	out->hand = DirectStrikeHud_place( left + 72.0f, handY - DSHUD_CARD_RISE - 4.0f, left + 1046.0f, 1080, 0, width, height );
	out->deck = DirectStrikeHud_place( left + 1054.0f, handY, left + 1154.0f, handY + 116.0f, 0, width, height );
	out->abilitiesKey = DirectStrikeHud_place( left + 1170.0f, 994, left + 1214.0f, 1038, 0, width, height );
	out->revealKey = DirectStrikeHud_place( left + 1222.0f, 994, left + 1266.0f, 1038, 0, width, height );

	// the open deck: a row of cards over the hand, its right end on the deck's (whose top copy sits 6 right)
	const float deckRight = left + 1160.0f, upgradeLeft = deckRight - 106.0f * DSHUD_UPGRADE_CELLS + 6.0f;
	out->upgradeRow = DirectStrikeHud_place( upgradeLeft, 822, deckRight, 938, 0, width, height );
	const int rowRight = out->upgradeRow.x + out->upgradeRow.w;		// the cells step from its right end in whole pixels
	out->upgradeRow.x = rowRight - DSHUD_UPGRADE_CELLS * DirectStrikeHud_px( 106, s ) + DirectStrikeHud_px( 106, s ) - DirectStrikeHud_px( 100, s );
	out->upgradeRow.w = rowRight - out->upgradeRow.x;

	// the abilities panel, its right edge 33 past the abilities key's and its foot over the row
	const DirectStrikeHudBox key = out->abilitiesKey;
	out->abilitiesRight = key.x + key.w + DirectStrikeHud_px( 33, s );
	out->abilitiesBottom = DirectStrikeHud_px( 950, s );
	out->abilitiesWidth = DirectStrikeHud_px( 192, s );
	out->abilityPad = DirectStrikeHud_px( 8, s );
	out->abilityRowH = DirectStrikeHud_px( 48, s );
	out->abilityCameo = DirectStrikeHud_box( DirectStrikeHud_px( 6, s ), DirectStrikeHud_px( 4, s ), DirectStrikeHud_px( 50, s ), DirectStrikeHud_px( 40, s ) );
	out->abilityIcon = DirectStrikeHud_box( DirectStrikeHud_px( 62, s ), DirectStrikeHud_px( 4, s ), DirectStrikeHud_px( 50, s ), DirectStrikeHud_px( 40, s ) );
	out->abilityToggle = DirectStrikeHud_box( DirectStrikeHud_px( 124, s ), DirectStrikeHud_px( 13, s ), DirectStrikeHud_px( 44, s ), DirectStrikeHud_px( 22, s ) );

	// Tab held: a frame a seat down the right edge from the top, README-v4's 1700,8 / 96 / 184 and on
	for( int i = 0; i < DSHUD_PLOT_FRAMES; ++i )
		out->plotFrame[ i ] = DirectStrikeHud_place( 1700, 8.0f + 88.0f * i, 1912, 88.0f + 88.0f * i, 1, width, height );
}

/// the open deck's card i of count, the row's right end on the deck's
inline DirectStrikeHudBox DirectStrikeHud_upgradeCell( const DirectStrikeHudLayout &l, int count, int i )
{
	const int stride = DirectStrikeHud_px( 106, l.scale ), w = DirectStrikeHud_px( 100, l.scale );
	const int right = l.upgradeRow.x + l.upgradeRow.w;
	return DirectStrikeHud_box( right - ( count - i ) * stride + ( stride - w ), l.upgradeRow.y, w, l.upgradeRow.h );
}

/// the abilities panel holding that many rows (six fill the art's glass)
inline DirectStrikeHudBox DirectStrikeHud_abilitiesPanel( const DirectStrikeHudLayout &l, int rows )
{
	const int h = 2 * l.abilityPad + rows * l.abilityRowH;
	return DirectStrikeHud_box( l.abilitiesRight - l.abilitiesWidth, l.abilitiesBottom - h, l.abilitiesWidth, h );
}

/// a row of the panel, the glass's width
inline DirectStrikeHudBox DirectStrikeHud_abilityRow( const DirectStrikeHudLayout &l, int rows, int row )
{
	const DirectStrikeHudBox panel = DirectStrikeHud_abilitiesPanel( l, rows );
	return DirectStrikeHud_box( panel.x + l.abilityPad, panel.y + l.abilityPad + row * l.abilityRowH, panel.w - 2 * l.abilityPad, l.abilityRowH );
}

/// a box inside a row (abilityCameo, abilityIcon, abilityToggle) on the screen
inline DirectStrikeHudBox DirectStrikeHud_at( const DirectStrikeHudBox &row, const DirectStrikeHudBox &part )
{
	return DirectStrikeHud_box( row.x + part.x, row.y + part.y, part.w, part.h );
}

#endif // __DIRECTSTRIKEHUDLAYOUT_H_
