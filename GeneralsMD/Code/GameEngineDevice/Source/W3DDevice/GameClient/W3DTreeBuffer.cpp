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

// FILE: W3DTreeBuffer.cpp ////////////////////////////////////////////////
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
// File name: W3DTreeBuffer.cpp
//
// Created:   John Ahlquist, May 2001
//
// Desc:      Draw buffer to handle all the trees in a scene.
//
//-----------------------------------------------------------------------------

// ------------------------------------------------------------------------------------------------
/** Topple options */
// ------------------------------------------------------------------------------------------------
enum
{
	W3D_TOPPLE_OPTIONS_NONE			 = 0x00000000,
	W3D_TOPPLE_OPTIONS_NO_BOUNCE = 0x00000001,  ///< do not bounce when hit the ground
	W3D_TOPPLE_OPTIONS_NO_FX		 = 0x00000002	///< do not play any FX when hit the ground
};
//-----------------------------------------------------------------------------
//         Includes                                                      
//-----------------------------------------------------------------------------
#include "Platform/SunShadow.h"
#include "W3DDevice/GameClient/SunShadowCasterCull.h"
#include "W3DDevice/GameClient/W3DTreeBuffer.h"

#include <stdio.h>
#include <string.h>
#include <assetmgr.h>
#include <texture.h>
#include "Common/MapReaderWriterInfo.h"
#include "Common/FileSystem.h" 
#include "Common/file.h"
#include "Common/PerfTimer.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "GameLogic/ScriptEngine.h"
#include "GameClient/Snow.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object.h"
#include "GameLogic/PartitionManager.h"
#include "GameClient/ClientRandomValue.h"
#include "GameClient/GameClient.h"		// logic-frame gate for the sway phase
#include "GameClient/FXList.h"
#include "GameClient/ObserverCamera.h"
#include "W3DDevice/GameClient/TerrainTex.h"
#include "W3DDevice/GameClient/HeightMap.h"
#include "W3DDevice/GameClient/W3DDynamicLight.h"
#include "W3DDevice/GameClient/Module/W3DTreeDraw.h"
#include "W3DDevice/GameClient/W3DShaderManager.h"
#include "W3DDevice/GameClient/W3DShadow.h"
#include "W3DDevice/GameClient/W3DShroud.h"
#include "W3DDevice/GameClient/W3DProjectedShadow.h"
#include "WW3D2/camera.h"
#include "WW3D2/dx8wrapper.h"
#include "WW3D2/dx8renderer.h"
#include "WW3D2/hlod.h"
#include "WW3D2/matinfo.h"
#include "WW3D2/mesh.h"
#include "WW3D2/meshmdl.h"
#include "WW3D2/vertmaterial.h"
#include "d3dx9runtime.h"
#include "d3d8shadertranslate.h"
#include "Platform/RenderTypes.h"

#ifdef _INTERNAL
// for occasional debugging...
//#pragma optimize("", off)
//#pragma MESSAGE("************************************** WARNING, optimization disabled for debugging purposes")
#endif

// If TEST_AND_BLEND is defined, it will do an alpha test and blend.  Otherwise just alpha test. jba. [5/30/2003]
#define dontTEST_AND_BLEND 1

#define USE_STATIC 1

#define END_OF_PARTITION (-1)

#define DELETED_TREE_TYPE (-2)

/******************************************************************************
						W3DTreeTextureClass
******************************************************************************/
//-----------------------------------------------------------------------------
//         Public Functions                                                
//-----------------------------------------------------------------------------

//=============================================================================
// W3DTreeBuffer::W3DTreeTextureClass::W3DTreeTextureClass
//=============================================================================
/** Constructor. Calls parent constructor to create a 16 bit per pixel D3D 
texture of the desired height and mip level. */
//=============================================================================
W3DTreeBuffer::W3DTreeTextureClass::W3DTreeTextureClass(unsigned width, unsigned height) :
	TextureClass(width, height, 
		WW3D_FORMAT_A8R8G8B8, MIP_LEVELS_ALL )
{
}

//=============================================================================
// W3DTreeBuffer::W3DTreeTextureClass::update
//=============================================================================
/** Sets the tile bitmap data into the texture.  The tiles are placed with 4
	pixel borders around them, so that when the tiles are scaled and bilinearly
	interpolated, you don't get seams between the tiles.  */
//=============================================================================
int W3DTreeBuffer::W3DTreeTextureClass::update(W3DTreeBuffer *buffer)
{

	//Set to clamp.
	Get_Filter().Set_U_Addr_Mode(TextureFilterClass::TEXTURE_ADDRESS_CLAMP);
	Get_Filter().Set_V_Addr_Mode(TextureFilterClass::TEXTURE_ADDRESS_CLAMP);

	IDirect3DSurface9 *surface_level;
	D3DSURFACE_DESC surface_desc;
	D3DLOCKED_RECT locked_rect;
	DX8_ErrorCode(Peek_D3D_Texture()->GetSurfaceLevel(0, &surface_level));
	DX8_ErrorCode(surface_level->GetDesc(&surface_desc));

	DX8_ErrorCode(surface_level->LockRect(&locked_rect, NULL, 0));

	Int tilePixelExtent = TILE_PIXEL_EXTENT;		 
//	Int numRows = surface_desc.Height/(tilePixelExtent+TILE_OFFSET);
#ifdef _DEBUG
	//DASSERT_MSG(tilesPerRow*numRows >= htMap->m_numBitmapTiles,Debug::Format ("Too many tiles.")); 
	//DEBUG_ASSERTCRASH((Int)surface_desc.Width >= tilePixelExtent*tilesPerRow, ("Bitmap too small."));
#endif
	if (surface_desc.Format == D3DFMT_A8R8G8B8) {
		Int tileNdx;
		Int pixelBytes = 4;
#if 0 // Fill unused texture for debug display.
		UnsignedInt cellX, cellY;
		for (cellX = 0; cellX < surface_desc.Width; cellX++) {
			for (cellY = 0; cellY < surface_desc.Height; cellY++) {
				UnsignedByte *pBGR = ((UnsignedByte *)locked_rect.pBits)+(cellY*surface_desc.Width+cellX)*pixelBytes;
				//*((Short*)pBGR) =  0x8000 + (((255-2*cellY)>>3)<<10) + ((4*cellX)>>4);
				*((Int*)pBGR) =  0xFF000000 | ( (((255-cellY))<<16) + ((cellX)) );
				
			}
		}
#endif
		for (tileNdx=0; tileNdx < buffer->getNumTiles(); tileNdx++) {
			TileData *pTile = buffer->getSourceTile(tileNdx);
			if (!pTile) continue;
			ICoord2D position = pTile->m_tileLocationInTexture;
			if (position.x<0) {
				continue;
			}
			Int i,j;
			for (j=0; j<tilePixelExtent; j++) {
				UnsignedByte *pBGR = pTile->getRGBDataForWidth(tilePixelExtent);
				pBGR += (tilePixelExtent-(1+j))*TILE_BYTES_PER_PIXEL*tilePixelExtent; // invert to match.
				Int row = position.y+j;
				UnsignedByte *pBGRA = ((UnsignedByte*)locked_rect.pBits) +
							(row)*surface_desc.Width*pixelBytes;

				Int column = position.x;
				pBGRA += column*pixelBytes;
				for (i=0; i<tilePixelExtent; i++) {
					// 15 bit color *((Short*)pBGRA) = 0x8000 + ((pBGR[2]>>3)<<10) + ((pBGR[1]>>3)<<5) + (pBGR[0]>>3);
					*((Int *)pBGRA) = (pBGR[3]<<24) + (pBGR[2]<<16) + (pBGR[1]<<8) + (pBGR[0]); 
					pBGRA +=pixelBytes;
					pBGR +=TILE_BYTES_PER_PIXEL;
				}
			}
		}

	}
	DX8_ErrorCode(surface_level->UnlockRect());
	surface_level->Release();
	DX8_ErrorCode(D3DXFilterTexture(Peek_D3D_Texture(), NULL, (UINT)0, D3DX_FILTER_BOX));	
	if (TheWritableGlobalData->m_textureReductionFactor) {
		DX8_ErrorCode(Peek_D3D_Texture()->SetLOD((UnsignedInt)TheWritableGlobalData->m_textureReductionFactor));
	}
	return(surface_desc.Height);
}


//=============================================================================
// W3DTreeBuffer::W3DTreeTextureClass::setLOD
//=============================================================================
/** Sets the lod of the texture to be loaded into the video card.  */
//=============================================================================
void W3DTreeBuffer::W3DTreeTextureClass::setLOD(Int LOD) const
{
	if (Peek_D3D_Texture()) {
		DX8_ErrorCode(Peek_D3D_Texture()->SetLOD((UnsignedInt)LOD));
	}
}
//=============================================================================
// W3DTreeBuffer::W3DTreeTextureClass::Apply
//=============================================================================
/** Sets the texture as the current D3D texture, and does some custom setup
(standard D3D setup, but beyond the scope of W3D).  */
//=============================================================================
void W3DTreeBuffer::W3DTreeTextureClass::Apply(unsigned int stage)
{
	// Do the base apply.
	TextureClass::Apply(stage);
}
//-----------------------------------------------------------------------------
//         Private Data                                                     
//-----------------------------------------------------------------------------

#ifdef TEST_AND_BLEND
// A W3D shader that does alpha, texturing, tests zbuffer, doesn't update zbuffer.
#define SC_ALPHA_DETAIL ( SHADE_CNST(ShaderClass::PASS_LEQUAL, ShaderClass::DEPTH_WRITE_ENABLE, ShaderClass::COLOR_WRITE_ENABLE, ShaderClass::SRCBLEND_SRC_ALPHA, \
	ShaderClass::DSTBLEND_ONE_MINUS_SRC_ALPHA, ShaderClass::FOG_DISABLE, ShaderClass::GRADIENT_MODULATE, ShaderClass::SECONDARY_GRADIENT_DISABLE, ShaderClass::TEXTURING_ENABLE, \
	ShaderClass::ALPHATEST_ENABLE, ShaderClass::CULL_MODE_ENABLE, \
	ShaderClass::DETAILCOLOR_DISABLE, ShaderClass::DETAILALPHA_DISABLE) )

#define SC_ALPHA_DETAIL_2X ( SHADE_CNST(ShaderClass::PASS_LEQUAL, ShaderClass::DEPTH_WRITE_ENABLE, ShaderClass::COLOR_WRITE_ENABLE, ShaderClass::SRCBLEND_SRC_ALPHA, \
	ShaderClass::DSTBLEND_ONE_MINUS_SRC_ALPHA, ShaderClass::FOG_DISABLE, ShaderClass::GRADIENT_MODULATE2X, ShaderClass::SECONDARY_GRADIENT_DISABLE, ShaderClass::TEXTURING_ENABLE, \
	ShaderClass::ALPHATEST_ENABLE, ShaderClass::CULL_MODE_ENABLE, \
	ShaderClass::DETAILCOLOR_DISABLE, ShaderClass::DETAILALPHA_DISABLE) )

#else
#define SC_ALPHA_DETAIL ( SHADE_CNST(ShaderClass::PASS_LEQUAL, ShaderClass::DEPTH_WRITE_ENABLE, ShaderClass::COLOR_WRITE_ENABLE, ShaderClass::SRCBLEND_ONE, \
	ShaderClass::DSTBLEND_ZERO, ShaderClass::FOG_DISABLE, ShaderClass::GRADIENT_MODULATE, ShaderClass::SECONDARY_GRADIENT_DISABLE, ShaderClass::TEXTURING_ENABLE, \
	ShaderClass::ALPHATEST_ENABLE, ShaderClass::CULL_MODE_DISABLE, \
	ShaderClass::DETAILCOLOR_DISABLE, ShaderClass::DETAILALPHA_DISABLE) )

#define SC_ALPHA_DETAIL_2X ( SHADE_CNST(ShaderClass::PASS_LEQUAL, ShaderClass::DEPTH_WRITE_ENABLE, ShaderClass::COLOR_WRITE_ENABLE, ShaderClass::SRCBLEND_ONE, \
	ShaderClass::DSTBLEND_ZERO, ShaderClass::FOG_DISABLE, ShaderClass::GRADIENT_MODULATE2X, ShaderClass::SECONDARY_GRADIENT_DISABLE, ShaderClass::TEXTURING_ENABLE, \
	ShaderClass::ALPHATEST_ENABLE, ShaderClass::CULL_MODE_ENABLE, \
	ShaderClass::DETAILCOLOR_DISABLE, ShaderClass::DETAILALPHA_DISABLE) )
#endif
static ShaderClass detailAlphaShader(SC_ALPHA_DETAIL);
static ShaderClass detailAlphaShader2X(SC_ALPHA_DETAIL_2X);

// The shadow pass draws the same triangles flattened onto the ground, so it must not write z (the
// tree standing on the shadow is drawn right after it) and must not cull: flattening a leaf card
// reverses the winding of whichever half of the crown faces away from the sun, and culling would
// drop it.
#define SC_TREE_SHADOW ( SHADE_CNST(ShaderClass::PASS_LEQUAL, ShaderClass::DEPTH_WRITE_DISABLE, ShaderClass::COLOR_WRITE_ENABLE, ShaderClass::SRCBLEND_SRC_ALPHA, \
	ShaderClass::DSTBLEND_ONE_MINUS_SRC_ALPHA, ShaderClass::FOG_DISABLE, ShaderClass::GRADIENT_MODULATE, ShaderClass::SECONDARY_GRADIENT_DISABLE, ShaderClass::TEXTURING_ENABLE, \
	ShaderClass::ALPHATEST_ENABLE, ShaderClass::CULL_MODE_DISABLE, \
	ShaderClass::DETAILCOLOR_DISABLE, ShaderClass::DETAILALPHA_DISABLE) )

static ShaderClass treeShadowShader(SC_TREE_SHADOW);

// The object trees' shadows carry their own colour on the vertex and their shape in the texture's
// alpha, so this one blends rather than tests: an alpha test would cut the soft edge of a frond off
// at whatever the reference happened to be.
#define SC_MODEL_SHADOW ( SHADE_CNST(ShaderClass::PASS_LEQUAL, ShaderClass::DEPTH_WRITE_DISABLE, ShaderClass::COLOR_WRITE_ENABLE, ShaderClass::SRCBLEND_SRC_ALPHA, \
	ShaderClass::DSTBLEND_ONE_MINUS_SRC_ALPHA, ShaderClass::FOG_DISABLE, ShaderClass::GRADIENT_MODULATE, ShaderClass::SECONDARY_GRADIENT_DISABLE, ShaderClass::TEXTURING_ENABLE, \
	ShaderClass::ALPHATEST_DISABLE, ShaderClass::CULL_MODE_DISABLE, \
	ShaderClass::DETAILCOLOR_DISABLE, ShaderClass::DETAILALPHA_DISABLE) )

static ShaderClass modelShadowShader(SC_MODEL_SHADOW);

// How dark a leaf lands on the ground, 0-255. The blob decal uses 160 and that is far too heavy for
// a silhouette: the blob's alpha falls off towards its rim, a leaf's does not, and the crown's own
// cards overlap so the middle of a shadow is paid for twice. Tune here.
#define TREE_SHADOW_ALPHA 90

// Where the shadow's own alpha test cuts, 0-255. Anything above the gaps between the leaves.
#define TREE_SHADOW_ALPHAREF 12

// How far the shadow floats over the tree's own base height, in world units. Without it the two
// surfaces are coplanar at the trunk and fight for the depth buffer.
#define TREE_SHADOW_LIFT 1.0f

// A sun near the horizon projects a shadow hundreds of units long, over terrain the flat projection
// knows nothing about. Cap the stretch at this many tree heights.
#define TREE_SHADOW_MAX_STRETCH 3.0f

// The sun, as how far a tree's shadow reaches sideways for every unit of height.  Both kinds of
// tree shadow are the same shear about the ground, and this is the number it shears by.  FALSE
// with no sun high enough to cast one.
static Bool treeShadowStretch(Real &stretchX, Real &stretchY)
{
	stretchX = 0.0f;
	stretchY = 0.0f;
	if (TheW3DShadowManager == NULL) {
		return false;
	}
	Vector3 &sunPos = TheW3DShadowManager->getLightPosWorld(0);
	if (sunPos.Z <= 1.0f) {
		return false;
	}
	stretchX = -sunPos.X / sunPos.Z;
	stretchY = -sunPos.Y / sunPos.Z;
	Real stretchSq = stretchX*stretchX + stretchY*stretchY;
	if (stretchSq > TREE_SHADOW_MAX_STRETCH*TREE_SHADOW_MAX_STRETCH) {
		Real scale = TREE_SHADOW_MAX_STRETCH / WWMath::Sqrt(stretchSq);
		stretchX *= scale;
		stretchY *= scale;
	}
	return true;
}

