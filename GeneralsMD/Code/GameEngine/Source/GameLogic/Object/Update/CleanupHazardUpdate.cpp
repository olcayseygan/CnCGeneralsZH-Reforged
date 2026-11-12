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

// FILE: CleanupHazardUpdate.cpp //////////////////////////////////////////////////////////////////////////
// Author: Kris Morness, August 2002
// Desc:   Update module to handle independent targeting of hazards to cleanup.
///////////////////////////////////////////////////////////////////////////////////////////////////

// INCLUDES ///////////////////////////////////////////////////////////////////////////////////////
#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#define DEFINE_WEAPONSLOTTYPE_NAMES

#include "Common/Player.h"
#include "Common/RandomValue.h"
#include "Common/ThingTemplate.h"
#include "Common/Xfer.h"
#include "GameClient/Drawable.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/PartitionManager.h"
#include "GameLogic/Object.h"
#include "GameLogic/ObjectIter.h"
#include "GameLogic/Module/CleanupHazardUpdate.h"
#include "GameLogic/Module/PhysicsUpdate.h"
#include "GameLogic/Weapon.h"
#include "GameLogic/WeaponSet.h"
#include "GameLogic/Module/AIUpdate.h"

#ifdef _INTERNAL
// for occasional debugging...
//#pragma optimize("", off)
//#pragma MESSAGE("************************************** WARNING, optimization disabled for debugging purposes")
#endif

/** Scans in a row an area cleaner stays out of reach of its hazard, standing about and getting no nearer
	to it, before it gives the area up. */
static const Int MAX_CLEANUP_APPROACHES = 8;

/** How much nearer to its hazard a scan has to find it to count as getting there, and how far it has to
	have driven since the last scan to count as on its way. */
static const Real CLEANUP_PROGRESS = 10.0f;
static const Real CLEANUP_DRIVEN = 20.0f;

/** How far round the hazard it gave up on it leaves the ground alone: a toxin field is many hazards, and
	remembering the one it gave up on, the next in the same field was gone for at once. The same reach
	makes the hazards of one field one target for the give-up count. */
static const Real GIVEN_UP_RADIUS = 150.0f;

/** How long an area cleaner goes with no hazard hosed or gone and no nearer to one before it gives the
	area up, whatever the scan count says. A field takes seconds to hose down. */
static const UnsignedInt CLEANUP_STUCK_FRAMES = 20 * LOGICFRAMES_PER_GAME_SECOND;

/** One scan of an area cleaner out of reach of its hazard: whether it counts toward giving up. It does
	when the cleaner stands no CLEANUP_PROGRESS nearer than the nearest it had come and has driven under
	CLEANUP_DRIVEN since the last scan. One driving round a river or a cliff gets no nearer for a while
	but keeps moving; one at the pathfinder's nearest point to a hazard it cannot reach stands there.
	A nearer reading becomes the new nearest. Split out for the tests. */
Bool CleanupHazard_stalls( Real distance, Real driven, Real *closest )
{
	if( distance <= *closest - CLEANUP_PROGRESS )
	{
		*closest = distance;
		return FALSE;
	}
	return driven < CLEANUP_DRIVEN;
}

/** Every hazard but those on the ground given up on. */
class PartitionFilterRejectGivenUp : public PartitionFilter
{
	Bool m_any;
	Coord3D m_pos;
public:
	PartitionFilterRejectGivenUp( Bool any, const Coord3D &pos ) : m_any( any ), m_pos( pos ) {}
	virtual Bool allow( Object *objOther )
	{
		return !m_any || sqr( objOther->getPosition()->x - m_pos.x ) + sqr( objOther->getPosition()->y - m_pos.y ) > sqr( GIVEN_UP_RADIUS );
	}
#if defined(_DEBUG) || defined(_INTERNAL)
	virtual const char* debugGetName() { return "PartitionFilterRejectGivenUp"; }
#endif
};

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
CleanupHazardUpdateModuleData::CleanupHazardUpdateModuleData()
{
	m_weaponSlot				= PRIMARY_WEAPON;
	m_scanFrames				= 0;
	m_scanRange					= 0.0f;
}

