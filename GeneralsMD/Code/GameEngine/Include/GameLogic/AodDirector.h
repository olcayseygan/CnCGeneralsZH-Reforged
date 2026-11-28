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
// the map carries the AOD_Exit area and at least one AOD_Entry_N waypoint; every other game never sees
// it.  Its state goes into the CRC only while it is on, and into every save (GameLogic version 25).
//
// A lane a seat, the map a mirror of itself (RMGLayout::buildAod): one seat's lane runs edge to edge,
// two to six seats' lanes run in from the rim to the pit in the middle.  The seats build on the plateau
// beside their lanes, all allied, humans only; the civilian player owns the waves and the director makes
// it every seat's enemy on a new game.  A wave is drawn from a table of retail units by points, more
// each wave, once for every lane alike, and comes out of every entry at once a unit at a time under a cap
// on how many may walk (issue #92: an AOD map with no cap crashed on its tank count).  Every unit
// attack-follows its lane, AOD_Entry_N through the AOD_Path_N_k, so it shoots back at what shoots it from
// the rim and walks on; the trench's cliffs keep it in the lane, and one that stops picks the lane up
// again at its next stop.  One that reaches the AOD_Exit area has got out: it goes in a flare and costs
// the seats a life, a boss more, out of the lives they share.  A kill pays the seat whose lane it died
// in; every wave pays every seat as it starts.  No lives left: every seat is out.  Every wave seen off:
// the seats win.
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
class PolygonTrigger;
class FXList;
class Player;

enum
{
	AOD_WAVES = 20,							///< waves to see off for the win
	AOD_LIVES = 20,							///< leaks the seats can take, together
	AOD_BOSS_EVERY = 5,					///< every fifth wave brings a boss at its head
	AOD_BOSS_LEAK = 5,						///< lives a boss costs when it leaks
	AOD_ALIVE_CAP = 80,					///< wave units walking at once at the most, every lane together
	AOD_FIRST_WAVE_SECONDS = 60,		///< game seconds to build before the first wave
	AOD_WAVE_GAP_SECONDS = 30,			///< and between the last of one wave out and the next
	AOD_SPAWN_GAP_FRAMES = 12,			///< logic frames between two units out of an entry
	AOD_KILL_PAY_PER_POINT = 15,		///< credits a wave unit's point pays the seat whose lane it died in
	AOD_WAVE_PAY = 200,					///< credits every seat gets as a wave starts
	AOD_WAVE_PAY_PER_WAVE = 50,		///< and that much more for each wave before it
	AOD_LOG_SECONDS = 10,				///< game seconds between two HEADLESS AOD status lines
	AOD_RETARGET_SECONDS = 4,			///< game seconds between two looks at an idle wave unit
	AOD_LANES_MOST = 6,					///< a lane a seat
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
/// The boss's template, and its health on that wave as a share of the template's, percent.
extern const char *Aod_bossName( void );
extern Int Aod_bossHealthPercent( Int wave );
/// The points wave (1 on) is worth on one lane; every lane gets the same wave.
extern Int Aod_wavePoints( Int wave );
extern Bool Aod_isBossWave( Int wave );
/// The rank a wave's units come in at: regular, then veteran, elite and heroic as the waves go on.
extern Int Aod_waveRank( Int wave );
/** The reference defence's share of a seat's pay, percent: what this run's -aodreference asks (0 without
	  it), and what a replay being played back recorded ("AR=" in its header). */
extern Int Aod_referencePercentAsked( void );
extern void Aod_setReplayReferencePercent( Int percent );

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
	virtual void loadPostProcess( void );

private:
	enum { OVER_NOT = 0, OVER_WON, OVER_LOST };
	struct Unit
	{
		ObjectID id;
		Int kind;												///< into the kind table, -1 for the boss
		Int lane;												///< the lane it walks, from 0
		Int wave;												///< the wave it came with
	};

	Object *spawn( Int kind, Int lane );
	Bool inExit( const Object *obj ) const;
	Int nearestStop( const Object *obj, Int lane ) const;	///< the stop of its lane nearest a unit
	Player *seatOfLane( Int lane ) const;		///< the seat on the start of that lane's sector, or NULL
	void startWave( UnsignedInt now );
	void pay( Player *seat, Int lane, Int amount );
	void lose( UnsignedInt now );
	void logWaves( UnsignedInt now ) const;
	// the reference defence (-aodreference <percent>, a skirmish dev aid, dead in any network game)
	void referenceDefence( UnsignedInt now );
	void findReferenceSpots( void );

	// from the map and the data, on every newMap; not saved
	Bool m_active;
	PolygonTrigger *m_exit;							///< the map's AOD_Exit area
	Int m_enemyPlayer;									///< the civilian player's index, who owns the waves
	Int m_seats;
	std::vector< std::vector<Coord3D> > m_paths;	///< each lane's waypoints in order, its entry first
	std::vector< std::vector<AsciiString> > m_stops;	///< and their names
	std::vector<const ThingTemplate *> m_kinds;
	const ThingTemplate *m_boss;
	const FXList *m_exitFX;							///< what a unit that gets out goes in

	// saved
	Int m_wave;													///< the last wave started, 0 before the first
	UnsignedInt m_nextWaveFrame;
	UnsignedInt m_nextSpawnFrame;
	std::vector<Int> m_pending;					///< kinds still to come out on every lane this wave, the next first; -1 the boss
	std::vector<Unit> m_units;					///< wave units walking
	Int m_lives;
	Int m_over;
	Int m_spawnTurn;										///< unused since version 2 (every lane gets each unit at once); kept in the save
	UnsignedInt m_spawned, m_kills, m_leaks;
	Int m_waveLeaks[ AOD_WAVES + 1 ];		///< lives lost to each wave's units

	// the reference defence's book, per lane; a dev aid, saved (version 3), its spots found again on a load
	Int m_refPercent;
	Int m_refEarned[ AOD_LANES_MOST ], m_refSpent[ AOD_LANES_MOST ], m_refBought[ AOD_LANES_MOST ];
	Bool m_refStarted;
	std::vector< std::vector<Coord3D> > m_refSpots;
	Int m_refNextSpot[ AOD_LANES_MOST ], m_refPlants[ AOD_LANES_MOST ];
};

extern AodDirector *TheAodDirector;

#endif // __AODDIRECTOR_H_
