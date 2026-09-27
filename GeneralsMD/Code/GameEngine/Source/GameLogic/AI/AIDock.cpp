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

// AIDock.cpp
// Implementation of docking behavior
// Author: Michael S. Booth, February 2002
#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine


#include "Common/Module.h"
#include "Common/Player.h"
#include "GameLogic/Object.h"
#include "GameLogic/AIDock.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/SupplyTruckAIUpdate.h"
#include "GameLogic/Module/UpdateModule.h"

//----------------------------------------------------------------------------------------------------------
/**
 * Create an AI state machine. Define all of the states the machine 
 * can possibly be in, and set the initial (default) state.
 */
AIDockMachine::AIDockMachine( Object *obj ) : StateMachine( obj, "AIDockMachine" )
{
	static const StateConditionInfo waitForClearanceConditions[] = 
	{
		StateConditionInfo(ableToAdvance, AI_DOCK_ADVANCE_POSITION, NULL),
		StateConditionInfo(NULL, NULL, NULL)	// keep last
	};

	// order matters: first state is the default state.
	defineState( AI_DOCK_APPROACH,						newInstance(AIDockApproachState)( this ), AI_DOCK_WAIT_FOR_CLEARANCE, EXIT_MACHINE_WITH_FAILURE );
	defineState( AI_DOCK_WAIT_FOR_CLEARANCE,	newInstance(AIDockWaitForClearanceState)( this ), AI_DOCK_MOVE_TO_ENTRY, EXIT_MACHINE_WITH_FAILURE, waitForClearanceConditions );
	defineState( AI_DOCK_ADVANCE_POSITION,		newInstance(AIDockAdvancePositionState)( this ), AI_DOCK_WAIT_FOR_CLEARANCE, EXIT_MACHINE_WITH_FAILURE );
	defineState( AI_DOCK_MOVE_TO_ENTRY,				newInstance(AIDockMoveToEntryState)( this ), AI_DOCK_MOVE_TO_DOCK, AI_DOCK_MOVE_TO_EXIT );
	defineState( AI_DOCK_MOVE_TO_DOCK,				newInstance(AIDockMoveToDockState)( this ), AI_DOCK_PROCESS_DOCK, AI_DOCK_MOVE_TO_EXIT );
	defineState( AI_DOCK_PROCESS_DOCK,				newInstance(AIDockProcessDockState)( this ), AI_DOCK_MOVE_TO_EXIT, AI_DOCK_MOVE_TO_EXIT );
	defineState( AI_DOCK_MOVE_TO_EXIT,				newInstance(AIDockMoveToExitState)( this ), AI_DOCK_MOVE_TO_RALLY, EXIT_MACHINE_WITH_FAILURE );
	defineState( AI_DOCK_MOVE_TO_RALLY,				newInstance(AIDockMoveToRallyState)( this ), EXIT_MACHINE_WITH_SUCCESS, EXIT_MACHINE_WITH_FAILURE );

	m_approachPosition = -1;
}

AIDockMachine::~AIDockMachine()
{
}

//-----------------------------------------------------------------------------
void AIDockMachine::halt() 
{ 
	Object *goalObject = getGoalObject();
		
	// sanity
	if( goalObject != NULL )
	{
		// get dock update interface
		DockUpdateInterface *dock = goalObject->getDockUpdateInterface();

		// We need to say goodbye, or we will leave our spot taken forever.
		if( dock != NULL )
			dock->cancelDock( getOwner() );
	}

	StateMachine::halt();
}


// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void AIDockMachine::crc( Xfer *xfer )
{
	StateMachine::crc(xfer);
}  // end crc

// ------------------------------------------------------------------------------------------------
/** Xfer Method */
// ------------------------------------------------------------------------------------------------
void AIDockMachine::xfer( Xfer *xfer )
{
	XferVersion cv = 1;	
	XferVersion v = cv; 
	xfer->xferVersion( &v, cv );

	StateMachine::xfer(xfer);
	xfer->xferInt(&m_approachPosition);
}  // end xfer

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void AIDockMachine::loadPostProcess( void )
{
	StateMachine::loadPostProcess();
}  // end loadPostProcess