//-------------------------------------------------------------------------------------------------
/*static*/ void CleanupHazardUpdateModuleData::buildFieldParse(MultiIniFieldParse& p)
{
	ModuleData::buildFieldParse(p);

	static const FieldParse dataFieldParse[] = 
	{
		{ "WeaponSlot",						INI::parseLookupList,						TheWeaponSlotTypeNamesLookupList, offsetof( CleanupHazardUpdateModuleData, m_weaponSlot ) },
		{ "ScanRate",							INI::parseDurationUnsignedInt,	NULL, offsetof( CleanupHazardUpdateModuleData, m_scanFrames ) },
		{ "ScanRange",						INI::parseReal,									NULL, offsetof( CleanupHazardUpdateModuleData, m_scanRange ) },
		{ 0, 0, 0, 0 }
	};
	p.add(dataFieldParse);
}

//-------------------------------------------------------------------------------------------------
CleanupHazardUpdate::CleanupHazardUpdate( Thing *thing, const ModuleData* moduleData ) : UpdateModule( thing, moduleData )
{
	m_bestTargetID							= INVALID_ID;
	m_nextScanFrames						= 0;
	m_nextShotAvailableInFrames = 0;
	m_inRange  									= false;
	m_weaponTemplate						= NULL;
	m_moveRange									= 0.0f;
	m_pos.zero();
	m_resumeGuard								= FALSE;
	m_resumeGuardMode						= GUARDMODE_NORMAL;
	m_returning									= FALSE;
	m_approaches								= 0;
	m_approaching								= FALSE;
	m_approachPos.zero();
	m_lastScanPos.zero();
	m_closestApproach						= 0.0f;
	m_progressFrame							= 0;
	m_lastTargetID							= INVALID_ID;
	m_resumeGuardObject					= INVALID_ID;
	m_hasGivenUp								= FALSE;
	m_givenUpPos.zero();

}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
CleanupHazardUpdate::~CleanupHazardUpdate( void )
{

}


//-------------------------------------------------------------------------------------------------
void CleanupHazardUpdate::onObjectCreated()
{
	const CleanupHazardUpdateModuleData *data = getCleanupHazardUpdateModuleData();
	Object *self = getObject();
	
	//Make sure we have a weapon template
	self->setWeaponSetFlag( WEAPONSET_VETERAN );
	Weapon *weapon = self->getWeaponInWeaponSlot( data->m_weaponSlot );
	if( !weapon )
	{
		DEBUG_CRASH( ("CleanupHazardUpdate for %s doesn't have a valid weapon template", 
			getObject()->getTemplate()->getName().str() ) );
		return;
	}
	m_weaponTemplate = weapon->getTemplate();

	//Make sure our firing range is smaller than the scan range.
	WeaponBonus bonus;
	bonus.clear();
	Real attackRange = m_weaponTemplate->getAttackRange( bonus );
	if( data->m_scanRange <= attackRange )
	{
		DEBUG_CRASH( ("CleanupHazardUpdate for %s requires the scan range (%.1f) being larger than the firing range (%.1f)",
			getObject()->getTemplate()->getName().str(), data->m_scanRange, attackRange ) );
	}
}

