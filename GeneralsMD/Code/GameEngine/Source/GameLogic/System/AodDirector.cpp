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
// Art of Defense: waves down the generated lanes to their exits (AodDirector.h).
///////////////////////////////////////////////////////////////////////////////////////////////////

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "Common/EarlyCommandLine.h"
#include "Common/Energy.h"
#include "Common/Money.h"
#include "Common/NameKeyGenerator.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/Team.h"
#include "Common/ThingFactory.h"
#include "Common/ThingTemplate.h"
#include "Common/Xfer.h"
#include "GameClient/FXList.h"
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
#include "GameLogic/PartitionManager.h"
#include "GameLogic/PolygonTrigger.h"
#include "GameLogic/TerrainLogic.h"
#include "GameLogic/Weapon.h"

static AodDirector theAodDirector;
AodDirector *TheAodDirector = &theAodDirector;

//-------------------------------------------------------------------------------------------------
/* The waves' units: retail ones every side builds without an upgrade, no transport, no hacker, nothing
	that carries a crew out with it.  Infantry, vehicles and, late, a helicopter, so no one kind of defence
	holds a lane alone.  Points are about a unit's worth against a defence.  Wave 1 is Technicals, which
	walk a lane in about 70 game seconds (a Rebel takes five minutes). */
static const AodKind theKinds[] =
{
	{ "GLAVehicleTechnical",				3, 1 },
	{ "GLAInfantryRebel",						1, 2 },
	{ "ChinaInfantryRedguard",			1, 2 },
	{ "AmericaInfantryRanger",			1, 3 },
	{ "AmericaVehicleHumvee",				4, 3 },
	{ "GLAInfantryTunnelDefender",	2, 4 },
	{ "GLATankScorpion",						5, 5 },
	{ "ChinaTankBattleMaster",			6, 6 },
	{ "AmericaTankCrusader",				7, 8 },
	{ "ChinaTankGattling",					7, 10 },
	{ "AmericaVehicleComanche",			8, 12 },
	{ "AmericaTankPaladin",					9, 13 },
};

Int Aod_kindCount( void ) { return (Int)( sizeof( theKinds ) / sizeof( theKinds[0] ) ); }
const AodKind &Aod_kind( Int kind ) { return theKinds[kind]; }
const char *Aod_bossName( void ) { return "ChinaTankOverlord"; }
/* An Overlord one and a half times over at wave 5, four and a half at wave 20.  The reference batches of 2026-11-28
	 (-aodreference, three sides at 1, 2 and 4 seats, two logic seeds): at 300% the whole budget let the wave 5 boss
	 through on every one-seat run, at 225% on four of six, at 150% on three of six, and half the budget never
	 stopped it. */
Int Aod_bossHealthPercent( Int wave ) { return 50 + 20 * wave; }
Bool Aod_isBossWave( Int wave ) { return wave > 0 && wave % AOD_BOSS_EVERY == 0; }

/* 6 + 4w + w*w/3 on the first batches let China's and the GLA's whole-budget reference defences fall between
	 waves 7 and 12 (America's held to 16); this curve is a quarter lighter by wave 10. */
Int Aod_wavePoints( Int wave )
{
	return 6 + 3 * wave + wave * wave / 4;
}

/* A kind's weight in a wave's draw: even to wave 3, then leaning a point's worth more every wave towards the dear
	 ones: at wave 10 a Paladin (9 points) is drawn 5.7 times as often as a Rebel for its weight, at wave 20 7.2. */
