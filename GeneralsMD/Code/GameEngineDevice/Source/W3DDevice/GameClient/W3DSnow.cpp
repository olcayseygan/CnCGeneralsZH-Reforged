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

// FILE: W3DSnow.h /////////////////////////////////////////////////////////

#include "W3DDevice/GameClient/W3DSnow.h"
#include "W3DDevice/GameClient/HeightMap.h"
#include "Common/GlobalData.h"
#include "GameClient/View.h"
#include "GameLogic/ScriptEngine.h"
#include "WW3D2/dx8wrapper.h"
#include "WW3D2/rinfo.h"
#include "WW3D2/camera.h"
#include "WW3D2/assetmgr.h"
#include "Platform/RenderTypes.h"


#ifdef _INTERNAL
// for occasional debugging...
//#pragma optimize("", off)
//#pragma MESSAGE("************************************** WARNING, optimization disabled for debugging purposes")
#endif

#define D3DFVF_POINTVERTEX (D3DFVF_XYZ)
#define SNOW_BUFFER_SIZE 4096	//size of vertex buffer holding particles.
#define SNOW_BATCH_SIZE	2048	//we render at most this many particles per drawprimitive call.  This number * 6 must be less than 65536 to fit into index buffer.

struct POINTVERTEX
{
    Vector3 v;	//center of particle.
};

W3DSnowManager::W3DSnowManager(void)
{
	m_indexBuffer=NULL;
	m_snowTexture=NULL;
	m_VertexBufferD3D=NULL;
	m_weatherDrops=NULL;
	m_weatherTextures[0]=NULL;
	m_weatherTextures[1]=NULL;
	m_weatherTime=0.0;
	m_weatherMap=NULL;
	m_weatherMapData=NULL;
	m_weatherMapWidth=0;
	m_weatherSandCells=0;
	m_weatherCells=0;
}

W3DSnowManager::~W3DSnowManager()
{
	ReleaseResources();
	delete [] m_weatherDrops;
}

// Direct3D 11 has no point sprites at all: the fixed-function primitive that expands one vertex
// into a screen-aligned quad was taken out of the API, and the replacement is a geometry shader the
// backend does not have.  The snow already carries a second path that builds those quads on the CPU
// and draws them through DX8Wrapper, so a presenting D3D11 run takes that one and gets its snow.
// SDL3 GPU on Linux is the same: Vulkan sizes a point only from a PointSize the vertex program
// writes, the device's fixed-function programs write none, so a point list there is a one-pixel
// dot and a pipeline the validation layers reject (VUID-VkGraphicsPipelineCreateInfo-topology-08773).
static Bool snowWantsPointSprites(void)
{
	return TheWeatherSetting->m_usePointSprites
		&& DX8Wrapper::Get_Current_Caps()->Support_PointSprites();
}

static Bool snowUsesPointSprites(void)
{
#if defined(__linux__)
	return FALSE;
#else
	return !Direct3D11_Present_Is_Enabled() && snowWantsPointSprites();
#endif
}

void W3DSnowManager::init( void )
{
	SnowManager::init();
	ReAcquireResources();
}

/** Releases all W3D/D3D assets before a reset.. */
void W3DSnowManager::ReleaseResources(void)
{
	REF_PTR_RELEASE(m_snowTexture);
	REF_PTR_RELEASE(m_weatherTextures[0]);
	REF_PTR_RELEASE(m_weatherTextures[1]);

	if (m_VertexBufferD3D)
		m_VertexBufferD3D->Release();

	m_VertexBufferD3D=NULL;

	REF_PTR_RELEASE(m_indexBuffer);
}

/** (Re)allocates all W3D/D3D assets after a reset.. */
Bool W3DSnowManager::ReAcquireResources(void)
{
	ReleaseResources();

	if (!TheWeatherSetting->m_snowEnabled && m_weatherKind == WEATHER_KIND_NONE)
		return TRUE;	//no need for resources if snow is disabled.

	if (snowUsesPointSprites())
	{
		LPDIRECT3DDEVICE9 m_pDev=DX8Wrapper::_Get_D3D_Device();

		DEBUG_ASSERTCRASH(m_pDev, ("Trying to ReAquireResources on W3DSnowManager without device"));

		if (m_VertexBufferD3D == NULL)
		{	// Create vertex buffer

			if (Render_Failed(m_pDev->CreateVertexBuffer
			(
				SNOW_BUFFER_SIZE*sizeof(POINTVERTEX),
				D3DUSAGE_WRITEONLY|D3DUSAGE_DYNAMIC|D3DUSAGE_POINTS, 
				D3DFVF_POINTVERTEX,
				D3DPOOL_DEFAULT,
				&m_VertexBufferD3D,
				NULL	// pSharedHandle, D3D9's extra parameter, reserved and always null
			)))
				return FALSE;
		}
	}
	else
	{
		m_indexBuffer=NEW_REF(DX8IndexBufferClass,(SNOW_BATCH_SIZE *6));	//allocate 2 triangles per flake, each with 3 indices.

		// Fill up the IB with static vertex indices that will be used for all smudges.
		{
			DX8IndexBufferClass::WriteLockClass lockIdxBuffer(m_indexBuffer);
			UnsignedShort *ib=lockIdxBuffer.Get_Index_Array();
			//quad of 4 triangles:
			//	0-----3
			//  |\   /|
			//  |  X  |
			//	|/   \|
			//  1-----2
			Int vbCount=0;
			for (Int i=0; i<SNOW_BATCH_SIZE; i++)
			{
				//Top
				ib[0]=vbCount+3;
				ib[1]=vbCount;
				ib[2]=vbCount+2;
				//Bottom
				ib[3]=vbCount+2;
				ib[4]=vbCount;
				ib[5]=vbCount+1;
		
				vbCount += 4;
				ib+=6;
			}
		}
	}

	m_snowTexture = WW3DAssetManager::Get_Instance()->Get_Texture(TheWeatherSetting->m_snowTexture.str());

	m_dwBase = SNOW_BUFFER_SIZE;
	m_dwDiscard = SNOW_BUFFER_SIZE;
	m_dwFlush = SNOW_BATCH_SIZE;

	return TRUE;
}

