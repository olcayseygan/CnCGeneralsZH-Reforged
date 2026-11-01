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

// AIUpdate.h //
// AI interface
// Author: Michael S. Booth, 2001-2002

#pragma once

#ifndef _AI_UPDATE_H_
#define _AI_UPDATE_H_

#include "GameLogic/Module/UpdateModule.h"
#include "GameLogic/AI.h"
#include "GameLogic/AIStateMachine.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/LocomotorSet.h"
#include "GameLogic/CrowdModel.h"

class AIGroup;
class AIStateMachine;
class AttackPriorityInfo;
class CrowdCorridor;
class DamageInfo;
class DozerAIInterface;
class Object;
class Path; 
class PathfindServicesInterface;
class PathNode;
class PhysicsBehavior;
#ifdef ALLOW_SURRENDER
class POWTruckAIUpdateInterface;
#endif
class SupplyTruckAIInterface;
class TurretAI;
class TurretAIData;
class Waypoint;
class WorkerAIInterface;
class HackInternetAIInterface;
class AssaultTransportAIInterface;
class JetAIUpdate;

enum AIStateType : Int;
enum HordeActionType : Int;
enum ObjectID : Int;


//-------------------------------------------------------------------------------------------------
const Real FAST_AS_POSSIBLE = 999999.0f;

//-------------------------------------------------------------------------------------------------
//
// Note: these values are saved in save files, so you MUST NOT REMOVE OR CHANGE
// existing values!
//
enum LocomotorSetType : Int
{
	LOCOMOTORSET_INVALID = -1,

	LOCOMOTORSET_NORMAL = 0,
	LOCOMOTORSET_NORMAL_UPGRADED,
	LOCOMOTORSET_FREEFALL,
	LOCOMOTORSET_WANDER,
	LOCOMOTORSET_PANIC,
	LOCOMOTORSET_TAXIING,			// set used for normally-airborne items while taxiing on ground
	LOCOMOTORSET_SUPERSONIC,	// set used for high-speed attacks
	LOCOMOTORSET_SLUGGISH,		// set used for abnormally slow (but not damaged) speeds

	LOCOMOTORSET_COUNT	///< keep last, please
};

//-------------------------------------------------------------------------------------------------
enum GuardTargetType
{ 
	GUARDTARGET_LOCATION,	// Guard a coord3d
	GUARDTARGET_OBJECT,		// Guard an object
	GUARDTARGET_AREA,			// Guard a Polygon trigger
	GUARDTARGET_NONE				// Currently not guarding
};

//-------------------------------------------------------------------------------------------------
// How a unit that went through the tunnel network walks from the far mouth to where it was sent.
// Written out in save/load xfer, don't change these numbers.
enum TunnelTripEnd
{
	TUNNEL_TRIP_MOVE = 0,					// a move order: walk there
	TUNNEL_TRIP_ATTACK_MOVE = 1		// a computer's attack: fight whatever is on the way
};

#ifdef DEFINE_LOCOMOTORSET_NAMES
static const char *TheLocomotorSetNames[] = 
{
	"SET_NORMAL",
	"SET_NORMAL_UPGRADED",
	"SET_FREEFALL",
	"SET_WANDER",
	"SET_PANIC",
	"SET_TAXIING",
	"SET_SUPERSONIC",
	"SET_SLUGGISH",

	NULL
};
#endif 

enum AutoAcquireStates
{
	AAS_Idle											= 0x01,
	AAS_Idle_Stealthed						= 0x02,
	AAS_Idle_No										= 0x04,
	AAS_Idle_Not_While_Attacking	= 0x08,
	AAS_Idle_Attack_Buildings			= 0x10,
};

#ifdef DEFINE_AUTOACQUIRE_NAMES
static const char *TheAutoAcquireEnemiesNames[] = 
{
	"YES",
	"STEALTHED",
	"NO",
	"NOTWHILEATTACKING",
	"ATTACK_BUILDINGS",

	NULL
};
#endif


//-------------------------------------------------------------------------------------------------
enum MoodMatrixParameters
{
	// Controller_Player and Controller_AI are mutually exclusive
	MM_Controller_Player		= 0x00000001,
	MM_Controller_AI				= 0x00000002,
	MM_Controller_Bitmask		= (MM_Controller_Player | MM_Controller_AI),
	
	// UnitTypes are mutually exclusive, ie: you cannot be turreted and air at the same time (we do
	// not make such a distinction, so air takes precedence over turretedness)
	MM_UnitType_NonTurreted = 0x00000010,
	MM_UnitType_Turreted		= 0x00000020,
	MM_UnitType_Air					= 0x00000040,
	MM_UnitType_Bitmask			= (MM_UnitType_NonTurreted | MM_UnitType_Turreted | MM_UnitType_Air),
	
	// A unit can be in only one mood at a time.
	MM_Mood_Sleep						= 0x00000100,
	MM_Mood_Passive					= 0x00000200,
	MM_Mood_Normal					= 0x00000400,
	MM_Mood_Alert						= 0x00000800,
	MM_Mood_Aggressive			= 0x00001000,
	MM_Mood_Bitmask					= (MM_Mood_Sleep | MM_Mood_Passive | MM_Mood_Normal | MM_Mood_Alert | MM_Mood_Aggressive) 
};

//-------------------------------------------------------------------------------------------------
enum MoodMatrixAction
{
	MM_Action_Idle,
	MM_Action_Move,
	MM_Action_Attack,
	MM_Action_AttackMove
};

//-------------------------------------------------------------------------------------------------
enum MoodActionAdjustment
{
	MAA_Action_Ok										= 0x00000001,
	MAA_Action_To_Idle							= 0x00000002,
	MAA_Action_To_AttackMove				= 0x00000004,
	MAA_Action_To_Bitmask						= (MAA_Action_Ok | MAA_Action_To_Idle | MAA_Action_To_AttackMove),

	MAA_Affect_Range_IgnoreAll			= 0x00000010,
	MAA_Affect_Range_WaitForAttack	= 0x00000020,
	// Normal doesn't affect ranges.
	MAA_Affect_Range_Alert					= 0x00000040,
	MAA_Affect_Range_Aggressive			= 0x00000080,
	MAA_Affect_Range_Bitmask				= (MAA_Affect_Range_IgnoreAll | MAA_Affect_Range_WaitForAttack | MAA_Affect_Range_Alert | MAA_Affect_Range_Aggressive)
};

//-------------------------------------------------------------------------------------------------
typedef std::vector< const LocomotorTemplate* > LocomotorTemplateVector;
typedef std::map< LocomotorSetType, LocomotorTemplateVector, std::less<LocomotorSetType> > LocomotorTemplateMap;

//-------------------------------------------------------------------------------------------------
class AIUpdateModuleData : public UpdateModuleData
{
public:
	LocomotorTemplateMap	m_locomotorTemplates;					///< locomotors for object
	const TurretAIData*		m_turretData[MAX_TURRETS];
	UnsignedInt						m_moodAttackCheckRate;				///< how frequently we should recheck for enemies due to moods, when idle
  Bool        m_forbidPlayerCommands;     ///< Should isAllowedToRespondToAiCommands() filter out commands from the player, thus making it ai-controllable only?
  Bool        m_turretsLinked;						///< Turrets are linked together and attack together.
	UnsignedInt						m_autoAcquireEnemiesWhenIdle;
#ifdef ALLOW_SURRENDER
 	UnsignedInt						m_surrenderDuration;					///< when we surrender, how long we stay surrendered.
#endif

	
  AIUpdateModuleData();
	virtual ~AIUpdateModuleData();