// State transition conditions ----------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
/* static */ Bool AIDockMachine::ableToAdvance( State *thisState, void* userData )
{
	Object *goalObject = thisState->getMachineGoalObject();
	AIDockMachine *myMachine = (AIDockMachine *)thisState->getMachine();

	if( goalObject == NULL )
		return FALSE;

	DockUpdateInterface *dock = goalObject->getDockUpdateInterface();

	// if we have nothing to dock with, fail
	if( dock == NULL )
		return FALSE;

	// if the dock says we can advance, then sidetrack to the scoot forward state
	if( dock->isClearToAdvance( thisState->getMachineOwner(), myMachine->m_approachPosition ) )
		return TRUE;

	// continue to wait
	return FALSE;
}


//----------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------

// ------------------------------------------------------------------------------------------------
/** Xfer Method */
// ------------------------------------------------------------------------------------------------
void AIDockApproachState::xfer( Xfer *xfer )
{
  // version
  XferVersion currentVersion = 2;
  XferVersion version = currentVersion;
  xfer->xferVersion( &version, currentVersion );

	if (version>=2) {
		AIInternalMoveToState::xfer(xfer);
	}

}  // end xfer

//----------------------------------------------------------------------------------------------
/**
 * Approach our waiting spot next to the dock.
 */
StateReturnType AIDockApproachState::onEnter( void )
{
	Object *goalObject = getMachineGoalObject();
		
	// sanity
	if( goalObject == NULL )
		return STATE_FAILURE;

	// get dock update interface
	DockUpdateInterface *dock = goalObject->getDockUpdateInterface();

	// if we have nothing to dock with, fail
	if (dock == NULL)
		return STATE_FAILURE;

	// fail if the dock is closed
	if( dock->isDockOpen() == FALSE )
	{
		dock->cancelDock( getMachineOwner() );
		return STATE_FAILURE;
	}

	// get a good place to wait from the dock
	Bool reserved = dock->reserveApproachPosition( getMachineOwner(), &m_goalPosition, &(( (AIDockMachine*)getMachine() )->m_approachPosition) );
	if( reserved == FALSE )
	{
		// dock is full
		return STATE_FAILURE;
	}

	AIUpdateInterface *ai = getMachineOwner()->getAIUpdateInterface();
	if (ai) {
		ai->ignoreObstacle( NULL );
	}

	//
	// Nobody ahead of us and nobody inside: drive straight in.
	//
	// The approach position is a queue slot, and the machine treats it as a destination - a move
	// order that ends by stopping there, then a wait, then a second move to the entry, then a third
	// to the pad.  With a queue that is the whole point.  With an empty dock, which is what a worker
	// meets on nearly every trip it makes, it is a full stop in open ground outside the building
	// followed by a standing start, twice a load: the hesitation at the supply pile and again at the
	// centre.  Taking the slot and reporting it reached without driving to it hands the dock to the
	// next state a frame later; the queue behind is unaffected, because there is nobody in it.
	//
	// Only for a dock with an entry point of its own.  One without the bones - a supply pile, a GLA
	// stash - names wherever the docker stands as its entry and its pad, so the approach walk is the
	// only thing that brings the docker to it; skip that and the load is taken where the worker was.
	//
	Coord3D enterPosition;
	dock->getEnterPosition( getMachineOwner(), &enterPosition );
	if( dock->getActiveDocker() == INVALID_ID
			&& ( (AIDockMachine*)getMachine() )->m_approachPosition == 0
			&& !( enterPosition == getMachineOwner()->getPositionFix()->toCoord3D() ) )	// P4: the dock's positions are float
	{
		dock->onApproachReached( getMachineOwner() );
		return STATE_SUCCESS;
	}

	// this behavior is an extention of basic MoveTo
	return AIInternalMoveToState::onEnter();
}

//----------------------------------------------------------------------------------------------
StateReturnType AIDockApproachState::update( void )
{
	Object *goalObject = getMachineGoalObject();

	// if we have nothing to dock with, fail
	if (goalObject == NULL)
		return STATE_FAILURE;

	// this behavior is an extention of basic MoveTo
	return AIInternalMoveToState::update();
}

