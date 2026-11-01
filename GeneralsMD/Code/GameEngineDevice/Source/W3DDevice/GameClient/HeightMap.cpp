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

// FILE: Heightmap.cpp ////////////////////////////////////////////////
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
// File name: Heightmap.cpp
//
// Created:   Mark W., John Ahlquist, April/May 2001
//
// Desc:      Draw the terrain and scorchmarks in a scene.
//
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
//         Includes                                                      
//-----------------------------------------------------------------------------


#include "W3DDevice/GameClient/HeightMap.h"
#include "Lib/Clock.h"

#ifndef USE_FLAT_HEIGHT_MAP // Flat height map uses flattened textures. jba. [3/20/2003]

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assetmgr.h>
#include <texture.h>
#include <tri.h>
#include <colmath.h>
#include <coltest.h>
#include <rinfo.h>
#include <camera.h>
#include "d3dx9runtime.h"
#include "Common/GlobalData.h"
#include "Common/PerfTimer.h"

#include "GameClient/InGameUI.h"
#include "GameClient/TerrainVisual.h"
#include "GameClient/View.h"
#include "GameClient/Water.h"

#include "GameLogic/AIPathfind.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/TerrainLogic.h"
#include "W3DDevice/GameClient/TerrainTex.h"
#include "W3DDevice/GameClient/W3DDynamicLight.h"
#include "W3DDevice/GameClient/W3DScene.h"
#include "W3DDevice/GameClient/W3DTerrainTracks.h"
#include "W3DDevice/GameClient/W3DReachOverlay.h"
#include "W3DDevice/GameClient/W3DBibBuffer.h"
#include "W3DDevice/GameClient/W3DTreeBuffer.h"
#include "W3DDevice/GameClient/W3DPropBuffer.h"
#include "W3DDevice/GameClient/W3DRoadBuffer.h"
#include "W3DDevice/GameClient/W3DBridgeBuffer.h"
#include "W3DDevice/GameClient/W3DWaypointBuffer.h"
#include "W3DDevice/GameClient/W3DCustomEdging.h"
#include "W3DDevice/GameClient/WorldHeightMap.h"
#include "W3DDevice/GameClient/W3DShaderManager.h"
#include "W3DDevice/GameClient/W3DShadow.h"
#include "W3DDevice/GameClient/W3DWater.h"
#include "W3DDevice/GameClient/W3DShroud.h"
#include "WW3D2/dx8wrapper.h"
#include "WW3D2/dx11runtime.h"
#include "WW3D2/light.h"
#include "WW3D2/scene.h"
#include "W3DDevice/GameClient/W3DPoly.h"
#include "W3DDevice/GameClient/W3DCustomScene.h"

#include "Common/PerfTimer.h"
#include "Common/UnitTimings.h" //Contains the DO_UNIT_TIMINGS define jba.		 

#ifdef _INTERNAL
// for occasional debugging...
//#pragma optimize("", off)
//#pragma MESSAGE("************************************** WARNING, optimization disabled for debugging purposes")
#endif

const Bool HALF_RES_MESH = false;

HeightMapRenderObjClass *TheHeightMap = NULL;
//-----------------------------------------------------------------------------
//         Private Data                                                     
//-----------------------------------------------------------------------------
#define SC_DETAIL_BLEND ( SHADE_CNST(ShaderClass::PASS_LEQUAL, ShaderClass::DEPTH_WRITE_ENABLE, ShaderClass::COLOR_WRITE_ENABLE, ShaderClass::SRCBLEND_ONE, \
	ShaderClass::DSTBLEND_ZERO, ShaderClass::FOG_DISABLE, ShaderClass::GRADIENT_MODULATE, ShaderClass::SECONDARY_GRADIENT_DISABLE, ShaderClass::TEXTURING_ENABLE, \
	ShaderClass::ALPHATEST_DISABLE, ShaderClass::CULL_MODE_ENABLE, ShaderClass::DETAILCOLOR_SCALE, ShaderClass::DETAILALPHA_DISABLE) )

static ShaderClass detailOpaqueShader(SC_DETAIL_BLEND);

#define DEFAULT_MAX_FRAME_EXTRABLEND_TILES		256	//default number of terrain tiles rendered per call (must fit in one VB)
#define DEFAULT_MAX_MAP_EXTRABLEND_TILES		2048	//default size of array allocated to hold all map extra blend tiles.
#define DEFAULT_MAX_BATCH_SHORELINE_TILES		512	//maximum number of terrain tiles rendered per call (must fit in one VB)
#define DEFAULT_MAX_MAP_SHORELINE_TILES		4096	//default size of array allocated to hold all map shoreline tiles.

#define ADJUST_FROM_INDEX_TO_REAL(k) ((k-m_map->getBorderSizeInline())*MAP_XY_FACTOR)
inline Int IABS(Int x) {	if (x>=0) return x; return -x;};

//-----------------------------------------------------------------------------
//         Private Functions                                               
//-----------------------------------------------------------------------------

//=============================================================================
// HeightMapRenderObjClass::freeIndexVertexBuffers
//=============================================================================
/** Frees the w3d resources used to draw the terrain. */
//=============================================================================
void HeightMapRenderObjClass::freeIndexVertexBuffers(void)
{
	REF_PTR_RELEASE(m_indexBuffer);
	REF_PTR_RELEASE(m_extraBlendVB);
	REF_PTR_RELEASE(m_extraBlendIB);
	m_extraBlendCapacity = 0;
	m_extraBlendMap = NULL;
	if (m_vertexBufferTiles) {
		for (int i=0; i<m_numVertexBufferTiles; i++)
			REF_PTR_RELEASE(m_vertexBufferTiles[i]);
		delete m_vertexBufferTiles;
		m_vertexBufferTiles = NULL;
	}
	if (m_vertexBufferBackup) {
		for (int i=0; i<m_numVertexBufferTiles; i++)
			delete m_vertexBufferBackup[i];
		delete m_vertexBufferBackup;
		m_vertexBufferBackup = NULL;
	}
	m_numVertexBufferTiles = 0;

}

//=============================================================================
// HeightMapRenderObjClass::freeMapResources
//=============================================================================
/** Frees the w3d resources used to draw the terrain. */
//=============================================================================
Int HeightMapRenderObjClass::freeMapResources(void)
{
	BaseHeightMapRenderObjClass::freeMapResources();
	freeIndexVertexBuffers();

	return 0;
}


//=============================================================================
// HeightMapRenderObjClass::doTheDynamicLight
//=============================================================================
/** Calculates the diffuse lighting as affected by dynamic lighting. */
//=============================================================================
UnsignedInt HeightMapRenderObjClass::doTheDynamicLight(VERTEX_FORMAT *vb, VERTEX_FORMAT *vbMirror, Vector3*light, Vector3*normal,  const DynamicLightInputs lights[], const Int lightsHere[], Int numHere)
{
#ifdef USE_NORMALS
	return;
#else
	Real shadeR, shadeG, shadeB;
	Int diffuse = vbMirror->diffuse;
#ifdef _DEBUG
	//vbMirror->diffuse += 30;	// Shows which vertexes are geting touched by dynamic light. debug only.
#endif
	
	// (gth) avoiding the extra divides (compiler unfortunately didn't do this automatically...)
	const float oo255 = (1.0f/255.0f);  
	shadeR = ((diffuse>>16)&0x00FF) * oo255;
	shadeG = ((diffuse>>8)&0x00FF) * oo255;
	shadeB = (diffuse&0x00FF) * oo255;

	Int alpha = (diffuse>>24)&0x00FF;
	Int k;
	for (k=0; k<numHere; k++) {
		const DynamicLightInputs &dl = lights[lightsHere[k]];
		if (!dl.enabled) {
			continue; // he is turned off.
		}
		Vector3 lightDirection(vbMirror->x, vbMirror->y, vbMirror->z);
		Real factor = 1.0f;
		switch(dl.type) {
		case LightClass::POINT:
		case LightClass::SPOT: {
				lightDirection -= dl.position;
				double range = dl.range, midRange = dl.midRange;
				Real dist = lightDirection.Length();
				if (dist >= range) continue;
				if (midRange < 0.1) continue;
				factor = 1.0f - (dist - midRange) / (range - midRange);
				factor = WWMath::Clamp(factor,0.0f,1.0f);

				// (gth) normalize here since we have the length
				lightDirection /= dist;
			} 
			break;
		case LightClass::DIRECTIONAL:
			lightDirection = dl.spotDirection;
			factor = 1.0;
			break;
		};
		
		// (gth) unneeded due to above normalization
		//lightDirection.Normalize();

		Vector3 lightRay(-lightDirection.X, -lightDirection.Y, -lightDirection.Z);
		Real shade = Vector3::Dot_Product(lightRay, *normal); 
		shade *= factor;
		const Vector3 &diffuse = dl.diffuse;
		const Vector3 &ambient = dl.ambient;
		if (shade > 1.0) shade = 1.0;
		if(shade < 0.0f) shade = 0.0f;
		shadeR += shade*diffuse.X;
		shadeG += shade*diffuse.Y;
		shadeB += shade*diffuse.Z;		
		shadeR += factor*ambient.X;
		shadeG += factor*ambient.Y;
		shadeB += factor*ambient.Z;		

	} 
	if (shadeR > 1.0) shadeR = 1.0;
	if(shadeR < 0.0f) shadeR = 0.0f;
	if (shadeG > 1.0) shadeG = 1.0;
	if(shadeG < 0.0f) shadeG = 0.0f;
	if (shadeB > 1.0) shadeB = 1.0;
	if(shadeB < 0.0f) shadeB = 0.0f;
	shadeR*=255.0f;
	shadeG*=255.0f;
	shadeB*=255.0f;

//	(gth) faster float to int conversion, return the result so we can re-use it.
//	vb->diffuse=REAL_TO_INT(shadeB) | (REAL_TO_INT(shadeG) << 8) | (REAL_TO_INT(shadeR) << 16) | ((int)alpha << 24);
	UnsignedInt light_val = WWMath::Float_To_Int_Chop(shadeB) | (WWMath::Float_To_Int_Chop(shadeG) << 8) | (WWMath::Float_To_Int_Chop(shadeR) << 16) | ((int)alpha << 24);
	vb->diffuse = light_val;
	return light_val;

#endif
}

