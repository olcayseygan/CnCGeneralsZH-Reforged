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
// reads the frame number, the logic random stream and logic state (the objects, the per-player
// shroud the partition manager keeps and CRCs every frame) and nothing else that could differ.
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
#include "GameLogic/LogicRandomValue.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Object.h"
#include "GameLogic/PartitionManager.h"
#include "GameLogic/ScenarioDrill.h"
#include "GameLogic/TerrainLogic.h"
#include "GameLogic/VictoryConditions.h"
#include "GameNetwork/GameInfo.h"
#include "Lib/Trig.h"

#include <algorithm>

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

/// how far from the spawn spot a zombie may stand when it arrives
static const Real APOCALYPSE_SPREAD_RADIUS = 150.0f;

/// where a group rises: CANDIDATES spots round one of the visited seat's objects, the first RING_MIN
/// away and each next one a step further out to RING_MAX, every one at a random bearing.  The
/// nearest that no seat can see and that stands RING_MIN clear of every seat's objects wins (an
/// object with no sight range, or one a hill blinds, can have fog right beside it); when none
/// qualifies, the one farthest from all the seats' objects does.  Each spot is moved to a clear cell
/// within CLEAR_SEARCH of it first.  ANCHORS objects get their ring walked before that fallback.
static const Real APOCALYPSE_RING_MIN = 300.0f;
static const Real APOCALYPSE_RING_MAX = 1200.0f;
static const Int APOCALYPSE_RING_CANDIDATES = 32;
static const Int APOCALYPSE_RING_ANCHORS = 4;
static const Real APOCALYPSE_CLEAR_SEARCH = 60.0f;

/// zombies alive at once: the floor holds up to four players, and each seat past the fourth adds a
/// step, so eight players face 300 rather than a wave of 208 already pressed against 200
// ponytail: a guess at what the engine carries next to eight bases, not a measured frame budget;
// lower the step if an eight-player match crawls once the cap is full.
static const Int APOCALYPSE_MAX_LIVE_FLOOR = 200;
static const Int APOCALYPSE_MAX_LIVE_STEP = 25;