	virtual Bool isAiModuleData() const { return true; }

	const LocomotorTemplateVector* findLocomotorTemplateVector(LocomotorSetType t) const;
	static void buildFieldParse(MultiIniFieldParse& p);
	static void parseLocomotorSet( INI* ini, void *instance, void *store, const void* /*userData*/ );

private:
	static void parseTurret( INI* ini, void *instance, void *store, const void* /*userData*/ );


};

//-------------------------------------------------------------------------------------------------
enum AIFreeToExitType // Note - written out in save/load xfer, don't change these numbers.  jba.
{
	FREE_TO_EXIT=0,
	NOT_FREE_TO_EXIT=1,
	WAIT_TO_EXIT=2
};

//-------------------------------------------------------------------------------------------------
/** 
 * The AIUpdateInterface module contains the interface to the AI system,
 * and performs the actual AI behaviors.
 */
class AIUpdateInterface : public UpdateModule, public AICommandInterface
{

	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE( AIUpdateInterface, "AIUpdateInterface" )
	MAKE_STANDARD_MODULE_MACRO_WITH_MODULE_DATA( AIUpdateInterface, AIUpdateModuleData )

protected:

	// yes, protected, NOT public.
	virtual void privateMoveToPosition( const Coord3D *pos, CommandSourceType cmdSource );			///< move to given position(s) tightening the formation.
	virtual void privateMoveToObject( Object *obj, CommandSourceType cmdSource );			///< move to given object
	virtual void privateMoveToAndEvacuate( const Coord3D *pos, CommandSourceType cmdSource );			///< move to given position(s)
	virtual void privateMoveToAndEvacuateAndExit( const Coord3D *pos, CommandSourceType cmdSource );			///< move to given position & unload transport.
	virtual void privateIdle(CommandSourceType cmdSource);						///< Enter idle state.	
	virtual void privateTightenToPosition( const Coord3D *pos, CommandSourceType cmdSource );			///< move to given position(s) tightening the formation.
	virtual void privateFollowWaypointPath( const Waypoint *way, CommandSourceType cmdSource );///< start following the path from the given point
	virtual void privateFollowWaypointPathAsTeam( const Waypoint *way, CommandSourceType cmdSource );///< start following the path from the given point
	virtual void privateFollowWaypointPathExact( const Waypoint *way, CommandSourceType cmdSource );///< start following the path from the given point
	virtual void privateFollowWaypointPathAsTeamExact( const Waypoint *way, CommandSourceType cmdSource );///< start following the path from the given point
	virtual void privateFollowPath( std::vector<Coord3D>* path, Object *ignoreObject, CommandSourceType cmdSource, Bool exitProduction );///< follow the path defined by the given array of points
	virtual void privateFollowPathAppend( const Coord3D *pos, CommandSourceType cmdSource );
	virtual void privateAttackObject( Object *victim, Int maxShotsToFire, CommandSourceType cmdSource );					///< attack given object
	virtual void privateForceAttackObject( Object *victim, Int maxShotsToFire, CommandSourceType cmdSource );					///< attack given object
	virtual void privateGuardRetaliate( Object *victim, const Coord3D *pos, Int maxShotsToFire, CommandSourceType cmdSource );				///< retaliate and attack attacker -- but with guard restrictions
	virtual void privateAttackTeam( const Team *team, Int maxShotsToFire, CommandSourceType cmdSource );							///< attack the given team
	virtual void privateAttackPosition( const Coord3D *pos, Int maxShotsToFire, CommandSourceType cmdSource );						///< attack given spot
	virtual void privateAttackMoveToPosition( const Coord3D *pos, Int maxShotsToFire, CommandSourceType cmdSource );			///< attack move to the given location
	virtual void privateAttackFollowWaypointPath( const Waypoint *way, Int maxShotsToFire, Bool asTeam, CommandSourceType cmdSource );			///< attack move along the following waypoint path, potentially as a team
	virtual void privateHunt( CommandSourceType cmdSource );														///< begin "seek and destroy"
	virtual void privateRepair( Object *obj, CommandSourceType cmdSource );						///< repair the given object
#ifdef ALLOW_SURRENDER
	virtual void privatePickUpPrisoner( Object *prisoner, CommandSourceType cmdSource );			///< pick up prisoner
	virtual void privateReturnPrisoners( Object *prison, CommandSourceType cmdSource );			///< return picked up prisoners to the 'prison'
#endif
	virtual void privateResumeConstruction( Object *obj, CommandSourceType cmdSource );	///< resume construction of object
	virtual void privateGetHealed( Object *healDepot, CommandSourceType cmdSource );		///< get healed at heal depot
	virtual void privateGetRepaired( Object *repairDepot, CommandSourceType cmdSource );///< get repaired at repair depot
	virtual void privateEnter( Object *obj, CommandSourceType cmdSource );							///< enter the given object
	virtual void privateDock( Object *obj, CommandSourceType cmdSource );							///< get near given object and wait for enter clearance
	virtual void privateExit( Object *objectToExit, CommandSourceType cmdSource );			///< get out of this Object
	virtual void privateExitInstantly( Object *objectToExit, CommandSourceType cmdSource );			///< get out of this Object this frame
	virtual void privateEvacuate( Int exposeStealthUnits, CommandSourceType cmdSource );												///< empty its contents
	virtual void privateEvacuateInstantly( Int exposeStealthUnits, CommandSourceType cmdSource );												///< empty its contents this frame
	virtual void privateExecuteRailedTransport( CommandSourceType cmdSource );					///< execute next leg in railed transport sequence
	virtual void privateGoProne( const DamageInfo *damageInfo, CommandSourceType cmdSource );												///< life altering state change, if this AI can do it
	virtual void privateGuardTunnelNetwork( GuardMode guardMode, CommandSourceType cmdSource );			///< guard the given spot
	virtual void privateGuardPosition( const Coord3D *pos, GuardMode guardMode, CommandSourceType cmdSource );			///< guard the given spot
	virtual void privateGuardObject( Object *objectToGuard, GuardMode guardMode, CommandSourceType cmdSource );		///< guard the given object
	virtual void privateGuardArea( const PolygonTrigger *areaToGuard, GuardMode guardMode, CommandSourceType cmdSource );	///< guard the given area
	virtual void privateAttackArea( const PolygonTrigger *areaToGuard, CommandSourceType cmdSource );	///< guard the given area
	virtual void privateHackInternet( CommandSourceType cmdSource );	///< Hack money from the heavens (free money)
	virtual void privateFaceObject( Object *target, CommandSourceType cmdSource );
	virtual void privateFacePosition( const Coord3D *pos, CommandSourceType cmdSource );
	virtual void privateRappelInto( Object *target, const Coord3D& pos, CommandSourceType cmdSource );
	virtual void privateCombatDrop( Object *target, const Coord3D& pos, CommandSourceType cmdSource );
	virtual void privateCommandButton( const CommandButton *commandButton, CommandSourceType cmdSource );
	virtual void privateCommandButtonPosition( const CommandButton *commandButton, const Coord3D *pos, CommandSourceType cmdSource );
	virtual void privateCommandButtonObject( const CommandButton *commandButton, Object *obj, CommandSourceType cmdSource );
	virtual void privateWander( const Waypoint *way, CommandSourceType cmdSource );	///< Wander around the waypoint path.
	virtual void privateWanderInPlace( CommandSourceType cmdSource );	///< Wander around the current position.
	virtual void privatePanic( const Waypoint *way, CommandSourceType cmdSource );	///< Run screaming down the waypoint path.
	virtual void privateBusy( CommandSourceType cmdSource );	///< Transition to the busy state
	virtual void privateMoveAwayFromUnit( Object *unit, CommandSourceType cmdSource );	///< Move out of the way of a unit.


public:
	AIUpdateInterface( Thing *thing, const ModuleData* moduleData );
	// virtual destructor prototype provided by memory pool declaration