//=============================================================================
// HeightMapRenderObjClass::updateVB
//=============================================================================
/** Update a rectangular block of the given Vertex Buffer. 
data is expected to be an array same dimensions as current heightmap
mapped into this VB.
*/
//=============================================================================
Int HeightMapRenderObjClass::updateVB(DX8VertexBufferClass	*pVB, char *data, Int x0, Int y0, Int x1, Int y1, Int originX, Int originY, WorldHeightMap *pMap, RefRenderObjListIterator *pLightsIterator)
{
	Int i,j;
	Vector3 lightRay[MAX_GLOBAL_LIGHTS];
	Int xCoord, yCoord;
	Int vn0,un0,vp1,up1;
	Vector3 l2r,n2f,normalAtTexel;
	Int	vertsPerRow=(VERTEX_BUFFER_TILE_LENGTH)*4;	//vertices per row of VB

	Int cellOffset = 1;
	if (HALF_RES_MESH) {
		cellOffset = 2;
	}

	REF_PTR_SET(m_map, pMap);	//update our heightmap pointer in case it changed since last call.
	m_lastRelightValid = false;	// the tile's diffuse goes back to static lighting, so the dynamic lights must be redone
	if (m_vertexBufferTiles && pMap)
	{
#ifdef _DEBUG
		assert(x0 >= originX && y0 >= originY && x1>x0 && y1>y0 && x1<=originX+VERTEX_BUFFER_TILE_LENGTH && y1<=originY+VERTEX_BUFFER_TILE_LENGTH);
#endif 

		// the global light directions do not change while one vertex buffer tile is being filled,
		// so build the rays once here instead of once per vertex - this loop used to sit inside
		// the i/j double loop and ran for every one of the tile's vertices.
		for (Int lightIndex=0; lightIndex < TheGlobalData->m_numGlobalLights; lightIndex++)
		{
			const Coord3D *lightPos = &TheGlobalData->m_terrainLightPos[lightIndex];
			lightRay[lightIndex].Set(-lightPos->x, -lightPos->y, -lightPos->z);
		}

		DX8VertexBufferClass::WriteLockClass lockVtxBuffer(pVB);
		VERTEX_FORMAT *vbHardware = (VERTEX_FORMAT*)lockVtxBuffer.Get_Vertex_Array();
		// a failed lock - a device that has gone away - hands back nothing to copy the tile into
		if (vbHardware == NULL) {
			return 0;
		}
		VERTEX_FORMAT *vBase = (VERTEX_FORMAT*)data;
		// Note that we are building the vertex buffer data in the memory buffer, data.
		// At the bottom, we will copy the final vertex data for one cell into the 
		// hardware vertex buffer. 
		
		for (j=y0; j<y1; j++)
		{
			VERTEX_FORMAT *vb = vBase;
			if (HALF_RES_MESH) {
				if (j&1) continue;
				vb += ((j-originY)/2)*vertsPerRow/2;	//skip to correct row in vertex buffer
				vb += ((x0-originX)/2)*4;		//skip to correct vertex in row.
			} else {
				vb += (j-originY)*vertsPerRow;	//skip to correct row in vertex buffer
				vb += (x0-originX)*4;		//skip to correct vertex in row.
			}
			vn0 = getYWithOrigin(j)-cellOffset;
			if (vn0 < -pMap->getDrawOrgY())
				vn0=-pMap->getDrawOrgY();
			vp1 = getYWithOrigin(j+cellOffset)+cellOffset;
			if (vp1 >= pMap->getYExtent()-pMap->getDrawOrgY())
				vp1=pMap->getYExtent()-pMap->getDrawOrgY()-1;

			yCoord = getYWithOrigin(j)+pMap->getDrawOrgY();
			for (i=x0; i<x1; i++)
			{
				if (HALF_RES_MESH) {
					if (i&1) continue;
				}
				un0 = getXWithOrigin(i)-cellOffset;
				if (un0 < -pMap->getDrawOrgX())
					un0=-pMap->getDrawOrgX();
				up1 = getXWithOrigin(i+cellOffset)+cellOffset;
				if (up1 >= pMap->getXExtent()-pMap->getDrawOrgX())
					up1=pMap->getXExtent()-pMap->getDrawOrgX()-1;
				xCoord = getXWithOrigin(i)+pMap->getDrawOrgX();

				//update the 4 vertices in this block
				float U[4], V[4];
				UnsignedByte alpha[4];
				float UA[4], VA[4];
				Bool flipForBlend = false;			 // True if the blend needs the triangles flipped.

				if (pMap) {
					pMap->getUVData(getXWithOrigin(i),getYWithOrigin(j),U, V, HALF_RES_MESH);
					pMap->getAlphaUVData(getXWithOrigin(i),getYWithOrigin(j), UA, VA, alpha, &flipForBlend, HALF_RES_MESH);
				} 


				//top-left sample
				l2r.Set(2*MAP_XY_FACTOR,0,MAP_HEIGHT_SCALE*(pMap->getDisplayHeight(getXWithOrigin(i)+cellOffset, getYWithOrigin(j)) - pMap->getDisplayHeight(un0, getYWithOrigin(j))));
				n2f.Set(0,2*MAP_XY_FACTOR,MAP_HEIGHT_SCALE*(pMap->getDisplayHeight(getXWithOrigin(i), (getYWithOrigin(j)+cellOffset)) - pMap->getDisplayHeight(getXWithOrigin(i), vn0)));
				
#ifdef ALLOW_TEMPORARIES
				normalAtTexel= Normalize(Vector3::Cross_Product(l2r,n2f));
#else
				Vector3::Normalized_Cross_Product(l2r, n2f, &normalAtTexel);
#endif

				vb->x=xCoord;
				vb->y=yCoord;
				vb->z=  ((float)pMap->getDisplayHeight(getXWithOrigin(i), getYWithOrigin(j)))*MAP_HEIGHT_SCALE;
				vb->x = ADJUST_FROM_INDEX_TO_REAL(vb->x);
				vb->y = ADJUST_FROM_INDEX_TO_REAL(vb->y);
				vb->u1=U[0];
				vb->v1=V[0];
				vb->u2=UA[0];
				vb->v2=VA[0];
				doTheLight(vb, lightRay, &normalAtTexel, pLightsIterator, alpha[0]);
				vb++;

				//top-right sample
				l2r.Set(2*MAP_XY_FACTOR,0,MAP_HEIGHT_SCALE*(pMap->getDisplayHeight(up1 , getYWithOrigin(j) ) - pMap->getDisplayHeight(getXWithOrigin(i) , getYWithOrigin(j) )));
				n2f.Set(0,2*MAP_XY_FACTOR,MAP_HEIGHT_SCALE*(pMap->getDisplayHeight(getXWithOrigin(i)+cellOffset , (getYWithOrigin(j)+cellOffset) ) - pMap->getDisplayHeight(getXWithOrigin(i)+cellOffset , vn0 )));
				
#ifdef ALLOW_TEMPORARIES
				normalAtTexel= Normalize(Vector3::Cross_Product(l2r,n2f));
#else
				Vector3::Normalized_Cross_Product(l2r, n2f, &normalAtTexel);
#endif

				vb->x=xCoord+cellOffset;
				vb->y=yCoord;
				vb->z=  ((float)pMap->getDisplayHeight(getXWithOrigin(i)+cellOffset, getYWithOrigin(j)))*MAP_HEIGHT_SCALE;
				vb->x = ADJUST_FROM_INDEX_TO_REAL(vb->x);
				vb->y = ADJUST_FROM_INDEX_TO_REAL(vb->y);
				vb->u1=U[1];
				vb->v1=V[1];
				vb->u2=UA[1];
				vb->v2=VA[1];
				doTheLight(vb, lightRay, &normalAtTexel, pLightsIterator, alpha[1]);
				vb++;

				//bottom-right sample
				l2r.Set(2*MAP_XY_FACTOR,0,MAP_HEIGHT_SCALE*(pMap->getDisplayHeight(up1 , (getYWithOrigin(j)+cellOffset) ) - pMap->getDisplayHeight(getXWithOrigin(i) , (getYWithOrigin(j)+cellOffset) )));
				n2f.Set(0,2*MAP_XY_FACTOR,MAP_HEIGHT_SCALE*(pMap->getDisplayHeight(getXWithOrigin(i)+cellOffset , vp1 ) - pMap->getDisplayHeight(getXWithOrigin(i)+cellOffset , getYWithOrigin(j) )));
				
#ifdef ALLOW_TEMPORARIES
				normalAtTexel= Normalize(Vector3::Cross_Product(l2r,n2f));
#else
				Vector3::Normalized_Cross_Product(l2r, n2f, &normalAtTexel);
#endif

				vb->x=xCoord+cellOffset;
				if (yCoord + 1 == pMap->getDrawOrgY() + m_y - 1) { 
					vb->y=yCoord+1;
				} else {
					vb->y=yCoord+cellOffset;
				}
				vb->z=  ((float)pMap->getDisplayHeight(getXWithOrigin(i)+cellOffset, getYWithOrigin(j)+cellOffset))*MAP_HEIGHT_SCALE;
				vb->x = ADJUST_FROM_INDEX_TO_REAL(vb->x);
				vb->y = ADJUST_FROM_INDEX_TO_REAL(vb->y);
				vb->u1=U[2];
				vb->v1=V[2];
				vb->u2=UA[2];
				vb->v2=VA[2];
				doTheLight(vb, lightRay, &normalAtTexel, pLightsIterator, alpha[2]);
				vb++;

				//bottom-left sample
				l2r.Set(2*MAP_XY_FACTOR,0,MAP_HEIGHT_SCALE*(pMap->getDisplayHeight(getXWithOrigin(i)+cellOffset , (getYWithOrigin(j)+cellOffset) ) - pMap->getDisplayHeight(un0 , (getYWithOrigin(j)+cellOffset) )));
				n2f.Set(0,2*MAP_XY_FACTOR,MAP_HEIGHT_SCALE*(pMap->getDisplayHeight(getXWithOrigin(i) , vp1 ) - pMap->getDisplayHeight(getXWithOrigin(i) , getYWithOrigin(j) )));
				
#ifdef ALLOW_TEMPORARIES
				normalAtTexel= Normalize(Vector3::Cross_Product(l2r,n2f));
#else
				Vector3::Normalized_Cross_Product(l2r, n2f, &normalAtTexel);
#endif

				if (xCoord == pMap->getDrawOrgX()) { 
					vb->x=xCoord;
					//if (vb->x < 0) vb->x = 0;
				} else {
					vb->x=xCoord;
				}
				if (yCoord + 1 == pMap->getDrawOrgY() + m_y - 1) { 
					vb->y=yCoord+1;
				} else {
					vb->y=yCoord+cellOffset;
				}
				vb->z=  ((float)pMap->getDisplayHeight(getXWithOrigin(i), getYWithOrigin(j)+cellOffset))*MAP_HEIGHT_SCALE;
				vb->x = ADJUST_FROM_INDEX_TO_REAL(vb->x);
				vb->y = ADJUST_FROM_INDEX_TO_REAL(vb->y);
				vb->u1=U[3];
				vb->v1=V[3];
				vb->u2=UA[3];
				vb->v2=VA[3];
				doTheLight(vb, lightRay, &normalAtTexel, pLightsIterator, alpha[3]);
				vb++;

				VERTEX_FORMAT *pCurVertices = vb-4;
#ifdef FLIP_TRIANGLES // jba - reduces "diamonding" in some cases, not others.  Better cliffs, though.
				VERTEX_FORMAT tmpVertex;
				if (flipForBlend) {
					tmpVertex = pCurVertices[0];
					pCurVertices[0] = pCurVertices[1];
					pCurVertices[1] = pCurVertices[2];
					pCurVertices[2] = pCurVertices[3];
					pCurVertices[3] = tmpVertex;
				}
#endif

				if (m_showImpassableAreas) {
					// Color impassable cells "red"
					DEBUG_ASSERTCRASH(PATHFIND_CELL_SIZE_F == MAP_XY_FACTOR, ("Pathfind must be terrain cell size, or this code needs reworking.  John A."));
					Real borderHiX = (pMap->getXExtent()-2*pMap->getBorderSizeInline())*MAP_XY_FACTOR;
					Real borderHiY = (pMap->getYExtent()-2*pMap->getBorderSizeInline())*MAP_XY_FACTOR;
					Bool border = pCurVertices[0].x == -MAP_XY_FACTOR || pCurVertices[0].y == -MAP_XY_FACTOR;
					Bool cliffMapped = pMap->isCliffMappedTexture(getXWithOrigin(i), getYWithOrigin(j));
					if (pCurVertices[0].x == borderHiX) {
						border = true;
					}
					if (pCurVertices[0].y == borderHiY) {
						border = true;
					}
					Bool isCliff = pMap->getCliffState(getXWithOrigin(i)+pMap->getDrawOrgX(), getYWithOrigin(j)+pMap->getDrawOrgY())
												 || showAsVisibleCliff(getXWithOrigin(i) + pMap->getDrawOrgX(), getYWithOrigin(j)+pMap->getDrawOrgY());

					if ( isCliff || border || cliffMapped) {
						Int cellX, cellY;
						for (cellX=0; cellX<2; cellX++) {
							for (cellY=0; cellY<2; cellY++) {
								Int vertex = cellX+2*cellY;
								if (border) {
									Bool doBorder = false;
									if (pCurVertices[vertex].y >= 0 && pCurVertices[vertex].y <= borderHiY) {
										if (pCurVertices[vertex].x == 0 || pCurVertices[vertex].x == borderHiX) {
											doBorder = true;
										}
									}
									if (pCurVertices[vertex].x >= 0 && pCurVertices[vertex].x <= borderHiX) {
										if (pCurVertices[vertex].y == 0 || pCurVertices[vertex].y == borderHiY) {
											doBorder = true;
										}
									}
									if (doBorder) {
										pCurVertices[vertex].diffuse &= 0xFF0000ff; // blue with alpha.
									}
								} else if (isCliff) {
									pCurVertices[vertex].diffuse &= 0xFFFF0000; // red with alpha.
								}
								if (cliffMapped && vertex==0) {
									pCurVertices[vertex].diffuse &= 0xFF000000; // Black.
									pCurVertices[vertex].diffuse |= 0xff00; // Add green.
								}
							}
						}
					}
				}

				// Note - We have been building the vertex buffer in the memory location.
				// Now copy the set of vertices into the hardware buffer.
				// We don't copy the whole vertex buffer because we often update only
				// a couple of rows and its a lot faster to just copy the ones that change.
				Int offset = pCurVertices - vBase;
				memcpy(vbHardware+offset, pCurVertices, 4*sizeof(VERTEX_FORMAT));
			}
		}
		return 0; //success.
	}
	return -1;
}

