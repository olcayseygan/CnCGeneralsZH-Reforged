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

// FILE: PieceDamageBehavior.cpp /////////////////////////////////////////////////////////////////
//
// A structure that comes apart where it is hit (the header has the rules).  Everything a hit reads
// is logic state: the damage the body took, the weapon's impact point and radius, and the pieces'
// bones as the model's pristine bones place them (Object::getSingleLogicalBonePosition, the way a
// missile finds its launch bone).  It writes the pieces' health, OBJECT_STATUS_NO_ATTACK when a
// DisablesWeapons piece breaks, and the drawable's sub-objects and an FX, which the logic never
// reads back.
//
///////////////////////////////////////////////////////////////////////////////////////////////////

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "Common/INI.h"
#include "Common/ThingTemplate.h"
#include "Common/Xfer.h"
#include "GameClient/Drawable.h"
#include "GameClient/FXList.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object.h"
#include "GameLogic/Module/BodyModule.h"
#include "GameLogic/Module/PieceDamageBehavior.h"

//-------------------------------------------------------------------------------------------------
/** The index of the piece already listed under that sub-object name; an unknown name stops the load. */
static Int findPiece( const PieceDamageBehaviorModuleData *self, const char *name )
{
	for( Int i = 0; i < (Int)self->m_pieces.size(); ++i )
		if( self->m_pieces[ i ].m_subObject.compareNoCase( name ) == 0 )
			return i;
	DEBUG_CRASH(( "PieceDamageBehavior: %s is not an earlier piece", name ));
	throw INI_INVALID_DATA;
}

//-------------------------------------------------------------------------------------------------
/** Piece = <sub-object> <bone> <health> [<earlier piece it rests on> ...] */
static void parsePiece( INI *ini, void *instance, void * /*store*/, const void * /*userData*/ )
{
	PieceDamageBehaviorModuleData *self = (PieceDamageBehaviorModuleData *)instance;
	if( self->m_pieces.size() >= PieceDamageState::MAX_PIECES )
	{
		DEBUG_CRASH(( "PieceDamageBehavior takes %d pieces at most", (Int)PieceDamageState::MAX_PIECES ));
		throw INI_INVALID_DATA;
	}

	PieceDamagePiece piece;
	piece.m_subObject = ini->getNextAsciiString();
	piece.m_bone = ini->getNextAsciiString();
	piece.m_health = INI::scanReal( ini->getNextToken() );
	piece.m_restsOn = 0;
	for( const char *token = ini->getNextTokenOrNull(); token; token = ini->getNextTokenOrNull() )
		piece.m_restsOn |= 1u << findPiece( self, token );
	self->m_pieces.push_back( piece );
}

//-------------------------------------------------------------------------------------------------
/** DisablesWeapons = <piece> [<piece> ...], each named by an earlier Piece line */
static void parseDisablesWeapons( INI *ini, void *instance, void * /*store*/, const void * /*userData*/ )
{
	PieceDamageBehaviorModuleData *self = (PieceDamageBehaviorModuleData *)instance;
	for( const char *token = ini->getNextTokenOrNull(); token; token = ini->getNextTokenOrNull() )
		self->m_disablesWeapons |= 1u << findPiece( self, token );
}

//-------------------------------------------------------------------------------------------------
PieceDamageBehaviorModuleData::PieceDamageBehaviorModuleData()
{
	m_minimumReach = 10.0f;
	m_blastReachScale = 1.0f;
	m_disablesWeapons = 0;
	m_breakFX = NULL;
}

//-------------------------------------------------------------------------------------------------
/*static*/ void PieceDamageBehaviorModuleData::buildFieldParse( MultiIniFieldParse& p )
{
	DamageModuleData::buildFieldParse( p );

	static const FieldParse dataFieldParse[] =
	{
		{ "MinimumReach",			INI::parsePositiveNonZeroReal,	NULL, offsetof( PieceDamageBehaviorModuleData, m_minimumReach ) },
		{ "BlastReachScale",	INI::parseReal,									NULL, offsetof( PieceDamageBehaviorModuleData, m_blastReachScale ) },
		{ "BreakFX",			INI::parseFXList,								NULL, offsetof( PieceDamageBehaviorModuleData, m_breakFX ) },
		{ "Piece",				parsePiece,											NULL, 0 },
		{ "DisablesWeapons",	parseDisablesWeapons,				NULL, 0 },
		{ 0, 0, 0, 0 }
	};
	p.add( dataFieldParse );
}

