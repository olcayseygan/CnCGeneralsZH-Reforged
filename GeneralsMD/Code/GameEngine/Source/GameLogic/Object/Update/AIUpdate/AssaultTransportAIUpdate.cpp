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

// AssaultTransportAIUpdate.cpp ////////////
// Author: Kris Morness, December 2002
// Desc:   State machine that allows assault transports (troop crawler) to deploy
//         troops, order them to attack, then return. Can do extra things like ordering
//         injured troops to return to the transport for healing purposes.

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "Common/Player.h"
#include "Common/ThingFactory.h"
#include "GameClient/Drawable.h"
#include "GameClient/InGameUI.h"
#include "GameLogic/ExperienceTracker.h"
#include "GameLogic/Module/BodyModule.h"
#include "GameLogic/Module/ContainModule.h"
#include "GameLogic/Module/AssaultTransportAIUpdate.h"
#include "GameLogic/Module/PhysicsUpdate.h"
#include "GameLogic/Object.h"
#include "GameLogic/PartitionManager.h"
#include "GameLogic/Weapon.h"
#include "Lib/FixBoundary.h"

#ifdef _INTERNAL
// for occasional debugging...
//#pragma optimize("", off)
//#pragma MESSAGE("************************************** WARNING, optimization disabled for debugging purposes")
#endif

//How long the transport waits for its troops to board again before it rolls on without them.
#define FRAMES_TO_WAIT_FOR_BOARDING (10 * LOGICFRAMES_PER_SECOND)

//-------------------------------------------------------------------------------------------------
/** Should the troops we deployed climb back into the transport?
  * Only once nobody is still shooting and there is nothing left near us worth shooting at.
  * Free function with no Object in its signature so a test can pin the rule - the same trick as
  * AIAttackMove_shouldRetaliate in AIStates.cpp. */
Bool AssaultTransport_shouldRetrieveMembers( Bool membersOutside, Bool membersFighting, Bool areaClear )
{
	return membersOutside && !membersFighting && areaClear;
}

//-------------------------------------------------------------------------------------------------
/** Is the transport standing there attacking with nobody left to put on the ground?
  * Its own weapon only deploys troops, so once it is empty the attack is theatre - drive on.
  * Not while the squad it dropped is still alive out there, though: that fight is the whole
  * point of having stopped, and driving off would leave them chasing the transport. */
Bool AssaultTransport_nothingLeftToDeploy( Bool isAttacking, Bool anyoneInside, Bool membersAlive )
{
	return isAttacking && !anyoneInside && !membersAlive;
}

//-------------------------------------------------------------------------------------------------
/** Does the transport hold still this frame instead of driving on with the attack move?
  * Only while troops are still outside and the boarding wait has not run out; see
  * AssaultTransport_shouldRetrieveMembers above. */
Bool AssaultTransport_waitingForBoarding( Bool membersOutside, UnsignedInt framesRemaining )
{
	return membersOutside && framesRemaining > 0;
}

//-------------------------------------------------------------------------------------------------
/** Did the boarding wait just run out on somebody who is still outside?
  * Asked before the countdown ticks, so 1 is the last frame of the wait. The wait used to be
  * re-armed the moment it hit zero - a man who could not path back in kept the transport standing
  * there for good, ignoring the attack move. */
