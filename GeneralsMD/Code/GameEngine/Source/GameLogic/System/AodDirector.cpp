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

// FILE: AodDirector.cpp //////////////////////////////////////////////////////////////////////////
// Art of Defense: waves down the generated paths to the seats' shared base (AodDirector.h).
///////////////////////////////////////////////////////////////////////////////////////////////////

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "Common/NameKeyGenerator.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/Team.h"
#include "Common/ThingFactory.h"
#include "Common/ThingTemplate.h"
#include "Common/Xfer.h"
#include "GameLogic/AI.h"
#include "GameLogic/AIPathfind.h"
#include "GameLogic/AodDirector.h"
#include "GameLogic/ExperienceTracker.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/LogicRandomValue.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/BodyModule.h"
#include "GameLogic/Module/CreateModule.h"
#include "GameLogic/Object.h"
#include "GameLogic/PolygonTrigger.h"
#include "GameLogic/TerrainLogic.h"
#include "GameLogic/Weapon.h"

static AodDirector theAodDirector;
AodDirector *TheAodDirector = &theAodDirector;

//-------------------------------------------------------------------------------------------------
/* The waves' units: retail ones every side builds without an upgrade, no transport, no hacker, nothing
	that carries a crew out with it.  Points are about a unit's worth against a defence. */
static const AodKind theKinds[] =
{
	{ "GLAInfantryRebel",				1, 1 },
	{ "ChinaInfantryRedguard",	1, 1 },
	{ "AmericaInfantryRanger",	1, 2 },
	{ "GLAVehicleTechnical",		3, 3 },
	{ "AmericaVehicleHumvee",		4, 4 },
	{ "GLATankScorpion",				5, 6 },
	{ "ChinaTankBattleMaster",	6, 7 },
	{ "AmericaTankCrusader",		7, 9 },
	{ "ChinaTankGattling",			7, 11 },
	{ "AmericaTankPaladin",			9, 13 },
};

Int Aod_kindCount( void ) { return (Int)( sizeof( theKinds ) / sizeof( theKinds[0] ) ); }
const AodKind &Aod_kind( Int kind ) { return theKinds[kind]; }
const char *Aod_bossName( void ) { return "ChinaTankOverlord"; }
Int Aod_bossHealthPercent( Int seats ) { return 300 + 150 * ( seats > 1 ? seats - 1 : 0 ); }
Bool Aod_isBossWave( Int wave ) { return wave > 0 && wave % AOD_BOSS_EVERY == 0; }

Int Aod_wavePoints( Int wave, Int seats )
{
	const Int base = 6 + 4 * wave + wave * wave / 3;
	return base * ( 100 + 50 * ( seats > 1 ? seats - 1 : 0 ) ) / 100;
}

Int Aod_waveRank( Int wave )
{
	if( wave >= 18 ) return LEVEL_HEROIC;
	if( wave >= 14 ) return LEVEL_ELITE;
	if( wave >= 8 ) return LEVEL_VETERAN;
	return LEVEL_REGULAR;
}

/// ticks a game second at this match's speed, read every time (never kept: the speed is per match)
static UnsignedInt gameSecond( void )
{
	const UnsignedInt frames = (UnsignedInt)LOGICFRAMES_PER_GAME_SECOND;
	return frames > 0 ? frames : 1;
}

/// a seat: a playable side that is not watching
static Bool isSeat( const Player *player )
{
	return player != NULL && player->isPlayableSide() && !player->isPlayerObserver();
}

//-------------------------------------------------------------------------------------------------
AodDirector::AodDirector( void )
{
	m_active = FALSE;
	reset();
}

//-------------------------------------------------------------------------------------------------
void AodDirector::reset( void )
{
	m_active = FALSE;
	m_base.zero();
	m_baseRadius = 0.0f;
	m_enemyPlayer = -1;
	m_seats = 0;
	m_spawnNames.clear();
	m_paths.clear();
	m_kinds.clear();
	m_boss = NULL;
	m_wave = 0;
	m_nextWaveFrame = 0;
	m_nextSpawnFrame = 0;
	m_pending.clear();
	m_units.clear();
	m_lives = AOD_LIVES;
	m_over = OVER_NOT;
	m_spawnTurn = 0;
	m_spawned = m_kills = m_leaks = 0;
}

