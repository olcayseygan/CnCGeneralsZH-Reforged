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

// FILE: CinemaDirector.h /////////////////////////////////////////////////////////////////////////
// -cinema <name>: the interface off and the camera flown from a shot list in Run/Cinema/<name>.txt.
///////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#ifndef __CINEMADIRECTOR_H_
#define __CINEMADIRECTOR_H_

#include "Lib/BaseType.h"
#include "Common/AsciiString.h"

enum CinemaVerb
{
	CINEMA_VERB_CUT,				///< cut <x> <y> [zoom] [angle] [pitch]: be there now
	CINEMA_VERB_MOVE,				///< move <x> <y> <seconds>: slide the point looked at
	CINEMA_VERB_ROUTE,			///< route <seconds> <x> <y> <x> <y> ...: slide through every point, curving
	CINEMA_VERB_ZOOM,				///< zoom <factor> <seconds>: distance from the ground, 1 is where the shot list began
	CINEMA_VERB_ANGLE,			///< angle <degrees> <seconds>: turn to a heading
	CINEMA_VERB_ORBIT,			///< orbit <degrees> <seconds>: turn by this much
	CINEMA_VERB_PITCH,			///< pitch <degrees> <seconds>: tilt, -36 to 36, 0 is the game's own
	CINEMA_VERB_FOLLOW,			///< follow <template> [seconds]: keep the nearest one in the middle of the frame
	CINEMA_VERB_UNFOLLOW,		///< unfollow: stop following, stay where the camera is
	CINEMA_VERB_HUD,				///< hud on|off
	CINEMA_VERB_LETTERBOX,	///< letterbox on|off
	CINEMA_VERB_CURSOR,			///< cursor on|off: the mouse pointer while the hud is off, off until asked
	CINEMA_VERB_SHOT,				///< shot: one screenshot
	CINEMA_VERB_END,				///< end: quit the game

	CINEMA_VERB_COUNT
};

enum { CINEMA_MAX_ROUTE_POINTS = 16 };

struct CinemaShot
{
	Real at;										///< seconds into the match
	CinemaVerb verb;
	Real seconds;								///< how long the move takes; 0 is a cut
	Real value[ 3 ];						///< x y, or the one number the verb takes, or zoom angle pitch after cut's x y
	Int values;									///< how many of value[] the line gave
	Real routeX[ CINEMA_MAX_ROUTE_POINTS ];
	Real routeY[ CINEMA_MAX_ROUTE_POINTS ];
	Int routePoints;
	AsciiString name;						///< follow's template
	Bool on;										///< hud and letterbox
};

/// parse one line of a shot list; FALSE with a reason for a line that is neither a shot nor blank
Bool CinemaDirector_parseLine( const char *line, CinemaShot *shot, Bool *isShot, AsciiString *reason );

/// smoothstep: 0 at 0, 1 at 1, level at both ends, which is what makes a move ease in and out
Real CinemaDirector_ease( Real t );

/// a point on a Catmull-Rom curve through points[], t from 0 at the first point to 1 at the last
void CinemaDirector_routePoint( const Real *xs, const Real *ys, Int count, Real t, Real *x, Real *y );

/// once a render pass, after the logic tick
void CinemaDirector_update( void );

/// the interface is off: nothing but the world is drawn
Bool CinemaDirector_hidesHud( void );

#endif // __CINEMADIRECTOR_H_