//-------------------------------------------------------------------------------------------------
void PieceDamageState::reset( const std::vector<PieceDamagePiece>& pieces )
{
	m_health.resize( pieces.size() );
	for( size_t i = 0; i < pieces.size(); ++i )
		m_health[ i ] = pieces[ i ].m_health;
}

//-------------------------------------------------------------------------------------------------
/*static*/ Real PieceDamageState::share( Real dist, Real nearest, Real reach )
{
	const Real s = 1.0f - ( dist - nearest ) / reach;
	return s < 0.0f ? 0.0f : s > 1.0f ? 1.0f : s;
}

//-------------------------------------------------------------------------------------------------
UnsignedInt PieceDamageState::hit( const std::vector<PieceDamagePiece>& pieces, const Coord3D *at,
																	 const Coord3D& impact, Real reach, Real amount )
{
	const Int count = (Int)m_health.size();
	const UnsignedInt before = getBrokenMask();

	Real dist[ MAX_PIECES ];
	Real nearest = -1.0f;
	for( Int i = 0; i < count; ++i )
	{
		if( m_health[ i ] <= 0.0f )
			continue;
		const Real dx = at[ i ].x - impact.x, dy = at[ i ].y - impact.y, dz = at[ i ].z - impact.z;
		dist[ i ] = sqrtf( dx * dx + dy * dy + dz * dz );
		if( nearest < 0.0f || dist[ i ] < nearest )
			nearest = dist[ i ];
	}
	if( nearest < 0.0f )
		return 0;		// nothing left standing

	for( Int i = 0; i < count; ++i )
		if( m_health[ i ] > 0.0f )
			m_health[ i ] -= amount * share( dist[ i ], nearest, reach );

	// a piece rests only on earlier ones, so one pass in list order settles every fall
	UnsignedInt broken = 0;
	for( Int i = 0; i < count; ++i )
	{
		if( m_health[ i ] <= 0.0f || ( pieces[ i ].m_restsOn & broken ) != 0 )
		{
			m_health[ i ] = 0.0f;
			broken |= 1u << i;
		}
	}
	return broken & ~before;
}

//-------------------------------------------------------------------------------------------------
UnsignedInt PieceDamageState::getBrokenMask() const
{
	UnsignedInt broken = 0;
	for( Int i = 0; i < (Int)m_health.size(); ++i )
		if( m_health[ i ] <= 0.0f )
			broken |= 1u << i;
	return broken;
}

//-------------------------------------------------------------------------------------------------
/** Version 1: the piece count and each piece's health */
void PieceDamageState::xfer( Xfer *xfer )
{
	XferVersion currentVersion = 1;
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );

	UnsignedByte count = (UnsignedByte)m_health.size();
	xfer->xferUnsignedByte( &count );
	if( xfer->getXferMode() == XFER_LOAD )
		m_health.resize( count );
	for( Int i = 0; i < count; ++i )
		xfer->xferReal( &m_health[ i ] );
}

//-------------------------------------------------------------------------------------------------
PieceDamageBehavior::PieceDamageBehavior( Thing *thing, const ModuleData* moduleData ) : DamageModule( thing, moduleData )
{
	m_state.reset( getPieceDamageBehaviorModuleData()->m_pieces );
}

//-------------------------------------------------------------------------------------------------
PieceDamageBehavior::~PieceDamageBehavior( void )
{
}

