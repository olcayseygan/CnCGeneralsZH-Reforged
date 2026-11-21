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

// FILE: LaneWarDirector.h ////////////////////////////////////////////////////////////////////////
//
// Lane War (docs/plans/LANE-MODE.md in the wrapper): the logic system that runs the mode on a map
// the random map generator's lane type wrote.  It switches itself on when the map carries the
// LaneWar_ build plots, middle area and spawn waypoints, and every other game never sees it.
//
// Lane players have no base: no command centre, no workers, nothing to build.  A build plot is a
// grid of square slots (LANEWAR_SLOT_SIZE) in the plot's own frame: the plot stands behind its end of
// the lane, its edge towards the lane is its front, a slot's column counts across that front from the
// player's left as he faces the enemy, and its row counts back from the front.  A unit is bought onto
// it at a slot through MSG_LANEWAR_ORDER (LANEWAR_ORDER_PLACE), covering as many slots a side as its
// footprint needs.  The plot's units stand at their slots: held, unable to fire, taking no damage,
// and no weapon may pick them.  None of that leans on the terrain around the plot.  Every 30 wall
// seconds a spawn tick pays every player and sends one seat's wave from each team, the seats taking
// turns: the plot's units are copied, template and slot place relative to the grid's centre, onto
// the team's spawn pad behind its fortress with the plot's front row in front, facing the enemy, and march past it down the lane
// as AIGroup::groupLaneMarch sends them.  The copies
// belong to the player and cannot be selected; each dies 130 wall seconds after it spawned.  A copy
// presses its own buttons on the skirmish AI's rules, as far as its player's switches let it
// (LANEWAR_ORDER_ABILITY), and one left standing short of the enemy fortress marches on.  A team
// loses when its fortress dies, through VictoryConditions, and nothing else counts.
//
// Fixed-wing jets stand on the plot's grid like any unit but do not march.  A wave's jet copies join
// their seat's squadron, which waits off the map behind its own end with nothing of it on the map.
// When the two teams' fronts are about to meet, the squadron's rearmed jets fly in line abreast, as
// their slots lay them out, straight along the lane to the meeting point, strike, and fly back off
// the map, where they reload and wait for the next clash until shot down or their lifespan is up.
// Helicopters stand on the ground grid and march with the wave.
//
///////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#ifndef __LANEWARDIRECTOR_H_
#define __LANEWARDIRECTOR_H_

#include "Common/GameCommon.h"
#include "Common/GameType.h"
#include "Common/Snapshot.h"
#include <vector>

class CommandButton;
class Object;
class Player;
class ThingTemplate;
class UpgradeTemplate;

enum
{
	LANEWAR_TEAMS = 2,										///< team A starts on the odd starts (1, 3, 5), team B on the even
	LANEWAR_SEATS = 3,										///< seats a team; seat N of a team stands on its Nth start
	LANEWAR_START_MONEY = 2000,						///< every player's cash when the match starts
	LANEWAR_INCOME = 400,									///< paid to every player on every spawn tick: 533 a minute, as 267 every 30 seconds was
	LANEWAR_MIDDLE_BONUS_PERCENT = 25,		///< on top of the income, for the team holding past the centre line
	LANEWAR_TICK_SECONDS = 45,						///< wall seconds between two spawn ticks
	LANEWAR_FIRST_TICK_SECONDS = 90,			///< wall seconds from the start to the first spawn tick, time to fill the plots
	LANEWAR_LIFESPAN_SECONDS = 130,				///< wall seconds a wave copy lives
	LANEWAR_SLOT_SIZE = 20,								///< world units a side of one plot slot, two cells; a plot 240 across its front and 320 deep is 12 columns by 16 rows
	LANEWAR_INCOME_TIERS = 4,							///< income levels a player can buy past the first
	LANEWAR_INCOME_TIER_PRICE = 1000,			///< each of them
	LANEWAR_INCOME_TIER_PAYS = 100,				///< more on every tick for each level bought, back in ten ticks
	LANEWAR_TECH_TIERS = 3,								///< a player starts on tech tier 1
	LANEWAR_SELL_SENT_PERCENT = 50,				///< a unit that has gone out in a wave sells back for this share; one that has not, for all of it
	LANEWAR_JET_PARK_MARGIN = 100,				///< world units past the map's border, behind his own end, where a player's jets wait
	LANEWAR_STRIKE_CONTACT_GAP = 250,			///< two fronts this close along the lane are fighting already: a strike goes now
	LANEWAR_STRIKE_SECONDS = 30,					///< game seconds a strike may last before its jets are called home
	LANEWAR_JET_REARM_SECONDS = 15,				///< game seconds a parked jet takes to reload before it can strike again
	LANEWAR_CARGO_LOG_SECONDS = 5,				///< wall seconds between the log's counts of passengers and drones out in the waves
};

