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
// Modified 2026 by İlyas Akın for the macOS/Linux port: moved here from GeneralsMD/Code/GameEngineDevice/Source/W3DDevice/GameClient/BaseHeightMap.cpp and GeneralsMD/Code/GameEngineDevice/Include/W3DDevice/GameClient/BaseHeightMap.h; see NOTICE.md and the git history.

// FILE: TerrainHeightSampling.cpp ////////////////////////////////////////////////////////////////
// Desc:   BaseHeightMapRenderObjClass's height maths, moved to gameengine (T1).
///////////////////////////////////////////////////////////////////////////////////////////////////

/* Each function is BaseHeightMap.cpp's text (getClipHeight's from BaseHeightMap.h), with only the
	 substitutions TerrainHeightSampling.h lists. */

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "Common/MapObject.h"		// MAP_XY_FACTOR, MAP_HEIGHT_SCALE
#include "GameLogic/TerrainHeightSampling.h"
#include "GameLogic/WorldHeightMapData.h"
#include "WWMath/vector3.h"

UnsignedByte TerrainHeightSampling::getClipHeight(WorldHeightMapData *clipMap, Int x, Int y)
{
	Int xextent = clipMap->getXExtent() - 1;
	Int yextent = clipMap->getYExtent() - 1;

	if (x < 0) 
		x = 0; 
	else if (x > xextent) 
		x = xextent;

	if (y < 0) 
		y = 0; 
	else if (y > yextent) 
		y = yextent;

	return clipMap->getDataPtr()[x + y*clipMap->getXExtent()];
}

