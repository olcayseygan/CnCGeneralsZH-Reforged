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
// Modified 2025-2026 by Olcay Seygan for Zero Hour Reforged; see the git history.
// Modified 2026 by İlyas Akın for the macOS/Linux port; see NOTICE.md and the git history.

////////////////////////////////////////////////////////////////////////////////
//																																						//
//  (c) 2001-2003 Electronic Arts Inc.																				//
//																																						//
////////////////////////////////////////////////////////////////////////////////


#pragma once


#ifndef __HEIGHTMAP_H_
#define __HEIGHTMAP_H_

#include "always.h"
#include "rendobj.h"
#include "w3d_file.h"
#include "dx8vertexbuffer.h"
#include "dx8indexbuffer.h"
#include "dx8wrapper.h"
#include "shader.h"
#include "vertmaterial.h"
#include "Lib/BaseType.h"
#include "Common/GameType.h"
#include "WorldHeightMap.h"
#include "BaseHeightMap.h"

#define VERTEX_BUFFER_TILE_LENGTH	32		//tiles of side length 32 (grid of 33x33 vertices).

class FrustumClass;

// Adjust the triangles to make cliff sides most attractive.  jba.
#define FLIP_TRIANGLES 1


/// Custom render object that draws the heightmap and handles intersection tests.
/**
Custom W3D render object that's used to process the terrain.  It handles
virtually everything to do with the terrain, including: drawing, lighting,
scorchmarks and intersection tests.
*/



class HeightMapRenderObjClass : public BaseHeightMapRenderObjClass
{	

public:

	HeightMapRenderObjClass(void);
	virtual ~HeightMapRenderObjClass(void);

	// DX8_CleanupHook methods
	virtual void ReleaseResources(void);	///< Release all dx8 resources so the device can be reset.
	virtual void ReAcquireResources(void);  ///< Reacquire all resources after device reset.


	/////////////////////////////////////////////////////////////////////////////
	// Render Object Interface (W3D methods)
	/////////////////////////////////////////////////////////////////////////////
	virtual void					Render(RenderInfoClass & rinfo);
	virtual void					On_Frame_Update(void); 

	///allocate resources needed to render heightmap
	virtual int initHeightData(Int width, Int height, WorldHeightMap *pMap, RefRenderObjListIterator *pLightsIterator, Bool updateExtraPassTiles=TRUE);
	virtual Int freeMapResources(void);	///< free resources used to render heightmap
	virtual void updateCenter(CameraClass *camera, RefRenderObjListIterator *pLightsIterator);

	void renderExtraBlendTiles(void);			///< render 3-way blend tiles that have blend of 3 textures.


	virtual void staticLightingChanged(void);
	virtual	void adjustTerrainLOD(Int adj);
	virtual void reset(void);
	virtual void doPartialUpdate(const IRegion2D &partialRange, WorldHeightMap *htMap, RefRenderObjListIterator *pLightsIterator);

	virtual void oversizeTerrain(Int tilesToOversize);

	virtual int updateBlock(Int x0, Int y0, Int x1, Int y1, WorldHeightMap *pMap, RefRenderObjListIterator *pLightsIterator);

protected:
	Int *m_extraBlendTilePositions;	///<array holding x,y tile positions of all extra blend tiles. (used for 3 textures per tile).
	Int m_numExtraBlendTiles;		///<number of blend tiles in m_extraBlendTilePositions.
	Int	m_numVisibleExtraBlendTiles; ///<number rendered last frame.	
	Int m_extraBlendTilePositionsSize;	//<total size of array including unused memory.