//=============================================================================
// HeightMapRenderObjClass::updateVBForLight
//=============================================================================
/** Update the dynamic lighting values only in a rectangular block of the given Vertex Buffer. 
The vertex locations and texture coords are unchanged.
*/
Int HeightMapRenderObjClass::updateVBForLight(DX8VertexBufferClass	*pVB, char *data, Int x0, Int y0, Int x1, Int y1, Int originX, Int originY, W3DDynamicLight *pLights[], Int numLights)
{
	Int i,j,k;
	Int vn0,un0,vp1,up1;
	Vector3 l2r,n2f,normalAtTexel;
	Int	vertsPerRow=(VERTEX_BUFFER_TILE_LENGTH)*4;	//vertices per row of VB

	if (m_vertexBufferTiles && m_map)
	{
#ifdef _DEBUG
		assert(x0 >= originX && y0 >= originY && x1>x0 && y1>y0 && x1<=originX+VERTEX_BUFFER_TILE_LENGTH && y1<=originY+VERTEX_BUFFER_TILE_LENGTH);
#endif 

		// The tile's columns and rows each light covers, one bit each, where it is now and where it was
		// at the last relight.  A quad under either rectangle of any light is rewritten, the old rectangle
		// so it goes back to its own colour.  Only the lights whose new rectangle holds a quad can reach
		// its corners: that rectangle is the light's whole range rounded out to cells.  Asking every light
		// at every quad and every corner was a third of the render on a crowded Classic frame.
		UnsignedInt colsNow[MAX_ENABLED_DYNAMIC_LIGHTS], colsPrev[MAX_ENABLED_DYNAMIC_LIGHTS];
		UnsignedInt rowsNow[MAX_ENABLED_DYNAMIC_LIGHTS], rowsPrev[MAX_ENABLED_DYNAMIC_LIGHTS];
		for (k=0; k<numLights; k++) {
			colsNow[k] = colsPrev[k] = rowsNow[k] = rowsPrev[k] = 0;
		}
		for (i=x0; i<x1; i++)
		{
			if (HALF_RES_MESH && (i&1)) continue;
			Int xCoord = getXWithOrigin(i)+m_map->getDrawOrgX()-m_map->getBorderSizeInline();
			for (k=0; k<numLights; k++) {
				if (pLights[k]->m_minX <= xCoord+1 && pLights[k]->m_maxX >= xCoord) colsNow[k] |= 1u<<(i-x0);
				if (pLights[k]->m_prevMinX <= xCoord+1 && pLights[k]->m_prevMaxX >= xCoord) colsPrev[k] |= 1u<<(i-x0);
			}
		}
		for (j=y0; j<y1; j++)
		{
			if (HALF_RES_MESH && (j&1)) continue;
			Int yCoord = getYWithOrigin(j)+m_map->getDrawOrgY()-m_map->getBorderSizeInline();
			for (k=0; k<numLights; k++) {
				if (pLights[k]->m_minY <= yCoord+1 && pLights[k]->m_maxY >= yCoord) rowsNow[k] |= 1u<<(j-y0);
				if (pLights[k]->m_prevMinY <= yCoord+1 && pLights[k]->m_prevMaxY >= yCoord) rowsPrev[k] |= 1u<<(j-y0);
			}
		}

		// Lock only from the first quad the loop below writes to the end of the last one.  The
		// whole tile is 4096 vertices, and with Direct3D 11 its unlock copied every byte into the
		// D3D9 buffer and again into the D3D11 one for a light a few cells wide.  The bytes outside
		// the range are untouched, and every copy of them already holds the same thing.
		UnsignedInt quadsInRow[VERTEX_BUFFER_TILE_LENGTH];
		Int firstVertex = -1;
		Int endVertex = 0;
		for (j=y0; j<y1; j++)
		{
			UnsignedInt quads = 0;
			for (k=0; k<numLights; k++) {
				if (rowsNow[k] & (1u<<(j-y0))) quads |= colsNow[k];
				if (rowsPrev[k] & (1u<<(j-y0))) quads |= colsPrev[k];
			}
			quadsInRow[j-y0] = quads;
			for (i=x0; i<x1; i++)
			{
				if (!(quads & (1u<<(i-x0)))) continue;
				Int offset = (j-originY)*vertsPerRow+4*(i-originX);
				if (HALF_RES_MESH) {
					offset = (j-originY)*vertsPerRow/4+2*(i-originX);
				}
				if (firstVertex < 0) firstVertex = offset;
				endVertex = offset + 4;
			}
		}
		if (firstVertex < 0)
			return 0;	// no quad under a light: the loop below would write nothing

		DX8VertexBufferClass::AppendLockClass lockVtxBuffer(pVB, firstVertex, endVertex-firstVertex);
		VERTEX_FORMAT *vLocked = (VERTEX_FORMAT*)lockVtxBuffer.Get_Vertex_Array();
		// a failed lock - a device that has gone away - hands back nothing to light
		if (vLocked == NULL) {
			m_lastRelightValid = false;	// nothing written, so the next pass tries again
			return 0;
		}
		VERTEX_FORMAT *vBase = vLocked - firstVertex;	// offsets below stay tile-relative
		VERTEX_FORMAT *vb;

		for (j=y0; j<y1; j++)
		{
			const UnsignedInt quads = quadsInRow[j-y0];
			if (quads == 0) {
				continue;
			}
			vn0 = getYWithOrigin(j)-1;
			if (vn0 < -m_map->getDrawOrgY())
				vn0=-m_map->getDrawOrgY();
			vp1 = getYWithOrigin(j+1)+1;
			if (vp1 >= m_map->getYExtent()-m_map->getDrawOrgY())
				vp1=m_map->getYExtent()-m_map->getDrawOrgY()-1;

			for (i=x0; i<x1; i++)
			{
				if (!(quads & (1u<<(i-x0)))) {
					continue;
				}
				Int lightsHere[MAX_ENABLED_DYNAMIC_LIGHTS];
				Int numHere = 0;
				for (k=0; k<numLights; k++) {
					if ((rowsNow[k] & (1u<<(j-y0))) && (colsNow[k] & (1u<<(i-x0)))) {
						lightsHere[numHere++] = k;
					}
				}
				// vb is the pointer to the vertex in the hardware dx8 vertex buffer.
				Int offset = (j-originY)*vertsPerRow+4*(i-originX);
				if (HALF_RES_MESH) {
					offset = (j-originY)*vertsPerRow/4+2*(i-originX);
				}
				vb = vBase + offset;	//skip to correct row in vertex buffer
				// vbMirror is the pointer to the vertex in our memory based copy.
				// The important point is that we can read out of our copy to get the original
				// diffuse color, and xyz location.  It is VERY SLOW to read out of the
				// hardware vertex buffer, possibly worse... jba.
				VERTEX_FORMAT *vbMirror = ((VERTEX_FORMAT*)data)  + offset;
				un0 = getXWithOrigin(i)-1;
				if (un0 < -m_map->getDrawOrgX())
					un0=-m_map->getDrawOrgX();
				up1 = getXWithOrigin(i+1)+1;
				if (up1 >= m_map->getXExtent()-m_map->getDrawOrgX())
					up1=m_map->getXExtent()-m_map->getDrawOrgX()-1;

				Vector3 lightRay(0,0,0);

				//top-left sample
				l2r.Set(2*MAP_XY_FACTOR,0,MAP_HEIGHT_SCALE*(m_map->getDisplayHeight(getXWithOrigin(i)+1, getYWithOrigin(j)) - m_map->getDisplayHeight(un0, getYWithOrigin(j))));
				n2f.Set(0,2*MAP_XY_FACTOR,MAP_HEIGHT_SCALE*(m_map->getDisplayHeight(getXWithOrigin(i), (getYWithOrigin(j)+1)) - m_map->getDisplayHeight(getXWithOrigin(i), vn0)));
				
#ifdef ALLOW_TEMPORARIES
				normalAtTexel= Normalize(Vector3::Cross_Product(l2r,n2f));
#else
				Vector3::Normalized_Cross_Product(l2r, n2f, &normalAtTexel);
#endif

				doTheDynamicLight(vb, vbMirror, &lightRay, &normalAtTexel, m_lastRelight.lights, lightsHere, numHere);
				vb++;	vbMirror++;

				//top-right sample
				l2r.Set(2*MAP_XY_FACTOR,0,MAP_HEIGHT_SCALE*(m_map->getDisplayHeight(up1 , getYWithOrigin(j) ) - m_map->getDisplayHeight(getXWithOrigin(i) , getYWithOrigin(j) )));
				n2f.Set(0,2*MAP_XY_FACTOR,MAP_HEIGHT_SCALE*(m_map->getDisplayHeight(getXWithOrigin(i)+1 , (getYWithOrigin(j)+1) ) - m_map->getDisplayHeight(getXWithOrigin(i)+1 , vn0 )));
				
#ifdef ALLOW_TEMPORARIES
				normalAtTexel= Normalize(Vector3::Cross_Product(l2r,n2f));
#else
				Vector3::Normalized_Cross_Product(l2r, n2f, &normalAtTexel);
#endif

				doTheDynamicLight(vb, vbMirror, &lightRay, &normalAtTexel, m_lastRelight.lights, lightsHere, numHere);
				vb++;	vbMirror++;

				//bottom-right sample
				l2r.Set(2*MAP_XY_FACTOR,0,MAP_HEIGHT_SCALE*(m_map->getDisplayHeight(up1 , (getYWithOrigin(j)+1) ) - m_map->getDisplayHeight(getXWithOrigin(i) , (getYWithOrigin(j)+1) )));
				n2f.Set(0,2*MAP_XY_FACTOR,MAP_HEIGHT_SCALE*(m_map->getDisplayHeight(getXWithOrigin(i)+1 , vp1 ) - m_map->getDisplayHeight(getXWithOrigin(i)+1 , getYWithOrigin(j) )));
				
#ifdef ALLOW_TEMPORARIES
				normalAtTexel= Normalize(Vector3::Cross_Product(l2r,n2f));
#else
				Vector3::Normalized_Cross_Product(l2r, n2f, &normalAtTexel);
#endif

				doTheDynamicLight(vb, vbMirror, &lightRay, &normalAtTexel, m_lastRelight.lights, lightsHere, numHere);
				vb++;	vbMirror++;

				//bottom-left sample
				l2r.Set(2*MAP_XY_FACTOR,0,MAP_HEIGHT_SCALE*(m_map->getDisplayHeight(getXWithOrigin(i)+1 , (getYWithOrigin(j)+1) ) - m_map->getDisplayHeight(un0 , (getYWithOrigin(j)+1) )));
				n2f.Set(0,2*MAP_XY_FACTOR,MAP_HEIGHT_SCALE*(m_map->getDisplayHeight(getXWithOrigin(i) , vp1 ) - m_map->getDisplayHeight(getXWithOrigin(i) , getYWithOrigin(j) )));
				
#ifdef ALLOW_TEMPORARIES
				normalAtTexel= Normalize(Vector3::Cross_Product(l2r,n2f));
#else
				Vector3::Normalized_Cross_Product(l2r, n2f, &normalAtTexel);
#endif

				doTheDynamicLight(vb, vbMirror, &lightRay, &normalAtTexel, m_lastRelight.lights, lightsHere, numHere);
				vb++;	vbMirror++;
			}
		}
		return 0; //success.
	}
	return -1;
}





//=============================================================================
// HeightMapRenderObjClass::doPartialUpdate
//=============================================================================
/** Updates a partial block of vertices from [x0,y0 to x1,y1]
The coordinates in partialRange are map cell coordinates, relative to the entire map.
The vertex coordinates and texture coordinates, as well as static lighting are updated.
*/
void HeightMapRenderObjClass::doPartialUpdate(const IRegion2D &partialRange, WorldHeightMap *htMap, RefRenderObjListIterator *pLightsIterator)
{	
	// Adjust range into the current drawn map range.
	Int minX = partialRange.lo.x - htMap->getDrawOrgX();
	Int maxX = partialRange.hi.x - htMap->getDrawOrgX();
	Int minY = partialRange.lo.y - htMap->getDrawOrgY();
	Int maxY = partialRange.hi.y - htMap->getDrawOrgY();
	if (minX<0) minX = 0;
	if (minY<0) minY = 0;
	if (maxX > m_x-1) maxX = m_x-1;
	if (maxY > m_y-1) maxY = m_y-1;
	if (maxX < minX) return;
	if (maxY < minY) return;
	if (m_originX == 0 && m_originY == 0) {
		// simple case.
		updateBlock(minX, minY, maxX, maxY,
								htMap, pLightsIterator);
	}
	else
	{
		minY = minY+m_originY;
		maxY = maxY+m_originY;

		if (minY> m_y-1) {
			minY -= m_y-1;
 			maxY -= m_y-1;
		}
		if (maxY > m_y-1) {
			maxY -= m_y-1;
			updateBlock(0, minY, m_x-1, m_y-1, htMap, pLightsIterator);
			updateBlock(0, 0, m_x-1, maxY, htMap, pLightsIterator);
		} else {
			updateBlock(0, minY, m_x-1, maxY, htMap, pLightsIterator);
		}
	}

	if (!m_extraBlendTilePositions)
	{	//Need to allocate memory
		m_extraBlendTilePositions = NEW Int[DEFAULT_MAX_MAP_EXTRABLEND_TILES];
		m_extraBlendTilePositionsSize = DEFAULT_MAX_MAP_EXTRABLEND_TILES;
	}
	
	//Find list of all extra blend tiles used on map.  These are tiles with 3 materials/textures
	//over the same tile and require an extra render pass.

	Int i, j;
	m_terrainContentVersion++;	// the 3-way list below changes
	//First remove any existing extra blend tiles within this partial region
	for (j=0; j<m_numExtraBlendTiles; j++)
	{	Int x = m_extraBlendTilePositions[j] & 0xffff;
		Int y = m_extraBlendTilePositions[j] >> 16;
		if (x >= partialRange.lo.x && x < partialRange.hi.x &&
			y >= partialRange.lo.y && y < partialRange.hi.y)
		{	//this tile is inside region being updated so remove it by shifting tile array
			memmove(m_extraBlendTilePositions+j,m_extraBlendTilePositions+j+1,(m_numExtraBlendTiles-1-j)*sizeof(Int));
			m_numExtraBlendTiles--;
			j--;	//need to look at index j again because this tile was removed
		}
	}

	for (j=partialRange.lo.y; j<partialRange.hi.y; j++)
		for (i=partialRange.lo.x; i<partialRange.hi.x; i++)
		{
			if (j<0 || i<0) continue;
			Real U[4],V[4];
			UnsignedByte alpha[4];
			Bool flipState,cliffState;
			if (htMap->getExtraAlphaUVData(i,j,U,V,alpha,&flipState, &cliffState))
			{	if (m_numExtraBlendTiles >= m_extraBlendTilePositionsSize)
				{	//no more room to store extra blend tiles so enlarge the buffer.
					Int *tempPositions=NEW Int[m_extraBlendTilePositionsSize+512];
					memcpy(tempPositions, m_extraBlendTilePositions, m_extraBlendTilePositionsSize*sizeof(Int));
					delete [] m_extraBlendTilePositions;
					//enlarge by more tiles to reduce memory trashing
					m_extraBlendTilePositions = tempPositions;
					m_extraBlendTilePositionsSize += 512;
				}
				//Pack x and y position into single integer since maps are limited in size
				m_extraBlendTilePositions[m_numExtraBlendTiles]=i | (j <<16);
				m_numExtraBlendTiles++;
			}
		}
	updateShorelineTiles(partialRange.lo.x,partialRange.lo.y,partialRange.hi.x,partialRange.hi.y,htMap);

	updateViewImpassableAreas(TRUE, minX, maxX, minY, maxY);
}