Real TerrainHeightSampling::getHeightMapHeight(WorldHeightMapData *clipMap, WorldHeightMapData *logicHeightMap, Real x, Real y, Coord3D* normal)
{

  if ( !logicHeightMap )
  {
		if (normal)
		{	
			// return a default normal pointing up
			normal->x = 0.0f;
			normal->y = 0.0f;
			normal->z = 1.0f;
		}
		return 0;
  }

  
	float height;

	//	3-----2
	//  |    /|
	//  |  /  |
	//	|/    |
	//  0-----1
	//Find surrounding grid points
	
	const Real MAP_XY_FACTOR_INV = 1.0f / MAP_XY_FACTOR;

	float xdiv = x * MAP_XY_FACTOR_INV;
	float ydiv = y * MAP_XY_FACTOR_INV;

	float ixf = FAST_REAL_FLOOR(xdiv);
	float iyf = FAST_REAL_FLOOR(ydiv);

	float fx = xdiv - ixf; //get fraction
	float fy = ydiv - iyf; //get fraction

	// since ixf & iyf are already floor'ed, we can use the fastest f->i conversion we have...
	Int	ix = fast_float2long_round(ixf) + logicHeightMap->getBorderSizeInline();
	Int	iy = fast_float2long_round(iyf) + logicHeightMap->getBorderSizeInline();
	Int xExtent = logicHeightMap->getXExtent();

	// Check for extent-3, not extent-1: we go into the next row/column of data for smoothed triangle points, so extent-1
	// goes off the end...
	if (ix > (xExtent-3) || iy > (logicHeightMap->getYExtent()-3) || iy < 1 || ix < 1)
	{	
		// sample point is not on the heightmap
		if (normal)
		{	
			// return a default normal pointing up
			normal->x = 0.0f;
			normal->y = 0.0f;
			normal->z = 1.0f;
		}
		return getClipHeight(clipMap, ix, iy) * MAP_HEIGHT_SCALE;
	}

	const UnsignedByte* data = logicHeightMap->getDataPtr();
	int idx = ix + iy*xExtent;
	float p0 = data[idx];
	float p2 = data[idx + xExtent + 1];
	if (fy > fx) // test if we are in the upper triangle
	{	
		float p3 = data[idx + xExtent];
		height = (p3 + (1.0f-fy)*(p0-p3) + fx*(p2-p3)) * MAP_HEIGHT_SCALE;
	}
	else
	{	
		// we are in the lower triangle
		float p1 = data[idx + 1];
		height = (p1 + fy*(p2-p1) + (1.0f-fx)*(p0-p1)) * MAP_HEIGHT_SCALE;
	}

//  DEBUG_ASSERTCRASH( height < 30, ("SOMEBODY THINKS THE CLIENT HEIGHTMAP IS GOOD ENOUGH FOR LOGIC SAMPLING."));

	if (normal) {
		//		9		  8
		//
		//10	3-----2		7
		//	  |    /|
		//	  |  /  |
		//		|/    |
		//11	0-----1		6
		//
		//		4			5
		//Find surrounding grid points for smoothed normals.
 		int idx4 = ix + (iy-1)*xExtent;
 		int idx0 = ix + iy*xExtent;
 		int idx3 = ix + iy*xExtent+xExtent;
		int idx9 = ix + (iy+2)*xExtent;
		UnsignedByte d0, d1, d2, d3, d4, d5, d6, d7, d8, d9, d10, d11;
		d0 = data[idx0];
		d1 = data[idx0+1];
		d2 = data[idx3+1];
		d3 = data[idx3];
		d4 = data[idx4];
		d5 = data[idx4+1];
		d6 = data[idx0+2];
		d7 = data[idx3+2];
		d8 = data[idx9+1];
		d9 = data[idx9];
		d10 = data[idx3-1];
		d11 = data[idx0-1];

		Real deltaZ_X0 = d1-d11;
		Real deltaZ_X1 = d6-d0;
		Real deltaZ_X2 = d7-d3;
		// Corner 3's X slope is 10 to 2; EA's text repeated corner 1's d6-d0 and left d10 unused.
		Real deltaZ_X3 = d2-d10;

		Real deltaZ_Y0 = d3-d4;
		Real deltaZ_Y1 = d2-d5;
		Real deltaZ_Y2 = d8-d1;
		Real deltaZ_Y3 = d9-d0;

		// Interpolate to get the smoothed valued.  Bilinear: blend along x on the bottom edge (0 to 1)
		// and the top edge (3 to 2), then along y.  EA's text blended 0 with 3 by fx, which handed
		// corner 1's slopes the weight of corner 3 and the other way round.
		Real deltaZ_X_Bottom = deltaZ_X0*(1.0f-fx) + fx*deltaZ_X1;
		Real deltaZ_X_Top = deltaZ_X3*(1.0f-fx) + fx*deltaZ_X2;
		Real deltaZ_X = deltaZ_X_Bottom*(1.0-fy) + fy*deltaZ_X_Top;

		Real deltaZ_Y_Bottom = deltaZ_Y0*(1.0f-fx) + fx*deltaZ_Y1;
		Real deltaZ_Y_Top = deltaZ_Y3*(1.0f-fx) + fx*deltaZ_Y2;
		Real deltaZ_Y = deltaZ_Y_Bottom*(1.0-fy) + fy*deltaZ_Y_Top;



			Vector3 l2r, n2f, normalAtTexel;
			l2r.Set(2*MAP_XY_FACTOR/MAP_HEIGHT_SCALE, 0, deltaZ_X);
			n2f.Set(0, 2*MAP_XY_FACTOR/MAP_HEIGHT_SCALE, deltaZ_Y);
			Vector3::Normalized_Cross_Product(l2r,n2f, &normalAtTexel);
			normal->x = normalAtTexel.X;
			normal->y = normalAtTexel.Y;
			normal->z = normalAtTexel.Z;

	}


	return height;
}

