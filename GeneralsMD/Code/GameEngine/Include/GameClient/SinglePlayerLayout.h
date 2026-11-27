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

// FILE: SinglePlayerLayout.h /////////////////////////////////////////////////////////////////////
// The single player's screens on the 1920x1080 board (docs/plans/MENU-REDESIGN.md, package 12):
// Generals' Challenge, the replay list, Load Game, the Save Replay box and the Save / Load box.
// Every number is the design's (experiments/proofs/2026-11-24/screens/single-player in the wrapper,
// rects.txt); the masters Tools/singleplayer_layout.py and Tools/boxes_layout.py write stand on the
// same numbers, and Tests/test_single_player.inc holds both to them.  Pure: no display, no strings.
///////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#ifndef __SINGLEPLAYERLAYOUT_H_
#define __SINGLEPLAYERLAYOUT_H_

#include "Lib/BaseType.h"

#include <math.h>
#include <stdio.h>
#include <vector>

namespace SinglePlayerLayout
{
	inline IRegion2D rect( Int x0, Int y0, Int x1, Int y1 )
	{
		IRegion2D r;
		r.lo.x = x0;
		r.lo.y = y0;
		r.hi.x = x1;
		r.hi.y = y1;
		return r;
	}

	//-----------------------------------------------------------------------------------------------
	// the frame the three screens share; the title row and its rule are UIStyle::menuHeader's
	//-----------------------------------------------------------------------------------------------
	const Int LEFT = 60;
	const Int RIGHT = 1860;
	const Int BODY_TOP = 128;
	const Int FOOT_RULE = 962;
	const Int KEYS_TOP = 976;
	const Int KEYS_BOTTOM = 1028;
	const Int KEY_GAP = 12;
	inline IRegion2D key( Int x0, Int x1 ) { return rect( x0, KEYS_TOP, x1, KEYS_BOTTOM ); }

	//-----------------------------------------------------------------------------------------------
	// Generals' Challenge
	//-----------------------------------------------------------------------------------------------
	const Int SIDES = 3;										///< USA, China, GLA, left to right
	const Int CARDS_PER_SIDE = 3;
	const Int CARD_WIDTH = 184;
	const Int CARD_HEIGHT = 240;
	const Int CARD_GAP = 10;
	const Int SIDE_GAP = 36;
	const Int CARDS_LEFT = 66;
	const Int CARDS_TOP = 160;
	const Int SIDE_WIDTH = CARDS_PER_SIDE * CARD_WIDTH + ( CARDS_PER_SIDE - 1 ) * CARD_GAP;
	const Int CARD_PORTRAIT_INSET = 6;			///< the square portrait, 6 in from the card's edge

	/// a side's head (USA, CHINA, GLA): its capitals at the top, a faint rule 24 under
	inline IRegion2D sideHead( Int side )
	{
		const Int x0 = CARDS_LEFT + side * ( SIDE_WIDTH + SIDE_GAP );
		return rect( x0, BODY_TOP, x0 + SIDE_WIDTH, BODY_TOP + 24 );
	}
	inline IRegion2D card( Int side, Int slot )
	{
		const Int x0 = sideHead( side ).lo.x + slot * ( CARD_WIDTH + CARD_GAP );
		return rect( x0, CARDS_TOP, x0 + CARD_WIDTH, CARDS_TOP + CARD_HEIGHT );
	}

	const IRegion2D DOSSIER_NAME = { { 60, 424 }, { 612, 472 } };
	const IRegion2D DOSSIER_RANK = { { 60, 476 }, { 612, 500 } };
	const IRegion2D DOSSIER_PORTRAIT = { { 60, 516 }, { 348, 948 } };
	const IRegion2D DOSSIER_FACTS = { { 368, 516 }, { 612, 948 } };
	const Int DOSSIER_CHALLENGE_TOP = 716;	///< THE CHALLENGE never stands above this

	/// the places map: Mercator from 62N to 8N over 128W to 128E, the band every station is in
	const IRegion2D PLACES = { { 644, 428 }, { 1860, 768 } };
	const Real PLACES_LON_WEST = -128.0f;
	const Real PLACES_LON_EAST = 128.0f;
	const Real PLACES_LAT_NORTH = 62.0f;
	const Real PLACES_LAT_SOUTH = 8.0f;
	const Int RING_RADIUS = 12;
	const Int RING_GAP = 32;								///< rings closer than this are pushed apart
	const IRegion2D PLACES_LEGEND = { { 1460, 716 }, { 1844, 752 } };

