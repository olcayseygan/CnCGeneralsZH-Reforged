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

// FILE: LaneWarDirector.h ////////////////////////////////////////////////////////////////////////
//
// Lane War (docs/plans/LANE-MODE.md in the wrapper): the logic system that runs the mode on a map
// the random map generator's lane type wrote.  It switches itself on when the map carries the
// LaneWar_ build plots, middle area and spawn waypoints, and every other game never sees it.
//
// Lane players have no base: no command centre, no workers, nothing to build.  A build plot is a
// grid of square slots (LANEWAR_SLOT_SIZE) in the plot's own frame: the plot stands behind its end of
// the lane, its edge towards the lane is its front, a slot's column counts across that front from the
// player's left as he faces the enemy, and its row counts back from the front.  A unit is bought onto
// it at a slot through MSG_LANEWAR_ORDER (LANEWAR_ORDER_PLACE), covering as many slots a side as its
// footprint needs.  The plot's units stand at their slots: held, unable to fire, taking no damage,
// and no weapon may pick them.  None of that leans on the terrain around the plot.  Every 30 wall
// seconds a spawn tick pays every player and sends one seat's wave from each team, the seats taking
// turns: the plot's units are copied, template and slot place relative to the grid's centre, onto
// the team's spawn pad with the plot's front row in front, facing the enemy, and march down the lane
// as AIGroup::groupLaneMarch sends them.  The copies
// belong to the player and cannot be selected; each dies 130 wall seconds after it spawned.  A team
// loses when its fortress dies, through VictoryConditions, and nothing else counts.
//
///////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#ifndef __LANEWARDIRECTOR_H_
#define __LANEWARDIRECTOR_H_

#include "Common/GameCommon.h"
#include "Common/GameType.h"
#include "Common/Snapshot.h"
#include <vector>

class Object;
class Player;
class ThingTemplate;

enum
{
	LANEWAR_TEAMS = 2,										///< team A starts on the odd starts (1, 3, 5), team B on the even
	LANEWAR_SEATS = 3,										///< seats a team; seat N of a team stands on its Nth start
	LANEWAR_START_MONEY = 2000,						///< every player's cash when the match starts
	LANEWAR_INCOME = 267,									///< paid to every player on every spawn tick
	LANEWAR_MIDDLE_BONUS_PERCENT = 25,		///< on top of the income, for the team holding past the centre line
	LANEWAR_TICK_SECONDS = 30,						///< wall seconds between spawn ticks, the first one included
	LANEWAR_LIFESPAN_SECONDS = 130,				///< wall seconds a wave copy lives
	LANEWAR_SLOT_SIZE = 20,								///< world units a side of one plot slot, two cells; a plot 240 across its front and 160 deep is 12 columns by 8 rows
};

#define LANEWAR_TICK_FRAMES ((UnsignedInt)(LANEWAR_TICK_SECONDS * LOGICFRAMES_PER_SECOND))
#define LANEWAR_LIFESPAN_FRAMES ((UnsignedInt)(LANEWAR_LIFESPAN_SECONDS * LOGICFRAMES_PER_SECOND))

/** What MSG_LANEWAR_ORDER asks for, its first argument; the integers after it depend on it. */
enum LaneWarOrderKind
{
	LANEWAR_ORDER_PLACE = 0,							///< (template id, column, row): buy that unit onto the sender's plot, its block's front left slot at that one
	LANEWAR_ORDER_TIER = 1,								///< (tier): buy an income or tech tier; not built, the buy panel step
};

/// slots a side a unit of this footprint radius covers, one at least
extern Int LaneWar_slotsFor( Real radius );

/** Where a slot of a plot lies in the world, the plot standing at x from loX to hiX and y from loY to
	  hiY with its front towards +x (front +1) or -x (front -1): the middle of the size by size block
	  whose front left slot is (col, row).  Columns run across the front from the left as one faces
	  the front, rows back from it, so the same slot is the same place in the wave whichever end the
	  plot stands at. */
inline Coord2D LaneWar_slotPlace( Real loX, Real loY, Real hiX, Real hiY, Real front, Int col, Int row, Int size )
{
	const Real frontX = front > 0.0f ? hiX : loX;
	const Real leftY = front > 0.0f ? hiY : loY;
	Coord2D at;
	at.x = frontX - front * ( row + 0.5f * size ) * LANEWAR_SLOT_SIZE;
	at.y = leftY - front * ( col + 0.5f * size ) * LANEWAR_SLOT_SIZE;
	return at;
}

/// a block of size by size slots at (x, y) stays inside a grid of cols by rows
inline Bool LaneWar_blockFits( Int x, Int y, Int size, Int cols, Int rows )
{
	return x >= 0 && y >= 0 && x + size <= cols && y + size <= rows;
}

/// two square blocks of slots share at least one slot
inline Bool LaneWar_blocksOverlap( Int ax, Int ay, Int aSize, Int bx, Int by, Int bSize )
{
	return ax < bx + bSize && bx < ax + aSize && ay < by + bSize && by < ay + aSize;
}

