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

// FILE: PartitionManager.cpp ////////////////////////////////////////////////////
//-----------------------------------------------------------------------------
//                                                                          
//                       Westwood Studios Pacific.                          
//                                                                          
//                       Confidential Information                           
//                Copyright (C) 2001 - All Rights Reserved                  
//                                                                          
//-----------------------------------------------------------------------------
//
// Project:   RTS3
//
// File name: PartitionManager.cpp
//
// Created:   Steven Johnson, September 2001
//
// Desc:      Partition management, this system will allow us to partition the
//					  objects in space, iterate objects in specified volumes, 
//					  regions, by types and other properties.
//
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
//         Includes                                                      
//-----------------------------------------------------------------------------
#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "Common/ActionManager.h"
#include "Common/DiscreteCircle.h"
#include "Common/GameEngine.h"
#include "Common/GameState.h"
#include "Common/MessageStream.h"
#include "Common/NameKeyGenerator.h"
#include "Common/PerfTimer.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/Radar.h"
#include "Common/ThingFactory.h"	// for bullet type hack
#include "Common/ThingTemplate.h"
#include "Common/Xfer.h"

#include "GameLogic/AIPathfind.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/IncomingDamage.h"
#include "GameLogic/Object.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/BodyModule.h"
#include "GameLogic/Module/CollideModule.h"
#include "GameLogic/Module/ContainModule.h"
#include "GameLogic/Module/StealthUpdate.h"
#include "GameLogic/PartitionManager.h"
#include "GameLogic/AI.h"					// aiIsIncomeNotATarget - a derrick is money, not a target
#include "GameLogic/PolygonTrigger.h"
#include "GameLogic/Squad.h"
#include "GameLogic/GhostObject.h"
#include "GameLogic/Weapon.h"

#include "GameClient/Line2D.h"
#include "GameClient/ControlBar.h"
#include "GameClient/ObserverCamera.h"

#include <float.h>

#ifdef _DEBUG
//#include "GameClient/InGameUI.h"	// for debugHints
#include "Common/PlayerList.h"
#endif

#include "Lib/FixBoundary.h"

#ifdef PM_CACHE_TERRAIN_HEIGHT
#include "common/mapobject.h"
#endif

/* Range queries this logic frame, and objects looked at inside them. Zeroed by
	 PartitionManager::update, read by the slow-frame log; counts only, no clock, because this sits
	 in the innermost loop of the most called function in the game. */
static Int thePartitionQueries = 0;
static Int thePartitionGathers = 0;
static Int thePartitionQueryObjects = 0;

Int PartitionManager::getQueryCountThisFrame( void ) { return thePartitionQueries; }
Int PartitionManager::getGatherCountThisFrame( void ) { return thePartitionGathers; }
Int PartitionManager::getQueryObjectCountThisFrame( void ) { return thePartitionQueryObjects; }
void PartitionManager::resetQueryCounts( void ) { thePartitionQueries = 0; thePartitionGathers = 0; thePartitionQueryObjects = 0; }

#ifdef DUMP_PERF_STATS
	long s_countInClosestObjects = 0;
	long s_countInClosestObjectsThisFrame = 0;
	Int64 s_timeInClosestObjects = 0;
	Int64 s_timeInClosestObjectsThisFrame = 0;
	UnsignedInt s_gcoPerfFrame = 0xffffffff;
#endif 

#ifdef _INTERNAL
// for occasional debugging...
//#pragma optimize("", off)
//#pragma MESSAGE("************************************** WARNING, optimization disabled for debugging purposes")
#endif

extern void addIcon(const Coord3D *pos, Real width, Int numFramesDuration, RGBColor color);

// HUGE_DIST in fixed point, and its square, which Q47.16 holds with a factor of 140 to spare
static constexpr Fix HUGE_DIST_FIX = Fix( 1000000 );
static constexpr Fix HUGE_DIST_SQR_FIX = Fix::fromRaw( (Int64)1000000 * 1000000 * Fix::ONE_RAW );

#define DISABLE_INVALID_PREVENTION	//Steven, I had to turn this off because it was causing problem with map border resizing (USA04). -MW

//------------------------------------------------------------------------------ Performance Timers 
//#include "Common/PerfMetrics.h"
//#include "Common/PerfTimer.h"

//-------------------------------------------------------------------------------------------------
//-----------------------------------------------------------------------------
//         Defines                                                         
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
static PartitionContactList* TheContactList = NULL;

//-----------------------------------------------------------------------------
//         Local Types                                                      
//-----------------------------------------------------------------------------
struct ThreatValueParms
{
	Int playerIndex;
	Real xCenter;
	Real yCenter;
	Real radius;
	UnsignedInt threatOrValue;
};

struct CollideInfo
{
	FCoord3D position;
	GeometryInfo geom;
	Fix angle;

	CollideInfo(const FCoord3D* p, const GeometryInfo& g, Fix a) : position(*p), geom(g), angle(a) { }
};

/* The collision tests run in fixed point.  CollideLocAndNormal is float in the shared header and
	 the collide modules take it that way (P4), so a result crosses over once, where a test is
	 dispatched. */
struct FCollideLocAndNormal
{
	FCoord3D loc;
	FCoord3D normal;
};

/// toward zero, the way the (Int) conversion of a float did
static Int fixTruncToInt(Fix f)
{
	return (Int)(f.raw() / Fix::ONE_RAW);
}

struct CellValueProcParms
{
	Int valueRequired;
	Bool greaterThan;
	ValueOrThreat valueType;
	PlayerMaskType allPlayersMask[MAX_PLAYER_COUNT];
	PlayerMaskType allowedPlayersMasks;
};

static int cellValueProc(PartitionCell* cell, void* userData);

/*
	Notes:
	-- collideLoc and collideNormal will only be filled in if result is true;
			they are undefined if result is false.
	-- either collideLoc or collideNormal may be null if you don't need 'em.
	-- collideLoc is not guaranteed to lie precisely on either 'a' or 'b'! 
			(this matters iff the objects are overlapping and thus have multiple/ambiguous 
			collision points; we pick an 'in-between' spot to simplify matters, such that
			the same spot will be used for the collision in both directions)
	-- collideNormal is the normal the collision surface at 'a' at collideLoc.
			(thus the normal for b is exactly opposite in direction)
	-- collideNormal is guaranteed to be a unit vector
*/
typedef Bool (*CollideTestProc)(const CollideInfo *a, const CollideInfo *b, FCollideLocAndNormal *cinfo);

// if the dist is greater than maxDist, return false, and the output stuff is undefined.
typedef Bool (*DistCalcProc)
(
	const FCoord3D *posA,
	const Object *objA,
	const FCoord3D *posB,
	const Object *objB,
	Fix& abDistSqr,
	FCoord3D& abVec,
	Fix maxDistSqr
);

//-----------------------------------------------------------------------------
//         Inline Functions                                                      
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
#define NO_FILTER_PROFILING

#ifdef FILTER_PROFILING
Bool DoFilterProfiling = false;
#endif

//DECLARE_PERF_TIMER(filtersAllow)
inline Bool filtersAllow(PartitionFilter **filters, Object *objOther)
{
	//USE_PERF_TIMER(filtersAllow)
#ifdef FILTER_PROFILING
	const Int MAXR = 32;
	static const char* names[MAXR];
	static Int rejections[MAXR];
	static Int usefulRejections[MAXR];
	static Int calls = 0;
	static Int maxEver = 0;

	Int idx;
	Bool allow = true;

	if (DoFilterProfiling)
	{
		if (calls == 0)
		{
			for (idx = 0; idx < MAXR; ++idx)
			{
				names[idx] = NULL;
				rejections[idx] = 0;
				usefulRejections[idx] = 0;
			}
			maxEver = 0;
		}
		++calls;
	}

	for (PartitionFilter **fp = filters; fp && *fp; fp++)
	{
		if (!(*fp)->allow(objOther))
		{
			if (DoFilterProfiling)
			{
				const char** namesarr = names;
				for (idx = 0; idx < maxEver; ++idx)
					if (namesarr[idx] == (*fp)->debugGetName())
						break;
				if (idx == maxEver)
				{
					namesarr[maxEver++] = (*fp)->debugGetName();
					DEBUG_ASSERTCRASH(maxEver<MAXR,("hmm, enlarge this array"));
				}
				++rejections[idx];
				if (allow)
					++usefulRejections[idx];
				allow = false;
				if (maxEver < idx)
					maxEver = idx;
			}
			else
			{
				return false;
			}
		}
	}

	if (DoFilterProfiling && calls > 0 && calls % 1000==0)
	{
		DEBUG_LOG(("\n\n"));
		for (idx = 0; idx < maxEver; idx++)
		{
			DEBUG_LOG(("rejections[%s] = %d (useful = %d)\n",names[idx],rejections[idx],usefulRejections[idx]));
		}
	}

	return allow;
#else
	for (PartitionFilter **fp = filters; fp && *fp; fp++)
	{
		if (!(*fp)->allow(objOther))
		{
			return false;
		}
	}
	// assume true if no filters rejected it	
	return true;
#endif
}

//-----------------------------------------------------------------------------
inline void vecDiff_2D(const FCoord3D *posA, const FCoord3D *posB, FCoord3D *resultVec)
{
	DEBUG_ASSERTCRASH(posA && posB && resultVec, ("null parm"));
	resultVec->x = posA->x - posB->x;
	resultVec->y = posA->y - posB->y;
	resultVec->z = Fix(0);
}

//-----------------------------------------------------------------------------
inline void vecDiff_3D(const FCoord3D *posA, const FCoord3D *posB, FCoord3D *resultVec)
{
	DEBUG_ASSERTCRASH(posA && posB && resultVec, ("null parm"));
	resultVec->x = posA->x - posB->x;
	resultVec->y = posA->y - posB->y;
	resultVec->z = posA->z - posB->z;
}

//-----------------------------------------------------------------------------
inline Fix calcSqrDist_2D(const FCoord3D *dist)
{
	return dist->x * dist->x + dist->y * dist->y;
}

//-----------------------------------------------------------------------------
inline Fix calcSqrDist_3D(const FCoord3D *dist)
{
	return dist->lengthSqr();
}

//-----------------------------------------------------------------------------
/// a zero vector stays zero, as Coord3D::normalize leaves it
inline void normalizeFix(FCoord3D *v)
{
	Fix len = v->length();
	if (len > Fix(0))
		v->set(v->x / len, v->y / len, v->z / len);
}

//-----------------------------------------------------------------------------
inline Int absInt(Int a)
{
	if (a < 0) return -a; else return a;
}

//-----------------------------------------------------------------------------
inline Int minInt(Int a, Int b)
{
	if (a < b) return a; else return b;
}

//-----------------------------------------------------------------------------
inline Int maxInt(Int a, Int b)
{
	if (a > b) return a; else return b;
}

//-----------------------------------------------------------------------------
//         Local Functions                                                      
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------

static void rectToFourPoints(
	const CollideInfo *a,		// z is ignored
	FCoord2D pts[]
);
static void testRotatedPointsAgainstRect(
	const FCoord2D *pts,				// an array of 4
	const CollideInfo *a,
	FCoord2D *avg,
	Int *avgTot
);

static Bool xy_collideTest_Rect_Rect(const CollideInfo *a, const CollideInfo *b, FCollideLocAndNormal *cinfo);
static Bool xy_collideTest_Rect_Circle(const CollideInfo *a, const CollideInfo *b, FCollideLocAndNormal *cinfo);
static Bool xy_collideTest_Circle_Rect(const CollideInfo *a, const CollideInfo *b, FCollideLocAndNormal *cinfo);

static Bool collideTest_Sphere_Sphere(const CollideInfo *a, const CollideInfo *b, FCollideLocAndNormal *cinfo);
static Bool collideTest_Sphere_Cylinder(const CollideInfo *a, const CollideInfo *b, FCollideLocAndNormal *cinfo);
static Bool collideTest_Sphere_Box(const CollideInfo *a, const CollideInfo *b, FCollideLocAndNormal *cinfo);
static Bool collideTest_Cylinder_Sphere(const CollideInfo *a, const CollideInfo *b, FCollideLocAndNormal *cinfo);
static Bool collideTest_Cylinder_Cylinder(const CollideInfo *a, const CollideInfo *b, FCollideLocAndNormal *cinfo);
static Bool collideTest_Cylinder_Box(const CollideInfo *a, const CollideInfo *b, FCollideLocAndNormal *cinfo);
static Bool collideTest_Box_Sphere(const CollideInfo *a, const CollideInfo *b, FCollideLocAndNormal *cinfo);
static Bool collideTest_Box_Cylinder(const CollideInfo *a, const CollideInfo *b, FCollideLocAndNormal *cinfo);
static Bool collideTest_Box_Box(const CollideInfo *a, const CollideInfo *b, FCollideLocAndNormal *cinfo);


//-----------------------------------------------------------------------------
inline void projectCoord3D(FCoord3D *coord, const FCoord3D *unitDir, Fix dist)
{
	coord->x += unitDir->x * dist;
	coord->y += unitDir->y * dist;
	coord->z += unitDir->z * dist;
}

//-----------------------------------------------------------------------------
inline void flipCoord3D(FCoord3D *coord)
{
	coord->x = -coord->x;
	coord->y = -coord->y;
	coord->z = -coord->z;
}

//-----------------------------------------------------------------------------
inline void flipCoord3D(Coord3D *coord)
{
	coord->x = -coord->x;
	coord->y = -coord->y;
	coord->z = -coord->z;
}

//-----------------------------------------------------------------------------
static void testRotatedPointsAgainstRect(
	const FCoord2D *pts,				// an array of 4
	const CollideInfo *a,
	FCoord2D *avg,
	Int *avgTot
)
{
	//DEBUG_ASSERTCRASH(a->geom.getGeomType() == GEOMETRY_BOX, ("only boxes are ok here"));
	Fix major = a->geom.getMajorRadiusFix();
	Fix minor = (a->geom.getGeomType() == GEOMETRY_SPHERE) ? a->geom.getMajorRadiusFix() : a->geom.getMinorRadiusFix();

	Fix c = fixCos(-a->angle);
	Fix s = fixSin(-a->angle);

	for (Int i = 0; i < 4; ++i, ++pts)
	{
		// convert to a delta relative to rect ctr
		Fix ptx = pts->x - a->position.x;
		Fix pty = pts->y - a->position.y;

		// inverse-rotate it to the right coord system
		Fix ptx_new = fixAbs(ptx*c - pty*s);
		Fix pty_new = fixAbs(ptx*s + pty*c);

		if (ptx_new <= major && pty_new <= minor)
		{
			avg->x += pts->x;
			avg->y += pts->y;
			*avgTot += 1;
		}
	}
}

//-----------------------------------------------------------------------------
static void rectToFourPoints(
	const CollideInfo *a,		// z is ignored
	FCoord2D pts[]
)
{
	Fix c = fixCos(a->angle);
	Fix s = fixSin(a->angle);

	Fix exc = a->geom.getMajorRadiusFix()*c;
	Fix eyc = a->geom.getMinorRadiusFix()*c;
	Fix exs = a->geom.getMajorRadiusFix()*s;
	Fix eys = a->geom.getMinorRadiusFix()*s;

	// tl
	pts[0].x = a->position.x - exc - eys;
	pts[0].y = a->position.y + eyc - exs;

	// tr
	pts[1].x = a->position.x + exc - eys;
	pts[1].y = a->position.y + eyc + exs;

	// bl
	pts[2].x = a->position.x - exc + eys;
	pts[2].y = a->position.y - eyc - exs;

	// br
	pts[3].x = a->position.x + exc + eys;
	pts[3].y = a->position.y - eyc + exs;
}

//-----------------------------------------------------------------------------
/**
	2d utility routine for rect/cyl & rect/sphere collisions testing.
*/
static Bool xy_collideTest_Circle_Rect(const CollideInfo *a, const CollideInfo *b, FCollideLocAndNormal *cinfo)
{
	Bool result = xy_collideTest_Rect_Circle(b, a, cinfo);
	if (cinfo)
		flipCoord3D(&cinfo->normal);
	return result;
}

//-----------------------------------------------------------------------------
/**
	2d utility routine for rect/cyl & rect/sphere collisions testing.
*/
static Bool xy_collideTest_Rect_Circle(const CollideInfo *a, const CollideInfo *b, FCollideLocAndNormal *cinfo)
{
	/// @todo srj -- this handles rotated rects, but is still not as accurate as it could be:
	/// the circle is tested as the square around it. (srj)
	CollideInfo btmp = *b;
	btmp.geom.setMinorRadiusFix(btmp.geom.getMajorRadiusFix());
	return xy_collideTest_Rect_Rect(a, &btmp, cinfo);
}

//-----------------------------------------------------------------------------
/**
	2d utility routine for cyl & sphere collisions testing.
*/
static Bool xy_collideTest_Circle_Circle(const CollideInfo *a, const CollideInfo *b, FCollideLocAndNormal *cinfo)
{
	FCoord3D diff;
	vecDiff_2D(&b->position, &a->position, &diff);
	Fix distSqr = calcSqrDist_2D(&diff);
	Fix touchingDist = a->geom.getMajorRadiusFix() + b->geom.getMajorRadiusFix();
	if (distSqr <= touchingDist * touchingDist)
	{
		// calc the approximate location and normal.
		if (cinfo)
		{
			cinfo->normal = diff;
			normalizeFix(&cinfo->normal);
			cinfo->loc = a->position;
			projectCoord3D(&cinfo->loc, &cinfo->normal, a->geom.getMajorRadiusFix());
		}

		return true;
	}
	return false;
}

//-----------------------------------------------------------------------------
/**
	2d utility routine for rect collisions testing.
*/
static Bool xy_collideTest_Rect_Rect(const CollideInfo *a, const CollideInfo *b, FCollideLocAndNormal *cinfo)
{
	FCoord2D pts[4];
	FCoord2D avg; avg.zero();
	Int avgTot = 0;

	rectToFourPoints(a, pts);
	testRotatedPointsAgainstRect(pts, b, &avg, &avgTot);

	rectToFourPoints(b, pts);
	testRotatedPointsAgainstRect(pts, a, &avg, &avgTot);
	
	if (avgTot > 0)
	{
		if (cinfo)
		{
			// this is a little hokey, but generally adequate...
			cinfo->loc.x = avg.x / Fix(avgTot);
			cinfo->loc.y = avg.y / Fix(avgTot);
			cinfo->loc.z = (a->position.z + b->position.z) * 0.5_fx;

			// yow, what exactly does it mean to find the normal to two *intersecting*
			// rects? with "true" physics we could project ahead and determine 
			// what the collision point would be, which would of course always
			// give a perfect, symmetrical normal. however, we almost always determine
			// collisions "after the fact" and thus the rects already intersect, meaning
			// that it's tricky to find the normal. we could do it the "right" way, which 
			// would be to back up the objects to find the perfect point (see above), but
			// we don't have velocity information at this point, so we can't really
			// do that. so, naturally, we provide a (poor) guess by just doing the center-to-center
			// normal. this is rarely the correct surface normal for a box-box collision,
			// but until I either (a) rewrite everything to have predictive collisions, 
			// or (b) come up with a better definition of a useful normal in this case,
			// I'm not sure we can do a whole lot better... (srj)
			vecDiff_2D(&b->position, &a->position, &cinfo->normal);
			normalizeFix(&cinfo->normal);
		}
		return true;
	}
	return false;
}

//-----------------------------------------------------------------------------
inline Bool z_collideTest_Sphere_Nonsphere(CollideTestProc xyproc, const CollideInfo *a, const CollideInfo *b, FCollideLocAndNormal *cinfo)
{
	// case 1: center of sphere is within the nonsphere z-space.
	if (a->position.z >= b->position.z && a->position.z <= b->position.z + b->geom.getMaxHeightAbovePositionFix())
	{
		if (xyproc(a, b, cinfo))
		{
			// a side-to-side collision; just need to adjust the z-coord of collideLoc
			if (cinfo)
			{
				cinfo->loc.z = a->position.z;
			}
			return true;
		}
		return false;
	}

	// case 2: center of sphere is above or below nonsphere, but within
	// major-radius of top or bottom.

	// 2a: sphere is below the nonsphere
	const Fix aRadius = a->geom.getMajorRadiusFix();
	Fix b_bot = b->position.z;
	if (a->position.z < b_bot && a->position.z + aRadius >= b_bot)
	{
		// find the radius of the slice of the sphere that is at b_bot
		CollideInfo amod = *a;
		amod.position.z = b_bot;
		Fix dz = b_bot - a->position.z;
		amod.geom.setMajorRadiusFix(fixSqrt(aRadius * aRadius - dz * dz));
		if (xyproc(&amod, b, cinfo))
		{
			// if you want to have 'end' collisions, you should add something like:
			//		if (circle a is contained by circle b) normal points straight up;
			// but since that's currently never possible (and even if it was, wouldn't matter)
			// we don't bother with the calculation.
			return true;
		}
		return false;
	}

	// 2b: sphere is above the nonsphere
	Fix b_top = b->position.z + b->geom.getMaxHeightAbovePositionFix();
	if (a->position.z > b_top && a->position.z - aRadius <= b_top)
	{
		CollideInfo amod = *a;
		amod.position.z = b_top;
		Fix dz = a->position.z - b_top;
		amod.geom.setMajorRadiusFix(fixSqrt(aRadius * aRadius - dz * dz));
		if (xyproc(&amod, b, cinfo))
		{
			// if you want to have 'end' collisions, you should add something like:
			//		if (circle a is contained by circle b) normal points straight down;
			// but since that's currently never possible (and even if it was, wouldn't matter)
			// we don't bother with the calculation.
			return true;
		}
		return false;
	}

	return false;
}

//-----------------------------------------------------------------------------
inline Bool z_collideTest_Nonsphere_Nonsphere(CollideTestProc xyproc, const CollideInfo *a, const CollideInfo *b, FCollideLocAndNormal *cinfo)
{
	// note: we already know that there is a z-intersection (our caller filters that out)
	// so we need not recheck that here.

	// Preflight for close objects.
	Fix dx = a->position.x - b->position.x;
	Fix dy = a->position.y - b->position.y;
	Fix dSqr = dx*dx + dy*dy;
	Fix minRadius = fixMin(fixMin(a->geom.getMajorRadiusFix(), a->geom.getMinorRadiusFix()),
		fixMin(b->geom.getMajorRadiusFix(), b->geom.getMinorRadiusFix()));

	Bool closeEnough = minRadius * minRadius > dSqr;

	if (closeEnough || xyproc(a, b, cinfo))
	{
		// since we allow objects to overlap at times, how do we distinguish
		// between a "top-to-bottom" vs "side-to-side" collision?
		// currently, we don't: cylindrical shapes currently never collide
		// top-to-bottom, so we just declare everything to be side-to-side.
		// if this changes, need to smarten this.

		// case 1: side-to-side collision
		// just need to adjust the z-coord of collideLoc
		if (cinfo)
		{
			if (b->position.z > a->position.z)
				cinfo->loc.z = (b->position.z + a->position.z + a->geom.getMaxHeightAbovePositionFix()) * 0.5_fx;
			else
				cinfo->loc.z = (a->position.z + b->position.z + b->geom.getMaxHeightAbovePositionFix()) * 0.5_fx;
		}
		return true;
	}

	return false;
}

//-----------------------------------------------------------------------------
static Bool collideTest_Sphere_Sphere(const CollideInfo *a, const CollideInfo *b, FCollideLocAndNormal *cinfo)
{
	FCoord3D diff;
	vecDiff_3D(&b->position, &a->position, &diff);
	Fix distSqr = calcSqrDist_3D(&diff);
	Fix touchingDist = a->geom.getMajorRadiusFix() + b->geom.getMajorRadiusFix();
	if (distSqr <= touchingDist * touchingDist)
	{
		if (cinfo)
		{
			cinfo->normal = diff;
			normalizeFix(&cinfo->normal);
			cinfo->loc = a->position;
			projectCoord3D(&cinfo->loc, &cinfo->normal, a->geom.getMajorRadiusFix());
		}

		return true;
	}
	return false;
}

//-----------------------------------------------------------------------------
static Bool collideTest_Sphere_Cylinder(const CollideInfo *a, const CollideInfo *b, FCollideLocAndNormal *cinfo)
{
	return z_collideTest_Sphere_Nonsphere(xy_collideTest_Circle_Circle, a, b, cinfo);
}

//-----------------------------------------------------------------------------
static Bool collideTest_Sphere_Box(const CollideInfo *a, const CollideInfo *b, FCollideLocAndNormal *cinfo)
{
	return z_collideTest_Sphere_Nonsphere(xy_collideTest_Circle_Rect, a, b, cinfo);
}

//-----------------------------------------------------------------------------
static Bool collideTest_Cylinder_Sphere(const CollideInfo *a, const CollideInfo *b, FCollideLocAndNormal *cinfo)
{
	Bool result = z_collideTest_Sphere_Nonsphere(xy_collideTest_Circle_Circle, b, a, cinfo);
	if (cinfo)
		flipCoord3D(&cinfo->normal);
	return result;
}

//-----------------------------------------------------------------------------
static Bool collideTest_Cylinder_Cylinder(const CollideInfo *a, const CollideInfo *b, FCollideLocAndNormal *cinfo)
{
	return z_collideTest_Nonsphere_Nonsphere(xy_collideTest_Circle_Circle, a, b, cinfo);
}

//-----------------------------------------------------------------------------
static Bool collideTest_Cylinder_Box(const CollideInfo *a, const CollideInfo *b, FCollideLocAndNormal *cinfo)
{
	return z_collideTest_Nonsphere_Nonsphere(xy_collideTest_Circle_Rect, a, b, cinfo);
}

//-----------------------------------------------------------------------------
static Bool collideTest_Box_Sphere(const CollideInfo *a, const CollideInfo *b, FCollideLocAndNormal *cinfo)
{
	Bool result = z_collideTest_Sphere_Nonsphere(xy_collideTest_Circle_Rect, b, a, cinfo);
	if (cinfo)
		flipCoord3D(&cinfo->normal);
	return result;
}

//-----------------------------------------------------------------------------
static Bool collideTest_Box_Cylinder(const CollideInfo *a, const CollideInfo *b, FCollideLocAndNormal *cinfo)
{
	return z_collideTest_Nonsphere_Nonsphere(xy_collideTest_Rect_Circle, a, b, cinfo);
}

//-----------------------------------------------------------------------------
static Bool collideTest_Box_Box(const CollideInfo *a, const CollideInfo *b, FCollideLocAndNormal *cinfo)
{
	return z_collideTest_Nonsphere_Nonsphere(xy_collideTest_Rect_Rect, a, b, cinfo);
}

//-----------------------------------------------------------------------------
/* The four distance measures, all in fixed point.  A boundary measure takes the two bounding radii
	 off the centre distance and shrinks the vector to match; a distance can't go below zero. */
static void shrinkByRadius(FCoord3D& diff, Fix actualDistSqr, Fix totalRad, Fix& abDistSqr)
{
	abDistSqr = actualDistSqr;
	if (totalRad <= Fix(0))
		return;

	Fix actualDist = fixSqrt(actualDistSqr);
	Fix shrunkenDist = actualDist - totalRad;
	if (shrunkenDist <= Fix(0))
	{
		abDistSqr = Fix(0);
		diff.zero();
		return;
	}
	Fix shrinkFactor = shrunkenDist / actualDist;
	abDistSqr = shrunkenDist * shrunkenDist;
	diff.scale(shrinkFactor);
}

//-----------------------------------------------------------------------------
static Bool distCalcProc_CenterAndCenter_2D(
	const FCoord3D *posA,
	const Object *objA,
	const FCoord3D *posB,
	const Object *objB,
	Fix& abDistSqr,
	FCoord3D& abVec,
	Fix maxDistSqr
)
{
	abVec.set(posB->x - posA->x, posB->y - posA->y, Fix(0));
	abDistSqr = abVec.x * abVec.x + abVec.y * abVec.y;
	return abDistSqr < maxDistSqr;
}