	inline Real mercator( Real latitude )
	{
		const Real rad = latitude * 3.14159265f / 180.0f;
		return (Real)log( tan( 3.14159265f / 4.0f + rad / 2.0f ) );
	}
	/// the map's height at a width: what makes the band's 340 at the design's 1216
	inline Int placesHeightFor( Int width )
	{
		const Real span = ( PLACES_LON_EAST - PLACES_LON_WEST ) * 3.14159265f / 180.0f;
		return (Int)floor( width * ( mercator( PLACES_LAT_NORTH ) - mercator( PLACES_LAT_SOUTH ) ) / span + 0.5f );
	}
	/// a station on the map at 1080
	inline void placesPoint( Real longitude, Real latitude, Real &x, Real &y )
	{
		const Real w = (Real)( PLACES.hi.x - PLACES.lo.x );
		const Real h = (Real)( PLACES.hi.y - PLACES.lo.y );
		const Real m0 = mercator( PLACES_LAT_NORTH ), m1 = mercator( PLACES_LAT_SOUTH );
		x = PLACES.lo.x + ( longitude - PLACES_LON_WEST ) / ( PLACES_LON_EAST - PLACES_LON_WEST ) * w;
		y = PLACES.lo.y + ( m0 - mercator( latitude ) ) / ( m0 - m1 ) * h;
	}
	/// the LOCATION UNKNOWN box for generals the string table stations nowhere, a 36 px line each
	inline IRegion2D unknownBox( Int count )
	{
		return rect( PLACES.lo.x + 16, PLACES.hi.y - 16 - 30 - 36 * count, PLACES.lo.x + 16 + 260, PLACES.hi.y - 16 );
	}

	/** Rings that would come closer than gap are pushed apart along the line between them, kept 16
		* in from the box's edge; a ring that moved keeps a leader line to its station (the caller's).
		* The design's spread, step for step: at most 120 rounds, stopping on the first quiet one. */
	inline void spreadRings( std::vector< Real > &x, std::vector< Real > &y, const IRegion2D &box, Real gap = (Real)RING_GAP )
	{
		const size_t n = x.size();
		for( Int round = 0; round < 120; round++ )
		{
			Bool moved = FALSE;
			for( size_t a = 0; a < n; a++ )
			{
				for( size_t b = a + 1; b < n; b++ )
				{
					Real dx = x[ b ] - x[ a ], dy = y[ b ] - y[ a ];
					Real dist = (Real)sqrt( dx * dx + dy * dy );
					if( dist >= gap )
						continue;
					if( dist < 0.001f )
					{
						dx = 0.0f;
						dy = 1.0f;
						dist = 1.0f;
					}
					const Real push = ( gap - dist ) / 2.0f / dist;
					x[ a ] -= dx * push;
					y[ a ] -= dy * push;
					x[ b ] += dx * push;
					y[ b ] += dy * push;
					moved = TRUE;
				}
			}
			for( size_t k = 0; k < n; k++ )
			{
				const Real lx = (Real)( box.lo.x + 16 ), hx = (Real)( box.hi.x - 16 );
				const Real ly = (Real)( box.lo.y + 16 ), hy = (Real)( box.hi.y - 16 );
				x[ k ] = x[ k ] < lx ? lx : x[ k ] > hx ? hx : x[ k ];
				y[ k ] = y[ k ] < ly ? ly : y[ k ] > hy ? hy : y[ k ];
			}
			if( !moved )
				break;
		}
	}

	/// the ladder, one tile a battle, 10 apart across the map's width, from under the map to the foot
	const Int LADDER_TOP = 784;
	const Int LADDER_BOTTOM = 948;
	const Int LADDER_GAP = 10;
	const Int LADDER_PORTRAIT = 84;
	const Int LADDER_SURNAMES_UNDER = 160;	///< tiles narrower than this name their generals by surname
	inline Real ladderTileWidth( Int count )
	{
		return count > 0 ? (Real)( PLACES.hi.x - PLACES.lo.x - LADDER_GAP * ( count - 1 ) ) / count : 0.0f;
	}
	inline IRegion2D ladderTile( Int count, Int index )
	{
		const Real w = ladderTileWidth( count );
		const Real x = PLACES.lo.x + index * ( w + LADDER_GAP );
		return rect( (Int)floor( x + 0.5f ), LADDER_TOP, (Int)floor( x + w + 0.5f ), LADDER_BOTTOM );
	}