//-------------------------------------------------------------------------------------------------
void PieceDamageBehavior::onDamage( DamageInfo *damageInfo )
{
	const DamageInfoInput& in = damageInfo->in;
	const Real amount = damageInfo->out.m_actualDamageDealt;
	Object *obj = getObject();
	if( !in.m_hasImpact || amount <= 0.0f || obj->isEffectivelyDead() )
		return;		// the killing blow leaves the pieces to the death and its rubble model

	const PieceDamageBehaviorModuleData *d = getPieceDamageBehaviorModuleData();
	const std::vector<PieceDamagePiece>& pieces = d->m_pieces;
	Coord3D at[ PieceDamageState::MAX_PIECES ];
	for( Int i = 0; i < (Int)pieces.size(); ++i )
	{
		if( m_state.getHealth( i ) <= 0.0f )
			continue;
		// a bone the draw module does not make public (ExtraPublicBone) is not found, and the piece
		// then stands at the object's foot
		const Bool found = obj->getSingleLogicalBonePosition( pieces[ i ].m_bone.str(), &at[ i ], NULL );
		DEBUG_ASSERTCRASH( found, ( "PieceDamageBehavior: %s has no public bone %s", obj->getTemplate()->getName().str(), pieces[ i ].m_bone.str() ) );
		(void)found;
	}

	const Real reach = max( in.m_impactRadius * d->m_blastReachScale, d->m_minimumReach );
	const UnsignedInt broke = m_state.hit( pieces, at, in.m_impactPos, reach, amount );
	if( broke == 0 )
		return;

	Int brokenCount = 0;
	for( UnsignedInt mask = m_state.getBrokenMask(); mask; mask &= mask - 1 )
		++brokenCount;
	for( Int i = 0; i < (Int)pieces.size(); ++i )
	{
		if( ( broke & ( 1u << i ) ) == 0 )
			continue;
		DEBUG_LOG(( "PIECE BREAK: frame %u object %d '%s' piece %s at (%.1f,%.1f,%.1f), hit at (%.1f,%.1f,%.1f) reach %.1f damage %.1f from %d, %d of %d broken\n",
			TheGameLogic->getFrame(), obj->getID(), obj->getTemplate()->getName().str(), pieces[ i ].m_subObject.str(),
			at[ i ].x, at[ i ].y, at[ i ].z, in.m_impactPos.x, in.m_impactPos.y, in.m_impactPos.z, reach, amount,
			in.m_sourceID, brokenCount, (Int)pieces.size() ));
		FXList::doFXPos( d->m_breakFX, &at[ i ] );
	}
	showBroken( broke );

	// the guns went with that piece: for good, and the object stays a target with what health it has
	if( ( broke & d->m_disablesWeapons ) != 0 && !obj->testStatus( OBJECT_STATUS_NO_ATTACK ) )
	{
		obj->setStatus( MAKE_OBJECT_STATUS_MASK( OBJECT_STATUS_NO_ATTACK ) );
		DEBUG_LOG(( "PIECE DISARM: frame %u object %d '%s' stops firing, health %.1f left\n",
			TheGameLogic->getFrame(), obj->getID(), obj->getTemplate()->getName().str(),
			obj->getBodyModule() ? obj->getBodyModule()->getHealth() : 0.0f ));
	}
}

//-------------------------------------------------------------------------------------------------
void PieceDamageBehavior::showBroken( UnsignedInt broken ) const
{
	Drawable *draw = getObject()->getDrawable();
	if( draw == NULL || broken == 0 )
		return;

	const std::vector<PieceDamagePiece>& pieces = getPieceDamageBehaviorModuleData()->m_pieces;
	AsciiString standIn;
	for( Int i = 0; i < (Int)pieces.size(); ++i )
	{
		if( ( broken & ( 1u << i ) ) == 0 )
			continue;
		draw->showSubObject( pieces[ i ].m_subObject, FALSE );
		standIn.format( "%s_B", pieces[ i ].m_subObject.str() );
		draw->showSubObject( standIn, TRUE );
	}
	draw->updateSubObjects();
}

//-------------------------------------------------------------------------------------------------
void PieceDamageBehavior::crc( Xfer *xfer )
{
	DamageModule::crc( xfer );
	m_state.xfer( xfer );
}

//-------------------------------------------------------------------------------------------------
/** Version 1: the pieces' state */
void PieceDamageBehavior::xfer( Xfer *xfer )
{
	XferVersion currentVersion = 1;
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );

	DamageModule::xfer( xfer );

	m_state.xfer( xfer );
	// a save from a build with other pieces starts them whole
	if( xfer->getXferMode() == XFER_LOAD && m_state.getCount() != (Int)getPieceDamageBehaviorModuleData()->m_pieces.size() )
		m_state.reset( getPieceDamageBehaviorModuleData()->m_pieces );
}

//-------------------------------------------------------------------------------------------------
void PieceDamageBehavior::loadPostProcess( void )
{
	DamageModule::loadPostProcess();
	DEBUG_LOG(( "PIECE LOAD: object %d '%s' broken mask 0x%x\n", getObject()->getID(),
		getObject()->getTemplate()->getName().str(), m_state.getBrokenMask() ));
	showBroken( m_state.getBrokenMask() );
}
