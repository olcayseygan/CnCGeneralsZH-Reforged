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

// FILE: SightUpgrade.h //////////////////////////////////////////////////////////////////////////
//
// Turkey's thermal sights: the upgrade sets VisionRange and ShroudClearingRange, and with
// EnablesStealthDetector switches on a StealthDetectorUpdate the unit carries InitiallyDisabled.
//
///////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#ifndef __SIGHT_UPGRADE_H_
#define __SIGHT_UPGRADE_H_

#include "GameLogic/Module/UpgradeModule.h"

//-------------------------------------------------------------------------------------------------
class SightUpgradeModuleData : public UpgradeModuleData
{
public:
	SightUpgradeModuleData( void );
	static void buildFieldParse( MultiIniFieldParse& p );

	Real m_visionRange;
	Real m_shroudClearingRange;
	Bool m_enablesStealthDetector;
};

//-------------------------------------------------------------------------------------------------
class SightUpgrade : public UpgradeModule
{

	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE( SightUpgrade, "SightUpgrade" )
	MAKE_STANDARD_MODULE_MACRO_WITH_MODULE_DATA( SightUpgrade, SightUpgradeModuleData );

public:

	SightUpgrade( Thing *thing, const ModuleData* moduleData );
	// virtual destructor prototype defined by MemoryPoolObject

protected:
	virtual void upgradeImplementation( );
	virtual Bool isSubObjectsUpgrade() { return false; }
};

#endif // __SIGHT_UPGRADE_H_