Int Aod_kindWeight( Int kind, Int wave )
{
	return 10 + 2 * Aod_kind( kind ).points * max( 0, wave - 3 );
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

/// the reference defence's share as the replay being played back recorded it (RecorderClass::readReplayHeader)
static Int theReplayReferencePercent = 0;

Int Aod_referencePercentAsked( void )
{
	char percent[16];
	if( !findEarlyCommandLineValue( L"-aodreference", percent, sizeof( percent ) ) )
		return 0;
	return max( 0, min( 100, atoi( percent ) ) );
}

void Aod_setReplayReferencePercent( Int percent )
{
	theReplayReferencePercent = max( 0, min( 100, percent ) );
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
	m_exit = NULL;
	m_enemyPlayer = -1;
	m_seats = 0;
	m_paths.clear();
	m_stops.clear();
	m_kinds.clear();
	m_boss = NULL;
	m_exitFX = NULL;
	m_wave = 0;
	m_nextWaveFrame = 0;
	m_nextSpawnFrame = 0;
	m_pending.clear();
	m_units.clear();
	m_lives = AOD_LIVES;
	m_over = OVER_NOT;
	m_spawnTurn = 0;
	m_spawned = m_kills = m_leaks = 0;
	for( Int w = 0; w <= AOD_WAVES; ++w )
	{
		m_waveLeaks[w] = 0;
		for( Int n = 0; n < AOD_LANES_MOST; ++n )
		{
			m_laneLeaked[w][n] = m_paidKills[w][n] = m_paidStipend[w][n] = 0;
			m_settled[w][n] = FALSE;
		}
	}
	for( Int n = 0; n < AOD_LANES_MOST; ++n )
		m_catchUp[n] = FALSE;
	m_cashSet = FALSE;
	m_refPercent = 0;
	m_refStarted = FALSE;
	m_refSpots.clear();
	for( Int n = 0; n < AOD_LANES_MOST; ++n )
	{
		m_refEarned[n] = m_refSpent[n] = m_refBought[n] = m_refNextSpot[n] = m_refPlants[n] = 0;
		m_refHeld[n].clear();
	}
}

//-------------------------------------------------------------------------------------------------
void AodDirector::newMap( Bool loadingSaveGame )
{
	reset();

	PolygonTrigger *exit = TheTerrainLogic->getTriggerAreaByName( "AOD_Exit" );
	if( exit == NULL )
		return;
	for( Int n = 1; n <= AOD_LANES_MOST; ++n )
	{
		AsciiString name;
		name.format( "AOD_Entry_%d", n );
		const Waypoint *entry = TheTerrainLogic->getWaypointByName( name );
		if( entry == NULL )
			break;
		// the lane's stops in order, down the single link each has
		std::vector<Coord3D> stops;
		std::vector<AsciiString> names;
		for( const Waypoint *way = entry; way != NULL && stops.size() < 1000; way = way->getNumLinks() > 0 ? way->getLink( 0 ) : NULL )
		{
			stops.push_back( *way->getLocation() );
			names.push_back( way->getName() );
		}
		m_paths.push_back( stops );
		m_stops.push_back( names );
	}
	Player *civilian = ThePlayerList->findPlayerWithNameKey( NAMEKEY( "PlyrCivilian" ) );
	if( m_paths.empty() || civilian == NULL )
	{
		DEBUG_LOG(( "HEADLESS AOD: the map has AOD_Exit but %s, so the mode stays off\n",
								m_paths.empty() ? "no AOD_Entry_1" : "no PlyrCivilian to own the waves" ));
		m_paths.clear();
		m_stops.clear();
		return;
	}

	m_active = TRUE;
	m_exit = exit;
	m_enemyPlayer = civilian->getPlayerIndex();
	for( Int k = 0; k < Aod_kindCount(); ++k )
	{
		m_kinds.push_back( TheThingFactory->findTemplate( Aod_kind( k ).name, FALSE ) );
		if( m_kinds.back() == NULL )
			DEBUG_LOG(( "HEADLESS AOD: no '%s' template in the data, the waves come without it\n", Aod_kind( k ).name ));
	}
	m_boss = TheThingFactory->findTemplate( Aod_bossName(), FALSE );
	m_exitFX = TheFXListStore != NULL ? TheFXListStore->findFXList( "FX_ParticleUplinkDeathInitial" ) : NULL;

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

	/* The reference defence: a skirmish dev aid that plays every seat with a fixed share of the money the
		 mode pays it, to tune the waves against (no human can play a test).  Dead in a network game.  The
		 skirmish's replay carries the share ("AR=" in its header, RecorderClass) and plays it back. */
	if( TheGameLogic->isInSkirmishGame() )
		m_refPercent = Aod_referencePercentAsked();
	else if( TheGameLogic->isInReplayGame() )
		m_refPercent = theReplayReferencePercent;
	if( m_refPercent > 0 )
		DEBUG_LOG(( "HEADLESS AOD: the reference defence plays every seat with %d%% of what it is paid\n", m_refPercent ));

	m_nextWaveFrame = TheGameLogic->getFrame() + AOD_FIRST_WAVE_SECONDS * gameSecond();
	m_lives = AOD_LIVES * (Int)m_paths.size();		// twenty a seat; a save's own count comes back over it
	DEBUG_LOG(( "HEADLESS AOD: on; %d seats, %d lanes, %d lives, waves of player %d, first wave at frame %u\n",
							m_seats, (Int)m_paths.size(), m_lives, m_enemyPlayer, m_nextWaveFrame ));
	for( UnsignedInt n = 0; n < m_paths.size(); ++n )
		DEBUG_LOG(( "HEADLESS AOD: lane %d from (%.0f,%.0f) to (%.0f,%.0f), %d stops\n", (Int)n + 1, m_paths[n].front().x, m_paths[n].front().y,
								m_paths[n].back().x, m_paths[n].back().y, (Int)m_paths[n].size() - 1 ));
}

//-------------------------------------------------------------------------------------------------
Bool AodDirector::inExit( const Object *obj ) const
{
	ICoord3D at;
	at.x = (Int)obj->getPosition()->x;
	at.y = (Int)obj->getPosition()->y;
	at.z = 0;
	return m_exit != NULL && m_exit->pointInTrigger( at );
}

//-------------------------------------------------------------------------------------------------
Int AodDirector::nearestStop( const Object *obj, Int lane ) const
{
	Int best = 0;
	Real least = 1.0e30f;
	const std::vector<Coord3D> &stops = m_paths[lane];
	for( UnsignedInt k = 0; k < stops.size(); ++k )
	{
		const Real dx = obj->getPosition()->x - stops[k].x, dy = obj->getPosition()->y - stops[k].y;
		if( dx * dx + dy * dy < least )
		{
			least = dx * dx + dy * dy;
			best = (Int)k;
		}
	}
	return best;
}

//-------------------------------------------------------------------------------------------------
/// The map stands seat n's start in lane n's sector (RMGLayout::buildAod), so a lane's seat is the one on its start.
Player *AodDirector::seatOfLane( Int lane ) const
{
	for( Int i = 0; i < ThePlayerList->getPlayerCount(); ++i )
	{
		Player *player = ThePlayerList->getNthPlayer( i );
		if( isSeat( player ) && player->getMpStartIndex() == lane )
			return player;
	}
	return NULL;
}

//-------------------------------------------------------------------------------------------------
void AodDirector::pay( Player *seat, Int lane, Int amount )
{
	if( amount <= 0 || seat == NULL || !seat->hasAnyObjects() )
		return;
	seat->getMoney()->deposit( (UnsignedInt)amount, FALSE );
	if( lane >= 0 && lane < AOD_LANES_MOST )
		m_refEarned[lane] += amount;
}

//-------------------------------------------------------------------------------------------------
/** The wave's units, drawn by points out of the kinds it has reached, the boss first on its wave.  One draw
	  serves every lane: each seat meets the same units in the same order (the map is a mirror too). */
void AodDirector::startWave( UnsignedInt now )
{
	++m_wave;
	Int points = Aod_wavePoints( m_wave );
	m_pending.clear();
	if( Aod_isBossWave( m_wave ) && m_boss != NULL )
		m_pending.push_back( -1 );
	/* Each unit one draw on the logic stream, weighted towards the dearer kinds as the waves go on
		 (Aod_kindWeight), so a late wave is fewer, heavier units and not a crowd of one-point soldiers. */
	while( points > 0 )
	{
		Int eligible[ 32 ], weights[ 32 ], count = 0, total = 0;
		for( Int k = 0; k < Aod_kindCount() && count < 32; ++k )
			if( m_kinds[k] != NULL && Aod_kind( k ).firstWave <= m_wave && Aod_kind( k ).points <= points )
			{
				weights[ count ] = Aod_kindWeight( k, m_wave );
				total += weights[ count ];
				eligible[ count++ ] = k;
			}
		if( count == 0 )
			break;
		Int roll = GameLogicRandomValue( 0, total - 1 ), pick = 0;
		while( roll >= weights[ pick ] )
			roll -= weights[ pick++ ];
		const Int k = eligible[ pick ];
		m_pending.push_back( k );
		points -= Aod_kind( k ).points;
	}
	// the stipend, with the catch-up for a seat whose lane leaked the last wave it finished
	Int lowest = 0, highest = 0;
	for( Int n = 0; n < (Int)m_paths.size(); ++n )
	{
		const Int stipend = AOD_STIPEND + ( m_catchUp[n] ? AOD_CATCHUP_PER_POINT * Aod_wavePoints( m_wave ) : 0 );
		pay( seatOfLane( n ), n, stipend );
		if( m_wave <= AOD_WAVES && n < AOD_LANES_MOST )
			m_paidStipend[m_wave][n] = stipend;
		lowest = n == 0 ? stipend : min( lowest, stipend );
		highest = max( highest, stipend );
		// the economy table's money column (econ.py): paid so far (with the start money under the reference), spent by it, in the bank
		Player *seat = seatOfLane( n );
		if( n < AOD_LANES_MOST )
			DEBUG_LOG(( "HEADLESS AOD MONEY: frame %u wave %d lane %d paid %d spent %d bank %d\n", now, m_wave, n + 1, m_refEarned[n], m_refSpent[n],
									seat != NULL ? (Int)seat->getMoney()->countMoney() : 0 ));
	}
	m_nextSpawnFrame = now;
	DEBUG_LOG(( "HEADLESS AOD: frame %u wave %d of %d starts, %d units a lane for %d points%s, stipend %d to %d, lives %d\n", now, m_wave, (Int)AOD_WAVES,
							(Int)m_pending.size(), Aod_wavePoints( m_wave ), Aod_isBossWave( m_wave ) && m_boss != NULL ? " with a boss" : "",
							lowest, highest, m_lives ));
}

//-------------------------------------------------------------------------------------------------
/** A wave is settled in a lane once all of it is out and the last of it there is dead or gone: a lane that
	  let none through pays its seat the clean wave's bonus, one that leaked earns its seat the catch-up on
	  the next stipend.  alive counts each wave's units still walking each lane. */
void AodDirector::settleWaves( UnsignedInt now, const Int alive[][ AOD_LANES_MOST ] )
{
	const Int lanes = min( (Int)m_paths.size(), (Int)AOD_LANES_MOST );
	for( Int w = 1; w <= m_wave && w <= AOD_WAVES; ++w )
	{
		if( w == m_wave && !m_pending.empty() )
			continue;		// still coming out
		for( Int n = 0; n < lanes; ++n )
		{
			if( m_settled[w][n] || alive[w][n] > 0 )
				continue;
			m_settled[w][n] = TRUE;
			const Bool clean = m_laneLeaked[w][n] == 0;
			const Int bonus = clean ? AOD_CLEAN_PER_POINT * Aod_wavePoints( w ) : 0;
			pay( seatOfLane( n ), n, bonus );
			m_catchUp[n] = !clean;
			DEBUG_LOG(( "HEADLESS AOD PAY: frame %u wave %d lane %d settled, %d leaked; kills %d stipend %d bonus %d\n", now, w, n + 1,
									m_laneLeaked[w][n], m_paidKills[w][n], m_paidStipend[w][n], bonus ));
		}
	}
}

//-------------------------------------------------------------------------------------------------
Object *AodDirector::spawn( Int kind, Int lane )
{
	const ThingTemplate *tmpl = kind < 0 ? m_boss : m_kinds[kind];
	const Waypoint *start = TheTerrainLogic->getWaypointByName( m_stops[lane][0] );
	if( tmpl == NULL || start == NULL )
		return NULL;
	Team *team = ThePlayerList->getNthPlayer( m_enemyPlayer )->getDefaultTeam();
	Object *obj = TheThingFactory->newObject( tmpl, team );
	if( obj == NULL )
		return NULL;
	// a little scatter, inside the trench's floor (5.5 cells, 55 units, either side of its middle)
	Coord3D pos = *start->getLocation();
	pos.x += GameLogicRandomValueReal( -15.0f, 15.0f );
	pos.y += GameLogicRandomValueReal( -15.0f, 15.0f );
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
		obj->getBodyModule()->setMaxHealth( obj->getBodyModule()->getMaxHealth() * (Real)Aod_bossHealthPercent( m_wave ) / 100.0f, FULLY_HEAL );
	team->setActive();
	TheAI->pathfinder()->addObjectToPathfindMap( obj );
	if( obj->getAI() != NULL )
		obj->getAI()->aiAttackFollowWaypointPath( start, NO_MAX_SHOTS_LIMIT, CMD_FROM_AI );

	Unit unit;
	unit.id = obj->getID();
	unit.kind = kind;
	unit.lane = lane;
	unit.wave = m_wave;
	unit.best = 0;
	unit.since = TheGameLogic->getFrame();
	m_units.push_back( unit );
	++m_spawned;
	if( m_spawned == 1 || ( kind < 0 && lane == 0 ) )
		DEBUG_LOG(( "HEADLESS AOD: frame %u %s '%s' %d out of %s\n", TheGameLogic->getFrame(), kind < 0 ? "the boss" : "the first unit",
								tmpl->getName().str(), (Int)unit.id, m_stops[lane][0].str() ));
	return obj;
}

//-------------------------------------------------------------------------------------------------
void AodDirector::lose( UnsignedInt now )
{
	m_over = OVER_LOST;
	DEBUG_LOG(( "HEADLESS AOD: frame %u lost at wave %d, %u leaks, %u kills\n", now, m_wave, m_leaks, m_kills ));
	logWaves( now );
	for( Int i = 0; i < ThePlayerList->getPlayerCount(); ++i )
	{
		Player *player = ThePlayerList->getNthPlayer( i );
		if( isSeat( player ) )
			player->killPlayer();
	}
}

//-------------------------------------------------------------------------------------------------
void AodDirector::logWaves( UnsignedInt now ) const
{
	AsciiString line, one;
	for( Int w = 1; w <= m_wave && w <= AOD_WAVES; ++w )
	{
		one.format( " %d", m_waveLeaks[w] );
		line.concat( one );
	}
	DEBUG_LOG(( "HEADLESS AOD WAVES: frame %u lives lost to waves 1..%d:%s; lives %d\n", now, m_wave, line.str(), m_lives ));
}

//-------------------------------------------------------------------------------------------------
void AodDirector::update( void )
{
	if( !m_active )
		return;
	const UnsignedInt now = TheGameLogic->getFrame();
	const UnsignedInt second = gameSecond();
	const Int lanes = (Int)m_paths.size();

	// every seat starts on the mode's money, whatever the room set (no harvesting here: AodDirector.h)
	if( !m_cashSet )
	{
		m_cashSet = TRUE;
		for( Int i = 0; i < ThePlayerList->getPlayerCount(); ++i )
		{
			Player *player = ThePlayerList->getNthPlayer( i );
			if( !isSeat( player ) )
				continue;
			player->getMoney()->withdraw( player->getMoney()->countMoney(), FALSE );
			player->getMoney()->deposit( AOD_START_CASH, FALSE );
		}
	}

	// the dead come off the list and pay, the ones that got out leak, the idle are sent on down their lane
	const UnsignedInt retarget = AOD_RETARGET_SECONDS * second;
	Int walking[ AOD_LANES_MOST ] = { 0 };
	Int alive[ AOD_WAVES + 1 ][ AOD_LANES_MOST ];
	for( Int w = 0; w <= AOD_WAVES; ++w )
		for( Int n = 0; n < AOD_LANES_MOST; ++n )
			alive[w][n] = 0;
	UnsignedInt kept = 0;
	for( UnsignedInt i = 0; i < m_units.size(); ++i )
	{
		Unit unit = m_units[i];
		if( unit.lane < 0 || unit.lane >= lanes )
			unit.lane = 0;		// a save's lane this map does not have
		Object *obj = TheGameLogic->findObjectByID( unit.id );
		if( obj == NULL || obj->isEffectivelyDead() )
		{
			++m_kills;
			const Int points = unit.kind < 0 ? 4 * Aod_wavePoints( max( 1, unit.wave ) ) / 10 : Aod_kind( unit.kind ).points;
			pay( seatOfLane( unit.lane ), unit.lane, AOD_KILL_PAY_PER_POINT * points );
			if( unit.wave >= 0 && unit.wave <= AOD_WAVES && unit.lane < AOD_LANES_MOST )
				m_paidKills[unit.wave][unit.lane] += AOD_KILL_PAY_PER_POINT * points;
			continue;
		}
		if( inExit( obj ) )
		{
			const Int cost = unit.kind < 0 ? AOD_BOSS_LEAK : 1;
			++m_leaks;
			m_lives -= cost;
			if( unit.wave >= 0 && unit.wave <= AOD_WAVES )
			{
				m_waveLeaks[unit.wave] += cost;
				if( unit.lane < AOD_LANES_MOST )
					++m_laneLeaked[unit.wave][unit.lane];
			}
			if( m_leaks <= 3 || m_leaks % 10 == 0 || unit.kind < 0 )
				DEBUG_LOG(( "HEADLESS AOD: frame %u '%s' %d of wave %d got out of lane %d at (%.0f,%.0f), lives %d\n", now,
										obj->getTemplate()->getName().str(), (Int)unit.id, unit.wave, unit.lane + 1, obj->getPosition()->x, obj->getPosition()->y, m_lives ));
			// it goes in a flare (the client draws it; nothing in the logic reads it back)
			FXList::doFXPos( m_exitFX, obj->getPosition() );
			TheGameLogic->destroyObject( obj );
			continue;
		}
		/* One that has stopped (a fight it was in is over, or a target it chased is gone) picks its lane up at
			the stop after the nearest one; the trench walls keep it from going anywhere else. */
		AIUpdateInterface *ai = obj->getAI();
		if( ai != NULL && ( now + (UnsignedInt)unit.id ) % retarget == 0 )
		{
			/* One that has reached no stop further down in AOD_STUCK_SECONDS is sent on as if idle, again every
				 AOD_STUCK_SECONDS, and after three times that is taken off the map: no life, no pay. */
			const Int at = nearestStop( obj, unit.lane );
			const UnsignedInt stuckFor = AOD_STUCK_SECONDS * second;
			if( at > unit.best || now < unit.since )
			{
				unit.best = at;
				unit.since = now;
			}
			const UnsignedInt still = now - unit.since;
			if( still >= 3 * stuckFor )
			{
				DEBUG_LOG(( "HEADLESS AOD: frame %u '%s' %d of wave %d stuck at stop %d of lane %d since frame %u, taken off\n", now,
										obj->getTemplate()->getName().str(), (Int)unit.id, unit.wave, at, unit.lane + 1, unit.since ));
				TheGameLogic->destroyObject( obj );
				continue;
			}
			if( ai->isIdle() || ( still >= stuckFor && still % stuckFor < retarget ) )
			{
				const Int last = (Int)m_stops[unit.lane].size() - 1;
				const Waypoint *next = TheTerrainLogic->getWaypointByName( m_stops[unit.lane][ min( at + 1, last ) ] );
				if( next != NULL )
					ai->aiAttackFollowWaypointPath( next, NO_MAX_SHOTS_LIMIT, CMD_FROM_AI );
			}
		}
		++walking[unit.lane];
		if( unit.wave >= 0 && unit.wave <= AOD_WAVES && unit.lane < AOD_LANES_MOST )
			++alive[unit.wave][unit.lane];
		m_units[kept++] = unit;
	}
	m_units.resize( kept );
	settleWaves( now, alive );

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
			logWaves( now );
			return;
		}
		if( m_wave < AOD_WAVES && now >= m_nextWaveFrame )
			startWave( now );
	}
	/* The next unit out of every entry at once, while every lane is under its share of the cap: no lane gets
		 its units sooner because another lane's are slow to die. */
	Bool room = !m_pending.empty() && now >= m_nextSpawnFrame;
	for( Int n = 0; n < lanes && room; ++n )
		room = walking[n] < AOD_ALIVE_CAP / lanes;
	if( room )
	{
		const Int kind = m_pending.front();
		m_pending.erase( m_pending.begin() );
		for( Int n = 0; n < lanes; ++n )
			spawn( kind, n );
		m_nextSpawnFrame = now + AOD_SPAWN_GAP_FRAMES;
		if( m_pending.empty() )
			m_nextWaveFrame = now + AOD_WAVE_GAP_SECONDS * second;
	}

	if( m_refPercent > 0 && now % second == 0 )
		referenceDefence( now );

	if( now % ( AOD_LOG_SECONDS * second ) == 0 )
	{
		// the oldest walker's place on its lane: how far down it the waves have got
		Int stop = -1, lane = 0;
		Coord3D at;
		at.zero();
		const Object *oldest = m_units.empty() ? NULL : TheGameLogic->findObjectByID( m_units[0].id );
		if( oldest != NULL )
		{
			lane = m_units[0].lane;
			stop = nearestStop( oldest, lane );
			at = *oldest->getPosition();
		}
		DEBUG_LOG(( "HEADLESS AOD: frame %u wave %d walking %d to come %d spawned %u kills %u leaks %u lives %d; oldest at (%.0f,%.0f) stop %d of %d on lane %d\n",
								now, m_wave, (Int)m_units.size(), (Int)m_pending.size(), m_spawned, m_kills, m_leaks, m_lives, at.x, at.y, stop,
								(Int)m_paths[lane].size() - 1, lane + 1 ));
		if( now % ( 6 * AOD_LOG_SECONDS * second ) == 0 )
			logWaves( now );
	}
}