//-------------------------------------------------------------------------------------------------
/** The update callback. */
//-------------------------------------------------------------------------------------------------
UpdateSleepTime CleanupHazardUpdate::update()
{	
/// @todo srj use SLEEPY_UPDATE here
	const CleanupHazardUpdateModuleData *data = getCleanupHazardUpdateModuleData();
	Object *obj = getObject();

	//Make sure we are "busy" for scripting purposes if the unit is cleaning up an area.
	if( m_moveRange > 0.0f )
	{
		//Means we are cleaning up an AREA, not just things immediately in range.
		AIUpdateInterface *ai = obj->getAI();
		if( ai )
		{
			if( ai->isIdle() )
			{
				//Keep him busy even though he's not moving -- he might be cleaning up.
				ai->aiBusy( CMD_FROM_AI );
			}
			else if( ai->getLastCommandSource() != CMD_FROM_AI )
			{
				//Either the player or a script gave a NEW order so abandon the cleanup area cause.
				m_moveRange = 0.0f;
				m_resumeGuard = FALSE;
				m_returning = FALSE;
				m_approaches = 0;
				m_approaching = FALSE;
				return UPDATE_SLEEP_NONE;
			}
		}
	}

	//Optimized firing at acquired target
	if( m_nextScanFrames > 0 )
	{
		m_nextScanFrames--;
		fireWhenReady(); //Only happens if something is tracked.
		return UPDATE_SLEEP_NONE;
	}
	m_nextScanFrames = data->m_scanFrames;

	if( m_moveRange == 0.0f )
		startAutoCleanup();

	//Periodic scanning (expensive)
	Object *found = scanClosestTarget();
	if( found )
	{
		m_returning = FALSE;
		// Cleaning an area: out of the weapon's reach scan after scan, and no nearer, is a hazard it cannot
		// drive up to. It gives the area up rather than order the same drive for good, and does not go for
		// the ground round that hazard again: noticed from home, it drove the same way for the hazard's
		// lifetime. Counted by distance and by how far it drove since the last scan, not by the AI state:
		// fireWhenReady orders the drive again whenever it stops, so a scan nearly always found it moving
		// and the count never got anywhere. Counted for the ground, not the one hazard: the nearest of a
		// toxin field changes as it drives, and each change started the count over
		//
		// And by time: in the cliff-top run (ambulancegiveup.txt on Twilight Flame) the count never reached
		// its eight in 6,000 frames, though nothing was ever cleaned, so whatever the scans read, an area
		// that has seen no progress for CLEANUP_STUCK_FRAMES is given up as well.  The clock starts with
		// the area; progress is a scan that finds it CLEANUP_PROGRESS nearer, the hazard it finds hit since
		// the last scan, or the one it was after gone (cleaned, or run out).  Driving is not: the likeliest
		// reading of that run is a cleaner shuttling along the cliff foot, over CLEANUP_DRIVEN a scan, which
		// the count takes for a drive round.  Each scan logs what it read, so the next run says which of the
		// two fired and why.
		WeaponBonus bonus;
		bonus.clear();
		const Real distance = (Real)sqrt( ThePartitionManager->getDistanceSquared( obj, found, FROM_CENTER_2D ) );
		const Real reach = m_weaponTemplate->getAttackRange( bonus );
		if( m_moveRange > 0.0f )
		{
			const Coord3D *at = found->getPosition();
			const Coord3D *here = obj->getPosition();
			const UnsignedInt now = TheGameLogic->getFrame();
			const Real driven = (Real)sqrt( sqr( here->x - m_lastScanPos.x ) + sqr( here->y - m_lastScanPos.y ) );
			if( !m_approaching )
				m_progressFrame = now;		// the clock runs from the start of the area, not of each ground in it
			if( !m_approaching || sqr( at->x - m_approachPos.x ) + sqr( at->y - m_approachPos.y ) > sqr( GIVEN_UP_RADIUS ) )
			{
				// fireWhenReady drives only an idle or busy cleaner, and one still on its way to the old ground
				// (in that run, never there) passed by a field it could reach for 900 frames.  Two grounds whose
				// nearest swaps as it drives are not ordered: only ground nearer by CLEANUP_PROGRESS than the old
				if( m_approaching && distance > reach && distance <= m_closestApproach - CLEANUP_PROGRESS )
				{
					obj->getAI()->aiIdle( CMD_FROM_AI );
					obj->getAI()->aiMoveToPosition( at, CMD_FROM_AI );
				}
				m_approaching = TRUE;
				m_approachPos = *at;
				m_closestApproach = distance;
				m_approaches = 0;
			}
			else
			{
				// progress for the clock: the hazard it was after gone, the one found hit since the last scan
				// (whichever of two puddles in reach is nearest), or a CLEANUP_PROGRESS nearer.  Driving is not:
				// one shuttling along a cliff foot drives every scan
				const Bool targetGone = m_lastTargetID != INVALID_ID && TheGameLogic->findObjectByID( m_lastTargetID ) == NULL;
				const Bool sameHazard = found->getID() == m_lastTargetID;
				const Bool hosed = found->getBodyModule()->getLastDamageTimestamp() + data->m_scanFrames >= now;
				// another hazard of the same ground being nearer is no progress: the plateau run swapped from
				// one field to the other at frame 1219, and the nearer reading started the clock over
				if( !sameHazard )
					m_closestApproach = min( m_closestApproach, distance );
				if( targetGone || hosed || distance <= m_closestApproach - CLEANUP_PROGRESS )
					m_progressFrame = now;
				if( distance > reach && CleanupHazard_stalls( distance, driven, &m_closestApproach ) )
					++m_approaches;
				else
					m_approaches = 0;
			}
			DEBUG_LOG(("CLEANUP frame %d '%s' %d scan: hazard %d %.0f away, reach %.0f, drove %.0f, nearest %.0f, %d stalls, %d frames since progress, state %d\n",
				now, obj->getTemplate()->getName().str(), obj->getID(), found->getID(), distance, reach, driven, m_closestApproach, m_approaches,
				(Int)(now - m_progressFrame), (Int)obj->getAI()->getCurrentStateID()));
			m_lastScanPos = *here;
			m_lastTargetID = found->getID();
			if( m_approaches >= MAX_CLEANUP_APPROACHES || now - m_progressFrame > CLEANUP_STUCK_FRAMES )
			{
				m_hasGivenUp = TRUE;
				m_givenUpPos = m_approachPos;
				finishArea( m_approaches >= MAX_CLEANUP_APPROACHES ? "gave up on a hazard it cannot reach" : "gave up on a hazard it cannot reach, no progress in time", TRUE );
				return UPDATE_SLEEP_NONE;
			}
		}
		//1 frame can make a big difference so fire ASAP!
		fireWhenReady();
	}
	else if( m_moveRange )
	{
		//There's nothing nearby, so if we are cleaning up an area versus hazards 
		//immediately in range, set the AI to idle so it can advance to the next script!
		AIUpdateInterface *ai = obj->getAI();
		if( ai && (ai->isIdle() || ai->isBusy()) )
		{
			Real fDist = sqrt( ThePartitionManager->getDistanceSquared( obj, &m_pos, FROM_CENTER_2D ) );
			// the walk back counts as done once it has stopped: a unit parked on the spot used to send it
			// back there every second for good
			if( fDist < 25.0f || m_returning )
			{
				//Abort clean area because there's nothing left to clean!
				finishArea( "area clean", FALSE );
			}
			else
			{
				//Abort clean area AFTER we move back to our initial position!
				ai->aiMoveToPosition( &m_pos, CMD_FROM_AI );
				m_returning = TRUE;
			}
		}
	}
	return UPDATE_SLEEP_NONE;
}

