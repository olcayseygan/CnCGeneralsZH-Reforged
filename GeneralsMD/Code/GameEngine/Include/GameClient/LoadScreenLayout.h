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

// FILE: LoadScreenLayout.h ///////////////////////////////////////////////////////////////////////
//
// The load board's layout decisions that need no window and no font, so a test can walk every wing
// from one player to thirty-two: the board in the menu language (README-loading.md beside loading.py
// under the wrapper's experiments/proofs/2026-11-24/screens/loading), frame A's panel, the teams as
// two wings with the map between them, the foot's tip and load bar.  Every number is on the
// 1920x1080 board; LoadScreen.cpp scales them by the screen's smaller axis.
////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#ifndef _LOADSCREENLAYOUT_H_
#define _LOADSCREENLAYOUT_H_

#include <algorithm>
#include <string.h>
#include <vector>
#include "Lib/BaseType.h"

/// the content and the header stand where every converted screen's do (UIStyle::MENU_INSET, its right
/// at MENU_PANEL_RIGHT - 32, the title row and the header rule at 110), so nothing jumps between screens;
/// README-loading.md drew the content from 60 and the rule at 112
const Int LOAD_LEFT = 64;
const Int LOAD_RIGHT = 1856;
const Int LOAD_TITLE_TOP = 46;
const Int LOAD_TITLE_BOTTOM = 94;
const Int LOAD_HEADER_RULE = 110;
const Int LOAD_BODY_TOP = 132;
const Int LOAD_BODY_BOTTOM = 880;
const Int LOAD_WING_WIDTH = 650;				///< 64 to 714 and 1206 to 1856, 26 short of the middle each side
const Int LOAD_MIDDLE_LEFT = 740;
const Int LOAD_MIDDLE_RIGHT = 1180;
const Int LOAD_FOOT_RULE = 892;
const Int LOAD_TIP_TOP = 904;
const Int LOAD_TIP_BOTTOM = 944;
const Int LOAD_BAR_TOP = 964;
const Int LOAD_BAR_BOTTOM = 1024;
const Int LOAD_SECTION_HEAD = 34;				///< RULES, OBSERVERS: the words, the rule at 26, then the rows
const Int LOAD_MAP_LARGEST = 440;
const Int LOAD_RULE_PITCH = 38;					///< a rule's row at most
const Int LOAD_OBSERVERS_ROOM = 84;			///< the observers' gap, head and line
const Int LOAD_NIGHTS_WELL = 190;				///< Apocalypse's map well, the nights panel under it
const Int LOAD_NIGHTS_HEIGHT = 216;			///< the head, the band of four phases, three rows of 38
const Int LOAD_NIGHT_LEAST = 92;				///< a phase's segment, wide enough for DUSK and its clock
const Int LOAD_CARD_LEAST = 140;				///< a row stays a card while its pitch holds a human's four lines
const Int LOAD_LINE_MOST = 88;					///< a line row's pitch at most
const Int LOAD_POSTER_LEAST = 420;			///< a card this tall is a poster: the portrait over the lines
const Int LOAD_PORTRAIT_MOST = 300;			///< a card's portrait
const Int LOAD_PLAIN_PORTRAIT_MOST = 360;	///< a poster's picture of a plain side, a small piece of art
const Int LOAD_PITCH_NAME = 34;					///< a card's lines: the name,
const Int LOAD_PITCH_LINE = 28;					///< the army, the general's persona, a computer's personality,
const Int LOAD_PITCH_BAR = 40;					///< a human's load channel with its figure
const Int LOAD_TRANSFER_ROW_MOST = 76;	///< a map transfer's row
const Int LOAD_TRANSFER_WIDTH = 880;		///< a map transfer's column, 64 to 944 and 976 to 1856
/// the mission and saved-game frame: glass under the header and the foot, the briefing on the left
const Int LOAD_MISSION_HEADER_BOTTOM = 132;
const Int LOAD_MISSION_FOOT_TOP = 884;
const Int LOAD_BRIEFING_TOP = 152;
const Int LOAD_BRIEFING_RIGHT = 724;
const Int LOAD_BRIEFING_LEAST = 407;		///< the briefing glass's foot at least
const Int LOAD_BRIEFING_PITCH = 44;			///< a briefing line
const Int LOAD_PIP_WIDTH = 22;					///< a mission's pip, 12 high, 30 apart
const Int LOAD_PIP_GAP = 8;

