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

// AIIdentity.cpp
// Which general a seat plays, and the identity that goes with it.  The hooks are in AIIdentity.h.

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine
#include "Common/Xfer.h"
#include "GameLogic/AIIdentity.h"
#include "GameLogic/LogicRandomValue.h"

/** The player template's Side for every general.  A side not listed plays the shared AI alone.  Append
	* only, never remap: AIGeneral is written in saves (AIIdentity::xfer). */
static const struct { const char *side; AIGeneral general; } AI_GENERAL_SIDES[] =
{
	{ "America",										AIGENERAL_USA },
	{ "AmericaLaserGeneral",				AIGENERAL_USA_LASER },
	{ "AmericaSuperWeaponGeneral",	AIGENERAL_USA_SUPERWEAPON },
	{ "AmericaAirForceGeneral",			AIGENERAL_USA_AIR_FORCE },
	{ "China",											AIGENERAL_CHINA },
	{ "ChinaTankGeneral",						AIGENERAL_CHINA_TANK },
	{ "ChinaInfantryGeneral",				AIGENERAL_CHINA_INFANTRY },
	{ "ChinaNukeGeneral",						AIGENERAL_CHINA_NUKE },
	{ "GLA",												AIGENERAL_GLA },
	{ "GLAToxinGeneral",						AIGENERAL_GLA_TOXIN },
	{ "GLADemolitionGeneral",				AIGENERAL_GLA_DEMOLITION },
	{ "GLAStealthGeneral",					AIGENERAL_GLA_STEALTH },
	{ "Turkiye",										AIGENERAL_TURKIYE },
};

AIGeneral AIIdentity::generalOfSide( const AsciiString &side )
{
	for( Int i = 0; i < (Int)( sizeof( AI_GENERAL_SIDES ) / sizeof( AI_GENERAL_SIDES[ 0 ] ) ); ++i )
		if( side.compareNoCase( AI_GENERAL_SIDES[ i ].side ) == 0 )
			return AI_GENERAL_SIDES[ i ].general;
	return AIGENERAL_NONE;
}

AIIdentity *AIIdentity::create( AIPlayer *ai, const AsciiString &side )
{
	const AIGeneral general = generalOfSide( side );
	AIIdentity *identity = NULL;
	if( general >= AIGENERAL_USA && general <= AIGENERAL_USA_AIR_FORCE )
		identity = createUSAIdentity( ai, general );
	else if( general >= AIGENERAL_CHINA && general <= AIGENERAL_CHINA_NUKE )
		identity = createChinaIdentity( ai, general );
	else if( general >= AIGENERAL_GLA && general <= AIGENERAL_GLA_STEALTH )
		identity = createGLAIdentity( ai, general );
	else if( general == AIGENERAL_TURKIYE )
		identity = createTurkiyeIdentity( ai, general );
	return identity ? identity : new AIIdentity( ai, general );
}

void AIIdentity::drawMove( UnsignedInt move, Int chance )
{
	if( chance >= 100 || ( chance > 0 && GameLogicRandomValue( 1, 100 ) <= chance ) )
		m_moves |= move;
}

/** 2: the general.  A load whose general is not the one this side makes would hand a side class another
	* class's fields, so it is refused. */
void AIIdentity::xfer( Xfer *xfer )
{
	XferVersion currentVersion = 2;
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );
	if( version >= 2 )
	{
		Int general = (Int)m_general;
		xfer->xferInt( &general );
		if( xfer->getXferMode() == XFER_LOAD && general != (Int)m_general )
		{
			DEBUG_CRASH(( "AIIdentity::xfer - saved general %d, the side makes %d", general, (Int)m_general ));
			throw XFER_INVALID_PARAMETERS;
		}
	}
	xfer->xferUnsignedInt( &m_moves );
}
