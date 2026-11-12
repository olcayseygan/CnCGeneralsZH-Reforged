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
// Modified 2026 by İlyas Akın for the macOS/Linux port: moved here from GeneralsMD/Code/GameEngineDevice/Source/W3DDevice/GameClient/Water/W3DWater.cpp; see NOTICE.md and the git history.

// FILE: WaterGridMotion.cpp //////////////////////////////////////////////////////////////////////
// Desc:   The water grid's mesh motion, moved out of WaterRenderObjClass::update (T1c, defect 17).
///////////////////////////////////////////////////////////////////////////////////////////////////

/* The body is W3DWater.cpp's text, with only the substitutions WaterGridMotion.h lists. */

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "Common/GameState.h"
#include "Common/GlobalData.h"
#include "Common/MapObject.h"
#include "Common/Xfer.h"
#include "GameLogic/WaterGridMotion.h"

// ------------------------------------------------------------------------------------------------
void WaterGridMotion::updateForLogicFrame( UnsignedInt currLogicFrame, UnsignedInt &lastLogicFrame, Bool doWaterGrid, Bool &meshInMotion,
	MeshPoint *meshData, Int gridCellsX, Int gridCellsY )
{
	// tuned per 30 Hz frame; velocity is per logic frame and gravity already per logic frame squared
	Constants constants;
	constants.atRestVelocity = ConvertRetailPerFrameRate( 1.0f );
	constants.dampening = ConvertRetailPerFrameFactor( 0.93f );
	constants.gravity = TheGlobalData->m_gravity;
	updateForLogicFrame( currLogicFrame, lastLogicFrame, doWaterGrid, meshInMotion, meshData, gridCellsX, gridCellsY, constants );
}

// ------------------------------------------------------------------------------------------------
void WaterGridMotion::updateForLogicFrame( UnsignedInt currLogicFrame, UnsignedInt &lastLogicFrame, Bool doWaterGrid, Bool &meshInMotion,
	MeshPoint *meshData, Int gridCellsX, Int gridCellsY, const Constants &constants )
{
	if( lastLogicFrame != currLogicFrame )
	{
		// for vertex animated water we need to update the vector field
		if( doWaterGrid && meshInMotion == TRUE )
		{
			const Real PREFERRED_HEIGHT_FUDGE = 1.0f;		///< this is close enough to at rest
			const Real AT_REST_VELOCITY_FUDGE = constants.atRestVelocity;		///< when we're close enought to at rest height and velocity we will stop
			const Real WATER_DAMPENING = constants.dampening;					///< use with up force of 15.0
			Int i, j;
			Int	mx = gridCellsX+1;
			Int my = gridCellsY+1;
			MeshPoint *pData;

			//
			// we will mark the mesh as clean now ... if any of the fields are still in motion
			// they will continue to mark the mesh as dirty so processing continues next frame
			//
			meshInMotion = FALSE;

			// go through each mesh point and adjust the height according to the velocity
			for( j = 0, pData = meshData; j < (my + 2); j++ )
			{	

				for( i = 0; i < (mx + 2); i++ )
				{

					// only pay attention to mesh points that are in motion
					if( BitTest( pData->status, IN_MOTION ) )
					{

						// DAMPENING to slow the changes down
						pData->velocity *= WATER_DAMPENING;

						// if the height here is below our preferred height, we want to add upward force to counteract it
						if( pData->height < pData->preferredHeight )
							pData->velocity -= constants.gravity * 3.0f;
						else
							pData->velocity += constants.gravity * 3.0f;

						// adjust the height at this grid location according to the current velocity		
						pData->height = pData->height + pData->velocity;

						//
						// if we are close enough to our preferred height and our velocity is small enough
						// this will be our resting location
						//
						if( fabs( pData->height - pData->preferredHeight ) < PREFERRED_HEIGHT_FUDGE &&
								fabs( pData->velocity ) < AT_REST_VELOCITY_FUDGE )
						{

							BitClear( pData->status, IN_MOTION );
							pData->height = pData->preferredHeight;
							pData->velocity = 0.0f;

						}  // end if
						else
						{

							// there is still motion in the mesh, we need to process next frame
							meshInMotion = TRUE;

						}  // end else

					}  // end if

					// on to the next one
					pData++;

				}  // end for i

			}  // end for j

		}  // end if

		// mark the last logic frame we processed on
		lastLogicFrame = currLogicFrame;

	}  // end if, a logic frame has passed

}  // end updateForLogicFrame

