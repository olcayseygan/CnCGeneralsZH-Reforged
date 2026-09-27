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

// FILE: Geometry.cpp /////////////////////////////////////////////////////////////////////////////
// Author: Steven Johnson, Aug 2002
// Desc:
///////////////////////////////////////////////////////////////////////////////////////////////////
#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#define DEFINE_GEOMETRY_NAMES

// USER INCLUDES //////////////////////////////////////////////////////////////////////////////////
#include "Common/Geometry.h"
#include "Common/INI.h"
#include "Common/RandomValue.h"
#include "GameClient/ClientRandomValue.h"
#include "Common/Xfer.h"
#include "Lib/FixBoundary.h"

#ifdef _INTERNAL
// for occasional debugging...
//#pragma optimize("", off)
//#pragma MESSAGE("************************************** WARNING, optimization disabled for debugging purposes")
#endif

///////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////

//=============================================================================
/*static*/ void GeometryInfo::parseGeometryType( INI* ini, void * /*instance*/, void *store, const void* /*userData*/ )
{
	((GeometryInfo*)store)->m_type = (GeometryType)INI::scanIndexList(ini->getNextToken(), GeometryNames);
	((GeometryInfo*)store)->calcBoundingStuff();
}

//=============================================================================
/*static*/ void GeometryInfo::parseGeometryIsSmall( INI* ini, void * /*instance*/, void *store, const void* /*userData*/ )
{
	((GeometryInfo*)store)->m_isSmall = INI::scanBool(ini->getNextToken());
	((GeometryInfo*)store)->calcBoundingStuff();
}

//=============================================================================
/*static*/ void GeometryInfo::parseGeometryHeight( INI* ini, void * /*instance*/, void *store, const void* /*userData*/ )
{
	((GeometryInfo*)store)->m_height = INI::scanFix(ini->getNextToken());
	((GeometryInfo*)store)->calcBoundingStuff();
}

//=============================================================================
/*static*/ void GeometryInfo::parseGeometryMajorRadius( INI* ini, void * /*instance*/, void *store, const void* /*userData*/ )
{
	((GeometryInfo*)store)->m_majorRadius = INI::scanFix(ini->getNextToken());
	((GeometryInfo*)store)->calcBoundingStuff();
}

//=============================================================================
/*static*/ void GeometryInfo::parseGeometryMinorRadius( INI* ini, void * /*instance*/, void *store, const void* /*userData*/ )
{
	((GeometryInfo*)store)->m_minorRadius = INI::scanFix(ini->getNextToken());
	((GeometryInfo*)store)->calcBoundingStuff();
}

//-----------------------------------------------------------------------------
void GeometryInfo::setFix(GeometryType type, Bool isSmall, Fix height, Fix majorRadius, Fix minorRadius)
{
	m_type = type;
	m_isSmall = isSmall;

	switch(m_type)
	{
		case GEOMETRY_SPHERE:
			m_majorRadius = majorRadius;
			m_minorRadius = majorRadius;
			m_height = majorRadius;
			break;

		case GEOMETRY_CYLINDER:
			m_majorRadius = majorRadius;
			m_minorRadius = majorRadius;
			m_height = height;
			break;

		case GEOMETRY_BOX:
			m_majorRadius = majorRadius;
			m_minorRadius = minorRadius;
			m_height = height;
			break;
	};

	calcBoundingStuff();
}

//-----------------------------------------------------------------------------
// P9 shims, for the client and the float callers of P4 to P7
//-----------------------------------------------------------------------------
void GeometryInfo::set(GeometryType type, Bool isSmall, Real height, Real majorRadius, Real minorRadius)
{
	setFix(type, isSmall, fixFromReal(height), fixFromReal(majorRadius), fixFromReal(minorRadius));
}
Real GeometryInfo::getMajorRadius() const { return fixToReal(m_majorRadius); }
Real GeometryInfo::getMinorRadius() const { return fixToReal(m_minorRadius); }
Real GeometryInfo::getBoundingCircleRadius() const { return fixToReal(m_boundingCircleRadius); }
Real GeometryInfo::getBoundingSphereRadius() const { return fixToReal(m_boundingSphereRadius); }
Real GeometryInfo::getMaxHeightAbovePosition() const { return fixToReal(getMaxHeightAbovePositionFix()); }
Real GeometryInfo::getMaxHeightBelowPosition() const { return fixToReal(getMaxHeightBelowPositionFix()); }
void GeometryInfo::setMaxHeightAbovePosition(Real z) { setMaxHeightAbovePositionFix(fixFromReal(z)); }
void GeometryInfo::expandFootprint(Real radius)
{
	m_majorRadius += fixFromReal(radius);
	m_minorRadius += fixFromReal(radius);
	calcBoundingStuff();
}

