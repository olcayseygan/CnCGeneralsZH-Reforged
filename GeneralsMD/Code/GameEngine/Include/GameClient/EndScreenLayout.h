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

// FILE: EndScreenLayout.h ////////////////////////////////////////////////////////////////////////
//
// The end-of-match screens in the menu language (MENU-REDESIGN.md package 13, the design in
// experiments/proofs/2026-11-24/spirit/c-bridge/README-end3.md): the rects of frame A's header, the
// two wings, the foot and the key row, how the Summary's rows are cut for 2 to 32 players, the
// standings' columns, and the build order's folded time axis.  Pure, on the 1920x1080 board, so a test
// walks every team split through them; ScoreScreenGraphs.cpp, ScoreScreen.cpp and
// DirectStrikeEndScreen.cpp draw them.
////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#ifndef _ENDSCREENLAYOUT_H_
#define _ENDSCREENLAYOUT_H_

#include "Lib/BaseType.h"

#include <algorithm>
#include <vector>

namespace EndScreen
{
	// frame A's panel and the header, every number at 1080
	const Int PANEL_LEFT = 32;
	const Int PANEL_TOP = 28;
	const Int PANEL_RIGHT = 1888;
	const Int PANEL_BOTTOM = 1052;
	const Int INSET_LEFT = 60;
	const Int INSET_RIGHT = 1860;
	const Int TITLE_TOP = 46;
	const Int TITLE_BOTTOM = 94;
	const Int TABS_TOP = 50;
	const Int TAB_WIDTH = 156;
	const Int TAB_HEIGHT = 38;
	const Int TAB_GAP = 8;
	const Int FACTS_TOP = 98;
	const Int FACTS_BOTTOM = 120;
	const Int RULE_Y = 130;						///< the lit 2 px FRAME rule
	const Int SHARE_TOP = 133;				///< the share line under it, 2 px
	const Int SHARE_BOTTOM = 135;
	const Int CONTENT_TOP = 146;

	// the two wings of the Summary and the build order at more than sixteen
	const Int WING_LEFT_X0 = 60;
	const Int WING_LEFT_X1 = 932;
	const Int WING_RIGHT_X0 = 988;
	const Int WING_RIGHT_X1 = 1860;

	// the foot and the keys on its last line, where the menu language keeps its key row
	const Int FOOT_GAP = 8;
	const Int FOOT_LINE = 132;
	const Int FOOT_WIDE = 260;
	const Int FOOT_PAD = 12;					///< the foot's content under its rule
	const Int FOOT_BOTTOM = 1036;			///< and its foot
	const Int PAGE_FOOT_RULE = 962;		///< Graphs and Build order: the faint rule over the key row
	const Int KEYS_TOP = 976;
	const Int KEYS_BOTTOM = 1028;
	const Int KEY_PRIMARY = 300;
	const Int KEY_SECONDARY = 150;
	const Int KEY_GAP = 16;

	// the Summary's heads and rows
	const Int TEAM_HEAD = 98;					///< two teams: bar, name, verdict, score, column heads, rule
	const Int WING_HEAD = 46;					///< several teams or a free for all: PLACES a-b and the heads
	const Int TOTALS = 36;						///< two teams of more than one: the TOTAL line
	const Int LINE_ROW_MAX = 45;
	const Int BAND = 34;							///< a team's band over its line rows
	const Int BAND_TIGHT = 28;				///< when the lines would fall under LINE_ROW_TIGHT
	const Int LINE_ROW_TIGHT = 32;
	const Int CARD_ROWS_MOST = 4;			///< up to four a team stand as cards
	const Int WIDE_FOOT_MOST = 2;			///< up to two a team get the wide foot
	const Int DENSE_FROM = 16;				///< sixteen a wing and more narrow the columns

	// the Graphs page: the chips' row, the plot and the legend
	const Int CHIP_HEIGHT = 34;
	const Int PLOT_TOP = 200;
	const Int PLOT_BOTTOM = 924;
	const Int PLOT_LEGEND_GAP = 36;

