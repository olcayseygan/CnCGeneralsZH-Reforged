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

// FILE: ProductionUpdate.h ///////////////////////////////////////////////////////////////////////
// Author: Colin Day, March 2002
// Desc:   This module allows things to be "constructed" from a building
///////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#ifndef __PRODUCTIONUPDATE_H_
#define __PRODUCTIONUPDATE_H_

// USER INCLUDES //////////////////////////////////////////////////////////////////////////////////
#include "Common/ModelState.h"
#include "GameLogic/Module/DieModule.h"
#include "GameLogic/Module/UpdateModule.h"

// FORWARD REFERNCES //////////////////////////////////////////////////////////////////////////////
class ProductionEntry;
class ThingTemplate;
class UpgradeTemplate;

///////////////////////////////////////////////////////////////////////////////////////////////////
enum ProductionID : Int
{
	PRODUCTIONID_INVALID = 0
};

enum ProductionType : Int
{
	PRODUCTION_INVALID = 0,
	PRODUCTION_UNIT,
	PRODUCTION_UPGRADE
};

//-------------------------------------------------------------------------------------------------
/** A ProductionEntry is a single entry representing something that we are supposed to 
	* produce */
//-------------------------------------------------------------------------------------------------
class ProductionEntry : public MemoryPoolObject
{

friend class ProductionUpdate;

	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE( ProductionEntry, "ProductionEntry" )

public:

	ProductionEntry( void );
	// virtual destructor provided by memory pool object

	/// query what kind of thing is being produced by this entry; NULL for an upgrade, whose template
	/// shares the storage and read as a ThingTemplate is garbage
	const ThingTemplate *getProductionObject( void ) const { return m_type == PRODUCTION_UNIT ? m_objectToProduce : NULL; }

	/// query what kind of upgrade is being produced by this entry
	const UpgradeTemplate *getProductionUpgrade( void ) const { return m_upgradeToResearch; }

	/// query the production type
	ProductionType getProductionType( void ) const { return m_type; }

	/// how much progress is done on this entry
	Real getPercentComplete( void ) const { return m_percentComplete; }

	/// get the unique (to the producer object) production ID
	ProductionID getProductionID( void ) const { return m_productionID; }

	Int getProductionQuantity() const { return m_productionQuantityTotal; } //How many I try to make
	Int getProductionQuantityRemaining() const { return m_productionQuantityTotal - m_productionQuantityProduced; }//How many I have made

	void oneProductionSuccessful() { ++m_productionQuantityProduced; m_exitDoor = DOOR_NONE_AVAILABLE; }//increment, and mark door to re-reserve

	ExitDoorType getExitDoor() const { return m_exitDoor; }
	void setExitDoor(ExitDoorType exitDoor) { m_exitDoor = exitDoor; }

	/// the hotkey squad this unit joins when it comes out (MSG_QUEUE_UNIT_FOR_GROUP), or -1
	Int getReinforcementSquad() const { return m_reinforcementSquad; }

protected:
	
	ProductionType m_type;														///< production type
	union
	{
		const ThingTemplate *m_objectToProduce;					///< what we're going to produce
		const UpgradeTemplate *m_upgradeToResearch;			///< what upgrade we're researching
	};
	ProductionID m_productionID;											///< our very own production ID!
	Real m_percentComplete;														///< percent our construction is complete
	Int m_framesUnderConstruction;										///< counter for how many frames we've been under construction (incremented once per update)
	Int m_totalProductionFrames;											///< the build time m_framesUnderConstruction was last measured against
	Int m_productionQuantityTotal;										///< it is now possible to construct multiple units simultaneously.
	Int m_productionQuantityProduced;									///< And we need to allow pausing within an entry, so we keep track of number of sub-successes
	ExitDoorType m_exitDoor;
	UnsignedInt m_costPaid;														///< what was withdrawn when this was queued, and what a cancel refunds
	Int m_reinforcementSquad;													///< hotkey squad the unit joins on exit, -1 for none

	ProductionEntry *m_next;													///< next in list
	ProductionEntry *m_prev;													///< prev in list

};

//-------------------------------------------------------------------------------------------------
struct QuantityModifier
{
	AsciiString m_templateName;
	Int					m_quantity;
};

