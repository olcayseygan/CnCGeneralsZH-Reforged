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
// game second under a cap on how many may walk at once.  Every zombie attack-moves down a flow
// field (ApocalypseFlowField) to the seats' nearest structure by the way there, and takes its route
// from the field instead of the path queue; one the field cannot route goes to the middle, and one
// gone idle beside its structure at the nearest thing of a seat's.  The seats lose when everything
// of theirs is dead (VICTORY_APOCALYPSE); the dead never lose.
//
// The match runs a day and night cycle in game seconds: day, dusk, night, dawn, and day again.  By
// day the waves are light; at dusk they thicken; at night they come fast and big.  The kinds come out
// of one roster table (theKinds in the .cpp): each has its threat points, the first night it walks,
// a weight a phase, a group size (the Carrion Bats fly in flocks) and flags.  Day 1 sends walkers
// only; a kind that unlocks on night N walks at dusk and night from then and by day from the day
// after.  Elites never come by day, one a living seat a wave; flyers take at most a share of a
// wave's points.  Anything else of the dead's that walks (a spawn line, what a death leaves) is
// adopted as a stray, counted under the cap and sent on.  Every cycle
// sends more and ranks them one level higher, to Heroic.  The light follows: the director hands the
// client a night amount after each tick (LogicThread::callAfterTick), the client tints the map's own
// light toward a dim red and puts every drawable in its NIGHT state once it is past half; nothing in
// the logic reads any of it back.
//
// A fog ring runs round the map, between the map's Apocalypse_Clear square and its edge: shrouded
// for every player whoever looks (PartitionManager::setAlwaysShroudedOutside), drawn as a thin red
// fog, and deadly: a unit of anyone but the dead that stands in it APOCALYPSE_FOG_GRACE_SECONDS dies,
// save a plane flying a general's power in from off the map (a supply drop, a paradrop, a strike).
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
class Path;
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
	APOCALYPSE_TELEGRAPH_SECONDS = 20,		///< game seconds a wave's gates glow before it comes; nightfall brings its first that soon
	APOCALYPSE_WAVE_BASE = 20,						///< a wave's threat points before its phase, cycle and the adaptive share: this,
	APOCALYPSE_WAVE_PER_SEAT = 14,				///< and this many a seat,
	APOCALYPSE_WAVE_GROWTH = 8,						///< and this many more every wave
	APOCALYPSE_WAVE_MOST = 1500,					///< a wave's points at most
	APOCALYPSE_CYCLE_GROWTH_PERCENT = 60,	///< more in every wave for each cycle gone by
	APOCALYPSE_POINTS_WALKER = 1,					///< what each kind costs of a wave's points
	APOCALYPSE_POINTS_RUNNER = 2,
	APOCALYPSE_POINTS_BRUTE = 6,
	APOCALYPSE_SKILL_PER_POINT = 20,			///< the general's skill points a zombie kill is worth, a threat point of it (doubled by the owner, 2026-11-23)
	APOCALYPSE_SKILL_PER_POINT_BOSS = 10,	///< and a boss's
	APOCALYPSE_FLYER_SHARE_EARLY = 15,		///< percent of a wave's points flyers may take, before APOCALYPSE_FLYER_LATE_NIGHT,
	APOCALYPSE_FLYER_SHARE_LATE = 25,			///< and from that night on
	APOCALYPSE_FLYER_LATE_NIGHT = 4,
	APOCALYPSE_GROUP_SPACING = 12,				///< world units between two of a flock or a pack as they come out
	APOCALYPSE_ADAPT_START = 100,					///< the adaptive share, percent: where it starts,
	APOCALYPSE_ADAPT_LEAST = 100,					///< how low it falls when waves reach the bases: never under the plain wave,
	APOCALYPSE_ADAPT_MOST = 600,					///< and how high it climbs when they die far out
	APOCALYPSE_ADAPT_UP = 30,							///< added when a wave was cleared before the next and reached nothing
	APOCALYPSE_ADAPT_SLOW = 10,						///< added when it was not cleared but reached nothing either
	APOCALYPSE_ADAPT_DOWN = 10,						///< taken off when it reached the bases
	APOCALYPSE_INFECT_SECONDS = 3,				///< game seconds a soldier the dead killed lies before he rises as one of them
	APOCALYPSE_RETARGET_SECONDS = 2,			///< game seconds between two looks of one zombie for something to go at
	APOCALYPSE_LOG_SECONDS = 10,					///< game seconds between two status lines
	APOCALYPSE_SEATS_MOST = 4,						///< co-op seats the waves size for at the most
	APOCALYPSE_HEAT_SECONDS = 2,					///< game seconds between two counts of the dead in the fog for its heat
	APOCALYPSE_HEAT_BLOCK = 4,						///< partition cells a side of a heat block
	APOCALYPSE_HEAT_PER_ZOMBIE = 64,			///< a block's heat for each of the dead in it, up to 255: four fill it
	APOCALYPSE_HEAT_GLOW = 255,						///< the fog's shroud level over the hottest block: clear, all the fog's red
	APOCALYPSE_WARN_GLOW = 215,						///< and over a sector whose wave is gathering, before any of it has risen
	APOCALYPSE_SPAWNS_MOST = 32,					///< spawn waypoints read at most: a wave's passes are a 32-bit mask
	APOCALYPSE_SPAWN_SPREAD = 60,					///< world units a zombie lands either way off its spawn
	APOCALYPSE_BASE_REACH = 200,					///< world units past the farthest start from the middle that count as having reached the bases
	APOCALYPSE_FOG_GRACE_SECONDS = 4,			///< game seconds anything but the dead may stand in the fog ring before it dies there
	APOCALYPSE_FOG_EDGE_IN = 40,					///< world units a ring spawn keeps off the map's edge and off the clear area
	APOCALYPSE_FOG_TRIES = 16,						///< spots drawn for a ring spawn before it falls back on a spawn waypoint
	APOCALYPSE_LIGHT_STEPS = 32,					///< the night amount the client is handed, in steps
	APOCALYPSE_LIGHT_TINT_EVERY = 4,			///< steps between two retints of the light tables, each a full relight of the ground
	APOCALYPSE_FIELD_CELLS_PER_FRAME = 10000,	///< flow field cells a logic frame builds or repairs
	APOCALYPSE_REACHED_LEAST = 10,				///< the dead that must reach a seat's structure between two waves for the adaptive share to read the bases as reached
};