//----------------------------------------------------------------------------------------------
void AIDockApproachState::onExit( StateExitType status )
{
	Object *goalObject = getMachineGoalObject();

	DockUpdateInterface *dock = NULL;
	if( goalObject )
		dock = goalObject->getDockUpdateInterface();

	// tell the dock we have approached
	if (dock)
	{
		// if we were interrupted, let the dock know we're not coming
		if (status == EXIT_RESET || dock->isDockOpen() == FALSE)
			dock->cancelDock( getMachineOwner() );
		else
			dock->onApproachReached( getMachineOwner() );
	}

	// this behavior is an extention of basic MoveTo
	AIInternalMoveToState::onExit( status );
}

//----------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------
/**
 * We have approached, now wait at our queue position until the dock says we can enter.
 */
StateReturnType AIDockWaitForClearanceState::onEnter( void )
{
	m_enterFrame = TheGameLogic->getFrame();
	return STATE_CONTINUE;
}

/**
 * We have approached, now wait at our queue position until the dock says we can enter.
 * @todo What if we are pushed off of our queue spot? We need to move back on... (MSB)
 */
StateReturnType AIDockWaitForClearanceState::update( void )
{
	Object *goalObject = getMachineGoalObject();

	if( goalObject == NULL )
		return STATE_FAILURE;

	DockUpdateInterface *dock = goalObject->getDockUpdateInterface();

	// if we have nothing to dock with, fail
	if (dock == NULL)
		return STATE_FAILURE;

	// fail if the dock is closed
	if( dock->isDockOpen() == FALSE )
	{
		dock->cancelDock( getMachineOwner() );
		return STATE_FAILURE;
	}

	// if the dock says we can enter, our wait is over
	if (dock->isClearToEnter( getMachineOwner() ))
		return STATE_SUCCESS;

	if (m_enterFrame + 30*LOGICFRAMES_PER_SECOND < TheGameLogic->getFrame()) {
		return STATE_FAILURE;
	}
	// continue to wait
	return STATE_CONTINUE;
}

//----------------------------------------------------------------------------------------------
void AIDockWaitForClearanceState::onExit( StateExitType status )
{
	Object *goalObject = getMachineGoalObject();

	DockUpdateInterface *dock = NULL;
	if( goalObject )
		dock = goalObject->getDockUpdateInterface();

	// if we were interrupted, let the dock know we're not coming
	if (dock && (dock->isDockOpen() == FALSE || status == EXIT_RESET))
		dock->cancelDock( getMachineOwner() );
}

//----------------------------------------------------------------------------------------------
void AIDockWaitForClearanceState::xfer(Xfer *xfer ) 
{
	XferVersion cv = 2;	
	XferVersion v = cv; 
	xfer->xferVersion( &v, cv );
	if (v >= 2) {
		xfer->xferUnsignedInt(&m_enterFrame);
	} else {
		m_enterFrame = TheGameLogic->getFrame();
	}
}
//----------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------

//----------------------------------------------------------------------------------------------
/**
 * Advance to our next waiting spot next to the dock.
 */
StateReturnType AIDockAdvancePositionState::onEnter( void )
{
	Object *goalObject = getMachineGoalObject();
		
	// sanity
	if( goalObject == NULL )
		return STATE_FAILURE;

	// get dock update interface
	DockUpdateInterface *dock = goalObject->getDockUpdateInterface();

	// if we have nothing to dock with, fail
	if (dock == NULL)
		return STATE_FAILURE;

	// fail if the dock is closed
	if( dock->isDockOpen() == FALSE )
	{
		dock->cancelDock( getMachineOwner() );
		return STATE_FAILURE;
	}

	// get a good place to wait from the dock
	Bool reserved = dock->advanceApproachPosition( getMachineOwner(), &m_goalPosition, &(( (AIDockMachine*)getMachine() )->m_approachPosition) );
	if( reserved == FALSE )
	{
		// dock is full
		return STATE_FAILURE;
	}

	AIUpdateInterface *ai = getMachineOwner()->getAIUpdateInterface();
	if (ai) {
		ai->ignoreObstacle( NULL );
	}
	// this behavior is an extention of basic MoveTo
	return AIInternalMoveToState::onEnter();
}

//----------------------------------------------------------------------------------------------
StateReturnType AIDockAdvancePositionState::update( void )
{
	Object *goalObject = getMachineGoalObject();

	// if we have nothing to dock with, fail
	if (goalObject == NULL)
		return STATE_FAILURE;

	// this behavior is an extention of basic MoveTo
	return AIInternalMoveToState::update();
}

