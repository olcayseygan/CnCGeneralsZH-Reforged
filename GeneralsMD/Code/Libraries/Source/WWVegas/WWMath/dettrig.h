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

/* dettrig.h - trigonometry that gives the same answer on every machine.
 *
 * The C runtime's transcendentals are the one piece of the simulation's
 * arithmetic that is not pinned down.  IEEE 754 requires +, -, *, / and sqrt
 * to be correctly rounded, so every machine agrees on those to the last bit.
 * It says nothing about sin, cos, atan2, asin or acos, and implementations
 * duly differ: ucrtbase dispatches on the host CPU, its answer changes with
 * the Windows version because the DLL ships with the machine rather than with
 * the game, and the x87 FSIN and FCOS instructions are microcoded differently
 * by Intel and by AMD.  A lockstep simulation whose two ends disagree about
 * one unit's facing by one bit has two different games running a second later.
 *
 * These functions are integer arithmetic over a committed table, so they carry
 * no machine dependence at all.  Everything in the engine that used to reach
 * for the runtime goes through here: WWMath::Sin and friends, the rotation
 * helpers in matrix3.h, matrix3d.h and vector3.h, and Lib/Trig.h on the game
 * side.
 *
 * Deliberately absent: a square root.  IEEE requires sqrt to be correctly
 * rounded, so it is already identical everywhere, and a table version of it
 * could only be worse.  Same for +, -, *, /, fabs, floor and ceil.
 *
 * Accuracy, measured against the reference over the whole range: 1.6e-7
 * radians for Sin and Cos, 2.2e-7 for ATan2, 4.2e-7 for ACos, 3.0e-7 for ASin.
 * That is about one float epsilon.  Tools/gentrigtables.py generates the table
 * and carries the error budget.
 */

#ifndef DETTRIG_H
#define DETTRIG_H

#if defined(_MSC_VER) && (_MSC_VER >= 1000)
#pragma once
#endif

namespace DetTrig
{

	float Sin( float radians );
	float Cos( float radians );
	float Tan( float radians );

	float ATan( float x );
	float ATan2( float y, float x );		///< the angle of (x, y), in (-PI, PI]
	float ACos( float x );							///< clamps its argument to [-1, 1]
	float ASin( float x );							///< clamps its argument to [-1, 1]

	// The same tables with no float on either side, for the fixed point simulation (Lib/Fix.h).  An
	// angle is a fraction of a turn over the whole unsigned int, 2^32 to the turn; a sine or cosine
	// comes back scaled by 2^24.
	int SinTurn( unsigned int turn );
	int CosTurn( unsigned int turn );
	long long ATan2Turn( long long y, long long x );	///< in turns, in [-2^31, 2^31]; (0, 0) gives 0

}	// namespace DetTrig

#endif // DETTRIG_H