#define LANEWAR_TICK_FRAMES ((UnsignedInt)(LANEWAR_TICK_SECONDS * LOGICFRAMES_PER_SECOND))
#define LANEWAR_FIRST_TICK_FRAMES ((UnsignedInt)(LANEWAR_FIRST_TICK_SECONDS * LOGICFRAMES_PER_SECOND))
#define LANEWAR_LIFESPAN_FRAMES ((UnsignedInt)(LANEWAR_LIFESPAN_SECONDS * LOGICFRAMES_PER_SECOND))
#define LANEWAR_STRIKE_FRAMES ((UnsignedInt)(LANEWAR_STRIKE_SECONDS * LOGICFRAMES_PER_GAME_SECOND))
#define LANEWAR_JET_REARM_FRAMES ((UnsignedInt)(LANEWAR_JET_REARM_SECONDS * LOGICFRAMES_PER_GAME_SECOND))

/** Whether a player's parked jets take off now for the coming clash.  `gap` is how far apart his team's
	  front and the enemy's stand along the lane, `mySpeed` and `enemySpeed` how fast each front closes on
	  the other a frame, `parkBehind` how far his jets wait behind his own front, `jetSpeed` the slowest
	  jet's a frame.  Due when the fronts are in contact, or when they will meet no later than the jets
	  can be there; *meetAhead is then how far in front of his front they meet. */
extern Bool LaneWar_strikeDue( Real gap, Real mySpeed, Real enemySpeed, Real parkBehind, Real jetSpeed, Real *meetAhead );

/** What MSG_LANEWAR_ORDER asks for, its first argument; the integers after it depend on it.  A plot
	  has one grid, LANEWAR_GRID_GROUND, and every unit stands on it, jets included; the grid argument
	  some orders carry is always that one (the air rows that were the second grid are gone). */
enum LaneWarOrderKind
{
	LANEWAR_ORDER_PLACE = 0,							///< (template id, column, row): buy that unit onto the sender's plot, its block's front left slot at that one, in the grid its kind takes
	LANEWAR_ORDER_TIER = 1,								///< (track, level): buy the next level of a track, LANEWAR_TRACK_INCOME or LANEWAR_TRACK_TECH
	LANEWAR_ORDER_SELL = 2,								///< (grid, column, row): sell the sender's plot unit covering that slot
	LANEWAR_ORDER_MOVE = 3,								///< (grid, from column, from row, to column, to row): put the sender's plot unit covering the first slot down with its front left slot at the second
	LANEWAR_ORDER_UPGRADE = 4,						///< (index): buy that entry of LaneWar_upgrades for the sender
	LANEWAR_ORDER_ABILITY = 5,						///< (template id, button slot, on): the sender's wave units of that template use that button (1) or leave it (0); the slot is an entry of LaneWar_abilitiesOf
	LANEWAR_ORDER_VETERANCY = 6,					///< (grid, column, row[, passenger]): raise the sender's plot unit covering that slot, or that passenger of it, one rank; its wave copies go out at it
	LANEWAR_ORDER_GARRISON = 7,						///< (grid, column, row, template id): buy an infantryman into the sender's plot unit covering that slot, one whose passengers fire out
	LANEWAR_ORDER_UNIT_UPGRADE = 8,				///< (grid, column, row, button slot): buy the object upgrade on that command set slot of the sender's plot unit covering that slot
};
// LANEWAR_ORDER_SELL takes a fourth integer too, a passenger's index (-1 or none for the unit and all it carries)

