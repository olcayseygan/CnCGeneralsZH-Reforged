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

// FILE: RadarCoverageBehavior.cpp ///////////////////////////////////////////////////////////////
//
// A radar covers its circle while it stands finished, alive and switched on: a tower under
// construction, out of power or hit by an EMP guides nothing, and neither does a dead one waiting
// to be removed.
//
///////////////////////////////////////////////////////////////////////////////////////////////////

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "Common/Player.h"
#include "Common/Xfer.h"
#include "GameLogic/Object.h"
#include "GameLogic/Module/RadarCoverageBehavior.h"

RadarCoverageBehavior *RadarCoverageBehavior::s_first = NULL;

//-------------------------------------------------------------------------------------------------
RadarCoverageBehaviorModuleData::RadarCoverageBehaviorModuleData()
{
	m_radius = 0.0f;
}

//-------------------------------------------------------------------------------------------------
/*static*/ void RadarCoverageBehaviorModuleData::buildFieldParse( MultiIniFieldParse& p )
{
	BehaviorModuleData::buildFieldParse( p );

	static const FieldParse dataFieldParse[] =
	{
		{ "Radius",	INI::parseReal,	NULL, offsetof( RadarCoverageBehaviorModuleData, m_radius ) },
		{ 0, 0, 0, 0 }
	};
	p.add( dataFieldParse );
}

//-------------------------------------------------------------------------------------------------
RadarCoverageBehavior::RadarCoverageBehavior( Thing *thing, const ModuleData* moduleData ) : BehaviorModule( thing, moduleData )
{
	m_next = s_first;
	s_first = this;
}

//-------------------------------------------------------------------------------------------------
RadarCoverageBehavior::~RadarCoverageBehavior( void )
{
	RadarCoverageBehavior **link = &s_first;
	while( *link != this )
		link = &(*link)->m_next;
	*link = m_next;
}

//-------------------------------------------------------------------------------------------------
/*static*/ Bool RadarCoverageBehavior::isCovered( const Player *player, const Coord3D *pos )
{
	for( const RadarCoverageBehavior *radar = s_first; radar; radar = radar->m_next )
	{
		const Object *obj = radar->getObject();
		if( obj->isEffectivelyDead() || obj->isDestroyed() || obj->isDisabled() || obj->testStatus( OBJECT_STATUS_UNDER_CONSTRUCTION ) )
			continue;

		const Player *owner = obj->getControllingPlayer();
		if( owner != player && owner->getRelationship( player->getDefaultTeam() ) != ALLIES )
			continue;

		const Real radius = radar->getRadarCoverageBehaviorModuleData()->m_radius;
		const Real dx = pos->x - obj->getPosition()->x;
		const Real dy = pos->y - obj->getPosition()->y;
		if( dx * dx + dy * dy <= radius * radius )
			return TRUE;
	}
	return FALSE;
}

//-------------------------------------------------------------------------------------------------
void RadarCoverageBehavior::crc( Xfer *xfer )
{
	BehaviorModule::crc( xfer );
}

//-------------------------------------------------------------------------------------------------
/** Xfer method
	* Version Info:
	* 1: Initial version */
//-------------------------------------------------------------------------------------------------
void RadarCoverageBehavior::xfer( Xfer *xfer )
{
	XferVersion currentVersion = 1;
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );

	BehaviorModule::xfer( xfer );
}

//-------------------------------------------------------------------------------------------------
void RadarCoverageBehavior::loadPostProcess( void )
{
	BehaviorModule::loadPostProcess();
}