// Shadows are wanted under either option.  The silhouette pass needs the vertex shader, since
// flattening the tree onto the ground is the shader's own sway arithmetic with different constants.
// Hardware without one keeps the blob, and so does classic graphics, because the blob is what every
// tree had in 2003.
static Bool treeSilhouetteShadows(IDirect3DVertexShader9 *treeVertexShader)
{
	return (TheGlobalData->m_useShadowDecals || TheGlobalData->m_useShadowVolumes) && treeVertexShader != 0
		&& TheW3DShadowManager != NULL && !TheGlobalData->m_classicGraphics;
}


/*
#define SC_ALPHA_DETAIL ( SHADE_CNST(ShaderClass::PASS_LEQUAL, ShaderClass::DEPTH_WRITE_ENABLE, ShaderClass::COLOR_WRITE_ENABLE, ShaderClass::SRCBLEND_ONE, \
	ShaderClass::DSTBLEND_ZERO, ShaderClass::FOG_DISABLE, ShaderClass::GRADIENT_MODULATE, ShaderClass::SECONDARY_GRADIENT_DISABLE, ShaderClass::TEXTURING_ENABLE, \
	ShaderClass::ALPHATEST_ENABLE, ShaderClass::CULL_MODE_DISABLE, \
	ShaderClass::DETAILCOLOR_DISABLE, ShaderClass::DETAILALPHA_DISABLE) )

static ShaderClass detailAlphaShader(SC_ALPHA_DETAIL);
*/

/*
#define SC_ALPHA_DETAIL ( SHADE_CNST(ShaderClass::PASS_LEQUAL, ShaderClass::DEPTH_WRITE_DISABLE, ShaderClass::COLOR_WRITE_ENABLE, ShaderClass::SRCBLEND_SRC_ALPHA, \
	ShaderClass::DSTBLEND_ONE_MINUS_SRC_ALPHA, ShaderClass::FOG_DISABLE, ShaderClass::GRADIENT_MODULATE, ShaderClass::SECONDARY_GRADIENT_DISABLE, ShaderClass::TEXTURING_ENABLE, \
	ShaderClass::ALPHATEST_ENABLE, ShaderClass::CULL_MODE_ENABLE, \
	ShaderClass::DETAILCOLOR_DISABLE, ShaderClass::DETAILALPHA_DISABLE) )

static ShaderClass detailAlphaShader(SC_ALPHA_DETAIL);
*/

/*
#define SC_ALPHA_MIRROR ( SHADE_CNST(ShaderClass::PASS_LEQUAL, ShaderClass::DEPTH_WRITE_DISABLE, ShaderClass::COLOR_WRITE_ENABLE, ShaderClass::SRCBLEND_SRC_ALPHA, \
	ShaderClass::DSTBLEND_ONE_MINUS_SRC_ALPHA, ShaderClass::FOG_DISABLE, ShaderClass::GRADIENT_MODULATE, ShaderClass::SECONDARY_GRADIENT_DISABLE, ShaderClass::TEXTURING_ENABLE, \
	ShaderClass::DETAILCOLOR_DISABLE, ShaderClass::DETAILALPHA_DISABLE, ShaderClass::ALPHATEST_DISABLE, ShaderClass::CULL_MODE_DISABLE, \
	ShaderClass::DETAILCOLOR_DISABLE, ShaderClass::DETAILALPHA_DISABLE) )

static ShaderClass mirrorAlphaShader(SC_ALPHA_DETAIL);

// ShaderClass::PASS_ALWAYS, 

#define SC_ALPHA_2D ( SHADE_CNST(PASS_ALWAYS, DEPTH_WRITE_DISABLE, COLOR_WRITE_ENABLE, \
	SRCBLEND_SRC_ALPHA, DSTBLEND_ONE_MINUS_SRC_ALPHA, FOG_DISABLE, GRADIENT_DISABLE, \
	SECONDARY_GRADIENT_DISABLE, TEXTURING_ENABLE, DETAILCOLOR_DISABLE, DETAILALPHA_DISABLE, \
	ALPHATEST_DISABLE, CULL_MODE_ENABLE, DETAILCOLOR_DISABLE, DETAILALPHA_DISABLE) )
ShaderClass ShaderClass::_PresetAlpha2DShader(SC_ALPHA_2D);
*/
//-----------------------------------------------------------------------------
//         Private Functions                                               
//-----------------------------------------------------------------------------

//=============================================================================
// W3DTreeBuffer::cull
//=============================================================================
/** Culls the trees, marking the visible flag.  If a tree becomes visible, it sets
it's sortKey */
//=============================================================================
void W3DTreeBuffer::cull(const CameraClass * camera, const CameraClass *sunCamera)
{
	Int curTree;

	// Calulate the vector direction that the camera is looking at.
	Matrix3D camera_matrix = camera->Get_Transform();
	float zmod = -1;
	float x = zmod * camera_matrix[0][2] ;
	float y = zmod * camera_matrix[1][2] ;
	float z = zmod * camera_matrix[2][2] ;
	m_cameraLookAtVector.Set(x,y,z);

	/* A tree's silhouette shadow is drawn out of the same batch as the tree, so a tree culled by its
		 own outline took its shadow with it: the shadow of a tree just past the edge of the screen lay
		 on the ground in view and went out all at once.  Where that shadow is drawn, a tree whose
		 shadow can reach the view is in the batch too.  It lies flat at the tree's base. */
	Real stretchX, stretchY;
	const Bool castsSilhouette = treeSilhouetteShadows(m_dwTreeVertexShader) && treeShadowStretch(stretchX, stretchY);
	/* The sun pass culls every frame, and with the camera still the answer is the same flags, the same
		 keys and the same count as last time: every input is in the key, and every change to a tree's
		 bounds or type comes through m_anythingChanged or a new tree, which drop it. */
	if (sunCamera) {
		static_assert(sizeof(PlaneClass) == 4*sizeof(Real), "the key copies the frustum planes as floats");
		Real key[SUN_CULL_KEY];
		memcpy(key, camera->Get_Frustum_Planes(), 6*sizeof(PlaneClass));
		memcpy(key+24, sunCamera->Get_Frustum_Planes(), 6*sizeof(PlaneClass));
		key[48] = castsSilhouette ? 1.0f : 0.0f;
		key[49] = castsSilhouette ? stretchX : 0.0f;
		key[50] = castsSilhouette ? stretchY : 0.0f;
		key[51] = TheGlobalData->m_shadowsForProps ? 1.0f : 0.0f;
		if (m_sunCullValid && m_sunCullTrees == m_numTrees && memcmp(key, m_sunCullKey, sizeof(key)) == 0) {
			m_updateAllKeys = false;
			return;
		}
		memcpy(m_sunCullKey, key, sizeof(key));
		m_sunCullTrees = m_numTrees;
		m_sunCullValid = TRUE;
	} else {
		m_sunCullValid = FALSE;
	}
	m_movedSinceCull = FALSE;
	/* The sun's box holds trees the screen does not, and they go into the same buffers the screen
		 draws from.  The fill takes trees in index order and stops at MAX_BUFFERS, so a box over dense
		 woodland (the freecam's is the whole map) would push trees in plain view out.  The screen's own
		 trees are counted first and the box only adds what the room left over holds.
		 ponytail: one whole buffer is kept spare for what each buffer's last break wastes; an exact
		 packing would have to fill the screen's trees first, which the fill loop does not do. */
	Int roomVertices = 0;
	Int roomIndices = 0;
	if (sunCamera) {
		m_sunCasterTrees = 0;
		roomVertices = (MAX_BUFFERS - 1) * MAX_TREE_VERTEX;
		roomIndices = (MAX_BUFFERS - 1) * MAX_TREE_INDEX;
		for (curTree=0; curTree<m_numTrees; curTree++) {
			const Int type = m_trees[curTree].treeType;
			if (type < 0 || m_treeTypes[type].m_mesh == 0)
				continue;
			if (camera->Cull_Sphere(m_trees[curTree].bounds) && !(castsSilhouette && shadowCanReachView(camera->Get_Frustum(),
					m_trees[curTree].bounds, m_trees[curTree].location.Z, stretchX, stretchY)))
				continue;
			roomVertices -= m_treeTypes[type].m_mesh->Peek_Model()->Get_Vertex_Count();
			roomIndices -= 3 * m_treeTypes[type].m_mesh->Peek_Model()->Get_Polygon_Count();
		}
	}
	for (curTree=0; curTree<m_numTrees; curTree++) {
		Bool doKey = false;	// We calculate the key when a tree becomes visible.
		const Int type = m_trees[curTree].treeType;
		Bool visible = !camera->Cull_Sphere(m_trees[curTree].bounds);
		if (!visible && castsSilhouette)
			visible = shadowCanReachView(camera->Get_Frustum(), m_trees[curTree].bounds, m_trees[curTree].location.Z,
				stretchX, stretchY);
		// Keep one shared buffer for both views. A tree outside the screen can still
		// cast onto it; reculling to just the screen would also rebuild it twice a frame.
		if (!visible && sunCamera && type >= 0 && m_treeTypes[type].m_mesh != 0
			&& !sunCamera->Cull_Sphere(m_trees[curTree].bounds)) {
			const Int vertices = m_treeTypes[type].m_mesh->Peek_Model()->Get_Vertex_Count();
			const Int indices = 3 * m_treeTypes[type].m_mesh->Peek_Model()->Get_Polygon_Count();
			if (vertices <= roomVertices && indices <= roomIndices) {
				visible = TRUE;
				roomVertices -= vertices;
				roomIndices -= indices;
			}
		}
		if (sunCamera && visible && type >= 0 && (m_treeTypes[type].m_doShadow || TheGlobalData->m_shadowsForProps))
			++m_sunCasterTrees;	// what loadTreesInVertexAndIndexBuffers gives a shadow range
		if (visible != m_trees[curTree].visible) {
			m_trees[curTree].visible=visible;
			m_anythingChanged = true;
			if (visible) {
				doKey = true;
			}
		}
		// Also calculate sort key if a tree is visible, and the view changed setting m_updateAllKeys to true.
		if (doKey || (visible&&m_updateAllKeys)) {
			// The sort key is essentially the distance of location in the direction of the 
			// camera look at.
			m_trees[curTree].sortKey = Vector3::Dot_Product(m_trees[curTree].location, m_cameraLookAtVector); 
		}
	}
	m_updateAllKeys = false;
}
//=============================================================================
// W3DTreeBuffer::getPartitionBucket
//=============================================================================
/** Returns the bucket index into m_areaPartition for a given location. */
//=============================================================================
Int W3DTreeBuffer::getPartitionBucket(const Coord3D &pos) const
{
	Real x = pos.x;
	Real y = pos.y;
	if (x<m_bounds.lo.x) x = m_bounds.lo.x;
	if (y<m_bounds.lo.y) y = m_bounds.lo.y;
	if (x>m_bounds.hi.x) x = m_bounds.hi.x;
	if (y>m_bounds.hi.y) y = m_bounds.hi.y;
	Int xIndex = REAL_TO_INT_FLOOR ( (x/(m_bounds.hi.x-m_bounds.lo.x)) * (PARTITION_WIDTH_HEIGHT-0.1f) );
	Int yIndex = REAL_TO_INT_FLOOR ( (y/(m_bounds.hi.y-m_bounds.lo.y)) * (PARTITION_WIDTH_HEIGHT-0.1f) );
	DEBUG_ASSERTCRASH(xIndex>=0 && yIndex>=0 && xIndex<PARTITION_WIDTH_HEIGHT && yIndex<PARTITION_WIDTH_HEIGHT, ("Invalid range."));
	return yIndex*PARTITION_WIDTH_HEIGHT + xIndex;
}

//=============================================================================
// W3DTreeBuffer::cull
//=============================================================================
/** Culls the trees, marking the visible flag.  If a tree becomes visible, it sets
it's sortKey */
//=============================================================================
void W3DTreeBuffer::updateSway(const BreezeInfo& info)
{
	Int i;
	for	(i=0; i<NUM_SWAY_ENTRIES; i++) {
		Real factor = Cos(i*2.0f*PI/(NUM_SWAY_ENTRIES+1.0f));
		Real angle = info.m_lean + (info.m_intensity  * factor);
		Real S = Sin(angle);
		Real C = Cos(angle);
		m_swayOffsets[i].X = info.m_directionVec.x * S;
		m_swayOffsets[i].Y = info.m_directionVec.y * S;
		m_swayOffsets[i].Z = C - 1.0f;
	}
	
	Real delta				= info.m_randomness * 0.5f;
	for	(i=0; i<m_numTrees; i++) {
		m_trees[i].swayType = 1+GameClientRandomValue(0, MAX_SWAY_TYPES-1);
	}
	for (i=0; i<MAX_SWAY_TYPES; i++) {
		m_curSwayStep[i] = NUM_SWAY_ENTRIES / (Real)info.m_breezePeriod;
		m_curSwayStep[i]	*= GameClientRandomValueReal(1.0f-delta, 1.0f+delta);
		if (m_curSwayStep[i]<0.0f) {
			m_curSwayStep[i] = 0.0f;
		}
		m_curSwayOffset[i] = 0;
		m_curSwayFactor[i] = GameClientRandomValueReal(1.0f-delta, 1.0f+delta);
	}	
	m_curSwayVersion = info.m_breezeVersion;
}

#if 0 // sort is not used, and messes up the order jba. [6/6/2003]
//=============================================================================
// W3DTreeBuffer::sort
//=============================================================================
/** Sorts the trees.  Does num_iterations of a bubble sort.  This is good because
it ends immediately if the trees are already sorted (which is most of the time)
and will perform a fixed amount of work each frame until it becomes sorted. */
//=============================================================================
void W3DTreeBuffer::sort(Int numIterations)
{
	// sort in descending order.
	Int iter;
	Bool swap = false;
	for (iter = 0; iter<numIterations; iter++) {
		Int cur = 0;
		// Note - only sorts the visible trees.
		while (cur<m_numTrees-iter && !m_trees[cur].visible) {
			cur++;
		}
		Int i;
		for (i=cur+1; i<m_numTrees-iter; i++) {
			if (m_trees[i].visible) {
				if (m_trees[cur].sortKey > m_trees[i].sortKey) {
					TTree tmp = m_trees[cur];
					m_trees[cur] = m_trees[i];
					m_trees[i] = tmp;
					swap = true;
				}
				cur = i;
			}
		}
		if (!swap) {
			return;
		}
		m_anythingChanged = true;
	}
}
#endif 

/********** GDIFileStream2 class ****************************/
class GDIFileStream2 : public InputStream
{
protected:
	File* m_file;
public:
	GDIFileStream2():m_file(NULL) {};
	GDIFileStream2(File* pFile):m_file(pFile) {};
	virtual Int read(void *pData, Int numBytes) {
		return(m_file?m_file->read(pData, numBytes):0);
	};
};