	/// ChallengeMode.ini's personas (0 to 9, Leang last): the cards' order under USA, CHINA and GLA
	const Int CHALLENGE_PERSONAS = 10;
	const Int LEANG = 9;
	inline Int sidePersona( Int side, Int slot )
	{
		static const Int order[ SIDES ][ CARDS_PER_SIDE ] = { { 0, 3, 5 }, { 2, 4, 7 }, { 1, 6, 8 } };
		return order[ side ][ slot ];
	}

	/** Where a persona is stationed, as longitude and latitude: the place GUI:BioBirthplaceEntry_PosN
		* names (Houston, Chengdu, Belfast in Maine, Jinan, Redwood Shores, Tripoli, Beijing, Lanzhou).
		* FALSE for the two the string stations nowhere (Thrax and Juhziz) and for any other number. */
	inline Bool station( Int persona, Real &longitude, Real &latitude )
	{
		static const Real at[ CHALLENGE_PERSONAS ][ 3 ] =
		{
			{ -95.4f, 29.8f, 1 }, { 0, 0, 0 }, { 104.1f, 30.7f, 1 }, { -69.0f, 44.4f, 1 }, { 117.0f, 36.7f, 1 },
			{ -122.2f, 37.5f, 1 }, { 13.2f, 32.9f, 1 }, { 116.4f, 39.9f, 1 }, { 0, 0, 0 }, { 103.8f, 36.1f, 1 }
		};
		if( persona < 0 || persona >= CHALLENGE_PERSONAS || at[ persona ][ 2 ] == 0 )
			return FALSE;
		longitude = at[ persona ][ 0 ];
		latitude = at[ persona ][ 1 ];
		return TRUE;
	}

	/// the army a persona fields, as its word's key (GUI:SPArmy...); Leang is the final battle
	inline const char *armyKey( Int persona )
	{
		static const char *keys[ CHALLENGE_PERSONAS ] =
		{
			"GUI:SPArmyAirForce", "GUI:SPArmyToxin", "GUI:SPArmyNuke", "GUI:SPArmySuperweapon", "GUI:SPArmyTank",
			"GUI:SPArmyLaser", "GUI:SPArmyStealth", "GUI:SPArmyInfantry", "GUI:SPArmyDemolition", "GUI:SPFinalBattle"
		};
		return persona >= 0 && persona < CHALLENGE_PERSONAS ? keys[ persona ] : "GUI:SPArmyAirForce";
	}

	const IRegion2D CHALLENGE_BACK = { { 60, KEYS_TOP }, { 270, KEYS_BOTTOM } };
	const Int DIFFICULTY_LEFT = 306;				///< the DIFFICULTY readout, after BACK
	const IRegion2D CHALLENGE_PLAY = { { 1460, KEYS_TOP }, { 1860, KEYS_BOTTOM } };

	//-----------------------------------------------------------------------------------------------
	// the lists' columns: each list's left and right edge in its own numbers, laid across any span
	// by the design's rule (the Save Replay box and the Save / Load box squeeze the screens' columns)
	//-----------------------------------------------------------------------------------------------
	struct Column
	{
		Int left, right;
		Bool alignRight;
	};
	inline Column columnAcross( Int left, Int right, Bool alignRight, Int listLeft, Int listRight, Int x0, Int x1 )
	{
		const Real k = (Real)( x1 - x0 ) / (Real)( listRight - listLeft );
		Column c;
		c.left = (Int)floor( x0 + ( left - listLeft ) * k + 0.5f );
		c.right = (Int)floor( x0 + ( right - listLeft ) * k + 0.5f );
		c.alignRight = alignRight;
		return c;
	}

	//-----------------------------------------------------------------------------------------------
	// the replay list
	//-----------------------------------------------------------------------------------------------
	const Int REPLAY_LIST_RIGHT = 1236;
	const IRegion2D REPLAY_HEADS = { { LEFT, BODY_TOP }, { REPLAY_LIST_RIGHT, 162 } };
	const Int REPLAY_ROW_HEIGHT = 38;
	const Int REPLAY_ROWS = 20;
	/// the list box: the rows and the 3 px bar at REPLAY_SCROLL_X, down to the legend
	const IRegion2D REPLAY_LIST = { { LEFT, 162 }, { REPLAY_LIST_RIGHT, 162 + REPLAY_ROWS * REPLAY_ROW_HEIGHT } };
	const Int REPLAY_ROW_RIGHT = 1228;
	const Int REPLAY_SCROLL_X = 1232;
	const IRegion2D REPLAY_LEGEND = { { 76, 932 }, { 1220, 950 } };

