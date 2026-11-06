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

// FILE: W3DTreeBuffer.h //////////////////////////////////////////////////
//-----------------------------------------------------------------------------
//                                                                          
//                       Westwood Studios Pacific.                          
//                                                                          
//                       Confidential Information					         
//                Copyright (C) 2001 - All Rights Reserved                  
//                                                                          
//-----------------------------------------------------------------------------
//
// Project:    RTS3
//
// File name:  W3DTreeBuffer.h
//
// Created:    John Ahlquist, May 2001
//
// Desc:       Draw buffer to handle all the trees in a scene.
//
//-----------------------------------------------------------------------------

#pragma once

#ifndef __W3DTREE_BUFFER_H_
#define __W3DTREE_BUFFER_H_

//-----------------------------------------------------------------------------
//           Includes                                                      
//-----------------------------------------------------------------------------
#include "always.h"
#include "rendobj.h"
#include "w3d_file.h"
#include "texture.h"
#include "dx8vertexbuffer.h"
#include "dx8indexbuffer.h"
#include "shader.h"
#include "vertmaterial.h"
#include "Lib/BaseType.h"
#include "Common/GameType.h"
#include "Common/AsciiString.h"
#include "Common/GlobalData.h"
#include "GameLogic/ScriptEngine.h"
#include <vector>

//-----------------------------------------------------------------------------
//           Forward References
//-----------------------------------------------------------------------------
class MeshClass;
class W3DTreeBuffer; 
class TileData;
class W3DTreeDrawModuleData;
struct BreezeInfo;
class GeometryInfo;
class W3DProjectedShadow;
struct SunShadowCasterCull;

//-----------------------------------------------------------------------------
//           Type Defines
//-----------------------------------------------------------------------------

/// One model that casts a shadow without being one of the batched trees.
/**
	A tree the map places as a real object - every palm in the game is one - is drawn by W3DModelDraw
	from its own render object and never reaches the tree buffer, so the flattening pass that gives a
	batched tree its silhouette has nothing to work on.  This is the same idea for those: the meshes
	are kept, the object never moves, and the shadow pass lays a copy of their triangles on the
	ground.
*/
typedef struct {
	DrawableID	drawableID;					///< who this belongs to, and how it is removed again
	Int					numMesh;						///< meshes taken from the model (LOD 0, unskinned)
	MeshClass		*mesh[4];						///< held with a reference, so the drawable dying cannot dangle it
	Int					texNdx[4];					///< index into the buffer's shadow texture list
	Real				baseZ;							///< the ground the shadow is laid on, read from the meshes each frame
	Bool				visible;						///< recomputed every frame
} TModelShadow;

enum W3DToppleState
{
	TOPPLE_UPRIGHT = 0,
	TOPPLE_FALLING,
	TOPPLE_FOGGED,
	TOPPPLE_SHROUDED,
	TOPPLE_DOWN
};
/// The individual data for a tree.
typedef struct {
	Vector3 location;					///< Drawing location
	Real		scale;						///< Scale at location.
	Real		sin;							///< Sine of the rotation angle at location.
	Real		cos;							///< Cosine of the rotation angle at location.
	Int			treeType;					///< Type of tree.  
	Bool		visible;					///< Visible flag, updated each frame.
	SphereClass bounds;				///< Bounding sphere for culling to set the visible flag.
	Real		sortKey;					///< Sort key, essentially the distance along the look at vector.
	DrawableID		drawableID;	///< Drawable this tree corresponds to.

	Real		pushAside;
	Real		pushAsideDelta;
	Real		pushAsideSin;			///< Sine of the rotation angle at location.
	Real		pushAsideCos;			///< Cosine of the rotation angle at location.
	ObjectID pushAsideSource; ///< Unit that is pushing us aside.

	UnsignedInt lastFrameUpdated; ///< Last frame push aside was updated.

	Int			nextInPartition;

	Int			swayType;					///< Which sway array entry we are using.

	Int			firstIndex;				///< First index in the vertex buffer for this tree.
	Int     bufferNdx;				///< Which vertex buffer this is in, -1 when the last fill left it out.
	Bool		moved;						///< Its vertices changed since updateVertexBuffer last wrote them.

	// Topple parameters. [7/7/2003]
	Real					m_angularVelocity;				///< Velocity in degrees per frame (or is it radians per frame?)
	Real					m_angularAcceleration;		///< Acceleration angularVelocity is increasing
	Coord3D				m_toppleDirection;				///< Z-less direction we are toppling
	W3DToppleState m_toppleState;						///< Stage this module is in.
	Real					m_angularAccumulation;		///< How much have I rotated so I know when to bounce.
	UnsignedInt		m_options;								///< topple options
	Matrix3D			m_mtx;
	UnsignedInt		m_sinkFramesLeft;					///< Toppled trees sink into the terrain & disappear, how many frames left.

} TTree;