void W3DSnowManager::updateIniSettings(void)
{
	//Call base class
	SnowManager::updateIniSettings();

	if (m_snowTexture && strcasecmp(m_snowTexture->Get_Texture_Name(),TheWeatherSetting->m_snowTexture.str()) != 0)
	{	
		REF_PTR_RELEASE(m_snowTexture);
		m_snowTexture = WW3DAssetManager::Get_Instance()->Get_Texture(TheWeatherSetting->m_snowTexture.str());
	}
}

void W3DSnowManager::reset( void )
{
	SnowManager::reset();
}

void W3DSnowManager::update(void)
{

	m_time += WW3D::Get_Frame_Time() / 1000.0f;

	//find current time offset, adjusting for overflow
	m_time=fmod(m_time,m_fullTimePeriod);

	m_weatherOn = TheGlobalData->m_weatherEffects && !TheGlobalData->m_classicGraphics && Direct3D11_Present_Is_Enabled();
	if (m_weatherOn)
		updateWeather();
	else
		m_weatherKind = WEATHER_KIND_NONE;
}

/* The Weather option (Options > Effects, Direct3D 11, off under Classic).  A client's picture and no
	more: quads, a wind on the trees and on rising smoke, the still haze.  Nothing here reaches logic,
	sight or the shroud, so two players may see different weather in one match.

	Each layer keeps a fixed number of quads in a square tile that follows the camera; a quad wraps
	round the tile, which leaves it where it is in the world while the camera pans.  So the cost is the
	count, whatever the zoom.  A quad covers no pixel the texture would not, and the program is the one
	EA's snow draws with. */
struct WeatherLayer
{
	const char *texture;
	Int count;			// quads on a tile of WEATHER_TILE_STEP; a smaller tile takes fewer
	Real along;			// world units a second down the wind
	Real down;			// and toward the ground
	Real length;		// a streak's length along its flight, or a card's height
	Real width;
	UnsignedInt colour;	// taken down with the map's light
	Real height;		// the layer's depth over the ground the camera looks at; 0 falls from the camera
	Bool facesCamera;	// a card instead of a streak along the flight
};

static const WeatherLayer WEATHER_LAYERS[WEATHER_KIND_COUNT][2] =
{
	{ { NULL }, { NULL } },
	{ { "EXRainDrop.tga", 8000, 180.0f, 1100.0f, 44.0f, 3.4f, 0xe0d8e4f0, 0.0f, FALSE }, { NULL } },
	{ { "EXRainDrop.tga", 6000, 520.0f, 25.0f, 72.0f, 3.2f, 0xb0ecd4a4, 150.0f, FALSE },
		{ "EXCloud01.tga", 200, 260.0f, 4.0f, 260.0f, 260.0f, 0x40d0ac7c, 170.0f, TRUE } },
	{ { "EXSnowFlake1.tga", 7000, 34.0f, 46.0f, 2.4f, 2.4f, 0xe0e8eef4, 0.0f, TRUE }, { NULL } }
};

static const Real WEATHER_WIND_STRENGTH[WEATHER_KIND_COUNT] = { 0.5f, 0.8f, 1.0f, 0.35f };
static const Int WEATHER_MAX_DROPS = 8192;
static const Real WEATHER_TILE_STEP = 2048.0f;
static const Real WEATHER_TILE_MIN = 512.0f;
static const Real WEATHER_TILE_MAX = 4096.0f;
static const Real WEATHER_FALL_PER_TILE = 0.75f;	// how far a falling layer reaches, by the tile
static const Real WEATHER_EYE_CLEARANCE = 0.55f;	// of the eye's height: nothing falls against the lens, where a streak would cross the screen

void W3DSnowManager::updateWeather(void)
{
	m_weatherTime = fmod(m_weatherTime + WW3D::Get_Frame_Time() / 1000.0, 3600.0);

	WorldHeightMap *map = TheTerrainRenderObject != NULL ? TheTerrainRenderObject->getMap() : NULL;
	if (map != NULL && (map != m_weatherMap || map->getDataPtr() != m_weatherMapData || map->getXExtent() != m_weatherMapWidth))
	{
		m_weatherMap = map;
		m_weatherMapData = map->getDataPtr();
		m_weatherMapWidth = map->getXExtent();
		map->countSandCells(&m_weatherSandCells, &m_weatherCells);
		DEBUG_LOG(("WEATHER map is %d sand of %d cells\n", m_weatherSandCells, m_weatherCells));
	}

	const WeatherKind kind = map == NULL ? WEATHER_KIND_NONE : weatherKindForMap(TheWeatherSetting->m_snowEnabled,
		TheGlobalData->m_weather == WEATHER_SNOWY, m_weatherSandCells, m_weatherCells);
	if (kind != m_weatherKind)
	{
		m_weatherKind = kind;
		REF_PTR_RELEASE(m_weatherTextures[0]);
		REF_PTR_RELEASE(m_weatherTextures[1]);
	}
	setWeatherWind(WEATHER_WIND_STRENGTH[kind]);
}

