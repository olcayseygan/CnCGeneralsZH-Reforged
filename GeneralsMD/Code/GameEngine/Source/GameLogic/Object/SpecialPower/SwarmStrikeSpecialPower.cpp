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

// FILE: SwarmStrikeSpecialPower.cpp /////////////////////////////////////////////////////////////
//
// Turkiye's loitering munitions.  A nuke burns one spot; this spends one munition on each of the
// most expensive targets in a wide circle, so a spread-out army hurts more than a packed base.
//
///////////////////////////////////////////////////////////////////////////////////////////////////

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "Common/Player.h"
#include "Common/ThingFactory.h"
#include "Common/ThingTemplate.h"
#include "Common/Xfer.h"
#include "GameClient/FXList.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object.h"
#include "GameLogic/Weapon.h"
#include "GameLogic/Module/ParkingPlaceBehavior.h"
#include "GameLogic/Module/SwarmStrikeSpecialPower.h"

// ------------------------------------------------------------------------------------------------
SwarmStrikeSpecialPowerModuleData::SwarmStrikeSpecialPowerModuleData( void )
{
	m_radius = 0.0f;
	m_targetCount = 1;
	m_repeatTargets = FALSE;
	m_weapon = NULL;
	m_targetFX = NULL;
}

// ------------------------------------------------------------------------------------------------
static void parseUpgradeTargetCount( INI *ini, void * /*instance*/, void *store, const void * /*userData*/ )
{
	SwarmStrikeSpecialPowerModuleData::UpgradeTargetCount up;
	INI::parseScience( ini, NULL, &up.m_science, NULL );
	INI::parseInt( ini, NULL, &up.m_count, NULL );
	((std::vector<SwarmStrikeSpecialPowerModuleData::UpgradeTargetCount> *)store)->push_back( up );
}

// ------------------------------------------------------------------------------------------------
/*static*/ void SwarmStrikeSpecialPowerModuleData::buildFieldParse( MultiIniFieldParse& p )
{
	SpecialPowerModuleData::buildFieldParse( p );

	static const FieldParse dataFieldParse[] =
	{
		{ "Radius",				INI::parseReal,						NULL, offsetof( SwarmStrikeSpecialPowerModuleData, m_radius ) },
		{ "TargetCount",	INI::parseInt,						NULL, offsetof( SwarmStrikeSpecialPowerModuleData, m_targetCount ) },
		{ "UpgradeTargetCount",	parseUpgradeTargetCount,	NULL, offsetof( SwarmStrikeSpecialPowerModuleData, m_upgradeTargetCount ) },
		{ "RepeatTargets",	INI::parseBool,					NULL, offsetof( SwarmStrikeSpecialPowerModuleData, m_repeatTargets ) },
		{ "Weapon",				INI::parseWeaponTemplate,	NULL, offsetof( SwarmStrikeSpecialPowerModuleData, m_weapon ) },
		{ "TargetFX",			INI::parseFXList,					NULL, offsetof( SwarmStrikeSpecialPowerModuleData, m_targetFX ) },
		{ "Drone",				INI::parseAsciiString,		NULL, offsetof( SwarmStrikeSpecialPowerModuleData, m_droneName ) },
		{ 0, 0, 0, 0 }
	};
	p.add( dataFieldParse );
}

// ------------------------------------------------------------------------------------------------
SwarmStrikeSpecialPower::SwarmStrikeSpecialPower( Thing *thing, const ModuleData *moduleData )
												: SpecialPowerModule( thing, moduleData )
{
}

// ------------------------------------------------------------------------------------------------
SwarmStrikeSpecialPower::~SwarmStrikeSpecialPower( void )
{
}

// ------------------------------------------------------------------------------------------------
/** One shot, at the target or else at the point: the Weapon, or a Drone scrambled from the
	* building's runways, which flies there and dives in. */
// ------------------------------------------------------------------------------------------------
static void fireShot( const SwarmStrikeSpecialPowerModuleData *data, Object *source, Object *target, const Coord3D *pos )
{
	if( data->m_droneName.isEmpty() )
	{
		if( target )
			TheWeaponStore->createAndFireTempWeapon( data->m_weapon, source, target );
		else
			TheWeaponStore->createAndFireTempWeapon( data->m_weapon, source, pos );
		return;
	}

	static NameKeyType parkingKey = TheNameKeyGenerator->nameToKey( "ParkingPlaceBehavior" );
	ParkingPlaceBehavior *airfield = (ParkingPlaceBehavior *)source->findUpdateModule( parkingKey );
	airfield->scramble( TheThingFactory->findTemplate( data->m_droneName ), target ? target->getID() : INVALID_ID,
		target ? target->getPosition() : pos );
}

// ------------------------------------------------------------------------------------------------
void SwarmStrikeSpecialPower::doSpecialPowerAtLocation( const Coord3D *loc, Real angle, UnsignedInt commandOptions )
{
	if( getObject()->isDisabled() )
		return;

	SpecialPowerModule::doSpecialPowerAtLocation( loc, angle, commandOptions );

	const SwarmStrikeSpecialPowerModuleData *data = getSwarmStrikeSpecialPowerModuleData();
	Int targetCount = data->m_targetCount;
	for( size_t i = 0; i < data->m_upgradeTargetCount.size(); ++i )
	{
		if( getObject()->getControllingPlayer()->hasScience( data->m_upgradeTargetCount[ i ].m_science ) )
		{
			targetCount = data->m_upgradeTargetCount[ i ].m_count;
			break;
		}
	}

	std::vector<Object*> targets;
	Weapon_findMostValuableEnemies( getObject(), loc, data->m_radius, targetCount, targets );

	for( std::vector<Object*>::iterator it = targets.begin(); it != targets.end(); ++it )
	{
		DEBUG_LOG(( "SWARM '%s' picks '%s' %d\n", getPowerName().str(), (*it)->getTemplate()->getName().str(), (*it)->getID() ));
		FXList::doFXPos( data->m_targetFX, (*it)->getPosition() );
		fireShot( data, getObject(), *it, NULL );
	}

	if( !data->m_repeatTargets )
		return;

	// the spare shots: round the same targets again, dearest first, or at the click when none stood there
	for( Int shot = (Int)targets.size(); shot < targetCount; ++shot )
		fireShot( data, getObject(), targets.empty() ? NULL : targets[ shot % targets.size() ], loc );
	DEBUG_LOG(( "SWARM '%s' frame %d sends %d at %d targets around (%.0f,%.0f)\n", getPowerName().str(),
		TheGameLogic->getFrame(), targetCount, (Int)targets.size(), loc->x, loc->y ));
}

// ------------------------------------------------------------------------------------------------
void SwarmStrikeSpecialPower::doSpecialPowerAtObject( Object *obj, UnsignedInt commandOptions )
{
	if( getObject()->isDisabled() )
		return;

	doSpecialPowerAtLocation( obj->getPosition(), INVALID_ANGLE, commandOptions );
}

// ------------------------------------------------------------------------------------------------
void SwarmStrikeSpecialPower::crc( Xfer *xfer )
{
	SpecialPowerModule::crc( xfer );
}

// ------------------------------------------------------------------------------------------------
/** Xfer method
	* Version Info:
	* 1: Initial version */
// ------------------------------------------------------------------------------------------------
void SwarmStrikeSpecialPower::xfer( Xfer *xfer )
{
	XferVersion currentVersion = 1;
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );

	SpecialPowerModule::xfer( xfer );
}

// ------------------------------------------------------------------------------------------------
void SwarmStrikeSpecialPower::loadPostProcess( void )
{
	SpecialPowerModule::loadPostProcess();
}
