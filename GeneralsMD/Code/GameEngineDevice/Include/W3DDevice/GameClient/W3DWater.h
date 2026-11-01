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

// FILE: W3DWater.h ///////////////////////////////////////////////////

#pragma once

#ifndef __W3DWater_H_
#define __W3DWater_H_

#include <vector>

#include "always.h"
#include "rendobj.h"
#include "w3d_file.h"
#include "dx8vertexbuffer.h"			 
#include "dx8indexbuffer.h"
#include "shader.h"
#include "vertmaterial.h"
#include "light.h"
#include "Lib/BaseType.h"
#include "Common/GameType.h"
#include "Common/Snapshot.h"
#include "GameLogic/WaterGridMotion.h"
#include "Platform/RenderTypes.h"
#include "Platform/ModernWaterBuild.h"
#include "Platform/WaterOpticalContext.h"
#include "Platform/WaterProjectedMesh.h"
#include "Platform/WaterHullVisual.h"
#include "Platform/WaterHullVisualLayout.h"
#include "Platform/WaterBowRefinement.h"
#include "Platform/WaterObstacleIndex.h"
#include "Platform/WaterHullModel.h"
#include "Platform/WaterHullPoseCache.h"
#include "Platform/WaterVesselMotion.h"
#include "Platform/WaterFoamHistory.h"
#include "Platform/WaterShoreSource.h"

class Drawable;
class MeshModelClass;

#include <vector>

#define INVALID_WATER_HEIGHT 0.0f	///water height guaranteed to be below all terrain.

#define NUM_BUMP_FRAMES 32	///number of animation frames in bump map
//Offsets in constant register file to Vertex shader constants
#define CV_ZERO 0
#define CV_ONE 1
#define CV_WORLDVIEWPROJ_0 2
#define CV_TEXPROJ_0 6
#define CV_PATCH_SCALE_OFFSET 10

class PolygonTrigger;
class WaterTracksRenderSystem;
class Xfer;
/// Custom render object that draws mirrors, water, and skies.
/**
This render object handles drawing reflected W3D scenes.  It will only work
with rectangular planar surfaces and was tuned with an emphasis on water.
Since skies are only visible in reflections, this code will also
render clouds and sky bodies.
*/
class WaterRenderObjClass : public RenderObjClass
{

public:

	enum WaterType
	{
		WATER_TYPE_0_TRANSLUCENT = 0,	//translucent water, no reflection
		WATER_TYPE_1_FB_REFLECTION,		//legacy frame buffer reflection (non translucent)
		WATER_TYPE_2_PVSHADER,		//pixel/vertex shader, texture reflection
		WATER_TYPE_3_GRIDMESH,		//3D Mesh based water
		WATER_TYPE_MAX		// end of enumeration
	};

	WaterRenderObjClass(void);
	~WaterRenderObjClass(void);

	/////////////////////////////////////////////////////////////////////////////
	// Render Object Interface (W3D methods) 
	/////////////////////////////////////////////////////////////////////////////
	virtual RenderObjClass *	Clone(void) const;
	virtual int						Class_ID(void) const;
	virtual void					Render(RenderInfoClass & rinfo);
/// @todo: Add methods for collision detection with water surface
//	virtual Bool					Cast_Ray(RayCollisionTestClass & raytest);
//	virtual Bool					Cast_AABox(AABoxCollisionTestClass & boxtest);
//	virtual Bool					Cast_OBBox(OBBoxCollisionTestClass & boxtest);
//	virtual Bool					Intersect_AABox(AABoxIntersectionTestClass & boxtest);
//	virtual Bool					Intersect_OBBox(OBBoxIntersectionTestClass & boxtest);

	virtual void					Get_Obj_Space_Bounding_Sphere(SphereClass & sphere) const;
    virtual void					Get_Obj_Space_Bounding_Box(AABoxClass & aabox) const;
	// Get and set static sort level
	virtual int		Get_Sort_Level(void) const		{ return m_sortLevel; }
  	virtual void	Set_Sort_Level(int level)		{ m_sortLevel = level;}