/// The individual data for a tree type.
typedef struct {
	MeshClass * m_mesh;			///< Mesh for this kind of tree.
	SphereClass m_bounds;		///< Bounding boxes for the base tree models.
	const W3DTreeDrawModuleData *m_data;
	ICoord2D		m_textureOrigin; ///< Texture origin in the mega texture.
	Int					m_numTiles;	///< Number of tex tiles.
	Int					m_firstTile;///< First texture tile.
	Int					m_tileWidth;///< Width in tiles of texture;
	Bool				m_halfTile; ///< Tiles are 64x64 pixels, half tile supports a 32x32 bit texture.  Have to adjust the uv values.
	Vector3			m_offset;
	Real				m_shadowSize; ///< Shadow radius.
	Bool				m_doShadow; ///< Draw shadow.

} TTreeType;

//
// W3DTreeBuffer: Draw buffer for the trees.
//
//
class W3DTreeBuffer : public Snapshot 
{	
//friend class BaseHeightMapRenderObjClass;

//-----------------------------------------------------------------------------
//                             W3DTreeTextureClass
//-----------------------------------------------------------------------------
class W3DTreeTextureClass : public TextureClass
{
	W3DMPO_GLUE(W3DTreeTextureClass)
protected:
	virtual void Apply(unsigned int stage);

public:
		/// Create texture.
		W3DTreeTextureClass(unsigned width, unsigned height);

		// just use default destructor. ~TerrainTextureClass(void);
public:
	int update(W3DTreeBuffer *buffer); ///< Sets the pixels, and returns the actual height of the texture.
	void setLOD(Int LOD) const;
};

public:
 
	W3DTreeBuffer(void);
	~W3DTreeBuffer(void); 
	/// Add a tree at location.  Name is the w3d model name.
	void addTree(DrawableID id, Coord3D location, Real scale, Real angle,
								Real randomScaleAmount, const W3DTreeDrawModuleData *data);
	/// Notify that an object moved, so check for collisions.
	void unitMoved(Object *unit);
	/// Add a type of tree.  Name is the w3d model name.
	Int addTreeType(const W3DTreeDrawModuleData *data);
	/// Updates a tree's location.  
	Bool updateTreePosition(DrawableID id, Coord3D location, Real angle);
	void pushAsideTree( DrawableID id, const Coord3D *pusherPos, 
		const Coord3D *pusherDirection, ObjectID pusherID );
	/// Remove a tree.
	void removeTree(DrawableID id);
	/// Remove trees that would be under a building.
	void removeTreesForConstruction(
		const Coord3D* pos, 
		const GeometryInfo& geom,
		Real angle
	);

	/// Take a model's shadow over: the object keeps drawing itself, the buffer draws its shadow.
	Bool addModelShadow(DrawableID id, RenderObjClass *robj);
	/// Give it back, when the drawable goes away or changes its model.
	void removeModelShadow(DrawableID id);

