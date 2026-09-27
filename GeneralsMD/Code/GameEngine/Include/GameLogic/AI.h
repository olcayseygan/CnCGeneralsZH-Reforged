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

////////////////////////////////////////////////////////////////////////////////
//																																						//
//  (c) 2001-2003 Electronic Arts Inc.																				//
//																																						//
////////////////////////////////////////////////////////////////////////////////

// AI.h //
// AI header file
// Author: Michael S. Booth, November 2000

#pragma once

#ifndef _AI_H_
#define _AI_H_

#include "Common/Snapshot.h"
#include "Common/SubsystemInterface.h"
#include "Common/GameMemory.h"
#include "Common/GameType.h"
#include "GameLogic/Damage.h"
#include "Common/STLTypedefs.h"
#include "Lib/Fix.h"

class AIGroup;
class AttackPriorityInfo;
class BuildListInfo;	
class CommandButton;
class Object;
class PartitionFilter;
class Path;
class Pathfinder;
class Player;
class PolygonTrigger;
class ThingTemplate;
class UpgradeTemplate;
class WeaponTemplate;

enum GUICommandType;
enum HackerAttackMode;
enum WeaponSetType;
enum WeaponLockType;
enum SpecialPowerType;

typedef std::vector<ObjectID> VecObjectID;
typedef VecObjectID::iterator VecObjectIDIt;

typedef std::list<Object *> ListObjectPtr;
typedef ListObjectPtr::iterator ListObjectPtrIt;

enum AIDebugOptions
{
	AI_DEBUG_NONE = 0, 
	AI_DEBUG_PATHS,
	AI_DEBUG_TERRAIN,
	AI_DEBUG_CELLS,
	AI_DEBUG_GROUND_PATHS,
	AI_DEBUG_ZONES,
	AI_DEBUG_END
};

enum 
{
	// multiply by the owner type, either AI or human
	AI_VISIONFACTOR_OWNERTYPE = 0x01,

	// multiply by the "mood" of the unit
	AI_VISIONFACTOR_MOOD = 0x02,

	// If we want to consider the inner radius, we will specify guard inner. Otherwise, use guard outer.
	AI_VISIONFACTOR_GUARDINNER = 0x04,
};
enum {MAX_AI_UPGRADES = 20};

typedef struct {
	Int	m_numSkills;
	ScienceType m_skills[MAX_AI_UPGRADES];
} TSkillSet;

class AISideInfo : public MemoryPoolObject
{
	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE(AISideInfo, "AISideInfo")		
public:
	AISideInfo( void ) : m_easy(0), m_normal(1), m_hard(2), m_next(NULL)
	{
		m_side.clear();
		m_baseDefenseStructure1.clear();
	}

	AsciiString m_side;						///< Name of the side
	Int					m_easy;						///< Number of gatherers to use in easy, normal & hard
	Int					m_normal;	
	Int					m_hard;
	TSkillSet		m_skillSet1;
	TSkillSet		m_skillSet2;
	TSkillSet		m_skillSet3;
	TSkillSet		m_skillSet4;
	TSkillSet		m_skillSet5;
	AsciiString m_baseDefenseStructure1;
	AISideInfo *m_next;
};
EMPTY_DTOR(AISideInfo)

class AISideBuildList : public MemoryPoolObject
{
	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE(AISideBuildList, "AISideBuildList")		
public:
	AISideBuildList( AsciiString side ); 
	//~AISideBuildList();
	
	void addInfo(BuildListInfo *info);

public:
	AsciiString				m_side;						///< Name of the faction.
	BuildListInfo*		m_buildList;				///< Build list for the faction.
	AISideBuildList*	m_next;
};



//-------------------------------------------------------------------------------------------------
/** Three rungs of how well the AI plays.  Deliberately NOT GameDifficulty, which carries the same
	* three values for a different reason: that enum answers "which scripts run" and is baked into
	* every shipped map and .scb, into the campaign's stat bonus tables and into network messages.
	* This one answers "how well does it play", and every rung of it plays by the player's rules - no
	* income, build-rate, vision or stat handicap at any level, in either direction.  Keeping it a
	* separate enum is what lets a rung be tuned in AI.ini without touching what a map's scripts do.
	* See AI-ROADMAP.md sections D6, D7 and D10.
	*/
//-------------------------------------------------------------------------------------------------
enum AISkillLevel
{
	AISKILL_EASY = 0,			///< slow and brave: looks around, but reacts far too late
	AISKILL_MEDIUM,				///< + unit retreat, closest-target focus, expands on its own
	AISKILL_BRUTAL,				///< the baseline: counters what you field, masses, no reaction delay

	AISKILL_COUNT
};

//-------------------------------------------------------------------------------------------------
/** What the AI is trying to do, as opposed to how well it does it.  A preference, never a bonus:
	* the roles have to be about equally strong or the axis silently becomes a difficulty setting. */
//-------------------------------------------------------------------------------------------------
enum AIRole
{
	AIROLE_AGGRESSIVE = 0,	///< cheap early units, few defences, commits with less
	AIROLE_DEFENSIVE,				///< base defences first, waits for more, counter-attacks
	AIROLE_SUPPORTIVE,			///< converges on an ally's target, answers calls for help, shares the map
	AIROLE_ECONOMIST,				///< expands early and often, fights only with an advantage
	AIROLE_STEAMROLLER,			///< heavy units, one large well-timed push

	AIROLE_COUNT
};

/** How much of the enemy army a role wants in hand before it commits.  The role's knob, not the
	* rung's: both spend the same money, they spend it at different moments. */
Real aiRoleMassFraction( AIRole role );

//-------------------------------------------------------------------------------------------------
/** One rung of the ladder.  Every new AI behaviour adds a field here rather than a
	* switch(difficulty) somewhere, so the whole ladder can be read, tuned and measured in one place.
	*
	* Nothing in here may exceed the AI's honest baseline - the top rung IS the baseline and the
	* lower rungs are handicaps on its *decisions*.  If a field ever appears that multiplies income,
	* build rate, vision or unit stats, the design has drifted. */
//-------------------------------------------------------------------------------------------------
struct AIDifficultyProfile
{
	// Perception. Never fog: only how diligently it looks and how fast it acts on what it saw.
	Fix		m_scoutIntervalSeconds;					///< how often the scout is re-tasked; every rung scouts
	Int		m_maxScouts;
	Fix		m_decisionIntervalSeconds;			///< how often it re-evaluates - the AI's APM, and so how late it answers
																				///< what it saw: a separate reaction delay stood here, read by nothing

	// Decision quality
	Fix		m_counterCompositionWeight;			///< 0 = ignore what the enemy fields (EA's behaviour)
	Bool	m_massBeforeAttacking;
	Fix		m_retreatTtkRatio;							///< allied time-to-kill / enemy TTK below which it pulls back
	Bool	m_retreatIndividualUnits;				///< pull a losing unit out of a fight the team is winning
	Bool	m_retreatTeams;									///< ... and break off the whole team when the fight is lost
	Bool	m_useInfluenceMapForAttackLane;
	Bool	m_focusFire;
	Bool	m_savesSciencePoints;				///< wait for the next power in the set instead of buying filler

	// Economy: decisions only, never multipliers
	Bool	m_adaptiveHarvesters;
	Bool	m_selfTriggeredExpansion;
	Bool	m_defendExpansions;
	Int		m_cashHoardThreshold;						///< above this, spend faster; 0 = never hurry
	Bool	m_economyBuildings;							///< past the hoard, buy production and income the build list never had

	// Fighting: decisions only, from what it knows can shoot where
	Bool	m_tacticalMicro;								///< kite what it outranges, take the high ground, pull hurt units out
};

