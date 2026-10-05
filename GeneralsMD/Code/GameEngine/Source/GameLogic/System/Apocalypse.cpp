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

// FILE: Apocalypse.cpp //////////////////////////////////////////////////////////////////////////
//
// Apocalypse mode's zombies.  Everything here runs inside the logic frame on every machine, so it
// reads the frame number and the logic random stream and nothing else that could differ.
//
///////////////////////////////////////////////////////////////////////////////////////////////////

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "Common/NameKeyGenerator.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/ThingFactory.h"
#include "Common/ThingTemplate.h"
#include "Common/Xfer.h"
#include "GameClient/InGameUI.h"
#include "GameLogic/AI.h"
#include "GameLogic/Apocalypse.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Object.h"
#include "GameLogic/PartitionManager.h"
#include "GameLogic/ScenarioDrill.h"
#include "GameLogic/TerrainLogic.h"
#include "GameNetwork/GameInfo.h"

/// the players get this long to put a base up before anything arrives
static const UnsignedInt APOCALYPSE_FIRST_SPAWN_FRAME = 120 * LOGICFRAMES_PER_SECOND;

/// wave mode: the first wave's zombies a player, the growth each wave after it, and the pause between two
static const Int APOCALYPSE_WAVE_BASE = 5;
static const Int APOCALYPSE_WAVE_STEP = 3;
static const UnsignedInt APOCALYPSE_WAVE_PAUSE = 60 * LOGICFRAMES_PER_SECOND;

/// steady mode: one zombie every START frames for a lone player at level 1, STEP frames sooner each
/// level, never sooner than FLOOR; a level lasts LEVEL frames and is announced like a wave
static const Int APOCALYPSE_STREAM_START = 10 * LOGICFRAMES_PER_SECOND;
static const Int APOCALYPSE_STREAM_STEP = 24;
static const Int APOCALYPSE_STREAM_FLOOR = LOGICFRAMES_PER_SECOND / 2;
static const UnsignedInt APOCALYPSE_STREAM_LEVEL = 60 * LOGICFRAMES_PER_SECOND;

/// how far from the spawn point a zombie may stand when it arrives
static const Real APOCALYPSE_SPREAD_RADIUS = 150.0f;

/// a full map's spawn: from the far corner, this many probes a twentieth of the way to the players
/// each, every one looking this far round itself for a clear cell
static const Int APOCALYPSE_EDGE_STEPS = 10;
static const Real APOCALYPSE_EDGE_STEP_FRACTION = 0.05f;
static const Real APOCALYPSE_EDGE_SEARCH_RADIUS = 100.0f;

/// zombies alive at once: the floor holds up to four players, and each seat past the fourth adds a
/// step, so eight players face 300 rather than a wave of 208 already pressed against 200
// ponytail: a guess at what the engine carries next to eight bases, not a measured frame budget;
// lower the step if an eight-player match crawls once the cap is full.
static const Int APOCALYPSE_MAX_LIVE_FLOOR = 200;
static const Int APOCALYPSE_MAX_LIVE_STEP = 25;

static Int theMode = APOCALYPSE_OFF;
static Int theWave = 0;										///< the wave, or the steady stream's level, last announced
static Int thePlayers = 1;									///< the non-observer seats, which size the waves
static Bool theSpawnChosen = FALSE;					///< theSpawnPoint holds the answer; until then the edge search is still to run
static Coord3D theSpawnPoint;
static Coord3D theCentroid;									///< the middle of the seats' start positions
static UnsignedInt theNextSpawnFrame = 0;

//-------------------------------------------------------------------------------------------------
Int Apocalypse_waveSize( Int wave, Int players )
{
	return (APOCALYPSE_WAVE_BASE + APOCALYPSE_WAVE_STEP * (wave - 1)) * players;
}

//-------------------------------------------------------------------------------------------------
UnsignedInt Apocalypse_streamInterval( Int level, Int players )
{
	const Int interval = (APOCALYPSE_STREAM_START - APOCALYPSE_STREAM_STEP * (level - 1)) / players;
	return (UnsignedInt)(interval > APOCALYPSE_STREAM_FLOOR ? interval : APOCALYPSE_STREAM_FLOOR);
}