	// the build order: the axis's labels over the tracks, the name block and the pictures
	const Int TRACKS_TOP = 206;
	const Int TRACKS_BOTTOM = 940;
	const Int NAME_BLOCK = 260;
	const Int NAME_BLOCK_TIGHT = 190;		///< two wings at more than sixteen
	const Int BUILD_PICTURE_MOST = 64;
	const Int BUILD_LANES_MOST = 5;
	const Int FOLD_PX = 34;

	/// what the Summary's rows are
	enum RowKind
	{
		ROWS_CARD,				///< one to four a team, two teams (or one): picture, name, army chart, figures
		ROWS_LINE,				///< five to sixteen a team, two teams: a stripe, the name, the figures
		ROWS_BANDED,			///< several teams: a band per team of two or more over its line rows
		ROWS_PLACES				///< a free for all: a line per player with his place
	};

	struct SummaryPlan
	{
		RowKind kind;
		Int foot;									///< FOOT_LINE or FOOT_WIDE
		Int head;									///< TEAM_HEAD or WING_HEAD
		Bool totals;							///< the TOTAL line under each team
		Bool single;							///< one team alone: its wing runs the whole width
		Int rowHeight;
		Int bandHeight;						///< ROWS_BANDED only
		std::vector< Int > wing;	///< per team in standings order: 0 the left wing, 1 the right
		Int rows[ 2 ];						///< rows in each wing (a band not counted)
		Int bands[ 2 ];						///< bands in each wing
	};

	/// where the foot starts for a foot that tall
	inline Int footTop( Int foot )
	{
		return PANEL_BOTTOM - FOOT_GAP - foot;
	}

	/// a ROWS_BANDED team's band: only a team of two or more; a player alone is his own team's line
	inline Bool bandedHasBand( Int teamSize )
	{
		return teamSize >= 2;
	}

