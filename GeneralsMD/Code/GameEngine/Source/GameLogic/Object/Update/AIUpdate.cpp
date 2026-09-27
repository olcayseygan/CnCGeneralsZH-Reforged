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

// AIUpdate.cpp //
// Implementation of generic AI mechanisms
// Author: Michael S. Booth, 2001-2002
// Subsequently : John Ahlquist 2002 and a cast of thousands.

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#define DEFINE_LOCOMOTORSET_NAMES					// for TheLocomotorSetNames[]
#define DEFINE_AUTOACQUIRE_NAMES

#include "Common/ActionManager.h"
#include "Common/GameState.h"
#include "Common/CRCDebug.h"
#include "Common/GlobalData.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/RandomValue.h"
#include "Common/Team.h"
#include "Common/ThingFactory.h"
#include "Common/ThingTemplate.h"
#include "Common/TunnelTracker.h"
#include "Common/Upgrade.h"
#include "Common/PerfTimer.h"
#include "Common/UnitTimings.h"
#include "Common/Xfer.h"
#include "Common/XferCRC.h"
#include "Lib/Trig.h"

#include "GameClient/ControlBar.h"
#include "GameClient/Drawable.h"
#include "GameClient/InGameUI.h"  // useful for printing quick debug strings when we need to

#include "GameLogic/AI.h"
#include "GameLogic/AIPathfind.h"
#include "GameLogic/CrowdModel.h"
#include "GameLogic/Locomotor.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/BodyModule.h"
#include "GameLogic/Module/ContainModule.h"
#include "GameLogic/Module/PhysicsUpdate.h"
#include "GameLogic/Module/ProneUpdate.h"
#include "GameLogic/Module/DeliverPayloadAIUpdate.h"
#include "GameLogic/Module/HackInternetAIUpdate.h"
#include "GameLogic/Module/HordeUpdate.h"
#include "GameLogic/Object.h"
#include "GameLogic/PartitionManager.h"
#include "GameLogic/PolygonTrigger.h"
#include "GameLogic/ScriptEngine.h"
#include "GameLogic/TurretAI.h"
#include "GameLogic/Weapon.h"
#include "Common/Radar.h"									// For TheRadar
#include "Lib/FixBoundary.h"

#define SLEEPY_AI

//-------------------------------------------------------------------------------------------------
// The object's place is fixed point; the pathfinder (P5), the locomotor and physics (P4), the
// weapons (P6) and the debug log still take float, and these are where it crosses over.
static inline Coord3D floatPosOf( const Object *obj ) { return obj->getPositionFix()->toCoord3D(); }
/// the footprint for the crowd model (P4), which steers in float
static inline Real floatRadiusOf( const Object *obj ) { return fixToReal( obj->getGeometryInfo().getBoundingCircleRadiusFix() ); }
// the ground under a goal or a path point that is still float (P4/P5), read from the fixed height map
static inline Real groundHeightAt( Real x, Real y )
{
	return fixToReal( TheTerrainLogic->getGroundHeightFix( fixFromReal( x ), fixFromReal( y ) ) );
}
static inline Real layerHeightAt( Real x, Real y, PathfindLayerEnum layer )
{
	return fixToReal( TheTerrainLogic->getLayerHeightFix( fixFromReal( x ), fixFromReal( y ), layer ) );
}

// PartitionManager::getRelativeAngle2D in fixed point: the signed angle from the object's heading to
// pos, in (-PI, PI], and zero when pos is where the object stands.  The atan2 of the cross and dot
// products is the float version's acos of the dot with the cross product's sign.
static Fix relativeAngle2DFix( const Object *obj, const FCoord3D &pos )
{
	const FCoord3D *objPos = obj->getPositionFix();
	const Fix vx = pos.x - objPos->x;
	const Fix vy = pos.y - objPos->y;
	if( vx == Fix( 0 ) && vy == Fix( 0 ) )
		return Fix( 0 );
	const FCoord3D *dir = obj->getUnitDirectionVector2DFix();
	return fixAtan2( dir->x * vy - dir->y * vx, dir->x * vx + dir->y * vy );
}

#ifdef _INTERNAL
// for occasional debugging...
//#pragma optimize("", off)
//#pragma MESSAGE("************************************** WARNING, optimization disabled for debugging purposes")
#endif

//-------------------------------------------------------------------------------------------------
AIUpdateModuleData::AIUpdateModuleData()
{
	//m_locomotorTemplates	-- nothing to do
	for (int i = 0; i < MAX_TURRETS; i++)
		m_turretData[i] = NULL;
	m_autoAcquireEnemiesWhenIdle = 0;
	m_moodAttackCheckRate = LOGICFRAMES_PER_SECOND * 2;
#ifdef ALLOW_SURRENDER
	m_surrenderDuration = LOGICFRAMES_PER_SECOND * 120;
#endif

  m_forbidPlayerCommands = FALSE;
	m_turretsLinked = FALSE;
}

//-------------------------------------------------------------------------------------------------
AIUpdateModuleData::~AIUpdateModuleData()
{
	for (int i = 0; i < MAX_TURRETS; i++)
	{
		if (m_turretData[i])
		{
			TurretAIData* td = const_cast<TurretAIData*>(m_turretData[i]);
			if (td)
				td->deleteInstance();
		}
	}
}

//-------------------------------------------------------------------------------------------------
const LocomotorTemplateVector* AIUpdateModuleData::findLocomotorTemplateVector(LocomotorSetType t) const
{
	if (m_locomotorTemplates.empty())
		return NULL;

  LocomotorTemplateMap::const_iterator it = m_locomotorTemplates.find(t);
  if (it == m_locomotorTemplates.end()) 
	{
		return NULL;
	}
	else
	{
		return &(*it).second;
	}
}

//
// -tracemove [id]: one line a frame for one unit.  A jam is an argument between the speed the unit
// wants, the ceiling its last collision put on it, the decaying bump limit and the frames it has
// spent blocked, and from outside the object none of those is visible - the run-level counters
// can say 40000 blocked frames and still not say which of them zeroed the tank.  The columns are
// in the order the code applies them, so the first one that goes to zero is the culprit.
//
// With no id the trace latches onto the first unit that gets blocked and follows it for the rest of
// the run: in a batch nobody knows an object id in advance, and the interesting unit is by
// definition one that is stuck.  Output only - nothing here is read back by any logic.
//
static ObjectID theTracedObjectID = INVALID_ID;

void AIUpdate_resetMoveTrace( void )
{
	theTracedObjectID = INVALID_ID;
}

static void AIUpdate_traceMove( const Object *obj, Bool blocked, Int blockedFrames,
																Real desiredSpeed, Real maxSpeed, Real maxBlockedSpeed,
																Real bumpSpeedLimit, Bool waitingForPath, Bool hasPath,
																Bool stuck )
{
	const Int wanted = TheGlobalData ? TheGlobalData->m_traceMoveID : 0;
	if (wanted == 0)
		return;

	if (wanted > 0)
	{
		if (obj->getID() != (ObjectID)wanted)
			return;
	}
	else
	{
		// no id given: the first unit to get blocked is the one we follow from then on
		if (theTracedObjectID == INVALID_ID)
		{
			if (!blocked)
				return;
			theTracedObjectID = obj->getID();
		}
		else if (obj->getID() != theTracedObjectID)
		{
			return;
		}
	}

	const Coord3D pos = floatPosOf( obj );
	const PhysicsBehavior *physics = obj->getPhysics();
	const Real actualSpeed = physics ? physics->getVelocityMagnitude() : 0.0f;
	DEBUG_LOG(("MOVETRACE %d,%d,%.2f,%.2f,%.3f,%.3f,%.3f,%.3f,%.3f,%d,%d,%d,%d\n",
		TheGameLogic->getFrame(), (Int)obj->getID(), pos.x, pos.y,
		actualSpeed, desiredSpeed, maxSpeed, maxBlockedSpeed, bumpSpeedLimit,
		blocked ? 1 : 0, blockedFrames, waitingForPath ? 1 : 0,
		hasPath ? 1 : 0));
	if (stuck)
	{
		DEBUG_LOG(("MOVETRACE %d,%d,stuck\n", TheGameLogic->getFrame(), (Int)obj->getID()));
	}
}

//-------------------------------------------------------------------------------------------------
/*static*/ void AIUpdateModuleData::buildFieldParse(MultiIniFieldParse& p)
{
  ModuleData::buildFieldParse(p);

	static const FieldParse dataFieldParse[] = 
	{
		{ "Turret",											AIUpdateModuleData::parseTurret,	NULL, offsetof(AIUpdateModuleData, m_turretData[0]) },
		{ "AltTurret",									AIUpdateModuleData::parseTurret,	NULL, offsetof(AIUpdateModuleData, m_turretData[1]) },
		{ "AutoAcquireEnemiesWhenIdle", INI::parseBitString32, TheAutoAcquireEnemiesNames, offsetof(AIUpdateModuleData, m_autoAcquireEnemiesWhenIdle) },
		{ "MoodAttackCheckRate",				INI::parseDurationUnsignedInt,		NULL, offsetof(AIUpdateModuleData, m_moodAttackCheckRate) },
#ifdef ALLOW_SURRENDER
		{ "SurrenderDuration",					INI::parseDurationUnsignedInt,		NULL, offsetof(AIUpdateModuleData, m_surrenderDuration) },
#endif
    { "ForbidPlayerCommands",				INI::parseBool,										NULL, offsetof(AIUpdateModuleData, m_forbidPlayerCommands) },
    { "TurretsLinked",							INI::parseBool,										NULL, offsetof( AIUpdateModuleData, m_turretsLinked ) },
		{ 0, 0, 0, 0 }
	};
  p.add(dataFieldParse);
}

//-------------------------------------------------------------------------------------------------
/*static*/ void AIUpdateModuleData::parseTurret(INI* ini, void *instance, void * store, const void* /*userData*/)
{
	if (*(TurretAIData**)store)
	{
		DEBUG_CRASH(("Only one turret to a customer, for now"));
		throw INI_INVALID_DATA;
	}

	TurretAIData* td = newInstance(TurretAIData);
	ini->initFromINIMultiProc(td, td->buildFieldParse);
	*(TurretAIData**)store = td;
}

//-------------------------------------------------------------------------------------------------
/*static*/ void AIUpdateModuleData::parseLocomotorSet(INI* ini, void *instance, void * /*store*/, const void* /*userData*/)
{
	ThingTemplate *tt = (ThingTemplate *)instance;
	AIUpdateModuleData *self = tt->friend_getAIModuleInfo();
	if (!self) 
	{
		DEBUG_CRASH( ("Attempted to specify a locomotor for object %s without an AIUpdate block.", tt->getName().str() ) );
		throw INI_INVALID_DATA;
	}

	LocomotorSetType set = (LocomotorSetType)INI::scanIndexList(ini->getNextToken(), TheLocomotorSetNames);
	if (!self->m_locomotorTemplates[set].empty())
	{
		if (ini->getLoadType() != INI_LOAD_CREATE_OVERRIDES)
		{
			DEBUG_CRASH(("re-specifying a LocomotorSet is no longer allowed\n"));
			throw INI_INVALID_DATA;
		}
	}

	self->m_locomotorTemplates[set].clear();
	for (const char* locoName = ini->getNextToken(); locoName; locoName = ini->getNextTokenOrNull())
	{
		if (!*locoName || !stricmp(locoName, "None"))
			continue;

		NameKeyType locoKey = NAMEKEY(locoName);
		const LocomotorTemplate* lt = TheLocomotorStore->findLocomotorTemplate(locoKey);
		if (!lt)
		{
			DEBUG_CRASH(("Locomotor %s not found!\n",locoName));
			throw INI_INVALID_DATA;
		}
		self->m_locomotorTemplates[set].push_back(lt);
	}
}