//-------------------------------------------------------------------------------------------------
/** What an enemy is fielding, as shares of the threat value the observer can actually see.  The
	* four shares overlap (a stealth tank is both armour and stealth) and each is 0..1. */
//-------------------------------------------------------------------------------------------------
struct AIEnemyComposition
{
	Real m_air;					///< share of the enemy's visible threat value that flies
	Real m_armour;			///< ... that is a vehicle
	Real m_infantry;
	Real m_stealth;			///< ... that can go invisible
	Real m_totalThreat;	///< and the absolute total the shares are taken from, for C2's massing

	AIEnemyComposition() : m_air(0.0f), m_armour(0.0f), m_infantry(0.0f), m_stealth(0.0f),
												 m_totalThreat(0.0f) {}
};

//-------------------------------------------------------------------------------------------------
/** One kind of enemy unit the AI can see, and how much of the visible army it makes up. */
//-------------------------------------------------------------------------------------------------
struct AIVisibleEnemy
{
	const ThingTemplate *m_template;
	Real m_weight;			///< summed combat power of every visible unit of this kind
	Real m_cost;				///< what one of them cost its owner
};

//-------------------------------------------------------------------------------------------------
/** What a team can answer, read off the unit templates it is built from. */
//-------------------------------------------------------------------------------------------------
struct AITeamCapability
{
	Real m_answer;					///< 0..1: how its units fare against the visible enemy army, money for money
	Bool m_detectsStealth;

	AITeamCapability() : m_answer(0.0f), m_detectsStealth(FALSE) {}
};

/** How well this team answers what the enemy is fielding, 0 (answers none of it) to 1 (answers all
	* of it).  Pure arithmetic on purpose - the whole point of B1 is a score you can test and tune
	* rather than a coin flip, and this is the half of it that has no engine in it. */
Real aiCounterScore( const AIEnemyComposition &enemy, const AITeamCapability &team );

//-------------------------------------------------------------------------------------------------
/** How one weapon works on one target, in the numbers that decide how long the target lasts. */
//-------------------------------------------------------------------------------------------------
struct AIShotPattern
{
	Real m_damagePerShot;		///< after the target's armour; 0 cannot hurt it
	Real m_delayFrames;			///< between two shots of one clip
	Int  m_clipSize;				///< 0 never reloads
	Real m_reloadFrames;		///< between the shot that empties a clip and the next one
	Real m_openingFrames;		///< once, before the first shot

	AIShotPattern() : m_damagePerShot(0.0f), m_delayFrames(0.0f), m_clipSize(0), m_reloadFrames(0.0f),
										m_openingFrames(0.0f) {}
};

const Real AI_CANNOT_KILL = -1.0f;

/** Frames until a weapon firing this pattern kills a target with this much health, or
	* AI_CANNOT_KILL when it does no damage to it. */
Real aiFramesToKill( Real targetHealth, const AIShotPattern &shots );

/** One pairing as 0..1, money for money: 0.5 an even trade, 1 when my side kills sixteen times
	* faster for its price (or cannot be hurt back), 0 when it cannot hurt the other side. */
Real aiMatchupScore( Real myFramesToKill, Real theirFramesToKill, Real myCost, Real theirCost );

/** How the exchange is going, as the ratio of how long this force lasts to how long it needs to
	* kill what is shooting at it.  Below 1 it is losing; below the rung's retreatTtkRatio it should
	* break off.  This is Sins of a Solar Empire's aiRetreatThreshold, and it is the right metric
	* rather than a health percentage: a unit at 20% health that still out-damages what is shooting
	* it should stay, and a full-health one being melted should not.
	*
	* 'power' is a stand-in for damage per second - the threat value the data already carries. */
Real aiRetreatRatio( Real myHealth, Real myPower, Real enemyHealth, Real enemyPower );

/** Hold a finished team at the rally point instead of sending it?
	*
	* A string of small waves is free veterancy for whoever is on the other end: EA's AI sent every
	* team the moment it was ready.  Massing is the change that makes the top rungs dangerous, and
	* it needs three answers - is there enough, is there time, and is home safe.
	*
	* massFraction is how much of what it can see of the enemy army it wants in hand before it
	* commits: the role's knob, not the rung's.  An aggressive AI presses with less. */
Bool aiShouldMass( Real waitingThreat, Real enemyVisibleThreat, Real massFraction,
									 Bool timeExpired, Bool baseUnderAttack );

/** How badly one place wants looking at, per step walked there.  A3's scouting is never a search -
	* the start positions are public, the lobby shows them - so the question is not "where is he" but
	* "whose picture is worth the walk", and the answer is the stalest one per unit of distance:
	*
	*     score = frames since we last looked there / distance to walk there
	*
	* lastSeenFrame 0 means never looked, which makes the age the whole match so far and puts those
	* ahead of everything already seen - nearest first among them, since the distance still divides.
	*
	* A picture younger than freshFrames scores 0: the trip buys nothing, and not making it is the
	* whole point.  So 0 means "do not go", not "go last".
	*/
Real aiScoutScore( UnsignedInt now, UnsignedInt lastSeenFrame, Real distance, UnsignedInt freshFrames );

/** The odds that any one start position nobody has looked at yet is an enemy's, given how many
	* enemies are still unaccounted for.  1v3 on an eight-position map: three enemies over the seven
	* positions that are not mine, so 3/7 each.  Cross an empty one off and it is 3/6, then 3/5 - the
	* remaining ones get likelier with every empty answer, which is the whole reason to ask.
	*
	* Find one instead and it accounts for itself: two left over five.
	*
	* It reaches 1.0 when there are as many enemies left to place as positions left to look at, and
	* that is the case worth having the arithmetic for - walking there confirms something already
	* known, so the AI can stop searching and spend the trip on what is actually standing there.  A
	* two-player map is that case at its first step: one enemy, one position, 1/1, known before the
	* scout is even built. */
Real aiStartOccupiedOdds( Int unlocatedEnemies, Int uncheckedPositions );

/** Is this building income rather than a target?  An oil derrick pays whoever owns it and costs one
	* infantryman to take, so a computer player razing the enemy's is the one thing it can do with a
	* derrick that earns nobody anything.
	*
	* Two things are deliberately still targets.  One that shoots back has to be shootable back at, or
	* the AI walks past a bunker forever.  And one that cannot be captured at all is only ever a
	* building in the way. */
Bool aiIsIncomeNotATarget( Bool isTechBuilding, Bool isCapturable, Bool isTechBaseDefense );

/** What a superweapon target is still worth, given one shot already aimed at it - 1.0 untouched,
	* 0.0 right on top of a shot aimed this frame, climbing back over 'window' frames and with
	* distance from that shot.
	*
	* A superweapon is in the air for several seconds and everything it is about to kill is standing
	* the whole time, so a scan that reads the object list hands every ready superweapon, and every
	* script pass until the first one lands, the identical point: three nukes in one crater.  The
	* recovery is deliberately gradual rather than a ban - the ground an enemy rebuilt on is worth
	* hitting again, and a base with nothing else left in it should still be shot at.
	*
	* Two shots further apart than the blast radius do not cover the same ground, so nothing fades. */
Real aiStrikeFade( Real distanceSqr, Real radiusSqr, UnsignedInt age, UnsignedInt window );

/** How attractive an enemy player is to go after, as a cost - lower is better, and it is a squared
	* distance with the other terms folded into it so the units stay comparable.
	*
	* EA's version was distance and nothing else, plus one rule that had the sign wrong: an enemy
	* who had lost his units or his production had his distance set to half the map, which reads as
	* "ignore the crippled one".  Finishing off the weak one is usually the right move, and refusing
	* to is what drags matches out.
	*
	* Sins of a Solar Empire has the identical bug, in its own community's words: its AI "is much
	* more likely to send its fleet to a well-defended planet roughly 5 map units away than an
	* ill-defended planet 25 map units away".  Two independently written RTS AIs converging on the
	* same mistake is decent evidence that distance-dominated target selection is the wrong default. */