//-------------------------------------------------------------------------------------------------
void AodDirector::newMap( Bool loadingSaveGame )
{
	reset();

	const Waypoint *base = TheTerrainLogic->getWaypointByName( "AOD_Base" );
	if( base == NULL )
		return;
	for( Int n = 1; ; ++n )
	{
		AsciiString name;
		name.format( "AOD_Spawn_%d", n );
		const Waypoint *spawn = TheTerrainLogic->getWaypointByName( name );
		if( spawn == NULL )
			break;
		m_spawnNames.push_back( name );
		// the path's stops in order, down the single link each has, for the log
		std::vector<Coord3D> stops;
		for( const Waypoint *way = spawn; way != NULL && stops.size() < 1000; way = way->getNumLinks() > 0 ? way->getLink( 0 ) : NULL )
			stops.push_back( *way->getLocation() );
		m_paths.push_back( stops );
	}
	Player *civilian = ThePlayerList->findPlayerWithNameKey( NAMEKEY( "PlyrCivilian" ) );
	if( m_spawnNames.empty() || civilian == NULL )
	{
		DEBUG_LOG(( "HEADLESS AOD: the map has AOD_Base but %s, so the mode stays off\n",
								m_spawnNames.empty() ? "no AOD_Spawn_1" : "no PlyrCivilian to own the waves" ));
		m_spawnNames.clear();
		m_paths.clear();
		return;
	}

	m_active = TRUE;
	m_base = *base->getLocation();
	// the base's reach is its area's outline; without the area, half the way to the nearest spawn
	PolygonTrigger *area = TheTerrainLogic->getTriggerAreaByName( "AOD_Base" );
	if( area != NULL && area->getNumPoints() >= 3 )
	{
		for( Int p = 0; p < area->getNumPoints(); ++p )
		{
			const Real dx = (Real)area->getPoint( p )->x - m_base.x, dy = (Real)area->getPoint( p )->y - m_base.y;
			m_baseRadius = max( m_baseRadius, (Real)sqrtf( dx * dx + dy * dy ) );
		}
	}
	else
	{
		m_baseRadius = 1.0e9f;
		for( UnsignedInt n = 0; n < m_paths.size(); ++n )
		{
			const Real dx = m_paths[n][0].x - m_base.x, dy = m_paths[n][0].y - m_base.y;
			m_baseRadius = min( m_baseRadius, 0.5f * (Real)sqrtf( dx * dx + dy * dy ) );
		}
	}
	m_enemyPlayer = civilian->getPlayerIndex();
	for( Int k = 0; k < Aod_kindCount(); ++k )
	{
		m_kinds.push_back( TheThingFactory->findTemplate( Aod_kind( k ).name, FALSE ) );
		if( m_kinds.back() == NULL )
			DEBUG_LOG(( "HEADLESS AOD: no '%s' template in the data, the waves come without it\n", Aod_kind( k ).name ));
	}
	m_boss = TheThingFactory->findTemplate( Aod_bossName(), FALSE );

	for( Int i = 0; i < ThePlayerList->getPlayerCount(); ++i )
		if( isSeat( ThePlayerList->getNthPlayer( i ) ) )
			++m_seats;

	if( !loadingSaveGame )
	{
		// the map, not the lobby, makes the sides: every seat together, the waves against all of them
		for( Int i = 0; i < ThePlayerList->getPlayerCount(); ++i )
		{
			Player *player = ThePlayerList->getNthPlayer( i );
			if( !isSeat( player ) )
				continue;
			for( Int j = 0; j < ThePlayerList->getPlayerCount(); ++j )
			{
				Player *other = ThePlayerList->getNthPlayer( j );
				if( other != player && isSeat( other ) )
					player->setPlayerRelationship( other, ALLIES );
			}
			player->setPlayerRelationship( civilian, ENEMIES );
			civilian->setPlayerRelationship( player, ENEMIES );
		}
	}

	m_nextWaveFrame = TheGameLogic->getFrame() + AOD_FIRST_WAVE_SECONDS * gameSecond();
	DEBUG_LOG(( "HEADLESS AOD: on; %d seats, %d spawns, base (%.0f,%.0f) radius %.0f, waves of player %d, first wave at frame %u\n",
							m_seats, (Int)m_spawnNames.size(), m_base.x, m_base.y, m_baseRadius, m_enemyPlayer, m_nextWaveFrame ));
	for( UnsignedInt n = 0; n < m_paths.size(); ++n )
		DEBUG_LOG(( "HEADLESS AOD: path %d from (%.0f,%.0f), %d stops to the base\n", (Int)n + 1, m_paths[n][0].x, m_paths[n][0].y,
								(Int)m_paths[n].size() - 1 ));
}

