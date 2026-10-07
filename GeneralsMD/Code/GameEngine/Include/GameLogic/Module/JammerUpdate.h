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

// FILE: JammerUpdate.h //////////////////////////////////////////////////////////////////////////
//
// Turkey's Koral.  Within Radius, enemy drones and spotters see only what is under them, and an
// enemy guided missile loses its lock and comes down scattered (MissileAIUpdate::projectileNowJammed).
//
///////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#ifndef __JAMMER_UPDATE_H_
#define __JAMMER_UPDATE_H_

#include "GameLogic/Module/UpdateModule.h"

//-------------------------------------------------------------------------------------------------
class JammerUpdateModuleData : public UpdateModuleData
{
public:

	Real m_radius;
	UnsignedInt m_scanFrames;

	JammerUpdateModuleData();

	static void buildFieldParse( MultiIniFieldParse& p );
};

//-------------------------------------------------------------------------------------------------
class JammerUpdate : public UpdateModule
{

	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE( JammerUpdate, "JammerUpdate" )
	MAKE_STANDARD_MODULE_MACRO_WITH_MODULE_DATA( JammerUpdate, JammerUpdateModuleData );

public:

	JammerUpdate( Thing *thing, const ModuleData* moduleData );
	// virtual destructor prototype provided by memory pool declaration

	virtual UpdateSleepTime update();
	virtual void onDelete( void );
	virtual DisabledMaskType getDisabledTypesToProcess() const { return DISABLEDMASK_ALL; }

private:

	void releaseAll();

	std::vector<ObjectID> m_jammed;		///< whose sight this one is holding down, in the order it took them
};

#endif	// __JAMMER_UPDATE_H_