//-------------------------------------------------------------------------------------------------
class ProductionUpdateModuleData : public UpdateModuleData
{

public:
  Int														m_numDoorAnimations;						///< has a door animation(s) upon unit built and exit
	DurationFrames										m_doorOpeningTime;							///< in frames, time it takes the door to open
	DurationFrames										m_doorWaitOpenTime;							///< in frames, time we should leave the door open
	DurationFrames										m_doorClosingTime;							///< in frames, time it takes to close the door
	DurationFrames										m_constructionCompleteDuration; ///< in frames, how long we state in "construction complete" condition after making something
	std::vector<QuantityModifier>	m_quantityModifiers;						///< Quantity modifiers modify the number of specified object to created whenever produced.
  Int														m_maxQueueEntries;							///< max things that can be queued at once.
	DisabledMaskType							m_disabledTypesToProcess;

	ProductionUpdateModuleData( void );
	static void buildFieldParse(MultiIniFieldParse& p);
	static void parseAppendQuantityModifier( INI* ini, void *instance, void *store, const void *userData );
};

//-------------------------------------------------------------------------------------------------
/** The unit a factory builds on repeat (fork).  The player turns one on and off with Ctrl+Alt and
	* the button's key, or a Ctrl+Alt click on it.  Whenever the factory's queue is empty it queues
	* that unit.  Only one unit at a time: turning a second on takes the first off.
	* Logic state, set only by MSG_SET_REPEAT_UNIT; kept apart from the module so the test binary can
	* drive it with no object behind it. */
//-------------------------------------------------------------------------------------------------
struct ProductionRepeat
{
	std::vector<const ThingTemplate *> m_units;	///< the unit on repeat, or empty
	UnsignedInt m_next;													///< index into m_units of the one whose turn it is

	ProductionRepeat() : m_next( 0 ) {}

	Bool has( const ThingTemplate *unit ) const
	{
		for( size_t i = 0; i < m_units.size(); ++i )
			if( m_units[ i ] == unit )
				return TRUE;
		return FALSE;
	}

	/// on replaces whatever was on repeat with this unit; off takes it out
	void set( const ThingTemplate *unit, Bool on )
	{
		if( on )
		{
			if( m_units.size() == 1 && m_units[ 0 ] == unit )
				return;
			m_units.clear();
			m_units.push_back( unit );
			m_next = 0;
			return;
		}
		for( size_t i = 0; i < m_units.size(); ++i )
		{
			if( m_units[ i ] != unit )
				continue;
			m_units.erase( m_units.begin() + i );
			if( i < m_next )
				--m_next;
			if( m_next >= m_units.size() )
				m_next = 0;
			return;
		}
	}

	const ThingTemplate *turn() const { return m_units.empty() ? NULL : m_units[ m_next ]; }
	void advance() { if( !m_units.empty() ) m_next = ( m_next + 1 ) % m_units.size(); }
	void clear() { m_units.clear(); m_next = 0; }
};

//-------------------------------------------------------------------------------------------------
enum CanMakeType : Int;

enum ProductionRepeatAction : Int
{
	REPEAT_QUEUE,		///< queue it now
	REPEAT_WAIT,		///< not now (money, queue, cap, power): try again next frame
	REPEAT_DROP			///< the factory can no longer make it: off the repeat
};

ProductionRepeatAction Production_repeatAction( CanMakeType canMake );

/** A building still going up or being sold researches nothing. Its own command bar never offers an
	* upgrade then, but a multi-selection that mixes it with finished ones did, and the logic took it. */