void W3DSnowManager::renderWeather(RenderInfoClass &rinfo)
{
	if (m_weatherDrops == NULL)
	{
		m_weatherDrops = NEW WeatherDrop[WEATHER_MAX_DROPS];
		for (Int i = 0; i < WEATHER_MAX_DROPS; ++i)
		{
			m_weatherDrops[i].x = rand() / (Real)RAND_MAX;
			m_weatherDrops[i].y = rand() / (Real)RAND_MAX;
			m_weatherDrops[i].phase = rand() / (Real)RAND_MAX;
		}
	}
	if (!m_indexBuffer)
		ReAcquireResources();

	Matrix4x4 identity(true);
	DX8Wrapper::Set_Transform(D3DTS_WORLD, identity);
	Matrix3D view;
	rinfo.Camera.Get_View_Matrix(&view);
	DX8Wrapper::Set_Transform(D3DTS_VIEW, view);
	VertexMaterialClass *vmat = VertexMaterialClass::Get_Preset(VertexMaterialClass::PRELIT_DIFFUSE);
	DX8Wrapper::Set_Material(vmat);
	REF_PTR_RELEASE(vmat);
	DX8Wrapper::Set_Index_Buffer(m_indexBuffer, 0);

	const GlobalData::TerrainLighting &sun = TheGlobalData->m_terrainLighting[TheGlobalData->m_timeOfDay][0];
	const Real light = WWMath::Clamp(0.299f * (sun.ambient.red + sun.diffuse.red) + 0.587f * (sun.ambient.green + sun.diffuse.green)
		+ 0.114f * (sun.ambient.blue + sun.diffuse.blue), 0.3f, 1.0f);

	// No sun's shadow on what falls: with it every pixel of every quad pays the blocker search, for a
	// shade nobody could see on a streak.
	Direct3D11_Set_Emissive(true);
	drawWeatherLayer(rinfo, 0, light);
	if (WEATHER_LAYERS[m_weatherKind][1].texture != NULL)
		drawWeatherLayer(rinfo, 1, light);
	Direct3D11_Set_Emissive(false);
}