//----------------------------------------------------------------------------------------------
void AIDockAdvancePositionState::onExit( StateExitType status )
{
	Object *goalObject = getMachineGoalObject();

	DockUpdateInterface *dock = NULL;
	if( goalObject )
		dock = goalObject->getDockUpdateInterface();

	// tell the dock we have approached
	if (dock)
	{
		// if we were interrupted, let the dock know we're not coming
		if (status == EXIT_RESET || dock->isDockOpen() == FALSE)
			dock->cancelDock( getMachineOwner() );
		else
			dock->onApproachReached( getMachineOwner() );
	}

	// this behavior is an extention of basic MoveTo
	AIInternalMoveToState::onExit( status );
}

//----------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------

//----------------------------------------------------------------------------------------------
/**
 * Move to the dock's entry position.
 */
StateReturnType AIDockMoveToEntryState::onEnter( void )
{
	Object *goalObject = getMachineGoalObject();

  DockUpdateInterface *dock = NULL;
	if( goalObject )
		dock = goalObject->getDockUpdateInterface();

	// if we have nothing to dock with, fail
	if (dock == NULL)
		return STATE_FAILURE;

	// fail if the dock is closed
	if( dock->isDockOpen() == FALSE )
	{
		dock->cancelDock( getMachineOwner() );
		return STATE_FAILURE;
	}

	AIUpdateInterface *ai = getMachineOwner()->getAIUpdateInterface();
	if( ai  &&  dock->isAllowPassthroughType() ) 
	{
		ai->ignoreObstacle( getMachineGoalObject() );
	}

	// get the enter position and set as our goal position
	dock->getEnterPosition( getMachineOwner(), &m_goalPosition );

	( (AIDockMachine*)getMachine() )->m_approachPosition = -1;

	// this behavior is an extention of basic MoveTo
	return AIInternalMoveToState::onEnter();
}

//----------------------------------------------------------------------------------------------
StateReturnType AIDockMoveToEntryState::update( void )
{
	// if we have nothing to dock with, fail
	if (getMachineGoalObject() == NULL)
		return STATE_FAILURE;

	// this behavior is an extention of basic MoveTo
	return AIInternalMoveToState::update();
}

//----------------------------------------------------------------------------------------------
void AIDockMoveToEntryState::onExit( StateExitType status )
{
	Object *goalObject = getMachineGoalObject();

	DockUpdateInterface *dock = NULL;
	if( goalObject )
		dock = goalObject->getDockUpdateInterface();

	if (dock)
	{
		if (dock->isDockOpen() == FALSE || status == EXIT_RESET)
		{
			// if we were interrupted, let the dock know we're not coming
			dock->cancelDock( getMachineOwner() );
		}
		else
		{
			// tell the dock we are at the entrance
			dock->onEnterReached( getMachineOwner() );
		}
	}

	// this behavior is an extention of basic MoveTo
	AIInternalMoveToState::onExit( status );
}

//----------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------

//----------------------------------------------------------------------------------------------
/**
 * Move to the dock's docking position.
 */
StateReturnType AIDockMoveToDockState::onEnter( void )
{
	Object *goalObject = getMachineGoalObject();

	DockUpdateInterface *dock = NULL;
	if( goalObject )
		dock = goalObject->getDockUpdateInterface();

	// if we have nothing to dock with, fail
	if (dock == NULL)
		return STATE_FAILURE;

	// fail if the dock is closed
	if( dock->isDockOpen() == FALSE )
	{
		dock->cancelDock( getMachineOwner() );
		return STATE_FAILURE;
	}

	// get the docking position
	dock->getDockPosition( getMachineOwner(), &m_goalPosition );

	AIUpdateInterface *ai = getMachineOwner()->getAIUpdateInterface();
	if( ai  &&  dock->isAllowPassthroughType() ) 
	{
		ai->ignoreObstacle( getMachineGoalObject() );
		setAdjustsDestination(false);
	}

	// since we are moving inside the dock, disallow interruptions
	getMachine()->lock("AIDockMoveToDockState::onEnter");

	// this behavior is an extention of basic MoveTo
	return AIInternalMoveToState::onEnter();
}

