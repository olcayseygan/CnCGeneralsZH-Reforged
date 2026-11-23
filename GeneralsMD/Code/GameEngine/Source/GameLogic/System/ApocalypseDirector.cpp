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
#include "GameLogic/Damage.h"
#include "GameLogic/Module/BodyModule.h"
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
static const Int ZOMBIE_POINTS[] = { APOCALYPSE_POINTS_WALKER, APOCALYPSE_POINTS_RUNNER, APOCALYPSE_POINTS_BRUTE };

static const ApocalypsePhaseRules thePhaseRules[ APOCALYPSE_PHASES ] =
{
	//	seconds							wave every	points %	out a frame	walking at most	gates		runners	brutes
	{ APOCALYPSE_DAY_SECONDS,		40,			100,			5,					450,						1, 2,		0,			5 },
	{ APOCALYPSE_DUSK_SECONDS,	30,			220,			6,					550,						2, 2,		20,			15 },
	{ APOCALYPSE_NIGHT_SECONDS,	25,			450,			10,					700,						2, 99,	30,			30 },
	{ APOCALYPSE_DAWN_SECONDS,	30,			200,			6,					550,						1, 2,		10,			5 },
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

Int Apocalypse_wavePoints( UnsignedInt wave, Int seats, Int phase, UnsignedInt cycle, Int adaptPercent )
{
	const Int plain = APOCALYPSE_WAVE_BASE + APOCALYPSE_WAVE_PER_SEAT * seats + APOCALYPSE_WAVE_GROWTH * (Int)( wave > 0 ? wave - 1 : 0 );
	const Int points = plain * Apocalypse_phaseRules( phase ).sizePercent / 100 * ( 100 + APOCALYPSE_CYCLE_GROWTH_PERCENT * (Int)cycle ) / 100
		* adaptPercent / 100;
	return points < APOCALYPSE_WAVE_MOST ? points : APOCALYPSE_WAVE_MOST;
}

Int Apocalypse_adapt( Int adaptPercent, Bool reachedBases, Bool cleared )
{
	Int next = adaptPercent;
	if( reachedBases )
		next -= APOCALYPSE_ADAPT_DOWN;
	else
		next += cleared ? APOCALYPSE_ADAPT_UP : APOCALYPSE_ADAPT_SLOW;
	return next < APOCALYPSE_ADAPT_LEAST ? APOCALYPSE_ADAPT_LEAST : next > APOCALYPSE_ADAPT_MOST ? APOCALYPSE_ADAPT_MOST : next;
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

void Apocalypse_phaseAt( UnsignedInt frame, Int *phase, UnsignedInt *intoPhase, UnsignedInt *phaseFrames, UnsignedInt *cycle )
{
	UnsignedInt cycleFrames = 0;
	for( Int p = 0; p < APOCALYPSE_PHASES; ++p )
		cycleFrames += Apocalypse_phaseRules( p ).seconds * gameSecond();
	*cycle = frame / cycleFrames;
	UnsignedInt into = frame % cycleFrames;
	for( Int p = 0; p < APOCALYPSE_PHASES; ++p )
	{
		const UnsignedInt length = Apocalypse_phaseRules( p ).seconds * gameSecond();
		if( into < length || p == APOCALYPSE_PHASES - 1 )
		{
			*phase = p;
			*intoPhase = into;
			*phaseFrames = length;
			return;
		}
		into -= length;
	}
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

/* The heat in the fog, client side: how thick the dead stand in each block of APOCALYPSE_HEAT_BLOCK
	partition cells a side, counted by the director every APOCALYPSE_HEAT_SECONDS and handed over after
	the tick.  The ring's shroud reads it back per cell (Apocalypse_heatAt) and thins where it is hot,
	so the red fog glows over a gathering horde.  Never saved: the next count rebuilds it. */
static std::vector<UnsignedByte> theHeat;
static Int theHeatBlocksX = 0, theHeatBlocksY = 0;

static void apocalypseApplyHeat( const std::vector<UnsignedByte> &heat, Int blocksX, Int blocksY )
{
	theHeat = heat;
	theHeatBlocksX = blocksX;
	theHeatBlocksY = blocksY;
	ThePartitionManager->redrawShroudForLocalPlayer();
}

void Apocalypse_heatGrid( Int *blocksX, Int *blocksY )
{
	*blocksX = theHeatBlocksX;
	*blocksY = theHeatBlocksY;
}

Int Apocalypse_heatOfBlock( Int blockX, Int blockY )
{
	if( blockX < 0 || blockY < 0 || blockX >= theHeatBlocksX || blockY >= theHeatBlocksY )
		return 0;
	return theHeat[ blockY * theHeatBlocksX + blockX ];
}

Int Apocalypse_heatAt( Int cellX, Int cellY )
{
	if( theHeat.empty() )
		return 0;
	// between the four nearest blocks' middles, so a horde reads as a soft blob and not as squares
	const Real bx = ( (Real)cellX + 0.5f ) / (Real)APOCALYPSE_HEAT_BLOCK - 0.5f;
	const Real by = ( (Real)cellY + 0.5f ) / (Real)APOCALYPSE_HEAT_BLOCK - 0.5f;
	const Int x0 = (Int)floorf( bx ), y0 = (Int)floorf( by );
	const Real tx = bx - (Real)x0, ty = by - (Real)y0;
	const Real top = Apocalypse_heatOfBlock( x0, y0 ) + ( Apocalypse_heatOfBlock( x0 + 1, y0 ) - Apocalypse_heatOfBlock( x0, y0 ) ) * tx;
	const Real bottom = Apocalypse_heatOfBlock( x0, y0 + 1 ) + ( Apocalypse_heatOfBlock( x0 + 1, y0 + 1 ) - Apocalypse_heatOfBlock( x0, y0 + 1 ) ) * tx;
	return (Int)( top + ( bottom - top ) * ty + 0.5f );
}

static void apocalypseKeepFog( void )
{
	theBaseShroudColor = TheGlobalData->m_shroudColor;
	theBaseFogKept = TRUE;
}

static void apocalypseRestoreFog( void )
{
	theHeat.clear();
	theHeatBlocksX = theHeatBlocksY = 0;
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
	m_waveSectors = 0;
	m_warnSectors = 0;
	m_adapt = APOCALYPSE_ADAPT_START;
	m_frontTurn = 0;
	m_reachedAtWave = 0;
	m_rises.clear();
	m_infected = 0;
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
		if( k > APOCALYPSE_SPAWNS_MOST )
		{
			DEBUG_LOG(( "HEADLESS APOCALYPSE: the map has more than %d spawns, the rest stay empty\n", APOCALYPSE_SPAWNS_MOST ));
			break;
		}
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
	// a co-op mode for four at the most: the waves never size for more, whatever the map was played with
	if( m_seats > APOCALYPSE_SEATS_MOST )
		m_seats = APOCALYPSE_SEATS_MOST;

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

	// the fog ring: everything between the clear area's outline and the map's edge, shrouded for good
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
		// the cells outside the area's own outline, which is what inRing reads back: shroud, kill test and spawns agree
		const Int cells = ThePartitionManager->setAlwaysShroudedOutside( clear );
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
Bool ApocalypseDirector::isZombie( const Object *obj ) const
{
	if( !m_active || obj == NULL || obj->getControllingPlayer() == NULL || obj->getControllingPlayer()->getPlayerIndex() != m_zombiePlayer )
		return FALSE;
	for( Int k = 0; k < KINDS; ++k )
		if( m_kinds[ k ] != NULL && obj->getTemplate()->isEquivalentTo( m_kinds[ k ] ) )
			return TRUE;
	return FALSE;
}

//-------------------------------------------------------------------------------------------------
/** A plane flying a general's power in from off the map: a DeliverPayload transport (the Supply Drop
	  Zone's and the Supply Drop's cargo plane, the paradrops, the A-10, B-52 and carpet bomber strikes)
	  or the Spectre gunship.  The power, not the player, flies them, and they come in over the map's
	  edge, through the fog ring: the cargo plane flies 125 a game second, so the 4 s grace killed it
	  500 in with its cargo still aboard.  Every aircraft the player orders still dies there. */
static Bool isOffMapDelivery( const Object *obj )
{
	static const NameKeyType key_DeliverPayloadAIUpdate = NAMEKEY( "DeliverPayloadAIUpdate" );
	static const NameKeyType key_SpectreGunshipUpdate = NAMEKEY( "SpectreGunshipUpdate" );
	return obj->findUpdateModule( key_DeliverPayloadAIUpdate ) != NULL || obj->findUpdateModule( key_SpectreGunshipUpdate ) != NULL;
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
	// the kind out of the phase's mix, a walker whenever the points left cannot pay for it
	const ApocalypsePhaseRules &rules = Apocalypse_phaseRules( m_phase );
	Int kind = KIND_WALKER;
	const Int roll = GameLogicRandomValue( 0, 99 );
	if( roll < rules.brutePercent )
		kind = KIND_BRUTE;
	else if( roll < rules.brutePercent + rules.runnerPercent )
		kind = KIND_RUNNER;
	if( m_kinds[ kind ] == NULL || ZOMBIE_POINTS[ kind ] > m_pending )
		kind = KIND_WALKER;
	m_pending -= ZOMBIE_POINTS[ kind ];

	// one of the gates the wave gathered at
	Int sector = -1;
	{
		Int bits = 0;
		for( Int g = 0; g < (Int)m_spawns.size(); ++g )
			if( m_waveSectors & ( 1u << g ) )
				++bits;
		if( bits > 0 )
		{
			Int pick = GameLogicRandomValue( 0, bits - 1 );
			for( Int g = 0; g < (Int)m_spawns.size(); ++g )
				if( ( m_waveSectors & ( 1u << g ) ) && pick-- == 0 )
				{
					sector = g;
					break;
				}
		}
	}

	Team *team = ThePlayerList->getNthPlayer( m_zombiePlayer )->getDefaultTeam();
	Object *obj = TheThingFactory->newObject( m_kinds[ kind ], team );
	if( obj == NULL )
		return;
	obj->setOrientation( 0.0f );
	AIUpdateInterface *ai = obj->getAI();

	// in the fog ring before that gate; on a map without one, or where no draw found such a spot, at the
	// gate's spawn waypoint
	Coord3D pos;
	Int which = -1;
	if( !m_hasRing || !ringSpot( &pos, obj, sector ) )
	{
		which = sector >= 0 ? sector : GameLogicRandomValue( 0, (Int)m_spawns.size() - 1 );
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
	obj = makeZombie( kind, pos, now, obj );
	if( obj != NULL && ( m_spawned <= 3 || ( kind != KIND_WALKER && m_spawned % 25 == 0 ) ) )
	{
		AsciiString where;
		if( which >= 0 )
			where.format( "at spawn %d", which + 1 );
		else
			where.format( "in the fog before gate %d", sector + 1 );
		DEBUG_LOG(( "HEADLESS APOCALYPSE: frame %u '%s' %d out %s (%.0f,%.0f)\n", now,
								m_kinds[ kind ]->getName().str(), (Int)obj->getID(), where.str(), pos.x, pos.y ));
	}
}

//-------------------------------------------------------------------------------------------------
/** One of the dead of that kind standing at pos, built, ranked for the cycle, on the list and walking at
	  the middle; NULL when the data has no such template.  `made` is one already made of that kind
	  (spawnOne makes it first to ask where it may stand), NULL to make one here. */
Object *ApocalypseDirector::makeZombie( Int kind, const Coord3D &pos, UnsignedInt now, Object *made )
{
	if( m_kinds[ kind ] == NULL )
		return NULL;
	Team *team = ThePlayerList->getNthPlayer( m_zombiePlayer )->getDefaultTeam();
	Object *obj = made != NULL ? made : TheThingFactory->newObject( m_kinds[ kind ], team );
	if( obj == NULL )
		return NULL;
	obj->setOrientation( 0.0f );
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
	if( m_spawned == 1 )
		DEBUG_LOG(( "HEADLESS APOCALYPSE: frame %u first of the dead '%s' %d selectable %d\n", now,
								obj->getTemplate()->getName().str(), (Int)z.id, obj->isSelectable() ? 1 : 0 ));
	if( obj->getAI() != NULL )
		obj->getAI()->aiAttackMoveToPosition( &m_centre, NO_MAX_SHOTS_LIMIT, CMD_FROM_AI );
	return obj;
}

//-------------------------------------------------------------------------------------------------
/// the gate whose bearing from the middle lies nearest that point's
Int ApocalypseDirector::sectorOf( Real x, Real y ) const
{
	const Real dx = x - m_centre.x, dy = y - m_centre.y;
	Int best = 0;
	Real bestDot = -1.0e30f;
	for( Int g = 0; g < (Int)m_spawns.size(); ++g )
	{
		const Real gx = m_spawns[ g ].x - m_centre.x, gy = m_spawns[ g ].y - m_centre.y;
		const Real length = sqrtf( gx * gx + gy * gy );
		const Real dot = length > 0.0f ? ( dx * gx + dy * gy ) / length : 0.0f;
		if( dot > bestDot )
		{
			bestDot = dot;
			best = g;
		}
	}
	return best;
}

//-------------------------------------------------------------------------------------------------
Bool ApocalypseDirector::isWarnedCell( Int cellX, Int cellY ) const
{
	if( m_warnSectors == 0 || ThePartitionManager == NULL )
		return FALSE;
	Real x, y;
	ThePartitionManager->getCellCenterPos( cellX, cellY, x, y );
	return ( m_warnSectors & ( 1u << sectorOf( x, y ) ) ) != 0;
}

//-------------------------------------------------------------------------------------------------
/// the gate of the n-th set bit (from 0) of a gate mask, in gate order; 0 when the mask has fewer
static Int nthGate( UnsignedInt mask, Int n )
{
	for( Int g = 0; g < APOCALYPSE_SPAWNS_MOST; ++g )
		if( ( mask & ( 1u << g ) ) && n-- == 0 )
			return g;
	return 0;
}

//-------------------------------------------------------------------------------------------------
/** The next wave picks its gates, the phase's count of them from the logic stream, and they glow for
	  APOCALYPSE_TELEGRAPH_SECONDS before it comes. */
void ApocalypseDirector::gatherWave( UnsignedInt now )
{
	const ApocalypsePhaseRules &rules = Apocalypse_phaseRules( m_phase );
	const Int gates = (Int)m_spawns.size();
	Int want = GameLogicRandomValue( rules.sectorsLeast, rules.sectorsMost < gates ? rules.sectorsMost : gates );
	if( want > gates )
		want = gates;
	if( want < 1 )
		want = 1;
	UnsignedInt mask = 0;
	AsciiString names;
	/* Co-op: the map's pass N faces Player_N_Start's lobe, the passes past the starts are seams between two
		lobes.  The fronts are the passes of the starts a seat holds, so a player alone on start 2 is not
		left watching the waves walk into lobe 1; a start nobody holds is a seam like the rest.  The first
		gate is the next front in turn, in gate order, so every seat is hit; every third wave it is a seam
		instead, which two neighbours must hold together. */
	{
		UnsignedInt fronts = 0;
		for( Int i = 0; i < ThePlayerList->getPlayerCount(); ++i )
		{
			Player *player = ThePlayerList->getNthPlayer( i );
			const Int start = isSeat( player ) ? player->getMpStartIndex() : -1;
			if( start >= 0 && start < gates )
				fronts |= 1u << start;
		}
		// no seat on a start the map knows: the first passes, one a seat, as before
		for( Int g = 0; fronts == 0 && g < m_seats && g < gates; ++g )
			fronts |= 1u << g;
		Int frontCount = 0;
		for( Int g = 0; g < gates; ++g )
			if( fronts & ( 1u << g ) )
				++frontCount;
		const UnsignedInt allGates = gates >= 32 ? 0xFFFFFFFFu : ( 1u << gates ) - 1;
		Int first = frontCount > 0 ? nthGate( fronts, (Int)( m_frontTurn % (UnsignedInt)frontCount ) ) : 0;
		if( gates > frontCount && m_wave % 3 == 2 )
			first = nthGate( allGates & ~fronts, GameLogicRandomValue( 0, gates - frontCount - 1 ) );
		else
			++m_frontTurn;
		mask |= 1u << first;
		names.format( " %d", first + 1 );
	}
	for( Int taken = 1; taken < want; ++taken )
	{
		Int pick = GameLogicRandomValue( 0, gates - 1 - taken );
		for( Int g = 0; g < gates; ++g )
			if( !( mask & ( 1u << g ) ) && pick-- == 0 )
			{
				mask |= 1u << g;
				AsciiString one;
				one.format( " %d", g + 1 );
				names.concat( one );
				break;
			}
	}
	m_warnSectors = mask;
	DEBUG_LOG(( "HEADLESS APOCALYPSE: frame %u %s wave %u gathers at gate%s%s, due at frame %u\n", now,
							Apocalypse_phaseName( m_phase ), m_wave + 1, want > 1 ? "s" : "", names.str(), m_nextWaveFrame ));
	LogicThread::callAfterTick( []() { if( ThePartitionManager ) ThePartitionManager->redrawShroudForLocalPlayer(); } );
}

//-------------------------------------------------------------------------------------------------
/** The gathered wave comes: judged against the last one first (the adaptive share), then paid for in
	  threat points that come out as one mass at its gates. */
void ApocalypseDirector::sendWave( UnsignedInt now )
{
	if( m_warnSectors == 0 )
		gatherWave( now );
	if( m_wave > 0 )
	{
		const Bool reached = m_reached > m_reachedAtWave;
		const Bool cleared = m_pending <= 0 && (Int)m_zombies.size() <= m_seats * 2;
		m_adapt = Apocalypse_adapt( m_adapt, reached, cleared );
	}
	++m_wave;
	const Int points = Apocalypse_wavePoints( m_wave, m_seats, m_phase, m_cycle, m_adapt );
	m_pending += points;
	m_waveSectors = m_warnSectors;
	m_warnSectors = 0;
	m_reachedAtWave = m_reached;
	m_nextWaveFrame = now + Apocalypse_phaseRules( m_phase ).waveSeconds * gameSecond();
	DEBUG_LOG(( "HEADLESS APOCALYPSE: frame %u %s wave %u of cycle %u brings %d points at %d%% share, rank %d; %d walking, %d points to come, next at frame %u\n",
							now, Apocalypse_phaseName( m_phase ), m_wave, m_cycle, points, m_adapt, Apocalypse_cycleRank( m_cycle ),
							(Int)m_zombies.size(), m_pending, m_nextWaveFrame ));
	LogicThread::callAfterTick( []() { if( ThePartitionManager ) ThePartitionManager->redrawShroudForLocalPlayer(); } );
}

//-------------------------------------------------------------------------------------------------
/** Soldiers the dead killed rise as the dead: a body of a seat's infantry whose last hurt came from
	  the dead player is noted while it lies, and APOCALYPSE_INFECT_SECONDS later an ApocalypseZombie
	  stands where it fell, if the cap has room (it waits for room otherwise).  Walked in object list
	  order; m_rises kept sorted by the victim's ObjectID. */
void ApocalypseDirector::watchTheFallen( UnsignedInt now )
{
	const Player *zombies = ThePlayerList->getNthPlayer( m_zombiePlayer );
	const Player *neutral = ThePlayerList->getNeutralPlayer();
	const PlayerMaskType zombieMask = zombies->getPlayerMask();
	const Int cap = Apocalypse_phaseRules( m_phase ).aliveCap;

	// those due rise; a risen one is kept while his body lies, so it is not noted twice
	std::vector<Rise> kept;
	for( UnsignedInt i = 0; i < m_rises.size(); ++i )
	{
		Rise r = m_rises[ i ];
		if( r.frame != 0 && now >= r.frame && (Int)m_zombies.size() < cap )
		{
			const Object *made = makeZombie( KIND_WALKER, r.where, now );
			if( made != NULL )
			{
				++m_infected;
				DEBUG_LOG(( "HEADLESS APOCALYPSE: frame %u fallen %d rises as zombie %d at (%.0f,%.0f), %u risen\n", now,
										(Int)r.victim, (Int)made->getID(), r.where.x, r.where.y, m_infected ));
			}
			r.frame = 0;
		}
		if( r.frame == 0 && TheGameLogic->findObjectByID( r.victim ) == NULL )
			continue;		// risen, and the body is gone: nothing left to tell apart
		kept.push_back( r );
	}
	m_rises.swap( kept );

	for( Object *obj = TheGameLogic->getFirstObject(); obj; obj = obj->getNextObject() )
	{
		if( !obj->isEffectivelyDead() || !obj->isKindOf( KINDOF_INFANTRY ) )
			continue;
		const Player *owner = obj->getControllingPlayer();
		if( owner == NULL || owner == zombies || owner == neutral )
			continue;
		const BodyModuleInterface *body = obj->getBodyModule();
		const DamageInfo *last = body != NULL ? body->getLastDamageInfo() : NULL;
		if( last == NULL || ( last->in.m_sourcePlayerMask & zombieMask ) == 0 )
			continue;
		Rise r;
		r.victim = obj->getID();
		std::vector<Rise>::iterator at = std::lower_bound( m_rises.begin(), m_rises.end(), r,
			[]( const Rise &a, const Rise &b ) { return a.victim < b.victim; } );
		if( at != m_rises.end() && at->victim == r.victim )
			continue;
		r.where = *obj->getPosition();
		r.frame = now + APOCALYPSE_INFECT_SECONDS * gameSecond();
		m_rises.insert( at, r );
	}
}

//-------------------------------------------------------------------------------------------------
/** The dead in the fog, counted by heat block and handed to the client after the tick: a read of the
	  zombie list in its order, no random drawn and nothing kept, so the logic is the same with or
	  without anyone looking. */
void ApocalypseDirector::handHeat( void ) const
{
	const Int blocksX = ( ThePartitionManager->getCellCountX() + APOCALYPSE_HEAT_BLOCK - 1 ) / APOCALYPSE_HEAT_BLOCK;
	const Int blocksY = ( ThePartitionManager->getCellCountY() + APOCALYPSE_HEAT_BLOCK - 1 ) / APOCALYPSE_HEAT_BLOCK;
	std::vector<UnsignedByte> heat( blocksX * blocksY, 0 );
	for( UnsignedInt i = 0; i < m_zombies.size(); ++i )
	{
		const Object *obj = TheGameLogic->findObjectByID( m_zombies[ i ].id );
		if( obj == NULL || !inRing( obj->getPosition() ) )
			continue;
		Int cellX, cellY;
		ThePartitionManager->worldToCell( obj->getPosition()->x, obj->getPosition()->y, &cellX, &cellY );
		const Int block = ( cellY / APOCALYPSE_HEAT_BLOCK ) * blocksX + cellX / APOCALYPSE_HEAT_BLOCK;
		if( block >= 0 && block < (Int)heat.size() )
			heat[ block ] = (UnsignedByte)min( 255, (Int)heat[ block ] + APOCALYPSE_HEAT_PER_ZOMBIE );
	}
	LogicThread::callAfterTick( [heat, blocksX, blocksY]() { apocalypseApplyHeat( heat, blocksX, blocksY ); } );
}

//-------------------------------------------------------------------------------------------------
Bool ApocalypseDirector::inRing( const Coord3D *pos ) const
{
	if( !m_hasRing )
		return FALSE;
	Int cellX, cellY;
	ThePartitionManager->worldToCell( pos->x, pos->y, &cellX, &cellY );
	return ThePartitionManager->isCellAlwaysShrouded( cellX, cellY );
}

//-------------------------------------------------------------------------------------------------
/** A spot in the fog ring before that gate (anywhere in the ring for -1), off the map's edge and off the
	  clear area by APOCALYPSE_FOG_EDGE_IN, where the object can stand: round the gate's spawn waypoint,
	  a little in towards the middle, more out, and a quarter of its distance from the middle either side.  Drawn from
	  the logic stream, FALSE after APOCALYPSE_FOG_TRIES misses. */
Bool ApocalypseDirector::ringSpot( Coord3D *pos, const Object *obj, Int sector )
{
	const Real in = (Real)APOCALYPSE_FOG_EDGE_IN;
	const AIUpdateInterface *ai = obj->getAI();
	for( Int tryNo = 0; tryNo < APOCALYPSE_FOG_TRIES; ++tryNo )
	{
		if( sector >= 0 && sector < (Int)m_spawns.size() )
		{
			const Real gx = m_spawns[ sector ].x - m_centre.x, gy = m_spawns[ sector ].y - m_centre.y;
			const Real reach = sqrtf( gx * gx + gy * gy );
			const Real ux = reach > 0.0f ? gx / reach : 1.0f, uy = reach > 0.0f ? gy / reach : 0.0f;
			// the fog's inner edge round the pass's spawn: a little in towards the clear ground, more out, wide either side
			const Real inward = GameLogicRandomValueReal( -0.12f * reach, 0.06f * reach );
			const Real aside = GameLogicRandomValueReal( -0.25f * reach, 0.25f * reach );
			pos->x = m_spawns[ sector ].x - ux * inward - uy * aside;
			pos->y = m_spawns[ sector ].y - uy * inward + ux * aside;
			if( pos->x < m_playLo.x + in || pos->x > m_playHi.x - in || pos->y < m_playLo.y + in || pos->y > m_playHi.y - in )
				continue;
		}
		else
		{
			pos->x = GameLogicRandomValueReal( m_playLo.x + in, m_playHi.x - in );
			pos->y = GameLogicRandomValueReal( m_playLo.y + in, m_playHi.y - in );
		}
		// in the fog, and as far from its edge as APOCALYPSE_FOG_EDGE_IN every way
		Coord3D probe = *pos;
		Bool inFog = inRing( pos );
		for( Int side = 0; side < 4 && inFog; ++side )
		{
			probe.x = pos->x + ( side == 0 ? in : side == 1 ? -in : 0.0f );
			probe.y = pos->y + ( side == 2 ? in : side == 3 ? -in : 0.0f );
			inFog = inRing( &probe );
		}
		if( !inFog )
			continue;
		pos->z = TheTerrainLogic->getGroundHeight( pos->x, pos->y );
		if( ai == NULL || TheAI->pathfinder()->validMovementPosition( obj->getCrusherLevel() > 0, obj->getLayer(), ai->getLocomotorSet(), pos ) )
			return TRUE;
	}
	return FALSE;
}

//-------------------------------------------------------------------------------------------------
/** Anything of the living (a unit of a seat or of anyone but the dead and the neutral map) that has
	  stood in the fog ring APOCALYPSE_FOG_GRACE_SECONDS dies there, save a power's plane from off the
	  map (isOffMapDelivery); what rides in one, a parachute and a crate are no unit of the ring's either.  Walked in object list order, the
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
				|| !inRing( obj->getPosition() ) || isOffMapDelivery( obj ) )
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
	// the night does not wait out the day's wave clock: its first wave is told now and comes a telegraph on
	if( m_phase == APOCALYPSE_NIGHT )
	{
		const UnsignedInt falls = now + APOCALYPSE_TELEGRAPH_SECONDS * gameSecond();
		if( m_nextWaveFrame > falls )
		{
			m_nextWaveFrame = falls;
			m_warnSectors = 0;		// what the day had gathered gives way to the night's own
		}
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

	// the next wave gathers at its gates a telegraph ahead, then comes as one mass
	if( m_kinds[ KIND_WALKER ] != NULL )
	{
		if( m_warnSectors == 0 && now + APOCALYPSE_TELEGRAPH_SECONDS * second >= m_nextWaveFrame )
			gatherWave( now );
		if( now >= m_nextWaveFrame )
			sendWave( now );
	}
	for( Int out = 0; out < rules.spawnsPerFrame && m_pending > 0 && (Int)m_zombies.size() < rules.aliveCap; ++out )
		spawnOne( now );

	// the fallen rise, five times a game second
	if( now % max( second / 5, 1U ) == 0 )
		watchTheFallen( now );

	if( m_hasRing && now % ( APOCALYPSE_HEAT_SECONDS * second ) == 0 )
		handHeat();

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
		DEBUG_LOG(( "HEADLESS APOCALYPSE: frame %u %s cycle %u wave %u walking %d spawned %u kills %u reached %u to come %d seats standing %d of %d, fog took %u, risen %u, share %d%%\n",
								now, Apocalypse_phaseName( m_phase ), m_cycle, m_wave, (Int)m_zombies.size(), m_spawned, m_kills, m_reached,
								m_pending, standing, m_seats, m_fogKills, m_infected, m_adapt ));
		Int inFog = 0, idle = 0;
		// the ones out of the fog that have not reached the bases, by what they do: where a wave stalls
		Int outIdle = 0, outOnStructure = 0, outOnUnit = 0, outWalking = 0;
		const Object *front = NULL;
		Real front2 = 1.0e30f;
		for( UnsignedInt i = 0; i < m_zombies.size(); ++i )
		{
			Object *obj = TheGameLogic->findObjectByID( m_zombies[ i ].id );
			if( obj == NULL )
				continue;
			const Bool fogged = inRing( obj->getPosition() );
			if( fogged )
				++inFog;
			AIUpdateInterface *ai = obj->getAI();
			if( ai != NULL && ai->isIdle() )
				++idle;
			if( fogged || m_zombies[ i ].reached )
				continue;
			const Object *victim = ai != NULL ? ai->getCurrentVictim() : NULL;
			if( ai == NULL || ai->isIdle() )
				++outIdle;
			else if( victim != NULL && victim->isKindOf( KINDOF_STRUCTURE ) )
				++outOnStructure;
			else if( victim != NULL )
				++outOnUnit;
			else
				++outWalking;
			const Real dx = obj->getPosition()->x - m_centre.x, dy = obj->getPosition()->y - m_centre.y;
			if( dx * dx + dy * dy < front2 )
			{
				front2 = dx * dx + dy * dy;
				front = obj;
			}
		}
		if( front != NULL )
		{
			const Object *victim = front->getAI() != NULL ? front->getAI()->getCurrentVictim() : NULL;
			DEBUG_LOG(( "HEADLESS APOCALYPSE: frame %u on the way in: %d idle, %d on a structure, %d on a unit, %d walking; the nearest at (%.0f,%.0f), %.0f from the middle against a reach of %.0f, on '%s'\n",
									now, outIdle, outOnStructure, outOnUnit, outWalking, front->getPosition()->x, front->getPosition()->y,
									sqrtf( front2 ), m_reachRadius, victim != NULL ? victim->getTemplate()->getName().str() : "nothing" ));
		}
		Int buildings = 0, units = 0, nearMiddle = 0;
		for( Object *obj = TheGameLogic->getFirstObject(); obj; obj = obj->getNextObject() )
		{
			if( obj->isEffectivelyDead() || !isSeat( obj->getControllingPlayer() ) )
				continue;
			if( obj->isKindOf( KINDOF_STRUCTURE ) )
				++buildings;
			else
				++units;
		}
		for( UnsignedInt i = 0; i < m_zombies.size(); ++i )
			if( m_zombies[ i ].reached )
				++nearMiddle;
		DEBUG_LOG(( "HEADLESS APOCALYPSE: frame %u the dead: %d still in the fog, %d idle, %d among the bases; the seats hold %d buildings, %d units\n",
								now, inFog, idle, nearMiddle, buildings, units ));
		for( Int i = 0; i < ThePlayerList->getPlayerCount(); ++i )
		{
			Player *player = ThePlayerList->getNthPlayer( i );
			if( isSeat( player ) )
				DEBUG_LOG(( "HEADLESS APOCALYPSE: frame %u seat %d money %u rank %d skill %d\n", now, i,
										player->getMoney()->countMoney(), player->getRankLevel(), player->getSkillPoints() ));
		}
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
	XferVersion currentVersion = 4;
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

	if( version >= 3 )
	{
		xfer->xferUnsignedInt( &m_waveSectors );
		xfer->xferUnsignedInt( &m_warnSectors );
		xfer->xferInt( &m_adapt );
		xfer->xferUnsignedInt( &m_reachedAtWave );
		xfer->xferUnsignedInt( &m_infected );
		count = (UnsignedInt)m_rises.size();
		xfer->xferUnsignedInt( &count );
		if( xfer->getXferMode() == XFER_LOAD )
			m_rises.resize( count );
		for( UnsignedInt i = 0; i < count; ++i )
		{
			xfer->xferObjectID( &m_rises[ i ].victim );
			xfer->xferCoord3D( &m_rises[ i ].where );
			xfer->xferUnsignedInt( &m_rises[ i ].frame );
		}
	}

	if( version >= 4 )
		xfer->xferUnsignedInt( &m_frontTurn );
}