Bool TerrainHeightSampling::isClearLineOfSight(WorldHeightMapData *renderMap, WorldHeightMapData *logicHeightMap, Real maxHeight, const Coord3D& pos, const Coord3D& posOther)
{
	if (renderMap == NULL)
		return false;	// doh. should not happen.


#define DO_BRESENHAM
#ifdef DO_BRESENHAM

	/*
		this is WAY faster, though not quite as accurate... however, the inaccuracy
		is pretty minimal, so we really should force other code to live with it. (srj)
	*/
	const Real MAP_XY_FACTOR_INV = 1.0f / MAP_XY_FACTOR;

	Int borderSize = logicHeightMap->getBorderSizeInline();
	Int start_x = REAL_TO_INT_FLOOR(pos.x * MAP_XY_FACTOR_INV) + borderSize;
	Int start_y = REAL_TO_INT_FLOOR(pos.y * MAP_XY_FACTOR_INV) + borderSize;
	Int end_x = REAL_TO_INT_FLOOR(posOther.x * MAP_XY_FACTOR_INV) + borderSize;
	Int end_y = REAL_TO_INT_FLOOR(posOther.y * MAP_XY_FACTOR_INV) + borderSize;
	Int delta_x = abs(end_x - start_x);			// The difference between the x's
	Int delta_y = abs(end_y - start_y);			// The difference between the y's
	Int x = start_x;												// Start x off at the first pixel
	Int y = start_y;												// Start y off at the first pixel

	Int xinc1, xinc2;
	if (end_x >= start_x)								// The x-values are increasing
	{
		xinc1 = 1;
		xinc2 = 1;
	}
	else																// The x-values are decreasing
	{
		xinc1 = -1;
		xinc2 = -1;
	}

	Int yinc1, yinc2;
	if (end_y >= start_y)               // The y-values are increasing
	{
		yinc1 = 1;
		yinc2 = 1;
	}
	else																// The y-values are decreasing
	{
		yinc1 = -1;
		yinc2 = -1;
	}

	Int den, num, numadd, numpixels;

	Bool checkY = true;
	if (delta_x >= delta_y)							// There is at least one x-value for every y-value
	{
		xinc1 = 0;												// Don't change the x when numerator >= denominator
		yinc2 = 0;												// Don't change the y for every iteration
		den = delta_x;
		num = delta_x / 2;
		numadd = delta_y;
		numpixels = delta_x;							// There are more x-values than y-values
	}
	else																// There is at least one y-value for every x-value
	{
		checkY = false;
		xinc2 = 0;												// Don't change the x for every iteration
		yinc1 = 0;												// Don't change the y when numerator >= denominator
		den = delta_y;
		num = delta_y / 2;
		numadd = delta_x;
		numpixels = delta_y;							// There are more y-values than x-values
	}

	Real nsInv = 1.0f / numpixels;
	Real z = pos.z;
	Real dz = posOther.z - z;
	Real zinc = dz * nsInv;

	Bool result = true;
	const UnsignedByte* data = logicHeightMap->getDataPtr();
	Int xExtent = logicHeightMap->getXExtent();
	Int yExtent = logicHeightMap->getYExtent();
	for (Int curpixel = 0; curpixel < numpixels; curpixel++)
	{
		if (x < 0 || 
				y < 0 ||
				x >= xExtent-1 ||
				y >= yExtent-1)
		{
			// once we go off the map, we're done
			break;
		}

		Int idx = x + y*xExtent;
		float height = data[idx];
		height = __max(height, data[idx + 1]);
		height = __max(height, data[idx + xExtent]);
		height = __max(height, data[idx + xExtent + 1]);
		height *= MAP_HEIGHT_SCALE;

		// if terrainHeight > z, we can't see, so punt.
		// add a little fudge to account for slop.
		const Real LOS_FUDGE = 0.5f;
		if (height > z + LOS_FUDGE)
		{
			result = false;
			break;
		}

		// we're above the max height of the terrain and still looking up, so we're done.
		// (don't bother for reverse test, since that doesn't generally happen)
		if (z >= maxHeight && zinc > 0.0f)
		{
			break;
		}

		z += zinc;

		// continue with the maintenance.
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
	
	return result;

#else

	// walk a line from obj to objOther and
	// find the highest point in between 'em. while
	// we're doing this, also estimate the point on the
	// line at the same x,y as the high-terrain-point.

	Real fx = pos.x;
	Real fy = pos.y;
	Real fz = pos.z;
	Real fdx = posOther.x - fx;
	Real fdy = posOther.y - fy;
	Real fdz = posOther.z - fz;

	// What's the largest step size that will be accurate enough?
	// Currently we use a step size of about 2 "feet", which
	// seems acceptable accuracy. If performance here is inadequate,
	// we can try increasing the step size, but be sure to retest
	// accuracy.
	Real len = ceilf(sqrtf(fdx*fdx + fdy*fdy));
	const Real STEP_LEN = 2.0f;
	Int numSteps = REAL_TO_INT_CEIL(len / STEP_LEN);
	if (numSteps < 1) numSteps = 1;
	Real fnsInv = 1.0f / numSteps;
	Real fxinc = fdx * fnsInv;
	Real fyinc = fdy * fnsInv;
	Real fzinc = fdz * fnsInv;
	while (numSteps--)
	{
		Real terrainHeight = getHeightMapHeight( fx, fy, NULL );

		// if terrainHeight > fz, we can't see, so punt.
		// add a little fudge to account for slop.
		const Real LOS_FUDGE = 0.5f;
		if (terrainHeight > fz + LOS_FUDGE)
		{
			return false;
		}

		// we're above the max height of the terrain and still looking up, so we're done.
		// (don't bother for reverse test, since that doesn't generally happen)
		if (fz >= maxHeight && fzinc > 0.0f)
		{
			return true;
		}

		fx += fxinc;
		fy += fyinc;
		fz += fzinc;

	}

	return true;
#endif
}

Real TerrainHeightSampling::getMaxCellHeight(WorldHeightMapData *renderMap, WorldHeightMapData *logicHeightMap, Real x, Real y)
{
	float p0,p1,p2,p3;
	float height;

	//	3-----2
	//  |    /|
	//  |  /  |
	//	|/    |
	//  0-----1
	//Find surrounding grid points

	if (renderMap == NULL)
	{	//sample point is not on the heightmap
		return 0.0f;	//return default height
	}



	Int offset = 1;
	Int iX = x/MAP_XY_FACTOR;
	Int iY = y/MAP_XY_FACTOR;
	iX += logicHeightMap->getBorderSizeInline();
	iY += logicHeightMap->getBorderSizeInline();
	if (iX<0) iX = 0;
	if (iY<0) iY = 0;
	if (iX >= (logicHeightMap->getXExtent()-1)) {
		iX = logicHeightMap->getXExtent()-2;
	}
	if (iY >= (logicHeightMap->getYExtent()-1)) {
		iY = logicHeightMap->getYExtent()-2;
	}
	UnsignedByte *data = logicHeightMap->getDataPtr();
	p0=data[iX+iY*logicHeightMap->getXExtent()]*MAP_HEIGHT_SCALE;
	p1=data[(iX+offset)+iY*logicHeightMap->getXExtent()]*MAP_HEIGHT_SCALE;
	p2=data[(iX+offset)+(iY+offset)*logicHeightMap->getXExtent()]*MAP_HEIGHT_SCALE;
	p3=data[iX+(iY+offset)*logicHeightMap->getXExtent()]*MAP_HEIGHT_SCALE;

	height=p0;
	height=__max(height,p1);
	height=__max(height,p2);
	height=__max(height,p3);

	return height;
}

Bool TerrainHeightSampling::isCliffCell(WorldHeightMapData *renderMap, WorldHeightMapData *logicHeightMap, Real x, Real y)
{

	if (renderMap == NULL)
	{	//sample point is not on the heightmap
		return false;
	}


	Int iX = x/MAP_XY_FACTOR;
	Int iY = y/MAP_XY_FACTOR;
	iX += logicHeightMap->getBorderSizeInline();
	iY += logicHeightMap->getBorderSizeInline();
	if (iX<0) iX = 0;
	if (iY<0) iY = 0;
	if (iX >= (logicHeightMap->getXExtent()-1)) {
		iX = logicHeightMap->getXExtent()-2;
	}
	if (iY >= (logicHeightMap->getYExtent()-1)) {
		iY = logicHeightMap->getYExtent()-2;
	}
	return logicHeightMap->getCliffState(iX, iY);
}

void TerrainHeightSampling::findMinMaxHeights(WorldHeightMapData *pMap, Real &minHeight, Real &maxHeight)
{
	//Find min/max values for all terrain heights, useful for rendering optimization
	Int m_mapDX=pMap->getXExtent();
	Int m_mapDY=pMap->getYExtent();
	Int i, j, minHt, maxHt;

	minHt = pMap->getMaxHeightValue();
	maxHt = 0;

	for (j=0; j<m_mapDY; j++) {
		for (i=0; i<m_mapDX; i++) {
			Short cur = pMap->getHeight(i,j);
			if (cur<minHt) minHt = cur;
			if (maxHt<cur) maxHt = cur;
		}
	}
	minHeight = minHt * MAP_HEIGHT_SCALE;
	maxHeight = maxHt * MAP_HEIGHT_SCALE;
}