///////////////////////////////////////////////////////////////////////////////////////////////////
// WaterGrid: WaterRenderObjClass's grid methods, moved with their members (LOGIC-THREAD M7)
///////////////////////////////////////////////////////////////////////////////////////////////////

// 3D Grid Mesh Water defaults, W3DWater.cpp's
#define WATER_MESH_X_VERTICES	128
#define WATER_MESH_Y_VERTICES	128
#define WATER_MESH_SPACING	MAP_XY_FACTOR	//same as terrain

// ------------------------------------------------------------------------------------------------
WaterGrid::WaterGrid( void )
{
	m_meshData = NULL;
	m_meshDataSize = 0;
	m_meshGeneration = 0;
	m_lastStepFrame = 0;
	m_meshInMotion = FALSE;
	m_doWaterGrid = FALSE;
	m_heightsEnabled = FALSE;
	m_pushed = FALSE;

	m_transform.Make_Identity();
	m_gridOrigin=Vector2(0,0);
	m_gridDirectionX=Vector2(1.0f,0.0f);
	m_gridDirectionY=Vector2(1.0f,0.0f);

	// set by TerrainLogic::init before anything reads them, as W3DTerrainVisual::init set them
	m_minGridHeight = 0.0f;
	m_maxGridHeight = 0.0f;
	m_gridChangeMaxRange = 0.0f;
	m_gridChangeAtt0 = 0.0f;
	m_gridChangeAtt1 = 0.0f;
	m_gridChangeAtt2 = 0.0f;

	m_gridCellSize=WATER_MESH_SPACING;
	m_gridCellsX=WATER_MESH_X_VERTICES;
	m_gridCellsY=WATER_MESH_Y_VERTICES;
}

// ------------------------------------------------------------------------------------------------
WaterGrid::~WaterGrid( void )
{
	if (m_meshData)
		delete [] m_meshData;
	m_meshData = NULL;
	m_meshDataSize = 0;
}

// ------------------------------------------------------------------------------------------------
void WaterGrid::reset( void )
{

	// a new game, or a load, steps on its first frame whatever frame the last game stopped on
	m_lastStepFrame = 0;

	// for vertex animated water mesh reset the values
	if( m_meshData)
	{
		Int i, j;
		MeshPoint *pData;
		Int	mx = m_gridCellsX + 1;
		Int my = m_gridCellsY + 1;

		// go through each mesh point and adjust the height according to the velocity
		for( j = 0, pData = m_meshData; j < (my + 2); j++ )
		{

			for( i = 0; i < (mx + 2); i++ )
			{

				// areset grid values for this cell
				pData->velocity = 0.0f;
				pData->height = 0.0f;
				pData->preferredHeight = 0.0f;
				pData->status = 0;	// AT_REST

				// on to the next one
				pData++;

			}  // end for i

		}  // end for j

		// mesh data is no longer in motion
		m_meshInMotion = FALSE;

	}  // end if, water type 3

}

// ------------------------------------------------------------------------------------------------
void WaterGrid::allocateMesh( void )
{
	//contains the current deformed water surface z(height) values.  With 1 vertex invisible border
	//around surface to speed up normal calculations.
	m_meshDataSize = (m_gridCellsX+1+2)*(m_gridCellsY+1+2);
	m_meshData=NEW MeshPoint[ m_meshDataSize ];
	memset(m_meshData,0,sizeof(MeshPoint)*(m_gridCellsX+1+2)*(m_gridCellsY+1+2));
	reset();
	++m_meshGeneration;
}

