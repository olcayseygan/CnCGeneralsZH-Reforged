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
	CINEMA_VERB_FILTER,			///< filter bw|blur|off: a map script's black and white view or motion blur zoom
	CINEMA_VERB_SHOT,				///< shot: one screenshot
	CINEMA_VERB_END,				///< end: quit the game
	CINEMA_VERB_DRONE,			///< drone <seconds> <x> <y> <height> ... [look ...] [tilt d] [clear u] [bank f] [sway f]: fly low through the points

	CINEMA_VERB_COUNT
};

enum { CINEMA_MAX_ROUTE_POINTS = 16 };

enum CinemaDroneLook
{
	CINEMA_DRONE_LOOK_AHEAD,		///< along the path, into the turns
	CINEMA_DRONE_LOOK_POINT,		///< at a ground point the whole flight
	CINEMA_DRONE_LOOK_OBJECT		///< at the one of the template nearest the middle of the path, tracked
};

struct CinemaDroneSettings
{
	Int look;										///< CinemaDroneLook
	Real lookX;
	Real lookY;
	Real tilt;									///< degrees added to the look ahead, below 0 looks down
	Real clearance;							///< world units kept between the eye and the ground, water or a roof under it
	Real bank;									///< share of the bank a real turn would give, 0 flies level
	Real sway;									///< share of the hand-held drift, 0 holds steady
};

struct CinemaShot
{
	Real at;										///< seconds into the match
	CinemaVerb verb;
	Real seconds;								///< how long the move takes; 0 is a cut
	Real value[ 3 ];						///< x y, or the one number the verb takes, or zoom angle pitch after cut's x y
	Int values;									///< how many of value[] the line gave
	Real routeX[ CINEMA_MAX_ROUTE_POINTS ];
	Real routeY[ CINEMA_MAX_ROUTE_POINTS ];
	Real routeZ[ CINEMA_MAX_ROUTE_POINTS ];	///< drone: height above the ground at each point
	Int routePoints;
	AsciiString name;						///< follow's template, filter's bw, blur or off, drone's look template
	Bool on;										///< hud and letterbox
	CinemaDroneSettings drone;
};

// ------------------------------------------------------------------------------------------------
// the drone: a flight planned once when its shot fires, then a pure function of the time into it,
// so the same list frames the same picture on every run

enum { CINEMA_DRONE_STATIONS = 512 };

struct CinemaDroneFlight
{
	Real xs[ CINEMA_MAX_ROUTE_POINTS ];
	Real ys[ CINEMA_MAX_ROUTE_POINTS ];
	Real heights[ CINEMA_MAX_ROUTE_POINTS ];
	Int points;
	Real seconds;
	Real length;								///< world units along the path
	Real cruise;								///< speed between the run-up and the slow-down
	Real ramp;									///< seconds of each
	Real param[ CINEMA_DRONE_STATIONS + 1 ];	///< curve parameter at equal distances along the path
	Real z[ CINEMA_DRONE_STATIONS + 1 ];			///< eye height at each, clear of whatever is under it
	CinemaDroneSettings settings;
};

struct CinemaDronePose
{
	Coord3D eye;
	Real heading;								///< radians, 0 looks along +x
	Real tilt;									///< radians, up positive
	Real roll;									///< radians, positive drops the right side (View::setFreeCameraPose)
	Real speed;									///< world units a second along the path
};

/// the top of whatever is at (x, y): ground, water or a roof
typedef Real (*CinemaGroundFn)( void *context, Real x, Real y );

void CinemaDrone_plan( CinemaDroneFlight *flight, const CinemaShot &shot, CinemaGroundFn ground, void *context );
/// how far along the path at t seconds into the flight: a smooth run-up, a cruise, a smooth stop
Real CinemaDrone_distance( const CinemaDroneFlight &flight, Real t, Real *speed );
/// where the eye is and how it faces at t; target is what it looks at, NULL to look ahead
void CinemaDrone_pose( const CinemaDroneFlight &flight, Real t, const Coord3D *target, CinemaDronePose *pose );

/// parse one line of a shot list; FALSE with a reason for a line that is neither a shot nor blank
Bool CinemaDirector_parseLine( const char *line, CinemaShot *shot, Bool *isShot, AsciiString *reason );

/// smoothstep: 0 at 0, 1 at 1, level at both ends, which is what makes a move ease in and out
Real CinemaDirector_ease( Real t );

/// a point on a Catmull-Rom curve through points[], t from 0 at the first point to 1 at the last
void CinemaDirector_routePoint( const Real *xs, const Real *ys, Int count, Real t, Real *x, Real *y );

/// once a render pass, after the logic tick
void CinemaDirector_update( void );

/// the console's hidehud: the interface off or back on without a shot list, the radar kept in the
/// bottom left corner when showMap is set
void CinemaDirector_setHudHidden( Bool hidden, Bool showMap );
Bool CinemaDirector_isHudHidden( void );
Bool CinemaDirector_showsMap( void );
void CinemaDirector_forgetHudHidden( void );

/// the interface is off, under -cinema or the console's freecam: nothing but the world is drawn
Bool CinemaDirector_hidesHud( void );

#endif // __CINEMADIRECTOR_H_