	///allocate W3D resources needed to render water
	void renderWater(void);				///<draw the water surface (flat)
	Int init(Real waterLevel, Real dx, Real dy, SceneClass *parentScene, WaterType type);
	void reset( void );  ///< reset any resources we need to
	void load(void);	///< load/setup any map dependent features
	void update( void ); ///< update phase of the water
#if ZH_MODERN_WATER
	void takeReadsForRender(void);
#endif
	void enableWaterGrid(Bool state);	///< used to active custom water for special maps. (i.e DAM).  The grid is TerrainLogic's WaterGrid; this builds its buffers
	void updateMapOverrides(void);	///< used to update any map specific map overrides for water appearance.
	void setTimeOfDay(TimeOfDay tod); ///<change sky/water for time of day
	void toggleCloudLayer(Bool state)	{	m_useCloudLayer=state;}	///<enables/disables the cloud layer
	void updateRenderTargetTextures(CameraClass *cam);	///< renders into any required textures.	
	void ReleaseResources(void);	///< Release all dx8 resources so the device can be reset.
	void ReAcquireResources(void);  ///< Reacquire all resources after device reset.
	Real getWaterHeight(Real x, Real y);	///<return water height at given point - for use by WB.

	void replaceSkyboxTexture(const AsciiString& oldTexName, const AsciiString& newTextName);
	TextureClass *getSkyTexture(void) const { return m_settings[m_tod].skyTexture; }	///< the time of day's sky, as WaterSet names it
	Bool hasModernVesselFoam(UnsignedInt objectID) const; ///< replacement was submitted for this hull in the current render frame
	Bool isModernWaterMirrorForOwner(UnsignedInt objectID) const; ///< owner's surface foam must not reflect back into the same water plane

protected:
	DX8IndexBufferClass			*m_indexBuffer;	///<indices defining quad
	SceneClass							*m_parentScene;	///<scene to be reflected
	ShaderClass m_shaderClass; ///<shader or rendering state for heightmap
	VertexMaterialClass	  		*m_vertexMaterialClass;	///<vertex lighting material
	VertexMaterialClass			*m_meshVertexMaterialClass; ///<vertex lighting marial for 3D water.
	LightClass					*m_meshLight;				///<light used for 3D Mesh Water.
	TextureClass *m_alphaClippingTexture;	///<used for faked clipping using alpha
	Real	m_dx;	///<x extent of water surface (offset from local center)
	Real	m_dy;	///<y extent of water surface (offset from local center)
	Vector3 m_planeNormal;		///<water plane normal
	Real		m_planeDistance;	///<water plane distance
	Real		m_level;			///<level of water (hack for water)
	Real		m_uOffset;			///<current texture offset on u axis
	Real		m_vOffset;			///<current texture offset on v axis
	Real		m_uScrollPerMs;		///<texels per/ms scroll rate in u direction
	Real		m_vScrollPerMs;		///<texels per/ms scroll rate in v direction
	Int			m_LastUpdateTime;	///<time of last cloud update
	Bool		m_useCloudLayer;	///<flag if clouds are on/off
	WaterType	m_waterType;		///<type of water being used
	Int			m_sortLevel;		///<sort order after main scene is rendered

	//Data used in GeForce3 bump-mapped water (uses direct D3D resources for better
	//performance and compatibility (most of these featues are not supported by W3D).
	struct SEA_PATCH_VERTEX	//vertex structure passed to D3D
	{
		float x,y,z;
		unsigned int c;
		float tu, tv;
	}; 

	LPDIRECT3DDEVICE9 m_pDev;						///<pointer to D3D Device
	LPDIRECT3DVERTEXBUFFER9 m_vertexBufferD3D;		///<D3D vertex buffer
	LPDIRECT3DINDEXBUFFER9	m_indexBufferD3D;	///<D3D index buffer
	Int						m_vertexBufferD3DOffset;	///<location to start writing vertices
	IDirect3DPixelShader9 *	m_dwWavePixelShader;	///<D3D pixel shader
	IDirect3DVertexShader9 * m_dwWaveVertexShader;	///<D3D vertex shader
	IDirect3DVertexDeclaration9 * m_waveVertexDeclaration;	///<what D3D8 carried beside the shader
	Int	m_numVertices;				///<number of vertices in D3D vertex buffer
	Int m_numIndices;				///<number of indices in D3D index buffer
	LPDIRECT3DTEXTURE9 m_pBumpTexture[NUM_BUMP_FRAMES]; ///<animation frames
	LPDIRECT3DTEXTURE9 m_pBumpTexture2[NUM_BUMP_FRAMES]; ///<animation frames
	Int					m_iBumpFrame;	///<current animation frame
	Real				m_bumpFrameAccum;	///<fractional carry, so the flipbook can run slower than one frame per tick
	Real				m_fBumpScale;	///<scales bump map uv perturbation
	TextureClass * m_pReflectionTexture;	///<render target for reflection
	Real m_reflectionLevel;	///<height of the water plane the reflection texture was rendered for
	Vector2 m_reflectionAreaMin;	///<lower left of the screen area the reflection texture covers, -1 to 1
	Vector2 m_reflectionAreaMax;	///<upper right of the same area
	Matrix3D m_reflectionCameraTransform;	///<where the camera stood when the reflection texture was last rendered
	Bool m_reflectionReused;	///<the frame before this one kept the previous reflection texture
	RenderObjClass	*m_skyBox;		///<box around level
	WaterTracksRenderSystem *m_waterTrackSystem;	///<object responsible for rendering water wakes

