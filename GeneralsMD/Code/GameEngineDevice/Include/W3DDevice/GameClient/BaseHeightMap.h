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

#ifndef __BASE_HEIGHTMAP_H_
#define __BASE_HEIGHTMAP_H_

#include <vector>
#include "always.h"
#include "rendobj.h"
#include "w3d_file.h"
#include "dx8vertexbuffer.h"
#include "dx8indexbuffer.h"
#include "dx8wrapper.h"
#include "shader.h"
#include "vertmaterial.h"
#include "Lib/BaseType.h"
#include "GameLogic/TerrainHeightSampling.h"
#include "Common/GameType.h"
#include "Common/LogicThread.h"
#include "WorldHeightMap.h"
#include "W3DDevice/GameClient/GroundMarks.h"

// The lights one terrain relight takes in, the rest dropped.  Only the vertex path reads it: the
// Direct3D 11 frame lights the ground per pixel and takes BLAST_LIGHT_SLOTS (32) instead.  Was 20.
#define MAX_ENABLED_DYNAMIC_LIGHTS 32
typedef UnsignedByte HeightSampleType;	//type of data to store in heightmap
class W3DTreeBuffer;
class W3DBibBuffer;
class W3DRoadBuffer;
class W3DBridgeBuffer;
class W3DWaypointBuffer;
class W3DTerrainLogic;
class W3DAssetManager;
class SimpleSceneClass;
class W3DShroud;
class W3DPropDrawModuleData;
class W3DPropBuffer;
class W3DTreeDrawModuleData;
class GeometryInfo;

/** LOGIC-THREAD.md phase 3, step 3b: logic's own copy of the map's heights.  A structure flattening its
	* footprint lowers cells inside the tick (W3DTerrainVisual::setRawMapHeight), and the render reads the
	* map beside the tick everywhere it builds, lights and shadows the ground.  So the tick writes this
	* copy and every height logic asks for comes from it, and the map the terrain draws takes the cell
	* after the tick, with terrainHeightChanged. */
class LogicHeightMapData : public WorldHeightMapData
{
public:
	~LogicHeightMapData( void ) { m_data = NULL; }	// m_heights owns them, not the base
	void copyFrom( WorldHeightMapData *map )
	{
		m_width = map->getXExtent();
		m_height = map->getYExtent();
		m_borderSize = map->getBorderSizeInline();
		m_dataSize = m_width * m_height;
		m_heights.assign( map->getDataPtr(), map->getDataPtr() + m_dataSize );
		m_data = m_heights.empty() ? NULL : &m_heights[ 0 ];
	}
private:
	std::vector< UnsignedByte > m_heights;
};
struct SunShadowCasterCull;

#define no_TIMING_TESTS	1

#define no_PRE_TRANSFORM_VERTEX // Don't do this, not a performance win.  jba.

typedef struct {
	Int minX, maxX;
	Int minY, maxY;
} TBounds;


class LightMapTerrainTextureClass; 
class CloudMapTerrainTextureClass;
class W3DDynamicLight;

#define DO_SCORCH 1

#define DO_ROADS 1

#ifdef DO_SCORCH
typedef struct {
	Vector3 location;
	Real		radius;
	Int			scorchType;
} TScorch;
#endif

#define VERTEX_FORMAT VertexFormatXYZDUV2
#define DX8_VERTEX_FORMAT DX8_FVF_XYZDUV2

/// Custom render object that draws the heightmap and handles intersection tests.
/**
Custom W3D render object that's used to process the terrain.  It handles
virtually everything to do with the terrain, including: drawing, lighting,
scorchmarks and intersection tests.
*/
class BaseHeightMapRenderObjClass : public RenderObjClass, public DX8_CleanupHook, public Snapshot
{	

public:

	BaseHeightMapRenderObjClass(void);
	virtual ~BaseHeightMapRenderObjClass(void);

	// DX8_CleanupHook methods
	virtual void ReleaseResources(void);	///< Release all dx8 resources so the device can be reset.
	virtual void ReAcquireResources(void);  ///< Reacquire all resources after device reset.


	/////////////////////////////////////////////////////////////////////////////
	// Render Object Interface (W3D methods)
	/////////////////////////////////////////////////////////////////////////////
	virtual RenderObjClass *	Clone(void) const;
	virtual int						Class_ID(void) const;
	virtual void					Render(RenderInfoClass & rinfo) = 0;
	virtual bool					Cast_Ray(RayCollisionTestClass & raytest); // This CANNOT be Bool, as it will not inherit properly if you make Bool == Int
	virtual void					Get_Obj_Space_Bounding_Sphere(SphereClass & sphere) const;
	virtual void					Get_Obj_Space_Bounding_Box(AABoxClass & aabox) const;