/// what each rank costs on top of a unit bought for `cost`, as a share of it: Veteran, Elite, Heroic
enum
{
	LANEWAR_VETERAN_PERCENT = 50,
	LANEWAR_ELITE_PERCENT = 75,
	LANEWAR_HEROIC_PERCENT = 100,
};

/// what raising a unit bought for `cost` to that rank costs (1 Veteran, 2 Elite, 3 Heroic); 0 for any other rank
extern Int LaneWar_veterancyPrice( Int cost, Int level );

/** A wave unit's button the director presses by itself (LaneWarDirector::useAbilities), read off the
	  template's own command set.  A weapon switch pair lists only its non-primary slot, and the remote
	  charges' detonator goes with the planting; both of those and the primary switch are always on. */
enum LaneWarAbilityKind
{
	LANEWAR_ABILITY_WEAPON,								///< a weapon only a button fires, or the one that ends the unit
	LANEWAR_ABILITY_POWER,								///< a special at an enemy: the laser lock, the charges, TNT, the hacks, the captures
	LANEWAR_ABILITY_SWITCH,								///< a weapon switch onto that slot
	LANEWAR_ABILITY_COMMAND,							///< hijack, sabotage, car bomb, the Internet hack
};

struct LaneWarAbility
{
	const CommandButton *button;
	Int slot;															///< the button's index in the command set from 0 (the INI's number less one), the toggle's key
	Int kind;															///< LaneWarAbilityKind
	Bool defaultOn;
};

enum { LANEWAR_ABILITIES_MAX = 8 };

/// the template's abilities in its own command set, in slot order; none for a structure
extern Int LaneWar_abilitiesOf( const ThingTemplate *tmpl, LaneWarAbility *out, Int maxItems );
/** On unless a player switches it off: building captures and the money buttons (the cash hack, the
	  Internet hack, sabotage) and the car bomb start off.  A GUICommandType and, for a special, its
	  SpecialPowerType. */
extern Bool LaneWar_abilityDefaultOn( Int commandType, Int powerType );

/// one player's switch of one template's button away from its default
struct LaneWarAbilityToggle
{
	Int player;
	Int templateID;
	Int slot;
	Bool on;
};

/** Set a switch in a list kept sorted by player, template and slot: a switch back to the default drops
	  the entry, so the list holds only what differs. */
extern void LaneWar_setAbilityToggle( std::vector<LaneWarAbilityToggle> &list, Int player, Int templateID, Int slot, Bool on, Bool defaultOn );
/// the switch in the list, or defaultOn where there is none
extern Bool LaneWar_abilityToggleOn( const std::vector<LaneWarAbilityToggle> &list, Int player, Int templateID, Int slot, Bool defaultOn );
/// LANEWAR_ORDER_ABILITY's integers hold together before any template is looked up
extern Bool LaneWar_abilityOrderArgsOk( const Int *args, Int argCount );

enum { LANEWAR_GRID_GROUND = 0, LANEWAR_GRIDS = 1 };
enum { LANEWAR_TRACK_INCOME = 0, LANEWAR_TRACK_TECH = 1 };

/// what a player on that income level is paid on a tick, the middle's bonus on top when his team holds it
extern UnsignedInt LaneWar_incomeAt( Int level, Bool holdsMiddle );
/// what buying that level of a track costs; 0 for a level that cannot be bought
extern Int LaneWar_tierPrice( Int track, Int level );
/// what selling a unit bought for cost gives back, whether or not it has gone out in a wave
inline Int LaneWar_refund( Int cost, Bool sent ) { return sent ? cost * LANEWAR_SELL_SENT_PERCENT / 100 : cost; }

/// slots a side a unit of this footprint radius covers, one at least
extern Int LaneWar_slotsFor( Real radius );

/** Where a slot of a plot lies in the world, the plot standing at x from loX to hiX and y from loY to
	  hiY with its front towards +x (front +1) or -x (front -1): the middle of the size by size block
	  whose front left slot is (col, row).  Columns run across the front from the left as one faces
	  the front, rows back from it, so the same slot is the same place in the wave whichever end the
	  plot stands at. */
