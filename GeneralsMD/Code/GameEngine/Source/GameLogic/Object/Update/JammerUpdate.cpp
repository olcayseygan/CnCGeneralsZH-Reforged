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

// FILE: JammerUpdate.cpp ////////////////////////////////////////////////////////////////////////
//
// Each scan takes the enemy drones and spotters inside the field and lets go of the ones that left
// it.  The hold is a count on the victim (Object::addSightJammer), so two Korals over one drone do
// not undo each other, and a dead or switched-off Koral lets go of everything it held.
//
///////////////////////////////////////////////////////////////////////////////////////////////////

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include <algorithm>

#include "Common/ThingTemplate.h"
#include "Common/Xfer.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object.h"
#include "GameLogic/PartitionManager.h"
#include "GameLogic/Module/JammerUpdate.h"

//-------------------------------------------------------------------------------------------------
JammerUpdateModuleData::JammerUpdateModuleData()
{
	m_radius = 0.0f;
	m_scanFrames = LOGICFRAMES_PER_SECOND / 6;
}

//-------------------------------------------------------------------------------------------------
/*static*/ void JammerUpdateModuleData::buildFieldParse( MultiIniFieldParse& p )
{
	UpdateModuleData::buildFieldParse( p );

	static const FieldParse dataFieldParse[] =
	{
		{ "Radius",		INI::parseReal,									NULL, offsetof( JammerUpdateModuleData, m_radius ) },
		{ "ScanRate",	INI::parseDurationUnsignedInt,	NULL, offsetof( JammerUpdateModuleData, m_scanFrames ) },
		{ 0, 0, 0, 0 }
	};
	p.add( dataFieldParse );
}

//-------------------------------------------------------------------------------------------------
JammerUpdate::JammerUpdate( Thing *thing, const ModuleData* moduleData ) : UpdateModule( thing, moduleData )
{
}

//-------------------------------------------------------------------------------------------------
JammerUpdate::~JammerUpdate( void )
{
}

//-------------------------------------------------------------------------------------------------
static Bool isInList( const std::vector<ObjectID>& ids, ObjectID id )
{
	return std::find( ids.begin(), ids.end(), id ) != ids.end();
}

//-------------------------------------------------------------------------------------------------
void JammerUpdate::releaseAll()
{
	for( std::vector<ObjectID>::const_iterator it = m_jammed.begin(); it != m_jammed.end(); ++it )
	{
		Object *victim = TheGameLogic->findObjectByID( *it );
		if( victim )
			victim->removeSightJammer();
	}
	m_jammed.clear();
}

//-------------------------------------------------------------------------------------------------
void JammerUpdate::onDelete( void )
{
	releaseAll();
}

//-------------------------------------------------------------------------------------------------
UpdateSleepTime JammerUpdate::update( void )
{
	const JammerUpdateModuleData *data = getJammerUpdateModuleData();
	Object *me = getObject();

	if( me->isEffectivelyDead() )
	{
		releaseAll();
		return UPDATE_SLEEP_FOREVER;
	}

	if( me->isDisabled() || me->testStatus( OBJECT_STATUS_UNDER_CONSTRUCTION ) )
	{
		releaseAll();
		return UPDATE_SLEEP( data->m_scanFrames );
	}

	PartitionFilterRelationship filterEnemies( me, PartitionFilterRelationship::ALLOW_ENEMIES );
	PartitionFilterAlive filterAlive;
	PartitionFilterSameMapStatus filterMapStatus( me );
	PartitionFilter *filters[] = { &filterEnemies, &filterAlive, &filterMapStatus, NULL };
	ObjectIterator *iter = ThePartitionManager->iterateObjectsInRange( me, data->m_radius, FROM_CENTER_2D, filters );
	MemoryPoolObjectHolder hold( iter );

	std::vector<ObjectID> inField;
	for( Object *obj = iter->first(); obj; obj = iter->next() )
	{
		ProjectileUpdateInterface *projectile = obj->getProjectileUpdateInterface();
		if( projectile )
		{
			DEBUG_LOG(( "JAMMER frame %d '%s' jams projectile '%s' %d\n", TheGameLogic->getFrame(),
				me->getTemplate()->getName().str(), obj->getTemplate()->getName().str(), obj->getID() ));
			projectile->projectileNowJammed();
			continue;
		}

		if( obj->isKindOf( KINDOF_DRONE ) || obj->getTemplate()->hasSpotterSight() )
			inField.push_back( obj->getID() );
	}

	for( std::vector<ObjectID>::const_iterator it = m_jammed.begin(); it != m_jammed.end(); ++it )
	{
		if( isInList( inField, *it ) )
			continue;
		Object *victim = TheGameLogic->findObjectByID( *it );
		if( victim )
			victim->removeSightJammer();
	}

	for( std::vector<ObjectID>::const_iterator it = inField.begin(); it != inField.end(); ++it )
	{
		if( isInList( m_jammed, *it ) )
			continue;
		Object *victim = TheGameLogic->findObjectByID( *it );
		DEBUG_LOG(( "JAMMER frame %d '%s' blinds '%s' %d\n", TheGameLogic->getFrame(),
			me->getTemplate()->getName().str(), victim->getTemplate()->getName().str(), victim->getID() ));
		victim->addSightJammer();
	}

	m_jammed.swap( inField );
	return UPDATE_SLEEP( data->m_scanFrames );
}

//-------------------------------------------------------------------------------------------------
void JammerUpdate::crc( Xfer *xfer )
{
	UpdateModule::crc( xfer );
}

//-------------------------------------------------------------------------------------------------
/** Xfer method
	* Version Info:
	* 1: Initial version */
//-------------------------------------------------------------------------------------------------
void JammerUpdate::xfer( Xfer *xfer )
{
	XferVersion currentVersion = 1;
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );

	UpdateModule::xfer( xfer );

	Int count = (Int)m_jammed.size();
	xfer->xferInt( &count );
	m_jammed.resize( count );
	for( Int i = 0; i < count; ++i )
		xfer->xferObjectID( &m_jammed[ i ] );
}

//-------------------------------------------------------------------------------------------------
void JammerUpdate::loadPostProcess( void )
{
	UpdateModule::loadPostProcess();
}