//=============================================================================
// W3DTreeBuffer::updateTexture
//=============================================================================
/** Creates a new texture. */
//=============================================================================
void W3DTreeBuffer::updateTexture(void)
{
	
	// Thirty-two tiles across, the way the terrain atlas is, so the bigger tiles fit the
	// same number of trees.
	const Int MAX_TEX_WIDTH = 32*TILE_PIXEL_EXTENT;

	Int i, j;
	Int maxHeight = 0;
	const Int maxTilesPerRow = 32;

	REF_PTR_RELEASE(m_treeTexture);

	Bool availableGrid[maxTilesPerRow][maxTilesPerRow];
	Int row, column;
	for (row=0; row<maxTilesPerRow; row++) {
		for (column=0; column<maxTilesPerRow; column++) {
			availableGrid[row][column] = true;
		}
	}

	for (i=0; i<m_numTiles; i++) {
		REF_PTR_RELEASE (m_sourceTiles[i]);
	}
	m_numTiles = 0;
	File *theFile = NULL;
	for (i=0; i<m_numTreeTypes; i++) {
		char texturePath[ _MAX_PATH ];
		m_treeTypes[i].m_numTiles = 0;
		snprintf( texturePath, ARRAY_SIZE(texturePath), "%s%s",TERRAIN_TGA_DIR_PATH, m_treeTypes[i].m_data->m_textureName.str() );
		theFile = TheFileSystem->openFile( texturePath, File::READ|File::BINARY);
		if (theFile==NULL) {
			snprintf( texturePath, ARRAY_SIZE(texturePath), "%s%s",TGA_DIR_PATH, m_treeTypes[i].m_data->m_textureName.str() );
			theFile = TheFileSystem->openFile( texturePath, File::READ|File::BINARY);
		}
		if (theFile != NULL) {
			GDIFileStream2 theStream(theFile);
			InputStream *pStr = &theStream;
			Bool halfTile;
			Int numTiles = WorldHeightMap::countTiles(pStr, &halfTile);
			Int width;
			for (width = 10; width >= 1; width--) {
				if (numTiles >= width*width) {
					numTiles = width*width;
					break;
				}
			}
			Bool texFound = false;
			for	(j=0; j<i; j++) {
				if (m_treeTypes[j].m_data->m_textureName.compareNoCase(m_treeTypes[i].m_data->m_textureName)==0) {					
					m_treeTypes[i].m_firstTile = 0;
					m_treeTypes[i].m_tileWidth = width;
					m_treeTypes[i].m_numTiles = 0;
					texFound = true;
					break;
				}
			}
			if (texFound) {
				theFile->close();
				continue;
			}
			if (m_numTiles+numTiles<=MAX_TILES) {
				theFile->seek(0, File::START);
				m_treeTypes[i].m_firstTile = m_numTiles;
				m_treeTypes[i].m_tileWidth = width;
				m_treeTypes[i].m_numTiles = numTiles;
				m_treeTypes[i].m_halfTile = halfTile;
				WorldHeightMap::readTiles(pStr, m_sourceTiles+m_treeTypes[i].m_firstTile, width);	
				m_numTiles += numTiles;
			} else {
				m_treeTypes[i].m_firstTile = 0;
				m_treeTypes[i].m_tileWidth = 0;
				m_treeTypes[i].m_numTiles = 0;
			}
			theFile->close();
		} else {
			DEBUG_CRASH(("Could not find texture %s\n", m_treeTypes[i].m_data->m_textureName.str()));
			m_treeTypes[i].m_firstTile = 0;
			m_treeTypes[i].m_tileWidth = 0;
			m_treeTypes[i].m_numTiles = 0;
		}
	}

	Int tmpWidth = 8;
	while (tmpWidth*tmpWidth<m_numTiles) {
		tmpWidth*=2;
	}
	Int tilesPerRow = tmpWidth;
	m_textureWidth = tmpWidth*TILE_PIXEL_EXTENT;
	
	if (m_textureWidth>MAX_TEX_WIDTH) {
		m_textureWidth = 64;
		m_textureHeight = 64;
		if (m_treeTexture==NULL) {
			m_treeTexture = new TextureClass("missing.tga");
		}
		DEBUG_CRASH(("Too many trees in a scene."));
		return;
	}

	for (i=0; i<m_numTiles; i++) {
		if (m_sourceTiles[i]) {
			m_sourceTiles[i]->m_tileLocationInTexture.x = -1;
			m_sourceTiles[i]->m_tileLocationInTexture.y = -1;
		}
	}	 

	/* put the tree tiles into the texture */
	Int texClass;
	Int tileWidth;
	for (tileWidth = tilesPerRow; tileWidth>0; tileWidth--) {
		for (texClass=0; texClass<m_numTreeTypes; texClass++) {
			Int width = m_treeTypes[texClass].m_tileWidth;
			if (width != tileWidth) continue;
			Bool texFound = false;
			for	(i=0; i<texClass; i++) {
				if (m_treeTypes[i].m_data->m_textureName.compareNoCase(m_treeTypes[texClass].m_data->m_textureName)==0) {					
					m_treeTypes[texClass].m_textureOrigin.x = m_treeTypes[i].m_textureOrigin.x;
					m_treeTypes[texClass].m_textureOrigin.y = m_treeTypes[i].m_textureOrigin.y;
					texFound = true;
					break;
				}
			}
			if (texFound) {
				continue;
			}

			// Find an available block of space.
			Bool found = false;
			for (row=0; row<(tilesPerRow-width)+1 && !found; row++) {
				for (column=0; column<(tilesPerRow-width)+1 && !found; column++) {
					if (availableGrid[row][column]) {
						Bool open = true;
						for (i=0; i<width && open; i++) {
							for (j=0; j<width&&open; j++) {
								if (!availableGrid[row+j][column+i]) {
									open = false;
								}
							}
						}
						if (open) found = true;
						break;
					}
				}
				if (found) break;
			}
			if (!found) {
				m_treeTypes[texClass].m_textureOrigin.x = 0;
				m_treeTypes[texClass].m_textureOrigin.y = 0;
				continue;
			}

			Int xOrigin = column*(TILE_PIXEL_EXTENT);
			Int yOrigin = row*(TILE_PIXEL_EXTENT);
			m_treeTypes[texClass].m_textureOrigin.x = xOrigin;
			m_treeTypes[texClass].m_textureOrigin.y = yOrigin;
			Int classHeight = yOrigin + width*TILE_PIXEL_EXTENT;
			if (maxHeight < classHeight) maxHeight = classHeight;

			for (i=0; i<width; i++) {
				for (j=0; j<width; j++) {
					availableGrid[row+j][column+i] = false;
					Int baseNdx = m_treeTypes[texClass].m_firstTile + i + j*width;
					Int x = xOrigin + i*TILE_PIXEL_EXTENT;
					Int y = yOrigin + ((width-j)-1)*TILE_PIXEL_EXTENT;
					m_sourceTiles[baseNdx]->m_tileLocationInTexture.x = x;
					m_sourceTiles[baseNdx]->m_tileLocationInTexture.y = y;
				}
			}
		}
	}
	DEBUG_ASSERTCRASH(maxHeight<=m_textureWidth, ("Bad max height."));
	W3DTreeTextureClass *tex = new W3DTreeTextureClass((UnsignedInt)m_textureWidth, (UnsignedInt)m_textureWidth);
	m_textureHeight = tex->update(this);

	m_treeTexture = tex;
	
	for (i=0; i<m_numTiles; i++) {
		REF_PTR_RELEASE (m_sourceTiles[i]);
	}
	//	m_treeTexture = NEW_REF (TextureClass, (m_treeTypes[0].m_textureName.str()));

}

/**Adjust the resolution of tree texture uploaded to the video card.  The system memory
version always remains at full resolution.  Someone should probably optimize this at
some point since it wastes a lot of system memory on low-end systems. -MW
*/
void W3DTreeBuffer::setTextureLOD(Int lod)
{
	if (m_treeTexture)
		((W3DTreeTextureClass*)m_treeTexture)->setLOD(lod);
}

//=============================================================================
// W3DTreeBuffer::doLighting
//=============================================================================
/** Calculates the diffuse lighting as affected by dynamic lighting. */
//=============================================================================
UnsignedInt W3DTreeBuffer::doLighting(const Vector3 *normal,  
															const GlobalData::TerrainLighting	*objectLighting, 
															const Vector3 *emissive, UnsignedInt vertDiffuse, Real scale) const
{

	Real shadeR, shadeG, shadeB;
	Real shade;
	shadeR = objectLighting[0].ambient.red+emissive->X;	//only the first light contributes to ambient
	shadeG = objectLighting[0].ambient.green+emissive->Y;
	shadeB = objectLighting[0].ambient.blue+emissive->Z;

	Int i;
	for	(i=0; i<MAX_GLOBAL_LIGHTS; i++) {
		Vector3 lightDirection(objectLighting[i].lightPos.x, objectLighting[i].lightPos.y, objectLighting[i].lightPos.z);
		lightDirection.Normalize();
		Vector3 lightRay(-lightDirection.X, -lightDirection.Y, -lightDirection.Z);
		shade = Vector3::Dot_Product(lightRay, *normal); 

		if (shade > 1.0) shade = 1.0;
		if(shade < 0.0f) shade = 0.0f;
		shadeR += shade*objectLighting[i].diffuse.red;
		shadeG += shade*objectLighting[i].diffuse.green;
		shadeB += shade*objectLighting[i].diffuse.blue;	
	}

	shadeR *= scale;
	shadeG *= scale;
	shadeB *= scale;
	
	if (shadeR > 1.0) shadeR = 1.0;
	if(shadeR < 0.0f) shadeR = 0.0f;
	if (shadeG > 1.0) shadeG = 1.0;
	if(shadeG < 0.0f) shadeG = 0.0f;
	if (shadeB > 1.0) shadeB = 1.0;
	if(shadeB < 0.0f) shadeB = 0.0f;
	
	if (vertDiffuse!=0xFFFFFFFF) {
		shade = vertDiffuse&0xff; //blue;
		shadeB *= shade/255.0f;
		shade = (vertDiffuse>>8)&0xFF; // green;
		shadeG *= shade/255.0f;
		shade = (vertDiffuse>>16)&0xFF; // red;
		shadeR *= shade/255.0f;
	}

	shadeR*=255.0f;
	shadeG*=255.0f;
	shadeB*=255.0f;
	const Real alpha = 255.0;
	return REAL_TO_UNSIGNEDINT(shadeB) | (REAL_TO_INT(shadeG) << 8) | (REAL_TO_INT(shadeR) << 16) | ((Int)alpha << 24);
	
}

