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

// FILE: TunnelContain.h ////////////////////////////////////////////////////////////////////////////
// Author: Graham Smallwood, March 2002
// Desc:   A version of OpenContain that overrides where the passengers are stored: the Owning Player's
//					TunnelTracker.  All queries about capacity and contents are also redirected.
///////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#ifndef __TUNNEL_CONTAIN_H_
#define __TUNNEL_CONTAIN_H_

// INCLUDES ///////////////////////////////////////////////////////////////////////////////////////
#include "GameLogic/Module/OpenContain.h"
#include "GameLogic/Module/HealContain.h"
#include "GameLogic/Module/DieModule.h"
#include "GameLogic/Module/CreateModule.h"
#include "Common/GameMemory.h"
#include "Lib/FixBoundary.h"
class Team;
class TunnelTracker;

//-------------------------------------------------------------------------------------------------
class TunnelContainModuleData : public OpenContainModuleData
{
public:

	Fix m_framesForFullHeal;			///< time (in frames) something becomes fully healed

	TunnelContainModuleData()
	{

		// by default, takes no time to heal ppl
		m_framesForFullHeal = Fix( 1 );

		//
		// by default we say that transports can have infantry inside them, this will be totally
		// overwritten by any data provided from the INI entry tho
		//
		m_allowInsideKindOf = MAKE_KINDOF_MASK(KINDOF_INFANTRY);

	}

	static void buildFieldParse(MultiIniFieldParse& p) 
	{
    OpenContainModuleData::buildFieldParse(p);

		static const FieldParse dataFieldParse[] = 
		{
			{ "TimeForFullHeal", INI::parseDurationFix, NULL, FIX_OFFSET( TunnelContainModuleData, m_framesForFullHeal ) },
			{ 0, 0, 0, 0 }
		};
    p.add(dataFieldParse);
	}
};

class TunnelContain : public OpenContain, public CreateModuleInterface
{

	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE( TunnelContain, "TunnelContain" )
	MAKE_STANDARD_MODULE_MACRO_WITH_MODULE_DATA( TunnelContain, TunnelContainModuleData )
	
public:

	TunnelContain( Thing *thing, const ModuleData* moduleData );
	// virtual destructor prototype provided by memory pool declaration

	virtual CreateModuleInterface* getCreate() { return this; }
	static Int getInterfaceMask() { return OpenContain::getInterfaceMask() | (MODULEINTERFACE_CREATE); }

	virtual OpenContain *asOpenContain() { return this; }  ///< treat as open container
	virtual Bool isGarrisonable() const { return false; }	///< can this unit be Garrisoned? (ick)
	virtual Bool isBustable() const { return TRUE; }	///< can this container get busted by a bunkerbuster
	virtual Bool isHealContain() const { return false; } ///< true when container only contains units while healing (not a transport!)
	virtual Bool isTunnelContain() const { return TRUE; }
	Real getFullTimeForHeal() const { return fixToReal( getTunnelContainModuleData()->m_framesForFullHeal ); } ///< frames until a contained object is fully healed; P8, TunnelTracker heals in float
	virtual Bool isImmuneToClearBuildingAttacks() const { return true; }
  virtual Bool isSpecialOverlordStyleContainer() const {return FALSE;}

	virtual void onContaining( Object *obj, Bool wasSelected );		///< object now contains 'obj'
	virtual void onRemoving( Object *obj );			///< object no longer contains 'obj'
	virtual void onSelling();///< Container is being sold.  Tunnel responds by kicking people out if this is the last tunnel.
	virtual void onCapture( Player *oldOwner, Player *newOwner ); // Need to change who we are registered with.

	virtual void orderAllPassengersToExit( CommandSourceType commandSource, Bool instantly );
	virtual void orderAllPassengersToIdle( CommandSourceType commandSource );

	virtual Bool isValidContainerFor(const Object* obj, Bool checkCapacity) const;
	virtual void addToContainList( Object *obj );		///< The part of AddToContain that inheritors can override (Can't do whole thing because of all the private stuff involved)
	virtual void removeFromContain( Object *obj, Bool exposeStealthUnits = FALSE );	///< remove 'obj' from contain list
	virtual void removeAllContained( Bool exposeStealthUnits = FALSE );				///< remove all objects on contain list
  virtual void harmAndForceExitAllContained( DamageInfo *info );
  virtual void killAllContained( void );				///< kill all objects on contain list

	// contain list access
	virtual void iterateContained( ContainIterateFunc func, void *userData, Bool reverse );
	virtual UnsignedInt getContainCount() const;
	virtual Int getContainMax( void ) const;
	virtual const ContainedItemsList* getContainedItemsList() const;
	virtual Bool isContained( const Object *obj ) const;	
	virtual Bool isDisplayedOnControlBar() const { return TRUE; } ///< Does this container display its contents on the ControlBar?
	virtual Bool getContainerPipsToShow( Int& numTotal, Int& numFull );	///< the ones staying inside; see TunnelTracker::getResidentCount
	virtual Bool isKickOutOnCapture(){ return FALSE; }///< Caves and Tunnels don't kick out on capture.

	// override the onDie we inherit from OpenContain
	virtual void onDie( const DamageInfo *damageInfo );  ///< the die callback

	virtual void onDelete( void );
	virtual void onCreate( void );
	virtual void onObjectCreated();
	virtual void onBuildComplete();
	virtual Bool shouldDoOnBuildComplete() const { return m_needToRunOnBuildComplete; }

	// so that the ppl within the tunnel network can get healed	
	virtual UpdateSleepTime update();												///< called once per frame

protected:

	//
	// A tunnel keeps nothing of its own - the whole contain list belongs to the owning player's
	// TunnelTracker, and a Player that has been init'ed but not yet handed a dict does not have one
	// (Player::init leaves it NULL, Player::initFromDict allocates it).  Every path below that
	// reaches for the tracker goes through this.
	//
	TunnelTracker *getTunnelTracker( void ) const;

	void scatterToNearbyPosition(Object* obj);
	Bool m_needToRunOnBuildComplete; 
	Bool m_isCurrentlyRegistered; ///< Keeps track if this is registered with the player, so we don't double remove and mess up

};

#endif  // end __TUNNEL_CONTAIN_H_