//-----------------------------------------------------------------------------
static Real calcDotProduct(const Coord3D& a, const Coord3D& b)
{
	return a.x*b.x + a.y*b.y + a.z*b.z;
}

//-----------------------------------------------------------------------------
static Real calcDistSquared(const Coord3D& a, const Coord3D& b)
{
	return sqr(a.x - b.x) + sqr(a.y - b.y) + sqr(a.z - b.z);
}

//-----------------------------------------------------------------------------
static Real calcPointToLineDistSquared(const Coord3D& pt, const Coord3D& lineStart, const Coord3D& lineEnd)
{
	Coord3D line, lineToPt, closest;

	line.x = lineEnd.x - lineStart.x;
	line.y = lineEnd.y - lineStart.y;
	line.z = lineEnd.z - lineStart.z;

	lineToPt.x = pt.x - lineStart.x;
	lineToPt.y = pt.y - lineStart.y;
	lineToPt.z = pt.z - lineStart.z;

	Real dot = calcDotProduct(lineToPt, line);
	if (dot <= 0.0f)
	{
		// angle is obtuse
		return calcDistSquared(pt, lineStart);
	}

	Real lineLenSqr = calcDistSquared(lineStart, lineEnd);
	DEBUG_ASSERTCRASH(lineLenSqr==calcDotProduct(line,line),("hmm"));
	if (lineLenSqr <= dot)
	{
		return calcDistSquared(pt, lineEnd);
	}

  Real tmp = dot / lineLenSqr;

	closest.x = lineStart.x + tmp * line.x;
	closest.y = lineStart.y + tmp * line.y;
	closest.z = lineStart.z + tmp * line.z;

  return calcDistSquared(pt, closest);
}

//=============================================================================
Bool GeometryInfo::isIntersectedByLineSegment(const Coord3D& loc, const Coord3D& from, const Coord3D& to) const
{
	DEBUG_CRASH(("this call does not work properly for nonspheres yet. use with caution."));

	/// @todo srj -- treats everything as a sphere for now. fix.
	Real distSquared = calcPointToLineDistSquared(loc, from, to);
	return distSquared <= sqr(getBoundingSphereRadius());
}

//=============================================================================
// given an object with this geom, located at 'pos', and another obj with the given
// pos & geom, calc the min and max pitches from this to that.
void GeometryInfo::calcPitches(const Coord3D& thisPos, const GeometryInfo& that, const Coord3D& thatPos,
	Real& minPitch, Real& maxPitch) const
{
	Coord3D thisCenter;
	getCenterPosition(thisPos, thisCenter);

	Real dxy = sqrt(sqr(thatPos.x - thisCenter.x) + sqr(thatPos.y - thisCenter.y));

	Real dz;

	/** @todo srj -- this could be better, by calcing it for all the corners, not just top-center
		and bottom-center... oh well */
	dz = (thatPos.z + that.getMaxHeightAbovePosition()) - thisCenter.z;
	maxPitch = ATan2(dz, dxy);

	dz = (thatPos.z - that.getMaxHeightBelowPosition()) - thisCenter.z;
	minPitch = ATan2(dz, dxy);
}

//=============================================================================
// given an object with this geom, SET how far above the object's canonical position its max z should extend.
void GeometryInfo::setMaxHeightAbovePositionFix(Fix z)
{
	switch(m_type)
	{
		case GEOMETRY_SPHERE:
			m_majorRadius = z;
			break;

		case GEOMETRY_BOX:
		case GEOMETRY_CYLINDER:
			m_height = z;
			break;
	};

	calcBoundingStuff();
}

