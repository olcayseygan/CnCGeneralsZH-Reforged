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
// Modified 2025-2026 by Olcay Seygan for Zero Hour Reforged; see the git history.

////////////////////////////////////////////////////////////////////////////////
//																																						//
//  (c) 2001-2003 Electronic Arts Inc.																				//
//																																						//
////////////////////////////////////////////////////////////////////////////////

// AIPlayer.h
// Computerized opponent
// Author: Michael S. Booth, January 2002

#pragma once

#ifndef _AI_PLAYER_H_
#define _AI_PLAYER_H_

#include "Common/GameMemory.h"
#include "Common/Snapshot.h"
#include "GameLogic/AI.h"			// AISkillLevel, AIRole and the difficulty profile the ladder reads
#include "Common/GameCommon.h"		// MAX_PLAYER_COUNT, for the per-enemy scouting stamps
#include "GameLogic/AIInfluenceMap.h"
#include <map>

enum { INVALID_SKILLSET_SELECTION = -1 };

class BuildListInfo;
// this header used to compile only behind whoever happened to include Team.h first
class TeamPrototype;
class TeamTemplateInfo;
class ProductionUpdateInterface;

/// the skirmish scripts send this team at the enemy at some point, rather than keeping it home on guard
Bool aiTeamAttacks(const TeamTemplateInfo *info);

/**
 * When a team is selected for training, a list of these
 * "work orders" are created, one for each member of the team.
 * This pairs team members with production buildings to keep 
 * track of who is building what, and allows us to track if
 * a building was destroyed while in the process of training a unit.
 */
class WorkOrder : public MemoryPoolObject,
									public Snapshot
{

	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE( WorkOrder, "WorkOrder" )		

public:

	WorkOrder():m_thing(NULL), m_factoryID(INVALID_ID), m_isResourceGatherer(false), m_isScout(false), m_numCompleted(0), m_numRequired(1), m_next(NULL) {};

	Bool isWaitingToBuild( void );		///< return true if nothing is yet building this unit
	void validateFactory( Player *thisPlayer );			///< verify factoryID still refers to an active object

public:

	const ThingTemplate *m_thing;			///< thing to build
	ObjectID m_factoryID;							///< ID of object that is building this, or zero if no-one is
	WorkOrder *m_next;
	Int			m_numCompleted;					  ///< Number built.
	Int			m_numRequired;					  ///< Number needed.
	Bool		m_required;								///< True if part of minimum requirement.
	Bool		m_isResourceGatherer;			///< True if resource gatherer.
	Bool		m_isScout;								///< True if this is the one unit kept for looking at the map.

protected:

	// snapshot methods
	virtual void crc( Xfer *xfer );
	virtual void xfer( Xfer *xfer );
	virtual void loadPostProcess( void );

};

