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

// FILE: UndeadBody.cpp ////////////////////////////////////////////////////////////////////////
// Author: Graham Smallwood, June 2003
// Desc:	 First death is intercepted and sets flags and setMaxHealth.  Second death is handled normally.
///////////////////////////////////////////////////////////////////////////////////////////////////

// INCLUDES ///////////////////////////////////////////////////////////////////////////////////////
#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine
#include "Common/Xfer.h"
#include "GameLogic/Module/UndeadBody.h"

#include "GameLogic/Object.h"
#include "GameLogic/Module/SlowDeathBehavior.h"
#include "Lib/FixBoundary.h"

// PUBLIC FUNCTIONS ///////////////////////////////////////////////////////////////////////////////

//-------------------------------------------------------------------------------------------------
void UndeadBodyModuleData::buildFieldParse(MultiIniFieldParse& p) 
{
  ActiveBodyModuleData::buildFieldParse(p);
	static const FieldParse dataFieldParse[] = 
	{
		{ "SecondLifeMaxHealth",			INI::parseFix,	NULL,		FIX_OFFSET( UndeadBodyModuleData, m_secondLifeMaxHealth ) },
		{ 0, 0, 0, 0 }
	};
  p.add(dataFieldParse);
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
UndeadBodyModuleData::UndeadBodyModuleData()
{
	m_secondLifeMaxHealth = Fix( 1 );
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
UndeadBody::UndeadBody( Thing *thing, const ModuleData* moduleData ) 
						 : ActiveBody( thing, moduleData )
{
	m_isSecondLife = FALSE;
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
UndeadBody::~UndeadBody( void )
{

}

// ------------------------------------------------------------------------------------------------
/** A death type that only ordinary slow deaths take is a real death, not a first one.  The Battle
	* Bus leaves SUICIDED and EXTRA_4 to a plain SlowDeathBehavior on purpose, but the second life
	* started that module straight from here, without onDie, so a detonated Demo bus vanished a frame
	* later without its death weapon.  With no slow death at all the second life still happens. */
// ------------------------------------------------------------------------------------------------
static Bool isSecondLifeDeath( Object *obj, const DamageInfo *damageInfo )
{
	Bool anyApplies = FALSE;
	for( BehaviorModule** update = obj->getBehaviorModules(); *update; ++update )
	{
		SlowDeathBehaviorInterface* sdu = (*update)->getSlowDeathBehaviorInterface();
		if( sdu != NULL && sdu->isDieApplicable( damageInfo ) )
		{
			if( sdu->canBeginSecondLife() )
				return TRUE;
			anyApplies = TRUE;
		}
	}
	return !anyApplies;
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void UndeadBody::attemptDamage( DamageInfo *damageInfo )
{
	// If we are on our first life, see if this damage will kill us.  If it will, bind it to one hitpoint
	// remaining, then go ahead and take it.
	Bool shouldStartSecondLife = FALSE;

	/* Whether this hit is lethal has to be asked of the damage that will actually land, not of the
		 number the weapon carries.  Armour, damage type multipliers and the veterancy bonus all sit
		 between the two, so a Battle Bus took its second life off shots that its armour would have
		 shrugged off, and survived ones that were going to kill it. */
	if( damageInfo->in.m_damageType != DAMAGE_UNRESISTABLE
			&& !m_isSecondLife
			&& estimateDamage( damageInfo->in ) >= getHealth()
			&& IsHealthDamagingDamage(damageInfo->in.m_damageType)
			&& isSecondLifeDeath( getObject(), damageInfo )
			)
	{
		// clamp what lands, for the same reason as the test above: armour applied after a raw clamp
		// to health-1 still killed, ran the die modules, and then revived the bus anyway
		Real landed = estimateDamage( damageInfo->in ) * getDamageScalar();
		if( landed > getHealth() - 1 && landed > 0.0f )
			damageInfo->in.m_amount *= ( getHealth() - 1 ) / landed;
		shouldStartSecondLife = TRUE;
	}

	ActiveBody::attemptDamage(damageInfo);

	// After we take it (which allows for damaging special effects), we will do our modifications to the body module
	if( shouldStartSecondLife )
		startSecondLife(damageInfo);
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void UndeadBody::startSecondLife(DamageInfo *damageInfo)
{
	const UndeadBodyModuleData *data = getUndeadBodyModuleData();

	// Flag module as no longer intercepting damage
	m_isSecondLife = TRUE;

	// Modify ActiveBody's max health and initial health
	setMaxHealth(fixToReal(data->m_secondLifeMaxHealth), FULLY_HEAL);	// P6: body health is float

	// Set Armor set flag to use second life armor
	setArmorSetFlag(ARMORSET_SECOND_LIFE);

	// Fire the Slow Death module.  The fact that this is not the result of an onDie will cause the special behavior
	Int total = 0;
	// update is used after the loop; VC6 for-scope let it escape.
	BehaviorModule** update;
	for( update = getObject()->getBehaviorModules(); *update; ++update )
	{
		SlowDeathBehaviorInterface* sdu = (*update)->getSlowDeathBehaviorInterface();
		if (sdu != NULL  && sdu->isDieApplicable(damageInfo) && sdu->canBeginSecondLife() )
		{
			total += sdu->getProbabilityModifier( damageInfo );
		}
	}
	DEBUG_ASSERTCRASH(total > 0, ("Hmm, this is wrong"));


	// this returns a value from 1...total, inclusive
	Int roll = GameLogicRandomValue(1, total);

	for( update = getObject()->getBehaviorModules(); *update; ++update)
	{
		SlowDeathBehaviorInterface* sdu = (*update)->getSlowDeathBehaviorInterface();
		if (sdu != NULL && sdu->isDieApplicable(damageInfo) && sdu->canBeginSecondLife())
		{
			roll -= sdu->getProbabilityModifier( damageInfo );
			if (roll <= 0)
			{
				sdu->beginSlowDeath(damageInfo);
				return;
			}
		}
	}

}


// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void UndeadBody::crc( Xfer *xfer )
{

	// extend base class
	ActiveBody::crc( xfer );

}  // end crc

// ------------------------------------------------------------------------------------------------
/** Xfer method
	* Version Info:
	* 1: Initial version */
// ------------------------------------------------------------------------------------------------
void UndeadBody::xfer( Xfer *xfer )
{

	// version
	XferVersion currentVersion = 1;
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );

	// extend base class
	ActiveBody::xfer( xfer );

	xfer->xferBool(&m_isSecondLife);

}  // end xfer

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void UndeadBody::loadPostProcess( void )
{

	// extend base class
	ActiveBody::loadPostProcess();

}  // end loadPostProcess
