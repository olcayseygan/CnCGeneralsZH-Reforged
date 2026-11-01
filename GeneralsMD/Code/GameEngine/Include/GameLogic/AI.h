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
// Modified 2026 by İlyas Akın for the macOS/Linux port; see NOTICE.md and the git history.

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
#include "Common/GameCommon.h"		// LOGICFRAMES_PER_SECOND
#include "GameLogic/Damage.h"
#include "Common/STLTypedefs.h"

class AIGroup;
class AIInfluenceMap;
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

enum GUICommandType : Int;
enum HackerAttackMode : Int;
enum WeaponSetType : Int;
enum WeaponLockType : Int;
enum SpecialPowerType : Int;

typedef std::vector<ObjectID> VecObjectID;
typedef VecObjectID::iterator VecObjectIDIt;

typedef std::list<Object *> ListObjectPtr;
typedef ListObjectPtr::iterator ListObjectPtrIt;

enum AIDebugOptions : Int
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
	AsciiString m_spotter;				///< the unit sent along with a gun that fires past its own sight; empty for a side without one
	std::vector<AsciiString> m_artillery;	///< the side's guns that fire past their own sight, one of them added to a share of its attack teams (AIDifficultyProfile m_artilleryShare)
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
	AISKILL_EASY = 0,			///< slow and brave: looks around, answers late, never retreats or counters
	AISKILL_MEDIUM,				///< half a counter, spends past 6000, unit retreat, expands on its own
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
/** The personality a lobby seat picked for a computer player: a third axis beside the rung and
	* the role, saying how it plays rather than how well.  Chosen per seat, carried in the slot
	* string and the replay header, and NORMAL is today's AI to the bit.  RANDOM is only ever a
	* lobby value: the AI turns it into one of the others from the logic random stream at the start
	* of the match.  The order is the lobby's and the slot string's; append, never reorder. */
//-------------------------------------------------------------------------------------------------
enum AIPersonality
{
	AIPERSONA_NORMAL = 0,
	AIPERSONA_ATTACKER,			///< early, frequent smaller waves, never banks
	AIPERSONA_TURTLE,				///< defences and walls first, few late big waves, retreats on losses
	AIPERSONA_ECONOMIST,		///< income and its upgrades first, surplus money to allies
	AIPERSONA_AIR,					///< airfields and aircraft, less of it against heavy anti-air
	AIPERSONA_RUSHER,				///< everything into one attack in the first minutes
	AIPERSONA_RAIDER,				///< fast units at gatherers, workers and depots, around the main army
	AIPERSONA_TECH,					///< tech, general powers and the superweapon first, defends until then
	AIPERSONA_INFANTRY,			///< cheap mass infantry, garrisons buildings
	AIPERSONA_ARMOR,				///< a slow tank-heavy army
	AIPERSONA_SNEAKY,				///< stealth, sabotage and hacking, flank routes
	AIPERSONA_OPPORTUNIST,	///< the weakest enemy, and whoever is already in a fight
	AIPERSONA_SUPPORTER,		///< the ally's target, the ally's base when it is hit
	AIPERSONA_COUNTER,			///< builds the answer to what the enemy fields
	AIPERSONA_RANDOM,				///< one of the above, drawn at the start of the match

	AIPERSONA_COUNT
};

/** The name a personality goes by in logs, the -aipersona switch and its string labels
	* ("GUI:AIPersona<Name>"), and the reverse; AIPERSONA_NORMAL for a name it does not know. */
