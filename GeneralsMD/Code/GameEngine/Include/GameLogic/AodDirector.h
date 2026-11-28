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
// in; every wave pays every seat a stipend as it starts, more to a seat that leaked, and a bonus for a wave
// its lane let none of through (AOD_START_CASH and on).  Twenty lives a seat.  The waves never stop and
// nobody wins or loses (the owner, 2026-11-28: "kazanma yok bunda da kaybetme de şu kadar wave dayandı
// olacak. sınırsız wave sayısı olacak"): no lives left ends the match for every seat alike, and what it
// says is how many waves they held.
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
	/* The waves never end.  Up to AOD_GROWTH_WAVES they grow as they always did (more points, dearer kinds,
		 ranks up to heroic at 18); past it their points level off towards AOD_POINTS_MOST, so a wave stays within
		 the walking cap, and every unit comes AOD_STRENGTH_PER_WAVE percent stronger a wave: health, and the pay
		 a kill and a stipend bring with it.  The formulas take the wave clamped to AOD_WAVE_MOST, where nothing
		 overflows (test_gameengine aod_wave_formulas_*). */
	AOD_GROWTH_WAVES = 20,
	AOD_POINTS_MOST = 266,				///< what a wave's points level off at: 166 at wave 20, 216 at 30, 241 at 50, 254 at 100
	AOD_STRENGTH_PER_WAVE = 8,		///< percent of health and pay a wave adds past AOD_GROWTH_WAVES: 180 at 30, 340 at 50, 740 at 100
	AOD_WAVE_MOST = 9999,					///< the wave the formulas stop growing at
	AOD_REFERENCE_WAVES = 30,			///< a -aodreference run (a dev aid) stops once this many are seen off, so its batches end
	AOD_LIVES = 20,							///< leaks the seats can take together, for every seat
	AOD_BOSS_EVERY = 5,					///< every fifth wave brings a boss at its head
	AOD_BOSS_LEAK = 5,						///< lives a boss costs when it leaks
	AOD_ALIVE_CAP = 80,					///< wave units walking at once at the most, every lane together
	AOD_FIRST_WAVE_SECONDS = 60,		///< game seconds to build before the first wave
	AOD_WAVE_GAP_SECONDS = 40,			///< and between the last of one wave out and the next (30 stacked five waves in a lane by wave 8)
	AOD_SPAWN_GAP_FRAMES = 12,			///< logic frames between two units out of an entry
	/* The money (the owner, 2026-11-28: no harvesting, kills pay): the map has no supply, so a seat lives on
		 what dies in its lane, a stipend as every wave starts, and a bonus for a wave its lane let none of
		 through.  A kill pays about 60 to 90 percent of what the unit costs to build (100 a point: a Paladin
		 900), so the money tracks the waves' strength.  A seat whose lane leaked gets a catch-up on the next
		 stipend, so a lane that falls behind can build back; the bonus is a tenth of a clean wave's kills, so
		 a strong seat does not run away.  The reference batches of 2026-11-28 found kill pay 40, 60 and 100
		 a point nearly alike at the whole budget: what holds a lane is where its guns stand and that they
		 are built again, more than the money. */
	AOD_START_CASH = 6000,				///< every seat's money at the start, whatever the room set
	AOD_KILL_PAY_PER_POINT = 100,	///< credits a wave unit's point pays the seat whose lane it died in
	AOD_STIPEND = 150,						///< credits every seat gets as a wave starts
	AOD_CATCHUP_PER_POINT = 30,		///< and that much a point of the new wave more if its lane leaked the last one it finished
	AOD_CLEAN_PER_POINT = 10,			///< credits a point of a wave its lane let none of through
	AOD_LOG_SECONDS = 10,				///< game seconds between two HEADLESS AOD status lines
	AOD_RETARGET_SECONDS = 4,			///< game seconds between two looks at an idle wave unit
	/* game seconds a wave unit may go without reaching a stop further down its lane before it is sent on again,
		 and three times that before it is taken off the map: a unit caught chasing a gun it cannot reach held its
		 lane's share of the walking cap and stopped every wave after it (a GLA four-seat reference run, 2026-11-28) */
	AOD_STUCK_SECONDS = 40,
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
/// Past AOD_GROWTH_WAVES every wave unit's health and the pay that comes with it, percent; 100 up to it.
extern Int Aod_waveStrengthPercent( Int wave );
/// A wave unit's health as it comes out, percent of its template's at its rank: the boss's share, or the wave's strength.
extern Int Aod_unitHealthPercent( Int wave, Bool boss );
/// An amount of money a wave pays, scaled by its strength.
extern Int Aod_wavePay( Int wave, Int amount );
/// The points wave (1 on) is worth on one lane; every lane gets the same wave.
extern Int Aod_wavePoints( Int wave );
/// A kind's weight in that wave's draw: even early, leaning to the dearer kinds as the waves go on.
extern Int Aod_kindWeight( Int kind, Int wave );
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
	void update( void );

	Int getWave( void ) const { return m_wave; }
	/// The waves the seats held: every one before the wave under way (the match's verdict, there is no winner)
	Int getWavesSurvived( void ) const { return m_over != OVER_NOT ? m_survived : m_wave > 1 ? m_wave - 1 : 0; }
	Int getLives( void ) const { return m_lives; }
	Bool isOver( void ) const { return m_over != OVER_NOT; }
	Int getLaneCount( void ) const { return (Int)m_paths.size(); }

	/* For the HUD (AOD panel in InGameUI.cpp), read at the render's sync point and never written back.  The next
		 wave is drawn a wave ahead (on the first frame, and as each wave starts), so what the panel shows is what
		 comes: its kinds a lane, the next first, -1 the boss. */
	Int getNextWave( void ) const { return m_nextWave; }		///< the wave getNextKinds is, 0 when none is drawn
	const std::vector<Int> &getNextKinds( void ) const { return m_next; }
	/// the frame the next wave starts, once the last of this one is out; 0 while it is still coming out
	UnsignedInt getNextWaveFrame( void ) const { return m_pending.empty() ? m_nextWaveFrame : 0; }
	/// the current wave's units of a kind (-1 the boss) still to come out of a lane or walking it
	Int countLeft( Int lane, Int kind ) const;
	const ThingTemplate *kindTemplate( Int kind ) const;		///< -1 the boss; NULL for a kind the data lacks

