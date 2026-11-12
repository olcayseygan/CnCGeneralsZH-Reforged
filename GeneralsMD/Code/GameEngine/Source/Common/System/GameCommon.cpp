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

// GameCommon.h
// Part of header detangling
// John McDonald, Aug 2002

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "Common/GameCommon.h"

//-------------------------------------------------------------------------------------------------
// Copied from a constexpr, so constant-initialised: valid before any dynamic initialiser runs.
static constexpr GameSpeedState NATIVE_GAME_SPEED = GameSpeed_compute( NATIVE_GAME_SPEED_PERCENT );
GameSpeedState TheGameSpeed = NATIVE_GAME_SPEED;

void GameSpeed_set( Int percent )
{
	DEBUG_ASSERTCRASH( GameSpeed_isAllowed( percent ), ( "GameSpeed_set: %d is not an allowed game speed", percent ) );
	if( !GameSpeed_isAllowed( percent ) )
		percent = NATIVE_GAME_SPEED_PERCENT;
	TheGameSpeed = GameSpeed_compute( percent );
}

#if defined(_DEBUG)
#if defined(_MSC_VER)
#include <intrin.h>
#endif
static Bool s_gameSpeedMainStarted = false;
static Int s_gameSpeedReadsBeforeMain = 0;
static const void *s_gameSpeedFirstEarlyReader = NULL;	///< where the first of them came from, for the map file

const GameSpeedState &GameSpeed_countedRead()
{
	if( !s_gameSpeedMainStarted )
	{
		if( s_gameSpeedReadsBeforeMain++ == 0 )
		{
#if defined(_MSC_VER)
			s_gameSpeedFirstEarlyReader = _ReturnAddress();
#endif
		}
	}
	return TheGameSpeed;
}

const void *GameSpeed_firstReaderBeforeMain()
{
	return s_gameSpeedFirstEarlyReader;
}

Int GameSpeed_readsBeforeMain()
{
	return s_gameSpeedReadsBeforeMain;
}

void GameSpeed_markMainStarted()
{
	s_gameSpeedMainStarted = true;
}
#endif

const char *TheVeterancyNames[] = 
{
	"REGULAR",
	"VETERAN",
	"ELITE",
	"HEROIC",
	NULL
};

const char *TheRelationshipNames[] =
{
	"ENEMIES",
	"NEUTRAL",
	"ALLIES",
	NULL
};

//-------------------------------------------------------------------------------------------------
Real normalizeAngle(Real angle)
{
	DEBUG_ASSERTCRASH(!_isnan(angle), ("Angle is NAN in normalizeAngle!\n"));

	if( _isnan(angle) )
		return 0;// ARGH!!!! Don't assert and then not handle it!  Error bad!  Fix error!

	while (angle > PI) 
		angle -= 2*PI;

	while (angle <= -PI) 
		angle += 2*PI;

	return angle;
}