Real aiEnemyCost( Real distSqr, Bool crippled, Bool alreadyTargetedByAnotherAI,
									Bool isAttackingMe, Real economyShare, Bool joinAllysTarget = FALSE );

/** Shorten a build delay when the money is piling up.  The classic AI disease is sitting on twenty
	* thousand cash while trickling one unit out at a time; the delays already move with wealth
	* (TAiData's Wealthy/Poor rates), but only one step, and a hoard walks straight past it.
	*
	* Twice the threshold halves the delay, four times quarters it, and it stops there - this is a
	* decision to spend faster, not a production multiplier, and D10 draws that line. */
Int aiHoardAdjustedDelay( Int frames, Int money, Int hoardAt );

/** How many copies of one team a skirmish AI may field.  The skirmish scripts cap the army by
	* their roster: late in a match every attack team left to build allows one copy, so the AI
	* replaces its losses and banks the rest.  Every two thresholds of cash allows one more copy of
	* the teams the script already permits, up to three more.  A team the data never allows stays
	* at zero, and no threshold leaves EA's limit alone. */
Int aiHoardAllowedTeamInstances( Int maxInstances, Int money, Int hoardAt );

/** Which approach a team takes: the lane with the least known enemy firepower along it.  A negative
	* entry is a lane this map does not have.  A tie, or nothing known anywhere, keeps the lane the
	* script asked for, so an AI that has not scouted plays exactly as the script wrote it. */
Int aiLeastDefendedLane( const Real *laneFirepower, Int laneCount, Int requestedLane );

/** Whether the attack teams parked at the staging point go now: when what is parked adds up to a wave,
	* or the oldest has waited long enough.  The threshold is this AI's own force, never a fraction of
	* the enemy's, because two AIs each waiting to outnumber the other never move. */
Bool aiReleaseWave( Real heldPower, Real wavePower, UnsignedInt heldFrames, UnsignedInt maxHoldFrames );

class TAiData : public Snapshot
{
public:
	TAiData();
	~TAiData();

	void addSideInfo(AISideInfo *info);
	void addFactionBuildList(AISideBuildList *buildList);

	// --------------- inherited from Snapshot interface --------------
	void crc( Xfer *xfer );
	void xfer( Xfer *xfer );
	void loadPostProcess( void );

	Fix m_structureSeconds;		// Try to build a structure every N seconds.
	Fix m_teamSeconds;					// Try to build a team every N seconds.
	Int m_resourcesWealthy;		// How many resources to be wealthy.
	Int m_resourcesPoor;			// How few resources to be poor.
	UnsignedInt m_forceIdleFramesCount;	// How many frames does a unit need to be Idle before it can begin looking for enemies?
	Fix m_structuresWealthyMod; // Factor to multiply m_structurFrames by if we are wealthy.
	Fix m_teamWealthyMod;		// Factor to multiply m_teamFrames by if we are wealthy.
	Fix m_structuresPoorMod; // Factor to multiply m_structureFrames by if we are poor.
	Fix m_teamPoorMod;				// Factor to multiply m_teamFrames if we are poor.
	Fix m_teamResourcesToBuild; // Amount of the resources needed to build a team required before we start.
	Fix m_guardInnerModifierAI;	// Multiply the AI unit's vision by this much == guard inner circle.
	Fix m_guardOuterModifierAI;	// Multiply the AI unit's vision by this much == guard outer circle.
	Fix m_guardInnerModifierHuman;	// Multiply the human unit's vision by this much == guard inner circle
	Fix m_guardOuterModifierHuman;	// Multiply the human unit's vision by this much == guard outer circle
	UnsignedInt m_guardChaseUnitFrames;		// Number of frames for which a unit should 
	UnsignedInt m_guardEnemyScanRate;		// rate to scan for enemies while guarding
	UnsignedInt m_guardEnemyReturnScanRate;		// rate to scan for enemies while guarding but returning

	Fix m_wallHeight;				// Height of special wall units can walk on top of.

	AIDifficultyProfile m_skill[AISKILL_COUNT];	///< the ladder; defaults in TAiData(), overridable from INI

	Fix m_alertRangeModifier;			// When a unit is alert, its range will be modified by this value
	Fix m_aggressiveRangeModifier;	// When a unit is aggressive, its range will be modified by this value

	/* The attack priority distance modifier changes relative values.  The relative priority
	   is reduced by the distance away, divided by the modifier.
		 Example: tanks are priority 10, powerplants priority 15, and modifier = 100.0
		 If a powerplant is 700 feet away from the attacker, and the tank is 
		 100 feet, the effective priority for tank is 9 (10-(100/100), and the
		 effective priority for powerplant is 8 (15 -(700/100).  So the tanks
		 would be attacked first because their distance weighted priority is greater. */
	Fix m_attackPriorityDistanceModifier; // Distance to reduce a relative AttackPriority by 1.
	
	
	/* 
		How close to a waypoint does a group of units have to be to consider itself at the waypoint?
		m_skirmishGroupFudgeValue is multiplied by the number of the units in the group asking if its
		close enough to determine this.
		
		So for instance (if m_skirmishGroupFudgeValue was 5), if 2 units are walking along a path, 
		they would check to see if they were less than 10 feet away from the waypoint in order 
		to say they were close enough. A group of 10 units would consider themselves close enough 
		if they were within 50 feet of the waypoint.
	*/
	Fix m_skirmishGroupFudgeValue;

	Fix m_maxRecruitDistance; // Maximum distance away that units can be recruited.
	Fix m_skirmishBaseDefenseExtraDistance; ///< instead of building base defenses right on the template bounding circle, push them out this much.
	Fix m_repulsedDistance; // How far a repulsed unit will run past vision range before stopping.
	Bool m_enableRepulsors; // Is repulsion enabled?

	Bool m_forceSkirmishAI; // If true, forces skirmish ai instead of solo ai for development until the skirmish ui is done.  jba.
	Bool m_rotateSkirmishBases; // If true, rotates skirmish ai bases to face the center of the map. jba.

	Bool m_attackUsesLineOfSight; // If true, attack for units with KINDOF_ATTACK_NEEDS_LINE_OF_SIGHT  uses line of sight. jba.

	Bool m_attackIgnoreInsignificantBuildings; // If true, attack for ALL UNITS ignores buildings that are hostile that are not significant. jkmcd

	// Group pathfind info.
	Int	 m_minInfantryForGroup;		// We need at least this many to do it.
	Int	 m_minVehiclesForGroup;		// We need at least this many vehicles to do it.
	Fix m_minDistanceForGroup;		// We need to move at least this far to do it.
	Fix m_distanceRequiresGroup; // If we are moving this far or farther, force group moving.
	Fix m_minClumpDensity;			// What density constitues a clump.  .5 means units occupying 1/2 of their bounding area.

	Int	 m_infantryPathfindDiameter; // Diameter of path in cells for infantry.
	Int  m_vehiclePathfindDiameter;  // Diameter of path in cells for vehicles.

	Int  m_rebuildDelaySeconds;  // Seconds to delay rebuilding after a base building is destroyed or captured.

	Fix  m_supplyCenterSafeRadius;  // Radius to scan for enemies to determine safety.

	Fix  m_aiDozerBoredRadiusModifier;  // Modifies ai dozers scan range so the move out farther than human ones.
	Bool	m_aiCrushesInfantry; // If true, AI vehicles will attempt to crush infantry.