//-------------------------------------------------------------------------------------------------
Bool AodDirector::inBase( const Object *obj ) const
{
	const Real dx = obj->getPosition()->x - m_base.x, dy = obj->getPosition()->y - m_base.y;
	return dx * dx + dy * dy < m_baseRadius * m_baseRadius;
}

//-------------------------------------------------------------------------------------------------
Int AodDirector::nearestPathStop( const Object *obj, Int *spawnOut ) const
{
	Int best = -1;
	Real least = 1.0e30f;
	for( UnsignedInt n = 0; n < m_paths.size(); ++n )
		for( UnsignedInt k = 0; k < m_paths[n].size(); ++k )
		{
			const Real dx = obj->getPosition()->x - m_paths[n][k].x, dy = obj->getPosition()->y - m_paths[n][k].y;
			if( dx * dx + dy * dy < least )
			{
				least = dx * dx + dy * dy;
				best = (Int)k;
				*spawnOut = (Int)n;
			}
		}
	return best;
}

//-------------------------------------------------------------------------------------------------
void AodDirector::paySeats( Int amount ) const
{
	if( amount <= 0 )
		return;
	for( Int i = 0; i < ThePlayerList->getPlayerCount(); ++i )
	{
		Player *player = ThePlayerList->getNthPlayer( i );
		if( isSeat( player ) && player->hasAnyObjects() )
			player->getMoney()->deposit( (UnsignedInt)amount, FALSE );
	}
}

//-------------------------------------------------------------------------------------------------
/// The wave's units drawn by points out of the kinds it has reached, the boss first on its wave.
void AodDirector::startWave( UnsignedInt now )
{
	++m_wave;
	const Int seats = max( m_seats, 1 );
	Int points = Aod_wavePoints( m_wave, seats );
	m_pending.clear();
	if( Aod_isBossWave( m_wave ) && m_boss != NULL )
		m_pending.push_back( -1 );
	while( points > 0 )
	{
		Int eligible[ 32 ], count = 0;
		for( Int k = 0; k < Aod_kindCount() && count < 32; ++k )
			if( m_kinds[k] != NULL && Aod_kind( k ).firstWave <= m_wave && Aod_kind( k ).points <= points )
				eligible[ count++ ] = k;
		if( count == 0 )
			break;
		const Int k = eligible[ GameLogicRandomValue( 0, count - 1 ) ];
		m_pending.push_back( k );
		points -= Aod_kind( k ).points;
	}
	const Int pay = AOD_WAVE_PAY + AOD_WAVE_PAY_PER_WAVE * ( m_wave - 1 );
	paySeats( pay );
	m_nextSpawnFrame = now;
	DEBUG_LOG(( "HEADLESS AOD: frame %u wave %d of %d starts, %d units for %d points%s, %d to every seat, lives %d\n", now, m_wave, (Int)AOD_WAVES,
							(Int)m_pending.size(), Aod_wavePoints( m_wave, seats ), Aod_isBossWave( m_wave ) && m_boss != NULL ? " with a boss" : "",
							pay, m_lives ));
}

