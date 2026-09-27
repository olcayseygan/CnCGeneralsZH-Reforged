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

// FILE: MaxHealthUpgrade.cpp /////////////////////////////////////////////////////////////////////////////
// Author: Kris Morness, September 2002
// Desc:	 UpgradeModule that adds a scalar to the object's experience gain.
///////////////////////////////////////////////////////////////////////////////////////////////////

// INCLUDES ///////////////////////////////////////////////////////////////////////////////////////
#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#define DEFINE_MAXHEALTHCHANGETYPE_NAMES
#include "Common/Xfer.h"
#include "GameLogic/Object.h"
#include "GameLogic/ExperienceTracker.h"
#include "GameLogic/Module/MaxHealthUpgrade.h"
#include "GameLogic/Module/BodyModule.h"
#include "Lib/FixBoundary.h"

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
MaxHealthUpgradeModuleData::MaxHealthUpgradeModuleData( void )
{
	m_addMaxHealth = Fix( 0 );
	m_maxHealthChangeType = SAME_CURRENTHEALTH;
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
void MaxHealthUpgradeModuleData::buildFieldParse(MultiIniFieldParse& p) 
{

  UpgradeModuleData::buildFieldParse( p );

	static const FieldParse dataFieldParse[] = 
	{
		{ "AddMaxHealth",					INI::parseFix,					NULL,										FIX_OFFSET( MaxHealthUpgradeModuleData, m_addMaxHealth ) },
		{ "ChangeType",						INI::parseIndexList,		TheMaxHealthChangeTypeNames, offsetof( MaxHealthUpgradeModuleData, m_maxHealthChangeType ) },
		{ 0, 0, 0, 0 }
	};

  p.add(dataFieldParse);

}  // end buildFieldParse

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
MaxHealthUpgrade::MaxHealthUpgrade( Thing *thing, const ModuleData* moduleData ) : UpgradeModule( thing, moduleData )
{
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
MaxHealthUpgrade::~MaxHealthUpgrade( void )
{
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
void MaxHealthUpgrade::upgradeImplementation( )
{
	const MaxHealthUpgradeModuleData *data = getMaxHealthUpgradeModuleData();

	Object *obj = getObject();

	BodyModuleInterface *body = obj->getBodyModule();
	if( body )
	{
		// Veterancy multiplies max health when it arrives, so an addition made after a promotion has
		// to carry the promotion's bonus too. Without it an Elite Paladin that researched Composite
		// Armor ended on 750 where one armored first and promoted later ended on 780.
		Real add = fixToReal( data->m_addMaxHealth ) * TheGlobalData->m_healthBonus[ obj->getVeterancyLevel() ];
		body->setMaxHealth( body->getMaxHealth() + add, data->m_maxHealthChangeType );	// P6: body health is float
	}
}

// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void MaxHealthUpgrade::crc( Xfer *xfer )
{

	// extend base class
	UpgradeModule::crc( xfer );

}  // end crc

// ------------------------------------------------------------------------------------------------
/** Xfer method
	* Version Info:
	* 1: Initial version */
// ------------------------------------------------------------------------------------------------
void MaxHealthUpgrade::xfer( Xfer *xfer )
{

	// version
	XferVersion currentVersion = 1;
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );

	// extend base class
	UpgradeModule::xfer( xfer );

}  // end xfer

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void MaxHealthUpgrade::loadPostProcess( void )
{

	// extend base class
	UpgradeModule::loadPostProcess();

}  // end loadPostProcess