//=============================================================================
// HeightMapRenderObjClass::updateBlock
//=============================================================================
/** Updates a block of vertices from [x0,y0 to x1,y1]
The vertex coordinates and texture coordinates, as well as static lighting are updated.
*/
Int HeightMapRenderObjClass::updateBlock(Int x0, Int y0, Int x1, Int y1,  WorldHeightMap *pMap, RefRenderObjListIterator *pLightsIterator)
{	
#ifdef _DEBUG
	DEBUG_ASSERTCRASH(x0>=0,  ("HeightMapRenderObjClass::UpdateBlock parameters extend beyond left edge."));
	DEBUG_ASSERTCRASH(y0>=0,  ("HeightMapRenderObjClass::UpdateBlock parameters extend beyond bottom edge."));
	DEBUG_ASSERTCRASH(x1<m_x, ("HeightMapRenderObjClass::UpdateBlock parameters extend beyond right edge."));
	DEBUG_ASSERTCRASH(y1<m_y, ("HeightMapRenderObjClass::UpdateBlock parameters extend beyond top edge."));
	DEBUG_ASSERTCRASH(x0<=x1, ("HeightMapRenderObjClass::UpdateBlock parameters have inside-out rectangle (on X)."));
	DEBUG_ASSERTCRASH(y0<=y1, ("HeightMapRenderObjClass::UpdateBlock parameters have inside-out rectangle (on Y)."));
#endif
	Invalidate_Cached_Bounding_Volumes();
	if (pMap) {
		REF_PTR_SET(m_stageZeroTexture, pMap->getTerrainTexture());
		REF_PTR_SET(m_stageOneTexture, pMap->getAlphaTerrainTexture());
	}

	Int i,j;
	DX8VertexBufferClass	**pVB;
	Int originX,originY;
	//step through each vertex buffer that needs updating
	for (j=0; j<m_numVBTilesY; j++)
	{
		originY=j*VERTEX_BUFFER_TILE_LENGTH;	//location of this VB on the large full-size heightmap
		Int yMin, yMax;
		yMin = originY;
		if (y0>yMin) yMin = y0;
		yMax = originY+VERTEX_BUFFER_TILE_LENGTH;
		if (y1<yMax) yMax = y1;
		if (yMin >= yMax) {
			continue;
		}
		for (i=0; i<m_numVBTilesX; i++)
		{	
			originX=i*VERTEX_BUFFER_TILE_LENGTH;	//location of this VB on the large full-size heightmap
			Int xMin, xMax;
			xMin = originX;
			if (xMin<x0) xMin = x0;
			xMax = originX+VERTEX_BUFFER_TILE_LENGTH;
			if (xMax>x1) xMax = x1;
			if (xMin >= xMax) {
				continue;
			}
			pVB=m_vertexBufferTiles+j*m_numVBTilesX+i;	//point to correct row/column of vertex buffers 
			char **pData = m_vertexBufferBackup+j*m_numVBTilesX+i;
			updateVB(*pVB, *pData, xMin, yMin, xMax, yMax, originX, originY, pMap, pLightsIterator);
		}
	}

	return 0;
}


//-----------------------------------------------------------------------------
//         Public Functions                                                
//-----------------------------------------------------------------------------

//=============================================================================
// HeightMapRenderObjClass::~HeightMapRenderObjClass
//=============================================================================
/** Destructor. Releases w3d assets. */
//=============================================================================
HeightMapRenderObjClass::~HeightMapRenderObjClass(void)
{
	W3DReachOverlay_Release();
	freeMapResources();
	if (m_extraBlendTilePositions) {
		delete [] m_extraBlendTilePositions;
		m_extraBlendTilePositions = NULL;
	}
}

//=============================================================================
// HeightMapRenderObjClass::HeightMapRenderObjClass
//=============================================================================
/** Constructor. Mostly nulls out the member variables. */
//=============================================================================
HeightMapRenderObjClass::HeightMapRenderObjClass(void):
m_extraBlendTilePositions(NULL),
m_numExtraBlendTiles(0),
m_numVisibleExtraBlendTiles(0),
m_extraBlendTilePositionsSize(0),
m_extraBlendVB(NULL),
m_extraBlendIB(NULL),
m_extraBlendCapacity(0),
m_extraBlendMap(NULL),
m_extraBlendVertexCount(0),
m_extraBlendIndexCount(0),
m_vertexBufferTiles(NULL),
m_vertexBufferBackup(NULL),
m_originX(0),
m_originY(0),
m_indexBuffer(NULL),
m_numVBTilesX(0),
m_numVBTilesY(0),
m_numVertexBufferTiles(0),
m_numBlockColumnsInLastVB(0),
m_numBlockRowsInLastVB(0),
m_lastRelightValid(false)
{
	TheHeightMap = this;
}


//=============================================================================
// HeightMapRenderObjClass::adjustTerrainLOD
//=============================================================================
/** Adjust the terrain Level Of Detail.  If adj > 0 , increases LOD 1 step, if 
adj < 0 decreases it one step, if adj==0, then just sets up for the current LOD */
//=============================================================================
void HeightMapRenderObjClass::adjustTerrainLOD(Int adj) 
{
	BaseHeightMapRenderObjClass::adjustTerrainLOD(adj);

	return;

#if 0
	if (adj>0 && TheGlobalData->m_terrainLOD<TERRAIN_LOD_MAX) TheWritableGlobalData->m_terrainLOD=(TerrainLOD)(TheGlobalData->m_terrainLOD+1);
	if (adj<0 && TheGlobalData->m_terrainLOD>TERRAIN_LOD_MIN) TheWritableGlobalData->m_terrainLOD=(TerrainLOD)(TheGlobalData->m_terrainLOD-1);

	switch (TheGlobalData->m_terrainLOD) {
		case	TERRAIN_LOD_MIN: TheWritableGlobalData->m_useCloudMap = false;
									TheWritableGlobalData->m_useLightMap = false ;
									TheWritableGlobalData->m_stretchTerrain = false;
									TheWritableGlobalData->m_useHalfHeightMap = true;
									break;
		case TERRAIN_LOD_HALF_CLOUDS: TheWritableGlobalData->m_useCloudMap = true;
									TheWritableGlobalData->m_useLightMap = true;
									TheWritableGlobalData->m_stretchTerrain = false;
									TheWritableGlobalData->m_useHalfHeightMap = true;
									break;
		case TERRAIN_LOD_STRETCH_NO_CLOUDS: TheWritableGlobalData->m_useCloudMap = false;
									TheWritableGlobalData->m_useLightMap = false;
									TheWritableGlobalData->m_stretchTerrain = true;
									TheWritableGlobalData->m_useHalfHeightMap = false;
									break;
		case TERRAIN_LOD_STRETCH_CLOUDS: TheWritableGlobalData->m_useCloudMap = true;
									TheWritableGlobalData->m_useLightMap = true;
									TheWritableGlobalData->m_stretchTerrain = true;
									TheWritableGlobalData->m_useHalfHeightMap = false;
									break;
		case TERRAIN_LOD_NO_CLOUDS: TheWritableGlobalData->m_useCloudMap = false;
									TheWritableGlobalData->m_useLightMap = false;
									TheWritableGlobalData->m_stretchTerrain = false;
									TheWritableGlobalData->m_useHalfHeightMap = false;
									break;
		default:
		case TERRAIN_LOD_NO_WATER: TheWritableGlobalData->m_useCloudMap = true;
									TheWritableGlobalData->m_useLightMap = true;
									TheWritableGlobalData->m_stretchTerrain = false;
									TheWritableGlobalData->m_useHalfHeightMap = false;
									break;
		case TERRAIN_LOD_MAX: TheWritableGlobalData->m_useCloudMap = true;
									TheWritableGlobalData->m_useLightMap = true;
									TheWritableGlobalData->m_stretchTerrain = false;
									TheWritableGlobalData->m_useHalfHeightMap = false;
									break;
	}
	if (m_map==NULL) return;
	m_map->setDrawOrg(m_map->getDrawOrgX(), m_map->getDrawOrgX());
	if (m_shroud)
		m_shroud->reset();	//need reset here since initHeightData will load new shroud.
	this->initHeightData(m_map->getDrawWidth(), 
											m_map->getDrawHeight(), m_map, NULL);
	staticLightingChanged();
	if (TheTacticalView) {
		TheTacticalView->setAngle(TheTacticalView->getAngle() + 1);
		TheTacticalView->setAngle(TheTacticalView->getAngle() - 1);
	}
#endif 
}

//=============================================================================
// HeightMapRenderObjClass::ReleaseResources
//=============================================================================
/** Releases all w3d assets, to prepare for Reset device call. */
//=============================================================================
void HeightMapRenderObjClass::ReleaseResources(void)
{
	W3DReachOverlay_Release();
	BaseHeightMapRenderObjClass::ReleaseResources();
}

//=============================================================================
// HeightMapRenderObjClass::ReAcquireResources
//=============================================================================
/** Reallocates all W3D assets after a reset.. */
//=============================================================================
void HeightMapRenderObjClass::ReAcquireResources(void)
{
	BaseHeightMapRenderObjClass::ReAcquireResources();
}

//=============================================================================
// HeightMapRenderObjClass::reset
//=============================================================================
/** Updates the macro noise/lightmap texture (pass 3) */
//=============================================================================
void HeightMapRenderObjClass::reset(void)
{
	BaseHeightMapRenderObjClass::reset();
}

//=============================================================================
// HeightMapRenderObjClass::oversizeTerrain
//=============================================================================
/** Sets the terrain oversize amount. */
//=============================================================================
void HeightMapRenderObjClass::oversizeTerrain(Int tilesToOversize)
{
	// Nothing left to widen: WorldHeightMap now draws the whole map on every map, so the 129x129
	// window this used to push out to 129+32n no longer exists.  Running the old arithmetic
	// anyway set the draw size to 161..257 vertices, which on any map bigger than that took
	// terrain away from the script that asked for more of it.
}