void W3DSnowManager::drawWeatherLayer(RenderInfoClass &rinfo, Int layerIndex, Real light)
{
	const WeatherLayer &layer = WEATHER_LAYERS[m_weatherKind][layerIndex];
	if (m_weatherTextures[layerIndex] == NULL)
		m_weatherTextures[layerIndex] = WW3DAssetManager::Get_Instance()->Get_Texture(layer.texture);
	DX8Wrapper::Set_Texture(0, m_weatherTextures[layerIndex]);
	// A card wider than a unit would cut a hard line where it meets the ground, so it takes no depth:
	// a cloud between the eye and the ground goes over all of it anyway.
	ShaderClass shader(ShaderClass::_PresetAlphaShader);
	if (layer.height > 0.0f && layer.facesCamera)
		shader.Set_Depth_Compare(ShaderClass::PASS_ALWAYS);
	// A near vertical streak past the point under the camera turns its back to it at a steep tilt.
	shader.Set_Cull_Mode(ShaderClass::CULL_MODE_DISABLE);
	DX8Wrapper::Set_Shader(shader);

	CameraClass &camera = rinfo.Camera;
	const Vector3 eye = camera.Get_Position();

	// The tile: a power of two times the step, so it holds through a pan and only a change of zoom
	// moves the drops, and centred ahead of the camera as far as it may go and still hold the near ground.
	AABoxClass visible;
	TheTerrainRenderObject->getMaximumVisibleBox(camera.Get_Frustum(), &visible, TRUE);
	const Real wanted = 2.0f * max(visible.Extent.X, visible.Extent.Y);
	Real tile = WEATHER_TILE_MIN;
	while (tile < wanted && tile < WEATHER_TILE_MAX)
		tile *= 2.0f;
	Vector2 ahead(visible.Center.X - eye.X, visible.Center.Y - eye.Y);
	const Real farthest = tile * 0.45f;
	if (ahead.Length() > farthest)
		ahead *= farthest / ahead.Length();
	const Real centreX = eye.X + ahead.X;
	const Real centreY = eye.Y + ahead.Y;
	const Real originX = centreX - tile * 0.5f;
	const Real originY = centreY - tile * 0.5f;

	const Real ground = TheTerrainRenderObject->getHeightMapHeight(centreX, centreY, NULL);
	const Real depth = layer.height > 0.0f ? layer.height : tile * WEATHER_FALL_PER_TILE;
	const Real top = layer.height > 0.0f ? ground + layer.height : eye.Z - (eye.Z - ground) * WEATHER_EYE_CLEARANCE;

	const Coord2D &wind = weatherBreezeInfo().m_directionVec;
	const Real shiftX = (Real)fmod(m_weatherTime * wind.x * layer.along, (double)tile);
	const Real shiftY = (Real)fmod(m_weatherTime * wind.y * layer.along, (double)tile);
	const Real fallen = (Real)fmod(m_weatherTime * layer.down / depth, 1.0);

	// A streak lies along its flight and takes its width across the view, so it never turns edge on.
	const Matrix3D &eyeFrame = camera.Get_Transform();
	Vector3 axis;
	Vector3 side;
	if (layer.facesCamera)
	{
		axis = eyeFrame.Get_Y_Vector() * -1.0f;
		side = eyeFrame.Get_X_Vector();
	}
	else
	{
		axis.Set(wind.x * layer.along, wind.y * layer.along, -layer.down);
		axis.Normalize();
		Vector3::Cross_Product(eyeFrame.Get_Z_Vector(), axis, &side);
		side.Normalize();
	}
	axis *= layer.length * 0.5f;
	side *= layer.width * 0.5f;
	const Vector3 corners[4] = { -axis - side, axis - side, axis + side, -axis + side };
	static const Real CORNER_U[4] = { 0.0f, 0.0f, 1.0f, 1.0f };
	static const Real CORNER_V[4] = { 0.0f, 1.0f, 1.0f, 0.0f };

	const UnsignedInt colour = (layer.colour & 0xff000000)
		| ((UnsignedInt)(((layer.colour >> 16) & 0xff) * light) << 16)
		| ((UnsignedInt)(((layer.colour >> 8) & 0xff) * light) << 8)
		| (UnsignedInt)((layer.colour & 0xff) * light);

	const Int count = min((Int)(layer.count * tile * tile / (WEATHER_TILE_STEP * WEATHER_TILE_STEP)), layer.count);
	Int done = 0;
	while (done < count)
	{
		const Int batch = min(count - done, (Int)SNOW_BATCH_SIZE);
		DynamicVBAccessClass vb_access(BUFFER_TYPE_DYNAMIC_DX8, dynamic_fvf_type, batch * 4);
		{
			DynamicVBAccessClass::WriteLockClass lock(&vb_access);
			VertexFormatXYZNDUV2 *verts = lock.Get_Formatted_Vertex_Array();
			for (Int i = 0; i < batch; ++i)
			{
				const WeatherDrop &drop = m_weatherDrops[done + i];
				const Real x = weatherWrap(drop.x * tile + shiftX, originX, tile);
				const Real y = weatherWrap(drop.y * tile + shiftY, originY, tile);
				const Real phase = drop.phase + fallen;
				const Real z = top - (phase - (Int)phase) * depth;
				for (Int corner = 0; corner < 4; ++corner)
				{
					verts->x = x + corners[corner].X;
					verts->y = y + corners[corner].Y;
					verts->z = z + corners[corner].Z;
					verts->nx = 0;	//keep AGP write-combining active
					verts->ny = 0;
					verts->nz = 0;
					verts->diffuse = colour;
					verts->u1 = CORNER_U[corner];
					verts->v1 = CORNER_V[corner];
					verts->u2 = 0;
					verts->v2 = 0;
					verts++;
				}
			}
		}
		DX8Wrapper::Set_Vertex_Buffer(vb_access);
		DX8Wrapper::Draw_Triangles(0, batch * 2, 0, batch * 4);
		done += batch;
	}
}

#define MAXIMUM_CAMERA_DISTANCE 100000	//maximum distance of camera position from world origin.
#define ISPOW2(x)  (x && (x & (x-1)) == 0)	//is a number a power of 2?
#define MODPOW2(x,y) ((x) & (y-1))		//mod '%' operator for powers of 2.

// Helper function to stuff a FLOAT into a DWORD argument
inline UnsignedInt FtoDW( float f ) { return *((UnsignedInt*)&f); }