/// how one phase plays: its length, how often a wave comes and how big, how fast it comes out, the cap
struct ApocalypsePhaseRules
{
	Int seconds;
	Int waveSeconds;
	Int sizePercent;
	Int spawnsPerFrame;					///< zombies a wave lets out a logic frame at most, so it comes as one mass
	Int aliveCap;
	Int sectorsLeast, sectorsMost;	///< gates a wave gathers at; MOST over the gate count means all of them
};

extern const ApocalypsePhaseRules &Apocalypse_phaseRules( Int phase );
extern const char *Apocalypse_phaseName( Int phase );

enum
{
	APOCALYPSE_KIND_WALKER = 0,						///< the roster's first row, what the fallen rise as and what a roll falls back on
	APOCALYPSE_KIND_ELITE = 1 << 0,				///< flags: never by day or dawn
	APOCALYPSE_KIND_FLYER = 1 << 1,
	APOCALYPSE_KIND_BOSS = 1 << 2,
};

/** One kind of the dead: its template, what it costs of a wave's points, the first night it comes
	  (0 from the start), how often a roll picks it in each phase (weights, summed over the kinds allowed),
	  how many come together, and its flags. */
struct ApocalypseKind
{
	const char *name;
	Int points;
	Int firstNight;
	Int weight[ APOCALYPSE_PHASES ];
	Int group;
	Int flags;
};

extern Int Apocalypse_kindCount( void );
extern const ApocalypseKind &Apocalypse_kind( Int kind );

/** Whether that kind may come in that phase of that cycle (from 0): first night N means at dusk and night
	  of night N (cycle N - 1) on, by day and dawn from cycle N; an elite never by day or dawn. */
extern Bool Apocalypse_kindAllowed( Int kind, Int phase, UnsignedInt cycle );

/** percent of a wave's points flyers may take in that phase of that cycle (from 0): the night number
	  counts as for an unlock, so the late share starts at the dusk before night APOCALYPSE_FLYER_LATE_NIGHT */
extern Int Apocalypse_flyerSharePercent( Int phase, UnsignedInt cycle );

/// the threat points a wave of that number (from 1) brings for that many seats, in that phase of that cycle (from 0), at that adaptive share
extern Int Apocalypse_wavePoints( UnsignedInt wave, Int seats, Int phase, UnsignedInt cycle, Int adaptPercent );

/** The adaptive share after a wave: down when it reached the bases, up fast when it was cleared before
	  the next one came, a little when it was held but not cleared. */
extern Int Apocalypse_adapt( Int adaptPercent, Bool reachedBases, Bool cleared );

/// the rank a zombie of that cycle walks in, 0 Regular to 3 Heroic
inline Int Apocalypse_cycleRank( UnsignedInt cycle ) { return cycle < 3 ? (Int)cycle : 3; }