const char *aiPersonalityName( AIPersonality persona );
AIPersonality aiPersonalityFromName( const char *name );

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
	Real	m_scoutIntervalSeconds;					///< how often the scout is re-tasked; every rung scouts
	Int		m_maxScouts;
	Real	m_decisionIntervalSeconds;			///< how often it re-evaluates - the AI's APM, and so how late it answers
																				///< what it saw: a separate reaction delay stood here, read by nothing

	// Decision quality
	Real	m_counterCompositionWeight;			///< 0 = ignore what the enemy fields (EA's behaviour)
	Bool	m_massBeforeAttacking;
	Real	m_retreatTtkRatio;							///< allied time-to-kill / enemy TTK below which it pulls back
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

	// Choosing fights: what it remembers of the enemy army, not only what is in sight
	Bool	m_engageGate;										///< weigh a fight by unit against unit, not money alone; nothing leaves home on its own
	Bool	m_answerArmy;										///< remember the enemy army it has seen, and train the answer to the biggest part of it
	Bool	m_massUnit;											///< pick one unit the enemy has no answer to, build a fist of it and hit with it

	// Spending: how big a bank it keeps, and what it buys past it (AIPlayer::doSpending)
	Real	m_bankTargetSeconds;						///< the bank it keeps, in seconds of its own income; 0 = no purchase queue and no spend rule
	Int		m_bankTargetFloor;							///< ... and never less than this, which is all of it before any income is measured
	Real	m_upgradePayoff;								///< an upgrade is bought when what it improves is worth this many times its price, half that past the bank target
	Real	m_upgradeStartSeconds;					///< and not before this far into the match
	Real	m_upgradeNonCombatWeight;				///< what a building or an unarmed unit counts for next to a fighting unit when an upgrade is weighed
	Real	m_surplusShare;									///< of the income over aiSurplusBudget's window, how much a bank over target may train as extra units
	Real	m_factoryBacklogSeconds;				///< the window over which every finished factory of a kind has to be full on AI_FACTORY_FULL_PERCENT of the spend passes before another of that kind goes up (aiWantsAnotherFactory)

	// Pushing: when the army first leaves home, and how small a force may go
	Real	m_firstPushSeconds;							///< no attack before this, nor before the first drop zone or black market stands (AI_PURCHASE_EARLY_FRAMES at most)
	Real	m_minAttackPower;								///< parked teams worth less than this wait for company, up to the wave's longest wait
	Real	m_siegeTrail;										///< artillery out with a wave keeps this far behind its front unit; 0 = walks with it

	// Superweapons
	Real	m_beamSteerSeconds;							///< how often a firing Particle Cannon's beam is driven onto the richest ground near it; 0 = the retail S curve
	Real	m_strikeEconomyWeight;					///< what a factory, power plant, superweapon, supply centre, black market, hacker or strategy centre is worth to a strike on top of its price, in prices; 0 = price alone
	Real	m_strikeThreatWeight;						///< what an armed unit or gun is worth to a strike on top of its price, in combat power; 0 = price alone, and his army is a candidate only when none of his buildings is
	Int		m_superweaponDefenses;					///< finished base defences each superweapon asks for (Player::canBuildMoreOfType); 0 = none

	// Building sites: when a placed building is given up, and whether idle dozers walk ahead to the next one
	Real	m_siteStallSeconds;							///< a site away from home that has not moved forward this long is cancelled and refunded (aiWatchSite)
	Int		m_siteBuildersLost;							///< ... and any site whose builders died this many times with nothing built in between
	Real	m_siteRetrySeconds;							///< how long a cancelled site's spot is left alone before it is placed again
	Bool	m_stageDozers;									///< an idle dozer walks to the site the base builder waits on money for

	// Artillery that fires past its own sight
	Int		m_artillerySpotters;						///< the side's spotters (AISideInfo m_spotter) added to a team that brings such a gun and no spotter
	Real	m_artilleryShare;								///< the share of a side's attack teams with no such gun that are given one of its own (AISideInfo m_artillery)

	// Coming under fire (AIPlayer::answerFire): what it does about a gun that is hitting it
	Real	m_influenceSeconds;							///< how old its map of what can shoot where may be when it is read
	Real	m_fireLookSeconds;							///< how often it looks at who is being hit, and so how late it answers
	Real	m_fireWordRadius;								///< how far from a unit hit the rest of the group goes with it; 0 = each answers its own hit alone
	Real	m_fireChargeOdds;								///< ours near the unit have to be worth this many times what covers the gun before they go for it
	Real	m_fireMemorySeconds;						///< how long a gun that hit it stays on its map after it has lost sight of it

	// Enemies in the base that have not hit anything yet (AIPlayer::baseSquatter)
	Real	m_baseWatchSeconds;							///< how often it looks for them, and so how late it answers; 0 = only once something has been hit
	Real	m_baseWatchOdds;								///< what it trains and sends at them, in times their price

	// A superweapon going up, the word every seat hears (AIPlayer::dodgeStrike)
	Real	m_strikeDodgeSeconds;						///< how long after that word its helicopters leave where they stand; 0 = they stay
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
	Int m_infantryCount;	///< how many known enemy soldiers, and ground vehicles, by head rather than threat
	Int m_vehicleCount;

	AIEnemyComposition() : m_air(0.0f), m_armour(0.0f), m_infantry(0.0f), m_stealth(0.0f),
												 m_totalThreat(0.0f), m_infantryCount(0), m_vehicleCount(0) {}
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
/** An enemy gun the AI knows of, and how far it reaches at anything and at aircraft. */
//-------------------------------------------------------------------------------------------------
struct AIKnownGun
{
	Real m_x;
	Real m_y;
	Real m_reach;
	Real m_airReach;		///< 0 for a gun that cannot aim at a helicopter
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

/** How one army's mix trades against another's, money for money: 1 an even trade, above 1 when my
	* money kills theirs faster than theirs kills mine, clamped to 1/16..16.  score holds myKinds rows of
	* theirKinds entries, aiMatchupScore of my kind i against their kind j, and each side's values weigh
	* its kinds.  Ten Overlords against ten Crusaders cost the same and are not the same fight. */
Real aiArmyAdvantage( const Real *myValue, Int myKinds, const Real *theirValue, Int theirKinds, const Real *score );

/** How the exchange is going, as the ratio of how long this force lasts to how long it needs to
	* kill what is shooting at it.  Below 1 it is losing; below the rung's retreatTtkRatio it should
	* break off.  This is Sins of a Solar Empire's aiRetreatThreshold, and it is the right metric
	* rather than a health percentage: a unit at 20% health that still out-damages what is shooting
	* it should stay, and a full-health one being melted should not.
	*
	* 'power' is a stand-in for damage per second - the threat value the data already carries. */
Real aiRetreatRatio( Real myHealth, Real myPower, Real enemyHealth, Real enemyPower );

/** Where a force losing a fight stops: the first spot on its line home, or 45 or 90 degrees off it, that
	* none of the fight's guns reach (x, y, reach triples) and that map, when given, has no enemy gun on,
	* plus one step, on the ground (bounds: low x, low y, high x, high y).  FALSE and home when there is none
	* short of it. */
Bool aiRetreatFallbackPoint( Real fromX, Real fromY, Real homeX, Real homeY, Real step,
														 const Real *guns, Int gunCount, const Real *bounds, Real *outX, Real *outY,
														 const AIInfluenceMap *map = NULL );

/** What a unit that has just been hit does about the gun that hit it. */
enum AIFireAnswer
{
	AIFIRE_CARRY_ON,		///< it is already in a gunfight, or may not leave this one
	AIFIRE_SHOOT_BACK,	///< the gun is in its reach: that is what it shoots now
	AIFIRE_GO_KILL,			///< out of reach, and ours near it are enough: they go and get it
	AIFIRE_LEAVE				///< out of reach, and ours are not enough: off the ground it covers
};

/** The rule.  A unit already shooting the gun or some other gun stays on it, and the retreat judges that
	* fight.  One with the gun in reach turns on it.  Out of reach it goes when it can get at the gun and ours
	* are not outgunned at the gun's own spot, and otherwise leaves if it may.  One that may not leave stays on
	* the order it has, or with none, idle under the shells, goes for the gun at any odds and leaves if it cannot
	* get at it.  ours and cover are in aiCombatPower's currency; odds is the rung's. */
AIFireAnswer aiAnswerFire( Bool onTheGun, Bool fightingAGun, Bool canShootNow, Bool canGetAtIt, Real ours, Real cover, Real odds, Bool mayLeave, Bool idle );

/** The odds a unit reads a gun at.  One already sent at it breaks off only well past the odds that sent it, so
	* a reading either side of them does not turn it round halfway. */
Real aiFireOddsNow( Real odds, Bool sentAtIt, Real margin );

/** A unit that leaves a gun it was sent at has tried it.  Going back on the next good reading walked it to and
	* fro for as long as the fight read between the odds that send and the odds that hold, so its hold ends at
	* home. */
Bool aiFireMayGoBack( Bool sentAtIt );

/** A unit stops counting as sent at a gun once the retreat has it, or a step of its own took it away (a hurt
	* unit pulled out, a kite), or the errand has run past the longest it can take.  Left on it, the hurt unit was
	* walked back to the gun from its safe spot. */
Bool aiFireAnswerOver( Bool retreatHasIt, Bool steppedAway, UnsignedInt framesOnIt, UnsignedInt maxFrames );

/** A unit holding after it left a fight goes free.  The retreat ends the hold of those it looks at, and only
	* one it has lost track of runs to staleFrames.  Nothing ends the others (a rung with no retreat, the default
	* team): they hold holdFrames and go free once they have stopped walking, for the call-ups to have them. */
Bool aiHolderLetGo( Bool retreatSeesIt, UnsignedInt heldFrames, Bool moving, UnsignedInt holdFrames, UnsignedInt staleFrames );

/** Where a helicopter at (x, y) goes when it hears a launch: reach away from the middle of the group it hangs
	* with (clumpX, clumpY), or off on its own line, picked by tie, when it is that middle.  Back the other way when
	* the map ends first (bounds: low x, low y, high x, high y).  Nobody knows where the shot is going, so reach is
	* twice the blast: from anywhere inside it, a straight flight that long ends outside whatever the heading. */
void aiStrikeDodgeSpot( Real x, Real y, Real clumpX, Real clumpY, Real reach, const Real *bounds, Int tie, Real *outX, Real *outY );

/** Whether a unit may answer a gun by leaving.  A fight at home is fought, home being where a retreat goes,
	* and a gun out past home that is shelling it is nothing the retreat will take up, so that one it may.
	* Away from home, a gun the retreat can see, known and within its radius, is the retreat's to judge on a
	* rung that retreats: units leaving on one hit where the fight as a whole holds were the passivity 4264d652
	* cost matches to take out. */
Bool aiFireMayLeave( Bool unitAtHome, Bool gunAtHome, Bool rungRetreats, Bool gunKnown, Real gunDistance, Real retreatRadius );

/** ours would lose to cover: there is something there, and ours are worth less than odds times it. */
Bool aiOutgunned( Real ours, Real cover, Real odds );

/** A body's last hit stamp is within window frames of now.  The stamp starts out as 0xffffffff. */
Bool aiRecentlyHit( UnsignedInt hitFrame, UnsignedInt now, UnsignedInt window );

/** A unit at (x, y) walking along (dirX, dirY), a unit vector, is about to walk into a gun at (gunX, gunY):
	* it is outside the gun's reach, heading towards it, and the next lookahead of its walk comes within edge
	* of it. */
Bool aiWalkEntersReach( Real x, Real y, Real dirX, Real dirY, Real gunX, Real gunY, Real reach, Real edge, Real lookahead );

/** A force holding where it fell back to: stay, go back to the fight it left, or give up and go home. */
enum AIFallbackDecision { AIFALLBACK_HOLD, AIFALLBACK_RESUME, AIFALLBACK_GO_HOME };
AIFallbackDecision aiRetreatHoldDecision( Bool arrived, UnsignedInt heldFrames, Real ratioThere, Real resumeRatio,
																					UnsignedInt minHold, UnsignedInt maxHold );

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

/** Another factory of this kind.  finished are standing and done, constructing are already on the
	* way, history holds one bit a spend pass, newest lowest, set when every finished one was full on it
	* (aiQueueKindFull, aiAirfieldsFull; aiSampleFactories), count is how many passes it holds, and
	* window is the rung's m_factoryBacklogSeconds in passes (aiBacklogSamples).
	*
	* Another goes up once the last window passes are all in and at least AI_FACTORY_FULL_PERCENT of
	* them were full, and a replacement when none are left.  One already on the way is the building
	* this would ask for.  A share, not an unbroken run: a pass that found a factory between two units
	* reset the clock, and an Easy computer whose units finish inside a pass never got a second one. */
Bool aiWantsAnotherFactory( Int finished, Int constructing, UnsignedInt history, Int count, Int window );

/** The share of a window's passes, in percent, that have to find the kind full. */
static const Int AI_FACTORY_FULL_PERCENT = 80;

/** The most passes a window holds: the bits in the history. */
static const Int AI_FACTORY_HISTORY_PASSES = 32;

/** The rung's backlog in spend passes of passFrames each, rounded up, 1 to AI_FACTORY_HISTORY_PASSES. */
inline Int aiBacklogSamples( Real seconds, Int passFrames )
{
	const Int frames = REAL_TO_INT( seconds * LOGICFRAMES_PER_SECOND );
	const Int passes = ( frames + passFrames - 1 ) / passFrames;
	return passes < 1 ? 1 : passes > AI_FACTORY_HISTORY_PASSES ? AI_FACTORY_HISTORY_PASSES : passes;
}

/** One spend pass of a kind's history: shifts in whether it was full.  One of the kind going up
	* empties it, so the window starts again once that one stands and has to prove itself full too. */
inline void aiSampleFactories( UnsignedInt *history, Int *count, Bool full, Int constructing )
{
	if( constructing > 0 )
	{
		*history = 0;
		*count = 0;
		return;
	}
	*history = ( *history << 1 ) | ( full ? 1u : 0u );
	if( *count < AI_FACTORY_HISTORY_PASSES )
		++*count;
}

/** Barracks or war factories are full on a pass when every finished one (busy of finished) was busy:
	* something in its queue, or a unit let out since the last pass.  Busy, not backed up: the scripts
	* train only into an idle factory and the surplus fills a queue one deep, so a unit waiting behind
	* the one being built almost never happened.  And busy over the pass, not at its instant: units that
	* finish inside a pass left an Easy factory idle on most looks. */
inline Bool aiQueueKindFull( Int finished, Int busy ) { return finished > 0 && busy >= finished; }

/** An airfield is full when its parking is: every finished one (parkingFull of finished) refuses an
	* aircraft to its queue, and the computer has wanted one lately (wanted, a buy refused for parking).
	* Its queue says nothing, since a full airfield takes nothing into it. */
inline Bool aiAirfieldsFull( Int finished, Int parkingFull, Bool wanted ) { return finished > 0 && parkingFull >= finished && wanted; }

/** How long a buy refused for parking keeps the computer wanting another airfield. */
static const UnsignedInt AI_PARKING_WANT_FRAMES = 15 * LOGICFRAMES_PER_SECOND;

/** One unfinished building the computer placed, as its base builder last saw it. */
struct AISiteWatch
{
	ObjectID		m_site;
	Real				m_percent;					///< construction percent when it last moved forward
	UnsignedInt	m_progressFrame;		///< the frame it was first seen or last moved forward
	ObjectID		m_lostBuilder;			///< the last dead builder counted, so one death counts once
	Int					m_buildersLost;			///< builders that died since it last moved forward
};

enum AISiteVerdict
{
	AISITE_KEEP = 0,
	AISITE_STALLED,				///< nothing built on it for the rung's m_siteStallSeconds
	AISITE_BUILDERS_LOST	///< m_siteBuildersLost builders died on the way with nothing built between
};

/** One look at a site: progress resets the clock and the death count, a builder that is gone and not
	* yet counted adds one.  stallFrames 0 never stalls (a site at home, which a busy dozer explains). */
AISiteVerdict aiWatchSite( AISiteWatch *watch, Real percent, ObjectID builder, Bool builderAlive,
													 UnsignedInt now, UnsignedInt stallFrames, Int maxLost );

/** A spot a cancelled site stood on, left alone until a frame. */
struct AICooledSpot
{
	Coord3D			m_pos;
	UnsignedInt	m_until;
};

/** How near a cancelled site a dock counts as the one it was for, and a new site as the same spot.
	* The site's is about one building across, so a base site given up under attack does not shut the
	* base around it. */
static const Real AI_COOLED_DOCK_RADIUS = 200.0f;
static const Real AI_COOLED_SITE_RADIUS = 60.0f;

/** True while pos is within radius of a spot whose wait has not run out. */
Bool aiSpotCooling( const std::vector<AICooledSpot> &spots, const Coord3D *pos, Real radius, UnsignedInt now );

/** How many copies of the faction tech building (strategy center, propaganda center, palace) to
	* keep up so one of them blowing up does not take the tree with it. */
static const Int AI_TECH_BUILDING_COPIES = 3;

/** Another of those, when fewer than AI_TECH_BUILDING_COPIES are standing and none is already on the way. */
Bool aiWantsAnotherTechBuilding( Int standing, Int onTheWay );

/** Base defences a Hard computer may stand: one per AI_ARMY_PER_DEFENSE fighting units, and however
	* small the army, one more every AI_DEFENSE_PACE_SECONDS of the match until two superweapons' worth
	* stand.  The army ration alone left a 1v1 base on two or three guns, short of the
	* DEFENSES_PER_SUPERWEAPON a superweapon asks for.  At 30 seconds the clock allows the twelfth
	* gun at six minutes, under the seven a Hard-against-Hard 1v1 lasts on average; 40 seconds and
	* four units a gun, the first try, put a superweapon up on 5 of 96 Hard sides. */
static const Int AI_ARMY_PER_DEFENSE = 3;
static const Int AI_DEFENSE_PACE_SECONDS = 30;
Int aiDefenseAllowance( Int army, UnsignedInt frame );

/** Which of the base defences a dozer can build goes up next, as an index into the candidates: the
	* one with the fewest standing, so a side's guns come as a mix rather than a row of whatever button
	* sits first.  A defence with no gun or spawn of its own (an empty bunker, a speaker tower) counts
	* AI_SUPPORT_DEFENSE_WEIGHT times what it has standing.  Ties go to an armed one, then to the first
	* candidate.  -1 when there are none. */
/** What one point of open ground inside a defence's reach is worth to the spot it is scored for:
	* how much nearer the enemy it lies than the middle of the base does, in reaches, never below
	* zero, plus a tenth for lying outside the base.  Ground behind the base counts only that tenth,
	* so a spot at the back scores a fraction of one at the front, and none inside the base. */
Real aiFireSampleWeight( Real baseToThreat, Real sampleToThreat, Real reach, Bool outsideBase );

/** Ground distance over the pathfind grid, handed out nearest first.  A step to a side neighbour
	* costs AI_GROUND_STEP and a diagonal one AI_GROUND_DIAGONAL, a 1.5 standing in for 1.41 so every
	* distance is a small integer: the cells wait in four buckets that wrap (Dial's algorithm), with
	* no heap and no tie broken by anything but the order the cells were reached, which is the same
	* on every machine.  Whether a cell may be entered is asked once, of the callback; a source is
	* handed out even when it may not be entered itself, so a flood seeded on a cliff edge still
	* reaches the ground beside it. */
static const Int AI_GROUND_STEP = 2;
static const Int AI_GROUND_DIAGONAL = 3;
static const UnsignedShort AI_GROUND_UNREACHED = 0xFFFF;

class AIGroundFlood
{
public:
	typedef Bool (*WalkableFunc)( Int x, Int y, void *userData );
	AIGroundFlood( Int width, Int height, WalkableFunc walkable, void *userData );
	void addSource( Int x, Int y );						///< distance 0; every source goes in before the first next()
	void setMaxDistance( Int maxDistance ) { m_max = maxDistance; }	///< nothing further is entered
	Bool next( Int *x, Int *y, Int *distance );	///< the nearest cell not yet handed out, FALSE when none is left
	std::vector<UnsignedShort> &field( void ) { return m_dist; }	///< y * width + x; final once next() has said FALSE

private:
	Bool walkable( Int x, Int y, Int index );
	Int m_width;
	Int m_height;
	WalkableFunc m_walkable;
	void *m_userData;
	std::vector<UnsignedShort> m_dist;
	std::vector<UnsignedByte> m_walk;			///< 0 not asked yet, 1 may be entered, 2 may not
	std::vector<Int> m_bucket[ 4 ];
	Int m_at;
	Int m_cursor;
	Int m_pending;
	Int m_max;
};

/** The narrowest place on an enemy's road in, for the Turtle's line.  fromBase and fromThreat are
	* ground fields over a grid width cells wide and cells long; a cell is on the road when the drive
	* from the threat through it to the base is at most AI_CHOKE_SLACK_PERCENT of routeLength, and on
	* our half of it.  The road between minDistance and maxDistance from the base is cut into bands
	* AI_CHOKE_BAND deep, and each band's count of road cells is divided by the width an open field's
	* road has at that distance, which grows as the square root of the distance from both ends: on open
	* ground every band ties and the nearest one wins, and a pass between cliffs is narrower than that.
	* Returns the road cell of the narrowest band nearest the band's middle, or -1 when no band holds a
	* road cell; *roadCells gets that band's count. */
static const Int AI_CHOKE_SLACK_PERCENT = 115;
static const Int AI_CHOKE_BAND = 4 * AI_GROUND_STEP;
Int aiFindChoke( const UnsignedShort *fromBase, const UnsignedShort *fromThreat, Int width, Int cells, Int routeLength,
								 Int minDistance, Int maxDistance, Int *roadCells );

static const Int AI_SUPPORT_DEFENSE_WEIGHT = 3;
Int aiPickBaseDefense( const Int *standing, const Bool *armed, Int count );

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

/** What a computer player's money can go to, as the purchase queue ranks it. */
enum AIPurchaseKind
{
	AIBUY_POWER = 0,			///< a power plant
	AIBUY_DOZER,
	AIBUY_ECONOMY,				///< supply center, gatherer, drop zone, black market, internet center, hacker
	AIBUY_BASE,						///< a factory or anything else on the build list
	AIBUY_SUPERWEAPON,		///< a superweapon, or the tech building it needs
	AIBUY_DEFENSE,				///< a base defense
	AIBUY_ARMY,
	AIBUY_UPGRADE,
	AIBUY_EMERGENCY,			///< fighters trained against enemies already standing in the base
	AIBUY_KIND_COUNT
};

/** The first minutes of a match, while economy and power outrank the army. */
const UnsignedInt AI_PURCHASE_EARLY_FRAMES = 6 * 60 * LOGICFRAMES_PER_SECOND;

/** How much a purchase of this kind matters next to the others; the higher wins the money.  Early in a
	* match income and power come before the army, which is how a Hard AI kept attacking with two
	* gatherers and no drop zone. */
Int aiPurchaseScore( AIPurchaseKind kind, Bool earlyGame );

//-------------------------------------------------------------------------------------------------
/** How a personality leans on the rung it plays at: multipliers on the rung's own knobs, so a Turtle
	* on Easy is still an Easy Turtle, and offsets on the scores the AI already ranks by.  A preference,
	* never a cheat: nothing here touches income, build rate, vision or a unit.  The Normal row is 1 and
	* 0 throughout, and every caller skips it besides, so a Normal seat plays as it always did. */
//-------------------------------------------------------------------------------------------------
struct AIPersonaWeights
{
	Int  m_role;						///< the AIRole it plays, or -1 for the one the match rolled
	Real m_firstPush;				///< times the rung's m_firstPushSeconds
	Real m_attackPower;			///< times the rung's m_minAttackPower
	Real m_wavePower;				///< times the power a parked wave waits for
	Real m_waveHold;				///< times the longest a parked team waits for the rest of its wave
	Real m_bank;						///< times the rung's bank target, its seconds and its floor
	Real m_hoard;						///< times the rung's cash hoard threshold
	Real m_surplus;					///< times the rung's surplus share, never past all of it
	Real m_retreat;					///< times the rung's retreat ratio: a rung that never retreats still does not
	Real m_counter;					///< times the rung's counter weight ...
	Real m_counterAdd;			///< ... and this on top, so Easy reads the enemy at all
	Real m_defenses;				///< times the base defences aiDefenseAllowance lets stand
	Real m_upgradeStart;		///< times the rung's m_upgradeStartSeconds
	Real m_defenceTeams;		///< added to a base or perimeter defence team's score, in production priority
	Real m_air;							///< added to a team's score at its share of aircraft, by cost
	Real m_infantry;				///< ... of infantry
	Real m_vehicle;					///< ... of ground vehicles
	Real m_stealth;					///< ... of units that can go invisible
	Int  m_buy[ AIBUY_KIND_COUNT ];	///< added to aiPurchaseScore, by kind
};

/** The row a personality plays by.  A Rusher's is the one it rushes by; once the rush is out it plays
	* by the Attacker's (AIPlayer::personaWeights). */
const AIPersonaWeights &aiPersonaWeights( AIPersonality persona );

/** What a team adds to its production score for the units it is made of: each share (0..1, by cost) at
	* the personality's weight, a defence team its flat weight on top. */
Real aiPersonaTeamBias( const AIPersonaWeights &weights, Bool defenceTeam, Real airShare, Real infantryShare,
												Real vehicleShare, Real stealthShare );

/** An air personality's taste for aircraft against what it has seen of the enemy's anti-air: the full
	* weight with none, nothing at two thirds of his known guns able to shoot up, and a dislike past that. */
Real aiPersonaAirBias( Real airWeight, Real enemyAntiAirShare );

/** An enemy's cost (aiEnemyCost) as an Opportunist sees it: the smaller his share of what is in sight,
	* the cheaper, and half price while he is busy with somebody else.  Any other personality, unchanged. */
Real aiPersonaEnemyCost( AIPersonality persona, Real cost, Real economyShare, Bool fightingSomeoneElse );

/** What an Economist passes to an ally out of a bank over its target: half of what is over twice the
	* target, in hundreds, at most cap, and 0 under AI_SHARE_MIN. */
static const Int AI_SHARE_MIN = 500;
Int aiPersonaShareAmount( Int bank, Int target, Int cap );

/** May a bot say something now: never twice inside AI_CHAT_GAP_FRAMES, the first line any time. */
static const UnsignedInt AI_CHAT_GAP_FRAMES = 150 * LOGICFRAMES_PER_SECOND;
Bool aiPersonaChatAllowed( UnsignedInt now, UnsignedInt lastChat );

/** What a player asks of his allies: his base is under attack, by aircraft, he is short of money, or
	* he goes after an enemy.  A computer player posts these itself; a person by beacon or chat command
	* (MSG_TEAM_REQUEST).  Only a personality answers: Normal reads nothing off the board. */
enum AITeamRequestKind
{
	AIREQ_HELP = 0,
	AIREQ_AA,
	AIREQ_MONEY,
	AIREQ_ATTACK,
	AIREQ_COUNT
};

struct AITeamRequest
{
	Int from;							///< the asking player's index
	Int enemy;						///< the player he is fighting, -1 for none known
	Coord3D pos;					///< where: the attacker, or the attack's target
	UnsignedInt frame;		///< when it was posted, 0 for never
};

/** How long a call for help stands, and a personality keeps to an ally's attack after he posted it. */
static const UnsignedInt AI_COOP_HELP_FRAMES = 15 * LOGICFRAMES_PER_SECOND;
static const UnsignedInt AI_COOP_JOIN_FRAMES = 120 * LOGICFRAMES_PER_SECOND;

/** Post a request from this player to every computer ally of his (allied both ways). */
void aiPostTeamRequest( Player *from, AITeamRequestKind kind, Int enemy, const Coord3D *pos );
/** The enemy of this player's who owns what stands nearest pos, -1 for none within reach. */
Int aiEnemyNear( const Player *of, const Coord3D *pos );

/** A skirmish AI's purchase queue, on every rung with a bank target.  Every spender asks it before it pays, with a stable key (a
	* template id), the price, its score and whether the thing could start right now (an idle factory,
	* a dozer).  It remembers one request: the best one it could not afford.  That one has the money
	* saved for it, and nothing that ranks below it is bought while the purchase would take the bank
	* under its price.
	*
	* The reservation is bounded every way, since the skirmish scripts keep seven to twenty priority
	* builds waiting all match and a queue that waits on all of them never buys anything.  Only a
	* request that could start now reserves.  A reservation whose spender stops asking goes after
	* AI_PURCHASE_STALE_FRAMES, one that is not met in AI_PURCHASE_HOLD_FRAMES goes and may not reserve
	* again for as long, and above hoardAt, the bank target (or the reserved price, if that is more) nothing
	* is held back at all, so the queue never makes the bank grow past one reserved price.  Ties go to
	* the lower key. */
struct AIPurchaseQueue
{
	enum { NONE = -1 };
	enum Verdict
	{
		BUY,				///< pay for it
		SHORT,			///< cannot afford it, and nothing was saved for it
		SAVING,			///< cannot afford it, and the money is being saved for it
		RESERVED,		///< ... from this request on
		HELD				///< could afford it, but it would eat the money saved for something better
	};
	enum Lapse { KEPT, STALE, TIMED_OUT };