	// Retaliate params. [8/25/2003]
	Fix	m_maxRetaliateDistance; // If attacker is > this distance, don't retaliate. [8/25/2003]
	Fix	m_retaliateFriendsRadius; // If we have friends within this radius, get them to help retaliate. [8/25/2003]


	AISideInfo *m_sideInfo;

	AISideBuildList *m_sideBuildLists;

	TAiData *m_next;
} ;

//------------------------------------------------------------------------------------------------------------
/**
 * The AI subsystem is responsible for the implementation of
 * the Artificial Intelligence of the game.  This includes 
 * the behaviors or all game entities, pathfinding, etc.
 */
class AI : public SubsystemInterface, public Snapshot
{
public:
	AI( void );
	~AI();

	virtual void init( void );						///< initialize AI to default values
	virtual void reset( void );						///< reset the AI system to prepare for a new map
	virtual void update( void );					///< do one frame of AI computation

	inline Pathfinder *pathfinder( void ) { return m_pathfinder; }	///< public access to the pathfind system

	/** How long the last AI frame spent in the pathfinder's queue and in the players' own updates,
		* in milliseconds.  GameLogic's slow frame log reads these: with several AI players a hitch is
		* either pathfinding or the AIs themselves, and guessing which has been the expensive way to
		* find out. */
	static Real getLastPathfindMS( void ) { return s_lastPathfindMS; }
	static Real getLastPlayerUpdateMS( void ) { return s_lastPlayerUpdateMS; }

	/* Target acquisitions this logic frame. The partition manager counts every range query it is
		 asked for; this counts the ones that are a unit looking for somebody to shoot, which is the
		 difference between knowing which subsystem to work on and guessing. Zeroed with the partition
		 counters at the top of the frame. */
	static Int getEnemyScanCountThisFrame( void ) { return s_enemyScans; }
	static void resetEnemyScanCount( void ) { s_enemyScans = 0; }
	static void bumpEnemyScanCount( void ) { ++s_enemyScans; }
	enum
	{
		CAN_SEE														=	1 << 0,
		CAN_ATTACK												= 1 << 1,
		IGNORE_INSIGNIFICANT_BUILDINGS		= 1 << 2,
		ATTACK_BUILDINGS									= 1 << 3,
		WITHIN_ATTACK_RANGE								= 1 << 4,
		UNFOGGED													= 1 << 5,
		PREFER_HIGH_THREAT								= 1 << 6		///< pick the biggest threat in range, not the nearest thing
	};
	Object *findClosestEnemy( const Object *me, Real range, UnsignedInt qualifiers, 
		const AttackPriorityInfo *info=NULL, PartitionFilter *optionalFilter=NULL);

	Object *findClosestRepulsor( const Object *me, Real range);

	Object *findClosestAlly( const Object *me, Real range, UnsignedInt qualifiers);

	// --------------- inherited from Snapshot interface --------------
	void crc( Xfer *xfer );
	void xfer( Xfer *xfer );
	void loadPostProcess( void );

	// AI Groups -----------------------------------------------------------------------------------------------
	AIGroup *createGroup( void );					///< instantiate a new AI Group
	void destroyGroup( AIGroup *group );	///< destroy the given AI Group
	AIGroup *findGroup( UnsignedInt id );	///< return the AI Group with the given ID

	// Formation info
	enum FormationID getNextFormationID(void);

	static void parseAiDataDefinition( INI* ini );

	static Int s_enemyScans;								///< see getEnemyScanCountThisFrame
	static Real s_lastPathfindMS;						///< see getLastPathfindMS
	static Real s_lastPlayerUpdateMS;				///< see getLastPlayerUpdateMS
	const TAiData *getAiData() {return m_aiData;}

	/// The rung's knobs. Bounds-checked, so a bad index reads as the baseline rather than as memory.
	const AIDifficultyProfile *getDifficultyProfile( AISkillLevel level ) const;

	static void parseSkillLevel(INI *ini, void *instance, void *store, const void *userData);

	// Note: Does not work for things that do not have AI. (This is in AI.h, after all)
	static Fix getAdjustedVisionRangeForObject(const Object *object, Int factorsToConsider);

	static void parseSideInfo( INI* ini, void *instance, void *store, const void *userData );					///< Parse the image part of the INI file
	static void parseSkirmishBuildList( INI* ini, void *instance, void *store, const void *userData );					///< Parse the image part of the INI file
	static void parseStructure( INI* ini, void *instance, void *store, const void *userData );					///< Parse the image part of the INI file
	static void parseSkillSet( INI* ini, void *instance, void *store, const void *userData );					///< Parse the image part of the INI file
	static void parseScience( INI* ini, void *instance, void *store, const void *userData );					///< Parse the image part of the INI file

	inline UnsignedInt getNextGroupID( void ) { return ++m_nextGroupID; }

	/// One building placement per logic frame across every computer player: FALSE when one already went down on this frame.
	Bool claimBuildingPlacement( UnsignedInt frame );

protected:
	Pathfinder *m_pathfinder;							///< the pathfinding system
	std::list<AIGroup *> m_groupList;			///< the list of AIGroups
	TAiData *m_aiData;
	void newOverride(void);
	void addSideInfo(AISideInfo *info);
	
	UnsignedInt m_nextGroupID;
	FormationID m_nextFormationID;
	UnsignedInt m_lastBuildingPlacementFrame;	///< cleared by reset: a static here outlived the match and deferred the next match's first placement on one machine only
};

extern AI *TheAI;												///< the Artificial Intelligence singleton


class Waypoint;
class Team;
class Weapon;

// ws2def.h (pulled in by winsock2.h) defines AI_PASSIVE as a getaddrinfo hint
// flag, which turns the enumerator below into 0x00000001=-1.  Nothing here calls
// getaddrinfo, and the enumerator's value is written into save games and .map
// files, so the macro is what gives way.
#undef AI_PASSIVE

// Note - written out in save/load xfer and .map files, don't change these numbers.
enum AttitudeType { AI_SLEEP = -2, AI_PASSIVE=-1, AI_NORMAL=0, AI_ALERT=1, AI_AGGRESSIVE=2, AI_INVALID=3 };		///< AI "attitude" behavior modifiers

enum CommandSourceType;

typedef UnsignedInt CommandSourceMask;

#ifdef DEFINE_COMMANDSOURCEMASK_NAMES
static const char *TheCommandSourceMaskNames[] = 
{
	"FROM_PLAYER",
	"FROM_SCRIPT",
	"FROM_AI",
	"FROM_DOZER", //don't use this
	"DEFAULT_SWITCH_WEAPON", //unit will pick this weapon when normal logic fails.

	NULL
};
#endif

//------------------------------------------------------------------------------------------------------------