/// what stands over a team's rows in a wing
enum LoadHeadKind
{
	LOAD_HEAD_NONE,				///< a free-for-all: every team one player, no heads
	LOAD_HEAD_RULE,				///< more than four teams: a rule in the team's colour
	LOAD_HEAD_LINE,				///< over line rows: TEAM n 18 and its ready count 24
	LOAD_HEAD_CARD				///< over cards: TEAM n 22 and its ready count 40
};

/// how a player's row is drawn
enum LoadRowKind
{
	LOAD_ROW_LINE,				///< stripe, small portrait, name, general, personality or load on one line
	LOAD_ROW_CARD,				///< the portrait as tall as the row, the lines beside it
	LOAD_ROW_POSTER				///< one player a team: the portrait as large as the wing allows, the lines under it
};

/// one wing's teams and rows, from the body's top
struct LoadWingPlan
{
	LoadHeadKind head;
	Int headHeight;				///< a head and its rule, to its first row
	Int gap;							///< after a team's last row
	LoadRowKind row;
	Int rowPitch;					///< one row's top to the next
	Int rowHeight;				///< what a row draws in: the pitch less the air under it
	Int used;							///< the wing's whole height
};

/** A wing holding teams teams of rows players in all, everyTeamAlone when each of them is one player:
	* a free-for-all (more than two teams of one) has no heads, more than four teams a rule each; a row
	* is a card while the body gives it LOAD_CARD_LEAST, else a line row of at most LOAD_LINE_MOST under
	* the smaller head; a card past LOAD_POSTER_LEAST is a poster. */
inline LoadWingPlan LoadScreen_planWing( Int teams, Int rows, Bool everyTeamAlone )
{
	teams = max( teams, 1 );
	rows = max( rows, 1 );
	const Int body = LOAD_BODY_BOTTOM - LOAD_BODY_TOP;
	const Bool freeForAll = teams > 2 && everyTeamAlone;
	const Bool crowded = teams > 4 && !freeForAll;
	LoadWingPlan plan;
	plan.head = freeForAll ? LOAD_HEAD_NONE : crowded ? LOAD_HEAD_RULE : LOAD_HEAD_CARD;
	plan.headHeight = freeForAll ? 0 : crowded ? 10 : 56;
	plan.gap = freeForAll ? 0 : crowded ? 4 : 12;
	Int room = body - plan.headHeight * teams - plan.gap * ( teams - 1 );
	const Bool cards = room / rows >= LOAD_CARD_LEAST;
	if( !cards && plan.head == LOAD_HEAD_CARD )
	{
		plan.head = LOAD_HEAD_LINE;
		plan.headHeight = 46;
		room = body - plan.headHeight * teams - plan.gap * ( teams - 1 );
	}
	plan.rowPitch = max( 1, cards ? room / rows : min( room / rows, LOAD_LINE_MOST ) );
	plan.row = !cards ? LOAD_ROW_LINE : plan.rowPitch > LOAD_POSTER_LEAST ? LOAD_ROW_POSTER : LOAD_ROW_CARD;
	plan.rowHeight = max( 1, plan.rowPitch - ( cards ? 8 : 2 ) );
	plan.used = plan.headHeight * teams + plan.rowPitch * rows + plan.gap * ( teams - 1 );
	return plan;
}