	/* The grid mesh and its settings are TerrainLogic's WaterGrid (LOGIC-THREAD M7).  The draw reads a
		 copy of it, taken in update() at the sync point (phase 3), so a tick stepping the grid beside the
		 draw changes nothing the draw is reading: the mesh, its size, the two flags and the transform,
		 which syncGridTransform copies into this object's own. */
	typedef WaterGridMotion::MeshPoint WaterMeshData;	///< moved, unchanged, with the step that moves it (T1c)
	UnsignedInt m_meshGeneration;	///< the grid allocation the mesh buffers were built for
	void syncGridTransform(void);	///< take the grid's transform, as the grid setters once set ours
	void takeGridForDraw(void);		///< the copy below, from TerrainLogic's grid
	std::vector<WaterMeshData> m_drawnMesh;
	Int m_drawnCellsX;
	Int m_drawnCellsY;
	Real m_drawnCellSize;
	Bool m_drawnMotionEnabled;
	Bool m_drawnPushed;
	Matrix3D m_drawnTransform;

	Real m_riverVOrigin;
	TextureClass *m_riverTexture;
	TextureClass *m_whiteTexture;		///< a texture containing only white used for NULL pixel shader stages.
	TextureClass *m_waterNoiseTexture;
	Bool m_useModernWater = FALSE; // active render style; synchronized before mirror/surface draws
	TextureClass *m_modernHeightTexture = NULL;
	TextureClass *m_modernBedTexture = NULL;
	Real m_modernWaveTime = 0.0f;
	Int m_modernMeshCells = 32;
	Real m_modernMeshMorph = 0.0f;
	Int m_modernProjectedColumns=160,m_modernProjectedRows=90;
	Int m_modernProjectedWidth=1280,m_modernProjectedHeight=720;
	WaterBowRefinement::Admissions m_modernBowAdmissions;
	uintptr_t m_modernBowPatchOwner=0;
	unsigned m_modernBowPatchPart=0;
	size_t m_modernBowVertices=0,m_modernBowTriangles=0;
	size_t m_modernProjectedBudget=60000;
	Bool m_modernProjectedCacheEnabled=TRUE;
	struct ModernProjectedSample { Real height,damping,obstacle,hullFreeObstacle,offshore,fftDamping,exposure; };
	std::unordered_map<WaterProjectedMesh::WorldKey,ModernProjectedSample,WaterProjectedMesh::WorldHash> m_modernProjectedSamples;
	Vector3 m_modernCamera;
	CameraClass *m_modernRenderCamera = NULL;
	struct ModernWaterObstacle {
		Vector3 position; Real radius,height,major,minor,cosine,sine; Bool box,hasHull=FALSE;
	};
	std::vector<ModernWaterObstacle> m_modernObstacles, m_modernPatchObstacles;
#if ZH_MODERN_WATER
	struct ModernWaterObject {
		Drawable *drawable;
		Vector3 position;
		Real level;
		WaterVesselMotion::Point initialVelocity;
		Bool vessel;
	};
	std::unordered_map<UnsignedInt,ModernWaterObject> m_modernObjectsForDraw;
	std::vector<UnsignedInt> m_modernObstacleObjectIDs;
	UnsignedInt m_modernMotionFrame=0;
	Bool m_modernMotionFrozen=FALSE,m_modernReadsReady=FALSE;
	struct WaterPolygonForDraw {
		std::vector<ICoord3D> points;
		Coord3D center;
		Real radius;
		Int riverStart;
		Bool river;
		uintptr_t owner;
		Bool isWaterArea() const { return TRUE; }
		Bool isRiver() const { return river; }
		Int getNumPoints() const { return Int(points.size()); }
		const ICoord3D *getPoint(Int i) const { if (i<0) i=0; if (i>=getNumPoints()) i=getNumPoints()-1; return &points[i]; }
		Int getRiverStart() const { return riverStart; }
		void getCenterXY(Coord3D *p) const { *p=center; }
		Real getRadius() const { return radius; }
	};
	std::vector<WaterPolygonForDraw> m_waterPolygonsForDraw;
	void takeWaterPolygonsForDraw(void);
	void takeMotionReadsForRender(void);
#endif
	WaterObstacleIndex::Grid m_modernObstacleIndex;
	std::vector<WaterObstacleIndex::Influence> m_modernObstacleInfluences;
	struct ModernHullCache {
		RenderObjClass *model=NULL; // retained until eviction/reset; pointer identity cannot be reused
		UnsignedInt objectID=0, lastUse=0;
		WaterHullPoseCache::Cache poses;
		std::vector<MeshModelClass*> slicedModels; // retained even if an HLOD replaces a child
		WaterHullPoseCache::Cache visualPoses;
		std::vector<MeshModelClass*> visualModels;
		WaterHullVisual::Packet visual={};
		WaterHullModel::Polygon polygon,contact;
		Real forwardSign=1.0f;
		WaterVesselMotion::State motion;
	};
	std::vector<ModernHullCache> m_modernHullCache;
	std::vector<WaterHullModel::Hull> m_modernHulls,m_modernContactHulls;
	std::vector<WaterHullVisual::Packet> m_modernVisualHulls;
	std::vector<UnsignedInt> m_modernHullObjectIDs;
	UnsignedInt m_modernHullSerial=0;
	WaterFoamHistory::History m_modernFoamHistory;
	WaterShoreHistory::History m_modernShoreHistory;
	WaterShoreSource::Batch m_modernShoreSource;
	uint64_t m_modernShoreRevision=1, m_modernShoreAttempted=0;
	Bool m_modernShoreReady=FALSE;
	unsigned m_modernFoamDrawFrame=~0u;
	std::vector<UnsignedInt> m_modernFoamDrawObjectIDs;
	struct ModernFoamTexture {
		Real level=0,timestamp=-1;
		TextureClass *texture=NULL;
		WaterFoamHistory::Mapping mapping;
		unsigned channels=0; // bit0 vessel RGB, bit1 coast A, bit2 second direction tile
		unsigned textureWidth=0,textureHeight=0;
		Bool valid=FALSE;
	};
	std::vector<ModernFoamTexture> m_modernFoamTextures;