//-------------------------------------------------------------------------------------------------
// The reference defence: a dev aid.  It stands in for a human on every seat so the waves can be tuned
// without one: whatever share of the money the mode has paid a seat it is allowed, it spends on its side's
// standard defences, put on the rim beside the seat's lane from the exit back, a power plant first whenever
// the side runs short.  The rest of the seat's money stays in the bank.
//-------------------------------------------------------------------------------------------------

/// spots on the high ground beside each lane, either side of every stop, the ones that cover the most lane first
void AodDirector::findReferenceSpots( void )
{
	m_refSpots.assign( m_paths.size(), std::vector<Coord3D>() );
	for( UnsignedInt n = 0; n < m_paths.size(); ++n )
	{
		const std::vector<Coord3D> &stops = m_paths[n];
		std::vector<Coord3D> candidates;
		for( Int k = (Int)stops.size() - 2; k >= 1; --k )
		{
			const Real dx = stops[k + 1].x - stops[k - 1].x, dy = stops[k + 1].y - stops[k - 1].y;
			const Real length = sqrtf( dx * dx + dy * dy );
			if( length <= 0.0f )
				continue;
			for( Int side = -1; side <= 1; side += 2 )
			{
				// every spot on the plateau 90 to 190 units out (the floor is 24 bytes, the plateau 62: 15 units over the stop is well up the wall)
				for( Real out = 90.0f; out <= 190.0f; out += 25.0f )
				{
					Coord3D spot;
					spot.x = stops[k].x - dy / length * out * (Real)side;
					spot.y = stops[k].y + dx / length * out * (Real)side;
					spot.z = TheTerrainLogic->getGroundHeight( spot.x, spot.y );
					if( spot.z >= stops[k].z + 15.0f )
						candidates.push_back( spot );
				}
			}
		}
		/* The lane as points 20 units apart, each worth 1 until a gun covers it and half as much again for each gun
			 after: the spots taken in turn by how much lane they see within 220 units, so the first guns stand at the
			 turns where the lane passes them twice and the last ones fill the straights. Seeing counts the terrain:
			 a gun's shot needs a clear line over the rim (Weapon::isClearFiringLineOfSightTerrain), and from 150 units
			 back the trench wall hides its floor, so guns there never fired (a GLA one-seat run, 0 kills in 20
			 leaks). Taking the stops from the exit back put the first guns at a ring's run into the pit, where a
			 Technical is in reach for two seconds. */
		std::vector<Coord3D> lane;
		for( UnsignedInt k = 1; k < stops.size(); ++k )
		{
			const Real dx = stops[k].x - stops[k - 1].x, dy = stops[k].y - stops[k - 1].y;
			const Int steps = max( 1, (Int)( sqrtf( dx * dx + dy * dy ) / 20.0f ) );
			for( Int s = 0; s < steps; ++s )
			{
				Coord3D p = stops[k - 1];
				p.x += dx * (Real)s / (Real)steps;
				p.y += dy * (Real)s / (Real)steps;
				lane.push_back( p );
			}
		}
		const Real reach2 = 220.0f * 220.0f;
		// the lane points each spot sees: in reach, a gun's height over the spot to a man's height over the floor
		std::vector< std::vector<Int> > sees( candidates.size() );
		for( UnsignedInt c = 0; c < candidates.size(); ++c )
		{
			Coord3D gun = candidates[c];
			gun.z += 12.0f;
			for( UnsignedInt i = 0; i < lane.size(); ++i )
			{
				const Real lx = lane[i].x - gun.x, ly = lane[i].y - gun.y;
				if( lx * lx + ly * ly > reach2 )
					continue;
				Coord3D target = lane[i];
				target.z = TheTerrainLogic->getGroundHeight( target.x, target.y ) + 6.0f;
				if( ThePartitionManager->isClearLineOfSightTerrain( NULL, gun, NULL, target ) )
					sees[c].push_back( (Int)i );
			}
		}
		std::vector<Real> worth( lane.size(), 1.0f );
		std::vector<Bool> taken( candidates.size(), FALSE );
		for( ;; )
		{
			Int best = -1;
			Real bestWorth = 0.0f;
			for( UnsignedInt c = 0; c < candidates.size(); ++c )
			{
				if( taken[c] )
					continue;
				Bool clear = TRUE;
				for( UnsignedInt s = 0; s < m_refSpots[n].size() && clear; ++s )
				{
					const Real sx = candidates[c].x - m_refSpots[n][s].x, sy = candidates[c].y - m_refSpots[n][s].y;
					clear = sx * sx + sy * sy > 90.0f * 90.0f;
				}
				if( !clear )
				{
					taken[c] = TRUE;
					continue;
				}
				Real covered = 0.0f;
				for( UnsignedInt i = 0; i < sees[c].size(); ++i )
					covered += worth[ sees[c][i] ];
				if( covered > bestWorth )
				{
					best = (Int)c;
					bestWorth = covered;
				}
			}
			if( best < 0 )
				break;
			taken[best] = TRUE;
			m_refSpots[n].push_back( candidates[best] );
			for( UnsignedInt i = 0; i < sees[best].size(); ++i )
				worth[ sees[best][i] ] *= 0.5f;
		}
	}
}