/// how far into the night that point of a phase is: 0 by day, rising through dusk, 1 at night, falling at dawn
extern Real Apocalypse_nightAmount( Int phase, UnsignedInt intoPhase, UnsignedInt phaseFrames );

/** Where that frame falls on the director's clock, which starts with the map at frame 0 and runs its
	  phases at fixed lengths: the phase, frames into it, its length and the cycle (from 0).  A pure
	  function of the frame, so the client's match clock can read it without touching the director. */
extern void Apocalypse_phaseAt( UnsignedInt frame, Int *phase, UnsignedInt *intoPhase, UnsignedInt *phaseFrames, UnsignedInt *cycle );

/// client side: how thick the dead stand in the fog round that partition cell, 0 to 255, from the last count handed over
extern Int Apocalypse_heatAt( Int cellX, Int cellY );
/// client side: the heat count's blocks a side (0 with no count yet) and one block's heat, 0 to 255; a block is APOCALYPSE_HEAT_BLOCK partition cells a side
extern void Apocalypse_heatGrid( Int *blocksX, Int *blocksY );
extern Int Apocalypse_heatOfBlock( Int blockX, Int blockY );

//-------------------------------------------------------------------------------------------------
/** The way in for the dead: one integer Dijkstra field over the pathfind grid's ground layer, from
	  every cell beside a seat's structure, so a zombie's route is a walk down the field instead of a
	  search of its own (measured in source/Tools/PathLab/apocflow.py, TODO Apocalypse > Performance 4).
	  The step costs are the search's (examineNeighboringCells) without its turn, traffic and wall hug
	  charges: 10 and 14 and a pinched cell +24.  Every body walks the same field, since the search
	  reads the ground under the centre cell only.

	  The field reads its own copy of the ground (walkable, pinched, the structure a cell is beside),
	  refreshed only where the pathfinder changed it (Pathfinder::internal_classifyObjectFootprint)
	  and only when a repair begins, so the field is a function of that copy alone: a cell keeps its
	  distance, its step, the first of its eight neighbours in examineNeighboringCells' order that
	  gives that distance, and the structure the step's cell leads to.  A repair clears every cell
	  whose route ran over ground that got dearer or closed, floods those again from their rim and
	  lets every cheaper or tied way spread from the changed ground, re-reading a cell's step whenever
	  it is reached: it ends on exactly the field a build from scratch makes
	  (apocalypse_flow_field_repair_matches_a_rebuild).  So a load needs only the copy and the repair
	  in hand: it builds the field the repair began from and runs that repair again to where it
	  stood.  It runs a slice a frame (stepRepair); a cell it has not refilled yet answers no route
	  and its zombie asks the pathfinder as before.

	  Built only while the director is on, from logic state only, in a fixed order with ties broken
	  on the cell: every machine holds the same field. */
class ApocalypseFlowField
{
public:
	enum { WALKABLE = 0x01, PINCHED = 0x02 };
	/// one cell of the ground copy changed: what it was and what the pathfinder says now
	struct Change
	{
		Int cell;
		UnsignedByte oldGround, newGround;
		ObjectID oldSource, newSource;
	};

	ApocalypseFlowField( void );
	void clear( void );																		///< nothing kept, memory given back
	/// a grid of that size: no ground, no route, no repair
	void setup( Int width, Int height );
	Bool isStarted( void ) const { return !m_dist.empty(); }
	Bool isRepairing( void ) const { return m_phase != PHASE_IDLE; }
	Bool isBuilding( void ) const { return m_phase != PHASE_IDLE && m_all; }
	Int getWidth( void ) const { return m_width; }
	Int getHeight( void ) const { return m_height; }

	/// the ground copy, set directly before a build (or by a load)
	UnsignedByte groundOf( Int cell ) const { return m_ground[ cell ]; }
	ObjectID sourceOf( Int cell ) const { return m_source[ cell ]; }
	void setGround( Int cell, UnsignedByte ground, ObjectID source ) { m_ground[ cell ] = ground; m_source[ cell ] = source; }

	/// every route forgotten and a build of the whole grid begun from the ground copy
	void beginBuild( void );
	/// apply these changes to the ground copy and begin the repair they need; only when none runs
	void beginRepair( const std::vector<Change> &changes );
	/// carry the running build or repair on by about `budget` cells; the cells it spent
	Int stepRepair( Int budget );
	const std::vector<Change> &repairChanges( void ) const { return m_changes; }