	Int					m_key;					///< what the money is saved for, NONE for nothing
	Int					m_cost;
	Int					m_score;
	UnsignedInt	m_since;				///< frame the reservation was made
	UnsignedInt	m_seen;					///< last frame its spender asked
	Int					m_held;					///< purchases held back for it so far
	Int					m_lapsedKey;		///< the last reservation that timed out ...
	UnsignedInt	m_lapsedUntil;	///< ... and the frame it may reserve again

	AIPurchaseQueue() : m_key( NONE ), m_cost( 0 ), m_score( 0 ), m_since( 0 ), m_seen( 0 ), m_held( 0 ),
											m_lapsedKey( NONE ), m_lapsedUntil( 0 ) {}

	Lapse expire( UnsignedInt frame );
	Verdict ask( Int key, Int cost, Int score, Bool startable, Int money, Int hoardAt, UnsignedInt frame );
	void bought( Int key ) { if( key == m_key ) m_key = NONE; }
};

const UnsignedInt AI_PURCHASE_STALE_FRAMES = 20 * LOGICFRAMES_PER_SECOND;
const UnsignedInt AI_PURCHASE_HOLD_FRAMES = 40 * LOGICFRAMES_PER_SECOND;

/** How long a stretch of income is measured over before the rate is taken again. */
const UnsignedInt AI_INCOME_WINDOW_FRAMES = 30 * LOGICFRAMES_PER_SECOND;

/** The bank a computer player keeps: seconds of its income, and never under the floor. */
Int aiBankTarget( Real incomePerSecond, Real seconds, Int floor );

/** How much a bank over its target may put into extra units on one spend pass: share of the income of
	* AI_SURPLUS_INCOME_SECONDS, and never what takes the bank under target.  At share 1 that is three
	* passes' income a pass, which drains a pile; a rung with a small share only tops its plan up. */
Int aiSurplusBudget( Int bank, Int target, Real incomePerSecond, Real share );

/** The same for a match that started rich: share of what the bank holds over keep (the bank target or
	* the default starting cash, whichever is more), never more than fortune, what the start handed over
	* the default.  0 in a match at the default cash, which then plays as it did. */
Int aiFortuneBudget( Int bank, Int keep, Int fortune, Real share );
const Real AI_SURPLUS_INCOME_SECONDS = 30.0f;

/** What a unit counts for when an upgrade is weighed: 1 with nothing known of the enemy or no weight
	* on it, up to 2 for a unit that wins every trade against what the enemy fields and down to 0 for
	* one that loses them all.  matchup is its value-weighted aiMatchupScore against the enemy army. */
Real aiCounterFit( Real matchup, Real weight );

/** What one thing an upgrade would change counts for, as a share of its price: a fighting unit its
	* aiCounterFit, a building or an unarmed unit the rung's nonCombatWeight. */
Real aiUpgradeWeight( Bool fights, Real fit, Real nonCombatWeight );

/** The upgrade to buy out of count candidates: the one whose covered value (what of the army it
	* improves, each thing weighed by aiUpgradeWeight) is the most times its price, if that is at least
	* payoff, or half of payoff when the bank is past its target.  -1 for none.  Ties go to the first. */
Int aiPickUpgrade( const Real *covered, const Int *cost, Int count, Real payoff, Bool overTarget );

/** Which approach a team takes: the lane with the least known enemy firepower along it.  A negative
	* entry is a lane this map does not have.  A tie, or nothing known anywhere, keeps the lane the
	* script asked for, so an AI that has not scouted plays exactly as the script wrote it. */
Int aiLeastDefendedLane( const Real *laneFirepower, Int laneCount, Int requestedLane );

/** Whether the attack teams parked at the staging point go now: when what is parked adds up to a wave,
	* or the oldest has waited long enough.  The threshold is this AI's own force, never a fraction of
	* the enemy's, because two AIs each waiting to outnumber the other never move. */
Bool aiReleaseWave( Real heldPower, Real wavePower, UnsignedInt heldFrames, UnsignedInt maxHoldFrames );

/** How hard a skirmish AI leans on its current enemy, from how it rates its chance against him.  In
	* order, so a comparison reads as "at least this much". */
enum AIPressure
{
	AIPRESSURE_DEFEND = 0,	///< outmatched: teams stay home until they are a bigger wave than usual
	AIPRESSURE_NORMAL,			///< an even match, or an enemy nobody has looked at: the wave as it always was
	AIPRESSURE_PRESS,				///< ahead: no waiting for a wave, teams go as they form
	AIPRESSURE_FINISH,			///< he has next to no army left: the guards at home go too
	AIPRESSURE_COUNT
};

/** The chance of winning the fight between the two armies, 0..1, from what each is worth in the
	* build-cost currency the waves are counted in.  Both sides are given a couple of tanks they do not
	* have, so one rifleman against nothing is not a rout. */
Real aiWinChance( Real myPower, Real enemyPower );

/** The same chance, side against side.  An ally's army counts by allyShare, and each ally of the enemy
	* counts by the same share as an army like his, so even teams rate the way one against one does. */
Real aiSideWinChance( Real myPower, Real alliedPower, Real enemyPower, Int enemyAllies, Real allyShare );

/** The level that chance asks for.  A level is left later than it is entered, so a chance sitting on
	* a threshold does not change the orders every two seconds: a Hard AI watching an army walk in and
	* out of its sight changed level six times in a minute on a chance between 0.58 and 0.66.  PRESS and
	* FINISH are only entered with the enemy's base in view: an army that is merely out of sight is not
	* an army that is gone. */
AIPressure aiPressureLevel( Real chance, Bool enemyBaseInSight, AIPressure current );

/** aiReleaseWave under pressure.  Ahead, whatever is parked goes.  Outmatched, the wave is twice the
	* size, the wait twice as long, and what has waited that long still goes only if it is a wave: a
	* trickle sent at an enemy who outnumbers it is a gift. */
Bool aiReleaseWaveAt( AIPressure level, Real heldPower, Real wavePower, UnsignedInt heldFrames, UnsignedInt maxHoldFrames );

class TAiData : public Snapshot
{
public:
	TAiData();
	~TAiData();