enum AICommandType	// Stored in save file, do not reorder/renumber.  jba.
{
	AICMD_NO_COMMAND = -1,
	AICMD_MOVE_TO_POSITION = 0,
	AICMD_MOVE_TO_OBJECT,
	AICMD_TIGHTEN_TO_POSITION,
	AICMD_MOVE_TO_POSITION_AND_EVACUATE,
	AICMD_MOVE_TO_POSITION_AND_EVACUATE_AND_EXIT,
	AICMD_IDLE,
	AICMD_FOLLOW_WAYPOINT_PATH,
	AICMD_FOLLOW_WAYPOINT_PATH_AS_TEAM,
	AICMD_FOLLOW_USER_PATH,	//Created by player in waypoint mode (this one will create little waypoint markers)
	AICMD_FOLLOW_PATH,
	AICMD_FOLLOW_EXITPRODUCTION_PATH,	// same as AICMD_FOLLOW_PATH, but only used when exiting your production facility.
	AICMD_ATTACK_OBJECT,
	AICMD_FORCE_ATTACK_OBJECT,
	AICMD_ATTACK_TEAM,
	AICMD_ATTACK_POSITION,
	AICMD_ATTACKMOVE_TO_POSITION,
	AICMD_ATTACKFOLLOW_WAYPOINT_PATH,
	AICMD_ATTACKFOLLOW_WAYPOINT_PATH_AS_TEAM,
	AICMD_HUNT,
	AICMD_REPAIR,
#ifdef ALLOW_SURRENDER
	AICMD_PICK_UP_PRISONER,
	AICMD_RETURN_PRISONERS,
#endif
	AICMD_RESUME_CONSTRUCTION,
	AICMD_GET_HEALED,
	AICMD_GET_REPAIRED,
	AICMD_ENTER,
	AICMD_DOCK,
	AICMD_EXIT,
	AICMD_EVACUATE,
	AICMD_EXECUTE_RAILED_TRANSPORT,
	AICMD_GO_PRONE,
	AICMD_GUARD_POSITION,
	AICMD_GUARD_OBJECT,
	AICMD_GUARD_AREA,
	AICMD_DEPLOY_ASSAULT_RETURN,
	AICMD_ATTACK_AREA,
	AICMD_HACK_INTERNET,
	AICMD_FACE_OBJECT,
	AICMD_FACE_POSITION,
	AICMD_RAPPEL_INTO,		// note that this applies to the rappeller.
	AICMD_COMBATDROP,			// note that this applies to the thing-being-rappelled-from.
	AICMD_COMMANDBUTTON_POS,
	AICMD_COMMANDBUTTON_OBJ,
	AICMD_COMMANDBUTTON,
	AICMD_WANDER,
	AICMD_WANDER_IN_PLACE,
	AICMD_PANIC,
	AICMD_BUSY,
	AICMD_FOLLOW_WAYPOINT_PATH_EXACT,
	AICMD_FOLLOW_WAYPOINT_PATH_AS_TEAM_EXACT,
	AICMD_MOVE_AWAY_FROM_UNIT,
	AICMD_FOLLOW_PATH_APPEND,
	AICMD_MOVE_TO_POSITION_EVEN_IF_SLEEPING,	// same as AICMD_MOVE_TO_POSITION, but even AI_SLEEP units respond.
	AICMD_GUARD_TUNNEL_NETWORK,
	AICMD_EVACUATE_INSTANTLY,
	AICMD_EXIT_INSTANTLY,
	AICMD_GUARD_RETALIATE,

	AICMD_NUM_COMMANDS	// keep last
};

struct AICommandParms
{
	AICommandType						m_cmd;
  CommandSourceType				m_cmdSource;
  Coord3D									m_pos;
  Object*									m_obj;
  Object*									m_otherObj;
  const Team*							m_team;
	std::vector<Coord3D>		m_coords;
  const Waypoint*         m_waypoint; 
  const PolygonTrigger*   m_polygon;     
  Int											m_intValue;       /// misc usage
  DamageInfo							m_damage;
	const CommandButton*		m_commandButton;
	Path*										m_path;

	AICommandParms(AICommandType cmd, CommandSourceType cmdSource);
};

class AICommandParmsStorage
{
private:
	AICommandType						m_cmd;
  CommandSourceType				m_cmdSource;
  Coord3D									m_pos;
  ObjectID								m_obj;
  ObjectID								m_otherObj;
  AsciiString							m_teamName;
	std::vector<Coord3D>		m_coords;
  const Waypoint*         m_waypoint; 
  const PolygonTrigger*   m_polygon;     
  Int											m_intValue;       /// misc usage
  DamageInfo							m_damage;
	const CommandButton*  	m_commandButton;
	Path*										m_path;

public:
	void store(const AICommandParms& parms);
	void reconstitute(AICommandParms& parms) const;
	void doXfer(Xfer *xfer);
	void setCommandSource(CommandSourceType cmdSource) { m_cmdSource = cmdSource; }
	AICommandType getCommandType() const { return m_cmd; }
	ObjectID getTargetObjectID() const { return m_obj; }
	const Coord3D& getTargetPosition() const { return m_pos; }
};

/**
	AI interface.  AIGroups, or Objects with an AIUpdate can be given these commands.
	
	NOTE NOTE NOTE: all of these may be overridden and possibly deferred by various AI classes,
	so they are NOT ALLOWED TO RETURN ANY VALUES, since the particular command issued might
	not be executed immediately...
*/
class AICommandInterface
{
public:
	
	virtual void aiDoCommand(const AICommandParms* parms) = 0;

	inline void aiMoveToPosition( const Coord3D *pos, CommandSourceType cmdSource )
	{
		AICommandParms parms(AICMD_MOVE_TO_POSITION, cmdSource);
		parms.m_pos = *pos;
		aiDoCommand(&parms);
	}

	inline void aiMoveToPositionEvenIfSleeping( const Coord3D *pos, CommandSourceType cmdSource )
	{
		AICommandParms parms(AICMD_MOVE_TO_POSITION_EVEN_IF_SLEEPING, cmdSource);
		parms.m_pos = *pos;
		aiDoCommand(&parms);
	}

	inline void aiMoveToObject( Object *obj, CommandSourceType cmdSource )
	{
		AICommandParms parms(AICMD_MOVE_TO_OBJECT, cmdSource);
		parms.m_obj = obj;
		aiDoCommand(&parms);
	}

	inline void aiTightenToPosition( const Coord3D *pos, CommandSourceType cmdSource )
	{
		AICommandParms parms(AICMD_TIGHTEN_TO_POSITION, cmdSource);
		parms.m_pos = *pos;
		aiDoCommand(&parms);
	}

	inline void aiMoveToAndEvacuate( const Coord3D *pos, CommandSourceType cmdSource )
	{
		AICommandParms parms(AICMD_MOVE_TO_POSITION_AND_EVACUATE, cmdSource);
		parms.m_pos = *pos;
		aiDoCommand(&parms);
	}

	inline void aiMoveToAndEvacuateAndExit( const Coord3D *pos, CommandSourceType cmdSource )
	{
		AICommandParms parms(AICMD_MOVE_TO_POSITION_AND_EVACUATE_AND_EXIT, cmdSource);
		parms.m_pos = *pos;
		aiDoCommand(&parms);
	}

	inline void aiIdle(CommandSourceType cmdSource)
	{
		AICommandParms parms(AICMD_IDLE, cmdSource);
		aiDoCommand(&parms);
	}

	inline void aiBusy(CommandSourceType cmdSource)
	{
		AICommandParms parms(AICMD_BUSY, cmdSource);
		aiDoCommand(&parms);
	}

	inline void aiFollowWaypointPath( const Waypoint *way, CommandSourceType cmdSource )
	{
		AICommandParms parms(AICMD_FOLLOW_WAYPOINT_PATH, cmdSource);
		parms.m_waypoint = way;
		aiDoCommand(&parms);
	}

	inline void aiFollowWaypointPathExact( const Waypoint *way, CommandSourceType cmdSource )
	{
		AICommandParms parms(AICMD_FOLLOW_WAYPOINT_PATH_EXACT, cmdSource);
		parms.m_waypoint = way;
		aiDoCommand(&parms);
	}

	inline void aiFollowWaypointPathAsTeam( const Waypoint *way, CommandSourceType cmdSource )
	{
		AICommandParms parms(AICMD_FOLLOW_WAYPOINT_PATH_AS_TEAM, cmdSource);
		parms.m_waypoint = way;
		aiDoCommand(&parms);
	}

	inline void aiFollowWaypointPathExactAsTeam( const Waypoint *way, CommandSourceType cmdSource )
	{
		AICommandParms parms(AICMD_FOLLOW_WAYPOINT_PATH_AS_TEAM_EXACT, cmdSource);
		parms.m_waypoint = way;
		aiDoCommand(&parms);
	}