	enum ReplayColumn { REPLAY_NAME, REPLAY_RECORDED, REPLAY_MAP, REPLAY_PLAYERS, REPLAY_LENGTH, REPLAY_VERSION, REPLAY_COLUMNS };
	inline Column replayColumn( Int which, Int x0 = LEFT, Int x1 = REPLAY_LIST_RIGHT )
	{
		static const Int edges[ REPLAY_COLUMNS ][ 3 ] =
		{
			{ 76, 380, 0 }, { 392, 590, 0 }, { 602, 800, 0 }, { 812, 914, 1 }, { 942, 1030, 1 }, { 1054, 1220, 0 }
		};
		return columnAcross( edges[ which ][ 0 ], edges[ which ][ 1 ], edges[ which ][ 2 ] != 0, LEFT, REPLAY_LIST_RIGHT, x0, x1 );
	}

	const Int REPLAY_DETAILS_LEFT = 1268;
	const IRegion2D REPLAY_DETAILS_NAME = { { 1268, 124 }, { 1860, 154 } };
	const IRegion2D REPLAY_DETAILS_LINE = { { 1268, 158 }, { 1860, 180 } };
	const IRegion2D REPLAY_PREVIEW = { { 1268, 192 }, { 1860, 496 } };
	const Int REPLAY_FACTS_TOP = 512;
	const Int REPLAY_FACT_PITCH = 54;
	const Int REPLAY_PLAYERS_TOP = 628;
	const Int REPLAY_PLAYER_ROW = 34;				///< at eight seats; more share the height to the foot
	/// a fact's cell: three a row, 54 apart
	inline IRegion2D replayFact( Int index )
	{
		const Int w = ( RIGHT - REPLAY_DETAILS_LEFT ) / 3;
		const Int x0 = REPLAY_DETAILS_LEFT + ( index % 3 ) * w;
		const Int y0 = REPLAY_FACTS_TOP + ( index / 3 ) * REPLAY_FACT_PITCH;
		return rect( x0, y0, x0 + w - 12, y0 + 46 );
	}
	/// the players stand in one column up to eight seats, in two past that (32 seats: 16 rows of 18)
	inline Int replayPlayerColumns( Int seats ) { return seats > 8 ? 2 : 1; }
	/// the players' rows under their head, the height shared to the foot, never over 34
	inline Int replayPlayerRow( Int seats )
	{
		if( seats <= 0 )
			return REPLAY_PLAYER_ROW;
		const Int columns = replayPlayerColumns( seats );
		const Int rows = ( seats + columns - 1 ) / columns;
		const Int fit = ( FOOT_RULE - 10 - ( REPLAY_PLAYERS_TOP + 34 ) ) / rows;
		return fit < REPLAY_PLAYER_ROW ? fit : REPLAY_PLAYER_ROW;
	}
	/// a map's picture at its own shape, as large as the preview box holds, centred in it
	inline IRegion2D fitShape( const IRegion2D &box, Real width, Real height )
	{
		const Int bw = box.hi.x - box.lo.x, bh = box.hi.y - box.lo.y;
		if( width <= 0.0f || height <= 0.0f )
			return box;
		Int w = bw, h = (Int)floor( bw * height / width + 0.5f );
		if( h > bh )
		{
			h = bh;
			w = (Int)floor( bh * width / height + 0.5f );
		}
		const Int x0 = box.lo.x + ( bw - w ) / 2, y0 = box.lo.y + ( bh - h ) / 2;
		return rect( x0, y0, x0 + w, y0 + h );
	}

	const IRegion2D REPLAY_BACK = { { 60, KEYS_TOP }, { 270, KEYS_BOTTOM } };
	const IRegion2D REPLAY_COPY = { { 282, KEYS_TOP }, { 542, KEYS_BOTTOM } };
	const IRegion2D REPLAY_DELETE = { { 554, KEYS_TOP }, { 734, KEYS_BOTTOM } };
	const IRegion2D REPLAY_WATCH = { { REPLAY_DETAILS_LEFT, KEYS_TOP }, { RIGHT, KEYS_BOTTOM } };

