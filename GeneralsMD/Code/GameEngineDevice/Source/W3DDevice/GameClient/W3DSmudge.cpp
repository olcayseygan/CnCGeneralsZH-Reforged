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

// W3DSmudge.cpp ////////////////////////////////////////////////////////////////////////////////
// Smudge System implementation
// Author: Mark Wilczynski, June 2003
///////////////////////////////////////////////////////////////////////////////////////////////////

#include "Lib/BaseType.h"
#include "always.h"
#include "W3DDevice/GameClient/W3DSmudge.h"
#include "W3DDevice/GameClient/W3DShaderManager.h"
#include "Common/GameMemory.h"
#include "Common/GlobalData.h"
#include "GameClient/View.h"
#include "GameClient/Display.h"
#include "WW3D2/texture.h"
#include "WW3D2/dx8indexbuffer.h"
#include "WW3D2/dx8wrapper.h"
#include "WW3D2/d3dx9runtime.h"
#include "WW3D2/rinfo.h"
#include "WW3D2/camera.h"
#include "WW3D2/sortingrenderer.h"
#include "Platform/RenderTypes.h"

#ifdef _INTERNAL
// for occasional debugging...
//#pragma optimize("", off)
//#pragma MESSAGE("************************************** WARNING, optimization disabled for debugging purposes")
#endif

SmudgeManager *TheSmudgeManager=NULL;

W3DSmudgeManager::W3DSmudgeManager(void)
{
}

W3DSmudgeManager::~W3DSmudgeManager()
{
	ReleaseResources();
}

void W3DSmudgeManager::init(void)
{
	SmudgeManager::init();

	// ReAcquireResources reads the back buffer off the device, and -nodevice has neither.
	if (TheGlobalData && TheGlobalData->m_noRenderDevice)
		return;

	ReAcquireResources();
}

void W3DSmudgeManager::reset (void)
{
	SmudgeManager::reset();	//base
}

void W3DSmudgeManager::ReleaseResources(void)
{
#ifdef USE_COPY_RECTS
	REF_PTR_RELEASE(m_backgroundTexture);
#endif
	REF_PTR_RELEASE(m_indexBuffer);
}

//Make sure (SMUDGE_DRAW_SIZE * 12) < 65535 because that's the max index buffer size.
#define SMUDGE_DRAW_SIZE	500	//draw at most 50 smudges per call. Tweak value to improve CPU/GPU parallelism.

void W3DSmudgeManager::ReAcquireResources(void)
{
	ReleaseResources();

	SurfaceClass *surface=DX8Wrapper::_Get_DX8_Back_Buffer();
	SurfaceClass::SurfaceDescription surface_desc;

	surface->Get_Description(surface_desc);
	REF_PTR_RELEASE(surface);

	#ifdef USE_COPY_RECTS
	m_backgroundTexture = MSGNEW("TextureClass") TextureClass(TheTacticalView->getWidth(),TheTacticalView->getHeight(),surface_desc.Format,MIP_LEVELS_1,TextureClass::POOL_DEFAULT, true);
	#endif

	m_backBufferWidth = surface_desc.Width;
	m_backBufferHeight = surface_desc.Height;

	m_indexBuffer=NEW_REF(DX8IndexBufferClass,(SMUDGE_DRAW_SIZE*4*3));	//allocate 4 triangles per smudge, each with 3 indices.

	// Fill up the IB with static vertex indices that will be used for all smudges.
	{
		DX8IndexBufferClass::WriteLockClass lockIdxBuffer(m_indexBuffer);
		UnsignedShort *ib=lockIdxBuffer.Get_Index_Array();
		//quad of 4 triangles:
		//	0-----3
		//  |\   /|
		//  |  4  |
		//	|/   \|
		//  1-----2
		Int vbCount=0;
		for (Int i=0; i<SMUDGE_DRAW_SIZE; i++)
		{
			//Top
			ib[0]=vbCount;
			ib[1]=vbCount+4;
			ib[2]=vbCount+3;
			//Right
			ib[3]=vbCount+3;
			ib[4]=vbCount+4;
			ib[5]=vbCount+2;
			//Bottom
			ib[6]=vbCount+2;
			ib[7]=vbCount+4;
			ib[8]=vbCount+1;
			//Left
			ib[9]=vbCount+1;
			ib[10]=vbCount+4;
			ib[11]=vbCount+0;

			vbCount += 5;
			ib+=12;
		}
	}
}

