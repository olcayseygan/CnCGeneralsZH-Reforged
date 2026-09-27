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

// FILE: Geometry.h ///////////////////////////////////////////////////////////////////////////////
// Author: Colin Day, November 2001
// Desc:	 Geometry descriptions
///////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#ifndef __GEOMETRY_H_
#define __GEOMETRY_H_

#include "Lib/BaseType.h"
#include "Common/AsciiString.h"
#include "Common/Snapshot.h"
#include "Lib/Fix.h"

class INI;

//-------------------------------------------------------------------------------------------------
/** Geometry type descriptions, keep this in the same order as GeometryNames[] below
	*
	* NOTE: Do *NOT* change the order of these defines unless you update the
	* partition manager ... in particular theCollideTestProcs depend on the 
	* order of this geometry and the fact that the values start at 1
	*/
//-------------------------------------------------------------------------------------------------
enum GeometryType
{
	GEOMETRY_SPHERE = 0,	///< partition/collision testing as sphere. (majorRadius = radius)
	GEOMETRY_CYLINDER,		///< partition/collision testing as cylinder. (majorRadius = radius, height = height)
	GEOMETRY_BOX,					///< partition/collision testing as rectangular box (majorRadius = half len in forward dir; minorRadius = half len in side dir; height = height)

	GEOMETRY_NUM_TYPES,  // keep this last
	GEOMETRY_FIRST = GEOMETRY_SPHERE
};

#ifdef DEFINE_GEOMETRY_NAMES
static const char *GeometryNames[] = 
{
	"SPHERE",		
	"CYLINDER",	
	"BOX",			
	NULL
};
#endif  // end DEFINE_GEOMETRY_NAMES

//-------------------------------------------------------------------------------------------------
#if defined(_DEBUG) || defined(_INTERNAL)
enum ExtentModType
{
	EXTENTMOD_INVALID = 0,
	EXTENTMOD_TYPE = 1,
	EXTENTMOD_MAJOR = 2,
	EXTENTMOD_MINOR = 3,
	EXTENTMOD_HEIGHT = 4,
};
static const Real EXTENT_BIG_CHANGE = 10.0f;
#endif

//-------------------------------------------------------------------------------------------------
/** Geometry information */
//-------------------------------------------------------------------------------------------------
class GeometryInfo : public Snapshot
{
private:
	// the extents are fixed point, read from the INI straight into Fix; the Real getters below are P2 shims
	GeometryType m_type;
	Bool m_isSmall;						///< if true, geometry is assumed to fit in a single partition cell
	Fix m_height;
	Fix m_majorRadius;
	Fix m_minorRadius;

	Fix m_boundingCircleRadius;	///< not in INI file -- size of bounding circle (2d)
	Fix m_boundingSphereRadius;	///< not in INI -- size of bounding sphere (3d)

	void calcBoundingStuff();

protected:
	
	// snapshot methods
	virtual void crc( Xfer *xfer );
	virtual void xfer( Xfer *xfer );
	virtual void loadPostProcess( void );

public:
	
	static void parseGeometryType( INI* ini, void * /*instance*/, void *store, const void* /*userData*/ );
	static void parseGeometryIsSmall( INI* ini, void * /*instance*/, void *store, const void* /*userData*/ );
	static void parseGeometryHeight( INI* ini, void * /*instance*/, void *store, const void* /*userData*/ );
	static void parseGeometryMajorRadius( INI* ini, void * /*instance*/, void *store, const void* /*userData*/ );
	static void parseGeometryMinorRadius( INI* ini, void * /*instance*/, void *store, const void* /*userData*/ );

	GeometryInfo(GeometryType type, Bool isSmall, Real height, Real majorRadius, Real minorRadius)
	{
		// Added by Sadullah Nader
		// Initializations missing and needed
		m_boundingCircleRadius = Fix( 0 );
		m_boundingSphereRadius = Fix( 0 );
		//

		set(type, isSmall, height, majorRadius, minorRadius);
	}

