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

// FILE: MarkAndRainBehavior.h ///////////////////////////////////////////////////////////////////
//
// Turkiye's "mark and rain": when an enemy dies where an eye of the gun's player or an ally holds
// it (a radar circle, RadarCoverageBehavior::isCovered, or a SpotterSight unit that sees that far),
// every gun carrying this module that reaches the spot gets BonusConditionType for BonusDuration
// through the object's TempWeaponBonusHelper, which also tints it.  Object::onDie calls onDeath.
//
///////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#ifndef __MARK_AND_RAIN_BEHAVIOR_H_
#define __MARK_AND_RAIN_BEHAVIOR_H_

#include "GameLogic/Module/BehaviorModule.h"

enum WeaponBonusConditionType : int;
class FXList;

//-------------------------------------------------------------------------------------------------
class MarkAndRainBehaviorModuleData : public BehaviorModuleData
{
public:

	WeaponBonusConditionType m_bonusConditionType;
	DurationFrames m_bonusDuration;
	const FXList *m_fx;			///< played on the gun each time a burst starts or restarts

	MarkAndRainBehaviorModuleData();

	static void buildFieldParse( MultiIniFieldParse& p );
};

//-------------------------------------------------------------------------------------------------
class MarkAndRainBehavior : public BehaviorModule
{

	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE( MarkAndRainBehavior, "MarkAndRainBehavior" )
	MAKE_STANDARD_MODULE_MACRO_WITH_MODULE_DATA( MarkAndRainBehavior, MarkAndRainBehaviorModuleData );

public:

	MarkAndRainBehavior( Thing *thing, const ModuleData* moduleData );
	// virtual destructor prototype provided by memory pool declaration

	/// victim has just died (Object::onDie): start the burst on every gun that qualifies
	static void onDeath( const Object *victim );

private:

	/* Every gun in the game, kept by the constructor and destructor, so a loaded game rebuilds it
		 and there is nothing to save.  onDeath sorts the guns it picks by ObjectID before touching
		 them, so the list's own order (which a load changes) never reaches the simulation. */
	static MarkAndRainBehavior *s_first;
	MarkAndRainBehavior *m_next;
};

#endif	// __MARK_AND_RAIN_BEHAVIOR_H_
