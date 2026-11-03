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

#include "Common/LogicThread.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/Xfer.h"
#include "GameClient/RadarContactMarks.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object.h"
#include "GameLogic/Module/PhysicsUpdate.h"
#include "GameLogic/Module/RadarCoverageBehavior.h"

RadarCoverageBehavior *RadarCoverageBehavior::s_first = NULL;

/* How often the radar hands its contacts to the client, which draws one sweep's rings and the
	 previous one's echoes (RadarContactLook::SweepSeconds is the same two seconds). */
static const UnsignedInt RADAR_SWEEP_FRAMES = 2 * LOGICFRAMES_PER_SECOND;

/* Horizontal speed in units a frame above which a unit counts as moving.  A tank creeping at a tenth
	 of its pace still shows; one turning its turret on the spot does not. */
static const Real RADAR_MOVING_SPEED = 0.05f;

//-------------------------------------------------------------------------------------------------
/** finished, alive and switched on */
static Bool isWorking( const Object *obj )
{
	return !( obj->isEffectivelyDead() || obj->isDestroyed() || obj->isDisabled() || obj->testStatus( OBJECT_STATUS_UNDER_CONSTRUCTION ) );
}

//-------------------------------------------------------------------------------------------------
/** a radar of owner's covers for player */
static Bool serves( const Player *owner, const Player *player )
{
	return owner == player || owner->getRelationship( player->getDefaultTeam() ) == ALLIES;
}

//-------------------------------------------------------------------------------------------------
static Bool isMoving( const Object *obj )
{
	const PhysicsBehavior *physics = obj->getPhysics();
	if( !physics )
		return FALSE;
	const Coord3D *vel = physics->getVelocity();
	return vel->x * vel->x + vel->y * vel->y > RADAR_MOVING_SPEED * RADAR_MOVING_SPEED;
}

//-------------------------------------------------------------------------------------------------
/** stealthed and nobody's detector has it */
static Bool isHidden( const Object *obj )
{
	return obj->testStatus( OBJECT_STATUS_STEALTHED ) && !obj->testStatus( OBJECT_STATUS_DETECTED );
}

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
		if( !isWorking( obj ) || !serves( obj->getControllingPlayer(), player ) )
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
/*static*/ Bool RadarCoverageBehavior::isTracked( const Player *player, const Object *obj )
{
	return !isHidden( obj ) && isMoving( obj ) && isCovered( player, obj->getPosition() );
}

//-------------------------------------------------------------------------------------------------
/** Walks the object list in its own order and the players in index order, so every machine builds
	* the same list, though only the client reads it: the logic keeps nothing of a sweep. */
//-------------------------------------------------------------------------------------------------
/*static*/ void RadarCoverageBehavior::sweep( void )
{
	const UnsignedInt frame = TheGameLogic->getFrame();
	if( frame % RADAR_SWEEP_FRAMES != 0 )
		return;

	// which players any working radar covers for, theirs or an ally's
	const Int playerCount = ThePlayerList->getPlayerCount();
	Bool covered[ MAX_PLAYER_COUNT ] = { FALSE };
	Bool anyCovered = FALSE;
	for( const RadarCoverageBehavior *radar = s_first; radar; radar = radar->m_next )
	{
		const Object *obj = radar->getObject();
		if( !isWorking( obj ) )
			continue;
		for( Int i = 0; i < playerCount; ++i )
		{
			if( !covered[ i ] && serves( obj->getControllingPlayer(), ThePlayerList->getNthPlayer( i ) ) )
				covered[ i ] = anyCovered = TRUE;
		}
	}

	std::vector<RadarContact> contacts;
	if( anyCovered )
	{
		for( Object *obj = TheGameLogic->getFirstObject(); obj; obj = obj->getNextObject() )
		{
			if( obj->isEffectivelyDead() || obj->isDestroyed() || obj->isContained() || obj->isStructure()
					|| obj->isKindOf( KINDOF_PROJECTILE ) || isHidden( obj ) || !isMoving( obj ) )
				continue;

			for( Int i = 0; i < playerCount; ++i )
			{
				if( !covered[ i ] )
					continue;
				const Player *player = ThePlayerList->getNthPlayer( i );
				if( player->getRelationship( obj->getTeam() ) != ENEMIES )
					continue;
				if( obj->getShroudedStatus( player->getPlayerIndex() ) <= OBJECTSHROUD_PARTIAL_CLEAR )
					continue;
				if( !isCovered( player, obj->getPosition() ) )
					continue;

				RadarContact contact;
				contact.player = player->getPlayerIndex();
				contact.id = obj->getID();
				contact.pos = *obj->getPosition();
				contacts.push_back( contact );
			}
		}

#ifdef DEBUG_LOGGING
		Int counts[ MAX_PLAYER_COUNT ] = { 0 };
		for( size_t c = 0; c < contacts.size(); ++c )
			++counts[ contacts[ c ].player ];
		char line[ 256 ];
		Int used = 0;
		for( Int i = 0; i < playerCount; ++i )
		{
			if( covered[ i ] )
				used += snprintf( line + used, sizeof( line ) - used, " p%d=%d", ThePlayerList->getNthPlayer( i )->getPlayerIndex(), counts[ ThePlayerList->getNthPlayer( i )->getPlayerIndex() ] );
		}
		line[ used ] = 0;
		DEBUG_LOG(( "RADARSWEEP frame %u contacts%s\n", frame, line ));
#endif
	}

	// the client draws them; logic hands over a copy and is done (LogicThread::callAfterTick)
	LogicThread::callAfterTick( [frame, contacts]() { RadarContactMarks_publish( frame, contacts ); } );
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