inline Coord2D LaneWar_slotPlace( Real loX, Real loY, Real hiX, Real hiY, Real front, Int col, Int row, Int size )
{
	const Real frontX = front > 0.0f ? hiX : loX;
	const Real leftY = front > 0.0f ? hiY : loY;
	Coord2D at;
	at.x = frontX - front * ( row + 0.5f * size ) * LANEWAR_SLOT_SIZE;
	at.y = leftY - front * ( col + 0.5f * size ) * LANEWAR_SLOT_SIZE;
	return at;
}

/// a block of size by size slots at (x, y) stays inside a grid of cols by rows
inline Bool LaneWar_blockFits( Int x, Int y, Int size, Int cols, Int rows )
{
	return x >= 0 && y >= 0 && x + size <= cols && y + size <= rows;
}

/// two square blocks of slots share at least one slot
inline Bool LaneWar_blocksOverlap( Int ax, Int ay, Int aSize, Int bx, Int by, Int bSize )
{
	return ax < bx + bSize && bx < ax + aSize && ay < by + bSize && by < ay + aSize;
}

/// the team (0 A, 1 B) of a 0-based start position
inline Int LaneWar_teamOfStart( Int start ) { return start % LANEWAR_TEAMS; }
/// the seat (0-based) of a 0-based start position
inline Int LaneWar_seatOfStart( Int start ) { return start / LANEWAR_TEAMS; }

/** The seat of a team whose wave goes out on the given tick (counted from 0): the occupied seats
	  take turns in seat order, an empty seat is passed over, so a lone player spawns on every tick.
	  -1 when nobody sits on that team. */
extern Int LaneWar_seatForTick( const Bool occupied[ LANEWAR_SEATS ], UnsignedInt tick );

/** Who holds the middle: the team with more live wave copies on the enemy's side of the centre
	  line, 0 for A and 1 for B, -1 on a tie (no copies at all included). */
extern Int LaneWar_middleHolder( Int pastLineA, Int pastLineB );

/// what one player is paid on a tick
extern UnsignedInt LaneWar_income( Bool holdsMiddle );

/** One unit a player may buy: the template his side or general fields, the tech tier (1 to 3) that
	  unlocks it, and whether it is his one hero. */
struct LaneWarRosterItem
{
	const ThingTemplate *thing;
	Int tier;
	Bool hero;
};

enum { LANEWAR_ROSTER_MAX = 48 };

/** The units the player's side offers in Lane War, tier by tier, each resolved to his general's own
	  template where the general has one (AirF_AmericaInfantryRanger for the Air Force General).  None for a
	  side outside the mode.  The same table on every machine: it reads the player's side and the
	  template store and nothing else. */
extern Int LaneWar_roster( const Player *player, LaneWarRosterItem *out, Int maxItems );

/// the template is the hero of the player's roster
extern Bool LaneWar_isHero( const Player *player, const ThingTemplate *tmpl );

/// the player upgrades the player's side or general sells in Lane War, as LaneWar_roster resolves units
extern Int LaneWar_upgrades( const Player *player, const UpgradeTemplate **out, Int maxItems );

//-------------------------------------------------------------------------------------------------
class LaneWarDirector : public Snapshot
{
public:
	LaneWarDirector( void );

	void reset( void );

	/** Read the map once its objects are in, before the starting buildings would go down.  A new game
		  also gets its money, its alliances by end and its fortresses; a loaded one takes those from
		  the save. */
	void newMap( Bool loadingSaveGame );

	Bool isActive( void ) const { return m_active; }

	/// once a logic frame, from GameLogic::update
	void update( void );

	/// VictoryConditions under VICTORY_LANEWAR: the player's team has lost its fortress
	Bool hasLostFortress( const Player *player ) const;

	/// MSG_LANEWAR_ORDER from TheCommandList: a LaneWarOrderKind and its integers
	void onOrder( Player *player, Int kind, const Int *args, Int argCount );

	/** Buy a unit of the template onto the player's own plot with its block's front left slot at
		  (col, row) on its grid: refused unless the player is
		  seated, the template is on his side's roster, its tier is open to him, it is not a second hero,
		  the block fits inside the grid, no unit of his already covers any of its slots and he can pay
		  for it.  The one entry point for the buy panel, the bot and the scenario verb laneplace. */
	Bool placeUnit( Player *player, const ThingTemplate *tmpl, Int col, Int row );

