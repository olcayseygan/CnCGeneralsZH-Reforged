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

// FILE: NeutronMissileSlowDeathUpdate.h //////////////////////////////////////////////////////////
// Author: Colin Day, April 2002
// Desc:   Update module for the neutron missile superweapon object
///////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#ifndef __NEUTRONMISSILESlowDeathBehavior_H_
#define __NEUTRONMISSILESlowDeathBehavior_H_

// INCLUDES ///////////////////////////////////////////////////////////////////////////////////////
#include "GameLogic/Module/SlowDeathBehavior.h"

class FXList;

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
enum NeutronBlast
{ 
	NEUTRON_BLAST_1 = 0,
	NEUTRON_BLAST_2,
	NEUTRON_BLAST_3,
	NEUTRON_BLAST_4,
	NEUTRON_BLAST_5,
	NEUTRON_BLAST_6,
	NEUTRON_BLAST_7,
	NEUTRON_BLAST_8,
	NEUTRON_BLAST_9,

	MAX_NEUTRON_BLASTS  // keep this last
};

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
struct BlastInfo
{
  Bool enabled;					///< this blast is enabled
	DurationFramesReal delay;						///< delay after death to start the regular blast
	DurationFramesReal scorchDelay;			///< delay after death to start a scorch blast
	Real innerRadius;			///< inner radius of damage
	Real outerRadius;			///< outer radius of damage
	Real maxDamage;				///< max amount
	Real minDamage;				///< any object in the outerradius will always have at least this much damage done
	RetailPerFrame toppleSpeed;			///< speed to topple things at
	RetailPerFrame pushForceMag;		///< magnitude of the physics force to push objects
};

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
class NeutronMissileSlowDeathBehaviorModuleData : public SlowDeathBehaviorModuleData
{

public:

	NeutronMissileSlowDeathBehaviorModuleData( void );

	static void buildFieldParse(MultiIniFieldParse& p);

	BlastInfo m_blastInfo[ MAX_NEUTRON_BLASTS ];		///< blast information
	Real m_scorchSize;															///< size of scorch mark
	const FXList *m_fxList;													///< the actual fx list that creates all the visuals.
	VelocityPerFrame m_blastFrontSpeed;													///< dist/frame a blast's ring travels out from the centre, 0 hits the whole radius at once

};

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
class NeutronMissileSlowDeathBehavior : public SlowDeathBehavior
{

	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE( NeutronMissileSlowDeathBehavior, "NeutronMissileSlowDeathBehavior" )
	MAKE_STANDARD_MODULE_MACRO_WITH_MODULE_DATA( NeutronMissileSlowDeathBehavior, NeutronMissileSlowDeathBehaviorModuleData )

public:

	NeutronMissileSlowDeathBehavior( Thing *thing, const ModuleData* moduleData );
	// virtual destructor prototype provided by memory pool declaration

	virtual UpdateSleepTime update( void );				 ///< the update call

protected:

	void doBlast( const BlastInfo *blastInfo, Real ringOuter, ObjectIDVector *hit );	///< do blast to objects within ringOuter not already in hit
	void doScorchBlast( const BlastInfo *blastInfo );  ///< do a scorch blast ring

	UnsignedInt m_activationFrame;									///< frame we were activated on
	Bool m_completedBlasts[ MAX_NEUTRON_BLASTS ];		///< blasts indexes we've already done
	Bool m_completedScorchBlasts[ MAX_NEUTRON_BLASTS ];		///< scorch blast indexes we've already done
	Bool m_scorchPlaced;														///< TRUE once we've placed the scorch mark
	ObjectIDVector m_hitObjects[ MAX_NEUTRON_BLASTS ];	///< sorted ids a moving blast front has already hit, emptied when the blast completes

};

#endif  // end __NEUTRONMISSILESlowDeathBehavior_H_