//-------------------------------------------------------------------------------------------------
void CleanupHazardUpdate::fireWhenReady()
{
	const CleanupHazardUpdateModuleData *data = getCleanupHazardUpdateModuleData();
	Object *self = getObject();

	//Track our target (this code prevents the object from moving beyond range)
	//If we are ordered to clean an area, then range doesn't matter because
	//we allow the unit to move to the area.
	Object *target = TheGameLogic->findObjectByID( m_bestTargetID );
	if( target && m_moveRange == 0.0f )
	{
		WeaponBonus bonus;
		bonus.clear();
		Real fireRange = m_weaponTemplate->getAttackRange( bonus );
		Object *me = getObject();
		Real fDist = sqrt( ThePartitionManager->getDistanceSquared( me, target, FROM_CENTER_2D ) );
		if( fDist < fireRange )
		{
			//We are currently in range!
			m_inRange = true;
		}
		else
		{
			if( m_inRange )
			{
				//We were in range last frame, but the target has moved out of firing range, so 
				//re-evaluate by forcing a new scan.
				m_nextScanFrames = GameLogicRandomValue( 0, ConvertRetailFrames( 3 ) );
				m_bestTargetID = INVALID_ID;
				m_inRange = false;	// or every later out-of-range target rescans and drops too
				if( !m_nextScanFrames )
				{
					scanClosestTarget();
					m_nextScanFrames = data->m_scanFrames;
					target = NULL; //Set target to NULL so we don't shoot at it (might be out of range)
				}
			}
			else
			{
				//Not in range
				m_inRange = false;
			}
		}
	}
	
	if( m_nextShotAvailableInFrames > 0 )
	{
		//We can't fire this frame.
		m_nextShotAvailableInFrames--;
		return;
	}

	//Fire control!
	if( target )
	{
		AIUpdateInterface *ai = self->getAI();
		if( ai )
		{
			WeaponBonus bonus;
			bonus.clear();
			if( (ai->isIdle() || ai->isBusy()) && m_moveRange > 0.0f &&
					ThePartitionManager->getDistanceSquared( self, target, FROM_CENTER_2D ) > sqr( m_weaponTemplate->getAttackRange( bonus ) ) )
			{
				// cleaning an area, with the hazard out of reach: drive up to it first, since the attack's own
				// approach never moved the unit and it stood shooting at nothing until the area was given up.
				// Out of the busy state first: an AI move given to a busy unit is only a temporary state laid
				// over it, ordered again every frame, and the unit crept at a step a frame
				ai->aiIdle( CMD_FROM_AI );
				ai->aiMoveToPosition( target->getPosition(), CMD_FROM_AI );
			}
			else if( ai->isIdle() || ai->isBusy() )
			{
				// lock it just till the weapon is empty or the attack is "done"
				self->setWeaponLock( data->m_weaponSlot, LOCKED_TEMPORARILY );
				ai->aiAttackObject( target, NO_MAX_SHOTS_LIMIT, CMD_FROM_AI );
			}
		}
	}
}

