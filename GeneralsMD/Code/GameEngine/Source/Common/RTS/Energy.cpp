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

// FILE: Energy.cpp /////////////////////////////////////////////////////////
//-----------------------------------------------------------------------------
//                                                                          
//                       Westwood Studios Pacific.                          
//                                                                          
//                       Confidential Information                           
//                Copyright (C) 2001 - All Rights Reserved                  
//                                                                          
//-----------------------------------------------------------------------------
//
// Project:   RTS3
//
// File name: Energy.cpp
//
// Created:   Steven Johnson, October 2001
//
// Desc:      @todo
//
//-----------------------------------------------------------------------------

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "Common/Energy.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/ThingTemplate.h"
#include "Common/Xfer.h"

#include "GameLogic/DirectStrikeDirector.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object.h"

//-----------------------------------------------------------------------------
// A Direct Strike seat builds no power plant, so a power-hungry unit (the Laser general's tanks, the
// Avenger) would be browned out from the moment it is placed.  The mode has no power economy: every
// seat counts as fully powered, the same way the infinite-power cheat does.
static Bool powerIsFree( void )
{
	return TheDirectStrikeDirector != NULL && TheDirectStrikeDirector->isActive();
}

#ifdef _INTERNAL
// for occasional debugging...
//#pragma optimize("", off)
//#pragma MESSAGE("************************************** WARNING, optimization disabled for debugging purposes")
#endif

//-----------------------------------------------------------------------------
Energy::Energy()
{
	m_energyProduction = 0;
	m_energyConsumption = 0;
	m_owner = NULL;
	m_powerSabotagedTillFrame = 0;
}

//-----------------------------------------------------------------------------
Int Energy::getProduction() const
{ 
	if( TheGameLogic->getFrame() < m_powerSabotagedTillFrame )
	{
		//Power sabotaged, therefore no power.
		return 0;
	}
	return m_energyProduction; 
}

//-----------------------------------------------------------------------------
Real Energy::getEnergySupplyRatio() const 
{ 
	DEBUG_ASSERTCRASH(m_energyProduction >= 0 && m_energyConsumption >= 0, ("neg Energy numbers\n"));

	if( TheGameLogic->getFrame() < m_powerSabotagedTillFrame )
	{
		//Power sabotaged, therefore no power, no ratio.
		return 0.0f;
	}

	if( ( m_owner->hasCheat( CHEAT_INFINITE_POWER ) || powerIsFree() ) && m_energyProduction < m_energyConsumption )
		return 1.0f;

	if (m_energyConsumption == 0)
		return (Real)m_energyProduction;

	return (Real)m_energyProduction / (Real)m_energyConsumption;
}

//-------------------------------------------------------------------------------------------------
Bool Energy::hasSufficientPower(void) const
{
	if( TheGameLogic->getFrame() < m_powerSabotagedTillFrame )
	{
		//Power sabotaged, therefore no power.
		return FALSE;
	}
	if( m_owner->hasCheat( CHEAT_INFINITE_POWER ) || powerIsFree() )
		return TRUE;
	return m_energyProduction >= m_energyConsumption;
}

//-------------------------------------------------------------------------------------------------
void Energy::adjustPower(Int powerDelta, Bool adding)
{
	if (powerDelta == 0) {
		return;
	}

	if (powerDelta > 0) {
		if (adding) { 
			addProduction(powerDelta);
		} else {
			addProduction(-powerDelta);
		}
	} else {
		// Seems a little odd, however, consumption is reversed. Negative power is positive consumption.
		if (adding) {
			addConsumption(-powerDelta);
		} else {
			addConsumption(powerDelta);
		}
	}
}

//-------------------------------------------------------------------------------------------------
/** new 'obj' will now add/subtract from this energy construct */
//-------------------------------------------------------------------------------------------------
void Energy::objectEnteringInfluence( Object *obj )
{

	// sanity
	if( obj == NULL )
		return;

	// get the amount of energy this object produces or consumes
	Int energy = obj->getTemplate()->getEnergyProduction();

	// adjust energy
	if( energy < 0 )
		addConsumption( -energy );
	else if( energy > 0 )
		addProduction( energy );

	// sanity
	DEBUG_ASSERTCRASH( m_energyProduction >= 0 && m_energyConsumption >= 0, 
										 ("Energy - Negative Energy numbers, Produce=%d Consume=%d\n",
										 m_energyProduction, m_energyConsumption) );

}  // end objectEnteringInfluence

