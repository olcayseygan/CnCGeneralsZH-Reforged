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

// FILE: SupplyWarehouseDockUpdate.h /////////////////////////////////////////////////////////////////////////////
// Author: Graham Smallwood Feb 2002
// Desc:   The action of this dock update is identifying who is docking and either taking Boxes away or giving them
///////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#ifndef _SUPPLY_WAREHOUSE_DOCK_UPDATE_H_
#define _SUPPLY_WAREHOUSE_DOCK_UPDATE_H_

// INCLUDES ///////////////////////////////////////////////////////////////////////////////////////
#include "Common/INI.h"
#include "Common/GameMemory.h"
#include "GameLogic/Module/DockUpdate.h"

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
class SupplyWarehouseDockUpdateModuleData : public DockUpdateModuleData
{
public:

  SupplyWarehouseDockUpdateModuleData( void );
	
	static void buildFieldParse(MultiIniFieldParse& p);

	Int m_startingBoxesData;
	Bool m_deleteWhenEmpty;

	//
	// A supply point refills itself, slowly, and faster the more of the base is built around it.
	// A warehouse used to be a fixed lump of money that ran out and left the map with nothing left
	// to fight over: whoever mined it out first had the game, and the second half of every match
	// was played on the income two players could no longer earn.  Now it is ground worth holding.
	//
	UnsignedInt m_regenDelay;			///< frames a box takes with nothing built near it, 0 = never refills
	Fix m_regenRadius;						///< how close a cash building has to be to count
	Int m_regenMaxBoxes;					///< ceiling, -1 for whatever it started with
};

//-------------------------------------------------------------------------------------------------
class SupplyWarehouseDockUpdate : public DockUpdate
{

	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE( SupplyWarehouseDockUpdate, "SupplyWarehouseDockUpdate" )
	MAKE_STANDARD_MODULE_MACRO_WITH_MODULE_DATA( SupplyWarehouseDockUpdate, SupplyWarehouseDockUpdateModuleData )

public:

	virtual DockUpdateInterface* getDockUpdateInterface() { return this; }

	SupplyWarehouseDockUpdate( Thing *thing, const ModuleData* moduleData );

	virtual void setDockCrippled( Bool setting ); ///< Game Logic can set me as inoperative.  I get to decide what that means.
	virtual Bool action( Object* docker, Object *drone = NULL );	///<For me, this means identifying who is docking and either taking Boxes away or giving them

	Int getBoxesStored() const { return m_boxesStored; }
	virtual Int getSupplyCashValue( void ) const;

	void setCashValue( Int cashValue );

	virtual void onObjectCreated();
	virtual UpdateSleepTime update();

	/// how many cash buildings stand close enough to work this point
	Int countNearbyCollectors( void ) const;

protected:


	Int m_boxesStored;
	UnsignedInt m_nextRegenFrame;		///< when the next box arrives, 0 until the first one is scheduled

};

#endif