	std::vector<UnsignedByte> m_modernHeights;
	Int m_modernHeightWidth = 0, m_modernHeightHeight = 0, m_modernHeightSide = 0;
	Int m_modernHeightBorder = -1;
	const void *m_modernHeightMap = NULL, *m_modernHeightData = NULL;
	Real m_modernOpticalLevel = 0.0f;
	struct ModernOpticalTexture {
		Real level; TextureClass *texture;
		TextureClass *waveCage=NULL;
		std::vector<UnsignedByte> waveCageSupport; // same quantized alpha sampled by the shader
		std::vector<UnsignedByte> ocean;
		WaterOpticalContext::Field shoreContext; // retained CPU source data, independent of texture upload success
		Real oceanExtent=0.0f;
	};
	std::vector<ModernOpticalTexture> m_modernOpticalTextures;
	std::vector<WaterOpticalContext::Polygon> m_modernOpticalPolygons;
	Bool m_modernOpticalPolygonsValid = FALSE;
	IDirect3DPixelShader9 *	m_waterPixelShader;		///<D3D pixel shader.
	IDirect3DPixelShader9 *	m_riverWaterPixelShader;		///<D3D pixel shader.
	IDirect3DPixelShader9 *	m_trapezoidWaterPixelShader;	///<D3D pixel shader
	IDirect3DPixelShader9 *	m_reflectionPixelShader;	///<darkens water where the reflection texture holds something
	IDirect3DPixelShader9 *	m_riverWaterFoamPixelShader;	///<the river's bytecode again, named for the D3D11 foam program (WaterFoam)
	IDirect3DPixelShader9 *	m_trapezoidWaterFoamPixelShader;	///<the trapezoid's bytecode again, named for the D3D11 foam program
	TextureClass *m_waterSparklesTexture;
	Real m_riverXOffset;
	Real m_riverYOffset;
	Bool m_drawingRiver;
	TextureClass *m_riverAlphaEdge;

