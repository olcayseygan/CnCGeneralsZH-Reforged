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
#include "Common/SpecialPower.h"
#include "Common/Team.h" 
#include "Common/ThingFactory.h"
#include "Common/PlayerList.h"
#include "Common/Recorder.h"
#include "Common/BuildAssistant.h"
#include "Common/ThingTemplate.h"
#include "Common/TunnelTracker.h"
#include "Common/Upgrade.h"
#include "Common/WellKnownKeys.h"
#include "Common/Xfer.h"
#include "GameClient/ControlBar.h"
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
#include "GameLogic/Module/CollideModule.h"	// ... and the collide that takes a vehicle by touching it
#include "GameLogic/Module/ContainModule.h"
#include "GameLogic/Module/OpenContain.h"			// a seat's own shoot-out flag, asked before a rider exists
#include "GameLogic/Module/TransportContain.h"	// ... and a template's hold, before one exists
#include "GameLogic/Module/JetAIUpdate.h"		// a Comanche is a jet with no runway
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
	* observerNdx < 0 is the old omniscient answer, for callers that are not one player's thinking. */
static Bool observerKnowsAbout( const Object *obj, Int observerNdx )
{
	if( obj == NULL )
		return FALSE;
	if( observerNdx < 0 )
		return TRUE;
	return !obj->isUnknownTo( observerNdx );
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
static Real aiCombatPower( const Object *obj )
{
	if( obj == NULL )
		return 0.0f;

	const ThingTemplate *tmpl = obj->getTemplate();
	if( tmpl == NULL )
		return 0.0f;

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

	return INT_TO_REAL( tmpl->calcCostToBuild( obj->getControllingPlayer() ) );
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
	 match, so the answer is kept. The destructor empties it, because the next match can free a template
	 and hand its address to another. */
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
	m_pressure = AIPRESSURE_NORMAL;
	m_knownEnemyPower = 0.0f;
	m_pressureEnemy = -1;

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
	m_baseCenter.zero();
	m_baseCenterSet = false;
	m_placementRing = 0;
	m_difficulty = TheScriptEngine->getGlobalDifficulty(); 
	m_skillLevel = skillLevelForDifficulty( m_difficulty );

	//
	// Role is rolled once and kept for the whole match.  Sins is explicit that its AI does not flip
	// personality mid-game, and that consistency is what makes an opponent readable and therefore
	// counterable - an AI that turtles for ten minutes and then rushes is not deep, it is noise.
	// GameLogicRandomValue, so a replay rolls the same one.
	//
	m_role = (GameLogicRandomValue( 0, AIROLE_COUNT - 1 ) == 0) ? AIROLE_AGGRESSIVE : AIROLE_DEFENSIVE;

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
	theFramesToKillCache.clear();
	theMaxHealthCache.clear();
	theDetectsStealthCache.clear();
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
		const AISideInfo *resInfo = TheAI->getAiData()->m_sideInfo;
		while (resInfo) {
			if (resInfo->m_side == m_player->getSide()) {
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
			resInfo = resInfo->m_next;
		}
		//
		// Adaptive harvesters (B6): the three INI numbers do not know how much supply is actually
		// parked next to this centre.  One extra gatherer per warehouse beyond the first within the
		// safe radius, capped at two extra - a centre with three piles beside it can keep more
		// trucks busy, and one with a single pile cannot.
		//
		if( getSkillProfile()->m_adaptiveHarvesters )
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

/** A warehouse with boxes left, not an enemy's, within reach of this supply center.  Any of them:
	* this asked the nearest one only, and an emptied pile hid the full one behind it. */
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
				player->getRelationship(supplySource->getTeam()) != ENEMIES) return TRUE;
	}
	return FALSE;
}