//=============================================================================
// W3DTreeBuffer::loadTreesInVertexAndIndexBuffers
//=============================================================================
/** Loads the trees into the vertex buffer for drawing. */
//=============================================================================
void W3DTreeBuffer::loadTreesInVertexAndIndexBuffers(RefRenderObjListIterator *pDynamicLightsIterator)
{
	if (!m_indexTree[0] || !m_vertexTree[0] || !m_initialized) {
		return;
	}
	if (!m_anythingChanged) {
		return;
	}

	if (m_shadow == NULL && TheW3DProjectedShadowManager) {
		Shadow::ShadowTypeInfo shadowInfo;
		shadowInfo.m_ShadowName[0] = 0;
		shadowInfo.allowUpdates=FALSE;	//shadow image will never update
		shadowInfo.allowWorldAlign=TRUE;	//shadow image will wrap around world objects
		shadowInfo.m_type = (ShadowType)SHADOW_DECAL;
		shadowInfo.m_sizeX=20;
		shadowInfo.m_sizeY=20;
		shadowInfo.m_offsetX=0;
		shadowInfo.m_offsetY=0;
		m_shadow = TheW3DProjectedShadowManager->createDecalShadow(&shadowInfo);
	}
	
	m_anythingChanged = false;
	Int curTree=0;
	Int bNdx;
	for (bNdx=0; bNdx<MAX_BUFFERS; ++bNdx) m_shadowRanges[bNdx].clear();
	// A slot is only a tree's while this fill gives it one: updateVertexBuffer writes a moved tree
	// into its slot alone, and a slot left over from an older fill now holds some other tree.
	// The fill writes every tree it takes in as it stands, so nothing moved is left to write.
	for (curTree=0; curTree<m_numTrees; curTree++) {
		m_trees[curTree].bufferNdx = -1;
		m_trees[curTree].moved = false;
	}
	m_movedTrees.clear();
	curTree = 0;
	const GlobalData::TerrainLighting *objectLighting = TheGlobalData->m_terrainObjectsLighting[TheGlobalData->m_timeOfDay];
	for (bNdx=0; bNdx<MAX_BUFFERS; bNdx++) {
		m_curNumTreeVertices[bNdx] = 0;
		m_curNumTreeIndices[bNdx] = 0;
		if (curTree >= m_numTrees) {
			break;
		}
		VertexFormatXYZNDUV1 *vb;
		UnsignedShort *ib;
		// Lock the buffers.
	#ifdef USE_STATIC
		DX8IndexBufferClass::WriteLockClass lockIdxBuffer(m_indexTree[bNdx], 0);
		DX8VertexBufferClass::WriteLockClass lockVtxBuffer(m_vertexTree[bNdx], 0);
	#else 
		DX8IndexBufferClass::WriteLockClass lockIdxBuffer(m_indexTree[bNdx], D3DLOCK_DISCARD);
		DX8VertexBufferClass::WriteLockClass lockVtxBuffer(m_vertexTree[bNdx], D3DLOCK_DISCARD);
	#endif
		vb=(VertexFormatXYZNDUV1*)lockVtxBuffer.Get_Vertex_Array();
		ib = lockIdxBuffer.Get_Index_Array();
		// a failed lock - a device that has gone away - hands back nothing to write into
		if (vb == NULL || ib == NULL) {
			DEBUG_LOG(("TREEBUFFER lock failed on buffer %d (vb %d ib %d), %d trees not drawn\n",
				bNdx, vb!=NULL, ib!=NULL, m_numTrees-curTree));
			continue;
		}
		// Add to the index buffer & vertex buffer.
		Vector2 lookAtVector(m_cameraLookAtVector.X, m_cameraLookAtVector.Y);
		lookAtVector.Normalize();
		// We draw from back to front, so we put the indexes in the buffer 
		// from back to front.
		UnsignedShort *curIb = ib;

		VertexFormatXYZNDUV1 *curVb = vb;



		
		for ( ;curTree<m_numTrees;curTree++) {
			Int type = m_trees[curTree].treeType;
			if (type<0) {
				continue; // Deleted tree. [6/9/2003]
			}
			if (!m_trees[curTree].visible) continue;
			Real scale = m_trees[curTree].scale;
			Vector3 loc = m_trees[curTree].location;
			Real theSin = m_trees[curTree].sin;
			Real theCos = m_trees[curTree].cos;
			if (type<0 || m_treeTypes[type].m_mesh == 0) {
				continue;
			}

			Bool doVertexLighting = true;

	#if 0 // no dynamic lighting.
			for (pDynamicLightsIterator->First(); !pDynamicLightsIterator->Is_Done(); pDynamicLightsIterator->Next())
			{		
				W3DDynamicLight *pLight = (W3DDynamicLight*)pDynamicLightsIterator->Peek_Obj();
				if (!pLight->isEnabled()) {
					continue; // he is turned off.
				}
				if (CollisionMath::Overlap_Test(m_trees[curTree].bounds, pLight->Get_Bounding_Sphere()) == CollisionMath::OUTSIDE) {
					continue; // this tree is outside of the light's influence.
				}
				doVertexLighting = true;
			}
	#endif
			Vector3 emissive(0.0f,0.0f,0.0f);
			MaterialInfoClass * matInfo = m_treeTypes[type].m_mesh->Get_Material_Info();
			if (matInfo) {
				VertexMaterialClass *vertMat = matInfo->Peek_Vertex_Material(0);
				if (vertMat) {
					vertMat->Get_Emissive(&emissive);
				}
			}
			REF_PTR_RELEASE(matInfo);


			Int startVertex = m_curNumTreeVertices[bNdx];
			Int i;
			Int numVertex = m_treeTypes[type].m_mesh->Peek_Model()->Get_Vertex_Count();
			Vector3 *pVert = m_treeTypes[type].m_mesh->Peek_Model()->Get_Vertex_Array();



			// If we happen to have too many trees, stop.
			if (m_curNumTreeVertices[bNdx]+numVertex+2>= MAX_TREE_VERTEX) {
				DEBUG_LOG(("TREEBUDGET buffer %d full at %d vertices, tree %d of %d\n",
					bNdx, m_curNumTreeVertices[bNdx], curTree, m_numTrees));
				break;
			}
			Int numIndex = m_treeTypes[type].m_mesh->Peek_Model()->Get_Polygon_Count();
			const TriIndex *pPoly = m_treeTypes[type].m_mesh->Peek_Model()->Get_Polygon_Array();
			if (m_curNumTreeIndices[bNdx]+3*numIndex+6 >= MAX_TREE_INDEX) {
				DEBUG_LOG(("TREEBUDGET buffer %d full at %d indices, tree %d of %d\n",
					bNdx, m_curNumTreeIndices[bNdx], curTree, m_numTrees));
				break;
			}
			m_trees[curTree].firstIndex = startVertex;
			m_trees[curTree].bufferNdx = bNdx;

			const Vector2*uvs=m_treeTypes[type].m_mesh->Peek_Model()->Get_UV_Array_By_Index(0);
			
			const Vector3*normals = m_treeTypes[type].m_mesh->Peek_Model()->Get_Vertex_Normal_Array();
			const unsigned *vecDiffuse = m_treeTypes[type].m_mesh->Peek_Model()->Get_Color_Array(0, false);

			Int diffuse = 0;
			if (normals == NULL) {
				doVertexLighting = false;
				Vector3 normal(0.0f,0.0f,1.0f);
				diffuse = doLighting(&normal, objectLighting, &emissive, 0xFFFFFFFF, 1.0f);
			}
	/*
	 *	
			// If we are doing reduced resolution terrain, do reduced
			// poly trees.
			Bool doPanel = (TheGlobalData->m_useHalfHeightMap || TheGlobalData->m_stretchTerrain);

			if (doPanel) {
				if (m_trees[curTree].rotates) {
					theSin = -lookAtVector.X;
					theCos = lookAtVector.Y;
				}
				// panel start is index offset, there are 3 index per triangle.
				if (m_trees[curTree].panelStart/3 + 2 > numIndex) {
					continue; // not enought polygons for the offset.  jba.
				}
				for (j=0; j<6; j++) {
					i = ((Int *)pPoly)[j+m_trees[curTree].panelStart];
					if (m_curNumTreeVertices >= MAX_TREE_VERTEX) 
						break;

					// Update the uv values.  The W3D models each have their own texture, and 
					// we use one texture with all images in one, so we have to change the uvs to 
					// match.
					Real U, V;
					if (type==SHRUB) {
						// shrub texture is tucked in the corner
						U = ((512-64)+uvs[i].U*64.0f)/512.0f;
						V = ((256-64)+uvs[i].V*64.0f)/256.0f;
					} else if (type==FENCE) {
						U = uvs[i].U*0.5f; 
						V = 1.0f + uvs[i].V;		
					} else {
						U = typeOffset+uvs[i].U*0.5f; 
						V = uvs[i].V;		
					}

					curVb->u1 = U;
					curVb->v1 = V/2.0;
					Vector3 vLoc;
					vLoc.X = pVert[i].X*scale*theCos - pVert[i].Y*scale*theSin;
					vLoc.Y = pVert[i].Y*scale*theCos + pVert[i].X*scale*theSin;

					vLoc.X += loc.X;
					vLoc.Y += loc.Y;
					vLoc.Z = loc.Z + pVert[i].Z*scale; 

					curVb->x = vLoc.X;
					curVb->y = vLoc.Y;
					curVb->z = vLoc.Z;	 
					if (doVertexLighting) {
						curVb->diffuse = doLighting(&vLoc, shadeR, shadeG, shadeB, m_trees[curTree].bounds, pDynamicLightsIterator);
					} else {
						curVb->diffuse = diffuse;
					}
					curVb++;
					m_curNumTreeVertices++;
				}

				for (i=0; i<6; i++) {
					if (m_curNumTreeIndices+4 > MAX_TREE_INDEX) 
						break;
					curIb--;
					*curIb = startVertex + i;
					m_curNumTreeIndices++;
				}
			} else {
	 */
			Real Uscale = m_treeTypes[type].m_tileWidth * (Real)TILE_PIXEL_EXTENT / (Real)m_textureWidth;
			Real Vscale = m_treeTypes[type].m_tileWidth * (Real)TILE_PIXEL_EXTENT / (Real)m_textureHeight;
			Real UOffset = m_treeTypes[type].m_textureOrigin.x/(Real)m_textureWidth; 
			Real VOffset = m_treeTypes[type].m_textureOrigin.y/(Real)m_textureHeight; 
			if (m_treeTypes[type].m_halfTile) {
				Uscale *= 0.5f;
				Vscale *= 0.5f;
				VOffset += (TILE_PIXEL_EXTENT/2) / (Real)m_textureHeight;
			}
			for (i=0; i<numVertex; i++) {
				if (m_curNumTreeVertices[bNdx] >= MAX_TREE_VERTEX) 
					break;

				// Update the uv values.  The W3D models each have their own texture, and 
				// we use one texture with all images in one, so we have to change the uvs to 
				// match.
				Real U, V;
				U = uvs[i].U; 
				V = uvs[i].V;	
				
				if (U>1.0f) U=1.0f;
				if (U<0.0f) U=0.0f;
				if (V>1.0f) V=1.0f;
				if (V<0.0f) V=0.0f;

				curVb->u1 = U*Uscale + UOffset;
				curVb->v1 = V*Vscale + VOffset;
				Real x = pVert[i].X;
				Real y = pVert[i].Y;

				
				Vector3 vLoc;
				x += m_treeTypes[type].m_offset.X;
				y += m_treeTypes[type].m_offset.Y;
				vLoc.X = x*scale*theCos - y*scale*theSin;
				vLoc.Y = y*scale*theCos + x*scale*theSin;
				vLoc.Z = pVert[i].Z*scale; 
				vLoc.Z += m_treeTypes[type].m_offset.Z;

				if (m_trees[curTree].m_toppleState != TOPPLE_UPRIGHT) {
					Matrix3D::Transform_Vector(m_trees[curTree].m_mtx, vLoc, &vLoc);
				} else {
					if (m_trees[curTree].pushAside>0.0f) {
						vLoc.X += pVert[i].Z * m_trees[curTree].pushAside * m_trees[curTree].pushAsideCos * m_treeTypes[type].m_data->m_maxOutwardMovement;
						vLoc.Y += pVert[i].Z * m_trees[curTree].pushAside * m_trees[curTree].pushAsideSin* m_treeTypes[type].m_data->m_maxOutwardMovement;
					}
					vLoc.X += loc.X;
					vLoc.Y += loc.Y;
					vLoc.Z += loc.Z;
				}


				curVb->x = vLoc.X;
				curVb->y = vLoc.Y;
				curVb->z = vLoc.Z;
				curVb->nx = m_trees[curTree].swayType;
				curVb->ny = 1.0f - m_treeTypes[type].m_data->m_darkening*m_trees[curTree].pushAside;
				curVb->nz = loc.Z;
				if (doVertexLighting) {
					Vector3 normal(0.0f, 0.0f, 1.0f);
					if (normals) {
						normal.X = normals[i].X*theCos - normals[i].Y*theSin;
						normal.Y = normals[i].Y*theCos + normals[i].X*theSin;
						normal.Z = normals[i].Z;
					}
					UnsignedInt vertexDiffuse;
					if (vecDiffuse) {
						vertexDiffuse = vecDiffuse[i];
					} else {
						vertexDiffuse = 0xffffffff;
					}
					curVb->diffuse = doLighting(&normal, objectLighting, &emissive, 
														vertexDiffuse, 1.0f);
				} else {
					curVb->diffuse = diffuse;
				}
				curVb++;
				m_curNumTreeVertices[bNdx]++;
			}

			const Int firstShadowIndex = m_curNumTreeIndices[bNdx];
			try {
			for (i=0; i<numIndex; i++) {
				if (m_curNumTreeIndices[bNdx]+4 > MAX_TREE_INDEX) 
					break;
				*curIb++ = startVertex + pPoly[i].I;
				*curIb++ = startVertex + pPoly[i].J;
				*curIb++ = startVertex + pPoly[i].K;
				m_curNumTreeIndices[bNdx]+=3;
			}
			IndexBufferExceptionFunc();
			} catch(...) {
				IndexBufferExceptionFunc();
			}
			const Int shadowCount = m_curNumTreeIndices[bNdx] - firstShadowIndex;
			if (shadowCount && (m_treeTypes[type].m_doShadow || TheGlobalData->m_shadowsForProps)) {
				std::vector<ShadowRange> &ranges = m_shadowRanges[bNdx];
				if (!ranges.empty() && ranges.back().first + ranges.back().count == firstShadowIndex)
					ranges.back().count += shadowCount;
				else ranges.push_back({firstShadowIndex, shadowCount});
			}
		}		
	}

}
//=============================================================================
// W3DTreeBuffer::updateVertexBuffer
//=============================================================================
/** Writes the trees stepTrees moved into their own slots and nothing else.  It used to lock each
whole buffer, and on Direct3D 11 a static buffer's unlock copies and uploads every byte it locked:
about a megabyte a buffer, four buffers, for one bush a soldier walked through, and in a crowd that
was most passes.  stepTrees lists the trees it moved; each buffer takes one lock over the span its
moved trees cover, so a move is written once, by whichever pass comes first. */
//=============================================================================
void W3DTreeBuffer::updateVertexBuffer(void)
{
	if (!m_indexTree[0] || !m_vertexTree[0] || !m_initialized) {
		return;
	}
	Int lo[MAX_BUFFERS], hi[MAX_BUFFERS];
	Int bNdx;
	for (bNdx=0; bNdx<MAX_BUFFERS; bNdx++) {
		lo[bNdx] = MAX_TREE_VERTEX;
		hi[bNdx] = 0;
	}
	Int kept = 0;
	Int n;
	for (n=0; n<(Int)m_movedTrees.size(); n++) {
		TTree &tree = m_trees[m_movedTrees[n]];
		// Deleted, culled out or left out of the fill: the fill that next takes it in writes it as it
		// stands then.
		if (tree.treeType<0 || !tree.visible || tree.bufferNdx<0) {
			tree.moved = false;
			continue;
		}
		const Int end = tree.firstIndex + m_treeTypes[tree.treeType].m_mesh->Peek_Model()->Get_Vertex_Count();
		if (tree.firstIndex < lo[tree.bufferNdx]) lo[tree.bufferNdx] = tree.firstIndex;
		if (end > hi[tree.bufferNdx]) hi[tree.bufferNdx] = end;
		m_movedTrees[kept++] = m_movedTrees[n];
	}
	m_movedTrees.resize(kept);

	for (bNdx=0; bNdx<MAX_BUFFERS; bNdx++) {
		if (hi[bNdx] <= lo[bNdx]) {
			continue;
		}
		DX8VertexBufferClass::AppendLockClass lockVtxBuffer(m_vertexTree[bNdx], lo[bNdx], hi[bNdx]-lo[bNdx]);
		VertexFormatXYZNDUV1 *vb = (VertexFormatXYZNDUV1*)lockVtxBuffer.Get_Vertex_Array();
		// The lock fails when the device has gone out from under us: its trees wait for the next pass.
		if (vb == NULL) {
			m_anyPushChanged = true;
			continue;
		}
		for (n=0; n<(Int)m_movedTrees.size(); n++) {
			TTree &tree = m_trees[m_movedTrees[n]];
			if (tree.bufferNdx != bNdx) {
				continue;
			}
			tree.moved = false;
			const Int type = tree.treeType;
			const Int numVertex = m_treeTypes[type].m_mesh->Peek_Model()->Get_Vertex_Count();
			const Vector3 *pVert = m_treeTypes[type].m_mesh->Peek_Model()->Get_Vertex_Array();
			VertexFormatXYZNDUV1 *curVb = vb + (tree.firstIndex - lo[bNdx]);

			const Real scale = tree.scale;
			const Vector3 loc = tree.location;
			const Real theSin = tree.sin;
			const Real theCos = tree.cos;
			for (Int i=0; i<numVertex; i++) {
				Real x = pVert[i].X;
				Real y = pVert[i].Y;

				Vector3 vLoc;
				x += m_treeTypes[type].m_offset.X;
				y += m_treeTypes[type].m_offset.Y;
				vLoc.X = x*scale*theCos - y*scale*theSin;
				vLoc.Y = y*scale*theCos + x*scale*theSin;
				vLoc.Z = pVert[i].Z*scale;
				vLoc.Z += m_treeTypes[type].m_offset.Z;

				if (tree.m_toppleState != TOPPLE_UPRIGHT) {
					tree.m_mtx.Transform_Vector(tree.m_mtx, vLoc, &vLoc);
				} else {
					if (tree.pushAside>0.0f) {
						vLoc.X += pVert[i].Z * tree.pushAside * tree.pushAsideCos * m_treeTypes[type].m_data->m_maxOutwardMovement;
						vLoc.Y += pVert[i].Z * tree.pushAside * tree.pushAsideSin* m_treeTypes[type].m_data->m_maxOutwardMovement;
					}
					vLoc.X += loc.X;
					vLoc.Y += loc.Y;
					vLoc.Z += loc.Z;
				}

				curVb->x = vLoc.X;
				curVb->y = vLoc.Y;
				curVb->z = vLoc.Z;
				curVb->ny = 1.0f - m_treeTypes[type].m_data->m_darkening*tree.pushAside;
				curVb++;
			}
		}
	}

	// what a failed lock left stays listed
	kept = 0;
	for (n=0; n<(Int)m_movedTrees.size(); n++) {
		if (m_trees[m_movedTrees[n]].moved) {
			m_movedTrees[kept++] = m_movedTrees[n];
		}
	}
	m_movedTrees.resize(kept);
}

//-----------------------------------------------------------------------------
//         Public Functions                                                
//-----------------------------------------------------------------------------

//=============================================================================
// W3DTreeBuffer::~W3DTreeBuffer
//=============================================================================
/** Destructor. Releases w3d assets. */
//=============================================================================
W3DTreeBuffer::~W3DTreeBuffer(void)
{
	freeTreeBuffers();
	REF_PTR_RELEASE(m_treeTexture);
	Int i;
	for (i=0; i<MAX_TYPES; i++) {
		REF_PTR_RELEASE(m_treeTypes[i].m_mesh);
	}
	if (m_shadow) {
		delete m_shadow;
		m_shadow = NULL;
	}
}

//=============================================================================
// W3DTreeBuffer::W3DTreeBuffer
//=============================================================================
/** Constructor. Sets m_initialized to true if it finds the w3d models it needs
for the trees. */
//=============================================================================
W3DTreeBuffer::W3DTreeBuffer(void)
{
	memset(this, sizeof(W3DTreeBuffer), 0);
	m_initialized = false;
	Int i;
	for	(i=0; i<MAX_BUFFERS; i++) {
		m_vertexTree[i] = NULL;
		m_indexTree[i] = NULL;
		m_curNumTreeVertices[i]=0;
		m_curNumTreeIndices[i]=0;
	}
	m_treeTexture = NULL;
	m_dwTreeVertexShader = 0;
	m_dwTreePixelShader = 0;
	// the memset above has its arguments the wrong way round and writes nothing, so anything
	// clearAllTrees releases has to be null before it is called
	m_numModelShadows = 0;
	m_numShadowTextures = 0;
	clearAllTrees();
	allocateTreeBuffers();
	m_initialized = true;
	m_curSwayVersion = -1;
	m_lastSwayFrame = 0xffffffff;	// so the first drawTrees always steps
	m_lastTreeStepFrame = 0xffffffff;

	m_shadow = NULL;

}


//=============================================================================
// W3DTreeBuffer::freeTreeBuffers
//=============================================================================
/** Frees the index and vertex buffers. */
//=============================================================================
void W3DTreeBuffer::freeTreeBuffers(void)
{
	Int i;
	for	(i=0; i<MAX_BUFFERS; i++) {
		REF_PTR_RELEASE(m_vertexTree[i]);
		REF_PTR_RELEASE(m_indexTree[i]);
	}
	
	if (m_dwTreePixelShader)
		m_dwTreePixelShader->Release();
	m_dwTreePixelShader = 0;

	if (m_dwTreeVertexShader)
		m_dwTreeVertexShader->Release();
	if (m_treeVertexDeclaration)
	{	m_treeVertexDeclaration->Release();
		m_treeVertexDeclaration=NULL;
	}
	m_dwTreeVertexShader = 0;
}

//=============================================================================
// W3DTreeBuffer::unitMoved
//=============================================================================
/** Check to see if a unit collided with a tree/grass/bush. */
//=============================================================================
void W3DTreeBuffer::unitMoved(Object *unit)
{
	if (unit->isKindOf(KINDOF_IMMOBILE)) {
		// This is the initial positioning of the object, and we don't care. jba. [6/5/2003]
		return;
	}
	Real radius = unit->getGeometryInfo().getMajorRadius();
	if (unit->getGeometryInfo().getGeomType()==GEOMETRY_BOX) {
		if (radius>unit->getGeometryInfo().getMinorRadius()) {
			radius = unit->getGeometryInfo().getMinorRadius();
		}
	}
	// Value to assume for the tree radius.
#define TREE_RADIUS_APPROX 7.0f
	radius += TREE_RADIUS_APPROX;

	Coord3D pos = *unit->getPosition();
	Real x = pos.x-radius;
	Real y = pos.y-radius;
	if (x<m_bounds.lo.x) x = m_bounds.lo.x;
	if (y<m_bounds.lo.y) y = m_bounds.lo.y;
	if (x>m_bounds.hi.x) x = m_bounds.hi.x;
	if (y>m_bounds.hi.y) y = m_bounds.hi.y;
	Int xIndex = REAL_TO_INT_FLOOR ( (x/(m_bounds.hi.x-m_bounds.lo.x)) * (PARTITION_WIDTH_HEIGHT-0.1f) );
	Int yIndex = REAL_TO_INT_FLOOR ( (y/(m_bounds.hi.y-m_bounds.lo.y)) * (PARTITION_WIDTH_HEIGHT-0.1f) );
	DEBUG_ASSERTCRASH(xIndex>=0 && yIndex>=0 && xIndex<PARTITION_WIDTH_HEIGHT && yIndex<PARTITION_WIDTH_HEIGHT, ("Invalid range."));

	x = pos.x+radius;
	y = pos.y+radius;
	if (x<m_bounds.lo.x) x = m_bounds.lo.x;
	if (y<m_bounds.lo.y) y = m_bounds.lo.y;
	if (x>m_bounds.hi.x) x = m_bounds.hi.x;
	if (y>m_bounds.hi.y) y = m_bounds.hi.y;
	Int xMax = REAL_TO_INT_CEIL ( (x/(m_bounds.hi.x-m_bounds.lo.x)) * (PARTITION_WIDTH_HEIGHT-0.1f) );
	Int yMax = REAL_TO_INT_CEIL ( (y/(m_bounds.hi.y-m_bounds.lo.y)) * (PARTITION_WIDTH_HEIGHT-0.1f) );
	DEBUG_ASSERTCRASH(xMax>=0 && yMax>=0 && xMax<=PARTITION_WIDTH_HEIGHT && yMax<=PARTITION_WIDTH_HEIGHT, ("Invalid range."));
	Int i, j;
	for (i=xIndex; i<xMax; i++) {
		for (j=yIndex; j<yMax; j++) {
			Int treeNdx = m_areaPartition[i + PARTITION_WIDTH_HEIGHT*j];
			while (treeNdx != END_OF_PARTITION) {
				// paranoia [7/7/2003]
				if (treeNdx<0 || treeNdx>=m_numTrees) {
					DEBUG_CRASH(("Invalid index."));
					break;
				}
				if (m_trees[treeNdx].treeType<0) {
					treeNdx = m_trees[treeNdx].nextInPartition;
					continue;	//  Tree is deleted. [7/11/2003]
				}
				Coord3D delta;
				delta.set(m_trees[treeNdx].location.X, m_trees[treeNdx].location.Y, m_trees[treeNdx].location.Z );
				delta.sub(&pos);
				if (radius*radius>delta.lengthSqr()) {
					bool canTopple = unit->getCrusherLevel() > 1;
					if (canTopple && m_treeTypes[m_trees[treeNdx].treeType].m_data->m_doTopple) {
						// Give a vector with direction to thing.
						Coord3D toppleVector;
						toppleVector.set(m_trees[treeNdx].location.X, m_trees[treeNdx].location.Y, 0);
						toppleVector.x -= unit->getPosition()->x;
						toppleVector.y -= unit->getPosition()->y;
						applyTopplingForce(m_trees+treeNdx, &toppleVector, 0, W3D_TOPPLE_OPTIONS_NONE);
					} else if (m_treeTypes[m_trees[treeNdx].treeType].m_data->m_framesToMoveOutward>1) {
						pushAsideTree(m_trees[treeNdx].drawableID, &pos, unit->getUnitDirectionVector2D(), unit->getID());
					}
				}
				treeNdx = m_trees[treeNdx].nextInPartition;
			}
		}
	}


}

