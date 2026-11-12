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

// FILE: MatchHistory.h ///////////////////////////////////////////////////////////////////////////
//
// What the score screen's graphs are drawn from: every player's money, army, buildings, power and
// losses every few seconds of logic time, the moments a superweapon went up or was fired, a
// general's power used, a promotion, and the order each player built things in.
//
// The logic writes it on every machine and in every replay, so the graphs agree everywhere, and
// nothing in the logic ever reads it back.  It is in no CRC and no save game.  It is bounded: the
// samples halve their own rate when the table fills, and the event and build lists stop growing at
// a cap, so a three-hour match costs the same few hundred kilobytes as a ten-minute one.
////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#ifndef __MATCHHISTORY_H__
#define __MATCHHISTORY_H__

#include "Lib/BaseType.h"
#include "Common/GameCommon.h"

#include <vector>

class Object;
class Player;
class SpecialPowerTemplate;
class ThingTemplate;

enum MatchSeries
{
	MATCH_SERIES_EARNED,			///< money earned so far
	MATCH_SERIES_BANK,				///< money in the bank
	MATCH_SERIES_ARMY,				///< fighting units standing: infantry, vehicles, aircraft; workers left out
	MATCH_SERIES_BUILDINGS,		///< structures standing
	MATCH_SERIES_POWER,				///< power produced
	MATCH_SERIES_LOST,				///< units lost so far
	MATCH_SERIES_COUNT
};

enum MatchEventType
{
	MATCH_EVENT_SUPERWEAPON_BUILT,
	MATCH_EVENT_SUPERWEAPON_FIRED,
	MATCH_EVENT_POWER,				///< a general's power, one a promotion bought
	MATCH_EVENT_RANK,					///< a promotion; the rank is in MatchEvent::rank
	MATCH_EVENT_TYPE_COUNT
};

struct MatchEvent
{
	UnsignedInt frame;
	Int player;
	MatchEventType type;
	const ThingTemplate *thing;							///< the superweapon built, or what fired the power
	const SpecialPowerTemplate *power;			///< the power fired, NULL for a build or a promotion
	Int rank;
};

struct MatchBuild
{
	UnsignedInt frame;
	const ThingTemplate *thing;
};

class MatchHistory
{
public:
	enum
	{
		MAX_SAMPLES = 360,				///< 30 minutes at the first rate; then every other sample goes and the rate halves
		MAX_EVENTS = 512,
		MAX_BUILDS = 200					///< per player: every structure, and the first of each unit type
	};
	#define MATCH_HISTORY_FIRST_INTERVAL ((Int)(5 * LOGICFRAMES_PER_GAME_SECOND))

	MatchHistory( void );

	void reset( void );

	/// Once a logic frame of a match: samples on the current interval and notes promotions.
	void update( UnsignedInt frame );
	void noteBuilt( const Player *player, const Object *thing );
	void notePower( const Player *player, const SpecialPowerTemplate *power, const ThingTemplate *source, Bool superweapon );

	Int getSampleCount( void ) const { return m_sampleCount; }
	UnsignedInt getSampleFrame( Int sample ) const { return m_sampleFrame[ sample ]; }
	Int getValue( Int sample, Int player, MatchSeries series ) const { return m_values[ sample ][ player ][ series ]; }
	Bool wasSampled( Int player ) const { return m_sampled[ player ]; }
	const std::vector< MatchEvent > &getEvents( void ) const { return m_events; }
	const std::vector< MatchBuild > &getBuilds( Int player ) const { return m_builds[ player ]; }

private:
	void addEvent( UnsignedInt frame, Int player, MatchEventType type, const ThingTemplate *thing, const SpecialPowerTemplate *power, Int rank );
	void sample( UnsignedInt frame );

	Int m_values[ MAX_SAMPLES ][ MAX_PLAYER_COUNT ][ MATCH_SERIES_COUNT ];
	UnsignedInt m_sampleFrame[ MAX_SAMPLES ];
	Int m_sampleCount;
	UnsignedInt m_intervalDoublings;	///< samples are MATCH_HISTORY_FIRST_INTERVAL frames apart, doubled this many times
	UnsignedInt interval( void ) const { return (UnsignedInt)MATCH_HISTORY_FIRST_INTERVAL << m_intervalDoublings; }
	Bool m_sampled[ MAX_PLAYER_COUNT ];
	Int m_rank[ MAX_PLAYER_COUNT ];
	std::vector< MatchEvent > m_events;
	std::vector< MatchBuild > m_builds[ MAX_PLAYER_COUNT ];
};

extern MatchHistory TheMatchHistory;

#endif // __MATCHHISTORY_H__