// ------------------------------------------------------------------------------------------------
void WaterGrid::enable( Bool state )
{
	m_heightsEnabled = state;
	m_doWaterGrid = state;
	m_pushed = FALSE;

	if (state && m_meshData == NULL)
		allocateMesh();
}

// ------------------------------------------------------------------------------------------------
void WaterGrid::disableMotion( void )
{
	m_doWaterGrid = FALSE;
	m_pushed = FALSE;
}

// ------------------------------------------------------------------------------------------------
void WaterGrid::update( UnsignedInt logicFrame )
{
	WaterGridMotion::updateForLogicFrame( logicFrame, m_lastStepFrame, m_doWaterGrid, m_meshInMotion, m_meshData, m_gridCellsX, m_gridCellsY );
}

// ------------------------------------------------------------------------------------------------
void WaterGrid::setHeightClamps(Real minz, Real maxz)
{
	m_minGridHeight = minz;
	m_maxGridHeight = maxz;
}

// ------------------------------------------------------------------------------------------------
void WaterGrid::addVelocity( Real worldX, Real worldY,
														 Real zVelocity, Real preferredHeight )
{

	if( m_doWaterGrid)
	{
		Real gx,gy;
		Real minX,maxX,minY,maxY;
		Int x,y;
		MeshPoint *meshPoint;
		m_pushed = TRUE;

		//check if center falls within grid bounds
		if (worldToGridSpace(worldX, worldY, gx, gy))
		{

			//find extents of influence
			minX = floorf(gx - m_gridChangeMaxRange);
			if (minX < 0 )
				minX = 0;	//clamp extent to fall within box
			maxX = ceilf(gx + m_gridChangeMaxRange);
			if (maxX > m_gridCellsX)
				maxX = m_gridCellsX;	//clamp extent to fall within box

			minY = floorf(gy - m_gridChangeMaxRange);
			if (minY < 0 )
				minY = 0;	//clamp extent to fall within box
			maxY = ceilf(gy + m_gridChangeMaxRange);
			if (maxY > m_gridCellsY)
				maxY = m_gridCellsY;	//clamp extent to fall within box

			for (y=minY; y<=maxY; y++)
			{
				for (x=minX; x<=maxX; x++)
				{

					// get the mesh point that we're concerned with
					meshPoint = &m_meshData[ (y + 1) * (m_gridCellsX + 1 + 2) + x + 1 ];

					// we now have a new preferred height
					meshPoint->preferredHeight = preferredHeight;

					//
					// set the velocity of this point based on the distance from the center of the
					// "core" point for this call
					//
					// The one caller, WaveGuideUpdate, pushes every logic frame, so the push is an
					// acceleration: per frame squared, a quarter of the 30 Hz amount.  WaterVelocity's
					// parse (per second to per frame) took one half, this is the other.
					meshPoint->velocity = meshPoint->velocity + ConvertRetailPerFrameRate( zVelocity );

					// this point is now "in motion"
					BitSet( meshPoint->status, WaterGridMotion::IN_MOTION );

				}
			}

			//
			// the mesh data is now dirty, we need to pass through the velocity field
			// during an update phase to update the positions
			//
			m_meshInMotion = TRUE;

		}

	}  // end if, water type is 3

}