//=============================================================================
// W3DTreeBuffer::allocateTreeBuffers
//=============================================================================
/** Allocates the index and vertex buffers. */
//=============================================================================
void W3DTreeBuffer::allocateTreeBuffers(void)
{
	Int i;
	for	(i=0; i<MAX_BUFFERS; i++) {
	#ifdef USE_STATIC
		m_vertexTree[i]=NEW_REF(DX8VertexBufferClass,(DX8_FVF_XYZNDUV1,MAX_TREE_VERTEX+4,DX8VertexBufferClass::USAGE_DEFAULT));
		m_indexTree[i]=NEW_REF(DX8IndexBufferClass,(MAX_TREE_INDEX+4, DX8IndexBufferClass::USAGE_DEFAULT));
	#else
		m_vertexTree[i]=NEW_REF(DX8VertexBufferClass,(DX8_FVF_XYZNDUV1,MAX_TREE_VERTEX+4,DX8VertexBufferClass::USAGE_DYNAMIC));
		m_indexTree[i]=NEW_REF(DX8IndexBufferClass,(MAX_TREE_INDEX+4, DX8IndexBufferClass::USAGE_DYNAMIC));
	#endif
		m_curNumTreeVertices[i]=0;
		m_curNumTreeIndices[i]=0;
	}

		//shader decleration
	// DX8_FVF_XYZNDUV1
	RenderUInt32 Declaration[] =
	{
		D3DVSD_STREAM( 0 ),
		D3DVSD_REG( 0, D3DVSDT_FLOAT3 ),  // Position
		D3DVSD_REG( 1, D3DVSDT_FLOAT3 ),  // Normal
		D3DVSD_REG( 2, D3DVSDT_D3DCOLOR), // Diffuse color	
		D3DVSD_REG( 7, D3DVSDT_FLOAT2 ),  // Tex coord
		D3DVSD_END()
	};

	RenderResult hr;
	hr = W3DShaderManager::LoadAndCreateD3DVertexShader("shaders\\Trees.vso", &Declaration[0], &m_dwTreeVertexShader, &m_treeVertexDeclaration);
	if (Render_Failed(hr))
		return;

	hr = W3DShaderManager::LoadAndCreateD3DPixelShader("shaders\\Trees.pso", &m_dwTreePixelShader);
	if (Render_Failed(hr))
		return;
}

//=============================================================================
// W3DTreeBuffer::clearAllTrees
//=============================================================================
/** Removes all trees. */
//=============================================================================
void W3DTreeBuffer::clearAllTrees(void)
{
	Int j;
	for (j=0; j<m_numModelShadows; j++) {
		Int k;
		for (k=0; k<m_modelShadows[j].numMesh; k++) {
			REF_PTR_RELEASE(m_modelShadows[j].mesh[k]);
		}
	}
	m_numModelShadows = 0;
	for (j=0; j<m_numShadowTextures; j++) {
		REF_PTR_RELEASE(m_shadowTextures[j]);
	}
	m_numShadowTextures = 0;

	m_numTrees=0;
	m_movedTrees.clear();
	m_bounds.lo.x = m_bounds.lo.y = 0;
	m_bounds.hi.x = m_bounds.hi.y = 1;
	REF_PTR_RELEASE(m_treeTexture);
	m_curNumTreeIndices[0]=0;
	m_anythingChanged = true;
	Int i;
	for (i=0; i<MAX_TYPES; i++) {
		REF_PTR_RELEASE(m_treeTypes[i].m_mesh);
	}
	for (i=0; i<PARTITION_WIDTH_HEIGHT*PARTITION_WIDTH_HEIGHT; i++) {
		m_areaPartition[i] = END_OF_PARTITION;
	}
	m_numTreeTypes = 0;
}

//=============================================================================
// W3DTreeBuffer::removeTree
//=============================================================================
/** Removes a tree.  */
//=============================================================================
void W3DTreeBuffer::removeTree(DrawableID id)
{
	Int i;
	for (i=0; i<m_numTrees; i++) {
		if (m_trees[i].drawableID == id) {
			m_trees[i].location = Vector3(0,0,0);
			m_trees[i].treeType = DELETED_TREE_TYPE;
			// Translate the bounding sphere of the model.
			m_trees[i].bounds.Center = Vector3(0,0,0);
			m_trees[i].bounds.Radius = 1;
			m_anythingChanged = true;
		}
	}
}

//=============================================================================
// W3DTreeBuffer::removeTree
//=============================================================================
/** Removes any trees that would be under a building.  */
//=============================================================================
void W3DTreeBuffer::removeTreesForConstruction(const Coord3D* pos, const GeometryInfo& geom, Real angle )
{
	// Just iterate all trees, as even non-collidable ones get removed. jba. [7/11/2003]
	Int i;
	for (i=0; i<m_numTrees; i++) {				// small, height,							radius,									minor radius
		if (m_trees[i].treeType < 0) {
			continue; // already deleted. jba [7/11/2003]
		}
		GeometryInfo info(GEOMETRY_CYLINDER, false, 5*TREE_RADIUS_APPROX, 2*TREE_RADIUS_APPROX, 2*TREE_RADIUS_APPROX);
		Coord3D treePos;
		treePos.set(m_trees[i].location.X, m_trees[i].location.Y, m_trees[i].location.Z);
		if (ThePartitionManager->geomCollidesWithGeom( pos, geom, angle, &treePos, info, 0.0f)) {
			// remove it [7/11/2003]
			m_trees[i].treeType = DELETED_TREE_TYPE;
			m_anythingChanged = true;
		} 
	}
}


//=============================================================================
// W3DTreeBuffer::addTreeTypes
//=============================================================================
/** Adds a type of tree (model & texture). */
//=============================================================================
//
// Every failure below returns -1, which is what addTree's "if (treeType<0)" already reads as "do
// not add this tree".  They used to return 0: a valid index, and when the failing type was the
// first one on the map, an index at a slot whose m_data was still null.  The tree was added anyway
// and put in the area partition, and the first unit to drive within its radius dereferenced that
// null in unitMoved - the access violation at 0x10 (m_framesToMoveOutward) a player hit with the
// base game's W3D.big not loaded, so every tree model on the map was missing.
//=============================================================================
Int W3DTreeBuffer::addTreeType(const W3DTreeDrawModuleData *data)
{
	if (m_numTreeTypes>=MAX_TYPES) {
		DEBUG_CRASH(("Too many kinds of trees in map.  Reduce kinds of trees, or raise tree limit. jba."));
		return -1;
	}
	m_needToUpdateTexture = true;

	m_treeTypes[m_numTreeTypes].m_mesh = NULL;

	RenderObjClass *robj=WW3DAssetManager::Get_Instance()->Create_Render_Obj(data->m_modelName.str());

	if (robj==NULL) {
		DEBUG_CRASH(("Unable to find model for tree %s\n", data->m_modelName.str()));
		return -1;
	}
	AABoxClass box;

	robj->Get_Obj_Space_Bounding_Box(box);
	Vector3 offset(0,0,0);
	if (robj->Class_ID() == RenderObjClass::CLASSID_HLOD) {
		RenderObjClass *hlod = robj;
		robj = hlod->Get_Sub_Object(0);
		// An HLOD with nothing in it answers this with NULL, and the bone transform below read
		// straight off it.  The model comes out of a big file the player supplies, so an empty one
		// is a thing that arrives rather than a thing that cannot happen.
		if (robj==NULL) {
			DEBUG_CRASH(("Tree %s is an empty hierarchy.\n", data->m_modelName.str()));
			REF_PTR_RELEASE(hlod);
			return -1;
		}
		const Matrix3D xfm = robj->Get_Bone_Transform(0);
		xfm.Get_Translation(&offset);
		REF_PTR_RELEASE(hlod);
	}

	if (robj->Class_ID() == RenderObjClass::CLASSID_MESH) {
		m_treeTypes[m_numTreeTypes].m_mesh = (MeshClass*)robj;
	} else {
		// m_mesh takes over the reference Create_Render_Obj (or Get_Sub_Object) handed us; every
		// other class walks out of this function and has to give it back.  A map full of trees the
		// asset manager answers with the wrong class leaked one render object per tree, because
		// addTreeType is called again for every instance of a type that never got added.
		REF_PTR_RELEASE(robj);
	}

	DEBUG_LOG(("TREETYPE %d: %s\n", m_numTreeTypes, data->m_modelName.str()));

	if (m_treeTypes[m_numTreeTypes].m_mesh==NULL) {
		DEBUG_CRASH(("Tree %s is not simple mesh. Tell artist to re-export. Don't Ignore!!!\n", data->m_modelName.str()));
		return -1;
	}

	Int numVertex = m_treeTypes[m_numTreeTypes].m_mesh->Peek_Model()->Get_Vertex_Count();
	Vector3 *pVert = m_treeTypes[m_numTreeTypes].m_mesh->Peek_Model()->Get_Vertex_Array();

	const Matrix3D xfm = m_treeTypes[m_numTreeTypes].m_mesh->Get_Transform();
	SphereClass bounds(pVert, numVertex);
	bounds.Center += offset;
	m_treeTypes[m_numTreeTypes].m_bounds = bounds;
	m_treeTypes[m_numTreeTypes].m_textureOrigin.x = 0;
	m_treeTypes[m_numTreeTypes].m_textureOrigin.y = 0;
	m_treeTypes[m_numTreeTypes].m_data = data;
	m_treeTypes[m_numTreeTypes].m_offset = offset;
	m_treeTypes[m_numTreeTypes].m_shadowSize = (box.Extent.X + box.Extent.Y); // Average extent * 2. jba.
	m_treeTypes[m_numTreeTypes].m_doShadow = data->m_doShadow;
	m_numTreeTypes++;
	return m_numTreeTypes-1;
}

//=============================================================================
// W3DTreeBuffer::addTree
//=============================================================================
/** Adds a tree.  Name is the W3D model name, supported models are
ALPINE, DECIDUOUS and SHRUB. */
//=============================================================================
void W3DTreeBuffer::addTree(DrawableID id, Coord3D location, Real scale, Real angle,
								Real randomScaleAmount, const W3DTreeDrawModuleData *data)
{
	if (m_numTrees >= MAX_TREES) {
		return;  
	}
	if (!m_initialized) {
		return;  
	}
	Int treeType = DELETED_TREE_TYPE;
	Int i;
	for (i=0; i<m_numTreeTypes; i++) {
		if (m_treeTypes[i].m_data->m_modelName.compareNoCase(data->m_modelName)==0 && 
				m_treeTypes[i].m_data->m_textureName.compareNoCase(data->m_textureName)==0) {
			treeType = i;
			break;
		}
	}
	if (treeType<0) {
		treeType = addTreeType(data);
		if (treeType<0) {
			return;
		}
		m_needToUpdateTexture = true;
	}
	if (data->m_framesToMoveOutward > 2 || data->m_doTopple) {
		// Trees/grass that topples or gets pushed aside (outward) gets put in the area partition. jba [7/7/2003]
		Short bucket = getPartitionBucket(location);
		m_trees[m_numTrees].nextInPartition = m_areaPartition[bucket];
		m_areaPartition[bucket] = m_numTrees;
	} else {
		m_trees[m_numTrees].nextInPartition = END_OF_PARTITION;
	}

	Real randomScale = GameClientRandomValueReal( 1.0f - randomScaleAmount, 1.0f+ randomScaleAmount );
	m_trees[m_numTrees].sin = WWMath::Sin(angle);
	m_trees[m_numTrees].cos = WWMath::Cos(angle);
	if (randomScaleAmount>0.0f) {
		// Randomizes the scale and orientation of trees.
		m_trees[m_numTrees].scale = scale*randomScale;
	} else {
		// Don't randomly scale & orient
		m_trees[m_numTrees].scale = scale;
	}
	m_trees[m_numTrees].location = Vector3(location.x, location.y, location.z);
	m_trees[m_numTrees].treeType = treeType;
	// Translate the bounding sphere of the model.
	m_trees[m_numTrees].bounds = m_treeTypes[treeType].m_bounds;
	m_trees[m_numTrees].bounds.Center *= m_trees[m_numTrees].scale;
	m_trees[m_numTrees].bounds.Radius *= m_trees[m_numTrees].scale;
	m_trees[m_numTrees].bounds.Center += m_trees[m_numTrees].location;
	// Initially set it invisible.  cull will update it's visiblity flag.
	m_trees[m_numTrees].visible = false;
	m_trees[m_numTrees].drawableID = id;

	m_trees[m_numTrees].firstIndex = 0;
	m_trees[m_numTrees].bufferNdx = -1;
	m_trees[m_numTrees].moved = false;

	//
	// swayType is a vertex shader constant index counted from c8, and c8 itself is the all-zero
	// "no sway" slot - the ten real ones are c9 to c18.  So the range is 1..MAX_SWAY_TYPES, which
	// is what updateSway rolls when the breeze changes; this one rolled 0..MAX_SWAY_TYPES-1, and a
	// tree that drew a zero stood perfectly still.  One tree in ten, until the next breeze change.
	//
	m_trees[m_numTrees].swayType = 1+GameClientRandomValue(0, MAX_SWAY_TYPES-1);
	m_trees[m_numTrees].pushAside = 0;
	m_trees[m_numTrees].lastFrameUpdated = 0;
	m_trees[m_numTrees].pushAsideSource = INVALID_ID;
	m_trees[m_numTrees].pushAsideDelta = 0;
	m_trees[m_numTrees].pushAsideCos = 1;
	m_trees[m_numTrees].pushAsideSin = 1;
	m_trees[m_numTrees].m_toppleState = TOPPLE_UPRIGHT;
	m_numTrees++;
}

//=============================================================================
// W3DTreeBuffer::updateTreePosition
//=============================================================================
/** Updates a tree's position */
//=============================================================================
Bool W3DTreeBuffer::updateTreePosition(DrawableID id, Coord3D location, Real angle)
{
	Int i;
	for (i=0; i<m_numTrees; i++) {
		if (m_trees[i].drawableID == id) {
			m_trees[i].location = Vector3(location.x, location.y, location.z);
			m_trees[i].sin = WWMath::Sin(angle);
			m_trees[i].cos = WWMath::Cos(angle);
			// Translate the bounding sphere of the model.
			m_trees[i].bounds = m_treeTypes[m_trees[i].treeType].m_bounds;
			m_trees[i].bounds.Center *= m_trees[i].scale;
			m_trees[i].bounds.Radius *= m_trees[i].scale;
			m_trees[i].bounds.Center += m_trees[i].location;
			m_anythingChanged = true;
			return true;
		}
	}
	return false;
}

//=============================================================================
// W3DTreeBuffer::pushAsideTree
//=============================================================================
/** Push sideways tree or grass. */
//=============================================================================
void W3DTreeBuffer::pushAsideTree(DrawableID id, const Coord3D *pusherPos, 
																	const Coord3D *pusherDirection, ObjectID pusherID )
{
	Int i;
	for (i=0; i<m_numTrees; i++) {
		if (m_trees[i].drawableID == id) {
			UnsignedInt lastFrame = m_trees[i].lastFrameUpdated;
			m_trees[i].lastFrameUpdated = TheGameLogic->getFrame();
			if(m_trees[i].pushAsideSource == pusherID) {
				if (m_trees[i].lastFrameUpdated - lastFrame < ConvertRetailFrames(3u))
					return; // already pushing. [5/28/2003]
			}

			if(m_trees[i].pushAside != 0.0f) {
				return; // already pushing. [5/28/2003]
			}
			m_trees[i].pushAsideSource = pusherID;
			Coord3D delta;
			delta.set(m_trees[i].location.X, m_trees[i].location.Y, m_trees[i].location.Z);
			delta.sub(pusherPos);

			if (pusherDirection->x*delta.y - pusherDirection->y*delta.x > 0.0f) {
				m_trees[i].pushAsideCos = -pusherDirection->y;
				m_trees[i].pushAsideSin = pusherDirection->x;
			} else {
				m_trees[i].pushAsideCos = pusherDirection->y;
				m_trees[i].pushAsideSin = -pusherDirection->x;
			}
			m_anyPushChanged = true;
			m_trees[i].pushAsideDelta = 1.0f/(Real)m_treeTypes[m_trees[i].treeType].m_data->m_framesToMoveOutward;
		}
	}
}