//=============================================================================
// given an object with this geom, how far above the object's canonical position does its max z extend?
Fix GeometryInfo::getMaxHeightAbovePositionFix() const
{
	switch(m_type)
	{
		case GEOMETRY_SPHERE:
			return m_majorRadius;

		case GEOMETRY_BOX:
		case GEOMETRY_CYLINDER:
			return m_height;
	};

	return Fix(0);
}

//=============================================================================
// given an object with this geom, how far below the object's canonical position does its max z extend?
Fix GeometryInfo::getMaxHeightBelowPositionFix() const
{
	return (m_type == GEOMETRY_SPHERE) ? m_majorRadius : Fix(0);
}

//=============================================================================
// given an object with this geom, located at 'pos', where is the "center" of the geometry?
Fix GeometryInfo::getZDeltaToCenterPositionFix() const
{
	return (m_type == GEOMETRY_SPHERE) ? Fix(0) : (m_height * 0.5_fx);
}

//=============================================================================
// given an object with this geom, located at 'pos', where is the "center" of the geometry?
void GeometryInfo::getCenterPosition(const Coord3D& pos, Coord3D& center) const
{
	center = pos;
	center.z += fixToReal(getZDeltaToCenterPositionFix());
}

//=============================================================================
void GeometryInfo::get2DBounds(const Coord3D& geomCenter, Real angle, Region2D& bounds) const
{
	const Real majorRadius = getMajorRadius();
	const Real minorRadius = getMinorRadius();
	switch(m_type)
	{
		case GEOMETRY_SPHERE:
		case GEOMETRY_CYLINDER:
		{
			bounds.lo.x = geomCenter.x - majorRadius;
			bounds.lo.y = geomCenter.y - majorRadius;
			bounds.hi.x = geomCenter.x + majorRadius;
			bounds.hi.y = geomCenter.y + majorRadius;
			break;
		}

		case GEOMETRY_BOX:
		{
			Real c = (Real)Cos(angle);
			Real s = (Real)Sin(angle);
			Real exc = majorRadius*c;
			Real eyc = minorRadius*c;
			Real exs = majorRadius*s;
			Real eys = minorRadius*s;
			Real x,y;
			x = geomCenter.x - exc - eys;
			y = geomCenter.y + eyc - exs;
			bounds.lo.x = x;
			bounds.lo.y = y;
			bounds.hi.x = x;
			bounds.hi.y = y;

			x = geomCenter.x + exc - eys;
			y = geomCenter.y + eyc + exs;
			if (bounds.lo.x > x) bounds.lo.x = x;
			if (bounds.lo.y > y) bounds.lo.y = y;
			if (bounds.hi.x < x) bounds.hi.x = x;
			if (bounds.hi.y < y) bounds.hi.y = y;

			x = geomCenter.x + exc + eys;
			y = geomCenter.y - eyc + exs;
			if (bounds.lo.x > x) bounds.lo.x = x;
			if (bounds.lo.y > y) bounds.lo.y = y;
			if (bounds.hi.x < x) bounds.hi.x = x;
			if (bounds.hi.y < y) bounds.hi.y = y;

			x = geomCenter.x - exc + eys;
			y = geomCenter.y - eyc - exs;
 			if (bounds.lo.x > x) bounds.lo.x = x;
			if (bounds.lo.y > y) bounds.lo.y = y;
			if (bounds.hi.x < x) bounds.hi.x = x;
			if (bounds.hi.y < y) bounds.hi.y = y;

			break;
		}
	}
}

