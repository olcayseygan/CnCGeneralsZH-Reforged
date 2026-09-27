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

// SupplyTruckAIUpdate.h ////////////
// Author: Graham Smallwood, February 2002
// Desc:   State machine that controls when and with who a Truck docks

#pragma once

#ifndef _SUPPLY_TRUCK_AI_UPDATE_H_
#define _SUPPLY_TRUCK_AI_UPDATE_H_

#include "Common/StateMachine.h"
#include "GameLogic/Module/AIUpdate.h"

//-------------------------------------------------------------------------------------------------
/** How far through a dock action (fetching or handing over one box) frame 'now' is, given the
	* window the dock state announced.  Negative outside the window - which is also what a docker
	* that is not docked reads, since nothing but the dock state ever moves the window.  Shared by
	* SupplyTruckAIUpdate and WorkerAIUpdate, which implement the same interface side by side. */
//-------------------------------------------------------------------------------------------------
inline Real dockActionProgress( UnsignedInt now, UnsignedInt start, UnsignedInt end )
{
	if( end <= start || now >= end || now < start )
		return -1.0f;

	return INT_TO_REAL( now - start ) / INT_TO_REAL( end - start );
}

//-------------------------------------------------------------------------------------------------
/** Is there another box in this for the docker after the one just handed over?  Asked the moment a
	* box changes hands, so that a docking that is finished ends there and then: a docker that stays
	* docked spends another whole action delay before finding out there was nothing left to take,
	* and the bar over its head fills again for a box that never arrives. */
//-------------------------------------------------------------------------------------------------
inline Bool supplyDockHasNextBox( Int stockLeft, Int dockerBoxes, Int dockerMax )
{
	return stockLeft > 0 && dockerBoxes < dockerMax;
}

//-------------------------------------------------------------------------------------------------
/** The shortest a leg between two supply dockings may take, pile to centre or centre to pile.  A
	* supply pile and a GLA stash have no dock bones, so each one docks a worker wherever its approach
	* spot is, and that spot is the clear ground nearest the building on the worker's side.  A stash
	* built at the legal limit leaves a gap about 24 feet wide beside the pile, and a worker standing
	* in it is on both approach spots at once: it loaded and unloaded without taking a step, a box
	* every 15 frames where a worker that walks the same gap needs 37 or more.  37 frames is that
	* shortest walked leg, so the floor only ever catches a worker that did not walk. */
//-------------------------------------------------------------------------------------------------
enum { SUPPLY_DOCK_MIN_LEG_FRAMES = 37 };

inline UnsignedInt supplyDockLegDelay( UnsignedInt dockDelay, UnsignedInt now, UnsignedInt lastActionFrame )
{
	// 0 is a docker that has never docked
	const UnsignedInt earliest = lastActionFrame + SUPPLY_DOCK_MIN_LEG_FRAMES;
	if( lastActionFrame == 0 || now >= earliest || earliest - now <= dockDelay )
		return dockDelay;

	return earliest - now;
}

//-------------------------------------------------------------------------------------------------
/** How long one visit to a supply point takes.  The whole load changes hands in a single action
	* now, so the visit is priced by what that load is: SupplyWarehouseActionDelay buys one box, and
	* a docker leaving with four of them waits for four.  Same total as taking them one at a time,
	* one bar instead of four.
	*
	* The price is fixed when the docking opens, so a box another worker takes out of the pile while
	* this one is loading is not refunded, and one the pile grows back is carried for free. */
//-------------------------------------------------------------------------------------------------
inline UnsignedInt supplyWarehouseActionDelay( UnsignedInt perBoxDelay, Int stockLeft, Int dockerBoxes, Int dockerMax )
{
	const Int room = dockerMax - dockerBoxes;
	Int boxes = ( stockLeft < room ) ? stockLeft : room;
	if( boxes < 1 )
		boxes = 1;			// nothing to take, but the docker still walks up and finds that out

	return perBoxDelay * (UnsignedInt)boxes;
}

//-------------------------------------------------------------------------------------------------
class SupplyTruckStateMachine : public StateMachine
{
	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE( SupplyTruckStateMachine, "SupplyTruckStateMachine" );
public:
	SupplyTruckStateMachine( Object *owner );

// state transition conditions

	static Bool ownerDocking( State *thisState, void* userData );
	static Bool ownerIdle( State *thisState, void* userData );
	static Bool ownerAvailableForSupplying( State *thisState, void* userData );
	static Bool ownerNotDockingOrIdle( State *thisState, void* userData );
	static Bool isForcedIntoWantingState( State *thisState, void* userData );
	static Bool isForcedIntoBusyState( State *thisState, void* userData );
	static Bool ownerPlayerCommanded( State *thisState, void* userData );

protected:
	// snapshot interface
	virtual void crc( Xfer *xfer );
	virtual void xfer( Xfer *xfer );
	virtual void loadPostProcess();
};