//-----------------------------------------------------------------------------
static Bool distCalcProc_BoundaryAndBoundary_2D(
	const FCoord3D *posA,
	const Object *objA,
	const FCoord3D *posB,
	const Object *objB,
	Fix& abDistSqr,
	FCoord3D& abVec,
	Fix maxDistSqr
)
{
	abVec.set(posB->x - posA->x, posB->y - posA->y, Fix(0));
	Fix totalRad = (objA ? objA->getGeometryInfo().getBoundingCircleRadiusFix() : Fix(0)) +
							(objB ? objB->getGeometryInfo().getBoundingCircleRadiusFix() : Fix(0));
	shrinkByRadius(abVec, abVec.x * abVec.x + abVec.y * abVec.y, totalRad, abDistSqr);
	return abDistSqr < maxDistSqr;
}

//-----------------------------------------------------------------------------
static Bool distCalcProc_CenterAndCenter_3D(
	const FCoord3D *posA,
	const Object *objA,
	const FCoord3D *posB,
	const Object *objB,
	Fix& abDistSqr,
	FCoord3D& abVec,
	Fix maxDistSqr
)
{
	abVec.set(posB->x - posA->x, posB->y - posA->y, posB->z - posA->z);
	abDistSqr = abVec.lengthSqr();
	return abDistSqr < maxDistSqr;
}

//-----------------------------------------------------------------------------
static Bool distCalcProc_BoundaryAndBoundary_3D(
	const FCoord3D *posA,
	const Object *objA,
	const FCoord3D *posB,
	const Object *objB,
	Fix& abDistSqr,
	FCoord3D& abVec,
	Fix maxDistSqr
)
{
	const GeometryInfo* geomA = objA ? &objA->getGeometryInfo() : NULL;
	const GeometryInfo* geomB = objB ? &objB->getGeometryInfo() : NULL;

	// note that object positions are defined as the bottom center of the geometry,
	// thus we must add the radius to the z coord to get the proper center of the bounding sphere.
	abVec.set(posB->x - posA->x, posB->y - posA->y,
		(posB->z + (geomB ? geomB->getZDeltaToCenterPositionFix() : Fix(0)))
			- (posA->z + (geomA ? geomA->getZDeltaToCenterPositionFix() : Fix(0))));
	Fix totalRad = (geomA ? geomA->getBoundingSphereRadiusFix() : Fix(0)) + (geomB ? geomB->getBoundingSphereRadiusFix() : Fix(0));
	shrinkByRadius(abVec, abVec.lengthSqr(), totalRad, abDistSqr);
	return abDistSqr < maxDistSqr;
}

//-----------------------------------------------------------------------------
//         Private Types                                                     
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
//         Private Data                                                     
//-----------------------------------------------------------------------------

static DistCalcProc theDistCalcProcs[] = 
{ 
	distCalcProc_CenterAndCenter_2D, 
	distCalcProc_CenterAndCenter_3D,
	distCalcProc_BoundaryAndBoundary_2D, 
	distCalcProc_BoundaryAndBoundary_3D, 
};

// NOTE: This *DEPENDS* on the order of the geometry enum defines
static CollideTestProc theCollideTestProcs[] = 
{
	collideTest_Sphere_Sphere,
	collideTest_Sphere_Cylinder,
	collideTest_Sphere_Box,
	collideTest_Cylinder_Sphere,
	collideTest_Cylinder_Cylinder,
	collideTest_Cylinder_Box,
	collideTest_Box_Sphere,
	collideTest_Box_Cylinder,
	collideTest_Box_Box
};

//-----------------------------------------------------------------------------
//         Public Data                                                      
//-----------------------------------------------------------------------------
PartitionManager *ThePartitionManager = NULL;  ///< the object manager singleton

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------

class PartitionContactListNode : public MemoryPoolObject
{
	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE(PartitionContactListNode, "PartitionContactListNode" )

public:
	PartitionContactListNode*			m_nextHash;	///< next node with same hash value 
	PartitionContactListNode*			m_next;			///< next node
	PartitionData*								m_obj;			///< one object that is possibly colliding
	PartitionData*								m_other;		///< the other object (or null for collisions with the terrain)
	Int														m_hashValue;///< index into hash table 
};

inline PartitionContactListNode::~PartitionContactListNode() { }

//-----------------------------------------------------------------------------

class PartitionContactList
{
private:

	/*
		socketcount should be prime (and "not too close to a power of 2) for best results.
		
		if this one isn't large enough, try this website:
		http://www.utm.edu/research/primes/lists/small/1000.txt

		So how is this chosen? Eh, pretty much based on experimentation.
	*/
	enum { PartitionContactList_SOCKET_COUNT = 5381 };


	PartitionContactListNode* m_contactHash[PartitionContactList_SOCKET_COUNT];
	PartitionContactListNode* m_contactList;

public:

	PartitionContactList()
	{
		memset(m_contactHash, 0, sizeof(m_contactHash));
		m_contactList = NULL;
	}

	~PartitionContactList()
	{
		resetContactList();
	}

	/**
		add a pair of objects to the contact list. If the pair is already
		present (in either this order or reverse order), nothing is done.
		Note that it is OK for other==null (this indicates a collisions with
		the ground) but it is not OK for obj==null.
	*/
	void addToContactList(PartitionData *obj, PartitionData *other);

	/**
		process all pairs in the contact list: first, determine if they
		truly collide, based on their geometry. if so, call the collide
		actions for each object in the pair.
	*/
	void processContactList();

	/**
		discard the contents of the contact list.
	*/
	void resetContactList();

	/**
		remove any contacts that refer to the given data.
	*/
	void removeSpecificPartitionData(PartitionData* data);

};

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------

#ifdef FASTER_GCO
// nothing
#else
//-----------------------------------------------------------------------------
CellOutwardIterator::CellOutwardIterator(PartitionManager *mgr, Int x, Int y)
{
	m_mgr = mgr;
	m_cellCenterX = x;
	m_cellCenterY = y;
	m_cellRadius = 0;
	m_maxRadius = maxInt(maxInt(x, mgr->getCellCountX() - x), maxInt(y, mgr->getCellCountY() - y));
	m_delta[0] = m_delta[1] = 0;
	m_inc = -1;
	m_cnt = 0;
	m_axis = 1;
	m_iter = 1;
	m_delta[m_axis] -= m_inc;
}

//-----------------------------------------------------------------------------
CellOutwardIterator::~CellOutwardIterator()
{
}

//-----------------------------------------------------------------------------
PartitionCell *CellOutwardIterator::nextCell(Bool skipEmpties)
{

try_again:

	if (m_cellRadius <= m_maxRadius)
	{

		if (m_iter > 0)
		{
			m_delta[m_axis] += m_inc;
			--m_iter;
			PartitionCell *cell = m_mgr->getCellAt(m_cellCenterX + m_delta[0], m_cellCenterY + m_delta[1]);
			if (!cell)
				goto try_again;
			if (skipEmpties && !cell->getFirstCoiInCell())
				goto try_again;
			return cell;
		}

		m_axis += 1;
		if (m_axis >= 2)
		{
			m_axis = 0;
			m_inc = -m_inc;
			m_cnt += 1;
			m_cellRadius = maxInt(absInt(m_delta[0]), absInt(m_delta[1]));
		}

		m_iter = m_cnt;
		goto try_again;
	}

	return NULL;
}
#endif

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
CellAndObjectIntersection::CellAndObjectIntersection()
{
	m_cell = NULL;
	m_module = NULL;
	m_prevCoi = NULL;
	m_nextCoi = NULL;
}

//-----------------------------------------------------------------------------
CellAndObjectIntersection::~CellAndObjectIntersection()
{
	DEBUG_ASSERTCRASH(m_prevCoi == NULL && m_nextCoi == NULL, ("destroying a linked COI"));
	DEBUG_ASSERTCRASH(!getModule(), ("destroying an in-use COI"));
}

//-----------------------------------------------------------------------------
void CellAndObjectIntersection::friend_addToCellList(CellAndObjectIntersection **pListHead)
{
	DEBUG_ASSERTCRASH(m_prevCoi == NULL && m_nextCoi == NULL && *pListHead != this, ("trying to add a cell to list, but it appears to already be in a list"));
	
	this->m_nextCoi = *pListHead;
	if (*pListHead)
		(*pListHead)->m_prevCoi = this;
	*pListHead = this;
}

//-----------------------------------------------------------------------------
void CellAndObjectIntersection::friend_removeFromCellList(CellAndObjectIntersection **pListHead)
{
#define DEBUG_ASSERTINLIST(c) \
	DEBUG_ASSERTCRASH((c)->m_prevCoi != NULL || (c)->m_nextCoi != NULL || *pListHead == (c), ("cell is not in list"));

	DEBUG_ASSERTINLIST(this);

	if (this->m_prevCoi)
	{
		DEBUG_ASSERTINLIST(this->m_prevCoi);
		DEBUG_ASSERTCRASH(*pListHead != this, ("bad linkage"));
		this->m_prevCoi->m_nextCoi = this->m_nextCoi;
	}
	else
	{
		DEBUG_ASSERTCRASH(*pListHead == this, ("bad linkage"));
		*pListHead = this->m_nextCoi;
	}

	if (this->m_nextCoi)
	{
		DEBUG_ASSERTINLIST(this->m_nextCoi);
		this->m_nextCoi->m_prevCoi = this->m_prevCoi;
	}

	this->m_prevCoi = NULL;
	this->m_nextCoi = NULL;

#undef DEBUG_ASSERTINLIST
}

//-----------------------------------------------------------------------------
void CellAndObjectIntersection::addCoverage(PartitionCell *cell, PartitionData *module)
{
	DEBUG_ASSERTCRASH(m_cell == NULL || m_cell == cell, ("mismatch"));
	DEBUG_ASSERTCRASH(m_module == NULL || m_module == module, ("mismatch"));

	if (m_module != NULL && m_module != module)
	{
		DEBUG_CRASH(("COI already in use by another module!"));
		return;
	}

	if (m_cell == NULL)
		cell->friend_addToCellList(this);

	m_cell = cell;
	m_module = module;
}

//-----------------------------------------------------------------------------
void CellAndObjectIntersection::removeAllCoverage()
{
	if (m_module == NULL)
	{
		DEBUG_CRASH(("COI not in use"));
		return;
	}

	m_cell->friend_removeFromCellList(this);
	m_cell = NULL;
	m_module = NULL;
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
PartitionCell::PartitionCell()
{
	//Added By Sadullah Nader
	//Initializations inserted
	m_cellX = m_cellY = 0;
	//
	m_firstCoiInCell = NULL;
	m_coiCount = 0;
#ifdef PM_CACHE_TERRAIN_HEIGHT
	m_loTerrainZ = HUGE_DIST_FIX;		// huge positive
	m_hiTerrainZ = -HUGE_DIST_FIX;	// huge negative
#endif
	/*
		You may be asking yourself: why do we model the shroud for all players,
		rather than just the local player? And the answer is: because this allows
		us to checksum these values for net games, to help prevent "shroud cheaters"
		(who use a trainer to disable the shroud on their system).
	*/
	for (int i = 0; i < MAX_PLAYER_COUNT; ++i)
	{
		// Default is "passive shroud".  1,0.
		m_shroudLevel[i].m_currentShroud = 1;
		m_shroudLevel[i].m_activeShroudLevel = 0;

		// default cash value is 0
		m_cashValue[i] = 0;

		// default threat value is 0
		m_threatValue[i] = 0;
	}
}

//-----------------------------------------------------------------------------
PartitionCell::~PartitionCell()
{
	DEBUG_ASSERTCRASH(m_firstCoiInCell == NULL && m_coiCount == 0, ("destroying a nonempty PartitionCell"));
	// but don't destroy the Cois; they don't belong to us
}

//-----------------------------------------------------------------------------
void PartitionCell::invalidateShroudedStatusForAllCois(Int playerIndex)
{
	for (CellAndObjectIntersection* coi = m_firstCoiInCell; coi; coi = coi->getNextCoi())
	{
		coi->getModule()->invalidateShroudedStatusForPlayer(playerIndex);
	}
}

//-----------------------------------------------------------------------------
/** Called only from the logic's own look and shroud edges, never from the local player's display
	* refresh, so every machine keeps the same memories on the same frame.  Only a cell leaving sight
	* can take the last of a structure out of view, so that is the one edge that remembers; shroud
	* laid over a cell nobody was looking at never makes a player remember a building they did not
	* see.  Shroud laid over all of it wipes the memory, as it wipes the ghost. */
//-----------------------------------------------------------------------------
void PartitionCell::updateSeenStructures(Int playerIndex, CellShroudStatus oldShroud, CellShroudStatus newShroud)
{
	for (CellAndObjectIntersection* coi = m_firstCoiInCell; coi; coi = coi->getNextCoi())
	{
		PartitionData *module = coi->getModule();
		Object *object = module->getObject();

		// a module holding only a ghost has no object left to remember
		if (object == NULL || !object->isKindOf(KINDOF_STRUCTURE) || object->isKindOf(KINDOF_ALWAYS_VISIBLE))
			continue;

		if (newShroud == CELLSHROUD_CLEAR || module->isFullyShroudedFor(playerIndex))
			object->forgetAsSeenBy(playerIndex);
		else if (oldShroud == CELLSHROUD_CLEAR && !module->isInSightOf(playerIndex))
			object->rememberAsSeenBy(playerIndex);
	}
}

//-----------------------------------------------------------------------------
void PartitionCell::addLooker(Int playerIndex)
{
	CellShroudStatus oldShroud = getShroudStatusForPlayer( playerIndex );
	// The decreasing Algorithm: A 1 will go straight to -1, otherwise it just gets decremented
	m_shroudLevel[playerIndex].m_currentShroud = min( m_shroudLevel[playerIndex].m_currentShroud - 1, -1 );

	CellShroudStatus newShroud = getShroudStatusForPlayer( playerIndex );

//	DEBUG_LOG(( "ADD    %d, %d.  CS = %d, AS = %d for player %d.\n",
//							m_cellX,
//							m_cellY,
//							m_shroudLevel[playerIndex].m_currentShroud,
//							m_shroudLevel[playerIndex].m_activeShroudLevel,
//							playerIndex
//							));

	if( oldShroud != newShroud )
	{
		// On an edge trigger, tell all objects to think about their shroudedness
		invalidateShroudedStatusForAllCois( playerIndex );
		updateSeenStructures( playerIndex, oldShroud, newShroud );

		if( playerIndex == TheObserverCamera.getShroudPlayerIndex() )
		{
			// and if this is the local player, do the Client update.
			TheDisplay->setShroudLevel(m_cellX, m_cellY, newShroud);
			TheRadar->setShroudLevel(m_cellX, m_cellY, newShroud);
		}
	}
}

//-----------------------------------------------------------------------------
void PartitionCell::removeLooker(Int playerIndex)
{
	CellShroudStatus oldShroud = getShroudStatusForPlayer( playerIndex );
	// the increasing Algorithm: a -1 goes up to min(1,activeLevel), otherwise it just gets incremented
	if( m_shroudLevel[playerIndex].m_currentShroud == -1 )
		m_shroudLevel[playerIndex].m_currentShroud = min( m_shroudLevel[playerIndex].m_activeShroudLevel, (Short)1 );
	else
	{
		DEBUG_ASSERTCRASH( m_shroudLevel[playerIndex].m_currentShroud < 0, ("Someone is RemoveLooker-ing on a cell that is not looked at.  This will make a permanent shroud blob.") );
		m_shroudLevel[playerIndex].m_currentShroud++;
	}
	CellShroudStatus newShroud = getShroudStatusForPlayer( playerIndex );

//	DEBUG_LOG(( "REMOVE %d, %d.  CS = %d, AS = %d for player %d.\n",
//							m_cellX,
//							m_cellY,
//							m_shroudLevel[playerIndex].m_currentShroud,
//							m_shroudLevel[playerIndex].m_activeShroudLevel,
//							playerIndex
//							));

	if( oldShroud != newShroud )
	{
		// On an edge trigger, tell all objects to think about their shroudedness
		invalidateShroudedStatusForAllCois( playerIndex );
		updateSeenStructures( playerIndex, oldShroud, newShroud );

		if( playerIndex == TheObserverCamera.getShroudPlayerIndex() )
		{
			// and if this is the local player, do the Client update.
			TheDisplay->setShroudLevel(m_cellX, m_cellY, newShroud);
			TheRadar->setShroudLevel(m_cellX, m_cellY, newShroud);
		}
	}
}

//-----------------------------------------------------------------------------
void PartitionCell::addShrouder( Int playerIndex )
{
	CellShroudStatus oldShroud = getShroudStatusForPlayer( playerIndex );
	// Increasing active shroud: activeLevel gets incremented, and CS is set to 1 if at zero
	// do the algorithm
	m_shroudLevel[playerIndex].m_activeShroudLevel++;
	if( m_shroudLevel[playerIndex].m_currentShroud == 0 )
	{
		m_shroudLevel[playerIndex].m_currentShroud = 1;
	}
	CellShroudStatus newShroud = getShroudStatusForPlayer( playerIndex );

	if( oldShroud != newShroud )
	{
		// On an edge trigger, tell all objects to think about their shroudedness
		invalidateShroudedStatusForAllCois( playerIndex );
		updateSeenStructures( playerIndex, oldShroud, newShroud );

		// and update the client if we are on the local player
		if( playerIndex == TheObserverCamera.getShroudPlayerIndex() )
		{
			TheDisplay->setShroudLevel(m_cellX, m_cellY, newShroud);
			TheRadar->setShroudLevel(m_cellX, m_cellY, newShroud);
		}
	}
}

//-----------------------------------------------------------------------------
void PartitionCell::removeShrouder( Int playerIndex )
{
	// Decreasing active shroud: just decrement activeLevel.  This will never result in a client change.
	// Either it was passive shroud and is now active, or it was being looked at and still is.
	m_shroudLevel[playerIndex].m_activeShroudLevel--;
	DEBUG_ASSERTCRASH( m_shroudLevel[playerIndex].m_activeShroudLevel >= 0, ("Shroud generation has gone negative.  This can't happen.") );
}

//-----------------------------------------------------------------------------
//Bool PartitionCell::isShroudedForPlayer( Int playerIndex ) const
//{
	// There isn't an absolute answer.  This cell is only shrouded in regards to a person
//	return (m_shroudLevel[playerIndex].m_currentShroud == 1);
//}

//-----------------------------------------------------------------------------
CellShroudStatus PartitionCell::getShroudStatusForPlayer( Int playerIndex ) const
{
	// There are now three answers, but the question still requires "to whom"

	if( m_shroudLevel[playerIndex].m_currentShroud == 1 )
		return CELLSHROUD_SHROUDED;
	else if( m_shroudLevel[playerIndex].m_currentShroud == 0 )
		return CELLSHROUD_FOGGED;// ie Nobody actively looking
	else
		return CELLSHROUD_CLEAR;
}

//-----------------------------------------------------------------------------
UnsignedInt PartitionCell::getThreatValue( Int playerIndex )
{
	if (playerIndex >= 0 && playerIndex < MAX_PLAYER_COUNT) {
		return m_threatValue[playerIndex];
	}
	return 0;
}

//-----------------------------------------------------------------------------
void PartitionCell::addThreatValue( Int playerIndex, UnsignedInt threatValue )
{
	if (playerIndex >= 0 && playerIndex < MAX_PLAYER_COUNT) {
#ifdef _DEBUG
		UnsignedInt oldThreatVal = m_threatValue[playerIndex];
		DEBUG_ASSERTCRASH(oldThreatVal <= oldThreatVal + threatValue, ("adding new threat value overflowed allotted storage."));
#endif
		m_threatValue[playerIndex] += threatValue;
	}
}

//-----------------------------------------------------------------------------
void PartitionCell::removeThreatValue( Int playerIndex, UnsignedInt threatValue )
{
	if (playerIndex >= 0 && playerIndex < MAX_PLAYER_COUNT) {
#ifdef _DEBUG
		UnsignedInt oldThreatVal = m_threatValue[playerIndex];
		DEBUG_ASSERTCRASH(oldThreatVal >= oldThreatVal - threatValue, ("removing new threat value underflowed allotted storage."));
#endif
		m_threatValue[playerIndex] -= threatValue;
	}
}

//-----------------------------------------------------------------------------
UnsignedInt PartitionCell::getCashValue( Int playerIndex )
{
	if (playerIndex >= 0 && playerIndex < MAX_PLAYER_COUNT) {
		return m_cashValue[playerIndex];
	}
	return 0;
}

//-----------------------------------------------------------------------------
void PartitionCell::addCashValue( Int playerIndex, UnsignedInt cashValue )
{
	if (playerIndex >= 0 && playerIndex < MAX_PLAYER_COUNT) {
#ifdef _DEBUG
		UnsignedInt oldCashVal = m_cashValue[playerIndex];
		DEBUG_ASSERTCRASH(oldCashVal <= oldCashVal + cashValue, ("adding new cash value overflowed allotted storage."));
#endif
		m_cashValue[playerIndex] += cashValue;
	}
}

//-----------------------------------------------------------------------------
void PartitionCell::removeCashValue( Int playerIndex, UnsignedInt cashValue )
{
	if (playerIndex >= 0 && playerIndex < MAX_PLAYER_COUNT) {
#ifdef _DEBUG
		UnsignedInt oldCashVal = m_cashValue[playerIndex];
		DEBUG_ASSERTCRASH(oldCashVal >= oldCashVal - cashValue, ("removing new cash value underflowed allotted storage."));
#endif
		m_cashValue[playerIndex] -= cashValue;
	}
}

//-----------------------------------------------------------------------------
void PartitionCell::friend_addToCellList(CellAndObjectIntersection *coi)
{
	if (coi)
	{
		coi->friend_addToCellList(&m_firstCoiInCell);
		++m_coiCount;
	}
}

//-----------------------------------------------------------------------------
void PartitionCell::friend_removeFromCellList(CellAndObjectIntersection *coi)
{
	if (coi)
	{
		coi->friend_removeFromCellList(&m_firstCoiInCell);
		--m_coiCount;
	}
}

//-----------------------------------------------------------------------------
void PartitionCell::getCellCenterPos(Fix& x, Fix& y)
{
	ThePartitionManager->getCellCenterPos(m_cellX, m_cellY, x, y);
}

//-----------------------------------------------------------------------------
#ifdef _DEBUG
void PartitionCell::validateCoiList()
{
	CellAndObjectIntersection *nextCoi = 0, *prevCoi = 0;
	for (CellAndObjectIntersection *coi = getFirstCoiInCell(); coi; prevCoi = coi, coi = nextCoi)
	{
		nextCoi = coi->getNextCoi();
		DEBUG_ASSERTCRASH(coi->getPrevCoi() == prevCoi, ("coi link mismatch"));
		DEBUG_ASSERTCRASH(prevCoi == NULL || prevCoi->getNextCoi() == coi, ("coi link mismatch"));
		DEBUG_ASSERTCRASH((coi == getFirstCoiInCell()) == (prevCoi == NULL) , ("coi link mismatch"));
		DEBUG_ASSERTCRASH(nextCoi == NULL || nextCoi->getPrevCoi() == coi, ("coi link mismatch"));
	}
}
#endif

// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void PartitionCell::crc( Xfer *xfer )
{

	xfer->xferUser(&m_shroudLevel, sizeof(ShroudLevel) * MAX_PLAYER_COUNT);
	xfer->xferUser(&m_cellX, sizeof(m_cellX));
	xfer->xferUser(&m_cellY, sizeof(m_cellY));

}  // end crc

// ------------------------------------------------------------------------------------------------
/** Xfer Method */
// ------------------------------------------------------------------------------------------------
void PartitionCell::xfer( Xfer *xfer )
{

	// version
	XferVersion currentVersion = 1;
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );

	// xfer shroud data
	xfer->xferUser( &m_shroudLevel, sizeof( ShroudLevel ) * MAX_PLAYER_COUNT );

}  // end xfer

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void PartitionCell::loadPostProcess( void )
{

}  // end loadPostProcess

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
PartitionData::PartitionData()
{
	//DEBUG_LOG(("create pd %08lx\n",this));
	m_next = NULL;
	m_prev = NULL;
	m_nextDirty = NULL;
	m_prevDirty = NULL;
	m_object = NULL;
	m_ghostObject = NULL;
	m_coiArrayCount = 0;
	m_coiArray = NULL;
	m_coiInUseCount = 0;
	m_doneFlag = 0;
	m_dirtyStatus = NOT_DIRTY;
	m_lastCell = NULL;
	for (int i = 0; i < MAX_PLAYER_COUNT; ++i)
	{
		m_everSeenByPlayer[i] = false;
		m_shroudedness[i] = OBJECTSHROUD_INVALID;
		m_shroudednessPrevious[i] = OBJECTSHROUD_INVALID;
	}
}

//-----------------------------------------------------------------------------
PartitionData::~PartitionData()
{
	//DEBUG_LOG(("toss pd for pd %08lx obj %08lx\n",this,m_object));
	removeAllTouchedCells();
	freeCoiArray();
	DEBUG_ASSERTCRASH(ThePartitionManager, ("ThePartitionManager is null"));
	if (ThePartitionManager && ThePartitionManager->isInListDirtyModules(this))
	{
		//DEBUG_LOG(("remove pd %08lx from dirty list (%08lx %08lx)\n",this,m_prevDirty,m_nextDirty));
		ThePartitionManager->removeFromDirtyModules(this);
		//DEBUG_ASSERTCRASH(!ThePartitionManager->isInListDirtyModules(this), ("hmm\n"));
	}
} 

//-----------------------------------------------------------------------------
Int PartitionData::getControllingPlayerIndex() const
{
	const Player* p = getObject()->getControllingPlayer();
	if (p)
	{
		Int playerIndex = p->getPlayerIndex();
		DEBUG_ASSERTCRASH(playerIndex >= 0 && playerIndex < MAX_PLAYER_COUNT, ("bad playerIndex"));
		return playerIndex;
	}

	DEBUG_CRASH(("this should never happen"));
	throw ERROR_BUG;
	return 0;
}

//-----------------------------------------------------------------------------
Bool PartitionData::isInSightOf(Int playerIndex)
{
	CellAndObjectIntersection* coi = m_coiArray;
	for (Int i = m_coiInUseCount; i; --i, ++coi)
	{
		if (coi->getCell()->getShroudStatusForPlayer(playerIndex) == CELLSHROUD_CLEAR)
			return TRUE;
	}
	return FALSE;
}

//-----------------------------------------------------------------------------
Bool PartitionData::isFullyShroudedFor(Int playerIndex)
{
	CellAndObjectIntersection* coi = m_coiArray;
	for (Int i = m_coiInUseCount; i; --i, ++coi)
	{
		if (coi->getCell()->getShroudStatusForPlayer(playerIndex) != CELLSHROUD_SHROUDED)
			return FALSE;
	}
	return TRUE;
}

//-----------------------------------------------------------------------------
// only used to restore state after map border resizing and/or xfer!
void PartitionData::friend_setShroudednessPrevious(Int playerIndex, ObjectShroudStatus status) 
{
	m_shroudednessPrevious[playerIndex] = status;

	// if our status is anything but shrouded, assume we've never been seen by the player.
	m_everSeenByPlayer[playerIndex] = (status == OBJECTSHROUD_SHROUDED) ? false : true;

	// if we're already invalid, mark us as special-invalid.
#ifndef DISABLE_INVALID_PREVENTION
	if (m_shroudedness[playerIndex] == OBJECTSHROUD_INVALID)
		m_shroudedness[playerIndex] = OBJECTSHROUD_INVALID_BUT_PREVIOUS_VALID;
#endif
} 

