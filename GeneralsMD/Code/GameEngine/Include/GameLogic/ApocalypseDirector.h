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

// FILE: ApocalypseDirector.h /////////////////////////////////////////////////////////////////////
//
// Apocalypse: the logic system that runs the survival mode on a map the random map generator's
// apocalypse style wrote ("-randommap <seed> <players> [size] apocalypse").  It switches itself on
// when the map carries Apocalypse_Centre and at least one Apocalypse_Spawn_N waypoint; every other
// game never sees it, and its state goes into the CRC only while it is on.
//
// Every seat starts in the walled clearing in the middle, allied with every other seat.  The
// civilian player owns the dead: on a new game the director makes it every seat's enemy, and it
// builds nothing.  Waves come from the spawns at the map's edges without end, trickled out a few a
// game second under a cap on how many may walk at once.  Every zombie attack-moves to the middle;
// one that has gone idle is sent at the nearest thing of a seat's.  The seats lose when everything
// of theirs is dead (VICTORY_APOCALYPSE); the dead never lose.
//
// The match runs a day and night cycle in game seconds: day, dusk, night, dawn, and day again.  By
// day the waves are light and only ApocalypseZombie walks; at dusk they thicken; at night they come
// fast and big, and ApocalypseZombieRunner and ApocalypseZombieBrute walk with them.  Every cycle
// sends more and ranks them one level higher, to Heroic.  The light follows: the director hands the
// client a night amount after each tick (LogicThread::callAfterTick), the client tints the map's own
// light toward a dim red and puts every drawable in its NIGHT state once it is past half; nothing in
// the logic reads any of it back.
//
// A fog ring runs round the map, between the map's Apocalypse_Clear square and its edge: shrouded
// for every player whoever looks (PartitionManager::setAlwaysShroudedOutside), drawn as a thin red
// fog, and deadly: a unit of anyone but the dead that stands in it APOCALYPSE_FOG_GRACE_SECONDS dies.
// The dead rise anywhere in it and walk in.  The fog's red and the smoke's follow the night.
//
// The HEADLESS APOCALYPSE lines carry the phase, the wave, the counts and the kills.
//
///////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#ifndef __APOCALYPSEDIRECTOR_H_
#define __APOCALYPSEDIRECTOR_H_

#include "Common/GameCommon.h"
#include "Common/GameType.h"
#include "Common/Snapshot.h"
#include <vector>

class Object;
class ThingTemplate;

enum ApocalypsePhase
{
	APOCALYPSE_DAY = 0,
	APOCALYPSE_DUSK,
	APOCALYPSE_NIGHT,
	APOCALYPSE_DAWN,
	APOCALYPSE_PHASES
};

enum
{
	APOCALYPSE_DAY_SECONDS = 240,					///< game seconds of each phase
	APOCALYPSE_DUSK_SECONDS = 45,
	APOCALYPSE_NIGHT_SECONDS = 120,
	APOCALYPSE_DAWN_SECONDS = 30,
	APOCALYPSE_FIRST_WAVE_SECONDS = 60,		///< game seconds from the start to the first wave, time to build
	APOCALYPSE_NIGHT_FALLS_SECONDS = 3,		///< game seconds from nightfall to its first wave
	APOCALYPSE_WAVE_BASE = 4,							///< a wave's zombies before its phase and cycle: this,
	APOCALYPSE_WAVE_PER_SEAT = 2,					///< and this many a seat,
	APOCALYPSE_WAVE_GROWTH = 3,						///< and this many more every wave
	APOCALYPSE_WAVE_MOST = 160,						///< a wave's zombies at most
	APOCALYPSE_CYCLE_GROWTH_PERCENT = 25,	///< more in every wave for each cycle gone by
	APOCALYPSE_RETARGET_SECONDS = 2,			///< game seconds between two looks of one zombie for something to go at
	APOCALYPSE_LOG_SECONDS = 10,					///< game seconds between two status lines
	APOCALYPSE_SPAWN_SPREAD = 60,					///< world units a zombie lands either way off its spawn
	APOCALYPSE_BASE_REACH = 200,					///< world units past the farthest start from the middle that count as having reached the bases
	APOCALYPSE_FOG_GRACE_SECONDS = 4,			///< game seconds anything but the dead may stand in the fog ring before it dies there
	APOCALYPSE_FOG_EDGE_IN = 40,					///< world units a ring spawn keeps off the map's edge and off the clear square
	APOCALYPSE_FOG_TRIES = 8,							///< spots drawn for a ring spawn before it falls back on a spawn waypoint
	APOCALYPSE_LIGHT_STEPS = 32,					///< the night amount the client is handed, in steps
	APOCALYPSE_LIGHT_TINT_EVERY = 4,			///< steps between two retints of the light tables, each a full relight of the ground
};