//=============================================================================
// HeightMapRenderObjClass::initHeightData
//=============================================================================
/** Allocate a heightmap of x by y vertices and fill with initial height values.
Also allocates all rendering resources such as vertex buffers, index buffers, 
shaders, and materials.*/
//=============================================================================
Int HeightMapRenderObjClass::initHeightData(Int x, Int y, WorldHeightMap *pMap, RefRenderObjListIterator *pLightsIterator, Bool updateExtraPassTiles)
{	
	BaseHeightMapRenderObjClass::initHeightData(x, y, pMap, pLightsIterator, updateExtraPassTiles);
	Int i,j;
//	Int	vertsPerRow=x*2-2;
//	Int	vertsPerColumn=y*2-2;

	HeightSampleType *data = NULL;
	if (pMap) {
		data = pMap->getDataPtr();
	}

	if (updateExtraPassTiles)
	{
		m_numExtraBlendTiles = 0;
		//Do some preprocessing on map to extract useful data
		if (pMap)
		{
			Int m_mapDX=pMap->getXExtent();
			Int m_mapDY=pMap->getYExtent();
			if (!m_extraBlendTilePositions)
			{	//Need to allocate memory
				m_extraBlendTilePositions = NEW Int[DEFAULT_MAX_MAP_EXTRABLEND_TILES];
				m_extraBlendTilePositionsSize = DEFAULT_MAX_MAP_EXTRABLEND_TILES;
			}
			
			//Find list of all extra blend tiles used on map.  These are tiles with 3 materials/textures
			//over the same tile and require an extra render pass.
			for (j=0; j<(m_mapDY-1); j++)
				for (i=0; i<(m_mapDX-1); i++)
				{
					Real U[4],V[4];
					UnsignedByte alpha[4];
					Bool flipState,cliffState;
					if (pMap->getExtraAlphaUVData(i,j,U,V,alpha,&flipState, &cliffState))
					{	if (m_numExtraBlendTiles >= m_extraBlendTilePositionsSize)
						{	//no more room to store extra blend tiles so enlarge the buffer.
							Int *tempPositions=NEW Int[m_extraBlendTilePositionsSize+512];
							memcpy(tempPositions, m_extraBlendTilePositions, m_extraBlendTilePositionsSize*sizeof(Int));
							delete [] m_extraBlendTilePositions;
							//enlarge by more tiles to reduce memory trashing
							m_extraBlendTilePositions = tempPositions;
							m_extraBlendTilePositionsSize += 512;
						}
						//Pack x and y position into single integer since maps are limited in size
						m_extraBlendTilePositions[m_numExtraBlendTiles]=i | (j <<16);
						m_numExtraBlendTiles++;
					}
				}
		}
	}

	m_originX = 0;
	m_originY = 0;
	m_needFullUpdate = true;

	// If the size changed, we need to allocate.
	Bool needToAllocate = (x != m_x || y != m_y);
	// If the textures aren't allocated (usually because of a hardware reset) need to allocate.
	if (m_stageOneTexture == NULL) {
		needToAllocate = true;
	}
	if (data && needToAllocate)
	{	//requested heightmap different from old one.
		freeIndexVertexBuffers();
		//Create static index buffers.  These will index the vertex buffers holding the map.
		m_indexBuffer=NEW_REF(DX8IndexBufferClass,(VERTEX_BUFFER_TILE_LENGTH*VERTEX_BUFFER_TILE_LENGTH*2*3));

		// Fill up the IB
		DX8IndexBufferClass::WriteLockClass lockIdxBuffer(m_indexBuffer);
		UnsignedShort *ib=lockIdxBuffer.Get_Index_Array();

		// a failed lock leaves nothing to write the indices into
		for (j=0; ib != NULL && j<(VERTEX_BUFFER_TILE_LENGTH*VERTEX_BUFFER_TILE_LENGTH*4); j+=VERTEX_BUFFER_TILE_LENGTH*4)
		{
			for (i=j; i<(j+VERTEX_BUFFER_TILE_LENGTH*4); i+=4)	//4 vertices per 2x2 block
			{
				ib[0]=i;
				ib[1]=i+2;
				ib[2]=i+3;

				ib[3]=i;
				ib[4]=i+1;
				ib[5]=i+2;

				ib+=6;	//skip the 6 indices we just filled
			}
		}

		//Get number of vertex buffers needed to hold current map
		//First round dimensions to next multiple of VERTEX_BUFFER_TILE_LENGTH since that's our
		//blocksize
		m_numVBTilesX=1;
		for (i=VERTEX_BUFFER_TILE_LENGTH+1; i<x;)
		{	i+=VERTEX_BUFFER_TILE_LENGTH;
			m_numVBTilesX++;
		}
		m_numVBTilesY=1;
		for (j=VERTEX_BUFFER_TILE_LENGTH+1; j<y;)
		{	j+=VERTEX_BUFFER_TILE_LENGTH;
			m_numVBTilesY++;
		}

		m_numBlockColumnsInLastVB=(x-1)%VERTEX_BUFFER_TILE_LENGTH;	//right border within last VB
		m_numBlockRowsInLastVB=(y-1)%VERTEX_BUFFER_TILE_LENGTH;	//bottom border within last VB

		m_numVertexBufferTiles=m_numVBTilesX*m_numVBTilesY;
		m_x=x;
		m_y=y;
		m_vertexBufferTiles = NEW DX8VertexBufferClass*[m_numVertexBufferTiles];
		m_vertexBufferBackup = NEW char *[m_numVertexBufferTiles];

		Int numVertex = VERTEX_BUFFER_TILE_LENGTH*2*VERTEX_BUFFER_TILE_LENGTH*2;

		for (i=0; i<m_numVertexBufferTiles; i++) {
#ifdef USE_NORMALS	 
			m_vertexBufferTiles[i]=NEW_REF(DX8VertexBufferClass,(DX8_FVF_XYZNUV2,numVertex,DX8VertexBufferClass::USAGE_DEFAULT));
#else
			m_vertexBufferTiles[i]=NEW_REF(DX8VertexBufferClass,(DX8_VERTEX_FORMAT,numVertex,DX8VertexBufferClass::USAGE_DEFAULT));
#endif
			m_vertexBufferBackup[i] = NEW char[numVertex*sizeof(VERTEX_FORMAT)];
		} 

		//go with a preset material for now.
	}

	updateBlock(0,0,x-1,y-1,pMap,pLightsIterator);

	return 0;
}


//=============================================================================
// HeightMapRenderObjClass::On_Frame_Update
//=============================================================================
/** Updates the diffuse color values in the vertices as affected by the dynamic lights.*/
//=============================================================================
void HeightMapRenderObjClass::On_Frame_Update(void)
{	
	BaseHeightMapRenderObjClass::On_Frame_Update();
	Int i,j,k;
	DX8VertexBufferClass	**pVB;
	Int originX,originY;
	if (Scene==NULL) return;
	RTS3DScene *pMyScene = (RTS3DScene *)Scene;


	RefRenderObjListIterator pDynamicLightsIterator(pMyScene->getDynamicLights());
	if (m_map == NULL) {
		return;
	}

#ifdef DO_UNIT_TIMINGS  		  
#pragma MESSAGE("*** WARNING *** DOING DO_UNIT_TIMINGS!!!!")
	return;
#endif

#ifdef EXTENDED_STATS
	if (DX8Wrapper::stats.m_disableTerrain) {
		return;
	}
#endif

	Int numDynaLights=0;
	W3DDynamicLight *enabledLights[MAX_ENABLED_DYNAMIC_LIGHTS];

	// A light's min/max come from its world position over MAP_XY_FACTOR, which is the map's
	// non-playable border already subtracted.  This rectangle used to be built without doing the
	// same, so it sat a whole border away from the lights it was testing: a light within that
	// distance of the low edge was dropped and never lit anything, and one that far past the high
	// edge was processed for nothing.  The per-tile tests further down (getYWithOrigin(...) -
	// getBorderSizeInline()) always had it right, which is what the two disagreed about.
	const Int xCoordMin = m_map->getDrawOrgX() - m_map->getBorderSizeInline();
	const Int yCoordMin = m_map->getDrawOrgY() - m_map->getBorderSizeInline();
	const Int xCoordMax = xCoordMin + m_map->getDrawWidth();
	const Int yCoordMax = yCoordMin + m_map->getDrawHeight();

	// On the Direct3D 11 frame every pixel of the ground takes these lights itself
	// (BLAST_LIGHT_SAMPLING, RTS3DScene's handBlastLights); relit here as well it took them twice, in
	// ten-unit steps.  A light lit into the tiles at the moment that turns on stays there until the
	// tiles are next rebuilt; it only turns on with the shadow map, at the match's first frame or
	// from the options.
	const Bool lightsPerPixel = Direct3D11_Lights_Per_Pixel();

	for (pDynamicLightsIterator.First(); !pDynamicLightsIterator.Is_Done(); pDynamicLightsIterator.Next())
	{
		W3DDynamicLight *pLight = (W3DDynamicLight*)pDynamicLightsIterator.Peek_Obj();
		pLight->m_processMe = false;
		if (lightsPerPixel) {
			pLight->m_priorEnable = false;
			continue;
		}
		if (pLight->m_enabled || pLight->m_priorEnable) {
			Real range = pLight->Get_Attenuation_Range();
			if (pLight->m_priorEnable) {
				pLight->m_prevMinX = pLight->m_minX;
				pLight->m_prevMinY = pLight->m_minY;
				pLight->m_prevMaxX = pLight->m_maxX;
				pLight->m_prevMaxY = pLight->m_maxY;
			} 
			Vector3	pos = pLight->Get_Position();
			pLight->m_minX = (pos.X-range)/MAP_XY_FACTOR;
			pLight->m_maxX = (pos.X+range)/MAP_XY_FACTOR+1.0f;
			pLight->m_minY = (pos.Y-range)/MAP_XY_FACTOR;
			pLight->m_maxY = (pos.Y+range)/MAP_XY_FACTOR+1.0f;
			if (!pLight->m_priorEnable) {
				pLight->m_prevMinX = pLight->m_minX;
				pLight->m_prevMinY = pLight->m_minY;
				pLight->m_prevMaxX = pLight->m_maxX;
				pLight->m_prevMaxY = pLight->m_maxY;
			} 

			if (pLight->m_minX < xCoordMax && 
					pLight->m_minY < yCoordMax &&
					pLight->m_maxX > xCoordMin && 
					pLight->m_maxY > yCoordMin) {
				pLight->m_processMe = TRUE;
			} else if (pLight->m_prevMinX < xCoordMax && 
					pLight->m_prevMinY < yCoordMax &&
					pLight->m_prevMaxX > xCoordMin && 
					pLight->m_prevMaxY > yCoordMin) {
				pLight->m_processMe = TRUE;
			} else {
				pLight->m_processMe = false;
			}
			if (pLight->m_processMe) {
				enabledLights[numDynaLights] = pLight;
				numDynaLights++;
				if (numDynaLights == MAX_ENABLED_DYNAMIC_LIGHTS) {
					break;
				}
			}
		}
		pLight->m_priorEnable = pLight->m_enabled;
	}
	if (numDynaLights > 0) {
		DynamicLightPass pass;
		memset((void *)&pass, 0, sizeof(pass));
		pass.map = m_map;
		pass.drawOrgX = m_map->getDrawOrgX();
		pass.drawOrgY = m_map->getDrawOrgY();
		pass.originX = m_originX;
		pass.originY = m_originY;
		pass.numLights = numDynaLights;
		for (k=0; k<numDynaLights; k++) {
			W3DDynamicLight *pLight = enabledLights[k];
			DynamicLightInputs &in = pass.lights[k];
			in.type = pLight->Get_Type();
			in.enabled = pLight->isEnabled();
			in.position = pLight->Get_Position();
			pLight->Get_Spot_Direction(in.spotDirection);
			pLight->Get_Diffuse(&in.diffuse);
			pLight->Get_Ambient(&in.ambient);
			pLight->Get_Far_Attenuation_Range(in.midRange, in.range);
			in.bounds[0] = pLight->m_minX;		in.bounds[1] = pLight->m_minY;
			in.bounds[2] = pLight->m_maxX;		in.bounds[3] = pLight->m_maxY;
			in.bounds[4] = pLight->m_prevMinX;	in.bounds[5] = pLight->m_prevMinY;
			in.bounds[6] = pLight->m_prevMaxX;	in.bounds[7] = pLight->m_prevMaxY;
		}
		// Same inputs as the last relight and no vertex buffer rewritten since: the tiles already
		// hold what this pass would write.  Ground that moved and is waiting for its rebuild, or a
		// full rebuild pending, changes the normals first, so those passes relight as before.
		if (m_lastRelightValid && !m_hasDirtyRegion && !m_needFullUpdate &&
				memcmp(&pass, &m_lastRelight, sizeof(pass)) == 0) {
			return;
		}
		memcpy((void *)&m_lastRelight, &pass, sizeof(pass));
		m_lastRelightValid = true;

		//step through each vertex buffer that needs updating
		for (j=0; j<m_numVBTilesY; j++)
		{
			originY=j*VERTEX_BUFFER_TILE_LENGTH;	//location of this VB on the large full-size heightmap
			Int yMin, yMax;
			yMin = originY;
			yMax = originY+VERTEX_BUFFER_TILE_LENGTH;
			Bool intersect = false;
			Int yCoordMin = getYWithOrigin(yMin)+m_map->getDrawOrgY()-m_map->getBorderSizeInline();
			Int yCoordMax = getYWithOrigin(yMax-1)+m_map->getDrawOrgY()+1-m_map->getBorderSizeInline();
			if (yCoordMax>yCoordMin) {
				// no wrap occurred.
				for (k=0; k<numDynaLights; k++) {
					if (enabledLights[k]->m_minY < yCoordMax && 
						enabledLights[k]->m_maxY > yCoordMin) {
						intersect = true;
						break;
					}
					if (enabledLights[k]->m_prevMinY < yCoordMax && 
						enabledLights[k]->m_prevMaxY > yCoordMin) {
						intersect = true;
						break;
					}
				}
			} else {
				// wrap occurred, so we are outside of this range.
				int tmp=yCoordMin;
				yCoordMin = yCoordMax;
				yCoordMax = tmp;
				for (k=0; k<numDynaLights; k++) {
					if (enabledLights[k]->m_minY <=  yCoordMin || 
						enabledLights[k]->m_maxY >= yCoordMax) {
						intersect = true;
						break;
					}
					if (enabledLights[k]->m_prevMinY <=  yCoordMin || 
						enabledLights[k]->m_prevMaxY >= yCoordMax) {
						intersect = true;
						break;
					}
				}
			}
			if (!intersect) {
				continue;
			}

			for (i=0; i<m_numVBTilesX; i++)
			{	
				originX=i*VERTEX_BUFFER_TILE_LENGTH;	//location of this VB on the large full-size heightmap
				Int xMin, xMax;
				xMin = originX;
				xMax = originX+VERTEX_BUFFER_TILE_LENGTH;

				Bool intersect = false;
				Int xCoordMin = getXWithOrigin(xMin)+m_map->getDrawOrgX()-m_map->getBorderSizeInline();
				Int xCoordMax = getXWithOrigin(xMax-1)+m_map->getDrawOrgX()+1-m_map->getBorderSizeInline();
				if (xCoordMax>xCoordMin) {
					// no wrap occurred.
					for (k=0; k<numDynaLights; k++) {
						if (enabledLights[k]->m_minX < xCoordMax && 
							enabledLights[k]->m_maxX > xCoordMin) {
							intersect = true;
							break;
						}
						if (enabledLights[k]->m_prevMinX < xCoordMax && 
							enabledLights[k]->m_prevMaxX > xCoordMin) {
							intersect = true;
							break;
						}
					}
				} else {
					// wrap occurred, so we are outside of this range.
					int tmp=xCoordMin;
					xCoordMin = xCoordMax;
					xCoordMax = tmp;
					for (k=0; k<numDynaLights; k++) {
						if (enabledLights[k]->m_minX <=  xCoordMin || 
							enabledLights[k]->m_maxX >= xCoordMax) {
							intersect = true;
							break;
						}
						if (enabledLights[k]->m_prevMinX <=  xCoordMin ||
							enabledLights[k]->m_prevMaxX >= xCoordMax) {
							intersect = true;
							break;
						}
					}
				}
				if (!intersect) {
					continue;
				}
				pVB=m_vertexBufferTiles+j*m_numVBTilesX+i;	//point to correct row/column of vertex buffers 
				char **pData = m_vertexBufferBackup+j*m_numVBTilesX+i;
				updateVBForLight(*pVB, *pData, xMin, yMin, xMax, yMax, originX,originY, enabledLights, numDynaLights);
			}
		}
	}
}