	// The path is taken, not copied: every caller hands over a local it is done with.
	inline void aiFollowExitProductionPath( std::vector<Coord3D>* path, Object *ignoreObject, CommandSourceType cmdSource )
	{
		AICommandParms parms(AICMD_FOLLOW_EXITPRODUCTION_PATH, cmdSource);
		parms.m_coords.swap( *path );
		parms.m_obj = ignoreObject;
		aiDoCommand(&parms);
	}

	inline void aiFollowPath( std::vector<Coord3D>* path, Object *ignoreObject, CommandSourceType cmdSource )
	{
		AICommandParms parms(AICMD_FOLLOW_PATH, cmdSource);
		parms.m_coords.swap( *path );
		parms.m_obj = ignoreObject;
		aiDoCommand(&parms);
	}

	inline void aiFollowPathAppend( const Coord3D* pos, CommandSourceType cmdSource )
	{
		AICommandParms parms(AICMD_FOLLOW_PATH_APPEND, cmdSource);
		parms.m_pos = *pos;
		aiDoCommand(&parms);
	}

	inline void aiAttackObject( Object *victim, Int maxShotsToFire, CommandSourceType cmdSource )
	{
		AICommandParms parms(AICMD_ATTACK_OBJECT, cmdSource);
		parms.m_obj = victim;
		parms.m_intValue = maxShotsToFire;
		aiDoCommand(&parms);
	}

	inline void aiForceAttackObject( Object *victim, Int maxShotsToFire, CommandSourceType cmdSource )
	{
		AICommandParms parms(AICMD_FORCE_ATTACK_OBJECT, cmdSource);
		parms.m_obj = victim;
		parms.m_intValue = maxShotsToFire;
		aiDoCommand(&parms);
	}

	inline void aiGuardRetaliate( Object *victim, const Coord3D *pos, Int maxShotsToFire, CommandSourceType cmdSource )
	{
		AICommandParms parms(AICMD_GUARD_RETALIATE, cmdSource);
		parms.m_obj = victim;
		parms.m_pos = *pos;
		parms.m_intValue = maxShotsToFire;
		aiDoCommand(&parms);
	}

	inline void aiAttackTeam( const Team *team, Int maxShotsToFire, CommandSourceType cmdSource )
	{
		AICommandParms parms(AICMD_ATTACK_TEAM, cmdSource);
		parms.m_team = team;
		parms.m_intValue = maxShotsToFire;
		aiDoCommand(&parms);
	}

	inline void aiAttackPosition( const Coord3D *pos, Int maxShotsToFire, CommandSourceType cmdSource )
	{
		AICommandParms parms(AICMD_ATTACK_POSITION, cmdSource);
		parms.m_pos = *pos;
		parms.m_intValue = maxShotsToFire;
		aiDoCommand(&parms);
	}

	inline void aiAttackMoveToPosition( const Coord3D *pos, Int maxShotsToFire, CommandSourceType cmdSource )
	{
		AICommandParms parms(AICMD_ATTACKMOVE_TO_POSITION, cmdSource);
		parms.m_pos = *pos;
		parms.m_intValue = maxShotsToFire;
		aiDoCommand(&parms);
	}

	inline void aiAttackFollowWaypointPath( const Waypoint *way, Int maxShotsToFire, CommandSourceType cmdSource )
	{
		AICommandParms parms(AICMD_ATTACKFOLLOW_WAYPOINT_PATH, cmdSource);
		parms.m_waypoint = way;
		parms.m_intValue = maxShotsToFire;
		aiDoCommand(&parms);
	}

	inline void aiAttackFollowWaypointPathAsTeam( const Waypoint *way, Int maxShotsToFire, CommandSourceType cmdSource )
	{
		AICommandParms parms(AICMD_ATTACKFOLLOW_WAYPOINT_PATH_AS_TEAM, cmdSource);
		parms.m_waypoint = way;
		parms.m_intValue = maxShotsToFire;
		aiDoCommand(&parms);
	}

	inline void aiHunt( CommandSourceType cmdSource )
	{
		AICommandParms parms(AICMD_HUNT, cmdSource);
		aiDoCommand(&parms);
	}

	inline void aiAttackArea( const PolygonTrigger *areaToGuard, CommandSourceType cmdSource )
	{
		AICommandParms parms(AICMD_ATTACK_AREA, cmdSource);
		parms.m_polygon = areaToGuard;
		aiDoCommand(&parms);
	}

	inline void aiRepair( Object *obj, CommandSourceType cmdSource )
	{
		AICommandParms parms(AICMD_REPAIR, cmdSource);
		parms.m_obj = obj;
		aiDoCommand(&parms);
	}

#ifdef ALLOW_SURRENDER
	inline void aiPickUpPrisoner( Object *obj, CommandSourceType cmdSource )
	{
		AICommandParms parms( AICMD_PICK_UP_PRISONER, cmdSource );
		parms.m_obj = obj;
		aiDoCommand( &parms );	
	}
#endif

#ifdef ALLOW_SURRENDER
	inline void aiReturnPrisoners( Object *prison, CommandSourceType cmdSource )
	{
		AICommandParms parms( AICMD_RETURN_PRISONERS, cmdSource );
		parms.m_obj = prison;
		aiDoCommand( &parms );
	}
#endif

	inline void aiResumeConstruction( Object *obj, CommandSourceType cmdSource )
	{
		AICommandParms parms(AICMD_RESUME_CONSTRUCTION, cmdSource);
		parms.m_obj = obj;
		aiDoCommand(&parms);
	}

	inline void aiGetHealed( Object *healDepot, CommandSourceType cmdSource )
	{
		AICommandParms parms(AICMD_GET_HEALED, cmdSource);
		parms.m_obj = healDepot;
		aiDoCommand(&parms);
	}

	inline void aiGetRepaired( Object *repairDepot, CommandSourceType cmdSource )
	{
		AICommandParms parms(AICMD_GET_REPAIRED, cmdSource);
		parms.m_obj = repairDepot;
		aiDoCommand(&parms);
	}

	inline void aiEnter( Object *obj, CommandSourceType cmdSource )
	{
		AICommandParms parms(AICMD_ENTER, cmdSource);
		parms.m_obj = obj;
		aiDoCommand(&parms);
	}

	inline void aiDock( Object *obj, CommandSourceType cmdSource )
	{
		AICommandParms parms(AICMD_DOCK, cmdSource);
		parms.m_obj = obj;
		aiDoCommand(&parms);
	}

	inline void aiExit( Object *objectToExit, CommandSourceType cmdSource )
	{
		AICommandParms parms(AICMD_EXIT, cmdSource);
		parms.m_obj = objectToExit;
		aiDoCommand(&parms);
	}

	inline void aiExitInstantly( Object *objectToExit, CommandSourceType cmdSource )
	{
		AICommandParms parms(AICMD_EXIT_INSTANTLY, cmdSource);
		parms.m_obj = objectToExit;
		aiDoCommand(&parms);
	}

	inline void aiEvacuate( Bool exposeStealthUnits, CommandSourceType cmdSource )
	{
		AICommandParms parms(AICMD_EVACUATE, cmdSource);
		if( exposeStealthUnits )
			parms.m_intValue = 1;
		else
			parms.m_intValue = 0;
		aiDoCommand(&parms);
	}
	
	inline void aiEvacuateInstantly( Bool exposeStealthUnits, CommandSourceType cmdSource )
	{
		AICommandParms parms(AICMD_EVACUATE_INSTANTLY, cmdSource);
		if( exposeStealthUnits )
			parms.m_intValue = 1;
		else
			parms.m_intValue = 0;
		aiDoCommand(&parms);
	}