//-------------------------------------------------------------------------------------------------
class SupplyTruckWantsToPickUpOrDeliverBoxesState :  public State
{
	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE(SupplyTruckWantsToPickUpOrDeliverBoxesState, "SupplyTruckWantsToPickUpOrDeliverBoxesState")		
protected:
	// snapshot interface STUBBED.
	virtual void crc( Xfer *xfer ){};
	virtual void xfer( Xfer *xfer ){XferVersion cv = 1;	XferVersion v = cv; xfer->xferVersion( &v, cv );}
	virtual void loadPostProcess(){};

public:
	SupplyTruckWantsToPickUpOrDeliverBoxesState( StateMachine *machine ) : State( machine, "SupplyTruckWantsToPickUpOrDeliverBoxesState" ) {}
	virtual StateReturnType update();
	virtual StateReturnType onEnter();
	virtual void onExit(StateExitType status);
};
EMPTY_DTOR(SupplyTruckWantsToPickUpOrDeliverBoxesState)

//-------------------------------------------------------------------------------------------------
class RegroupingState :  public State
{
	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE(RegroupingState, "RegroupingState")		
protected:
	// snapshot interface STUBBED.
	virtual void crc( Xfer *xfer ){};
	virtual void xfer( Xfer *xfer ){XferVersion cv = 1;	XferVersion v = cv; xfer->xferVersion( &v, cv );}
	virtual void loadPostProcess(){};
public:
	RegroupingState( StateMachine *machine ) : State( machine, "RegroupingState" ) {}
	virtual StateReturnType update();
	virtual StateReturnType onEnter();// Will tell me to aiMove back to base.
	virtual void onExit(StateExitType status);
};
EMPTY_DTOR(RegroupingState)

//-------------------------------------------------------------------------------------------------
class DockingState :  public State
{
	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE(DockingState, "DockingState")		
protected:
	// snapshot interface STUBBED.
	virtual void crc( Xfer *xfer ){};
	virtual void xfer( Xfer *xfer ){XferVersion cv = 1;	XferVersion v = cv; xfer->xferVersion( &v, cv );}
	virtual void loadPostProcess(){};
public:
	DockingState( StateMachine *machine ) :State( machine, "DockingState" ) {}
	virtual StateReturnType update();
	virtual StateReturnType onEnter();
	virtual void onExit(StateExitType status);
};
EMPTY_DTOR(DockingState)

//-------------------------------------------------------------------------------------------------
enum
{
	ST_IDLE,						///< Not doing anything.  Should I autopilot?
	ST_BUSY,						///< Direct player involvement (move) has taken me off autopilot
	ST_WANTING,					///< Search for warehouse or center and dock with it
	ST_REGROUPING,			///< Wanting failed, so hang out at base until something changes.  Autopilot will turn off.
	ST_DOCKING					///< Docking substates are running, wait for them to finish
};

//-------------------------------------------------------------------------------------------------
class SupplyTruckAIUpdateModuleData : public AIUpdateModuleData
{
public:
	Int m_maxBoxesData;
	UnsignedInt m_centerDelay;
	UnsignedInt m_warehouseDelay;
	Fix m_warehouseScanDistance;
 	AudioEventRTS m_suppliesDepletedVoice;						///< Sound played when I take the last box.

	SupplyTruckAIUpdateModuleData()
	{
		m_maxBoxesData = 0;
		m_centerDelay = 0;
		m_warehouseDelay = 0;
		m_warehouseScanDistance = Fix( 100 );
	}

	static void buildFieldParse(MultiIniFieldParse& p) 
	{
    AIUpdateModuleData::buildFieldParse(p);

		static const FieldParse dataFieldParse[] = 
		{
			{ "MaxBoxes",					INI::parseInt,		NULL, offsetof( SupplyTruckAIUpdateModuleData, m_maxBoxesData ) },
			{ "SupplyCenterActionDelay", INI::parseDurationUnsignedInt, NULL, offsetof( SupplyTruckAIUpdateModuleData, m_centerDelay ) },
			{ "SupplyWarehouseActionDelay", INI::parseDurationUnsignedInt, NULL, offsetof( SupplyTruckAIUpdateModuleData, m_warehouseDelay ) },
			{ "SupplyWarehouseScanDistance", INI::parseFix, NULL, FIX_OFFSET( SupplyTruckAIUpdateModuleData, m_warehouseScanDistance ) },
 			{ "SuppliesDepletedVoice", INI::parseAudioEventRTS, NULL, offsetof( SupplyTruckAIUpdateModuleData, m_suppliesDepletedVoice) },
			{ 0, 0, 0, 0 }
		};
    p.add(dataFieldParse);

	}
};