	TimeOfDay m_tod;	///<time of day setting for reflected cloud layer

	struct Setting
	{
		TextureClass	*skyTexture;
		TextureClass	*waterTexture;
		Int				waterRepeatCount;
		Real			skyTexelsPerUnit;	//texel density of sky plane (higher value repeats texture more).
		UnsignedInt			vertex00Diffuse;		
		UnsignedInt			vertex10Diffuse;		
		UnsignedInt			vertex11Diffuse;		
		UnsignedInt			vertex01Diffuse;
		UnsignedInt			waterDiffuse;
		UnsignedInt			transparentWaterDiffuse;
		Real			uScrollPerMs;		
		Real			vScrollPerMs;
	};

	Setting m_settings[ TIME_OF_DAY_COUNT ];	///< settings for each time of day
#if ZH_MODERN_WATER
	void drawRiverWater(const WaterPolygonForDraw *pTrig);
#else
	void drawRiverWater(PolygonTrigger *pTrig);
#endif
	void drawTrapezoidWater(Vector3 points[4]);
	void loadSetting ( Setting *skySetting, TimeOfDay timeOfDay );	///<init sky/water settings from GDF
	void renderSky(void);	///<draw the sky layer (clouds, stars, etc.)
	void testCurvedWater(void);	///<draw the sky layer (clouds, stars, etc.)
	void renderSkyBody(Matrix3D *mat);	///<draw the sky body (sun, moon, etc.)
	void renderWaterMesh(void);			///<draw the water surface mesh (deformed 3d mesh).
	RenderResult initBumpMap(LPDIRECT3DTEXTURE9 *pTex, TextureClass *pBumpSource);	///<copies data into bump-map format.
	void renderMirror(CameraClass *cam, Real level);	///< Draw reflected scene into texture
	void drawReflection(Int triangleCount, Int vertexCount);	///< Lay the reflection over the water just drawn
	void drawSea(RenderInfoClass & rinfo);	///< Draw the surface of the water
	///bounding box of frustum clipped polygon plane
	Bool getClippedWaterPlane(CameraClass *cam, AABoxClass *box);

	void setupFlatWaterShader(Bool heightCorrection=FALSE,Bool waveCage=FALSE);
	void setupJbaWaterShader(void);
	void synchronizeWaterStyle(void);
	void registerWaterStyleShaders(void);
	void setupModernWaterShader(Bool river,Bool heightCorrection=FALSE,Bool waveCage=FALSE);
	void updateModernWaterTextures(void);
	void updateModernWaterHulls(void);
	void clearModernWaterHulls(Bool admissions=TRUE);
	void clearModernWaterFoam(Bool history=TRUE);
	void bindModernWaterFoam(Real level,Bool river);
	void invalidateModernWaterShore(void);
	void updateModernWaterShore(void);
	Real modernHullDamping(Real x,Real y,Real level) const;
	Bool modernHasHull(UnsignedInt objectID) const;
	void clearModernWaterOpticalTextures(void);
	void refreshModernWaterOpticalPolygons(void);
	TextureClass *modernWaterOpticalTexture(Real level);
	void drawModernWaterPatch(const Vector3 points[4], Bool river, Real riverStart = 0, Real riverEnd = 1);
	Real modernWaterDamping(Real x, Real y, Real z, Real offshore=0.0f, Real *obstacleAttenuation=NULL, Real *terrainDepth=NULL, Real *hullFreeDamping=NULL, Real *hullFreeObstacle=NULL) const;
	Bool m_renderingMirror = FALSE; ///< prevents a water draw sampling the mirror being written
	void cleanupJbaWaterShader(void);

	//Methods used for GeForce3 specific water
	RenderResult generateIndexBuffer(int sizeX, int sizeY);	///<Generate static index buufer
	RenderResult generateVertexBuffer( Int sizeX, Int sizeY, Int vertexSize, Bool doFill);///<Generate static vertex buffer

};

extern WaterRenderObjClass *TheWaterRenderObj; ///<global water rendering object

#endif  // end __W3DWater_H_