	virtual AIUpdateInterface* getAIUpdateInterface() { return this; }

	// Disabled conditions to process (AI will still process held status)
	virtual DisabledMaskType getDisabledTypesToProcess() const { return MAKE_DISABLED_MASK( DISABLED_HELD ); }
	
	// Some very specific, complex behaviors are used by more than one AIUpdate.  Here are their interfaces.
	virtual DozerAIInterface* getDozerAIInterface() {return NULL;}
	virtual SupplyTruckAIInterface* getSupplyTruckAIInterface() {return NULL;}
	virtual const DozerAIInterface* getDozerAIInterface() const {return NULL;}
	virtual const SupplyTruckAIInterface* getSupplyTruckAIInterface() const {return NULL;}
#ifdef ALLOW_SURRENDER
	virtual POWTruckAIUpdateInterface *getPOWTruckAIUpdateInterface( void ) { return NULL; }
#endif
	virtual WorkerAIInterface* getWorkerAIInterface( void ) { return NULL; }
	virtual const WorkerAIInterface* getWorkerAIInterface( void ) const { return NULL; }
	virtual HackInternetAIInterface* getHackInternetAIInterface() { return NULL; }
	virtual const HackInternetAIInterface* getHackInternetAIInterface() const { return NULL; }
	virtual AssaultTransportAIInterface* getAssaultTransportAIInterface() { return NULL; }
	virtual const AssaultTransportAIInterface* getAssaultTransportAIInterface() const { return NULL; }
	virtual JetAIUpdate* getJetAIUpdate() { return NULL; }
	virtual const JetAIUpdate* getJetAIUpdate() const { return NULL; }

#ifdef ALLOW_SURRENDER
	void setSurrendered( const Object *objWeSurrenderedTo, Bool surrendered );
	inline Bool isSurrendered( void ) const { return m_surrenderedFramesLeft > 0; }
	inline Int getSurrenderedPlayerIndex() const { return m_surrenderedPlayerIndex; }
#endif

	virtual void joinTeam( void );			///< This unit just got added to a team & needs to catch up.
	
	Bool areTurretsLinked() const { return getAIUpdateModuleData()->m_turretsLinked; }

	// this is present solely for some transports to override, so that they can land before 
	// allowing people to exit...
	virtual AIFreeToExitType getAiFreeToExit(const Object* exiter) const { return FREE_TO_EXIT; }
	
	// this is present solely to allow some special-case things to override, like landed choppers.
	virtual Bool isAllowedToAdjustDestination() const { return true; }
	virtual Bool isAllowedToMoveAwayFromUnit() const { return true; }
	// this is used for Chinooks, so the pathfinder doesn't make 'em avoid 'em... for combat drops!
	virtual ObjectID getBuildingToNotPathAround() const { return INVALID_ID; }

	// AI Interface implementation -----------------------------------------------------------------------
	virtual Bool isIdle() const;
	virtual Bool isAttacking() const;
	virtual Bool isClearingMines() const;
	virtual Bool isTaxiingToParking() const { return FALSE; } //only applies to jets interacting with runways.

	//Definition of busy -- when explicitly in the busy state. Moving or attacking is not considered busy!
	virtual Bool isBusy() const;

	virtual void onObjectCreated();
	virtual void doQuickExit( std::vector<Coord3D>* path );			///< get out of this Object
	
	virtual void aiDoCommand(const AICommandParms* parms);
	
	virtual const Coord3D *getGuardLocation( void ) const { return &m_locationToGuard;	}
	virtual const ObjectID getGuardObject( void ) const { return m_objectToGuard; }
	virtual const PolygonTrigger *getAreaToGuard( void ) const { return m_areaToGuard; }
	virtual GuardTargetType getGuardTargetType() const { return m_guardTargetType[1]; }
	virtual void clearGuardTargetType() { m_guardTargetType[1] = m_guardTargetType[0]; m_guardTargetType[0] = GUARDTARGET_NONE; }
	virtual GuardMode getGuardMode() const { return m_guardMode; }
	// the radius a player's guard order gave this unit; 0 leaves it on its own vision-based range
	Real getGuardRadius() const { return m_guardRadius; }
	void setGuardRadius( Real radius ) { m_guardRadius = radius; }
	// A player's unit on the aggressive stance takes what it sees, not only what it can already
	// shoot, and goes after it the way a computer player's unit does.  Defensive is EA's rule
	Bool hasAggressiveStance() const { return m_aggressiveStance; }
	void setAggressiveStance( Bool aggressive ) { m_aggressiveStance = aggressive; }

	virtual Object* construct( const ThingTemplate *what, 
														 const Coord3D *pos, Real angle, 
														 Player *owningPlayer,
														 Bool isRebuild ) { return NULL; }///< construct a building


	void ignoreObstacle( const Object *obj );			///< tell the pathfinder to ignore the given object as an obstacle
	void ignoreObstacleID( ObjectID id );			///< tell the pathfinder to ignore the given object as an obstacle
	
	AIStateType getAIStateType() const;							///< What general state is the AIState Machine in?

	AsciiString getCurrentStateName(void) const { return m_stateMachine->getCurrentStateName(); }

	virtual Object* getEnterTarget();					///< if we are trying to enter something (see enter() above), what is the thing in question? (or null)

	/**
		This exists solely to be overridden by children... the idea is that when a unit is targeted by someone else, it
		asks for an offset to add to its targeting position... if this returns true, the offset should be added to the
		true target pos. This is primarily for the Aurora Bomber, which uses this technique to make itself "unhittable"
		when in Supersonic Attack Mode -- things still target it, they just aim "behind" it!
	*/
	virtual Bool getSneakyTargetingOffset(Coord3D* offset) const { return false; }

	/**
		Exists solely to be overridden by JetAIUpdate...
	*/
	virtual void addTargeter(ObjectID id, Bool add) { return; }
	virtual Bool isTemporarilyPreventingAimSuccess() const { return false; }

	 
	void setPriorWaypointID( UnsignedInt id )   { m_priorWaypointID = id; };
	void setCurrentWaypointID( UnsignedInt id ) { m_currentWaypointID = id; };

	// Group ----------------------------------------------------------------------------------------------
	// these three methods allow a group leader's path to be communicated to the other group members

	AIGroup *getGroup(void);

	// it's VERY RARE you want to call this function; you should normally use Object::isEffectivelyDead()
	// instead. the exception would be for things that need to know whether to call markIsDead or not.
	Bool isAiInDeadState( void ) const { return m_isAiDead; }				///< return true if we are dead
	void markAsDead( void );

	Bool isRecruitable(void) const {return m_isRecruitable;}
	void setIsRecruitable(Bool isRecruitable) {m_isRecruitable = isRecruitable;}

	Real getDesiredSpeed() const { return m_desiredSpeed; }
	void setDesiredSpeed( Real speed ) { m_desiredSpeed = speed; }	///< how fast we want to go

	// these are virtual because subclasses might need to override them. (srj)
	virtual void setLocomotorGoalPositionOnPath();
	virtual void setLocomotorGoalPositionExplicit(const Coord3D& newPos);
	virtual void setLocomotorGoalOrientation(Real angle);
	virtual void setLocomotorGoalNone();
	virtual Bool isDoingGroundMovement(void) const;  ///< True if moving along ground. 