	void addSideInfo(AISideInfo *info);
	void addFactionBuildList(AISideBuildList *buildList);

	/** A side's resource, defence and skill set entry, and its skirmish build list.  Under -modfactions a
		* mod faction's own mod's AIData.ini is loaded under "M<n>~<side>" (AI::parseAiDataDefinition), so
		* modIndex asks for that first and falls back on retail's entry of the side when the mod has none.
		* NULL when neither has one. */
	const AISideInfo *findSideInfo( const AsciiString &side, Int modIndex ) const;
	const AISideBuildList *findBuildList( const AsciiString &side, Int modIndex ) const;
	const AISideInfo *findSideInfo( const Player *player ) const;	///< by the player's side and template's mod

	// --------------- inherited from Snapshot interface --------------
	void crc( Xfer *xfer );
	void xfer( Xfer *xfer );
	void loadPostProcess( void );

	Real m_structureSeconds;		// Try to build a structure every N seconds.
	Real m_teamSeconds;					// Try to build a team every N seconds.
	Int m_resourcesWealthy;		// How many resources to be wealthy.
	Int m_resourcesPoor;			// How few resources to be poor.
	UnsignedInt m_forceIdleFramesCount;	// How many frames does a unit need to be Idle before it can begin looking for enemies?
	Real m_structuresWealthyMod; // Factor to multiply m_structurFrames by if we are wealthy.
	Real m_teamWealthyMod;		// Factor to multiply m_teamFrames by if we are wealthy.
	Real m_structuresPoorMod; // Factor to multiply m_structureFrames by if we are poor.
	Real m_teamPoorMod;				// Factor to multiply m_teamFrames if we are poor.
	Real m_teamResourcesToBuild; // Amount of the resources needed to build a team required before we start.
	Real m_guardInnerModifierAI;	// Multiply the AI unit's vision by this much == guard inner circle.
	Real m_guardOuterModifierAI;	// Multiply the AI unit's vision by this much == guard outer circle.
	Real m_guardInnerModifierHuman;	// Multiply the human unit's vision by this much == guard inner circle
	Real m_guardOuterModifierHuman;	// Multiply the human unit's vision by this much == guard outer circle
	UnsignedInt m_guardChaseUnitFrames;		// Number of frames for which a unit should 
	UnsignedInt m_guardEnemyScanRate;		// rate to scan for enemies while guarding
	UnsignedInt m_guardEnemyReturnScanRate;		// rate to scan for enemies while guarding but returning