//=============================================================================
void GeometryInfo::clipPointToFootprint(const Coord3D& geomCenter, Coord3D& ptToClip) const
{
	const Real majorRadius = getMajorRadius();
	const Real minorRadius = getMinorRadius();
	switch(m_type)
	{
		case GEOMETRY_SPHERE:
		case GEOMETRY_CYLINDER:
		{
			Real dx = ptToClip.x - geomCenter.x;
			Real dy = ptToClip.y - geomCenter.y;
			Real radius = sqrt(sqr(dx) + sqr(dy));
			if (radius > majorRadius)
			{
				Real ratio = majorRadius / radius;
				ptToClip.x = geomCenter.x + dx * ratio;
				ptToClip.y = geomCenter.y + dy * ratio;
			}
			break;
		}

		case GEOMETRY_BOX:
		{
			ptToClip.x = clamp(geomCenter.x - majorRadius, ptToClip.x, geomCenter.x + majorRadius);
			ptToClip.y = clamp(geomCenter.y - minorRadius, ptToClip.y, geomCenter.y + minorRadius);
			break;
		}
	};
}

//=============================================================================
inline Bool isWithin(Real a, Real b, Real c) { return a<=b && b<=c; }

//=============================================================================
Bool GeometryInfo::isPointInFootprint(const Coord3D& geomCenter, const Coord3D& pt) const
{
	const Real majorRadius = getMajorRadius();
	const Real minorRadius = getMinorRadius();
	switch(m_type)
	{
		case GEOMETRY_SPHERE:
		case GEOMETRY_CYLINDER:
		{
			Real dx = pt.x - geomCenter.x;
			Real dy = pt.y - geomCenter.y;
			Real radius = sqrt(sqr(dx) + sqr(dy));
			return (radius <= majorRadius);
			break;
		}

		case GEOMETRY_BOX:
		{
			return isWithin(geomCenter.x - majorRadius, pt.x, geomCenter.x + majorRadius) &&
							isWithin(geomCenter.y - minorRadius, pt.y, geomCenter.y + minorRadius);
		}
	};
	return false;
}

//=============================================================================
static Real footprintRandom(Real lo, Real hi, Bool useClientRandom)
{
	return useClientRandom ? GameClientRandomValueReal(lo, hi) : GameLogicRandomValueReal(lo, hi);
}

//=============================================================================
void GeometryInfo::makeRandomOffsetWithinFootprint(Coord3D& pt, Bool useClientRandom) const
{
	const Real majorRadius = getMajorRadius();
	const Real minorRadius = getMinorRadius();
	switch(m_type)
	{
		case GEOMETRY_SPHERE:
		case GEOMETRY_CYLINDER:
		{
			// this is a better technique than the more obvious radius-and-angle
			// one, because the latter tends to clump more towards the center.
			Real maxDistSqr = sqr(majorRadius);
			Real distSqr;
			do
			{
				pt.x = footprintRandom(-majorRadius, majorRadius, useClientRandom);
				pt.y = footprintRandom(-majorRadius, majorRadius, useClientRandom);
				pt.z = 0.0f;
				distSqr = sqr(pt.x) + sqr(pt.y);
			} while (distSqr > maxDistSqr);
			break;
		}

		case GEOMETRY_BOX:
		{
			pt.x = footprintRandom(-majorRadius, majorRadius, useClientRandom);
			pt.y = footprintRandom(-minorRadius, minorRadius, useClientRandom);
			pt.z = 0.0f;
			break;
		}
	};
}

//=============================================================================
void GeometryInfo::makeRandomOffsetOnPerimeter(Coord3D& pt) const
{
	const Real majorRadius = getMajorRadius();
	const Real minorRadius = getMinorRadius();
	switch(m_type)
	{
		case GEOMETRY_SPHERE:
		case GEOMETRY_CYLINDER:
		{
			DEBUG_CRASH( ("GeometryInfo::makeRandomOffsetOnPerimeter() not implemented for SPHERE or CYLINDER extents. Using position.") );

			//Kris: Did not have time nor need to support non-box extents. I added this feature for script placement
			//      of boobytraps.
			pt.x = 0.0f;
			pt.y = 0.0f;
			break;
		}

		case GEOMETRY_BOX:
		{
			if( GameLogicRandomValueReal( 0.0f, 1.0f ) < 0.5f )
			{
				//Pick random point on x axis.
				pt.x = GameLogicRandomValueReal(-majorRadius, majorRadius);

				//Min or max the y axis value
				if( GameLogicRandomValueReal( 0.0f, 1.0f ) < 0.5f )
					pt.y = -minorRadius;
				else
					pt.y = minorRadius;
			}
			else
			{
				//Pick random point on y axis.
				pt.y = GameLogicRandomValueReal(-minorRadius, minorRadius);

				//Min or max the x axis value
				if( GameLogicRandomValueReal( 0.0f, 1.0f ) < 0.5f )
					pt.x = -majorRadius;
				else
					pt.x = majorRadius;
			}
			pt.z = 0.0f;
			break;
		}
	};
}