	/** Why placeUnit would refuse that purchase now, NULL when it would not: the same test, asked
		  without buying, so the HUD's ghost turns red exactly where the logic says no.  `skip` is a plot
		  unit not to count as in the way (one being moved), INVALID_ID for none. */
	const char *whyNotPlace( const Player *player, const ThingTemplate *tmpl, Int col, Int row, ObjectID skip = INVALID_ID ) const;

	/// the player's plot unit covering that slot of that grid, -1 for none (an index into getPlotUnits)
	Int plotUnitAt( const Player *player, Int grid, Int col, Int row ) const;

	/** A plot's corners in the world, for the client's slot grid; FALSE off a lane map.  Read once the
		  map is in: they never change during a match. */
	Bool getPlotBounds( Int team, Int seat, Coord2D *lo, Coord2D *hi ) const;

	struct PlotUnit
	{
		ObjectID id;
		Int team, seat;
		Int grid;												///< LANEWAR_GRID_GROUND
		Int col, row, size;
		Int cost;												///< what was paid for it
		Bool sent;											///< it has gone out in a wave at least once
		Int veterancy;									///< the rank bought for its wave copies, 0 Regular to 3 Heroic; the plot unit itself shows none
		Int veterancyPaid;							///< what those ranks cost
		std::vector<const UpgradeTemplate *> upgrades;	///< its object upgrades in the order bought; the plot unit has them and every copy is given them
		Int upgradesPaid;
		struct Passenger
		{
			ObjectID id;									///< the plot copy, held inside the plot unit
			const ThingTemplate *thing;
			Int cost;
			Int veterancy;
			Int veterancyPaid;
		};
		std::vector<Passenger> passengers;	///< in the order loaded, which is the order a copy loads them
	};
	/// one object upgrade on a plot unit's command set, for the HUD
	struct UnitUpgrade
	{
		Int slot;												///< the button's command set slot, LANEWAR_ORDER_UNIT_UPGRADE's last integer
		const UpgradeTemplate *upgrade;
		Int price;
		Bool owned;
		Bool available;									///< not owned and nothing it conflicts with owned
	};

