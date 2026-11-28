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

// FILE: DirectStrikeBot.h /////////////////////////////////////////////////////////////////////////////
//
// The Direct Strike computer seat (docs/plans/DIRECT-STRIKE.md in the wrapper).  The skirmish AI stands down in
// a lane game (AIPlayer::update), so a computer seat would buy nothing; this buys for it.  It runs
// inside the logic frame, from DirectStrikeDirector::update, and spends through DirectStrikeDirector::onOrder,
// the entry point a player's MSG_DIRECTSTRIKE_ORDER reaches, so it can buy nothing a player could not.  It
// keeps no state of its own: every decision is read off the director, the players' money, the seat's
// AI rating and the logic random stream, so a save, a replay and a second machine all see the same
// purchases.
//
// How well it plays is the seat's AI rating, an Elo (AI.h): every number of its plan is one curve of
// the rating through the lobby's four levels, Easy at 800, Medium at 1300, Hard at 1800 and Brutal at
// 2200, and on past them, so any rating plays its own strength and a higher one plays better.
//
///////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#ifndef __DIRECTSTRIKEBOT_H_
#define __DIRECTSTRIKEBOT_H_

#include "Common/GameCommon.h"
#include "GameLogic/DirectStrikeDirector.h"

/** What a seat of one rating does.  A tick is a spawn tick counted from 0; a tick at or past
	  DIRECTSTRIKE_BOT_NEVER_TICK means never. */
struct DirectStrikeBotPlan
{
	Real rating;
	UnsignedInt stepFrames;													///< logic frames between two of its decisions
	Int reserve;																		///< money kept back, except in the last seconds before its own wave
	Real incomeTick[ DIRECTSTRIKE_INCOME_TIERS ];				///< from which tick it wants income level 1, 2, 3, 4
	Real techTick[ DIRECTSTRIKE_TECH_TIERS - 1 ];				///< ... tech tier 2 and 3
	Real heroTick;
	Real jetTick;
	Real extrasTick;																///< ranks, unit upgrades and player upgrades
	Real moveTick;																	///< from which tick it moves a unit that stands outside its role's rows back into them
	Real sellTick;																	///< ... sells a unit that trades badly against the enemy plots for a better one
	Real counterWeight;															///< how much the enemy plots' mix decides a pick, 0 not at all, 1 wholly
	Real jitter;																		///< the random share of a pick
	Bool saves;																			///< waits for what it wants rather than spending on something else
};

enum { DIRECTSTRIKE_BOT_NEVER_TICK = 40 };		///< twenty minutes of ticks: what a plan leaves to never

/// the plan of a seat at that AI rating
extern void DirectStrikeBot_planAt( Real rating, DirectStrikeBotPlan *plan );

/** Whether the plan wants the next step of a track (DIRECTSTRIKE_TRACK_INCOME or DIRECTSTRIKE_TRACK_TECH) by now:
	  `level` is the one to be bought (income 1 to 4, tech 2 or 3), `tick` the spawn ticks so far.  Never an income
	  level that would pay its price back after a typical match has ended. */
extern Bool DirectStrikeBot_wantsTier( const DirectStrikeBotPlan &plan, Int track, Int level, UnsignedInt tick );

/** The rows a unit of `size` slots tries, best first, for its role in a grid `rows` deep (row 0 is the
	  front).  The grid splits in three bands, the front and back thirds and the middle between them, and
	  a role's own band comes first: the front from the front back, the back from the back forward, the
	  middle from its front.  Then the other rows, nearest the band first.  Returns how many it wrote;
	  *inBand is how many of them lie in the role's band. */
enum { DIRECTSTRIKE_BOT_FRONT, DIRECTSTRIKE_BOT_MIDDLE, DIRECTSTRIKE_BOT_BACK };
extern Int DirectStrikeBot_rowOrder( Int role, Int rows, Int size, Int *out, Int maxItems, Int *inBand );
/// the columns a block of `size` tries in a grid `cols` across, from the middle outwards, left first
extern Int DirectStrikeBot_columnOrder( Int cols, Int size, Int *out, Int maxItems );

/// once a logic frame from DirectStrikeDirector::update, while no fortress is down: every computer seat's turn to spend
extern void DirectStrikeBot_update( DirectStrikeDirector *director, UnsignedInt now );

#endif // __DIRECTSTRIKEBOT_H_