/*Recursively subdivide the large snow box enclosing the camera until we reach some predefined leaf size.  This
method is used so that very few off-screen particles end up getting rendered.  Culling them individually would
be too expensive since we're dealing with 1000's for this effect.*/
void W3DSnowManager::renderSubBox(RenderInfoClass &rinfo, Int originX, Int originY, Int cubeDimX, Int cubeDimY )
{
	//check if this box is too large and needs subdivision
	Int boxDimX=cubeDimX - originX;
	Int boxDimY=cubeDimY - originY;
	Int halfX=REAL_TO_INT_CEIL(boxDimX*0.5f);
	Int halfY=REAL_TO_INT_CEIL(boxDimY*0.5f);

	CameraClass &camera=rinfo.Camera;
	MinMaxAABoxClass mmbox;

	if (boxDimX > m_leafDim)
	{	//subdivide the box
		if (boxDimY > m_leafDim)
		{	//subdivide in both directions
			//Upper left
			mmbox.MinCorner.Set(originX*m_emitterSpacing-m_cullOverscan, (originY + halfY)*m_emitterSpacing-m_cullOverscan, m_snowCeiling-m_boxDimensions);
			mmbox.MaxCorner.Set((originX + halfX)*m_emitterSpacing+m_cullOverscan, cubeDimY*m_emitterSpacing+m_cullOverscan, m_snowCeiling);
			if (CollisionMath::Overlap_Test(camera.Get_Frustum(),mmbox) != CollisionMath::OUTSIDE)
				renderSubBox(rinfo, originX, originY + halfY, originX + halfX, cubeDimY);
			//Upper right
			mmbox.MinCorner.Set((originX + halfX)*m_emitterSpacing-m_cullOverscan, (originY + halfY)*m_emitterSpacing-m_cullOverscan, m_snowCeiling-m_boxDimensions);
			mmbox.MaxCorner.Set(cubeDimX*m_emitterSpacing+m_cullOverscan, cubeDimY*m_emitterSpacing+m_cullOverscan, m_snowCeiling);
			if (CollisionMath::Overlap_Test(camera.Get_Frustum(),mmbox) != CollisionMath::OUTSIDE)
				renderSubBox(rinfo, originX + halfX, originY + halfY,cubeDimX, cubeDimY);
			//Lower left
			mmbox.MinCorner.Set(originX*m_emitterSpacing-m_cullOverscan, originY*m_emitterSpacing-m_cullOverscan, m_snowCeiling-m_boxDimensions);
			mmbox.MaxCorner.Set((originX + halfX)*m_emitterSpacing+m_cullOverscan, (originY + halfY)*m_emitterSpacing+m_cullOverscan, m_snowCeiling);
			if (CollisionMath::Overlap_Test(camera.Get_Frustum(),mmbox) != CollisionMath::OUTSIDE)
				renderSubBox(rinfo, originX,originY,originX + halfX, originY + halfY);
			//Lower right
			mmbox.MinCorner.Set((originX + halfX)*m_emitterSpacing-m_cullOverscan, originY*m_emitterSpacing-m_cullOverscan, m_snowCeiling-m_boxDimensions);
			mmbox.MaxCorner.Set(cubeDimX*m_emitterSpacing+m_cullOverscan, (originY + halfY)*m_emitterSpacing+m_cullOverscan, m_snowCeiling);
			if (CollisionMath::Overlap_Test(camera.Get_Frustum(),mmbox) != CollisionMath::OUTSIDE)
				renderSubBox(rinfo, originX + halfX, originY, cubeDimX, originY + halfY);
			return;
		}
		else
		{	//only subdivide in x direction.
			//Left
			mmbox.MinCorner.Set(originX*m_emitterSpacing-m_cullOverscan, originY*m_emitterSpacing-m_cullOverscan, m_snowCeiling-m_boxDimensions);
			mmbox.MaxCorner.Set((originX + halfX)*m_emitterSpacing+m_cullOverscan, cubeDimY*m_emitterSpacing+m_cullOverscan, m_snowCeiling);
			if (CollisionMath::Overlap_Test(camera.Get_Frustum(),mmbox) != CollisionMath::OUTSIDE)
				renderSubBox(rinfo, originX, originY, originX + halfX, cubeDimY);
			//Right
			mmbox.MinCorner.Set((originX + halfX)*m_emitterSpacing-m_cullOverscan, originY*m_emitterSpacing-m_cullOverscan, m_snowCeiling-m_boxDimensions);
			mmbox.MaxCorner.Set(cubeDimX*m_emitterSpacing+m_cullOverscan, cubeDimY*m_emitterSpacing+m_cullOverscan, m_snowCeiling);
			if (CollisionMath::Overlap_Test(camera.Get_Frustum(),mmbox) != CollisionMath::OUTSIDE)
				renderSubBox(rinfo, originX + halfX, originY, cubeDimX, cubeDimY);
			return;
		}
	}
	else
	if (boxDimY > m_leafDim)
	{	//only subdivide in y direction
		//Top
		mmbox.MinCorner.Set(originX*m_emitterSpacing-m_cullOverscan, (originY+halfY)*m_emitterSpacing-m_cullOverscan, m_snowCeiling-m_boxDimensions);
		mmbox.MaxCorner.Set(cubeDimX*m_emitterSpacing+m_cullOverscan, cubeDimY*m_emitterSpacing+m_cullOverscan, m_snowCeiling);
		if (CollisionMath::Overlap_Test(camera.Get_Frustum(),mmbox) != CollisionMath::OUTSIDE)
			renderSubBox(rinfo, originX, originY+halfY,cubeDimX, cubeDimY);
		//Bottom
		mmbox.MinCorner.Set(originX*m_emitterSpacing-m_cullOverscan, originY*m_emitterSpacing-m_cullOverscan, m_snowCeiling-m_boxDimensions);
		mmbox.MaxCorner.Set(cubeDimX*m_emitterSpacing+m_cullOverscan, (originY + halfY)*m_emitterSpacing+m_cullOverscan, m_snowCeiling);
		if (CollisionMath::Overlap_Test(camera.Get_Frustum(),mmbox) != CollisionMath::OUTSIDE)
			renderSubBox(rinfo, originX, originY, cubeDimX, originY + halfY);
		return;
	}

	//Box too small to subdivide so render it.

	//Find total number of particles that need rendering.
	Int totalPart=(cubeDimY-originY)*(cubeDimX-originX);

	if (!totalPart)
		return;	//nothing to render.

	Int y=originY;	//loop counter.
	Int cubeOriginXRemainder = originX;	//loop counter - adjusted when not all particles fit into render buffer.
	Vector3 snowCenter;

	m_totalRendered += totalPart;

	while (totalPart)
	{
		Int batchSize=totalPart;

		if (batchSize > m_dwFlush)
			batchSize = m_dwFlush;

		if((m_dwBase + batchSize) > m_dwDiscard)
			m_dwBase = 0;

		POINTVERTEX* verts;

		if(m_VertexBufferD3D->Lock(m_dwBase * sizeof(POINTVERTEX), batchSize * sizeof(POINTVERTEX),
			(void **) &verts, m_dwBase ? D3DLOCK_NOOVERWRITE : D3DLOCK_DISCARD) != D3D_OK )
			return;	//couldn't lock buffer.

		Int numberInBatch=0;

		for (;y<cubeDimY; y++)
		{
			for (Int x=cubeOriginXRemainder; x<cubeDimX; x++)
			{
				if (numberInBatch >= batchSize)
				{	cubeOriginXRemainder = x;
					goto flush_particles;
				}

				//Get initial height from noise table.  We add a large value to make sure it's positive.  Then
				//modulate by table dimensions to find a value.
				Int noiseOffset=MODPOW2(x+MAXIMUM_CAMERA_DISTANCE,SNOW_NOISE_X)+MODPOW2(y+MAXIMUM_CAMERA_DISTANCE,SNOW_NOISE_Y)*SNOW_NOISE_X;
				if (noiseOffset > (SNOW_NOISE_X * SNOW_NOISE_Y))
					noiseOffset = 0;	//this should never happen but check to prevent buffer over/under flow.

				//find current height
				Real h0=m_snowCeiling-fmod(m_heightTraveled+m_startingHeights[noiseOffset],m_boxDimensions);

				//find world-space position of snow flake
				snowCenter.Set(x*m_emitterSpacing,y*m_emitterSpacing,h0);

				//Adjust position so snow flakes don't fall straight down.
				snowCenter.X += m_amplitude * WWMath::Fast_Sin( h0 * m_frequencyScaleX + (Real)x); 
				snowCenter.Y += m_amplitude * WWMath::Fast_Sin( h0 * m_frequencyScaleY + (Real)y); 

				*(Vector3 *)verts=snowCenter;
				verts++;

				numberInBatch++;
			}
			//getting here means we did not overflow the render buffer, so reset x origin to normal.
			cubeOriginXRemainder = originX;	//reset to normal amount
		}

flush_particles:
		m_VertexBufferD3D->Unlock();
		//Render any particles that may be queued up.
		if (numberInBatch)
		{
			Debug_Statistics::Record_DX8_Polys_And_Vertices(numberInBatch*2,numberInBatch*4,ShaderClass::_PresetOpaqueShader);
			DX8Wrapper::_Get_D3D_Device()->DrawPrimitive( D3DPT_POINTLIST, m_dwBase, numberInBatch);
			totalPart -= numberInBatch;
			m_dwBase += numberInBatch;
		}
	}
}