	virtual void					On_Frame_Update(void); 
	virtual void					Notify_Added(SceneClass * scene);

  // Other VIRTUAL methods. [3/20/2003]

	///allocate resources needed to render heightmap
	virtual int initHeightData(Int width, Int height, WorldHeightMap *pMap, RefRenderObjListIterator *pLightsIterator, Bool updateExtraPassTiles=TRUE);
	virtual Int freeMapResources(void);	///< free resources used to render heightmap
	virtual void updateCenter(CameraClass *camera, RefRenderObjListIterator *pLightsIterator);
 	virtual void adjustTerrainLOD(Int adj);
	virtual void doPartialUpdate(const IRegion2D &partialRange, WorldHeightMap *htMap, RefRenderObjListIterator *pLightsIterator) = 0;
	virtual void staticLightingChanged(void);

	/** One terrain cell moved - a building foundation flattening the ground under it, say.
		*
		* This used to go through staticLightingChanged(), which rebuilds and re-lights every drawn
		* vertex on the map: 61 ms on a four-player map, a visible stutter, for one cell.  Instead the
		* cell joins a dirty rectangle that the client flushes once a frame through doPartialUpdate,
		* which is the same call WorldBuilder has always used for exactly this. */
	void terrainHeightChanged(Int x, Int y);

	/** The accumulated dirty rectangle, or FALSE if there is none.  Clears it. */
	Bool takeDirtyRegion(IRegion2D &region);
	virtual void oversizeTerrain(Int tilesToOversize);
	virtual void reset(void);

  void redirectToHeightmap( WorldHeightMap *pMap ) 
  {
    REF_PTR_RELEASE( m_map );
	  REF_PTR_SET(m_map, pMap);	//update our heightmap pointer in case it changed since last call.
  }


	/// TerrainHeightSampling::getClipHeight on this object's map (the text was here; T1 moved it to gameengine).
	inline UnsignedByte getClipHeight(Int x, Int y) const
	{
		return TerrainHeightSampling::getClipHeight(m_map, x, y);
	}

	/// Update the macro texture (pass 3).
	void setTextureLOD(Int lod);	///<change the maximum mip-level sent to the hardware.
	void applyScorchTextureLOD(void);	///<re-apply that level to the scorch texture (it is rebuilt on a device reset)
	void updateMacroTexture(AsciiString textureName);
	void doTextures(Bool flag) {m_disableTextures = !flag;};
	/// Update the diffuse value from static light info for one vertex.
	void doTheLight(VERTEX_FORMAT *vb, Vector3*light, Vector3*normal, RefRenderObjListIterator *pLightsIterator, UnsignedByte alpha);
	void addScorch(Vector3 location, Real radius, Scorches type);
	/// The Battle Marks option: a burn that fades, on the ring's fixed cap.  Nothing without the option.
	void addGroundMark(const Vector3 &location, Real radius, Int type);
	/// The option, and a picture that draws it: Direct3D 11 and not Classic.
	static Bool groundMarksOn(void);
	/// Everything added so far came with the map and is not to be thrown away to make room.
	void markScorchesStatic(void);
	void addTree(DrawableID id, Coord3D location, Real scale, Real angle,
								Real randomScaleAmount,  const W3DTreeDrawModuleData *data);
	void removeAllTrees(void);
	void removeTree(DrawableID id);
	Bool updateTreePosition(DrawableID id, Coord3D location, Real angle);
	void stepTrees(void);	///< W3DTreeBuffer::stepTrees, at the render's sync point
	void takeWaypointsForRender(void);	///< W3DWaypointBuffer::takeReadsForRender, at the render's sync point
	void takePropShroudForRender(void);	///< W3DPropBuffer::takeShroudForRender, at the render's sync point
	void renderTrees(CameraClass * camera); ///< renders the tree buffer.
	Int renderTreeShadowCasters(RenderInfoClass &sunInfo, CameraClass *sceneCamera, const SunShadowCasterCull &cull); ///< the trees drawn into the sun's map, counted

	void addProp(Int id, Coord3D location, Real angle, Real scale, const AsciiString &modelName);
	void removeProp(Int id);
	/// Hand a model's shadow to the tree buffer, which lays its triangles on the ground.
	Bool addModelShadow(DrawableID id, RenderObjClass *robj);
	void removeModelShadow(DrawableID id);
	void removeAllProps(void);

	void unitMoved( Object *unit );
	void notifyShroudChanged(void);
	void removeTreesAndPropsForConstruction(
		const Coord3D* pos, 
		const GeometryInfo& geom,
		Real angle
	);