//=============================================================================
Real GeometryInfo::getFootprintArea() const
{
	switch(m_type)
	{
		case GEOMETRY_SPHERE:
		case GEOMETRY_CYLINDER:
		{
			return PI * sqr(getBoundingCircleRadius());
		}

		case GEOMETRY_BOX:
		{
			return 4.0f * getMajorRadius() * getMinorRadius();
		}
	};

	DEBUG_CRASH(("should never get here"));
	return 0.0f;
}

//=============================================================================
void GeometryInfo::calcBoundingStuff()
{
	switch(m_type)
	{
		case GEOMETRY_SPHERE:
		{
			m_boundingSphereRadius = m_majorRadius;
			m_boundingCircleRadius = m_majorRadius;
			break;
		}
		case GEOMETRY_CYLINDER:
		{
			m_boundingCircleRadius = m_majorRadius;
			m_boundingSphereRadius = fixMax(m_height * 0.5_fx, m_majorRadius);
			break;
		}

		case GEOMETRY_BOX:
		{
			const Fix halfHeight = m_height * 0.5_fx;
			m_boundingCircleRadius = fixSqrt(m_majorRadius * m_majorRadius + m_minorRadius * m_minorRadius);
			m_boundingSphereRadius = fixSqrt(m_majorRadius * m_majorRadius + m_minorRadius * m_minorRadius + halfHeight * halfHeight);
			break;
		}
	};
}

#if defined(_DEBUG) || defined(_INTERNAL)
//=============================================================================
void GeometryInfo::tweakExtents(ExtentModType extentModType, Real extentModAmount)
{
	switch(extentModType)
	{
		case EXTENTMOD_HEIGHT:
			m_height += fixFromReal(extentModAmount);
			break;
		case EXTENTMOD_MAJOR:
			m_majorRadius += fixFromReal(extentModAmount);
			break;
		case EXTENTMOD_MINOR:
			m_minorRadius += fixFromReal(extentModAmount);
			break;
		case EXTENTMOD_TYPE:
			m_type = (GeometryType)((m_type + ((extentModType == EXTENTMOD_TYPE)?1:0)) % GEOMETRY_NUM_TYPES);
			break;
	}
	m_isSmall = false;
	calcBoundingStuff();
}
#endif

#if defined(_DEBUG) || defined(_INTERNAL)
//=============================================================================
AsciiString GeometryInfo::getDescriptiveString() const
{
	AsciiString msg;
	msg.format("%d/%d(%g %g %g)", (Int)m_type, (Int)m_isSmall, getMajorRadius(), getMinorRadius(), fixToReal(m_height));
	return msg;
}
#endif

// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void GeometryInfo::crc( Xfer *xfer )
{

}  // end crc

// ------------------------------------------------------------------------------------------------
/** Xfer method
	* Version Info:
	* 1: Initial version
	* 2: the extents in fixed point */
// ------------------------------------------------------------------------------------------------
void GeometryInfo::xfer( Xfer *xfer )
{

	// version
	XferVersion currentVersion = 2;
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );

	// type
	xfer->xferUser( &m_type, sizeof( GeometryType ) );

	// is small
	xfer->xferBool( &m_isSmall );

	Fix *extents[] = { &m_height, &m_majorRadius, &m_minorRadius, &m_boundingCircleRadius, &m_boundingSphereRadius };
	for( Int i = 0; i < 5; ++i )
	{
		if( version >= 2 )
			xfer->xferFix( extents[ i ] );
		else
		{
			Real r = fixToReal( *extents[ i ] );
			xfer->xferReal( &r );
			*extents[ i ] = fixFromReal( r );
		}
	}

}  // end xfer

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void GeometryInfo::loadPostProcess( void )
{

}  // end loadPostProcess