	Real m_wallHeight;				// Height of special wall units can walk on top of.

	AIDifficultyProfile m_skill[AISKILL_COUNT];	///< the ladder; defaults in TAiData(), overridable from INI
	
	Real m_alertRangeModifier;			// When a unit is alert, its range will be modified by this value
	Real m_aggressiveRangeModifier;	// When a unit is aggressive, its range will be modified by this value

	/* The attack priority distance modifier changes relative values.  The relative priority
	   is reduced by the distance away, divided by the modifier.
		 Example: tanks are priority 10, powerplants priority 15, and modifier = 100.0
		 If a powerplant is 700 feet away from the attacker, and the tank is 
		 100 feet, the effective priority for tank is 9 (10-(100/100), and the
		 effective priority for powerplant is 8 (15 -(700/100).  So the tanks
		 would be attacked first because their distance weighted priority is greater. */
	Real m_attackPriorityDistanceModifier; // Distance to reduce a relative AttackPriority by 1.
	
	
	/* 
		How close to a waypoint does a group of units have to be to consider itself at the waypoint?
		m_skirmishGroupFudgeValue is multiplied by the number of the units in the group asking if its
		close enough to determine this.
		
		So for instance (if m_skirmishGroupFudgeValue was 5), if 2 units are walking along a path, 
		they would check to see if they were less than 10 feet away from the waypoint in order 
		to say they were close enough. A group of 10 units would consider themselves close enough 
		if they were within 50 feet of the waypoint.
	*/
	Real m_skirmishGroupFudgeValue;	