//-------------------------------------------------------------------------------------------------
Object* CleanupHazardUpdate::scanClosestTarget()
{
	const CleanupHazardUpdateModuleData *data = getCleanupHazardUpdateModuleData();
	Object *me = getObject();
	Object *bestTargetInRange = NULL;
	m_bestTargetID = INVALID_ID;

	if( m_moveRange > 0.0f )
	{
		//Look for targets around the target position only (but add scan range and move range).
		//This case only happens when we are performing a cleanup area command.
		bestTargetInRange = closestHazard( &m_pos, data->m_scanRange + m_moveRange );
	}
	else
	{
		//Look for targets near me -- passive default.
		bestTargetInRange = closestHazard( me->getPosition(), data->m_scanRange );
	}

	if( bestTargetInRange ) 
	{
		m_bestTargetID = bestTargetInRange->getID();
	}
	return bestTargetInRange;
}

//-------------------------------------------------------------------------------------------------
Object* CleanupHazardUpdate::closestHazard( const Coord3D *from, Real range ) const
{
	PartitionFilterAcceptByKindOf kindFilter(MAKE_KINDOF_MASK(KINDOF_CLEANUP_HAZARD), KINDOFMASK_NONE);
	PartitionFilterSameMapStatus filterMapStatus(getObject());
	PartitionFilterRejectGivenUp givenUpFilter( m_hasGivenUp, m_givenUpPos );
	PartitionFilter* filters[] = { &kindFilter, &filterMapStatus, &givenUpFilter, NULL };
	return ThePartitionManager->getClosestObject( from, range, FROM_CENTER_2D, filters );
}

//-------------------------------------------------------------------------------------------------
/** The hazard it is shooting or driving at, or the middle of the area when it has none yet. */
void CleanupHazardUpdate::giveUpArea()
{
	const Object *target = TheGameLogic->findObjectByID( m_bestTargetID );
	m_givenUpPos = target ? *target->getPosition() : m_pos;
	m_hasGivenUp = TRUE;
}