//-------------------------------------------------------------------------------------------------
// subclasses may want to override this, to use a subclass of AIStateMachine.
AIStateMachine* AIUpdateInterface::makeStateMachine()
{
	return newInstance(AIStateMachine)( getObject(), "AIUpdateInterfaceMachine");
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
AIUpdateInterface::AIUpdateInterface( Thing *thing, const ModuleData* moduleData ) : 
	UpdateModule( thing, moduleData )
{
	int i;

	m_priorWaypointID = 0xfacade;
	m_currentWaypointID	= 0xfacade;
	m_stateMachine = NULL;
	m_nextEnemyScanTime = 0;
	m_currentVictimID = INVALID_ID;
	m_desiredSpeed = FAST_AS_POSSIBLE;
	m_lastCommandSource = CMD_FROM_AI;
	m_guardMode = GUARDMODE_NORMAL;
	m_guardTargetType[0] = m_guardTargetType[1] = GUARDTARGET_NONE;
	m_locationToGuard.zero();
	m_objectToGuard = INVALID_ID;
	m_areaToGuard = NULL;
	m_attackInfo = NULL;
	m_waypointCount = 0;
	m_waypointIndex = 0;
	m_completedWaypoint = NULL;
	m_path = NULL;
	m_requestedVictimID = INVALID_ID;
	m_requestedDestination.zero();
	m_requestedDestination2.zero();
	m_pathTimestamp = 0;
	m_ignoreObstacleID = INVALID_ID;
	m_pathExtraDistance = 0;
	m_pathfindGoalCell.x = m_pathfindGoalCell.y = -1;
	m_pathfindCurCell.x = m_pathfindCurCell.y = -1;
	m_blockedFrames = 0;
	m_curMaxBlockedSpeed = 0;
	m_laneFraction = 0.5f;
	m_laneFractionValid = FALSE;
	m_laneHoldFrame = 0;
	m_pendingLane = 0.5f;
	m_hasPendingLane = FALSE;
	m_corridor = NULL;
	m_crowdLat = 0.0f;
	m_pendingCrowdLat = 0.0f;
	m_hasPendingCrowdLat = FALSE;
	m_crowdLaneIdx = 0;
	m_crowdLaneOf = 0;
	m_crowdLaneSpace = 0.0f;
	m_crowdFit = 0;
	m_crowdLatValid = FALSE;
	m_crowdHoldFrame = 0;
	m_crowdSample = 0;
	m_crowdQueued = 0;
	m_crowdSide = 1;
	m_crowdSepSmooth = 0.0f;
	m_crowdCap = 0.0f;
	m_crowdCapValid = FALSE;
	m_crowdAim = 0.0f;
	m_crowdAimValid = FALSE;
	m_noProgress = 0;
	m_headOnFrames = 0;
	m_headOnSeen = FALSE;
	m_lastProgressPos.zero();
	m_lastProgressAngle = 0.0f;
	m_ditherFrom.zero();
	m_ditherFrame = 0;
	m_ditherTravel = 0.0f;
	m_rescueStage = 0;
	m_rescueTo.zero();
	m_rescueUntil = 0;
	m_rescueCool = 0;
	m_repathAsked = FALSE;
	m_bumpSpeedLimit = FAST_AS_POSSIBLE;
	m_ignoreCollisionsUntil = 0;
	m_queueForPathFrame = 0;
	m_finalPosition.zero();
	m_repulsor1 = INVALID_ID;
	m_repulsor2 = INVALID_ID;
	m_nextGoalPathIndex = -1;
	m_moveOutOfWay1 = INVALID_ID;
	m_moveOutOfWay2 = INVALID_ID;
	m_exitProductionRallyPoint.zero();
	m_hasExitProductionRallyPoint = FALSE;
	m_salvageReturnPosition.zero();
	m_hasSalvageReturnPosition = FALSE;
	m_tunnelTripGoal.zero();
	m_hasTunnelTrip = FALSE;
	m_tunnelTripEnd = TUNNEL_TRIP_MOVE;
	m_locomotorSet.clear();
	m_curLocomotor = NULL;
	m_curLocomotorSet = LOCOMOTORSET_INVALID;
	m_locomotorGoalType = NONE;
	m_locomotorGoalData.zero();
	for (i = 0; i < MAX_TURRETS; i++)
		m_turretAI[i] = NULL;
	m_turretSyncFlag = TURRET_INVALID;
	m_attitude = AI_NORMAL;
	m_nextMoodCheckTime = 0;
#ifdef ALLOW_DEMORALIZE
	m_demoralizedFramesLeft = 0;
#endif
#ifdef ALLOW_SURRENDER
	m_surrenderedFramesLeft = 0;
	m_surrenderedPlayerIndex = -1;
#endif
	m_crateCreated = INVALID_ID;
	m_tmpInt = 0;
	m_doFinalPosition = FALSE;
	m_waitingForPath = FALSE;
	m_isAttackPath = FALSE;
	m_isFinalGoal = FALSE;
	m_isApproachPath = FALSE;
	m_isSafePath = FALSE;
	m_movementComplete = FALSE;
	m_isMoving = FALSE;
	m_isBlocked = FALSE;
	m_isBlockedAndStuck = FALSE;
	m_upgradedLocomotors = FALSE;
	m_canPathThroughUnits = FALSE;
	m_randomlyOffsetMoodCheck = FALSE;
	m_isAiDead = FALSE;
	m_isRecruitable = TRUE; // Things default to being recruitable.
	m_executingWaypointQueue = FALSE;
	// Never initialised at all before: a human player's unit read whatever was in the heap to
	// decide whether it chases something it auto-acquired, so the same order gave two answers on
	// two machines. FALSE is the rule the approach state is written around; a deploy-and-attack
	// state or an attack move turns it on for as long as it lasts.
	m_allowedToChase = FALSE;
	m_retryPath = FALSE;
	m_pathfindFoundNothing = FALSE;
	m_isInUpdate = FALSE;
	m_fixLocoInPostProcess = FALSE;

	// ---------------------------------------------

	for (i = 0; i < MAX_TURRETS; i++)
	{
		if (getAIUpdateModuleData()->m_turretData[i])
		{
			m_turretAI[i] = newInstance(TurretAI)(getObject(), getAIUpdateModuleData()->m_turretData[i], (WhichTurretType)i);
		}
	}

	chooseLocomotorSet(LOCOMOTORSET_NORMAL);

#ifdef SLEEPY_AI
	setWakeFrame(getObject(), UPDATE_SLEEP_NONE);
#endif
}

#ifdef ALLOW_SURRENDER
//=============================================================================
// Object::setSurrendered, and related methods ================================
//=============================================================================
void AIUpdateInterface::setSurrendered( const Object *objWeSurrenderedTo, Bool surrendered )
{
	if (surrendered)
	{
		Bool wasSurrendered = isSurrendered();

		const AIUpdateModuleData* d = getAIUpdateModuleData();

		if (m_surrenderedFramesLeft < d->m_surrenderDuration)
			m_surrenderedFramesLeft = d->m_surrenderDuration;
		
		const Player* playerWeSurrenderedTo = objWeSurrenderedTo ? objWeSurrenderedTo->getControllingPlayer() : NULL;
		m_surrenderedPlayerIndex = playerWeSurrenderedTo ? playerWeSurrenderedTo->getPlayerIndex() : -1;

		if (!wasSurrendered)
		{
			//aiIdle(CMD_FROM_AI);
			// srj sez: calling aiIdle() won't work, since we are probably "effectivelyDead"...
			// meaning we won't respong to aiDoCommand! so go straight to the metal here:
			getStateMachine()->clear();
			getStateMachine()->setState( AI_IDLE );
			setLastCommandSource(CMD_FROM_AI);

			// Play our sound surrendered
			AudioEventRTS surrenderSound = *getObject()->getTemplate()->getVoiceSurrender();
			surrenderSound.setObjectID(getObject()->getID());
			TheAudio->addAudioEvent(&surrenderSound);		
		}
	}
	else
	{
		// GS During the act of surrendering, we dipped to 0 and then were manually set to have hit points.  
		// That made us alive but marked as Dead.  Gotta undo that.

		getObject()->setEffectivelyDead( FALSE );

		m_surrenderedFramesLeft = 0;
		m_surrenderedPlayerIndex = -1;
	}

}
#endif

//=============================================================================
void AIUpdateInterface::setGoalPositionClipped(const Coord3D* in, CommandSourceType cmdSource)
{
	if (in)
	{
		Coord3D tmp  = *in;
		if (cmdSource == CMD_FROM_PLAYER)
		{
			Real fudge = TheGlobalData->m_partitionCellSize * 0.5f;
			if (getObject()->isKindOf(KINDOF_AIRCRAFT) && getObject()->isSignificantlyAboveTerrain() && m_curLocomotor != NULL)
			{
				// aircraft must stay further away from the map edges, to prevent getting "lost"
				fudge = max(fudge, m_curLocomotor->getPreferredHeight());
			}
			Region3D mapRegion;
			TheTerrainLogic->getExtent( &mapRegion );
			if (tmp.x < mapRegion.lo.x + fudge)
			{
				tmp.x = mapRegion.lo.x + fudge;
			}
			if (tmp.x > mapRegion.hi.x - fudge)
			{
				tmp.x = mapRegion.hi.x - fudge;
			}
			if (tmp.y < mapRegion.lo.y + fudge)
			{
				tmp.y = mapRegion.lo.y + fudge;
			}
			if (tmp.y > mapRegion.hi.y - fudge)
			{
				tmp.y = mapRegion.hi.y - fudge;
			}
		}
		getStateMachine()->setGoalPosition(&tmp);
	}
	else
	{
		getStateMachine()->setGoalPosition(NULL);
	}
}

/* Called by the pathfinder when it processes the pathfind queue.  Basically, it's our turn
to call use the PathfindServicesInterface to do a pathfind operation.  This shouldn't be called
(and in fact is very hard to do because PathfindServicesInterace is private to the pathfinder)
except by the pathfinder during pathfind queue processing.  jba */
//-------------------------------------------------------------------------------------------------
void AIUpdateInterface::doPathfind( PathfindServicesInterface *pathfinder )
{
	if (!m_waitingForPath) {
		return;
	}
	//CRCDEBUG_LOG(("AIUpdateInterface::doPathfind() for object %d\n", getObject()->getID()));
	m_waitingForPath = FALSE;
	if (m_isSafePath) {
		destroyPath();
		Coord3D pos1, pos2;
		pos1.set(-1000,-1000,0);
		Object *repulsor = TheGameLogic->findObjectByID(m_repulsor1);
		if (repulsor) {
			pos1 = floatPosOf(repulsor);
		}
		pos2 = pos1;
		repulsor = TheGameLogic->findObjectByID(m_repulsor2);
		if (repulsor) {
			pos2 = floatPosOf(repulsor);
		}
		const Coord3D myPos = floatPosOf(getObject());
		m_path = pathfinder->findSafePath(getObject(), m_locomotorSet,
			&myPos,
			&pos1, 	&pos2, 
			getObject()->getVisionRange() + TheAI->getAiData()->m_repulsedDistance);
		m_pathfindFoundNothing = (m_path == NULL);
		return;
	}
	if (m_isApproachPath & !isDoingGroundMovement()) {
		m_isApproachPath = false;
	}
	if (m_isApproachPath) {
		destroyPath();
		const Coord3D myPos = floatPosOf(getObject());
		m_path = pathfinder->findClosestPath(getObject(), m_locomotorSet, &myPos,
			&m_requestedDestination, m_isBlockedAndStuck, 0.2f, FALSE );
		m_pathfindFoundNothing = (m_path == NULL);
		if (isDoingGroundMovement() && getPath()) {
			TheAI->pathfinder()->updateGoal(getObject(), getPath()->getLastNode()->getPosition(),
				getPath()->getLastNode()->getLayer());
		}
		return;
	}
	if (m_isAttackPath) {
		Object *victim = NULL;
		if (m_requestedVictimID != INVALID_ID) { 
			victim = TheGameLogic->findObjectByID(m_requestedVictimID);
		}
		if (computeAttackPath(pathfinder, victim, &m_requestedDestination))	{
			// in range already comes back with no path, and the approach state still ends on that
			m_pathfindFoundNothing = (m_path == NULL);
			if (getPath()) {
				TheAI->pathfinder()->updateGoal(getObject(), getPath()->getLastNode()->getPosition(),
					getPath()->getLastNode()->getLayer());
			}
			//CRCDEBUG_LOG(("AIUpdateInterface::doPathfind() - m_isAttackPath = TRUE after computeAttackPath\n"));
			m_isAttackPath = TRUE; 
			return;
		}
		//CRCDEBUG_LOG(("AIUpdateInterface::doPathfind() - m_isAttackPath = FALSE after computeAttackPath()\n"));
		m_isAttackPath = FALSE;
		if (victim) {
			m_requestedDestination = floatPosOf(victim);
			/* find a pathable destination near the victim.*/
			TheAI->pathfinder()->adjustToPossibleDestination(getObject(), getLocomotorSet(), &m_requestedDestination);
			ignoreObstacle(victim); 
		}
	} 
	computePath(pathfinder, &m_requestedDestination);
	if (m_isFinalGoal && isDoingGroundMovement() && getPath()) {
		TheAI->pathfinder()->updateGoal(getObject(), getPath()->getLastNode()->getPosition(),
			getPath()->getLastNode()->getLayer());
	}
	if (m_queueForPathFrame > TheGameLogic->getFrame()) {
		m_waitingForPath = TRUE;
	}
#ifdef SLEEPY_AI
	// if we're no longer waiting for a path, make sure we wake up right away!
	if (!m_waitingForPath)
	{
		wakeUpNow();
	}
#endif
}

/* Requests a path to be found.  Note that if it is possible to do it without having to use the 
pathfinder (air units just move point to point) it generates the path immediately.  Otherwise the path
will be processed when we get to the front of the pathfind queue. jba */
//-------------------------------------------------------------------------------------------------
void AIUpdateInterface::requestPath( Coord3D *destination, Bool isFinalGoal ) 
{

	if (m_locomotorSet.getValidSurfaces() == 0) {
		DEBUG_CRASH(("Attempting to path immobile unit."));
	}

	//DEBUG_LOG(("Request Frame %d, obj %s %x\n", TheGameLogic->getFrame(), getObject()->getTemplate()->getName().str(), getObject()));
	m_requestedDestination = *destination;
	m_isFinalGoal = isFinalGoal;
	CRCDEBUG_LOG(("AIUpdateInterface::requestPath() - m_isAttackPath = FALSE for object %d\n", getObject()->getID()));
	m_isAttackPath = FALSE;	
	m_requestedVictimID = INVALID_ID;	
	m_isApproachPath = FALSE;
	m_isSafePath = FALSE;
	if (canComputeQuickPath()) {
		computeQuickPath(destination);
		return;
	}
	m_waitingForPath = TRUE;
	if (m_pathTimestamp > TheGameLogic->getFrame()-3) {
		/* Requesting path very quickly.  Can cause a spin. The spin needs a gap between two
			 paths, not the one or two seconds EA waited here, which left a unit that had just been
			 given a second order standing still or walking the old path until the timer ran out. So
			 the request waits out the rest of the three frames and no longer. */
		setQueueForPathTime(repathDebounceFrames());
		// See if it has been too soon.
		// jba intense debug
		//DEBUG_LOG(("Info - RePathing very quickly %d, %d.\n", m_pathTimestamp, TheGameLogic->getFrame()));
		if (m_path && m_isBlockedAndStuck) {
			setIgnoreCollisionTime(2*LOGICFRAMES_PER_SECOND);
			m_blockedFrames = 0;
			m_isBlocked = FALSE;
			m_isBlockedAndStuck = FALSE;
		}
		return;
	}
	queueForPathOrRetry();

}

//-------------------------------------------------------------------------------------------------
void AIUpdateInterface::requestAttackPath( ObjectID victimID, const Coord3D* victimPos ) 
{
	if (m_locomotorSet.getValidSurfaces() == 0) {
		DEBUG_CRASH(("Attempting to path immobile unit."));
	}
	CRCDEBUG_LOG(("AIUpdateInterface::requestAttackPath() - m_isAttackPath = TRUE for object %d\n", getObject()->getID()));
	m_requestedDestination = *victimPos;
	m_requestedVictimID = victimID;	
	m_isAttackPath = TRUE;
	m_isApproachPath = FALSE;
	m_isSafePath = FALSE;
	m_waitingForPath = TRUE;
	if (m_pathTimestamp > TheGameLogic->getFrame()-3) {
		/* Requesting path very quickly.  Can cause a spin. */
		setQueueForPathTime(repathDebounceFrames());
		setLocomotorGoalNone();
		return;
	}
	queueForPathOrRetry();
}

//-------------------------------------------------------------------------------------------------
void AIUpdateInterface::requestApproachPath( Coord3D *destination ) 
{
	if (m_locomotorSet.getValidSurfaces() == 0) {
		DEBUG_CRASH(("Attempting to path immobile unit."));
	}
	m_requestedDestination = *destination;
	m_isFinalGoal = TRUE;
	CRCDEBUG_LOG(("AIUpdateInterface::requestApproachPath() - m_isAttackPath = FALSE for object %d\n", getObject()->getID()));
	m_isAttackPath = FALSE;	
	m_requestedVictimID = INVALID_ID;	
	m_isApproachPath = TRUE;
	m_isSafePath = FALSE;
	m_waitingForPath = TRUE;
	if (m_pathTimestamp > TheGameLogic->getFrame()-3) {
		/* Requesting path very quickly.  Can cause a spin. */
		setQueueForPathTime(repathDebounceFrames());
		return;
	}
	queueForPathOrRetry();
}

//-------------------------------------------------------------------------------------------------
// Requests a safe path away from the repulsor.
void AIUpdateInterface::requestSafePath( ObjectID repulsor ) 
{
	if (repulsor != m_repulsor1) {
		m_repulsor2 = m_repulsor1; // save the prior repulsor.
	}
	m_repulsor1 = repulsor;	
	m_isFinalGoal = FALSE;
	CRCDEBUG_LOG(("AIUpdateInterface::requestSafePath() - m_isAttackPath = FALSE for object %d\n", getObject()->getID()));
	m_isAttackPath = FALSE;	
	m_requestedVictimID = INVALID_ID;	
	m_isApproachPath = FALSE;
	m_isSafePath = TRUE;
	m_waitingForPath = TRUE;
	if (m_pathTimestamp > TheGameLogic->getFrame()-3) {
		/* Requesting path very quickly.  Can cause a spin. */
		setQueueForPathTime(repathDebounceFrames());
		return;
	}
	queueForPathOrRetry();
}

enum {WAYPOINT_PATH_LIMIT=1024};
//-------------------------------------------------------------------------------------------------
// 
void AIUpdateInterface::setPathFromWaypoint(const Waypoint *way, const Coord2D *offset) 
{
	destroyPath();
	m_path = newInstance(Path);
	Coord3D pos = floatPosOf(getObject());	// P5: path nodes are float
	m_path->prependNode( &pos, LAYER_GROUND );
	m_path->markOptimized();
	int count = 0;
	while (way) {
		Coord3D wayPos = *way->getLocation();
		wayPos.x += offset->x;
		wayPos.y += offset->y;
		if (way->getLink(0) == NULL) {
			TheAI->pathfinder()->snapPosition(getObject(), &wayPos);
		}
		m_path->appendNode( &wayPos, LAYER_GROUND );
		way = way->getLink(0);
		count++;
		if (count>WAYPOINT_PATH_LIMIT) break;
	}
	m_waitingForPath = FALSE;	 
	TheAI->pathfinder()->setDebugPath(m_path);
#ifdef SLEEPY_AI
	// if we're no longer waiting for a path, make sure we wake up right away!
	wakeUpNow();
#endif
}

//-------------------------------------------------------------------------------------------------
void AIUpdateInterface::onObjectCreated()
{
	// create the behavior state machine.
	// can't do this in the ctor because makeStateMachine is a protected virtual func,
	// and overrides to virtual funcs don't exist in our ctor. (look it up.)
	if (m_stateMachine == NULL)
	{
		m_stateMachine = makeStateMachine();
		m_stateMachine->initDefaultState();
	}
}

//-------------------------------------------------------------------------------------------------
AIUpdateInterface::~AIUpdateInterface( void )
{
	m_locomotorSet.clear();
	m_curLocomotor = NULL;

	if( m_stateMachine ) {
		m_stateMachine->halt();
		m_stateMachine->deleteInstance();
	}

	for (int i = 0; i < MAX_TURRETS; i++)
	{
		if (m_turretAI[i])
			m_turretAI[i]->deleteInstance();
		m_turretAI[i] = NULL;
	}
	m_stateMachine = NULL;

	// destroy the current path. (destroyPath is NULL savvy)
	destroyPath();

}

//=============================================================================
void AIUpdateInterface::setTurretTargetObject(WhichTurretType tur, Object* o, Bool forceAttacking)
{
	if (m_turretAI[tur])
	{
		m_turretAI[tur]->setTurretTargetObject(o, forceAttacking);
	}
}

//=============================================================================
Object* AIUpdateInterface::getTurretTargetObject( WhichTurretType tur, Bool clearDeadTargets )
{
	if( m_turretAI[ tur ] )
	{
		Object *obj;
		Coord3D pos;
		if( m_turretAI[ tur ]->friend_getTurretTarget( obj, pos, clearDeadTargets ) == TARGET_OBJECT )
		{
			return obj;
		}
	}
	return NULL;
}

//=============================================================================
void AIUpdateInterface::setTurretTargetPosition(WhichTurretType tur, const Coord3D* pos)
{
	if (m_turretAI[tur])
	{
		m_turretAI[tur]->setTurretTargetPosition(pos);
	}
}

//=============================================================================
void AIUpdateInterface::setTurretEnabled(WhichTurretType tur, Bool enabled)
{
	if (m_turretAI[tur])
	{
		m_turretAI[tur]->setTurretEnabled( enabled );
	}
}

//=============================================================================
void AIUpdateInterface::recenterTurret(WhichTurretType tur)
{
	if (m_turretAI[tur])
	{
		m_turretAI[tur]->recenterTurret();
	}
}

//=============================================================================
void AIUpdateInterface::stopTurretsTurning()
{
	for (Int i = 0; i < MAX_TURRETS; ++i)
	{
		if (m_turretAI[i])
		{
			m_turretAI[i]->stopTurning();
		}
	}
}

//=============================================================================
Bool AIUpdateInterface::isTurretEnabled( WhichTurretType tur ) const
{
	if( m_turretAI[ tur ] )
	{
		return m_turretAI[ tur ]->isTurretEnabled();
	}
	return FALSE;
}

//=============================================================================
Bool AIUpdateInterface::isTurretInNaturalPosition(WhichTurretType tur) const
{
	if (m_turretAI[tur])
	{
		return m_turretAI[tur]->isTurretInNaturalPosition();
	}
	return FALSE;
}

//=============================================================================
Bool AIUpdateInterface::isWeaponSlotOnTurretAndAimingAtTarget(WeaponSlotType wslot, const Object* victim) const
{
	for (int i = 0; i < MAX_TURRETS; i++)
	{
		if (m_turretAI[i] && m_turretAI[i]->isWeaponSlotOnTurret(wslot))
		{
			return m_turretAI[i]->isTryingToAimAtTarget(victim);
		}
	}
	return FALSE;
}

//=============================================================================
Bool AIUpdateInterface::getTurretRotAndPitch(WhichTurretType tur, Real* turretAngle, Real* turretPitch) const
{
	if (m_turretAI[tur])
	{
		if (turretAngle)
			*turretAngle = m_turretAI[tur]->getTurretAngle();
		if (turretPitch)
			*turretPitch = m_turretAI[tur]->getTurretPitch();
		return TRUE;
	}
	return FALSE;
}

//=============================================================================
Real AIUpdateInterface::getTurretTurnRate(WhichTurretType tur) const
{
	return (tur != TURRET_INVALID && m_turretAI[tur] != NULL) ?
					m_turretAI[tur]->getTurnRate() :
					0.0f;
}

//=============================================================================
WhichTurretType AIUpdateInterface::getWhichTurretForCurWeapon() const
{
	for (int i = 0; i < MAX_TURRETS; ++i)
		if (m_turretAI[i] && m_turretAI[i]->isOwnersCurWeaponOnTurret())
			return (WhichTurretType)i;

	return TURRET_INVALID;
}

//=============================================================================
WhichTurretType AIUpdateInterface::getWhichTurretForWeaponSlot(WeaponSlotType wslot, Real* turretAngle, Real* turretPitch) const
{
	for (int i = 0; i < MAX_TURRETS; ++i)
	{
		if (m_turretAI[i] && m_turretAI[i]->isWeaponSlotOnTurret(wslot))
		{
			if (turretAngle)
				*turretAngle = m_turretAI[i]->getTurretAngle();
			if (turretPitch)
				*turretPitch = m_turretAI[i]->getTurretPitch();

			return (WhichTurretType)i;
		}
	}
	return TURRET_INVALID;
}

//=============================================================================
Real AIUpdateInterface::getCurLocomotorSpeed() const
{
	if (m_curLocomotor != NULL)
		return m_curLocomotor->getMaxSpeedForCondition(getObject()->getBodyModule()->getDamageState());

	DEBUG_LOG(("no current locomotor!"));
	return 0.0f;
}

//=============================================================================
void AIUpdateInterface::setLocomotorUpgrade(Bool set)
{
	m_upgradedLocomotors = set;
	if (m_curLocomotorSet == LOCOMOTORSET_NORMAL || m_curLocomotorSet == LOCOMOTORSET_NORMAL_UPGRADED)
		chooseLocomotorSet(LOCOMOTORSET_NORMAL);
}

//=============================================================================
Bool AIUpdateInterface::chooseLocomotorSet(LocomotorSetType wst)
{
	DEBUG_ASSERTCRASH(wst != LOCOMOTORSET_NORMAL_UPGRADED, ("never pass LOCOMOTORSET_NORMAL_UPGRADED here"));
	if (wst == LOCOMOTORSET_NORMAL && m_upgradedLocomotors)
		wst = LOCOMOTORSET_NORMAL_UPGRADED;

	if (wst == m_curLocomotorSet)
		return TRUE;

	if (chooseLocomotorSetExplicit(wst))
	{
		chooseGoodLocomotorFromCurrentSet();
		return TRUE;
	}

	return FALSE;
}

//=============================================================================
// this should only be called by load/save, or by chooseLocomotorSet.
// it does no sanity checking; it just jams it in.
Bool AIUpdateInterface::chooseLocomotorSetExplicit(LocomotorSetType wst)
{
	const LocomotorTemplateVector* set = getAIUpdateModuleData()->findLocomotorTemplateVector(wst);
	if (set)
	{
		// A dying aircraft keeps the locomotor it has. JetSlowDeathBehavior grounds a wreck by taking the
		// lift and the turn rate off the current Locomotor instance, and a rebuild from the templates
		// (the jet's attack or return set timing out, say) hands the wreck full lift back and it flies
		// on. LOCOMOTORSET_INVALID is a set not built yet, which loadPostProcess still has to build.
		const Object* obj = getObject();
		if (m_curLocomotorSet != LOCOMOTORSET_INVALID && obj->isEffectivelyDead() && obj->isKindOf(KINDOF_AIRCRAFT))
			return FALSE;

		m_locomotorSet.clear();
		m_curLocomotor = NULL;
		for (Int i = 0; i < set->size(); ++i)
		{
			const LocomotorTemplate* lt = set->at(i);
			if (lt)
				m_locomotorSet.addLocomotor(lt);
		}
		m_curLocomotorSet = wst;
		return TRUE;
	}
	return FALSE;
}

//-------------------------------------------------------------------------------------------------
void AIUpdateInterface::chooseGoodLocomotorFromCurrentSet( void )
{
	Locomotor* prevLoco = m_curLocomotor;

	const Coord3D myPos = floatPosOf(getObject());	// P5
	Locomotor* newLoco = TheAI->pathfinder()->chooseBestLocomotorForPosition(getObject()->getLayer(), &m_locomotorSet, &myPos);

	if (newLoco == NULL)
	{
		if (prevLoco != NULL)
		{
		/* due to physics, we might slight into a cell for which we have no loco
			(eg, cliff) and get stuck. this is bad. as a solution, we do this.
			this may look a little funny, but as a practical matter, it works well, 
			since the pathfinder will prevent us from doing any significant "wrong" terrain. */
			newLoco = prevLoco;
		}
		else
		{
			/* this can happen for a newly-created object, which might come into being in 
				the middle of an obstacle. for now, we just fake it and choose a ground locomotor. */
			newLoco = m_locomotorSet.findLocomotor(LOCOMOTORSURFACE_GROUND);
		}
	}

	m_curLocomotor = newLoco;

	if (prevLoco != m_curLocomotor)
	{
		// make sure the group's speed will be recalculated
		if (getGroup())
			getGroup()->recomputeGroupSpeed();

		// turn off precision-z-pos anytime the loco changes, just in case,
		// since it should only be enabled in very special cases
		m_curLocomotor->setUsePreciseZPos(FALSE);
		// ditto for no-slow-down.
		m_curLocomotor->setNoSlowDownAsApproachingDest(FALSE);
		// ditto for ultra-accuracy.
		m_curLocomotor->setUltraAccurate(FALSE);
	}
}

//----------------------------------------------------------------------------------------------------------
Object* AIUpdateInterface::checkForCrateToPickup()
{
	if (m_crateCreated != INVALID_ID) 
	{
		Object* crate = TheGameLogic->findObjectByID(m_crateCreated);
		m_crateCreated = INVALID_ID; // we have processed it, so clear it.
		if (crate) 
		{
			for (BehaviorModule** m = crate->getBehaviorModules(); *m; ++m)
			{
				CollideModuleInterface* collide = (*m)->getCollide();
				if (!collide)
					continue;

				if( collide->wouldLikeToCollideWith(getObject()))
				{
					return crate;
				}
			}
		}
	}
	return NULL;
}

#ifdef ALLOW_SURRENDER
//-------------------------------------------------------------------------------------------------
void AIUpdateInterface::doSurrenderUpdateStuff()
{
	RELEASE_CRASH(("Read the comment in doSurrenderUpdateStuff"));

	/*
		If you ever re-enable this code, you must convert it to be 
		properly sleepy. It is crucial that we avoid requiring a call
		to AIUpdate every frame just to support this. (srj)
	*/

	UnsignedInt prevSurrenderedFrames = m_surrenderedFramesLeft;

	if (m_surrenderedFramesLeft > 0)
		--m_surrenderedFramesLeft;

	if (m_surrenderedFramesLeft > 0)
		getObject()->setModelConditionState( MODELCONDITION_SURRENDER );
	else
		getObject()->clearModelConditionState( MODELCONDITION_SURRENDER );

	//
	// when we leave a surrendered state we give ourselves an idle command ... why you ask? Well
	// during the surrender sequence we might have started moving towards a POW truck come
	// to pick us up, but now we should stop and be all normal again
	//
	if( prevSurrenderedFrames > 0 && m_surrenderedFramesLeft == 0 )
	{
		m_surrenderedPlayerIndex = -1;
		aiIdle( CMD_FROM_AI );
	}
}
#endif

//-------------------------------------------------------------------------------------------------
void AIUpdateInterface::setQueueForPathTime(Int frames)
{
#ifdef SLEEPY_AI
	if (frames >= UPDATE_SLEEP_NONE && getWakeFrame() > UPDATE_SLEEP(frames))
	{
		if (m_isInUpdate)
		{
			// we're changing this while in our own update (probably via a move state).
			// just do nothing, since update will calculate the correct sleep behavior at the end.
		}
		else
		{
			setWakeFrame(getObject(), UPDATE_SLEEP(frames));
		}
	}
#endif
	m_queueForPathFrame = frames ? (TheGameLogic->getFrame() + frames) : 0;
}

//-------------------------------------------------------------------------------------------------
/* A full pathfind queue refuses the request, and every caller used to carry on waiting for a path
	 nobody was going to find: the unit stood there for the rest of the match. Refused, it asks
	 again half a second later through the same timer a too-quick repath uses. */
void AIUpdateInterface::queueForPathOrRetry()
{
	if (!TheAI->pathfinder()->queueForPath(getObject()->getID()))
		setQueueForPathTime(LOGICFRAMES_PER_SECOND / 2);
}

//-------------------------------------------------------------------------------------------------
void AIUpdateInterface::wakeUpNow()
{
#ifdef SLEEPY_AI
	if (getWakeFrame() > UPDATE_SLEEP_NONE)
	{
		if (m_isInUpdate)
		{
			// we're changing this while in our own update (probably via a move state).
			// just do nothing, since update will calculate the correct sleep behavior at the end.
		}
		else
		{
			setWakeFrame(getObject(), UPDATE_SLEEP_NONE);
		}
	}
#endif
}

//-------------------------------------------------------------------------------------------------
void AIUpdateInterface::friend_notifyStateMachineChanged()
{
	wakeUpNow();
}

//-------------------------------------------------------------------------------------------------
static Bool hasFightingWeapon( const Object *obj )
{
	for (Int slot = PRIMARY_WEAPON; slot < WEAPONSLOT_COUNT; ++slot)
	{
		const Weapon *weapon = obj->getWeaponInWeaponSlot( (WeaponSlotType)slot );
		if (weapon == NULL)
			continue;
		const DamageType damageType = weapon->getDamageType();
		if (damageType != DAMAGE_DISARM && damageType != DAMAGE_HAZARD_CLEANUP)
			return TRUE;
	}
	return FALSE;
}

//-------------------------------------------------------------------------------------------------
/**
 * The "main loop" of the AI subsystem
 */
DECLARE_PERF_TIMER(AIUpdateInterface_update)
UpdateSleepTime AIUpdateInterface::update( void )	 
{
	//DEBUG_LOG(("AIUpdateInterface frame %d: %08lx\n",TheGameLogic->getFrame(),getObject()));

	USE_PERF_TIMER(AIUpdateInterface_update)
	
	m_isInUpdate = TRUE;

	m_completedWaypoint = NULL; // Reset so state machine update can set it if we just completed the path.

	// assume we can sleep forever, unless the state machine (or turret, etc) demand otherwise
	UpdateSleepTime subMachineSleep = UPDATE_SLEEP_FOREVER;

	StateReturnType stRet = getStateMachine()->updateStateMachine();

	// A unit that was just built walks a short exit path out of its producer and then, if the player
	// set a rally point, attack moves to it - so it stops and fights whatever it runs into on the way
	// instead of taking the shots and walking on.  This is checked right after the machine ran,
	// because finishing (or failing) the exit path is what drops us into idle in the first place.
	// Something with nothing to fight with - a dozer, a supply truck, an empty ambulance - has no
	// reason to stop for what it meets, so it takes the plain move instead.  isAbleToAttack alone
	// does not say that: a dozer or a GLA worker carries a mine disarming weapon and an ambulance a
	// hazard cleanup one, and both count as weapons there.
	if (m_hasExitProductionRallyPoint && getAIStateType() == AI_IDLE)
	{
		Coord3D rallyPoint = m_exitProductionRallyPoint;
		m_hasExitProductionRallyPoint = FALSE;
		const Bool fights = getObject()->isAbleToAttack() && hasFightingWeapon(getObject());

		// a rally point across the map is reached through the tunnels when they are shorter
		// the rally point and the tunnel tracker are float
		const Coord3D myPos = floatPosOf( getObject() );
		const Real walkX = rallyPoint.x - myPos.x;
		const Real walkY = rallyPoint.y - myPos.y;
		Object *entrance = getObject()->getControllingPlayer()->getTunnelSystem()->findTunnelShortcut( &myPos,
			&rallyPoint, (Real)sqrt( walkX * walkX + walkY * walkY ) );
		const Bool tunnelled = entrance != NULL
			&& takeTunnelTrip( entrance, &rallyPoint, fights ? TUNNEL_TRIP_ATTACK_MOVE : TUNNEL_TRIP_MOVE, CMD_FROM_AI );
		if (!tunnelled)
		{
			if (fights)
				privateAttackMoveToPosition( &rallyPoint, NO_MAX_SHOTS_LIMIT, CMD_FROM_AI );
			else
				privateMoveToPosition( &rallyPoint, CMD_FROM_AI );
		}
		stRet = STATE_CONTINUE;
	}

	// A unit that was sent to pick up a salvage crate walks back to the spot it was standing on, so
	// the line it was part of closes up again instead of drifting to wherever the wrecks fell.  The
	// crate is reached, and the trip ends, by dropping into idle, which is the same signal the rally
	// point above waits for.
	if (m_hasSalvageReturnPosition && getAIStateType() == AI_IDLE)
	{
		Coord3D returnPosition = m_salvageReturnPosition;
		m_hasSalvageReturnPosition = FALSE;
		privateMoveToPosition( &returnPosition, CMD_FROM_AI );
		stRet = STATE_CONTINUE;
	}

	// A move order that a tunnel shortened: idle inside the network means the enter just finished, so
	// leave by the mouth nearest the goal; idle outside means the exit is done, or the enter gave up,
	// and either way what is left is the walk to the goal.
	if (m_hasTunnelTrip && getAIStateType() == AI_IDLE)
	{
		Object *me = getObject();
		Object *tunnel = me->getContainedBy();
		if (tunnel != NULL && tunnel->getContain()->isTunnelContain())
		{
			Object *exit = me->getControllingPlayer()->getTunnelSystem()->findQuietTunnelNear( &m_tunnelTripGoal );
			privateExit( exit != NULL ? exit : tunnel, CMD_FROM_AI );
		}
		else
		{
			Coord3D goal = m_tunnelTripGoal;
			m_hasTunnelTrip = FALSE;
			if (m_tunnelTripEnd == TUNNEL_TRIP_ATTACK_MOVE && me->isAbleToAttack() && hasFightingWeapon(me))
				privateAttackMoveToPosition( &goal, NO_MAX_SHOTS_LIMIT, CMD_FROM_AI );
			else
				privateMoveToPosition( &goal, CMD_FROM_AI );
		}
		stRet = STATE_CONTINUE;
	}

	if (IS_STATE_SLEEP(stRet))
	{
		Int frames = GET_STATE_SLEEP_FRAMES(stRet);
		if (frames < subMachineSleep)
			subMachineSleep = UPDATE_SLEEP(frames);
	}
	else
	{
		// it's STATE_CONTINUE, STATE_SUCCESS, or STATE_FAILURE, 
		// any of which will probably require next frame
		subMachineSleep = UPDATE_SLEEP_NONE;
	}

	// note that this is all OK with sleepiness, since m_movementComplete can
	// only be set via our statemachine (via friend_startingMove or friend_endMove),
	// which we just called. thus we should
	// never have worry about waking ourselves up when this changes, since
	// if it changes the code will always flow thru here anyway. (srj)
	if (m_movementComplete)
	{
		setQueueForPathTime(0);

		/* Arrived, so the march is over.  The slot deliberately outlives a route (see
			 crowdReleaseCorridor) and this is the other end of that: without it a unit that finished a
			 group move would still be carrying its place in a formation that has stopped existing, and
			 would ride the lane for that place the next time anything sent it anywhere. */
		clearCrowdLane();

		// destroy path
		destroyPath();
		setLocomotorGoalNone();

		getObject()->clearModelConditionState(MODELCONDITION_MOVING);

		Coord3D goalPos;
		if (TheAI->pathfinder()->goalPosition(getObject(), &goalPos)) 
		{
			// Pop to goal - This shouldn't happen (often), but make sure we got to where we're going.
			// P5: the pathfinder's goal is float
			const Coord3D myPos = floatPosOf(getObject());
			Real dx = goalPos.x-myPos.x;
			Real dy = goalPos.y-myPos.y;
			if (dx*dx+dy*dy>=PATHFIND_CELL_SIZE_F*PATHFIND_CELL_SIZE_F)
			{
				// Too far, so just grid current pos.
				goalPos = myPos;
				TheAI->pathfinder()->snapPosition(getObject(), &goalPos);
			}
			setFinalPosition(&goalPos);
			TheAI->pathfinder()->updateGoal(getObject(), &goalPos, getObject()->getLayer());
		}
		m_movementComplete = FALSE;
		ignoreObstacle(NULL);
	}

	UnsignedInt now = TheGameLogic->getFrame();
	if (m_queueForPathFrame != 0)
	{
		if (now >= m_queueForPathFrame)
		{
			setQueueForPathTime(0);
			queueForPathOrRetry();
		}
		else
		{
			UnsignedInt sleepForPathDelta = m_queueForPathFrame - now;
			if (sleepForPathDelta < subMachineSleep)
				subMachineSleep = UPDATE_SLEEP(sleepForPathDelta);
		}
	}

	Object *obj = getObject();

	if (! obj->isEffectivelyDead() &&
			! obj->isDisabledByType( DISABLED_PARALYZED ) &&
			! obj->isDisabledByType( DISABLED_UNMANNED ) &&
			! obj->isDisabledByType( DISABLED_EMP ) &&
			! obj->isDisabledByType( DISABLED_SUBDUED ) &&
			! obj->isDisabledByType( DISABLED_HACKED ) )
	{
		// If we are dead, don't let the turrets do anything anymore, or else they will keep attacking
		for (int i = 0; i < MAX_TURRETS; ++i)
		{
			if (m_turretAI[i])
			{
				UpdateSleepTime tmp = m_turretAI[i]->updateTurretAI();
				if (tmp < subMachineSleep)
					subMachineSleep = tmp;
			}
		}
	}

	// must do death check outside of the state machine update, to avoid corruption
	if (isAiInDeadState() && !(getStateMachine()->getCurrentStateID() == AI_DEAD) )
	{
		/// @todo Yikes! If we are not interruptable, and we die, what do we do? (MSB)
		getStateMachine()->clear();
		getStateMachine()->setState( AI_DEAD );
		getStateMachine()->lock("AIUpdateInterface::update");
		// strangely, dead things need to NOT sleep at all. (but they don't stay dead for long,
		// so this is not too bad.)
		subMachineSleep = UPDATE_SLEEP_NONE;
	}

	// do this objects movement
	UpdateSleepTime tmp = doLocomotor();
	if (tmp < subMachineSleep)
		subMachineSleep = tmp;

#ifdef ALLOW_DEMORALIZE
	RELEASE_CRASH(("If ALLOW_DEMORALIZE is ever defined, this code must be redone to do proper SLEEPY updates. (srj)"));
	// update the demoralized frames if present
	if( m_demoralizedFramesLeft > 0 )
	{
		setDemoralized( m_demoralizedFramesLeft - 1 );
	}
#endif

#ifdef ALLOW_SURRENDER
	RELEASE_CRASH(("If ALLOW_SURRENDER is ever defined, this code must be redone to do proper SLEEPY updates. (srj)"));
	doSurrenderUpdateStuff();
#endif

	m_isInUpdate = FALSE;

	if (m_completedWaypoint != NULL)
	{
		// sleep NONE here so that it will get reset next frame.
		// this happen infrequently, so it shouldn't be an issue.
		return UPDATE_SLEEP_NONE;
	}
	else
	{
#ifdef SLEEPY_AI
		return subMachineSleep;
#else
		return UPDATE_SLEEP_NONE;
#endif
	}
} 



//-------------------------------------------------------------------------------------------------
/**
 * Append waypoint to queue for later movement
 */
Bool AIUpdateInterface::queueWaypoint( const Coord3D *pos )
{
	if (m_waypointCount < MAX_WAYPOINTS)
	{
		m_waypointQueue[ m_waypointCount++ ] = *pos;
		return TRUE;
	}
	return FALSE;
}

//-------------------------------------------------------------------------------------------------
/**
 * Start moving along the waypoint path in the queue
 */
void AIUpdateInterface::executeWaypointQueue( void )
{
	// the dead don't listen very well
	if (isAiInDeadState())
		return;

//	m_actionStack->clear();

	if (m_waypointCount > 0)
	{
		m_waypointIndex = 0;
		m_executingWaypointQueue = TRUE;
	}
}

//-------------------------------------------------------------------------------------------------
void AIUpdateInterface::clearWaypointQueue( void )
{
	m_waypointCount = 0;
	m_executingWaypointQueue = FALSE;
}

//-------------------------------------------------------------------------------------------------
void AIUpdateInterface::markAsDead()
{
	m_isAiDead = TRUE;
	getObject()->setEffectivelyDead(TRUE);
	wakeUpNow();	// wake us up immediately so that our anim plays promptly!
}

//-------------------------------------------------------------------------------------------------
/* Returns TRUE if this ai has a higher path priority than the other one.
The way to have a higher priority is:
1. If the paths were assigned when both units were in the same ai group, we use the path priority assigned.
2. If not, the unit that is in front has the higher priority.
3. If exactly tied (usually beacause both units got unfortunately snapped to the same location), ObjectID is used
to break the tie. 
*/
Bool AIUpdateInterface::hasHigherPathPriority(AIUpdateInterface *otherAI) const
{
	Object *other = otherAI->getObject();

	// Dozers have highest priority.
	if (getObject()->isKindOf(KINDOF_DOZER) && !other->isKindOf(KINDOF_DOZER)) {
		return TRUE;
	}
	if (!getObject()->isKindOf(KINDOF_DOZER) && other->isKindOf(KINDOF_DOZER)) {
		return FALSE;
	}

	// Vehicles always have higher priority than infantry.
	if (getObject()->isKindOf(KINDOF_VEHICLE) && other->isKindOf(KINDOF_INFANTRY)) {
		return TRUE;
	}
	if (getObject()->isKindOf(KINDOF_INFANTRY) && other->isKindOf(KINDOF_VEHICLE)) {
		return FALSE;
	}

	// The paths aren't of the same group, so see which unit is in front.
	const FCoord3D ourDir = *getObject()->getUnitDirectionVector2DFix();
	const FCoord3D otherDir = *other->getUnitDirectionVector2DFix();
	if (ourDir.x*otherDir.x + ourDir.y*otherDir.y <= Fix(0)) {
		return getObject()->getID() < other->getID();
	}
	FCoord2D combinedDir;
	combinedDir.set(ourDir.x + otherDir.x, ourDir.y + otherDir.y);
	FCoord2D vectorToOther;
	vectorToOther.set(other->getPositionFix()->x - getObject()->getPositionFix()->x,
		other->getPositionFix()->y - getObject()->getPositionFix()->y);
	// Dot product is our directions projected onto each other.
	Fix dotProduct = combinedDir.x*vectorToOther.x	+ combinedDir.y*vectorToOther.y;
	if (dotProduct>Fix(0)) return FALSE;  // other is ahead of us along our directional vector.
	if (dotProduct<Fix(0)) return TRUE; // We are ahead of other.
	// Exactly equal.  Use object id's to break the tie.  
	return getObject()->getID() < other->getID();
}

//-------------------------------------------------------------------------------------------------
/* Returns max speed we can have and not run into unit that is blocking us.
*/
Real AIUpdateInterface::calculateMaxBlockedSpeed(Object *other) const
{
	const FCoord3D ourDir = *getObject()->getUnitDirectionVector2DFix();
	const FCoord3D otherDir = *other->getUnitDirectionVector2DFix();
	// Dot product is our directions projected onto each other.
	FCoord2D vectorToOther;
	vectorToOther.set(other->getPositionFix()->x - getObject()->getPositionFix()->x,
		other->getPositionFix()->y - getObject()->getPositionFix()->y);
	const Fix len = vectorToOther.length();
	if (len > Fix(0))
		vectorToOther.set(vectorToOther.x / len, vectorToOther.y / len);
	Fix dotProduct = vectorToOther.x*otherDir.x	+ vectorToOther.y*otherDir.y;
	if (dotProduct<Fix(0)) return 0; // They are running into us.

	// P4: velocities and speeds are the physics' and the locomotor's, still float
	Real speedFactor = fixToReal(dotProduct);
	PhysicsBehavior *otherPhysics = other->getPhysics();
	if (!otherPhysics) {
		return m_curMaxBlockedSpeed;
	}
	Coord3D otherVel = *otherPhysics->getVelocity();
	otherVel.z = 0;
	// Calculate how fast other is moving away from us...
	Real awaySpeed = otherVel.length() * speedFactor;

	// Now calculate the amount we are moving relative to towards them...
	dotProduct = vectorToOther.x*ourDir.x	+ vectorToOther.y*ourDir.y;
	if (dotProduct<=Fix(0)) {
		// Unexpected - we are moving away.  Shouldn't be blocked...
		return m_curMaxBlockedSpeed;
	}
	Real maxSpeed = awaySpeed / fixToReal(dotProduct);
	if (other->getFormationID()!=NO_FORMATION_ID && getObject()->getFormationID()==other->getFormationID()) {
		maxSpeed *= 0.55f; // don't let formations crowd each other.
	}
	if (maxSpeed>m_curMaxBlockedSpeed) return m_curMaxBlockedSpeed;
	return maxSpeed;
}


//-------------------------------------------------------------------------------------------------
static const Fix HEAD_ON_DOT = -0.5_fx;				///< facing more than 120 degrees apart is driving at each other
static const Int  HEAD_ON_PASS_FRAMES = 8;			///< held up head-on this long: pass through

Bool AIUpdateInterface::blockedBy(Object *other)
/* Returns TRUE if we are blocked from moving by the other object.*/
{
	Object *obj = getObject();
	const FCoord3D pos = *obj->getPositionFix();
	ICoord2D goalCell = *getPathfindGoalCell();

	// If we are near our final goal, don't get stuck.
	if (goalCell.x>0 && goalCell.y>0) {
		// P4: the state machine's goal is float
		const FCoord3D goalPos = fcoordFromCoord3D(*getStateMachine()->getGoalPosition());
		const Fix cell = Fix(PATHFIND_CELL_SIZE);
		if (fixAbs(goalPos.x-pos.x)<cell && fixAbs(goalPos.y-pos.y)<cell) {
			return FALSE; // If we're approaching our goal, ignore obstacles.
		}
	}

	Bool canCrush = obj->canCrushOrSquish(other, TEST_CRUSH_OR_SQUISH);
	if (canCrush) return FALSE; // just run over them.

	AIUpdateInterface* aiOther = other->getAI();

	if (!aiOther) return FALSE; // Ignore it.
	if (!aiOther->isDoingGroundMovement()) {
		return FALSE; // Can't be blocked if the other is airborne.
	}

	if (getCurLocomotor() && getCurLocomotor()->isMovingBackwards()) {
		return false; // don't collide.
	}
	Bool otherMoving = ( aiOther->m_locomotorGoalType != NONE );
	const FCoord3D otherPos = *other->getPositionFix();
	Fix dx = pos.x-otherPos.x;
	Fix dy = pos.y-otherPos.y;
	Fix curDSqr = dx*dx+dy*dy;
	const FCoord3D ourDir = *obj->getUnitDirectionVector2DFix();
	const FCoord3D theirDir = *other->getUnitDirectionVector2DFix();
	// Dot product is our directions projected onto each other.
	const Fix dotProduct = ourDir.x*theirDir.x	+ ourDir.y*theirDir.y;

	/* A foot soldier marching our way is not in our way. A vehicle blocked by an ally on foot tells him
		 to step aside, and a soldier who steps aside loses his own route and stands for a second before
		 the repath guard lets him ask for another: in a 30-unit mixed squad every one of the 28 stops of
		 that kind was a Ranger shoved by a Crusader going the same way. The vehicle drives through him
		 instead, the way infantry already walks through infantry. One standing still, or crossing, is
		 still asked to move. */
	if (!obj->isKindOf(KINDOF_INFANTRY) && other->isKindOf(KINDOF_INFANTRY) && otherMoving) {
		if (dotProduct > 0.5_fx)
			return FALSE;
	}

	if (obj->isKindOf(KINDOF_INFANTRY) && other->isKindOf(KINDOF_INFANTRY)) {
		// Infantry doesn't tend to impede other infantry...
#ifdef INFANTRY_MOVES_THROUGH_INFANTRY
		if (!otherMoving) {
			return FALSE;  // Infantry can run through other infantry.
		}
		return FALSE; 
#else
		// If we are crossing, just pass through.
		if (dotProduct<=0.25_fx) return FALSE;  // we are not moving in the same direction.
#endif
	}

	if (curDSqr < 0.01_fx) {	// a tenth of a unit apart: a cell's square times 0.0001
		// Somehow 2 units ended up on the same grid.
		// Lowest path priority wins.
		return (hasHigherPathPriority(aiOther));
	}

	// we've been blocked for a while.  If we're crossing, just move through.
	if (getNumFramesBlocked()>LOGICFRAMES_PER_SECOND) {
		if (dotProduct<=Fix(0)) return FALSE;  // we are not moving in the same direction.
	}
	/* The rule above is EA's way out of a head-on meeting and it never fires: doLocomotor puts
		 m_blockedFrames back to 1 on every frame the locomotor reports itself unblocked, which a tank
		 that has finished turning to face its blocker does, so two groups driving into each other
		 stood nose to nose until the rescue ladder or a step aside broke them up. Traced on 20 BattleMasters meeting 20 Crusaders in the open, the front
		 pair sat at a speed of 0 for over 200 frames while everybody behind queued on them. This one
		 counts on its own clock and lets the pair through each other after a quarter of a second. */
	if (otherMoving && dotProduct <= HEAD_ON_DOT)
	{
		m_headOnSeen = TRUE;
		if (m_headOnFrames > HEAD_ON_PASS_FRAMES)
			return FALSE;
	}

	Fix collisionAngle = relativeAngle2DFix( obj, otherPos );
	Fix otherAngle = relativeAngle2DFix( other, pos );
	//DEBUG_LOG(("Collision angle %.2f, %.2f, %s, %x %s\n", collisionAngle*180/PI, otherAngle*180/PI, obj->getTemplate()->getName().str(), obj, other->getTemplate()->getName().str()));
	Fix angleLimit = FIX_PI/Fix(4); // 45 degrees.
	const Fix halfPi = FIX_PI/Fix(2);
	if (collisionAngle>halfPi || collisionAngle<-halfPi) {
		return FALSE; // we're moving away.
	}
	if (!otherMoving) angleLimit *= 0.75_fx;
	if (collisionAngle>angleLimit || collisionAngle<-angleLimit) {
		if (dotProduct<=Fix(0)) return FALSE;  // we are not moving in the same direction.
		if (otherMoving && (otherAngle>angleLimit || otherAngle<-angleLimit) ) {
			// See if we're running into each other.
			dx += ourDir.x - theirDir.x;
			dy += ourDir.y - theirDir.y;
			if (curDSqr>dx*dx+dy*dy) {
				if (hasHigherPathPriority(aiOther)) {
					// Lowest path priority wins.
					return FALSE;
				}
			}	else {
				//DEBUG_LOG(("Moving Away From EachOther\n"));
				return FALSE;  // moving away, so no need for corrective action.
			}
		} else {
			return FALSE;	 // Off angle, and they're not moving, so we aren't moving into each other.
		}
	}


	if (!aiOther->isAiInDeadState())	
	{
		return TRUE;
	}

	return FALSE;
}

//-------------------------------------------------------------------------------------------------
Bool AIUpdateInterface::needToRotate(void)
/* Returns TRUE if we need to rotate to point in our path's direcion.*/
{
	if (isWaitingForPath()) 
		return TRUE; // new path will probably require rotation.

	if (this->getCurLocomotor() && this->getCurLocomotor()->getWanderWidthFactor()>0.0f) 
		return FALSE; // wanderers don't need to rotate.

	Fix deltaAngle = Fix(0);
	if (getPath())
	{
		ClosestPointOnPathInfo info;
		CRCDEBUG_LOG(("AIUpdateInterface::needToRotate() - calling computePointOnPath() for object %d\n", getObject()->getID()));
		// P5: the path is float
		getPath()->computePointOnPath(getObject(), m_locomotorSet, floatPosOf(getObject()), info);
		deltaAngle = relativeAngle2DFix( getObject(), fcoordFromCoord3D(info.posOnPath) );
	}

	if (fixAbs(deltaAngle)>FIX_PI/Fix(30))
	{
		return TRUE;
	}

	return FALSE;
}


//-------------------------------------------------------------------------------------------------
// the crowd model: the constants the steering stack is made of, all in world units (a pathfind cell is 10).
//-------------------------------------------------------------------------------------------------
static const Real CROWD_AIR					= 4.0f;		///< air a body wants round it, on top of both radii
static const Real CROWD_SEP_STEP		= 1.5f;		///< most one frame of separation may move a lane
static const Real CROWD_PASS_CLEAR	= 6.0f;		///< room a passing lane leaves beside the blocker
static const Real CROWD_PASS_TIGHT	= 1.5f;		///< and the room it settles for rather than queue
static const Int  CROWD_PASS_RETRY	= 10;			///< queued this long: ask to pass again, hold or no hold
static const Real CROWD_TAPER_DIST	= PATHFIND_CELL_SIZE_F * 8.0f;	///< the band closes over the last of the route
static const Real CROWD_PLAN_END_SLACK	= PATHFIND_CELL_SIZE_F * 3.0f;	///< how far the move state may move a planned route's end and it still be the same order
static const Int  CROWD_BRAKE_FRAMES	= 8;		///< frames of closing time a brake is allowed to read
static const Int  CROWD_FAN_FRAMES	= 12;			///< held up this long before spreading out
static const Real CROWD_FAN_RATE		= 0.8f;		///< and then sideways at this much a frame
static const Real CROWD_SEP_FILTER	= 0.13f;	///< how much of a frame's sideways push is believed (~4/sec)
static const Real CROWD_RELEASE_FILTER	= 0.15f;	///< how fast the throttle comes back once a brake lifts (~half a second to full)
static const Real CROWD_AIM_CRUISE	= 0.23f;	///< how fast the aim follows the route while driving (~7/sec)
static const Real CROWD_AIM_URGENT	= 0.67f;	///< and while manoeuvring (~20/sec), where lag is worse than twitch
static const Real CROWD_AIM_DEAD		= 0.035f;	///< two degrees: hold the wheel still rather than chase the noise
//-------------------------------------------------------------------------------------------------
/* Being stuck.  These are not crowd constants: every unit trying to drive somewhere is measured
	 against them, whether or not the crowd model steers it.  The rungs of the ladder are spaced by continued lack of
	 progress rather than by wall clock, so a unit that starts moving again drops off it at once. */
//-------------------------------------------------------------------------------------------------
static const Real STUCK_TURN_EPSILON	= 0.004f;	///< a quarter of a degree a frame: below this the hull is not turning
static const Int  STUCK_PRESS_FRAMES	= 10;		///< a third of a second of getting nowhere: stop being polite
static const Int  STUCK_REPATH_FRAMES	= 45;		///< a second and a half: the ladder's first rung, and the gap between rungs
static const Int  STUCK_BACKOUT_FRAMES	= 66;	///< how long a backing-out manoeuvre is given before the route is tried again
static const Int  STUCK_COOL_FRAMES	= 90;			///< no second drastic thing inside this, or a wedged pair rock forever
static const Int  STUCK_CYCLE_FRAMES	= 150;	///< five seconds past the last rung: run the whole ladder again
static const Int  STUCK_DITHER_FRAMES	= 90;		///< three seconds is long enough to tell a shuffle from a corner
static const Real STUCK_BRIDGE_NEAR	= PATHFIND_CELL_SIZE_F * 20.0f;	///< a unit shaking this close to a deck on its route is at the ramp
static const Real STUCK_DITHER_RATIO	= 0.3f;	///< gained less than this much of the ground it covered
static const Int  CROWD_MERGE_FRAMES	= 45;		///< a second and a half: how far ahead a merge is worth noticing
static const Int  CROWD_TTC_FRAMES	= 21;			///< closing this fast on somebody counts as being in our way, wherever it sits
static const Int  CROWD_REPATH_QUEUE	= 24;		///< queued behind traffic this long: stop steering round it and ask for a new route

//-------------------------------------------------------------------------------------------------
/* Returns TRUE if the physics collide should apply the force.  Normally not.
Also determines whether objects are blocked, and if so, if they are stuck.  jba.*/
Bool AIUpdateInterface::processCollision(PhysicsBehavior *physics, Object *other)
{

#ifdef DO_UNIT_TIMINGS
	return false;
#endif

	if (m_ignoreCollisionsUntil > TheGameLogic->getFrame()) 
		return FALSE;

	if (m_canPathThroughUnits) 
		return FALSE;

	AIUpdateInterface* aiOther = other->getAI();
	if (aiOther == NULL) 
		return FALSE;

	Bool selfMoving = isMoving();
	Bool otherMoving = ( aiOther && aiOther->isMoving() );
	if (!isDoingGroundMovement()) return FALSE;
	if (!aiOther->isDoingGroundMovement()) return FALSE;
	if (selfMoving) 
	{
		Bool blocked = blockedBy(other);
		if (blocked) 
		{
			if (getObject()->isKindOf(KINDOF_INFANTRY)) 
			{
				// Panic bounces around.
				if (getStateMachine()->getCurrentStateID() == AI_PANIC) 
				{
					return TRUE; // just bounce off of other humans.
				}
			}
			m_isBlocked = TRUE; // we are blocked.
 			if (otherMoving && aiOther->isWaitingForPath()) 
			{
				return FALSE; // let them get their path;
			}

			Real maxSpeed = calculateMaxBlockedSpeed(other);
			// -tracemove <id>: who is in the way, which way he faces against us, and where he sits off our nose,
			// so a jam can be walked back to the pair at its front one unit at a time
			if (TheGlobalData->m_traceMoveID > 0 && getObject()->getID() == (ObjectID)TheGlobalData->m_traceMoveID)
			{
				// the log line is float
				const FCoord3D *od = other->getUnitDirectionVector2DFix();
				const FCoord3D *md = getObject()->getUnitDirectionVector2DFix();
				const Coord3D otherPos = floatPosOf(other);
				DEBUG_LOG(("MOVEBLOCK %d by %d %s at %.0f,%.0f facing %.2f moving %d waiting %d allowed %.3f bearing %.1f\n", TheGameLogic->getFrame(),
					other->getID(), other->getTemplate()->getName().str(), otherPos.x, otherPos.y,
					fixToReal(md->x * od->x + md->y * od->y), otherMoving, aiOther->isWaitingForPath(), maxSpeed,
					fixToReal(relativeAngle2DFix( getObject(), *other->getPositionFix() )) * 180.0f / PI));
			}
			if (maxSpeed < m_curMaxBlockedSpeed)
			{
				m_curMaxBlockedSpeed = maxSpeed;
			}

			// whether the route is wide enough to go round is decided every frame in crowdSteer, off the
			// band and the whole neighbourhood rather than off this one collision

			if (!aiOther->isMovingAwayFrom(getObject())) {

				if (other->isKindOf(KINDOF_INFANTRY) && !getObject()->isKindOf(KINDOF_INFANTRY)) 
				{
					//Kris: Patch 1.01 -- November 5, 2003
					//Prevent busy units from being told to move out of the way!
					if( other->testStatus( OBJECT_STATUS_IS_USING_ABILITY ) || other->getAI() && other->getAI()->isBusy() )
					{
						return FALSE;
					}
					aiOther->aiMoveAwayFromUnit(getObject(), CMD_FROM_AI);
					return FALSE;
				}
#define dont_MOVE_AROUND // It just causes more problems than it fixes. jba.
#ifdef MOVE_AROUND 
				if (m_curLocomotor!=NULL && (other->isKindOf(KINDOF_INFANTRY)==getObject()->isKindOf(KINDOF_INFANTRY))) {
					Real myMaxSpeed = m_curLocomotor->getMaxSpeedForCondition(getObject()->getBodyModule()->getDamageState());
					Locomotor *hisLoco = aiOther->getCurLocomotor();
					if (hisLoco) {
						Real hisMaxSpeed = hisLoco->getMaxSpeedForCondition(other->getBodyModule()->getDamageState());
						if (hisMaxSpeed > 0.05 && hisMaxSpeed < 0.6f*myMaxSpeed)	{
							aiOther->aiMoveAwayFromUnit(getObject(), CMD_FROM_AI);
							return FALSE;
						}
					}
				}
#endif
			}

			//DEBUG_LOG(("Blocked %s, %x, %s\n", getObject()->getTemplate()->getName().str(), getObject(), other->getTemplate()->getName().str()));
			if (m_blockedFrames==0) m_blockedFrames = 1;
			if (!needToRotate()) 
			{
				// If we are already pointing in the right direction, we may be stuck.
				if (!otherMoving)
				{
					// (asking the idle allied blocker to step aside was tried here and reverted:
					// at a group's destination every arriving unit shoved the parked ones, which
					// shoved others - the group milled about and repathed without end)
					//
					/* The crowd model asks again, under the three conditions that revert was missing.  We have
						 to have been held up for a while, so an arrival that clears on its own is left
						 alone; we have to still have somewhere to be, which is what stops the whole thing
						 at a destination where nobody does; and the parked unit has to be the smaller of
						 the two, so a mob of infantry cannot pass a tank around by taking turns to shove
						 it. */
					if (m_crowdQueued > CROWD_FAN_FRAMES * 2
								&& Crowd_remaining(getObject()) > PATHFIND_CELL_SIZE_F * 3.0f
								&& !crowdOutranksMe(other)
								&& !aiOther->isMovingAwayFrom(getObject())
								&& !aiOther->isBusy()
								&& !other->testStatus(OBJECT_STATUS_IS_USING_ABILITY))
					{
						aiOther->aiMoveAwayFromUnit(getObject(), CMD_FROM_AI);
						m_crowdQueued = 0;			// he has been asked; give him time to answer
						return FALSE;
					}
					// Intense logging jba
					// DEBUG_LOG(("Blocked&Stuck !otherMoving\n"));
					m_isBlockedAndStuck = TRUE;
					return FALSE;
				}

				// See if other is blocked by us.
				if (aiOther->blockedBy(getObject())) 
				{
					if (!aiOther->needToRotate()) 
					{
						// Deadlocked: settled by size and by who is nearer the end of his route, which is
						// the same order the steering used all the way here
						if (crowdOutranksMe(other))
						{
							// get out of his way.
							aiMoveAwayFromUnit(aiOther->getObject(), CMD_FROM_AI);
							//m_isBlockedAndStuck = TRUE;
							// Intense logging jba.
							// DEBUG_LOG(("Blocked&Stuck other is blockedByUs, has higher priority\n"));
						}
					}
				}	
				else 
				{
					// Just wait.
				}
			}	
			else
			{
				// We are rotating, so don't accumulate blocked frames.
				m_blockedFrames = 1;
			}
		}
	}	
	else 
	{
		if (isAiInDeadState()) 
		{
			// Dead infantry get pushed around by crushers.
			if (getObject()->isKindOf(KINDOF_INFANTRY) && other->canCrushOrSquish(getObject(), TEST_SQUISH_ONLY)) 
			{
				return TRUE;
			}
		}

		const FCoord3D *myFix = getObject()->getPositionFix();
		const FCoord3D *otherFix = other->getPositionFix();
		Fix dx = myFix->x - otherFix->x;
		Fix dy = myFix->y - otherFix->y;
		Fix curDSqr = dx*dx+dy*dy;
		// (a footprint-scaled threshold was tried here and reverted: tanks parked closer than
		// it by the group's own formation kept being pulled apart and never came to rest)
		if (!otherMoving && curDSqr < Fix(PATHFIND_CELL_SIZE*PATHFIND_CELL_SIZE/4))
		{
			// P5: the pathfinder and the move order take float positions
			Coord3D otherPos = floatPosOf(other);
			if (this->getCurrentStateID() == AI_BUSY) {
				return false;
			}
			if (getObject()->testStatus(OBJECT_STATUS_IS_USING_ABILITY)) {
				return false;  // we are doing a special ability.  Shouldn't move at this time.  jba.
			}
			// jba intense debug
			//DEBUG_LOG(("*****Units ended up on top of each other.  Shouldn't happen.\n"));
			if (isIdle()) {
				Coord3D safePosition = floatPosOf(getObject());
				
				TheAI->pathfinder()->adjustToPossibleDestination(getObject(), getLocomotorSet(), &safePosition);
				aiMoveToPosition( &safePosition, CMD_FROM_AI ); 
			}
			if (aiOther->isIdle()) {
				TheAI->pathfinder()->adjustToPossibleDestination(other, aiOther->getLocomotorSet(), 
					&otherPos);
				aiOther->aiMoveToPosition( &otherPos, CMD_FROM_AI);
			}
		}
	}
	return FALSE;
}

//-------------------------------------------------------------------------------------------------
/**
 * See if we can do a quick path without pathfinding.
 */
Bool AIUpdateInterface::canComputeQuickPath( void )
{
	/* Basically, if a unit is moving through the air, we can quick path.  jba. */
	Bool landBound = FALSE;
	// Note - if a truck happens to pop into the air and gets a move to command, it still
	// needs to pathfind.  So only skip pathfinding for airborne things that can fly... jba.
	if (!(m_locomotorSet.getValidSurfaces() & LOCOMOTORSURFACE_AIR))
  {
		landBound = TRUE;
	}

	Bool unitIsFlyingThroughTheAir = FALSE;
	if (landBound) {
		unitIsFlyingThroughTheAir = FALSE; // Land bound units never fly.
	}	else {
		if (!isDoingGroundMovement()) {
			// If it can fly, and it isn't moving on the ground, we're flying.
			unitIsFlyingThroughTheAir = TRUE;
		}
	}
	return unitIsFlyingThroughTheAir;
}

//-------------------------------------------------------------------------------------------------
/**
 * Create a quick path.  (Just places the start & end point as the path). jba.
 */
Bool AIUpdateInterface::computeQuickPath( const Coord3D *destination )
{
	// for now, quick path objects don't pathfind, generally airborne units
	// build a trivial one-node path containing destination

	
	// First, see if our path already goes to the destination.
	if (m_path) {
		PathNode *closeNode = NULL;
		closeNode = m_path->getLastNode();
		if (closeNode && closeNode->getNextOptimized()==NULL) {
			Real dxSqr = destination->x - closeNode->getPosition()->x;
			dxSqr *= dxSqr;
			Real dySqr = destination->y - closeNode->getPosition()->y;
			dySqr *= dySqr;
			Real dzSqr = destination->z - closeNode->getPosition()->z;
			dzSqr *= dzSqr;
			if (dxSqr+dySqr+dzSqr<0.25f) {
				return TRUE;
			}
		}
	}
	// destroy previous path
	destroyPath();
	if (getObject()->isKindOf(KINDOF_AIRCRAFT) && !getObject()->isKindOf(KINDOF_PROJECTILE)) {	
		m_path = TheAI->pathfinder()->getAircraftPath(getObject(), destination);
	} else {
		m_path = newInstance(Path);
		m_path->prependNode( destination, LAYER_GROUND );
		Coord3D pos = floatPosOf(getObject());	// P5: path nodes are float
		pos.z = destination->z;
		m_path->prependNode( &pos, getObject()->getLayer() );
		m_path->getFirstNode()->setNextOptimized(m_path->getFirstNode()->getNext());

		if (TheGlobalData->m_debugAI==AI_DEBUG_PATHS) 
		{
			TheAI->pathfinder()->setDebugPath(m_path);
		}
	}


	// timestamp when the path was created
	m_pathTimestamp = TheGameLogic->getFrame();

	m_blockedFrames = 0;
	m_isBlockedAndStuck = FALSE;
	return TRUE;
}

//-------------------------------------------------------------------------------------------------
/**
 * Invoke the pathfinder to compute a path to the desired location.
 */
Bool AIUpdateInterface::computePath( PathfindServicesInterface *pathServices, Coord3D *destination )
{

	if (!m_isBlockedAndStuck)	{
		destroyPath();
	}

	// the group's plan is for the path this order asks for and no other, whichever way it is answered
	CrowdRoute planned;
	planned.swap( m_crowdPlanned );

	if (canComputeQuickPath())
	{
		return computeQuickPath(destination);
	}
	m_retryPath = false;
	// P5: the pathfinder, its extent and the destination are float
	const Coord3D myPos = floatPosOf(getObject());
	Region3D extent;
	TheTerrainLogic->getMaximumPathfindExtent(&extent);
	if (!extent.isInRegionNoZ(destination)) {
		// We're going off the map.
		Coord3D pos = myPos;
		if (!extent.isInRegionNoZ(&pos))	{
			// We're starting off the map.  Since we're off the map, we can't pathfind so just build a path.
			return computeQuickPath(destination);
		}
	}

	// Special case of exit factory. jba.
	if ((m_stateMachine->getCurrentStateID() == AI_FOLLOW_EXITPRODUCTION_PATH) && canPathThroughUnits()) {
		Bool ok = computeQuickPath(destination);
		if (ok) {
			TheAI->pathfinder()->moveAlliesAwayFromDestination(getObject(), *destination);
			setCanPathThroughUnits(false);
			setGoalPositionClipped(destination, CMD_FROM_AI);
			return ok;
		}
	}

	Path *theNewPath = NULL;
	TheAI->pathfinder()->setIgnoreObstacleID( getIgnoredObstacleID() );
	TheAI->pathfinder()->setIgnoreUnderConstruction( getDozerAIInterface() != NULL );

	Coord3D originalDestination = *destination;
	// sanity check - if destination cell is invalid, don't bother pathing

	LocomotorSurfaceTypeMask surfaces = m_locomotorSet.getValidSurfaces();
	if (!m_isFinalGoal && TheAI->pathfinder()->isLinePassable( getObject(), surfaces,
			getObject()->getLayer(), myPos, originalDestination, false, true)) {
		// this way out skips the reset at the bottom, and a flag left on would answer for whoever
		// asks the pathfinder next
		TheAI->pathfinder()->setIgnoreUnderConstruction( FALSE );
		TheAI->pathfinder()->setIgnoreObstacleID( INVALID_ID );
		return computeQuickPath(destination);
	}

	if (!planned.empty() && !m_isBlockedAndStuck)
		theNewPath = crowdPlannedPath( planned, originalDestination );

	PathfindLayerEnum destinationLayer = TheTerrainLogic->getLayerForDestination(destination);
	if (theNewPath != NULL)
	{
		// the lane the group planned for us: see setPlannedCrowdRoute
	}
	else if (TheAI->pathfinder()->validMovementPosition( getObject()->getCrusherLevel()>0, destinationLayer, m_locomotorSet, destination ) == FALSE)
	{
		theNewPath = NULL;
	}
	else
	{
		// compute a ground-based path
		if (m_isBlockedAndStuck) {
			theNewPath = pathServices->patchPath( getObject(), m_locomotorSet, 
				getPath(), m_isBlockedAndStuck);
		}	else {
			theNewPath = pathServices->findPath( getObject(), m_locomotorSet, &myPos,
				destination);
		}
	}
	if (theNewPath==NULL && m_path==NULL) {
		Real pathCostFactor = 0.0f;
		theNewPath = pathServices->findClosestPath( getObject(), m_locomotorSet, &myPos,
			destination, m_isBlockedAndStuck, pathCostFactor, FALSE );
		m_retryPath = true;
	}
	TheAI->pathfinder()->setIgnoreObstacleID( INVALID_ID );
	TheAI->pathfinder()->setIgnoreUnderConstruction( FALSE );
	// after both the direct path and the closest-path fallback: nothing came back, so this unit
	// is not going anywhere this frame.  The count is what says whether a cost change made the
	// search fail rather than merely route differently.
	if (theNewPath == NULL)
		Pathfinder::bumpNoPath();
	m_pathfindFoundNothing = (theNewPath == NULL);
	if (theNewPath) {
		// destroy previous path
		destroyPath();
		m_path = theNewPath;
		if (getCurLocomotor() && getCurLocomotor()->isUltraAccurate()) {
			// Move exactly to the destination.  Normal ground pathfinding moves to a gridded location.
			theNewPath->updateLastNode(&originalDestination);
		}
		setLocomotorGoalPositionOnPath();
 		if( !getObject()->isKindOf(KINDOF_NO_COLLIDE))// If I don't collide with things, I don't need to tell them to get out of the way
			TheAI->pathfinder()->moveAllies(getObject(), theNewPath);
	} else {
		// Keep using the old path.
 		if (m_path && m_isBlockedAndStuck) {
			destroyPath();
			// Stop and wait one second.

			setQueueForPathTime(LOGICFRAMES_PER_SECOND);
			Coord3D goalPos;
			Object *obj = getObject();
			goalPos = floatPosOf(obj);
			TheAI->pathfinder()->snapPosition(obj, &goalPos);
			setFinalPosition(&goalPos);
			setLocomotorGoalNone();

			m_blockedFrames = 0;
			m_isBlocked = FALSE;
			m_isBlockedAndStuck = FALSE;
		}
	}
	// timestamp when the path was created
	m_pathTimestamp = TheGameLogic->getFrame();

	m_blockedFrames = 0;
	m_isBlockedAndStuck = FALSE;
	if (m_path)
		return TRUE;

	return FALSE;
}

//-------------------------------------------------------------------------------------------------
/** The route the group planned for this unit, as a path from where the unit is now.

		The plan was laid at order time from where the unit stood then, and the path is asked for a
		frame or several later, so two things are checked again rather than trusted: the first leg,
		from here, and the end, which the move state may have nudged onto free ground since.  A plan
		that fails either is dropped for a search, which is what the unit would have had anyway. */
//-------------------------------------------------------------------------------------------------
Path *AIUpdateInterface::crowdPlannedPath( const CrowdRoute& planned, const Coord3D& destination )
{
	Object *self = getObject();
	const CrowdRoutePoint& last = planned.back();
	const Real endDx = last.pos.x - destination.x;
	const Real endDy = last.pos.y - destination.y;
	if (endDx * endDx + endDy * endDy > CROWD_PLAN_END_SLACK * CROWD_PLAN_END_SLACK)
		return NULL;					// a different order from the one this was planned for

	const Coord3D myPosF = floatPosOf( self );	// P5: path nodes are float
	const Coord3D *myPos = &myPosF;
	if (!TheAI->pathfinder()->isLinePassable( self, m_locomotorSet.getValidSurfaces(), self->getLayer(),
				*myPos, planned.front().pos, false, true ))
		return NULL;

	Path *path = newInstance( Path );
	path->markOptimized();
	path->appendNode( myPos, self->getLayer() );
	for (Int k = 0; k + 1 < (Int)planned.size(); k++)
		path->appendNode( &planned[ k ].pos, planned[ k ].layer );
	path->appendNode( &destination, TheTerrainLogic->getLayerForDestination( &destination ) );
	return path;
}

//-------------------------------------------------------------------------------------------------
/**
 * Invoke the pathfinder to compute a path to attack the current victim.
 */
Bool AIUpdateInterface::computeAttackPath( PathfindServicesInterface *pathServices, const Object *victim, const Coord3D* victimPos )
{
	//CRCDEBUG_LOG(("AIUpdateInterface::computeAttackPath() for object %d\n", getObject()->getID()));
	// See if it has been too soon.
	if (m_pathTimestamp >= TheGameLogic->getFrame()-2) 
	{
		// jba intense debug
		//CRCDEBUG_LOG(("Info - RePathing very quickly %d, %d.\n", m_pathTimestamp, TheGameLogic->getFrame()));
		if (m_path && m_isBlockedAndStuck) 
		{
			setIgnoreCollisionTime(2*LOGICFRAMES_PER_SECOND);
			m_blockedFrames = 0;
			m_isBlocked = FALSE;
			m_isBlockedAndStuck = FALSE;
			return TRUE;
		}
	}
	Bool landBound = FALSE;
	// Note - if a truck happens to pop into the air and gets a move to command, it still
	// needs to pathfind.  So only skip pathfinding for airborne things that can fly... jba.
	if (!(m_locomotorSet.getValidSurfaces() & LOCOMOTORSURFACE_AIR))
  {
		landBound = TRUE;
	}

	Object* source = getObject();
	if (!victim && !victimPos) 
	{
		//CRCDEBUG_LOG(("AIUpdateInterface::computeAttackPath() - victim is NULL\n"));
		return FALSE;
	}

	PathfindLayerEnum victimLayer = LAYER_GROUND;
	if (victim) {
		victimLayer = victim->getLayer();
	}

	Weapon *weapon = source->getCurrentWeapon();
	if (!weapon)
	{
		DEBUG_CRASH(("no weapon in AIUpdateInterface::computeAttackPath"));
		return FALSE;
	}

	// is our weapon within attack range?
	// if so, just return TRUE with no path.
	if (victim != NULL)
	{
		if (weapon->isWithinAttackRange(source, victim))
		{
			Bool viewBlocked = FALSE;
			if (isDoingGroundMovement() && !victim->isSignificantlyAboveTerrain()) 
			{
				viewBlocked = TheAI->pathfinder()->isAttackViewBlockedByObstacle(source, floatPosOf(source), victim, floatPosOf(victim));	// P5
			}
			if (!viewBlocked) 
			{
				destroyPath();
				//CRCDEBUG_LOG(("AIUpdateInterface::computeAttackPath() - target is in range and visible\n"));
				return TRUE;
			}
			
		}
	}
	else if (victimPos != NULL)
	{
		if (weapon->isWithinAttackRange(source, victimPos))
		{
			Bool viewBlocked = FALSE;
			if (isDoingGroundMovement()) 
			{
				viewBlocked = TheAI->pathfinder()->isAttackViewBlockedByObstacle(source, floatPosOf(source), NULL, *victimPos);	// P5
			}
			if (!viewBlocked) {
				destroyPath();
				//CRCDEBUG_LOG(("AIUpdateInterface::computeAttackPath() target pos is in range and visible\n"));
				return TRUE;
			}
		}
	}

	// Contact weapon
	if (weapon->isContactWeapon()) 
	{
		// Weapon is basically a contact weapon, like a car bomb.  The approach target logic
		// has been modified to let it approach the object, so just approach the target position.	jba.
		Coord3D tmp = *victimPos;
		destroyPath();
		if (this->getCurLocomotor()) 
		{
			getCurLocomotor()->setNoSlowDownAsApproachingDest(TRUE);
		}
		Bool ok = computePath(pathServices, &tmp);
		if (m_path==NULL) return false;
		Real dx, dy;
		dx = victimPos->x - m_path->getLastNode()->getPosition()->x;
		dy = victimPos->y - m_path->getLastNode()->getPosition()->y;
		if (sqr(dx)+sqr(dy) < sqr(PATHFIND_CELL_SIZE_F*3)) {
			if (m_path) 
			{
				m_path->updateLastNode(victimPos); // jam in the coordinates of the target.
			}
		}
		const Coord3D sourcePos = floatPosOf(source);	// P5: against a path node
		dx = sourcePos.x - m_path->getLastNode()->getPosition()->x;
		dy = sourcePos.y - m_path->getLastNode()->getPosition()->y;
		if (sqr(dx)+sqr(dy) < sqr(PATHFIND_CELL_SIZE_F)) {
			// Very short path - we can't get to the goal.
			destroyPath();
			return false;
		}
		//CRCDEBUG_LOG(("AIUpdateInterface::computeAttackPath() is contact weapon\n"));
		return ok;
	}


	Coord3D localVictimPos;
	if (victim != NULL)
	{
		if (victim->isKindOf(KINDOF_BRIDGE)) 
		{
			TBridgeAttackInfo info;
			TheTerrainLogic->getBridgeAttackPoints(victim, &info);
			// the bridge's attack points are map data, in float
			const FCoord3D point1 = fcoordFromCoord3D(info.attackPoint1);
			const FCoord3D point2 = fcoordFromCoord3D(info.attackPoint2);
			Fix distSqr1 = ThePartitionManager->getDistanceSquaredFix( source, &point1, FROM_BOUNDINGSPHERE_3D );
			Fix distSqr2 = ThePartitionManager->getDistanceSquaredFix( source, &point2, FROM_BOUNDINGSPHERE_3D );
			if (distSqr2<distSqr1) {
 				localVictimPos = info.attackPoint2;
			} else {
 				localVictimPos = info.attackPoint1;
			}
		}
		else
		{
			localVictimPos = floatPosOf(victim);	// P5/P6: the attack path and the weapon are float
		}
	}
	else
	{
		localVictimPos = *victimPos;
	}

	localVictimPos.z = layerHeightAt( localVictimPos.x, localVictimPos.y, victimLayer );

	if (getObject()->isAboveTerrain() && !landBound)
	{
		// for now, airborne objects don't pathfind
		// build a trivial one-node path containing destination

		weapon->computeApproachTarget(getObject(), victim, &localVictimPos, 0, localVictimPos);
		//DEBUG_ASSERTCRASH(weapon->isGoalPosWithinAttackRange(getObject(), &localVictimPos, victim, victimPos, NULL),
		//	("position we just calced is not acceptable\n"));
		
		// First, see if our path already goes to the destination.
		if (m_path) 
		{
			PathNode *startNode, *closeNode = NULL;
			startNode = m_path->getFirstNode();
			closeNode = startNode->getNextOptimized();
			if (closeNode && closeNode->getNextOptimized()==NULL) {
				Real dxSqr = localVictimPos.x - closeNode->getPosition()->x;
				dxSqr *= dxSqr;
				Real dySqr = localVictimPos.y - closeNode->getPosition()->y;
				dySqr *= dySqr;
				if (dxSqr+dySqr<0.25f) 
				{
					return TRUE;
				}
			}
		}
		// destroy previous path
		destroyPath();
		m_path = newInstance(Path);
		m_path->prependNode( &localVictimPos, LAYER_GROUND );
		Coord3D pos = floatPosOf(getObject());
		pos.z = localVictimPos.z;
		m_path->prependNode( &pos, LAYER_GROUND );
		m_path->getFirstNode()->setNextOptimized(m_path->getFirstNode()->getNext());
		if (TheGlobalData->m_debugAI==AI_DEBUG_PATHS) 
		{
			TheAI->pathfinder()->setDebugPath(m_path);
		}
	}
	else
	{
		// destroy previous path
		destroyPath();

		TheAI->pathfinder()->setIgnoreObstacleID( getIgnoredObstacleID() );

		// compute a ground-based path
		const Coord3D myPos = floatPosOf(getObject());	// P5
		m_path = pathServices->findAttackPath( getObject(), m_locomotorSet, &myPos,
			victim, &localVictimPos, weapon);
		if (m_path) {
			Coord3D goal = *m_path->getLastNode()->getPosition();
			if (!weapon->isGoalPosWithinAttackRange(getObject(), &goal, victim, &localVictimPos)) {
				// We didn't actually find a path we can attack from. [8/14/2003]
				// If the move is a short distance, just do a find closest path to our current
				// position.  This will unstack us if we are on top of another unit. jba.
				Coord3D objPos = myPos;
				goal.sub(&objPos);
				if (goal.length()<3*PATHFIND_CELL_SIZE_F) {
					destroyPath();
					TheAI->pathfinder()->adjustDestination(getObject(), m_locomotorSet, &objPos);
					m_path = pathServices->findClosestPath(getObject(), m_locomotorSet, &myPos,
								&objPos, false, 0.2f, true );
				}
				if (m_path==NULL) {
					TheAI->pathfinder()->setIgnoreObstacleID( INVALID_ID );
					return false;
				}
			}
			goal = *m_path->getLastNode()->getPosition();
			TheAI->pathfinder()->updateGoal(getObject(), &goal, TheTerrainLogic->getLayerForDestination(&goal));
			if (m_path->getBlockedByAlly()) 
			{
	 			if( !getObject()->isKindOf(KINDOF_NO_COLLIDE))// If I don't collide with things, I don't need to tell them to get out of the way
					TheAI->pathfinder()->moveAllies(getObject(), m_path);
			}
		}
		TheAI->pathfinder()->setIgnoreObstacleID( INVALID_ID );
	}

	// timestamp when the path was created
	m_pathTimestamp = TheGameLogic->getFrame();

	m_blockedFrames = 0;
	m_isBlockedAndStuck = FALSE;
	//CRCDEBUG_LOG(("AIUpdateInterface::computeAttackPath() done\n"));
	if (m_path)
		return TRUE;

	return FALSE;
}

//-------------------------------------------------------------------------------------------------
/**
 * Destroy the current path, and set it to NULL
 */
void AIUpdateInterface::destroyPath( void )
{
	// destroy previous path
	if (m_path)
		m_path->deleteInstance();

	m_path = NULL;
	m_waitingForPath = FALSE; // we no longer need it.
	//CRCDEBUG_LOG(("AIUpdateInterface::destroyPath() - m_isAttackPath = FALSE for object %d\n", getObject()->getID()));
	m_isAttackPath = FALSE;
	// the lane belongs to the route, not to the unit: a new route gets a new one, seeded off
	// wherever the unit is standing when it is handed
	m_laneFractionValid = FALSE;
	crowdReleaseCorridor();
	setLocomotorGoalNone();
}

//-------------------------------------------------------------------------------------------------
/** The band was measured against one route and means nothing against another, so it dies with the
		path.  m_crowdSample going back to 0 also clears the "this route has no band" latch, which is
		what stops a unit on a two-cell path from re-probing every frame for the rest of its life. */
//-------------------------------------------------------------------------------------------------
void AIUpdateInterface::crowdReleaseCorridor( void )
{
	if (m_corridor)
	{
		delete m_corridor;
		m_corridor = NULL;
	}
	m_crowdLatValid = FALSE;
	m_crowdSample = 0;
	m_crowdFit = 0;
	m_crowdQueued = 0;
	m_crowdHoldFrame = 0;
	m_crowdSepSmooth = 0.0f;
	m_crowdCap = 0.0f;
	m_crowdCapValid = FALSE;
	m_crowdAimValid = FALSE;
	/* The backing-out manoeuvre survives the route.  Being wedged is the one thing a new route does
		 not cure - the new one starts in the same hole - and a unit that repaths every second while
		 stuck would throw away the only rule that gets it out, once a second, forever. */

	/* And so does the march.  A route dies for all sorts of reasons that have nothing to do with the
		 group breaking up - the unit was blocked and repathed, an attack move stopped to shoot and
		 started again, the destination moved - and every one of them used to drop the unit out of the
		 crowd model for the rest of its life, because the lane was consumed with the old path and the
		 gate at the top of crowdSteer wants one.  So the slot re-arms itself: the offset is zero
		 because the new route starts under the unit's own tracks, and the per-frame fit takes it back
		 out to its own lane within a few frames.  Only the next order clears it (clearCrowdLane). */
	if (m_crowdLaneOf >= 1)		// 1 is a member driving a lane its group planned: see setPlannedCrowdRoute
	{
		m_pendingCrowdLat = 0.0f;
		m_hasPendingCrowdLat = TRUE;
	}
}

//-------------------------------------------------------------------------------------------------
/**
 * Pick where across the route's width this unit rides.
 *
 * The lane has to be handed down by whoever issued the order, and the first version of this
 * measured it here instead, which was worth nothing: a unit's route starts under its own tracks,
 * so its sideways distance from that route is zero and every member of a group came out at 0.5.
 * A group therefore has to say, at order time, where each member sat across the group - see
 * AIGroup::groupMoveToPosition.  A unit ordered on its own has no group to sit across and rides
 * the centre, which is what retail does anyway.
 */
//-------------------------------------------------------------------------------------------------
void AIUpdateInterface::seedLaneFraction( void )
{
	Bool hadPending = m_hasPendingLane;

	m_laneFraction = m_hasPendingLane ? m_pendingLane : 0.5f;
	m_laneFractionValid = TRUE;
	m_laneHoldFrame = 0;
	m_hasPendingLane = FALSE;

	// the other end of the SHOWLANES trail: a lane handed out at order time is worth nothing if the
	// path arrives after something else has already seeded this unit at the centre.
	if (TheGlobalData->m_showLanes)
	{
		DEBUG_LOG(("SHOWLANES seed: unit %d pending=%d lane=%.2f\n", getObject()->getID(),
			hadPending ? 1 : 0, m_laneFraction));
	}
}

//-------------------------------------------------------------------------------------------------
/**
 * Right of way between two units, decided the same way by both of them.
 *
 * Size first, because a bigger body has less room to be squeezed and shoving it is what wedges a
 * doorway.  Then how much route is left, so the unit nearly there is let out rather than made to
 * wait behind one that has half a map to cross.  The inside of a bend counts as being further along
 * than it is: it is the position with no room, and letting it out first is what lets the rest flow
 * round the corner instead of all four of them arriving at the apex together.  Id last, which
 * decides nothing on the ground but guarantees the two answers are opposites.
 */
//-------------------------------------------------------------------------------------------------
Bool AIUpdateInterface::crowdOutranksMe( Object *other ) const
{
	const Object *self = getObject();
	if (other == NULL)
		return FALSE;

	if (Crowd_outranks( other, self ))
		return TRUE;
	if (Crowd_outranks( self, other ))
		return FALSE;

	Real mine = Crowd_remaining( self ) - Crowd_bendBonus( m_corridor, m_crowdSample, m_crowdLat );
	Real his = Crowd_remaining( other );
	const AIUpdateInterface *ai = other->getAIUpdateInterface();
	if (ai != NULL)
		his -= Crowd_bendBonus( ai->getCrowdCorridor(), ai->getCrowdSample(), ai->getCrowdLat() );

	if (fabs( mine - his ) > 1.0f)
		return his < mine;

	return other->getID() < self->getID();
}

//-------------------------------------------------------------------------------------------------
/**
 * Where across the route to drive this frame, and how fast.
 *
 * This is the whole crowd model, and it is one function on purpose: every rule in it reads the
 * same neighbour scan and writes the same two numbers, and the only two numbers the engine's
 * steering will accept are the point to aim at and the speed to aim at it with.  Retail has one
 * reactive rule (a collision, once it has already happened, caps the speed) and consequently a
 * group of twenty crosses a map in single file and stops dead in a doorway.
 *
 * Order matters and is the sandbox's: give way to something bigger coming up behind, then deal with
 * whatever is directly ahead - pass it if there is room, brake if there is not - then spread out,
 * but only while actually held up, then push apart from whoever is too close.  Separation last
 * because it is the smallest correction and has to win the tie; fanning out before it, because a
 * unit that has been stopped for a second wants a different lane and not a nudge.
 */
//-------------------------------------------------------------------------------------------------
/** Somewhere a wedged unit can back out to, or FALSE if it is walled in on every side.
		Sideways first and backwards second: the ground ahead is what it is already failing to drive
		through, and a unit that has been stationary for two seconds is in a hole its own route made.

		`backFirst` reverses that order, and the caller sets it wherever the road has no width - a
		bridge and the ground either side of it.  The riverbank next to an abutment is perfectly valid
		ground to stand on, so the sideways probe finds it, and a unit that backs out onto it has left
		the only queue that leads onto the bridge and has to fight its way back into the funnel.  That
		is the jam at the mouth of a bridge, and it is this function causing it.  Taking the sideways
		candidates away entirely rather than demoting them was measured and is worse: a unit that
		cannot back up either has nowhere left to go and stays wedged for the rest of the battle. */
//-------------------------------------------------------------------------------------------------
static Bool crowdFindEscape( Object *self, const LocomotorSet& locoSet, const Coord2D& tan,
														 Int firstSide, Bool backFirst, Coord3D *out )
{
	// P4/P5: the crowd tangent and the pathfinder's test are float
	const Coord3D posF = floatPosOf( self );
	const Coord3D *pos = &posF;
	const Real myR = floatRadiusOf( self );
	const Real across = myR * 2.0f + PATHFIND_CELL_SIZE_F;
	const Real behind = myR + PATHFIND_CELL_SIZE_F * 0.5f;
	const Bool crusher = self->getCrusherLevel() > 0;
	const PathfindLayerEnum layer = self->getLayer();

	const Real sx = -tan.y, sy = tan.x;			// left of the route
	for (Int k = 0; k < 4; k++)
	{
		// out and back to the roomier side, out and back the other way, straight out, straight back;
		// on a bridge the last of those is tried first and the sideways three only if it fails
		const Int t = backFirst ? ((k == 0) ? 3 : k - 1) : k;
		const Real side = (t == 1) ? -(Real)firstSide : (Real)firstSide;
		const Real lat = (t < 3) ? across : 0.0f;
		const Real back = (t < 2) ? behind : ((t == 2) ? 0.0f : across);

		Coord3D p;
		p.x = pos->x + sx * lat * side - tan.x * back;
		p.y = pos->y + sy * lat * side - tan.y * back;
		p.z = groundHeightAt( p.x, p.y );
		if (TheAI->pathfinder()->validMovementPosition( crusher, layer, locoSet, &p ))
		{
			*out = p;
			return TRUE;
		}
	}
	return FALSE;
}

//-------------------------------------------------------------------------------------------------
/** Is this unit getting anywhere?
 *
 * One test, once a frame, for everything that is trying to drive somewhere - not only for units the
 * crowd model happens to be steering.  It used to live inside crowdSteer, which meant it was only
 * ever asked about a unit that had been handed a lane by a group order, so the computer's armies
 * and every single unit a player ever ordered anywhere were never asked at all.
 *
 * Ground covered against ground asked for, and turning counts as covering ground while the hull is
 * still swinging round towards where it wants to go.  See the comment on the alignment test: any
 * turn at all excuses a tank rocking against a wall, and no turn at all condemns a tank that is
 * simply slow to come about.
 */
//-------------------------------------------------------------------------------------------------
void AIUpdateInterface::updateProgress( void )
{
	Object *self = getObject();
	// P4: progress is measured against locomotor speeds and turn rates, still float
	const Coord3D myPosF = floatPosOf( self );
	const Coord3D *myPos = &myPosF;
	const Real facing = fixToReal( self->getOrientationFix() );

	const Real dx = myPos->x - m_lastProgressPos.x;
	const Real dy = myPos->y - m_lastProgressPos.y;
	const Real moved = (Real)sqrt( dx * dx + dy * dy );
	const Real turned = (Real)fabs( normalizeAngle( facing - m_lastProgressAngle ) );

	m_lastProgressPos = *myPos;
	m_lastProgressAngle = facing;

	/* Not trying to go anywhere: nothing to be stuck at.

		 Aircraft are outside this, and the attempt to bring them in is worth recording.  They sit
		 outside the whole movement stack - no pathfinding, no collisions, no crowd, no traffic - so
		 nothing in the game can say whether an aircraft that wants to move is moving.  Opening this
		 gate to them measured 234.4 blocked unit-frames per 1000 against the ground battle's 244.9,
		 with six of eight drill seeds bit-identical and the whole difference in the two where
		 aircraft stalled, and it stayed at 244.9 after every rung of the ladder was closed to them
		 again.  So merely counting them changes the match by a route not traced here.  An aircraft
		 question needs a harness that orders aircraft about, which the drill does not, and until that
		 exists this gate stays shut rather than paying 4.5% of the ground battle for a number nobody
		 can yet act on. */
	if (!isDoingGroundMovement() || getPath() == NULL || isWaitingForPath()
				|| m_curLocomotor == NULL || m_locomotorGoalType != POSITION_ON_PATH)
	{
		m_noProgress = 0;
		m_rescueStage = 0;
		return;
	}

	const BodyDamageType damage = self->getBodyModule()->getDamageState();
	Real asked = m_curLocomotor->getMaxSpeedForCondition( damage );
	if (m_desiredSpeed < asked)
		asked = m_desiredSpeed;						// a unit told to go slowly is not a unit going nowhere
	if (asked < 0.01f)
	{
		m_noProgress = 0;
		m_rescueStage = 0;
		return;
	}

	Real turnBar = STUCK_TURN_EPSILON;
	const Real rate = m_curLocomotor->getMaxTurnRate( damage );
	if (rate * 0.5f > turnBar)
		turnBar = rate * 0.5f;

	Bool comingAbout = FALSE;
	if (turned >= turnBar)
	{
		// which way the hull is pointing against where the locomotor is being sent
		const Coord3D myDir = self->getUnitDirectionVector2DFix()->toCoord3D();
		const Real gx = m_locomotorGoalData.x - myPos->x;
		const Real gy = m_locomotorGoalData.y - myPos->y;
		const Real gl = (Real)sqrt( gx * gx + gy * gy );
		if (gl > 0.01f)
			comingAbout = ((myDir.x * gx + myDir.y * gy) / gl) < 0.87f;		// more than thirty degrees off
		else
			comingAbout = TRUE;
	}

	if (moved < asked * 0.2f && !comingAbout)
	{
		++m_noProgress;
		// counted from a third of a second on, so the odd frame lost to a collision is not a wedge
		if (m_noProgress > STUCK_PRESS_FRAMES)
			Pathfinder::bumpWedgeFrame( m_noProgress );
	}
	else
	{
		m_noProgress = 0;
		m_rescueStage = 0;
	}

	/* ---- and the other way of getting nowhere: going back and forth ----
		 The test above asks whether the unit moved.  A unit dithering beside a building moves on every
		 single frame, at full speed, and is exactly where it started three seconds later, so that test
		 passes it and no counter anywhere rises.

		 Ground covered against ground gained, over three seconds.  A unit driving somewhere has the
		 two nearly equal; one shuffling has covered several body lengths and gained nothing.  A first
		 attempt measured displacement against the speed asked for instead, which fired fifty times a
		 match on units that were merely slowing down to arrive and caught nothing: the ratio is the
		 discriminator, not the distance.

		 What it does about it is deliberately not "ask for a new route".  Repathing is what dithering
		 is made of - two routes of nearly equal cost round opposite sides of the same building, and a
		 unit that flips between them every time it is asked - so the answer is to get the body out to
		 open ground first and let the route be planned from there.  That is rung three, and this
		 hands the ladder straight to it. */
	const UnsignedInt now = TheGameLogic->getFrame();
	m_ditherTravel += moved;
	if (m_ditherFrame == 0 || now < m_ditherFrame)
	{
		m_ditherFrame = now;
		m_ditherFrom = *myPos;
		m_ditherTravel = 0.0f;
	}
	else if (now - m_ditherFrame >= (UnsignedInt)STUCK_DITHER_FRAMES)
	{
		const Real ddx = myPos->x - m_ditherFrom.x;
		const Real ddy = myPos->y - m_ditherFrom.y;
		const Real net = (Real)sqrt( ddx * ddx + ddy * ddy );
		const Real body = fixToReal( self->getGeometryInfo().getBoundingCircleRadiusFix() ) * 2.0f;

		/* This counts and does nothing else, on purpose, and the three things it used to do are
			 worth writing down because each of them sounded right.

			 Back the body out to open ground, on the theory that the ground is the problem: fired
			 about fifteen times a match and took blocked unit-frames from 234 to 317 per 1000 and
			 stalled orders from 0.2% to 1.1%.  Reversing a unit that is already going back and forth
			 adds a third direction to the argument and pushes the reversal into whoever is behind it.

			 Then, since dithering is made of changing your mind, stop it being asked: no new route and
			 no lane change for three seconds.  255 per 1000.  Better than reversing and still worse
			 than nothing, because a unit forbidden to change lanes in traffic is a unit queueing.

			 Then the same without the lane half, holding only the route: 245.  Still worse than the 234
			 of leaving it alone.  Three attempts, one direction, so the rule is a counter until
			 somebody has a fourth idea.  What it is worth is knowing the number: about fifteen units a
			 match under the drill drive a long way and gain nothing, and nobody could see that at all
			 before. */
		if (AIUpdate_isDithering( net, m_ditherTravel, body ))
			Pathfinder::bumpDither();

		/* The one case that is acted on: a unit on the ground beside or underneath a bridge its route
			 goes over, shaking on the spot.  It came in beside the ramp rather than onto it and is
			 pressing against the side of the ramp at full throttle for a spot on the deck: it moves
			 every frame, so no count of stuck frames ever rises for it, and it covers too little ground
			 to read as dithering either.  Two of twenty Crusaders over the long bridge on Golden Oasis
			 never got across for that.  A route asked for from where it stands leads it round to the
			 ramp. */
		const Bool shaking = net < body * 0.25f && m_ditherTravel > body;
		if (shaking && self->getLayer() == LAYER_GROUND && m_path != NULL && now >= m_rescueCool)
		{
			for (const PathNode *node = m_path->getFirstNode(); node != NULL; node = node->getNextOptimized())
			{
				const Real dx = node->getPosition()->x - myPos->x;
				const Real dy = node->getPosition()->y - myPos->y;
				if (node->getLayer() > LAYER_GROUND && dx * dx + dy * dy < STUCK_BRIDGE_NEAR * STUCK_BRIDGE_NEAR)
				{
					crowdRepath();
					break;
				}
			}
		}

		m_ditherFrame = now;
		m_ditherFrom = *myPos;
		m_ditherTravel = 0.0f;
	}
}

Bool AIUpdate_isDithering( Real net, Real travelled, Real bodySize )
{
	if (travelled < bodySize * 2.0f)
		return FALSE;						// barely moved at all: that is the other test's business
	return net < travelled * STUCK_DITHER_RATIO;
}

//-------------------------------------------------------------------------------------------------
/** The rescue ladder.
 *
 * The property this is here to provide: a unit that wants to move and is not moving has something
 * tried on it within a second and a half, and something else every second and a half after that,
 * ending in a rung that always does something and then starting again.  It cannot guarantee that a
 * unit never gets stuck - a unit walled in on four sides by buildings has nowhere to be sent - but
 * it does guarantee that nothing is left sitting there with the game ignoring it.
 *
 * The rungs are in order of how much they disturb: ask for a different route, ask the neighbours to
 * move and stop bouncing off them, back out to open ground.  Each fires once per episode.
 */
//-------------------------------------------------------------------------------------------------
Int AIUpdate_stuckRung( Int noProgressFrames, Int stageDone )
{
	/* The whole of the ladder's timing, as one function with no state in it, so the property it is
		 here to provide can be checked rather than asserted: every rung is reached in order, none is
		 skipped, and past the last one the ladder starts again instead of going quiet. */
	if (stageDone >= 3)
		return (noProgressFrames > STUCK_CYCLE_FRAMES) ? -1 : 0;
	if (noProgressFrames < STUCK_REPATH_FRAMES)
		return 0;
	return stageDone + 1;
}

void AIUpdateInterface::stuckRescue( void )
{
	if (m_rescueUntil > 0)
		return;												// already backing out; rescueSteer is driving

	const Int rung = AIUpdate_stuckRung( m_noProgress, m_rescueStage );
	if (rung == -1)
	{
		// still stuck after the last rung: run the whole ladder again rather than give up on it
		m_noProgress = 0;
		m_rescueStage = 0;
		return;
	}
	if (rung == 0)
		return;

	Object *self = getObject();
	const UnsignedInt now = TheGameLogic->getFrame();
	if (now < m_rescueCool)
		return;

	if (rung == 1)
	{
		/* Rung one: a different route, from where the unit actually is.  This is the cheapest thing
			 that can possibly help and it covers the two commonest causes at once - a route through
			 something that is never going to move, and a unit that has been shoved inside an obstacle,
			 which the search itself knows how to path out of. */
		m_rescueStage = 1;
		crowdRepath();
		return;
	}

	// nothing above the ground gets here at all: see the gate in updateProgress
	if (!isDoingGroundMovement())
		return;

	if (rung == 2)
	{
		/* Rung two: the neighbours.  Anything of ours standing within a body length is asked to move,
			 and collisions are ignored for a second so the unit can push through the gap that opens
			 instead of bouncing off the edge of it.  This is the engine's own pair of tools, used here
			 in the one place that knows the unit has got nowhere for three seconds. */
		m_rescueStage = 2;
		const Fix range = self->getGeometryInfo().getBoundingCircleRadiusFix() * Fix( 3 ) + Fix( PATHFIND_CELL_SIZE );

		PartitionFilterRelationship		fRel( self, PartitionFilterRelationship::ALLOW_ALLIES );
		PartitionFilterAlive					fAlive;
		PartitionFilterSameMapStatus	fMap( self );
		PartitionFilter *filters[] = { &fRel, &fAlive, &fMap, NULL };
		SimpleObjectIterator *iter = ThePartitionManager->iterateObjectsInRangeFix( self, range, FROM_CENTER_2D, filters );
		MemoryPoolObjectHolder hold( iter );

		for (Object *o = iter->first(); o; o = iter->next())
		{
			if (o == self)
				continue;
			AIUpdateInterface *ai = o->getAI();
			if (ai == NULL || !ai->isDoingGroundMovement() || ai->isBusy())
				continue;
			if (o->testStatus( OBJECT_STATUS_IS_USING_ABILITY ))
				continue;
			ai->aiMoveAwayFromUnit( self, CMD_FROM_AI );
		}

		setIgnoreCollisionTime( LOGICFRAMES_PER_SECOND );
		m_rescueCool = now + LOGICFRAMES_PER_SECOND;
		return;
	}

	/* Rung three: get the body out.  Sideways first and backwards second, because the ground ahead
		 is what it is already failing to drive through.  The direction is the one the locomotor is
		 being sent in, which is the route when there is one and the goal when there is not. */
	m_rescueStage = 3;

	// P4: the locomotor goal and the crowd tangent are float
	const Coord3D myPos = floatPosOf( self );
	Coord2D tan;
	tan.x = m_locomotorGoalData.x - myPos.x;
	tan.y = m_locomotorGoalData.y - myPos.y;
	if (tan.length() < 0.01f)
	{
		const Coord3D myDir = self->getUnitDirectionVector2DFix()->toCoord3D();
		tan.x = myDir.x;
		tan.y = myDir.y;
	}
	tan.normalize();

	const Int firstSide = (self->getID() & 1) ? 1 : -1;		// no band here to say which side has room
	if (crowdFindEscape( self, m_locomotorSet, tan, firstSide, FALSE, &m_rescueTo ))
	{
		// and the queue behind has to open up, or we are reversing into it
		crowdAskBehindToBackOff( tan );
		Pathfinder::bumpEscape();
		setIgnoreCollisionTime( LOGICFRAMES_PER_SECOND );
		m_rescueUntil = now + STUCK_BACKOUT_FRAMES;
		m_noProgress = 0;
	}
	else
	{
		// walled in on every side: the only thing left to change is where it is trying to go
		crowdRepath();
	}
}

//-------------------------------------------------------------------------------------------------
Bool AIUpdateInterface::rescueSteer( Coord3D& goalPos )
{
	if (m_rescueUntil == 0)
		return FALSE;

	Object *self = getObject();
	// P4: the rescue point is a float steering goal
	const Coord3D myPos = floatPosOf( self );
	const Real dx = m_rescueTo.x - myPos.x;
	const Real dy = m_rescueTo.y - myPos.y;
	const Real myR = floatRadiusOf( self );
	const Bool arrived = (Real)sqrt( dx * dx + dy * dy ) < myR + 2.0f;

	if (arrived || TheGameLogic->getFrame() >= m_rescueUntil)
	{
		m_rescueUntil = 0;
		m_rescueCool = TheGameLogic->getFrame() + STUCK_COOL_FRAMES;
		m_noProgress = 0;
		m_rescueStage = 0;
		/* Out of the hole, and standing beside a route that still leads back into it.  Without this
			 the backing out hands the unit straight back to whatever wedged it and the pair of them
			 take turns for the rest of the match. */
		crowdRepath();
		return FALSE;
	}

	goalPos = m_rescueTo;
	goalPos.z = groundHeightAt( goalPos.x, goalPos.y );
	return TRUE;
}

//-------------------------------------------------------------------------------------------------
/** Give up on this route and ask for another one, to the same place, from where the unit actually
		is now.

		Every rule in crowdSteer steers along a route somebody else chose, and there is a case none of
		them can answer: the route leads through a unit that is never going to move.  Sliding sideways
		does not help, braking does not help, and backing out only puts the unit at the end of the
		same route.  The only answer is a different route, and the traffic map has by then been
		stamped with exactly where the jam is, so the new one goes round it.

		It is asked for rather than taken: setting the blocked count past the two seconds
		AIInternalMoveToState already repaths on is how the move state is told, and it is deliberately
		not m_isBlockedAndStuck - that flag patches the old route locally, which is the answer to
		"something is in the way here" and not to "this route is no good".  Plain findPath from the
		current position, which is what the player means by pathfinding again. */
//-------------------------------------------------------------------------------------------------
void AIUpdateInterface::crowdRepath( void )
{
	Pathfinder::bumpCrowdRepath();
	m_blockedFrames = 2 * LOGICFRAMES_PER_SECOND + 1;
	m_repathAsked = TRUE;
	m_isBlockedAndStuck = FALSE;
	m_crowdQueued = 0;
	m_noProgress = 0;
	// one drastic thing at a time: the same cooldown that stops a wedged pair rocking forever
	m_rescueCool = TheGameLogic->getFrame() + STUCK_COOL_FRAMES;
}

//-------------------------------------------------------------------------------------------------
/** Ask whoever is queued directly behind us to back off.

		A unit at the head of a jam cannot reverse out of it: the queue behind it is what it would be
		reversing into, and that queue is the reason it has no room to turn either.  So before backing
		out it says so, and the engine's own "get out of my way" order does the rest.  Two bodies
		back, no further: the whole column peeling away for one wedged tank is how a jam turns into a
		rout.  This is the sandbox's jam protocol with the column arithmetic left out - the engine
		already has an order for making room, and it does not need a second one. */
//-------------------------------------------------------------------------------------------------
void AIUpdateInterface::crowdAskBehindToBackOff( const Coord2D& tan )
{
	Object *self = getObject();
	const Fix myRFix = self->getGeometryInfo().getBoundingCircleRadiusFix();
	const Real myR = fixToReal( myRFix );		// P4: the tangent is the crowd model's, in float

	PartitionFilterRelationship		fRel( self, PartitionFilterRelationship::ALLOW_ALLIES );
	PartitionFilterAlive					fAlive;
	PartitionFilterSameMapStatus	fMap( self );
	PartitionFilter *filters[] = { &fRel, &fAlive, &fMap, NULL };
	SimpleObjectIterator *iter = ThePartitionManager->iterateObjectsInRangeFix( self, myRFix * Fix( 4 ) + Fix( PATHFIND_CELL_SIZE ), FROM_CENTER_2D, filters );
	MemoryPoolObjectHolder hold( iter );

	const FCoord3D *myPos = self->getPositionFix();
	for (Object *o = iter->first(); o; o = iter->next())
	{
		if (o == self)
			continue;
		AIUpdateInterface *ai = o->getAI();
		if (ai == NULL || !ai->isDoingGroundMovement() || ai->isBusy())
			continue;
		if (o->testStatus( OBJECT_STATUS_IS_USING_ABILITY ))
			continue;

		const FCoord3D *hp = o->getPositionFix();
		const Real dx = fixToReal( hp->x - myPos->x );
		const Real dy = fixToReal( hp->y - myPos->y );
		const Real fwd = dx * tan.x + dy * tan.y;
		if (fwd > -myR)
			continue;					// beside us or in front: not what is boxing us in
		const Real side = -dx * tan.y + dy * tan.x;
		const Real hisR = floatRadiusOf( o );
		if (fabs( side ) > myR + hisR)
			continue;					// in the next lane, not behind us

		ai->aiMoveAwayFromUnit( self, CMD_FROM_AI );
	}
}

//-------------------------------------------------------------------------------------------------
void AIUpdateInterface::crowdSteer( Coord3D& goalPos, Real& speed )
{
	Object *self = getObject();
	Path *path = getPath();
	if (path == NULL || m_curLocomotor == NULL)
		return;

	/* Only units that were sent somewhere as a group.  A group order hands every member a lane
		 before the paths exist, and that hand-out is the flag: no lane handed down, no band, and the
		 unit drives retail's centre line.  A unit crossing a map on its own has nobody to share the
		 road with, and the rules below then cost it real time - they are written to resolve traffic,
		 and a single vehicle repositioning inside a firefight is not traffic. */
	if (!m_hasPendingCrowdLat && !m_crowdLatValid)
		return;

	/* Foot soldiers get no band and no traffic rules, on either side of the question.  Infantry walks
		 through infantry - the bodies interpenetrate, retail lets them, and a squad sent across a map
		 is meant to arrive as a squad on one line rather than as a rank spread over sixty feet of
		 road.  Every rule below is written for bodies that cannot share ground: give way, queue,
		 brake, fan out.  Applied to a platoon they are a machine for pulling it apart, which is what a
		 group of riflemen ordered across open ground was visibly doing.  A vehicle does not get the
		 rules for them either (the scan below skips them): it drives over them, and braking for a man
		 it is about to walk through is a stop for nothing.

		 What a foot soldier keeps is the wedge rescue.  Returning out of this function for infantry
		 was measured and it is not free: it takes the rescue away with everything else, and stuck
		 units went from 1.3 a match to 16.0.  So the ladder below still runs for him and only the
		 lane is held at nothing. */
	const Bool foot = self->isKindOf( KINDOF_INFANTRY );

	/* The band is measured once per route.  m_crowdSample = -1 is the latch for a route too short
		 to have one, without which a unit driving the last two cells of its path re-probes two
		 hundred cells every frame for the rest of its life. */
	if (m_corridor == NULL)
	{
		if (m_crowdSample < 0)
			return;
		CrowdCorridor *corr = new CrowdCorridor;
		if (!corr->build( self, m_locomotorSet, path ))
		{
			delete corr;
			m_crowdSample = -1;
			return;
		}
		m_corridor = corr;
		m_crowdSample = 0;
		// the lane is not cleared here: destroyPath already did that for a new route, and a save
		// reloaded mid-drive has to come back in the lane it was saved in rather than at the centre
	}

	// P4: the crowd model steers in float, the corridor and the lanes with it
	const Coord3D myPosF = floatPosOf( self );
	const Coord3D *myPos = &myPosF;
	const Int i = m_corridor->nearest( *myPos, m_crowdSample );
	m_crowdSample = i;

	const CrowdCorridor::Sample& here = m_corridor->at( i );
	const Real myR = floatRadiusOf( self );

	/* The last cell of the route is arrival, and arrival is not a formation problem.  Only the last
		 cell: standing the rules down over the last four instead - on the theory that a short hop
		 inside a firefight is not a march - measures three times worse (298 blocked unit-frames per
		 1000 against 108).  Short hops are exactly where units are packed tightest. */
	if (m_corridor->length() - here.along < PATHFIND_CELL_SIZE_F)
		return;

	if (!m_crowdLatValid)
	{
		/* A unit ordered on its own rides wherever it is already standing, which for a route that
			 starts under its own tracks is the centre line - retail's answer, and the right one.  A
			 unit ordered as part of a group was told where to sit before the path existed. */
		m_crowdLat = m_hasPendingCrowdLat ? m_pendingCrowdLat : m_corridor->latOf( i, *myPos );
		if (!m_hasPendingCrowdLat)
			m_crowdLaneOf = 0;			// and it belongs to no rank, so nothing re-fits it to the road
		m_hasPendingCrowdLat = FALSE;
		m_crowdLatValid = TRUE;
		m_crowdSide = (here.left >= here.right) ? 1 : -1;
	}

	const UnsignedInt now = TheGameLogic->getFrame();

	/* How long this unit has been asking to move and not moving is not counted here any more, and
		 neither is the backing out that answers it.  Both live in updateProgress and stuckRescue,
		 which run for every unit that is trying to drive somewhere rather than only for the ones a
		 group order happened to hand a lane to.  All that is left of it here is the courtesy switch:
		 a unit that has got nowhere for a third of a second has already given way, braked and queued,
		 and every one of those is now costing it the speed it needs to push through. */
	const Bool pressing = m_noProgress > STUCK_PRESS_FRAMES;

	//--- one scan, every rule reads it -------------------------------------------------------------
	const Real scanRange = PATHFIND_CELL_SIZE_F * 4.0f + myR;
	const Real lookAhead = PATHFIND_CELL_SIZE_F * (Real)CROWD_LOOKAHEAD_CELLS + myR;

	PartitionFilterRelationship		fRel( self, PartitionFilterRelationship::ALLOW_ALLIES );
	PartitionFilterAlive					fAlive;
	PartitionFilterSameMapStatus	fMap( self );
	PartitionFilter *filters[] = { &fRel, &fAlive, &fMap, NULL };
	// a foot soldier reads nobody: he has no lane to hold and nothing on the road is his traffic
	SimpleObjectIterator *iter = foot ? NULL
		: ThePartitionManager->iterateObjectsInRangeFix( self, fixFromReal( scanRange ), FROM_CENTER_2D, filters );
	MemoryPoolObjectHolder hold( iter );

	Real sep = 0.0f;					// how far sideways the crowd is pushing us
	Object *blocker = NULL;		// nearest thing directly in the way
	Real blockerFwd = 0.0f;
	Real blockerGap = 0.0f;
	Real blockerSpeed = 0.0f;
	Bool blockerMoving = FALSE;
	Bool giveWay = FALSE;
	Real giveWayLat = 0.0f;
	Int rank = 0;							// how many held-up neighbours are ahead of us in the queue
	Bool touching = FALSE;		// somebody's body is inside ours right now
	Bool crossing = FALSE;		// somebody outside our lane will reach us if we both keep going
	Object *joiner = NULL;		// somebody coming in from the side who will cross our line
	Real joinerSide = 0.0f;
	Real joinerR = 0.0f;
	Real joinerWhen = 1.0e9f;

	/* Where the traffic in front of us sits across the road, which is the question a pass has to ask
		 and used to not.  A pass aimed a body and a half clear of the unit directly ahead is aimed at
		 whatever is standing beside that unit, and in a pack six wide every such slot is taken: the
		 unit picks one, gets shoved back out of it by the collision push, and queues in the middle of
		 its own group instead of going round the outside of it. */
	enum { CROWD_NEAR_MAX = 16 };
	Real nearLat[CROWD_NEAR_MAX];		// each neighbour's line across the road
	Real nearR[CROWD_NEAR_MAX];
	Int nearCount = 0;
	Real packLo = 0.0f, packHi = 0.0f;		// the outside edges of the whole pack ahead
	Bool packAny = FALSE;

	PhysicsBehavior *myPhys = self->getPhysics();
	Coord3D myVel;
	myVel.zero();
	if (myPhys != NULL)
		myVel = *myPhys->getVelocity();

	for (Object *o = (iter != NULL) ? iter->first() : NULL; o; o = iter->next())
	{
		if (o == self)
			continue;
		AIUpdateInterface *ai = o->getAI();
		if (ai == NULL || !ai->isDoingGroundMovement())
			continue;

		// infantry is nobody's traffic: see the gate at the top of this function
		if (o->isKindOf( KINDOF_INFANTRY ))
			continue;

		const Coord3D hpF = floatPosOf( o );
		const Coord3D *hp = &hpF;
		const Real dx = hp->x - myPos->x;
		const Real dy = hp->y - myPos->y;
		const Real fwd = dx * here.tan.x + dy * here.tan.y;			// along our route
		const Real side = -dx * here.tan.y + dy * here.tan.x;		// across it, left positive
		const Real hisR = floatRadiusOf( o );
		const Real gap = (Real)sqrt( dx * dx + dy * dy ) - myR - hisR;
		const Real comfort = myR + hisR + CROWD_AIR;

		PhysicsBehavior *hisPhys = o->getPhysics();
		const Bool hisMoving = ai->isMoving() && hisPhys != NULL && hisPhys->getVelocityMagnitude() > 0.05f;

		if (gap < 0.0f)
			touching = TRUE;

		/* Somebody joining our road from the side, which is the case braking handles worst: he is not
			 in front of us yet, so nothing slows down, and by the time he is, both of us are in the same
			 square.  Moving over for him costs nothing and turns a queue into a zipper.

			 The test is a real closest approach on the two current courses, not a cone.  Anything looser
			 and a column shuffles sideways for every unit driving vaguely alongside it, which costs more
			 speed than the braking it replaces. */
		if (hisMoving && fabs( side ) > (myR + hisR) * 0.5f && hisPhys != NULL)
		{
			const Coord3D *hv = hisPhys->getVelocity();
			const Real hs = (Real)sqrt( hv->x * hv->x + hv->y * hv->y );
			if (hs > 0.01f)
			{
				const Real align = (hv->x * here.tan.x + hv->y * here.tan.y) / hs;
				// a merge comes in at an angle: parallel traffic is the stream itself, head-on is not a merge
				if (align > 0.35f && align < 0.8f)
				{
					const Real rvx = hv->x - myVel.x;
					const Real rvy = hv->y - myVel.y;
					const Real rvv = rvx * rvx + rvy * rvy;
					if (rvv > 1.0e-6f)
					{
						const Real when = -(dx * rvx + dy * rvy) / rvv;			// frames to closest approach
						if (when >= 0.0f && when < (Real)CROWD_MERGE_FRAMES && when < joinerWhen)
						{
							const Real mx = dx + rvx * when;
							const Real my = dy + rvy * when;
							if ((Real)sqrt( mx * mx + my * my ) < myR + hisR + 0.8f && crowdOutranksMe( o ))
							{
								joiner = o;
								joinerSide = side;
								joinerR = hisR;
								joinerWhen = when;
							}
						}
					}
				}
			}
		}

		/* Push apart from anybody inside the comfort radius, not only from whoever is exactly
			 abreast.  The abreast test was the whole of the separation rule and it is the one case
			 the collision push already handles: two bodies side by side are touching, and physics
			 sorts that out.  What it missed is the case boids is actually for - a neighbour half a
			 body ahead and half a body over, converging - where nothing is touching yet and a foot
			 of lane now is worth a stop later.  Weighted by how far inside the radius it is, squared,
			 so a distant neighbour is nearly free and a close one dominates. */
		if (gap < comfort && comfort > 0.01f)
		{
			/* How much of "away from him" is sideways, which for a unit directly ahead is none of it.
				 That matters: the lane is the only thing this push can move, so a follower nose to tail
				 with the unit in front has nothing to say about which way to steer, and saying
				 something anyway is a column that snakes. */
			const Real dist = (Real)sqrt( dx * dx + dy * dy );
			Real dir;
			if (dist > 0.01f && fabs( side ) > 0.01f)
				dir = -side / dist;
			else if (fabs( fwd ) < myR + hisR)
				dir = (o->getID() < self->getID()) ? -1.0f : 1.0f;	// exactly abreast; somebody has to pick
			else
				dir = 0.0f;

			Real w = (comfort - ((gap > 0.0f) ? gap : 0.0f)) / comfort;
			sep += dir * w * w * comfort;
		}

		/* Something bigger coming up behind in our lane.  It cannot go round us in the room it has
			 and it will not stop, so the courtesy is ours to extend, and extending it costs us a lane
			 rather than a stop. */
		if (!giveWay && hisMoving && fwd < 0.0f && Crowd_outranks( o, self )
					&& fabs( side ) < myR + hisR + CROWD_AIR)
		{
			const Real away = myR + hisR + CROWD_AIR;
			Real dir;
			if (fabs( side ) > 0.01f)
				dir = (side > 0.0f) ? -1.0f : 1.0f;
			else
				dir = (Real)m_crowdSide;
			/* Clear of *his* line, not of wherever we happen to be.  Stepping aside from our own
				 current lane looks the same for one frame and is not the same thing at all: the lane it
				 produces is the input to the next frame's step, so a unit given way to for a second walks
				 itself out to the edge of the band a body width at a time. */
			giveWayLat = m_corridor->latOf( i, *hp ) + dir * away;
			giveWay = TRUE;
		}

		/* Anything abreast or in front, wherever it sits across the road.  This is the set the pass
			 below is aimed clear of, so it is deliberately wider than the "in our way" test under it:
			 the unit two lanes over is not blocking us and is exactly the one we must not aim at. */
		if (fwd > -myR && fwd < lookAhead + hisR)
		{
			const Real hisLatHere = m_corridor->latOf( i, *hp );
			if (nearCount < CROWD_NEAR_MAX)
			{
				nearLat[nearCount] = hisLatHere;
				nearR[nearCount] = hisR;
				++nearCount;
			}
			const Real lo = hisLatHere - hisR;
			const Real hi = hisLatHere + hisR;
			if (!packAny || lo < packLo) packLo = lo;
			if (!packAny || hi > packHi) packHi = hi;
			packAny = TRUE;
		}

		/* Somebody we are closing on who is not in our lane: a crossing, not a queue.  Time to
			 contact rather than distance, so the neighbour drifting across our nose two bodies away
			 counts and the one sitting at the same distance going the same way does not.

			 This eases us off and nothing more.  Letting a crossing unit become the blocker below was
			 tried on 2026-09-04 and is the reason units stopped dead for no visible reason: every rule
			 under `blocker` reads the blocker's speed *along our own route*, which for something
			 crossing us is about zero, so a car passing across our nose was priced exactly like a
			 wreck sitting in our lane and the brake took us to a standstill.  Crossing traffic is what
			 the merge rule above is for; all that is wanted here is a lift off the throttle. */
		if (hisPhys != NULL && fwd > 0.0f && fabs( side ) >= myR + hisR + 1.0f)
		{
			const Coord3D *hv = hisPhys->getVelocity();
			const Real dist = (Real)sqrt( dx * dx + dy * dy );
			if (dist > 0.01f)
			{
				const Real closing = ((myVel.x - hv->x) * dx + (myVel.y - hv->y) * dy) / dist;
				if (closing > 0.05f && gap < closing * (Real)CROWD_TTC_FRAMES)
					crossing = TRUE;
			}
		}

		if (fwd > 0.0f && fwd < lookAhead + hisR && fabs( side ) < myR + hisR + 1.0f)
		{
			if (blocker == NULL || fwd < blockerFwd)
			{
				blocker = o;
				blockerFwd = fwd;
				blockerGap = gap;
				blockerSpeed = 0.0f;
				blockerMoving = hisMoving;
				if (hisPhys != NULL)
				{
					const Coord3D *v = hisPhys->getVelocity();
					blockerSpeed = v->x * here.tan.x + v->y * here.tan.y;		// his speed our way, not his speed
					if (blockerSpeed < 0.0f)
						blockerSpeed = 0.0f;
				}
			}
		}

		// our place in the queue, which is how far out we fan when it stops moving
		if (gap < comfort * 2.0f && crowdOutranksMe( o ))
			++rank;
	}

	//--- the rules, in order -----------------------------------------------------------------------
	// nothing was scanned for a foot soldier, so every rule below stands down on its own; the lane is
	// the one thing that has to be said out loud, and it is the middle of the road
	Real lat = foot ? 0.0f : m_crowdLat;

	/* How wide the group is, decided here and not once at the group's feet.  The old answer came from
		 two probes taken where the group was standing when the order was given, and it was then the
		 group's width for the whole trip: a dozen tanks leaving a base through a gate were handed one
		 lane and drove the next thousand feet in single file across open ground, because nothing asked
		 the question a second time.  So what the group hands out is a place in a queue - idx out of of,
		 sorted across - and the number of lanes is worked out every frame from the band at this unit's
		 own sample.  The road decides, sample by sample: the ranks close up going into a doorway and
		 open out again on the far side, and neither of those is a decision anybody makes.

		 The blocking is the group's, unchanged: whoever was left of somebody stays left of them, and
		 the ones sharing a lane follow each other.  It has to be, or the ranks would renumber every
		 time the road changed width and the group would shuffle sideways for the whole drive.

		 A lane taken to pass somebody or to give way outranks the slot while its hold lasts, or the
		 unit would slide back into the blocker it just pulled out around: the hold is the one thing
		 that says "I am not where I belong on purpose". */
	Int laneFit = 0, laneMine = 0;
	if (!foot && now >= m_crowdHoldFrame && m_crowdLaneOf > 1 && m_crowdLaneSpace > 0.001f)
	{
		/* With a deadband, because the raw count is a floor of a measurement that moves sample to
			 sample.  A band a hair over five lanes wide reads five, four, five, four along a perfectly
			 ordinary stretch of road, and every flip renumbers the whole group: half the units are told
			 to move half a lane sideways, then back, for the length of the road.  Widening therefore
			 wants half a lane of room over the rank it is claiming; narrowing takes effect at once,
			 because when the ground is gone it is gone. */
		const Real span = here.left + here.right;
		Int want = Crowd_laneCount( span, m_crowdLaneSpace, m_crowdLaneOf );

		if (m_crowdFit <= 0)
			m_crowdFit = want;
		else if (want < m_crowdFit)
			m_crowdFit = want;
		else if (want > m_crowdFit && span >= ((Real)(want - 1) + 0.5f) * m_crowdLaneSpace)
			m_crowdFit = want;

		laneFit = m_crowdFit;
		laneMine = (m_crowdLaneIdx * laneFit) / m_crowdLaneOf;
		const Real bias = (here.left - here.right) * 0.5f;		// the middle of the ground, not of the route
		lat = bias + ((Real)laneMine - (Real)(laneFit - 1) * 0.5f) * m_crowdLaneSpace;
	}
	Real cap = speed;
	Bool queued = FALSE;
	Bool merged = FALSE;

	if (giveWay && !pressing)
	{
		lat = giveWayLat;
		m_crowdHoldFrame = now + CROWD_HOLD_FRAMES;
	}

	/* Make room for the joiner rather than braking for him, and only for one we would have had to
		 brake for anyway - crowdOutranksMe already asked that question.  A unit that is itself getting
		 nowhere extends no courtesies: it has none to spare. */
	if (joiner != NULL && !pressing)
	{
		const Real shift = myR + joinerR + 0.5f;
		const Real want = lat - ((joinerSide > 0.0f) ? shift : -shift);
		if (fabs( m_corridor->clampLat( i, want ) - want ) < 0.5f)
		{
			lat = want;
			m_crowdHoldFrame = now + CROWD_HOLD_FRAMES;
			merged = TRUE;
		}
	}

	/* Passing is tried whether or not this unit is getting anywhere; only the braking and the
		 courtesies stand down when it is not.  Standing the whole block down under `pressing` was
		 the reason a held-up unit drove into the back of whatever was in front of it and stayed
		 there: at ten frames of no progress it stopped looking for a way round, which is precisely
		 the moment it needs one.  Nothing here costs speed - a pass is a lane, not a stop. */
	if (blocker != NULL)
	{
		if (!crowdOutranksMe( blocker ))
		{
			// we have right of way; he is the one who has to move, and we only avoid rear-ending him
			if (blockerMoving && blockerGap < myR && blockerSpeed < cap && !pressing)
				cap = blockerSpeed;
		}
		else
		{
			const Real hisR = floatRadiusOf( blocker );
			const Real hisLat = m_corridor->latOf( i, floatPosOf( blocker ) );

			/* Pass on the outside of a bend.  The inside is where the road runs out, and a unit that
				 dives up the inside of a turn to get past somebody arrives at the apex with a wall on
				 one side and the unit it just passed on the other. */
			const Real curv = m_corridor->curvature( i );
			Int first = (fabs( curv ) > 0.08f) ? ((curv > 0.0f) ? -1 : 1) : m_crowdSide;

			/* Go round rather than sit behind, and mean it.  Two gates used to send a unit to the back
				 of a queue that a foot of road either side would have let it out of.

				 The first was the lane hold.  It is there so a lane cannot flap, and a unit that gave way
				 to somebody a second ago is exactly the unit now stuck behind the next one along - the
				 hold was answering "do not change your mind" to a question nobody asked twice.  Once a
				 unit has been queueing for a third of a second, the hold does not get a vote.

				 The second was the clearance.  Six units of air beside the blocker is what a comfortable
				 pass wants, and a road that has five is not a road you queue on: the tight figure is a
				 body and a half of air, which is still a pass and is still wider than the collision push
				 that would otherwise decide it.  Full clearance is tried on both sides before the tight
				 one is tried on either, so the comfortable pass still wins wherever there is room.

				 Between those two comes going round the outside of the whole pack, and the first pass
				 over the candidates takes one only if no neighbour is already sitting on it.  Without
				 that test the road is the only thing consulted, so in a group six abreast the clear slot
				 beside the blocker is another tank and the pass is a swap of positions inside the same
				 knot.  With one unit in front the outside of the pack and the tight pass beside him are
				 the same line, so nothing changes for the ordinary overtake; it is the pack that goes
				 wide.

				 Then the same candidates again with the occupancy test off: refusing every occupied line
				 sounds right and means a unit with nowhere clean to go queues instead of moving into a
				 gap that is about to open.

				 The whole of it - the pack candidates and the occupancy test together - was worth
				 nothing at all while the group was one lane wide, and measured on its own then it cost
				 stuck units.  It pays once the group is as wide as the road: taking it back out again
				 costs 5065.6 blocked unit-frames a match against 4963.8 with it in, over 38 seeds, and
				 twenty-three of those seeds do not notice either way.  It is the pack that goes wide. */
			Bool took = FALSE;
			const Bool retry = m_crowdQueued > CROWD_PASS_RETRY;
			if (now >= m_crowdHoldFrame || retry)
			{
				const Real clearShift = myR + hisR + CROWD_PASS_CLEAR;
				const Real tightShift = myR + hisR + CROWD_PASS_TIGHT;
				const Real outside = myR + CROWD_PASS_TIGHT;

				Real cands[6];
				Int nCand = 0;
				cands[nCand++] = hisLat + (Real)first * clearShift;
				cands[nCand++] = hisLat - (Real)first * clearShift;
				if (packAny)
				{
					cands[nCand++] = (first > 0) ? (packHi + outside) : (packLo - outside);
					cands[nCand++] = (first > 0) ? (packLo - outside) : (packHi + outside);
				}
				cands[nCand++] = hisLat + (Real)first * tightShift;
				cands[nCand++] = hisLat - (Real)first * tightShift;

				for (Int pass = 0; pass < 2 && !took; pass++)
				{
					for (Int c = 0; c < nCand && !took; c++)
					{
						const Real want = cands[c];
						if (fabs( m_corridor->clampLat( i, want ) - want ) >= 0.5f)
							continue;					// the road does not go there

						if (pass == 0)
						{
							Bool taken = FALSE;
							for (Int n = 0; n < nearCount && !taken; n++)
							{
								if ((Real)fabs( nearLat[n] - want ) < myR + nearR[n])
									taken = TRUE;	// somebody is already in it
							}
							if (taken)
								continue;
						}

						lat = want;
						m_crowdSide = (want >= hisLat) ? 1 : -1;
						m_crowdHoldFrame = now + CROWD_HOLD_FRAMES;
						took = TRUE;
					}
				}
			}

			if (!took)
			{
				queued = TRUE;

				/* Brake behind a unit that is going somewhere, and never behind one that is not.
					 A parked ally on the route is not traffic, it is an obstacle, and the engine already
					 knows what to do about obstacles: drive into it, count the blocked frames, repath.
					 Braking short of it instead means the collision never happens, m_blockedFrames never
					 rises, and every piece of retail's stuck machinery sits idle while the column dies
					 politely a metre behind a tank that is never going to move.  That one line was worth
					 16593 blocked unit-frames against a baseline of 507. */
				/* And then only when we are actually going to hit him.  Braking by distance slows the
					 whole march: a unit ten units behind traffic moving at its own speed is not catching
					 anybody up, and pricing that gap costs 60% of the column's speed for nothing.  Closing
					 speed and the time it leaves is the only thing worth reading. */
				if (blockerMoving && !pressing)
				{
					const Real want = Crowd_brakeSpeed( speed, blockerSpeed, blockerGap, CROWD_BRAKE_FRAMES );
					if (want < cap)
						cap = want;
				}
			}
		}
	}

	if (queued)
		++m_crowdQueued;
	else if (m_crowdQueued > 0)
		--m_crowdQueued;

	/* Queued behind something that is not going anywhere, for the best part of a second, with no
		 way round it.  Everything above has already been tried by now - both sides of the blocker,
		 the outside of the whole pack, the tight pass - so the route itself is the problem, and the
		 sandbox's answer is the one taken here: ask for another one.  It is cheap because it is rare
		 (once every three seconds per unit at the very most) and it is the difference between a
		 column that goes round a wreck and a column that parks behind it. */
	if (m_crowdQueued > CROWD_REPATH_QUEUE && blocker != NULL && !blockerMoving
				&& now >= m_rescueCool && Crowd_remaining( self ) > PATHFIND_CELL_SIZE_F * 3.0f)
	{
		crowdRepath();
	}

	/* Fanning out is only worth anything while stopped.  Doing it all the time is a formation, and
		 a formation held across a map is what drives a group into every obstacle sideways-on; the
		 sandbox spreads out at the back of a jam and closes up again the moment it clears. */
	if (m_crowdQueued > CROWD_FAN_FRAMES && !pressing)
	{
		/* Two bodies off the line is a wide road.  Multiplying by the whole queue rank is how the
			 eighth unit in a jam ends up eighty units into the scenery, still politely queueing. */
		Int step = rank + 1;
		if (step > 2) step = 2;
		const Real target = (Real)m_crowdSide * (2.0f * myR + CROWD_PASS_CLEAR) * (Real)step;
		if (target > lat + 0.1f)
			lat += CROWD_FAN_RATE;
		else if (target < lat - 0.1f)
			lat -= CROWD_FAN_RATE;
	}

	/* Separation goes into the lane and stays there.  Steering with it frame by frame and leaving
		 the lane where it was reads better on paper - a shove is not a decision - and measures worse:
		 3312 blocked unit-frames against 1236 over the same eight seeds.  A shove that is forgotten
		 has to be paid again every frame, and two units abreast in a narrow band spend the whole
		 drive rediscovering each other. */
	/* Through a filter on the way in, though.  The raw push is the sum over whoever happens to be
		 inside the comfort radius this frame, and that set changes every frame: one neighbour drifting
		 in and out of range flips the push by a body width and back again, and the lane - and with it
		 the point the unit is aiming at - shimmers. */
	m_crowdSepSmooth += (sep - m_crowdSepSmooth) * CROWD_SEP_FILTER;
	sep = m_crowdSepSmooth;
	if (sep > CROWD_SEP_STEP) sep = CROWD_SEP_STEP;
	if (sep < -CROWD_SEP_STEP) sep = -CROWD_SEP_STEP;
	lat += sep;

	/* Everything above chose a lane; this is the only place the unit is allowed to move into one,
		 and never faster than it drives.  Giving way and passing both name a lane a full body width
		 away, and taking it in one frame swings the aim point twenty-odd feet sideways two cells in
		 front of the tracks - which is a turn no tank can make, so the unit stops and rotates on the
		 spot, and by the time it is facing the new lane the rule that asked for it has moved on.  The
		 spinning and the rocking back and forth are both this.  A quarter of forward speed sideways
		 is about twenty degrees of steering, which a tank takes without stopping. */
	const Real laneRate = (speed * 0.25f > 0.4f) ? speed * 0.25f : 0.4f;
	Real move = m_corridor->clampLat( i, lat ) - m_crowdLat;
	if (move > laneRate) move = laneRate;
	if (move < -laneRate) move = -laneRate;
	m_crowdLat = m_corridor->clampLat( i, m_crowdLat + move );

	/* The gate rule is deliberately not here.  The sandbox holds a unit in the open in front of a
		 one-body-wide doorway whose far side is plugged, so the column jams outside the doorway
		 rather than in it, and that was ported on 2026-09-04 and taken straight back out on the same
		 day.  Two things were wrong with it in this engine and only the first is fixable.  It counted
		 a parked ally as a plug, and units that have arrived stand around near narrow ground all the
		 time, so ordinary traffic was being held for units that were never going to move.  Worse, the
		 hold is a stop, a stop makes m_crowdStuck climb, ten frames of that sets `pressing`, and
		 `pressing` turns the rule off - so the unit lurched forward, stopped, lurched forward, at
		 about three times a second.  A rule whose own failure mode is a stutter has to be measured
		 before it goes in, not after, and this one had never been measured on its own. */

	/* Somebody is going to cross in front of us: lift off, do not stop.  See the scan above for why
		 this is a cap of its own rather than another blocker. */
	if (crossing)
	{
		const Real ease = speed * 0.6f;
		if (ease < cap)
			cap = ease;
	}

	/* No easing off for a bend.  There was a rule here that cut the speed to as little as 55% wherever
		 the route turned within four samples, and the corners of a pathfinder route are everywhere: a
		 quarter of all crowd frames on the choke probe were under it, at an average of two thirds of
		 full speed, and a unit with the road to itself ran at 0.92.  The locomotor already slows for a
		 turn it cannot make.  Taking the rule out brought the last of 20 Crusaders in after 2136 frames
		 instead of 2581, with blocked unit-frames 3965 against 5815 and wedged 2439 against 3890. */

	//--- and finally, the two numbers the locomotor takes ------------------------------------------
	/* How far ahead to steer.  The distance is the old one, two cells and a body; what changed is
		 where it is measured from.  Taking the point at a sample index quantises it: the aim jumped a
		 whole cell forward every time the unit crossed a sample boundary, and on anything but a
		 straight the direction handed to the locomotor stepped with it.  It runs off the unit's own
		 unquantised distance along the route now, and the point is taken between samples, so it slides.

		 Making the distance itself a travel time was tried and reverted.  It cures the same wobble and
		 costs four times the stuck units: on forty maps, 4667 blocked unit-frames a match and 1.4 stuck
		 became 5159 and 5.4, and clamping the horizon where the route bends recovered none of it (29 of
		 39 seeds came back bit-identical).  A point fifty feet up the road is measured against ground
		 fifty feet up the road, and the narrow bit in between is not consulted by anybody.  The chassis
		 difference is dealt with in the aim filter below instead, where being wrong only costs lag. */
	const Real myAlong = m_corridor->alongOf( i, *myPos );
	const Real routeLen = m_corridor->length();

	Real wantAlong = myAlong + lookAhead;
	if (wantAlong > routeLen) wantAlong = routeLen;

	/* The point has to be in front of the unit, and being in front along the route is not the same
		 thing.  A unit shoved sideways out of a queue, or one carried past its own lookahead through a
		 corner, gets handed a point behind its own tracks, and the locomotor turns round and drives at
		 it: that is the rocking back and forth, and at a corner, where the route is already round the
		 bend, it is the rotating on the spot.  Walk forward until the point is ahead. */
	for (;;)
	{
		Real outLat = m_crowdLat;
		const Real leftToRun = routeLen - wantAlong;
		if (leftToRun < CROWD_TAPER_DIST)
			outLat *= leftToRun / CROWD_TAPER_DIST;	// the band closes on the destination, so the group arrives together

		/* Against the narrowest ground between here and there, not against the ground at either end.
			 The unit drives the straight line to this point, and a band that is wide where it is
			 standing and wide where it is aiming can be a body's width in the middle: that line is
			 then through the corner of the building the route went round, and the unit arrives at it
			 sideways and stops.  This is what "they get caught on obstacles" looked like from inside
			 the code. */
		m_corridor->pointAt( wantAlong, m_corridor->clampLatNarrowest( myAlong, wantAlong, outLat ), &goalPos );

		const Real ahead = (goalPos.x - myPos->x) * here.tan.x + (goalPos.y - myPos->y) * here.tan.y;
		if (ahead >= myR || wantAlong >= routeLen)
			break;

		wantAlong += PATHFIND_CELL_SIZE_F;
		if (wantAlong > routeLen) wantAlong = routeLen;
	}

	/* ---- and the aim goes through a filter of its own ----
		 Every rule above moves the point a little, the sample the point is taken from hops forward a
		 cell at a time, and the band's own width wobbles: the direction handed to the locomotor is
		 never still, and a locomotor steered at a direction that is never still is a metronome.  So
		 the direction is low-passed and small errors are ignored outright.

		 Only while cruising.  In a jam, behind somebody, giving way, moving over, or when the route
		 genuinely turns hard, the filter opens right up - lag costs more than twitch the moment the
		 unit is actually manoeuvring, and a smoothed answer to "there is a tank in front of you" is
		 the wrong kind of calm. */
	const Real aimDx = goalPos.x - myPos->x;
	const Real aimDy = goalPos.y - myPos->y;
	const Real aimDist = (Real)sqrt( aimDx * aimDx + aimDy * aimDy );
	if (aimDist > 0.01f)
	{
		const Real wantAim = ATan2( aimDy, aimDx );		// the table, not the runtime: this decides a position
		if (!m_crowdAimValid)
		{
			m_crowdAim = wantAim;
			m_crowdAimValid = TRUE;
		}

		const Real err = normalizeAngle( wantAim - m_crowdAim );
		const Bool urgent = pressing || touching || queued || giveWay || merged
												|| (blocker != NULL && blockerGap < myR)
												|| fabs( err ) > 1.0f;

		/* One gain for every chassis, which is not obviously right and is what measured best.  Damping
			 the cruising gain for fast vehicles - the same fraction of the aim distance they eat in half
			 a second - was tried against the sliding steering point below and cost 5172 blocked
			 unit-frames a match and 7.5 units left stuck, against 4918 and 2.0 without it.  Lag on the
			 wheel and a point further up the road fail the same way in the end: both answer where the
			 unit was a moment ago.  The sliding point cured the shake on its own. */
		m_crowdAim = normalizeAngle( m_crowdAim + err * (urgent ? CROWD_AIM_URGENT : CROWD_AIM_CRUISE) );

		const Real facing = fixToReal( self->getOrientationFix() );
		if (!urgent && fabs( normalizeAngle( m_crowdAim - facing ) ) < CROWD_AIM_DEAD)
			m_crowdAim = facing;		// two degrees is not worth a steering input

		goalPos.x = myPos->x + Cos( m_crowdAim ) * aimDist;
		goalPos.y = myPos->y + Sin( m_crowdAim ) * aimDist;
	}

	goalPos.z = groundHeightAt( goalPos.x, goalPos.y );

	/* Brake now, come off the brake slowly.  Everything above decides the cap from one frame's worth
		 of neighbours, and that answer is not steady: the blocker slips out of the lookahead cone for
		 a frame, or drifts a foot sideways past the lane test, and the cap goes from his speed to ours
		 and back.  The unit lunges and brakes several times a second, the unit behind reads that speed
		 and does the same harder, and the whole column shunts.  The lane, the sideways push and the
		 aim are all filtered for exactly this reason; the throttle was the one number still handed to
		 the locomotor raw. */
	if (!m_crowdCapValid)
	{
		m_crowdCap = cap;
		m_crowdCapValid = TRUE;
	}
	else
	{
		m_crowdCap = Crowd_releaseCap( m_crowdCap, cap, CROWD_RELEASE_FILTER );
	}

	if (m_crowdCap < speed)
		speed = m_crowdCap;

	if (TheGlobalData->m_showLanes && (now % LOGICFRAMES_PER_SECOND) == 0)
	{
		const char *mode = pressing ? "press" : (giveWay ? "yield" : (merged ? "merge"
											 : (queued ? "brake" : (blocker != NULL ? "pass" : "free"))));
		DEBUG_LOG(("SHOWLANES crowd: unit %d %s sample %d lat %.1f band %.1f/%.1f lane %d/%d of %d queued %d stuck %d rank %d speed %.2f cross %d vel %.2f\n",
			self->getID(), mode, i, m_crowdLat, here.left, here.right, laneMine, laneFit, m_crowdLaneOf,
			m_crowdQueued, m_noProgress, rank, speed, crossing ? 1 : 0,
			myPhys != NULL ? myPhys->getVelocityMagnitude() : 0.0f));
	}
}

//-------------------------------------------------------------------------------------------------
/**
 * This is used by the internal move to state to indicate that a move started.
 */
void AIUpdateInterface::friend_startingMove(void) 
{
	m_movementComplete = FALSE; // we aren't finished moving.
	m_isMoving = TRUE;
	m_blockedFrames = 0;
	m_isBlockedAndStuck = FALSE;
}

//-------------------------------------------------------------------------------------------------
/**
 * This is used by the internal move to state to indicate that a move completed.
 */
void AIUpdateInterface::friend_endingMove()
{
	m_movementComplete = TRUE;
	m_isMoving = FALSE;
}

//-------------------------------------------------------------------------------------------------
/**
 * This is used by the jetai to set a specific path.
 */
void AIUpdateInterface::friend_setPath(Path *path)
{
	destroyPath();
	m_path = path;
}

//-------------------------------------------------------------------------------------------------
/**
 * This is used by the guard tunnel network state to set a target object.
 */
void AIUpdateInterface::friend_setGoalObject(Object *obj)
{
	Bool locked = getStateMachine()->isLocked();
	getStateMachine()->unlock();
	getStateMachine()->setGoalObject(obj);
	if (locked) {
		getStateMachine()->lock("Friend_setGlobalObject re-locking");
	}
}

//-------------------------------------------------------------------------------------------------
/** Is there a path at all that exists from us to the destination location */
//-------------------------------------------------------------------------------------------------
Bool AIUpdateInterface::isPathAvailable( const Coord3D *destination ) const
{
	
	// sanity
	if( destination == NULL )
		return FALSE;

	const Coord3D myPos = floatPosOf( getObject() );	// P5

	return TheAI->pathfinder()->clientSafeQuickDoesPathExist( m_locomotorSet, &myPos, destination );

}  // end isPathAvailable

//-------------------------------------------------------------------------------------------------
/** Is there a path (computed using the less accurate but quick method )
	* at all that exists from us to the destination location */
//-------------------------------------------------------------------------------------------------
Bool AIUpdateInterface::isQuickPathAvailable( const Coord3D *destination ) const
{
	
	// sanity
	if( destination == NULL )
		return FALSE;

	const Coord3D myPos = floatPosOf( getObject() );	// P5

	return TheAI->pathfinder()->clientSafeQuickDoesPathExistForUI( m_locomotorSet, &myPos, destination );

}  // end isQuickPathAvailable




//-------------------------------------------------------------------------------------------------
Bool AIUpdateInterface::isValidLocomotorPosition(const Coord3D* pos) const
{
	return TheAI->pathfinder()->validMovementPosition( getObject()->getCrusherLevel()>0, getObject()->getLayer(), m_locomotorSet, pos );
}

//-------------------------------------------------------------------------------------------------
DECLARE_PERF_TIMER(doLocomotor)
/**
 * Compute drive forces
 */
UpdateSleepTime AIUpdateInterface::doLocomotor( void )
{
	USE_PERF_TIMER(doLocomotor)

	if (getObject()->isKindOf(KINDOF_IMMOBILE))
		return UPDATE_SLEEP_FOREVER;

	chooseGoodLocomotorFromCurrentSet();

	if (m_headOnSeen)
		++m_headOnFrames;
	else
		m_headOnFrames = 0;
	m_headOnSeen = FALSE;

	if (m_isBlocked)
	{
		++m_blockedFrames;
		// this is the only place a blocked unit is counted once per frame rather than once per
		// collision pair, so it is where the traffic-jam number for the headless run comes from
		Pathfinder::bumpBlockedFrame( m_isBlockedAndStuck );

		/* And it is where the traffic map is written.  A unit that is stopped is the definition of
			 traffic; a unit that is merely slow is not, and neither is a unit parked because it has
			 arrived.  The cell it is standing in gets more expensive for about a second, which is
			 long enough for whoever repaths next to be handed a way round the line instead of a
			 place in it. */
		Int trafficRadius = 0;
		Bool trafficCenter = true;
		TheAI->pathfinder()->getRadiusAndCenter(getObject(), trafficRadius, trafficCenter);
		const Coord3D myPos = floatPosOf(getObject());	// P5
		TheAI->pathfinder()->noteTraffic(&myPos, trafficRadius);
	}
	else
	{
		m_blockedFrames = 0;
	}

	/* Re-stamp where this unit expects to be over the next few seconds, once a second, offset by
		 its own id so the whole army does not do it on one frame.  Always from where the unit
		 actually is - a claim made a second ago against a plan that has since been held up is
		 exactly the claim that would send somebody else round a crossing that is no longer there. */
	if (getPath() != NULL)
	{
		UnsignedInt stagger = TheGameLogic->getFrame() + (UnsignedInt)getObject()->getID();
		if ((stagger % PF_CLAIM_REFRESH_FRAMES) == 0)
			TheAI->pathfinder()->claimPathTiming(getObject(), getPath());
	}

	/* The lane across the route is taken the first frame the unit drives on a new route, not when
		 the route is built: a path can be handed over several frames ahead of the move actually
		 starting, and the group that ordered it may still be handing out lanes.

		 There is no drift back to the middle.  The first version had one, on the theory that a lane
		 should be a response to traffic and not a permanent kink, and it was the whole reason a
		 group kept collapsing back into single file - it pulled every unit at 0.008 a frame towards
		 the same line.  The taper over the last PF_LANE_TAPER_CELLS closes the band on arrival,
		 which is the only place the spread actually has to go away. */
	if (!m_laneFractionValid && getPath() != NULL)
		seedLaneFraction();

	/* Is this unit getting anywhere, and if not, what is being done about it.  Both run for every
		 unit that is trying to drive somewhere, on every frame, before anything decides where to
		 steer: the answer is what the courtesy rules and the rescue ladder are both hung off. */
	updateProgress();
	stuckRescue();

	const Bool traceWasBlocked = m_isBlocked;	// -tracemove: the flag is cleared on the next line
	m_isBlocked = FALSE;

	Bool blocked = m_blockedFrames > 0;
	Bool requiresConstantCalling = TRUE;	// assume the worst.

	if (m_curLocomotor)
	{
		m_curLocomotor->setPhysicsOptions(getObject());

		if (isAiInDeadState() && !m_curLocomotor->getLocomotorWorksWhenDead())
		{
			// now it's over, I'm dead, and I haven't done anything that I want,
			// or, I'm still alive, and there's nothing I want to do
		}
		else
		{
			switch (m_locomotorGoalType)
			{
				case POSITION_EXPLICIT:
					{
						Real speed = m_desiredSpeed;
						Real myMaxSpeed = m_curLocomotor->getMaxSpeedForCondition(getObject()->getBodyModule()->getDamageState());
						if( speed == FAST_AS_POSSIBLE || speed > myMaxSpeed )
							speed = myMaxSpeed;
						m_curLocomotor->locoUpdate_moveTowardsPosition(getObject(), 
							m_locomotorGoalData, 0.0f, speed, &blocked);
						m_doFinalPosition = FALSE;
					}
					break;

				case POSITION_ON_PATH:
					{	 
						if (!getPath())
						{
							if (m_waitingForPath) 
							{
								return UPDATE_SLEEP_FOREVER;  // Can't move till we get our path.
							}
							DEBUG_LOG(("Dead %d, obj %s %x\n", isAiInDeadState(), getObject()->getTemplate()->getName().str(), getObject()));
#ifdef STATE_MACHINE_DEBUG
							DEBUG_LOG(("Waiting %d, state %s\n", m_waitingForPath, getStateMachine()->getCurrentStateName().str()));
							m_stateMachine->setDebugOutput(1);
#endif
							DEBUG_CRASH(("must have a path here (doLocomotor)"));
							break;
						}
						Coord3D goalPos;
						Real onPathDistToGoal;
						const Coord3D myPos = floatPosOf(getObject());	// P5: the path is float
						if (!isDoingGroundMovement())
						{
							// airborne locomotor.  Get the goal and distance direct to the goal, don't consider obstacles.
							onPathDistToGoal = getPath()->computeFlightDistToGoal(&myPos, goalPos);
						}
						else
						{
							// Compute the actual goal position along the path to move towards.  Consider
							// obstacles, and follow the intermediate path points.
							ClosestPointOnPathInfo info;
							CRCDEBUG_LOG(("AIUpdateInterface::doLocomotor() - calling computePointOnPath() for %s\n",
								DescribeObject(getObject()).str()));
							getPath()->computePointOnPath(getObject(), m_locomotorSet, myPos, info);
							onPathDistToGoal = info.distAlongPath;
							goalPos = info.posOnPath;
							// layer is a possible bridge in the path.  Check & set the layer if applicable.
							TheAI->pathfinder()->updateLayer(getObject(), info.layer);
						}
						// updateProgress and the rescue ladder read this as "where the route sends us";
						// setLocomotorGoalPositionOnPath() zeroes it, so without this they measured
						// against the map's origin corner
						m_locomotorGoalData = goalPos;
				 
						Real speed = m_desiredSpeed;
						Real myMaxSpeed = m_curLocomotor->getMaxSpeedForCondition(getObject()->getBodyModule()->getDamageState());
						if( speed == FAST_AS_POSSIBLE || speed > myMaxSpeed )
							speed = myMaxSpeed;

						if (blocked && speed>m_curMaxBlockedSpeed) 
						{
							speed = m_curMaxBlockedSpeed;
							/* The bump limit used to shrink by 5% every frame of contact and never grow while
								 contact lasted, so once a blocker had stood still for a moment it sat at zero: the
								 tank in front drove off at full speed and the one touching it stayed parked until
								 the bodies came apart, then crept back up from a fifth of its speed. Traced on a
								 Crusader, 17 frames at a limit of 0.000 while the speed it was allowed rose from
								 0.02 to 0.55. It now recovers towards what the blocker allows, the same way it
								 recovers out of contact, and drives at 95% of it so the gap still opens. */
							if (m_bumpSpeedLimit>speed) {
								m_bumpSpeedLimit = speed;
							} else {
								if (m_bumpSpeedLimit<speed*0.2f) {
									m_bumpSpeedLimit = speed*0.2f;
								}
								m_bumpSpeedLimit *= 1.05f;
								if (m_bumpSpeedLimit>speed) {
									m_bumpSpeedLimit = speed;
								}
							}
							speed = m_bumpSpeedLimit * 0.95f;
						} 
						else 
						{
							blocked = FALSE;
							if (m_bumpSpeedLimit<FAST_AS_POSSIBLE) {
								if (m_bumpSpeedLimit<speed*0.2f) {
									m_bumpSpeedLimit = speed*0.2f;
								}
								m_bumpSpeedLimit *= 1.05f;
							}
							if (speed>m_bumpSpeedLimit) {
								speed = m_bumpSpeedLimit;
							}
						}

						/* A unit backing out of somewhere is not following its route this frame, and
							 nothing else may touch the point it is being sent to.  This is the one rung of
							 the rescue ladder that has to steer rather than ask, and it is here rather than
							 inside the crowd model because being wedged is not a crowd problem. */
						if (!rescueSteer(goalPos))
						{
							/* The crowd model hangs here, on the two numbers about to be handed to the
								 locomotor.  The point on the route has already been worked out; the crowd model
								 moves it sideways across the width of the road and takes speed off for whatever is
								 in the way.  Ground movement only - an aircraft has no road and no traffic. */
							if (isDoingGroundMovement())
								crowdSteer(goalPos, speed);
						}

						m_curLocomotor->locoUpdate_moveTowardsPosition(getObject(), goalPos,
							onPathDistToGoal+getPathExtraDistance(), speed, &blocked);

						m_doFinalPosition = FALSE;
					}
					break;

				case ANGLE:
					{
						m_curLocomotor->locoUpdate_moveTowardsAngle(getObject(), m_locomotorGoalData.x);
						m_doFinalPosition = FALSE;
					}
					break;

				case NONE:
					{
						if (m_doFinalPosition) 
						{
							FCoord3D pos = *getObject()->getPositionFix();
							Bool onGround = !getObject()->isAboveTerrain() && getObject()->getLayer() == LAYER_GROUND;
							// P4: the final position is a float move goal
							FCoord3D finalPos = fcoordFromCoord3D(m_finalPosition);
							Fix dx = finalPos.x - pos.x;
							Fix dy = finalPos.y - pos.y;
							Fix dSqr = dx*dx+dy*dy;
							const Fix DARN_CLOSE = 0.25_fx;
							if (dSqr < DARN_CLOSE)
							{
								m_doFinalPosition = FALSE;
								if (onGround)
									finalPos.z = TheTerrainLogic->getGroundHeightFix( finalPos.x, finalPos.y );
								else
									finalPos.z = pos.z;
								m_finalPosition.z = fixToReal(finalPos.z);
								getObject()->setPositionFix(&finalPos);
							}
							else
							{
								Fix dist = fixSqrt(dSqr);
								if (dist<Fix(1)) dist = Fix(1);
								const Fix step = Fix(2*PATHFIND_CELL_SIZE) / (dist*Fix(LOGICFRAMES_PER_SECOND));
								pos.x += dx*step;
								pos.y += dy*step;
								if (onGround)
									pos.z = TheTerrainLogic->getGroundHeightFix( pos.x, pos.y );
								getObject()->setPositionFix(&pos);
							}
						}
						requiresConstantCalling = m_curLocomotor->locoUpdate_maintainCurrentPosition(getObject());
					}
					break;
			}
		}
		
		// a repath asked for this frame is carried in m_blockedFrames; clamping it here threw it away
		// before the move state could read it
		if (!blocked && m_blockedFrames>1 && !m_repathAsked)
		{
			m_blockedFrames = 1;
		}
		m_repathAsked = FALSE;

		// After our movement for the frame, update our AirborneTarget flag.
		// P4: the locomotor's targeting height is still a Real
		if(getObject()->getHeightAboveTerrainFix() > fixFromReal( m_curLocomotor->getAirborneTargetingHeight() ) )
			getObject()->setStatus( MAKE_OBJECT_STATUS_MASK( OBJECT_STATUS_AIRBORNE_TARGET ) );
		else
			getObject()->clearStatus( MAKE_OBJECT_STATUS_MASK( OBJECT_STATUS_AIRBORNE_TARGET ) );

		// before the ceiling is thrown away for the frame - it is the value the trace is about
		AIUpdate_traceMove( getObject(), traceWasBlocked, m_blockedFrames,
			m_desiredSpeed,
			m_curLocomotor->getMaxSpeedForCondition(getObject()->getBodyModule()->getDamageState()),
			m_curMaxBlockedSpeed, m_bumpSpeedLimit, isWaitingForPath(), getPath() != NULL,
			m_isBlockedAndStuck );

		m_curMaxBlockedSpeed = FAST_AS_POSSIBLE;
	}

	if (m_curLocomotor != NULL
			&& m_locomotorGoalType == NONE
			&& m_doFinalPosition == FALSE
			&& m_isBlocked == FALSE
			&& requiresConstantCalling == FALSE)
	{
		return UPDATE_SLEEP_FOREVER;
	}
	else
	{
		return UPDATE_SLEEP_NONE;
	}

}

//-------------------------------------------------------------------------------------------------
void AIUpdateInterface::setLocomotorGoalPositionOnPath()
{
	m_locomotorGoalType = POSITION_ON_PATH;
	m_locomotorGoalData.zero();
}

//-------------------------------------------------------------------------------------------------
void AIUpdateInterface::setLocomotorGoalPositionExplicit(const Coord3D& newPos)
{
	m_locomotorGoalType = POSITION_EXPLICIT;
	m_locomotorGoalData = newPos;
#ifdef _DEBUG
if (_isnan(m_locomotorGoalData.x) || _isnan(m_locomotorGoalData.y) || _isnan(m_locomotorGoalData.z))
{
	DEBUG_CRASH(("NAN in setLocomotorGoalPositionExplicit"));
}
#endif
}

//-------------------------------------------------------------------------------------------------
void AIUpdateInterface::setLocomotorGoalOrientation(Real angle)
{
	m_locomotorGoalType = ANGLE;
	m_locomotorGoalData.x = angle;
#ifdef _DEBUG
if (_isnan(m_locomotorGoalData.x) || _isnan(m_locomotorGoalData.y) || _isnan(m_locomotorGoalData.z))
{
	DEBUG_CRASH(("NAN in setLocomotorGoalOrientation"));
}
#endif
}

//-------------------------------------------------------------------------------------------------
void AIUpdateInterface::setLocomotorGoalNone()
{
	m_locomotorGoalType = NONE;
}

//-------------------------------------------------------------------------------------------------
Bool AIUpdateInterface::isDoingGroundMovement(void) const
{
  
  if (getObject()->isDisabledByType( DISABLED_UNMANNED ) 
   && getObject()->isKindOf( KINDOF_PRODUCED_AT_HELIPAD ) )
  {
    return TRUE; // an unmanned helicopter gets grounded, eventually.
  }

	if (m_locomotorSet.getValidSurfaces() == LOCOMOTORSURFACE_AIR) 
	{
		return FALSE;  // air only loco.
	}

	if (m_curLocomotor == NULL) 
	{
		return FALSE;	// No loco, so we aren't moving.
	}

	// Cur loco is air, so not ground.
	if (m_curLocomotor->getLegalSurfaces() & LOCOMOTORSURFACE_AIR) 
	{
		return FALSE; 
	}

	// We are held, so not moving on ground.
	if( getObject()->isDisabledByType( DISABLED_HELD ) ) 
	{
		return FALSE;
	}

	// if we're airborne and "allowed to fall", we are probably deliberately in midair
	// due to rappel or accident...
	const PhysicsBehavior* physics = getObject()->getPhysics();
	if (getObject()->isAboveTerrain() && physics != NULL && physics->getAllowToFall())
	{
		return FALSE;
	}

	// After all exceptions, we must be doing ground movement.
	//DEBUG_ASSERTLOG(getObject()->isSignificantlyAboveTerrain(), ("Object %s is significantly airborne but also doing ground movement. What?\n",getObject()->getTemplate()->getName().str()));
	return TRUE;
}

//-------------------------------------------------------------------------------------------------
/** Some aircraft (comanche in particular, which hover) shouldn't stack destinations.
Others, like missles, should stack destinations.  AdjustDestination in pathfinder unstacks
destinations, and this routine identifies non-ground units that should unstack. */

Bool AIUpdateInterface::isAircraftThatAdjustsDestination(void) const
{
	if (m_curLocomotor == NULL) 
	{
		return FALSE;	// No loco, so we aren't moving.
	}

	if (m_curLocomotor->getAppearance() == LOCO_HOVER) 
	{
		return TRUE;	// Hover adjusts.
	}
	if (m_curLocomotor->getAppearance() == LOCO_WINGS)
	{
		return TRUE; // wings adjusts.
	}
	if (m_curLocomotor->getAppearance() == LOCO_THRUST)
	{
		return FALSE; // thrust doesn't adjust.
	}

	return FALSE;
}

//-------------------------------------------------------------------------------------------------
Bool AIUpdateInterface::getTreatAsAircraftForLocoDistToGoal() const
{
	Bool treatAsAircraft = !isDoingGroundMovement();
	if (getPathExtraDistance() > PATHFIND_CLOSE_ENOUGH) 
	{
		// We are following a waypoint or other multiple point path, so use the "easy" success criteria.
		treatAsAircraft = TRUE;
	}
	if (m_curLocomotor && m_curLocomotor->getAppearance() == LOCO_HOVER) 
	{
		// Hovercrafts are very sloppy.  So use aircraft tests for distance to goal.  jba.
		treatAsAircraft = TRUE;
	}
	return treatAsAircraft;
}

//-------------------------------------------------------------------------------------------------
Real AIUpdateInterface::getLocomotorDistanceToGoal() 
{
	switch (m_locomotorGoalType)
	{
		case POSITION_EXPLICIT:
			DEBUG_CRASH(("not yet implemented"));
			return 0.0f;

		case POSITION_ON_PATH:
			if (!getPath()) 
			{
				DEBUG_CRASH(("must have a path here (getLocomotorDistanceToGoal)"));
				return 0.0f;
			}
			else if (!m_curLocomotor) 
			{
				//DEBUG_LOG(("no locomotor here, so no dist. (this is ok.)\n"));
				return 0.0f;
			}	
			else if( m_curLocomotor->isCloseEnoughDist3D() || getObject()->isKindOf(KINDOF_PROJECTILE))
			{
				const Object *me = getObject();
				const Coord3D *dest = getGoalPosition();
				if (m_path->getLastNode()) {
					dest = m_path->getLastNode()->getPosition();
				}
				// P4/P5: the goal is a float path point and the answer goes to the locomotor
				const FCoord3D destFix = fcoordFromCoord3D( *dest );
				Fix distance = ThePartitionManager->getDistanceSquaredFix( me, &destFix, FROM_CENTER_3D );
				return fixToReal( fixSqrt( distance ) );// Other paths return dots of normalized vectors, so one sqrt ain't so bad
			}
			else 
			{
				Coord3D goalPos;
				Bool treatAsAircraft = getTreatAsAircraftForLocoDistToGoal();
				Real dist;
				const Coord3D myPos = floatPosOf( getObject() );	// P4/P5: path distances for the locomotor
				if (treatAsAircraft)
				{
					// airborne locomotor.  Get the goal and distance direct to the goal, don't consider obstacles.
					dist =  getPath()->computeFlightDistToGoal(&myPos, goalPos);
				}	else {
					// Ground based locomotor.
					ClosestPointOnPathInfo info;
					CRCDEBUG_LOG(("AIUpdateInterface::getLocomotorDistanceToGoal() - calling computePointOnPath() for object %d\n", getObject()->getID()));
					getPath()->computePointOnPath(getObject(), m_locomotorSet, myPos, info);
					goalPos = info.posOnPath;
					dist = info.distAlongPath;
				}
				if (m_path->getLastNode()) {
					goalPos = *m_path->getLastNode()->getPosition();
				}
				// We are trying to get to goal.  So,
				// If the actual distance is farther, then use the actual distance so we get there.
				Real dx = goalPos.x - myPos.x;
				Real dy = goalPos.y - myPos.y;
				Real distSqr = dx*dx + dy*dy;
				
				if (treatAsAircraft) 
				{
					if (sqr(dist) > distSqr) 
					{
						return sqrt(distSqr);
					}
					else
					{
						return dist; 
					}
				}

				if (dist<PATHFIND_CELL_SIZE_F || sqr(dist) < distSqr)
					return sqrtf(distSqr);
				else
					return dist;			 

			}

		case ANGLE:
		case NONE:
			// If it isn't a positional goal, we are there already.
			return 0.0f;
	}

	return 0.0f;
}
 

/**
 * Catch up with the rest of the team.
 */
void AIUpdateInterface::joinTeam( void )
{
	// the dead don't listen very well
	if (isAiInDeadState())
		return;

	if (getObject()->isMobile() == FALSE)
		return;

	chooseLocomotorSet(LOCOMOTORSET_NORMAL);
	getStateMachine()->clear();
	getStateMachine()->setGoalWaypoint(NULL);
	Object *obj = getObject();
	Object *other = NULL;
	Team *team = obj->getTeam();
	for (DLINK_ITERATOR<Object> iter = team->iterate_TeamMemberList(); !iter.done(); iter.advance())
	{
		Object *anObj = iter.cur();
		if (!anObj) 
		{
			continue;
		}
		if (obj == anObj) 
		{
			// it's us.
			continue;
		}	
		else if (anObj->getAI()) 
		{
			if( !anObj->isDisabledByType( DISABLED_HELD ) ) 
			{
				other = anObj;
				break;
			}
		}
	}
	if (other) {
		AIUpdateInterface* ai = other->getAI();
		if (ai->isIdle()) {
			const Coord3D otherPos = floatPosOf(other);	// P4: move orders take float
			aiMoveToPosition(&otherPos, CMD_FROM_AI);
			return;
		}
		if (ai->getGoalObject()) {
			getStateMachine()->setGoalObject(ai->getGoalObject());
		} else {
			getStateMachine()->setGoalPosition(ai->getGoalPosition());
		}
		// a team on a waypoint path is followed along the same path; without it the copied state has no
		// waypoint to start from
		if (ai->getStateMachine()->getGoalWaypoint())
			getStateMachine()->setGoalWaypoint(ai->getStateMachine()->getGoalWaypoint());
		StateID	state = ai->getCurrentStateID();
		setLastCommandSource( CMD_FROM_AI );
		// Match the state.
		getStateMachine()->setState( state );
	}

}  // end joinTeam

//-------------------------------------------------------------------------------------------------
Bool AIUpdateInterface::isAllowedToRespondToAiCommands(const AICommandParms* parms) const
{
	// the dead don't listen very well
	// (unless they are seeking to feed on the brains of the living)
	// [urrr, need brains]
	if (getObject()->isEffectivelyDead())
		return FALSE;

	// We're catching the sleep mood here. AI Units that are asleep actually ignore all commands.
	// (See the AI Mood matrix for more info)
	UnsignedInt moodParms = getMoodMatrixValue();
	if ((moodParms & MM_Controller_AI) && (moodParms & MM_Mood_Sleep) && (parms->m_cmd != AICMD_MOVE_TO_POSITION_EVEN_IF_SLEEPING))
		return FALSE;

  const AIUpdateModuleData *data = getAIUpdateModuleData();

  Bool forbidden = data->m_forbidPlayerCommands;

  if ( parms->m_cmdSource == CMD_FROM_PLAYER && forbidden )
    return FALSE; 
  // THIS IS JUST FOR THE SPECTREGUNSHIP FOR NOW... 
  // IT LOCKS OUT USER INPUT, 
  // ALLOWING ONLY THE SPECTREUPDATE TO COMMAND IT VIA CMD_FROM_AI
  // AUTHOR, LORENZEN... 5/15/03

	//
	// A unit that is still walking the exit path out of the thing that produced it finishes that
	// step before it will listen to anyone.  Taking an order mid-doorway leaves it turning around
	// inside the building's footprint, which blocks the next unit off the line and, with
	// setCanPathThroughUnits on for the exit path, lets it be shoved back through the wall.
	// Aircraft are left alone: their "exit path" is the taxi and takeoff run off a helipad or
	// airfield, which the player is expected to be able to redirect.
	//
	if ( parms->m_cmdSource == CMD_FROM_PLAYER
			 && getStateMachine()->getCurrentStateID() == AI_FOLLOW_EXITPRODUCTION_PATH
			 && isDoingGroundMovement() )
		return FALSE;

	return TRUE;
}

//-------------------------------------------------------------------------------------------------
void AIUpdateInterface::aiDoCommand(const AICommandParms* parms)
{
	if (!isAllowedToRespondToAiCommands(parms))
		return;

	// Any order at all replaces the trip to the producer's rally point - except the exit path itself,
	// which is the leg that precedes it.
	if (parms->m_cmd != AICMD_FOLLOW_EXITPRODUCTION_PATH)
		m_hasExitProductionRallyPoint = FALSE;

	// Likewise the walk back from a salvage crate: the player moving the unit somewhere means the
	// spot it left is no longer where it belongs.  The order that sends it to the crate is given
	// first and the return position recorded after, so this does not eat its own trip.
	m_hasSalvageReturnPosition = FALSE;

	// A tunnel trip drives itself with AI orders - the exit, the step out of the door - so only an
	// order from somebody else ends it.
	if (parms->m_cmdSource != CMD_FROM_AI)
		m_hasTunnelTrip = FALSE;

#ifdef ALLOW_SURRENDER
	// surrendered items have very limited options, and only via AI cmds
	if (isSurrendered())
	{
		if (parms->m_cmdSource != CMD_FROM_AI)
			return;

		switch (parms->m_cmd)
		{
			case AICMD_MOVE_TO_POSITION:
			case AICMD_MOVE_TO_OBJECT:
			case AICMD_IDLE:
			case AICMD_ENTER:
			case AICMD_EXIT:
				break;

			default:
				DEBUG_LOG(("ignoring ai cmd due to surrender condition"));
				return;
		}
	}
#endif

  
	switch (parms->m_cmd)
	{
		case AICMD_MOVE_TO_POSITION:
		case AICMD_MOVE_TO_POSITION_EVEN_IF_SLEEPING:
			privateMoveToPosition(&parms->m_pos, parms->m_cmdSource);
			break;
		case AICMD_MOVE_TO_OBJECT:
			privateMoveToObject(parms->m_obj, parms->m_cmdSource);
			break;
		case AICMD_TIGHTEN_TO_POSITION:
			privateTightenToPosition(&parms->m_pos, parms->m_cmdSource);
			break;
		case AICMD_MOVE_TO_POSITION_AND_EVACUATE:
			privateMoveToAndEvacuate(&parms->m_pos, parms->m_cmdSource);
			break;
		case AICMD_MOVE_TO_POSITION_AND_EVACUATE_AND_EXIT:
			privateMoveToAndEvacuateAndExit(&parms->m_pos, parms->m_cmdSource);
			break;
		case AICMD_IDLE:
			privateIdle(parms->m_cmdSource);
			break;
		case AICMD_FOLLOW_WAYPOINT_PATH:
			privateFollowWaypointPath(parms->m_waypoint, parms->m_cmdSource);
			break;
		case AICMD_FOLLOW_WAYPOINT_PATH_AS_TEAM:
			privateFollowWaypointPathAsTeam(parms->m_waypoint, parms->m_cmdSource);
			break;
		case AICMD_FOLLOW_WAYPOINT_PATH_EXACT:
			privateFollowWaypointPathExact(parms->m_waypoint, parms->m_cmdSource);
			break;
		case AICMD_FOLLOW_WAYPOINT_PATH_AS_TEAM_EXACT:
			privateFollowWaypointPathAsTeamExact(parms->m_waypoint, parms->m_cmdSource);
			break;
		case AICMD_FOLLOW_PATH:
		{
			// The callee takes the path, so hand it a copy and leave the caller's parms intact.
			std::vector<Coord3D> coords = parms->m_coords;
			privateFollowPath(&coords, parms->m_obj, parms->m_cmdSource, FALSE);
			break;
		}
		case AICMD_FOLLOW_PATH_APPEND:
			privateFollowPathAppend(&parms->m_pos, parms->m_cmdSource);
			break;
		case AICMD_FOLLOW_EXITPRODUCTION_PATH:
		{
			std::vector<Coord3D> coords = parms->m_coords;
			privateFollowPath(&coords, parms->m_obj, parms->m_cmdSource, TRUE);
			break;
		}
		case AICMD_ATTACK_OBJECT:
			privateAttackObject(parms->m_obj, parms->m_intValue, parms->m_cmdSource);
			break;
		case AICMD_FORCE_ATTACK_OBJECT:
			privateForceAttackObject(parms->m_obj, parms->m_intValue, parms->m_cmdSource);
			break;
		case AICMD_GUARD_RETALIATE:
			privateGuardRetaliate( parms->m_obj, &parms->m_pos, parms->m_intValue, parms->m_cmdSource );
			break;
		case AICMD_ATTACK_TEAM:
			privateAttackTeam(parms->m_team, parms->m_intValue, parms->m_cmdSource);
			break;
		case AICMD_ATTACK_POSITION:
			privateAttackPosition(&parms->m_pos, parms->m_intValue, parms->m_cmdSource);
			break;
		case AICMD_ATTACKMOVE_TO_POSITION:
			privateAttackMoveToPosition(&parms->m_pos, parms->m_intValue, parms->m_cmdSource);
			break;
		case AICMD_ATTACKFOLLOW_WAYPOINT_PATH:
			privateAttackFollowWaypointPath(parms->m_waypoint, parms->m_intValue, FALSE, parms->m_cmdSource);
			break;
		case AICMD_ATTACKFOLLOW_WAYPOINT_PATH_AS_TEAM:
			privateAttackFollowWaypointPath(parms->m_waypoint, parms->m_intValue, TRUE, parms->m_cmdSource);
			break;
		case AICMD_HUNT:
			privateHunt(parms->m_cmdSource);
			break;
		case AICMD_ATTACK_AREA:
			privateAttackArea(parms->m_polygon, parms->m_cmdSource);
			break;
		case AICMD_REPAIR:
			privateRepair(parms->m_obj, parms->m_cmdSource);
			break;
#ifdef ALLOW_SURRENDER
		case AICMD_PICK_UP_PRISONER:
			privatePickUpPrisoner( parms->m_obj, parms->m_cmdSource );
			break;
		case AICMD_RETURN_PRISONERS:
			privateReturnPrisoners( parms->m_obj, parms->m_cmdSource );
			break;
#endif
		case AICMD_RESUME_CONSTRUCTION:
			privateResumeConstruction(parms->m_obj, parms->m_cmdSource);
			break;
		case AICMD_GET_HEALED:
			privateGetHealed(parms->m_obj, parms->m_cmdSource);
			break;
		case AICMD_GET_REPAIRED:
			privateGetRepaired(parms->m_obj, parms->m_cmdSource);
			break;
		case AICMD_ENTER://///////////////////////////////////////////////////////////////
			privateEnter(parms->m_obj, parms->m_cmdSource);
			break;
		case AICMD_DOCK:
			privateDock(parms->m_obj, parms->m_cmdSource);
			break;
		case AICMD_EXIT:////////////////////////////////////////////////////////////////////
			privateExit(parms->m_obj, parms->m_cmdSource);
			break;
		case AICMD_EXIT_INSTANTLY://///////////////////////////////////////////////////////
			privateExitInstantly( parms->m_obj, parms->m_cmdSource );
			break;
		case AICMD_EVACUATE://///////////////////////////////////////////////////////////
			privateEvacuate(parms->m_intValue, parms->m_cmdSource);
			break;
		case AICMD_EVACUATE_INSTANTLY:////////////////////////////////////////////////////
			privateEvacuateInstantly( parms->m_intValue, parms->m_cmdSource );
			break;
		case AICMD_EXECUTE_RAILED_TRANSPORT:
			privateExecuteRailedTransport( parms->m_cmdSource );
			break;
		case AICMD_GO_PRONE:
			privateGoProne(&parms->m_damage, parms->m_cmdSource);
			break;
		case AICMD_GUARD_POSITION:
		{
			//Kris: Aug 18, 2003 -- If you were retaliating and ordered to enter guard mode, 
			//the state needs to be cleared before doing so or else we leave the state too
			//late and clear data AFTER we go into the new guard mode causing units to 
			//move to zero (bottom left corner).
			AIStateMachine *state = getStateMachine();
			if( state && state->getCurrentStateID() == AI_GUARD_RETALIATE )
			{
				state->clear();
			}
			//end

			privateGuardPosition(&parms->m_pos, (GuardMode)parms->m_intValue, parms->m_cmdSource);
			break;
		}
		case AICMD_GUARD_OBJECT:
		{
			//Kris: Aug 18, 2003 -- If you were retaliating and ordered to enter guard mode, 
			//the state needs to be cleared before doing so or else we leave the state too
			//late and clear data AFTER we go into the new guard mode causing units to 
			//move to zero (bottom left corner).
			AIStateMachine *state = getStateMachine();
			if( state && state->getCurrentStateID() == AI_GUARD_RETALIATE )
			{
				state->clear();
			}
			//end

			privateGuardObject(parms->m_obj, (GuardMode)parms->m_intValue, parms->m_cmdSource);
			break;
		}
		case AICMD_GUARD_TUNNEL_NETWORK:
		{
			//Kris: Aug 18, 2003 -- If you were retaliating and ordered to enter guard mode, 
			//the state needs to be cleared before doing so or else we leave the state too
			//late and clear data AFTER we go into the new guard mode causing units to 
			//move to zero (bottom left corner).
			AIStateMachine *state = getStateMachine();
			if( state && state->getCurrentStateID() == AI_GUARD_RETALIATE )
			{
				state->clear();
			}
			//end

			privateGuardTunnelNetwork((GuardMode)parms->m_intValue, parms->m_cmdSource);
			break;
		}
		case AICMD_GUARD_AREA:
		{
			//Kris: Aug 18, 2003 -- If you were retaliating and ordered to enter guard mode, 
			//the state needs to be cleared before doing so or else we leave the state too
			//late and clear data AFTER we go into the new guard mode causing units to 
			//move to zero (bottom left corner).
			AIStateMachine *state = getStateMachine();
			if( state && state->getCurrentStateID() == AI_GUARD_RETALIATE )
			{
				state->clear();
			}
			//end

			privateGuardArea(parms->m_polygon, (GuardMode)parms->m_intValue, parms->m_cmdSource);
			break;
		}
		case AICMD_HACK_INTERNET:
			privateHackInternet( parms->m_cmdSource );
			break;
		case AICMD_FACE_OBJECT:
			privateFaceObject( parms->m_obj, parms->m_cmdSource );
			break;
		case AICMD_FACE_POSITION:
			privateFacePosition( &parms->m_pos, parms->m_cmdSource );
			break;
		case AICMD_RAPPEL_INTO:
			privateRappelInto( parms->m_obj, parms->m_pos, parms->m_cmdSource );
			break;
		case AICMD_COMBATDROP:
			privateCombatDrop( parms->m_obj, parms->m_pos, parms->m_cmdSource );
			break;
		case AICMD_COMMANDBUTTON:
			privateCommandButton( parms->m_commandButton, parms->m_cmdSource );
			break;
		case AICMD_COMMANDBUTTON_OBJ:
			privateCommandButtonObject( parms->m_commandButton, parms->m_obj, parms->m_cmdSource );
			break;
		case AICMD_COMMANDBUTTON_POS:
			privateCommandButtonPosition( parms->m_commandButton, &parms->m_pos, parms->m_cmdSource );
			break;
		case AICMD_WANDER:
			privateWander( parms->m_waypoint, parms->m_cmdSource );
			break;
		case AICMD_WANDER_IN_PLACE:
			privateWanderInPlace(parms->m_cmdSource);
			break;
		case AICMD_PANIC:
			privatePanic( parms->m_waypoint, parms->m_cmdSource );
			break;
		case AICMD_BUSY:
			privateBusy( parms->m_cmdSource );
			break;
		case AICMD_MOVE_AWAY_FROM_UNIT:
			privateMoveAwayFromUnit( parms->m_obj, parms->m_cmdSource );
			break;
		default:
			DEBUG_CRASH(("unhandled AI command!"));
			break;
	}
}


//-------------------------------------------------------------------------------------------------
// AI Command Interface implementation for AIUpdateInterface
//

/**
 * Move to given position(s)
 */
void AIUpdateInterface::privateMoveToPosition( const Coord3D *pos, CommandSourceType cmdSource )
{
	if (getObject()->isMobile() == FALSE) 
		return;

	//Resetting the locomotor here was initially added for scripting purposes. It has been moved
	//to the responsibility of the script to reset the locomotor before moving. This is needed because
	//other systems (like the battle drone) change the locomotor based on what it's trying to do, and
	//doesn't want to get reset when ordered to move.
	//chooseLocomotorSet(LOCOMOTORSET_NORMAL);

	if (!isIdle() && cmdSource == CMD_FROM_AI) {
		// This is an internally generated move to, and we are in a non-idle state. [8/19/2003]
		// Our state could be the source of this command, so 
		// Move for 20 seconds [8/19/2003]
		// Things like attack state don't take kindly to being booted out unceremoniously. jba. [8/19/2003]
		setGoalPositionClipped(pos, cmdSource);
		m_blockedFrames = 0;
		m_isBlocked = FALSE;
		m_isBlockedAndStuck = FALSE;
		getStateMachine()->setTemporaryState(AI_MOVE_TO, LOGICFRAMES_PER_SECOND * 20);
	} else {
		// Normal user or script command, just do it. [8/19/2003]
		getStateMachine()->clear();
		setGoalPositionClipped(pos, cmdSource);
		m_blockedFrames = 0;
		m_isBlocked = FALSE;
		m_isBlockedAndStuck = FALSE;
		setLastCommandSource( cmdSource );
		getStateMachine()->setState( AI_MOVE_TO );
	}

}

//-------------------------------------------------------------------------------------------------
/**
 * Move to given object
 */
void AIUpdateInterface::privateMoveToObject( Object *obj, CommandSourceType cmdSource ) 
{
	// the dead don't listen very well
	if (m_isAiDead)
		return;

	if (getObject()->isMobile() == FALSE)
		return;

	//Resetting the locomotor here was initially added for scripting purposes. It has been moved
	//to the responsibility of the script to reset the locomotor before moving. This is needed because
	//other systems (like the battle drone) change the locomotor based on what it's trying to do, and
	//doesn't want to get reset when ordered to move.
	//chooseLocomotorSet(LOCOMOTORSET_NORMAL);
	
	getStateMachine()->clear();
	getStateMachine()->setGoalObject( obj );
	m_blockedFrames = 0;
	m_isBlocked = FALSE;
	m_isBlockedAndStuck = FALSE;
	setLastCommandSource( cmdSource );
	getStateMachine()->setState( AI_MOVE_TO );

}

//----------------------------------------------------------------------------------------
// Face a specified object -- succeed when facing
//----------------------------------------------------------------------------------------
void AIUpdateInterface::privateFaceObject( Object *obj, CommandSourceType cmdSource )
{
	if( !getObject()->isMobile() )
	{
		return;
	}

	//Resetting the locomotor here was initially added for scripting purposes. It has been moved
	//to the responsibility of the script to reset the locomotor before moving. This is needed because
	//other systems (like the battle drone) change the locomotor based on what it's trying to do, and
	//doesn't want to get reset when ordered to move.
	//chooseLocomotorSet(LOCOMOTORSET_NORMAL);

	getStateMachine()->clear();
	getStateMachine()->setGoalObject( obj );
	m_blockedFrames = 0;
	m_isBlocked = FALSE;
	m_isBlockedAndStuck = FALSE;
	setLastCommandSource( cmdSource );
	getStateMachine()->setState( AI_FACE_OBJECT );
}

//----------------------------------------------------------------------------------------
// Face a specified position -- succeed when facing
//----------------------------------------------------------------------------------------
void AIUpdateInterface::privateFacePosition( const Coord3D *pos, CommandSourceType cmdSource )
{
	if( !getObject()->isMobile() )
	{
		return;
	}

	//Resetting the locomotor here was initially added for scripting purposes. It has been moved
	//to the responsibility of the script to reset the locomotor before moving. This is needed because
	//other systems (like the battle drone) change the locomotor based on what it's trying to do, and
	//doesn't want to get reset when ordered to move.
	//chooseLocomotorSet(LOCOMOTORSET_NORMAL);

	getStateMachine()->clear();
	setGoalPositionClipped(pos, cmdSource);
	m_blockedFrames = 0;
	m_isBlocked = FALSE;
	m_isBlockedAndStuck = FALSE;
	setLastCommandSource( cmdSource );
	getStateMachine()->setState( AI_FACE_POSITION );
}

//----------------------------------------------------------------------------------------
// Rappel into target and devastate contents (if not empty).
// If target is null, rappel to ground.
//----------------------------------------------------------------------------------------
void AIUpdateInterface::privateRappelInto( Object *target, const Coord3D& pos, CommandSourceType cmdSource )
{

	//Resetting the locomotor here was initially added for scripting purposes. It has been moved
	//to the responsibility of the script to reset the locomotor before moving. This is needed because
	//other systems (like the battle drone) change the locomotor based on what it's trying to do, and
	//doesn't want to get reset when ordered to move.
	//chooseLocomotorSet(LOCOMOTORSET_NORMAL);

	getStateMachine()->clear();
	getStateMachine()->setGoalObject( target );
	setGoalPositionClipped(&pos, cmdSource);
	m_blockedFrames = 0;
	m_isBlocked = FALSE;
	m_isBlockedAndStuck = FALSE;
	setLastCommandSource( cmdSource );
	getStateMachine()->setState( AI_RAPPEL_INTO );
}


//----------------------------------------------------------------------------------------
/**
 * Move to given position(s)
 * If transportExits, transport returns and deletes itself.
 */
void AIUpdateInterface::privateMoveToAndEvacuate( const Coord3D *pos, CommandSourceType cmdSource )
{
	if (getObject()->isMobile() == FALSE)
		return;

	//Resetting the locomotor here was initially added for scripting purposes. It has been moved
	//to the responsibility of the script to reset the locomotor before moving. This is needed because
	//other systems (like the battle drone) change the locomotor based on what it's trying to do, and
	//doesn't want to get reset when ordered to move.
	//chooseLocomotorSet(LOCOMOTORSET_NORMAL);

	getStateMachine()->clear();
	setGoalPositionClipped(pos, cmdSource);
	m_blockedFrames = 0;
	m_isBlocked = FALSE;
	m_isBlockedAndStuck = FALSE;
	setLastCommandSource( cmdSource );

	m_stateMachine->setState( AI_MOVE_AND_EVACUATE );
}

//----------------------------------------------------------------------------------------
/**
 * Move to given position(s)
 * If transportExits, transport returns and deletes itself.
 */
void AIUpdateInterface::privateMoveToAndEvacuateAndExit( const Coord3D *pos, CommandSourceType cmdSource )
{
	if (getObject()->isMobile() == FALSE)
		return;

	//Resetting the locomotor here was initially added for scripting purposes. It has been moved
	//to the responsibility of the script to reset the locomotor before moving. This is needed because
	//other systems (like the battle drone) change the locomotor based on what it's trying to do, and
	//doesn't want to get reset when ordered to move.
	//chooseLocomotorSet(LOCOMOTORSET_NORMAL);

	getStateMachine()->clear();
	setGoalPositionClipped(pos, cmdSource);
	m_blockedFrames = 0;
	m_isBlocked = FALSE;
	m_isBlockedAndStuck = FALSE;
	setLastCommandSource( cmdSource );

	static NameKeyType key_DeliverPayloadAIUpdate = NAMEKEY("DeliverPayloadAIUpdate");
	DeliverPayloadAIUpdate *dp = (DeliverPayloadAIUpdate*)getObject()->findUpdateModule( key_DeliverPayloadAIUpdate );
	if( dp )
	{
		dp->deliverPayloadViaModuleData( pos );
	}
	else
	{
		getStateMachine()->setState( AI_MOVE_AND_EVACUATE_AND_EXIT);
	}

}

//----------------------------------------------------------------------------------------
/**
 * Enter idle state.
 */
void AIUpdateInterface::privateIdle(CommandSourceType cmdSource)
{
	if (getObject()->isKindOf(KINDOF_PROJECTILE))
		return;

	getStateMachine()->clear();
	getStateMachine()->setState( AI_IDLE );
	setLastCommandSource( cmdSource );

	ContainModuleInterface *contain = getObject()->getContain();
	if (contain)
	{
		const ContainedItemsList* items = contain->getContainedItemsList();
		if (items)
		{
			for (ContainedItemsList::const_iterator it = items->begin(); it != items->end(); ++it)
			{
				Object* obj = *it;
				AIUpdateInterface* ai = obj ? obj->getAI() : NULL;
				if (ai)
					ai->aiIdle(cmdSource);
			}
		}
	}

}

//----------------------------------------------------------------------------------------
Bool AIUpdateInterface::isIdle() const
{
	const AIStateMachine *state = getStateMachine();
	if( state->getCurrentStateID() == AI_IDLE )
	{
		return TRUE;
	}
	return state->isInIdleState();
}

//----------------------------------------------------------------------------------------
Bool AIUpdateInterface::isAttacking() const
{
	return getStateMachine()->isInAttackState();
}

//----------------------------------------------------------------------------------------
//Definition of busy -- when explicitly in the busy state. Moving or attacking is not considered busy!
//----------------------------------------------------------------------------------------
Bool AIUpdateInterface::isBusy() const
{
	return getStateMachine()->isInBusyState();
}

//----------------------------------------------------------------------------------------
Bool AIUpdateInterface::isClearingMines() const
{
	// if we are attacking with an anti-mine weapon, we are clearing mines, regardless
	// of our target.

	if (!getObject()->testStatus(OBJECT_STATUS_IS_ATTACKING))
		return FALSE;

	const Weapon* weapon = getObject()->getCurrentWeapon();
	if (!weapon)
		return FALSE;

	if ((weapon->getAntiMask() & WEAPON_ANTI_MINE) == 0)
		return FALSE;

	return TRUE;
}

//----------------------------------------------------------------------------------------
/**
 * Take the shortest path towards pos in order to tighten up a formation
 */
void AIUpdateInterface::privateTightenToPosition( const Coord3D *pos, CommandSourceType cmdSource )
{
	if (getObject()->isMobile() == FALSE)
		return;
	getStateMachine()->clear();
	getStateMachine()->setGoalObject( NULL );
	setGoalPositionClipped(pos, cmdSource);
	setLastCommandSource( cmdSource );
	getStateMachine()->setState( AI_MOVE_AND_TIGHTEN );
}
//----------------------------------------------------------------------------------------
/**
 * Is this moving out of the way of another unit.
 */
Bool AIUpdateInterface::isMovingAwayFrom(Object *obj)	 const
{
	ObjectID id = obj->getID();
	if (m_stateMachine->getTemporaryState() == AI_MOVE_OUT_OF_THE_WAY) {
		if (m_moveOutOfWay1 == id) return TRUE;
		if (m_moveOutOfWay2 == id) return TRUE; 
	}
	return FALSE;
}
//----------------------------------------------------------------------------------------
/**
 * Is this moving out of the way of another unit.
 */
Bool AIUpdateInterface::isMoving() const
{
	if (isIdle()) {
		return false;
	}
	if (m_locomotorGoalType != NONE) {
		return TRUE;
	}
	if (m_isMoving) {
		return TRUE;
	}
	return FALSE;
}

//----------------------------------------------------------------------------------------
/**
 * Move out of the way of another unit.
 */
void AIUpdateInterface::privateMoveAwayFromUnit( Object *unit, CommandSourceType cmdSource )
{
	// the dead don't listen very well
	if (isAiInDeadState() || (getObject()->isMobile() == FALSE) || !isAllowedToMoveAwayFromUnit()) 
	{
		return;
	}

	//
	// A queued AI command is re-issued a frame or more after it was made, and the object it names
	// can be gone by then - a hacker told to step aside while it is coming out of its hacking state
	// is the reproducible case.
	//
	if (unit == NULL)
		return;

	ObjectID id = unit->getID();
	if (m_stateMachine->getTemporaryState() == AI_MOVE_OUT_OF_THE_WAY) {
		if (m_moveOutOfWay1 == id) {
			if (m_isBlocked) {
				setIgnoreCollisionTime(LOGICFRAMES_PER_SECOND*2); // cheat for 2 seconds.
			}
			return;
		}
		if (m_moveOutOfWay2 == id) {
			if (m_isBlocked) {
				setIgnoreCollisionTime(LOGICFRAMES_PER_SECOND*2); // cheat for 2 seconds.
			}
			return;
		}
	}
	m_moveOutOfWay2 = m_moveOutOfWay1;
	m_moveOutOfWay1 = id;
	Object *obj2 = TheGameLogic->findObjectByID(m_moveOutOfWay2);
	Path *path2 = NULL;
	if (obj2 && obj2->getAI()) {
		path2 = obj2->getAI()->getPath();
	}

	Path* unitPath = NULL;
	if (unit && unit->getAI()) {
		unitPath = unit->getAI()->getPath();
	}
	if (unitPath == NULL) return;
	Path *newPath = TheAI->pathfinder()->getMoveAwayFromPath(getObject(), unit, unitPath, obj2, path2);
	if (newPath==NULL && !canPathThroughUnits())	{
		setCanPathThroughUnits(TRUE);
		newPath = TheAI->pathfinder()->getMoveAwayFromPath(getObject(), unit, unitPath, obj2, path2);
	}
		
	if (newPath) {
		destroyPath();
		m_path = newPath;
		wakeUpNow();
		m_stateMachine->setTemporaryState(AI_MOVE_OUT_OF_THE_WAY, 10*LOGICFRAMES_PER_SECOND);
		if (m_path) 
		{
	 		if( !getObject()->isKindOf(KINDOF_NO_COLLIDE))// If I don't collide with things, I don't need to tell them to get out of the way
				TheAI->pathfinder()->moveAllies(getObject(), m_path);
		}
	}

}

//----------------------------------------------------------------------------------------
/**
 * Start following the path from the given point
 */
void AIUpdateInterface::privateFollowWaypointPath( const Waypoint *way, CommandSourceType cmdSource )
{
	if (getObject()->isMobile() == FALSE)
		return;

	//Resetting the locomotor here was initially added for scripting purposes. It has been moved
	//to the responsibility of the script to reset the locomotor before moving. This is needed because
	//other systems (like the battle drone) change the locomotor based on what it's trying to do, and
	//doesn't want to get reset when ordered to move.
	//chooseLocomotorSet(LOCOMOTORSET_NORMAL);

	getStateMachine()->clear();
	getStateMachine()->setGoalWaypoint( way );
	setLastCommandSource( cmdSource );
	getStateMachine()->setState( AI_FOLLOW_WAYPOINT_PATH_AS_INDIVIDUALS );
}

//----------------------------------------------------------------------------------------
/**
 * Start following the path from the given point
 */
void AIUpdateInterface::privateFollowWaypointPathExact( const Waypoint *way, CommandSourceType cmdSource )
{
	if (getObject()->isMobile() == FALSE)
		return;

	//Resetting the locomotor here was initially added for scripting purposes. It has been moved
	//to the responsibility of the script to reset the locomotor before moving. This is needed because
	//other systems (like the battle drone) change the locomotor based on what it's trying to do, and
	//doesn't want to get reset when ordered to move.
	//chooseLocomotorSet(LOCOMOTORSET_NORMAL);

	getStateMachine()->clear();
	getStateMachine()->setGoalWaypoint( way );
	setLastCommandSource( cmdSource );
	getStateMachine()->setState( AI_FOLLOW_WAYPOINT_PATH_AS_INDIVIDUALS_EXACT );
}

//----------------------------------------------------------------------------------------
/**
 * Start following the path from the given point
 */
void AIUpdateInterface::privateFollowWaypointPathAsTeam( const Waypoint *way, CommandSourceType cmdSource )
{
	if (getObject()->isMobile() == FALSE)
		return;

	//Resetting the locomotor here was initially added for scripting purposes. It has been moved
	//to the responsibility of the script to reset the locomotor before moving. This is needed because
	//other systems (like the battle drone) change the locomotor based on what it's trying to do, and
	//doesn't want to get reset when ordered to move.
	//chooseLocomotorSet(LOCOMOTORSET_NORMAL);

	getStateMachine()->clear();
	getStateMachine()->setGoalWaypoint( way );
	setLastCommandSource( cmdSource );
	getStateMachine()->setState( AI_FOLLOW_WAYPOINT_PATH_AS_TEAM );
}

//----------------------------------------------------------------------------------------
/**
 * Start following the path from the given point
 */
void AIUpdateInterface::privateFollowWaypointPathAsTeamExact( const Waypoint *way, CommandSourceType cmdSource )
{
	if (getObject()->isMobile() == FALSE)
		return;

	//Resetting the locomotor here was initially added for scripting purposes. It has been moved
	//to the responsibility of the script to reset the locomotor before moving. This is needed because
	//other systems (like the battle drone) change the locomotor based on what it's trying to do, and
	//doesn't want to get reset when ordered to move.
	//chooseLocomotorSet(LOCOMOTORSET_NORMAL);

	getStateMachine()->clear();
	getStateMachine()->setGoalWaypoint( way );
	setLastCommandSource( cmdSource );
	getStateMachine()->setState( AI_FOLLOW_WAYPOINT_PATH_AS_TEAM_EXACT );
}

//----------------------------------------------------------------------------------------
void AIUpdateInterface::privateFollowPathAppend( const Coord3D *pos, CommandSourceType cmdSource )
{
	// We're adding a dynamic waypoint!
	Bool effectivelyMoving = isMoving() || isWaitingForPath();

	if (getAIStateType() == AI_FOLLOW_PATH && getStateMachine()->getGoalPathSize() > 0 && effectivelyMoving)
	{
		//We already have a path, so simply add the point to the end of it!
		getStateMachine()->addToGoalPath(pos);
	}
	else if (effectivelyMoving)
	{
		//Our unit is moving to a point already so simply add our waypoint after that point
		//and convert it to a waypoint command!
		std::vector<Coord3D> path;
		path.push_back( *getGoalPosition() );
		path.push_back( *pos );
		privateFollowPath( &path, NULL, cmdSource, false );
	}
	else
	{
		//Hopefully we're idle or doing something that doesn't require movement.
		std::vector<Coord3D> path;
		path.push_back( *pos );
		privateFollowPath( &path, NULL, cmdSource, false );
	}
}

//----------------------------------------------------------------------------------------
/**
 * Remember the rally point of the producer that just built us.  The exit path is only the step out
 * of the door; update() turns this into an attack move once that step is done.
 */
void AIUpdateInterface::friend_setExitProductionRallyPoint( const Coord3D *pos )
{
	m_exitProductionRallyPoint = *pos;
	m_hasExitProductionRallyPoint = TRUE;
}

//----------------------------------------------------------------------------------------
/**
 * Remember the spot to come back to once the salvage crate we were just sent to is collected.
 */
void AIUpdateInterface::friend_setSalvageReturnPosition( const Coord3D *pos )
{
	m_salvageReturnPosition = *pos;
	m_hasSalvageReturnPosition = TRUE;
}

//----------------------------------------------------------------------------------------
/**
 * Turn an order to go to `goal` into an enter into `entrance`.  update() takes it from there: out of
 * the mouth nearest the goal, then the last leg to it.
 */
Bool AIUpdateInterface::takeTunnelTrip( Object *entrance, const Coord3D *goal, TunnelTripEnd end, CommandSourceType cmdSource )
{
	if (!isDoingGroundMovement() || !TheActionManager->canEnterObject( getObject(), entrance, cmdSource, DONT_CHECK_CAPACITY ))
		return FALSE;

	// a player's enter ends any trip (aiDoCommand), so the trip is set after it
	aiEnter( entrance, cmdSource );
	m_tunnelTripGoal = *goal;
	m_tunnelTripEnd = end;
	m_hasTunnelTrip = TRUE;
	return TRUE;
}

//----------------------------------------------------------------------------------------
/**
 * Follow the path defined by the given array of points
 */
void AIUpdateInterface::privateFollowPath( std::vector<Coord3D>* path, Object *ignoreObject, CommandSourceType cmdSource, Bool exitProduction )
{
	if (getObject()->isMobile() == FALSE)
		return;

	//Resetting the locomotor here was initially added for scripting purposes. It has been moved
	//to the responsibility of the script to reset the locomotor before moving. This is needed because
	//other systems (like the battle drone) change the locomotor based on what it's trying to do, and
	//doesn't want to get reset when ordered to move.
	//chooseLocomotorSet(LOCOMOTORSET_NORMAL);

	// clear current state machine
	getStateMachine()->clear();

	if (path->size()>0) {
		const Coord3D goal = (*path)[path->size()-1];
		getStateMachine()->setGoalPosition(&goal);
	}
	// set path info
	getStateMachine()->setGoalPath( path );


	// set the command source
	setLastCommandSource( cmdSource );

	ignoreObstacle(ignoreObject);

	// start us following
	getStateMachine()->setState( exitProduction ? AI_FOLLOW_EXITPRODUCTION_PATH : AI_FOLLOW_PATH );

}

//----------------------------------------------------------------------------------------
/**
 * Attack given object
 */
void AIUpdateInterface::privateAttackObject( Object *victim, Int maxShotsToFire, CommandSourceType cmdSource )
{
	//Resetting the locomotor here was initially added for scripting purposes. It has been moved
	//to the responsibility of the script to reset the locomotor before moving. This is needed because
	//other systems (like the battle drone) change the locomotor based on what it's trying to do, and
	//doesn't want to get reset when ordered to move.
	//chooseLocomotorSet(LOCOMOTORSET_NORMAL);

	if (!victim) 
	{
		// Hard to kill em if they're already dead.  jba
		return;
	}

	getStateMachine()->clear();
	getStateMachine()->setGoalObject( victim );
	setLastCommandSource( cmdSource );
	getStateMachine()->setState( AI_ATTACK_OBJECT );

	// do this after setting it as the current state, as the max-shots-to-fire is reset in AttackState::onEnter()
	Weapon* weapon = getObject()->getCurrentWeapon();
	if (weapon)
		weapon->setMaxShotCount(maxShotsToFire);
}

//-----------------------------------------------------------------------------------------
void AIUpdateInterface::privateForceAttackObject( Object *victim, Int maxShotsToFire, CommandSourceType cmdSource )
{
	if (!victim) {
		return;
	}

	getStateMachine()->clear();
	getStateMachine()->setGoalObject( victim );
	setLastCommandSource( cmdSource );
	getStateMachine()->setState( AI_FORCE_ATTACK_OBJECT );

	// do this after setting it as the current state, as the max-shots-to-fire is reset in AttackState::onEnter()
	Weapon* weapon = getObject()->getCurrentWeapon();
	if (weapon)
		weapon->setMaxShotCount(maxShotsToFire);
}

//-----------------------------------------------------------------------------------------
void AIUpdateInterface::privateGuardRetaliate( Object *victim, const Coord3D *pos, Int maxShotsToFire, CommandSourceType cmdSource )
{
	if (!victim) {
		return;
	}

	getStateMachine()->clear();
	getStateMachine()->setGoalObject( victim );
	setGoalPositionClipped( pos, cmdSource );
	setLastCommandSource( cmdSource );
	getStateMachine()->setState( AI_GUARD_RETALIATE );

	// do this after setting it as the current state, as the max-shots-to-fire is reset in AttackState::onEnter()
	Weapon* weapon = getObject()->getCurrentWeapon();
	if (weapon)
		weapon->setMaxShotCount(maxShotsToFire);
}

//----------------------------------------------------------------------------------------
/**
 * Attack the given team
 */
void AIUpdateInterface::privateAttackTeam( const Team *team, Int maxShotsToFire, CommandSourceType cmdSource )
{
	//Resetting the locomotor here was initially added for scripting purposes. It has been moved
	//to the responsibility of the script to reset the locomotor before moving. This is needed because
	//other systems (like the battle drone) change the locomotor based on what it's trying to do, and
	//doesn't want to get reset when ordered to move.
	//chooseLocomotorSet(LOCOMOTORSET_NORMAL);

	getStateMachine()->clear();
	getStateMachine()->setGoalTeam( team );
	setLastCommandSource( cmdSource );
	getStateMachine()->setState( AI_ATTACK_SQUAD );

	// do this after setting it as the current state, as the max-shots-to-fire is reset in AttackState::onEnter()
	Weapon* weapon = getObject()->getCurrentWeapon();
	if (weapon)
		weapon->setMaxShotCount(maxShotsToFire);
}

//----------------------------------------------------------------------------------------
/**
 * Attack given spot
 */
void AIUpdateInterface::privateAttackPosition( const Coord3D *pos, Int maxShotsToFire, CommandSourceType cmdSource )
{
	//Resetting the locomotor here was initially added for scripting purposes. It has been moved
	//to the responsibility of the script to reset the locomotor before moving. This is needed because
	//other systems (like the battle drone) change the locomotor based on what it's trying to do, and
	//doesn't want to get reset when ordered to move.
	//chooseLocomotorSet(LOCOMOTORSET_NORMAL);

	Coord3D localPos = *pos;
	pos = NULL;

	// ick... rather grody hack for disarming stuff. if we attack a position,
	// but have a "continue range" for the weapon, try to find a suitable object
	// to attack first.
	Weapon* weapon = getObject()->getCurrentWeapon();
	Real continueRange = weapon ? weapon->getContinueAttackRange() : 0.0f;
	if (continueRange > 0.0f)
	{
		// ick. set this bit so we can find the mine to go target, even if stealthed. (srj)
		getObject()->setStatus( MAKE_OBJECT_STATUS_MASK( OBJECT_STATUS_IGNORING_STEALTH ) );
		PartitionFilterPossibleToAttack filterAttack(ATTACK_NEW_TARGET, getObject(), cmdSource);
		PartitionFilterSameMapStatus filterMapStatus(getObject());
		PartitionFilter *filters[] = { &filterAttack, &filterMapStatus, NULL };
		// P4/P6: the ordered spot and the weapon's continue range are float
		const FCoord3D spot = fcoordFromCoord3D(localPos);
		Object* victim = ThePartitionManager->getClosestObjectFix(&spot, fixFromReal(continueRange), FROM_CENTER_2D, filters);
		getObject()->clearStatus( MAKE_OBJECT_STATUS_MASK( OBJECT_STATUS_IGNORING_STEALTH ) );

		if (victim)
		{
 			aiAttackObject(victim, maxShotsToFire, cmdSource);
			return;
		}
		else
		{
			// limit 'em to one shot, and fall thru.
			maxShotsToFire = 1;
		}
	}

	// if it's a contact weapon, we must be able to path to the target pos. if not, find a spot close by.
	// this fixes an obscure bug with mine-clearing: if you tell someone to clear mines and put the centerpoint
	// inside a building, the dozer/worker will just go thru the building to that spot. ick. so if you find that
	// this clause (below) is problematic, you'll probbaly have to find another way to fix this mine-clearing bug. (srj)
	// Already in range means there is nothing to walk to: the weapon goes off where it stands.
	if (weapon && weapon->isContactWeapon() && !weapon->isWithinAttackRange(getObject(), &localPos) && !isPathAvailable(&localPos))
	{
		FindPositionOptions fpOptions;
		fpOptions.minRadius = 0.0f;
		fpOptions.maxRadius = 100.0f;
		fpOptions.sourceToPathToDest = getObject();// This makes it find a place forWhom can get to.
		Coord3D tmp;
		if (ThePartitionManager->findPositionAround(&localPos, &fpOptions, &tmp))
			localPos = tmp;
	}

	getStateMachine()->clear();
	destroyPath();
	setGoalPositionClipped(&localPos, cmdSource);
	setLastCommandSource( cmdSource );
	getStateMachine()->setState( AI_ATTACK_POSITION );


	//Set the goal object to NULL because if we are attacking a location, we need to be able to move up to it properly.
	//When this isn't set, the move aborts before getting into firing range, thus deadlocks.
	getStateMachine()->setGoalObject( NULL );

	// do this after setting it as the current state, as the max-shots-to-fire is reset in AttackState::onEnter()
	weapon = getObject()->getCurrentWeapon();
	if (weapon)
		weapon->setMaxShotCount(maxShotsToFire);
}

//----------------------------------------------------------------------------------------
/**
 * Attack move to the given location
 */
void AIUpdateInterface::privateAttackMoveToPosition( const Coord3D *pos, Int maxShotsToFire, CommandSourceType cmdSource )
{
	if (m_isAiDead || getObject()->isMobile() == FALSE)
		return;

	//Resetting the locomotor here was initially added for scripting purposes. It has been moved
	//to the responsibility of the script to reset the locomotor before moving. This is needed because
	//other systems (like the battle drone) change the locomotor based on what it's trying to do, and
	//doesn't want to get reset when ordered to move.
	//chooseLocomotorSet(LOCOMOTORSET_NORMAL);

	getStateMachine()->clear();
	setGoalPositionClipped(pos, cmdSource);
	setLastCommandSource( cmdSource );
	getStateMachine()->setState( AI_ATTACK_MOVE_TO );

	// do this after setting it as the current state, as the max-shots-to-fire is reset in AttackState::onEnter()
	Weapon* weapon = getObject()->getCurrentWeapon();
	if (weapon)
		weapon->setMaxShotCount(maxShotsToFire);
}

//----------------------------------------------------------------------------------------
/**
 * Attack move down a given waypoint path. If asTeam is TRUE, do so as a team.
 */
void AIUpdateInterface::privateAttackFollowWaypointPath( const Waypoint *way, Int maxShotsToFire, Bool asTeam, CommandSourceType cmdSource )
{
	if (m_isAiDead || getObject()->isMobile() == FALSE)
		return;

	//Resetting the locomotor here was initially added for scripting purposes. It has been moved
	//to the responsibility of the script to reset the locomotor before moving. This is needed because
	//other systems (like the battle drone) change the locomotor based on what it's trying to do, and
	//doesn't want to get reset when ordered to move.
	//chooseLocomotorSet(LOCOMOTORSET_NORMAL);

	getStateMachine()->clear();
	getStateMachine()->setGoalWaypoint( way );
	setLastCommandSource( cmdSource );
	getStateMachine()->setState( (asTeam ? AI_ATTACKFOLLOW_WAYPOINT_PATH_AS_TEAM : AI_ATTACKFOLLOW_WAYPOINT_PATH_AS_INDIVIDUALS) );

	// do this after setting it as the current state, as the max-shots-to-fire is reset in AttackState::onEnter()
	Weapon* weapon = getObject()->getCurrentWeapon();
	if (weapon)
		weapon->setMaxShotCount(maxShotsToFire);
}


//----------------------------------------------------------------------------------------
/**
 * Begin "seek and destroy"
 */
void AIUpdateInterface::privateHunt( CommandSourceType cmdSource )
{
	if (getObject()->isMobile() == FALSE)
		return;

	if (getObject()->isKindOf(KINDOF_PROJECTILE))
		return;

	//Resetting the locomotor here was initially added for scripting purposes. It has been moved
	//to the responsibility of the script to reset the locomotor before moving. This is needed because
	//other systems (like the battle drone) change the locomotor based on what it's trying to do, and
	//doesn't want to get reset when ordered to move.
	//chooseLocomotorSet(LOCOMOTORSET_NORMAL);

	getStateMachine()->clear();
	setLastCommandSource( cmdSource );
	getStateMachine()->setState( AI_HUNT );
}

//----------------------------------------------------------------------------------------
/**
 * Begin "seek and destroy"
 */
void AIUpdateInterface::privateAttackArea( const PolygonTrigger *areaToGuard, CommandSourceType cmdSource )
{
	if (getObject()->isMobile() == FALSE)
		return;

	if (getObject()->isKindOf(KINDOF_PROJECTILE))
		return;

	m_areaToGuard = areaToGuard;

	//Resetting the locomotor here was initially added for scripting purposes. It has been moved
	//to the responsibility of the script to reset the locomotor before moving. This is needed because
	//other systems (like the battle drone) change the locomotor based on what it's trying to do, and
	//doesn't want to get reset when ordered to move.
	//chooseLocomotorSet(LOCOMOTORSET_NORMAL);

	getStateMachine()->clear();
	setLastCommandSource( cmdSource );
	getStateMachine()->setState( AI_ATTACK_AREA);
}

//----------------------------------------------------------------------------------------
/**
 * Repair the given object
 */
void AIUpdateInterface::privateRepair( Object *obj, CommandSourceType cmdSource )
{

	// there is no "default" way for generic objects to repair each other
	return;
				
}

#ifdef ALLOW_SURRENDER
//----------------------------------------------------------------------------------------
/**
	* Pick up prisoner
	*/
void AIUpdateInterface::privatePickUpPrisoner( Object *prisoner, CommandSourceType cmdSource )
{

	// there is no "default" way for generic units to pick up prisoners
	return;

}
#endif

#ifdef ALLOW_SURRENDER
//----------------------------------------------------------------------------------------
/**
	* Return prisoners
	*/
void AIUpdateInterface::privateReturnPrisoners( Object *prison, CommandSourceType cmdSource )
{

	// there is no "default" way for generic units to return prisoners
	return;

}
#endif

//----------------------------------------------------------------------------------------
/**
	* Resume construction of object
	*/
void AIUpdateInterface::privateResumeConstruction( Object *obj, CommandSourceType cmdSource )
{

	// there is no "default" way for generic objects to resume construction
	return;

}

//----------------------------------------------------------------------------------------
/**
 * Get healed at the heal depot
 */
void AIUpdateInterface::privateGetHealed( Object *healDepot, CommandSourceType cmdSource )
{

  // sanity, if we can't get healed from here get outta here
	if( TheActionManager->canGetHealedAt( getObject(), healDepot, cmdSource ) == FALSE )
		return;

	// enter the heal dest for healing
	aiEnter( healDepot, cmdSource );

}

//----------------------------------------------------------------------------------------
/**
 * Get repaired at the repair depot
 */
void AIUpdateInterface::privateGetRepaired( Object *repairDepot, CommandSourceType cmdSource )
{

	// sanity, if we can't get repaired from here get out of here
	if( TheActionManager->canGetRepairedAt( getObject(), repairDepot, cmdSource ) == FALSE )
		return;

	// dock with the repair depot
	aiDock( repairDepot, cmdSource );

}

//----------------------------------------------------------------------------------------
/**
 * Enter the given object
 */
void AIUpdateInterface::privateEnter( Object *obj, CommandSourceType cmdSource )
{
	Object *me = getObject();
	if( me->isMobile() == FALSE )
		return;

	//Resetting the locomotor here was initially added for scripting purposes. It has been moved
	//to the responsibility of the script to reset the locomotor before moving. This is needed because
	//other systems (like the battle drone) change the locomotor based on what it's trying to do, and
	//doesn't want to get reset when ordered to move.
	//chooseLocomotorSet(LOCOMOTORSET_NORMAL);

	if( TheActionManager->canEnterObject( me, obj, cmdSource, DONT_CHECK_CAPACITY ) )
	{
		getStateMachine()->clear();
		getStateMachine()->setGoalObject( obj );
		setLastCommandSource( cmdSource );
		getStateMachine()->setState( AI_ENTER );
	}
}

//----------------------------------------------------------------------------------------
/**
 * Dock with the given object
 */
void AIUpdateInterface::privateDock( Object *obj, CommandSourceType cmdSource )
{
	if (getObject()->isMobile() == FALSE)
		return;

	getStateMachine()->clear();
	getStateMachine()->setGoalObject( obj );
	setLastCommandSource( cmdSource );
	getStateMachine()->setState( AI_DOCK );
}

//----------------------------------------------------------------------------------------
void AIUpdateInterface::privateCombatDrop( Object *target, const Coord3D& pos, CommandSourceType cmdSource )
{
	DEBUG_CRASH(("default implementation, should never be called"));
	if( getObject()->getContain() )
	{
		getObject()->getContain()->removeAllContained(FALSE);
	}
}

//----------------------------------------------------------------------------------------
/**
 * Get out of whatever it is inside of
 */
void AIUpdateInterface::privateExit( Object *objectToExit, CommandSourceType cmdSource )
{
	Object *us = getObject();
	if (!objectToExit)
	{
		objectToExit = us->getContainedBy();

		if (!objectToExit)
			return;
	}
	else
	{
		// An object cannot get out of something it is not inside.  The order carries a container
		// with it, and a stale one - the transport it left last, a building it was told to leave
		// before somebody else pulled it out - used to be obeyed anyway.  Ask the container, not
		// the passenger: a tunnel network shares one list, so the mouth being unloaded is rarely
		// the mouth the passenger walked into.
		const ContainModuleInterface *contain = objectToExit->getContain();
		if (contain == NULL || !contain->isContained(us))
			return;
	}

  if ( objectToExit->isDisabledByType( DISABLED_SUBDUED ) )
    return;


	// we must go thru this state (rather than calling exitObjectViaDoor directly!), 
	// because a few containers might need to delay to allow
	// us to exit (eg, Chinooks must land), meaning we might have to wait a bit, and coordinate
	// with the container by actually NOTIFYING it that we want to exit...
	getStateMachine()->clear();
	getStateMachine()->setGoalObject( objectToExit );
	setLastCommandSource( cmdSource );
	getStateMachine()->setState( AI_EXIT );
}

//----------------------------------------------------------------------------------------
/**
 * Get out of whatever it is inside of this frame
 */
void AIUpdateInterface::privateExitInstantly( Object *objectToExit, CommandSourceType cmdSource )
{
	Object *us = getObject();
	if (!objectToExit)
	{
		objectToExit = us->getContainedBy();

		if (!objectToExit)
			return;
	}
	else
	{
		// An object cannot get out of something it is not inside.  The order carries a container
		// with it, and a stale one - the transport it left last, a building it was told to leave
		// before somebody else pulled it out - used to be obeyed anyway.  Ask the container, not
		// the passenger: a tunnel network shares one list, so the mouth being unloaded is rarely
		// the mouth the passenger walked into.
		const ContainModuleInterface *contain = objectToExit->getContain();
		if (contain == NULL || !contain->isContained(us))
			return;
	}

  if ( objectToExit->isDisabledByType( DISABLED_SUBDUED ) )
    return;

	// we must go thru this state (rather than calling exitObjectViaDoor directly!), 
	// because a few containers might need to delay to allow
	// us to exit (eg, Chinooks must land), meaning we might have to wait a bit, and coordinate
	// with the container by actually NOTIFYING it that we want to exit...
	getStateMachine()->clear();
	getStateMachine()->setGoalObject( objectToExit );
	setLastCommandSource( cmdSource );
	getStateMachine()->setState( AI_EXIT_INSTANTLY );
}


//----------------------------------------------------------------------------------------
/**
 * Get out of whatever it is inside of
 */
void AIUpdateInterface::doQuickExit( std::vector<Coord3D>* path )
{

	Bool locked = getStateMachine()->isLocked();
	getStateMachine()->unlock();

	// set path info
	getStateMachine()->setGoalPath( path );

	getStateMachine()->setTemporaryState( AI_FOLLOW_EXITPRODUCTION_PATH, 10*LOGICFRAMES_PER_SECOND);
	if (locked) {
		getStateMachine()->lock("Relocking in doQuickExit.");
	}
}

//----------------------------------------------------------------------------------------
/**
 * Empty its contents
 */
void AIUpdateInterface::privateEvacuate( Int exposeStealthUnits, CommandSourceType cmdSource )
{

  if ( getObject()->isDisabledByType( DISABLED_SUBDUED ) )
    return;


	ContainModuleInterface *contain = getObject()->getContain();
	if( contain )
	{
		if( exposeStealthUnits )
		{
			contain->markAllPassengersDetected();
		}
		contain->orderAllPassengersToExit( cmdSource, FALSE );
	}
}

//----------------------------------------------------------------------------------------
/**
 * Empty its contents this frame
 */
void AIUpdateInterface::privateEvacuateInstantly( Int exposeStealthUnits, CommandSourceType cmdSource )
{

  if ( getObject()->isDisabledByType( DISABLED_SUBDUED ) )
    return;


	ContainModuleInterface *contain = getObject()->getContain();
	if( contain )
	{
		if( exposeStealthUnits )
		{
			contain->markAllPassengersDetected();
		}
		contain->orderAllPassengersToExit( cmdSource, TRUE );
	}
}

// ------------------------------------------------------------------------------------------------
void AIUpdateInterface::privateExecuteRailedTransport( CommandSourceType cmdSource )
{

	// there is no default implementation for this

}

//----------------------------------------------------------------------------------------
///< life altering state change, if this AI can do it
void AIUpdateInterface::privateGoProne( const DamageInfo *damageInfo, CommandSourceType )
{
	static NameKeyType proneModuleKey = TheNameKeyGenerator->nameToKey( "ProneUpdate" );
	ProneUpdate *proneModule = (ProneUpdate *)getObject()->findUpdateModule( proneModuleKey );

	if( proneModule )
		proneModule->goProne( damageInfo );
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
/**
 * Wander around
 */
void AIUpdateInterface::privateWander( const Waypoint *way, CommandSourceType cmdSource )
{
	if (getObject()->isMobile() == FALSE)
		return;

	//Resetting the locomotor here was initially added for scripting purposes. It has been moved
	//to the responsibility of the script to reset the locomotor before moving. This is needed because
	//other systems (like the battle drone) change the locomotor based on what it's trying to do, and
	//doesn't want to get reset when ordered to move.
	//chooseLocomotorSet(LOCOMOTORSET_WANDER);

	getStateMachine()->clear();
	setLastCommandSource( cmdSource );
	getStateMachine()->setGoalWaypoint( way );
	getStateMachine()->setState( AI_WANDER );
	
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
/**
 * Wander around
 */
void AIUpdateInterface::privateWanderInPlace( CommandSourceType cmdSource )
{
	if (getObject()->isMobile() == FALSE)
		return;

	//Resetting the locomotor here was initially added for scripting purposes. It has been moved
	//to the responsibility of the script to reset the locomotor before moving. This is needed because
	//other systems (like the battle drone) change the locomotor based on what it's trying to do, and
	//doesn't want to get reset when ordered to move.
	//chooseLocomotorSet(LOCOMOTORSET_WANDER);

	getStateMachine()->clear();
	setLastCommandSource( cmdSource );
	getStateMachine()->setState( AI_WANDER_IN_PLACE );
	
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
/**
 * Panic
 */
void AIUpdateInterface::privatePanic( const Waypoint *way, CommandSourceType cmdSource )
{
	if (getObject()->isMobile() == FALSE)
		return;

	//Resetting the locomotor here was initially added for scripting purposes. It has been moved
	//to the responsibility of the script to reset the locomotor before moving. This is needed because
	//other systems (like the battle drone) change the locomotor based on what it's trying to do, and
	//doesn't want to get reset when ordered to move.
	//chooseLocomotorSet(LOCOMOTORSET_PANIC);
	
	getStateMachine()->clear();
	setLastCommandSource( cmdSource );
	getStateMachine()->setGoalWaypoint( way );
	getStateMachine()->setState( AI_PANIC );
	
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
/**
 * Busy
 */
void AIUpdateInterface::privateBusy( CommandSourceType cmdSource )
{
	getStateMachine()->clear();
	setLastCommandSource( cmdSource );
	getStateMachine()->setState( AI_BUSY );
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
/**
 * Guard the given spot
 */
void AIUpdateInterface::privateGuardPosition( const Coord3D *pos, GuardMode guardMode, CommandSourceType cmdSource )
{
	if (getObject()->isMobile() == FALSE)
		return;

	if (getObject()->isKindOf(KINDOF_PROJECTILE))
		return;

	if (m_guardTargetType[1] == GUARDTARGET_NONE) {
		m_guardTargetType[1] = GUARDTARGET_LOCATION;
	} else {
		m_guardTargetType[0] = GUARDTARGET_LOCATION;
	}
	Coord3D adjPos = *pos;
	if (cmdSource==CMD_FROM_PLAYER) {
		// Clip to playable area.
		Region3D r;
		TheTerrainLogic->getExtent(&r);
		if (!r.isInRegionNoZ(&adjPos))
			adjPos = TheTerrainLogic->findClosestEdgePoint(&adjPos);
	}
	m_locationToGuard = adjPos;
	m_guardMode = guardMode;

	getStateMachine()->clear();
	// The guard machine moves on its own goal, so the outer one kept whatever the last order left
	// in it, (0,0,0) on a fresh unit. An ALT-queued move appends after the goal position and so
	// started its path from the map's corner.
	setGoalPositionClipped( &adjPos, cmdSource );
	setLastCommandSource( cmdSource );
	getStateMachine()->setState( AI_GUARD );
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
/**
 * Guard the given spot
 */
void AIUpdateInterface::privateGuardTunnelNetwork( GuardMode guardMode, CommandSourceType cmdSource )
{
	if (getObject()->isMobile() == FALSE)
		return;

	if (getObject()->isKindOf(KINDOF_PROJECTILE))
		return;

	m_guardMode = guardMode;

	getStateMachine()->clear();
	setLastCommandSource( cmdSource );
	getStateMachine()->setState( AI_GUARD_TUNNEL_NETWORK );
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
/**
 * Guard the given spot
 */
void AIUpdateInterface::privateGuardObject( Object *objectToGuard, GuardMode guardMode, CommandSourceType cmdSource )
{
	if (getObject()->isMobile() == FALSE)
		return;

	if (getObject()->isKindOf(KINDOF_PROJECTILE))
		return;

	if (m_guardTargetType[1] == GUARDTARGET_NONE) {
		m_guardTargetType[1] = GUARDTARGET_OBJECT;
	} else {
		m_guardTargetType[0] = GUARDTARGET_OBJECT;
	}
	m_guardMode = guardMode;
	m_objectToGuard = objectToGuard->getID();

	getStateMachine()->clear();
	setLastCommandSource( cmdSource );
	getStateMachine()->setState( AI_GUARD );
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
/**
 * Guard the given spot
 */
void AIUpdateInterface::privateGuardArea( const PolygonTrigger *areaToGuard, GuardMode guardMode, CommandSourceType cmdSource )
{
	if (getObject()->isMobile() == FALSE)
		return;

	if (getObject()->isKindOf(KINDOF_PROJECTILE))
		return;

	if (m_guardTargetType[1] == GUARDTARGET_NONE) {
		m_guardTargetType[1] = GUARDTARGET_AREA;
	} else {
		m_guardTargetType[0] = GUARDTARGET_AREA;
	}
	m_areaToGuard = areaToGuard;
	m_guardMode = guardMode;

	Coord3D pos;
	m_areaToGuard->getCenterPoint(&pos);
	m_locationToGuard = pos;
	m_objectToGuard = INVALID_ID; //just in case.
	getStateMachine()->clear();
	setLastCommandSource( cmdSource );
	getStateMachine()->setState( AI_GUARD );
}

//-------------------------------------------------------------------------------------------------
void AIUpdateInterface::privateHackInternet( CommandSourceType cmdSource )
{
	// We need to be able to hack in containers
//	if (getObject()->isMobile() == FALSE)
//		return;

	getStateMachine()->clear();
	setLastCommandSource( cmdSource );

	static NameKeyType key_HackInternetAIUpdate = NAMEKEY("HackInternetAIUpdate");
	HackInternetAIUpdate *ai = (HackInternetAIUpdate*)getObject()->findUpdateModule( key_HackInternetAIUpdate );
	if( ai )
	{
		ai->hackInternet();
	}
	else
	{
		DEBUG_CRASH(("Unit %s is expecting a 'Update = HackInternetAIUpdate' entry in FactionUnit.ini", getObject()->getTemplate()->getName().str() ) );
	}
}

/// if we are attacking "fromID", stop that and attack "toID" instead
void AIUpdateInterface::transferAttack(ObjectID fromID, ObjectID toID)
{
	Object *newTarget = TheGameLogic->findObjectByID( toID );

	if (m_currentVictimID == fromID)
		m_currentVictimID = toID;

	Object* goalObj = getStateMachine()->getGoalObject();
	if (goalObj && goalObj->getID() == fromID)
		getStateMachine()->setGoalObject( newTarget );

	//Transfer the turrets too this frame.
	for( Int i = 0; i < MAX_TURRETS; i++ )
	{
		goalObj = getTurretTargetObject( (WhichTurretType)i, FALSE );
		if( goalObj && goalObj->getID() == fromID )
		{
			setTurretTargetObject( (WhichTurretType)i, newTarget, TRUE );
		}
	}

}

//----------------------------------------------------------------------------------------------------------
/**
 * Indicate who we are attacking.
 */
void AIUpdateInterface::setCurrentVictim( const Object *victim )
{
	if (victim == NULL)
	{
		// be paranoid, in case we are called from dtors, etc.
		if (m_currentVictimID != INVALID_ID)
		{
			Object* self = getObject();
			Object* target = TheGameLogic->findObjectByID(m_currentVictimID);
			if (self != NULL && target != NULL)
			{
				AIUpdateInterface* targetAI = target->getAI();
				if (targetAI)
				{
					targetAI->addTargeter(self->getID(), FALSE);
				}
			}
		}

		m_currentVictimID = INVALID_ID;
	}
	else
	{
		// we don't add a targeter here, since we usually want to defer
		// that until we are actually aiming (as opposed to, say, approaching)
		// the victim.
		m_currentVictimID = victim->getID();
	}
}

/**
 * Who is our current victim?
 */
Object *AIUpdateInterface::getCurrentVictim( void ) const
{
	if (m_currentVictimID != INVALID_ID)
		return TheGameLogic->findObjectByID( m_currentVictimID );

	return NULL;
}

// if we are attacking a position (and NOT an object), return it. otherwise return null.
const Coord3D *AIUpdateInterface::getCurrentVictimPos( void ) const
{
	if (getObject()->testStatus(OBJECT_STATUS_IS_ATTACKING))
	{
		if (m_currentVictimID == INVALID_ID)
		{
			return getStateMachine()->getGoalPosition();
		}
	}

	return NULL;
}


/**
 * Set the behavior modifier for this agent
 */
void AIUpdateInterface::setAttitude( AttitudeType tude )
{
	m_attitude = tude;
}

/**
 * Get the current behavior modifier state	
 */
AttitudeType AIUpdateInterface::getAttitude( void ) const
{
	return m_attitude;
}

/**
 * Return the current state the AI is in.
 */
AIStateType AIUpdateInterface::getAIStateType() const
{
	return (AIStateType)getStateMachine()->getCurrentStateID();
}

//-------------------------------------------------------------------------------------------------
void AIUpdateInterface::ignoreObstacle( const Object *obj )
{
	m_ignoreObstacleID = obj ? obj->getID() : INVALID_ID;
}

//-------------------------------------------------------------------------------------------------
void AIUpdateInterface::ignoreObstacleID( ObjectID id )
{
	m_ignoreObstacleID = id;
}

//-------------------------------------------------------------------------------------------------
ObjectID AIUpdateInterface::getIgnoredObstacleID( void ) const
{ 
	return m_ignoreObstacleID; 
}

//-------------------------------------------------------------------------------------------------
Object* AIUpdateInterface::getEnterTarget()
{
	AIStateType stateType = getAIStateType();

	if( stateType != AI_ENTER && 
			stateType != AI_GUARD_TUNNEL_NETWORK &&
			stateType != AI_GET_REPAIRED )
		return NULL;

	return getStateMachine()->getGoalObject();
}

//-------------------------------------------------------------------------------------------------
void AIUpdateInterface::setLastCommandSource( CommandSourceType source )
{
	m_lastCommandSource = source; 
}

//-------------------------------------------------------------------------------------------------
UnsignedInt AIUpdateInterface::getMoodMatrixValue( void ) const
{
	UnsignedInt returnVal = 0;
	// seems like a weird way to get my controlling object, but I don't see another
	if (!getStateMachine()) 
	{
		return returnVal;
	}
	
	const Object *owner = getObject();
	Player *player = owner->getControllingPlayer();

	if (!player) 
	{
		return returnVal;
	}
	
	if (player->getPlayerType() == PLAYER_HUMAN) 
	{
		returnVal |= MM_Controller_Player;
		// Human units don't have a mood.

	} 
	else 
	{
		returnVal |= MM_Controller_AI;
		switch (getAttitude())
		{
			case AI_SLEEP:			returnVal |= MM_Mood_Sleep; break;
			case AI_PASSIVE:		returnVal |= MM_Mood_Passive; break;
			case AI_NORMAL:			returnVal |= MM_Mood_Normal; break;
			case AI_ALERT:			returnVal |= MM_Mood_Alert; break;
			case AI_AGGRESSIVE:	returnVal |= MM_Mood_Aggressive; break;
			default: 
				DEBUG_CRASH(("Unknown mood '%d' in getMoodMatrixValue. (Team '%s'). Using normal. (jkmcd)", getAttitude(), getObject()->getTeam()->getName().str() ));
				returnVal |= MM_Mood_Normal;
				break;
		}
	}

	if (getLocomotorSet().getValidSurfaces() & LOCOMOTORSURFACE_AIR) 
	{
		returnVal |= MM_UnitType_Air;
	} 
	else 
	{
		if (m_turretAI[0] != NULL) 
		{
			returnVal |= MM_UnitType_Turreted;
		} 
		else 
		{
			returnVal |= MM_UnitType_NonTurreted;
		}
	}

	return returnVal;
}

//-------------------------------------------------------------------------------------------------
UnsignedInt AIUpdateInterface::getMoodMatrixActionAdjustment( MoodMatrixAction action ) const
{
	// Angry Mob Members (but not Nexi) are never subject to moods. In particular,
	// they must never, ever, ever convert a move into an attack move, or Bad Things
	// will happend, since MobMemberSlavedUpdate expects a moveto to remain a moveto.
	// Mark L sez that members do not, in fact, need any mood adjustment whatsoever,
	// since the mood of the nexus wants to control all this anyway. Unfortunately, there
	// is no KINDOF_MOB_MEMBER, and we don't want to add one at the eleventh hour...
	// this, however, is a unique and safe combination that applies only to mob members. (srj)
	if (getObject()->isKindOf(KINDOF_INFANTRY) && getObject()->isKindOf(KINDOF_IGNORED_IN_GUI))
	{
		return MAA_Action_Ok;
	}

	UnsignedInt moodMatrix = getMoodMatrixValue();
	UnsignedInt returnVal = 0;

	if (moodMatrix & MM_Controller_Player) 
	{
		// Player-controlled units can always do actions (from a mood perspective, at any rate)
		returnVal = MAA_Action_Ok;
		return returnVal;
	}

	returnVal = MAA_Action_Ok;
	switch (action)
	{
		case MM_Action_Idle: 
		{
			switch( moodMatrix & MM_Mood_Bitmask )
			{
				case MM_Mood_Sleep:				returnVal = MAA_Action_Ok | MAA_Affect_Range_IgnoreAll; break;
				case MM_Mood_Passive:			returnVal = MAA_Action_Ok | MAA_Affect_Range_WaitForAttack; break;
				case MM_Mood_Normal:			returnVal = MAA_Action_Ok; break;
				case MM_Mood_Alert:				returnVal = MAA_Action_Ok | MAA_Affect_Range_Alert; break;
				case MM_Mood_Aggressive:	returnVal = MAA_Action_Ok | MAA_Affect_Range_Aggressive; break;
			}
			break;
		}
		case MM_Action_Move:
		{
			switch( moodMatrix & MM_Mood_Bitmask )
			{
				case MM_Mood_Sleep:				returnVal = MAA_Action_To_Idle | MAA_Affect_Range_IgnoreAll; break;
				case MM_Mood_Passive:			returnVal = MAA_Action_Ok | MAA_Affect_Range_WaitForAttack; break;
				case MM_Mood_Normal:			returnVal = MAA_Action_Ok; break;
				case MM_Mood_Alert:				returnVal = MAA_Action_To_AttackMove | MAA_Affect_Range_Alert; break;
				case MM_Mood_Aggressive:	returnVal = MAA_Action_To_AttackMove | MAA_Affect_Range_Aggressive; break;
			}
			break;
		}
		case MM_Action_Attack:
		{
			switch( moodMatrix & MM_Mood_Bitmask )
			{
				case MM_Mood_Sleep:				returnVal = MAA_Action_To_Idle | MAA_Affect_Range_IgnoreAll; break;
				case MM_Mood_Passive:			returnVal = MAA_Action_Ok; break;
				case MM_Mood_Normal:			returnVal = MAA_Action_Ok; break;
				case MM_Mood_Alert:				returnVal = MAA_Action_Ok; break;
				case MM_Mood_Aggressive:	returnVal = MAA_Action_Ok; break;
			}
			break;
		}
		case MM_Action_AttackMove:
		{
			switch( moodMatrix & MM_Mood_Bitmask )
			{
				case MM_Mood_Sleep:				returnVal = MAA_Action_To_Idle | MAA_Affect_Range_IgnoreAll; break;
				case MM_Mood_Passive:			returnVal = MAA_Action_Ok; break;
				case MM_Mood_Normal:			returnVal = MAA_Action_Ok; break;
				case MM_Mood_Alert:				returnVal = MAA_Action_Ok | MAA_Affect_Range_Alert; break;
				case MM_Mood_Aggressive:	returnVal = MAA_Action_Ok | MAA_Affect_Range_Aggressive; break;
			}
			break;
		}
	};

	return returnVal;
}

//----------------------------------------------------------------------------------------------
void AIUpdateInterface::wakeUpAndAttemptToTarget( void )
{
	if (!isIdle()) {
		return;
	}

	UnsignedInt now = TheGameLogic->getFrame();
	m_nextMoodCheckTime = now;
	m_randomlyOffsetMoodCheck = TRUE;
}

//----------------------------------------------------------------------------------------------
/**
 * Reset when we should next look for a target. Usually called by *Idle::onEnter
 */
void AIUpdateInterface::resetNextMoodCheckTime()
{
	UnsignedInt now = TheGameLogic->getFrame();
	m_nextMoodCheckTime = now + TheAI->getAiData()->m_forceIdleFramesCount;
	m_randomlyOffsetMoodCheck = TRUE;
}

//----------------------------------------------------------------------------------------------
void AIUpdateInterface::setNextMoodCheckTime( UnsignedInt frame )
{
	m_nextMoodCheckTime = frame;
	m_randomlyOffsetMoodCheck = false;
}



Bool AIUpdateInterface::canAutoAcquireWhileStealthed() const 
{ 
  if ( getObject() && getObject()->getStealth() && getObject()->getStealth()->isGrantedBySpecialPower() )
    return TRUE;
  return getAIUpdateModuleData()->m_autoAcquireEnemiesWhenIdle & AAS_Idle_Stealthed;
}


//----------------------------------------------------------------------------------------------
/**
 * Return the next object that our mood suggests we should attack.
 */
/** How far past its own reach a unit on attack move looks for something to stop for. */
static const Real ATTACK_MOVE_SEARCH_SCALE = 1.5f;

/** Cosine of half the forward arc an aircraft on attack move will turn for.  0.5 is sixty degrees
	* either side of the nose; written as the cosine because the test below is done squared, which is
	* what keeps a trig call out of the simulation - see Lib/Trig.h. */
static const Fix AIRCRAFT_FORWARD_ARC_COS = 0.5_fx;

//-------------------------------------------------------------------------------------------------
/** An aircraft looks where it is going.
	*
	* It cannot stop, it turns in a wide circle, and a target off to one side or behind it is one it
	* has to come all the way round for - which is how a flight of bombers ends up orbiting a corner
	* of the map instead of arriving anywhere.  So the long look is forward only: inside the arc the
	* full attack-move distance, outside it no further than the aircraft can already shoot, which
	* still lets it take something it happens to be passing. */
//-------------------------------------------------------------------------------------------------
class PartitionFilterForwardArc : public PartitionFilter
{
private:
	const Object *m_self;
	Fix m_sideRangeSqr;			///< how far it will look outside the arc

public:
	// P6: the weapon range arrives as a Real
	PartitionFilterForwardArc( const Object *self, Real sideRange )
		: m_self( self ), m_sideRangeSqr( fixFromReal( sideRange ) * fixFromReal( sideRange ) )
	{ }

	virtual Bool allow( Object *other )
	{
		if( other == NULL || m_self == NULL )
			return FALSE;

		const FCoord3D *myPos = m_self->getPositionFix();
		const FCoord3D *hisPos = other->getPositionFix();
		const Fix dx = hisPos->x - myPos->x;
		const Fix dy = hisPos->y - myPos->y;
		const Fix distSqr = dx * dx + dy * dy;

		// close enough to shoot without turning at all: take it wherever it is
		if( distSqr <= m_sideRangeSqr )
			return TRUE;
		if( distSqr < 0.01_fx )
			return TRUE;

		//
		// dot / dist >= cos(arc), squared on both sides so there is no square root and no cosine in
		// here at all: the simulation has to give the same answer on every machine in the game, and
		// the runtime's own maths does not (Lib/Trig.h).  The sign test is what squaring costs.
		//
		const FCoord3D *myDir = m_self->getUnitDirectionVector2DFix();
		const Fix dot = myDir->x * dx + myDir->y * dy;
		if( dot <= Fix( 0 ) )
			return FALSE;			// behind us

		return ( dot * dot ) >= ( AIRCRAFT_FORWARD_ARC_COS * AIRCRAFT_FORWARD_ARC_COS * distSqr );
	}

#if defined(_DEBUG) || defined(_INTERNAL)
	virtual const char* debugGetName() { return "PartitionFilterForwardArc"; }
#endif
};

//-------------------------------------------------------------------------------------------------
/** A ground unit looks as far as the high ground lets it shoot: the scan runs out to the largest
	* reach any height could give, and this cuts each enemy back to the reach its own height gives. */
//-------------------------------------------------------------------------------------------------
class PartitionFilterElevatedReach : public PartitionFilter
{
private:
	const Object *m_self;
	Real m_range;			///< the search distance on flat ground

public:
	PartitionFilterElevatedReach( const Object *self, Real range )
		: m_self( self ), m_range( range )
	{ }

	virtual Bool allow( Object *other )
	{
		// P6: the weapon's elevated reach is float
		const Fix reach = fixFromReal( Weapon_elevatedRange( m_self, m_range, fixToReal( other->getPositionFix()->z ) ) );
		return ThePartitionManager->getDistanceSquaredFix( m_self, other, FROM_BOUNDINGSPHERE_2D ) <= reach * reach;
	}

#if defined(_DEBUG) || defined(_INTERNAL)
	virtual const char* debugGetName() { return "PartitionFilterElevatedReach"; }
#endif
};

Object* AIUpdateInterface::getNextMoodTarget( Bool calledByAI, Bool calledDuringIdle, Bool allowOutOfWeaponRangeTargets, Bool requireWithinWeaponRange )
{
	Object *obj = getObject();

	// if we're dead, we can't attack
	if (obj->isEffectivelyDead()) 
		return NULL;

	if (obj->testStatus(OBJECT_STATUS_IS_USING_ABILITY)) {
		return NULL;  // we are doing a special ability.  Shouldn't auto-acquire a target at this time.  jba.
	}

	const AIUpdateModuleData* d = getAIUpdateModuleData();
	
	if (calledDuringIdle)
	{
		if ((d->m_autoAcquireEnemiesWhenIdle & AAS_Idle) == 0) 
		{
			return NULL;
		}
	}

// srj sez: this should ignore calledDuringIdle, despite what the name of the bit implies.
	if (isAttacking() && BitTest(d->m_autoAcquireEnemiesWhenIdle, AAS_Idle_Not_While_Attacking))
	{
		return NULL;
	}

	//Check if unit is stealthed... is so we won't acquire targets unless he has
	//AutoAcquireWhenIdle = Yes Stealthed.
	if ( calledDuringIdle )
	{
		if( obj->getStatusBits().test( OBJECT_STATUS_STEALTHED ) ) 
		{
			if( !canAutoAcquireWhileStealthed() ) 
			{
  			const Object *container = obj->getContainedBy();
  			if( ! (container && container->getContain()->isPassengerAllowedToFire()) )
  			{
					// Sorry, stealthed and not allowed to idle fire when stealthed.
					// Being in a firing container is an exception to this veto.
  				return NULL;
  			}
			}
		}
	}

	UnsignedInt now = TheGameLogic->getFrame();

	// Check if team auto targets same victim.
	Object *teamVictim = NULL;
	if (calledByAI && obj->getTeam()->getPrototype()->getTemplateInfo()->m_attackCommonTarget) 
	{
		teamVictim = obj->getTeam()->getTeamTargetObject();
		if (teamVictim) {
			// Make sure we can attack the team victim.  Mixed teams can acquire aircraft, and units
			// like toxin tractors shouldn't acquire aircraft. jba. [8/27/2003]
			CanAttackResult result = obj->getAbleToAttackSpecificObject( ATTACK_NEW_TARGET, teamVictim, CMD_FROM_AI );
			if( result != ATTACKRESULT_POSSIBLE && result != ATTACKRESULT_POSSIBLE_AFTER_MOVING ) {
				teamVictim = NULL; // Can't attack him. jba [8/27/2003]
			}
		}
		
		if (teamVictim && getAttitude()>=AI_NORMAL) 
			return teamVictim;
	}

	DEBUG_ASSERTCRASH(m_nextMoodCheckTime != 0, ("m_nextMoodCheckTime should never be zero here."));

	if (calledByAI)
	{
		// make sure it's time to check again.
		if (now < m_nextMoodCheckTime)
			return NULL;

		Int checkRate = d->m_moodAttackCheckRate;
		m_nextMoodCheckTime = now + checkRate;
		if (m_randomlyOffsetMoodCheck)
		{
			Int halfRate = checkRate >> 1;
			m_nextMoodCheckTime = (UnsignedInt)((Int)m_nextMoodCheckTime + GameLogicRandomValue(-halfRate, halfRate));
			m_randomlyOffsetMoodCheck = FALSE;
		}
	}

	// Use Guard Outer, which typically corresponds to the total range
	Real rangeToFindWithin = TheAI->getAdjustedVisionRangeForObject(obj, AI_VISIONFACTOR_OWNERTYPE | AI_VISIONFACTOR_MOOD);

	//
	// A caller that closes with what it finds (attack move) measures the search against its own
	// weapon rather than against its eyes.  The two have nothing to do with each other - artillery
	// outranges its vision, a scout sees far further than it can shoot - so a unit whose weapon
	// outranged its sight found nothing this way and walked past everything it was ordered to kill,
	// while a unit that sees a long way stopped for things it would spend a minute driving to.
	// Half as far again as it can shoot: far enough to turn and close, near enough that a column
	// does not scatter after everything on the horizon.
	//
	if (allowOutOfWeaponRangeTargets)
	{
		const Real weaponRange = obj->getLargestWeaponRange();
		if (weaponRange > 0.0f)
			rangeToFindWithin = weaponRange * (requireWithinWeaponRange ? 1.0f : ATTACK_MOVE_SEARCH_SCALE);
	}

	if (rangeToFindWithin <= 0.0f)
		return NULL;

	//If we are contained by an object, add it's bounding radius so that large buildings can auto acquire everything in
	//outer ranges. Calculating this from the center is bad... although this code makes it possible to acquire a target
	//outside of range, but in that case, it'll just fail and continue.
	const Object *container = obj->getContainedBy();
	if( container )
	{
		rangeToFindWithin += floatRadiusOf( container );	// P7: the AI's search ranges are float
	}

	UnsignedInt moodMatrixVal = getMoodMatrixValue();
	if ((moodMatrixVal & MM_Controller_AI) && (moodMatrixVal & MM_Mood_Passive)) 
	{
		BodyModuleInterface *bmi = obj->getBodyModule();
		if (!bmi)
			return NULL;

		//Kris: August 26, 2003
		//Do not allow units that healed me to get acquired! They are our friends!!!
		if( bmi->getLastDamageInfo()->in.m_damageType != DAMAGE_HEALING )
		{
			return TheGameLogic->findObjectByID(bmi->getLastDamageInfo()->in.m_sourceID);
		}
	}
	UnsignedInt flags = AI::CAN_ATTACK;
	if (TheAI->getAiData()->m_attackUsesLineOfSight) {
		if (obj->isKindOf(KINDOF_ATTACK_NEEDS_LINE_OF_SIGHT)) {
			flags |= AI::CAN_SEE;
		}
	}

	if (TheAI->getAiData()->m_attackIgnoreInsignificantBuildings) {
		flags |= AI::IGNORE_INSIGNIFICANT_BUILDINGS; 
	}
	
	//
	// Idle auto-acquire ignores buildings unless the unit is one of the few with AttackBuildings in
	// its AutoAcquireEnemiesWhenIdle, which is right for a unit standing around, and wrong for an
	// attack move: the player pointed at the enemy base and the group walked through it without
	// firing at a single structure.  A caller that closes with what it finds takes buildings too;
	// the CAN_ATTACK filter still rejects the ones this unit's weapons cannot hurt.
	//
	if( (d->m_autoAcquireEnemiesWhenIdle & AAS_Idle_Attack_Buildings) || allowOutOfWeaponRangeTargets )
	{
		flags |= AI::ATTACK_BUILDINGS;
	}

	// if we're called by AI, and are human controlled, then our AI will not
	// allow us to pursue the target. therefore, we should ensure that we only
	// look for targets that are already within attack range (as opposed to vision range).
	// The caller can lift that restriction (attack move does) when it will actually close
	// with what it finds instead of driving past it.
	if (calledByAI && !allowOutOfWeaponRangeTargets && obj->getControllingPlayer()->getPlayerType() == PLAYER_HUMAN)
	{
		flags |= AI::WITHIN_ATTACK_RANGE;
	}

	// ... and a caller that has been told not to move at all right now takes only what it can shoot
	// from where it stands, whoever is playing.
	if (requireWithinWeaponRange)
	{
		flags |= AI::WITHIN_ATTACK_RANGE;
	}
	
	//
	// Instead of shroud affecting the ability to attack, it affects the ability to target.
	// The same checks apply as the old WeaponSet check (now commented out, search for getShroudedStatus)
	//
	// This used to carry "&& getPlayerType() == PLAYER_HUMAN", which is not a balance knob but a
	// hard if on who is playing: a computer player's units auto-acquired targets standing in fog it
	// could not see, and a human player's could not.  UNFOGGED is the only thing that engages
	// PartitionFilterFreeOfFog, and getNextMoodTarget is the shared path - attack states, guard, mob
	// members and base defence turrets all route through here - so the exemption covered every one
	// of them.  The AI now sees what a player sees, which is also what makes stealth work against it.
	//
	if( calledByAI && obj->getControllingPlayer() )
	{
		flags |= AI::UNFOGGED;
	}

	//
	// A caller that closes with what it finds picks the biggest threat in range instead of the
	// nearest thing (see AI_threatScore).  Walking up to a dozer while the artillery next to it
	// keeps firing is only sensible for a unit that cannot move to a better target - and this
	// caller can.
	//
	if (allowOutOfWeaponRangeTargets)
	{
		flags |= AI::PREFER_HIGH_THREAT;
	}

	//
	// ... and an aircraft only makes that long look forward - see PartitionFilterForwardArc.
	//
	PartitionFilterForwardArc filterArc( obj, obj->getLargestWeaponRange() );
	PartitionFilterElevatedReach filterHeight( obj, rangeToFindWithin );
	PartitionFilter *arc = NULL;
	if( allowOutOfWeaponRangeTargets && obj->isKindOf( KINDOF_AIRCRAFT ) )
		arc = &filterArc;
	else if( !obj->isKindOf( KINDOF_AIRCRAFT ) && container == NULL )
	{
		arc = &filterHeight;
		rangeToFindWithin += Weapon_elevationRangeBonus( rangeToFindWithin, FLT_MAX );
	}

	Object *newVictim = TheAI->findClosestEnemy(obj, rangeToFindWithin, flags, getAttackInfo(), arc);

/*
DEBUG_LOG(("GNMT frame %d: %s %08lx (con %s %08lx) uses range %f, flags %08lx, %s finds %s %08lx\n",
	now,
	obj->getTemplate()->getName().str(),
	obj,
	container ? container->getTemplate()->getName().str() : "",
	container,
	rangeToFindWithin,
	flags,
	getAttackInfo() != NULL && getAttackInfo() != TheScriptEngine->getDefaultAttackInfo() ? "ATTACKINFO," : "",
	newVictim ? newVictim->getTemplate()->getName().str() : "",
	newVictim
));
*/

	if (newVictim)
	{
		CRCDEBUG_LOG(("AIUpdateInterface::getNextMoodTarget() - %d is attacking %d\n", obj->getID(), newVictim->getID()));
/*
srj debug hack. ignore.
Int ot = getTmpValue();
if (ot!=0&&now>ot&&now-ot<=4)
ot=ot;
setTmpValue(now);
*/
	}

	return newVictim;
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
Bool AIUpdateInterface::hasNationalism() const
{
	const Player *player = getObject()->getControllingPlayer();
	if( player )
	{
		///@todo Find a better way to represent nationalism without hardcoding here (CBD)
		static const UpgradeTemplate *nationalismTemplate = TheUpgradeCenter->findUpgrade( "Upgrade_Nationalism" );
		DEBUG_ASSERTCRASH( nationalismTemplate != NULL, ("AIUpdateInterface::hasNationalism - Nationalism upgrade not found\n") );
		if( nationalismTemplate )
			return player->hasUpgradeComplete( nationalismTemplate );
	}
	return FALSE;
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
Bool AIUpdateInterface::hasFanaticism() const
{
	const Player *player = getObject()->getControllingPlayer();
	if( player )
	{
		///@todo Find a better way to represent fanaticism without hardcoding here (MAL)
		static const UpgradeTemplate *fanaticismTemplate = TheUpgradeCenter->findUpgrade( "Upgrade_Fanaticism" );
		DEBUG_ASSERTCRASH( fanaticismTemplate != NULL, ("AIUpdateInterface::hasFanaticism - Fanaticism upgrade not found\n") );
		if( fanaticismTemplate )
			return player->hasUpgradeComplete( fanaticismTemplate );
	}
	return FALSE;
}

// ------------------------------------------------------------------------------------------------
// the horde module that owns the bonus tells us what it sees, instead of us walking every behavior
// module of the object to work it out again.
// ------------------------------------------------------------------------------------------------
void AIUpdateInterface::evaluateMoraleBonus( Bool inHorde, Bool allowNationalism, HordeActionType type )
{
#ifdef ALLOW_DEMORALIZE
	if( isDemoralized() )
	{
		Object *us = getObject();

		us->setWeaponBonusCondition( WEAPONBONUSCONDITION_DEMORALIZED );

		// a demoralized unit gets none of the three bonuses
		us->clearWeaponBonusCondition( WEAPONBONUSCONDITION_HORDE );
		us->clearWeaponBonusCondition( WEAPONBONUSCONDITION_NATIONALISM );
		us->clearWeaponBonusCondition( WEAPONBONUSCONDITION_FANATICISM );

		Drawable *draw = us->getDrawable();
		if( draw && !us->isKindOf( KINDOF_PORTABLE_STRUCTURE ) )
			draw->setTerrainDecal( TERRAIN_DECAL_DEMORALIZED );

		return;
	}

	getObject()->clearWeaponBonusCondition( WEAPONBONUSCONDITION_DEMORALIZED );
#endif

	//Lorenzen temporarily disabled, since it fights with the horde buff
	//Drawable *draw = getObject()->getDrawable();
	//if ( draw && !getObject()->isKindOf( KINDOF_PORTABLE_STRUCTURE ) )
	//	draw->setTerrainDecal(TERRAIN_DECAL_NONE);

	switch( type )
	{
		case HORDEACTION_HORDE:
			evaluateNationalismBonusClassic( inHorde, allowNationalism );
			break;

		case HORDEACTION_HORDE_FIXED:
			evaluateNationalismBonus( inHorde, allowNationalism );
			break;
	}
}

// ------------------------------------------------------------------------------------------------
// the classic rule: nationalism and fanaticism are granted while the upgrades are owned and are
// never taken away again, even when the horde breaks up.
// ------------------------------------------------------------------------------------------------
void AIUpdateInterface::evaluateNationalismBonusClassic( Bool inHorde, Bool allowNationalism )
{
	Object *us = getObject();

	if( inHorde )
		us->setWeaponBonusCondition( WEAPONBONUSCONDITION_HORDE );
	else
		us->clearWeaponBonusCondition( WEAPONBONUSCONDITION_HORDE );

	if( allowNationalism && hasNationalism() )
	{
		us->setWeaponBonusCondition( WEAPONBONUSCONDITION_NATIONALISM );

		// FOR THE NEW GC INFANTRY GENERAL
		if( hasFanaticism() )
			us->setWeaponBonusCondition( WEAPONBONUSCONDITION_FANATICISM );
		else
			us->clearWeaponBonusCondition( WEAPONBONUSCONDITION_FANATICISM );
	}
	else
		us->clearWeaponBonusCondition( WEAPONBONUSCONDITION_NATIONALISM );
}

// ------------------------------------------------------------------------------------------------
// the fixed rule: all three bonuses follow the horde status, and fanaticism no longer needs
// nationalism to be owned as well.
// ------------------------------------------------------------------------------------------------
void AIUpdateInterface::evaluateNationalismBonus( Bool inHorde, Bool allowNationalism )
{
	Object *us = getObject();

	if( inHorde )
	{
		us->setWeaponBonusCondition( WEAPONBONUSCONDITION_HORDE );

		if( allowNationalism && hasNationalism() )
			us->setWeaponBonusCondition( WEAPONBONUSCONDITION_NATIONALISM );
		else
			us->clearWeaponBonusCondition( WEAPONBONUSCONDITION_NATIONALISM );

		if( allowNationalism && hasFanaticism() )
			us->setWeaponBonusCondition( WEAPONBONUSCONDITION_FANATICISM );
		else
			us->clearWeaponBonusCondition( WEAPONBONUSCONDITION_FANATICISM );
	}
	else
	{
		us->clearWeaponBonusCondition( WEAPONBONUSCONDITION_HORDE );
		us->clearWeaponBonusCondition( WEAPONBONUSCONDITION_NATIONALISM );
		us->clearWeaponBonusCondition( WEAPONBONUSCONDITION_FANATICISM );
	}
}

#ifdef ALLOW_DEMORALIZE
// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void AIUpdateInterface::setDemoralized( UnsignedInt durationInFrames )
{
	UnsignedInt prevDemoralizedFrames = m_demoralizedFramesLeft;

	// overwrite the previous demoralized time left
	m_demoralizedFramesLeft = durationInFrames;

	// if we turned on or turned off we need to re-evaluate our bonus conditions
	if( (prevDemoralizedFrames == 0 && m_demoralizedFramesLeft > 0) ||
			(prevDemoralizedFrames > 0 && m_demoralizedFramesLeft == 0) )
	{

		// evaluate demoralization, nationalism, and horde effect as they are all intertwined
		Object *us = getObject();
		for( BehaviorModule** u = us->getBehaviorModules(); *u; ++u )
		{
			HordeUpdateInterface *hui = (*u)->getHordeUpdateInterface();
			if( hui )
				evaluateMoraleBonus( hui->isInHorde(), hui->isAllowedNationalism(), hui->getHordeActionType() );
		}

	}  // end if

}
#endif

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void AIUpdateInterface::privateCommandButton( const CommandButton *commandButton, CommandSourceType cmdSource )
{
	if( !commandButton )
	{
		return;
	}

	if (getObject()->isKindOf(KINDOF_PROJECTILE))
		return;

	//First of all, it's quite possible to get this far with an object incapable of performing such a task. Scripts will have
	//entire teams of multiple unit types and want to order units to do something... if they can, great.. if not, ignore.
	Object *owner = getObject();
	if( owner )
	{
		AIUpdateInterface *ai = owner->getAI();
		if( ai )
		{
			//Make sure the owner has the same command button.
			const CommandSet *commandSet = TheControlBar->findCommandSet( owner->getCommandSetString() );
			if( commandSet )
			{
				for( int i = 0; i < MAX_COMMANDS_PER_SET; i++ )
				{
					const CommandButton *aCommandButton = commandSet->getCommandButton(i);
					if( commandButton == aCommandButton )
					{
						//We found the matching command button so now order the unit to do what the button wants.
						switch( commandButton->getCommandType() )
						{
							//ONLY NO TARGET VIA AI BUTTONS NEED BE IMPLEMENTED HERE!
							case GUI_COMMAND_STOP:
								ai->aiIdle( cmdSource );
								break;
							default:
								if( owner->getName().isNotEmpty() )
								{
									DEBUG_ASSERTCRASH( 0, ("AIUpdate::privateCommandButton() -- unit %s ('%s'), command %s not implemented.",
										owner->getTemplate()->getName().str(), owner->getName().str(), commandButton->getTextLabel().str() ) );
								}
								else
								{
									DEBUG_ASSERTCRASH( 0, ("AIUpdate::privateCommandButton() -- unit %s, command %s not implemented.",
										owner->getTemplate()->getName().str(), commandButton->getTextLabel().str() ) );
								}
						}
					}
				}
			}
		}
	}
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void AIUpdateInterface::privateCommandButtonPosition( const CommandButton *commandButton, const Coord3D *pos, CommandSourceType cmdSource )
{
	if( !commandButton )
	{
		return;
	}

	if (getObject()->isKindOf(KINDOF_PROJECTILE))
		return;

	//First of all, it's quite possible to get this far with an object incapable of performing such a task. Scripts will have
	//entire teams of multiple unit types and want to order units to do something... if they can, great.. if not, ignore.
	Object *owner = getObject();
	if( owner )
	{
		AIUpdateInterface *ai = owner->getAI();
		if( ai )
		{
			//Make sure the owner has the same command button.
			const CommandSet *commandSet = TheControlBar->findCommandSet( owner->getCommandSetString() );
			if( commandSet )
			{
				for( int i = 0; i < MAX_COMMANDS_PER_SET; i++ )
				{
					const CommandButton *aCommandButton = commandSet->getCommandButton(i);
					if( commandButton == aCommandButton )
					{
						//We found the matching command button so now order the unit to do what the button wants.
						switch( commandButton->getCommandType() )
						{
							//LOCATION BASED COMMANDS ONLY VIA AI
							case GUI_COMMAND_NONE:
							default:
								if( owner->getName().isNotEmpty() )
								{
									DEBUG_ASSERTCRASH( 0, ("AIUpdate::privateCommandButtonPosition() -- unit %s ('%s'), command %s not implemented.",
										owner->getTemplate()->getName().str(), owner->getName().str(), commandButton->getTextLabel().str() ) );
								}
								else
								{
									DEBUG_ASSERTCRASH( 0, ("AIUpdate::privateCommandButtonPosition() -- unit %s, command %s not implemented.",
										owner->getTemplate()->getName().str(), commandButton->getTextLabel().str() ) );
								}
								break;
						}
					}
				}
			}
		}
	}
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void AIUpdateInterface::privateCommandButtonObject( const CommandButton *commandButton, Object *obj, CommandSourceType cmdSource )
{
	if( !commandButton )
	{
		return;
	}

	if (getObject()->isKindOf(KINDOF_PROJECTILE))
		return;

	//First of all, it's quite possible to get this far with an object incapable of performing such a task. Scripts will have
	//entire teams of multiple unit types and want to order units to do something... if they can, great.. if not, ignore.
	Object *owner = getObject();
	if( owner )
	{
		AIUpdateInterface *ai = owner->getAI();
		//Make sure the owner has the same command button.
		const CommandSet *commandSet = TheControlBar->findCommandSet( owner->getCommandSetString() );
		if( commandSet )
		{
			for( int i = 0; i < MAX_COMMANDS_PER_SET; i++ )
			{
				const CommandButton *aCommandButton = commandSet->getCommandButton(i);
				if( commandButton == aCommandButton )
				{
					//We found the matching command button so now order the unit to do what the button wants.
					switch( commandButton->getCommandType() )
					{
						//OBJECT BASED COMMANDS ONLY VIA AI
						case GUI_COMMAND_COMBATDROP:
							if( ai )
							{
								ai->aiCombatDrop( obj, floatPosOf( obj ), cmdSource );	// P4: orders take float
							}
							break;
						default:
						{
							AsciiString myName = owner->getTemplate()->getName().str();
							AsciiString myNickname;
							AsciiString targetName = obj->getTemplate()->getName().str();
							AsciiString targetNickname;
							if( owner->getName().isNotEmpty() )
							{
								myNickname.format( "('%s')", owner->getName().str() );
							}
							if( obj->getName().isNotEmpty() )
							{
								targetNickname.format( "('%s')", obj->getName().str() );
							}

							DEBUG_ASSERTCRASH( 0, ("AIUpdate::privateCommandButtonPosition() -- unit %s %s, command %s at unit %s %s not implemented.",
								myName.str(), myNickname.str(), commandButton->getTextLabel().str(), targetName.str(), targetNickname.str() ) );
						}
					}
				}
			}
		}
	}
}

// ------------------------------------------------------------------------------------------------
AIGroup *AIUpdateInterface::getGroup(void)
{
	return getObject()->getGroup();
}


///////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////

// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void AIUpdateInterface::crc( Xfer *x )
{
	CRCGEN_LOG(("AIUpdateInterface::crc() begin - %8.8X\n", ((XferCRC *)x)->getCRC()));
	// extend base class
	UpdateModule::crc( x );

	xfer(x);

	CRCGEN_LOG(("AIUpdateInterface::crc() end - %8.8X\n", ((XferCRC *)x)->getCRC()));

}  // end crc

// ------------------------------------------------------------------------------------------------
/** Xfer method
	* Version Info:
	* 1: Initial version
	* 5: the production rally point and its flag
	* 6: the out-of-bounds xfer of m_guardTargetType is fixed
	* 11: m_isMoving, which the duplicated m_isSafePath used to stand in place of
	* 12: m_allowedToChase
	* 13: m_pathfindFoundNothing
	* 14: the salvage return position and its flag
	* 16: the tunnel trip's goal and its flag
	* 17: how the tunnel trip's last leg is walked */
// ------------------------------------------------------------------------------------------------
void AIUpdateInterface::xfer( Xfer *xfer )
{
  // version
  const XferVersion currentVersion = 17;
  XferVersion version = currentVersion;
  xfer->xferVersion( &version, currentVersion );
 
 // extend base class
  UpdateModule::xfer( xfer );
 
	xfer->xferUnsignedInt(&m_priorWaypointID);
	xfer->xferUnsignedInt(&m_currentWaypointID);
	xfer->xferSnapshot(m_stateMachine);
	xfer->xferBool(&m_isAiDead);
	xfer->xferBool(&m_isRecruitable);

	xfer->xferUnsignedInt(&m_nextEnemyScanTime);		
	xfer->xferObjectID(&m_currentVictimID);	
	xfer->xferReal(&m_desiredSpeed);
	xfer->xferUser(&m_lastCommandSource, sizeof(m_lastCommandSource));
	if (version < 6)
	{
		// the original wrote m_guardTargetType[0] and [1], then [1] and [2] - and [2] is one past the
		// end of a two-element array, landing on m_locationToGuard.  Read an old save exactly the way
		// it was written, so it still loads.
		xfer->xferUser(&m_guardTargetType[0], sizeof(m_guardTargetType));
		xfer->xferUser(&m_guardTargetType[1], sizeof(m_guardTargetType[1]));
		xfer->xferUser(&m_locationToGuard, sizeof(m_guardTargetType[1]));
	}
	else
	{
		xfer->xferUser(m_guardTargetType, sizeof(m_guardTargetType));
	}

	xfer->xferCoord3D(&m_locationToGuard);

	xfer->xferObjectID(&m_objectToGuard);

	AsciiString triggerName;
	if (m_areaToGuard) triggerName = m_areaToGuard->getTriggerName();
	xfer->xferAsciiString(&triggerName);
	if (xfer->getXferMode() == XFER_LOAD)
	{
		if (triggerName.isNotEmpty()) {
			m_areaToGuard = TheTerrainLogic->getTriggerAreaByName(triggerName);
		}
	} 

	AsciiString attackName;
	if (m_attackInfo) attackName = m_attackInfo->getName();
	xfer->xferAsciiString(&attackName);
	if (xfer->getXferMode() == XFER_LOAD)
	{
		if (attackName.isNotEmpty()) {
			m_attackInfo = TheScriptEngine->getAttackInfo(attackName);
		}
	}  

	xfer->xferInt(&m_waypointCount);
	if (m_waypointCount<0 || m_waypointCount>MAX_WAYPOINTS) {
		DEBUG_CRASH(("Invalid waypoint count %d, max = %d", m_waypointCount, MAX_WAYPOINTS));
		throw SC_INVALID_DATA;
	}
	Int i;
	for (i=0; i<m_waypointCount; i++) {
		xfer->xferCoord3D(&m_waypointQueue[i]);
	}
	xfer->xferInt(&m_waypointIndex);
	xfer->xferBool(&m_executingWaypointQueue);

	UnsignedInt id = INVALID_WAYPOINT_ID;
	if (m_completedWaypoint) {
		id = m_completedWaypoint->getID();
	}
	xfer->xferUnsignedInt(&id);
	if (xfer->getXferMode() == XFER_LOAD)
	{
		m_completedWaypoint = TheTerrainLogic->getWaypointByID(id);
	}

	xfer->xferBool(&m_waitingForPath);
	Bool gotPath = (m_path != NULL);
	xfer->xferBool(&gotPath);
	if (xfer->getXferMode() == XFER_LOAD)	{
		if (gotPath) {
			m_path = newInstance(Path);
		}
	}
	if (gotPath) {
		xfer->xferSnapshot(m_path);
	}
	xfer->xferObjectID(&m_requestedVictimID);
	xfer->xferCoord3D(&m_requestedDestination);
	xfer->xferCoord3D(&m_requestedDestination2);

	// Not needed - we will recompute paths on load.
	//xfer->xferUnsignedInt(&m_pathTimestamp);		
	
	xfer->xferObjectID(&m_ignoreObstacleID);
	xfer->xferReal(&m_pathExtraDistance);
	xfer->xferICoord2D(&m_pathfindGoalCell);
	xfer->xferICoord2D(&m_pathfindCurCell);

	// Not needed - jba.
	//Int					m_blockedFrames;						///< Number of frames we've been blocked.
	//Real				m_curMaxBlockedSpeed;				///< Max speed we can have and not run into blocking things.
	//Bool				m_isBlocked;
	//Bool				m_isBlockedAndStuck;				///< True if we are stuck & need to recompute path.
	//Bool				m_isInUpdate;
	//Bool				m_fixLocoInPostProcess;

	xfer->xferUnsignedInt(&m_ignoreCollisionsUntil);
	xfer->xferUnsignedInt(&m_queueForPathFrame);
	xfer->xferCoord3D(&m_finalPosition);
	xfer->xferBool(&m_doFinalPosition);
	xfer->xferBool(&m_isAttackPath);
	xfer->xferBool(&m_isFinalGoal);
	xfer->xferBool(&m_isApproachPath);
	xfer->xferBool(&m_isSafePath);
	xfer->xferBool(&m_movementComplete);
	if (version >= 11)
	{
		xfer->xferBool(&m_isMoving);
	}
	else
	{
		// Version 10 and earlier wrote m_isSafePath a second time in this slot, and m_isMoving
		// was never saved at all, so a loaded unit in an AIInternalMoveToState reported itself
		// as standing still.
		Bool safePathWrittenTwice = m_isSafePath;
		xfer->xferBool(&safePathWrittenTwice);
	}
	if (version >= 12)
	{
		// A unit saved while closing with something it picked out for itself keeps the permission
		// to close with it; before this it loaded as FALSE and turned back.
		xfer->xferBool(&m_allowedToChase);
	}
	xfer->xferBool(&m_upgradedLocomotors);
	xfer->xferBool(&m_canPathThroughUnits);
	xfer->xferBool(&m_randomlyOffsetMoodCheck);
	xfer->xferObjectID(&m_repulsor1);
	xfer->xferObjectID(&m_repulsor2);

	if (version < 3)
	{
		Int lastFrameMoved = 0;
		xfer->xferInt(&lastFrameMoved);
	}

	xfer->xferObjectID(&m_moveOutOfWay1);
	xfer->xferObjectID(&m_moveOutOfWay2);

	if (xfer->getXferMode() == XFER_LOAD && version < 4)
	{
		// Read in from .ini
		//LocomotorSet			m_locomotorSet;
		AsciiString setName;
		if (m_curLocomotorSet > LOCOMOTORSET_INVALID && m_curLocomotorSet < LOCOMOTORSET_COUNT) 
			setName = TheLocomotorSetNames[m_curLocomotorSet];

		xfer->xferAsciiString(&setName);

		if (setName.isNotEmpty()) 
			m_curLocomotorSet = (LocomotorSetType)INI::scanIndexList(setName.str(), TheLocomotorSetNames);

		m_fixLocoInPostProcess = TRUE;
	}
	else
	{
		if (xfer->getXferMode() == XFER_LOAD)
		{
			// our ctor choose a NORMAL set for us. it's simpler
			// to simply clear out whatever we have here and allow 
			// xferSelfAndCurLocoPtr() to continue to require a pristine,
			// empty set. (srj)
			m_locomotorSet.clear();
			m_curLocomotor = NULL;
		}
		m_locomotorSet.xferSelfAndCurLocoPtr(xfer, &m_curLocomotor);
		xfer->xferUser(&m_curLocomotorSet, sizeof(m_curLocomotorSet));
	}

	xfer->xferUser(&m_locomotorGoalType, sizeof(m_locomotorGoalType));
	xfer->xferCoord3D(&m_locomotorGoalData);

	for (i=0; i<MAX_TURRETS; i++) {
		if (m_turretAI[i]) {
			xfer->xferSnapshot(m_turretAI[i]);
		}
	}
	xfer->xferUser(&m_turretSyncFlag, sizeof(m_turretSyncFlag));
	xfer->xferUser(&m_attitude, sizeof(m_attitude));

	xfer->xferUnsignedInt(&m_nextMoodCheckTime);
	if (version == 1)	
	{
		// surrender + demoralize
#ifdef ALLOW_DEMORALIZE
		xfer->xferUnsignedInt(&m_demoralizedFramesLeft);
#else
		UnsignedInt demoralizedFramesLeft = 0;
		xfer->xferUnsignedInt(&demoralizedFramesLeft);
#endif
#ifdef ALLOW_SURRENDER
		xfer->xferUnsignedInt(&m_surrenderedFramesLeft);
		xfer->xferInt(&m_surrenderedPlayerIndex);
#else
		UnsignedInt surrenderedFramesLeft = 0;
		Int surrenderedPlayerIndex = 0;
		xfer->xferUnsignedInt(&surrenderedFramesLeft);
		xfer->xferInt(&surrenderedPlayerIndex);
#endif
	}
	else if (version == 2)
	{
#ifdef ALLOW_SURRENDER
		DEBUG_CRASH(("fix me ALLOW_SURRENDER"));	// should not happen
#endif
		// demoralize only
#ifdef ALLOW_DEMORALIZE
		xfer->xferUnsignedInt(&m_demoralizedFramesLeft);
#else
		UnsignedInt tmp0 = 0;
		xfer->xferUnsignedInt(&tmp0);
#endif
	}
	else
	{
		// else no surrender or demoralize
#ifdef ALLOW_SURRENDER
		DEBUG_CRASH(("fix me ALLOW_SURRENDER"));	// should not happen
#endif
#ifdef ALLOW_DEMORALIZE
		DEBUG_CRASH(("fix me ALLOW_DEMORALIZE"));	// should not happen
#endif
	}

	xfer->xferObjectID(&m_crateCreated);
	if (version < 3)
	{
		Int repulsorCountdown = 0;
		xfer->xferInt(&repulsorCountdown);
	}

	if (version >= 5)
	{
		xfer->xferCoord3D(&m_exitProductionRallyPoint);
		xfer->xferBool(&m_hasExitProductionRallyPoint);
	}

	if (version >= 7)
	{
		// the lane across the route.  An older save has none, and the ctor's 0.5 / not-valid means
		// every loaded unit re-seeds itself the first frame it drives, which is the right answer.
		xfer->xferReal(&m_laneFraction);
		xfer->xferBool(&m_laneFractionValid);
		xfer->xferUnsignedInt(&m_laneHoldFrame);
	}

	if (version >= 8)
	{
		// a lane handed down by the ordering group and not yet taken up.  Only a save made in the
		// few frames between the order and the path arriving carries one.
		xfer->xferReal(&m_pendingLane);
		xfer->xferBool(&m_hasPendingLane);
	}

	if (version >= 9)
	{
		/* The crowd model's lane, which is a distance and not a share, plus how long the unit has been held
			 up - a save made in the middle of a jam that came back with everybody patient again would
			 restart the jam from the beginning.  The band itself is not saved: it is derived from the
			 route and is rebuilt the first frame after the load. */
		xfer->xferReal(&m_crowdLat);
		xfer->xferBool(&m_crowdLatValid);
		xfer->xferReal(&m_pendingCrowdLat);
		xfer->xferBool(&m_hasPendingCrowdLat);
		xfer->xferUnsignedInt(&m_crowdHoldFrame);
		xfer->xferInt(&m_crowdQueued);
		xfer->xferInt(&m_crowdSide);
	}

	if (version >= 10)
	{
		// the slot across the group, which outlives the band and cannot be worked out again from
		// anything the unit can see on its own
		xfer->xferInt(&m_crowdLaneIdx);
		xfer->xferInt(&m_crowdLaneOf);
		xfer->xferReal(&m_crowdLaneSpace);
	}

	if (version >= 13)
	{
		// lives from one path search to the move state's next look at it, which a save can fall between
		xfer->xferBool(&m_pathfindFoundNothing);
	}

	if (version >= 14)
	{
		// a save made while the unit is on its way to a salvage crate owes it the walk back
		xfer->xferCoord3D(&m_salvageReturnPosition);
		xfer->xferBool(&m_hasSalvageReturnPosition);
	}

	if (version >= 15)
	{
		// a group's planned lane, which a save can fall between the order and the path it is for
		UnsignedShort corners = (UnsignedShort)m_crowdPlanned.size();
		xfer->xferUnsignedShort(&corners);
		if (xfer->getXferMode() == XFER_LOAD)
			m_crowdPlanned.resize( corners );
		for (Int k = 0; k < (Int)corners; k++)
		{
			xfer->xferCoord3D(&m_crowdPlanned[ k ].pos);
			Int layer = (Int)m_crowdPlanned[ k ].layer;
			xfer->xferInt(&layer);
			m_crowdPlanned[ k ].layer = (PathfindLayerEnum)layer;
		}
	}

	if (version >= 16)
	{
		// a save made between a tunnel's enter and the walk out of the far mouth owes the rest of the trip
		xfer->xferCoord3D(&m_tunnelTripGoal);
		xfer->xferBool(&m_hasTunnelTrip);
	}

	if (version >= 17)
	{
		// a computer's wave comes out fighting, a move order does not
		Int end = (Int)m_tunnelTripEnd;
		xfer->xferInt(&end);
		m_tunnelTripEnd = (TunnelTripEnd)end;
	}

}  // end xfer

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void AIUpdateInterface::loadPostProcess( void )
{
	UpdateModule::loadPostProcess();

	if (m_fixLocoInPostProcess && m_curLocomotorSet!=LOCOMOTORSET_INVALID) 
	{
		m_fixLocoInPostProcess = FALSE;

		LocomotorSetType lst = m_curLocomotorSet;
		// Set the current to invalid, because chooseLocomotorSet aborts if it is already set to the desired value.
		m_curLocomotorSet = LOCOMOTORSET_INVALID;
		chooseLocomotorSet(lst);
	}

	if (!isMoving()) {
		m_pathfindGoalCell.x = -1;
		m_pathfindGoalCell.y = -1;
		const Coord3D myPos = floatPosOf(getObject());	// P5
		TheAI->pathfinder()->updateGoal(getObject(), &myPos, getObject()->getLayer());
		m_pathfindCurCell.x = -1;
		m_pathfindCurCell.y = -1;
		TheAI->pathfinder()->updatePos(getObject(), &myPos);
	}	else {
		if (m_pathfindGoalCell.x >= 0 && m_pathfindGoalCell.y >= 0) {
			Coord3D goalPos;
			goalPos.x = m_pathfindGoalCell.x * PATHFIND_CELL_SIZE_F + PATHFIND_CELL_SIZE_F*0.5f;
			goalPos.y = m_pathfindGoalCell.y * PATHFIND_CELL_SIZE_F + PATHFIND_CELL_SIZE_F*0.5f;
			m_pathfindGoalCell.x = -1;
			m_pathfindGoalCell.y = -1;
			TheAI->pathfinder()->updateGoal(getObject(), &goalPos, getObject()->getLayer());
		}
	}

}  // end loadPostProcess

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
Int AIUpdateInterface::friend_getWaypointGoalPathSize() const 
{ 
			//
			// it is VERY IMPORTANT to check for the current state type as being follow-path, 
			// because "getGoalPath" and friends are used for other things (eg, jet takeoff and landing).
			// if you don't do this check, you will end up with really bizarre behavior in obscure jet-related
			// cases, and our users will all laugh at us.
			//
			// the goalpath should really be completely private, but at this point, this ugly scheme
			// has to be lived with. (srj)
			//
	if (getAIStateType() != AI_FOLLOW_PATH)
		return 0;

	return getStateMachine()->getGoalPathSize(); 
}

// ------------------------------------------------------------------------------------------------
Bool AIUpdateInterface::hasLocomotorForSurface(LocomotorSurfaceType surfaceType)
{
	LocomotorSurfaceTypeMask surfaceMask = (LocomotorSurfaceTypeMask)surfaceType;
	if (m_locomotorSet.findLocomotor(surfaceMask))
		return TRUE;
	else
		return FALSE;
}

// ------------------------------------------------------------------------------------------------