//=============================================================================
// HeightMapRenderObjClass::staticLightingChanged
//=============================================================================
/** Notification that all lighting needs to be recalculated. */
//=============================================================================
void HeightMapRenderObjClass::staticLightingChanged( void )
{
	BaseHeightMapRenderObjClass::staticLightingChanged();
}

#define CENTER_LIMIT 2
#define BIG_JUMP 16
#define WIDE_STEP 32

static Int visMinX, visMinY, visMaxX, visMaxY;
static Bool check(const FrustumClass & frustum, WorldHeightMap *pMap, Int x, Int y) 
{
	if (x<0 || y<0) return(false);
	if (x>= pMap->getXExtent() || y>= pMap->getYExtent()) return(false);
	if (x >= visMinX && y >= visMinY && x <=visMaxX && y <= visMaxY) {
		return(true);
	}
	Int height = pMap->getHeight(x, y);
	Vector3 loc((x-pMap->getBorderSizeInline())*MAP_XY_FACTOR, (y-pMap->getBorderSizeInline())*MAP_XY_FACTOR, height*MAP_HEIGHT_SCALE);
	if (CollisionMath::Overlap_Test(frustum,loc) == CollisionMath::INSIDE) {
		if (x<visMinX) visMinX=x;
		if (x>visMaxX) visMaxX=x;
		if (y<visMinY) visMinY=y;
		if (y>visMaxY) visMaxY=y;
		return(true);
	}
	return(false);
}

static void calcVis(const FrustumClass & frustum, WorldHeightMap *pMap, Int minX, Int minY, Int maxX, Int maxY, Int limit)
{
	if (maxX-minX<2) return;
	if (maxY-minY<2) return;
	if (minX >=visMinX && minY >= visMinY && maxX <=visMaxX && maxY <= visMaxY) {
		return;
	}
	Int midX = (minX+maxX)/2;
	Int midY = (minY+maxY)/2;
	Bool recurse1 = maxX-minX>=limit;
	Bool recurse2 = recurse1;
	Bool recurse3 = recurse1;
	Bool recurse4 = recurse1;	 
	/* boxes are:

			1     2


			3			4 */

	if (check(frustum, pMap, midX, maxY)) {
		recurse1=true;
		recurse2=true;
	}
	if (check(frustum, pMap, midX, minY)) {
		recurse3=true;
		recurse4=true;
	}
	if (check(frustum, pMap, midX, midY)) {
		recurse1=true;
		recurse2=true;
		recurse3=true;
		recurse4=true;
	}
	if (check(frustum, pMap, minX, midY)) {
		recurse1=true;
		recurse3=true;
	}
	if (check(frustum, pMap, maxX, midY)) {
		recurse2=true;
		recurse4=true;
	}
	if (recurse1) {
		calcVis(frustum, pMap, minX, midY, midX, maxY, limit);
	}
	if (recurse2) {
		calcVis(frustum, pMap, midX, midY, maxX, maxY, limit);
	}
	if (recurse3) {
		calcVis(frustum, pMap, minX, minY, midX, midY, limit);
	}
	if (recurse4) {
		calcVis(frustum, pMap, midX, minY, maxX, midY, limit);
	}
}





//=============================================================================
// HeightMapRenderObjClass::updateCenter
//=============================================================================
/** Updates the positioning of the drawn portion of the height map in the 
heightmap.  As the view slides around, this determines what is the actually
rendered portion of the terrain.  Only a 96x96 section is rendered at any time, 
even though maps can be up to 1024x1024.  This function determines which subset
is rendered. */
//=============================================================================
void HeightMapRenderObjClass::updateCenter(CameraClass *camera , RefRenderObjListIterator *pLightsIterator)
{
	if (m_map==NULL) {
		return;
	}
	if (m_updating) {
		return;
	}
	if (m_vertexBufferTiles ==NULL)
		return;		//did not initialize resources yet.

	BaseHeightMapRenderObjClass::updateCenter(camera, pLightsIterator);

	m_updating = true;
	if (m_needFullUpdate)
  {
		m_needFullUpdate = false;
		/* Rebuilding and re-lighting every rendered vertex of the terrain.  This is tens of
			 milliseconds - a visible stutter - and something as small as one grid cell being lowered
			 by a building foundation asks for it (W3DTerrainVisual::setRawMapHeight).  Logged with
			 its cost so a stutter report can be tied to it instead of guessed at. */
		const UnsignedInt fullUpdateStart = Clock_Milliseconds();
		updateBlock(0, 0, m_x-1, m_y-1, m_map, pLightsIterator);
		DEBUG_LOG(("TERRAIN FULL UPDATE: %d ms at frame %d\n",
							 (Int)(Clock_Milliseconds() - fullUpdateStart),
							 TheGameLogic ? TheGameLogic->getFrame() : 0));
		m_updating = false;
		return;
	}

	if (m_x >= m_map->getXExtent() && m_y >= m_map->getYExtent()) 
  {
		m_updating = false;
		return; // no need to center. 
	}

	Int cellOffset = 1;
	if (HALF_RES_MESH) {
		cellOffset = 2;
	}
	// determine the ray corresponding to the camera and distance to projection plane
	Matrix3D camera_matrix = camera->Get_Transform();
	
	Vector3 camera_location  = camera->Get_Position();

	Vector3 rayLocation;
	Vector3 rayDirection;
	Vector3 rayDirectionPt;
	// the projected ray has the same origin as the camera
	rayLocation = camera_location; 
	// determine the location of the screen coordinate in camera-model space
	const ViewportClass &viewport = camera->Get_Viewport();
	Int i, j, minHt;

	Real intersectionZ;
	minHt = m_map->getMaxHeightValue();
	// row major: getDisplayHeight indexes x + width*y, so y belongs on the outside.  The answer is
	// a minimum and does not care about the order; the cache does.
	for (j=0; j<m_y; j++) {
		for (i=0; i<m_x; i++) {
			Short cur = m_map->getDisplayHeight(i,j);
			if (cur<minHt) minHt = cur;
		}
	}
	intersectionZ = (float)minHt;
//	float aspect = camera->Get_Aspect_Ratio();

	Vector2 min,max;
	camera->Get_View_Plane(min,max);
	float xscale = (max.X - min.X);
	float yscale = (max.Y - min.Y);

	float zmod = -1.0; // Scene->vpd; // Note: view plane distance is now always 1.0 from the camera
	float minX = 200000;
	float maxX = -minX;
	float minY = 200000;
	float maxY = -minY;
	for (i=0; i<2; i++) {
		for (j=0; j<2; j++) {
			float xmod = (-i + 0.5 + viewport.Min.X) * zmod * xscale;// / aspect;
			float ymod = (j - 0.5 - viewport.Min.Y) * zmod * yscale;// * aspect;

			// transform the screen coordinates by the camera's matrix into world coordinates.
			float x = zmod * camera_matrix[0][2] + xmod * camera_matrix[0][0] + ymod * camera_matrix[0][1];
			float y = zmod * camera_matrix[1][2] + xmod * camera_matrix[1][0] + ymod * camera_matrix[1][1];
			float z = zmod * camera_matrix[2][2] + xmod * camera_matrix[2][0] + ymod * camera_matrix[2][1];

			rayDirection.Set(x,y,z);
			rayDirection.Normalize();
			rayDirectionPt = rayLocation+rayDirection;

			x = Vector3::Find_X_At_Z(intersectionZ, rayLocation, rayDirectionPt);
			y = Vector3::Find_Y_At_Z(intersectionZ, rayLocation, rayDirectionPt);
			if (x<minX) minX = x;
			if (x>maxX) maxX = x;
			if (y<minY) minY = y;
			if (y>maxY) maxY = y;
		}
	}

	// convert back to cell indexes.
	minX /= MAP_XY_FACTOR;
	maxX /= MAP_XY_FACTOR;
	minY /= MAP_XY_FACTOR;
	maxY /= MAP_XY_FACTOR;

	minX += m_map->getBorderSizeInline();
	maxX += m_map->getBorderSizeInline();
	minY += m_map->getBorderSizeInline();
	maxY += m_map->getBorderSizeInline();

	visMinX = m_map->getXExtent();
	visMinY = m_map->getYExtent();
	visMaxX = 0;
	visMaxY = 0;

	///< @todo find out why values go out of range
	if (minX<0) minX=0;
	if (minY<0) minY=0;
	if (maxX > visMinX) maxX = visMinX;
	if (maxY > visMinY) maxY = visMinY;

	const FrustumClass & frustum = camera->Get_Frustum();
	Int limit = (maxX-minX)/2;
	if (limit > WIDE_STEP/2) {
		limit=WIDE_STEP/2;
	}
	calcVis(frustum, m_map, minX-WIDE_STEP/2, minY-WIDE_STEP/2, maxX+WIDE_STEP/2, maxY+WIDE_STEP/2, limit);

	if (m_map) {
		Int newOrgX;
		if (visMaxX-visMinX > m_x) {
			newOrgX = (maxX+minX)/2-m_x/2.0;	
		} else {
			newOrgX = (visMaxX+visMinX)/2-m_x/2.0;
		}

		Int newOrgY;
		if (visMaxY - visMinY > m_y) {
			newOrgY = visMinY+1;	
		}	else {
			newOrgY = (visMaxY+visMinY)/2-m_y/2.0;
		}																															  
		if (TheTacticalView->getFieldOfView() != 0) {
			newOrgX = (visMaxX+visMinX)/2-m_x/2.0;
			newOrgY = (visMaxY+visMinY)/2-m_y/2.0;
		}
		if (HALF_RES_MESH) {
			newOrgX &= 0xFFFFFFFE;
			newOrgY &= 0xFFFFFFFE;
		}
		Int deltaX = newOrgX - m_map->getDrawOrgX();
		Int deltaY = newOrgY - m_map->getDrawOrgY();
		if (IABS(deltaX) > m_x/2 || IABS(deltaY)>m_x/2) {
			m_map->setDrawOrg(newOrgX, newOrgY);
			m_originY = 0;
			m_originX = 0;
			updateBlock(0, 0, m_x-1, m_y-1, m_map, pLightsIterator); 
			m_updating = false;
			return;
		}

		if (abs(deltaX)>CENTER_LIMIT || abs(deltaY)>CENTER_LIMIT) {
			// The row test was >= where the guard above and the column test below are both >, so a
			// sideways scroll of exactly CENTER_LIMIT rows also rebuilt the rows - and then took
			// the early return below and left the columns for the next frame.
			if (abs(deltaY) > CENTER_LIMIT) {
				if (m_map->setDrawOrg(m_map->getDrawOrgX(), newOrgY)) {
					Int minY = 0;
					Int maxY = 0;
					deltaY -= newOrgY - m_map->getDrawOrgY(); 
					m_originY += deltaY;
					if (m_originY >= m_y-1) m_originY -= m_y-1;
					if (deltaY<0) {
						minY = m_originY;
						maxY = m_originY-deltaY;
					} else {
						minY = m_originY - deltaY;
						maxY = m_originY;
					}
					minY-=cellOffset;
					if (m_originY < 0) m_originY += m_y-1;
					if (minY<0) {
						minY += m_y-1;
						if (minY<0) minY = 0;
						updateBlock(0, minY, m_x-1, m_y-1, m_map, pLightsIterator);
						updateBlock(0, 0, m_x-1, maxY, m_map, pLightsIterator);
					} else {
						updateBlock(0, minY, m_x-1, maxY, m_map, pLightsIterator);
					}
				}
				// It is much more efficient to update a cople of columns one frame, and then
				// a couple of rows.  So if we aren't "jumping" to a new view, and have done X
				// recently, return.
				if (abs(deltaX) < BIG_JUMP && !m_doXNextTime) {
					m_updating = false;
					m_doXNextTime = true;
					return;	// Only do the y this frame.  Do x next frame.  jba.
				}
			}
			if (abs(deltaX) > CENTER_LIMIT) {
				m_doXNextTime = false;
				newOrgX = m_map->getDrawOrgX() + deltaX;
				if (m_map->setDrawOrg(newOrgX, m_map->getDrawOrgY())) {
					Int minX = 0;
					Int maxX = 0;
					deltaX -= newOrgX - m_map->getDrawOrgX(); 
					m_originX += deltaX;
					if (m_originX >= m_x-1) m_originX -= m_x-1;
					if (deltaX<0) {
						minX = m_originX;
						maxX = m_originX-deltaX;
					} else {
						minX = m_originX - deltaX;
						maxX = m_originX;
					}
					minX-=cellOffset;
					maxX+=cellOffset;
					if (m_originX < 0) m_originX += m_x-1;
					if (minX<0) {
						minX += m_x-1;
						if (minX<0) minX = 0;
						updateBlock(minX,0,m_x-1, m_y-1, m_map, pLightsIterator);
						updateBlock(0,0,maxX, m_y-1, m_map, pLightsIterator);
					} else {
						updateBlock(minX,0,maxX, m_y-1, m_map, pLightsIterator);
					}
				}
			} 
		}
	}
	m_updating = false;
}

//=============================================================================
// HeightMapRenderObjClass::Render
//=============================================================================
/** Renders (draws) the terrain. */
//=============================================================================
DECLARE_PERF_TIMER(Terrain_Render)