	Bool isValidLocomotorPosition(const Coord3D* pos) const;
	Bool isAircraftThatAdjustsDestination(void) const;  ///< True if is aircraft that doesn't stack destinations (missles for example do stack destinations.) 
	Real getCurLocomotorSpeed() const;
	Real getLocomotorDistanceToGoal();
	const Locomotor *getCurLocomotor() const {return m_curLocomotor;}
	Locomotor *getCurLocomotor() { return m_curLocomotor; }
	LocomotorSetType getCurLocomotorSetType() const { return m_curLocomotorSet; }
	Bool hasLocomotorForSurface(LocomotorSurfaceType surfaceType);

	// turret stuff.
	WhichTurretType getWhichTurretForWeaponSlot(WeaponSlotType wslot, Real* turretAngle, Real* turretPitch = NULL) const;
	WhichTurretType getWhichTurretForCurWeapon() const;
	/**
		return true iff the weapon is on a turret, that turret is trying to aim at the victim, 
		BUT is not yet pointing in the right dir.
	*/
	Bool isWeaponSlotOnTurretAndAimingAtTarget(WeaponSlotType wslot, const Object* victim) const;
	Bool getTurretRotAndPitch(WhichTurretType tur, Real* turretAngle, Real* turretPitch) const;
	Real getTurretTurnRate(WhichTurretType tur) const;
	Real getTurretArcShortfall(WhichTurretType tur, Real relAngle) const;
	WhichTurretType getAimingTurret(Bool *noseAims) const;	///< the turret the current attack aims with, and whether the nose has to
	void setTurretTargetObject(WhichTurretType tur, Object* o, Bool isForceAttacking = FALSE);
	Object *getTurretTargetObject( WhichTurretType tur, Bool clearDeadTargets = TRUE );
	void setTurretTargetPosition(WhichTurretType tur, const Coord3D* pos);
	void setTurretEnabled(WhichTurretType tur, Bool enabled);
	void recenterTurret(WhichTurretType tur);
	void stopTurretsTurning();	///< every turret stops its move loop and rotate look
	Bool isTurretEnabled( WhichTurretType tur ) const;
	Bool isTurretInNaturalPosition(WhichTurretType tur) const;

	// "Planning Mode" -----------------------------------------------------------------------------------
	Bool queueWaypoint( const Coord3D *pos );				///< add waypoint to end of move list. return true if success, false if queue was full and those the waypoint not added
	void clearWaypointQueue( void );								///< reset the waypoint queue to empty
	void executeWaypointQueue( void );							///< start moving along queued waypoints

	// Pathfinding ---------------------------------------------------------------------------------------
private:
	Bool computePath( PathfindServicesInterface *pathfinder, Coord3D *destination );	///< computes path to destination, returns false if no path
	Bool computeAttackPath(PathfindServicesInterface *pathfinder,  const Object *victim, const Coord3D* victimPos );	///< computes path to attack the current target, returns false if no path
#ifdef ALLOW_SURRENDER
	void doSurrenderUpdateStuff();
#endif

public:
	void doPathfind( PathfindServicesInterface *pathfinder ); 
	void requestPath( Coord3D *destination, Bool isGoalDestination );	///< Queues a request to pathfind to destination.
	void requestAttackPath( ObjectID victimID, const Coord3D* victimPos );	///< computes path to attack the current target, returns false if no path
	void requestApproachPath( Coord3D *destination );	///< computes path to attack the current target, returns false if no path
	void requestSafePath( ObjectID repulsor1 );	///< computes path to attack the current target, returns false if no path

	Bool isWaitingForPath(void) const {return m_waitingForPath;}
	Bool isAttackPath(void) const {return m_isAttackPath;} ///< True if we have a path to an attack location.
	void cancelPath(void); ///< Called if we no longer need the path. 
	Path* getPath( void ) { return m_path; }				///< return the agent's current path
	const Path* getPath( void ) const { return m_path; }				///< return the agent's current path
	void destroyPath( void );												///< destroy the current path, setting it to NULL
	UnsignedInt getPathAge( void ) const { return TheGameLogic->getFrame() - m_pathTimestamp; }	///< return the "age" of the path
	Bool isPathAvailable( const Coord3D *destination ) const; ///< does a path exist between us and the destination
	Bool isQuickPathAvailable( const Coord3D *destination ) const;  ///< does a path (using quick pathfind) exist between us and the destination
	Int getNumFramesBlocked(void) const {return m_blockedFrames;}
	Bool isBlockedAndStuck(void) const {return m_isBlockedAndStuck;}
	/// consecutive frames this unit has wanted to move without moving; see updateProgress
	Int getNoProgressFrames(void) const {return m_noProgress;}

	/* Lanes.  The route says where to go; the lane fraction says where across its width this unit
		 rides while it goes there - 0 hard right, 1 hard left, 0.5 the centre line, which is where
		 retail always drove.  It is seeded from where the unit actually was when the route was
		 built, so a spread-out group starts spread out, and it is the only per-unit state the band
		 needs: the width itself is measured off the map where the unit is, never stored. */
	Bool hasLaneFraction(void) const {return m_laneFractionValid;}
	Real getLaneFraction(void) const {return m_laneFraction;}

	/** Told by the group that issued the order where this member sat across the group, as a
			fraction of the group's own width.  It has to come from outside: every member is handed
			its own route starting under its own tracks, so measured against that route each of them
			is dead centre and none of them has any reason to prefer a side.  The offset from the
			group's centre is the only shape there is, and it is the shape the player drew a box
			round. Held until the path actually arrives, which can be several frames later. */
	void setPendingLane( Real u ) { m_pendingLane = u; m_hasPendingLane = TRUE; }

	/** The crowd model's version of the same thing, and the reason it is a second field rather than
			a rescaling of the one above: this one is a distance in world units, positive to the left of
			the route, and it means the same thing wherever the unit is standing. A fraction of the
			local width does not - it drags a unit into every wide bay its route happens to pass and
			swings it round the outside of corners. */
	void setPendingCrowdLat( Real lat ) { m_pendingCrowdLat = lat; m_hasPendingCrowdLat = TRUE; }

	/** Where this member sat across the group, as a place in a queue rather than a distance: `idx`
			out of `of`, sorted left to right, laid out `spacing` apart.

			The distance above is measured once, at the group's feet, and is therefore the width of the
			ground the group is standing on - which is not the width of the road it is about to drive
			down. A group leaving a base through a gate was handed one lane and stayed one lane wide for
			the rest of the trip, because nothing ever asked the question again. The slot is asked every
			frame against the band at the unit's own sample, so the group is as wide as the ground under
			it right now: single file in the gate, six abreast on the far side, and back to single file
			at the next one. */
	void setPendingCrowdLane( Int idx, Int of, Real spacing )
		{ m_crowdLaneIdx = idx; m_crowdLaneOf = of; m_crowdLaneSpace = spacing; }

	/** Out of the march. The slot deliberately outlives a route - a group that repaths halfway
			across a map keeps its formation - so an order that hands out no lanes has to say so, or a
			unit sent off on its own would still be riding the lane it was given as part of a group
			that has since dispersed. */
	void clearCrowdLane( void )
		{ m_crowdLaneIdx = 0; m_crowdLaneOf = 0; m_crowdLaneSpace = 0.0f; m_hasPendingCrowdLat = FALSE; m_crowdPlanned.clear(); }