//-------------------------------------------------------------------------------------------------
Int Apocalypse_maxLive( Int players )
{
	const Int cap = APOCALYPSE_MAX_LIVE_FLOOR + APOCALYPSE_MAX_LIVE_STEP * (players - 4);
	return cap > APOCALYPSE_MAX_LIVE_FLOOR ? cap : APOCALYPSE_MAX_LIVE_FLOOR;
}

//-------------------------------------------------------------------------------------------------
void Apocalypse_newGame( Int mode )
{
	theMode = mode;
	theWave = 0;
	thePlayers = 1;
	theSpawnChosen = FALSE;
	theSpawnPoint.zero();
	theCentroid.zero();
	theNextSpawnFrame = APOCALYPSE_FIRST_SPAWN_FRAME;
}

//-------------------------------------------------------------------------------------------------
Int Apocalypse_mode( void )
{
	return theMode;
}

//-------------------------------------------------------------------------------------------------
Bool Apocalypse_isZombiePlayer( const Player *player )
{
	return theMode != APOCALYPSE_OFF && player->getPlayerNameKey() == NAMEKEY( APOCALYPSE_PLAYER_NAME );
}

//-------------------------------------------------------------------------------------------------
static Waypoint *startWaypoint( Int startPos )
{
	AsciiString name;
	name.format( "Player_%d_Start", startPos + 1 );	// start waypoints are 1-based
	return TheTerrainLogic->getWaypointByName( name );
}

/** The zombies come from the free start position farthest from the players, which on a map built
	  for more players than the lobby holds is somebody's empty base: open ground the map maker
	  already made reachable.  A full map has none, and then they come from the corner of the map
	  farthest from the players, resolved on the first tick (edgeSpawnPoint). */
Int Apocalypse_chooseSpawnPoint( const GameInfo *game )
{
	Bool taken[ MAX_SLOTS ];
	for( Int k = 0; k < MAX_SLOTS; ++k )
		taken[ k ] = FALSE;

	theCentroid.zero();
	thePlayers = 0;
	for( Int i = 0; i < MAX_SLOTS; ++i )
	{
		const GameSlot *slot = game->getConstSlot( i );
		if( !slot->isOccupied() || slot->getPlayerTemplate() == PLAYERTEMPLATE_OBSERVER )
			continue;

		taken[ slot->getStartPos() ] = TRUE;
		theCentroid.add( startWaypoint( slot->getStartPos() )->getLocation() );
		++thePlayers;
	}
	theCentroid.scale( 1.0f / thePlayers );

	Int start = -1;
	Real farthest = -1.0f;
	for( Int k = 0; k < MAX_SLOTS; ++k )
	{
		Waypoint *waypoint = taken[ k ] ? NULL : startWaypoint( k );
		if( waypoint == NULL )
			continue;

		const Real dx = waypoint->getLocation()->x - theCentroid.x;
		const Real dy = waypoint->getLocation()->y - theCentroid.y;
		if( dx * dx + dy * dy > farthest )
		{
			farthest = dx * dx + dy * dy;
			theSpawnPoint = *waypoint->getLocation();
			start = k;
		}
	}
	theSpawnChosen = start >= 0;
	DEBUG_LOG(( "APOCALYPSE: mode %d, %d players, zombies start at position %d (%.0f,%.0f)\n",
							theMode, thePlayers, start, theSpawnPoint.x, theSpawnPoint.y ));
	return start;
}

/** The corner of the playable area farthest from the players is often a cliff or the impassable rim
	  the map maker painted round the edge, and a zombie standing there never reaches anybody.  So the
	  search walks from the corner towards the players and takes the first clear cell it meets. */
// ponytail: a clear cell is not proof of a road to the players (a walled-off plateau passes);
// a path query with the zombie's locomotor would be, if a map turns up that needs it.
static Coord3D edgeSpawnPoint( void )
{
	const Coord3D corner = TheTerrainLogic->findFarthestEdgePoint( &theCentroid );

	FindPositionOptions options;
	options.flags = FPF_CLEAR_CELLS_ONLY;
	options.maxRadius = APOCALYPSE_EDGE_SEARCH_RADIUS;
	Coord3D probe = corner;
	for( Int step = 1; step <= APOCALYPSE_EDGE_STEPS; ++step )
	{
		const Real t = step * APOCALYPSE_EDGE_STEP_FRACTION;
		probe.x = corner.x + (theCentroid.x - corner.x) * t;
		probe.y = corner.y + (theCentroid.y - corner.y) * t;
		probe.z = TheTerrainLogic->getGroundHeight( probe.x, probe.y );

		Coord3D found;
		if( ThePartitionManager->findPositionAround( &probe, &options, &found ) )
			return found;
	}
	return probe;
}