/*Copies a portion of the current render target into a specified buffer*/
Int copyRect(unsigned char *buf, Int bufSize, int oX, int oY, int width, int height)
{
 	IDirect3DSurface9 *surface=NULL;	///<previous render target
 	IDirect3DSurface9 *tempSurface=NULL;
	Int result = 0;
	RenderResult hr = D3D_OK;
	unsigned int surfaceSize = 0;

 	LPDIRECT3DDEVICE9 m_pDev=DX8Wrapper::_Get_D3D_Device();

	if (!m_pDev)
		goto error;

 	m_pDev->GetRenderTarget(PRIMARY_RENDER_TARGET, &surface);

	if (!surface)
		goto error;

 	D3DSURFACE_DESC desc;
 
 	surface->GetDesc(&desc);
 
	RenderRect srcRect;
	srcRect.left=oX;
	srcRect.top=oY;
	srcRect.right=oX+width;
	srcRect.bottom=oY+height;

	RenderPoint dstPoint;
	dstPoint.x=0;
	dstPoint.y=0;

 	hr=m_pDev->CreateOffscreenPlainSurface(width, height, desc.Format, D3DPOOL_SYSTEMMEM, &tempSurface, NULL);

	if (hr != D3D_OK)
		goto error;

	// The source is the render target, so this is the one direction CopyRects had that
	// UpdateSurface and StretchRect cannot do.  GetRenderTargetData takes no rectangle,
	// so the surface is the size of the piece being read back and the read starts there.
	{
		IDirect3DSurface9 *wholeTarget = NULL;
		hr=m_pDev->CreateOffscreenPlainSurface(desc.Width, desc.Height, desc.Format,
			D3DPOOL_SYSTEMMEM, &wholeTarget, NULL);
		if (hr == D3D_OK)
		{
			hr=m_pDev->GetRenderTargetData(surface, wholeTarget);
			if (hr == D3D_OK)
				hr=D3DXLoadSurfaceFromSurface(tempSurface, NULL, NULL, wholeTarget, NULL, &srcRect,
					D3DX_FILTER_NONE, 0);
			wholeTarget->Release();
		}
	}

	if (hr != D3D_OK)
		goto error;

 	D3DLOCKED_RECT lrect;
 
 	hr=tempSurface->LockRect(&lrect,NULL,D3DLOCK_READONLY);

	if (hr != D3D_OK)
		goto error;

 	tempSurface->GetDesc(&desc);

	// D3D9's surface description dropped the byte size D3D8 carried, so the readback is
	// measured from the rows the lock reports instead.
	surfaceSize = (unsigned int)lrect.Pitch * desc.Height;

	if (surfaceSize < bufSize)
		bufSize = surfaceSize;
		
	memcpy(buf,lrect.pBits,bufSize);
	result = bufSize;

error:
	if (surface)
		surface->Release();
	if (tempSurface)
		tempSurface->Release();

	return result;
}

#define UNIQUE_COLOR	(0x12345678)
#define BLOCK_SIZE	(8)