inline Bool WorkOrder::isWaitingToBuild( void )
{
	if (m_factoryID!=INVALID_ID)
		return false;
	if (m_numCompleted >= m_numRequired)
		return false;
	return true;
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
class TeamInQueue : public MemoryPoolObject,
										public Snapshot
{

	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE( TeamInQueue, "TeamInQueue"  )

private:		

	MAKE_DLINK(TeamInQueue, TeamBuildQueue)				///< the instances of our prototype
	MAKE_DLINK(TeamInQueue, TeamReadyQueue)				///< the instances of our prototype

protected:

	// snapshot methods
	virtual void crc( Xfer *xfer );
	virtual void xfer( Xfer *xfer );
	virtual void loadPostProcess( void );

public:

	TeamInQueue() : 
		m_workOrders(NULL), 
		m_team(NULL), 
		m_nextTeamInQueue(NULL), 
		m_sentToStartLocation(false), 
		m_reinforcement(false), 
		m_stopQueueing(false),
		m_reinforcementID(INVALID_ID),
		//Added By Sadullah Nader
		//Initialization(s) inserted
		m_frameStarted(0),
		m_priorityBuild(FALSE)
		//		
	{
	}

	Bool isAllBuilt( void );				///< Returns true if the team is finished building.
	Bool isBuildTimeExpired( void );///< Returns true if the team has run out of build time.
	Bool isMinimumBuilt( void );		///< Returns true if the team has started building at least the minimum number of units.
	Bool includesADozer( void );		///< Returns true if the team includes a dozer unit.
	Bool areBuildsComplete( void );	///< Returns true if all units in factories have finished building.
	void disband( void );						///< Disbands the team (moves units into the default team).
	void stopQueueing(void) {m_stopQueueing=true;} ///< Stops building new units, just finishes current.

public:

	WorkOrder *m_workOrders;				///< list of work orders
	Bool m_priorityBuild;						///< True if the team is specifically requested.
	Team *m_team;										///< the team that units built by the m_workOrders go into
	TeamInQueue *m_nextTeamInQueue; ///< next
	Int	m_frameStarted;							///< Frame we started building.
	Bool m_sentToStartLocation;			///< Has it been sent to it's start location?
	Bool m_stopQueueing;						///< True if we are to quit queueing units (usually because we ran out of build time.)
	Bool m_reinforcement;						///< True if it is a unit to reinforce an existing team.
	ObjectID m_reinforcementID;			///< True if it is a unit to reinforce an existing team.

};


/**
 * The computer-controlled opponent.
 */
class AIPlayer : public MemoryPoolObject,
								 public Snapshot
{
	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE( AIPlayer, "AIPlayer"  )		

public:

	AIPlayer( Player *p );							///< constructor
	
	virtual Bool computeSuperweaponTarget(const SpecialPowerTemplate *power, Coord3D *pos, Int playerNdx, Real weaponRadius); ///< Calculates best pos for weapon given radius.

	/** What every computer player spent this logic frame, by job, plus whichever single one cost
		* the most.  The slow-frame report in GameLogic.cpp could only say "players 16.2ms", which is
		* enough to know a stutter is in here and not enough to do anything about it.  Reset once a
		* frame by AI::update; empty outside a logging build. */
	static void resetFrameProfile( void );
	static const char *getProfileReport( void );

	/** Empty the unit-against-unit tables from the last match and, when a skirmish AI plays this one,
		* fill them for every unit the sides in it can build, so the first team choice does not pay for
		* them mid-match.  Called by GameLogic::startNewGame once the map's INI and the players are in. */
	static void prepareMatchupTables( void );

	/** Base building is the expensive job once the expansion convoy is out of the way, and it is a
		* loop over the build list that calls several searches per entry. These name which one.
		* AISkirmishPlayer overrides processBaseBuilding, so both copies are instrumented and the
		* timers have to be reachable from either file. */
	enum BaseSubPhase
	{
		BASE_SUB_HOLE = 0,		///< scanning every object in the game for a GLA rebuild hole
		BASE_SUB_DOZERFIX,		///< a building under construction whose dozer went missing
		BASE_SUB_SAFE,				///< isLocationSafe for one build list entry
		BASE_SUB_FINDDOZER,		///< findDozer for one build list entry
		BASE_SUB_CANMAKE,			///< canMakeUnit for one build list entry
		BASE_SUB_BUILD,				///< actually placing the one building this pass will start
		BASE_SUB_COUNT
	};
	static void profileBaseSubBegin( void );
	static void profileBaseSubEnd( Int slot );
	static void profileBaseSubCount( Int slot );

public: // AIPlayer interface, may be overridden by AISkirmishPlayer.  jba.

	virtual void update();											///< simulates the behavior of a player

	virtual void newMap();											///< New map loaded call.

	/// Invoked when a unit I am training comes into existence
	virtual void onUnitProduced( Object *factory, Object *unit );

	/// Invoked when a structure I am building comes into existence
	virtual void onStructureProduced( Object *factory, Object *structure );

	virtual void buildSpecificAITeam(TeamPrototype *teamProto, Bool priorityBuild); ///< Builds this team immediately.

	virtual void buildAIBaseDefense(Bool flank); ///< Builds base defense on front or flank of base.

	virtual void buildAIBaseDefenseStructure(const AsciiString &thingName, Bool flank); ///< Builds base defense on front or flank of base.

	virtual void buildSpecificAIBuilding(const AsciiString &thingName); ///< Builds this building as soon as possible.


	virtual void recruitSpecificAITeam(TeamPrototype *teamProto, Real recruitRadius); ///< Builds this team immediately.

	virtual Bool isSkirmishAI(void) {return false;}
	/** A skirmish AI that plays the fork's own game beside its scripts: economy, base, army, waves and
		* tactics.  Not one whose map wrote those scripts itself (an Art of Defense map's waves), which is
		* left to them the way a Challenge or mission opponent is, and not one on a player's own map
		* (SidesList::forkAIMap), which plays EA's AI.  Every fork AI rule keys on this, never on
		* isSkirmishAI() or the player type. */
	Bool runsOwnAI(void);
	/** Whose eyes this AI reads another player's estate through: its own index on the fork's AI, so
		* it knows only what it has scouted; -1 on EA's, which knew the whole map and whose map scripts
		* were written for that.  Pass it as the observerNdx below. */
	Int knowledgeObserver(void);
	virtual Player *getAiEnemy(void) {return NULL;}	///< Solo AI attacks based on scripting.  Only skirmish auto-acquires an enemy at this point.  jba.
	virtual Bool checkBridges(Object *unit, Waypoint *way) {return false;}
	/** Center, Flank or Backdoor: the approach with the least enemy firepower this AI knows about.
		* The script's own label when the rung does not read the map, or the label is not one of the three. */
	AsciiString chooseApproachLabel(const Coord3D *from, const AsciiString &requested, Int pathSuffix);
	/** C2: park an attack team that is not a wave on its own until the rest of the wave is in hand.
		* TRUE when it is parked, and the caller's own order must not go out.  logRefusal writes the AI
		* ENGAGE line for a team that goes on its own instead; a caller that logs its own passes FALSE. */
	Bool holdTeamForWave(Team *team, const AsciiString &approach, Int pathSuffix, Bool logRefusal = TRUE);
	/** A script sends this team hunting or at an area: logged, and with EngageGate a team still at
		* home parks for the next wave instead.  TRUE when it is parked, and the script's order must not go out. */
	Bool gateTeamAttack(Team *team, const char *cause);
	void onUnitLost(const Object *obj);		///< one of ours died: was it alone, away from a team that is still alive?
	virtual void repairStructure(ObjectID structure);

	virtual void selectSkillset(Int skillset);

public:
	Bool getBaseCenter(Coord3D *pos) const {*pos = m_baseCenter; return m_baseCenterSet;}
	/// Difficulty level for this player.
	GameDifficulty getAIDifficulty(void) const;
	void setAIDifficulty(GameDifficulty difficulty) {m_difficulty = difficulty;}

	/** Which rung of the six-step ladder this AI plays at, and what it is trying to do.  Two
		* independent axes: the rung says how well, the role says what.  See AI-ROADMAP.md D6/D8. */
	AISkillLevel getAISkillLevel(void) const {return m_skillLevel;}
	void setAISkillLevel(AISkillLevel level) {m_skillLevel = level;}
	AIRole getAIRole(void) const {return m_role;}

	/** The third axis, how it plays: set once from the seat at the start of the match, after the role is
		* rolled, which a personality with a role of its own then overrides.  RANDOM draws one of the others
		* from the logic random stream here, and only then. */
	void setAIPersonality(AIPersonality persona);
	AIPersonality getAIPersonality(void) const {return m_persona;}
	Bool superweaponIsPlan(void) const;	///< a Tech AI, or a GLA Air AI whose SCUD Storm stands in for the air force: its first superweapon is rushed
	void noteTeamRequest(AITeamRequestKind kind, Int from, Int enemy, const Coord3D *pos);	///< an ally asks (aiPostTeamRequest)
	void noteStrikeLaunched(const Player *launcher);	///< a superweapon went up, the word every seat hears: who fired, not where (dodgeStrike)
	/// HEADLESS PERSONA: one line of what this seat's personality did over the match, for ai-batch
	void logPersonaSummary(void);

	/// This rung's knobs, for any behaviour that has a difficulty-dependent decision to make.
	const AIDifficultyProfile *getSkillProfile(void) const;

	/// What a seat that predates the ladder (an old save, a replay, a script) plays at.
	static AISkillLevel skillLevelForDifficulty(GameDifficulty difficulty);
	void buildBySupplies(Int minimumCash, const AsciiString &thingName, Bool holdableOnly = FALSE ); ///< Builds a building by supplies. holdableOnly is the AI's own expansion: see findSupplyCenter, and no room beside the dock means no building
	void buildSpecificBuildingNearestTeam( const AsciiString &thingName, const Team *team );
	void buildUpgrade(const AsciiString &upgrade ); ///< Builds an upgrade.
	/// A team is about to be destroyed.
	void aiPreTeamDestroy( const Team *team );
	/// Is the nearest supply source safe?
 	Bool isSupplySourceSafe( Int minSupplies );
	/// Is a supply source attacked?
	Bool isSupplySourceAttacked( void );

	Bool isLocationSafe( const Coord3D *pos, const ThingTemplate *tthing);

	/// Have the team guard a supply center.
	void guardSupplyCenter( Team *team, Int minSupplies );

	void setTeamDelaySeconds(Int delay) {m_teamSeconds = delay;}

	/// Calculates the closest construction zone location based on a template.
	Bool calcClosestConstructionZoneLocation( const ThingTemplate *constructTemplate, Coord3D *location );

protected:

	// snapshot methods
	virtual void crc( Xfer *xfer );
	virtual void xfer( Xfer *xfer );
	virtual void loadPostProcess( void );

	/** Look at the map.  There was no concept of scouting in the AI at all - it did not need one
		* while every strategic decision read the enemy's object list straight out of the game (see
		* the observer index on getPlayerStructureBounds).  One cheap unit, replaced when it dies,
		* touring the enemy start positions: that is what turns fog back into information. */
	/** Break off a fight that is being lost.  The word "retreat" did not appear anywhere in the AI:
		* teams fought to the last man, which is the single most visible thing that made it look
		* stupid.  Two levels, because pulling out only whole teams still loses the units that were
		* individually finished - Sins does exactly that, and keeps losing capital ships for it. */
	/** B3: decide to expand, instead of waiting for a script to say so.  buildBySupplies is a
		* working expansion mechanism that only ever ran when the "Build supply center" script action
		* fired - the AI never worked out for itself that it was running out of money. */
	virtual void doExpansion(void);

	virtual void doRetreats(void);

	/** C2: hold a finished team at the rally point until there is a force worth sending.  A string of
		* small waves is free veterancy for the other side. */
	Bool shouldHoldForMassing( TeamInQueue *team );

	virtual void doScouting(void);
	Object *findScout(void);						///< a spare unit of ours that can do the touring, not already scouting
	void queueScout(void);							///< ... or build the cheapest one that can
	Bool scoutInQueue(void);						///< one is already on order
	Bool nextScoutTarget(Int slot, const Coord3D *from, Coord3D *pos);	///< the stalest enemy start position worth the walk
	void updateStartIntel(void);				///< cross off the start positions the scouts have looked at, and deduce the rest
	void countStartIntel(Int *enemies, Int *occupied, Int *unchecked) const;	///< the three numbers the odds are made of
	Bool enemyStartGuess(Int playerNdx, Coord3D *pos);	///< where this enemy is, or the best address we have for him

	/** B?: an oil derrick is income standing in the open.  The AI used to walk past the neutral ones
		* and shoot the enemy's, which is the one thing you can do with a derrick that earns nothing. */
	virtual void doCapture(void);
	Object *findCapturer(void);					///< a unit of ours that can walk into a building and own it
	Object *nearestTechBuilding(const Coord3D *from, Bool wantOurs);	///< the closest one worth taking, or worth sitting on
	void queueCapturer(void);						///< ... or build the cheapest thing that can take one
	void queueSupportUnit(const ThingTemplate *tmpl, const char *what);	///< one cheap unit, outside the team system

	/** The enemy's tanks are worth more taken than shot: one hijacker walks off with one, and Black
		* Lotus shuts one down for nothing.  Neither had ever been used by a computer player. */
	virtual void doHijack(void);
	Object *findHijacker(void);					///< a unit of ours that takes a vehicle by walking into it
	Object *nearestStealableVehicle(const Coord3D *from, Real reach);	///< the closest enemy vehicle in sight; reach <= 0 is the map

	/** Past the hoard, buy what the build list never had.  A tank factory or a barracks when that
		* queue is backing up, an airfield with no count cap, income buildings the same way, and money
		* units (China's hackers) from any factory with room in its queue. */
	virtual void doEconomy(void);

	/** Buy a power plant before the margin runs out rather than after, and put it on the far side of
		* the base from whoever this player is fighting. */
	virtual void doPower(void);

	/** Hard: the superweapon as soon as it can be bought, and the tech building it waits on before
		* that, with no clock; as many superweapons as the game's rules allow, and a few copies of the
		* tech building so one of them blowing up leaves the tree standing. */
	virtual void doSuperweapons(void);
	void doParticleBeams(void);	///< drive a firing Particle Cannon's beam onto the richest ground within reach of it
	Bool richestGroundNear(const Coord3D *spot, Real radius, Coord3D *aim);	///< that ground, FALSE when there is nothing worth moving to

	/** The purchase queue (AIPurchaseQueue): every skirmish spender asks before it pays, on every rung
		* with a bank target, and the thing at the top that cannot be afforded yet has the money saved for
		* it.  Off, every ask is yes and the engine's own money check decides, as before. */
	Bool purchaseQueueOn(void);

	/** The spend rule, on every rung with a bank target.  Measures the income, and with the bank past
		* aiBankTarget spends what is over it: upgrades at half the payoff, extra units from idle factories
		* within aiSurplusBudget.  Another factory of any kind whose every factory stays busy, whatever the
		* bank.  Upgrades that pay for the army are bought below the target as well, through the purchase
		* queue. */
	void doSpending(void);
	Int bankTarget(void);																	///< what the bank keeps, from the income measured last
	void buyUpgradesForArmy(Bool overTarget);								///< the player upgrade the standing army gets the most out of
	void addProductionForQueues(void);											///< another factory of each kind whose every factory has stayed busy
	void trainSurplus(Int budget);												///< a unit from each idle factory, within budget
	void trainFavoured(void);															///< a personality's favoured units, below the bank target as well
	void buyPersonaTransports(void);											///< an Infantry AI's ground transports, a few by rung
	void sendPersonaTransports(AIGroup *wave);	///< ... driven loaded to a wave's front, riders put down there
	Int					m_incomeMark;						///< money earned in all, when the income was last measured ...
	UnsignedInt	m_incomeMarkFrame;			///< ... and the frame, 0 for never
	Int					m_bankMark;							///< ... and the bank then
	Real				m_incomePerSecond;			///< smoothed over the windows measured so far
	Int					m_startFortune;					///< what the match started with over the default starting cash, 0 at the default
	UnsignedInt	m_factoryFullHistory[ 3 ];	///< per war factory, barracks, airfield: a bit a spend pass, newest lowest, set when every finished one was full (busy, or an airfield's parking) (aiSampleFactories)
	Int					m_factoryFullCount[ 3 ];		///< ... and how many passes that holds since one of the kind last went up
	std::vector<ObjectID> m_producedSincePass;	///< the barracks and war factories that let a unit out since the last spend pass (onUnitProduced)
	UnsignedInt	m_parkingBlockedFrame;					///< the last frame a buy of an aircraft was refused because the airfield's parking was full, 0 for never
	std::vector<AISiteWatch>	m_siteWatches;		///< the unfinished buildings this AI placed, in the order first seen
	std::vector<AICooledSpot>	m_cooledSpots;		///< spots of sites it gave up on, left alone for a while
	Bool watchSite(BuildListInfo *info, Object *bldg);			///< cancel and refund a site it cannot finish; TRUE when it did
	Bool spotCooling(const Coord3D *pos);										///< a site was given up near here lately
	void stageDozer(Object *dozer, const Coord3D *site);		///< an idle dozer walks ahead to a site waiting on money
	Bool purchaseAllowed(Int key, Int cost, AIPurchaseKind kind, Bool startable, const AsciiString &what, Int scoreOffset = 0);
	void purchased(Int key, Int cost, AIPurchaseKind kind, const AsciiString &what);
	Bool buyUnit(ProductionUpdateInterface *pu, const ThingTemplate *unit, AIPurchaseKind kind);	///< queueCreateUnit through the queue
	Bool buyUpgrade(ProductionUpdateInterface *pu, const UpgradeTemplate *upgrade, Int scoreOffset = 0);	///< queueUpgrade through the queue
	AIPurchaseKind purchaseKindOf(const ThingTemplate *tmpl) const;
	AIPurchaseQueue	m_purchases;
	AsciiString			m_purchaseSavingFor;	///< the reserved item's name, for the log only

	/** Fire every ready special power whose template says AIFiresWhenReady, once its science is bought. */
	void doReadySpecialPowers(void);

	Bool enemyDirection(Coord3D *dir);	///< unit vector from this base towards the nearest enemy's best known address
	Bool isHeldExpansion(const Object *warehouse);	///< our supply center stands at it, and it is nearer our base than any enemy's
	Bool isOurSideOfMap(const Coord3D *pos);	///< no nearer any living enemy's base than ours
	Bool buildExpansionDefense(void);	///< this side's first base defense at the dock chosen last; true while one is on the build list and buildable
	void doTunnels(Object *dozer);	///< a tunnel at home, at the held expansion and far out on the next wave's road
	void buildAsap(const ThingTemplate *tmpl);	///< the plan's own unbuilt entry if it has one, otherwise a new spot behind the base

	void buyMoneyUnits(void);
	Int moneyUnitRoom(void) const;	///< how many more money units this player's army and internet centers carry
	Bool hasEnoughMoneyUnitsFor(TeamPrototype *proto) const;	///< this team is all hackers and there is no room for more
	Bool placeNear(const ThingTemplate *tmpl, const Coord3D *center, Real innerRadius, Bool walkOutward);	///< the nearest legal, safe spot by the ground from center, queued for a dozer. walkOutward keeps searching further out as the near ground fills
	Bool queueExtraFactory(Object *dozer, KindOfType kind);	///< one more of this factory, beside a held expansion or around the base, once the ones standing fall behind their queues
	Bool holdsExtraFactory(const ThingTemplate *tmpl);	///< a factory of a kind already standing or going up, while the ones standing keep up with their queues
	const ThingTemplate *nextBaseDefense(Object *dozer);	///< the base defence off this dozer's buttons that the base has fewest of, NULL for none
	Bool placeDefense(const ThingTemplate *defense);	///< queue it on the ring spot whose clear field of fire covers the most ground the enemy drives in over; a Turtle's round a choke first
	Bool placeDefenseAround(const ThingTemplate *defense, const Coord3D *choke);	///< placeDefense's search, on rings round the base center, or round choke when it is set
	Int possibleEnemyStarts(Coord3D *starts, Int *known);	///< starts a scout found an enemy at, then while one is unplaced every start not looked at
	Int enemyStarts(Coord3D *threats);	///< enemyStartGuess of every enemy with anything left, at most MAX_PLAYER_COUNT
	const std::vector<Coord3D> &turtleChokes(void);	///< the narrowest place on the road in from each start an enemy may be at, nearest first, cached until the base or the starts change
	Int guardedChokes(void);	///< how many of turtleChokes this rung fortifies
	Real nearestChokeDistance(const Coord3D *pos);	///< straight-line distance to the nearest guarded choke, negative when there is none
	void queueTurtleLine(void);	///< the Turtle's defence pass, on every rung, before the hoard and not while the base is hit
	void queueBaseDefenses(Object *dozer);	///< the next defences the allowance has room for, off this dozer's buttons, each on its best spot
	void fortifyChokes(Object *dozer);	///< a Turtle's mines on the guns at its chokes and demo traps in front of them
	const std::vector<UnsignedShort> &groundFromBase(void);	///< AIGroundFlood distance of every pathfind cell from the base center, cached until the center moves
	const std::vector<UnsignedShort> &groundFromThreats(const Coord3D *threats, Int count);	///< the same from the nearest of these, cached until they change
	Bool reachableFromBase(const Coord3D *pos);	///< a vehicle at the base center could drive there; TRUE before the base is placed
	Real knownFirepowerAlongPath(Waypoint *way);	///< what this AI has seen that can shoot, along an approach
	AsciiString secondApproachLabel(const Coord3D *from, const AsciiString &taken, Int pathSuffix);	///< the quietest other road, or empty
	Bool loadGunships(void);	///< infantry boards the transports it can shoot out of: the wave's anything at home, a team's its own; TRUE while a firing gunship is still filling
	Int buyGunshipRiders(Int freeSeats);	///< Medium and up train the men for the firing seats nobody fills; how many are in training
	void garrisonBuildings(void);	///< infantry into our own buildings it shoots out of, and into empty ones with an enemy near, at home and at the front
	void evacuateBuildings(void);	///< our men out of a building about to fall, and out of a neutral one once no enemy is near
	void buyGunshipChinook(void);	///< Medium and up buy a supply-center transport the riders shoot out of, as a gunship
	void doShuttles(void);	///< the transport Chinooks load at home, fly the wave's ground units to its road and come back
	void buyTransportChinook(void);	///< one more transport Chinook while the last wave needs more lift than there is
	void lendDutyChinook(void);	///< a duty Chinook gathers only while no gatherer can exist otherwise
	void computeShuttleFront(Waypoint *way, AIGroup *wave);	///< where the transport Chinooks put this wave's units down
	Int loadShuttle(Object *ship);	///< call the attack teams' ground units at home into one Chinook; how many were called
	Bool flightIsQuiet(const Coord3D *from, Real x, Real y) const;	///< a straight flight crosses nothing the AI has seen shoot
	void doHelixes(void);	///< China's Helixes: the upgrade the enemy army calls for, healers behind the wave, bomb raids
	void buyDutyHelix(void);	///< Medium and up buy the healers and raiders their Helixes' buttons allow
	Int helixRoleWanted(const ThingTemplate *tmpl) const;	///< the job a Helix of this kind would take now, -1 for none
	void takeDutyHelix(Object *helix);	///< a Helix buyDutyHelix ordered comes out and takes its job
	void doAmbulances(void);	///< the USA's Ambulances: bought once there is an army, kept behind it, taking in the hurt and cleaning our side
	void steerAmbulance(Int slot, const std::vector<AIKnownGun> &guns);	///< one Ambulance's job this pass
	void upgradeHelix(Object *helix, Int role, AIEnemyComposition *enemy, Bool *enemyRead);	///< the upgrade this Helix's job or the enemy army calls for
	void collectKnownGuns(std::vector<AIKnownGun> *guns) const;	///< every enemy gun this AI knows of, in object list order
	void steerHealer(Int slot, const std::vector<AIKnownGun> &guns);	///< a healer over our hurt behind the line, clear of every known gun
	void flyRaid(Int slot, const std::vector<AIKnownGun> &guns);	///< a raider's bomb run on an enemy building, in and out the quiet side
	Bool pickRaidTarget(const Object *raider, const std::vector<AIKnownGun> &guns, Object **target, Coord3D *entry, Int *gunsOnRun) const;
	Coord3D rearOf(const Coord3D *from, const std::vector<AIKnownGun> &guns, Real margin) const;	///< the nearest point toward home no known gun reaches by margin
	Bool isGunshipRider(const Object *obj) const;	///< infantry loadGunships may put in a transport
	void sendIdleAttackTeams(void);	///< attack teams standing at home join the next wave instead of waiting for the script's signal
	Real addHomeStrays(AIGroup *wave) const;	///< the default team's fighters idle at home go with the wave
	Real knownFirepowerNear(const Coord3D *pos);	///< what this AI has seen that can shoot, near a point
	Bool forwardHoldPoint(const AsciiString &approach, Int pathSuffix, const Coord3D *enemyPos, Coord3D *hold);	///< where a wave gathers on its road
	void sendWave(AIGroup *wave, const AsciiString &approach, Int pathSuffix, Int teams, Real power, UnsignedInt heldFrames, const char *cause);
	void logEngage(const char *cause, const std::vector<Object *> &units);	///< AI ENGAGE: what goes out, and how it weighs against the army it remembers
	Bool refuseHold(Team *team, const char *why, Bool logIt);	///< FALSE, after logging the team holdTeamForWave lets go
	Bool holdReinforcement(Object *obj);	///< a top-up for a team out on the map waits for the next wave instead of walking out alone
	void rememberEnemies(void);					///< bring the seen-enemy ledger up to date with what is in sight now
	void rememberedArmy(std::vector<AIVisibleEnemy> *army) const;	///< the ledger, one entry a kind, weighed by combat power
	void sendWaveThroughTunnels(AIGroup *wave, const Coord3D *center, Waypoint *way);	///< whoever can goes by tunnel when the path is the long way round

	virtual void doBaseBuilding(void);
	virtual void checkReadyTeams(void);
	virtual void checkQueuedTeams(void);
	virtual void doTeamBuilding(void);
	virtual void doUpgradesAndSkills(void);
	virtual Object *findDozer(const Coord3D *pos);
	Object *findNearestDozer(const Coord3D *pos);	///< the nearest dozer, busy or not
	Bool isPowerThin(void) const;	///< less than one more building's draw left over
	virtual void queueDozer(void);
	void computeEnemyComposition( AIEnemyComposition *out, std::vector<AIVisibleEnemy> *army = NULL );	///< what this AI can see the enemy fielding, and optionally which units
	Real visibleEstateValue( Int playerNdx );					///< what this AI can see that player is worth, in build cost
	virtual Bool selectTeamToBuild( void );			///< determine the next team to build
	virtual Bool selectTeamToReinforce( Int minPriority );			///< determine the next team to reinforce
	virtual Bool startTraining( WorkOrder *order, Bool busyOK, AsciiString teamName);	///< find a production building that can handle the order, and start building
	virtual Bool isAGoodIdeaToBuildTeam( TeamPrototype *proto );		///< return true if team should be built
	virtual void processBaseBuilding( void );		///< do base-building behaviors
	virtual void processTeamBuilding( void );		///< do team-building behaviors
 	/** These two read another player's estate.  observerNdx is who is allowed to know: pass a
 		* player index and only what that player can see (or, for a building, has ever seen) is
 		* counted; -1 keeps the omniscient answer for a caller that is not a player's own thinking. */
 	static Int getPlayerSuperweaponValue( Coord3D *center, Int playerNdx, Real radius, Bool includeMilitaryUnits = TRUE, Int observerNdx = -1,
																				Real economyWeight = 0.0f, Real threatWeight = 0.0f );
// End of aiplayer interface. 

protected:

	MAKE_DLINK_HEAD(TeamInQueue, TeamBuildQueue);		///< List of teams being build
	MAKE_DLINK_HEAD(TeamInQueue, TeamReadyQueue);		///< List of teams built, waiting to reach rally point.

protected:
	Int computeStructureDelay( void );	///< frames to wait before trying the next structure
	Int computeTeamDelay( void );				///< frames to wait before trying the next team
	Int hoardThreshold( void );					///< bank that speeds the two delays up; skirmish AI only

public:
	/// the arithmetic behind the two above, free of the Player so a test can reach it
	static Int computeBuildDelay( Real seconds, Int money, Int poorAt, Int wealthyAt,
																Real poorMod, Real wealthyMod, Real rateScale );

	/**
		Where in a repeating check's cycle this player sits.  Every AIPlayer arms the same timers
		with the same constants on the same frame, so a lobby full of bots does its base building,
		team building and bridge repair in lockstep for the whole match and the cost of all of them
		lands on one logic frame.  Spreading them over the cycle changes *when* each one runs, not
		how often, and it is a function of the player index alone - the simulation stays deterministic.
	*/
	static Int computeUpdatePhase( Int playerIndex, Int cycleFrames );

	Bool isGunshipChinook( const Object *obj ) const;	///< a Chinook this AI bought to carry riders, not to gather
	Bool isGunshipAircraft( const Object *obj ) const;	///< a helicopter, or one of those Chinooks
	Bool isTransportChinook( const Object *obj ) const;	///< a plain Chinook this AI bought to fly its wave, not to gather
	Bool isDutyChinook( const Object *obj ) const;	///< either of those, which the gatherer counts leave out
	Bool isDutyHelix( const Object *obj ) const;	///< a Helix this AI bought to heal or to raid, which nothing else gives orders
	Bool isDutyAmbulance( const Object *obj ) const;	///< an Ambulance this AI bought to follow the army, which nothing else gives orders
	Bool isWalkingToAmbulance( const Object *obj ) const;	///< a patient on its way into a duty Ambulance, which nothing else gives orders either
	Bool isOutToMend( const Object *obj ) const;	///< a pulled-out veteran or a patient, out of its team until releaseMended frees it
protected:

	/**
		How much faster than the AIData delays this AI works.  1 = exactly as the data says.
		The skirmish AI overrides it: it is meant to react faster than a scripted campaign AI,
		and used to get that by clamping its own timers, which threw the data away.
	*/
	virtual Real getBuildRateScale( void ) { return 1.0f; }

	Bool isPossibleToBuildTeam( TeamPrototype *proto, Bool requireIdleFactory, Bool &needMoney );		///< return true if team can be considered for building
	Object *buildStructureNow(const ThingTemplate *bldgPlan, BuildListInfo *info );		///< Build a base buiding.
	Object *buildStructureWithDozer(const ThingTemplate *bldgPlan, BuildListInfo *info );		///< Build a base buiding.
	void clearTeamsInQueue( void );			///< Delete all teams in the build queue.
	void computeCenterAndRadiusOfBase(Coord3D *center, Real *radius);
	Object *findFactory(const ThingTemplate *thing, Bool busyOK); ///< Find a factory to build a unit.  If force is true, may return a busy factory.
	void sendForSpotters( TeamInQueue *team, const Object *gun );	///< a gun that fires past its own sight came out for this team: train its spotters
	Bool replaceSpotter( void );				///< a team in the field has a gun that fires past its own sight and no spotter left: train it one
	void queueUnits( void );						///< Check the team build list, & queue up units at any idle factories.
	void checkForSupplyCenter( BuildListInfo *info, Object *bldg);
 	void queueSupplyTruck(void);
	void updateBridgeRepair(void);
	Bool dozerInQueue(void);
	Object *findSupplyCenter(Int minSupplies, Bool holdableOnly = FALSE);	///< holdableOnly: within reach of home, on our side of the map, no enemy at it
	void getPlayerStructureBounds(Region2D *bounds, Int playerNdx, Bool conservative = FALSE, Int observerNdx = -1 );

	/// what a superweapon aimed here is worth, with the shots already on their way taken off
	Int superweaponScore( Coord3D *center, Int playerNdx, Real radius, Bool targetMilitaryUnits );
	Real strikeEconomyWeight( void );	///< the rung's m_strikeEconomyWeight for a skirmish AI, 0 for a campaign one
	Real strikeThreatWeight( void );	///< the rung's m_strikeThreatWeight for a skirmish AI, 0 for a campaign one
	void noteSuperweaponAim( const Coord3D *pos );	///< remember a spot, so the next shot goes elsewhere

protected:

	Player *m_player;									///< the Player we represent

	AISkillLevel m_skillLevel;				///< rung of the ladder: how well this AI plays
	mutable AIDifficultyProfile m_measuredProfile;	///< the rung's profile less the knobs -aiknobsoff turns off for this slot
	AIRole		m_role;									///< what it is trying to do; rolled once, kept for the match

	// the personality (AIPersonality) and what it keeps; nothing here is read on a Normal seat
	const AIPersonaWeights &personaWeights(void) const;	///< its row, the Attacker's for a Rusher whose rush is out
	AIPersonaWeights personaTaste(void) const;		///< ... with an Air one's taste cut by the enemy's known anti-air, or turned to vehicles on a side with no airfield
	Int personaShareKind(void) const;	///< the kind (SHARE_* in AIPlayer.cpp) whose share of what it builds this personality is about, -1 for none
	AIPersonaWeights personaKindTaste(void) const;	///< the row, turned to vehicles for an Air AI on a side with no airfield
	Bool sideFlies(void) const;									///< this side's dozers can put up an airfield at all
	Int favouredFactoryScore(const ThingTemplate *tmpl) const;	///< what a personality adds to the purchase score of a factory of its favoured kind, 0 for any other building
	Bool isKindPersona(void) const;		///< an Air, Infantry or Armor AI, whose army is its favoured kind
	Int personaBuildingScore(const ThingTemplate *tmpl) const;	///< ... and of any building: that, or an Air AI's first few anti-air defences
	Real wavePower(void) const;										///< what a parked wave waits for
	UnsignedInt waveMaxHoldFrames(void) const;		///< ... and for how long at most
	Bool rushPending(void) const;									///< a Rusher whose one big attack has not gone yet
	UnsignedInt rushFrame(void) const;						///< when it goes, by rung
	void personaChat(const char *trigger);				///< a line of chat in character, rate limited; display only
	void notePersonaProgress(void);								///< the summary's superweapon frame and the chat's beaten enemies
	void doPersonaRaids(void);										///< Raider: fast units at the enemy's gatherers, workers and depots
	void huntGatherers(void);										///< raiders out of the base turn on the nearest known gatherer near them
	void personaSortie(void);										///< Attacker and Rusher on a rung that does not mass: the rush and the regular sorties of what idles at home
	void defendAlly(void);												///< Supporter: idle units at home go to an ally's base when it is hit
	void postTeamRequests(void);									///< what this AI asks of its allies, every wave tick
	void answerTeamRequests(void);								///< Air, Attacker and Rusher answer an ally's call for help
	const AITeamRequest *freshRequest(AITeamRequestKind kind, UnsignedInt within) const;	///< the request of that kind posted inside within frames, NULL for none
	void coopChat(const char *trigger);					///< one line to the team when answering, GUI:AICoop<trigger>
	AITeamRequest	m_inbox[ AIREQ_COUNT ];				///< the latest request of each kind from an ally
	UnsignedInt		m_answeredFrame;							///< the frame of the last call answerTeamRequests answered
	void shareSurplusWithAllies(Int bank, Int target);	///< Economist: who gets what out of the bank (the hook only)
	AsciiString sneakyLane(const Coord3D *from, const AsciiString &requested, Int pathSuffix);	///< Sneaky: the back door or the flank

	AIPersonality	m_persona;
	Bool					m_personaFromRandom;	///< the seat said Random and this was drawn
	Bool					m_rushSent;
	Bool					m_techDone;						///< a Tech AI's first superweapon stands: from then on it plays the Normal row
	UnsignedInt		m_raidFrame;					///< the last Raider sortie
	enum { MAX_PERSONA_RAIDERS = 8 };
	ObjectID			m_raider[ MAX_PERSONA_RAIDERS ];	///< the Raider's own raiders, which no wave or defence takes
	UnsignedInt		m_shareFrame;					///< the last Economist share
	UnsignedInt		m_chatFrame;					///< the last line of chat, 0 for none yet
	Int						m_beatenMask;					///< enemies seen eliminated, by player index, for the chat
	// counters for logPersonaSummary, written and never read by a decision
	Int						m_personaWaves;
	UnsignedInt		m_firstWaveFrame;
	Int						m_personaShared;
	UnsignedInt		m_superweaponFrame;

	enum { MAX_AI_SCOUTS = 2 };				///< the ladder's maxScouts never asks for more than this
	ObjectID	m_scoutID[ MAX_AI_SCOUTS ];	///< the units currently touring the map for us
	Int				m_scoutTargetFor[ MAX_AI_SCOUTS ];	///< which start position each one is walking to
	Int				m_scoutTimer;						///< frames until the next scouting check
	UnsignedInt m_scoutSeenFrame[ MAX_PLAYER_COUNT ];	///< when each start position was last looked at; 0 == never
	Bool			m_startChecked[ MAX_PLAYER_COUNT ];	///< start positions we have looked at, or deduced without looking
	Bool			m_startOccupied[ MAX_PLAYER_COUNT ];	///< ... and which of those turned out to hold an enemy
	Int				m_playerStartNdx[ MAX_PLAYER_COUNT ];	///< the start position each player is known to be at; -1 == not found yet
	UnsignedInt m_startIntelFrame;			///< frame the above was last brought up to date
	ObjectID	m_capturerID;						///< the unit currently out taking tech buildings for us
	ObjectID	m_ferryID;							///< the helicopter flying the capturer to its target, INVALID_ID for none
	std::vector<ObjectID>	m_droppedRiders;	///< infantry a helicopter is putting down at a fight, sent on once out
	enum { MAX_GUNSHIP_CHINOOKS = 2 };
	ObjectID	m_gunshipChinook[ MAX_GUNSHIP_CHINOOKS ];	///< Combat Chinooks bought to carry riders; INVALID_ID for a free slot
	UnsignedInt m_boardWaitFrame;			///< a wave ready to leave first waited for its gunships' riders on this frame; 0 for none
	/// A plain Chinook bought to fly the wave's ground units to the front, never to gather
	enum { SHUTTLE_HOME, SHUTTLE_LOADING, SHUTTLE_OUT, SHUTTLE_RETURNING };
	struct ShuttleChinook
	{
		ObjectID		id;					///< INVALID_ID for a free slot
		Int					phase;			///< SHUTTLE_HOME, SHUTTLE_LOADING, SHUTTLE_OUT or SHUTTLE_RETURNING
		UnsignedInt	frame;			///< when the phase began
		Int					load;				///< units aboard when it took off
		Bool				aborted;		///< hit on the way out, and putting its load down where it was
		Coord3D			drop;				///< where this trip puts them down
		Coord3D			home;				///< where it waits and loads; set the first time it loads
	};
	enum { MAX_TRANSPORT_CHINOOKS = 4 };
	ShuttleChinook	m_shuttle[ MAX_TRANSPORT_CHINOOKS ];
	Coord3D			m_shuttleFront;			///< the drop point on the last wave's road, short of what the AI has seen shoot
	UnsignedInt	m_shuttleFrontFrame;	///< when that wave left; 0 for no wave yet
	Int					m_lastWaveSlots;		///< transport slots the last wave's ground units take
	ObjectID		m_lentChinook;			///< a duty Chinook gathering because no gatherer can exist otherwise
	/// A Helix bought for a job of its own outside the teams: a healer behind the wave, or a raider
	enum { HELIX_HEALER, HELIX_RAIDER };
	enum { RAID_HOME, RAID_OUT, RAID_IN, RAID_BACK };
	enum { HEAL_HOME, HEAL_PATIENT, HEAL_REAR, HEAL_PULLBACK };
	struct DutyHelix
	{
		ObjectID		id;					///< INVALID_ID for a free slot
		Int					role;				///< HELIX_HEALER or HELIX_RAIDER
		Int					phase;			///< a raider's RAID_*, a healer's HEAL_*
		UnsignedInt	frame;			///< when the phase began
		ObjectID		target;			///< a raider's building, a healer's patient
		Coord3D			spot;				///< a raider's way in and out, a healer's last ordered spot
		Real				targetHealth;	///< the building's health when the bomb went
	};
	enum { MAX_DUTY_HELIXES = 5 };
	DutyHelix		m_dutyHelix[ MAX_DUTY_HELIXES ];
	/// An Ambulance bought to follow the army: the hurt infantry climb in, and it cleans hazards on our side
	struct DutyAmbulance
	{
		ObjectID		id;					///< INVALID_ID for a free slot
		Coord3D			spot;				///< where it was last sent
		Coord3D			seen;				///< for the log only, not saved: where it stood on the last pass
		Real				seenHealth;	///< for the log only, not saved: its health share on the last pass
		Real				seenReach;	///< for the log only, not saved: how deep inside a known gun's reach it stood, negative when clear
		Int					pullbacks;	///< for the log only, not saved: pull-back orders this one was given
		Int					holds;			///< for the log only, not saved: looks it wanted to pull back and found no clearer ground
	};
	enum { MAX_DUTY_AMBULANCES = 2 };
	DutyAmbulance	m_ambulance[ MAX_DUTY_AMBULANCES ];
	UnsignedInt		m_ambulanceLost;		///< the frame the last duty Ambulance was lost, 0 for never
	Int				m_ambulancesBought;	///< duty Ambulances that came out this match
	/// for the log only, not saved: each patient an Ambulance called in, until it is let out, dies or walks off
	struct CalledPatient
	{
		ObjectID		id;
		ObjectID		ambulance;
		UnsignedInt	frame;
		Real				health;
		Real				distance;
		Bool				inside;
	};
	std::vector<CalledPatient>	m_calledPatients;
	void followPatients(void);
	Int					m_healerSeconds;		///< for the log only, not saved: healer seconds counted, and those clear of the nearest gun's reach
	Int					m_healerClearSeconds;
	Int				m_captureTimer;					///< frames until the next look for something to capture
	ObjectID	m_hijackerID;						///< the thief currently out after an enemy vehicle
	Int				m_hijackTimer;					///< frames until the next look for a vehicle to take
	Int				m_retreatTimer;					///< frames until the next look at how the fights are going
	Int				m_expandTimer;					///< frames until the next look for somewhere to expand to

	Bool		m_readyToBuildTeam;				///< True if the team select timer has expired.
	Bool		m_readyToBuildStructure;	///< True if the buildDelay timer has expired.
	Int			m_teamTimer;							///< Counts out the time between teams, as specified by ini.
	Int			m_structureTimer;					///< Counts out the time between structures, as specified by ini.
	Int			m_teamSeconds;						///< How many seconds to delay between teams.

	Int			m_buildDelay;							///< Delay for building in case we are resource or prereq. limited.
	Int			m_teamDelay;							///< Delay for teams in case we are resource or factory prereq. limited.

	Int			m_frameLastBuildingBuilt;	///< When we built the last building.

	/* Where buildStructureWithDozer's flood fill for somewhere to put a building had got to when it
		 ran out of its per-frame budget, and which building and spot it was searching for.  Not
		 xferred: both machines in a network game compute them the same way from the same frames.  A
		 savegame loaded half way through a search floods again from the start, so the building can
		 go up a few frames later, or on another cell, than it would have in the game that never
		 saved.  Network games are not saved, so nothing has to match that. */
	std::vector<ICoord2D> m_buildSearchCells;	///< every pathfind cell reached so far, in the order reached
	Int			m_buildSearchNext;								///< first entry of m_buildSearchCells not yet expanded
	std::vector<UnsignedByte> m_buildSearchSeen;	///< one flag per cell of the square the search may cover
	const ThingTemplate *m_buildSearchPlan;		///< the building whose footprint the search is trying
	Coord3D m_buildProbePos;

	/* Ground distance fields over the whole pathfind grid (groundFromBase, groundFromThreats), not
		 xferred for the same reason: each is a pure function of the map, the bridges standing and
		 the point it was flooded from, recomputed the first time it is asked for after the point
		 moves or a bridge falls or is put back. */
	std::vector<UnsignedShort> m_groundFromBase;
	Coord3D m_groundFromBaseAt;
	UnsignedInt m_groundFromBaseBridges;
	std::vector<UnsignedShort> m_groundFromThreats;
	std::vector<Coord3D> m_groundThreatsAt;
	UnsignedInt m_groundThreatsBridges;

	/* The Turtle's chokes (turtleChokes), not xferred for the same reason: a pure function of the
		 map, the bridges, the base center and the enemy addresses, recomputed when one of them moves. */
	std::vector<Coord3D> m_chokes;
	std::vector<Coord3D> m_chokeThreatsAt;
	Coord3D m_chokeBaseAt;
	UnsignedInt m_chokeBridges;

	GameDifficulty m_difficulty;

	Int			m_skillsetSelector;

	Coord3D m_baseCenter; // Center of the initial build list of structures.
	Bool		m_baseCenterSet; // True if baseCenter is valid.
	Real m_baseRadius; // Radius of the initial build list of structures.
	Int			m_placementRing;	///< ground distance (AIGroundFlood units) placeNear starts trying spots at when it may walk outward

	// Bridge repair info.
	enum {MAX_STRUCTURES_TO_REPAIR = 2};
	ObjectID m_structuresToRepair[MAX_STRUCTURES_TO_REPAIR];
	ObjectID m_repairDozer;
	UnsignedInt m_frameAfterFailedDozerQueue;	///< frame+1 of the last queueDozer that found no factory, 0 for never
	Coord3D  m_repairDozerOrigin;
	Int			 m_structuresInQueue;
	Bool		 m_dozerQueuedForRepair;
	Bool		 m_dozerIsRepairing;			///< the repair dozer is trying to repair the bridge.
	Int			 m_bridgeTimer;

	UnsignedInt	m_supplySourceAttackCheckFrame;
	ObjectID m_attackedSupplyCenter;

	ObjectID m_curWarehouseID;

	/** C2: attack teams parked at the staging point, waiting to go out together. */
	virtual void doWaves(void);
	enum { MAX_HELD_TEAMS = 16 };
	Bool				m_heldUsed[ MAX_HELD_TEAMS ];
	UnsignedInt	m_heldTeam[ MAX_HELD_TEAMS ];			///< TeamID; Team.h is not included here
	AsciiString	m_heldLabel[ MAX_HELD_TEAMS ];		///< the approach the script asked for
	Int					m_heldSuffix[ MAX_HELD_TEAMS ];		///< the enemy start index its path name ends in
	UnsignedInt	m_heldSince;											///< frame the first of the parked teams arrived

	Bool isOutOnOrders(const Object *obj) const;	///< away from home with something to do
	Bool isAtHome(const Coord3D *pos) const;			///< within two base radii of the base center
	Bool isBaseUnderAttack(void) const;						///< hit lately, with a known enemy that can shoot standing at home
	Object *homeIntruder(Int *count, std::vector<AIVisibleEnemy> *army) const;	///< the enemy nearest the base center while the base is under attack
	Object *baseSquatter(Int *count, std::vector<AIVisibleEnemy> *army) const;	///< the known enemy nearest the base center inside the base, hit or not, a building and a worker included
	void defendHome(void);												///< a base under attack trains fighters from its bank and sends its idle units in
	void sendAtSquatter(Object *squatter, Real worth, const std::vector<Object *> &owned);	///< the nearest idle fighters at home, up to worth in combat power
	void dodgeStrike(void);												///< the rung's beat after the word of an enemy launch, the helicopters leave where they stand
	UnsignedInt m_strikeNoticeFrame;							///< when the word of an enemy launch came, 0 when it has been acted on
	Real waitingPower(Team *team, AIGroup *group) const;	///< what a team has for a wave, less the members out on orders; they join the group if one is given

	/** How hard this AI leans on its current enemy: its chance against him, looked at again on the
		* wave tick, decides how big a wave has to be and whether the guards at home go as well. */
	void updatePressure(void);
	void sendIdleUnitsHunting(void);							///< attack units idle at the end of their road, and the guards once the enemy is finished
	Bool holdsTeamsForWaves(void) const;					///< this rung parks attack teams at the current level
	Bool readyToPush(void);												///< the economy is in and the opening is over: attack teams may leave home
	Bool waveGoes(Real power, UnsignedInt heldFrames);	///< what is parked, worth this, leaves now
	void buyEarlyIncome(void);										///< the first drop zone and black market, ahead of the army
	void keepSiegeBehind(void);										///< artillery out with a wave trails its front unit
	void holdAtDefences(void);										///< ground units stop at the edge of a known defence's reach until ours there outweigh it; guns that outreach one shell it
	Real edgeRatio(const Object *gun, Real reach, const Coord3D *from);	///< the fight where a unit at from would walk into a gun's reach
	Bool leavesToFinish(const Object *obj) const;	///< a fighter the last push takes off guard, as against a worker or a scout
	/** Every enemy unit this AI has seen and not since lost track of: what it was, whose, and where and
		* when it was last in sight.  An army that walks back into the fog is still an army.  A row goes when
		* it has not been seen for LEDGER_FORGET_FRAMES, or when the ground it was last seen on is in sight
		* and it is not there and no longer exists: the AI looked and it was gone. */
	struct SeenEnemy
	{
		const ThingTemplate	*tmpl;
		Int					owner;			///< player index
		UnsignedInt	seen;				///< last frame it was in sight
		Coord3D			pos;				///< where
		Bool				stealth;		///< it can go invisible
	};
	std::map<ObjectID, SeenEnemy> m_seenEnemies;
	void answerArmy(void);	///< train the answer to the biggest part of the remembered enemy army nothing of ours answers

	/** The fist: one unit picked because the army it remembers has no answer to it, built from the idle
		* factories, held together at the staging point and sent as one. */
	void doMass(void);
	const ThingTemplate *pickMassUnit(const std::vector<AIVisibleEnemy> &army, Real *efficiency) const;
	Bool isMassUnit(ObjectID id) const;
	const ThingTemplate	*m_massTemplate;	///< NULL while no unit is picked
	UnsignedInt					m_massPickFrame;	///< when it was picked, or dropped; 0 for never
	UnsignedInt					m_massSince;			///< when the first of the fist now waiting joined it
	std::vector<ObjectID>	m_massUnits;	///< the fist so far, in the order they came out
	AIPressure	m_pressure;
	Real				m_knownEnemyPower;								///< his army as this AI believes it: what is in sight at least, less for each look at his base that does not find it, more for each look not taken
	Int					m_pressureEnemy;									///< the player index that figure is about, -1 for nobody yet

	/** Where the last few superweapons were aimed, so the next one does not land in the same crater
		* while the one before it is still in the air. */
	enum { MAX_REMEMBERED_STRIKES = 4 };
	Coord3D			m_strikeAim[ MAX_REMEMBERED_STRIKES ];
	UnsignedInt	m_strikeFrame[ MAX_REMEMBERED_STRIKES ];	///< frame each was aimed; 0 == slot never used
	Int					m_strikeNext;											///< slot the next aim is written to

	/** What this player knows can shoot at each patch of the map, and what it has there itself. */
	AIInfluenceMap	m_influence;
	AIInfluenceMap	m_influenceNext;		///< Hard's: what this round's objects have stamped so far; it becomes m_influence when the round ends
	void updateInfluence(void);										///< Hard's map, one share of the objects a frame
	void rebuildInfluence(void);									///< the whole map on one frame
	void stampInfluence(AIInfluenceMap &map, const Object *obj);
	void stampFireReports(AIInfluenceMap &map);
	void ensureInfluence(void);										///< the map, no older than this rung reads it; below Hard this is what draws it
	Bool hasTacticalMap(void) const;							///< Hard's map, for the reads the rungs below it do not make
	Bool quietGround(const Coord3D *from, const Real *gun, Coord3D *spot);	///< where to go from here to be off the map's guns and clear of this one (x, y, reach, or NULL); FALSE when no way gets out

	/** A gun that has hit this player from past where its fights look or from the fog.  It goes on the map as
		* if seen, where it stood at the last hit, and counts in the fights around that hit, for the rung's
		* m_fireMemorySeconds.  The shell's arc is on the screen, so this tells the AI what it tells a player. */
	struct FireReport
	{
		ObjectID		shooter;
		Coord3D			at;					///< where the gun stood
		Coord3D			hitAt;			///< where what it hit stood
		Real				reach;
		Real				power;
		UnsignedInt	frame;			///< the last hit
	};
	std::vector<FireReport>	m_fireReports;
	Object *reportedShooter(const FireReport &report) const;	///< NULL when it is dead, forgotten or no enemy now
	void noteFire(const Object *shooter, const Object *gun, const Coord3D *hitAt);

	/** A unit hit answers the gun that hit it, once in each of the rung's looks: AIPlayer::answerFire.  What it
		* takes to get a gun is read once a look, by the first unit that has to ask, and holds for the rest. */
	struct GunReading
	{
		ObjectID		gun;
		Real				ours;					///< what is near the unit that read it and can hurt this gun
		Real				cover;				///< what the map has covering the gun's own spot
		Real				edge;					///< a fixed gun: the fight at the edge of its reach
		UnsignedInt	frame;
	};
	std::vector<GunReading>	m_gunReadings;
	void answerFire(void);
	AIFireAnswer answerHit(Object *obj, Object *shooter, Object *gun);	///< decided and ordered
	const GunReading &readGun(const Object *obj, const Object *gun);
	void fightersNear(const Coord3D *at, Real radius, std::vector<Object *> *fighters);	///< our own who take orders here, in ObjectID order
	void finishAnswers(void);											///< those done with a gun go back to what they were doing
	Bool isAnsweringFire(ObjectID unit);

	/** Hard's fighting units, one at a time: step back from what they outrange, climb onto ground
		* that lengthens their guns, and take a hurt unit out of ground it cannot win on. */
	virtual void doTactics(void);
	void doTransports(void);	///< helicopters put riders who cannot shoot out down at the fight
	Bool measuringWithoutTactics(void) const;	///< -notactics has this slot fight the old way
	Bool fightsOnItsOwn(Object *obj);		///< armed, on the ground, and with no job of its own or base to guard: the tactics' and the answers' to order
	void tacticsFor(Object *obj);
	struct TacticalStep
	{
		ObjectID		unit;
		ObjectID		target;						///< what it goes back to shooting when the step is done
		Coord3D			origin;						///< where it stepped from, to walk back to when the target is gone
		UnsignedInt	resumeFrame;			///< 0 while it is not stepping
		UnsignedInt	nextClimbFrame;		///< no look for higher ground before this
		UnsignedInt	leaveAloneUntil;	///< sent home by the retreat, not to be turned round
		Bool				rejoin;						///< stepped out of its team's order, and goes back to the team when the fight is over
		Int					savedAttitude;		///< its mood before a step calmed it, AI_INVALID when it has its own
		UnsignedInt	lastKiteFrame;		///< last time it stepped back from something it outranges, 0 for never
		UnsignedInt	lastSeenFrame;		///< a unit not looked at for a while has died or left, and its row goes
		Bool				fallingBack;			///< pulled out of a lost fight to a safe spot, and goes back once it is safe
		Coord3D			fallbackFrom;			///< the fight it was pulled out of
		UnsignedInt	fallbackFrame;		///< when it was pulled out
		UnsignedInt	answerFrame;			///< when it was sent at a gun that hit it, 0 while it is not; target and origin are what it goes back to
	};
	std::vector<TacticalStep>	m_tactics;
	TacticalStep *findTacticalStep(ObjectID unit);
	TacticalStep *tacticalStepFor(ObjectID unit);		///< ... making the row if there is none
	void leaveTacticsAlone(ObjectID unit);
	Bool isFallingBack(ObjectID unit) const;		///< holding at a safe spot after a lost fight, for doRetreats to send back
	void measureFight(const Coord3D *centre, Bool countHolders, Real *myHealth, Real *myPower,
		Real *enemyHealth, Real *enemyPower, std::vector<Real> *enemyGuns, Real *advantage = NULL, Real *defencePower = NULL);
	Real fightRatio(const Coord3D *centre, Bool countHolders, Real *enemyHealth, Real *enemyPower, std::vector<Real> *enemyGuns,
		Real *defencePower = NULL);	///< the exchange around centre, by the units in it when the rung weighs them; defencePower is the part of the enemy's that stands outside our base and is a building or a gun known by its shells
	Bool doFallback(Team *team);		///< TRUE when it sent the team's holders back or home on this pass
	void pullOutRanked(Team *team, Bool inFight, Real ratio);	///< veterans and up leave earlier than the team, to a repair or heal pad
	void releaseMended(void);		///< pulled-out veterans and let-out patients standing idle again become recruitable
	void stepCalmly(Object *obj, TacticalStep *step, const Coord3D *spot);	///< a move the unit's mood cannot turn into an attack move
	void restoreMood(Object *obj, TacticalStep *step);
	Bool pickTacticalSpot(const Object *obj, const Coord3D *from, const Coord3D *awayFrom, Real distance,
		const Coord3D *mustReach, Real reach, Coord3D *spot);
	void spotterStandOff(Object *obj, TacticalStep *step);	///< a spotter backs out of the nearest armed enemy's reach, keeping it in sight
};

#endif // _AI_PLAYER_H_