	/** The cells from (x, y) down to a structure's side, the first and last included; FALSE when the
		  cell has no route yet, or the walk meets ground that has changed under it. */
	Bool route( Int x, Int y, std::vector<Int> *cells ) const;
	Int cellX( Int index ) const { return index / m_height; }
	Int cellY( Int index ) const { return index % m_height; }
	Int distOf( Int index ) const { return m_dist[ index ]; }
	UnsignedByte stepOf( Int index ) const { return m_step[ index ]; }
	ObjectID targetOf( Int index ) const { return m_label[ index ]; }
	enum { UNREACHED = 0x7FFFFFFF, NO_STEP = 0xFF };

private:
	enum Phase { PHASE_IDLE, PHASE_MARK, PHASE_SEED, PHASE_FLOOD };
	enum { MARK_UNKNOWN = 0, MARK_IN = 1, MARK_OUT = 2, SCAN_PER_CELL = 16 };
	struct Open
	{
		Int dist;
		Int index;
		Bool operator>( const Open &that ) const { return dist != that.dist ? dist > that.dist : index > that.index; }
	};

	Bool walkable( Int x, Int y ) const;
	Bool stepAllowed( Int x, Int y, Int direction ) const;		///< walkable there, and a diagonal not through a closed corner
	Int enterCost( Int index, Int direction ) const;
	void push( Int dist, Int index );
	void settle( Int index );															///< the step and structure from the neighbours, then offer itself to them
	void markChain( Int index );
	void clearCell( Int index );

	Int m_width, m_height;					///< the pathfinder's grid; a cell is x * m_height + y, as its clearance
	std::vector<UnsignedByte> m_ground;	///< WALKABLE, PINCHED: the ground copy
	std::vector<ObjectID> m_source;	///< the seat's structure a walkable cell is beside, INVALID_ID for none
	std::vector<Int> m_dist;				///< the route's cost from here, UNREACHED for none
	std::vector<UnsignedByte> m_step;	///< which of the eight neighbours the route goes on to, NO_STEP at a source
	std::vector<ObjectID> m_label;	///< the structure the route ends beside

	// the running build or repair
	Phase m_phase;
	Int m_cursor;
	Bool m_all;											///< the whole grid: a build
	std::vector<Change> m_changes;	///< what the repair applied, kept for a save
	std::vector<UnsignedByte> m_mark;	///< MARK_*: does the cell's route run over ground that got dearer
	std::vector<UnsignedByte> m_queued;	///< queued since it last settled: a second entry for the same distance settles nothing
	std::vector<Int> m_region;			///< the cells cleared to be filled again
	std::vector<Int> m_cheaper;			///< changed cells that only got cheaper or became sources
	std::vector<Int> m_chain;				///< markChain's walk
	std::vector<Open> m_open;				///< a min-heap on (dist, index)
};

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

	/// one of the dead: while the mode is on, an object of the dead player made from one of the zombie templates
	Bool isZombie( const Object *obj ) const;

	/// the pathfinder changed the ground under a structure there (cell rectangle): the flow field repairs it
	void noteGroundChange( const IRegion2D &cells );
	/** One of the dead's route from `from` down the flow field, as an optimized path, when it ends at
		  `to`, the goal the director sent it to; NULL otherwise, and the search answers as before. */
	Path *fieldPath( const Object *obj, const Coord3D *from, const Coord3D *to );
	/// the general's skill points a kill of that zombie is worth: its threat points times APOCALYPSE_SKILL_PER_POINT
	Int skillPointsOf( const Object *zombie ) const;

	/// once a logic frame, from GameLogic::update
	void update( void );

	UnsignedInt getWave( void ) const { return m_wave; }
	UnsignedInt getKills( void ) const { return m_kills; }
	Int getAlive( void ) const { return (Int)m_zombies.size(); }
	Int getPhase( void ) const { return m_phase; }
	UnsignedInt getCycle( void ) const { return m_cycle; }
	/** The next wave gathers in the gate sector this ring cell (partition cell coordinates) lies in:
		  the client draws it deeper red.  Read only. */
	Bool isWarnedCell( Int cellX, Int cellY ) const;

protected:
	virtual void crc( Xfer *xfer );
	virtual void xfer( Xfer *xfer );
	virtual void loadPostProcess( void );