//-------------------------------------------------------------------------------------------------
Object *AodDirector::spawn( Int kind, Int spawnIndex )
{
	const ThingTemplate *tmpl = kind < 0 ? m_boss : m_kinds[kind];
	const Waypoint *start = TheTerrainLogic->getWaypointByName( m_spawnNames[spawnIndex] );
	if( tmpl == NULL || start == NULL )
		return NULL;
	Team *team = ThePlayerList->getNthPlayer( m_enemyPlayer )->getDefaultTeam();
	Object *obj = TheThingFactory->newObject( tmpl, team );
	if( obj == NULL )
		return NULL;
	Coord3D pos = *start->getLocation();
	pos.x += GameLogicRandomValueReal( -20.0f, 20.0f );
	pos.y += GameLogicRandomValueReal( -20.0f, 20.0f );
	pos.z = TheTerrainLogic->getGroundHeight( pos.x, pos.y );
	obj->setOrientation( 0.0f );
	obj->setPosition( &pos );
	for( BehaviorModule **m = obj->getBehaviorModules(); *m; ++m )
	{
		CreateModuleInterface *create = (*m)->getCreate();
		if( create )
			create->onBuildComplete();
	}
	const Int rank = Aod_waveRank( m_wave );
	if( rank > LEVEL_REGULAR && obj->getExperienceTracker() != NULL )
		obj->getExperienceTracker()->setVeterancyLevel( (VeterancyLevel)rank, FALSE );
	if( kind < 0 && obj->getBodyModule() != NULL )
		obj->getBodyModule()->setMaxHealth( obj->getBodyModule()->getMaxHealth() * (Real)Aod_bossHealthPercent( m_seats ) / 100.0f, FULLY_HEAL );
	team->setActive();
	TheAI->pathfinder()->addObjectToPathfindMap( obj );
	if( obj->getAI() != NULL )
		obj->getAI()->aiAttackFollowWaypointPath( start, NO_MAX_SHOTS_LIMIT, CMD_FROM_AI );

	Unit unit;
	unit.id = obj->getID();
	unit.kind = kind;
	m_units.push_back( unit );
	++m_spawned;
	if( m_spawned == 1 || kind < 0 )
		DEBUG_LOG(( "HEADLESS AOD: frame %u %s '%s' %d out of %s\n", TheGameLogic->getFrame(), kind < 0 ? "the boss" : "the first unit",
								tmpl->getName().str(), (Int)unit.id, m_spawnNames[spawnIndex].str() ));
	return obj;
}

//-------------------------------------------------------------------------------------------------
void AodDirector::lose( UnsignedInt now )
{
	m_over = OVER_LOST;
	DEBUG_LOG(( "HEADLESS AOD: frame %u lost at wave %d, %u leaks, %u kills\n", now, m_wave, m_leaks, m_kills ));
	for( Int i = 0; i < ThePlayerList->getPlayerCount(); ++i )
	{
		Player *player = ThePlayerList->getNthPlayer( i );
		if( isSeat( player ) )
			player->killPlayer();
	}
}

