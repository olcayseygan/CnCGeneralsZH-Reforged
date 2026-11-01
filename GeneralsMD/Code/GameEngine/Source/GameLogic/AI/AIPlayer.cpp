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

// AIPlayer.cpp ///////////////////////////////////////////////////////////////////////////////////
// Computerized opponent
// Author: Michael S. Booth, January 2002
///////////////////////////////////////////////////////////////////////////////////////////////////

// INCLUDES ///////////////////////////////////////////////////////////////////////////////////////
#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine
#include "Lib/Clock.h"
#include "Common/GameMemory.h"
#include "Common/GameState.h"
#include "Common/GlobalData.h"
#include "Common/PerfTimer.h"
#include "Common/Player.h"
#include "Common/PlayerTemplate.h"
#include "Common/SpecialPower.h"
#include "Common/Team.h" 
#include "Common/ThingFactory.h"
#include "Common/PlayerList.h"
#include "Common/Recorder.h"
#include "Common/ResourceGatheringManager.h"
#include "Common/BuildAssistant.h"
#include "Common/ThingTemplate.h"
#include "Common/TunnelTracker.h"
#include "Common/Upgrade.h"
#include "Common/WellKnownKeys.h"
#include "Common/Xfer.h"
#include "GameClient/ControlBar.h"
#include "GameClient/GameText.h"			// a personality's lines of chat
#include "GameClient/InGameUI.h"			// ... and where they are shown
#include "GameClient/TerrainVisual.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object.h"
#include "GameLogic/AIPlayer.h"
#include "GameClient/Drawable.h"
#include "GameLogic/SidesList.h"
#include "GameLogic/AI.h"
#include "GameLogic/AIPathfind.h"
#include "GameLogic/Weapon.h"				// the weapons B1 weighs a team's units by
#include "GameLogic/WeaponSet.h"
#include "GameLogic/Armor.h"				// ... the armour they land on
#include "GameLogic/ArmorSet.h"
#include "GameLogic/Module/ActiveBody.h"	// ... and the health behind it
#include "GameLogic/TerrainLogic.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/DozerAIUpdate.h"
#include "GameLogic/Module/UpdateModule.h"
#include "GameLogic/ScriptEngine.h"
#include "GameLogic/Module/ProductionUpdate.h"
#include "GameLogic/Module/RebuildHoleBehavior.h"
#include "GameLogic/Module/SupplyTruckAIUpdate.h"
#include "GameLogic/Module/SupplyWarehouseDockUpdate.h"
#include "GameLogic/PartitionManager.h"
#include "Common/ActionManager.h"				// canCaptureBuilding, for the tech buildings
#include "GameLogic/Module/SpecialPowerModule.h"	// ... and the module that does it
#include "GameLogic/Module/SpecialAbilityUpdate.h"	// ending a raider's bomb run
#include "GameLogic/Module/StealthUpdate.h"	// a unit that goes invisible from the factory
#include "GameLogic/Module/CollideModule.h"	// ... and the collide that takes a vehicle by touching it
#include "GameLogic/Module/ContainModule.h"
#include "GameLogic/Module/OpenContain.h"			// a seat's own shoot-out flag, asked before a rider exists
#include "GameLogic/Module/TransportContain.h"	// ... and a template's hold, before one exists
#include "GameLogic/Module/JetAIUpdate.h"		// a Comanche is a jet with no runway
#include "GameLogic/Module/CleanupHazardUpdate.h"	// an Ambulance out cleaning is left to it
#include "GameLogic/Module/ParticleUplinkCannonUpdate.h"	// driving the beam
#include "GameLogic/Module/PhysicsUpdate.h"		// which way a unit is walking, at the edge of a defence's reach
#include <map>
#include "Platform/MsvcFloatCasts.h"

#ifdef _INTERNAL
// for occasional debugging...
//#pragma optimize("", off)
//#pragma MESSAGE("************************************** WARNING, optimization disabled for debugging purposes")
#endif

#define SUPPLY_CENTER_CLOSE_DIST (20*PATHFIND_CELL_SIZE_F)

#define USE_DOZER 1

/** Does 'observerNdx' know this thing is there?
	*
	* "I can see it now, or it is a building I have seen and buildings do not walk away" - which is
	* the whole information model the AI needs.  Object::isUnknownTo draws that line from the cells
	* and each structure's memory of who last saw it, both kept on the logic's own frames.  It used to
	* be getShroudedStatus != SHROUDED, which draws the same line but decides "has seen it" from
	* whichever code happens to ask while the building is in view: the drawing loop, run for every
	* player only on an observer's machine, so two machines could think different things.
	*
	* observerNdx < 0 is the old omniscient answer, for callers that are not one player's thinking.
	*
	* A stealthed unit nobody has detected is unknown to anyone but its own side, wherever it stands:
	* a hidden Burton walking into the base drew the whole army after him (issue #64).  The rule is the
	* one PartitionFilterStealthedAndUndetected applies to a unit choosing its target, bomb trucks
	* included: one disguised as the observer's enemy is seen, one dressed as his friend is not. */
static Bool observerKnowsAbout( const Object *obj, Int observerNdx )
{
	if( obj == NULL )
		return FALSE;
	if( observerNdx < 0 )
		return TRUE;
	if( obj->isUnknownTo( observerNdx ) )
		return FALSE;
	if( !obj->testStatus( OBJECT_STATUS_STEALTHED ) || obj->testStatus( OBJECT_STATUS_DETECTED ) )
		return TRUE;
	const Player *observer = ThePlayerList->getNthPlayer( observerNdx );
	if( observer->getRelationship( obj->getTeam() ) == ALLIES )
		return TRUE;
	if( !obj->isKindOf( KINDOF_DISGUISER ) )
		return FALSE;
	const StealthUpdate *stealth = obj->getStealth();
	if( !obj->testStatus( OBJECT_STATUS_DISGUISED ) || stealth == NULL || !stealth->isDisguised() )
		return TRUE;
	const Player *dressedAs = ThePlayerList->getNthPlayer( stealth->getDisguisedPlayerIndex() );
	return observer->getRelationship( dressedAs->getDefaultTeam() ) == ENEMIES;
}

/** What a unit is worth in a fight, for every decision here that has to weigh one force against
	* another - what to build against (B1), when to break off (C1), when there is enough to attack
	* with (C2).
	*
	* ThreatValue is the data's own answer to exactly this question, and it is what the engine's own
	* threat map is built from.  It also defaults to zero and the shipped Object INI almost never
	* sets it, so a first cut of all three of those decisions quietly weighed every force in the game
	* at nothing and did nothing at all.  Build cost is the fallback: always present, and a fair
	* proxy - the data already prices a tank above a rifleman.
	*/
static Real templateCombatPower( const ThingTemplate *tmpl, const Player *owner )
{
	//
	// Only what can shoot has any power in an exchange.  Cost as a proxy is only fair among things
	// that fight: a war factory costs as much as a tank column and cannot fire a shot, and the
	// first cut of this counted every building in range as part of the enemy's firepower - so every
	// team that came within sight of a base decided it was losing and went home, and 20 measured
	// matches ended with zero kills on both sides.
	//
	if( !tmpl->isKindOf( KINDOF_CAN_ATTACK ) )
		return 0.0f;

	const Real threat = INT_TO_REAL( tmpl->getThreatValue() );
	if( threat > 0.0f )
		return threat;

	return INT_TO_REAL( tmpl->calcCostToBuild( owner ) );
}

/** What a rank adds to a unit's worth: a quarter of its price a chevron.  A heroic tank hits harder,
	* takes more and heals itself, and the price alone weighed it the same as one off the line. */
Real AIRank_valueScale( Int rank )
{
	return 1.0f + 0.25f * (Real)rank;
}

/** The same figure for a unit on the field, its rank counted.  Ranks are on show to every player, so
	* weighing an enemy's by them reads nothing a human could not. */
static Real aiCombatPower( const Object *obj )
{
	if( obj == NULL || obj->getTemplate() == NULL )
		return 0.0f;
	return templateCombatPower( obj->getTemplate(), obj->getControllingPlayer() ) * AIRank_valueScale( (Int)obj->getVeterancyLevel() );
}

/** One of the map's own Player_N_Start waypoints, by 0-based position index.  Where the start
	* positions *are* is public - the map has them, every human can read them off the preview.  Which
	* of them the enemy is standing on is not, and that is what the AI has to find out. */
static Bool startPositionLoc( Int startNdx, Coord3D *pos )
{
	if( startNdx < 0 || startNdx >= MAX_PLAYER_COUNT || TheTerrainLogic == NULL )
		return FALSE;

	AsciiString name;
	name.format( "Player_%d_Start", startNdx + 1 );		// the waypoints are 1-based
	Waypoint *way = TheTerrainLogic->getWaypointByName( name );
	if( way == NULL )
		return FALSE;

	*pos = *way->getLocation();
	return TRUE;
}

/** Where a player actually started.  This is ground truth, and the AI is only allowed to ask it
	* where it has earned the answer: standing next to the position with a scout, for its own and its
	* allies' positions, or once elimination has left nowhere else for an enemy to be.  Everything
	* that wants an enemy's address goes through enemyStartGuess instead. */
static Bool playerStartPosition( Int playerNdx, Coord3D *pos )
{
	Player *p = ThePlayerList ? ThePlayerList->getNthPlayer( playerNdx ) : NULL;
	if( p == NULL )
		return FALSE;

	return startPositionLoc( p->getMpStartIndex(), pos );
}


/* How often each of the periodic jobs re-checks.  Up here rather than beside the function that uses
	 each one, because the constructor needs them: it spreads the players across each cycle, and it
	 cannot do that without knowing how long the cycle is.  See the staggering block below. */

/** How often the AI looks for somewhere to expand to.  Long, because an expansion is a building
	* order and the base builder has its own rhythm to keep. */
static const Int EXPANSION_CHECK_SECONDS = 60;

/** How often the scout is looked at.  It only ever gets an order when it has arrived, so this is a
	* check, not a repath. */
static const Int SCOUT_CHECK_RATE = 2 * LOGICFRAMES_PER_SECOND;

/** How often the AI looks for something to capture.  It only ever gets an order when it is idle, so
	* this is a check, not a re-path. */
static const Int CAPTURE_CHECK_RATE = 5 * LOGICFRAMES_PER_SECOND;

/** A capture further than this from the capturer goes by helicopter when one is waiting, and the
	* helicopter puts it down this far short of the building. */
static const Real FERRY_MIN_WALK = 900.0f;
static const Real FERRY_DROP_STANDOFF = 60.0f;

/** How often the AI looks for a vehicle to take.  Same rhythm as the capture check, and the target
	* has to be in sight, so a faster clock would only re-order what is already walking. */
static const Int HIJACK_CHECK_RATE = 5 * LOGICFRAMES_PER_SECOND;

#ifdef DEBUG_LOGGING
/** Milliseconds between two performance counter readings.  Used by the per-job AI profile below and
	* by the sub-timers inside processBaseBuilding, which is why it lives up here. */
static Real aiPlayerElapsedMS( const Int64 &from, const Int64 &to )
{
	static Int64 freq = 0;
	if( freq == 0 )
		freq = Clock_Ticks_Per_Second();
	if( freq == 0 )
		return 0.0f;
	return (Real)( (double)(to - from) * 1000.0 / (double)freq );
}

/* Sub-timers inside the "base" job, which is the one that got expensive once the expansion convoy
	 was out of the way.  They nest inside it, so they keep their own start variable rather than
	 sharing the per-job one, and the report prints them as a breakdown of base rather than beside
	 it.  Reached through AIPlayer statics because AISkirmishPlayer overrides processBaseBuilding and
	 the version that actually runs in a skirmish lives in the other file. */
static const char *theAIBaseSubName[ AIPlayer::BASE_SUB_COUNT ] =
	{ "hole", "dozerfix", "safe", "finddozer", "canmake", "build" };
static Real theAIBaseSubMS[ AIPlayer::BASE_SUB_COUNT ];
static Int theAIBaseSubCalls[ AIPlayer::BASE_SUB_COUNT ];
static Int64 theAIBaseSubStart;
#endif

/*static*/ void AIPlayer::profileBaseSubBegin( void )
{
#ifdef DEBUG_LOGGING
	theAIBaseSubStart = Clock_Ticks();
#endif
}

/*static*/ void AIPlayer::profileBaseSubEnd( Int slot )
{
#ifdef DEBUG_LOGGING
	if( slot < 0 || slot >= BASE_SUB_COUNT )
		return;
	Int64 now;
	now = Clock_Ticks();
	theAIBaseSubMS[ slot ] += aiPlayerElapsedMS( theAIBaseSubStart, now );
	++theAIBaseSubCalls[ slot ];
#endif
}

/*static*/ void AIPlayer::profileBaseSubCount( Int slot )
{
#ifdef DEBUG_LOGGING
	if( slot >= 0 && slot < BASE_SUB_COUNT )
		++theAIBaseSubCalls[ slot ];
#endif
}

#ifdef DEBUG_LOGGING
#define AI_BASE_SUB_BEGIN()		AIPlayer::profileBaseSubBegin()
#define AI_BASE_SUB_END(slot)	AIPlayer::profileBaseSubEnd(slot)
#else
#define AI_BASE_SUB_BEGIN()
#define AI_BASE_SUB_END(slot)
#endif

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
/* How long one unit template takes to kill another, read off template data alone (see
	 templateFramesToKill). selectTeamToBuild asks it for every unit of every candidate team against
	 every kind of enemy in sight, both ways round, and each answer walks module names, weapon sets and
	 armour: an eight-player match spent 84ms of one frame there. Nothing it reads changes during a
	 match, so the answer is kept. prepareMatchupTables empties it at the start of every match, because
	 the next match can free a template and hand its address to another, and fills it on the loading
	 screen when a skirmish AI is playing. */
typedef std::pair<const ThingTemplate *, const ThingTemplate *> AITemplatePair;
static std::map<AITemplatePair, Real> theFramesToKillCache;
// the two questions under it that walk a template's module list by name, kept the same way: the first
// team choice of a match fills all three at once, and that one choice was the 90ms frame
static std::map<const ThingTemplate *, Real> theMaxHealthCache;
static std::map<const ThingTemplate *, Bool> theDetectsStealthCache;

AIPlayer::AIPlayer( Player *p ) :
m_player(p),
m_buildDelay(0), 
m_teamDelay(0),	
m_teamTimer(2),	// Important - don't start building teams until frame 1.
m_structureTimer(2), // Important - don't start building structures until frame 1.
m_readyToBuildTeam(false),
m_readyToBuildStructure(false),
m_structuresInQueue(0),
m_repairDozer(INVALID_ID),
m_frameAfterFailedDozerQueue(0),
m_skillsetSelector(INVALID_SKILLSET_SELECTION),
m_dozerQueuedForRepair(false),
m_supplySourceAttackCheckFrame(0),
m_attackedSupplyCenter(INVALID_ID),
m_teamSeconds(10),
m_curWarehouseID(INVALID_ID),
m_buildSearchNext(0),
m_buildSearchPlan(NULL),
m_scoutTimer(1),
m_retreatTimer(1),
m_expandTimer(1),
m_skillLevel(AISKILL_BRUTAL),
m_role(AIROLE_AGGRESSIVE)
{
	for (Int scoutSlot = 0; scoutSlot < MAX_AI_SCOUTS; ++scoutSlot) {
		m_scoutID[scoutSlot] = INVALID_ID;
		m_scoutTargetFor[scoutSlot] = -1;	// nothing looked at yet; the score picks the first target
	}
	for (Int startNdx = 0; startNdx < MAX_PLAYER_COUNT; ++startNdx) {
		m_scoutSeenFrame[startNdx] = 0;		// 0 == never seen, which outranks every real age below
		m_startChecked[startNdx] = FALSE;
		m_startOccupied[startNdx] = FALSE;
		m_playerStartNdx[startNdx] = -1;	// nobody located yet, not even us - updateStartIntel seeds it
	}
	m_startIntelFrame = 0;
	m_capturerID = INVALID_ID;
	m_ferryID = INVALID_ID;
	for( Int chinook = 0; chinook < MAX_GUNSHIP_CHINOOKS; ++chinook )
		m_gunshipChinook[ chinook ] = INVALID_ID;
	m_boardWaitFrame = 0;
	for( Int shuttle = 0; shuttle < MAX_TRANSPORT_CHINOOKS; ++shuttle )
	{
		m_shuttle[ shuttle ].id = INVALID_ID;
		m_shuttle[ shuttle ].phase = SHUTTLE_HOME;
		m_shuttle[ shuttle ].frame = 0;
		m_shuttle[ shuttle ].load = 0;
		m_shuttle[ shuttle ].aborted = FALSE;
		m_shuttle[ shuttle ].drop.zero();
		m_shuttle[ shuttle ].home.zero();
	}
	m_shuttleFront.zero();
	m_shuttleFrontFrame = 0;
	m_lastWaveSlots = 0;
	m_lentChinook = INVALID_ID;
	for( Int helix = 0; helix < MAX_DUTY_HELIXES; ++helix )
	{
		m_dutyHelix[ helix ].id = INVALID_ID;
		m_dutyHelix[ helix ].role = HELIX_HEALER;
		m_dutyHelix[ helix ].phase = 0;
		m_dutyHelix[ helix ].frame = 0;
		m_dutyHelix[ helix ].target = INVALID_ID;
		m_dutyHelix[ helix ].spot.zero();
		m_dutyHelix[ helix ].targetHealth = 0.0f;
	}
	for( Int slot = 0; slot < MAX_DUTY_AMBULANCES; ++slot )
	{
		m_ambulance[ slot ].id = INVALID_ID;
		m_ambulance[ slot ].spot.zero();
		m_ambulance[ slot ].seen.zero();
		m_ambulance[ slot ].seenHealth = 1.0f;
		m_ambulance[ slot ].seenReach = 0.0f;
		m_ambulance[ slot ].pullbacks = 0;
		m_ambulance[ slot ].holds = 0;
	}
	m_ambulanceLost = 0;
	m_ambulancesBought = 0;
	m_incomeMark = 0;
	m_incomeMarkFrame = 0;
	m_bankMark = 0;
	m_incomePerSecond = 0.0f;
	m_startFortune = 0;
	for( Int k = 0; k < 3; ++k )
	{
		m_factoryFullHistory[ k ] = 0;
		m_factoryFullCount[ k ] = 0;
	}
	m_parkingBlockedFrame = 0;
	m_persona = AIPERSONA_NORMAL;
	m_personaFromRandom = FALSE;
	m_rushSent = FALSE;
	m_techDone = FALSE;
	m_raidFrame = 0;
	for( Int r = 0; r < MAX_PERSONA_RAIDERS; ++r )
		m_raider[ r ] = INVALID_ID;
	m_shareFrame = 0;
	for( Int k = 0; k < AIREQ_COUNT; ++k )
	{
		m_inbox[ k ].from = -1;
		m_inbox[ k ].enemy = -1;
		m_inbox[ k ].pos.zero();
		m_inbox[ k ].frame = 0;
	}
	m_answeredFrame = 0;
	m_chatFrame = 0;
	m_beatenMask = 0;
	m_personaWaves = 0;
	m_firstWaveFrame = 0;
	m_personaShared = 0;
	m_superweaponFrame = 0;
	m_healerSeconds = 0;
	m_healerClearSeconds = 0;
	m_captureTimer = 1;
	m_hijackerID = INVALID_ID;
	m_hijackTimer = 1;

	for( Int held = 0; held < MAX_HELD_TEAMS; ++held )
	{
		m_heldUsed[ held ] = FALSE;
		m_heldTeam[ held ] = 0;
		m_heldSuffix[ held ] = 0;
	}
	m_heldSince = 0;
	m_strikeNoticeFrame = 0;
	m_pressure = AIPRESSURE_NORMAL;
	m_knownEnemyPower = 0.0f;
	m_pressureEnemy = -1;
	m_massTemplate = NULL;
	m_massPickFrame = 0;
	m_massSince = 0;

	for( Int strike = 0; strike < MAX_REMEMBERED_STRIKES; ++strike )
	{
		m_strikeAim[ strike ].zero();
		m_strikeFrame[ strike ] = 0;		// 0 == nothing aimed here yet, which fades nothing
	}
	m_strikeNext = 0;

	m_frameLastBuildingBuilt = TheGameLogic->getFrame();
	p->setCanBuildUnits(false); // turn off ai production by default.

	Int i;
	for (i=0; i<MAX_STRUCTURES_TO_REPAIR; i++) {
		m_structuresToRepair[i] = INVALID_ID;
	}
	m_repairDozerOrigin.zero();
	m_buildProbePos.zero();
	m_groundFromBaseAt.zero();
	m_groundFromBaseBridges = 0;
	m_groundThreatsBridges = 0;
	m_chokeBaseAt.zero();
	m_chokeBridges = 0;
	m_baseCenter.zero();
	m_baseCenterSet = false;
	m_placementRing = 0;
	m_difficulty = TheScriptEngine->getGlobalDifficulty(); 
	m_skillLevel = skillLevelForDifficulty( m_difficulty );
	// m_role is rolled in AISkirmishPlayer's constructor: a script-driven opponent must not draw
	// from the logic random stream where retail did not.

	m_teamSeconds = TheAI->getAiData()->m_teamSeconds;

	//
	// Stagger this player's repeating checks.  doBaseBuilding re-arms itself for 2 seconds,
	// doTeamBuilding for 5 and updateBridgeRepair for 1, all from constants, and every AIPlayer is
	// built with the same timers on the same frame, so a lobby full of them checks in unison from
	// the first frame to the last and the whole cost of it lands on one logic frame.  The offset
	// moves each player to its own slot in the cycle; nothing runs less often.
	//
	// The two "ready" timers get the offset as well: they are what anchors the cadence that follows
	// (both hand their delay a zero on the frame they expire, and both expire on frame 2 for
	// everyone), so leaving them alone would re-align the players two frames in.  The price is that
	// the last player's first base-building check comes up to 2 seconds late and its first team
	// check up to 5 - once, at the start of the match.
	//
	const Int playerIndex = p->getPlayerIndex();
	m_buildDelay = computeUpdatePhase( playerIndex, 2*LOGICFRAMES_PER_SECOND );
	m_teamDelay = computeUpdatePhase( playerIndex, 5*LOGICFRAMES_PER_SECOND );
	m_structureTimer += m_buildDelay;
	m_teamTimer += m_teamDelay;
	// ...and EA never initialized this one at all.  updateBridgeRepair returns before reading it
	// while the repair queue is empty, which it is here, so it has been benign - but only just.
	m_bridgeTimer = computeUpdatePhase( playerIndex, LOGICFRAMES_PER_SECOND );

	/* The four jobs this fork added start their timers at 1, so every computer player ran every one
		 of them on the same logic frame - and kept doing so for the rest of the match, because each
		 re-arms with a fixed interval and so never drifts apart again.

		 Measured on Twilight Flame with eight top-rung AI: doExpansion alone cost 15.4ms of a
		 16.7ms frame, all of it landing on one frame a minute. Spread across the cycle it is under
		 6ms, and nothing runs any less often than it did. */
	/* What is needed is only that the players land on *different* frames, not that they are spread
		 evenly over the cycle - and the difference matters for the slow ones. Spreading the
		 once-a-minute expansion check over its whole minute delays the last player's first expansion
		 by nearly half of one, which showed up as a smaller army over six matches. A two-second
		 window puts eight players seven frames apart, which is all the convoy needs broken, and
		 costs nobody anything measurable. The offset is permanent either way: each timer re-arms
		 with a fixed interval, so players that start apart stay apart. */
	const Int SPREAD = 2 * LOGICFRAMES_PER_SECOND;
	m_expandTimer += computeUpdatePhase( playerIndex, SPREAD );
	m_captureTimer += computeUpdatePhase( playerIndex, SPREAD );
	m_hijackTimer += computeUpdatePhase( playerIndex, SPREAD );
	m_retreatTimer += computeUpdatePhase( playerIndex, SPREAD );
	// The scout check's own cycle is already only two seconds, so its full cycle is the window.
	m_scoutTimer += computeUpdatePhase( playerIndex, SCOUT_CHECK_RATE );
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
AIPlayer::~AIPlayer()
{
	clearTeamsInQueue();
}

// ------------------------------------------------------------------------------------------------
/** Invoked when a structure I am building is finished building. */
// ------------------------------------------------------------------------------------------------
void AIPlayer::onStructureProduced( Object *factory, Object *bldg )
{
	// the opening build order is read from these: which building came up on which frame
	DEBUG_LOG(("AI BUILT frame %d player %d '%s', %d in the bank\n", TheGameLogic->getFrame(),
		m_player->getPlayerIndex(), bldg->getTemplate()->getName().str(), m_player->getMoney()->countMoney()));
	m_teamDelay = 0; // Cause the update queues & selection to happen immediately.
	m_buildDelay = 0; // Cause 
	/* Find the info building this. */
	BuildListInfo *info;
	for( info = m_player->getBuildList(); info; info = info->getNext() )
	{
		if (info->getObjectID() != bldg->getID()) continue;
		Dict d;
		d.setAsciiString(TheKey_objectName, info->getBuildingName());
		d.setAsciiString(TheKey_objectScriptAttachment, info->getScript());
		// The build list's health is the health the map author wanted the building to start at.
		// Writing it here, when construction finishes, throws away whatever the scaffold actually
		// has: a structure shot at while it was going up comes out at full health, and one the map
		// wanted damaged comes out damaged twice.  Whatever the scaffold ended on is what it keeps.
		d.setBool(TheKey_objectUnsellable, info->getUnsellable());
		
		info->setUnderConstruction(false);
		bldg->updateObjValuesFromMapProperties(&d);
		// clear the under construction status
		bldg->clearStatus( MAKE_OBJECT_STATUS_MASK2( OBJECT_STATUS_UNDER_CONSTRUCTION, OBJECT_STATUS_RECONSTRUCTING ) );

		// UnderConstruction just cleared, so update our upgrades
		bldg->updateUpgradeModules();

		TheScriptEngine->addObjectToCache(bldg);
		TheScriptEngine->runObjectScript(info->getScript(), bldg);
		if (TheGlobalData->m_debugAI) {
			AsciiString bldgName = bldg->getTemplate()->getName();
			bldgName.concat(" - Building completed.");
			TheScriptEngine->AppendDebugMessage(bldgName, false);
		}
		checkForSupplyCenter(info, bldg);
		return;
	}

	// Look in build list & see if this is spawned from a hole.
	for( info = m_player->getBuildList(); info; info = info->getNext() )
	{
		const ThingTemplate *bldgPlan = TheThingFactory->findTemplate( info->getTemplateName() );
		if (!bldgPlan) {																											 
			continue;
		}		
		if (!bldgPlan->isEquivalentTo(bldg->getTemplate())) {
			continue; // not the same kind of building we're looking for.
		}
		// check for hole.
		if (info->getObjectID() != INVALID_ID) {
			// used to have a building.
			Object *obj = TheGameLogic->findObjectByID( info->getObjectID() );
			if (obj!=NULL) {
				if (obj->isKindOf(KINDOF_REBUILD_HOLE)) {
					RebuildHoleBehaviorInterface *rhbi = RebuildHoleBehavior::getRebuildHoleBehaviorInterfaceFromObject( obj );
					if( rhbi ) {
						ObjectID spawnedID = rhbi->getReconstructedBuildingID();
						if (bldg->getID() == spawnedID) {
							DEBUG_LOG(("AI got rebuilt %s\n", bldgPlan->getName().str()));
							info->setObjectID(bldg->getID());
							return;
						}
					}
				}
			} 
		}
	}

	if (TheGameLogic->getFrame()>0) {
		DEBUG_LOG(("***AI PLAYER-Structure not found in production queue.\n"));
	}
}

// ------------------------------------------------------------------------------------------------
/** See if the building is a supply center, and see how many supply trucks we want. */
// ------------------------------------------------------------------------------------------------
void AIPlayer::checkForSupplyCenter( BuildListInfo *info, Object *bldg )
{
	class SupplyCenterDockUpdate;
	// if it is a supply center, I must have boxes
	static const NameKeyType key_centerUpdate = NAMEKEY("SupplyCenterDockUpdate");
	SupplyCenterDockUpdate *centerModule = (SupplyCenterDockUpdate*)bldg->findUpdateModule( key_centerUpdate );
	if( centerModule  )
	{
		info->setSupplyBuilding(true);
		Int desiredGatherers = 0;
		const AISideInfo *resInfo = TheAI->getAiData()->findSideInfo( m_player );
		if (resInfo) {
			GameDifficulty difficulty = m_difficulty;
			if (difficulty == DIFFICULTY_EASY) {
				desiredGatherers = resInfo->m_easy;
			}
			if (difficulty == DIFFICULTY_NORMAL) {
				desiredGatherers = resInfo->m_normal;
			}
			if (difficulty == DIFFICULTY_HARD) {
				desiredGatherers = resInfo->m_hard;
			}
		}
		//
		// Adaptive harvesters (B6): the three INI numbers do not know how much supply is actually
		// parked next to this centre.  One extra gatherer per warehouse beyond the first within the
		// safe radius, capped at two extra - a centre with three piles beside it can keep more
		// trucks busy, and one with a single pile cannot.
		//
		if( runsOwnAI() && getSkillProfile()->m_adaptiveHarvesters )
		{
			Int warehouses = 0;
			PartitionFilterAcceptByKindOf supplyOnly( MAKE_KINDOF_MASK( KINDOF_SUPPLY_SOURCE ), KINDOFMASK_NONE );
			PartitionFilterAlive filterAlive;
			PartitionFilter *filters[] = { &supplyOnly, &filterAlive, 0 };

			MemoryPoolObjectHolder hold;
			SimpleObjectIterator *nearby = ThePartitionManager->iterateObjectsInRange(
					bldg->getPosition(), TheAI->getAiData()->m_supplyCenterSafeRadius, FROM_CENTER_2D, filters );
			hold.hold( nearby );
			for( Object *w = nearby->first(); w; w = nearby->next() )
				++warehouses;

			Int extra = warehouses - 1;
			if( extra < 0 ) extra = 0;
			if( extra > 2 ) extra = 2;
			desiredGatherers += extra;
		}

		info->setSupplyBuilding(true);
		info->setCurrentGatherers(-1);
		info->setDesiredGatherers(desiredGatherers+1); // get a freebie with the supply depots.
	}
}

/** A warehouse with boxes left, not an enemy's and not beside another player's center, within reach
	* of this supply center.  Any of them: this asked the nearest one only, and an emptied pile hid the
	* full one behind it. */
static Bool hasSuppliesNear( Player *player, const Object *supplyCenter, Real reach )
{
	Real radius = reach + supplyCenter->getGeometryInfo().getBoundingCircleRadius();

	PartitionFilterAcceptByKindOf f1(MAKE_KINDOF_MASK(KINDOF_SUPPLY_SOURCE), KINDOFMASK_NONE);
	PartitionFilterPlayer f2(player, false);	// Only find other.
	PartitionFilterOnMap filterMapStatus;

	PartitionFilter *filters[] = { &f1, &f2, &filterMapStatus, 0 };

	static const NameKeyType key_warehouseUpdate = NAMEKEY("SupplyWarehouseDockUpdate");
	ObjectIterator *iter = ThePartitionManager->iterateObjectsInRange(supplyCenter->getPosition(), radius, FROM_BOUNDINGSPHERE_2D, filters);
	MemoryPoolObjectHolder hold(iter);
	for (Object *supplySource = iter->first(); supplySource; supplySource = iter->next()) {
		SupplyWarehouseDockUpdate *warehouseModule = (SupplyWarehouseDockUpdate*)supplySource->findUpdateModule( key_warehouseUpdate );
		if (!warehouseModule) return TRUE;
		if (warehouseModule->getBoxesStored()*TheGlobalData->m_baseValuePerSupplyBox > 0 &&
				player->getRelationship(supplySource->getTeam()) != ENEMIES &&
				!ResourceGatheringManager::isPileOfAnotherPlayer(player, supplySource)) return TRUE;
	}
	return FALSE;
}

static const TransportContainModuleData *transportContainOf( const ThingTemplate *tmpl );
static Bool isAmbulanceTemplate( const ThingTemplate *tmpl );
static Bool passengersFireFrom( const ThingTemplate *tmpl );
static Bool isHelicopter( const Object *obj );

struct IdleTruckSearch
{
	Player *player;
	const AIPlayer *owner;	///< whose duty Chinooks stand idle on purpose
	const Object *center;
	Object *truck;
	Bool waiting;		///< a gatherer stands idle beside a center that still has boxes
};

static void considerIdleTruck( Object *obj, void *userData )
{
	IdleTruckSearch *search = (IdleTruckSearch *)userData;
	if( search->truck || !obj->isKindOf( KINDOF_HARVESTER ) || obj->isEffectivelyDead() || obj->isContained() || obj->getAI() == NULL ||
			search->owner->isDutyChinook( obj ) )
		return;
	AIUpdateInterface *ai = obj->getAI();
	SupplyTruckAIInterface *truckAI = ai->getSupplyTruckAIInterface();
	if( truckAI == NULL || !ai->isIdle() )
		return;
	DozerAIInterface *dozerAI = ai->getDozerAIInterface();
	if( dozerAI && dozerAI->isAnyTaskPending() )
		return;		// a GLA worker between jobs
	const Object *dock = TheGameLogic->findObjectByID( truckAI->getPreferredDockID() );
	if( dock && dock != search->center && hasSuppliesNear( search->player, dock, SUPPLY_CENTER_CLOSE_DIST ) )
	{
		// its own center still has boxes to fetch and it stands idle all the same: the pile has no
		// room for it, and it would have none for a new one
		search->waiting = TRUE;
		return;
	}
	// the truck looks for a pile as far as its own scan reaches once it is at the center, and a
	// Chinook's reaches far past the ground beside the center
	if( !hasSuppliesNear( search->player, search->center, truckAI->getWarehouseScanDistance() ) )
		return;
	search->truck = obj;
}

/** A truck of this player's standing about with nothing to fetch, that could fetch from this center. */
static Object *findIdleTruck( Player *player, const AIPlayer *owner, const Object *center, Bool *waiting )
{
	IdleTruckSearch search;
	search.player = player;
	search.owner = owner;
	search.center = center;
	search.truck = NULL;
	search.waiting = FALSE;
	player->iterateObjects( considerIdleTruck, &search );
	*waiting = search.waiting;
	return search.truck;
}

// ------------------------------------------------------------------------------------------------
/** Queue up a supply truck to be built. */
// ------------------------------------------------------------------------------------------------
void AIPlayer::queueSupplyTruck( void )
{
	Bool truckInQueue = false;
	for ( DLINK_ITERATOR<TeamInQueue> iter = iterate_TeamBuildQueue(); !iter.done(); iter.advance())
	{
		TeamInQueue *team = iter.cur();
		WorkOrder *order;
		for( order = team->m_workOrders; order; order = order->m_next )
		{
			// GLA dozers (workers) are also resource gatherers, so make sure it isn't a worker. jba.
			if (order->m_isResourceGatherer) {
				truckInQueue = true;
			}
		}
	}	
	
	if (truckInQueue) {
		return; // already building a supply truck.
	}
	Int totalHarvesters = 0;

	// See how many harvesters we have servicing this supply src.
	// Scan my units.
	Player::PlayerTeamList::const_iterator it;
	for (it = m_player->getPlayerTeams()->begin(); it != m_player->getPlayerTeams()->end(); ++it) {
		for (DLINK_ITERATOR<Team> iter = (*it)->iterate_TeamInstanceList(); !iter.done(); iter.advance()) {
			Team *team = iter.cur();
			if (!team) {
				continue;
			}			
			for (DLINK_ITERATOR<Object> objIter = team->iterate_TeamMemberList(); !objIter.done(); objIter.advance()) {
				Object *obj = objIter.cur();
				if (!obj)  continue;
				if (!obj->isKindOf(KINDOF_HARVESTER) || isDutyChinook(obj) || obj->getID() == m_lentChinook) continue;
				if (!obj->getAI()) continue;

				SupplyTruckAIInterface* supplyTruckAI = obj->getAI()->getSupplyTruckAIInterface();
				if( supplyTruckAI )	{
					totalHarvesters++;
				}
			}
		}
	}

	/* Find the info building this. */
	for( BuildListInfo *info = m_player->getBuildList(); info; info = info->getNext() )
	{
		if (info->isSupplyBuilding() == false) continue;
		Int desiredGatherers = info->getDesiredGatherers();
		Int curGatherers = info->getCurrentGatherers();

		if (curGatherers>=desiredGatherers) {
			// Check & see if any have died.
			Object *supplyCenter = TheGameLogic->findObjectByID(info->getObjectID());
			// Check for supplies.
			if (supplyCenter) {
				if (supplyCenter->isKindOf(KINDOF_REBUILD_HOLE)) {
					continue; // don't consider rebuild holes.
				}
				// Make sure we have a supplies near it.
				if (!hasSuppliesNear(m_player, supplyCenter, SUPPLY_CENTER_CLOSE_DIST)) {
					continue;
				}
				// Ok, it has supplies available near it.
				checkForSupplyCenter(info, supplyCenter);
				Int curGatherers = 0;
				// See how many harvesters we have servicing this supply src.
				// Scan my units.
				Player::PlayerTeamList::const_iterator it;
				for (it = m_player->getPlayerTeams()->begin(); it != m_player->getPlayerTeams()->end(); ++it) {
					for (DLINK_ITERATOR<Team> iter = (*it)->iterate_TeamInstanceList(); !iter.done(); iter.advance()) {
						Team *team = iter.cur();
						if (!team) {
							continue;
						}			
						for (DLINK_ITERATOR<Object> objIter = team->iterate_TeamMemberList(); !objIter.done(); objIter.advance()) {
							Object *obj = objIter.cur();
							if (!obj)  continue;
							if (!obj->isKindOf(KINDOF_HARVESTER) || isDutyChinook(obj) || obj->getID() == m_lentChinook) continue;
							if (!obj->getAI()) continue;

							SupplyTruckAIInterface* supplyTruckAI = obj->getAI()->getSupplyTruckAIInterface();
							if( supplyTruckAI )	{
								ObjectID dock = supplyTruckAI->getPreferredDockID();
								if (dock == supplyCenter->getID()) {
									curGatherers++;
									if (!supplyTruckAI->isCurrentlyFerryingSupplies()) {
										// Note - although this is the ai, we are sending in CMD_FROM_PLAYER.
										// This causes the dock object to stick in the docking interface.
										// The supply truck ai issues dock commands, and they become confused.
										// Thus, player.  jba.  ;(
										obj->getAI()->aiDock(supplyCenter, CMD_FROM_PLAYER);
									}
								}
							}
						}
					}
				}
				//DEBUG_LOG(("Expected %d harvesters, found %d, need %d\n", info->getDesiredGatherers(), 
				//	curGatherers, info->getDesiredGatherers()-curGatherers) );
				info->setCurrentGatherers(curGatherers);
			}
		} else {
			/* See if we have any "loose" harvesters (cause my supply center got nuked.) */
			Player::PlayerTeamList::const_iterator it;
			for (it = m_player->getPlayerTeams()->begin(); it != m_player->getPlayerTeams()->end(); ++it) {
				for (DLINK_ITERATOR<Team> iter = (*it)->iterate_TeamInstanceList(); !iter.done(); iter.advance()) {
					Team *team = iter.cur();
					if (!team) continue;
					for (DLINK_ITERATOR<Object> objIter = team->iterate_TeamMemberList(); !objIter.done(); objIter.advance()) {
						Object *obj = objIter.cur();
						if (!obj)  continue;
						if (!obj->isKindOf(KINDOF_HARVESTER) || isDutyChinook(obj) || obj->getID() == m_lentChinook) continue;
						if (!obj->getAI()) continue;

						SupplyTruckAIInterface* supplyTruckAI = obj->getAI()->getSupplyTruckAIInterface();
						if( supplyTruckAI )	{
							ObjectID dock = supplyTruckAI->getPreferredDockID();
							if (TheGameLogic->findObjectByID(dock)!=NULL) continue;
							if (supplyTruckAI->isCurrentlyFerryingSupplies() || supplyTruckAI->isForcedIntoWantingState()) 
							{
								// This thinks he is a gatherer, but doesn't have a preferred dock id.
								Object *center = TheGameLogic->findObjectByID(info->getObjectID());
								// a GLA building that died leaves a rebuild hole in its build list slot, and a
								// hole is no dock: the gatherer drove to it and stood there
								if (center && !center->isKindOf(KINDOF_REBUILD_HOLE)) {
									info->setCurrentGatherers(info->getCurrentGatherers()+1);
									// Note - although this is the ai, we are sending in CMD_FROM_PLAYER.
									// This causes the dock object to stick in the docking interface.
									// The supply truck ai issues dock commands, and they become confused.
									// Thus, player.  jba.  ;(
									obj->getAI()->aiDock(center, CMD_FROM_PLAYER);
									DEBUG_LOG(("Re-attaching supply truck to supply center.\n"));
									return;
								}
							}
						}
					}
				}
			}
			/* A truck whose pile ran dry regroups at the nearest supply center, which is its own dry one,
				 and looks again only as far as its scan reaches, so the trucks of a picked-clean pile stood
				 round it for the rest of the match while this center bought new ones, three times what it
				 wanted: a Hard China kept nine trucks, eight of them idle, from frame 5000 on. An idle one
				 comes here first, and a center with nothing beside it gets no truck at all.

				 The idle one was only looked for when a pile stood right beside the center, two hundred
				 units, and a USA center whose near pile was gone sent nobody for the one a Chinook could
				 reach from it. The Chinooks hung over the command center and more were bought for the
				 centers that still had boxes. Now the idle one is asked for first, against its own reach,
				 and no center buys while a gatherer of another stands idle beside boxes it cannot get at. */
			Object *center = TheGameLogic->findObjectByID(info->getObjectID());
			Bool truckWaiting = FALSE;
			if (center && !center->isKindOf(KINDOF_REBUILD_HOLE)) {
				Object *idleTruck = findIdleTruck(m_player, this, center, &truckWaiting);
				if (idleTruck) {
					DEBUG_LOG(("AI GATHERER frame %d player %d sends idle '%s' %d to center %d at (%.0f,%.0f), %d of %d there\n",
						TheGameLogic->getFrame(), m_player->getPlayerIndex(), idleTruck->getTemplate()->getName().str(), idleTruck->getID(),
						center->getID(), center->getPosition()->x, center->getPosition()->y, max(info->getCurrentGatherers(), 0) + 1, desiredGatherers));
					info->setCurrentGatherers(max(info->getCurrentGatherers(), 0) + 1);
					idleTruck->getAI()->aiDock(center, CMD_FROM_PLAYER);	// from the player, so it stays his preferred dock (see above)
					return;
				}
				if (!hasSuppliesNear(m_player, center, SUPPLY_CENTER_CLOSE_DIST)) {
					continue;
				}
			}
			if (truckWaiting) {
				continue;
			}
			if (totalHarvesters >= desiredGatherers*3) {
				continue; // we got lotsa gatherers.
			}
			Bool canBuildUnits = m_player->getCanBuildUnits();
			// If we need a supply truck thingy, turn on unit building for a moment.
			m_player->setCanBuildUnits(true);
			const ThingTemplate *tTemplate = TheThingFactory->firstTemplate();
			while (tTemplate) {	 
				// never a Chinook its riders can shoot out of: that is a gunship, and the Air Force General
				// bought its 1,200 Combat Chinook as a gatherer here at frame 8,636 of a Medium match
				Bool isSupplyTruck = tTemplate->isKindOf(KINDOF_HARVESTER) && !passengersFireFrom(tTemplate);
				if (isSupplyTruck) {
					Object *factory = findFactory(tTemplate, false);
					if (factory) {
						// we can build one.
						WorkOrder *order = newInstance(WorkOrder);
						order->m_thing = tTemplate;
						order->m_factoryID = INVALID_ID;
						order->m_numRequired = 1;
						order->m_required = true;
						order->m_isResourceGatherer =true;
						// prepend to head of list
						order->m_next = NULL;
						TeamInQueue *team = newInstance(TeamInQueue);
						// Put in front of queue.
						prependTo_TeamBuildQueue(team);
						team->m_priorityBuild = true;
						team->m_workOrders = order;
						team->m_frameStarted = TheGameLogic->getFrame();
						// Stick it on the default team
						team->m_team = m_player->getDefaultTeam(); 
						AsciiString teamName = "Supply truck - building one at the ";
						teamName.concat(factory->getTemplate()->getName());
						TheScriptEngine->AppendDebugMessage(teamName, false);
						DEBUG_LOG(("AI GATHERER frame %d player %d buys '%s' for center %d, %d of %d there, %d harvesters in all, %d in the bank\n",
							TheGameLogic->getFrame(), m_player->getPlayerIndex(), tTemplate->getName().str(), info->getObjectID(),
							info->getCurrentGatherers(), desiredGatherers, totalHarvesters, m_player->getMoney()->countMoney()));
						m_teamDelay = 0;
						if (info->getCurrentGatherers()==-1) {
							// First one is automatic. jba.
							order->m_factoryID = factory->getID();
							info->setCurrentGatherers(0);
						}	else {
							startTraining( order, team->m_priorityBuild, team->m_team->getName());
						}
						break;
					}
				}
				tTemplate = tTemplate->friend_getNextTemplate();
			}
			m_player->setCanBuildUnits(canBuildUnits);
		}
	}
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
static void deleteQueue(TeamInQueue* o)
{
	if (o)
	{
		o->deleteInstance();
	}
}

// ------------------------------------------------------------------------------------------------
/** Clear the current work order */
// ------------------------------------------------------------------------------------------------
void AIPlayer::clearTeamsInQueue( void )
{
	removeAll_TeamBuildQueue(deleteQueue);
	removeAll_TeamReadyQueue(deleteQueue);
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
Object *AIPlayer::buildStructureNow(const ThingTemplate *bldgPlan, BuildListInfo *info)
{

	// inst-construct the building.  Turkiye's build list names AmericaCommandCenter (the skirmish
	// scripts ask for it by that name); ThingFactory::newObject makes a Turkiye player's the Turkish one.
	Object *bldg = TheBuildAssistant->buildObjectNow( NULL, 
																						bldgPlan,
																						info->getLocation(),
																						info->getAngle(),
																						m_player );

	// store the object with the build order  
	if (bldg)
	{
		Dict d;
		d.setAsciiString(TheKey_objectName, info->getBuildingName());
		d.setAsciiString(TheKey_objectScriptAttachment, info->getScript());
		// The build list's health is the health the map author wanted the building to start at.
		// Writing it here, when construction finishes, throws away whatever the scaffold actually
		// has: a structure shot at while it was going up comes out at full health, and one the map
		// wanted damaged comes out damaged twice.  Whatever the scaffold ended on is what it keeps.
		d.setBool(TheKey_objectUnsellable, info->getUnsellable());
		
		bldg->updateObjValuesFromMapProperties(&d);

		info->setObjectID( bldg->getID() );
		info->setObjectTimestamp( TheGameLogic->getFrame()+1 );	// has to be non-zero, so just add 1.

		// clear the under construction status
		bldg->clearStatus( MAKE_OBJECT_STATUS_MASK2( OBJECT_STATUS_UNDER_CONSTRUCTION, OBJECT_STATUS_RECONSTRUCTING ) );

		// UnderConstruction just cleared, so update our upgrades
		bldg->updateUpgradeModules();

		if (TheGlobalData->m_debugAI) {
			AsciiString bldgName = bldgPlan->getName();
			bldgName.concat(" - Building completed.");
			TheScriptEngine->AppendDebugMessage(bldgName, false);
		}
		TheScriptEngine->addObjectToCache(bldg);
		TheScriptEngine->runObjectScript(info->getScript(), bldg);
		checkForSupplyCenter(info, bldg);
		ExitInterface *exitInterface = bldg->getObjectExitInterface();
		if( exitInterface )
		{
			Coord3D rallyPoint;
			Bool gotOffset = false;
			if (fabs(info->getRallyOffset()->x) > 1.0f || fabs(info->getRallyOffset()->y)>1.0f) {
				gotOffset = true;	// was a bare statement, so RallyPointOffset was never applied
			}
			if (!exitInterface->getNaturalRallyPoint(rallyPoint)) {
				rallyPoint = *info->getLocation();
			}
			if (gotOffset) {
				rallyPoint.x += info->getRallyOffset()->x;
				rallyPoint.y += info->getRallyOffset()->y;
				exitInterface->setRallyPoint(&rallyPoint);
			}
		}
	} // bldg built
	return bldg;
}

/** How far buildStructureWithDozer's flood fill may reach from the build list spot, in pathfind
	* cells either way: half the width of the square rings it replaced. */
static const Int BUILD_SEARCH_CELLS = 5;
static const Int SKIRMISH_BUILD_SEARCH_CELLS = 60;
/// Budgets fitted at 30 logic frames a second, halved at 60 so a search runs at the same pace per second.
static const Int BUILD_PROBES_PER_FRAME = 32 / LOGICFRAMES_PER_RETAIL_FRAME;			///< isLocationLegalToBuild calls one frame may spend
static const Int BUILD_EXPANSIONS_PER_FRAME = 512 / LOGICFRAMES_PER_RETAIL_FRAME;	///< cells one frame may take off the flood's queue

/** How far from the base center, in base radii, a building that stays home may stand.  Two, the
	* reach the home guard already answers to (the 2.0f * m_baseRadius lists).  1.25 was tried first:
	* over ten Brutal matches on Twilight Flame the drop zones asked for fell from 87 to 13 and the
	* Scud Storms from 46 to 6, with the money in the bank and no room to put them. */
static const Real BASE_EDGE_REACH = 2.0f;

/** What a computer buys for itself and can only keep at home: the buildings that pay out on a timer,
	* the superweapons and the tech building they wait on.  The search for room used to walk outward
	* until the map ended and the dozer's flood reached sixty cells past that, so a full base put its
	* black markets and its Scud Storm out on ground nobody held.  These go inside the base or at its
	* edge, or are not built. */
static Bool staysHome( const ThingTemplate *tmpl )
{
	return tmpl->isKindOf( KINDOF_FS_SUPPLY_DROPZONE ) || tmpl->isKindOf( KINDOF_FS_BLACK_MARKET ) ||
		tmpl->isKindOf( KINDOF_FS_INTERNET_CENTER ) || tmpl->isKindOf( KINDOF_FS_SUPERWEAPON ) ||
		tmpl->isKindOf( KINDOF_FS_ADVANCED_TECH );
}

// ------------------------------------------------------------------------------------------------
/** Whether the building search may flood through a cell: ground a vehicle could cross, the AI's own
	* or anyone's structure standing on it, or the deck of a bridge that is still up over water or a
	* cliff. worldPos is the cell's position in the world, for the bridge test. */
// ------------------------------------------------------------------------------------------------
static Bool isBuildSearchWalkable( Int cellX, Int cellY, const Coord3D *worldPos )
{
	const PathfindCell *cell = TheAI->pathfinder()->getCell( LAYER_GROUND, cellX, cellY );
	if( cell == NULL )
		return FALSE;		// off the map

	switch( cell->getType() )
	{
		case PathfindCell::CELL_CLEAR:
		case PathfindCell::CELL_RUBBLE:
		case PathfindCell::CELL_OBSTACLE:
			return TRUE;
		default:
			break;
	}

	for( Bridge *bridge = TheTerrainLogic->getFirstBridge(); bridge; bridge = bridge->getNext() )
	{
		if( bridge->peekBridgeInfo()->curDamageState != BODY_RUBBLE && bridge->isPointOnBridge( worldPos ) )
			return TRUE;
	}
	return FALSE;
}

/** The pathfind grid's size in cells, for an AIGroundFlood over all of it. */
static void groundGridSize( Int *width, Int *height )
{
	const ICoord2D *hi = TheAI->pathfinder()->getExtent();
	*width = hi->x + 1;
	*height = hi->y + 1;
}

/** isBuildSearchWalkable for an AIGroundFlood.  userData, when set, is a field from groundFromBase:
	* a cell the base cannot drive to is not entered either. */
static Bool groundFloodWalkable( Int x, Int y, void *userData )
{
	const std::vector<UnsignedShort> *fromBase = (const std::vector<UnsignedShort> *)userData;
	if( fromBase )
	{
		Int width, height;
		groundGridSize( &width, &height );
		if( (*fromBase)[ y * width + x ] == AI_GROUND_UNREACHED )
			return FALSE;
	}
	Coord3D at;
	at.x = ( x + 0.5f ) * PATHFIND_CELL_SIZE_F;
	at.y = ( y + 0.5f ) * PATHFIND_CELL_SIZE_F;
	at.z = 0.0f;
	return isBuildSearchWalkable( x, y, &at );
}

/** A flood over the whole grid from these points, run to the end, into field. */
static void floodWholeGrid( const Coord3D *from, Int count, std::vector<UnsignedShort> &field )
{
	Int width, height;
	groundGridSize( &width, &height );
	AIGroundFlood flood( width, height, groundFloodWalkable, NULL );
	for( Int i = 0; i < count; ++i )
	{
		ICoord2D cell;
		TheAI->pathfinder()->worldToCell( &from[ i ], &cell );
		flood.addSource( cell.x, cell.y );
	}
	Int x, y, d;
	while( flood.next( &x, &y, &d ) )
		;
	field.swap( flood.field() );
}

/** Which bridges are down, one bit each in list order, folded past 32.  Part of every ground field's
	* key: a field kept across a bridge falling would answer differently from one flooded after a
	* savegame load, which starts with no field. */
static UnsignedInt bridgesDownKey( void )
{
	UnsignedInt key = 0;
	for( Bridge *bridge = TheTerrainLogic->getFirstBridge(); bridge; bridge = bridge->getNext() )
		key = ( ( key << 1 ) | ( key >> 31 ) ) ^ ( bridge->peekBridgeInfo()->curDamageState == BODY_RUBBLE ? 1u : 0u );
	return key;
}

/** The field's value at a world position, AI_GROUND_UNREACHED for ground not reached. */
static Int groundAt( const std::vector<UnsignedShort> &field, const Coord3D *pos )
{
	Int width, height;
	groundGridSize( &width, &height );
	ICoord2D cell;
	TheAI->pathfinder()->worldToCell( pos, &cell );
	return field[ cell.y * width + cell.x ];
}

// ------------------------------------------------------------------------------------------------
/** How far every cell of the map is from the base center by the ground.  The building searches and
	* the defence scoring all ask the same question, whether a spot is joined to the base by ground a
	* vehicle can cross, and a ring of straight-line distances cannot answer it: the far side of a
	* valley or a ledge behind a cliff is close on the map and a long drive round, or no drive at all.
	* One flood of the whole map, then a lookup until the base center moves.
	* Structures count as ground (isBuildSearchWalkable), so the base's own buildings do not cut it
	* in two.  A bridge falling or put back floods it again. */
// ------------------------------------------------------------------------------------------------
const std::vector<UnsignedShort> &AIPlayer::groundFromBase( void )
{
	const UnsignedInt bridges = bridgesDownKey();
	if( m_groundFromBase.empty() || m_groundFromBaseAt.x != m_baseCenter.x || m_groundFromBaseAt.y != m_baseCenter.y ||
			m_groundFromBaseBridges != bridges )
	{
		m_groundFromBaseAt = m_baseCenter;
		m_groundFromBaseBridges = bridges;
		floodWholeGrid( &m_baseCenter, 1, m_groundFromBase );
	}
	return m_groundFromBase;
}

// ------------------------------------------------------------------------------------------------
const std::vector<UnsignedShort> &AIPlayer::groundFromThreats( const Coord3D *threats, Int count )
{
	const UnsignedInt bridges = bridgesDownKey();
	Bool same = !m_groundFromThreats.empty() && (Int)m_groundThreatsAt.size() == count && m_groundThreatsBridges == bridges;
	for( Int i = 0; same && i < count; ++i )
		same = m_groundThreatsAt[ i ].x == threats[ i ].x && m_groundThreatsAt[ i ].y == threats[ i ].y;
	if( !same )
	{
		m_groundThreatsAt.assign( threats, threats + count );
		m_groundThreatsBridges = bridges;
		floodWholeGrid( threats, count, m_groundFromThreats );
	}
	return m_groundFromThreats;
}

// ------------------------------------------------------------------------------------------------
Bool AIPlayer::reachableFromBase( const Coord3D *pos )
{
	return !m_baseCenterSet || groundAt( groundFromBase(), pos ) != AI_GROUND_UNREACHED;
}

// ------------------------------------------------------------------------------------------------
/** The entry's spot cannot take the building.  The base builder takes the first priority entry on
	* the list and nothing else that pass, so an entry that fails the same way every pass stops the
	* whole base: a Hard GLA's Scud Storm flooded 7,406 cells, found nothing, and flooded again for
	* 17,000 frames while 40,000 sat in the bank and nine buildings waited behind it.  A priority
	* entry is a one-off request, and whoever asked for it places it again with a fresh search, so
	* it is blanked: every walk of the build list skips an entry with no template, and the base
	* builder takes a priority entry whatever its rebuild count says.  An entry of the map's own plan
	* waits out the rebuild delay and lets the rest of the list go first. */
// ------------------------------------------------------------------------------------------------
static void giveUpOnSpot( BuildListInfo *info )
{
	if( info->isPriorityBuild() )
		info->setTemplateName( AsciiString::TheEmptyString );
	else
		info->setObjectTimestamp( TheGameLogic->getFrame() + 1 );
}

// ------------------------------------------------------------------------------------------------
/** A site this AI placed and cannot finish is cancelled for its money back, the way a player cancels
	* one, and its spot left alone for the rung's m_siteRetrySeconds.  A supply centre put down at a far
	* dock in enemy ground stood unbuilt all match: every dozer sent to it was killed on the way, the
	* money was gone, and the scaffold kept the dock (and every gun the scripts asked for beside it)
	* from going anywhere else.  A priority entry is blanked, as giveUpOnSpot does, for whoever asked to
	* ask again; a map plan entry waits out the retry and gets its rebuild back. */
// ------------------------------------------------------------------------------------------------
Bool AIPlayer::watchSite( BuildListInfo *info, Object *bldg )
{
	const UnsignedInt now = TheGameLogic->getFrame();
	AISiteWatch *watch = NULL;
	for( size_t i = 0; i < m_siteWatches.size(); )
	{
		const Object *site = TheGameLogic->findObjectByID( m_siteWatches[ i ].m_site );
		if( site == NULL || !site->testStatus( OBJECT_STATUS_UNDER_CONSTRUCTION ) )
		{
			m_siteWatches.erase( m_siteWatches.begin() + i );		// finished, or gone
			continue;
		}
		if( site == bldg )
			watch = &m_siteWatches[ i ];
		++i;
	}
	if( watch == NULL )
	{
		AISiteWatch fresh;
		fresh.m_site = bldg->getID();
		fresh.m_percent = bldg->getConstructionPercent();
		fresh.m_progressFrame = now;
		fresh.m_lostBuilder = INVALID_ID;
		fresh.m_buildersLost = 0;
		m_siteWatches.push_back( fresh );
		watch = &m_siteWatches.back();
	}

	const AIDifficultyProfile *profile = getSkillProfile();
	const ObjectID builderID = bldg->getBuilderID();
	const Object *builder = TheGameLogic->findObjectByID( builderID );
	// at home a site that does not move is waiting for a busy dozer, not for one that cannot get there
	const UnsignedInt stallFrames = isAtHome( bldg->getPosition() ) ? 0 :
		(UnsignedInt)REAL_TO_INT( profile->m_siteStallSeconds * LOGICFRAMES_PER_SECOND );
	const AISiteVerdict verdict = aiWatchSite( watch, bldg->getConstructionPercent(), builderID,
		builder != NULL && !builder->isEffectivelyDead(), now, stallFrames, profile->m_siteBuildersLost );
	if( verdict == AISITE_KEEP )
		return FALSE;

	const UnsignedInt retryFrames = (UnsignedInt)REAL_TO_INT( profile->m_siteRetrySeconds * LOGICFRAMES_PER_SECOND );
	const Int refund = bldg->testStatus( OBJECT_STATUS_RECONSTRUCTING ) ? 0 : bldg->getTemplate()->calcCostToBuild( m_player );
	m_player->getMoney()->deposit( refund, FALSE );
	DEBUG_LOG(("AI SITE frame %d player %d gives up '%s' at (%.0f,%.0f), %s, %.0f%% built, %d refunded, spot free at frame %d\n",
		now, m_player->getPlayerIndex(), bldg->getTemplate()->getName().str(), bldg->getPosition()->x, bldg->getPosition()->y,
		verdict == AISITE_STALLED ? "no progress" : "builders lost", bldg->getConstructionPercent(), refund, now + retryFrames));

	for( size_t i = 0; i < m_cooledSpots.size(); )
	{
		if( m_cooledSpots[ i ].m_until <= now )
			m_cooledSpots.erase( m_cooledSpots.begin() + i );
		else
			++i;
	}
	AICooledSpot spot;
	spot.m_pos = *bldg->getPosition();
	spot.m_until = now + retryFrames;
	m_cooledSpots.push_back( spot );

	// the dock it was for stops being the one the scripts' guns and factories go beside
	const Object *warehouse = TheGameLogic->findObjectByID( m_curWarehouseID );
	if( warehouse && aiSpotCooling( m_cooledSpots, warehouse->getPosition(), AI_COOLED_DOCK_RADIUS, now ) )
		m_curWarehouseID = INVALID_ID;

	info->setObjectID( INVALID_ID );
	info->setUnderConstruction( false );
	if( info->isPriorityBuild() )
	{
		info->setTemplateName( AsciiString::TheEmptyString );
	}
	else
	{
		info->incrementNumRebuilds();
		info->setObjectTimestamp( now + retryFrames );
	}
	bldg->kill();		// its dozer drops the job, as when a player cancels
	return TRUE;
}

// ------------------------------------------------------------------------------------------------
Bool AIPlayer::spotCooling( const Coord3D *pos )
{
	return aiSpotCooling( m_cooledSpots, pos, AI_COOLED_SITE_RADIUS, TheGameLogic->getFrame() );
}

// ------------------------------------------------------------------------------------------------
/** The base builder has its next site and is waiting for the money.  An idle dozer walks there now
	* rather than from wherever it stopped once the money comes, so the building starts on arrival.
	* Only one standing idle: never one on a job, the repair dozer, or a worker hauling supplies. */
// ------------------------------------------------------------------------------------------------
void AIPlayer::stageDozer( Object *dozer, const Coord3D *site )
{
	if( !runsOwnAI() || !getSkillProfile()->m_stageDozers || dozer->getID() == m_repairDozer )
		return;
	AIUpdateInterface *ai = dozer->getAI();
	if( ai->getDozerAIInterface()->isAnyTaskPending() || !ai->isIdle() )
		return;
	SupplyTruckAIInterface *supplyTruckAI = ai->getSupplyTruckAIInterface();
	if( supplyTruckAI && (supplyTruckAI->isCurrentlyFerryingSupplies() || supplyTruckAI->isForcedIntoWantingState()) )
		return;
	const Real STANDOFF = 4 * PATHFIND_CELL_SIZE_F;		// beside the footprint, not on it
	const Real dx = dozer->getPosition()->x - site->x;
	const Real dy = dozer->getPosition()->y - site->y;
	const Real distSqr = dx*dx + dy*dy;
	if( distSqr <= sqr( 3 * STANDOFF ) || !reachableFromBase( site ) )
		return;
	const Real dist = sqrt( distSqr );
	Coord3D to = *site;
	to.x += dx / dist * STANDOFF;
	to.y += dy / dist * STANDOFF;
	to.z = TheTerrainLogic->getGroundHeight( to.x, to.y );
	ai->aiMoveToPosition( &to, CMD_FROM_AI );
	DEBUG_LOG(("AI STAGE frame %d player %d dozer %d walks %.0f ahead to (%.0f,%.0f)\n",
		TheGameLogic->getFrame(), m_player->getPlayerIndex(), dozer->getID(), dist, site->x, site->y));
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
Object *AIPlayer::buildStructureWithDozer(const ThingTemplate *bldgPlan, BuildListInfo *info)
{
	// A site given up here lately is left alone until its wait runs out.
	if (runsOwnAI() && spotCooling(info->getLocation())) {
		giveUpOnSpot(info);
		return NULL;
	}
	// Find a dozer.
	Object *dozer = findDozer(info->getLocation());
	if (dozer==NULL && runsOwnAI() && getSkillProfile()->m_economyBuildings && isPowerThin() &&
			bldgPlan->isKindOf(KINDOF_FS_POWER) && !bldgPlan->isKindOf(KINDOF_CASH_GENERATOR)) {
		// every dozer busy and the power running out: a Hard AI takes one off whatever it is doing. A China
		// asked for a plant with 26,000 in the bank and waited 95 seconds, going dark half way, because
		// every dozer was on something else. The half-built thing it leaves is resumed by the next dozer
		// free, the way a dozer killed on the job is replaced.
		dozer = findNearestDozer(info->getLocation());
	}
	if (dozer==NULL) {
		return NULL;
	}
	// Check available funds, and with a Hard AI whether the money is saved for something better.
	Money *money = m_player->getMoney();
	const Int cost = bldgPlan->calcCostToBuild(m_player);
	if (!purchaseAllowed(bldgPlan->getTemplateID(), cost, purchaseKindOf(bldgPlan), TRUE, bldgPlan->getName(), personaBuildingScore(bldgPlan)) ||
			(Int)money->countMoney()<cost) {
		stageDozer(dozer, info->getLocation());		// the site is known, only the money is not there yet
		return NULL;
	}

	/* One building placement per logic frame, across every computer player.
		 Placing a building is the expensive half of base building: when the chosen spot is taken,
		 isLocationLegalToBuild is called over a ring of candidate positions reaching 120 pathfind
		 cells for a skirmish AI, and finding a spot out at the edge of that costs 10ms - measured.
		 One of those is a frame nobody notices; the slow frames in an eight-player match were two
		 landing together, because a completed building shortcuts the timer that was meant to keep
		 the players apart.

		 The loser waits exactly one frame - a sixtieth of a second on a building that takes half a
		 minute to put up. The order is the player list order, so every machine defers the same
		 player on the same frame and a replay still matches. */
	if (!TheAI->claimBuildingPlacement(TheGameLogic->getFrame())) {
		m_buildDelay = 1;		// try again next frame; doBaseBuilding leaves any value >= 1 alone
		return NULL;
	}
	// construct the building
	Coord3D pos = *info->getLocation();
	pos.z += TheTerrainLogic->getGroundHeight(pos.x, pos.y);
	if( !dozer->getAIUpdateInterface() )
	{
		return NULL;
	}
	Real angle = info->getAngle();
 	if( TheBuildAssistant->isLocationLegalToBuild( &pos, bldgPlan, angle,
																								 BuildAssistant::NO_ENEMY_OBJECT_OVERLAP,
																								 dozer, m_player ) != LBC_OK ) {
		// If there's enemy units or structures, don't build/rebuild.
		TheTerrainVisual->removeAllBibs();	// isLocationLegalToBuild adds bib feedback, turn it off.  jba.
		giveUpOnSpot(info);
		return NULL;
	}

	// validate the the position to build at is valid
	if( TheBuildAssistant->isLocationLegalToBuild( &pos, bldgPlan, angle,
																								 BuildAssistant::CLEAR_PATH |
																								 BuildAssistant::TERRAIN_RESTRICTIONS |
																								 BuildAssistant::NO_OBJECT_OVERLAP,
																								 dozer, m_player ) != LBC_OK ) {
			// Warn.
			AsciiString bldgName = bldgPlan->getName();
			bldgName.concat(" - Dozer unable to place.  Attempting to adjust position.");
			TheScriptEngine->AppendDebugMessage(bldgName, false);

			/* The spot in the build list is taken, so this floods outwards from it one pathfind cell
				 at a time, nearest first, and takes the first cell where the building fits. The square
				 rings it replaced took the first legal position on a ring, and that could be the far
				 side of a cliff or a river from the base: close in a straight line, a long drive round.
				 The flood only walks ground joined to the spot - clear ground, rubble, structures
				 standing on it, and bridges that are still up - so nearest means nearest by the ground.

				 Every position tried is a call to isLocationLegalToBuild, a partition query and a
				 terrain sample, and a whole search in one frame was measured at 46ms in a four-player
				 match. So each frame gets a budget of positions and cells, and the queue is kept
				 between frames: the cells come off it in the same order whichever frame runs them, so
				 the spot it settles on is the spot it would have found in one go. The budget counts
				 positions and not milliseconds, because a stopwatch would try a different number of
				 them on a slower machine and the two would desync.

				 When nothing fits, EA settled for the original spot, checked with
				 NO_ENEMY_OBJECT_OVERLAP alone. That option skips every structure that is not an
				 enemy's, the AI's own buildings included, and it is how a base grew buildings inside
				 buildings. No spot now means no building from this entry: giveUpOnSpot drops it or
				 sets it aside, so the rest of the list is not held behind it. */
			static const Int NEIGHBOURS[8][2] = { {1,0}, {-1,0}, {0,1}, {0,-1}, {1,1}, {-1,1}, {1,-1}, {-1,-1} };
			// EA's reach and stride, keyed as EA keyed them: any skirmish AI searched wide, its own map or not
			const Int searchRadius = isSkirmishAI() ? SKIRMISH_BUILD_SEARCH_CELLS : BUILD_SEARCH_CELLS;
			const Int searchWidth = 2*searchRadius + 1;
			const Int probeStride = isSkirmishAI() ? 2 : 1;		// the rings tried every other cell for a skirmish AI too
			// A home building the skirmish AI asked for stays within the base's edge and away from an
			// enemy.  The flood only tested that the building fits.
			const Bool home = runsOwnAI() && info->isPriorityBuild() && m_baseCenterSet && staysHome(bldgPlan);
			ICoord2D origin;
			TheAI->pathfinder()->worldToCell(&pos, &origin);
			if (m_buildSearchCells.empty() || m_buildSearchPlan != bldgPlan ||
					m_buildProbePos.x != pos.x || m_buildProbePos.y != pos.y) {
				m_buildSearchPlan = bldgPlan;
				m_buildProbePos = pos;
				m_buildSearchCells.clear();
				m_buildSearchCells.push_back(origin);
				m_buildSearchNext = 0;
				m_buildSearchSeen.assign(searchWidth*searchWidth, 0);
				m_buildSearchSeen[searchRadius*searchWidth + searchRadius] = 1;
			}

			Bool valid = false;
			Coord3D newPos = pos;
			Int probes = 0;
			Int expansions = 0;
			while (m_buildSearchNext < (Int)m_buildSearchCells.size() &&
					probes < BUILD_PROBES_PER_FRAME && expansions < BUILD_EXPANSIONS_PER_FRAME) {
				const ICoord2D cell = m_buildSearchCells[m_buildSearchNext++];
				++expansions;
				const Int dx = cell.x - origin.x;
				const Int dy = cell.y - origin.y;
				for (Int n = 0; n < 8; ++n) {
					const Int nx = dx + NEIGHBOURS[n][0];
					const Int ny = dy + NEIGHBOURS[n][1];
					if (abs(nx) > searchRadius || abs(ny) > searchRadius) continue;
					UnsignedByte &seen = m_buildSearchSeen[(ny + searchRadius)*searchWidth + nx + searchRadius];
					if (seen) continue;
					seen = 1;
					Coord3D neighbourPos = pos;
					neighbourPos.x += nx*PATHFIND_CELL_SIZE_F;
					neighbourPos.y += ny*PATHFIND_CELL_SIZE_F;
					if (isBuildSearchWalkable(origin.x + nx, origin.y + ny, &neighbourPos)) {
						ICoord2D next;
						next.x = origin.x + nx;
						next.y = origin.y + ny;
						m_buildSearchCells.push_back(next);
					}
				}
				if ((dx == 0 && dy == 0) || dx % probeStride != 0 || dy % probeStride != 0) continue;
				if (TheAI->pathfinder()->getCell(LAYER_GROUND, cell.x, cell.y)->getType() != PathfindCell::CELL_CLEAR) continue;
				newPos.x = pos.x + dx*PATHFIND_CELL_SIZE_F;
				newPos.y = pos.y + dy*PATHFIND_CELL_SIZE_F;
				if (home && sqr(newPos.x - m_baseCenter.x) + sqr(newPos.y - m_baseCenter.y) > sqr(BASE_EDGE_REACH*m_baseRadius)) continue;
				// a flood from a spot that is itself cut off from the base stays on that ground
				if (runsOwnAI() && !reachableFromBase(&newPos)) continue;
				++probes;
				valid = TheBuildAssistant->isLocationLegalToBuild( &newPos, bldgPlan, angle,
																						 BuildAssistant::CLEAR_PATH |
																						 BuildAssistant::TERRAIN_RESTRICTIONS |
																						 BuildAssistant::NO_OBJECT_OVERLAP,
																						 dozer, m_player ) == LBC_OK;
				if (valid && home && !isLocationSafe(&newPos, bldgPlan)) valid = false;
				if (valid) break;
			}
			const Bool searchUnfinished = !valid && m_buildSearchNext < (Int)m_buildSearchCells.size();
			if (!searchUnfinished) {
				m_buildSearchCells.clear();		// found, or every reachable cell tried: the next search starts over
			}
			if (!valid) {
				TheTerrainVisual->removeAllBibs();	// isLocationLegalToBuild adds bib feedback
				if (searchUnfinished) {
					m_buildDelay = 1;		// try again next frame; doBaseBuilding leaves any value >= 1 alone
				} else {
					giveUpOnSpot(info);
				}
				return NULL;
			}
			pos = newPos;

	}

	TheTerrainVisual->removeAllBibs();	// isLocationLegalToBuild adds bib feedback, turn it off.  jba.
	if (!TheAI->pathfinder()->clientSafeQuickDoesPathExist(dozer->getAI()->getLocomotorSet(),
		dozer->getPosition(), &pos)) {
		AsciiString bldgName = bldgPlan->getName();
		bldgName.concat(" - Dozer unable to reach building.  Teleporting.");
		TheScriptEngine->AppendDebugMessage(bldgName, false);
		dozer->setPosition(&pos);
		// R1, smooth motion: a teleport, not travel - the picture shows the new place at once.  A flag on the
		// client's drawable, which nothing in the logic reads.
		if (dozer->getDrawable() != NULL)
			dozer->getDrawable()->markMotionDiscontinuity();
	}

	Object *bldg = TheBuildAssistant->buildObjectNow( dozer, 
																						bldgPlan,
																						&pos,
																						angle,
																						m_player );



#if defined _DEBUG || defined _INTERNAL
	if (TheGlobalData->m_debugAI == AI_DEBUG_PATHS)
	{
		extern void addIcon(const Coord3D *pos, Real width, Int numFramesDuration, RGBColor color);
 		RGBColor color;
		color.blue = 0;
		color.red = 1;
		color.green = 0;
		Coord3D myPos;
		myPos = *dozer->getPosition();
		myPos.z = TheTerrainLogic->getGroundHeight( myPos.x, myPos.y ) + 0.5f;
		addIcon(&myPos, 2*PATHFIND_CELL_SIZE_F, 120, color);
		myPos = pos;
		myPos.z = TheTerrainLogic->getGroundHeight( myPos.x, myPos.y ) + 0.5f;
		addIcon(&myPos, 2*PATHFIND_CELL_SIZE_F, 120, color);	
		Real dx, dy;
		dx = dozer->getPosition()->x - pos.x;
		dy = dozer->getPosition()->y - pos.y;

		Int count = sqrt(dx*dx+dy*dy)/(PATHFIND_CELL_SIZE_F/2);
		if (count<2) count = 2;
		Int i;
		color.green = 1;
		for (i=1; i<count; i++) {
			myPos.x = dozer->getPosition()->x + (pos.x-dozer->getPosition()->x)*i/count;
			myPos.y = dozer->getPosition()->y + (pos.y-dozer->getPosition()->y)*i/count;
			myPos.z = TheTerrainLogic->getGroundHeight( myPos.x, myPos.y ) + 0.5f;
			addIcon(&myPos, PATHFIND_CELL_SIZE_F/2, 120, color);

		}
	}	
#endif

	// store the object with the build order  
	if (bldg)
	{
		ExitInterface *exitInterface = bldg->getObjectExitInterface();
		if( exitInterface )
		{
			Coord3D rallyPoint;
			Bool gotOffset = false;
			if (fabs(info->getRallyOffset()->x) > 1.0f || fabs(info->getRallyOffset()->y)>1.0f) {
				gotOffset = true;	// was a bare statement, so RallyPointOffset was never applied
			}
			if (!exitInterface->getNaturalRallyPoint(rallyPoint)) {
				rallyPoint = *info->getLocation();
			}
			if (gotOffset) {
				rallyPoint.x += info->getRallyOffset()->x;
				rallyPoint.y += info->getRallyOffset()->y;
				exitInterface->setRallyPoint(&rallyPoint);
			}
		}
		info->setObjectID( bldg->getID() );
		info->setObjectTimestamp( TheGameLogic->getFrame()+1 );	// Has to be non-zero, so add 1.
		info->setUnderConstruction(true);
		purchased( bldgPlan->getTemplateID(), cost, purchaseKindOf(bldgPlan), bldgPlan->getName() );

		if (TheGlobalData->m_debugAI) {
			AsciiString bldgName = bldgPlan->getName();
			bldgName.concat(" - Building started.");
			TheScriptEngine->AppendDebugMessage(bldgName, false);
		}
	} // bldg built
	TheTerrainVisual->removeAllBibs();	// isLocationLegalToBuild adds bib feedback, turn it off.  jba.
	return bldg;
}

// ------------------------------------------------------------------------------------------------
/** Build our base. */
// ------------------------------------------------------------------------------------------------
void AIPlayer::processBaseBuilding( void )
{
	//
	// Refresh base buildings. Scan through list, if a building is missing,
	// rebuild it, unless it's rebuild count is zero.
	//
	if (m_readyToBuildStructure)
	{

		for( BuildListInfo *info = m_player->getBuildList(); info; info = info->getNext() )
		{
			AsciiString name = info->getTemplateName();
			if (name.isEmpty()) continue;
			const ThingTemplate *bldgPlan = TheThingFactory->findTemplate( name );
			if (!bldgPlan) {																											 
				DEBUG_LOG(("*** ERROR - Build list building '%s' doesn't exist.\n", name.str()));
				continue;
			}
			// check for hole.
			if (info->getObjectID() != INVALID_ID) {
				// used to have a building.
				Object *bldg = TheGameLogic->findObjectByID( info->getObjectID() );
				if (bldg==NULL) {
					// got destroyed.
					ObjectID priorID;
					priorID = info->getObjectID();
					info->setObjectID(INVALID_ID);
					info->setObjectTimestamp(TheGameLogic->getFrame()+1);
					// Scan for a GLA hole.	KINDOF_REBUILD_HOLE
					AI_BASE_SUB_BEGIN();
					Object *obj;
					for( obj = TheGameLogic->getFirstObject(); obj; obj = obj->getNextObject() ) {
						if (!obj->isKindOf(KINDOF_REBUILD_HOLE)) continue;
						RebuildHoleBehaviorInterface *rhbi = RebuildHoleBehavior::getRebuildHoleBehaviorInterfaceFromObject( obj );
						if( rhbi ) {
							ObjectID spawnerID = rhbi->getSpawnerID();
							if (priorID == spawnerID) {
								DEBUG_LOG(("AI Found hole to rebuild %s\n", bldgPlan->getName().str()));
								info->setObjectID(obj->getID());
							}
						}
 					}
					AI_BASE_SUB_END( BASE_SUB_HOLE );
				}	else {
					if (bldg->getControllingPlayer() == m_player) {
						// Check for built or dozer missing.
						if( bldg->getStatusBits().test( OBJECT_STATUS_UNDER_CONSTRUCTION ) )
						{
							AI_BASE_SUB_BEGIN();
							// make sure dozer is working on him.
							ObjectID builder = bldg->getBuilderID();
							Object* myDozer = TheGameLogic->findObjectByID(builder);
							if (myDozer==NULL) {
								DEBUG_LOG(("AI's Dozer got killed.  Find another dozer.\n"));
 								myDozer = findDozer(bldg->getPosition());
								if (myDozer==NULL || myDozer->getAI()==NULL) {
									AI_BASE_SUB_END( BASE_SUB_DOZERFIX );
									continue;
								}
								myDozer->getAI()->aiResumeConstruction(bldg, CMD_FROM_AI);
							}	else {
								// make sure he is building.
								myDozer->getAI()->aiResumeConstruction(bldg, CMD_FROM_AI);
							}
							AI_BASE_SUB_END( BASE_SUB_DOZERFIX );
						}
					} else {
						// oops, got captured.
						info->setObjectID(INVALID_ID);
						info->setObjectTimestamp(TheGameLogic->getFrame()+1);
					}	
				}
			}
			if (info->getObjectID()==INVALID_ID && info->getObjectTimestamp()>0) {
				// this object was built at some time, and got destroyed at or near objectTimestamp.
				// Wait a few seconds before initiating a rebuild.
				if (info->getObjectTimestamp()+TheAI->getAiData()->m_rebuildDelaySeconds*LOGICFRAMES_PER_SECOND > TheGameLogic->getFrame()) {
					continue;
				}	else {
					DEBUG_LOG(("Enabling rebuild for %s\n", info->getTemplateName().str()));
					info->setObjectTimestamp(0); // ready to build.
				}
			}
			// check if this building has any "rebuilds" left
			if (info->isBuildable())
			{
				Object *bldg = TheGameLogic->findObjectByID( info->getObjectID() );

				if (bldg == NULL)
				{


#ifdef USE_DOZER
					// dozer-construct the building
					AI_BASE_SUB_BEGIN();
					bldg = buildStructureWithDozer(bldgPlan, info);
					AI_BASE_SUB_END( BASE_SUB_BUILD );
					// store the object with the build order
					if (bldg)
					{
						info->setObjectID( bldg->getID() );
						info->decrementNumRebuilds();

						m_readyToBuildStructure = false;
						m_structureTimer = computeStructureDelay();
						m_frameLastBuildingBuilt = TheGameLogic->getFrame();
						// only build one building per delay loop
						break;
					} // bldg built

#else
					// force delay between rebuilds
					if (TheGameLogic->getFrame() - m_frameLastBuildingBuilt < framesToBuild) 
					{
						m_buildDelay = framesToBuild - (TheGameLogic->getFrame() - m_frameLastBuildingBuilt); 
						return;
					}	else {
						// building is missing, (re)build it
						// deduct money to build, if we have it
						Int cost = bldgPlan->calcCostToBuild( m_player );
						if (m_player->getMoney()->countMoney() >= cost)
						{
							// we have the money, deduct it
							m_player->getMoney()->withdraw( cost );

							// inst-construct the building
							bldg = buildStructureNow(bldgPlan, info, NULL);
							// store the object with the build order
							if (bldg)
							{
								info->setObjectID( bldg->getID() );
								info->decrementNumRebuilds();

								m_readyToBuildStructure = false;
								m_structureTimer = computeStructureDelay();
								m_frameLastBuildingBuilt = TheGameLogic->getFrame();
								// only build one building per delay loop
								break;
							} // bldg built
						} // have money
					} // rebuild delay ok
#endif
				} // building missing
			} // is buildable
		}
	}
}

//-------------------------------------------------------------------------------------------------
/** A team is about to be destroyed */
//-------------------------------------------------------------------------------------------------
void AIPlayer::aiPreTeamDestroy( const Team *deletedTeam )
{
	{
		for (DLINK_ITERATOR<TeamInQueue> iter = iterate_TeamBuildQueue(); !iter.done(); iter.advance())
		{
			TeamInQueue *team = iter.cur();
			if (team->m_team == deletedTeam) {
				// The members of the team all got killed before we could finish building the team.
				removeFrom_TeamBuildQueue(team);
				team->deleteInstance();
				iter = iterate_TeamBuildQueue();
			}
		}
	}
	{
		for ( DLINK_ITERATOR<TeamInQueue> iter = iterate_TeamReadyQueue(); !iter.done(); iter.advance())
		{
			TeamInQueue *team = iter.cur();
			if (team->m_team == deletedTeam) {
				// The members of the team all got killed before we could activate the team.
				removeFrom_TeamReadyQueue(team);
				team->deleteInstance();
				iter = iterate_TeamReadyQueue();
			}
		}
	}
}

//-------------------------------------------------------------------------------------------------
/** Guard supply center */
//-------------------------------------------------------------------------------------------------
void AIPlayer::guardSupplyCenter( Team *team, Int minSupplies )
{
	m_supplySourceAttackCheckFrame = 0; // force check.
	Object *warehouse = NULL;
	if (isSupplySourceAttacked()) {
		warehouse = TheGameLogic->findObjectByID(m_attackedSupplyCenter);
	}
	if (warehouse==NULL) {
		warehouse = findSupplyCenter(minSupplies);
	}
	if (warehouse) {

		AIGroup* theGroup = TheAI->createGroup();
		if (!theGroup) {
			return;
		}
		team->getTeamAsAIGroup(theGroup);
		Coord3D location = *warehouse->getPosition();
		// It's probably a defensive move - position towards the enemy.
		Region2D bounds;
		// getSkirmishEnemyPlayer() can be NULL (no resolvable human enemy, e.g. an all-AI game);
		// every caller in ScriptActions checks it, these two did not.
		Player *skirmishEnemy = TheScriptEngine->getSkirmishEnemyPlayer();
		Coord3D offset;
		offset.zero();
		if (skirmishEnemy)
		{
			getPlayerStructureBounds(&bounds, skirmishEnemy->getPlayerIndex(), FALSE, m_player->getPlayerIndex());
			offset.x = location.x - (bounds.lo.x+bounds.hi.x)*0.5f;
			offset.y = location.y - (bounds.lo.y+bounds.hi.y)*0.5f;
			offset.normalize();
		}
		Real radius = warehouse->getGeometryInfo().getBoundingCircleRadius()*0.8f;

		location.x -= offset.x*radius;
		location.y -= offset.y*radius;
		theGroup->groupGuardPosition( &location, GUARDMODE_NORMAL, CMD_FROM_SCRIPT );

	}
}

//-------------------------------------------------------------------------------------------------
/** Is a supply source attacked? */
//-------------------------------------------------------------------------------------------------
Bool AIPlayer::isSupplySourceAttacked( void )
{
	// don't scan more often than every 10 seconds. This was a bare 10 used as a FRAME count, so
	// the throttle was a third of a second and, worse, the "have I been attacked recently" test
	// below only saw damage that landed in the last 10 frames.
	const Int SCAN_RATE = 10 * LOGICFRAMES_PER_SECOND;
	UnsignedInt curFrame = TheGameLogic->getFrame();
	if (curFrame==0) {
		m_supplySourceAttackCheckFrame = curFrame+SCAN_RATE;
		return false; // can't be attacked on first frame.
	}
	m_attackedSupplyCenter = INVALID_ID;
	if (curFrame < m_supplySourceAttackCheckFrame) {
		return false;
	}
	if (m_player->getAttackedFrame()+SCAN_RATE < curFrame) {
		return false; // haven't been attacked recently.
	}
	m_supplySourceAttackCheckFrame = curFrame+SCAN_RATE;

	// Scan my units.
	Player::PlayerTeamList::const_iterator it;
	for (it = m_player->getPlayerTeams()->begin(); it != m_player->getPlayerTeams()->end(); ++it) {
		for (DLINK_ITERATOR<Team> iter = (*it)->iterate_TeamInstanceList(); !iter.done(); iter.advance()) {
			Team *team = iter.cur();
			if (!team) {
				continue;
			}			
			for (DLINK_ITERATOR<Object> objIter = team->iterate_TeamMemberList(); !objIter.done(); objIter.advance()) {
				Object *obj = objIter.cur();
				if (!obj) {
					continue;
				}
				if (!obj->isKindOf(KINDOF_CASH_GENERATOR) && !obj->isKindOf(KINDOF_DOZER) &&
					!obj->isKindOf(KINDOF_HARVESTER)) {
					continue;
				}
				// check for attacked.
				BodyModuleInterface *body = obj->getBodyModule();
				if (body) {
					const DamageInfo *info = body->getLastDamageInfo();
					if (info) {
						if (info->out.m_noEffect) {
							continue;
						}
						if (body->getLastDamageTimestamp() + SCAN_RATE > curFrame) {
							// winner.
							m_attackedSupplyCenter = obj->getID();
							return true;
						}
					}
				}
			}
		}
	}
	return false;
}

//-------------------------------------------------------------------------------------------------
/** Is the nearest supply source safe? */
//-------------------------------------------------------------------------------------------------
Bool AIPlayer::isSupplySourceSafe( Int minSupplies )
{
	Object *warehouse = findSupplyCenter(minSupplies);
	if (warehouse==NULL) return true; // it's safe cause it doesn't exist.
	return (isLocationSafe(warehouse->getPosition(), warehouse->getTemplate()));
}

//-------------------------------------------------------------------------------------------------
/** Is this location safe for building this thing? */
//-------------------------------------------------------------------------------------------------
Bool AIPlayer::isLocationSafe(const Coord3D *pos, const ThingTemplate *tthing )
{
	if (tthing == NULL) return 0;

	// See if we have enemies.
	Real radius = TheAI->getAiData()->m_supplyCenterSafeRadius;
	radius += tthing->getTemplateGeometryInfo().getBoundingCircleRadius();

	// only consider enemies.
	PartitionFilterPlayerAffiliation	filterTeam(m_player, (ALLOW_ALLIES|ALLOW_NEUTRAL), false);

	// and only stuff that is not dead
	PartitionFilterAlive filterAlive;

	// and only stuff that isn't stealthed (and not detected)
	// (note that stealthed allies aren't hidden from us, but we're only looking for enemies here)
	PartitionFilterRejectByObjectStatus filterStealth( MAKE_OBJECT_STATUS_MASK( OBJECT_STATUS_STEALTHED ), 
																										 MAKE_OBJECT_STATUS_MASK2( OBJECT_STATUS_DETECTED, OBJECT_STATUS_DISGUISED ) );

	// (optional) only stuff that is significant
	PartitionFilterInsignificantBuildings filterInsignificant(true, false);

	PartitionFilterRejectByKindOf	filterHarvesters(MAKE_KINDOF_MASK(KINDOF_HARVESTER), KINDOFMASK_NONE);

	PartitionFilterRejectByKindOf	filterDozer(MAKE_KINDOF_MASK(KINDOF_DOZER), KINDOFMASK_NONE);

	PartitionFilter *filters[16];
	Int numFilters = 0;

	filters[numFilters++] = &filterTeam;
	filters[numFilters++] = &filterAlive;
	filters[numFilters++] = &filterStealth;

	filters[numFilters++] = &filterInsignificant;
	filters[numFilters++] = &filterHarvesters;
	filters[numFilters++] = &filterDozer;
	filters[numFilters] = NULL;

	Object *enemy = ThePartitionManager->getClosestObject(  pos, radius, FROM_BOUNDINGSPHERE_2D, filters );
	if (enemy!=NULL) {
		return false;
	}
	return true;

}  // isSupplySourceSafe


// ------------------------------------------------------------------------------------------------
/** Invoked when a unit I am training comes into existence */
// ------------------------------------------------------------------------------------------------
void AIPlayer::onUnitProduced( Object *factory, Object *unit )
{
	Bool found = false;
	// this was uninitialised and is only assigned for units that actually have a SupplyTruck AI
	// (USA/China dozers do not), so stack garbage decided whether m_repairDozer got bound - and
	// that made an AI game non-deterministic.
	Bool supplyTruck = false;

	// factory could be NULL at the start of the game.
	if (factory == NULL) {
		return;
	}

	// a barracks or war factory that let a unit out was busy on this spend pass, whatever its queue
	// reads when the pass looks (doSpending, which empties the list and runs only with the purchase queue)
	if( purchaseQueueOn() && ( factory->isKindOf( KINDOF_FS_WARFACTORY ) || factory->isKindOf( KINDOF_FS_BARRACKS ) ) &&
			std::find( m_producedSincePass.begin(), m_producedSincePass.end(), factory->getID() ) == m_producedSincePass.end() )
		m_producedSincePass.push_back( factory->getID() );

	for (DLINK_ITERATOR<TeamInQueue> iter = iterate_TeamBuildQueue(); !iter.done(); iter.advance())
	{
		TeamInQueue *team = iter.cur();
		// find work order entry and delete it
		WorkOrder *order;
		if (found) break;
		for( order = team->m_workOrders; order; order = order->m_next )
		{
			if (order->m_factoryID == factory->getID() && order->m_numCompleted < order->m_numRequired && unit->getTemplate()->isEquivalentTo(order->m_thing))
			{
				// found associated order, mark it complete.
				order->m_numCompleted++;
				// put new unit into the team under construction
				if (team->m_team)
					unit->setTeam( team->m_team );
				sendForSpotters( team, unit );
				if (team->m_reinforcement) {
					team->m_reinforcementID = unit->getID();
				}
				AIUpdateInterface *ai = unit->getAIUpdateInterface();
				if (team->m_team->getPrototype()->getTemplateInfo()->m_hasHomeLocation) {
					if (ai) {
						std::vector<Coord3D> path;
						path.push_back( *ai->getGoalPosition() );
						path.push_back(team->m_team->getPrototype()->getTemplateInfo()->m_homeLocation);
						ai->aiFollowExitProductionPath(&path, NULL, CMD_FROM_AI);
					}
				}

				order->m_factoryID = INVALID_ID; // no longer using this factory.
				// a support order the fork's AI put in for a job of its own; EA's AI never has one, and
				// a unit that came out of anything else must not land in those slots
				const Bool dutyOrder = order->m_isScout && runsOwnAI();
				if (ai) {
					// tell it to start gathering resources.
					// Here is the special bit for this exit style, force wanting on SupplyTruck types
					SupplyTruckAIInterface* supplyTruckAI = ai->getSupplyTruckAIInterface();
					if( supplyTruckAI )	{
						if (order->m_isResourceGatherer) {
							supplyTruck = true;
						} else {
							supplyTruck = false;
						}
						supplyTruckAI->setForceWantingState(supplyTruck);
						if (supplyTruck) {
							// assign to a supply depot.
							for( BuildListInfo *info = m_player->getBuildList(); info; info = info->getNext() )
							{
								if (info->isSupplyBuilding() && info->getDesiredGatherers()>0 &&
									info->getDesiredGatherers()>info->getCurrentGatherers()) {
										Object *obj = TheGameLogic->findObjectByID(info->getObjectID());
										if (obj) {
											info->setCurrentGatherers(info->getCurrentGatherers()+1);
											// Note - although this is the ai, we are sending in CMD_FROM_PLAYER.
											// This causes the dock object to stick in the docking interface.
											// The supply truck ai issues dock commands, and they become confused.
											// Thus, player.  jba.  ;(
											ai->aiDock(obj, CMD_FROM_PLAYER);
											break;	// one truck, one center: it was counted at every center that wanted one
										}
									}
							}

						}
						// a Chinook buyGunshipChinook ordered (a support-unit order, m_isScout): the supply
						// center's exit put it to gathering, which the line above has just switched off, and
						// it stays on our books as a gunship.  A script team's transport Chinook is not one
						else if (dutyOrder && unit->isKindOf(KINDOF_AIRCRAFT) && unit->isKindOf(KINDOF_HARVESTER) &&
								passengersFireFrom(unit->getTemplate())) {
							for (Int chinook = 0; chinook < MAX_GUNSHIP_CHINOOKS; ++chinook) {
								if (m_gunshipChinook[chinook] == INVALID_ID) {
									m_gunshipChinook[chinook] = unit->getID();
									DEBUG_LOG(("AI GUNSHIP frame %d player %d '%s' %d comes out as a gunship\n", TheGameLogic->getFrame(),
										m_player->getPlayerIndex(), unit->getTemplate()->getName().str(), unit->getID()));
									break;
								}
							}
						}
						// ... and a plain one buyTransportChinook ordered, which flies the wave's units
						else if (dutyOrder && unit->isKindOf(KINDOF_AIRCRAFT) && unit->isKindOf(KINDOF_HARVESTER) &&
								transportContainOf(unit->getTemplate())) {
							for (Int shuttle = 0; shuttle < MAX_TRANSPORT_CHINOOKS; ++shuttle) {
								if (m_shuttle[shuttle].id == INVALID_ID) {
									m_shuttle[shuttle].id = unit->getID();
									m_shuttle[shuttle].phase = SHUTTLE_HOME;
									m_shuttle[shuttle].frame = TheGameLogic->getFrame();
									m_shuttle[shuttle].load = 0;
									m_shuttle[shuttle].home = *unit->getPosition();
									DEBUG_LOG(("AI SHUTTLE frame %d player %d '%s' %d comes out as a transport\n", TheGameLogic->getFrame(),
										m_player->getPlayerIndex(), unit->getTemplate()->getName().str(), unit->getID()));
									break;
								}
							}
						}
					}
				}
				// a Helix buyDutyHelix ordered; a team's Helix comes out of a team order, not a support one
				if (dutyOrder && isHelicopter(unit))
					takeDutyHelix(unit);
				// ... and an Ambulance doAmbulances ordered
				if (dutyOrder && isAmbulanceTemplate(unit->getTemplate())) {
					for (Int slot = 0; slot < MAX_DUTY_AMBULANCES; ++slot) {
						if (m_ambulance[slot].id == INVALID_ID) {
							m_ambulance[slot].id = unit->getID();
							m_ambulance[slot].spot = *unit->getPosition();
							m_ambulance[slot].seen = *unit->getPosition();
							m_ambulance[slot].seenHealth = 1.0f;
							m_ambulance[slot].seenReach = 0.0f;
							m_ambulance[slot].pullbacks = 0;
							m_ambulance[slot].holds = 0;
							++m_ambulancesBought;
							DEBUG_LOG(("AI AMBULANCE frame %d player %d '%s' %d comes out, %d bought this match\n", TheGameLogic->getFrame(),
								m_player->getPlayerIndex(), unit->getTemplate()->getName().str(), unit->getID(), m_ambulancesBought));
							break;
						}
					}
				}
				found = true;
				break;
			}
		}
	}
	if (!supplyTruck && unit->isKindOf(KINDOF_DOZER)) {
		if (m_dozerQueuedForRepair) {
			m_repairDozer = unit->getID();
			m_dozerQueuedForRepair =false;
		} else {
			m_buildDelay = 0;
			m_structureTimer = 1;
		}
	}
	if (!found) {
		DEBUG_LOG(("***AI PLAYER-Unit not found in production queue.\n"));
	}

	m_teamDelay = 0; // Cause the update queues & selection to happen immediately.
}

/** How long a spot stays spoken for after a superweapon has been aimed at it.
	*
	* It has to outlast a recharge or it buys nothing: the first cut was a minute, and the spy
	* satellite - which goes through this same script action - recharges in 1801 frames and found
	* its memory expired by one frame every single time, so it scanned the identical cell of the
	* identical base seventeen times in a match.  One recharge is not enough either: a Particle
	* Uplink Cannon comes back in 8400 frames and at a window of 9000 the crater has recovered 93%
	* of its worth, which moves nothing - measured, three shots into the same spot.  Two recharges
	* is the number, so the shot after this one has to find somewhere half as good before it will
	* come back here.
	*
	* Coming back is deliberate.  The same three shots were at a cluster he kept rebuilding, worth
	* 6859, then 7300, then 8900 - a target that grows back past the fade is one worth hitting
	* again, and the fade recovering the whole way is what allows it. */
enum { SUPERWEAPON_REAIM_FRAMES = 600 * LOGICFRAMES_PER_SECOND };

/** What a strike's blast holds, by plain price, for the AI SUPERWEAPON line only: buildings against
	* units, and the dearest building. */
struct StrikeSummary
{
	Int structures;
	Int units;
	const Object *top;
};

static void summarizeStrike( const Coord3D *at, Int playerNdx, Real radius, Int observerNdx, StrikeSummary *summary )
{
	summary->structures = 0;
	summary->units = 0;
	summary->top = NULL;
	Int topCost = 0;
	Player *enemy = ThePlayerList->getNthPlayer( playerNdx );
	Player::PlayerTeamList::const_iterator teamIt;
	for( teamIt = enemy->getPlayerTeams()->begin(); teamIt != enemy->getPlayerTeams()->end(); ++teamIt )
	{
		for( DLINK_ITERATOR<Team> instanceIt = (*teamIt)->iterate_TeamInstanceList(); !instanceIt.done(); instanceIt.advance() )
		{
			for( DLINK_ITERATOR<Object> memberIt = instanceIt.cur()->iterate_TeamMemberList(); !memberIt.done(); memberIt.advance() )
			{
				const Object *obj = memberIt.cur();
				const Real dx = at->x - obj->getPosition()->x;
				const Real dy = at->y - obj->getPosition()->y;
				if( dx*dx + dy*dy >= radius*radius || obj->isEffectivelyDead() || !observerKnowsAbout( obj, observerNdx ) )
					continue;
				const Int cost = obj->getTemplate()->calcCostToBuild( enemy );
				if( !obj->isKindOf( KINDOF_STRUCTURE ) )
				{
					summary->units += cost;
					continue;
				}
				summary->structures += cost;
				if( cost > topCost )
				{
					topCost = cost;
					summary->top = obj;
				}
			}
		}
	}
}

//----------------------------------------------------------------------------------------------------------
/** What a superweapon aimed at this spot is worth to this player right now.
 *
 * The score itself is EA's - the price of everything inside the blast, with the rung's threat and
 * economy weights on top (getPlayerSuperweaponValue) - and the fade on top of it
 * is what stops the salvo. A negative score belongs to a sneak attack, which reads defended ground
 * as a cost rather than a prize; fading that would flatter exactly the ground it is avoiding.
 * A Challenge or mission opponent gets neither weight nor fade: it aims as retail did.
 */
//----------------------------------------------------------------------------------------------------------
Int AIPlayer::superweaponScore( Coord3D *center, Int playerNdx, Real radius, Bool targetMilitaryUnits )
{
	const Int value = getPlayerSuperweaponValue( center, playerNdx, radius, targetMilitaryUnits, knowledgeObserver(),
																							 strikeEconomyWeight(), strikeThreatWeight() );
	if( value <= 0 || !runsOwnAI() )
		return value;

	const UnsignedInt now = TheGameLogic->getFrame();
	Real worth = 1.0f;
	for( Int strike = 0; strike < MAX_REMEMBERED_STRIKES; ++strike )
	{
		if( m_strikeFrame[ strike ] == 0 )
			continue;

		const Real dx = center->x - m_strikeAim[ strike ].x;
		const Real dy = center->y - m_strikeAim[ strike ].y;
		const Real fade = aiStrikeFade( dx*dx + dy*dy, sqr( radius ), now - m_strikeFrame[ strike ],
																		SUPERWEAPON_REAIM_FRAMES );
		if( fade < worth )
			worth = fade;			// the freshest shot covering this spot decides
	}

	return REAL_TO_INT( value * worth );
}

//----------------------------------------------------------------------------------------------------------
/** A Challenge or mission opponent aims as the map script was written for: price alone. */
//----------------------------------------------------------------------------------------------------------
Real AIPlayer::strikeEconomyWeight( void )
{
	return runsOwnAI() ? getSkillProfile()->m_strikeEconomyWeight : 0.0f;
}

Real AIPlayer::strikeThreatWeight( void )
{
	return runsOwnAI() ? getSkillProfile()->m_strikeThreatWeight : 0.0f;
}

//----------------------------------------------------------------------------------------------------------
/** Remember a spot the next shot should leave alone.
 *
 * ponytail: recorded where the target is picked rather than where the shot leaves the silo - the
 * one caller fires immediately after a successful compute, and nothing else in the tree aims one.
 */
//----------------------------------------------------------------------------------------------------------
void AIPlayer::noteSuperweaponAim( const Coord3D *pos )
{
	m_strikeAim[ m_strikeNext ] = *pos;
	m_strikeFrame[ m_strikeNext ] = TheGameLogic->getFrame();
	m_strikeNext = (m_strikeNext + 1) % MAX_REMEMBERED_STRIKES;
}

//----------------------------------------------------------------------------------------------------------
/**
 * Find a good spot to fire a superweapon.
 */
Bool AIPlayer::computeSuperweaponTarget(const SpecialPowerTemplate *power, Coord3D *retPos, Int playerNdx, Real weaponRadius)
{
	// a Tech AI turns its powers on whoever is hitting an ally (the team board)
	const AITeamRequest *call = m_persona == AIPERSONA_TECH ? freshRequest( AIREQ_HELP, AI_COOP_HELP_FRAMES ) : NULL;
	if( call && call->enemy >= 0 && call->enemy != playerNdx )
	{
		DEBUG_LOG(("AI COOP frame %d player %d turns '%s' on player %d, who hits player %d\n", TheGameLogic->getFrame(),
			m_player->getPlayerIndex(), power->getName().str(), call->enemy, call->from));
		playerNdx = call->enemy;
	}

  Bool success = FALSE;

	Region2D bounds;
	getPlayerStructureBounds(&bounds, playerNdx, FALSE, m_player->getPlayerIndex());

	if( bounds.hi.x == 0 
		&& bounds.lo.x == 0 
		&& bounds.hi.y == 0 
		&& bounds.lo.y == 0 
		)
	{
		Region3D bounds3D;
		// Degenerate bounds because he has no buildings.  Don't give up, scan the whole map for the leftovers.
		TheTerrainLogic->getExtent(&bounds3D);
		bounds.hi.x = bounds3D.hi.x;
		bounds.lo.x = bounds3D.lo.x;
		bounds.hi.y = bounds3D.hi.y;
		bounds.lo.y = bounds3D.lo.y;
	}

	if (weaponRadius<1.0f) {
		weaponRadius = 1.0f; // sanity to avoid divide by 0.
	}

	Int xCount, yCount;
	bounds.lo.x += weaponRadius;
	bounds.hi.x -= weaponRadius;
	if (bounds.hi.x<bounds.lo.x) {
		bounds.hi.x = bounds.lo.x = (bounds.hi.x+bounds.lo.x)/2.0f;
	}
	if (bounds.hi.y<bounds.lo.y) {
		bounds.hi.y = bounds.lo.y = (bounds.hi.y+bounds.lo.y)/2.0f;
	}
	
	xCount = REAL_TO_INT_CEIL(bounds.width()/weaponRadius)+1;
	yCount = REAL_TO_INT_CEIL(bounds.height()/weaponRadius)+1;

	if (xCount>10) xCount = 10;
	if (yCount>10) yCount = 10;

	Int cash = -1;
	Coord3D pos;
	Coord3D bestPos;
	Int x, y, xDelta, yDelta, xIndex, yIndex, xStart, yStart;

	Bool targetMilitaryUnits = TRUE;
	if( power->getSpecialPowerType() == SPECIAL_SNEAK_ATTACK )
	{
		//Military units will have a negative effect on where to drop the special power.
		//We want to target rich areas that are poorly defended or undefended.
		targetMilitaryUnits = FALSE;
	}

	//Randomize which way we iterate the grid. We don't always want to start in the bottom left corner incase
	//of a bad calculation, it'll would always end up there.
	switch( GameLogicRandomValue( 1, 4 ) )
	{
		case 1:
			//x min to max, y min to max
			xDelta = 1,	yDelta = 1;
			xStart = 0,	yStart = 0;
			break;
		case 2:
			//x max to min, y min to max
			xDelta = -1, yDelta = 1;
			xStart = xCount, yStart = 0;
			break;
		case 3:
			//x min to max, y max to min
			xDelta = 1, yDelta = -1;
			xStart = 0, yStart = yCount;
			break;
		case 4:
		default:
			//x max to min, y max to min
			xDelta = -1, yDelta = -1;
			xStart = xCount, yStart = yCount;
			break;
	}

	//Calculate the generally best position
	xIndex = xStart;
	for( x = 0; x < xCount; x++, xIndex += xDelta ) 
	{
		yIndex = yStart;
		for( y = 0; y < yCount; y++, yIndex += yDelta ) 
		{
			pos.x = bounds.lo.x + ( bounds.width() * xIndex ) / xCount;
			pos.y = bounds.lo.y + ( bounds.height() * yIndex ) / yCount;
			pos.z = 0;
			Int curCash = superweaponScore( &pos, playerNdx, 2*weaponRadius, targetMilitaryUnits );
			if ( curCash > cash)
			{
				cash = curCash;
				bestPos = pos;
			}
		}
	}

	//
	// An army is a threat point wherever it happens to be standing, and the grid above only covers
	// the ground his buildings are on: getPlayerStructureBounds measures structures, so the wave
	// parked on our own doorstep was never a candidate at all - the AI could only ever shoot at
	// real estate. Offer what we can see of his army as centres too and let the same score choose.
	// A rung that puts no threat weight on his army (Hard) looks there only when the grid found
	// nothing of his worth a shot; a Challenge or mission opponent never, as in retail.
	// ponytail: a value pass per armed unit, which is only paid on a frame a superweapon is ready.
	//
	Player *enemy = ThePlayerList->getNthPlayer( playerNdx );
	if( targetMilitaryUnits && enemy != NULL && runsOwnAI() && ( strikeThreatWeight() > 0.0f || cash <= 0 ) )
	{
		const Int observerNdx = m_player->getPlayerIndex();
		Player::PlayerTeamList::const_iterator teamIt;
		for( teamIt = enemy->getPlayerTeams()->begin(); teamIt != enemy->getPlayerTeams()->end(); ++teamIt )
		{
			for( DLINK_ITERATOR<Team> instanceIt = (*teamIt)->iterate_TeamInstanceList(); !instanceIt.done(); instanceIt.advance() )
			{
				Team *team = instanceIt.cur();
				if( !team )
					continue;
				for( DLINK_ITERATOR<Object> memberIt = team->iterate_TeamMemberList(); !memberIt.done(); memberIt.advance() )
				{
					Object *pObj = memberIt.cur();
					if( !pObj || pObj->isKindOf( KINDOF_STRUCTURE ) )
						continue;			// his buildings are what the grid above already walked
					if( !pObj->isKindOf( KINDOF_CAN_ATTACK ) )
						continue;			// a harvester is not a threat point; it is already worth its price in the grid
					if( pObj->isSignificantlyAboveTerrain() )
						continue;			// same rule as the value function: nothing is aimed at a plane in the air
					if( !observerKnowsAbout( pObj, observerNdx ) )
						continue;

					Coord3D pos = *pObj->getPosition();
					pos.z = 0;
					Int curCash = superweaponScore( &pos, playerNdx, 2*weaponRadius, targetMilitaryUnits );
					if( curCash > cash )
					{
						cash = curCash;
						bestPos = pos;
					}
				}
			}
		}
	}

	//Fine tune that position by looking at a even smaller radius.
	Coord3D veryBestPos;
	xCount = 11;
	yCount = 11;
	cash = -1;
	Int count = 0;
	for( x = 0; x < xCount; x++ ) 
	{
		for( y = 0; y < yCount; y++ ) 
		{
			pos.x = bestPos.x + (x-5)*(weaponRadius/10);
			pos.y = bestPos.y + (y-5)*(weaponRadius/10);	// was (x-5): only the diagonal was scanned
			pos.z = 0;
			Int curCash = superweaponScore( &pos, playerNdx, weaponRadius, targetMilitaryUnits );
			if ( curCash > cash) 
			{
				cash = curCash;
				veryBestPos = pos;
				count = 1;
			}	
			else if (curCash==cash) 
			{
				veryBestPos.x += pos.x;
				veryBestPos.y += pos.y;
				count++;
			}
		}
	}
	if (count>1) {
		veryBestPos.x /= count;
		veryBestPos.y /= count;
	}
	veryBestPos.z = TheTerrainLogic->getGroundHeight(veryBestPos.x, veryBestPos.y);
	*retPos = veryBestPos;

  success = ( cash > -1 );

	if( success )
	{
		StrikeSummary summary;
		summarizeStrike( &veryBestPos, playerNdx, weaponRadius, knowledgeObserver(), &summary );
		DEBUG_LOG(("AI SUPERWEAPON frame %d player %d skill %d aims '%s' at (%.0f,%.0f), worth %d: structures %d units %d, top '%s'\n",
			TheGameLogic->getFrame(), m_player->getPlayerIndex(), (Int)m_skillLevel, power->getName().str(), veryBestPos.x,
			veryBestPos.y, cash, summary.structures, summary.units, summary.top ? summary.top->getTemplate()->getName().str() : "none"));
		noteSuperweaponAim( &veryBestPos );
	}


  return success;


}

/** The least a strategic target counts for before the rung's weight: a captured derrick's build price
	* is next to nothing and it pays for the whole match.
	* ponytail: one flat floor for every kind; a per-kind income estimate if derricks still go unhit. */
const Real SUPERWEAPON_ECONOMY_FLOOR = 1000.0f;

/** What builds his army, powers his base, pays for it or opens his tech: a strike there is felt for
	* the rest of the match, where the army it could have hit is rebuilt in a minute. */
static Bool isStrategicTarget( const Object *obj )
{
	return obj->isKindOf( KINDOF_CASH_GENERATOR ) || obj->isKindOf( KINDOF_FS_SUPPLY_CENTER ) ||
				 obj->isKindOf( KINDOF_FS_BLACK_MARKET ) || obj->isKindOf( KINDOF_MONEY_HACKER ) ||
				 obj->isKindOf( KINDOF_FS_INTERNET_CENTER ) || obj->isKindOf( KINDOF_FS_SUPPLY_DROPZONE ) ||
				 obj->isKindOf( KINDOF_FS_STRATEGY_CENTER ) || obj->isKindOf( KINDOF_FS_ADVANCED_TECH ) ||
				 obj->isKindOf( KINDOF_FS_FACTORY ) || obj->isKindOf( KINDOF_FS_BARRACKS ) ||
				 obj->isKindOf( KINDOF_FS_WARFACTORY ) || obj->isKindOf( KINDOF_FS_AIRFIELD ) ||
				 obj->isKindOf( KINDOF_FS_POWER ) || obj->isKindOf( KINDOF_FS_SUPERWEAPON );
}

//----------------------------------------------------------------------------------------------------------
/**
 * Get the target value for structures in an area.  economyWeight is what a strategic target
 * (isStrategicTarget) adds on top of its price, in prices; threatWeight what anything armed adds,
 * in its combat power.  Both 0 is EA's score.
 */
Int AIPlayer::getPlayerSuperweaponValue(Coord3D *center, Int playerNdx, Real radius, Bool includeMilitaryUnits, Int observerNdx,
																				Real economyWeight, Real threatWeight )
{
	if (radius < 4*PATHFIND_CELL_SIZE_F) 
	{
		radius = 4*PATHFIND_CELL_SIZE_F;
	}
	Player::PlayerTeamList::const_iterator it;
	Real cash = 0;
	Real radSqr = sqr(radius);

	Player* pPlayer = ThePlayerList->getNthPlayer(playerNdx);
	if (pPlayer == NULL) 
		return 0;
	for (it = pPlayer->getPlayerTeams()->begin(); it != pPlayer->getPlayerTeams()->end(); ++it) 
	{
		for (DLINK_ITERATOR<Team> iter = (*it)->iterate_TeamInstanceList(); !iter.done(); iter.advance()) 
		{
			Team *team = iter.cur();
			if (!team) continue;
			for (DLINK_ITERATOR<Object> iter = team->iterate_TeamMemberList(); !iter.done(); iter.advance()) 
			{
				Object *pObj = iter.cur();
				if (!pObj)
					continue;
				// the distance first: the beam steer asks this for 49 points around the beam, and the scouting
				// test on every object of every enemy made a steer 2.6 ms late in a match, against 0.9 now
				Coord3D pos = *pObj->getPosition();
				Real dx = center->x - pos.x;
				Real dy = center->y - pos.y;
				if (dx*dx+dy*dy >= radSqr)
					continue;
				if (pObj->isEffectivelyDead())
					continue;			// a tank in its death throes stays on the team for seconds and held a steered beam on its wreck
				if (!observerKnowsAbout(pObj, observerNdx))
					continue;			// a superweapon is aimed at what has been scouted, not at the object list

				Bool applyNegValue = FALSE;
				if( !includeMilitaryUnits )
				{
					if( pObj->isKindOf( KINDOF_FS_BASE_DEFENSE ) || pObj->isKindOf( KINDOF_TECH_BASE_DEFENSE ) )
					{
						//Hostile structure
						applyNegValue = TRUE;
					}
					else if( pObj->isKindOf( KINDOF_VEHICLE ) || pObj->isKindOf( KINDOF_INFANTRY ) )
					{
						if( !pObj->isKindOf( KINDOF_DOZER ) && !pObj->isKindOf( KINDOF_HARVESTER ) )
						{
							//Hostile unit.
							applyNegValue = TRUE;
						}
					}
				}
				else if (pObj->isKindOf(KINDOF_AIRCRAFT)) 
				{
					if (pObj->isSignificantlyAboveTerrain()) 
					{
						continue; // Don't target flying aircraft.  OK if in the airstrip.
					}
				}
				{
					Real dist = sqrt(dx*dx+dy*dy);
					Real factor = 1.0f - (dist/(2*radius)); // 1.0 in center, 0.5 on edges.
					Real value = pObj->getTemplate()->calcCostToBuild(pPlayer);
					if (pObj->isKindOf(KINDOF_COMMANDCENTER)) 
					{
						if( !includeMilitaryUnits )
							value = value * 5.0f; //Command centers are prime targets for sneak attacks.
						else
							value = value / 10; // Command centers cannot be killed by any superweapon, so we don't want to target them as highly. jba.
					}
					if (pObj->isKindOf( KINDOF_FS_SUPERWEAPON ) ) 
					{
						if( !includeMilitaryUnits )
							value = value * 5.0f; //Superweapons are prime targets for sneak attacks.
						else
							value = value / 10; // Superweapons cannot be killed by any superweapon, so we don't want to target them as highly. jba.
					}
					//
					// The rung's threat weight: what can shoot is worth this many times its combat
					// power on top of its price (aiCombatPower reads the ThreatValue where the data
					// sets it and falls back on cost). At 2 on every rung it sent nearly every strike
					// at the army, which the player rebuilds in a minute; Easy keeps that, Medium
					// halves it and Hard drops it.
					//
					if( includeMilitaryUnits && threatWeight > 0.0f )
						value += aiCombatPower( pObj ) * threatWeight;

					// What builds and pays for the army, powers the base and opens the next tier is
					// the hit felt all match. A superweapon counts at the floor rather than its price:
					// EA's note above holds, a strike damages one and seldom kills it, and its power
					// plants are what switch it off.
					if( economyWeight > 0.0f && !applyNegValue && isStrategicTarget( pObj ) )
					{
						const Real price = pObj->isKindOf( KINDOF_FS_SUPERWEAPON ) ? 0.0f : (Real)pObj->getTemplate()->calcCostToBuild( pPlayer );
						value += economyWeight * max( price, SUPERWEAPON_ECONOMY_FLOOR );
					}

					if( applyNegValue )
					{
						cash -= factor * value * 5.0f; //Extremely undesired
					}
					else
					{
						cash += factor * value;
					}
				}
			}
		}
	}
	return cash;
}
// ------------------------------------------------------------------------------------------------
/** Search the computer player's buildings for one that can build the given request 
	* and start training the unit.
	* If busyOK is true, it will queue a unit even if one is building.  This lets 
	* script invoked teams "push" to the front of the queue. */
// ------------------------------------------------------------------------------------------------
Bool AIPlayer::startTraining( WorkOrder *order, Bool busyOK, AsciiString teamName)
{
	Object *factory = findFactory(order->m_thing, busyOK);
	if( factory )
	{
		ProductionUpdateInterface *pu = factory->getProductionUpdateInterface();
		if (pu && buyUnit( pu, order->m_thing, purchaseKindOf( order->m_thing ) )) {
			order->m_factoryID = factory->getID();
			if (TheGlobalData->m_debugAI) {
				AsciiString teamStr = "Queuing ";
				teamStr.concat(order->m_thing->getName());
				teamStr.concat(" for ");
				teamStr.concat(teamName);
				TheScriptEngine->AppendDebugMessage(teamStr, false);
			}
			return true;
		}
	}  // end if

	return FALSE;

}

// ------------------------------------------------------------------------------------------------
/** Search the computer player's buildings for one that can build the given request.
	* If busyOK is true, it will return a busy factory if there are no idle ones.  This is 
	* used for script invoked teams "push" to the front of the queue. */
// ------------------------------------------------------------------------------------------------
Object *AIPlayer::findFactory(const ThingTemplate *thing, Bool busyOK) 
{
	Object *busyFactory = NULL; // We prefer a factory that isn't busy.
	for( BuildListInfo *info = m_player->getBuildList(); info; info = info->getNext() )
	{
		Object *factory = TheGameLogic->findObjectByID( info->getObjectID() );
		if( factory )
		{
			if (factory->getControllingPlayer() != m_player) {
				// captured. Stamp the timestamp too, exactly as processBaseBuilding does - without
				// it the RebuildDelay gate below is skipped and the AI keeps trying to rebuild on
				// top of the enemy-owned building, every structure cycle.
				info->setObjectID(INVALID_ID);
				info->setObjectTimestamp(TheGameLogic->getFrame()+1);
				continue;
			}
			// ignore buildings that are under construction.
			if (factory->testStatus(OBJECT_STATUS_UNDER_CONSTRUCTION))
				continue;
			// also ignore buildings that are being sold.
			if (factory->testStatus(OBJECT_STATUS_SOLD))
				continue;
			ProductionUpdateInterface *pu = factory->getProductionUpdateInterface();
			// If it doesn't produce, continue.
			if (!pu) continue;
			// if we can't create the unit do nothing
			if( TheBuildAssistant->isPossibleToMakeUnit( factory, thing ) == FALSE )
				continue;
			// If the factory is not busy, return it.
			Bool busy = pu->getProductionCount()>0;
			if (!busy) return factory; // found a not busy factory.
			if (busyOK) busyFactory = factory;
		}  // end if

	}  // end for
	// We didn't find an idle factory, so return the busy one.
	if (busyOK) return busyFactory;
	return NULL;
}

// ------------------------------------------------------------------------------------------------
/** A side built out of another side's reskins runs that side's scripts, and a unit they name with
	* no reskin for this side (America's Stealth Fighter, for Turkiye) is left out of the team rather
	* than keeping the whole team off the build list. */
// ------------------------------------------------------------------------------------------------
static Bool sideCanNeverField( const Player *player, const ThingTemplate *thing )
{
	const AsciiString &side = player->getSide();
	return thing->getDefaultOwningSide() != side && TheThingFactory->sideBorrowsTemplates( side ) &&
		TheThingFactory->findSideReskin( thing, side ) == NULL;
}

// ------------------------------------------------------------------------------------------------
/** The side's own reskin of a unit another side's scripts name, a Turkiye player's Firtina for the
	* Tomahawk, else the unit itself.  A team order for the American template was costed and saved for
	* at the American price while the factory made and charged the Turkish one, and Money::withdraw
	* takes what is there: a Firtina came out for the Tomahawk's 1200 with 1200 in the bank. */
// ------------------------------------------------------------------------------------------------
static const ThingTemplate *ownTemplate( const Player *player, const ThingTemplate *thing )
{
	const ThingTemplate *own = TheThingFactory->findSideReskin( thing, player->getSide() );
	return own != NULL ? own : thing;
}

// ------------------------------------------------------------------------------------------------
/** The side's spotter (AISideInfo m_spotter) for a team the scripts send at the enemy that names no
	* spotter of its own, else NULL: a gun kept at home has the base's sight. */
// ------------------------------------------------------------------------------------------------
static const ThingTemplate *missingSpotterFor( const Player *player, const TeamPrototype *proto )
{
	const AISideInfo *sideInfo = TheAI->getAiData()->findSideInfo( player );
	if( sideInfo == NULL || sideInfo->m_spotter.isEmpty() )
		return NULL;		// a side that never fires blind
	const TeamTemplateInfo *info = proto->getTemplateInfo();
	if( info->m_isBaseDefense || info->m_isPerimeterDefense || !aiTeamAttacks( info ) )
		return NULL;

	const ThingTemplate *spotter = TheThingFactory->findTemplate( sideInfo->m_spotter );
	for( Int i = 0; i < info->m_numUnitsInfo; ++i )
	{
		const ThingTemplate *unit = TheThingFactory->findTemplate( info->m_unitsInfo[ i ].unitThingName, FALSE );
		if( unit == NULL || info->m_unitsInfo[ i ].maxUnits <= 0 )
			continue;		// an empty slot, or a map's team naming a unit this game does not have
		if( ownTemplate( player, unit )->isEquivalentTo( spotter ) )
			return NULL;
	}
	return spotter;
}

// ------------------------------------------------------------------------------------------------
/** A gun that fires past its own sight came out for a team still being built: it trains the rung's
	* ArtillerySpotters of the side's spotter as part of the team, once.  They used to be ordered with
	* the team, and a barracks turns a 700 spotter out long before a war factory has saved for a 1400
	* gun: Hard bought 23 to 34 spotters in fifteen minutes for 5 to 11 guns, the teams whose gun never
	* came disbanded and the spotters sat in the Sipers. */
// ------------------------------------------------------------------------------------------------
void AIPlayer::sendForSpotters( TeamInQueue *team, const Object *gun )
{
	const Int spotters = getSkillProfile()->m_artillerySpotters;
	if( !runsOwnAI() || spotters <= 0 || !gun->getTemplate()->acquiresBeyondVision() )
		return;
	const ThingTemplate *spotter = missingSpotterFor( m_player, team->m_team->getPrototype() );
	const char *why = spotter == NULL ? "a team kept at home or with its own spotter" : "sends for its spotters";
	for( WorkOrder *order = team->m_workOrders; spotter && order; order = order->m_next )
		if( order->m_thing->isEquivalentTo( spotter ) )
		{
			why = "its spotters already sent for";		// the team's second gun
			spotter = NULL;
		}
	if( spotter )
	{
		// a computer player holds canBuildUnits off between team orders, and the barracks check reads it
		const Bool canBuildUnits = m_player->getCanBuildUnits();
		m_player->setCanBuildUnits( TRUE );
		if( findFactory( spotter, TRUE ) == NULL )
		{
			why = "no barracks to train its spotters";
			spotter = NULL;
		}
		m_player->setCanBuildUnits( canBuildUnits );
	}
	if( spotter )
	{
		WorkOrder *order = newInstance(WorkOrder);
		order->m_thing = spotter;
		order->m_factoryID = INVALID_ID;
		order->m_numRequired = spotters;
		order->m_required = false;		// a team whose spotters are late goes out without them rather than disbanding at the build timeout
		order->m_next = team->m_workOrders;
		team->m_workOrders = order;
	}
	DEBUG_LOG(("AI ARTILLERY frame %d player %d '%s' %d comes out for team '%s', %s\n", TheGameLogic->getFrame(),
		m_player->getPlayerIndex(), gun->getTemplate()->getName().str(), gun->getID(), team->m_team->getName().str(), why));
}

// ------------------------------------------------------------------------------------------------
/** Can this team be given one of the side's guns (AISideInfo m_artillery): one the scripts send at the
	* enemy, all of it on the ground, with no gun that fires past its sight already, no builder and no
	* harvester in it. */
// ------------------------------------------------------------------------------------------------
static Bool takesArtillery( const Player *player, const TeamPrototype *proto )
{
	const TeamTemplateInfo *info = proto->getTemplateInfo();
	if( info->m_isBaseDefense || info->m_isPerimeterDefense || !aiTeamAttacks( info ) )
		return FALSE;
	Bool fielded = FALSE;
	for( Int i = 0; i < info->m_numUnitsInfo; ++i )
	{
		const ThingTemplate *unit = TheThingFactory->findTemplate( info->m_unitsInfo[ i ].unitThingName, FALSE );
		if( unit == NULL || info->m_unitsInfo[ i ].maxUnits <= 0 || sideCanNeverField( player, unit ) )
			continue;
		unit = ownTemplate( player, unit );
		if( unit->isKindOf( KINDOF_AIRCRAFT ) || unit->isKindOf( KINDOF_DOZER ) || unit->isKindOf( KINDOF_HARVESTER ) ||
				unit->acquiresBeyondVision() )
			return FALSE;
		fielded = TRUE;
	}
	return fielded;
}

// ------------------------------------------------------------------------------------------------
/** Return true if team can be considered for building */
// ------------------------------------------------------------------------------------------------
Bool AIPlayer::isPossibleToBuildTeam( TeamPrototype *proto, Bool requireIdleFactory, Bool &notEnoughMoney)
{
	/* Make sure we have at least one idle factory, and factories for all unit types. */
	Bool anyIdle = false;
	Bool anyFielded = false;
	Bool anyLeftOut = false;
	Int cost=0;
	notEnoughMoney = false;
	for( int i=0; i<proto->getTemplateInfo()->m_numUnitsInfo; i++ )
	{
		const TCreateUnitsInfo *unitInfo = &proto->getTemplateInfo()->m_unitsInfo[0];
		const ThingTemplate *thing = TheThingFactory->findTemplate( unitInfo[i].unitThingName );
		if (thing && sideCanNeverField(m_player, thing)) {
			anyLeftOut = true;
			continue;
		}
		if (thing) {
			thing = ownTemplate(m_player, thing);
			anyFielded = true;
			Int thingCost = thing->calcCostToBuild(m_player);
			// One walk of the build list, not two: findFactory hands back the first idle factory
			// when there is one and a busy one only when none is idle, so its queue says which.
			Object *factory = findFactory(thing, true);
			if (NULL == factory) {
				// Couldn't find a factory.
				return false;
			}
			if (factory->getProductionUpdateInterface()->getProductionCount() == 0) {
				// Found an idle factory.
				anyIdle = true;
			}
			cost += thingCost * ((unitInfo[i].maxUnits+unitInfo[i].minUnits)/2.0f);
		}
	}
	if (anyLeftOut && !anyFielded)
		return false;		// nothing in it this side can field
	cost *= TheAI->getAiData()->m_teamResourcesToBuild;
	if (m_player->getMoney()->countMoney() < cost)	{
		notEnoughMoney = true;
		return false; // too expensive
	}
	if (anyIdle) 
	{
		return true;
	}
	if (!requireIdleFactory) 
	{
		// Doesn't require an idle factory, so we're ok.
		return true;
	}
	return false;
}

// ------------------------------------------------------------------------------------------------
/** Check if this team is buildable, doesn't exceed maximum limits, meets conditions, 
	* and isn't under construction. */
// ------------------------------------------------------------------------------------------------
/** Is every unit this team asks for a money unit?  The shipped skirmish scripts give China's hacker
	team a production priority of 1000, which no other team can be scored above, so once it is
	buildable it wins every selection it is offered - fifteen of thirty-five on seed 11. */
static Bool isMoneyUnitTeam( const TeamPrototype *proto )
{
	const TeamTemplateInfo *info = proto->getTemplateInfo();
	Int named = 0;
	for( Int i = 0; i < info->m_numUnitsInfo; ++i )
	{
		const ThingTemplate *unit = TheThingFactory->findTemplate( info->m_unitsInfo[ i ].unitThingName, FALSE );
		if( unit == NULL )
			continue;			// a map's team naming a unit this game does not have
		if( !unit->isKindOf( KINDOF_MONEY_HACKER ) )
			return FALSE;
		++named;
	}
	return named > 0;
}

/** A team of nothing but hackers spends against the same room the direct purchase does, or the two
	of them together bury a barracks under money units all match. */
Bool AIPlayer::hasEnoughMoneyUnitsFor( TeamPrototype *proto ) const
{
	return isMoneyUnitTeam( proto ) && moneyUnitRoom() <= 0;
}

Bool AIPlayer::isAGoodIdeaToBuildTeam( TeamPrototype *proto )
{
	// Check condition.
	if (!proto->evaluateProductionCondition()) {
		return false;
	}

	// the cap on hackers and other money units is the fork's; EA's AI trains what its script asks for
	if (runsOwnAI() && hasEnoughMoneyUnitsFor(proto)) {
		return false;
	}
	// check build limit
	if (proto->countTeamInstances() >= proto->getTemplateInfo()->m_maxInstances){
		if (TheGlobalData->m_debugAI) {	
			AsciiString str;
			str.format("Team %s not chosen - %d already exist.", proto->getName().str(), proto->countTeamInstances());
			TheScriptEngine->AppendDebugMessage(str, false);
		}
		return false;	// Max already built.
	}

	for ( DLINK_ITERATOR<TeamInQueue> iter = iterate_TeamBuildQueue(); !iter.done(); iter.advance())
	{
		TeamInQueue *team = iter.cur();
		if (team->m_team->getPrototype() == proto) {
			return false; // currently building one of these.
		}
	}
	Bool needMoney;
	if (!isPossibleToBuildTeam( proto, true, needMoney)) {
		if (TheGlobalData->m_debugAI) {	
			AsciiString str;
			if (needMoney) {
				str.format("Team %s not chosen - Not enough money.", proto->getName().str());
			} else {
				str.format("Team %s not chosen - Factory/tech missing or busy.", proto->getName().str());
			}
			TheScriptEngine->AppendDebugMessage(str, false);
		}
		return false;
	}
	return true;
}

// ------------------------------------------------------------------------------------------------
/** A spotter killed in the field was never replaced: the team's guns kept walking with nobody to see
	* for them.  An attack team that still has a gun firing past its own sight and none of the side's
	* spotter left on its list trains one as a reinforcement, which joins it where it stands.  One order at
	* a time, and not for a team still being built, whose spotters sendForSpotters has in hand. */
Bool AIPlayer::replaceSpotter( void )
{
	if( !runsOwnAI() || getSkillProfile()->m_artillerySpotters <= 0 )
		return false;
	for( Player::PlayerTeamList::const_iterator t = m_player->getPlayerTeams()->begin(); t != m_player->getPlayerTeams()->end(); ++t )
	{
		TeamPrototype *proto = (*t);
		const ThingTemplate *spotter = missingSpotterFor( m_player, proto );
		if( spotter == NULL )
			continue;
		Bool busy = false;
		for( DLINK_ITERATOR<TeamInQueue> iter = iterate_TeamBuildQueue(); !iter.done(); iter.advance() )
			if( iter.cur()->m_team->getPrototype() == proto )
				busy = true;
		if( busy )
			continue;
		for( DLINK_ITERATOR<Team> iter = proto->iterate_TeamInstanceList(); !iter.done(); iter.advance() )
		{
			Team *team = iter.cur();
			Bool blindGun = false;
			Bool sighted = false;
			for( DLINK_ITERATOR<Object> m = team->iterate_TeamMemberList(); !m.done(); m.advance() )
			{
				if( m.cur()->isEffectivelyDead() )
					continue;
				blindGun |= m.cur()->getTemplate()->acquiresBeyondVision();
				sighted |= m.cur()->getTemplate()->isEquivalentTo( spotter );
			}
			if( !blindGun || sighted )
				continue;

			const Bool canBuildUnits = m_player->getCanBuildUnits();
			m_player->setCanBuildUnits( TRUE );
			Object *factory = findFactory( spotter, TRUE );
			m_player->setCanBuildUnits( canBuildUnits );
			if( factory == NULL )
				return false;

			WorkOrder *order = newInstance(WorkOrder);
			order->m_thing = spotter;
			order->m_factoryID = INVALID_ID;
			order->m_numRequired = 1;
			order->m_required = false;
			order->m_next = NULL;
			TeamInQueue *teamQ = newInstance(TeamInQueue);
			prependTo_TeamBuildQueue( teamQ );
			teamQ->m_priorityBuild = false;
			teamQ->m_reinforcement = true;
			teamQ->m_workOrders = order;
			teamQ->m_frameStarted = TheGameLogic->getFrame();
			teamQ->m_team = team;
			DEBUG_LOG(("AI ARTILLERY frame %d player %d replaces the spotter of team '%s'\n", TheGameLogic->getFrame(),
				m_player->getPlayerIndex(), team->getName().str()));
			m_teamDelay = 0;
			startTraining( order, teamQ->m_priorityBuild, teamQ->m_team->getName() );
			return true;
		}
	}
	return false;
}

// ------------------------------------------------------------------------------------------------
/** See if any existing teams need reinforcements, and have higher priority. */
// ------------------------------------------------------------------------------------------------
Bool AIPlayer::selectTeamToReinforce( Int minPriority )
{
	if( replaceSpotter() )
		return true;

	// Find a high production priority team that needs reinforcements.
	Player::PlayerTeamList::const_iterator t;
	Team *curTeam = NULL;
	Int curPriority = minPriority; // Don't reinforce a team unless it is above min priority.
	const ThingTemplate *curThing = NULL;
	for (t = m_player->getPlayerTeams()->begin(); t != m_player->getPlayerTeams()->end(); ++t)
	{
		TeamPrototype *proto = (*t);
		Bool busy = false;
		for ( DLINK_ITERATOR<TeamInQueue> iter = iterate_TeamBuildQueue(); !iter.done(); iter.advance())
		{
			TeamInQueue *team = iter.cur();
			if (team->m_team->getPrototype() == proto) {
				busy = true; // currently building one of these.
			}
		}
		if (busy) continue;
		if (proto->getTemplateInfo()->m_automaticallyReinforce && proto->getTemplateInfo()->m_productionPriority>curPriority) {
			// Check the team instances.
			for (DLINK_ITERATOR<Team> iter = proto->iterate_TeamInstanceList(); !iter.done(); iter.advance())
			{
				Team *team = iter.cur();
				if (team->hasAnyUnits() == false) 
				{
					continue; // empty.
				}
				const TCreateUnitsInfo *unitInfo = &team->getPrototype()->getTemplateInfo()->m_unitsInfo[0];
				for( int i=0; i<team->getPrototype()->getTemplateInfo()->m_numUnitsInfo; i++ )
				{
					if (unitInfo[i].maxUnits < 1) continue;
					const ThingTemplate *thing = TheThingFactory->findTemplate( unitInfo[i].unitThingName );
					if (thing==NULL) continue;
					Int count=0;
					team->countObjectsByThingTemplate(1, &thing, false, &count);
					if (count < unitInfo[i].maxUnits) 
					{
						// See if there is a factory available.
						if (NULL != findFactory(thing, false)) 
						{
							curTeam = team;
							curPriority = proto->getTemplateInfo()->m_productionPriority;
							curThing = thing;
						}
					}
				}				
			}
		}
	}
	if (curTeam && curThing) 
	{
		/* We have something to build. */
		TeamInQueue *teamQ = newInstance(TeamInQueue);
		// Put in front of queue.
		prependTo_TeamBuildQueue(teamQ);
		teamQ->m_priorityBuild = false;
		teamQ->m_reinforcement = true;

		WorkOrder *order = newInstance(WorkOrder);
		order->m_thing = curThing;
		order->m_factoryID = INVALID_ID;
		order->m_numRequired = 1;
		order->m_required = true;
		// prepend to head of list
		order->m_next = NULL;
		teamQ->m_workOrders = order;
		teamQ->m_frameStarted = TheGameLogic->getFrame();
		teamQ->m_team = curTeam; 

		AsciiString teamName = curTeam->getPrototype()->getName();
		teamName.concat(" - AutoReinforcing one ");
		teamName.concat(curThing->getName());
		TheScriptEngine->AppendDebugMessage(teamName, false);

		// start the creation of a new unit
		Coord3D origin;
		origin = curTeam->getPrototype()->getTemplateInfo()->m_homeLocation;
		if (curTeam->getFirstItemIn_TeamMemberList()) 
		{
			origin = *curTeam->getFirstItemIn_TeamMemberList()->getPosition();
		}
		Object *unit = curTeam->tryToRecruit(curThing, &origin, TheAI->getAiData()->m_maxRecruitDistance);
		if (unit) 
		{
			order->m_numCompleted = 1;

			AsciiString teamStr = "Team '";
			teamStr.concat(curTeam->getPrototype()->getName());
			teamStr.concat("' recruits ");
			teamStr.concat(curThing->getName());
			teamStr.concat(" from team '");
			teamStr.concat(unit->getTeam()->getPrototype()->getName());
			teamStr.concat("'");
			TheScriptEngine->AppendDebugMessage(teamStr, false);

			unit->setTeam(curTeam);

			teamQ->m_reinforcementID = unit->getID();

			AIUpdateInterface *ai = unit->getAIUpdateInterface();
			if (ai) 
			{
				ai->aiIdle(CMD_FROM_AI);
			}
		} else {
			startTraining( order, teamQ->m_priorityBuild, teamQ->m_team->getName());
		}
		m_teamDelay = 0;
		return true;
	}
	return false;
}

// ------------------------------------------------------------------------------------------------
/** Determine the next team to build.  Return true if one was selected. */
// ------------------------------------------------------------------------------------------------
static Real templateMaxHealth( const ThingTemplate *tmpl )
{
	std::map<const ThingTemplate *, Real>::const_iterator known = theMaxHealthCache.find( tmpl );
	if( known != theMaxHealthCache.end() )
		return known->second;
	const Real health = tmpl->calcMaxHealth();
	theMaxHealthCache[ tmpl ] = health;
	return health;
}

/** "Can it see stealth" is not a KindOf - it is a module the data hangs on the unit, so ask the
	* template's module list by name, once per template. */
static Bool templateDetectsStealth( const ThingTemplate *tmpl )
{
	std::map<const ThingTemplate *, Bool>::const_iterator known = theDetectsStealthCache.find( tmpl );
	if( known != theDetectsStealthCache.end() )
		return known->second;
	Bool detects = FALSE;
	const ModuleInfo &modules = tmpl->getBehaviorModuleInfo();
	for( Int m = 0; m < modules.getCount(); ++m )
	{
		if( modules.getNthName( m ).compareNoCase( "StealthDetectorUpdate" ) == 0 )
		{
			detects = TRUE;
			break;
		}
	}
	theDetectsStealthCache[ tmpl ] = detects;
	return detects;
}

// ponytail: an aircraft that empties its clip flies home to rearm; a flat twenty seconds stands in
// for the trip, until a measured sortie replaces it
static const Real AI_SORTIE_FRAMES = 20.0f * LOGICFRAMES_PER_SECOND;

//-------------------------------------------------------------------------------------------------
/** How one weapon works on one target, as the numbers aiFramesToKill needs.  FALSE when the weapon
	* may not aim at it at all.  Read off the unconditioned sets - no upgrades, no veterancy - which is
	* the unit the build menu offers. */
//-------------------------------------------------------------------------------------------------
static Bool shotPatternAgainst( const WeaponTemplate *weapon, const ThingTemplate *target, Real targetHealth, AIShotPattern *out )
{
	const Int aimNeeded = target->isKindOf( KINDOF_AIRCRAFT ) ? WEAPON_ANTI_AIRBORNE_VEHICLE : WEAPON_ANTI_GROUND;
	if( (weapon->getAntiMask() & aimNeeded) == 0 )
		return FALSE;

	//
	// A gattling spins up: its own WeaponBonus block makes it fire faster once it has kept firing.
	// An exchange lasts that long, so it is read at the fastest step the weapon has.
	//
	WeaponBonus bonus;
	if( weapon->getExtraBonus() != NULL )
	{
		WeaponBonusConditionFlags spunUp = 0;
		if( weapon->getContinuousFireTwoShotsNeeded() != INT_MAX )
			spunUp = 1 << WEAPONBONUSCONDITION_CONTINUOUS_FIRE_FAST;
		else if( weapon->getContinuousFireOneShotsNeeded() != INT_MAX )
			spunUp = 1 << WEAPONBONUSCONDITION_CONTINUOUS_FIRE_MEAN;
		weapon->getExtraBonus()->appendBonuses( spunUp, bonus );
	}

	const ArmorTemplateSet *armorSet = target->findArmorTemplateSet( ArmorSetFlags() );
	const Armor armor = TheArmorStore->makeArmor( armorSet ? armorSet->getArmorTemplate() : NULL );
	const DamageType type = weapon->getDamageType();

	if( type == DAMAGE_KILLPILOT )
		out->m_damagePerShot = target->isKindOf( KINDOF_VEHICLE ) ? armor.adjustDamage( type, targetHealth ) : 0.0f;	// the vehicle leaves the fight in one hit
	else if( type == DAMAGE_HEALING || !IsHealthDamagingDamage( type ) )
		out->m_damagePerShot = 0.0f;
	else
		out->m_damagePerShot = armor.adjustDamage( type, weapon->getPrimaryDamage( bonus ) );

	const Real rateOfFire = bonus.getField( WeaponBonus::RATE_OF_FIRE );
	const Real preAttack = INT_TO_REAL( weapon->getPreAttackDelay( bonus ) );
	out->m_delayFrames = INT_TO_REAL( weapon->getMinDelayBetweenShots() + weapon->getMaxDelayBetweenShots() ) * 0.5f / rateOfFire;
	out->m_clipSize = weapon->getClipSize();
	out->m_reloadFrames = INT_TO_REAL( weapon->getClipReloadTime( bonus ) );
	if( weapon->getReloadType() == RETURN_TO_BASE_TO_RELOAD )
		out->m_reloadFrames += AI_SORTIE_FRAMES;

	switch( weapon->getPrefireType() )
	{
		case PREFIRE_PER_SHOT:
			out->m_delayFrames += preAttack;
			out->m_reloadFrames += preAttack;
			break;
		case PREFIRE_PER_CLIP:
			out->m_openingFrames = preAttack;
			out->m_reloadFrames += preAttack;
			break;
		case PREFIRE_PER_ATTACK:
			out->m_openingFrames = preAttack;
			break;
	}
	return TRUE;
}

//-------------------------------------------------------------------------------------------------
/** Frames one unit template needs to kill another, with the best weapon it carries against it. */
//-------------------------------------------------------------------------------------------------
static Real computeTemplateFramesToKill( const ThingTemplate *attacker, const ThingTemplate *target )
{
	if( attacker->getWeaponTemplateSets().empty() )
		return AI_CANNOT_KILL;			// a dozer, a supply truck: the data gives it nothing to shoot with

	const Real health = templateMaxHealth( target );
	const WeaponTemplateSet *set = attacker->findWeaponTemplateSet( WeaponSetFlags() );
	Real best = AI_CANNOT_KILL;
	for( Int slot = 0; slot < WEAPONSLOT_COUNT; ++slot )
	{
		const WeaponTemplate *weapon = set->getNth( (WeaponSlotType)slot );
		AIShotPattern shots;
		if( weapon == NULL || !shotPatternAgainst( weapon, target, health, &shots ) )
			continue;

		const Real frames = aiFramesToKill( health, shots );
		if( frames >= 0.0f && (best < 0.0f || frames < best) )
			best = frames;
	}
	return best;
}

static Real templateFramesToKill( const ThingTemplate *attacker, const ThingTemplate *target )
{
	const AITemplatePair key( attacker, target );
	std::map<AITemplatePair, Real>::const_iterator known = theFramesToKillCache.find( key );
	if( known != theFramesToKillCache.end() )
		return known->second;
	const Real frames = computeTemplateFramesToKill( attacker, target );
	theFramesToKillCache[ key ] = frames;
	return frames;
}

static Int playerArmyMod( const Player *player )
{
	const PlayerTemplate *faction = player->getPlayerTemplate();
	return faction != NULL ? faction->getModIndex() : -1;
}

/** A mod's America carries retail's side name, so a scan of every template by side or by kind walks
 * into the other army: a retail Ranger's job went to a mod's cheaper rifleman nobody can train here. */
static Bool isOwnArmy( const Player *player, const ThingTemplate *tmpl )
{
	return tmpl->getModIndex() == playerArmyMod( player );
}

/*static*/ void AIPlayer::prepareMatchupTables( void )
{
	theFramesToKillCache.clear();
	theMaxHealthCache.clear();
	theDetectsStealthCache.clear();

	Bool skirmishAI = FALSE;
	typedef std::pair<AsciiString, Int> SideInMod;
	std::vector<SideInMod> sides;
	for( Int i = 0; i < ThePlayerList->getPlayerCount(); ++i )
	{
		Player *player = ThePlayerList->getNthPlayer( i );
		if( player->isSkirmishAIPlayer() )
			skirmishAI = TRUE;
		const SideInMod side( player->getSide(), playerArmyMod( player ) );
		if( std::find( sides.begin(), sides.end(), side ) == sides.end() )
			sides.push_back( side );
	}
	if( !skirmishAI )
		return;

#ifdef DEBUG_LOGGING
	const Int64 start = Clock_Ticks();
#endif
	// ponytail: units only, the ones a side in the match can buy; a captured or scripted unit, a
	// defence, an override from map.ini is still answered the first time it is asked
	std::vector<const ThingTemplate *> units;
	for( const ThingTemplate *tmpl = TheThingFactory->firstTemplate(); tmpl; tmpl = tmpl->friend_getNextTemplate() )
	{
		if( tmpl->friend_getBuildCost() <= 0 || tmpl->isKindOf( KINDOF_STRUCTURE ) || tmpl->getBuildable() == BSTATUS_NO )
			continue;
		if( std::find( sides.begin(), sides.end(), SideInMod( tmpl->getDefaultOwningSide(), tmpl->getModIndex() ) ) != sides.end() )
			units.push_back( tmpl );
	}
	for( size_t a = 0; a < units.size(); ++a )
	{
		templateDetectsStealth( units[ a ] );
		for( size_t t = 0; t < units.size(); ++t )
			templateFramesToKill( units[ a ], units[ t ] );
	}
#ifdef DEBUG_LOGGING
	const Int64 end = Clock_Ticks();
	DEBUG_LOG(( "AI MATCHUP PREFILL %d units, %d pairs, %.1fms\n", (Int)units.size(),
		(Int)theFramesToKillCache.size(), aiPlayerElapsedMS( start, end ) ));
#endif
}

//-------------------------------------------------------------------------------------------------
/** Count one visible enemy unit into the army: one entry per kind of unit, in the order first seen. */
//-------------------------------------------------------------------------------------------------
static void addToVisibleArmy( std::vector<AIVisibleEnemy> *army, const ThingTemplate *tmpl, const Player *owner, Real weight )
{
	for( std::vector<AIVisibleEnemy>::iterator kind = army->begin(); kind != army->end(); ++kind )
	{
		if( kind->m_template == tmpl )
		{
			kind->m_weight += weight;
			return;
		}
	}

	AIVisibleEnemy kind;
	kind.m_template = tmpl;
	kind.m_weight = weight;
	kind.m_cost = INT_TO_REAL( tmpl->calcCostToBuild( owner ) );
	army->push_back( kind );
}

//-------------------------------------------------------------------------------------------------
/** How one army trades against another, unit against unit and money for money (aiArmyAdvantage). */
//-------------------------------------------------------------------------------------------------
static Real armyAdvantage( const std::vector<AIVisibleEnemy> &mine, const std::vector<AIVisibleEnemy> &theirs )
{
	if( mine.empty() || theirs.empty() )
		return 1.0f;
	std::vector<Real> myValue, theirValue, score;
	for( size_t i = 0; i < mine.size(); ++i )
		myValue.push_back( mine[ i ].m_weight );
	for( size_t j = 0; j < theirs.size(); ++j )
		theirValue.push_back( theirs[ j ].m_weight );
	for( size_t i = 0; i < mine.size(); ++i )
		for( size_t j = 0; j < theirs.size(); ++j )
			score.push_back( aiMatchupScore( templateFramesToKill( mine[ i ].m_template, theirs[ j ].m_template ),
				templateFramesToKill( theirs[ j ].m_template, mine[ i ].m_template ), mine[ i ].m_cost, theirs[ j ].m_cost ) );
	return aiArmyAdvantage( &myValue[ 0 ], (Int)myValue.size(), &theirValue[ 0 ], (Int)theirValue.size(), &score[ 0 ] );
}

/** One unit into an army by kind, at its combat power, when an army is being kept. */
static void addToArmy( std::vector<AIVisibleEnemy> *army, const Object *obj )
{
	const Real power = aiCombatPower( obj );
	if( army && power > 0.0f && !obj->isEffectivelyDead() )
		addToVisibleArmy( army, obj->getTemplate(), obj->getControllingPlayer(), power );
}

/** How often the ledger of seen enemies is brought up to date, and how long a unit nobody has seen is
	* still believed in.  Three minutes is long enough for an army that walked back into the fog to be
	* remembered when it walks out again, and short enough that one the AI never saw die is let go. */
static const Int LEDGER_RATE = 2 * LOGICFRAMES_PER_SECOND;
static const UnsignedInt LEDGER_FORGET_FRAMES = 180 * LOGICFRAMES_PER_SECOND;

//-------------------------------------------------------------------------------------------------
/** Write down every enemy unit in sight, and cross off the ones it has looked for and not found.  Only
	* what observerKnowsAbout allows, so the ledger is what a player would know from his own screen. */
//-------------------------------------------------------------------------------------------------
void AIPlayer::rememberEnemies( void )
{
	const Int me = m_player->getPlayerIndex();
	const UnsignedInt now = TheGameLogic->getFrame();
	if( !runsOwnAI() || (now + computeUpdatePhase( me, LEDGER_RATE )) % LEDGER_RATE != 0 )
		return;

	for( Int i = 0; i < ThePlayerList->getPlayerCount(); ++i )
	{
		Player *p = ThePlayerList->getNthPlayer( i );
		if( p == NULL || p == m_player || m_player->getRelationship( p->getDefaultTeam() ) != ENEMIES )
			continue;
		for( Player::PlayerTeamList::const_iterator t = p->getPlayerTeams()->begin(); t != p->getPlayerTeams()->end(); ++t )
		{
			for( DLINK_ITERATOR<Team> iter = (*t)->iterate_TeamInstanceList(); !iter.done(); iter.advance() )
			{
				for( DLINK_ITERATOR<Object> objIter = iter.cur()->iterate_TeamMemberList(); !objIter.done(); objIter.advance() )
				{
					Object *obj = objIter.cur();
					if( obj->isEffectivelyDead() || obj->isContained() || obj->isKindOf( KINDOF_STRUCTURE ) ||
							obj->isKindOf( KINDOF_PROJECTILE ) || aiCombatPower( obj ) <= 0.0f || !observerKnowsAbout( obj, me ) )
						continue;
					SeenEnemy &row = m_seenEnemies[ obj->getID() ];
					row.tmpl = obj->getTemplate();
					row.owner = p->getPlayerIndex();
					row.seen = now;
					row.pos = *obj->getPosition();
					row.stealth = obj->getStatusBits().test( OBJECT_STATUS_CAN_STEALTH );
				}
			}
		}
	}

	for( std::map<ObjectID, SeenEnemy>::iterator it = m_seenEnemies.begin(); it != m_seenEnemies.end(); )
	{
		Bool forget = it->second.seen + LEDGER_FORGET_FRAMES <= now;
		if( !forget && it->second.seen != now &&
				ThePartitionManager->getShroudStatusForPlayer( me, &it->second.pos ) == CELLSHROUD_CLEAR )
		{
			// the ground it stood on is in sight and it is not there: gone, unless it still exists and only
			// walked off, in which case it is somewhere in the fog still
			const Object *obj = TheGameLogic->findObjectByID( it->first );
			forget = obj == NULL || obj->isEffectivelyDead() || obj->getControllingPlayer()->getPlayerIndex() != it->second.owner;
		}
		if( forget )
			m_seenEnemies.erase( it++ );
		else
			++it;
	}
}

//-------------------------------------------------------------------------------------------------
/** The ledger as an army: one entry a kind of unit, weighed by what each is worth in a fight, in the
	* order of the units' IDs. */
//-------------------------------------------------------------------------------------------------
void AIPlayer::rememberedArmy( std::vector<AIVisibleEnemy> *army ) const
{
	for( std::map<ObjectID, SeenEnemy>::const_iterator it = m_seenEnemies.begin(); it != m_seenEnemies.end(); ++it )
	{
		const Player *owner = ThePlayerList->getNthPlayer( it->second.owner );
		addToVisibleArmy( army, it->second.tmpl, owner, templateCombatPower( it->second.tmpl, owner ) );
	}
}

//-------------------------------------------------------------------------------------------------
/** AI ENGAGE: something of ours sets off at the enemy.  What it is worth, what the enemy army it
	* remembers is worth, and the two ratios a fight is decided by: money against money, and how the
	* units fare against each other (armyAdvantage).  The cause says who sent it. */
//-------------------------------------------------------------------------------------------------
void AIPlayer::logEngage( const char *cause, const std::vector<Object *> &units )
{
	std::vector<AIVisibleEnemy> mine;
	Real power = 0.0f;
	for( std::vector<Object *>::const_iterator u = units.begin(); u != units.end(); ++u )
	{
		const Real p = aiCombatPower( *u );
		if( (*u)->isEffectivelyDead() || p <= 0.0f )
			continue;
		power += p;
		addToVisibleArmy( &mine, (*u)->getTemplate(), m_player, p );
	}
	if( mine.empty() )
		return;
	std::vector<AIVisibleEnemy> theirs;
	rememberedArmy( &theirs );
	Real theirPower = 0.0f;
	for( std::vector<AIVisibleEnemy>::const_iterator k = theirs.begin(); k != theirs.end(); ++k )
		theirPower += k->m_weight;
	DEBUG_LOG(("AI ENGAGE frame %d player %d %s: %d units, %.0f power against %.0f remembered, cost %.2f matchup %.2f\n",
		TheGameLogic->getFrame(), m_player->getPlayerIndex(), cause, (Int)units.size(), power, theirPower,
		theirPower > 0.0f ? power / theirPower : 99.0f, armyAdvantage( mine, theirs )));
}

//-------------------------------------------------------------------------------------------------
/** AI LOSS: one of a team's units died.  Straggling when two or more of its team are still alive and
	* at most one of them stands within 300 of it: the unit that went in on its own, as against the last
	* survivor of a lost fight, which dies alone whatever the AI did. */
//-------------------------------------------------------------------------------------------------
void AIPlayer::onUnitLost( const Object *obj )
{
	const Team *team = obj->getTeam();
	// AI DEFLOSS: killed by a building's gun or its garrison, for the count of units thrown at defences
	const DamageInfo *last = obj->getBodyModule()->getLastDamageInfo();
	const Object *killer = last ? TheGameLogic->findObjectByID( last->in.m_sourceID ) : NULL;
	if( killer && killer->getContainedBy() )
		killer = killer->getContainedBy();
	if( runsOwnAI() && !obj->isKindOf( KINDOF_STRUCTURE ) && killer && killer->isKindOf( KINDOF_STRUCTURE ) &&
			m_player->getRelationship( killer->getTeam() ) == ENEMIES )
		DEBUG_LOG(("AI DEFLOSS frame %d player %d '%s' worth %.0f to '%s'; team '%s' state %d source %d, %.0f from home\n", TheGameLogic->getFrame(),
			m_player->getPlayerIndex(), obj->getTemplate()->getName().str(), aiCombatPower( obj ), killer->getTemplate()->getName().str(),
			team ? team->getName().str() : "", obj->getAI() ? (Int)obj->getAI()->getCurrentStateID() : -1,
			obj->getAI() ? (Int)obj->getAI()->getLastCommandSource() : -1,
			(Real)sqrt( sqr( obj->getPosition()->x - m_baseCenter.x ) + sqr( obj->getPosition()->y - m_baseCenter.y ) )));
	// what killed a duty Ambulance, from how far, and whether this AI knew of it
	if( runsOwnAI() && isDutyAmbulance( obj ) )
		DEBUG_LOG(("AI AMBULANCE frame %d player %d %d killed by '%s' %.0f away, %s, %s\n", TheGameLogic->getFrame(), m_player->getPlayerIndex(),
			obj->getID(), killer ? killer->getTemplate()->getName().str() : "nothing known",
			killer ? (Real)sqrt( sqr( killer->getPosition()->x - obj->getPosition()->x ) + sqr( killer->getPosition()->y - obj->getPosition()->y ) ) : -1.0f,
			killer && observerKnowsAbout( killer, m_player->getPlayerIndex() ) ? "known" : "unseen",
			killer && killer->isKindOf( KINDOF_AIRCRAFT ) ? "aircraft" : killer && killer->isKindOf( KINDOF_STRUCTURE ) ? "building" : "ground"));
	if( runsOwnAI() && obj->isKindOf( KINDOF_STRUCTURE ) && aiCombatPower( obj ) > 0.0f )
	{
		// the gun behind a shell or a missile, for the count of defences the siege guns took
		const Object *gun = killer && killer->isKindOf( KINDOF_PROJECTILE ) ? TheGameLogic->findObjectByID( killer->getProducerID() ) : killer;
		DEBUG_LOG(("AI DEFDEAD frame %d player %d '%s' worth %.0f to %d '%s'\n", TheGameLogic->getFrame(), m_player->getPlayerIndex(),
			obj->getTemplate()->getName().str(), aiCombatPower( obj ), gun ? gun->getID() : INVALID_ID,
			gun ? gun->getTemplate()->getName().str() : ""));
	}
	if( !runsOwnAI() || obj->isKindOf( KINDOF_STRUCTURE ) || team == NULL || team == m_player->getDefaultTeam() )
		return;
	const Real NEAR_SQR = 300.0f * 300.0f;
	Int alive = 0;
	Int beside = 0;		// not "near": windef.h makes that a macro
	for( DLINK_ITERATOR<Object> m = team->iterate_TeamMemberList(); !m.done(); m.advance() )
	{
		const Object *mate = m.cur();
		if( mate == obj || mate->isEffectivelyDead() || mate->isKindOf( KINDOF_STRUCTURE ) )
			continue;
		++alive;
		if( sqr( mate->getPosition()->x - obj->getPosition()->x ) + sqr( mate->getPosition()->y - obj->getPosition()->y ) <= NEAR_SQR )
			++beside;
	}
	DEBUG_LOG(("AI LOSS frame %d player %d '%s' of '%s' %s, %d teammates alive, %d within 300\n", TheGameLogic->getFrame(),
		m_player->getPlayerIndex(), obj->getTemplate()->getName().str(), team->getName().str(),
		alive < 2 ? "last" : (beside <= 1 ? "straggling" : "grouped"), alive, beside));
}

//-------------------------------------------------------------------------------------------------
/** What a team prototype can answer, over every unit it is built from: each of its units against
	* each kind of enemy unit in sight, both ways round, weighted by how much of the visible army that
	* kind is and by how many of the unit the team fields. */
//-------------------------------------------------------------------------------------------------
static AITeamCapability teamCapability( const TeamPrototype *proto, const Player *owner, const std::vector<AIVisibleEnemy> &army )
{
	AITeamCapability cap;
	const TeamTemplateInfo *info = proto ? proto->getTemplateInfo() : NULL;
	if( info == NULL )
		return cap;

	Real answered = 0.0f;
	Real weighed = 0.0f;
	for( Int i = 0; i < info->m_numUnitsInfo; ++i )
	{
		const ThingTemplate *tmpl = TheThingFactory->findTemplate( info->m_unitsInfo[ i ].unitThingName, FALSE );
		if( tmpl == NULL )
			continue;			// a map's team naming a unit this game does not have

		// the same question the game itself answers when it builds the object, one INI edit away from
		// being right for a mod's own detector
		if( templateDetectsStealth( tmpl ) )
			cap.m_detectsStealth = TRUE;

		const Int minUnits = info->m_unitsInfo[ i ].minUnits;
		const Real fielded = INT_TO_REAL( minUnits > 1 ? minUnits : 1 );
		const Real myCost = INT_TO_REAL( tmpl->calcCostToBuild( owner ) );
		for( std::vector<AIVisibleEnemy>::const_iterator enemy = army.begin(); enemy != army.end(); ++enemy )
		{
			const Real score = aiMatchupScore( templateFramesToKill( tmpl, enemy->m_template ),
																				 templateFramesToKill( enemy->m_template, tmpl ), myCost, enemy->m_cost );
			answered += fielded * enemy->m_weight * score;
			weighed += fielded * enemy->m_weight;
		}
	}

	if( weighed > 0.0f )
		cap.m_answer = answered / weighed;
	return cap;
}

/** A template that goes invisible from the factory: an innate StealthUpdate.  Most of a side carries one
	* that only an upgrade or a scrambler switches on, and those are not a stealthy army. */
static Bool templateCanStealth( const ThingTemplate *tmpl )
{
	const ModuleInfo &modules = tmpl->getBehaviorModuleInfo();
	for( Int m = 0; m < modules.getCount(); ++m )
		if( modules.getNthName( m ).compareNoCase( "StealthUpdate" ) == 0 )
			return ( (const StealthUpdateModuleData *)modules.getNthData( m ) )->m_innateStealth;
	return FALSE;
}

/** Some unit that goes invisible from the factory waits on this science: the USA's Pathfinder. */
static Bool unlocksStealthUnit( const Player *player, ScienceType science )
{
	for( const ThingTemplate *tmpl = TheThingFactory->firstTemplate(); tmpl; tmpl = tmpl->friend_getNextTemplate() )
	{
		if( !templateCanStealth( tmpl ) || !isOwnArmy( player, tmpl ) )
			continue;
		for( Int p = 0; p < tmpl->getPrereqCount(); ++p )
		{
			const ScienceVec &sciences = tmpl->getNthPrereq( p )->getSciencePrereqs();
			for( ScienceVec::const_iterator s = sciences.begin(); s != sciences.end(); ++s )
				if( *s == science )
					return TRUE;
		}
	}
	return FALSE;
}

static Int personaFavouredKind( const AIPersonaWeights &taste );

/** The kinds a personality's share is counted in: personaFavouredKind's three, then units that can go
	* invisible (a Sneaky AI's) and a GLA Air AI's rockets and anti-air (isAirAnalogue). */
enum { SHARE_VEHICLE, SHARE_INFANTRY, SHARE_AIR, SHARE_STEALTH, SHARE_AIR_ANALOGUE, SHARE_KINDS };

/** The share of its kind among the units it has built that an Air, Infantry, Armor or Sneaky AI wants,
	* by rung, before the scripts' teams short of that kind are built again.  A team under half its cost
	* of the kind is short of it: an infantry team riding a Humvee is still infantry. */
static const Real PERSONA_SHARE_TARGET[ SHARE_KINDS ][ AISKILL_COUNT ] = {
	{ 0.50f, 0.65f, 0.75f }, { 0.60f, 0.65f, 0.70f }, { 0.35f, 0.50f, 0.60f }, { 0.30f, 0.40f, 0.50f }, { 0.35f, 0.50f, 0.60f } };
static const Real PERSONA_TEAM_KIND_SHARE = 0.5f;
/** The bank, by rung, past which nothing is skipped: Hard's hoard, and Easy's floor where it has none. */
static const Int PERSONA_SKIP_BANK[ AISKILL_COUNT ] = { 3000, 4000, 4000 };

static Bool isAirAnalogue( const ThingTemplate *tmpl );

/** Of the units this player built that fly, walk or drive, the share of one kind (SHARE_*), by count as
	* the HEADLESS PERSONA summary counts it. */
static Real builtKindShare( Player *player, Int kind )
{
	ScoreKeeper *score = player->getScoreKeeper();
	const KindOfMaskType none = KINDOFMASK_NONE;
	KindOfMaskType air = none;
	air.set( KINDOF_AIRCRAFT );
	KindOfMaskType ground = none;
	ground.set( KINDOF_INFANTRY );
	KindOfMaskType vehicle = none;
	vehicle.set( KINDOF_VEHICLE );
	KindOfMaskType notVehicle = air;
	notVehicle.set( KINDOF_HARVESTER );
	notVehicle.set( KINDOF_DOZER );
	Int built[ SHARE_KINDS ];
	built[ SHARE_VEHICLE ] = score->getTotalUnitsBuilt( vehicle, notVehicle );
	built[ SHARE_INFANTRY ] = score->getTotalUnitsBuilt( ground, air );
	built[ SHARE_AIR ] = score->getTotalUnitsBuilt( air, none );
	const Int total = built[ SHARE_VEHICLE ] + built[ SHARE_INFANTRY ] + built[ SHARE_AIR ];
	if( kind == SHARE_STEALTH )
		built[ kind ] = score->getTotalUnitsBuilt( ground, air, templateCanStealth ) + score->getTotalUnitsBuilt( vehicle, notVehicle, templateCanStealth ) +
			score->getTotalUnitsBuilt( air, none, templateCanStealth );
	else if( kind == SHARE_AIR_ANALOGUE )
		built[ kind ] = score->getTotalUnitsBuilt( vehicle, notVehicle, isAirAnalogue );
	return total > 0 ? INT_TO_REAL( built[ kind ] ) / INT_TO_REAL( total ) : 0.0f;
}

/** What a team prototype is made of, by cost: the shares that fly, walk, drive and can go invisible,
	* each 0..1, for the personality's taste (aiPersonaTeamBias). */
static void teamKindShares( const TeamPrototype *proto, const Player *owner, Real *air, Real *infantry, Real *vehicle, Real *stealth )
{
	*air = *infantry = *vehicle = *stealth = 0.0f;
	const TeamTemplateInfo *info = proto->getTemplateInfo();
	Real total = 0.0f;
	for( Int i = 0; i < info->m_numUnitsInfo; ++i )
	{
		const ThingTemplate *tmpl = TheThingFactory->findTemplate( info->m_unitsInfo[ i ].unitThingName, FALSE );
		if( tmpl == NULL )
			continue;			// a map's team naming a unit this game does not have
		const Int minUnits = info->m_unitsInfo[ i ].minUnits;
		const Real worth = INT_TO_REAL( minUnits > 1 ? minUnits : 1 ) * INT_TO_REAL( tmpl->calcCostToBuild( owner ) );
		total += worth;
		if( tmpl->isKindOf( KINDOF_AIRCRAFT ) )
			*air += worth;
		else if( tmpl->isKindOf( KINDOF_INFANTRY ) )
			*infantry += worth;
		else if( tmpl->isKindOf( KINDOF_VEHICLE ) )
			*vehicle += worth;
		if( templateCanStealth( tmpl ) )
			*stealth += worth;
	}
	if( total <= 0.0f )
		return;
	*air /= total;
	*infantry /= total;
	*vehicle /= total;
	*stealth /= total;
}

//-------------------------------------------------------------------------------------------------
/** What this AI can see of what its enemies are fielding, weighted by threat value so a tank
	* counts for more than a rifleman.  Fog-aware by construction - it asks observerKnowsAbout of
	* every object, which is what makes B1 A2's first customer. */
//-------------------------------------------------------------------------------------------------
void AIPlayer::computeEnemyComposition( AIEnemyComposition *out, std::vector<AIVisibleEnemy> *army )
{
	Real air = 0.0f, armour = 0.0f, infantry = 0.0f, stealth = 0.0f, total = 0.0f;
	const Int me = m_player->getPlayerIndex();

	// AnswerArmy: the army it remembers, not the part of it in sight this second.  Ten Overlords that
	// rolled back behind the fog are still ten Overlords, and the counter has to be on order before they
	// come back, not after
	if( getSkillProfile()->m_answerArmy )
	{
		for( std::map<ObjectID, SeenEnemy>::const_iterator it = m_seenEnemies.begin(); it != m_seenEnemies.end(); ++it )
		{
			const ThingTemplate *tmpl = it->second.tmpl;
			const Player *owner = ThePlayerList->getNthPlayer( it->second.owner );
			const Real threat = templateCombatPower( tmpl, owner );
			if( threat <= 0.0f )
				continue;
			total += threat;
			if( army != NULL )
				addToVisibleArmy( army, tmpl, owner, threat );
			if( tmpl->isKindOf( KINDOF_AIRCRAFT ) )		air += threat;
			if( tmpl->isKindOf( KINDOF_VEHICLE ) )		armour += threat;
			if( tmpl->isKindOf( KINDOF_INFANTRY ) )		infantry += threat;
			if( it->second.stealth )									stealth += threat;
			if( tmpl->isKindOf( KINDOF_INFANTRY ) )
				++out->m_infantryCount;
			else if( tmpl->isKindOf( KINDOF_VEHICLE ) && !tmpl->isKindOf( KINDOF_AIRCRAFT ) )
				++out->m_vehicleCount;
		}
	}
	else for( Int i = 0; i < ThePlayerList->getPlayerCount(); ++i )
	{
		Player *p = ThePlayerList->getNthPlayer( i );
		if( p == NULL || p == m_player )
			continue;
		if( m_player->getRelationship( p->getDefaultTeam() ) != ENEMIES )
			continue;

		for( Player::PlayerTeamList::const_iterator t = p->getPlayerTeams()->begin();
				 t != p->getPlayerTeams()->end(); ++t )
		{
			for( DLINK_ITERATOR<Team> iter = (*t)->iterate_TeamInstanceList(); !iter.done(); iter.advance() )
			{
				Team *team = iter.cur();
				if( team == NULL )
					continue;
				for( DLINK_ITERATOR<Object> objIter = team->iterate_TeamMemberList(); !objIter.done(); objIter.advance() )
				{
					Object *obj = objIter.cur();
					if( obj == NULL || obj->isEffectivelyDead() )
						continue;
					if( obj->isKindOf( KINDOF_STRUCTURE ) || obj->isKindOf( KINDOF_PROJECTILE ) )
						continue;			// this is about the army, not the base
					if( !observerKnowsAbout( obj, me ) )
						continue;

					const Real threat = aiCombatPower( obj );
					if( threat <= 0.0f )
						continue;

					total += threat;
					if( army != NULL )
						addToVisibleArmy( army, obj->getTemplate(), p, threat );
					if( obj->isKindOf( KINDOF_AIRCRAFT ) )				air += threat;
					if( obj->isKindOf( KINDOF_VEHICLE ) )					armour += threat;
					if( obj->isKindOf( KINDOF_INFANTRY ) )				infantry += threat;
					if( obj->getStatusBits().test( OBJECT_STATUS_CAN_STEALTH ) )	stealth += threat;
					if( obj->isKindOf( KINDOF_INFANTRY ) )
						++out->m_infantryCount;
					else if( obj->isKindOf( KINDOF_VEHICLE ) && !obj->isKindOf( KINDOF_AIRCRAFT ) )
						++out->m_vehicleCount;
				}
			}
		}
	}

	out->m_totalThreat = total;

	if( total <= 0.0f )
		return;			// nothing seen: the caller's zeroed composition means "no opinion"

	out->m_air = air / total;
	out->m_armour = armour / total;
	out->m_infantry = infantry / total;
	out->m_stealth = stealth / total;
}

//-------------------------------------------------------------------------------------------------
/** What this AI can see another player is worth, priced in build cost - his army and his base.
	* Fog-aware, so an enemy nobody has scouted is worth nothing to attack rather than everything. */
//-------------------------------------------------------------------------------------------------
Real AIPlayer::visibleEstateValue( Int playerNdx )
{
	Player *p = ThePlayerList->getNthPlayer( playerNdx );
	if( p == NULL )
		return 0.0f;

	const Int me = m_player->getPlayerIndex();
	Real value = 0.0f;

	for( Player::PlayerTeamList::const_iterator t = p->getPlayerTeams()->begin();
			 t != p->getPlayerTeams()->end(); ++t )
	{
		for( DLINK_ITERATOR<Team> iter = (*t)->iterate_TeamInstanceList(); !iter.done(); iter.advance() )
		{
			Team *team = iter.cur();
			if( team == NULL )
				continue;
			for( DLINK_ITERATOR<Object> objIter = team->iterate_TeamMemberList(); !objIter.done(); objIter.advance() )
			{
				Object *obj = objIter.cur();
				if( obj == NULL || obj->isEffectivelyDead() || obj->isKindOf( KINDOF_PROJECTILE ) )
					continue;
				if( !observerKnowsAbout( obj, me ) )
					continue;
				value += INT_TO_REAL( obj->getTemplate()->calcCostToBuild( p ) );
			}
		}
	}
	return value;
}

Bool AIPlayer::selectTeamToBuild( void )
{

	// find the highest priority of all teams
	Player::PlayerTeamList::const_iterator t;
	const Int invalidPri = -99999;
	Int hiPri = invalidPri;
	// collect all teams that are possible to build, and are at the highest priority
	Player::PlayerTeamList candidateList1;
	for (t = m_player->getPlayerTeams()->begin(); t != m_player->getPlayerTeams()->end(); ++t)
	{
		if (isAGoodIdeaToBuildTeam(*t))
		{
			candidateList1.push_back( (*t) );
			Int pri = (*t)->getTemplateInfo()->m_productionPriority;

			if (pri > hiPri)
			{
				hiPri = pri;
			}
		}
	}

	if (selectTeamToReinforce(hiPri)) {
		return true;
	}

	// check if no team prototypes are valid for production
	if (hiPri == invalidPri)
		return false;

	if (TheGlobalData->m_debugAI) {
		TheScriptEngine->AppendDebugMessage("**AI** Selecting team to build", false);
	}

	//
	// EA's version stopped here: take the highest static productionPriority and flip a coin among
	// the ties.  Not one line of it looked at what the enemy was fielding, which is why an AI facing
	// nothing but aircraft would go on building tanks.
	//
	// So the static priority is the base of a score rather than the whole of it, and two situational
	// terms ride on top:
	//
	//   - how well the team answers what this AI can *see* the enemy fielding (B1), scaled by the
	//     rung's counterCompositionWeight - zero on Easy, which is EA's behaviour exactly, half
	//     on Normal, and one on Hard;
	//   - what this AI is trying to do (D8): an aggressive one leans towards attack teams, a
	//     defensive one towards the teams the data flags as base or perimeter defence.  A
	//     preference, not a bonus - both spend the same money.
	//
	// The coin flip stays, for the ties that remain. Determinism: GameLogicRandomValue, as before.
	//
	const AIDifficultyProfile *profile = getSkillProfile();
	AIEnemyComposition enemy;
	std::vector<AIVisibleEnemy> army;
	const Bool skirmish = runsOwnAI();		// a Challenge or mission opponent picks by priority alone, as EA's did
	if (skirmish && profile->m_counterCompositionWeight > 0.0f)
		computeEnemyComposition( &enemy, &army );

	// what a perfect counter, and the role preference, are worth in units of production priority
	const Real COUNTER_SPAN = 20.0f;
	const Real ROLE_SPAN = 10.0f;

	// AnswerArmy: the one kind of unit that makes up a third or more of the army it remembers, which a
	// team that it hard-counters should not be built into.  Crusaders went on coming off the line into
	// ten Overlords because the rest of the score still liked them
	const Real DOMINANT_SHARE = 0.3f;
	const Real HARD_COUNTERED = 0.25f;
	std::vector<AIVisibleEnemy> dominant;
	if( skirmish && profile->m_answerArmy && enemy.m_totalThreat > 0.0f )
	{
		size_t top = 0;
		for( size_t k = 1; k < army.size(); ++k )
			if( army[ k ].m_weight > army[ top ].m_weight )
				top = k;
		if( !army.empty() && army[ top ].m_weight >= DOMINANT_SHARE * enemy.m_totalThreat )
			dominant.push_back( army[ top ] );
	}

	// the personality's taste for what a team is made of; an Air one's for aircraft shrinks with the
	// share of the enemy's known guns that can shoot up
	const Bool persona = skirmish && m_persona != AIPERSONA_NORMAL;
	const AIPersonaWeights taste = persona ? personaTaste() : aiPersonaWeights( AIPERSONA_NORMAL );
	// an Air, Infantry or Armor AI under its rung's share of its kind built skips the scripts' teams
	// short of that kind (not a Sneaky one: a side has one to three units that go invisible, two of them
	// heroes, and skipping for them left it no army at all): the taste on the score moved nothing, and every soldier they trained
	// thinned an Air AI's share, every tank an Infantry AI's
	const Bool kindPersona = persona && ( m_persona == AIPERSONA_AIR || m_persona == AIPERSONA_INFANTRY || m_persona == AIPERSONA_ARMOR );
	const Int favoured = kindPersona ? personaShareKind() : -1;
	Real favouredShare = 0.0f;
	// ... while the bank is under PERSONA_SKIP_BANK: past it what it skips is money left lying, and an
	// Infantry AI spent two thirds of what a Normal one did on the same seat
	const Bool skipOthers = favoured >= 0 && (Int)m_player->getMoney()->countMoney() <= PERSONA_SKIP_BANK[ m_skillLevel ] &&
		( favouredShare = builtKindShare( m_player, favoured ) ) < PERSONA_SHARE_TARGET[ favoured ][ m_skillLevel ];
	Int skipped = 0;

	Player::PlayerTeamList candidateList;
	Int count = 0;
	Real bestScore = 0.0f;
	Bool haveBest = FALSE;
	for (t = candidateList1.begin(); t != candidateList1.end(); ++t)
	{
		const TeamTemplateInfo *info = (*t)->getTemplateInfo();
		const Bool isDefenceTeam = info->m_isBaseDefense || info->m_isPerimeterDefense;

		Real score = INT_TO_REAL( info->m_productionPriority );
		if( skirmish )
		{
			score += profile->m_counterCompositionWeight * COUNTER_SPAN *
							 aiCounterScore( enemy, teamCapability( *t, m_player, army ) );
			if( (m_role == AIROLE_DEFENSIVE) == (isDefenceTeam != FALSE) )
				score += ROLE_SPAN;
		}
		if( persona )
		{
			Real air, infantry, vehicle, stealth;
			teamKindShares( *t, m_player, &air, &infantry, &vehicle, &stealth );
			// a GLA Air AI's rockets and anti-air are vehicles to a team's cost
			const Real teamShare = favoured == SHARE_AIR ? air : favoured == SHARE_INFANTRY ? infantry : favoured == SHARE_STEALTH ? stealth : vehicle;
			// the base guards too: a China Air AI's were half its infantry, and the emergency trainer
			// (defendHome) still answers anything that walks in
			if( skipOthers && teamShare < PERSONA_TEAM_KIND_SHARE )
			{
				++skipped;
				continue;
			}
			score += aiPersonaTeamBias( taste, isDefenceTeam, air, infantry, vehicle, stealth );
		}
		if( !dominant.empty() && !isDefenceTeam && teamCapability( *t, m_player, dominant ).m_answer < HARD_COUNTERED )
			score -= COUNTER_SPAN;

		if( !haveBest || score > bestScore )
		{
			bestScore = score;
			haveBest = TRUE;
			candidateList.clear();
			count = 0;
		}
		if( score >= bestScore )
		{
			candidateList.push_back( (*t) );
			count++;
		}
	}

	if( skipped > 0 )
		DEBUG_LOG(("AI PERSONA frame %d player %d skips %d teams short of its kind, %.2f of what it built is, %d teams left\n",
			TheGameLogic->getFrame(), m_player->getPlayerIndex(), skipped, favouredShare, count));
	if( skipOthers && count == 0 )
		return false;

	// pick a random team from the hi-priority set
	Int which = GameLogicRandomValue( 0, count-1 );

	TeamPrototype *teamProto = NULL;
	Int i = 0;
	for (t = candidateList.begin(); t != candidateList.end(); ++t)
	{
		if (i == which)
		{
			teamProto = (*t);
			break;
		}

		i++;
	}
	if (teamProto) {
		if (!teamProto->getTemplateInfo()->m_hasHomeLocation && !isSkirmishAI()) {
			AsciiString teamStr = "Error : team '";
			teamStr.concat(teamProto->getName());
			teamStr.concat("' has no Home Position (or Origin).");
			TheScriptEngine->AppendDebugMessage(teamStr, false);
		}
		// one line per decision, so a batch can show what the AI answered with and not just who won
		DEBUG_LOG(("AI COUNTER frame %d player %d builds '%s' score %.2f counter %.2f against %d visible kinds, %.0f threat\n",
			TheGameLogic->getFrame(), m_player->getPlayerIndex(), teamProto->getName().str(), bestScore,
			aiCounterScore( enemy, teamCapability( teamProto, m_player, army ) ), (Int)army.size(), enemy.m_totalThreat));

		// Build it at low priority, as we have selected it automagically.
		buildSpecificAITeam(teamProto, false);
		m_readyToBuildTeam = false;
		m_teamTimer = computeTeamDelay();
		return true;
	}
	return false;
}

// ------------------------------------------------------------------------------------------------
/** Build a specific team.  If priorityBuild, put at front of queue with priority set. */
// ------------------------------------------------------------------------------------------------
void AIPlayer::buildSpecificAIBuilding(const AsciiString &thingName)
{
	//
	AsciiString teamStr = "Error : Solo ai doesn't support BuildSpecificBuilding. '";
	teamStr.concat(thingName);
	teamStr.concat("' not built.");
	TheScriptEngine->AppendDebugMessage(teamStr, false);
}

// ------------------------------------------------------------------------------------------------
/** Build an upgrade. */
// ------------------------------------------------------------------------------------------------
void AIPlayer::buildUpgrade(const AsciiString &upgrade)
{
	const UpgradeTemplate *curUpgrade = TheUpgradeCenter->findUpgrade(upgrade);
	if (curUpgrade==NULL) {
		AsciiString msg = "Upgrade ";
		msg.concat(upgrade);
		msg.concat(" does not exist.  Ignoring request.");
		TheScriptEngine->AppendDebugMessage( msg, false);
		return;
	}
 	if (curUpgrade->getUpgradeType()==UPGRADE_TYPE_OBJECT) {
		AsciiString msg = "Player build upgrade: Upgrade ";
		msg.concat(upgrade);
		msg.concat(" is an object, not a player upgrade.  Ignoring request.");
		TheScriptEngine->AppendDebugMessage( msg, false);
		return;
	}
	// See if it is in progress.
	if (m_player->hasUpgradeInProduction(curUpgrade)) {
		AsciiString msg = TheNameKeyGenerator->keyToName(m_player->getPlayerNameKey());
		msg.concat(" already has upgrade ");
		msg.concat(upgrade);
		msg.concat(" queued.  Ignoring request.");
		TheScriptEngine->AppendDebugMessage( msg, false);
		return;
	}
	// See if it is in progress.
	if (m_player->hasUpgradeComplete(curUpgrade)) {
		AsciiString msg = TheNameKeyGenerator->keyToName(m_player->getPlayerNameKey());
		msg.concat(" already has upgrade ");
		msg.concat(upgrade);
		msg.concat(" completed.  Ignoring request.");
		TheScriptEngine->AppendDebugMessage( msg, false);
		return;
	}


	// No money.
	if( TheUpgradeCenter->canAffordUpgrade( m_player, curUpgrade ) == FALSE ) {
		AsciiString msg = TheNameKeyGenerator->keyToName(m_player->getPlayerNameKey());
		msg.concat(" lacks money to build upgrade ");
		msg.concat(upgrade);
		msg.concat(" at this time.  Ignoring request.");
		TheScriptEngine->AppendDebugMessage( msg, false);
		return;
	}
	// Find a production queue.
	for( const BuildListInfo *info = m_player->getBuildList(); info; info = info->getNext() )
	{
		Object *factory = TheGameLogic->findObjectByID( info->getObjectID() );
		if( factory )
		{
			if( factory->getStatusBits().test( OBJECT_STATUS_UNDER_CONSTRUCTION ) )
				continue;
			if( factory->getStatusBits().test( OBJECT_STATUS_SOLD ) )
				continue;
			Bool canUpgradeHere = false;
			const CommandSet *commandSet = TheCommandStore->findCommandSet( factory->getCommandSetString() );
			if( commandSet == NULL) continue;
			for( Int j = 0; j < MAX_COMMANDS_PER_SET; j++ )
			{
				//Get the command button.
				const CommandButton *commandButton = commandSet->getCommandButton(j);
				if (commandButton==NULL) continue;
				if (commandButton->getName().isEmpty() )	continue;	
				if (commandButton->getUpgradeTemplate() == NULL )	continue;	
 				if (commandButton->getUpgradeTemplate()->getUpgradeName() == curUpgrade->getUpgradeName()) {
					canUpgradeHere = true;
				}
			}
			if (!canUpgradeHere) continue;
			ProductionUpdateInterface *pu = factory->getProductionUpdateInterface();
			// If it doesn't produce, continue.
			if (!pu) continue;
			// Try to queue it.
			if (buyUpgrade(pu, curUpgrade)) {
				AsciiString msg = TheNameKeyGenerator->keyToName(m_player->getPlayerNameKey());
				msg.concat(" queues ");
				msg.concat(curUpgrade->getUpgradeName());
				msg.concat(" at ");
				msg.concat(factory->getTemplate()->getName());
				TheScriptEngine->AppendDebugMessage( msg, false);
				return;
			}
		}  // end if
	}  // end for

	AsciiString msg = TheNameKeyGenerator->keyToName(m_player->getPlayerNameKey());
	msg.concat(" lacks factory to build upgrade ");
	msg.concat(upgrade);
	msg.concat(" at this time.  Ignoring request.");
	TheScriptEngine->AppendDebugMessage( msg, false);
	return;
}

/** Defenses buildBySupplies puts beside one warehouse. */
static const Int MAX_DEFENSES_PER_WAREHOUSE = 2;

/** The furthest dock from its base center a computer with a base expands to, in world units. */
static const Real EXPANSION_MAX_DISTANCE = 2000.0f;

struct OwnedNearCount
{
	const ThingTemplate *tmpl;
	Coord3D at;
	Real radiusSqr;
	Int count;
};

static void countOwnedNearObject( Object *obj, void *userData )
{
	OwnedNearCount *search = (OwnedNearCount *)userData;
	if( !obj->isEffectivelyDead() && obj->getTemplate()->isEquivalentTo( search->tmpl ) &&
			sqr( obj->getPosition()->x - search->at.x ) + sqr( obj->getPosition()->y - search->at.y ) <= search->radiusSqr )
		++search->count;
}

/** This player's copies of a building within radius of a point: standing, going up, or asked for
	* and waiting for a dozer. */
static Int countOwnedNear( Player *player, const ThingTemplate *tmpl, const Coord3D *at, Real radius )
{
	OwnedNearCount search;
	search.tmpl = tmpl;
	search.at = *at;
	search.radiusSqr = radius * radius;
	search.count = 0;
	player->iterateObjects( countOwnedNearObject, &search );
	for( BuildListInfo *info = player->getBuildList(); info; info = info->getNext() )
	{
		if( info->isPriorityBuild() && info->isBuildable() && info->getObjectID() == INVALID_ID &&
				info->getTemplateName() == tmpl->getName() &&
				sqr( info->getLocation()->x - at->x ) + sqr( info->getLocation()->y - at->y ) <= search.radiusSqr )
			++search.count;
	}
	return search.count;
}

/** The nearest standing structure of another side, ally or enemy, beside a dock: whoever built it
	* has the pile already, or sits on it.  Civilian buildings are neutral and do not count. */
static Object *otherSideStructureNear( Player *player, const Object *dock )
{
	PartitionFilterAcceptByKindOf fStructure(MAKE_KINDOF_MASK(KINDOF_STRUCTURE), KINDOFMASK_NONE);
	PartitionFilterPlayer fNotMine(player, false);
	PartitionFilterPlayerAffiliation fSide(player, ALLOW_ALLIES | ALLOW_ENEMIES, true);
	PartitionFilterAlive fAlive;
	PartitionFilterOnMap fOnMap;
	PartitionFilter *filters[] = { &fStructure, &fNotMine, &fSide, &fAlive, &fOnMap, 0 };
	return ThePartitionManager->getClosestObject( dock->getPosition(),
		SUPPLY_CENTER_CLOSE_DIST + dock->getGeometryInfo().getBoundingCircleRadius(), FROM_BOUNDINGSPHERE_2D, filters );
}

// ------------------------------------------------------------------------------------------------
/** Build a supply center near a supply source with minimumCash or more resources. */
// ------------------------------------------------------------------------------------------------
void AIPlayer::buildBySupplies(Int minimumCash, const AsciiString& thingName, Bool holdableOnly)
{
	Object *bestSupplyWarehouse = findSupplyCenter(minimumCash, holdableOnly);
	const ThingTemplate* tTemplate = TheThingFactory->findTemplate(thingName);
	// tTemplate was dereferenced here and only null-checked a few lines further down, so a bad
	// building name in the "Build supply center" script action faulted.
	if (tTemplate && !tTemplate->isKindOf(KINDOF_CASH_GENERATOR)) {
		// Build by the current warehouse.
		Object *curWarehouse = TheGameLogic->findObjectByID(m_curWarehouseID);
		if (curWarehouse) {
			bestSupplyWarehouse = curWarehouse;
		}
		// Every call aims at the same spot beside the warehouse and the wiggle below takes the nearest
		// free ground to it, so each defense the scripts asked for went up against the last one: a
		// Hard China stood nine Gattling cannons shoulder to shoulder at one pile.  EA's AI builds
		// every defense its script asks for, as the mission was balanced against.
		if (bestSupplyWarehouse && runsOwnAI() && countOwnedNear(m_player, tTemplate, bestSupplyWarehouse->getPosition(),
				SUPPLY_CENTER_CLOSE_DIST + bestSupplyWarehouse->getGeometryInfo().getBoundingCircleRadius()) >= MAX_DEFENSES_PER_WAREHOUSE) {
			return;
		}
	}


	if (bestSupplyWarehouse && tTemplate) {
		Coord3D location;
		location = *bestSupplyWarehouse->getPosition();
		// offset back towards the base.
		Coord2D offset;
		offset.x = location.x - m_baseCenter.x;
		offset.y = location.y - m_baseCenter.y;
		offset.normalize();
		Real radius = 3*PATHFIND_CELL_SIZE_F;
		if (!tTemplate->isKindOf(KINDOF_CASH_GENERATOR)) {
			// It's probably a defensive structure - build towards the enemy.
			Region2D bounds;
			Player *skirmishEnemy = TheScriptEngine->getSkirmishEnemyPlayer();	// can be NULL
			if (skirmishEnemy)
			{
				getPlayerStructureBounds(&bounds, skirmishEnemy->getPlayerIndex(), FALSE, m_player->getPlayerIndex());
				offset.x = location.x - (bounds.lo.x+bounds.hi.x)*0.5f;
				offset.y = location.y - (bounds.lo.y+bounds.hi.y)*0.5f;
				offset.normalize();
			}
			radius = bestSupplyWarehouse->getGeometryInfo().getBoundingCircleRadius();
		}
		location.x -= offset.x*radius;
		location.y -= offset.y*radius;
		Real angle = tTemplate->getPlacementViewAngle();

 		// validate the the position to build at is valid
		Bool valid=false;
		Bool blocked=false;
		Coord3D newPos = location;
		if( TheBuildAssistant->isLocationLegalToBuild( &location, tTemplate, angle,
																									 BuildAssistant::NO_OBJECT_OVERLAP,
																									 NULL, m_player ) != LBC_OK ) {
			blocked = true;
			// Warn. 
			const Coord3D *warehouseLocation = bestSupplyWarehouse->getPosition();
			AsciiString debugMessage;
			debugMessage.format(" %s - buildBySupplies unable to place near dock at (%.2f,%.2f).  Attempting to adjust position.",
													tTemplate->getName().str(),
													warehouseLocation->x,
													warehouseLocation->y
													);
			TheScriptEngine->AppendDebugMessage(debugMessage, false);
			if( TheGlobalData->m_debugSupplyCenterPlacement )
				DEBUG_LOG(("%s", debugMessage.str()));
			// try to fix.
			Real posOffset;
			// Wiggle it a little :)
			for (posOffset = 0; posOffset<2*SUPPLY_CENTER_CLOSE_DIST; posOffset += 2*PATHFIND_CELL_SIZE_F) {
				Real offset = posOffset/2;
				Real xPos, yPos;
				yPos = location.y-offset;
				for (xPos = location.x-offset; xPos <= location.x+offset; xPos+=PATHFIND_CELL_SIZE_F) {
					newPos.x = xPos;
					newPos.y = yPos;
					valid = TheBuildAssistant->isLocationLegalToBuild( &newPos, tTemplate, angle,
																							 BuildAssistant::CLEAR_PATH |
																							 BuildAssistant::TERRAIN_RESTRICTIONS |
																							 BuildAssistant::NO_OBJECT_OVERLAP,
																							 NULL, m_player ) == LBC_OK;
					if (valid) break;
					if( TheGlobalData->m_debugSupplyCenterPlacement )
						DEBUG_LOG(("buildBySupplies -- Fail at (%.2f,%.2f)\n", newPos.x, newPos.y));
					newPos.y = yPos+posOffset;
					valid = TheBuildAssistant->isLocationLegalToBuild( &newPos, tTemplate, angle,
																							 BuildAssistant::CLEAR_PATH |
																							 BuildAssistant::TERRAIN_RESTRICTIONS |
																							 BuildAssistant::NO_OBJECT_OVERLAP,
																							 NULL, m_player ) == LBC_OK;
					if (valid) break;
					if( TheGlobalData->m_debugSupplyCenterPlacement )
						DEBUG_LOG(("buildBySupplies -- Fail at (%.2f,%.2f)\n", newPos.x, newPos.y));
				}
				if (valid) break;
				xPos = location.x-offset;
				for (yPos = location.y-offset; yPos <= location.y+offset; yPos+=PATHFIND_CELL_SIZE_F) {
					newPos.x = xPos;
					newPos.y = yPos;
					valid = TheBuildAssistant->isLocationLegalToBuild( &newPos, tTemplate, angle,
																							 BuildAssistant::CLEAR_PATH |
																							 BuildAssistant::TERRAIN_RESTRICTIONS |
																							 BuildAssistant::NO_OBJECT_OVERLAP,
																							 NULL, m_player ) == LBC_OK;
					if (valid) break;
					if( TheGlobalData->m_debugSupplyCenterPlacement )
						DEBUG_LOG(("buildBySupplies -- Fail at (%.2f,%.2f)\n", newPos.x, newPos.y));
					newPos.x = xPos+posOffset;
					valid = TheBuildAssistant->isLocationLegalToBuild( &newPos, tTemplate, angle,
																							 BuildAssistant::CLEAR_PATH |
																							 BuildAssistant::TERRAIN_RESTRICTIONS |
																							 BuildAssistant::NO_OBJECT_OVERLAP,
																							 NULL, m_player ) == LBC_OK;
					if (valid) break;
					if( TheGlobalData->m_debugSupplyCenterPlacement )
						DEBUG_LOG(("buildBySupplies -- Fail at (%.2f,%.2f)\n", newPos.x, newPos.y));
				}
				if (valid) break;
			}
		}
		if (valid) 
		{
			if( TheGlobalData->m_debugSupplyCenterPlacement )
				DEBUG_LOG(("buildAISupplyCenter -- SUCCESS at (%.2f,%.2f)\n", newPos.x, newPos.y));
			location = newPos;
		}
		TheTerrainVisual->removeAllBibs();	// isLocationLegalToBuild adds bib feedback, turn it off.  jba.
		if (holdableOnly && blocked && !valid) {
			// No room beside the dock.  A script's request still goes down on the taken spot and the
			// dozer's flood looks for room; the AI's own expansion is not built.
			return;
		}
		location.z = 0; // All build list locations are ground relative.
		const Object *claimant = otherSideStructureNear( m_player, bestSupplyWarehouse );
		DEBUG_LOG(("AI EXPAND frame %d player %d builds '%s' at (%.0f,%.0f) by dock %d, %d in the bank, %.0f from a base of radius %.0f%s, held by player %d\n",
			TheGameLogic->getFrame(), m_player->getPlayerIndex(), thingName.str(), location.x, location.y, bestSupplyWarehouse->getID(),
			m_player->getMoney()->countMoney(), sqrt( sqr( location.x - m_baseCenter.x ) + sqr( location.y - m_baseCenter.y ) ), m_baseRadius,
			holdableOnly ? ", its own choice" : "", claimant ? claimant->getControllingPlayer()->getPlayerIndex() : -1));
		m_player->addToPriorityBuildList(thingName, &location, angle);
		m_curWarehouseID = bestSupplyWarehouse->getID();
	}
}

// ------------------------------------------------------------------------------------------------
/** Calculates the closest construction zone location based on a template. */
// ------------------------------------------------------------------------------------------------
Bool AIPlayer::calcClosestConstructionZoneLocation( const ThingTemplate *constructTemplate, Coord3D *location )
{
	if( !constructTemplate || !location )
	{
		return FALSE;
	}

  Bool success = FALSE;

	// offset back towards the base.
	Coord2D offset;
	offset.x = location->x - m_baseCenter.x;
	offset.y = location->y - m_baseCenter.y;
	offset.normalize();

	Real angle = constructTemplate->getPlacementViewAngle();

 	// validate the the position to build at is valid
	Bool valid=false;
	Coord3D newPos = *location;
	if( TheBuildAssistant->isLocationLegalToBuild( location, constructTemplate, angle, BuildAssistant::NO_OBJECT_OVERLAP, NULL, m_player ) != LBC_OK )
	{
		// Warn. 
		AsciiString bldgName = constructTemplate->getName();
		bldgName.concat(" - calcClosestConstructionZoneLocation unable to place.  Attempting to adjust position.");
		TheScriptEngine->AppendDebugMessage( bldgName, false );
		// try to fix.
		Real posOffset;
		// Wiggle it a little :)
		for( posOffset = 0; posOffset < 2 * SUPPLY_CENTER_CLOSE_DIST; posOffset += 2 * PATHFIND_CELL_SIZE_F ) 
		{
			Real offset = posOffset / 2;
			Real xPos, yPos;
			yPos = location->y - offset;
			for( xPos = location->x - offset; xPos <= location->x + offset; xPos += PATHFIND_CELL_SIZE_F ) 
			{
				newPos.x = xPos;
				newPos.y = yPos;
				valid = TheBuildAssistant->isLocationLegalToBuild( &newPos, constructTemplate, angle,
																						 BuildAssistant::CLEAR_PATH |
																						 BuildAssistant::TERRAIN_RESTRICTIONS |
																						 BuildAssistant::NO_OBJECT_OVERLAP,
																						 NULL, m_player ) == LBC_OK;
				if( valid ) 
					break;
				
				newPos.y = yPos + posOffset;
				valid = TheBuildAssistant->isLocationLegalToBuild( &newPos, constructTemplate, angle,
																						 BuildAssistant::CLEAR_PATH |
																						 BuildAssistant::TERRAIN_RESTRICTIONS |
																						 BuildAssistant::NO_OBJECT_OVERLAP,
																						 NULL, m_player ) == LBC_OK;
			}
			
			if( valid ) 
				break;

			xPos = location->x - offset;
			for( yPos = location->y - offset; yPos <= location->y + offset; yPos += PATHFIND_CELL_SIZE_F ) 
			{
				newPos.x = xPos;
				newPos.y = yPos;
				valid = TheBuildAssistant->isLocationLegalToBuild( &newPos, constructTemplate, angle,
																						 BuildAssistant::CLEAR_PATH |
																						 BuildAssistant::TERRAIN_RESTRICTIONS |
																						 BuildAssistant::NO_OBJECT_OVERLAP,
																						 NULL, m_player ) == LBC_OK;
				if( valid ) 
					break;

				newPos.x = xPos + posOffset;
				valid = TheBuildAssistant->isLocationLegalToBuild( &newPos, constructTemplate, angle,
																						 BuildAssistant::CLEAR_PATH |
																						 BuildAssistant::TERRAIN_RESTRICTIONS |
																						 BuildAssistant::NO_OBJECT_OVERLAP,
																						 NULL, m_player ) == LBC_OK;
			}

			if( valid ) 
				break;
		}
	}
	if( valid )
	{
		//We succeeded in calculating the best position.
		location->set( &newPos );
    success = TRUE;
	}
	else
	{
		//We failed to calculate a position, so zero out the position.
		location->zero();
    success = FALSE;
	}

	TheTerrainVisual->removeAllBibs();	// isLocationLegalToBuild adds bib feedback, turn it off.  jba.

  return success;

}

// ------------------------------------------------------------------------------------------------
/** Build a specific building nearest specified team. */
// ------------------------------------------------------------------------------------------------
void AIPlayer::buildSpecificBuildingNearestTeam( const AsciiString &thingName, const Team *team )
{
	const ThingTemplate *tTemplate = TheThingFactory->findTemplate( thingName );

	if( !tTemplate || !team )
	{
		//Assert will already happen in the failed findTemplate call.
		return;
	}

	//From the team's location, find the most valid build location.
	const Coord3D *location = team->getEstimateTeamPosition();
	if( !location )
	{
		return;
	}

	// offset back towards the base.
	Coord2D offset;
	offset.x = location->x - m_baseCenter.x;
	offset.y = location->y - m_baseCenter.y;
	offset.normalize();

	Real angle = tTemplate->getPlacementViewAngle();

 	// validate the the position to build at is valid
	Bool valid=false;
	Coord3D newPos = *location;
	if( TheBuildAssistant->isLocationLegalToBuild( location, tTemplate, angle, BuildAssistant::NO_OBJECT_OVERLAP, NULL, m_player ) != LBC_OK )
	{
		// Warn. 
		AsciiString bldgName = tTemplate->getName();
		bldgName.concat(" - buildSpecificBuildingNearestTeam unable to place.  Attempting to adjust position.");
		TheScriptEngine->AppendDebugMessage( bldgName, false );
		// try to fix.
		Real posOffset;
		// Wiggle it a little :)
		for( posOffset = 0; posOffset < 2 * SUPPLY_CENTER_CLOSE_DIST; posOffset += 2 * PATHFIND_CELL_SIZE_F ) 
		{
			Real offset = posOffset / 2;
			Real xPos, yPos;
			yPos = location->y-offset;
			for( xPos = location->x - offset; xPos <= location->x + offset; xPos += PATHFIND_CELL_SIZE_F ) 
			{
				newPos.x = xPos;
				newPos.y = yPos;
				valid = TheBuildAssistant->isLocationLegalToBuild( &newPos, tTemplate, angle,
																						 BuildAssistant::CLEAR_PATH |
																						 BuildAssistant::TERRAIN_RESTRICTIONS |
																						 BuildAssistant::NO_OBJECT_OVERLAP,
																						 NULL, m_player ) == LBC_OK;
				if( valid ) 
					break;
				
				newPos.y = yPos + posOffset;
				valid = TheBuildAssistant->isLocationLegalToBuild( &newPos, tTemplate, angle,
																						 BuildAssistant::CLEAR_PATH |
																						 BuildAssistant::TERRAIN_RESTRICTIONS |
																						 BuildAssistant::NO_OBJECT_OVERLAP,
																						 NULL, m_player ) == LBC_OK;
			}
			
			if( valid ) 
				break;

			xPos = location->x - offset;
			for( yPos = location->y - offset; yPos <= location->y + offset; yPos += PATHFIND_CELL_SIZE_F ) 
			{
				newPos.x = xPos;
				newPos.y = yPos;
				valid = TheBuildAssistant->isLocationLegalToBuild( &newPos, tTemplate, angle,
																						 BuildAssistant::CLEAR_PATH |
																						 BuildAssistant::TERRAIN_RESTRICTIONS |
																						 BuildAssistant::NO_OBJECT_OVERLAP,
																						 NULL, m_player ) == LBC_OK;
				if( valid ) 
					break;

				newPos.x = xPos + posOffset;
				valid = TheBuildAssistant->isLocationLegalToBuild( &newPos, tTemplate, angle,
																						 BuildAssistant::CLEAR_PATH |
																						 BuildAssistant::TERRAIN_RESTRICTIONS |
																						 BuildAssistant::NO_OBJECT_OVERLAP,
																						 NULL, m_player ) == LBC_OK;
			}

			if( valid ) 
				break;
		}
	}
	if( valid ) 
	{
		newPos.z = 0; // All build list locations are ground relative.
		m_player->addToPriorityBuildList( thingName, &newPos, angle );
	}

	TheTerrainVisual->removeAllBibs();	// isLocationLegalToBuild adds bib feedback, turn it off.  jba.

}

// ------------------------------------------------------------------------------------------------
/** Find a supply center we haven't built a supply depot near yet. */
// ------------------------------------------------------------------------------------------------
Object *AIPlayer::findSupplyCenter(Int minimumCash, Bool holdableOnly)
{
	Object *bestSupplyWarehouse = NULL;
	Real bestDistSqr = 0;
	Object *bestSafeWarehouse = NULL;
	Real bestSafeDistSqr = 0;
	Object *obj;
	Coord3D enemyCenter;
	enemyCenter.zero();
	Region2D bounds;
	const Bool ownAI = runsOwnAI();
	Player *enemy = getAiEnemy();
	if (enemy) {
		getPlayerStructureBounds(&bounds, enemy->getPlayerIndex(), FALSE, m_player->getPlayerIndex());
		enemyCenter.set( (bounds.lo.x+bounds.hi.x)*0.5f, (bounds.lo.y+bounds.hi.y)*0.5f, 0);
	}

	do {
		for( obj = TheGameLogic->getFirstObject(); obj; obj = obj->getNextObject() )
		{
			if (!obj->isKindOf(KINDOF_STRUCTURE)) continue;
			if (!obj->isKindOf(KINDOF_SUPPLY_SOURCE)) continue;
			// ... and one this AI has laid eyes on. This loop walks every object on the map, so
			// without the test the AI expands to docks in shroud it has never been near.  EA's AI knew
			// every dock, and a mission's script picks from all of them.
			if (!observerKnowsAbout(obj, knowledgeObserver())) continue;
			static const NameKeyType key_warehouseUpdate = NAMEKEY("SupplyWarehouseDockUpdate");
			SupplyWarehouseDockUpdate *warehouseModule = (SupplyWarehouseDockUpdate*)obj->findUpdateModule( key_warehouseUpdate );
			if( warehouseModule )	{	 
				Int availableCash = warehouseModule->getBoxesStored()*TheGlobalData->m_baseValuePerSupplyBox;
				if (availableCash<minimumCash) continue;
				if( m_player->getRelationship(obj->getTeam()) == ENEMIES ) {
					continue;
				}

				// Make sure we don't have a supply center near it.
				Coord3D center = *obj->getPosition();
				Real radius = SUPPLY_CENTER_CLOSE_DIST + obj->getGeometryInfo().getBoundingCircleRadius();

				PartitionFilterAcceptByKindOf f1(MAKE_KINDOF_MASK(KINDOF_CASH_GENERATOR), KINDOFMASK_NONE);
				PartitionFilterPlayer f2(m_player, true);	// Only find your own units.
				PartitionFilterOnMap filterMapStatus;
				PartitionFilter *filters[] = { &f1, &f2, &filterMapStatus, 0 };

				Object *supplyCenter = ThePartitionManager->getClosestObject(&center, radius, FROM_BOUNDINGSPHERE_2D, filters);
				if (supplyCenter) {
					// We already have a supply center.
					continue;
				}
				// Another side's pile: its supply center works it, or its base stands round it.  Only
				// our own center was looked for (an ally's too, for a supportive AI), so a dock inside
				// an ally's base or a second enemy's read as free and the AI put a center, then guns
				// and factories, against theirs.  EA's AI takes the dock its script asks for, as it did.
				if (ownAI && otherSideStructureNear(m_player, obj)) {
					continue;
				}
				// a site given up at this dock lately (watchSite): not straight back into the same trap
				if (ownAI && aiSpotCooling(m_cooledSpots, obj->getPosition(), AI_COOLED_DOCK_RADIUS, TheGameLogic->getFrame())) {
					continue;
				}

				Real dx, dy;
				dx = obj->getPosition()->x - m_baseCenter.x;
				dy = obj->getPosition()->y - m_baseCenter.y;
				Real distSqr = dx*dx + dy*dy;
				// An AI expanding on its own takes a dock it can hold: within EXPANSION_MAX_DISTANCE of
				// home, nearer its own base than any enemy's, and with no enemy standing at it.  The test
				// below only knows the one enemy this AI is attacking, and lets through a dock a fifth
				// nearer him than us.  A script's choice of dock is left as it was.
				if (holdableOnly && (distSqr > sqr(EXPANSION_MAX_DISTANCE) || !isOurSideOfMap(obj->getPosition()) ||
						!isLocationSafe(obj->getPosition(), obj->getTemplate()))) {
					continue;
				}
				if (enemy) {
					// make sure this isn't closer to our enemy than us.
					dx = obj->getPosition()->x - enemyCenter.x;
					dy = obj->getPosition()->y - enemyCenter.y;
					if (distSqr*0.4>(dx*dx+dy*dy)*0.6f) {
						// closer than 60/40 to enemy than to us, probably not a good candidate for expansion.
						continue;
					}
				}

				if (bestSupplyWarehouse==NULL) {
					bestSupplyWarehouse = obj;
					bestDistSqr = distSqr;
				} else if (bestDistSqr>distSqr) {
					bestSupplyWarehouse = obj;
					bestDistSqr = distSqr;
				}
				// The scripts ask "is the supply source safe" of this same pick and wait while it is
				// not, putting guns down beside it: one enemy tank parked by the nearest pile held a
				// Hard AI's expansion for ten minutes with a free pile one valley over.  A safe dock
				// is preferred, the nearest of them, and the unsafe nearest only when none is safe.
				// A mission's script picked its piles by hand and keeps EA's nearest.
				if (!holdableOnly && ownAI && (bestSafeWarehouse==NULL || bestSafeDistSqr>distSqr) &&
						isLocationSafe(obj->getPosition(), obj->getTemplate())) {
					bestSafeWarehouse = obj;
					bestSafeDistSqr = distSqr;
				}
			}
		}
		if (bestSafeWarehouse) return bestSafeWarehouse;
		if (bestSupplyWarehouse) break;
		minimumCash /= 2;
 	} while (minimumCash > 100);

	return bestSupplyWarehouse;
}

// ------------------------------------------------------------------------------------------------
/**  Build a base defense. */
// ------------------------------------------------------------------------------------------------
void AIPlayer::buildAIBaseDefense(Bool flank)
{
	//
	AsciiString teamStr = "Error : Solo ai doesn't support buildAIBaseDefense. '";
	TheScriptEngine->AppendDebugMessage(teamStr, false);
}

// ------------------------------------------------------------------------------------------------
/** Build a base defense. */
// ------------------------------------------------------------------------------------------------
void AIPlayer::buildAIBaseDefenseStructure(const AsciiString &thingName, Bool flank)
{
	//
	AsciiString teamStr = "Error : Solo ai doesn't support buildAIBaseDefenseStructure. '";
	TheScriptEngine->AppendDebugMessage(teamStr, false);
}

// ------------------------------------------------------------------------------------------------
/** Repair a bridge or other structure. */
// ------------------------------------------------------------------------------------------------
void AIPlayer::repairStructure(ObjectID structure)
{
	Object *structureObj = TheGameLogic->findObjectByID(structure);
	if (structureObj==NULL) return;
	if (structureObj->getBodyModule()==NULL) return;
	// If the structure is not noticably damaged, don't bother.
	enum BodyDamageType structureState = structureObj->getBodyModule()->getDamageState(); 
	if (structureState==BODY_PRISTINE) {
		return; 
	}
	if (structureObj->isKindOf(KINDOF_BRIDGE)) {
		// Locate the correct post to repair.
	}
	Int i;
	for (i=0; i<m_structuresInQueue; i++) {
		if (m_structuresToRepair[i] == structureObj->getID()) {
			DEBUG_LOG(("info - Bridge already queued for repair.\n"));
			return;
		}
	}
	if (m_structuresInQueue==MAX_STRUCTURES_TO_REPAIR) {
		DEBUG_LOG(("Structure repair queue is full, ignoring repair request. JBA\n"));
		return;
	}
	m_structuresToRepair[m_structuresInQueue] = structureObj->getID();
	m_structuresInQueue++;
}

// ------------------------------------------------------------------------------------------------
/** select a skillset for the player. */
// ------------------------------------------------------------------------------------------------
void AIPlayer::selectSkillset(Int skillset)
{
	DEBUG_ASSERTCRASH(m_skillsetSelector == INVALID_SKILLSET_SELECTION, 
		("Selecting a skill set (%d) after one has already been chosen (%d) means some points have been incorrectly spent.\n", skillset + 1, m_skillsetSelector + 1));

	m_skillsetSelector = skillset;
}

// ------------------------------------------------------------------------------------------------
/** Do per frame work (if any) repairing bridges. */
// ------------------------------------------------------------------------------------------------
void AIPlayer::updateBridgeRepair(void)
{
	if (m_structuresInQueue == 0) return;
	// Check once a second.
	m_bridgeTimer--;
	if (m_bridgeTimer>0) return;
	m_bridgeTimer = LOGICFRAMES_PER_SECOND;
	Object *bridgeObj=NULL;
	while (bridgeObj==NULL && m_structuresInQueue>0) {
		bridgeObj = TheGameLogic->findObjectByID(m_structuresToRepair[0]);
		if (bridgeObj==NULL) {
			Int i;
			for (i=0; i<m_structuresInQueue-1; i++) {
				m_structuresToRepair[i] = m_structuresToRepair[i+1];
			}
			m_structuresInQueue--;
		}
	}
	if (m_structuresInQueue == 0) return;

	// Got a bridge to repair.
	Object *dozer = NULL;
	Coord3D bridgePos = *bridgeObj->getPosition();
	enum BodyDamageType bridgeState = bridgeObj->getBodyModule()->getDamageState(); 
	if (m_repairDozer==INVALID_ID) {
		m_dozerIsRepairing = false;
		// Need a dozer.
		if (m_dozerQueuedForRepair) {
			return; // we're waiting for one.
		}
		dozer = findDozer(&bridgePos);
		if (dozer) {
			m_repairDozer = dozer->getID();
			m_repairDozerOrigin = *dozer->getPosition();
			dozer->getAI()->aiRepair(bridgeObj, CMD_FROM_AI);
			DEBUG_LOG(("Telling dozer to repair\n"));
			m_dozerIsRepairing = true;
			return;
		}
		queueDozer();
		m_dozerQueuedForRepair = true;
		return;
	}

	dozer = TheGameLogic->findObjectByID(m_repairDozer);
	if (dozer==NULL) {
		m_repairDozer=INVALID_ID; // we got killed.
		m_bridgeTimer=0;
		return; // Just try to find a dozer next frame.
	}

	DozerAIInterface* dozerAI = dozer->getAI()->getDozerAIInterface();
	if (dozerAI==NULL) {
		DEBUG_CRASH(("Unexpected - dozer doesn't have dozer interface."));
		return;
	}
	if (m_dozerIsRepairing) {
		if (!dozerAI->isAnyTaskPending())	{
			// should be done repairing.
			if (bridgeState==BODY_PRISTINE) {
				DEBUG_LOG(("Dozer finished repairing structure.\n"));
				// we're done.
				Int i;
				for (i=0; i<m_structuresInQueue-1; i++) {
					m_structuresToRepair[i] = m_structuresToRepair[i+1];
				}
				m_structuresInQueue--;
				m_dozerIsRepairing = false;
				if (m_structuresInQueue==0) {
					// Go home.  
					Coord3D pos = m_baseCenter;
					if (!m_baseCenterSet) {
						pos = m_repairDozerOrigin;
					}
					AIUpdateInterface *ai=dozer->getAI();
					TheAI->pathfinder()->adjustToPossibleDestination(dozer, ai->getLocomotorSet(), &pos);
					dozer->getAI()->aiMoveToPosition(&pos, CMD_FROM_AI);
					return;
				}
			}
		}	else {
			// dozer should be working on the bridge.
			return;
		}
	}	
	dozer->getAI()->aiRepair(bridgeObj, CMD_FROM_AI);
	m_dozerIsRepairing = true;
	DEBUG_LOG(("Telling dozer to repair\n"));
}

// ------------------------------------------------------------------------------------------------
/** Build a specific team.  If priorityBuild, put at front of queue with priority set. */
// ------------------------------------------------------------------------------------------------
void AIPlayer::buildSpecificAITeam( TeamPrototype *teamProto, Bool priorityBuild)
{
	//
	// Create "Team in queue" based on team population
	//
	if (teamProto)
	{	
		if (!m_player->getCanBuildUnits()) {
			AsciiString teamStr = "Can't build team '";
			teamStr.concat(teamProto->getName());
			teamStr.concat("' because build units is disabled.");
			TheScriptEngine->AppendDebugMessage(teamStr, false);
			return;
		}
		if (priorityBuild && teamProto->getIsSingleton()) {
			Team *singletonTeam = TheTeamFactory->findTeam( teamProto->getName() );
			if (singletonTeam && singletonTeam->hasAnyObjects()) {
				AsciiString teamStr = "Unable to build singleton team '";
				teamStr.concat("' because team already exists.");
				TheScriptEngine->AppendDebugMessage(teamStr, false); 
				return;
			}
		}
		// Check & make sure we have factories.
		Bool needMoney;
		if (!isPossibleToBuildTeam(teamProto, false, needMoney)) {
			if (needMoney) {
				// Queue it up anyway.
				AsciiString teamStr = "Note - queueing team '";
				teamStr.concat(teamProto->getName());
				teamStr.concat("' but there is enough money.");
				TheScriptEngine->AppendDebugMessage(teamStr, false);
			} else {	
				// Tech tree doesn't work.
				AsciiString teamStr = "Unable to build team '";
				teamStr.concat(teamProto->getName());
				if (needMoney) {
					teamStr.concat("' - Not enough money.");
				} else {
					teamStr.concat("' because required factories/tech don't exist.");
				}
				TheScriptEngine->AppendDebugMessage(teamStr, false);
				return;
			}
		}
		const TCreateUnitsInfo *unitInfo = &teamProto->getTemplateInfo()->m_unitsInfo[0];
		WorkOrder *orders = NULL;
		Int i;
		// Queue up optional units.
		for( i=0; i<teamProto->getTemplateInfo()->m_numUnitsInfo; i++ )
		{
			const ThingTemplate *thing = TheThingFactory->findTemplate( unitInfo[i].unitThingName );
			if (thing && !sideCanNeverField(m_player, thing))
			{
				int count = unitInfo[i].maxUnits-unitInfo[i].minUnits;
				if (count>0) {
					WorkOrder *order = newInstance(WorkOrder);
					order->m_thing = ownTemplate(m_player, thing);
					order->m_factoryID = INVALID_ID;
					order->m_numRequired = count;
					// prepend to head of list
					order->m_next = orders;
					orders = order;
				}
			}
		}
		// Queue up required units.
		for( i=0; i<teamProto->getTemplateInfo()->m_numUnitsInfo; i++ )
		{
			const ThingTemplate *thing = TheThingFactory->findTemplate( unitInfo[i].unitThingName );
			if (thing && !sideCanNeverField(m_player, thing))
			{
				int count = unitInfo[i].minUnits;
				WorkOrder *order = newInstance(WorkOrder);
				order->m_thing = ownTemplate(m_player, thing);
				order->m_factoryID = INVALID_ID;
				order->m_numRequired = count;
				order->m_required = true;
				// prepend to head of list
				order->m_next = orders;
				orders = order;
			}
		}
		// a share of the attack teams of a side whose guns fire past their own sight takes one of them
		// (AISideInfo Artillery), when a factory is up to make it: the scripts it runs may name none
		const AISideInfo *sideInfo = runsOwnAI() ? TheAI->getAiData()->findSideInfo( m_player ) : NULL;
		const ThingTemplate *gun = NULL;
		if (orders && sideInfo && !sideInfo->m_artillery.empty() && takesArtillery( m_player, teamProto ) &&
				GameLogicRandomValueReal( 0.0f, 1.0f ) < getSkillProfile()->m_artilleryShare)
		{
			gun = TheThingFactory->findTemplate( sideInfo->m_artillery[ GameLogicRandomValue( 0, (Int)sideInfo->m_artillery.size() - 1 ) ] );
			if (findFactory( gun, true ) == NULL)
				gun = NULL;
		}
		if (gun)
		{
			WorkOrder *order = newInstance(WorkOrder);
			order->m_thing = gun;
			order->m_factoryID = INVALID_ID;
			order->m_numRequired = 1;
			order->m_required = true;
			order->m_next = orders;
			orders = order;
			DEBUG_LOG(("AI ARTILLERY frame %d player %d team '%s' takes an added '%s'\n", TheGameLogic->getFrame(),
				m_player->getPlayerIndex(), teamProto->getName().str(), gun->getName().str()));
		}
		if (orders)
		{
			/* We have something to build. */
			TeamInQueue *team = newInstance(TeamInQueue);
			if (priorityBuild) {
				// Put in front of queue.
				prependTo_TeamBuildQueue(team);
				team->m_priorityBuild = true;
			}	else {
				// Put in back of queue.
				reverse_TeamBuildQueue();
				prependTo_TeamBuildQueue(team);
				reverse_TeamBuildQueue();
				team->m_priorityBuild = false;
			}
			team->m_workOrders = orders;
			team->m_frameStarted = TheGameLogic->getFrame();
			// create inactive team to place members into as they are built
			// when team is complete, the team is activated
			team->m_team = TheTeamFactory->createInactiveTeam( teamProto->getName() ); 
			AsciiString teamName = teamProto->getName();
			teamName.concat(" - starting team build.");
			TheScriptEngine->AppendDebugMessage(teamName, false);
			m_teamDelay = 0;
			if (team->m_team->getPrototype()->getTemplateInfo()->m_executeActions) {
				const Script *script = TheScriptEngine->findScriptByName(team->m_team->getPrototype()->getTemplateInfo()->m_productionCondition);
				if (script && script->getAction()) {
					TheScriptEngine->friend_executeAction(script->getAction(), team->m_team);
				}
			}
		} else {
			if (TheGlobalData->m_debugAI) {
				AsciiString teamName = teamProto->getName();
				teamName.concat(" - contains 0 buildable units.");
				TheScriptEngine->AppendDebugMessage(teamName, false);
			}
		}

	}
}

// ------------------------------------------------------------------------------------------------
/** Recruit a specific team, within the specific radius of the home position. */
// ------------------------------------------------------------------------------------------------
void AIPlayer::recruitSpecificAITeam(TeamPrototype *teamProto, Real recruitRadius)
{
	if (recruitRadius < 1) recruitRadius = 99999.0f;
	//
	// Create "Team in queue" based on team population
	//
	if (teamProto)
	{	
		if (teamProto->getIsSingleton()) {
			Team *singletonTeam = TheTeamFactory->findTeam( teamProto->getName() );
			if (singletonTeam && singletonTeam->hasAnyObjects()) {
				AsciiString teamStr = "Unable to recruit singleton team '";
				teamStr.concat("' because team already exists.");
				TheScriptEngine->AppendDebugMessage(teamStr, false); 
				return;
			}
		}
		if (!teamProto->getTemplateInfo()->m_hasHomeLocation && !isSkirmishAI())
		{
			AsciiString teamStr = "Error : team '";
			teamStr.concat(teamProto->getName());
			teamStr.concat("' has no Home Position (or Origin).");
			TheScriptEngine->AppendDebugMessage(teamStr, false);
		}
		// create inactive team to place members into as they are built
		// when team is complete, the team is activated
		Team *theTeam = TheTeamFactory->createInactiveTeam( teamProto->getName() ); 
		AsciiString teamName = teamProto->getName();
		teamName.concat(" - Recruiting.");
		TheScriptEngine->AppendDebugMessage(teamName, false);
		const TCreateUnitsInfo *unitInfo = &teamProto->getTemplateInfo()->m_unitsInfo[0];
//		WorkOrder *orders = NULL;
		Int i;
		Int unitsRecruited = 0;
		// Recruit.
		for( i=0; i<teamProto->getTemplateInfo()->m_numUnitsInfo; i++ )
		{
			const ThingTemplate *thing = TheThingFactory->findTemplate( unitInfo[i].unitThingName );
			if (thing)
			{
				int count = unitInfo[i].maxUnits;
				while (count>0) {
					Object *unit = theTeam->tryToRecruit(thing, &teamProto->getTemplateInfo()->m_homeLocation, recruitRadius);
					if (unit) 
					{
						unitsRecruited++;

						AsciiString teamStr = "Team '";
						teamStr.concat(theTeam->getPrototype()->getName());
						teamStr.concat("' recruits ");
						teamStr.concat(thing->getName());
						teamStr.concat(" from team '");
						teamStr.concat(unit->getTeam()->getPrototype()->getName());
						teamStr.concat("'");
						TheScriptEngine->AppendDebugMessage(teamStr, false);

						unit->setTeam(theTeam);

						AIUpdateInterface *ai = unit->getAIUpdateInterface();
						if (ai) 
						{
#if defined(_DEBUG) || defined(_INTERNAL)
							Coord3D pos = *unit->getPosition();
							Coord3D to = teamProto->getTemplateInfo()->m_homeLocation;
							DEBUG_LOG(("Moving unit from %f,%f to %f,%f\n", pos.x, pos.y , to.x, to.y ));
#endif
							ai->aiMoveToPosition( &teamProto->getTemplateInfo()->m_homeLocation, CMD_FROM_AI);
						}
					} else {
						break;
					}
					count--;
				}
			}
		}
		if (unitsRecruited>0) 
		{
			/* We have something to build. */
			TeamInQueue *team = newInstance(TeamInQueue);
			// Put in front of queue.
			prependTo_TeamReadyQueue(team);
			team->m_priorityBuild = false;
			team->m_workOrders = NULL;
			team->m_frameStarted = TheGameLogic->getFrame();
			team->m_team = theTeam; 
			AsciiString teamName = teamProto->getName();
			teamName.concat(" - Finished recruiting.");
			TheScriptEngine->AppendDebugMessage(teamName, false);
		}	else {
			//disband.
			if (!theTeam->getPrototype()->getIsSingleton()) {
				theTeam->deleteInstance();
				theTeam = NULL;
			}
			AsciiString teamName = teamProto->getName();
			teamName.concat(" - Recruited 0 units, disbanding.");
			TheScriptEngine->AppendDebugMessage(teamName, false);
		}
	}
}

// ------------------------------------------------------------------------------------------------
/** Train our teams. */
// ------------------------------------------------------------------------------------------------
void AIPlayer::processTeamBuilding( void )
{
	// select a new team
	if (selectTeamToBuild()) {
		queueUnits();
	}
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void AIPlayer::queueUnits( void )
{

	queueSupplyTruck();

	// For each member of the current team to build, try to find a faction building to build it.
	//
	for ( DLINK_ITERATOR<TeamInQueue> iter = iterate_TeamBuildQueue(); !iter.done(); iter.advance())
	{
		TeamInQueue *team = iter.cur();
		for( WorkOrder *order = team->m_workOrders; order; order = order->m_next )
		{
			// check if there is a unit on the map that we can steal (recruit) instead of building
			// @todo: Should this try to alter the home location of the recruiting area to 
			// the center of the team, or to the home area of this player?
			Coord3D home = team->m_team->getPrototype()->getTemplateInfo()->m_homeLocation;
			Bool hasHome = false;
			if (team->m_team->getPrototype()->getTemplateInfo()->m_hasHomeLocation) {
				hasHome = true; 
			} else {
				hasHome = getBaseCenter(&home);
			}
			while (order->isWaitingToBuild()) {
				
				Object *unit = team->m_team->tryToRecruit(order->m_thing, &home, TheAI->getAiData()->m_maxRecruitDistance);
				if (unit) 
				{
					order->m_numCompleted++;

					AsciiString teamStr = "Team '";
					teamStr.concat(team->m_team->getPrototype()->getName());
					teamStr.concat("' recruits ");
					teamStr.concat(order->m_thing->getName());
					teamStr.concat(" from team '");
					teamStr.concat(unit->getTeam()->getPrototype()->getName());
					teamStr.concat("'");
					TheScriptEngine->AppendDebugMessage(teamStr, false);

					unit->setTeam(team->m_team);

					AIUpdateInterface *ai = unit->getAIUpdateInterface();
					if (hasHome) {
						ai->aiMoveToPosition( &home, CMD_FROM_AI);
					} else {
						ai->aiIdle(CMD_FROM_AI); // stop, you've been recruited.
					}
				}	else {
					break;
				}
			}
			if (order->isWaitingToBuild())
			{

				// start the creation of a new unit
				startTraining( order, team->m_priorityBuild, team->m_team->getName());
			}
			else
			{
				// we are under construction, verify our factory still exists
				order->validateFactory(m_player);
			}
		}
	}
}


//----------------------------------------------------------------------------------------------------------
/**
 * Frames to wait before trying the next structure, from AIData's StructureSeconds with the
 * rate modifier for a poor or a wealthy player applied.  This was four copies of the same
 * five lines; the skirmish AI needs to bend the result, so it lives in one place now.
 */
Int AIPlayer::computeStructureDelay( void )
{
	return aiHoardAdjustedDelay( computeBuildDelay( TheAI->getAiData()->m_structureSeconds,
													  m_player->getMoney()->countMoney(),
													  TheAI->getAiData()->m_resourcesPoor,
													  TheAI->getAiData()->m_resourcesWealthy,
													  TheAI->getAiData()->m_structuresPoorMod,
													  TheAI->getAiData()->m_structuresWealthyMod,
													  getBuildRateScale() ),
															 m_player->getMoney()->countMoney(),
															 hoardThreshold() );
}

//----------------------------------------------------------------------------------------------------------
Bool AIPlayer::runsOwnAI( void )
{
	return isSkirmishAI() && TheSidesList->forkAIMap() && !TheSidesList->skirmishScriptsFromMap();
}

Int AIPlayer::knowledgeObserver( void )
{
	return runsOwnAI() ? m_player->getPlayerIndex() : -1;
}

//----------------------------------------------------------------------------------------------------------
/**
 * The bank at which a skirmish AI starts building faster.  A Challenge or mission opponent is handed
 * its fortune by the map script and builds at the pace that script was written for.
 */
Int AIPlayer::hoardThreshold( void )
{
	return runsOwnAI() ? getSkillProfile()->m_cashHoardThreshold : 0;
}

//----------------------------------------------------------------------------------------------------------
/**
 * Where in a repeating check's cycle this player sits.  A player's slot is its index alone, so the
 * answer is the same on every machine and survives a save/load.
 */
Int AIPlayer::computeUpdatePhase( Int playerIndex, Int cycleFrames )
{
	if (playerIndex < 0 || cycleFrames < 1) {
		return 0;
	}
	return ((playerIndex % MAX_PLAYER_COUNT) * cycleFrames) / MAX_PLAYER_COUNT;
}

//----------------------------------------------------------------------------------------------------------
/**
 * The delay arithmetic, with nothing of the Player in it so a test can drive it: the written
 * delay, bent by whichever rate modifier the player's bank account selects, then divided by
 * how much faster than the data this kind of AI is meant to work.
 */
Int AIPlayer::computeBuildDelay( Real seconds, Int money, Int poorAt, Int wealthyAt,
																 Real poorMod, Real wealthyMod, Real rateScale )
{
	if (money < poorAt) {
		seconds = seconds/poorMod;
	}	else if (money > wealthyAt) {
		seconds = seconds/wealthyMod;
	}
	return floatToIntAsMsvc(seconds*LOGICFRAMES_PER_SECOND/rateScale);
}

//----------------------------------------------------------------------------------------------------------
/**
 * Frames to wait before trying the next team.  Same as computeStructureDelay, except the
 * base delay is m_teamSeconds, which the SET_BASE_CONSTRUCTION_SPEED script action writes.
 */
Int AIPlayer::computeTeamDelay( void )
{
	return aiHoardAdjustedDelay( computeBuildDelay( (Real)m_teamSeconds,
													  m_player->getMoney()->countMoney(),
													  TheAI->getAiData()->m_resourcesPoor,
													  TheAI->getAiData()->m_resourcesWealthy,
													  TheAI->getAiData()->m_teamPoorMod,
													  TheAI->getAiData()->m_teamWealthyMod,
													  getBuildRateScale() ),
															 m_player->getMoney()->countMoney(),
															 hoardThreshold() );
}

//----------------------------------------------------------------------------------------------------------
/**
 * See if it's time to build another base building.
 */
void AIPlayer::doBaseBuilding( void )
{
	if (m_player->getCanBuildBase()) {
		// See if we are ready to start trying a structure.
		if (!m_readyToBuildStructure) {
			m_structureTimer--;
			if (m_structureTimer<=0) {
				m_readyToBuildStructure = true;
				m_buildDelay = 0;
			}
		}
		// This timer is to keep from banging on the logic each frame.  If something interesting
		// happens, like a building is added or a unit finished, the timers are shortcut.
		m_buildDelay--;		
		if (m_buildDelay<1) {
			if (m_readyToBuildStructure) {
				processBaseBuilding();
			}
			if (m_buildDelay<1) {	// processBaseBuilding may reset m_buildDelay.
				m_buildDelay = 2*LOGICFRAMES_PER_SECOND; // check again in 2 seconds.
			}
			// Note that this timer gets shortcut when a building is completed.
		}
	}
}

//----------------------------------------------------------------------------------------------------------
/**
 * See if any ready teams have finished moving to the rally point.
 */
//----------------------------------------------------------------------------------------------------------
/** Is this team worth holding back for?
	*
	* Only attack waves: a reinforcement is going to an existing team and a base defence team is
	* needed where it stands.  The strength in hand is everything still waiting in the ready queue,
	* which is what would go out together if we released now.
	*/
//----------------------------------------------------------------------------------------------------------
Bool AIPlayer::shouldHoldForMassing( TeamInQueue *team )
{
	const AIDifficultyProfile *profile = getSkillProfile();
	if( !profile->m_massBeforeAttacking )
		return FALSE;
	if( team == NULL || team->m_reinforcement )
		return FALSE;

	const TeamTemplateInfo *info = team->m_team ? team->m_team->getPrototype()->getTemplateInfo() : NULL;
	if( info == NULL || info->m_isBaseDefense || info->m_isPerimeterDefense )
		return FALSE;

	// there is a fight at home: nothing waits at a rally point while the base is being hit
	const UnsignedInt now = TheGameLogic->getFrame();
	const Bool baseUnderAttack =
			(m_player->getAttackedFrame() + 30 * LOGICFRAMES_PER_SECOND > now) && (m_player->getAttackedFrame() > 0);

	// the existing sixty-second valve stays the outer bound on any wait
	const Bool timeExpired = team->m_frameStarted + 60 * LOGICFRAMES_PER_SECOND < now;

	// what is in hand: everything still queued up behind the rally point
	Real waitingThreat = 0.0f;
	for( DLINK_ITERATOR<TeamInQueue> iter = iterate_TeamReadyQueue(); !iter.done(); iter.advance() )
	{
		TeamInQueue *t = iter.cur();
		if( t == NULL || t->m_team == NULL )
			continue;
		for( DLINK_ITERATOR<Object> objIter = t->m_team->iterate_TeamMemberList(); !objIter.done(); objIter.advance() )
		{
			Object *obj = objIter.cur();
			if( obj == NULL || obj->isEffectivelyDead() )
				continue;
			waitingThreat += aiCombatPower( obj );
		}
	}

	//
	// How much of what it can see of the enemy army it wants in hand first. The role's knob, not
	// the rung's: an aggressive AI commits earlier and with less, a defensive one waits for more
	// and would rather counter-attack after absorbing a hit. Same money, spent differently.
	//
	const Real massFraction = (m_role == AIROLE_AGGRESSIVE) ? 0.8f : 1.3f;

	AIEnemyComposition enemy;
	computeEnemyComposition( &enemy );

	return aiShouldMass( waitingThreat, enemy.m_totalThreat, massFraction, timeExpired, baseUnderAttack );
}

/** A unit trained to top up a team that is out on the map walks to it on its own, across the map.
	* Logged as an AI ENGAGE; with EngageGate it stays at home instead, where sendIdleAttackTeams calls
	* it up with the next wave.  TRUE when it stays. */
Bool AIPlayer::holdReinforcement( Object *obj )
{
	Team *team = obj->getTeam();
	if( !runsOwnAI() || team == NULL || team == m_player->getDefaultTeam() )
		return FALSE;
	const TeamTemplateInfo *info = team->getPrototype()->getTemplateInfo();
	if( info->m_isBaseDefense || info->m_isPerimeterDefense )
		return FALSE;
	Bool away = FALSE;
	for( DLINK_ITERATOR<Object> m = team->iterate_TeamMemberList(); !m.done(); m.advance() )
		if( m.cur() != obj && !m.cur()->isEffectivelyDead() && !isAtHome( m.cur()->getPosition() ) )
			away = TRUE;
	if( !away )
		return FALSE;		// the team is at home: joining it is a walk across the base
	if( getSkillProfile()->m_engageGate && aiTeamAttacks( info ) )
	{
		DEBUG_LOG(("AI ENGAGE frame %d player %d keeps '%s' for '%s' at home for the next wave\n", TheGameLogic->getFrame(),
			m_player->getPlayerIndex(), obj->getTemplate()->getName().str(), team->getName().str()));
		return TRUE;
	}
	std::vector<Object *> units( 1, obj );
	logEngage( "reinforce", units );
	return FALSE;
}

void AIPlayer::checkReadyTeams( void )
{
	// See if any ready teams are gathered at their rally point
	{	// needed to scope iter.  silly ms c++.
		for ( DLINK_ITERATOR<TeamInQueue> iter = iterate_TeamReadyQueue(); !iter.done(); iter.advance())
		{
			TeamInQueue *team = iter.cur();
			// If 60 seconds passed, start anyway.
			Bool timeExpired = team->m_frameStarted+60*LOGICFRAMES_PER_SECOND < TheGameLogic->getFrame();
			Bool allIdle=TRUE;	
			Bool anyIdle = FALSE;
			if (team->m_reinforcement) {
				Object *obj = TheGameLogic->findObjectByID(team->m_reinforcementID);
				if (obj && obj->getAIUpdateInterface()) {
					allIdle = obj->getAIUpdateInterface()->isIdle();
					anyIdle = allIdle;
				}
			} else {
				allIdle = team->m_team->isIdle();
				for (DLINK_ITERATOR<Object> iter = team->m_team->iterate_TeamMemberList(); !iter.done(); iter.advance()) {
					Object *obj = iter.cur();	
					if (obj->getAI() && obj->getAI()->isIdle()) {
						anyIdle = true;
						break;
					}
				}
			}
			if (anyIdle && team->m_team->getPrototype()->getTemplateInfo()->m_executeActions) {
				const Script *script = TheScriptEngine->findScriptByName(team->m_team->getPrototype()->getTemplateInfo()->m_productionCondition);
				if (script && script->getAction()) {
					// we have a start action.  So don't wait for allIdle as the team may be guarding.
					allIdle = true;
				}
			}
			if (timeExpired) allIdle = true;
			//
			// C2 (massing before attacking) was hooked in here and is deliberately not any more.
			// Holding a finished team at the rally point by leaving it in the ready queue jams the
			// whole production pipeline behind it - the team still counts as an instance, so no
			// replacement is selected, and 20 measured brutal-vs-brutal matches went from nine
			// decided to none, with zero kills and a quarter of the spending on both sides.
			//
			// The second half of the lesson is in the threshold rather than the plumbing: a wait
			// measured against the enemy's whole visible army deadlocks two symmetric AIs, because
			// neither ever has more than the other. Wherever this lands next, it has to let the
			// team activate (so production keeps moving) and hold it somewhere that is not the
			// queue, against a threshold that is not a fraction of the opponent.
			//
			// shouldHoldForMassing and aiShouldMass are kept, with their test, for that attempt.
			//
			if (allIdle) {
				if (!team->m_sentToStartLocation) {
					team->m_sentToStartLocation = true;
					/*
					if (team->m_team->getPrototype()->getTemplateInfo()->m_hasHomeLocation && 
							!team->m_reinforcement) {
 						AIGroup* theGroup = TheAI->createGroup();
						if (theGroup) {
							team->m_team->getTeamAsAIGroup(theGroup);
							Coord3D destination = team->m_team->getPrototype()->getTemplateInfo()->m_homeLocation;
							theGroup->groupTightenToPosition( &destination, false, CMD_FROM_AI );
							team->m_frameStarted = TheGameLogic->getFrame();
							continue;
						}
					}
					*/
				}
				// Start the team up.
				removeFrom_TeamReadyQueue(team);
				if (team->m_reinforcement) {
					Object *obj = TheGameLogic->findObjectByID(team->m_reinforcementID);
					if (obj&&obj->getAIUpdateInterface() && !holdReinforcement(obj)) {
						obj->getAIUpdateInterface()->joinTeam();
					}
				} else {
					// mark our completed team as "active" - this will invoke any OnCreate scripts, etc.
					team->m_team->setActive();
					if (isSkirmishAI()) {
						TheScriptEngine->clearTeamFlags();
					}
					if (TheGlobalData->m_debugAI) {
						AsciiString teamName = team->m_team->getPrototype()->getName();
						teamName.concat(" - team activated.");
						TheScriptEngine->AppendDebugMessage(teamName, false);
					}
				}
				team->deleteInstance();
				iter = iterate_TeamReadyQueue();
			}																		 
		}
	}
}

//----------------------------------------------------------------------------------------------------------
/**
 * See if any queued teams have finished building, or have run out of time.
 */
void AIPlayer::checkQueuedTeams( void )
{
	// See if any teams are expired.
	{	// needed to scope iter.  silly ms c++.
		for ( DLINK_ITERATOR<TeamInQueue> iter = iterate_TeamBuildQueue(); !iter.done(); iter.advance())
		{
			TeamInQueue *team = iter.cur();
			if (team && team->isBuildTimeExpired())	{
				if (team->isMinimumBuilt()) 
				{
					if (team->areBuildsComplete()) {
						// Move to ready queue
						removeFrom_TeamBuildQueue(team);
						prependTo_TeamReadyQueue(team);
					}	else {
						continue;
					}
				}	else {
					// Disband.
					removeFrom_TeamBuildQueue(team);
					team->disband();
					team->deleteInstance();
					if (isSkirmishAI()) {
						TheScriptEngine->clearTeamFlags();
					}
				}	 
				iter = iterate_TeamBuildQueue();
			}
		}
	}

	// See if any teams are ready.
	{	// needed to scope iter.  silly ms c++.
		for ( DLINK_ITERATOR<TeamInQueue> iter = iterate_TeamBuildQueue(); !iter.done(); iter.advance())
		{
			TeamInQueue *team = iter.cur();
			if (team && team->isAllBuilt())
			{
				// Move to ready queue
				removeFrom_TeamBuildQueue(team);
				prependTo_TeamReadyQueue(team);
				iter = iterate_TeamBuildQueue();
				continue;
			}
			Bool anyIdle = false;
			for (DLINK_ITERATOR<Object> iter = team->m_team->iterate_TeamMemberList(); !iter.done(); iter.advance()) {
				Object *obj = iter.cur();
				if (obj && obj->getAI() && obj->getAI()->isIdle()) {
					anyIdle = true;
					break;		// one is the answer, and this runs every frame for every team in the queue
				}
			}
			if (anyIdle) {
				if (team->m_team->getPrototype()->getTemplateInfo()->m_executeActions) {
					const Script *script = TheScriptEngine->findScriptByName(team->m_team->getPrototype()->getTemplateInfo()->m_productionCondition);
					if (script) {
						TheScriptEngine->friend_executeAction(script->getAction(), team->m_team);
					}
				}
			}	
		}
	}
}

//----------------------------------------------------------------------------------------------------------
/**
 * See if it is time to start another ai team building.
 */
void AIPlayer::doTeamBuilding( void )
{
	// See if any teams are expired.
	if (m_player->getCanBuildUnits()) {
		// See if we are ready to start trying a team.
		if (!m_readyToBuildTeam) {
			m_teamTimer--;
			if (m_teamTimer<=0) {
				m_readyToBuildTeam = true;
				m_teamDelay = 0;
			}
		}

		// This timer is to keep from banging on the logic each frame.  If something interesting
		// happens, like a building is added or a unit finished, the timers are shortcut.
		m_teamDelay--;
		if (m_teamDelay<1) {
			queueUnits(); // update the queues.
			if (m_readyToBuildTeam) {
				processTeamBuilding();
			}
			m_teamDelay = 5*LOGICFRAMES_PER_SECOND; // check again in 5 seconds.
			// Note that this timer gets shortcut when a unit or building is completed.
		}
	}
}

//----------------------------------------------------------------------------------------------------------
/**
 * See if it is time to start another upgrade or skill building.
 */
void AIPlayer::doUpgradesAndSkills( void )
{
	if (TheGameLogic->getFrame() < 2) {
		// can't do updates on the first few frames
		return;
	}

	Bool checkScience = m_player->getSciencePurchasePoints()>0;
	if (!checkScience) {
		return;
	}
	const AISideInfo *sideInfo = TheAI->getAiData()->findSideInfo( m_player );
	if (sideInfo == NULL) return;

	if (m_skillsetSelector == INVALID_SKILLSET_SELECTION) {
		Int limit = 0;		
		// Pick randomly among the skillsets that have skills.
		// Designers sometimes only define skillset 1 & 2, or some such.  jba.
		if (sideInfo->m_skillSet2.m_numSkills>0) {
			limit = 1;
			if (sideInfo->m_skillSet3.m_numSkills>0) {
				limit = 2;
				if (sideInfo->m_skillSet4.m_numSkills>0) {
					limit = 3;
					if (sideInfo->m_skillSet5.m_numSkills>0) {
						limit = 4;
					}
				}
			}
		}
		if (isSkirmishAI() && !runsOwnAI()) {
			m_skillsetSelector = GameLogicRandomValue(0, limit);	// EA's roll, across every set
		} else if (isSkirmishAI()) {
			//
			// Split the designers' skill sets between the two roles rather than rolling across all
			// of them (D8).  This makes no claim about which set is which - only that an aggressive
			// AI and a defensive one draw from different halves, so the powers you see coming tell
			// you which one you are facing.  Still a roll, so repeat matches still differ.
			//
			if (limit > 0) {
				const Int half = limit / 2;
				if (m_role == AIROLE_AGGRESSIVE)
					m_skillsetSelector = GameLogicRandomValue(0, half);
				else
					m_skillsetSelector = GameLogicRandomValue(half, limit);
			} else {
				m_skillsetSelector = 0;
			}
		} else {
			m_skillsetSelector = 0; // Non-skirmish default to 0.  jba.
		}
	}

	// SKILLS
	if (m_player->getSciencePurchasePoints()>0) {
		const TSkillSet *skillset;
		switch(m_skillsetSelector) {
			default:
			case 0: skillset = &sideInfo->m_skillSet1; break;
			case 1: skillset = &sideInfo->m_skillSet2; break;
			case 2: skillset = &sideInfo->m_skillSet3; break;
			case 3: skillset = &sideInfo->m_skillSet4; break;
			case 4: skillset = &sideInfo->m_skillSet5; break;
		}
		const Bool saves = runsOwnAI() && getSkillProfile()->m_savesSciencePoints;
		// a Sneaky AI's points go first on whatever unlocks a unit that goes invisible
		if (m_persona == AIPERSONA_SNEAKY) {
			for (Int s = 0; s < skillset->m_numSkills; ++s) {
				const ScienceType science = skillset->m_skills[s];
				if (unlocksStealthUnit(m_player, science) && m_player->isCapableOfPurchasingScience(science))
					m_player->attemptToPurchaseScience(science);
			}
		}
		Int i;
		for (i=0; i<skillset->m_numSkills; i++) {
			ScienceType science = skillset->m_skills[i];

			//
			// Saving up (B5).  EA walked the whole set and bought the first thing it could afford,
			// so a point went on filler while the ability the set is actually built around sat two
			// points away for ever.  If the next thing in the designer's own order is only out of
			// reach on points, stop here and keep them; anything else - already owned, or blocked
			// by a rank we have not reached - is skipped as before.
			//
			if (saves && !m_player->hasScience(science)) {
				const Int cost = TheScienceStore->getSciencePurchaseCost(science);
				if (cost > m_player->getSciencePurchasePoints()) {
					break;
				}
			}

			if (m_player->isCapableOfPurchasingScience(science)) {
				if (m_player->attemptToPurchaseScience(science)) {
						AsciiString msg = TheNameKeyGenerator->keyToName(m_player->getPlayerNameKey());
						msg.concat(" purchases from SkillSet");
						msg.concat('1'+m_skillsetSelector);
						msg.concat(' ');
						msg.concat(TheScienceStore->getInternalNameForScience(science));
						msg.concat(".");
						TheScriptEngine->AppendDebugMessage( msg, false);
				}
			}
		}
	}


}

//----------------------------------------------------------------------------------------------------------
/**
 * Perform computer-controlled player AI
 */
//DECLARE_PERF_TIMER(AIPlayer_update)
#ifdef DEBUG_LOGGING
/* The slow-frame report in GameLogic.cpp could only say "players 16.2ms" - one number for every
	 computer player and all ten of the jobs each one does. That is enough to know the stutter is in
	 here and not enough to do anything about it, so this breaks it down the way ScriptEngine and
	 Pathfinder already break theirs down: per job, plus whichever single player cost the most.
	 Reset once per logic frame by AI::update. */
enum { AIP_BASE, AIP_READY, AIP_QUEUED, AIP_TEAM, AIP_UPGRADE,
			 AIP_BRIDGE, AIP_SCOUT, AIP_RETREAT, AIP_EXPAND, AIP_CAPTURE, AIP_ECONOMY, AIP_POWER, AIP_WAVE, AIP_TACTICS, AIP_PHASE_COUNT };
static const char *theAIPhaseName[ AIP_PHASE_COUNT ] =
	{ "base", "ready", "queued", "team", "upg", "bridge", "scout", "retreat", "expand", "capture", "economy", "power", "wave", "tactics" };
static Real theAIPhaseMS[ AIP_PHASE_COUNT ];
static Real theAIWorstPlayerMS = 0.0f;
static Int theAIWorstPlayer = -1;
static Int theAIPlayersUpdated = 0;
static Int64 theAIPhaseStart;

static void aiPhaseBegin( void )
{
	theAIPhaseStart = Clock_Ticks();
}

static void aiPhaseEnd( Int phase )
{
	Int64 now;
	now = Clock_Ticks();
	theAIPhaseMS[ phase ] += aiPlayerElapsedMS( theAIPhaseStart, now );
}


#define AI_PHASE( phase, call )	do { aiPhaseBegin(); call; aiPhaseEnd( phase ); } while(0)

/*static*/ void AIPlayer::resetFrameProfile( void )
{
	for( Int i = 0; i < AIP_PHASE_COUNT; ++i )
		theAIPhaseMS[ i ] = 0.0f;
	theAIWorstPlayerMS = 0.0f;
	theAIWorstPlayer = -1;
	theAIPlayersUpdated = 0;
	for( Int s = 0; s < BASE_SUB_COUNT; ++s )
	{
		theAIBaseSubMS[ s ] = 0.0f;
		theAIBaseSubCalls[ s ] = 0;
	}
}

/*static*/ const char *AIPlayer::getProfileReport( void )
{
	static char report[ 512 ];
	Int used = sprintf( report, "%d ai players, worst player %d %.1fms |",
											theAIPlayersUpdated, theAIWorstPlayer, theAIWorstPlayerMS );
	for( Int phase = 0; phase < AIP_PHASE_COUNT; ++phase )
		used += sprintf( report + used, " %s %.1f", theAIPhaseName[ phase ], theAIPhaseMS[ phase ] );
	used += sprintf( report + used, " || base:" );
	for( Int s = 0; s < BASE_SUB_COUNT; ++s )
		used += sprintf( report + used, " %s %.1f/%dx",
										 theAIBaseSubName[ s ], theAIBaseSubMS[ s ], theAIBaseSubCalls[ s ] );
	return report;
}
#else
/*static*/ void AIPlayer::resetFrameProfile( void ) { }
/*static*/ const char *AIPlayer::getProfileReport( void ) { return ""; }
#define AI_PHASE( phase, call )	call
#endif

void AIPlayer::update( void )
{
	//USE_PERF_TIMER(AIPlayer_update)
#ifdef DEBUG_LOGGING
	Int64 playerStart;
	playerStart = Clock_Ticks();
#endif

	AI_PHASE( AIP_BASE,    doBaseBuilding() );			// See if it's time to build another building.
	AI_PHASE( AIP_READY,   checkReadyTeams() );			// See if any teams are ready to start.
	AI_PHASE( AIP_QUEUED,  checkQueuedTeams() );		// See if any teams are complete.
	AI_PHASE( AIP_TEAM,    doTeamBuilding() );			// See if it's time to start another team.
	AI_PHASE( AIP_UPGRADE, doUpgradesAndSkills() );	// See if it's time to build an upgrade or buy a skill.
	AI_PHASE( AIP_BRIDGE,  updateBridgeRepair() );	// Handle any bridge repairs.
	// Everything below is the skirmish AI's own. A Challenge or mission opponent is handed its money,
	// its base, its attacks and its superweapon by the map script, which is the whole of its difficulty:
	// run on top of that, Kwai's $30000 stood three nuclear silos a minute into the match.  So is a
	// skirmish seat on a map that scripts it: an Art of Defense GLA left its wave paths for a base.
	if( runsOwnAI() )
	{
		AI_PHASE( AIP_SCOUT,   doScouting() );					// Keep one unit looking at the map.
		AI_PHASE( AIP_RETREAT, doRetreats() );					// Break off the fights we are losing.
		AI_PHASE( AIP_EXPAND,  doExpansion() );					// Go and take the money that is lying around.
		AI_PHASE( AIP_CAPTURE, doCapture() );						// ... and the money that is standing around.
		AI_PHASE( AIP_CAPTURE, doHijack() );						// Take the enemy's tanks rather than shoot them.
		AI_PHASE( AIP_ECONOMY, doEconomy() );						// ... and the money sitting in the bank.
		AI_PHASE( AIP_ECONOMY, doSpending() );					// Upgrades for the army, and whatever is over the bank target.
		AI_PHASE( AIP_POWER,   doPower() );							// Keep the lights on, and out of reach.
		AI_PHASE( AIP_ECONOMY, doSuperweapons() );			// The big guns, as soon as they can be bought.
		AI_PHASE( AIP_TACTICS, doParticleBeams() );			// ... and the Particle Cannon's beam driven across his base.
		AI_PHASE( AIP_ECONOMY, doReadySpecialPowers() );	// The powers no script knows to fire, as soon as they are ready.
		AI_PHASE( AIP_WAVE,    rememberEnemies() );			// Write down the enemy army in sight.
		AI_PHASE( AIP_WAVE,    answerArmy() );					// Train the answer to the part of it nothing answers.
		AI_PHASE( AIP_WAVE,    doMass() );							// Build the fist, and send it when it is one.
		AI_PHASE( AIP_WAVE,    doWaves() );							// Send the parked attack teams out together.
		AI_PHASE( AIP_TACTICS, dodgeStrike() );					// A superweapon went up: the helicopters do not wait to see where.
		AI_PHASE( AIP_TACTICS, doTactics() );						// Fight each unit from where it is strongest.
		AI_PHASE( AIP_TACTICS, holdAtDefences() );			// Wait at the edge of the guns, and shell what can be outranged.
		AI_PHASE( AIP_TACTICS, doTransports() );				// Put the helicopters' riders down at the fight.
		AI_PHASE( AIP_TACTICS, doShuttles() );					// Fly the wave's ground units up its road.
		AI_PHASE( AIP_TACTICS, doHelixes() );					// Helix upgrades by the enemy army, healers, bomb raids.
		AI_PHASE( AIP_TACTICS, doAmbulances() );				// Ambulances behind the army, for the hurt and the hazards.
	}

#ifdef DEBUG_LOGGING
	Int64 playerEnd;
	playerEnd = Clock_Ticks();
	const Real playerMS = aiPlayerElapsedMS( playerStart, playerEnd );
	++theAIPlayersUpdated;
	if( playerMS > theAIWorstPlayerMS )
	{
		theAIWorstPlayerMS = playerMS;
		theAIWorstPlayer = m_player ? m_player->getPlayerIndex() : -1;
	}
#endif
}

//----------------------------------------------------------------------------------------------------------
/**
 * Find any things that build stuff & add them to the build list.  Then build any initially built
 * buildings.
 */
void AIPlayer::newMap( void )
{
	BuildListInfo *info = m_player->getBuildList();
	// Add any factories placed to the build list.
	Object *obj;
	for( obj = TheGameLogic->getFirstObject(); obj; obj = obj->getNextObject() )
	{

		Player *owner = obj->getControllingPlayer();
		if (owner==m_player) {
			// See if it's a factory.
			ProductionUpdateInterface *pu = obj->getProductionUpdateInterface();
			// If it doesn't produce, continue.
			if (!pu) continue;
			m_player->addToBuildList(obj);
		}

	}
	computeCenterAndRadiusOfBase(&m_baseCenter, &m_baseRadius);

	// Build any with the initially built flag.
	for( /* nothing */; info; info = info->getNext() )
	{
		AsciiString name = info->getTemplateName();
		if (name.isEmpty()) continue;
		const ThingTemplate *bldgPlan = TheThingFactory->findTemplate( name );
		if (!bldgPlan) {																											 
			DEBUG_LOG(("*** ERROR - Build list building '%s' doesn't exist.\n", name.str()));
			continue;
		}
		if (info->isInitiallyBuilt()) {
			buildStructureNow(bldgPlan, info);
		} else {
			info->incrementNumRebuilds(); // the initial build in the normal build list consumes a rebuild, so add one.
		}
	}
}

// ------------------------------------------------------------------------------------------------
/** Find the center of the base and the radius of buildings.  */
// ------------------------------------------------------------------------------------------------
void AIPlayer::computeCenterAndRadiusOfBase(Coord3D *center, Real *radius)
{
	//
	BuildListInfo *info;
	Coord2D totalPos;
	totalPos.x = 0;
	totalPos.y = 0;
	Int numBldg=0;
	for( info = m_player->getBuildList(); info; info = info->getNext() )
	{
		AsciiString name = info->getTemplateName();
		if (name.isEmpty()) continue;
		const ThingTemplate *bldgPlan = TheThingFactory->findTemplate( name );
		if (!bldgPlan) {																											 
			continue;
		}
		Coord3D pos = *info->getLocation();
		totalPos.x += pos.x;
		totalPos.y += pos.y;
		numBldg++;
	}
	if (numBldg>0) {
		totalPos.x /= numBldg;
		totalPos.y /= numBldg;
	}

	m_baseCenterSet = numBldg>0;
	center->x = totalPos.x;
	center->y = totalPos.y;

	Real maxRadSqr = 0;
	//
	for( info = m_player->getBuildList(); info; info = info->getNext() )
	{
		AsciiString name = info->getTemplateName();
		if (name.isEmpty()) continue;
		const ThingTemplate *bldgPlan = TheThingFactory->findTemplate( name );
		if (!bldgPlan) {																											 
			continue;
		}
		Coord3D pos = *info->getLocation();
		Real dx = pos.x-center->x;
		Real dy = pos.y-center->y;
		if (dx<0) dx = -dx;
		if (dy<0) dy = -dy;
		Real bldgRadius = bldgPlan->getTemplateGeometryInfo().getBoundingCircleRadius()*0.4f;
		dx += bldgRadius;
		dy += bldgRadius; 
		Real radSqr = dx*dx+dy*dy;
		if (radSqr>maxRadSqr) maxRadSqr=radSqr;
	}
	*radius = sqrt(maxRadSqr);
}

//----------------------------------------------------------------------------------------------------------
/**
 * Checks to see if we're building a dozer.
 */
Bool AIPlayer::dozerInQueue( void )
{
	{	// needed to scope iter.  silly ms c++.
		for ( DLINK_ITERATOR<TeamInQueue> iter = iterate_TeamBuildQueue(); !iter.done(); iter.advance())
		{
			TeamInQueue *team = iter.cur();
			if (team && team->includesADozer() )
			{
				return true; // dozer is building already.
			}
		}
	}
	return false;
}

/** A building of this name the AI asked for is on the build list and not yet standing. */
static Bool hasPendingRequest( Player *player, const AsciiString &name )
{
	for( BuildListInfo *info = player->getBuildList(); info; info = info->getNext() )
	{
		if( info->isPriorityBuild() && info->getTemplateName() == name &&
				info->getObjectID() == INVALID_ID && info->isBuildable() )
			return TRUE;
	}
	return FALSE;
}

/** Something of this player's that can shoot stands within reach of a point: a base defense, a
	* team on guard. */
static Bool hasGuardNear( Player *player, const Coord3D *pos, Real reach )
{
	PartitionFilterPlayer mine( player, true );
	PartitionFilterAlive alive;
	PartitionFilterRejectByKindOf notHarvester( MAKE_KINDOF_MASK( KINDOF_HARVESTER ), KINDOFMASK_NONE );
	PartitionFilterRejectByKindOf notDozer( MAKE_KINDOF_MASK( KINDOF_DOZER ), KINDOFMASK_NONE );
	PartitionFilter *filters[] = { &mine, &alive, &notHarvester, &notDozer, NULL };

	ObjectIterator *iter = ThePartitionManager->iterateObjectsInRange( pos, reach, FROM_BOUNDINGSPHERE_2D, filters );
	MemoryPoolObjectHolder hold( iter );
	for( Object *obj = iter->first(); obj; obj = iter->next() )
	{
		if( obj->isAbleToAttack() && !obj->testStatus( OBJECT_STATUS_UNDER_CONSTRUCTION ) )
			return TRUE;
	}
	return FALSE;
}

//----------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------
/** Notice that there is money to be had, and go and take it.
	*
	* Every piece of this already existed and only ever ran from a script: buildBySupplies places a
	* structure beside a warehouse, findSupplyCenter picks a warehouse that is worth taking and is
	* not already served, and guardSupplyCenter sends a team to sit on one.  What was missing was
	* any decision to call them - so an AI ran its starting piles dry and then simply stopped
	* earning.
	*
	* The defence goes down in the same job: an undefended expansion is a gift, and the same
	* buildBySupplies call places a base defence structure beside the warehouse facing the enemy
	* (that branch of it is written for exactly this and had no caller either).
	*/
//----------------------------------------------------------------------------------------------------------
void AIPlayer::doExpansion( void )
{
	const AIDifficultyProfile *profile = getSkillProfile();
	if( !profile->m_selfTriggeredExpansion )
		return;

	if( --m_expandTimer > 0 )
		return;
	m_expandTimer = EXPANSION_CHECK_SECONDS * LOGICFRAMES_PER_SECOND;

	if( !m_player->getCanBuildBase() || !m_baseCenterSet )
		return;

	//
	// What this faction calls a supply centre: whatever is already flagged as one on the build
	// list.  Faction-correct and mod-correct without a table, and if the AI has never had one it
	// has nothing to expand with anyway.
	//
	AsciiString supplyCenterName;
	for( BuildListInfo *info = m_player->getBuildList(); info; info = info->getNext() )
	{
		if( info->isSupplyBuilding() && info->getTemplateName().isNotEmpty() )
		{
			supplyCenterName = info->getTemplateName();
			break;
		}
	}
	if( supplyCenterName.isEmpty() )
		return;

	//
	// A centre queued by the last check and not yet standing does not count as "a centre beside that
	// dock", so the same dock was picked again every minute, a fresh copy queued, and its placement
	// search run again: 2,300 checks, 23-33ms, each time.  The base builder still owns the queued one.
	//
	if( hasPendingRequest( m_player, supplyCenterName ) )
		return;

	const ThingTemplate *tmpl = TheThingFactory->findTemplate( supplyCenterName, FALSE );
	if( tmpl == NULL )
		return;

	//
	// One expansion at a time.  The last one, if its supply center still stands, its pile still has
	// boxes and it is out past the base, gets something of ours that shoots beside it before the
	// next is opened; every minute opened another, each as bare as the one before.  The wait lasts
	// only as long as a gun for it is on the build list and can be built: a profile that defends
	// nothing, a side with no gun, a dock with no room for one or two guns already standing there
	// all let the next expansion go ahead.
	//
	const Object *last = TheGameLogic->findObjectByID( m_curWarehouseID );
	if( profile->m_defendExpansions && last && isHeldExpansion( last ) &&
			sqr( last->getPosition()->x - m_baseCenter.x ) + sqr( last->getPosition()->y - m_baseCenter.y ) > sqr( 2.0f * m_baseRadius ) &&
			hasSuppliesNear( m_player, last, 0.0f ) &&
			!hasGuardNear( m_player, last->getPosition(), TheAI->getAiData()->m_supplyCenterSafeRadius ) &&
			buildExpansionDefense() )
		return;

	// pay for it out of what is spare, not out of the army's money
	const Int cost = tmpl->calcCostToBuild( m_player );
	if( m_player->getMoney()->countMoney() < 2 * cost )
		return;

	//
	// Somewhere worth going. findSupplyCenter already refuses a warehouse that is picked clean, one
	// this AI already has a centre beside, and one that is closer to the enemy than to us.
	//
	const Int WORTH_TAKING = 1000;
	if( findSupplyCenter( WORTH_TAKING, TRUE ) == NULL )
		return;

	buildBySupplies( WORTH_TAKING, supplyCenterName, TRUE );

	// the gun follows a supply center that was asked for: buildBySupplies puts it at the dock it
	// chose last, and a call that found no room chose none
	if( profile->m_defendExpansions && hasPendingRequest( m_player, supplyCenterName ) )
		buildExpansionDefense();
}

/** This side's first base defense, beside the dock buildBySupplies chose last.  True while one is
	* on the build list and this player can build it. */
Bool AIPlayer::buildExpansionDefense( void )
{
	const AISideInfo *resInfo = TheAI->getAiData()->findSideInfo( m_player );
	if( resInfo != NULL )
	{
		const ThingTemplate *defense = TheThingFactory->findTemplate( resInfo->m_baseDefenseStructure1, FALSE );
		if( defense == NULL )
			return FALSE;
		buildBySupplies( 1000, resInfo->m_baseDefenseStructure1 );	// the amount only picks a dock when the last one is gone
		return m_player->canBuild( defense ) && hasPendingRequest( m_player, resInfo->m_baseDefenseStructure1 );
	}
	return FALSE;
}

//----------------------------------------------------------------------------------------------------------
/** How often the economy is looked at.  No timer: the frame and the player's slot decide it, so there
	* is nothing to save and every machine lands on the same frame. */
static const Int ECONOMY_CHECK_RATE = 10 * LOGICFRAMES_PER_SECOND;

/** doEconomy, doSpending and answerArmy all run on the ten-second rate, and in the player's own slot
	* they fell on one frame together, with the team pass beside them: 8 to 10 ms of one player late in a
	* match, a dropped picture every ten seconds.  Each takes its own third of the slot instead, in the
	* order they ran in: spending one step after the economy, the answer two. */
static const Int ECONOMY_SLOT_STEP = ECONOMY_CHECK_RATE / ( 3 * MAX_PLAYER_COUNT );

/** How often the power is looked at, and the margin that counts as thin.  Five seconds because a
	* plant takes about that long to matter and an outage costs the whole base; the reserve is about
	* what one more building will draw, so the plant is ordered before the building that needs it. */
static const Int POWER_CHECK_RATE = 5 * LOGICFRAMES_PER_SECOND;
static const Int POWER_RESERVE = 5;

/** How often a Hard AI looks at whether it can put up a superweapon. */
static const Int SUPERWEAPON_CHECK_RATE = 5 * LOGICFRAMES_PER_SECOND;

/** How far behind the base a new power plant goes, in base radii, measured away from the enemy. */
static const Real POWER_SETBACK = 0.75f;

/** And how far in front of it a bought base defense goes, on the same line the other way. */
static const Real DEFENSE_STANDOFF = 1.0f;

/** Dozers a Hard AI trains on its own when every one it has is on a building. */
static const Int MAX_ECONOMY_DOZERS = 4;

/** Legality checks placeNear may spend per call, as rings of tries: PLACEMENT_RINGS times
	* PLACEMENT_ANGLES.  Every try costs about a millisecond, so that is the spike, not the reach of
	* the search.  A search allowed to walk outward remembers the ground distance it reached and starts
	* the next call there, so a full opening base does not end the search. */
static const Int PLACEMENT_ANGLES = 12;
static const Int PLACEMENT_RINGS = 2;

/** Rings' worth of tries a call spends on a building that stays home (staysHome).  It has no outward
	* walk to carry on with next time, and a spot past the base's edge costs no legality check. */
static const Int HOME_PLACEMENT_RINGS = 6;

/** How far either side of a waypoint an approach counts the guns along it. */
static const Real APPROACH_WATCH_RADIUS = 250.0f;

/** Longest approach walked, in waypoints; a path that loops would otherwise never end. */
static const Int APPROACH_MAX_WAYPOINTS = 64;

/** The influence map's cell: a fifth of a tank's range, so the edge of a gun's reach is placed to
	* within a tank's length. */
static const Real INFLUENCE_CELL_SIZE = 60.0f;

/** A unit that kited this recently is left in its fight by the retreat. */
static const UnsignedInt KITE_KEEPS_FIGHT_FRAMES = 5 * LOGICFRAMES_PER_SECOND;

/** One of the rung's times in logic frames, never less than one. */
static Int rungFrames( Real seconds )
{
	return max<Int>( 1, REAL_TO_INT_FLOOR( seconds * LOGICFRAMES_PER_SECOND + 0.5f ) );
}

/** What stands on the map for a shooter: itself, or what it rides in (a garrison's building, an Overlord
	* under its bunker). */
static Object *gunCarrier( Object *shooter )
{
	while( shooter->getContainedBy() )
		shooter = shooter->getContainedBy();
	return shooter;
}

/** Clear ground all the way on the straight line between two points: no cliff, water or wall cell in
	* between.  A spot on top of a ledge is a short step on the map and a long drive round by the ramp,
	* and a tactical step is only worth it when it is the short one. */
static Bool groundLineClear( const Coord3D *from, const Coord3D *to )
{
	const Real dx = to->x - from->x;
	const Real dy = to->y - from->y;
	const Int samples = 1 + REAL_TO_INT_FLOOR( sqrt( dx * dx + dy * dy ) / (PATHFIND_CELL_SIZE_F * 0.5f) );
	for( Int i = 1; i <= samples; ++i )
	{
		Coord3D at;
		at.x = from->x + dx * i / samples;
		at.y = from->y + dy * i / samples;
		at.z = 0.0f;
		ICoord2D cell;
		TheAI->pathfinder()->worldToCell( &at, &cell );
		const PathfindCell *pathCell = TheAI->pathfinder()->getCell( LAYER_GROUND, cell.x, cell.y );
		if( pathCell == NULL || pathCell->getType() != PathfindCell::CELL_CLEAR )
			return FALSE;
	}
	return TRUE;
}

/** Clear ground under the middle, the corners and the edges of a square half wide either side of pos:
	* a look at nine pathfind cells, against the millisecond a legality check costs. */
static Bool footprintOnClearGround( const Coord3D *pos, Real half )
{
	for( Int corner = 0; corner < 9; ++corner )
	{
		Coord3D at = *pos;
		at.x += half * ( corner % 3 - 1 );
		at.y += half * ( corner / 3 - 1 );
		ICoord2D cell;
		TheAI->pathfinder()->worldToCell( &at, &cell );
		const PathfindCell *pathCell = TheAI->pathfinder()->getCell( LAYER_GROUND, cell.x, cell.y );
		if( pathCell == NULL || pathCell->getType() != PathfindCell::CELL_CLEAR )
			return FALSE;
	}
	return TRUE;
}

/** Something of this kind the builder can make right now, off the builder's own buttons, so the
	* answer is right for every faction and general without a table of names.  moneyLater also takes one
	* the bank cannot pay for yet, for a request the purchase queue saves up for. */
static const ThingTemplate *buildableOfKind( Object *builder, GUICommandType commandType, KindOfType kind, Bool moneyLater = FALSE )
{
	const CommandSet *commandSet = TheCommandStore->findCommandSet( builder->getCommandSetString() );
	if( commandSet == NULL )
		return NULL;

	for( Int i = 0; i < MAX_COMMANDS_PER_SET; ++i )
	{
		const CommandButton *button = commandSet->getCommandButton( i );
		if( button == NULL || button->getCommandType() != commandType )
			continue;
		const ThingTemplate *tmpl = button->getThingTemplate();
		if( tmpl == NULL || !tmpl->isKindOf( kind ) )
			continue;
		const CanMakeType canMake = TheBuildAssistant->canMakeUnit( builder, tmpl );
		if( canMake == CANMAKE_OK || (moneyLater && canMake == CANMAKE_NO_MONEY) )
			return tmpl;
	}
	return NULL;
}

static Bool priorityBuildPending( Player *player, const ThingTemplate *tmpl );

//----------------------------------------------------------------------------------------------------------
/** The base defence this player has the fewest of, off the dozer's buttons (aiPickBaseDefense).  Asking
	* buildableOfKind for one gave the first button every time: a USA base of nothing but Patriots, a
	* GLA one of Stinger Sites, and China bunkers nobody sat in beside the Gattling Cannons its scripts
	* asked for.  One standing, going up or waiting on the build list counts alike. */
//----------------------------------------------------------------------------------------------------------
const ThingTemplate *AIPlayer::nextBaseDefense( Object *dozer )
{
	const CommandSet *commandSet = TheCommandStore->findCommandSet( dozer->getCommandSetString() );
	if( commandSet == NULL )
		return NULL;

	const ThingTemplate *candidates[ MAX_COMMANDS_PER_SET ];
	Bool armed[ MAX_COMMANDS_PER_SET ];
	Int count = 0;
	for( Int i = 0; i < MAX_COMMANDS_PER_SET; ++i )
	{
		const CommandButton *button = commandSet->getCommandButton( i );
		if( button == NULL || button->getCommandType() != GUI_COMMAND_DOZER_CONSTRUCT )
			continue;
		const ThingTemplate *tmpl = button->getThingTemplate();
		if( tmpl == NULL || !tmpl->isKindOf( KINDOF_FS_BASE_DEFENSE ) )
			continue;
		// short of money is still a candidate: the build list waits for it, and a pick made only from
		// what the bank covers today would lean on the cheapest
		const CanMakeType canMake = TheBuildAssistant->canMakeUnit( dozer, tmpl );
		if( canMake != CANMAKE_OK && canMake != CANMAKE_NO_MONEY )
			continue;
		candidates[ count ] = tmpl;
		armed[ count ] = tmpl->canPossiblyHaveAnyWeapon() || tmpl->isKindOf( KINDOF_SPAWNS_ARE_THE_WEAPONS );
		++count;
	}
	if( count == 0 )
		return NULL;

	Int standing[ MAX_COMMANDS_PER_SET ];
	m_player->countObjectsByThingTemplate( count, candidates, TRUE, standing, FALSE );
	for( Int i = 0; i < count; ++i )
		if( priorityBuildPending( m_player, candidates[ i ] ) )
			++standing[ i ];
	// a Turtle's line leans on the gun its soldiers sit in, the Fire Base: two of it to one of the rest
	if( m_persona == AIPERSONA_TURTLE )
		for( Int i = 0; i < count; ++i )
			if( armed[ i ] && candidates[ i ]->isKindOf( KINDOF_GARRISONABLE_UNTIL_DESTROYED ) )
				standing[ i ] /= 2;

	const Int pick = aiPickBaseDefense( standing, armed, count );
	return pick < 0 ? NULL : candidates[ pick ];
}

/** Where a defence's own guns reach, off its default weapon set.  A Stinger Site's soldiers, an empty
	* bunker and a speaker tower have none, and are scored as if they reached DEFENSE_REACH_UNARMED. */
static const Real DEFENSE_REACH_UNARMED = 250.0f;

static Real defenseReach( const ThingTemplate *tmpl )
{
	Real reach = 0.0f;
	const WeaponTemplateSet *set = tmpl->getWeaponTemplateSets().empty() ? NULL : tmpl->findWeaponTemplateSet( WeaponSetFlags() );
	for( Int slot = 0; set && slot < WEAPONSLOT_COUNT; ++slot )
	{
		const WeaponTemplate *weapon = set->getNth( (WeaponSlotType)slot );
		if( weapon && weapon->getUnmodifiedAttackRange() > reach )
			reach = weapon->getUnmodifiedAttackRange();
	}
	return reach > 0.0f ? reach : DEFENSE_REACH_UNARMED;
}

static void collectDefensePositions( Object *obj, void *userData )
{
	if( obj->isKindOf( KINDOF_FS_BASE_DEFENSE ) && !obj->isEffectivelyDead() )
		((std::vector<Coord3D> *)userData)->push_back( *obj->getPosition() );
}

/** Every defence of this player's standing, going up or queued on the build list. */
static void collectGuns( Player *player, std::vector<Coord3D> &guns )
{
	player->iterateObjects( collectDefensePositions, &guns );
	for( BuildListInfo *info = player->getBuildList(); info; info = info->getNext() )
	{
		const ThingTemplate *planned = info->isPriorityBuild() && info->getObjectID() == INVALID_ID
			? TheThingFactory->findTemplate( info->getTemplateName(), FALSE ) : NULL;
		if( planned && planned->isKindOf( KINDOF_FS_BASE_DEFENSE ) )
			guns.push_back( *info->getLocation() );
	}
}

/** The Turtle's line.  Its guns go on rings round a choke instead of round the base, at these shares
	* of their own reach, as close as this share of the reach to each other, and no further out past the
	* choke than this share of it, so the rows stand staggered on our side of the pass and cover each
	* other.  The choke with the fewest guns within TURTLE_CHOKE_GUARD gets the next one. */
static const Real TURTLE_SPACING = 0.3f;
static const Real TURTLE_FRONT = 0.25f;
static const Real TURTLE_CHOKE_GUARD = 500.0f;
/** Chokes fortified, the share of Turtle's own defence allowance it stands, the guns at a choke that
	* buy the mine upgrade and the demo traps laid in front of the line, by rung. */
static const Int TURTLE_CHOKES[ AISKILL_COUNT ] = { 1, 2, 3 };
static const Real TURTLE_DEFENSE_SCALE[ AISKILL_COUNT ] = { 0.4f, 0.7f, 1.0f };
static const Int TURTLE_MINED_GUNS[ AISKILL_COUNT ] = { 2, 4, 8 };
static const Int TURTLE_DEMO_TRAPS[ AISKILL_COUNT ] = { 2, 4, 6 };
/** Where the chokes are looked for, in base radii from the base by the ground, and never past this
	* share of the road to the enemy.  Two enemies whose chokes lie closer than TURTLE_CHOKE_MERGE share one. */
static const Real TURTLE_CHOKE_NEAR = 1.0f;
static const Real TURTLE_CHOKE_FAR = 2.5f;
static const Real TURTLE_CHOKE_ROAD_SHARE = 0.45f;
static const Real TURTLE_CHOKE_MERGE = 300.0f;

//----------------------------------------------------------------------------------------------------------
Int AIPlayer::enemyStarts( Coord3D *threats )
{
	Int threatCount = 0;
	for( Int i = 0; i < ThePlayerList->getPlayerCount() && threatCount < MAX_PLAYER_COUNT; ++i )
	{
		Player *them = ThePlayerList->getNthPlayer( i );
		if( them == m_player || m_player->getRelationship( them->getDefaultTeam() ) != ENEMIES || !them->hasAnyObjects() )
			continue;
		if( enemyStartGuess( i, &threats[ threatCount ] ) )
			++threatCount;
	}
	return threatCount;
}

//----------------------------------------------------------------------------------------------------------
/** Every start an enemy may still be at: the ones a scout found him at, and while an enemy is not
	* placed yet every start nobody has looked at.  *known, when asked, counts the found ones, which come
	* first. */
//----------------------------------------------------------------------------------------------------------
Int AIPlayer::possibleEnemyStarts( Coord3D *starts, Int *known )
{
	Bool everyonePlaced = TRUE;
	for( Int i = 0; i < ThePlayerList->getPlayerCount() && i < MAX_PLAYER_COUNT; ++i )
	{
		Player *them = ThePlayerList->getNthPlayer( i );
		if( them != m_player && m_player->getRelationship( them->getDefaultTeam() ) == ENEMIES && them->hasAnyObjects() &&
				m_playerStartNdx[ i ] < 0 )
			everyonePlaced = FALSE;
	}
	Int count = 0;
	for( Int startNdx = 0; startNdx < MAX_PLAYER_COUNT; ++startNdx )
		if( m_startOccupied[ startNdx ] && startPositionLoc( startNdx, &starts[ count ] ) )
			++count;
	if( known )
		*known = count;
	for( Int startNdx = 0; startNdx < MAX_PLAYER_COUNT && !everyonePlaced; ++startNdx )
		if( !m_startChecked[ startNdx ] && startPositionLoc( startNdx, &starts[ count ] ) )
			++count;
	return count;
}

//----------------------------------------------------------------------------------------------------------
/** ZH has no wall to build, so the Turtle's wall is its guns packed across the narrowest place on each
	* enemy's road in (aiFindChoke), a base radius or two out.  Flooded once per enemy, and again only
	* when the base center, an enemy's address or a bridge changes. */
//----------------------------------------------------------------------------------------------------------
const std::vector<Coord3D> &AIPlayer::turtleChokes( void )
{
	if( !m_baseCenterSet )
	{
		m_chokes.clear();
		return m_chokes;
	}
	// every start an enemy may still be at, not only the best guess: on a map with more starts than
	// players the guess pointed at an empty one for most of the match, and the line moved when a scout
	// found the real one
	Coord3D threats[ MAX_PLAYER_COUNT ];
	const Int threatCount = possibleEnemyStarts( threats, NULL );
	const UnsignedInt bridges = bridgesDownKey();
	Bool same = (Int)m_chokeThreatsAt.size() == threatCount && threatCount > 0 && m_chokeBridges == bridges &&
		m_chokeBaseAt.x == m_baseCenter.x && m_chokeBaseAt.y == m_baseCenter.y;
	for( Int i = 0; same && i < threatCount; ++i )
		same = m_chokeThreatsAt[ i ].x == threats[ i ].x && m_chokeThreatsAt[ i ].y == threats[ i ].y;
	if( same )
		return m_chokes;

	m_chokeThreatsAt.assign( threats, threats + threatCount );
	m_chokeBridges = bridges;
	m_chokeBaseAt = m_baseCenter;
	m_chokes.clear();
	std::vector<Int> roadLength;
	std::vector<Int> roads;
	const std::vector<UnsignedShort> &fromBase = groundFromBase();
	Int width, height;
	groundGridSize( &width, &height );
	const Real WORLD_TO_GROUND = AI_GROUND_STEP / PATHFIND_CELL_SIZE_F;
	for( Int t = 0; t < threatCount; ++t )
	{
		std::vector<UnsignedShort> fromThreat;
		floodWholeGrid( &threats[ t ], 1, fromThreat );
		const Int route = groundAt( fromThreat, &m_baseCenter );
		if( route == AI_GROUND_UNREACHED )
			continue;
		const Int nearest = REAL_TO_INT( TURTLE_CHOKE_NEAR * m_baseRadius * WORLD_TO_GROUND );
		Int farthest = REAL_TO_INT( TURTLE_CHOKE_FAR * m_baseRadius * WORLD_TO_GROUND );
		if( farthest > REAL_TO_INT( TURTLE_CHOKE_ROAD_SHARE * INT_TO_REAL( route ) ) )
			farthest = REAL_TO_INT( TURTLE_CHOKE_ROAD_SHARE * INT_TO_REAL( route ) );
		Int roadCells = 0;
		const Int cell = aiFindChoke( &fromBase[ 0 ], &fromThreat[ 0 ], width, width * height, route, nearest, farthest, &roadCells );
		if( cell < 0 )
			continue;
		Coord3D choke;
		choke.x = ( INT_TO_REAL( cell % width ) + 0.5f ) * PATHFIND_CELL_SIZE_F;
		choke.y = ( INT_TO_REAL( cell / width ) + 0.5f ) * PATHFIND_CELL_SIZE_F;
		choke.z = 0.0f;
		DEBUG_LOG(("AI CHOKE frame %d player %d finds the road from (%.0f,%.0f) narrowest at (%.0f,%.0f), %d road cells, %.0f from the base by the ground\n",
			TheGameLogic->getFrame(), m_player->getPlayerIndex(), threats[ t ].x, threats[ t ].y, choke.x, choke.y, roadCells,
			INT_TO_REAL( fromBase[ cell ] ) / WORLD_TO_GROUND));
		Bool merged = FALSE;
		for( size_t c = 0; c < m_chokes.size() && !merged; ++c )
			if( sqr( m_chokes[ c ].x - choke.x ) + sqr( m_chokes[ c ].y - choke.y ) < sqr( TURTLE_CHOKE_MERGE ) )
			{
				merged = TRUE;
				++roads[ c ];
				if( route < roadLength[ c ] )
					roadLength[ c ] = route;
			}
		if( merged )
			continue;
		m_chokes.push_back( choke );
		roadLength.push_back( route );
		roads.push_back( 1 );
	}

	// the choke most roads in go through first, then the nearest enemy's, the earlier of two alike: with
	// seven starts still open the guess nearest by the road was an empty one as often as not
	for( size_t i = 1; i < m_chokes.size(); ++i )
		for( size_t j = i; j > 0 && ( roads[ j ] > roads[ j - 1 ] || ( roads[ j ] == roads[ j - 1 ] && roadLength[ j ] < roadLength[ j - 1 ] ) ); --j )
		{
			std::swap( m_chokes[ j ], m_chokes[ j - 1 ] );
			std::swap( roads[ j ], roads[ j - 1 ] );
			std::swap( roadLength[ j ], roadLength[ j - 1 ] );
		}
	return m_chokes;
}

//----------------------------------------------------------------------------------------------------------
Int AIPlayer::guardedChokes( void )
{
	const Int chokes = (Int)turtleChokes().size();
	return chokes < TURTLE_CHOKES[ m_skillLevel ] ? chokes : TURTLE_CHOKES[ m_skillLevel ];
}

//----------------------------------------------------------------------------------------------------------
Real AIPlayer::nearestChokeDistance( const Coord3D *pos )
{
	const Int guarded = guardedChokes();
	const std::vector<Coord3D> &chokes = turtleChokes();
	Real nearestSqr = -1.0f;
	for( Int c = 0; c < guarded; ++c )
	{
		const Real d = sqr( chokes[ c ].x - pos->x ) + sqr( chokes[ c ].y - pos->y );
		if( nearestSqr < 0.0f || d < nearestSqr )
			nearestSqr = d;
	}
	return nearestSqr < 0.0f ? -1.0f : (Real)sqrt( nearestSqr );
}

//----------------------------------------------------------------------------------------------------------
/** A Turtle's gun goes to the choke with the fewest guns, and round the base as before when none is
	* found or no spot round it is legal. */
//----------------------------------------------------------------------------------------------------------
Bool AIPlayer::placeDefense( const ThingTemplate *defense )
{
	if( m_persona == AIPERSONA_TURTLE && m_baseCenterSet )
	{
		const Int guarded = guardedChokes();
		const std::vector<Coord3D> &chokes = turtleChokes();
		std::vector<Coord3D> guns;
		collectGuns( m_player, guns );
		Int pick = -1;
		Int pickGuns = 0;
		for( Int c = 0; c < guarded; ++c )
		{
			Int gunsNear = 0;
			for( std::vector<Coord3D>::const_iterator g = guns.begin(); g != guns.end(); ++g )
				if( sqr( g->x - chokes[ c ].x ) + sqr( g->y - chokes[ c ].y ) < sqr( TURTLE_CHOKE_GUARD ) )
					++gunsNear;
			if( pick < 0 || gunsNear < pickGuns )
			{
				pick = c;
				pickGuns = gunsNear;
			}
		}
		if( pick >= 0 )
		{
			const Coord3D choke = chokes[ pick ];
			if( placeDefenseAround( defense, &choke ) )
				return TRUE;
		}
	}
	return placeDefenseAround( defense, NULL );
}

/** The mine upgrade a building of China's lays round itself is an object upgrade on its own buttons,
	* the Nuke General's EMP mines included; this is in both their names. */
static const char *const TURTLE_MINE_MARK = "Mines";
/** Demo traps go out in front of a choke on rings this far from it, this far apart, and the best few
	* by how near the enemy's road they lie get a legality check. */
static const Int TURTLE_TRAP_RINGS = 3;
static const Real TURTLE_TRAP_RING[ TURTLE_TRAP_RINGS ] = { 100.0f, 180.0f, 260.0f };
static const Int TURTLE_TRAP_ANGLES = 16;
static const Real TURTLE_TRAP_SPACING = 80.0f;
static const Int TURTLE_TRAP_TRIES = 8;

static void collectFinishedDefenses( Object *obj, void *userData )
{
	if( obj->isKindOf( KINDOF_FS_BASE_DEFENSE ) && !obj->isEffectivelyDead() && !obj->getStatusBits().test( OBJECT_STATUS_UNDER_CONSTRUCTION ) )
		((std::vector<Object *> *)userData)->push_back( obj );
}

static void collectDemoTraps( Object *obj, void *userData )
{
	if( obj->isKindOf( KINDOF_DEMOTRAP ) && !obj->isEffectivelyDead() )
		((std::vector<Coord3D> *)userData)->push_back( *obj->getPosition() );
}

static const UpgradeTemplate *mineUpgradeOn( const Object *obj )
{
	const CommandSet *commandSet = TheCommandStore->findCommandSet( obj->getCommandSetString() );
	if( commandSet == NULL )
		return NULL;
	for( Int i = 0; i < MAX_COMMANDS_PER_SET; ++i )
	{
		const CommandButton *button = commandSet->getCommandButton( i );
		if( button && button->getCommandType() == GUI_COMMAND_OBJECT_UPGRADE && button->getUpgradeTemplate() &&
				strstr( button->getUpgradeTemplate()->getUpgradeName().str(), TURTLE_MINE_MARK ) != NULL )
			return button->getUpgradeTemplate();
	}
	return NULL;
}

//----------------------------------------------------------------------------------------------------------
/** In front of the Turtle's line: China's guns at a choke buy the mines their own buttons offer, and a
	* GLA dozer lays demo traps on the road just past the choke, one a pass, up to the rung's count.  The
	* USA has neither, and leans on Fire Bases instead (nextBaseDefense). */
//----------------------------------------------------------------------------------------------------------
void AIPlayer::fortifyChokes( Object *dozer )
{
	const Int guarded = guardedChokes();
	if( guarded == 0 )
		return;
	const std::vector<Coord3D> &chokes = turtleChokes();

	std::vector<Object *> defenses;
	m_player->iterateObjects( collectFinishedDefenses, &defenses );
	Int mined = 0;
	Object *unmined = NULL;
	const UpgradeTemplate *unminedUpgrade = NULL;
	for( size_t d = 0; d < defenses.size(); ++d )
	{
		const Real distance = nearestChokeDistance( defenses[ d ]->getPosition() );
		const UpgradeTemplate *mines = mineUpgradeOn( defenses[ d ] );
		if( distance < 0.0f || distance > TURTLE_CHOKE_GUARD || mines == NULL )
			continue;
		if( defenses[ d ]->hasUpgrade( mines ) || defenses[ d ]->getProductionUpdateInterface()->isUpgradeInQueue( mines ) )
			++mined;
		else if( unmined == NULL )
		{
			unmined = defenses[ d ];
			unminedUpgrade = mines;
		}
	}
	if( unmined && mined < TURTLE_MINED_GUNS[ m_skillLevel ] && buyUpgrade( unmined->getProductionUpdateInterface(), unminedUpgrade ) )
		DEBUG_LOG(("AI TURTLE frame %d player %d mines '%s' at (%.0f,%.0f), %.0f from its choke, %d mined before\n",
			TheGameLogic->getFrame(), m_player->getPlayerIndex(), unmined->getTemplate()->getName().str(), unmined->getPosition()->x,
			unmined->getPosition()->y, nearestChokeDistance( unmined->getPosition() ), mined));

	const ThingTemplate *trap = buildableOfKind( dozer, GUI_COMMAND_DOZER_CONSTRUCT, KINDOF_DEMOTRAP, purchaseQueueOn() );
	if( trap == NULL || priorityBuildPending( m_player, trap ) )
		return;
	std::vector<Coord3D> traps;
	m_player->iterateObjects( collectDemoTraps, &traps );
	if( (Int)traps.size() >= TURTLE_DEMO_TRAPS[ m_skillLevel ] )
		return;

	// past the choke by the ground, the spots nearest the enemy's road first
	const Coord3D &choke = chokes[ traps.size() % guarded ];
	Coord3D threats[ MAX_PLAYER_COUNT ];
	const Int threatCount = enemyStarts( threats );
	if( threatCount == 0 )
		return;
	const std::vector<UnsignedShort> &fromBase = groundFromBase();
	const std::vector<UnsignedShort> &fromThreat = groundFromThreats( threats, threatCount );
	const Int chokeGround = groundAt( fromBase, &choke );
	const Int SPOTS = TURTLE_TRAP_RINGS * TURTLE_TRAP_ANGLES;
	Coord3D spot[ SPOTS ];
	Int road[ SPOTS ];
	Int order[ SPOTS ];
	Int candidates = 0;
	for( Int s = 0; s < SPOTS; ++s )
	{
		const Real angle = 2.0f * PI * ( s % TURTLE_TRAP_ANGLES ) / TURTLE_TRAP_ANGLES;
		const Real radius = TURTLE_TRAP_RING[ s / TURTLE_TRAP_ANGLES ];
		Coord3D at;
		at.x = choke.x + radius * Cos( angle );
		at.y = choke.y + radius * Sin( angle );
		at.z = 0.0f;
		ICoord2D cell;
		TheAI->pathfinder()->worldToCell( &at, &cell );
		const PathfindCell *pathCell = TheAI->pathfinder()->getCell( LAYER_GROUND, cell.x, cell.y );
		if( pathCell == NULL || pathCell->getType() != PathfindCell::CELL_CLEAR )
			continue;		// off the map or not ground anything drives over
		const Int b = groundAt( fromBase, &at );
		const Int t = groundAt( fromThreat, &at );
		if( b == AI_GROUND_UNREACHED || t == AI_GROUND_UNREACHED || b <= chokeGround )
			continue;
		Bool crowded = FALSE;
		for( size_t k = 0; k < traps.size() && !crowded; ++k )
			crowded = sqr( traps[ k ].x - at.x ) + sqr( traps[ k ].y - at.y ) < sqr( TURTLE_TRAP_SPACING );
		if( crowded )
			continue;
		spot[ candidates ] = at;
		road[ candidates ] = b + t;
		order[ candidates ] = candidates;
		++candidates;
	}
	for( Int i = 1; i < candidates; ++i )
		for( Int j = i; j > 0 && road[ order[ j ] ] < road[ order[ j - 1 ] ]; --j )
		{
			const Int swap = order[ j ];
			order[ j ] = order[ j - 1 ];
			order[ j - 1 ] = swap;
		}
	const Real placeAngle = trap->getPlacementViewAngle();
	for( Int n = 0; n < candidates && n < TURTLE_TRAP_TRIES; ++n )
	{
		Coord3D at = spot[ order[ n ] ];
		const Bool legal = TheBuildAssistant->isLocationLegalToBuild( &at, trap, placeAngle,
											BuildAssistant::CLEAR_PATH | BuildAssistant::TERRAIN_RESTRICTIONS | BuildAssistant::NO_OBJECT_OVERLAP,
											NULL, m_player ) == LBC_OK;
		TheTerrainVisual->removeAllBibs();	// isLocationLegalToBuild adds bib feedback
		if( !legal || !isLocationSafe( &at, trap ) )
			continue;
		DEBUG_LOG(("AI TURTLE frame %d player %d lays '%s' at (%.0f,%.0f), %.0f in front of its choke at (%.0f,%.0f), %d laid before\n",
			TheGameLogic->getFrame(), m_player->getPlayerIndex(), trap->getName().str(), at.x, at.y,
			sqrt( sqr( at.x - choke.x ) + sqr( at.y - choke.y ) ), choke.x, choke.y, (Int)traps.size()));
		m_player->addToPriorityBuildList( trap->getName(), &at, placeAngle );
		return;
	}
	DEBUG_LOG(("AI TURTLE frame %d player %d has no spot for '%s' in front of its choke at (%.0f,%.0f), %d spots looked at\n",
		TheGameLogic->getFrame(), m_player->getPlayerIndex(), trap->getName().str(), choke.x, choke.y, candidates));
}

/** Rings round the base center a defence may go on, in base radii, and spots per ring. */
static const Int DEFENSE_RINGS = 4;
static const Real DEFENSE_RING_RADII[ DEFENSE_RINGS ] = { 0.75f, 1.0f, 1.25f, 1.5f };
/** The same round a Turtle's choke, in shares of the defence's own reach. */
static const Real TURTLE_RING_REACH[ DEFENSE_RINGS ] = { 0.15f, 0.3f, 0.45f, 0.6f };
static const Int DEFENSE_SPOTS_PER_RING = 24;
/** Ground looked at round each spot: this many directions, at half and nine tenths of the reach. */
static const Int DEFENSE_FIRE_SAMPLES = 16;
/** Best-scored spots asked for legality, each about a millisecond. */
static const Int DEFENSE_PLACE_TRIES = 12;
/** Ground counts as the enemy's road in when the drive from him through it to the base center is no
	* more than this much longer than his shortest one, and a sample on it counts this much more. */
static const Real ROUTE_SLACK = 1.15f;
static const Real ROUTE_BONUS = 1.0f;

//----------------------------------------------------------------------------------------------------------
/** A defence goes where its guns see the most of the ground an enemy comes over.  A spot on a ring
	* round the base is scored by the open ground inside its reach: each sample point on clear,
	* drivable ground counts for how much nearer the enemy it lies than the middle of the base does
	* (aiFireSampleWeight), plus a little for being outside the base at all, and counts nothing when a building of ours or a cliff stands
	* between it and the spot.  Nearer is by the ground, off groundFromThreats: measured in a straight
	* line, the cliff face behind a base looked like its front door, and the guns went up facing a
	* mountain while the enemy drove in round the side.  A sample on the enemy's shortest drive in
	* (ROUTE_SLACK of it) counts ROUTE_BONUS more, so the guns cover the road he takes, and a spot the
	* base cannot drive to is not scored at all.  Every defence already standing or queued within three quarters of the
	* reach divides the score, so the guns spread along the front instead of stacking.  The enemy is
	* where enemyStartGuess says, the same knowledge every other decision here uses.  The spot that went
	* up in front of a fixed point used to land behind the base's own buildings on a crowded map, and a
	* missed spot fell back to the middle of the base; one with no score is not built at all now. */
//----------------------------------------------------------------------------------------------------------
Bool AIPlayer::placeDefenseAround( const ThingTemplate *defense, const Coord3D *choke )
{
	if( !m_baseCenterSet )
		return FALSE;

	Coord3D threats[ MAX_PLAYER_COUNT ];
	Int threatCount = enemyStarts( threats );
	if( threatCount == 0 )
	{
		Coord3D dir;
		if( !enemyDirection( &dir ) )
			return FALSE;
		threats[ 0 ].x = m_baseCenter.x + dir.x * 4.0f * m_baseRadius;
		threats[ 0 ].y = m_baseCenter.y + dir.y * 4.0f * m_baseRadius;
		threats[ 0 ].z = 0.0f;
		threatCount = 1;
	}

	// an enemy who cannot drive here (an island, a cliff top) is weighed in a straight line as before
	const std::vector<UnsignedShort> &fromBase = groundFromBase();
	const std::vector<UnsignedShort> &fromThreat = groundFromThreats( threats, threatCount );
	const Int routeLength = groundAt( fromThreat, &m_baseCenter );
	const Bool byGround = routeLength != AI_GROUND_UNREACHED;
	const Real GROUND_TO_WORLD = PATHFIND_CELL_SIZE_F / AI_GROUND_STEP;

	std::vector<Coord3D> guns;
	collectGuns( m_player, guns );

	const Real reach = defenseReach( defense );
	const Real spacingSqr = sqr( ( choke ? TURTLE_SPACING : 0.75f ) * reach );
	const Coord3D ringCenter = choke ? *choke : m_baseCenter;
	const Int chokeGround = choke ? groundAt( fromBase, choke ) : 0;
	const Int frontGround = REAL_TO_INT( TURTLE_FRONT * reach / GROUND_TO_WORLD );
	const Real footprint = defense->getTemplateGeometryInfo().getBoundingCircleRadius();
	const Real footprintSqr = sqr( 2.0f * footprint );
	Region3D extent;
	TheTerrainLogic->getMaximumPathfindExtent( &extent );

	const Int SPOTS = DEFENSE_RINGS * DEFENSE_SPOTS_PER_RING;
	Coord3D spot[ SPOTS ];
	Real score[ SPOTS ];
	Int clear[ SPOTS ];
	Int blocked[ SPOTS ];
	Int order[ SPOTS ];
	for( Int s = 0; s < SPOTS; ++s )
	{
		const Real angle = 2.0f * PI * ( s % DEFENSE_SPOTS_PER_RING ) / DEFENSE_SPOTS_PER_RING;
		const Real radius = choke ? TURTLE_RING_REACH[ s / DEFENSE_SPOTS_PER_RING ] * reach
															: DEFENSE_RING_RADII[ s / DEFENSE_SPOTS_PER_RING ] * m_baseRadius;
		spot[ s ].x = ringCenter.x + radius * Cos( angle );
		spot[ s ].y = ringCenter.y + radius * Sin( angle );
		spot[ s ].z = 0.0f;
		score[ s ] = 0.0f;
		clear[ s ] = 0;
		blocked[ s ] = 0;
		order[ s ] = s;
		if( spot[ s ].x < extent.lo.x || spot[ s ].x > extent.hi.x || spot[ s ].y < extent.lo.y || spot[ s ].y > extent.hi.y )
			continue;

		// a spot that cannot be legal is not worth a legality check: on a building, a cliff or water,
		// or on top of a gun already there or queued.  Without this 11 of every 12 checks failed, most
		// of them on restricted terrain under the footprint's edge, so the corners are looked at too
		{
			if( !footprintOnClearGround( &spot[ s ], footprint ) )
				continue;
			// a ledge or the far side of a valley the base cannot drive to guards nothing the enemy drives over
			if( groundAt( fromBase, &spot[ s ] ) == AI_GROUND_UNREACHED )
				continue;
			// the Turtle's line stands on our side of its pass
			if( choke && groundAt( fromBase, &spot[ s ] ) > chokeGround + frontGround )
				continue;
			// the rest failed on the clearance kept round another building, ours or anybody's
			PartitionFilterAcceptByKindOf structuresOnly( MAKE_KINDOF_MASK( KINDOF_STRUCTURE ), KINDOFMASK_NONE );
			PartitionFilter *structureFilters[] = { &structuresOnly, NULL };
			if( ThePartitionManager->getClosestObject( &spot[ s ], footprint + PATHFIND_CELL_SIZE_F * 2.0f,
						FROM_BOUNDINGSPHERE_2D, structureFilters ) != NULL )
				continue;
			Bool onGun = FALSE;
			for( std::vector<Coord3D>::const_iterator g = guns.begin(); g != guns.end() && !onGun; ++g )
				onGun = sqr( g->x - spot[ s ].x ) + sqr( g->y - spot[ s ].y ) < footprintSqr;
			if( onGun )
				continue;
			// and ground the build rules refuse - a tile marked unbuildable, a slope - fails most of
			// what is left, and the terrain half of the legality test alone is a footprint walk
			if( TheBuildAssistant->isLocationLegalToBuild( &spot[ s ], defense, defense->getPlacementViewAngle(),
						BuildAssistant::TERRAIN_RESTRICTIONS, NULL, m_player ) != LBC_OK )
				continue;
			// an enemy within reach of the dozer: on a small map the front ring sat that close to the
			// enemy's own base, and the best spots were refused on every pass for the whole match
			if( !isLocationSafe( &spot[ s ], defense ) )
				continue;
		}


		for( Int k = 0; k < 2 * DEFENSE_FIRE_SAMPLES; ++k )
		{
			const Real sampleAngle = 2.0f * PI * ( k % DEFENSE_FIRE_SAMPLES ) / DEFENSE_FIRE_SAMPLES;
			const Real sampleReach = ( k < DEFENSE_FIRE_SAMPLES ? 0.5f : 0.9f ) * reach;
			Coord3D at;
			at.x = spot[ s ].x + sampleReach * Cos( sampleAngle );
			at.y = spot[ s ].y + sampleReach * Sin( sampleAngle );
			at.z = 0.0f;

			ICoord2D cell;
			TheAI->pathfinder()->worldToCell( &at, &cell );
			const PathfindCell *pathCell = TheAI->pathfinder()->getCell( LAYER_GROUND, cell.x, cell.y );
			if( pathCell == NULL || pathCell->getType() != PathfindCell::CELL_CLEAR )
				continue;		// not ground anything drives over

			// measured against the enemy nearest this ground, from the middle of the base: ground the
			// enemy has to cross to reach us, not merely ground in front of this spot
			const Bool outsideBase = sqr( at.x - m_baseCenter.x ) + sqr( at.y - m_baseCenter.y ) > sqr( m_baseRadius );
			Real weight;
			if( byGround )
			{
				const Int index = cell.y * ( TheAI->pathfinder()->getExtent()->x + 1 ) + cell.x;
				const Int sampleToThreat = fromThreat[ index ];
				if( sampleToThreat == AI_GROUND_UNREACHED )
					continue;		// a pocket the enemy cannot drive into
				weight = aiFireSampleWeight( routeLength * GROUND_TO_WORLD, sampleToThreat * GROUND_TO_WORLD, reach, outsideBase );
				if( fromBase[ index ] != AI_GROUND_UNREACHED && sampleToThreat + fromBase[ index ] <= routeLength * ROUTE_SLACK )
					weight += ROUTE_BONUS;
			}
			else
			{
				Real sampleToThreat = -1.0f;
				Int nearest = 0;
				for( Int t = 0; t < threatCount; ++t )
				{
					const Real d = sqrt( sqr( threats[ t ].x - at.x ) + sqr( threats[ t ].y - at.y ) );
					if( sampleToThreat < 0.0f || d < sampleToThreat )
					{
						sampleToThreat = d;
						nearest = t;
					}
				}
				const Real baseToThreat = sqrt( sqr( threats[ nearest ].x - m_baseCenter.x ) + sqr( threats[ nearest ].y - m_baseCenter.y ) );
				weight = aiFireSampleWeight( baseToThreat, sampleToThreat, reach, outsideBase );
			}
			if( weight <= 0.0f )
				continue;
			if( !groundLineClear( &spot[ s ], &at ) )
			{
				++blocked[ s ];
				continue;
			}
			++clear[ s ];
			score[ s ] += weight;
		}

		Int neighbours = 0;
		for( std::vector<Coord3D>::const_iterator g = guns.begin(); g != guns.end(); ++g )
			if( sqr( g->x - spot[ s ].x ) + sqr( g->y - spot[ s ].y ) < spacingSqr )
				++neighbours;
		score[ s ] /= INT_TO_REAL( 1 + neighbours );
	}

	// best first, the lower index of two alike
	for( Int i = 1; i < SPOTS; ++i )
		for( Int j = i; j > 0 && score[ order[ j ] ] > score[ order[ j - 1 ] ]; --j )
		{
			const Int swap = order[ j ];
			order[ j ] = order[ j - 1 ];
			order[ j - 1 ] = swap;
		}

	const Real placeAngle = defense->getPlacementViewAngle();
	Int illegal = 0;
	LegalBuildCode lastCode = LBC_OK;
	// a pass whose best dozen all failed asked the same dozen again ten seconds later, all match long.
	// Half the tries go to the best spots, the other half to a window further down the ranking that
	// moves on every pass, round to the top again every fourth
	const Int HALF = DEFENSE_PLACE_TRIES / 2;
	const Int window = HALF + (Int)( ( TheGameLogic->getFrame() / ECONOMY_CHECK_RATE ) % 4 ) * HALF;
	for( Int t = 0; t < DEFENSE_PLACE_TRIES; ++t )
	{
		const Int n = t < HALF ? t : window + t - HALF;
		if( n >= SPOTS )
			break;
		const Int s = order[ n ];
		if( score[ s ] <= 0.0f )
			continue;		// ranked after every spot worth anything; the window may have run into them
		const LegalBuildCode code = TheBuildAssistant->isLocationLegalToBuild( &spot[ s ], defense, placeAngle,
											BuildAssistant::CLEAR_PATH | BuildAssistant::TERRAIN_RESTRICTIONS | BuildAssistant::NO_OBJECT_OVERLAP,
											NULL, m_player );
		TheTerrainVisual->removeAllBibs();	// isLocationLegalToBuild adds bib feedback
		if( code != LBC_OK )
		{
			++illegal;
			lastCode = code;
		}
		else
		{
			DEBUG_LOG(("AI DEFENSE frame %d player %d places '%s' at (%.0f,%.0f), rank %d, score %.2f, fire clear %d blocked %d, %d guns near\n",
				TheGameLogic->getFrame(), m_player->getPlayerIndex(), defense->getName().str(), spot[ s ].x, spot[ s ].y, n + 1,
				score[ s ], clear[ s ], blocked[ s ], (Int)guns.size()));
			if( choke )
				DEBUG_LOG(("AI TURTLE frame %d player %d puts '%s' at (%.0f,%.0f), %.0f from its choke at (%.0f,%.0f)\n",
					TheGameLogic->getFrame(), m_player->getPlayerIndex(), defense->getName().str(), spot[ s ].x, spot[ s ].y,
					sqrt( sqr( spot[ s ].x - choke->x ) + sqr( spot[ s ].y - choke->y ) ), choke->x, choke->y));
			m_player->addToPriorityBuildList( defense->getName(), &spot[ s ], placeAngle );
			return TRUE;
		}
	}
	DEBUG_LOG(("AI DEFENSE frame %d player %d has no spot for '%s'%s, best score %.2f, fire clear %d blocked %d, %d illegal (last code %d)\n",
		TheGameLogic->getFrame(), m_player->getPlayerIndex(), defense->getName().str(), choke ? " round its choke" : "",
		score[ order[ 0 ] ], clear[ order[ 0 ] ], blocked[ order[ 0 ] ], illegal, (Int)lastCode));
	return FALSE;
}

/** How loaded this player's factories of one kind are.  Counted off the objects, because a factory
	* bought past the build list is still a factory. */
struct FactoryLoad
{
	KindOfType kind;
	Int finished;
	Int constructing;
	Int busy;					///< finished ones with something in the queue, or that let a unit out since the last spend pass
	Int parkingFull;		///< finished ones with no parking place left, which refuse an aircraft to their queue
	const std::vector<ObjectID> *producedSincePass;		///< the factories that let a unit out since the last spend pass
};

/** The production kinds the computer buys extra copies of, in m_factoryBackedUpSince's order. */
static const Int PRODUCTION_KINDS = 3;
static const KindOfType PRODUCTION_KIND[ PRODUCTION_KINDS ] = { KINDOF_FS_WARFACTORY, KINDOF_FS_BARRACKS, KINDOF_FS_AIRFIELD };

/** How deep a spending personality queues its surplus behind what a factory already builds, by rung. */
static const Int PERSONA_SURPLUS_DEPTH[ AISKILL_COUNT ] = { 2, 2, 3 };
/** How many factories of its favoured kind a personality with a unit taste stands without waiting for
	* a backlog, by rung. */
static const Int PERSONA_FAVOURED_FACTORIES[ AISKILL_COUNT ] = { 2, 3, 4 };
/** How many vehicles a Raider sortie takes, by rung (AIPlayer::doPersonaRaids); it keeps twice that
	* many of its cheapest vehicle, the quickest it can buy (trainFavoured). */
static const Int RAID_PARTY[ AISKILL_COUNT ] = { 2, 3, 4 };
/** When the first sortie goes; the raiders are bought from a minute before. */
static const Int RAID_START_SECONDS[ AISKILL_COUNT ] = { 300, 240, 180 };

/** The PRODUCTION_KIND a personality's taste favours, -1 for none: aircraft the airfield, infantry and
	* stealth the barracks, vehicles the war factory. */
static Int personaFavouredKind( const AIPersonaWeights &taste )
{
	if( taste.m_air > 0.0f && taste.m_air >= taste.m_infantry && taste.m_air >= taste.m_vehicle )
		return 2;
	if( taste.m_infantry > 0.0f && taste.m_infantry >= taste.m_vehicle )
		return 1;
	if( taste.m_vehicle > 0.0f )
		return 0;
	return taste.m_stealth > 0.0f ? 1 : -1;
}

/** What one unit is to a personality's taste: above 0 a kind it likes, below a kind it does not. */
static Real unitTaste( const AIPersonaWeights &taste, const ThingTemplate *tmpl )
{
	const Bool air = tmpl->isKindOf( KINDOF_AIRCRAFT );
	const Bool infantry = !air && tmpl->isKindOf( KINDOF_INFANTRY );
	const Bool vehicle = !air && !infantry && tmpl->isKindOf( KINDOF_VEHICLE );
	return aiPersonaTeamBias( taste, FALSE, air ? 1.0f : 0.0f, infantry ? 1.0f : 0.0f, vehicle ? 1.0f : 0.0f,
		templateCanStealth( tmpl ) ? 1.0f : 0.0f );
}

/** What a unit's purchase score moves by for a personality that likes or dislikes its kind: a liked one
	* stays under a factory (at a tie the factory for it waited the whole match behind the units), a
	* disliked one waits behind the base defences and an upgrade. */
static const Int PERSONA_LIKED_SCORE = 10;
static const Int PERSONA_DISLIKED_SCORE = -25;
/** A favoured factory goes ahead of the power, dozers and gatherers: an Air AI's second airfield sat on
	* the list from 3.3 minutes to 15, held behind whatever the queue was saving for. */
static const Int PERSONA_FAVOURED_FACTORY_SCORE = 25;

Int AIPlayer::favouredFactoryScore( const ThingTemplate *tmpl ) const
{
	if( m_persona == AIPERSONA_NORMAL )
		return 0;
	// not ahead of the first supply center: an Infantry China put up its second barracks before it, and
	// its third at 2.5 minutes with 850 in the bank
	KindOfMaskType supply = KINDOFMASK_NONE;
	supply.set( KINDOF_FS_SUPPLY_CENTER );
	if( m_player->countObjects( supply, KINDOFMASK_NONE ) == 0 )
		return 0;
	const Int favoured = personaFavouredKind( personaKindTaste() );
	return favoured >= 0 && tmpl->isKindOf( PRODUCTION_KIND[ favoured ] ) ? PERSONA_FAVOURED_FACTORY_SCORE : 0;
}

static void countFactoryLoad( Object *obj, void *userData )
{
	FactoryLoad *load = (FactoryLoad *)userData;
	if( obj->isEffectivelyDead() || !obj->isKindOf( load->kind ) )
		return;
	if( obj->testStatus( OBJECT_STATUS_UNDER_CONSTRUCTION ) )
	{
		++load->constructing;
		return;
	}
	ProductionUpdateInterface *pu = obj->getProductionUpdateInterface();
	if( pu == NULL )
		return;
	// busy at this instant, or between two units: an Easy barracks that trains a Red Guard inside a pass
	// was found idle on most of them and never counted as busy
	const std::vector<ObjectID> &produced = *load->producedSincePass;
	if( pu->getProductionCount() > 0 || std::find( produced.begin(), produced.end(), obj->getID() ) != produced.end() )
		++load->busy;
	++load->finished;
	// the same question ProductionUpdate::canQueueCreateUnit asks before an aircraft goes in the queue;
	// the answer depends only on whether the thing asked about is made at a helipad, and the airfield
	// itself is not
	for( BehaviorModule **m = obj->getBehaviorModules(); *m; ++m )
	{
		ParkingPlaceBehaviorInterface *pp = (*m)->getParkingPlaceBehaviorInterface();
		if( pp && !pp->hasAvailableSpaceFor( obj->getTemplate() ) )
		{
			++load->parkingFull;
			break;
		}
	}
}

static const std::vector<ObjectID> s_noneProduced;

static FactoryLoad readFactoryLoad( Player *player, KindOfType kind, const std::vector<ObjectID> *producedSincePass = &s_noneProduced )
{
	FactoryLoad load = { kind, 0, 0, 0, 0, producedSincePass };
	player->iterateObjects( countFactoryLoad, &load );
	return load;
}

/** Whether every finished factory of the kind was full on this pass: a barracks or war factory busy
	* now or since the last pass, an airfield by its parking while a buy was refused for it within
	* AI_PARKING_WANT_FRAMES. */
static Bool factoryKindFull( const FactoryLoad &load, UnsignedInt parkingBlockedFrame )
{
	if( load.kind != KINDOF_FS_AIRFIELD )
		return aiQueueKindFull( load.finished, load.busy );
	const Bool wanted = parkingBlockedFrame != 0 && TheGameLogic->getFrame() - parkingBlockedFrame <= AI_PARKING_WANT_FRAMES;
	return aiAirfieldsFull( load.finished, load.parkingFull, wanted );
}

/** Factories of this kind the build list has yet to put up, which go up before any extra one; with
	* askedOnly, only the ones the AI asked for itself (a priority entry). */
static Int plannedOfKind( Player *player, KindOfType kind, Bool askedOnly = FALSE )
{
	Int planned = 0;
	for( BuildListInfo *info = player->getBuildList(); info; info = info->getNext() )
	{
		if( info->getObjectID() != INVALID_ID || !info->isBuildable() || ( askedOnly && !info->isPriorityBuild() ) )
			continue;
		const ThingTemplate *tmpl = TheThingFactory->findTemplate( info->getTemplateName(), FALSE );
		if( tmpl && tmpl->isKindOf( kind ) )
			++planned;
	}
	return planned;
}

struct TechCount
{
	KindOfType kind;
	Int standing;
	Int onTheWay;
};

static void countTechBuildings( Object *obj, void *userData )
{
	TechCount *tech = (TechCount *)userData;
	if( obj->isEffectivelyDead() || !obj->isKindOf( tech->kind ) )
		return;
	if( obj->testStatus( OBJECT_STATUS_UNDER_CONSTRUCTION ) )
		++tech->onTheWay;
	else
		++tech->standing;
}

/** Where this player's hackers go to work: into an internet center with room, or else the quiet side
	* of the base. */
struct HackerDuty
{
	Coord3D spot;
	Object *internetCenter;
};

static void findInternetCenterWithRoom( Object *obj, void *userData )
{
	HackerDuty *duty = (HackerDuty *)userData;
	if( duty->internetCenter || !obj->isKindOf( KINDOF_FS_INTERNET_CENTER ) || obj->isEffectivelyDead() ||
			obj->testStatus( OBJECT_STATUS_UNDER_CONSTRUCTION ) )
		return;
	const ContainModuleInterface *contain = obj->getContain();
	if( contain && contain->getContainCount() < contain->getContainMax() )
		duty->internetCenter = obj;
}

/** How near the quiet spot a hacker has to be before it sits down there. */
static const Real HACK_SPOT_RADIUS = 120.0f;


/** A hacker standing about is income nobody switched on.  It used to switch on wherever it stood,
	* which was the barracks' rally point, and a player watched China's hackers sit down at the front of
	* its base and get shot. */
static void putToHacking( Object *obj, void *userData )
{
	if( !obj->isKindOf( KINDOF_MONEY_HACKER ) || obj->isContained() || obj->isEffectivelyDead() )
		return;
	AIUpdateInterface *ai = obj->getAI();
	if( ai == NULL || !ai->isIdle() )
		return;

	const HackerDuty *duty = (const HackerDuty *)userData;
	if( duty->internetCenter )
	{
		ai->aiEnter( duty->internetCenter, CMD_FROM_AI );
		return;
	}
	const Real distSqr = sqr( obj->getPosition()->x - duty->spot.x ) + sqr( obj->getPosition()->y - duty->spot.y );
	if( distSqr > sqr( HACK_SPOT_RADIUS ) )
	{
		// spread round the spot by id, so a dozen of them do not queue for the same square
		const Real angle = 2.0f * PI * (obj->getID() % PLACEMENT_ANGLES) / PLACEMENT_ANGLES;
		Coord3D dest = duty->spot;
		dest.x += HACK_SPOT_RADIUS * 0.5f * Cos( angle );
		dest.y += HACK_SPOT_RADIUS * 0.5f * Sin( angle );
		ai->aiMoveToPosition( &dest, CMD_FROM_AI );
		return;
	}
	ai->aiHackInternet( CMD_FROM_AI );
}

/** The first dozer this player owns, busy or not. */
static void findAnyDozer( Object *obj, void *userData )
{
	Object **dozer = (Object **)userData;
	if( *dozer == NULL && obj->isKindOf( KINDOF_DOZER ) && !obj->isEffectivelyDead() )
		*dozer = obj;
}

struct DozerTally
{
	Int dozers;
	Int building;
};

static void tallyDozer( Object *obj, void *userData )
{
	DozerTally *tally = (DozerTally *)userData;
	if( !obj->isKindOf( KINDOF_DOZER ) || obj->isEffectivelyDead() || obj->getAI() == NULL )
		return;
	++tally->dozers;
	DozerAIInterface *dozerAI = obj->getAI()->getDozerAIInterface();
	if( dozerAI && dozerAI->isTaskPending( DOZER_TASK_BUILD ) )
		++tally->building;
}

/** A purchase of this kind is already on the build list and waiting for a dozer.  Asked per template
	* rather than for the whole list: the script's own plan keeps seven to twenty priority entries
	* waiting through most of a match, and a global "wait for the last one" never let anything through. */
static Bool priorityBuildPending( Player *player, const ThingTemplate *tmpl )
{
	for( BuildListInfo *info = player->getBuildList(); info; info = info->getNext() )
	{
		if( info->isPriorityBuild() && info->isBuildable() && info->getObjectID() == INVALID_ID &&
				info->getTemplateName() == tmpl->getName() )
			return TRUE;
	}
	return FALSE;
}

/** A plant of this player's is already going up.  One halfway built is the answer to the outage
	* arriving, and queueing a second one is a war factory this player will not be able to buy. */
static void findPowerUnderConstruction( Object *obj, void *userData )
{
	Bool *building = (Bool *)userData;
	if( obj->isKindOf( KINDOF_FS_POWER ) && !obj->isKindOf( KINDOF_CASH_GENERATOR )
			&& obj->testStatus( OBJECT_STATUS_UNDER_CONSTRUCTION ) )
		*building = TRUE;
}

/** What the army and the base are made of, standing or going up. */
struct BaseTally
{
	Int army;
	Int defenses;
	Int superweapons;
};

static void tallyObject( Object *obj, void *userData )
{
	BaseTally *tally = (BaseTally *)userData;
	if( obj->isEffectivelyDead() )
		return;
	if( obj->isKindOf( KINDOF_FS_BASE_DEFENSE ) )
		++tally->defenses;
	else if( obj->isKindOf( KINDOF_FS_SUPERWEAPON ) )
		++tally->superweapons;
	else if( !obj->isKindOf( KINDOF_STRUCTURE ) && !obj->isKindOf( KINDOF_DOZER ) && !obj->isKindOf( KINDOF_HARVESTER ) &&
					 !obj->isKindOf( KINDOF_MONEY_HACKER ) && obj->isAbleToAttack() )
		++tally->army;
}

static BaseTally tallyBase( Player *player )
{
	BaseTally tally = { 0, 0, 0 };
	player->iterateObjects( tallyObject, &tally );
	return tally;
}

/** The base defences that shoot up an Air AI stands before its aircraft outrank them, by rung: with
	* every dollar on airfields and jets a Hard USA Air stood none, and its Patriots sat on the list. */
static const Int AIR_AA_DEFENSES[ AISKILL_COUNT ] = { 1, 2, 3 };
/** ... and what lifts one of those over a jet (55 + the row's 20 + the liked 10) from the row's 45 - 15. */
static const Int AIR_AA_DEFENSE_SCORE = 60;

static Bool shootsUp( const ThingTemplate *tmpl )
{
	const WeaponTemplateSet *set = tmpl->getWeaponTemplateSets().empty() ? NULL : tmpl->findWeaponTemplateSet( WeaponSetFlags() );
	for( Int slot = 0; set && slot < WEAPONSLOT_COUNT; ++slot )
	{
		const WeaponTemplate *weapon = set->getNth( (WeaponSlotType)slot );
		if( weapon && ( weapon->getAntiMask() & WEAPON_ANTI_AIRBORNE_VEHICLE ) != 0 )
			return TRUE;
	}
	return FALSE;
}

Int AIPlayer::personaBuildingScore( const ThingTemplate *tmpl ) const
{
	if( m_persona == AIPERSONA_AIR && tmpl->isKindOf( KINDOF_FS_BASE_DEFENSE ) && shootsUp( tmpl ) &&
			tallyBase( m_player ).defenses < AIR_AA_DEFENSES[ m_skillLevel ] )
		return AIR_AA_DEFENSE_SCORE;
	return favouredFactoryScore( tmpl );
}

/** Money units that earn in the open: what a player owns before an internet center stands, and what
	it keeps beside the centers it has. */
static const Int MONEY_UNITS_IN_THE_OPEN = 4;

/** Fighting units for every money unit past that.  The hackers share the barracks queue with the
	infantry, and a Hard Tank General with no ratio spent two thirds of 20,000 frames on them. */
static const Int ARMY_PER_MONEY_UNIT = 4;

/** The money units a player has standing, and the seats its finished internet centers hold for them.
	Counted rather than tracked: a hacker dies, is captured, or walks into a transport, and a running
	tally would drift. */
struct MoneyUnitTally
{
	Int owned;
	Int seats;
};

static void tallyMoneyUnit( Object *obj, void *userData )
{
	MoneyUnitTally *tally = (MoneyUnitTally *)userData;
	if( obj->isEffectivelyDead() )
		return;
	if( obj->isKindOf( KINDOF_MONEY_HACKER ) )
		++tally->owned;
	else if( obj->isKindOf( KINDOF_FS_INTERNET_CENTER ) && !obj->testStatus( OBJECT_STATUS_UNDER_CONSTRUCTION ) )
		tally->seats += obj->getContain()->getContainMax();
}

/** Room grows with the army, so an AI that cannot field one does not buy an economy instead, and a
	late-game bank with forty tanks behind it buys ten.  The seats bound it: past the centers' seats
	plus the few in the open, a hacker is one more target sitting in the base. */
Int AIPlayer::moneyUnitRoom( void ) const
{
	MoneyUnitTally money = { 0, 0 };
	m_player->iterateObjects( tallyMoneyUnit, &money );
	const Int byArmy = max( MONEY_UNITS_IN_THE_OPEN, tallyBase( m_player ).army / ARMY_PER_MONEY_UNIT );
	return min( byArmy, money.seats + MONEY_UNITS_IN_THE_OPEN ) - money.owned;
}

//----------------------------------------------------------------------------------------------------------
/** Which way the trouble comes from, as a unit vector out of this base: towards the nearest enemy,
	* at the best address this player has for him.
	*
	* The nearest one, not the one it has decided to attack - those are different questions. An
	* attack target is picked on what it is worth and can be on the far side of the map, and a wall
	* built facing that has its back to the neighbour who is actually next door.
	*
	* enemyStartGuess is what answers "where is he": a base it has scouted, or the start position it
	* has not yet been able to rule out. Reading the enemy's object list directly would put a
	* defence line exactly where the enemy is standing without anybody having gone to look, which is
	* the map-reading this AI had taken out of it everywhere else. */
//----------------------------------------------------------------------------------------------------------
Bool AIPlayer::enemyDirection( Coord3D *dir )
{
	Coord3D target;
	Bool found = FALSE;
	Real bestDistanceSqr = 0.0f;

	for( Int i = 0; i < ThePlayerList->getPlayerCount(); ++i )
	{
		Player *them = ThePlayerList->getNthPlayer( i );
		if( them == m_player || m_player->getRelationship( them->getDefaultTeam() ) != ENEMIES )
			continue;
		if( !them->hasAnyObjects() )
			continue;

		Coord3D theirs;
		if( !enemyStartGuess( i, &theirs ) )
			continue;

		const Real distanceSqr = sqr( theirs.x - m_baseCenter.x ) + sqr( theirs.y - m_baseCenter.y );
		if( !found || distanceSqr < bestDistanceSqr )
		{
			target = theirs;
			bestDistanceSqr = distanceSqr;
			found = TRUE;
		}
	}

	if( !found )
	{
		// nobody left to face, so face the middle, which is the way every skirmish start faces
		Region3D bounds;
		TheTerrainLogic->getMaximumPathfindExtent( &bounds );
		target.x = bounds.lo.x + bounds.width() * 0.5f;
		target.y = bounds.lo.y + bounds.height() * 0.5f;
		target.z = 0;
	}

	dir->x = target.x - m_baseCenter.x;
	dir->y = target.y - m_baseCenter.y;
	dir->z = 0;
	if( dir->length() < 1.0f )
		return FALSE;

	dir->normalize();
	return TRUE;
}

//----------------------------------------------------------------------------------------------------------
/** Keeping the lights on, which the plan does not.  The build list carries a fixed number of power
	* plants and the scripts never add one, so a base that loses two of them to a raid sits
	* underpowered - radar dark, base defenses offline, the superweapon's clock stopped - until the
	* rebuild timer comes round, and then the rebuild goes back to the exact spot the raid already
	* knows the way to.  A plant is the cheapest building on the list and the one thing every other
	* building depends on, so it is worth buying one before the meter reads zero.
	*
	* Two decisions here.  Build when the margin is thin rather than when the power is already out,
	* because a plant takes time to go up and the base is disabled for all of it.  And put it behind
	* the base, on the far side from whoever this player is fighting, so the raid that comes for it
	* has to cross the whole base first - which is the half of the answer the rebuild timer can
	* never give. */
//----------------------------------------------------------------------------------------------------------
/** Less than one more building's draw left over, or already short. */
Bool AIPlayer::isPowerThin( void ) const
{
	const Energy *energy = m_player->getEnergy();
	return energy->getProduction() < energy->getConsumption() + POWER_RESERVE;
}

void AIPlayer::doPower( void )
{
	const Int phase = computeUpdatePhase( m_player->getPlayerIndex(), POWER_CHECK_RATE );
	if( (TheGameLogic->getFrame() + phase) % POWER_CHECK_RATE != 0 )
		return;

	if( !m_player->getCanBuildBase() || !m_baseCenterSet )
		return;

	if( !isPowerThin() )
		return;
	const Energy *energy = m_player->getEnergy();
	if( !energy->hasSufficientPower() )
		DEBUG_LOG(("AI POWER frame %d player %d dark, %d of %d, %d in the bank\n", TheGameLogic->getFrame(),
			m_player->getPlayerIndex(), energy->getProduction(), energy->getConsumption(), m_player->getMoney()->countMoney()));

	Bool building = FALSE;
	m_player->iterateObjects( findPowerUnderConstruction, &building );
	if( building )
		return;

	Object *dozer = NULL;
	m_player->iterateObjects( findAnyDozer, &dozer );
	if( dozer == NULL )
		return;

	// with the purchase queue the plant is asked for before the bank can pay, and the queue saves for it
	const ThingTemplate *tmpl = buildableOfKind( dozer, GUI_COMMAND_DOZER_CONSTRUCT, KINDOF_FS_POWER, purchaseQueueOn() );
	if( tmpl == NULL || tmpl->isKindOf( KINDOF_CASH_GENERATOR ) || priorityBuildPending( m_player, tmpl ) )
		return;

	Coord3D spot = m_baseCenter;
	Coord3D dir;
	if( enemyDirection( &dir ) )
	{
		spot.x -= dir.x * m_baseRadius * POWER_SETBACK;
		spot.y -= dir.y * m_baseRadius * POWER_SETBACK;
	}

	// the back of a grown base fills up.  the spot behind the base walks outward with it; a miss there
	// still tries the inner rings of the base itself, and that try does not move the outward walk
	if( !placeNear( tmpl, &spot, 0.0f, TRUE ) )
		placeNear( tmpl, &m_baseCenter, m_baseRadius, FALSE );
}

//----------------------------------------------------------------------------------------------------------
/** The drop zone and the black market: what the opening buys before the army leaves home. */
static const Int EARLY_INCOME_KINDS = 2;
static const KindOfType EARLY_INCOME[ EARLY_INCOME_KINDS ] = { KINDOF_FS_SUPPLY_DROPZONE, KINDOF_FS_BLACK_MARKET };

/** Early in the match the first drop zone and the first black market do not wait for the hoard: they
	* are asked for when none stands, and the purchase queue ranks them over the army and saves for them.
	* Behind the hoard alone a Hard AI went out to fight on two gatherers before it owned either. */
void AIPlayer::buyEarlyIncome( void )
{
	if( !purchaseQueueOn() || TheGameLogic->getFrame() >= AI_PURCHASE_EARLY_FRAMES || !m_player->getCanBuildBase() ||
			!m_baseCenterSet || isBaseUnderAttack() )
		return;
	Object *dozer = NULL;
	m_player->iterateObjects( findAnyDozer, &dozer );
	for( Int i = 0; dozer && i < EARLY_INCOME_KINDS; ++i )
	{
		const ThingTemplate *tmpl = buildableOfKind( dozer, GUI_COMMAND_DOZER_CONSTRUCT, EARLY_INCOME[ i ], TRUE );
		if( tmpl == NULL || priorityBuildPending( m_player, tmpl ) )
			continue;
		TechCount owned;
		owned.kind = EARLY_INCOME[ i ];
		owned.standing = 0;
		owned.onTheWay = 0;
		m_player->iterateObjects( countTechBuildings, &owned );
		if( owned.standing + owned.onTheWay == 0 )
			placeNear( tmpl, &m_baseCenter, 0.5f * m_baseRadius, FALSE );
	}
}

/** Players said the computer attacks too early and too often.  A Hard USA on Twilight Flame sent two
	* units at frame 5,873 and its first drop zone stood at 10,438.  So no attack team leaves home before
	* m_firstPushSeconds, nor before a drop zone or black market stands when the side has one to build.
	* That wait ends with the opening buyEarlyIncome buys in, whatever stands, so a base that cannot
	* build one still plays.  China has neither and waits for the clock alone. */
Bool AIPlayer::readyToPush( void )
{
	const UnsignedInt now = TheGameLogic->getFrame();
	if( now < (UnsignedInt)(getSkillProfile()->m_firstPushSeconds * LOGICFRAMES_PER_SECOND) )
		return FALSE;
	if( now >= AI_PURCHASE_EARLY_FRAMES || !m_player->getCanBuildBase() )
		return TRUE;
	// an Attacker goes on the clock alone: waiting on the black market it parked its teams for six minutes
	if( m_persona == AIPERSONA_ATTACKER || m_persona == AIPERSONA_RUSHER )
		return TRUE;
	Object *dozer = NULL;
	m_player->iterateObjects( findAnyDozer, &dozer );
	const CommandSet *commandSet = dozer ? TheCommandStore->findCommandSet( dozer->getCommandSetString() ) : NULL;
	if( commandSet == NULL )
		return TRUE;
	// one the dozer has the prerequisites for, whatever else stops it this moment: asking for one it
	// could make now, a Hard USA went out at 4.7 minutes, a minute before its drop zone stood.  A USA
	// drop zone needs the Strategy Center, which Medium puts up late, and is not waited for before it
	Bool hasButton = FALSE;
	for( Int i = 0; i < EARLY_INCOME_KINDS; ++i )
	{
		for( Int b = 0; b < MAX_COMMANDS_PER_SET; ++b )
		{
			const CommandButton *button = commandSet->getCommandButton( b );
			if( button && button->getCommandType() == GUI_COMMAND_DOZER_CONSTRUCT && button->getThingTemplate() &&
					button->getThingTemplate()->isKindOf( EARLY_INCOME[ i ] ) &&
					TheBuildAssistant->canMakeUnit( dozer, button->getThingTemplate() ) != CANMAKE_NO_PREREQ )
				hasButton = TRUE;
		}
		TechCount owned;
		owned.kind = EARLY_INCOME[ i ];
		owned.standing = 0;
		owned.onTheWay = 0;
		m_player->iterateObjects( countTechBuildings, &owned );
		if( owned.standing > 0 )
			return TRUE;
	}
	return !hasButton;
}

//----------------------------------------------------------------------------------------------------------
/** A bank of a hundred thousand is an army and an economy that were never bought.  The skirmish build
	* list is a fixed plan - two war factories and a barracks - and the scripts never add to it, so
	* once the plan stood the money had nowhere to go.
	*
	* What the plan cannot buy.  A tank factory or a barracks when that queue is backing up, and an
	* airfield on every pass the money allows, with no cap on how many stand.  Income buildings the
	* same way: a supply drop zone, a black market, an internet center, whichever of those the faction
	* can build, on every pass.  Hackers from every factory that trains them.  And base defenses, a
	* mix of the side's types, as many as aiDefenseAllowance gives the army and the clock.
	*/
//----------------------------------------------------------------------------------------------------------
void AIPlayer::doEconomy( void )
{
	const AIDifficultyProfile *profile = getSkillProfile();
	const Int phase = computeUpdatePhase( m_player->getPlayerIndex(), ECONOMY_CHECK_RATE );
	if( (TheGameLogic->getFrame() + phase) % ECONOMY_CHECK_RATE != 0 )
		return;
	// a rung that buys nothing else past its build list still puts its first income up before it pushes
	if( !profile->m_economyBuildings )
	{
		if( m_player->getEnergy()->hasSufficientPower() )
			buyEarlyIncome();
		// the Turtle's line is its point on every rung, and the rungs below Hard buy no defences here otherwise
		if( m_persona == AIPERSONA_TURTLE )
			queueTurtleLine();
		return;
	}

	if( m_baseCenterSet )
	{
		HackerDuty duty;
		duty.internetCenter = NULL;
		duty.spot = m_baseCenter;
		Coord3D dir;
		if( enemyDirection( &dir ) )
		{
			duty.spot.x -= dir.x * m_baseRadius * POWER_SETBACK;
			duty.spot.y -= dir.y * m_baseRadius * POWER_SETBACK;
		}
		m_player->iterateObjects( findInternetCenterWithRoom, &duty );
		m_player->iterateObjects( putToHacking, &duty );
	}

	// a dark base buys its power plant before anything else: one Hard USA spent 9,560 on other things
	// in the middle of a two-minute outage
	if( !m_player->getEnergy()->hasSufficientPower() )
		return;

	// a tunnel pays back in the time every wave and every retreat saves, so it does not wait for the
	// hoard: behind it, it was bought once in four matches, since an economy that works spends the
	// bank down to the threshold on the army
	if( m_player->getCanBuildBase() && m_baseCenterSet )
	{
		Object *dozer = NULL;
		m_player->iterateObjects( findAnyDozer, &dozer );
		if( dozer )
			doTunnels( dozer );
	}

	buyEarlyIncome();

	if( m_persona == AIPERSONA_TURTLE )
		queueTurtleLine();

	// hackers and the buildings that pay out on a timer earn their price back, so they only wait for
	// the hoard threshold, and each of them is bought on every pass the bank allows one ...
	if( m_player->getMoney()->countMoney() <= profile->m_cashHoardThreshold )
		return;
	// ... unless the base is being hit: then the bank and the dozers are defendHome's.  A rushed China
	// put up an Internet Center and a second barracks while sixteen rebels stood in its base.
	if( isBaseUnderAttack() )
		return;
	buyMoneyUnits();

	if( !m_player->getCanBuildBase() || !m_baseCenterSet )
		return;

	/* Base building only ever asked for a dozer when it had none, so a Hard AI played the whole match
		 on two, and everything bought below waited in one line for them: nine base-building passes in
		 ten found both on a job. Four China Tank AIs on Twilight Flame, seeds 7 to 9, free-for-all and
		 2v2, 18,000 frames: 8 of the 24 never put up a war factory and never attacked. With this, none,
		 39 attack waves became 82, and each spent 67,000 instead of 45,000. */
	DozerTally dozers = { 0, 0 };
	m_player->iterateObjects( tallyDozer, &dozers );
	if( dozers.dozers > 0 && dozers.building == dozers.dozers && dozers.dozers < MAX_ECONOMY_DOZERS )
		queueDozer();

	// any dozer will do to read the buttons from: an idle one is what builds it, and that is later
	Object *dozer = NULL;
	m_player->iterateObjects( findAnyDozer, &dozer );
	if( dozer == NULL )
		return;

	const Int INCOME_KINDS = 3;
	static const KindOfType INCOME[ INCOME_KINDS ] = {
		KINDOF_FS_SUPPLY_DROPZONE, KINDOF_FS_BLACK_MARKET, KINDOF_FS_INTERNET_CENTER };
	for( Int i = 0; i < INCOME_KINDS; ++i )
	{
		const ThingTemplate *tmpl = buildableOfKind( dozer, GUI_COMMAND_DOZER_CONSTRUCT, INCOME[ i ] );
		// from half way out to the edge: placeNear keeps these inside the base (staysHome)
		if( tmpl && !priorityBuildPending( m_player, tmpl ) )
			placeNear( tmpl, &m_baseCenter, 0.5f * m_baseRadius, FALSE );
	}

	// another factory is doSpending's, on every rung
	if( m_persona != AIPERSONA_TURTLE )
		queueBaseDefenses( dozer );
}

//----------------------------------------------------------------------------------------------------------
/** A gun on the side the trouble comes from, before the factories the pass buys.  Production and
	* income go round the base center, the power goes behind it, and this goes out in front, so the
	* defenses stand in front of everything else.  It used to be one every minute and a half and never
	* more than ten.  Bought whenever the bank allowed, they took the money from the army: over twelve
	* four-player matches 23 guns became 241, and attack waves fell from 223 to 187.  So the guns grow
	* with the army, and with the clock up to two superweapons' worth (aiDefenseAllowance), one at a
	* time, each the type the base has fewest of, each on the spot with the widest field of fire
	* (placeDefense).  They wait only for the hoard, like the income buildings: behind twice the
	* hoard, a 1v1 Hard side built two to five a match.  A base three or more behind its allowance
	* queues two a pass, of different types, since one a pass left most of the allowance unspent.
	* The Turtle's do not wait for the hoard: its line is what it spends on first, and the purchase
	* queue saves for each gun. */
//----------------------------------------------------------------------------------------------------------
void AIPlayer::queueTurtleLine( void )
{
	// before the hoard, unless the base is already being hit
	if( !m_player->getCanBuildBase() || !m_baseCenterSet || isBaseUnderAttack() )
		return;
	Object *dozer = NULL;
	m_player->iterateObjects( findAnyDozer, &dozer );
	if( dozer )
		queueBaseDefenses( dozer );
}

void AIPlayer::queueBaseDefenses( Object *dozer )
{
	const BaseTally tally = tallyBase( m_player );
	Int allowance = aiDefenseAllowance( tally.army, TheGameLogic->getFrame() );
	if( m_persona != AIPERSONA_NORMAL )
		allowance = REAL_TO_INT( INT_TO_REAL( allowance ) * personaWeights().m_defenses );
	if( m_persona == AIPERSONA_TURTLE )
		allowance = REAL_TO_INT( INT_TO_REAL( allowance ) * TURTLE_DEFENSE_SCALE[ m_skillLevel ] );
	const Int perPass = allowance - tally.defenses >= 3 ? 2 : 1;
	for( Int k = 0; k < perPass && tally.defenses + k < allowance; ++k )
	{
		const ThingTemplate *defense = nextBaseDefense( dozer );
		if( defense == NULL || priorityBuildPending( m_player, defense ) )
			break;
		const Bool placed = placeDefense( defense );
		DEBUG_LOG(("AI ECONOMY frame %d player %d %s defense '%s', %d of %d standing, army %d\n", TheGameLogic->getFrame(),
			m_player->getPlayerIndex(), placed ? "puts up" : "has no room for", defense->getName().str(),
			tally.defenses + k, allowance, tally.army));
		if( !placed )
			break;
	}
	if( m_persona == AIPERSONA_TURTLE )
		fortifyChokes( dozer );
}

/** How near a point one of this player's tunnels has to stand to count as covering it. */
static const Real TUNNEL_COVER_RADIUS = 350.0f;

/** How far out along the line to the nearest enemy the forward tunnel goes, as a share of the way.
	* A wave sets off from the edge of its own base, and an exit short of the enemy's doorstep saves
	* little or nothing over walking (TunnelTracker::findTunnelShortcut): at 0.45, when a shortcut still
	* had to save 30% of the walk, not one wave in four matches went underground. */
static const Real FORWARD_TUNNEL_SHARE = 0.75f;

/** A tunnel network the builder can put up right now, off its own buttons.  The tunnel carries
	* FS_BASE_DEFENSE like the guns, so the kind cannot tell them apart; the module can. */
static const ThingTemplate *buildableTunnel( Object *builder )
{
	const CommandSet *commandSet = TheCommandStore->findCommandSet( builder->getCommandSetString() );
	if( commandSet == NULL )
		return NULL;

	for( Int i = 0; i < MAX_COMMANDS_PER_SET; ++i )
	{
		const CommandButton *button = commandSet->getCommandButton( i );
		if( button == NULL || button->getCommandType() != GUI_COMMAND_DOZER_CONSTRUCT )
			continue;
		const ThingTemplate *tmpl = button->getThingTemplate();
		if( tmpl == NULL || TheBuildAssistant->canMakeUnit( builder, tmpl ) != CANMAKE_OK )
			continue;
		const ModuleInfo &modules = tmpl->getBehaviorModuleInfo();
		for( Int m = 0; m < modules.getCount(); ++m )
		{
			if( modules.getNthName( m ).compareNoCase( "TunnelContain" ) == 0 )
				return tmpl;
		}
	}
	return NULL;
}

static Bool hasTunnelNear( Player *player, const Coord3D *spot )
{
	const std::list<ObjectID> *tunnels = player->getTunnelSystem()->getContainerList();
	for( std::list<ObjectID>::const_iterator it = tunnels->begin(); it != tunnels->end(); ++it )
	{
		const Object *tunnel = TheGameLogic->findObjectByID( *it );
		if( tunnel && sqr( tunnel->getPosition()->x - spot->x ) + sqr( tunnel->getPosition()->y - spot->y ) <= sqr( TUNNEL_COVER_RADIUS ) )
			return TRUE;
	}
	return FALSE;
}

//----------------------------------------------------------------------------------------------------------
/** A GLA computer makes a habit of tunnels at the places that matter: one at home, one at the
	* expansion it holds, and one on the road the next wave takes, three quarters of the way along.
	* The move orders, waves and retreats take a tunnel when it is the shorter way
	* (TunnelTracker::findTunnelShortcut), and with these three a wave goes underground at home and comes
	* up outside the enemy's base, and a beaten team near the front comes up at home.  One is asked for
	* a pass, the nearest to home first; the forward one only where nothing this AI has seen can shoot.
	* There is no cap: the spots are fixed, one a road, and a spot with a tunnel near it asks for none. */
//----------------------------------------------------------------------------------------------------------
void AIPlayer::doTunnels( Object *dozer )
{
	const ThingTemplate *tunnel = buildableTunnel( dozer );
	if( tunnel == NULL || priorityBuildPending( m_player, tunnel ) )
		return;

	const Int MAX_SPOTS = 3;
	Coord3D spots[ MAX_SPOTS ];
	Real innerRadius[ MAX_SPOTS ];
	Int count = 0;

	// at home, out of the middle, which the production buildings have taken
	spots[ count ] = m_baseCenter;
	innerRadius[ count++ ] = 0.5f * m_baseRadius;

	const Object *warehouse = TheGameLogic->findObjectByID( m_curWarehouseID );
	if( warehouse && isHeldExpansion( warehouse ) )
	{
		spots[ count ] = *warehouse->getPosition();
		innerRadius[ count++ ] = warehouse->getGeometryInfo().getBoundingCircleRadius();
	}

	// on the road the parked wave is about to take, which the script chose knowing where the enemy is;
	// this AI's own guess can be an empty start position for most of a match on a four-start map.  A
	// wave parks for seconds at a time, so with nobody parked the last road taken stands in for it
	Int road = -1;
	for( Int held = 0; held < MAX_HELD_TEAMS; ++held )
	{
		if( m_heldLabel[ held ].isEmpty() )
			continue;
		if( road < 0 || m_heldUsed[ held ] )
			road = held;
		if( m_heldUsed[ held ] )
			break;
	}
	Waypoint *start = NULL;
	if( road >= 0 )
	{
		AsciiString pathLabel;
		pathLabel.format( "%s%d", m_heldLabel[ road ].str(), m_heldSuffix[ road ] );
		start = TheTerrainLogic->getClosestWaypointOnPath( &m_baseCenter, pathLabel );
	}
	Waypoint *end = start;
	for( Int step = 0; end != NULL && end->getNumLinks() > 0 && step < APPROACH_MAX_WAYPOINTS; ++step )
		end = end->getLink( 0 );
	if( end != NULL )
	{
		const Real reach = FORWARD_TUNNEL_SHARE * sqrt( sqr( end->getLocation()->x - m_baseCenter.x ) + sqr( end->getLocation()->y - m_baseCenter.y ) );
		Waypoint *way = start;
		for( Int step = 0; way != NULL && step < APPROACH_MAX_WAYPOINTS; ++step )
		{
			const Coord3D *at = way->getLocation();
			if( sqr( at->x - m_baseCenter.x ) + sqr( at->y - m_baseCenter.y ) >= sqr( reach ) )
			{
				if( reach > m_baseRadius && knownFirepowerNear( at ) <= 0.0f )
				{
					spots[ count ] = *at;
					innerRadius[ count++ ] = 0.0f;
				}
				break;
			}
			way = (way->getNumLinks() > 0) ? way->getLink( 0 ) : NULL;
		}
	}

	for( Int i = 0; i < count; ++i )
	{
		if( hasTunnelNear( m_player, &spots[ i ] ) )
			continue;
		placeNear( tunnel, &spots[ i ], innerRadius[ i ], FALSE );
		return;
	}
}

/** One of this player's supply centers stands within reach of a warehouse. */
struct SupplyCenterSearch
{
	Coord3D at;
	Real reachSqr;
	Bool found;
};

static void findSupplyCenterNear( Object *obj, void *userData )
{
	SupplyCenterSearch *search = (SupplyCenterSearch *)userData;
	if( search->found || !obj->isKindOf( KINDOF_FS_SUPPLY_CENTER ) || obj->isEffectivelyDead() )
		return;
	if( sqr( obj->getPosition()->x - search->at.x ) + sqr( obj->getPosition()->y - search->at.y ) <= search->reachSqr )
		search->found = TRUE;
}

//----------------------------------------------------------------------------------------------------------
/** The expansion a new factory may go beside.  m_curWarehouseID is the last dock this player chose and
	* nothing ever clears it: not when the supply center beside it is destroyed, not when the dock turns
	* out to sit in somebody's base. A player reported a Hard AI putting its war factory back up, again and
	* again, on the ground where he had just destroyed it, inside his own base: placeNear tries the same
	* spots in the same order, so the rubble was the first legal spot every time. So the dock has to still
	* be ours, with our supply center at it, and nearer our base than any enemy's we know of. */
//----------------------------------------------------------------------------------------------------------
Bool AIPlayer::isHeldExpansion( const Object *warehouse )
{
	const Real reach = warehouse->getGeometryInfo().getBoundingCircleRadius() + SUPPLY_CENTER_CLOSE_DIST;
	SupplyCenterSearch search;
	search.at = *warehouse->getPosition();
	search.reachSqr = reach * reach;
	search.found = FALSE;
	m_player->iterateObjects( findSupplyCenterNear, &search );
	if( !search.found )
		return FALSE;
	return isOurSideOfMap( &search.at );
}

/** The point is no nearer any living enemy's base, as far as this AI knows where that is, than ours. */
Bool AIPlayer::isOurSideOfMap( const Coord3D *pos )
{
	const Real oursSqr = sqr( pos->x - m_baseCenter.x ) + sqr( pos->y - m_baseCenter.y );
	for( Int i = 0; i < ThePlayerList->getPlayerCount(); ++i )
	{
		Player *them = ThePlayerList->getNthPlayer( i );
		if( them == m_player || m_player->getRelationship( them->getDefaultTeam() ) != ENEMIES || !them->hasAnyObjects() )
			continue;
		Coord3D theirs;
		if( enemyStartGuess( i, &theirs ) && sqr( pos->x - theirs.x ) + sqr( pos->y - theirs.y ) < oursSqr )
			return FALSE;
	}
	return TRUE;
}

/** The plan has an entry for this building that is not standing and not yet asked for. */
static Bool hasUnbuiltPlanEntry( Player *player, const ThingTemplate *tmpl )
{
	for( BuildListInfo *info = player->getBuildList(); info; info = info->getNext() )
	{
		if( info->getTemplateName() != tmpl->getName() || info->isPriorityBuild() )
			continue;
		if( TheGameLogic->findObjectByID( info->getObjectID() ) == NULL )
			return TRUE;
	}
	return FALSE;
}

void AIPlayer::buildAsap( const ThingTemplate *tmpl )
{
	if( hasUnbuiltPlanEntry( m_player, tmpl ) )
	{
		DEBUG_LOG(("AI SUPERWEAPON frame %d player %d asks for the plan's '%s', %d in the bank\n", TheGameLogic->getFrame(),
			m_player->getPlayerIndex(), tmpl->getName().str(), m_player->getMoney()->countMoney()));
		buildSpecificAIBuilding( tmpl->getName() );
		return;
	}
	Coord3D spot = m_baseCenter;
	Coord3D dir;
	if( enemyDirection( &dir ) )
	{
		spot.x -= dir.x * m_baseRadius * POWER_SETBACK;
		spot.y -= dir.y * m_baseRadius * POWER_SETBACK;
	}
	if( placeNear( tmpl, &spot, 0.0f, FALSE ) )
		return;

	/* A superweapon's footprint rarely fits one of placeNear's tries in a grown base, so the request
		 goes down behind the base anyway and the dozer's flood search looks for the room, no further
		 than the base's edge (staysHome).  A
		 base with none floods seven thousand cells for nothing, and giveUpOnSpot drops the request, so
		 it is only put down every sixth check: asked every check, one Hard GLA spent 3,687 frames of a
		 match flooding for a Scud Storm with the rest of its base waiting behind it. */
	const Int FLOOD_EVERY_CHECKS = 6;
	if( (TheGameLogic->getFrame() / SUPERWEAPON_CHECK_RATE) % FLOOD_EVERY_CHECKS != 0 )
		return;
	spot.z = 0;
	m_player->addToPriorityBuildList( tmpl->getName(), &spot, tmpl->getPlacementViewAngle() );
}

//----------------------------------------------------------------------------------------------------------
/** The skirmish scripts hold a Hard AI's superweapon back until an escalation counter, one point every
	* ten seconds, reaches 75: twelve and a half minutes, or seven and a half once the enemy owns a tech
	* building, and then one copy. A player who went seven Hard AIs against one saw a single nuke in the
	* whole match. The owner's call: no clock. As soon as a dozer can build a superweapon, the money
	* is in the bank and the rung's ration of guns stands (m_superweaponDefenses, DEFENSES_PER_SUPERWEAPON
	* here), the first one goes up. More follow as the
	* base grows, one per that many guns, as the guns grow with the army; unrationed, they took the money
	* the army needed. That ration binds computer players only (Player::canBuildMoreOfType), and the
	* lobby's superweapon setting and Pro Rules still bind beside it; canMakeUnit asks all three.
	* If the tech building it needs is missing, that goes up first, and copies follow once the first
	* superweapon is standing, up to AI_TECH_BUILDING_COPIES, so one of them blowing up leaves the tree
	* standing. Hard only, like the rest of the economy, except for a side whose scripts cannot build its
	* superweapon (planNamesSuperweapon): Easy and Medium put that one up here too, as late as the scripts
	* put up America's uplink. */
//----------------------------------------------------------------------------------------------------------
/** The scripts build a superweapon only through the side's build list (buildSpecificAIBuilding), so a
	* plan that names none, Turkiye's, which runs America's scripts and has no uplink, never gets one
	* from them.  The request buildAsap puts down is a build list entry of its own, so Easy and Medium
	* ask for one, as the scripts' once-only line does for America. */
static Bool planNamesSuperweapon( Player *player )
{
	for( BuildListInfo *info = player->getBuildList(); info; info = info->getNext() )
	{
		const ThingTemplate *tmpl = TheThingFactory->findTemplate( info->getTemplateName() );
		if( tmpl && tmpl->isKindOf( KINDOF_FS_SUPERWEAPON ) )
			return TRUE;
	}
	return FALSE;
}

/** Easy's and Medium's first superweapon when the scripts cannot build it: America's scripts ask for
	* the uplink at escalation 180 and 100, a point every ten seconds. */
static const UnsignedInt SCRIPTLESS_SUPERWEAPON_FRAMES[ AISKILL_COUNT ] =
	{ 1800 * LOGICFRAMES_PER_SECOND, 1000 * LOGICFRAMES_PER_SECOND, 0 };

void AIPlayer::doSuperweapons( void )
{
	const Int phase = computeUpdatePhase( m_player->getPlayerIndex(), SUPERWEAPON_CHECK_RATE );
	if( (TheGameLogic->getFrame() + phase) % SUPERWEAPON_CHECK_RATE != 0 )
		return;
	if( !getSkillProfile()->m_economyBuildings &&
			( TheGameLogic->getFrame() < SCRIPTLESS_SUPERWEAPON_FRAMES[ m_skillLevel ] || planNamesSuperweapon( m_player ) ) )
		return;
	if( !m_player->getCanBuildBase() || !m_baseCenterSet || !m_player->getEnergy()->hasSufficientPower() )
		return;		// a dark base's superweapon clock is stopped anyway; the money goes to the plant

	Object *dozer = NULL;
	m_player->iterateObjects( findAnyDozer, &dozer );
	if( dozer == NULL )
		return;

	// with the purchase queue they are asked for before the bank can pay, and the queue saves for them
	const ThingTemplate *superweapon = buildableOfKind( dozer, GUI_COMMAND_DOZER_CONSTRUCT, KINDOF_FS_SUPERWEAPON, purchaseQueueOn() );
	const ThingTemplate *techBuilding = buildableOfKind( dozer, GUI_COMMAND_DOZER_CONSTRUCT, KINDOF_FS_ADVANCED_TECH, purchaseQueueOn() );

	TechCount tech;
	tech.kind = KINDOF_FS_ADVANCED_TECH;
	tech.standing = 0;
	tech.onTheWay = 0;
	m_player->iterateObjects( countTechBuildings, &tech );
	if( techBuilding && priorityBuildPending( m_player, techBuilding ) )
		++tech.onTheWay;

	/* The first tech building before any superweapon, the first superweapon before the spares, then
		 the tech building up to AI_TECH_BUILDING_COPIES, then more superweapons by the gun ratio.  A
		 faction that cannot buy the tech building falls straight through to the superweapon. */
	const BaseTally tally = tallyBase( m_player );
	const Bool wantTech = techBuilding && aiWantsAnotherTechBuilding( tech.standing, tech.onTheWay );
	const Bool firstTech = wantTech && tech.standing < 1;
	const Bool spareTech = wantTech && tally.superweapons >= 1;
	if( firstTech || spareTech )
	{
		buildAsap( techBuilding );
		return;
	}

	// buildableOfKind already asked canMakeUnit, which holds the defence ration
	if( superweapon && !priorityBuildPending( m_player, superweapon ) )
		buildAsap( superweapon );
}

static void findParticleCannons( Object *obj, void *userData )
{
	static const NameKeyType key = NAMEKEY( "ParticleUplinkCannonUpdate" );
	if( !obj->isKindOf( KINDOF_FS_SUPERWEAPON ) )
		return;
	ParticleUplinkCannonUpdate *cannon = (ParticleUplinkCannonUpdate *)obj->findUpdateModule( key );
	if( cannon )
		((std::vector<ParticleUplinkCannonUpdate *> *)userData)->push_back( cannon );
}

/** How far from the burning spot a steer looks, about four seconds of the beam's fast drive. */
const Real BEAM_STEER_REACH = 150.0f;
/** The look is a square of this many steps each side of the burning spot. */
const Int BEAM_STEER_STEPS = 3;

//----------------------------------------------------------------------------------------------------------
/** A computer player's Particle Cannon used to draw EA's S curve: 200 feet along the line from the
	* uplink to the aim point, 50 either side of it, whatever stood there.  The aim was the richest
	* spot in his base, and most of the ten seconds of beam burned the ground on either side of it.
	* A player drives the beam instead, from one building to the next.  So does this: every
	* m_beamSteerSeconds it looks around the spot the beam is burning, and sends it to the ground
	* worth most with the same score the aim used, nearer ground preferred.  What the beam has killed
	* drops out of that score, so it moves on from a dead cluster to the next one by itself.
	* Hard steers more often than the game's double click delay, which makes the cannon drive at its
	* fast speed; Medium drives at the walking speed; Easy keeps the S curve. */
//----------------------------------------------------------------------------------------------------------
void AIPlayer::doParticleBeams( void )
{
	const Real every = getSkillProfile()->m_beamSteerSeconds;
	if( every <= 0.0f )
		return;
	const Int rate = REAL_TO_INT_CEIL( every * LOGICFRAMES_PER_SECOND );
	if( TheGameLogic->getFrame() % rate != 0 )
		return;

	std::vector<ParticleUplinkCannonUpdate *> cannons;
	m_player->iterateObjects( findParticleCannons, &cannons );
	for( size_t i = 0; i < cannons.size(); ++i )
	{
		Coord3D spot;
		Real radius;
		if( !cannons[ i ]->getBurningSpot( &spot, &radius ) )
			continue;
		Coord3D aim;
		if( richestGroundNear( &spot, radius, &aim ) )
			cannons[ i ]->setSpecialPowerOverridableDestination( &aim );
	}
}

//----------------------------------------------------------------------------------------------------------
Bool AIPlayer::richestGroundNear( const Coord3D *spot, Real radius, Coord3D *aim )
{
	const Int me = m_player->getPlayerIndex();
	const Real step = BEAM_STEER_REACH / BEAM_STEER_STEPS;
	Real best = 0.0f;
	for( Int x = -BEAM_STEER_STEPS; x <= BEAM_STEER_STEPS; ++x )
	{
		for( Int y = -BEAM_STEER_STEPS; y <= BEAM_STEER_STEPS; ++y )
		{
			Coord3D pos;
			pos.x = spot->x + x * step;
			pos.y = spot->y + y * step;
			pos.z = 0.0f;
			Int value = 0;
			for( Int p = 0; p < ThePlayerList->getPlayerCount(); ++p )
			{
				Player *other = ThePlayerList->getNthPlayer( p );
				if( other != m_player && m_player->getRelationship( other->getDefaultTeam() ) == ENEMIES )
					value += getPlayerSuperweaponValue( &pos, p, radius, TRUE, me, strikeEconomyWeight(), strikeThreatWeight() );
			}
			const Real distance = sqrt( (Real)(x * x + y * y) ) * step;
			const Real worth = value * (1.0f - 0.5f * distance / BEAM_STEER_REACH);
			if( worth > best )
			{
				best = worth;
				*aim = pos;
			}
		}
	}
	if( best <= 0.0f )
		return FALSE;
	aim->z = TheTerrainLogic->getGroundHeight( aim->x, aim->y );
	DEBUG_LOG(("AI BEAM frame %d player %d drives the beam from (%.0f,%.0f) to (%.0f,%.0f), worth %.0f, radius %.0f\n",
		TheGameLogic->getFrame(), me, spot->x, spot->y, aim->x, aim->y, best, radius));
	return TRUE;
}

//----------------------------------------------------------------------------------------------------------
/** The purchase queue.  Every skirmish rung with a bank target, each saving below its own target; every
	* campaign AI buys exactly as it did. */
//----------------------------------------------------------------------------------------------------------
Bool AIPlayer::purchaseQueueOn( void )
{
	return runsOwnAI() && getSkillProfile()->m_bankTargetSeconds > 0.0f;
}

Int AIPlayer::bankTarget( void )
{
	const AIDifficultyProfile *profile = getSkillProfile();
	return aiBankTarget( m_incomePerSecond, profile->m_bankTargetSeconds, profile->m_bankTargetFloor );
}

AIPurchaseKind AIPlayer::purchaseKindOf( const ThingTemplate *tmpl ) const
{
	if( tmpl->isKindOf( KINDOF_DOZER ) )
		return AIBUY_DOZER;
	if( tmpl->isKindOf( KINDOF_FS_POWER ) && !tmpl->isKindOf( KINDOF_CASH_GENERATOR ) )
		return AIBUY_POWER;
	if( tmpl->isKindOf( KINDOF_HARVESTER ) || tmpl->isKindOf( KINDOF_MONEY_HACKER ) || tmpl->isKindOf( KINDOF_FS_SUPPLY_CENTER ) ||
			tmpl->isKindOf( KINDOF_FS_SUPPLY_DROPZONE ) || tmpl->isKindOf( KINDOF_FS_BLACK_MARKET ) || tmpl->isKindOf( KINDOF_FS_INTERNET_CENTER ) )
		return AIBUY_ECONOMY;
	if( tmpl->isKindOf( KINDOF_FS_SUPERWEAPON ) || tmpl->isKindOf( KINDOF_FS_ADVANCED_TECH ) )
		return AIBUY_SUPERWEAPON;
	if( tmpl->isKindOf( KINDOF_FS_BASE_DEFENSE ) )
		return AIBUY_DEFENSE;
	// the Turtle's demo traps are part of its line, and waited behind every building as one of them
	if( m_persona == AIPERSONA_TURTLE && tmpl->isKindOf( KINDOF_DEMOTRAP ) )
		return AIBUY_DEFENSE;
	if( tmpl->isKindOf( KINDOF_STRUCTURE ) )
		return AIBUY_BASE;
	return AIBUY_ARMY;
}

Bool AIPlayer::purchaseAllowed( Int key, Int cost, AIPurchaseKind kind, Bool startable, const AsciiString &what, Int scoreOffset )
{
	if( !purchaseQueueOn() )
		return TRUE;

	const UnsignedInt now = TheGameLogic->getFrame();
	const Int money = (Int)m_player->getMoney()->countMoney();
	const Int held = m_purchases.m_held;
	const AIPurchaseQueue::Lapse lapse = m_purchases.expire( now );
	if( lapse != AIPurchaseQueue::KEPT )
		DEBUG_LOG(("AI QUEUE frame %d player %d stops saving for '%s', %s, %d held back for it, %d in the bank\n", now,
			m_player->getPlayerIndex(), m_purchaseSavingFor.str(), lapse == AIPurchaseQueue::STALE ? "no longer asked for" : "timed out",
			held, money));

	Int score = aiPurchaseScore( kind, now < AI_PURCHASE_EARLY_FRAMES );
	if( m_persona != AIPERSONA_NORMAL )
		score += personaWeights().m_buy[ kind ] + scoreOffset;
	const Int target = bankTarget();
	const AIPurchaseQueue::Verdict verdict = m_purchases.ask( key, cost, score, startable, money, target, now );
	if( verdict == AIPurchaseQueue::RESERVED )
	{
		m_purchaseSavingFor = what;
		DEBUG_LOG(("AI QUEUE frame %d player %d saves for '%s', %d of %d in the bank, score %d, bank target %d\n", now,
			m_player->getPlayerIndex(), what.str(), money, cost, score, target));
	}
	return verdict == AIPurchaseQueue::BUY;
}

void AIPlayer::purchased( Int key, Int cost, AIPurchaseKind kind, const AsciiString &what )
{
	if( !purchaseQueueOn() )
		return;
	const UnsignedInt now = TheGameLogic->getFrame();
	if( key == m_purchases.m_key )
		DEBUG_LOG(("AI QUEUE frame %d player %d buys '%s' for %d, score %d, %d left in the bank, saved for over %d frames and %d held buys\n",
			now, m_player->getPlayerIndex(), what.str(), cost, aiPurchaseScore( kind, now < AI_PURCHASE_EARLY_FRAMES ),
			m_player->getMoney()->countMoney(), now - m_purchases.m_since, m_purchases.m_held));
	else
		DEBUG_LOG(("AI QUEUE frame %d player %d buys '%s' for %d, score %d, %d left in the bank\n", now,
			m_player->getPlayerIndex(), what.str(), cost, aiPurchaseScore( kind, now < AI_PURCHASE_EARLY_FRAMES ),
			m_player->getMoney()->countMoney()));
	m_purchases.bought( key );
}

Bool AIPlayer::buyUnit( ProductionUpdateInterface *pu, const ThingTemplate *unit, AIPurchaseKind kind )
{
	// an airfield with every parking place taken refuses the aircraft (queueCreateUnit fails on it), so
	// nothing is saved for one, and the refusal is what says the computer wants another airfield
	if( pu->canQueueCreateUnit( unit ) == CANMAKE_PARKING_PLACES_FULL )
	{
		m_parkingBlockedFrame = TheGameLogic->getFrame();
		return FALSE;
	}
	const Int cost = unit->calcCostToBuild( m_player );
	// a personality's taste moves an army unit up or down the queue: the scripts' teams of a kind it
	// does not like wait while its favoured units are saved for
	Int offset = 0;
	if( m_persona != AIPERSONA_NORMAL && kind == AIBUY_ARMY )
	{
		const Real taste = unitTaste( personaKindTaste(), unit );
		offset = taste > 0.0f ? PERSONA_LIKED_SCORE : taste < 0.0f ? PERSONA_DISLIKED_SCORE : 0;
	}
	if( !purchaseAllowed( unit->getTemplateID(), cost, kind, pu->getProductionCount() == 0, unit->getName(), offset ) )
		return FALSE;
	if( !pu->queueCreateUnit( unit, pu->requestUniqueUnitID() ) )
		return FALSE;
	purchased( unit->getTemplateID(), cost, kind, unit->getName() );
	return TRUE;
}

/** An upgrade's key: its bit in the upgrade mask, which the upgrade center hands out in INI order,
	* past every template id. */
static Int upgradePurchaseKey( const UpgradeTemplate *upgrade )
{
	const UpgradeMaskType mask = upgrade->getUpgradeMask();
	for( Int i = 0; i < UPGRADE_MAX_COUNT; ++i )
		if( mask.test( i ) )
			return 0x10000 + i;
	return 0x10000 + UPGRADE_MAX_COUNT;
}

Bool AIPlayer::buyUpgrade( ProductionUpdateInterface *pu, const UpgradeTemplate *upgrade, Int scoreOffset )
{
	const Int key = upgradePurchaseKey( upgrade );
	const Int cost = upgrade->calcCostToBuild( m_player );
	if( !purchaseAllowed( key, cost, AIBUY_UPGRADE, pu->getProductionCount() == 0, upgrade->getUpgradeName(), scoreOffset ) )
		return FALSE;
	if( !pu->queueUpgrade( upgrade ) )
		return FALSE;
	purchased( key, cost, AIBUY_UPGRADE, upgrade->getUpgradeName() );
	return TRUE;
}

//----------------------------------------------------------------------------------------------------------
static void collectObjects( Object *obj, void *userData )
{
	((std::vector<Object*> *)userData)->push_back( obj );
}

//----------------------------------------------------------------------------------------------------------
/** The skirmish scripts fire the powers they name and nothing else, so a side that runs another
	* side's scripts (Turkiye runs America's) never touched its own.  A power marked AIFiresWhenReady goes
	* off the moment it is ready and the science behind it is bought, at the target the scripts' own
	* "fire at most cost" would pick. */
//----------------------------------------------------------------------------------------------------------
static const Int SPECIAL_POWER_CHECK_RATE = 2 * LOGICFRAMES_PER_SECOND;

void AIPlayer::doReadySpecialPowers( void )
{
	const Int phase = computeUpdatePhase( m_player->getPlayerIndex(), SPECIAL_POWER_CHECK_RATE );
	if( (TheGameLogic->getFrame() + phase) % SPECIAL_POWER_CHECK_RATE != 0 )
		return;
	Player *enemy = getAiEnemy();
	if( enemy == NULL )
		return;

	std::vector<Object*> owned;
	m_player->iterateObjects( collectObjects, &owned );
	for( std::vector<Object*>::iterator it = owned.begin(); it != owned.end(); ++it )
	{
		Object *obj = *it;
		if( obj->isEffectivelyDead() || obj->isDisabled() )
			continue;
		for( BehaviorModule **m = obj->getBehaviorModules(); *m; ++m )
		{
			SpecialPowerModuleInterface *power = (*m)->getSpecialPower();
			if( power == NULL || !power->getSpecialPowerTemplate()->isFiredByAIWhenReady() || !power->isReady() )
				continue;
			const ScienceType science = power->getRequiredScience();
			if( science != SCIENCE_INVALID && !m_player->hasScience( science ) )
				continue;

			const SpecialPowerTemplate *tmpl = power->getSpecialPowerTemplate();
			const Real radius = max( 50.0f, tmpl->getRadiusCursorRadius() );
			Coord3D target;
			if( !m_player->computeSuperweaponTarget( tmpl, &target, enemy->getPlayerIndex(), radius ) || target.lengthSqr() <= 0.0f )
				continue;
			DEBUG_LOG(("AI POWER frame %d player %d fires '%s' at (%.0f,%.0f)\n", TheGameLogic->getFrame(),
				m_player->getPlayerIndex(), tmpl->getName().str(), target.x, target.y));
			power->doSpecialPowerAtLocation( &target, INVALID_ANGLE, COMMAND_FIRED_BY_SCRIPT );
		}
	}
}

//----------------------------------------------------------------------------------------------------------
/** A money unit from every factory that trains one and has at most one thing in its queue.  An
	* idle-only rule measured as two hackers in a whole match, because a barracks feeding the army is
	* never idle; one slot behind the army's unit is a delay the army does not notice.  It used to be
	* one factory a pass; the owner's call is that a Hard China never falls behind on hackers.
	*
	* Within moneyUnitRoom, because nothing counted them.  A Hard Tank General trained 67 of them over
	* 20,000 frames on seed 11, about two thirds of everything it spent, and a player watching it from
	* the other side of the map reported that China's tank general builds nothing but infantry. */
//----------------------------------------------------------------------------------------------------------
void AIPlayer::buyMoneyUnits( void )
{
	const Int MAX_QUEUED_AHEAD = 1;
	Int room = moneyUnitRoom();
	if( room <= 0 )
		return;

	for( BuildListInfo *info = m_player->getBuildList(); info; info = info->getNext() )
	{
		Object *factory = TheGameLogic->findObjectByID( info->getObjectID() );
		if( factory == NULL || factory->getControllingPlayer() != m_player || factory->testStatus( OBJECT_STATUS_UNDER_CONSTRUCTION ) )
			continue;
		ProductionUpdateInterface *pu = factory->getProductionUpdateInterface();
		if( pu == NULL || pu->getProductionCount() > MAX_QUEUED_AHEAD )
			continue;
		const ThingTemplate *moneyUnit = buildableOfKind( factory, GUI_COMMAND_UNIT_BUILD, KINDOF_MONEY_HACKER );
		if( moneyUnit && buyUnit( pu, moneyUnit, AIBUY_ECONOMY ) )
		{
			DEBUG_LOG(("AI ECONOMY frame %d player %d trains '%s', %d in the bank, room for %d more\n",
				TheGameLogic->getFrame(), m_player->getPlayerIndex(), moneyUnit->getName().str(),
				m_player->getMoney()->countMoney(), room - 1));
			if( --room <= 0 )
				return;
		}
	}
}

//----------------------------------------------------------------------------------------------------------
/** One more factory of this kind, beside a held expansion or around the base.  The expansion try is
	* local: a miss out there must not walk the base's ring cursor past the ground around the command
	* center, or the first crowded dock skips the base itself.  Nothing while one of this kind is asked for
	* and not yet up, since that one goes first, and then only when holdsExtraFactory lets it.  A plan copy
	* the scripts have not asked for does not wait it out: they reach those at eight minutes. */
//----------------------------------------------------------------------------------------------------------
Bool AIPlayer::queueExtraFactory( Object *dozer, KindOfType kind )
{
	const ThingTemplate *tmpl = buildableOfKind( dozer, GUI_COMMAND_DOZER_CONSTRUCT, kind );
	if( tmpl == NULL )
		return FALSE;
	// a personality's favoured factory goes up to the rung's count without waiting for a backlog, one at a
	// time, and without waiting for the build list's own, which the scripts reach at eight minutes
	Bool favoured = FALSE;
	if( m_persona != AIPERSONA_NORMAL )
	{
		const Int f = personaFavouredKind( personaTaste() );
		if( f >= 0 && PRODUCTION_KIND[ f ] == kind )
		{
			const FactoryLoad load = readFactoryLoad( m_player, kind );
			favoured = load.constructing == 0 && load.finished < PERSONA_FAVOURED_FACTORIES[ m_skillLevel ];
		}
	}
	if( plannedOfKind( m_player, kind, TRUE ) > 0 || ( !favoured && holdsExtraFactory( tmpl ) ) )
		return FALSE;

	const Object *warehouse = TheGameLogic->findObjectByID( m_curWarehouseID );
	if( warehouse && isHeldExpansion( warehouse ) &&
			placeNear( tmpl, warehouse->getPosition(),
				warehouse->getGeometryInfo().getBoundingCircleRadius() + SUPPLY_CENTER_CLOSE_DIST * 0.5f, FALSE ) )
		return TRUE;
	return placeNear( tmpl, &m_baseCenter, m_baseRadius, TRUE );
}

//----------------------------------------------------------------------------------------------------------
/** Another factory of a kind waits until every finished one has been full on most of the spend passes
	* of the rung's m_factoryBacklogSeconds (aiWantsAnotherFactory; doSpending samples it), and while one is
	* going up.  The build list's own copies wait the same way: the Infantry General's plan holds four
	* barracks, and its scripts asked for the second 38 seconds in, before the first had trained anyone,
	* and the bank went to nothing waiting on the third. */
//----------------------------------------------------------------------------------------------------------
Bool AIPlayer::holdsExtraFactory( const ThingTemplate *tmpl )
{
	for( Int k = 0; k < PRODUCTION_KINDS; ++k )
	{
		if( !tmpl->isKindOf( PRODUCTION_KIND[ k ] ) )
			continue;
		const FactoryLoad load = readFactoryLoad( m_player, PRODUCTION_KIND[ k ] );
		// a personality's favoured kind goes up to the rung's count one at a time, backlog or not: an Air
		// AI's plan airfield waited here from 3.3 minutes to 15
		if( favouredFactoryScore( tmpl ) > 0 )
			return load.constructing > 0 || load.finished >= PERSONA_FAVOURED_FACTORIES[ m_skillLevel ];
		return !aiWantsAnotherFactory( load.finished, load.constructing, m_factoryFullHistory[ k ], m_factoryFullCount[ k ],
			aiBacklogSamples( getSkillProfile()->m_factoryBacklogSeconds, ECONOMY_CHECK_RATE ) );
	}
	return FALSE;
}

//----------------------------------------------------------------------------------------------------------
/** Flood out from center over ground a vehicle can cross, nearest first by the ground, until a spot
	* is legal and safe, and hand it to the dozers as a priority build.  It used to try twelve angles
	* on rings round center, and a ring is a straight-line distance: the first legal spot on it could
	* be across a valley or on a ledge behind a cliff, near on the map and a long drive round, and a
	* factory went up there with the dozer teleported to it.  The flood only enters ground the base
	* itself can drive to (groundFromBase), so the nearest spot it finds is the nearest by the road.
	*
	* A call still spends at most two rings' worth of legality checks, two dozen, and a footprint on
	* anything but clear ground is turned away before one is spent.  walkOutward carries the ground
	* distance it reached in m_placementRing, so the search moves out across calls instead of giving
	* up at the opening base, and goes back to the middle once the flood runs out of ground. */
//----------------------------------------------------------------------------------------------------------
Bool AIPlayer::placeNear( const ThingTemplate *tmpl, const Coord3D *center, Real innerRadius, Bool walkOutward )
{
	const Real structureRadius = tmpl->getTemplateGeometryInfo().getBoundingCircleRadius();
	const Real placeAngle = tmpl->getPlacementViewAngle();

	// A building that stays home does not walk outward, and leaves the ring the others have reached
	// alone: it tries every ring that can still be inside the base's edge, on every call.
	const Bool home = staysHome( tmpl );
	const Real homeReachSqr = sqr( BASE_EDGE_REACH * m_baseRadius );
	const Int rings = home ? HOME_PLACEMENT_RINGS : PLACEMENT_RINGS;
	if( home )
		walkOutward = FALSE;

	// as far as the outermost ring reached, by the ground with half again for the way round; the
	// outward walk goes as far as the ground does
	const Real ringReach = innerRadius + structureRadius * ( 2 * rings + 1 );
	const Int maxDistance = walkOutward ? AI_GROUND_UNREACHED - 1 : REAL_TO_INT_CEIL( ringReach * AI_GROUND_DIAGONAL / PATHFIND_CELL_SIZE_F );
	const Int startDistance = walkOutward && m_placementRing > 0 ? m_placementRing : 0;
	const Real innerSqr = innerRadius > 0.0f ? sqr( innerRadius + structureRadius ) : 0.0f;
	// spots a structure's radius apart, as the rings were; never closer than every other cell
	Int stride = REAL_TO_INT_FLOOR( structureRadius / PATHFIND_CELL_SIZE_F );
	if( stride < 2 )
		stride = 2;

	Int width, height;
	groundGridSize( &width, &height );
	AIGroundFlood flood( width, height, groundFloodWalkable, m_baseCenterSet ? (void *)&groundFromBase() : NULL );
	flood.setMaxDistance( maxDistance );
	ICoord2D origin;
	TheAI->pathfinder()->worldToCell( center, &origin );
	flood.addSource( origin.x, origin.y );

	Int tries = rings * PLACEMENT_ANGLES;
	Int x, y, distance;
	while( flood.next( &x, &y, &distance ) )
	{
		const Int dx = x - origin.x;
		const Int dy = y - origin.y;
		if( distance < startDistance || dx % stride != 0 || dy % stride != 0 )
			continue;
		if( TheAI->pathfinder()->getCell( LAYER_GROUND, x, y )->getType() != PathfindCell::CELL_CLEAR )
			continue;
		Coord3D pos = *center;
		pos.x += dx * PATHFIND_CELL_SIZE_F;
		pos.y += dy * PATHFIND_CELL_SIZE_F;
		pos.z = 0;
		if( sqr( pos.x - center->x ) + sqr( pos.y - center->y ) < innerSqr )
			continue;
		if( home && sqr( pos.x - m_baseCenter.x ) + sqr( pos.y - m_baseCenter.y ) > homeReachSqr )
			continue;
		// the square inside the footprint's circle; the corners of the circle's own square stick out
		// past a building's corners and turned away spots that fit
		if( !footprintOnClearGround( &pos, structureRadius * 0.7f ) )
			continue;
		if( tries == 0 )
		{
			// out of checks: the next call starts here, or one cell further when this call never
			// got past the distance it started at, so a crowded ring cannot hold the walk forever
			if( walkOutward )
				m_placementRing = distance > startDistance ? distance : distance + 1;
			return FALSE;
		}
		--tries;

		const Bool legal = TheBuildAssistant->isLocationLegalToBuild( &pos, tmpl, placeAngle,
											BuildAssistant::CLEAR_PATH | BuildAssistant::TERRAIN_RESTRICTIONS | BuildAssistant::NO_OBJECT_OVERLAP,
											NULL, m_player ) == LBC_OK;
		TheTerrainVisual->removeAllBibs();	// isLocationLegalToBuild adds bib feedback
		if( legal && isLocationSafe( &pos, tmpl ) )
		{
			if( walkOutward )
				m_placementRing = distance;
			DEBUG_LOG(("AI ECONOMY frame %d player %d builds '%s' at (%.0f,%.0f), %d in the bank, %.0f from a base of radius %.0f, %.0f by the ground from where it looked\n", TheGameLogic->getFrame(),
				m_player->getPlayerIndex(), tmpl->getName().str(), pos.x, pos.y, m_player->getMoney()->countMoney(),
				sqrt( sqr( pos.x - m_baseCenter.x ) + sqr( pos.y - m_baseCenter.y ) ), m_baseRadius,
				distance * PATHFIND_CELL_SIZE_F / AI_GROUND_STEP));
			m_player->addToPriorityBuildList( tmpl->getName(), &pos, placeAngle );
			return TRUE;
		}
	}
	if( walkOutward )
		m_placementRing = 0;		// every spot the ground holds was looked at: back to the middle
	if( home )
		DEBUG_LOG(("AI ECONOMY frame %d player %d has no room for '%s' within %.0f of its base, %d in the bank\n", TheGameLogic->getFrame(),
			m_player->getPlayerIndex(), tmpl->getName().str(), BASE_EDGE_REACH * m_baseRadius, m_player->getMoney()->countMoney()));
	return FALSE;
}

//----------------------------------------------------------------------------------------------------------
/** B4, second attempt.  The first aimed at the enemy's most valuable cell, which is his money, and
	* walked past his army: 7-9, twice the losses.  The roadmap asked for the approach where the defence
	* is thinnest, and the engine's threat layer cannot say where that is - it is built from
	* ThingTemplate::ThreatValue, which the shipped data never sets, so every cell reads zero.  So the
	* guns are counted off the objects themselves, the same weighing the retreat and the counter score
	* use, and only what this AI has seen: a defence it has never scouted does not steer it. */
//----------------------------------------------------------------------------------------------------------
AsciiString AIPlayer::chooseApproachLabel( const Coord3D *from, const AsciiString &requested, Int pathSuffix )
{
	if( !runsOwnAI() || !getSkillProfile()->m_useInfluenceMapForAttackLane || from == NULL )
		return requested;

	const Int LANE_COUNT = 3;
	static const char *LANES[ LANE_COUNT ] = { SKIRMISH_CENTER, SKIRMISH_FLANK, SKIRMISH_BACKDOOR };

	Int requestedLane = -1;
	for( Int i = 0; i < LANE_COUNT; ++i )
	{
		if( requested.compareNoCase( LANES[ i ] ) == 0 )
			requestedLane = i;
	}
	if( requestedLane < 0 )
		return requested;		// "Special" and whatever a map names for itself stay the script's business

	Real firepower[ LANE_COUNT ];
	for( Int i = 0; i < LANE_COUNT; ++i )
	{
		AsciiString label;
		label.format( "%s%d", LANES[ i ], pathSuffix );
		Waypoint *way = TheTerrainLogic->getClosestWaypointOnPath( from, label );
		firepower[ i ] = (way == NULL) ? -1.0f : knownFirepowerAlongPath( way );
	}

	const Int lane = aiLeastDefendedLane( firepower, LANE_COUNT, requestedLane );
	if( lane != requestedLane )
		DEBUG_LOG(("AI LANE frame %d player %d takes %s (%.0f) instead of %s (%.0f)\n", TheGameLogic->getFrame(),
			m_player->getPlayerIndex(), LANES[ lane ], firepower[ lane ], LANES[ requestedLane ], firepower[ requestedLane ]));
	return AsciiString( LANES[ lane ] );
}

//----------------------------------------------------------------------------------------------------------
/** The quietest road that is not this one, or empty when the map has no other. */
//----------------------------------------------------------------------------------------------------------
AsciiString AIPlayer::secondApproachLabel( const Coord3D *from, const AsciiString &taken, Int pathSuffix )
{
	if( !runsOwnAI() || !getSkillProfile()->m_useInfluenceMapForAttackLane )
		return AsciiString::TheEmptyString;

	const Int LANE_COUNT = 3;
	static const char *LANES[ LANE_COUNT ] = { SKIRMISH_CENTER, SKIRMISH_FLANK, SKIRMISH_BACKDOOR };

	Real firepower[ LANE_COUNT ];
	for( Int i = 0; i < LANE_COUNT; ++i )
	{
		firepower[ i ] = -1.0f;
		if( taken.compareNoCase( LANES[ i ] ) == 0 )
			continue;
		AsciiString label;
		label.format( "%s%d", LANES[ i ], pathSuffix );
		Waypoint *way = TheTerrainLogic->getClosestWaypointOnPath( from, label );
		if( way )
			firepower[ i ] = knownFirepowerAlongPath( way );
	}

	const Int lane = aiLeastDefendedLane( firepower, LANE_COUNT, -1 );
	return (lane < 0) ? AsciiString::TheEmptyString : AsciiString( LANES[ lane ] );
}

//----------------------------------------------------------------------------------------------------------
Real AIPlayer::knownFirepowerAlongPath( Waypoint *way )
{
	Real firepower = 0.0f;
	for( Int step = 0; way != NULL && step < APPROACH_MAX_WAYPOINTS; ++step )
	{
		firepower += knownFirepowerNear( way->getLocation() );
		Waypoint *next = (way->getNumLinks() > 0) ? way->getLink( 0 ) : NULL;
		if( next && hasTacticalMap() )
		{
			// the whole leg, not only its ends: a gun that covers the middle of a long leg between two
			// waypoints out of its reach is still on the road
			const Coord3D *a = way->getLocation();
			const Coord3D *b = next->getLocation();
			const Real length = sqrt( sqr( b->x - a->x ) + sqr( b->y - a->y ) );
			const Int samples = REAL_TO_INT_FLOOR( length / INFLUENCE_CELL_SIZE );
			for( Int i = 1; i < samples; ++i )
			{
				const Real t = INT_TO_REAL( i ) / INT_TO_REAL( samples );
				firepower += m_influence.enemyAt( a->x + (b->x - a->x) * t, a->y + (b->y - a->y) * t );
			}
		}
		way = next;
	}
	return firepower;
}

static Real fixedGunPower( const Object *obj, Real *reach );
static Real groundAttackRange( const Object *obj );

/** How far past a defence's reach one of ours still counts as under it: a step from walking in.  Short of
	* the 76 a Missile Defender kiting a Gatling Cannon keeps between the two reaches. */
static const Real DEFENCE_FIGHT_SLACK = 30.0f;

/** A known enemy building that shoots the ground: what it shoots with (fixedGunPower) and how far. */
struct KnownDefence
{
	Object *m_object;
	Real m_reach;
	Real m_power;
};

//----------------------------------------------------------------------------------------------------------
/** Every enemy building this player knows of that shoots the ground, its garrison's guns included, in the
	* order of the object list.  What a spot on the map scores is the guns among them that reach it.  A
	* list and not a layer of the influence map: that map is drawn on demand below Hard, its enemy layer
	* mixes the armies in, and every layer of it goes into a save; a match has a few dozen of these at most.
	* Not a superweapon, whose gun reaches the whole map. */
//----------------------------------------------------------------------------------------------------------
static void collectKnownDefences( const Player *player, std::vector<KnownDefence> *defences )
{
	const Int me = player->getPlayerIndex();
	for( Object *obj = TheGameLogic->getFirstObject(); obj; obj = obj->getNextObject() )
	{
		if( obj->isEffectivelyDead() || !(obj->isKindOf( KINDOF_STRUCTURE ) || obj->isKindOf( KINDOF_IMMOBILE )) ||
				obj->isKindOf( KINDOF_FS_SUPERWEAPON ) || player->getRelationship( obj->getTeam() ) != ENEMIES || !observerKnowsAbout( obj, me ) )
			continue;
		KnownDefence defence;
		defence.m_object = obj;
		defence.m_power = fixedGunPower( obj, &defence.m_reach );
		if( defence.m_power > 0.0f )
			defences->push_back( defence );
	}
}

/** How far a unit's primary gun reaches the ground: the one it fires on its own, and not the longest it
	* carries.  A Missile Defender's longest is the laser-guided shot its ability fires, 300, and it walks up
	* to a Gatling Cannon with its 175: sent to shell them by the longest, Medium's died attacking them. */
static Real primaryGroundRange( const Object *obj )
{
	const Weapon *weapon = obj->getWeaponInWeaponSlot( PRIMARY_WEAPON );
	return weapon && (weapon->getTemplate()->getAntiMask() & WEAPON_ANTI_GROUND) != 0 ? weapon->getAttackRange( obj ) : 0.0f;
}

/** ... and a unit template's, off its default weapon set. */
static Real templateGroundReach( const ThingTemplate *tmpl )
{
	const WeaponTemplateSet *set = tmpl->getWeaponTemplateSets().empty() ? NULL : tmpl->findWeaponTemplateSet( WeaponSetFlags() );
	const WeaponTemplate *weapon = set ? set->getNth( PRIMARY_WEAPON ) : NULL;
	return weapon && (weapon->getAntiMask() & WEAPON_ANTI_GROUND) != 0 ? weapon->getUnmodifiedAttackRange() : 0.0f;
}

/** aiMatchupScore for a unit of ours against one kind of the enemy's.  enemyReach is a building's: a
	* building cannot walk up to what outranges it, so a gun that does is never shot back at, the way an
	* Inferno Cannon shells a Gatling Cannon. */
static Real matchupAgainst( const ThingTemplate *tmpl, Real cost, const AIVisibleEnemy &enemy, Real enemyReach )
{
	const Bool outranges = enemyReach > 0.0f && templateGroundReach( tmpl ) >= enemyReach + DEFENCE_FIGHT_SLACK;
	return aiMatchupScore( templateFramesToKill( tmpl, enemy.m_template ),
		outranges ? AI_CANNOT_KILL : templateFramesToKill( enemy.m_template, tmpl ), cost, enemy.m_cost );
}

//----------------------------------------------------------------------------------------------------------
/** What this AI has seen that can shoot, within the approach watch radius of a point. */
//----------------------------------------------------------------------------------------------------------
Real AIPlayer::knownFirepowerNear( const Coord3D *pos )
{
	// the influence map answers the question the radius only approximated: not what stands near the
	// road, but what can shoot onto it, the high ground's reach included.  The radius count below also
	// took in this player's own units, which the affiliation filter lets through whatever it is asked.
	if( hasTacticalMap() )
		return m_influence.enemyAt( pos->x, pos->y );

	PartitionFilterPlayerAffiliation enemies( m_player, ALLOW_ENEMIES, true );
	PartitionFilterAlive alive;
	PartitionFilter *filters[] = { &enemies, &alive, NULL };

	Real firepower = 0.0f;
	ObjectIterator *iter = ThePartitionManager->iterateObjectsInRange( pos, APPROACH_WATCH_RADIUS, FROM_BOUNDINGSPHERE_2D, filters );
	MemoryPoolObjectHolder hold( iter );
	for( Object *obj = iter->first(); obj; obj = iter->next() )
	{
		if( observerKnowsAbout( obj, m_player->getPlayerIndex() ) )
			firepower += aiCombatPower( obj );
	}
	return firepower;
}

//----------------------------------------------------------------------------------------------------------
/** How far along the road to the enemy the forward hold point stands, as a share of the distance. */
static const Real HOLD_POINT_SHARE = 1.0f / 3.0f;

/** A player asked the computer to hold ground instead of sitting in its base between attacks.  So a
	* Hard wave gathers out on its road, a third of the way to the enemy, where the road leaves the
	* base: the ground between the bases is held while the wave fills up, and the attack starts a third
	* of the way there.  Only while nothing this AI has seen can shoot at the spot; otherwise the wave
	* gathers at home as before. */
//----------------------------------------------------------------------------------------------------------
Bool AIPlayer::forwardHoldPoint( const AsciiString &approach, Int pathSuffix, const Coord3D *enemyPos, Coord3D *hold )
{
	AsciiString pathLabel;
	pathLabel.format( "%s%d", approach.str(), pathSuffix );
	Waypoint *way = TheTerrainLogic->getClosestWaypointOnPath( &m_baseCenter, pathLabel );
	const Real reach = HOLD_POINT_SHARE * sqrt( sqr( enemyPos->x - m_baseCenter.x ) + sqr( enemyPos->y - m_baseCenter.y ) );
	for( Int step = 0; way != NULL && step < APPROACH_MAX_WAYPOINTS; ++step )
	{
		const Coord3D *at = way->getLocation();
		if( sqr( at->x - m_baseCenter.x ) + sqr( at->y - m_baseCenter.y ) >= sqr( reach ) )
		{
			if( reach <= m_baseRadius || knownFirepowerNear( at ) > 0.0f )
				return FALSE;
			*hold = *at;
			return TRUE;
		}
		way = (way->getNumLinks() > 0) ? way->getLink( 0 ) : NULL;
	}
	return FALSE;
}

//----------------------------------------------------------------------------------------------------------
/** A wave worth sending: about what seven main battle tanks cost, in the build-cost currency
	* aiCombatPower counts in. */
static const Real WAVE_POWER = 6000.0f;

/** The longest a parked team waits for the rest of its wave. */
static const UnsignedInt WAVE_MAX_HOLD_FRAMES = 90 * LOGICFRAMES_PER_SECOND;

/** How often the parked teams are looked at. */
static const Int WAVE_CHECK_RATE = 2 * LOGICFRAMES_PER_SECOND;

/** The longest a ready wave waits for riders to fill a gunship: a walk across the base, or a Red
	* Guard or two out of the barracks. */
static const UnsignedInt BOARD_WAIT_FRAMES = 20 * LOGICFRAMES_PER_SECOND;

/** A wave this strong is two, and the share of its power that takes the second road. */
static const Real FLANK_WAVE_POWER = 2.0f * WAVE_POWER;
static const Real FLANK_SHARE = 1.0f / 3.0f;

/** A helicopter: flies, and needs no runway.  The Comanche runs on the jet update with the runway
	* switched off, so "not a jet" missed every one of them; a jet that needs a runway flies its own
	* sortie loop and is left to it.  Supply choppers are harvesters.  A general's power plane - a B-52,
	* a paradrop cargo plane, a Spectre - flies on its own script and is not the player's to select; the
	* first version of this sent paradrop planes off with the waves. */
static Bool isHelicopter( const Object *obj )
{
	if( !obj->isKindOf( KINDOF_AIRCRAFT ) || obj->isKindOf( KINDOF_HARVESTER ) || obj->isKindOf( KINDOF_STRUCTURE ) ||
			obj->isKindOf( KINDOF_EMP_HARDENED ) || obj->isKindOf( KINDOF_DRONE ) || !obj->isSelectable() ||
			obj->getAI() == NULL || obj->isEffectivelyDead() )
		return FALSE;
	const JetAIUpdate *jet = obj->getAI()->getJetAIUpdate();
	return jet == NULL || !jet->friend_needsRunway();
}

Bool AIPlayer::isGunshipChinook( const Object *obj ) const
{
	if( obj->getID() == m_lentChinook )
		return FALSE;		// gathering until a real gatherer exists again
	for( Int chinook = 0; chinook < MAX_GUNSHIP_CHINOOKS; ++chinook )
		if( m_gunshipChinook[ chinook ] == obj->getID() )
			return !obj->isEffectivelyDead() && obj->getAI() != NULL;
	return FALSE;
}

Bool AIPlayer::isGunshipAircraft( const Object *obj ) const
{
	return (isHelicopter( obj ) && !isDutyHelix( obj )) || isGunshipChinook( obj );
}

Bool AIPlayer::isTransportChinook( const Object *obj ) const
{
	if( obj->getID() == m_lentChinook )
		return FALSE;
	for( Int shuttle = 0; shuttle < MAX_TRANSPORT_CHINOOKS; ++shuttle )
		if( m_shuttle[ shuttle ].id == obj->getID() )
			return !obj->isEffectivelyDead() && obj->getAI() != NULL;
	return FALSE;
}

Bool AIPlayer::isDutyChinook( const Object *obj ) const
{
	return isGunshipChinook( obj ) || isTransportChinook( obj );
}

Bool AIPlayer::isDutyHelix( const Object *obj ) const
{
	for( Int helix = 0; helix < MAX_DUTY_HELIXES; ++helix )
		if( m_dutyHelix[ helix ].id == obj->getID() )
			return TRUE;
	return FALSE;
}

/** What waits when a wave leaves: attack helicopters left on guard anywhere, and helicopters at home,
	* the gunships with riders among them. */
struct HomeAirSearch
{
	Coord3D home;
	Real reachSqr;
	AIGroup *wave;
	ObjectID ferry;		///< the helicopter carrying the capturer, which stays on that job
	const AIPlayer *owner;	///< whose gunship Chinooks fly with the helicopters
	Bool ridersComing;		///< riders are walking to a firing seat or being trained for one (loadGunships)
};

/** Somebody of this player's is on the way to climb into this helicopter. */
struct BoardingSearch
{
	const Object *transport;
	Bool found;
};

static void findBoarder( Object *obj, void *userData )
{
	BoardingSearch *search = (BoardingSearch *)userData;
	AIUpdateInterface *ai = obj->getAI();
	if( !search->found && ai && ai->getCurrentStateID() == AI_ENTER && ai->getGoalObject() == search->transport )
		search->found = TRUE;
}

static void addHomeHelicopter( Object *obj, void *userData )
{
	HomeAirSearch *search = (HomeAirSearch *)userData;
	if( !search->owner->isGunshipAircraft( obj ) || obj->isContained() || obj->getGroup() == search->wave || obj->getID() == search->ferry )
		return;
	// on guard or doing nothing, wherever the guard team put it; one already out on an attack keeps going
	const StateID state = obj->getAI()->getCurrentStateID();
	const Bool waiting = state == AI_IDLE || state == AI_GUARD || state == AI_GUARD_RETALIATE;
	const Bool atHome = sqr( obj->getPosition()->x - search->home.x ) + sqr( obj->getPosition()->y - search->home.y ) <= search->reachSqr;
	if( !waiting && !atHome )
		return;
	// loadGunships only seats riders who can shoot out, so anything carrying riders is a gunship
	ContainModuleInterface *contain = obj->getContain();
	const Bool carriesShooters = contain && contain->getContainCount() > 0;

	// one with riders still walking over to it waits for them and goes with the next wave: the first
	// version flew three Helixes off empty two seconds after fifteen men were sent to fill them
	if( contain && contain->getContainMax() > 0 )
	{
		BoardingSearch boarding;
		boarding.transport = obj;
		boarding.found = FALSE;
		obj->getControllingPlayer()->iterateObjects( findBoarder, &boarding );
		if( boarding.found )
			return;
	}
	// an empty gunship whose riders are still coming stays for the next wave too, once this one has
	// waited what it will for them: a bunkered Helix flown off empty is a Helix with no guns
	if( search->ridersComing && contain && contain->getContainCount() == 0 && contain->asOpenContain() &&
			contain->asOpenContain()->OpenContain::isPassengerAllowedToFire() )
		return;
	if( obj->isAbleToAttack() || carriesShooters )
	{
		DEBUG_LOG(("AI WAVE frame %d player %d takes '%s' %d off guard, %d riders, %s\n", TheGameLogic->getFrame(),
			obj->getControllingPlayer()->getPlayerIndex(), obj->getTemplate()->getName().str(), obj->getID(), contain ? (Int)contain->getContainCount() : 0,
			contain && contain->asOpenContain() && contain->asOpenContain()->OpenContain::isPassengerAllowedToFire() ? "firing seats" : "no firing seats"));
		search->wave->add( obj );
	}
}

/** Away from home with something to do: on a road, in a fight, hunting.  Whatever is done with the
	* rest of its team, this one is left to it.  One still walking to the point its wave gathers at is
	* waiting, not out. */
Bool AIPlayer::isOutOnOrders( const Object *obj ) const
{
	const StateID state = obj->getAI()->getCurrentStateID();
	if( state == AI_IDLE || state == AI_GUARD || state == AI_GUARD_RETALIATE || state == AI_MOVE_AND_TIGHTEN )
		return FALSE;
	return !isAtHome( obj->getPosition() );
}

/** Within the reach the home guard answers to, two base radii from the base center. */
Bool AIPlayer::isAtHome( const Coord3D *pos ) const
{
	return sqr( pos->x - m_baseCenter.x ) + sqr( pos->y - m_baseCenter.y ) <= sqr( 2.0f * m_baseRadius );
}

/** Something of ours was hit in the last thirty seconds and an enemy that can shoot, one this AI knows
	* of, stands at home.  The attacked frame alone is set by a hit on anything anywhere, and a tank at
	* the enemy's base would have kept every wave at home. */
Bool AIPlayer::isBaseUnderAttack( void ) const
{
	return homeIntruder( NULL, NULL ) != NULL;
}

/** The known enemy standing nearest the base center while the base is under attack (isBaseUnderAttack),
	* how many of them stand at home and, one entry a kind, what they cost; NULL when the base is not
	* under attack. */
Object *AIPlayer::homeIntruder( Int *count, std::vector<AIVisibleEnemy> *army ) const
{
	if( count )
		*count = 0;
	const UnsignedInt attacked = m_player->getAttackedFrame();
	if( !m_baseCenterSet || attacked == 0 || attacked + 30 * LOGICFRAMES_PER_SECOND <= TheGameLogic->getFrame() )
		return NULL;

	PartitionFilterAlive filterAlive;
	PartitionFilterOnMap filterOnMap;
	PartitionFilter *filters[] = { &filterAlive, &filterOnMap, NULL };
	ObjectIterator *iter = ThePartitionManager->iterateObjectsInRange( &m_baseCenter, 2.0f * m_baseRadius, FROM_CENTER_2D, filters );
	MemoryPoolObjectHolder hold( iter );
	Object *nearest = NULL;
	Real nearestSqr = 0.0f;
	for( Object *obj = iter->first(); obj; obj = iter->next() )
	{
		if( obj->isKindOf( KINDOF_PROJECTILE ) || m_player->getRelationship( obj->getTeam() ) != ENEMIES )
			continue;
		if( !obj->isAbleToAttack() || !observerKnowsAbout( obj, m_player->getPlayerIndex() ) )
			continue;
		if( count == NULL )
			return obj;		// only asked whether there is one
		++*count;
		if( army )
			addToVisibleArmy( army, obj->getTemplate(), obj->getControllingPlayer(),
				INT_TO_REAL( obj->getTemplate()->calcCostToBuild( obj->getControllingPlayer() ) ) );
		const Real distSqr = sqr( obj->getPosition()->x - m_baseCenter.x ) + sqr( obj->getPosition()->y - m_baseCenter.y );
		if( nearest == NULL || distSqr < nearestSqr )
		{
			nearest = obj;
			nearestSqr = distSqr;
		}
	}
	return nearest;
}

/** A Tunnel Network put up behind the barracks shoots at nothing until something walks past, and a worker
	* with a Patriot on his mind never shoots at all.  homeIntruder wants a hit and a gun, so on Easy such a
	* tunnel stood for the rest of the match and on Hard for up to four minutes.  This asks for neither: anything
	* that walks or stands, known to this AI, inside the base itself.  It stays out of isBaseUnderAttack: that has
	* some 25 readers and one Patriot would stop the economy and the waves for as long as it stood.  Not an
	* aircraft, which is gone before an answer arrives and is homeIntruder's when it fires. */
Object *AIPlayer::baseSquatter( Int *count, std::vector<AIVisibleEnemy> *army ) const
{
	*count = 0;
	if( !m_baseCenterSet )
		return NULL;
	PartitionFilterAlive filterAlive;
	PartitionFilterOnMap filterOnMap;
	PartitionFilter *filters[] = { &filterAlive, &filterOnMap, NULL };
	// the base radius runs through the outer buildings: a tunnel just behind them is still in the base
	ObjectIterator *iter = ThePartitionManager->iterateObjectsInRange( &m_baseCenter, 1.5f * m_baseRadius, FROM_CENTER_2D, filters );
	MemoryPoolObjectHolder hold( iter );
	Object *nearest = NULL;
	Real nearestSqr = 0.0f;
	for( Object *obj = iter->first(); obj; obj = iter->next() )
	{
		if( !(obj->isKindOf( KINDOF_STRUCTURE ) || obj->isKindOf( KINDOF_INFANTRY ) || obj->isKindOf( KINDOF_VEHICLE )) ||
				obj->isKindOf( KINDOF_AIRCRAFT ) || obj->isKindOf( KINDOF_MINE ) || obj->isKindOf( KINDOF_DEMOTRAP ) || obj->isKindOf( KINDOF_DRONE ) )
			continue;
		if( m_player->getRelationship( obj->getTeam() ) != ENEMIES || !observerKnowsAbout( obj, m_player->getPlayerIndex() ) )
			continue;
		++*count;
		addToVisibleArmy( army, obj->getTemplate(), obj->getControllingPlayer(),
			INT_TO_REAL( obj->getTemplate()->calcCostToBuild( obj->getControllingPlayer() ) ) );
		const Real distSqr = sqr( obj->getPosition()->x - m_baseCenter.x ) + sqr( obj->getPosition()->y - m_baseCenter.y );
		if( nearest == NULL || distSqr < nearestSqr )
		{
			nearest = obj;
			nearestSqr = distSqr;
		}
	}
	return nearest;
}

/** Everything that can build a unit, finished and standing. */
static void collectFactories( Object *obj, void *userData )
{
	if( obj->isEffectivelyDead() || !obj->isKindOf( KINDOF_STRUCTURE ) || obj->testStatus( OBJECT_STATUS_UNDER_CONSTRUCTION ) ||
			obj->getProductionUpdateInterface() == NULL )
		return;
	((std::vector<Object *> *)userData)->push_back( obj );
}

static void collectOwned( Object *obj, void *userData )
{
	((std::vector<Object *> *)userData)->push_back( obj );
}

/** A base defense of this player's is going up. */
static void findDefenseUnderConstruction( Object *obj, void *userData )
{
	if( obj->isKindOf( KINDOF_FS_BASE_DEFENSE ) && obj->testStatus( OBJECT_STATUS_UNDER_CONSTRUCTION ) )
		*(Bool *)userData = TRUE;
}

/** The fighting unit off this factory's buttons that answers one kind of intruder best, money for
	* money, and the dearer of two that answer it equally.  Not a worker, a gatherer, a hacker or an
	* aircraft: a jet flies its sortie and goes home, and the fight is at the door. */
static const ThingTemplate *bestDefender( Object *factory, const Player *owner, const AIVisibleEnemy &enemy,
																					Bool allowHelicopters = FALSE, Real *outScore = NULL, Real enemyReach = 0.0f )
{
	const CommandSet *commandSet = TheCommandStore->findCommandSet( factory->getCommandSetString() );
	if( commandSet == NULL )
		return NULL;

	const ThingTemplate *best = NULL;
	Real bestScore = -1.0f;
	Int bestCost = 0;
	for( Int i = 0; i < MAX_COMMANDS_PER_SET; ++i )
	{
		const CommandButton *button = commandSet->getCommandButton( i );
		if( button == NULL || button->getCommandType() != GUI_COMMAND_UNIT_BUILD )
			continue;
		const ThingTemplate *tmpl = button->getThingTemplate();
		// a helicopter fights where the army is and stays there, which a jet does not
		const Bool aircraftAllowed = allowHelicopters && tmpl && tmpl->isKindOf( KINDOF_PRODUCED_AT_HELIPAD );
		if( tmpl == NULL || !tmpl->canPossiblyHaveAnyWeapon() || tmpl->isKindOf( KINDOF_DOZER ) || tmpl->isKindOf( KINDOF_HARVESTER ) ||
				tmpl->isKindOf( KINDOF_MONEY_HACKER ) || (tmpl->isKindOf( KINDOF_AIRCRAFT ) && !aircraftAllowed) ||
				TheBuildAssistant->canMakeUnit( factory, tmpl ) != CANMAKE_OK )
			continue;

		const Int cost = tmpl->calcCostToBuild( owner );
		const Real score = matchupAgainst( tmpl, INT_TO_REAL( cost ), enemy, enemyReach );
		if( best == NULL || score > bestScore || (score == bestScore && cost > bestCost) )
		{
			best = tmpl;
			bestScore = score;
			bestCost = cost;
		}
	}
	if( outScore )
		*outScore = bestScore;
	return best;
}

/** Count a unit of ours, standing or on order, against the kind of intruder it answers best. */
static void creditAnswer( const ThingTemplate *tmpl, const Player *owner, const std::vector<AIVisibleEnemy> &army, std::vector<Real> *answered )
{
	if( tmpl == NULL || army.empty() || !tmpl->canPossiblyHaveAnyWeapon() )
		return;
	const Real cost = INT_TO_REAL( tmpl->calcCostToBuild( owner ) );
	size_t best = 0;
	Real bestScore = -1.0f;
	for( size_t k = 0; k < army.size(); ++k )
	{
		const Real score = aiMatchupScore( templateFramesToKill( tmpl, army[ k ].m_template ),
			templateFramesToKill( army[ k ].m_template, tmpl ), cost, army[ k ].m_cost );
		if( score > bestScore )
		{
			best = k;
			bestScore = score;
		}
	}
	(*answered)[ best ] += cost;
}

/** A unit that could go at a squatter, and how far it has to go. */
struct SquatterHunter
{
	Object *unit;
	Real distSqr;
};

static Bool nearerHunter( const SquatterHunter &a, const SquatterHunter &b )
{
	return a.distSqr != b.distSqr ? a.distSqr < b.distSqr : a.unit->getID() < b.unit->getID();
}

/** The nearest idle fighters at home, up to worth counted in aiCombatPower with those already on it, by a
	* direct order: an attack move walks past a building that shoots at nothing.  A worker draws one rifle
	* and not the whole wave parked at home. */
void AIPlayer::sendAtSquatter( Object *squatter, Real worth, const std::vector<Object *> &owned )
{
	std::vector<SquatterHunter> hunters;
	for( std::vector<Object *>::const_iterator o = owned.begin(); o != owned.end(); ++o )
	{
		Object *obj = *o;
		if( obj->isEffectivelyDead() || obj->isContained() || obj->getAI() == NULL || obj->isKindOf( KINDOF_IMMOBILE ) ||
				!isAtHome( obj->getPosition() ) || !leavesToFinish( obj ) )
			continue;
		if( obj->getAI()->getGoalObject() == squatter || obj->getAI()->getCurrentVictim() == squatter )
		{
			worth -= aiCombatPower( obj );
			continue;
		}
		const StateID state = obj->getAI()->getCurrentStateID();
		if( state != AI_IDLE && state != AI_GUARD && state != AI_GUARD_RETALIATE && state != AI_MOVE_AND_TIGHTEN )
			continue;
		if( obj->getAbleToAttackSpecificObject( ATTACK_NEW_TARGET, squatter, CMD_FROM_AI ) < ATTACKRESULT_POSSIBLE_AFTER_MOVING )
			continue;
		SquatterHunter hunter;
		hunter.unit = obj;
		hunter.distSqr = sqr( obj->getPosition()->x - squatter->getPosition()->x ) + sqr( obj->getPosition()->y - squatter->getPosition()->y );
		hunters.push_back( hunter );
	}
	std::sort( hunters.begin(), hunters.end(), nearerHunter );
	// an attack order on a building out in the fog ends at once, for a computer player: walk there first
	const Bool inView = ThePartitionManager->getShroudStatusForPlayer( m_player->getPlayerIndex(), squatter->getPosition() ) == CELLSHROUD_CLEAR;
	Int sent = 0;
	for( size_t h = 0; h < hunters.size() && worth > 0.0f; ++h )
	{
		if( inView )
			hunters[ h ].unit->getAI()->aiAttackObject( squatter, NO_MAX_SHOTS_LIMIT, CMD_FROM_AI );
		else
			hunters[ h ].unit->getAI()->aiAttackMoveToPosition( squatter->getPosition(), NO_MAX_SHOTS_LIMIT, CMD_FROM_AI );
		worth -= aiCombatPower( hunters[ h ].unit );
		++sent;
	}
	if( sent > 0 )
		DEBUG_LOG(("AI SQUATTER frame %d player %d sends %d idle units at '%s' at (%.0f,%.0f), base radius %.0f\n", TheGameLogic->getFrame(),
			m_player->getPlayerIndex(), sent, squatter->getTemplate()->getName().str(), squatter->getPosition()->x, squatter->getPosition()->y, m_baseRadius));
}

//----------------------------------------------------------------------------------------------------------
/** A base under attack spends its bank on the fight.  A Hard USA rushed a minute in had two fighting
	* units and 8,000 in the bank, because the opening goes on buildings first and the team plan trains
	* at its own pace: sixteen rebels stood in its base for a minute and lost one.  So while the base is
	* hit every factory that can train a fighter queues two, for as long as the money lasts, and whatever
	* of ours stands idle at home is sent at the intruder nearest the middle of the base.  Each one is the
	* best answer to the kind of intruder the least answered so far, money for money: answering the whole
	* crowd at once picked Missile Defenders against sixteen rebels and four Scorpions, since the tanks
	* weigh as much as the rifles and a rocket beats a tank by more than it loses to a rifle.  Every rung
	* and every side: the factories' own buttons say what can be trained. */
//----------------------------------------------------------------------------------------------------------
void AIPlayer::defendHome( void )
{
	if( !runsOwnAI() )
		return;
	Int intruders = 0;
	std::vector<AIVisibleEnemy> army;
	Object *intruder = homeIntruder( &intruders, &army );
	// nobody has been hit: still look round the base on the rung's clock, this being a turn of doWaves' tick
	const Bool squatter = intruder == NULL;
	if( squatter )
	{
		const Real watch = getSkillProfile()->m_baseWatchSeconds;
		if( watch <= 0.0f || (TheGameLogic->getFrame() + computeUpdatePhase( m_player->getPlayerIndex(), WAVE_CHECK_RATE )) % rungFrames( watch ) >= WAVE_CHECK_RATE )
			return;
		intruder = baseSquatter( &intruders, &army );
		if( intruder == NULL )
			return;
	}
	if( m_persona != AIPERSONA_NORMAL && !squatter )
		personaChat( "Hit" );

	// Two deep.  The queue this builds reads to doEconomy as a barracks that cannot keep up, and a rushed
	// China bought a second barracks in the middle of the fight where its next building used to be the
	// war factory; doEconomy buys nothing while the base is hit, which is what keeps that out.
	const Int EMERGENCY_QUEUE = 2;
	std::vector<Object *> owned;
	m_player->iterateObjects( collectOwned, &owned );
	std::vector<Object *> factories;
	m_player->iterateObjects( collectFactories, &factories );

	// what is already answering: the fighters at home and everything in the factories' queues
	std::vector<Real> answered( army.size(), 0.0f );
	for( std::vector<Object *>::const_iterator o = owned.begin(); o != owned.end(); ++o )
	{
		if( !(*o)->isEffectivelyDead() && !(*o)->isKindOf( KINDOF_STRUCTURE ) && isAtHome( (*o)->getPosition() ) && leavesToFinish( *o ) )
			creditAnswer( (*o)->getTemplate(), m_player, army, &answered );
	}
	for( std::vector<Object *>::const_iterator f = factories.begin(); f != factories.end(); ++f )
	{
		const ProductionUpdateInterface *pu = (*f)->getProductionUpdateInterface();
		for( const ProductionEntry *entry = pu->firstProduction(); entry; entry = pu->nextProduction( entry ) )
			if( entry->getProductionType() == PRODUCTION_UNIT )		// an upgrade shares the pointer's storage
				creditAnswer( entry->getProductionObject(), m_player, army, &answered );
	}

	for( std::vector<Object *>::const_iterator f = factories.begin(); f != factories.end(); ++f )
	{
		ProductionUpdateInterface *pu = (*f)->getProductionUpdateInterface();
		while( pu->getProductionCount() < EMERGENCY_QUEUE )
		{
			// the kind with the most of its worth still unanswered, the first seen of two alike
			size_t kind = 0;
			for( size_t k = 1; k < army.size(); ++k )
				if( army[ k ].m_weight - answered[ k ] > army[ kind ].m_weight - answered[ kind ] )
					kind = k;
			// a squatter gets what it is worth and no more: the bank is for the match, which is still on
			if( squatter && answered[ kind ] >= army[ kind ].m_weight * getSkillProfile()->m_baseWatchOdds )
				break;
			Real score = 0.0f;
			const ThingTemplate *unit = bestDefender( *f, m_player, army[ kind ], FALSE, &score );
			// and never an Ambulance bought because nothing here hurts it
			if( unit == NULL || (squatter && score <= 0.0f) || !buyUnit( pu, unit, AIBUY_EMERGENCY ) )
				break;
			answered[ kind ] += INT_TO_REAL( unit->calcCostToBuild( m_player ) );
			DEBUG_LOG(("AI DEFEND frame %d player %d trains '%s' at '%s' against %d intruders, %d in the bank\n",
				TheGameLogic->getFrame(), m_player->getPlayerIndex(), unit->getName().str(), (*f)->getTemplate()->getName().str(),
				intruders, m_player->getMoney()->countMoney()));
		}
	}

	// a gun between the middle of the base and the intruder, one at a time: the factories train one
	// unit at a time whatever the bank holds, and a China with 12,500 and one barracks spent 5,000 of
	// it over the minute its base was being hit
	Object *dozer = NULL;
	m_player->iterateObjects( findAnyDozer, &dozer );
	if( dozer && m_player->getCanBuildBase() && !squatter )
	{
		const ThingTemplate *defense = nextBaseDefense( dozer );
		Bool going = FALSE;
		m_player->iterateObjects( findDefenseUnderConstruction, &going );
		if( defense && !going && !priorityBuildPending( m_player, defense ) )
		{
			Coord3D spot = m_baseCenter;
			Coord2D toward;
			toward.x = intruder->getPosition()->x - m_baseCenter.x;
			toward.y = intruder->getPosition()->y - m_baseCenter.y;
			if( toward.length() > 0.0f )
			{
				toward.normalize();
				spot.x += toward.x * DEFENSE_STANDOFF * m_baseRadius;
				spot.y += toward.y * DEFENSE_STANDOFF * m_baseRadius;
			}
			const Bool placed = placeNear( defense, &spot, 0.0f, FALSE );
			DEBUG_LOG(("AI DEFEND frame %d player %d %s a '%s' against the intruder, %d in the bank\n", TheGameLogic->getFrame(),
				m_player->getPlayerIndex(), placed ? "puts up" : "has no room for", defense->getName().str(), m_player->getMoney()->countMoney()));
		}
	}

	// whatever stands at home with nothing to do but guard goes at the intruder, once there is enough of
	// it: a rushed China sent its Tank Hunters out by twos into sixteen rebels and lost them, where they
	// had stood by the Gattling Cannon and let it do the work
	Real homePower = 0.0f;
	std::vector<AIVisibleEnemy> home;
	for( std::vector<Object *>::const_iterator o = owned.begin(); o != owned.end(); ++o )
	{
		if( !(*o)->isEffectivelyDead() && !(*o)->isKindOf( KINDOF_STRUCTURE ) && isAtHome( (*o)->getPosition() ) && leavesToFinish( *o ) )
		{
			homePower += aiCombatPower( *o );
			addToArmy( &home, *o );
		}
	}
	Real intruderWorth = 0.0f;
	for( std::vector<AIVisibleEnemy>::const_iterator k = army.begin(); k != army.end(); ++k )
		intruderWorth += k->m_weight;
	// by unit against unit as well as price: four Tank Hunters are a match for two Overlords that cost as
	// much again, and four Crusaders are not.  The advantage is the square law's, so its root scales power
	if( getSkillProfile()->m_engageGate )
		homePower *= (Real)sqrt( armyAdvantage( home, army ) );
	// one that cannot shoot is worth going at with anything
	if( homePower < 0.5f * intruderWorth && !(squatter && !intruder->isAbleToAttack()) )
		return;
	// A gun standing still outside the base itself is not coming in: on a small map the enemy's own towers
	// and an expansion's Patriots sit within two base radii, and idle units sent at them one or two at a
	// time were the biggest loss to defences in 32 Hard against Medium matches.  They go only with odds
	// that win, Easy the least careful.  A unit that can move is still met wherever it stands at home:
	// gating those too slowed the quick wins on small maps and lost more over the longer matches.
	static const Real OUTSIDE_BASE_ODDS[ AISKILL_COUNT ] = { 1.0f, 1.5f, 2.0f };
	const Bool outsideBase =
		sqr( intruder->getPosition()->x - m_baseCenter.x ) + sqr( intruder->getPosition()->y - m_baseCenter.y ) > sqr( m_baseRadius );
	if( intruder->isKindOf( KINDOF_IMMOBILE ) && outsideBase && homePower < OUTSIDE_BASE_ODDS[ m_skillLevel ] * intruderWorth )
		return;
	// Nor at one outside the base standing under the enemy's guns, unless with the same odds against the
	// guns as well.  Home is two base radii, and on Tournament Desert that reaches China's Gatling Cannons by
	// the middle supply docks.  A Hard Air Force there sent idle units at an intruder 114 times a match, and
	// attack moves of its default team were its largest loss to buildings, 175 of 736 over eight matches.
	// The guns standing at home are intruders, already in intruderWorth, and only the rest add to it.  Inside
	// the base itself nothing holds the guard back, or a tower built in the middle of it would be left to shoot
	Bool covered = FALSE;
	Real cover = 0.0f;
	if( outsideBase )
	{
		std::vector<KnownDefence> defences;
		collectKnownDefences( m_player, &defences );
		const Coord3D *at = intruder->getPosition();
		for( size_t d = 0; d < defences.size(); ++d )
		{
			const Coord3D *gun = defences[ d ].m_object->getPosition();
			if( sqr( gun->x - at->x ) + sqr( gun->y - at->y ) > sqr( defences[ d ].m_reach + DEFENCE_FIGHT_SLACK ) )
				continue;
			covered = TRUE;
			if( !isAtHome( gun ) )
				cover += defences[ d ].m_power;
		}
	}
	if( covered && homePower < OUTSIDE_BASE_ODDS[ m_skillLevel ] * (intruderWorth + cover) )
		return;

	if( squatter )
	{
		sendAtSquatter( intruder, intruderWorth * getSkillProfile()->m_baseWatchOdds, owned );
		return;
	}

	Int sent = 0;
	for( std::vector<Object *>::const_iterator o = owned.begin(); o != owned.end(); ++o )
	{
		Object *obj = *o;
		if( obj->isEffectivelyDead() || obj->isContained() || obj->getAI() == NULL || obj->isKindOf( KINDOF_IMMOBILE ) ||
				!isAtHome( obj->getPosition() ) || !leavesToFinish( obj ) )
			continue;
		const StateID state = obj->getAI()->getCurrentStateID();
		if( state != AI_IDLE && state != AI_GUARD && state != AI_GUARD_RETALIATE && state != AI_MOVE_AND_TIGHTEN )
			continue;
		obj->getAI()->aiAttackMoveToPosition( intruder->getPosition(), NO_MAX_SHOTS_LIMIT, CMD_FROM_AI );
		++sent;
	}
	if( sent > 0 )
		DEBUG_LOG(("AI DEFEND frame %d player %d sends %d idle units at the intruder '%s' at (%.0f,%.0f)\n", TheGameLogic->getFrame(),
			m_player->getPlayerIndex(), sent, intruder->getTemplate()->getName().str(), intruder->getPosition()->x, intruder->getPosition()->y));
}

/** How often the army is looked at for a part of the enemy's nothing answers, how big a part has to be
	* before a factory is turned over to it, what counts as an answer money for money, and what the bank
	* keeps back for the team plan. */
static const Int ANSWER_RATE = 10 * LOGICFRAMES_PER_SECOND;
/** The last third of the player's slot, as ECONOMY_SLOT_STEP splits the economy's, but off this rate. */
static const Int ANSWER_SLOT_OFFSET = 2 * ( ANSWER_RATE / ( 3 * MAX_PLAYER_COUNT ) );
static const Real ANSWER_MIN_GAP = 1500.0f;
static const Real ANSWER_SCORE = 0.6f;
static const Int ANSWER_RESERVE = 1000;

/** Count a unit of ours against the enemy kind it answers (ANSWER_SCORE) that has the most of its worth
	* still unanswered.  Not the one it answers best: a Missile Defender answers a Battlemaster even better
	* than an Overlord, and counted that way, two Battlemasters at home soaked up every one trained against
	* twenty Overlords. */
static void creditWhereNeeded( const ThingTemplate *tmpl, const Player *owner, const std::vector<AIVisibleEnemy> &army,
															 const std::vector<Real> &reach, std::vector<Real> *answered )
{
	if( tmpl == NULL || !tmpl->canPossiblyHaveAnyWeapon() )
		return;
	const Real cost = INT_TO_REAL( tmpl->calcCostToBuild( owner ) );
	Int best = -1;
	for( size_t k = 0; k < army.size(); ++k )
	{
		if( matchupAgainst( tmpl, cost, army[ k ], reach[ k ] ) < ANSWER_SCORE )
			continue;
		if( best < 0 || army[ k ].m_weight - (*answered)[ k ] > army[ best ].m_weight - (*answered)[ best ] )
			best = (Int)k;
	}
	if( best >= 0 )
		(*answered)[ best ] += cost;
}

//----------------------------------------------------------------------------------------------------------
/** B1/B3 outside an emergency.  defendHome trained the right answer to whatever stood in the base; the
	* rest of the match the AI built from the script's team list, weighted by what it could see that
	* second, and the skirmish scripts' own counter teams wait for fifteen enemy tanks.  A player with ten
	* Overlords was answered by Crusaders until they reached the base.  So every ten seconds the enemy army
	* the AI remembers is set against its own: each of its units, and each one on order, counts against
	* the enemy kind it answers best, if it answers it at all (ANSWER_SCORE).  The kind with the most of
	* its worth unanswered gets one of its best answer from every idle factory that has one, helicopters
	* included, while the bank holds the price and a reserve.
	*
	* Its known defences count too once they are heavy, a share DEFENCES_HEAVY_SHARE of the army it
	* remembers: what shells them from beyond their reach (an Inferno Cannon, a Tomahawk) never takes a shot
	* back, and an aircraft never does from a gun that cannot shoot up.  Left out, the counter-build only
	* ever answered tanks, and the waves met a Gatling Cannon line with more tanks. */
//----------------------------------------------------------------------------------------------------------
static const Real DEFENCES_HEAVY_SHARE = 0.5f;

/** One gun of a defence into the army by kind, at its combat power, with the farthest reach any of its
	* kind shoots from. */
static void addDefenceGun( std::vector<AIVisibleEnemy> *guns, std::vector<Real> *reach, const Object *gun, Real from )
{
	addToVisibleArmy( guns, gun->getTemplate(), gun->getControllingPlayer(), aiCombatPower( gun ) );
	reach->resize( guns->size(), 0.0f );
	for( size_t k = 0; k < guns->size(); ++k )
		if( (*guns)[ k ].m_template == gun->getTemplate() && from > (*reach)[ k ] )
			(*reach)[ k ] = from;
}

void AIPlayer::answerArmy( void )
{
	if( !runsOwnAI() || !m_baseCenterSet || !getSkillProfile()->m_answerArmy ||
			(TheGameLogic->getFrame() + computeUpdatePhase( m_player->getPlayerIndex(), ANSWER_RATE ) + ANSWER_RATE - ANSWER_SLOT_OFFSET) % ANSWER_RATE != 0 )
		return;
	if( isBaseUnderAttack() )
		return;		// defendHome's
	std::vector<AIVisibleEnemy> army;
	rememberedArmy( &army );
	std::vector<Real> reach( army.size(), 0.0f );
	Real armyWorth = 0.0f;
	for( size_t k = 0; k < army.size(); ++k )
		armyWorth += army[ k ].m_weight;
	std::vector<KnownDefence> defences;
	collectKnownDefences( m_player, &defences );
	std::vector<AIVisibleEnemy> guns;
	std::vector<Real> gunReach;
	Real gunsWorth = 0.0f;
	for( size_t d = 0; d < defences.size(); ++d )
	{
		// a building by its own gun, and its garrison by the men in it: a Bunker's own template shoots nothing,
		// and every unit scored a sure win against it
		const Object *building = defences[ d ].m_object;
		if( groundAttackRange( building ) > 0.0f )
			addDefenceGun( &guns, &gunReach, building, defences[ d ].m_reach );
		const ContainModuleInterface *contain = building->getContain();
		if( contain && contain->isPassengerAllowedToFire() )
		{
			const ContainedItemsList *items = contain->getContainedItemsList();
			for( ContainedItemsList::const_iterator it = items->begin(); it != items->end(); ++it )
				if( groundAttackRange( *it ) > 0.0f )
					addDefenceGun( &guns, &gunReach, *it, defences[ d ].m_reach );
		}
		gunsWorth += defences[ d ].m_power;
	}
	if( gunsWorth > 0.0f && gunsWorth >= DEFENCES_HEAVY_SHARE * armyWorth )
	{
		army.insert( army.end(), guns.begin(), guns.end() );
		reach.insert( reach.end(), gunReach.begin(), gunReach.end() );
	}
	if( army.empty() )
		return;

	std::vector<Object *> owned;
	m_player->iterateObjects( collectOwned, &owned );
	std::vector<Object *> factories;
	m_player->iterateObjects( collectFactories, &factories );
	std::vector<Real> answered( army.size(), 0.0f );
	for( std::vector<Object *>::const_iterator o = owned.begin(); o != owned.end(); ++o )
		if( !(*o)->isEffectivelyDead() && !(*o)->isKindOf( KINDOF_STRUCTURE ) && leavesToFinish( *o ) )
			creditWhereNeeded( (*o)->getTemplate(), m_player, army, reach, &answered );
	for( std::vector<Object *>::const_iterator f = factories.begin(); f != factories.end(); ++f )
	{
		const ProductionUpdateInterface *pu = (*f)->getProductionUpdateInterface();
		for( const ProductionEntry *entry = pu->firstProduction(); entry; entry = pu->nextProduction( entry ) )
			if( entry->getProductionType() == PRODUCTION_UNIT )
				creditWhereNeeded( entry->getProductionObject(), m_player, army, reach, &answered );
	}

	size_t kind = 0;
	for( size_t k = 1; k < army.size(); ++k )
		if( army[ k ].m_weight - answered[ k ] > army[ kind ].m_weight - answered[ kind ] )
			kind = k;
	const Real gap = army[ kind ].m_weight - answered[ kind ];
	if( gap < ANSWER_MIN_GAP )
		return;

	for( std::vector<Object *>::const_iterator f = factories.begin(); f != factories.end() && answered[ kind ] < army[ kind ].m_weight; ++f )
	{
		ProductionUpdateInterface *pu = (*f)->getProductionUpdateInterface();
		if( pu->getProductionCount() > 1 )
			continue;		// one behind the unit in hand at most: the team plan keeps the rest of the queue, and its mix
		Real score = 0.0f;
		const ThingTemplate *unit = bestDefender( *f, m_player, army[ kind ], TRUE, &score, reach[ kind ] );
		if( unit == NULL || score < ANSWER_SCORE )
			continue;
		const Int cost = unit->calcCostToBuild( m_player );
		if( m_player->getMoney()->countMoney() < (UnsignedInt)(cost + ANSWER_RESERVE) || !buyUnit( pu, unit, AIBUY_ARMY ) )
			continue;
		answered[ kind ] += INT_TO_REAL( cost );
		DEBUG_LOG(("AI ANSWER frame %d player %d trains '%s' at '%s' against '%s', %.0f of %.0f unanswered, score %.2f, %d in the bank\n",
			TheGameLogic->getFrame(), m_player->getPlayerIndex(), unit->getName().str(), (*f)->getTemplate()->getName().str(),
			army[ kind ].m_template->getName().str(), gap, army[ kind ].m_weight, score, m_player->getMoney()->countMoney()));
	}
}

/** How a unit of ours trades with the enemy army this AI remembers, money for money: the value-weighted
	* mean of aiMatchupScore over every kind in it, 0.5 with nothing remembered. */
static Real armyMatchup( const ThingTemplate *tmpl, const Player *owner, const std::vector<AIVisibleEnemy> &army )
{
	const Real cost = INT_TO_REAL( tmpl->calcCostToBuild( owner ) );
	Real weighed = 0.0f;
	Real scored = 0.0f;
	for( size_t k = 0; k < army.size(); ++k )
	{
		weighed += army[ k ].m_weight;
		scored += army[ k ].m_weight * aiMatchupScore( templateFramesToKill( tmpl, army[ k ].m_template ),
			templateFramesToKill( army[ k ].m_template, tmpl ), cost, army[ k ].m_cost );
	}
	return weighed > 0.0f ? scored / weighed : 0.5f;
}

/** The fighting unit off this factory's buttons a bank over its target goes on: the best trade against
	* the remembered enemy army as far as the rung counters at all (aiCounterFit), and the dearer of two
	* that score the same, so a rung that never counters buys the heaviest thing the factory makes.  Not a
	* worker, a gatherer, a hacker or a jet; a helicopter stays with the army, so it may.
	*
	* With a personality's taste, a unit of a kind it likes comes before every other, and an Air one
	* takes jets as well. */
/** How far the enemy's anti-air may cut an Air AI's taste for aircraft, as a share of it: below this it
	* stopped building air at all, and the owner wants an air force against anti-air too, of the aircraft
	* that trade best with it (the counter pick does that) and with more escorts. */
static const Real AIR_TASTE_FLOOR = 0.5f;

/** The personality's row, an Air one's taste for aircraft cut by the share of the enemy's known guns
	* that shoot up (aiPersonaAirBias), never below AIR_TASTE_FLOOR of it.  An Air AI whose side has no
	* airfield, the GLA, likes vehicles instead, and its favoured units are its rockets and its anti-air
	* (trainFavoured, isAirAnalogue). */
AIPersonaWeights AIPlayer::personaKindTaste( void ) const
{
	AIPersonaWeights taste = personaWeights();
	if( taste.m_air > 0.0f && !sideFlies() )
	{
		taste.m_vehicle = taste.m_air;
		taste.m_air = 0.0f;
	}
	return taste;
}

AIPersonaWeights AIPlayer::personaTaste( void ) const
{
	AIPersonaWeights taste = personaKindTaste();
	if( taste.m_air > 0.0f )
	{
		std::vector<AIKnownGun> guns;
		collectKnownGuns( &guns );
		Int antiAir = 0;
		for( size_t g = 0; g < guns.size(); ++g )
			if( guns[ g ].m_airReach > 0.0f )
				++antiAir;
		const Real cut = aiPersonaAirBias( taste.m_air, guns.empty() ? 0.0f : INT_TO_REAL( antiAir ) / INT_TO_REAL( (Int)guns.size() ) );
		taste.m_air = max( cut, AIR_TASTE_FLOOR * taste.m_air );
	}
	return taste;
}

Int AIPlayer::personaShareKind( void ) const
{
	if( m_persona == AIPERSONA_NORMAL )
		return -1;
	if( m_persona == AIPERSONA_SNEAKY )
		return SHARE_STEALTH;
	if( m_persona == AIPERSONA_AIR && !sideFlies() )
		return SHARE_AIR_ANALOGUE;
	return personaFavouredKind( personaKindTaste() );
}

Bool AIPlayer::superweaponIsPlan( void ) const
{
	return m_persona == AIPERSONA_TECH || ( m_persona == AIPERSONA_AIR && !sideFlies() );
}

static Bool airfieldOnButtons( const Object *builder )
{
	const CommandSet *commandSet = TheCommandStore->findCommandSet( builder->getCommandSetString() );
	for( Int i = 0; commandSet && i < MAX_COMMANDS_PER_SET; ++i )
	{
		const CommandButton *button = commandSet->getCommandButton( i );
		if( button && button->getCommandType() == GUI_COMMAND_DOZER_CONSTRUCT && button->getThingTemplate() &&
				button->getThingTemplate()->isKindOf( KINDOF_FS_AIRFIELD ) )
			return TRUE;
	}
	return FALSE;
}

//----------------------------------------------------------------------------------------------------------
/** Whether this side builds airfields at all, off a dozer's buttons, whatever their prerequisites; TRUE
	* with no dozer to ask, which keeps what the side was doing. */
//----------------------------------------------------------------------------------------------------------
Bool AIPlayer::sideFlies( void ) const
{
	Object *dozer = NULL;
	m_player->iterateObjects( findAnyDozer, &dozer );
	return dozer == NULL || airfieldOnButtons( dozer );
}

/** What stands in for an aircraft in a GLA Air AI's army: a vehicle with a gun that shoots up (a Quad
	* Cannon) or one that reaches like artillery (a Rocket Buggy, a SCUD Launcher), off its own weapons, so
	* every general's variant counts. */
static const Real AIR_ANALOGUE_REACH = 300.0f;

static Bool isAirAnalogue( const ThingTemplate *tmpl )
{
	if( !tmpl->isKindOf( KINDOF_VEHICLE ) || tmpl->isKindOf( KINDOF_AIRCRAFT ) )
		return FALSE;
	const WeaponTemplateSet *set = tmpl->getWeaponTemplateSets().empty() ? NULL : tmpl->findWeaponTemplateSet( WeaponSetFlags() );
	for( Int slot = 0; set && slot < WEAPONSLOT_COUNT; ++slot )
	{
		const WeaponTemplate *weapon = set->getNth( (WeaponSlotType)slot );
		if( weapon && ( ( weapon->getAntiMask() & WEAPON_ANTI_AIRBORNE_VEHICLE ) != 0 || weapon->getUnmodifiedAttackRange() >= AIR_ANALOGUE_REACH ) )
			return TRUE;
	}
	return FALSE;
}

static const ThingTemplate *surplusUnit( Object *factory, const Player *owner, const std::vector<AIVisibleEnemy> &army, Real weight,
																				 const AIPersonaWeights *taste = NULL )
{
	const CommandSet *commandSet = TheCommandStore->findCommandSet( factory->getCommandSetString() );
	if( commandSet == NULL )
		return NULL;

	const Bool jets = taste && taste->m_air > 0.0f;
	const ThingTemplate *best = NULL;
	Real bestFit = 0.0f;
	Int bestCost = 0;
	Bool bestLiked = FALSE;
	for( Int i = 0; i < MAX_COMMANDS_PER_SET; ++i )
	{
		const CommandButton *button = commandSet->getCommandButton( i );
		if( button == NULL || button->getCommandType() != GUI_COMMAND_UNIT_BUILD )
			continue;
		const ThingTemplate *tmpl = button->getThingTemplate();
		if( tmpl == NULL || !tmpl->canPossiblyHaveAnyWeapon() || tmpl->isKindOf( KINDOF_DOZER ) || tmpl->isKindOf( KINDOF_HARVESTER ) ||
				tmpl->isKindOf( KINDOF_MONEY_HACKER ) || (tmpl->isKindOf( KINDOF_AIRCRAFT ) && !tmpl->isKindOf( KINDOF_PRODUCED_AT_HELIPAD ) && !jets) ||
				TheBuildAssistant->canMakeUnit( factory, tmpl ) != CANMAKE_OK )
			continue;
		const Int cost = tmpl->calcCostToBuild( owner );
		const Real fit = aiCounterFit( armyMatchup( tmpl, owner, army ), weight );
		const Bool liked = taste && unitTaste( *taste, tmpl ) > 0.0f;
		if( taste && liked != bestLiked && best != NULL )
		{
			if( !liked )
				continue;
		}
		else if( !(best == NULL || fit > bestFit || (fit == bestFit && cost > bestCost)) )
			continue;
		best = tmpl;
		bestFit = fit;
		bestCost = cost;
		bestLiked = liked;
	}
	return best;
}

//----------------------------------------------------------------------------------------------------------
/** The bank a computer kept was the part of its income nothing was ever bought with: a Hard AI facing
	* Medium ended its matches on 12,300 on average, a Medium one on up to 62,000.  So every rung
	* keeps a bank of so many seconds of what it actually earns (m_bankTargetSeconds, aiBankTarget), and
	* what is over that goes on upgrades and on extra units within the rung's share (aiSurplusBudget).
	* Another factory goes up whenever every one of its kind has stayed busy (addProductionForQueues).
	* The upgrades that pay for the standing army are bought below the target as well, each rung from
	* its own point in the match, and all of it goes through the purchase queue, which keeps below the
	* same target. */
//----------------------------------------------------------------------------------------------------------
void AIPlayer::doSpending( void )
{
	if( !purchaseQueueOn() )
		return;
	const UnsignedInt now = TheGameLogic->getFrame();
	if( (now + computeUpdatePhase( m_player->getPlayerIndex(), ECONOMY_CHECK_RATE ) + ECONOMY_CHECK_RATE - ECONOMY_SLOT_STEP) % ECONOMY_CHECK_RATE != 0 )
		return;

	// whether every finished factory of each kind is full on this pass, into a rolling window the gate
	// reads as a share (queueExtraFactory); one going up empties it, so the new one has to prove itself
	// full too
	const Int window = aiBacklogSamples( getSkillProfile()->m_factoryBacklogSeconds, ECONOMY_CHECK_RATE );
	for( Int k = 0; k < PRODUCTION_KINDS; ++k )
	{
		const FactoryLoad load = readFactoryLoad( m_player, PRODUCTION_KIND[ k ], &m_producedSincePass );
		aiSampleFactories( &m_factoryFullHistory[ k ], &m_factoryFullCount[ k ],
			factoryKindFull( load, m_parkingBlockedFrame ), load.constructing );
		if( load.finished > 0 && m_factoryFullCount[ k ] > 0 && m_factoryFullCount[ k ] % window == 0 )
		{
			Int full = 0;
			for( Int i = 0; i < window; ++i )
				if( m_factoryFullHistory[ k ] & ( 1u << i ) )
					++full;
			DEBUG_LOG(("AI FACTORY frame %d player %d %s full on %d of the last %d passes, %d standing, %d in the bank\n",
				now, m_player->getPlayerIndex(), k == 0 ? "war factory" : k == 1 ? "barracks" : "airfield",
				full, window, load.finished, m_player->getMoney()->countMoney()));
		}
	}
	m_producedSincePass.clear();

	// the income, off what the score screen counts as earned: gathered, hacked, captured and given
	const Int bank = (Int)m_player->getMoney()->countMoney();
	const Int earned = m_player->getScoreKeeper()->getTotalMoneyEarned();
	if( now - m_incomeMarkFrame >= AI_INCOME_WINDOW_FRAMES )
	{
		const Real window = INT_TO_REAL( earned - m_incomeMark ) * LOGICFRAMES_PER_SECOND / INT_TO_REAL( now - m_incomeMarkFrame );
		const Int bankWindowAgo = m_bankMark;
		m_incomePerSecond = m_incomeMarkFrame == 0 ? window : 0.5f * (m_incomePerSecond + window);
		m_incomeMark = earned;
		m_incomeMarkFrame = now;
		m_bankMark = bank;
		DEBUG_LOG(("AI ECONOMY frame %d player %d earns %.0f a second, %d in the bank (%d a window ago), bank target %d, %s\n", now,
			m_player->getPlayerIndex(), m_incomePerSecond, bank, bankWindowAgo, bankTarget(), bank > bankTarget() ? "spends the rest" : "keeps it"));
	}

	if( isBaseUnderAttack() )
		return;		// defendHome's
	addProductionForQueues();
	if( m_persona != AIPERSONA_NORMAL )
		trainFavoured();
	const Int target = bankTarget();
	const Bool overTarget = bank > target;
	buyUpgradesForArmy( overTarget );
	if( !overTarget )
		return;
	if( m_persona == AIPERSONA_ECONOMIST )
		shareSurplusWithAllies( bank, target );
	//
	// A match that starts on 50,000 handed every AI a bank no income window ever saw coming: the
	// surplus is a share of what it earns, and in the first minutes it earns nothing, so the fortune
	// sat there while the build list ticked along. What is over the default starting cash, up to
	// what the start gave over it, goes out at the rung's share a pass. At the default cash the
	// fortune is 0 and nothing here changes.
	//
	const Int keep = max( target, (Int)TheGlobalData->m_defaultStartingCash.countMoney() );
	const Int fortune = aiFortuneBudget( bank, keep, m_startFortune, getSkillProfile()->m_surplusShare );
	const Int surplus = aiSurplusBudget( (Int)m_player->getMoney()->countMoney(), target, m_incomePerSecond, getSkillProfile()->m_surplusShare );
	if( fortune > surplus )
		DEBUG_LOG(("AI ECONOMY frame %d player %d spends its starting fortune, %d of it this pass, %d in the bank, %d kept\n", now,
			m_player->getPlayerIndex(), fortune, bank, keep));
	trainSurplus( max( surplus, fortune ) );
}

//----------------------------------------------------------------------------------------------------------
/** Only the scripts ever bought an upgrade, from a list written before anyone knew what the AI would
	* field.  This buys the one player upgrade, of every one a standing building offers, that the army on
	* the field gets the most out of for its price: every fighting unit the upgrade would change counts
	* at its price weighed by how it trades with the enemy army this AI remembers (aiCounterFit, as far
	* as the rung counters), a building or an unarmed unit at m_upgradeNonCombatWeight of its price (at
	* full price a GLA base made Fortified Structure the first upgrade of every match), and it is bought when that sum is m_upgradePayoff times
	* the price, half that when the bank is over its target.  One a pass, not before m_upgradeStartSeconds,
	* at the building with the shortest queue that offers it, through the purchase queue like everything
	* else. */
//----------------------------------------------------------------------------------------------------------
static const Real PERSONA_UPGRADE_LIKED = 2.0f;
static const Real PERSONA_UPGRADE_DISLIKED = 0.5f;
/** What lifts an upgrade most of whose worth is the personality's own kind over that kind's units in the
	* queue (55 + 10 against 35): an Infantry China spent every dollar on soldiers and never bought
	* Nationalism, which its Normal opponent had at six minutes. */
static const Int PERSONA_KIND_UPGRADE_SCORE = 35;

/** The unit's control bar shows this upgrade as one of its own.  Nationalism has no upgrade module on a
	* Red Guard (the horde reads the player's upgrade), so affectedByUpgrade never counted it and an
	* Infantry China bought it at nine minutes. */
static Bool showsUpgrade( const ThingTemplate *tmpl, const UpgradeTemplate *upgrade )
{
	for( Int c = 0; c < MAX_UPGRADE_CAMEO_UPGRADES; ++c )
		if( tmpl->getUpgradeCameoName( c ) == upgrade->getUpgradeName() )
			return TRUE;
	return FALSE;
}

void AIPlayer::buyUpgradesForArmy( Bool overTarget )
{
	const AIDifficultyProfile *profile = getSkillProfile();
	const UnsignedInt now = TheGameLogic->getFrame();
	if( INT_TO_REAL( now ) < profile->m_upgradeStartSeconds * LOGICFRAMES_PER_SECOND )
		return;

	// every player upgrade a building of ours offers and we could start: not owned, not on order, every
	// science its button asks for held
	std::vector<Object *> factories;
	m_player->iterateObjects( collectFactories, &factories );
	std::vector<const UpgradeTemplate *> upgrades;
	std::vector<Object *> where;
	for( std::vector<Object *>::const_iterator f = factories.begin(); f != factories.end(); ++f )
	{
		const CommandSet *commandSet = TheCommandStore->findCommandSet( (*f)->getCommandSetString() );
		if( commandSet == NULL )
			continue;
		const UnsignedInt queued = (*f)->getProductionUpdateInterface()->getProductionCount();
		for( Int i = 0; i < MAX_COMMANDS_PER_SET; ++i )
		{
			const CommandButton *button = commandSet->getCommandButton( i );
			if( button == NULL || button->getCommandType() != GUI_COMMAND_PLAYER_UPGRADE || button->getUpgradeTemplate() == NULL )
				continue;
			const UpgradeTemplate *upgrade = button->getUpgradeTemplate();
			if( m_player->hasUpgradeComplete( upgrade ) || m_player->hasUpgradeInProduction( upgrade ) )
				continue;
			Bool sciences = TRUE;
			for( size_t s = 0; s < button->getScienceVec().size(); ++s )
				if( !m_player->hasScience( button->getScienceVec()[ s ] ) )
					sciences = FALSE;
			if( !sciences )
				continue;
			size_t u = 0;
			while( u < upgrades.size() && upgrades[ u ] != upgrade )
				++u;
			if( u == upgrades.size() )
			{
				upgrades.push_back( upgrade );
				where.push_back( *f );
			}
			else if( queued < where[ u ]->getProductionUpdateInterface()->getProductionCount() )
				where[ u ] = *f;
		}
	}
	if( upgrades.empty() )
		return;

	// what each would improve, a fighting unit at its price times how it trades
	std::vector<AIVisibleEnemy> army;
	rememberedArmy( &army );
	std::vector<Object *> owned;
	m_player->iterateObjects( collectOwned, &owned );
	std::vector<Real> covered( upgrades.size(), 0.0f );
	std::vector<Real> likedCovered( upgrades.size(), 0.0f );
	const Bool tasted = m_persona != AIPERSONA_NORMAL;
	const AIPersonaWeights taste = tasted ? personaKindTaste() : aiPersonaWeights( AIPERSONA_NORMAL );
	std::vector<const ThingTemplate *> fitTemplate;		// the fit by template, worked out once a pass
	std::vector<Real> fitValue;
	for( std::vector<Object *>::const_iterator o = owned.begin(); o != owned.end(); ++o )
	{
		const Object *obj = *o;
		if( obj->isEffectivelyDead() || obj->testStatus( OBJECT_STATUS_UNDER_CONSTRUCTION ) )
			continue;
		const ThingTemplate *tmpl = obj->getTemplate();
		const Bool fights = !obj->isKindOf( KINDOF_STRUCTURE ) && tmpl->canPossiblyHaveAnyWeapon();
		Real fit = 1.0f;
		if( fights && !army.empty() )
		{
			size_t t = 0;
			while( t < fitTemplate.size() && fitTemplate[ t ] != tmpl )
				++t;
			if( t == fitTemplate.size() )
			{
				fitTemplate.push_back( tmpl );
				fitValue.push_back( aiCounterFit( armyMatchup( tmpl, m_player, army ), profile->m_counterCompositionWeight ) );
			}
			fit = fitValue[ t ];
		}
		Real value = aiUpgradeWeight( fights, fit, profile->m_upgradeNonCombatWeight ) * INT_TO_REAL( tmpl->calcCostToBuild( m_player ) );
		// a personality's upgrades are its kind's: what improves the units it likes counts double, the
		// ones it does not half, so its infantry, armour or air upgrades come first
		const Real liking = tasted && fights ? unitTaste( taste, tmpl ) : 0.0f;
		value *= liking > 0.0f ? PERSONA_UPGRADE_LIKED : liking < 0.0f ? PERSONA_UPGRADE_DISLIKED : 1.0f;
		for( size_t u = 0; u < upgrades.size(); ++u )
			if( obj->affectedByUpgrade( upgrades[ u ] ) || ( tasted && showsUpgrade( tmpl, upgrades[ u ] ) ) )
			{
				covered[ u ] += value;
				if( liking > 0.0f )
					likedCovered[ u ] += value;
			}
	}

	std::vector<Int> cost( upgrades.size(), 0 );
	for( size_t u = 0; u < upgrades.size(); ++u )
		cost[ u ] = upgrades[ u ]->calcCostToBuild( m_player );
	const Int pick = aiPickUpgrade( &covered[ 0 ], &cost[ 0 ], (Int)upgrades.size(), profile->m_upgradePayoff, overTarget );
	if( pick < 0 )
		return;
	const Int offset = 2.0f * likedCovered[ pick ] > covered[ pick ] ? PERSONA_KIND_UPGRADE_SCORE : 0;
	if( buyUpgrade( where[ pick ]->getProductionUpdateInterface(), upgrades[ pick ], offset ) )
		DEBUG_LOG(("AI UPGRADE frame %d player %d queues '%s' at '%s' for %d, improves %.0f of what it owns, %.1f times the price where %.1f will do, %d in the bank, bank target %d, %d enemy kinds known\n",
			now, m_player->getPlayerIndex(), upgrades[ pick ]->getUpgradeName().str(), where[ pick ]->getTemplate()->getName().str(),
			cost[ pick ], covered[ pick ], covered[ pick ] / INT_TO_REAL( cost[ pick ] ),
			overTarget ? 0.5f * profile->m_upgradePayoff : profile->m_upgradePayoff, m_player->getMoney()->countMoney(), bankTarget(),
			(Int)army.size()));
}

//----------------------------------------------------------------------------------------------------------
/** Another war factory, barracks or airfield of each kind whose every factory has stayed busy for the
	* rung's m_factoryBacklogSeconds (queueExtraFactory), on every rung and with no cap on how many stand:
	* a kind that keeps all of its factories busy is short of them.  A cap of 3, 5 and 8 production
	* buildings and a bank rule stood here, behind a gate that waited for a unit queued behind the one
	* being built, which the scripts never queue, so the computer stood one of each all match.  The
	* money is the purchase queue's, like every other building. */
//----------------------------------------------------------------------------------------------------------
void AIPlayer::addProductionForQueues( void )
{
	if( !m_player->getCanBuildBase() || !m_baseCenterSet )
		return;
	Object *dozer = NULL;
	m_player->iterateObjects( findAnyDozer, &dozer );
	if( dozer == NULL )
		return;
	for( Int k = 0; k < PRODUCTION_KINDS; ++k )
	{
		// the first of a kind, and one put back, is the plan's when its scripts ask: from here an
		// Easy computer stood its barracks and airfield in the first ten seconds, ahead of its plan
		const FactoryLoad load = readFactoryLoad( m_player, PRODUCTION_KIND[ k ] );
		if( load.finished == 0 || !queueExtraFactory( dozer, PRODUCTION_KIND[ k ] ) )
			continue;
		DEBUG_LOG(("AI ECONOMY frame %d player %d puts up another %s, %d standing, %d in the bank\n",
			TheGameLogic->getFrame(), m_player->getPlayerIndex(),
			k == 0 ? "war factory" : k == 1 ? "barracks" : "airfield", load.finished,
			m_player->getMoney()->countMoney()));
	}
}

//----------------------------------------------------------------------------------------------------------
/** A unit from each factory with nothing in its queue, while the budget (aiSurplusBudget) covers it.  It
	* joins the default team, and the next wave takes it along with the other strays.  Spending every idle
	* factory's worth on every rung lifted Medium's peak army from 67 to 99 and Easy's from 34 to 67, and
	* Hard against Medium went from 3-0 to 1-1, so each rung's share of its income sets the budget. */
//----------------------------------------------------------------------------------------------------------
void AIPlayer::trainSurplus( Int budget )
{
	if( budget <= 0 )
		return;
	std::vector<AIVisibleEnemy> army;
	rememberedArmy( &army );
	std::vector<Object *> factories;
	m_player->iterateObjects( collectFactories, &factories );
	// a personality with a taste for one kind of unit spends the surplus at the factories that make it
	// first, in list order, and none at all at a factory whose units it dislikes while one it likes stands.
	// One that spends (a unit taste, or a surplus share over the rung's) queues behind the scripts' own
	// units as well: a factory is never empty while the scripts' teams train, and the bank of an
	// Attacker that waited for an empty one ran to 43,000.
	const Bool persona = m_persona != AIPERSONA_NORMAL;
	AIPersonaWeights taste = aiPersonaWeights( AIPERSONA_NORMAL );
	Int depth = 1;
	if( persona )
	{
		taste = personaTaste();
		if( personaFavouredKind( taste ) >= 0 || taste.m_surplus > 1.0f )
			depth = PERSONA_SURPLUS_DEPTH[ m_skillLevel ];
		std::vector<Object *> liked, plain, disliked;
		for( std::vector<Object *>::const_iterator f = factories.begin(); f != factories.end(); ++f )
		{
			const ThingTemplate *unit = surplusUnit( *f, m_player, army, getSkillProfile()->m_counterCompositionWeight, &taste );
			const Real bias = unit == NULL ? 0.0f : unitTaste( taste, unit );
			(bias > 0.0f ? liked : bias < 0.0f ? disliked : plain).push_back( *f );
		}
		factories = liked;
		factories.insert( factories.end(), plain.begin(), plain.end() );
		if( liked.empty() )
			factories.insert( factories.end(), disliked.begin(), disliked.end() );
	}
	for( std::vector<Object *>::const_iterator f = factories.begin(); f != factories.end(); ++f )
	{
		ProductionUpdateInterface *pu = (*f)->getProductionUpdateInterface();
		// up to the depth in one pass: one a factory a pass trained 1,600 of the 3,600 an Attacker earned
		while( (Int)pu->getProductionCount() < depth )
		{
			const ThingTemplate *unit = surplusUnit( *f, m_player, army, getSkillProfile()->m_counterCompositionWeight, persona ? &taste : NULL );
			if( unit == NULL )
				break;
			const Int cost = unit->calcCostToBuild( m_player );
			if( cost > budget || !buyUnit( pu, unit, AIBUY_ARMY ) )
				break;
			budget -= cost;
			DEBUG_LOG(("AI ECONOMY frame %d player %d trains '%s' at '%s' out of the surplus, %d of the budget left, %d in the bank\n",
				TheGameLogic->getFrame(), m_player->getPlayerIndex(), unit->getName().str(), (*f)->getTemplate()->getName().str(),
				budget, m_player->getMoney()->countMoney()));
		}
	}
}

/** How deep a personality with a unit taste keeps its favoured factories' queues, below the bank target, by rung. */
static const Int PERSONA_FAVOURED_DEPTH[ AISKILL_COUNT ] = { 1, 2, 2 };
/** The same for the Air, Infantry and Armor AIs, whose kind is their whole army now that the scripts'
	* teams short of it are skipped (selectTeamToBuild): an aircraft costs three to six soldiers, so by
	* count its share only climbs with more of them on order, and a barracks that trained one soldier at
	* a time could not field a horde. */
static const Int KIND_FAVOURED_DEPTH[ AISKILL_COUNT ] = { 1, 3, 3 };
static const Int INFANTRY_FAVOURED_DEPTH[ AISKILL_COUNT ] = { 2, 3, 4 };
/** What a kind AI queues on top while its bank is past PERSONA_SKIP_BANK. */
static const Int RICH_FAVOURED_DEPTH = 2;

/** The cheapest armed ground unit of a kind off this factory's buttons it can make now, the first of
	* two alike: a Technical, a Humvee, a Scorpion, the quick ones a Raider sends; a Red Guard, a Rebel,
	* a Ranger, the horde an Infantry AI fills its ranks with.  Not a worker or a gatherer. */
static Bool hasPrimaryWeapon( const ThingTemplate *tmpl )
{
	// a Demo radar van carries only its suicide charge, in the tertiary slot: "can have a weapon" let it
	// be the Raider's raider, and parties of radar vans killed nothing
	const WeaponTemplateSet *set = tmpl->getWeaponTemplateSets().empty() ? NULL : tmpl->findWeaponTemplateSet( WeaponSetFlags() );
	return set && set->getNth( PRIMARY_WEAPON ) != NULL;
}

static const ThingTemplate *cheapestArmed( Object *factory, const Player *owner, KindOfType kind )
{
	const CommandSet *commandSet = TheCommandStore->findCommandSet( factory->getCommandSetString() );
	const ThingTemplate *best = NULL;
	Int bestCost = 0;
	for( Int i = 0; commandSet && i < MAX_COMMANDS_PER_SET; ++i )
	{
		const CommandButton *button = commandSet->getCommandButton( i );
		if( button == NULL || button->getCommandType() != GUI_COMMAND_UNIT_BUILD )
			continue;
		const ThingTemplate *tmpl = button->getThingTemplate();
		if( tmpl == NULL || !hasPrimaryWeapon( tmpl ) || !tmpl->isKindOf( kind ) || tmpl->isKindOf( KINDOF_AIRCRAFT ) ||
				tmpl->isKindOf( KINDOF_DOZER ) || tmpl->isKindOf( KINDOF_HARVESTER ) || tmpl->isKindOf( KINDOF_MONEY_HACKER ) ||
				TheBuildAssistant->canMakeUnit( factory, tmpl ) != CANMAKE_OK )
			continue;
		const Int cost = tmpl->calcCostToBuild( owner );
		if( best == NULL || cost < bestCost )
		{
			best = tmpl;
			bestCost = cost;
		}
	}
	return best;
}

/** A unit this factory can make now that accept takes (a GLA Air AI's isAirAnalogue, a Sneaky AI's
	* templateCanStealth), armed unless armedOnly is off, the cheapest or the dearest, the first of two
	* alike; NULL when it makes none. */
static const ThingTemplate *favouredUnitOf( Object *factory, const Player *owner, Bool (*accept)( const ThingTemplate * ), Bool dearest,
	Bool armedOnly = TRUE )
{
	const CommandSet *commandSet = TheCommandStore->findCommandSet( factory->getCommandSetString() );
	const ThingTemplate *best = NULL;
	Int bestCost = 0;
	for( Int i = 0; commandSet && i < MAX_COMMANDS_PER_SET; ++i )
	{
		const CommandButton *button = commandSet->getCommandButton( i );
		if( button == NULL || button->getCommandType() != GUI_COMMAND_UNIT_BUILD )
			continue;
		const ThingTemplate *tmpl = button->getThingTemplate();
		if( tmpl == NULL || ( armedOnly && !hasPrimaryWeapon( tmpl ) ) || !accept( tmpl ) || TheBuildAssistant->canMakeUnit( factory, tmpl ) != CANMAKE_OK )
			continue;
		const Int cost = tmpl->calcCostToBuild( owner );
		if( best == NULL || ( dearest ? cost > bestCost : cost < bestCost ) )
		{
			best = tmpl;
			bestCost = cost;
		}
	}
	return best;
}

struct TemplateCount
{
	const ThingTemplate *tmpl;
	Int count;
};

static void countOfTemplate( Object *obj, void *userData )
{
	TemplateCount *tally = (TemplateCount *)userData;
	if( !obj->isEffectivelyDead() && obj->getTemplate() == tally->tmpl )
		++tally->count;
}

//----------------------------------------------------------------------------------------------------------
/** The scripts' teams decide most of what a computer trains, and they are the same for every
	* personality, so a taste on the team score moved nothing: an Air AI built 12% aircraft and a Normal
	* one 14%.  So a personality with a unit taste trains its favoured units itself, once the opening is
	* over, at every factory of the kind it likes with room in its queue, at the full price and through the
	* purchase queue, where a liked unit outranks the scripts' disliked ones (buyUnit).  With fewer of those
	* factories standing than the rung's count, it puts up another. */
//----------------------------------------------------------------------------------------------------------
void AIPlayer::trainFavoured( void )
{
	// the opening's economy first; a Raider's raiders a minute before its first sortie
	// and an Air, Infantry or Armor AI's from its first factory of the kind: behind the drop-zone wait its
	// first jet came at six minutes, and the scripts' teams it skips are the rest of its army
	const Bool raider = m_persona == AIPERSONA_RAIDER;
	const Bool sneaky = m_persona == AIPERSONA_SNEAKY;
	const Bool ofItsKind = m_persona == AIPERSONA_AIR || m_persona == AIPERSONA_INFANTRY || m_persona == AIPERSONA_ARMOR || sneaky;
	if( raider ? TheGameLogic->getFrame() < (UnsignedInt)( ( RAID_START_SECONDS[ m_skillLevel ] - 60 ) * LOGICFRAMES_PER_SECOND )
						 : !ofItsKind && !readyToPush() )
		return;
	const AIPersonaWeights taste = personaTaste();
	const Int favoured = personaFavouredKind( taste );
	if( favoured < 0 )
		return;
	if( m_persona == AIPERSONA_INFANTRY )
		buyPersonaTransports();
	const Bool groundedAir = m_persona == AIPERSONA_AIR && favoured == 0;
	const KindOfType kind = PRODUCTION_KIND[ favoured ];
	std::vector<AIVisibleEnemy> army;
	rememberedArmy( &army );
	std::vector<Object *> factories;
	m_player->iterateObjects( collectFactories, &factories );
	Int ofKind = 0;
	for( std::vector<Object *>::const_iterator f = factories.begin(); f != factories.end(); ++f )
	{
		if( !(*f)->isKindOf( kind ) )
			continue;
		++ofKind;
		ProductionUpdateInterface *pu = (*f)->getProductionUpdateInterface();
		const Int *depth = m_persona == AIPERSONA_INFANTRY ? INFANTRY_FAVOURED_DEPTH : ofItsKind ? KIND_FAVOURED_DEPTH : PERSONA_FAVOURED_DEPTH;
		// a bank past the skip line queues deeper: the money is there and the queue was the limit
		const Int rich = ofItsKind && (Int)m_player->getMoney()->countMoney() > PERSONA_SKIP_BANK[ m_skillLevel ] ? RICH_FAVOURED_DEPTH : 0;
		if( (Int)pu->getProductionCount() >= depth[ m_skillLevel ] + rich )
			continue;
		// a GLA Air AI's rockets and anti-air, the cheap and the dear ones by turns; a Sneaky AI's whatever
		// goes invisible there, and the counter's pick once that runs out (one Jarmen Kell).  An Infantry
		// AI's soldiers are the counter's pick: every other one the cheapest, a horde of Red Guards against
		// Gattling tanks, lost twice what it killed on every side in 18 Hard matches
		const Bool turn = ( TheGameLogic->getFrame() / ECONOMY_CHECK_RATE + ofKind ) % 2 == 0;
		const ThingTemplate *unit = raider ? cheapestArmed( *f, m_player, KINDOF_VEHICLE )
															: groundedAir ? favouredUnitOf( *f, m_player, isAirAnalogue, turn )
															: sneaky ? favouredUnitOf( *f, m_player, templateCanStealth, turn )
																			: NULL;
		if( unit == NULL && !raider && !groundedAir )
			unit = surplusUnit( *f, m_player, army, getSkillProfile()->m_counterCompositionWeight, &taste );
		if( unit == NULL || unitTaste( taste, unit ) <= 0.0f )
			continue;
		// a Raider keeps two sorties' worth of its raider, and the rest of the money for the army
		if( raider )
		{
			TemplateCount owned = { unit, 0 };
			m_player->iterateObjects( countOfTemplate, &owned );
			if( owned.count >= 2 * RAID_PARTY[ m_skillLevel ] )
				continue;
		}
		if( !buyUnit( pu, unit, AIBUY_ARMY ) )
			continue;
		DEBUG_LOG(("AI PERSONA frame %d player %d trains favoured '%s' at '%s', %d in the bank\n", TheGameLogic->getFrame(),
			m_player->getPlayerIndex(), unit->getName().str(), (*f)->getTemplate()->getName().str(), m_player->getMoney()->countMoney()));
	}
	// one airfield with room in its queue still trains one unit at a time: the next goes up whatever the
	// queues, one at a time (queueExtraFactory), with one Air AI's second airfield at 10.6 minutes otherwise
	if( ofKind >= PERSONA_FAVOURED_FACTORIES[ m_skillLevel ] || !m_player->getCanBuildBase() || !m_baseCenterSet )
		return;
	Object *dozer = NULL;
	m_player->iterateObjects( findAnyDozer, &dozer );
	if( dozer && queueExtraFactory( dozer, kind ) )
		DEBUG_LOG(("AI PERSONA frame %d player %d puts up favoured factory %d of %d, %d in the bank\n", TheGameLogic->getFrame(),
			m_player->getPlayerIndex(), ofKind + 1, PERSONA_FAVOURED_FACTORIES[ m_skillLevel ], m_player->getMoney()->countMoney()));
}

/** The fist.  How often it is looked at; what the enemy army has to be worth before a unit is picked
	* against it, and how long a dropped pick waits before the next; how good an enemy kind has to be
	* against the unit to count as his answer, and how much of his army that answer has to be before the
	* unit is dropped; how good the unit has to be against his army to be picked; how big a fist goes,
	* and what the bank keeps back for the team plan. */
static const Int MASS_RATE = 5 * LOGICFRAMES_PER_SECOND;
static const Real MASS_MIN_ENEMY = 2000.0f;
static const UnsignedInt MASS_REPICK_FRAMES = 60 * LOGICFRAMES_PER_SECOND;
static const Real MASS_COUNTERED = 0.7f;
static const Real MASS_DROP_SHARE = 0.3f;
static const Real MASS_PICK_SHARE = 0.2f;
static const Real MASS_MIN_EFFICIENCY = 0.55f;
static const Real MASS_FIST_POWER = 6000.0f;
static const Real MASS_FIST_MAX_POWER = 18000.0f;
static const Int MASS_FIST_MAX_UNITS = 14;
static const UnsignedInt MASS_HOLD_FRAMES = 120 * LOGICFRAMES_PER_SECOND;
static const UnsignedInt MASS_MAX_HOLD_FRAMES = 180 * LOGICFRAMES_PER_SECOND;
static const Int MASS_RESERVE = 1500;

/** This factory has a button for that unit. */
static Bool factoryOffers( Object *factory, const ThingTemplate *tmpl )
{
	const CommandSet *commandSet = TheCommandStore->findCommandSet( factory->getCommandSetString() );
	for( Int i = 0; commandSet && i < MAX_COMMANDS_PER_SET; ++i )
	{
		const CommandButton *button = commandSet->getCommandButton( i );
		if( button && button->getCommandType() == GUI_COMMAND_UNIT_BUILD && button->getThingTemplate() == tmpl )
			return TRUE;
	}
	return FALSE;
}

/** The unit for the fist: of everything the factories can train now, the one that does best, money for
	* money, against the army this AI remembers, and that nothing in that army is already good against
	* (MASS_COUNTERED).  NULL when none does well enough. */
const ThingTemplate *AIPlayer::pickMassUnit( const std::vector<AIVisibleEnemy> &army, Real *efficiency ) const
{
	std::vector<Object *> factories;
	m_player->iterateObjects( collectFactories, &factories );
	const ThingTemplate *best = NULL;
	Real bestEfficiency = 0.0f;
	Int bestCost = 0;
	for( std::vector<Object *>::const_iterator f = factories.begin(); f != factories.end(); ++f )
	{
		const CommandSet *commandSet = TheCommandStore->findCommandSet( (*f)->getCommandSetString() );
		for( Int i = 0; commandSet && i < MAX_COMMANDS_PER_SET; ++i )
		{
			const CommandButton *button = commandSet->getCommandButton( i );
			if( button == NULL || button->getCommandType() != GUI_COMMAND_UNIT_BUILD )
				continue;
			const ThingTemplate *tmpl = button->getThingTemplate();
			if( tmpl == NULL || !tmpl->canPossiblyHaveAnyWeapon() || tmpl->isKindOf( KINDOF_DOZER ) || tmpl->isKindOf( KINDOF_HARVESTER ) ||
					tmpl->isKindOf( KINDOF_MONEY_HACKER ) || tmpl->isKindOf( KINDOF_HERO ) ||
					(tmpl->isKindOf( KINDOF_AIRCRAFT ) && !tmpl->isKindOf( KINDOF_PRODUCED_AT_HELIPAD )) ||
					TheBuildAssistant->canMakeUnit( *f, tmpl ) != CANMAKE_OK )
				continue;
			const Int cost = tmpl->calcCostToBuild( m_player );
			if( cost <= 0 )
				continue;
			Real weighed = 0.0f, scored = 0.0f, countered = 0.0f;
			for( std::vector<AIVisibleEnemy>::const_iterator k = army.begin(); k != army.end(); ++k )
			{
				const Real mineFrames = templateFramesToKill( tmpl, k->m_template );
				const Real theirFrames = templateFramesToKill( k->m_template, tmpl );
				scored += k->m_weight * aiMatchupScore( mineFrames, theirFrames, INT_TO_REAL( cost ), k->m_cost );
				weighed += k->m_weight;
				if( aiMatchupScore( theirFrames, mineFrames, k->m_cost, INT_TO_REAL( cost ) ) >= MASS_COUNTERED )
					countered += k->m_weight;
			}
			// an answer in his army is one that is a real part of it: a pair of Gattling tanks at home do not
			// make every rifle a bad idea against twenty Overlords.  Below the share that drops a pick, so a
			// pick is not dropped on the next look
			if( weighed <= 0.0f || countered >= MASS_PICK_SHARE * weighed )
				continue;
			const Real e = scored / weighed;
			if( best == NULL || e > bestEfficiency || (e == bestEfficiency && cost > bestCost) )
			{
				best = tmpl;
				bestEfficiency = e;
				bestCost = cost;
			}
		}
	}
	*efficiency = bestEfficiency;
	return bestEfficiency >= MASS_MIN_EFFICIENCY ? best : NULL;
}

Bool AIPlayer::isMassUnit( ObjectID id ) const
{
	return std::find( m_massUnits.begin(), m_massUnits.end(), id ) != m_massUnits.end();
}

//----------------------------------------------------------------------------------------------------------
/** A player who finds a unit the other side has no answer to builds nothing else and hits with all of
	* it at once.  The AI built the script's mix, every unit of it in some team's three or four.  So it
	* picks one: the unit doing best, money for money, against the enemy army it remembers, that nothing
	* in that army is already good against.  Every idle factory that can trains it while the bank holds
	* its price and a reserve (the team plan keeps the busy ones, and its mix: anti-air, repair, counters).
	* The ones that come out wait together at the staging point, out of the waves, until they are a fist:
	* a wave's worth, or three quarters of the enemy army, up to three waves or fourteen units.  Then the
	* fist goes as one wave, with whatever is parked.  When the enemy starts fielding its answer, a third
	* of his army that is good against it, the pick is dropped, what was held goes back to the waves, and a
	* new unit is picked a minute later. */
//----------------------------------------------------------------------------------------------------------
void AIPlayer::doMass( void )
{
	const UnsignedInt now = TheGameLogic->getFrame();
	if( !runsOwnAI() || !m_baseCenterSet || !getSkillProfile()->m_massUnit ||
			(now + computeUpdatePhase( m_player->getPlayerIndex(), MASS_RATE )) % MASS_RATE != 0 )
		return;
	Player *enemy = getAiEnemy();
	if( enemy == NULL )
		return;

	// the fist so far: still alive and still ours
	for( std::vector<ObjectID>::iterator it = m_massUnits.begin(); it != m_massUnits.end(); )
	{
		const Object *obj = TheGameLogic->findObjectByID( *it );
		if( obj == NULL || obj->isEffectivelyDead() || obj->getControllingPlayer() != m_player )
			it = m_massUnits.erase( it );
		else
			++it;
	}

	std::vector<AIVisibleEnemy> army;
	rememberedArmy( &army );
	Real armyPower = 0.0f;
	for( std::vector<AIVisibleEnemy>::const_iterator k = army.begin(); k != army.end(); ++k )
		armyPower += k->m_weight;

	if( m_massTemplate && armyPower > 0.0f )
	{
		const Real cost = INT_TO_REAL( m_massTemplate->calcCostToBuild( m_player ) );
		Real countered = 0.0f;
		for( std::vector<AIVisibleEnemy>::const_iterator k = army.begin(); k != army.end(); ++k )
			if( aiMatchupScore( templateFramesToKill( k->m_template, m_massTemplate ), templateFramesToKill( m_massTemplate, k->m_template ),
						k->m_cost, cost ) >= MASS_COUNTERED )
				countered += k->m_weight;
		if( countered >= MASS_DROP_SHARE * armyPower )
		{
			DEBUG_LOG(("AI MASS frame %d player %d drops '%s': %.0f of the %.0f army it remembers answers it, %d held go back to the waves\n",
				now, m_player->getPlayerIndex(), m_massTemplate->getName().str(), countered, armyPower, (Int)m_massUnits.size()));
			m_massTemplate = NULL;
			m_massPickFrame = now;
			m_massUnits.clear();
			return;
		}
	}
	if( m_massTemplate == NULL )
	{
		if( armyPower < MASS_MIN_ENEMY || (m_massPickFrame != 0 && now - m_massPickFrame < MASS_REPICK_FRAMES) )
			return;
		Real efficiency = 0.0f;
		m_massTemplate = pickMassUnit( army, &efficiency );
		m_massPickFrame = now;
		if( m_massTemplate == NULL )
		{
			DEBUG_LOG(("AI MASS frame %d player %d finds nothing to mass against the %.0f army it remembers, best %.2f\n",
				now, m_player->getPlayerIndex(), armyPower, efficiency));
			return;
		}
		DEBUG_LOG(("AI MASS frame %d player %d picks '%s', %.2f money for money against the %.0f army it remembers\n",
			now, m_player->getPlayerIndex(), m_massTemplate->getName().str(), efficiency, armyPower));
	}

	// new ones off the line join the fist
	for( DLINK_ITERATOR<Object> m = m_player->getDefaultTeam()->iterate_TeamMemberList(); !m.done(); m.advance() )
	{
		Object *obj = m.cur();
		if( obj->getTemplate() == m_massTemplate && !obj->isEffectivelyDead() && !obj->isContained() && obj->getAI() &&
				isAtHome( obj->getPosition() ) && !isMassUnit( obj->getID() ) )
		{
			if( m_massUnits.empty() )
				m_massSince = now;
			m_massUnits.push_back( obj->getID() );
		}
	}

	const Bool underAttack = isBaseUnderAttack();
	if( !underAttack )
	{
		std::vector<Object *> factories;
		m_player->iterateObjects( collectFactories, &factories );
		const Int cost = m_massTemplate->calcCostToBuild( m_player );
		for( std::vector<Object *>::const_iterator f = factories.begin(); f != factories.end(); ++f )
		{
			ProductionUpdateInterface *pu = (*f)->getProductionUpdateInterface();
			if( pu->getProductionCount() > 0 || !factoryOffers( *f, m_massTemplate ) ||
					TheBuildAssistant->canMakeUnit( *f, m_massTemplate ) != CANMAKE_OK ||
					m_player->getMoney()->countMoney() < (UnsignedInt)(cost + MASS_RESERVE) )
				continue;
			if( buyUnit( pu, m_massTemplate, AIBUY_ARMY ) )
				DEBUG_LOG(("AI MASS frame %d player %d trains '%s' at '%s', %d in the fist, %d in the bank\n", now,
					m_player->getPlayerIndex(), m_massTemplate->getName().str(), (*f)->getTemplate()->getName().str(),
					(Int)m_massUnits.size(), m_player->getMoney()->countMoney()));
		}
	}

	Real power = 0.0f;
	for( std::vector<ObjectID>::const_iterator it = m_massUnits.begin(); it != m_massUnits.end(); ++it )
		power += aiCombatPower( TheGameLogic->findObjectByID( *it ) );
	Real target = 0.75f * armyPower;
	if( target < MASS_FIST_POWER ) target = MASS_FIST_POWER;
	if( target > MASS_FIST_MAX_POWER ) target = MASS_FIST_MAX_POWER;
	const Int pathSuffix = enemy->getMpStartIndex() + 1;

	// a fist the factories cannot finish goes anyway, half a wave after two minutes and whatever it is
	// after three: held for good, the units of the pick were missing from every wave for the whole match
	const UnsignedInt held = m_massUnits.empty() ? 0 : now - m_massSince;
	const Bool ready = power >= target || (Int)m_massUnits.size() >= MASS_FIST_MAX_UNITS ||
		(held >= MASS_HOLD_FRAMES && power >= 0.5f * MASS_FIST_POWER) || held >= MASS_MAX_HOLD_FRAMES;
	if( !underAttack && !m_massUnits.empty() && ready )
	{
		AIGroup *fist = TheAI->createGroup();
		for( std::vector<ObjectID>::const_iterator it = m_massUnits.begin(); it != m_massUnits.end(); ++it )
		{
			Object *obj = TheGameLogic->findObjectByID( *it );
			if( obj && !obj->isContained() )
				fist->add( obj );
		}
		Int teams = 0;
		Real total = power;
		for( Int i = 0; i < MAX_HELD_TEAMS; ++i )
		{
			if( !m_heldUsed[ i ] )
				continue;
			m_heldUsed[ i ] = FALSE;
			Team *team = TheTeamFactory->findTeamByID( m_heldTeam[ i ] );
			if( team )
			{
				total += waitingPower( team, fist );
				++teams;
			}
		}
		total += addHomeStrays( fist );		// which leaves the fist's own to it
		m_massUnits.clear();
		if( fist->isEmpty() )
		{
			TheAI->destroyGroup( fist );
			return;
		}
		Coord3D center;
		fist->getCenter( &center );
		DEBUG_LOG(("AI MASS frame %d player %d sends the fist of '%s', %.0f power of %.0f wanted after %d s, with %d parked teams\n", now,
			m_player->getPlayerIndex(), m_massTemplate->getName().str(), power, target, held / LOGICFRAMES_PER_SECOND, teams));
		sendWave( fist, chooseApproachLabel( &center, AsciiString( "Center" ), pathSuffix ), pathSuffix, teams, total, 0, "fist" );
		return;
	}

	// the rest wait together where the waves gather, out of the way of the factories' doors
	Coord3D staging = m_baseCenter;
	Coord3D enemyPos;
	if( enemyStartGuess( enemy->getPlayerIndex(), &enemyPos ) )
	{
		Coord2D toward;
		toward.x = enemyPos.x - m_baseCenter.x;
		toward.y = enemyPos.y - m_baseCenter.y;
		if( toward.length() > 0.0f )
		{
			toward.normalize();
			staging.x += toward.x * m_baseRadius;
			staging.y += toward.y * m_baseRadius;
		}
	}
	AIGroup *waiting = NULL;
	for( std::vector<ObjectID>::const_iterator it = m_massUnits.begin(); it != m_massUnits.end(); ++it )
	{
		Object *obj = TheGameLogic->findObjectByID( *it );
		if( obj == NULL || obj->isContained() || !obj->getAI()->isIdle() ||
				sqr( obj->getPosition()->x - staging.x ) + sqr( obj->getPosition()->y - staging.y ) <= sqr( 0.5f * m_baseRadius ) )
			continue;
		if( waiting == NULL )
			waiting = TheAI->createGroup();
		waiting->add( obj );
	}
	if( waiting )
		waiting->groupTightenToPosition( &staging, FALSE, CMD_FROM_AI );
}

/** What a team has to put into a wave, and those members into the group when one is given.  Not the
	* members already out on orders.  A team is topped up at home while the rest of it fights, and the
	* wave code took the team whole: one Hard USA's 'Artillery F' had three new members idle at home, was
	* called up as six, and the three at the enemy's base turned round to walk the approach path again
	* from the middle of the map.  That side had 58,000 of army and between five and eight units at the
	* enemy's base. */
Real AIPlayer::waitingPower( Team *team, AIGroup *group ) const
{
	Real power = 0.0f;
	for( DLINK_ITERATOR<Object> iter = team->iterate_TeamMemberList(); !iter.done(); iter.advance() )
	{
		Object *obj = iter.cur();
		if( obj->isEffectivelyDead() || obj->getAI() == NULL || isOutOnOrders( obj ) || isFallingBack( obj->getID() ) )
			continue;
		power += aiCombatPower( obj );
		if( group == NULL )
			continue;
		if( !obj->isContained() )
		{
			if( obj->getGroup() != group )
				group->add( obj );
			continue;
		}
		// a rider goes with its gunship, not on its own orders, and the gunship goes with the rider's wave:
		// the wave's infantry may have boarded a Humvee or a Helix of another team (loadGunships).  An
		// Overlord's riders sit in the bunker it carries, and the Overlord is what drives
		Object *ship = obj->getContainedBy();
		if( ship->isKindOf( KINDOF_PORTABLE_STRUCTURE ) && ship->getContainedBy() )
			ship = ship->getContainedBy();
		if( ship->getAI() && !ship->isKindOf( KINDOF_STRUCTURE ) && ship->getID() != m_ferryID && ship->getGroup() != group &&
				!isOutOnOrders( ship ) )
			group->add( ship );
	}
	return power;
}

/** A script, or one it hands the team on to, that sends the team at the enemy: an attack order, a wait
	* on the flag the skirmish scripts raise to launch a wave, or a count towards the next one.  Each
	* player's copy of the scripts carries its index on every name, "_LAUNCH_ATTACK3".  China's 5th wave
	* Dragon Tanks only count: the attack script their team names is not in the file, so they guarded
	* the base for the whole match. */
static Bool scriptAttacks( const Script *script, Int depth )
{
	if( script == NULL || depth > 4 )
		return FALSE;
	for( OrCondition *orCond = script->getOrCondition(); orCond; orCond = orCond->getNextOrCondition() )
		for( Condition *cond = orCond->getFirstAndCondition(); cond; cond = cond->getNext() )
			if( cond->getConditionType() == Condition::FLAG && cond->getParameter( 0 )->getString().startsWith( "_LAUNCH_ATTACK" ) )
				return TRUE;
	for( Int branch = 0; branch < 2; ++branch )
	{
		for( ScriptAction *action = branch ? script->getFalseAction() : script->getAction(); action; action = action->getNext() )
		{
			switch( action->getActionType() )
			{
				case ScriptAction::SKIRMISH_FOLLOW_APPROACH_PATH:
				case ScriptAction::SKIRMISH_MOVE_TO_APPROACH_PATH:
					return TRUE;
			}
			for( Int i = 0; i < action->getNumParameters(); ++i )
			{
				const Parameter *param = action->getParameter( i );
				if( param->getParameterType() == Parameter::COUNTER && param->getString().startsWith( "_COUNTER_FOR_ATTACK" ) )
					return TRUE;
				// only a script the team is set to run; following every script a script names reached the
				// subroutine lists and called the garrison teams and the base expanders attackers
				const ScriptAction::ScriptActionType type = action->getActionType();
				if( (type == ScriptAction::TEAM_EXECUTE_SEQUENTIAL_SCRIPT || type == ScriptAction::TEAM_EXECUTE_SEQUENTIAL_SCRIPT_LOOPING) &&
						param->getParameterType() == Parameter::SCRIPT &&
						scriptAttacks( TheScriptEngine->findScriptByName( param->getString() ), depth + 1 ) )
					return TRUE;
			}
		}
	}
	return FALSE;
}

/** A team the skirmish scripts send at the enemy at some point, as against one they keep at home on
	* guard: the tunnel guards, the fire base crew, the palace garrison. */
Bool aiTeamAttacks( const TeamTemplateInfo *info )
{
	if( scriptAttacks( TheScriptEngine->findScriptByName( info->m_scriptOnCreate ), 0 ) ||
			scriptAttacks( TheScriptEngine->findScriptByName( info->m_scriptOnIdle ), 0 ) )
		return TRUE;
	for( Int i = 0; i < MAX_GENERIC_SCRIPTS; ++i )
		if( scriptAttacks( TheScriptEngine->findScriptByName( info->m_teamGenericScripts[ i ] ), 0 ) )
			return TRUE;
	return FALSE;
}

//----------------------------------------------------------------------------------------------------------
/** C2, second attempt.  Teams went out as they came off the line - a lone artillery piece, a single bomb
	* truck, one helicopter - and three brutal matches on Winter Wolf counted 29% of the AI's units dying
	* with two or fewer of their own combat units within 300 feet.
	*
	* The first attempt held finished teams in the ready queue, and a team in the queue still counts as an
	* instance, so production stopped behind it.  This one lets the team activate and catches its attack
	* order instead: the team drives to a staging point on the enemy's side of the base and waits there,
	* production carries on, and doWaves sends everything parked out together.
	*/
//----------------------------------------------------------------------------------------------------------
/** Every living member of a team. */
static void collectTeamUnits( Team *team, std::vector<Object *> *units )
{
	for( DLINK_ITERATOR<Object> iter = team->iterate_TeamMemberList(); !iter.done(); iter.advance() )
		if( !iter.cur()->isEffectivelyDead() )
			units->push_back( iter.cur() );
}

/** holdTeamForWave lets this team go as its script said: say so, and why. */
Bool AIPlayer::refuseHold( Team *team, const char *why, Bool logIt )
{
	if( logIt )
	{
		std::vector<Object *> units;
		collectTeamUnits( team, &units );
		logEngage( why, units );
		// a team the scripts send on its own is an attack as much as a wave is, for the summary
		if( m_personaWaves == 0 )
			m_firstWaveFrame = TheGameLogic->getFrame();
		++m_personaWaves;
	}
	return FALSE;
}

Bool AIPlayer::holdTeamForWave( Team *team, const AsciiString &approach, Int pathSuffix, Bool logRefusal )
{
	// a base being hit keeps what its scripts would send out, on every rung, until the fight at home is over
	if( team == NULL || !runsOwnAI() )
		return FALSE;
	const TeamTemplateInfo *info = team->getPrototype()->getTemplateInfo();
	if( info->m_isBaseDefense || info->m_isPerimeterDefense )
		return FALSE;
	const Bool underAttack = isBaseUnderAttack();

	Int freeSlot = -1;
	Bool anyParked = FALSE;
	for( Int i = 0; i < MAX_HELD_TEAMS; ++i )
	{
		if( !m_heldUsed[ i ] )
		{
			if( freeSlot < 0 )
				freeSlot = i;
			continue;
		}
		if( m_heldTeam[ i ] == team->getID() )
			return TRUE;		// already parked; the script asking again changes nothing
		anyParked = TRUE;
	}
	// a wave on its own waits for nobody, and a rung that does not mass sends what is worth sending
	if( !underAttack && waveGoes( waitingPower( team, NULL ), 0 ) )
		return refuseHold( team, holdsTeamsForWaves() ? "alone" : "press", logRefusal );
	if( freeSlot < 0 )
	{
		if( !readyToPush() )
			return TRUE;		// the opening keeps it at home, parked or not
		return refuseHold( team, "full", logRefusal );		// the staging point is full, so this one goes as the script said
	}

	m_heldUsed[ freeSlot ] = TRUE;
	m_heldTeam[ freeSlot ] = team->getID();
	m_heldLabel[ freeSlot ] = approach;
	m_heldSuffix[ freeSlot ] = pathSuffix;
	if( !anyParked )
		m_heldSince = TheGameLogic->getFrame();

	Coord3D staging = m_baseCenter;
	Player *enemy = getAiEnemy();
	Coord3D enemyPos;
	if( enemy && enemyStartGuess( enemy->getPlayerIndex(), &enemyPos ) )
	{
		Coord2D toward;
		toward.x = enemyPos.x - m_baseCenter.x;
		toward.y = enemyPos.y - m_baseCenter.y;
		toward.normalize();
		staging.x += toward.x * m_baseRadius;
		staging.y += toward.y * m_baseRadius;

		// the wave's road is the first parked team's, so every team of one wave gathers at one spot
		Int firstParked = freeSlot;
		for( Int i = 0; i < MAX_HELD_TEAMS; ++i )
		{
			if( m_heldUsed[ i ] && i != freeSlot )
			{
				firstParked = i;
				break;
			}
		}
		// an outmatched AI gathers at home, where the wave is also the garrison, and so does one whose base
		// is being hit
		Coord3D hold;
		if( getSkillProfile()->m_useInfluenceMapForAttackLane && m_pressure != AIPRESSURE_DEFEND && !underAttack &&
				forwardHoldPoint( m_heldLabel[ firstParked ], m_heldSuffix[ firstParked ], &enemyPos, &hold ) )
		{
			DEBUG_LOG(("AI HOLD frame %d player %d gathers at (%.0f,%.0f) on %s%d\n", TheGameLogic->getFrame(),
				m_player->getPlayerIndex(), hold.x, hold.y, m_heldLabel[ firstParked ].str(), m_heldSuffix[ firstParked ]));
			staging = hold;
		}
	}
	// the map chooses the ground: never a spot a gun it knows of already covers
	ensureInfluence();
	if( m_influence.enemyAt( staging.x, staging.y ) > 0.0f )
	{
		const Coord3D wanted = staging;
		if( !quietGround( &wanted, NULL, &staging ) )
			staging = wanted;
	}
	AIGroup *group = TheAI->createGroup();
	waitingPower( team, group );
	if( group->isEmpty() )
		TheAI->destroyGroup( group );
	else
		group->groupTightenToPosition( &staging, FALSE, CMD_FROM_AI );
	return TRUE;
}

//----------------------------------------------------------------------------------------------------------
/** A skirmish script sends a team hunting, or at an area.  Both go from wherever the team stands, on
	* their own, the moment the script fires: a team just out of the factory walks across the map alone,
	* past the wave parked at the staging point.  A team still mostly at home parks with that wave
	* instead, and goes out with it, unless it is worth sending on its own (waveGoes). */
//----------------------------------------------------------------------------------------------------------
Bool AIPlayer::gateTeamAttack( Team *team, const char *cause )
{
	if( team == NULL || !runsOwnAI() )
		return FALSE;
	const TeamTemplateInfo *info = team->getPrototype()->getTemplateInfo();
	if( info->m_isBaseDefense || info->m_isPerimeterDefense )
		return FALSE;
	std::vector<Object *> units;
	collectTeamUnits( team, &units );
	if( units.empty() )
		return FALSE;
	Int home = 0;
	for( std::vector<Object *>::const_iterator u = units.begin(); u != units.end(); ++u )
		if( isAtHome( (*u)->getPosition() ) )
			++home;
	Player *enemy = getAiEnemy();
	// every rung: holdTeamForWave lets a team worth sending go, and keeps the rest for the next wave
	if( enemy && m_baseCenterSet && 2 * home >= (Int)units.size() &&
			holdTeamForWave( team, AsciiString( "Center" ), enemy->getMpStartIndex() + 1, FALSE ) )
	{
		DEBUG_LOG(("AI ENGAGE frame %d player %d parks '%s' for the next wave instead of its script's %s\n",
			TheGameLogic->getFrame(), m_player->getPlayerIndex(), team->getName().str(), cause));
		return TRUE;
	}
	logEngage( cause, units );
	return FALSE;
}

//----------------------------------------------------------------------------------------------------------
/** One player's army as an observer knows it: everything that moves and shoots, in aiCombatPower's
	* currency.  observer -1 is the player counting its own.  An enemy carried inside something is not
	* counted, since isUnknownTo answers "known" for whatever has no place on the map. */
struct KnownArmy
{
	Int observer;
	Real power;
	Int buildings;		///< his buildings the observer knows of, and the sum of where they stand
	Real buildingsX;
	Real buildingsY;
};

static void addKnownArmy( Object *obj, void *userData )
{
	KnownArmy *army = (KnownArmy *)userData;
	if( obj->isEffectivelyDead() || obj->isKindOf( KINDOF_PROJECTILE ) )
		return;
	if( army->observer >= 0 && (obj->isContained() || !observerKnowsAbout( obj, army->observer )) )
		return;
	if( obj->isKindOf( KINDOF_STRUCTURE ) )
	{
		++army->buildings;
		army->buildingsX += obj->getPosition()->x;
		army->buildingsY += obj->getPosition()->y;
	}
	else if( !obj->isKindOf( KINDOF_MONEY_HACKER ) )
		army->power += aiCombatPower( obj );
}

/** How much of the enemy army this AI believes in is kept for each look at his base that does not find
	* it, and how much of the way back to an army like its own the belief goes for each look not taken. */
static const Real KNOWN_ARMY_KEPT = 0.9f;
static const Real UNSEEN_ARMY_GROWS = 0.02f;
/** How much of an ally's army, or an enemy ally's, this AI counts on when it rates its chance, by rung. */
static const Real ALLY_ARMY_SHARE[ AISKILL_COUNT ] = { 0.5f, 0.75f, 1.0f };

/** How often the guards at home are sent hunting once the enemy is finished.  Not every wave tick: a
	* guard whose script puts it straight back on guard would be sent again every two seconds. */
static const Int GUARD_HUNT_RATE = 30 * LOGICFRAMES_PER_SECOND;

/** As far as AIHuntState looks for a victim. */
static const Real HUNT_REACH = 9999.9f;

/** The rungs that park attack teams, and when.  Hard does until it is ahead.  Medium sends each team
	* as it forms, as it always did, except into an enemy that outmatches it; it knows when to leave a
	* losing fight, and this is the same judgement made before the walk.  Easy never waits. */
Bool AIPlayer::holdsTeamsForWaves( void ) const
{
	// a Rusher on any rung parks everything for the one attack
	if( rushPending() )
		return TRUE;
	const AIDifficultyProfile *profile = getSkillProfile();
	if( profile->m_massBeforeAttacking )
		return m_pressure <= AIPRESSURE_NORMAL;
	return m_pressure == AIPRESSURE_DEFEND && profile->m_retreatTtkRatio > 0.0f;
}

/** Whether parked teams worth power, the oldest parked heldFrames ago, leave now.  Not before the
	* opening is over (readyToPush).  A trickle under m_minAttackPower waits for company however long it
	* takes and whatever the pressure: in one 18,000-frame Twilight Flame match each, 43 of the 47
	* attacks two Hard AIs made were one to four units, and 14 of Medium's 19.  Letting it go after the
	* wave's longest wait still sent two-unit Hard waves. */
Bool AIPlayer::waveGoes( Real power, UnsignedInt heldFrames )
{
	// the Rusher's one attack goes at its frame with whatever is parked, and nothing goes before it.
	// A team asking to be held (heldFrames 0, holdTeamForWave) is always parked, so none leaves alone
	if( rushPending() )
		return heldFrames > 0 && TheGameLogic->getFrame() >= rushFrame() && power > 0.0f;
	if( !readyToPush() || power < getSkillProfile()->m_minAttackPower )
		return FALSE;
	if( !holdsTeamsForWaves() )
		return power > 0.0f;
	return aiReleaseWaveAt( m_pressure, power, wavePower(), heldFrames, waveMaxHoldFrames() );
}

Real AIPlayer::wavePower( void ) const
{
	return m_persona == AIPERSONA_NORMAL ? WAVE_POWER : WAVE_POWER * personaWeights().m_wavePower;
}

UnsignedInt AIPlayer::waveMaxHoldFrames( void ) const
{
	return m_persona == AIPERSONA_NORMAL ? WAVE_MAX_HOLD_FRAMES
																			 : (UnsignedInt)( INT_TO_REAL( WAVE_MAX_HOLD_FRAMES ) * personaWeights().m_waveHold );
}

/** Who the last push takes out of a team the scripts keep at home.  The workers, the hackers, the
	* scouts and whoever is out capturing keep their jobs, and a jet flies its own sorties. */
Bool AIPlayer::leavesToFinish( const Object *obj ) const
{
	if( aiCombatPower( obj ) <= 0.0f || !obj->isAbleToAttack() || obj->isKindOf( KINDOF_STRUCTURE ) || obj->isKindOf( KINDOF_HARVESTER ) ||
			obj->isKindOf( KINDOF_DOZER ) || obj->isKindOf( KINDOF_MONEY_HACKER ) )
		return FALSE;
	// a slave goes where its master goes: a Stinger Site's crew is in the default team, and unselectable
	if( obj->testStatus( OBJECT_STATUS_UNSELECTABLE ) )
		return FALSE;
	if( obj->isKindOf( KINDOF_AIRCRAFT ) && !isHelicopter( obj ) )
		return FALSE;
	const ObjectID id = obj->getID();
	for( Int slot = 0; slot < MAX_AI_SCOUTS; ++slot )
		if( m_scoutID[ slot ] == id )
			return FALSE;
	for( Int slot = 0; slot < MAX_PERSONA_RAIDERS; ++slot )
		if( m_raider[ slot ] == id )
			return FALSE;		// a Raider's raider goes raiding, and nowhere else
	return id != m_capturerID && id != m_ferryID && id != m_hijackerID && !isDutyHelix( obj ) && !isDutyAmbulance( obj );
}

//----------------------------------------------------------------------------------------------------------
/** The owner watched a computer with an army at home and an enemy with nothing left go on waiting
	* for a wave.  The wave was sized against a constant, whoever it was for.  So the AI now rates its
	* chance against its current enemy, its own army against what it knows of his, and the wave code
	* reads the level that chance gives (aiPressureLevel).
	*
	* What it knows of his army is what it can see, and an army at home behind the fog is not in sight.
	* So the figure is a belief.  It starts at an army the size of this AI's own and is never less than
	* what is in sight.  It comes down only while the middle of his base is in view and the army is not,
	* a tenth for each look, which is forty seconds of standing in an empty base before the guards are
	* called out.  With nobody looking it climbs back towards an army the size of this AI's own: an enemy
	* left alone is an even match again in a couple of minutes.  His base is where the buildings this AI
	* knows of stand, not his start position: the AI's army can be inside his base with the start still
	* unlocated, since only a scout pins it, and the guess meanwhile is the nearest start nobody has
	* walked to.
	*
	* Three simpler versions were played on Twilight Flame first.  Taking what was in sight at his base
	* for the whole of it had one Hard China call 4,100 against an army its owner counted at 22,700 and
	* rising, and send its guards out to finish him for nine minutes.  Remembering the most ever seen
	* had both sides four minutes in at 21,300 against 800 and 14,700 against 1,200, each pressing an
	* enemy it had seen one corner of, and the waves were never massed again.  Looking at the guessed
	* start had a USA at (4367,3119) watching an empty start at (4426,1630) beside its own base, its
	* belief falling a tenth every two seconds, while the GLA it was fighting stood at (460,1679).
	*
	* It is rated side against side: seven Hard AIs against one player with 180 tanks each weighed its own
	* army against all of his, and all seven sat at home on defend for fourteen minutes with 230 of their 455
	* units in their bases. An ally's army counts by the rung's ALLY_ARMY_SHARE, and so does each ally of his,
	* at what is believed of him, so an even four against four rates as one against one did. */
//----------------------------------------------------------------------------------------------------------
void AIPlayer::updatePressure( void )
{
	Player *enemy = getAiEnemy();
	if( enemy == NULL )
		return;
	const Int me = m_player->getPlayerIndex();

	KnownArmy mine = { -1, 0.0f, 0, 0.0f, 0.0f };
	m_player->iterateObjects( addKnownArmy, &mine );
	KnownArmy his = { me, 0.0f, 0, 0.0f, 0.0f };
	enemy->iterateObjects( addKnownArmy, &his );

	Bool baseInSight = FALSE;
	if( his.buildings > 0 )
	{
		Coord3D middle;
		middle.x = his.buildingsX / his.buildings;
		middle.y = his.buildingsY / his.buildings;
		middle.z = 0.0f;
		baseInSight = ThePartitionManager->getShroudStatusForPlayer( me, &middle ) == CELLSHROUD_CLEAR;
	}

	// a new enemy, a save from before this was kept, or an enemy none of whose buildings has been seen:
	// an even match until somebody looks
	if( enemy->getPlayerIndex() != m_pressureEnemy || his.buildings == 0 )
	{
		m_pressureEnemy = enemy->getPlayerIndex();
		m_knownEnemyPower = mine.power;
	}
	else if( baseInSight )
		m_knownEnemyPower *= KNOWN_ARMY_KEPT;
	else if( m_knownEnemyPower < mine.power )
		m_knownEnemyPower += (mine.power - m_knownEnemyPower) * UNSEEN_ARMY_GROWS;
	if( his.power > m_knownEnemyPower )
		m_knownEnemyPower = his.power;

	// what the two armies are worth to each other, unit against unit, as well as by price: a Crusader
	// army against Overlords it has seen is worth less than its money says
	Real myPower = mine.power;
	if( getSkillProfile()->m_engageGate )
	{
		std::vector<Object *> owned;
		m_player->iterateObjects( collectOwned, &owned );
		std::vector<AIVisibleEnemy> myArmy, theirArmy;
		for( std::vector<Object *>::const_iterator o = owned.begin(); o != owned.end(); ++o )
			if( !(*o)->isKindOf( KINDOF_STRUCTURE ) && !(*o)->isKindOf( KINDOF_MONEY_HACKER ) && !(*o)->isKindOf( KINDOF_PROJECTILE ) )
				addToArmy( &myArmy, *o );
		rememberedArmy( &theirArmy );
		myPower *= (Real)sqrt( armyAdvantage( myArmy, theirArmy ) );
	}
	KnownArmy allies = { -1, 0.0f, 0, 0.0f, 0.0f };
	Int enemyAllies = 0;
	for( Int i = 0; i < ThePlayerList->getPlayerCount(); ++i )
	{
		Player *other = ThePlayerList->getNthPlayer( i );
		if( other == m_player || other == enemy )
			continue;
		const Team *otherTeam = other->getDefaultTeam();
		if( m_player->getRelationship( otherTeam ) == ALLIES && other->getRelationship( enemy->getDefaultTeam() ) == ENEMIES )
			other->iterateObjects( addKnownArmy, &allies );
		else if( m_player->getRelationship( otherTeam ) == ENEMIES && enemy->getRelationship( otherTeam ) == ALLIES &&
				other->hasAnyUnits() && other->hasAnyBuildFacility() )
			++enemyAllies;
	}
	const Real chance = aiSideWinChance( myPower, allies.power, m_knownEnemyPower, enemyAllies, ALLY_ARMY_SHARE[ m_skillLevel ] );
	AIPressure level = aiPressureLevel( chance, baseInSight, m_pressure );

	// the guards stay while anybody else is still in the game to walk into the base they left
	if( level == AIPRESSURE_FINISH )
	{
		for( Int i = 0; i < ThePlayerList->getPlayerCount(); ++i )
		{
			Player *other = ThePlayerList->getNthPlayer( i );
			if( other != enemy && other != m_player && m_player->getRelationship( other->getDefaultTeam() ) == ENEMIES &&
					other->hasAnyUnits() && other->hasAnyBuildFacility() )
				level = AIPRESSURE_PRESS;
		}
	}
	if( level == m_pressure )
		return;
	static const char *const names[ AIPRESSURE_COUNT ] = { "defend", "normal", "press", "finish" };
	DEBUG_LOG(("AI PRESSURE frame %d player %d chance %.2f level %s, %.0f power (%.0f by the units) and %.0f allied against %.0f believed and %d allies\n", TheGameLogic->getFrame(),
		me, chance, names[ level ], mine.power, myPower, allies.power, m_knownEnemyPower, enemyAllies));
	m_pressure = level;
}

//----------------------------------------------------------------------------------------------------------
/** Send the parked teams once they add up to a wave or have waited long enough.  They go as one group
	* down one approach at the pace of the slowest, so what gathered together arrives together. */
//----------------------------------------------------------------------------------------------------------
void AIPlayer::doWaves( void )
{
	if( (TheGameLogic->getFrame() + computeUpdatePhase( m_player->getPlayerIndex(), WAVE_CHECK_RATE )) % WAVE_CHECK_RATE != 0 )
		return;

	updatePressure();
	defendHome();
	notePersonaProgress();
	postTeamRequests();
	if( m_persona != AIPERSONA_NORMAL )
		answerTeamRequests();
	if( m_persona == AIPERSONA_SUPPORTER )
		defendAlly();
	if( m_persona == AIPERSONA_RAIDER )
		doPersonaRaids();
	// and an Air, Infantry or Armor AI on every rung: what trainFavoured trains stands on no team, it skips
	// the scripts' teams short of its kind, and a Hard Infantry USA sent no wave the whole match
	if( runsOwnAI() && ( isKindPersona() ||
			( ( m_persona == AIPERSONA_ATTACKER || m_persona == AIPERSONA_RUSHER ) && !getSkillProfile()->m_massBeforeAttacking ) ) )
		personaSortie();
	sendIdleAttackTeams();
	sendIdleUnitsHunting();
	keepSiegeBehind();

	Real power = 0.0f;
	Int first = -1;
	Int teams = 0;
	for( Int i = 0; i < MAX_HELD_TEAMS; ++i )
	{
		if( !m_heldUsed[ i ] )
			continue;
		Team *team = TheTeamFactory->findTeamByID( m_heldTeam[ i ] );
		if( team == NULL || !team->hasAnyUnits() )
		{
			m_heldUsed[ i ] = FALSE;
			continue;
		}
		power += waitingPower( team, NULL );
		if( first < 0 )
			first = i;
		++teams;
	}
	const Bool gunshipFilling = loadGunships();
	evacuateBuildings();
	garrisonBuildings();		// after the gunships: the wave's riders first, the home guard's leftovers into buildings
	if( first < 0 )
		return;
	// no wave walks out of a base that is being hit
	if( isBaseUnderAttack() )
	{
		DEBUG_LOG(("AI WAVE frame %d player %d keeps %d teams at home, the base is under attack\n", TheGameLogic->getFrame(),
			m_player->getPlayerIndex(), teams));
		return;
	}
	const UnsignedInt heldFrames = TheGameLogic->getFrame() - m_heldSince;
	// a full staging point goes whatever it adds up to: sixteen GLA teams of one man each are 3,200, and
	// every team formed behind them would leave alone
	// and a rung that does not mass lets go the moment what is parked is worth sending
	if( teams < MAX_HELD_TEAMS ? !waveGoes( power, heldFrames ) : !readyToPush() )
		return;
	const char *cause = !holdsTeamsForWaves() ? "press" : teams >= MAX_HELD_TEAMS ? "full" : power >= wavePower() ? "wave" : "timeout";

	// a gunship still being filled holds the wave a little while its riders walk over or come out of
	// the barracks.  Without the wait 13 of 15 bunkered Helixes left with nobody aboard: the riders
	// came out after the wave had gone
	const UnsignedInt now = TheGameLogic->getFrame();
	if( gunshipFilling && (m_boardWaitFrame == 0 || now - m_boardWaitFrame < BOARD_WAIT_FRAMES) )
	{
		if( m_boardWaitFrame == 0 )
		{
			m_boardWaitFrame = now;
			DEBUG_LOG(("AI WAVE frame %d player %d holds %d teams up to %d s for gunship riders\n", now,
				m_player->getPlayerIndex(), teams, BOARD_WAIT_FRAMES / LOGICFRAMES_PER_SECOND));
		}
		return;
	}
	m_boardWaitFrame = 0;

	const AsciiString requested = m_heldLabel[ first ];
	const Int pathSuffix = m_heldSuffix[ first ];

	AIGroup *wave = TheAI->createGroup();
	for( Int i = 0; i < MAX_HELD_TEAMS; ++i )
	{
		if( !m_heldUsed[ i ] )
			continue;
		m_heldUsed[ i ] = FALSE;
		Team *team = TheTeamFactory->findTeamByID( m_heldTeam[ i ] );
		if( team )
			waitingPower( team, wave );
	}
	power += addHomeStrays( wave );
	// the Rusher's one attack takes every fighter at home, the guard teams' as well: one rush went with
	// one team of four and 8,275 of army stayed behind
	if( rushPending() )
	{
		std::vector<Object *> owned;
		m_player->iterateObjects( collectOwned, &owned );
		for( std::vector<Object *>::const_iterator o = owned.begin(); o != owned.end(); ++o )
		{
			Object *obj = *o;
			if( obj->isEffectivelyDead() || obj->getAI() == NULL || obj->isContained() || obj->isKindOf( KINDOF_IMMOBILE ) ||
					obj->getGroup() == wave || !isAtHome( obj->getPosition() ) || !leavesToFinish( obj ) || isMassUnit( obj->getID() ) )
				continue;
			const StateID state = obj->getAI()->getCurrentStateID();
			if( state != AI_IDLE && state != AI_GUARD && state != AI_GUARD_RETALIATE )
				continue;
			power += aiCombatPower( obj );
			wave->add( obj );
		}
	}

	/* The helicopters at home go too.  A player watched China build helicopters and never send them:
		 the skirmish scripts put the Comanches and the Helixes into guard teams, and across eight
		 four-player matches every one of them sat on guard for the whole game.  So an attack helicopter
		 within reach of the base, and a gunship carrying riders, leaves with the wave and flies over it
		 at the wave's pace: its guns are with the army, and its riders' rockets are over it. */
	HomeAirSearch air;
	air.home = m_baseCenter;
	air.reachSqr = sqr( 2.0f * m_baseRadius );
	air.wave = wave;
	air.ferry = m_ferryID;
	air.owner = this;
	air.ridersComing = gunshipFilling;
	m_player->iterateObjects( addHomeHelicopter, &air );
	if( wave->isEmpty() )
	{
		TheAI->destroyGroup( wave );
		return;
	}

	Coord3D center;
	wave->getCenter( &center );
	// a Sneaky wave takes the back door or the flank, whatever the scripts asked and the guns say
	const AsciiString approach = m_persona == AIPERSONA_SNEAKY ? sneakyLane( &center, requested, pathSuffix )
																														 : chooseApproachLabel( &center, requested, pathSuffix );
	if( m_personaWaves == 0 )
		m_firstWaveFrame = now;
	++m_personaWaves;
	if( m_persona != AIPERSONA_NORMAL )
	{
		if( rushPending() )
			DEBUG_LOG(("AI PERSONA frame %d player %d rushes with %d teams, %.0f power\n", now, m_player->getPlayerIndex(), teams, power));
		m_rushSent = TRUE;
		personaChat( "Wave" );
	}

	/* A wave big enough to be two sends a third of itself down another road.  A player asked for the
		 computer to come at him from more than one side; one column down the quietest road is one fight
		 he can meet with everything.  The second road is the next quietest, by the same count of the guns
		 this AI has seen, and the split is by power, so the flank is a third of the fight and not a third
		 of the heads.  Too small a wave, or a map with one road, and it all goes together as before. */
	AsciiString flank;
	if( power >= FLANK_WAVE_POWER )
		flank = secondApproachLabel( &center, approach, pathSuffix );
	if( !flank.isEmpty() )
	{
		AIGroup *flankGroup = TheAI->createGroup();
		Real flankPower = 0.0f;
		const VecObjectID &ids = wave->getAllIDs();
		std::vector<ObjectID> members( ids.begin(), ids.end() );
		// the main body keeps its last unit: an AIGroup emptied by remove() destroys itself
		for( std::vector<ObjectID>::const_iterator it = members.begin();
				 it != members.end() && flankPower < power * FLANK_SHARE && wave->getCount() > 1; ++it )
		{
			Object *obj = TheGameLogic->findObjectByID( *it );
			if( obj == NULL )
				continue;
			flankPower += aiCombatPower( obj );
			wave->remove( obj );
			flankGroup->add( obj );
		}
		sendWave( flankGroup, flank, pathSuffix, teams, flankPower, heldFrames, cause );
		power -= flankPower;
	}
	sendWave( wave, approach, pathSuffix, teams, power, heldFrames, cause );
}

//----------------------------------------------------------------------------------------------------------
/** Fill the waiting transports with infantry.  The scripts flew the helicopters empty for the whole
	* match, and drove every Humvee and Battle Bus empty as well: the USA and China scripts order
	* TEAM_LOAD_TRANSPORTS and then TEAM_GUARD_AREA in the same breath, and the guard order replaces the
	* boarding order before anyone has taken a step.  Two Hard four-player matches on Twilight Flame
	* boarded nobody at all.  Their China flew a Helix off with a wave 14 times, empty every time, though
	* it had bought the battle bunker by frame 13,500 in both: China's Hard waves carry no infantry, so
	* the parked wave had nobody to put in it.
	*
	* An infantryman boards the nearest seat that will take him, which is the transport's own answer per
	* rider: a bunkered Helix or a Humvee lets him shoot out, an Overlord carrying its bunker takes him
	* into the bunker.  One that cannot shoot out rides only in a helicopter, as cargo, and is put down
	* where the fighting starts (doTransports); a ground transport never carries him.  Who boards what:
	*
	* The parked wave's infantry boards anything waiting, a helicopter at home or out on guard, or a
	* ground transport standing at home; the transport then leaves with the wave (waitingPower).  So
	* does the infantry of the default team, the riders trained here and the defenders defendHome
	* trained, standing at home.  The infantry of an attack team that is not parked boards only its own
	* team's transports, which is what the scripts asked for: the team then goes where its scripts send
	* it, riders aboard, on every rung.  Infantry of any other team keeps its job.  Taking any infantry
	* idle or on guard at home was tried: that is the bunker team, the derrick capturer and the tech
	* capture team, whose own orders took them straight back, and 83 boarding orders in one match seated
	* nobody.
	*
	* One already on its way to a seat is no longer idle and is left alone. */
//----------------------------------------------------------------------------------------------------------
struct GunshipList
{
	Coord3D home;
	Real reachSqr;
	ObjectID ferry;		///< the capturer's helicopter, not a seat for the wave
	const AIPlayer *owner;	///< whose gunship Chinooks count as helicopters; NULL takes helicopters only
	std::vector<Object *> gunships;
};

static void findGunshipAtHome( Object *obj, void *userData )
{
	GunshipList *list = (GunshipList *)userData;
	const ContainModuleInterface *contain = obj->getContain();
	if( !(list->owner ? list->owner->isGunshipAircraft( obj ) : isHelicopter( obj )) || contain == NULL ||
			(Int)contain->getContainCount() >= contain->getContainMax() || obj->isContained() || obj->getID() == list->ferry )
		return;
	const StateID state = obj->getAI()->getCurrentStateID();
	const Bool waiting = state == AI_IDLE || state == AI_GUARD || state == AI_GUARD_RETALIATE;
	if( waiting || sqr( obj->getPosition()->x - list->home.x ) + sqr( obj->getPosition()->y - list->home.y ) <= list->reachSqr )
		list->gunships.push_back( obj );
}

/** A seat for loadGunships: a helicopter as findGunshipAtHome takes one, or a ground transport with a
	* seat free standing waiting - a Humvee, a Battle Bus, a Troop Crawler, a Listening Outpost, an
	* Overlord or an Emperor with its bunker on.  Not a building, a tunnel, a heal bay or a worker. */
static void findSeat( Object *obj, void *userData )
{
	if( ((GunshipList *)userData)->owner->isGunshipAircraft( obj ) )
	{
		findGunshipAtHome( obj, userData );
		return;
	}
	const ContainModuleInterface *contain = obj->getContain();
	if( contain == NULL || (Int)contain->getContainCount() >= contain->getContainMax() || contain->isGarrisonable() ||
			contain->isTunnelContain() || contain->isHealContain() || !obj->isKindOf( KINDOF_VEHICLE ) ||
			obj->isKindOf( KINDOF_AIRCRAFT ) || obj->isKindOf( KINDOF_STRUCTURE ) || obj->isKindOf( KINDOF_IMMOBILE ) ||
			obj->isKindOf( KINDOF_HARVESTER ) || obj->isKindOf( KINDOF_DOZER ) || !obj->isSelectable() ||
			obj->getAI() == NULL || obj->isEffectivelyDead() || obj->isContained() )
		return;
	const StateID state = obj->getAI()->getCurrentStateID();
	if( state == AI_IDLE || state == AI_GUARD || state == AI_GUARD_RETALIATE )
		((GunshipList *)userData)->gunships.push_back( obj );
}

/** Where this player's infantry already walking to a door is going. */
static void collectBoardingGoals( Object *obj, void *userData )
{
	AIUpdateInterface *ai = obj->getAI();
	if( ai && obj->isKindOf( KINDOF_INFANTRY ) && ai->getCurrentStateID() == AI_ENTER && ai->getGoalObject() )
		((std::vector<ObjectID> *)userData)->push_back( ai->getGoalObject()->getID() );
}

/** Infantry that may be put in a transport: a fighter, not a worker, a hacker, a scout, the capturer or
	* the hijacker (leavesToFinish).  Not a hero, whose worth is what he does on foot; the Black Lotus
	* has no gun and was sent to a Helix three times.  Not a Stinger Site's soldiers either,
	* who stand on the default team, cannot be selected and go back to their site: the first version
	* sent the same eight at one Battle Bus 316 times in a match. */
/** A member of something else's band: an Angry Mob's rioters, who go back to their mob wherever they
	* are sent.  The first garrison pass sent the same four rioters at one Palace 333 times in a match,
	* and the mob itself, which never goes in, 21 times. */
static Bool isSlaved( const Object *obj )
{
	for( BehaviorModule **m = obj->getBehaviorModules(); *m; ++m )
		if( (*m)->getSlavedUpdateInterface() )
			return TRUE;
	return FALSE;
}

Bool AIPlayer::isGunshipRider( const Object *obj ) const
{
	return obj->isKindOf( KINDOF_INFANTRY ) && !obj->isKindOf( KINDOF_HERO ) && obj->isSelectable() && !obj->isContained() &&
		!obj->isEffectivelyDead() && obj->getAI() && leavesToFinish( obj ) && !isMassUnit( obj->getID() ) && !isSlaved( obj ) &&
		!obj->isKindOf( KINDOF_MOB_NEXUS ) && !obj->getTemplate()->hasSpotterSight();		// a spotter is the eyes of a gun team, not a seat filler
}

/** An infantryman who rides out with the army, where loadGunships puts him. */
struct GunshipRider
{
	Object *obj;
	const char *from;		///< "wave", "default" or "team"; a "team" rider sits only with his own team
};

Bool AIPlayer::loadGunships( void )
{
	if( !runsOwnAI() || !m_baseCenterSet )
		return FALSE;
	buyGunshipChinook();
	GunshipList list;
	list.home = m_baseCenter;
	list.reachSqr = sqr( 2.0f * m_baseRadius );
	list.ferry = m_ferryID;
	list.owner = this;
	m_player->iterateObjects( findSeat, &list );
	if( list.gunships.empty() )
		return FALSE;
	std::vector<ObjectID> goals;
	m_player->iterateObjects( collectBoardingGoals, &goals );

	// seats nobody is walking to, less those handed out this pass, so ten idle riders are not all sent
	// at one free seat.  A rider takes longer than a pass to reach his door, and counting only the men
	// inside sent seven Red Guards at one Helix's five seats in fourteen seconds
	std::vector<Int> seats;
	for( std::vector<Object *>::const_iterator g = list.gunships.begin(); g != list.gunships.end(); ++g )
		seats.push_back( (*g)->getContain()->getContainMax() - (Int)(*g)->getContain()->getContainCount() -
			(Int)std::count( goals.begin(), goals.end(), (*g)->getID() ) );

	std::vector<GunshipRider> riders;
	for( Int i = 0; i < MAX_HELD_TEAMS; ++i )
	{
		if( !m_heldUsed[ i ] )
			continue;
		Team *team = TheTeamFactory->findTeamByID( m_heldTeam[ i ] );
		if( team == NULL )
			continue;
		for( DLINK_ITERATOR<Object> iter = team->iterate_TeamMemberList(); !iter.done(); iter.advance() )
		{
			Object *rider = iter.cur();
			if( isGunshipRider( rider ) && rider->getAI()->isIdle() )
			{
				GunshipRider entry = { rider, "wave" };
				riders.push_back( entry );
			}
		}
	}
	const Team *defaultTeam = m_player->getDefaultTeam();
	// while the base is hit, the men at home and the teams' own are its defenders and stay in the fight
	const Bool underAttack = isBaseUnderAttack();
	for( Player::PlayerTeamList::const_iterator t = m_player->getPlayerTeams()->begin(); !underAttack && t != m_player->getPlayerTeams()->end(); ++t )
	{
		const TeamTemplateInfo *info = (*t)->getTemplateInfo();
		Int attacks = -1;		// read off the scripts only for a prototype with a team in the field
		for( DLINK_ITERATOR<Team> iter = (*t)->iterate_TeamInstanceList(); !iter.done(); iter.advance() )
		{
			Team *team = iter.cur();
			const Bool isDefault = team == defaultTeam;
			if( !isDefault )
			{
				if( !team->isActive() )
					continue;		// still being built
				if( attacks < 0 )
					attacks = !info->m_isBaseDefense && !info->m_isPerimeterDefense && aiTeamAttacks( info );
				if( !attacks )
					continue;
			}
			Bool parked = FALSE;
			for( Int i = 0; i < MAX_HELD_TEAMS; ++i )
				if( m_heldUsed[ i ] && m_heldTeam[ i ] == team->getID() )
					parked = TRUE;
			if( parked )
				continue;		// taken above
			for( DLINK_ITERATOR<Object> m = team->iterate_TeamMemberList(); !m.done(); m.advance() )
			{
				Object *rider = m.cur();
				if( !isGunshipRider( rider ) || isFallingBack( rider->getID() ) )
					continue;
				const StateID state = rider->getAI()->getCurrentStateID();
				if( state != AI_IDLE && state != AI_GUARD && state != AI_GUARD_RETALIATE )
					continue;
				if( isDefault && !isAtHome( rider->getPosition() ) )
					continue;
				GunshipRider entry = { rider, isDefault ? "default" : "team" };
				riders.push_back( entry );
			}
		}
	}

	for( std::vector<GunshipRider>::const_iterator r = riders.begin(); r != riders.end(); ++r )
	{
		Object *rider = r->obj;
		const Bool ownTeamOnly = strcmp( r->from, "team" ) == 0;
		Int nearest = -1;
		Real nearestSqr = 0.0f;
		Bool nearestFires = FALSE;
		Int nearestRank = 0;
		for( size_t g = 0; g < list.gunships.size(); ++g )
		{
			Object *gunship = list.gunships[ g ];
			if( seats[ g ] <= 0 || !gunship->getContain()->isValidContainerFor( rider, TRUE ) )
				continue;
			const Bool helicopter = isGunshipAircraft( gunship );
			const Real distSqr = sqr( gunship->getPosition()->x - rider->getPosition()->x ) + sqr( gunship->getPosition()->y - rider->getPosition()->y );
			const Bool sameTeam = gunship->getTeam() == rider->getTeam();
			if( ownTeamOnly )
			{
				// the team's own transport, near enough to walk to: a member topped up at home does not
				// cross the map to the half of his team out guarding the combat zone
				if( !sameTeam || distSqr > list.reachSqr )
					continue;
			}
			else if( !helicopter && !sameTeam )
			{
				// another team's ground transport goes with the wave only from home, and a base guard stays
				const TeamTemplateInfo *seatInfo = gunship->getTeam()->getPrototype()->getTemplateInfo();
				if( !isAtHome( gunship->getPosition() ) || seatInfo->m_isBaseDefense || seatInfo->m_isPerimeterDefense )
					continue;
			}
			// a rider who cannot shoot out rides as cargo, in a helicopter only: doTransports puts him down
			// where the fight starts.  Seating only those who could shoot out left every unbunkered Helix
			// empty - 43 Helixes left with waves over two Hard China matches, all of them with no one aboard.
			// Only a wave's own man rides as cargo: one trained for a firing seat and sent to an unbunkered
			// Helix instead is a gunner lost, and the bunkered one he was bought for flew off empty
			const Bool fires = gunship->getContain()->isPassengerAllowedToFire( rider->getID() );
			if( !fires && (!helicopter || !hasTacticalMap() || strcmp( r->from, "wave" ) != 0) )
				continue;		// nothing would put cargo down without the tactics: gunship seats only
			// a seat he can shoot from before a nearer one he cannot, and a gunship's before a Humvee's: the
			// riders trained for a Combat Chinook walked into the Humvees parked nearer, and a Medium Air
			// Force General trained 49 Rangers for its eight seats
			const Int rank = (fires ? 2 : 0) + (helicopter ? 1 : 0);
			if( nearest < 0 || rank > nearestRank || (rank == nearestRank && distSqr < nearestSqr) )
			{
				nearest = (Int)g;
				nearestSqr = distSqr;
				nearestFires = fires;
				nearestRank = rank;
			}
		}
		if( nearest < 0 )
			continue;		// no seat this rider fits
		Object *ship = list.gunships[ nearest ];
		--seats[ nearest ];
		DEBUG_LOG(("AI GUNSHIP frame %d player %d boards '%s' onto '%s' %d, %d of %d seats taken, %s, %s\n", TheGameLogic->getFrame(),
			m_player->getPlayerIndex(), rider->getTemplate()->getName().str(), ship->getTemplate()->getName().str(), ship->getID(),
			ship->getContain()->getContainCount(), ship->getContain()->getContainMax(), nearestFires ? "fires" : "cargo", r->from));
		rider->getAI()->aiEnter( ship, CMD_FROM_AI );
		// a helicopter on guard stays in the air and nobody gets in: the same Black Lotus was sent to one
		// Helix three times over two minutes and never boarded, while the capturer boarded an idle one
		// first time.  Stood down, it lands for its riders, and the wave takes idle helicopters anyway.
		// A ground transport on guard is boarded where it stands and keeps its guard
		AIUpdateInterface *shipAI = ship->getAI();
		if( isGunshipAircraft( ship ) && shipAI->getCurrentStateID() != AI_IDLE )
			shipAI->aiIdle( CMD_FROM_AI );
	}

	// the helicopters' firing seats nobody was found for, and whether anybody is on his way to one
	Int freeSeats = 0;
	Bool walking = FALSE;
	for( size_t g = 0; g < list.gunships.size(); ++g )
	{
		ContainModuleInterface *contain = list.gunships[ g ]->getContain();
		if( !isGunshipAircraft( list.gunships[ g ] ) || !contain->asOpenContain()->OpenContain::isPassengerAllowedToFire() )
			continue;
		if( seats[ g ] > 0 )
			freeSeats += seats[ g ];
		if( seats[ g ] < contain->getContainMax() - (Int)contain->getContainCount() )
			walking = TRUE;
	}
	const Int training = buyGunshipRiders( freeSeats );
	return walking || (freeSeats > 0 && training > 0);
}

/** One of our own buildings whose men shoot out of it - a Bunker, a Fire Base, a Palace - finished, not
	* knocked out by an EMP or a hacker, and with a seat free.  Not a tunnel or a heal bay, and not a map
	* building we hold, which stays a neutral seat: counted as ours, a farmhouse taken while the base was
	* hit went on taking men and buying them in a quiet base. */
static Bool isMapBuilding( const Object *obj );
static void findFiringBuilding( Object *obj, void *userData )
{
	ContainModuleInterface *contain = obj->getContain();
	if( obj->isKindOf( KINDOF_STRUCTURE ) && contain && !contain->isTunnelContain() && !contain->isHealContain() && !isMapBuilding( obj ) &&
			(Int)contain->getContainCount() < contain->getContainMax() && contain->isPassengerAllowedToFire() &&
			!obj->isEffectivelyDead() && !obj->testStatus( OBJECT_STATUS_UNDER_CONSTRUCTION ) && !obj->testStatus( OBJECT_STATUS_SOLD ) )
		((std::vector<Object *> *)userData)->push_back( obj );
}

/** How near an enemy has to be to a neutral building for our men to go in, and how far off the last one
	* has to be before they come out again.  The gap keeps them from stepping in and out of the same house
	* while a fight drifts along its edge. */
static const Real GARRISON_ENEMY_NEAR = 350.0f;
static const Real GARRISON_ENEMY_GONE = 500.0f;

/** How far from where he stands a man out in the field goes for a building, by skill.  Easy takes none:
	* its men stay with their team, as they always did. */
static const Real GARRISON_FRONT_REACH[] = { 0.0f, 150.0f, 250.0f };
/** ... and an Infantry personality's, which fills the houses of a city map from Easy up. */
static const Real GARRISON_FRONT_REACH_INFANTRY[] = { 150.0f, 300.0f, 450.0f };

/** The health share under which our men leave a building before it comes down on them, by skill.  A
	* civilian building hurts its men when it falls (DamagePercentToUnits).  Easy stays to the end. */
static const Real GARRISON_LEAVE_HEALTH[] = { 0.0f, 0.25f, 0.4f };

/** The nearest visible enemy unit within radius of pos that is not a worker, or NULL. */
static Object *enemyWithin( Player *player, const Coord3D *pos, Real radius )
{
	PartitionFilterPlayerAffiliation filterTeam( player, (ALLOW_ALLIES|ALLOW_NEUTRAL), false );
	PartitionFilterAlive filterAlive;
	PartitionFilterRejectByObjectStatus filterStealth( MAKE_OBJECT_STATUS_MASK( OBJECT_STATUS_STEALTHED ),
		MAKE_OBJECT_STATUS_MASK2( OBJECT_STATUS_DETECTED, OBJECT_STATUS_DISGUISED ) );
	PartitionFilterRejectByKindOf filterHarvesters( MAKE_KINDOF_MASK( KINDOF_HARVESTER ), KINDOFMASK_NONE );
	PartitionFilterRejectByKindOf filterDozers( MAKE_KINDOF_MASK( KINDOF_DOZER ), KINDOFMASK_NONE );
	PartitionFilter *filters[] = { &filterTeam, &filterAlive, &filterStealth, &filterHarvesters, &filterDozers, NULL };
	MemoryPoolObjectHolder hold;
	SimpleObjectIterator *around = ThePartitionManager->iterateObjectsInRange( pos, radius, FROM_BOUNDINGSPHERE_2D, filters, ITER_SORTED_NEAR_TO_FAR );
	hold.hold( around );
	// a man or a vehicle: the first version filled a farmhouse at home for an enemy Bunker standing near it
	// from the second minute, and others for an Inferno Cannon's shell and a pool of toxin
	for( Object *obj = around->first(); obj; obj = around->next() )
		if( obj->isKindOf( KINDOF_INFANTRY ) || obj->isKindOf( KINDOF_VEHICLE ) )
			return obj;
	return NULL;
}

/** A civilian building, a farmhouse or a church, which no faction builds: ours once we are in it.  Its
	* template names no side, or the civilian one; a Bunker names its faction's. */
static Bool isMapBuilding( const Object *obj )
{
	const AsciiString &side = obj->getTemplate()->getDefaultOwningSide();
	return obj->getContain()->isGarrisonable() && (side.isEmpty() || side.compareNoCase( "Civilian" ) == 0);
}

/** The structure each of our men is inside, once each, in the order the men come. */
static void collectOccupiedBuildings( Object *obj, void *userData )
{
	Object *building = obj->getContainedBy();
	std::vector<Object *> *list = (std::vector<Object *> *)userData;
	if( obj->isKindOf( KINDOF_INFANTRY ) && building && building->isKindOf( KINDOF_STRUCTURE ) &&
			std::find( list->begin(), list->end(), building ) == list->end() )
		list->push_back( building );
}

//----------------------------------------------------------------------------------------------------------
/** The men garrisonBuildings put in a building stayed there for the rest of the match: a farmhouse taken
	* while the base was hit kept the next wave's infantry when the fight was long over, and a house at
	* the front kept its men while the front moved on.  They come out of a map building once no enemy is
	* near it, and Medium and Hard take them out of any building, our own Bunker as well, before it falls. */
//----------------------------------------------------------------------------------------------------------
void AIPlayer::evacuateBuildings( void )
{
	if( !runsOwnAI() )
		return;
	std::vector<Object *> buildings;
	m_player->iterateObjects( collectOccupiedBuildings, &buildings );
	for( std::vector<Object *>::const_iterator b = buildings.begin(); b != buildings.end(); ++b )
	{
		Object *building = *b;
		const BodyModuleInterface *body = building->getBodyModule();
		const Real health = body->getHealth() / body->getMaxHealth();
		const char *why = NULL;
		if( health < GARRISON_LEAVE_HEALTH[ m_skillLevel ] )
			why = "falling";
		else if( isMapBuilding( building ) && !enemyWithin( m_player, building->getPosition(), GARRISON_ENEMY_GONE ) )
			why = "quiet";
		if( why == NULL )
			continue;
		DEBUG_LOG(("AI GARRISON frame %d player %d empties '%s' %d, %d men, %s, %.0f%% health\n", TheGameLogic->getFrame(),
			m_player->getPlayerIndex(), building->getTemplate()->getName().str(), building->getID(),
			building->getContain()->getContainCount(), why, health * 100.0f));
		building->getContain()->orderAllPassengersToExit( CMD_FROM_AI, FALSE );
	}
}

/** Add the map's empty garrisonable buildings within radius of pos to seats, each once. */
static void addMapSeats( Player *player, const Coord3D *pos, Real radius, std::vector<Object *> *seats )
{
	PartitionFilterAlive filterAlive;
	PartitionFilterOnMap filterOnMap;
	PartitionFilterGarrisonableByPlayer filterGarrison( player, TRUE, CMD_FROM_AI );
	PartitionFilter *filters[] = { &filterAlive, &filterOnMap, &filterGarrison, 0 };
	MemoryPoolObjectHolder hold;
	SimpleObjectIterator *nearby = ThePartitionManager->iterateObjectsInRange( pos, radius, FROM_CENTER_2D, filters );
	hold.hold( nearby );
	for( Object *b = nearby->first(); b; b = nearby->next() )
		if( isMapBuilding( b ) && b->getContain()->isPassengerAllowedToFire() &&
				std::find( seats->begin(), seats->end(), b ) == seats->end() )
			seats->push_back( b );
}

//----------------------------------------------------------------------------------------------------------
/** The computer built Bunkers and Fire Bases and never put a man in one, and walked past every empty
	* farmhouse while the enemy's rebels moved in.  Our own firing buildings take the default team's
	* infantry standing at home, wherever the building is, so a Bunker at an expansion is manned from home;
	* a base defence team's men take those at home only.  Medium and up train men for the empty seats out
	* of the bank (buyGunshipRiders).  A map building takes our men only while an enemy is near it: at home
	* that is the base being hit, and in the field, for Medium and Hard, it is the front, where a team's
	* men standing idle or on guard go into the empty houses within their reach.  evacuateBuildings lets
	* them out again.  A wave's own men are never taken.  Runs after loadGunships on the same pass, so a
	* man it has sent to a gunship is no longer idle here. */
//----------------------------------------------------------------------------------------------------------
void AIPlayer::garrisonBuildings( void )
{
	if( !runsOwnAI() || !m_baseCenterSet )
		return;
	std::vector<Object *> seats;
	m_player->iterateObjects( findFiringBuilding, &seats );
	const size_t own = seats.size();
	const Real reach = 2.0f * m_baseRadius;
	const Real frontReach = m_persona == AIPERSONA_INFANTRY ? GARRISON_FRONT_REACH_INFANTRY[ m_skillLevel ] : GARRISON_FRONT_REACH[ m_skillLevel ];
	addMapSeats( m_player, &m_baseCenter, reach, &seats );

	const Team *defaultTeam = m_player->getDefaultTeam();
	std::vector<GunshipRider> riders;
	for( Player::PlayerTeamList::const_iterator t = m_player->getPlayerTeams()->begin(); t != m_player->getPlayerTeams()->end(); ++t )
	{
		const TeamTemplateInfo *info = (*t)->getTemplateInfo();
		const Bool guard = info && (info->m_isBaseDefense || info->m_isPerimeterDefense);
		for( DLINK_ITERATOR<Team> iter = (*t)->iterate_TeamInstanceList(); !iter.done(); iter.advance() )
		{
			Team *team = iter.cur();
			const Bool isDefault = team == defaultTeam;
			if( !isDefault && !team->isActive() )
				continue;
			Bool parked = FALSE;
			for( Int i = 0; i < MAX_HELD_TEAMS; ++i )
				if( m_heldUsed[ i ] && m_heldTeam[ i ] == team->getID() )
					parked = TRUE;
			if( parked )
				continue;		// a wave's
			for( DLINK_ITERATOR<Object> m = team->iterate_TeamMemberList(); !m.done(); m.advance() )
			{
				Object *rider = m.cur();
				if( !isGunshipRider( rider ) || isFallingBack( rider->getID() ) )
					continue;
				const StateID state = rider->getAI()->getCurrentStateID();
				if( state != AI_IDLE && state != AI_GUARD && state != AI_GUARD_RETALIATE )
					continue;
				const Bool home = isAtHome( rider->getPosition() );
				if( home ? !isDefault && !guard : frontReach <= 0.0f )
					continue;
				if( !home )
					addMapSeats( m_player, rider->getPosition(), frontReach, &seats );
				GunshipRider entry = { rider, !home ? "front" : isDefault ? "default" : "guard" };
				riders.push_back( entry );
			}
		}
	}
	if( seats.empty() )
		return;

	// seats nobody is walking to, as loadGunships counts them, and whether a map building has an enemy
	// near enough to be worth going in
	std::vector<ObjectID> goals;
	m_player->iterateObjects( collectBoardingGoals, &goals );
	std::vector<Int> free;
	std::vector<const Object *> threat;		// what makes a map building worth it; our own is its own reason
	for( size_t s = 0; s < seats.size(); ++s )
	{
		const BodyModuleInterface *body = seats[ s ]->getBodyModule();
		free.push_back( seats[ s ]->getContain()->getContainMax() - (Int)seats[ s ]->getContain()->getContainCount() -
			(Int)std::count( goals.begin(), goals.end(), seats[ s ]->getID() ) );
		// nobody goes into a building evacuateBuildings would take him out of again
		if( body->getHealth() < body->getMaxHealth() * GARRISON_LEAVE_HEALTH[ m_skillLevel ] )
			free.back() = 0;
		threat.push_back( s < own ? seats[ s ] : enemyWithin( m_player, seats[ s ]->getPosition(), GARRISON_ENEMY_NEAR ) );
	}

	for( std::vector<GunshipRider>::const_iterator r = riders.begin(); r != riders.end(); ++r )
	{
		Object *rider = r->obj;
		const Bool stray = strcmp( r->from, "default" ) == 0;
		const Bool front = strcmp( r->from, "front" ) == 0;
		Int nearest = -1;
		Real nearestSqr = 0.0f;
		for( size_t g = 0; g < seats.size(); ++g )
		{
			const Bool mine = g < own;
			if( free[ g ] <= 0 || threat[ g ] == NULL )
				continue;
			ContainModuleInterface *contain = seats[ g ]->getContain();
			if( !contain->isValidContainerFor( rider, TRUE ) || !contain->isPassengerAllowedToFire( rider->getID() ) )
				continue;
			const Real distSqr = sqr( seats[ g ]->getPosition()->x - rider->getPosition()->x ) + sqr( seats[ g ]->getPosition()->y - rider->getPosition()->y );
			// a man at the front only within his reach; one at home to any building at home, and a stray
			// to our own anywhere: an expansion's Bunker is manned from home
			if( front ? distSqr > sqr( frontReach ) : !(mine && stray) && !isAtHome( seats[ g ]->getPosition() ) )
				continue;
			// our own building before a neutral one, then the nearest; a tie goes to the first in list order
			const Bool nearestMine = nearest >= 0 && (size_t)nearest < own;
			if( nearest < 0 || (mine && !nearestMine) || (mine == nearestMine && distSqr < nearestSqr) )
			{
				nearest = (Int)g;
				nearestSqr = distSqr;
			}
		}
		if( nearest < 0 )
			continue;
		Object *building = seats[ nearest ];
		--free[ nearest ];
		DEBUG_LOG(("AI GARRISON frame %d player %d puts '%s' %d into '%s' %d, %d of %d seats taken, %s, %s, %s, for '%s'\n", TheGameLogic->getFrame(),
			m_player->getPlayerIndex(), rider->getTemplate()->getName().str(), rider->getID(), building->getTemplate()->getName().str(), building->getID(),
			building->getContain()->getContainCount(), building->getContain()->getContainMax(), (size_t)nearest < own ? "own" : "neutral",
			r->from, isAtHome( building->getPosition() ) ? "home" : "field", threat[ nearest ]->getTemplate()->getName().str()));
		rider->getAI()->aiEnter( building, CMD_FROM_AI );
	}

	// men for our own empty firing seats, at home and at the expansions: a trained rider comes out at home
	// and a stray walks from there to any of them.  Only out of money above the cash hoard: the first
	// version bought five Red Guards for China's first Bunker a minute in, and the skirmish script, finding
	// the bank and the trucks short a few seconds later, sold the whole base in two of eight matches.  The
	// script's own Bunker team fills the Bunker anyway, in time
	if( (Int)m_player->getMoney()->countMoney() <= getSkillProfile()->m_cashHoardThreshold )
		return;
	Int freeSeats = 0;
	for( size_t g = 0; g < own; ++g )
		if( free[ g ] > 0 )
			freeSeats += free[ g ];
	buyGunshipRiders( freeSeats );
}

/** The infantry a barracks trains to ride in a gunship: the first fighter on its buttons, so a Red
	* Guard, a Ranger, a Rebel or a Mini-Gunner.  Not a worker, a hacker or a hero.  Against an enemy
	* mostly on wheels and tracks, armour names the vehicle most of his strength is in, and the rider is whichever of them
	* kills it fastest: the Tank Hunter a bunkered Helix is bought for, a Missile Defender in a Humvee. */
static const ThingTemplate *gunshipRiderTemplate( Object *factory, const ThingTemplate *armour )
{
	const CommandSet *commandSet = TheCommandStore->findCommandSet( factory->getCommandSetString() );
	if( commandSet == NULL )
		return NULL;
	const ThingTemplate *best = NULL;
	Real bestFrames = 0.0f;
	for( Int i = 0; i < MAX_COMMANDS_PER_SET; ++i )
	{
		const CommandButton *button = commandSet->getCommandButton( i );
		if( button == NULL || button->getCommandType() != GUI_COMMAND_UNIT_BUILD )
			continue;
		const ThingTemplate *tmpl = button->getThingTemplate();
		if( tmpl == NULL || !tmpl->isKindOf( KINDOF_INFANTRY ) || !tmpl->canPossiblyHaveAnyWeapon() || tmpl->isKindOf( KINDOF_DOZER ) ||
				tmpl->isKindOf( KINDOF_MONEY_HACKER ) || tmpl->isKindOf( KINDOF_HERO ) || tmpl->hasSpotterSight() ||
				tmpl->calcCostToBuild( factory->getControllingPlayer() ) <= 0 ||
				TheBuildAssistant->canMakeUnit( factory, tmpl ) != CANMAKE_OK )
			continue;
		if( armour == NULL )
			return tmpl;
		const Real frames = templateFramesToKill( tmpl, armour );
		if( best == NULL || frames < bestFrames )
		{
			best = tmpl;
			bestFrames = frames;
		}
	}
	return best;
}

/** The most riders one order trains: a Helix's seats. */
static const Int MAX_GUNSHIP_RIDERS_ORDERED = 5;

/** Riders for the helicopters whose seats nobody fills.  China's Hard waves are tanks, and the only
	* infantry it trains guards the base, sits in bunkers or captures, so its bunkered Helixes had nobody
	* to carry at all.  Medium and up buy the men as soon as a firing seat stands empty, one order at a
	* time, while the bank holds twice what the order costs; they come out on the default team, and
	* loadGunships seats them from home.  Waiting for twice the cash hoard, as the first version did,
	* bought riders four times in four Hard matches, and 13 of 15 Helix departures carried nobody. */
Int AIPlayer::buyGunshipRiders( Int freeSeats )
{
	if( freeSeats <= 0 || m_skillLevel == AISKILL_EASY || isBaseUnderAttack() )
		return 0;

	// the vehicle most of the enemy's strength is in, when more of it is on wheels and tracks than on foot:
	// the same reading upgradeHelix buys a bunker on
	AIEnemyComposition enemy;
	std::vector<AIVisibleEnemy> army;
	computeEnemyComposition( &enemy, &army );
	const ThingTemplate *armour = NULL;
	Real armourWeight = 0.0f;
	for( size_t k = 0; k < army.size() && enemy.m_armour - enemy.m_air >= enemy.m_infantry && enemy.m_totalThreat > 0.0f; ++k )
	{
		if( army[ k ].m_template->isKindOf( KINDOF_VEHICLE ) && !army[ k ].m_template->isKindOf( KINDOF_AIRCRAFT ) &&
				army[ k ].m_weight > armourWeight )
		{
			armour = army[ k ].m_template;
			armourWeight = army[ k ].m_weight;
		}
	}

	std::vector<Object *> factories;
	m_player->iterateObjects( collectFactories, &factories );
	// the barracks at home with the shortest queue: the first order, queued at whichever came first,
	// took 2,000 frames to come out, and one trained at an expansion is never seated from home
	Object *barracks = NULL;
	const ThingTemplate *rider = NULL;
	for( size_t f = 0; f < factories.size(); ++f )
	{
		if( !isAtHome( factories[ f ]->getPosition() ) )
			continue;
		const ThingTemplate *tmpl = gunshipRiderTemplate( factories[ f ], armour );
		if( tmpl && (barracks == NULL || factories[ f ]->getProductionUpdateInterface()->getProductionCount() <
				barracks->getProductionUpdateInterface()->getProductionCount()) )
		{
			rider = tmpl;
			barracks = factories[ f ];
		}
	}
	if( rider == NULL )
		return 0;
	// what is already being trained of that kind counts against the seats, the last order or a team's
	Int training = 0;
	for( size_t f = 0; f < factories.size(); ++f )
	{
		const ProductionUpdateInterface *pu = factories[ f ]->getProductionUpdateInterface();
		for( const ProductionEntry *entry = pu->firstProduction(); entry; entry = pu->nextProduction( entry ) )
			if( entry->getProductionType() == PRODUCTION_UNIT && entry->getProductionObject() == rider )
				++training;
	}
	// and so does one already trained and standing idle wherever he is: loadGunships seats only those at
	// home, and counting the seats alone bought another for every man it could not reach
	Int idle = 0;
	for( DLINK_ITERATOR<Object> m = m_player->getDefaultTeam()->iterate_TeamMemberList(); !m.done(); m.advance() )
		if( m.cur()->getTemplate()->isEquivalentTo( rider ) && isGunshipRider( m.cur() ) && m.cur()->getAI()->isIdle() )
			++idle;
	// the floor: the bank keeps as much again as the order spends, so riders never stop a base going up
	const Int wanted = min( min( freeSeats, MAX_GUNSHIP_RIDERS_ORDERED ) - training - idle,
		(Int)m_player->getMoney()->countMoney() / (2 * rider->calcCostToBuild( m_player )) );

	ProductionUpdateInterface *pu = barracks->getProductionUpdateInterface();
	Int queued = 0;
	while( queued < wanted && buyUnit( pu, rider, AIBUY_ARMY ) )
		++queued;
	if( queued > 0 )
		DEBUG_LOG(("AI GUNSHIP frame %d player %d trains %d '%s' at '%s' for %d free seats, %d in the bank, against %s (%d infantry, %d vehicles known)\n",
			TheGameLogic->getFrame(), m_player->getPlayerIndex(), queued, rider->getName().str(), barracks->getTemplate()->getName().str(), freeSeats,
			m_player->getMoney()->countMoney(), armour ? armour->getName().str() : "infantry", enemy.m_infantryCount, enemy.m_vehicleCount));
	return training + queued;
}

/** A template's transport hold, read off its contain module before one exists; NULL for none. */
static const TransportContainModuleData *transportContainOf( const ThingTemplate *tmpl )
{
	const ModuleInfo &modules = tmpl->getBehaviorModuleInfo();
	for( Int m = 0; m < modules.getCount(); ++m )
		if( modules.getNthName( m ).compare( "TransportContain" ) == 0 )
			return (const TransportContainModuleData *)modules.getNthData( m );
	return NULL;
}

/** Whether a template's riders may shoot out of it. */
static Bool passengersFireFrom( const ThingTemplate *tmpl )
{
	const TransportContainModuleData *hold = transportContainOf( tmpl );
	return hold && hold->m_passengersAllowedToFire;
}

/** A Chinook a factory of ours can build now: a harvester that flies with a hold.  Firing, the Air Force
	* General's Combat Chinook, a gunship; not firing, the plain Chinook every USA general gathers with,
	* which the shuttle buys as a transport. */
static const ThingTemplate *chinookTemplate( Object *factory, Bool firing )
{
	const CommandSet *commandSet = TheCommandStore->findCommandSet( factory->getCommandSetString() );
	if( commandSet == NULL )
		return NULL;
	for( Int i = 0; i < MAX_COMMANDS_PER_SET; ++i )
	{
		const CommandButton *button = commandSet->getCommandButton( i );
		if( button == NULL || button->getCommandType() != GUI_COMMAND_UNIT_BUILD )
			continue;
		const ThingTemplate *tmpl = button->getThingTemplate();
		if( tmpl && tmpl->isKindOf( KINDOF_HARVESTER ) && tmpl->isKindOf( KINDOF_AIRCRAFT ) && transportContainOf( tmpl ) &&
				transportContainOf( tmpl )->m_slotCapacity > 0 && passengersFireFrom( tmpl ) == firing &&
				TheBuildAssistant->canMakeUnit( factory, tmpl ) == CANMAKE_OK )
			return tmpl;
	}
	return NULL;
}

/** The Air Force General's Combat Chinook carries eight who shoot out of it, and the computer only
	* ever bought the supply Chinook beside it: its waves were Humvees and jets.  Medium and up buy up to
	* MAX_GUNSHIP_CHINOOKS of them out of the bank above the cash hoard, as an order of their own, so
	* onUnitProduced switches off the gathering the supply center's door puts every Chinook to and keeps
	* its id.  From then on it is a helicopter to loadGunships, the wave and doTransports, and the
	* gatherer counts leave it out. */
void AIPlayer::buyGunshipChinook( void )
{
	if( m_skillLevel == AISKILL_EASY || isBaseUnderAttack() || scoutInQueue() ||
			(Int)m_player->getMoney()->countMoney() <= getSkillProfile()->m_cashHoardThreshold )
		return;
	Int owned = 0;
	for( Int chinook = 0; chinook < MAX_GUNSHIP_CHINOOKS; ++chinook )
	{
		const Object *obj = TheGameLogic->findObjectByID( m_gunshipChinook[ chinook ] );
		if( obj == NULL || obj->isEffectivelyDead() || obj->getControllingPlayer() != m_player )
			m_gunshipChinook[ chinook ] = INVALID_ID;
		else
			++owned;
	}
	if( owned >= MAX_GUNSHIP_CHINOOKS )
		return;

	std::vector<Object *> factories;
	m_player->iterateObjects( collectFactories, &factories );
	for( size_t f = 0; f < factories.size(); ++f )
	{
		const ThingTemplate *gunship = chinookTemplate( factories[ f ], TRUE );
		if( gunship == NULL )
			continue;
		queueSupportUnit( gunship, "GUNSHIP" );
		if( scoutInQueue() )
			DEBUG_LOG(("AI GUNSHIP frame %d player %d orders '%s' as a gunship, %d owned, %d in the bank\n", TheGameLogic->getFrame(),
				m_player->getPlayerIndex(), gunship->getName().str(), owned, m_player->getMoney()->countMoney()));
		return;
	}
}

//----------------------------------------------------------------------------------------------------------
void AIPlayer::sendWave( AIGroup *wave, const AsciiString &approach, Int pathSuffix, Int teams, Real power, UnsignedInt heldFrames, const char *cause )
{
	Coord3D center;
	wave->getCenter( &center );
	AsciiString pathLabel;
	pathLabel.format( "%s%d", approach.str(), pathSuffix );
	Waypoint *way = TheTerrainLogic->getClosestWaypointOnPath( &center, pathLabel );
	// the ranked units pullOutRanked has out of their teams, mending or on the way, against the whole army
	Int rankedOut = 0;
	Real rankedOutPower = 0.0f;
	Real armyPower = 0.0f;
	std::vector<Object *> owned;
	m_player->iterateObjects( collectOwned, &owned );
	for( size_t o = 0; o < owned.size(); ++o )
	{
		const Object *obj = owned[ o ];
		if( obj->isEffectivelyDead() || obj->isKindOf( KINDOF_STRUCTURE ) || obj->getAI() == NULL )
			continue;
		const Real unitPower = aiCombatPower( obj );
		armyPower += unitPower;
		if( obj->getVeterancyLevel() > LEVEL_REGULAR && !obj->getAI()->isRecruitable() && obj->getTeam() == m_player->getDefaultTeam() )
		{
			++rankedOut;
			rankedOutPower += unitPower;
		}
	}
	DEBUG_LOG(("AI WAVE frame %d player %d sends %d teams, %d units, %.0f power, after %d s, down %s; %d ranked out mending, %.0f of %.0f army power\n", TheGameLogic->getFrame(),
		m_player->getPlayerIndex(), teams, wave->getCount(), power, heldFrames / LOGICFRAMES_PER_SECOND, pathLabel.str(), rankedOut, rankedOutPower, armyPower));
	if( way == NULL )
		return;
	std::vector<Object *> units;
	const VecObjectID &ids = wave->getAllIDs();
	for( VecObjectID::const_iterator it = ids.begin(); it != ids.end(); ++it )
	{
		Object *obj = TheGameLogic->findObjectByID( *it );
		if( obj )
			units.push_back( obj );
	}
	logEngage( cause, units );

	computeShuttleFront( way, wave );
	// the team board: this attack, for an ally to join (acquireEnemy)
	Player *target = getAiEnemy();
	if( target )
	{
		Region2D bounds;
		getPlayerStructureBounds( &bounds, target->getPlayerIndex(), FALSE, m_player->getPlayerIndex() );
		Coord3D aim;
		aim.x = bounds.lo.x + bounds.width() / 2;
		aim.y = bounds.lo.y + bounds.height() / 2;
		aim.z = 0.0f;
		aiPostTeamRequest( m_player, AIREQ_ATTACK, target->getPlayerIndex(), &aim );
	}
	if( m_persona == AIPERSONA_INFANTRY )
		sendPersonaTransports( wave );
	wave->groupFollowWaypointPathAsTeam( way, CMD_FROM_AI );
	sendWaveThroughTunnels( wave, &center, way );
}

//----------------------------------------------------------------------------------------------------------
/** The owner watched the computer pile its army up at home and never use it.  The skirmish scripts send
	* an attack team only while their launch flag is up, and only a team standing inside the base when it
	* goes up; a team finished a moment later guards the base until the next launch, and one that came
	* back from a lost fight guards it for good.  Eight Brutal matches on Twilight Flame had 43 units idle
	* at home on average after frame 18000, 23 of them in attack teams, one GLA at 38 Angry Mob members with
	* 70,000 banked.  So an attack team that stands idle at home is called up here: it parks for the next
	* wave, or goes on its own when it is a wave by itself or the staging point is full.  The guards stay,
	* since the scripts never send them.  The same eight matches then had 17 idle at home, 9 in attack
	* teams, and sent 319 waves instead of 134.
	*
	* Against an enemy with next to no army left (AIPRESSURE_FINISH) every rung calls its attack teams up
	* this way, and sendIdleUnitsHunting takes the guards.
	*
	* Issue #99: it was Hard's alone, and Medium and Easy kept the pile.  A Medium China on Twilight Flame
	* stood 57 units at the lip of its plateau at minute fifteen, 109,000 in the bank and not one attack
	* made: its wave teams had come out after the launch window and its strays were in no team at all.
	* So every rung calls its idle attack teams up, the rungs that do not mass on a slower round
	* (IDLE_CALLUP_SECONDS), and the strays standing idle at home go out with a team that leaves. */
//----------------------------------------------------------------------------------------------------------
/** How often a rung that does not mass its waves calls up what stands idle at home; 0 is every wave
	* tick, Hard's. */
static const Int IDLE_CALLUP_SECONDS[ AISKILL_COUNT ] = { 60, 20, 0 };

void AIPlayer::sendIdleAttackTeams( void )
{
	if( !runsOwnAI() || !m_baseCenterSet )
		return;
	if( m_pressure != AIPRESSURE_FINISH && !getSkillProfile()->m_massBeforeAttacking )
	{
		// once a period, without a member for it: doWaves runs every WAVE_CHECK_RATE frames
		const UnsignedInt every = (UnsignedInt)( IDLE_CALLUP_SECONDS[ m_skillLevel ] * LOGICFRAMES_PER_SECOND );
		if( every != 0 && TheGameLogic->getFrame() % every >= (UnsignedInt)WAVE_CHECK_RATE )
			return;
	}
	// a team standing at home while the base is hit is its garrison: guarding, or idle beside a fight it
	// will pick up, it is not called away
	if( isBaseUnderAttack() )
		return;
	Player *enemy = getAiEnemy();
	if( enemy == NULL )
		return;
	const Int pathSuffix = enemy->getMpStartIndex() + 1;
	const AsciiString center( "Center" );
	const Real reachSqr = sqr( 2.0f * m_baseRadius );

	for( Player::PlayerTeamList::const_iterator t = m_player->getPlayerTeams()->begin(); t != m_player->getPlayerTeams()->end(); ++t )
	{
		const TeamTemplateInfo *info = (*t)->getTemplateInfo();
		if( info->m_isBaseDefense || info->m_isPerimeterDefense || !aiTeamAttacks( info ) )
			continue;
		for( DLINK_ITERATOR<Team> iter = (*t)->iterate_TeamInstanceList(); !iter.done(); iter.advance() )
		{
			Team *team = iter.cur();
			if( !team->isActive() )
				continue;		// still being built; it leaves when it is whole
			Bool parked = FALSE;
			for( Int i = 0; i < MAX_HELD_TEAMS; ++i )
				if( m_heldUsed[ i ] && m_heldTeam[ i ] == team->getID() )
					parked = TRUE;
			if( parked )
				continue;

			// most of the members that can walk and are not out on orders are at home with nothing to do.
			// Not all of them: an Angry Mob's members never stop milling round their nexus, and one
			// straggler out on the map kept 22 of them at home for a minute
			Int members = 0;
			Int waiting = 0;
			for( DLINK_ITERATOR<Object> m = team->iterate_TeamMemberList(); !m.done(); m.advance() )
			{
				Object *obj = m.cur();
				if( obj->isEffectivelyDead() || obj->getAI() == NULL || obj->isContained() || obj->isKindOf( KINDOF_IMMOBILE ) ||
						isOutOnOrders( obj ) || isFallingBack( obj->getID() ) )
					continue;
				++members;
				const StateID state = obj->getAI()->getCurrentStateID();
				const Bool idle = state == AI_IDLE || state == AI_GUARD || state == AI_GUARD_RETALIATE;
				if( idle && sqr( obj->getPosition()->x - m_baseCenter.x ) + sqr( obj->getPosition()->y - m_baseCenter.y ) <= reachSqr )
					++waiting;
			}
			if( waiting == 0 || 2 * waiting < members )
				continue;

			DEBUG_LOG(("AI WAVE frame %d player %d calls up '%s', %d units idle at home\n", TheGameLogic->getFrame(),
				m_player->getPlayerIndex(), team->getName().str(), waiting));
			if( holdTeamForWave( team, center, pathSuffix, FALSE ) )
				continue;
			AIGroup *group = TheAI->createGroup();
			// two statements: both fill the group, and the order of +'s operands is the compiler's
			Real power = waitingPower( team, group );
			power += addHomeStrays( group );
			Coord3D from;
			group->getCenter( &from );
			sendWave( group, chooseApproachLabel( &from, center, pathSuffix ), pathSuffix, 1, power, 0, "calledup" );
		}
	}
}

/** How often an Attacker on a rung that does not mass its waves sends what stands idle at home, by
	* rung; Hard masses, and its waves are the Attacker's knobs already. */
static const Int PERSONA_SORTIE_SECONDS[ AISKILL_COUNT ] = { 120, 75, 0 };
/** ... and an Air, Infantry or Armor AI's, on every rung, an Infantry AI's transports with them
	* (sendPersonaTransports). */
static const Int KIND_SORTIE_SECONDS[ AISKILL_COUNT ] = { 150, 120, 90 };
/** ... and what a sortie of theirs has to be worth, in the rung's smallest attacks: at one, a Hard
	* Infantry China went out ten times a match in handfuls and lost twice what it killed. */
static const Real KIND_SORTIE_POWER[ AISKILL_COUNT ] = { 1.0f, 1.5f, 2.0f };

Bool AIPlayer::isKindPersona( void ) const
{
	return m_persona == AIPERSONA_AIR || m_persona == AIPERSONA_INFANTRY || m_persona == AIPERSONA_ARMOR;
}

/** The ground transports an Infantry AI keeps, by rung, and the fewest seats one has to have to count. */
static const Int INFANTRY_TRANSPORTS[ AISKILL_COUNT ] = { 1, 2, 3 };
static const Int INFANTRY_TRANSPORT_MIN_SEATS = 5;

/** A ground vehicle whose hold carries infantry: a Humvee, a Battle Bus, a Troop Crawler, a Technical. */
static Bool isGroundTransport( const ThingTemplate *tmpl )
{
	const TransportContainModuleData *hold = transportContainOf( tmpl );
	return hold && hold->m_slotCapacity >= INFANTRY_TRANSPORT_MIN_SEATS && tmpl->isKindOf( KINDOF_VEHICLE ) &&
		!tmpl->isKindOf( KINDOF_AIRCRAFT ) && !tmpl->isKindOf( KINDOF_HARVESTER ) && !tmpl->isKindOf( KINDOF_DOZER );
}

static void countGroundTransports( Object *obj, void *userData )
{
	if( !obj->isEffectivelyDead() && isGroundTransport( obj->getTemplate() ) )
		++*(Int *)userData;
}

//----------------------------------------------------------------------------------------------------------
/** An Infantry AI's soldiers walked to every fight and arrived one at a time.  So it keeps a few ground
	* transports, the cheapest its war factories offer, outside the team system: they stand at home,
	* loadGunships fills them, and sendPersonaTransports drives them out with the waves. */
//----------------------------------------------------------------------------------------------------------
void AIPlayer::buyPersonaTransports( void )
{
	Int owned = 0;
	m_player->iterateObjects( countGroundTransports, &owned );
	std::vector<Object *> factories;
	m_player->iterateObjects( collectFactories, &factories );
	Object *factory = NULL;
	const ThingTemplate *best = NULL;
	for( std::vector<Object *>::const_iterator f = factories.begin(); f != factories.end(); ++f )
	{
		ProductionUpdateInterface *pu = (*f)->getProductionUpdateInterface();
		if( pu == NULL )
			continue;
		// one on order at a time, counted with the ones standing
		for( const ProductionEntry *entry = pu->firstProduction(); entry; entry = pu->nextProduction( entry ) )
			if( entry->getProductionObject() && isGroundTransport( entry->getProductionObject() ) )
				return;
		// a Battle Bus and a Troop Crawler have no gun of their own: their riders are it
		const ThingTemplate *transport = favouredUnitOf( *f, m_player, isGroundTransport, FALSE, FALSE );
		if( transport && ( best == NULL || transport->calcCostToBuild( m_player ) < best->calcCostToBuild( m_player ) ) )
		{
			best = transport;
			factory = *f;
		}
	}
	if( best == NULL || owned >= INFANTRY_TRANSPORTS[ m_skillLevel ] )
		return;
	// through the queue as a unit it likes: buyUnit would score a vehicle as one the Infantry AI does not
	ProductionUpdateInterface *pu = factory->getProductionUpdateInterface();
	const Int cost = best->calcCostToBuild( m_player );
	if( !purchaseAllowed( best->getTemplateID(), cost, AIBUY_ARMY, pu->getProductionCount() == 0, best->getName(), PERSONA_LIKED_SCORE ) ||
			!pu->queueCreateUnit( best, pu->requestUniqueUnitID() ) )
		return;
	purchased( best->getTemplateID(), cost, AIBUY_ARMY, best->getName() );
	DEBUG_LOG(("AI PERSONA frame %d player %d orders '%s' as a transport, %d of %d owned, %d in the bank\n", TheGameLogic->getFrame(),
		m_player->getPlayerIndex(), best->getName().str(), owned, INFANTRY_TRANSPORTS[ m_skillLevel ], m_player->getMoney()->countMoney()));
}

//----------------------------------------------------------------------------------------------------------
/** With each wave, an Infantry AI's loaded ground transports standing at home drive to the wave's front
	* (computeShuttleFront) and put their riders down there; doTransports sends the riders on at the enemy
	* once they are out.  One the wave took drives on ahead of it the same way: a Troop Crawler that went
	* with the walkers arrived with its eight Red Guards still inside. */
//----------------------------------------------------------------------------------------------------------
void AIPlayer::sendPersonaTransports( AIGroup *wave )
{
	std::vector<Object *> owned;
	m_player->iterateObjects( collectOwned, &owned );
	const VecObjectID ids = wave->getAllIDs();
	for( std::vector<Object *>::const_iterator o = owned.begin(); o != owned.end(); ++o )
	{
		Object *ship = *o;
		if( ship->isEffectivelyDead() || ship->isContained() || ship->getAI() == NULL || !isGroundTransport( ship->getTemplate() ) )
			continue;
		// a wave of nothing but the transport goes as it is: the group dies with its last member
		const Bool inWave = std::find( ids.begin(), ids.end(), ship->getID() ) != ids.end();
		if( inWave && wave->getCount() <= 1 )
			continue;
		const StateID state = ship->getAI()->getCurrentStateID();
		if( !inWave && ( !isAtHome( ship->getPosition() ) || ( state != AI_IDLE && state != AI_GUARD && state != AI_GUARD_RETALIATE ) ) )
			continue;
		std::vector<ObjectID> cargo;
		const ContainedItemsList *items = ship->getContain()->getContainedItemsList();
		for( ContainedItemsList::const_iterator it = items->begin(); it != items->end(); ++it )
			if( (*it)->isKindOf( KINDOF_INFANTRY ) )
				cargo.push_back( (*it)->getID() );
		if( cargo.empty() )
			continue;
		DEBUG_LOG(("AI PERSONA frame %d player %d drives '%s' %d with %d riders to the front at (%.0f,%.0f)\n", TheGameLogic->getFrame(),
			m_player->getPlayerIndex(), ship->getTemplate()->getName().str(), ship->getID(), (Int)cargo.size(), m_shuttleFront.x, m_shuttleFront.y));
		if( inWave )
			wave->remove( ship );
		ship->getAI()->aiMoveToAndEvacuate( &m_shuttleFront, CMD_FROM_SCRIPT );
		m_droppedRiders.insert( m_droppedRiders.end(), cargo.begin(), cargo.end() );
	}
}

//----------------------------------------------------------------------------------------------------------
/** Easy and Medium do not mass their attack teams, so the Attacker's and the Rusher's wave knobs moved
	* nothing there: a Medium Attacker sent one wave a match to its Normal opponent's three, and a Medium
	* Rusher's rush went at seven and a half minutes with one team of a third of its army.  So on those
	* rungs the Rusher sends every fighter at home at its rush time, as one wave, and the Attacker (and the
	* Rusher after its rush) sends what stands idle at home every PERSONA_SORTIE_SECONDS, once it is worth
	* the rung's smallest attack.  The base guard teams stay, except for the rush. */
//----------------------------------------------------------------------------------------------------------
void AIPlayer::personaSortie( void )
{
	if( !m_baseCenterSet || isBaseUnderAttack() )
		return;
	Player *enemy = getAiEnemy();
	if( enemy == NULL )
		return;
	const UnsignedInt now = TheGameLogic->getFrame();
	const Bool rush = rushPending();
	if( rush )
	{
		if( now < rushFrame() )
			return;
	}
	else
	{
		const Int *seconds = isKindPersona() ? KIND_SORTIE_SECONDS : PERSONA_SORTIE_SECONDS;
		const UnsignedInt every = (UnsignedInt)( seconds[ m_skillLevel ] * LOGICFRAMES_PER_SECOND );
		// once a period, without a member for it: doWaves runs every WAVE_CHECK_RATE frames
		if( every == 0 || now % every >= (UnsignedInt)WAVE_CHECK_RATE || !readyToPush() )
			return;
	}

	std::vector<Object *> owned;
	m_player->iterateObjects( collectOwned, &owned );
	AIGroup *group = TheAI->createGroup();
	Real power = 0.0f;
	// an Air AI's aircraft are its army: they go as the wave's air part
	const Bool flies = m_persona == AIPERSONA_AIR;
	for( std::vector<Object *>::const_iterator o = owned.begin(); o != owned.end(); ++o )
	{
		Object *obj = *o;
		if( obj->isEffectivelyDead() || obj->isKindOf( KINDOF_STRUCTURE ) || obj->isKindOf( KINDOF_IMMOBILE ) ||
				( obj->isKindOf( KINDOF_AIRCRAFT ) && !flies ) || obj->getAI() == NULL || obj->isContained() || !obj->isAbleToAttack() ||
				!isAtHome( obj->getPosition() ) || !leavesToFinish( obj ) || isOutOnOrders( obj ) || isFallingBack( obj->getID() ) )
			continue;
		const StateID state = obj->getAI()->getCurrentStateID();
		if( state != AI_IDLE && state != AI_GUARD && state != AI_GUARD_RETALIATE )
			continue;
		// the guard teams stay, except for the rush and a Medium Attacker's sorties: Medium's scripts keep
		// most of its army in them, and a sortie without them was two units
		const TeamTemplateInfo *info = obj->getTeam() ? obj->getTeam()->getPrototype()->getTemplateInfo() : NULL;
		const Bool takesGuards = rush || ( m_persona == AIPERSONA_ATTACKER && m_skillLevel >= AISKILL_MEDIUM );
		if( !takesGuards && info && ( info->m_isBaseDefense || info->m_isPerimeterDefense ) )
			continue;
		group->add( obj );
		power += aiCombatPower( obj );
		// and the men riding in it: a USA Infantry AI's Rangers sat in their Humvees, uncounted, and the
		// sortie never reached Hard's smallest attack
		const ContainModuleInterface *contain = obj->getContain();
		if( contain && !contain->isGarrisonable() )
		{
			const ContainedItemsList *items = contain->getContainedItemsList();
			for( ContainedItemsList::const_iterator it = items->begin(); it != items->end(); ++it )
				power += aiCombatPower( *it );
		}
	}
	const Real needed = getSkillProfile()->m_minAttackPower * ( isKindPersona() ? KIND_SORTIE_POWER[ m_skillLevel ] : 1.0f );
	if( group->isEmpty() || ( !rush && power < needed ) )
	{
		TheAI->destroyGroup( group );
		return;
	}

	const Int pathSuffix = enemy->getMpStartIndex() + 1;
	Coord3D from;
	group->getCenter( &from );
	const AsciiString approach = chooseApproachLabel( &from, AsciiString( SKIRMISH_CENTER ), pathSuffix );
	if( m_personaWaves == 0 )
		m_firstWaveFrame = now;
	++m_personaWaves;
	if( rush )
		DEBUG_LOG(("AI PERSONA frame %d player %d rushes with %d units, %.0f power\n", now, m_player->getPlayerIndex(), group->getCount(), power));
	m_rushSent = TRUE;
	personaChat( "Wave" );
	sendWave( group, approach, pathSuffix, 0, power, 0, rush ? "rush" : "sortie" );
}

//----------------------------------------------------------------------------------------------------------
/** The default team's fighters standing idle at home join the wave, and what they add to it.  defendHome
	* trains its emergency defenders into that team, and no script ever gives it an order: once the fight
	* at home was over they stood where it had left them, for the rest of the match.  Sixteen Hard matches
	* on Twilight Flame, looked at every thirty seconds, found 2.4 of them a side standing still for half a
	* minute or more, the largest of the idle piles; with this and the hunt in sendIdleUnitsHunting, 0.4. */
//----------------------------------------------------------------------------------------------------------
Real AIPlayer::addHomeStrays( AIGroup *wave ) const
{
	Real power = 0.0f;
	for( DLINK_ITERATOR<Object> m = m_player->getDefaultTeam()->iterate_TeamMemberList(); !m.done(); m.advance() )
	{
		Object *obj = m.cur();
		if( obj->isEffectivelyDead() || obj->getAI() == NULL || obj->isContained() || obj->isKindOf( KINDOF_IMMOBILE ) ||
				!isAtHome( obj->getPosition() ) || !leavesToFinish( obj ) || isMassUnit( obj->getID() ) )
			continue;		// the fist goes as one, when it is one (doMass)
		const StateID state = obj->getAI()->getCurrentStateID();
		if( state != AI_IDLE && state != AI_GUARD && state != AI_GUARD_RETALIATE )
			continue;
		power += aiCombatPower( obj );
		wave->add( obj );
	}
	return power;
}

//----------------------------------------------------------------------------------------------------------
/** Sixteen Hard matches on Twilight Flame had a side four to one ahead and calling its enemy finished
	* for up to 40% of the match without winning it.  One of them, a USA with 46,625 of army against a
	* GLA it counted at 6,584, had 9,250 of that standing idle at the end of its approach path, a
	* Tomahawk's walk from buildings it had orders about and none to shoot: a wave sent from here has no
	* script to carry on with when the path runs out.  So a member of an attack team standing idle away
	* from home is sent hunting, which is what the scripts do with a team of their own at that point.
	* Not a parked team, which is idle at its hold point on purpose, and not while this AI is
	* outmatched.  On every wave tick: a hunt is dropped by the next kiting step or retreat, and looked
	* at every thirty seconds a quarter of the units at the enemy's base were idle at each look.
	*
	* Against an enemy with next to no army left (AIPRESSURE_FINISH) the guards go as well: the base
	* defence teams, the teams whose scripts never attack and the strays in the default team have
	* nothing left to guard against.  Only a unit the hunt has a victim for.  An ECM tank with nothing
	* but buildings left to find went back on guard and was sent out again every thirty seconds. */
//----------------------------------------------------------------------------------------------------------
void AIPlayer::sendIdleUnitsHunting( void )
{
	if( !runsOwnAI() || !m_baseCenterSet || m_pressure == AIPRESSURE_DEFEND || !readyToPush() )
		return;
	const Bool finish = m_pressure == AIPRESSURE_FINISH;
	const Bool guardsGo = finish &&
		(TheGameLogic->getFrame() + computeUpdatePhase( m_player->getPlayerIndex(), WAVE_CHECK_RATE )) % GUARD_HUNT_RATE == 0;
	const Real reachSqr = sqr( 2.0f * m_baseRadius );
	Int hunters = 0;
	Int guards = 0;
	std::vector<Object *> hunting;

	for( Player::PlayerTeamList::const_iterator t = m_player->getPlayerTeams()->begin(); t != m_player->getPlayerTeams()->end(); ++t )
	{
		const TeamTemplateInfo *info = (*t)->getTemplateInfo();
		// the default team's fighters away from home hunt too: no script ever sends them anywhere.  At home
		// they leave with the next wave (addHomeStrays), or with the guards when the guards go
		const Bool attacks = ((*t) == m_player->getDefaultTeam()->getPrototype() && !guardsGo) ||
			(!info->m_isBaseDefense && !info->m_isPerimeterDefense && aiTeamAttacks( info ));
		if( !attacks && !guardsGo )
			continue;
		for( DLINK_ITERATOR<Team> iter = (*t)->iterate_TeamInstanceList(); !iter.done(); iter.advance() )
		{
			Team *team = iter.cur();
			if( !team->isActive() )
				continue;		// still being built
			Bool parked = FALSE;
			for( Int i = 0; i < MAX_HELD_TEAMS; ++i )
				if( m_heldUsed[ i ] && m_heldTeam[ i ] == team->getID() )
					parked = TRUE;
			if( parked )
				continue;

			for( DLINK_ITERATOR<Object> m = team->iterate_TeamMemberList(); !m.done(); m.advance() )
			{
				Object *obj = m.cur();
				// one holding after a lost fight is idle on purpose; doRetreats sends it back when it is safe
				if( obj->isEffectivelyDead() || obj->getAI() == NULL || obj->isContained() || obj->isKindOf( KINDOF_IMMOBILE ) ||
						!leavesToFinish( obj ) || isFallingBack( obj->getID() ) )
					continue;
				const StateID state = obj->getAI()->getCurrentStateID();
				const Bool atHome = sqr( obj->getPosition()->x - m_baseCenter.x ) + sqr( obj->getPosition()->y - m_baseCenter.y ) <= reachSqr;
				// an attack team's members at home are sendIdleAttackTeams' to call up together
				if( attacks ? (state != AI_IDLE || atHome) : (state != AI_IDLE && state != AI_GUARD && state != AI_GUARD_RETALIATE) )
					continue;
				if( TheAI->findClosestEnemy( obj, HUNT_REACH, AI::CAN_ATTACK, obj->getAI()->getAttackInfo() ) == NULL )
					continue;
				obj->getAI()->aiHunt( CMD_FROM_AI );
				++hunters;
				hunting.push_back( obj );
				if( !attacks )
					++guards;
			}
		}
	}
	if( hunters > 0 )
	{
		DEBUG_LOG(("AI WAVE frame %d player %d sends %d idle units hunting, %d of them off guard\n", TheGameLogic->getFrame(),
			m_player->getPlayerIndex(), hunters, guards));
		logEngage( "idlehunt", hunting );
	}
}

//----------------------------------------------------------------------------------------------------------
/** A GLA wave whose tunnels come up nearer the end of its approach path than the path itself goes
	* takes them, the way a player's move order does (TunnelTracker::findTunnelShortcut), and comes up
	* fighting.  The path is ordered first and the tunnel replaces it for whoever can go in, so a
	* member that cannot - an aircraft, anything the tunnel refuses - still has the path. */
//----------------------------------------------------------------------------------------------------------
static const Int WAVE_PATH_MAX_POINTS = 256;	///< a path that links back on itself stops being walked here

void AIPlayer::sendWaveThroughTunnels( AIGroup *wave, const Coord3D *center, Waypoint *way )
{
	Real walk = (Real)sqrt( sqr( way->getLocation()->x - center->x ) + sqr( way->getLocation()->y - center->y ) );
	Waypoint *end = way;
	for( Int i = 0; i < WAVE_PATH_MAX_POINTS && end->getNumLinks() > 0; ++i )
	{
		Waypoint *next = end->getLink( 0 );
		walk += (Real)sqrt( sqr( next->getLocation()->x - end->getLocation()->x ) + sqr( next->getLocation()->y - end->getLocation()->y ) );
		end = next;
	}

	// a copy: a member that goes into the tunnel leaves the group
	const VecObjectID members = wave->getAllIDs();
	const Object *walker = members.empty() ? NULL : TheGameLogic->findObjectByID( members.front() );
	Object *entrance = walker == NULL || walker->getAI() == NULL ? NULL
		: TunnelTracker::findTunnelShortcut( walker, center, end->getLocation(), walk );
	if( entrance == NULL )
		return;

	Int sent = 0;
	const Int waveSize = wave->getCount();
	for( VecObjectID::const_iterator it = members.begin(); it != members.end(); ++it )
	{
		Object *obj = TheGameLogic->findObjectByID( *it );
		if( obj && obj->getAI() && obj->getAI()->takeTunnelTrip( entrance, end->getLocation(), TUNNEL_TRIP_ATTACK_MOVE, CMD_FROM_AI ) )
			++sent;
	}
	DEBUG_LOG(("AI WAVE frame %d player %d sends %d of %d units through tunnel %d, the path is %.0f long\n", TheGameLogic->getFrame(),
		m_player->getPlayerIndex(), sent, waveSize, entrance->getID(), walk));
}

//----------------------------------------------------------------------------------------------------------
/** The transport Chinooks.  A USA wave walks the whole road to the enemy, tanks at the pace of the
	* slowest, while the Chinook every USA general builds at the supply center carries eight slots, two
	* tanks and two men, at 150 a second.  Medium and up buy plain Chinooks for nothing but that: they
	* load the wave's ground units at home, put them down on the wave's road short of anything the AI
	* has seen shoot, fly home and load again while the wave is under way.  They never gather, and the
	* supply Chinooks are never taken for it. */
//----------------------------------------------------------------------------------------------------------
static const Int SHUTTLE_CHECK_RATE = LOGICFRAMES_PER_SECOND;

/** How long after a wave leaves its stragglers and the units that come out behind it are flown after it. */
static const UnsignedInt SHUTTLE_WINDOW_FRAMES = 120 * LOGICFRAMES_PER_SECOND;

/** The longest a Chinook waits at home for the units it called aboard, and the shortest. */
static const UnsignedInt SHUTTLE_LOAD_FRAMES = 30 * LOGICFRAMES_PER_SECOND;
static const UnsignedInt SHUTTLE_LOAD_MIN_FRAMES = 5 * LOGICFRAMES_PER_SECOND;

/** A leg that takes longer than this has stuck somewhere, and its order is given again. */
static const UnsignedInt SHUTTLE_LEG_FRAMES = 90 * LOGICFRAMES_PER_SECOND;

/** How far along the wave's road its units are put down when nothing the AI has seen shoots there:
	* past the middle, short of the end, which is the enemy's base. */
static const Real SHUTTLE_DROP_SHARE = 0.6f;

/** A Chinook hit below this share of its health on the way out puts its load down where it is. */
static const Real SHUTTLE_ABORT_HEALTH = 0.5f;

/** Flying is worth it only beyond a walk this much longer than the base is wide. */
static const Real SHUTTLE_MIN_FLIGHT = 600.0f;

/** ChinookLocomotor's Speed, and what a round trip costs besides the flying: lifting off, landing,
	* the boarding and the unloading. */
static const Real CHINOOK_CRUISE_SPEED = 150.0f;
static const UnsignedInt SHUTTLE_TURNAROUND_FRAMES = 40 * LOGICFRAMES_PER_SECOND;

/** A straight flight from a point to (x, y) crosses no cell the influence map says the enemy's known
	* guns reach.  Below Hard every flight is quiet: their map is drawn when a unit comes under fire. */
Bool AIPlayer::flightIsQuiet( const Coord3D *from, Real x, Real y ) const
{
	if( !hasTacticalMap() )
		return TRUE;
	const Real dx = x - from->x;
	const Real dy = y - from->y;
	const Real flight = (Real)sqrt( dx * dx + dy * dy );
	for( Real along = 0.0f; along <= flight; along += INFLUENCE_CELL_SIZE )
		if( m_influence.enemyAt( from->x + dx * along / flight, from->y + dy * along / flight ) > 0.0f )
			return FALSE;
	return TRUE;
}

/** The drop point on the wave's road: the last point that is no further than SHUTTLE_DROP_SHARE of the
	* road and comes before the first one the influence map says the enemy's guns reach.  Without the
	* influence map (below Hard) only the share counts.  And the transport slots the wave's ground units
	* take, which is what the Chinooks are bought for. */
void AIPlayer::computeShuttleFront( Waypoint *way, AIGroup *wave )
{
	// the road from where the wave stands, and on from its end to the enemy's base: the approach paths
	// stop in the middle of the map, and a wave gathered a third of the way out had 269 of road left
	std::vector<Coord3D> road;
	road.push_back( *way->getLocation() );
	Waypoint *at = way;
	for( Int i = 0; i < WAVE_PATH_MAX_POINTS && at->getNumLinks() > 0; ++i )
	{
		at = at->getLink( 0 );
		road.push_back( *at->getLocation() );
	}
	Coord3D enemyPos;
	Player *enemy = getAiEnemy();
	if( enemy && enemyStartGuess( enemy->getPlayerIndex(), &enemyPos ) )
		road.push_back( enemyPos );
	Real length = 0.0f;
	for( size_t p = 1; p < road.size(); ++p )
		length += (Real)sqrt( sqr( road[ p ].x - road[ p - 1 ].x ) + sqr( road[ p ].y - road[ p - 1 ].y ) );

	// walked a cell at a time, up to the share or the first cell the enemy's known guns reach.  The drop
	// is the furthest of those the Chinook can fly to from home in a straight line over nothing known to
	// shoot: the first version measured the road alone, and seven USA Chinooks in one match were shot
	// down, each flying straight at a drop beyond an enemy base
	Coord3D drop = road[ 0 ];
	Real walked = 0.0f;
	Bool hot = FALSE;
	for( size_t p = 1; p < road.size() && !hot; ++p )
	{
		const Real legX = road[ p ].x - road[ p - 1 ].x;
		const Real legY = road[ p ].y - road[ p - 1 ].y;
		const Real leg = (Real)sqrt( legX * legX + legY * legY );
		for( Real along = INFLUENCE_CELL_SIZE; along <= leg && !hot; along += INFLUENCE_CELL_SIZE )
		{
			const Real x = road[ p - 1 ].x + legX * along / leg;
			const Real y = road[ p - 1 ].y + legY * along / leg;
			hot = walked + along > length * SHUTTLE_DROP_SHARE || (hasTacticalMap() && m_influence.enemyAt( x, y ) > 0.0f);
			if( !hot && flightIsQuiet( &m_baseCenter, x, y ) )
			{
				drop.x = x;
				drop.y = y;
			}
		}
		walked += leg;
	}
	walked = (Real)sqrt( sqr( drop.x - road[ 0 ].x ) + sqr( drop.y - road[ 0 ].y ) );
	drop.z = TheTerrainLogic->getGroundHeight( drop.x, drop.y );

	Int slots = 0;
	const VecObjectID &ids = wave->getAllIDs();
	for( VecObjectID::const_iterator it = ids.begin(); it != ids.end(); ++it )
	{
		const Object *obj = TheGameLogic->findObjectByID( *it );
		if( obj && !obj->isKindOf( KINDOF_AIRCRAFT ) && !obj->isKindOf( KINDOF_HUGE_VEHICLE ) &&
				(obj->isKindOf( KINDOF_INFANTRY ) || obj->isKindOf( KINDOF_VEHICLE )) )
			slots += obj->getTransportSlotCount();
	}
	// a wave split in two goes out in the same frame, flank first: both halves count
	const UnsignedInt now = TheGameLogic->getFrame();
	m_lastWaveSlots = (m_shuttleFrontFrame == now) ? m_lastWaveSlots + slots : slots;
	m_shuttleFront = drop;
	m_shuttleFrontFrame = now;
	DEBUG_LOG(("AI SHUTTLE frame %d player %d front at (%.0f,%.0f), %.0f from the wave, its road to the enemy %.0f long, the wave takes %d slots\n", now,
		m_player->getPlayerIndex(), drop.x, drop.y, walked, length, m_lastWaveSlots));
}

/** Who is on the way to what, vehicles as well as infantry.  Matched against a Chinook's id it is who is
	* still coming aboard: by the goal alone, since a tank sent at a hovering Chinook is not in the enter
	* state the whole time, and counting only that sent the Chinooks off with half their load or none. */
struct EnterWalker
{
	Object *walker;
	ObjectID goal;
};

static void collectEnterWalkers( Object *obj, void *userData )
{
	AIUpdateInterface *ai = obj->getAI();
	if( ai && !obj->isContained() && !obj->isKindOf( KINDOF_AIRCRAFT ) && ai->getGoalObject() )
	{
		EnterWalker walker = { obj, ai->getGoalObject()->getID() };
		((std::vector<EnterWalker> *)userData)->push_back( walker );
	}
}

/** A ground unit of a wave or an attack team that is still at home and not in a fight: on the road
	* out, gathering, or standing about. */
static Bool shuttleState( StateID state )
{
	switch( state )
	{
		case AI_IDLE: case AI_GUARD: case AI_GUARD_RETALIATE: case AI_MOVE_TO: case AI_MOVE_AND_TIGHTEN: case AI_ATTACK_MOVE_TO:
		case AI_FOLLOW_WAYPOINT_PATH_AS_TEAM: case AI_FOLLOW_WAYPOINT_PATH_AS_INDIVIDUALS: case AI_FOLLOW_WAYPOINT_PATH_AS_TEAM_EXACT:
		case AI_FOLLOW_WAYPOINT_PATH_AS_INDIVIDUALS_EXACT: case AI_FOLLOW_PATH:
		case AI_ATTACKFOLLOW_WAYPOINT_PATH_AS_INDIVIDUALS: case AI_ATTACKFOLLOW_WAYPOINT_PATH_AS_TEAM:
			return TRUE;
		default:
			return FALSE;
	}
}

/** Fill one Chinook at home with the attack teams' ground units at home, the heavy ones first: a tank
	* walks slowest.  Not a parked team's, which waits for its own wave, nor a base guard's. */
Int AIPlayer::loadShuttle( Object *ship )
{
	ContainModuleInterface *hold = ship->getContain();
	Int freeSlots = hold->getContainMax() - (Int)hold->getContainCount() - hold->getExtraSlotsInUse();
	std::vector<Object *> cargo;
	for( Player::PlayerTeamList::const_iterator t = m_player->getPlayerTeams()->begin(); t != m_player->getPlayerTeams()->end(); ++t )
	{
		const TeamTemplateInfo *info = (*t)->getTemplateInfo();
		if( info->m_isBaseDefense || info->m_isPerimeterDefense || !aiTeamAttacks( info ) )
			continue;
		for( DLINK_ITERATOR<Team> iter = (*t)->iterate_TeamInstanceList(); !iter.done(); iter.advance() )
		{
			Team *team = iter.cur();
			if( !team->isActive() || team == m_player->getDefaultTeam() )
				continue;
			Bool parked = FALSE;
			for( Int i = 0; i < MAX_HELD_TEAMS; ++i )
				if( m_heldUsed[ i ] && m_heldTeam[ i ] == team->getID() )
					parked = TRUE;
			if( parked )
				continue;
			for( DLINK_ITERATOR<Object> m = team->iterate_TeamMemberList(); !m.done(); m.advance() )
			{
				Object *obj = m.cur();
				if( obj->isEffectivelyDead() || obj->getAI() == NULL || obj->isContained() || obj->isKindOf( KINDOF_AIRCRAFT ) ||
						obj->isKindOf( KINDOF_STRUCTURE ) || obj->isKindOf( KINDOF_IMMOBILE ) || obj->getTransportSlotCount() <= 0 ||
						!isAtHome( obj->getPosition() ) || !leavesToFinish( obj ) || isFallingBack( obj->getID() ) ||
						!shuttleState( obj->getAI()->getCurrentStateID() ) || !hold->isValidContainerFor( obj, FALSE ) )
					continue;
				cargo.push_back( obj );
			}
		}
	}

	Int vehicles = 0;
	Int infantry = 0;
	Int slots = 0;
	// two passes over the same list: what takes three slots or more, then the rest
	for( Int heavy = 1; heavy >= 0; --heavy )
	{
		for( size_t c = 0; c < cargo.size(); ++c )
		{
			Object *obj = cargo[ c ];
			const Int need = obj->getTransportSlotCount();
			if( (need >= 3) != (heavy == 1) || need > freeSlots )
				continue;
			freeSlots -= need;
			slots += need;
			if( obj->isKindOf( KINDOF_INFANTRY ) )
				++infantry;
			else
				++vehicles;
			// out of the wave's group, which would otherwise hold its road for a member in a hold
			if( obj->getGroup() )
				obj->getGroup()->remove( obj );
			obj->getAI()->aiEnter( ship, CMD_FROM_AI );
		}
	}
	if( vehicles + infantry > 0 )
		DEBUG_LOG(("AI SHUTTLE frame %d player %d loads %d vehicles and %d infantry, %d of %d slots, into '%s' %d\n", TheGameLogic->getFrame(),
			m_player->getPlayerIndex(), vehicles, infantry, slots, hold->getContainMax(), ship->getTemplate()->getName().str(), ship->getID()));
	return vehicles + infantry;
}

/** One more transport Chinook while the last wave needs more lift than there is: its slots over what
	* the Chinooks can carry in the window, a round trip being the flight there and back plus the
	* turnaround.  One order at a time, out of the bank above the cash hoard. */
void AIPlayer::buyTransportChinook( void )
{
	if( m_lastWaveSlots <= 0 || isBaseUnderAttack() || scoutInQueue() ||
			(Int)m_player->getMoney()->countMoney() <= getSkillProfile()->m_cashHoardThreshold )
		return;
	Int owned = 0;
	for( Int shuttle = 0; shuttle < MAX_TRANSPORT_CHINOOKS; ++shuttle )
	{
		const Object *obj = TheGameLogic->findObjectByID( m_shuttle[ shuttle ].id );
		if( obj && !obj->isEffectivelyDead() && obj->getControllingPlayer() == m_player )
			++owned;
	}
	if( owned >= MAX_TRANSPORT_CHINOOKS )
		return;

	std::vector<Object *> factories;
	m_player->iterateObjects( collectFactories, &factories );
	const ThingTemplate *transport = NULL;
	for( size_t f = 0; f < factories.size() && transport == NULL; ++f )
		transport = chinookTemplate( factories[ f ], FALSE );
	if( transport == NULL )
		return;

	const Real flight = (Real)sqrt( sqr( m_shuttleFront.x - m_baseCenter.x ) + sqr( m_shuttleFront.y - m_baseCenter.y ) );
	const UnsignedInt roundTrip = (UnsignedInt)(2.0f * flight / CHINOOK_CRUISE_SPEED * LOGICFRAMES_PER_SECOND) + SHUTTLE_TURNAROUND_FRAMES;
	const Int trips = max( 1, (Int)(SHUTTLE_WINDOW_FRAMES / roundTrip) );
	const Int lift = transportContainOf( transport )->m_slotCapacity * trips;
	const Int needed = min( (Int)MAX_TRANSPORT_CHINOOKS, (m_lastWaveSlots + lift - 1) / lift );
	if( owned >= needed )
		return;
	queueSupportUnit( transport, "SHUTTLE" );
	if( scoutInQueue() )
		DEBUG_LOG(("AI SHUTTLE frame %d player %d orders '%s' as a transport, %d owned, %d needed for %d slots %.0f away, %d trips each, %d in the bank\n",
			TheGameLogic->getFrame(), m_player->getPlayerIndex(), transport->getName().str(), owned, needed, m_lastWaveSlots, flight, trips,
			m_player->getMoney()->countMoney()));
}

/** Gatherers of this player's that are not on duty, and whether one could be had: a gatherer order in
	* the queue, or a plain one a factory can build and the bank can pay for. */
struct GathererCount
{
	const AIPlayer *owner;
	ObjectID lent;
	Int count;
};

static void countGatherers( Object *obj, void *userData )
{
	GathererCount *gatherers = (GathererCount *)userData;
	if( obj->isKindOf( KINDOF_HARVESTER ) && !obj->isEffectivelyDead() && obj->getAI() && obj->getAI()->getSupplyTruckAIInterface() &&
			obj->getID() != gatherers->lent && !gatherers->owner->isDutyChinook( obj ) )
		++gatherers->count;
}

/** A duty Chinook, gunship or transport, gathers only when the player has no gatherer left and cannot
	* get one: no factory for one, or no money.  It goes back to its duty the moment a real one exists. */
void AIPlayer::lendDutyChinook( void )
{
	GathererCount gatherers;
	gatherers.owner = this;
	gatherers.lent = m_lentChinook;
	gatherers.count = 0;
	m_player->iterateObjects( countGatherers, &gatherers );

	Object *lent = TheGameLogic->findObjectByID( m_lentChinook );
	if( lent && (lent->isEffectivelyDead() || lent->getControllingPlayer() != m_player) )
		lent = NULL;
	if( lent == NULL )
		m_lentChinook = INVALID_ID;
	if( lent )
	{
		if( gatherers.count == 0 )
			return;
		lent->getAI()->getSupplyTruckAIInterface()->setForceWantingState( FALSE );
		lent->getAI()->aiIdle( CMD_FROM_AI );
		DEBUG_LOG(("AI SHUTTLE frame %d player %d '%s' %d stops gathering, %d gatherers again\n", TheGameLogic->getFrame(),
			m_player->getPlayerIndex(), lent->getTemplate()->getName().str(), lent->getID(), gatherers.count));
		m_lentChinook = INVALID_ID;
		for( Int shuttle = 0; shuttle < MAX_TRANSPORT_CHINOOKS; ++shuttle )
			if( m_shuttle[ shuttle ].id == lent->getID() )
				m_shuttle[ shuttle ].phase = SHUTTLE_RETURNING;
		return;
	}
	if( gatherers.count > 0 )
		return;

	for( DLINK_ITERATOR<TeamInQueue> iter = iterate_TeamBuildQueue(); !iter.done(); iter.advance() )
		for( const WorkOrder *order = iter.cur()->m_workOrders; order; order = order->m_next )
			if( order->m_isResourceGatherer )
				return;		// one is on its way
	const Bool canBuildUnits = m_player->getCanBuildUnits();
	m_player->setCanBuildUnits( true );
	Bool canGet = FALSE;
	for( const ThingTemplate *tmpl = TheThingFactory->firstTemplate(); tmpl && !canGet; tmpl = tmpl->friend_getNextTemplate() )
		canGet = tmpl->isKindOf( KINDOF_HARVESTER ) && !passengersFireFrom( tmpl ) && findFactory( tmpl, true ) != NULL &&
			(Int)m_player->getMoney()->countMoney() >= tmpl->calcCostToBuild( m_player );
	m_player->setCanBuildUnits( canBuildUnits );
	if( canGet )
		return;

	// a transport before a gunship: it carries nobody between waves anyway
	Object *duty = NULL;
	for( Int shuttle = 0; shuttle < MAX_TRANSPORT_CHINOOKS && duty == NULL; ++shuttle )
	{
		Object *obj = TheGameLogic->findObjectByID( m_shuttle[ shuttle ].id );
		if( obj && isTransportChinook( obj ) )
			duty = obj;
	}
	for( Int chinook = 0; chinook < MAX_GUNSHIP_CHINOOKS && duty == NULL; ++chinook )
	{
		Object *obj = TheGameLogic->findObjectByID( m_gunshipChinook[ chinook ] );
		if( obj && isGunshipChinook( obj ) )
			duty = obj;
	}
	if( duty == NULL )
		return;
	m_lentChinook = duty->getID();
	duty->getAI()->getSupplyTruckAIInterface()->setForceWantingState( TRUE );
	DEBUG_LOG(("AI SHUTTLE frame %d player %d lends '%s' %d to the supply runs, no gatherer left and none to be had\n", TheGameLogic->getFrame(),
		m_player->getPlayerIndex(), duty->getTemplate()->getName().str(), duty->getID()));
}

void AIPlayer::doShuttles( void )
{
	if( !runsOwnAI() || !m_baseCenterSet || m_skillLevel == AISKILL_EASY || measuringWithoutTactics() )
		return;
	if( (TheGameLogic->getFrame() + computeUpdatePhase( m_player->getPlayerIndex(), SHUTTLE_CHECK_RATE )) % SHUTTLE_CHECK_RATE != 0 )
		return;
	lendDutyChinook();
	buyTransportChinook();

	const UnsignedInt now = TheGameLogic->getFrame();
	const Real flight = (Real)sqrt( sqr( m_shuttleFront.x - m_baseCenter.x ) + sqr( m_shuttleFront.y - m_baseCenter.y ) );
	const Bool frontOpen = m_shuttleFrontFrame != 0 && now - m_shuttleFrontFrame < SHUTTLE_WINDOW_FRAMES &&
		flight > 2.0f * m_baseRadius + SHUTTLE_MIN_FLIGHT && !isBaseUnderAttack();
	std::vector<EnterWalker> walkers;
	m_player->iterateObjects( collectEnterWalkers, &walkers );

	for( Int s = 0; s < MAX_TRANSPORT_CHINOOKS; ++s )
	{
		ShuttleChinook &shuttle = m_shuttle[ s ];
		if( shuttle.id == INVALID_ID )
			continue;
		Object *ship = TheGameLogic->findObjectByID( shuttle.id );
		if( ship == NULL || ship->isEffectivelyDead() || ship->getControllingPlayer() != m_player )
		{
			static const char *const phases[] = { "at home", "loading", "out", "returning" };
			DEBUG_LOG(("AI SHUTTLE frame %d player %d loses transport %d %s, %d units aboard\n", now, m_player->getPlayerIndex(),
				shuttle.id, phases[ shuttle.phase ], shuttle.phase == SHUTTLE_OUT ? shuttle.load : 0));
			shuttle.id = INVALID_ID;
			continue;
		}
		if( shuttle.id == m_lentChinook )
			continue;
		AIUpdateInterface *ai = ship->getAI();
		ContainModuleInterface *hold = ship->getContain();
		const Coord3D *pos = ship->getPosition();
		Int boarding = 0;
		for( size_t w = 0; w < walkers.size(); ++w )
			if( walkers[ w ].goal == shuttle.id )
				++boarding;

		switch( shuttle.phase )
		{
			case SHUTTLE_HOME:
				if( !ai->isIdle() )
					break;
				if( !isAtHome( pos ) )
				{
					// the supply center's door and its rally point are out by the supply piles, past what
					// counts as home: every transport waited there and nothing was ever loaded.  It waits
					// inside the base instead, on the side the waves go out
					Coord3D stage = m_baseCenter;
					const Real frontDist = (Real)sqrt( sqr( m_shuttleFront.x - m_baseCenter.x ) + sqr( m_shuttleFront.y - m_baseCenter.y ) );
					if( m_shuttleFrontFrame != 0 && frontDist > 1.0f )
					{
						stage.x += (m_shuttleFront.x - m_baseCenter.x) / frontDist * m_baseRadius;
						stage.y += (m_shuttleFront.y - m_baseCenter.y) / frontDist * m_baseRadius;
					}
					stage.z = TheTerrainLogic->getGroundHeight( stage.x, stage.y );
					ai->aiMoveToPosition( &stage, CMD_FROM_AI );
					break;
				}
				if( !frontOpen )
					break;
				shuttle.home = *pos;
				if( hold->getContainCount() > 0 || loadShuttle( ship ) > 0 )
				{
					shuttle.phase = SHUTTLE_LOADING;
					shuttle.frame = now;
				}
				break;

			case SHUTTLE_LOADING:
				// wait while anybody is still coming and there is room, for a few seconds at least: an order
				// given this second shows nobody walking yet
				if( now - shuttle.frame < SHUTTLE_LOAD_FRAMES &&
						(now - shuttle.frame < SHUTTLE_LOAD_MIN_FRAMES ||
						 (boarding > 0 && (Int)hold->getContainCount() + hold->getExtraSlotsInUse() < hold->getContainMax())) )
					break;
				// whoever has not got in by now stays for the next trip
				for( size_t w = 0; w < walkers.size(); ++w )
					if( walkers[ w ].goal == shuttle.id )
						walkers[ w ].walker->getAI()->aiIdle( CMD_FROM_AI );
				shuttle.frame = now;
				if( hold->getContainCount() == 0 || m_shuttleFrontFrame == 0 )
				{
					shuttle.phase = SHUTTLE_HOME;
					break;
				}
				shuttle.drop = m_shuttleFront;
				if( !flightIsQuiet( pos, m_shuttleFront.x, m_shuttleFront.y ) )
				{
					// the way out has turned hot since the wave left: as far along it as is still quiet, and
					// if that is hardly past home they get out and walk.  Staying home outright whenever
					// the straight line touched a hot cell kept 74 loads at home over four matches
					const Real dx = m_shuttleFront.x - pos->x;
					const Real dy = m_shuttleFront.y - pos->y;
					const Real flight = (Real)sqrt( dx * dx + dy * dy );
					Real reach = 0.0f;
					for( Real along = INFLUENCE_CELL_SIZE; along <= flight; along += INFLUENCE_CELL_SIZE )
					{
						if( m_influence.enemyAt( pos->x + dx * along / flight, pos->y + dy * along / flight ) > 0.0f )
							break;
						reach = along;
					}
					shuttle.drop.x = pos->x + dx * reach / flight;
					shuttle.drop.y = pos->y + dy * reach / flight;
					shuttle.drop.z = TheTerrainLogic->getGroundHeight( shuttle.drop.x, shuttle.drop.y );
					if( isAtHome( &shuttle.drop ) || reach < SHUTTLE_MIN_FLIGHT )
					{
						DEBUG_LOG(("AI SHUTTLE frame %d player %d '%s' %d stays home, the flight to (%.0f,%.0f) is hot after %.0f, %d units walk\n", now,
							m_player->getPlayerIndex(), ship->getTemplate()->getName().str(), shuttle.id, m_shuttleFront.x, m_shuttleFront.y,
							reach, (Int)hold->getContainCount()));
						ai->aiEvacuate( FALSE, CMD_FROM_AI );
						shuttle.phase = SHUTTLE_HOME;
						break;
					}
				}
				shuttle.load = (Int)hold->getContainCount();
				shuttle.aborted = FALSE;
				{
					Int vehicles = 0;
					const ContainedItemsList *items = hold->getContainedItemsList();
					for( ContainedItemsList::const_iterator it = items->begin(); it != items->end(); ++it )
					{
						m_droppedRiders.push_back( (*it)->getID() );
						if( (*it)->isKindOf( KINDOF_VEHICLE ) )
							++vehicles;
					}
					DEBUG_LOG(("AI SHUTTLE frame %d player %d flies %d units, %d vehicles, %d of %d slots, in '%s' %d to (%.0f,%.0f)\n", now,
						m_player->getPlayerIndex(), shuttle.load, vehicles, (Int)hold->getContainCount() + hold->getExtraSlotsInUse(), hold->getContainMax(),
						ship->getTemplate()->getName().str(), shuttle.id, shuttle.drop.x, shuttle.drop.y));
				}
				// an order rather than an AI's aside, as doTransports gives it
				ai->aiMoveToAndEvacuate( &shuttle.drop, CMD_FROM_SCRIPT );
				shuttle.phase = SHUTTLE_OUT;
				break;

			case SHUTTLE_OUT:
				if( hold->getContainCount() > 0 )
				{
					const BodyModuleInterface *body = ship->getBodyModule();
					if( !shuttle.aborted && body->getHealth() < body->getMaxHealth() * SHUTTLE_ABORT_HEALTH )
					{
						// hit on the way: down here, before the rest of the flight costs the load as well
						Coord3D here = *pos;
						here.z = TheTerrainLogic->getGroundHeight( here.x, here.y );
						DEBUG_LOG(("AI SHUTTLE frame %d player %d '%s' %d aborts at (%.0f,%.0f), %.0f%% health, puts %d units down\n", now,
							m_player->getPlayerIndex(), ship->getTemplate()->getName().str(), shuttle.id, here.x, here.y,
							100.0f * body->getHealth() / body->getMaxHealth(), (Int)hold->getContainCount()));
						shuttle.drop = here;
						shuttle.aborted = TRUE;
						ai->aiMoveToAndEvacuate( &shuttle.drop, CMD_FROM_SCRIPT );
						shuttle.frame = now;
					}
					else if( now - shuttle.frame > SHUTTLE_LEG_FRAMES )
					{
						ai->aiMoveToAndEvacuate( &shuttle.drop, CMD_FROM_SCRIPT );
						shuttle.frame = now;
					}
					break;
				}
				DEBUG_LOG(("AI SHUTTLE frame %d player %d '%s' %d drops %d units at (%.0f,%.0f), %d s out, %.0f from home\n", now,
					m_player->getPlayerIndex(), ship->getTemplate()->getName().str(), shuttle.id, shuttle.load, pos->x, pos->y,
					(now - shuttle.frame) / LOGICFRAMES_PER_SECOND, (Real)sqrt( sqr( pos->x - shuttle.home.x ) + sqr( pos->y - shuttle.home.y ) )));
				ai->aiMoveToPosition( &shuttle.home, CMD_FROM_AI );
				shuttle.phase = SHUTTLE_RETURNING;
				shuttle.frame = now;
				break;

			case SHUTTLE_RETURNING:
				if( isAtHome( pos ) && ai->isIdle() )
				{
					DEBUG_LOG(("AI SHUTTLE frame %d player %d '%s' %d back home after %d s\n", now, m_player->getPlayerIndex(),
						ship->getTemplate()->getName().str(), shuttle.id, (now - shuttle.frame) / LOGICFRAMES_PER_SECOND));
					shuttle.phase = SHUTTLE_HOME;
					shuttle.frame = now;
				}
				else if( ai->isIdle() || now - shuttle.frame > SHUTTLE_LEG_FRAMES )
				{
					ai->aiMoveToPosition( &shuttle.home, CMD_FROM_AI );
					shuttle.frame = now;
				}
				break;
		}
	}
}

//----------------------------------------------------------------------------------------------------------
/** China's Helix carries one of three portable structures, and each shuts the other two out.  The
	* computer bought every upgrade on the Helix's buttons in button order, so every Helix it flew got a
	* battle bunker whatever it was up against, and napalm on top.  Now a team's Helix takes the gattling
	* against an enemy of more men than vehicles and the bunker otherwise, and the computer buys Helixes
	* for jobs of their own: two healers under a propaganda tower, a third once the army is big, that
	* hang behind the wave out of every known gun's reach, and two raiders that bomb the enemy's
	* buildings from whichever side his anti-air does not cover. */
//----------------------------------------------------------------------------------------------------------
static const Int HELIX_CHECK_RATE = LOGICFRAMES_PER_SECOND;

/** The Helix upgrades by what they do, matched on the name: the Nuke and Infantry generals carry their
	* own copies, Nuke_Upgrade_HelixNukeBomb and Upgrade_Infa_ChinaHelixBattleBunker. */
enum { HELIX_GATTLING, HELIX_BUNKER, HELIX_TOWER, HELIX_BOMB, HELIX_UPGRADE_KINDS };
static const char *const HELIX_UPGRADE_MARK[ HELIX_UPGRADE_KINDS ] = { "GattlingCannon", "BattleBunker", "PropagandaTower", "Bomb" };
static const char *const HELIX_ROLE_NAME[ 2 ] = { "healer", "raider" };

static const Int MIN_HEALERS = 2;
static const Int MAX_HEALERS = 3;
static const Int HEALER_THIRD_ARMY = 30;		///< fighting units before a third healer is bought
static const Int MAX_RAIDERS = 2;
static const Real HEALER_PATIENT_HEALTH = 0.9f;	///< a unit below this share of its health is worth hovering over
static const UnsignedInt HEALER_HIT_FRAMES = 3 * LOGICFRAMES_PER_SECOND;	///< a healer hit this recently pulls back
static const UnsignedInt HEALER_REPORT_FRAMES = 60 * LOGICFRAMES_PER_SECOND;
/** The reach of a Helix's propaganda tower, ChinaHelixPropagandaTower's Radius in retail ChinaAir.ini.
	* A unit inside two towers heals as fast as inside one (Object::attemptHealingFromSoleBenefactor),
	* so a healer keeps its spot this far from every other healer's. */
static const Real HEALER_TOWER_RADIUS = 150.0f;

/** How far past a known gun's reach a Helix keeps, for the gun's own step forward. */
static const Real HELIX_GUN_MARGIN = 80.0f;
/** A Helix's spot has to move this far before it is given a new one; it also counts as arrived. */
static const Real HELIX_REORDER_DISTANCE = 60.0f;

/** A raid comes in to its building from this far out, on whichever of eight sides the fewest known
	* anti-air guns cover, and leaves the same way.  The flight out to that point crosses none. */
static const Real RAID_ENTRY_DISTANCE = 400.0f;
static const Int RAID_SIDES = 8;
static const Real RAID_SIDE[ RAID_SIDES ][ 2 ] = {
	{ 1.0f, 0.0f }, { 0.7071f, 0.7071f }, { 0.0f, 1.0f }, { -0.7071f, 0.7071f },
	{ -1.0f, 0.0f }, { -0.7071f, -0.7071f }, { 0.0f, -1.0f }, { 0.7071f, -0.7071f } };
static const Int RAID_MAX_GUNS_ON_RUN = 1;
/** How far ahead on its way in a raider looks for anti-air turning up.  The whole road, looked at
	* from home, turned hot within two seconds on raid after raid: a gun on wheels crossing the far end
	* of a flight across the map has moved on by the time the Helix gets there. */
static const Real RAID_LOOKAHEAD = 600.0f;
/** Nothing heals a Helix at home but a healer passing over it, so the bar to leave is low. */
static const Real RAID_DEPART_HEALTH = 0.7f;
static const Real RAID_ABORT_HEALTH = 0.5f;
static const UnsignedInt RAID_RUN_FRAMES = 30 * LOGICFRAMES_PER_SECOND;
static const UnsignedInt RAID_LEG_FRAMES = 90 * LOGICFRAMES_PER_SECOND;

/** How far round a healer looks for clear ground when the way home is covered. */
static const Real REAR_SEARCH_REACH = 900.0f;

/** What deepestReach answers with no gun at all: further out than any gun reaches. */
static const Real NO_GUN_DEPTH = -1.0e9f;

/** One kind of Helix upgrade on the buttons a Helix of this kind is built with, NULL when it has none.
	* The built-with set, because an upgraded Helix swaps its own for a shorter one. */
static const UpgradeTemplate *helixUpgradeOn( const ThingTemplate *tmpl, Int kind )
{
	const CommandSet *commandSet = TheCommandStore->findCommandSet( tmpl->friend_getCommandSetString() );
	if( commandSet == NULL )
		return NULL;
	for( Int i = 0; i < MAX_COMMANDS_PER_SET; ++i )
	{
		const CommandButton *button = commandSet->getCommandButton( i );
		if( button && button->getCommandType() == GUI_COMMAND_OBJECT_UPGRADE && button->getUpgradeTemplate() &&
				strstr( button->getUpgradeTemplate()->getUpgradeName().str(), HELIX_UPGRADE_MARK[ kind ] ) != NULL )
			return button->getUpgradeTemplate();
	}
	return NULL;
}

/** A Helix of this kind flies the napalm bomb flyRaid drops.  A mod's Helix can carry a "Bomb" upgrade
	* on its buttons and drop something else, or nothing the AI knows: ShockWave's under -modfactions
	* came out as a raider with no such power and the raid read a null one. */
static Bool helixCarriesNapalmBomb( const ThingTemplate *tmpl )
{
	const ModuleInfo &modules = tmpl->getBehaviorModuleInfo();
	for( Int m = 0; m < modules.getCount(); ++m )
	{
		if( modules.getNthName( m ).compareNoCase( "SpecialAbilityUpdate" ) != 0 )
			continue;
		const SpecialPowerTemplate *power = static_cast<const SpecialAbilityUpdateModuleData *>( modules.getNthData( m ) )->m_specialPowerTemplate;
		if( power && power->getSpecialPowerType() == SPECIAL_HELIX_NAPALM_BOMB )
			return TRUE;
	}
	return FALSE;
}

/** How much a raid wants a building: production, then income, then power; 0 for anything else. */
static Int raidPreference( const Object *obj )
{
	if( obj->isKindOf( KINDOF_FS_BARRACKS ) || obj->isKindOf( KINDOF_FS_WARFACTORY ) || obj->isKindOf( KINDOF_FS_AIRFIELD ) )
		return 3;
	if( obj->isKindOf( KINDOF_FS_SUPPLY_CENTER ) || obj->isKindOf( KINDOF_FS_SUPPLY_DROPZONE ) || obj->isKindOf( KINDOF_FS_BLACK_MARKET ) ||
			obj->isKindOf( KINDOF_FS_INTERNET_CENTER ) || obj->isKindOf( KINDOF_CASH_GENERATOR ) )
		return 2;
	if( obj->isKindOf( KINDOF_FS_POWER ) )
		return 1;
	return 0;
}

/** How far inside the reach of the gun that reaches it deepest a point stands; below zero it is clear
	* of every one. */
static Real deepestReach( const std::vector<AIKnownGun> &guns, Real x, Real y, Bool aircraftOnly )
{
	Real deepest = NO_GUN_DEPTH;
	for( size_t g = 0; g < guns.size(); ++g )
	{
		const Real reach = aircraftOnly ? guns[ g ].m_airReach : guns[ g ].m_reach;
		if( reach <= 0.0f )
			continue;
		const Real depth = reach - (Real)sqrt( sqr( x - guns[ g ].m_x ) + sqr( y - guns[ g ].m_y ) );
		if( depth > deepest )
			deepest = depth;
	}
	return deepest;
}

/** The known anti-air guns that reach a straight flight from a to b, margin included, each counted once. */
static Int gunsOverFlight( const std::vector<AIKnownGun> &guns, Real ax, Real ay, Real bx, Real by )
{
	const Real dx = bx - ax;
	const Real dy = by - ay;
	const Real lengthSqr = dx * dx + dy * dy;
	Int count = 0;
	for( size_t g = 0; g < guns.size(); ++g )
	{
		if( guns[ g ].m_airReach <= 0.0f )
			continue;
		Real along = 0.0f;
		if( lengthSqr > 0.0f )
			along = ((guns[ g ].m_x - ax) * dx + (guns[ g ].m_y - ay) * dy) / lengthSqr;
		if( along < 0.0f )
			along = 0.0f;
		if( along > 1.0f )
			along = 1.0f;
		if( sqr( guns[ g ].m_x - (ax + dx * along) ) + sqr( guns[ g ].m_y - (ay + dy * along) ) <= sqr( guns[ g ].m_airReach + HELIX_GUN_MARGIN ) )
			++count;
	}
	return count;
}

/** The next stretch of a raid's way in crosses a known anti-air gun: the next RAID_LOOKAHEAD of the
	* flight to the entry point, and the bomb run itself once the entry point is that close. */
static Bool raidAheadIsHot( const std::vector<AIKnownGun> &guns, const Coord3D *pos, const Coord3D *entry, const Coord3D *target )
{
	const Real dx = entry->x - pos->x;
	const Real dy = entry->y - pos->y;
	const Real left = (Real)sqrt( dx * dx + dy * dy );
	const Real share = left > RAID_LOOKAHEAD ? RAID_LOOKAHEAD / left : 1.0f;
	if( gunsOverFlight( guns, pos->x, pos->y, pos->x + dx * share, pos->y + dy * share ) > 0 )
		return TRUE;
	return left <= RAID_LOOKAHEAD && gunsOverFlight( guns, entry->x, entry->y, target->x, target->y ) > RAID_MAX_GUNS_ON_RUN;
}

void AIPlayer::collectKnownGuns( std::vector<AIKnownGun> *guns ) const
{
	const Int me = m_player->getPlayerIndex();
	for( Object *obj = TheGameLogic->getFirstObject(); obj; obj = obj->getNextObject() )
	{
		// not a superweapon: a Scud Storm's weapon reaches 999999, and once one was known every healer saw
		// the whole map in reach and hung over its own base center pulling back until it was shot down
		if( obj->isEffectivelyDead() || obj->isKindOf( KINDOF_PROJECTILE ) || obj->isKindOf( KINDOF_FS_SUPERWEAPON ) ||
				m_player->getRelationship( obj->getTeam() ) != ENEMIES || !observerKnowsAbout( obj, me ) )
			continue;
		AIKnownGun gun;
		gun.m_x = obj->getPosition()->x;
		gun.m_y = obj->getPosition()->y;
		gun.m_reach = 0.0f;
		gun.m_airReach = 0.0f;
		for( Int slot = PRIMARY_WEAPON; slot < WEAPONSLOT_COUNT; ++slot )
		{
			const Weapon *weapon = obj->getWeaponInWeaponSlot( (WeaponSlotType)slot );
			if( weapon == NULL )
				continue;
			const Real range = weapon->getAttackRange( obj );
			if( range > gun.m_reach )
				gun.m_reach = range;
			if( (weapon->getTemplate()->getAntiMask() & WEAPON_ANTI_AIRBORNE_VEHICLE) != 0 && range > gun.m_airReach )
				gun.m_airReach = range;
		}
		if( gun.m_reach > 0.0f )
			guns->push_back( gun );
	}
}

Int AIPlayer::helixRoleWanted( const ThingTemplate *tmpl ) const
{
	Int healers = 0;
	Int raiders = 0;
	Int free = 0;
	for( Int slot = 0; slot < MAX_DUTY_HELIXES; ++slot )
	{
		if( m_dutyHelix[ slot ].id == INVALID_ID )
			++free;
		else if( m_dutyHelix[ slot ].role == HELIX_HEALER )
			++healers;
		else
			++raiders;
	}
	if( free == 0 )
		return -1;
	const Int healersWanted = tallyBase( m_player ).army >= HEALER_THIRD_ARMY ? MAX_HEALERS : MIN_HEALERS;
	if( healers < healersWanted && helixUpgradeOn( tmpl, HELIX_TOWER ) )
		return HELIX_HEALER;
	if( raiders < MAX_RAIDERS && helixUpgradeOn( tmpl, HELIX_BOMB ) && helixCarriesNapalmBomb( tmpl ) )
		return HELIX_RAIDER;
	return -1;
}

/** One order at a time, through the support-unit queue like the gunship Chinook, out of the bank above
	* the cash hoard.  The job is read off the Helix's own buttons: the Infantry General's has no
	* propaganda tower and never heals. */
void AIPlayer::buyDutyHelix( void )
{
	if( isBaseUnderAttack() || scoutInQueue() || (Int)m_player->getMoney()->countMoney() <= getSkillProfile()->m_cashHoardThreshold )
		return;
	std::vector<Object *> factories;
	m_player->iterateObjects( collectFactories, &factories );
	for( size_t f = 0; f < factories.size(); ++f )
	{
		const CommandSet *commandSet = TheCommandStore->findCommandSet( factories[ f ]->getCommandSetString() );
		if( commandSet == NULL )
			continue;
		for( Int i = 0; i < MAX_COMMANDS_PER_SET; ++i )
		{
			const CommandButton *button = commandSet->getCommandButton( i );
			if( button == NULL || button->getCommandType() != GUI_COMMAND_UNIT_BUILD )
				continue;
			const ThingTemplate *tmpl = button->getThingTemplate();
			if( tmpl == NULL || !tmpl->isKindOf( KINDOF_AIRCRAFT ) || tmpl->isKindOf( KINDOF_HARVESTER ) ||
					TheBuildAssistant->canMakeUnit( factories[ f ], tmpl ) != CANMAKE_OK )
				continue;
			const Int role = helixRoleWanted( tmpl );
			if( role < 0 )
				continue;
			queueSupportUnit( tmpl, "HELIX" );
			if( scoutInQueue() )
				DEBUG_LOG(("AI HELIX frame %d player %d orders '%s' as a %s, %d in the bank\n", TheGameLogic->getFrame(),
					m_player->getPlayerIndex(), tmpl->getName().str(), HELIX_ROLE_NAME[ role ], m_player->getMoney()->countMoney()));
			return;
		}
	}
}

void AIPlayer::takeDutyHelix( Object *helix )
{
	const Int role = helixRoleWanted( helix->getTemplate() );
	if( role < 0 )
		return;		// the need went while it was trained: it stays a plain helicopter for the waves
	for( Int slot = 0; slot < MAX_DUTY_HELIXES; ++slot )
	{
		DutyHelix &duty = m_dutyHelix[ slot ];
		if( duty.id != INVALID_ID )
			continue;
		duty.id = helix->getID();
		duty.role = role;
		duty.phase = role == HELIX_HEALER ? HEAL_HOME : RAID_HOME;
		duty.frame = TheGameLogic->getFrame();
		duty.target = INVALID_ID;
		duty.spot = *helix->getPosition();
		duty.targetHealth = 0.0f;
		DEBUG_LOG(("AI HELIX frame %d player %d '%s' %d comes out as a %s\n", TheGameLogic->getFrame(), m_player->getPlayerIndex(),
			helix->getTemplate()->getName().str(), helix->getID(), HELIX_ROLE_NAME[ role ]));
		return;
	}
}

/** role is -1 for a team's Helix.  China's attack-team script queues the gattling on every Helix it
	* launches, so a structure queued by anyone that is not the one wanted is taken back off the queue. */
void AIPlayer::upgradeHelix( Object *helix, Int role, AIEnemyComposition *enemy, Bool *enemyRead )
{
	const UpgradeTemplate *upgrades[ HELIX_UPGRADE_KINDS ];
	Bool any = FALSE;
	for( Int kind = 0; kind < HELIX_UPGRADE_KINDS; ++kind )
	{
		upgrades[ kind ] = helixUpgradeOn( helix->getTemplate(), kind );
		any = any || upgrades[ kind ] != NULL;
	}
	if( !any )
		return;

	Bool settled = FALSE;
	for( Int kind = HELIX_GATTLING; kind <= HELIX_TOWER; ++kind )
		if( upgrades[ kind ] && helix->hasUpgrade( upgrades[ kind ] ) )
			settled = TRUE;
	if( !*enemyRead )
	{
		computeEnemyComposition( enemy );
		*enemyRead = TRUE;
	}
	Int portable = HELIX_GATTLING;
	if( role == HELIX_HEALER )
		portable = HELIX_TOWER;
	else if( role < 0 )
	{
		// by threat, as the rest of the counter code reads the enemy: by head, infantry outnumbered the
		// vehicles in every match of the first batch, the Tank General's army included
		portable = enemy->m_infantry > enemy->m_armour - enemy->m_air ? HELIX_GATTLING : HELIX_BUNKER;
		if( upgrades[ portable ] == NULL )
			portable = portable == HELIX_GATTLING ? HELIX_BUNKER : HELIX_GATTLING;
	}
	const UpgradeTemplate *wanted = upgrades[ portable ];

	ProductionUpdateInterface *pu = helix->getProductionUpdateInterface();
	for( Int kind = HELIX_GATTLING; kind <= HELIX_TOWER && !settled; ++kind )
	{
		if( upgrades[ kind ] && upgrades[ kind ] != wanted && pu->isUpgradeInQueue( upgrades[ kind ] ) )
		{
			pu->cancelUpgrade( upgrades[ kind ] );
			DEBUG_LOG(("AI HELIX frame %d player %d takes '%s' off '%s' %d's queue\n", TheGameLogic->getFrame(), m_player->getPlayerIndex(),
				upgrades[ kind ]->getUpgradeName().str(), helix->getTemplate()->getName().str(), helix->getID()));
		}
	}
	if( pu->getProductionCount() > 0 )
		return;

	// a raider its bomb first; a team's Helix pays out of the bank above the hoard, one bought for a job pays for the job
	const UpgradeTemplate *next = settled ? NULL : wanted;
	if( role == HELIX_RAIDER && upgrades[ HELIX_BOMB ] && !helix->hasUpgrade( upgrades[ HELIX_BOMB ] ) )
		next = upgrades[ HELIX_BOMB ];
	if( next == NULL || !helix->affectedByUpgrade( next ) || !TheUpgradeCenter->canAffordUpgrade( m_player, next ) )
		return;
	if( role < 0 && (Int)m_player->getMoney()->countMoney() <= getSkillProfile()->m_cashHoardThreshold + next->calcCostToBuild( m_player ) )
		return;
	if( buyUpgrade( pu, next ) )
		DEBUG_LOG(("AI HELIX frame %d player %d upgrades '%s' %d with '%s' as a %s, enemy known: %d infantry, %d vehicles, threat %.0f%% infantry %.0f%% ground vehicles\n",
			TheGameLogic->getFrame(), m_player->getPlayerIndex(), helix->getTemplate()->getName().str(), helix->getID(),
			next->getUpgradeName().str(), role < 0 ? "team gunship" : HELIX_ROLE_NAME[ role ], enemy->m_infantryCount, enemy->m_vehicleCount,
			enemy->m_infantry * 100.0f, (enemy->m_armour - enemy->m_air) * 100.0f));
}

Coord3D AIPlayer::rearOf( const Coord3D *from, const std::vector<AIKnownGun> &guns, Real margin ) const
{
	const Real dx = m_baseCenter.x - from->x;
	const Real dy = m_baseCenter.y - from->y;
	const Real length = (Real)sqrt( dx * dx + dy * dy );
	Coord3D spot = m_baseCenter;
	spot.z = TheTerrainLogic->getGroundHeight( spot.x, spot.y );
	for( Real along = 0.0f; along < length; along += INFLUENCE_CELL_SIZE )
	{
		const Real x = from->x + dx * along / length;
		const Real y = from->y + dy * along / length;
		if( deepestReach( guns, x, y, FALSE ) < -margin )
		{
			spot.set( x, y, TheTerrainLogic->getGroundHeight( x, y ) );
			return spot;
		}
	}
	// the whole way home is covered, the base itself among it: the nearest clear ground any way round,
	// out to REAR_SEARCH_REACH
	Region3D extent;
	TheTerrainLogic->getExtent( &extent );
	for( Real reach = INFLUENCE_CELL_SIZE; reach <= REAR_SEARCH_REACH; reach += INFLUENCE_CELL_SIZE )
	{
		for( Int side = 0; side < RAID_SIDES; ++side )
		{
			const Real x = from->x + RAID_SIDE[ side ][ 0 ] * reach;
			const Real y = from->y + RAID_SIDE[ side ][ 1 ] * reach;
			if( x >= extent.lo.x && x <= extent.hi.x && y >= extent.lo.y && y <= extent.hi.y &&
					deepestReach( guns, x, y, FALSE ) < -margin )
			{
				spot.set( x, y, TheTerrainLogic->getGroundHeight( x, y ) );
				return spot;
			}
		}
	}
	return spot;
}

static Bool insideAnotherTower( const std::vector<Coord3D> &taken, Real x, Real y )
{
	for( size_t t = 0; t < taken.size(); ++t )
		if( sqr( taken[ t ].x - x ) + sqr( taken[ t ].y - y ) < sqr( HEALER_TOWER_RADIUS ) )
			return TRUE;
	return FALSE;
}

/** A healer goes over the most hurt of ours that stands clear of every known gun, out with the army
	* when the army is out; with nobody hurt it hangs behind the middle of whoever is out, at the first
	* point toward home no known gun reaches.  Hit, or with a known anti-air gun in reach, it pulls back
	* the same way.  It carries nobody and is in no wave.
	* Healers in lower slots choose first, and a later one leaves alone whatever already stands inside
	* their towers: it takes the most hurt unit outside them, and its place behind the wave moves
	* sideways across the way home until it clears them. */
void AIPlayer::steerHealer( Int slot, const std::vector<AIKnownGun> &guns )
{
	DutyHelix &duty = m_dutyHelix[ slot ];
	Object *helix = TheGameLogic->findObjectByID( duty.id );
	AIUpdateInterface *ai = helix->getAI();
	const Coord3D *pos = helix->getPosition();
	const UnsignedInt now = TheGameLogic->getFrame();

	// the measure, out of the base only: further from the nearest known gun than that gun reaches
	const AIKnownGun *nearest = NULL;
	Real nearestSqr = 0.0f;
	for( size_t g = 0; g < guns.size(); ++g )
	{
		const Real distSqr = sqr( guns[ g ].m_x - pos->x ) + sqr( guns[ g ].m_y - pos->y );
		if( nearest == NULL || distSqr < nearestSqr )
		{
			nearest = &guns[ g ];
			nearestSqr = distSqr;
		}
	}
	if( !isAtHome( pos ) )
	{
		++m_healerSeconds;
		if( nearest == NULL || nearestSqr > sqr( nearest->m_reach ) )
			++m_healerClearSeconds;
	}

	const UpgradeTemplate *tower = helixUpgradeOn( helix->getTemplate(), HELIX_TOWER );
	const UnsignedInt lastHit = helix->getBodyModule()->getLastDamageTimestamp();
	const Bool hit = lastHit != 0 && now - lastHit < HEALER_HIT_FRAMES;
	// anti-air with the margin; any other gun once the healer is inside its reach, which is short of the
	// margin a patient has to stand clear by, so the two do not hand the healer back and forth
	const Bool antiAirNear = deepestReach( guns, pos->x, pos->y, TRUE ) > -HELIX_GUN_MARGIN;
	const Bool inReach = deepestReach( guns, pos->x, pos->y, FALSE ) > 0.0f;
	Int phase = HEAL_HOME;
	Object *patient = NULL;
	Coord3D spot = m_baseCenter;
	Int out = 0;
	if( hit || antiAirNear || inReach )
	{
		phase = HEAL_PULLBACK;
		spot = rearOf( pos, guns, HELIX_GUN_MARGIN );
	}
	else if( helix->hasUpgrade( tower ) )
	{
		// the spots the healers in lower slots took this pass, the slots being steered in order
		std::vector<Coord3D> taken;
		for( Int s = 0; s < slot; ++s )
		{
			const DutyHelix &other = m_dutyHelix[ s ];
			if( other.id != INVALID_ID && other.role == HELIX_HEALER && (other.phase == HEAL_PATIENT || other.phase == HEAL_REAR) )
				taken.push_back( other.spot );
		}
		std::vector<Object *> owned;
		m_player->iterateObjects( collectOwned, &owned );
		Object *hurtOut = NULL;
		Object *hurtHome = NULL;
		Real missingOut = 0.0f;
		Real missingHome = 0.0f;
		Coord3D middle;
		middle.zero();
		for( size_t o = 0; o < owned.size(); ++o )
		{
			Object *obj = owned[ o ];
			if( obj->isEffectivelyDead() || obj->isContained() || obj->isKindOf( KINDOF_STRUCTURE ) || obj->isKindOf( KINDOF_AIRCRAFT ) ||
					(!obj->isKindOf( KINDOF_INFANTRY ) && !obj->isKindOf( KINDOF_VEHICLE )) || aiCombatPower( obj ) <= 0.0f )
				continue;
			const Coord3D *at = obj->getPosition();
			const Bool home = isAtHome( at );
			if( !home )
			{
				middle.x += at->x;
				middle.y += at->y;
				++out;
			}
			const BodyModuleInterface *body = obj->getBodyModule();
			const Real missing = body->getMaxHealth() - body->getHealth();
			if( body->getHealth() >= HEALER_PATIENT_HEALTH * body->getMaxHealth() || deepestReach( guns, at->x, at->y, FALSE ) >= -HELIX_GUN_MARGIN ||
					insideAnotherTower( taken, at->x, at->y ) )
				continue;
			if( !home && missing > missingOut )
			{
				hurtOut = obj;
				missingOut = missing;
			}
			else if( home && missing > missingHome )
			{
				hurtHome = obj;
				missingHome = missing;
			}
		}
		patient = hurtOut ? hurtOut : (out > 0 ? NULL : hurtHome);
		if( patient )
		{
			phase = HEAL_PATIENT;
			spot = *patient->getPosition();
		}
		else if( out > 0 )
		{
			phase = HEAL_REAR;
			middle.x /= out;
			middle.y /= out;
			spot = rearOf( &middle, guns, HELIX_GUN_MARGIN );
			// sideways, alternately right and left, by a tower's reach plus the slack a lower slot's spot
			// keeps before it is re-ordered, so a step always clears the spot it stepped away from
			const Real dx = m_baseCenter.x - middle.x;
			const Real dy = m_baseCenter.y - middle.y;
			const Real length = (Real)sqrt( dx * dx + dy * dy );
			if( length > 0.0f && insideAnotherTower( taken, spot.x, spot.y ) )
			{
				Region3D extent;
				TheTerrainLogic->getExtent( &extent );
				for( Int step = 1; step <= 4; ++step )
				{
					const Real side = (HEALER_TOWER_RADIUS + HELIX_REORDER_DISTANCE) * ((step + 1) / 2) * ((step & 1) ? 1.0f : -1.0f);
					const Real x = spot.x - dy / length * side;
					const Real y = spot.y + dx / length * side;
					if( x >= extent.lo.x && x <= extent.hi.x && y >= extent.lo.y && y <= extent.hi.y &&
							!insideAnotherTower( taken, x, y ) && deepestReach( guns, x, y, FALSE ) < -HELIX_GUN_MARGIN )
					{
						spot.x = x;
						spot.y = y;
						break;
					}
				}
			}
		}
	}
	if( phase == HEAL_HOME && isAtHome( pos ) )
		spot = *pos;
	spot.z = TheTerrainLogic->getGroundHeight( spot.x, spot.y );

	const ObjectID patientID = patient ? patient->getID() : INVALID_ID;
	const Bool moved = sqr( spot.x - duty.spot.x ) + sqr( spot.y - duty.spot.y ) > sqr( HELIX_REORDER_DISTANCE );
	const Bool away = sqr( spot.x - pos->x ) + sqr( spot.y - pos->y ) > sqr( HELIX_REORDER_DISTANCE );
	if( !moved && phase == duty.phase && !(ai->isIdle() && away) )
		return;
	if( phase != duty.phase || patientID != duty.target )
	{
		static const char *const doing[] = { "goes home", "hovers over a hurt unit", "follows behind the wave", "pulls back" };
		DEBUG_LOG(("AI HEALER frame %d player %d '%s' %d %s at (%.0f,%.0f)%s%s, %d units out, nearest known gun %.0f away reaching %.0f\n",
			now, m_player->getPlayerIndex(), helix->getTemplate()->getName().str(), duty.id, doing[ phase ], spot.x, spot.y,
			patient ? ", over " : (hit ? ", hit" : (antiAirNear ? ", anti-air near" : (inReach ? ", in a gun's reach" : ""))),
			patient ? patient->getTemplate()->getName().str() : "",
			out, nearest ? (Real)sqrt( nearestSqr ) : 0.0f, nearest ? nearest->m_reach : 0.0f));
	}
	ai->aiMoveToPosition( &spot, CMD_FROM_AI );
	duty.spot = spot;
	duty.phase = phase;
	duty.target = patientID;
	duty.frame = now;
}

/** The enemy building a raid goes for: production before income before power, then the fewest known
	* anti-air guns over the bomb run, then the shortest flight.  The way out to the entry point crosses
	* no known anti-air gun at all, and the entry point is on the map. */
Bool AIPlayer::pickRaidTarget( const Object *raider, const std::vector<AIKnownGun> &guns, Object **target, Coord3D *entry, Int *gunsOnRun ) const
{
	Region3D extent;
	TheTerrainLogic->getExtent( &extent );
	const Coord3D *from = raider->getPosition();
	const Int me = m_player->getPlayerIndex();
	Int bestPreference = 0;
	Real bestFlight = 0.0f;
	*target = NULL;
	for( Object *obj = TheGameLogic->getFirstObject(); obj; obj = obj->getNextObject() )
	{
		const Int preference = raidPreference( obj );
		if( preference == 0 || obj->isEffectivelyDead() || m_player->getRelationship( obj->getTeam() ) != ENEMIES || !observerKnowsAbout( obj, me ) )
			continue;
		const Coord3D *at = obj->getPosition();
		for( Int side = 0; side < RAID_SIDES; ++side )
		{
			const Real x = at->x + RAID_SIDE[ side ][ 0 ] * RAID_ENTRY_DISTANCE;
			const Real y = at->y + RAID_SIDE[ side ][ 1 ] * RAID_ENTRY_DISTANCE;
			if( x < extent.lo.x || x > extent.hi.x || y < extent.lo.y || y > extent.hi.y || gunsOverFlight( guns, from->x, from->y, x, y ) > 0 )
				continue;
			const Int run = gunsOverFlight( guns, x, y, at->x, at->y );
			if( run > RAID_MAX_GUNS_ON_RUN )
				continue;
			const Real flight = (Real)sqrt( sqr( x - from->x ) + sqr( y - from->y ) );
			if( *target == NULL || preference > bestPreference ||
					(preference == bestPreference && (run < *gunsOnRun || (run == *gunsOnRun && flight < bestFlight))) )
			{
				*target = obj;
				entry->set( x, y, TheTerrainLogic->getGroundHeight( x, y ) );
				*gunsOnRun = run;
				bestPreference = preference;
				bestFlight = flight;
			}
		}
	}
	return *target != NULL;
}

/** A raider at home with its bomb ready flies out to its entry point, drops on the building from
	* there, and comes back the way it went.  Hit below half its health, or with a known anti-air gun
	* turning up over the way in, it turns for home. */
void AIPlayer::flyRaid( Int slot, const std::vector<AIKnownGun> &guns )
{
	DutyHelix &duty = m_dutyHelix[ slot ];
	Object *helix = TheGameLogic->findObjectByID( duty.id );
	AIUpdateInterface *ai = helix->getAI();
	const Coord3D *pos = helix->getPosition();
	const UnsignedInt now = TheGameLogic->getFrame();
	const BodyModuleInterface *body = helix->getBodyModule();
	const Real health = body->getHealth() / body->getMaxHealth();
	SpecialPowerModuleInterface *bomb = helix->findSpecialPowerModuleInterface( SPECIAL_HELIX_NAPALM_BOMB );
	Object *target = TheGameLogic->findObjectByID( duty.target );
	const Bool targetGone = target == NULL || target->isEffectivelyDead();
	Coord3D home = m_baseCenter;
	home.z = TheTerrainLogic->getGroundHeight( home.x, home.y );
	const char *abort = NULL;

	switch( duty.phase )
	{
		case RAID_HOME:
		{
			// a raider waiting at home moves off from anti-air in the base, the way a healer does: 20 of the
			// 55 raiders lost in twelve matches went down at home
			const UnsignedInt lastHit = body->getLastDamageTimestamp();
			if( (lastHit != 0 && now - lastHit < HEALER_HIT_FRAMES) || deepestReach( guns, pos->x, pos->y, TRUE ) > -HELIX_GUN_MARGIN )
			{
				const Coord3D clear = rearOf( pos, guns, HELIX_GUN_MARGIN );
				ai->aiMoveToPosition( &clear, CMD_FROM_AI );
				break;
			}
			if( !isAtHome( pos ) )
			{
				if( ai->isIdle() )
					ai->aiMoveToPosition( &home, CMD_FROM_AI );
				break;
			}
			if( !bomb->isReady() || health < RAID_DEPART_HEALTH || isBaseUnderAttack() )
				break;
			Object *pick = NULL;
			Coord3D entry;
			Int gunsOnRun = 0;
			if( !pickRaidTarget( helix, guns, &pick, &entry, &gunsOnRun ) )
				break;
			DEBUG_LOG(("AI RAID frame %d player %d sends '%s' %d at '%s' %d of player %d, %.0f away, in from (%.0f,%.0f), %d known anti-air guns over the run, %d known in all\n",
				now, m_player->getPlayerIndex(), helix->getTemplate()->getName().str(), duty.id, pick->getTemplate()->getName().str(), pick->getID(),
				pick->getControllingPlayer()->getPlayerIndex(), (Real)sqrt( sqr( pick->getPosition()->x - pos->x ) + sqr( pick->getPosition()->y - pos->y ) ),
				entry.x, entry.y, gunsOnRun, (Int)guns.size()));
			duty.target = pick->getID();
			duty.spot = entry;
			duty.targetHealth = -1.0f;
			duty.phase = RAID_OUT;
			duty.frame = now;
			ai->aiMoveToPosition( &entry, CMD_FROM_AI );
			break;
		}

		case RAID_OUT:
			if( targetGone )
				abort = "its target is gone";
			else if( health < RAID_ABORT_HEALTH )
				abort = "hit";
			else if( raidAheadIsHot( guns, pos, &duty.spot, target->getPosition() ) )
			{
				// a new way in from here, to this building or another, before giving the raid up
				Object *pick = NULL;
				Coord3D entry;
				Int gunsOnRun = 0;
				if( pickRaidTarget( helix, guns, &pick, &entry, &gunsOnRun ) )
				{
					DEBUG_LOG(("AI RAID frame %d player %d '%s' %d turns from a hot way in, now at '%s' %d in from (%.0f,%.0f), %d known anti-air guns over the run\n",
						now, m_player->getPlayerIndex(), helix->getTemplate()->getName().str(), duty.id, pick->getTemplate()->getName().str(), pick->getID(),
						entry.x, entry.y, gunsOnRun));
					duty.target = pick->getID();
					duty.spot = entry;
					duty.frame = now;
					ai->aiMoveToPosition( &entry, CMD_FROM_AI );
					break;
				}
				abort = "the way in turned hot";
			}
			if( abort )
			{
				ai->aiMoveToPosition( &home, CMD_FROM_AI );
				break;
			}
			if( ai->isIdle() || sqr( pos->x - duty.spot.x ) + sqr( pos->y - duty.spot.y ) <= sqr( HELIX_REORDER_DISTANCE ) )
			{
				helix->doSpecialPowerAtLocation( bomb->getSpecialPowerTemplate(), target->getPosition(), INVALID_ANGLE, 0, FALSE );
				duty.phase = RAID_IN;
				duty.frame = now;
			}
			else if( now - duty.frame > RAID_LEG_FRAMES )
			{
				ai->aiMoveToPosition( &duty.spot, CMD_FROM_AI );
				duty.frame = now;
			}
			break;

		case RAID_IN:
			if( !bomb->isReady() )
			{
				// the bomb is away: its recharge has started
				duty.targetHealth = targetGone ? 0.0f : target->getBodyModule()->getHealth();
				DEBUG_LOG(("AI RAID frame %d player %d '%s' %d drops on '%s' %d at (%.0f,%.0f), %d s out, the building at %.0f of %.0f health\n",
					now, m_player->getPlayerIndex(), helix->getTemplate()->getName().str(), duty.id,
					targetGone ? "nothing" : target->getTemplate()->getName().str(), duty.target, pos->x, pos->y, (now - duty.frame) / LOGICFRAMES_PER_SECOND,
					duty.targetHealth, targetGone ? 0.0f : target->getBodyModule()->getMaxHealth()));
				ai->aiMoveToPosition( &duty.spot, CMD_FROM_AI );
				duty.phase = RAID_BACK;
				duty.frame = now;
			}
			else if( targetGone )
				abort = "its target is gone";
			else if( health < RAID_ABORT_HEALTH )
				abort = "hit";
			else if( now - duty.frame > RAID_RUN_FRAMES )
				abort = "the run took too long";
			if( abort )
				ai->aiMoveToPosition( &duty.spot, CMD_FROM_AI );
			// The bomb is a persistent ability: it drops again under the Helix every time it recharges, and
			// an aborted one flies back to its spot the moment the Helix is idle.  Only an order from
			// outside the AI ends it, and these are all the AI's own, so a raider bombed its way home and
			// went on bombing its own base every ten seconds.
			if( duty.phase == RAID_BACK || abort )
				helix->findSpecialAbilityUpdate( SPECIAL_HELIX_NAPALM_BOMB )->onExit( FALSE );
			break;

		case RAID_BACK:
			if( isAtHome( pos ) && ai->isIdle() )
			{
				DEBUG_LOG(("AI RAID frame %d player %d '%s' %d back home after %d s, %s, target %d %s, health at the drop %.0f, now %.0f\n", now,
					m_player->getPlayerIndex(), helix->getTemplate()->getName().str(), duty.id, (now - duty.frame) / LOGICFRAMES_PER_SECOND,
					duty.targetHealth < 0.0f ? "aborted" : "bombed", duty.target, targetGone ? "destroyed" : "standing",
					duty.targetHealth, targetGone ? 0.0f : target->getBodyModule()->getHealth()));
				duty.phase = RAID_HOME;
				duty.frame = now;
			}
			else if( ai->isIdle() || now - duty.frame > RAID_LEG_FRAMES )
				ai->aiMoveToPosition( &home, CMD_FROM_AI );
			break;
	}
	if( abort )
	{
		DEBUG_LOG(("AI RAID frame %d player %d '%s' %d aborts on '%s' %d, %s, at %.0f%% health\n", now, m_player->getPlayerIndex(),
			helix->getTemplate()->getName().str(), duty.id, targetGone ? "nothing" : target->getTemplate()->getName().str(), duty.target,
			abort, health * 100.0f));
		duty.phase = RAID_BACK;
		duty.frame = now;
	}
}

void AIPlayer::doHelixes( void )
{
	if( !runsOwnAI() || !m_baseCenterSet || m_skillLevel == AISKILL_EASY || measuringWithoutTactics() )
		return;
	if( (TheGameLogic->getFrame() + computeUpdatePhase( m_player->getPlayerIndex(), HELIX_CHECK_RATE )) % HELIX_CHECK_RATE != 0 )
		return;
	const UnsignedInt now = TheGameLogic->getFrame();
	for( Int slot = 0; slot < MAX_DUTY_HELIXES; ++slot )
	{
		DutyHelix &duty = m_dutyHelix[ slot ];
		if( duty.id == INVALID_ID )
			continue;
		const Object *helix = TheGameLogic->findObjectByID( duty.id );
		if( helix && !helix->isEffectivelyDead() && helix->getControllingPlayer() == m_player )
			continue;
		static const char *const raidPhases[] = { "at home", "on the way in", "on the bomb run", "on the way back" };
		static const char *const healPhases[] = { "at home", "over a hurt unit", "behind the wave", "pulling back" };
		DEBUG_LOG(("AI HELIX frame %d player %d loses its %s %d, %s\n", now, m_player->getPlayerIndex(), HELIX_ROLE_NAME[ duty.role ], duty.id,
			duty.role == HELIX_RAIDER ? raidPhases[ duty.phase ] : healPhases[ duty.phase ]));
		duty.id = INVALID_ID;
	}
	buyDutyHelix();

	AIEnemyComposition enemy;
	Bool enemyRead = FALSE;
	std::vector<Object *> owned;
	m_player->iterateObjects( collectOwned, &owned );
	for( size_t o = 0; o < owned.size(); ++o )
	{
		Object *helix = owned[ o ];
		if( !isHelicopter( helix ) || helix->getProductionUpdateInterface() == NULL )
			continue;
		Int role = -1;
		for( Int slot = 0; slot < MAX_DUTY_HELIXES; ++slot )
			if( m_dutyHelix[ slot ].id == helix->getID() )
				role = m_dutyHelix[ slot ].role;
		upgradeHelix( helix, role, &enemy, &enemyRead );
	}

	std::vector<AIKnownGun> guns;
	Bool gunsRead = FALSE;
	Int healers = 0;
	for( Int slot = 0; slot < MAX_DUTY_HELIXES; ++slot )
	{
		if( m_dutyHelix[ slot ].id == INVALID_ID )
			continue;
		if( !gunsRead )
		{
			collectKnownGuns( &guns );
			gunsRead = TRUE;
		}
		if( m_dutyHelix[ slot ].role == HELIX_HEALER )
		{
			++healers;
			steerHealer( slot, guns );
		}
		else
			flyRaid( slot, guns );
	}
	if( healers > 0 && (now / HELIX_CHECK_RATE) % (HEALER_REPORT_FRAMES / HELIX_CHECK_RATE) == 0 )
		DEBUG_LOG(("AI HEALER frame %d player %d has %d healers; out of the base %d of %d healer seconds clear of the nearest known gun's reach\n",
			now, m_player->getPlayerIndex(), healers, m_healerClearSeconds, m_healerSeconds));
}

//----------------------------------------------------------------------------------------------------------
/** The USA's Ambulance carries three infantry, heals them a quarter of their health a second, and
	* cleans toxins, anthrax and radiation off the ground.  The computer never built one.  Medium and up
	* now buy one once AMBULANCE_FIRST_ARMY fighting units stand and a second at AMBULANCE_SECOND_ARMY,
	* through the support-unit queue.  An Ambulance waits
	* AMBULANCE_BEHIND back from the middle of whoever is out, toward home and clear of every known gun,
	* or at home with nobody out.  The cash hoard does not hold it back, since a USA at war banks under
	* the hoard all match.  Our infantry within AMBULANCE_CALL_REACH of it, under half health and not hit lately,
	* walk over and climb in, and each is let out once it is whole.  It drives to the nearest hazard
	* on our side of the map that no known gun covers; one beside it it notices by itself
	* (CleanupHazardUpdate::startAutoCleanup).
	*
	* It is a 240-health truck with no gun.  Hit in the last AMBULANCE_HIT_FRAMES or within the margin of a
	* known gun's reach (AIAmbulance_gunMargin, scaled to the map), it drops whatever it was doing, the
	* cleanup included, and drives AMBULANCE_PULLBACK toward home to the first ground the margin clears,
	* but only when that ground is clearer than where it stands; otherwise it holds.  Under
	* AMBULANCE_REPAIR_HEALTH it goes to be repaired.
	* Before that it parked 150 from the middle of the army and kept cleaning the toxins of the fight it
	* stood in: 90 of 112 bought in eight 2v2 matches died, each bought again once the bank allowed.
	* A lost one is now replaced after AMBULANCE_REBUY_FRAMES.
	*
	* A patient leaves its team for the default one until it is let out, as a pulled-out veteran does
	* (pullOutRanked): left in its team, the team's next script order walked it away from the door, and
	* 23 of 93 called in were ever let out.
	*
	* Split out with plain values so the rules are testable without a running game. */
//----------------------------------------------------------------------------------------------------------
static const Int AMBULANCE_CHECK_RATE = LOGICFRAMES_PER_SECOND;
static const Int AMBULANCE_FIRST_ARMY = 8;
static const Int AMBULANCE_SECOND_ARMY = 20;
static const Real AMBULANCE_PATIENT_HEALTH = 0.5f;
static const Real AMBULANCE_CALL_REACH = 900.0f;
static const UnsignedInt AMBULANCE_PATIENT_QUIET = 2 * LOGICFRAMES_PER_SECOND;
static const Real AMBULANCE_BEHIND = 300.0f;
static const Real AMBULANCE_HAZARD_REACH = 1500.0f;
/** The cleanup ability's MaxMoveDistanceFromLocation in retail AmericaVehicle.ini. */
static const Real AMBULANCE_CLEANUP_RANGE = 300.0f;
static const Real AMBULANCE_REORDER_DISTANCE = 100.0f;
/** By rung (Easy, Medium, Hard; Easy fields none, its values keep the table whole).  How long after a
	* hit it stays back.  How far clear of every known gun it keeps: a share of the distance between the
	* two bases, held between a floor and a cap.  A fixed 160 or 200 broke on the small generated maps,
	* where its own base stood that close to known enemy guns: no ground was clear, and on 32 such matches
	* the Hard USA pulled back 312 times against 116, at full health and unhit for minutes, cycling between
	* its wait spot and its base.  On Twilight Flame the share reaches the cap. */
static const UnsignedInt AMBULANCE_HIT_FRAMES[ AISKILL_COUNT ] = { 4 * LOGICFRAMES_PER_SECOND, 6 * LOGICFRAMES_PER_SECOND, 8 * LOGICFRAMES_PER_SECOND };
static const Real AMBULANCE_MARGIN_SHARE[ AISKILL_COUNT ] = { 0.04f, 0.06f, 0.08f };
static const Real AMBULANCE_MARGIN_MIN[ AISKILL_COUNT ] = { 40.0f, 60.0f, 80.0f };
static const Real AMBULANCE_MARGIN_MAX[ AISKILL_COUNT ] = { 120.0f, 160.0f, 200.0f };
/** A spot it waits at stands this share of the margin farther clear again, so a gun a step closer does
	* not send it back the moment it gets there. */
static const Real AMBULANCE_WAIT_SLACK = 0.5f;
static const Real AMBULANCE_PULLBACK = 400.0f;
static const Real AMBULANCE_REPAIR_HEALTH = 0.5f;
static const UnsignedInt AMBULANCE_REBUY_FRAMES = 90 * LOGICFRAMES_PER_SECOND;
/** Medium keeps one at a time, waits twice as long to replace it and buys half as many in a match.
	* Each lost one was bought again on Hard every 90 seconds: eight came out of two USA seats in one
	* 16-minute match (ambA4_700), and a player counted dozens over a long game. */
static const Int AMBULANCE_MATCH_CAP[ AISKILL_COUNT ] = { 0, 3, 6 };

Int AIAmbulance_wanted( Int army, Int skill )
{
	if( skill == AISKILL_EASY )
		return 0;
	if( army >= AMBULANCE_SECOND_ARMY && skill >= AISKILL_BRUTAL )
		return 2;
	return army >= AMBULANCE_FIRST_ARMY ? 1 : 0;
}

Bool AIAmbulance_rebuyWaits( UnsignedInt now, UnsignedInt lost, Int skill )
{
	const UnsignedInt wait = skill >= AISKILL_BRUTAL ? AMBULANCE_REBUY_FRAMES : 2 * AMBULANCE_REBUY_FRAMES;
	return lost != 0 && now - lost < wait;
}

Bool AIAmbulance_mayBuyMore( Int bought, Int skill )
{
	return bought < AMBULANCE_MATCH_CAP[ skill ];
}

/** How far clear of every known gun it keeps, for bases baseDistance apart. */
Real AIAmbulance_gunMargin( Real baseDistance, Int skill )
{
	return clamp( AMBULANCE_MARGIN_MIN[ skill ], AMBULANCE_MARGIN_SHARE[ skill ] * baseDistance, AMBULANCE_MARGIN_MAX[ skill ] );
}

/** Hit lately, or a known gun within the margin.  clear is how far outside the reach of the gun that
	* reaches deepest it stands, below zero when inside it. */
Bool AIAmbulance_pullsBack( UnsignedInt now, UnsignedInt lastHit, Real clear, Real margin, Int skill )
{
	return clear < margin || (lastHit != 0 && now - lastHit < AMBULANCE_HIT_FRAMES[ skill ]);
}

/** Whether a pull-back moves it to ground clear by spotClear from where it stands clear by hereClear.
	* Only to ground clearer by a reorder step: on ground no clearer, it holds where it is, which is what
	* stops the cycle.  Hit since its last look by something it does not know of, it goes back regardless,
	* since the known guns cannot say where the fire came from. */
Bool AIAmbulance_movesBack( Real hereClear, Real spotClear, Bool hitSinceLook )
{
	return hitSinceLook || spotClear >= hereClear + AMBULANCE_REORDER_DISTANCE;
}

/** Whether it goes forward to a wait spot clear by spotClear: clear by the margin and the slack, or at
	* least clearer than where it stands; otherwise it stays put. */
Bool AIAmbulance_waitsAt( Real hereClear, Real spotClear, Real margin )
{
	return spotClear >= margin * (1.0f + AMBULANCE_WAIT_SLACK) || spotClear >= hereClear + AMBULANCE_REORDER_DISTANCE;
}

/** What becomes of each one called is in the AI PATIENT lines.  In one Hard and one Medium match (ambC,
	* 2026-10-11) 10 of 15 died on the way, every one within three seconds of the call, so in the fight
	* that hurt it; every one that lived to climb in was let out whole.  Leaving out whoever was hit in the
	* last four seconds called nobody at all in the same two matches.
	*
	* So one still being shot is not called, and one left alone for AMBULANCE_PATIENT_QUIET is, from twice
	* the old 450.  In six Hard USA mirrors on Twilight Flame (2026-10-11) every hurt soldier within 450 had
	* been hit in the last two seconds; the quiet ones, out of the fight, stood 450 to 1,500 away for ten
	* seconds and more, which is the walk back toward an Ambulance waiting behind the army. */
Bool AIAmbulance_callsPatient( Real healthFraction, Real distance, UnsignedInt sinceHit )
{
	return healthFraction < AMBULANCE_PATIENT_HEALTH && distance <= AMBULANCE_CALL_REACH && sinceHit >= AMBULANCE_PATIENT_QUIET;
}

/** A vehicle with a hold that cleans hazards: the Ambulance, and each general's copy of it. */
static Bool isAmbulanceTemplate( const ThingTemplate *tmpl )
{
	if( !tmpl->isKindOf( KINDOF_VEHICLE ) || transportContainOf( tmpl ) == NULL )
		return FALSE;
	const ModuleInfo &modules = tmpl->getBehaviorModuleInfo();
	for( Int m = 0; m < modules.getCount(); ++m )
		if( modules.getNthName( m ).compare( "CleanupHazardUpdate" ) == 0 )
			return TRUE;
	return FALSE;
}

Bool AIPlayer::isDutyAmbulance( const Object *obj ) const
{
	for( Int slot = 0; slot < MAX_DUTY_AMBULANCES; ++slot )
		if( m_ambulance[ slot ].id == obj->getID() )
			return TRUE;
	return FALSE;
}

Bool AIPlayer::isWalkingToAmbulance( const Object *obj ) const
{
	AIUpdateInterface *ai = const_cast<Object *>( obj )->getAI();		// getGoalObject is not const
	return ai && ai->getCurrentStateID() == AI_ENTER && ai->getGoalObject() && isDutyAmbulance( ai->getGoalObject() );
}

Bool AIPlayer::isOutToMend( const Object *obj ) const
{
	const AIUpdateInterface *ai = obj->getAI();
	return ai && !ai->isRecruitable() && obj->getTeam() == m_player->getDefaultTeam();
}

/** For the log only: what became of each patient an Ambulance called in.  It climbs in, then is let out
	* or dies inside; or it dies on the way; or something else gives it an order first and it walks off. */
void AIPlayer::followPatients( void )
{
	const UnsignedInt now = TheGameLogic->getFrame();
	for( size_t p = 0; p < m_calledPatients.size(); )
	{
		CalledPatient &patient = m_calledPatients[ p ];
		Object *obj = TheGameLogic->findObjectByID( patient.id );
		const Object *ambulance = TheGameLogic->findObjectByID( patient.ambulance );
		const Bool ambulanceUp = ambulance && !ambulance->isEffectivelyDead();
		const char *outcome = NULL;
		Real health = 0.0f;
		Real away = 0.0f;
		if( obj && !obj->isEffectivelyDead() )
		{
			health = obj->getBodyModule()->getHealth() / obj->getBodyModule()->getMaxHealth();
			if( ambulance )
				away = (Real)sqrt( sqr( obj->getPosition()->x - ambulance->getPosition()->x ) + sqr( obj->getPosition()->y - ambulance->getPosition()->y ) );
		}
		if( obj == NULL || obj->isEffectivelyDead() )
			outcome = patient.inside ? (ambulanceUp ? "dies inside" : "dies with the ambulance") : "dies on the way";
		else if( obj->isContained() )
		{
			if( !patient.inside )
			{
				patient.inside = TRUE;
				DEBUG_LOG(("AI PATIENT frame %d player %d '%s' %d climbs in after %d s at %.0f%% health\n", now, m_player->getPlayerIndex(),
					obj->getTemplate()->getName().str(), patient.id, (now - patient.frame) / LOGICFRAMES_PER_SECOND, health * 100.0f));
			}
		}
		else if( patient.inside )
			outcome = ambulanceUp ? "is let out" : "is let out by the wreck";
		else if( obj->getAI()->getCurrentStateID() != AI_ENTER || obj->getAI()->getGoalObject() != ambulance )
			outcome = ambulanceUp ? "walks off" : "walks off, the ambulance lost";
		if( outcome == NULL )
		{
			++p;
			continue;
		}
		DEBUG_LOG(("AI PATIENT frame %d player %d %d %s after %d s at %.0f%% health, %.0f from the ambulance; called at %.0f%% health from %.0f\n", now,
			m_player->getPlayerIndex(), patient.id, outcome, (now - patient.frame) / LOGICFRAMES_PER_SECOND, health * 100.0f, away,
			patient.health * 100.0f, patient.distance));
		if( obj && !obj->isEffectivelyDead() && !patient.inside )
		{
			const Object *goal = obj->getAI()->getGoalObject();
			DEBUG_LOG(("AI PATIENT frame %d player %d %d walk-off state %d source %d goal %d team '%s' recruitable %d hit %d s ago; ambulance state %d moving %d\n", now,
				m_player->getPlayerIndex(), patient.id, (Int)obj->getAI()->getCurrentStateID(), (Int)obj->getAI()->getLastCommandSource(),
				goal ? (Int)goal->getID() : 0, obj->getTeam() ? obj->getTeam()->getName().str() : "", (Int)obj->getAI()->isRecruitable(),
				(now - obj->getBodyModule()->getLastDamageTimestamp()) / LOGICFRAMES_PER_SECOND,
				ambulanceUp ? (Int)ambulance->getAI()->getCurrentStateID() : -1, ambulanceUp ? (Int)ambulance->getAI()->isMoving() : -1));
		}
		m_calledPatients.erase( m_calledPatients.begin() + p );
	}
}

void AIPlayer::doAmbulances( void )
{
	if( !runsOwnAI() || !m_baseCenterSet || m_skillLevel == AISKILL_EASY || measuringWithoutTactics() )
		return;
	if( (TheGameLogic->getFrame() + computeUpdatePhase( m_player->getPlayerIndex(), AMBULANCE_CHECK_RATE )) % AMBULANCE_CHECK_RATE != 0 )
		return;
	Int owned = 0;
	for( Int slot = 0; slot < MAX_DUTY_AMBULANCES; ++slot )
	{
		DutyAmbulance &duty = m_ambulance[ slot ];
		if( duty.id == INVALID_ID )
			continue;
		const Object *ambulance = TheGameLogic->findObjectByID( duty.id );
		if( ambulance && !ambulance->isEffectivelyDead() && ambulance->getControllingPlayer() == m_player )
		{
			++owned;
			continue;
		}
		DEBUG_LOG(("AI AMBULANCE frame %d player %d loses %d at (%.0f,%.0f), %.0f from home, last seen at %.0f%% health %s a known gun's reach by %.0f, last sent to (%.0f,%.0f), %d pull-backs %d holds\n",
			TheGameLogic->getFrame(), m_player->getPlayerIndex(), duty.id, duty.seen.x, duty.seen.y,
			(Real)sqrt( sqr( duty.seen.x - m_baseCenter.x ) + sqr( duty.seen.y - m_baseCenter.y ) ), duty.seenHealth * 100.0f,
			duty.seenReach > 0.0f ? "inside" : "clear of", fabs( duty.seenReach ), duty.spot.x, duty.spot.y, duty.pullbacks, duty.holds));
		duty.id = INVALID_ID;
		m_ambulanceLost = TheGameLogic->getFrame();
	}
	releaseMended();
	followPatients();

	const Int wanted = AIAmbulance_wanted( tallyBase( m_player ).army, m_skillLevel );
	// no cash hoard to clear: an Ambulance is 600, and a USA at war banks under the hoard all match long.
	// queueSupportUnit still waits for twice its price
	if( owned < wanted && AIAmbulance_mayBuyMore( m_ambulancesBought, m_skillLevel ) &&
			!AIAmbulance_rebuyWaits( TheGameLogic->getFrame(), m_ambulanceLost, m_skillLevel ) && !isBaseUnderAttack() && !scoutInQueue() )
	{
		std::vector<Object *> factories;
		m_player->iterateObjects( collectFactories, &factories );
		Bool ordered = FALSE;
		for( size_t f = 0; f < factories.size() && !ordered; ++f )
		{
			const CommandSet *commandSet = TheCommandStore->findCommandSet( factories[ f ]->getCommandSetString() );
			if( commandSet == NULL )
				continue;
			for( Int i = 0; i < MAX_COMMANDS_PER_SET && !ordered; ++i )
			{
				const CommandButton *button = commandSet->getCommandButton( i );
				if( button == NULL || button->getCommandType() != GUI_COMMAND_UNIT_BUILD || button->getThingTemplate() == NULL ||
						!isAmbulanceTemplate( button->getThingTemplate() ) || TheBuildAssistant->canMakeUnit( factories[ f ], button->getThingTemplate() ) != CANMAKE_OK )
					continue;
				queueSupportUnit( button->getThingTemplate(), "AMBULANCE" );
				ordered = TRUE;
				if( scoutInQueue() )
					DEBUG_LOG(("AI AMBULANCE frame %d player %d orders '%s', %d owned of %d wanted, %d in the bank\n", TheGameLogic->getFrame(),
						m_player->getPlayerIndex(), button->getThingTemplate()->getName().str(), owned, wanted, m_player->getMoney()->countMoney()));
			}
		}
	}

	std::vector<AIKnownGun> guns;
	Bool gunsRead = FALSE;
	for( Int slot = 0; slot < MAX_DUTY_AMBULANCES; ++slot )
	{
		if( m_ambulance[ slot ].id == INVALID_ID )
			continue;
		if( !gunsRead )
		{
			collectKnownGuns( &guns );
			gunsRead = TRUE;
		}
		steerAmbulance( slot, guns );
	}
}

static Object *findMender( Player *player, Object *unit );

void AIPlayer::steerAmbulance( Int slot, const std::vector<AIKnownGun> &guns )
{
	DutyAmbulance &duty = m_ambulance[ slot ];
	Object *ambulance = TheGameLogic->findObjectByID( duty.id );
	static NameKeyType key_CleanupHazardUpdate = NAMEKEY( "CleanupHazardUpdate" );
	CleanupHazardUpdate *cleaner = (CleanupHazardUpdate *)ambulance->findUpdateModule( key_CleanupHazardUpdate );
	if( ambulance->isContained() )
		return;
	AIUpdateInterface *ai = ambulance->getAI();
	ContainModuleInterface *hold = ambulance->getContain();
	const Coord3D *pos = ambulance->getPosition();
	const UnsignedInt now = TheGameLogic->getFrame();
	const Real reach = deepestReach( guns, pos->x, pos->y, FALSE );
	duty.seen = *pos;
	duty.seenHealth = ambulance->getBodyModule()->getHealth() / ambulance->getBodyModule()->getMaxHealth();
	duty.seenReach = reach;

	// each one whole again gets out; one still hurt stays in and goes on healing
	const ContainedItemsList *riders = hold->getContainedItemsList();
	for( ContainedItemsList::const_iterator r = riders->begin(); r != riders->end(); ++r )
	{
		Object *rider = *r;
		if( rider->getBodyModule()->getHealth() < rider->getBodyModule()->getMaxHealth() || rider->getAI() == NULL ||
				rider->getAI()->getCurrentStateID() == AI_EXIT )
			continue;
		DEBUG_LOG(("AI AMBULANCE frame %d player %d '%s' %d lets 1 out whole, '%s' %d, at (%.0f,%.0f)\n", now, m_player->getPlayerIndex(),
			ambulance->getTemplate()->getName().str(), duty.id, rider->getTemplate()->getName().str(), rider->getID(), pos->x, pos->y));
		rider->getAI()->aiExit( ambulance, CMD_FROM_AI );
	}

	const StateID state = ai->getCurrentStateID();
	const BodyModuleInterface *body = ambulance->getBodyModule();

	// shot at, or inside a gun's reach: back toward home, out of every known gun's reach, whatever it was
	// doing, a trip to be repaired included.  Sent to be repaired first, it drove the pathfinder's way to
	// the War Factory, through whatever reached that way: 20 of 31 lost in eight matches (ambA4) died on
	// that trip, most within a few seconds of the order and inside a known gun's reach
	//
	// How clear it keeps scales with the map (AIAmbulance_gunMargin), and it only ever moves to clearer
	// ground: with no clearer ground to go to it holds where it is, and it goes forward again only to a
	// spot clear by the margin and a slack, or clearer than where it stands (AIAmbulance_waitsAt)
	Player *enemy = getAiEnemy();
	Coord3D enemyPos;
	const Bool enemyPlaced = enemy && enemyStartGuess( enemy->getPlayerIndex(), &enemyPos );
	const Real margin = enemyPlaced ?
		AIAmbulance_gunMargin( (Real)sqrt( sqr( enemyPos.x - m_baseCenter.x ) + sqr( enemyPos.y - m_baseCenter.y ) ), m_skillLevel ) :
		AMBULANCE_MARGIN_MAX[ m_skillLevel ];
	const Real spotMargin = margin * (1.0f + AMBULANCE_WAIT_SLACK);
	const Real hereClear = -reach;
	const UnsignedInt lastHit = body->getLastDamageTimestamp();
	if( AIAmbulance_pullsBack( now, lastHit, hereClear, margin, m_skillLevel ) )
	{
		const Bool hitSinceLook = lastHit != 0 && now - lastHit < (UnsignedInt)AMBULANCE_CHECK_RATE;
		const Real sentClear = -deepestReach( guns, duty.spot.x, duty.spot.y, FALSE );
		// on its way to clearer ground already, a repair trip included, and not hit since: it keeps going
		if( !hitSinceLook && !cleaner->isCleaningArea() && ai->isMoving() && AIAmbulance_movesBack( hereClear, sentClear, FALSE ) )
			return;
		Coord3D back = *pos;
		const Real dx = m_baseCenter.x - pos->x;
		const Real dy = m_baseCenter.y - pos->y;
		const Real length = (Real)sqrt( dx * dx + dy * dy );
		if( length > AMBULANCE_PULLBACK )
		{
			back.x += dx * AMBULANCE_PULLBACK / length;
			back.y += dy * AMBULANCE_PULLBACK / length;
		}
		else
			back = m_baseCenter;
		Coord3D spot = rearOf( &back, guns, spotMargin );
		const Real spotClear = -deepestReach( guns, spot.x, spot.y, FALSE );
		if( !AIAmbulance_movesBack( hereClear, spotClear, hitSinceLook ) )
		{
			// no clearer ground than this: it holds here.  Driving on into worse, or cleaning, it stops
			++duty.holds;
			if( cleaner->isCleaningArea() || (ai->isMoving() && !AIAmbulance_movesBack( hereClear, sentClear, FALSE )) )
			{
				DEBUG_LOG(("AI AMBULANCE frame %d player %d '%s' %d holds at (%.0f,%.0f), %.0f clear against a margin of %.0f, no clearer ground; %d holds\n",
					now, m_player->getPlayerIndex(), ambulance->getTemplate()->getName().str(), duty.id, pos->x, pos->y, hereClear, margin, duty.holds));
				if( cleaner->isCleaningArea() )
					cleaner->giveUpArea();
				const Coord3D stop = *pos;
				ai->aiMoveToPosition( &stop, CMD_FROM_SCRIPT );
				duty.spot = stop;
			}
			return;
		}
		++duty.pullbacks;
		DEBUG_LOG(("AI AMBULANCE frame %d player %d '%s' %d pulls back to (%.0f,%.0f) at %.0f%% health, %.0f clear against a margin of %.0f, to %.0f clear, hit %d s ago%s; %d pull-backs\n",
			now, m_player->getPlayerIndex(), ambulance->getTemplate()->getName().str(), duty.id, spot.x, spot.y,
			body->getHealth() * 100.0f / body->getMaxHealth(), hereClear, margin, spotClear,
			lastHit != 0 ? (Int)((now - lastHit) / LOGICFRAMES_PER_SECOND) : -1, cleaner->isCleaningArea() ? ", off its cleanup" : "", duty.pullbacks));
		// pulled off a cleanup, it leaves that ground alone from now on: the spill it was sent to was
		// picked again the next look, and the cleanup drove it back after the same hazard under the guns
		if( cleaner->isCleaningArea() )
			cleaner->giveUpArea();
		ai->aiMoveToPosition( &spot, CMD_FROM_SCRIPT );		// not CMD_FROM_AI, which a cleanup under way would keep
		duty.spot = spot;
		return;
	}

	// badly hurt, and out of reach now: to be repaired, riders and all, who go on healing inside
	if( body->getHealth() < AMBULANCE_REPAIR_HEALTH * body->getMaxHealth() )
	{
		Object *mender = findMender( m_player, ambulance );
		if( mender && TheActionManager->canGetRepairedAt( ambulance, mender, CMD_FROM_AI ) )
		{
			if( state != AI_DOCK && state != AI_ENTER && state < 1000 )
			{
				DEBUG_LOG(("AI AMBULANCE frame %d player %d '%s' %d goes to be repaired at '%s' %d, %.0f%% health\n", now, m_player->getPlayerIndex(),
					ambulance->getTemplate()->getName().str(), duty.id, mender->getTemplate()->getName().str(), mender->getID(),
					body->getHealth() * 100.0f / body->getMaxHealth()));
				ai->aiGetRepaired( mender, CMD_FROM_SCRIPT );		// not CMD_FROM_AI, which a cleanup under way would keep
				duty.spot = *mender->getPosition();
			}
			return;
		}
	}

	if( cleaner->isCleaningArea() || state == AI_DOCK )
		return;		// its own module drives it until the ground is clean and brings it back; or it is being repaired

	// the hurt nearby climb in, while there is room, and the middle of whoever is out
	BoardingSearch boarding;
	boarding.transport = ambulance;
	boarding.found = FALSE;
	m_player->iterateObjects( findBoarder, &boarding );
	Int room = hold->getContainMax() - (Int)hold->getContainCount() - hold->getExtraSlotsInUse();
	Int called = 0;
	std::vector<Object *> owned;
	m_player->iterateObjects( collectOwned, &owned );
	Coord3D middle;
	middle.zero();
	Int out = 0;
	for( size_t o = 0; o < owned.size(); ++o )
	{
		Object *obj = owned[ o ];
		if( obj->isEffectivelyDead() || obj->isContained() || obj->getAI() == NULL || obj->isKindOf( KINDOF_STRUCTURE ) ||
				obj->isKindOf( KINDOF_AIRCRAFT ) || isDutyAmbulance( obj ) )
			continue;
		if( (obj->isKindOf( KINDOF_INFANTRY ) || obj->isKindOf( KINDOF_VEHICLE )) && aiCombatPower( obj ) > 0.0f && !isAtHome( obj->getPosition() ) )
		{
			middle.x += obj->getPosition()->x;
			middle.y += obj->getPosition()->y;
			++out;
		}
		const StateID state = obj->getAI()->getCurrentStateID();
		if( state == AI_ENTER && obj->getAI()->getGoalObject() == ambulance )
			--room;		// a seat already taken by one on its way
		// not a burning man running about, which is infantry with no gun and dies in seconds
		if( room <= 0 || !obj->isKindOf( KINDOF_INFANTRY ) || aiCombatPower( obj ) <= 0.0f )
			continue;
		const BodyModuleInterface *body = obj->getBodyModule();
		const Real distance = (Real)sqrt( sqr( obj->getPosition()->x - pos->x ) + sqr( obj->getPosition()->y - pos->y ) );
		if( state == AI_ENTER || state == AI_DOCK || state >= 1000 || !hold->isValidContainerFor( obj, TRUE ) )
			continue;
		if( !AIAmbulance_callsPatient( body->getHealth() / body->getMaxHealth(), distance, now - body->getLastDamageTimestamp() ) )
			continue;
		leaveTacticsAlone( obj->getID() );
		// out of its team until it is let out, or the team's next script order walks it off (see releaseMended)
		obj->setTeam( m_player->getDefaultTeam() );
		obj->getAI()->setIsRecruitable( FALSE );
		obj->getAI()->aiEnter( ambulance, CMD_FROM_AI );
		--room;
		++called;
		CalledPatient patient;
		patient.id = obj->getID();
		patient.ambulance = duty.id;
		patient.frame = now;
		patient.health = body->getHealth() / body->getMaxHealth();
		patient.distance = distance;
		patient.inside = FALSE;
		m_calledPatients.push_back( patient );
		DEBUG_LOG(("AI AMBULANCE frame %d player %d '%s' %d calls in '%s' %d at %.0f%% health, %.0f away\n", now, m_player->getPlayerIndex(),
			ambulance->getTemplate()->getName().str(), duty.id, obj->getTemplate()->getName().str(), obj->getID(),
			body->getHealth() * 100.0f / body->getMaxHealth(), distance));
	}
	if( called > 0 || boarding.found )
		return;		// no new spot while they walk over

	// the nearest hazard, if it is on our side and clear of every known gun's reach by the healer's margin,
	// and not on ground it gave up on
	Object *hazard = cleaner->closestHazard( pos, AMBULANCE_HAZARD_REACH );
	// and no farther from home than where it waits behind the army, or at home with nobody out.  Three of
	// four lost in one Hard match (ambD) died on or after a cleanup 1,400 out, 2,100 to 2,800 from home
	Real hazardLimit = 0.0f;
	if( out > 0 )
		hazardLimit = (Real)sqrt( sqr( middle.x / out - m_baseCenter.x ) + sqr( middle.y / out - m_baseCenter.y ) ) - AMBULANCE_BEHIND;
	if( hazard && isOurSideOfMap( hazard->getPosition() ) &&
			(isAtHome( hazard->getPosition() ) ||
				sqr( hazard->getPosition()->x - m_baseCenter.x ) + sqr( hazard->getPosition()->y - m_baseCenter.y ) <= sqr( max( hazardLimit, 0.0f ) )) &&
			deepestReach( guns, hazard->getPosition()->x, hazard->getPosition()->y, FALSE ) < -spotMargin )
	{
		DEBUG_LOG(("AI AMBULANCE frame %d player %d '%s' %d goes to clean '%s' %d at (%.0f,%.0f), %.0f away\n", now, m_player->getPlayerIndex(),
			ambulance->getTemplate()->getName().str(), duty.id, hazard->getTemplate()->getName().str(), hazard->getID(),
			hazard->getPosition()->x, hazard->getPosition()->y,
			(Real)sqrt( sqr( hazard->getPosition()->x - pos->x ) + sqr( hazard->getPosition()->y - pos->y ) )));
		cleaner->setCleanupAreaParameters( hazard->getPosition(), AMBULANCE_CLEANUP_RANGE, FALSE );
		duty.spot = *hazard->getPosition();
		return;
	}

	// behind whoever is out, or home
	Coord3D spot = m_baseCenter;
	if( out > 0 )
	{
		middle.x /= out;
		middle.y /= out;
		const Real dx = m_baseCenter.x - middle.x;
		const Real dy = m_baseCenter.y - middle.y;
		const Real length = (Real)sqrt( dx * dx + dy * dy );
		if( length > AMBULANCE_BEHIND )
		{
			middle.x += dx * AMBULANCE_BEHIND / length;
			middle.y += dy * AMBULANCE_BEHIND / length;
			spot = rearOf( &middle, guns, spotMargin );
		}
	}
	else if( isAtHome( pos ) )
		spot = *pos;
	spot.z = TheTerrainLogic->getGroundHeight( spot.x, spot.y );
	const Bool moved = sqr( spot.x - duty.spot.x ) + sqr( spot.y - duty.spot.y ) > sqr( AMBULANCE_REORDER_DISTANCE );
	const Bool away = sqr( spot.x - pos->x ) + sqr( spot.y - pos->y ) > sqr( AMBULANCE_REORDER_DISTANCE );
	if( !moved && !(ai->isIdle() && away) )
		return;
	// not forward onto ground the margin does not clear: rearOf falls back to the base center when nothing
	// is clear, and on a small map that sent it back and forth between there and its pull-back spot
	if( !AIAmbulance_waitsAt( hereClear, -deepestReach( guns, spot.x, spot.y, FALSE ), margin ) )
		return;
	if( moved )
		DEBUG_LOG(("AI AMBULANCE frame %d player %d '%s' %d waits at (%.0f,%.0f), %d units out\n", now, m_player->getPlayerIndex(),
			ambulance->getTemplate()->getName().str(), duty.id, spot.x, spot.y, out));
	ai->aiMoveToPosition( &spot, CMD_FROM_AI );
	duty.spot = spot;
}

//----------------------------------------------------------------------------------------------------------
/** How far around a fight to look for what is in it.  Wide enough to catch the base defences and
	* the second rank shooting into it, not so wide that a skirmish at the front counts the garrison
	* at the back as part of the same exchange. */
static const Real RETREAT_ENGAGEMENT_RADIUS = 300.0f;

/** How long a unit sent home from a lost fight is left to walk there before a fight it walks into
	* may pull it out again.  Pulled out on the same pass that sent it home, a team that gave up stood
	* through a second full hold beside the fight it had given up on. */
static const UnsignedInt RETREAT_HOME_WALK_FRAMES = 30 * LOGICFRAMES_PER_SECOND;

static Real groundAttackRange( const Object *obj );

/** A unit's contribution to an exchange: what it can still take, and what it can still deal.  The
	* threat value the data already carries stands in for damage per second - it is what
	* AI_threatScore uses for the same purpose. */
static void addToForce( const Object *obj, Real *health, Real *power )
{
	if( obj == NULL || obj->isEffectivelyDead() )
		return;

	const Real power2 = aiCombatPower( obj );
	if( power2 <= 0.0f )
		return;			// it is not in the exchange, so neither its health nor its damage counts

	const BodyModuleInterface *body = obj->getBodyModule();
	if( body )
		*health += body->getHealth();
	*power += power2;
}

//----------------------------------------------------------------------------------------------------------
/** Who a "fall back to the base" order can be handed to.
	*
	* Not aircraft.  A jet or a helicopter flies its own round trip - take off, spend the load, land
	* to reload and heal - and a move order is what starts that cycle.  Handing a parked aircraft one
	* every decision interval had it take off, fly at the base centre, go idle, land, and take off
	* again on the next tick, for as long as a fight near the base kept reading as lost: the
	* airfield emptied and filled every few seconds and nothing ever reached the enemy.  Everything
	* that sends an aircraft home is already in JetAIUpdate - out of ammo, out of special ammo, idle
	* for ReturnToBaseIdleTime - so the retreat has nothing to add and no business interrupting it.
	*
	* Split out with plain flags so the rule is testable without a running game.
	*/
Bool AIRetreat_canBeOrderedHome( Bool hasAI, Bool isStructureOrImmobile, Bool ownsItsOwnLanding )
{
	return hasAI && !isStructureOrImmobile && !ownsItsOwnLanding;
}

/** The same rule, asked of a real object. */
static Bool retreatCanOrderHome( const Object *obj )
{
	if( obj == NULL )
		return FALSE;
	const AIUpdateInterface *ai = obj->getAI();
	return AIRetreat_canBeOrderedHome( ai != NULL,
			obj->isKindOf( KINDOF_STRUCTURE ) || obj->isKindOf( KINDOF_IMMOBILE ),
			ai != NULL && ai->getJetAIUpdate() != NULL );
}

//----------------------------------------------------------------------------------------------------------
/** A ranked unit is worth keeping.  It hits harder and takes more, and it took a match of fighting to
	* earn: a new one off the line is a rookie again.  So it leaves a fight on its own health, whatever
	* its team does, below RANK_HEALTH_BASE plus RANK_HEALTH_STEP a rank (35% for a veteran, 55% for a
	* heroic), and leaves a fight its team is losing before the team does, at the rung's ratio raised by
	* RANK_RATIO_STEP a rank and never above an even fight.  A regular soldier is the retreat's as before.
	*
	* Split out with plain values so the rule is testable without a running game. */
static const Real RANK_HEALTH_BASE = 0.25f;
static const Real RANK_HEALTH_STEP = 0.1f;
static const Real RANK_RATIO_STEP = 0.25f;
/** A losing fight pulls only one that has been hurt in it.  Colonel Burton was walked out of one at full
	* health, and a whole one has nothing to mend: canGetRepairedAt and canGetHealedAt refuse it. */
static const Real RANK_UNHURT = 0.9f;
/** How near a unit's own goal has to be to where pullOutRanked would send it to count as on its way. */
static const Real RANK_GOAL_SLACK = 50.0f;

Bool AIRetreat_rankPullsOut( Int rank, Real healthFraction, Bool inFight, Real ratio, Real retreatRatio )
{
	if( rank <= 0 )
		return FALSE;
	if( healthFraction < RANK_HEALTH_BASE + RANK_HEALTH_STEP * rank )
		return TRUE;
	return inFight && healthFraction < RANK_UNHURT && ratio < min( retreatRatio * (1.0f + RANK_RATIO_STEP * rank), 1.0f );
}

/** The nearest of our own buildings that mends this unit - a repair pad for a vehicle, a heal pad for
	* infantry - or NULL.  Ties go to the first in the object list. */
static Object *findMender( Player *player, Object *unit )
{
	std::vector<Object *> owned;
	player->iterateObjects( collectOwned, &owned );
	Object *best = NULL;
	Real bestSqr = 0.0f;
	for( std::vector<Object *>::const_iterator o = owned.begin(); o != owned.end(); ++o )
	{
		if( !(*o)->isKindOf( KINDOF_STRUCTURE ) ||
				!(TheActionManager->canGetRepairedAt( unit, *o, CMD_FROM_AI ) || TheActionManager->canGetHealedAt( unit, *o, CMD_FROM_AI )) )
			continue;
		const Real distSqr = sqr( (*o)->getPosition()->x - unit->getPosition()->x ) + sqr( (*o)->getPosition()->y - unit->getPosition()->y );
		if( best == NULL || distSqr < bestSqr )
		{
			best = *o;
			bestSqr = distSqr;
		}
	}
	return best;
}

/** The ranked members of a team that AIRetreat_rankPullsOut says go, to the nearest building that mends
	* them, or home when there is none.  Marked as a walk home, so neither this nor the team's retreat
	* turns them round for RETREAT_HOME_WALK_FRAMES.
	*
	* A pulled unit leaves its team for the default one and is kept from being recruited until it stands
	* idle again, mended or home.  Left in its team, the team's next script order (a waypoint path, an
	* attack move) took it off the repair pad on the way: one Tomahawk was sent to its War Factory at 4%,
	* 2% and 2% health thirty seconds apart and never reached it.  Once idle, any team recruits it back. */
void AIPlayer::pullOutRanked( Team *team, Bool inFight, Real ratio )
{
	const UnsignedInt now = TheGameLogic->getFrame();
	std::vector<Object *> pulled;
	for( DLINK_ITERATOR<Object> objIter = team->iterate_TeamMemberList(); !objIter.done(); objIter.advance() )
	{
		Object *obj = objIter.cur();
		const Int rank = (Int)obj->getVeterancyLevel();
		if( rank <= LEVEL_REGULAR || obj->isEffectivelyDead() || obj->isContained() || !retreatCanOrderHome( obj ) ||
				obj->getBodyModule() == NULL || obj->getAI()->hasTunnelTrip() )
			continue;
		const TacticalStep *row = findTacticalStep( obj->getID() );
		if( row && (row->fallingBack || (row->fallbackFrame != 0 && now - row->fallbackFrame < RETREAT_HOME_WALK_FRAMES)) )
			continue;
		// one already out stays out until releaseMended frees it.  Out of its team it is in the default
		// one, which doRetreats walks too, and every RETREAT_HOME_WALK_FRAMES it was sent again from
		// wherever it stood: an Angry Mob member that its mob kept walking was pulled 8 times, every 30 s
		if( isOutToMend( obj ) )
			continue;
		// one already on its way into a dock or a building is being mended, or will be.  So is one in a state
		// of its own module's (1000 and up): a Helix sent to its airfield flies there in ChinookAIUpdate's
		// states, and was sent again on every pass of a 3,600-foot flight
		const StateID state = obj->getAI()->getCurrentStateID();
		if( state == AI_DOCK || state == AI_ENTER || state >= 1000 )
			continue;
		const Real health = obj->getBodyModule()->getHealth() / obj->getBodyModule()->getMaxHealth();
		if( !AIRetreat_rankPullsOut( rank, health, inFight, ratio, getSkillProfile()->m_retreatTtkRatio ) )
			continue;
		Object *mender = findMender( m_player, obj );
		if( mender == NULL && isAtHome( obj->getPosition() ) )
			continue;		// home already, and nothing there mends it
		// one already walking there from an earlier pass whose row has since gone
		const Coord3D *to = mender ? mender->getPosition() : &m_baseCenter;
		const Coord3D *goal = obj->getAI()->getGoalPosition();
		if( obj->getAI()->isMoving() && goal && sqr( goal->x - to->x ) + sqr( goal->y - to->y ) < sqr( RANK_GOAL_SLACK ) )
			continue;
		leaveTacticsAlone( obj->getID() );
		TacticalStep *step = tacticalStepFor( obj->getID() );
		step->fallingBack = FALSE;
		step->fallbackFrom = *obj->getPosition();
		step->fallbackFrame = now;
		stepCalmly( obj, step, to );
		if( mender && TheActionManager->canGetRepairedAt( obj, mender, CMD_FROM_AI ) )
			obj->getAI()->aiGetRepaired( mender, CMD_FROM_SCRIPT );
		else if( mender )
			obj->getAI()->aiGetHealed( mender, CMD_FROM_SCRIPT );
		DEBUG_LOG(("AI VETERAN frame %d player %d pulls '%s' %d rank %d out at %.0f%% health, fight %.2f%s, state %d, %.0f from %s, now state %d\n", now,
			m_player->getPlayerIndex(), obj->getTemplate()->getName().str(), obj->getID(), rank, health * 100.0f,
			inFight ? ratio : 0.0f, inFight ? "" : " (none)", (Int)state,
			(Real)sqrt( sqr( to->x - obj->getPosition()->x ) + sqr( to->y - obj->getPosition()->y ) ),
			mender ? mender->getTemplate()->getName().str() : "base", (Int)obj->getAI()->getCurrentStateID()));
		pulled.push_back( obj );
	}
	// out of the member walk above, which setTeam would break
	for( std::vector<Object *>::const_iterator p = pulled.begin(); p != pulled.end(); ++p )
	{
		(*p)->setTeam( m_player->getDefaultTeam() );
		(*p)->getAI()->setIsRecruitable( FALSE );
	}
}

/** Pulled-out ranked units and Ambulance patients that stand idle again, mended, home or let out, go back
	* to being recruitable.  A patient still inside stays out of reach.
	* ponytail: any ranked or infantry default-team unit not recruitable counts as one of ours; a script that
	* marks such a unit unrecruitable would see it undone. */
void AIPlayer::releaseMended( void )
{
	for( DLINK_ITERATOR<Object> m = m_player->getDefaultTeam()->iterate_TeamMemberList(); !m.done(); m.advance() )
	{
		Object *obj = m.cur();
		AIUpdateInterface *ai = obj->getAI();
		if( ai == NULL || ai->isRecruitable() || obj->isEffectivelyDead() || obj->isContained() ||
				(obj->getVeterancyLevel() <= LEVEL_REGULAR && !obj->isKindOf( KINDOF_INFANTRY )) )
			continue;
		const StateID state = ai->getCurrentStateID();
		if( state == AI_DOCK || state == AI_ENTER || state >= 1000 || ai->isMoving() )
			continue;
		ai->setIsRecruitable( TRUE );
		DEBUG_LOG(("AI VETERAN frame %d player %d frees '%s' %d at %.0f%% health\n", TheGameLogic->getFrame(), m_player->getPlayerIndex(),
			obj->getTemplate()->getName().str(), obj->getID(), obj->getBodyModule()->getHealth() / obj->getBodyModule()->getMaxHealth() * 100.0f));
	}
}

//----------------------------------------------------------------------------------------------------------
/** Look at every fight this AI is in and break off the ones it is losing.
	*
	* Deliberately at the player level and on the rung's decision interval rather than inside the
	* unit update: it is one partition query per engaged team every few seconds, not a judgement
	* every unit makes every frame, and it keeps the whole of "when do we quit" in one readable
	* place.
	*/
//----------------------------------------------------------------------------------------------------------
void AIPlayer::doRetreats( void )
{
	const AIDifficultyProfile *profile = getSkillProfile();
	if( profile->m_retreatTtkRatio <= 0.0f )
		return;			// the bottom rung does not know how to quit, on purpose

	if( --m_retreatTimer > 0 )
		return;
	m_retreatTimer = REAL_TO_INT_CEIL( profile->m_decisionIntervalSeconds * LOGICFRAMES_PER_SECOND );
	if( m_retreatTimer < 1 )
		m_retreatTimer = 1;

	releaseMended();

	if( !m_baseCenterSet )
		return;			// nowhere to fall back to

	for( Player::PlayerTeamList::const_iterator t = m_player->getPlayerTeams()->begin();
			 t != m_player->getPlayerTeams()->end(); ++t )
	{
		const TeamTemplateInfo *info = (*t)->getTemplateInfo();
		if( info && (info->m_isBaseDefense || info->m_isPerimeterDefense) )
			continue;			// a base defence team that falls back has abandoned the thing it defends

		for( DLINK_ITERATOR<Team> iter = (*t)->iterate_TeamInstanceList(); !iter.done(); iter.advance() )
		{
			Team *team = iter.cur();
			if( team == NULL )
				continue;
			// the default team is no squad: its hunters, strays and emergency defenders stand all over the map,
			// and their middle is nowhere.  Judged as one, a China pulled 59 of them out of a fight only that
			// middle was in (Bot Cup S1-C1, frame 180382).  Its ranked units still go to be mended
			if( team == m_player->getDefaultTeam() )
			{
				pullOutRanked( team, FALSE, 1.0f );
				continue;
			}

			// where this team is
			Real count = 0.0f;
			Coord3D centre;
			centre.zero();
			const Object *walker = NULL;	// whose feet judge the tunnel home
			for( DLINK_ITERATOR<Object> objIter = team->iterate_TeamMemberList(); !objIter.done(); objIter.advance() )
			{
				Object *obj = objIter.cur();
				if( obj == NULL || obj->isEffectivelyDead() )
					continue;
				if( obj->isKindOf( KINDOF_PROJECTILE ) || !retreatCanOrderHome( obj ) )
					continue;			// only the units that could actually be pulled out place the fight
				if( walker == NULL )
					walker = obj;
				centre.x += obj->getPosition()->x;
				centre.y += obj->getPosition()->y;
				count += 1.0f;
			}
			if( count < 1.0f )
				continue;
			centre.x /= count;
			centre.y /= count;
			centre.z = TheTerrainLogic->getGroundHeight( centre.x, centre.y );

			// the members holding back from an earlier fight: back to it, or home.  Not judged again on the
			// same pass: read from where they stood, 36 teams sent back were pulled out again at once
			if( doFallback( team ) )
				continue;

			// A fight at home is not broken off: home is where the retreat goes.  Ordered to the base
			// centre, the defenders of an early rush stopped shooting and walked, every decision tick,
			// while sixteen rebels took the base apart untouched for a minute.  A ranked unit hurt badly
			// enough still goes to be mended, at home too.  Except a fight under enemy guns outside the base
			// itself (measureFight's defencePower; one built inside it is fought where it stands): home is
			// two base radii, and on Tournament Desert that takes in the middle of the map, where China
			// puts Gatling Cannons by the supply docks both sides want
			const Bool home = isAtHome( &centre );
			Real enemyHealth = 0.0f, enemyPower = 0.0f, defencePower = 0.0f;
			std::vector<Real> enemyGuns;
			const Real ratio = fightRatio( &centre, FALSE, &enemyHealth, &enemyPower, &enemyGuns, &defencePower );
			const Bool homeFight = home && defencePower <= 0.0f;
			pullOutRanked( team, !homeFight && enemyPower > 0.0f, homeFight ? 1.0f : ratio );
			if( homeFight )
				continue;

			if( enemyPower <= 0.0f )
				continue;			// not in a fight

			if( ratio >= profile->m_retreatTtkRatio )
				continue;			// holding, or winning

			//
			// Losing.  The whole team falls back if this rung knows how; otherwise the members that
			// are personally finished go, which saves the units that would otherwise die inside a
			// fight the team as a whole is still winning.
			//
			// Back to the first ground on the way home that none of the guns in this fight reach, not
			// to the base.  Sent home, a team that broke off a fight at the enemy's gate walked the
			// whole map back, healed nothing (nothing at home repairs a unit the walk delivers to the
			// base centre), and then stood there until a script or the next wave called it up.  From
			// the safe spot it goes back once the fight turns (doFallback).  Only when there is no
			// such ground short of home does it go home, through the tunnels when there is one near the
			// fight and one near home.
			//
			Coord3D fallback = m_baseCenter;
			Region3D map;
			TheTerrainLogic->getExtent( &map );
			const Real bounds[ 4 ] = { map.lo.x, map.lo.y, map.hi.x, map.hi.y };
			ensureInfluence();
			const Bool shortOfHome = !measuringWithoutTactics() &&
				aiRetreatFallbackPoint( centre.x, centre.y, m_baseCenter.x, m_baseCenter.y, INFLUENCE_CELL_SIZE,
					enemyGuns.empty() ? NULL : &enemyGuns[ 0 ], (Int)enemyGuns.size() / 3, bounds, &fallback.x, &fallback.y, &m_influence );
			if( shortOfHome )
				fallback.z = TheTerrainLogic->getGroundHeight( fallback.x, fallback.y );
			const Real homeX = m_baseCenter.x - centre.x;
			const Real homeY = m_baseCenter.y - centre.y;
			Object *homeTunnel = shortOfHome ? NULL : TunnelTracker::findTunnelShortcut( walker, &centre, &m_baseCenter,
				(Real)sqrt( homeX * homeX + homeY * homeY ) );
			if( homeTunnel )
				DEBUG_LOG(("AI RETREAT frame %d player %d falls back through tunnel %d\n", TheGameLogic->getFrame(),
					m_player->getPlayerIndex(), homeTunnel->getID()));
			Int ordered = 0;
			for( DLINK_ITERATOR<Object> objIter = team->iterate_TeamMemberList(); !objIter.done(); objIter.advance() )
			{
				Object *obj = objIter.cur();
				if( obj == NULL || obj->isEffectivelyDead() || !retreatCanOrderHome( obj ) )
					continue;

				if( !profile->m_retreatTeams )
				{
					if( !profile->m_retreatIndividualUnits )
						continue;
					// this one's own exchange, against the same enemy force
					Real oneHealth = 0.0f, onePower = 0.0f;
					addToForce( obj, &oneHealth, &onePower );
					if( aiRetreatRatio( oneHealth, onePower, enemyHealth, enemyPower ) >= profile->m_retreatTtkRatio )
						continue;
				}

				// one already on its way down a tunnel is on its way home; ordering it again would turn it
				// round at the mouth
				if( obj->getAI()->hasTunnelTrip() )
					continue;
				// nor one walking to an Ambulance that called it in: sent home instead, it left the Ambulance
				// hurt (the other 2 of the 4 walk-offs in six Hard USA mirrors)
				if( isWalkingToAmbulance( obj ) || isOutToMend( obj ) )
					continue;
				// one that is kiting the fight is winning its own share of it however the totals read: a
				// Rocket Buggy turned for home in front of four tanks it outran and outranged died turning
				const TacticalStep *kiting = findTacticalStep( obj->getID() );
				if( kiting && kiting->lastKiteFrame != 0 && TheGameLogic->getFrame() - kiting->lastKiteFrame < KITE_KEEPS_FIGHT_FRAMES )
					continue;
				// one already holding at a safe spot is doFallback's: ordered again, it crept a step further
				// home and started its hold over on every tick the fight beside it still read as lost.  One
				// already sent home (a fallback frame and no hold) is left to walk there.
				if( kiting && (kiting->fallingBack ||
						(kiting->fallbackFrame != 0 && TheGameLogic->getFrame() - kiting->fallbackFrame < RETREAT_HOME_WALK_FRAMES)) )
					continue;
				++ordered;
				if( homeTunnel != NULL && obj->getAI()->takeTunnelTrip( homeTunnel, &m_baseCenter, TUNNEL_TRIP_MOVE, CMD_FROM_AI ) )
					leaveTacticsAlone( obj->getID() );
				else if( measuringWithoutTactics() )
					obj->getAI()->aiMoveToPosition( &m_baseCenter, CMD_FROM_AI );
				else
				{
					// the walk back taken calm and as an order, on every rung: a CMD_FROM_AI move to a unit
					// that is fighting is laid under its attack, and an aggressive mood turns the walk back
					// into a fight, so the retreat never happened
					leaveTacticsAlone( obj->getID() );
					TacticalStep *step = tacticalStepFor( obj->getID() );
					step->fallingBack = shortOfHome;
					step->fallbackFrom = centre;
					step->fallbackFrame = TheGameLogic->getFrame();
					stepCalmly( obj, step, &fallback );
				}
			}
			if( ordered > 0 )
				DEBUG_LOG(("AI RETREAT frame %d player %d '%s' pulls %d units out at %.2f, %s (%.0f,%.0f)\n", TheGameLogic->getFrame(),
					m_player->getPlayerIndex(), team->getName().str(), ordered, ratio, shortOfHome ? "falls back to" : "goes home to",
					fallback.x, fallback.y));
		}
	}
}

/** A unit pulled out of a lost fight to a safe spot, and not yet sent back or home. */
Bool AIPlayer::isFallingBack( ObjectID unit ) const
{
	for( size_t i = 0; i < m_tactics.size(); ++i )
		if( m_tactics[ i ].unit == unit )
			return m_tactics[ i ].fallingBack;
	return FALSE;
}

//----------------------------------------------------------------------------------------------------------
/** Both sides of the exchange around centre, measured the same way: everything of ours in the fight
	* against everything of theirs in it.  With countHolders, every unit of ours holding at a safe spot
	* after leaving a fight here counts on our side wherever it stands, which is how the holders weigh the
	* fight they would go back to.  Not one team's holders: ten teams of three pulled out of one fight each
	* read it as three against the enemy's army, and thirty units stood beside it for the whole hold.  With
	* enemyGuns, every known enemy gun in reach of the ground (base defences too) is added as x, y, reach.
	*
	* Not one team against everything of theirs.  An AI team is one to three units, and the first cut of
	* the retreat compared one such team with every enemy within three hundred feet - so a team of two
	* tanks read a battle it was part of as 2,600 against 29,400, decided it was losing by a hundred to
	* one and walked home.  Every team did, every few seconds, for the whole match: twenty measured
	* matches ended with zero kills on both sides. */
//----------------------------------------------------------------------------------------------------------
static Bool heldFrom( Bool fallingBack, const Coord3D *from, const Coord3D *centre )
{
	return fallingBack && sqr( from->x - centre->x ) + sqr( from->y - centre->y ) <= sqr( RETREAT_ENGAGEMENT_RADIUS );
}

/** What a building shoots the ground with: its own gun and the men firing out of it (a Bunker, a Fire
	* Base, a civilian building with a garrison), in aiCombatPower's currency, and the longest reach among
	* them.  A building's own aiCombatPower is 0 when it cannot attack, and so is a garrison's in a building
	* that lets nobody fire.  Nothing while it is going up, being sold, or out of power, EMP'd or hacked. */
static Real fixedGunPower( const Object *obj, Real *reach )
{
	*reach = 0.0f;
	if( obj->testStatus( OBJECT_STATUS_UNDER_CONSTRUCTION ) || obj->testStatus( OBJECT_STATUS_SOLD ) || obj->isDisabled() )
		return 0.0f;
	*reach = groundAttackRange( obj );
	Real power = *reach > 0.0f ? aiCombatPower( obj ) : 0.0f;
	const ContainModuleInterface *contain = obj->getContain();
	if( contain && contain->isPassengerAllowedToFire() )
	{
		const ContainedItemsList *items = contain->getContainedItemsList();
		for( ContainedItemsList::const_iterator it = items->begin(); it != items->end(); ++it )
		{
			const Real range = groundAttackRange( *it );
			if( range <= 0.0f )
				continue;
			power += aiCombatPower( *it );
			if( range > *reach )
				*reach = range;
		}
	}
	return power;
}

void AIPlayer::measureFight( const Coord3D *centre, Bool countHolders, Real *myHealth, Real *myPower,
														 Real *enemyHealth, Real *enemyPower, std::vector<Real> *enemyGuns, Real *advantage, Real *defencePower )
{
	std::vector<AIVisibleEnemy> mine, theirs;
	std::vector<AIVisibleEnemy> *myKinds = advantage ? &mine : NULL;
	std::vector<AIVisibleEnemy> *theirKinds = advantage ? &theirs : NULL;
	PartitionFilterAlive filterAlive;
	PartitionFilterOnMap filterOnMap;
	PartitionFilter *filters[] = { &filterAlive, &filterOnMap, 0 };

	std::vector<const Object *> defences;		// the known enemy buildings that shoot, met in the walk
	std::vector<Real> defenceReach;
	std::vector<Coord3D> ours;								// where our units in the fight stand
	MemoryPoolObjectHolder hold;
	SimpleObjectIterator *nearby = ThePartitionManager->iterateObjectsInRange(
			centre, RETREAT_ENGAGEMENT_RADIUS, FROM_CENTER_2D, filters );
	hold.hold( nearby );
	for( Object *e = nearby->first(); e; e = nearby->next() )
	{
		if( e->isKindOf( KINDOF_PROJECTILE ) )
			continue;
		const Bool fixed = e->isKindOf( KINDOF_STRUCTURE ) || e->isKindOf( KINDOF_IMMOBILE );

		if( e->getControllingPlayer() == m_player )
		{
			// a holder counted below is not counted twice
			const TacticalStep *row = countHolders ? findTacticalStep( e->getID() ) : NULL;
			if( !fixed && (row == NULL || !heldFrom( row->fallingBack, &row->fallbackFrom, centre )) )
			{
				addToForce( e, myHealth, myPower );
				addToArmy( myKinds, e );
				ours.push_back( *e->getPosition() );
			}
		}
		else if( m_player->getRelationship( e->getTeam() ) == ENEMIES )
		{
			// only what it can see: an AI that pulls back from something it has not found is reading
			// the object list again (A2)
			if( !observerKnowsAbout( e, m_player->getPlayerIndex() ) )
				continue;
			Real reach = 0.0f;
			if( !fixed )
			{
				addToForce( e, enemyHealth, enemyPower );
				addToArmy( theirKinds, e );
				reach = enemyGuns ? groundAttackRange( e ) : 0.0f;
			}
			else if( fixedGunPower( e, &reach ) > 0.0f )
			{
				defences.push_back( e );
				defenceReach.push_back( reach );
			}
			if( enemyGuns && reach > 0.0f )
			{
				enemyGuns->push_back( e->getPosition()->x );
				enemyGuns->push_back( e->getPosition()->y );
				enemyGuns->push_back( reach );
			}
		}
	}

	/* A defence whose guns take in the fight is in it.  Left out, a wave dying to Gatling Cannons read
		 enemy power 0, "not in a fight", and never broke off: in Bot Cup S1-C1 the Air Force lost 406 units
		 to buildings, 210 of them to Gatling Cannons, and 188 Tomahawk missiles besides.  They were left out
		 on purpose once, since counting every building near a fight made each approach to a base a lost
		 fight and the AI stopped
		 attacking; only a gun that reaches the fight's centre or one of ours in it counts here, so a wave
		 still walks up to a base and is judged when the guns are on it, and holders weigh the fight they
		 left by the guns that cover the spot. */
	for( size_t d = 0; d < defences.size(); ++d )
	{
		const Coord3D *at = defences[ d ]->getPosition();
		const Real reachSqr = sqr( defenceReach[ d ] + DEFENCE_FIGHT_SLACK );
		Bool covers = sqr( at->x - centre->x ) + sqr( at->y - centre->y ) <= reachSqr;
		for( size_t u = 0; !covers && u < ours.size(); ++u )
			covers = sqr( at->x - ours[ u ].x ) + sqr( at->y - ours[ u ].y ) <= reachSqr;
		if( !covers )
			continue;
		Real reach = 0.0f;
		const Real guns = fixedGunPower( defences[ d ], &reach );
		*enemyPower += guns;
		// a gun the enemy put up inside our base is one more intruder there, not a reason to leave it
		if( defencePower && sqr( at->x - m_baseCenter.x ) + sqr( at->y - m_baseCenter.y ) > sqr( m_baseRadius ) )
			*defencePower += guns;
		*enemyHealth += defences[ d ]->getBodyModule()->getHealth();
		addToArmy( theirKinds, defences[ d ] );
		const ContainModuleInterface *contain = defences[ d ]->getContain();
		if( contain && contain->isPassengerAllowedToFire() )
		{
			const ContainedItemsList *items = contain->getContainedItemsList();
			for( ContainedItemsList::const_iterator it = items->begin(); it != items->end(); ++it )
				addToArmy( theirKinds, *it );
		}
	}

	/* A gun that has hit ours around here and that the walk above missed: one in the fog, or one past its
		 radius.  The siege guns start at 300, so a Tomahawk shelling a team from further off read enemy power
		 0, "not in a fight", with the team standing in its shells.  Around here is where the gun stands or
		 where it hit, so holders weigh a gun they walked away from as well as the ground they left. */
	const Int me = m_player->getPlayerIndex();
	for( size_t r = 0; r < m_fireReports.size(); ++r )
	{
		const FireReport &report = m_fireReports[ r ];
		if( sqr( report.hitAt.x - centre->x ) + sqr( report.hitAt.y - centre->y ) > sqr( RETREAT_ENGAGEMENT_RADIUS ) &&
				sqr( report.at.x - centre->x ) + sqr( report.at.y - centre->y ) > sqr( RETREAT_ENGAGEMENT_RADIUS ) )
			continue;
		Object *shooter = reportedShooter( report );
		if( shooter == NULL )
			continue;
		const Object *gun = gunCarrier( shooter );
		// one in view is where it is: the walk above met it, or it has driven off and no longer covers this
		if( observerKnowsAbout( gun, me ) )
		{
			const Real gunDistanceSqr = sqr( gun->getPosition()->x - centre->x ) + sqr( gun->getPosition()->y - centre->y );
			if( gunDistanceSqr <= sqr( RETREAT_ENGAGEMENT_RADIUS ) || gunDistanceSqr > sqr( report.reach + DEFENCE_FIGHT_SLACK ) )
				continue;
		}
		*enemyPower += report.power;
		*enemyHealth += gun->getBodyModule()->getHealth();
		addToArmy( theirKinds, gun );
		if( enemyGuns )
		{
			enemyGuns->push_back( report.at.x );
			enemyGuns->push_back( report.at.y );
			enemyGuns->push_back( report.reach );
		}
		// a building, as above: mortars in the fog behind a rush are no reason to leave the base to it
		if( defencePower && (gun->isKindOf( KINDOF_STRUCTURE ) || gun->isKindOf( KINDOF_IMMOBILE )) &&
				sqr( report.at.x - m_baseCenter.x ) + sqr( report.at.y - m_baseCenter.y ) > sqr( m_baseRadius ) )
			*defencePower += report.power;
	}

	for( size_t i = 0; countHolders && i < m_tactics.size(); ++i )
	{
		if( !heldFrom( m_tactics[ i ].fallingBack, &m_tactics[ i ].fallbackFrom, centre ) )
			continue;
		Object *obj = TheGameLogic->findObjectByID( m_tactics[ i ].unit );
		// and still ours: a holder taken by a Hijacker keeps its row until it expires
		if( obj && !obj->isEffectivelyDead() && obj->getControllingPlayer() == m_player && !obj->isKindOf( KINDOF_PROJECTILE ) &&
				!obj->isKindOf( KINDOF_STRUCTURE ) && !obj->isKindOf( KINDOF_IMMOBILE ) )
		{
			addToForce( obj, myHealth, myPower );
			addToArmy( myKinds, obj );
		}
	}
	if( advantage )
		*advantage = armyAdvantage( mine, theirs );
}

//----------------------------------------------------------------------------------------------------------
/** How the fight around centre is going (aiRetreatRatio).  With EngageGate the units in it are weighed
	* against each other as well as by price: twelve Crusaders and six Overlords cost the same, and the
	* Crusaders lose.  The money ratio called that fight even and kept feeding tanks into it; one by unit
	* against unit breaks it off, and stays in one a cheaper army of the right units is winning. */
//----------------------------------------------------------------------------------------------------------
Real AIPlayer::fightRatio( const Coord3D *centre, Bool countHolders, Real *enemyHealth, Real *enemyPower, std::vector<Real> *enemyGuns,
													 Real *defencePower )
{
	Real myHealth = 0.0f, myPower = 0.0f;
	Real advantage = 1.0f;
	*enemyHealth = 0.0f;
	*enemyPower = 0.0f;
	measureFight( centre, countHolders, &myHealth, &myPower, enemyHealth, enemyPower, enemyGuns,
		getSkillProfile()->m_engageGate ? &advantage : NULL, defencePower );
	return aiRetreatRatio( myHealth, myPower, *enemyHealth, *enemyPower ) * advantage;
}

/** How long a team that fell back holds before it may go back, at the least and at the most; and how
	* well the fight it left has to read before it does, which is even.  Breaking off is decided at the
	* rung's own ratio, well under even, so a team does not turn round on the same reading it ran from. */
static const UnsignedInt FALLBACK_MIN_HOLD_FRAMES = 5 * LOGICFRAMES_PER_SECOND;
static const UnsignedInt FALLBACK_MAX_HOLD_FRAMES = 30 * LOGICFRAMES_PER_SECOND;
static const Real FALLBACK_RESUME_RATIO = 1.0f;
/** A holder doFallback has not let go of this long after it left the fight is one it no longer sees. */
static const UnsignedInt FALLBACK_STALE_FRAMES = FALLBACK_MAX_HOLD_FRAMES + 10 * LOGICFRAMES_PER_SECOND;
/** How long the holders wait for every one of them to stop before the fight they left gets a say. */
static const UnsignedInt FALLBACK_ARRIVE_MAX_FRAMES = 10 * LOGICFRAMES_PER_SECOND;
/** How long one that left a gun's ground holds when no retreat is looking at it. */
static const UnsignedInt FIRE_HOLD_FRAMES = 10 * LOGICFRAMES_PER_SECOND;

//----------------------------------------------------------------------------------------------------------
/** The members of this team holding at a safe spot after a lost fight.  Once they have stopped, they go
	* back to the fight they left when ours there now outweigh it - every holder from that fight counted,
	* whichever team it is in - attack-moving to it, which picks up the fight or whatever is beyond it.
	* A fight with nothing of theirs left in it sends them home instead: an attack-move to an empty spot
	* left them idle out on the map, where nothing calls up a team while this AI is outmatched.  A fight
	* that still reads as lost FALLBACK_MAX_HOLD_FRAMES after the first of them left it sends them home,
	* where the next wave takes them along; timed from the last to leave, a teammate pulled out every
	* few seconds kept the first ones standing for as long as the fight went on. */
//----------------------------------------------------------------------------------------------------------
Bool AIPlayer::doFallback( Team *team )
{
	const UnsignedInt now = TheGameLogic->getFrame();
	Bool holding = FALSE;
	Bool arrived = TRUE;
	Coord3D from;
	UnsignedInt since = now;
	UnsignedInt first = now;
	for( DLINK_ITERATOR<Object> objIter = team->iterate_TeamMemberList(); !objIter.done(); objIter.advance() )
	{
		Object *obj = objIter.cur();
		const TacticalStep *step = findTacticalStep( obj->getID() );
		if( step == NULL || !step->fallingBack || obj->isEffectivelyDead() )
			continue;
		if( !holding || step->fallbackFrame > since )
		{
			from = step->fallbackFrom;		// the latest fight any of them left
			since = step->fallbackFrame;
		}
		if( !holding || step->fallbackFrame < first )
			first = step->fallbackFrame;
		holding = TRUE;
		if( obj->getAI() && obj->getAI()->isMoving() )
			arrived = FALSE;
	}
	if( !holding )
		return FALSE;

	Real enemyHealth = 0.0f, enemyPower = 0.0f;
	const Real ratio = fightRatio( &from, TRUE, &enemyHealth, &enemyPower, NULL );
	// the shortest hold runs from the last to arrive, the longest from the first
	const UnsignedInt held = now - first >= FALLBACK_MAX_HOLD_FRAMES ? now - first : now - since;
	// one still moving this long after it was pulled out is on somebody else's errand (its script, a
	// chase); waiting for it held the rest beside a fight that had already turned, 95 times in 148 to the cap
	if( held >= FALLBACK_ARRIVE_MAX_FRAMES )
		arrived = TRUE;
	AIFallbackDecision what = aiRetreatHoldDecision( arrived, held, ratio, FALLBACK_RESUME_RATIO,
		FALLBACK_MIN_HOLD_FRAMES, FALLBACK_MAX_HOLD_FRAMES );
	if( what == AIFALLBACK_HOLD )
		return FALSE;
	const Bool fightOver = what == AIFALLBACK_RESUME && enemyPower <= 0.0f;
	if( fightOver )
		what = AIFALLBACK_GO_HOME;

	DEBUG_LOG(("AI RETREAT frame %d player %d '%s' %s after %d s, the fight at (%.0f,%.0f) reads %.2f\n", now,
		m_player->getPlayerIndex(), team->getName().str(),
		what == AIFALLBACK_RESUME ? "goes back" : fightOver ? "goes home, the fight is over," : "gives up and goes home",
		held / LOGICFRAMES_PER_SECOND, from.x, from.y, ratio));
	for( DLINK_ITERATOR<Object> objIter = team->iterate_TeamMemberList(); !objIter.done(); objIter.advance() )
	{
		Object *obj = objIter.cur();
		TacticalStep *step = findTacticalStep( obj->getID() );
		if( step == NULL || !step->fallingBack )
			continue;
		step->fallingBack = FALSE;
		step->fallbackFrame = 0;		// free to be pulled out of the next fight, unless it is walked home below
		if( obj->isEffectivelyDead() || obj->getAI() == NULL )
			continue;
		// One that is already on its way somewhere was given that by its script, the home guard or a
		// step of its own, and carries on with it, unless the fight is lost: a holder still walking to
		// its safe spot when the hold ran out kept that walk, unmarked, and the same pass pulled it out
		// again for another full hold.  One with no gun has no fight to go back to: a Dozer pulled out of
		// a fight was walked back into it.
		// One walking to an Ambulance that called it in carries on too: walked home instead, it had left
		// the Ambulance's enter order still hurt.
		const Bool givesUp = what == AIFALLBACK_GO_HOME && !fightOver;
		if( isWalkingToAmbulance( obj ) || (!givesUp && obj->getAI()->isMoving()) || (what == AIFALLBACK_RESUME && aiCombatPower( obj ) <= 0.0f) )
			restoreMood( obj, step );
		else if( what == AIFALLBACK_RESUME )
		{
			restoreMood( obj, step );
			// and committed, like one sent at a gun that hit it: back on the ground it left, the next hit does
			// not walk it off again on a reading the retreat would not act on
			step->origin = from;
			step->target = INVALID_ID;
			step->answerFrame = now;
			step->rejoin = FALSE;
			obj->getAI()->aiAttackMoveToPosition( &from, NO_MAX_SHOTS_LIMIT, CMD_FROM_AI );
		}
		else
		{
			leaveTacticsAlone( obj->getID() );
			step->fallbackFrame = now;		// the walk home doRetreats leaves alone
			stepCalmly( obj, step, &m_baseCenter );
		}
	}
	return TRUE;
}

//----------------------------------------------------------------------------------------------------------
/** How often each fighting unit is looked at.  This and the map's rung clock go by the frame and the
	* object's ID, so there is no timer to save and every machine does it on the same frame. */
static const Int TACTICS_RATE = LOGICFRAMES_PER_SECOND / 3;

/** Kiting.  A unit steps back from something it outranges once that thing is inside its own reach
	* plus this much, and stops where the enemy's gun is this far short of it. */
static const Real KITE_MARGIN = 25.0f;
/** ... and starts watching it this far outside the enemy's reach, so the turn is done before it arrives. */
static const Real KITE_WATCH_MARGIN = 2.0f * KITE_MARGIN;
/** ... and only from something at least this share of what it is worth itself. */
static const Real KITE_WORTHY_SHARE = 0.5f;
/** ... and, when that something can move, only with at least this much more range than it. */
static const Real KITE_MOBILE_RANGE_RATIO = 1.5f;
/** The longest a step lasts before it goes back to shooting, whether it got there or not. */
static const UnsignedInt TACTICAL_STEP_MAX_FRAMES = 2 * LOGICFRAMES_PER_SECOND;
/** A kite is a hop, not a walk: every frame on the move is a frame not shooting.  It gets the turn and
	* the drive to its spot, and never more than this. */
static const UnsignedInt KITE_STEP_MAX_FRAMES = 3 * LOGICFRAMES_PER_SECOND;
static const UnsignedInt KITE_TURN_FRAMES = LOGICFRAMES_PER_SECOND;

/** The high ground.  Worth a climb when it is this much higher than where the unit stands, which is
	* thirty more units of range; looked for this far round the unit, and not again for a while. */
static const Real CLIMB_MIN_GAIN = 10.0f;
static const Real CLIMB_SEARCH_RADIUS = 110.0f;
static const UnsignedInt CLIMB_INTERVAL_FRAMES = 6 * LOGICFRAMES_PER_SECOND;

/** A unit this hurt, standing where what can shoot it outweighs what is on its side, is spent for
	* nothing if it stays. */
static const Real HURT_HEALTH_SHARE = 0.35f;

/** Rows for units not looked at for this long are dropped: dead, garrisoned, or gone home. */
static const UnsignedInt TACTICAL_ROW_EXPIRY_FRAMES = 10 * LOGICFRAMES_PER_SECOND;
/** A unit the retreat sent home is not turned round by a kite. */
static const UnsignedInt LEAVE_ALONE_FRAMES = 10 * LOGICFRAMES_PER_SECOND;
/** ... and gets its mood back when it stops walking, or this long after that at the latest. */
static const UnsignedInt WALK_HOME_MAX_FRAMES = 30 * LOGICFRAMES_PER_SECOND;

static void collectObject( Object *obj, void *userData )
{
	((std::vector<Object *> *)userData)->push_back( obj );
}

/** The longest gun this has that can hit something standing on the ground. */
static Real groundAttackRange( const Object *obj )
{
	Real best = 0.0f;
	for( Int slot = PRIMARY_WEAPON; slot < WEAPONSLOT_COUNT; ++slot )
	{
		const Weapon *weapon = obj->getWeaponInWeaponSlot( (WeaponSlotType)slot );
		if( weapon == NULL || (weapon->getTemplate()->getAntiMask() & WEAPON_ANTI_GROUND) == 0 )
			continue;
		const Real range = weapon->getAttackRange( obj );
		if( range > best )
			best = range;
	}
	return best;
}

/** A ground vehicle whose gun outreaches the line: the Tomahawk, the Scud Launcher, the Nuke and Inferno
	* Cannons, the Rocket Buggy.  Not one that carries anybody: the Battle Bus's dummy gun reaches 320. */
static const Real SIEGE_RANGE = 300.0f;
static Bool isSiegeGun( const Object *obj )
{
	return obj->isKindOf( KINDOF_VEHICLE ) && !obj->isKindOf( KINDOF_AIRCRAFT ) && obj->getContain() == NULL &&
		groundAttackRange( obj ) >= SIEGE_RANGE;
}

/** How far a trailing siege gun may stray from its place before it is sent back to it. */
static const Real SIEGE_SLACK = 60.0f;

/** A wave walks its road at the pace of the slowest and nobody keeps an order in it, so a Tomahawk or
	* a Nuke Cannon can be first round the corner, where it dies to the first tank it meets before its
	* long gun is any use.  Each wave tick a siege gun out with a group and not firing is sent to a spot
	* m_siegeTrail behind the group's front unit, the line unit farthest from home, on the side towards
	* home.  It attack-moves there, so whatever comes into its reach on the way is shot.  When the group
	* has no line unit left it is left alone.  The front is not the unit nearest the enemy's start: on
	* Twilight Flame that start was a guess, and a Paladin driving at the real enemy drove away from it.
	* ponytail: home is a straight line from the front, not the road; take the path's previous waypoint
	* if guns end up behind a cliff. */
void AIPlayer::keepSiegeBehind( void )
{
	const Real trail = getSkillProfile()->m_siegeTrail;
	if( !runsOwnAI() || !m_baseCenterSet || trail <= 0.0f )
		return;
	const Coord3D &base = m_baseCenter;
	std::vector<Object *> owned;
	m_player->iterateObjects( collectObject, &owned );
	Int sent = 0;
	Int ahead = 0;
	for( std::vector<Object *>::const_iterator o = owned.begin(); o != owned.end(); ++o )
	{
		Object *gun = *o;
		if( gun->isEffectivelyDead() || gun->isContained() || gun->getAI() == NULL || gun->getGroup() == NULL ||
				!isSiegeGun( gun ) || gun->getAI()->isAttacking() || isAtHome( gun->getPosition() ) || isFallingBack( gun->getID() ) ||
				isAnsweringFire( gun->getID() ) )
			continue;
		const Object *front = NULL;
		Real frontSqr = 0.0f;
		const VecObjectID &ids = gun->getGroup()->getAllIDs();
		for( VecObjectID::const_iterator it = ids.begin(); it != ids.end(); ++it )
		{
			const Object *mate = TheGameLogic->findObjectByID( *it );
			if( mate == NULL || mate == gun || mate->isEffectivelyDead() || mate->isContained() || mate->isKindOf( KINDOF_AIRCRAFT ) ||
					isSiegeGun( mate ) || aiCombatPower( mate ) <= 0.0f )
				continue;
			const Real d = sqr( mate->getPosition()->x - base.x ) + sqr( mate->getPosition()->y - base.y );
			if( front == NULL || d > frontSqr )
			{
				front = mate;
				frontSqr = d;
			}
		}
		if( front == NULL )
			continue;
		Coord2D home;
		home.x = base.x - front->getPosition()->x;
		home.y = base.y - front->getPosition()->y;
		if( home.x == 0.0f && home.y == 0.0f )
			continue;
		home.normalize();
		Coord3D spot = *front->getPosition();
		spot.x += home.x * trail;
		spot.y += home.y * trail;
		spot.z = TheTerrainLogic->getGroundHeight( spot.x, spot.y );
		if( sqr( gun->getPosition()->x - spot.x ) + sqr( gun->getPosition()->y - spot.y ) <= sqr( SIEGE_SLACK ) )
			continue;
		gun->getAI()->aiAttackMoveToPosition( &spot, NO_MAX_SHOTS_LIMIT, CMD_FROM_AI );
		++sent;
		if( sqr( gun->getPosition()->x - base.x ) + sqr( gun->getPosition()->y - base.y ) > frontSqr )
			++ahead;
	}
	if( sent > 0 )
		DEBUG_LOG(("AI SIEGE frame %d player %d puts %d siege guns %.0f behind their front, %d of them were ahead of it\n",
			TheGameLogic->getFrame(), m_player->getPlayerIndex(), sent, trail, ahead));
}

/** How often the edges are looked at; the odds ours at an edge need, by rung; how far ahead of a moving
	* unit its walk is looked at; and how far outside a reach a held unit waits.  The wait is more than the
	* quickest ground unit covers between two looks: at 45 an idle unit that picked the defence as its next
	* target was past the edge before the next look, and the Air Force lost more of those than before. */
static const Int EDGE_RATE = LOGICFRAMES_PER_SECOND;
static const Real EDGE_ODDS[ AISKILL_COUNT ] = { 0.0f, 1.0f, 1.25f };
static const Real EDGE_LOOKAHEAD = 120.0f;
static const Real EDGE_WAIT = 90.0f;
/** A wait spot another gun covers moves out by EDGE_WAIT, this many times at most before the unit goes home. */
static const Int EDGE_WAIT_TRIES = 4;
/** A walk home goes round a reach this far outside its edge. */
static const Real EDGE_ROUND = 30.0f;
/** Slower than this a frame, a unit is standing rather than walking anywhere. */
static const Real EDGE_STILL_SPEED = 0.05f;
/** An attack move ending this near the fight a unit was pulled out of is doFallback sending it back. */
static const Real RESUME_GOAL_SLACK = 30.0f;
/** A gun goes to shell a known defence this far past its own range, and fires from this far inside it;
	* and only one it would bring down on its own in this long.  A Missile Defender's laser outreaches a
	* garrisoned warehouse, and stood plinking at it 97 times in four matches.  A gun already walking to
	* within this of its spot is left to walk. */
static const Real SHELL_WALK = 200.0f;
static const Real SHELL_INSET = 10.0f;
static const Real SHELL_MAX_FRAMES = 60.0f * LOGICFRAMES_PER_SECOND;
static const Real SHELL_SPOT_SLACK = 60.0f;

/** Whether a known defence other than the one at skip covers a spot. */
static Bool otherDefenceCovers( const std::vector<KnownDefence> &defences, size_t skip, Real x, Real y )
{
	for( size_t d = 0; d < defences.size(); ++d )
	{
		const Coord3D *at = defences[ d ].m_object->getPosition();
		if( d != skip && sqr( at->x - x ) + sqr( at->y - y ) <= sqr( defences[ d ].m_reach + DEFENCE_FIGHT_SLACK ) )
			return TRUE;
	}
	return FALSE;
}

/** A unit with nothing to shoot goes after the nearest known defence its primary gun outreaches, up to
	* SHELL_WALK past its range, that it would bring down alone within SHELL_MAX_FRAMES, from a spot inside its
	* range that no other known gun covers.  It walks to that spot first and opens fire from there: told to
	* attack from afar, it took its own way in and could stop under the next gun.  The defence's index, or -1. */
static Int shellDefence( const Player *player, Object *obj, Real range, const std::vector<KnownDefence> &defences )
{
	const Coord3D *pos = obj->getPosition();
	Int best = -1;
	Real bestSqr = 0.0f;
	Coord3D fire;
	fire.zero();
	for( size_t d = 0; d < defences.size(); ++d )
	{
		const Coord3D *at = defences[ d ].m_object->getPosition();
		const Real distSqr = sqr( at->x - pos->x ) + sqr( at->y - pos->y );
		const Real framesToKill = templateFramesToKill( obj->getTemplate(), defences[ d ].m_object->getTemplate() );
		if( range < defences[ d ].m_reach + DEFENCE_FIGHT_SLACK || distSqr > sqr( range + SHELL_WALK ) || (best >= 0 && distSqr >= bestSqr) ||
				framesToKill < 0.0f || framesToKill > SHELL_MAX_FRAMES )
			continue;
		const Real dist = (Real)sqrt( distSqr );
		const Real fireX = at->x + (pos->x - at->x) / dist * (range - SHELL_INSET);
		const Real fireY = at->y + (pos->y - at->y) / dist * (range - SHELL_INSET);
		if( otherDefenceCovers( defences, d, fireX, fireY ) ||
				obj->getAbleToAttackSpecificObject( ATTACK_NEW_TARGET, defences[ d ].m_object, CMD_FROM_AI ) != ATTACKRESULT_POSSIBLE )
			continue;
		best = (Int)d;
		bestSqr = distSqr;
		fire.x = fireX;
		fire.y = fireY;
	}
	if( best < 0 )
		return -1;
	AIUpdateInterface *ai = obj->getAI();
	const KnownDefence &gun = defences[ best ];
	const Bool inRange = bestSqr <= sqr( range );
	const Coord3D *goal = ai->getGoalPosition();
	if( !inRange && ai->isMoving() && sqr( goal->x - fire.x ) + sqr( goal->y - fire.y ) <= sqr( SHELL_SPOT_SLACK ) )
		return best;		// on its way there
	DEBUG_LOG(("AI SHELL frame %d player %d sends %d '%s', range %.0f, at %d '%s', reach %.0f, %.0f away, %s\n", TheGameLogic->getFrame(),
		player->getPlayerIndex(), obj->getID(), obj->getTemplate()->getName().str(), range, gun.m_object->getID(),
		gun.m_object->getTemplate()->getName().str(), gun.m_reach, (Real)sqrt( bestSqr ), inRange ? "fires" : "walks to its spot"));
	if( inRange )
		ai->aiAttackObject( gun.m_object, NO_MAX_SHOTS_LIMIT, CMD_FROM_AI );
	else
	{
		fire.z = TheTerrainLogic->getGroundHeight( fire.x, fire.y );
		ai->aiMoveToPosition( &fire, CMD_FROM_AI );
	}
	return best;
}

//----------------------------------------------------------------------------------------------------------
/** The owner's rule for the defences this AI knows of.  A ground unit of ours about to walk into the reach
	* of one (on a wave's road, out hunting, sent at an intruder, or after a defence it picked as its next
	* target) stops at the edge, unless ours there outweigh what covers that spot and the army near it by
	* the rung's odds (EDGE_ODDS), at a spot no other known gun covers.  It waits there calm, its mood kept
	* from walking it in, until ours at that edge do outweigh it; then its mood comes back and it goes in
	* with the rest, by the hunt or the next wave.  Nothing waits on a timer, and the waves leave as before.
	* Meanwhile a unit with nothing to shoot whose primary gun outreaches a defence (an Inferno Cannon, a
	* Tomahawk) shells it from where it is never shot back (shellDefence).  A unit walking home from a lost
	* fight goes round the reach.  A unit on an errand of its own (mending, boarding, docking, a tunnel, a
	* scout) and one doFallback sent back to its fight are left alone.  Easy walks in, as it always did.
	*
	* In eight Hard Air Force matches against China on Tournament Desert the Air Force lost 1,229 units to
	* buildings with the retreat already weighing the guns; the most, 229, were units that had picked a
	* defence as their next target and walked up to it, then attack moves, then the waves on their roads. */
//----------------------------------------------------------------------------------------------------------
void AIPlayer::holdAtDefences( void )
{
	const UnsignedInt now = TheGameLogic->getFrame();
	const Real odds = EDGE_ODDS[ m_skillLevel ];
	if( !runsOwnAI() || !m_baseCenterSet || odds <= 0.0f || measuringWithoutTactics() ||
			(now + computeUpdatePhase( m_player->getPlayerIndex(), EDGE_RATE )) % EDGE_RATE != 0 )
		return;
	std::vector<KnownDefence> defences;
	collectKnownDefences( m_player, &defences );
	if( defences.empty() )
		return;
	std::vector<Real> edgeRatios( defences.size(), -1.0f );		// the fight at each one's edge, measured once a look
	// a gun the enemy put up inside our base is fought where it stands: holding the guard at its edge, two
	// Gattling Cannons dropped 220 from the Air Force's start lived half a minute longer
	std::vector<Bool> insideBase( defences.size(), FALSE );
	for( size_t d = 0; d < defences.size(); ++d )
	{
		const Coord3D *at = defences[ d ].m_object->getPosition();
		insideBase[ d ] = sqr( at->x - m_baseCenter.x ) + sqr( at->y - m_baseCenter.y ) <= sqr( m_baseRadius );
	}
	std::vector<Object *> owned;
	m_player->iterateObjects( collectObject, &owned );
	Int held = 0;
	Int around = 0;
	for( size_t o = 0; o < owned.size(); ++o )
	{
		Object *obj = owned[ o ];
		AIUpdateInterface *ai = obj->getAI();
		if( obj->isEffectivelyDead() || ai == NULL || obj->isContained() || obj->isKindOf( KINDOF_IMMOBILE ) || obj->isKindOf( KINDOF_AIRCRAFT ) ||
				groundAttackRange( obj ) <= 0.0f || !leavesToFinish( obj ) )
			continue;
		const StateID state = ai->getCurrentStateID();
		if( isOutToMend( obj ) || isWalkingToAmbulance( obj ) || ai->hasTunnelTrip() || state == AI_DOCK || state == AI_ENTER || state >= 1000 )
			continue;
		TacticalStep *step = findTacticalStep( obj->getID() );
		const Coord3D *goal = ai->getGoalPosition();
		// a kite is the tactics'; one pulled out of a lost fight is doFallback's, and so is one it sent back to
		// that fight once ours there outweighed it
		if( step && (step->resumeFrame != 0 || step->fallingBack || (state == AI_ATTACK_MOVE_TO &&
				sqr( goal->x - step->fallbackFrom.x ) + sqr( goal->y - step->fallbackFrom.y ) <= sqr( RESUME_GOAL_SLACK ))) )
			continue;
		const Bool walkingHome = step && step->fallbackFrame != 0 && now - step->fallbackFrame < RETREAT_HOME_WALK_FRAMES;
		const Coord3D *pos = obj->getPosition();
		const Object *victim = ai->getCurrentVictim();
		const Real shellRange = primaryGroundRange( obj );

		// the defence it shells, or walks to shell: the edge it stops at is its own range, not the gun's reach
		Int shelled = -1;
		for( size_t d = 0; d < defences.size() && victim != NULL; ++d )
			if( defences[ d ].m_object == victim && shellRange >= defences[ d ].m_reach + DEFENCE_FIGHT_SLACK )
				shelled = (Int)d;
		if( victim == NULL && !walkingHome && (step == NULL || now >= step->leaveAloneUntil) )
			shelled = shellDefence( m_player, obj, shellRange, defences );

		if( !ai->isMoving() )
		{
			// one held at an edge stands there calm while ours at it are short, and is let go once they are not
			if( step == NULL || step->savedAttitude == AI_INVALID || walkingHome )
				continue;
			Int edgeOf = -1;
			for( size_t d = 0; d < defences.size() && edgeOf < 0; ++d )
			{
				const Coord3D *at = defences[ d ].m_object->getPosition();
				const Real distSqr = sqr( at->x - pos->x ) + sqr( at->y - pos->y );
				const Real band = defences[ d ].m_reach + DEFENCE_FIGHT_SLACK + EDGE_WAIT * EDGE_WAIT_TRIES;
				if( !insideBase[ d ] && distSqr > sqr( defences[ d ].m_reach ) && distSqr <= sqr( band ) )
					edgeOf = (Int)d;
			}
			if( edgeOf < 0 )
				continue;
			if( edgeRatios[ edgeOf ] < 0.0f )
				edgeRatios[ edgeOf ] = edgeRatio( defences[ edgeOf ].m_object, defences[ edgeOf ].m_reach, pos );
			step->leaveAloneUntil = edgeRatios[ edgeOf ] < odds ? now + LEAVE_ALONE_FRAMES : now;
			continue;
		}
		const PhysicsBehavior *physics = obj->getPhysics();
		if( physics == NULL )
			continue;
		const Coord3D *velocity = physics->getVelocity();
		const Real speed = (Real)sqrt( sqr( velocity->x ) + sqr( velocity->y ) );
		if( speed < EDGE_STILL_SPEED )
			continue;
		const Real dirX = velocity->x / speed;
		const Real dirY = velocity->y / speed;

		// the nearest defence whose reach, and the slack round it, the next EDGE_LOOKAHEAD of its walk crosses;
		// one already inside a reach is in a fight, and doRetreats judges that
		Int entering = -1;
		Real enteringSqr = 0.0f;
		Bool inside = FALSE;
		for( size_t d = 0; d < defences.size() && !inside; ++d )
		{
			const Coord3D *at = defences[ d ].m_object->getPosition();
			const Real reach = defences[ d ].m_reach;
			const Real distSqr = sqr( at->x - pos->x ) + sqr( at->y - pos->y );
			inside = distSqr <= sqr( reach );
			if( inside || (Int)d == shelled || insideBase[ d ] || (entering >= 0 && distSqr >= enteringSqr) ||
					!aiWalkEntersReach( pos->x, pos->y, dirX, dirY, at->x, at->y, reach, reach + DEFENCE_FIGHT_SLACK, EDGE_LOOKAHEAD ) )
				continue;
			entering = (Int)d;
			enteringSqr = distSqr;
		}
		if( inside || entering < 0 )
			continue;

		const KnownDefence &gun = defences[ entering ];
		const Coord3D *at = gun.m_object->getPosition();
		const Real dist = (Real)sqrt( enteringSqr );
		const Real outX = (pos->x - at->x) / dist;
		const Real outY = (pos->y - at->y) / dist;
		const Real wait = gun.m_reach + DEFENCE_FIGHT_SLACK + EDGE_WAIT;
		if( walkingHome )
		{
			// round it: to where a line touches the circle EDGE_ROUND outside the edge, on the side towards home
			// that no other known gun covers, and home from there.  From inside that circle it first walks
			// straight out to the wait distance, since a line from in there to its side cuts across the reach
			const Real round = gun.m_reach + DEFENCE_FIGHT_SLACK + EDGE_ROUND;
			std::vector<Coord3D> path;
			Real from = dist;
			if( dist <= round )
			{
				Coord3D out;
				out.x = at->x + outX * wait;
				out.y = at->y + outY * wait;
				out.z = TheTerrainLogic->getGroundHeight( out.x, out.y );
				path.push_back( out );
				from = wait;
			}
			const Real touch = round / from;
			const Real side = (Real)sqrt( 1.0f - touch * touch );
			Coord3D best = m_baseCenter;
			Real bestSqr = -1.0f;
			for( Int turn = -1; turn <= 1; turn += 2 )
			{
				Coord3D spot;
				spot.x = at->x + round * (outX * touch - outY * side * turn);
				spot.y = at->y + round * (outY * touch + outX * side * turn);
				spot.z = TheTerrainLogic->getGroundHeight( spot.x, spot.y );
				const Real homeSqr = sqr( spot.x - m_baseCenter.x ) + sqr( spot.y - m_baseCenter.y );
				if( !otherDefenceCovers( defences, entering, spot.x, spot.y ) && (bestSqr < 0.0f || homeSqr < bestSqr) )
				{
					best = spot;
					bestSqr = homeSqr;
				}
			}
			if( bestSqr < 0.0f )
				continue;		// both sides under other guns: it keeps the walk it has
			path.push_back( best );
			path.push_back( m_baseCenter );
			ai->aiFollowPath( &path, NULL, CMD_FROM_SCRIPT );
			++around;
			continue;
		}

		if( edgeRatios[ entering ] < 0.0f )
			edgeRatios[ entering ] = edgeRatio( gun.m_object, gun.m_reach, pos );
		if( edgeRatios[ entering ] >= odds )
			continue;		// ours there outweigh it: in they go
		// out from the gun on its own side, further out while another gun covers the spot; home when every
		// spot along there is under one
		Coord3D spot = m_baseCenter;
		for( Int tries = 0; tries < EDGE_WAIT_TRIES; ++tries )
		{
			const Real out = wait + EDGE_WAIT * INT_TO_REAL( tries );
			const Real x = at->x + outX * out;
			const Real y = at->y + outY * out;
			if( !otherDefenceCovers( defences, entering, x, y ) )
			{
				spot.x = x;
				spot.y = y;
				break;
			}
		}
		spot.z = TheTerrainLogic->getGroundHeight( spot.x, spot.y );
		leaveTacticsAlone( obj->getID() );
		stepCalmly( obj, tacticalStepFor( obj->getID() ), &spot );
		++held;
	}
	if( held + around > 0 )
		DEBUG_LOG(("AI EDGE frame %d player %d holds %d at the edge of known defences, walks %d round them\n",
			now, m_player->getPlayerIndex(), held, around));
}

/** How the fight at a known gun's edge goes: fightRatio where a unit at from would walk into its reach. */
Real AIPlayer::edgeRatio( const Object *gun, Real reach, const Coord3D *from )
{
	const Coord3D *at = gun->getPosition();
	const Real dist = (Real)sqrt( sqr( from->x - at->x ) + sqr( from->y - at->y ) );
	Coord3D entry;
	entry.x = at->x + (from->x - at->x) / dist * reach;
	entry.y = at->y + (from->y - at->y) / dist * reach;
	entry.z = TheTerrainLogic->getGroundHeight( entry.x, entry.y );
	Real enemyHealth = 0.0f, enemyPower = 0.0f;
	return fightRatio( &entry, FALSE, &enemyHealth, &enemyPower, NULL );
}

/** Where a gun fires from, for the high ground's reach: an aircraft's range is flat
	* (Weapon_elevatedRange), so it counts as standing on the ground under it. */
static Real firingHeight( const Object *obj )
{
	const Coord3D *pos = obj->getPosition();
	if( obj->isKindOf( KINDOF_AIRCRAFT ) )
		return TheTerrainLogic->getGroundHeight( pos->x, pos->y );
	return pos->z;
}

//----------------------------------------------------------------------------------------------------------
/** B4, the map itself.  Every armed thing this player knows about is stamped onto the cells its guns
	* reach: the enemy's into one layer, this player's and its allies' into the other.  Known means
	* observerKnowsAbout, the same line every other decision here draws, so a gun it has never seen
	* does not steer it and a bunker it saw once keeps doing so. */
//----------------------------------------------------------------------------------------------------------
void AIPlayer::stampInfluence( AIInfluenceMap &map, const Object *obj )
{
	if( obj->isEffectivelyDead() || obj->isKindOf( KINDOF_PROJECTILE ) )
		return;
	const Bool mine = obj->getControllingPlayer() == m_player;
	const Relationship relation = m_player->getRelationship( obj->getTeam() );
	// the friendly layer is for the tactics alone, so the rungs below Hard do not pay for it
	if( (mine || relation == ALLIES) ? !getSkillProfile()->m_tacticalMicro : relation != ENEMIES )
		return;
	const Real power = aiCombatPower( obj );
	if( power <= 0.0f )
		return;
	const Real range = groundAttackRange( obj );
	if( range <= 0.0f )
		return;
	const Coord3D *pos = obj->getPosition();
	if( mine || relation == ALLIES )
		map.stampFriend( pos->x, pos->y, firingHeight( obj ), range, power );
	else if( observerKnowsAbout( obj, m_player->getPlayerIndex() ) )
		map.stampEnemy( pos->x, pos->y, firingHeight( obj ), range, power );
}

/** The guns it knows of only by their shells, where they stood at the last one. */
void AIPlayer::stampFireReports( AIInfluenceMap &map )
{
	const Int me = m_player->getPlayerIndex();
	for( size_t r = 0; r < m_fireReports.size(); ++r )
	{
		const FireReport &report = m_fireReports[ r ];
		Object *shooter = reportedShooter( report );
		if( shooter && !observerKnowsAbout( gunCarrier( shooter ), me ) )
			map.stampEnemy( report.at.x, report.at.y, report.at.z, report.reach, report.power );
	}
}

/** Stamping the whole map in one frame was 2 to 6 ms a player a second at minute 30, the largest of the
	* burst frames the AI owned.  So on Hard's clock each object stamps once a round on the frame its ID falls
	* on, into a map nobody reads, and that map takes over when the player's round ends: the map read is
	* whole, its oldest stamp up to two rounds old where it used to be one. */
void AIPlayer::updateInfluence( void )
{
	// the first look takes everything at once, so nothing reads a map that is not whole
	if( !m_influence.isBuilt() )
		rebuildInfluence();
	// the first round, or a save from before there were two; a map that starts mid-round is short of
	// objects and goes, and the first full round takes over
	if( !m_influenceNext.isSized() )
	{
		m_influenceNext = m_influence;
		m_influenceNext.clear();
		m_influenceNext.markUnbuilt();
	}

	const UnsignedInt now = TheGameLogic->getFrame();
	const Int round = rungFrames( getSkillProfile()->m_influenceSeconds );
	const UnsignedInt turn = now % (UnsignedInt)round;
	// ponytail: one walk of every object a player a frame, about 10000 ID tests at minute 30; one
	// shared list a frame if that ever shows beside the stamps
	for( Object *obj = TheGameLogic->getFirstObject(); obj; obj = obj->getNextObject() )
		if( (UnsignedInt)obj->getID() % (UnsignedInt)round == turn )
			stampInfluence( m_influenceNext, obj );

	if( (now + computeUpdatePhase( m_player->getPlayerIndex(), round )) % round == 0 )
	{
		if( m_influenceNext.isBuilt() )
		{
			stampFireReports( m_influenceNext );
			std::swap( m_influence, m_influenceNext );
			m_influence.markBuilt( now );
		}
		m_influenceNext.clear();
		m_influenceNext.markBuilt( now );
	}
}

/** The whole map on one frame: Hard's first look, and every draw below Hard. */
void AIPlayer::rebuildInfluence( void )
{
	if( !m_influence.isSized() )
	{
		Region3D extent;
		TheTerrainLogic->getExtent( &extent );
		m_influence.reset( extent.lo.x, extent.lo.y, extent.hi.x - extent.lo.x, extent.hi.y - extent.lo.y, INFLUENCE_CELL_SIZE );
		for( Int row = 0; row < m_influence.getRows(); ++row )
		{
			for( Int col = 0; col < m_influence.getCols(); ++col )
			{
				Real x, y;
				m_influence.cellCenter( col, row, &x, &y );
				m_influence.setCellHeight( col, row, TheTerrainLogic->getGroundHeight( x, y ) );
			}
		}
	}

	m_influence.clear();
	for( Object *obj = TheGameLogic->getFirstObject(); obj; obj = obj->getNextObject() )
		stampInfluence( m_influence, obj );
	stampFireReports( m_influence );
	m_influence.markBuilt( TheGameLogic->getFrame() );
}

/** Hard stamps its map over the frames of its clock (updateInfluence), and everything below Hard draws it
	* here, when it has something to read it for and it is older than the rung's m_influenceSeconds: a whole
	* draw costs 2 to 6 ms late in a match, and a quiet match has nothing to read it for.  -notactics stays off
	* the map altogether, and every read of one never drawn is nothing there. */
void AIPlayer::ensureInfluence( void )
{
	if( measuringWithoutTactics() )
		return;
	// ponytail: below Hard one whole walk of the objects on one frame, every two or four seconds at most while
	// something asks.  Stamp it over frames as Hard's if that ever shows in a tail
	const AIDifficultyProfile *profile = getSkillProfile();
	if( m_influence.isBuilt() && (profile->m_tacticalMicro ||
			TheGameLogic->getFrame() - m_influence.getBuiltFrame() < (UnsignedInt)rungFrames( profile->m_influenceSeconds )) )
		return;
	rebuildInfluence();
}

/** Every rung reads the map for where to stand now, and drawn is not enough to say the rest may: the lanes,
	* the drops and the shuttles' flights are Hard's, and were switched on by the map being there at all. */
Bool AIPlayer::hasTacticalMap( void ) const
{
	return getSkillProfile()->m_tacticalMicro && m_influence.isBuilt();
}

/** The first ground towards home that the map has no gun on and this gun, when given (x, y, reach), does not
	* reach, by aiRetreatFallbackPoint.  From home, where the road home goes nowhere, the way out is straight
	* away from the gun.  FALSE, with home or that far from the gun in spot, when nothing nearer is quiet, which
	* is no reason for a group busy with something else to walk home on one hit. */
Bool AIPlayer::quietGround( const Coord3D *from, const Real *gun, Coord3D *spot )
{
	ensureInfluence();
	Region3D map;
	TheTerrainLogic->getExtent( &map );
	const Real bounds[ 4 ] = { map.lo.x, map.lo.y, map.hi.x, map.hi.y };
	Coord3D toward = m_baseCenter;
	if( gun && isAtHome( from ) )
	{
		const Real away = (Real)sqrt( sqr( from->x - gun[ 0 ] ) + sqr( from->y - gun[ 1 ] ) );
		if( away > 1.0f )
		{
			toward.x = from->x + (from->x - gun[ 0 ]) / away * 2.0f * gun[ 2 ];
			toward.y = from->y + (from->y - gun[ 1 ]) / away * 2.0f * gun[ 2 ];
		}
	}
	const Bool found = aiRetreatFallbackPoint( from->x, from->y, toward.x, toward.y, INFLUENCE_CELL_SIZE, gun, gun ? 1 : 0, bounds, &spot->x, &spot->y, &m_influence );
	spot->z = TheTerrainLogic->getGroundHeight( spot->x, spot->y );
	return found;
}

//----------------------------------------------------------------------------------------------------------
AIPlayer::TacticalStep *AIPlayer::findTacticalStep( ObjectID unit )
{
	for( size_t i = 0; i < m_tactics.size(); ++i )
	{
		if( m_tactics[ i ].unit == unit )
			return &m_tactics[ i ];
	}
	return NULL;
}

AIPlayer::TacticalStep *AIPlayer::tacticalStepFor( ObjectID unit )
{
	TacticalStep *existing = findTacticalStep( unit );
	if( existing )
		return existing;
	TacticalStep step;
	step.unit = unit;
	step.target = INVALID_ID;
	step.resumeFrame = 0;
	step.nextClimbFrame = 0;
	step.leaveAloneUntil = 0;
	step.lastSeenFrame = TheGameLogic->getFrame();
	step.origin.zero();
	step.rejoin = FALSE;
	step.savedAttitude = AI_INVALID;
	step.lastKiteFrame = 0;
	step.fallingBack = FALSE;
	step.fallbackFrom.zero();
	step.fallbackFrame = 0;
	step.answerFrame = 0;
	m_tactics.push_back( step );
	return &m_tactics.back();
}

void AIPlayer::leaveTacticsAlone( ObjectID unit )
{
	TacticalStep *step = tacticalStepFor( unit );
	step->resumeFrame = 0;
	step->leaveAloneUntil = TheGameLogic->getFrame() + LEAVE_ALONE_FRAMES;
}

/** A computer player's teams are set aggressive, and an aggressive mood turns every plain move into
	* an attack move (AIMoveToState::update): the kite, the climb and the walk out of a lost fight would
	* turn round at the first enemy in reach and go back to shooting.  So the step is taken calm, and the
	* mood comes back when the step is over. */
void AIPlayer::stepCalmly( Object *obj, TacticalStep *step, const Coord3D *spot )
{
	AIUpdateInterface *ai = obj->getAI();
	if( step->savedAttitude == AI_INVALID )
	{
		// getAttitude is protected; the mood matrix carries the same answer
		switch( ai->getMoodMatrixValue() & MM_Mood_Bitmask )
		{
			case MM_Mood_Sleep:				step->savedAttitude = AI_SLEEP; break;
			case MM_Mood_Passive:			step->savedAttitude = AI_PASSIVE; break;
			case MM_Mood_Alert:				step->savedAttitude = AI_ALERT; break;
			case MM_Mood_Aggressive:	step->savedAttitude = AI_AGGRESSIVE; break;
			default:									step->savedAttitude = AI_NORMAL; break;
		}
	}
	ai->setAttitude( AI_NORMAL );
	// and given as an order rather than an AI's aside: a CMD_FROM_AI move to a unit that is busy is laid
	// over its attack as a temporary state (privateMoveToPosition), and the attack carries on under it
	// - the buggy that was told to back away drove on towards the tank it was shooting
	ai->aiMoveToPosition( spot, CMD_FROM_SCRIPT );
}

void AIPlayer::restoreMood( Object *obj, TacticalStep *step )
{
	if( step->savedAttitude == AI_INVALID )
		return;
	obj->getAI()->setAttitude( (AttitudeType)step->savedAttitude );
	step->savedAttitude = AI_INVALID;
}

//----------------------------------------------------------------------------------------------------------
/** A spot distance out from awayFrom, on the side the unit is on, that the ground lets it stand on and
	* from which mustReach is still inside reach (stretched by any height it gains).  Of the candidates,
	* the one the fewest known guns cover; among the ones about as quiet as that, the highest. */
//----------------------------------------------------------------------------------------------------------
Bool AIPlayer::pickTacticalSpot( const Object *obj, const Coord3D *from, const Coord3D *awayFrom, Real distance,
																 const Coord3D *mustReach, Real reach, Coord3D *spot )
{
	Real dx = from->x - awayFrom->x;
	Real dy = from->y - awayFrom->y;
	Real length = sqrt( dx * dx + dy * dy );
	if( length < 1.0f )
	{
		// standing on top of it: step towards home
		dx = m_baseCenter.x - from->x;
		dy = m_baseCenter.y - from->y;
		length = sqrt( dx * dx + dy * dy );
		if( length < 1.0f )
			return FALSE;
	}
	dx /= length;
	dy /= length;

	const Int CANDIDATES = 7;
	static const Real TURN[ CANDIDATES ] = { 0.0f, 0.45f, -0.45f, 0.9f, -0.9f, 1.35f, -1.35f };		// radians either side of straight back

	Bool found = FALSE;
	Real bestThreat = 0.0f;
	Real bestZ = 0.0f;
	for( Int i = 0; i < CANDIDATES; ++i )
	{
		const Real c = Cos( TURN[ i ] );
		const Real s = Sin( TURN[ i ] );
		Coord3D at;
		at.x = awayFrom->x + (dx * c - dy * s) * distance;
		at.y = awayFrom->y + (dx * s + dy * c) * distance;
		at.z = TheTerrainLogic->getGroundHeight( at.x, at.y );

		if( !groundLineClear( from, &at ) )
			continue;
		if( mustReach )
		{
			const Real reachFromThere = reach + Weapon_elevationRangeBonus( reach, at.z - mustReach->z );
			if( sqr( at.x - mustReach->x ) + sqr( at.y - mustReach->y ) > sqr( reachFromThere ) )
				continue;
		}
		const Real threat = m_influence.enemyAt( at.x, at.y );
		// about as quiet: within a tenth, or both nothing
		const Bool quieter = !found || threat < bestThreat * 0.9f;
		const Bool asQuietAndHigher = found && threat <= bestThreat * 1.1f && at.z > bestZ;
		if( quieter || asQuietAndHigher )
		{
			found = TRUE;
			bestThreat = threat;
			bestZ = at.z;
			*spot = at;
		}
	}
	return found;
}

//----------------------------------------------------------------------------------------------------------
/** Hard's units, fought one at a time from the influence map.  Deliberately at the player level: the
	* unit's own AI still aims, picks targets and drives; this only decides where it stands, on the
	* rung's own clock. */
//----------------------------------------------------------------------------------------------------------
/** -notactics, the measuring half of a batch: this player fights the way it did before the tactics,
	* the retreat and the approaches included. */
Bool AIPlayer::measuringWithoutTactics( void ) const
{
	// A skirmish or the playback of one, so a recording made with the switch plays back with it too
	// (given the switch again: it is a measuring aid and does not travel in the replay).
	const Int originalMode = (TheRecorder && TheRecorder->getMode() == RECORDERMODETYPE_PLAYBACK)
													 ? TheRecorder->getGameMode() : TheGameLogic->getGameMode();
	return TheGlobalData->m_noTacticsSlotParity >= 0 && originalMode == GAME_SKIRMISH &&
		(ThePlayerList->getSlotIndex( m_player->getPlayerIndex() ) & 1) == TheGlobalData->m_noTacticsSlotParity;
}

/** Those of the player's objects whose turn in the look falls on this frame. */
struct FireLookTurn
{
	UnsignedInt look;
	UnsignedInt frameOfLook;
	std::vector<Object *> units;
};

static void collectFireLookTurn( Object *obj, void *userData )
{
	FireLookTurn *turn = (FireLookTurn *)userData;
	if( (UnsignedInt)obj->getID() % turn->look == turn->frameOfLook )
		turn->units.push_back( obj );
}

void AIPlayer::doTactics( void )
{
	if( measuringWithoutTactics() )
		return;
	const UnsignedInt now = TheGameLogic->getFrame();
	const Int me = m_player->getPlayerIndex();
	const Bool tactics = getSkillProfile()->m_tacticalMicro;

	if( tactics )
		updateInfluence();

	answerFire();

	// each unit once every TACTICS_RATE frames, on the frame its ID falls on, not a whole army on one
	if( tactics && m_baseCenterSet )
	{
		FireLookTurn turn;
		turn.look = TACTICS_RATE;
		turn.frameOfLook = now % turn.look;
		m_player->iterateObjects( collectFireLookTurn, &turn );
		for( size_t i = 0; i < turn.units.size(); ++i )
			tacticsFor( turn.units[ i ] );
	}

	if( (now + computeUpdatePhase( me, TACTICS_RATE )) % TACTICS_RATE != 0 )
		return;

	// ponytail: a linear list searched per unit, a few hundred rows at most; a map keyed on the ID if
	// armies ever grow past that
	for( size_t i = 0; i < m_tactics.size(); )
	{
		Object *unit = TheGameLogic->findObjectByID( m_tactics[ i ].unit );
		const TacticalStep &row = m_tactics[ i ];
		// with the tactics running a row goes when the unit has not been looked at for a while; without
		// them the only rows are walks home from a retreat, which end when the walk does.  One holding
		// after a retreat is doFallback's until it sends it back or home, and doFallback does that by the
		// longest hold; one still holding well past it has left for a team doFallback never looks at (a
		// base defence), and gets its mood back here rather than stand calm and uncalled for good.
		// A rung that does not retreat and the default team have no doFallback to send on one that left a gun's
		// ground: let go, it is idle and the hunt and the next call-up have it.
		const Bool done = (unit == NULL || unit->isEffectivelyDead()) ? TRUE :
			row.fallingBack ? aiHolderLetGo( getSkillProfile()->m_retreatTtkRatio > 0.0f && unit->getTeam() != m_player->getDefaultTeam(),
				now - row.fallbackFrame, unit->getAI() && unit->getAI()->isMoving(), FIRE_HOLD_FRAMES, FALLBACK_STALE_FRAMES ) :
			row.answerFrame != 0 ? FALSE :		// finishAnswers ends these
			tactics ? now - row.lastSeenFrame > TACTICAL_ROW_EXPIRY_FRAMES :
			now >= row.leaveAloneUntil && (unit->getAI() == NULL || !unit->getAI()->isMoving() || now >= row.leaveAloneUntil + WALK_HOME_MAX_FRAMES);
		if( done )
		{
			if( row.fallingBack && unit && !unit->isEffectivelyDead() )
				DEBUG_LOG(("AI RETREAT frame %d player %d '%s' still holding after %d s, let go\n", now, me,
					unit->getTemplate()->getName().str(), (now - row.fallbackFrame) / LOGICFRAMES_PER_SECOND));
			// one that is still alive (home, in a tunnel, garrisoned) gets the mood its team gave it back
			if( unit && unit->getAI() )
				restoreMood( unit, &m_tactics[ i ] );
			m_tactics[ i ] = m_tactics.back();
			m_tactics.pop_back();
		}
		else
			++i;
	}
}

//----------------------------------------------------------------------------------------------------------
/** Twice the widest superweapon blast, 210 for the Neutron Missile and 200 for a Scud Storm's. */
static const Real STRIKE_DODGE_REACH = 450.0f;

void AIPlayer::noteStrikeLaunched( const Player *launcher )
{
	if( m_strikeNoticeFrame == 0 && runsOwnAI() && getSkillProfile()->m_strikeDodgeSeconds > 0.0f &&
			m_player->getRelationship( launcher->getDefaultTeam() ) == ENEMIES )
		m_strikeNoticeFrame = TheGameLogic->getFrame();
}

struct StrikeDodgers
{
	const AIPlayer *owner;
	ObjectID ferry;
	std::vector<Object *> ships;
};

static void collectStrikeDodgers( Object *obj, void *userData )
{
	StrikeDodgers *found = (StrikeDodgers *)userData;
	if( !found->owner->isGunshipAircraft( obj ) || found->owner->isTransportChinook( obj ) || obj->isContained() || obj->getID() == found->ferry )
		return;
	found->ships.push_back( obj );
}

/** Six Comanches hovering in a base died to a nuclear missile with five seconds of warning, four of them on Medium
	* and all six on the other rungs, and to a Scud Storm with ten.  Every seat hears a launch announced.  The aim is never said, its marker shows
	* only to the player firing, so the helicopters leave wherever they are, spread out from each other, and take
	* the retreat's own row: nothing calls a holder up, the hold's clock brings it back to the ground it left.  One
	* launch anywhere costs each a few seconds of hovering somewhere else.  Nothing here runs without a launch.
	* ponytail: ground units stay, too slow to clear a blast, and a jet flies its own loop; a visible missile's
	* heading would say more, at the cost of a walk over the projectiles every launch */
void AIPlayer::dodgeStrike( void )
{
	if( m_strikeNoticeFrame == 0 || TheGameLogic->getFrame() - m_strikeNoticeFrame < (UnsignedInt)rungFrames( getSkillProfile()->m_strikeDodgeSeconds ) )
		return;
	m_strikeNoticeFrame = 0;

	StrikeDodgers found;
	found.owner = this;
	found.ferry = m_ferryID;
	m_player->iterateObjects( collectStrikeDodgers, &found );
	if( found.ships.empty() )
		return;

	Region3D map;
	TheTerrainLogic->getExtent( &map );
	const Real bounds[ 4 ] = { map.lo.x, map.lo.y, map.hi.x, map.hi.y };
	const UnsignedInt now = TheGameLogic->getFrame();
	std::vector<Coord3D> spots( found.ships.size() );
	for( size_t s = 0; s < found.ships.size(); ++s )
	{
		const Coord3D *pos = found.ships[ s ]->getPosition();
		Real sumX = 0.0f, sumY = 0.0f;
		Int group = 0;
		for( size_t o = 0; o < found.ships.size(); ++o )
		{
			const Coord3D *other = found.ships[ o ]->getPosition();
			if( sqr( other->x - pos->x ) + sqr( other->y - pos->y ) > sqr( STRIKE_DODGE_REACH ) )
				continue;
			sumX += other->x;
			sumY += other->y;
			++group;
		}
		aiStrikeDodgeSpot( pos->x, pos->y, sumX / group, sumY / group, STRIKE_DODGE_REACH, bounds, (Int)found.ships[ s ]->getID(), &spots[ s ].x, &spots[ s ].y );
		spots[ s ].z = TheTerrainLogic->getGroundHeight( spots[ s ].x, spots[ s ].y );
	}
	for( size_t s = 0; s < found.ships.size(); ++s )
	{
		Object *ship = found.ships[ s ];
		const Coord3D origin = *ship->getPosition();
		leaveTacticsAlone( ship->getID() );
		TacticalStep *step = findTacticalStep( ship->getID() );
		if( !step->fallingBack )
			step->fallbackFrame = now;
		step->fallingBack = TRUE;
		step->fallbackFrom = origin;
		step->answerFrame = 0;
		step->rejoin = FALSE;
		stepCalmly( ship, step, &spots[ s ] );
	}
	DEBUG_LOG(("AI STRIKE frame %d player %d flies %d helicopters off on the word of a launch\n", now, m_player->getPlayerIndex(), (Int)found.ships.size()));
}

//----------------------------------------------------------------------------------------------------------
/** How often the loaded helicopters are looked at, and how far back from the fight they may land. */
static const Int TRANSPORT_CHECK_RATE = LOGICFRAMES_PER_SECOND / 2;
static const Int DROP_BACKOFF_CELLS = 8;

struct LoadedTransports
{
	ObjectID ferry;
	const AIPlayer *owner;
	std::vector<Object *> ships;
};

static void findLoadedTransport( Object *obj, void *userData )
{
	LoadedTransports *found = (LoadedTransports *)userData;
	const ContainModuleInterface *contain = obj->getContain();
	if( !found->owner->isGunshipAircraft( obj ) || obj->isContained() || obj->getID() == found->ferry || contain == NULL || contain->getContainCount() == 0 )
		return;
	found->ships.push_back( obj );
}

/** A helicopter that went with a wave carrying riders who cannot shoot out of it puts them down at the
	* edge of the fight: the first cell on its way home that nothing known covers, from the moment the
	* influence map says it is over ground the enemy's guns reach.  Once they are out and standing they
	* go on towards the enemy the wave was sent at. */
void AIPlayer::doTransports( void )
{
	if( (TheGameLogic->getFrame() + computeUpdatePhase( m_player->getPlayerIndex(), TRANSPORT_CHECK_RATE )) % TRANSPORT_CHECK_RATE != 0 )
		return;

	Coord3D enemyPos;
	Player *enemy = getAiEnemy();
	const Bool haveEnemy = enemy && enemyStartGuess( enemy->getPlayerIndex(), &enemyPos );
	for( size_t i = 0; i < m_droppedRiders.size(); )
	{
		Object *rider = TheGameLogic->findObjectByID( m_droppedRiders[ i ] );
		if( rider && !rider->isEffectivelyDead() && (rider->isContained() || rider->getAI() == NULL || !rider->getAI()->isIdle()) )
		{
			++i;
			continue;
		}
		if( rider && !rider->isEffectivelyDead() && haveEnemy )
			rider->getAI()->aiAttackMoveToPosition( &enemyPos, NO_MAX_SHOTS_LIMIT, CMD_FROM_AI );
		m_droppedRiders[ i ] = m_droppedRiders.back();
		m_droppedRiders.pop_back();
	}
	// the dropped riders above include the transport Chinooks' cargo on every rung; the helicopters'
	// own drops below are Hard's, with its tactics running: nothing else loads the wave's infantry
	if( !hasTacticalMap() )
		return;

	LoadedTransports found;
	found.ferry = m_ferryID;
	found.owner = this;
	m_player->iterateObjects( findLoadedTransport, &found );
	for( size_t s = 0; s < found.ships.size(); ++s )
	{
		Object *ship = found.ships[ s ];
		const Coord3D *pos = ship->getPosition();
		if( m_influence.enemyAt( pos->x, pos->y ) <= 0.0f )
			continue;		// not at the fight yet

		ContainModuleInterface *contain = ship->getContain();
		std::vector<ObjectID> cargo;
		Bool dropping = FALSE;
		const ContainedItemsList *items = contain->getContainedItemsList();
		for( ContainedItemsList::const_iterator it = items->begin(); it != items->end(); ++it )
		{
			const Object *rider = *it;
			if( !rider->isKindOf( KINDOF_INFANTRY ) || contain->isPassengerAllowedToFire( rider->getID() ) )
				continue;		// a gunship's riders fight from it; the Helix's own gun mount is not cargo
			if( std::find( m_droppedRiders.begin(), m_droppedRiders.end(), rider->getID() ) != m_droppedRiders.end() )
				dropping = TRUE;
			cargo.push_back( rider->getID() );
		}
		if( cargo.empty() || dropping )
			continue;

		Coord3D drop = *pos;
		const Real homeX = m_baseCenter.x - pos->x;
		const Real homeY = m_baseCenter.y - pos->y;
		const Real homeDist = sqrt( homeX * homeX + homeY * homeY );
		for( Int back = 1; back <= DROP_BACKOFF_CELLS && homeDist > 1.0f; ++back )
		{
			drop.x = pos->x + homeX / homeDist * back * INFLUENCE_CELL_SIZE;
			drop.y = pos->y + homeY / homeDist * back * INFLUENCE_CELL_SIZE;
			if( m_influence.enemyAt( drop.x, drop.y ) <= 0.0f )
				break;
		}
		drop.z = TheTerrainLogic->getGroundHeight( drop.x, drop.y );
		DEBUG_LOG(("AI TRANSPORT frame %d player %d drops %d riders from '%s' at (%.0f,%.0f)\n", TheGameLogic->getFrame(),
			m_player->getPlayerIndex(), (Int)cargo.size(), ship->getTemplate()->getName().str(), drop.x, drop.y));
		// an order rather than an AI's aside, or it is laid under the wave's path and never happens
		ship->getAI()->aiMoveToAndEvacuate( &drop, CMD_FROM_SCRIPT );
		m_droppedRiders.insert( m_droppedRiders.end(), cargo.begin(), cargo.end() );
	}
}

//----------------------------------------------------------------------------------------------------------
void AIPlayer::spotterStandOff( Object *obj, TacticalStep *step )
{
	AIUpdateInterface *ai = obj->getAI();
	const Coord3D *pos = obj->getPosition();
	const UnsignedInt now = TheGameLogic->getFrame();
	const Real sight = obj->getVisionRange();

	Object *threat = NULL;
	Real threatReach = 0.0f;
	Real threatDistSqr = 0.0f;
	PartitionFilterPlayerAffiliation enemies( m_player, ALLOW_ENEMIES, true );
	PartitionFilterAlive alive;
	PartitionFilter *filters[] = { &enemies, &alive, NULL };
	ObjectIterator *iter = ThePartitionManager->iterateObjectsInRange( pos, sight, FROM_CENTER_2D, filters );
	MemoryPoolObjectHolder hold( iter );
	for( Object *enemy = iter->first(); enemy; enemy = iter->next() )
	{
		if( enemy->getControllingPlayer() == m_player || m_player->getRelationship( enemy->getTeam() ) != ENEMIES )
			continue;
		if( enemy->isKindOf( KINDOF_PROJECTILE ) || !observerKnowsAbout( enemy, m_player->getPlayerIndex() ) )
			continue;
		const Real enemyRange = groundAttackRange( enemy );
		if( enemyRange <= 0.0f )
			continue;
		const Real reach = enemyRange + Weapon_elevationRangeBonus( enemyRange, firingHeight( enemy ) - pos->z );
		const Real distSqr = sqr( enemy->getPosition()->x - pos->x ) + sqr( enemy->getPosition()->y - pos->y );
		if( distSqr > sqr( reach + KITE_WATCH_MARGIN + ai->getCurLocomotorSpeed() * KITE_TURN_FRAMES ) )
			continue;
		if( threat == NULL || distSqr < threatDistSqr )
		{
			threat = enemy;
			threatReach = reach;
			threatDistSqr = distSqr;
		}
	}
	if( threat == NULL )
		return;

	const Real standoff = min( threatReach + KITE_MARGIN, sight );
	if( threatDistSqr >= sqr( standoff - KITE_MARGIN ) )
		return;		// already standing off: a step would only slide it sideways
	Coord3D spot;
	if( !pickTacticalSpot( obj, pos, threat->getPosition(), standoff, threat->getPosition(), sight, &spot ) )
		return;
	DEBUG_LOG(("AI TACTICS frame %d player %d keeps spotter '%s' out of '%s' reach %.0f, (%.0f,%.0f) to (%.0f,%.0f)\n",
		now, m_player->getPlayerIndex(), obj->getTemplate()->getName().str(), threat->getTemplate()->getName().str(), threatReach,
		pos->x, pos->y, spot.x, spot.y));
	// when the step is over it attack-moves to the origin; the spot, so it does not walk back in
	step->target = INVALID_ID;
	step->origin = spot;
	const Real speed = max( ai->getCurLocomotorSpeed(), 0.1f );
	const Real walk = sqrt( sqr( spot.x - pos->x ) + sqr( spot.y - pos->y ) );
	step->resumeFrame = now + min<UnsignedInt>( KITE_STEP_MAX_FRAMES, KITE_TURN_FRAMES + REAL_TO_INT_CEIL( walk / speed ) );
	step->rejoin = TRUE;
	stepCalmly( obj, step, &spot );
}

//----------------------------------------------------------------------------------------------------------
/** Who takes the orders the tactics and the answers to fire give, and who is left to what it is doing. */
//----------------------------------------------------------------------------------------------------------
Bool AIPlayer::fightsOnItsOwn( Object *obj )
{
	// armed, not "able to attack" this frame: that reads false while a clip reloads, which is exactly
	// when a Rocket Buggy has to step away, and it took every buggy out of here the moment it had fired
	if( obj->isEffectivelyDead() || obj->isContained() || groundAttackRange( obj ) <= 0.0f )
		return FALSE;
	if( obj->isKindOf( KINDOF_STRUCTURE ) || obj->isKindOf( KINDOF_IMMOBILE ) || obj->isKindOf( KINDOF_AIRCRAFT ) ||
			obj->isKindOf( KINDOF_DOZER ) || obj->isKindOf( KINDOF_HARVESTER ) || obj->isKindOf( KINDOF_PROJECTILE ) )
		return FALSE;
	AIUpdateInterface *ai = obj->getAI();
	if( ai == NULL || ai->hasTunnelTrip() || ai->getCurLocomotor() == NULL )
		return FALSE;		// a gun that cannot move (a Helix's gattling mount) has no step to take
	// base defence stays where it was put, and the scouts, the capturer and the hijacker have their jobs
	Team *team = obj->getTeam();
	if( team == NULL || obj->isKindOf( KINDOF_MONEY_HACKER ) )
		return FALSE;
	const TeamTemplateInfo *info = team->getPrototype()->getTemplateInfo();
	if( info && (info->m_isBaseDefense || info->m_isPerimeterDefense) )
		return FALSE;
	// nor a patient on its way into an Ambulance: past leaveTacticsAlone's ten seconds the pull-out and the
	// rejoin walked 2 of 4 such patients off in six Hard USA mirrors on Twilight Flame (2026-10-11)
	// nor one pulled out to be mended: waiting its turn at an Arms Dealer's dock it stood still, and the
	// rejoin walked it back to the fight still hurt
	if( obj->getID() == m_capturerID || obj->getID() == m_hijackerID || isDutyHelix( obj ) || isDutyAmbulance( obj ) ||
			isWalkingToAmbulance( obj ) || isOutToMend( obj ) )
		return FALSE;
	for( Int i = 0; i < MAX_AI_SCOUTS; ++i )
	{
		if( obj->getID() == m_scoutID[ i ] )
			return FALSE;
	}
	return TRUE;
}

//----------------------------------------------------------------------------------------------------------
void AIPlayer::tacticsFor( Object *obj )
{
	if( !fightsOnItsOwn( obj ) )
		return;
	AIUpdateInterface *ai = obj->getAI();
	Team *team = obj->getTeam();
	const Coord3D *pos = obj->getPosition();
	const UnsignedInt now = TheGameLogic->getFrame();
	const Real threatHere = m_influence.enemyAt( pos->x, pos->y );
	Object *victim = ai->getCurrentVictim();
	const TacticalStep *existing = findTacticalStep( obj->getID() );
	if( threatHere <= 0.0f && victim == NULL &&
			(existing == NULL || (existing->resumeFrame == 0 && !existing->rejoin && existing->savedAttitude == AI_INVALID)) )
		return;		// nothing can shoot it and it is shooting nothing: nothing to decide

	TacticalStep *step = tacticalStepFor( obj->getID() );
	step->lastSeenFrame = now;
	if( now < step->leaveAloneUntil )
		return;
	// home from a lost fight: its own mood again once the walk is over, and not before, or the mood turns
	// what is left of the walk into an attack move.  One holding at a safe spot stays calm until it is sent
	// back, or its mood walks it into the fight it was pulled out of.
	if( step->resumeFrame == 0 && !step->fallingBack && (!ai->isMoving() || now >= step->leaveAloneUntil + WALK_HOME_MAX_FRAMES) )
		restoreMood( obj, step );

	// a step under way ends when it gets there or runs out of time, and the unit goes back to work:
	// the thing it was shooting if that still stands, otherwise back up the road it stepped off
	if( step->resumeFrame != 0 )
	{
		if( now < step->resumeFrame && ai->isMoving() )
			return;
		step->resumeFrame = 0;
		restoreMood( obj, step );
		Object *target = TheGameLogic->findObjectByID( step->target );
		if( target && !target->isEffectivelyDead() && observerKnowsAbout( target, m_player->getPlayerIndex() ) )
			ai->aiAttackObject( target, NO_MAX_SHOTS_LIMIT, CMD_FROM_AI );
		else
			ai->aiAttackMoveToPosition( &step->origin, NO_MAX_SHOTS_LIMIT, CMD_FROM_AI );
		return;
	}

	// a step replaced the order the team gave it, so once the fight it stepped for is over it is standing
	// on its own in the middle of the map; it goes back to the rest of its team rather than wait there
	// to be picked off
	if( step->rejoin && victim == NULL && threatHere <= 0.0f && !ai->isMoving() )
	{
		step->rejoin = FALSE;
		Coord3D centre;
		centre.zero();
		Real count = 0.0f;
		for( DLINK_ITERATOR<Object> iter = team->iterate_TeamMemberList(); !iter.done(); iter.advance() )
		{
			const Object *mate = iter.cur();
			if( mate == NULL || mate == obj || mate->isEffectivelyDead() || mate->isContained() )
				continue;
			centre.x += mate->getPosition()->x;
			centre.y += mate->getPosition()->y;
			count += 1.0f;
		}
		if( count > 0.0f )
		{
			centre.x /= count;
			centre.y /= count;
			centre.z = TheTerrainLogic->getGroundHeight( centre.x, centre.y );
			ai->aiAttackMoveToPosition( &centre, NO_MAX_SHOTS_LIMIT, CMD_FROM_AI );
		}
		return;
	}

	const Real myRange = groundAttackRange( obj );
	if( myRange <= 0.0f )
		return;

	// a hurt unit on ground it cannot win goes home, where the next wave will pick it up; one already
	// at home has nowhere better to be and fights where it stands
	const BodyModuleInterface *body = obj->getBodyModule();
	const Real friendsHere = m_influence.friendAt( pos->x, pos->y );
	if( body && body->getHealth() < body->getMaxHealth() * HURT_HEALTH_SHARE && threatHere > friendsHere && !isAtHome( pos ) )
	{
		DEBUG_LOG(("AI TACTICS frame %d player %d pulls a hurt '%s' out, %.0f against %.0f\n", now, m_player->getPlayerIndex(),
			obj->getTemplate()->getName().str(), threatHere, friendsHere));
		// out to the first ground on the way home that nothing known covers, not all the way home: on a
		// big map the walk home crossed the ground it was pulled out of, and it stays near enough to
		// guard the road behind the fight
		Coord3D safe = m_baseCenter;
		const Real homeX = m_baseCenter.x - pos->x;
		const Real homeY = m_baseCenter.y - pos->y;
		const Real homeDist = sqrt( homeX * homeX + homeY * homeY );
		for( Real along = INFLUENCE_CELL_SIZE; along < homeDist; along += INFLUENCE_CELL_SIZE )
		{
			const Real x = pos->x + homeX * along / homeDist;
			const Real y = pos->y + homeY * along / homeDist;
			if( m_influence.enemyAt( x, y ) <= 0.0f )
			{
				safe.x = x + homeX * INFLUENCE_CELL_SIZE / homeDist;		// a cell past the edge of the reach
				safe.y = y + homeY * INFLUENCE_CELL_SIZE / homeDist;
				safe.z = TheTerrainLogic->getGroundHeight( safe.x, safe.y );
				break;
			}
		}
		stepCalmly( obj, step, &safe );
		step->rejoin = TRUE;
		step->leaveAloneUntil = now + LEAVE_ALONE_FRAMES;
		return;
	}

	// a spotter is the eyes of the guns behind it, and its carbine is for show: it stays just out of
	// reach of every armed enemy it can see and keeps that enemy in sight, rather than trade shots
	if( obj->getTemplate()->hasSpotterSight() )
	{
		spotterStandOff( obj, step );
		return;
	}

	// kite: the nearest thing that is about to reach it and that it outranges
	Object *closing = NULL;
	Real closingRange = 0.0f;
	Real closingDistSqr = 0.0f;
	Object *nearestArmed = NULL;			// the closest armed thing in this unit's reach, whatever else it is
	Real nearestArmedDistSqr = 0.0f;
	{
		PartitionFilterPlayerAffiliation enemies( m_player, ALLOW_ENEMIES, true );
		PartitionFilterAlive alive;
		PartitionFilter *filters[] = { &enemies, &alive, NULL };
		ObjectIterator *iter = ThePartitionManager->iterateObjectsInRange( pos, myRange, FROM_CENTER_2D, filters );
		MemoryPoolObjectHolder hold( iter );
		for( Object *enemy = iter->first(); enemy; enemy = iter->next() )
		{
			// the affiliation filter lets the player's own objects through whatever it is asked for
			if( enemy->getControllingPlayer() == m_player || m_player->getRelationship( enemy->getTeam() ) != ENEMIES )
				continue;
			if( enemy->isKindOf( KINDOF_PROJECTILE ) || !observerKnowsAbout( enemy, m_player->getPlayerIndex() ) )
				continue;
			const Real enemyRange = groundAttackRange( enemy );
			if( enemyRange <= 0.0f )
				continue;
			const Real reach = enemyRange + Weapon_elevationRangeBonus( enemyRange, firingHeight( enemy ) - pos->z );
			const Real distSqr = sqr( enemy->getPosition()->x - pos->x ) + sqr( enemy->getPosition()->y - pos->y );
			// and only one it can shoot: without this a Battlemaster "turned on" every Helix overhead,
			// 410 times in two matches, and drove after aircraft its gun cannot reach
			if( (nearestArmed == NULL || distSqr < nearestArmedDistSqr) &&
					obj->getAbleToAttackSpecificObject( ATTACK_NEW_TARGET, enemy, CMD_FROM_AI ) == ATTACKRESULT_POSSIBLE )
			{
				nearestArmed = enemy;
				nearestArmedDistSqr = distSqr;
			}
			// watched from as far out as the unit drives while it turns round, since a vehicle cannot
			// reverse: a buggy that started its turn at the enemy's reach finished it inside
			if( distSqr > sqr( reach + KITE_WATCH_MARGIN + ai->getCurLocomotorSpeed() * KITE_TURN_FRAMES ) )
				continue;		// not close enough to hurt yet, nor soon
			// running from something faster only means not shooting at it; a gun that cannot move is
			// always worth standing off from
			AIUpdateInterface *enemyAI = enemy->getAI();
			const Bool enemyMoves = enemyAI && enemyAI->getCurLocomotor() && !enemy->isKindOf( KINDOF_IMMOBILE ) && !enemy->isKindOf( KINDOF_STRUCTURE );
			const Real enemySpeed = enemyMoves ? enemyAI->getCurLocomotorSpeed() : 0.0f;
			if( enemySpeed > ai->getCurLocomotorSpeed() )
				continue;
			// a gun that moves is worth stepping away from while it is fighting this player or already
			// reaches this unit; one still out of reach and busy with somebody else is shot, not run from.
			// Four tanks sent at one buggy of four drove through the other three.
			const Object *enemyVictim = enemyAI ? enemyAI->getCurrentVictim() : NULL;
			const Bool comingForUs = enemyVictim && enemyVictim->getControllingPlayer() == m_player;
			if( enemyMoves && !comingForUs && distSqr > sqr( reach ) )
				continue;
			// a gun that walks is run from only by something that outranges it by a long way, a rocket
			// launcher against a tank; a tank that hopped back from another tank's slightly shorter gun
			// spent the hop turning round, and on Twilight Flame kiting like that lost more than it saved
			if( enemyMoves && myRange < reach * KITE_MOBILE_RANGE_RATIO )
				continue;
			// and only from something worth running from: a tank that backs away from a rifleman it
			// could have driven over was the commonest step the first version took
			if( obj->getCrusherLevel() > enemy->getCrushableLevel() || aiCombatPower( enemy ) < KITE_WORTHY_SHARE * aiCombatPower( obj ) )
				continue;
			if( closing == NULL || distSqr < closingDistSqr )
			{
				closing = enemy;
				closingRange = reach;
				closingDistSqr = distSqr;
			}
		}
	}
	// fire, then step: a gun that is ready or aiming shoots first, and the hop happens while it reloads.
	// Hopping on the clock alone had a Tomahawk step back every second and never finish a launch.
	// Once the enemy's gun already reaches it, it steps whatever its own gun is doing: a Tomahawk that
	// waited to finish aiming while four tanks drove up to it died aiming.
	const Weapon *gun = obj->getCurrentWeapon();
	// ... and only when what it is shooting at is in reach; a gun that is ready while it drives towards a
	// target further off is not about to fire, and waiting on it let a buggy drive into the tanks
	const Bool gunBusy = gun && (gun->getStatus() == READY_TO_FIRE || gun->getStatus() == PRE_ATTACK) && victim != NULL &&
		sqr( victim->getPosition()->x - pos->x ) + sqr( victim->getPosition()->y - pos->y ) <= sqr( myRange );
	const Bool alreadyHit = closing && closingDistSqr <= sqr( closingRange );
	// Only what can aim without driving: infantry, or a gun on a turret.  A Rocket Buggy's rack is fixed
	// to the chassis and a car cannot turn on the spot, so every hop cost it a three-second circle to
	// face the tank again, and it fired less kiting than standing.
	const Bool aimsWithoutDriving = obj->isKindOf( KINDOF_INFANTRY ) || ai->getWhichTurretForCurWeapon() != TURRET_INVALID;
	Real standoff = 0.0f;
	if( aimsWithoutDriving && closing && (alreadyHit || !gunBusy) && AIKite_standoffDistance( myRange, closingRange, KITE_MARGIN, &standoff ) &&
			closingDistSqr < sqr( standoff - KITE_MARGIN ) )		// already standing off: a step would only slide it sideways
	{
		Coord3D spot;
		if( pickTacticalSpot( obj, pos, closing->getPosition(), standoff, closing->getPosition(), myRange, &spot ) )
		{
			// back to shooting the gun it stepped away from, which the spot keeps in reach; going back to a
			// target further off drove the buggies through the tanks in front of it to get there
			step->target = closing->getID();
			DEBUG_LOG(("AI TACTICS frame %d player %d kites '%s' back from '%s', %.0f outranging %.0f, (%.0f,%.0f) to (%.0f,%.0f) away from (%.0f,%.0f)\n",
				now, m_player->getPlayerIndex(), obj->getTemplate()->getName().str(), closing->getTemplate()->getName().str(), myRange, closingRange,
				pos->x, pos->y, spot.x, spot.y, closing->getPosition()->x, closing->getPosition()->y));
			step->origin = *pos;
			// long enough to turn round and drive there; a vehicle given one second did not finish the turn
			step->lastKiteFrame = now;
			const Real speed = max( ai->getCurLocomotorSpeed(), 0.1f );
			const Real walk = sqrt( sqr( spot.x - pos->x ) + sqr( spot.y - pos->y ) );
			step->resumeFrame = now + min<UnsignedInt>( KITE_STEP_MAX_FRAMES, KITE_TURN_FRAMES + REAL_TO_INT_CEIL( walk / speed ) );
			step->rejoin = TRUE;
			stepCalmly( obj, step, &spot );
			return;
		}
	}

	// A target out of reach is walked to, through whatever is in reach on the way.  Four buggies sent at
	// the last of four tanks drove past the other three with their rockets loaded and died without one
	// shot fired.  With an armed enemy already in reach, that is the one it shoots.
	if( victim && nearestArmed && nearestArmed != victim &&
			sqr( victim->getPosition()->x - pos->x ) + sqr( victim->getPosition()->y - pos->y ) > sqr( myRange ) )
	{
		DEBUG_LOG(("AI TACTICS frame %d player %d turns '%s' on the '%s' in reach instead of walking to its target\n", now,
			m_player->getPlayerIndex(), obj->getTemplate()->getName().str(), nearestArmed->getTemplate()->getName().str()));
		ai->aiAttackObject( nearestArmed, NO_MAX_SHOTS_LIMIT, CMD_FROM_AI );
		return;
	}

	/* The high ground.  A climb is worth its walk only when it changes the exchange: the target can hit
		 this unit where it stands, and from a rise nearby this unit reaches the target while the target no
		 longer reaches it.  Climbing whenever a rise was near measured worse on Twilight Flame than not
		 climbing at all - every walk mid-fight is time not shooting, and its ramps made the walk long. */
	const Real victimRange = victim ? groundAttackRange( victim ) : 0.0f;
	const Real victimReachHere = victimRange + (victim ? Weapon_elevationRangeBonus( victimRange, firingHeight( victim ) - pos->z ) : 0.0f);
	const Bool victimHitsMe = victim && victimRange > 0.0f &&
		sqr( victim->getPosition()->x - pos->x ) + sqr( victim->getPosition()->y - pos->y ) <= sqr( victimReachHere );
	if( victimHitsMe && !ai->isMoving() && now >= step->nextClimbFrame )
	{
		step->nextClimbFrame = now + CLIMB_INTERVAL_FRAMES;
		const Int DIRECTIONS = 8;
		Coord3D best;
		Bool found = FALSE;
		Real bestZ = pos->z + CLIMB_MIN_GAIN;
		for( Int ring = 1; ring <= 2; ++ring )
		{
			const Real radius = CLIMB_SEARCH_RADIUS * ring / 2.0f;
			for( Int d = 0; d < DIRECTIONS; ++d )
			{
				const Real angle = 2.0f * PI * d / DIRECTIONS;
				Coord3D at;
				at.x = pos->x + radius * Cos( angle );
				at.y = pos->y + radius * Sin( angle );
				at.z = TheTerrainLogic->getGroundHeight( at.x, at.y );
				if( at.z < bestZ )
					continue;
				ICoord2D cell;
				TheAI->pathfinder()->worldToCell( &at, &cell );
				const PathfindCell *pathCell = TheAI->pathfinder()->getCell( LAYER_GROUND, cell.x, cell.y );
				if( pathCell == NULL || pathCell->getType() != PathfindCell::CELL_CLEAR )
					continue;
				const Coord3D *vpos = victim->getPosition();
				const Real reachFromThere = myRange + Weapon_elevationRangeBonus( myRange, at.z - vpos->z );
				const Real victimReachThere = victimRange + Weapon_elevationRangeBonus( victimRange, firingHeight( victim ) - at.z );
				const Real distSqr = sqr( at.x - vpos->x ) + sqr( at.y - vpos->y );
				if( distSqr > sqr( reachFromThere ) || distSqr <= sqr( victimReachThere + KITE_MARGIN ) )
					continue;		// out of its own reach, or still inside the target's
				if( m_influence.enemyAt( at.x, at.y ) > threatHere || !groundLineClear( pos, &at ) )
					continue;
				best = at;
				bestZ = at.z;
				found = TRUE;
			}
		}
		if( found )
		{
			DEBUG_LOG(("AI TACTICS frame %d player %d takes '%s' %.0f up onto (%.0f,%.0f)\n", now, m_player->getPlayerIndex(),
				obj->getTemplate()->getName().str(), best.z - pos->z, best.x, best.y));
			step->target = victim->getID();
			step->origin = *pos;
			step->resumeFrame = now + TACTICAL_STEP_MAX_FRAMES;
			step->rejoin = TRUE;
			stepCalmly( obj, step, &best );
		}
	}
}

//----------------------------------------------------------------------------------------------------------
/** How long a unit sent at a gun takes another hit on the odds that keep it going, which are lower than the
	* odds that sent it.  Reading each hit afresh had it turn round halfway across, every time what covered the
	* gun changed by one tank. */
static const UnsignedInt FIRE_COMMIT_FRAMES = 20 * LOGICFRAMES_PER_SECOND;
/** The longest a unit counts as being on that errand: it has gone off on some other order by then. */
static const UnsignedInt FIRE_ANSWER_MAX_FRAMES = 60 * LOGICFRAMES_PER_SECOND;
/** How far under a rung's odds one on its way to a gun has to read before it breaks off (aiFireOddsNow). */
static const Real FIRE_RETURN_MARGIN = 1.25f;

/** Whether this shoots at things on the ground: a unit's own gun, or what a building has on it and in it. */
static Bool shootsGround( const Object *obj )
{
	Real reach = 0.0f;
	if( obj->isKindOf( KINDOF_STRUCTURE ) || obj->isKindOf( KINDOF_IMMOBILE ) )
		return fixedGunPower( obj, &reach ) > 0.0f;
	return groundAttackRange( obj ) > 0.0f;
}

/** The enemy gun that hit this within the last window frames, or NULL.  Not an aircraft: nothing out-walks
	* one, and what can shoot it picks it up by itself.  Not a superweapon, whose reach is the whole map, nor a
	* mine.  A long gun hitting from past sight stands at its launcher's place in the damage record, and
	* getClearableLastAttacker would clear it for the unit's own retaliation. */
static Object *lastShooter( const Object *obj, const Player *player, UnsignedInt now, UnsignedInt window )
{
	const BodyModuleInterface *body = obj->getBodyModule();
	if( body == NULL || !aiRecentlyHit( body->getLastDamageTimestamp(), now, window ) )
		return NULL;
	Object *shooter = TheGameLogic->findObjectByID( body->getLastDamageInfo()->in.m_sourceID );
	if( shooter == NULL || shooter->isEffectivelyDead() || shooter->isKindOf( KINDOF_PROJECTILE ) || gunCarrier( shooter )->isKindOf( KINDOF_AIRCRAFT ) ||
			shooter->isKindOf( KINDOF_FS_SUPERWEAPON ) || shooter->isKindOf( KINDOF_MINE ) || player->getRelationship( shooter->getTeam() ) != ENEMIES )
		return NULL;
	return groundAttackRange( shooter ) > 0.0f ? shooter : NULL;
}

Object *AIPlayer::reportedShooter( const FireReport &report ) const
{
	if( TheGameLogic->getFrame() - report.frame > (UnsignedInt)rungFrames( getSkillProfile()->m_fireMemorySeconds ) )
		return NULL;
	Object *shooter = TheGameLogic->findObjectByID( report.shooter );
	return shooter && !shooter->isEffectivelyDead() && m_player->getRelationship( shooter->getTeam() ) == ENEMIES ? shooter : NULL;
}

/** A gun measureFight meets by itself, known and near what it hit, needs no report. */
void AIPlayer::noteFire( const Object *shooter, const Object *gun, const Coord3D *hitAt )
{
	const Coord3D *at = gun->getPosition();
	const Bool known = observerKnowsAbout( gun, m_player->getPlayerIndex() );
	if( known && sqr( at->x - hitAt->x ) + sqr( at->y - hitAt->y ) <= sqr( RETREAT_ENGAGEMENT_RADIUS ) )
		return;
	for( size_t r = 0; r < m_fireReports.size(); ++r )
	{
		if( m_fireReports[ r ].shooter == shooter->getID() )
		{
			m_fireReports[ r ].at = *at;
			m_fireReports[ r ].hitAt = *hitAt;
			m_fireReports[ r ].frame = TheGameLogic->getFrame();
			return;
		}
	}
	FireReport report;
	report.shooter = shooter->getID();
	report.at = *at;
	report.hitAt = *hitAt;
	report.reach = groundAttackRange( shooter );
	report.power = aiCombatPower( shooter );
	report.frame = TheGameLogic->getFrame();
	m_fireReports.push_back( report );
	// on the map from now, not from the next time it is drawn
	if( !known )
		m_influence.stampEnemy( at->x, at->y, at->z, report.reach, report.power );
}

Bool AIPlayer::isAnsweringFire( ObjectID unit )
{
	const TacticalStep *step = findTacticalStep( unit );
	return step != NULL && step->answerFrame != 0;
}

/** Those that went for a gun and now stand idle, the gun dead or gone: back to the building they had been
	* shooting, or the ground they left. */
void AIPlayer::finishAnswers( void )
{
	const UnsignedInt now = TheGameLogic->getFrame();
	for( size_t i = 0; i < m_tactics.size(); ++i )
	{
		TacticalStep &row = m_tactics[ i ];
		if( row.answerFrame == 0 )
			continue;
		Object *unit = TheGameLogic->findObjectByID( row.unit );
		// idle at a safe spot is not done with the fight
		if( unit == NULL || unit->isEffectivelyDead() || unit->getAI() == NULL ||
				aiFireAnswerOver( row.fallbackFrame != 0, row.rejoin, now - row.answerFrame, FIRE_ANSWER_MAX_FRAMES ) )
		{
			row.answerFrame = 0;
			continue;
		}
		row.lastSeenFrame = now;
		if( !unit->getAI()->isIdle() )
			continue;
		row.answerFrame = 0;
		Object *target = TheGameLogic->findObjectByID( row.target );
		if( target && !target->isEffectivelyDead() && observerKnowsAbout( target, m_player->getPlayerIndex() ) &&
				unit->getAbleToAttackSpecificObject( ATTACK_NEW_TARGET, target, CMD_FROM_AI ) != ATTACKRESULT_NOT_POSSIBLE )
		{
			unit->getAI()->aiAttackObject( target, NO_MAX_SHOTS_LIMIT, CMD_FROM_AI );
			continue;
		}
		// ponytail: a wave goes back to where it was hit and the idle team call-up sends it on, not its old road;
		// keep the order it was on in the row if the walk back shows
		if( sqr( row.origin.x - unit->getPosition()->x ) + sqr( row.origin.y - unit->getPosition()->y ) > sqr( INFLUENCE_CELL_SIZE ) )
			unit->getAI()->aiAttackMoveToPosition( &row.origin, NO_MAX_SHOTS_LIMIT, CMD_FROM_AI );
	}
}

static Bool lowerObjectID( const Object *a, const Object *b )
{
	return a->getID() < b->getID();
}

/** In ObjectID order: the partition's own order differs after a load, and orders and sums here decide things. */
void AIPlayer::fightersNear( const Coord3D *at, Real radius, std::vector<Object *> *fighters )
{
	PartitionFilterAlive filterAlive;
	PartitionFilterOnMap filterOnMap;
	PartitionFilter *filters[] = { &filterAlive, &filterOnMap, 0 };
	MemoryPoolObjectHolder hold;
	SimpleObjectIterator *nearby = ThePartitionManager->iterateObjectsInRange( at, radius, FROM_CENTER_2D, filters );
	hold.hold( nearby );
	for( Object *e = nearby->first(); e; e = nearby->next() )
		if( e->getControllingPlayer() == m_player && fightsOnItsOwn( e ) )
			fighters->push_back( e );
	std::sort( fighters->begin(), fighters->end(), lowerObjectID );
}

/** The gun's reading, once a look: what it would take to get it, by the map. */
const AIPlayer::GunReading &AIPlayer::readGun( const Object *obj, const Object *gun )
{
	for( size_t i = 0; i < m_gunReadings.size(); ++i )
		if( m_gunReadings[ i ].gun == gun->getID() )
			return m_gunReadings[ i ];

	ensureInfluence();
	const Coord3D *at = gun->getPosition();
	const Bool fixed = gun->isKindOf( KINDOF_STRUCTURE ) || gun->isKindOf( KINDOF_IMMOBILE );
	Real reach = 0.0f;
	GunReading reading;
	reading.gun = gun->getID();
	reading.frame = TheGameLogic->getFrame();
	// the map is up to the rung's clock old, and the gun itself is always there
	reading.cover = max<Real>( m_influence.enemyAt( at->x, at->y ), fixed ? fixedGunPower( gun, &reach ) : aiCombatPower( gun ) );
	reading.ours = 0.0f;
	std::vector<AIVisibleEnemy> mine, theirs;
	std::vector<Object *> fighters;
	fightersNear( obj->getPosition(), RETREAT_ENGAGEMENT_RADIUS, &fighters );
	for( size_t f = 0; f < fighters.size(); ++f )
	{
		if( templateFramesToKill( fighters[ f ]->getTemplate(), gun->getTemplate() ) < 0.0f )
			continue;
		reading.ours += aiCombatPower( fighters[ f ] );
		addToArmy( &mine, fighters[ f ] );
	}
	// by unit against unit as well as price, where the rung weighs fights that way: Red Guards are worth an
	// Inferno Cannon by the money, and burn
	if( getSkillProfile()->m_engageGate )
	{
		addToArmy( &theirs, gun );
		reading.ours *= (Real)sqrt( armyAdvantage( mine, theirs ) );
	}
	// a building's own cell is no place to walk to, its reach is what counts; and the owner's rule for a defence
	// holds whatever fired first, nobody walks into one short of the rung's odds at its edge (holdAtDefences).
	// Being shelled by it sent a parked team of four up to five Fire Bases, the first of them all it knew of
	reading.edge = fixed ? edgeRatio( gun, reach, obj->getPosition() ) : 0.0f;
	m_gunReadings.push_back( reading );
	return m_gunReadings.back();
}

/** One unit hit, one answer, by aiAnswerFire, and the order that goes with it. */
AIFireAnswer AIPlayer::answerHit( Object *obj, Object *shooter, Object *gun )
{
	AIUpdateInterface *ai = obj->getAI();
	const UnsignedInt now = TheGameLogic->getFrame();
	const StateID state = ai->getCurrentStateID();
	if( state == AI_DOCK || state == AI_ENTER || state >= 1000 )
		return AIFIRE_CARRY_ON;
	const TacticalStep *row = findTacticalStep( obj->getID() );
	Bool committed = FALSE;
	if( row )
	{
		// a kite or a climb under way
		if( row->resumeFrame != 0 )
			return AIFIRE_CARRY_ON;
		committed = row->answerFrame != 0 && now - row->answerFrame < FIRE_COMMIT_FRAMES;
		// on its way out, by this or by the retreat
		if( row->fallbackFrame == now || (ai->isMoving() && (row->fallingBack || now < row->leaveAloneUntil ||
				(row->fallbackFrame != 0 && now - row->fallbackFrame < RETREAT_HOME_WALK_FRAMES))) )
			return AIFIRE_CARRY_ON;
	}
	const Coord3D *pos = obj->getPosition();
	const Coord3D *at = gun->getPosition();
	const Object *victim = ai->getCurrentVictim();
	const Bool onTheGun = victim == gun || victim == shooter;
	const Bool fightingAGun = !onTheGun && victim && shootsGround( victim );
	const Bool canShootNow = !onTheGun && !fightingAGun && obj->getAbleToAttackSpecificObject( ATTACK_NEW_TARGET, gun, CMD_FROM_AI ) == ATTACKRESULT_POSSIBLE;
	const Bool fixed = gun->isKindOf( KINDOF_STRUCTURE ) || gun->isKindOf( KINDOF_IMMOBILE );
	const Bool holding = row && row->fallingBack;
	Real ours = 0.0f;
	Real cover = 0.0f;
	Bool canGetAtIt = FALSE;
	if( !onTheGun && !fightingAGun && !canShootNow )
	{
		const GunReading &reading = readGun( obj, gun );
		ours = reading.ours;
		cover = reading.cover;
		// a holder goes back when the hold says; a spotter's carbine is for show, the guns it spots for are what
		// answers this; and a gun nobody can see cannot be shot however close they walk
		canGetAtIt = !holding && !obj->getTemplate()->hasSpotterSight() && templateFramesToKill( obj->getTemplate(), gun->getTemplate() ) >= 0.0f &&
			(!gun->testStatus( OBJECT_STATUS_STEALTHED ) || gun->testStatus( OBJECT_STATUS_DETECTED )) &&
			(fixed ? reading.edge >= aiFireOddsNow( EDGE_ODDS[ m_skillLevel ], committed, FIRE_RETURN_MARGIN ) :
				TheAI->pathfinder()->clientSafeQuickDoesPathExist( ai->getLocomotorSet(), pos, at ));
	}
	const AIDifficultyProfile *profile = getSkillProfile();
	const Real distance = (Real)sqrt( sqr( at->x - pos->x ) + sqr( at->y - pos->y ) );
	const Bool mayLeave = aiFireMayLeave( isAtHome( pos ), isAtHome( at ), profile->m_retreatTtkRatio > 0.0f,
		observerKnowsAbout( gun, m_player->getPlayerIndex() ), distance, RETREAT_ENGAGEMENT_RADIUS );
	// the guns round the first one show up one hit at a time
	const AIFireAnswer answer = aiAnswerFire( onTheGun, fightingAGun, canShootNow, canGetAtIt, ours, cover,
		aiFireOddsNow( profile->m_fireChargeOdds, committed, FIRE_RETURN_MARGIN ), mayLeave, ai->isIdle() );
	if( answer == AIFIRE_CARRY_ON || (committed && answer != AIFIRE_LEAVE) )
		return AIFIRE_CARRY_ON;

	if( answer == AIFIRE_LEAVE )
	{
		const Real gunTriple[ 3 ] = { at->x, at->y, groundAttackRange( shooter ) };
		Coord3D spot;
		// with nothing quiet near, one with an order stays on it and one standing idle under the shells still goes
		if( (!quietGround( pos, gunTriple, &spot ) && !ai->isIdle()) || sqr( spot.x - pos->x ) + sqr( spot.y - pos->y ) < sqr( INFLUENCE_CELL_SIZE ) )
			return AIFIRE_CARRY_ON;		// nowhere better
		DEBUG_LOG(("AI FIRE frame %d player %d '%s' %d hit by '%s' %.0f away leaves (%.0f,%.0f) for (%.0f,%.0f), %.0f against %.0f\n", now, m_player->getPlayerIndex(),
			obj->getTemplate()->getName().str(), obj->getID(), shooter->getTemplate()->getName().str(), distance, pos->x, pos->y, spot.x, spot.y, ours, cover));
		leaveTacticsAlone( obj->getID() );
		TacticalStep *step = findTacticalStep( obj->getID() );
		if( !step->fallingBack )
			step->fallbackFrame = now;		// the hold's clock, which moving on does not start again
		step->fallingBack = TRUE;
		// going back to it means going for the gun, not onto its ground; home for one that has tried that
		step->fallbackFrom = aiFireMayGoBack( committed ) ? *at : m_baseCenter;
		step->answerFrame = 0;
		step->rejoin = FALSE;
		stepCalmly( obj, step, &spot );
		return answer;
	}

	DEBUG_LOG(("AI FIRE frame %d player %d '%s' %d hit by '%s' %.0f away %s from (%.0f,%.0f), %.0f against %.0f\n", now, m_player->getPlayerIndex(),
		obj->getTemplate()->getName().str(), obj->getID(), shooter->getTemplate()->getName().str(), distance,
		answer == AIFIRE_SHOOT_BACK ? "shoots back" : "goes for it", pos->x, pos->y, ours, cover));
	// shot from where it holds, on the hold's clock: taken off the hold, the next pass pulled it out again
	if( holding )
	{
		ai->aiAttackObject( gun, NO_MAX_SHOTS_LIMIT, CMD_FROM_AI );
		return answer;
	}
	TacticalStep *step = tacticalStepFor( obj->getID() );
	if( step->answerFrame == 0 )
	{
		step->target = victim ? victim->getID() : INVALID_ID;
		step->origin = *pos;
	}
	step->answerFrame = now;
	step->lastSeenFrame = now;
	step->rejoin = FALSE;
	step->fallingBack = FALSE;
	step->fallbackFrame = 0;
	restoreMood( obj, step );
	// a building it can aim at is aimed at, or it takes up the building it was shooting again on the way.  A
	// gun out on the map is walked at, and whatever is in reach on the way is shot
	if( answer == AIFIRE_SHOOT_BACK || (fixed && obj->getAbleToAttackSpecificObject( ATTACK_NEW_TARGET, gun, CMD_FROM_AI ) != ATTACKRESULT_NOT_POSSIBLE) )
		ai->aiAttackObject( gun, NO_MAX_SHOTS_LIMIT, CMD_FROM_AI );
	else
		ai->aiAttackMoveToPosition( at, NO_MAX_SHOTS_LIMIT, CMD_FROM_AI );
	return answer;
}

//----------------------------------------------------------------------------------------------------------
/** The owner:"bu ai lar artik su influence map i falan biraz ciddiye alsinlar, hedef aliniyorlar hala
	* devam ediyorlar ayni yerde."  A unit idle or parked under a long gun only acquires what it can see, a
	* wave on its road only what its mood looks for, and one shooting a building stays on the building with
	* a tower on it, and none of the rest of the AI asked who was hitting them.  So once in each of the rung's
	* looks each unit hit lately answers the gun that hit it: shoots it if it can; goes for it with whatever is
	* near it if the map says ours can take what covers its spot; and otherwise leaves that ground for ground
	* the map has quiet, and holds there.  The rest of the AI is told as well: the gun goes on the map and into
	* the fights around it even from the fog (noteFire).
	*
	* A unit is looked at on the frame its ID comes up, so a look costs one share of the owner's objects a frame
	* and a stamp read for each.  What surrounds a gun is read once a look, and that first read is the one that
	* tells the units round it; the map is drawn only if something needs it. */
//----------------------------------------------------------------------------------------------------------
void AIPlayer::answerFire( void )
{
	const AIDifficultyProfile *profile = getSkillProfile();
	const UnsignedInt now = TheGameLogic->getFrame();
	const Int look = rungFrames( profile->m_fireLookSeconds );
	if( !m_baseCenterSet )
		return;

	for( size_t i = 0; i < m_gunReadings.size(); )
	{
		if( now - m_gunReadings[ i ].frame < (UnsignedInt)look )
		{
			++i;
			continue;
		}
		m_gunReadings[ i ] = m_gunReadings.back();
		m_gunReadings.pop_back();
	}
	if( (now + computeUpdatePhase( m_player->getPlayerIndex(), look )) % look == 0 )
	{
		for( size_t r = 0; r < m_fireReports.size(); )
		{
			if( reportedShooter( m_fireReports[ r ] ) )
			{
				++r;
				continue;
			}
			m_fireReports[ r ] = m_fireReports.back();
			m_fireReports.pop_back();
		}
		finishAnswers();
	}

	FireLookTurn turn;
	turn.look = (UnsignedInt)look;
	turn.frameOfLook = now % turn.look;
	m_player->iterateObjects( collectFireLookTurn, &turn );
	std::sort( turn.units.begin(), turn.units.end(), lowerObjectID );
	for( size_t o = 0; o < turn.units.size(); ++o )
	{
		Object *obj = turn.units[ o ];
		if( obj->isEffectivelyDead() )
			continue;
		Object *shooter = lastShooter( obj, m_player, now, (UnsignedInt)look );
		if( shooter == NULL )
			continue;
		Object *gun = gunCarrier( shooter );
		noteFire( shooter, gun, obj->getPosition() );		// a building's or a worker's hit tells it where the gun is too
		if( !fightsOnItsOwn( obj ) )
			continue;
		const size_t known = m_gunReadings.size();
		const Object *victim = obj->getAI()->getCurrentVictim();
		const Bool onTheGun = victim == gun || victim == shooter;
		answerHit( obj, shooter, gun );
		// one already shooting the gun has no need of a reading, and went alone for want of one
		if( onTheGun )
			readGun( obj, gun );
		// the first unit to read this gun tells those around it, and what it read holds for them
		if( m_gunReadings.size() == known || profile->m_fireWordRadius <= 0.0f )
			continue;
		std::vector<Object *> fighters;
		fightersNear( obj->getPosition(), profile->m_fireWordRadius, &fighters );
		for( size_t f = 0; f < fighters.size(); ++f )
			if( fighters[ f ] != obj )
				answerHit( fighters[ f ], shooter, gun );
	}
}

//----------------------------------------------------------------------------------------------------------
/** The cheapest thing this player could build that can walk somewhere and is not needed elsewhere.
	* Faction-agnostic on purpose - it lands on the Ranger, the Red Guard, the Rebel and the
	* Technical without a per-side table to keep, and on whatever a mod's cheapest unit is. */
static const ThingTemplate *cheapestScoutTemplate( Player *player )
{
	const ThingTemplate *best = NULL;
	Int bestCost = 0;

	for( const ThingTemplate *t = TheThingFactory->firstTemplate(); t; t = t->friend_getNextTemplate() )
	{
		if( !t->isKindOf( KINDOF_INFANTRY ) && !t->isKindOf( KINDOF_VEHICLE ) )
			continue;
		if( t->isKindOf( KINDOF_DOZER ) || t->isKindOf( KINDOF_HARVESTER ) )
			continue;		// those have a job
		if( t->isKindOf( KINDOF_AIRCRAFT ) || t->isKindOf( KINDOF_IMMOBILE ) )
			continue;
		if( !isOwnArmy( player, t ) || !player->canBuild( t ) )
			continue;

		Int cost = t->calcCostToBuild( player );
		if( cost <= 0 )
			continue;
		if( best == NULL || cost < bestCost )
		{
			best = t;
			bestCost = cost;
		}
	}
	return best;
}

//----------------------------------------------------------------------------------------------------------
/**
 * A unit of ours that is free to go and look at things.  The default team is where anything built
 * outside the team system lands - the scout queued below, and the odd leftover - so nothing here is
 * taken off a team that has a mission.
 */
Object *AIPlayer::findScout( void )
{
	Team *team = m_player->getDefaultTeam();
	if( team == NULL )
		return NULL;

	for( DLINK_ITERATOR<Object> iter = team->iterate_TeamMemberList(); !iter.done(); iter.advance() )
	{
		Object *obj = iter.cur();
		if( obj == NULL || obj->isEffectivelyDead() )
			continue;
		//
		// A positive test, not a list of exclusions: the default team also holds everything the
		// player has in flight, and a missile is mobile, carries an AIUpdate and belongs to no
		// team - the first draft of this went scouting with a Patriot missile several times a
		// minute, and never with anything that could actually go and look.
		//
		if( !obj->isKindOf( KINDOF_INFANTRY ) && !obj->isKindOf( KINDOF_VEHICLE ) )
			continue;
		if( obj->isKindOf( KINDOF_PROJECTILE ) || obj->isKindOf( KINDOF_IMMOBILE ) )
			continue;
		if( obj->isKindOf( KINDOF_DOZER ) || obj->isKindOf( KINDOF_HARVESTER ) )
			continue;		// the economy is not a scout
		if( obj->isKindOf( KINDOF_AIRCRAFT ) )
			continue;
		if( obj->getAI() == NULL )
			continue;
		if( obj->getID() == m_capturerID || obj->getID() == m_hijackerID || isDutyAmbulance( obj ) )
			continue;		// it has a job; two owners giving one unit orders is how both jobs stall

		Bool alreadyScouting = FALSE;
		for( Int slot = 0; slot < MAX_AI_SCOUTS; ++slot )
			if( m_scoutID[ slot ] == obj->getID() )
				alreadyScouting = TRUE;
		if( alreadyScouting )
			continue;

		return obj;
	}
	return NULL;
}

//----------------------------------------------------------------------------------------------------------
Bool AIPlayer::scoutInQueue( void )
{
	for( DLINK_ITERATOR<TeamInQueue> iter = iterate_TeamBuildQueue(); !iter.done(); iter.advance() )
	{
		TeamInQueue *team = iter.cur();
		if( team == NULL )
			continue;
		for( WorkOrder *order = team->m_workOrders; order; order = order->m_next )
			if( order->m_isScout )
				return true;
	}
	return false;
}

//----------------------------------------------------------------------------------------------------------
/**
 * Put one cheap unit on order to do the looking.  Same shape as queueDozer: a one-order priority
 * team on the player's default team, so it never disturbs the team the AI is massing.
 */
void AIPlayer::queueScout( void )
{
	queueSupportUnit( cheapestScoutTemplate( m_player ), "SCOUT" );
}

//----------------------------------------------------------------------------------------------------------
/**
 * Put one cheap unit on order for a job outside the team system - the scout, the capturer.  Same
 * shape as queueDozer: a one-order priority team on the player's default team, so it never disturbs
 * the team the AI is massing, and it waits for spare change so it can never be what stops a base
 * going up.  One at a time, whichever job asked first.
 */
void AIPlayer::queueSupportUnit( const ThingTemplate *tTemplate, const char *what )
{
	if( tTemplate == NULL || scoutInQueue() )
		return;

	// scouting must never be what stops a base going up, so it waits for spare change
	if( m_player->getMoney()->countMoney() < 2 * tTemplate->calcCostToBuild( m_player ) )
		return;

	Bool canBuildUnits = m_player->getCanBuildUnits();
	m_player->setCanBuildUnits( true );

	Object *factory = findFactory( tTemplate, true );
	if( factory )
	{
		WorkOrder *order = newInstance(WorkOrder);
		order->m_thing = tTemplate;
		order->m_factoryID = INVALID_ID;
		order->m_numRequired = 1;
		order->m_required = true;
		order->m_isResourceGatherer = FALSE;
		order->m_isScout = TRUE;
		order->m_next = NULL;

		TeamInQueue *team = newInstance(TeamInQueue);
		prependTo_TeamBuildQueue( team );
		team->m_priorityBuild = true;
		team->m_workOrders = order;
		team->m_frameStarted = TheGameLogic->getFrame();
		team->m_team = m_player->getDefaultTeam();

		AsciiString msg = what;
		msg.concat( " - building one at the " );
		msg.concat( factory->getTemplate()->getName() );
		TheScriptEngine->AppendDebugMessage( msg, false );

		m_teamDelay = 0;
		startTraining( order, team->m_priorityBuild, team->m_team->getName() );
	}

	m_player->setCanBuildUnits( canBuildUnits );
}

//----------------------------------------------------------------------------------------------------------
/** How often the start-position picture is brought up to date.  doScouting is called every frame and
	* then sits behind its own timer for up to a rung's scouting interval, which would be far too slow
	* to notice a scout arriving somewhere. */
static const Int START_INTEL_RATE = LOGICFRAMES_PER_SECOND;

//----------------------------------------------------------------------------------------------------------
/** The three numbers the odds are made of: enemies still in the game, positions known to hold one,
	* and positions nobody has looked at yet. */
void AIPlayer::countStartIntel( Int *enemies, Int *occupied, Int *unchecked ) const
{
	*enemies = *occupied = *unchecked = 0;
	if( ThePlayerList == NULL )
		return;

	const Int playerCount = ThePlayerList->getPlayerCount();
	for( Int i = 0; i < playerCount && i < MAX_PLAYER_COUNT; ++i )
	{
		Player *p = ThePlayerList->getNthPlayer( i );
		if( p == NULL || p == m_player || !p->isPlayerActive() )
			continue;
		if( m_player->getRelationship( p->getDefaultTeam() ) == ENEMIES )
			++(*enemies);
	}

	for( Int startNdx = 0; startNdx < MAX_PLAYER_COUNT; ++startNdx )
	{
		Coord3D there;
		if( !startPositionLoc( startNdx, &there ) )
			continue;						// the map does not have this position at all
		if( m_startOccupied[ startNdx ] )
			++(*occupied);
		else if( !m_startChecked[ startNdx ] )
			++(*unchecked);
	}
}

//----------------------------------------------------------------------------------------------------------
/**
 * Keep the picture of who is where up to date, and deduce whatever the picture already implies.
 *
 * Two things happen here.  A scout standing within sight of a start position answers that position:
 * an enemy is standing on it, or it is empty and crossed off for good.  And then the arithmetic:
 * every position known to hold an enemy accounts for one of them, so what is left is unlocated
 * enemies over unchecked positions.  1v3 on an eight-position map starts at 3/7 and rises to 3/6 and
 * 3/5 as the empty ones are crossed off.
 *
 * The moment it reaches 1 there is nothing left to find out - the remaining positions all hold an
 * enemy, and no walk can say anything a subtraction has not already said.  A two-player map is that
 * same case at its first step: one enemy, one position, 1/1, settled before a scout is even built.
 * The scouts are then free to go and look at what is standing there, which is the part that never
 * stops being worth knowing.
 */
void AIPlayer::updateStartIntel( void )
{
	const UnsignedInt now = TheGameLogic->getFrame();
	if( m_startIntelFrame != 0 && now < m_startIntelFrame + START_INTEL_RATE )
		return;
	m_startIntelFrame = now;

	if( ThePlayerList == NULL )
		return;

	const Int playerCount = ThePlayerList->getPlayerCount();

	//
	// Seed: where we are, and where our allies are, is not something anyone has to go and find out.
	// It also takes those positions out of the candidate list, which is what makes the 1v3 example
	// seven candidates rather than eight.
	//
	for( Int i = 0; i < playerCount && i < MAX_PLAYER_COUNT; ++i )
	{
		Player *p = ThePlayerList->getNthPlayer( i );
		if( p == NULL || m_playerStartNdx[ i ] >= 0 )
			continue;
		//
		// ALLIES, not "anything that is not an enemy": the neutral and civilian players are neither,
		// and they report start index 0.  Letting them through crossed off the map's first start
		// position for every AI that was not itself standing on it - so whoever did start there was
		// permanently believed to be somewhere else.
		//
		if( p != m_player && m_player->getRelationship( p->getDefaultTeam() ) != ALLIES )
			continue;

		const Int startNdx = p->getMpStartIndex();
		if( startNdx >= 0 && startNdx < MAX_PLAYER_COUNT )
		{
			m_playerStartNdx[ i ] = startNdx;
			m_startChecked[ startNdx ] = TRUE;
		}
	}

	//
	// What the scouts can see from where they are standing.  This is the entire cost of the
	// information: a unit had to walk there.
	//
	for( Int slot = 0; slot < MAX_AI_SCOUTS; ++slot )
	{
		Object *scout = TheGameLogic->findObjectByID( m_scoutID[ slot ] );
		if( scout == NULL || scout->isEffectivelyDead() )
			continue;

		const Coord3D *at = scout->getPosition();
		Real see = scout->getVisionRange();
		if( see < 1.0f )
			see = 1.0f;

		for( Int startNdx = 0; startNdx < MAX_PLAYER_COUNT; ++startNdx )
		{
			//
			// A deduced position is checked without anyone knowing whose it is - the elimination says
			// an enemy is there, not which one.  So this keeps looking at positions already crossed
			// off until each has a name against it; only an empty one, or one already pinned to a
			// player, is finished with.
			//
			Bool pinned = FALSE;
			for( Int i = 0; i < MAX_PLAYER_COUNT; ++i )
				if( m_playerStartNdx[ i ] == startNdx )
					pinned = TRUE;
			if( m_startChecked[ startNdx ] && (pinned || !m_startOccupied[ startNdx ]) )
				continue;

			Coord3D there;
			if( !startPositionLoc( startNdx, &there ) )
				continue;

			const Real dx = there.x - at->x;
			const Real dy = there.y - at->y;
			if( dx*dx + dy*dy > see*see )
				continue;			// not close enough to say anything about it

			m_startChecked[ startNdx ] = TRUE;
			m_scoutSeenFrame[ startNdx ] = now;

			for( Int i = 0; i < playerCount && i < MAX_PLAYER_COUNT; ++i )
			{
				Player *p = ThePlayerList->getNthPlayer( i );
				if( p == NULL || p == m_player || p->getMpStartIndex() != startNdx )
					continue;
				if( m_player->getRelationship( p->getDefaultTeam() ) != ENEMIES )
					continue;

				m_startOccupied[ startNdx ] = TRUE;
				m_playerStartNdx[ i ] = startNdx;			// and now we know whose it is, not just that it is someone's
				DEBUG_LOG(("AI player %d found player %d at start position %d\n",
									 m_player->getPlayerIndex(), i, startNdx + 1));
			}
		}
	}

	// the elimination: as many enemies left to place as there are places left to put them
	Int enemies = 0, occupied = 0, unchecked = 0;
	countStartIntel( &enemies, &occupied, &unchecked );
	if( aiStartOccupiedOdds( enemies - occupied, unchecked ) < 1.0f )
		return;

	for( Int startNdx = 0; startNdx < MAX_PLAYER_COUNT; ++startNdx )
	{
		Coord3D there;
		if( m_startChecked[ startNdx ] || !startPositionLoc( startNdx, &there ) )
			continue;

		//
		// Deduced, not seen: we know an enemy is here without knowing which one, and that is enough
		// to attack it and enough to stop searching for it.  Which of them it is gets settled by the
		// first unit that arrives, above.
		//
		m_startChecked[ startNdx ] = TRUE;
		m_startOccupied[ startNdx ] = TRUE;
		DEBUG_LOG(("AI player %d deduced an enemy at start position %d without going there\n",
							 m_player->getPlayerIndex(), startNdx + 1));
	}
}

//----------------------------------------------------------------------------------------------------------
/**
 * The best address this AI has for an enemy, which is not always his real one.
 *
 * Found him: his position.  Not found him: the nearest position deduced to hold *an* enemy, and
 * failing that the nearest one nobody has looked at.  It never returns somewhere already crossed off
 * as empty, and it never returns nothing while there is anywhere left he could be - a fogged AI with
 * no coordinates at all sends every attack team to the map corner.
 *
 * Attacking the best guess is also how the guess gets corrected: the team that arrives can see.
 */
Bool AIPlayer::enemyStartGuess( Int playerNdx, Coord3D *pos )
{
	if( playerNdx >= 0 && playerNdx < MAX_PLAYER_COUNT && m_playerStartNdx[ playerNdx ] >= 0 )
		return startPositionLoc( m_playerStartNdx[ playerNdx ], pos );

	Coord3D from = m_baseCenter;
	if( !m_baseCenterSet )
		playerStartPosition( m_player->getPlayerIndex(), &from );

	Int best = -1;
	Real bestDistSqr = 0.0f;
	Bool bestOccupied = FALSE;

	for( Int startNdx = 0; startNdx < MAX_PLAYER_COUNT; ++startNdx )
	{
		Coord3D there;
		if( !startPositionLoc( startNdx, &there ) )
			continue;
		if( m_startChecked[ startNdx ] && !m_startOccupied[ startNdx ] )
			continue;			// looked, empty; he is not there

		Bool someoneElses = FALSE;
		for( Int i = 0; i < MAX_PLAYER_COUNT; ++i )
			if( i != playerNdx && m_playerStartNdx[ i ] == startNdx )
				someoneElses = TRUE;
		if( someoneElses )
			continue;

		const Real dx = there.x - from.x;
		const Real dy = there.y - from.y;
		const Real distSqr = dx*dx + dy*dy;
		const Bool occupied = m_startOccupied[ startNdx ];

		// a position known to hold someone beats a maybe, whatever the distances say
		if( best < 0 || (occupied && !bestOccupied) ||
				(occupied == bestOccupied && distSqr < bestDistSqr) )
		{
			best = startNdx;
			bestDistSqr = distSqr;
			bestOccupied = occupied;
			*pos = there;
		}
	}

	return (best >= 0);
}

//----------------------------------------------------------------------------------------------------------
/**
 * Where this scout goes next.  Two jobs, in that order.
 *
 * Searching: a start position nobody has looked at might be an enemy's, and updateStartIntel says
 * how likely that is - 3/7 in a 1v3 on an eight-position map.  Every unchecked position carries the
 * same odds, though, so the odds cancel out of the comparison and what is left is the nearest one:
 * the cheapest question to answer first.  What the odds are actually for is knowing when to stop.
 *
 * Touring: once every enemy is placed there is nothing left to search for, and the job becomes
 * keeping the picture of the bases current.  That is the stalest one per step walked -
 *
 *     score = frames since we last looked there / distance to walk there
 *
 * - recomputed at every arrival, so a scout drops a base the other scout has just refreshed and
 * takes the one nobody has been to in a while.  A picture younger than this rung's scouting interval
 * is not worth the walk at all: the scout stays where it is, keeping vision on what it can already
 * see, and is asked again in a couple of seconds.
 *
 * Somewhere crossed off as empty is never a target for either job.
 */
Bool AIPlayer::nextScoutTarget( Int slot, const Coord3D *from, Coord3D *pos )
{
	if( slot < 0 || slot >= MAX_AI_SCOUTS || from == NULL )
		return FALSE;

	const UnsignedInt now = TheGameLogic->getFrame();

	//
	// This is only ever called with an idle scout, so the last leg is over: whatever it was sent to
	// look at, it has now looked at.  (A scout that went idle because its path failed stamps a look it
	// never took - that costs one target staying stale for a cycle, not a wrong decision.)
	//
	if( m_scoutTargetFor[ slot ] >= 0 && m_scoutTargetFor[ slot ] < MAX_PLAYER_COUNT )
		m_scoutSeenFrame[ m_scoutTargetFor[ slot ] ] = now;

	const AIDifficultyProfile *profile = getSkillProfile();
	const UnsignedInt fresh = REAL_TO_INT_CEIL( profile->m_scoutIntervalSeconds * LOGICFRAMES_PER_SECOND );

	Int bestNdx = -1;
	Real bestScore = 0.0f;
	Bool bestSearching = FALSE;
	Coord3D bestPos;

	for( Int startNdx = 0; startNdx < MAX_PLAYER_COUNT; ++startNdx )
	{
		Coord3D there;
		if( !startPositionLoc( startNdx, &there ) )
			continue;

		const Bool searching = !m_startChecked[ startNdx ];
		if( !searching && !m_startOccupied[ startNdx ] )
			continue;			// crossed off, or ours: nothing there to go and see

		Bool taken = FALSE;
		for( Int other = 0; other < MAX_AI_SCOUTS; ++other )
			if( other != slot && m_scoutID[ other ] != INVALID_ID && m_scoutTargetFor[ other ] == startNdx )
				taken = TRUE;
		if( taken )
			continue;			// the other scout is already on its way there

		const Real dx = there.x - from->x;
		const Real dy = there.y - from->y;
		const Real dist = (Real)sqrt( dx*dx + dy*dy );

		const Real score = aiScoutScore( now, m_scoutSeenFrame[ startNdx ], dist, fresh );
		if( score <= 0.0f )
			continue;			// looked at recently enough that the walk would buy nothing

		// finding an enemy beats refreshing one already found, whatever the ages say
		if( bestNdx < 0 || (searching && !bestSearching) ||
				(searching == bestSearching && score > bestScore) )
		{
			bestNdx = startNdx;
			bestScore = score;
			bestSearching = searching;
			bestPos = there;
		}
	}

	if( bestNdx < 0 )
		return FALSE;			// everything worth seeing has been seen recently enough

	m_scoutTargetFor[ slot ] = bestNdx;
	*pos = bestPos;
	return TRUE;
}

//----------------------------------------------------------------------------------------------------------
/** The cheapest infantry this player can build that can walk into a building and own it.  Read off
	* the command set rather than a per-faction table, so it lands on the Ranger, the Rebel and the
	* Red Guard - and on whatever a mod gave the ability to. */
static const ThingTemplate *cheapestCapturerTemplate( Player *player )
{
	const ThingTemplate *best = NULL;
	Int bestCost = 0;

	for( const ThingTemplate *t = TheThingFactory->firstTemplate(); t; t = t->friend_getNextTemplate() )
	{
		if( !t->isKindOf( KINDOF_INFANTRY ) || t->isKindOf( KINDOF_DOZER ) )
			continue;
		if( !isOwnArmy( player, t ) || !player->canBuild( t ) )
			continue;

		const Int cost = t->calcCostToBuild( player );
		if( cost <= 0 || (best != NULL && cost >= bestCost) )
			continue;

		const CommandSet *set = TheCommandStore->findCommandSet( t->friend_getCommandSetString() );
		if( set == NULL )
			continue;

		Bool canCapture = FALSE;
		for( Int i = 0; i < MAX_COMMANDS_PER_SET; ++i )
		{
			const CommandButton *button = set->getCommandButton( i );
			if( button == NULL || button->getCommandType() != GUI_COMMAND_SPECIAL_POWER )
				continue;
			const SpecialPowerTemplate *power = button->getSpecialPowerTemplate();
			if( power && power->getSpecialPowerType() == SPECIAL_INFANTRY_CAPTURE_BUILDING )
				canCapture = TRUE;
		}
		if( !canCapture )
			continue;

		best = t;
		bestCost = cost;
	}
	return best;
}

//----------------------------------------------------------------------------------------------------------
/**
 * A unit of ours that can take a building.  Same rule as findScout: the default team only, so
 * nothing is pulled off a team that has a mission.
 */
Object *AIPlayer::findCapturer( void )
{
	Team *team = m_player->getDefaultTeam();
	if( team == NULL )
		return NULL;

	for( DLINK_ITERATOR<Object> iter = team->iterate_TeamMemberList(); !iter.done(); iter.advance() )
	{
		Object *obj = iter.cur();
		if( obj == NULL || obj->isEffectivelyDead() || obj->getAI() == NULL )
			continue;
		if( obj->findSpecialPowerModuleInterface( SPECIAL_INFANTRY_CAPTURE_BUILDING ) == NULL )
			continue;

		Bool busy = FALSE;
		for( Int slot = 0; slot < MAX_AI_SCOUTS; ++slot )
			if( m_scoutID[ slot ] == obj->getID() )
				busy = TRUE;			// the map still has to be looked at
		if( busy )
			continue;

		return obj;
	}
	return NULL;
}

//----------------------------------------------------------------------------------------------------------
/**
 * The closest tech building worth walking to: one to take (wantOurs FALSE) or one of ours to stand
 * on (wantOurs TRUE).
 *
 * Fog-aware like everything else here - a derrick nobody has seen yet is not a plan.  Uncaptured
 * ones belong to the neutral player, captured ones to whoever took them, so both are found by
 * walking the player list, which is the same shape getPlayerStructureBounds uses.
 */
Object *AIPlayer::nearestTechBuilding( const Coord3D *from, Bool wantOurs )
{
	if( ThePlayerList == NULL || from == NULL )
		return NULL;

	const Int myNdx = m_player->getPlayerIndex();
	Object *best = NULL;
	Real bestDistSqr = 0.0f;

	const Int playerCount = ThePlayerList->getPlayerCount();
	for( Int i = 0; i < playerCount; ++i )
	{
		Player *p = ThePlayerList->getNthPlayer( i );
		if( p == NULL )
			continue;
		if( wantOurs != (p == m_player) )
			continue;
		if( !wantOurs && p != m_player && m_player->getRelationship( p->getDefaultTeam() ) == ALLIES )
			continue;			// it is not polite to capture an ally's buildings

		for( Player::PlayerTeamList::const_iterator it = p->getPlayerTeams()->begin();
				 it != p->getPlayerTeams()->end(); ++it )
		{
			for( DLINK_ITERATOR<Team> teamIter = (*it)->iterate_TeamInstanceList(); !teamIter.done(); teamIter.advance() )
			{
				Team *team = teamIter.cur();
				if( team == NULL )
					continue;

				for( DLINK_ITERATOR<Object> objIter = team->iterate_TeamMemberList(); !objIter.done(); objIter.advance() )
				{
					Object *obj = objIter.cur();
					if( obj == NULL || obj->isEffectivelyDead() )
						continue;
					if( !obj->isKindOf( KINDOF_TECH_BUILDING ) )
						continue;
					if( !wantOurs && obj->isKindOf( KINDOF_IMMUNE_TO_CAPTURE ) )
						continue;
					if( !observerKnowsAbout( obj, myNdx ) )
						continue;			// not found yet, so not a plan

					const Coord3D *at = obj->getPosition();
					const Real dx = at->x - from->x;
					const Real dy = at->y - from->y;
					const Real distSqr = dx*dx + dy*dy;
					if( best == NULL || distSqr < bestDistSqr )
					{
						best = obj;
						bestDistSqr = distSqr;
					}
				}
			}
		}
	}
	return best;
}

//----------------------------------------------------------------------------------------------------------
/**
 * Go and take the buildings that pay.
 *
 * An oil derrick is a supply pile that never runs out and costs one infantryman to own, and the AI
 * had no concept of one: it walked past the neutral ones and shot the enemy's.  One cheap unit now
 * does the rounds, and when there is nothing left to take it stands on what we own - an uncaptured
 * derrick is free money for whoever walks up to it next, and a captured one is free money for
 * whoever walks up to it after that.
 */
void AIPlayer::doCapture( void )
{
	if( --m_captureTimer > 0 )
		return;
	m_captureTimer = CAPTURE_CHECK_RATE;

	Object *capturer = TheGameLogic->findObjectByID( m_capturerID );
	if( capturer && (capturer->isEffectivelyDead() || capturer->getControllingPlayer() != m_player) )
		capturer = NULL;
	if( capturer == NULL )
	{
		m_capturerID = INVALID_ID;
		capturer = findCapturer();
		if( capturer )
			m_capturerID = capturer->getID();
	}

	Coord3D from = m_baseCenter;
	if( capturer )
		from = *capturer->getPosition();

	// The next derrick is the one nearest home, not nearest the capturer: from where it stood each
	// capture led to the next one further out, so the round walked into the middle of the map and the
	// derricks behind the base waited on it for minutes, or were never taken.
	Object *target = nearestTechBuilding( &m_baseCenter, FALSE );

	if( capturer == NULL )
	{
		// only pay for one when there is something to spend it on
		if( target )
			queueCapturer();
		return;
	}

	// the ride out: a capturer sitting in its helicopter is flown to the building and put down beside it
	if( capturer->isContained() )
	{
		Object *ship = capturer->getContainedBy();
		if( ship && ship->getID() == m_ferryID && ship->getAI() && ship->getAI()->isIdle() && target )
		{
			Coord3D drop = *target->getPosition();
			const Real dx = ship->getPosition()->x - drop.x;
			const Real dy = ship->getPosition()->y - drop.y;
			const Real length = sqrt( dx * dx + dy * dy );
			if( length > FERRY_DROP_STANDOFF )
			{
				drop.x += dx / length * FERRY_DROP_STANDOFF;
				drop.y += dy / length * FERRY_DROP_STANDOFF;
			}
			drop.z = TheTerrainLogic->getGroundHeight( drop.x, drop.y );
			DEBUG_LOG(("AI player %d flies its capturer to a '%s'\n", m_player->getPlayerIndex(), target->getTemplate()->getName().str()));
			ship->getAI()->aiMoveToAndEvacuate( &drop, CMD_FROM_AI );
		}
		return;
	}

	AIUpdateInterface *ai = capturer->getAI();
	if( ai == NULL || !ai->isIdle() )
		return;			// still on its way, to the building or to its seat
	m_ferryID = INVALID_ID;		// standing and out, so the helicopter is free for the waves again

	//
	// A capture in progress reads as idle - the ability parks the unit's AI while it works - so
	// this check used to come round every five seconds and order the same capture again on the
	// same building, restarting it before it could finish.  On anything whose capture takes longer
	// than the check rate that is a loop with no way out: the derrick flashed and ticked forever
	// with a rifleman standing on it, and each restart left another copy of the ability's loop
	// sound playing.
	//
	if( capturer->testStatus( OBJECT_STATUS_IS_USING_ABILITY ) )
		return;			// already taking one

	if( target == NULL )
	{
		// nothing left to take: sit on what we own rather than wander home and let it be taken back
		Object *ours = nearestTechBuilding( &from, TRUE );
		if( ours )
			ai->aiGuardObject( ours, GUARDMODE_NORMAL, CMD_FROM_AI );
		return;
	}

	//
	// canCaptureBuilding refuses through the shroud and the capture itself has a range, so a target
	// we know about but cannot reach from here is walked to first and asked again next check.
	//
	SpecialPowerModuleInterface *mod = capturer->findSpecialPowerModuleInterface( SPECIAL_INFANTRY_CAPTURE_BUILDING );

	//
	// The Ranger, the Red Guard and the Rebel all come out of the barracks with the capture paused
	// until the player buys it there, and nothing bought it: the capturer walked up to a derrick and
	// stood on it all match.  Buy it (buildUpgrade waits for the money and ignores a repeat) and keep
	// the capturer home until it is in.
	//
	static const AsciiString captureUpgradeName( "Upgrade_InfantryCaptureBuilding" );
	const UpgradeTemplate *captureUpgrade = TheUpgradeCenter->findUpgrade( captureUpgradeName );
	if( mod && captureUpgrade && mod->getPercentReady() < 1.0f && !m_player->hasUpgradeComplete( captureUpgrade ) )
	{
		const Bool queued = m_player->hasUpgradeInProduction( captureUpgrade );
		buildUpgrade( captureUpgradeName );
		if( !queued && m_player->hasUpgradeInProduction( captureUpgrade ) )
			DEBUG_LOG(("AI player %d buys the capture upgrade for a '%s'\n", m_player->getPlayerIndex(),
				target->getTemplate()->getName().str()));
		return;
	}

	if( mod && TheActionManager->canCaptureBuilding( capturer, target, CMD_FROM_AI ) )
	{
		mod->doSpecialPowerAtObject( target, 0 );
		DEBUG_LOG(("AI player %d capturing a '%s'\n", m_player->getPlayerIndex(),
							 target->getTemplate()->getName().str()));
	}
	else
	{
		// a long walk goes by air when a helicopter with a seat is waiting at home: the rifleman who
		// walked across the map to a derrick was the capture a player saw take minutes
		Object *ship = NULL;
		const Real walkSqr = sqr( target->getPosition()->x - from.x ) + sqr( target->getPosition()->y - from.y );
		if( walkSqr > sqr( FERRY_MIN_WALK ) && !measuringWithoutTactics() )
		{
			GunshipList list;
			list.home = m_baseCenter;
			list.reachSqr = sqr( 2.0f * m_baseRadius );
			list.ferry = INVALID_ID;
			list.owner = NULL;		// helicopters only: a gunship Chinook stays with the wave
			m_player->iterateObjects( findGunshipAtHome, &list );
			Real nearestSqr = 0.0f;
			for( size_t g = 0; g < list.gunships.size(); ++g )
			{
				Object *candidate = list.gunships[ g ];
				if( candidate->getAI() == NULL || !candidate->getAI()->isIdle() || isDutyHelix( candidate ) ||
						!candidate->getContain()->isValidContainerFor( capturer, TRUE ) )
					continue;
				const Real distSqr = sqr( candidate->getPosition()->x - from.x ) + sqr( candidate->getPosition()->y - from.y );
				if( ship == NULL || distSqr < nearestSqr )
				{
					ship = candidate;
					nearestSqr = distSqr;
				}
			}
		}
		if( ship )
		{
			DEBUG_LOG(("AI player %d boards its capturer onto '%s' %d for a '%s'\n", m_player->getPlayerIndex(),
				ship->getTemplate()->getName().str(), ship->getID(), target->getTemplate()->getName().str()));
			m_ferryID = ship->getID();
			ai->aiEnter( ship, CMD_FROM_AI );
		}
		else
			ai->aiMoveToObject( target, CMD_FROM_AI );
	}
}

//----------------------------------------------------------------------------------------------------------
void AIPlayer::queueCapturer( void )
{
	queueSupportUnit( cheapestCapturerTemplate( m_player ), "CAPTURE" );
}

/** How far a vehicle hacker will go for a target.  About a unit's sight: Black Lotus shuts a tank
	* down for a few seconds, which is worth nothing if she spends a minute walking to it and dies
	* standing next to it. */
static const Real VEHICLE_HACK_REACH = 250.0f;

/** How far a thief will go for one, measured from the thief, or from the base while we have none.
	* Wider than the hack, because the prize is permanent and the walk is the whole plan.  Not the
	* whole map, which is what the first draft allowed: with the shroud not yet up on frame 7, every
	* computer player in the match went shopping for a hijacker to send at an enemy dozer standing on
	* the far side of it. */
static const Real HIJACK_REACH = 700.0f;

/** Could this player buy this unit if it were asking a factory right now?
	*
	* A computer player keeps canBuildUnits false between team orders, and allowedToBuild refuses
	* every non-structure while it is, so a plain canBuild here answers "no" for a reason that has
	* nothing to do with the unit: a Toxin General with a barracks, an arms dealer and 17,000 in the
	* bank reported no buildable hijacker on every pass of a fifteen-thousand-frame match.
	* queueSupportUnit flips the same flag around its own factory lookup; this is that trick moved to
	* the question in front of it.  cheapestScoutTemplate and cheapestCapturerTemplate ask without it
	* and so find their unit only on the passes where the flag happens to be up - left as they are
	* because both were measured with that in them. */
static Bool couldBuildUnit( Player *player, const ThingTemplate *tmpl )
{
	const Bool wasAllowed = player->getCanBuildUnits();
	player->setCanBuildUnits( TRUE );
	const Bool answer = player->canBuild( tmpl );
	player->setCanBuildUnits( wasAllowed );
	return answer;
}

//----------------------------------------------------------------------------------------------------------
/** This one takes a vehicle by walking into it.  Asked of the object rather than of a faction table,
	* so it lands on the GLA Hijacker and on anything a mod gave the same collide to. */
static Bool carriesHijackModule( const Object *obj )
{
	for( BehaviorModule **m = obj->getBehaviorModules(); *m; ++m )
	{
		const CollideModuleInterface *collide = (*m)->getCollide();
		if( collide && collide->isHijackedVehicleCrateCollide() )
			return TRUE;
	}
	return FALSE;
}

//----------------------------------------------------------------------------------------------------------
/** The cheapest thing this player can build that can do it.  The module list is the test here too -
	* the hijack has no command button to read, because the player just clicks the tank. */
static const ThingTemplate *cheapestHijackerTemplate( Player *player )
{
	const ThingTemplate *best = NULL;
	Int bestCost = 0;

	for( const ThingTemplate *t = TheThingFactory->firstTemplate(); t; t = t->friend_getNextTemplate() )
	{
		if( !t->isKindOf( KINDOF_INFANTRY ) || !isOwnArmy( player, t ) || !couldBuildUnit( player, t ) )
			continue;

		const Int cost = t->calcCostToBuild( player );
		if( cost <= 0 || (best != NULL && cost >= bestCost) )
			continue;

		Bool canHijack = FALSE;
		const ModuleInfo &modules = t->getBehaviorModuleInfo();
		for( Int i = 0; i < modules.getCount(); ++i )
		{
			if( modules.getNthName( i ).compareNoCase( "ConvertToHijackedVehicleCrateCollide" ) == 0 )
				canHijack = TRUE;
		}
		if( !canHijack )
			continue;

		best = t;
		bestCost = cost;
	}
	return best;
}

//----------------------------------------------------------------------------------------------------------
/**
 * A thief of ours with nothing to do.  Default team only, the same rule findCapturer and findScout
 * follow: one that arrived with an attack team has a job already.
 */
Object *AIPlayer::findHijacker( void )
{
	Team *team = m_player->getDefaultTeam();
	if( team == NULL )
		return NULL;

	for( DLINK_ITERATOR<Object> iter = team->iterate_TeamMemberList(); !iter.done(); iter.advance() )
	{
		Object *obj = iter.cur();
		if( obj == NULL || obj->isEffectivelyDead() || obj->isContained() || obj->getAI() == NULL )
			continue;
		if( obj->getID() == m_capturerID )
			continue;			// out taking a derrick
		if( !carriesHijackModule( obj ) )
			continue;

		Bool busy = FALSE;
		for( Int slot = 0; slot < MAX_AI_SCOUTS; ++slot )
			if( m_scoutID[ slot ] == obj->getID() )
				busy = TRUE;			// the map still has to be looked at
		if( busy )
			continue;

		return obj;
	}
	return NULL;
}

//----------------------------------------------------------------------------------------------------------
/**
 * The closest enemy vehicle in sight, or none within reach.
 *
 * A vehicle goes back to SHROUDED the moment our vision leaves it, so observerKnowsAbout reads here
 * as "we can see it now" - the only honest rule for a target that drives off while we walk to it.
 *
 * Nearest, not richest: a thief walks, and the Humvee it reaches is worth more than the Overlord it
 * dies halfway to.
 */
Object *AIPlayer::nearestStealableVehicle( const Coord3D *from, Real reach )
{
	if( ThePlayerList == NULL || from == NULL )
		return NULL;

	const Int myNdx = m_player->getPlayerIndex();
	const Real reachSqr = reach * reach;
	Object *best = NULL;
	Real bestDistSqr = 0.0f;

	const Int playerCount = ThePlayerList->getPlayerCount();
	for( Int i = 0; i < playerCount; ++i )
	{
		Player *p = ThePlayerList->getNthPlayer( i );
		if( p == NULL || p == m_player )
			continue;
		if( m_player->getRelationship( p->getDefaultTeam() ) != ENEMIES )
			continue;			// a civilian's parked car is not a prize, and an ally's tank is not either

		for( Player::PlayerTeamList::const_iterator it = p->getPlayerTeams()->begin();
				 it != p->getPlayerTeams()->end(); ++it )
		{
			for( DLINK_ITERATOR<Team> teamIter = (*it)->iterate_TeamInstanceList(); !teamIter.done(); teamIter.advance() )
			{
				Team *team = teamIter.cur();
				if( team == NULL )
					continue;

				for( DLINK_ITERATOR<Object> objIter = team->iterate_TeamMemberList(); !objIter.done(); objIter.advance() )
				{
					Object *obj = objIter.cur();
					if( obj == NULL || obj->isEffectivelyDead() || obj->isContained() )
						continue;
					if( !obj->isKindOf( KINDOF_VEHICLE ) )
						continue;
					if( obj->isKindOf( KINDOF_AIRCRAFT ) || obj->isKindOf( KINDOF_DRONE ) )
						continue;			// neither a hijacker nor a hacker is allowed either of those
					if( !observerKnowsAbout( obj, myNdx ) )
						continue;

					const Coord3D *at = obj->getPosition();
					const Real dx = at->x - from->x;
					const Real dy = at->y - from->y;
					const Real distSqr = dx*dx + dy*dy;
					if( distSqr > reachSqr )
						continue;
					if( best == NULL || distSqr < bestDistSqr )
					{
						best = obj;
						bestDistSqr = distSqr;
					}
				}
			}
		}
	}
	return best;
}

//----------------------------------------------------------------------------------------------------------
/** Black Lotus, or whoever else the data gave the vehicle hack to, standing about with it ready. */
static void findIdleVehicleHacker( Object *obj, void *userData )
{
	Object **hacker = (Object **)userData;
	if( *hacker || obj->isEffectivelyDead() || obj->isContained() )
		return;
	if( !obj->hasSpecialPower( SPECIAL_BLACKLOTUS_DISABLE_VEHICLE_HACK ) )
		return;
	if( obj->testStatus( OBJECT_STATUS_IS_USING_ABILITY ) )
		return;
	AIUpdateInterface *ai = obj->getAI();
	if( ai == NULL || !ai->isIdle() )
		return;
	*hacker = obj;
}

//----------------------------------------------------------------------------------------------------------
/**
 * Take the enemy's vehicles instead of shooting at them.
 *
 * A hijacker costs a few hundred and walks off with a tank, and no computer player had ever built
 * one: the skirmish scripts name their teams by template and not one of the shipped teams asks for
 * it.  So one thief at a time, bought out of spare change the way the capturer is, and only while
 * there is something in sight to take.  It is the same money argument as the derrick - the cheapest
 * unit in the game trading itself for the most expensive one on the field.
 *
 * The vehicle hack rides along here because it is the same decision made for free: Black Lotus
 * arrives with the script's teams already, her hack costs nothing and sits ready most of the match,
 * and the AI has never once used it.  She only takes what is already in front of her.
 */
void AIPlayer::doHijack( void )
{
	if( --m_hijackTimer > 0 )
		return;
	m_hijackTimer = HIJACK_CHECK_RATE;

	if( m_player == NULL || !m_player->isPlayableSide() )
		return;

	Object *hacker = NULL;
	m_player->iterateObjects( findIdleVehicleHacker, &hacker );
	if( hacker )
	{
		Object *victim = nearestStealableVehicle( hacker->getPosition(), VEHICLE_HACK_REACH );
		SpecialPowerModuleInterface *mod = hacker->findSpecialPowerModuleInterface( SPECIAL_BLACKLOTUS_DISABLE_VEHICLE_HACK );
		if( victim && mod && TheActionManager->canDisableVehicleViaHacking( hacker, victim, CMD_FROM_AI ) )
		{
			mod->doSpecialPowerAtObject( victim, 0 );
			DEBUG_LOG(("AI player %d hacks a '%s'\n", m_player->getPlayerIndex(),
								 victim->getTemplate()->getName().str()));
		}
	}

	Object *thief = TheGameLogic->findObjectByID( m_hijackerID );
	if( thief && (thief->isEffectivelyDead() || thief->getControllingPlayer() != m_player) )
		thief = NULL;			// dead, or it is sitting in the tank it took and is somebody else's problem
	if( thief == NULL )
	{
		m_hijackerID = INVALID_ID;
		thief = findHijacker();
		if( thief )
			m_hijackerID = thief->getID();
	}

	Coord3D from = m_baseCenter;
	if( thief )
		from = *thief->getPosition();

	Object *target = nearestStealableVehicle( &from, HIJACK_REACH );
	if( target == NULL )
		return;			// nothing in reach; the thief waits where it is rather than walk into the fog

	if( thief == NULL )
	{
		// Only pay for one when there is something to spend it on.  It shares the one support-unit
		// slot with the scout and the capturer, so a match that keeps losing scouts keeps the thief
		// waiting - which is the right order of priorities and the reason there is never more than
		// one of these walking about.
		queueSupportUnit( cheapestHijackerTemplate( m_player ), "HIJACK" );
		return;
	}

	AIUpdateInterface *ai = thief->getAI();
	if( ai == NULL || !ai->isIdle() )
		return;			// still on its way to the last one

	//
	// canHijackVehicle refuses through the shroud, and aiEnter is the same order the player's click
	// ends up as: the walk is the attack, and the collide module does the rest on contact.
	//
	if( TheActionManager->canHijackVehicle( thief, target, CMD_FROM_AI ) )
	{
		ai->aiEnter( target, CMD_FROM_AI );
		DEBUG_LOG(("AI player %d sends a '%s' after a '%s'\n", m_player->getPlayerIndex(),
							 thief->getTemplate()->getName().str(), target->getTemplate()->getName().str()));
	}
}

//----------------------------------------------------------------------------------------------------------
/**
 * Keep one unit looking at the map.  Cheap by construction: one unit, ordered only when it has
 * arrived somewhere, replaced out of spare change when it dies.  Every difficulty scouts - an AI
 * that never looks reads as broken rather than as easy; what difficulty should change is how fast
 * it acts on what it finds.
 */
void AIPlayer::doScouting( void )
{
	//
	// Only a side that is actually playing goes looking.  A map's civilians are a computer player
	// too - that is how they are owned - and their cars and trucks sit on that player's default
	// team, which is exactly where findScout() looks.  So the civilian player was picking a parked
	// car out of the scenery and driving it to a start position: a stranger rolling into your base
	// in the first minute of every game, every game.  It has no faction, no build list and nothing
	// to learn from the map, so it does not scout.
	//
	if( m_player == NULL || !m_player->isPlayableSide() )
		return;

	// cheap, on its own one-second clock, and it has to run whether or not the scouts need an order:
	// this is where an arrival turns into an answer, and where the answers turn into a deduction
	updateStartIntel();

	if( --m_scoutTimer > 0 )
		return;
	m_scoutTimer = SCOUT_CHECK_RATE;

	// no new scout out of a base being hit: a rushed China sent two of the Red Guards it had just
	// trained for the fight off to look at the map
	if( isBaseUnderAttack() )
		return;

	const AIDifficultyProfile *profile = getSkillProfile();
	Int wanted = profile->m_maxScouts;
	if( wanted < 1 ) wanted = 1;
	if( wanted > MAX_AI_SCOUTS ) wanted = MAX_AI_SCOUTS;

	Bool ordered = FALSE;
	Bool short_ = FALSE;

	for( Int slot = 0; slot < wanted; ++slot )
	{
		Object *scout = TheGameLogic->findObjectByID( m_scoutID[ slot ] );
		if( scout && (scout->isEffectivelyDead() || scout->getControllingPlayer() != m_player) )
			scout = NULL;

		if( scout == NULL )
		{
			m_scoutID[ slot ] = INVALID_ID;
			scout = findScout();
			if( scout )
			{
				m_scoutID[ slot ] = scout->getID();
				// once per scout, so the log says whether this works at all without saying it twice
				DEBUG_LOG(("AI player %d scouting with a '%s'\n", m_player->getPlayerIndex(),
									 scout->getTemplate()->getName().str()));
			}
		}

		if( scout == NULL )
		{
			short_ = TRUE;
			continue;
		}

		AIUpdateInterface *ai = scout->getAI();
		if( ai == NULL || !ai->isIdle() )
			continue;			// still on its way

		Coord3D goal;
		if( nextScoutTarget( slot, scout->getPosition(), &goal ) )
		{
			ai->aiMoveToPosition( &goal, CMD_FROM_AI );
			ordered = TRUE;
		}
	}

	// one short of what this rung wants: put one on order, out of spare change
	if( short_ )
		queueScout();

	//
	// A scout that has just been sent somewhere is left alone for this rung's scouting interval.
	// This is the ladder's honest perception knob: every rung scouts, they differ in how diligently.
	// Ninety seconds between legs on Easy is a scout that wanders; twenty-five on Brutal is one
	// that keeps the map current.
	//
	if( ordered )
	{
		Int interval = REAL_TO_INT_CEIL( profile->m_scoutIntervalSeconds * LOGICFRAMES_PER_SECOND );
		if( interval > m_scoutTimer )
			m_scoutTimer = interval;
	}
}

//----------------------------------------------------------------------------------------------------------
/**
 * Queues up a dozer.
 */
void AIPlayer::queueDozer( void )
{

	if (dozerInQueue()) return;
	/* A player with no factory that can make a dozer walks every thing template in the game here and
		 finds nothing, and base building asks once per missing build-list entry: 33 walks and 13ms in
		 one pass of a late 4v4, every two seconds. Nothing a factory needs can change between two calls
		 in the same frame, so the answer is kept for the frame. */
	const UnsignedInt frameAfterNow = TheGameLogic->getFrame() + 1;
	if (m_frameAfterFailedDozerQueue == frameAfterNow) return;
	m_frameAfterFailedDozerQueue = frameAfterNow;
	// Find a factory that can build a dozer.

	Bool canBuildUnits = m_player->getCanBuildUnits();
	// If we need a dozer, turn on unit building for a moment.
	m_player->setCanBuildUnits(true);
	const ThingTemplate *tTemplate = TheThingFactory->firstTemplate();
	while (tTemplate) {
		if (tTemplate->isKindOf(KINDOF_DOZER)) {
			Object *factory = findFactory(tTemplate, true);
			if (factory) {
				// we can build one.
				WorkOrder *order = newInstance(WorkOrder);
				order->m_thing = tTemplate;
				order->m_factoryID = INVALID_ID;
				order->m_numRequired = 1;
				order->m_required = true;
				order->m_isResourceGatherer = FALSE;
				// prepend to head of list
				order->m_next = NULL;
				TeamInQueue *team = newInstance(TeamInQueue);
				// Put in front of queue.
				prependTo_TeamBuildQueue(team);
				team->m_priorityBuild = true;
				team->m_workOrders = order;
				team->m_frameStarted = TheGameLogic->getFrame();
				// Stick it on the default team
				team->m_team = m_player->getDefaultTeam(); 
				AsciiString teamName = "DOZER - building one at the ";
				teamName.concat(factory->getTemplate()->getName());
				TheScriptEngine->AppendDebugMessage(teamName, false);
				m_teamDelay = 0;
				startTraining( order, team->m_priorityBuild, team->m_team->getName());
				break;
			}
		}
		tTemplate = tTemplate->friend_getNextTemplate();
	}
	// restore canbuildunits.
	m_player->setCanBuildUnits(canBuildUnits);
}


//-------------------------------------------------------------------------------------------------
/** Difficulty level for this player */
//-------------------------------------------------------------------------------------------------
enum GameDifficulty AIPlayer::getAIDifficulty(void) const
{
	return m_difficulty;
}

//-------------------------------------------------------------------------------------------------
/** The rung a seat that predates the ladder plays at - an old save, a replay, a scripted campaign
	* player.  The three that existed map to the three the lobby used to offer. */
//-------------------------------------------------------------------------------------------------
AISkillLevel AIPlayer::skillLevelForDifficulty( GameDifficulty difficulty )
{
	switch( difficulty )
	{
		case DIFFICULTY_EASY:		return AISKILL_EASY;
		case DIFFICULTY_NORMAL:	return AISKILL_MEDIUM;
		default:								return AISKILL_BRUTAL;
	}
}

//-------------------------------------------------------------------------------------------------
const AIDifficultyProfile *AIPlayer::getSkillProfile( void ) const
{
	const AIDifficultyProfile *profile = TheAI->getDifficultyProfile( m_skillLevel );
	const Int parity = TheGlobalData->m_aiKnobsOffParity;
	Bool knobsOff = FALSE;
	if( parity >= 0 )
	{
		// -aiknobsoff: a skirmish, or the playback of one given the switch again, as -notactics reads it
		const Int originalMode = (TheRecorder && TheRecorder->getMode() == RECORDERMODETYPE_PLAYBACK)
													 ? TheRecorder->getGameMode() : TheGameLogic->getGameMode();
		knobsOff = originalMode == GAME_SKIRMISH &&
							 (parity == 2 || (ThePlayerList->getSlotIndex( m_player->getPlayerIndex() ) & 1) == parity);
	}
	if( !knobsOff && m_persona == AIPERSONA_NORMAL )
		return profile;
	m_measuredProfile = *profile;
	if( knobsOff )
	{
		const Int mask = TheGlobalData->m_aiKnobsOffMask;
		if( mask & AIKNOB_ENGAGE_GATE )	m_measuredProfile.m_engageGate = FALSE;
		if( mask & AIKNOB_ANSWER_ARMY )	m_measuredProfile.m_answerArmy = FALSE;
		if( mask & AIKNOB_MASS_UNIT )		m_measuredProfile.m_massUnit = FALSE;
		if( mask & AIKNOB_BASE_WATCH )	m_measuredProfile.m_baseWatchSeconds = 0.0f;
		if( mask & AIKNOB_STRIKE_DODGE )	m_measuredProfile.m_strikeDodgeSeconds = 0.0f;
	}
	if( m_persona == AIPERSONA_NORMAL )
		return &m_measuredProfile;

	// the personality's lean on the rung, so each scales with Easy, Medium and Hard (aiPersonaWeights)
	const AIPersonaWeights &w = personaWeights();
	AIDifficultyProfile &p = m_measuredProfile;
	p.m_firstPushSeconds = rushPending() ? INT_TO_REAL( rushFrame() ) / LOGICFRAMES_PER_SECOND : p.m_firstPushSeconds * w.m_firstPush;
	p.m_minAttackPower *= w.m_attackPower;
	p.m_bankTargetSeconds *= w.m_bank;
	p.m_bankTargetFloor = REAL_TO_INT( INT_TO_REAL( p.m_bankTargetFloor ) * w.m_bank );
	p.m_cashHoardThreshold = REAL_TO_INT( INT_TO_REAL( p.m_cashHoardThreshold ) * w.m_hoard );
	p.m_surplusShare = min( 1.0f, p.m_surplusShare * w.m_surplus );
	p.m_retreatTtkRatio *= w.m_retreat;
	p.m_counterCompositionWeight = p.m_counterCompositionWeight * w.m_counter + w.m_counterAdd;
	p.m_upgradeStartSeconds *= w.m_upgradeStart;
	// a Counter remembers the army it has seen from Medium up, which Medium alone does not
	if( m_persona == AIPERSONA_COUNTER && m_skillLevel >= AISKILL_MEDIUM )
		p.m_answerArmy = TRUE;
	return &m_measuredProfile;
}

//-------------------------------------------------------------------------------------------------
/** A Rusher's one big attack, by rung: Easy the latest.  If it has not gone two minutes after, it
	* never will, and the Rusher plays on as an Attacker. */
static const Int RUSH_SECONDS[ AISKILL_COUNT ] = { 270, 225, 180 };
static const UnsignedInt RUSH_GIVE_UP_FRAMES = 120 * LOGICFRAMES_PER_SECOND;

UnsignedInt AIPlayer::rushFrame( void ) const
{
	return (UnsignedInt)( RUSH_SECONDS[ m_skillLevel ] * LOGICFRAMES_PER_SECOND );
}

Bool AIPlayer::rushPending( void ) const
{
	return m_persona == AIPERSONA_RUSHER && !m_rushSent && TheGameLogic->getFrame() < rushFrame() + RUSH_GIVE_UP_FRAMES;
}

const AIPersonaWeights &AIPlayer::personaWeights( void ) const
{
	if( m_persona == AIPERSONA_RUSHER && !rushPending() )
		return aiPersonaWeights( AIPERSONA_ATTACKER );
	if( m_persona == AIPERSONA_TECH && m_techDone )
		return aiPersonaWeights( AIPERSONA_NORMAL );
	return aiPersonaWeights( m_persona );
}

void AIPlayer::setAIPersonality( AIPersonality persona )
{
	// EA's AI has no personality: a seat on a map that plays it is Normal whatever the lobby said,
	// draws nothing and keeps the role it rolled (SidesList's flags are set before players are)
	if( !runsOwnAI() )
		persona = AIPERSONA_NORMAL;
	// only a Random seat draws, so a Normal seat's random stream is the one it always was
	m_personaFromRandom = persona == AIPERSONA_RANDOM;
	if( m_personaFromRandom )
		persona = (AIPersonality)GameLogicRandomValue( AIPERSONA_ATTACKER, AIPERSONA_COUNTER );
	m_persona = persona;
	const AIPersonaWeights &w = aiPersonaWeights( persona );
	if( w.m_role >= 0 )
		m_role = (AIRole)w.m_role;
	DEBUG_LOG(("AI PERSONA player %d is %s%s, role %d\n", m_player->getPlayerIndex(), aiPersonalityName( persona ),
		m_personaFromRandom ? " (rolled from Random)" : "", (Int)m_role));
}

//-------------------------------------------------------------------------------------------------
/** A line in character: "GUI:AIChat<Personality><Trigger><n>", Wave with two to choose from by the
	* clock and the seat, never by the random stream.  The chat is drawn by the client and goes nowhere
	* on the wire, as the GG line does, and nothing here is read back by a decision. */
//-------------------------------------------------------------------------------------------------
void AIPlayer::personaChat( const char *trigger )
{
	const UnsignedInt now = TheGameLogic->getFrame();
	if( m_persona == AIPERSONA_NORMAL || !aiPersonaChatAllowed( now, m_chatFrame ) )
		return;
	const Int variants = strcmp( trigger, "Wave" ) == 0 ? 2 : 1;
	const Int pick = 1 + (Int)( ( now / LOGICFRAMES_PER_SECOND + (UnsignedInt)m_player->getPlayerIndex() ) % (UnsignedInt)variants );
	AsciiString label;
	label.format( "GUI:AIChat%s%s%d", aiPersonalityName( m_persona ), trigger, pick );
	m_chatFrame = now;
	DEBUG_LOG(("AI CHAT frame %d player %d %s\n", now, m_player->getPlayerIndex(), label.str()));
	TheInGameUI->chatMessage( m_player, TheGameText->fetch( label ) );
}

static void findFinishedSuperweapon( Object *obj, void *userData )
{
	if( obj->isKindOf( KINDOF_FS_SUPERWEAPON ) && !obj->isEffectivelyDead() && !obj->testStatus( OBJECT_STATUS_UNDER_CONSTRUCTION ) )
		*(Bool *)userData = TRUE;
}

/** The summary's first superweapon, and for the chat every enemy gone for good, once each. */
void AIPlayer::notePersonaProgress( void )
{
	if( m_superweaponFrame == 0 )
	{
		Bool standing = FALSE;
		m_player->iterateObjects( findFinishedSuperweapon, &standing );
		if( standing )
			m_superweaponFrame = TheGameLogic->getFrame();
	}
	if( m_persona == AIPERSONA_NORMAL )
		return;
	// the Tech AI's plan is done once its first superweapon stands, and from then it plays as Normal
	if( m_persona == AIPERSONA_TECH && !m_techDone && m_superweaponFrame != 0 )
	{
		m_techDone = TRUE;
		DEBUG_LOG(("AI PERSONA frame %d player %d has its superweapon up and plays on as Normal\n", TheGameLogic->getFrame(),
			m_player->getPlayerIndex()));
	}
	for( Int i = 0; i < ThePlayerList->getPlayerCount() && i < 32; ++i )
	{
		Player *other = ThePlayerList->getNthPlayer( i );
		if( (m_beatenMask & (1 << i)) != 0 || other == m_player || !other->isPlayableSide() ||
				m_player->getRelationship( other->getDefaultTeam() ) != ENEMIES || other->hasAnyUnits() || other->hasAnyBuildFacility() )
			continue;
		m_beatenMask |= 1 << i;
		personaChat( "Win" );
	}
}

//-------------------------------------------------------------------------------------------------
/** The Raider's sorties: from its own start, not the army's first push (that waited for the drop zone,
	* and the first raid went at six and a half minutes), every so often the fastest idle ground vehicles,
	* at home or back from the last raid, go for the enemy gatherer or worker this AI knows of that the
	* fewest known guns cover, nearest home first, so the party goes around the army rather than into it.
	* With no gatherer known it drives at a supply depot, attacking what it meets on the way, which is
	* the gatherers at it.  Easy sends the fewest, the latest and the least often. */
//-------------------------------------------------------------------------------------------------
static const Int RAID_EVERY_SECONDS[ AISKILL_COUNT ] = { 60, 40, 30 };
static const Int RAID_MAX_GUNS_OVER_TARGET = 1;
static const Real RAID_GUN_MARGIN = 50.0f;
/** How far a raider looks round itself for a gatherer to turn on, and how near a supply centre a pile
	* has to be to count as the one its gatherers load at. */
static const Real RAID_HUNT_RADIUS = 450.0f;
static const Real RAID_PILE_REACH = 600.0f;

/** What a raid is for: a supply truck or a worker on the ground.  A Chinook gathers in the air, out of
	* a ground raider's reach. */
static Bool isGroundGatherer( const Object *obj )
{
	return ( obj->isKindOf( KINDOF_HARVESTER ) || obj->isKindOf( KINDOF_DOZER ) ) && !obj->isKindOf( KINDOF_AIRCRAFT );
}

static Bool raidsFasterFirst( Object *a, Object *b )
{
	const Real sa = a->getAI()->getCurLocomotorSpeed();
	const Real sb = b->getAI()->getCurLocomotorSpeed();
	return sa != sb ? sa > sb : a->getID() < b->getID();
}

void AIPlayer::doPersonaRaids( void )
{
	const UnsignedInt now = TheGameLogic->getFrame();
	if( !m_baseCenterSet || now + 60 * LOGICFRAMES_PER_SECOND < (UnsignedInt)( RAID_START_SECONDS[ m_skillLevel ] * LOGICFRAMES_PER_SECOND ) )
		return;

	// the raiders are its own, two sorties' worth, so the waves and the home guard do not take them: the
	// first version picked from whatever stood idle at home, which the next wave had already taken, and
	// raided four times a match.  The fastest ground vehicles idle or on guard at home fill the empty places
	const Int keep = 2 * RAID_PARTY[ m_skillLevel ];
	Int free = 0;
	for( Int r = 0; r < keep; ++r )
	{
		const Object *raider = TheGameLogic->findObjectByID( m_raider[ r ] );
		if( raider == NULL || raider->isEffectivelyDead() )
			m_raider[ r ] = INVALID_ID;
		if( m_raider[ r ] == INVALID_ID )
			++free;
	}
	if( free > 0 )
	{
		std::vector<Object *> owned;
		m_player->iterateObjects( collectOwned, &owned );
		std::vector<Object *> recruits;
		for( std::vector<Object *>::const_iterator o = owned.begin(); o != owned.end(); ++o )
		{
			Object *obj = *o;
			if( obj->isEffectivelyDead() || obj->isContained() || obj->getAI() == NULL || !obj->isKindOf( KINDOF_VEHICLE ) ||
					obj->isKindOf( KINDOF_AIRCRAFT ) || obj->isKindOf( KINDOF_IMMOBILE ) || !hasPrimaryWeapon( obj->getTemplate() ) ||
					!isAtHome( obj->getPosition() ) || !leavesToFinish( obj ) )
				continue;
			const StateID state = obj->getAI()->getCurrentStateID();
			if( state != AI_IDLE && state != AI_GUARD && state != AI_GUARD_RETALIATE )
				continue;
			Bool parked = FALSE;
			for( Int i = 0; i < MAX_HELD_TEAMS; ++i )
				if( m_heldUsed[ i ] && obj->getTeam() && m_heldTeam[ i ] == obj->getTeam()->getID() )
					parked = TRUE;
			if( !parked )
				recruits.push_back( obj );
		}
		std::sort( recruits.begin(), recruits.end(), raidsFasterFirst );
		size_t next = 0;
		for( Int r = 0; r < keep && next < recruits.size(); ++r )
			if( m_raider[ r ] == INVALID_ID )
				m_raider[ r ] = recruits[ next++ ]->getID();
	}

	huntGatherers();

	if( isBaseUnderAttack() || now < (UnsignedInt)( RAID_START_SECONDS[ m_skillLevel ] * LOGICFRAMES_PER_SECOND ) ||
			(m_raidFrame != 0 && now - m_raidFrame < (UnsignedInt)( RAID_EVERY_SECONDS[ m_skillLevel ] * LOGICFRAMES_PER_SECOND )) )
		return;

	// the sortie: the raiders with nothing to do, wherever they stand, a party's worth
	std::vector<Object *> party;
	for( Int r = 0; r < keep && (Int)party.size() < RAID_PARTY[ m_skillLevel ]; ++r )
	{
		Object *raider = TheGameLogic->findObjectByID( m_raider[ r ] );
		if( raider == NULL || raider->isContained() )
			continue;
		const StateID state = raider->getAI()->getCurrentStateID();
		if( state == AI_IDLE || state == AI_GUARD || state == AI_GUARD_RETALIATE )
			party.push_back( raider );
	}
	// never one alone: six sorties of one Battlemaster each killed nothing
	if( (Int)party.size() < 2 )
		return;
	m_raidFrame = now;
	Int raiders = 0;
	for( Int r = 0; r < keep; ++r )
		if( m_raider[ r ] != INVALID_ID )
			++raiders;
	DEBUG_LOG(("AI RAIDPARTY frame %d player %d has %d raiders, %d of them free\n", now, m_player->getPlayerIndex(), raiders, (Int)party.size()));

	std::vector<AIKnownGun> guns;
	collectKnownGuns( &guns );
	const Int me = m_player->getPlayerIndex();
	// a gatherer he has, by the fewest known guns over it and then the nearest; a supply centre of his the
	// same way when none is known.  88 of 117 sorties used to drive at the neutral supply pile, where an
	// attack-move found nothing to shoot, and a whole match killed two gatherers
	Object *target = NULL;
	Real bestScore = 0.0f;
	Int bestGuns = 0;
	Bool bestWorker = FALSE;
	for( Object *obj = TheGameLogic->getFirstObject(); obj; obj = obj->getNextObject() )
	{
		const Bool worker = isGroundGatherer( obj );
		const Bool depot = obj->isKindOf( KINDOF_FS_SUPPLY_CENTER );
		if( (!worker && !depot) || obj->isEffectivelyDead() || m_player->getRelationship( obj->getTeam() ) != ENEMIES ||
				!observerKnowsAbout( obj, me ) )
			continue;
		const Coord3D *at = obj->getPosition();
		Int covering = 0;
		for( size_t g = 0; g < guns.size(); ++g )
			if( sqr( guns[ g ].m_x - at->x ) + sqr( guns[ g ].m_y - at->y ) <= sqr( guns[ g ].m_reach + RAID_GUN_MARGIN ) )
				++covering;
		if( worker && covering > RAID_MAX_GUNS_OVER_TARGET )
			continue;
		const Real score = (Real)sqrt( sqr( at->x - m_baseCenter.x ) + sqr( at->y - m_baseCenter.y ) );
		if( target == NULL || (worker && !bestWorker) ||
				(worker == bestWorker && (covering < bestGuns || (covering == bestGuns && score < bestScore))) )
		{
			target = obj;
			bestScore = score;
			bestGuns = covering;
			bestWorker = worker;
		}
	}

	// a supply centre is where his gatherers unload, and the pile beside it where they load: the party
	// drives at the middle of the two, and huntGatherers turns it on the nearest gatherer there.  With
	// neither known, the pile nearest his base
	Coord3D goal;
	const Coord3D *from = NULL;
	Coord3D guess;
	if( target && !bestWorker )
		from = target->getPosition();
	else if( target == NULL )
	{
		// a start a scout found him at; else every start he may be at by turns, sortie by sortie.  The best
		// guess alone sent every sortie of a match to an empty start, with his base at the other end
		Coord3D starts[ MAX_PLAYER_COUNT ];
		Int known = 0;
		const Int count = possibleEnemyStarts( starts, &known );
		if( count == 0 )
			return;
		const Int sortie = (Int)( now / ( RAID_EVERY_SECONDS[ m_skillLevel ] * LOGICFRAMES_PER_SECOND ) );
		guess = starts[ known > 0 ? sortie % known : sortie % count ];
		from = &guess;
	}
	if( from )
	{
		const Object *pile = NULL;
		Real nearestSqr = 0.0f;
		for( Object *obj = TheGameLogic->getFirstObject(); obj; obj = obj->getNextObject() )
		{
			if( !obj->isKindOf( KINDOF_SUPPLY_SOURCE ) || obj->isEffectivelyDead() || m_player->getRelationship( obj->getTeam() ) == ALLIES )
				continue;
			const Real distSqr = sqr( obj->getPosition()->x - from->x ) + sqr( obj->getPosition()->y - from->y );
			if( pile == NULL || distSqr < nearestSqr )
			{
				pile = obj;
				nearestSqr = distSqr;
			}
		}
		if( pile == NULL && target == NULL )
			return;
		goal = pile ? *pile->getPosition() : *from;
		if( target && pile && nearestSqr <= sqr( RAID_PILE_REACH ) )
		{
			goal.x = 0.5f * ( goal.x + from->x );
			goal.y = 0.5f * ( goal.y + from->y );
		}
		else if( target )
			goal = *from;
		if( target == NULL )
			target = (Object *)pile;
	}
	for( std::vector<Object *>::const_iterator r = party.begin(); r != party.end(); ++r )
	{
		if( bestWorker )
			(*r)->getAI()->aiAttackObject( target, NO_MAX_SHOTS_LIMIT, CMD_FROM_AI );
		else
			(*r)->getAI()->aiAttackMoveToPosition( &goal, NO_MAX_SHOTS_LIMIT, CMD_FROM_AI );
	}
	DEBUG_LOG(("AI RAIDER frame %d player %d sends %d at '%s' %d at (%.0f,%.0f), %d known guns over it\n", now, me, (Int)party.size(),
		target->getTemplate()->getName().str(), target->getID(), bestWorker ? target->getPosition()->x : goal.x,
		bestWorker ? target->getPosition()->y : goal.y, bestGuns));
}

//-------------------------------------------------------------------------------------------------
/** A raider out of the base that is not already after a gatherer turns on the nearest one this AI
	* knows of within RAID_HUNT_RADIUS: on its way, and when it gets to the supply centre it was sent at.
	* An attack-move shoots what it meets and walks on, and a truck turning off the road was let go. */
//-------------------------------------------------------------------------------------------------
void AIPlayer::huntGatherers( void )
{
	const Int me = m_player->getPlayerIndex();
	for( Int r = 0; r < MAX_PERSONA_RAIDERS; ++r )
	{
		Object *raider = TheGameLogic->findObjectByID( m_raider[ r ] );
		if( raider == NULL || raider->isEffectivelyDead() || raider->isContained() || isAtHome( raider->getPosition() ) )
			continue;
		// a raider at an empty start crosses it off as a scout would, so the next sortie goes elsewhere
		for( Int startNdx = 0; startNdx < MAX_PLAYER_COUNT; ++startNdx )
		{
			Coord3D there;
			if( m_startChecked[ startNdx ] || !startPositionLoc( startNdx, &there ) ||
					sqr( there.x - raider->getPosition()->x ) + sqr( there.y - raider->getPosition()->y ) > sqr( raider->getVisionRange() ) )
				continue;
			m_startChecked[ startNdx ] = TRUE;
			for( Int i = 0; i < ThePlayerList->getPlayerCount() && i < MAX_PLAYER_COUNT; ++i )
			{
				Player *p = ThePlayerList->getNthPlayer( i );
				if( p != m_player && p->getMpStartIndex() == startNdx && m_player->getRelationship( p->getDefaultTeam() ) == ENEMIES )
				{
					m_startOccupied[ startNdx ] = TRUE;
					m_playerStartNdx[ i ] = startNdx;
				}
			}
			DEBUG_LOG(("AI RAIDER frame %d player %d looks at start position %d: %s\n", TheGameLogic->getFrame(), me, startNdx + 1,
				m_startOccupied[ startNdx ] ? "an enemy" : "empty"));
		}
		// already on one: the goal reads empty while the chase is in a substate, and the order went out
		// again every two seconds
		const Object *goal = raider->getAI()->getGoalObject();
		if( ( goal && isGroundGatherer( goal ) ) || raider->getAI()->getCurrentStateID() == AI_ATTACK_OBJECT )
			continue;
		PartitionFilterPlayerAffiliation enemies( m_player, ALLOW_ENEMIES, true );
		PartitionFilterAlive alive;
		PartitionFilter *filters[] = { &enemies, &alive, NULL };
		ObjectIterator *iter = ThePartitionManager->iterateObjectsInRange( raider->getPosition(), RAID_HUNT_RADIUS, FROM_BOUNDINGSPHERE_2D,
			filters, ITER_SORTED_NEAR_TO_FAR );
		MemoryPoolObjectHolder hold( iter );
		for( Object *obj = iter->first(); obj; obj = iter->next() )
		{
			// the affiliation filter lets this player's own through too: the first version chased its own workers
			if( !isGroundGatherer( obj ) || m_player->getRelationship( obj->getTeam() ) != ENEMIES || !observerKnowsAbout( obj, me ) )
				continue;
			raider->getAI()->aiAttackObject( obj, NO_MAX_SHOTS_LIMIT, CMD_FROM_AI );
			DEBUG_LOG(("AI RAIDER frame %d player %d turns '%s' %d on '%s' %d\n", TheGameLogic->getFrame(), me,
				raider->getTemplate()->getName().str(), raider->getID(), obj->getTemplate()->getName().str(), obj->getID()));
			break;
		}
	}
}

//-------------------------------------------------------------------------------------------------
/** The Supporter answers a call: an ally hit in the last ten seconds with a known enemy that can
	* shoot standing in his base sends this AI's idle units at home to the intruder nearest the middle
	* of it, one ally a pass, while its own base is quiet. */
//-------------------------------------------------------------------------------------------------
void AIPlayer::defendAlly( void )
{
	if( !m_baseCenterSet || isBaseUnderAttack() )
		return;
	const UnsignedInt now = TheGameLogic->getFrame();
	const Int me = m_player->getPlayerIndex();
	for( Int i = 0; i < ThePlayerList->getPlayerCount(); ++i )
	{
		Player *ally = ThePlayerList->getNthPlayer( i );
		const UnsignedInt hit = ally->getAttackedFrame();
		if( ally == m_player || hit == 0 || hit + 10 * LOGICFRAMES_PER_SECOND <= now || !ally->isPlayableSide() ||
				m_player->getRelationship( ally->getDefaultTeam() ) != ALLIES || ally->getRelationship( m_player->getDefaultTeam() ) != ALLIES )
			continue;
		Region2D bounds;
		getPlayerStructureBounds( &bounds, i, FALSE, me );
		Coord3D middle;
		middle.x = bounds.lo.x + bounds.width() / 2;
		middle.y = bounds.lo.y + bounds.height() / 2;
		middle.z = 0.0f;

		PartitionFilterAlive filterAlive;
		PartitionFilterOnMap filterOnMap;
		PartitionFilter *filters[] = { &filterAlive, &filterOnMap, NULL };
		ObjectIterator *iter = ThePartitionManager->iterateObjectsInRange( &middle, 2.0f * m_baseRadius, FROM_CENTER_2D, filters, ITER_SORTED_NEAR_TO_FAR );
		MemoryPoolObjectHolder hold( iter );
		Object *intruder = NULL;
		for( Object *obj = iter->first(); obj && intruder == NULL; obj = iter->next() )
			if( !obj->isKindOf( KINDOF_PROJECTILE ) && m_player->getRelationship( obj->getTeam() ) == ENEMIES &&
					obj->isAbleToAttack() && observerKnowsAbout( obj, me ) )
				intruder = obj;
		if( intruder == NULL )
			continue;

		std::vector<Object *> owned;
		m_player->iterateObjects( collectOwned, &owned );
		Int sent = 0;
		for( std::vector<Object *>::const_iterator o = owned.begin(); o != owned.end(); ++o )
		{
			Object *obj = *o;
			if( obj->isEffectivelyDead() || obj->isContained() || obj->getAI() == NULL || obj->isKindOf( KINDOF_IMMOBILE ) ||
					!isAtHome( obj->getPosition() ) || !leavesToFinish( obj ) )
				continue;
			const StateID state = obj->getAI()->getCurrentStateID();
			if( state != AI_IDLE && state != AI_GUARD && state != AI_GUARD_RETALIATE && state != AI_MOVE_AND_TIGHTEN )
				continue;
			obj->getAI()->aiAttackMoveToPosition( intruder->getPosition(), NO_MAX_SHOTS_LIMIT, CMD_FROM_AI );
			++sent;
		}
		if( sent > 0 )
			DEBUG_LOG(("AI SUPPORT frame %d player %d sends %d idle units to ally %d against '%s' at (%.0f,%.0f)\n", now, me, sent, i,
				intruder->getTemplate()->getName().str(), intruder->getPosition()->x, intruder->getPosition()->y));
		return;
	}
}

//-------------------------------------------------------------------------------------------------
/** The Economist's surplus for an ally: once a minute, part of a bank well over its target goes to the
	* poorest ally still in the game, the first in the player list of two alike.  The most each rung
	* passes at a time, Easy the least. */
//-------------------------------------------------------------------------------------------------
static const Int SHARE_CAP[ AISKILL_COUNT ] = { 1000, 2000, 3000 };
static const UnsignedInt SHARE_EVERY_FRAMES = 60 * LOGICFRAMES_PER_SECOND;

void AIPlayer::shareSurplusWithAllies( Int bank, Int target )
{
	const UnsignedInt now = TheGameLogic->getFrame();
	if( m_shareFrame != 0 && now - m_shareFrame < SHARE_EVERY_FRAMES )
		return;
	const Int amount = aiPersonaShareAmount( bank, target, SHARE_CAP[ m_skillLevel ] );
	if( amount <= 0 )
		return;
	Player *poorest = NULL;
	for( Int i = 0; i < ThePlayerList->getPlayerCount(); ++i )
	{
		Player *ally = ThePlayerList->getNthPlayer( i );
		if( ally == m_player || !ally->isPlayableSide() || !ally->hasAnyObjects() ||
				m_player->getRelationship( ally->getDefaultTeam() ) != ALLIES || ally->getRelationship( m_player->getDefaultTeam() ) != ALLIES )
			continue;
		if( poorest == NULL || ally->getMoney()->countMoney() < poorest->getMoney()->countMoney() )
			poorest = ally;
	}
	// an ally who asked for money (the board) goes before the poorest
	const AITeamRequest *asked = freshRequest( AIREQ_MONEY, SHARE_EVERY_FRAMES );
	if( asked )
	{
		Player *asker = ThePlayerList->getNthPlayer( asked->from );
		if( asker->isPlayableSide() && asker->hasAnyObjects() && m_player->getRelationship( asker->getDefaultTeam() ) == ALLIES &&
				asker->getRelationship( m_player->getDefaultTeam() ) == ALLIES )
			poorest = asker;
	}
	if( poorest == NULL )
		return;
	m_shareFrame = now;
	m_personaShared += amount;
	// straight from bank to bank inside the logic, as MSG_TRANSFER_MONEY does: every machine runs this
	// same decision, so nothing goes through the message stream
	const UnsignedInt moved = m_player->getMoney()->withdraw( (UnsignedInt)amount );
	poorest->getMoney()->deposit( moved );
	if( asked && poorest->getPlayerIndex() == asked->from )
		coopChat( "Money" );
	DEBUG_LOG(("AI SHARE frame %d player %d gives %u to player %d (%d in its bank now)%s, %d in ours, bank target %d\n", now,
		m_player->getPlayerIndex(), moved, poorest->getPlayerIndex(), poorest->getMoney()->countMoney(),
		asked && poorest->getPlayerIndex() == asked->from ? ", asked for" : "", m_player->getMoney()->countMoney(), target));
}

//-------------------------------------------------------------------------------------------------
/** The team board.  A request lands in every computer ally's inbox, the latest of each kind kept; a
	* personality answers, Normal never reads it, so Normal teams play as before. */
//-------------------------------------------------------------------------------------------------
static const Real COOP_ENEMY_REACH = 1000.0f;
/** A computer player short of this much, three minutes in, asks its allies for money. */
static const Int COOP_POOR_BANK = 300;
static const UnsignedInt COOP_POOR_AFTER_FRAMES = 180 * LOGICFRAMES_PER_SECOND;

void aiPostTeamRequest( Player *from, AITeamRequestKind kind, Int enemy, const Coord3D *pos )
{
	for( Int i = 0; i < ThePlayerList->getPlayerCount(); ++i )
	{
		Player *ally = ThePlayerList->getNthPlayer( i );
		if( ally != from && ally->getRelationship( from->getDefaultTeam() ) == ALLIES && from->getRelationship( ally->getDefaultTeam() ) == ALLIES )
			ally->noteTeamRequest( kind, from->getPlayerIndex(), enemy, pos );
	}
}

Int aiEnemyNear( const Player *of, const Coord3D *pos )
{
	PartitionFilterAlive filterAlive;
	PartitionFilterOnMap filterOnMap;
	PartitionFilter *filters[] = { &filterAlive, &filterOnMap, NULL };
	ObjectIterator *iter = ThePartitionManager->iterateObjectsInRange( pos, COOP_ENEMY_REACH, FROM_CENTER_2D, filters, ITER_SORTED_NEAR_TO_FAR );
	MemoryPoolObjectHolder hold( iter );
	for( Object *obj = iter->first(); obj; obj = iter->next() )
	{
		const Player *owner = obj->getControllingPlayer();
		if( owner && owner->isPlayableSide() && of->getRelationship( obj->getTeam() ) == ENEMIES )
			return owner->getPlayerIndex();
	}
	return -1;
}

void AIPlayer::noteTeamRequest( AITeamRequestKind kind, Int from, Int enemy, const Coord3D *pos )
{
	AITeamRequest &slot = m_inbox[ kind ];
	slot.from = from;
	slot.enemy = enemy;
	slot.pos = *pos;
	slot.frame = TheGameLogic->getFrame();
}

const AITeamRequest *AIPlayer::freshRequest( AITeamRequestKind kind, UnsignedInt within ) const
{
	const AITeamRequest &slot = m_inbox[ kind ];
	return slot.frame != 0 && TheGameLogic->getFrame() - slot.frame < within ? &slot : NULL;
}

void AIPlayer::coopChat( const char *trigger )
{
	const UnsignedInt now = TheGameLogic->getFrame();
	if( !aiPersonaChatAllowed( now, m_chatFrame ) )
		return;
	AsciiString label;
	label.format( "GUI:AICoop%s", trigger );
	m_chatFrame = now;
	DEBUG_LOG(("AI CHAT frame %d player %d %s\n", now, m_player->getPlayerIndex(), label.str()));
	TheInGameUI->chatMessage( m_player, TheGameText->fetch( label ) );
}

/** A computer player's own requests: the armed enemy nearest the middle of its base while it is hit,
	* by air or not, and its empty bank.  Its attacks are posted where they are sent (sendWave). */
void AIPlayer::postTeamRequests( void )
{
	if( !m_baseCenterSet )
		return;
	const UnsignedInt now = TheGameLogic->getFrame();
	if( isBaseUnderAttack() )
	{
		PartitionFilterAlive filterAlive;
		PartitionFilterOnMap filterOnMap;
		PartitionFilter *filters[] = { &filterAlive, &filterOnMap, NULL };
		ObjectIterator *iter = ThePartitionManager->iterateObjectsInRange( &m_baseCenter, 1.5f * m_baseRadius, FROM_CENTER_2D, filters, ITER_SORTED_NEAR_TO_FAR );
		MemoryPoolObjectHolder hold( iter );
		for( Object *obj = iter->first(); obj; obj = iter->next() )
		{
			if( obj->isKindOf( KINDOF_PROJECTILE ) || !obj->isAbleToAttack() || m_player->getRelationship( obj->getTeam() ) != ENEMIES )
				continue;
			const Int enemy = obj->getControllingPlayer()->getPlayerIndex();
			aiPostTeamRequest( m_player, obj->isKindOf( KINDOF_AIRCRAFT ) ? AIREQ_AA : AIREQ_HELP, enemy, obj->getPosition() );
			break;
		}
	}
	if( now > COOP_POOR_AFTER_FRAMES && (Int)m_player->getMoney()->countMoney() < COOP_POOR_BANK )
		aiPostTeamRequest( m_player, AIREQ_MONEY, -1, &m_baseCenter );
}

/** An ally's call for help, answered by the Air AI with its idle aircraft at the attacker (an air raid
	* too) and by the Attacker and Rusher with a counter-attack of what idles at home on the attacker's
	* own base.  The Supporter has defendAlly, the Tech AI its powers (computeSuperweaponTarget), the
	* Economist the money (shareSurplusWithAllies).  Once a request: what went is not sent again. */
void AIPlayer::answerTeamRequests( void )
{
	if( !m_baseCenterSet || isBaseUnderAttack() )
		return;
	const Bool air = m_persona == AIPERSONA_AIR;
	const Bool counter = m_persona == AIPERSONA_ATTACKER || m_persona == AIPERSONA_RUSHER;
	if( !air && !counter )
		return;
	const AITeamRequest *call = freshRequest( AIREQ_AA, AI_COOP_HELP_FRAMES );
	if( call == NULL || !air )
		call = freshRequest( AIREQ_HELP, AI_COOP_HELP_FRAMES );
	if( call == NULL || call->frame == m_answeredFrame )
		return;
	m_answeredFrame = call->frame;

	Coord3D goal = call->pos;
	if( counter )
	{
		if( call->enemy < 0 )
			return;
		Region2D bounds;
		getPlayerStructureBounds( &bounds, call->enemy, FALSE, m_player->getPlayerIndex() );
		goal.x = bounds.lo.x + bounds.width() / 2;
		goal.y = bounds.lo.y + bounds.height() / 2;
		goal.z = TheTerrainLogic->getGroundHeight( goal.x, goal.y );
	}
	std::vector<Object *> owned;
	m_player->iterateObjects( collectOwned, &owned );
	Int sent = 0;
	for( std::vector<Object *>::const_iterator o = owned.begin(); o != owned.end(); ++o )
	{
		Object *obj = *o;
		if( obj->isEffectivelyDead() || obj->isContained() || obj->getAI() == NULL || obj->isKindOf( KINDOF_IMMOBILE ) ||
				obj->isKindOf( KINDOF_STRUCTURE ) || !obj->isAbleToAttack() || ( !air && obj->isKindOf( KINDOF_AIRCRAFT ) ) ||
				!isAtHome( obj->getPosition() ) || !leavesToFinish( obj ) || isOutOnOrders( obj ) )
			continue;
		const StateID state = obj->getAI()->getCurrentStateID();
		if( state != AI_IDLE && state != AI_GUARD && state != AI_GUARD_RETALIATE )
			continue;
		obj->getAI()->aiAttackMoveToPosition( &goal, NO_MAX_SHOTS_LIMIT, CMD_FROM_AI );
		++sent;
	}
	if( sent == 0 )
		return;
	coopChat( "Help" );
	DEBUG_LOG(("AI COOP frame %d player %d answers player %d's call (%s) with %d %s at (%.0f,%.0f)\n", TheGameLogic->getFrame(),
		m_player->getPlayerIndex(), call->from, call == &m_inbox[ AIREQ_AA ] ? "aircraft" : "base", sent,
		air ? "aircraft" : "units on his attacker's base", goal.x, goal.y));
}

//-------------------------------------------------------------------------------------------------
/** The Sneaky wave's road: the back door and the flank by turns, a minute each, whichever the map has
	* from where the wave stands.  A lane a script named for itself stays the script's. */
//-------------------------------------------------------------------------------------------------
AsciiString AIPlayer::sneakyLane( const Coord3D *from, const AsciiString &requested, Int pathSuffix )
{
	if( requested.compareNoCase( SKIRMISH_CENTER ) != 0 && requested.compareNoCase( SKIRMISH_FLANK ) != 0 &&
			requested.compareNoCase( SKIRMISH_BACKDOOR ) != 0 )
		return requested;
	static const char *const SIDE_ROADS[ 2 ] = { SKIRMISH_BACKDOOR, SKIRMISH_FLANK };
	const Int turn = (Int)( ( TheGameLogic->getFrame() / ( 60 * LOGICFRAMES_PER_SECOND ) ) % 2 );
	for( Int k = 0; k < 2; ++k )
	{
		const char *lane = SIDE_ROADS[ (turn + k) % 2 ];
		AsciiString label;
		label.format( "%s%d", lane, pathSuffix );
		if( TheTerrainLogic->getClosestWaypointOnPath( from, label ) == NULL )
			continue;
		DEBUG_LOG(("AI LANE frame %d player %d sneaks down %s instead of %s\n", TheGameLogic->getFrame(), m_player->getPlayerIndex(),
			lane, requested.str()));
		return AsciiString( lane );
	}
	return requested;
}

//-------------------------------------------------------------------------------------------------
/** HEADLESS PERSONA: what this seat's personality did over the match, from counters no decision reads
	* and the score keeper.  The shares are of the units it built that fly, walk and drive. */
//-------------------------------------------------------------------------------------------------
void AIPlayer::logPersonaSummary( void )
{
	ScoreKeeper *score = m_player->getScoreKeeper();
	KindOfMaskType none = KINDOFMASK_NONE;
	KindOfMaskType air = none;
	air.set( KINDOF_AIRCRAFT );
	KindOfMaskType infantry = none;
	infantry.set( KINDOF_INFANTRY );
	KindOfMaskType vehicle = none;
	vehicle.set( KINDOF_VEHICLE );
	KindOfMaskType notVehicle = air;
	notVehicle.set( KINDOF_HARVESTER );
	notVehicle.set( KINDOF_DOZER );
	const Int aircraft = score->getTotalUnitsBuilt( air, none );
	const Int soldiers = score->getTotalUnitsBuilt( infantry, air );
	const Int vehicles = score->getTotalUnitsBuilt( vehicle, notVehicle );
	const Int total = aircraft + soldiers + vehicles;
	const Real per = total > 0 ? 1.0f / INT_TO_REAL( total ) : 0.0f;
	KindOfMaskType gatherer = none;
	gatherer.set( KINDOF_HARVESTER );
	KindOfMaskType worker = none;
	worker.set( KINDOF_DOZER );
	// on the ground: a Chinook is a gatherer too, and whatever shot it down was no raid
	KindOfMaskType gathererOrAir = gatherer;
	gathererOrAir.set( KINDOF_AIRCRAFT );
	const Int gathererKills = score->getTotalObjectsDestroyed( gatherer, air ) + score->getTotalObjectsDestroyed( worker, gathererOrAir );
	// the guns standing measured against the chokes a Turtle on this rung would fortify, whatever the seat plays
	std::vector<Object *> defenses;
	m_player->iterateObjects( collectFinishedDefenses, &defenses );
	Int chokeDefenses = 0;
	Int chokeMined = 0;
	Int measured = 0;
	Real chokeDistance = 0.0f;
	for( size_t d = 0; d < defenses.size(); ++d )
	{
		const Real distance = nearestChokeDistance( defenses[ d ]->getPosition() );
		if( distance < 0.0f )
			continue;
		++measured;
		chokeDistance += distance;
		if( distance > TURTLE_CHOKE_GUARD )
			continue;
		++chokeDefenses;
		const UpgradeTemplate *mines = mineUpgradeOn( defenses[ d ] );
		if( mines && defenses[ d ]->hasUpgrade( mines ) )
			++chokeMined;
	}
	std::vector<Coord3D> traps;
	m_player->iterateObjects( collectDemoTraps, &traps );
	// the share of the kind the personality is about, -1 for one with none (and Normal)
	const Int shareKind = personaShareKind();
	DEBUG_LOG(("HEADLESS PERSONA %d: %s waves %d defenses %d aircraftShare %.2f infantryShare %.2f vehicleShare %.2f gathererKills %d wouldShare %d superweapon %u firstWave %u chokeDefenses %d chokeDist %.0f chokeMined %d demoTraps %d favouredShare %.2f stealthShare %.2f\n",
		m_player->getPlayerIndex(), aiPersonalityName( m_persona ), m_personaWaves, tallyBase( m_player ).defenses,
		INT_TO_REAL( aircraft ) * per, INT_TO_REAL( soldiers ) * per, INT_TO_REAL( vehicles ) * per, gathererKills,
		m_personaShared, m_superweaponFrame, m_firstWaveFrame, chokeDefenses,
		measured > 0 ? chokeDistance / INT_TO_REAL( measured ) : 0.0f, chokeMined, (Int)traps.size(),
		shareKind >= 0 ? builtKindShare( m_player, shareKind ) : -1.0f, builtKindShare( m_player, SHARE_STEALTH )));
}


//----------------------------------------------------------------------------------------------------------
/**
 * Finds a dozer that isn't building or collecting resources.
 */
struct FindDozerSearch
{
	const Coord3D *pos;
	ObjectID repairDozer;
	Bool needDozer;
	Object *dozer;
	Object *closestDozer;
	Real closestDistSqr;
};

static void considerDozer( Object *obj, void *userData )
{
	FindDozerSearch *search = (FindDozerSearch *)userData;
	if (!obj->isKindOf(KINDOF_DOZER))
		return;

	AIUpdateInterface *ai = obj->getAIUpdateInterface();
	if (ai==NULL)
		return;

	DozerAIInterface* dozerAI = ai->getDozerAIInterface();
	if (!dozerAI)
		return;

	// Since workers can be dozers, hmmm....
	SupplyTruckAIInterface* supplyTruckAI = ai->getSupplyTruckAIInterface();
	if( !dozerAI->isAnyTaskPending() && supplyTruckAI ) {
		// If it is gathering supplies, don't steal it.
		if (supplyTruckAI->isCurrentlyFerryingSupplies() || supplyTruckAI->isForcedIntoWantingState())
			return;
	}
	if (obj->getID() == search->repairDozer)
		return; // don't steal the repair dozer.

	search->needDozer = false; // dozer exists, may be busy.
	if (dozerAI->isTaskPending(DOZER_TASK_BUILD))
		return; // already building.

	if (!dozerAI->isAnyTaskPending())
		search->dozer = obj; // prefer an idle dozer
	if (search->dozer==NULL)
		search->dozer = obj; // but we'll take one doing stuff.

	if (search->dozer && !dozerAI->isAnyTaskPending()) {
		// Got a good one, track closest.
		const Real dx = search->pos->x - search->dozer->getPosition()->x;
		const Real dy = search->pos->y - search->dozer->getPosition()->y;
		const Real distSqr = dx*dx+dy*dy;
		if (search->closestDozer == NULL || distSqr < search->closestDistSqr) {
			search->closestDozer = search->dozer;
			search->closestDistSqr = distSqr;
		}
	}
}

/* Walks this player's own objects rather than every object in the game. The skirmish AI asks once per
	 missing build-list entry, so a late 4v4 walked the whole world twenty times in one frame here:
	 27ms of a 50ms base-building pass. The player's own list is a few dozen objects. */
Object * AIPlayer::findDozer( const Coord3D *pos )
{
	FindDozerSearch search;
	search.pos = pos;
	search.repairDozer = m_repairDozer;
	search.needDozer = true;
	search.dozer = NULL;
	search.closestDozer = NULL;
	search.closestDistSqr = 0;

	m_player->iterateObjects( considerDozer, &search );

	if (search.needDozer) {
		queueDozer();
	}
	if (search.closestDozer) return search.closestDozer;
	return search.dozer;
}

/** The nearest live dozer, busy or not. */
struct NearestDozerSearch
{
	Coord3D pos;
	Object *dozer;
	Real distSqr;
};

static void considerAnyDozer( Object *obj, void *userData )
{
	NearestDozerSearch *search = (NearestDozerSearch *)userData;
	if( !obj->isKindOf( KINDOF_DOZER ) || obj->isEffectivelyDead() || obj->getAI() == NULL )
		return;
	const Real distSqr = sqr( obj->getPosition()->x - search->pos.x ) + sqr( obj->getPosition()->y - search->pos.y );
	if( search->dozer == NULL || distSqr < search->distSqr )
	{
		search->dozer = obj;
		search->distSqr = distSqr;
	}
}

Object * AIPlayer::findNearestDozer( const Coord3D *pos )
{
	NearestDozerSearch search;
	search.pos = *pos;
	search.dozer = NULL;
	search.distSqr = 0.0f;
	m_player->iterateObjects( considerAnyDozer, &search );
	return search.dozer;
}

// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void AIPlayer::crc( Xfer *xfer )
{

}  // end crc

// ------------------------------------------------------------------------------------------------
/** Xfer method
	* Version Info:
	* 1: Initial version 
	* 2: added m_teamSeconds delay.
	* 3: Added m_curWarehouseID.
	* 1: Reset back to 1 with major save file changes.
*/
// ------------------------------------------------------------------------------------------------
void AIPlayer::xfer( Xfer *xfer )
{

	// version
	XferVersion currentVersion = 34;		// 34: the word of a launch not yet acted on  2: scout 3: rung and role  4: scouting stamps  5: the capturer  6: parked waves  7: superweapon aims  8: the hijacker  9: tactical steps  10: the ferry and dropped riders  11: the outward placement ring  12: the pressure level  13: falling back  14: gunship Chinooks and the boarding wait  15: transport Chinooks and the lent one  16: healer and raider Helixes  17: the seen-enemy ledger  18: the fist  19: the Ambulances  20: the last Ambulance lost  21: the purchase queue's reservation  22: the measured income  23: the Ambulances bought  24: the starting fortune  25: the factory backlog clocks  26: the personality  27: the team request inbox  28: the last aircraft refused for parking  29: the watched sites and the cooled spots  30: the factory backlog as a window of passes  31: the factories that let a unit out since the last pass  32: the guns known by their shells, the answers to fire  33: the influence map being stamped
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );

	// team build queue count
	UnsignedShort teamBuildQueueCount = 0;
	for( DLINK_ITERATOR< TeamInQueue > teamInQueueIt = iterate_TeamBuildQueue();
			 teamInQueueIt.done() == FALSE;
			 teamInQueueIt.advance() )
		teamBuildQueueCount++;
	xfer->xferUnsignedShort( &teamBuildQueueCount );

	// team build queue data
	TeamInQueue *teamInQueue;
	if( xfer->getXferMode() == XFER_SAVE )
	{

		for( DLINK_ITERATOR< TeamInQueue > teamInQueueIt = iterate_TeamBuildQueue();
				 teamInQueueIt.done() == FALSE;
				 teamInQueueIt.advance() )
		{
		
			// get element data
			teamInQueue = teamInQueueIt.cur();

			// xfer it
			xfer->xferSnapshot( teamInQueue );

		}  // end for, iterate team build queue

	}  // end if, save
	else
	{

		// sanity, the list must be empty
		if( getFirstItemIn_TeamBuildQueue() != NULL )
		{
		
			DEBUG_CRASH(( "AIPlayer::xfer - TeamBuildQueue head is not NULL, you should delete it or something before loading a new list\n" ));
			throw SC_INVALID_DATA;

		}  // end if

		// ready all data
		for( UnsignedShort i = 0; i < teamBuildQueueCount; ++i )
		{

			// allocate new team in queue instance
			teamInQueue = newInstance(TeamInQueue);

			// attach to end of list
			prependTo_TeamBuildQueue( teamInQueue );

			// xfer data
			xfer->xferSnapshot( teamInQueue );

		}  // end for, i

		// the list was loaded in reverse order, reverse the list so it's in the same order as before
		reverse_TeamBuildQueue();

	}  // end else, load

	// team ready queue count
	UnsignedShort teamReadyQueueCount = 0;
	for( DLINK_ITERATOR< TeamInQueue > teamReadyQueueIt = iterate_TeamReadyQueue();
			 teamReadyQueueIt.done() == FALSE;
			 teamReadyQueueIt.advance() )
		teamReadyQueueCount++;
	xfer->xferUnsignedShort( &teamReadyQueueCount );

	// team Ready queue data
	TeamInQueue *teamReadyQueue;
	if( xfer->getXferMode() == XFER_SAVE )
	{

		for( DLINK_ITERATOR< TeamInQueue > teamReadyQueueIt = iterate_TeamReadyQueue();
				 teamReadyQueueIt.done() == FALSE;
				 teamReadyQueueIt.advance() )
		{
		
			// get element
			teamReadyQueue = teamReadyQueueIt.cur();
			
			// xfer data
			xfer->xferSnapshot( teamReadyQueue );

		}  // end for, iterate team ready queue

	}  // end if, save
	else
	{

		// sanity, the list must be empty
		if( getFirstItemIn_TeamReadyQueue() != NULL )
		{
		
			DEBUG_CRASH(( "AIPlayer::xfer - TeamReadyQueue head is not NULL, you should delete it or something before loading a new list\n" ));
			throw SC_INVALID_DATA;

		}  // end if

		// read all data
		for( UnsignedShort i = 0; i < teamReadyQueueCount; ++i )
		{

			// allocate new team in queue instance
			teamInQueue = newInstance(TeamInQueue);

			// attach to end of list
			prependTo_TeamReadyQueue( teamInQueue );

			// xfer data
			xfer->xferSnapshot( teamInQueue );

		}  // end for, i

		// reverse the list since it was loaded in reverse order due to the prepend
		reverse_TeamReadyQueue();

	}  // end else, load

	// xfer player index ... this is really just for sanity
	PlayerIndex playerIndex = m_player->getPlayerIndex();
	xfer->xferUser( &playerIndex, sizeof( PlayerIndex ) );
	if( playerIndex != m_player->getPlayerIndex() )
	{

		DEBUG_CRASH(( "AIPlayer::xfer - player index mismatch\n" ));
		throw SC_INVALID_DATA;

	}  // end if

	// xfer the rest of the ai player data (it's pretty straight forward)
	xfer->xferBool( &m_readyToBuildTeam );
	xfer->xferBool( &m_readyToBuildStructure );
	xfer->xferInt( &m_teamTimer );
	xfer->xferInt( &m_structureTimer );

	xfer->xferInt( &m_buildDelay );
	xfer->xferInt( &m_teamDelay );

	xfer->xferInt(&m_teamSeconds);
 	xfer->xferObjectID(&m_curWarehouseID);

	xfer->xferInt( &m_frameLastBuildingBuilt );

	xfer->xferUser( &m_difficulty, sizeof( GameDifficulty ) );
	xfer->xferInt( &m_skillsetSelector );

	xfer->xferCoord3D( &m_baseCenter );
	xfer->xferBool( &m_baseCenterSet );
	xfer->xferReal( &m_baseRadius );

	// the scout, so a loaded game does not go blind or start a second one
	if( version >= 2 )
	{
		xfer->xferInt( &m_scoutTimer );
		for( Int scoutSlot = 0; scoutSlot < MAX_AI_SCOUTS; ++scoutSlot )
		{
			xfer->xferObjectID( &m_scoutID[ scoutSlot ] );
			xfer->xferInt( &m_scoutTargetFor[ scoutSlot ] );
		}
	}
	// what has been looked at and what was found there, so a loaded game does not search a map it
	// has already searched - or forget an enemy it had located
	if( version >= 4 )
	{
		for( Int startNdx = 0; startNdx < MAX_PLAYER_COUNT; ++startNdx )
		{
			xfer->xferUnsignedInt( &m_scoutSeenFrame[ startNdx ] );
			xfer->xferBool( &m_startChecked[ startNdx ] );
			xfer->xferBool( &m_startOccupied[ startNdx ] );
			xfer->xferInt( &m_playerStartNdx[ startNdx ] );
		}
	}
	if( version >= 3 )
	{
		xfer->xferInt( &m_retreatTimer );
		xfer->xferInt( &m_expandTimer );
	}
	// the capturer, so a loaded game does not build a second one
	if( version >= 5 )
	{
		xfer->xferObjectID( &m_capturerID );
		xfer->xferInt( &m_captureTimer );
	}
	// the thief, for the same reason: a loaded game does not buy a second one
	if( version >= 8 )
	{
		xfer->xferObjectID( &m_hijackerID );
		xfer->xferInt( &m_hijackTimer );
	}
	// the attack teams parked at the staging point, so a loaded game sends the same wave
	if( version >= 6 )
	{
		for( Int held = 0; held < MAX_HELD_TEAMS; ++held )
		{
			xfer->xferBool( &m_heldUsed[ held ] );
			xfer->xferUnsignedInt( &m_heldTeam[ held ] );
			xfer->xferAsciiString( &m_heldLabel[ held ] );
			xfer->xferInt( &m_heldSuffix[ held ] );
		}
		xfer->xferUnsignedInt( &m_heldSince );
	}
	// where the last shots were aimed, so a loaded game does not put the next one in the same crater
	if( version >= 7 )
	{
		for( Int strike = 0; strike < MAX_REMEMBERED_STRIKES; ++strike )
		{
			xfer->xferCoord3D( &m_strikeAim[ strike ] );
			xfer->xferUnsignedInt( &m_strikeFrame[ strike ] );
		}
		xfer->xferInt( &m_strikeNext );
	}
	// the units part way through a tactical step, above all the mood each was calmed from: a game saved
	// in the middle of a kite would otherwise load a unit that stays calm for the rest of the match.  And
	// the influence map as it stands, since the next rebuild can be seconds off below Hard and everything
	// that picks where to stand reads it before then.
	if( version >= 9 )
	{
		m_influence.xfer( xfer );
		UnsignedShort rows = (UnsignedShort)m_tactics.size();
		xfer->xferUnsignedShort( &rows );
		if( xfer->getXferMode() == XFER_LOAD )
			m_tactics.resize( rows );
		for( UnsignedShort i = 0; i < rows; ++i )
		{
			TacticalStep &step = m_tactics[ i ];
			xfer->xferObjectID( &step.unit );
			xfer->xferObjectID( &step.target );
			xfer->xferCoord3D( &step.origin );
			xfer->xferUnsignedInt( &step.resumeFrame );
			xfer->xferUnsignedInt( &step.nextClimbFrame );
			xfer->xferUnsignedInt( &step.leaveAloneUntil );
			xfer->xferUnsignedInt( &step.lastSeenFrame );
			xfer->xferBool( &step.rejoin );
			xfer->xferInt( &step.savedAttitude );
			xfer->xferUnsignedInt( &step.lastKiteFrame );
			// a unit holding after a lost fight, so a loaded game still sends it back
			if( version >= 13 )
			{
				xfer->xferBool( &step.fallingBack );
				xfer->xferCoord3D( &step.fallbackFrom );
				xfer->xferUnsignedInt( &step.fallbackFrame );
			}
			else
			{
				step.fallingBack = FALSE;
				step.fallbackFrom.zero();
				step.fallbackFrame = 0;
			}
			if( version >= 32 )
				xfer->xferUnsignedInt( &step.answerFrame );
			else
				step.answerFrame = 0;
		}
	}
	// the capturer's helicopter, and the riders a helicopter is putting down at a fight
	if( version >= 10 )
	{
		xfer->xferObjectID( &m_ferryID );
		UnsignedShort dropped = (UnsignedShort)m_droppedRiders.size();
		xfer->xferUnsignedShort( &dropped );
		if( xfer->getXferMode() == XFER_LOAD )
			m_droppedRiders.resize( dropped );
		for( UnsignedShort i = 0; i < dropped; ++i )
			xfer->xferObjectID( &m_droppedRiders[ i ] );
	}
	// where the next outward search starts, so a loaded game does not pack its new buildings back
	// into the opening rings the base has already filled
	if( version >= 11 )
		xfer->xferInt( &m_placementRing );
	// how hard it is leaning on its enemy, and the army of his it remembers; an older save starts even
	if( version >= 12 )
	{
		xfer->xferUser( &m_pressure, sizeof( AIPressure ) );
		xfer->xferReal( &m_knownEnemyPower );
		xfer->xferInt( &m_pressureEnemy );
	}
	// the Chinooks bought to carry riders, which a loaded game must not hand to the economy as gatherers
	if( version >= 14 )
	{
		for( Int chinook = 0; chinook < MAX_GUNSHIP_CHINOOKS; ++chinook )
			xfer->xferObjectID( &m_gunshipChinook[ chinook ] );
		xfer->xferUnsignedInt( &m_boardWaitFrame );
	}
	// the transport Chinooks mid-trip, and the wave road they fly to
	if( version >= 15 )
	{
		for( Int shuttle = 0; shuttle < MAX_TRANSPORT_CHINOOKS; ++shuttle )
		{
			ShuttleChinook &s = m_shuttle[ shuttle ];
			xfer->xferObjectID( &s.id );
			xfer->xferInt( &s.phase );
			xfer->xferUnsignedInt( &s.frame );
			xfer->xferInt( &s.load );
			xfer->xferBool( &s.aborted );
			xfer->xferCoord3D( &s.drop );
			xfer->xferCoord3D( &s.home );
		}
		xfer->xferCoord3D( &m_shuttleFront );
		xfer->xferUnsignedInt( &m_shuttleFrontFrame );
		xfer->xferInt( &m_lastWaveSlots );
		xfer->xferObjectID( &m_lentChinook );
	}
	// the healer and raider Helixes and where each is in its job
	if( version >= 16 )
	{
		for( Int helix = 0; helix < MAX_DUTY_HELIXES; ++helix )
		{
			DutyHelix &duty = m_dutyHelix[ helix ];
			xfer->xferObjectID( &duty.id );
			xfer->xferInt( &duty.role );
			xfer->xferInt( &duty.phase );
			xfer->xferUnsignedInt( &duty.frame );
			xfer->xferObjectID( &duty.target );
			xfer->xferCoord3D( &duty.spot );
			xfer->xferReal( &duty.targetHealth );
		}
	}
	// the enemy units it has seen, so a loaded game remembers the army that went back into the fog
	if( version >= 17 )
	{
		UnsignedShort seen = (UnsignedShort)m_seenEnemies.size();
		xfer->xferUnsignedShort( &seen );
		if( xfer->getXferMode() == XFER_SAVE )
		{
			for( std::map<ObjectID, SeenEnemy>::iterator it = m_seenEnemies.begin(); it != m_seenEnemies.end(); ++it )
			{
				ObjectID id = it->first;
				AsciiString name = it->second.tmpl->getName();
				xfer->xferObjectID( &id );
				xfer->xferAsciiString( &name );
				xfer->xferInt( &it->second.owner );
				xfer->xferUnsignedInt( &it->second.seen );
				xfer->xferCoord3D( &it->second.pos );
				xfer->xferBool( &it->second.stealth );
			}
		}
		else
		{
			m_seenEnemies.clear();
			for( UnsignedShort i = 0; i < seen; ++i )
			{
				ObjectID id;
				AsciiString name;
				SeenEnemy row;
				xfer->xferObjectID( &id );
				xfer->xferAsciiString( &name );
				xfer->xferInt( &row.owner );
				xfer->xferUnsignedInt( &row.seen );
				xfer->xferCoord3D( &row.pos );
				xfer->xferBool( &row.stealth );
				row.tmpl = TheThingFactory->findTemplate( name, FALSE );
				if( row.tmpl )
					m_seenEnemies[ id ] = row;
			}
		}
	}
	// the fist: the unit picked, when, and what of it is waiting
	if( version >= 18 )
	{
		AsciiString massName = m_massTemplate ? m_massTemplate->getName() : AsciiString::TheEmptyString;
		xfer->xferAsciiString( &massName );
		if( xfer->getXferMode() == XFER_LOAD )
			m_massTemplate = massName.isEmpty() ? NULL : TheThingFactory->findTemplate( massName, FALSE );
		xfer->xferUnsignedInt( &m_massPickFrame );
		xfer->xferUnsignedInt( &m_massSince );
		UnsignedShort held = (UnsignedShort)m_massUnits.size();
		xfer->xferUnsignedShort( &held );
		if( xfer->getXferMode() == XFER_LOAD )
			m_massUnits.resize( held );
		for( UnsignedShort i = 0; i < held; ++i )
			xfer->xferObjectID( &m_massUnits[ i ] );
	}
	if( version >= 19 )
	{
		for( Int slot = 0; slot < MAX_DUTY_AMBULANCES; ++slot )
		{
			xfer->xferObjectID( &m_ambulance[ slot ].id );
			xfer->xferCoord3D( &m_ambulance[ slot ].spot );
		}
	}
	if( version >= 20 )
		xfer->xferUnsignedInt( &m_ambulanceLost );
	if( version >= 21 )
	{
		xfer->xferInt( &m_purchases.m_key );
		xfer->xferInt( &m_purchases.m_cost );
		xfer->xferInt( &m_purchases.m_score );
		xfer->xferUnsignedInt( &m_purchases.m_since );
		xfer->xferUnsignedInt( &m_purchases.m_seen );
		xfer->xferInt( &m_purchases.m_held );
		xfer->xferInt( &m_purchases.m_lapsedKey );
		xfer->xferUnsignedInt( &m_purchases.m_lapsedUntil );
		xfer->xferAsciiString( &m_purchaseSavingFor );
	}
	if( version >= 22 )
	{
		xfer->xferInt( &m_incomeMark );
		xfer->xferUnsignedInt( &m_incomeMarkFrame );
		xfer->xferInt( &m_bankMark );
		xfer->xferReal( &m_incomePerSecond );
	}
	if( version >= 23 )
		xfer->xferInt( &m_ambulancesBought );
	if( version >= 24 )
		xfer->xferInt( &m_startFortune );
	// the factory backlog: from 30 the rolling window of full passes, before it a clock of frames that
	// means nothing to the window, read and dropped so the window starts empty
	if( version >= 30 )
	{
		xfer->xferUser( m_factoryFullHistory, sizeof( m_factoryFullHistory ) );
		xfer->xferUser( m_factoryFullCount, sizeof( m_factoryFullCount ) );
	}
	else if( version >= 25 )
	{
		UnsignedInt oldClocks[ 3 ];
		xfer->xferUser( oldClocks, sizeof( oldClocks ) );
	}

	// the ladder rung and the role, which are rolled once and must come back the same way
	if( version >= 3 )
	{
		xfer->xferUser( &m_skillLevel, sizeof( AISkillLevel ) );
		xfer->xferUser( &m_role, sizeof( AIRole ) );
	}
	// the personality, drawn once like the role, and what it keeps
	if( version >= 26 )
	{
		xfer->xferUser( &m_persona, sizeof( AIPersonality ) );
		xfer->xferBool( &m_personaFromRandom );
		xfer->xferBool( &m_rushSent );
		xfer->xferBool( &m_techDone );
		xfer->xferUnsignedInt( &m_raidFrame );
		xfer->xferUser( m_raider, sizeof( m_raider ) );
		xfer->xferUnsignedInt( &m_shareFrame );
		xfer->xferUnsignedInt( &m_chatFrame );
		xfer->xferInt( &m_beatenMask );
		xfer->xferInt( &m_personaWaves );
		xfer->xferUnsignedInt( &m_firstWaveFrame );
		xfer->xferInt( &m_personaShared );
		xfer->xferUnsignedInt( &m_superweaponFrame );
	}

	xfer->xferUser( m_structuresToRepair, sizeof( ObjectID ) * MAX_STRUCTURES_TO_REPAIR );
	xfer->xferObjectID( &m_repairDozer );
	xfer->xferInt( &m_structuresInQueue );
	xfer->xferBool( &m_dozerQueuedForRepair );
	xfer->xferBool( &m_dozerIsRepairing );
	xfer->xferInt( &m_bridgeTimer );

	// an ally's calls for help, which an Air, Attacker or Rusher AI answers for a while after
	if( version >= 27 )
	{
		for( Int k = 0; k < AIREQ_COUNT; ++k )
		{
			xfer->xferInt( &m_inbox[ k ].from );
			xfer->xferInt( &m_inbox[ k ].enemy );
			xfer->xferCoord3D( &m_inbox[ k ].pos );
			xfer->xferUnsignedInt( &m_inbox[ k ].frame );
		}
		xfer->xferUnsignedInt( &m_answeredFrame );
	}

	// the last aircraft refused for parking, which keeps the airfield backlog clock running
	if( version >= 28 )
		xfer->xferUnsignedInt( &m_parkingBlockedFrame );

	// the unfinished sites it is watching, and the spots of the ones it gave up on
	if( version >= 29 )
	{
		UnsignedShort count = (UnsignedShort)m_siteWatches.size();
		xfer->xferUnsignedShort( &count );
		m_siteWatches.resize( count );
		for( UnsignedShort i = 0; i < count; ++i )
		{
			xfer->xferObjectID( &m_siteWatches[ i ].m_site );
			xfer->xferReal( &m_siteWatches[ i ].m_percent );
			xfer->xferUnsignedInt( &m_siteWatches[ i ].m_progressFrame );
			xfer->xferObjectID( &m_siteWatches[ i ].m_lostBuilder );
			xfer->xferInt( &m_siteWatches[ i ].m_buildersLost );
		}
		count = (UnsignedShort)m_cooledSpots.size();
		xfer->xferUnsignedShort( &count );
		m_cooledSpots.resize( count );
		for( UnsignedShort i = 0; i < count; ++i )
		{
			xfer->xferCoord3D( &m_cooledSpots[ i ].m_pos );
			xfer->xferUnsignedInt( &m_cooledSpots[ i ].m_until );
		}
	}

	// the factories that let a unit out since the last spend pass, which that pass counts as busy
	if( version >= 31 )
	{
		UnsignedShort count = (UnsignedShort)m_producedSincePass.size();
		xfer->xferUnsignedShort( &count );
		m_producedSincePass.resize( count );
		for( UnsignedShort i = 0; i < count; ++i )
			xfer->xferObjectID( &m_producedSincePass[ i ] );
	}

	// the guns it knows of by their shells, so a loaded game keeps off the same ground
	if( version >= 32 )
	{
		UnsignedShort reports = (UnsignedShort)m_fireReports.size();
		xfer->xferUnsignedShort( &reports );
		m_fireReports.resize( reports );
		for( UnsignedShort i = 0; i < reports; ++i )
		{
			FireReport &report = m_fireReports[ i ];
			xfer->xferObjectID( &report.shooter );
			xfer->xferCoord3D( &report.at );
			xfer->xferCoord3D( &report.hitAt );
			xfer->xferReal( &report.reach );
			xfer->xferReal( &report.power );
			xfer->xferUnsignedInt( &report.frame );
		}
		UnsignedShort gunReadings = (UnsignedShort)m_gunReadings.size();
		xfer->xferUnsignedShort( &gunReadings );
		m_gunReadings.resize( gunReadings );
		for( UnsignedShort i = 0; i < gunReadings; ++i )
		{
			GunReading &reading = m_gunReadings[ i ];
			xfer->xferObjectID( &reading.gun );
			xfer->xferReal( &reading.ours );
			xfer->xferReal( &reading.cover );
			xfer->xferReal( &reading.edge );
			xfer->xferUnsignedInt( &reading.frame );
		}
	}

	// the influence map half stamped, or a loaded game's next map would be missing the round's first half
	if( version >= 33 )
		m_influenceNext.xfer( xfer );

	if( version >= 34 )
		xfer->xferUnsignedInt( &m_strikeNoticeFrame );

}  // end xfer

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void AIPlayer::loadPostProcess( void )
{

}  // end loadPostProcess

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
TeamInQueue::~TeamInQueue()
{
	WorkOrder *order, *next;

	for( order = m_workOrders; order; order = next )
	{
		next = order->m_next;
		order->deleteInstance();
	}
	// If we have a team, activate it.  If it is empty, Team.cpp will remove empty active teams.
	if (m_team) m_team->setActive();
	m_workOrders = NULL;
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
Bool TeamInQueue::isAllBuilt()
{
	WorkOrder *order;

	Bool stillBuilding = false;
	for( order = m_workOrders; order; order = order->m_next )
	{
		if (order->m_numRequired>order->m_numCompleted) 
		{
			stillBuilding = true;
		}
	}
	return !stillBuilding;
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
Bool TeamInQueue::isBuildTimeExpired()
{
	if (m_team->getPrototype()->getTemplateInfo()->m_initialIdleFrames<1) {
		return false; // Unlimited time.
	}
	if (TheGameLogic->getFrame() > m_frameStarted + m_team->getPrototype()->getTemplateInfo()->m_initialIdleFrames) {
		return true;
	}
	return false;
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
Bool TeamInQueue::isMinimumBuilt()
{
	WorkOrder *order;

	for( order = m_workOrders; order; order = order->m_next )
	{
		Int count = order->m_numCompleted;
		if (order->m_factoryID != INVALID_ID) {
			count++; // we have one building.
		}
		if (order->m_numRequired>count) 
		{
			if (order->m_required) {
				return false; // required units not built.
			}
		}
	}
	return true;
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
Bool TeamInQueue::includesADozer()
{
	WorkOrder *order;

	for( order = m_workOrders; order; order = order->m_next )
	{
		// GLA dozers (workers) are also resource gatherers, so make sure it isn't a gatherer. jba.
		if (order->m_thing->isKindOf(KINDOF_DOZER) && !order->m_isResourceGatherer) {
			return true;
		}
	}
	return false;
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
Bool TeamInQueue::areBuildsComplete()
{
	WorkOrder *order;

	for( order = m_workOrders; order; order = order->m_next )
	{
		if (order->m_factoryID != INVALID_ID) {
			return false; // we have one building.
		}
	}
	return true;
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void TeamInQueue::disband()
{
	Team *newTeam = m_team->getPrototype()->getControllingPlayer()->getDefaultTeam();
	AsciiString teamName = m_team->getPrototype()->getName();
	teamName.concat(" - team disbanded, build time expired.");
	TheScriptEngine->AppendDebugMessage(teamName, false);
	if (m_team != newTeam) {
		m_team->transferUnitsTo(newTeam);
		if (!m_team->getPrototype()->getIsSingleton()) {
			m_team->deleteInstance();
		}
		m_team = NULL;
	}
}

// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void TeamInQueue::crc( Xfer *xfer )
{

}  // end crc

// ------------------------------------------------------------------------------------------------
/** Xfer method 
	* Version Info:
	* 1: Initial version */
// ------------------------------------------------------------------------------------------------
void TeamInQueue::xfer( Xfer *xfer )
{

	// version
	XferVersion currentVersion = 1;
	XferVersion version = currentVersion;;
	xfer->xferVersion( &version, currentVersion );

	// xfer work order count
	UnsignedShort workOrderCount = 0;
	WorkOrder *workOrder;
	for( workOrder = m_workOrders; workOrder; workOrder = workOrder->m_next )
		workOrderCount++;
	xfer->xferUnsignedShort( &workOrderCount );

	// xfer work orders
	if( xfer->getXferMode() == XFER_SAVE )
	{

		// xfer each work order
		for( workOrder = m_workOrders; workOrder; workOrder = workOrder->m_next )
		{

			// xfer work order data
			xfer->xferSnapshot( workOrder );

		}  // end for

	}  // end if, save
	else
	{

		// sanity
		if( m_workOrders != NULL )
		{

			DEBUG_CRASH(( "TeamInQueue::xfer - m_workOrders should be NULL but isn't.  Perhaps you should blow it away before loading\n" ));
			throw SC_INVALID_DATA;

		}  // end if

		// load all work orders
		for( UnsignedShort i = 0; i < workOrderCount; ++i )
		{

			// allocate new work order
			workOrder = newInstance(WorkOrder);

			// attach to list at the end
			workOrder->m_next = NULL;
			if( m_workOrders == NULL )
				m_workOrders = workOrder;
			else
			{
				WorkOrder *last = m_workOrders;

				while( last->m_next != NULL )
					last = last->m_next;

				last->m_next = workOrder;

			}  // end else

			// load work order data
			xfer->xferSnapshot( workOrder );

		}  // end for, i

	}  // end else, load

	// xfer the rest of the team in queue data
	xfer->xferBool( &m_priorityBuild );
	TeamID teamID = m_team ? m_team->getID() : TEAM_ID_INVALID;
	xfer->xferUser( &teamID, sizeof( TeamID ) );
	if( xfer->getXferMode() == XFER_LOAD )
		m_team = TheTeamFactory->findTeamByID( teamID );
	xfer->xferInt( &m_frameStarted );
	xfer->xferBool( &m_sentToStartLocation );
	xfer->xferBool( &m_stopQueueing );
	xfer->xferBool( &m_reinforcement );
	xfer->xferObjectID( &m_reinforcementID );

}  // end xfer

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void TeamInQueue::loadPostProcess( void )
{

}  // end loadPostProcess

///////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
WorkOrder::~WorkOrder()
{

}  // end WorkOrder

// ------------------------------------------------------------------------------------------------
/** Verify factoryID still refers to an active object */
// ------------------------------------------------------------------------------------------------
void WorkOrder::validateFactory( Player *thisPlayer )
{

	if (m_factoryID == INVALID_ID) 
		return;
	Object *factory = TheGameLogic->findObjectByID( m_factoryID );
	if ( factory == NULL) {
		m_factoryID = INVALID_ID;
		return;
	}
	if (factory->getControllingPlayer()!=thisPlayer) {
		m_factoryID = INVALID_ID;
	}

}  // end validateFactory

// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void WorkOrder::crc( Xfer *xfer )
{

}  // end crc

// ------------------------------------------------------------------------------------------------
/** Xfer method
	* Version Info:
	* 1: Initial version */
// ------------------------------------------------------------------------------------------------
void WorkOrder::xfer( Xfer *xfer )
{

	// version
	XferVersion currentVersion = 2;		// 2: the scout flag
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );

	// thing template
	AsciiString thingTemplateName = m_thing ? m_thing->getName() : AsciiString::TheEmptyString;
	xfer->xferAsciiString( &thingTemplateName );
	if( xfer->getXferMode() == XFER_LOAD )
		m_thing = TheThingFactory->findTemplate( thingTemplateName );

	// factory id
	xfer->xferObjectID( &m_factoryID );

	// num completed
	xfer->xferInt( &m_numCompleted );

	// num required
	xfer->xferInt( &m_numRequired );

	// is required
	xfer->xferBool( &m_required );

	// is resource gatherer
	xfer->xferBool( &m_isResourceGatherer );

	// is the map-watching scout
	if( version >= 2 )
		xfer->xferBool( &m_isScout );

}  // end xfer

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void WorkOrder::loadPostProcess( void )
{

}  // end loadPostProcess


//----------------------------------------------------------------------------------------------------------
/**
 * Get the bounds for a player's structure.
 */
void AIPlayer::getPlayerStructureBounds( Region2D *bounds, Int playerNdx, Bool conservative, Int observerNdx )
{
	Player::PlayerTeamList::const_iterator it;
	Bool firstObject = true;
	Bool firstStructure = true;
	bounds->hi.x = bounds->lo.x = bounds->hi.y = bounds->lo.y = 0;
	Region2D objBounds;
	objBounds.hi.x = objBounds.lo.x = objBounds.hi.y = objBounds.lo.y = 0;
	// EA's AI saw every building on the map, and the map scripts it runs were written for that: a
	// player's own index only limits the fork's AI, whichever caller passed it
	if (!runsOwnAI())
		observerNdx = -1;

	Player* pPlayer = ThePlayerList->getNthPlayer(playerNdx);
	if (pPlayer == NULL) 
		return;
	for (it = pPlayer->getPlayerTeams()->begin(); it != pPlayer->getPlayerTeams()->end(); ++it) 
	{
		for (DLINK_ITERATOR<Team> iter = (*it)->iterate_TeamInstanceList(); !iter.done(); iter.advance()) 
		{
			Team *team = iter.cur();
			if (!team) 
				continue;
			for (DLINK_ITERATOR<Object> iter = team->iterate_TeamMemberList(); !iter.done(); iter.advance()) 
			{
				Object *pObj = iter.cur();
				if (!pObj) 
					continue;
				if (!observerKnowsAbout(pObj, observerNdx))
					continue;			// this player has not found it yet
				const Bool isStructure = pObj->isKindOf(KINDOF_STRUCTURE);

				if( isStructure && conservative && pObj->isKindOf( KINDOF_CONSERVATIVE_BUILDING ) )
				{
					//Kris - Aug 14, 2003
					//Conservative buildings (captured tech buildings, sneak attack buildings are rejected).
					//This was added so base boundaries aren't potentially most of the map. Because sneak
					//attack uses an inverse cost calculation by avoiding defended areas, we need to keep
					//things close to the enemy base.
					continue;
				}

				{
					Coord3D pos = *pObj->getPosition();

					//
					// objBounds spans EVERY object, so it can stand in for a player who owns no
					// structures. It used to be updated only inside the structure test, which made
					// it a copy of the structure bounds and left the fallback below meaningless.
					//
					if (firstObject) {
						objBounds.lo.x = objBounds.hi.x = pos.x;
						objBounds.lo.y = objBounds.hi.y = pos.y;
						firstObject = false;
					}	
					else 
					{
						if (objBounds.lo.x>pos.x) objBounds.lo.x = pos.x;
						if (objBounds.lo.y>pos.y) objBounds.lo.y = pos.y;
						if (objBounds.hi.x<pos.x) objBounds.hi.x = pos.x;
						if (objBounds.hi.y<pos.y) objBounds.hi.y = pos.y;
					}

					if (!isStructure)
						continue;

					if (firstStructure) 
					{
						bounds->lo.x = bounds->hi.x = pos.x;
						bounds->lo.y = bounds->hi.y = pos.y;
						firstStructure = false;
					}	
					else 
					{
						if (bounds->lo.x>pos.x) bounds->lo.x = pos.x;
						if (bounds->lo.y>pos.y) bounds->lo.y = pos.y;
						if (bounds->hi.x<pos.x) bounds->hi.x = pos.x;
						if (bounds->hi.y<pos.y) bounds->hi.y = pos.y;
					}
				}
			}
		}
	}
	if (firstStructure && firstObject && observerNdx >= 0) {
		//
		// The observer has seen nothing of this player at all - the opening minutes, before anyone
		// has scouted.  Aim at the best address we have for him rather than at the (0,0) corner this
		// function would otherwise report: his real position if a scout has found him, and until then
		// whichever start position the elimination says is likeliest to be his.
		//
		Coord3D start;
		if (enemyStartGuess(playerNdx, &start)) {
			bounds->lo.x = bounds->hi.x = start.x;
			bounds->lo.y = bounds->hi.y = start.y;
		}
		return;
	}
	if (firstStructure && !firstObject) {
		// Player had no structures, so use unit bounds.
		// (the test used to be !firstStructure - i.e. it only "fell back" when structures HAD been
		// found, which was a no-op, and a structureless player was left with the (0,0,0,0) bounds
		// this function starts with. acquireEnemy, findSupplyCenter and guardSupplyCenter all then
		// aimed at the map corner.)
		*bounds = objBounds;
	}
}