// ------------------------------------------------------------------------------------------------
void WaterGrid::changeHeight(Real wx, Real wy, Real delta)
{
	Real gx,gy;
	Real *oldData;
	Real newData;
	Real distance;
	Real minX,maxX,minY,maxY;
	Int x,y;

	//check if center falls within grid bounds
	if (worldToGridSpace(wx, wy, gx, gy))
	{	//find extents of influence
		minX = floorf(gx - m_gridChangeMaxRange);
		if (minX < 0 )
			minX = 0;	//clamp extent to fall within box
		maxX = ceilf(gx + m_gridChangeMaxRange);
		if (maxX > m_gridCellsX)
			maxX = m_gridCellsX;	//clamp extent to fall within box

		minY = floorf(gy - m_gridChangeMaxRange);
		if (minY < 0 )
			minY = 0;	//clamp extent to fall within box
		maxY = ceilf(gy + m_gridChangeMaxRange);
		if (maxY > m_gridCellsY)
			maxY = m_gridCellsY;	//clamp extent to fall within box

		for (y=minY; y<=maxY; y++)
		{
			for (x=minX; x<=maxX; x++)
			{	oldData = &m_meshData[(y+1)*(m_gridCellsX+1+2)+x+1].height;
				distance = (gx - (Real)x)*(gx - (Real)x) + (gy - (Real)y)*(gy - (Real)y);
				distance = sqrt(distance);
				newData = *oldData + 1.0f/(m_gridChangeAtt0+m_gridChangeAtt1*distance+distance*distance*m_gridChangeAtt2)*delta;
				//Clamp to min/max values
				if (newData < m_minGridHeight)
					newData = m_minGridHeight;
				if (newData > m_maxGridHeight)
					newData = m_maxGridHeight;
				*oldData = newData;
			}
		}
	}
}

// ------------------------------------------------------------------------------------------------
void WaterGrid::setChangeAttenuationFactors(Real a, Real b, Real c, Real range)
{
	m_gridChangeAtt0 = a;
	m_gridChangeAtt1 = b;
	m_gridChangeAtt2 = c;
	m_gridChangeMaxRange = range/m_gridCellSize;	//convert range to grid space
}

// ------------------------------------------------------------------------------------------------
void WaterGrid::setTransform(Real angle, Real x, Real y, Real z)
{
	m_gridDirectionX = Vector2(1.0f,0.0f);

	m_gridOrigin.X = x;
	m_gridOrigin.Y = y;

	Matrix3D xform(1);
	xform.Rotate_Z(angle);

	m_gridDirectionX.X = xform.Get_X_Vector().X;
	m_gridDirectionX.Y = xform.Get_X_Vector().Y;

	m_gridDirectionY.X = xform.Get_Y_Vector().X;
	m_gridDirectionY.Y = xform.Get_Y_Vector().Y;

	xform.Set_Translation(Vector3(x,y,z));
	m_transform = xform;
}

// ------------------------------------------------------------------------------------------------
/** As EA's: only the matrix moves.  The origin and axes worldToGridSpace uses stay where the angle
	* version put them; the scripts that raise and lower the grid change only the Z, which getHeight
	* adds. */
// ------------------------------------------------------------------------------------------------
void WaterGrid::setTransform(const Matrix3D *transform )
{

	if( transform )
		m_transform = *transform;

}

// ------------------------------------------------------------------------------------------------
void WaterGrid::getTransform(Matrix3D *transform ) const
{

	if( transform )
		*transform = m_transform;

}

// ------------------------------------------------------------------------------------------------
void WaterGrid::setResolution(Real gridCellsX, Real gridCellsY, Real cellSize)
{
	m_gridCellSize=cellSize;

	// EA compared m_gridCellsY with itself: a change in Y alone is not a change, and is kept
	if (m_gridCellsX != gridCellsX || m_gridCellsY != m_gridCellsY)
	{	//resolutoin has changed
		m_gridCellsX=gridCellsX;
		m_gridCellsY=gridCellsY;

		if (m_meshData)
		{

			delete [] m_meshData;//free previously allocated grid and allocate new size
			m_meshData = NULL;	 // must set to NULL so that we properly re-allocate
			m_meshDataSize = 0;

			// the render object called its own enableWaterGrid(true) here and put m_doWaterGrid back,
			// which left only the allocation and the cleared m_disableRiver
			m_pushed = FALSE;
			allocateMesh();

		}
	}
}

// ------------------------------------------------------------------------------------------------
void WaterGrid::getResolution( Real *gridCellsX, Real *gridCellsY, Real *cellSize ) const
{

	if( gridCellsX )
		*gridCellsX = m_gridCellsX;
	if( gridCellsY )
		*gridCellsY = m_gridCellsY;
	if( cellSize )
		*cellSize = m_gridCellSize;

}