	/** The route this member's next path is to be, planned by its group rather than searched for.

			Each member's own search finds the shortest way from where it stands, and the shortest way
			round the end of a wall is past the end of the wall, so every member of a group comes round
			a corner over the same point and the group turns it in single file. The group plans one
			route instead and hands each member that route moved sideways by its lane, corners and all,
			so the outside lane takes the outside of the turn. It is the member's path, not a hint
			beside one: the path that is followed, the path progress is measured against and the path
			the band is built on are the same thing. It is used once, for the path the order asks for;
			any later repath is the pathfinder's. A member whose planned lane could not be laid clear of
			the ground has none, and searches for its own. */
	void setPlannedCrowdRoute( const CrowdRoute& route ) { m_crowdPlanned = route; }
	Real getCrowdLat( void ) const { return m_crowdLat; }
	const CrowdCorridor *getCrowdCorridor( void ) const { return m_corridor; }
	/** Which sample of its own band the unit was beside last frame.  Priority between two units is
			decided from both of their bends, and a unit that worked one of them out for itself and the
			other one by guesswork would rank the pair differently from how they rank themselves. */
	Int getCrowdSample( void ) const { return m_crowdSample; }
	Bool canComputeQuickPath(void); ///< Returns true if we can quickly comput a path.  Usually missiles & the like that just move straight to the destination.
	Bool computeQuickPath(const Coord3D *destination); ///< Computes a quick path to the destination.

	Bool isMoving() const;
	Bool isMovingAwayFrom(Object *obj) const;

	// the following routines should only be called by the AIInternalMoveToState.  
	// They are used to determine when we are really through moving.  Due to the nature of the beast, 
	// we often exit one move state & immediately enter another.  This should not cause a "stop start".
	// jba.
	void friend_startingMove(void);
	void friend_endingMove(void);

	void friend_setPath(Path *newPath);
	Path* friend_getPath() { return m_path; }

	void friend_setGoalObject(Object *obj);

	virtual Bool processCollision(PhysicsBehavior *physics, Object *other); ///< Returns true if the physics collide should apply the force.  Normally not.  jba.
	ObjectID getIgnoredObstacleID( void ) const;

	// "Waypoint Mode" -----------------------------------------------------------------------------------
	const Waypoint *getCompletedWaypoint(void) const {return m_completedWaypoint;}
	void setCompletedWaypoint(const Waypoint *pWay) {m_completedWaypoint = pWay;}

	const LocomotorSet& getLocomotorSet(void) const {return m_locomotorSet;}
	void setPathExtraDistance(Real dist) {m_pathExtraDistance = dist;}
	inline Real getPathExtraDistance() const { return m_pathExtraDistance; }

	virtual Bool chooseLocomotorSet(LocomotorSetType wst);

	virtual CommandSourceType getLastCommandSource() const { return m_lastCommandSource; }

	const AttackPriorityInfo *getAttackInfo(void) {return m_attackInfo;}
	void setAttackInfo(const AttackPriorityInfo *info) {m_attackInfo = info;}

	void setCurPathfindCell(const ICoord2D &cell) {m_pathfindCurCell = cell;}
	void setPathfindGoalCell(const ICoord2D &cell) {m_pathfindGoalCell = cell;}
	
	void setPathFromWaypoint(const Waypoint *way, const Coord2D *offset);
	
	const ICoord2D *getCurPathfindCell(void) const {return &m_pathfindCurCell;}
	const ICoord2D *getPathfindGoalCell(void) const {return &m_pathfindGoalCell;}

	/// Return true if our path has higher priority.
	Bool hasHigherPathPriority(AIUpdateInterface *otherAI) const;
	void setFinalPosition(const Coord3D *pos) { m_finalPosition = *pos; m_doFinalPosition = false;}

	virtual UpdateSleepTime update( void );	///< update this object's AI

	/// if we are attacking "fromID", stop that and attack "toID" instead
	void transferAttack(ObjectID fromID, ObjectID toID);

	void setCurrentVictim( const Object *nemesis );			///<  Current victim.
	Object *getCurrentVictim( void ) const;
	virtual void notifyVictimIsDead() { }
	Bool isCarriedGunOn( const Object *victim ) const;	///< a rider's turret or a passenger is already shooting at victim
	void noteWithdrawTarget( const Object *victim );		///< an attack is ending; a helicopter or a turret may keep shooting victim while it moves
	void updateWithdrawTarget();												///< keep the turret on that target while it can, let go once it cannot

	// if we are attacking a position (and NOT an object), return it. otherwise return null.
	const Coord3D *getCurrentVictimPos( void ) const;	

	void setLocomotorUpgrade(Bool set);

	// This function is used to notify the unit that it may have a target of opportunity to attack.
	void wakeUpAndAttemptToTarget( void );
	
	void resetNextMoodCheckTime( void );

	//Specifically set a frame to check next mood time -- added for the purpose of ordering a stealth combat unit that can't 
	//autoacquire while stealthed, but isn't stealthed and can stealth and is not detected, and the player specifically orders
	//that unit to stop. In this case, instead of the unit autoacquiring another unit, and preventing him from stealthing,
	//we will instead delay the autoacquire until later to give him enough time to stealth properly.
	void setNextMoodCheckTime( UnsignedInt frame );

	///< States should call this with calledByAI set true to prevent them from checking every frame
	///< States that are doing idle checks should call with calledDuringIdle set true so that they check their
	///< States that will actually close with what they find (attack move) should call with allowOutOfWeaponRangeTargets
	///< set true, which searches vision range instead of only what is already within weapon range.
	///< requireWithinWeaponRange narrows that back to what can be shot without moving an inch, whoever
	///< is playing - an attack move whose approach is suspended still fires at what it can reach.

	Object *getNextMoodTarget( Bool calledByAI, Bool calledDuringIdle, Bool allowOutOfWeaponRangeTargets = FALSE, Bool requireWithinWeaponRange = FALSE );
	UnsignedInt getNextMoodCheckTime() const { return m_nextMoodCheckTime; }

	// This function will return a combination of MoodMatrixParameter flags.
	UnsignedInt getMoodMatrixValue( void ) const;
	UnsignedInt getMoodMatrixActionAdjustment( MoodMatrixAction action ) const;
	void setAttitude( AttitudeType tude );	///< set the behavior modifier for this agent

	// Common AI "status" effects -------------------------------------------------------------------
	Bool hasNationalism() const;
	Bool hasFanaticism() const;
	void evaluateMoraleBonus( Bool inHorde, Bool allowNationalism, HordeActionType type );
	void evaluateNationalismBonusClassic( Bool inHorde, Bool allowNationalism );
	void evaluateNationalismBonus( Bool inHorde, Bool allowNationalism );

#ifdef ALLOW_DEMORALIZE
	// demoralization ... what a nifty word to write.
	Bool isDemoralized( void ) const { return m_demoralizedFramesLeft > 0; }
	void setDemoralized( UnsignedInt durationInFrames );
#endif
	
	Bool canPathThroughUnits( void ) const { return m_canPathThroughUnits; }
	void setCanPathThroughUnits( Bool canPath ) { m_canPathThroughUnits = canPath; if (canPath) m_isBlockedAndStuck=false;}

	// Notify the ai that it has caused a crate to be created (usually by killing something.)
	void notifyCrate(ObjectID id) { m_crateCreated = id; }
	ObjectID getCrateID(void) const { return m_crateCreated; }
	Object* checkForCrateToPickup();

	void setIgnoreCollisionTime(Int frames) { m_ignoreCollisionsUntil = TheGameLogic->getFrame() + frames; }

	void setQueueForPathTime(Int frames);