static const TransportContainModuleData *transportContainOf( const ThingTemplate *tmpl );
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

	/* No builder here to pick the side's own building, so pick it from the side.  Turkey's build list
		 names AmericaCommandCenter (the skirmish scripts ask for it by that name), and the command
		 center adjustBuildList puts up in place of the starting one came out American, with American
		 dozers that built an American base. */
	if (bldgPlan->getDefaultOwningSide() != m_player->getSide())
	{
		for (const ThingTemplate *t = TheThingFactory->firstTemplate(); t; t = t->friend_getNextTemplate())
		{
			if (t->getDefaultOwningSide() == m_player->getSide() && t->isEquivalentTo(bldgPlan))
			{
				bldgPlan = t;
				break;
			}
		}
	}

	// inst-construct the building
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
static const Int BUILD_PROBES_PER_FRAME = 32;			///< isLocationLegalToBuild calls one frame may spend
static const Int BUILD_EXPANSIONS_PER_FRAME = 512;	///< cells one frame may take off the flood's queue

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
// ------------------------------------------------------------------------------------------------
Object *AIPlayer::buildStructureWithDozer(const ThingTemplate *bldgPlan, BuildListInfo *info)
{
	// Find a dozer.
	Object *dozer = findDozer(info->getLocation());
	if (dozer==NULL && getSkillProfile()->m_economyBuildings && isPowerThin() &&
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
	// Check available funds.
	Money *money = m_player->getMoney();
	if (money->countMoney()<bldgPlan->calcCostToBuild(m_player)) {
		return NULL;
	}

	/* One building placement per logic frame, across every computer player.
		 Placing a building is the expensive half of base building: when the chosen spot is taken,
		 isLocationLegalToBuild is called over a ring of candidate positions reaching 120 pathfind
		 cells for a skirmish AI, and finding a spot out at the edge of that costs 10ms - measured.
		 One of those is a frame nobody notices; the slow frames in an eight-player match were two
		 landing together, because a completed building shortcuts the timer that was meant to keep
		 the players apart.

		 The loser waits exactly one frame - a thirtieth of a second on a building that takes half a
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
			const Int searchRadius = isSkirmishAI() ? SKIRMISH_BUILD_SEARCH_CELLS : BUILD_SEARCH_CELLS;
			const Int searchWidth = 2*searchRadius + 1;
			const Int probeStride = isSkirmishAI() ? 2 : 1;		// the rings tried every other cell for a skirmish AI too
			// A home building the skirmish AI asked for stays within the base's edge and away from an
			// enemy.  The flood only tested that the building fits.
			const Bool home = isSkirmishAI() && info->isPriorityBuild() && m_baseCenterSet && staysHome(bldgPlan);
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
						else if (order->m_isScout && unit->isKindOf(KINDOF_AIRCRAFT) && unit->isKindOf(KINDOF_HARVESTER) &&
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
						else if (order->m_isScout && unit->isKindOf(KINDOF_AIRCRAFT) && unit->isKindOf(KINDOF_HARVESTER) &&
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
				if (order->m_isScout && isHelicopter(unit))
					takeDutyHelix(unit);
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

//----------------------------------------------------------------------------------------------------------
/** What a superweapon aimed at this spot is worth to this player right now.
 *
 * The score itself is EA's - the price of everything inside the blast - and the fade on top of it
 * is what stops the salvo. A negative score belongs to a sneak attack, which reads defended ground
 * as a cost rather than a prize; fading that would flatter exactly the ground it is avoiding.
 */
//----------------------------------------------------------------------------------------------------------
Int AIPlayer::superweaponScore( Coord3D *center, Int playerNdx, Real radius, Bool targetMilitaryUnits )
{
	const Int value = getPlayerSuperweaponValue( center, playerNdx, radius, targetMilitaryUnits, m_player->getPlayerIndex() );
	if( value <= 0 )
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
	// ponytail: a value pass per armed unit, which is only paid on a frame a superweapon is ready.
	//
	Player *enemy = ThePlayerList->getNthPlayer( playerNdx );
	if( targetMilitaryUnits && enemy != NULL )
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
		DEBUG_LOG(("AI SUPERWEAPON frame %d player %d aims '%s' at (%.0f,%.0f), worth %d\n", TheGameLogic->getFrame(),
			m_player->getPlayerIndex(), power->getName().str(), veryBestPos.x, veryBestPos.y, cash));
		noteSuperweaponAim( &veryBestPos );
	}


  return success;


}

/** How much an armed target is worth on top of its price, so the shot goes where the threat is. */
const Real SUPERWEAPON_THREAT_WEIGHT = 2.0f;

//----------------------------------------------------------------------------------------------------------
/**
 * Get the target value for structures in an area.
 */
Int AIPlayer::getPlayerSuperweaponValue(Coord3D *center, Int playerNdx, Real radius, Bool includeMilitaryUnits, Int observerNdx )
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
				Coord3D pos = *pObj->getPosition();
				Real dx = center->x - pos.x;
				Real dy = center->y - pos.y;
				if (dx*dx+dy*dy<radSqr) 
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
					// What a superweapon is for. Price alone ranks a supply stash above the tank
					// column parked beside it, which is why these shots landed in the money and not
					// in the army. What can shoot is worth several times its price to be rid of:
					// aiCombatPower reads ThingTemplate's ThreatValue where the data sets it and
					// falls back on cost, the same weighing the retreat and the counter score use.
					//
					if( includeMilitaryUnits )
						value += aiCombatPower( pObj ) * SUPERWEAPON_THREAT_WEIGHT;

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
		if (pu && pu->queueCreateUnit( order->m_thing, pu->requestUniqueUnitID() )) {
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
/** Return true if team can be considered for building */
// ------------------------------------------------------------------------------------------------
Bool AIPlayer::isPossibleToBuildTeam( TeamPrototype *proto, Bool requireIdleFactory, Bool &notEnoughMoney)
{
	/* Make sure we have at least one idle factory, and factories for all unit types. */
	Bool anyIdle = false;
	Int cost=0;
	notEnoughMoney = false;
	for( int i=0; i<proto->getTemplateInfo()->m_numUnitsInfo; i++ )
	{
		const TCreateUnitsInfo *unitInfo = &proto->getTemplateInfo()->m_unitsInfo[0];
		const ThingTemplate *thing = TheThingFactory->findTemplate( unitInfo[i].unitThingName );
		if (thing) {
			Int thingCost = thing->calcCostToBuild(m_player);
			if (NULL == findFactory(thing, true)) {
				// Couldn't find a factory.
				return false;
			}
			if (NULL != findFactory(thing, false)) {
				// Found an idle factory.
				anyIdle = true;
			}
			cost += thingCost * ((unitInfo[i].maxUnits+unitInfo[i].minUnits)/2.0f);
		}
	}
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

	if (hasEnoughMoneyUnitsFor(proto)) {
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
/** See if any existing teams need reinforcements, and have higher priority. */
// ------------------------------------------------------------------------------------------------
Bool AIPlayer::selectTeamToReinforce( Int minPriority )
{
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

//-------------------------------------------------------------------------------------------------
/** What this AI can see of what its enemies are fielding, weighted by threat value so a tank
	* counts for more than a rifleman.  Fog-aware by construction - it asks observerKnowsAbout of
	* every object, which is what makes B1 A2's first customer. */
//-------------------------------------------------------------------------------------------------
void AIPlayer::computeEnemyComposition( AIEnemyComposition *out, std::vector<AIVisibleEnemy> *army )
{
	Real air = 0.0f, armour = 0.0f, infantry = 0.0f, stealth = 0.0f, total = 0.0f;
	const Int me = m_player->getPlayerIndex();

	for( Int i = 0; i < ThePlayerList->getPlayerCount(); ++i )
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
	if (profile->m_counterCompositionWeight > 0.0f)
		computeEnemyComposition( &enemy, &army );

	// what a perfect counter, and the role preference, are worth in units of production priority
	const Real COUNTER_SPAN = 20.0f;
	const Real ROLE_SPAN = 10.0f;

	Player::PlayerTeamList candidateList;
	Int count = 0;
	Real bestScore = 0.0f;
	Bool haveBest = FALSE;
	for (t = candidateList1.begin(); t != candidateList1.end(); ++t)
	{
		const TeamTemplateInfo *info = (*t)->getTemplateInfo();
		const Bool isDefenceTeam = info->m_isBaseDefense || info->m_isPerimeterDefense;

		Real score = INT_TO_REAL( info->m_productionPriority );
		score += profile->m_counterCompositionWeight * COUNTER_SPAN *
						 aiCounterScore( enemy, teamCapability( *t, m_player, army ) );
		if( (m_role == AIROLE_DEFENSIVE) == (isDefenceTeam != FALSE) )
			score += ROLE_SPAN;

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
			const CommandSet *commandSet = TheControlBar->findCommandSet( factory->getCommandSetString() );
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
			if (pu->queueUpgrade(curUpgrade)) {
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
		// Hard China stood nine Gattling cannons shoulder to shoulder at one pile.
		if (bestSupplyWarehouse && countOwnedNear(m_player, tTemplate, bestSupplyWarehouse->getPosition(),
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
	Object *obj;
	Coord3D enemyCenter;
	enemyCenter.zero();
	Region2D bounds;
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
			// without the test the AI expands to docks in shroud it has never been near.
			if (!observerKnowsAbout(obj, m_player->getPlayerIndex())) continue;
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
				// and factories, against theirs.
				if (otherSideStructureNear(m_player, obj)) {
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
			}
		}
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
			if (thing)
			{
				int count = unitInfo[i].maxUnits-unitInfo[i].minUnits;
				if (count>0) {
					WorkOrder *order = newInstance(WorkOrder);
					order->m_thing = thing;
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
			if (thing)
			{
				int count = unitInfo[i].minUnits;
				WorkOrder *order = newInstance(WorkOrder);
				order->m_thing = thing;
				order->m_factoryID = INVALID_ID;
				order->m_numRequired = count;
				order->m_required = true;
				// prepend to head of list
				order->m_next = orders;
				orders = order;
			}
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
															 getSkillProfile()->m_cashHoardThreshold );
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
															 getSkillProfile()->m_cashHoardThreshold );
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
					if (obj&&obj->getAIUpdateInterface()) {
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
	const AISideInfo *sideInfo = TheAI->getAiData()->m_sideInfo;
	while (sideInfo) {
		if (sideInfo->m_side == m_player->getSide()) {
			break;
		}
		sideInfo = sideInfo->m_next;
	}
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
		if (isSkirmishAI()) {
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
		const Bool saves = getSkillProfile()->m_savesSciencePoints;
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
	AI_PHASE( AIP_SCOUT,   doScouting() );					// Keep one unit looking at the map.
	AI_PHASE( AIP_RETREAT, doRetreats() );					// Break off the fights we are losing.
	AI_PHASE( AIP_EXPAND,  doExpansion() );					// Go and take the money that is lying around.
	AI_PHASE( AIP_CAPTURE, doCapture() );						// ... and the money that is standing around.
	AI_PHASE( AIP_CAPTURE, doHijack() );						// Take the enemy's tanks rather than shoot them.
	AI_PHASE( AIP_ECONOMY, doEconomy() );						// ... and the money sitting in the bank.
	AI_PHASE( AIP_POWER,   doPower() );							// Keep the lights on, and out of reach.
	AI_PHASE( AIP_ECONOMY, doSuperweapons() );			// The big guns, as soon as they can be bought.
	AI_PHASE( AIP_WAVE,    doWaves() );							// Send the parked attack teams out together.
	AI_PHASE( AIP_TACTICS, doTactics() );						// Fight each unit from where it is strongest.
	AI_PHASE( AIP_TACTICS, doTransports() );				// Put the helicopters' riders down at the fight.
	AI_PHASE( AIP_TACTICS, doShuttles() );					// Fly the wave's ground units up its road.
	AI_PHASE( AIP_TACTICS, doHelixes() );					// Helix upgrades by the enemy army, healers, bomb raids.

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
	for( const AISideInfo *resInfo = TheAI->getAiData()->m_sideInfo; resInfo; resInfo = resInfo->m_next )
	{
		if( resInfo->m_side != m_player->getSide() )
			continue;
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

/** Tries on each ring when looking for somewhere to put a purchase down.  Every try is a legality
	* check that costs about a millisecond, so a call still only walks two rings: that is the spike,
	* not the reach of the search.  A search allowed to walk outward remembers the ring it reached
	* and starts the next call there, so a full opening base does not end the search. */
static const Int PLACEMENT_ANGLES = 12;
static const Int PLACEMENT_RINGS = 2;

/** Rings a call tries for a building that stays home (staysHome).  It has no outward walk to carry
	* on with next time, and a spot past the base's edge costs no legality check. */
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

/** Something of this kind the builder can make right now, off the builder's own buttons, so the
	* answer is right for every faction and general without a table of names. */
static const ThingTemplate *buildableOfKind( Object *builder, GUICommandType commandType, KindOfType kind )
{
	const CommandSet *commandSet = TheControlBar->findCommandSet( builder->getCommandSetString() );
	if( commandSet == NULL )
		return NULL;

	for( Int i = 0; i < MAX_COMMANDS_PER_SET; ++i )
	{
		const CommandButton *button = commandSet->getCommandButton( i );
		if( button == NULL || button->getCommandType() != commandType )
			continue;
		const ThingTemplate *tmpl = button->getThingTemplate();
		if( tmpl && tmpl->isKindOf( kind ) && TheBuildAssistant->canMakeUnit( builder, tmpl ) == CANMAKE_OK )
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
	const CommandSet *commandSet = TheControlBar->findCommandSet( dozer->getCommandSetString() );
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

/** Rings round the base center a defence may go on, in base radii, and spots per ring. */
static const Int DEFENSE_RINGS = 4;
static const Real DEFENSE_RING_RADII[ DEFENSE_RINGS ] = { 0.75f, 1.0f, 1.25f, 1.5f };
static const Int DEFENSE_SPOTS_PER_RING = 24;
/** Ground looked at round each spot: this many directions, at half and nine tenths of the reach. */
static const Int DEFENSE_FIRE_SAMPLES = 16;
/** Best-scored spots asked for legality, each about a millisecond. */
static const Int DEFENSE_PLACE_TRIES = 12;

//----------------------------------------------------------------------------------------------------------
/** A defence goes where its guns see the most of the ground an enemy comes over.  A spot on a ring
	* round the base is scored by the open ground inside its reach: each sample point on clear,
	* drivable ground counts for how much nearer the enemy it lies than the middle of the base does
	* (aiFireSampleWeight), plus a little for being outside the base at all, and counts nothing when a building of ours or a cliff stands
	* between it and the spot.  Every defence already standing or queued within three quarters of the
	* reach divides the score, so the guns spread along the front instead of stacking.  The enemy is
	* where enemyStartGuess says, the same knowledge every other decision here uses.  The spot that went
	* up in front of a fixed point used to land behind the base's own buildings on a crowded map, and a
	* missed spot fell back to the middle of the base; one with no score is not built at all now. */
//----------------------------------------------------------------------------------------------------------
Bool AIPlayer::placeDefense( const ThingTemplate *defense )
{
	if( !m_baseCenterSet )
		return FALSE;

	Coord3D threats[ MAX_PLAYER_COUNT ];
	Int threatCount = 0;
	for( Int i = 0; i < ThePlayerList->getPlayerCount() && threatCount < MAX_PLAYER_COUNT; ++i )
	{
		Player *them = ThePlayerList->getNthPlayer( i );
		if( them == m_player || m_player->getRelationship( them->getDefaultTeam() ) != ENEMIES || !them->hasAnyObjects() )
			continue;
		if( enemyStartGuess( i, &threats[ threatCount ] ) )
			++threatCount;
	}
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

	std::vector<Coord3D> guns;
	m_player->iterateObjects( collectDefensePositions, &guns );
	for( BuildListInfo *info = m_player->getBuildList(); info; info = info->getNext() )
	{
		const ThingTemplate *planned = info->isPriorityBuild() && info->getObjectID() == INVALID_ID
			? TheThingFactory->findTemplate( info->getTemplateName(), FALSE ) : NULL;
		if( planned && planned->isKindOf( KINDOF_FS_BASE_DEFENSE ) )
			guns.push_back( *info->getLocation() );
	}

	const Real reach = defenseReach( defense );
	const Real spacingSqr = sqr( 0.75f * reach );
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
		const Real radius = DEFENSE_RING_RADII[ s / DEFENSE_SPOTS_PER_RING ] * m_baseRadius;
		spot[ s ].x = m_baseCenter.x + radius * Cos( angle );
		spot[ s ].y = m_baseCenter.y + radius * Sin( angle );
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
			Bool open = TRUE;
			for( Int corner = 0; corner < 9 && open; ++corner )
			{
				Coord3D at = spot[ s ];
				at.x += footprint * ( corner % 3 - 1 );
				at.y += footprint * ( corner / 3 - 1 );
				ICoord2D cell;
				TheAI->pathfinder()->worldToCell( &at, &cell );
				const PathfindCell *pathCell = TheAI->pathfinder()->getCell( LAYER_GROUND, cell.x, cell.y );
				open = pathCell != NULL && pathCell->getType() == PathfindCell::CELL_CLEAR;
			}
			if( !open )
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
			const Real weight = aiFireSampleWeight( baseToThreat, sampleToThreat, reach,
				sqr( at.x - m_baseCenter.x ) + sqr( at.y - m_baseCenter.y ) > sqr( m_baseRadius ) );
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
			m_player->addToPriorityBuildList( defense->getName(), &spot[ s ], placeAngle );
			return TRUE;
		}
	}
	DEBUG_LOG(("AI DEFENSE frame %d player %d has no spot for '%s', best score %.2f, fire clear %d blocked %d, %d illegal (last code %d)\n",
		TheGameLogic->getFrame(), m_player->getPlayerIndex(), defense->getName().str(), score[ order[ 0 ] ], clear[ order[ 0 ] ], blocked[ order[ 0 ] ],
		illegal, (Int)lastCode));
	return FALSE;
}

/** How loaded this player's factories of one kind are.  Counted off the objects, because a factory
	* bought past the build list is still a factory. */
struct FactoryLoad
{
	KindOfType kind;
	Int finished;
	Int constructing;
	Int idle;
	Int deepest;
};

static void readFactoryLoad( Object *obj, void *userData )
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
	++load->finished;
	const Int queued = (Int)pu->getProductionCount();
	if( queued == 0 )
		++load->idle;
	if( queued > load->deepest )
		load->deepest = queued;
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

	const ThingTemplate *tmpl = buildableOfKind( dozer, GUI_COMMAND_DOZER_CONSTRUCT, KINDOF_FS_POWER );
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
	if( !profile->m_economyBuildings )
		return;
	const Int phase = computeUpdatePhase( m_player->getPlayerIndex(), ECONOMY_CHECK_RATE );
	if( (TheGameLogic->getFrame() + phase) % ECONOMY_CHECK_RATE != 0 )
		return;

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
	Bool homeFull = FALSE;		// an income building was wanted and the base had no room for it
	for( Int i = 0; i < INCOME_KINDS; ++i )
	{
		const ThingTemplate *tmpl = buildableOfKind( dozer, GUI_COMMAND_DOZER_CONSTRUCT, INCOME[ i ] );
		// from half way out to the edge: placeNear keeps these inside the base (staysHome)
		if( tmpl && !priorityBuildPending( m_player, tmpl ) && !placeNear( tmpl, &m_baseCenter, 0.5f * m_baseRadius, FALSE ) )
			homeFull = TRUE;
	}

	/* A gun on the side the trouble comes from, before the factories the pass buys.  Production and
		 income go round the base center, the power goes behind it, and this goes out in front, so the
		 defenses stand in front of everything else.  It used to be one every minute and a half and never
		 more than ten.  Bought whenever the bank allowed, they took the money from the army: over twelve
		 four-player matches 23 guns became 241, and attack waves fell from 223 to 187.  So the guns grow
		 with the army, and with the clock up to two superweapons' worth (aiDefenseAllowance), one at a
		 time, each the type the base has fewest of, each on the spot with the widest field of fire
		 (placeDefense).  They wait only for the hoard, like the income buildings: behind twice the
		 hoard, a 1v1 Hard side built two to five a match.  A base three or more behind its allowance
		 queues two a pass, of different types, since one a pass left most of the allowance unspent. */
	const BaseTally tally = tallyBase( m_player );
	const Int allowance = aiDefenseAllowance( tally.army, TheGameLogic->getFrame() );
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

	// ... and what does not pay back for twice it, so the opening build order is not what pays for it
	if( m_player->getMoney()->countMoney() <= 2 * profile->m_cashHoardThreshold )
		return;

	/* Airfields have no count, so a tank queue that never empties would take every pass and the
		 airfield would wait forever.  Even economy ticks ask for the airfield first, odd ticks ask for
		 the queues first, and a kind that wants nothing simply yields the pass. */
	const KindOfType QUEUED_KINDS[ 2 ] = { KINDOF_FS_WARFACTORY, KINDOF_FS_BARRACKS };
	const Bool airFirst = ((TheGameLogic->getFrame() + phase) / ECONOMY_CHECK_RATE) % 2 == 0;
	for( Int pass = 0; pass < 2; ++pass )
	{
		const Bool airPass = (pass == 0) == airFirst;
		if( airPass )
		{
			if( queueExtraFactory( dozer, KINDOF_FS_AIRFIELD, TRUE ) )
				return;
		}
		else
		{
			// The income building the base had no room for used to go up out on the map, and that was
			// where a GLA's money went: it has no airfield to spend on, and one banked 136,939 by the
			// end of a match.  With home full the money goes into production, a factory a pass whether
			// or not its queue is backing up, the way the airfields always have.
			for( Int i = 0; i < 2; ++i )
				if( queueExtraFactory( dozer, QUEUED_KINDS[ i ], homeFull ) )
					return;
		}
	}
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
	const CommandSet *commandSet = TheControlBar->findCommandSet( builder->getCommandSetString() );
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
	* is in the bank and DEFENSES_PER_SUPERWEAPON guns stand, the first one goes up. More follow as the
	* base grows, one per that many guns, as the guns grow with the army; unrationed, they took the money
	* the army needed. That ration binds computer players only (Player::canBuildMoreOfType), and the
	* lobby's superweapon setting and Pro Rules still bind beside it; canMakeUnit asks all three.
	* If the tech building it needs is missing, that goes up first, and copies follow once the first
	* superweapon is standing, up to AI_TECH_BUILDING_COPIES, so one of them blowing up leaves the tree
	* standing. Hard only, like the rest of the economy. */
//----------------------------------------------------------------------------------------------------------
void AIPlayer::doSuperweapons( void )
{
	if( !getSkillProfile()->m_economyBuildings )
		return;
	const Int phase = computeUpdatePhase( m_player->getPlayerIndex(), SUPERWEAPON_CHECK_RATE );
	if( (TheGameLogic->getFrame() + phase) % SUPERWEAPON_CHECK_RATE != 0 )
		return;
	if( !m_player->getCanBuildBase() || !m_baseCenterSet || !m_player->getEnergy()->hasSufficientPower() )
		return;		// a dark base's superweapon clock is stopped anyway; the money goes to the plant

	Object *dozer = NULL;
	m_player->iterateObjects( findAnyDozer, &dozer );
	if( dozer == NULL )
		return;

	const ThingTemplate *superweapon = buildableOfKind( dozer, GUI_COMMAND_DOZER_CONSTRUCT, KINDOF_FS_SUPERWEAPON );
	const ThingTemplate *techBuilding = buildableOfKind( dozer, GUI_COMMAND_DOZER_CONSTRUCT, KINDOF_FS_ADVANCED_TECH );

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
		if( moneyUnit && pu->queueCreateUnit( moneyUnit, pu->requestUniqueUnitID() ) )
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
	* center, or the first crowded dock skips the base itself. */
//----------------------------------------------------------------------------------------------------------
Bool AIPlayer::queueExtraFactory( Object *dozer, KindOfType kind, Bool unlimited )
{
	const ThingTemplate *tmpl = buildableOfKind( dozer, GUI_COMMAND_DOZER_CONSTRUCT, kind );
	if( tmpl == NULL || priorityBuildPending( m_player, tmpl ) )
		return FALSE;

	FactoryLoad load;
	load.kind = kind;
	load.finished = 0;
	load.constructing = 0;
	load.idle = 0;
	load.deepest = 0;
	m_player->iterateObjects( readFactoryLoad, &load );
	if( !aiWantsAnotherFactory( load.finished, load.constructing, load.idle, load.deepest, unlimited ) )
		return FALSE;

	const Object *warehouse = TheGameLogic->findObjectByID( m_curWarehouseID );
	if( warehouse && isHeldExpansion( warehouse ) &&
			placeNear( tmpl, warehouse->getPosition(),
				warehouse->getGeometryInfo().getBoundingCircleRadius() + SUPPLY_CENTER_CLOSE_DIST * 0.5f, FALSE ) )
		return TRUE;
	return placeNear( tmpl, &m_baseCenter, m_baseRadius, TRUE );
}

//----------------------------------------------------------------------------------------------------------
/** Walk rings round center until a spot is legal and safe, and hand it to the dozers as a priority
	* build.  A call still tries two rings, two dozen legality checks.  walkOutward carries the first
	* ring in m_placementRing, so the search moves out across calls instead of giving up at the opening
	* base, and wraps back to the middle once that ring is past the pathfind extent. */
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

	Int startRing = 0;
	if( walkOutward )
	{
		Region3D extent;
		TheTerrainLogic->getMaximumPathfindExtent( &extent );
		Real farthest = 0.0f;
		const Real cornerX[ 2 ] = { extent.lo.x, extent.hi.x };
		const Real cornerY[ 2 ] = { extent.lo.y, extent.hi.y };
		for( Int cx = 0; cx < 2; ++cx )
		{
			for( Int cy = 0; cy < 2; ++cy )
			{
				const Real dx = cornerX[ cx ] - center->x;
				const Real dy = cornerY[ cy ] - center->y;
				const Real dist = sqrt( dx * dx + dy * dy );
				if( dist > farthest )
					farthest = dist;
			}
		}
		const Real reach = innerRadius + structureRadius * (2 * m_placementRing + 1);
		if( m_placementRing < 0 || reach > farthest )
			m_placementRing = 0;
		startRing = m_placementRing;
	}

	for( Int ring = startRing; ring < startRing + rings; ++ring )
	{
		const Real distance = innerRadius + structureRadius * (2 * ring + 1);
		for( Int step = 0; step < PLACEMENT_ANGLES; ++step )
		{
			const Real angle = 2.0f * PI * step / PLACEMENT_ANGLES;
			Coord3D pos = *center;
			pos.x += distance * Cos( angle );
			pos.y += distance * Sin( angle );
			pos.z = 0;
			if( home && sqr( pos.x - m_baseCenter.x ) + sqr( pos.y - m_baseCenter.y ) > homeReachSqr )
				continue;

			const Bool legal = TheBuildAssistant->isLocationLegalToBuild( &pos, tmpl, placeAngle,
												BuildAssistant::CLEAR_PATH | BuildAssistant::TERRAIN_RESTRICTIONS | BuildAssistant::NO_OBJECT_OVERLAP,
												NULL, m_player ) == LBC_OK;
			TheTerrainVisual->removeAllBibs();	// isLocationLegalToBuild adds bib feedback
			if( legal && isLocationSafe( &pos, tmpl ) )
			{
				if( walkOutward )
					m_placementRing = ring;
				DEBUG_LOG(("AI ECONOMY frame %d player %d builds '%s' at (%.0f,%.0f), %d in the bank, %.0f from a base of radius %.0f\n", TheGameLogic->getFrame(),
					m_player->getPlayerIndex(), tmpl->getName().str(), pos.x, pos.y, m_player->getMoney()->countMoney(),
					sqrt( sqr( pos.x - m_baseCenter.x ) + sqr( pos.y - m_baseCenter.y ) ), m_baseRadius));
				m_player->addToPriorityBuildList( tmpl->getName(), &pos, placeAngle );
				return TRUE;
			}
		}
	}
	if( walkOutward )
		m_placementRing = startRing + PLACEMENT_RINGS;
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
	if( !getSkillProfile()->m_useInfluenceMapForAttackLane || from == NULL )
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
	if( !getSkillProfile()->m_useInfluenceMapForAttackLane )
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
		if( next && m_influence.isBuilt() )
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

//----------------------------------------------------------------------------------------------------------
/** What this AI has seen that can shoot, within the approach watch radius of a point. */
//----------------------------------------------------------------------------------------------------------
Real AIPlayer::knownFirepowerNear( const Coord3D *pos )
{
	// the influence map answers the question the radius only approximated: not what stands near the
	// road, but what can shoot onto it, the high ground's reach included.  The radius count below also
	// took in this player's own units, which the affiliation filter lets through whatever it is asked.
	if( m_influence.isBuilt() )
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
static const ThingTemplate *bestDefender( Object *factory, const Player *owner, const AIVisibleEnemy &enemy )
{
	const CommandSet *commandSet = TheControlBar->findCommandSet( factory->getCommandSetString() );
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
		if( tmpl == NULL || !tmpl->canPossiblyHaveAnyWeapon() || tmpl->isKindOf( KINDOF_DOZER ) || tmpl->isKindOf( KINDOF_HARVESTER ) ||
				tmpl->isKindOf( KINDOF_MONEY_HACKER ) || tmpl->isKindOf( KINDOF_AIRCRAFT ) ||
				TheBuildAssistant->canMakeUnit( factory, tmpl ) != CANMAKE_OK )
			continue;

		const Int cost = tmpl->calcCostToBuild( owner );
		const Real score = aiMatchupScore( templateFramesToKill( tmpl, enemy.m_template ),
			templateFramesToKill( enemy.m_template, tmpl ), INT_TO_REAL( cost ), enemy.m_cost );
		if( best == NULL || score > bestScore || (score == bestScore && cost > bestCost) )
		{
			best = tmpl;
			bestScore = score;
			bestCost = cost;
		}
	}
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
	if( !isSkirmishAI() )
		return;
	Int intruders = 0;
	std::vector<AIVisibleEnemy> army;
	Object *intruder = homeIntruder( &intruders, &army );
	if( intruder == NULL )
		return;

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
			const ThingTemplate *unit = bestDefender( *f, m_player, army[ kind ] );
			if( unit == NULL || !pu->queueCreateUnit( unit, pu->requestUniqueUnitID() ) )
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
	if( dozer && m_player->getCanBuildBase() )
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
	for( std::vector<Object *>::const_iterator o = owned.begin(); o != owned.end(); ++o )
	{
		if( !(*o)->isEffectivelyDead() && !(*o)->isKindOf( KINDOF_STRUCTURE ) && isAtHome( (*o)->getPosition() ) && leavesToFinish( *o ) )
			homePower += aiCombatPower( *o );
	}
	Real intruderWorth = 0.0f;
	for( std::vector<AIVisibleEnemy>::const_iterator k = army.begin(); k != army.end(); ++k )
		intruderWorth += k->m_weight;
	if( homePower < 0.5f * intruderWorth )
		return;

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
		DEBUG_LOG(("AI DEFEND frame %d player %d sends %d idle units at the intruder at (%.0f,%.0f)\n", TheGameLogic->getFrame(),
			m_player->getPlayerIndex(), sent, intruder->getPosition()->x, intruder->getPosition()->y));
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
		if( obj->isEffectivelyDead() || obj->getAI() == NULL || isOutOnOrders( obj ) )
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
Bool AIPlayer::holdTeamForWave( Team *team, const AsciiString &approach, Int pathSuffix )
{
	// a base being hit keeps what its scripts would send out, on every rung, until the fight at home is over
	if( team == NULL || !isSkirmishAI() )
		return FALSE;
	const Bool underAttack = isBaseUnderAttack();
	if( !underAttack && !holdsTeamsForWaves() )
		return FALSE;
	const TeamTemplateInfo *info = team->getPrototype()->getTemplateInfo();
	if( info->m_isBaseDefense || info->m_isPerimeterDefense )
		return FALSE;

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
	if( freeSlot < 0 )
		return FALSE;		// the staging point is full, so this one goes as the script said
	if( !underAttack && aiReleaseWaveAt( m_pressure, waitingPower( team, NULL ), WAVE_POWER, 0, WAVE_MAX_HOLD_FRAMES ) )
		return FALSE;		// a wave on its own waits for nobody

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
	AIGroup *group = TheAI->createGroup();
	waitingPower( team, group );
	if( group->isEmpty() )
		TheAI->destroyGroup( group );
	else
		group->groupTightenToPosition( &staging, FALSE, CMD_FROM_AI );
	return TRUE;
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
	const AIDifficultyProfile *profile = getSkillProfile();
	if( profile->m_massBeforeAttacking )
		return m_pressure <= AIPRESSURE_NORMAL;
	return m_pressure == AIPRESSURE_DEFEND && profile->m_retreatTtkRatio > 0.0f;
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
	return id != m_capturerID && id != m_ferryID && id != m_hijackerID && !isDutyHelix( obj );
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
	* belief falling a tenth every two seconds, while the GLA it was fighting stood at (460,1679). */
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

	const Real chance = aiWinChance( mine.power, m_knownEnemyPower );
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
	DEBUG_LOG(("AI PRESSURE frame %d player %d chance %.2f level %s, %.0f power against %.0f believed\n", TheGameLogic->getFrame(),
		me, chance, names[ level ], mine.power, m_knownEnemyPower));
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
	sendIdleAttackTeams();
	sendIdleUnitsHunting();

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
	// and a rung that does not mass lets go the moment the fight at home is over
	if( teams < MAX_HELD_TEAMS && holdsTeamsForWaves() && !aiReleaseWaveAt( m_pressure, power, WAVE_POWER, heldFrames, WAVE_MAX_HOLD_FRAMES ) )
		return;

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
	const AsciiString approach = chooseApproachLabel( &center, requested, pathSuffix );

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
		sendWave( flankGroup, flank, pathSuffix, teams, flankPower, heldFrames );
		power -= flankPower;
	}
	sendWave( wave, approach, pathSuffix, teams, power, heldFrames );
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
Bool AIPlayer::isGunshipRider( const Object *obj ) const
{
	return obj->isKindOf( KINDOF_INFANTRY ) && !obj->isKindOf( KINDOF_HERO ) && obj->isSelectable() && !obj->isContained() &&
		!obj->isEffectivelyDead() && obj->getAI() && leavesToFinish( obj );
}

/** An infantryman who rides out with the army, where loadGunships puts him. */
struct GunshipRider
{
	Object *obj;
	const char *from;		///< "wave", "default" or "team"; a "team" rider sits only with his own team
};

Bool AIPlayer::loadGunships( void )
{
	if( !isSkirmishAI() || !m_baseCenterSet )
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
			if( !fires && (!helicopter || !m_influence.isBuilt() || strcmp( r->from, "wave" ) != 0) )
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

/** The infantry a barracks trains to ride in a gunship: the first fighter on its buttons, so a Red
	* Guard, a Ranger, a Rebel or a Mini-Gunner.  Not a worker, a hacker or a hero.  Against an enemy
	* mostly on wheels and tracks, armour names the vehicle most of his strength is in, and the rider is whichever of them
	* kills it fastest: the Tank Hunter a bunkered Helix is bought for, a Missile Defender in a Humvee. */
static const ThingTemplate *gunshipRiderTemplate( Object *factory, const ThingTemplate *armour )
{
	const CommandSet *commandSet = TheControlBar->findCommandSet( factory->getCommandSetString() );
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
				tmpl->isKindOf( KINDOF_MONEY_HACKER ) || tmpl->isKindOf( KINDOF_HERO ) ||
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
	while( queued < wanted && pu->queueCreateUnit( rider, pu->requestUniqueUnitID() ) )
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
	const CommandSet *commandSet = TheControlBar->findCommandSet( factory->getCommandSetString() );
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
void AIPlayer::sendWave( AIGroup *wave, const AsciiString &approach, Int pathSuffix, Int teams, Real power, UnsignedInt heldFrames )
{
	Coord3D center;
	wave->getCenter( &center );
	AsciiString pathLabel;
	pathLabel.format( "%s%d", approach.str(), pathSuffix );
	Waypoint *way = TheTerrainLogic->getClosestWaypointOnPath( &center, pathLabel );
	DEBUG_LOG(("AI WAVE frame %d player %d sends %d teams, %d units, %.0f power, after %d s, down %s\n", TheGameLogic->getFrame(),
		m_player->getPlayerIndex(), teams, wave->getCount(), power, heldFrames / LOGICFRAMES_PER_SECOND, pathLabel.str()));
	if( way == NULL )
		return;

	computeShuttleFront( way, wave );
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
	* this way, and sendIdleUnitsHunting takes the guards. */
//----------------------------------------------------------------------------------------------------------
void AIPlayer::sendIdleAttackTeams( void )
{
	if( !isSkirmishAI() || (m_pressure != AIPRESSURE_FINISH && !getSkillProfile()->m_massBeforeAttacking) || !m_baseCenterSet )
		return;
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
			if( holdTeamForWave( team, center, pathSuffix ) )
				continue;
			AIGroup *group = TheAI->createGroup();
			const Real power = waitingPower( team, group );
			Coord3D from;
			group->getCenter( &from );
			sendWave( group, chooseApproachLabel( &from, center, pathSuffix ), pathSuffix, 1, power, 0 );
		}
	}
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
				!isAtHome( obj->getPosition() ) || !leavesToFinish( obj ) )
			continue;
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
	if( !isSkirmishAI() || !m_baseCenterSet || m_pressure == AIPRESSURE_DEFEND )
		return;
	const Bool finish = m_pressure == AIPRESSURE_FINISH;
	const Bool guardsGo = finish &&
		(TheGameLogic->getFrame() + computeUpdatePhase( m_player->getPlayerIndex(), WAVE_CHECK_RATE )) % GUARD_HUNT_RATE == 0;
	const Real reachSqr = sqr( 2.0f * m_baseRadius );
	Int hunters = 0;
	Int guards = 0;

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
				if( !attacks )
					++guards;
			}
		}
	}
	if( hunters > 0 )
		DEBUG_LOG(("AI WAVE frame %d player %d sends %d idle units hunting, %d of them off guard\n", TheGameLogic->getFrame(),
			m_player->getPlayerIndex(), hunters, guards));
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

	Object *entrance = m_player->getTunnelSystem()->findTunnelShortcut( center, end->getLocation(), walk );
	if( entrance == NULL )
		return;

	// a copy: a member that goes into the tunnel leaves the group
	Int sent = 0;
	const Int waveSize = wave->getCount();
	const VecObjectID members = wave->getAllIDs();
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
	* guns reach.  Without the map (below Hard) nothing is known, and every flight is quiet. */
Bool AIPlayer::flightIsQuiet( const Coord3D *from, Real x, Real y ) const
{
	if( !m_influence.isBuilt() )
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
			hot = walked + along > length * SHUTTLE_DROP_SHARE || (m_influence.isBuilt() && m_influence.enemyAt( x, y ) > 0.0f);
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
	if( !isSkirmishAI() || !m_baseCenterSet || m_skillLevel == AISKILL_EASY || measuringWithoutTactics() )
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
	const CommandSet *commandSet = TheControlBar->findCommandSet( tmpl->friend_getCommandSetString() );
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
	if( raiders < MAX_RAIDERS && helixUpgradeOn( tmpl, HELIX_BOMB ) )
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
		const CommandSet *commandSet = TheControlBar->findCommandSet( factories[ f ]->getCommandSetString() );
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
	if( pu->queueUpgrade( next ) )
		DEBUG_LOG(("AI HELIX frame %d player %d upgrades '%s' %d with '%s' as a %s, enemy known: %d infantry, %d vehicles, threat %.0f%% infantry %.0f%% ground vehicles\n",
			TheGameLogic->getFrame(), m_player->getPlayerIndex(), helix->getTemplate()->getName().str(), helix->getID(),
			next->getUpgradeName().str(), role < 0 ? "team gunship" : HELIX_ROLE_NAME[ role ], enemy->m_infantryCount, enemy->m_vehicleCount,
			enemy->m_infantry * 100.0f, (enemy->m_armour - enemy->m_air) * 100.0f));
}

Coord3D AIPlayer::rearOf( const Coord3D *from, const std::vector<AIKnownGun> &guns ) const
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
		if( deepestReach( guns, x, y, FALSE ) < -HELIX_GUN_MARGIN )
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
					deepestReach( guns, x, y, FALSE ) < -HELIX_GUN_MARGIN )
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
		spot = rearOf( pos, guns );
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
			spot = rearOf( &middle, guns );
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
				const Coord3D clear = rearOf( pos, guns );
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
	if( !isSkirmishAI() || !m_baseCenterSet || m_skillLevel == AISKILL_EASY || measuringWithoutTactics() )
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

			// where this team is
			Real count = 0.0f;
			Coord3D centre;
			centre.zero();
			for( DLINK_ITERATOR<Object> objIter = team->iterate_TeamMemberList(); !objIter.done(); objIter.advance() )
			{
				Object *obj = objIter.cur();
				if( obj == NULL || obj->isEffectivelyDead() )
					continue;
				if( obj->isKindOf( KINDOF_PROJECTILE ) || !retreatCanOrderHome( obj ) )
					continue;			// only the units that could actually be pulled out place the fight
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
			// while sixteen rebels took the base apart untouched for a minute.
			if( isAtHome( &centre ) )
				continue;

			Real myHealth = 0.0f, myPower = 0.0f;
			Real enemyHealth = 0.0f, enemyPower = 0.0f;
			std::vector<Real> enemyGuns;
			measureFight( &centre, FALSE, &myHealth, &myPower, &enemyHealth, &enemyPower, &enemyGuns );

			if( enemyPower <= 0.0f )
				continue;			// not in a fight

			const Real ratio = aiRetreatRatio( myHealth, myPower, enemyHealth, enemyPower );
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
			const Bool shortOfHome = !measuringWithoutTactics() &&
				aiRetreatFallbackPoint( centre.x, centre.y, m_baseCenter.x, m_baseCenter.y, INFLUENCE_CELL_SIZE,
					enemyGuns.empty() ? NULL : &enemyGuns[ 0 ], (Int)enemyGuns.size() / 3, &fallback.x, &fallback.y );
			if( shortOfHome )
				fallback.z = TheTerrainLogic->getGroundHeight( fallback.x, fallback.y );
			const Real homeX = m_baseCenter.x - centre.x;
			const Real homeY = m_baseCenter.y - centre.y;
			Object *homeTunnel = shortOfHome ? NULL : m_player->getTunnelSystem()->findTunnelShortcut( &centre, &m_baseCenter,
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
Bool AIPlayer::isFallingBack( ObjectID unit )
{
	const TacticalStep *step = findTacticalStep( unit );
	return step != NULL && step->fallingBack;
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

void AIPlayer::measureFight( const Coord3D *centre, Bool countHolders, Real *myHealth, Real *myPower,
														 Real *enemyHealth, Real *enemyPower, std::vector<Real> *enemyGuns )
{
	PartitionFilterAlive filterAlive;
	PartitionFilterOnMap filterOnMap;
	PartitionFilter *filters[] = { &filterAlive, &filterOnMap, 0 };

	MemoryPoolObjectHolder hold;
	SimpleObjectIterator *nearby = ThePartitionManager->iterateObjectsInRange(
			centre, RETREAT_ENGAGEMENT_RADIUS, FROM_CENTER_2D, filters );
	hold.hold( nearby );
	for( Object *e = nearby->first(); e; e = nearby->next() )
	{
		if( e->isKindOf( KINDOF_PROJECTILE ) )
			continue;
		//
		// Only the field battle. Base defences shoot, but they are what an attack goes *through* -
		// counting them makes every approach to a defended base read as a lost fight, and the AI never
		// attacks anything again. Picking a way in past them is the influence map's job (B4), not the
		// retreat's.  Where it stops falling back is another matter: a tower's reach is no place to wait.
		//
		const Bool fixed = e->isKindOf( KINDOF_STRUCTURE ) || e->isKindOf( KINDOF_IMMOBILE );

		if( e->getControllingPlayer() == m_player )
		{
			// a holder counted below is not counted twice
			const TacticalStep *row = countHolders ? findTacticalStep( e->getID() ) : NULL;
			if( !fixed && (row == NULL || !heldFrom( row->fallingBack, &row->fallbackFrom, centre )) )
				addToForce( e, myHealth, myPower );
		}
		else if( m_player->getRelationship( e->getTeam() ) == ENEMIES )
		{
			// only what it can see: an AI that pulls back from something it has not found is reading
			// the object list again (A2)
			if( !observerKnowsAbout( e, m_player->getPlayerIndex() ) )
				continue;
			if( !fixed )
				addToForce( e, enemyHealth, enemyPower );
			const Real reach = enemyGuns ? groundAttackRange( e ) : 0.0f;
			if( reach > 0.0f )
			{
				enemyGuns->push_back( e->getPosition()->x );
				enemyGuns->push_back( e->getPosition()->y );
				enemyGuns->push_back( reach );
			}
		}
	}

	if( !countHolders )
		return;
	for( size_t i = 0; i < m_tactics.size(); ++i )
	{
		if( !heldFrom( m_tactics[ i ].fallingBack, &m_tactics[ i ].fallbackFrom, centre ) )
			continue;
		Object *obj = TheGameLogic->findObjectByID( m_tactics[ i ].unit );
		// and still ours: a holder taken by a Hijacker keeps its row until it expires
		if( obj && !obj->isEffectivelyDead() && obj->getControllingPlayer() == m_player && !obj->isKindOf( KINDOF_PROJECTILE ) &&
				!obj->isKindOf( KINDOF_STRUCTURE ) && !obj->isKindOf( KINDOF_IMMOBILE ) )
			addToForce( obj, myHealth, myPower );
	}
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

	Real myHealth = 0.0f, myPower = 0.0f;
	Real enemyHealth = 0.0f, enemyPower = 0.0f;
	measureFight( &from, TRUE, &myHealth, &myPower, &enemyHealth, &enemyPower, NULL );
	const Real ratio = aiRetreatRatio( myHealth, myPower, enemyHealth, enemyPower );
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
		const Bool givesUp = what == AIFALLBACK_GO_HOME && !fightOver;
		if( (!givesUp && obj->getAI()->isMoving()) || (what == AIFALLBACK_RESUME && aiCombatPower( obj ) <= 0.0f) )
			restoreMood( obj, step );
		else if( what == AIFALLBACK_RESUME )
		{
			restoreMood( obj, step );
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
/** How often the influence map is redrawn, and how often each fighting unit is looked at.  Both are on
	* the frame and the player's slot, so there is no timer to save and every machine does it on the
	* same frame. */
static const Int INFLUENCE_REBUILD_RATE = LOGICFRAMES_PER_SECOND;
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
	const Int me = m_player->getPlayerIndex();
	for( Object *obj = TheGameLogic->getFirstObject(); obj; obj = obj->getNextObject() )
	{
		if( obj->isEffectivelyDead() || obj->isKindOf( KINDOF_PROJECTILE ) )
			continue;
		const Bool mine = obj->getControllingPlayer() == m_player;
		const Relationship relation = m_player->getRelationship( obj->getTeam() );
		if( !mine && relation != ENEMIES && relation != ALLIES )
			continue;
		const Real power = aiCombatPower( obj );
		if( power <= 0.0f )
			continue;
		const Real range = groundAttackRange( obj );
		if( range <= 0.0f )
			continue;
		const Coord3D *pos = obj->getPosition();
		if( mine || relation == ALLIES )
			m_influence.stampFriend( pos->x, pos->y, firingHeight( obj ), range, power );
		else if( observerKnowsAbout( obj, me ) )
			m_influence.stampEnemy( pos->x, pos->y, firingHeight( obj ), range, power );
	}
	m_influence.markBuilt( TheGameLogic->getFrame() );
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

void AIPlayer::doTactics( void )
{
	if( measuringWithoutTactics() )
		return;
	const UnsignedInt now = TheGameLogic->getFrame();
	const Int me = m_player->getPlayerIndex();
	const Bool tactics = getSkillProfile()->m_tacticalMicro;

	if( tactics && (!m_influence.isBuilt() || (now + computeUpdatePhase( me, INFLUENCE_REBUILD_RATE )) % INFLUENCE_REBUILD_RATE == 0) )
		rebuildInfluence();

	if( (now + computeUpdatePhase( me, TACTICS_RATE )) % TACTICS_RATE != 0 )
		return;

	if( tactics && m_baseCenterSet )
	{
		std::vector<Object *> units;
		m_player->iterateObjects( collectObject, &units );
		for( size_t i = 0; i < units.size(); ++i )
			tacticsFor( units[ i ] );
	}

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
		const Bool done = (unit == NULL || unit->isEffectivelyDead()) ? TRUE :
			row.fallingBack ? now - row.fallbackFrame > FALLBACK_STALE_FRAMES :
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
	if( !m_influence.isBuilt() )
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
void AIPlayer::tacticsFor( Object *obj )
{
	// armed, not "able to attack" this frame: that reads false while a clip reloads, which is exactly
	// when a Rocket Buggy has to step away, and it took every buggy out of here the moment it had fired
	if( obj->isEffectivelyDead() || obj->isContained() || groundAttackRange( obj ) <= 0.0f )
		return;
	if( obj->isKindOf( KINDOF_STRUCTURE ) || obj->isKindOf( KINDOF_IMMOBILE ) || obj->isKindOf( KINDOF_AIRCRAFT ) ||
			obj->isKindOf( KINDOF_DOZER ) || obj->isKindOf( KINDOF_HARVESTER ) || obj->isKindOf( KINDOF_PROJECTILE ) )
		return;
	AIUpdateInterface *ai = obj->getAI();
	if( ai == NULL || ai->hasTunnelTrip() || ai->getCurLocomotor() == NULL )
		return;		// a gun that cannot move (a Helix's gattling mount) has no step to take
	// base defence stays where it was put, and the scouts, the capturer and the hijacker have their jobs
	Team *team = obj->getTeam();
	if( team == NULL || obj->isKindOf( KINDOF_MONEY_HACKER ) )
		return;
	const TeamTemplateInfo *info = team->getPrototype()->getTemplateInfo();
	if( info && (info->m_isBaseDefense || info->m_isPerimeterDefense) )
		return;
	if( obj->getID() == m_capturerID || obj->getID() == m_hijackerID || isDutyHelix( obj ) )
		return;
	for( Int i = 0; i < MAX_AI_SCOUTS; ++i )
	{
		if( obj->getID() == m_scoutID[ i ] )
			return;
	}

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
		if( !player->canBuild( t ) )
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
		if( obj->getID() == m_capturerID || obj->getID() == m_hijackerID )
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
	if( TheControlBar == NULL )
		return NULL;

	const ThingTemplate *best = NULL;
	Int bestCost = 0;

	for( const ThingTemplate *t = TheThingFactory->firstTemplate(); t; t = t->friend_getNextTemplate() )
	{
		if( !t->isKindOf( KINDOF_INFANTRY ) || t->isKindOf( KINDOF_DOZER ) )
			continue;
		if( !player->canBuild( t ) )
			continue;

		const Int cost = t->calcCostToBuild( player );
		if( cost <= 0 || (best != NULL && cost >= bestCost) )
			continue;

		const CommandSet *set = TheControlBar->findCommandSet( t->friend_getCommandSetString() );
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

	Object *target = nearestTechBuilding( &from, FALSE );

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
		if( !t->isKindOf( KINDOF_INFANTRY ) || !couldBuildUnit( player, t ) )
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
	return TheAI->getDifficultyProfile( m_skillLevel );
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
	XferVersion currentVersion = 16;		// 2: scout  3: rung and role  4: scouting stamps  5: the capturer  6: parked waves  7: superweapon aims  8: the hijacker  9: tactical steps  10: the ferry and dropped riders  11: the outward placement ring  12: the pressure level  13: falling back  14: gunship Chinooks and the boarding wait  15: transport Chinooks and the lent one  16: healer and raider Helixes
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
	// the influence map as it stands, since the next rebuild can be most of a second off and the lanes,
	// the retreat and the steps all read it before then.
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

	// the ladder rung and the role, which are rolled once and must come back the same way
	if( version >= 3 )
	{
		xfer->xferUser( &m_skillLevel, sizeof( AISkillLevel ) );
		xfer->xferUser( &m_role, sizeof( AIRole ) );
	}

	xfer->xferUser( m_structuresToRepair, sizeof( ObjectID ) * MAX_STRUCTURES_TO_REPAIR );
	xfer->xferObjectID( &m_repairDozer );
	xfer->xferInt( &m_structuresInQueue );
	xfer->xferBool( &m_dozerQueuedForRepair );
	xfer->xferBool( &m_dozerIsRepairing );
	xfer->xferInt( &m_bridgeTimer );

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