// ------------------------------------------------------------------------------------------------
/** W3DTerrainVisual::getWaterGridHeight and the render object's getGridVertexHeight together: the
	* answer is TRUE for a point inside an enabled grid, and the height is only written when there is
	* a mesh to read it from.  Get_Position().Z was the transform's Z translation. */
// ------------------------------------------------------------------------------------------------
Bool WaterGrid::getHeight( Real worldX, Real worldY, Real *height ) const
{
	Real gridX, gridY;

	if (m_heightsEnabled && worldToGridSpace(worldX, worldY, gridX, gridY))
	{	//point falls within grid, return correct height
		if (m_meshData)
			*height=m_meshData[(REAL_TO_INT(gridY)+1)*(m_gridCellsX+1+2)+REAL_TO_INT(gridX)+1].height+ m_transform.Get_Z_Translation();
		return TRUE;
	}
	return FALSE;
}

// ------------------------------------------------------------------------------------------------
Bool WaterGrid::worldToGridSpace(Real worldX, Real worldY, Real &gridX, Real &gridY) const
{
	Real dx,dy;
	Real ooGridCellSize = 1.0f/m_gridCellSize;

	dx=worldX - m_gridOrigin.X;
	dy=worldY - m_gridOrigin.Y;
	gridX = ooGridCellSize * (dx * m_gridDirectionX.X + dy * m_gridDirectionX.Y);
	gridY = ooGridCellSize * (dx * m_gridDirectionY.X + dy * m_gridDirectionY.Y);

	if (gridX < 0)
		return FALSE;
	if (gridX > m_gridCellsX-1 )
		return FALSE;
	if (gridY < 0)
		return FALSE;
	if (gridY > m_gridCellsY-1 )
		return FALSE;

	return TRUE;
}

// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void WaterGrid::crc( Xfer *xfer )
{

}  // end crc

// ------------------------------------------------------------------------------------------------
/** Xfer, WaterRenderObjClass's bytes, which W3DTerrainVisual::xfer still writes in its block
	* Version Info:
	* 1: Initial version
	* 2: The motion flag and the grid's height.  EA's version 1 dropped both, so a loaded grid stood
	*    still until the next WaveGuide push and sat at GameData's height whatever a script had raised
	*    it to, and logic's isUnderwater read those heights. */
// ------------------------------------------------------------------------------------------------
void WaterGrid::xfer( Xfer *xfer )
{

	// version
	XferVersion currentVersion = 2;
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );

	// grid cells x
	Int cellsX = m_gridCellsX;
	xfer->xferInt( &cellsX );
	if( cellsX != m_gridCellsX )
	{

		DEBUG_CRASH(( "WaterGrid::xfer - cells X mismatch\n" ));
		throw SC_INVALID_DATA;

	}  // end if

	// grid cells Y
	Int cellsY = m_gridCellsY;
	xfer->xferInt( &cellsY );
	if( cellsY != m_gridCellsY )
	{

		DEBUG_CRASH(( "WaterGrid::xfer - cells Y mismatch\n" ));
		throw SC_INVALID_DATA;

	}  // end if

	// xfer each of the mesh data points
	for( Int i = 0; i < m_meshDataSize; ++i )
	{

		// height
		xfer->xferReal( &m_meshData[ i ].height );

		// velocity
		xfer->xferReal( &m_meshData[ i ].velocity );

		// status
		xfer->xferUnsignedByte( &m_meshData[ i ].status );

		// preferred height
		xfer->xferUnsignedByte( &m_meshData[ i ].preferredHeight );

	}  // end for, i

	if( version >= 2 )
	{

		// still settling: the step skips the whole mesh while this is FALSE
		xfer->xferBool( &m_meshInMotion );

		// the grid's height, which a script's water height moves (TerrainLogic::setWaterHeight)
		Real z = m_transform.Get_Z_Translation();
		xfer->xferReal( &z );
		m_transform.Set_Z_Translation( z );

	}  // end if

}  // end xfer

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void WaterGrid::loadPostProcess( void )
{

}  // end loadPostProcess