/// a card's lines from their top: the name, the army, the persona when the general has one, and the
/// last, a computer's personality (LOAD_PITCH_LINE), a human's load (LOAD_PITCH_BAR) or nothing (0)
inline Int LoadScreen_cardText( Bool persona, Int lastPitch )
{
	return LOAD_PITCH_NAME + LOAD_PITCH_LINE + ( persona ? LOAD_PITCH_LINE : 0 ) + lastPitch;
}

/// a card's portrait: as tall as the row less 8 above and below, at most LOAD_PORTRAIT_MOST
inline Int LoadScreen_cardPortrait( Int rowHeight )
{
	return max( 1, min( rowHeight - 16, LOAD_PORTRAIT_MOST ) );
}

/// a poster's portrait, over its text and 20 of air, inside the row's 18 and 18 of margin
inline Int LoadScreen_posterPortrait( Int width, Int rowHeight, Int text, Bool plainSide )
{
	Int side = min( width - 36, rowHeight - 16 - ( text + 20 ) );
	if( plainSide )
		side = min( side, LOAD_PLAIN_PORTRAIT_MOST );
	return max( 1, side );
}

/** Which wing each team stands in, the teams in their order: two teams face each other (and a lone
	* team starts on the left; the board runs it on into the right wing itself); more go whole, the
	* largest first, each to the wing with fewer rows, a head counting as one. */
inline std::vector< Int > LoadScreen_wingOfTeams( const std::vector< Int > &sizes )
{
	const Int count = (Int)sizes.size();
	std::vector< Int > wing( count, 0 );
	if( count <= 2 )
	{
		for( Int t = 0; t < count; t++ )
			wing[ t ] = t;
		return wing;
	}
	std::vector< Int > order( count );
	for( Int t = 0; t < count; t++ )
		order[ t ] = t;
	std::stable_sort( order.begin(), order.end(), [ &sizes ]( Int a, Int b ) { return sizes[ a ] > sizes[ b ]; } );
	Int rows[ 2 ] = { 0, 0 };
	for( Int each = 0; each < count; each++ )
	{
		const Int t = order[ each ];
		const Int side = rows[ 1 ] < rows[ 0 ] ? 1 : 0;
		wing[ t ] = side;
		rows[ side ] += sizes[ t ] + 1;
	}
	return wing;
}

/// the middle column, top to bottom
struct LoadMiddlePlan
{
	Int vsTop;						///< VS's box, 50 high; -1 with no versus
	IRegion2D map;				///< the preview in the map's shape, or the well
	Int nightsTop;				///< Apocalypse's nights panel; -1 without
	Int rulesTop;					///< the RULES head; -1 with no rules
	Int rulePitch;
	Int observersTop;			///< the OBSERVERS head; -1 with nobody watching
	Int bottom;						///< where the stack ends
};

/** The middle for a board: VS over the map in a versus; the map as large as the rest leaves, at most
	* LOAD_MAP_LARGEST either way, in its shape (aspect, width over height) or as a well the full width
	* (well: a hidden or generated map); the nights panel under a smaller well (nights); the rules at
	* LOAD_RULE_PITCH or less; the observers' line. */