	void setTextureLOD(Int lod);	///<used to adjust maximum mip level sent to hardware.
	/// Empties the tree buffer. 
	void clearAllTrees(void);
	/// Empties the tree buffer.
	void setBounds(const Region2D &bounds) {m_bounds = bounds;}
	/// Draws the trees.  Uses camera for culling.
	void drawTrees(CameraClass * camera, RefRenderObjListIterator *pDynamicLightsIterator,
		const CameraClass *sunCamera = NULL);
	void stepTrees(void);	///< the toppling and pushed trees, once a logic frame, at the render's sync point
	Int drawModelShadowCasters(RenderInfoClass &sunInfo, const SunShadowCasterCull &cull); ///< the object trees drawn into the sun's map
	Int getSunCasterTrees(void) const {return m_sunCasterTrees;} ///< batched trees the last sun pass drew
	/// Called when the view changes, and sort key needs to be recalculated.
	/// Normally sortKey gets calculated when a tree becomes visible.
	void doFullUpdate(void) {m_updateAllKeys = true;};
	void setIsTerrain(void) {m_isTerrainPass = true;}; ///< Terrain calls this to tell trees to draw.
	Bool needToDraw(void) {return m_isTerrainPass;};

	Int getNumTiles(void) {return m_numTiles;}
	TileData *getSourceTile(Int ndx) {return m_sourceTiles[ndx];}
	void allocateTreeBuffers(void);							 ///< Allocates the buffers.
	void freeTreeBuffers(void);									 ///< Frees the index and vertex buffers.

private:
	enum { MAX_TREE_VERTEX=30000, 
					MAX_TREE_INDEX=60000, 
					MAX_TREES=4000};
	enum {MAX_TYPES = 64,
				MAX_TILES = 512,
				NUM_SWAY_ENTRIES = 100,
				MAX_SWAY_TYPES = 10,
				//
				// Every visible tree is baked into these buffers, and a tree that does not fit is
				// simply not drawn: the fill loop breaks and the rest of the forest is gone until
				// the camera moves and the set changes.  One buffer is 30000 vertices, about 730
				// trees, and a zoomed-out camera over dense woodland goes through that.  The fill,
				// the push-aside update and the draw were all written to walk a list of buffers and
				// then left at one; four costs about 4MB and takes the wall four times further away.
				//
				MAX_BUFFERS = 4,
				SORT_ITERATIONS_PER_FRAME=10};
	enum {PARTITION_WIDTH_HEIGHT = 100};
	enum {MAX_MODEL_SHADOWS = 1024,		///< object trees on a map; Golden Oasis has 278 palms
				MAX_SHADOW_TEXTURES = 32,		///< distinct textures across those models
				MAX_SHADOW_BATCH_VERTEX = 8000};	///< per texture, per frame
	DX8VertexBufferClass	*m_vertexTree[MAX_BUFFERS];	///<Tree vertex buffer.
	DX8IndexBufferClass			*m_indexTree[MAX_BUFFERS];	///<indices defining a triangles for the tree drawing.
	struct ShadowRange { Int first, count; };
	std::vector<ShadowRange> m_shadowRanges[MAX_BUFFERS];
	Bool m_lastShadowsForProps = FALSE;
	Int m_sunCasterTrees = 0;
	// What the last sun pass culled against: both views' frustum planes, the silhouette stretch and
	// ShadowsForProps.  The next pass with the same key and no tree changed keeps its visible flags
	// rather than walking every tree again; any tree change or a screen-only cull drops the key.
	enum { SUN_CULL_KEY = 2*6*4 + 4 };
	Real m_sunCullKey[SUN_CULL_KEY];
	Int m_sunCullTrees = 0;
	Bool m_sunCullValid = FALSE;
	Bool m_movedSinceCull = FALSE;	///< a tree moved since the last full cull (a sinking one's sort key moves)
	std::vector<Int> m_movedTrees;	///< the trees with moved set, in the order stepTrees moved them
	IDirect3DPixelShader9 *	m_dwTreePixelShader;	///<D3D pixel shader
	IDirect3DVertexShader9 * m_dwTreeVertexShader;	///<D3D vertex shader
	IDirect3DVertexDeclaration9 * m_treeVertexDeclaration;	///<what D3D8 carried beside the shader

	Short		m_areaPartition[PARTITION_WIDTH_HEIGHT*PARTITION_WIDTH_HEIGHT];
	Region2D m_bounds;
	