	/// Add a bib at location.  
	void addTerrainBib(Vector3 corners[4], ObjectID id, Bool highlight);
	void addTerrainBibDrawable(Vector3 corners[4], DrawableID id, Bool highlight);
	/// Remove a bib.  
	void removeTerrainBib(ObjectID id);
	void removeTerrainBibDrawable(DrawableID id);

	/// Removes all bibs.  
	void removeAllTerrainBibs(void);
	/// Remove all highlighting.  
	void removeTerrainBibHighlighting(void);

	W3DShroud *getShroud()	{return m_shroud;}
	W3DBridgeBuffer *getBridgeBuffer()	{return m_bridgeBuffer;}
	void updateShorelineTiles(Int minX, Int minY, Int maxX, Int maxY, WorldHeightMap *pMap);	///<figure out which tiles on this map cross water plane
	void updateShorelineTile(Int X, Int Y, Int Border, WorldHeightMap *pMap);	///<figure out which tiles on this map cross water plane
	void recordShoreLineSortInfos(void);
	void updateViewImpassableAreas(Bool partial = FALSE, Int minX = 0, Int maxX = 0, Int minY = 0, Int maxY = 0);
	void clearAllScorches(void);
	void setTimeOfDay( TimeOfDay tod );
	void loadRoadsAndBridges(W3DTerrainLogic *pTerrainLogic, Bool saveGame); ///< Load the roads from the map objects.
	void worldBuilderUpdateBridgeTowers( W3DAssetManager *assetManager, SimpleSceneClass *scene );							///< for the editor updating of bridge tower visuals
	Int  getStaticDiffuse(Int x, Int y); ///< Gets the diffuse terrain lighting value for a point on the mesh.

	virtual Int	getNumExtraBlendTiles(Bool visible) { return 0;}
	Int getNumShoreLineTiles(Bool visible)	{ return visible?m_numVisibleShoreLineTiles:m_numShoreLineTiles;}
	void setShoreLineDetail(void);	///<update shoreline tiles in case the feature was toggled by user.
	Bool getMaximumVisibleBox(const FrustumClass &frustum,  AABoxClass *box, Bool ignoreMaxHeight);	///<3d extent of visible terrain.
	Real getHeightMapHeight(Real x, Real y, Coord3D* normal) const;	///<return height and normal at given point
	Real getDrawHeight(Real x, Real y) const;	///< getHeightMapHeight from the drawn map, for the render beside a tick
	void heightsLoaded(void) { s_logicHeights.copyFrom(m_map); staticLightingChanged(); }	///< a save wrote every height at once
	UnsignedByte getLogicRawHeight(Int x, Int y) { return s_logicHeights.getHeight(x, y); }	///< a cell of logic's heights (LogicHeightMapData)
	void setLogicRawHeight(Int x, Int y, UnsignedByte height) { s_logicHeights.setRawHeight(x, y, height); }	///< the drawn map takes it with terrainHeightChanged
	Bool isCliffCell(Real x, Real y);	///<return height and normal at given point
	Real getMinHeight(void) const {return m_minHeight;}	///<return minimum height of entire terrain
	Real getMaxHeight(void) const {return m_maxHeight;}	///<return maximum height of entire terrain
	void getDrawnExtent(Region3D *extent);	///<everything the terrain draws, the border ring past the playable map included, lowest to highest ground
	Real getMaxCellHeight(Real x, Real y) const;	///< returns maximum height of the 4 cell corners.
	WorldHeightMap *getMap(void) {return m_map;}	///< returns object holding the heightmap samples - need this for fast access.
	Bool isClearLineOfSight(const Coord3D& pos, const Coord3D& posOther) const;

	Bool getShowImpassableAreas(void) {return m_showImpassableAreas;}
	void setShowImpassableAreas(Bool show) {m_showImpassableAreas = show;}

	Bool showAsVisibleCliff(Int xIndex, Int yIndex) const;
	
	Bool evaluateAsVisibleCliff(Int xIndex, Int yIndex, Real valuesGreaterThanRad);

	Real getViewImpassableAreaSlope(void) const { return m_curImpassableSlope; }
	void setViewImpassableAreaSlope(Real viewSlope) { m_curImpassableSlope = viewSlope; }
	
	Bool doesNeedFullUpdate(void) {return m_needFullUpdate;}