	/** The Summary for teams of these sizes in standings order.  A row is whatever the content area
		* divided by the rows gives, line rows capped at LINE_ROW_MAX: nothing is left to a grid.  Two
		* teams stand one a wing; several teams are cut into the wings where the taller wing comes out
		* shortest, standings order kept; a free for all puts the first half of the places on the left. */
	inline SummaryPlan summaryPlan( const std::vector< Int > &teamSizes, Bool freeForAll )
	{
		SummaryPlan plan;
		plan.kind = ROWS_LINE;
		plan.foot = FOOT_LINE;
		plan.head = WING_HEAD;
		plan.totals = FALSE;
		plan.single = FALSE;
		plan.rowHeight = LINE_ROW_MAX;
		plan.bandHeight = 0;
		plan.rows[ 0 ] = plan.rows[ 1 ] = 0;
		plan.bands[ 0 ] = plan.bands[ 1 ] = 0;
		const Int teams = (Int)teamSizes.size();
		plan.wing.assign( teams, 0 );
		if( teams == 0 )
			return plan;

		Int biggest = 0;
		for( Int t = 0; t < teams; t++ )
			biggest = max( biggest, teamSizes[ t ] );

		if( freeForAll )
		{
			plan.kind = ROWS_PLACES;
			const Int left = ( teams + 1 ) / 2;
			for( Int t = 0; t < teams; t++ )
			{
				plan.wing[ t ] = t < left ? 0 : 1;
				plan.rows[ plan.wing[ t ] ] += teamSizes[ t ];
			}
			const Int room = footTop( plan.foot ) - CONTENT_TOP - plan.head;
			plan.rowHeight = min( LINE_ROW_MAX, room / max( max( plan.rows[ 0 ], plan.rows[ 1 ] ), 1 ) );
			return plan;
		}

		if( teams <= 2 )
		{
			plan.single = teams == 1;
			plan.kind = biggest <= CARD_ROWS_MOST ? ROWS_CARD : ROWS_LINE;
			plan.foot = biggest <= WIDE_FOOT_MOST ? FOOT_WIDE : FOOT_LINE;
			plan.head = TEAM_HEAD;
			plan.totals = biggest > 1;
			for( Int t = 0; t < teams; t++ )
			{
				plan.wing[ t ] = t;
				plan.rows[ t ] = teamSizes[ t ];
			}
			const Int room = footTop( plan.foot ) - CONTENT_TOP - plan.head - ( plan.totals ? TOTALS : 0 );
			plan.rowHeight = room / max( biggest, 1 );
			if( plan.kind == ROWS_LINE )
				plan.rowHeight = min( plan.rowHeight, LINE_ROW_MAX );
			return plan;
		}

		// several teams: a team costs its players and its band; the cut leaves the taller wing shortest
		plan.kind = ROWS_BANDED;
		std::vector< Int > cost( teams, 0 );
		Int total = 0;
		for( Int t = 0; t < teams; t++ )
		{
			cost[ t ] = teamSizes[ t ] + ( bandedHasBand( teamSizes[ t ] ) ? 1 : 0 );
			total += cost[ t ];
		}
		Int cut = 1, tallest = total + 1, left = 0;
		for( Int k = 1; k < teams; k++ )
		{
			left += cost[ k - 1 ];
			const Int tall = max( left, total - left );
			if( tall < tallest )
			{
				tallest = tall;
				cut = k;
			}
		}
		for( Int t = 0; t < teams; t++ )
		{
			plan.wing[ t ] = t < cut ? 0 : 1;
			plan.rows[ plan.wing[ t ] ] += teamSizes[ t ];
			if( bandedHasBand( teamSizes[ t ] ) )
				plan.bands[ plan.wing[ t ] ]++;
		}
		const Int room = footTop( plan.foot ) - CONTENT_TOP - plan.head;
		plan.bandHeight = BAND;
		for( Int pass = 0; pass < 2; pass++ )
		{
			plan.rowHeight = LINE_ROW_MAX;
			for( Int w = 0; w < 2; w++ )
			{
				if( plan.rows[ w ] > 0 )
					plan.rowHeight = min( plan.rowHeight, ( room - plan.bands[ w ] * plan.bandHeight ) / plan.rows[ w ] );
			}
			if( plan.rowHeight >= LINE_ROW_TIGHT )
				break;
			plan.bandHeight = BAND_TIGHT;
		}
		return plan;
	}

	/// the picture of a card row that tall
	inline Int cardPicture( Int rowHeight )
	{
		return min( 180, rowHeight - 20 );
	}

	/// the Summary's columns, in the standings' order: score, units built, lost, destroyed, buildings
	/// built, lost, destroyed, money earned, spent
	enum { COLUMNS = 9 };

	/** Each column's left and right on the board, right to left from right - 8: money 76 and 76,
		* buildings 36, 36, 48, units 50, 46, 54, the score 80; figures 10 apart inside a group, groups 22
		* apart.  dense (sixteen a wing and more) takes every width times 0.9 and groups 16 apart. */
	inline void summaryColumns( Int right, Bool dense, Int left[ COLUMNS ], Int rightEdge[ COLUMNS ] )
	{
		static const Int widths[ COLUMNS ] = { 80, 50, 46, 54, 36, 36, 48, 76, 76 };
		static const Int group[ COLUMNS ] = { 0, 1, 1, 1, 2, 2, 2, 3, 3 };
		const Int inside = dense ? 9 : 10;
		const Int between = dense ? 16 : 22;
		Int x = right - 8;
		for( Int col = COLUMNS - 1; col >= 0; col-- )
		{
			if( col < COLUMNS - 1 )
				x -= group[ col ] != group[ col + 1 ] ? between : inside;
			rightEdge[ col ] = x;
			x -= dense ? widths[ col ] * 9 / 10 : widths[ col ];
			left[ col ] = x;
		}
	}