void W3DSnowManager::render(RenderInfoClass &rinfo)
{
	if (m_weatherKind != WEATHER_KIND_NONE && m_isVisible)
	{
		renderWeather(rinfo);
		return;
	}

	if (!TheWeatherSetting->m_snowEnabled || !m_isVisible)
		return;

	Int usePointSprites = snowUsesPointSprites();

	//make sure the noise table is powers of 2 in dimensions.
	WWASSERT(ISPOW2(SNOW_NOISE_X) && ISPOW2(SNOW_NOISE_Y));

	//CameraClass &camera=rinfo.Camera;

	const Coord3D &cPos=TheTacticalView->get3DCameraPosition();
	Vector3 camPos(cPos.x,cPos.y,cPos.z);

	//Number of emitters from cube center to edge of visible extent.
	Int mumEmittersInHalf=(Int)floor(m_boxDimensions / m_emitterSpacing * 0.5f);

	//Find origin of visible cube surrounding camera.
	Int cubeCenterX=(Int)floor(camPos.X/m_emitterSpacing);
	Int cubeCenterY=(Int)floor(camPos.Y/m_emitterSpacing);

	//Find extents of visible cube surrounding camera.
	Int cubeOriginX=cubeCenterX - mumEmittersInHalf;	//top/left extents.
	Int cubeOriginY=cubeCenterY - mumEmittersInHalf;
	Int cubeDimX=cubeCenterX + mumEmittersInHalf;		//bottom/right extents.
	Int cubeDimY=cubeCenterY + mumEmittersInHalf;

 	const FrustumClass & frustum = rinfo.Camera.Get_Frustum();
	AABoxClass bbox;

	//Get a bounding box around our visible universe.  Bounded by terrain and the sky
	//so much tighter fitting volume than what's actually visible.  This will cull
	//particles falling under the ground.

 	TheTerrainRenderObject->getMaximumVisibleBox(frustum, &bbox, TRUE);

	//Particles move outside the visible box as a result of local sine movement
	//so adjust bounding box to include them.
	bbox.Extent.X += m_amplitude+m_quadSize;
	bbox.Extent.Y += m_amplitude+m_quadSize;

	//Clip our visible snow rendering box
	if ((cubeOriginX * m_emitterSpacing ) < (bbox.Center.X - bbox.Extent.X))
		cubeOriginX = (Int)floor ((bbox.Center.X - bbox.Extent.X)/m_emitterSpacing);

	if ((cubeOriginY * m_emitterSpacing ) < (bbox.Center.Y - bbox.Extent.Y))
		cubeOriginY = (Int)floor ((bbox.Center.Y - bbox.Extent.Y)/m_emitterSpacing);

	if ((cubeDimX * m_emitterSpacing ) > (bbox.Center.X + bbox.Extent.X))
		cubeDimX = (Int)floor ((bbox.Center.X + bbox.Extent.X)/m_emitterSpacing);

	if ((cubeDimY * m_emitterSpacing ) > (bbox.Center.Y + bbox.Extent.Y))
		cubeDimY = (Int)floor ((bbox.Center.Y + bbox.Extent.Y)/m_emitterSpacing);

	if ((cubeDimY - cubeOriginY) < 0 || (cubeDimX-cubeOriginX) < 0)
		return;	//entire snow box is culled by either x or y screen boundary.

	//Find total number of particles that need rendering.
	Int totalPart=(cubeDimY-cubeOriginY)*(cubeDimX-cubeOriginX);

	if (totalPart <= 0)
		return;	//nothing to render.

	//Height at the top of the cube with camera at center.
	m_snowCeiling = camPos.Z + m_boxDimensions/2.0f;

	//Offset to allow cube extents to move with camera.
	Real cameraOffset = fmod (camPos.Z,m_boxDimensions);
	m_heightTraveled=m_time*m_velocity+cameraOffset;	//height that snow flake traveled this frame.

	Matrix4x4 identity(true);
	DX8Wrapper::Set_Transform(D3DTS_WORLD,identity);	

	DX8Wrapper::Set_Shader(ShaderClass::_PresetAlphaShader);

	VertexMaterialClass *vmat=VertexMaterialClass::Get_Preset(VertexMaterialClass::PRELIT_DIFFUSE);
	DX8Wrapper::Set_Material(vmat);
	REF_PTR_RELEASE(vmat);

	//make sure we have all the resources we need
	if (usePointSprites && !m_VertexBufferD3D)
		ReAcquireResources();

	if (!usePointSprites && !m_indexBuffer)
		ReAcquireResources();

	DX8Wrapper::Set_Texture(0,m_snowTexture);

	if (!usePointSprites)
	{	
		renderAsQuads(rinfo,cubeOriginX,cubeOriginY,cubeDimX,cubeDimY);
		return;
	}

	Vector3 snowCenter;

	DX8Wrapper::Apply_Render_State_Changes();

    // Set the render states for using point sprites
	DX8Wrapper::Set_DX8_Render_State( D3DRS_POINTSPRITEENABLE, TRUE );
    DX8Wrapper::Set_DX8_Render_State( D3DRS_POINTSCALEENABLE,  TRUE );
    DX8Wrapper::Set_DX8_Render_State( D3DRS_POINTSIZE,     FtoDW(m_pointSize) );
    DX8Wrapper::Set_DX8_Render_State( D3DRS_POINTSIZE_MIN, FtoDW(m_minPointSize) );
    DX8Wrapper::Set_DX8_Render_State( D3DRS_POINTSIZE_MAX, FtoDW(m_maxPointSize) );
    DX8Wrapper::Set_DX8_Render_State( D3DRS_POINTSCALE_A,  FtoDW(0.00f) );
    DX8Wrapper::Set_DX8_Render_State( D3DRS_POINTSCALE_B,  FtoDW(0.00f) );
    DX8Wrapper::Set_DX8_Render_State( D3DRS_POINTSCALE_C,  FtoDW(1.00f) );

	DX8Wrapper::_Get_D3D_Device()->SetStreamSource( 0, m_VertexBufferD3D, 0, sizeof(POINTVERTEX) );
    DX8Wrapper::_Get_D3D_Device()->SetFVF( D3DFVF_POINTVERTEX );
	m_dwBase = SNOW_BUFFER_SIZE;	//start with a new vertex buffer each frame.

	m_leafDim = 45;	//cull boxes that are 20x20 emitters in size. Making them much smaller will result in too many draw calls.
	m_totalRendered = 0;	//keep track of how many particles were rendered.

	//Particle centers can deviate from center by by amplitude of sine offset.  They also have radius m_quadSize.
	//Enlarge culling bounds to compensate.
	m_cullOverscan = m_amplitude+m_quadSize;
	renderSubBox(rinfo,cubeOriginX,cubeOriginY,cubeDimX,cubeDimY);

	// Reset render states
    DX8Wrapper::Set_DX8_Render_State( D3DRS_POINTSPRITEENABLE, FALSE );
    DX8Wrapper::Set_DX8_Render_State( D3DRS_POINTSCALEENABLE,  FALSE );

}