	// For the attack move, that switches from move to attack, and the attack is CMD_FROM_AI, 
	// while the move is the original command source.  John A.
	void friend_setLastCommandSource( CommandSourceType source ) {m_lastCommandSource = source;}

	Bool canAutoAcquire() const { return getAIUpdateModuleData()->m_autoAcquireEnemiesWhenIdle; }

  Bool canAutoAcquireWhileStealthed() const ;


protected:
	
	/*
		AIUpdates run in the initial phase. 

		It's actually quite important that AI (the thing that drives Locomotors) and Physics
		run in the same order, relative to each other, for a given object; otherwise,
		interesting oscillations can occur in some situations, with friction being applied
		either before or after the locomotive force, making for huge stuttery messes. (srj)
	*/
	virtual SleepyUpdatePhase getUpdatePhase() const { return PHASE_INITIAL; }

	void setGoalPositionClipped(const Coord3D* in, CommandSourceType cmdSource);

	virtual Bool isAllowedToRespondToAiCommands(const AICommandParms* parms) const;

	// getAttitude is protected because other places should call getMoodMatrixValue to get all the facts they need to consider.
	AttitudeType getAttitude( void ) const;				///< get the current behavior modifier state.

	Bool blockedBy(Object *other); ///< Returns true if we are blocked by "other"
	Bool needToRotate(void); ///< Returns true if we are not pointing in the right direction for movement.
	Real calculateMaxBlockedSpeed(Object *other) const;

	virtual UpdateSleepTime doLocomotor();	// virtual so subclasses can override
	void chooseGoodLocomotorFromCurrentSet();

	void setLastCommandSource( CommandSourceType source );

	// subclasses may want to override this, to use a subclass of AIStateMachine.
	virtual AIStateMachine* makeStateMachine();

	virtual Bool getTreatAsAircraftForLocoDistToGoal() const;

	// yes, protected, NOT public... try to avoid exposing the state machine directly, please
	inline AIStateMachine* getStateMachine() { return m_stateMachine; }
	inline const AIStateMachine* getStateMachine() const { return m_stateMachine; }

	void wakeUpNow();

public:

	inline StateID getCurrentStateID() const { return getStateMachine()->getCurrentStateID(); }	///< return the id of the current state of the machine
	/// A move an AI hands a busy unit lands in the temporary state and leaves the current one alone, so ask that first.
	inline Bool isTurningToFace() const
	{
		StateID state = getStateMachine()->getTemporaryState();
		if( state == INVALID_STATE_ID )
			state = getCurrentStateID();
		return state == AI_FACE_OBJECT || state == AI_FACE_POSITION;
	}
/// @ todo -- srj sez: JBA NUKE THIS CODE, IT IS EVIL
	inline void friend_addToWaypointGoalPath( const Coord3D *pathPoint ) { getStateMachine()->addToGoalPath(pathPoint); }

	// this is intended for use ONLY by W3dWaypointBuffer and AIFollowPathState.
	inline const Coord3D* friend_getGoalPathPosition( Int index ) const { return getStateMachine()->getGoalPathPosition( index ); }

	// this is intended for use ONLY by W3dWaypointBuffer.
	Int friend_getWaypointGoalPathSize() const;

	// this is intended for use ONLY by W3dWaypointBuffer.
	inline Int friend_getCurrentGoalPathIndex() const { return m_nextGoalPathIndex; }

	// this is intended for use ONLY by AIFollowPathState.
	inline void friend_setCurrentGoalPathIndex( Int index ) { m_nextGoalPathIndex = index; }

	// this is intended for use ONLY by the production exit modules.
	void friend_setExitProductionRallyPoint( const Coord3D *pos );
	void friend_dropOrdersAfterExit() { m_hasExitProductionRallyPoint = FALSE; m_ordersAfterExit.clear(); }	///< a newer order replaces the rally point and the held ones

	// this is intended for use ONLY by the salvage collection pass in GameLogic.
	void friend_setSalvageReturnPosition( const Coord3D *pos );

	/** Down `entrance`, out of the mouth nearest `goal`, and on to `goal` the way `end` says.  FALSE,
			and nothing ordered, for a unit that cannot go into a tunnel. */
	Bool takeTunnelTrip( Object *entrance, const Coord3D *goal, TunnelTripEnd end, CommandSourceType cmdSource );
	Bool hasTunnelTrip() const { return m_hasTunnelTrip; }	///< on its way through the tunnel network to a move order's goal
	void endTunnelTrip();	///< what the trip was for is gone: forget it, unless the unit is already underground
	const Coord3D *getTunnelTripGoal() const { return &m_tunnelTripGoal; }	///< where the trip ends, once out of the far mouth
	Bool isExitingProducer() const;	///< still driving the exit path out of the structure that built it
	TunnelTripEnd getTunnelTripEnd() const { return m_tunnelTripEnd; }	///< how the leg from the far mouth is walked
#if defined(_DEBUG) || defined(_INTERNAL)	
	inline const Coord3D *friend_getRequestedDestination() const { return &m_requestedDestination; }
	inline const Coord3D *friend_getRequestedDestination2() const { return &m_requestedDestination2; }
#endif

	inline Object* getGoalObject() { return getStateMachine()->getGoalObject(); }	///< return the id of the current state of the machine
	inline const Coord3D* getGoalPosition() const { return getStateMachine()->getGoalPosition(); }	///< return the id of the current state of the machine

	inline WhichTurretType friend_getTurretSync() const { return m_turretSyncFlag; }
	inline void friend_setTurretSync(WhichTurretType t) { m_turretSyncFlag = t; }

	inline UnsignedInt getPriorWaypointID ( void ) { return m_priorWaypointID; };
	inline UnsignedInt getCurrentWaypointID ( void ) { return m_currentWaypointID; };

	inline void clearMoveOutOfWay(void) {m_moveOutOfWay1 = INVALID_ID; m_moveOutOfWay2 = INVALID_ID;}

	inline void setTmpValue(Int val) {m_tmpInt = val;}
	inline Int getTmpValue(void) {return m_tmpInt;}

	inline Bool getRetryPath(void) {return m_retryPath;}
	inline Bool didPathfindFindNothing(void) const {return m_pathfindFoundNothing;}
	void queueForPathOrRetry(void);	///< join the pathfind queue, or try again shortly when it is full
	Int repathDebounceFrames(void) const {return (Int)(m_pathTimestamp + ConvertRetailFrames(3) - TheGameLogic->getFrame());}	///< what is left of the tenth of a second a new path waits after the last one
	UnsignedInt getQueueForPathFrame(void) const {return m_queueForPathFrame;}	///< nonzero while a too-quick path request is held back
	
	inline void setAllowedToChase( Bool allow ) { m_allowedToChase = allow; }
	inline Bool isAllowedToChase() const { return m_allowedToChase; }

	// only for AIStateMachine.
	virtual void friend_notifyStateMachineChanged();

private:
	// this should only be called by load/save, or by chooseLocomotorSet.
	// it does no sanity checking; it just jams it in.
	Bool chooseLocomotorSetExplicit(LocomotorSetType wst);

	/// Pick this unit's lane off its own sideways distance from the route it was just handed.
	void seedLaneFraction( void );

	/** The crowd model, one frame, one unit: measure the band if it has not been measured, look at
			the neighbours once, and come back with where to steer and how fast. Everything the sandbox
			does per frame - separation, right of way, priority, passing, fanning out, easing through a
			bend - happens in here and expresses itself as those two numbers, because that is the whole
			interface the locomotor gives us. */
	void crowdSteer( Coord3D& goalPos, Real& speed );