Bool AssaultTransport_boardingWaitJustExpired( Bool membersOutside, UnsignedInt framesRemaining )
{
	return membersOutside && framesRemaining == 1;
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------

//-------------------------------------------------------------------------------------------------
AssaultTransportAIUpdate::AssaultTransportAIUpdate( Thing *thing, const ModuleData* moduleData ) : AIUpdateInterface( thing, moduleData )
{
	m_currentMembers = MAX_TRANSPORT_SLOTS; //First time, max it out, to ensure clearing arrays in reset.
	reset();
} 

//-------------------------------------------------------------------------------------------------
void AssaultTransportAIUpdate::reset()
{
	for( int i = 0; i < m_currentMembers; i++ )
	{
		m_memberIDs[ i ] = INVALID_ID;
	}
	m_currentMembers = 0;
  m_attackMoveGoalPos.zero();
  m_designatedTarget = INVALID_ID;
	m_state = IDLE;
	m_framesRemaining = 0;
	m_isAttackMove = FALSE;
	m_isAttackObject = FALSE;
}

//-------------------------------------------------------------------------------------------------
AssaultTransportAIUpdate::~AssaultTransportAIUpdate( void )
{
} 

//-------------------------------------------------------------------------------------------------
void AssaultTransportAIUpdate::aiDoCommand(const AICommandParms* parms)
{
	//Inspect the command and reset everything when necessary.
	if( parms->m_cmdSource != CMD_FROM_AI )
	{
		//Now the only time we care about anything is if we were ordered to attack something or attack move.
		switch( parms->m_cmd ) 
		{
			case AICMD_ATTACKMOVE_TO_POSITION:
				//Reset because we have been ordered to do something.
				reset();
				m_attackMoveGoalPos = parms->m_pos;
				m_isAttackMove = TRUE;
				break;
			case AICMD_ATTACK_OBJECT:
				//Reset because we have been ordered to do something.
				reset();
				//m_designatedTarget = parms->m_obj ? parms->m_obj->getID() : INVALID_ID;
				m_isAttackObject = TRUE;
				break;
			case AICMD_IDLE:
				m_designatedTarget = INVALID_ID;
				//Order all outside members back inside!
				retrieveMembers();
				reset();
				break;
			default:
				//Reset because we have been ordered to do something we're not handling.
				reset();
				break;
		}
	}

	//Note, in both cases, the transport will fire a dummy DEPLOY weapon that will trigger the 
	//evacuation of the troops.
	AIUpdateInterface::aiDoCommand( parms );
}

//-------------------------------------------------------------------------------------------------
void AssaultTransportAIUpdate::beginAssault( const Object *designatedTarget ) const
{
	//The transport has determined it is in range to begin the assault (via weapon system).
	//Now order the evacuation of healthy troops, and let the update handle moving them.
	if( designatedTarget )
	{
		m_designatedTarget = designatedTarget->getID();
	}
}

//-------------------------------------------------------------------------------------------------
Bool AssaultTransportAIUpdate::isIdle() const
{
	return AIUpdateInterface::isIdle();
}

//-------------------------------------------------------------------------------------------------
UpdateSleepTime calcSleepTime()
{
	return UPDATE_SLEEP_NONE;
}

//-------------------------------------------------------------------------------------------------
UpdateSleepTime AssaultTransportAIUpdate::update( void )
{
	Object *transport = getObject();
	//const AssaultTransportAIUpdateModuleData *data = getAssaultTransportAIUpdateModuleData();

	if( transport->isEffectivelyDead() )
	{
		giveFinalOrders();
		return UPDATE_SLEEP_FOREVER;
	}

	//First removing dead members or members that have been ordered to do something outside of this AI.
	if( m_currentMembers )
	{
		for( int i = 0; i < m_currentMembers; i++ )
		{
			Object *member = TheGameLogic->findObjectByID( m_memberIDs[ i ] );
			AIUpdateInterface *ai = member ? member->getAI() : NULL;
			if( !member || member->isEffectivelyDead() || ai->getLastCommandSource() != CMD_FROM_AI )
			{
				//Member is toast -- so remove him from our list!
				if( m_currentMembers - 1 > i )
				{
					//Move the last slot to this slot to keep array contiguous.
					m_memberIDs[ i ] = m_memberIDs[ m_currentMembers - 1 ];
				}
				else
				{
					//Just clean out last slot.
					m_memberIDs[ i ] = INVALID_ID;
				}
				if( ai )
				{
					//Important! Members of our assault transport must be allowed to chase down designated enemies.
					//Generally only player commands allow this, so this flag allows AI commands to do the same.
					//We need to turn this off though, because this ex-member is no longer under transport control.
					ai->setAllowedToChase( FALSE );
				}
				m_currentMembers--;
			}
		}
	}

	//Now add any potentially new members to the group.
	ContainModuleInterface *contain = transport->getContain();
	if( contain )
	{
		const ContainedItemsList *passengerList = contain->getContainedItemsList();
		ContainedItemsList::const_iterator passengerIterator;
		passengerIterator = passengerList->begin();
		while( passengerIterator != passengerList->end() )
		{
			Object *passenger = *passengerIterator;
			//Advance to the next iterator
			passengerIterator++;

			//Make sure it isn't in our list already.
			Bool found = FALSE;
			for( int i = 0; i < m_currentMembers; i++ )
			{
				if( passenger->getID() == m_memberIDs[ i ] )
				{
					//He is in the list... so skip him.
					found = TRUE;
					break;
				}
			}
			if( found )
			{
				//Get next passenger.
				continue;
			}

			//It's possible to add members manually -- but if we already have 10 members, then wait!
			if( m_currentMembers < MAX_TRANSPORT_SLOTS )
			{
				//Not in list, so add him!
				m_memberIDs[ m_currentMembers ] = passenger->getID();
				if( passenger->getAI() )
				{
					//Important! Members of our assault transport must be allowed to chase down designated enemies.
					//Generally only player commands allow this, so this flag allows AI commands to do the same.
					passenger->getAI()->setAllowedToChase( TRUE );
				}

				m_currentMembers++;
			}
		}
	}

	//Nobody left to put on the ground: stop posing with the deploy weapon and get on with the order.
	//m_currentMembers only counts our living troops, so with the hold empty they are all outside.
	if( AssaultTransport_nothingLeftToDeploy( transport->testStatus( OBJECT_STATUS_IS_ATTACKING ),
																						contain && contain->getContainCount() > 0,
																						m_currentMembers > 0 ) )
	{
		if( m_isAttackMove && getAIStateType() != AI_ATTACK_MOVE_TO )
		{
			aiAttackMoveToPosition( &m_attackMoveGoalPos, NO_MAX_SHOTS_LIMIT, CMD_FROM_AI );
		}
		else if( !m_isAttackMove )
		{
			aiIdle( CMD_FROM_AI );
		}
		return UPDATE_SLEEP_NONE;
	}

	//Keep track of the average position of all combat units assigned to me.
	FCoord3D fighterCentroidPos;
	UnsignedInt fightingMembers = 0;
	fighterCentroidPos.zero();

	//If we're already in the process, reacquire the designated target again... see if
	//it's still alive.
	Object *designatedTarget = TheGameLogic->findObjectByID( m_designatedTarget );
	if( designatedTarget && designatedTarget->isEffectivelyDead() )
	{
		designatedTarget = NULL;
	}
	if( designatedTarget )
	{
		//Look for members not currently attacking this target.
		for( int i = 0; i < m_currentMembers; i++ )
		{
			Object *member = TheGameLogic->findObjectByID( m_memberIDs[ i ] );
			AIUpdateInterface *ai = member ? member->getAI() : NULL;
			
			if( member && ai )
			{
				if( member->isContained() )
				{
					//Everyone rides out to fight, however scratched up he is. Retail kept anyone
					//short of full health inside and sent the wounded back in to be patched up.
					ai->aiExit( transport, CMD_FROM_AI );
				}
				else
				{
					//Increment the number of fighters and their position.
					fighterCentroidPos.add( *member->getPositionFix() );
					fightingMembers++;

					if( !ai->isMoving() )
					{
						if( ai->getGoalObject() != designatedTarget )
						{
							//Okay, this dude is outside and waiting... order him to attack the designated target
							ai->aiAttackObject( designatedTarget, NO_MAX_SHOTS_LIMIT, CMD_FROM_AI );
						}
					}
				}
			}
		}
	}
	else
	{
		//The target we deployed for is gone. Find out whether the troops we let out still have
		//something to do - the same rule for both orders, so they no longer climb back in and
		//straight back out once per dead enemy.
		Bool membersOutside = FALSE;
		Bool membersFighting = FALSE;
		for( int i = 0; i < m_currentMembers; i++ )
		{
			Object *member = TheGameLogic->findObjectByID( m_memberIDs[ i ] );
			AIUpdateInterface *ai = member ? member->getAI() : NULL;
			if( member && ai && !member->isContained() )
			{
				membersOutside = TRUE;
				if( ai->getCurrentVictim() != NULL )
				{
					membersFighting = TRUE;
				}
			}
		}

		//Only worth a partition scan once the troops are out and nobody is engaged.
		Bool areaClear = FALSE;
		if( membersOutside && !membersFighting )
		{
			areaClear = isAssaultAreaClear();
		}
		Bool retrieve = AssaultTransport_shouldRetrieveMembers( membersOutside, membersFighting, areaClear );

		if( m_isAttackMove )
		{
			if( retrieve )
			{
				//Nobody left to shoot at, so pick the troops back up instead of walking them along
				//behind the transport for the rest of the attack move.
				retrieveMembers();
				if( !m_framesRemaining )
				{
					m_framesRemaining = FRAMES_TO_WAIT_FOR_BOARDING;
				}
			}

			if( AssaultTransport_waitingForBoarding( membersOutside, m_framesRemaining ) )
			{
				//Hold here while they climb back in - driving off would just make them chase us.
				//The wait is bounded so a member that cannot make it back never stalls the attack move.
				if( getAIStateType() == AI_ATTACK_MOVE_TO )
				{
					aiIdle( CMD_FROM_AI );
				}
				if( AssaultTransport_boardingWaitJustExpired( membersOutside, m_framesRemaining ) )
				{
					//He is never getting back in. Hand him the order himself instead of arming the
					//wait again next frame, which held the transport still for good.
					releaseStragglers();
				}
				m_framesRemaining--;
			}
			else
			{
				m_framesRemaining = 0;
				if( getAIStateType() != AI_ATTACK_MOVE_TO )
				{
					//Continue to move towards the attackmove area.
					aiAttackMoveToPosition( &m_attackMoveGoalPos, NO_MAX_SHOTS_LIMIT, CMD_FROM_AI );
				}
			}
		}
		else if( m_isAttackObject && retrieve )
		{
			//Ordered onto one target: stay out and finish off whatever else is around us first.
			retrieveMembers();
		}
	}

	/*
	//Keep near the troops.
	if( !m_framesRemaining ) 
	{
		if( !isMoving() && fightingMembers && designatedTarget )
		{
			m_framesRemaining = 45;

			//Get centriod pos now that we know the number of fighting members.
			Real scale = 1.0f / (Real)fightingMembers;
			fighterCentroidPos.scale( scale );

			Coord3D designatedTargetPos = *designatedTarget->getPosition();

			//Calculate a vector from the target passed the fighters to be at a safe place 
			//to be as a transport.
			Coord3D vector;
			vector.set( &fighterCentroidPos );
			vector.sub( &designatedTargetPos );
			vector.normalize();
			vector.scale( 150.0f );

			Coord3D transportGoalPos;
			transportGoalPos.set( &designatedTargetPos );
			transportGoalPos.add( &vector );

			Real distanceSqrd = ThePartitionManager->getDistanceSquared( transport, &transportGoalPos, FROM_CENTER_2D );
			if( distanceSqrd > 40.0f * 40.0f )
			{
				//Order the transport to move to the safer position
				//aiMoveToPosition( &transportGoalPos, CMD_FROM_AI );
			}
		}
	}
	else
	{
		m_framesRemaining--;
	}
	if( designatedTarget && !isMoving() )
	{
		//Order the transport to face the designated target!
		//aiFaceObject( designatedTarget, CMD_FROM_AI );
	}
	*/
	
	/*UpdateSleepTime ret =*/ AIUpdateInterface::update();
	//return (mine < ret) ? mine : ret;
	/// @todo srj -- someday, make sleepy. for now, must not sleep.
	return UPDATE_SLEEP_NONE;
}

//-------------------------------------------------------------------------------------------------
/** How far around a unit do we look for someone still worth fighting?
  * At least as far as that unit can shoot. The INI's ClearRangeRequiredToContinueAttackMove is
  * shorter than an infantry rifle, so the men used to kill the one enemy they were deployed for
  * and climb straight back in with the next one standing in plain sight and well in range. */
Real AssaultTransport_clearScanRange( Real iniRange, Real weaponRange )
{
	return weaponRange > iniRange ? weaponRange : iniRange;
}

//-------------------------------------------------------------------------------------------------
/** Anything alive and hostile to the transport within range of this object? */
static Bool areaClearAround( const Object *around, const Object *transport, Real range )
{
	PartitionFilterRelationship		filterRelationship( transport, PartitionFilterRelationship::ALLOW_ENEMIES );
	PartitionFilterAlive					filterAlive;
	PartitionFilterSameMapStatus	filterMapStatus( transport );
	PartitionFilter *filters[] = { &filterRelationship, &filterAlive, &filterMapStatus, NULL };

	// P6: the weapon range is still float
	return ThePartitionManager->getClosestObjectFix( around, fixFromReal( AssaultTransport_clearScanRange( range, around->getLargestWeaponRange() ) ), FROM_CENTER_2D, filters ) == NULL;
}

//-------------------------------------------------------------------------------------------------
/** Is there anything left worth fighting? ClearRangeRequiredToContinueAttackMove is the INI knob
  * EA declared for exactly this and never read.
  * Asked around the deployed men as well as the transport: they are allowed to chase, so the fight
  * routinely ends up outside the transport's own bubble, and asking only there boarded them with an
  * enemy still standing in front of them. */
Bool AssaultTransportAIUpdate::isAssaultAreaClear() const
{
	const AssaultTransportAIUpdateModuleData *data = getAssaultTransportAIUpdateModuleData();
	const Object *transport = getObject();
	Real range = fixToReal( data->m_clearRangeRequiredToContinueAttackMove );	// P6: it is maxed against the float weapon range

	if( !areaClearAround( transport, transport, range ) )
	{
		return FALSE;
	}

	for( int i = 0; i < m_currentMembers; i++ )
	{
		const Object *member = TheGameLogic->findObjectByID( m_memberIDs[ i ] );
		if( member && !member->isContained() && !areaClearAround( member, transport, range ) )
		{
			return FALSE;
		}
	}
	return TRUE;
}

//-------------------------------------------------------------------------------------------------
void AssaultTransportAIUpdate::retrieveMembers()
{
	//Order all outside members back inside!
	for( int i = 0; i < m_currentMembers; i++ )
	{
		Object *member = TheGameLogic->findObjectByID( m_memberIDs[ i ] );
		AIUpdateInterface *ai = member ? member->getAI() : NULL;
		if( member && ai )
		{
			Bool contained = member->isContained();
			if( !contained )
			{
				//This contained member is healthy so order him to exit to start fighting!
				if (ai->getAIStateType() != AI_ENTER) {
					ai->aiEnter( getObject(), CMD_FROM_AI );
				} 
			}
		}
	}
}

//-------------------------------------------------------------------------------------------------
/** The boarding wait ran out with men still on the ground. Give them the transport's own order so
  * they walk it themselves; a player-sourced order also drops them from the member list on the next
  * update, which is what lets the transport get on with the attack move. */
void AssaultTransportAIUpdate::releaseStragglers()
{
	for( int i = 0; i < m_currentMembers; i++ )
	{
		Object *member = TheGameLogic->findObjectByID( m_memberIDs[ i ] );
		AIUpdateInterface *ai = member ? member->getAI() : NULL;
		if( member && ai && !member->isContained() )
		{
			ai->aiAttackMoveToPosition( &m_attackMoveGoalPos, NO_MAX_SHOTS_LIMIT, CMD_FROM_PLAYER );
		}
	}
}

//-------------------------------------------------------------------------------------------------
void AssaultTransportAIUpdate::giveFinalOrders()
{
	//All members have been ejected outside already -- transfer the original order to the troops
	for( int i = 0; i < m_currentMembers; i++ )
	{
		Object *member = TheGameLogic->findObjectByID( m_memberIDs[ i ] );
		AIUpdateInterface *ai = member ? member->getAI() : NULL;
		if( member && ai )
		{
			Object *designatedTarget = TheGameLogic->findObjectByID( m_designatedTarget );

			if( m_isAttackObject && designatedTarget )
			{
				ai->aiAttackObject( designatedTarget, NO_MAX_SHOTS_LIMIT, CMD_FROM_PLAYER );
			}
			else if( m_isAttackMove )
			{
				ai->aiAttackMoveToPosition( &m_attackMoveGoalPos, NO_MAX_SHOTS_LIMIT, CMD_FROM_PLAYER );
			}

			ai->setAllowedToChase( FALSE );
		}
	}
}

//-------------------------------------------------------------------------------------------------
/** CRC */
//-------------------------------------------------------------------------------------------------
void AssaultTransportAIUpdate::crc( Xfer *xfer )
{
	// extend base class
	AIUpdateInterface::crc(xfer);
}  // end crc

//-------------------------------------------------------------------------------------------------
/** Xfer method
	* Version Info:
	* 1: Initial version */
//-------------------------------------------------------------------------------------------------
void AssaultTransportAIUpdate::xfer( Xfer *xfer )
{
  // version
  XferVersion currentVersion = 1;
  XferVersion version = currentVersion;
  xfer->xferVersion( &version, currentVersion );
 
 // extend base class
	AIUpdateInterface::xfer(xfer);

	xfer->xferInt( &m_currentMembers );

	for( int i = 0; i < m_currentMembers; i++ )
	{
		xfer->xferObjectID( &(m_memberIDs[ i ]) );
		Bool obsoleteHealingFlag = FALSE;		// members no longer go back in to heal; keeps the format
		xfer->xferBool( &obsoleteHealingFlag );
	}

	xfer->xferCoord3D( &m_attackMoveGoalPos );
	xfer->xferObjectID( &m_designatedTarget );
	
	Int state = (Int)m_state;
	xfer->xferInt( &state );
	m_state = (AssaultStateTypes)state;
	
	xfer->xferUnsignedInt( &m_framesRemaining );
	xfer->xferBool( &m_isAttackMove );
	xfer->xferBool( &m_isAttackObject );

}  // end xfer

//-------------------------------------------------------------------------------------------------
/** Load post process */
//-------------------------------------------------------------------------------------------------
void AssaultTransportAIUpdate::loadPostProcess( void )
{
 // extend base class
	AIUpdateInterface::loadPostProcess();
}  // end loadPostProcess