//----------------------------------------------------------------------------------------------
StateReturnType AIDockMoveToDockState::update( void )
{
	Object *goalObject = getMachineGoalObject();

	// if we have nothing to dock with, fail
	if (goalObject == NULL)
		return STATE_FAILURE;

	DockUpdateInterface *dock = goalObject->getDockUpdateInterface();
	if( dock->isDockOpen() == FALSE )
		return STATE_FAILURE;

	// this behavior is an extention of basic MoveTo
	return AIInternalMoveToState::update();
}

//----------------------------------------------------------------------------------------------
void AIDockMoveToDockState::onExit( StateExitType status )
{
	Object *goalObject = getMachineGoalObject();

	DockUpdateInterface *dock = NULL;
	if( goalObject )
		dock = goalObject->getDockUpdateInterface();

	// tell the dock we are at the docking point
	if (dock)
	{

		// if we were interrupted, let the dock know we're not coming
		if (status == EXIT_RESET || dock->isDockOpen() == FALSE )
			dock->cancelDock( getMachineOwner() );
		else
			dock->onDockReached( getMachineOwner() );

	}

	// unlock the machine
	getMachine()->unlock();

	// this behavior is an extention of basic MoveTo
	AIInternalMoveToState::onExit( status );
}

//----------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------
AIDockProcessDockState::AIDockProcessDockState( StateMachine *machine ) : State( machine, "AIDockProcessDockState" )
{
	m_nextDockActionFrame = 0;
}

//----------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------
void AIDockProcessDockState::setNextDockActionFrame()
{
	// If we have a SupplyTruck Interface, then we will ask for our specific delay time
	SupplyTruckAIInterface *supplyTruck = getMachineOwner()->getAI()->getSupplyTruckAIInterface();
	if( supplyTruck )
	{
		UnsignedInt delay = supplyTruck->getActionDelayForDock( getMachineGoalObject() );
		m_nextDockActionFrame = TheGameLogic->getFrame() + delay;
		// ... and let the docker publish that window, so a bar can fill over it while it works
		supplyTruck->noteDockActionWindow( delay );
		return;
	}

	// The default is that it is simply okay to Action right away
	m_nextDockActionFrame = TheGameLogic->getFrame();
}

//----------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------
StateReturnType AIDockProcessDockState::onEnter( void )
{
	Object *goalObject = getMachineGoalObject();

	DockUpdateInterface *dock = NULL;
	if( goalObject )
		dock = goalObject->getDockUpdateInterface();

	// if we have nothing to dock with, fail
	if (dock == NULL)
		return STATE_FAILURE;

	setNextDockActionFrame();

	return STATE_CONTINUE;
}

//----------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------
/**
 * We are now docked. Invoke the dock's action() method until it returns false.
 */
StateReturnType AIDockProcessDockState::update( void )
{
	Object *goalObject = getMachineGoalObject();

	DockUpdateInterface *dock = NULL;
	if( goalObject )
		dock = goalObject->getDockUpdateInterface();

	// if we have nothing to dock with, fail
	if (dock == NULL)
		return STATE_FAILURE;

	// Some dockers can have a delay built in
	if( TheGameLogic->getFrame() < m_nextDockActionFrame )
		return STATE_CONTINUE;

	Object *drone = findMyDrone();

	// invoke the dock's action until it tells us it is done or the dock becomes closed
	if( dock->isDockOpen() == false || dock->action( getMachineOwner(), drone ) == false )
		return STATE_SUCCESS;

	//
	// Only now, with one more transfer actually going to happen, does the next window open. It
	// used to be opened before the action, so the very transfer that ended the docking restarted
	// the bar over the worker's head and then walked off: the worker left the moment its own
	// progress bar reset to nothing, which is what "it leaves before it has finished" looks like.
	//
	setNextDockActionFrame();

	return STATE_CONTINUE;
}

//----------------------------------------------------------------------------------------------
struct DroneInfo
{
	Object *owner;
	Object *drone;
	Bool found;
};

void findDrone( Object *obj, void *droneInfo )
{
	DroneInfo *dInfo = (DroneInfo*)droneInfo;
	
	if( !dInfo->found && obj )
	{
		if( obj->isKindOf( KINDOF_DRONE ) && obj->getProducerID() == dInfo->owner->getID() )
		{
			dInfo->found = TRUE;
			dInfo->drone = obj;
		}
	}
}

