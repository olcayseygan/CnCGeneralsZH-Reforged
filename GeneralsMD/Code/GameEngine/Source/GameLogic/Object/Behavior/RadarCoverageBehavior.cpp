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
#include "Common/ThingTemplate.h"
#include "Common/Upgrade.h"
#include "Common/Xfer.h"
#include "GameClient/RadarContactMarks.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object.h"
#include "GameLogic/Weapon.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/DeletionUpdate.h"
#include "GameLogic/Module/LifetimeUpdate.h"
#include "GameLogic/Module/PhysicsUpdate.h"
#include "GameLogic/Module/RadarCoverageBehavior.h"

RadarCoverageBehavior *RadarCoverageBehavior::s_first = NULL;

/* How often the radar hands its contacts to the client, which draws one sweep's rings and the
	 previous one's echoes (RadarContactLook::SweepSeconds is the same two seconds). */
static const UnsignedInt RADAR_SWEEP_FRAMES = 2 * LOGICFRAMES_PER_SECOND;

/* Horizontal speed in units a frame above which a unit counts as moving.  A tank creeping at a tenth
	 of its pace still shows; one turning its turret on the spot does not. */
static const Real RADAR_MOVING_SPEED = 0.05f;

/* A vehicle whose longest weapon reaches this far reads as artillery on a classified chip: tanks
	 shoot well inside it, the rocket and shell batteries of every side beyond it. */
static const Real RADAR_ARTILLERY_RANGE = 300.0f;

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
/** The class a contact's chip names, from what the object is.  Anti-air is a vehicle whose primary
	* weapon hits aircraft and not the ground; artillery a vehicle that reaches RADAR_ARTILLERY_RANGE.
	* NONE for anything that is neither a unit nor a base defence, which no recon reports standing. */
static RadarGlyph glyphOf( const Object *obj )
{
	if( obj->isStructure() )
		return obj->isKindOf( KINDOF_FS_BASE_DEFENSE ) || obj->isKindOf( KINDOF_TECH_BASE_DEFENSE ) ? RADAR_GLYPH_STRUCTURE_DEFENCE : RADAR_GLYPH_NONE;
	if( obj->isKindOf( KINDOF_AIRCRAFT ) )
		return RADAR_GLYPH_AIRCRAFT;
	if( obj->isKindOf( KINDOF_INFANTRY ) )
		return RADAR_GLYPH_INFANTRY;
	if( !obj->isKindOf( KINDOF_VEHICLE ) )
		return RADAR_GLYPH_NONE;

	const Weapon *primary = obj->getWeaponInWeaponSlot( PRIMARY_WEAPON );	// a dozer or a transport carries none
	if( primary && ( primary->getAntiMask() & WEAPON_ANTI_AIRBORNE_VEHICLE ) && !( primary->getAntiMask() & WEAPON_ANTI_GROUND ) )
		return RADAR_GLYPH_ANTIAIR;
	if( obj->getLargestWeaponRange() >= RADAR_ARTILLERY_RANGE )
		return RADAR_GLYPH_ARTILLERY;
	return RADAR_GLYPH_VEHICLE;
}

//-------------------------------------------------------------------------------------------------
/** the frame a timed source (a recon marker) goes, from its LifetimeUpdate or DeletionUpdate, else 0 */
static UnsignedInt endFrameOf( const Object *obj )
{
	static const NameKeyType lifetimeKey = TheNameKeyGenerator->nameToKey( "LifetimeUpdate" );
	static const NameKeyType deletionKey = TheNameKeyGenerator->nameToKey( "DeletionUpdate" );
	const LifetimeUpdate *lifetime = (const LifetimeUpdate *)obj->findUpdateModule( lifetimeKey );
	if( lifetime )
		return lifetime->getDieFrame();
	DeletionUpdate *deletion = (DeletionUpdate *)obj->findUpdateModule( deletionKey );
	return deletion ? deletion->getDieFrame() : 0;
}

//-------------------------------------------------------------------------------------------------
static void parseRadiusUpgrade( INI *ini, void * /*instance*/, void *store, const void * /*userData*/ )
{
	RadarCoverageBehaviorModuleData::RadiusUpgrade up;
	up.m_upgrade = ini->getNextToken();
	INI::parseReal( ini, NULL, &up.m_scale, NULL );
	((std::vector<RadarCoverageBehaviorModuleData::RadiusUpgrade> *)store)->push_back( up );
}

//-------------------------------------------------------------------------------------------------
RadarCoverageBehaviorModuleData::RadarCoverageBehaviorModuleData()
{
	m_radius = 0.0f;
	m_onlyWhenStationary = FALSE;
	m_paintsTargetOnly = FALSE;
	m_seesStationary = FALSE;
}