//-------------------------------------------------------------------------------------------------
/** The area is done with: back on guard if a cleanup it started took it off guard, otherwise out of
	the busy state the area kept it in, which has no way out of its own. Left busy, it never read as
	idle again, so scripts waiting on it stalled and it never noticed another hazard. goHome walks it
	back to where the area started, for an area given up away from there. */
void CleanupHazardUpdate::finishArea( const char *why, Bool goHome )
{
	Object *obj = getObject();
	AIUpdateInterface *ai = obj->getAI();
	DEBUG_LOG(("CLEANUP frame %d '%s' %d %s%s\n", TheGameLogic->getFrame(), obj->getTemplate()->getName().str(), obj->getID(),
		why, m_resumeGuard ? ", back on guard" : ""));
	m_moveRange = 0.0f;
	m_returning = FALSE;
	m_approaches = 0;
	m_approaching = FALSE;
	if( m_resumeGuard )
	{
		m_resumeGuard = FALSE;
		// back on the object it guarded, not the spot it stood on when it left; on the spot if that is gone
		Object *guarded = TheGameLogic->findObjectByID( m_resumeGuardObject );
		m_resumeGuardObject = INVALID_ID;
		if( guarded && !guarded->isEffectivelyDead() )
			ai->aiGuardObject( guarded, (GuardMode)m_resumeGuardMode, CMD_FROM_AI );
		else
			ai->aiGuardPosition( &m_pos, (GuardMode)m_resumeGuardMode, CMD_FROM_AI );
	}
	else if( goHome )
		ai->aiMoveToPosition( &m_pos, CMD_FROM_AI );
	else
		ai->aiIdle( CMD_FROM_AI );
}

//-------------------------------------------------------------------------------------------------
/** How far past its own scan range a cleaner standing idle or on guard goes for a hazard by itself.
	The scan range is the weapon's reach, so before this an Ambulance cleaned only what it was parked
	on and watched a toxin field a truck's length away. With the Ambulance's 100 it notices up to 300,
	the reach its cleanup ability is given around the point it is sent to. */
static const Real AUTO_CLEANUP_MOVE_RANGE = 200.0f;

/** Idle, or on a guard order, with a hazard in reach: clean the area round where it stands the way the
	cleanup ability does, which brings it back here when nothing is left, and guard here again if it
	was guarding. Any other order, the player's or a script's, is left alone, and one given while it is
	cleaning ends the cleanup as it ends the ability's. */
void CleanupHazardUpdate::startAutoCleanup()
{
	const CleanupHazardUpdateModuleData *data = getCleanupHazardUpdateModuleData();
	Object *obj = getObject();
	AIUpdateInterface *ai = obj->getAI();
	if( !ai || obj->isContained() )
		return;
	const Bool guarding = ai->getCurrentStateID() == AI_GUARD;
	if( !guarding && !ai->isIdle() )
		return;
	// the AI's own Ambulance cleans what the AI sends it to, which keeps it behind the army and clear of
	// the guns it knows: by itself it drove up to 300 ahead of where it waited, and 7 of 25 lost in twelve
	// Hard against Medium matches on Twilight Flame (2026-10-11) died within 30 s of a cleanup it started
	if( obj->getControllingPlayer() && obj->getControllingPlayer()->isAIDutyAmbulance( obj ) )
		return;

	if( !closestHazard( obj->getPosition(), data->m_scanRange + AUTO_CLEANUP_MOVE_RANGE ) )
		return;

	m_pos = *obj->getPosition();
	m_moveRange = AUTO_CLEANUP_MOVE_RANGE;
	// read before aiBusy below, whose exit from the guard state clears the guard target type
	m_resumeGuard = guarding;
	m_resumeGuardMode = ai->getGuardMode();
	m_resumeGuardObject = guarding && ai->getGuardTargetType() == GUARDTARGET_OBJECT ? ai->getGuardObject() : INVALID_ID;
	ai->aiBusy( CMD_FROM_AI );
	DEBUG_LOG(("CLEANUP frame %d '%s' %d of player %d goes to clean by itself round (%.0f,%.0f)%s\n", TheGameLogic->getFrame(),
		obj->getTemplate()->getName().str(), obj->getID(), obj->getControllingPlayer() ? obj->getControllingPlayer()->getPlayerIndex() : -1,
		m_pos.x, m_pos.y, guarding ? ", off guard" : ""));
}