/// how one phase plays: its length, how often a wave comes and how big, how fast it comes out, the cap
struct ApocalypsePhaseRules
{
	Int seconds;
	Int waveSeconds;
	Int sizePercent;
	Int spawnsPerSecond;
	Int aliveCap;
};

extern const ApocalypsePhaseRules &Apocalypse_phaseRules( Int phase );
extern const char *Apocalypse_phaseName( Int phase );

/// what a wave of that number (from 1) sends for that many seats, in that phase of that cycle (from 0)
extern Int Apocalypse_waveSize( UnsignedInt wave, Int seats, Int phase, UnsignedInt cycle );

/// the rank a zombie of that cycle walks in, 0 Regular to 3 Heroic
inline Int Apocalypse_cycleRank( UnsignedInt cycle ) { return cycle < 3 ? (Int)cycle : 3; }

/// how far into the night that point of a phase is: 0 by day, rising through dusk, 1 at night, falling at dawn
extern Real Apocalypse_nightAmount( Int phase, UnsignedInt intoPhase, UnsignedInt phaseFrames );

//-------------------------------------------------------------------------------------------------
class ApocalypseDirector : public Snapshot
{
public:
	ApocalypseDirector( void );

	void reset( void );

	/** Read the map once its objects are in.  A new game also gets its alliances: every seat allied
		  with every other and the civilians every seat's enemy; a loaded one takes those from the save. */
	void newMap( Bool loadingSaveGame );

	Bool isActive( void ) const { return m_active; }

	/// once a logic frame, from GameLogic::update
	void update( void );

	UnsignedInt getWave( void ) const { return m_wave; }
	UnsignedInt getKills( void ) const { return m_kills; }
	Int getAlive( void ) const { return (Int)m_zombies.size(); }
	Int getPhase( void ) const { return m_phase; }
	UnsignedInt getCycle( void ) const { return m_cycle; }

protected:
	virtual void crc( Xfer *xfer );
	virtual void xfer( Xfer *xfer );
	virtual void loadPostProcess( void ) { }

private:
	struct Zombie
	{
		ObjectID id;
		Bool reached;										///< it has been within m_reachRadius of the middle, among the bases
	};
	enum { KIND_WALKER, KIND_RUNNER, KIND_BRUTE, KINDS };

	struct FogStay
	{
		ObjectID id;
		UnsignedInt since;							///< the frame it was first found in the ring this time
	};

	void spawnOne( UnsignedInt now );
	Bool ringSpot( Coord3D *pos, const Object *obj );
	Bool inRing( const Coord3D *pos ) const;
	void killInFog( UnsignedInt now );
	void sendOn( Object *obj );
	Int seatsAlive( void ) const;
	void advancePhase( UnsignedInt now );
	void handLight( UnsignedInt now );

	// from the map and the data on every load
	Bool m_active;
	Coord3D m_centre;
	Real m_reachRadius;									///< the farthest start from the middle and APOCALYPSE_BASE_REACH
	Bool m_hasRing;											///< the map has Apocalypse_Clear: a fog ring between it and the edge
	Coord2D m_clearLo, m_clearHi;				///< the square the ring leaves, world units
	Coord2D m_playLo, m_playHi;					///< the playable map
	std::vector<Coord3D> m_spawns;
	Int m_zombiePlayer;									///< the civilian player's index
	Int m_seats;												///< seats at the start
	const ThingTemplate *m_kinds[ KINDS ];	///< NULL where the data lacks one; a missing walker spawns nothing, a missing night kind walks as a walker

	// logic state, saved and in the CRC
	Int m_phase;												///< ApocalypsePhase
	UnsignedInt m_phaseStartFrame;
	UnsignedInt m_cycle;								///< nights gone by
	UnsignedInt m_wave;									///< waves sent so far
	UnsignedInt m_nextWaveFrame;
	UnsignedInt m_nextSpawnFrame;
	Int m_pending;											///< zombies of the waves sent still to come out
	UnsignedInt m_spawned;
	UnsignedInt m_kills;								///< zombies dead, whatever killed them
	UnsignedInt m_reached;							///< zombies that got within reach of the middle
	std::vector<Zombie> m_zombies;			///< the living, in the order they came out
	std::vector<FogStay> m_fogStays;		///< what of the seats' stands in the ring now, by ObjectID
	UnsignedInt m_fogKills;							///< what of the seats' the ring has killed

	// what was last handed to the client, so the retint and the log run only on a change; nothing else reads them
	Int m_lightLevel;
	Bool m_lightsOn;
	Int m_fogLevel;											///< the fog ring's and the smoke's tint, last handed to the client
	Bool m_fallLogged;
};

extern ApocalypseDirector *TheApocalypseDirector;

#endif // __APOCALYPSEDIRECTOR_H_