Bool W3DSmudgeManager::testHardwareSupport(void)
{
	if (m_hardwareSupportStatus == SMUDGE_SUPPORT_UNKNOWN)
	{	//we have not done the test yet.

#if defined(__linux__)
		// The probe's second quad, like every smudge after it, samples the render texture it draws
		// into.  SDL3 GPU cannot read a pass's own target, so the device refuses that draw; the
		// untouched block then matched the first quad and the probe answered yes, and each smudge
		// was refused in its turn.  Answer no without the feedback draw: smudge systems draw
		// nothing either way.
		m_hardwareSupportStatus = SMUDGE_SUPPORT_NO;
		return FALSE;
#endif

		IDirect3DTexture9 *backTexture=W3DShaderManager::getRenderTexture();
		if (!backTexture)
		{	//do trivial test first to see if render target exists.
			m_hardwareSupportStatus = SMUDGE_SUPPORT_NO;
			return FALSE;
		}

		if (!W3DShaderManager::isRenderingToTexture())
			return FALSE;	//can't do the test unless we're rendering to texture.

		VertexMaterialClass *vmat=VertexMaterialClass::Get_Preset(VertexMaterialClass::PRELIT_DIFFUSE);
		DX8Wrapper::Set_Material(vmat);
		REF_PTR_RELEASE(vmat);	//no need to keep a reference since it's a preset.
		
		ShaderClass shader=ShaderClass::_PresetOpaqueShader;
		shader.Set_Depth_Compare(ShaderClass::PASS_ALWAYS);
		shader.Set_Depth_Mask(ShaderClass::DEPTH_WRITE_DISABLE);
		DX8Wrapper::Set_Shader(shader);
		DX8Wrapper::Set_Texture(0,NULL);
		DX8Wrapper::Apply_Render_State_Changes();	//force update of view and projection matrices

		struct _TRANS_LIT_TEX_VERTEX {
			Vector4 p;
			UnsignedInt color;   // diffuse color    
			float	u;
			float	v;
		} v[4];

		//bottom right
		v[0].p = Vector4( BLOCK_SIZE-0.5f, BLOCK_SIZE-0.5f, 0.0f, 1.0f );
		v[0].u = BLOCK_SIZE/(Real)TheDisplay->getWidth();	v[0].v = BLOCK_SIZE/(Real)TheDisplay->getHeight();;
		//top right
		v[1].p = Vector4( BLOCK_SIZE-0.5f, 0-0.5f, 0.0f, 1.0f );
		v[1].u = BLOCK_SIZE/(Real)TheDisplay->getWidth();	v[1].v = 0;
		//bottom left
		v[2].p = Vector4(  0-0.5f, BLOCK_SIZE-0.5f, 0.0f, 1.0f );
		v[2].u = 0;	v[2].v = BLOCK_SIZE/(Real)TheDisplay->getHeight();
		//top left
		v[3].p = Vector4(  0-0.5f,  0-0.5f, 0.0f, 1.0f );
		v[3].u = 0;	v[3].v = 0;

		v[0].color = UNIQUE_COLOR;
		v[1].color = UNIQUE_COLOR;
		v[2].color = UNIQUE_COLOR;
		v[3].color = UNIQUE_COLOR;

		LPDIRECT3DDEVICE9 pDev=DX8Wrapper::_Get_D3D_Device();

		//draw polygons like this is very inefficient but for only 2 triangles, it's
		//not worth bothering with index/vertex buffers.
		pDev->SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1);

		// Both quads are read back off the Direct3D 9 target, so they go to that device even while
		// Direct3D 11 presents.
		DX8Wrapper::_Draw_DX8_Primitive_UP(D3DPT_TRIANGLESTRIP, 2, v, sizeof(_TRANS_LIT_TEX_VERTEX), true);

		UnsignedInt refData[BLOCK_SIZE*BLOCK_SIZE];
		memset(refData,0,sizeof(refData));
		Int bufSize=copyRect((unsigned char *)refData,sizeof(refData),0,0,BLOCK_SIZE,BLOCK_SIZE);	//copy area we just rendered using solid color
		if (!bufSize)
		{
			m_hardwareSupportStatus = SMUDGE_SUPPORT_NO;
			return FALSE;
		}

		DX8Wrapper::Set_DX8_Texture(0,backTexture);

		UnsignedInt testData[BLOCK_SIZE*BLOCK_SIZE];
		memset(testData,0xff,sizeof(testData));

		v[0].color = 0xffffffff;
		v[1].color = 0xffffffff;
		v[2].color = 0xffffffff;
		v[3].color = 0xffffffff;

		DX8Wrapper::_Draw_DX8_Primitive_UP(D3DPT_TRIANGLESTRIP, 2, v, sizeof(_TRANS_LIT_TEX_VERTEX), true);
		bufSize=copyRect((unsigned char *)testData,sizeof(testData),0,0,BLOCK_SIZE,BLOCK_SIZE);

		if (!bufSize)
		{
			m_hardwareSupportStatus = SMUDGE_SUPPORT_NO;
			return FALSE;
		}

		//compare the 2 buffers to see if they match.
		if (memcmp(testData,refData,bufSize) == 0)
		{
			m_hardwareSupportStatus = SMUDGE_SUPPORT_YES;
			return TRUE;
		}
		m_hardwareSupportStatus = SMUDGE_SUPPORT_NO;
	}

	return (SMUDGE_SUPPORT_YES == m_hardwareSupportStatus);
}