void HeightMapRenderObjClass::Render(RenderInfoClass & rinfo)
{
	USE_PERF_TIMER(Terrain_Render)
	
	Int i,j,devicePasses;
	W3DShaderManager::ShaderTypes st;
	Bool doCloud = TheGlobalData->m_useCloudMap;

	Matrix3D tm(Transform);
#if 0 // There is some weirdness sometimes with the dx8 static buffers.
			// This usually fixes terrain flashing.  jba.
	static Int delay = 1;
	delay --;
	if (delay<1) {
		delay = 1;
		static Int ndx = -1;
		ndx++;
		if (ndx>=m_numVertexBufferTiles) {
			ndx = 0;
		}
		DX8VertexBufferClass::WriteLockClass lockVtxBuffer(m_vertexBufferTiles[ndx]);
		VERTEX_FORMAT *vb = (VERTEX_FORMAT*)lockVtxBuffer.Get_Vertex_Array();	
		vb = 0;
	}
#endif
	// If there are trees, tell them to draw at the transparent time to draw.
	if (m_treeBuffer) {
		m_treeBuffer->setIsTerrain();
	}


#ifdef DO_UNIT_TIMINGS
#pragma MESSAGE("*** WARNING *** DOING DO_UNIT_TIMINGS!!!!")
	return;
#endif

#ifdef EXTENDED_STATS
	if (DX8Wrapper::stats.m_disableTerrain) {
		return;
	}
#endif

	DX8Wrapper::Set_Light_Environment(rinfo.light_environment);

	// Force shaders to update.
	m_stageTwoTexture->restore();
	DX8Wrapper::Set_Texture(0,NULL);
	DX8Wrapper::Set_Texture(1,NULL);
	ShaderClass::Invalidate();

	//	tm.Scale(ObjSpaceExtent);
	DX8Wrapper::Set_Transform(D3DTS_WORLD,tm);

	//Apply the shader and material

	DX8Wrapper::Set_Index_Buffer(m_indexBuffer,0);

	Bool doMultiPassWireFrame=FALSE;

	if (((RTS3DScene *)rinfo.Camera.Get_User_Data())->getCustomPassMode() == SCENE_PASS_ALPHA_MASK ||
		((SceneClass *)rinfo.Camera.Get_User_Data())->Get_Extra_Pass_Polygon_Mode() == SceneClass::EXTRA_PASS_CLEAR_LINE)
	{
			if (WW3D::Is_Texturing_Enabled())
			{	//first pass where we just fill the z-buffer

				devicePasses=1;	//one pass solid, next in wireframe.
				doMultiPassWireFrame=TRUE;

				if (rinfo.Additional_Pass_Count())
				{
					rinfo.Peek_Additional_Pass(0)->Install_Materials();
					renderTerrainPass(&rinfo.Camera);
					rinfo.Peek_Additional_Pass(0)->UnInstall_Materials();
					return;
				}
			}
			else
			{	//wireframe pass
				//Set to vertex diffuse lighting
				DX8Wrapper::Set_Material(m_vertexMaterialClass);
				//Set shader to non-textured solid color from vertex
				DX8Wrapper::Set_Shader(ShaderClass::_PresetOpaqueSolidShader);
				devicePasses=1;	//one pass solid, next in wireframe.
				DX8Wrapper::Apply_Render_State_Changes();
				DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_COLORARG2, D3DTA_TFACTOR );
				DX8Wrapper::Set_DX8_Render_State(D3DRS_TEXTUREFACTOR,0xff808080);
				doMultiPassWireFrame=TRUE;
				renderTerrainPass(&rinfo.Camera);
				DX8Wrapper::Set_DX8_Render_State(D3DRS_TEXTUREFACTOR,0xff008000);
				return;
			}
	}
	else
	{
		DX8Wrapper::Set_Material(m_vertexMaterialClass);
		DX8Wrapper::Set_Shader(m_shaderClass);

		if (TheGlobalData->m_timeOfDay == TIME_OF_DAY_NIGHT) {
			doCloud = false;
		}

 		st=W3DShaderManager::ST_TERRAIN_BASE; //set default shader
 		
 		//set correct shader based on current settings
 		if (!ShaderClass::Is_Backface_Culling_Inverted())
 		{	//not reflection pass
 			if (TheGlobalData->m_useLightMap && doCloud)
 			{	st=W3DShaderManager::ST_TERRAIN_BASE_NOISE12;
 			}
 			else
 			if (TheGlobalData->m_useLightMap)
 			{	//lightmap only
 				st=W3DShaderManager::ST_TERRAIN_BASE_NOISE2;
 			}
 			else
 			if (doCloud)
 			{	//cloudmap only
 				st=W3DShaderManager::ST_TERRAIN_BASE_NOISE1;
 			}
 		}
 		else
 		{	//reflection pass, just do base texture
 			st=W3DShaderManager::ST_TERRAIN_BASE;
 		}
 
 		//Find number of passes required to render current shader
 		devicePasses=W3DShaderManager::getShaderPasses(st);
 
 		if (m_disableTextures)
 			devicePasses=1;	//force to 1 lighting-only pass
 
 		//Specify all textures that this shader may need.
 		W3DShaderManager::setTexture(0,m_stageZeroTexture);
 		W3DShaderManager::setTexture(1,m_stageZeroTexture);
 		W3DShaderManager::setTexture(2,m_stageTwoTexture);	//cloud
 		W3DShaderManager::setTexture(3,m_stageThreeTexture);//noise
		//Disable writes to destination alpha channel (if there is one)
		if (DX8Wrapper::getBackBufferFormat() == WW3D_FORMAT_A8R8G8B8)
			DX8Wrapper::Set_DX8_Render_State(D3DRS_COLORWRITEENABLE,D3DCOLORWRITEENABLE_BLUE|D3DCOLORWRITEENABLE_GREEN|D3DCOLORWRITEENABLE_RED);
	}

	Int pass;
 	for (pass=0; pass<devicePasses && !ShaderClass::Is_Backface_Culling_Inverted(); pass++) {	//a water mirror leaves the ground out, see the shorelines below
#ifdef TIMING_TESTS
#endif
		if (!doMultiPassWireFrame)	//multi-pass wireframe doesn't use regular shaders.
		{
 			if (m_disableTextures ) {
 				DX8Wrapper::Set_Shader(ShaderClass::_PresetOpaque2DShader);
 				DX8Wrapper::Set_Texture(0,NULL);
   			} else {
 				W3DShaderManager::setShader(st, pass);
			}
		}

		for (j=0; j<m_numVBTilesY; j++)
			for (i=0; i<m_numVBTilesX; i++)
			{
				static int count = 0;
				count++;
				Int numPolys = VERTEX_BUFFER_TILE_LENGTH*VERTEX_BUFFER_TILE_LENGTH*2;
				Int numVertex = (VERTEX_BUFFER_TILE_LENGTH*2)*(VERTEX_BUFFER_TILE_LENGTH*2);
				if (HALF_RES_MESH) {
					numPolys /= 4;
					numVertex /= 4;
				}
				DX8Wrapper::Set_Vertex_Buffer(m_vertexBufferTiles[j*m_numVBTilesX+i]);
#ifdef PRE_TRANSFORM_VERTEX
				if (m_xformedVertexBuffer && pass==0) {
					// Note - m_xformedVertexBuffer should only be used for non T&L hardware.  jba.
					DX8Wrapper::Apply_Render_State_Changes();
					int code = DX8Wrapper::_Get_D3D_Device()->ProcessVertices(0, 0, numVertex, m_xformedVertexBuffer[j*m_numVBTilesX+i], 0); 
					::OutputDebugString("did process vertex\n");
				}
				if (m_xformedVertexBuffer) {
					// Note - m_xformedVertexBuffer should only be used for non T&L hardware.  jba.
					DX8Wrapper::Apply_Render_State_Changes();
					DX8Wrapper::_Get_D3D_Device()->SetStreamSource(
						0,
						m_xformedVertexBuffer[j*m_numVBTilesX+i],
						0,
						Get_FVF_Vertex_Size(D3DFVF_XYZRHW |D3DFVF_DIFFUSE|D3DFVF_TEX2));
					DX8Wrapper::_Get_D3D_Device()->SetFVF(D3DFVF_XYZRHW |D3DFVF_DIFFUSE|D3DFVF_TEX2);
				}
#endif				
				if (Is_Hidden() == 0) {
					DX8Wrapper::Draw_Triangles(	0,numPolys, 0,	numVertex);
				}

			}
	}		

	if (!doMultiPassWireFrame)
	{
		if (pass)	//shader was applied at least once?
 			W3DShaderManager::resetShader(st);

		// A water mirror leaves the ground out, and everything drawn onto it.  The reflection darkens
		// water wherever anything stands in the mirror, and the banks would lay a dark band along
		// every shore; the ground was also most of what the mirror drew.
		if (!ShaderClass::Is_Backface_Culling_Inverted())
		{
			//Draw feathered shorelines
			renderShoreLines(&rinfo.Camera);

			//Do additional pass over any tiles that have 3 textures blended together.
			if (TheGlobalData->m_use3WayTerrainBlends)
				renderExtraBlendTiles();
		}

		Int yCoordMin = m_map->getDrawOrgY();
		Int yCoordMax = m_y+m_map->getDrawOrgY()-1;
		Int xCoordMin = m_map->getDrawOrgX();
		Int xCoordMax = m_x+m_map->getDrawOrgX()-1;	
	#ifdef TEST_CUSTOM_EDGING
		// Draw edging just before last pass.
		DX8Wrapper::Set_Texture(0,NULL);
		DX8Wrapper::Set_Texture(1,NULL);
		m_stageTwoTexture->restore();
		m_customEdging->drawEdging(m_map, xCoordMin, xCoordMax, yCoordMin, yCoordMax, 
			m_stageZeroTexture, doCloud?m_stageTwoTexture:NULL, TheGlobalData->m_useLightMap?m_stageThreeTexture:NULL);
	#endif
	#ifdef DO_ROADS
		DX8Wrapper::Set_Texture(0,NULL);
		DX8Wrapper::Set_Texture(1,NULL);
		m_stageTwoTexture->restore();

		ShaderClass::Invalidate();
		if (!ShaderClass::Is_Backface_Culling_Inverted()) {
			DX8Wrapper::Set_Material(m_vertexMaterialClass);
			if (Scene) {
				RTS3DScene *pMyScene = (RTS3DScene *)Scene;
				RefRenderObjListIterator pDynamicLightsIterator(pMyScene->getDynamicLights());
				m_roadBuffer->drawRoads(&rinfo.Camera, doCloud?m_stageTwoTexture:NULL, TheGlobalData->m_useLightMap?m_stageThreeTexture:NULL,
					m_disableTextures,xCoordMin-m_map->getBorderSizeInline(), xCoordMax-m_map->getBorderSizeInline(), yCoordMin-m_map->getBorderSizeInline(), yCoordMax-m_map->getBorderSizeInline(), &pDynamicLightsIterator);
			}
		}
	#endif
		// A defence's hittable ground goes on the bare ground and its roads, read back from their depth
		// on Direct3D 11: so before the props and the bridges, which stand on it, and before the shroud,
		// which darkens it with the ground.  Every model is drawn over it.  Not into a mirror.
		if (!ShaderClass::Is_Backface_Culling_Inverted())
			W3DReachOverlay_Draw(rinfo.Camera);

	if (m_propBuffer) {
		m_propBuffer->drawProps(rinfo);
	}
	#ifdef DO_SCORCH
		DX8Wrapper::Set_Texture(0,NULL);
		DX8Wrapper::Set_Texture(1,NULL);
		m_stageTwoTexture->restore();

		ShaderClass::Invalidate();
		if (!ShaderClass::Is_Backface_Culling_Inverted()) {
			drawScorches();
		}
	#endif
		DX8Wrapper::Set_Texture(0,NULL);
		DX8Wrapper::Set_Texture(1,NULL);
		m_stageTwoTexture->restore();
		ShaderClass::Invalidate();
		DX8Wrapper::Apply_Render_State_Changes();

		m_bridgeBuffer->drawBridges(&rinfo.Camera, m_disableTextures, doCloud?m_stageTwoTexture:NULL);

		if (TheTerrainTracksRenderObjClassSystem)
			TheTerrainTracksRenderObjClassSystem->flush();

		// The water a reflection lands on is shrouded already, and shrouding the ground inside the
		// mirror as well would draw the whole terrain a second time for no visible difference.
		if (m_shroud && rinfo.Additional_Pass_Count() && !ShaderClass::Is_Backface_Culling_Inverted())
		{
			rinfo.Peek_Additional_Pass(0)->Install_Materials();
			renderTerrainPass(&rinfo.Camera);
			rinfo.Peek_Additional_Pass(0)->UnInstall_Materials();
		}

		ShaderClass::Invalidate();
		DX8Wrapper::Apply_Render_State_Changes();
	}
	else
			m_bridgeBuffer->drawBridges(&rinfo.Camera, m_disableTextures, m_stageTwoTexture);

  if ( m_waypointBuffer ) 
	  m_waypointBuffer->drawWaypoints(rinfo);

	m_bibBuffer->renderBibs();

	// the build grid goes down with the ground it describes: drawn here it is depth tested against
	// the terrain and covered by everything drawn after it, so it reads as paint on the map rather
	// than as a wireframe floating over the scene
	if( TheInGameUI )
	{
		TheInGameUI->drawBuildGrid();

		// and the wash inside an attack circle, for the same reason: the units it is sweeping up
		// have to stand on top of it
		TheInGameUI->drawAttackCircleFill();
	}

	// We do some custom blending, so tell the shader class to reset everything.
	DX8Wrapper::Set_Texture(0,NULL);
	DX8Wrapper::Set_Texture(1,NULL);
	m_stageTwoTexture->restore();
	ShaderClass::Invalidate();
	DX8Wrapper::Set_Material(NULL);

}



