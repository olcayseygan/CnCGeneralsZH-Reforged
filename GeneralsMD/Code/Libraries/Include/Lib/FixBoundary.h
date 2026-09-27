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

/* FixBoundary.h - the only door between Fix and float.
 *
 * Kept out of Fix.h so that a GameLogic file which includes this one is a file that crosses the
 * boundary, and a grep finds every one of them.
 *
 * The way in reads the float's bits and decodes them with integer operations, so it gives the same
 * Fix on every machine whatever the FPU is set to; it rounds to the nearest step, ties to even.
 * NaN asserts and gives zero, and anything outside the range saturates.  The way out is one
 * int64-to-float conversion and one multiply by a power of two, both exact or correctly rounded.
 */

#pragma once

#ifndef _LIB_FIX_BOUNDARY_H_
#define _LIB_FIX_BOUNDARY_H_

#include "Lib/Fix.h"
#include <string.h>

Fix fixFromFloatBits( UnsignedInt bits );

inline Fix fixFromReal( Real r )
{
	UnsignedInt bits;
	memcpy( &bits, &r, sizeof( bits ) );
	return fixFromFloatBits( bits );
}

inline Real fixToReal( Fix f )
{
	return (Real)f.raw() * (1.0f / (Real)Fix::ONE_RAW);
}

inline FCoord3D fcoordFromCoord3D( const Coord3D &c )
{
	FCoord3D f;
	f.set( fixFromReal( c.x ), fixFromReal( c.y ), fixFromReal( c.z ) );
	return f;
}

inline FCoord3D fcoordFromCoord3D( const Coord3D *c )
{
	return fcoordFromCoord3D( *c );
}

/// every cell through fixFromReal; the way back is FixMatrix3D::toMatrix3D
FixMatrix3D fixMatrixFromMatrix3D( const Matrix3D &in );

#endif // _LIB_FIX_BOUNDARY_H_