//-------------------------------------------------------------------------------------------------
//This allows the unit to cleanup an area until clean, then the AI goes idle.
//-------------------------------------------------------------------------------------------------
void CleanupHazardUpdate::setCleanupAreaParameters( const Coord3D *pos, Real range, Bool forgetGivenUp )
{
	Object *obj = getObject();
	AIUpdateInterface *ai = obj->getAI();

	//Setting the move range triggers that passive conditions for it to be allowed to move
	//from the specified position.
	m_moveRange = range;
	m_pos = *pos;
	m_resumeGuard = FALSE;
	m_returning = FALSE;
	m_approaches = 0;
	m_approaching = FALSE;
	// a player sending it on purpose has it try every hazard there again; the computer's own orders pick
	// round the ground given up on (closestHazard), and forgetting it here walked the next one back to it
	if( forgetGivenUp )
		m_hasGivenUp = FALSE;

	if( ai )
	{
		//CMD_FROM_AI important because it'll abort when other types are used -- like if a player
		//or script orders the unit to do something else, we need a way to cancel this passive
		//situation.
		ai->aiMoveToPosition( pos, CMD_FROM_AI ); 
	}
}

// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void CleanupHazardUpdate::crc( Xfer *xfer )
{

	// extend base class
	UpdateModule::crc( xfer );

}  // end crc

// ------------------------------------------------------------------------------------------------
/** Xfer method
	* Version Info:
	* 1: Initial version
	* 2: the guard a cleanup it started by itself gives back, the walk back, the drives toward a hazard
	* 3: the object that guard was on, the hazard given up on
	* 4: the ground given up on instead of the one hazard, and the ground it is after with how near it has come
	* 5: the frame it last made progress in the area, and the hazard it was after at the last scan */
// ------------------------------------------------------------------------------------------------
void CleanupHazardUpdate::xfer( Xfer *xfer )
{

	// version
	XferVersion currentVersion = 5;
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );

	// extend base class
	UpdateModule::xfer( xfer );

	// best target id
	xfer->xferObjectID( &m_bestTargetID );

	// in range
	xfer->xferBool( &m_inRange );

	// next scan frames
	xfer->xferInt( &m_nextScanFrames );

	// next shot available in frames
	xfer->xferInt( &m_nextShotAvailableInFrames );

	// don't need to save weapon template, it's retrieved onObjectCreated
	// const WeaponTemplate *m_weaponTemplate;

	// pos
	xfer->xferCoord3D( &m_pos );

	// move range
	xfer->xferReal( &m_moveRange );

	if( version >= 2 )
	{
		xfer->xferBool( &m_resumeGuard );
		xfer->xferInt( &m_resumeGuardMode );
		xfer->xferBool( &m_returning );
		xfer->xferInt( &m_approaches );
	}
	if( version >= 3 )
		xfer->xferObjectID( &m_resumeGuardObject );
	if( version == 3 )
	{
		ObjectID givenUpID = INVALID_ID;		// the one hazard version 3 remembered; its ground is not known
		xfer->xferObjectID( &givenUpID );
	}
	if( version >= 4 )
	{
		xfer->xferBool( &m_hasGivenUp );
		xfer->xferCoord3D( &m_givenUpPos );
		xfer->xferBool( &m_approaching );
		xfer->xferCoord3D( &m_approachPos );
		xfer->xferCoord3D( &m_lastScanPos );
		xfer->xferReal( &m_closestApproach );
	}
	if( version >= 5 )
	{
		xfer->xferUnsignedInt( &m_progressFrame );
		xfer->xferObjectID( &m_lastTargetID );
	}
	else
		m_progressFrame = TheGameLogic->getFrame();

}  // end xfer

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void CleanupHazardUpdate::loadPostProcess( void )
{

	// extend base class
	UpdateModule::loadPostProcess();

}  // end loadPostProcess