	/// throw the measured band away; the next frame that needs one measures the new route
	void crowdReleaseCorridor( void );
	Path *crowdPlannedPath( const CrowdRoute& planned, const Coord3D& destination );	///< the planned route as a path from here, or NULL if it no longer fits

	/** Is this unit getting anywhere?  One test, once a frame, for every unit that is trying to
			drive somewhere, and the only place m_noProgress is written. */
	void updateProgress( void );

	/** And what to do when the answer has been no for long enough.  A ladder with a rung every
			second and a half, ending in one that always does something, so no unit can want to move
			and be ignored for longer than that.  The timing itself is `AIUpdate_stuckRung`, which is
			a free function so a test can hold it to the promise. */
	void stuckRescue( void );

	/** TRUE when a rescue manoeuvre owns the steering this frame, in which case `goalPos` has been
			replaced with where the unit is being sent and nothing else may touch it. */
	Bool rescueSteer( Coord3D& goalPos );

	/// Give up on the current route and ask the move state for a fresh one to the same place.
	void crowdRepath( void );

	/// Tell whoever is queued right behind us to make room, so we can back out of a jam.
	void crowdAskBehindToBackOff( const Coord2D& tan );

	/// TRUE when `other` gets the line and we are the one who has to do something about it
	Bool crowdOutranksMe( Object *other ) const;

private:
	UnsignedInt					m_priorWaypointID;						///< ID of the path we follwed to before the most recent one
	UnsignedInt					m_currentWaypointID;					///< ID of the most recent one...

	AIStateMachine*			m_stateMachine;							///< the state machine
	UnsignedInt					m_nextEnemyScanTime;				///< how long until the next enemy scan
	ObjectID						m_currentVictimID;					///< if not INVALID_ID, this agent's current victim.
	ObjectID						m_withdrawTargetID;					///< what a unit told to move mid-fight keeps shooting while it moves
	UnsignedInt					m_withdrawFrame;						///< the last frame that target was still held
	Real								m_desiredSpeed;						///< the desired speed of the tank
	CommandSourceType		m_lastCommandSource;			/**< Keep track of the source of the last command we got.
																									This is set immediately before the SetState that goes
																									to the state machine, so onEnter in there can know where
																									the command came from.
																								*/

	GuardMode							m_guardMode;
	Real									m_guardRadius;				///< set by a player's guard order; 0 for the vision-based range
	Bool									m_aggressiveStance;		///< set by MSG_SET_STANCE; FALSE is the defensive stance, EA's
	GuardTargetType				m_guardTargetType[2];
	Coord3D								m_locationToGuard;
	ObjectID							m_objectToGuard;
	const PolygonTrigger*	m_areaToGuard;

	// Attack Info --------------------------------------------------------------------------------------------
	const AttackPriorityInfo*	m_attackInfo;
	AsciiString								m_attackInfoToLoad;	///< the set a save named: the script engine that holds it loads after the objects

	// "Planning Mode" -----------------------------------------------------------------------------------------
	enum { MAX_WAYPOINTS = 16 };
	Coord3D						m_waypointQueue[ MAX_WAYPOINTS ];		///< the waypoint queue
	Int								m_waypointCount;										///< number of waypoints in the queue
	Int								m_waypointIndex;										///< current waypoint we are moving to
	const Waypoint*		m_completedWaypoint;								///< Set to the last waypoint in a path when the FollowWaypointPath is complete.

	// Pathfinding ---------------------------------------------------------------------------------------------
	Path*				m_path;											///< current path to follow (for moving)
	ObjectID		m_requestedVictimID;
	Coord3D			m_requestedDestination;
	Coord3D			m_requestedDestination2;							
	UnsignedInt	m_pathTimestamp;						///< time of path construction
	ObjectID		m_ignoreObstacleID;					///< ignore this obstacle when pathfinding
	Real				m_pathExtraDistance;				///< If we are following a waypoint path, there will be extra distance beyond the current path.
	ICoord2D		m_pathfindGoalCell;					///< Cell we are moving towards.
	ICoord2D		m_pathfindCurCell;					///< Cell we are currently occupying.
	Int					m_blockedFrames;						///< Number of frames we've been blocked.
	Real				m_curMaxBlockedSpeed;				///< Max speed we can have and not run into blocking things.
	Real				m_laneFraction;							///< Where across the route's width we ride: 0 right edge, 1 left edge.
	Bool				m_laneFractionValid;				///< False until the fraction has been seeded off the current path.
	UnsignedInt	m_laneHoldFrame;						///< A lane taken to get round somebody is kept until this frame.
	Real				m_pendingLane;							///< Lane the ordering group picked for us, waiting for the path to arrive.
	Bool				m_hasPendingLane;						///< False when nobody handed us one and we have to measure our own.
	CrowdCorridor* m_corridor;						///< thecurrent route with the width of the ground beside it. Owned; dies with the path.
	Real				m_crowdLat;									///< howfar left of the route we ride, in world units.
	Real				m_pendingCrowdLat;					///< thesame, as the ordering group wanted it, waiting for the path.
	Bool				m_hasPendingCrowdLat;				///< FALSEfor a unit ordered on its own, which rides where it already is.
	Int					m_crowdLaneIdx;							///< ourplace across the group, 0 = leftmost.
	Int					m_crowdLaneOf;							///< howmany of us there were; 0 = nobody handed us a slot.
	Real				m_crowdLaneSpace;						///< howfar apart the group wanted its lanes, one widest body plus air.
	Int					m_crowdFit;									///< howmany lanes the band is currently held to carry; 0 = not worked out yet.
	Bool				m_crowdLatValid;						///< FALSEuntil the lane has been taken up against the current route.
	UnsignedInt	m_crowdHoldFrame;						///< a lanetaken to pass somebody is kept until this frame.
	Int					m_crowdSample;							///< whichsample of the band we were beside last frame (search hint; -1 = no band).
	Int					m_crowdQueued;							///< consecutiveframes spent braking or stuck behind somebody.
	Int					m_crowdSide;								///< whichside we prefer to pass on, +1 left, -1 right.
	CrowdRoute	m_crowdPlanned;							///< the path the group planned for us, waiting for the move to ask for one; empty = search.
	/* The rest of the crowd state is transient and deliberately not saved, for the same reason the
		 band itself is not: a filter, a stuck count and a half-finished backing-out manoeuvre are all
		 rebuilt within a second of the load, and a saved one restarts a jam that is over. */
	Real				m_crowdSepSmooth;						///< low-passedsideways push from the neighbours.
	Real				m_crowdCap;									///< the speed cap held from last frame: a brake lowers it at once, only a ramp raises it.
	Bool				m_crowdCapValid;						///< FALSE until there is a held cap to ramp up from.
	Real				m_crowdAim;									///< low-passeddirection the steering point is taken in, radians.
	Bool				m_crowdAimValid;						///< FALSEuntil the filter has something to start from.
	/* Being stuck, and getting out of it.  None of this is part of the crowd model: a unit that has
			stopped without a collision to show for it is not a crowd problem, it is the problem, and
			until this was pulled out of the crowd model it was only ever asked about units that happened
			to have been handed a lane by a group order. */
	Int					m_noProgress;								///< consecutive frames wanting to move, not moving, not turning either.
	Int					m_headOnFrames;							///< consecutive frames held up by somebody driving straight at us.
	Bool				m_headOnSeen;								///< a collision this frame was with somebody driving straight at us.
	Coord2D			m_sideStep;									///< a soldier's way out of the vehicles he touched since the last move, summed.
	Bool				m_sideStepSeen;							///< and whether he touched any.
	Coord3D			m_lastProgressPos;					///< where we were last frame, which is how the above is counted.
	Real				m_lastProgressAngle;				///< and which way we were pointing, because coming about is progress too.
	/* Dithering: driving a long way and getting nowhere, which the frame by frame test above cannot
			see because the unit is moving on every one of those frames. */
	Coord3D			m_ditherFrom;								///< where the current window opened.
	UnsignedInt	m_ditherFrame;							///< and when.
	Real				m_ditherTravel;							///< how much ground has actually been covered since, path length not displacement.
	Int					m_rescueStage;							///< how far up the rescue ladder this episode has climbed.
	Coord3D			m_rescueTo;									///< the free ground a wedged unit is backing out to.
	UnsignedInt	m_rescueUntil;							///< frame that manoeuvre gives up (0 = not backing out).
	UnsignedInt	m_rescueCool;								///< no second drastic thing before this frame.
	Bool				m_repathAsked;							///< crowdRepath ran this doLocomotor; keep its blocked count for the move state.
	Real				m_bumpSpeedLimit;						///< Max speed after bumping a unit.
	UnsignedInt	m_ignoreCollisionsUntil;		///< Timer to cheat if we get stuck.
	/**
		If nonzero and >= now, frame when we should queueForPath.

		NEVER EVER EVER set this directly. you must call setQueueForPathTime() in ALL CASES.
		NEVER EVER EVER set this directly. you must call setQueueForPathTime() in ALL CASES.
		NEVER EVER EVER set this directly. you must call setQueueForPathTime() in ALL CASES.
		NEVER EVER EVER set this directly. you must call setQueueForPathTime() in ALL CASES.
	*/
	UnsignedInt	m_queueForPathFrame;
	Coord3D			m_finalPosition;						///< Final position for the moveto states.
	ObjectID		m_repulsor1;								///< First object we are running away from.
	ObjectID		m_repulsor2;								///< Second object we are running away from.
	Int					m_nextGoalPathIndex;				///< The simple goal path index we are moving to next.
	ObjectID		m_moveOutOfWay1;
	ObjectID		m_moveOutOfWay2;
	Coord3D			m_exitProductionRallyPoint;	///< Rally point to attack-move to once we are clear of the producer that just built us.
	Bool				m_hasExitProductionRallyPoint;	///< True while m_exitProductionRallyPoint is still waiting to be ordered.
	mutable std::vector<AICommandParmsStorage> m_ordersAfterExit;	///< Player orders given while still on the exit path, carried out in order once off it.
	Coord3D			m_salvageReturnPosition;		///< Where we stood when we were sent to fetch a salvage crate.
	Bool				m_hasSalvageReturnPosition;	///< True while the walk back to m_salvageReturnPosition is still owed.
	Coord3D			m_tunnelTripGoal;						///< Where a move order sent us before a tunnel shortened the way there.
	Bool				m_hasTunnelTrip;						///< True from the order to enter a tunnel until the walk from the exit to m_tunnelTripGoal is ordered.
	TunnelTripEnd	m_tunnelTripEnd;					///< How the last leg from the far mouth to m_tunnelTripGoal is walked.

