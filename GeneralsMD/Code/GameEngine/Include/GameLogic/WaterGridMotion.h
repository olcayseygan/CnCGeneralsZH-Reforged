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
// Modified 2026 by İlyas Akın for the macOS/Linux port: parts come from GeneralsMD/Code/GameEngineDevice/Include/W3DDevice/GameClient/W3DWater.h, the rest is new code, copyright 2026 İlyas Akın; see NOTICE.md and the git history.

// FILE: WaterGridMotion.h ////////////////////////////////////////////////////////////////////////
// Desc:   The water grid's mesh motion, one step per logic frame (T1c, defect 17).
///////////////////////////////////////////////////////////////////////////////////////////////////

/* The water grid is simulation state kept in a render object.  On a map with a WaveGuide1 waypoint,
	 WaveGuideUpdate pushes velocity into WaterRenderObjClass's mesh and TerrainLogic::isUnderwater reads
	 the mesh's heights back, for pathfinding, locomotors, the partition manager and more.  Between the
	 two, the mesh moves: every point in motion is damped, pulled towards its preferred height and moved.

	 EA stepped that motion from the render object's update, on the client pass, at most once a pass,
	 gated on the logic frame having changed.  EA's loop ran one client pass per logic frame, so that was
	 one step a frame.  This fork's catch-up runs several logic frames behind one client pass, and a
	 machine that caught up k frames stepped the mesh once: grid heights came to depend on the machine's
	 frame-rate history (defect 17).

	 GameLogic::update now steps it, at its top, before anything in the frame reads or writes the grid -
	 where EA's client pass stood, just ahead of the logic frame - through WaterGrid::update.
	 The step is W3DWater.cpp's text unchanged, frame gate and all, but for these substitutions:
	 - currLogicFrame is a parameter where the original read TheGameLogic->getFrame() (0 without one);
	 - the render object's m_doWaterGrid, m_meshInMotion, m_meshData, m_gridCellsX and m_gridCellsY are
		 parameters of the same names without the m_;
	 - WaterRenderObjClass::IN_MOTION and WaterMeshData are this class's IN_MOTION and MeshPoint, which
		 WaterRenderObjClass now uses under its old names (the same members in the same order).
	 Tests/test_water_grid.cpp holds it to the original's own output, and shows the count per frame.

	 The rest of the grid followed the step out of the render object (LOGIC-THREAD M7): WaterGrid below
	 is the mesh, its size, its transform, its clamps and falloff and its two enable flags, owned by
	 TerrainLogic.  Logic reads and writes nothing else; WaterRenderObjClass draws from it. */

#pragma once

#ifndef WATERGRIDMOTION_H
#define WATERGRIDMOTION_H

#include "Lib/BaseType.h"
#include "Common/Snapshot.h"
#include "WWMath/matrix3d.h"
#include "WWMath/vector2.h"

class WaterGridMotion
{
public:

	enum
	{
		IN_MOTION = 0x01		///< WaterRenderObjClass::WaterMeshStatus's
	};

	/// One point of the grid's mesh: WaterRenderObjClass::WaterMeshData, which is this type
	struct MeshPoint
	{
		Real height;										///< height of the 3D mesh at this point
		Real velocity;									///< velocity in Z that this point is moving up and down
		UnsignedByte status;						///< status for this grid point
		UnsignedByte preferredHeight;		///< the hight we prefer to be
	};

	/** Moves every point in motion one step, the first time it is called for a given logic frame; a
		* second call for the same frame does nothing.  meshData holds (gridCellsX+1+2)*(gridCellsY+1+2)
		* points, a border of one around the grid.  lastLogicFrame is the gate, EA's function static: the
		* caller owns it, so a new game or a load starts from a gate of its own. */
	static void updateForLogicFrame( UnsignedInt currLogicFrame, UnsignedInt &lastLogicFrame, Bool doWaterGrid, Bool &meshInMotion,
		MeshPoint *meshData, Int gridCellsX, Int gridCellsY );

	/// The step's per-frame amounts.  The game's are EA's 30 Hz ones converted to the rate and the game
	/// speed, gravity TheGlobalData's; a test passes the retail-speed ones to step as its golden was recorded.
	struct Constants
	{
		Real atRestVelocity;		///< below this speed, near the preferred height, a point stops
		Real dampening;					///< velocity factor per frame
		Real gravity;						///< per frame squared
	};

	/// the same step with the amounts given instead of the game's
	static void updateForLogicFrame( UnsignedInt currLogicFrame, UnsignedInt &lastLogicFrame, Bool doWaterGrid, Bool &meshInMotion,
		MeshPoint *meshData, Int gridCellsX, Int gridCellsY, const Constants &constants );

};