	Real m_maxRecruitDistance; // Maximum distance away that units can be recruited.
	Real m_skirmishBaseDefenseExtraDistance; ///< instead of building base defenses right on the template bounding circle, push them out this much.
	Real m_repulsedDistance; // How far a repulsed unit will run past vision range before stopping.
	Bool m_enableRepulsors; // Is repulsion enabled?

	Bool m_forceSkirmishAI; // If true, forces skirmish ai instead of solo ai for development until the skirmish ui is done.  jba.
	Bool m_rotateSkirmishBases; // If true, rotates skirmish ai bases to face the center of the map. jba.

	Bool m_attackUsesLineOfSight; // If true, attack for units with KINDOF_ATTACK_NEEDS_LINE_OF_SIGHT  uses line of sight. jba.

	Bool m_attackIgnoreInsignificantBuildings; // If true, attack for ALL UNITS ignores buildings that are hostile that are not significant. jkmcd

	// Group pathfind info.
	Int	 m_minInfantryForGroup;		// We need at least this many to do it.
	Int	 m_minVehiclesForGroup;		// We need at least this many vehicles to do it.
	Real m_minDistanceForGroup;		// We need to move at least this far to do it.
	Real m_distanceRequiresGroup; // If we are moving this far or farther, force group moving.
	Real m_minClumpDensity;				// What density constitues a clump.  .5 means units occupying 1/2 of their bounding area.