	inline void aiExecuteRailedTransport( CommandSourceType cmdSource )
	{
		AICommandParms parms( AICMD_EXECUTE_RAILED_TRANSPORT, cmdSource );
		aiDoCommand( &parms );
	}

	inline void aiGoProne( const DamageInfo *damageInfo, CommandSourceType cmdSource )
	{
		AICommandParms parms(AICMD_GO_PRONE, cmdSource);
		parms.m_damage = *damageInfo;
		aiDoCommand(&parms);
	}

	inline void aiGuardPosition( const Coord3D *pos, GuardMode guardMode, CommandSourceType cmdSource )
	{
		AICommandParms parms(AICMD_GUARD_POSITION, cmdSource);
		parms.m_pos = *pos;
		parms.m_intValue = guardMode;
		aiDoCommand(&parms);
	}

	inline void aiGuardObject( Object *objToGuard, GuardMode guardMode, CommandSourceType cmdSource )
	{
		AICommandParms parms(AICMD_GUARD_OBJECT, cmdSource);
		parms.m_obj = objToGuard;
		parms.m_intValue = guardMode;
		aiDoCommand(&parms);
	}

	inline void aiGuardArea( const PolygonTrigger *areaToGuard, GuardMode guardMode, CommandSourceType cmdSource )
	{
		AICommandParms parms(AICMD_GUARD_AREA, cmdSource);
		parms.m_polygon = areaToGuard;
		parms.m_intValue = guardMode;
		aiDoCommand(&parms);
	}

	inline void aiGuardTunnelNetwork( GuardMode guardMode, CommandSourceType cmdSource )
	{
		AICommandParms parms(AICMD_GUARD_TUNNEL_NETWORK, cmdSource);
		parms.m_intValue = guardMode;
		aiDoCommand(&parms);
	}

	inline void aiHackInternet( CommandSourceType cmdSource )
	{
		AICommandParms parms(AICMD_HACK_INTERNET, cmdSource);
		aiDoCommand(&parms);
	}

	inline void aiFaceObject( Object *target, CommandSourceType cmdSource )
	{
		AICommandParms parms(AICMD_FACE_OBJECT, cmdSource);
		parms.m_obj = target;
		aiDoCommand(&parms);
	}

	inline void aiFacePosition( const Coord3D *pos, CommandSourceType cmdSource )
	{
		AICommandParms parms(AICMD_FACE_POSITION, cmdSource);
		parms.m_pos = *pos;
		aiDoCommand(&parms);
	}

	inline void aiRappelInto( Object *target, const Coord3D& pos, CommandSourceType cmdSource )
	{
		AICommandParms parms(AICMD_RAPPEL_INTO, cmdSource);
		parms.m_obj = target;
		parms.m_pos = pos;
		aiDoCommand(&parms);
	}

	inline void aiCombatDrop( Object *target, const Coord3D& pos, CommandSourceType cmdSource )
	{
		AICommandParms parms(AICMD_COMBATDROP, cmdSource);
		parms.m_obj = target;
		parms.m_pos = pos;
		aiDoCommand(&parms);
	}

	inline void aiDoCommandButton( const CommandButton *commandButton, CommandSourceType cmdSource )
	{
		AICommandParms parms(AICMD_COMMANDBUTTON, cmdSource);
		parms.m_commandButton = commandButton;
		aiDoCommand(&parms);
	}

	inline void aiDoCommandButtonAtPosition( const CommandButton *commandButton, const Coord3D *pos, CommandSourceType cmdSource )
	{
		AICommandParms parms(AICMD_COMMANDBUTTON_POS, cmdSource);
		parms.m_pos = *pos;
		parms.m_commandButton = commandButton;
		aiDoCommand(&parms);
	}
	
	inline void aiDoCommandButtonAtObject( const CommandButton *commandButton, Object *obj, CommandSourceType cmdSource )
	{
		AICommandParms parms(AICMD_COMMANDBUTTON_OBJ, cmdSource);
		parms.m_obj = obj;
		parms.m_commandButton = commandButton;
		aiDoCommand(&parms);
	}

	inline void aiMoveAwayFromUnit( Object *obj, CommandSourceType cmdSource )
	{
		AICommandParms parms(AICMD_MOVE_AWAY_FROM_UNIT, cmdSource);
		parms.m_obj = obj;
		aiDoCommand(&parms);
	}

	inline void aiWander( const Waypoint *way, CommandSourceType cmdSource )
	{
		AICommandParms parms(AICMD_WANDER, cmdSource);
		parms.m_waypoint = way;
		aiDoCommand(&parms);
	}

	inline void aiWanderInPlace(CommandSourceType cmdSource)
	{
		AICommandParms parms(AICMD_WANDER_IN_PLACE, cmdSource);
		aiDoCommand(&parms);
	}

	inline void aiPanic( const Waypoint *way, CommandSourceType cmdSource )
	{
		AICommandParms parms(AICMD_PANIC, cmdSource);
		parms.m_waypoint = way;
		aiDoCommand(&parms);
	}

};


//------------------------------------------------------------------------------------------------------------
/**
 * An "AIGroup" is a simple collection of AI objects, used by the AI
 * for such things as Group Pathfinding.
 */
class AIGroup : public MemoryPoolObject, public Snapshot
{
private:
	void groupAttackObjectPrivate( Bool forced, Object *victim, Int maxShotsToFire, CommandSourceType cmdSource );					///< attack given object

public:

	// --------------- inherited from Snapshot interface --------------
	void crc( Xfer *xfer );
	void xfer( Xfer *xfer );
	void loadPostProcess( void );