	virtual int updateBlock(Int x0, Int y0, Int x1, Int y1, WorldHeightMap *pMap, RefRenderObjListIterator *pLightsIterator) = 0;

protected:
	// snapshot methods
	virtual void crc( Xfer *xfer );
	virtual void xfer( Xfer *xfer );
	virtual void loadPostProcess( void );

protected:
	Int	m_x;	///< dimensions of heightmap 
	Int	m_y;	///< dimensions of heightmap

#ifdef DO_SCORCH
	// ponytail: one 16-bit buffer pair, so the indices cap the drawn marks at about 680 of radius
	// fifteen whatever MAX_SCORCH_MARKS says (DX8IndexBufferClass counts in an unsigned short); the
	// newest are drawn and the oldest wait in the list.  A second buffer pair when that shows.
	enum { MAX_SCORCH_VERTEX=65535,
					MAX_SCORCH_INDEX=65535,
					MAX_SCORCH_MARKS=1000,
					SCORCH_MARKS_IN_TEXTURE=9,
					SCORCH_PER_ROW = 3};
	DX8VertexBufferClass	*m_vertexScorch;	///<Scorch vertex buffer.
	DX8IndexBufferClass			*m_indexScorch;	///<indices defining a triangles for the scorch drawing.
	TextureClass *m_scorchTexture;	///<Scorch mark texture
	Int			m_curNumScorchVertices;	 ///<number of vertices used in m_vertexScorch.
	Int			m_curNumScorchIndices;	 ///<number of indices used in m_indexScorch.
	TScorch	m_scorches[MAX_SCORCH_MARKS];
	Int			m_numScorches;
	/** How many of the scorches at the front of that list came with the map.
		*
		* The list is full-up on any long match and the oldest entry is thrown away to make room.
		* The oldest entries are the map's own authored scorch marks, so an hour of fighting quietly
		* erased everything the level designer put down. These are never evicted. */
	Int			m_numStaticScorches;
	Int			m_currentTextureLOD;	///< last value setTextureLOD was given; re-applied to textures rebuilt after a device reset

	Int			m_scorchesInBuffer;		///< how many are in the buffers.  If less than numScorches, we need to update
	
	// NOTE: This argument (contrary to most of the rest of the engine), is in degrees, not radians.
	Real		m_curImpassableSlope;

	GroundMarks::Ring m_groundMarks;	///< drawn from the same buffers as the scorches, newest first and in at most half of them
	UnsignedInt m_groundMarksBuilt;		///< logic frame they last went into the buffers
	Bool m_groundMarksPending;				///< one was added since

	void updateGroundMarks(void);	 ///<Decide whether this frame rebuilds the buffers for the marks.
	void updateScorches(void);	 ///<Update m_vertexScorch and m_indexScorch so all scorches will be drawn.
	void allocateScorchBuffers(void);	 ///<allocate static buffers for drawing scorch marks.
	void freeScorchBuffers(void);		 ///< frees up scorch buffers.
	void drawScorches(void);		///< Draws the scorch mark polygons in m_vertexScorch.
#endif
	WorldHeightMap *m_map;
	/** Every height logic asks for, see LogicHeightMapData.  One for the map, not one a render object: the
		* terrain LOD swaps TheTerrainRenderObject between this and the flat one, a choice each machine
		* makes for itself, and logic has to read the same heights through either. */
	static LogicHeightMapData s_logicHeights;
	/** The heights a query reads: logic's copy, or the drawn map for a render inside the overlap window
		* (the tree shadows, the decals), where the tick may be writing the copy. */
	WorldHeightMapData *logicHeightsOrNull( void ) const
	{
		if( m_map == NULL )
			return NULL;
		if( LogicThread::isInsideWindow() )
			return m_map;
		return &s_logicHeights;
	}
	Bool m_useDepthFade;	///<fade terrain lighting under water
	Bool m_updating;
	Vector3 m_depthFade;	///<depth based fall off values for r,g,b
	Bool m_disableTextures;
	Bool m_needFullUpdate; ///< True if lighting changed, and we need to update all instead of what moved.
	IRegion2D m_dirtyRegion;	///< cells changed since the client last rebuilt them, in map coordinates
	Bool m_hasDirtyRegion;		///< ...and whether there are any
	Bool m_doXNextTime; ///< True if we updated y scroll, and need to do x scroll next frame.
	Real	m_minHeight;	///<minimum value of height samples in heightmap
	Real	m_maxHeight;	///<maximum value of height samples in heightmap
	Bool m_showImpassableAreas; ///< If true, shade impassable areas.

	// STL is "smart." This is a variable sized bitset. Very memory efficient.
	std::vector<bool> m_showAsVisibleCliff;

