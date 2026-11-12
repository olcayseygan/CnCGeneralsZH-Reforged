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

// FILE: CleanupHazardUpdate.cpp //////////////////////////////////////////////////////////////////////////
// Author: Kris Morness, August 2002
// Desc:   Update module to handle independent targeting of hazards to cleanup.
///////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#ifndef __CLEANUP_HAZARD_UPDATE_H_
#define __CLEANUP_HAZARD_UPDATE_H_

// INCLUDES ///////////////////////////////////////////////////////////////////////////////////////
#include "Common/KindOf.h"
#include "GameLogic/Module/UpdateModule.h"

// FORWARD REFERENCES /////////////////////////////////////////////////////////////////////////////
class ThingTemplate;
class WeaponTemplate;


//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
class CleanupHazardUpdateModuleData : public ModuleData
{
public:
	WeaponSlotType	m_weaponSlot;
	DurationFrames			m_scanFrames;
	Real						m_scanRange;

	CleanupHazardUpdateModuleData();
	static void buildFieldParse(MultiIniFieldParse& p);

private: 

};

//-------------------------------------------------------------------------------------------------
/** The default	update module */
//-------------------------------------------------------------------------------------------------
class CleanupHazardUpdate : public UpdateModule
{

	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE( CleanupHazardUpdate, "CleanupHazardUpdate" )
	MAKE_STANDARD_MODULE_MACRO_WITH_MODULE_DATA( CleanupHazardUpdate, CleanupHazardUpdateModuleData );

public:

	CleanupHazardUpdate( Thing *thing, const ModuleData* moduleData );
	// virtual destructor prototype provided by memory pool declaration

	virtual void onObjectCreated();
	virtual UpdateSleepTime update();

	Object* scanClosestTarget();
	void fireWhenReady();

	/// This allows the unit to cleanup an area until clean, then the AI goes idle.  forgetGivenUp: a player's
	/// order, which tries the ground it gave up on again; the computer's leaves it given up
	void setCleanupAreaParameters( const Coord3D *pos, Real range, Bool forgetGivenUp );
	Bool isCleaningArea() const { return m_moveRange > 0.0f; }	///< out cleaning an area, the cleanup ability's or one it noticed by itself
	Object *closestHazard( const Coord3D *from, Real range ) const;	///< the nearest hazard within range, none on the ground it gave up on
	void giveUpArea();	///< leave the ground round the hazard it is after alone from now on, as if it could not reach it

protected:

	void startAutoCleanup();	///< idle or on guard with a hazard near: clean it, then come back
	void finishArea( const char *why, Bool goHome );	///< the area is done with: back on guard, home, or idle

	ObjectID m_bestTargetID;
	Bool m_inRange;
	Int m_nextScanFrames;
	Int m_nextShotAvailableInFrames;
	const WeaponTemplate *m_weaponTemplate;

	//Cleanup area (temporary values).
	Coord3D m_pos;
	Real		m_moveRange;

	//A cleanup it started by itself took the unit off guard: it guards m_pos again when the area is clean.
	Bool		m_resumeGuard;
	Int			m_resumeGuardMode;	///< a GuardMode
	Bool		m_returning;				///< the area is clean and the walk back to m_pos has been ordered
	Int			m_approaches;				///< scans in a row out of reach of its hazard, standing about, and no nearer to it
	Bool		m_approaching;			///< m_approachPos holds the ground m_approaches counts for
	Coord3D	m_approachPos;			///< where the hazard it is after stood when the count began; any within GIVEN_UP_RADIUS is the same ground
	Coord3D	m_lastScanPos;			///< where it stood on the last scan that counted
	Real		m_closestApproach;	///< the nearest it has come to a hazard on that ground
	UnsignedInt	m_progressFrame;	///< the last frame of progress in the area: started, nearer, a hazard hosed or gone
	ObjectID	m_lastTargetID;		///< the hazard the last scan found
	ObjectID	m_resumeGuardObject;	///< the object it was guarding when a cleanup took it off guard, or INVALID_ID for a spot
	Bool		m_hasGivenUp;				///< m_givenUpPos holds ground it gave up on
	Coord3D	m_givenUpPos;				///< where the last hazard it gave up on as out of reach stood; none near it is gone for again
};


#endif