protected:
	virtual void crc( Xfer *xfer );
	virtual void xfer( Xfer *xfer );
	virtual void loadPostProcess( void );

private:
	enum { OVER_NOT = 0, OVER_WON, OVER_LOST };		///< OVER_WON: a save from before the waves were endless, or a reference run stopped
	enum { OLD_WAVE_SLOTS = 21 };			///< waves 0..20, the fixed books a save before version 6 carries
	/// one wave's book, kept for every wave started (version 6; a fixed 21 before)
	struct WaveBook
	{
		Int leaks;													///< lives lost to its units
		Int leaked[ AOD_LANES_MOST ];				///< its units that got out of each lane
		Bool settled[ AOD_LANES_MOST ];			///< its last unit in each lane gone, its bonus paid or not
		Int paidKills[ AOD_LANES_MOST ];		///< what it paid each lane's seat, for the log alone; not saved
		Int paidStipend[ AOD_LANES_MOST ];
	};
	struct Unit
	{
		ObjectID id;
		Int kind;												///< into the kind table, -1 for the boss
		Int lane;												///< the lane it walks, from 0
		Int wave;												///< the wave it came with
		Int best;												///< the furthest stop it has been nearest (version 4)
		UnsignedInt since;							///< the frame it got there or spawned (version 4)
	};

	Object *spawn( Int kind, Int lane );
	Bool inExit( const Object *obj ) const;
	Int nearestStop( const Object *obj, Int lane ) const;	///< the stop of its lane nearest a unit
	Player *seatOfLane( Int lane ) const;		///< the seat on the start of that lane's sector, or NULL
	void drawWave( Int wave, std::vector<Int> &kinds ) const;	///< a wave's units out of the logic stream
	void startWave( UnsignedInt now );
	void pay( Player *seat, Int lane, Int amount );
	void lose( UnsignedInt now );
	void logWaves( UnsignedInt now ) const;
	void settleWaves( UnsignedInt now, const std::vector<Int> &alive );	///< alive: a wave's units walking a lane, wave * AOD_LANES_MOST + lane
	WaveBook &book( Int wave );							///< that wave's book, the books grown to it
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
	Int m_nextWave;											///< the wave m_next was drawn for, 0 for none (version 5)
	std::vector<Int> m_next;						///< that wave's kinds, drawn a wave ahead for the HUD (version 5)
	std::vector<Unit> m_units;					///< wave units walking
	Int m_lives;
	Int m_over;
	Int m_spawnTurn;										///< unused since version 2 (every lane gets each unit at once); kept in the save
	UnsignedInt m_spawned, m_kills, m_leaks;
	std::vector<WaveBook> m_books;			///< every wave's book, wave 0 the first (version 6)
	Int m_survived;											///< the waves held, set as the match ends (version 6)
	Bool m_catchUp[ AOD_LANES_MOST ];		///< the lane leaked the last wave it finished: its next stipend carries the catch-up
	Bool m_cashSet;											///< the seats' start money set

	// the reference defence's book, per lane; a dev aid, saved (version 3), its spots found again on a load
	Int m_refPercent;
	Int m_refEarned[ AOD_LANES_MOST ], m_refSpent[ AOD_LANES_MOST ], m_refBought[ AOD_LANES_MOST ];
	Bool m_refStarted;
	std::vector< std::vector<Coord3D> > m_refSpots;
	Int m_refNextSpot[ AOD_LANES_MOST ], m_refPlants[ AOD_LANES_MOST ];
	std::vector<ObjectID> m_refHeld[ AOD_LANES_MOST ];	///< the gun on each spot, gone or never built a spot to build on (version 4)
};

extern AodDirector *TheAodDirector;

#endif // __AODDIRECTOR_H_