	//-----------------------------------------------------------------------------------------------
	// Load Game
	//-----------------------------------------------------------------------------------------------
	const Int SAVE_LIST_RIGHT = 1168;
	const IRegion2D SAVE_HEADS = { { LEFT, BODY_TOP }, { SAVE_LIST_RIGHT, 162 } };
	const Int SAVE_ROW_HEIGHT = 40;
	const Int SAVE_ROWS = 18;
	const IRegion2D SAVE_LIST = { { LEFT, 162 }, { SAVE_LIST_RIGHT, 162 + SAVE_ROWS * SAVE_ROW_HEIGHT } };
	const Int SAVE_ROW_RIGHT = 1160;
	const Int SAVE_SCROLL_X = 1164;
	const Int SAVE_LEGEND_TOP = 894;				///< two lines, 26 apart

	enum SaveColumn { SAVE_NAME, SAVE_CAMPAIGN, SAVE_MISSION, SAVE_KIND, SAVE_SAVED, SAVE_COLUMNS };
	inline Column saveColumn( Int which, Int x0 = LEFT, Int x1 = SAVE_LIST_RIGHT )
	{
		static const Int edges[ SAVE_COLUMNS ][ 3 ] =
		{
			{ 76, 404, 0 }, { 416, 610, 0 }, { 622, 700, 1 }, { 724, 884, 0 }, { 896, 1152, 0 }
		};
		return columnAcross( edges[ which ][ 0 ], edges[ which ][ 1 ], edges[ which ][ 2 ] != 0, LEFT, SAVE_LIST_RIGHT, x0, x1 );
	}

	const Int SAVE_DETAILS_LEFT = 1200;
	const IRegion2D SAVE_DETAILS_NAME = { { 1200, 124 }, { 1860, 154 } };
	const IRegion2D SAVE_DETAILS_CAMPAIGN = { { 1200, 160 }, { 1860, 182 } };
	const IRegion2D SAVE_DETAILS_MISSION = { { 1200, 194 }, { 1860, 210 } };
	const Int MISSION_TILES_TOP = 226;
	const Int MISSION_TILE_HEIGHT = 60;
	const Int MISSION_TILE_GAP = 12;
	/// a mission's tile, the campaign's missions across the details column
	inline IRegion2D missionTile( Int count, Int index )
	{
		const Int w = count > 0 ? ( RIGHT - SAVE_DETAILS_LEFT - MISSION_TILE_GAP * ( count - 1 ) ) / count : 0;
		const Int x0 = SAVE_DETAILS_LEFT + index * ( w + MISSION_TILE_GAP );
		return rect( x0, MISSION_TILES_TOP, x0 + w, MISSION_TILES_TOP + MISSION_TILE_HEIGHT );
	}
	const IRegion2D SAVE_PREVIEW = { { 1200, 322 }, { 1600, 722 } };
	const Int SAVE_FACTS_LEFT = 1620;
	const Int SAVE_MISSION_TOP = 740;
	const Int SAVE_DIFFICULTY_TOP = 840;

	const IRegion2D SAVE_BACK = { { 60, KEYS_TOP }, { 270, KEYS_BOTTOM } };
	const IRegion2D SAVE_DELETE = { { 282, KEYS_TOP }, { 462, KEYS_BOTTOM } };
	const IRegion2D SAVE_LOAD = { { SAVE_DETAILS_LEFT, KEYS_TOP }, { RIGHT, KEYS_BOTTOM } };

	//-----------------------------------------------------------------------------------------------
	// the boxes (UIStyle's box: head 82, foot 84, inset 32)
	//-----------------------------------------------------------------------------------------------
	/// Save Replay over the end screen, 1040 wide: NAME and its text box, the saved replays, CANCEL, SAVE;
	/// the heads 22 under the text box and 34 over the list, so text box and list stand 56 apart (8 px grid)
	const Int REPLAY_BOX_ROWS = 7;
	const Int REPLAY_BOX_HEIGHT = 82 + 24 + 48 + 22 + 34 + REPLAY_BOX_ROWS * REPLAY_ROW_HEIGHT + 16 + 84;
	const IRegion2D REPLAY_BOX = { { 440, 252 }, { 1480, 252 + REPLAY_BOX_HEIGHT } };
	const IRegion2D REPLAY_BOX_ENTRY = { { 572, 358 }, { 1448, 406 } };
	const Int REPLAY_BOX_HEADS = 428;
	const IRegion2D REPLAY_BOX_LIST = { { 472, 462 }, { 1448, 462 + REPLAY_BOX_ROWS * REPLAY_ROW_HEIGHT } };
	enum ReplayBoxColumn { RBOX_NAME, RBOX_RECORDED, RBOX_MAP, RBOX_LENGTH, RBOX_COLUMNS };
	inline Column replayBoxColumn( Int which )
	{
		static const Int edges[ RBOX_COLUMNS ][ 3 ] = { { 16, 360, 0 }, { 372, 590, 0 }, { 602, 860, 0 }, { 872, 960, 1 } };
		Column c;
		c.left = REPLAY_BOX_LIST.lo.x + edges[ which ][ 0 ];
		c.right = REPLAY_BOX_LIST.lo.x + edges[ which ][ 1 ];
		c.alignRight = edges[ which ][ 2 ] != 0;
		return c;
	}