///Performs additional terrain rendering pass, blending in the black shroud texture.
void HeightMapRenderObjClass::renderTerrainPass(CameraClass *pCamera)
{
	DX8Wrapper::Set_Transform(D3DTS_WORLD,Matrix3D(1));

	//Apply the shader and material
	
	DX8Wrapper::Set_Index_Buffer(m_indexBuffer,0);

	for (Int j=0; j<m_numVBTilesY; j++)
		for (Int i=0; i<m_numVBTilesX; i++)
		{
			static int count = 0;
			count++;
			Int numPolys = VERTEX_BUFFER_TILE_LENGTH*VERTEX_BUFFER_TILE_LENGTH*2;
			Int numVertex = (VERTEX_BUFFER_TILE_LENGTH*2)*(VERTEX_BUFFER_TILE_LENGTH*2);
			if (HALF_RES_MESH) {
				numPolys /= 4;
				numVertex /= 4;
			}
			DX8Wrapper::Set_Vertex_Buffer(m_vertexBufferTiles[j*m_numVBTilesX+i]);
#ifdef PRE_TRANSFORM_VERTEX
			if (m_xformedVertexBuffer && pass==0) {
				// Note - m_xformedVertexBuffer should only be used for non T&L hardware.  jba.
				DX8Wrapper::Apply_Render_State_Changes();
				int code = DX8Wrapper::_Get_D3D_Device()->ProcessVertices(0, 0, numVertex, m_xformedVertexBuffer[j*m_numVBTilesX+i], 0); 
				::OutputDebugString("did process vertex\n");
			}
			if (m_xformedVertexBuffer) {
				// Note - m_xformedVertexBuffer should only be used for non T&L hardware.  jba.
				DX8Wrapper::Apply_Render_State_Changes();
				DX8Wrapper::_Get_D3D_Device()->SetStreamSource(
					0,
					m_xformedVertexBuffer[j*m_numVBTilesX+i],
					Get_FVF_Vertex_Size(D3DFVF_XYZRHW |D3DFVF_DIFFUSE|D3DFVF_TEX2));
				DX8Wrapper::_Get_D3D_Device()->SetFVF(D3DFVF_XYZRHW |D3DFVF_DIFFUSE|D3DFVF_TEX2);
			}
#endif				
			if (Is_Hidden() == 0) {
				DX8Wrapper::Draw_Triangles(	0,numPolys, 0,	numVertex);
			}
		}
}

//=============================================================================
// HeightMapRenderObjClass::renderExtraBlendTiles
//=============================================================================
/** Renders an additoinal terrain pass including only those tiles which have more than 2 textures
blended together.  Used primarily for corner cases where 3 different textures meet.*/
void HeightMapRenderObjClass::renderExtraBlendTiles(void)
{
	Int vertexCount = 0;
	Int indexCount = 0;
	Int xExtent = m_map->getXExtent();
	Int border = m_map->getBorderSizeInline();
	static Int maxBlendTiles = DEFAULT_MAX_FRAME_EXTRABLEND_TILES;

	m_numVisibleExtraBlendTiles = 0;

	if (!m_numExtraBlendTiles)
		return;	//nothing to draw

	if (maxBlendTiles > 10000)	//we can only fit about 10000 tiles into a single VB.
		maxBlendTiles = 10000;

	//Loop over visible terrain and extract all the tiles that need extra blend
	Int drawEdgeY=m_map->getDrawOrgY()+m_map->getDrawHeight()-1;
	Int drawEdgeX=m_map->getDrawOrgX()+m_map->getDrawWidth()-1;
	if (drawEdgeX > (m_map->getXExtent()-1))
		drawEdgeX = m_map->getXExtent()-1;
	if (drawEdgeY > (m_map->getYExtent()-1))
		drawEdgeY = m_map->getYExtent()-1;
	Int drawStartX=m_map->getDrawOrgX();
	Int drawStartY=m_map->getDrawOrgY();

	// Everything the fill below reads, apart from the 3-way list, the heights and the lights, which
	// all move m_terrainContentVersion.  The lights are checked here, once a pass.
	refreshStaticDiffuseInputs();
	const Int key[EXTRA_BLEND_KEY_SIZE] = { m_terrainContentVersion, drawStartX, drawStartY,
		drawEdgeX, drawEdgeY, maxBlendTiles, m_map->getExtraBlendUVGeneration(),
		TheGlobalData->m_adjustCliffTextures };
	if (m_extraBlendMap != m_map || memcmp(key, m_extraBlendKey, sizeof(key)) != 0)
	{
		m_extraBlendMap = NULL;
		if (m_extraBlendCapacity != maxBlendTiles)
		{
			REF_PTR_RELEASE(m_extraBlendVB);
			REF_PTR_RELEASE(m_extraBlendIB);
			m_extraBlendVB = NEW_REF(DX8VertexBufferClass,(DX8_FVF_XYZNDUV2,maxBlendTiles*4,DX8VertexBufferClass::USAGE_DEFAULT));
			m_extraBlendIB = NEW_REF(DX8IndexBufferClass,(maxBlendTiles*6));
			m_extraBlendCapacity = maxBlendTiles;
		}

		DX8VertexBufferClass::WriteLockClass lock(m_extraBlendVB);
		VertexFormatXYZNDUV2* vb=(VertexFormatXYZNDUV2*)lock.Get_Vertex_Array();
		DX8IndexBufferClass::WriteLockClass lockib(m_extraBlendIB);
		UnsignedShort *ib=lockib.Get_Index_Array();

		if (!vb || !ib) return;

		const UnsignedByte* data = m_map->getDataPtr();

		try {
		for (Int j=0; j<m_numExtraBlendTiles; j++)
		{
			if (vertexCount >= (maxBlendTiles*4))
				break;	//no room in vertex buffer

			Real U[4],V[4];
			UnsignedByte alpha[4];
			Bool flipState,cliffState;
			Int x = m_extraBlendTilePositions[j] & 0xffff;
			Int y = m_extraBlendTilePositions[j] >> 16;

			if (x >= drawStartX && x < drawEdgeX &&
				y >= drawStartY && y < drawEdgeY &&
				m_map->getExtraAlphaUVData(x,y,U,V,alpha,&flipState, &cliffState))
			{	//this tile is inside visible region and has 3rd blend layer.

				Int idx = x+y*xExtent;

				Real p0=data[idx]*MAP_HEIGHT_SCALE;
				Real p1=data[idx+1]*MAP_HEIGHT_SCALE;
				Real p2=data[idx + 1 + xExtent]*MAP_HEIGHT_SCALE;
				Real p3=data[idx + xExtent]*MAP_HEIGHT_SCALE;
				if (cliffState && abs(p0-p2) > abs(p1-p3))	//cliffs sometimes force a flip
					flipState = TRUE;

				vb->x=(x-border)*MAP_XY_FACTOR;	 
				vb->y=(y-border)*MAP_XY_FACTOR;
				vb->z=p0;
				vb->nx=0;
				vb->ny=0;
				vb->nz=0;
				vb->diffuse=(alpha[0]<<24)|(getStaticDiffuse(x,y) & 0x00ffffff);
				vb->u1=U[0];
				vb->v1=V[0];
				vb->u2=0;
				vb->v2=0;
				vb++;

				vb->x=(x+1-border)*MAP_XY_FACTOR;	 
				vb->y=(y-border)*MAP_XY_FACTOR;
				vb->z=p1;
				vb->nx=0;
				vb->ny=0;
				vb->nz=0;
				vb->diffuse=(alpha[1]<<24)|(getStaticDiffuse(x+1,y) & 0x00ffffff);
				vb->u1=U[1];
				vb->v1=V[1];
				vb->u2=0;
				vb->v2=0;
				vb++;

				vb->x=(x+1-border)*MAP_XY_FACTOR;	 
				vb->y=(y+1-border)*MAP_XY_FACTOR;
				vb->z=p2;
				vb->nx=0;
				vb->ny=0;
				vb->nz=0;
				vb->diffuse=(alpha[2]<<24)|(getStaticDiffuse(x+1,y+1) & 0x00ffffff);
				vb->u1=U[2];
				vb->v1=V[2];
				vb->u2=0;
				vb->v2=0;
				vb++;

				vb->x=(x-border)*MAP_XY_FACTOR;	 
				vb->y=(y+1-border)*MAP_XY_FACTOR;
				vb->z=p3;
				vb->nx=0;
				vb->ny=0;
				vb->nz=0;
				vb->diffuse=(alpha[3]<<24)|(getStaticDiffuse(x,y+1) & 0x00ffffff);
				vb->u1=U[3];
				vb->v1=V[3];
				vb->u2=0;
				vb->v2=0;
				vb++;
				
				if (flipState)
				{
					ib[0]=1+vertexCount;
					ib[1]=3+vertexCount;
					ib[2]=0+vertexCount;
					ib[3]=1+vertexCount;
					ib[4]=2+vertexCount;
					ib[5]=3+vertexCount;
				}
				else
				{
					ib[0]=0+vertexCount;
					ib[1]=2+vertexCount;
					ib[2]=3+vertexCount;
					ib[3]=0+vertexCount;
					ib[4]=1+vertexCount;
					ib[5]=2+vertexCount;
				}
				ib += 6;
				vertexCount +=4;
				indexCount +=6;
			}//tile has 3rd blend layer and is visible
		}	//for all extre blend tiles
		IndexBufferExceptionFunc();
		} catch(...) {
			IndexBufferExceptionFunc();
		}

		memcpy(m_extraBlendKey, key, sizeof(key));
		m_extraBlendMap = m_map;
		m_extraBlendVertexCount = vertexCount;
		m_extraBlendIndexCount = indexCount;
	}//unlock vertex buffer
	vertexCount = m_extraBlendVertexCount;
	indexCount = m_extraBlendIndexCount;

	if (vertexCount)
	{
		//Check if we couldn't fit all blend tiles into vertex buffer so we can enlarge it for next frame.
		if (vertexCount == (maxBlendTiles*4))
			maxBlendTiles += 16;	//enlarge by 16 to reduce trashing.

		ShaderClass::Invalidate();	//invalidate to force shader to reset since we directly changed states
		DX8Wrapper::Set_Index_Buffer(m_extraBlendIB,0);
		DX8Wrapper::Set_Vertex_Buffer(m_extraBlendVB);
		VertexMaterialClass *vmat=VertexMaterialClass::Get_Preset(VertexMaterialClass::PRELIT_DIFFUSE);
		DX8Wrapper::Set_Material(vmat);
		REF_PTR_RELEASE(vmat);
		ShaderClass shader=ShaderClass::_PresetOpaqueShader;
		shader.Set_Depth_Mask(ShaderClass::DEPTH_WRITE_DISABLE);	//disable writes to z
		DX8Wrapper::Set_Shader(shader);

		if (TheGlobalData->m_use3WayTerrainBlends == 2)
		{
			shader.Set_Primary_Gradient(ShaderClass::GRADIENT_DISABLE);	//disable lighting.
			shader.Set_Texturing(ShaderClass::TEXTURING_DISABLE);		//disable texturing.
			DX8Wrapper::Set_Shader(shader);
			DX8Wrapper::Set_Texture(0,NULL);	//debug mode which draws terrain tiles in white.
			if (Is_Hidden() == 0) {
				DX8Wrapper::Draw_Triangles(	0,indexCount/3, 0,	vertexCount);	//draw a quad, 2 triangles, 4 verts
				m_numVisibleExtraBlendTiles += indexCount/6;
			}
		}
		else
		{
			W3DShaderManager::setTexture(0,m_stageOneTexture);
			W3DShaderManager::setTexture(1,m_stageTwoTexture);	//cloud
			W3DShaderManager::setTexture(2,m_stageThreeTexture);	//noise/lightmap

			W3DShaderManager::ShaderTypes st = W3DShaderManager::ST_ROAD_BASE;

			Bool doCloud = TheGlobalData->m_useCloudMap;
			if (TheGlobalData->m_timeOfDay == TIME_OF_DAY_NIGHT) {
				doCloud = false;
			}

			if (TheGlobalData->m_useLightMap && doCloud)
 			{	
				st = W3DShaderManager::ST_ROAD_BASE_NOISE12;
 			}
 			else if (TheGlobalData->m_useLightMap)
 			{	//lightmap only
 				st = W3DShaderManager::ST_ROAD_BASE_NOISE2;
 			}
 			else if (doCloud)
 			{	//cloudmap only
 				st = W3DShaderManager::ST_ROAD_BASE_NOISE1;
 			}

			Int devicePasses=W3DShaderManager::getShaderPasses(st);

			for (Int pass=0; pass < devicePasses; pass++)
			{
				W3DShaderManager::setShader(st, pass);
				if (Is_Hidden() == 0) {
					DX8Wrapper::Draw_Triangles(	0,indexCount/3, 0,	vertexCount);	//draw a quad, 2 triangles, 4 verts
					m_numVisibleExtraBlendTiles += indexCount/6;
				}
			}
			W3DShaderManager::resetShader(st);
		}
  }
}
#endif