	/** The keys on the key row, right to left: the primary KEY_PRIMARY wide, then each other
		* KEY_SECONDARY.  index 0 is the primary; returns the key's left and right on the board. */
	inline void keyRect( Int index, Int &x0, Int &x1 )
	{
		x1 = INSET_RIGHT;
		if( index > 0 )
			x1 -= KEY_PRIMARY + KEY_GAP + ( index - 1 ) * ( KEY_SECONDARY + KEY_GAP );
		x0 = x1 - ( index == 0 ? KEY_PRIMARY : KEY_SECONDARY );
	}

	/// the foot's map square, its left at INSET_LEFT under the foot's rule
	inline Int footMap( Int foot )
	{
		return foot - 2 * FOOT_PAD + 4;
	}

	/// where the foot's middle (highlights, the chat, the rating card) starts and ends with keys keys
	inline Int footMiddleLeft( Int foot )
	{
		return INSET_LEFT + footMap( foot ) + ( foot == FOOT_WIDE ? 250 : 200 );
	}

	inline Int footMiddleRight( Int keys )
	{
		Int x0, x1;
		keyRect( max( keys, 1 ) - 1, x0, x1 );
		return x0 - 24;
	}

	/// the Graphs page: the legend's left edge (two columns of sixteen at more than sixteen players)
	inline Int legendLeft( Int players )
	{
		return players > 16 ? 1240 : 1440;
	}

	//-------------------------------------------------------------------------------------------------
	// The build order's time axis covers only what happened.  Every build keeps margin frames of room
	// round it, the match's start and the axis's end are kept too, and stretches that come within quiet
	// frames of each other are joined.  A longer quiet stretch, when nobody on the page built anything,
	// is folded to foldPx.  Every kept stretch runs at one scale, so no time is drawn faster than any
	// other.
	//-------------------------------------------------------------------------------------------------
	struct AxisStretch
	{
		UnsignedInt from;
		UnsignedInt to;
		Real x0;
		Real x1;
	};

	/// the axis's end: the last build rounded up to the minute after it with room seconds of room
	inline UnsignedInt buildAxisEnd( UnsignedInt lastBuild, UnsignedInt framesPerSecond, UnsignedInt roomSeconds )
	{
		const UnsignedInt minute = 60 * framesPerSecond;
		const UnsignedInt reach = lastBuild + roomSeconds * framesPerSecond;
		return max( minute, ( reach + minute - 1 ) / minute * minute );
	}

	inline std::vector< AxisStretch > foldAxis( const std::vector< UnsignedInt > &builds, UnsignedInt end, UnsignedInt margin,
		UnsignedInt quiet, Int foldPx, Int left, Int width )
	{
		std::vector< AxisStretch > kept;
		std::vector< UnsignedInt > points( builds );
		points.push_back( 0 );
		points.push_back( end );
		std::sort( points.begin(), points.end() );
		for( size_t each = 0; each < points.size(); each++ )
		{
			const UnsignedInt at = min( points[ each ], end );
			AxisStretch stretch;
			stretch.from = at > margin ? at - margin : 0;
			stretch.to = min( at + margin, end );
			stretch.x0 = stretch.x1 = 0.0f;
			if( !kept.empty() && stretch.from <= kept.back().to + quiet )
				kept.back().to = max( kept.back().to, stretch.to );
			else
				kept.push_back( stretch );
		}
		UnsignedInt keptFrames = 0;
		for( size_t each = 0; each < kept.size(); each++ )
			keptFrames += kept[ each ].to - kept[ each ].from;
		const Int folds = (Int)kept.size() - 1;
		const Real scale = keptFrames > 0 ? (Real)max( width - folds * foldPx, 1 ) / (Real)keptFrames : 0.0f;
		Real x = (Real)left;
		for( size_t each = 0; each < kept.size(); each++ )
		{
			if( each > 0 )
				x += (Real)foldPx;
			kept[ each ].x0 = x;
			x += (Real)( kept[ each ].to - kept[ each ].from ) * scale;
			kept[ each ].x1 = x;
		}
		return kept;
	}