	void setFix(GeometryType type, Bool isSmall, Fix height, Fix majorRadius, Fix minorRadius);

	inline void setMajorRadiusFix(Fix majorRadius) { m_majorRadius = majorRadius; calcBoundingStuff(); }
	inline void setMinorRadiusFix(Fix minorRadius) { m_minorRadius = minorRadius; calcBoundingStuff(); }

	inline GeometryType getGeomType() const { return m_type; }
	inline Bool getIsSmall() const { return m_isSmall; }
	inline Fix getMajorRadiusFix() const { return m_majorRadius; }	// x-axis
	inline Fix getMinorRadiusFix() const { return m_minorRadius; }	// y-axis
	inline Fix getBoundingCircleRadiusFix() const { return m_boundingCircleRadius; }
	inline Fix getBoundingSphereRadiusFix() const { return m_boundingSphereRadius; }
	Fix getMaxHeightAbovePositionFix() const;
	Fix getMaxHeightBelowPositionFix() const;
	Fix getZDeltaToCenterPositionFix() const;
	void setMaxHeightAbovePositionFix(Fix z);

	// P2 SHIMS: the float face of the extents, converted on the way in and out.  P9 deletes them.
	void set(GeometryType type, Bool isSmall, Real height, Real majorRadius, Real minorRadius);
	void setMajorRadius(Real majorRadius);
	void setMinorRadius(Real minorRadius);
	Real getMajorRadius() const;
	Real getMinorRadius() const;
	Real getBoundingCircleRadius() const;
	Real getBoundingSphereRadius() const;
	// END P2 SHIMS

	Bool isIntersectedByLineSegment(const Coord3D& loc, const Coord3D& from, const Coord3D& to) const;

	Real getFootprintArea() const;

	// given an object with this geom, how far above the object's canonical position does its max z extend?
	Real getMaxHeightAbovePosition() const;

	// given an object with this geom, how far below the object's canonical position does its max z extend?
	Real getMaxHeightBelowPosition() const;

	// given an object with this geom, how far above/below the object's canonical position is its center?
	Real getZDeltaToCenterPosition() const;

	// given an object with this geom, located at 'pos', where is the "center" of the geometry?
	void getCenterPosition(const Coord3D& pos, Coord3D& center) const;

	// given an object with this geom, located at 'pos', and another obj with the given
	// pos & geom, calc the min and max pitches from this to that.
	void calcPitches(const Coord3D& thisPos, const GeometryInfo& that, const Coord3D& thatPos,
		Real& minPitch, Real& maxPitch) const;

	/// expand the 2d footprint
	void expandFootprint(Real radius);

	/// get the 2d bounding box
	void get2DBounds(const Coord3D& geomCenter, Real angle, Region2D& bounds	) const;

	/// note that the pt is generated using game logic random, not game client random!
	/**
		Pick a random point inside this footprint.  Pass useClientRandom when the point is only ever
		going to be looked at - a particle, a spark - because whether that effect exists at all is a
		decision the player's detail settings get to make, and the simulation may not depend on it.
		Anything that places a real object keeps the default and rolls from the shared logic stream.
	*/
	void makeRandomOffsetWithinFootprint(Coord3D& pt, Bool useClientRandom = FALSE) const;
	void makeRandomOffsetOnPerimeter(Coord3D& pt) const; //Chooses a random point on the extent border.

	void clipPointToFootprint(const Coord3D& geomCenter, Coord3D& ptToClip) const;

	Bool isPointInFootprint(const Coord3D& geomCenter, const Coord3D& pt) const;

	// given an object with this geom, SET how far above the object's canonical position its max z should extend.
	void setMaxHeightAbovePosition(Real z);

#if defined(_DEBUG) || defined(_INTERNAL)
	void tweakExtents(ExtentModType extentModType, Real extentModAmount);
	AsciiString getDescriptiveString() const;
#endif

};

#endif 