/** The vertex animated water grid, as simulation state.  This was WaterRenderObjClass's mesh and
	* settings and W3DTerrainVisual's m_isWaterGridRenderingEnabled; every body is the original text with
	* the render object's members renamed to these.  The two flags stay two, as they were:
	* m_heightsEnabled is the one getHeight answers to (W3DTerrainVisual's), m_doWaterGrid the one the
	* motion and the pushes answer to (the render object's), and a new map's load clears only the second.
	* The renderer reads this at draw time, outside the tick for now; making that read safe against a
	* tick running beside the draw is a later phase. */
class WaterGrid : public Snapshot
{
public:

	typedef WaterGridMotion::MeshPoint MeshPoint;

	WaterGrid( void );
	virtual ~WaterGrid( void );

	void reset( void );																			///< zero the mesh, as a game's reset did
	void enable( Bool state );															///< what TerrainVisual::enableWaterGrid did to the grid
	void disableMotion( void );															///< what a new map's load did: the render object's enableWaterGrid(false)
	void update( UnsignedInt logicFrame );									///< one step of motion, once per logic frame (WaterGridMotion)
	void setHeightClamps( Real minz, Real maxz );						///< min/max height values allowed in grid
	void setChangeAttenuationFactors( Real a, Real b, Real c, Real range );	///< falloff for changeHeight
	void setTransform( Real angle, Real x, Real y, Real z );	///< position/orientation of grid in space
	void setTransform( const Matrix3D *transform );					///< set transform by matrix (leaves the grid axes alone)
	void getTransform( Matrix3D *transform ) const;					///< get grid transform matrix
	void setResolution( Real gridCellsX, Real gridCellsY, Real cellSize );	///< grid resolution and spacing
	void getResolution( Real *gridCellsX, Real *gridCellsY, Real *cellSize ) const;
	void addVelocity( Real worldX, Real worldY, Real zVelocity, Real preferredHeight );
	void changeHeight( Real x, Real y, Real delta );				///< change height of grid at world point including neighbors within falloff
	Bool getHeight( Real worldX, Real worldY, Real *height ) const;	///< TRUE inside an enabled grid; height untouched without a mesh

	// for the renderer
	Bool isMotionEnabled( void ) const { return m_doWaterGrid; }
	Bool isHeightEnabled( void ) const { return m_heightsEnabled; }
	Bool wasPushed( void ) const { return m_pushed; }				///< a push landed since the last enable (EA's m_disableRiver)
	MeshPoint *getMeshData( void ) { return m_meshData; }
	Int getCellsX( void ) const { return m_gridCellsX; }
	Int getCellsY( void ) const { return m_gridCellsY; }
	Real getCellSize( void ) const { return m_gridCellSize; }
	UnsignedInt getMeshGeneration( void ) const { return m_meshGeneration; }	///< bumps each time the mesh is (re)allocated

protected:

	virtual void crc( Xfer *xfer );
	virtual void xfer( Xfer *xfer );
	virtual void loadPostProcess( void );

private:

	void allocateMesh( void );
	Bool worldToGridSpace( Real worldX, Real worldY, Real &gridX, Real &gridY ) const;

	MeshPoint *m_meshData;					///< heightmap data for 3D Mesh based water
	UnsignedInt m_meshDataSize;			///< size of m_meshData
	UnsignedInt m_meshGeneration;		///< count of allocations, so the renderer knows to rebuild its buffers
	UnsignedInt m_lastStepFrame;		///< the logic frame the motion last stepped on; 0 after a reset
	Bool m_meshInMotion;						///< TRUE once we've messed with velocities and are in motion
	Bool m_doWaterGrid;							///< the render object's flag: motion and pushes
	Bool m_heightsEnabled;					///< W3DTerrainVisual's m_isWaterGridRenderingEnabled: heights
	Bool m_pushed;									///< the render object's m_disableRiver; drawing only, not saved

	Matrix3D m_transform;						///< the render object's Transform: the grid's place in the world
	Vector2	m_gridDirectionX;				///< vector along water grid's x-axis (scaled to world-space)
	Vector2	m_gridDirectionY;				///< vector along water grid's y-axis (scaled to world-space)
	Vector2	m_gridOrigin;						///< unit vector along water grid's x-axis
	Real m_minGridHeight;						///< minimum height value allowed for water mesh
	Real m_maxGridHeight;						///< maximum height value allowed for water mesh
	Real m_gridChangeMaxRange;			///< maximum range of changeHeight() method
	Real m_gridChangeAtt0;
	Real m_gridChangeAtt1;
	Real m_gridChangeAtt2;
	Real m_gridCellSize;						///< world-space width/height of each cell.
	Int  m_gridCellsX;							///< number of cells along width
	Int  m_gridCellsY;							///< number of cells along height

};

#endif // WATERGRIDMOTION_H