//-------------------------------------------------------------------------------------------------
static void spawnZombies( Int count )
{
	Player *zombies = ThePlayerList->findPlayerWithNameKey( NAMEKEY( APOCALYPSE_PLAYER_NAME ) );
	const ThingTemplate *tmpl = TheThingFactory->findTemplate( APOCALYPSE_ZOMBIE_TEMPLATE );

	Int live = 0;
	zombies->countObjectsByThingTemplate( 1, &tmpl, TRUE, &live );
	const Int maxLive = Apocalypse_maxLive( thePlayers );
	if( count > maxLive - live )
		count = maxLive - live;

	FindPositionOptions options;
	options.maxRadius = APOCALYPSE_SPREAD_RADIUS;
	for( Int i = 0; i < count; ++i )
	{
		Coord3D pos = theSpawnPoint;
		ThePartitionManager->findPositionAround( &theSpawnPoint, &options, &pos );
		pos.z = TheTerrainLogic->getGroundHeight( pos.x, pos.y );

		Object *zombie = ScenarioDrill_spawnOne( tmpl, zombies->getDefaultTeam(), &pos );
		zombie->getAI()->aiHunt( CMD_FROM_AI );
	}
}

//-------------------------------------------------------------------------------------------------
void Apocalypse_tick( void )
{
	if( theMode == APOCALYPSE_OFF )
		return;

	// the edge search needs the pathfinder's cells, which are not there yet when startNewGame chooses
	if( !theSpawnChosen )
	{
		theSpawnPoint = edgeSpawnPoint();
		theSpawnChosen = TRUE;
		DEBUG_LOG(( "APOCALYPSE: no free start position, zombies come from (%.0f,%.0f)\n",
								theSpawnPoint.x, theSpawnPoint.y ));
	}

	const UnsignedInt now = TheGameLogic->getFrame();
	if( now < theNextSpawnFrame )
		return;

	if( theMode == APOCALYPSE_WAVES )
	{
		++theWave;
		TheInGameUI->message( "GUI:ApocalypseWave", theWave );
		spawnZombies( Apocalypse_waveSize( theWave, thePlayers ) );
		theNextSpawnFrame = now + APOCALYPSE_WAVE_PAUSE;
		return;
	}

	const Int level = (Int)((now - APOCALYPSE_FIRST_SPAWN_FRAME) / APOCALYPSE_STREAM_LEVEL) + 1;
	if( level > theWave )
	{
		theWave = level;
		TheInGameUI->message( "GUI:ApocalypseWave", theWave );
	}
	spawnZombies( 1 );
	theNextSpawnFrame = now + Apocalypse_streamInterval( theWave, thePlayers );
}

//-------------------------------------------------------------------------------------------------
void Apocalypse_announceEnd( UnsignedInt endFrame )
{
	if( theMode == APOCALYPSE_OFF )
		return;

	const Int seconds = (Int)(endFrame / LOGICFRAMES_PER_SECOND);
	TheInGameUI->message( "GUI:ApocalypseSurvived", theWave, seconds / 60, seconds % 60 );
	DEBUG_LOG(( "APOCALYPSE: over on frame %d after %d waves\n", endFrame, theWave ));
}

//-------------------------------------------------------------------------------------------------
void Apocalypse_xfer( Xfer *xfer )
{
	xfer->xferInt( &theMode );
	xfer->xferInt( &theWave );
	xfer->xferInt( &thePlayers );
	xfer->xferBool( &theSpawnChosen );
	xfer->xferCoord3D( &theSpawnPoint );
	xfer->xferCoord3D( &theCentroid );
	xfer->xferUnsignedInt( &theNextSpawnFrame );
}
