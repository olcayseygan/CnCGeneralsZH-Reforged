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

// AISkirmishPlayer.cpp
// Computerized opponent
// Author: Michael S. Booth, January 2002

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine


#include "Common/GameMemory.h"
#include "Common/GlobalData.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/PlayerTemplate.h"
#include "Common/Team.h"
#include "Common/ThingFactory.h"
#include "Common/BuildAssistant.h"
#include "Common/SpecialPower.h"
#include "Common/ThingTemplate.h"
#include "Common/WellKnownKeys.h"
#include "Common/Xfer.h"
#include "Common/Recorder.h"
#include "GameClient/InGameUI.h"
#include "GameLogic/VictoryConditions.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object.h"
#include "GameLogic/AISkirmishPlayer.h"
#include "GameLogic/SidesList.h"
#include "GameLogic/AI.h"
#include "GameLogic/AIPathfind.h"
#include "GameLogic/TerrainLogic.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/DozerAIUpdate.h"
#include "GameLogic/Module/RebuildHoleBehavior.h"
#include "GameLogic/Module/UpdateModule.h"
#include "GameLogic/PartitionManager.h"
#include "GameLogic/ScriptEngine.h"
#include "GameLogic/Module/ProductionUpdate.h"
#include "GameClient/TerrainVisual.h"	

#ifdef _INTERNAL
// for occasional debugging...
//#pragma optimize("", off)
//#pragma MESSAGE("************************************** WARNING, optimization disabled for debugging purposes")
#endif

#define USE_DOZER 1	 



///////////////////////////////////////////////////////////////////////////////////////////////////
// PRIVATE DATA ///////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////

AISkirmishPlayer::AISkirmishPlayer( Player *p ) :	AIPlayer(p), 
m_curFlankBaseDefense(0),
m_curFrontBaseDefense(0),
m_curFrontLeftDefenseAngle(0),
m_curFrontRightDefenseAngle(0),
m_curLeftFlankLeftDefenseAngle(0),
m_curLeftFlankRightDefenseAngle(0),
m_curRightFlankLeftDefenseAngle(0),
m_curRightFlankRightDefenseAngle(0),
m_frameToCheckEnemy(0),
m_currentEnemy(NULL),
m_hopelessSinceFrame(0),
m_surrenderFrame(0)

{
	m_frameLastBuildingBuilt = TheGameLogic->getFrame();
	p->setCanBuildUnits(true); // turn on ai production by default.

	//
	// Role is rolled once and kept for the whole match.  Sins is explicit that its AI does not flip
	// personality mid-game, and that consistency is what makes an opponent readable and therefore
	// counterable - an AI that turtles for ten minutes and then rushes is not deep, it is noise.
	// GameLogicRandomValue, so a replay rolls the same one.  Rolled here rather than in AIPlayer's
	// constructor so a Challenge or mission opponent leaves the logic random stream as retail did;
	// nothing between the two constructors draws, so a skirmish rolls on the same draw as before.
	// A seat that plays EA's AI (a player's own map, or one that scripts its seats) does not roll at
	// all: retail had no role and drew nothing here.  The sides list is read before the players are
	// made, so runsOwnAI() already knows, and in this body isSkirmishAI() is this class's.
	//
	if (runsOwnAI()) {
		m_role = (GameLogicRandomValue( 0, AIROLE_COUNT - 1 ) == 0) ? AIROLE_AGGRESSIVE : AIROLE_DEFENSIVE;
	}
}

AISkirmishPlayer::~AISkirmishPlayer()
{
	clearTeamsInQueue();
}


/**
 * Build our base.
 */