void W3DSmudgeManager::render(RenderInfoClass &rinfo)
{
	//Verify that the card supports the effect.
	if (!testHardwareSupport())
		return;

	//
	// ...and that there is anything to smudge.  The list was not looked at until forty lines down,
	// after the camera matrices, the surface description and, on the copy-rects path, a read of the
	// back buffer's surface level - all of it every frame of a match with no heat haze anywhere on
	// the screen, which is most of them.
	//
	if (m_usedSmudgeSetList.Head() == NULL)
		return;

	//
	// The particle pass adds its global set every frame, smudges or not, so the check above almost
	// never fires in a match.  A frame whose sets are all empty returns here as well, after the one
	// thing the path below did for it: the flush of the sorted translucent polys, which it reached
	// on a render texture before finding nothing to draw.  Kept so the draw order is the same.
	//
	SmudgeSet *anySet = m_usedSmudgeSetList.Head();
	while (anySet && anySet->getUsedSmudgeList().Head() == NULL)
		anySet = anySet->Succ();
	if (anySet == NULL)
	{
#ifndef USE_COPY_RECTS
		if (W3DShaderManager::getRenderTexture() && W3DShaderManager::isRenderingToTexture())
#endif
			SortingRendererClass::Flush();
		return;
	}

	CameraClass &camera=rinfo.Camera;
	Vector3 vsVert;
	Vector4 ssVert;
	Real uvSpanX,uvSpanY;
	Vector3 vertex_offsets[4] = {
		Vector3(-0.5f, 0.5f, 0.0f),
		Vector3(-0.5f, -0.5f, 0.0f),
		Vector3(0.5f, -0.5f, 0.0f),
		Vector3(0.5f, 0.5f, 0.0f)
	};

#define THE_COLOR (0x00ffeedd)

	UnsignedInt vertexDiffuse[5]={THE_COLOR,THE_COLOR,THE_COLOR,THE_COLOR,THE_COLOR};

	Matrix4x4 proj;
	Matrix3D view;

	camera.Get_View_Matrix(&view);
	camera.Get_Projection_Matrix(&proj);

	SurfaceClass::SurfaceDescription surface_desc;
#ifdef USE_COPY_RECTS
	SurfaceClass *background=m_backgroundTexture->Get_Surface_Level();
	background->Get_Description(surface_desc);
#else
	D3DSURFACE_DESC D3DDesc;

	IDirect3DTexture9 *backTexture=W3DShaderManager::getRenderTexture();
	if (!backTexture || !W3DShaderManager::isRenderingToTexture())
		return;	//this card doesn't support render targets.

	backTexture->GetLevelDesc(0,&D3DDesc);

	surface_desc.Width = D3DDesc.Width;
	surface_desc.Height = D3DDesc.Height;
#endif

	Real texClampX = (Real)TheTacticalView->getWidth()/(Real)surface_desc.Width;
	Real texClampY = (Real)TheTacticalView->getHeight()/(Real)surface_desc.Height;

	Real texScaleX = texClampX*0.5f;
	Real texScaleY = texClampY*0.5f;

	//Do a first pass over the smudges to determine how many are visible
	//and to fill in their world-space positions and screen uv coordinates.
	//TODO: Optimize out this extra pass!
	//TODO: Find size of screen rectangle that actually needs copying.

	SmudgeSet *set=m_usedSmudgeSetList.Head();	//first set that didn't fit into render batch.
	Int count = 0;

	if (set)
	{	//there are possibly some smudges to render, so make sure background particles have finished drawing.
		SortingRendererClass::Flush();	//draw sorted translucent polys like particles.
	}

	while (set)
	{
		Smudge *smudge=set->getUsedSmudgeList().Head();

		while (smudge)
		{
			//Get view-space center
			Matrix3D::Transform_Vector(view,smudge->m_pos,&vsVert);

			//Get 5 view-space vertices
			Smudge::smudgeVertex *verts=smudge->m_verts;

			//Do center vertex outside 'for' loop since it's different.
			verts[4].pos = vsVert;

			for (Int i=0; i<4; i++)
			{
				verts[i].pos = vsVert + vertex_offsets[i] * smudge->m_size;
				//Ge uv coordinates for each vertex
				ssVert = proj * verts[i].pos;
				Real oow = 1.0f/ssVert.W;
				ssVert *= oow;	//returned in camera space which is -1,-1 (bottom-left) to 1,1 (top-right)
				//convert camera space to uv space: 0,0 (top-left), 1,1 (bottom-right)
				verts[i].uv.Set((ssVert.X+1.0f)*texScaleX,(1.0f-ssVert.Y)*texScaleY);

				Vector2 &thisUV=verts[i].uv;

				//Clamp coordinates so we're not referencing texels outside the view.
				if (thisUV.X > texClampX)
					smudge->m_offset.X = 0;
				else
				if (thisUV.X < 0)
					smudge->m_offset.X = 0;

				if (thisUV.Y > texClampY)
					smudge->m_offset.Y = 0;
				else
				if (thisUV.Y < 0)
					smudge->m_offset.Y = 0;

			}

			//Finish center vertex
			//Ge uv coordinates by interpolating corner uv coordinates and applying desired offset.
			uvSpanX=verts[3].uv.X - verts[0].uv.X;
			uvSpanY=verts[1].uv.Y - verts[0].uv.Y;
			// The pull at the centre is a share of the smudge's own width, which is a shimmer on a
			// smoke puff and a second copy of the ground on a nuke's heat ring: those are 200 to 350
			// pixels across at the default zoom, sixteen to twenty-four of them overlap, and each
			// blended a copy of the ground shifted by up to twenty pixels over the unshifted one,
			// on both devices.  Held to a few pixels of the view it stays a shimmer at any size.
			// 0.004 kept the doubling off and the nuke's ring with it, readable for half a second;
			// 0.006 stacked shifted copies of the flash's white edge into stepped bands
			const Real MAX_PULL = 0.005f;	// of the view's width and height, about 10 pixels at 1920
			const Real pullX = WWMath::Clamp(uvSpanX*smudge->m_offset.X, -MAX_PULL*texClampX, MAX_PULL*texClampX);
			// .Y, not .X: the vertical offset was rolled, clamped against the top and bottom of the
			// view a dozen lines up, and then never read - the centre vertex took the horizontal
			// one on both axes, so every smudge pulled the background along one diagonal.
			const Real pullY = WWMath::Clamp(uvSpanY*smudge->m_offset.Y, -MAX_PULL*texClampY, MAX_PULL*texClampY);
			verts[4].uv.X=verts[0].uv.X+uvSpanX*0.5f+pullX;
			verts[4].uv.Y=verts[0].uv.Y+uvSpanY*0.5f+pullY;

			count++;	//increment visible smudge count.
			smudge=smudge->Succ();
		}

		set=set->Succ();	//advance to next node.
	}

	if (!count)
	{
#ifdef USE_COPY_RECTS
		REF_PTR_RELEASE(background);
#endif
		return;	//nothing to render.
	}

#ifdef USE_COPY_RECTS
	SurfaceClass *backBuffer=DX8Wrapper::_Get_DX8_Back_Buffer();

	backBuffer->Get_Description(surface_desc);

	//Copy the area of backbuffer occupied by smudges into an alternate buffer.
	background->Copy(0,0,0,0,surface_desc.Width,surface_desc.Height,backBuffer);

	REF_PTR_RELEASE(background);
	REF_PTR_RELEASE(backBuffer);
#endif

	Matrix4x4 identity(true);
	DX8Wrapper::Set_Transform(D3DTS_WORLD,identity);	
	DX8Wrapper::Set_Transform(D3DTS_VIEW,identity);	

	DX8Wrapper::Set_Index_Buffer(m_indexBuffer,0);
	//DX8Wrapper::Set_Shader(ShaderClass::_PresetOpaqueSpriteShader);

	DX8Wrapper::Set_Shader(ShaderClass::_PresetAlphaShader);
#ifdef USE_COPY_RECTS
	DX8Wrapper::Set_Texture(0,m_backgroundTexture);
#else
	DX8Wrapper::Set_DX8_Texture(0,backTexture);
	//Need these states in case texture is non-power-of-2
	DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
	DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
	DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DSAMP_ADDRESSW, D3DTADDRESS_CLAMP);
	DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
	DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
	DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