//-------------------------------------------------------------------------------------------------
/** 'obj' will now no longer add/subtrack from this energy construct */
//-------------------------------------------------------------------------------------------------
void Energy::objectLeavingInfluence( Object *obj )
{

	// sanity
	if( obj == NULL )
		return;

	// get the amount of energy this object produces or consumes
	Int energy = obj->getTemplate()->getEnergyProduction();

	// adjust energy
	if( energy < 0 )
		addConsumption( energy );
	else if( energy > 0 )
		addProduction( -energy );

	// sanity
	DEBUG_ASSERTCRASH( m_energyProduction >= 0 && m_energyConsumption >= 0, 
										 ("Energy - Negative Energy numbers, Produce=%d Consume=%d\n",
										 m_energyProduction, m_energyConsumption) );

}

//-------------------------------------------------------------------------------------------------
/** Adds an energy bonus to the player's pool of energy when the "Control Rods" upgrade
		is made to the American Cold Fusion Plant */
//-------------------------------------------------------------------------------------------------
void Energy::addPowerBonus( Object *obj )
{

	// sanity
	if( obj == NULL )
		return;

	//
	// The mirror of removePowerBonus: a disabled producer contributes nothing, and the bonus of an
	// upgrade completed while it is disabled is added by onDisabledEdge when it comes back.  Adding
	// it here as well counts it twice.
	//
	if( obj->isDisabled() )
		return;

	addProduction(obj->getTemplate()->getEnergyBonus());

	// sanity
	DEBUG_ASSERTCRASH( m_energyProduction >= 0 && m_energyConsumption >= 0, 
										 ("Energy - Negative Energy numbers, Produce=%d Consume=%d\n",
										 m_energyProduction, m_energyConsumption) );

}

// ------------------------------------------------------------------------------------------------
/** Removed an energy bonus */
// ------------------------------------------------------------------------------------------------
void Energy::removePowerBonus( Object *obj )
{

	// sanity
	if( obj == NULL )
		return;

	//
	// A disabled producer contributes nothing: onDisabledEdge already took its production and its
	// bonus out of the pool.  Taking the bonus away a second time here drives the pool below what
	// the player actually has and fakes a brownout.
	//
	if( obj->isDisabled() )
		return;

	addProduction( -obj->getTemplate()->getEnergyBonus() );

	// sanity
	DEBUG_ASSERTCRASH( m_energyProduction >= 0 && m_energyConsumption >= 0, 
										 ("Energy - Negative Energy numbers, Produce=%d Consume=%d\n",
										 m_energyProduction, m_energyConsumption) );

}  // end removePowerBonus

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
// Private functions
// ------------------------------------------------------------------------------------------------
void Energy::addProduction(Int amt)
{
	m_energyProduction += amt; 

	if( m_owner == NULL )
		return;

	// A repeated Brownout signal does nothing bad, and we need to handle more than just edge cases.
	// Like low power, now even more low power, refresh disable.
	m_owner->onPowerBrownOutChange( !hasSufficientPower() );
}

// ------------------------------------------------------------------------------------------------
void Energy::addConsumption(Int amt)
{
	m_energyConsumption += amt; 

	if( m_owner == NULL )
		return;

	m_owner->onPowerBrownOutChange( !hasSufficientPower() );
}

// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void Energy::crc( Xfer *xfer )
{

}  // end crc

// ------------------------------------------------------------------------------------------------
/** Xfer method
	* Version Info:
	* 1: Initial version */
// ------------------------------------------------------------------------------------------------
void Energy::xfer( Xfer *xfer )
{

	// version
	XferVersion currentVersion = 3;
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );

	// It is actually incorrect to save these, as they are reconstructed when the buildings are loaded
	// I need to version though so old games will load wrong rather than crashing

	// production
	if( version < 2 )
		xfer->xferInt( &m_energyProduction );
	
	// consumption
	if( version < 2 )
		xfer->xferInt( &m_energyConsumption );

	// owning player index
	Int owningPlayerIndex;
	if( xfer->getXferMode() == XFER_SAVE )
		owningPlayerIndex = m_owner->getPlayerIndex();
	xfer->xferInt( &owningPlayerIndex );
	m_owner = ThePlayerList->getNthPlayer( owningPlayerIndex );

	//Sabotage
	if( version >= 3 )
	{
		xfer->xferUnsignedInt( &m_powerSabotagedTillFrame );
	}

}  // end xfer

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void Energy::loadPostProcess( void )
{

}  // end loadPostProcess