//-------------------------------------------------------------------------------------------------
class SupplyTruckAIInterface
{
	// This is no longer a leaf behavior.  Someone else needs to combine this
	// with another major AIUpdate.  So provide an interface to satisfy the people
	// who look this up by name.
public:
	virtual Int getNumberBoxes() const = 0;
	virtual Bool loseOneBox() = 0;
	virtual Bool gainOneBox( Int remainingStock ) = 0;

	// returns true if we can fetch/deliver supplies. normally returns true
	// if the AI is idle, but subclasses might add further restrictions.
	virtual Bool isAvailableForSupplying() const = 0;
	virtual Bool isCurrentlyFerryingSupplies() const = 0;
	virtual Fix getWarehouseScanDistance() const = 0; ///< How far can I look for a warehouse?

	virtual void setForceWantingState(Bool v) = 0;
	virtual Bool isForcedIntoWantingState() const = 0;
	virtual void setForceBusyState(Bool v) = 0;
	virtual Bool isForcedIntoBusyState() const = 0;
	virtual ObjectID getPreferredDockID() const = 0;
	virtual UnsignedInt getActionDelayForDock( Object *dock ) = 0;
	virtual Int getUpgradedSupplyBoost() const = 0;
	virtual Int getMaxBoxes() const = 0;			///< how many boxes a full load is

	/** The dock state announces how long the box it is working on takes; that window is what the
		* progress bar over the worker's head fills across.  (See AIDockProcessDockState.) */
	virtual void noteDockActionWindow( UnsignedInt frames ) = 0;
	virtual Real getDockActionProgress() const = 0;	///< 0..1 while loading or unloading, negative otherwise
};

//-------------------------------------------------------------------------------------------------
class SupplyTruckAIUpdate : public AIUpdateInterface, public SupplyTruckAIInterface
{

	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE( SupplyTruckAIUpdate, "SupplyTruckAIUpdate" )
	MAKE_STANDARD_MODULE_MACRO_WITH_MODULE_DATA( SupplyTruckAIUpdate, SupplyTruckAIUpdateModuleData )

private:

public:

	SupplyTruckAIUpdate( Thing *thing, const ModuleData* moduleData );
	// virtual destructor prototype provided by memory pool declaration

	virtual SupplyTruckAIInterface* getSupplyTruckAIInterface() {return this;}
	virtual const SupplyTruckAIInterface* getSupplyTruckAIInterface() const {return this;}

	virtual Int getNumberBoxes() const { return m_numberBoxes; }
	virtual Bool loseOneBox();
	virtual Bool gainOneBox( Int remainingStock );

	// this is present for subclasses (eg, Chinook) to override, to
	// prevent supply-ferry behavior in some cases (eg, when toting passengers)
	virtual Bool isAvailableForSupplying() const;
	virtual Bool isCurrentlyFerryingSupplies() const;
	virtual Fix getWarehouseScanDistance() const; ///< How far can I look for a warehouse?

	virtual void setForceWantingState(Bool v) { m_forcePending = v; } // When a Supply Center creates us (or maybe other sources later), we need to hop into autopilot mode.
	virtual Bool isForcedIntoWantingState() const { return m_forcePending; }

	virtual void setForceBusyState(Bool v) { m_forcedBusyPending = v; } 
	virtual Bool isForcedIntoBusyState() const { return m_forcedBusyPending; }

	virtual ObjectID getPreferredDockID() const { return m_preferredDock; }
	virtual UnsignedInt getActionDelayForDock( Object *dock );
	virtual Int getUpgradedSupplyBoost() const { return 0; }
	virtual Int getMaxBoxes() const { return getSupplyTruckAIUpdateModuleData()->m_maxBoxesData; }

	virtual void noteDockActionWindow( UnsignedInt frames );
	virtual Real getDockActionProgress() const;

	virtual UpdateSleepTime update();
	virtual void aiDoCommand(const AICommandParms* parms);

protected:

	virtual AIStateMachine* makeStateMachine();
	virtual void privateDock( Object *obj, CommandSourceType cmdSource );
	virtual void privateIdle(CommandSourceType cmdSource);						///< Enter idle state.	

private:
	UnsignedInt								m_dockActionStartFrame;	///< when the box currently being moved started
	UnsignedInt								m_dockActionEndFrame;		///< and when it lands; equal means "not docking"
	SupplyTruckStateMachine*	m_supplyTruckStateMachine;
	ObjectID									m_preferredDock;			///< Instead of searching, try this one first
	Int												m_numberBoxes;
	Bool											m_forcePending;				// To prevent a function from doing a setState, 
																									// forceWanting will latch into here until serviced.
	Bool											m_forcedBusyPending;	// A supply truck can't tell the difference between Idle since
																									// I'm between docking states, or a Stop command without help.
 	AudioEventRTS m_suppliesDepletedVoice;						///< Sound played when I take the last box.

};

#endif