inline Bool canResearchWithStatus( const ObjectStatusMaskType &status )
{
	return !status.testForAny( MAKE_OBJECT_STATUS_MASK2( OBJECT_STATUS_UNDER_CONSTRUCTION, OBJECT_STATUS_SOLD ) );
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
class ProductionUpdateInterface
{

public:

	virtual CanMakeType canQueueCreateUnit( const ThingTemplate *unitType ) const = 0;
	virtual CanMakeType canQueueUpgrade( const UpgradeTemplate *upgrade ) const = 0;

	virtual ProductionID requestUniqueUnitID( void ) = 0;

	virtual Bool queueUpgrade( const UpgradeTemplate *upgrade ) = 0;
	virtual void cancelUpgrade( const UpgradeTemplate *upgrade ) = 0;
	virtual Bool isUpgradeInQueue( const UpgradeTemplate *upgrade ) const = 0;
	virtual UnsignedInt countUnitTypeInQueue( const ThingTemplate *unitType ) const = 0;
	virtual Int getQuantityPerOrder( const ThingTemplate *unitType ) const = 0;

	virtual Bool queueCreateUnit( const ThingTemplate *unitType, ProductionID productionID, Int reinforcementSquad = -1 ) = 0;
	virtual Bool cancelUnitCreate( ProductionID productionID ) = 0;
	virtual void cancelAllUnitsOfType( const ThingTemplate *unitType) = 0;

	virtual void cancelAndRefundAllProduction( void ) = 0;

	virtual void setRepeatUnit( const ThingTemplate *unitType, Bool on ) = 0;	///< see ProductionRepeat
	virtual Bool isRepeatUnit( const ThingTemplate *unitType ) const = 0;

	virtual UnsignedInt getProductionCount( void ) const = 0;
	virtual UnsignedInt getMaxQueueEntries( void ) const = 0;

	virtual const ProductionEntry *firstProduction( void ) const = 0;
	virtual const ProductionEntry *nextProduction( const ProductionEntry *p ) const = 0;

	virtual void setHoldDoorOpen(ExitDoorType exitDoor, Bool holdIt) = 0;

	//These functions keep track of the special power construction of a new building via a special power instead of standard production interface.
	//This was added for the sneak attack building functionality.
	virtual const CommandButton* getSpecialPowerConstructionCommandButton() const = 0;
	virtual void setSpecialPowerConstructionCommandButton( const CommandButton *commandButton ) = 0;

};

//-------------------------------------------------------------------------------------------------
/** Production modules do actual unit construction */
//-------------------------------------------------------------------------------------------------
class ProductionUpdate : public UpdateModule, public ProductionUpdateInterface, public DieModuleInterface
{

	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE( ProductionUpdate, "ProductionUpdate" )
	MAKE_STANDARD_MODULE_MACRO_WITH_MODULE_DATA( ProductionUpdate, ProductionUpdateModuleData )

public:

	ProductionUpdate( Thing *thing, const ModuleData* moduleData );
	// virtual destructor prototype provided by MemoryPoolObject

	static Int getInterfaceMask() { return UpdateModule::getInterfaceMask() | (MODULEINTERFACE_DIE); }

	// Disabled conditions to process (AI will still process held status)
	virtual DisabledMaskType getDisabledTypesToProcess() const { return getProductionUpdateModuleData()->m_disabledTypesToProcess; }

	virtual ProductionUpdateInterface* getProductionUpdateInterface( void ) { return this; }
	virtual DieModuleInterface* getDie() { return this; }
	static ProductionUpdateInterface *getProductionUpdateInterfaceFromObject( Object *obj );

	virtual CanMakeType canQueueCreateUnit( const ThingTemplate *unitType ) const;
	virtual CanMakeType canQueueUpgrade( const UpgradeTemplate *upgrade ) const;

	/** this method is used to request a unique ID to assign to the production of a single
	unit.  It is unique to all units that can be created from this source object, but is 
	not unique amoung multiple source objects */
	virtual ProductionID requestUniqueUnitID( void ) { ProductionID tmp = m_uniqueID; m_uniqueID = (ProductionID)(m_uniqueID+1); return tmp; }

	virtual Bool queueUpgrade( const UpgradeTemplate *upgrade );				///< queue upgrade "research"
	virtual void cancelUpgrade( const UpgradeTemplate *upgrade );				///< cancel upgrade "research"
	virtual Bool isUpgradeInQueue( const UpgradeTemplate *upgrade ) const;		///< is the upgrade in our production queue already
	virtual UnsignedInt countUnitTypeInQueue( const ThingTemplate *unitType ) const;  ///< count number of units with matching unit type in the production queue
	virtual Int getQuantityPerOrder( const ThingTemplate *unitType ) const;	///< units one paid order hands back, from QuantityModifier

	virtual Bool queueCreateUnit( const ThingTemplate *unitType, ProductionID productionID, Int reinforcementSquad = -1 );	///< queue unit to be produced; a squad 0-9 sends it to that hotkey squad on exit
	virtual Bool cancelUnitCreate( ProductionID productionID );		      ///< cancel construction of unit with matching production ID
	virtual void cancelAllUnitsOfType( const ThingTemplate *unitType);	///< cancel all production of type unitType 

	virtual void cancelAndRefundAllProduction( void );									///< cancel and refund anything in the production queue

	virtual void setRepeatUnit( const ThingTemplate *unitType, Bool on ) { m_repeat.set( unitType, on ); wakeIfResting(); }
	virtual Bool isRepeatUnit( const ThingTemplate *unitType ) const { return m_repeat.has( unitType ); }

	virtual UnsignedInt getProductionCount( void ) const { return m_productionCount; }    ///< return # of things in the production queue
	virtual UnsignedInt getMaxQueueEntries( void ) const { return getProductionUpdateModuleData()->m_maxQueueEntries; }  ///< how deep the queue may go (INI MaxQueueEntries)

	// walking the production list from outside
	virtual const ProductionEntry *firstProduction( void ) const { return m_productionQueue; }
	virtual const ProductionEntry *nextProduction( const ProductionEntry *p ) const { return p ? p->m_next : NULL; }

	virtual void setHoldDoorOpen(ExitDoorType exitDoor, Bool holdIt);

	virtual UpdateSleepTime update( void );					///< the update

	//These functions keep track of the special power construction of a new building via a special power instead of standard production interface.
	//This was added for the sneak attack building functionality.
	virtual const CommandButton* getSpecialPowerConstructionCommandButton() const { return m_specialPowerConstructionCommandButton; }
	virtual void setSpecialPowerConstructionCommandButton( const CommandButton *commandButton ) { m_specialPowerConstructionCommandButton = commandButton; }

	// DieModuleInterface
	virtual void onDie( const DamageInfo *damageInfo );

	virtual void onCapture( Player *oldOwner, Player *newOwner );

protected:

	void queueRepeatUnit( void );										///< an empty queue takes the repeated unit whose turn it is
	Bool isResting( void ) const;										///< the next update would change nothing until wakeIfResting
	void wakeIfResting( void );											///< something is queued, on repeat or at a door


	void addToProductionQueue( ProductionEntry *production );				///< add to *END* of production queue list
	void removeFromProductionQueue( ProductionEntry *production );	///< remove production from the queue list

	void updateDoors( Bool moreUnitsComing );				///< update the door behavior
	void beginOpeningDoor( ExitDoorType exitDoor );	///< start (or hold) a door on its way open

	struct DoorInfo
	{
		UnsignedInt m_doorOpenedFrame;											///< for producer objects that have a door open/close animation when they make something
		UnsignedInt m_doorWaitOpenFrame;										///< frame we entered into wait open state
		UnsignedInt m_doorClosedFrame;											///< frame any door was closed on
		Bool				m_holdOpen;															///< if T, don't allow door to close
	};

	const CommandButton *m_specialPowerConstructionCommandButton; ///< In a mode to construct a specific building via a special power. (NO NEED TO SAVE DATA ON THIS FIELD)
	ProductionEntry*		m_productionQueue;							///< queue of things we want to build
	ProductionEntry*		m_productionQueueTail;					///< tail pointer for m_productionQueue
	ProductionID				m_uniqueID;											///< unique ID counter for producing units
	UnsignedInt					m_productionCount;							///< # of things in the production queue
	UnsignedInt					m_constructionCompleteFrame;		///< frame construction was complete on
	DoorInfo						m_doors[DOOR_COUNT_MAX];
	ModelConditionFlags m_clearFlags;										///< flags to clear from model
	ModelConditionFlags m_setFlags;											///< flags to set in model
	Bool								m_flagsDirty;										///< clearFlags/setFlags needs to be set into the model
	ProductionRepeat		m_repeat;												///< the units built on repeat

};

#endif  // end __PRODUCTIONUPDATE_H_