/**For hardware that doesn't support point sprites*/
void W3DSnowManager::renderAsQuads(RenderInfoClass &rinfo, Int cubeOriginX, Int cubeOriginY, Int cubeDimX, Int cubeDimY)
{

	Matrix4x4 proj;
	Matrix3D view;
	Vector3 snowCenter;
	Vector3 snowCenterVS;

	CameraClass &camera=rinfo.Camera;

	camera.Get_View_Matrix(&view);
	camera.Get_Projection_Matrix(&proj);

	Vector3 vertex_offsets[4] = {
		Vector3(-0.5f, 0.5f, 0.0f),
		Vector3(-0.5f, -0.5f, 0.0f),
		Vector3(0.5f, -0.5f, 0.0f),
		Vector3(0.5f, 0.5f, 0.0f)
	};

	Vector2 quad_uvs[4] = {
		Vector2(0.0f, 0.0f),
		Vector2(0.0f, 1.0f),
		Vector2(1.0f, 1.0f),
		Vector2(1.0f, 0.0f)
	};


	// A snowy map tunes its flakes for point sprites: SnowPointSize with a SnowMaxPointSize of 10 or
	// 16 pixels, and SnowQuadSize mostly left at the 0.5 default.  Taken as a world size, that 0.5
	// grows without limit on a flake next to the camera, which is how D3D11 drew snowflakes the size
	// of a building.  When the map asked for sprites the quad gets the size D3D9 gave the sprite:
	// SnowPointSize * viewport height / eye distance pixels, clamped, turned back into view units.
	Bool sizeLikePointSprites = snowWantsPointSprites();
	Int deviceWidth, deviceHeight, deviceBits;
	Bool deviceWindowed;
	WW3D::Get_Device_Resolution(deviceWidth, deviceHeight, deviceBits, deviceWindowed);
	Real viewportHeight = (Real)deviceHeight;

	Matrix4x4 identity(true);
	DX8Wrapper::Set_Transform(D3DTS_VIEW,identity);	

	DX8Wrapper::Set_Index_Buffer(m_indexBuffer,0);

	Int y=cubeOriginY;	//loop counter.
	Int cubeOriginXRemainder = cubeOriginX;	//loop counter - adjusted when not all particles fit into render buffer.

	//Find total number of particles that need rendering.
	Int totalPart=(cubeDimY-cubeOriginY)*(cubeDimX-cubeOriginX);

	m_totalRendered += totalPart;

	while (totalPart)
	{
		Int batchSize=totalPart;

		if (batchSize > SNOW_BATCH_SIZE)
			batchSize = SNOW_BATCH_SIZE;

		Int numberInBatch=0;

		DynamicVBAccessClass vb_access(BUFFER_TYPE_DYNAMIC_DX8,dynamic_fvf_type,batchSize*4);	//allocate 4 verts per flake
		{
			DynamicVBAccessClass::WriteLockClass lock(&vb_access);
			VertexFormatXYZNDUV2* verts=lock.Get_Formatted_Vertex_Array();

			for (;y<cubeDimY; y++)
			{
				for (Int x=cubeOriginXRemainder; x<cubeDimX; x++)
				{
					if (numberInBatch >= batchSize)
					{	cubeOriginXRemainder = x;
						goto flush_particles;
					}

					//Get initial height from noise table.  We add a large value to make sure it's positive.  Then
					//modulate by table dimensions to find a value.
					Int noiseOffset=MODPOW2(x+MAXIMUM_CAMERA_DISTANCE,SNOW_NOISE_X)+MODPOW2(y+MAXIMUM_CAMERA_DISTANCE,SNOW_NOISE_Y)*SNOW_NOISE_X;
					if (noiseOffset > (SNOW_NOISE_X * SNOW_NOISE_Y))
						noiseOffset = 0;	//this should never happen but check to prevent buffer over/under flow.

					//find current height
					Real h0=m_snowCeiling-fmod(m_heightTraveled+m_startingHeights[noiseOffset],m_boxDimensions);

					//find world-space position of snow flake
					snowCenter.Set(x*m_emitterSpacing,y*m_emitterSpacing,h0);

					//Get view-space position
					Matrix3D::Transform_Vector(view,snowCenter,&snowCenterVS);

					//Adjust position so snow flakes don't fall straight down.
					snowCenterVS.X += m_amplitude * WWMath::Fast_Sin( h0 * m_frequencyScaleX + (Real)x);
					snowCenterVS.Y += m_amplitude * WWMath::Fast_Sin( h0 * m_frequencyScaleY + (Real)y);

					Real flakeSize = m_quadSize;
					if (sizeLikePointSprites)
					{
						Real flakePixels = WWMath::Clamp(m_pointSize * viewportHeight / snowCenterVS.Length(), m_minPointSize, m_maxPointSize);
						flakeSize = flakePixels * 2.0f * -snowCenterVS.Z / (proj[1][1] * viewportHeight);
					}

					for (Int i=0; i<4; i++)
					{
						*(Vector3 *)verts=snowCenterVS + vertex_offsets[i] * flakeSize;
						verts->nx=0;	//keep AGP write-combining active
						verts->ny=0;
						verts->nz=0;
						verts->diffuse=0xffffffff;	//set to opaque
						verts->u1=quad_uvs[i].X;
						verts->v1=quad_uvs[i].Y;
						verts->u2=0;	//keep AGP write-combining active
						verts->v2=0;
						verts++;
					}

					numberInBatch++;
				}
				//getting here means we did not overflow the render buffer, so reset x origin to normal.
				cubeOriginXRemainder = cubeOriginX;	//reset to normal amount
			}
flush_particles:
			numberInBatch;	//need something at goto destination - stupid c compiler.
		}

		//Render any particles that may be queued up.
		if (numberInBatch)
		{
			DX8Wrapper::Set_Vertex_Buffer(vb_access);
			DX8Wrapper::Draw_Triangles(	0,numberInBatch*2, 0, numberInBatch*4);	
			totalPart -= numberInBatch;
		}
	}
}