	// Read only, for the HUD (LaneHud.cpp), which asks at the render's sync point
	Bool seatOf( const Player *player, Int *team, Int *seat ) const;
	Int getIncomeLevel( Int team, Int seat ) const { return m_incomeLevel[ team ][ seat ]; }
	Int getTechLevel( Int team, Int seat ) const { return m_techLevel[ team ][ seat ]; }
	Int getMiddleHolder( void ) const { return m_middleHolder; }
	Bool isHeroBought( Int team, Int seat ) const { return m_heroBought[ team ][ seat ]; }
	/// what the seat is paid on the next tick if the middle stays with whoever holds it now
	UnsignedInt getIncome( Int team, Int seat ) const { return LaneWar_incomeAt( m_incomeLevel[ team ][ seat ], m_middleHolder == team ); }
	Int getSeatPlayer( Int team, Int seat ) const { return m_seatPlayer[ team ][ seat ]; }
	UnsignedInt getTickCount( void ) const { return m_tickCount; }
	UnsignedInt getNextTickFrame( void ) const { return m_nextTickFrame; }
	ObjectID getFortress( Int team ) const { return m_fortress[ team ]; }
	ObjectID getTower( Int team, Int k ) const { return m_tower[ team ][ k ]; }
	Bool isFortressDown( Int team ) const { return m_fortressDown[ team ]; }
	const Coord3D *getSpawn( Int team ) const { return &m_spawn[ team ]; }
	const Coord3D *getMiddle( void ) const { return &m_middle; }
	Real towardEnemy( Int team ) const;
	Int getGridCols( Int team, Int seat, Int grid = LANEWAR_GRID_GROUND ) const { return m_gridCols[ team ][ seat ][ grid ]; }
	Int getGridRows( Int team, Int seat, Int grid = LANEWAR_GRID_GROUND ) const { return m_gridRows[ team ][ seat ][ grid ]; }
	Bool getGridBounds( Int team, Int seat, Int grid, Coord2D *lo, Coord2D *hi ) const;
	Coord3D slotCentre( Int team, Int seat, Int grid, Int col, Int row, Int size ) const;
	const std::vector<PlotUnit> &getPlotUnits( void ) const { return m_plotUnits; }
	Int getCopyCount( void ) const { return (Int)m_copies.size(); }
	ObjectID getCopyID( Int i ) const { return m_copies[ i ].id; }
	/** A fixed-wing jet: an aircraft not made at a helipad and with no helicopter locomotor.  Only a jet
		  strikes from off the map; a helicopter walks with the wave. */
	static Bool isJet( const ThingTemplate *tmpl );
	/// the slots a side a unit of this template covers
	static Int sizeFor( const ThingTemplate *tmpl );
	/// the player's wave units of the template use the button in that slot of its command set (LaneWar_abilitiesOf)
	Bool isAbilityOn( Int player, const ThingTemplate *tmpl, Int slot ) const;
	/// the plot unit's (an index into getPlotUnits) rank and what the next one costs, 0 at Heroic
	Int getVeterancy( Int plotIndex ) const { return m_plotUnits[ plotIndex ].veterancy; }
	Int getNextVeterancyPrice( Int plotIndex ) const { return LaneWar_veterancyPrice( m_plotUnits[ plotIndex ].cost, m_plotUnits[ plotIndex ].veterancy + 1 ); }
	/// the same for one of its passengers (an index into its passengers)
	Int getNextPassengerVeterancyPrice( Int plotIndex, Int passenger ) const;
	/// the object upgrades on the plot unit's command set, owned or not, in slot order
	Int getUnitUpgrades( Int plotIndex, UnitUpgrade *out, Int maxItems ) const;
	/// what selling the plot unit gives back now: the unit, its ranks, its upgrades and every passenger with his ranks
	Int getSellValue( Int plotIndex ) const;
	/** How many the plot unit carries and holds at most, its passengers' slots counted as its container
		  counts them; 0 and 0 for a unit that carries nobody.  A container the logic will still refuse
		  (an Overlord with no bunker takes only its add-on, a Troop Crawler's men cannot fire out) shows
		  room here: the order's log says why. */
	void getPassengerRoom( Int plotIndex, Int *used, Int *most ) const;
	/// the jets flying now, out on a strike or on the way home, for the strip map: where each is and whose; at most `most`
	Int getAirborneJets( Coord3D *where, Int *playerIndex, Int most ) const;

protected:
	virtual void crc( Xfer *xfer );
	virtual void xfer( Xfer *xfer );
	virtual void loadPostProcess( void ) { }

private:
	struct Copy
	{
		ObjectID id;
		UnsignedInt dieFrame;
		Int owner;											///< the player index it last marched for; a hijacked copy marches again for its taker
	};
	enum { JET_PARKED, JET_OUT, JET_HOME };
	/** A wave's jet copy.  Parked, nothing of it is on the map: it is only this entry, so it cannot be
		  seen, picked, hit or see, and it reloads by being made anew.  Out, it flies its strike; home, it
		  flies back off the map to park again. */
	struct Jet
	{
		ObjectID id;										///< INVALID_ID while parked
		const ThingTemplate *thing;
		Int team, seat;
		Int col, row;										///< its plot slot, its place in the line abreast
		Int state;
		UnsignedInt dieFrame;
		UnsignedInt stateFrame;					///< parked: the frame it is rearmed; out: the frame it took off
		Real health;										///< what it had when it last parked, 0 for full
		Int veterancy;
		Int strikes;										///< strikes it has flown
		std::vector<const UpgradeTemplate *> upgrades;	///< its plot unit's object upgrades when it joined, given again at every take-off
	};
	void expireCopies( UnsignedInt now );
	void useAbilities( UnsignedInt now );
	Bool buttonOn( Int player, const ThingTemplate *tmpl, const CommandButton *button ) const;
	Bool walkUp( Object *obj, const CommandButton *button, Int team );
	Bool enteredByAnother( const Object *obj, const Object *target ) const;
	void march( Object *obj, Int team );
	Bool setAbility( Player *player, const Int *args, Int argCount );
	void refillJets( void );
	/// a wave's jet copy waits off the map with its seat's squadron instead of marching
	void joinSquadron( const PlotUnit &u, UnsignedInt now );
	void updateJets( UnsignedInt now );
	void launchJets( Int team, Int seat, UnsignedInt now );
	Bool frontOf( Int team, Real *x, Real *speed ) const;
	Real parkX( Int team ) const;
	Real enemyEndX( Int team ) const;
	Bool heroCopyAlive( const Player *player ) const;
	void tick( UnsignedInt now );
	void logCargo( UnsignedInt now ) const;
	void spawnWave( Int team, Int seat, UnsignedInt now );
	Bool buyTier( Player *player, Int track, Int level );
	Bool sellUnit( Player *player, Int grid, Int col, Int row, Int passenger );
	Bool garrison( Player *player, const Int *args, Int argCount );
	Bool buyUnitUpgrade( Player *player, const Int *args, Int argCount );
	void spawnCargo( Object *copy, const PlotUnit &u, Team *owner, UnsignedInt now );
	Bool moveUnit( Player *player, Int grid, Int fromCol, Int fromRow, Int toCol, Int toRow );
	Bool buyUpgrade( Player *player, Int index );
	Bool buyVeterancy( Player *player, const Int *args, Int argCount );
	Bool readGrid( const char *prefix, Int team, Int seat, Int grid );

