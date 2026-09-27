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

// FILE: DrawnPath.cpp //////////////////////////////////////////////////////////////////////////

#include "PreRTS.h"

#include "Common/DrawnPath.h"
#include "GameLogic/Object.h"
#include "Lib/FixBoundary.h"

//-------------------------------------------------------------------------------------------------
void buildPathArcLengths( const std::vector<Coord3D>& path, std::vector<Real>& arc )
{
	arc.clear();
	if (path.empty())
		return;

	arc.push_back( 0.0f );
	for (Int i = 1; i < (Int)path.size(); i++)
	{
		const Real dx = path[ i ].x - path[ i - 1 ].x;
		const Real dy = path[ i ].y - path[ i - 1 ].y;
		arc.push_back( arc[ i - 1 ] + sqrtf( dx * dx + dy * dy ) );
	}
}

//-------------------------------------------------------------------------------------------------
Real distanceAlongPath( const std::vector<Coord3D>& path, const std::vector<Real>& arc,
												Real x, Real y )
{
	Real best = 0.0f;
	Real bestDistSqr = -1.0f;

	for (Int i = 1; i < (Int)path.size(); i++)
	{
		const Real sx = path[ i - 1 ].x;
		const Real sy = path[ i - 1 ].y;
		const Real dx = path[ i ].x - sx;
		const Real dy = path[ i ].y - sy;
		const Real segLenSqr = dx * dx + dy * dy;

		Real t = 0.0f;
		if (segLenSqr > 0.0f)
		{
			t = ((x - sx) * dx + (y - sy) * dy) / segLenSqr;
			if (t < 0.0f)
				t = 0.0f;
			else if (t > 1.0f)
				t = 1.0f;
		}

		const Real px = sx + dx * t;
		const Real py = sy + dy * t;
		const Real distSqr = (x - px) * (x - px) + (y - py) * (y - py);
		if (bestDistSqr < 0.0f || distSqr < bestDistSqr)
		{
			bestDistSqr = distSqr;
			best = arc[ i - 1 ] + sqrtf( segLenSqr ) * t;
		}
	}

	return best;
}

//-------------------------------------------------------------------------------------------------
void pointAlongPath( const std::vector<Coord3D>& path, const std::vector<Real>& arc,
										 Real dist, Coord3D *out )
{
	Int i = 1;
	while (i < (Int)path.size() - 1 && arc[ i ] < dist)
		i++;

	const Real segLen = arc[ i ] - arc[ i - 1 ];
	const Real t = (segLen > 0.0f) ? ((dist - arc[ i - 1 ]) / segLen) : 0.0f;
	out->x = path[ i - 1 ].x + (path[ i ].x - path[ i - 1 ].x) * t;
	out->y = path[ i - 1 ].y + (path[ i ].y - path[ i - 1 ].y) * t;
	out->z = 0.0f;
}

//-------------------------------------------------------------------------------------------------
void orderAlongPath( std::vector<Object *>& movers, const std::vector<Coord3D>& path,
										 const std::vector<Real>& arc )
{
	const Int count = movers.size();

	// the keys are worked out once: the sort below asks for them n-squared times, and each one is a
	// walk of the whole curve
	std::vector<Real> keys;
	keys.reserve( count );
	for (Int i = 0; i < count; i++)
		keys.push_back( distanceAlongPath( path, arc,	// P7: the drawn path is float
																			 fixToReal( movers[ i ]->getPositionFix()->x ),
																			 fixToReal( movers[ i ]->getPositionFix()->y ) ) );

	// Insertion sort, keys and units together: the selection is a few dozen objects at most, and
	// the comparator is a total order, so the answer does not depend on the order they came in.
	for (Int i = 1; i < count; i++)
	{
		Object *held = movers[ i ];
		const Real heldKey = keys[ i ];

		Int j = i - 1;
		while (j >= 0)
		{
			if (keys[ j ] < heldKey || (keys[ j ] == heldKey && movers[ j ]->getID() < held->getID()))
				break;
			movers[ j + 1 ] = movers[ j ];
			keys[ j + 1 ] = keys[ j ];
			j--;
		}

		movers[ j + 1 ] = held;
		keys[ j + 1 ] = heldKey;
	}
}