	Int	 m_infantryPathfindDiameter; // Diameter of path in cells for infantry.
	Int  m_vehiclePathfindDiameter;  // Diameter of path in cells for vehicles.

	Int  m_rebuildDelaySeconds;  // Seconds to delay rebuilding after a base building is destroyed or captured.

	Real  m_supplyCenterSafeRadius;  // Radius to scan for enemies to determine safety.

	Real  m_aiDozerBoredRadiusModifier;  // Modifies ai dozers scan range so the move out farther than human ones.
	Bool	m_aiCrushesInfantry; // If true, AI vehicles will attempt to crush infantry.

	// Retaliate params. [8/25/2003]
	Real	m_maxRetaliateDistance; // If attacker is > this distance, don't retaliate. [8/25/2003]
	Real	m_retaliateFriendsRadius; // If we have friends within this radius, get them to help retaliate. [8/25/2003]


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
	/// the key a mod's AIData entries are filed under: "M<n>~America" for mod index n-1, the side itself for retail (-1)
	static AsciiString modSideKey( const AsciiString &side, Int modIndex );

	static Int s_enemyScans;								///< see getEnemyScanCountThisFrame
	static Real s_lastPathfindMS;						///< see getLastPathfindMS
	static Real s_lastPlayerUpdateMS;				///< see getLastPlayerUpdateMS
	const TAiData *getAiData() {return m_aiData;}

