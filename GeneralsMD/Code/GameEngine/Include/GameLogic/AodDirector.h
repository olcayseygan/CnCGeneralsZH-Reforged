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

// FILE: AodDirector.h ////////////////////////////////////////////////////////////////////////////
//
// Art of Defense: the logic system that runs the tower defence mode on a map the random map
// generator's aod style wrote ("-randommap <seed> [players] [size] aod").  It switches itself on when
// the map carries AOD_Base and at least one AOD_Spawn_N waypoint; every other game never sees it, and
// its state goes into the CRC only while it is on.
//
// The seats share one base on the plateau in the middle, all allied.  The civilian player owns the
// waves: on a new game the director makes it every seat's enemy.  A wave is drawn from a table of
// retail units by points, more each wave and more for every seat, and comes out of the spawns a unit
// at a time under a cap on how many may walk at once (issue #92: an AOD map with no cap crashed on
// its tank count).  Every unit attack-follows its spawn's waypoint path, AOD_Spawn_N through the
// AOD_Path_N_k to AOD_Base.  One that crosses the AOD_Base area leaks: it is taken off the map and
// costs the seats a life, a boss more.  A kill pays every seat; so does every wave as it starts.
// No lives left: every seat is out.  Every wave seen off: the seats win.
//
// Pure logic state, fed only by the map, the frame and the logic random stream.  Nothing here reads
// the client.
//
///////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#ifndef __AODDIRECTOR_H_
#define __AODDIRECTOR_H_

#include "Common/GameCommon.h"
#include "Common/GameType.h"
#include "Common/Snapshot.h"
#include <vector>

class Object;
class ThingTemplate;

enum
{
	AOD_WAVES = 20,							///< waves to see off for the win
	AOD_LIVES = 20,							///< leaks the seats can take
	AOD_BOSS_EVERY = 5,					///< every fifth wave brings a boss at its head
	AOD_BOSS_LEAK = 5,						///< lives a boss costs when it leaks
	AOD_ALIVE_CAP = 80,					///< wave units walking at once at the most
	AOD_FIRST_WAVE_SECONDS = 60,		///< game seconds to build before the first wave
	AOD_WAVE_GAP_SECONDS = 30,			///< and between the last of one wave out and the next
	AOD_SPAWN_GAP_FRAMES = 12,			///< logic frames between two units out of the spawns, taking turns
	AOD_KILL_PAY_PER_POINT = 15,		///< credits a wave unit's point pays every seat when it dies
	AOD_WAVE_PAY = 200,					///< credits every seat gets as a wave starts
	AOD_WAVE_PAY_PER_WAVE = 50,		///< and that much more for each wave before it
	AOD_LOG_SECONDS = 10,				///< game seconds between two HEADLESS AOD status lines
	AOD_RETARGET_SECONDS = 4,			///< game seconds between two looks at an idle wave unit
};

/// One kind of wave unit: a retail template, what it costs a wave, the first wave it comes in.
struct AodKind
{
	const char *name;
	Int points;
	Int firstWave;
};

extern Int Aod_kindCount( void );
extern const AodKind &Aod_kind( Int kind );
/// The boss's template, and its health as a share of the template's for this many seats, percent.
extern const char *Aod_bossName( void );
extern Int Aod_bossHealthPercent( Int seats );
/// The points wave (1 on) is worth for this many seats.
extern Int Aod_wavePoints( Int wave, Int seats );
extern Bool Aod_isBossWave( Int wave );
/// The rank a wave's units come in at: regular, then veteran, elite and heroic as the waves go on.
extern Int Aod_waveRank( Int wave );

class AodDirector : public Snapshot
{
public:
	AodDirector( void );

	void reset( void );
	/// Called once the map's objects are in: on when the map names what the mode needs.
	void newMap( Bool loadingSaveGame );
	Bool isActive( void ) const { return m_active; }
	/// Every wave seen off and nothing left walking: the seats have won.
	Bool hasWon( void ) const { return m_active && m_over == OVER_WON; }
	void update( void );

	Int getWave( void ) const { return m_wave; }
	Int getLives( void ) const { return m_lives; }

protected:
	virtual void crc( Xfer *xfer );
	virtual void xfer( Xfer *xfer );
	virtual void loadPostProcess( void ) { }

private:
	enum { OVER_NOT = 0, OVER_WON, OVER_LOST };
	struct Unit
	{
		ObjectID id;
		Int kind;												///< into the kind table, -1 for the boss
	};

	Object *spawn( Int kind, Int spawnIndex );
	Bool inBase( const Object *obj ) const;
	Int nearestPathStop( const Object *obj, Int *spawnOut ) const;	///< for the log alone
	void startWave( UnsignedInt now );
	void paySeats( Int amount ) const;
	void lose( UnsignedInt now );

	// from the map and the data, on every newMap; not saved
	Bool m_active;
	Coord3D m_base;
	Real m_baseRadius;									///< the AOD_Base area's reach from the waypoint
	Int m_enemyPlayer;									///< the civilian player's index, who owns the waves
	Int m_seats;
	std::vector<AsciiString> m_spawnNames;
	std::vector< std::vector<Coord3D> > m_paths;	///< each spawn's waypoints in order, for the log
	std::vector<const ThingTemplate *> m_kinds;
	const ThingTemplate *m_boss;

	// saved
	Int m_wave;													///< the last wave started, 0 before the first
	UnsignedInt m_nextWaveFrame;
	UnsignedInt m_nextSpawnFrame;
	std::vector<Int> m_pending;					///< kinds still to come out this wave, the next first; -1 the boss
	std::vector<Unit> m_units;					///< wave units walking
	Int m_lives;
	Int m_over;
	Int m_spawnTurn;										///< the spawn the next unit comes out of
	UnsignedInt m_spawned, m_kills, m_leaks;
};

extern AodDirector *TheAodDirector;

#endif // __AODDIRECTOR_H_