	TextureClass *m_treeTexture;	///<Trees texture
	Int			m_textureWidth;				///<Width in pixels m_treeTexture;
	Int			m_textureHeight;				///<Width in pixels m_treeTexture;
	Int			m_curNumTreeVertices[MAX_BUFFERS]; ///<Number of vertices used in m_vertexTree.
	Int			m_curNumTreeIndices[MAX_BUFFERS];	///<Number of indices used in b_indexTree;
	TTree	m_trees[MAX_TREES];			///< The tree buffer.  All trees are stored here.
	Int			m_numTrees;						///< Number of trees in m_trees.
	Bool		m_anythingChanged;	///< Set to true if visibility or sorting changed.
	Bool		m_anyPushChanged;		///< Set to true if push aside is active.
	Bool		m_updateAllKeys;  ///< Set to true when the view changes.
	Bool		m_initialized;		///< True if the subsystem initialized.
	Bool		m_isTerrainPass;  ///< True if the terrain was drawn in this W3D scene render pass.
	Bool		m_needToUpdateTexture; ///< True if we need to update the texture.
	TTreeType m_treeTypes[MAX_TYPES];	///< Info about a kind of tree.
	Int			m_numTreeTypes;						///< Number of entries in m_treeTypes.

	Int			m_numTiles;
	TileData			*m_sourceTiles[MAX_TILES];	///< Tiles for m_textureClasses
	Vector3 m_cameraLookAtVector;
	Vector3 m_swayOffsets[NUM_SWAY_ENTRIES];
	Int			m_curSwayVersion;

	Real		m_curSwayOffset[MAX_SWAY_TYPES];
	UnsignedInt	m_lastSwayFrame;		///< last logic frame the sway was stepped on (drawTrees runs per render frame)
	UnsignedInt	m_lastTreeStepFrame;	///< last logic frame stepTrees moved the toppling and pushed trees on
	BreezeInfo m_breezeForDraw{};
	Bool m_pauseForDraw = FALSE;
	Real		m_curSwayStep[MAX_SWAY_TYPES];
	Real		m_curSwayFactor[MAX_SWAY_TYPES];

	W3DProjectedShadow *m_shadow;

	TModelShadow m_modelShadows[MAX_MODEL_SHADOWS];	///< shadows for trees the map placed as objects
	Int m_numModelShadows;
	TextureClass *m_shadowTextures[MAX_SHADOW_TEXTURES];	///< their textures, held with a reference
	Int m_numShadowTextures;

protected:
	// snapshot methods
	virtual void crc( Xfer *xfer );
	virtual void xfer( Xfer *xfer );
	virtual void loadPostProcess( void );

protected:
	/// Updates the sway offsets.
	void updateSway(const BreezeInfo& info);
	void loadTreesInVertexAndIndexBuffers(RefRenderObjListIterator *pDynamicLightsIterator); ///< Fills the index and vertex buffers for drawing.
	void updateVertexBuffer(void); ///< Fills the index and vertex buffers for drawing.
	void drawTreeBuffers(Bool shadowPass, Bool sunPass = FALSE); ///< Draws every filled buffer, as trees or as their shadows.
	void drawModelShadows(CameraClass *camera, Real stretchX, Real stretchY); ///< the object trees' shadows
	Int  addShadowTexture(TextureClass *tex);	///< index of this texture in the shadow list, adding it if new
	void cull(const CameraClass * camera, const CameraClass *sunCamera = NULL); ///< Culls the trees.
	UnsignedInt  doLighting(const Vector3 *normal,  
		const GlobalData::TerrainLighting	*objectLighting, 
		const Vector3 *emissive, UnsignedInt vertexDiffuse, Real scale) const;
#if 0 // sort is no longer used and messes up the order. jba [6/6/2003]
	void sort( Int iterations );								 ///< Performs partial bubble sort.
#endif 
	void updateTexture(void);

	Int  getPartitionBucket(const Coord3D &pos) const;

	void updateTopplingTree(TTree *tree);
	void applyTopplingForce( TTree *tree, const Coord3D* toppleDirection, Real toppleSpeed,
																			 UnsignedInt options );

};

#endif  // end __W3DTREE_BUFFER_H_