	/** getStaticDiffuse's answer per height sample.  The 3-way blend pass asks for four of them per
		visible tile on every render pass, and the answer only moves with the five heights it reads
		(kept here as the key) and with the lights, whose every input sits in m_staticDiffuseInputs
		as raw bits; a change there bumps m_staticDiffuseGeneration and so retires every cell.
		The inputs are walked once a render frame, on every 3-way pass, and again after anything
		that says the lighting moved (m_staticDiffuseStale), not on every call. */
	struct StaticDiffuseCell
	{
		Int diffuse;
		UnsignedShort generation;	///< 0 is never valid
		UnsignedByte heights[5];	///< centre, left, right, below, above
	};
	std::vector<StaticDiffuseCell> m_staticDiffuseCells;
	std::vector<UnsignedInt> m_staticDiffuseInputs;
	UnsignedShort m_staticDiffuseGeneration;
	Bool m_staticDiffuseStale;				///< lighting or map changed since the last walk
	UnsignedInt m_staticDiffuseFrame;	///< WW3D frame of the last walk
	void refreshStaticDiffuseInputs(void);

	/// Bumped by anything that can change what the ground draws: heights, lighting, the 3-way tile
	/// list.  A cache of terrain vertices that saw the same value has nothing to rebuild.
	Int m_terrainContentVersion;


	ShaderClass m_shaderClass; ///<shader or rendering state for heightmap
	VertexMaterialClass	  	  *m_vertexMaterialClass;	///< vertex shader (lighting) for terrain
	TextureClass *m_stageZeroTexture;	///<primary texture
	TextureClass *m_stageOneTexture;	///<transparent edging texture
	CloudMapTerrainTextureClass *m_stageTwoTexture;	///<Cloud map texture
	TextureClass *m_stageThreeTexture;	///<light/noise map texture
	AsciiString m_macroTextureName; ///< Name for stage 3 texture.
	TextureClass *m_destAlphaTexture;	///< Texture holding destination alpha LUT for water depth.

	W3DTreeBuffer *m_treeBuffer; ///< Class for drawing trees and other alpha objects.
	W3DPropBuffer *m_propBuffer; ///< Class for drawing trees and other alpha objects.
	W3DBibBuffer *m_bibBuffer; ///< Class for drawing trees and other alpha objects.
	W3DWaypointBuffer *m_waypointBuffer; ///< Draws waypoints.
#ifdef DO_ROADS
	W3DRoadBuffer *m_roadBuffer; ///< Class for drawing roads.
#endif
	W3DBridgeBuffer *m_bridgeBuffer;
	W3DShroud *m_shroud;	///< Class for drawing the shroud over terrain.
	struct shoreLineTileInfo
	{	Int m_xy;	//x,y position of tile
		Real verts[4*3];	//position of tile vertices. 4 verts with 3 components.
		Real t0,t1,t2,t3;	//index into water depth alpha LUT.
	};

	//at each x or y coordinate of the map (depending on major axis) we store a list of
	//shoreline blend tiles having the same x or y coordinate.  This makes searching the
	//shoreline tiles much faster.
	struct shoreLineTileSortInfo
	{
		Int	tileStartIndex;	//index within m_shoreLineTilePositions where tiles start
		Int numTiles;		//total tiles at this coordinate.
		UnsignedShort minTileCoordinate;	//lowest coordinate in list
		UnsignedShort maxTileCoordinate;	//highest coordinate in list.
	};

	shoreLineTileInfo *m_shoreLineTilePositions;	///<array holding x,y tile positions of all shoreline terrain.
	Int m_numShoreLineTiles;		///<number of tiles in m_shoreLineTilePositions.
	Int m_numVisibleShoreLineTiles;	///<number of visible tiles.
	Int m_shoreLineTilePositionsSize;	///<total size of array including unused memory.
	Real m_currentMinWaterOpacity;		///<current value inside the gradient lookup texture.
	shoreLineTileSortInfo *m_shoreLineSortInfos;
	Int m_shoreLineSortInfosSize;
	Int m_shoreLineSortInfosXMajor;
	Int m_shoreLineTileSortMaxCoordinate;	///<keep track of coordinate range along axis used for m_shoreLineSortInfos
	Int m_shoreLineTileSortMinCoordinate;
	void initDestAlphaLUT(void);	///<initialize water depth LUT stored in m_destAlphaTexture
	void renderShoreLines(CameraClass *pCamera);	///<re-render parts of terrain that need custom blending into water edge
	void renderShoreLinesSorted(CameraClass *pCamera);	///<optimized version for game usage.
};

extern BaseHeightMapRenderObjClass *TheTerrainRenderObject;
#endif  // end __FLAT_HEIGHTMAP_H_