//-------------------------------------------------------------------------------------------------
/*static*/ void RadarCoverageBehaviorModuleData::buildFieldParse( MultiIniFieldParse& p )
{
	BehaviorModuleData::buildFieldParse( p );

	static const FieldParse dataFieldParse[] =
	{
		{ "Radius",							INI::parseReal,					NULL, offsetof( RadarCoverageBehaviorModuleData, m_radius ) },
		{ "RadiusUpgrade",			parseRadiusUpgrade,			NULL, offsetof( RadarCoverageBehaviorModuleData, m_radiusUpgrades ) },
		{ "RequiredUpgrade",		INI::parseAsciiString,	NULL, offsetof( RadarCoverageBehaviorModuleData, m_requiredUpgrade ) },
		{ "ClassifyUpgrade",		INI::parseAsciiString,	NULL, offsetof( RadarCoverageBehaviorModuleData, m_classifyUpgrade ) },
		{ "OnlyWhenStationary",	INI::parseBool,					NULL, offsetof( RadarCoverageBehaviorModuleData, m_onlyWhenStationary ) },
		{ "PaintsTargetOnly",		INI::parseBool,					NULL, offsetof( RadarCoverageBehaviorModuleData, m_paintsTargetOnly ) },
		{ "SeesStationary",			INI::parseBool,					NULL, offsetof( RadarCoverageBehaviorModuleData, m_seesStationary ) },
		{ 0, 0, 0, 0 }
	};
	p.add( dataFieldParse );
}