//-------------------------------------------------------------------------------------------------
void AodDirector::update( void )
{
	if( !m_active )
		return;
	const UnsignedInt now = TheGameLogic->getFrame();
	const UnsignedInt second = gameSecond();

	// the dead come off the list and pay, the ones in the base leak, the idle are sent on
	const UnsignedInt retarget = AOD_RETARGET_SECONDS * second;
	UnsignedInt kept = 0;
	for( UnsignedInt i = 0; i < m_units.size(); ++i )
	{
		const Unit unit = m_units[i];
		Object *obj = TheGameLogic->findObjectByID( unit.id );
		if( obj == NULL || obj->isEffectivelyDead() )
		{
			++m_kills;
			paySeats( AOD_KILL_PAY_PER_POINT * ( unit.kind < 0 ? 4 * Aod_wavePoints( m_wave, max( m_seats, 1 ) ) / 10 : Aod_kind( unit.kind ).points ) );
			continue;
		}
		if( inBase( obj ) )
		{
			++m_leaks;
			m_lives -= unit.kind < 0 ? AOD_BOSS_LEAK : 1;
			if( m_leaks <= 3 || m_leaks % 10 == 0 || unit.kind < 0 )
				DEBUG_LOG(( "HEADLESS AOD: frame %u '%s' %d leaked into the base, lives %d\n", now, obj->getTemplate()->getName().str(),
										(Int)unit.id, m_lives ));
			TheGameLogic->destroyObject( obj );
			continue;
		}
		AIUpdateInterface *ai = obj->getAI();
		if( ai != NULL && ai->isIdle() && ( now + (UnsignedInt)unit.id ) % retarget == 0 )
			ai->aiAttackMoveToPosition( &m_base, NO_MAX_SHOTS_LIMIT, CMD_FROM_AI );
		m_units[kept++] = unit;
	}
	m_units.resize( kept );

	if( m_over == OVER_NOT && m_lives <= 0 )
		lose( now );
	if( m_over != OVER_NOT )
		return;

	if( m_pending.empty() )
	{
		if( m_wave >= AOD_WAVES && m_units.empty() )
		{
			m_over = OVER_WON;
			DEBUG_LOG(( "HEADLESS AOD: frame %u won, every wave seen off, lives %d, %u leaks, %u kills\n", now, m_lives, m_leaks, m_kills ));
			return;
		}
		if( m_wave < AOD_WAVES && now >= m_nextWaveFrame )
			startWave( now );
	}
	// one unit out at a time, the spawns taking turns, under the cap
	if( !m_pending.empty() && now >= m_nextSpawnFrame && (Int)m_units.size() < AOD_ALIVE_CAP )
	{
		const Int kind = m_pending.front();
		m_pending.erase( m_pending.begin() );
		spawn( kind, m_spawnTurn % (Int)m_spawnNames.size() );
		m_spawnTurn = ( m_spawnTurn + 1 ) % (Int)m_spawnNames.size();
		m_nextSpawnFrame = now + AOD_SPAWN_GAP_FRAMES;
		if( m_pending.empty() )
			m_nextWaveFrame = now + AOD_WAVE_GAP_SECONDS * second;
	}

	if( now % ( AOD_LOG_SECONDS * second ) == 0 )
	{
		// the oldest walker's place on its path: how far down it the waves have got
		Int stop = -1, path = 0;
		Real toBase = 0.0f;
		Coord3D at;
		at.zero();
		const Object *oldest = m_units.empty() ? NULL : TheGameLogic->findObjectByID( m_units[0].id );
		if( oldest != NULL )
		{
			stop = nearestPathStop( oldest, &path );
			at = *oldest->getPosition();
			const Real dx = at.x - m_base.x, dy = at.y - m_base.y;
			toBase = sqrtf( dx * dx + dy * dy );
		}
		DEBUG_LOG(( "HEADLESS AOD: frame %u wave %d walking %d to come %d spawned %u kills %u leaks %u lives %d; oldest at (%.0f,%.0f) stop %d of %d on path %d, %.0f from the base\n",
								now, m_wave, (Int)m_units.size(), (Int)m_pending.size(), m_spawned, m_kills, m_leaks, m_lives, at.x, at.y, stop,
								oldest != NULL ? (Int)m_paths[path].size() - 1 : 0, path + 1, toBase ));
	}
}

//-------------------------------------------------------------------------------------------------
void AodDirector::crc( Xfer *xfer )
{
	AodDirector::xfer( xfer );
}

//-------------------------------------------------------------------------------------------------
/** Version 1: the wave clock, the wave's units still to come, the walking units, lives and counts.
	  The base, the spawns, the owner and the templates come from the map and the data on every load. */
void AodDirector::xfer( Xfer *xfer )
{
	XferVersion currentVersion = 1;
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );

	xfer->xferInt( &m_wave );
	xfer->xferUnsignedInt( &m_nextWaveFrame );
	xfer->xferUnsignedInt( &m_nextSpawnFrame );
	xfer->xferInt( &m_lives );
	xfer->xferInt( &m_over );
	xfer->xferInt( &m_spawnTurn );
	xfer->xferUnsignedInt( &m_spawned );
	xfer->xferUnsignedInt( &m_kills );
	xfer->xferUnsignedInt( &m_leaks );

	UnsignedInt count = (UnsignedInt)m_pending.size();
	xfer->xferUnsignedInt( &count );
	if( xfer->getXferMode() == XFER_LOAD )
		m_pending.resize( count );
	for( UnsignedInt i = 0; i < count; ++i )
		xfer->xferInt( &m_pending[i] );

	count = (UnsignedInt)m_units.size();
	xfer->xferUnsignedInt( &count );
	if( xfer->getXferMode() == XFER_LOAD )
		m_units.resize( count );
	for( UnsignedInt i = 0; i < count; ++i )
	{
		xfer->xferObjectID( &m_units[i].id );
		xfer->xferInt( &m_units[i].kind );
	}
}