//----------------------------------------------------------------------------------------------
Object* AIDockProcessDockState::findMyDrone()
{
	//First do the fast cached check.
	Object *drone = TheGameLogic->findObjectByID( m_droneID );
	if( drone )
	{
		return drone;
	}

	//Nope... look for a drone (perhaps we just finished building one after docking?)
	Object *self = getMachineOwner();
	Player *player = self->getControllingPlayer();
	DroneInfo dInfo;
	dInfo.found = FALSE;
	dInfo.drone = NULL;
	dInfo.owner = self;

	//Iterate the objects in search for a drone with a producer ID of me.
	if( player )
	{
		player->iterateObjects( findDrone, &dInfo );
	}

	//If we found a drone, store it's ID as cached.
	if( dInfo.drone )
	{
		m_droneID = dInfo.drone->getID();
	}
	return dInfo.drone;
}

//----------------------------------------------------------------------------------------------
void AIDockProcessDockState::onExit( StateExitType status )
{
	// unlock the machine
	getMachine()->unlock();
}

//----------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------

//----------------------------------------------------------------------------------------------
/**
 * Move to the dock's exit position.
 */
StateReturnType AIDockMoveToExitState::onEnter( void )
{
	Object *goalObject = getMachineGoalObject();

	DockUpdateInterface *dock = NULL;
	if( goalObject )
		dock = goalObject->getDockUpdateInterface();

	// if we have nothing to dock with, fail
	if (dock == NULL)
		return STATE_FAILURE;

	// get the exit position
	dock->getExitPosition( getMachineOwner(), &m_goalPosition );

	AIUpdateInterface *ai = getMachineOwner()->getAIUpdateInterface();
	if( ai  &&  dock->isAllowPassthroughType() ) 
	{
		ai->ignoreObstacle( getMachineGoalObject() );
		setAdjustsDestination(false);
	}

	// this behavior is an extention of basic MoveTo
	return AIInternalMoveToState::onEnter();
}

//----------------------------------------------------------------------------------------------
StateReturnType AIDockMoveToExitState::update( void )
{
	// if we have nothing to dock with, fail
	if (getMachineGoalObject() == NULL)
		return STATE_FAILURE;

	// this behavior is an extention of basic MoveTo
	return AIInternalMoveToState::update();
}

//----------------------------------------------------------------------------------------------
void AIDockMoveToExitState::onExit( StateExitType status )
{
	Object *goalObject = getMachineGoalObject();

	DockUpdateInterface *dock = NULL;
	if( goalObject )
		dock = goalObject->getDockUpdateInterface();

	// tell the dock we have exited
	if (dock)
		dock->onExitReached( getMachineOwner() );

	// unlock the machine
	getMachine()->unlock();

	// this behavior is an extention of basic MoveTo
	AIInternalMoveToState::onExit( status );
}

//----------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------

//----------------------------------------------------------------------------------------------
/**
 * Move to the dock's rally position, if he wants me to.
 */
StateReturnType AIDockMoveToRallyState::onEnter( void )
{
	Object *goalObject = getMachineGoalObject();

  DockUpdateInterface *dock = NULL;
	if( goalObject )
		dock = goalObject->getDockUpdateInterface();

	// if we have nothing to dock with, fail
	if (dock == NULL)
		return STATE_FAILURE;

	// if they don't have anywhere to send us, then we are good
	if( ! dock->isRallyPointAfterDockType()															//Chooses not to
		|| goalObject->getObjectExitInterface() == NULL										//or can't
		|| goalObject->getObjectExitInterface()->getRallyPoint() == NULL	//or can't right now.
		)
	{
		return STATE_SUCCESS; // Success in an Enter is like success in an update.  We're all fine here
	}

	// get the rally point and set as our goal position
	m_goalPosition = *goalObject->getObjectExitInterface()->getRallyPoint();

	// this behavior is an extention of basic MoveTo
	return AIInternalMoveToState::onEnter();
}

//----------------------------------------------------------------------------------------------
StateReturnType AIDockMoveToRallyState::update( void )
{
	// This state is fine with the loss of the goal object after the move starts

	// this behavior is an extention of basic MoveTo
	return AIInternalMoveToState::update();
}

//----------------------------------------------------------------------------------------------
void AIDockMoveToRallyState::onExit( StateExitType status )
{
	// This state is fine with the loss of the goal object after the move starts

	// this behavior is an extention of basic MoveTo
	AIInternalMoveToState::onExit( status );
}