//=============================================================================
// W3DTreeBuffer::addShadowTexture
//=============================================================================
/** Finds this texture in the shadow list, adding it if it is new.  The list is what the shadow pass
batches by: every palm on the map is two meshes and two textures, so a hundred of them cost two
draw calls and not two hundred. */
//=============================================================================
Int W3DTreeBuffer::addShadowTexture(TextureClass *tex)
{
	Int i;
	for (i=0; i<m_numShadowTextures; i++) {
		if (m_shadowTextures[i] == tex) {
			return i;
		}
	}
	if (m_numShadowTextures >= MAX_SHADOW_TEXTURES) {
		return -1;
	}
	m_shadowTextures[m_numShadowTextures] = NULL;
	REF_PTR_SET(m_shadowTextures[m_numShadowTextures], tex);
	return m_numShadowTextures++;
}

//=============================================================================
// W3DTreeBuffer::addModelShadow
//=============================================================================
/** Takes over the shadow of a tree the map placed as a real object.  The meshes are held with a
reference and their world transforms copied, because these things never move; the drawable can die
without leaving anything dangling here, and removeModelShadow is what actually forgets it.

Returns FALSE when there is nothing usable, and the caller then leaves the model with the round
decal the template asked for. */
//=============================================================================
Bool W3DTreeBuffer::addModelShadow(DrawableID id, RenderObjClass *robj)
{
	if (!m_initialized || robj == NULL || m_numModelShadows >= MAX_MODEL_SHADOWS) {
		return false;
	}

	TModelShadow &ms = m_modelShadows[m_numModelShadows];
	ms.drawableID = id;
	ms.numMesh = 0;
	ms.visible = false;

	// LOD 0 is the model as it stands closest to the camera, which is the shape worth casting
	Int subCount = 1;
	Bool isHLod = robj->Class_ID() == RenderObjClass::CLASSID_HLOD;
	if (isHLod) {
		subCount = ((HLodClass *)robj)->Get_Lod_Model_Count(0);
	}

	Int i;
	for (i=0; i<subCount && ms.numMesh<4; i++) {
		RenderObjClass *sub = isHLod ? ((HLodClass *)robj)->Peek_Lod_Model(0, i) : robj;
		if (sub == NULL || sub->Class_ID() != RenderObjClass::CLASSID_MESH) {
			continue;
		}
		MeshClass *mesh = (MeshClass *)sub;
		//a skinned mesh is deformed by its bones every frame and its vertex array is not where it
		//stands; nothing that casts one of these shadows is animated, so skip it rather than
		//lay the bind pose on the ground
		if (mesh->Peek_Model() == NULL || mesh->Peek_Model()->Get_Flag(MeshGeometryClass::SKIN)) {
			continue;
		}
		if (mesh->Peek_Model()->Get_Vertex_Count() < 3) {
			continue;
		}

		TextureClass *tex = NULL;
		MaterialInfoClass *matInfo = mesh->Get_Material_Info();
		if (matInfo) {
			if (matInfo->Texture_Count() > 0) {
				tex = matInfo->Peek_Texture(0);
			}
			REF_PTR_RELEASE(matInfo);
		}
		//no texture means no alpha, and the shape of a leafy sheet is entirely in its alpha
		if (tex == NULL) {
			continue;
		}
		Int texNdx = addShadowTexture(tex);
		if (texNdx < 0) {
			continue;
		}

		ms.mesh[ms.numMesh] = NULL;
		REF_PTR_SET(ms.mesh[ms.numMesh], mesh);
		ms.texNdx[ms.numMesh] = texNdx;
		ms.numMesh++;
	}

	if (ms.numMesh == 0) {
		return false;
	}

	//
	// No transform and no position are read here.  This runs while the drawable is being built, and
	// the object is not standing anywhere yet: the render object still reports its object-space
	// bounds around the origin.  The meshes are held with a reference, so where they stand is asked
	// of them every frame instead.
	//
	ms.baseZ = 0.0f;
	ms.visible = false;
	m_numModelShadows++;
	return true;
}

//=============================================================================
// W3DTreeBuffer::removeModelShadow
//=============================================================================
/** Forgets a model shadow.  The list is packed by moving the last entry into the hole, since
nothing here cares what order they are in. */
//=============================================================================
void W3DTreeBuffer::removeModelShadow(DrawableID id)
{
	Int i;
	for (i=0; i<m_numModelShadows; i++) {
		if (m_modelShadows[i].drawableID != id) {
			continue;
		}
		Int k;
		for (k=0; k<m_modelShadows[i].numMesh; k++) {
			REF_PTR_RELEASE(m_modelShadows[i].mesh[k]);
		}
		m_numModelShadows--;
		if (i != m_numModelShadows) {
			m_modelShadows[i] = m_modelShadows[m_numModelShadows];
		}
		m_modelShadows[m_numModelShadows].numMesh = 0;
		return;
	}
}

//=============================================================================
// W3DTreeBuffer::drawModelShadows
//=============================================================================
/** Lays the object trees' triangles on the ground, the same way the batched trees are laid: every
vertex slides along the sun by its own height and drops to the model's base.  These are done on the
CPU rather than in Trees.vso because they are not in the tree vertex buffer and there are few of
them on screen at once - a palm is forty vertices.

Colour comes from the vertex, not the texture, so the ground gets shade and not a picture of a
palm; alpha comes from the texture, so the gaps between the fronds are gaps in the shadow. */
//=============================================================================
Int W3DTreeBuffer::drawModelShadowCasters(RenderInfoClass &sunInfo, const SunShadowCasterCull &cull)
{
	Int drawn = 0;
	for (Int i=0; i<m_numModelShadows; ++i) {
		TModelShadow &model = m_modelShadows[i];
		Bool cast = FALSE;
		for (Int k=0; k<model.numMesh; ++k) {
			MeshClass *mesh = model.mesh[k];
			if (mesh->Is_Not_Hidden_At_All() && !sunInfo.Camera.Cull_Sphere(mesh->Get_Bounding_Sphere())
				&& cull.Visible(mesh->Get_Bounding_Sphere())) {
				mesh->Render(sunInfo);
				cast = TRUE;
			}
		}
		if (cast) ++drawn;
	}
	return drawn;
}

void W3DTreeBuffer::drawModelShadows(CameraClass *camera, Real stretchX, Real stretchY)
{
	if (m_numModelShadows == 0 || m_numShadowTextures == 0) {
		return;
	}

	Int i, t;
	Int numVisible = 0;
	for (i=0; i<m_numModelShadows; i++) {
		TModelShadow &ms = m_modelShadows[i];
		ms.visible = false;
		ms.baseZ = 0.0f;
		Int k;
		for (k=0; k<ms.numMesh; k++) {
			//the meshes are where the object stands now, which is the only place this is known.  The
			//cached box never asks whether the transform moved: a sub-mesh takes its HLod's transform
			//only when something validates it, and a palm placed after a draw had already cached its
			//box at the origin kept that box, and its shadow stayed culled, for the whole match
			ms.mesh[k]->Get_Transform();
			const AABoxClass &box = ms.mesh[k]->Get_Bounding_Box();
			Real bottom = box.Center.Z - box.Extent.Z;
			if (k == 0 || bottom < ms.baseZ) {
				ms.baseZ = bottom;
			}
			if (!ms.visible && (camera == NULL || !camera->Cull_Sphere(ms.mesh[k]->Get_Bounding_Sphere()))) {
				ms.visible = true;
			}
		}
		//and when its shadow can reach the screen, not only its trunk: one standing just past the edge
		//lays its shadow in view, and culling by the trunk took that out all at once
		for (k=0; k<ms.numMesh && !ms.visible; k++) {
			ms.visible = shadowCanReachView(camera->Get_Frustum(), ms.mesh[k]->Get_Bounding_Sphere(), ms.baseZ,
				stretchX, stretchY);
		}
		if (ms.visible) {
			numVisible++;
		}
	}
	if (numVisible == 0) {
		return;
	}

	const UnsignedInt shadowDiffuse = (UnsignedInt)TREE_SHADOW_ALPHA << 24;

	//
	// The world transform is the caller's, the terrain's own, and it is left alone: these vertices
	// are built in world space exactly like the batched trees', which lean on the same thing.
	// Setting an identity here would hand the tree pass below the wrong matrix, since it reads the
	// world transform back out of the device to build its own.
	//
	VertexMaterialClass *vmat = VertexMaterialClass::Get_Preset(VertexMaterialClass::PRELIT_DIFFUSE);
	DX8Wrapper::Set_Material(vmat);
	REF_PTR_RELEASE(vmat);
	DX8Wrapper::Set_Shader(modelShadowShader);
	DX8Wrapper::Set_Texture(1, NULL);

	for (t=0; t<m_numShadowTextures; t++) {
		//two passes over the list: the dynamic buffers have to be asked for an exact size
		Int numVertex = 0;
		Int numIndex = 0;
		for (i=0; i<m_numModelShadows; i++) {
			if (!m_modelShadows[i].visible) {
				continue;
			}
			Int k;
			for (k=0; k<m_modelShadows[i].numMesh; k++) {
				if (m_modelShadows[i].texNdx[k] != t) {
					continue;
				}
				MeshModelClass *model = m_modelShadows[i].mesh[k]->Peek_Model();
				if (numVertex + model->Get_Vertex_Count() > MAX_SHADOW_BATCH_VERTEX) {
					continue;
				}
				numVertex += model->Get_Vertex_Count();
				numIndex += 3*model->Get_Polygon_Count();
			}
		}
		if (numVertex == 0 || numIndex == 0) {
			continue;
		}

		//DynamicVBAccessClass hands out one format and one only, and asserts on it in a debug build;
		//in Release a mismatch is silent and every vertex comes out of the buffer at the wrong stride
		DynamicVBAccessClass vbAccess(BUFFER_TYPE_DYNAMIC_DX8, DX8_FVF_XYZNDUV2, numVertex);
		DynamicIBAccessClass ibAccess(BUFFER_TYPE_DYNAMIC_DX8, numIndex);
		{
			DynamicVBAccessClass::WriteLockClass vbLock(&vbAccess);
			DynamicIBAccessClass::WriteLockClass ibLock(&ibAccess);
			VertexFormatXYZNDUV2 *vb = vbLock.Get_Formatted_Vertex_Array();
			UnsignedShort *ib = ibLock.Get_Index_Array();
			if (vb == NULL || ib == NULL) {
				continue;
			}

			Int curVertex = 0;
			Int curIndex = 0;
			for (i=0; i<m_numModelShadows; i++) {
				if (!m_modelShadows[i].visible) {
					continue;
				}
				Int k;
				for (k=0; k<m_modelShadows[i].numMesh; k++) {
					if (m_modelShadows[i].texNdx[k] != t) {
						continue;
					}
					MeshModelClass *model = m_modelShadows[i].mesh[k]->Peek_Model();
					Int meshVerts = model->Get_Vertex_Count();
					Int meshPolys = model->Get_Polygon_Count();
					if (curVertex + meshVerts > numVertex || curIndex + 3*meshPolys > numIndex) {
						continue;
					}
					const Vector3 *pVert = model->Get_Vertex_Array();
					const Vector2 *uvs = model->Get_UV_Array_By_Index(0);
					const TriIndex *pPoly = model->Get_Polygon_Array();
					if (pVert == NULL || uvs == NULL || pPoly == NULL) {
						continue;
					}
					const Matrix3D &xform = m_modelShadows[i].mesh[k]->Get_Transform();
					Real baseZ = m_modelShadows[i].baseZ;

					Int v;
					Int startVertex = curVertex;
					for (v=0; v<meshVerts; v++) {
						Vector3 world;
						Matrix3D::Transform_Vector(xform, pVert[v], &world);
						Real height = world.Z - baseZ;
						if (height < 0.0f) {
							height = 0.0f;
						}
						Real landX = world.X + height*stretchX;
						Real landY = world.Y + height*stretchY;
						//
						// The batched trees lie on a flat plane at the tree's own base, and on a
						// slope half of that plane ends up under the ground.  Here the vertices are
						// being built one at a time on the CPU anyway, so each one can ask the
						// terrain how high it is where it lands, and the shadow follows the hill.
						//
						Real groundZ = TheTerrainRenderObject != NULL
														? TheTerrainRenderObject->getHeightMapHeight(landX, landY, NULL)
														: baseZ;
						vb[curVertex].x = landX;
						vb[curVertex].y = landY;
						vb[curVertex].z = groundZ + TREE_SHADOW_LIFT;
						vb[curVertex].nx = 0.0f;
						vb[curVertex].ny = 0.0f;
						vb[curVertex].nz = 1.0f;
						vb[curVertex].diffuse = shadowDiffuse;
						vb[curVertex].u1 = uvs[v].U;
						vb[curVertex].v1 = uvs[v].V;
						vb[curVertex].u2 = 0.0f;
						vb[curVertex].v2 = 0.0f;
						curVertex++;
					}
					for (v=0; v<meshPolys; v++) {
						ib[curIndex++] = startVertex + pPoly[v].I;
						ib[curIndex++] = startVertex + pPoly[v].J;
						ib[curIndex++] = startVertex + pPoly[v].K;
					}
				}
			}
			numVertex = curVertex;
			numIndex = curIndex;
		}
		if (numIndex == 0) {
			continue;
		}

		DX8Wrapper::Set_Texture(0, m_shadowTextures[t]);
		DX8Wrapper::Set_Index_Buffer(ibAccess, 0);
		DX8Wrapper::Set_Vertex_Buffer(vbAccess);
		DX8Wrapper::Draw_Triangles(0, numIndex/3, 0, numVertex);
	}

	DX8Wrapper::Invalidate_Cached_Render_States();
}