	void groupMoveToPosition( const Coord3D *pos, Bool addWaypoint, CommandSourceType cmdSource );
	void groupMoveToAndEvacuate( const Coord3D *pos, CommandSourceType cmdSource );			///< move to given position(s)
	void groupMoveToAndEvacuateAndExit( const Coord3D *pos, CommandSourceType cmdSource );			///< move to given position & unload transport.
	void groupIdle(CommandSourceType cmdSource);						///< Enter idle state.
	void groupScatter(CommandSourceType cmdSource);						///< Enter idle state.
	void groupCreateFormation(CommandSourceType cmdSource); ///< Make the current selection a user formation.
	void groupTightenToPosition( const Coord3D *pos, Bool addWaypoint, CommandSourceType cmdSource );			///< move to given position(s)
	void groupFollowWaypointPath( const Waypoint *way, CommandSourceType cmdSource );///< start following the path from the given point
	void groupFollowWaypointPathAsTeam( const Waypoint *way, CommandSourceType cmdSource );///< start following the path from the given point
	void groupFollowWaypointPathExact( const Waypoint *way, CommandSourceType cmdSource );///< start following the path from the given point
	void groupFollowWaypointPathAsTeamExact( const Waypoint *way, CommandSourceType cmdSource );///< start following the path from the given point
	void groupFollowPath( const std::vector<Coord3D>* path, Object *ignoreObject, CommandSourceType cmdSource );///< follow the path defined by the given array of points
	void groupAttackObject( Object *victim, Int maxShotsToFire, CommandSourceType cmdSource )
	{
		groupAttackObjectPrivate(false, victim, maxShotsToFire, cmdSource);
	}
	void groupForceAttackObject( Object *victim, Int maxShotsToFire, CommandSourceType cmdSource )
	{
		groupAttackObjectPrivate(true, victim, maxShotsToFire, cmdSource);
	}
	void groupAttackTeam( const Team *team, Int maxShotsToFire, CommandSourceType cmdSource );							///< attack the given team
	void groupAttackPosition( const Coord3D *pos, Int maxShotsToFire, CommandSourceType cmdSource );						///< attack given spot
	void groupAttackMoveToPosition( const Coord3D *pos, Int maxShotsToFire, CommandSourceType cmdSource, Bool matchSpeeds = TRUE );	///< Attack move to the location
	Bool getMatchSpeeds( void ) const { return m_matchSpeeds; }		///< does the last attack move hold everyone to the slowest member?
	void groupHunt( CommandSourceType cmdSource );														///< begin "seek and destroy"
	void groupRepair( Object *obj, CommandSourceType cmdSource );						///< repair the given object
	void groupResumeConstruction( Object *obj, CommandSourceType cmdSource );	///< resume construction on the object
	void groupGetHealed( Object *healDepot, CommandSourceType cmdSource );		///< go get healed at the heal depot
	void groupGetRepaired( Object *repairDepot, CommandSourceType cmdSource );///< go get repaired at the repair depot
	void groupEnter( Object *obj, CommandSourceType cmdSource );							///< enter the given object
	void groupDock( Object *obj, CommandSourceType cmdSource );							///< get near given object and wait for enter clearance
	void groupExit( Object *objectToExit, CommandSourceType cmdSource );			///< get out of this Object
	void groupEvacuate( CommandSourceType cmdSource );												///< empty its contents
	void groupExecuteRailedTransport( CommandSourceType cmdSource );					///< execute railed transport events
	void groupGoProne( const DamageInfo *damageInfo, CommandSourceType cmdSource );												///< life altering state change, if this AI can do it
	void groupGuardPosition( const Coord3D *pos, GuardMode guardMode, CommandSourceType cmdSource );						///< guard the given spot
	void groupGuardObject( Object *objToGuard, GuardMode guardMode, CommandSourceType cmdSource );			///< guard an object
	void groupGuardArea( const PolygonTrigger *areaToGuard, GuardMode guardMode, CommandSourceType cmdSource ); ///< guard an area
	void groupAttackArea( const PolygonTrigger *areaToGuard, CommandSourceType cmdSource ); ///< guard an area
	void groupHackInternet( CommandSourceType cmdSource );				///< Begin hacking the internet for free cash from the heavens.
	void groupDoSpecialPower( UnsignedInt specialPowerID, UnsignedInt commandOptions );
	void groupDoSpecialPowerAtObject( UnsignedInt specialPowerID, Object *object, UnsignedInt commandOptions ); 
	void groupDoSpecialPowerAtLocation( UnsignedInt specialPowerID, const Coord3D *location, Real angle, const Object *object, UnsignedInt commandOptions );
#ifdef ALLOW_SURRENDER
	void groupSurrender( const Object *objWeSurrenderedTo, Bool surrender, CommandSourceType cmdSource );
#endif
	void groupCheer( CommandSourceType cmdSource );
	void groupSell( CommandSourceType cmdSource );
	void groupToggleOvercharge( CommandSourceType cmdSource );
#ifdef ALLOW_SURRENDER
	void groupPickUpPrisoner( Object *prisoner, CommandSourceType cmdSource );	///< pick up prisoner
	void groupReturnToPrison( Object *prison, CommandSourceType cmdSource );		///< return to prison
#endif
	void groupCombatDrop( Object *target, const Coord3D& pos, CommandSourceType cmdSource );
	void groupDoCommandButton( const CommandButton *commandButton, CommandSourceType cmdSource );
	void groupDoCommandButtonAtPosition( const CommandButton *commandButton, const Coord3D *pos, CommandSourceType cmdSource );
	void groupDoCommandButtonUsingWaypoints( const CommandButton *commandButton, const Waypoint *way, CommandSourceType cmdSource );
	void groupDoCommandButtonAtObject( const CommandButton *commandButton, Object *obj, CommandSourceType cmdSource );
	void groupSetEmoticon( const AsciiString &name, Int duration );
	void groupOverrideSpecialPowerDestination( SpecialPowerType spType, const Coord3D *loc, CommandSourceType cmdSource );

	void setAttitude( AttitudeType tude );	///< set the behavior modifier for this agent
	AttitudeType getAttitude( void ) const;				///< get the current behavior modifier state	

	Bool isIdle() const;
	//Definition of busy -- when explicitly in the busy state. Moving or attacking is not considered busy!
	Bool isBusy() const;
	Bool isGroupAiDead() const;

	// Returns an object that can perform the special power. Useful for making queries on the Action Manager
	Object *getSpecialPowerSourceObject( UnsignedInt specialPowerID );

	// Returns an object that has a command button for the GUI command type.
	Object *getCommandButtonSourceObject( GUICommandType type );

	// Group methods --------------------------------------------------------------------------------
	Bool isMember( Object *obj );						///< return true if object is in this group

	Real getSpeed( void );									///< return the speed of the group's slowest member
	Bool getCenter( Coord3D *center );				///< compute centroid of group
	Bool getMinMaxAndCenter( Coord2D *min, Coord2D *max, Coord3D *center );
	void computeIndividualDestination( Coord3D *dest, const Coord3D *groupDest, 
		Object *obj, const Coord3D *center, Bool isFormation ); ///< compute destination of individual object, based on group destination
	Int getCount( void );										///< return the number of objects in the group
	Bool isEmpty( void ) const;								///< returns true if the group has no members
	void queueUpgrade( const UpgradeTemplate *upgrade );	///< queue an upgrade

	void add( Object *obj );								///< add object to group
	
	// returns true if remove destroyed the group for us.
	Bool remove( Object *obj);
	
	// If the group contains any objects not owned by ownerPlayer, return TRUE.
	Bool containsAnyObjectsNotOwnedByPlayer( const Player *ownerPlayer );

	// Remove any objects that aren't owned by the player, and return true if the group was destroyed due to emptiness
	Bool removeAnyObjectsNotOwnedByPlayer( const Player *ownerPlayer );
	
	UnsignedInt getID( void );
		
	///< get IDs for every object in this group
	const VecObjectID& getAllIDs ( void ) const;

	void recomputeGroupSpeed() { m_dirty = true; }

	void setMineClearingDetail( Bool set );
	Bool setWeaponLockForGroup( WeaponSlotType weaponSlot, WeaponLockType lockType ); ///< Set the groups' weapon choice.  
	void releaseWeaponLockForGroup(WeaponLockType lockType);///< Clear each guys weapon choice
	void setWeaponSetFlag( WeaponSetType wst );

protected:
	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE( AIGroup, "AIGroupPool" );		///< @todo Set real numbers for mem alloc

	ListObjectPtrIt internalRemove(ListObjectPtrIt iterToRemove);

	Bool friend_moveInfantryToPos( const Coord3D *pos, CommandSourceType cmdSource );
	Bool friend_moveVehicleToPos( const Coord3D *pos, CommandSourceType cmdSource );
	void friend_moveFormationToPos( const Coord3D *pos, CommandSourceType cmdSource );
	Bool friend_computeGroundPath( const Coord3D *pos, CommandSourceType cmdSource );

private:
	// AIGroups must be created through TheAI->createGroup()
	friend class AI;
	AIGroup( void );

	void recompute( void );									///< recompute various group info, such as speed, leader, etc

	ListObjectPtr m_memberList;							///< the list of member Objects
	UnsignedInt	m_memberListSize;	 					///< the size of the list of member Objects

	Real m_speed;														///< maximum speed of group (slowest member)
	Bool m_matchSpeeds;											///< last attack move asked for one shared speed (see groupAttackMoveToPosition)
	Bool m_dirty;														///< "dirty bit" - if true then group speed, leader, needs recompute

	UnsignedInt m_id;												///< the unique ID of this group
	Path *m_groundPath;											///< Group ground path.
	
	mutable VecObjectID	m_lastRequestedIDList;			///< this is used so we can return by reference, saving a copy
};


#endif // _AI_H_