	// Locomotors -------------------------------------------------------------------------------------------------
	enum LocoGoalType	 // Note - written out in save/load xfer, don't change these numbers.  jba.
	{
		NONE = 0,
		POSITION_ON_PATH = 1,
		POSITION_EXPLICIT = 2,
		ANGLE = 3
	};
	LocomotorSet			m_locomotorSet;
	Locomotor*				m_curLocomotor;
	LocomotorSetType	m_curLocomotorSet;
	LocoGoalType			m_locomotorGoalType;
	Coord3D						m_locomotorGoalData;

	// Turrets -------------------------------------------------------------------------------------------------
	TurretAI*					m_turretAI[MAX_TURRETS];		// ai for our turret (or null if no turret)
	WhichTurretType		m_turretSyncFlag;						///< for private use by multiturreted units where the turrets must sync with each other

	// AI -------------------------------------------------------------------------------------------
	AttitudeType	m_attitude;
	UnsignedInt		m_nextMoodCheckTime;

	// Common AI "status" effects -------------------------------------------------------------------
#ifdef ALLOW_DEMORALIZE
	UnsignedInt   m_demoralizedFramesLeft;
#endif
#ifdef ALLOW_SURRENDER
	UnsignedInt		m_surrenderedFramesLeft;				///< Non-zero when in a surrendered state
	Int						m_surrenderedPlayerIndex;				///< if surrendered, the playerindex to whom we are surrendered, or -1 if available for everyone
#endif

	// Note the id of a crate we caused to be created.
	ObjectID			m_crateCreated;

	Int						m_tmpInt;

	// -------------------------------------------------------------------
	Bool				m_doFinalPosition;					///< True if we are moving towards final position in a non-physics kind of way. 
	Bool				m_waitingForPath;						///< True if we have a pathfind request outstanding.
	Bool				m_isAttackPath;							///< True if we have an attack path.
	Bool				m_isFinalGoal;							///< True if this path ends at our destination (as opposed to an intermediate point on a waypoint path).
	Bool				m_isApproachPath;						///< True if we are just approaching to tighten.
	Bool				m_isSafePath;								///< True if we are just approaching to tighten.
	Bool				m_movementComplete;					///< True if we finished an AIInternalMoveToState.
	Bool				m_isMoving;									///< True if we are in an AIInternalMoveToState.
	Bool				m_isBlocked;
	Bool				m_isBlockedAndStuck;				///< True if we are stuck & need to recompute path.
	Bool				m_upgradedLocomotors;
	Bool				m_canPathThroughUnits;			///< Can path through units.
	Bool				m_randomlyOffsetMoodCheck;	///< If true, randomly offset the mood check rate next time, to avoid "spiking" of ai checks
	Bool				m_isAiDead;									///< TRUE if dead
	Bool				m_isRecruitable;						///< TRUE if recruitable by the ai.
	Bool				m_executingWaypointQueue;						///< if true, we are moving thru the waypoints
	Bool				m_retryPath;								///< If true, we need to try the path a second time.  jba.
	Bool				m_pathfindFoundNothing;			///< The last ground search, closest-path fallback included, returned no path.
	Bool				m_allowedToChase;						///< Allowed to pursue targets.
	Bool				m_isInUpdate;								///< If true, we are inside our update method.
	Bool				m_fixLocoInPostProcess;		
};

/** Which rung of the rescue ladder a unit takes next, given how long it has wanted to move without
		moving and how far up the ladder this episode has already climbed.

		0 is "nothing yet", 1 asks for another route, 2 asks the neighbours to move and stops bouncing
		off them, 3 backs the body out, and -1 says start again from the bottom.  It is a free function
		with no state so the promise it makes can be tested rather than believed: the rungs come in
		order, none is skipped, and the ladder never stops offering one. */
extern Int AIUpdate_stuckRung( Int noProgressFrames, Int stageDone );

/** Has this unit driven a long way and got nowhere?

		`travelled` is the ground actually covered over the window, `net` the straight line from where
		it started to where it ended, and `bodySize` the unit's own diameter.  A unit going somewhere
		has a net close to its travel; one shuffling back and forth beside a building has a travel of
		several body lengths and a net of almost nothing.  The body-size floor is what keeps a unit
		that is simply slow, or stopped, out of it: those are the other test's business. */
extern Bool AIUpdate_isDithering( Real net, Real travelled, Real bodySize );

//------------------------------------------------------------------------------------------------------------
// Inlines
//

#endif // _AI_UPDATE_H_