	/// Save / Load over a match, 1040 wide: the new save and the saves, DELETE, LOAD GAME, SAVE GAME
	const Int SAVE_BOX_ROWS = 8;
	const Int SAVE_BOX_HEIGHT = 82 + 20 + 34 + SAVE_BOX_ROWS * SAVE_ROW_HEIGHT + 16 + 84;
	const IRegion2D SAVE_BOX = { { 440, 124 }, { 1480, 124 + SAVE_BOX_HEIGHT } };
	const Int SAVE_BOX_HEADS = 226;
	const IRegion2D SAVE_BOX_LIST = { { 472, 260 }, { 1448, 260 + SAVE_BOX_ROWS * SAVE_ROW_HEIGHT } };
	const Int SAVE_BOX_KEY_WIDTH = 190;

	/// the save's description, asked in a question box with a text box in it
	const Int DESC_BOX_HEIGHT = 82 + 24 + 28 + 12 + 48 + 28 + 84;

	/// a box key, slot 0 the rightmost: UIStyle::boxKeyRect1080 at a width of its own, 16 apart (BOX_KEY_GAP)
	inline IRegion2D boxKey( const IRegion2D &box, Int slot, Int width )
	{
		const Int right = box.hi.x - 32 - slot * ( width + 16 );
		return rect( right - width, box.hi.y - 20 - 48, right, box.hi.y - 20 );
	}

	//-----------------------------------------------------------------------------------------------
	// readouts
	//-----------------------------------------------------------------------------------------------
	/** The extra save fields (DIFFICULTY, PLAYED FOR, the challenge's general) cost a second read of
		* every file while the list is built.  The design's fallback: if fifty saves would take over
		* 200 ms, the extras drop.  The budget for a folder of saves, at least fifty saves' worth. */
	inline Int saveExtrasBudgetMs( Int saves )
	{
		return 200 * ( saves > 50 ? saves : 50 ) / 50;
	}

	/// seconds as the lists write a length: 14:05, 1:12:40
	inline void clockText( Int seconds, char *out, Int size )
	{
		if( seconds < 0 )
			seconds = 0;
		const Int h = seconds / 3600, m = ( seconds / 60 ) % 60, s = seconds % 60;
		if( h > 0 )
			snprintf( out, size, "%d:%02d:%02d", h, m, s );
		else
			snprintf( out, size, "%02d:%02d", m, s );
	}

	/// the teams as the replay's details say them: "4 v 4", "1 v 1 v 1"; players with no team stand
	/// alone, each his own side
	inline void teamsText( const std::vector< Int > &teamOfSeat, char *out, Int size )
	{
		std::vector< Int > teams, counts;
		Int alone = 0;
		for( size_t i = 0; i < teamOfSeat.size(); i++ )
		{
			if( teamOfSeat[ i ] < 0 )
			{
				alone++;
				continue;
			}
			size_t t = 0;
			while( t < teams.size() && teams[ t ] != teamOfSeat[ i ] )
				t++;
			if( t == teams.size() )
			{
				teams.push_back( teamOfSeat[ i ] );
				counts.push_back( 0 );
			}
			counts[ t ]++;
		}
		for( Int a = 0; a < alone; a++ )
			counts.push_back( 1 );
		out[ 0 ] = 0;
		Int used = 0;
		for( size_t c = 0; c < counts.size() && used < size; c++ )
			used += snprintf( out + used, size - used, c ? " v %d" : "%d", counts[ c ] );
	}
}

#endif // __SINGLEPLAYERLAYOUT_H_