void AodDirector::referenceDefence( UnsignedInt now )
{
	if( !m_refStarted )
	{
		m_refStarted = TRUE;
		findReferenceSpots();
		for( Int n = 0; n < (Int)m_paths.size() && n < AOD_LANES_MOST; ++n )
		{
			Player *seat = seatOfLane( n );
			m_refEarned[n] += seat != NULL ? (Int)seat->getMoney()->countMoney() : 0;		// the start money
			DEBUG_LOG(( "HEADLESS AOD REF: lane %d seat %s, start money %d, %d spots\n", n + 1,
									seat != NULL ? seat->getBaseSide().str() : "none", m_refEarned[n], (Int)m_refSpots[n].size() ));
		}
	}
	for( Int n = 0; n < (Int)m_paths.size() && n < AOD_LANES_MOST; ++n )
	{
		Player *seat = seatOfLane( n );
		if( seat == NULL || !seat->hasAnyObjects() )
			continue;
		const AsciiString side = seat->getBaseSide();
		/* The side's defences, taken in turn, and the power plant they need.  China's base guns are the Gattling
			 cannon alone, which tanks shrug off, so its tanks stand guard too, two Overlords to a cannon (Battlemasters
			 instead let the wave 5 boss through on every whole-budget run). */
		const char *buys[3] = { "GLAStingerSite", "GLATankMarauder", "GLATankMarauder" };
		const char *plant = NULL;
		if( side.compareNoCase( "USA" ) == 0 || side.compareNoCase( "America" ) == 0 )
		{
			buys[0] = "AmericaPatriotBattery";
			buys[1] = "AmericaFireBase";
			buys[2] = "AmericaPatriotBattery";
			plant = "AmericaPowerPlant";
		}
		else if( side.compareNoCase( "China" ) == 0 )
		{
			buys[0] = "ChinaTankOverlord";
			buys[1] = "ChinaGattlingCannon";
			buys[2] = "ChinaTankOverlord";
			plant = "ChinaPowerPlant";
		}
		for( Int guard = 0; guard < 8; ++guard )
		{
			const Bool needPower = plant != NULL && !seat->getEnergy()->hasSufficientPower();
			const ThingTemplate *tmpl = TheThingFactory->findTemplate( needPower ? plant : buys[ m_refBought[n] % 3 ], FALSE );
			if( tmpl == NULL )
				break;
			const Int cost = tmpl->calcCostToBuild( seat );
			const Int allowed = m_refEarned[n] * m_refPercent / 100 - m_refSpent[n];
			if( cost > allowed || cost > (Int)seat->getMoney()->countMoney() )
				break;
			Coord3D pos;
			Int spot = -1;
			if( needPower )
			{
				// beside the start, a ring of plants
				AsciiString name;
				name.format( "Player_%d_Start", n + 1 );
				const Waypoint *start = TheTerrainLogic->getWaypointByName( name );
				if( start == NULL )
					break;
				// a row of five, rows going on: any spread will do, the plants only have to stand apart and clear of the start
				const Int plants = m_refPlants[n]++;
				pos = *start->getLocation();
				pos.x += 70.0f * (Real)( ( plants % 5 ) - 2 );
				pos.y -= 160.0f + 70.0f * (Real)( plants / 5 );
			}
			else
			{
				/* The first spot whose gun is gone: a gun at the rim trades fire with the waves (they see it as it sees
					 them), and a player builds again where one fell. */
				std::vector<ObjectID> &held = m_refHeld[n];
				if( held.size() < m_refSpots[n].size() )
					held.resize( m_refSpots[n].size(), INVALID_ID );
				spot = -1;
				for( UnsignedInt s = 0; s < held.size() && spot < 0; ++s )
				{
					const Object *gun = held[s] != INVALID_ID ? TheGameLogic->findObjectByID( held[s] ) : NULL;
					if( gun == NULL || gun->isEffectivelyDead() )
						spot = (Int)s;
				}
				if( spot < 0 )
					break;
				pos = m_refSpots[n][spot];
				m_refNextSpot[n] = max( m_refNextSpot[n], spot + 1 );
			}
			pos.z = TheTerrainLogic->getGroundHeight( pos.x, pos.y );
			Object *obj = TheThingFactory->newObject( tmpl, seat->getDefaultTeam() );
			if( obj == NULL )
				break;
			obj->setPosition( &pos );
			for( BehaviorModule **m = obj->getBehaviorModules(); *m; ++m )
			{
				CreateModuleInterface *create = (*m)->getCreate();
				if( create )
					create->onBuildComplete();
			}
			TheAI->pathfinder()->addObjectToPathfindMap( obj );
			if( spot >= 0 )
				m_refHeld[n][spot] = obj->getID();
			seat->getMoney()->withdraw( (UnsignedInt)cost, FALSE );
			m_refSpent[n] += cost;
			if( !needPower )
				++m_refBought[n];
			DEBUG_LOG(( "HEADLESS AOD REF: frame %u lane %d buys '%s' for %d at (%.0f,%.0f), spent %d of %d earned\n", now, n + 1,
									tmpl->getName().str(), cost, pos.x, pos.y, m_refSpent[n], m_refEarned[n] ));
		}
	}
}