void AISkirmishPlayer::processBaseBuilding( void )
{
	//
	// Refresh base buildings. Scan through list, if a building is missing,
	// rebuild it, unless it's rebuild count is zero.
	//
	// A Hard AI in the dark does not wait out the structure timer for its power plant: a USA whose plants a
	// superweapon took sat dark for two minutes of logic frames with a plant asked for and money coming in.
	// Once a second rather than every frame, because a pass walks the whole plan.
	const Bool darkRush = runsOwnAI() && getSkillProfile()->m_economyBuildings && !m_player->getEnergy()->hasSufficientPower() &&
		TheGameLogic->getFrame() % LOGICFRAMES_PER_SECOND == 0;
	if (m_readyToBuildStructure || darkRush)
	{
		const ThingTemplate *bldgPlan=NULL;
		BuildListInfo	*bldgInfo = NULL;
		Bool isPriority = false;
		// and a Tech AI from Medium up, whose whole plan is the tree and the superweapon at its top, and a
		// GLA Air one, whose SCUD Storm stands in for the air force it cannot build
		const Bool rushBigGuns = runsOwnAI() && (getSkillProfile()->m_economyBuildings ||
			(m_persona != AIPERSONA_NORMAL && m_skillLevel >= AISKILL_MEDIUM && superweaponIsPlan()));
		Bool rushTaken = false;
		// With the purchase queue the priority entries are taken by its score, the first of the best in
		// list order, so an early drop zone does not wait behind the scripts' bunkers.
		const Bool byScore = purchaseQueueOn();
		const Bool earlyGame = TheGameLogic->getFrame() < AI_PURCHASE_EARLY_FRAMES;
		Int bldgScore = 0;
		Object *bldg = NULL;
		const ThingTemplate *powerPlan=NULL;
		BuildListInfo	*powerInfo = NULL;
		Bool isUnderPowered = !m_player->getEnergy()->hasSufficientPower();
		const Bool powerThin = isPowerThin();
		Bool powerUnderConstruction = false;
		for( BuildListInfo *info = m_player->getBuildList(); info; info = info->getNext() )
		{
			AsciiString name = info->getTemplateName();
			if (name.isEmpty()) continue;
			const ThingTemplate *curPlan = TheThingFactory->findTemplate( name );
			if (!curPlan) {																											 
				DEBUG_LOG(("*** ERROR - Build list building '%s' doesn't exist.\n", name.str()));
				continue;
			}
			bldg = TheGameLogic->findObjectByID( info->getObjectID() );	
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
					AIPlayer::profileBaseSubBegin();
					Object *obj;
					for( obj = TheGameLogic->getFirstObject(); obj; obj = obj->getNextObject() ) {
						if (!obj->isKindOf(KINDOF_REBUILD_HOLE)) continue;
						RebuildHoleBehaviorInterface *rhbi = RebuildHoleBehavior::getRebuildHoleBehaviorInterfaceFromObject( obj );
						if( rhbi ) {
							ObjectID spawnerID = rhbi->getSpawnerID();
							if (priorID == spawnerID) {
								DEBUG_LOG(("AI Found hole to rebuild %s\n", curPlan->getName().str()));
								info->setObjectID(obj->getID());
								break;
							}
						}
 					}
					AIPlayer::profileBaseSubEnd( AIPlayer::BASE_SUB_HOLE );
					// The hole rebuilds it by itself.  The bldg this pass tests further down is the outer
					// one, still the null lookup of the dead building, so the entry went on to be picked
					// for a dozer as well and a second copy went up beside the hole.
					if (info->getObjectID() != INVALID_ID) {
						continue;
					}
				}	else {
					if (bldg->getControllingPlayer() == m_player) {
						// Check for built or dozer missing.
						if( bldg->getStatusBits().test( OBJECT_STATUS_UNDER_CONSTRUCTION ) )
						{
							// a site its dozers cannot reach, or die on the way to, is cancelled for the money;
							// EA's AI waits on it for ever, as retail did
							if (runsOwnAI() && watchSite(info, bldg)) {
								continue;
							}
							if (bldg->isKindOf(KINDOF_FS_POWER) && !bldg->isKindOf(KINDOF_CASH_GENERATOR))
							{
								powerUnderConstruction = true;
							}
							// make sure dozer is working on him.
							ObjectID builder = bldg->getBuilderID();
							Object* myDozer = TheGameLogic->findObjectByID(builder);
              
              if (myDozer && ( myDozer->getControllingPlayer() != m_player || myDozer->isDisabledByType( DISABLED_UNMANNED ) ) )
              {//I don't expect this dozer to work well with me.
                myDozer = NULL;
                bldg->setBuilder( NULL );
              }

							if (myDozer==NULL) {
								DEBUG_LOG(("AI's Dozer got killed (or captured).  Find another dozer.\n"));
								queueDozer();
 								myDozer = findDozer(bldg->getPosition());
								if (myDozer==NULL || myDozer->getAI()==NULL) {
									continue;
								}
								myDozer->getAI()->aiResumeConstruction(bldg, CMD_FROM_AI);
							}	else {
								// make sure he is building.
								myDozer->getAI()->aiResumeConstruction(bldg, CMD_FROM_AI);
							}
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
			if (bldg) {
				continue; // already built.
			}
			// A second war factory, barracks or airfield of the plan's waits until the ones standing
			// fall behind their queues, as an extra one would.  Only for the fork's AI: the queues are
			// sampled by its doSpending, so EA's would wait on a sample that never comes.
			if (runsOwnAI() && holdsExtraFactory(curPlan)) {
				continue;
			}
			// The safety check below is a range query over everything near the spot, most of a
			// millisecond each late in a 4v4, and it ran for every unbuilt entry on every pass (36 of
			// them, 28ms, in one frame). An entry that cannot change what this pass picks skips it.
			// A priority entry whose prerequisite is not standing yet was taken all the same, and
			// buildObjectNow refused it on every pass, so nothing queued behind it went up either: a
			// China AI asked for a bunker before its barracks and sat on 35,000 until frame 8116.
			// A Hard AI puts its superweapon and the tech building it needs ahead of every other priority
			// entry: the scripts keep seven to twenty of those waiting through a match, one goes up a
			// pass, and a tech building asked for at frame 1932 was still in that queue at 30000.
			// A power plant jumps it as well once the margin is thin: the plant doPower asked for ahead
			// of an outage waited 45 seconds in that queue and the base went dark before it was started.
			const Bool rushThis = rushBigGuns && (curPlan->isKindOf(KINDOF_FS_SUPERWEAPON) || curPlan->isKindOf(KINDOF_FS_ADVANCED_TECH) ||
				(powerThin && curPlan->isKindOf(KINDOF_FS_POWER) && !curPlan->isKindOf(KINDOF_CASH_GENERATOR)));
			const AIPurchaseKind planKind = purchaseKindOf(curPlan);
			const Int planScore = byScore ? aiPurchaseScore(planKind, earlyGame) +
				(m_persona != AIPERSONA_NORMAL ? personaWeights().m_buy[planKind] + personaBuildingScore(curPlan) : 0) : 0;
			const Bool couldBePriority = info->isPriorityBuild() &&
				(!isPriority || (rushThis && !rushTaken) || planScore > bldgScore) && m_player->canBuild(curPlan);
			const Bool couldBePower = powerPlan==NULL && curPlan->isKindOf(KINDOF_FS_POWER) &&
				!curPlan->isKindOf(KINDOF_CASH_GENERATOR) && (isUnderPowered || info->isAutomaticBuild());
			// The rest of the plan, unasked: a seat its map scripts builds what the scripts ask and the
			// power, as retail's did, or an Art of Defense GLA puts up a base and leaves its waves.
			const Bool couldBeBuilt = runsOwnAI() && bldgPlan==NULL && info->isAutomaticBuild() && info->isBuildable();
			// EA's AI walked every entry on, and short of power an automatic one with no free dozer
			// queues one below; that entry keeps going for it, so the dozer comes when retail's did.
			const Bool retailDozerAsk = !runsOwnAI() && isUnderPowered && info->isAutomaticBuild();
			if (!couldBePriority && !couldBePower && !couldBeBuilt && !retailDozerAsk) {
				continue;
			}
			// Make sure it is safe to build here.
			AIPlayer::profileBaseSubBegin();
			const Bool locationSafe = isLocationSafe(info->getLocation(), curPlan);
			AIPlayer::profileBaseSubEnd( AIPlayer::BASE_SUB_SAFE );
			if (!locationSafe) {
				continue;
			}
			if (couldBePriority) {
				// Always take priority build, unless we already have priority build.
				bldgPlan = curPlan;
				bldgInfo = info;
				isPriority = true;
				rushTaken = rushThis;
				bldgScore = planScore;
			}
			if (curPlan->isKindOf(KINDOF_FS_POWER)) {
				if (powerPlan==NULL && !curPlan->isKindOf(KINDOF_CASH_GENERATOR)) {
					if (isUnderPowered || info->isAutomaticBuild()) {
						powerPlan = curPlan;
						powerInfo = info;
					}
				}
			}
			if (!info->isAutomaticBuild()) {
				continue; // marked to not build automatically.
			}
			AIPlayer::profileBaseSubBegin();
			Object *dozer = findDozer(info->getLocation());
			AIPlayer::profileBaseSubEnd( AIPlayer::BASE_SUB_FINDDOZER );
			if (dozer==NULL) {
				if (isUnderPowered) {
					queueDozer();
				}
				continue;
			}
			// this used to test bldgPlan, which is still NULL for anything but a priority build at
			// this point - and canMakeUnit(dozer, NULL) is CANMAKE_NO_PREREQ, so every
			// AutomaticallyBuild entry hit the continue below and the AI only ever built what its
			// scripts marked priority (plus the forced power plant).
			AIPlayer::profileBaseSubBegin();
			const CanMakeType canMake = TheBuildAssistant->canMakeUnit(dozer, curPlan);
			AIPlayer::profileBaseSubEnd( AIPlayer::BASE_SUB_CANMAKE );
			if (canMake!=CANMAKE_OK) {
				if (info->isBuildable()) {
					AsciiString bldgName = info->getTemplateName();
					bldgName.concat(" - Dozer unable to build - money or technology missing.");
					TheScriptEngine->AppendDebugMessage(bldgName, false);
				}
				continue;
			}
			// check if this building has any "rebuilds" left.  EA's test above read bldgPlan, so this
			// never took anything for retail; EA's AI still builds only what is asked and the power.
			if (info->isBuildable() && runsOwnAI())
			{
				if (bldgPlan == NULL) {
					bldgPlan = curPlan;
					bldgInfo = info;
				}
			}
		}
		if (powerPlan && powerInfo && !powerPlan->isEquivalentTo(bldgPlan)) {
			if (!powerUnderConstruction) {
				bldgPlan = powerPlan;
				bldgInfo = powerInfo;
				DEBUG_LOG(("Forcing build of power plant.\n"));
			}
		}
		if (!m_readyToBuildStructure && bldgPlan != powerPlan) {
			bldgPlan = NULL;		// the timer is only skipped for the power plant
		}
		if (bldgPlan && bldgInfo) {
#ifdef USE_DOZER
			// dozer-construct the building
			AIPlayer::profileBaseSubBegin();
			bldg = buildStructureWithDozer(bldgPlan, bldgInfo);
			AIPlayer::profileBaseSubEnd( AIPlayer::BASE_SUB_BUILD );
			// store the object with the build order
			if (bldg)
			{
				bldgInfo->setObjectID( bldg->getID() );
				bldgInfo->decrementNumRebuilds();

				m_readyToBuildStructure = false;
				m_structureTimer = computeStructureDelay();
				m_frameLastBuildingBuilt = TheGameLogic->getFrame();
				// only build one building per delay loop
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
		}
	}
}

/**
 * Invoked when a unit I am training comes into existence
 */
void AISkirmishPlayer::onUnitProduced( Object *factory, Object *unit )
{
	AIPlayer::onUnitProduced(factory, unit);
}

/**
 * Search the computer player's buildings for one that can build the given request
 * and start training the unit.
 * If busyOK is true, it will queue a unit even if one is building.  This lets 
 * script invoked teams "push" to the front of the queue.
 */
Bool AISkirmishPlayer::startTraining( WorkOrder *order, Bool busyOK, AsciiString teamName)
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


/**
 * Check if this team is buildable, doesn't exceed maximum limits, meets conditions, and isn't under construction.
 */
Bool AISkirmishPlayer::isAGoodIdeaToBuildTeam( TeamPrototype *proto )
{
	// Check condition.
	if (!proto->evaluateProductionCondition()) {
		return false;
	}

	// the hacker cap and the hoard are the fork's AI; EA's builds what its scripts ask, as many as the data says
	if (runsOwnAI() && hasEnoughMoneyUnitsFor(proto)) {
		return false;
	}
	// check build limit, which a hoard raises for attack teams; defence teams keep the data's, and so do
	// the guard teams the scripts never send, or a rich AI stood four copies of its tunnel guards at home
	const TeamTemplateInfo *info = proto->getTemplateInfo();
	Int allowedInstances = info->m_maxInstances;
	if (!info->m_isBaseDefense && !info->m_isPerimeterDefense && aiTeamAttacks(info)) {
		allowedInstances = aiHoardAllowedTeamInstances( allowedInstances, m_player->getMoney()->countMoney(),
																										hoardThreshold() );
	}
	if (proto->countTeamInstances() >= allowedInstances){
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

/**
 * See if any existing teams need reinforcements, and have higher priority.
 */
Bool AISkirmishPlayer::selectTeamToReinforce( Int minPriority )
{
	return AIPlayer::selectTeamToReinforce(minPriority);
}

/**
 * Determine the next team to build.  Return true if one was selected.
 */
Bool AISkirmishPlayer::selectTeamToBuild( void )
{
	return AIPlayer::selectTeamToBuild();	
}

/** 
	Build a specific building.  
	*/
void AISkirmishPlayer::buildSpecificAIBuilding(const AsciiString &thingName)
{
	//
	Bool found = false;
	Bool foundUnbuilt = false;
	for( BuildListInfo *info = m_player->getBuildList(); info; info = info->getNext() )
	{
		if (info->getTemplateName()==thingName) 
		{
			AsciiString name = info->getTemplateName();
			if (name.isEmpty()) continue;
			const ThingTemplate *bldgPlan = TheThingFactory->findTemplate( name );
			if (!bldgPlan) {																											 
				DEBUG_LOG(("*** ERROR - Build list building '%s' doesn't exist.\n", name.str()));
				continue;
			}
			Object *bldg = TheGameLogic->findObjectByID( info->getObjectID() );
			found = true;
			if (bldg) {
				continue; // already built.
			}
			if (info->isPriorityBuild()) {
				continue; // already marked for priority build.
			}
			foundUnbuilt = true;
			info->markPriorityBuild();
			break;
		}
	}	
	if (foundUnbuilt) {
		m_buildDelay = 0;
		AsciiString buildingStr = "Queueing building '";
		buildingStr.concat(thingName);
		buildingStr.concat("' for construction.");
		TheScriptEngine->AppendDebugMessage(buildingStr, false); 
	}	else if (found) {
		AsciiString buildingStr = "Warning - all instances of building '";
		buildingStr.concat(thingName);
		buildingStr.concat("' are already built or queued for build, not queueing.");
		TheScriptEngine->AppendDebugMessage(buildingStr, false); 
	}	else {
		AsciiString buildingStr = "Error - could not find building '";
		buildingStr.concat(thingName);
		buildingStr.concat("' in the building template list.");
		TheScriptEngine->AppendDebugMessage(buildingStr, false); 
	}
}



/** 
	Gets the player index of my enemy.  
	*/
Int AISkirmishPlayer::getMyEnemyPlayerIndex(void) {
	Int playerNdx;
	if (m_currentEnemy) {
		return m_currentEnemy->getPlayerIndex();
	}
	// For now, return first human player, as there should only be one. jba
	for (playerNdx=0; playerNdx<ThePlayerList->getPlayerCount(); playerNdx++) {
		if (ThePlayerList->getNthPlayer(playerNdx)->getPlayerType() == PLAYER_HUMAN) {
			break;
		}
	}
	return playerNdx;
}

/** 
	Get the AI's enemy.  Recalc if it has been a while (5 seconds.)  
*/
//----------------------------------------------------------------------------------------------------------
/** Where to send an attack, off the engine's own influence map.
	*
	* This is B4: the map is already there, per player, with cash and threat per cell, with a query
	* API and even an in-game debug view - and until now its only users were two script actions.  The
	* C++ AI never touched it, and aimed every attack at the geometric centre of the enemy's
	* buildings, which is the middle of his base and therefore the middle of his defences.
	*
	* The enemy's most valuable cell is where his economy is, which is usually not where his guns
	* are.  Fog-masked, so it is somewhere this AI has actually seen; FALSE means it has not looked
	* yet and the caller keeps the old aim point.
	*
	* Hard's alone, and only where the map has no approach path to take: with one the attack goes down
	* the road chooseApproachLabel picks instead, since aiming at the money walked past the army.  See the
	* note on useInfluenceMapForAttackLane in AI.cpp.  This is the engine's cash layer, not AIInfluenceMap.
	*/
//----------------------------------------------------------------------------------------------------------
Bool AISkirmishPlayer::influenceMapAttackGoal( Coord3D *goal )
{
	// EA's AI aims at the middle of the buildings, as retail did
	if( !runsOwnAI() || !getSkillProfile()->m_useInfluenceMapForAttackLane )
		return FALSE;
	if( ThePartitionManager == NULL || goal == NULL )
		return FALSE;

	Coord3D found;
	if( !ThePartitionManager->getMostValuableVisibleLocation( m_player->getPlayerIndex(), ALLOW_ENEMIES,
																													 VOT_CashValue, &found ) )
		return FALSE;

	goal->x = found.x;
	goal->y = found.y;
	goal->z = TheTerrainLogic->getGroundHeight( goal->x, goal->y );
	return TRUE;
}

//----------------------------------------------------------------------------------------------------------
/** EA's acquireEnemy as it shipped, for a seat that plays EA's AI.  Its quirks are the point: the
	* crippled enemy pushed to half the map, the gang-up penalty counting this AI's own pick, the
	* "attacking me" nudge going to whoever else is targeted, and the bounds read through the fog. */
//----------------------------------------------------------------------------------------------------------
void AISkirmishPlayer::acquireEnemyRetail(void)
{
	Player *bestEnemy = NULL;
	Real bestDistanceSqr = HUGE_DIST*HUGE_DIST;

	if (m_currentEnemy) {
		Bool inBadShape = !m_currentEnemy->hasAnyUnits() || !m_currentEnemy->hasAnyBuildFacility();
		if (!inBadShape) return;
	}

	// look for the closest enemy.
	Int i;
	for (i=0; i<ThePlayerList->getPlayerCount(); i++) {
		Player *curPlayer = ThePlayerList->getNthPlayer(i);
		if (m_player->getRelationship(curPlayer->getDefaultTeam()) == ENEMIES) {
			if (curPlayer->hasAnyObjects()==false) continue; // not much of an enemy.
			// ok, we got an enemy;
			// If a player is out of units, or out of build facilities, we can lower his priority.
			Bool inBadShape = !curPlayer->hasAnyUnits() || !curPlayer->hasAnyBuildFacility();

			Coord3D enemyPos = m_baseCenter;
			Region2D bounds;
			getPlayerStructureBounds(&bounds, i);
			enemyPos.x = bounds.lo.x + bounds.width()/2;
			enemyPos.y = bounds.lo.y + bounds.height()/2;
			Real curDistSqr = sqr(enemyPos.x-m_baseCenter.x) + sqr(enemyPos.y-m_baseCenter.y);

			//Fudge for in bad shape.	 If an enemy is crippled, concentrate on the other ones.
			if (inBadShape) {
				curDistSqr = HUGE_DIST*HUGE_DIST*0.5f;
			}
			// See if other ai's are attacking this target.
			// We don't want the ai's to gang up on one enemy.
			Int k;
			for (k=0; k<ThePlayerList->getPlayerCount(); k++) {
				if (k==i) continue;  // don't count self.
				Player *somePlayer = ThePlayerList->getNthPlayer(k);
				if (somePlayer->isSkirmishAIPlayer() && (somePlayer->getCurrentEnemy()==curPlayer)) {
					// Some ai is already targeting this guy.  Add a distance penalty.
					curDistSqr += (500*500);
				}
				if (somePlayer->isSkirmishAIPlayer() && (somePlayer->getCurrentEnemy()==m_player)) {
					// he is attacking me.  So I will (gently) prefer to attack him.
					curDistSqr -= (25*25);
					if (curDistSqr<0) curDistSqr = 0;
				}
			}

			// Ai enemy - will take if we don't get a better offer.
			if (curDistSqr<bestDistanceSqr) {
				bestEnemy = curPlayer;
				bestDistanceSqr = curDistSqr;
			}
		}
	}
	if (bestEnemy!=NULL && (bestEnemy!=m_currentEnemy)) {
		m_currentEnemy = bestEnemy;
		AsciiString msg = TheNameKeyGenerator->keyToName(m_player->getPlayerNameKey());
		msg.concat(" acquiring target enemy player: ");
		msg.concat(TheNameKeyGenerator->keyToName(m_currentEnemy->getPlayerNameKey()));
		TheScriptEngine->AppendDebugMessage( msg, false);
	}
}

void AISkirmishPlayer::acquireEnemy(void)
{
	if (!runsOwnAI()) {
		acquireEnemyRetail();
		return;
	}

	Player *bestEnemy = NULL;
	Real bestDistanceSqr = HUGE_DIST*HUGE_DIST;

	// a personality joins an ally's attack (the team board): his target is ours while it stands
	if (m_persona != AIPERSONA_NORMAL && m_persona != AIPERSONA_OPPORTUNIST) {
		const AITeamRequest *attack = freshRequest(AIREQ_ATTACK, AI_COOP_JOIN_FRAMES);
		Player *target = attack && attack->enemy >= 0 ? ThePlayerList->getNthPlayer(attack->enemy) : NULL;
		if (target && target != m_currentEnemy && m_player->getRelationship(target->getDefaultTeam()) == ENEMIES &&
				target->hasAnyObjects()) {
			DEBUG_LOG(("AI COOP frame %d player %d joins player %d's attack on player %d\n", TheGameLogic->getFrame(),
				m_player->getPlayerIndex(), attack->from, attack->enemy));
			m_currentEnemy = target;
			coopChat("Join");
			return;
		}
	}

	// an Opportunist keeps looking for the weaker, busier enemy, and changes when one is clearly cheaper
	const Bool opportunist = m_persona == AIPERSONA_OPPORTUNIST;
	Real currentCost = HUGE_DIST*HUGE_DIST;
	if (m_currentEnemy && !opportunist) {
		Bool inBadShape = !m_currentEnemy->hasAnyUnits() || !m_currentEnemy->hasAnyBuildFacility();
		if (!inBadShape) return;
	}

	//
	// How much of what this AI can see belongs to each candidate, so a fat enemy outweighs a thin
	// one at the same distance. Gathered first because each player's share is of the total.
	//
	Real estate[ MAX_PLAYER_COUNT ];
	Real estateTotal = 0.0f;
	{
		for (Int e = 0; e < ThePlayerList->getPlayerCount() && e < MAX_PLAYER_COUNT; e++) {
			estate[e] = visibleEstateValue(e);
			estateTotal += estate[e];
		}
	}

	// look for the best enemy to go after.
	Int i;
	for (i=0; i<ThePlayerList->getPlayerCount(); i++) {
		Player *curPlayer = ThePlayerList->getNthPlayer(i);
		if (m_player->getRelationship(curPlayer->getDefaultTeam()) == ENEMIES) {
			if (curPlayer->hasAnyObjects()==false) continue; // not much of an enemy.
			// ok, we got an enemy;
			// If a player is out of units, or out of build facilities, we can lower his priority.
			Bool inBadShape = !curPlayer->hasAnyUnits() || !curPlayer->hasAnyBuildFacility();

			Coord3D enemyPos = m_baseCenter;
			Region2D bounds;
			getPlayerStructureBounds(&bounds, i, FALSE, m_player->getPlayerIndex());
			enemyPos.x = bounds.lo.x + bounds.width()/2;
			enemyPos.y = bounds.lo.y + bounds.height()/2;
			Real curDistSqr = sqr(enemyPos.x-m_baseCenter.x) + sqr(enemyPos.y-m_baseCenter.y);

			//
			// The two flags EA folded straight into the distance, gathered rather than applied:
			// aiEnemyCost decides what each is worth, and its test says what that is.  The "in bad
			// shape" rule went with them, with its sign corrected - EA set a crippled enemy's
			// distance to half the map, which reads as "ignore the one you are about to beat".
			//
			Bool alreadyTargeted = FALSE;
			// The candidate itself has to have picked us. EA asked this of every other AI in the loop
			// below, which skips the candidate, so the bonus went to anyone but the aggressor.
			Bool attackingMe = curPlayer->isSkirmishAIPlayer() && curPlayer->getCurrentEnemy()==m_player;
			Bool targetedByAnother = FALSE;		// as alreadyTargeted, this AI's own pick left out
			Int k;
			for (k=0; k<ThePlayerList->getPlayerCount(); k++) {
				if (k==i) continue;  // don't count self.
				Player *somePlayer = ThePlayerList->getNthPlayer(k);
				if (!somePlayer->isSkirmishAIPlayer()) continue;
				if (somePlayer->getCurrentEnemy()==curPlayer) {
					alreadyTargeted = TRUE;
					if (somePlayer != m_player)
						targetedByAnother = TRUE;
				}
			}

			const Real share = (estateTotal > 0.0f && i < MAX_PLAYER_COUNT) ? (estate[i] / estateTotal) : 0.0f;
			// an Opportunist does not count its own pick as somebody else's: its current enemy would pay
			// the gang-up term and the busy discount for being its own target
			curDistSqr = aiEnemyCost( curDistSqr, inBadShape, opportunist ? targetedByAnother : alreadyTargeted, attackingMe, share,
														getAIRole() == AIROLE_SUPPORTIVE );
			if (opportunist) {
				// busy: another computer's chosen enemy, or hit in the last twenty seconds while he is not
				// ours, so the hit was somebody else's.  The player keeps only when he was last hit, not by whom.
				const UnsignedInt hit = curPlayer->getAttackedFrame();
				const Bool busy = targetedByAnother ||
					(curPlayer != m_currentEnemy && hit != 0 && hit + 20 * LOGICFRAMES_PER_SECOND > TheGameLogic->getFrame());
				curDistSqr = aiPersonaEnemyCost( m_persona, curDistSqr, share, busy );
				if (curPlayer == m_currentEnemy)
					currentCost = curDistSqr;
			}

			// Ai enemy - will take if we don't get a better offer.
			if (curDistSqr<bestDistanceSqr) {
				bestEnemy = curPlayer;
				bestDistanceSqr = curDistSqr;
			}
		}
	}
	// a standing enemy the Opportunist could still fight is dropped only for one at under 70% of his cost
	if (opportunist && m_currentEnemy && bestDistanceSqr >= 0.7f * currentCost &&
			m_currentEnemy->hasAnyUnits() && m_currentEnemy->hasAnyBuildFacility())
		return;
	if (bestEnemy!=NULL && (bestEnemy!=m_currentEnemy)) {
		if (opportunist)
			DEBUG_LOG(("AI PERSONA frame %d player %d turns on player %d, cost %.0f against %.0f\n", TheGameLogic->getFrame(),
				m_player->getPlayerIndex(), bestEnemy->getPlayerIndex(), bestDistanceSqr, currentCost));
		m_currentEnemy = bestEnemy;
		AsciiString msg = TheNameKeyGenerator->keyToName(m_player->getPlayerNameKey());
		msg.concat(" acquiring target enemy player: ");
		msg.concat(TheNameKeyGenerator->keyToName(m_currentEnemy->getPlayerNameKey()));
		TheScriptEngine->AppendDebugMessage( msg, false);
	}
}



/** 
	Get the AI's enemy.  Recalc if it has been a while (20 seconds.)  
*/
Player *AISkirmishPlayer::getAiEnemy(void)
{
	if (TheGameLogic->getFrame()>=m_frameToCheckEnemy) {
		m_frameToCheckEnemy = TheGameLogic->getFrame() + 5*LOGICFRAMES_PER_SECOND;
		acquireEnemy();
	}
	return m_currentEnemy;
}

/** 
	Build base defense structures on the front or flank of the base.  
*/
void AISkirmishPlayer::buildAIBaseDefense(Bool flank)
{
	const AISideInfo *resInfo = TheAI->getAiData()->findSideInfo( m_player );
	AsciiString defenseTemplateName;
	if (resInfo) {
		defenseTemplateName = resInfo->m_baseDefenseStructure1;
	}
	// the data names one defence per side, so a script asking for ten got ten of it: the side's mix
	// comes off a dozer's buttons instead, and the data's choice stands in when no dozer is left.
	// EA's AI builds the data's one, on the approach-path ring, as retail did.
	Object *dozer = runsOwnAI() ? findNearestDozer(&m_baseCenter) : NULL;
	const ThingTemplate *mix = dozer ? nextBaseDefense(dozer) : NULL;
	if (mix) {
		// the spot with the widest field of fire first; the approach-path ring when none scores
		if (!placeDefense(mix))
			buildAIBaseDefenseStructure(mix->getName(), flank);
	} else if (resInfo) {
		buildAIBaseDefenseStructure(resInfo->m_baseDefenseStructure1, flank);
	}
}

/** 
	Build base defense structures on the front or flank of the base.  
	Base defenses are placed as follows:
	m_baseCenter and m_baseRadius are calculated on map load.
	Defenses are placed along the this circle.
	Front defenses (!flank) are placed starting at the "Center" approach path.  
	The first front defense is placed towards th Center path.  Number 2 is placed
	to the left of #1, #3 is placed to the right of #1, #4 is placed to the left of 
	#2 and so on.  So it looks like:

												#1
									 #2 			#3
					#6  #4								 #5	  #7
		  #8																	#9

	The flank base defenses cover the "Flank" approach, and the "Backdoor" approach.
	They alternate between these two, so the first flank defense covers flank, and the second 
	covers backdoor, and continue to alternate.  They cover the approach using the same 
	pattern as front above.
	John A.

	*/
void AISkirmishPlayer::buildAIBaseDefenseStructure(const AsciiString &thingName, Bool flank)
{
	const ThingTemplate *tTemplate = TheThingFactory->findTemplate(thingName);
	if (tTemplate==NULL) {
		DEBUG_CRASH(("Couldn't find base defense structure '%s' for side %s", thingName.str(), m_player->getSide().str()));
		return;
	}
	do {
		AsciiString pathLabel;
		if (flank) {
			if (m_curFlankBaseDefense&1) {
				pathLabel.format("%s%d", SKIRMISH_FLANK, m_player->getMpStartIndex()+1);
			}	else {
				pathLabel.format("%s%d", SKIRMISH_BACKDOOR, m_player->getMpStartIndex()+1);
			}
		}	else {
			pathLabel.format("%s%d", SKIRMISH_CENTER, m_player->getMpStartIndex()+1);
		}

		Coord3D goalPos = m_baseCenter;
		Waypoint *way = TheTerrainLogic->getClosestWaypointOnPath( &goalPos, pathLabel );
		if (way) {
			goalPos = *way->getLocation();
		} else {
			if (flank) return;
			if (!influenceMapAttackGoal(&goalPos)) {
				Region2D bounds;
				// what this AI has seen of him; EA's sees every building, as retail did
				getPlayerStructureBounds(&bounds, getMyEnemyPlayerIndex(), FALSE, runsOwnAI() ? m_player->getPlayerIndex() : -1);
				goalPos.x = bounds.lo.x + bounds.width()/2;
				goalPos.y = bounds.lo.y + bounds.height()/2;
			}
		}
		Coord2D offset;
		offset.x = goalPos.x-m_baseCenter.x;
		offset.y = goalPos.y-m_baseCenter.y;
		offset.normalize();
		Real defenseDistance = m_baseRadius;
		defenseDistance += TheAI->getAiData()->m_skirmishBaseDefenseExtraDistance;
		offset.x *= defenseDistance;
		offset.y *= defenseDistance;

		Real structureRadius = tTemplate->getTemplateGeometryInfo().getBoundingCircleRadius();
		Real baseCircumference = 2*PI*defenseDistance;
		Real angleOffset = 2*PI*(structureRadius*4/baseCircumference);

		Int selector;
		Real angle;
		if (flank) {
			selector = m_curFlankBaseDefense>>1;
			if (m_curFlankBaseDefense&1) {
				if (selector&1) {
					m_curLeftFlankRightDefenseAngle -= angleOffset;
					angle = m_curLeftFlankRightDefenseAngle;
				}	else {
					angle = m_curLeftFlankLeftDefenseAngle;
					m_curLeftFlankLeftDefenseAngle += angleOffset;
				}
			}	else {
				if (selector&1) {
					m_curRightFlankRightDefenseAngle -= angleOffset;
					angle = m_curRightFlankRightDefenseAngle;
				}	else {
					angle = m_curRightFlankLeftDefenseAngle;
					m_curRightFlankLeftDefenseAngle += angleOffset;
				}
			}

		} else {
			selector = m_curFrontBaseDefense;
			if (selector&1) {
				m_curFrontRightDefenseAngle -= angleOffset;
				angle = m_curFrontRightDefenseAngle;
			}	else {
				angle = m_curFrontLeftDefenseAngle;
				m_curFrontLeftDefenseAngle += angleOffset;
			}
		}

		// fabs: the right-hand angles walk NEGATIVE (they are decremented above), so a bare
		// "angle > PI/3" only ever bounded the left side and right-side defenses wrapped past
		// the intended +/-60 degree arc.
		if (fabs(angle) > PI/3) break;
		Real s = Sin(angle);
		Real c = Cos(angle);

		/* Two DEBUG_LOG lines used to sit here, printing the angle and the candidate position for
			 every position this loop tries, and DebugLog fflushes every line. Removed as noise - they
			 are sin and cos of a placement angle, which nobody reads, and RELEASE_DEBUG_LOGGING is on
			 by default so they were doing synchronous writes in a shipping build.

			 They are NOT what makes this action expensive, which is worth writing down because it
			 looked like they might be: removing them changed nothing measurable. The action costs
			 10-13ms in one logic frame, and the old logging shows why - it never printed more than
			 ten candidates for one call, so the cost is roughly a millisecond per candidate, inside
			 isLocationLegalToBuild. */
		Coord3D buildPos = m_baseCenter;
		buildPos.x += offset.x*c - offset.y*s;
		buildPos.y += offset.y*c + offset.x*s;

		/* See if we can build there. */
		Bool canBuild;
		Real placeAngle = tTemplate->getPlacementViewAngle();
		// a spot the base cannot drive to guards nothing: a ledge, or across a valley the path runs beside.
		// EA's AI takes the first legal spot, as retail did.
		canBuild = (!runsOwnAI() || reachableFromBase(&buildPos)) && LBC_OK == TheBuildAssistant->isLocationLegalToBuild(&buildPos, tTemplate, placeAngle,
			BuildAssistant::TERRAIN_RESTRICTIONS|BuildAssistant::NO_OBJECT_OVERLAP, NULL, m_player);
		TheTerrainVisual->removeAllBibs();	// isLocationLegalToBuild adds bib feedback, turn it off.  jba.
		if (flank) {
			m_curFlankBaseDefense++;
		} else {
			m_curFrontBaseDefense++;
		}
		if (canBuild) {
			m_player->addToPriorityBuildList(thingName, &buildPos, placeAngle);
			break;
		}
	}	while (true);

}


/** 
	Checks bridges along a waypoint path.  If any are destroyed, sends a dozer to fix, and returns true.
	If there is no bridge problem, returns false.
	*/
Bool AISkirmishPlayer::checkBridges(Object *unit, Waypoint *way)
{
	Coord3D unitPos = *unit->getPosition();
	AIUpdateInterface *ai = unit->getAI();
	if (!ai) return false; // no ai 
	const LocomotorSet& locoSet = ai->getLocomotorSet();
	Waypoint *curWay;
	for (curWay = way; curWay; curWay = curWay->getNext()) {
		if (TheAI->pathfinder()->clientSafeQuickDoesPathExist(locoSet, &unitPos, curWay->getLocation())) {
			continue;
		}
		ObjectID brokenBridge = INVALID_ID;
		if (TheAI->pathfinder()->findBrokenBridge(locoSet, &unitPos, curWay->getLocation(), &brokenBridge)) {
			repairStructure(brokenBridge);
			return true;
		}
	}
	return false;

}


/** 
	Build a specific team.  If priorityBuild, put at front of queue with priority set.
	*/
void AISkirmishPlayer::buildSpecificAITeam( TeamPrototype *teamProto, Bool priorityBuild)
{
	AIPlayer::buildSpecificAITeam(teamProto, priorityBuild);
}


/** 
	Recruit a specific team, within the specific radius of the home position.
	*/
void AISkirmishPlayer::recruitSpecificAITeam(TeamPrototype *teamProto, Real recruitRadius)
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
		if (!teamProto->getTemplateInfo()->m_hasHomeLocation) 
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




/**
 * Train our teams.
 */
void AISkirmishPlayer::processTeamBuilding( void )
{
	// select a new team
	if (selectTeamToBuild()) {
		queueUnits();
	}
}

//----------------------------------------------------------------------------------------------------------
/**
 * How much faster than AIData's written delays the skirmish AI works.
 *
 * EA got the same effect by clamping m_structureTimer and m_teamTimer to a flat 3 seconds on
 * every frame they counted down.  That did make the skirmish AI brisk, but it also silently
 * threw away everything the data said: with the shipped TeamSeconds of 10 the clamp fired for
 * every player, so TeamsWealthyRate and TeamsPoorRate changed nothing, a rich AI and a broke
 * AI queued teams at exactly the same 3 second beat, and the SET_BASE_CONSTRUCTION_SPEED
 * script action could only ever slow an AI down to 3 seconds too.  Expressed as a rate
 * instead, the shipped values land on the same 3 seconds they do today, and the modifiers
 * work again around it: a wealthy skirmish AI presses at 1.5 seconds, a poor one drops to 5.
 * See FINDINGS.md 4.
 *
 * A seat that plays EA's AI keeps the data's delays and EA's clamp (doBaseBuilding, doTeamBuilding).
 */
Real AISkirmishPlayer::getBuildRateScale( void )
{
	return runsOwnAI() ? 10.0f/3.0f : 1.0f;
}

//----------------------------------------------------------------------------------------------------------
/**
 * See if it's time to build another base building.
 */
void AISkirmishPlayer::doBaseBuilding( void )
{
	if (m_player->getCanBuildBase()) {
		// See if we are ready to start trying a structure.
		if (!m_readyToBuildStructure) {
			m_structureTimer--;
			if (m_structureTimer<=0) {
				m_readyToBuildStructure = true;
				m_buildDelay = 0;
			}
			// EA's flat 3 seconds, for a seat that plays EA's AI; the fork's works off getBuildRateScale
			if (!runsOwnAI() && m_structureTimer > 3*LOGICFRAMES_PER_SECOND) {
				m_structureTimer = 3*LOGICFRAMES_PER_SECOND;
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
void AISkirmishPlayer::checkReadyTeams( void )
{
	AIPlayer::checkReadyTeams();
}

//----------------------------------------------------------------------------------------------------------
/**
 * See if any queued teams have finished building, or have run out of time.
 */
void AISkirmishPlayer::checkQueuedTeams( void )
{
	AIPlayer::checkQueuedTeams();
}

//----------------------------------------------------------------------------------------------------------
/**
 * See if it is time to start another ai team building.
 */
void AISkirmishPlayer::doTeamBuilding( void )
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
			// EA's flat 3 seconds, as for the structure timer above
			if (!runsOwnAI() && m_teamTimer > 3*LOGICFRAMES_PER_SECOND) {
				m_teamTimer = 3*LOGICFRAMES_PER_SECOND;
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
			m_teamDelay = 2*LOGICFRAMES_PER_SECOND; // check again in 5 seconds.
			// Note that this timer gets shortcut when a unit or building is completed.
		}
	}
}

//----------------------------------------------------------------------------------------------------------
/**
 * Perform computer-controlled player AI
 */
void AISkirmishPlayer::update( void )
{
	AIPlayer::update();
	doSurrender();
}

//----------------------------------------------------------------------------------------------------------
/** What a player is worth on the field: the build cost of everything alive he owns, and how many of
	* it can put a building up. */
struct SurrenderTally
{
	Int64 value;		// an Unlimited bank alone is 10 million, and the check multiplies by 100
	Int builders;
};

static void tallyForSurrender( Object *obj, void *userData )
{
	if( obj->isEffectivelyDead() )
		return;
	SurrenderTally *tally = (SurrenderTally *)userData;
	tally->value += obj->getTemplate()->friend_getBuildCost();
	if( obj->isKindOf( KINDOF_DOZER ) )
		++tally->builders;
}

/// The bank counts toward worth, except an Unlimited one: every side holds the same bottomless 10
/// million there, which would bury the field in the comparison and never let anyone fall behind.
static Int64 bankForSurrender( Player *player )
{
	const Money *money = player->getMoney();
	return money->isUnlimited() ? 0 : money->countMoney();
}

//----------------------------------------------------------------------------------------------------------
/**
 * Give up the way a person does when the match is gone: say GG, and a moment later surrender, handing
 * whatever is left to a living ally.  Lost means one of two things, held for a while so a dip in a
 * fight or a base being rebuilt does not count:
 *   - nothing left that builds and nothing that could put a builder back up, while behind on worth;
 *   - worth (build cost of everything alive, plus the bank unless it is Unlimited) a small fraction of the strongest
 *     living enemy's.
 * Easy gives up soonest, Hard fights longest.  Only in skirmish and multiplayer, and their replays:
 * a mission or Challenge opponent belongs to its script.  Every input is logic state, so every
 * machine and the replay say GG on the same frame; the chat line is a write to the client.
 */
void AISkirmishPlayer::doSurrender( void )
{
	if( m_player->isPlayerDead() )
		return;

	const UnsignedInt now = TheGameLogic->getFrame();
	if( m_surrenderFrame != 0 )
	{
		if( now >= m_surrenderFrame )
		{
			DEBUG_LOG(( "AI SURRENDER player %d frame %u\n", (Int)m_player->getPlayerIndex(), now ));
			m_player->surrender( TRUE );
		}
		return;
	}

	// Every five seconds, players a few frames apart.
	const UnsignedInt CHECK_FRAMES = 5 * LOGICFRAMES_PER_SECOND;
	if( (now + 7 * (UnsignedInt)m_player->getPlayerIndex()) % CHECK_FRAMES != 0 )
		return;
	// a seat its map scripts may hold no base by design, an Art of Defense GLA's waves
	if( !TheRecorder->isMultiplayer() || !m_player->isPlayableSide() || !runsOwnAI() )
		return;

	// Easy, Medium, Hard.
	static const Int EARLIEST_MINUTE[ DIFFICULTY_COUNT ] = { 6, 8, 10 };	// never before this
	static const Int WORTH_PERCENT[ DIFFICULTY_COUNT ]   = { 20, 12, 8 };	// of the strongest enemy's
	static const Int HOLD_SECONDS[ DIFFICULTY_COUNT ]    = { 30, 45, 60 };	// lost this long before GG
	const Int difficulty = getAIDifficulty();

	if( now < (UnsignedInt)( EARLIEST_MINUTE[ difficulty ] * 60 * LOGICFRAMES_PER_SECOND ) )
		return;

	SurrenderTally mine = { 0, 0 };
	m_player->iterateObjects( tallyForSurrender, &mine );
	mine.value += bankForSurrender( m_player );

	Int64 strongestEnemy = 0;
	for( Int i = 0; i < ThePlayerList->getPlayerCount(); ++i )
	{
		Player *other = ThePlayerList->getNthPlayer( i );
		if( other == m_player || !other->isPlayableSide() || other->isPlayerObserver() )
			continue;
		if( m_player->getRelationship( other->getDefaultTeam() ) != ENEMIES )
			continue;
		if( TheVictoryConditions->hasSinglePlayerBeenDefeated( other ) )
			continue;
		SurrenderTally theirs = { 0, 0 };
		other->iterateObjects( tallyForSurrender, &theirs );
		theirs.value += bankForSurrender( other );
		if( theirs.value > strongestEnemy )
			strongestEnemy = theirs.value;
	}

	const Bool cannotRebuild = !m_player->hasAnyBuildFacility() && mine.builders == 0 && mine.value < strongestEnemy;
	const Bool outweighed = mine.value * 100 < strongestEnemy * WORTH_PERCENT[ difficulty ];
	if( !cannotRebuild && !outweighed )
	{
		m_hopelessSinceFrame = 0;
		return;
	}
	if( m_hopelessSinceFrame == 0 )
	{
		m_hopelessSinceFrame = now;
		return;
	}
	if( now - m_hopelessSinceFrame < (UnsignedInt)( HOLD_SECONDS[ difficulty ] * LOGICFRAMES_PER_SECOND ) )
		return;

	DEBUG_LOG(( "AI GG player %d frame %u worth %lld enemy %lld rebuild %d\n", (Int)m_player->getPlayerIndex(),
		now, (long long)mine.value, (long long)strongestEnemy, (Int)!cannotRebuild ));
	TheInGameUI->chatMessage( m_player, UnicodeString( u"GG" ) );
	m_surrenderFrame = now + 2 * LOGICFRAMES_PER_SECOND;
}

//----------------------------------------------------------------------------------------------------------
/**
 * Adjusts the build list to match the starting position.
 */
void AISkirmishPlayer::adjustBuildList(BuildListInfo *list)
{
	Bool foundStart = false;
	Coord3D startPos;
	
	// Find our command center location.
	Object *obj;
	for( obj = TheGameLogic->getFirstObject(); obj; obj = obj->getNextObject() )
	{	

		Player *owner = obj->getControllingPlayer();
		if (owner==m_player) {
			// See if it's a command center.
			if (obj->isKindOf(KINDOF_COMMANDCENTER)) {
				foundStart = true;
				startPos = *obj->getPosition();
				m_player->onStructureUndone(obj);
				TheAI->pathfinder()->removeObjectFromPathfindMap(obj);
				TheGameLogic->destroyObject(obj);
				break;
			}
		}
	}
	if (!foundStart) {
		DEBUG_LOG(("Couldn't find starting command center for ai player.\n"));
		return;
	}
	// Find the location of the command center in the build list.
	Bool foundInBuildList = false;
	Coord3D buildPos;
	BuildListInfo *cur = list;
	while (cur) {
		const ThingTemplate *tTemplate = TheThingFactory->findTemplate(cur->getTemplateName());
		if (tTemplate && tTemplate->isKindOf(KINDOF_COMMANDCENTER)) {
			foundInBuildList = true;
			buildPos = *cur->getLocation();
			cur->setInitiallyBuilt(true);
		}
		cur = cur->getNext();
	}
	Region3D bounds;
	TheTerrainLogic->getMaximumPathfindExtent(&bounds);
	/* calculate section of 3x3 grid:
		6 7 8
		3 4 5
		0 1 2 */

	Int gridIndex = 0;
	if (startPos.x > bounds.lo.x + bounds.width()/3) {
		gridIndex++;
	}
	if (startPos.x > bounds.lo.x + 2*bounds.width()/3) {
		gridIndex++;
	}

	if (startPos.y > bounds.lo.y + bounds.height()/3) {
		gridIndex+=3;
	}
	if (startPos.y > bounds.lo.y + 2*bounds.height()/3) {
		gridIndex+=3;
	}

	Real baseRotation = 0;
	if (TheAI->getAiData()->m_rotateSkirmishBases) {
		switch (gridIndex) {
			case 0 : baseRotation = 0; break;
			case 1 : baseRotation = PI/4; break;// 45 degrees.
			case 2 : baseRotation = PI/2; break; // 90 degrees;
			case 3 : baseRotation = -PI/4; break; // -45 degrees.
			case 4 : baseRotation = 0; break;
			case 5 : baseRotation = 3*PI/4; break; // 135 degrees.
			case 6 : baseRotation = -PI/2; break; // -90 degrees;
			case 7 : baseRotation = -3*PI/4; break; // -135 degrees.
			case 8 : baseRotation = PI; break; // 180 degrees.
		}
	}

	// the 3*PI/4 is a fixed offset between the build list's authoring frame and the world, so it
	// belongs to the position transform only - baseRotation alone is what the buildings turn by.
	Real angle = baseRotation + 3*PI/4;

	Real s = Sin(angle);
	Real c = Cos(angle);

	// every entry moves with the base, not just the command center. This loop used to carry a
	// condition copy-pasted from the search loop above that tested list->getTemplateName() - the
	// list HEAD - so it was loop-invariant: if the first entry happened to be the command center
	// every entry was transformed, otherwise none was and the base stayed at the INI's absolute
	// coordinates. It also fed setAngle its own angle, so RotateSkirmishBases turned the layout
	// but left every building facing its authored direction.
	if (foundInBuildList) {
		cur = list;
		while (cur) {
			Coord3D curPos = *cur->getLocation();
			// Transform to new coords.
			curPos.x -= buildPos.x;
			curPos.y -= buildPos.y;
			Real newX = curPos.x*c - curPos.y*s;
			Real newY = curPos.y*c + curPos.x*s;
			curPos.x = newX + startPos.x;
			curPos.y = newY + startPos.y;
			cur->setLocation(curPos);
			cur->setAngle(cur->getAngle() + baseRotation);
			cur = cur->getNext();
		}
	}

}



//----------------------------------------------------------------------------------------------------------
/**
 * Find any things that build stuff & add them to the build list.  Then build any initially built
 * buildings.
 */
void AISkirmishPlayer::newMap( void )
{
	// what the setup handed out over the default, which doSpending spends as it would a hoard
	m_startFortune = (Int)m_player->getMoney()->countMoney() - (Int)TheGlobalData->m_defaultStartingCash.countMoney();
	if( m_startFortune < 0 )
		m_startFortune = 0;

	/* Get our proper build list. */
	AsciiString mySide = m_player->getSide();
	DEBUG_LOG(("AI Player side is %s\n", mySide.str()));
	// -modfactions: a mod faction's own mod's list ("M<n>~America", its AIData.ini), retail's of the side without one
	const PlayerTemplate *pt = m_player->getPlayerTemplate();
	const Int mod = pt != NULL ? pt->getModIndex() : -1;	// no template: a map whose faction is gone
	const AISideBuildList *build = TheAI->getAiData()->findBuildList( mySide, mod );
	if (build) {
		BuildListInfo *buildList = build->m_buildList->duplicate();
		// a mod without a list of its own gets retail's, read as the mod's own building of each name
		// where the mod has one.  Before this its command center was torn down below and put back as
		// retail's.
		const Bool retailList = mod >= 0 && build->m_side == mySide;
		for (BuildListInfo *info = buildList; retailList && info != NULL; info = info->getNext())
		{
			AsciiString modName;
			modName.format("M%d~%s", mod + 1, info->getTemplateName().str());
			const ThingTemplate *modPlan = TheThingFactory->findTemplate( modName );
			if (modPlan != NULL)
				info->setTemplateName( modPlan->getName() );
		}
		adjustBuildList(buildList); // adjust to  our start position.
		m_player->setBuildList(buildList);
		computeCenterAndRadiusOfBase(&m_baseCenter, &m_baseRadius);
		DEBUG_LOG(("AI Player build list is %s's\n", build->m_side.str()));
	}
	DEBUG_ASSERTLOG(build!=NULL, ("Couldn't find build list for skirmish player.\n"));

	// Build any with the initially built flag.
	for( BuildListInfo *info = m_player->getBuildList(); info; info = info->getNext() )
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

//----------------------------------------------------------------------------------------------------------
/**
 * Queues up a dozer.
 */
void AISkirmishPlayer::queueDozer( void )
{
	AIPlayer::queueDozer();
}

//----------------------------------------------------------------------------------------------------------
/**
 * Finds a dozer that isn't building or collecting resources.
 */
Object * AISkirmishPlayer::findDozer( const Coord3D *pos )
{
	return AIPlayer::findDozer(pos);
}


//----------------------------------------------------------------------------------------------------------
/**
 * Find a good spot to fire a superweapon.
 */
Bool AISkirmishPlayer::computeSuperweaponTarget(const SpecialPowerTemplate *power, Coord3D *retPos, Int playerNdx, Real weaponRadius)
{

	// what this AI has seen of the target; EA's sees every building, as retail did
	const Int observer = runsOwnAI() ? m_player->getPlayerIndex() : -1;
	Region2D bounds;
	getPlayerStructureBounds(&bounds, playerNdx, FALSE, observer);

	if( power->getSpecialPowerType() == SPECIAL_CLUSTER_MINES || power->getSpecialPowerType() == NUKE_SPECIAL_CLUSTER_MINES )
	{
		// hackus brutus - mine the entrances to our base.
		AsciiString pathLabel;
		Int mode = GameLogicRandomValue(0, 2);
		if (mode==1) {
				pathLabel.format("%s%d", SKIRMISH_FLANK, m_player->getMpStartIndex()+1);
		}	else if (mode==2) {
				pathLabel.format("%s%d", SKIRMISH_BACKDOOR, m_player->getMpStartIndex()+1);
		}	else {
			pathLabel.format("%s%d", SKIRMISH_CENTER, m_player->getMpStartIndex()+1);
		}

		Coord3D goalPos = m_baseCenter;
		Waypoint *way = TheTerrainLogic->getClosestWaypointOnPath( &goalPos, pathLabel );
		if (way) {
			goalPos = *way->getLocation();
		} else {
			if (!influenceMapAttackGoal(&goalPos)) {
				Region2D bounds;
				getPlayerStructureBounds(&bounds, getMyEnemyPlayerIndex(), FALSE, observer);
				goalPos.x = bounds.lo.x + bounds.width()/2;
				goalPos.y = bounds.lo.y + bounds.height()/2;
			}
		}
		Coord2D offset;
		offset.x = goalPos.x-m_baseCenter.x;
		offset.y = goalPos.y-m_baseCenter.y;
		offset.normalize();
		offset.x *= m_baseRadius;
		offset.y *= m_baseRadius;
		*retPos = m_baseCenter;
		retPos->x += offset.x;
		retPos->y += offset.y;
		retPos->z = TheTerrainLogic->getGroundHeight(retPos->x, retPos->y);
		return TRUE;
	}

	return AIPlayer::computeSuperweaponTarget(power, retPos, playerNdx, weaponRadius);

}

// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void AISkirmishPlayer::crc( Xfer *xfer )
{

}  // end crc

// ------------------------------------------------------------------------------------------------
/** Xfer method
	* Version Info;
	* 1: Initial version */
// ------------------------------------------------------------------------------------------------
void AISkirmishPlayer::xfer( Xfer *xfer )
{

	// version
	// 2: the current enemy and the frame to look for one again.  Without them a loaded game picked
	//    its enemy afresh on the first frame and played on differently from the one that was saved.
	// 3: the surrender clock.
	XferVersion currentVersion = 3;
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );

	// xfer base class info
	AIPlayer::xfer( xfer );

	if( version >= 2 )
	{
		xfer->xferUnsignedInt( &m_frameToCheckEnemy );
		Int enemyIndex = m_currentEnemy ? m_currentEnemy->getPlayerIndex() : -1;
		xfer->xferInt( &enemyIndex );
		if( xfer->getXferMode() == XFER_LOAD )
			m_currentEnemy = enemyIndex >= 0 ? ThePlayerList->getNthPlayer( enemyIndex ) : NULL;
	}

	if( version >= 3 )
	{
		xfer->xferUnsignedInt( &m_hopelessSinceFrame );
		xfer->xferUnsignedInt( &m_surrenderFrame );
	}

	// front base defense
	xfer->xferInt( &m_curFrontBaseDefense );

	// flank base defense
	xfer->xferInt( &m_curFlankBaseDefense );

	// front left defense angle
	xfer->xferReal( &m_curFrontLeftDefenseAngle );

	// front right defense angle
	xfer->xferReal( &m_curFrontRightDefenseAngle );

	// left flank left defense angle
	xfer->xferReal( &m_curLeftFlankLeftDefenseAngle );

	// left flank right defense angle
	xfer->xferReal( &m_curLeftFlankRightDefenseAngle );

	// right flank left defense angle
	xfer->xferReal( &m_curRightFlankLeftDefenseAngle );

	// right flank right defense angle
	xfer->xferReal( &m_curRightFlankRightDefenseAngle );

}  // end xfer

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void AISkirmishPlayer::loadPostProcess( void )
{

}  // end loadPostProcess