	// read from the map on every load, so not saved
	Bool m_active;
	Coord2D m_gridLo[ LANEWAR_TEAMS ][ LANEWAR_SEATS ][ LANEWAR_GRIDS ];		///< a grid's low x, low y corner
	Coord2D m_gridHi[ LANEWAR_TEAMS ][ LANEWAR_SEATS ][ LANEWAR_GRIDS ];
	Int m_gridCols[ LANEWAR_TEAMS ][ LANEWAR_SEATS ][ LANEWAR_GRIDS ];			///< slots across the front, 0 for a grid the map lacks
	Int m_gridRows[ LANEWAR_TEAMS ][ LANEWAR_SEATS ][ LANEWAR_GRIDS ];			///< slots from the front back
	Coord3D m_plotAnchor[ LANEWAR_TEAMS ][ LANEWAR_SEATS ];	///< the ground grid's centre, what a wave copy is placed relative to
	Coord3D m_spawn[ LANEWAR_TEAMS ];
	Coord3D m_middle;

	// saved and CRC'd
	Int m_seatPlayer[ LANEWAR_TEAMS ][ LANEWAR_SEATS ];		///< player index, -1 for an empty seat
	ObjectID m_fortress[ LANEWAR_TEAMS ];
	ObjectID m_tower[ LANEWAR_TEAMS ][ 2 ];								///< LaneWar_TowerA1, A2, B1, B2; INVALID_ID where the map has none
	Bool m_fortressDown[ LANEWAR_TEAMS ];
	UnsignedInt m_nextTickFrame;
	UnsignedInt m_tickCount;
	std::vector<Copy> m_copies;														///< in spawn order, so dieFrame never falls
	std::vector<PlotUnit> m_plotUnits;										///< in purchase order, which is ObjectID order
	Int m_incomeLevel[ LANEWAR_TEAMS ][ LANEWAR_SEATS ];		///< 0 to LANEWAR_INCOME_TIERS, bought
	Int m_techLevel[ LANEWAR_TEAMS ][ LANEWAR_SEATS ];			///< 1 to LANEWAR_TECH_TIERS
	Bool m_heroBought[ LANEWAR_TEAMS ][ LANEWAR_SEATS ];		///< the seat's one hero is bought, once a match
	Int m_middleHolder;																		///< the team the last tick found holding the middle, -1 nobody
	std::vector<LaneWarAbilityToggle> m_abilityToggles;					///< the players' switches away from LaneWar_abilityDefaultOn, sorted
	std::vector<Jet> m_jets;																	///< every seat's squadron, in the order its copies joined
};

extern LaneWarDirector *TheLaneWarDirector;

#endif // __LANEWARDIRECTOR_H_