//-------------------------------------------------------------------------------------------------
void AodDirector::crc( Xfer *xfer )
{
	AodDirector::xfer( xfer );
}

//-------------------------------------------------------------------------------------------------
/** Version 1: the wave clock, the wave's units still to come, the walking units, lives and counts.
	  The lanes, the exit, the owner and the templates come from the map and the data on every load.
	  Version 2 adds each walking unit's lane and wave, and the lives lost to each wave: the waves walk
	  entry to exit, every lane at once (version 1 walked to a base; its units read lane 1, wave 0).
	  Version 3 adds the reference defence's book (a dev aid's, so a reloaded reference game does not buy
	  again on spots it took); its spots come from the map again (loadPostProcess).  Older saves keep
	  the book newMap began with.
	  Version 4 adds the money's book: what got out of each lane in each wave, which waves each lane has
	  settled, which lanes are owed the catch-up, that the start money was set, and how far each walking
	  unit has got and since when, and the reference's gun on each of its spots.  An older save had its
	  money set already and is taken as settled up to its last wave; its units start their watch at the
	  load; its reference sees every spot free and may build on one it holds. */
void AodDirector::xfer( Xfer *xfer )
{
	XferVersion currentVersion = 4;
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
		if( version >= 2 )
		{
			xfer->xferInt( &m_units[i].lane );
			xfer->xferInt( &m_units[i].wave );
		}
		else
		{
			m_units[i].lane = 0;
			m_units[i].wave = 0;
		}
	}
	if( version >= 2 )
		for( Int w = 0; w <= AOD_WAVES; ++w )
			xfer->xferInt( &m_waveLeaks[w] );

	if( version >= 3 )
	{
		xfer->xferInt( &m_refPercent );
		xfer->xferBool( &m_refStarted );
		for( Int n = 0; n < AOD_LANES_MOST; ++n )
		{
			xfer->xferInt( &m_refEarned[n] );
			xfer->xferInt( &m_refSpent[n] );
			xfer->xferInt( &m_refBought[n] );
			xfer->xferInt( &m_refNextSpot[n] );
			xfer->xferInt( &m_refPlants[n] );
		}
	}

	if( version >= 4 )
	{
		for( Int w = 0; w <= AOD_WAVES; ++w )
			for( Int n = 0; n < AOD_LANES_MOST; ++n )
			{
				xfer->xferInt( &m_laneLeaked[w][n] );
				xfer->xferBool( &m_settled[w][n] );
			}
		for( Int n = 0; n < AOD_LANES_MOST; ++n )
			xfer->xferBool( &m_catchUp[n] );
		xfer->xferBool( &m_cashSet );
		for( UnsignedInt i = 0; i < count; ++i )
		{
			xfer->xferInt( &m_units[i].best );
			xfer->xferUnsignedInt( &m_units[i].since );
		}
		for( Int n = 0; n < AOD_LANES_MOST; ++n )
		{
			UnsignedInt held = (UnsignedInt)m_refHeld[n].size();
			xfer->xferUnsignedInt( &held );
			if( xfer->getXferMode() == XFER_LOAD )
				m_refHeld[n].resize( held, INVALID_ID );
			for( UnsignedInt s = 0; s < held; ++s )
				xfer->xferObjectID( &m_refHeld[n][s] );
		}
	}
	else if( xfer->getXferMode() == XFER_LOAD )
	{
		for( UnsignedInt i = 0; i < count; ++i )
		{
			m_units[i].best = 0;
			m_units[i].since = TheGameLogic->getFrame();
		}
		m_cashSet = TRUE;
		for( Int w = 0; w < m_wave && w <= AOD_WAVES; ++w )
			for( Int n = 0; n < AOD_LANES_MOST; ++n )
				m_settled[w][n] = TRUE;
	}
}

//-------------------------------------------------------------------------------------------------
void AodDirector::loadPostProcess( void )
{
	// the reference's spots are the map's: found again, the same ones, so the saved next spot points where it did
	if( m_active && m_refStarted && m_refSpots.empty() )
		findReferenceSpots();
}