//-----------------------------------------------------------------------------
ObjectShroudStatus PartitionData::getShroudedStatus(Int playerIndex)
{
	// sanity
	DEBUG_ASSERTCRASH( playerIndex >= 0 && playerIndex < MAX_PLAYER_COUNT, 
										 ("PartitionData::getShroudedStatus - Invalid player index '%d'\n", playerIndex) );

	if (!ThePartitionManager->getUpdatedSinceLastReset()) 
	{
		// The shroud should be invalid until update has been called.
		// I'll set myself as invalid so the next time you ask I'll recompute.  (If I just return Invalid, then the old state will persist unfairly over a reset)
		invalidateShroudedStatusForPlayer(playerIndex);
		return m_shroudedness[playerIndex];
	}

	// answered before the cache, so no snapshot is taken and the site is not marked as seen: the
	// building that goes up there later is news to this player like any other
	if (m_object && Object_isPlanHiddenFrom(m_object->testStatus(OBJECT_STATUS_UNDER_CONSTRUCTION),
																					m_object->getConstructionPercent(),
																					ThePlayerList->getNthPlayer(playerIndex)->getRelationship(m_object->getTeam())))
		return OBJECTSHROUD_SHROUDED;

#ifndef DISABLE_INVALID_PREVENTION
	if (m_shroudedness[playerIndex] == OBJECTSHROUD_INVALID || m_shroudedness[playerIndex] == OBJECTSHROUD_INVALID_BUT_PREVIOUS_VALID)
	{
		Bool updateShroudednessPrevious = (m_shroudedness[playerIndex] != OBJECTSHROUD_INVALID_BUT_PREVIOUS_VALID);
#else
	if (m_shroudedness[playerIndex] == OBJECTSHROUD_INVALID)
	{
#endif
		Int shroudedCells = 0;
		Int foggedCells = 0;
		CellAndObjectIntersection* coi = m_coiArray;
		for (Int i = m_coiInUseCount; i; --i, ++coi)
		{
			if( coi->getCell()->getShroudStatusForPlayer(playerIndex) == CELLSHROUD_SHROUDED )
				++shroudedCells;
			else if( coi->getCell()->getShroudStatusForPlayer(playerIndex) == CELLSHROUD_FOGGED )
				++foggedCells;
		}

		if( m_coiInUseCount == 0 )
		{	m_shroudedness[playerIndex] = OBJECTSHROUD_SHROUDED;				// Stuff off the map have no COIs since they aren't in the PartitionCell range
			m_everSeenByPlayer[playerIndex] = false;	//force object as never seen by the player
		}
		else if( shroudedCells == m_coiInUseCount )
		{	m_shroudedness[playerIndex] = OBJECTSHROUD_SHROUDED;				// every cell I use is shrouded
			m_everSeenByPlayer[playerIndex] = false; //force object as never seen by the player
			if (m_ghostObject && m_shroudednessPrevious[playerIndex] == OBJECTSHROUD_FOGGED)
			{	//we are shrouding an area that used to be fogged so release our memory of what was there.
				m_ghostObject->freeSnapShot(playerIndex);
			}
		}
		else if( shroudedCells + foggedCells == m_coiInUseCount )
		{	
			m_shroudedness[playerIndex] = OBJECTSHROUD_FOGGED;	//object is visible but fogged.
			if (m_object && m_ghostObject)	//object does not exist for modules holding only GhostObjects
			{
				//fogged but may not be visible if faction unit or faction building that has not been seen before
				Player *player=ThePlayerList->getNthPlayer(playerIndex);
				if (player->getRelationship(m_object->getTeam()) == NEUTRAL)
				{	//anything neutral that moves around will not be rendered inside fog.
					if (!m_object->isKindOf(KINDOF_IMMOBILE))
						m_shroudedness[playerIndex] = OBJECTSHROUD_SHROUDED;
				}
				else	//Not neutral
				{	//enemy unit will always be shrouded unless it's a building that's already been seen by the player.  Fogged Mines are also always
					//shroued no matter what.
					if (!(m_object->isKindOf(KINDOF_IMMOBILE) && m_everSeenByPlayer[playerIndex]) || m_object->isKindOf(KINDOF_MINE))
						m_shroudedness[playerIndex] = OBJECTSHROUD_SHROUDED;
				}
				if (m_shroudedness[playerIndex] == OBJECTSHROUD_FOGGED)
				{	//successfully applied fog to object so check if we need to freeze it's state
					if (m_shroudednessPrevious[playerIndex] < OBJECTSHROUD_FOGGED)
					{	//object was not previously fogged but now is fogged.
						//need to record its current state so that it doesn't change
						//while fogged.
						m_ghostObject->snapShot(playerIndex);
					}
				}
			}
		}
		else if( shroudedCells == 0  &&  foggedCells == 0 )
		{	//Record that this object was seen by the player.  This info will be used to show fogged enemy faction buildings.
			m_everSeenByPlayer[playerIndex] = true;
			m_shroudedness[playerIndex] = OBJECTSHROUD_CLEAR;
			if (m_ghostObject && m_shroudednessPrevious[playerIndex] == OBJECTSHROUD_FOGGED)
			{	//object was previously fogged but now is visible so we no longer
				//need a ghost object.
				m_ghostObject->freeSnapShot(playerIndex);
			}
		}	// no cell I use has anything
		else
		{	//Record that this object was seen by the player.  This info will be used to show fogged enemy faction buildings.
			m_everSeenByPlayer[playerIndex] = true;
			m_shroudedness[playerIndex] = OBJECTSHROUD_PARTIAL_CLEAR;		// I am at least partially clear otherwise
			if (m_ghostObject && m_shroudednessPrevious[playerIndex] == OBJECTSHROUD_FOGGED)
			{	//object was previously fogged but now is visible so we no longer
				//need a ghost object.
				m_ghostObject->freeSnapShot(playerIndex);
			}
		}
#ifndef DISABLE_INVALID_PREVENTION
 		if (m_coiInUseCount && updateShroudednessPrevious) 
		{	
#else
 		if (m_coiInUseCount) 
		{	
#endif
			//only remember the previous state of objects actually on the map.
 			m_shroudednessPrevious[playerIndex] = m_shroudedness[playerIndex];
		}
	}

	return m_shroudedness[playerIndex];
}

//-----------------------------------------------------------------------------
void PartitionData::removeAllTouchedCells()
{
	CellAndObjectIntersection *coi = m_coiArray;
	for (Int i = m_coiArrayCount; i > 0; --i, ++coi)
	{
		if (coi->getModule())
		{
			DEBUG_ASSERTCRASH(coi->getModule() == this, ("coi uses wrong module"));
			coi->removeAllCoverage();
			DEBUG_ASSERTCRASH(!coi->getModule(), ("coi should no longer be in use"));
			--m_coiInUseCount;
		}
	}
	DEBUG_ASSERTCRASH(m_coiInUseCount == 0, ("hmm, coi count mismatch"));
}

// -----------------------------------------------------------------------------
void PartitionData::addSubPixToCoverage(PartitionCell *cell)
{
	DEBUG_ASSERTCRASH(m_coiInUseCount < m_coiArrayCount, ("not enough cois allocated for this object"));
	if (cell)
	{			
		// see if we already have a coi for this cell.
		CellAndObjectIntersection *coi = m_coiArray;
		CellAndObjectIntersection *coiToUse = NULL;
		for (Int i = __min(m_coiInUseCount,m_coiArrayCount); i; --i, ++coi)
		{
			if (coi->getCell() == cell)
			{
				coiToUse = coi;
				break;
			}
		}
		DEBUG_ASSERTCRASH(coiToUse != NULL || m_coiInUseCount < m_coiArrayCount, ("not enough cois allocated for this object"));
		if (coiToUse == NULL && m_coiInUseCount < m_coiArrayCount)
		{
			// nope, no coi for this cell, allocate a new one
			coiToUse = &m_coiArray[m_coiInUseCount++];
		}
		if (coiToUse)
		{
			coiToUse->addCoverage(cell, this);
		}
	}
}

// -----------------------------------------------------------------------------
void PartitionData::doRectFill(
	Fix centerX,
	Fix centerY,
	Fix halfsizeX,
	Fix halfsizeY,
	Fix angle
)
{
	Fix c = fixCos(angle);
	Fix s = fixSin(angle);

	Fix actualCellSize = ThePartitionManager->getCellSizeFix();
	Fix stepSize = actualCellSize * 0.5_fx; // in theory, should be getCellSize() exactly, but needs to be smaller to avoid aliasing problems
	Fix ydx = s * stepSize;
	Fix ydy = -c * stepSize;
	Fix xdx = c * stepSize;
	Fix xdy = s * stepSize;

	// GS 12-26-02  Oh my.  The step size has been fudged down to ensure that a cell is not jumped over,
	// but then the original step size was used to determine the number of steps.  This error gets
	// more pronounced on bigger buildings.  ie a 100 X 160 registers cells for a 80 x 100.  If you
	// fudge one, you have to fudge the other.

	/*
		srj fixes subtle bug: need +1 to get all of misaligned geometries accted for.
	*/
	Int numStepsX = PartitionManager_cellsIn(halfsizeX * Fix(2), stepSize, true);
	Int numStepsY = PartitionManager_cellsIn(halfsizeY * Fix(2), stepSize, true);
	// GS 12-26-02  Oh my part 2.  Now that we are no longer greatly underestimating our COI imprint,
	// we don't need this fudge.  The fudge is not in the calcMaxCOIForShape, so it just results in
	// good COIs getting dropped since we didn't allocate enough to handle this fudge.

	Fix tl_x = centerX - halfsizeX*c - halfsizeY*s;
	Fix tl_y = centerY + halfsizeY*c - halfsizeX*s;

	for (Int iy = 0; iy < numStepsY; ++iy, tl_x += ydx, tl_y += ydy)
	{
		Fix x = tl_x;
		Fix y = tl_y;
		for (Int ix = 0; ix < numStepsX; ++ix, x += xdx, y += xdy)
		{
			Int cellx, celly;
			ThePartitionManager->worldToCellFix(x, y, &cellx, &celly);
			PartitionCell *cell = ThePartitionManager->getCellAt(cellx, celly);	// might be null if off the edge
			if (cell)
			{
				addSubPixToCoverage(cell);
			}
		}
	}

}

// -----------------------------------------------------------------------------
void PartitionData::hLineCircle(Int x1, Int x2, Int y)
{
	for (Int x = x1; x <= x2; ++x)
	{
		PartitionCell* cell = ThePartitionManager->getCellAt(x, y);
		if (cell)
		{
      addSubPixToCoverage(cell);
		}
	}
}

// -----------------------------------------------------------------------------
void PartitionData::doCircleFill(
	Fix centerX,
	Fix centerY,
	Fix radius
)
{
	DEBUG_ASSERTCRASH(m_coiInUseCount == 0, ("expected no coi in use here"));

	Int cellCenterX, cellCenterY;
	ThePartitionManager->worldToCellFix(centerX, centerY, &cellCenterX, &cellCenterY);

	Int cellRadius = ThePartitionManager->worldToCellDist(radius);
	if (cellRadius < 1) 
		cellRadius = 1;

	Int y = cellRadius - 1;
	Int dec = 3 - 2*cellRadius;
	for (Int x = 0; x < cellRadius; x++)
	{
		hLineCircle(cellCenterX - x, cellCenterX + x, cellCenterY + y);
		hLineCircle(cellCenterX - x, cellCenterX + x, cellCenterY - y);
		hLineCircle(cellCenterX - y, cellCenterX + y, cellCenterY + x);
		hLineCircle(cellCenterX - y, cellCenterX + y, cellCenterY - x);

		if (dec >= 0)
		{
			dec += (1 - y) << 2;
			--y;
		}
		dec += (x << 2) + 6;
	}
}

// -----------------------------------------------------------------------------
// The scanline circle above is a midpoint-circle outline with its spans filled in, in whole cells:
// it claims cells the object does not reach and misses cells it does, most visibly for radii of
// about 20 to 40 (one to two cells).  This one keeps a cell if the circle overlaps the cell's
// square at all.
void PartitionData::doCircleFillPrecise(Fix centerX, Fix centerY, Fix radius)
{
	DEBUG_ASSERTCRASH(m_coiInUseCount == 0, ("expected no coi in use here"));

	Int minCellX, minCellY, maxCellX, maxCellY;
	ThePartitionManager->worldToCellFix(centerX - radius, centerY - radius, &minCellX, &minCellY);
	ThePartitionManager->worldToCellFix(centerX + radius, centerY + radius, &maxCellX, &maxCellY);

	const Fix halfCell = ThePartitionManager->getCellSizeFix() * 0.5_fx;

	for (Int x = minCellX; x <= maxCellX; ++x)
	{
		for (Int y = minCellY; y <= maxCellY; ++y)
		{
			PartitionCell *cell = ThePartitionManager->getCellAt(x, y);
			if (cell == NULL)
				continue;

			Fix cellCenterX, cellCenterY;
			ThePartitionManager->getCellCenterPos(x, y, cellCenterX, cellCenterY);

			// distance from the circle's center to the closest point of the cell's square
			Fix dx = fixMax(fixAbs(centerX - cellCenterX) - halfCell, Fix(0));
			Fix dy = fixMax(fixAbs(centerY - cellCenterY) - halfCell, Fix(0));

			if (dx*dx + dy*dy < radius*radius)
				addSubPixToCoverage(cell);
		}
	}
}

// -----------------------------------------------------------------------------
void PartitionData::doSmallFill(
	Fix centerX,
	Fix centerY,
	Fix radius
)
{
	DEBUG_ASSERTCRASH(m_coiInUseCount == 0, ("expected no coi in use here"));

	Fix halfCellSize = ThePartitionManager->getCellSizeFix() * 0.5_fx;
	if (radius > halfCellSize)
	{
		DEBUG_CRASH(("object is too large to use a 'small' geometry, truncating size to cellsize\n"));
		radius = halfCellSize;
	}

	Int cx1, cy1, cx2, cy2;
	ThePartitionManager->worldToCellFix(centerX - radius, centerY - radius, &cx1, &cy1);
	ThePartitionManager->worldToCellFix(centerX + radius, centerY + radius, &cx2, &cy2);

	DEBUG_ASSERTCRASH(absInt(cx2-cx1)<=1,("bad cx"));
	DEBUG_ASSERTCRASH(absInt(cy2-cy1)<=1,("bad cy"));

	for (Int x = cx1; x <= cx2; x++)
	{
		for (Int y = cy1; y <= cy2; y++)
		{
			PartitionCell *cell = ThePartitionManager->getCellAt(x, y);
			if (cell)
			{
				m_coiArray[m_coiInUseCount++].addCoverage(cell, this);
			}
		}
	}

	#ifdef INTENSE_DEBUG
	for (int i = 0; i < m_coiInUseCount; i++)
	{
		for (int j = 0; j < i; j++)
		{
			DEBUG_ASSERTCRASH(m_coiArray[i].getCell() != m_coiArray[j].getCell(), ("dup cells"));
		}
	}
	#endif
}

//-----------------------------------------------------------------------------
void PartitionData::addPossibleCollisions(PartitionContactList *ctList)
{
// actually, we do occasionally want to detect collisions of dead AIs.
// e.g., dead technicals flying thru the air should check for collisions with
// immobile structures, because otherwise they pass thru them, which looks dopey.
// if preventing dead-ai-collisions is necessary to fix something else, please
// ensure that the above case is cool before signing off. (srj)
#ifdef NOPE_READ_THE_COMMENT
	// dead objects never collide with things...
 	AIUpdateInterface *ai = getObject()->getAIUpdateInterface();
 	if (ai && ai->isAiInDeadState())
	{
		return;
	}
#endif

	//DEBUG_LOG(("adding possible collision for %s\n",getObject()->getTemplate()->getName().str()));

	CellAndObjectIntersection *myCoi = m_coiArray;
	for (Int i = m_coiInUseCount; i > 0; --i, ++myCoi)
	{
		PartitionCell *cell = myCoi->getCell();
		if (cell->getCoiCount() < 2)
			continue;

		for (CellAndObjectIntersection *coi = cell->getFirstCoiInCell(); coi; coi = coi->getNextCoi())
		{
			PartitionData *that = coi->getModule();
			if (this != that)
			{
				ctList->addToContactList(this, that);
			}
		}
	}
}

//-----------------------------------------------------------------------------
Bool PartitionData::collidesWith(const PartitionData *that, CollideLocAndNormal *cinfo) const
{
	const Object *thisObj = this->getObject();
	const Object *thatObj = that->getObject();

	if( thisObj->isKindOf( KINDOF_NO_COLLIDE )  ||  thatObj->isKindOf( KINDOF_NO_COLLIDE ) )
		return FALSE; // A collision extent of zero size is still a point and can collide, but we don't always want to.

	CollideInfo thisInfo(thisObj->getPositionFix(), thisObj->getGeometryInfo(), thisObj->getOrientationFix());
	CollideInfo thatInfo(thatObj->getPositionFix(), thatObj->getGeometryInfo(), thatObj->getOrientationFix());

	// invariant for all geometries: first do z collision check.
	Fix thisTop = thisInfo.position.z + thisInfo.geom.getMaxHeightAbovePositionFix();
	Fix thisBot = thisInfo.position.z - thisInfo.geom.getMaxHeightBelowPositionFix();
	Fix thatTop = thatInfo.position.z + thatInfo.geom.getMaxHeightAbovePositionFix();
	Fix thatBot = thatInfo.position.z - thatInfo.geom.getMaxHeightBelowPositionFix();
	if (thisTop >= thatBot && thisBot <= thatTop)
	{
		GeometryType thisGeom = thisObj->getGeometryInfo().getGeomType();
		GeometryType thatGeom = thatObj->getGeometryInfo().getGeomType();

		//
		// NOTE: This assumes geometry enumerations that start at GEOMETRY_FIRST AND depends on the
		// order in which they appear in the enum list
		//
		CollideTestProc collideProc = theCollideTestProcs[ (thisGeom - GEOMETRY_FIRST) * GEOMETRY_NUM_TYPES + (thatGeom - GEOMETRY_FIRST) ];
		FCollideLocAndNormal floc;
		Bool hit = (*collideProc)(&thisInfo, &thatInfo, cinfo ? &floc : NULL);
		if (hit && cinfo)
		{
			// P4: the collide modules take the contact in float
			cinfo->loc = floc.loc.toCoord3D();
			cinfo->normal = floc.normal.toCoord3D();
		}
		return hit;
	}
	else
	{
		// no z-intersection -> no collision.
		return false;
	}
}

//-----------------------------------------------------------------------------
/* See if thisObj collides with geom at pos & angle. */
Bool PartitionManager::geomCollidesWithGeom(const Coord3D* pos1,
		const GeometryInfo& geom1,
		Real angle1,
		const Coord3D* pos2,
		const GeometryInfo& geom2,
		Real angle2) const
{
	FCoord3D fpos1 = fcoordFromCoord3D(pos1);
	FCoord3D fpos2 = fcoordFromCoord3D(pos2);
	return geomCollidesWithGeomFix(&fpos1, geom1, fixFromReal(angle1), &fpos2, geom2, fixFromReal(angle2));
}

//-----------------------------------------------------------------------------
Bool PartitionManager::geomCollidesWithGeomFix(const FCoord3D* pos1,
		const GeometryInfo& geom1,
		Fix angle1,
		const FCoord3D* pos2,
		const GeometryInfo& geom2,
		Fix angle2) const
{
	CollideInfo thisInfo(pos1, geom1, angle1);
	CollideInfo thatInfo(pos2, geom2, angle2);

	// invariant for all geometries: first do z collision check.
	if (thisInfo.position.z + thisInfo.geom.getMaxHeightAbovePositionFix() >= thatInfo.position.z &&
			thisInfo.position.z <= thatInfo.position.z + thatInfo.geom.getMaxHeightAbovePositionFix())
	{
		GeometryType thisGeom = geom1.getGeomType();
		GeometryType thatGeom = geom2.getGeomType();

		//
		// NOTE: This assumes geometry enumerations that start at GEOMETRY_FIRST AND depends on the
		// order in which they appear in the enum list
		//
		CollideTestProc collideProc = theCollideTestProcs[ (thisGeom - GEOMETRY_FIRST) * GEOMETRY_NUM_TYPES + (thatGeom - GEOMETRY_FIRST) ];
		FCollideLocAndNormal cloc;
		return (*collideProc)(&thisInfo, &thatInfo, &cloc);
	}
	else
	{
		// no z-intersection -> no collision.
		return false;
	}
}

//-----------------------------------------------------------------------------
void PartitionData::updateCellsTouched()
{
	GeometryType geom;
	Bool isSmall;
	FCoord3D pos;
	Fix angle,majorRadius,minorRadius;


	Object *obj = getObject();
	DEBUG_ASSERTCRASH(obj != NULL || m_ghostObject != NULL, ("must be attached to an Object here 1"));

	if (obj)
	{
		//we have no object using this PartitionData but we still have a GhostObject so copy its data.
		geom = obj->getGeometryInfo().getGeomType();
		isSmall = obj->getGeometryInfo().getIsSmall();
		pos = *obj->getPositionFix();
		angle = obj->getOrientationFix();
		majorRadius = obj->getGeometryInfo().getMajorRadiusFix();
		minorRadius = obj->getGeometryInfo().getMinorRadiusFix();
	}
	else if (m_ghostObject)
	{
		// the ghost object is the client's, and float
		geom = m_ghostObject->getGeometryType();
		isSmall = m_ghostObject->getGeometrySmall();
		pos = fcoordFromCoord3D(m_ghostObject->getParentPosition());
		angle = fixFromReal(m_ghostObject->getParentAngle());
		majorRadius = fixFromReal(m_ghostObject->getGeometryMajorRadius());
		minorRadius = fixFromReal(m_ghostObject->getGeometryMinorRadius());
	}

	removeAllTouchedCells();
	if (isSmall)
	{
		doSmallFill(pos.x, pos.y, majorRadius);
	}
	else
	{
		switch(geom)
		{
			case GEOMETRY_SPHERE:
			case GEOMETRY_CYLINDER:
			{
				doCircleFillPrecise(pos.x, pos.y, majorRadius);
				break;
			}

			case GEOMETRY_BOX:
			{
				doRectFill(pos.x, pos.y, majorRadius, minorRadius, angle);
				break;
			}
		};
	}

	Int currentCellIndexX, currentCellIndexY;
	ThePartitionManager->worldToCellFix( pos.x, pos.y, &currentCellIndexX, &currentCellIndexY );
	const PartitionCell *currentCell = ThePartitionManager->getCellAt( currentCellIndexX, currentCellIndexY );
	if(obj && currentCell != m_lastCell )
	{
		// To not expose PartitionCells, he will think in terms of points.  He will 
		// unlook at a point and look at the new point.  We do the rounding and the 
		// changing into PartitionCells
		obj->onPartitionCellChange(); 
		m_lastCell = currentCell;
	}

	// if we have moved, our shroudedness status might be different for all players,
	// so it must all be invalidated.
	invalidateShroudedStatusForAllPlayers();

#ifdef INTENSE_DEBUG
	for (Int i = 0; i < m_coiInUseCount; i++)
	{
		for (Int j = 0; j < i; j++)
		{
			if (m_coiArray[i].getCell() == m_coiArray[j].getCell())
			{
				DEBUG_CRASH(("dup cells in COI array, this is bad"));
			}
		}
	}
#endif

}

//-----------------------------------------------------------------------------
void PartitionData::invalidateShroudedStatusForPlayer(Int playerIndex) 
{ 
#ifndef DISABLE_INVALID_PREVENTION
	if (m_shroudedness[playerIndex] != OBJECTSHROUD_INVALID && m_shroudedness[playerIndex] != OBJECTSHROUD_INVALID_BUT_PREVIOUS_VALID)
#endif
		m_shroudedness[playerIndex] = OBJECTSHROUD_INVALID;
}

//-----------------------------------------------------------------------------
void PartitionData::invalidateShroudedStatusForAllPlayers() 
{ 
	for (Int i = 0; i < MAX_PLAYER_COUNT; i++) 
	{
		invalidateShroudedStatusForPlayer(i);
	}
}

#if defined(_DEBUG) || defined(_INTERNAL)
static AsciiString theObjName;
#endif

//-----------------------------------------------------------------------------
Int PartitionData::calcMaxCoiForShape(GeometryType geom, Fix majorRadius, Fix minorRadius, Bool isSmall)
{
	Int result;


  // THis is commented out, since some cases od big extets labeled small seem to be escaping.
  //M Lorenzen 8/26/03
//	if (isSmall)
//	{
//		#if defined(_DEBUG) || defined(_INTERNAL)
//		Int chk = calcMaxCoiForShape(geom, majorRadius, minorRadius, false);
//		DEBUG_ASSERTCRASH(chk <= 4, ("Small objects should be <= 4 cells, but I calced %s as %d\n",theObjName.str(),chk));
//		#endif
//		result = 4;
//	}
//	else
	{
		switch(geom)
		{
			case GEOMETRY_SPHERE:
			case GEOMETRY_CYLINDER:
			{
				// note that majorRadius is a radius, not a diameter.
				// this actually allocates a few too many, but that's ok.
				Int cells = ThePartitionManager->worldToCellDist(majorRadius*Fix(2)) + 1;
				result = cells * cells;
			}
			case GEOMETRY_BOX:
			{
				Fix diagonal = fixSqrt(majorRadius*majorRadius + minorRadius*minorRadius);
				Int cells = ThePartitionManager->worldToCellDist(diagonal*Fix(2)) + 1;
				result = cells * cells;
			}
		};
	}
	if (result < 4)
		result = 4;
	return result;
}

//-----------------------------------------------------------------------------
Int PartitionData::calcMaxCoiForObject()
{
	Object *obj = getObject();
	DEBUG_ASSERTCRASH(obj != NULL, ("must be attached to an Object here 2"));
	
	GeometryType geom = obj->getGeometryInfo().getGeomType();
	Fix majorRadius = obj->getGeometryInfo().getMajorRadiusFix();
	Fix minorRadius = obj->getGeometryInfo().getMinorRadiusFix();
	Bool isSmall = obj->getGeometryInfo().getIsSmall();
#if defined(_DEBUG) || defined(_INTERNAL)
theObjName = obj->getTemplate()->getName();
#endif
	return calcMaxCoiForShape(geom, majorRadius, minorRadius, isSmall);
}

//-----------------------------------------------------------------------------
void PartitionData::makeDirty(Bool needToUpdateCells)
{
	//DEBUG_LOG(("makeDirty for pd %08lx obj %08lx\n",this,m_object));
	if (!ThePartitionManager->isInListDirtyModules(this))
	{
		if (needToUpdateCells)
			m_dirtyStatus = NEED_CELL_UPDATE_AND_COLLISION_CHECK;
		else if (m_dirtyStatus == NOT_DIRTY)
			m_dirtyStatus = NEED_COLLISION_CHECK;
		ThePartitionManager->prependToDirtyModules(this);
	}
	else if (needToUpdateCells)
	{
		// If I am in the list already, I may be in there for a half update, so if my
		// needs have increased I'll upgrade what I want.
		m_dirtyStatus = NEED_CELL_UPDATE_AND_COLLISION_CHECK;
	}
}

//-----------------------------------------------------------------------------
void PartitionData::allocCoiArray()
{
	DEBUG_ASSERTCRASH(m_coiArrayCount == 0 && m_coiArray == NULL, ("hmm, coi should probably be null here"));
	DEBUG_ASSERTCRASH(m_coiInUseCount == 0, ("hmm, coi count mismatch"));
	m_coiArrayCount = calcMaxCoiForObject();
	m_coiArray = MSGNEW("PartitionManager_COI") CellAndObjectIntersection[m_coiArrayCount];	// may throw!
	m_coiInUseCount = 0;
	makeDirty(true);
}

//-----------------------------------------------------------------------------
void PartitionData::freeCoiArray()
{
	delete [] m_coiArray;	// yes, it's OK to call this on null...
	m_coiArray = NULL;
	m_coiArrayCount = 0;
	m_coiInUseCount = 0;
	makeDirty(true);
}

//-----------------------------------------------------------------------------
void PartitionData::attachToObject(Object* object)
{

	// remember who contains us
	m_object = object;

	// we only snapshot things that are immobile and have something to draw.  Don't need ghostobjects for others.
	if (object->isKindOf(KINDOF_IMMOBILE))
	{	const ThingTemplate *tmplate=object->getTemplate();
		const ModuleInfo &drawMod=tmplate->getDrawModuleInfo();
		//Objects with default draw modules usually don't need ghostobjects so skip them to save memory.
		Bool makeGhostObject=TRUE;
		for (Int i=0; i<drawMod.getCount(); i++)
		{
			if (strcmp(drawMod.getNthName(i).str(),"W3DDefaultDraw") == 0)
			{	makeGhostObject=FALSE;
				break;
			}
		}
		if (makeGhostObject)
			m_ghostObject = TheGhostObjectManager->addGhostObject(object, this);
	}

	//DEBUG_LOG(("attach pd for pd %08lx obj %08lx\n",this,m_object));

	// (re)calc maxCoi and (re)alloc cois
	DEBUG_ASSERTCRASH(m_coiArrayCount == 0 && m_coiArray == NULL, ("hmm, coi should probably be null here"));
	DEBUG_ASSERTCRASH(m_coiInUseCount == 0, ("hmm, coi count mismatch"));
	freeCoiArray();
	allocCoiArray();	// may throw!

	if (object)
		object->friend_setPartitionData(this);
	makeDirty(true);
}

//-----------------------------------------------------------------------------
void PartitionData::detachFromObject()
{
	// this is a little hokey... if we are in the midst of processing the contact
	// list, we have to ensure that any potential collisions get removed from that
	// list, since we are about to go bye-bye. ugly, but effective. (srj)
	//
	// note 2: we used to do this in our dtor, but it's too late then, since we require
	// our Object to still be present in order to figure out what to remove.
	// so do it here, since detaching it should nuke it from the obj list anyway. (srj)
	if (TheContactList)
		TheContactList->removeSpecificPartitionData(this);

	if (m_object)
		m_object->friend_setPartitionData(NULL);
	removeAllTouchedCells();
	freeCoiArray();

	//DEBUG_LOG(("detach pd for pd %08lx obj %08lx\n",this,m_object));

	// no longer attached to object
	m_object = NULL;
	TheGhostObjectManager->removeGhostObject(m_ghostObject);
	m_ghostObject = NULL;
}

//-----------------------------------------------------------------------------
void PartitionData::attachToGhostObject(GhostObject* object)
{

	// remember who contains us
	m_object = NULL;	//it's only attached to a ghost object, no parent object.
	m_ghostObject = object;

	// (re)calc maxCoi and (re)alloc cois
	DEBUG_ASSERTCRASH(m_coiArrayCount == 0 && m_coiArray == NULL, ("hmm, coi should probably be null here"));
	DEBUG_ASSERTCRASH(m_coiInUseCount == 0, ("hmm, coi count mismatch"));
	freeCoiArray();

	m_coiArrayCount = calcMaxCoiForShape(object->getGeometryType(), fixFromReal(object->getGeometryMajorRadius()), fixFromReal(object->getGeometryMinorRadius()),object->getGeometrySmall());
	m_coiArray = MSGNEW("PartitionManager_COI") CellAndObjectIntersection[m_coiArrayCount];	// may throw!
	m_coiInUseCount = 0;
	makeDirty(true);

	if (m_ghostObject)
		m_ghostObject->updateParentObject(NULL,this);
}

//-----------------------------------------------------------------------------
void PartitionData::detachFromGhostObject(void)
{
	// this is a little hokey... if we are in the midst of processing the contact
	// list, we have to ensure that any potential collisions get removed from that
	// list, since we are about to go bye-bye. ugly, but effective. (srj)
	//
	// note 2: we used to do this in our dtor, but it's too late then, since we require
	// our Object to still be present in order to figure out what to remove.
	// so do it here, since detaching it should nuke it from the obj list anyway. (srj)
	if (TheContactList)
		TheContactList->removeSpecificPartitionData(this);

	removeAllTouchedCells();
	freeCoiArray();

	//DEBUG_LOG(("detach pd for pd %08lx obj %08lx\n",this,m_object));

	// no longer attached to object
	m_object = NULL;
	m_ghostObject = NULL;
}

//-----------------------------------------------------------------------------
inline UnsignedInt hash2ints(Int a, Int b)
{
	// do it this way so that [a,b] always hashes to the same value as [b,a].
	// this is unsophisticated but reasonable, since all ObjectIDs will
	// quite likely be well below 65536...
	if (a < b)
	{
		return (a<<16)+b;
	}
	else
	{
		return (b<<16)+a;
	}
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
void PartitionContactList::addToContactList( PartitionData *obj, PartitionData *other )
{
	if (obj == other || obj == NULL || other == NULL)
		return;
	
	Object* obj_obj = obj->getObject();
	Object* other_obj = other->getObject();
	if (obj_obj == NULL || other_obj == NULL)
		return;

	// compute hash index based on object's ids.
	UnsignedInt hashValue = hash2ints(obj_obj->getID(), other_obj->getID());
	hashValue %= PartitionContactList_SOCKET_COUNT;

	// make sure given hit has not already been recorded 
	for (PartitionContactListNode* cd = m_contactHash[ hashValue ]; cd; cd = cd->m_nextHash )
	{
		if ((cd->m_obj == obj && cd->m_other == other) ||
				(cd->m_obj == other && cd->m_other == obj)) 
		{
			// already noted 
			return;
		}
	}

	// new hit 
	PartitionContactListNode *ncd = newInstance(PartitionContactListNode);
	ncd->m_obj = obj;
	ncd->m_other = other;
	ncd->m_hashValue = hashValue;

	// add to hash table 
	ncd->m_nextHash = m_contactHash[ hashValue ];
	m_contactHash[ hashValue ] = ncd;

	// add to list of contacts for this frame 
	ncd->m_next = m_contactList;
	m_contactList = ncd;


#if 0

Int depth = 0;
for (PartitionContactListNode *cd2 = m_contactHash[ hashValue ]; cd2; cd2 = cd2->m_nextHash )
{
	depth++;
}
if (depth > 3)
{
	DEBUG_LOG(("depth is %d for %s %08lx (%d) - %s %08lx (%d)\n",
		depth,obj_obj->getTemplate()->getName().str(),obj_obj,obj_obj->getID(),
		other_obj->getTemplate()->getName().str(),other_obj,other_obj->getID()
		));

	for (cd2 = m_contactHash[ hashValue ]; cd2; cd2 = cd2->m_nextHash )
	{
		UnsignedInt rawhash = djb2hash2ints(cd2->m_obj->getObject()->getID(), cd2->m_other->getObject()->getID());
		//hashValue %= PartitionContactList_SOCKET_COUNT;


		DEBUG_LOG(("ENTRY: %s %08lx (%d) - %s %08lx (%d) [rawhash %d]\n",
			cd2->m_obj->getObject()->getTemplate()->getName().str(),cd2->m_obj->getObject(),cd2->m_obj->getObject()->getID(),
			cd2->m_other->getObject()->getTemplate()->getName().str(),cd2->m_other->getObject(),cd2->m_other->getObject()->getID(),
			rawhash));
	}
}

static Real aggtotal = 0;
static Real aggfull = 0;
static Real aggcount = 0;
for (int ii = 0; ii < PartitionContactList_SOCKET_COUNT; ++ii)
{
	if (m_contactHash[ii])
		aggfull += 1.0f;

	for (cd2 = m_contactHash[ ii ]; cd2; cd2 = cd2->m_nextHash )
	{
		aggtotal += 1.0f;
	}
}
aggcount += 1.0f;
DEBUG_ASSERTLOG(((Int)aggcount)%1000!=0,("avg hash depth at %f is %f, fullness %f%%\n",
aggcount,aggtotal/(aggcount*PartitionContactList_SOCKET_COUNT),(aggfull*100)/(aggcount*PartitionContactList_SOCKET_COUNT)));
#endif


}

//-----------------------------------------------------------------------------
void PartitionContactList::removeSpecificPartitionData(PartitionData* data)
{
	for (PartitionContactListNode* cd = m_contactList; cd; cd = cd->m_next)
	{
		if (cd->m_obj == data || cd->m_other == data)
		{
			cd->m_obj = NULL;
			cd->m_other = NULL;
		}
	}
}

//-----------------------------------------------------------------------------
void PartitionContactList::resetContactList()
{
	// remove items from hash table 
	PartitionContactListNode* cdnext;
	for (PartitionContactListNode* cd = m_contactList; cd; cd = cdnext)
	{
		cdnext = cd->m_next;
		cd->deleteInstance();
	}

	memset(m_contactHash, 0, sizeof(m_contactHash));
	m_contactList = NULL;
}

//-----------------------------------------------------------------------------
void PartitionContactList::processContactList()
{
	for (PartitionContactListNode* cd = m_contactList; cd; cd = cd->m_next) 
	{
		if (cd->m_obj == NULL || cd->m_other == NULL)
			continue;

		// we know that their partitions overlap; determine if they REALLY collide 
		// before proceeding...
		CollideLocAndNormal cinfo;
		if (!cd->m_obj->friend_collidesWith(cd->m_other, &cinfo))
			continue;

		Object* obj = cd->m_obj->getObject();
		Object* other = cd->m_other->getObject();
		
		if( obj->getStatusBits().test( OBJECT_STATUS_NO_COLLISIONS ) ||
				other->getStatusBits().test( OBJECT_STATUS_NO_COLLISIONS ) )
			continue;

		DEBUG_ASSERTCRASH(!(obj->isKindOf(KINDOF_IMMOBILE) && other->isKindOf(KINDOF_IMMOBILE)), 
			("we should never have collisions between two immobile things reported"));

		// the onCollide() calls can remove the object(s) from the partition mgr,
		// thus destroying the partitiondata for 'em. go ahead and null these out here
		// so we won't be tempted to use 'em (since they might be bogus).
		cd->m_obj = NULL;
		cd->m_other = NULL;

		obj->onCollide(other, &cinfo.loc, &cinfo.normal);
		flipCoord3D(&cinfo.normal);

 		//Before checking the "other" case, make sure that the previous collision didn't
 		//absorb him. This becomes a conflict for pilots giving veterancy to transports
 		//and pilots entering transports. Both would occur if these isDestroyed checks 
 		//were missing.
 		if( !obj->isDestroyed() && !other->isDestroyed() )
 		{
 			other->onCollide(obj, &cinfo.loc, &cinfo.normal);
 		}

		//
		// NOTE: it is VERY IMPORTANT (for performance reasons) to not re-dirty immobile things.
		//
		// NOTE also that we re-get partitiondata from the object, since it might have been
		// removed from the partition system by the onCollide call...
		//
		if (!obj->isDestroyed() && obj->friend_getPartitionData() != NULL && !obj->isKindOf(KINDOF_IMMOBILE))
		{
//DEBUG_LOG(("%d: re-dirtying collision of %s %08lx with %s %08lx\n",TheGameLogic->getFrame(),obj->getTemplate()->getName().str(),obj,other->getTemplate()->getName().str(),other));
			obj->friend_getPartitionData()->makeDirty(false);
		}
		if (!other->isDestroyed() && other->friend_getPartitionData() != NULL && !other->isKindOf(KINDOF_IMMOBILE))
		{
//DEBUG_LOG(("%d: re-dirtying collision of %s %08lx with %s %08lx [other]\n",TheGameLogic->getFrame(),other->getTemplate()->getName().str(),other,obj->getTemplate()->getName().str(),obj));
			other->friend_getPartitionData()->makeDirty(false);
		}
	}
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
PartitionManager::PartitionManager()
{
	m_moduleList = NULL;
	m_cellSize = m_cellSizeInv = 0.0f;
	m_cellSizeFix = Fix(0);
	m_worldLoFix.zero();
	m_cellCountX = 0;
	m_cellCountY = 0;
	m_totalCellCount = 0;
	m_cells = NULL;
	m_worldExtents.lo.zero();
	m_worldExtents.hi.zero();
	m_dirtyModules = NULL;
	m_updatedSinceLastReset = false;
#ifdef FASTER_GCO
	m_maxGcoRadius = 0;
#endif
} 

//-----------------------------------------------------------------------------
PartitionManager::~PartitionManager()
{

	shutdown();

}  // end ~PartitionManager

//-----------------------------------------------------------------------------
#ifdef PM_CACHE_TERRAIN_HEIGHT
static void calcHeights(const FCoord2D& worldLo, Fix cellSize, Int x, Int y, Fix& loZ, Fix& hiZ)
{
	DEBUG_ASSERTCRASH(TheTerrainLogic, ("no TheTerrainLogic"));
	Fix xbase = worldLo.x + (Fix(x) * cellSize);
	Fix ybase = worldLo.y + (Fix(y) * cellSize);
	const Fix ROUGH_STEP_SIZE = Fix(10);	// MAP_XY_FACTOR: no point in stepping smaller than grid scale
	Int numSteps = PartitionManager_cellsIn(cellSize, ROUGH_STEP_SIZE, true);
	Fix step = cellSize / Fix(numSteps);
	loZ = HUGE_DIST_FIX;		// huge positive
	hiZ = -HUGE_DIST_FIX;		// huge negative
	for (Fix yy = Fix(0); yy <= cellSize; yy += step)
	{
		for (Fix xx = Fix(0); xx <= cellSize; xx += step)
		{
			Fix h = TheTerrainLogic->getGroundHeightFix( xbase + xx, ybase + yy );
			if (h < loZ) loZ = h;
			if (h > hiZ) hiZ = h;
		}
	}
}
#endif

//-----------------------------------------------------------------------------
void PartitionManager::init()
{
	m_cellSize = TheGlobalData->m_partitionCellSize;
	if (m_cellSize < 1.0)
		m_cellSize = 1.0;

	m_cellSizeInv = (Real)(1.0 / m_cellSize);
	m_cellSizeFix = fixFromReal(m_cellSize);
	m_worldLoFix.zero();

	DEBUG_ASSERTCRASH(m_cells == NULL, ("double init"));

	if (TheTerrainLogic)
	{
		TheTerrainLogic->getExtent(&m_worldExtents);
		//DEBUG_ASSERTLOG(m_worldExtents.width() > 0 && m_worldExtents.height() > 0, ("TheTerrainLogic must be loaded before ThePartitionManager"));
		// you'd think it wouldn't be legal for terrainlogic to have zero area, but apparently
		// that's what it resets itself to. let's just make the world a simple place and pretend
		// it's nonzero in area, so that we can proceed without crashing.
		if (m_worldExtents.width() < 1.0f)
			m_worldExtents.hi.x = m_worldExtents.lo.x + 1.0f;
		if (m_worldExtents.height() < 1.0f)
			m_worldExtents.hi.y = m_worldExtents.lo.y + 1.0f;
		m_worldLoFix.set(fixFromReal(m_worldExtents.lo.x), fixFromReal(m_worldExtents.lo.y));
		m_cellCountX = REAL_TO_INT_CEIL(m_worldExtents.width() * m_cellSizeInv);
		m_cellCountY = REAL_TO_INT_CEIL(m_worldExtents.height() * m_cellSizeInv);
		m_totalCellCount = m_cellCountX * m_cellCountY;
		m_cells = MSGNEW("PartitionManager_Cells") PartitionCell[m_totalCellCount];
		for (Int x = 0; x < m_cellCountX; x++)
		{
			for (Int y = 0; y < m_cellCountY; y++)
			{
#ifdef PM_CACHE_TERRAIN_HEIGHT
				Fix loZ, hiZ;
				calcHeights(m_worldLoFix, m_cellSizeFix, x, y, loZ, hiZ);
				getCellAt(x, y)->init(x, y, loZ, hiZ);
#else
				getCellAt(x, y)->init(x, y);
#endif
			}
		}

#ifdef FASTER_GCO
		calcRadiusVec();
#endif

	}
	else
	{
		m_cellSize = m_cellSizeInv = 0.0f;
		m_cellSizeFix = Fix(0);
		m_worldLoFix.zero();
		m_cellCountX = 0;
		m_cellCountY = 0;
		m_totalCellCount = 0;
		m_cells = NULL;
		m_worldExtents.lo.zero();
		m_worldExtents.hi.zero();
	}
}

//-----------------------------------------------------------------------------
#ifdef DUMP_PERF_STATS
void PartitionManager::getPMStats(double& gcoTimeThisFrameTotal, double& gcoTimeThisFrameAvg)
{
	Int64 freq64;
	GetPrecisionTimerTicksPerSec(&freq64);

	double gcoTimeInMSecs = (double)s_timeInClosestObjectsThisFrame * 1000.0f / (double)freq64;

	gcoTimeThisFrameTotal = gcoTimeInMSecs;
	gcoTimeThisFrameAvg = gcoTimeInMSecs / (double)s_countInClosestObjectsThisFrame;
}
#endif

//-----------------------------------------------------------------------------
void PartitionManager::reset()
{
#ifdef DUMP_PERF_STATS
	s_countInClosestObjects = 0;
	s_timeInClosestObjects = 0;
	s_countInClosestObjectsThisFrame = 0;
	s_timeInClosestObjectsThisFrame = 0;
	s_gcoPerfFrame = 0xffffffff;
#endif

	resetPendingUndoShroudRevealQueue();

	shutdown();
	//init();
}

//-----------------------------------------------------------------------------
void PartitionManager::shutdown()
{
	m_updatedSinceLastReset = false;
	ThePartitionManager->removeAllDirtyModules();

#ifdef _DEBUG
	// the above *should* remove all the touched cells (via unRegisterObject), but let's check:
	DEBUG_ASSERTCRASH( m_moduleList == NULL, ("hmm, modules left over"));
	PartitionData *mod, *nextMod;
	for( mod = m_moduleList; mod; mod = nextMod )
	{
		nextMod = mod->getNext();
		DEBUG_ASSERTCRASH(mod->friend_getCoiInUseCount() == 0, ("hmm, coi count mismatch"));
		mod->friend_removeAllTouchedCells();
	}
#endif

#ifdef FASTER_GCO
	m_radiusVec.clear();
#endif

	resetPendingUndoShroudRevealQueue();
	
	delete [] m_cells;
	m_cells = NULL;

	m_cellSize = m_cellSizeInv = 0.0f;
	m_cellSizeFix = Fix(0);
	m_worldLoFix.zero();
	m_cellCountX = 0;
	m_cellCountY = 0;
	m_totalCellCount = 0;
	m_worldExtents.lo.zero();
	m_worldExtents.hi.zero();
}

//-----------------------------------------------------------------------------
//DECLARE_PERF_TIMER(PartitionManager_update)
void PartitionManager::update()
{
	//USE_PERF_TIMER(PartitionManager_update)
	{
#ifdef INTENSE_DEBUG
		Int cc = 0;
#endif
		if (!m_updatedSinceLastReset) 
		{
			m_updatedSinceLastReset = true;
		}

		PartitionContactList ctList;
		TheContactList = &ctList;
		while (m_dirtyModules)
		{
#ifdef INTENSE_DEBUG
			++cc;
#endif

			// save it.
			PartitionData *dirty = m_dirtyModules;
			DEBUG_ASSERTCRASH(dirty->getObject() != NULL || dirty->getGhostObject() != NULL, 
												("must be attached to an Object here %08lx",dirty));

			// get this BEFORE removing from dirty list, since that clears the
			// flag in question.
			Bool updateEm = dirty->isInNeedOfUpdatingCells();
			Bool collideEm = dirty->isInNeedOfCollisionCheck() && dirty->getObject();	//only update collisions if we have object
			
			// detach it from the dirty list.
			removeFromDirtyModules(dirty);

			if (updateEm)
			{
				dirty->friend_updateCellsTouched();
			}

			if (collideEm && !dirty->getObject()->isKindOf(KINDOF_IMMOBILE))
			{
				dirty->addPossibleCollisions(&ctList);
			}
		}
		
		ctList.processContactList();
#ifdef INTENSE_DEBUG
		DEBUG_ASSERTLOG(cc==0,("updated partition info for %d objects\n",cc));
#endif
		TheContactList = NULL;

		processPendingUndoShroudRevealQueue();
	}

#if defined(_DEBUG) || defined(_INTERNAL)
	if (TheGlobalData->m_debugThreatMap) 
	{
		if (TheGameLogic->getFrame() % TheGlobalData->m_debugThreatMapTileDuration)
		{
			Int cellCount = m_cellCountX * m_cellCountY;
			for (int i = 0; i < cellCount; ++i) 
			{
				UnsignedInt threat = m_cells[i].getThreatValue(ThePlayerList->getLocalPlayer()->getPlayerIndex());
				if (threat > 0) 
				{
					Real threatMul = INT_TO_REAL(threat) / TheGlobalData->m_maxDebugThreat;
					if (threatMul > 1.0f)
						threatMul = 1.0f;

					Real size = TheGlobalData->m_partitionCellSize;
					Coord3D pos = { m_cells[i].getCellX() * size, 
													m_cells[i].getCellY() * size, 
													0 };
					pos.z = fixToReal(TheTerrainLogic->getGroundHeightFix(fixFromReal(pos.x), fixFromReal(pos.y)));	// a debug icon, drawn
					RGBColor color;
					color.red = threatMul;
					color.blue = 0.0f;
					color.green = 0.0f;

					addIcon(&pos, size, TheGlobalData->m_debugThreatMapTileDuration, color);
				}
			}
		}
	}

	if (TheGlobalData->m_debugCashValueMap)
	{
		if (TheGameLogic->getFrame() % TheGlobalData->m_debugCashValueMapTileDuration)
		{
			Int cellCount = m_cellCountX * m_cellCountY;
			for (int i = 0; i < cellCount; ++i) 
			{
				UnsignedInt value = m_cells[i].getCashValue(ThePlayerList->getLocalPlayer()->getPlayerIndex());
				if (value > 0) 
				{
					Real valueMul = INT_TO_REAL(value) / TheGlobalData->m_maxDebugValue;
					if (valueMul > 1.0f)
						valueMul = 1.0f;

					Real size = TheGlobalData->m_partitionCellSize;
					Coord3D pos = { m_cells[i].getCellX() * size, 
													m_cells[i].getCellY() * size, 
													0 };
					pos.z = fixToReal(TheTerrainLogic->getGroundHeightFix(fixFromReal(pos.x), fixFromReal(pos.y)));	// a debug icon, drawn
					RGBColor color;
					color.red = 0.0f;
					color.blue = 0.0f;
					color.green = valueMul;

					addIcon(&pos, size, TheGlobalData->m_debugCashValueMapTileDuration, color);
				}
			}			
		}
	}
#endif // defined(_DEBUG) || defined(_INTERNAL)
}  // end update

//------------------------------------------------------------------------------
void PartitionManager::registerObject( Object* object )
{
	// sanity
	if( object == NULL )
		return;

	// if object is already part of this system get out of here
	if( object->friend_getPartitionData() != NULL )
	{
		DEBUG_LOG(( "Object '%s' already registered with partition manager\n",
								object->getTemplate()->getName().str() ));
		return;
	}  // end if

	// allocate a new module of partition data
	PartitionData *mod = newInstance( PartitionData );

	// link the module to the list in the partition manager
	mod->setPrev( NULL );
	mod->setNext( m_moduleList );
	if( m_moduleList )
		m_moduleList->setPrev( mod );
	m_moduleList = mod;

	// add module to object
	mod->attachToObject( object );

}

//------------------------------------------------------------------------------
void PartitionManager::unRegisterObject( Object* object )
{
	// sanity
	if( object == NULL )
		return;

	// get the partition module
	PartitionData *mod = object->friend_getPartitionData();
	if( mod == NULL )
		return;

	GhostObject *ghost;

	// need to figure out if any players have a fogged memory of this object.
	// if so, we can't remove it from the shroud system just yet.
	if ((ghost=mod->getGhostObject()) != NULL && mod->wasSeenByAnyPlayers() < MAX_PLAYER_COUNT)
	{
		if (TheContactList)
			TheContactList->removeSpecificPartitionData(mod);
		object->friend_setPartitionData(NULL);
		mod->friend_setObject(NULL);
		//Tell the ghost object that its parent is dead.
		ghost->updateParentObject(NULL, mod);
		return;
	}

	// detach the module from the object
	mod->detachFromObject();

	// remove module from master list
	PartitionData *next = mod->getNext();
	PartitionData *prev = mod->getPrev();
	if( next )
		next->setPrev( prev );
	if( prev )
		prev->setNext( next );
	else
		m_moduleList = next;

	// delete module
	mod->deleteInstance();

}

//------------------------------------------------------------------------------
void PartitionManager::registerGhostObject( GhostObject* object)
{
	// sanity
	if( object == NULL )
		return;

	// if object is already part of this system get out of here
	if( object->friend_getPartitionData() != NULL )
	{
		DEBUG_LOG(( "GhostObject already registered with partition manager\n"));
		return;
	}  // end if

	// allocate a new module of partition data
	PartitionData *mod = newInstance( PartitionData );

	// link the module to the list in the partition manager
	mod->setPrev( NULL );
	mod->setNext( m_moduleList );
	if( m_moduleList )
		m_moduleList->setPrev( mod );
	m_moduleList = mod;

	// add module to object
	mod->attachToGhostObject( object);
}

//------------------------------------------------------------------------------
void PartitionManager::unRegisterGhostObject( GhostObject* object )
{
	// sanity
	if( object == NULL )
		return;

	// get the partition module
	PartitionData *mod = object->friend_getPartitionData();
	if( mod == NULL )
		return;

	// detach the module from the object
	mod->detachFromGhostObject();

	// remove module from master list
	PartitionData *next = mod->getNext();
	PartitionData *prev = mod->getPrev();
	if( next )
		next->setPrev( prev );
	if( prev )
		prev->setNext( next );
	else
		m_moduleList = next;

	// delete module
	mod->deleteInstance();
}

/** 
	Reveals the map for the given player, but does not override Shroud generation.  (Script)
*/
void PartitionManager::revealMapForPlayer( Int playerIndex )
{
	// By looking and then stopping on every cell, I clear all Passive Shroud
	// By adding a looker directly I don't hit the Ally logic of the normal look/doShroudReveal
	for (int i = 0; i < m_totalCellCount; ++i) 
	{
		m_cells[i].addLooker( playerIndex );
		m_cells[i].removeLooker( playerIndex );
	}
}

/** 
	Reveals the map for the given player, AND permanently disables all Shroud generation (Observer Mode).
	*/
void PartitionManager::revealMapForPlayerPermanently( Int playerIndex )
{
	// By skipping the removeLooker, I consider myself as actively looking at everything, 
	// so Shroud generation will no longer function
	// By adding a looker directly I don't hit the Ally logic of the normal look/doShroudReveal
	for (int i = 0; i < m_totalCellCount; ++i) 
	{
		m_cells[i].addLooker( playerIndex );
	}
}

/** 
		Adds a layer of permanent blindness.  Used solely to undo the permanent reveal for debugging
	*/
void PartitionManager::undoRevealMapForPlayerPermanently( Int playerIndex )
{
	//First make sure no lingering looks will leave holes when they aren't wanted.
	processEntirePendingUndoShroudRevealQueue();

	// This will have amusing consequences if done without a preceding revealMapForPlayerPermanently.
	// Everything you own can become shrouded.
	for (int i = 0; i < m_totalCellCount; ++i) 
	{
		m_cells[i].removeLooker( playerIndex );
	}
}

/** 
	Resets the shroud for the given player with passive shroud (can re-explore).
	*/
void PartitionManager::shroudMapForPlayer( Int playerIndex )
{
	//First make sure no lingering looks will leave holes when they aren't wanted.
	processEntirePendingUndoShroudRevealQueue();

	// By pulsing a blast of shroud like this, we will set everything not actively looked at as Passive Shroud
	for (int i = 0; i < m_totalCellCount; ++i) 
	{
		m_cells[i].addShrouder( playerIndex );
		m_cells[i].removeShrouder( playerIndex );
	}
}

//-----------------------------------------------------------------------------
void PartitionManager::refreshShroudForLocalPlayer()
{
	// This is a drawing refresh only, and so is allowed to use the Local Player, or the player a
	// watcher is looking through.
	TheDisplay->clearShroud();
	TheRadar->clearShroud();

	Int playerIndex = TheObserverCamera.getShroudPlayerIndex();
	for (int i = 0; i < m_totalCellCount; ++i)
	{
		Int x = m_cells[i].getCellX();
		Int y = m_cells[i].getCellY();
		CellShroudStatus status = m_cells[i].getShroudStatusForPlayer(playerIndex);
		TheDisplay->setShroudLevel(x, y, status);
		TheRadar->setShroudLevel(x, y, status);
		m_cells[i].invalidateShroudedStatusForAllCois(playerIndex);
	}
}

//-----------------------------------------------------------------------------
CellShroudStatus PartitionManager::getShroudStatusForPlayer(Int playerIndex, Int x, Int y) const
{
	if( playerIndex < 0 )
		return CELLSHROUD_SHROUDED;// Safety.  There are no Negative players, but PlayerIndex is typedef'd to Int, not UnsignedInt

	const PartitionCell* cell = getCellAt(x, y);
	return cell ? cell->getShroudStatusForPlayer(playerIndex) : CELLSHROUD_SHROUDED;
}

//-----------------------------------------------------------------------------
CellShroudStatus PartitionManager::getShroudStatusForPlayer(Int playerIndex, const Coord3D *loc ) const
{
	Int x, y;

	ThePartitionManager->worldToCell( loc->x, loc->y, &x, &y );

	return getShroudStatusForPlayer( playerIndex, x, y );
}


//-----------------------------------------------------------------------------
ObjectShroudStatus PartitionManager::getPropShroudStatusForPlayer(Int playerIndex, const Coord3D *loc ) const
{
	Int x, y;

	ThePartitionManager->worldToCell( loc->x - m_cellSize*0.5f, loc->y - m_cellSize*0.5f, &x, &y );

	CellShroudStatus cellStat = getShroudStatusForPlayer( playerIndex, x, y );
	if (cellStat != getShroudStatusForPlayer( playerIndex, x+1, y )) {
		return OBJECTSHROUD_PARTIAL_CLEAR;
	}
	if (cellStat != getShroudStatusForPlayer( playerIndex, x+1, y+1 )) {
		return OBJECTSHROUD_PARTIAL_CLEAR;
	}
	if (cellStat != getShroudStatusForPlayer( playerIndex, x, y+1 )) {
		return OBJECTSHROUD_PARTIAL_CLEAR;
	}
	if (cellStat == CELLSHROUD_SHROUDED) {
		return OBJECTSHROUD_SHROUDED;
	}
	if (cellStat == CELLSHROUD_CLEAR) {
		return OBJECTSHROUD_CLEAR;
	}
	return OBJECTSHROUD_FOGGED;
}



#ifdef FASTER_GCO
//-----------------------------------------------------------------------------
Int PartitionManager::calcMinRadius(const ICoord2D& cur)
{
	/*
		ok, so if obj "A" is in cell (0,0), and obj "B"
		is in cell (cur.x, cur.y), calc the smallest distance they could be
		from each other, in integral multiples of cellSize. Note that
		each object can be anywhere within a given cell (not just in the center!)
		so even objects in the same cell can still be cellSize*1.414 apart,
		and objects in adjacent cells can be nearly-zero distance apart!
	*/
	Real halfCell = m_cellSize * 0.5;
	Coord3D centerPos[4] = 
	{ 
		{ -halfCell, -halfCell }, 
		{ halfCell, -halfCell }, 
		{ -halfCell, halfCell }, 
		{ halfCell, halfCell } 
	};

	Real x = cur.x * m_cellSize;
	Real y = cur.y * m_cellSize;
	Coord3D otherPos[4] = 
	{ 
		{ x - halfCell, y - halfCell }, 
		{ x + halfCell, y - halfCell }, 
		{ x - halfCell, y + halfCell }, 
		{ x + halfCell, y + halfCell } 
	};

	/*
		this is ugly and there's probably a faster way, but we only do this once per PM reset,
		so it really shouldn't matter... (I hope)
	*/

	double minDistSqr = 1e12;				// double, not real
	for (int i = 0; i < 4; ++i)
	{
		for (int j = 0; j < 4; ++j)
		{
			// double, not real
			double dx = centerPos[i].x - otherPos[j].x;
			double dy = centerPos[i].y - otherPos[j].y;
			double curDistSqr = dx*dx + dy*dy;
			if (minDistSqr > curDistSqr)
				minDistSqr = curDistSqr;
		}
	}

	// double, not real
	double dist = sqrtf(minDistSqr);
	Int minRadius = REAL_TO_INT_CEIL( dist / m_cellSize );

	return minRadius;
}
#endif

#ifdef FASTER_GCO
//-----------------------------------------------------------------------------
void PartitionManager::calcRadiusVec()
{
	Real cellSize = getCellSize();
	Int cx = getCellCountX();
	Int cy = getCellCountY();

	// double, not real
	double dx = (double)cx * (double)cellSize;
	double dy = (double)cy * (double)cellSize;
	double maxPossibleDist = sqrt(dx*dx + dy*dy);
	
	m_maxGcoRadius = REAL_TO_INT_CEIL(maxPossibleDist / cellSize);
	
	m_radiusVec.clear();
	m_radiusVec.resize(m_maxGcoRadius+1, OffsetVec());

	ICoord2D cur;
	for (cur.y = -cy+1; cur.y < cy; ++cur.y)
	{
		for (cur.x = -cx+1; cur.x < cx; ++cur.x)
		{
			/*
				m_radiusVec[curRadius] contains a list of the cells (foo) that could
				contain objects that are <= (curRadius * cellSize) distance away from cell (0,0).
			*/
			Int curRadius = calcMinRadius(cur);
			DEBUG_ASSERTCRASH(curRadius <= m_maxGcoRadius, ("expected max of %d but got %d\n",m_maxGcoRadius,curRadius));
			if (curRadius <= m_maxGcoRadius)
				m_radiusVec[curRadius].push_back(cur);
		}
	}

#if defined(_DEBUG) || defined(_INTERNAL)
	Int total = 0;
	for (Int i = 0; i <= m_maxGcoRadius; ++i)
	{
		total += m_radiusVec[i].size();
		//DEBUG_LOG(("radius %d has %d entries\n",i,m_radiusVec[i].size()));
	}
	DEBUG_ASSERTCRASH(total == (cx*2-1)*(cy*2-1),("expected %d, got %d\n",(cx*2-1)*(cy*2-1),total));
#endif

}
#endif

//-----------------------------------------------------------------------------
//DECLARE_PERF_TIMER(getClosestObjects)
Object *PartitionManager::getClosestObjectsFix(
	const Object *obj,
	const FCoord3D *pos,
	Fix maxDist,
	DistanceCalculationType dc,
	PartitionFilter **filters,
	SimpleObjectIterator *iterArg,	// if nonnull, append ALL satisfactory objects to the iterator (not just the single closest)
	Fix *closestDistArg,
	FCoord3D *closestVecArg
)
{
	// a sentinel past HUGE_DIST means "anywhere", and HUGE_DIST's square is the largest that fits
	if (maxDist > HUGE_DIST_FIX)
		maxDist = HUGE_DIST_FIX;

	//USE_PERF_TIMER(getClosestObjects)

#ifdef DUMP_PERF_STATS
	if (TheGameLogic->getFrame() != s_gcoPerfFrame)
	{
		s_gcoPerfFrame = TheGameLogic->getFrame();
		s_countInClosestObjectsThisFrame = 0;
		s_timeInClosestObjectsThisFrame = 0;
	}
	++s_countInClosestObjects;
	++s_countInClosestObjectsThisFrame;

	Int64 startTime64;
	GetPrecisionTimer(&startTime64);
#endif
	
#ifdef _DEBUG
	static Int theEntrancyCount = 0;
	DEBUG_ASSERTCRASH(theEntrancyCount == 0, ("sorry, this routine is not reentrant"));
	++theEntrancyCount;
#endif

	DEBUG_ASSERTCRASH((obj==NULL) != (pos == NULL), ("either obj or pos must be null"));

	DistCalcProc distProc = theDistCalcProcs[dc];

	const FCoord3D *objPos;
	const Object *objToUse;
	if (pos)
	{
		objPos = pos;
		objToUse = NULL;
	}
	else
	{
		objPos = obj->getPositionFix();
		objToUse = obj;
	}
	/* This is the single most expensive function in an eight-player match - 9.6% of the whole game
		 in a sampling profile - and nothing said whether that was a few enormous queries or a great
		 many small ones. Two counters, read by the slow-frame log, which is the difference between
		 knowing and guessing. */
	++thePartitionQueries;
	// the gathering form is the dear one: it keeps every survivor, sorted, in a pooled iterator
	if (iterArg)
		++thePartitionGathers;

	Int cellCenterX, cellCenterY;
	// ponytail: the cell grid is still float; P5 gives world-to-cell its own fixed floor
	worldToCellFix(objPos->x, objPos->y, &cellCenterX, &cellCenterY);

	Object* closestObj = NULL;
	Fix closestDistSqr = maxDist * maxDist;	// if it's not closer than this, we shouldn't consider it anyway...
	FCoord3D closestVec;
	closestVec.zero();

#ifdef FASTER_GCO

	Int maxRadius = m_maxGcoRadius;
	if (maxDist < HUGE_DIST_FIX)
	{
		// don't go outwards any farther than necessary.
		maxRadius = minInt(m_maxGcoRadius, worldToCellDist(maxDist));
	}
#if defined(INTENSE_DEBUG)
	/*
		Note, if you ever enable this code, be forewarned that it can give
		you "false positives" for objects that are located just off the map... (srj)
	*/
	Int maxRadiusLimit = maxRadius + 3;
	if (maxRadiusLimit > m_maxGcoRadius) maxRadiusLimit = m_maxGcoRadius;
#else
	Int maxRadiusLimit = maxRadius;
#endif

	Bool foundAny = false;

	static Int theIterFlag = 1;	// nonzero, thanks
	++theIterFlag;

	/* A gathering query keeps everything in range in whatever order it finds it, and the caller sorts
		 afterwards. When the range covers the whole map the ring walk below still steps through every
		 cell offset either side of the centre, four times the map's cells and most of them off it: a
		 hunting unit's whole-map search spent 15-33ms there to find 3,600 objects. The master module
		 list holds exactly the objects that sit in cells, so walking it gathers the same set. */
	if (iterArg && maxRadius >= m_maxGcoRadius)
	{
		for (PartitionData *thisMod = m_moduleList; thisMod; thisMod = thisMod->getNext())
		{
			if (thisMod->friend_getCoiInUseCount() == 0)
				continue;

			Object *thisObj = thisMod->getObject();
			++thePartitionQueryObjects;
			if (thisObj == obj || thisObj == NULL)
				continue;

			Fix thisDistSqr;
			FCoord3D distVec;
			if (!(*distProc)(objPos, objToUse, thisObj->getPositionFix(), thisObj, thisDistSqr, distVec, closestDistSqr))
				continue;

			if (!filtersAllow(filters, thisObj))
				continue;

			iterArg->insertFix(thisObj, thisDistSqr);
		}
		maxRadiusLimit = -1;	// nothing left for the ring walk to find
	}

	/*
		m_radiusVec[curRadius] contains a list of the cells (foo) that could
		contain objects that are <= (curRadius * cellSize) distance away from cell (0,0).
	*/
  for (Int curRadius = 0; curRadius <= maxRadiusLimit; ++curRadius)
  {
    const OffsetVec& offsets = m_radiusVec[curRadius];
		if (offsets.empty())
			continue;
    for (OffsetVec::const_iterator it = offsets.begin(); it != offsets.end(); ++it)
		{
			PartitionCell* thisCell = getCellAt(cellCenterX + it->x, cellCenterY + it->y);
			if (thisCell == NULL)
				continue;

			for (CellAndObjectIntersection *thisCoi = thisCell->getFirstCoiInCell(); thisCoi; thisCoi = thisCoi->getNextCoi())
			{
				PartitionData *thisMod = thisCoi->getModule();
				Object *thisObj = thisMod->getObject();
				++thePartitionQueryObjects;

				// never compare against ourself.
				if (thisObj == obj || thisObj == NULL)
					continue;

				// since an object can exist in multiple COIs, we use this to avoid processing
				// the same one more than once.
				if (thisMod->friend_getDoneFlag() == theIterFlag)
					continue;
				thisMod->friend_setDoneFlag(theIterFlag);
			
				Fix thisDistSqr;
				FCoord3D distVec;
				if (!(*distProc)(objPos, objToUse, thisObj->getPositionFix(), thisObj, thisDistSqr, distVec, closestDistSqr))
					continue;

				if (!filtersAllow(filters, thisObj))
					continue;

				// ok, this is within the range, and the filters allow it.
				// add it to the iter, if we have one....
				if (iterArg)
				{
					iterArg->insertFix(thisObj, thisDistSqr);
				}
				else
				{
					// hey, this is the new closest object! cool.
					// (note that we can't break out now 'cuz we have to finish examining the
					// rest of curRadius)
					closestObj = thisObj;
					closestDistSqr = thisDistSqr;
					closestVec = distVec;

					if (!foundAny)
					{
						// if not adding to iterArg, we want to stop once we have the closest object. 
						maxRadiusLimit = curRadius;
					}
					foundAny = true;
				}

			} // next coi
		}	// next cell in this radius
  } // next radius

#else // not FASTER_GCO
#error the ring walk without FASTER_GCO was never moved to fixed point; FASTER_GCO is always on
#endif  // not FASTER_GCO

	if (closestVecArg)
	{
		*closestVecArg = closestVec;
	}
	if (closestDistArg)
	{
		*closestDistArg = fixSqrt(closestDistSqr);
	}

#ifdef _DEBUG
	--theEntrancyCount;
#endif
#ifdef DUMP_PERF_STATS
	Int64 endTime64;
	GetPrecisionTimer(&endTime64);
	Int64 delta = (endTime64 - startTime64);
	s_timeInClosestObjects += delta;
	s_timeInClosestObjectsThisFrame += delta;
#endif

	return closestObj;	// might be null...
}


//-----------------------------------------------------------------------------
Object *PartitionManager::getClosestObjectFix(const Object *obj, Fix maxDist, DistanceCalculationType dc,
	PartitionFilter **filters, Fix *closestDist, FCoord3D *closestDistVec)
{
	return getClosestObjectsFix(obj, NULL, maxDist, dc, filters, NULL, closestDist, closestDistVec);
}

//-----------------------------------------------------------------------------
Object *PartitionManager::getClosestObjectFix(const FCoord3D *pos, Fix maxDist, DistanceCalculationType dc,
	PartitionFilter **filters, Fix *closestDist, FCoord3D *closestDistVec)
{
	return getClosestObjectsFix(NULL, pos, maxDist, dc, filters, NULL, closestDist, closestDistVec);
}

//-----------------------------------------------------------------------------
Fix PartitionManager::getDistanceSquaredFix(const Object *obj, const Object *otherObj, DistanceCalculationType dc, FCoord3D *vec)
{
	return getGoalDistanceSquaredFix(obj, obj->getPositionFix(), otherObj, dc, vec);
}

//-----------------------------------------------------------------------------
Fix PartitionManager::getDistanceSquaredFix(const Object *obj, const FCoord3D *pos, DistanceCalculationType dc, FCoord3D *vec)
{
	return getGoalDistanceSquaredFix(obj, obj->getPositionFix(), pos, dc, vec);
}

//-----------------------------------------------------------------------------
// Gets the distance if obj were at goalPos.  Used to calculate attack position paths.
Fix PartitionManager::getGoalDistanceSquaredFix(const Object *obj, const FCoord3D *goalPos, const Object *otherObj, DistanceCalculationType dc, FCoord3D *vec)
{
	Fix thisDistSqr;
	FCoord3D thisVec;
	(*theDistCalcProcs[dc])(goalPos, obj, otherObj->getPositionFix(), otherObj, thisDistSqr, thisVec, HUGE_DIST_SQR_FIX);
	if (vec)
		*vec = thisVec;
	return thisDistSqr;
}

//-----------------------------------------------------------------------------
Fix PartitionManager::getGoalDistanceSquaredFix(const Object *obj, const FCoord3D *goalPos, const FCoord3D *otherPos, DistanceCalculationType dc, FCoord3D *vec)
{
	Fix thisDistSqr;
	FCoord3D thisVec;
	(*theDistCalcProcs[dc])(goalPos, obj, otherPos, NULL, thisDistSqr, thisVec, HUGE_DIST_SQR_FIX);
	if (vec)
		*vec = thisVec;
	return thisDistSqr;
}

//-----------------------------------------------------------------------------
SimpleObjectIterator *PartitionManager::iterateObjectsInRangeFix(const Object *obj, Fix maxDist, DistanceCalculationType dc,
	PartitionFilter **filters, IterOrderType order)
{
	MemoryPoolObjectHolder iterHolder;
	SimpleObjectIterator *iter = newInstance(SimpleObjectIterator);
	iterHolder.hold(iter);

	getClosestObjectsFix(obj, NULL, maxDist, dc, filters, iter, NULL, NULL);

	iter->sort(order);
	iterHolder.release();
	return iter;
}

//-----------------------------------------------------------------------------
SimpleObjectIterator *PartitionManager::iterateObjectsInRangeFix(const FCoord3D *pos, Fix maxDist, DistanceCalculationType dc,
	PartitionFilter **filters, IterOrderType order)
{
	MemoryPoolObjectHolder iterHolder;
	SimpleObjectIterator *iter = newInstance(SimpleObjectIterator);
	iterHolder.hold(iter);

	getClosestObjectsFix(NULL, pos, maxDist, dc, filters, iter, NULL, NULL);

	iter->sort(order);
	iterHolder.release();
	return iter;
}

//-----------------------------------------------------------------------------
// P2 shims over the Fix queries above
//-----------------------------------------------------------------------------
static void fromFCoord3D(const FCoord3D &f, Coord3D *c)
{
	if (c)
		*c = f.toCoord3D();
}

//-----------------------------------------------------------------------------
Object *PartitionManager::getClosestObject(
	const Object *obj,
	Real maxDist,
	DistanceCalculationType dc,
	PartitionFilter **filters,
	Real *closestDist,
	Coord3D *closestDistVec
)
{
	Fix dist;
	FCoord3D vec;
	Object *found = getClosestObjectsFix(obj, NULL, fixFromReal(maxDist), dc, filters, NULL, &dist, &vec);
	if (closestDist)
		*closestDist = fixToReal(dist);
	fromFCoord3D(vec, closestDistVec);
	return found;
}

//-----------------------------------------------------------------------------
Object *PartitionManager::getClosestObject(
	const Coord3D *pos,
	Real maxDist,
	DistanceCalculationType dc,
	PartitionFilter **filters,
	Real *closestDist,
	Coord3D *closestDistVec
)
{
	Fix dist;
	FCoord3D vec;
	FCoord3D fpos = fcoordFromCoord3D(pos);
	Object *found = getClosestObjectsFix(NULL, &fpos, fixFromReal(maxDist), dc, filters, NULL, &dist, &vec);
	if (closestDist)
		*closestDist = fixToReal(dist);
	fromFCoord3D(vec, closestDistVec);
	return found;
}

//-----------------------------------------------------------------------------
void PartitionManager::getVectorToFix(const Object *obj, const Object *otherObj, DistanceCalculationType dc, FCoord3D& vec)
{
	getDistanceSquaredFix(obj, otherObj, dc, &vec);
}

//-----------------------------------------------------------------------------
void PartitionManager::getVectorToFix(const Object *obj, const FCoord3D *pos, DistanceCalculationType dc, FCoord3D& vec)
{
	getDistanceSquaredFix(obj, pos, dc, &vec);
}

//-----------------------------------------------------------------------------
void PartitionManager::getVectorTo(const Object *obj, const Object *otherObj, DistanceCalculationType dc, Coord3D& vec)
{
	FCoord3D fvec;
	getVectorToFix(obj, otherObj, dc, fvec);
	vec = fvec.toCoord3D();
}

//-----------------------------------------------------------------------------
void PartitionManager::getVectorTo(const Object *obj, const Coord3D *pos, DistanceCalculationType dc, Coord3D& vec)
{
	FCoord3D fvec;
	FCoord3D fpos = fcoordFromCoord3D(pos);
	getVectorToFix(obj, &fpos, dc, fvec);
	vec = fvec.toCoord3D();
}

//-----------------------------------------------------------------------------
Real PartitionManager::getDistanceSquared(const Object *obj, const Object *otherObj, DistanceCalculationType dc, Coord3D *vec)
{
	FCoord3D fvec;
	Fix d = getDistanceSquaredFix(obj, otherObj, dc, &fvec);
	fromFCoord3D(fvec, vec);
	return fixToReal(d);
}

//-----------------------------------------------------------------------------
Real PartitionManager::getDistanceSquared(const Object *obj, const Coord3D *pos, DistanceCalculationType dc, Coord3D *vec)
{
	FCoord3D fvec;
	FCoord3D fpos = fcoordFromCoord3D(pos);
	Fix d = getDistanceSquaredFix(obj, &fpos, dc, &fvec);
	fromFCoord3D(fvec, vec);
	return fixToReal(d);
}

//-----------------------------------------------------------------------------
Real PartitionManager::getGoalDistanceSquared(const Object *obj, const Coord3D *goalPos, const Object *otherObj, DistanceCalculationType dc, Coord3D *vec)
{
	FCoord3D fvec;
	FCoord3D fgoal = fcoordFromCoord3D(goalPos);
	Fix d = getGoalDistanceSquaredFix(obj, &fgoal, otherObj, dc, &fvec);
	fromFCoord3D(fvec, vec);
	return fixToReal(d);
}

//-----------------------------------------------------------------------------
// Gets the distance if obj were at goalPos.  Used to calculate attack position paths.
Real PartitionManager::getGoalDistanceSquared(const Object *obj, const Coord3D *goalPos, const Coord3D *otherPos, DistanceCalculationType dc, Coord3D *vec)
{
	FCoord3D fvec;
	FCoord3D fgoal = fcoordFromCoord3D(goalPos);
	FCoord3D fother = fcoordFromCoord3D(otherPos);
	Fix d = getGoalDistanceSquaredFix(obj, &fgoal, &fother, dc, &fvec);
	fromFCoord3D(fvec, vec);
	return fixToReal(d);
}

//-----------------------------------------------------------------------------
/* The signed angle from the object's heading to pos, in (-PI, PI].  EA took the acos of the dot of
	 the two unit vectors and signed it with their cross product; the atan2 of cross over dot is the
	 same angle, and needs neither the normalizing nor the clamp. */
Fix PartitionManager::getRelativeAngle2DFix( const Object *obj, const FCoord3D *pos )
{
	const FCoord3D *objPos = obj->getPositionFix();
	Fix vx = pos->x - objPos->x;
	Fix vy = pos->y - objPos->y;
	if (vx == Fix(0) && vy == Fix(0))
		return Fix(0);

	const FCoord3D *dir = obj->getUnitDirectionVector2DFix();
	Fix c = dir->x*vx + dir->y*vy;
	Fix perpZ = dir->x*vy - dir->y*vx;
	return fixAtan2( perpZ, c );
}

//-----------------------------------------------------------------------------
Fix PartitionManager::getRelativeAngle2DFix( const Object *obj, const Object *otherObj )
{
	return getRelativeAngle2DFix( obj, otherObj->getPositionFix() );
}

//-----------------------------------------------------------------------------
Real PartitionManager::getRelativeAngle2D( const Object *obj, const Coord3D *pos )
{
	FCoord3D fpos = fcoordFromCoord3D( pos );
	return fixToReal( getRelativeAngle2DFix( obj, &fpos ) );
}

//-----------------------------------------------------------------------------
SimpleObjectIterator *PartitionManager::iterateObjectsInRange(
	const Object *obj,
	Real maxDist,
	DistanceCalculationType dc,
	PartitionFilter **filters,
	IterOrderType order
)
{
	return iterateObjectsInRangeFix(obj, fixFromReal(maxDist), dc, filters, order);
}

//-----------------------------------------------------------------------------
SimpleObjectIterator *PartitionManager::iterateObjectsInRange(
	const Coord3D *pos,
	Real maxDist,
	DistanceCalculationType dc,
	PartitionFilter **filters,
	IterOrderType order
)
{
	FCoord3D fpos = fcoordFromCoord3D(pos);
	return iterateObjectsInRangeFix(&fpos, fixFromReal(maxDist), dc, filters, order);
}

//-----------------------------------------------------------------------------
SimpleObjectIterator* PartitionManager::iteratePotentialCollisionsFix(
	const FCoord3D* pos,
	const GeometryInfo& geom,
	Fix angle,
	Bool use2D
)
{
	Fix maxDist = geom.getBoundingSphereRadiusFix() * 1.1_fx;	// just a little slop

	MemoryPoolObjectHolder iterHolder;
	SimpleObjectIterator *iter = newInstance(SimpleObjectIterator);
	iterHolder.hold(iter);

	PartitionFilterWouldCollide filter(*pos, geom, angle, true);
	PartitionFilter *filters[] = { &filter, NULL };

	getClosestObjectsFix(NULL, pos, maxDist, use2D ? FROM_BOUNDINGSPHERE_2D : FROM_BOUNDINGSPHERE_3D, filters, iter, NULL, NULL);

	iterHolder.release();
	return iter;
}

//-----------------------------------------------------------------------------
SimpleObjectIterator* PartitionManager::iteratePotentialCollisions( const Coord3D* pos, const GeometryInfo& geom, Real angle, Bool use2D )
{
	FCoord3D fpos = fcoordFromCoord3D(pos);
	return iteratePotentialCollisionsFix(&fpos, geom, fixFromReal(angle), use2D);
}

//-----------------------------------------------------------------------------
Bool PartitionManager::isColliding( const Object *a, const Object *b ) const
{
	//Make sure we have objects
  if( !a || !b )
	{
    return false;
	}

	//Get both object's partition data
  const PartitionData* ad = a->friend_getConstPartitionData();
  const PartitionData* bd = b->friend_getConstPartitionData();

	//Make sure we got it
  if( !ad || !bd )
	{
    return false;
	}

	//See if the partition data collides.
	return ad->friend_collidesWith( bd, NULL );
}

//-----------------------------------------------------------------------------
SimpleObjectIterator *PartitionManager::iterateAllObjects(PartitionFilter **filters)
{
	MemoryPoolObjectHolder iterHolder;
	SimpleObjectIterator *iter = newInstance(SimpleObjectIterator);
	iterHolder.hold(iter);

	// no distance constraints; we're gonna have to process 'em all anyway,
	// so go thru in the fastest way we know how.
	PartitionData *mod, *nextMod;
	for( mod = m_moduleList; mod; mod = nextMod )
	{
		nextMod = mod->getNext();
		Object *obj = mod->getObject();
		if (obj && filtersAllow(filters, obj))
		{
			iter->insertFix( obj, Fix(0) );
		}
	}

	iterHolder.release();
	return iter;
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
Bool PartitionManager::tryPosition( const FCoord3D *center,
																		Fix dist,
																		Fix angle,
																		const FindPositionOptions *options,
																		FCoord3D *result )
{

	// compute the spot on the terrain we've picked
	FCoord3D fpos;
	fpos.x = dist * fixCos( angle ) + center->x;
	fpos.y = dist * fixSin( angle ) + center->y;

	PathfindLayerEnum layer = LAYER_GROUND;
	if ((options->flags & FPF_USE_HIGHEST_LAYER) != 0)
	{
		fpos.z = Fix( 99999 );
		Coord3D probe = fpos.toCoord3D();
		layer = TheTerrainLogic->getHighestLayerForDestination(&probe);
		fpos.z = TheTerrainLogic->getLayerHeightFix(fpos.x, fpos.y, layer);
		// ensure we are slightly above the bridge, to account for fudge & sloppy art
		if (layer != LAYER_GROUND)
			fpos.z += Fix( 1 );
	}
	else
	{
		fpos.z = TheTerrainLogic->getGroundHeightFix( fpos.x, fpos.y );
	}

	if (fixAbs(fpos.z - center->z) > options->maxZDelta)
		return FALSE;

	Coord3D pos = fpos.toCoord3D();	// for the cliff map and the pathfinder, which are float

	//
	// we don't usually find positions on cliffs.
	// someday, add bit options for this, like for water.
	//
	if (TheTerrainLogic->isCliffCell(pos.x, pos.y) && layer == LAYER_GROUND)
		return FALSE;

	//
	// srj sez:
	// we don't usually find positions on impassable areas.
	// someday, add bit options for this, like for water.
	//
	{
		Int cellX = PartitionManager_cellsIn(fpos.x, Fix(PATHFIND_CELL_SIZE), false);
		Int cellY = PartitionManager_cellsIn(fpos.y, Fix(PATHFIND_CELL_SIZE), false);
		PathfindCell* cell = TheAI->pathfinder()->getCell(layer, cellX, cellY);
		if (!cell || cell->getType() == PathfindCell::CELL_IMPASSABLE)
		{
			return FALSE;
		}
		if( BitTest(options->flags, FPF_CLEAR_CELLS_ONLY) && cell->getType() != PathfindCell::CELL_CLEAR )
			return FALSE;

	}

	//
	// we don't usually find positions in the water unless we explicitly say that's OK,
	// or if the option is set we can only pick position underwater
	//
	if( BitTest( options->flags, FPF_IGNORE_WATER ) == FALSE )
	{
		Bool isUnderwater = TheTerrainLogic->isUnderwaterFix( fpos.x, fpos.y );

		//
		// if we want water spots only and this is underwater it's no good, otherwise we want
		// the default behavior where underwater spots are invalid
		//
		if( BitTest( options->flags, FPF_WATER_ONLY ) && (isUnderwater == FALSE || layer != LAYER_GROUND) )
			return FALSE;
		else if( isUnderwater == TRUE && layer == LAYER_GROUND )
			return FALSE;

	}  // end if

	// object checks
	if( BitTest( options->flags, FPF_IGNORE_ALL_OBJECTS ) == FALSE )
	{

		//
		// iterate the potential collisions at this location using a 
		// very small sphere geometry around the point
		//
		GeometryInfo geometry( GEOMETRY_SPHERE, TRUE, Fix( 5 ), Fix( 5 ), Fix( 5 ) );
		ObjectIterator *iter = ThePartitionManager->iteratePotentialCollisionsFix( &fpos, geometry, angle, true );
		MemoryPoolObjectHolder hold( iter );
//	Bool overlap = FALSE;

		for( Object *them = iter->first(); them; them = iter->next() )
		{

			// if this is the ignore object, ignore it
			if( them == options->ignoreObject )
				continue;  // continue in the object iterator for loop

			// relationship checks
			if( options->relationshipObject )
			{

				// if this is an ally/neutral unit and we ignore those, do so
				if( BitTest( options->flags, FPF_IGNORE_ALLY_OR_NEUTRAL_UNITS ) == TRUE &&
						options->relationshipObject->getRelationship( them ) != ENEMIES &&
						(them->isKindOf( KINDOF_INFANTRY ) || them->isKindOf( KINDOF_VEHICLE )) )
					continue;

				// if this is an ally/neutral structure and we ignore those, do so
				if( BitTest( options->flags, FPF_IGNORE_ALLY_OR_NEUTRAL_STRUCTURES ) == TRUE &&
						options->relationshipObject->getRelationship( them ) != ENEMIES &&
						them->isKindOf( KINDOF_STRUCTURE ) )
					continue;

				// if this is an enemy unit and we ignore those, do so
				if( BitTest( options->flags, FPF_IGNORE_ENEMY_UNITS ) == TRUE &&
						options->relationshipObject->getRelationship( them ) == ENEMIES &&
						(them->isKindOf( KINDOF_INFANTRY ) || them->isKindOf( KINDOF_VEHICLE )) )
					continue;

				// if this is an enemy structure and we ignore those, do so
				if( BitTest( options->flags, FPF_IGNORE_ENEMY_STRUCTURES ) == TRUE &&
						options->relationshipObject->getRelationship( them ) == ENEMIES &&
						them->isKindOf( KINDOF_STRUCTURE ) )
					continue;

				// A friendly mine is no obstacle: nothing walks around it and it never goes off under us.
				// Its geometry is the 30-unit trigger circle, though, so a supply center with the mine
				// upgrade ringed itself with rejected spots and sent its trucks to dock beyond them.
				if( them->isKindOf( KINDOF_MINE ) &&
						options->relationshipObject->getRelationship( them ) == ALLIES )
					continue;

			}  // end if, relationship checks

			//
			// if we have a source that must path to the destination we will ignore that too
			// NOTE: If needed, we can reorganize the meaning of this method to be
			// more explicit on when to pay attention to any 'source' objects and when not too
			// but for now we're keeping the API very simple for the kinds of queries
			// that we're likely to do for an RTS (CBD)
			//
			if( them == options->sourceToPathToDest )
				continue;  // continue in the object iterator for loop

			// oops, we have overlapped something
			return FALSE;

		}  // end for, them

	}  // end if

	//
	// finally ... if sourceToPathDest is valid ... the source object must be able to
	// find a path the the position we have selected.  We want to do this last because
	// a pathfind can sometimes be an relatively expensive operation
	//
	if( options->sourceToPathToDest )
	{
		const AIUpdateInterface *ai = options->sourceToPathToDest->getAIUpdateInterface();

		// check for path existence
		Coord3D sourcePos = options->sourceToPathToDest->getPositionFix()->toCoord3D();	// P5: the pathfinder is float
		if( ai && TheAI->pathfinder()->clientSafeQuickDoesPathExist( ai->getLocomotorSet(),
																									&sourcePos,
																									&pos ) == FALSE )
				return FALSE;

	}  // end if

	// save result and return TRUE for position found
	*result = fpos;
	return TRUE;

}  // end tryPosition

//
// the following determines how fast we expand our concentric ring search for the
// find position methods
//
static const Fix ringSpacing = Fix( 5 );

//-------------------------------------------------------------------------------------------------
/** This method will attempt to find a legal postion from the center position specified,
	* at least minRadis away from it, but no more than maxRadius away.
	*
	* Return TRUE if position is found and that position is returned in 'result'
	* return FALSE no legal position exists or invalid params 
	*/
//-------------------------------------------------------------------------------------------------
Bool PartitionManager::findPositionAround( const FCoord3D *center,
																					 const FindPositionOptions *options,
																					 FCoord3D *result )
{

	// sanity
	if( center == NULL || result == NULL || options == NULL )
		return FALSE;

	Region3D extent;
	TheTerrainLogic->getMaximumPathfindExtent(&extent);
	FRegion3D fextent;	// the extent is the terrain's, in float
	fextent.lo = fcoordFromCoord3D(extent.lo);
	fextent.hi = fcoordFromCoord3D(extent.hi);
	// If the goal is off the map, it is a scripted setup, so just
	// use the center.
	if (!fextent.isInRegionNoZ(*center)) {
		*result = *center;
		return true;
	}
	// sanity, FPF_IGNORE_WATER and FPF_WATER_ONLY are mutually exclusive
	DEBUG_ASSERTCRASH( !(BitTest( options->flags, FPF_IGNORE_WATER ) == TRUE &&
										   BitTest( options->flags, FPF_WATER_ONLY ) == TRUE),
										 ("PartitionManager::findPositionAround - The options FPF_WATER_ONLY and FPF_IGNORE_WATER are mutually exclusive.  You cannot use them together\n") );

	// pick a random angle from the center location to start at
	Fix startAngle;
	if( options->startAngle == RANDOM_START_ANGLE )
		startAngle = fixFromReal( GameLogicRandomValueReal( 0.0f, TWO_PI ) );
	else
		startAngle = options->startAngle;

	// start the search at the most inner ring (minRadius) and expand to outer most ring (maxRadius)
	for( Fix dist = options->minRadius; dist <= options->maxRadius; dist += ringSpacing )
	{

		//
		// given the spacing that we've been using for the angles on the 'idea' inner circle,
		// we will need more points on larger circles to cover more ground
		//
		Fix angleSpacing;
		if( dist == options->minRadius )
			angleSpacing = FIX_TWO_PI;
		else
			angleSpacing = (ringSpacing / (dist + Fix(1))) * (FIX_TWO_PI / Fix(6));  // larger float = more samples

		//
		// on this "ring", try all the angles available to us in a circle ... we'll start at
		// the 'startAngle', then try 'angleSpacing' to the left, then 'angleSpacing' to the
		// right, then '2 angleSpacings' to the left etc., ping ponging around the start angle
		// until we've done them all the way around the circle
		//

		// how many samples will we test
		Int samples = PartitionManager_cellsIn( FIX_TWO_PI / angleSpacing, Fix(2), true );
		for( Int i = 0; i < samples; ++i )
		{

			// try one "side"
			if( tryPosition( center, dist, startAngle + angleSpacing * Fix(i), options, result ) == TRUE )
				return TRUE;

			//
			// try the other "side" ... note for the first point at minRadius this position is
			// the same so we won't test it again in that case
			//
			if( i != 0 )
				if( tryPosition( center, dist, startAngle - angleSpacing * Fix(i), options, result ) == TRUE )
					return TRUE;	

		}  // end if

	}  // end for, dist

	// no position was able to be found
	return FALSE;

}  // end findPositionAround

//-----------------------------------------------------------------------------
// This is the main accessor of the shroud system.  At this level, allies are taken
// into consideration as specified by the caller.  Look/Unlook are the ones sending Ally info, as that
// is in Object where Allies make sense.  AddLooker literally just adds a looker for the player you specify.
// This way, Full map reveals and Observer mode active look will not carry over to all 
// allies.  They'll use the RevealWholeDamnMap series, which call addLooker directly.
void PartitionManager::doShroudReveal(Fix centerX, Fix centerY, Fix radius, PlayerMaskType playerMask) 
{
	Int cellCenterX, cellCenterY;
	ThePartitionManager->worldToCellFix(centerX, centerY, &cellCenterX, &cellCenterY);

	Int cellRadius = ThePartitionManager->worldToCellDist(radius);
	if (cellRadius < 1) 
		cellRadius = 1;

	DiscreteCircle circle(cellCenterX, cellCenterY, cellRadius);

	for( Int currentIndex = ThePlayerList->getPlayerCount() - 1; currentIndex >=0; currentIndex-- )
	{
		// Object's Look is the one who knows about allies.  Anyone can pask a player mask to me and all
		// of those players will have an active looker applied to a bunch of cells
		const Player *currentPlayer = ThePlayerList->getNthPlayer( currentIndex );
		if( BitTest( playerMask, currentPlayer->getPlayerMask() ) )
		{
			circle.drawCircle(hLineAddLooker, (void*)currentIndex);
		}
	}
}
	
//-----------------------------------------------------------------------------
void PartitionManager::processPendingUndoShroudRevealQueue( Bool considerTimestamp )
{
	//Keep going until the front one is in the future.  UndoShroudReveal on each one you process.
	//Again, you know these are in order, because we control the adding of the Constant in-the-queue time.
	UnsignedInt compareTime;
	if(considerTimestamp)
		compareTime = TheGameLogic->getFrame();
	else
		compareTime = UINT_MAX;

	while( !m_pendingUndoShroudReveals.empty() && (m_pendingUndoShroudReveals.front()->m_data < compareTime) )
	{
		SightingInfo *thisInfo = m_pendingUndoShroudReveals.front();

		if( !thisInfo->m_revealedCells.empty() )
			applyRecordedReveal( thisInfo, FALSE );
		else
			undoShroudReveal( thisInfo->m_where.x, thisInfo->m_where.y, thisInfo->m_howFar, thisInfo->m_forWhom );

		thisInfo->deleteInstance();
		m_pendingUndoShroudReveals.pop();
	}
}

//-----------------------------------------------------------------------------
void PartitionManager::processEntirePendingUndoShroudRevealQueue()
{
	// Something major is about to happen, so we need to rush through our queue and get everything in order.
	// Examples are map resize, full map undo-reveal, stuff like that.

	// Don't like duplicating code, so route to original function.
	processPendingUndoShroudRevealQueue(FALSE);
}

//-----------------------------------------------------------------------------
void PartitionManager::resetPendingUndoShroudRevealQueue()
{
	while( !m_pendingUndoShroudReveals.empty() )
	{
		SightingInfo *thisInfo = m_pendingUndoShroudReveals.front();
		thisInfo->deleteInstance();
		m_pendingUndoShroudReveals.pop();
	}
}

//-----------------------------------------------------------------------------
void PartitionManager::undoShroudReveal(Fix centerX, Fix centerY, Fix radius, PlayerMaskType playerMask) 
{
	Int cellCenterX, cellCenterY;
	ThePartitionManager->worldToCellFix(centerX, centerY, &cellCenterX, &cellCenterY);

	Int cellRadius = ThePartitionManager->worldToCellDist(radius);
	if (cellRadius < 1) 
		cellRadius = 1;

	DiscreteCircle circle(cellCenterX, cellCenterY, cellRadius);

	for( Int currentIndex = ThePlayerList->getPlayerCount() - 1; currentIndex >=0; currentIndex-- )
	{
		const Player *currentPlayer = ThePlayerList->getNthPlayer( currentIndex );
		if( BitTest( playerMask, currentPlayer->getPlayerMask() ) )
		{
			circle.drawCircle(hLineRemoveLooker, (void*)currentIndex);
		}
	}
}
	
//-----------------------------------------------------------------------------
void PartitionManager::queueUndoShroudReveal( SightingInfo *sighting )
{
	UnsignedInt now = TheGameLogic->getFrame();
	SightingInfo *newInfo = newInstance(SightingInfo);

	newInfo->m_where = sighting->m_where;
	newInfo->m_howFar = sighting->m_howFar;
	newInfo->m_forWhom = sighting->m_forWhom;
	newInfo->m_revealedCells.swap( sighting->m_revealedCells );
	newInfo->m_data = now + TheGlobalData->m_unlookPersistDuration;

	m_pendingUndoShroudReveals.push(newInfo);
}

//-----------------------------------------------------------------------------
static Real slopeFromEye( Real height, Int cellOffsetX, Int cellOffsetY, Real cellSize, Real eyeZ )
{
	return ( height - eyeZ ) / ( sqrtf( (Real)( cellOffsetX * cellOffsetX + cellOffsetY * cellOffsetY ) ) * cellSize );
}

//-----------------------------------------------------------------------------
/** minor * (major - 1) / major, rounded half away from zero: where a line from the middle through
	(major, minor) crosses the ring one step further in. */
static Int scaleOneRingIn( Int minor, Int major )
{
	const Int halfStep = minor < 0 ? -major : major;
	return ( 2 * minor * ( major - 1 ) + halfStep ) / ( 2 * major );
}

//-----------------------------------------------------------------------------
/** One pass, ring by ring outwards. Each cell takes the steepest slope met on the way in from its
	neighbour one step closer to the middle, together with that neighbour's own slope, so a ridge
	keeps hiding everything behind it however far the circle reaches. The middle cell is where the
	eye stands and hides nothing. */
void PartitionManager_findSightMargins( const Real *heights, Int cellRadius, Real cellSize, Real eyeZ,
																				Real *sightMargin )
{
	const Int side = 2 * cellRadius + 1;
	const Int middle = cellRadius * side + cellRadius;
	static std::vector<Real> steepestOnTheWay;
	steepestOnTheWay.resize( side * side );

	steepestOnTheWay[ middle ] = -FLT_MAX;
	sightMargin[ middle ] = 0.0f;

	for( Int ring = 1; ring <= cellRadius; ++ring )
	{
		for( Int offsetY = -ring; offsetY <= ring; ++offsetY )
		{
			const Int stepX = ( abs( offsetY ) == ring ) ? 1 : 2 * ring;
			for( Int offsetX = -ring; offsetX <= ring; offsetX += stepX )
			{
				Int innerX, innerY;
				if( abs( offsetX ) >= abs( offsetY ) )
				{
					innerX = offsetX - ( offsetX < 0 ? -1 : 1 );
					innerY = scaleOneRingIn( offsetY, abs( offsetX ) );
				}
				else
				{
					innerY = offsetY - ( offsetY < 0 ? -1 : 1 );
					innerX = scaleOneRingIn( offsetX, abs( offsetY ) );
				}

				const Int inner = ( innerY + cellRadius ) * side + innerX + cellRadius;
				const Int here = ( offsetY + cellRadius ) * side + offsetX + cellRadius;

				Real steepest = steepestOnTheWay[ inner ];
				if( inner != middle )
					steepest = max( steepest, slopeFromEye( heights[ inner ], innerX, innerY, cellSize, eyeZ ) );

				steepestOnTheWay[ here ] = steepest;
				sightMargin[ here ] = slopeFromEye( heights[ here ], offsetX, offsetY, cellSize, eyeZ ) - steepest;
			}
		}
	}
}

//-----------------------------------------------------------------------------
struct RecordedLookers
{
	Int m_cornerX;		///< cell coordinates of the first entry of m_revealedCells
	Int m_cornerY;
	Int m_side;
	const UnsignedByte *m_revealedCells;
	Int m_playerIndex;
	Bool m_reveal;		///< add the looker, or take it away again
};

//-----------------------------------------------------------------------------
static void hLineRecordedLooker( Int x1, Int x2, Int y, void *lookersVoid )
{
	const RecordedLookers *lookers = (const RecordedLookers *)lookersVoid;
	if( y < 0 || y >= ThePartitionManager->getCellCountY() )
		return;

	const UnsignedByte *revealed = lookers->m_revealedCells + ( y - lookers->m_cornerY ) * lookers->m_side - lookers->m_cornerX;
	for( Int x = max( x1, 0 ); x <= min( x2, ThePartitionManager->getCellCountX() - 1 ); ++x )
	{
		if( !revealed[ x ] )
			continue;

		PartitionCell *cell = ThePartitionManager->getCellAt( x, y );
		if( lookers->m_reveal )
			cell->addLooker( lookers->m_playerIndex );
		else
			cell->removeLooker( lookers->m_playerIndex );
	}
}

//-----------------------------------------------------------------------------
static Int sightCellRadius( Fix radius )
{
	return max( ThePartitionManager->worldToCellDist( radius ), 1 );
}

//-----------------------------------------------------------------------------
void PartitionManager::applyRecordedReveal( const SightingInfo *sighting, Bool reveal )
{
	Int cellCenterX, cellCenterY;
	worldToCellFix( sighting->m_where.x, sighting->m_where.y, &cellCenterX, &cellCenterY );
	const Int cellRadius = sightCellRadius( sighting->m_howFar );

	RecordedLookers lookers;
	lookers.m_cornerX = cellCenterX - cellRadius;
	lookers.m_cornerY = cellCenterY - cellRadius;
	lookers.m_side = 2 * cellRadius + 1;
	lookers.m_revealedCells = &sighting->m_revealedCells[0];
	lookers.m_reveal = reveal;

	DiscreteCircle circle( cellCenterX, cellCenterY, cellRadius );
	for( Int currentIndex = ThePlayerList->getPlayerCount() - 1; currentIndex >= 0; currentIndex-- )
	{
		if( BitTest( sighting->m_forWhom, ThePlayerList->getNthPlayer( currentIndex )->getPlayerMask() ) )
		{
			lookers.m_playerIndex = currentIndex;
			circle.drawCircle( hLineRecordedLooker, &lookers );
		}
	}
}

//-----------------------------------------------------------------------------
/** The highest standing bridge deck over a cell, or the cell's ground where no deck crosses it. The
	deck is ground to walk on and to see: judged by the valley floor under it, a bridge over a gorge
	hid behind the gorge's own edge. Sampled on a 3x3 grid, since a deck is not much wider than a
	cell and can miss the middle of one it crosses. */
static Real sightGroundHeight( PartitionCell *cell )
{
	// P9: the sight margins and the bridges are float
	Real height = fixToReal( cell->getHiTerrain() );
	Fix fxCenterX, fxCenterY;
	cell->getCellCenterPos( fxCenterX, fxCenterY );
	const Real centerX = fixToReal( fxCenterX );
	const Real centerY = fixToReal( fxCenterY );
	const Real halfCell = 0.5f * ThePartitionManager->getCellSize();

	for( Bridge *bridge = TheTerrainLogic->getFirstBridge(); bridge; bridge = bridge->getNext() )
	{
		if( bridge->peekBridgeInfo()->curDamageState == BODY_RUBBLE )
			continue;

		for( Int sampleY = -1; sampleY <= 1; ++sampleY )
		{
			for( Int sampleX = -1; sampleX <= 1; ++sampleX )
			{
				Coord3D sample;
				sample.set( centerX + sampleX * halfCell, centerY + sampleY * halfCell, 0.0f );
				if( bridge->isPointOnBridge( &sample ) )
					height = max( height, bridge->getBridgeHeight( &sample, NULL ) );
			}
		}
	}
	return height;
}

//-----------------------------------------------------------------------------
/** The top of whatever stands in a cell: the highest ground or bridge deck in it, or the roof of a
	building on it if that is higher. Trees and bushes that are objects count; the bridge object
	itself does not, its deck is the ground. A planned building nobody has started is not there yet. */
static Real sightBlockingHeight( PartitionCell *cell, const Object *looker, const Object *lookerContainer )
{
	Real height = sightGroundHeight( cell );
	for( CellAndObjectIntersection *coi = cell->getFirstCoiInCell(); coi; coi = coi->getNextCoi() )
	{
		// a ghost object has no Object behind it
		const Object *obj = coi->getModule()->getObject();
		if( obj == NULL || obj == looker || obj == lookerContainer )
			continue;

		if( !obj->isKindOf( KINDOF_STRUCTURE ) && !obj->isKindOf( KINDOF_SHRUBBERY ) )
			continue;

		if( obj->isKindOf( KINDOF_BRIDGE ) )
			continue;

		if( Object_isAwaitingBuilder( obj->testStatus( OBJECT_STATUS_UNDER_CONSTRUCTION ), obj->getConstructionPercent() ) )
			continue;

		// P9: the shroud's heights are float, like the cell cache they start from
		height = max( height, fixToReal( obj->getPositionFix()->z + obj->getGeometryInfo().getMaxHeightAbovePositionFix() ) );
	}
	return height;
}

//-----------------------------------------------------------------------------
void PartitionManager::doBlockedShroudReveal( SightingInfo *sighting, const Object *looker, Fix flatRange )
{
	Int cellCenterX, cellCenterY;
	worldToCellFix( sighting->m_where.x, sighting->m_where.y, &cellCenterX, &cellCenterY );
	const Int cellRadius = sightCellRadius( sighting->m_howFar );
	const Object *lookerContainer = looker->getContainedBy();

	const Int side = 2 * cellRadius + 1;
	static std::vector<Real> heights;
	static std::vector<Real> sightMargin;
	heights.resize( side * side );
	sightMargin.resize( side * side );

	for( Int offsetY = -cellRadius; offsetY <= cellRadius; ++offsetY )
	{
		const Int cellY = min( max( cellCenterY + offsetY, 0 ), m_cellCountY - 1 );
		for( Int offsetX = -cellRadius; offsetX <= cellRadius; ++offsetX )
		{
			const Int cellX = min( max( cellCenterX + offsetX, 0 ), m_cellCountX - 1 );
			heights[ ( offsetY + cellRadius ) * side + offsetX + cellRadius ] =
				sightBlockingHeight( getCellAt( cellX, cellY ), looker, lookerContainer );
		}
	}

	// P9: the sight margins are float
	PartitionManager_findSightMargins( &heights[0], cellRadius, m_cellSize, fixToReal( sighting->m_where.z ), &sightMargin[0] );

	// r * ( r + 1 ) rather than r squared: the circle the reveal is drawn with is a Bresenham one,
	// and on flat ground this has to keep every cell of it
	const Bool stretched = sighting->m_howFar > flatRange;
	sighting->m_revealedCells.resize( side * side );
	for( Int cell = 0; cell < side * side; ++cell )
	{
		Bool revealed = sightMargin[ cell ] >= 0.0f;
		if( revealed && stretched )
		{
			const Int offsetX = cell % side - cellRadius;
			const Int offsetY = cell / side - cellRadius;
			// P6: Weapon_elevatedRange is float
			const Int reachCells = sightCellRadius( fixFromReal( Weapon_elevatedRange( looker, fixToReal( flatRange ), heights[ cell ] ) ) );
			revealed = offsetX * offsetX + offsetY * offsetY <= reachCells * ( reachCells + 1 );
		}
		sighting->m_revealedCells[ cell ] = revealed;
	}

	applyRecordedReveal( sighting, TRUE );
}
	
//-----------------------------------------------------------------------------
void PartitionManager::doShroudCover(Fix centerX, Fix centerY, Fix radius, PlayerMaskType playerMask) 
{
	Int cellCenterX, cellCenterY;
	ThePartitionManager->worldToCellFix(centerX, centerY, &cellCenterX, &cellCenterY);

	Int cellRadius = ThePartitionManager->worldToCellDist(radius);
	if (cellRadius < 1) 
		cellRadius = 1;

	DiscreteCircle circle(cellCenterX, cellCenterY, cellRadius);

	for( Int currentIndex = ThePlayerList->getPlayerCount() - 1; currentIndex >=0; currentIndex-- )
	{
		// Object's Shroud is the one who knows about allies.  Anyone can pask a player mask to me and all
		// of those players will have an active shrouder applied to a bunch of cells
		const Player *currentPlayer = ThePlayerList->getNthPlayer( currentIndex );
		if( BitTest( playerMask, currentPlayer->getPlayerMask() ) )
		{
			circle.drawCircle(hLineAddShrouder, (void*)currentIndex);
		}
	}
}

//-----------------------------------------------------------------------------
void PartitionManager::undoShroudCover(Fix centerX, Fix centerY, Fix radius, PlayerMaskType playerMask) 
{
	Int cellCenterX, cellCenterY;
	ThePartitionManager->worldToCellFix(centerX, centerY, &cellCenterX, &cellCenterY);

	Int cellRadius = ThePartitionManager->worldToCellDist(radius);
	if (cellRadius < 1) 
		cellRadius = 1;

	DiscreteCircle circle(cellCenterX, cellCenterY, cellRadius);

	for( Int currentIndex = ThePlayerList->getPlayerCount() - 1; currentIndex >=0; currentIndex-- )
	{
		const Player *currentPlayer = ThePlayerList->getNthPlayer( currentIndex );
		if( BitTest( playerMask, currentPlayer->getPlayerMask() ) )
		{
			circle.drawCircle(hLineRemoveShrouder, (void*)currentIndex);
		}
	}
}

//-----------------------------------------------------------------------------
void PartitionManager::doThreatAffect( Fix centerX, Fix centerY, Fix radius, UnsignedInt threatVal, PlayerMaskType playerMask)
{
	Int cellCenterX, cellCenterY;
	ThePartitionManager->worldToCellFix(centerX, centerY, &cellCenterX, &cellCenterY);
	Real fCellCenterX = INT_TO_REAL(cellCenterX);
	Real fCellCenterY = INT_TO_REAL(cellCenterY);

	Int cellRadius = ThePartitionManager->worldToCellDist(radius);
	if (cellRadius < 1) 
		cellRadius = 1;

	Real fCellRadius = INT_TO_REAL(cellRadius + 1);

	DiscreteCircle circle(cellCenterX, cellCenterY, cellRadius);

	ThreatValueParms parms;
	parms.radius = fCellRadius;
	parms.threatOrValue = threatVal;
	parms.xCenter = fCellCenterX;
	parms.yCenter = fCellCenterY;

	for( Int currentIndex = ThePlayerList->getPlayerCount() - 1; currentIndex >=0; currentIndex-- )
	{
		const Player *currentPlayer = ThePlayerList->getNthPlayer( currentIndex );
		if( BitTest( playerMask, currentPlayer->getPlayerMask() ) )
		{
			parms.playerIndex = currentIndex;
			circle.drawCircle(hLineAddThreat, &parms);
		}
	}
}

//-----------------------------------------------------------------------------
void PartitionManager::undoThreatAffect( Fix centerX, Fix centerY, Fix radius, UnsignedInt threatVal, PlayerMaskType playerMask)
{
	Int cellCenterX, cellCenterY;
	ThePartitionManager->worldToCellFix(centerX, centerY, &cellCenterX, &cellCenterY);
	Real fCellCenterX = INT_TO_REAL(cellCenterX);
	Real fCellCenterY = INT_TO_REAL(cellCenterY);

	Int cellRadius = ThePartitionManager->worldToCellDist(radius);
	if (cellRadius < 1) 
		cellRadius = 1;

	Real fCellRadius = INT_TO_REAL(cellRadius + 1);

	DiscreteCircle circle(cellCenterX, cellCenterY, cellRadius);

	ThreatValueParms parms;
	parms.radius = fCellRadius;
	parms.threatOrValue = threatVal;
	parms.xCenter = fCellCenterX;
	parms.yCenter = fCellCenterY;

	for( Int currentIndex = ThePlayerList->getPlayerCount() - 1; currentIndex >=0; currentIndex-- )
	{
		const Player *currentPlayer = ThePlayerList->getNthPlayer( currentIndex );
		if( BitTest( playerMask, currentPlayer->getPlayerMask() ) )
		{
			parms.playerIndex = currentIndex;
			circle.drawCircle(hLineRemoveThreat, &parms);
		}
	}
}

//-----------------------------------------------------------------------------
void PartitionManager::doValueAffect( Fix centerX, Fix centerY, Fix radius, UnsignedInt valueVal, PlayerMaskType playerMask)
{
	Int cellCenterX, cellCenterY;
	ThePartitionManager->worldToCellFix(centerX, centerY, &cellCenterX, &cellCenterY);
	Real fCellCenterX = INT_TO_REAL(cellCenterX);
	Real fCellCenterY = INT_TO_REAL(cellCenterY);

	Int cellRadius = ThePartitionManager->worldToCellDist(radius);
	if (cellRadius < 1) 
		cellRadius = 1;

	Real fCellRadius = INT_TO_REAL(cellRadius + 1);

	DiscreteCircle circle(cellCenterX, cellCenterY, cellRadius);

	ThreatValueParms parms;
	parms.radius = fCellRadius;
	parms.threatOrValue = valueVal;
	parms.xCenter = fCellCenterX;
	parms.yCenter = fCellCenterY;

	for( Int currentIndex = ThePlayerList->getPlayerCount() - 1; currentIndex >=0; currentIndex-- )
	{
		const Player *currentPlayer = ThePlayerList->getNthPlayer( currentIndex );
		if( BitTest( playerMask, currentPlayer->getPlayerMask() ) )
		{
			parms.playerIndex = currentIndex;
			circle.drawCircle(hLineAddValue, &parms);
		}
	}
}

//-----------------------------------------------------------------------------
void PartitionManager::undoValueAffect( Fix centerX, Fix centerY, Fix radius, UnsignedInt valueVal, PlayerMaskType playerMask)
{
	Int cellCenterX, cellCenterY;
	ThePartitionManager->worldToCellFix(centerX, centerY, &cellCenterX, &cellCenterY);
	Real fCellCenterX = INT_TO_REAL(cellCenterX);
	Real fCellCenterY = INT_TO_REAL(cellCenterY);

	Int cellRadius = ThePartitionManager->worldToCellDist(radius);
	if (cellRadius < 1) 
		cellRadius = 1;

	Real fCellRadius = INT_TO_REAL(cellRadius + 1);

	DiscreteCircle circle(cellCenterX, cellCenterY, cellRadius);

	ThreatValueParms parms;
	parms.radius = fCellRadius;
	parms.threatOrValue = valueVal;
	parms.xCenter = fCellCenterX;
	parms.yCenter = fCellCenterY;
	
	for( Int currentIndex = ThePlayerList->getPlayerCount() - 1; currentIndex >=0; currentIndex-- )
	{
		const Player *currentPlayer = ThePlayerList->getNthPlayer( currentIndex );
		if( BitTest( playerMask, currentPlayer->getPlayerMask() ) )
		{
			parms.playerIndex = currentIndex;
			circle.drawCircle(hLineRemoveValue, &parms);
		}
	}
}

//-----------------------------------------------------------------------------
void PartitionManager::worldToCell(Real wx, Real wy, Int *cx, Int *cy)
{
	worldToCellFix(fixFromReal(wx), fixFromReal(wy), cx, cy);
}

//-----------------------------------------------------------------------------
void PartitionManager::getCellCenterPos(Int x, Int y, Fix& xx, Fix& yy)
{
	DEBUG_ASSERTCRASH(x >= 0 && y >= 0, ("hmm, invalid cell"));
	Fix half = m_cellSizeFix*0.5_fx;
	xx = m_worldLoFix.x + (Fix(x) * m_cellSizeFix) + half;
	yy = m_worldLoFix.y + (Fix(y) * m_cellSizeFix) + half;
}

//-----------------------------------------------------------------------------
#ifdef PM_CACHE_TERRAIN_HEIGHT

	struct TerrainExtremeData
	{
		Fix minZ;
		Fix maxZ;
		FCoord2D minZPos;
		FCoord2D maxZPos;
		Bool isValid;
	};

static Int checkTerrainExtreme(PartitionCell* cell, void* userData)
{
	TerrainExtremeData* data = (TerrainExtremeData*)userData;

	data->isValid = true;

	if (cell->getLoTerrain() < data->minZ)
	{
		data->minZ = cell->getLoTerrain();
		cell->getCellCenterPos(data->minZPos.x, data->minZPos.y);
	}

	if (cell->getHiTerrain() > data->maxZ)
	{
		data->maxZ = cell->getHiTerrain();
		cell->getCellCenterPos(data->maxZPos.x, data->maxZPos.y);
	}

	return 0;	// zero to continue
}
#endif

//-----------------------------------------------------------------------------
#ifdef PM_CACHE_TERRAIN_HEIGHT
Bool PartitionManager::estimateTerrainExtremesAlongLine(const Coord3D& pos, const Coord3D& posOther, Real* minZ, Real* maxZ, Coord2D* minZPos, Coord2D* maxZPos)
{
	TerrainExtremeData data;
	data.minZ = HUGE_DIST_FIX;
	data.maxZ = -HUGE_DIST_FIX;
	data.minZPos.zero();
	data.maxZPos.zero();
	data.isValid = false;
	iterateCellsAlongLine(fcoordFromCoord3D(pos), fcoordFromCoord3D(posOther), checkTerrainExtreme, &data);
	// P6: this is float for its projectile caller
	if (minZ) *minZ = fixToReal(data.minZ);
	if (maxZ) *maxZ = fixToReal(data.maxZ);
	if (minZPos && data.minZ < HUGE_DIST_FIX) *minZPos = data.minZPos.toCoord2D();
	if (maxZPos && data.maxZ > -HUGE_DIST_FIX) *maxZPos = data.maxZPos.toCoord2D();
	return data.isValid;
}
#endif

//-----------------------------------------------------------------------------
// Uses Bresenham line algorithm from www.gamedev.net.
Int PartitionManager::iterateCellsAlongLine(const FCoord3D& pos, const FCoord3D& posOther, CellAlongLineProc proc, void* userData)
{
	ICoord2D start, end, delta;
	Int x, y;
	Int xinc1, xinc2;
	Int yinc1, yinc2;
	Int den, num, numadd;
	Int numpixels;

	worldToCellFix(pos.x, pos.y, &start.x, &start.y);
	worldToCellFix(posOther.x, posOther.y, &end.x, &end.y);

	delta.x = abs(end.x - start.x);			// The difference between the x's
	delta.y = abs(end.y - start.y);			// The difference between the y's
	x = start.x;												// Start x off at the first pixel
	y = start.y;												// Start y off at the first pixel

	if (end.x >= start.x)								// The x-values are increasing
	{
		xinc1 = 1;
		xinc2 = 1;
	}
	else																// The x-values are decreasing
	{
		xinc1 = -1;
		xinc2 = -1;
	}

	if (end.y >= start.y)               // The y-values are increasing
	{
		yinc1 = 1;
		yinc2 = 1;
	}
	else																// The y-values are decreasing
	{
		yinc1 = -1;
		yinc2 = -1;
	}
	Bool checkY = true;
	if (delta.x >= delta.y)							// There is at least one x-value for every y-value
	{
		xinc1 = 0;												// Don't change the x when numerator >= denominator
		yinc2 = 0;												// Don't change the y for every iteration
		den = delta.x;
		num = delta.x / 2;
		numadd = delta.y;
		numpixels = delta.x;							// There are more x-values than y-values
	}
	else																// There is at least one y-value for every x-value
	{
		checkY = false;
		xinc2 = 0;												// Don't change the x for every iteration
		yinc1 = 0;												// Don't change the y when numerator >= denominator
		den = delta.y;
		num = delta.y / 2;
		numadd = delta.x;
		numpixels = delta.y;							// There are more y-values than x-values
	}

	for (Int curpixel = 0; curpixel <= numpixels; curpixel++)
	{
		PartitionCell* cell = ThePartitionManager->getCellAt(x, y);	// might be null if off the edge
		DEBUG_ASSERTCRASH(cell != NULL, ("off the map"));
		if (cell)
		{
			Int ret = (*proc)(cell, userData);
			if (ret != 0)
				return ret;	// bail early
		}

		num += numadd;										// Increase the numerator by the top of the fraction
		if (num >= den)										// Check if numerator >= denominator
		{
			num -= den;											// Calculate the new numerator value
			x += xinc1;											// Change the x as appropriate
			y += yinc1;											// Change the y as appropriate
		}
		x += xinc2;												// Change the x as appropriate
		y += yinc2;												// Change the y as appropriate
	}
	
	return 0;
}

//-----------------------------------------------------------------------------
Int PartitionManager::iterateCellsBreadthFirst(const FCoord3D *pos, CellBreadthFirstProc proc, void *userData)
{
	// starting at pos, iterate the cells in the following manner:
	// left, up, right, down
	// using Breadth-first search.
	// Call proc on each cell, and if it returns non-zero, then return the cell index
	// -1 means error, but we should add a define later for this.

	Int cellX, cellY;
	ThePartitionManager->worldToCellFix(pos->x, pos->y, &cellX, &cellY);
	
	// Note, bool. not Bool, cause bool will cause this to be a bitfield.
	std::vector<bool> bitField;
	Int cellCount = m_cellCountX * m_cellCountY;
	bitField.resize(cellCount);
	// 0 means not done, 1 means done.

	for (Int i = 0; i < cellCount; ++i) {
		bitField[i] = false;
	}

	std::queue<PartitionCell *> cellQ;

	// mark the starting cell done
	bitField[cellY * m_cellCountX + cellX] = true;
	cellQ.push(&m_cells[cellY * m_cellCountX + cellX]);

	Int curX = cellX;
	Int curY = cellY;
	while (!cellQ.empty()) {
		// first, add the neighbors
		// left
		if (curX - 1 >= 0) {
			if (!bitField[curY * m_cellCountX + curX - 1]) {
				bitField[curY * m_cellCountX + curX - 1] = true;
				cellQ.push(&m_cells[curY * m_cellCountX + curX - 1]);
			}
		}

		// top
		if (curY - 1 >= 0) {
			if (!bitField[(curY - 1) * m_cellCountX + curX]) {
				bitField[(curY - 1) * m_cellCountX + curX] = true;
				cellQ.push(&m_cells[(curY - 1) * m_cellCountX + curX]);
			}
		}

		// right
		if (curX + 1 < m_cellCountX) {
			if (!bitField[curY * m_cellCountX + curX + 1]) {
				bitField[curY * m_cellCountX + curX + 1] = true;
				cellQ.push(&m_cells[curY * m_cellCountX + curX + 1]);
			}
		}

		// bottom
		if (curY + 1 > 0) {
			if (!bitField[(curY + 1) * m_cellCountX + curX]) {
				bitField[(curY + 1) * m_cellCountX + curX] = true;
				cellQ.push(&m_cells[(curY + 1) * m_cellCountX + curX]);
			}
		}

		// now process the current top.
		PartitionCell *curCell = cellQ.front();
		cellQ.pop();
		curX = curCell->getCellX();
		curY = curCell->getCellY();

		if ((proc)(curCell, userData) != 0)
			return (curCell->getCellY() * m_cellCountX + curCell->getCellX());
	}

	return -1;
}

//-----------------------------------------------------------------------------
static Real calcDist2D(Real x1, Real y1, Real x2, Real y2)
{
	return sqrtf(sqr(x1-x2) + sqr(y1-y2));
}

//-----------------------------------------------------------------------------
Bool PartitionManager::isClearLineOfSightTerrain(const Object* obj, const Coord3D& objPos, const Object* other, const Coord3D& otherPos)
{
	return isClearLineOfSightTerrainFix(obj, fcoordFromCoord3D(objPos), other, fcoordFromCoord3D(otherPos));
}

//-----------------------------------------------------------------------------
Bool PartitionManager::isClearLineOfSightTerrainFix(const Object* obj, const FCoord3D& objPos, const Object* other, const FCoord3D& otherPos)
{
	FCoord3D pos, posOther;

	if (obj)
	{
		pos = *obj->getPositionFix();
		// note that we want to measure from the top of the collision
		// shape, not the bottom! (most objects have eyes a lot closer
		// to their head than their feet. if we have really odd critters
		// with eye-feet, we'll need to change this assumption.)
		pos.z += obj->getGeometryInfo().getMaxHeightAbovePositionFix();
	}
	else
	{
		pos = objPos;
	}

	if (other)
	{
		posOther = *other->getPositionFix();
		// note that we want to measure from the top of the collision
		// shape, not the bottom! (most objects have eyes a lot closer
		// to their head than their feet. if we have really odd critters
		// with eye-feet, we'll need to change this assumption.)
		posOther.z += other->getGeometryInfo().getMaxHeightAbovePositionFix();
	}
	else
	{
		posOther = otherPos;
	}

#ifdef NO_BAD_AND_INACCURATE
/*
	This looks tempting, and in theory would be better than the below code; however, it
	is not (yet) accurate enough for our targeting needs. what we really need to do is add a Bresenham
	line iterator to the TerrainLogic itself, which would probably suffice. Until then, this code is
	left in as an example of what NOT to do. (srj)
*/
	Real maxZ;
	Coord2D maxZPos;
	Bool valid = estimateTerrainExtremesAlongLine(pos, posOther, NULL, &maxZ, NULL, &maxZPos);
	DEBUG_ASSERTCRASH(valid, ("this should never happen unless both positions are off-map"));
	if (!valid)
		return true;

	Real terrainAtHighPoint = maxZ;

	Real totalDist2D = calcDist2D(pos.x, pos.y, posOther.x, posOther.y);
	Real terrainDist2D = calcDist2D(pos.x, pos.y, maxZPos.x, maxZPos.y);
	
	const Real TINY_DIST = 0.01f;
	Real percent = (totalDist2D > TINY_DIST) ? (terrainDist2D / totalDist2D) : 0.0f;
	if (percent < 0.0f) percent = 0.0f;
	if (percent > 1.0f) percent = 1.0f;

	Real lineOfSightAtHighPoint = pos.z + (posOther.z - pos.z) * percent;

	// if terrainAtHighPoint > lineOfSightAtHighPoint, we can't see.
	// add a little fudge to account for slop.
	const Real LOS_FUDGE = 0.5f;
	if (terrainAtHighPoint > lineOfSightAtHighPoint + LOS_FUDGE)
	{
		//DEBUG_LOG(("isClearLineOfSightTerrain fails\n"));
		return false;
	}

	return true;

#else
	// P2: the terrain's line of sight is float
	return TheTerrainLogic->isClearLineOfSight(pos.toCoord3D(), posOther.toCoord3D());
#endif
}

// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void PartitionManager::crc( Xfer *xfer )
{

	for (Int i=0; i<m_totalCellCount; ++i)
	{
		m_cells[i].crc(xfer);
	}

}  // end crc

// ------------------------------------------------------------------------------------------------
/** Xfer Method
	* Version Info:
	* 1: Initial version
	* 2: m_pendingUndoShroudReveals stores Unlooks waiting to happen.
	*/
// ------------------------------------------------------------------------------------------------
void PartitionManager::xfer( Xfer *xfer )
{

	// version
	XferVersion currentVersion = 2;
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );

	// cell size information
	Real cellSize = m_cellSize;
	xfer->xferReal( &cellSize );

	//
	// cell size should match what we have allocated in the partition manager, if it doesn't
	// we need to increment the version here and edit this code to be able to load
	// the old cell size if changed after we release
	//
	if( cellSize != m_cellSize )
	{

		DEBUG_CRASH(( "Partition cell size has changed, this save game file is invalid\n" ));
		throw SC_INVALID_DATA;

	}  // end if

	// cell count
	Int totalCellCount = m_totalCellCount;
	xfer->xferInt( &totalCellCount );
	
	// total cell count better match what we have allocated for cells in the partition manager
	if( totalCellCount != m_totalCellCount )
	{

		DEBUG_CRASH(( "Partition total cell count mismatch %d, should be %d\n",
									totalCellCount, m_totalCellCount ));
		throw SC_INVALID_DATA;

	}  // end if

	// xfer each cell information
	PartitionCell *cell;
	for( Int i = 0; i < totalCellCount; ++i )
	{

		// get this partition cell
		cell = &m_cells[ i ];

		// xfer the data for this cell
		xfer->xferSnapshot( cell );

	}  // end for i

	// when loading tell the partition manager to rethink and refresh all shroud information
	if( xfer->getXferMode() == XFER_LOAD )
	{

		// tell partition manager to re-evaluate shroud things when next asked
		m_updatedSinceLastReset = FALSE;

		// refresh the shroud for the local player which will update the radar and everything
		refreshShroudForLocalPlayer();

	}  // end if

	if( version >= 2 )
	{
		Int queueSize = m_pendingUndoShroudReveals.size();
		xfer->xferInt(&queueSize);
		// This xfer thing is still cool.  On save, I just wrote the number of elements.  On load, the first line 
		// above was a decoy, and the second found out the size.

		if(xfer->getXferMode() == XFER_LOAD)
		{
			// have to remove this assert, because during load there is a setTeam call for each guy on a sub-team, and that results 
			// in a queued unlook, so we actually have stuff in here at the start.  I am fairly certain that setTeam should wait
			// until loadPostProcess, but I ain't gonna change it now.
//			DEBUG_ASSERTCRASH(m_pendingUndoShroudReveals.size() == 0, ("At load, we appear to not be in a reset state.") );

			// I have to split this up though, since on Load I need to make new instances.
			for( Int infoIndex = 0; infoIndex < queueSize; infoIndex++ )
			{
				SightingInfo *newInfo = newInstance(SightingInfo);
				xfer->xferSnapshot(newInfo);
				m_pendingUndoShroudReveals.push(newInfo);
			}
		}
		else
		{
			// And on Save, I need to loop through, but not destroy anything, so I pop-push n times, cycling through the queue once
			// (No random access on queues, and speed not a concern in saveload)
			for( Int infoIndex = 0; infoIndex < queueSize; infoIndex++ )
			{
				SightingInfo *saveInfo = m_pendingUndoShroudReveals.front();
				xfer->xferSnapshot(saveInfo);
				m_pendingUndoShroudReveals.pop();
				m_pendingUndoShroudReveals.push(saveInfo);
			}
		}

	}
	else
	{
		// Version 1 save games will just not have any SightingInfos in the queue to be undone.
	}

}  // end xfer

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void PartitionManager::loadPostProcess( void )
{

}  // end loadPostProcess

//-----------------------------------------------------------------------------
Real PartitionManager::getGroundOrStructureHeight(Real posx, Real posy)
{
	// P9: float in the shared header; the work inside is fixed
	FCoord3D pos;
	pos.x = fixFromReal( posx );
	pos.y = fixFromReal( posy );

	// get the terrain height
	pos.z = TheTerrainLogic->getGroundHeightFix( pos.x, pos.y );

	// scan all objects in the radius of our extent and find the tallest height among them
	PartitionFilterAcceptByKindOf filter1( MAKE_KINDOF_MASK( KINDOF_STRUCTURE ), KINDOFMASK_NONE );
	PartitionFilter *filters[] = { &filter1, NULL };
	const Fix RANGE = Fix( 1 );
	ObjectIterator *iter = iterateObjectsInRangeFix( &pos, RANGE, FROM_BOUNDINGSPHERE_2D, filters );
	MemoryPoolObjectHolder hold( iter );

	Fix tallestHeight = Fix( 0 );
	for( Object* obj = iter->first(); obj; obj = iter->next() )
	{
		// store the height of the tallest object under us
		tallestHeight = fixMax( tallestHeight, obj->getGeometryInfo().getMaxHeightAbovePositionFix() );
	}

  return fixToReal( pos.z + tallestHeight );
}

//-------------------------------------------------------------------------------------------------
void PartitionManager::getMostValuableLocation( Int playerIndex, UnsignedInt whichPlayerTypes, ValueOrThreat valType, FCoord3D *outLocation )
{
	if (!outLocation)
		return;

	PlayerMaskType playerMask = ThePlayerList->getPlayersWithRelationship(playerIndex, whichPlayerTypes);
	if (playerMask == 0)
		return;

	Int cellCount = m_cellCountX * m_cellCountY;
	
	PlayerMaskType allPlayerMasks[MAX_PLAYER_COUNT] = { 0 };
	Int totalPlayerCount = ThePlayerList->getPlayerCount();
	
	Int i;
	for (i = 0; i < totalPlayerCount; ++i) {
		Player *player = ThePlayerList->getNthPlayer(i);
		if (!player) {
			continue;
		}

		allPlayerMasks[i] = player->getPlayerMask();
	}

	Int greatestValueCell = -1;
	Int maxCellValue = -1;
	for (i = 0; i < cellCount; ++i) {
		Int cellValue = 0;

		for (Int player = 0; player < MAX_PLAYER_COUNT; ++player) {
			if (BitTest(allPlayerMasks[player], playerMask)) {
				if (valType == VOT_CashValue) {
					cellValue += m_cells[i].getCashValue(player);
				} else {
					cellValue += m_cells[i].getThreatValue(player);
				}
			}
		}

		if (cellValue > maxCellValue) {
			maxCellValue = cellValue;
			greatestValueCell = i;
		}
	}

	if (greatestValueCell == -1 || maxCellValue == -1) {
		DEBUG_CRASH(("PartitionManager::getMostValuableLocation: jkmcd"));
		return;
	}

	outLocation->set(Fix(m_cells[greatestValueCell].getCellX()) * m_cellSizeFix,
									 Fix(m_cells[greatestValueCell].getCellY()) * m_cellSizeFix,
									 Fix(0)
									);
}

//-------------------------------------------------------------------------------------------------
Bool PartitionManager::getMostValuableVisibleLocation( Int playerIndex, UnsignedInt whichPlayerTypes,
																											 ValueOrThreat valType, FCoord3D *outLocation )
{
	if (!outLocation)
		return FALSE;

	PlayerMaskType playerMask = ThePlayerList->getPlayersWithRelationship(playerIndex, whichPlayerTypes);
	if (playerMask == 0)
		return FALSE;

	PlayerMaskType allPlayerMasks[MAX_PLAYER_COUNT] = { 0 };
	Int totalPlayerCount = ThePlayerList->getPlayerCount();
	Int i;
	for (i = 0; i < totalPlayerCount; ++i) {
		Player *player = ThePlayerList->getNthPlayer(i);
		if (player)
			allPlayerMasks[i] = player->getPlayerMask();
	}

	const Int cellCount = m_cellCountX * m_cellCountY;
	Int greatestValueCell = -1;
	Int maxCellValue = 0;			// zero, not -1: a cell worth nothing is not an answer

	for (i = 0; i < cellCount; ++i) {
		Int cellValue = 0;
		for (Int player = 0; player < MAX_PLAYER_COUNT; ++player) {
			if (BitTest(allPlayerMasks[player], playerMask)) {
				if (valType == VOT_CashValue) {
					cellValue += m_cells[i].getCashValue(player);
				} else {
					cellValue += m_cells[i].getThreatValue(player);
				}
			}
		}

		if (cellValue <= maxCellValue)
			continue;

		// ... and only if this player has seen the ground it is standing on
		Coord3D where;
		where.set(m_cells[i].getCellX() * TheGlobalData->m_partitionCellSize,
							m_cells[i].getCellY() * TheGlobalData->m_partitionCellSize, 0);
		if (getPropShroudStatusForPlayer(playerIndex, &where) == OBJECTSHROUD_SHROUDED)
			continue;

		maxCellValue = cellValue;
		greatestValueCell = i;
	}

	if (greatestValueCell == -1)
		return FALSE;

	outLocation->set(Fix(m_cells[greatestValueCell].getCellX()) * m_cellSizeFix,
									 Fix(m_cells[greatestValueCell].getCellY()) * m_cellSizeFix,
									 Fix(0));
	return TRUE;
}

//-------------------------------------------------------------------------------------------------
void PartitionManager::getNearestGroupWithValue( Int playerIndex, UnsignedInt whichPlayerTypes, ValueOrThreat valType,
															 const FCoord3D *sourceLocation, Int valueRequired, Bool greaterThan, FCoord3D *outLocation )
{
	if (!(sourceLocation && outLocation))
		return;

	PlayerMaskType playerMask = ThePlayerList->getPlayersWithRelationship(playerIndex, whichPlayerTypes);
	if (playerMask == 0)
		return;
	
	PlayerMaskType allPlayerMasks[MAX_PLAYER_COUNT] = { 0 };
	Int totalPlayerCount = ThePlayerList->getPlayerCount();
	
	Int i;
	for (i = 0; i < totalPlayerCount; ++i) {
		Player *player = ThePlayerList->getNthPlayer(i);
		if (!player) {
			continue;
		}

		allPlayerMasks[i] = player->getPlayerMask();
	}

	CellValueProcParms parms;
	parms.valueRequired = valueRequired;
	parms.greaterThan = valueRequired;
	parms.valueType = valType;
	parms.allowedPlayersMasks = playerMask;
	for (i = 0; i < MAX_PLAYER_COUNT; ++i) 
		parms.allPlayersMask[i] = allPlayerMasks[i];
	
	Int nearestGreat = iterateCellsBreadthFirst(sourceLocation, cellValueProc, &parms);
	if (nearestGreat != -1) {
		(*outLocation).x = Fix(m_cells[nearestGreat].getCellX()) * m_cellSizeFix;
		(*outLocation).y = Fix(m_cells[nearestGreat].getCellY()) * m_cellSizeFix;
		(*outLocation).z = Fix(0);
	}

	// all done
}

//-------------------------------------------------------------------------------------------------
void PartitionManager::storeFoggedCells(ShroudStatusStoreRestore &outPartitionStore, Bool storeToFog) const
{
	Int i, j, p;

	if (storeToFog) {
		// This is the first pass
		outPartitionStore.m_cellsWide = m_cellCountX;
		
		// Resize all of our arrays.
		for (p = 0; p < MAX_PLAYER_COUNT; ++p) {
			outPartitionStore.m_foggedOrRevealed[p].resize(m_totalCellCount);
			for (i = 0; i < m_totalCellCount; ++i) {
				outPartitionStore.m_foggedOrRevealed[p][i] = STORE_DONTTOUCH;
			}
		}
	}

	for (p = 0; p < MAX_PLAYER_COUNT; ++p) {
		if (outPartitionStore.m_foggedOrRevealed[p].size() != m_totalCellCount) {
			DEBUG_CRASH(("PartitionManager::storeFoggedCells: jkmcd - x36872"));
			continue;
		}

		for (j = 0; j < m_cellCountY; ++j) {
			for (i = 0; i < m_cellCountX; ++i) {
				UnsignedByte &byteToWrite = outPartitionStore.m_foggedOrRevealed[p][j * m_cellCountX + i]; 

				if (storeToFog && m_cells[j * m_cellCountX + i].getShroudStatusForPlayer(p) == CELLSHROUD_FOGGED) {
					byteToWrite = STORE_FOG;
				}
					
				if (!storeToFog && m_cells[j * m_cellCountX + i].getShroudStatusForPlayer(p) == CELLSHROUD_CLEAR) {
					byteToWrite = STORE_PERMANENTLY_REVEALED;
				}
			}
		}
	}
}

//-------------------------------------------------------------------------------------------------
void PartitionManager::restoreFoggedCells(const ShroudStatusStoreRestore &inPartitionStore, Bool restoreToFog)
{
	Int i, j, p;

	Int storeWidth = inPartitionStore.m_cellsWide;
	Int storeHeight = inPartitionStore.m_foggedOrRevealed[0].size() / storeWidth;

	for (p = 0; p < MAX_PLAYER_COUNT; ++p) {
		for (j = 0; j < storeHeight; ++j) {
			if (j >= m_cellCountY) {
				// All done.
				return;
			}

			for (i = 0; i < storeWidth; ++i) {
				if (i >= m_cellCountX) {
					// Skip to the next line. This info will be thrown away.
					break;
				}

				UnsignedByte byteToRestore = inPartitionStore.m_foggedOrRevealed[p][j * storeWidth + i];
				if (byteToRestore == STORE_DONTTOUCH) {
					continue;
				}

				if (byteToRestore == STORE_FOG && restoreToFog) {
					// restore the fog status of this cell
					m_cells[j * m_cellCountX + i].addLooker(p);
					m_cells[j * m_cellCountX + i].removeLooker(p);
				}

				if (byteToRestore == STORE_PERMANENTLY_REVEALED && !restoreToFog) {
					// Add an extra looker.
					m_cells[j * m_cellCountX + i].addLooker(p);
				}
			}
		}
	}
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
PartitionFilterRejectBuildings::PartitionFilterRejectBuildings(const Object *o) : 
	m_self(o),
	m_acquireEnemies(false)
{ 
	// if I am a computer-controlled opponent, auto-aquire enemy buildings
	if (m_self->getControllingPlayer()->getPlayerType() == PLAYER_COMPUTER)
	{
		m_acquireEnemies = true;
	}
}

//-----------------------------------------------------------------------------
Bool PartitionFilterRejectBuildings::allow( Object *other )
{
	// this filter allows all non-buildings
	// um, no, it clearly doesn't any more.
	// didn't the person adding the code below read the comment?
	if (!other->isKindOf( KINDOF_STRUCTURE ))
		return true;

	const Player* myPlayer = m_self->getControllingPlayer();
	if (!myPlayer)
		return false;

	// Get the controlling team of other.
	ContainModuleInterface* contain = other->getContain();
	const Player* otherPlayer = contain ? contain->getApparentControllingPlayer(myPlayer) : NULL;

	if (!otherPlayer)
		otherPlayer = other->getControllingPlayer();

	if (!otherPlayer)
		return false;

	Relationship relationship = myPlayer->getRelationship(otherPlayer->getDefaultTeam());
	if (relationship != ENEMIES)
		return false;

	// if I am a computer-controlled opponent, auto-aquire enemy buildings (if we can see them!)
	if (m_acquireEnemies)
		return true;

	if (other->isKindOf( KINDOF_FS_BASE_DEFENSE))
	{
		// Don't reject base defenses.
		return true;
	}

	if (other->getContain() != NULL && other->isAbleToAttack())
	{
		// Don't reject garrisoned buildings that can attack
		return true;
  }

	return false;
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
/** Partly in sight counts. Since hills and buildings block sight, a building hides its own back
	cells from anyone lower than its roof and is never wholly clear, so taking only CLEAR here left
	every unit the AI controls standing in front of an enemy base without firing a shot. */
Bool PartitionFilterFreeOfFog::allow( Object *other )
{
	const ObjectShroudStatus status = other->getShroudedStatus(m_comparisonIndex);
	return status == OBJECTSHROUD_CLEAR || status == OBJECTSHROUD_PARTIAL_CLEAR;
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
Bool PartitionFilterInsignificantBuildings::allow( Object *other )
{
	if (other->isStructure()) {
		if (other->isNonFactionStructure() && !m_allowInsignificant) {
			ContainModuleInterface *cmi = other->getContain();
			if (cmi) {
				if (!cmi->isGarrisonable() || cmi->getContainCount() == 0) {
					return false;
				}
			}
		}
		return true;
		
	} else {
		if (m_allowNonBuildings) {
			return true;
		}
	}

	return false;
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------


//-----------------------------------------------------------------------------

Bool PartitionFilterRepulsor::allow( Object *other )
{
	if (other == m_self) 
	{
		// don't repulse yourself. :)
		return false;
	}

	// If it's flagged, it's a repulsor.
	if (other->testStatus(OBJECT_STATUS_REPULSOR)) 
	{
		return	true;
	}

	if (other->isEffectivelyDead()) 
		return false; // no dead enemies.

	Relationship r = m_self->getRelationship(other);
	if (r != ENEMIES) 
	{
		return false; // only enemies auto repulse.
	}

	if (other->isKindOf( KINDOF_STRUCTURE ))
	{
		// always pay attention to buildings that can attack
		if (other->isAbleToAttack())
			return true;
		return false;
	}
	
	if ( other->isKindOf( KINDOF_INERT ))
		return false;


	if ( ! other->isAbleToAttack() )
	{
		return false;
	}

	return true;
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------

Bool PartitionFilterIrregularArea::allow( Object *other )
{

	Coord3D pos = other->getPositionFix()->toCoord3D();	// P7: the area is float script data
	return PointInsideArea2D(&pos, m_area, m_numPointsInArea);
}

//-----------------------------------------------------------------------------
Bool PartitionFilterPolygonTrigger::allow( Object *other )
{
	ICoord3D iPos;
	iPos.x = fixTruncToInt(other->getPositionFix()->x);
	iPos.y = fixTruncToInt(other->getPositionFix()->y);
	iPos.z = 0; // Trigger areas compare on xy only.
	return m_trigger->pointInTrigger(iPos);
}

//-----------------------------------------------------------------------------

Bool PartitionFilterPlayer::allow( Object *other )
{
	return ((m_player == other->getControllingPlayer()) == m_match);
}

//-----------------------------------------------------------------------------
Bool PartitionFilterPlayerAffiliation::allow( Object *other )
{
	Relationship rel = m_player->getRelationship(other->getTeam());
	switch (rel)
	{
		case ENEMIES:	
			if (m_affiliation & ALLOW_ENEMIES) {
				return m_match;
			}
			break;

		case NEUTRAL:
			if (m_affiliation & ALLOW_NEUTRAL) {
				return m_match;
			}
			break;

		case ALLIES:
			if (m_affiliation & ALLOW_ALLIES) {
				return m_match;
			}
			break;
	}

	if (other->getControllingPlayer() == m_player) {
		return m_match;		
	}

	return !m_match;
}

//-----------------------------------------------------------------------------
Bool PartitionFilterThing::allow( Object *other )
{
	return (m_tThing->isEquivalentTo(other->getTemplate()) == m_match);
}

//-----------------------------------------------------------------------------
Bool PartitionFilterGarrisonable::allow( Object *other )
{
	ContainModuleInterface *cmi = other->getContain();
	if (!cmi) {
		return !m_match;
	}

	return (cmi->isGarrisonable() == m_match);
}

//-----------------------------------------------------------------------------
Bool PartitionFilterGarrisonableByPlayer::allow( Object *other )
{
	return TheActionManager->canPlayerGarrison(m_player, other, m_commandSource) == m_match;
}

//-----------------------------------------------------------------------------
Bool PartitionFilterUnmannedObject::allow( Object *other )
{
	return (other->isDisabledByType( DISABLED_UNMANNED ) == m_match);
}

//-----------------------------------------------------------------------------
Bool PartitionFilterValidCommandButtonTarget::allow( Object *other )
{
	return (m_commandButton->isValidToUseOn(m_source, other, NULL, m_commandSource) == m_match);
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
Bool PartitionFilterIsFlying::allow(Object *objOther)
{
	return objOther->isUsingAirborneLocomotor();
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
PartitionFilterWouldCollide::PartitionFilterWouldCollide(const FCoord3D& pos, const GeometryInfo& geom, Fix angle, Bool desired) :
  m_position(pos),
	m_geom(geom),
  m_angle(angle),
  m_desiredCollisionResult(desired)
{
}

//-----------------------------------------------------------------------------

Bool PartitionFilterWouldCollide::allow(Object *objOther)
{
	CollideInfo thisInfo(&m_position, m_geom, m_angle);
	CollideInfo thatInfo(objOther->getPositionFix(), objOther->getGeometryInfo(), objOther->getOrientationFix());

  Bool doesCollide;

	// invariant for all geometries: first do z collision check.
	if (thisInfo.position.z + thisInfo.geom.getMaxHeightAbovePositionFix() >= thatInfo.position.z &&
			thisInfo.position.z <= thatInfo.position.z + thatInfo.geom.getMaxHeightAbovePositionFix())
	{
		GeometryType thisGeom = m_geom.getGeomType();
		GeometryType thatGeom = objOther->getGeometryInfo().getGeomType();

		//
		// NOTE: This assumes geometry enumerations that start at GEOMETRY_FIRST AND depends on the
		// order in which they appear in the enum list
		//
		CollideTestProc collideProc = theCollideTestProcs[ (thisGeom - GEOMETRY_FIRST) * GEOMETRY_NUM_TYPES + (thatGeom - GEOMETRY_FIRST) ];
		FCollideLocAndNormal cinfo;
		doesCollide = (*collideProc)(&thisInfo, &thatInfo, &cinfo);
	}
	else
	{
		// no z-intersection -> no collision.
		doesCollide = false;
	}

  return doesCollide == m_desiredCollisionResult;
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
Bool PartitionFilterSamePlayer::allow(Object *objOther)
{
	if (m_player == objOther->getControllingPlayer())
		return true;

	return false;
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
Bool PartitionFilterRelationship::allow(Object *objOther)
{
	Relationship r = m_obj->getRelationship(objOther);

	if ((m_flags & (1<<r)) != 0)
		return true;

	return false;
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
PartitionFilterAcceptOnTeam::PartitionFilterAcceptOnTeam(const Team *team) : m_team(team)
{
}

//-----------------------------------------------------------------------------
Bool PartitionFilterAcceptOnTeam::allow(Object *objOther) 
{
	// objOther is guaranteed to be non-null, so we don't need to check (srj)
	return (objOther->getTeam() == m_team);
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
PartitionFilterAcceptOnSquad::PartitionFilterAcceptOnSquad(const Squad *squad) : m_squad(squad)
{
}

//-----------------------------------------------------------------------------

Bool PartitionFilterAcceptOnSquad::allow(Object *objOther)
{
	return (m_squad && m_squad->isOnSquad(objOther) && !objOther->isEffectivelyDead());
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
Bool PartitionFilterSameMapStatus::allow(Object* objOther)
{
	// objOther is guaranteed to be non-null, so we don't need to check (srj)
	return objOther->isOffMap() == m_obj->isOffMap();
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
Bool PartitionFilterOnMap::allow(Object* objOther)
{
	// objOther is guaranteed to be non-null, so we don't need to check (srj)
	return !objOther->isOffMap();
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
Bool PartitionFilterAlive::allow( Object *objOther )
{
	return !objOther->isEffectivelyDead();
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
PartitionFilterRejectBehind::PartitionFilterRejectBehind( Object *obj )
{
	m_obj = obj;
}

//-----------------------------------------------------------------------------

Bool PartitionFilterRejectBehind::allow( Object *other )
{
	// objOther is guaranteed to be non-null, so we don't need to check (srj)

	// the model's x axis; only the sign of the dot is wanted, so it needs no normalizing
	const FixMatrix3D *mtx = m_obj->getTransformMatrixFix();

	FCoord3D v;
	ThePartitionManager->getDistanceSquaredFix( m_obj, other, FROM_CENTER_3D, &v );

	Fix dot = mtx->m[0][0] * v.x + mtx->m[1][0] * v.y + mtx->m[2][0] * v.z;

	return dot > Fix(0);
}



//-----------------------------------------------------------------------------
PartitionFilterLineOfSight::PartitionFilterLineOfSight(const Object *obj)
{
	m_obj = obj;
}

//-----------------------------------------------------------------------------
Bool PartitionFilterLineOfSight::allow(Object *objOther)
{
	// objOther is guaranteed to be non-null, so we don't need to check (srj)

	// both objects are given, so it measures from their own eyes and these positions go unread
	if (!ThePartitionManager->isClearLineOfSightTerrainFix(m_obj, *m_obj->getPositionFix(), objOther, *objOther->getPositionFix()))
		return false;

	if (TheAI && TheAI->pathfinder()->isViewBlockedByObstacle(m_obj, objOther))
		return false;

	return true;
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
PartitionFilterPossibleToAttack::PartitionFilterPossibleToAttack(AbleToAttackType t, const Object *obj, CommandSourceType commandSource) :
	m_attackType(t),
	m_obj(obj),
	m_commandSource(commandSource)
{
}

//-----------------------------------------------------------------------------
Bool PartitionFilterPossibleToAttack::allow(Object *objOther)
{
	// objOther is guaranteed to be non-null, so we don't need to check (srj)
	
	// we should have already filtered out isAbleToAttack!
#ifdef _DEBUG
	// disable this assert for INTERNAL builds (srj)
	DEBUG_ASSERTCRASH(m_obj && m_obj->isAbleToAttack(), ("if the object is unable to attack at all, you should filter that out ahead of time!"));
#endif
	//
	// Someone else's shots, in the air or announced and about to be fired, already add up to more
	// than this thing has left, so acquiring it would spend a volley on a corpse.  Our own
	// announcement does not count against us, or a unit re-scanning would refuse the target it is
	// currently lining up.  Skipping it here is what spreads a
	// group's fire over several targets instead of piling all of it onto the nearest one.  This
	// filter is only ever used to acquire a target on the unit's own initiative - guarding, attack
	// moving, idle scanning - so an explicit attack order is never touched by it.
	//
	if (IncomingDamageTracker::isSpokenFor(objOther, m_obj->getID()))
		return FALSE;

	//
	// A capturable tech building is income, not a target.  The computer razed the enemy's oil derricks
	// on sight - the one thing you can do with a derrick that earns nobody anything - when it could
	// have walked an infantryman in and owned it.  As above, this filter only ever runs when a unit is
	// picking its own target, so an explicit order to level one still goes through, and a tech
	// building that shoots back is still shot back at.
	//
	if (m_obj->getControllingPlayer() && m_obj->getControllingPlayer()->getPlayerType() == PLAYER_COMPUTER &&
			aiIsIncomeNotATarget(objOther->isKindOf(KINDOF_TECH_BUILDING),
													 objOther->isKindOf(KINDOF_CAPTURABLE),
													 objOther->isKindOf(KINDOF_TECH_BASE_DEFENSE)))
		return FALSE;

	CanAttackResult result = m_obj->getAbleToAttackSpecificObject( m_attackType, objOther, m_commandSource );
	if( result == ATTACKRESULT_POSSIBLE || result == ATTACKRESULT_POSSIBLE_AFTER_MOVING )
	{
		return TRUE;
	}
	return FALSE;
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
PartitionFilterPossibleToEnter::PartitionFilterPossibleToEnter(const Object *obj, CommandSourceType commandSource) :
	m_obj(obj),
	m_commandSource(commandSource)
{
}

//-----------------------------------------------------------------------------
Bool PartitionFilterPossibleToEnter::allow(Object *objOther)
{
	if (!objOther || !m_obj) 
		return FALSE;

	if( TheActionManager->canEnterObject( m_obj, objOther, m_commandSource, DONT_CHECK_CAPACITY ) )
		return TRUE;
	else
		return FALSE;
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
PartitionFilterPossibleToHijack::PartitionFilterPossibleToHijack(const Object *obj, CommandSourceType commandSource) :
	m_obj(obj),
	m_commandSource(commandSource)
{
}

//-----------------------------------------------------------------------------
Bool PartitionFilterPossibleToHijack::allow(Object *objOther)
{
	if (!objOther || !m_obj) 
		return FALSE;

	if( TheActionManager->canHijackVehicle(m_obj, objOther, m_commandSource) )
		return TRUE;
	else
		return FALSE;
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
PartitionFilterLastAttackedBy::PartitionFilterLastAttackedBy(Object *obj) 
{
	if (obj && obj->getBodyModule()) {
		m_lastAttackedBy = obj->getBodyModule()->getLastDamageInfo()->in.m_sourceID;
	} else {
		m_lastAttackedBy = INVALID_ID;
	}
}


//-----------------------------------------------------------------------------

Bool PartitionFilterLastAttackedBy::allow(Object *other)
{
	// objOther is guaranteed to be non-null, so we don't need to check (srj)
	if (other->getID() == m_lastAttackedBy)
		return true;

	return false;
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
Bool PartitionFilterAcceptByObjectStatus::allow(Object *objOther)
{ 
	ObjectStatusMaskType status = objOther->getStatusBits();
	return status.testForAll( m_mustBeSet ) && status.testForNone( m_mustBeClear );
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
Bool PartitionFilterRejectByObjectStatus::allow(Object *objOther)
{ 
	ObjectStatusMaskType status = objOther->getStatusBits();

	return !( status.testForAll( m_mustBeSet ) && status.testForNone( m_mustBeClear ) );
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
Bool PartitionFilterAcceptByKindOf::allow(Object *objOther)
{
	return objOther->isKindOfMulti(m_mustBeSet, m_mustBeClear);
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
Bool PartitionFilterRejectByKindOf::allow(Object *objOther)
{
	return !objOther->isKindOfMulti(m_mustBeSet, m_mustBeClear); 
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
Bool PartitionFilterStealthedAndUndetected::allow( Object *objOther )
{
	// objOther is guaranteed to be non-null, so we don't need to check (srj)

	Bool stealthed = objOther->testStatus( OBJECT_STATUS_STEALTHED );
	Bool detected = objOther->testStatus( OBJECT_STATUS_DETECTED );
	Bool disguised = objOther->testStatus( OBJECT_STATUS_DISGUISED );

	if( stealthed && !detected )
	{
		if( !objOther->isKindOf( KINDOF_DISGUISER ) )
		{
			//We have a stealthed & undetected object.
			return m_allow;
		}
		else if( disguised )
		{
			//Exception case -- bomb trucks can't be considered stealthed units when they are disguised as the enemy.
      
      StealthUpdate *update = objOther->getStealth();

			if( update && update->isDisguised() )
			{
				Player *ourPlayer = m_obj->getControllingPlayer();
				Player *otherPlayer = ThePlayerList->getNthPlayer( update->getDisguisedPlayerIndex() );
				if( ourPlayer && otherPlayer )
				{
					if( ourPlayer->getRelationship( otherPlayer->getDefaultTeam() ) == ENEMIES )
					{
						//Our stealthed & undetected object is disguised as a unit perceived to be our enemy,
						//therefore it's not considered to be stealthed!
						return !m_allow;
					}
					//Our object is disguised as a "friendly" object, therefore it's considered to be stealthed.
					return m_allow;
				}
			}
			//Our bomb truck is not disguised therefore it's not stealthed.
			return !m_allow;
		}
	}
	else
	{
		//This handles neutral containers that hold stealth units. This specifically fixes a bug where hunt scripts would ignore
		//this case -- units would acquire the building Jarmen Kell occupied even though it was not stealth detected.
		const ContainModuleInterface* contain = objOther->getContain();
		if( contain )
		{
			const Player* victimApparentController = contain->getApparentControllingPlayer( m_obj->getControllingPlayer() );
			//Check if it's stealthed!
			if( contain->getStealthUnitsContained() == contain->getContainCount() )
			{
				//Check if the first object inside is detected (if one is detected, all are detected).
				ContainedItemsList::const_iterator it = contain->getContainedItemsList()->begin();
				Object *member = (*it);
				if( member && !(*it)->getStatusBits().test( OBJECT_STATUS_DETECTED ) )
				{
					//Finally check the relationship!
					if( victimApparentController && m_obj->getTeam()->getRelationship( victimApparentController->getDefaultTeam() ) == ENEMIES )
					{
						//Our object is a neutral building garrisoned by enemy units we can't see, therefore it is stealthed.
						return m_allow;
					}
				}
			}
		}
	}

	//The unit is not stealthed!
	return !m_allow;
}


//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
static int cellValueProc(PartitionCell* cell, void* userData)
{
	CellValueProcParms *parms = (CellValueProcParms*) userData;

	UnsignedInt val = 0;
	for (Int i = 0; i < MAX_PLAYER_COUNT; ++i) {
		if (BitTest(parms->allowedPlayersMasks, parms->allPlayersMask[i])) {
			if (parms->valueType == VOT_CashValue) {
				val += cell->getCashValue(i);
			} else {
				val += cell->getThreatValue(i);
			}
		}
	}

	if ((val > parms->valueRequired && parms->greaterThan) || 
			(val < parms->valueRequired && !parms->greaterThan)) {
		return 1;
	}

	return 0;
}

// -----------------------------------------------------------------------------
static void hLineAddLooker(Int x1, Int x2, Int y, void *playerIndexVoid)
{
	if (y < 0 || y >= ThePartitionManager->m_cellCountY || x1 >= ThePartitionManager->m_cellCountX || x2 < 0)
		return;

	Int playerIndex = (Int)(playerIndexVoid);

	PartitionCell* cell = &ThePartitionManager->m_cells[y * ThePartitionManager->m_cellCountX + x1];	// yes, this could be invalid. we'll skip the bad ones.
	for (Int x = x1; x <= x2; ++x, ++cell)
	{
		if (x < 0 || x >= ThePartitionManager->m_cellCountX)
			continue;
		cell->addLooker(playerIndex);
	}
}

// -----------------------------------------------------------------------------
static void hLineRemoveLooker(Int x1, Int x2, Int y, void *playerIndexVoid)
{
	if (y < 0 || y >= ThePartitionManager->m_cellCountY || x1 >= ThePartitionManager->m_cellCountX || x2 < 0)
		return;

	Int playerIndex = (Int)(playerIndexVoid);

	PartitionCell* cell = &ThePartitionManager->m_cells[y * ThePartitionManager->m_cellCountX + x1];	// yes, this could be invalid. we'll skip the bad ones.
	for (Int x = x1; x <= x2; ++x, ++cell)
	{
		if (x < 0 || x >= ThePartitionManager->m_cellCountX)
			continue;
		cell->removeLooker(playerIndex);
	}
}

// -----------------------------------------------------------------------------
static void hLineAddShrouder(Int x1, Int x2, Int y, void *playerIndexVoid)
{
	if (y < 0 || y >= ThePartitionManager->m_cellCountY || x1 >= ThePartitionManager->m_cellCountX || x2 < 0)
		return;

	Int playerIndex = (Int)(playerIndexVoid);

	PartitionCell* cell = &ThePartitionManager->m_cells[y * ThePartitionManager->m_cellCountX + x1];	// yes, this could be invalid. we'll skip the bad ones.
	for (Int x = x1; x <= x2; ++x, ++cell)
	{
		if (x < 0 || x >= ThePartitionManager->m_cellCountX)
			continue;
		cell->addShrouder( playerIndex );
	}
}

// -----------------------------------------------------------------------------
static void hLineRemoveShrouder(Int x1, Int x2, Int y, void *playerIndexVoid)
{
	if (y < 0 || y >= ThePartitionManager->m_cellCountY || x1 >= ThePartitionManager->m_cellCountX || x2 < 0)
		return;

	Int playerIndex = (Int)(playerIndexVoid);

	PartitionCell* cell = &ThePartitionManager->m_cells[y * ThePartitionManager->m_cellCountX + x1];	// yes, this could be invalid. we'll skip the bad ones.
	for (Int x = x1; x <= x2; ++x, ++cell)
	{
		if (x < 0 || x >= ThePartitionManager->m_cellCountX)
			continue;
		cell->removeShrouder( playerIndex );
	}
}

// -----------------------------------------------------------------------------
static void hLineAddThreat(Int x1, Int x2, Int y, void *threatValueParms)
{
	if (y < 0 || y >= ThePartitionManager->m_cellCountY || x1 >= ThePartitionManager->m_cellCountX || x2 < 0)
		return;

	ThreatValueParms *parms = (ThreatValueParms*)threatValueParms;

	Real distance;
	Real mulVal = 1.0f;

	PartitionCell* cell = &ThePartitionManager->m_cells[y * ThePartitionManager->m_cellCountX + x1];	// yes, this could be invalid. we'll skip the bad ones.
	for (Int x = x1; x <= x2; ++x, ++cell)
	{
		if (x < 0 || x >= ThePartitionManager->m_cellCountX)
			continue;

		distance = sqrt( sqr(x - parms->xCenter) + sqr(y - parms->yCenter) );
		mulVal = 1 - distance / parms->radius;
		if (mulVal < 0.0f) 
			mulVal = 0.0f;
		else if (mulVal > 1.0f)
			mulVal = 1.0f;

		cell->addThreatValue( parms->playerIndex, REAL_TO_UNSIGNEDINT(parms->threatOrValue * mulVal) );
	}
}

// -----------------------------------------------------------------------------
static void hLineRemoveThreat(Int x1, Int x2, Int y, void *threatValueParms)
{
	if (y < 0 || y >= ThePartitionManager->m_cellCountY || x1 >= ThePartitionManager->m_cellCountX || x2 < 0)
		return;

	ThreatValueParms *parms = (ThreatValueParms*)threatValueParms;

	Real distance;
	Real mulVal = 1.0f;

	PartitionCell* cell = &ThePartitionManager->m_cells[y * ThePartitionManager->m_cellCountX + x1];	// yes, this could be invalid. we'll skip the bad ones.
	for (Int x = x1; x <= x2; ++x, ++cell)
	{
		if (x < 0 || x >= ThePartitionManager->m_cellCountX)
			continue;

		distance = sqrt( sqr(x - parms->xCenter) + sqr(y - parms->yCenter) );
		mulVal = 1 - distance / parms->radius;
		if (mulVal < 0.0f) 
			mulVal = 0.0f;
		else if (mulVal > 1.0f)
			mulVal = 1.0f;
		
		cell->removeThreatValue( parms->playerIndex, REAL_TO_UNSIGNEDINT(parms->threatOrValue * mulVal) );
	}
}

// -----------------------------------------------------------------------------
static void hLineAddValue(Int x1, Int x2, Int y, void *threatValueParms)
{
	if (y < 0 || y >= ThePartitionManager->m_cellCountY || x1 >= ThePartitionManager->m_cellCountX || x2 < 0)
		return;

	ThreatValueParms *parms = (ThreatValueParms*)threatValueParms;

	Real distance;
	Real mulVal = 1.0f;

	PartitionCell* cell = &ThePartitionManager->m_cells[y * ThePartitionManager->m_cellCountX + x1];	// yes, this could be invalid. we'll skip the bad ones.
	for (Int x = x1; x <= x2; ++x, ++cell)
	{
		if (x < 0 || x >= ThePartitionManager->m_cellCountX)
			continue;

		distance = sqrt( sqr(x - parms->xCenter) + sqr(y - parms->yCenter) );
		mulVal = 1 - distance / parms->radius;
		if (mulVal < 0.0f) 
			mulVal = 0.0f;
		else if (mulVal > 1.0f)
			mulVal = 1.0f;
		
		cell->addCashValue( parms->playerIndex, REAL_TO_UNSIGNEDINT(parms->threatOrValue * mulVal) );
	}
}

// -----------------------------------------------------------------------------
static void hLineRemoveValue(Int x1, Int x2, Int y, void *threatValueParms)
{
	if (y < 0 || y >= ThePartitionManager->m_cellCountY || x1 >= ThePartitionManager->m_cellCountX || x2 < 0)
		return;

	ThreatValueParms *parms = (ThreatValueParms*)threatValueParms;

	Real distance;
	Real mulVal = 1.0f;

	PartitionCell* cell = &ThePartitionManager->m_cells[y * ThePartitionManager->m_cellCountX + x1];	// yes, this could be invalid. we'll skip the bad ones.
	for (Int x = x1; x <= x2; ++x, ++cell)
	{
		if (x < 0 || x >= ThePartitionManager->m_cellCountX)
			continue;

		distance = sqrt( sqr(x - parms->xCenter) + sqr(y - parms->yCenter) );
		mulVal = 1 - distance / parms->radius;
		if (mulVal < 0.0f) 
			mulVal = 0.0f;
		else if (mulVal > 1.0f)
			mulVal = 1.0f;

		cell->removeCashValue( parms->playerIndex, REAL_TO_UNSIGNEDINT(parms->threatOrValue * mulVal) );
	}
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
SightingInfo::SightingInfo() 
{
	reset();
}

// ------------------------------------------------------------------------------------------------
/** Resetting sighting info */
// ------------------------------------------------------------------------------------------------
void SightingInfo::reset()
{
	m_where.zero();
	m_howFar = Fix(0);
	m_forWhom = 0;
	m_data = 0;
	m_revealedCells.clear();
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
Bool SightingInfo::isInvalid() const
{
	return m_howFar == Fix(0);
}

// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void SightingInfo::crc( Xfer *xfer )
{

}  // end crc

// ------------------------------------------------------------------------------------------------
/** Xfer Method
	* Version Info:
	* 1: Initial version
	* 2: m_revealedCells; a version 1 sighting was a whole circle and is undone as one
	* 3: m_where and m_howFar in fixed point */
// ------------------------------------------------------------------------------------------------
void SightingInfo::xfer( Xfer *xfer )
{

	// version
	XferVersion currentVersion = 3;
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );

	if( version >= 3 )
	{
		xfer->xferFCoord3D( &m_where );
		xfer->xferFix( &m_howFar );
	}
	else
	{
		// an old save's float where and how far, taken in once through the boundary
		Coord3D where = m_where.toCoord3D();
		Real howFar = fixToReal( m_howFar );
		xfer->xferCoord3D( &where );
		xfer->xferReal( &howFar );
		m_where = fcoordFromCoord3D( where );
		m_howFar = fixFromReal( howFar );
	}

	// for whom
	xfer->xferUser( &m_forWhom, sizeof( PlayerMaskType ) );

	// how much
	xfer->xferUnsignedInt( &m_data );

	if( version >= 2 )
	{
		Int revealedCellCount = m_revealedCells.size();
		xfer->xferInt( &revealedCellCount );
		m_revealedCells.resize( revealedCellCount );
		if( revealedCellCount > 0 )
			xfer->xferUser( &m_revealedCells[0], revealedCellCount );
	}

}  // end xfer

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void SightingInfo::loadPostProcess()
{

}  // end loadPostProcess

// ------------------------------------------------------------------------------------------------
SightingInfo::~SightingInfo()
{

}  // end loadPostProcess