private:
	struct Zombie
	{
		ObjectID id;
		Bool reached;										///< it has stood beside a seat's structure or hit one (hasReachedBases)
	};

	struct FogStay
	{
		ObjectID id;
		UnsignedInt since;							///< the frame it was first found in the ring this time
	};

	struct Rise
	{
		ObjectID victim;								///< the soldier that fell, while his body lies
		Coord3D where;
		UnsignedInt frame;							///< when he rises; 0 once he has
	};

	Int spawnOne( UnsignedInt now );		///< one roll: one of the dead, or a whole flock or pack; how many came out
	Object *makeZombie( Int kind, const Coord3D &pos, UnsignedInt now, Object *made = NULL );
	Int kindOf( const Object *obj ) const;	///< its row in the roster, -1 for none
	Int sectorOf( Real x, Real y ) const;
	void gatherWave( UnsignedInt now );
	void sendWave( UnsignedInt now );
	void watchTheFallen( UnsignedInt now );
	void handHeat( void ) const;		///< count the dead in the fog by block and hand the client the copy, nothing kept
	Bool ringSpot( Coord3D *pos, const Object *obj, Int sector );
	Bool inRing( const Coord3D *pos ) const;
	void killInFog( UnsignedInt now );
	void sendOn( Object *obj );
	Int seatsAlive( void ) const;
	void advancePhase( UnsignedInt now );
	void handLight( UnsignedInt now );
	Bool fieldRoute( const Object *obj, const Coord3D *from, std::vector<Int> *cells ) const;
	Bool fieldGoal( const Object *obj, Coord3D *goal ) const;	///< where its route ends, when that is somewhere to walk to
	void readGround( Int x, Int y, UnsignedByte *ground, ObjectID *source ) const;	///< the pathfinder's ground as the field copies it
	void startField( void );
	void stepField( void );
	Bool hasReachedBases( const Object *obj ) const;	///< beside a seat's structure, or hitting one
	void xferField( Xfer *xfer, Bool withChanges );

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
	std::vector<const ThingTemplate *> m_kinds;	///< one a roster row, NULL where the data lacks it; a missing walker spawns nothing, a missing kind walks as a walker

	// logic state, saved and in the CRC
	Int m_phase;												///< ApocalypsePhase
	UnsignedInt m_phaseStartFrame;
	UnsignedInt m_cycle;								///< nights gone by
	UnsignedInt m_wave;									///< waves sent so far
	UnsignedInt m_nextWaveFrame;
	UnsignedInt m_nextSpawnFrame;
	Int m_pending;											///< threat points of the waves sent still to come out
	UnsignedInt m_waveSectors;					///< the gates the waves out now come from, one bit a gate
	UnsignedInt m_warnSectors;					///< the gates the next wave gathers at, glowing; 0 until it is told
	Int m_adapt;												///< the adaptive share, percent (Apocalypse_adapt)
	UnsignedInt m_frontTurn;						///< whose front the next wave gathers before: the held starts' passes, in gate order
	UnsignedInt m_reachedAtWave;				///< m_reached when the last wave came, to judge it by
	std::vector<Rise> m_rises;					///< the fallen soldiers waiting to rise, and those that have, while their bodies lie
	UnsignedInt m_infected;							///< soldiers risen as the dead
	UnsignedInt m_spawned;
	UnsignedInt m_kills;								///< zombies dead, whatever killed them
	UnsignedInt m_reached;							///< zombies that got within reach of the middle
	std::vector<Zombie> m_zombies;			///< the living, in the order they came out
	std::vector<FogStay> m_fogStays;		///< what of the seats' stands in the ring now, by ObjectID
	UnsignedInt m_fogKills;							///< what of the seats' the ring has killed
	UnsignedInt m_fieldRepairs;					///< flow field repairs finished, the build included (in the CRC as a check)

	// the flow field: saved as its ground copy, the repair in hand and the changes waiting (xfer)
	ApocalypseFlowField m_field;
	Bool m_fieldWanted;									///< build it on the next tick
	UnsignedInt m_fieldSteps;						///< slices the running build or repair has had, for a load to run it again
	std::vector<IRegion2D> m_groundChanges;	///< ground changed since the running repair began
	UnsignedInt m_fieldAnswered, m_fieldRefused, m_fieldCells;	///< for the log only
	// what a load read, for loadPostProcess to build the field from
	Bool m_fieldLoaded, m_fieldLoadedRepair, m_fieldLoadedBuild;
	std::vector<ApocalypseFlowField::Change> m_fieldLoadedChanges;
	Int m_eliteRoom;										///< elites the waves out now may still send: one a living seat, reset by each wave
	Int m_flyerRoom;										///< threat points flyers may still take of them: the flyer share of the last wave

	// what was last handed to the client, so the retint and the log run only on a change; nothing else reads them
	Int m_lightLevel;
	Bool m_lightsOn;
	Int m_fogLevel;											///< the fog ring's and the smoke's tint, last handed to the client
	Bool m_fallLogged;
	UnsignedInt m_straysLogged;					///< strays adopted, for the log alone; not saved
};

extern ApocalypseDirector *TheApocalypseDirector;

#endif // __APOCALYPSEDIRECTOR_H_
