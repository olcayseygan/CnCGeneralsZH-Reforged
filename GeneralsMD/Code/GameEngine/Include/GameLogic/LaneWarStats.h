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

// FILE: LaneWarStats.h ///////////////////////////////////////////////////////////////////////////
//
// A Lane War match's record and its end.  Logic state, saved and CRC'd beside LaneWarDirector: what
// each seat sent, bought, earned, killed and broke, how long its team held the middle, a timeline
// sampled once a wall second with the frames the towers fell, and the end of the match.
//
// The end: a fortress that dies does not end the match on that frame.  The team is out once its
// fortress has had LANEWAR_COLLAPSE_SECONDS of game time to come down (the fracture's baked collapse
// is about four seconds after half a second's pause), and only then does VictoryConditions see it,
// so the decision, the end frame and every report of the match fall after the collapse.  The client
// shows its own end for it (getEndPhase), not the stock Victory and Defeat windows and score screen.
//
// Hooks, each a line in the code that already does the thing: LaneWarDirector's placeUnit, onOrder,
// tick and spawnWave for the economy and the waves, Object::scoreTheKill for kills and
// ActiveBody::attemptDamage for damage to the towers and fortresses.  A wave unit is any unit of a
// seated player but his own towers and fortress: his plot units cannot fire, so what kills is a wave
// copy, a passenger or drone riding with one, or a jet of his squadron.
//
///////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#ifndef __LANEWARSTATS_H_
#define __LANEWARSTATS_H_

#include "Common/GameCommon.h"
#include "Common/Snapshot.h"
#include "GameLogic/LaneWarDirector.h"
#include <vector>

class Object;
class Player;
class ThingTemplate;

enum LaneWarSpendKind
{
	LANEWAR_SPEND_UNITS = 0,						///< units bought onto the plot
	LANEWAR_SPEND_TIERS,								///< income and tech tiers
	LANEWAR_SPEND_UPGRADES,							///< player upgrades and a plot unit's object upgrades
	LANEWAR_SPEND_RANKS,								///< ranks on plot units and passengers
	LANEWAR_SPEND_PASSENGERS,						///< infantry loaded into a plot unit
	LANEWAR_SPEND_KINDS
};

enum LaneWarEarnKind
{
	LANEWAR_EARN_INCOME = 0,						///< the tick's pay without the middle's bonus
	LANEWAR_EARN_MIDDLE,								///< the bonus for the team holding the middle
	LANEWAR_EARN_REFUNDS,								///< what sales gave back
	LANEWAR_EARN_KINDS
};

enum LaneWarEndPhase
{
	LANEWAR_END_PLAYING = 0,						///< both fortresses stand
	LANEWAR_END_COLLAPSE,								///< a fortress is down and coming apart; the match is not decided yet
	LANEWAR_END_OVER										///< decided: the collapse has had its time
};

enum { LANEWAR_COLLAPSE_SECONDS = 5 };	///< game seconds a dead fortress has to fall before its team is out
#define LANEWAR_COLLAPSE_FRAMES ((UnsignedInt)(LANEWAR_COLLAPSE_SECONDS * LOGICFRAMES_PER_GAME_SECOND))

/// how many of one template, in the order each was first counted
struct LaneWarTemplateCount
{
	const ThingTemplate *thing;
	Int count;
};

/// one seat's match
struct LaneWarSeatStats
{
	Int waves;													///< spawn ticks that sent at least one unit of his
	Int unitsSent;											///< wave copies of his plot units, jets and passengers left out
	Int placed;													///< units bought onto his plot
	std::vector<LaneWarTemplateCount> placedBy;
	Int earned[ LANEWAR_EARN_KINDS ];
	Int spent[ LANEWAR_SPEND_KINDS ];
	Int kills;													///< enemies his wave units killed
	std::vector<LaneWarTemplateCount> killsBy;	///< by the killer's template, for the MVP unit
	Real towerDamage;										///< health his wave units took off the enemy's towers
	Real fortressDamage;								///< and off the enemy fortress
	UnsignedInt middleFrames;						///< logic frames his team held the middle with a unit of his past the line

	Int totalEarned( void ) const;
	Int totalSpent( void ) const;
	/// the template with the most kills, the first counted on a tie; NULL with none
	const ThingTemplate *mvp( Int *kills ) const;
};