	/// a frame on a folded axis: inside its stretch at the one scale, in a fold at the fold's middle
	inline Real axisX( const std::vector< AxisStretch > &axis, UnsignedInt frame )
	{
		if( axis.empty() )
			return 0.0f;
		for( size_t each = 0; each < axis.size(); each++ )
		{
			const AxisStretch &s = axis[ each ];
			if( frame <= s.to )
			{
				if( frame >= s.from )
				{
					const UnsignedInt span = s.to - s.from;
					return span > 0 ? s.x0 + ( s.x1 - s.x0 ) * (Real)( frame - s.from ) / (Real)span : s.x0;
				}
				return each == 0 ? s.x0 : ( axis[ each - 1 ].x1 + s.x0 ) * 0.5f;
			}
		}
		return axis.back().x1;
	}

	/// whether a frame falls inside a kept stretch (a tick there is drawn; one in a fold is not)
	inline Bool axisKeeps( const std::vector< AxisStretch > &axis, UnsignedInt frame )
	{
		for( size_t each = 0; each < axis.size(); each++ )
		{
			if( frame >= axis[ each ].from && frame <= axis[ each ].to )
				return TRUE;
		}
		return FALSE;
	}

	/** A build's lane among lanes of pictures width wide: the first that is clear at x, else the one
		* that ends soonest, where it overlaps the last picture at its true time.  laneRight holds where
		* each lane's last picture ends, and is moved on. */
	inline Int buildLane( std::vector< Int > &laneRight, Int x, Int width, Int gap )
	{
		Int lane = 0;
		for( Int l = 0; l < (Int)laneRight.size(); l++ )
		{
			if( x >= laneRight[ l ] )
			{
				lane = l;
				break;
			}
			if( laneRight[ l ] < laneRight[ lane ] )
				lane = l;
		}
		laneRight[ lane ] = max( laneRight[ lane ], x + width + gap );
		return lane;
	}

	//-------------------------------------------------------------------------------------------------
	// Direct Strike's end: two wings, a team head each, a row a seat, the timeline under them
	//-------------------------------------------------------------------------------------------------
	const Int DS_TIMELINE_GAP = 24;
	const Int DS_TIMELINE_BOTTOM = 950;		///< over the faint rule of the key row

	/// a Direct Strike row's height: 128 for one a team, 104 for two or three
	inline Int directStrikeRowHeight( Int seatsPerTeam )
	{
		return seatsPerTeam <= 1 ? 128 : 104;
	}

	/// a seat's seven figures: waves, units placed, earned, spent, kills, damage (the lead), the middle held
	enum { DS_COLUMNS = 7, DS_LEAD_COLUMN = 5 };

	/// each figure's left and right on the board, right to left from right - 8, 14 apart
	inline void directStrikeColumns( Int right, Int left[ DS_COLUMNS ], Int rightEdge[ DS_COLUMNS ] )
	{
		static const Int widths[ DS_COLUMNS ] = { 56, 60, 84, 84, 60, 104, 70 };
		Int x = right - 8;
		for( Int col = DS_COLUMNS - 1; col >= 0; col-- )
		{
			rightEdge[ col ] = x;
			x -= widths[ col ];
			left[ col ] = x;
			x -= 14;
		}
	}

	/// where the timeline starts under seatsPerTeam rows
	inline Int directStrikeTimelineTop( Int seatsPerTeam )
	{
		return CONTENT_TOP + TEAM_HEAD + max( seatsPerTeam, 1 ) * directStrikeRowHeight( seatsPerTeam ) + DS_TIMELINE_GAP;
	}
}

#endif // _ENDSCREENLAYOUT_H_