inline LoadMiddlePlan LoadScreen_planMiddle( Bool versus, Real aspect, Bool well, Int rules, Bool observers, Bool nights )
{
	LoadMiddlePlan plan;
	Int y = LOAD_BODY_TOP;
	plan.vsTop = -1;
	if( versus )
	{
		plan.vsTop = y - 6;
		y += 60;
	}
	else
		y += 4;
	const Int width = LOAD_MIDDLE_RIGHT - LOAD_MIDDLE_LEFT;
	const Int rulesHeight = rules > 0 ? LOAD_SECTION_HEAD + rules * 30 : 0;
	Int room = LOAD_BODY_BOTTOM - y - ( rules > 0 ? 18 + rulesHeight : 0 ) - ( observers ? LOAD_OBSERVERS_ROOM : 0 );
	room = min( room, LOAD_MAP_LARGEST );
	if( nights )
		room = LOAD_NIGHTS_WELL;
	room = max( room, 1 );
	Int mapWidth = width, mapHeight = min( room, width );
	if( !well )
	{
		if( aspect <= 0.0f )
			aspect = 1.0f;
		mapHeight = REAL_TO_INT( width / aspect + 0.5f );
		if( mapHeight > room )
		{
			mapHeight = room;
			mapWidth = min( width, REAL_TO_INT( mapHeight * aspect + 0.5f ) );
		}
		mapWidth = max( mapWidth, 1 );
		mapHeight = max( mapHeight, 1 );
	}
	plan.map.lo.x = LOAD_MIDDLE_LEFT + ( width - mapWidth ) / 2;
	plan.map.lo.y = y;
	plan.map.hi.x = plan.map.lo.x + mapWidth;
	plan.map.hi.y = y + mapHeight;
	y += mapHeight;
	plan.nightsTop = -1;
	if( nights )
	{
		plan.nightsTop = y + 18;
		y = plan.nightsTop + LOAD_NIGHTS_HEIGHT;
	}
	plan.rulesTop = -1;
	plan.rulePitch = 0;
	if( rules > 0 )
	{
		plan.rulesTop = y + 18;
		const Int left = LOAD_BODY_BOTTOM - plan.rulesTop - ( observers ? LOAD_OBSERVERS_ROOM : 0 ) - LOAD_SECTION_HEAD;
		plan.rulePitch = max( 1, min( LOAD_RULE_PITCH, left / rules ) );
		y = plan.rulesTop + LOAD_SECTION_HEAD + rules * plan.rulePitch;
	}
	plan.observersTop = -1;
	if( observers )
	{
		plan.observersTop = y + 14;
		y = plan.observersTop + LOAD_SECTION_HEAD + 30;
	}
	plan.bottom = y;
	return plan;
}

/// the four phases' segments across total: each its share of the cycle, never under least, the day
/// giving up what the floor took
inline void LoadScreen_nightWidths( Int total, const Int seconds[ 4 ], Int least, Int widths[ 4 ] )
{
	Int cycle = 0;
	for( Int each = 0; each < 4; each++ )
		cycle += seconds[ each ];
	Int sum = 0;
	for( Int each = 0; each < 4; each++ )
	{
		widths[ each ] = max( least, cycle > 0 ? ( total * seconds[ each ] * 2 + cycle ) / ( cycle * 2 ) : 0 );
		sum += widths[ each ];
	}
	widths[ 0 ] -= sum - total;
}

/// game seconds as the wall clock passes them at the match's speed (percent of retail), rounded:
/// Apocalypse's day of 240 is 160 at 150
inline Int LoadScreen_wallSeconds( Int gameSeconds, Int speedPercent )
{
	if( speedPercent <= 0 )
		return gameSeconds;
	return ( gameSeconds * 200 + speedPercent ) / ( speedPercent * 2 );
}

/// a map transfer's rows in one of its two columns, under the heads
inline Int LoadScreen_transferRow( Int rows )
{
	const Int top = LOAD_BODY_TOP + 4 + LOAD_SECTION_HEAD;
	return max( 1, min( LOAD_TRANSFER_ROW_MOST, ( LOAD_BODY_BOTTOM - top ) / max( rows, 1 ) ) );
}

/** A mission's place in its campaign, 1 for the first: its index in the chain the campaign plays,
	* FirstMission and each NextMission in turn (chain, count names), which is not the number in its
	* name (GEN_GLA skips Mission03); 0 when it is not on the chain. */
inline Int LoadScreen_missionPlace( const char *const chain[], Int count, const char *mission )
{
	for( Int each = 0; each < count; each++ )
	{
		if( chain[ each ] && mission && strcmp( chain[ each ], mission ) == 0 )
			return each + 1;
	}
	return 0;
}

#endif // _LOADSCREENLAYOUT_H_
