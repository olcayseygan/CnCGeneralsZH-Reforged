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

// FILE: MatchHistory.cpp /////////////////////////////////////////////////////////////////////////
// The score screen's graphs, recorded as the match is played.  See the header.
////////////////////////////////////////////////////////////////////////////////////////////////////

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "Common/MatchHistory.h"

#include "Common/Energy.h"
#include "Common/Money.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/Science.h"
#include "Common/ScoreKeeper.h"
#include "Common/SpecialPower.h"
#include "Common/ThingTemplate.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object.h"

MatchHistory TheMatchHistory;

//-------------------------------------------------------------------------------------------------
MatchHistory::MatchHistory( void )
{
	reset();
}

//-------------------------------------------------------------------------------------------------
void MatchHistory::reset( void )
{
	m_sampleCount = 0;
	m_interval = FIRST_INTERVAL;
	for( Int player = 0; player < MAX_PLAYER_COUNT; player++ )
	{
		m_sampled[ player ] = FALSE;
		m_rank[ player ] = 0;
		m_builds[ player ].clear();
	}
	m_events.clear();
}

//-------------------------------------------------------------------------------------------------
static Bool isPlayed( const Player *player )
{
	return player->isPlayableSide() && !player->isPlayerObserver();
}

//-------------------------------------------------------------------------------------------------
struct MatchCount
{
	Int army;
	Int buildings;
};

static void countStanding( Object *obj, void *userData )
{
	MatchCount *count = (MatchCount *)userData;
	if( obj->isEffectivelyDead() || obj->isKindOf( KINDOF_DRONE ) || obj->isKindOf( KINDOF_PROJECTILE ) || obj->isKindOf( KINDOF_MINE ) )
		return;
	if( obj->isKindOf( KINDOF_STRUCTURE ) )
	{
		if( !obj->testStatus( OBJECT_STATUS_UNDER_CONSTRUCTION ) && !obj->isKindOf( KINDOF_WALK_ON_TOP_OF_WALL ) )
			++count->buildings;
		return;
	}
	if( obj->isKindOf( KINDOF_DOZER ) || obj->isKindOf( KINDOF_HARVESTER ) )
		return;
	if( obj->isKindOf( KINDOF_INFANTRY ) || obj->isKindOf( KINDOF_VEHICLE ) || obj->isKindOf( KINDOF_AIRCRAFT ) )
		++count->army;
}

//-------------------------------------------------------------------------------------------------
void MatchHistory::sample( UnsignedInt frame )
{
	if( m_sampleCount == MAX_SAMPLES )
	{
		// keep the even samples, which sit on multiples of the doubled interval
		for( Int index = 0; index < MAX_SAMPLES / 2; index++ )
		{
			m_sampleFrame[ index ] = m_sampleFrame[ index * 2 ];
			memcpy( m_values[ index ], m_values[ index * 2 ], sizeof( m_values[ index ] ) );
		}
		m_sampleCount = MAX_SAMPLES / 2;
		m_interval *= 2;
		if( frame % m_interval != 0 )
			return;
	}

	const Int sampleIndex = m_sampleCount++;
	m_sampleFrame[ sampleIndex ] = frame;
	memset( m_values[ sampleIndex ], 0, sizeof( m_values[ sampleIndex ] ) );
	for( Int index = 0; index < ThePlayerList->getPlayerCount() && index < MAX_PLAYER_COUNT; index++ )
	{
		Player *player = ThePlayerList->getNthPlayer( index );
		if( !isPlayed( player ) )
			continue;
		MatchCount count = { 0, 0 };
		player->iterateObjects( countStanding, &count );
		Int *values = m_values[ sampleIndex ][ index ];
		values[ MATCH_SERIES_EARNED ] = player->getScoreKeeper()->getTotalMoneyEarned();
		values[ MATCH_SERIES_BANK ] = (Int)player->getMoney()->countMoney();
		values[ MATCH_SERIES_ARMY ] = count.army;
		values[ MATCH_SERIES_BUILDINGS ] = count.buildings;
		values[ MATCH_SERIES_POWER ] = player->getEnergy()->getProduction();
		values[ MATCH_SERIES_LOST ] = player->getScoreKeeper()->getTotalUnitsLost();
		m_sampled[ index ] = TRUE;
	}
}

//-------------------------------------------------------------------------------------------------
void MatchHistory::update( UnsignedInt frame )
{
	if( frame % m_interval == 0 )
		sample( frame );

	for( Int index = 0; index < ThePlayerList->getPlayerCount() && index < MAX_PLAYER_COUNT; index++ )
	{
		Player *player = ThePlayerList->getNthPlayer( index );
		if( !isPlayed( player ) )
			continue;
		const Int rank = player->getRankLevel();
		if( m_rank[ index ] != 0 && rank > m_rank[ index ] )
			addEvent( frame, index, MATCH_EVENT_RANK, NULL, NULL, rank );
		m_rank[ index ] = rank;
	}
}

//-------------------------------------------------------------------------------------------------
void MatchHistory::addEvent( UnsignedInt frame, Int player, MatchEventType type, const ThingTemplate *thing, const SpecialPowerTemplate *power, Int rank )
{
	if( m_events.size() >= MAX_EVENTS )
		return;
	MatchEvent event;
	event.frame = frame;
	event.player = player;
	event.type = type;
	event.thing = thing;
	event.power = power;
	event.rank = rank;
	m_events.push_back( event );
}

//-------------------------------------------------------------------------------------------------
/** Every structure goes in; a unit only the first time its player builds one of its kind, which is
	* the part of a build order worth reading.  Fifty more Red Guards say nothing the first did not. */
//-------------------------------------------------------------------------------------------------
void MatchHistory::noteBuilt( const Player *player, const Object *thing )
{
	if( !isPlayed( player ) )
		return;
	const Int index = player->getPlayerIndex();
	const ThingTemplate *tmpl = thing->getTemplate();
	if( tmpl->isKindOf( KINDOF_DRONE ) || tmpl->isKindOf( KINDOF_PROJECTILE ) || tmpl->isKindOf( KINDOF_MINE ) )
		return;
	const UnsignedInt frame = TheGameLogic->getFrame();
	if( IsSuperweaponInMatch( tmpl ) )
		addEvent( frame, index, MATCH_EVENT_SUPERWEAPON_BUILT, tmpl, NULL, 0 );

	std::vector< MatchBuild > &builds = m_builds[ index ];
	if( builds.size() >= MAX_BUILDS )
		return;
	if( !tmpl->isKindOf( KINDOF_STRUCTURE ) )
	{
		for( size_t built = 0; built < builds.size(); built++ )
		{
			if( builds[ built ].thing == tmpl )
				return;
		}
	}
	MatchBuild build;
	build.frame = frame;
	build.thing = tmpl;
	builds.push_back( build );
}

//-------------------------------------------------------------------------------------------------
/** A superweapon fired, or a power a promotion bought.  A unit's own ability, a sniper's shot or a
	* hacker's, is no event. */
//-------------------------------------------------------------------------------------------------
void MatchHistory::notePower( const Player *player, const SpecialPowerTemplate *power, const ThingTemplate *source, Bool superweapon )
{
	if( !isPlayed( player ) )
		return;
	const Int index = player->getPlayerIndex();
	if( superweapon )
		addEvent( TheGameLogic->getFrame(), index, MATCH_EVENT_SUPERWEAPON_FIRED, source, power, 0 );
	else if( power->getRequiredScience() != SCIENCE_INVALID )
		addEvent( TheGameLogic->getFrame(), index, MATCH_EVENT_POWER, source, power, 0 );
}
