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

// FILE: ApocalypseDirector.cpp ///////////////////////////////////////////////////////////////////
//
// The Apocalypse mode; ApocalypseDirector.h says what it does.  Everything here is logic state: time
// is the logic frame in game seconds, every draw comes from the logic stream, the zombies are kept
// in the order they came out, and nothing reads the client.  The light at the bottom is the client's:
// the logic hands it a night amount after a tick and never looks at what it did with it.
//
///////////////////////////////////////////////////////////////////////////////////////////////////

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "Common/GlobalData.h"
#include "Common/LogicThread.h"
#include "Common/NameKeyGenerator.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/Team.h"
#include "Common/ThingFactory.h"
#include "Common/ThingTemplate.h"
#include "Common/Xfer.h"
#include "GameClient/Display.h"
#include "GameClient/Drawable.h"
#include "GameClient/GameClient.h"
#include "GameLogic/AI.h"
#include "GameLogic/AIPathfind.h"
#include "GameLogic/ApocalypseDirector.h"
#include "GameLogic/ExperienceTracker.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/LogicRandomValue.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/CreateModule.h"
#include "GameLogic/Object.h"
#include "GameLogic/PartitionManager.h"
#include "GameLogic/PolygonTrigger.h"
#include "GameClient/ParticleSys.h"
#include <algorithm>
#include "GameLogic/TerrainLogic.h"
#include "GameLogic/Weapon.h"

static ApocalypseDirector theApocalypseDirector;
ApocalypseDirector *TheApocalypseDirector = &theApocalypseDirector;

/// the walker, then the two that come out at night
static const char *const ZOMBIE_TEMPLATES[] = { "ApocalypseZombie", "ApocalypseZombieRunner", "ApocalypseZombieBrute" };
/// a night zombie's kind out of 100: walkers under the first, runners under the second, brutes the rest
enum { NIGHT_WALKERS_PERCENT = 45, NIGHT_RUNNERS_PERCENT = 75 };

static const ApocalypsePhaseRules thePhaseRules[ APOCALYPSE_PHASES ] =
{
	//	seconds							wave every	size %	out a second	walking at most
	{ APOCALYPSE_DAY_SECONDS,		40,			100,		4,						120 },
	{ APOCALYPSE_DUSK_SECONDS,	30,			125,		5,						140 },
	{ APOCALYPSE_NIGHT_SECONDS,	15,			220,		8,						180 },
	{ APOCALYPSE_DAWN_SECONDS,	30,			125,		5,						140 },
};

const ApocalypsePhaseRules &Apocalypse_phaseRules( Int phase )
{
	return thePhaseRules[ phase >= 0 && phase < APOCALYPSE_PHASES ? phase : APOCALYPSE_DAY ];
}

const char *Apocalypse_phaseName( Int phase )
{
	static const char *const names[ APOCALYPSE_PHASES ] = { "day", "dusk", "night", "dawn" };
	return names[ phase >= 0 && phase < APOCALYPSE_PHASES ? phase : APOCALYPSE_DAY ];
}

Int Apocalypse_waveSize( UnsignedInt wave, Int seats, Int phase, UnsignedInt cycle )
{
	const Int plain = APOCALYPSE_WAVE_BASE + APOCALYPSE_WAVE_PER_SEAT * seats + APOCALYPSE_WAVE_GROWTH * (Int)( wave > 0 ? wave - 1 : 0 );
	const Int size = plain * Apocalypse_phaseRules( phase ).sizePercent / 100 * ( 100 + APOCALYPSE_CYCLE_GROWTH_PERCENT * (Int)cycle ) / 100;
	return size < APOCALYPSE_WAVE_MOST ? size : APOCALYPSE_WAVE_MOST;
}