#endif
	VertexMaterialClass *vmat=VertexMaterialClass::Get_Preset(VertexMaterialClass::PRELIT_DIFFUSE);
	DX8Wrapper::Set_Material(vmat);
	REF_PTR_RELEASE(vmat);
	DX8Wrapper::Apply_Render_State_Changes();

	//Disable reading texture alpha since it's undefined.
	//DX8Wrapper::Set_DX8_Texture_Stage_State(0,D3DTSS_COLOROP,D3DTOP_SELECTARG1);			
	DX8Wrapper::Set_DX8_Texture_Stage_State(0,D3DTSS_ALPHAOP,D3DTOP_SELECTARG2);			

	Int smudgesRemaining=count;
	set=m_usedSmudgeSetList.Head();	//first smudge set that needs rendering.
	Smudge	*remainingSmudgeStart=set->getUsedSmudgeList().Head();	//first smudge that needs rendering.

	while (smudgesRemaining)	//keep drawing smudges until we run out.
	{
		//Now that we know how many smudges need rendering, allocate vertex buffer space and copy verts.
		count=smudgesRemaining;

		if (count > SMUDGE_DRAW_SIZE)
			count = SMUDGE_DRAW_SIZE;

		Int smudgesInRenderBatch=0;

		DynamicVBAccessClass vb_access(BUFFER_TYPE_DYNAMIC_DX8,dynamic_fvf_type,count*5);	//allocate 5 verts per smudge.
		{
			DynamicVBAccessClass::WriteLockClass lock(&vb_access);
			VertexFormatXYZNDUV2* verts=lock.Get_Formatted_Vertex_Array();

			while (set)
			{
				Smudge *smudge=remainingSmudgeStart;

				while (smudge)
				{
					Smudge::smudgeVertex *smVerts = smudge->m_verts;

					//Check if we exceeded maximum number of smudges allowed per draw call.
					if (smudgesInRenderBatch >= count)
					{	remainingSmudgeStart = smudge;
						goto flushSmudges;
					}

					//Set center vertex opacity.
					vertexDiffuse[4] = ((Int)(smudge->m_opacity * 255.0f) << 24) | THE_COLOR;

					for (Int i=0; i<5; i++)
					{
						verts->x=smVerts->pos.X;
						verts->y=smVerts->pos.Y;
						verts->z=smVerts->pos.Z;
						verts->nx=0;	//keep AGP write-combining active
						verts->ny=0;
						verts->nz=0;
						verts->diffuse=vertexDiffuse[i];	//set to transparent
						verts->u1=smVerts->uv.X;
						verts->v1=smVerts->uv.Y;
						verts->u2=0;	//keep AGP write-combining active
						verts->v2=0;
						verts++;
						smVerts++;
					}

					smudgesInRenderBatch++;
					smudge=smudge->Succ();
				}

				set=set->Succ();	//advance to next node.

				if (set)	//start next batch at beginning of set.
					remainingSmudgeStart = set->getUsedSmudgeList().Head();
			}
flushSmudges:
			DX8Wrapper::Set_Vertex_Buffer(vb_access);
		}

		DX8Wrapper::Draw_Triangles(	0,smudgesInRenderBatch*4, 0, smudgesInRenderBatch*5);	

//Debug Code which draws outline around smudge
/*		DX8Wrapper::_Get_D3D_Device()->SetRenderState(D3DRS_FILLMODE,D3DFILL_WIREFRAME);
		DX8Wrapper::_Get_D3D_Device()->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE);
		DX8Wrapper::Set_DX8_Texture_Stage_State(0,D3DTSS_COLOROP,D3DTOP_SELECTARG2);			
		DX8Wrapper::Draw_Triangles(	0,smudgesInRenderBatch*4, 0, smudgesInRenderBatch*5);	
		DX8Wrapper::_Get_D3D_Device()->SetRenderState(D3DRS_FILLMODE,D3DFILL_SOLID);
		DX8Wrapper::_Get_D3D_Device()->SetRenderState(D3DRS_ALPHABLENDENABLE,TRUE);
		DX8Wrapper::Set_DX8_Texture_Stage_State(0,D3DTSS_COLOROP,D3DTOP_SELECTARG1);			
*/
		smudgesRemaining -= smudgesInRenderBatch;
	}

	DX8Wrapper::Set_DX8_Texture_Stage_State(0,D3DTSS_COLOROP,D3DTOP_MODULATE);			
	DX8Wrapper::Set_DX8_Texture_Stage_State(0,D3DTSS_ALPHAOP,D3DTOP_MODULATE);			

}