//=============================================================================
// W3DTreeBuffer::drawTreeBuffers
//=============================================================================
/** Draws every buffer that has anything in it, either as the trees themselves or, with the vertex
shader's constants set to flatten them, as their shadows.  The vertex and index buffers are the
same ones both times - a tree's shadow is that tree's triangles. */
//=============================================================================
void W3DTreeBuffer::drawTreeBuffers(Bool shadowPass, Bool sunPass)
{
	LPDIRECT3DDEVICE9 dev = DX8Wrapper::_Get_D3D_Device();
	if (!dev) {
		return;
	}
	Int bNdx;
	for (bNdx=0;bNdx<MAX_BUFFERS; bNdx++) {
		if (m_curNumTreeIndices[bNdx]==0) {
			break;
		}
		DX8Wrapper::Set_Index_Buffer(m_indexTree[bNdx],0);
		DX8Wrapper::Set_Vertex_Buffer(m_vertexTree[bNdx]);
		// Render the waving grass
		DX8Wrapper::Apply_Render_State_Changes();
		if (m_dwTreeVertexShader) {
			// D3D8 carried the vertex declaration beside the shader and SetVertexShader bound
			// both.  D3D9 split them, and a shader whose dcl_ inputs have no matching element in
			// whatever declaration is currently bound reads zero out of them.  The FVF the vertex
			// buffer sets has no blend weight and no blend indices, which is where Trees.vso keeps
			// the tree's base height, its sway type and its lit colour: without this the trees draw
			// unswayed and black, their shadows draw unflattened on top of them, and the buffer,
			// the draw call and the matrix all look right while it happens.
			dev->SetVertexDeclaration(m_treeVertexDeclaration);
			DX8Wrapper::Set_Vertex_Shader(m_dwTreeVertexShader);
			DX8Wrapper::Set_DX8_Texture_Stage_State(0,  D3DTSS_TEXCOORDINDEX, 0);
			DX8Wrapper::Set_DX8_Texture_Stage_State(1,  D3DTSS_TEXCOORDINDEX, 1);
			DX8Wrapper::Set_DX8_Texture_Stage_State(1,  D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
		}
		if (shadowPass) {
			//
			// A shadow has no colour of its own: the texture says where a leaf is, the texture
			// factor says how dark to paint the ground there.  Taking the colour from the vertices
			// instead would paint the tree's own lit greens flat on the grass.
			//
			// These go in behind DX8Wrapper's back, the way the shroud stage above does, so the
			// caller ends drawing with Invalidate_Cached_Render_States.
			//
			DX8Wrapper::Set_DX8_Render_State(D3DRS_TEXTUREFACTOR, (D3DCOLOR)(TREE_SHADOW_ALPHA<<24));
			//
			// ShaderClass sets the alpha test to 0x60 for the tree pass, where alpha comes straight
			// off the texture.  Here it is that alpha scaled down to the shadow's own opacity, so
			// every pixel in the crown would fail the same test and the shadow would be nothing at
			// all.  Test near zero instead: what is being rejected is the gaps between leaves.
			//
			DX8Wrapper::Set_DX8_Render_State(D3DRS_ALPHAREF, TREE_SHADOW_ALPHAREF);
			DX8Wrapper::Set_DX8_Texture_Stage_State(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
			DX8Wrapper::Set_DX8_Texture_Stage_State(0, D3DTSS_COLORARG1, D3DTA_TFACTOR);
			DX8Wrapper::Set_DX8_Texture_Stage_State(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
			DX8Wrapper::Set_DX8_Texture_Stage_State(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
			DX8Wrapper::Set_DX8_Texture_Stage_State(0, D3DTSS_ALPHAARG2, D3DTA_TFACTOR);
			DX8Wrapper::Set_DX8_Texture_Stage_State(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
			DX8Wrapper::Set_DX8_Texture_Stage_State(1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
		}
		if (sunPass) {
			for (const ShadowRange &range : m_shadowRanges[bNdx])
				DX8Wrapper::Draw_Triangles(range.first, range.count/3, 0, m_curNumTreeVertices[bNdx]);
		} else {
			DX8Wrapper::Draw_Triangles(0, m_curNumTreeIndices[bNdx]/3, 0, m_curNumTreeVertices[bNdx]);
		}
	}
	if (shadowPass) {
		DX8Wrapper::Invalidate_Cached_Render_States();
	}
}

DECLARE_PERF_TIMER(Tree_Render)

//=============================================================================
// W3DTreeBuffer::drawTrees
//=============================================================================
/** Draws the trees.  Uses camera to cull. */
//=============================================================================
//-------------------------------------------------------------------------------------------------
/** Update pushed aside and toppling trees, once a logic frame like the sway: the push and sink
	counts are logic frames, and the topple's speeds were converted to them.  Run every render pass,
	they went as much faster as the picture rate was over the logic's.  Called at the render's sync
	point rather than from drawTrees: a toppling tree asks the partition whether the drawn player sees
	its cell, and its bounce plays an effect (LOGIC-THREAD.md phase 3). */
//-------------------------------------------------------------------------------------------------
void W3DTreeBuffer::stepTrees( void )
{
	Bool pause = TheScriptEngine->isTimeFrozenScript() || TheScriptEngine->isTimeFrozenDebug();
	if (TheGameLogic && TheGameLogic->isGamePaused()) {
		pause = true;
	}
	const UnsignedInt now = TheGameClient ? TheGameClient->getFrame() : 0;
	if (pause || now == m_lastTreeStepFrame) {
		return;
	}
	m_lastTreeStepFrame = now;

	for (Int curTree=0; curTree<m_numTrees; curTree++) {
		Int type = m_trees[curTree].treeType;
		if (type<0) { // deleted.
			continue;
		}
		// A tree whose vertices move this logic frame is marked, and only such a tree: one lying still
		// on the ground, or held up in the fog, rewrites nothing.
		Bool moved = false;
		if(m_trees[curTree].m_toppleState == TOPPLE_FALLING ||
			 m_trees[curTree].m_toppleState == TOPPLE_FOGGED) {
			const W3DToppleState before = m_trees[curTree].m_toppleState;
			updateTopplingTree(m_trees+curTree);
			moved = before != TOPPLE_FOGGED || m_trees[curTree].m_toppleState != TOPPLE_FOGGED;
		} else if(m_trees[curTree].m_toppleState == TOPPLE_DOWN) {
			if (m_treeTypes[type].m_data->m_killWhenToppled) {
				moved = true;
				if (m_trees[curTree].m_sinkFramesLeft==0) {
					m_trees[curTree].treeType = DELETED_TREE_TYPE; // delete it. [7/11/2003]
					m_anythingChanged = true; // need to regenerate trees. [7/11/2003]
				}
				m_trees[curTree].m_sinkFramesLeft--;
				m_trees[curTree].location.Z -= m_treeTypes[type].m_data->m_sinkDistance/m_treeTypes[type].m_data->m_sinkFrames;
				m_trees[curTree].m_mtx.Set_Translation(m_trees[curTree].location);
			}
		} else if (m_trees[curTree].pushAsideDelta!=0.0f) {
			moved = true;
			m_trees[curTree].pushAside += m_trees[curTree].pushAsideDelta;
			if (m_trees[curTree].pushAside>=1.0f) {
				m_trees[curTree].pushAsideDelta = -1.0/(Real)m_treeTypes[type].m_data->m_framesToMoveInward;
			} else if (m_trees[curTree].pushAside<=0.0f) {
				m_trees[curTree].pushAsideDelta = 0.0f;
				m_trees[curTree].pushAside = 0.0f;
			}
		}
		if (moved) {
			if (!m_trees[curTree].moved) {
				m_trees[curTree].moved = true;
				m_movedTrees.push_back(curTree);
			}
			m_anyPushChanged = true;
			m_movedSinceCull = true;
		}
	}
}

void W3DTreeBuffer::drawTrees(CameraClass * camera, RefRenderObjListIterator *pDynamicLightsIterator,
	const CameraClass *sunCamera)
{
	// While the sun's map holds the frame the trees are in it as their own shape, so the silhouettes,
	// the blob decals and the palms' flattened copies would shadow the ground a second time.
	const Bool mapped = SunShadow::OwnsSceneShadows(DX8Wrapper::_Get_D3D_Device());
	SunShadow::ReceiverExclusion sunReceivers(DX8Wrapper::_Get_D3D_Device(), !mapped);
	USE_PERF_TIMER(Tree_Render)
	if (!m_isTerrainPass && !sunCamera) {
		return;
	}

	// if breeze changes, always process the full update, even if not visible, 
	// so that things offscreen won't 'pop' when first viewed
	const BreezeInfo& info = weatherBreezeInfo();
	Bool pause = TheScriptEngine->isTimeFrozenScript() || TheScriptEngine->isTimeFrozenDebug();
	if (TheGameLogic && TheGameLogic->isGamePaused()) {
		pause = true;
	}
	Int i;
	if (!pause) {
		if (info.m_breezeVersion != m_curSwayVersion) 
		{
			updateSway(info);
		}		
	}

	//
	// m_curSwayStep is a per-LOGIC-frame increment, but drawTrees runs once per RENDER frame, so
	// with rendering uncapped the trees swayed at (fps/30)x speed. Advance the phase only when
	// the logic frame changed; the interpolation below still runs every render frame.
	//
	UnsignedInt swayNow = TheGameClient ? TheGameClient->getFrame() : 0;
	Bool stepSway = !pause && (swayNow != m_lastSwayFrame);
	if (stepSway) {
		m_lastSwayFrame = swayNow;
	}

	Vector3 swayFactor[MAX_SWAY_TYPES];
	for (i=0; i<MAX_SWAY_TYPES; i++) {
		if (stepSway) {
			m_curSwayOffset[i] += m_curSwayStep[i];
			if (m_curSwayOffset[i] > NUM_SWAY_ENTRIES-1) {
				m_curSwayOffset[i] -= NUM_SWAY_ENTRIES-1;
			}
		}
		Int minOffset = REAL_TO_INT_FLOOR(m_curSwayOffset[i]);
		if (minOffset>=0 && minOffset+1<NUM_SWAY_ENTRIES) {
			Real f2 = m_curSwayOffset[i] - minOffset;
			Real f1 = 1.0f - f2;
			swayFactor[i] = f1*m_swayOffsets[minOffset] + f2*m_swayOffsets[minOffset+1];
			swayFactor[i] *= m_curSwayFactor[i];
		}
	}
	
	if (!sunCamera) m_isTerrainPass = false;

	if (m_needToUpdateTexture) {
		m_needToUpdateTexture = false;
		updateTexture();
	}
	if (m_treeTexture==NULL) {
		return;
	}
	// The visible flags and the buffers built from them are shared by every pass.  A water mirror
	// culling them against its own frustum left the screen drawing the mirror's trees until the
	// next cull, and with the mirror skipping frames the trees flickered between the two sets.  The
	// mirror draws what the screen culled, which is what stands near the water it lands on.
	// A tree that came or went since the last cull drops the sun pass's key, whichever pass sees the
	// change first.  A moved one only matters to a cull asked to redo the sort keys: a sinking tree's
	// key follows its height.
	if (m_anythingChanged || (m_movedSinceCull && m_updateAllKeys)) {
		m_sunCullValid = FALSE;
	}
	if (sunCamera || (m_updateAllKeys && !ShaderClass::Is_Backface_Culling_Inverted())) {
		cull(camera, sunCamera);
	}

	Int curTree;

	// Shadows are wanted under either option: the loop used to be gated on m_useShadowDecals alone,
	// so with volume shadows picked trees were the one thing in the scene standing on nothing.
	Bool drawShadows = !sunCamera && !mapped && (TheGlobalData->m_useShadowDecals || TheGlobalData->m_useShadowVolumes);
	Bool silhouetteShadows = drawShadows && treeSilhouetteShadows(m_dwTreeVertexShader);

	// Draw tree shadows.
	// Trees are batched vertices with no render object of their own, so the one shared blob decal
	// is all they can have.
	if (m_shadow && TheW3DProjectedShadowManager && drawShadows && !silhouetteShadows) {
		for (curTree=0; curTree<m_numTrees; curTree++) {
			Int type = m_trees[curTree].treeType;
			if (type<0) { // deleted.
				continue;
			}
			//DoShadow is the tree type's own say, and 27 of the game's 128 tree types never name it at
			//all - the module data defaults it to false, so those stood on clean ground.  With
			//ShadowsForProps on, every tree gets the blob and the flag only decides it when that is off.
			if (!m_trees[curTree].visible ||
				(!m_treeTypes[type].m_doShadow && !TheGlobalData->m_shadowsForProps)) {
				continue;
			}
			Real factor = 1.0f;
			if (m_trees[curTree].m_toppleState == TOPPLE_FALLING ||
					m_trees[curTree].m_toppleState == TOPPLE_DOWN) {
				continue;
			}
			m_shadow->setSize(m_treeTypes[type].m_shadowSize, -m_treeTypes[type].m_shadowSize*factor);
			m_shadow->setPosition(m_trees[curTree].location.X, m_trees[curTree].location.Y, m_trees[curTree].location.Z);
			TheW3DProjectedShadowManager->queueDecal(m_shadow);
		}
		TheW3DProjectedShadowManager->flushDecals(m_shadow->getTexture(0), SHADOW_DECAL);
	}

	// the pushed aside and toppling trees moved in stepTrees, at the sync point

	// ShadowsForProps decides which trees get a range in the sun pass, so a change rebuilds them.
	if (m_lastShadowsForProps != TheGlobalData->m_shadowsForProps) {
		m_lastShadowsForProps = TheGlobalData->m_shadowsForProps;
		m_anythingChanged = true;
	}
	if (m_anythingChanged) {
		loadTreesInVertexAndIndexBuffers(pDynamicLightsIterator);
		m_anythingChanged = false;
	} else if (m_anyPushChanged) {
		m_anyPushChanged = false;
		updateVertexBuffer();
	}

//#define DEBUG_TEXTURE 1
#ifdef DEBUG_TEXTURE // Draw the combined texture for debugging. jba. [4/21/2003]
	// Setup the vertex buffer, shader & texture.
	DX8Wrapper::Set_Shader(detailAlphaShader);
	DX8Wrapper::Set_Texture(0,m_treeTexture);
	DynamicIBAccessClass ib_access(BUFFER_TYPE_DYNAMIC_DX8, 6);
	//draw an infinite sky plane
	DynamicVBAccessClass vb_access(BUFFER_TYPE_DYNAMIC_DX8, DX8_FVF_XYZNDUV2, 4);
	{
		DynamicIBAccessClass::WriteLockClass ibLock(&ib_access);
		UnsignedShort *ndx = ibLock.Get_Index_Array();

		if (ndx) {
			ndx[0] = 0;
			ndx[1] = 1;
			ndx[2] = 2;
			ndx[3] = 1;
			ndx[4] = 3;
			ndx[5] = 2;
		}
		DynamicVBAccessClass::WriteLockClass lock(&vb_access);
		VertexFormatXYZNDUV2* verts=lock.Get_Formatted_Vertex_Array();
		if(verts)
		{
			Real width = 300;
			Real origin = 40;
			verts[0].x=origin;
			verts[0].y=origin;
			verts[0].z=15;
			verts[0].u1=0;
			verts[0].v1=0;
			verts[0].diffuse=0xffffffff;
	
			verts[1].x=origin+width;
			verts[1].y=origin;
			verts[1].z=15;
			verts[1].u1=1;
			verts[1].v1=0;
			verts[1].diffuse=0xffffffff;
	
			verts[2].x=origin;
			verts[2].y=origin+width;
			verts[2].z=15;
			verts[2].u1=0;
			verts[2].v1=1;
			verts[2].diffuse=0xffffffff;
	
			verts[3].x=origin+width;
			verts[3].y=origin+width;
			verts[3].z=15;
			verts[3].u1=1;
			verts[3].v1=1;
			verts[3].diffuse=0xffffffff;
		}
	}

	DX8Wrapper::Set_Index_Buffer(ib_access,0);
	DX8Wrapper::Set_Vertex_Buffer(vb_access);

	Matrix3D tm(1);
	DX8Wrapper::Set_Transform(D3DTS_WORLD,tm);

	DX8Wrapper::Draw_Triangles(	0,2, 0,	4);	//draw a quad, 2 triangles, 4 verts
#endif


	Real stretchX, stretchY;
	Bool haveSun = treeShadowStretch(stretchX, stretchY);

	//
	// The trees the map placed as objects are not in this buffer, so they are drawn whether it has
	// anything in it or not - a map can have them with no batched tree in sight at all.  This sits
	// on both sides of the early return rather than before it, and after the tree pass rather than
	// before, so that nothing it leaves on the device can reach the trees: it is its own pass and
	// it ends with Invalidate_Cached_Render_States.
	//
	if (m_curNumTreeIndices[0] == 0) {
		if (drawShadows && haveSun) {
			drawModelShadows(camera, stretchX, stretchY);
		}
		return;
	}

	D3DXMATRIX mat;
	if (m_dwTreeVertexShader) {
		//
		// This has to be applied first, and the trees disappear off the side of the world without it.
		// _Get_DX8_Transform below asks the device for the matrix, while DX8Wrapper::Set_Transform -
		// which is how the caller handed us the terrain's world matrix - only marks its own copy
		// dirty and sends nothing.  Read before the pending state is flushed and what comes back is
		// whatever the last thing drawn happened to leave on the device, which is a different wrong
		// matrix depending on where the camera is standing.  EA read these after two applies for
		// exactly this reason; this is that, made deliberate.
		//
		DX8Wrapper::Apply_Render_State_Changes();
		D3DXMATRIX matProj, matView, matWorld;
		DX8Wrapper::_Get_DX8_Transform(D3DTS_WORLD, *(Matrix4x4*)&matWorld);
		DX8Wrapper::_Get_DX8_Transform(D3DTS_VIEW, *(Matrix4x4*)&matView);
		DX8Wrapper::_Get_DX8_Transform(D3DTS_PROJECTION, *(Matrix4x4*)&matProj);
		D3DXMatrixMultiply( &mat, &matView, &matProj );
		D3DXMatrixMultiply( &mat, &matWorld, &mat );
	}

	//
	// The tree's own shadow, cast by the tree's own triangles.
	//
	// Trees.nvv already computes h = vertex Z minus the tree's base Z (the base rides in the normal
	// slot as v1.z) and then writes position = h * c[8+swayType] + vertex.  That is a shear about
	// the base, which is exactly what a shadow projected onto a flat ground plane is.  Hand it
	// (sunX, sunY, -1, 0) instead of the breeze and the same instruction lays the tree down: xy
	// slides along the ground by the sun's angle, and z collapses to the base height.  No second
	// shader, which matters here - the .vso files come out of the shipped big files and this fork
	// has no assembler to build a new one.
	//
	// The shape is the tree's own texture alpha, so leaf gaps are holes in the shadow, and the
	// breeze is added on top of the sun so the shadow sways with the tree that casts it.  Toppling
	// and push-aside are already baked into the vertices, so a falling tree's shadow falls with it.
	//
	// What this does not do is follow the terrain: the shadow is flat at the tree's own base
	// height, so on a slope part of it sinks under the ground and is clipped by the depth test.
	//
	if (silhouetteShadows && haveSun) {
		// v * translate(0,0,lift) * worldViewProj, by hand: with row vectors the product's
		// translation row is the lift times row 3, added to row 4.
		D3DXMATRIX shadowMat = mat;
		shadowMat._41 += TREE_SHADOW_LIFT * shadowMat._31;
		shadowMat._42 += TREE_SHADOW_LIFT * shadowMat._32;
		shadowMat._43 += TREE_SHADOW_LIFT * shadowMat._33;
		shadowMat._44 += TREE_SHADOW_LIFT * shadowMat._34;
		D3DXMatrixTranspose( &shadowMat, &shadowMat );
		DX8Wrapper::Set_Vertex_Shader_Constant(  4, &shadowMat,  4 );

		// c8 is the entry a tree with no sway reads, c9 and up are the sway types.
		Vector4 flat(stretchX, stretchY, -1.0f, 0);
		DX8Wrapper::Set_Vertex_Shader_Constant(  8, &flat,  1 );
		for	(i=0; i<MAX_SWAY_TYPES; i++) {
			Vector4 flatSway(stretchX + swayFactor[i].X, stretchY + swayFactor[i].Y, -1.0f, 0);
			DX8Wrapper::Set_Vertex_Shader_Constant(  9+i, &flatSway,  1 );
		}

		DX8Wrapper::Set_Shader(treeShadowShader);
		DX8Wrapper::Set_Texture(0,m_treeTexture);
		DX8Wrapper::Set_Texture(1,NULL);
		DX8Wrapper::Set_Vertex_Shader(m_dwTreeVertexShader);
		DX8Wrapper::Apply_Render_State_Changes();
		drawTreeBuffers(true);
	}

	DX8Wrapper::Set_Shader(detailAlphaShader);

	DX8Wrapper::Set_Texture(0,m_treeTexture);
	DX8Wrapper::Set_Texture(1,NULL);
	DX8Wrapper::Set_DX8_Texture_Stage_State(0,  D3DTSS_TEXCOORDINDEX, 0);
	DX8Wrapper::Set_DX8_Texture_Stage_State(1,  D3DTSS_TEXCOORDINDEX, 1);
	// Draw all the trees.
	DX8Wrapper::Apply_Render_State_Changes();
	if (!sunCamera) W3DShaderManager::setShroudTex(1);
	DX8Wrapper::Apply_Render_State_Changes();
 
	if (m_dwTreeVertexShader) {
		D3DXMatrixTranspose( &mat, &mat );

		// c4  - Composite World-View-Projection Matrix
		DX8Wrapper::Set_Vertex_Shader_Constant(  4, &mat,  4 );

		//
		// The matrix the trees are about to be drawn with, checked against the one the device is
		// actually holding at this moment.  This is not a debugging leftover: reading a transform
		// out of the device before the wrapper has flushed its pending state hands back whatever
		// the last thing drawn left there, the trees go off the side of the world, and nothing about
		// the tree buffer looks wrong while it happens - the vertices are all present and the draw
		// call is made.  Two GetTransforms a frame is a cheap price for never hunting that again.
		//
		{
			D3DXMATRIX nowWorld, nowView, nowProj, nowWvp;
			DX8Wrapper::_Get_DX8_Transform(D3DTS_WORLD, *(Matrix4x4*)&nowWorld);
			DX8Wrapper::_Get_DX8_Transform(D3DTS_VIEW, *(Matrix4x4*)&nowView);
			DX8Wrapper::_Get_DX8_Transform(D3DTS_PROJECTION, *(Matrix4x4*)&nowProj);
			D3DXMatrixMultiply( &nowWvp, &nowView, &nowProj );
			D3DXMatrixMultiply( &nowWvp, &nowWorld, &nowWvp );
			D3DXMatrixTranspose( &nowWvp, &nowWvp );
			Real worst = 0.0f;
			const Real *a = (const Real *)&mat;
			const Real *b = (const Real *)&nowWvp;
			for (Int e=0; e<16; e++) {
				Real d = a[e] - b[e];
				if (d < 0.0f) d = -d;
				if (d > worst) worst = d;
			}
			if (worst > 0.001f) {
				DEBUG_LOG(("TREEMATRIX the trees are being drawn with the wrong matrix, worst term off by %f\n",
					worst));
			}
		}
		Vector4 noSway(0,0,0,0);
		DX8Wrapper::Set_Vertex_Shader_Constant(  8, &noSway,  1 );

		// c8 - c8+MAX_SWAY_TYPES - the sway amount.
		for	(i=0; i<MAX_SWAY_TYPES; i++) {
			Vector4 sway4(swayFactor[i].X, swayFactor[i].Y, swayFactor[i].Z, 0);
			DX8Wrapper::Set_Vertex_Shader_Constant(  9+i, &sway4,  1 );
		}

		W3DShroud *shroud;
		if ((shroud=TheTerrainRenderObject->getShroud()) != 0) {
			// Setup shroud texture info [6/6/2003]
			float xoffset = 0;
			float yoffset = 0;
			Real width=shroud->getCellWidth();
			Real height=shroud->getCellHeight();

			xoffset = -(float)shroud->getDrawOriginX() + width;
			yoffset = -(float)shroud->getDrawOriginY() + height;
			Vector4 offset(xoffset, yoffset, 0, 0);
			DX8Wrapper::Set_Vertex_Shader_Constant(  32, &offset,  1 );
			width = 1.0f/(width*shroud->getTextureWidth());
			height = 1.0f/(height*shroud->getTextureHeight());
			offset.Set(width, height, 1, 1);
			DX8Wrapper::Set_Vertex_Shader_Constant(  33, &offset,  1 );

		} else {
			Vector4 offset(0,0,0,0);
			DX8Wrapper::Set_Vertex_Shader_Constant(  32, &offset,  1 );
			DX8Wrapper::Set_Vertex_Shader_Constant(  33, &offset,  1 );
		}

		DX8Wrapper::Set_Vertex_Shader(m_dwTreeVertexShader);
#if 0
		DX8Wrapper::Set_Pixel_Shader(m_dwTreePixelShader);
		// a.c. 6/16 - allow switching between normal and 2X mode for terrain
		Real mulTwoX = 0.5f;
		if(TheGlobalData && TheGlobalData->m_useOverbright)
			mulTwoX = 1.0f;	
		DX8Wrapper::_Get_D3D_Device()->SetPixelShaderConstant(1, D3DXVECTOR4(mulTwoX, mulTwoX, mulTwoX, mulTwoX), 1);
#endif

	} else {
		DX8Wrapper::Set_Vertex_Format(DX8_FVF_XYZNDUV1);
	}


	if (sunCamera) {
		// Leaves are thin cards. Capture both faces with the same alpha cutoff as
		// the visible tree, and never stamp the shroud into their shadow shape.
		DX8Wrapper::Set_DX8_Render_State(D3DRS_ALPHABLENDENABLE, FALSE);
		DX8Wrapper::Set_DX8_Render_State(D3DRS_CULLMODE, D3DCULL_NONE);
	}
	drawTreeBuffers(false, sunCamera != NULL);

	DX8Wrapper::Set_Vertex_Format(DX8_FVF_XYZNDUV1);
	DX8Wrapper::Set_Pixel_Shader(NULL);
	DX8Wrapper::Invalidate_Cached_Render_States();	//code above mucks around with W3D states so make sure we reset

	//the object trees' shadows, last, after the trees themselves have been drawn and the states reset
	if (drawShadows && haveSun) {
		drawModelShadows(camera, stretchX, stretchY);
	}
}

//-------------------------------------------------------------------------------------------------
///< Start the toppling process by giving a force vector
//-------------------------------------------------------------------------------------------------
void W3DTreeBuffer::applyTopplingForce( TTree *tree, const Coord3D* toppleDirection, Real toppleSpeed,
																			 UnsignedInt options )
{
	if (tree->m_toppleState != TOPPLE_UPRIGHT) {
		return;
	}
	const W3DTreeDrawModuleData* d = m_treeTypes[tree->treeType].m_data;
  // Having a low toppleSpeed is BAD. In particular, if the toppleSpeed is exactly 0, the
  // tree will stay upright forever, frozen in place (because the sway update is dead) 
  // but never dying
  // MinimumToppleSpeed is per retail frame and the tree steps once a logic frame
  const Real minimumToppleSpeed = ConvertRetailPerFrameRate( d->m_minimumToppleSpeed );
  if ( toppleSpeed < minimumToppleSpeed )
  {
    toppleSpeed = minimumToppleSpeed;
  }

	tree->m_toppleDirection = *toppleDirection;
	tree->m_toppleDirection.normalize();
	tree->m_angularAccumulation = 0;

	// toppleSpeed is per logic frame; the acceleration taken from it is per frame squared, so it
	// halves once more, as in ToppleUpdate
	tree->m_angularVelocity = toppleSpeed * d->m_initialVelocityPercent;
	tree->m_angularAcceleration = ConvertRetailPerFrameRate( toppleSpeed * d->m_initialAccelPercent );
	tree->m_toppleState = TOPPLE_FALLING;
	tree->m_options = options;
	Coord3D pos;
	pos.set(tree->location.X, tree->location.Y, tree->location.Z);
	FXList::doFXPos(d->m_toppleFX, &pos);
	m_anyPushChanged = true;
	tree->m_mtx.Make_Identity();
	tree->m_mtx.Set_Translation(tree->location);

}

// this is our "bounce" limit -- slightly less that 90 degrees, to account for slop.
static const Real ANGULAR_LIMIT = PI/2 - PI/64;		

//-------------------------------------------------------------------------------------------------
///< Keep track of rotational fall distance, bounce and/or stop when needed.
//-------------------------------------------------------------------------------------------------
void W3DTreeBuffer::updateTopplingTree(TTree *tree)
{
	//DLOG(Debug::Format("updating W3DTreeBuffer %08lx\n",this));
	DEBUG_ASSERTCRASH(tree->m_toppleState != TOPPLE_UPRIGHT, ("hmm, we should be sleeping here"));
	if ( (tree->m_toppleState == TOPPLE_UPRIGHT)  ||  (tree->m_toppleState == TOPPLE_DOWN) )
		return;

	const W3DTreeDrawModuleData* d = m_treeTypes[tree->treeType].m_data;
	Int localPlayerIndex = ThePlayerList ? TheObserverCamera.getShroudPlayerIndex() : 0;
	Coord3D pos;
	pos.set(tree->location.X, tree->location.Y, tree->location.Z);
	ObjectShroudStatus ss = ThePartitionManager->getPropShroudStatusForPlayer(localPlayerIndex, &pos);
	if (ss==OBJECTSHROUD_FOGGED) {
		// Don't update fogged trees. [8/11/2003]
		tree->m_toppleState = TOPPLE_FOGGED;
		return;
	} else if (tree->m_toppleState == TOPPLE_FOGGED) {
		// was fogged, now isn't.  
		tree->m_angularVelocity = 0;
		tree->m_toppleState = TOPPLE_DOWN;
		tree->m_mtx.In_Place_Pre_Rotate_X(-ANGULAR_LIMIT * tree->m_toppleDirection.y);
		tree->m_mtx.In_Place_Pre_Rotate_Y(ANGULAR_LIMIT * tree->m_toppleDirection.x);
		if (d->m_killWhenToppled) {
			// If got killed in the fog, just remove. jba [8/11/2003]
			tree->m_sinkFramesLeft = 0;
		}
		return;
	}
	const Real VELOCITY_BOUNCE_LIMIT = ConvertRetailPerFrameRate(0.01f);				// if the velocity after a bounce will be this or lower, just stop at zero
	const Real VELOCITY_BOUNCE_SOUND_LIMIT = ConvertRetailPerFrameRate(0.03f);	// and if this low, then skip the bounce sound

	Real curVelToUse = tree->m_angularVelocity;
	if (tree->m_angularAccumulation + curVelToUse > ANGULAR_LIMIT)
		curVelToUse = ANGULAR_LIMIT - tree->m_angularAccumulation;

	tree->m_mtx.In_Place_Pre_Rotate_X(-curVelToUse * tree->m_toppleDirection.y);
	tree->m_mtx.In_Place_Pre_Rotate_Y(curVelToUse * tree->m_toppleDirection.x);

	
	tree->m_angularAccumulation += curVelToUse;
	if ((tree->m_angularAccumulation >= ANGULAR_LIMIT) && (tree->m_angularVelocity > 0))
	{
		// Hit so either bounce or stop if too little remaining velocity.
		tree->m_angularVelocity *= -d->m_bounceVelocityPercent;

		if( BitTest( tree->m_options, W3D_TOPPLE_OPTIONS_NO_BOUNCE ) == TRUE || 
				fabs(tree->m_angularVelocity) < VELOCITY_BOUNCE_LIMIT )
		{
			// too slow, just stop
			tree->m_angularVelocity = 0;
			tree->m_toppleState = TOPPLE_DOWN;
			if (d->m_killWhenToppled) {
				tree->m_sinkFramesLeft = d->m_sinkFrames;
			}
		}
		else if( fabs(tree->m_angularVelocity) >= VELOCITY_BOUNCE_SOUND_LIMIT )
		{
			// fast enough bounce to warrant the bounce fx
			if( BitTest( tree->m_options, W3D_TOPPLE_OPTIONS_NO_FX ) == FALSE ) {
				Vector3 loc(0, 0, 3*TREE_RADIUS_APPROX); // Kinda towards the top of the tree. jba. [7/11/2003]
				Vector3 xloc;
				tree->m_mtx.Transform_Vector(tree->m_mtx, loc, &xloc);
				Coord3D pos;
				pos.set(xloc.X, xloc.Y, xloc.Z);
				FXList::doFXPos(d->m_bounceFX, &pos);
			}
		}
	}
	else
	{
		tree->m_angularVelocity += tree->m_angularAcceleration;
	}

}

// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void W3DTreeBuffer::crc( Xfer *xfer )
{
	// empty. jba [8/11/2003]	
}  // end CRC

// ------------------------------------------------------------------------------------------------
/** Xfer
	* Version Info:
	* 1: Initial version */
// ------------------------------------------------------------------------------------------------
void W3DTreeBuffer::xfer( Xfer *xfer )
{

	// version
	XferVersion currentVersion = 1;
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );

	Int i;
	Int numTrees = m_numTrees;
	xfer->xferInt(&numTrees);
	if (xfer->getXferMode() == XFER_LOAD)	{	
		m_numTrees = 0;
		for (i=0; i<PARTITION_WIDTH_HEIGHT*PARTITION_WIDTH_HEIGHT; i++) {
			m_areaPartition[i] = END_OF_PARTITION;
		}
	}

	// Save trees. [8/11/2003]
	for	(i=0; i<numTrees; i++) {
		TTree tree;
		memset(&tree, 0, sizeof(tree));
		AsciiString modelName;
		AsciiString modelTexture;
		Int treeType = DELETED_TREE_TYPE;
		if (xfer->getXferMode() != XFER_LOAD) {
			tree = m_trees[i];
			treeType = m_trees[i].treeType;
			if (treeType != DELETED_TREE_TYPE) {
				modelName = m_treeTypes[treeType].m_data->m_modelName;
				modelTexture = m_treeTypes[treeType].m_data->m_textureName;
			}
		}
		xfer->xferAsciiString(&modelName);
		xfer->xferAsciiString(&modelTexture);
		if (xfer->getXferMode() == XFER_LOAD) {
			Int j;
			for (j=0; j<m_numTreeTypes; j++) {
				if (m_treeTypes[j].m_data->m_modelName.compareNoCase(modelName)==0 && 
						m_treeTypes[j].m_data->m_textureName.compareNoCase(modelTexture)==0) {
					treeType = j;
					break;
				}
			}
		}
				
		xfer->xferReal(&tree.location.X);
		xfer->xferReal(&tree.location.Y);
		xfer->xferReal(&tree.location.Z);

		xfer->xferReal(&tree.scale);	///< Scale at location.
		xfer->xferReal(&tree.sin);	///< Sine of the rotation angle at location.
		xfer->xferReal(&tree.cos);	///< Cosine of the rotation angle at location.

		xfer->xferDrawableID(&tree.drawableID);	///< Drawable this tree corresponds to.

		// Topple parameters. [7/7/2003]
		xfer->xferReal(&tree.m_angularVelocity);	///< Velocity in degrees per frame (or is it radians per frame?)
		xfer->xferReal(&tree.m_angularAcceleration);	///< Acceleration angularVelocity is increasing
		xfer->xferCoord3D(&tree.m_toppleDirection);	///< Z-less direction we are toppling
		xfer->xferUser(&tree.m_toppleState, sizeof(tree.m_toppleState));	///< Stage this module is in.
		xfer->xferReal(&tree.m_angularAccumulation);	///< How much have I rotated so I know when to bounce.
		xfer->xferUnsignedInt(&tree.m_options);	///< topple options
		xfer->xferMatrix3D(&tree.m_mtx);
		xfer->xferUnsignedInt(&tree.m_sinkFramesLeft);	///< Toppled trees sink into the terrain & disappear, how many frames left.

		if (xfer->getXferMode() == XFER_LOAD && treeType != DELETED_TREE_TYPE && treeType < m_numTreeTypes) {
			Coord3D pos;
			pos.set(tree.location.X, tree.location.Y, tree.location.Z);
			Real angle = 0;
			addTree(tree.drawableID, pos, tree.scale, angle, 0, m_treeTypes[treeType].m_data);
			if (m_numTrees) {
				TTree *curTree = &m_trees[m_numTrees-1];
				curTree->m_angularAcceleration = tree.m_angularAcceleration;
				curTree->m_angularVelocity = tree.m_angularVelocity;
				curTree->m_toppleDirection = tree.m_toppleDirection;
				curTree->m_toppleState = tree.m_toppleState;
				curTree->m_options = tree.m_options;
				curTree->m_mtx = tree.m_mtx;
				curTree->m_sinkFramesLeft = tree.m_sinkFramesLeft;
			}
		}
	}

}  // end xfer

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void W3DTreeBuffer::loadPostProcess( void )
{
	// empty. jba [8/11/2003]	
}  // end loadPostProcess




