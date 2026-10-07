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

// FILE: RadarJamSpecialPower.cpp ////////////////////////////////////////////////////////////////
//
// Radar belongs to a player, not to a place, so "blind a part of the map" reads as: the players
// whose buildings stand in that part go without radar for a while.
//
///////////////////////////////////////////////////////////////////////////////////////////////////

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/Xfer.h"
#include "GameClient/FXList.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object.h"
#include "GameLogic/PartitionManager.h"
#include "GameLogic/Module/RadarJamSpecialPower.h"

// ------------------------------------------------------------------------------------------------
RadarJamSpecialPowerModuleData::RadarJamSpecialPowerModuleData( void )
{
	m_radius = 0.0f;
	m_durationFrames = 0;
	m_fx = NULL;
}

// ------------------------------------------------------------------------------------------------
/*static*/ void RadarJamSpecialPowerModuleData::buildFieldParse( MultiIniFieldParse& p )
{
	SpecialPowerModuleData::buildFieldParse( p );

	static const FieldParse dataFieldParse[] =
	{
		{ "Radius",		INI::parseReal,									NULL, offsetof( RadarJamSpecialPowerModuleData, m_radius ) },
		{ "Duration",	INI::parseDurationUnsignedInt,	NULL, offsetof( RadarJamSpecialPowerModuleData, m_durationFrames ) },
		{ "FX",				INI::parseFXList,								NULL, offsetof( RadarJamSpecialPowerModuleData, m_fx ) },
		{ 0, 0, 0, 0 }
	};
	p.add( dataFieldParse );
}

// ------------------------------------------------------------------------------------------------
RadarJamSpecialPower::RadarJamSpecialPower( Thing *thing, const ModuleData *moduleData )
												: SpecialPowerModule( thing, moduleData )
{
}

// ------------------------------------------------------------------------------------------------
RadarJamSpecialPower::~RadarJamSpecialPower( void )
{
}

// ------------------------------------------------------------------------------------------------
void RadarJamSpecialPower::doSpecialPowerAtLocation( const Coord3D *loc, Real angle, UnsignedInt commandOptions )
{
	if( getObject()->isDisabled() )
		return;

	SpecialPowerModule::doSpecialPowerAtLocation( loc, angle, commandOptions );

	const RadarJamSpecialPowerModuleData *data = getRadarJamSpecialPowerModuleData();
	PartitionFilterRelationship filterEnemies( getObject(), PartitionFilterRelationship::ALLOW_ENEMIES );
	PartitionFilterAcceptByKindOf filterStructures( MAKE_KINDOF_MASK( KINDOF_STRUCTURE ), KINDOFMASK_NONE );
	PartitionFilterAlive filterAlive;
	PartitionFilter *filters[] = { &filterEnemies, &filterStructures, &filterAlive, NULL };
	ObjectIterator *iter = ThePartitionManager->iterateObjectsInRange( loc, data->m_radius, FROM_CENTER_2D, filters );
	MemoryPoolObjectHolder hold( iter );

	UnsignedInt jammedUntil = TheGameLogic->getFrame() + data->m_durationFrames;
	for( Object *obj = iter->first(); obj; obj = iter->next() )
	{
		Player *victim = obj->getControllingPlayer();
		if( victim->hasRadar() )
			DEBUG_LOG(( "RADARJAM frame %d player %d loses radar until %d\n", TheGameLogic->getFrame(), victim->getPlayerIndex(), jammedUntil ));
		victim->jamRadarUntil( jammedUntil );
	}

	FXList::doFXPos( data->m_fx, loc );
}

// ------------------------------------------------------------------------------------------------
void RadarJamSpecialPower::doSpecialPowerAtObject( Object *obj, UnsignedInt commandOptions )
{
	if( getObject()->isDisabled() )
		return;

	doSpecialPowerAtLocation( obj->getPosition(), INVALID_ANGLE, commandOptions );
}

// ------------------------------------------------------------------------------------------------
void RadarJamSpecialPower::crc( Xfer *xfer )
{
	SpecialPowerModule::crc( xfer );
}

// ------------------------------------------------------------------------------------------------
/** Xfer method
	* Version Info:
	* 1: Initial version */
// ------------------------------------------------------------------------------------------------
void RadarJamSpecialPower::xfer( Xfer *xfer )
{
	XferVersion currentVersion = 1;
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );

	SpecialPowerModule::xfer( xfer );
}

// ------------------------------------------------------------------------------------------------
void RadarJamSpecialPower::loadPostProcess( void )
{
	SpecialPowerModule::loadPostProcess();
}
