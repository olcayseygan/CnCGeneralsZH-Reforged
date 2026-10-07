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

// FILE: SwarmStrikeSpecialPower.h ///////////////////////////////////////////////////////////////
//
// Turkiye's loitering munitions: within Radius of the click, the TargetCount most expensive enemies
// on the ground each take one Weapon shot, and TargetFX plays on each of them.
//
///////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#ifndef __SWARM_STRIKE_SPECIAL_POWER_H_
#define __SWARM_STRIKE_SPECIAL_POWER_H_

#include "GameLogic/Module/SpecialPowerModule.h"

class FXList;
class WeaponTemplate;

//-------------------------------------------------------------------------------------------------
class SwarmStrikeSpecialPowerModuleData : public SpecialPowerModuleData
{
public:

	SwarmStrikeSpecialPowerModuleData( void );

	static void buildFieldParse( MultiIniFieldParse& p );

	Real m_radius;
	Int m_targetCount;
	const WeaponTemplate *m_weapon;
	const FXList *m_targetFX;
};

//-------------------------------------------------------------------------------------------------
class SwarmStrikeSpecialPower : public SpecialPowerModule
{

	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE( SwarmStrikeSpecialPower, "SwarmStrikeSpecialPower" )
	MAKE_STANDARD_MODULE_MACRO_WITH_MODULE_DATA( SwarmStrikeSpecialPower, SwarmStrikeSpecialPowerModuleData )

public:

	SwarmStrikeSpecialPower( Thing *thing, const ModuleData *moduleData );
	// virtual destructor provided by memory pool object

	virtual void doSpecialPowerAtObject( Object *obj, UnsignedInt commandOptions );
	virtual void doSpecialPowerAtLocation( const Coord3D *loc, Real angle, UnsignedInt commandOptions );
};

#endif  // end __SWARM_STRIKE_SPECIAL_POWER_H_