/// the match once a wall second (and on the frames a fortress fell and the match was decided)
struct LaneWarSample
{
	UnsignedInt frame;
	Int middleHolder;										///< the team the director last paid for the middle, -1 nobody
	Int middle;													///< -100 to 100: team A's share of the wave units past the line less team B's, in percent
	Int fortress[ LANEWAR_TEAMS ];			///< health, 0 once down
	Int tower[ LANEWAR_TEAMS ][ 2 ];		///< health of LaneWar_TowerA1, A2, B1, B2; 0 down or where the map has none
};

//-------------------------------------------------------------------------------------------------
class LaneWarStats : public Snapshot
{
public:
	LaneWarStats( void );

	void reset( void );

	/// once a logic frame, from GameLogic::update right after the director
	void update( void );

	// hooks
	void onPlace( const Player *player, const ThingTemplate *tmpl, Int cost );
	/// every MSG_LANEWAR_ORDER but a placement: what it took from or gave back to the player's money
	void onOrder( const Player *player, Int kind, UnsignedInt moneyBefore, UnsignedInt moneyAfter );
	void onIncome( Int playerIndex, UnsignedInt pay, UnsignedInt bonus );
	void onWave( Int playerIndex, Int units );
	void onKill( const Object *killer, const Object *victim );
	void onDamage( const Object *damager, const Object *victim, Real amount );

	/** MSG_SELF_DESTRUCT in a Lane War match (GameLogicDispatch), or a kick vote carried (GameLogic's
		* kickPlayer): the player left, by Exit, Surrender, a dropped connection or a kick, on a frame the
		* command list fixes for every machine.  The team's fortress
		* and towers he held go to a teammate still playing; when nobody of the team plays on, the team is
		* out on this frame, with no collapse to wait for.  A computer seat never leaves. */
	void onPlayerLeft( const Player *player );

	/// VictoryConditions under VICTORY_LANEWAR: the player's team lost its fortress and the collapse has had its time, or left
	Bool hasLost( const Player *player ) const;

	// Read only, for the client at the render's sync point (LaneWarEndScreen.cpp)
	Int getEndPhase( void ) const;
	/// the frame the team's fortress was found dead, 0 while it stands
	UnsignedInt getFortressDownFrame( Int team ) const { return m_downFrame[ team ]; }
	/// the frame the team's tower (0 or 1) was found dead, 0 while it stands or where the map has none
	UnsignedInt getTowerDownFrame( Int team, Int k ) const { return m_towerDownFrame[ team ][ k ]; }
	/// the frame the last of the team's seated players left, 0 while one plays on
	UnsignedInt getTeamLeftFrame( Int team ) const { return m_teamLeftFrame[ team ]; }
	/// the frame the match is decided on, 0 while no fortress is down and no team has left
	UnsignedInt getEndFrame( void ) const;
	/// the team is out of the match decided on end: its fortress fell before it, or the team left by it
	Bool isOut( Int team, UnsignedInt end ) const;
	/// the team left standing once the match is over; -1 before, or when both fortresses fell
	Int getWinner( void ) const;
	const LaneWarSeatStats &getSeat( Int team, Int seat ) const { return m_seats[ team ][ seat ]; }
	UnsignedInt getTeamMiddleFrames( Int team ) const { return m_teamMiddleFrames[ team ]; }
	Int getSampleCount( void ) const { return (Int)m_samples.size(); }
	const LaneWarSample &getSample( Int i ) const { return m_samples[ i ]; }

	/// the whole record as HEADLESS LANEWAR STATS lines
	void log( void ) const;

protected:
	virtual void crc( Xfer *xfer );
	virtual void xfer( Xfer *xfer );
	virtual void loadPostProcess( void ) { }

private:
	Bool counting( void ) const;
	LaneWarSeatStats *seatStats( const Player *player );
	Bool isWaveUnit( const Object *obj, Int *team ) const;
	void sample( UnsignedInt now, Int middle );

	// saved and CRC'd
	LaneWarSeatStats m_seats[ LANEWAR_TEAMS ][ LANEWAR_SEATS ];
	UnsignedInt m_teamMiddleFrames[ LANEWAR_TEAMS ];
	std::vector<LaneWarSample> m_samples;
	UnsignedInt m_towerDownFrame[ LANEWAR_TEAMS ][ 2 ];
	UnsignedInt m_downFrame[ LANEWAR_TEAMS ];
	UnsignedInt m_seatLeftFrame[ LANEWAR_TEAMS ][ LANEWAR_SEATS ];	///< 0 while the seat plays
	UnsignedInt m_teamLeftFrame[ LANEWAR_TEAMS ];
	Bool m_logged;												///< the record went to the log when the match was decided
};

extern LaneWarStats *TheLaneWarStats;

#endif // __LANEWARSTATS_H_
