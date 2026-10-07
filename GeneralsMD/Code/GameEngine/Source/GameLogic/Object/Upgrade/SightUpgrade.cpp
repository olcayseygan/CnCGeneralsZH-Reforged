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

// FILE: SightUpgrade.cpp ////////////////////////////////////////////////////////////////////////

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "Common/Xfer.h"
#include "GameLogic/Object.h"
#include "GameLogic/Module/SightUpgrade.h"
#include "GameLogic/Module/StealthDetectorUpdate.h"

//-------------------------------------------------------------------------------------------------
SightUpgradeModuleData::SightUpgradeModuleData( void )
{
	m_visionRange = 0.0f;
	m_shroudClearingRange = 0.0f;
	m_enablesStealthDetector = FALSE;
}

//-------------------------------------------------------------------------------------------------
void SightUpgradeModuleData::buildFieldParse( MultiIniFieldParse& p )
{
	UpgradeModuleData::buildFieldParse( p );

	static const FieldParse dataFieldParse[] =
	{
		{ "VisionRange",						INI::parseReal, NULL, offsetof( SightUpgradeModuleData, m_visionRange ) },
		{ "ShroudClearingRange",		INI::parseReal, NULL, offsetof( SightUpgradeModuleData, m_shroudClearingRange ) },
		{ "EnablesStealthDetector",	INI::parseBool, NULL, offsetof( SightUpgradeModuleData, m_enablesStealthDetector ) },
		{ 0, 0, 0, 0 }
	};
	p.add( dataFieldParse );
}

//-------------------------------------------------------------------------------------------------
SightUpgrade::SightUpgrade( Thing *thing, const ModuleData* moduleData ) : UpgradeModule( thing, moduleData )
{
}

//-------------------------------------------------------------------------------------------------
SightUpgrade::~SightUpgrade( void )
{
}

//-------------------------------------------------------------------------------------------------
void SightUpgrade::upgradeImplementation( )
{
	const SightUpgradeModuleData *data = getSightUpgradeModuleData();
	Object *obj = getObject();

	obj->setVisionRange( data->m_visionRange );
	obj->setShroudClearingRange( data->m_shroudClearingRange );

	if( data->m_enablesStealthDetector )
	{
		static NameKeyType key_StealthDetectorUpdate = NAMEKEY( "StealthDetectorUpdate" );
		StealthDetectorUpdate *detector = (StealthDetectorUpdate*)obj->findUpdateModule( key_StealthDetectorUpdate );
		DEBUG_ASSERTCRASH( detector, ("SightUpgrade on '%s' enables a StealthDetectorUpdate it does not have", obj->getTemplate()->getName().str()) );
		detector->setSDEnabled( true );
	}
}

// ------------------------------------------------------------------------------------------------
void SightUpgrade::crc( Xfer *xfer )
{
	UpgradeModule::crc( xfer );
}

// ------------------------------------------------------------------------------------------------
/** Xfer method
	* Version Info:
	* 1: Initial version */
// ------------------------------------------------------------------------------------------------
void SightUpgrade::xfer( Xfer *xfer )
{
	XferVersion currentVersion = 1;
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );

	UpgradeModule::xfer( xfer );
}

// ------------------------------------------------------------------------------------------------
void SightUpgrade::loadPostProcess( void )
{
	UpgradeModule::loadPostProcess();
}
