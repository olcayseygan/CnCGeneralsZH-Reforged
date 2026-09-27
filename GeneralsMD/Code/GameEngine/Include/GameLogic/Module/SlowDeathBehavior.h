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

// FILE: SlowDeathBehavior.h /////////////////////////////////////////////////////////////////////////
// Author: Colin Day, December 2001
// Desc:   Update that will count down a lifetime and destroy object when it reaches zero
///////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#ifndef __SlowDeathBehavior_H_
#define __SlowDeathBehavior_H_

// INCLUDES ///////////////////////////////////////////////////////////////////////////////////////
#include "GameLogic/Module/BehaviorModule.h"
#include "GameLogic/Module/DieModule.h"
#include "GameLogic/Module/UpdateModule.h"

class FXList;
class ObjectCreationList;
class WeaponTemplate;
class DamageInfo;

//-------------------------------------------------------------------------------------------------
typedef std::vector<const FXList*> FXListVec;
typedef std::vector<const ObjectCreationList*> OCLVec;
typedef std::vector<const WeaponTemplate*> WeaponTemplateVec;

//-------------------------------------------------------------------------------------------------
enum SlowDeathPhaseType
{
	SDPHASE_INITIAL = 0,
	SDPHASE_MIDPOINT,
	SDPHASE_FINAL,

	SD_PHASE_COUNT	// keep last
};

#ifdef DEFINE_SLOWDEATHPHASE_NAMES
static const char *TheSlowDeathPhaseNames[] = 
{
	"INITIAL",
	"MIDPOINT",
	"FINAL",

	NULL
};
#endif


//-------------------------------------------------------------------------------------------------
class SlowDeathBehaviorModuleData : public UpdateModuleData
{
public:
	DieMuxData				m_dieMuxData;
	Fix								m_sinkRate;
	Int								m_probabilityModifier;
	Fix								m_modifierBonusPerOverkillPercent;
	UnsignedInt				m_sinkDelay;
	UnsignedInt				m_sinkDelayVariance;
	Fix								m_destructionAltitude;
	UnsignedInt				m_destructionDelay;
	UnsignedInt				m_destructionDelayVariance;
	FXListVec					m_fx[SD_PHASE_COUNT];
	OCLVec						m_ocls[SD_PHASE_COUNT];
	WeaponTemplateVec	m_weapons[SD_PHASE_COUNT];
	Fix								m_flingForce;
	Fix								m_flingForceVariance;
	Fix								m_flingPitch;
	Fix								m_flingPitchVariance;

	enum 
	{
		//flags used by m_maskOfLoadedEffects
		HAS_FX							= 1,
		HAS_OCL							= 2,
		HAS_WEAPON					= 4,
		HAS_NON_LOD_EFFECTS = (HAS_OCL | HAS_WEAPON)
	};
	Byte							m_maskOfLoadedEffects;	///<used to keep track of whether m_fx,m_ocls,m_weapons actually contain anything.

	SlowDeathBehaviorModuleData();
	static void buildFieldParse(MultiIniFieldParse& p);
	inline Bool hasNonLodEffects() const
	{
		return (m_maskOfLoadedEffects & SlowDeathBehaviorModuleData::HAS_NON_LOD_EFFECTS) != 0;
	}

private:

};

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
class SlowDeathBehaviorInterface
{
public:
	virtual void beginSlowDeath( const DamageInfo *damageInfo ) = 0;
	virtual Int getProbabilityModifier( const DamageInfo *damageInfo ) const = 0;
	virtual Bool isDieApplicable(const DamageInfo *damageInfo) const = 0;
	virtual Bool canBeginSecondLife() const = 0;	///< can this module turn an UndeadBody's first death into its second life
};

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
class SlowDeathBehavior : public UpdateModule, 
													public DieModuleInterface,
													public SlowDeathBehaviorInterface
{

	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE( SlowDeathBehavior, "SlowDeathBehavior" )
	MAKE_STANDARD_MODULE_MACRO_WITH_MODULE_DATA( SlowDeathBehavior, SlowDeathBehaviorModuleData )

public:

	SlowDeathBehavior( Thing *thing, const ModuleData* moduleData );
	// virtual destructor prototype provided by memory pool declaration

	static Int getInterfaceMask() { return UpdateModule::getInterfaceMask() | (MODULEINTERFACE_DIE); }

	// BehaviorModule
	virtual DieModuleInterface* getDie() { return this; }

	// UpdateModuleInterface
	virtual UpdateSleepTime update();	
	virtual SlowDeathBehaviorInterface* getSlowDeathBehaviorInterface() { return this; }
	// Disabled conditions to process -- all
	virtual DisabledMaskType getDisabledTypesToProcess() const { return DISABLEDMASK_ALL; }

	// DieModuleInterface
	virtual void onDie( const DamageInfo *damageInfo );

	// SlowDeathBehaviorInterface
	virtual void beginSlowDeath( const DamageInfo *damageInfo );
	virtual Int getProbabilityModifier( const DamageInfo *damageInfo ) const;
	virtual Bool isDieApplicable(const DamageInfo *damageInfo) const { return getSlowDeathBehaviorModuleData()->m_dieMuxData.isDieApplicable(getObject(), damageInfo); }
	virtual Bool canBeginSecondLife() const { return FALSE; }

protected:

	void doPhaseStuff(SlowDeathPhaseType sdphase);
	inline Bool isSlowDeathActivated() const { return (m_flags & (1<<SLOW_DEATH_ACTIVATED)) != 0; }
	inline UnsignedInt getDestructionFrame() const { return m_destructionFrame; }

	/** Move the moment of destruction, for a subclass whose death ends somewhere rather than at a
		* time - an aircraft's ends at the ground. */
	inline void setDestructionFrame(UnsignedInt frame) { m_destructionFrame = frame; }

private:
	
	enum
	{
		SLOW_DEATH_ACTIVATED,
		MIDPOINT_EXECUTED,
		FLUNG_INTO_AIR,
		BOUNCED
	};

	UnsignedInt m_sinkFrame;							///< Frame to be sunken into the ground on
	UnsignedInt m_midpointFrame;					///< The midpoint is between .25 through life and .75 through life (eg)
	UnsignedInt m_destructionFrame;	
	Real				m_acceleratedTimeScale;		///<used to speedup deaths when needed to improve game performance.
	UnsignedInt	m_flags;
};

#endif // __SlowDeathBehavior_H_