Real Apocalypse_nightAmount( Int phase, UnsignedInt intoPhase, UnsignedInt phaseFrames )
{
	const Real t = phaseFrames > 0 ? (Real)( intoPhase < phaseFrames ? intoPhase : phaseFrames ) / (Real)phaseFrames : 1.0f;
	switch( phase )
	{
		case APOCALYPSE_DUSK:		return t;
		case APOCALYPSE_NIGHT:	return 1.0f;
		case APOCALYPSE_DAWN:		return 1.0f - t;
		default:								return 0.0f;
	}
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
// The light, client side.  Only ever called through LogicThread::callAfterTick, so it runs on the
// main thread between two ticks.  It writes the client's light, which the logic never reads, and the
// drawables' NIGHT state, which it does read through launch bones: that one is set on every tick as a
// pure function of the frame (handLight).
//-------------------------------------------------------------------------------------------------

static GlobalData::TerrainLighting theBaseTerrainLight[ MAX_GLOBAL_LIGHTS ];
static GlobalData::TerrainLighting theBaseObjectLight[ MAX_GLOBAL_LIGHTS ];
static Real theBaseShadowStrength = 0.0f;
static Bool theBaseLightKept = FALSE;

/// the map's own light for its hour, as it loaded, before any tint, and the sun shadows' own darkness
static void apocalypseKeepBaseLight( void )
{
	const Int tod = TheGlobalData->m_timeOfDay;
	for( Int k = 0; k < MAX_GLOBAL_LIGHTS; ++k )
	{
		theBaseTerrainLight[ k ] = TheGlobalData->m_terrainLighting[ tod ][ k ];
		theBaseObjectLight[ k ] = TheGlobalData->m_terrainObjectsLighting[ tod ][ k ];
	}
	theBaseShadowStrength = TheGlobalData->m_shadowMapStrength;
	theBaseLightKept = TRUE;
}

/** The shadow darkness is a setting of the whole game, not the map's, so it goes back when the match
	  ends; the light tables come back with the next map's own. */
static void apocalypseRestoreShadow( void )
{
	if( !theBaseLightKept )
		return;
	TheWritableGlobalData->m_shadowMapStrength = theBaseShadowStrength;
	theBaseLightKept = FALSE;
}

static void tint( RGBColor &out, const RGBColor &base, const Real *day, const Real *night, Real amount )
{
	out.red = base.red * ( day[ 0 ] + ( night[ 0 ] - day[ 0 ] ) * amount );
	out.green = base.green * ( day[ 1 ] + ( night[ 1 ] - day[ 1 ] ) * amount );
	out.blue = base.blue * ( day[ 2 ] + ( night[ 2 ] - day[ 2 ] ) * amount );
}

/** Tint the map's light `level` steps of APOCALYPSE_LIGHT_STEPS toward the red night (a faint warm haze
	  at 0), and put every drawable in its NIGHT state or out of it.

	  The night takes the sun down to seven tenths of its red and under a third of the rest, and lifts
	  the ambient a tenth in red while keeping three fifths of the rest.  An ambient cut as hard as the
	  sun left every face turned away from it black, so a red zombie read as a silhouette; a sun at full
	  red over a cut ambient summed past white in red alone, and the darker bands of palm trunks and
	  fronds went black while the light ones went orange, tiger stripes out of the texture itself.

	  The Direct3D 11 sun shadow darkens a blocked pixel by a fixed strength, whatever the light, so
	  under a dimmed sun it stayed as hard as at noon; the night halves it.  The -d3d9 stencil
	  shadows keep the map's own colour.

	  The ground's full relight costs a 40 to 56 ms frame (TERRAIN FULL UPDATE), so the tables move
	  every APOCALYPSE_LIGHT_TINT_EVERY steps of the amount, eight times a dusk; the NIGHT state goes
	  with every step.

	  The ground reads m_terrainAmbient and m_terrainDiffuse, GlobalData::setTimeOfDay's copies of the
	  hour's terrain light, and never the table itself: the Direct3D 11 ground's sun every frame
	  (HeightMap.cpp) and the vertex light the -d3d9 ground bakes in staticLightingChanged.  Tinting the
	  table alone left the ground at noon under red units, so the copies are written here too, the
	  hour itself untouched (the logic reads m_timeOfDay). */
static void apocalypseApplyLight( Int level, Bool relight, Bool lightsOn )
{
	if( relight && theBaseLightKept )
	{
		static const Real HAZE[ 3 ] = { 1.03f, 0.99f, 0.93f };
		static const Real NIGHT_AMBIENT[ 3 ] = { 1.10f, 0.62f, 0.60f };
		static const Real NIGHT_DIFFUSE[ 3 ] = { 0.70f, 0.32f, 0.28f };
		const Real amount = (Real)level / (Real)APOCALYPSE_LIGHT_STEPS;
		const Int tod = TheGlobalData->m_timeOfDay;
		for( Int k = 0; k < MAX_GLOBAL_LIGHTS; ++k )
		{
			GlobalData::TerrainLighting &terrain = TheWritableGlobalData->m_terrainLighting[ tod ][ k ];
			GlobalData::TerrainLighting &objects = TheWritableGlobalData->m_terrainObjectsLighting[ tod ][ k ];
			tint( terrain.ambient, theBaseTerrainLight[ k ].ambient, HAZE, NIGHT_AMBIENT, amount );
			tint( terrain.diffuse, theBaseTerrainLight[ k ].diffuse, HAZE, NIGHT_DIFFUSE, amount );
			tint( objects.ambient, theBaseObjectLight[ k ].ambient, HAZE, NIGHT_AMBIENT, amount );
			tint( objects.diffuse, theBaseObjectLight[ k ].diffuse, HAZE, NIGHT_DIFFUSE, amount );
			TheWritableGlobalData->m_terrainAmbient[ k ] = terrain.ambient;
			TheWritableGlobalData->m_terrainDiffuse[ k ] = terrain.diffuse;
		}
		// ponytail: 0.45 and 0.52 mirror SHADOW_MAP_STRENGTH and SHADOW_MAP_REFORGED_STRENGTH
		// (W3DVolumetricShadow.cpp), the strengths a 0 here stands for; move them to GlobalData if they change
		const Real dayStrength = theBaseShadowStrength > 0.0f ? theBaseShadowStrength
			: TheGlobalData->m_lightingStyle == LIGHTING_STYLE_REFORGED ? 0.52f : 0.45f;
		TheWritableGlobalData->m_shadowMapStrength = level > 0 ? dayStrength * ( 1.0f - 0.5f * amount ) : theBaseShadowStrength;
		if( !TheGlobalData->m_headless && TheDisplay != NULL )
			TheDisplay->setTimeOfDay( (TimeOfDay)tod );
	}

	if( TheGameClient == NULL )
		return;
	for( Drawable *draw = TheGameClient->firstDrawable(); draw != NULL; draw = draw->getNextDrawable() )
	{
		if( ( draw->getModelConditionFlags().test( MODELCONDITION_NIGHT ) != 0 ) == ( lightsOn != 0 ) )
			continue;
		ModelConditionFlags flags = draw->getModelConditionFlags();
		flags.set( MODELCONDITION_NIGHT, lightsOn ? 1 : 0 );
		draw->replaceModelConditionFlags( flags );
	}
}

//-------------------------------------------------------------------------------------------------
// The fog ring's colour and the smoke's, client side, handed over the same way as the light and
// kept apart from it: the shroud's colour (GlobalData::m_shroudColor, a multiply over every fogged
// and shrouded cell) goes from a faint warm red by day to a deep one at night, and so does the tint
// over every smoke and dust particle (ParticleSystemManager::setSmokeTint).  Both are settings of the
// whole game, kept when the map loads and given back when the match ends.
//-------------------------------------------------------------------------------------------------

static RGBColor theBaseShroudColor;
static Bool theBaseFogKept = FALSE;

static void apocalypseKeepFog( void )
{
	theBaseShroudColor = TheGlobalData->m_shroudColor;
	theBaseFogKept = TRUE;
}

static void apocalypseRestoreFog( void )
{
	if( !theBaseFogKept )
		return;
	TheWritableGlobalData->m_shroudColor = theBaseShroudColor;
	RGBColor white;
	white.red = white.green = white.blue = 1.0f;
	if( TheParticleSystemManager != NULL )
		TheParticleSystemManager->setSmokeTint( white );
	theBaseFogKept = FALSE;
}

/// `level` of APOCALYPSE_LIGHT_STEPS into the night
static void apocalypseApplyFog( Int level )
{
	if( !theBaseFogKept )
		return;
	const Real amount = (Real)level / (Real)APOCALYPSE_LIGHT_STEPS;
	static const Real FOG_DAY[ 3 ] = { 1.00f, 0.62f, 0.56f };
	// red stays whole: the Direct3D 11 fog memory reads the brightest channel as the shroud's light
	static const Real FOG_NIGHT[ 3 ] = { 1.00f, 0.26f, 0.21f };
	static const Real SMOKE_DAY[ 3 ] = { 1.00f, 0.88f, 0.82f };
	static const Real SMOKE_NIGHT[ 3 ] = { 0.95f, 0.45f, 0.38f };
	RGBColor fog, smoke;
	fog.red = FOG_DAY[ 0 ] + ( FOG_NIGHT[ 0 ] - FOG_DAY[ 0 ] ) * amount;
	fog.green = FOG_DAY[ 1 ] + ( FOG_NIGHT[ 1 ] - FOG_DAY[ 1 ] ) * amount;
	fog.blue = FOG_DAY[ 2 ] + ( FOG_NIGHT[ 2 ] - FOG_DAY[ 2 ] ) * amount;
	smoke.red = SMOKE_DAY[ 0 ] + ( SMOKE_NIGHT[ 0 ] - SMOKE_DAY[ 0 ] ) * amount;
	smoke.green = SMOKE_DAY[ 1 ] + ( SMOKE_NIGHT[ 1 ] - SMOKE_DAY[ 1 ] ) * amount;
	smoke.blue = SMOKE_DAY[ 2 ] + ( SMOKE_NIGHT[ 2 ] - SMOKE_DAY[ 2 ] ) * amount;
	TheWritableGlobalData->m_shroudColor = fog;
	if( TheParticleSystemManager != NULL )
		TheParticleSystemManager->setSmokeTint( smoke );
	// the shroud texture holds the colour per cell: write every cell again, the display's copy only
	if( ThePartitionManager != NULL )
		ThePartitionManager->redrawShroudForLocalPlayer();
}

//-------------------------------------------------------------------------------------------------
ApocalypseDirector::ApocalypseDirector( void )
{
	m_active = FALSE;
	reset();
}

//-------------------------------------------------------------------------------------------------
void ApocalypseDirector::reset( void )
{
	if( m_active )
	{
		LogicThread::callAfterTick( []() { apocalypseRestoreShadow(); } );
		LogicThread::callAfterTick( []() { apocalypseRestoreFog(); } );
	}
	m_active = FALSE;
	m_centre.zero();
	m_reachRadius = 0.0f;
	m_spawns.clear();
	m_zombiePlayer = -1;
	m_seats = 0;
	for( Int k = 0; k < KINDS; ++k )
		m_kinds[ k ] = NULL;
	m_phase = APOCALYPSE_DAY;
	m_phaseStartFrame = 0;
	m_cycle = 0;
	m_wave = 0;
	m_nextWaveFrame = 0;
	m_nextSpawnFrame = 0;
	m_pending = 0;
	m_spawned = 0;
	m_kills = 0;
	m_reached = 0;
	m_zombies.clear();
	m_hasRing = FALSE;
	m_clearLo.x = m_clearLo.y = m_clearHi.x = m_clearHi.y = 0.0f;
	m_playLo = m_clearLo;
	m_playHi = m_clearLo;
	m_fogStays.clear();
	m_fogKills = 0;
	m_fogLevel = -1;
	m_lightLevel = -1;
	m_lightsOn = FALSE;
	m_fallLogged = FALSE;
}

//-------------------------------------------------------------------------------------------------
void ApocalypseDirector::newMap( Bool loadingSaveGame )
{
	reset();

	const Waypoint *centre = TheTerrainLogic->getWaypointByName( "Apocalypse_Centre" );
	if( centre == NULL )
		return;
	for( Int k = 1; ; ++k )
	{
		AsciiString name;
		name.format( "Apocalypse_Spawn_%d", k );
		const Waypoint *spawn = TheTerrainLogic->getWaypointByName( name );
		if( spawn == NULL )
			break;
		m_spawns.push_back( *spawn->getLocation() );
	}
	if( m_spawns.empty() )
	{
		DEBUG_LOG(( "HEADLESS APOCALYPSE: the map has Apocalypse_Centre but no Apocalypse_Spawn_1, so the mode stays off\n" ));
		return;
	}
	Player *civilian = ThePlayerList->findPlayerWithNameKey( NAMEKEY( "PlyrCivilian" ) );
	if( civilian == NULL )
	{
		DEBUG_LOG(( "HEADLESS APOCALYPSE: the map has no PlyrCivilian to own the dead, so the mode stays off\n" ));
		m_spawns.clear();
		return;
	}

	m_active = TRUE;
	m_centre = *centre->getLocation();
	m_zombiePlayer = civilian->getPlayerIndex();
	Real farthest = 0.0f;
	for( Int k = 1; ; ++k )
	{
		AsciiString name;
		name.format( "Player_%d_Start", k );
		const Waypoint *start = TheTerrainLogic->getWaypointByName( name );
		if( start == NULL )
			break;
		const Real dx = start->getLocation()->x - m_centre.x, dy = start->getLocation()->y - m_centre.y;
		farthest = max( farthest, (Real)sqrtf( dx * dx + dy * dy ) );
	}
	m_reachRadius = farthest + (Real)APOCALYPSE_BASE_REACH;
	for( Int k = 0; k < KINDS; ++k )
	{
		m_kinds[ k ] = TheThingFactory->findTemplate( ZOMBIE_TEMPLATES[ k ], FALSE );
		if( m_kinds[ k ] == NULL )
		{
			DEBUG_LOG(( "HEADLESS APOCALYPSE: no '%s' template in the data, %s\n", ZOMBIE_TEMPLATES[ k ],
									k == KIND_WALKER ? "so nothing will spawn" : "so walkers come in its place" ));
			DEBUG_CRASH(( "Apocalypse: no '%s' template", ZOMBIE_TEMPLATES[ k ] ));
		}
	}

	for( Int i = 0; i < ThePlayerList->getPlayerCount(); ++i )
		if( isSeat( ThePlayerList->getNthPlayer( i ) ) )
			++m_seats;

	if( !loadingSaveGame )
	{
		// the map, not the lobby, makes the sides: every seat together, the dead against all of them
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

	// the fog ring: everything between the clear square and the map's edge, shrouded for good
	const PolygonTrigger *clear = TheTerrainLogic->getTriggerAreaByName( "Apocalypse_Clear" );
	if( clear != NULL && clear->getNumPoints() >= 3 )
	{
		ICoord3D lo = *clear->getPoint( 0 ), hi = lo;
		for( Int p = 1; p < clear->getNumPoints(); ++p )
		{
			const ICoord3D *pt = clear->getPoint( p );
			lo.x = min( lo.x, pt->x );
			lo.y = min( lo.y, pt->y );
			hi.x = max( hi.x, pt->x );
			hi.y = max( hi.y, pt->y );
		}
		Region3D extent;
		TheTerrainLogic->getExtent( &extent );
		m_playLo.x = extent.lo.x;
		m_playLo.y = extent.lo.y;
		m_playHi.x = extent.hi.x;
		m_playHi.y = extent.hi.y;
		m_clearLo.x = (Real)lo.x;
		m_clearLo.y = (Real)lo.y;
		m_clearHi.x = (Real)hi.x;
		m_clearHi.y = (Real)hi.y;
		m_hasRing = TRUE;
		const Int cells = ThePartitionManager->setAlwaysShroudedOutside( m_clearLo.x, m_clearLo.y, m_clearHi.x, m_clearHi.y );
		DEBUG_LOG(( "HEADLESS APOCALYPSE: fog ring outside (%.0f,%.0f)-(%.0f,%.0f) of the map (%.0f,%.0f)-(%.0f,%.0f), %d cells shrouded for good\n",
								m_clearLo.x, m_clearLo.y, m_clearHi.x, m_clearHi.y, m_playLo.x, m_playLo.y, m_playHi.x, m_playHi.y, cells ));
	}
	LogicThread::callAfterTick( []() { apocalypseKeepFog(); } );

	m_phaseStartFrame = TheGameLogic->getFrame();
	m_nextWaveFrame = TheGameLogic->getFrame() + APOCALYPSE_FIRST_WAVE_SECONDS * gameSecond();
	// the map has just put its own light in; a save load overwrites the clock below, not the light
	LogicThread::callAfterTick( []() { apocalypseKeepBaseLight(); } );
	DEBUG_LOG(( "HEADLESS APOCALYPSE: on; %d seats, %d spawns, centre (%.0f,%.0f), zombies of player %d, first wave at frame %u\n",
							m_seats, (Int)m_spawns.size(), m_centre.x, m_centre.y, m_zombiePlayer, m_nextWaveFrame ));
}

//-------------------------------------------------------------------------------------------------
Int ApocalypseDirector::seatsAlive( void ) const
{
	Int alive = 0;
	for( Int i = 0; i < ThePlayerList->getPlayerCount(); ++i )
	{
		const Player *player = ThePlayerList->getNthPlayer( i );
		if( isSeat( player ) && player->hasAnyObjects() )
			++alive;
	}
	return alive;
}

//-------------------------------------------------------------------------------------------------
/// an idle zombie goes at the nearest thing a seat has, or back to the middle when nothing is left
void ApocalypseDirector::sendOn( Object *obj )
{
	AIUpdateInterface *ai = obj->getAI();
	if( ai == NULL || !ai->isIdle() )
		return;
	PartitionFilterRelationship enemies( obj, PartitionFilterRelationship::ALLOW_ENEMIES );
	PartitionFilterAlive alive;
	PartitionFilter *filters[] = { &enemies, &alive, NULL };
	const Object *target = ThePartitionManager->getClosestObject( obj, 1.0e6f, FROM_CENTER_2D, filters );
	ai->aiAttackMoveToPosition( target != NULL ? target->getPosition() : &m_centre, NO_MAX_SHOTS_LIMIT, CMD_FROM_AI );
}

//-------------------------------------------------------------------------------------------------
void ApocalypseDirector::spawnOne( UnsignedInt now )
{
	Int kind = KIND_WALKER;
	if( m_phase == APOCALYPSE_NIGHT )
	{
		const Int roll = GameLogicRandomValue( 0, 99 );
		kind = roll < NIGHT_WALKERS_PERCENT ? KIND_WALKER : roll < NIGHT_RUNNERS_PERCENT ? KIND_RUNNER : KIND_BRUTE;
		if( m_kinds[ kind ] == NULL )
			kind = KIND_WALKER;
	}
	Team *team = ThePlayerList->getNthPlayer( m_zombiePlayer )->getDefaultTeam();
	Object *obj = TheThingFactory->newObject( m_kinds[ kind ], team );
	if( obj == NULL )
		return;
	obj->setOrientation( 0.0f );
	AIUpdateInterface *ai = obj->getAI();

	// anywhere in the fog ring the dead can stand; on a map without one, or where no draw found such a
	// spot, at a spawn waypoint
	Coord3D pos;
	Int which = -1;
	if( !m_hasRing || !ringSpot( &pos, obj ) )
	{
		which = GameLogicRandomValue( 0, (Int)m_spawns.size() - 1 );
		const Coord3D &spawn = m_spawns[ which ];
		pos = spawn;
		pos.x += GameLogicRandomValueReal( -(Real)APOCALYPSE_SPAWN_SPREAD, (Real)APOCALYPSE_SPAWN_SPREAD );
		pos.y += GameLogicRandomValueReal( -(Real)APOCALYPSE_SPAWN_SPREAD, (Real)APOCALYPSE_SPAWN_SPREAD );
		pos.z = TheTerrainLogic->getGroundHeight( pos.x, pos.y );
		// a tree or a rock where the spread put it: the spawn itself, which is on the clear trail
		if( ai != NULL && !TheAI->pathfinder()->validMovementPosition( obj->getCrusherLevel() > 0, obj->getLayer(), ai->getLocomotorSet(), &pos ) )
		{
			pos = spawn;
			pos.z = TheTerrainLogic->getGroundHeight( pos.x, pos.y );
		}
	}
	obj->setPosition( &pos );
	for( BehaviorModule **m = obj->getBehaviorModules(); *m; ++m )
	{
		CreateModuleInterface *create = (*m)->getCreate();
		if( create )
			create->onBuildComplete();
	}
	const Int rank = Apocalypse_cycleRank( m_cycle );
	if( rank > LEVEL_REGULAR && obj->getExperienceTracker() != NULL )
		obj->getExperienceTracker()->setVeterancyLevel( (VeterancyLevel)rank, FALSE );
	team->setActive();
	TheAI->pathfinder()->addObjectToPathfindMap( obj );

	Zombie z;
	z.id = obj->getID();
	z.reached = FALSE;
	m_zombies.push_back( z );
	++m_spawned;
	if( ai != NULL )
		ai->aiAttackMoveToPosition( &m_centre, NO_MAX_SHOTS_LIMIT, CMD_FROM_AI );
	if( m_spawned <= 3 || ( kind != KIND_WALKER && m_spawned % 20 == 0 ) )
	{
		AsciiString where = "in the fog";
		if( which >= 0 )
			where.format( "at spawn %d", which + 1 );
		DEBUG_LOG(( "HEADLESS APOCALYPSE: frame %u '%s' %d out %s (%.0f,%.0f) at rank %d\n", now,
								m_kinds[ kind ]->getName().str(), (Int)z.id, where.str(), pos.x, pos.y, rank ));
	}
}

//-------------------------------------------------------------------------------------------------
Bool ApocalypseDirector::inRing( const Coord3D *pos ) const
{
	return m_hasRing && ( pos->x < m_clearLo.x || pos->x > m_clearHi.x || pos->y < m_clearLo.y || pos->y > m_clearHi.y );
}

//-------------------------------------------------------------------------------------------------
/** A spot in the fog ring, off the map's edge and off the clear square by APOCALYPSE_FOG_EDGE_IN, where
	  the object can stand; drawn from the logic stream, FALSE after APOCALYPSE_FOG_TRIES misses. */
Bool ApocalypseDirector::ringSpot( Coord3D *pos, const Object *obj )
{
	const Real in = (Real)APOCALYPSE_FOG_EDGE_IN;
	const AIUpdateInterface *ai = obj->getAI();
	for( Int tryNo = 0; tryNo < APOCALYPSE_FOG_TRIES; ++tryNo )
	{
		pos->x = GameLogicRandomValueReal( m_playLo.x + in, m_playHi.x - in );
		pos->y = GameLogicRandomValueReal( m_playLo.y + in, m_playHi.y - in );
		if( pos->x > m_clearLo.x - in && pos->x < m_clearHi.x + in && pos->y > m_clearLo.y - in && pos->y < m_clearHi.y + in )
			continue;
		pos->z = TheTerrainLogic->getGroundHeight( pos->x, pos->y );
		if( ai == NULL || TheAI->pathfinder()->validMovementPosition( obj->getCrusherLevel() > 0, obj->getLayer(), ai->getLocomotorSet(), pos ) )
			return TRUE;
	}
	return FALSE;
}

//-------------------------------------------------------------------------------------------------
/** Anything of the living (a unit of a seat or of anyone but the dead and the neutral map) that has
	  stood in the fog ring APOCALYPSE_FOG_GRACE_SECONDS dies there.  Walked in object list order, the
	  stays kept sorted by ObjectID, so every machine kills the same thing on the same frame. */
void ApocalypseDirector::killInFog( UnsignedInt now )
{
	const UnsignedInt grace = APOCALYPSE_FOG_GRACE_SECONDS * gameSecond();
	const Player *neutral = ThePlayerList->getNeutralPlayer();
	std::vector<FogStay> stays;
	std::vector<Object *> doomed;
	for( Object *obj = TheGameLogic->getFirstObject(); obj; obj = obj->getNextObject() )
	{
		const Player *owner = obj->getControllingPlayer();
		if( owner == NULL || owner == neutral || owner->getPlayerIndex() == m_zombiePlayer || obj->isEffectivelyDead()
				|| obj->getContainedBy() != NULL
				|| !( obj->isKindOf( KINDOF_INFANTRY ) || obj->isKindOf( KINDOF_VEHICLE ) || obj->isKindOf( KINDOF_AIRCRAFT ) )
				|| !inRing( obj->getPosition() ) )
			continue;
		FogStay stay;
		stay.id = obj->getID();
		stay.since = now;
		std::vector<FogStay>::const_iterator was = std::lower_bound( m_fogStays.begin(), m_fogStays.end(), stay,
			[]( const FogStay &a, const FogStay &b ) { return a.id < b.id; } );
		if( was != m_fogStays.end() && was->id == stay.id )
			stay.since = was->since;
		if( now - stay.since >= grace )
			doomed.push_back( obj );
		else
			stays.push_back( stay );
	}
	std::sort( stays.begin(), stays.end(), []( const FogStay &a, const FogStay &b ) { return a.id < b.id; } );
	m_fogStays.swap( stays );
	for( UnsignedInt i = 0; i < doomed.size(); ++i )
	{
		Object *obj = doomed[ i ];
		++m_fogKills;
		DEBUG_LOG(( "HEADLESS APOCALYPSE: frame %u '%s' %d of player %d at (%.0f,%.0f) died in the fog, %u so far\n", now,
								obj->getTemplate()->getName().str(), (Int)obj->getID(), obj->getControllingPlayer()->getPlayerIndex(),
								obj->getPosition()->x, obj->getPosition()->y, m_fogKills ));
		obj->kill();
	}
}

//-------------------------------------------------------------------------------------------------
void ApocalypseDirector::advancePhase( UnsignedInt now )
{
	const UnsignedInt length = Apocalypse_phaseRules( m_phase ).seconds * gameSecond();
	if( now < m_phaseStartFrame + length )
		return;
	m_phase = ( m_phase + 1 ) % APOCALYPSE_PHASES;
	m_phaseStartFrame = now;
	if( m_phase == APOCALYPSE_DAY )
		++m_cycle;
	// the night does not wait out the day's wave clock
	if( m_phase == APOCALYPSE_NIGHT )
	{
		const UnsignedInt falls = now + APOCALYPSE_NIGHT_FALLS_SECONDS * gameSecond();
		if( m_nextWaveFrame > falls )
			m_nextWaveFrame = falls;
	}
	DEBUG_LOG(( "HEADLESS APOCALYPSE: phase %s, cycle %u, frame %u, next wave at frame %u, %d walking\n",
							Apocalypse_phaseName( m_phase ), m_cycle, now, m_nextWaveFrame, (Int)m_zombies.size() ));
}

//-------------------------------------------------------------------------------------------------
/** Hand the client the night amount after every tick.  The drawables' NIGHT state is a pure function of
	  the frame, set on every one of them after every tick, because the logic does read it back: a
	  model's launch and exit bones come from its drawable's current state (Drawable::
	  getProjectileLaunchOffset), so a drawable made since the last hand-out, or one restored from a
	  save, must not keep another state than the straight game gives it.  Only the retint waits for
	  the amount to move a step (m_lightLevel, which nothing in the logic reads). */
void ApocalypseDirector::handLight( UnsignedInt now )
{
	const UnsignedInt length = Apocalypse_phaseRules( m_phase ).seconds * gameSecond();
	const Real amount = Apocalypse_nightAmount( m_phase, now - m_phaseStartFrame, length );
	const Int level = (Int)( amount * (Real)APOCALYPSE_LIGHT_STEPS + 0.5f );
	const Bool lightsOn = level * 2 >= APOCALYPSE_LIGHT_STEPS;
	// the tables move a quarter as often as the amount: the ground's relight is a long frame
	const Bool relight = m_lightLevel < 0 || level / APOCALYPSE_LIGHT_TINT_EVERY != m_lightLevel / APOCALYPSE_LIGHT_TINT_EVERY;
	if( lightsOn != m_lightsOn )
		DEBUG_LOG(( "HEADLESS APOCALYPSE: frame %u lights %s\n", now, lightsOn ? "on" : "off" ));
	m_lightLevel = level;
	m_lightsOn = lightsOn;
	LogicThread::callAfterTick( [level, relight, lightsOn]() { apocalypseApplyLight( level, relight, lightsOn ); } );
}

//-------------------------------------------------------------------------------------------------
void ApocalypseDirector::update( void )
{
	if( !m_active )
		return;
	const UnsignedInt now = TheGameLogic->getFrame();
	const UnsignedInt second = gameSecond();

	advancePhase( now );
	const ApocalypsePhaseRules &rules = Apocalypse_phaseRules( m_phase );

	// the dead come off the list, the living are checked for the middle and, now and then, sent on
	const UnsignedInt retarget = APOCALYPSE_RETARGET_SECONDS * second;
	const Real reach2 = m_reachRadius * m_reachRadius;
	UnsignedInt kept = 0;
	for( UnsignedInt i = 0; i < m_zombies.size(); ++i )
	{
		Zombie z = m_zombies[ i ];
		Object *obj = TheGameLogic->findObjectByID( z.id );
		if( obj == NULL || obj->isEffectivelyDead() )
		{
			++m_kills;
			continue;
		}
		if( !z.reached )
		{
			const Real dx = obj->getPosition()->x - m_centre.x, dy = obj->getPosition()->y - m_centre.y;
			if( dx * dx + dy * dy < reach2 )
			{
				z.reached = TRUE;
				++m_reached;
				if( m_reached <= 3 || m_reached % 50 == 0 )
					DEBUG_LOG(( "HEADLESS APOCALYPSE: frame %u zombie %d reached the bases, %u have\n", now, (Int)z.id, m_reached ));
			}
		}
		if( ( now + (UnsignedInt)z.id ) % retarget == 0 )
			sendOn( obj );
		m_zombies[ kept++ ] = z;
	}
	m_zombies.resize( kept );

	if( m_kinds[ KIND_WALKER ] != NULL && now >= m_nextWaveFrame )
	{
		++m_wave;
		const Int size = Apocalypse_waveSize( m_wave, m_seats, m_phase, m_cycle );
		m_pending += size;
		m_nextWaveFrame = now + rules.waveSeconds * second;
		DEBUG_LOG(( "HEADLESS APOCALYPSE: frame %u %s wave %u of cycle %u sends %d at rank %d; %d walking, %d to come, next wave at frame %u\n",
								now, Apocalypse_phaseName( m_phase ), m_wave, m_cycle, size, Apocalypse_cycleRank( m_cycle ),
								(Int)m_zombies.size(), m_pending, m_nextWaveFrame ));
	}

	if( m_pending > 0 && now >= m_nextSpawnFrame && (Int)m_zombies.size() < rules.aliveCap )
	{
		spawnOne( now );
		--m_pending;
		const UnsignedInt step = second / (UnsignedInt)rules.spawnsPerSecond;
		m_nextSpawnFrame = now + ( step > 0 ? step : 1 );
	}

	handLight( now );

	// the ring takes what lingers in it, twice a game second
	if( m_hasRing && now % max( second / 2, 1U ) == 0 )
		killInFog( now );

	// the fog ring's and the smoke's red, an eighth of the way at a time (each step redraws the shroud)
	{
		const UnsignedInt length = Apocalypse_phaseRules( m_phase ).seconds * second;
		const Real amount = Apocalypse_nightAmount( m_phase, now - m_phaseStartFrame, length );
		const Int level = (Int)( amount * (Real)APOCALYPSE_LIGHT_STEPS + 0.5f ) / APOCALYPSE_LIGHT_TINT_EVERY * APOCALYPSE_LIGHT_TINT_EVERY;
		if( level != m_fogLevel )
		{
			m_fogLevel = level;
			LogicThread::callAfterTick( [level]() { apocalypseApplyFog( level ); } );
		}
	}

	if( now % ( APOCALYPSE_LOG_SECONDS * second ) == 0 )
	{
		const Int standing = seatsAlive();
		DEBUG_LOG(( "HEADLESS APOCALYPSE: frame %u %s cycle %u wave %u walking %d spawned %u kills %u reached %u to come %d seats standing %d of %d, fog took %u\n",
								now, Apocalypse_phaseName( m_phase ), m_cycle, m_wave, (Int)m_zombies.size(), m_spawned, m_kills, m_reached,
								m_pending, standing, m_seats, m_fogKills ));
		if( standing == 0 && !m_fallLogged )
		{
			m_fallLogged = TRUE;
			DEBUG_LOG(( "HEADLESS APOCALYPSE: frame %u the last seat fell at %s of cycle %u, wave %u, after %u kills\n", now,
									Apocalypse_phaseName( m_phase ), m_cycle, m_wave, m_kills ));
		}
	}
}

//-------------------------------------------------------------------------------------------------
void ApocalypseDirector::crc( Xfer *xfer )
{
	ApocalypseDirector::xfer( xfer );
}

//-------------------------------------------------------------------------------------------------
/** Version 1: the day's clock, the wave clock, the counts and the living zombies.  The centre, the
	  spawns, the owner and the templates come from the map and the data on every load, and the light
	  is handed to the client again on the first tick after it. */
void ApocalypseDirector::xfer( Xfer *xfer )
{
	XferVersion currentVersion = 2;
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );

	xfer->xferInt( &m_phase );
	xfer->xferUnsignedInt( &m_phaseStartFrame );
	xfer->xferUnsignedInt( &m_cycle );
	xfer->xferUnsignedInt( &m_wave );
	xfer->xferUnsignedInt( &m_nextWaveFrame );
	xfer->xferUnsignedInt( &m_nextSpawnFrame );
	xfer->xferInt( &m_pending );
	xfer->xferUnsignedInt( &m_spawned );
	xfer->xferUnsignedInt( &m_kills );
	xfer->xferUnsignedInt( &m_reached );

	UnsignedInt count = (UnsignedInt)m_zombies.size();
	xfer->xferUnsignedInt( &count );
	if( xfer->getXferMode() == XFER_LOAD )
		m_zombies.resize( count );
	for( UnsignedInt i = 0; i < count; ++i )
	{
		xfer->xferObjectID( &m_zombies[ i ].id );
		xfer->xferBool( &m_zombies[ i ].reached );
	}

	if( version >= 2 )
	{
		xfer->xferUnsignedInt( &m_fogKills );
		count = (UnsignedInt)m_fogStays.size();
		xfer->xferUnsignedInt( &count );
		if( xfer->getXferMode() == XFER_LOAD )
			m_fogStays.resize( count );
		for( UnsignedInt i = 0; i < count; ++i )
		{
			xfer->xferObjectID( &m_fogStays[ i ].id );
			xfer->xferUnsignedInt( &m_fogStays[ i ].since );
		}
	}
}