static Int theMode = APOCALYPSE_OFF;
static Int theWave = 0;										///< the wave, or the steady stream's level, last announced
static Int thePlayers = 1;									///< the non-observer seats, which size the waves
static Int theTurn = 0;										///< zombies raised so far, which says whose turn it is next
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
void Apocalypse_newGame( const GameInfo *game )
{
	theMode = game ? game->getApocalypseMode() : APOCALYPSE_OFF;
	theWave = 0;
	thePlayers = game ? game->getNumNonObserverPlayers() : 1;
	theTurn = 0;
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

/// a seat still in the match - human or computer - and what it has on the field
struct ApocalypseSeat
{
	Player *player;
	std::vector<Object *> objects;
};

//-------------------------------------------------------------------------------------------------
static void collectSeatObject( Object *obj, void *userData )
{
	// a shell in flight or a radar scan's marker is not where anybody stands
	if( !obj->isKindOf( KINDOF_PROJECTILE ) && !obj->isKindOf( KINDOF_INERT ) && !obj->isEffectivelyDead() )
		((std::vector<Object *> *)userData)->push_back( obj );
}

//-------------------------------------------------------------------------------------------------
static bool objectIDLess( const Object *a, const Object *b )
{
	return a->getID() < b->getID();
}

/** Every seat the zombies hunt that is still in it: computer seats too, which a headless run is made
	  of.  An observer, and a seat already defeated, sees the whole map revealed, so neither may take
	  part in the test of who can see a spot. */
static void findSeats( Player *zombies, std::vector<ApocalypseSeat> &seats )
{
	for( Int i = 0; i < ThePlayerList->getPlayerCount(); ++i )
	{
		Player *player = ThePlayerList->getNthPlayer( i );
		if( zombies->getRelationship( player->getDefaultTeam() ) != ENEMIES || !player->isPlayerActive() ||
				TheVictoryConditions->hasSinglePlayerBeenDefeated( player ) )
			continue;

		seats.push_back( ApocalypseSeat() );
		seats.back().player = player;
		player->iterateObjects( collectSeatObject, &seats.back().objects );
		if( seats.back().objects.empty() )
			seats.pop_back();
		else	// a loaded save rebuilds the team lists in load order, so the random anchor draw goes by ID
			std::sort( seats.back().objects.begin(), seats.back().objects.end(), objectIDLess );
	}
}

/** Somebody can see this point right now.  Each seat's shroud already carries its allies' eyes
	  (Object::look reveals for every ally), so asking every seat covers shared vision. */
static Bool seenBySeat( const std::vector<ApocalypseSeat> &seats, Real x, Real y )
{
	Coord3D pos;
	pos.set( x, y, 0.0f );
	for( size_t i = 0; i < seats.size(); ++i )
	{
		if( ThePartitionManager->getShroudStatusForPlayer( seats[ i ].player->getPlayerIndex(), &pos ) == CELLSHROUD_CLEAR )
			return TRUE;
	}
	return FALSE;
}

//-------------------------------------------------------------------------------------------------
static Real nearestSeatDistSqr( const std::vector<ApocalypseSeat> &seats, const Coord3D *pos )
{
	Real nearest = FLT_MAX;
	for( size_t i = 0; i < seats.size(); ++i )
	{
		for( size_t k = 0; k < seats[ i ].objects.size(); ++k )
		{
			const Coord3D *at = seats[ i ].objects[ k ]->getPosition();
			const Real d = (at->x - pos->x) * (at->x - pos->x) + (at->y - pos->y) * (at->y - pos->y);
			if( d < nearest )
				nearest = d;
		}
	}
	return nearest;
}

/** Where a group rises near this seat: round one of its objects picked at random, so a base and an
	  army in the field both get visited, on a ring walked outwards until a clear cell nobody sees.
	  An object in the middle of the seats' combined sight gets no such cell, so up to ANCHORS of
	  them are tried before the farthest spot of all their rings is taken. */
// ponytail: a clear cell is not proof of a road to the seat (a walled-off plateau passes), and when
// not one candidate finds a clear cell the group rises on the first anchor itself; a path query with
// the zombie's locomotor would settle both, if a map turns up that needs it.
static Coord3D spawnSpotNear( const std::vector<ApocalypseSeat> &seats, const ApocalypseSeat &seat )
{
	Region3D extent;
	TheTerrainLogic->getMaximumPathfindExtent( &extent );

	FindPositionOptions options;
	options.flags = FPF_CLEAR_CELLS_ONLY;
	options.maxRadius = APOCALYPSE_CLEAR_SEARCH;

	Coord3D best;
	Real bestDistSqr = -1.0f;
	for( Int a = 0; a < APOCALYPSE_RING_ANCHORS; ++a )
	{
		const Coord3D anchor = *seat.objects[ GameLogicRandomValue( 0, (Int)seat.objects.size() - 1 ) ]->getPosition();
		if( a == 0 )
			best = anchor;
		for( Int i = 0; i < APOCALYPSE_RING_CANDIDATES; ++i )
		{
			const Real radius = APOCALYPSE_RING_MIN + (APOCALYPSE_RING_MAX - APOCALYPSE_RING_MIN) * i / (APOCALYPSE_RING_CANDIDATES - 1);
			const Real angle = GameLogicRandomValueReal( 0.0f, TWO_PI );
			Coord3D probe;
			probe.x = anchor.x + radius * Cos( angle );
			probe.y = anchor.y + radius * Sin( angle );
			if( probe.x < extent.lo.x || probe.x > extent.hi.x || probe.y < extent.lo.y || probe.y > extent.hi.y )
				continue;
			probe.z = TheTerrainLogic->getGroundHeight( probe.x, probe.y );

			Coord3D found;
			if( !ThePartitionManager->findPositionAround( &probe, &options, &found ) )
				continue;
			const Real distSqr = nearestSeatDistSqr( seats, &found );
			if( distSqr >= APOCALYPSE_RING_MIN * APOCALYPSE_RING_MIN && !seenBySeat( seats, found.x, found.y ) )
				return found;
			if( distSqr > bestDistSqr )
			{
				bestDistSqr = distSqr;
				best = found;
			}
		}
	}
	return best;
}

/** Raise count zombies, split evenly over the seats still in the match and the remainder to whoever
	  is next in turn, each seat's share as one group in the fog near it. */
static void spawnZombies( Int count )
{
	Player *zombies = ThePlayerList->findPlayerWithNameKey( NAMEKEY( APOCALYPSE_PLAYER_NAME ) );
	const ThingTemplate *tmpl = TheThingFactory->findTemplate( APOCALYPSE_ZOMBIE_TEMPLATE );

	Int live = 0;
	zombies->countObjectsByThingTemplate( 1, &tmpl, TRUE, &live );
	const Int maxLive = Apocalypse_maxLive( thePlayers );
	if( count > maxLive - live )
		count = maxLive - live;

	// the frames between the last seat falling and the victory check calling the match
	std::vector<ApocalypseSeat> seats;
	findSeats( zombies, seats );
	if( seats.empty() )
		return;

	const Int n = (Int)seats.size();
	FindPositionOptions options;
	options.maxRadius = APOCALYPSE_SPREAD_RADIUS;
	for( Int k = 0; k < n; ++k )
	{
		const Int share = count / n + ( k < count % n ? 1 : 0 );
		if( share == 0 )
			continue;

		const ApocalypseSeat &seat = seats[ (theTurn + k) % n ];
		const Coord3D spot = spawnSpotNear( seats, seat );
		const Bool spotHidden = !seenBySeat( seats, spot.x, spot.y );
		for( Int i = 0; i < share; ++i )
		{
			// the search takes the first free point from minRadius out, and a zombie does not block
			// the next one, so without a random start the whole group would stand on the spot itself
			options.minRadius = GameLogicRandomValueReal( 0.0f, APOCALYPSE_SPREAD_RADIUS );
			Coord3D pos = spot;
			ThePartitionManager->findPositionAround( &spot, &options, &pos );
			// a hidden spot can have somebody's sight on part of its circle; nobody watches one rise
			if( spotHidden && seenBySeat( seats, pos.x, pos.y ) )
				pos = spot;
			pos.z = TheTerrainLogic->getGroundHeight( pos.x, pos.y );

			Object *zombie = ScenarioDrill_spawnOne( tmpl, zombies->getDefaultTeam(), &pos );
			zombie->getAI()->aiHunt( CMD_FROM_AI );
		}
	}
	theTurn += count;
}

//-------------------------------------------------------------------------------------------------
void Apocalypse_tick( void )
{
	if( theMode == APOCALYPSE_OFF )
		return;

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
void Apocalypse_xfer( Xfer *xfer, UnsignedByte gameLogicVersion )
{
	xfer->xferInt( &theMode );
	xfer->xferInt( &theWave );
	xfer->xferInt( &thePlayers );

	// version 18 came from one fixed spot: whether it was found, the spot and the seats' middle
	if( gameLogicVersion < 19 )
	{
		Bool spawnChosen;
		Coord3D spawnPoint;
		Coord3D centroid;
		xfer->xferBool( &spawnChosen );
		xfer->xferCoord3D( &spawnPoint );
		xfer->xferCoord3D( &centroid );
	}

	xfer->xferUnsignedInt( &theNextSpawnFrame );

	if( gameLogicVersion >= 19 )
		xfer->xferInt( &theTurn );
}
