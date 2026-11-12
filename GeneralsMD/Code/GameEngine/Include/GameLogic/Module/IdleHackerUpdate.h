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

// FILE: IdleHackerUpdate.h //////////////////////////////////////////////////////////////////////
//
// A human player's hacker left standing for IdleTime goes into the nearest own Internet Center with
// a free seat within CenterRange, or else starts hacking where it stands.  Any order resets the
// clock, since it leaves the hacker busy for at least one frame.  A stop or an exit the player
// ordered holds it until his next order (HackInternetAIUpdate::isHeldByPlayer).  The computer's
// hackers are put to work by AIPlayer (putToHacking), so this one leaves them alone.
//
///////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#ifndef __IDLE_HACKER_UPDATE_H_
#define __IDLE_HACKER_UPDATE_H_

#include "GameLogic/Module/UpdateModule.h"

//-------------------------------------------------------------------------------------------------
class IdleHackerUpdateModuleData : public UpdateModuleData
{
public:

	DurationFrames m_idleFrames;
	Real m_centerRange;

	IdleHackerUpdateModuleData();

	static void buildFieldParse( MultiIniFieldParse& p );
};

//-------------------------------------------------------------------------------------------------
class IdleHackerUpdate : public UpdateModule
{

	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE( IdleHackerUpdate, "IdleHackerUpdate" )
	MAKE_STANDARD_MODULE_MACRO_WITH_MODULE_DATA( IdleHackerUpdate, IdleHackerUpdateModuleData );

public:

	IdleHackerUpdate( Thing *thing, const ModuleData* moduleData );
	// virtual destructor prototype provided by memory pool declaration

	virtual UpdateSleepTime update();

private:

	Object *findInternetCenter();

	UnsignedInt m_idleSinceFrame;		///< the frame the hacker was last seen busy, contained or not the player's
};

#endif	// __IDLE_HACKER_UPDATE_H_