//-------------------------------------------------------------------------------------------------
RadarCoverageBehavior::RadarCoverageBehavior( Thing *thing, const ModuleData* moduleData ) : BehaviorModule( thing, moduleData )
{
	m_next = s_first;
	s_first = this;

	// the upgrades parse after the objects, so the data holds names; a wrong one ends the game here
	const RadarCoverageBehaviorModuleData *data = getRadarCoverageBehaviorModuleData();
	AsciiString who;
	who.format( "RadarCoverageBehavior on %s", getObject()->getTemplate()->getName().str() );
	for( size_t i = 0; i < data->m_radiusUpgrades.size(); ++i )
		m_radiusUpgrades.push_back( TheUpgradeCenter->requireUpgrade( data->m_radiusUpgrades[ i ].m_upgrade, who ) );
	m_requiredUpgrade = data->m_requiredUpgrade.isEmpty() ? NULL : TheUpgradeCenter->requireUpgrade( data->m_requiredUpgrade, who );
	m_classifyUpgrade = data->m_classifyUpgrade.isEmpty() ? NULL : TheUpgradeCenter->requireUpgrade( data->m_classifyUpgrade, who );
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
/** Radius scaled by the owner's RadiusUpgrades, or 0 while the source is down, still lacks its
	* RequiredUpgrade, or moves with OnlyWhenStationary. */
Real RadarCoverageBehavior::reach( void ) const
{
	const Object *obj = getObject();
	const RadarCoverageBehaviorModuleData *data = getRadarCoverageBehaviorModuleData();
	const Player *owner = obj->getControllingPlayer();
	if( !isWorking( obj ) || ( m_requiredUpgrade && !owner->hasUpgradeComplete( m_requiredUpgrade ) )
			|| ( data->m_onlyWhenStationary && isMoving( obj ) ) )
		return 0.0f;

	Real radius = data->m_radius;
	for( size_t i = 0; i < m_radiusUpgrades.size(); ++i )
	{
		if( owner->hasUpgradeComplete( m_radiusUpgrades[ i ] ) )
			radius *= data->m_radiusUpgrades[ i ].m_scale;
	}
	return radius;
}

//-------------------------------------------------------------------------------------------------
/** The enemy it shoots at, or else the enemy its orders aim it at, while that stands within reach. */
const Object *RadarCoverageBehavior::paintedTarget( void ) const
{
	const Real radius = reach();
	if( radius <= 0.0f )
		return NULL;

	const Object *obj = getObject();
	const AIUpdateInterface *ai = obj->getAI();
	const Object *target = ai->getCurrentVictim();
	if( !target )
		target = const_cast<AIUpdateInterface *>( ai )->getGoalObject();
	if( !target || target->isEffectivelyDead() || obj->getRelationship( target ) != ENEMIES )
		return NULL;

	const Real dx = target->getPosition()->x - obj->getPosition()->x;
	const Real dy = target->getPosition()->y - obj->getPosition()->y;
	return dx * dx + dy * dy <= radius * radius ? target : NULL;
}

//-------------------------------------------------------------------------------------------------
Bool RadarCoverageBehavior::classifies( void ) const
{
	return getRadarCoverageBehaviorModuleData()->m_seesStationary
		|| ( m_classifyUpgrade && getObject()->getControllingPlayer()->hasUpgradeComplete( m_classifyUpgrade ) );
}

//-------------------------------------------------------------------------------------------------
/** Every source of player's or an ally's that reaches pos, or that paints target (by identity when
	* target is given, else by pos standing within the painted object's bounding circle).  Stops as
	* soon as it knows what need asks; every answer is an OR over the sources, so their order does not
	* matter. */
/*static*/ void RadarCoverageBehavior::survey( const Player *player, const Coord3D *pos, const Object *target, SurveyNeed need, Survey &out )
{
	out.covered = out.painted = out.recon = out.classified = FALSE;
	for( const RadarCoverageBehavior *radar = s_first; radar; radar = radar->m_next )
	{
		const Object *obj = radar->getObject();
		if( !serves( obj->getControllingPlayer(), player ) )
			continue;

		const RadarCoverageBehaviorModuleData *data = radar->getRadarCoverageBehaviorModuleData();
		if( data->m_paintsTargetOnly )
		{
			const Object *painted = radar->paintedTarget();
			if( !painted )
				continue;
			if( target )
			{
				if( painted != target )
					continue;
			}
			else
			{
				const Real r = painted->getGeometryInfo().getBoundingCircleRadius();
				const Real dx = pos->x - painted->getPosition()->x;
				const Real dy = pos->y - painted->getPosition()->y;
				if( dx * dx + dy * dy > r * r )
					continue;
			}
			out.painted = TRUE;
		}
		else
		{
			const Real radius = radar->reach();
			const Real dx = pos->x - obj->getPosition()->x;
			const Real dy = pos->y - obj->getPosition()->y;
			if( radius <= 0.0f || dx * dx + dy * dy > radius * radius )
				continue;
			if( data->m_seesStationary )
				out.recon = TRUE;
		}

		out.covered = TRUE;
		if( need == SURVEY_COVER || ( need == SURVEY_PAINT && out.painted ) || ( need == SURVEY_HOLD && ( out.painted || out.recon ) ) )
			return;
		if( !out.classified )
			out.classified = radar->classifies();
	}
}

//-------------------------------------------------------------------------------------------------
/*static*/ Bool RadarCoverageBehavior::isCovered( const Player *player, const Coord3D *pos )
{
	Survey s;
	survey( player, pos, NULL, SURVEY_COVER, s );
	return s.covered;
}

//-------------------------------------------------------------------------------------------------
/** Any coverage holds a mover; a standing object takes a paint, or a recon source if it is a unit
	* or a base defence. */
/*static*/ Bool RadarCoverageBehavior::isTracked( const Player *player, const Object *obj )
{
	if( isHidden( obj ) )
		return FALSE;
	Survey s;
	if( isMoving( obj ) )
	{
		survey( player, obj->getPosition(), obj, SURVEY_COVER, s );
		return s.covered;
	}
	const Bool reconHolds = glyphOf( obj ) != RADAR_GLYPH_NONE;
	survey( player, obj->getPosition(), obj, reconHolds ? SURVEY_HOLD : SURVEY_PAINT, s );
	return s.painted || ( s.recon && reconHolds );
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

	/* which players any live radar covers for, theirs or an ally's, and the recon areas, in the radar
		 list's order: that order differs after a load, but only the client reads the areas */
	const Int playerCount = ThePlayerList->getPlayerCount();
	Bool covered[ MAX_PLAYER_COUNT ] = { FALSE };
	Bool anyCovered = FALSE;
	std::vector<RadarReconArea> areas;
	for( const RadarCoverageBehavior *radar = s_first; radar; radar = radar->m_next )
	{
		const Real radius = radar->reach();
		if( radius <= 0.0f )
			continue;
		const Object *obj = radar->getObject();
		const Bool recon = radar->getRadarCoverageBehaviorModuleData()->m_seesStationary;
		for( Int i = 0; i < playerCount; ++i )
		{
			const Player *player = ThePlayerList->getNthPlayer( i );
			if( !serves( obj->getControllingPlayer(), player ) )
				continue;
			covered[ i ] = anyCovered = TRUE;

			// one area for every player it serves, the owner and his allies, who get its recon chips too
			if( recon )
			{
				RadarReconArea area;
				area.player = player->getPlayerIndex();
				area.pos = *obj->getPosition();
				area.radius = radius;
				area.endFrame = endFrameOf( obj );
				areas.push_back( area );
			}
		}
	}

	std::vector<RadarContact> contacts;
	if( anyCovered )
	{
		for( Object *obj = TheGameLogic->getFirstObject(); obj; obj = obj->getNextObject() )
		{
			if( obj->isEffectivelyDead() || obj->isDestroyed() || obj->isContained()
					|| obj->isKindOf( KINDOF_PROJECTILE ) || isHidden( obj ) )
				continue;
			// a mover shows in any coverage; a standing unit or base defence only to a paint or a recon source
			const Bool moving = isMoving( obj );
			const RadarGlyph glyph = glyphOf( obj );
			if( !moving && glyph == RADAR_GLYPH_NONE )
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
				Survey s;
				survey( player, obj->getPosition(), obj, SURVEY_ALL, s );
				if( moving ? !s.covered : !( s.painted || s.recon ) )
					continue;

				RadarContact contact;
				contact.player = player->getPlayerIndex();
				contact.id = obj->getID();
				contact.pos = *obj->getPosition();
				contact.glyph = (UnsignedByte)( s.classified ? glyph : RADAR_GLYPH_NONE );
				contact.recon = s.recon;
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
		DEBUG_LOG(( "RADARSWEEP frame %u contacts%s recon areas %d\n", frame, line, (Int)areas.size() ));
#endif
	}

	// the client draws them; logic hands over a copy and is done (LogicThread::callAfterTick)
	LogicThread::callAfterTick( [frame, contacts, areas]() { RadarContactMarks_publish( frame, contacts, areas ); } );
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