	/// The rung's knobs. Bounds-checked, so a bad index reads as the baseline rather than as memory.
	const AIDifficultyProfile *getDifficultyProfile( AISkillLevel level ) const;

	static void parseSkillLevel(INI *ini, void *instance, void *store, const void *userData);

	// Note: Does not work for things that do not have AI. (This is in AI.h, after all)
	static Real getAdjustedVisionRangeForObject(const Object *object, Int factorsToConsider);

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
	/// where each live group sits in m_groupList, so destroyGroup need not walk it.  Looked up by
	/// address and never iterated: m_groupList alone keeps the order the CRC walks
	std::unordered_map<AIGroup *, std::list<AIGroup *>::iterator> m_groupIndex;
	TAiData *m_aiData;
	void newOverride(void);
	void addSideInfo(AISideInfo *info);
	void sweepEmptyGroups( void );				///< destroy the groups made since the last sweep that nobody joined

	UnsignedInt m_nextGroupID;
	UnsignedInt m_sweptGroupID;						///< every group up to this ID has been through sweepEmptyGroups
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
enum AttitudeType : Int { AI_SLEEP = -2, AI_PASSIVE=-1, AI_NORMAL=0, AI_ALERT=1, AI_AGGRESSIVE=2, AI_INVALID=3 };		///< AI "attitude" behavior modifiers

enum CommandSourceType : Int;

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
	void groupAttackMoveToPosition( const Coord3D *pos, Int maxShotsToFire, CommandSourceType cmdSource );	///< Attack move to the location
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
	void addTeam( Team *team );							///< add() every team member, in team order (Team::getTeamAsAIGroup)
	
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
	Bool m_dirty;														///< "dirty bit" - if true then group speed, leader, needs recompute

	UnsignedInt m_id;												///< the unique ID of this group
	Path *m_groundPath;											///< Group ground path.
	
	mutable VecObjectID	m_lastRequestedIDList;			///< this is used so we can return by reference, saving a copy
};


#endif // _AI_H_
