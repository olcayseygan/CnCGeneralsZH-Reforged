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

// FILE: RadarCoverageBehavior.h /////////////////////////////////////////////////////////////////
//
// Turkiye's radar: a circle of Radius in which its own and its allies' radar-guided rockets keep
// their lock (WeaponTemplate RadarBlindHitChance, Weapon.cpp).
//
///////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#ifndef __RADAR_COVERAGE_BEHAVIOR_H_
#define __RADAR_COVERAGE_BEHAVIOR_H_

#include "GameLogic/Module/BehaviorModule.h"

class Player;

//-------------------------------------------------------------------------------------------------
class RadarCoverageBehaviorModuleData : public BehaviorModuleData
{
public:

	Real m_radius;

	RadarCoverageBehaviorModuleData();

	static void buildFieldParse( MultiIniFieldParse& p );
};

//-------------------------------------------------------------------------------------------------
class RadarCoverageBehavior : public BehaviorModule
{

	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE( RadarCoverageBehavior, "RadarCoverageBehavior" )
	MAKE_STANDARD_MODULE_MACRO_WITH_MODULE_DATA( RadarCoverageBehavior, RadarCoverageBehaviorModuleData );

public:

	RadarCoverageBehavior( Thing *thing, const ModuleData* moduleData );
	// virtual destructor prototype provided by memory pool declaration

	/// a working radar of this player or an ally covers this point
	static Bool isCovered( const Player *player, const Coord3D *pos );

private:

	/* Every radar in the game, in no order that matters: isCovered only asks whether any of them
		 reaches.  The constructor and destructor keep it, so a loaded game rebuilds it as its objects
		 are made and there is nothing to save. */
	static RadarCoverageBehavior *s_first;
	RadarCoverageBehavior *m_next;
};

#endif	// __RADAR_COVERAGE_BEHAVIOR_H_