	/** The 3-way tiles' vertices, kept from one pass to the next.  They used to be rebuilt into the
		shared dynamic buffer on every pass, and they only change with what m_extraBlendKey holds:
		the terrain's content version, the drawn rectangle, the buffer's capacity, the map and how
		its tiles sit in the terrain texture. */
	/** Subdivided, a tile is 24 indices and DX8IndexBufferClass counts them in an unsigned short, so the
		tiles go out in batches of at most EXTRA_BLEND_SUBDIVIDED_TILES, each its own pair of buffers;
		four of them hold the 10000 tiles the pass is ever allowed.  Two triangles a cell is one batch. */
	enum { EXTRA_BLEND_KEY_SIZE = 8, EXTRA_BLEND_BATCHES = 4, EXTRA_BLEND_SUBDIVIDED_TILES = 65535/24 };
	DX8VertexBufferClass *m_extraBlendVB[EXTRA_BLEND_BATCHES];
	DX8IndexBufferClass *m_extraBlendIB[EXTRA_BLEND_BATCHES];
	Int m_extraBlendCapacity;		///< vertices each batch's vertex buffer holds (tiles times four, or nine subdivided)
	Int m_extraBlendBatches;		///< batches allocated
	WorldHeightMap *m_extraBlendMap;	///< map the buffers were filled from; NULL means refill
	Int m_extraBlendKey[EXTRA_BLEND_KEY_SIZE];
	Int m_extraBlendVertexCount[EXTRA_BLEND_BATCHES];
	Int m_extraBlendIndexCount[EXTRA_BLEND_BATCHES];
	void freeExtraBlendBuffers(void);
	DX8VertexBufferClass	**m_vertexBufferTiles;	///<collection of smaller vertex buffers that make up 1 heightmap
	char	**m_vertexBufferBackup;	///< In memory copy of the vertex buffer data for quick update of dynamic lighting.
	Int m_originX; ///<  Origin point in the grid.  Slides around.
	Int m_originY; ///< Origin point in the grid.  Slides around.
	DX8IndexBufferClass			*m_indexBuffer;	///<indices defining triangles in a VB tile.
	Int	m_numVBTilesX;	///<dimensions of array containing all the vertex buffers 
	Int	m_numVBTilesY;	///<dimensions of array containing all the vertex buffers
	Int m_numVertexBufferTiles;	///<number of vertex buffers needed to store this heightmap
	Int	m_numBlockColumnsInLastVB;///<a VB tile may be partially filled, this indicates how many 2x2 vertex blocks are filled.
	Int	m_numBlockRowsInLastVB;///<a VB tile may be partially filled, this indicates how many 2x2 vertex blocks are filled.

	/** What doTheDynamicLight reads from one light, taken once per relight instead of once per
		vertex.  The relight reads these, the heights and m_vertexBufferBackup, and never the vertex
		buffer it writes, so a pass whose inputs match the last relight's would write the same bytes
		again.  Lights change on logic frames and the terrain draws more often than that. */
	struct DynamicLightInputs
	{
		Int type;
		Int enabled;
		Vector3 position;
		Vector3 spotDirection;
		Vector3 diffuse;
		Vector3 ambient;
		double midRange;	///< double, the way Get_Far_Attenuation_Range hands it to the falloff
		double range;
		Int bounds[8];	///< m_minX, m_minY, m_maxX, m_maxY, then the four m_prev bounds
	};
	struct DynamicLightPass
	{
		WorldHeightMap *map;
		Int drawOrgX;
		Int drawOrgY;
		Int originX;
		Int originY;
		Int numLights;
		DynamicLightInputs lights[MAX_ENABLED_DYNAMIC_LIGHTS];
	};
	DynamicLightPass m_lastRelight;	///< compared with memcmp, so only ever written by memset and memcpy
	Bool m_lastRelightValid;	///< false once anything has rewritten the vertex buffers since that relight

	/** The map's sun per pixel on Direct3D 11 (TERRAIN_RELIGHT_SAMPLING): the tiles and the 3-way tiles
		bake the light without the global lights, and their programs add those from a smooth normal off
		the heights.  Decided at each whole rebuild (sunPerPixelWanted), so the tiles and the draws never
		disagree for more than the frame that notices; off under -d3d9, -headless and Classic, which keep
		EA's per-vertex light. */
	Bool m_sunPerPixel;
	WorldHeightMap *m_sunHeightsMap;	///< map, and its content version, the heights texture last took
	Int m_sunHeightsVersion;
	static Bool sunPerPixelWanted(void);
	void setSunPerPixel(Bool on);	///< the tiles' light from the next rebuild on; the 3-way tiles and roads follow
	Bool prepareSunPerPixel(void);	///< heights and light up for this frame; whether the draws add the sun
public:
	virtual Bool sunPerPixelBaked(void) const { return m_sunPerPixel; }
protected:

	/** Each cell drawn as two by two smaller ones (m_subdivided, TerrainSubdivide.h): nine vertices a
		cell in place of four, their heights off the samples' Catmull-Rom patch held near the cell's
		plane, everything else blended over the old two triangles so a texel lands where it did.  The
		index buffer is fixed and a flipped cell turns its nine vertices a quarter turn, as EA turned
		its four.  On Direct3D 11, not Classic, not -headless, not -noTerrainSubdivide; decided at each
		whole rebuild, which builds the buffers again when the answer moved. */
	static Bool subdivideWanted(void);
	Int vertsPerCell(void) const { return m_subdivided ? 9 : 4; }
	Int polysPerCell(void) const { return m_subdivided ? 8 : 2; }
	void allocateTileBuffers(Int x, Int y);	///< the vertex buffer tiles, their backups and the index buffer, at m_subdivided
	Bool setSubdivided(Bool on);	///< TRUE when it changed, and the buffers have to be built again
	/// A cell's four corners as updateVB built them (turned when flipped) into its nine subdivided vertices.
	void writeSubdividedCell(VERTEX_FORMAT *dst, const VERTEX_FORMAT corners[4], Bool flip, Int cellX, Int cellY);

	/** Each vertex buffer tile's lowest and highest ground, so a pass draws only the tiles its camera
		sees (tileInView).  Taken where the tile is rebuilt; the drawn ground never leaves its cells'
		corner range, subdivided or not. */
	std::vector<Real> m_tileMinZ;
	std::vector<Real> m_tileMaxZ;
	void takeTileBounds(Int tileX, Int tileY);
	Bool tileInView(const FrustumClass &frustum, Int tileX, Int tileY) const;

	UnsignedInt doTheDynamicLight(VERTEX_FORMAT *vb, VERTEX_FORMAT *vbMirror, Vector3*light, Vector3*normal, const DynamicLightInputs lights[], const Int lightsHere[], Int numHere);
	//
	// Gets the index that corresponds to the data.  For example, if the columns are shifted by 3,
	// index 3 is actually the first row of polygons, or 0.  Yes it is confusing, but it makes
	// sliding the map 10x faster.
	//
	// Inline because the vertex fill loops call these eight or more times per vertex, on a whole
	// tile at a time.  Out of line they were real calls, and the compiler could not see that the
	// repeated calls with the same argument all return the same thing.
	//
	Int getXWithOrigin(Int x) const
	{
		x -= m_originX;
		if (x<0) x+= m_x-1;
		if (x>= m_x-1) x-=m_x-1;
		if (x<0) x = 0;
		if (x>= m_x-1) x=m_x-1;
		return x;
	}

	Int getYWithOrigin(Int y) const
	{
		y -= m_originY;
		if (y<0) y+= m_y-1;
		if (y>= m_y-1) y-=m_y-1;
		if (y<0) y = 0;
		if (y>= m_y-1) y=m_y-1;
		return y;
	}
	///update vertex diffuse color for dynamic lights inside given rectangle
	Int updateVBForLight(DX8VertexBufferClass *pVB, char *data, Int x0, Int y0, Int x1, Int y1, Int originX, Int originY, W3DDynamicLight *pLights[], Int numLights);
	///update vertex buffer vertices inside given rectangle
	Int updateVB(DX8VertexBufferClass	*pVB, char *data, Int x0, Int y0, Int x1, Int y1, Int originX, Int originY, WorldHeightMap *pMap, RefRenderObjListIterator *pLightsIterator);
	///upate vertex buffers associated with the given rectangle
	void initDestAlphaLUT(void);	///<initialize water depth LUT stored in m_destAlphaTexture
	void renderTerrainPass(CameraClass *pCamera);	///< renders additional terrain pass.
	Int	getNumExtraBlendTiles(Bool visible) { return visible?m_numVisibleExtraBlendTiles:m_numExtraBlendTiles;}
	void freeIndexVertexBuffers(void);


  
};

#endif  // end __HEIGHTMAP_H_