/// the team (0 A, 1 B) of a 0-based start position
inline Int LaneWar_teamOfStart( Int start ) { return start % LANEWAR_TEAMS; }
/// the seat (0-based) of a 0-based start position
inline Int LaneWar_seatOfStart( Int start ) { return start / LANEWAR_TEAMS; }

/** The seat of a team whose wave goes out on the given tick (counted from 0): the occupied seats
	  take turns in seat order, an empty seat is passed over, so a lone player spawns on every tick.
	  -1 when nobody sits on that team. */
extern Int LaneWar_seatForTick( const Bool occupied[ LANEWAR_SEATS ], UnsignedInt tick );

/** Who holds the middle: the team with more live wave copies on the enemy's side of the centre
	  line, 0 for A and 1 for B, -1 on a tie (no copies at all included). */
extern Int LaneWar_middleHolder( Int pastLineA, Int pastLineB );

/// what one player is paid on a tick
extern UnsignedInt LaneWar_income( Bool holdsMiddle );

//-------------------------------------------------------------------------------------------------
class LaneWarDirector : public Snapshot
{
public:
	LaneWarDirector( void );

	void reset( void );

	/** Read the map once its objects are in, before the starting buildings would go down.  A new game
		  also gets its money, its alliances by end and its fortresses; a loaded one takes those from
		  the save. */
	void newMap( Bool loadingSaveGame );

	Bool isActive( void ) const { return m_active; }

	/// once a logic frame, from GameLogic::update
	void update( void );

	/// VictoryConditions under VICTORY_LANEWAR: the player's team has lost its fortress
	Bool hasLostFortress( const Player *player ) const;

	/// MSG_LANEWAR_ORDER from TheCommandList: a LaneWarOrderKind and its integers
	void onOrder( Player *player, Int kind, const Int *args, Int argCount );

	/** Buy a unit of the template onto the player's own plot with its block's front left slot at
		  (col, row): refused unless the player is seated, the template is a unit, the block fits inside
		  his plot's grid, no unit of his already covers any of its slots and he can pay for it.  The
		  one entry point for the buy panel, the bot and the scenario verb laneplace. */
	Bool placeUnit( Player *player, const ThingTemplate *tmpl, Int col, Int row );

	/** A plot's corners in the world, for the client's slot grid; FALSE off a lane map.  Read once the
		  map is in: they never change during a match. */
	Bool getPlotBounds( Int team, Int seat, Coord2D *lo, Coord2D *hi ) const;

protected:
	virtual void crc( Xfer *xfer );
	virtual void xfer( Xfer *xfer );
	virtual void loadPostProcess( void ) { }

private:
	struct Copy
	{
		ObjectID id;
		UnsignedInt dieFrame;
	};

	struct PlotUnit
	{
		ObjectID id;
		Int team, seat;
		Int col, row, size;
	};

	Bool seatOf( const Player *player, Int *team, Int *seat ) const;
	Real towardEnemy( Int team ) const;
	Coord3D slotCentre( Int team, Int seat, Int col, Int row, Int size ) const;
	void expireCopies( UnsignedInt now );
	void tick( UnsignedInt now );
	void spawnWave( Int team, Int seat, UnsignedInt now );

	// read from the map on every load, so not saved
	Bool m_active;
	Coord2D m_plotLo[ LANEWAR_TEAMS ][ LANEWAR_SEATS ];			///< the plot's low x, low y corner
	Coord2D m_plotHi[ LANEWAR_TEAMS ][ LANEWAR_SEATS ];
	Int m_gridCols[ LANEWAR_TEAMS ][ LANEWAR_SEATS ];				///< slots across the front
	Int m_gridRows[ LANEWAR_TEAMS ][ LANEWAR_SEATS ];				///< slots from the front back
	Coord3D m_plotAnchor[ LANEWAR_TEAMS ][ LANEWAR_SEATS ];	///< the grid's centre, what a wave copy is placed relative to
	Coord3D m_spawn[ LANEWAR_TEAMS ];
	Coord3D m_middle;

	// saved and CRC'd
	Int m_seatPlayer[ LANEWAR_TEAMS ][ LANEWAR_SEATS ];		///< player index, -1 for an empty seat
	ObjectID m_fortress[ LANEWAR_TEAMS ];
	ObjectID m_tower[ LANEWAR_TEAMS ][ 2 ];								///< LaneWar_TowerA1, A2, B1, B2; INVALID_ID where the map has none
	Bool m_fortressDown[ LANEWAR_TEAMS ];
	UnsignedInt m_nextTickFrame;
	UnsignedInt m_tickCount;
	std::vector<Copy> m_copies;														///< in spawn order, so dieFrame never falls
	std::vector<PlotUnit> m_plotUnits;										///< in purchase order, which is ObjectID order
};

extern LaneWarDirector *TheLaneWarDirector;

#endif // __LANEWARDIRECTOR_H_
