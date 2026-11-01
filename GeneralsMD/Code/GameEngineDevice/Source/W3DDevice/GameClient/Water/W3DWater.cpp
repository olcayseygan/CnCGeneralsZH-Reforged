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

// FILE: W3DWater.cpp /////////////////////////////////////////////////////////////////////////////
// Created:   Mark Wilczynski, June 2001
// Desc:      Draw reflective water surface.  Also handles drawing of waves/ripples
//			  on the surface.
///////////////////////////////////////////////////////////////////////////////////////////////////

#define SCROLL_UV

// INCLUDES ///////////////////////////////////////////////////////////////////////////////////////
#include "stdio.h"
#include "Lib/Clock.h"
#include "Platform/WaterSurfaceModel.h"
#include "Platform/ModernWaterMaterial.h"
#include "Platform/WaterShoreOverlay.h"
#include "Platform/WaterFoamWindow.h"
#include "Platform/WaterFoamParticlePolicy.h"
#include "Platform/WaterHullContact.h"
#include "Platform/WaterBedMaterial.h"
#include "Platform/WaterFftGeometry.h"
#include "Platform/WaterTerrainCulling.h"
#include "Platform/WaterWaveCage.h"
#include "Platform/WaterWaveCageMesh.h"
#include "GameLogic/Object.h"
#include "GameLogic/TerrainLogic.h"
#include "GameLogic/Module/PhysicsUpdate.h"
#include "GameClient/Drawable.h"
#include "W3DDevice/GameClient/Module/W3DModelDraw.h"
#include "W3DDevice/GameClient/W3DWater.h"
#include "W3DDevice/GameClient/HeightMap.h"
#include "W3DDevice/GameClient/W3DShroud.h"
#include "W3DDevice/GameClient/W3DWaterTracks.h"
#include "W3DDevice/GameClient/W3DAssetManager.h"
#include "texture.h"
#include "assetmgr.h"
#include "rinfo.h"
#include "camera.h"
#include "scene.h"
#include "dx8wrapper.h"
#include "light.h"
#include "d3dx9math.h"
#include "d3d8shadertranslate.h"
#include "dx11twin.h"
#include "simplevec.h"
#include "mesh.h"
#include "meshmdl.h"
#include "hlod.h"
#include "matinfo.h"

#include "Common/GameState.h"
#include "Common/GameEngine.h"
#include "Common/GlobalData.h"
#include "Common/PerfTimer.h"
#include "Common/Xfer.h"
#include "Common/GameLOD.h"

#include "GameClient/Water.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/PolygonTrigger.h"
#include "GameLogic/ScriptEngine.h"
#include "GameLogic/TerrainLogic.h"
#include "W3DDevice/GameClient/W3DShaderManager.h"
#include "W3DDevice/GameClient/W3DDisplay.h"
#include "W3DDevice/GameClient/W3DPoly.h"
#include "W3DDevice/GameClient/W3DScene.h"
#include "W3DDevice/GameClient/W3DCustomScene.h"

#ifdef _INTERNAL
// for occasional debugging...
//#pragma optimize("", off)
//#pragma MESSAGE("************************************** WARNING, optimization disabled for debugging purposes")
#endif

#define MIPMAP_BUMP_TEXTURE

// DEFINES ////////////////////////////////////////////////////////////////////////////////////////
#define SKYPLANE_SIZE	(384.0f*MAP_XY_FACTOR)
#define SKYPLANE_HEIGHT	(30.0f)

#define SKYBODY_TEXTURE	"TSMoonLarg.tga"
#define SKYBODY_SIZE	45.0f		//extent or radius of sky body

#define SKYBODY_X	150.0f	//location of skybody
#define SKYBODY_Y	550.0f	//location of skybody

/* in the bay
#define SKYBODY_X	120.0f			//location of skybody
#define SKYBODY_Y	75.0f			//location of skybody
*/

#define SKYBODY_HEIGHT	SKYPLANE_HEIGHT	//altitude of sky body (z-buffer disabled, so can equal sky height).

//GeForce3 water system defines
#define PATCH_SIZE 15		//number of vertices on patch edge.  Large patches may waste vertices off edge of screen.
#define PATCH_UV_TILES	42	//number of times the bump map texture is tiled across patch (must be integer!).
#define PATCH_SCALE (4.0f * MAP_XY_FACTOR)	//horizontal scale factor. Adjust this and size to get desired vertex density.
#define SEA_REFLECTION_SIZE 256		//dimensions of reflection texture
#define WATER_REFLECTION_SIZE 512	//dimensions of the reflection texture laid over map water
#define WATER_REFLECTION_AREA_MARGIN 0.05f	//slack around the water's screen area, so filtering at its edge reads inside it
#define WATER_REFLECTION_STRENGTH 0.55f	//how far a reflected object darkens the water under it
#define WATER_REFLECTION_SKY_COLOR Vector3(1.0f,1.0f,1.0f)	//white, so where nothing stands the water keeps its own colour

#define SEA_BUMP_SCALE		(0.06f)		//scales the du/dv offsets stored in bump map (~ amount to perturb)
#define BUMP_SIZE (50.f)
#define REFLECTION_FACTOR 0.1f

#define PATCH_WIDTH (PATCH_SIZE-1)	//internal defines
#define PATCH_UV_SCALE	((Real)PATCH_UV_TILES/(Real)PATCH_WIDTH)	

//3D Grid Mesh Water defines.
#define WATER_MESH_OPACITY		0.5f
#define WATER_MESH_X_VERTICES	128
#define WATER_MESH_Y_VERTICES	128
#define WATER_MESH_SPACING	MAP_XY_FACTOR	//same as terrain

#ifdef USE_MESH_NORMALS
#define WATER_MESH_FVF	DX8_FVF_XYZNDUV2
typedef VertexFormatXYZNDUV2 MaterMeshVertexFormat;
#else
#define WATER_MESH_FVF	DX8_FVF_XYZDUV2
typedef VertexFormatXYZDUV2 MaterMeshVertexFormat;
#endif

// Converts a FLOAT to a DWORD for use in SetRenderState() calls
static inline UnsignedInt F2DW( float f ) { return *((UnsignedInt*)&f); }

//The Direct3D 11 copies of m_vertexBufferD3D and m_indexBufferD3D, null on a run without -dx11.
//Both members are created with CreateVertexBuffer/CreateIndexBuffer straight on the device rather
//than through DX8VertexBufferClass, so they get none of the mirroring that class does.  The wave
//grid and the mesh water share the two members and so share these two twins; whichever of the pair
//is built last owns them.
static DX11BufferTwinClass *waterVertexTwin=NULL;
static DX11BufferTwinClass *waterIndexTwin=NULL;

static void releaseWaterTwins(void)
{
	delete waterVertexTwin;
	waterVertexTwin=NULL;
	delete waterIndexTwin;
	waterIndexTwin=NULL;
}

#define DRAW_WATER_WAKES
/// @todo: Fix clipping of objects that intersect the mirror surface
//#define CLIP_GEOMETRY_TO_PLANE	// this enables clipping of objects that intersect the mirror surfaces

// Some shader combinations that can be useful in rendering water:

// Modulate stage0 with stage1 texture.  Also modulate stage 0 with vertex color.
#define SC_DETAIL_BLEND ( SHADE_CNST(ShaderClass::PASS_LEQUAL, ShaderClass::DEPTH_WRITE_ENABLE, ShaderClass::COLOR_WRITE_ENABLE,\
	ShaderClass::SRCBLEND_SRC_ALPHA,ShaderClass::DSTBLEND_ONE_MINUS_SRC_ALPHA, ShaderClass::FOG_DISABLE, ShaderClass::GRADIENT_MODULATE, ShaderClass::SECONDARY_GRADIENT_DISABLE, \
	ShaderClass::TEXTURING_ENABLE, 	ShaderClass::ALPHATEST_DISABLE, ShaderClass::CULL_MODE_ENABLE, ShaderClass::DETAILCOLOR_DETAILBLEND, ShaderClass::DETAILALPHA_DISABLE) )

// Just a z-buffer fill, nothing is written to the color buffer.
#define SC_ZFILL_BLEND ( SHADE_CNST(ShaderClass::PASS_LEQUAL, ShaderClass::DEPTH_WRITE_ENABLE, ShaderClass::COLOR_WRITE_DISABLE, ShaderClass::SRCBLEND_ZERO, \
	ShaderClass::DSTBLEND_ONE, ShaderClass::FOG_DISABLE, ShaderClass::GRADIENT_MODULATE, ShaderClass::SECONDARY_GRADIENT_DISABLE, ShaderClass::TEXTURING_ENABLE, \
	ShaderClass::DETAILCOLOR_SCALE, ShaderClass::DETAILALPHA_DISABLE, ShaderClass::ALPHATEST_DISABLE, ShaderClass::CULL_MODE_ENABLE, \
	ShaderClass::DETAILCOLOR_SCALE, ShaderClass::DETAILALPHA_DISABLE) )

// No texturing, just vertex color with vertex alpha
#define SC_ZFILL_BLENDx ( SHADE_CNST(ShaderClass::PASS_LEQUAL, ShaderClass::DEPTH_WRITE_ENABLE, ShaderClass::COLOR_WRITE_ENABLE, \
	ShaderClass::SRCBLEND_ZERO, ShaderClass::DSTBLEND_SRC_COLOR, ShaderClass::FOG_DISABLE, ShaderClass::GRADIENT_MODULATE, ShaderClass::SECONDARY_GRADIENT_DISABLE, \
	ShaderClass::TEXTURING_DISABLE, ShaderClass::DETAILCOLOR_DISABLE, ShaderClass::DETAILALPHA_DISABLE, ShaderClass::ALPHATEST_DISABLE, ShaderClass::CULL_MODE_ENABLE, \
	ShaderClass::DETAILCOLOR_DISABLE, ShaderClass::DETAILALPHA_DISABLE) )

// Modulate blended with vertex alpha modulation
#define SC_ZFILL_MODULATE_TEX ( SHADE_CNST(ShaderClass::PASS_LEQUAL, ShaderClass::DEPTH_WRITE_ENABLE, ShaderClass::COLOR_WRITE_ENABLE,\
	ShaderClass::SRCBLEND_ZERO, ShaderClass::DSTBLEND_SRC_COLOR, ShaderClass::FOG_DISABLE, ShaderClass::GRADIENT_MODULATE, ShaderClass::SECONDARY_GRADIENT_DISABLE, \
	ShaderClass::TEXTURING_ENABLE, ShaderClass::ALPHATEST_DISABLE, ShaderClass::CULL_MODE_DISABLE, ShaderClass::DETAILCOLOR_DISABLE, ShaderClass::DETAILALPHA_DISABLE) )

// Alpha blended with vertex alpha modulation
#define SC_ZFILL_ALPHA_TEX ( SHADE_CNST(ShaderClass::PASS_LEQUAL, ShaderClass::DEPTH_WRITE_ENABLE, ShaderClass::COLOR_WRITE_ENABLE,\
	ShaderClass::SRCBLEND_SRC_ALPHA, ShaderClass::DSTBLEND_ONE_MINUS_SRC_ALPHA, ShaderClass::FOG_DISABLE, ShaderClass::GRADIENT_DISABLE, ShaderClass::SECONDARY_GRADIENT_DISABLE, \
	ShaderClass::TEXTURING_ENABLE, ShaderClass::ALPHATEST_DISABLE, ShaderClass::CULL_MODE_DISABLE, ShaderClass::DETAILCOLOR_DISABLE, ShaderClass::DETAILALPHA_DISABLE) )

// Alpha blended with vertex alpha modulation
#define SC_OPAQUE_TEXONLY ( SHADE_CNST(ShaderClass::PASS_LEQUAL, ShaderClass::DEPTH_WRITE_ENABLE, ShaderClass::COLOR_WRITE_ENABLE,\
	ShaderClass::SRCBLEND_ONE, ShaderClass::DSTBLEND_ZERO, ShaderClass::FOG_DISABLE, ShaderClass::GRADIENT_DISABLE, ShaderClass::SECONDARY_GRADIENT_DISABLE, \
	ShaderClass::TEXTURING_ENABLE, ShaderClass::ALPHATEST_DISABLE, ShaderClass::CULL_MODE_DISABLE, ShaderClass::DETAILCOLOR_DISABLE, ShaderClass::DETAILALPHA_DISABLE) )

// Alpha blended with vertex alpha modulation
#define SC_ZFILL_BLEND3 ( SHADE_CNST(ShaderClass::PASS_LEQUAL, ShaderClass::DEPTH_WRITE_ENABLE, ShaderClass::COLOR_WRITE_ENABLE,\
	ShaderClass::SRCBLEND_SRC_ALPHA, ShaderClass::DSTBLEND_ONE_MINUS_SRC_ALPHA, ShaderClass::FOG_DISABLE, ShaderClass::GRADIENT_MODULATE, ShaderClass::SECONDARY_GRADIENT_DISABLE, \
	ShaderClass::TEXTURING_ENABLE, ShaderClass::ALPHATEST_DISABLE, ShaderClass::CULL_MODE_DISABLE, ShaderClass::DETAILCOLOR_DISABLE, ShaderClass::DETAILALPHA_DISABLE) )

static ShaderClass zFillAlphaShader(SC_ZFILL_BLEND3);
static ShaderClass blendStagesShader(SC_DETAIL_BLEND);

WaterRenderObjClass *TheWaterRenderObj=NULL; ///<global water rendering object

#define SAFE_RELEASE(p)      { if(p) { (p)->Release(); (p)=NULL; } }

void doSkyBoxSet(Bool startDraw)
{
	if (TheWritableGlobalData)
		TheWritableGlobalData->m_drawSkyBox = startDraw;
}

#define DONUT_SIDES	90
#define INNER_RADIUS 200.0f
#define OUTER_RADIUS 250.0f
#define TEXTURE_REPEAT_COUNT 16
#define DONUT_HEIGHT	15.0f
//#define DO_FLAT_DONUT
#define AMP_SCALE	(30.0f/120.0f)
#define WAVE_FREQ	0.3f
#define AMP_SCALE2	(10.0f/120.0f)
#define NOISE_FREQ	(2.0f*PI/WAVE_FREQ)

#define NOISE_REPEAT_FACTOR ((float)(1.0f/(16.0f)))	//the foam programs undo this (TexCoord2 * 16) to find the world
static const Real WATER_SCROLL_SPEED = 0.2f;
static const Real AUTHORED_FPS = 30.0f;

static Bool wireframeForDebug = 0;

//
// The Water Foam option (WaterFoam): whitecap flecks, a foam band and a soft blend where the
// water gets shallow, and the sun's shadow on the surface.  It is the D3D11 water program under a
// second name (engineshader.cpp, "water foam"); the D3D9 bytecode is the same, so -d3d9 and the
// Classic setting, which has no sun's map to take the pixel back to the world through, keep the
// plain water and its destination alpha feather.  It dresses the Classic water only: the Modern
// style draws the plain names with a text of its own, which the foam program would replace.
//
#define WATER_FOAM_CYCLE 10.0f	//swell cycles per unit of m_riverVOrigin: about eight seconds a cycle

static Bool waterFoam(void)
{
	return TheGlobalData->m_waterFoam && !TheGlobalData->m_classicGraphics
		&& TheGlobalData->m_waterStyle == 0 && Direct3D11_Present_Is_Enabled();
}

// What the foam programs read out of the texture factor: green is the swell's phase, alpha the
// opacity deep water reaches and red the depth that reaches it over 32.  Alpha zero keeps the
// water's own alpha, which is every water body that is not feathered.
static UnsignedInt waterFoamFactor(Real riverVOrigin, Real opaqueDepth, Real deepOpacity)
{
	const Real phase = fmodf(riverVOrigin * WATER_FOAM_CYCLE, 1.0f);
	const Int depth = REAL_TO_INT(WWMath::Clamp(opaqueDepth / 32.0f, 0.0f, 1.0f) * 255.0f);
	const Int opacity = REAL_TO_INT(WWMath::Clamp(deepOpacity, 0.0f, 1.0f) * 255.0f);
	return ((UnsignedInt)opacity << 24) | ((UnsignedInt)depth << 16) | ((UnsignedInt)REAL_TO_INT(phase * 255.0f) << 8);
}

//
// The river used to get its shroud from a second pass that redrew the whole quad through
// ST_SHROUD_TEXTURE.  That pass multiplies the frame buffer by the shroud and knows nothing about
// the river's own edge alpha (m_riverAlphaEdge, stage 3), so along the banks - where the river is
// nearly transparent and the terrain underneath has already been shrouded once by the terrain pass
// - the shroud landed twice and the shore came out as a dark stripe.  In full black shroud the
// opposite showed: the river's alpha blend let a little brightness through where there should be
// none.
//
// So the shroud goes into the vertex colour instead, and the second pass goes away.  Both the
// colour and the alpha are scaled: the pixel shader multiplies the water texture by the colour and
// the additive terms (sparkles, edge glow) by the alpha, so a fully shrouded river is black rather
// than transparent.  One sample per river vertex is coarser than per-pixel, but the shroud is a
// low-resolution grid to begin with and a river carries a vertex pair per waypoint.
//
static Int applyShroudToRiverDiffuse(W3DShroud *shroud, Real x, Real y, Int diffuse)
{
	if (shroud == NULL)
		return diffuse;

	const Int cellX = REAL_TO_INT_FLOOR( x / shroud->getCellWidth() );
	const Int cellY = REAL_TO_INT_FLOOR( y / shroud->getCellHeight() );
	const Int level = shroud->getShroudLevel( cellX, cellY );	// 0 = black, 255 = clear

	if (level >= 255)
		return diffuse;

	return ((((diffuse >> 24) & 0xff) * level / 255) << 24) |
				 ((((diffuse >> 16) & 0xff) * level / 255) << 16) |
				 ((((diffuse >>  8) & 0xff) * level / 255) <<  8) |
				 (  ((diffuse        & 0xff) * level / 255)       );
}

void WaterRenderObjClass::setupJbaWaterShader(void) 
{
	if (!TheWaterTransparency->m_additiveBlend)
		DX8Wrapper::Set_Shader(ShaderClass::_PresetAlphaShader);
	else
		DX8Wrapper::Set_Shader(ShaderClass::_PresetAdditiveShader);

	VertexMaterialClass *vmat=VertexMaterialClass::Get_Preset(VertexMaterialClass::PRELIT_DIFFUSE);
	DX8Wrapper::Set_Material(vmat);
	REF_PTR_RELEASE(vmat);
	m_riverTexture->Get_Filter().Set_Mag_Filter(TextureFilterClass::FILTER_TYPE_BEST);
	m_riverTexture->Get_Filter().Set_Min_Filter(TextureFilterClass::FILTER_TYPE_BEST);
	m_riverTexture->Get_Filter().Set_Mip_Mapping(TextureFilterClass::FILTER_TYPE_BEST);

//	Setting *setting=&m_settings[m_tod];	

	DX8Wrapper::Apply_Render_State_Changes();	//force update of view and projection matrices
	DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_ALPHAOP,   D3DTOP_ADD );
	if (!m_riverAlphaEdge->Is_Initialized())
		m_riverAlphaEdge->Init();
	DX8Wrapper::Set_DX8_Texture(3,m_riverAlphaEdge->Peek_D3D_Texture());	
	DX8Wrapper::Set_DX8_Texture_Stage_State(3,  D3DSAMP_ADDRESSU, D3DTADDRESS_WRAP);
	DX8Wrapper::Set_DX8_Texture_Stage_State(3,  D3DSAMP_ADDRESSV, D3DTADDRESS_WRAP);
	DX8Wrapper::Set_DX8_Texture_Stage_State(0,  D3DTSS_TEXCOORDINDEX, 0);
	DX8Wrapper::Set_DX8_Texture_Stage_State(1,  D3DTSS_TEXCOORDINDEX, 0);
	DX8Wrapper::Set_DX8_Texture_Stage_State(3,  D3DTSS_TEXCOORDINDEX, 1);

	Bool doSparkles = true;

	if (m_riverWaterPixelShader && doSparkles) {
		if (!m_waterSparklesTexture->Is_Initialized())
			m_waterSparklesTexture->Init();
		DX8Wrapper::Set_DX8_Texture(1,m_waterSparklesTexture->Peek_D3D_Texture());	

		if (!m_waterNoiseTexture->Is_Initialized())
			m_waterNoiseTexture->Init();
		DX8Wrapper::Set_DX8_Texture(2,m_waterNoiseTexture->Peek_D3D_Texture());	

		DX8Wrapper::Set_DX8_Texture_Stage_State(1,  D3DSAMP_ADDRESSU, D3DTADDRESS_WRAP);
		DX8Wrapper::Set_DX8_Texture_Stage_State(1,  D3DSAMP_ADDRESSV, D3DTADDRESS_WRAP);

		DX8Wrapper::Set_DX8_Texture_Stage_State(2,  D3DTSS_TEXCOORDINDEX, D3DTSS_TCI_CAMERASPACEPOSITION);
		// Two output coordinates are used.
		DX8Wrapper::Set_DX8_Texture_Stage_State(2,  D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_COUNT2);	
		DX8Wrapper::Set_DX8_Texture_Stage_State(2,  D3DSAMP_ADDRESSU, D3DTADDRESS_WRAP);
		DX8Wrapper::Set_DX8_Texture_Stage_State(2,  D3DSAMP_ADDRESSV, D3DTADDRESS_WRAP);
		D3DXMATRIX inv;
		float det;

		Matrix4x4 curView;
		DX8Wrapper::_Get_DX8_Transform(D3DTS_VIEW, curView);
		D3DXMatrixInverse(&inv, &det, (D3DXMATRIX*)&curView);
		D3DXMATRIX scale;

		D3DXMatrixScaling(&scale, NOISE_REPEAT_FACTOR, NOISE_REPEAT_FACTOR,1);
		D3DXMATRIX destMatrix = inv * scale;
		D3DXMatrixTranslation(&scale, m_riverVOrigin, m_riverVOrigin,0);
		destMatrix = destMatrix*scale;
		DX8Wrapper::_Set_DX8_Transform(D3DTS_TEXTURE2, *(Matrix4x4*)&destMatrix);

	}
	DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR );
	DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR );
	DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DSAMP_MINFILTER, D3DTEXF_LINEAR );
	DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR );
	DX8Wrapper::Set_DX8_Texture_Stage_State( 2, D3DSAMP_MINFILTER, D3DTEXF_LINEAR );
	DX8Wrapper::Set_DX8_Texture_Stage_State( 2, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR );
	DX8Wrapper::Set_DX8_Texture_Stage_State( 3, D3DSAMP_MINFILTER, D3DTEXF_LINEAR );
	DX8Wrapper::Set_DX8_Texture_Stage_State( 3, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR );
	if (m_riverWaterPixelShader){
		DX8Wrapper::_Get_D3D_Device()->SetPixelShaderConstantF(0,   D3DXVECTOR4(REFLECTION_FACTOR, REFLECTION_FACTOR, REFLECTION_FACTOR, 1.0f), 1);
		DX8Wrapper::Set_Pixel_Shader(m_riverWaterPixelShader);
		if (waterFoam()) {
			DX8Wrapper::Set_DX8_Render_State(D3DRS_TEXTUREFACTOR, waterFoamFactor(m_riverVOrigin, 0.0f, 0.0f));
			DX8Wrapper::Set_Pixel_Shader(m_riverWaterFoamPixelShader);
		}
		setupModernWaterShader(TRUE);
	}
}

// SDL's map water composites the coloured mirror in its surface shader. Windows retains the
// original assembly and its separate reflection pass. No texture assets or game data are changed.
namespace {
struct WaterHullMesh : WaterHullPoseCache::Mesh {
	MeshModelClass *model;
};
static_assert(WaterHullPoseCache::MaxMeshes==WaterHullModel::MaxMeshes,"Hull pose snapshots share the collector bound");
static Bool CollectWaterHullMeshes(RenderObjClass *object,const Vector3 &origin,float axisX,float axisY,
	float level,float crestRise,std::vector<WaterHullMesh> &meshes,unsigned &visited,unsigned depth=0)
{
	if (!object || object->Is_Hidden() || object->Is_Animation_Hidden()) return TRUE;
	if (depth>6 || ++visited>WaterHullModel::MaxMeshes) return FALSE;
	if (object->Class_ID()==RenderObjClass::CLASSID_MESH) {
		MeshModelClass *model=static_cast<MeshClass*>(object)->Peek_Model();
		if (!model || model->Get_Flag(MeshGeometryClass::SKIN) || object->Is_Additive()) return TRUE;
		const AABoxClass &bounds=object->Get_Bounding_Box();
		if (level+crestRise<bounds.Center.Z-bounds.Extent.Z-0.01f || level>bounds.Center.Z+bounds.Extent.Z+0.01f) return TRUE;
		if (model->Get_Vertex_Count()<=0 || model->Get_Polygon_Count()<=0) return TRUE;
		const Matrix3D &matrix=object->Get_Transform();
		WaterHullMesh mesh;mesh.model=model;
		mesh.crossesMean=level>=bounds.Center.Z-bounds.Extent.Z-0.01f && level<=bounds.Center.Z+bounds.Extent.Z+0.01f;
		mesh.identity=model;mesh.vertexArray=model->Get_Vertex_Array();mesh.triangleArray=model->Get_Polygon_Array();
		mesh.vertexCount=model->Get_Vertex_Count();mesh.triangleCount=model->Get_Polygon_Count();
		AABoxClass localBounds;model->Get_Bounding_Box(&localBounds);
		const float localCenter[3]={localBounds.Center.X,localBounds.Center.Y,localBounds.Center.Z};
		const float localExtent[3]={localBounds.Extent.X,localBounds.Extent.Y,localBounds.Extent.Z};
		mesh.supportRadius=WaterHullPoseCache::SupportRadius(localCenter,localExtent);
		if(!std::isfinite(mesh.supportRadius)) return FALSE;
		for (unsigned column=0;column<4;++column) {
			const float x=matrix[0][column]-(column==3?origin.X:0.0f);
			const float y=matrix[1][column]-(column==3?origin.Y:0.0f);
			mesh.transform[0][column]=x*axisX+y*axisY;
			mesh.transform[1][column]=-x*axisY+y*axisX;
			mesh.transform[2][column]=matrix[2][column]-(column==3?level:0.0f);
			for (unsigned row=0;row<3;++row) {
				if (!std::isfinite(mesh.transform[row][column]) || std::abs(mesh.transform[row][column])>1.0e6f) return FALSE;
			}
		}
		if(!WaterHullPoseCache::Valid(mesh)) return FALSE;
		meshes.push_back(mesh);
		return TRUE;
	}
	if (object->Class_ID()==RenderObjClass::CLASSID_HLOD) {
		HLodClass *hlod=static_cast<HLodClass*>(object);
		// Retain the most detailed authored contour as visual LODs change. It is independent
		// of deck/superstructure bounds and is never used for collision or pathfinding.
		const int lod=hlod->Get_Lod_Count()-1;
		if (lod>=0) for (int i=0;i<hlod->Get_Lod_Model_Count(lod);++i)
			if (!CollectWaterHullMeshes(hlod->Peek_Lod_Model(lod,i),origin,axisX,axisY,level,crestRise,meshes,visited,depth+1)) return FALSE;
		for (int i=0;i<hlod->Get_Additional_Model_Count();++i)
			if (!CollectWaterHullMeshes(hlod->Peek_Additional_Model(i),origin,axisX,axisY,level,crestRise,meshes,visited,depth+1)) return FALSE;
		return TRUE;
	}
	const int children=object->Get_Num_Sub_Objects();
	if (children>(int)WaterHullModel::MaxMeshes) return FALSE;
	for(int i=0;i<children;++i) {
		RenderObjClass *child=object->Get_Sub_Object(i);
		const Bool ok=CollectWaterHullMeshes(child,origin,axisX,axisY,level,crestRise,meshes,visited,depth+1);
		REF_PTR_RELEASE(child);
		if (!ok) return FALSE;
	}
	return TRUE;
}
static Real WaterTerrainDepth(WorldHeightMap *map,Real x,Real y,Real level)
{
	if(!map || map->getXExtent()<2 || map->getYExtent()<2) return 0.0f;
	const Real mx=clamp(x/MAP_XY_FACTOR+map->getBorderSize(),0.0f,(Real)map->getXExtent()-1.001f);
	const Real my=clamp(y/MAP_XY_FACTOR+map->getBorderSize(),0.0f,(Real)map->getYExtent()-1.001f);
	const Int ix=(Int)mx,iy=(Int)my;const Real fx=mx-ix,fy=my-iy;
	const Real a=map->getHeight(ix,iy)*(1-fx)+map->getHeight(ix+1,iy)*fx;
	const Real b=map->getHeight(ix,iy+1)*(1-fx)+map->getHeight(ix+1,iy+1)*fx;
	return level-(a*(1-fy)+b*fy)*MAP_HEIGHT_SCALE;
}
static WaterHullModel::Point3 WaterHullPoint(const WaterHullMesh &mesh,const Vector3 &p)
{
	WaterHullModel::Point3 out;
	out.x=mesh.transform[0][0]*p.X+mesh.transform[0][1]*p.Y+mesh.transform[0][2]*p.Z+mesh.transform[0][3];
	out.y=mesh.transform[1][0]*p.X+mesh.transform[1][1]*p.Y+mesh.transform[1][2]*p.Z+mesh.transform[1][3];
	out.z=mesh.transform[2][0]*p.X+mesh.transform[2][1]*p.Y+mesh.transform[2][2]*p.Z+mesh.transform[2][3];
	return out;
}
}

void WaterRenderObjClass::clearModernWaterHulls(Bool admissions)
{
	if (admissions)
		m_modernBowAdmissions.Clear();
	for (ModernHullCache &cache : m_modernHullCache) {
		for (MeshModelClass *mesh : cache.slicedModels)
			REF_PTR_RELEASE(mesh);
		for (MeshModelClass *mesh : cache.visualModels)
			REF_PTR_RELEASE(mesh);
		REF_PTR_RELEASE(cache.model);
	}
	m_modernHullCache.clear();
	m_modernHulls.clear();
	m_modernContactHulls.clear();
	m_modernVisualHulls.clear();
	m_modernHullObjectIDs.clear();
	m_modernHullSerial = 0;
}

Bool WaterRenderObjClass::modernHasHull(UnsignedInt objectID) const
{
	return std::find(m_modernHullObjectIDs.begin(), m_modernHullObjectIDs.end(), objectID)
		!= m_modernHullObjectIDs.end();
}

Bool WaterRenderObjClass::hasModernVesselFoam(UnsignedInt objectID) const
{
#if ZH_MODERN_WATER
	return m_useModernWater && !m_renderingMirror && m_trapezoidWaterPixelShader
		&& m_modernFoamDrawFrame == WW3D::Get_Frame_Count()
		&& std::find(m_modernFoamDrawObjectIDs.begin(), m_modernFoamDrawObjectIDs.end(), objectID)
			!= m_modernFoamDrawObjectIDs.end();
#else
	return FALSE;
#endif
}

#if ZH_MODERN_WATER
void WaterRenderObjClass::takeWaterPolygonsForDraw(void)
{
	LOGIC_WINDOW_READ( WINDOW_TERRAIN );
	m_waterPolygonsForDraw.clear();
	for (PolygonTrigger *trigger=PolygonTrigger::getFirstPolygonTrigger();trigger;trigger=trigger->getNext()) {
		if (!trigger->isWaterArea() || trigger->getNumPoints()<1) continue;
		WaterPolygonForDraw polygon;
		polygon.center.z=0.0f;
		polygon.river=trigger->isRiver();polygon.riverStart=trigger->getRiverStart();
		polygon.owner=reinterpret_cast<uintptr_t>(trigger);
		trigger->getCenterXY(&polygon.center);polygon.radius=trigger->getRadius();
		polygon.points.reserve(trigger->getNumPoints());
		for (Int i=0;i<trigger->getNumPoints();++i) polygon.points.push_back(*trigger->getPoint(i));
		m_waterPolygonsForDraw.push_back(std::move(polygon));
	}
}

void WaterRenderObjClass::takeMotionReadsForRender(void)
{
	m_modernMotionFrame=TheGameLogic ? TheGameLogic->getFrame() : 0;
	m_modernMotionFrozen=(TheGameLogic && TheGameLogic->isGamePaused()) || (TheScriptEngine &&
		(TheScriptEngine->isTimeFrozenScript() || TheScriptEngine->isTimeFrozenDebug()));
}

void WaterRenderObjClass::takeReadsForRender(void)
{
	// Logic owns the water tables and object values; model transforms stay on the render side
	// so the hull slice follows the same interpolated pose as the displayed vessel.
	synchronizeWaterStyle();
	takeWaterPolygonsForDraw();
	takeMotionReadsForRender();
	m_modernObjectsForDraw.clear();m_modernObstacles.clear();m_modernObstacleObjectIDs.clear();
	m_modernReadsReady=m_useModernWater && TheGameLogic && TheTerrainLogic &&
		TheTerrainRenderObject && TheTerrainRenderObject->getMap();
#if defined(__linux__)
	// Windows' wave tracks still update in their own flush, as they always have.
	if (m_waterTrackSystem) m_waterTrackSystem->takeReadsForRender();
#endif
	if (!m_modernReadsReady) return;

	for (const Object *object=TheGameLogic->getFirstObject();object;object=object->getNextObject()) {
		if (object->isDestroyed()) continue;
		const Coord3D &position=*object->getPosition();
		if (object->isKindOf(KINDOF_STRUCTURE) || object->isKindOf(KINDOF_BRIDGE) || object->isKindOf(KINDOF_IMMOBILE)) {
			const GeometryInfo &geometry=object->getGeometryInfo();
			ModernWaterObstacle obstacle;
			obstacle.position.Set(position.x,position.y,position.z);
			obstacle.radius=geometry.getBoundingCircleRadius();
			obstacle.height=geometry.getMaxHeightAbovePosition();
			obstacle.box=geometry.getGeomType()==GEOMETRY_BOX;
			obstacle.major=geometry.getMajorRadius();obstacle.minor=geometry.getMinorRadius();
			obstacle.cosine=cosf(object->getOrientation());obstacle.sine=sinf(object->getOrientation());
			if (obstacle.radius>1.0f) {
				m_modernObstacles.push_back(obstacle);
				m_modernObstacleObjectIDs.push_back((UnsignedInt)object->getID());
			}
		}
		Drawable *drawable=object->getDrawable();
		if (!drawable || drawable->isDrawableEffectivelyHidden() || drawable->getFullyObscuredByShroud()) continue;
		Real level=0.0f;
		if (!TheTerrainLogic->isUnderwater(position.x,position.y,&level)) continue;
		ModernWaterObject read={drawable,Vector3(position.x,position.y,position.z),level,{},FALSE};
		read.vessel=!object->isKindOf(KINDOF_AIRCRAFT) && (object->isKindOf(KINDOF_AIRCRAFT_CARRIER) ||
			object->isKindOf(KINDOF_BOAT) || object->isKindOf(KINDOF_VEHICLE));
		const PhysicsBehavior *physics=read.vessel?object->getPhysics():NULL;
		if (physics) {
			const Coord3D &velocity=*physics->getVelocity();
			read.initialVelocity={velocity.x*LOGICFRAMES_PER_SECOND,velocity.y*LOGICFRAMES_PER_SECOND};
		}
		m_modernObjectsForDraw.emplace((UnsignedInt)object->getID(),read);
	}
}
#endif

Bool WaterRenderObjClass::isModernWaterMirrorForOwner(UnsignedInt objectID) const
{
#if ZH_MODERN_WATER
	if(!m_useModernWater || !m_renderingMirror || m_waterType!=WATER_TYPE_0_TRANSLUCENT || !m_trapezoidWaterPixelShader ||
		!m_pReflectionTexture || !m_modernReadsReady) return FALSE;
	const auto found=m_modernObjectsForDraw.find(objectID);
	if(found==m_modernObjectsForDraw.end()) return FALSE;
	Drawable *drawable=found->second.drawable;
	const Real level=found->second.level;
	if(drawable->isDrawableEffectivelyHidden() || drawable->getFullyObscuredByShroud() ||
		!WaterFoamParticlePolicy::SameMirrorSurface(true,true,level,m_reflectionLevel)) return FALSE;
	for(DrawModule **modules=drawable->getDrawModules();modules && *modules;++modules) {
		const ObjectDrawInterface *draw=(*modules)->getObjectDrawInterface();
		if(!draw) continue;
		RenderObjClass *model=((W3DModelDraw*)draw)->getRenderObject();
		if(!model || model->Is_Hidden() || model->Is_Animation_Hidden()) continue;
		const AABoxClass &bounds=model->Get_Bounding_Box();
		if(level>=bounds.Center.Z-bounds.Extent.Z-0.1f && level<=bounds.Center.Z+bounds.Extent.Z+0.1f) return TRUE;
	}
#endif
	return FALSE;
}

Real WaterRenderObjClass::modernHullDamping(Real x,Real y,Real level) const
{
	Real damping=1.0f;
	for (size_t i=0;i<m_modernHulls.size();++i) damping=min(damping,WaterHullModel::Damping(m_modernHulls[i],x,y,level));
	return damping;
}

void WaterRenderObjClass::updateModernWaterHulls(void)
{
#if ZH_MODERN_WATER
	m_modernHulls.clear();
	m_modernContactHulls.clear();
	m_modernVisualHulls.clear();
	m_modernHullObjectIDs.clear();
	if (!m_modernReadsReady || !TheTerrainRenderObject || !TheTerrainRenderObject->getMap()) {
		clearModernWaterHulls();clearModernWaterFoam();return;
	}
	++m_modernHullSerial;
	const UnsignedInt motionFrame=m_modernMotionFrame;
	const Bool motionFrozen=m_modernMotionFrozen;
	struct Candidate { UnsignedInt objectID; const ModernWaterObject *object; RenderObjClass *model; Real level,score,samplePhase,crestRise; Bool influenceVisible; };
	m_modernFoamHistory.Advance(m_modernWaveTime);
	std::vector<Candidate> candidates;
	for (const auto &entry:m_modernObjectsForDraw) {
		const ModernWaterObject &object=entry.second;
		if (!object.vessel) continue;
		Drawable *drawable=object.drawable;
		if (drawable->isDrawableEffectivelyHidden() || drawable->getFullyObscuredByShroud()) continue;
		const Vector3 &position=object.position;
		const Real level=object.level;
		Real localOffshore=0.0f;
		for (size_t i=0;i<m_modernOpticalTextures.size();++i)
			if (m_modernOpticalTextures[i].level==level && !m_modernOpticalTextures[i].ocean.empty())
				localOffshore=max(localOffshore,WaterSurfaceModel::SampleOffshore(m_modernOpticalTextures[i].ocean.data(),m_modernHeightWidth,m_modernHeightHeight,
					position.X/MAP_XY_FACTOR+m_modernHeightBorder,position.Y/MAP_XY_FACTOR+m_modernHeightBorder,MAP_XY_FACTOR).height);
		if (localOffshore<=0.001f) continue;
		const Real terrainDepth=WaterTerrainDepth(TheTerrainRenderObject->getMap(),position.X,position.Y,level);
		const Real crestRise=WaterHullContact::ReachableCrest(terrainDepth,WaterSurfaceModel::TerrainDamping(terrainDepth,localOffshore));
		for (DrawModule **modules=drawable->getDrawModules();modules && *modules;++modules) {
			const ObjectDrawInterface *draw=(*modules)->getObjectDrawInterface();
			if (!draw) continue;
			// All W3D object draw interfaces are W3DModelDraw implementations (also used by
			// projected shadows and ghost snapshots); use the displayed, interpolated transform.
			W3DModelDraw *modelDraw=(W3DModelDraw*)draw;
			RenderObjClass *model=modelDraw->getRenderObject();
			// Scene visibility culls the hull itself, but an offscreen vessel can still disturb
			// visible water. Keep explicit visibility/shroud exclusions, then cull its influence.
			if (!model || model->Is_Hidden() || model->Is_Animation_Hidden()) continue;
			const AABoxClass &bounds=model->Get_Bounding_Box();
			if (level<bounds.Center.Z-bounds.Extent.Z-0.01f || level>bounds.Center.Z+bounds.Extent.Z+0.01f) continue;
			AABoxClass influence=bounds;
			influence.Extent.X+=122.0f;influence.Extent.Y+=122.0f;
			influence.Extent.Z+=WaterSurfaceModel::MaxDisplacement;
			const Bool influenceVisible=!m_modernRenderCamera || !m_modernRenderCamera->Cull_Box(influence);
			const Real dx=bounds.Center.X-m_modernCamera.X,dy=bounds.Center.Y-m_modernCamera.Y;
			const Real radius=sqrtf(bounds.Extent.X*bounds.Extent.X+bounds.Extent.Y*bounds.Extent.Y);
			const Real score=max(0.0f,sqrtf(dx*dx+dy*dy)-radius);
			Candidate candidate={entry.first,&object,model,level,score,modelDraw->clientOnly_getRenderMotionSamplePhase(),crestRise,influenceVisible};
			size_t insert=0;
			while(insert<candidates.size() && (candidates[insert].score<score ||
				(candidates[insert].score==score && candidates[insert].objectID<entry.first))) ++insert;
			if (insert<WaterFoamHistory::MaxEmitters) {
				candidates.insert(candidates.begin()+insert,candidate);
				if(candidates.size()>WaterFoamHistory::MaxEmitters) candidates.pop_back();
			}
			break;
		}
	}
	for (const Candidate &candidate:candidates) {
		const Matrix3D &matrix=candidate.model->Get_Transform();
		const Vector3 origin=matrix.Get_Translation();
		float axisX=matrix[0][0],axisY=matrix[1][0],axisLength=sqrtf(axisX*axisX+axisY*axisY);
		if (!(axisLength>0.0001f)) continue;
		axisX/=axisLength;axisY/=axisLength;
		std::vector<WaterHullMesh> meshes;unsigned visited=0;
		if (!CollectWaterHullMeshes(candidate.model,origin,axisX,axisY,candidate.level,candidate.crestRise,meshes,visited)) continue;
		size_t slot=0;
		for (;slot<m_modernHullCache.size();++slot)
			if(m_modernHullCache[slot].model==candidate.model && m_modernHullCache[slot].objectID==(UnsignedInt)candidate.objectID) break;
		if (slot==m_modernHullCache.size()) {
			if (slot==WaterHullModel::MaxCacheEntries) {
				slot=0;
				for(size_t i=1;i<m_modernHullCache.size();++i) if(m_modernHullCache[i].lastUse<m_modernHullCache[slot].lastUse) slot=i;
				for(MeshModelClass *mesh:m_modernHullCache[slot].slicedModels) REF_PTR_RELEASE(mesh);
				for(MeshModelClass *mesh:m_modernHullCache[slot].visualModels) REF_PTR_RELEASE(mesh);
				REF_PTR_RELEASE(m_modernHullCache[slot].model);
				m_modernHullCache[slot]=ModernHullCache();
			} else m_modernHullCache.push_back(ModernHullCache());
			m_modernHullCache[slot].model=candidate.model;candidate.model->Add_Ref();
			m_modernHullCache[slot].objectID=(UnsignedInt)candidate.objectID;
		}
		ModernHullCache &cache=m_modernHullCache[slot];cache.lastUse=m_modernHullSerial;
		if(!cache.poses.Reusable(meshes,candidate.crestRise)) {
			cache.polygon=WaterHullModel::Polygon();cache.contact=WaterHullModel::Polygon();
			std::vector<WaterHullModel::Point> points,contactPoints;unsigned triangles=0,contactTriangles=0;Bool valid=TRUE,contactValid=TRUE;
			for(const WaterHullMesh &mesh:meshes) {
				const unsigned count=mesh.model->Get_Polygon_Count();
				// Contact-only meshes cannot consume the independent mean-waterline budget.
				if(mesh.crossesMean && valid) {
					if(count>WaterHullModel::MaxSliceTriangles-triangles) valid=FALSE;
					else triangles+=count;
				}
				if(contactValid) {
					if(count>WaterHullModel::MaxSliceTriangles-contactTriangles) contactValid=FALSE;
					else contactTriangles+=count;
				}
				if(!valid && !contactValid) break;
				if((!mesh.crossesMean || !valid) && !contactValid) continue;
				const Vector3 *vertices=mesh.model->Get_Vertex_Array();
				const TriIndex *indices=mesh.model->Get_Polygon_Array();
				for(unsigned i=0;i<count;++i) {
					const TriIndex &t=indices[i];
					if(t[0]>=mesh.model->Get_Vertex_Count() || t[1]>=mesh.model->Get_Vertex_Count() || t[2]>=mesh.model->Get_Vertex_Count()) {
						if(mesh.crossesMean) valid=FALSE;
						contactValid=FALSE;break;
					}
					const WaterHullModel::Point3 a=WaterHullPoint(mesh,vertices[t[0]]),b=WaterHullPoint(mesh,vertices[t[1]]),c=WaterHullPoint(mesh,vertices[t[2]]);
					if(mesh.crossesMean && valid && !WaterHullModel::SliceTriangle(a,b,c,points)) valid=FALSE;
					if(contactValid && !WaterHullContact::ProjectTriangle(a,b,c,candidate.crestRise,contactPoints)) contactValid=FALSE;
				}
				if(!valid && !contactValid) break;
			}
			if(valid) {
				cache.polygon=WaterHullModel::BuildPolygon(points);
				if(contactValid) cache.contact=WaterHullModel::BuildPolygon(contactPoints);
			}
			// Keep exactly the poses that produced these contours, including an empty result.
			// Retained children prevent identity reuse when an HLOD replaces its mesh assets.
			for(MeshModelClass *mesh:cache.slicedModels) REF_PTR_RELEASE(mesh);
			cache.slicedModels.clear();
			if(cache.slicedModels.capacity()<meshes.size()) cache.slicedModels.reserve(meshes.size());
			for(const WaterHullMesh &mesh:meshes) { mesh.model->Add_Ref();cache.slicedModels.push_back(mesh.model); }
			cache.poses.Store(meshes,candidate.crestRise);

			cache.forwardSign=1.0f;
			const int bow=candidate.model->Get_Bone_Index("BOWWAKE"),stern=candidate.model->Get_Bone_Index("PROPELLERWAKE");
			if(bow>0 && stern>0) {
				const Vector3 direction=candidate.model->Get_Bone_Transform(bow).Get_Translation()-candidate.model->Get_Bone_Transform(stern).Get_Translation();
				cache.forwardSign=direction.X*axisX+direction.Y*axisY<0.0f?-1.0f:1.0f;
			} else if(cache.polygon.count>=3) {
				// Assets without wake bones use the narrower end of their actual waterline.
				float minX=1.0e30f,maxX=-1.0e30f;
				for(unsigned i=0;i<cache.polygon.count;++i) {minX=min(minX,cache.polygon.points[i].x);maxX=max(maxX,cache.polygon.points[i].x);}
				float leftMin=1.0e30f,leftMax=-1.0e30f,rightMin=1.0e30f,rightMax=-1.0e30f;
				const float band=(maxX-minX)*0.2f;
				for(unsigned i=0;i<cache.polygon.count;++i) {
					const WaterHullModel::Point p=cache.polygon.points[i];
					if(p.x<minX+band) {leftMin=min(leftMin,p.y);leftMax=max(leftMax,p.y);}
					if(p.x>maxX-band) {rightMin=min(rightMin,p.y);rightMax=max(rightMax,p.y);}
				}
				if(leftMax-leftMin<rightMax-rightMin) cache.forwardSign=-1.0f;
			}
		}

		if(cache.polygon.count<3) continue;
		const WaterVesselMotion::Point initialVelocity=candidate.object->initialVelocity;
		const WaterVesselMotion::State previousMotion=cache.motion;
		const WaterVesselMotion::Point velocity=WaterVesselMotion::Update(cache.motion,{origin.X,origin.Y},
			candidate.level,motionFrame,motionFrozen,initialVelocity,float(LOGICFRAMES_PER_SECOND),candidate.samplePhase);
		auto packHull=[&](const WaterHullModel::Polygon &polygon,WaterHullModel::Hull &hull,float sign)->Bool {
			if(polygon.count<3) return FALSE;
			float minX=1.0e30f,maxX=-1.0e30f,minY=1.0e30f,maxY=-1.0e30f;
			for(unsigned i=0;i<polygon.count;++i) {
				const WaterHullModel::Point p=polygon.points[i];
				minX=min(minX,p.x);maxX=max(maxX,p.x);minY=min(minY,p.y);maxY=max(maxY,p.y);
			}
			if(maxX-minX<2.0f || maxY-minY<1.0f) return FALSE;
			const float centerX=(minX+maxX)*0.5f,centerY=(minY+maxY)*0.5f;
			hull.data[0][0]=origin.X+axisX*centerX-axisY*centerY;hull.data[0][1]=origin.Y+axisY*centerX+axisX*centerY;
			hull.data[0][2]=candidate.level;hull.data[0][3]=float(polygon.count);
			hull.data[1][0]=axisX*sign;hull.data[1][1]=axisY*sign;
			hull.data[1][2]=(maxX-minX)*0.5f;hull.data[1][3]=(maxY-minY)*0.5f;
			hull.data[2][0]=velocity.x;hull.data[2][1]=velocity.y;
			hull.data[2][2]=float((unsigned(candidate.objectID)*2654435761U)&65535U)/65535.0f;
			hull.data[2][3]=sqrtf(hull.data[1][2]*hull.data[1][2]+hull.data[1][3]*hull.data[1][3]);
			hull.data[3][0]=-hull.data[1][2];hull.data[3][2]=hull.data[1][2];
			float sternY=0.0f,bowY=0.0f;unsigned sternCount=0,bowCount=0;
			for(unsigned i=0;i<WaterHullModel::MaxVertices;++i) {
				const WaterHullModel::Point p=polygon.points[min(i,polygon.count-1)];
				hull.data[4+i/2][(i%2)*2]=(p.x-centerX)*sign;
				hull.data[4+i/2][(i%2)*2+1]=(p.y-centerY)*sign;
				if(i<polygon.count) {
					const float localX=(p.x-centerX)*sign,localY=(p.y-centerY)*sign;
					if(localX<-hull.data[1][2]+0.5f) {sternY+=localY;++sternCount;}
					if(localX>hull.data[1][2]-0.5f) {bowY+=localY;++bowCount;}
				}
			}
			hull.data[3][1]=sternCount?sternY/sternCount:0.0f;
			hull.data[3][3]=bowCount?bowY/bowCount:0.0f;
			return TRUE;
		};
		WaterHullModel::Hull hull,contactHull;
		if(!packHull(cache.polygon,hull,cache.forwardSign)) continue;
		// The displayed model axis is stable through pitch/roll. A sliced narrow-end
		// heuristic can reverse on a keel sliver; actual velocity selects contact ends.
		if(!packHull(cache.contact,contactHull,1.0f)) packHull(cache.polygon,contactHull,1.0f);
		// Keep deposition independent of the eight nearest hulls sent to the shader. Visible
		// vessels outside the camera still leave history in world space, subject to a fixed bound.
		const double motionDt=(double(motionFrame)-previousMotion.frame+candidate.samplePhase-previousMotion.phase)/LOGICFRAMES_PER_SECOND;
		const double motionDx=origin.X-previousMotion.position.x,motionDy=origin.Y-previousMotion.position.y;
		const Bool resetMotion=!previousMotion.initialized || (motionFrame<previousMotion.frame || candidate.level!=previousMotion.level ||
			motionDx*motionDx+motionDy*motionDy>pow(max(32.0,250.0*max(0.0,motionDt)),2.0));
		const WaterFoamHistory::Point motionPosition={origin.X,origin.Y};
		m_modernFoamHistory.Observe((UnsignedInt)candidate.objectID,contactHull,m_modernWaveTime,TRUE,resetMotion,&motionPosition);
		if(candidate.influenceVisible && m_modernHulls.size()<WaterHullModel::MaxHulls) {
			WaterHullVisual::Packet visualPacket={};
			{
				// A separate cache keeps the mean-water clearance, pressure and history unchanged.
				// Only the eight admitted shader hulls collect full reachable visual geometry.
				const float bound=WaterFftGeometry::MaxVerticalDisplacement;
				std::vector<WaterHullMesh> visualMeshes;unsigned visualVisited=0;
				const Bool complete=CollectWaterHullMeshes(candidate.model,origin,axisX,axisY,
					candidate.level-bound,2.0f*bound,visualMeshes,visualVisited);
				const Bool reusable=complete && cache.visualPoses.Reusable(visualMeshes,2.0f*bound,0.0);
				if(!complete) {
					cache.visual=WaterHullVisual::Packet();cache.visualPoses.Clear();
					for(MeshModelClass *mesh:cache.visualModels) REF_PTR_RELEASE(mesh);
					cache.visualModels.clear();
				} else if(!reusable) {
					WaterHullVisual::Builder builder(-bound,bound);
					Bool valid=TRUE;unsigned inspected=0;
					for(const WaterHullMesh &mesh:visualMeshes) {
						const unsigned count=mesh.model->Get_Polygon_Count();
						if(count>WaterHullModel::MaxSliceTriangles-inspected) {valid=FALSE;break;}
						inspected+=count;
						const Vector3 *vertices=mesh.model->Get_Vertex_Array();
						const TriIndex *indices=mesh.model->Get_Polygon_Array();
						for(unsigned ti=0;ti<count;++ti) {
							const TriIndex &triangle=indices[ti];
							if(triangle[0]>=mesh.vertexCount || triangle[1]>=mesh.vertexCount || triangle[2]>=mesh.vertexCount) {valid=FALSE;break;}
							WaterHullVisual::Point3 p[3];
							for(unsigned vi=0;vi<3;++vi) {
								const auto q=WaterHullPoint(mesh,vertices[triangle[vi]]);
								p[vi]={q.x,q.y,q.z-bound};
							}
							if(!builder.AddTriangle(p[0],p[1],p[2])) {valid=FALSE;break;}
						}
						if(!valid) break;
					}
					cache.visual=WaterHullVisual::Packet();
					if(valid) builder.Finish(cache.visual);
					// Cache even complete-but-over-budget geometry; never truncate it to fit.
					// Exact poses are required because nearly tangent slices amplify small motion.
					for(MeshModelClass *mesh:cache.visualModels) REF_PTR_RELEASE(mesh);
					cache.visualModels.clear();cache.visualModels.reserve(visualMeshes.size());
					for(const WaterHullMesh &mesh:visualMeshes) {mesh.model->Add_Ref();cache.visualModels.push_back(mesh.model);}
					cache.visualPoses.Store(visualMeshes,2.0f*bound);
				}
				const float deltaX=contactHull.data[0][0]-origin.X,deltaY=contactHull.data[0][1]-origin.Y;
				WaterHullVisual::Reframe(cache.visual,deltaX*axisX+deltaY*axisY,
					-deltaX*axisY+deltaY*axisX,visualPacket);

			}
			m_modernVisualHulls.push_back(visualPacket);

			m_modernHulls.push_back(hull);
			m_modernContactHulls.push_back(contactHull);
			m_modernHullObjectIDs.push_back((UnsignedInt)candidate.objectID);
		}
	}
#endif
}

// Foam is stored in world-space path sections. The sampled window only controls raster cost;
// moving the camera reconstructs the same history instead of moving or forgetting that history.
void WaterRenderObjClass::clearModernWaterFoam(Bool history)
{
	m_modernFoamDrawFrame = ~0u;
	m_modernFoamDrawObjectIDs.clear();
	for (ModernFoamTexture &field : m_modernFoamTextures) REF_PTR_RELEASE(field.texture);
	m_modernFoamTextures.clear();
	if (history) m_modernFoamHistory.Clear();
}

void WaterRenderObjClass::invalidateModernWaterShore(void)
{
	m_modernShoreHistory.Clear();
	m_modernShoreSource.Clear();
	m_modernShoreReady = FALSE;
	if (++m_modernShoreRevision == 0) m_modernShoreRevision = 1;
	m_modernShoreAttempted = 0;
	for (ModernFoamTexture &field : m_modernFoamTextures) field.valid = FALSE;
}

void WaterRenderObjClass::updateModernWaterShore(void)
{
#if ZH_MODERN_WATER
	if (m_modernShoreAttempted != m_modernShoreRevision) {
		m_modernShoreAttempted = m_modernShoreRevision;
		m_modernShoreReady = FALSE;
		std::vector<double> levels;
		for (const auto &polygon : m_modernOpticalPolygons) {
			if (polygon.points.size() < 3) continue;
			const double level = polygon.points[0].z;
			Bool flat = TRUE;
			for (const auto &p : polygon.points) flat = flat && fabs(double(p.z) - level) < .01;
			if (flat && std::find(levels.begin(), levels.end(), level) == levels.end()) levels.push_back(level);
		}
		std::sort(levels.begin(), levels.end());
		WaterShoreHistory::BuildResult result = WaterShoreHistory::BuildResult::Invalid;
		if (m_modernOpticalPolygonsValid && !m_modernHeights.empty() && levels.size() <= WaterShoreHistory::MaxLevels) {
			// Complete every level before collecting pointers: texture-vector reallocation cannot
			// invalidate a source field, and camera order cannot choose the retained history levels.
			for (double level : levels) modernWaterOpticalTexture(Real(level));
			std::vector<WaterShoreSource::Level> fields;
			for (double level : levels) for (const auto &field : m_modernOpticalTextures)
				if (field.level == level) {
					fields.push_back({level, &field.shoreContext});
					break;
				}
			WaterShoreSource::Batch candidate;
			if (fields.size() == levels.size()) result = candidate.Build(m_modernHeights.data(), m_modernHeightWidth,
				m_modernHeightHeight, m_modernHeightBorder, MAP_XY_FACTOR, MAP_HEIGHT_SCALE, m_modernOpticalPolygons, fields);
			if (result == WaterShoreHistory::BuildResult::Ready) {
				WaterShoreHistory::Parameters settings;
				settings.maximumRate = WaterShoreFoam::MaximumRate;
				result = m_modernShoreHistory.Install(candidate.Grid(), candidate.Cells(), m_modernShoreRevision, m_modernWaveTime, settings);
				if (result == WaterShoreHistory::BuildResult::Ready) {
					m_modernShoreSource = std::move(candidate);
					m_modernShoreReady = TRUE;
				}
			}
		}

	}
	if (m_modernShoreReady && m_modernShoreHistory.ValidFor(m_modernShoreRevision)) {
		const auto result = m_modernShoreHistory.Advance(m_modernWaveTime, m_modernShoreSource);
		if (result == WaterShoreHistory::AdvanceResult::Invalid) m_modernShoreReady = FALSE;
		if (result == WaterShoreHistory::AdvanceResult::Rewound)
			for (ModernFoamTexture &field : m_modernFoamTextures) field.valid = FALSE;
	}
#endif
}

namespace ModernWater {

bool Supported()
{
#if defined(_WIN32)
	return Direct3D11_Is_Active() && Direct3D11_Present_Is_Enabled();
#else
	return ZH_MODERN_WATER;
#endif
}

} // namespace ModernWater

#if ZH_MODERN_WATER
/** The Modern style is asked for and a device draws it.  Classic graphics is the 2003 picture, so it
		keeps EA's water whatever WaterStyle says, as it keeps the old clouds and drops the foam; the
		quality slider and its presets put Modern into WaterStyle from Medium up. */
static Bool wantsModernWater(void)
{
	return TheGlobalData && TheGlobalData->m_waterStyle != 0 && !TheGlobalData->m_classicGraphics
		&& ModernWater::Supported();
}

/** The Modern water's registers run to c895, past any Direct3D 9 device; on Windows they go to the
		Direct3D 11 backend's own bank, which is the only device that draws with them. */
static void setModernWaterConstants(UnsignedInt first, const float *values, size_t count)
{
#if defined(_WIN32)
	Direct3D11_Set_Water_Constants(first, values, (UnsignedInt)count);
#else
	DX8Wrapper::_Get_D3D_Device()->SetPixelShaderConstantF(first, values, (UnsignedInt)count);
#endif
}
#endif

void WaterRenderObjClass::bindModernWaterFoam(Real level, Bool river)
{
#if ZH_MODERN_WATER
	D3DXVECTOR4 area(0, 0, 0, 0), parameters(0, level, 0, m_modernWaveTime);
	TextureClass *texture = NULL;
	const Bool vessel = m_modernFoamHistory.HasFoam();
	const Bool coast = m_modernShoreReady && m_modernShoreHistory.ValidFor(m_modernShoreRevision)
		&& !m_modernShoreHistory.Domain().Cells().empty();
	const unsigned channels = ((vessel ? 1 : 0)|(coast ? 2 : 0))|((vessel || coast) ? 4 : 0);
	if (!river && channels && m_modernRenderCamera) {
		size_t index = 0;
		for (; index < m_modernFoamTextures.size(); ++index) if (m_modernFoamTextures[index].level == level) break;
		if (index == m_modernFoamTextures.size() && index < WaterOpticalContext::MaxLevels) {
			m_modernFoamTextures.push_back(ModernFoamTexture());
			m_modernFoamTextures.back().level = level;
		}
		if (index < m_modernFoamTextures.size()) {
			ModernFoamTexture &field = m_modernFoamTextures[index];
			const Vector2 ndc[5] = {Vector2(0, 0), Vector2(-1, -1), Vector2(1, -1), Vector2(1, 1), Vector2(-1, 1)};
			WaterFoamWindow::Point rays[5];
			for (unsigned i = 0; i < 5; ++i) {
				Vector3 target;
				m_modernRenderCamera->Un_Project(target, ndc[i]);
				const Vector3 d = target - m_modernCamera;
				rays[i] = {d.X, d.Y, d.Z};
			}
			const WaterFoamHistory::Mapping map = WaterFoamWindow::Choose({m_modernCamera.X, m_modernCamera.Y, m_modernCamera.Z}, rays, level);
			const Bool remap = map.originX != field.mapping.originX || map.originY != field.mapping.originY ||
				map.widthWorld != field.mapping.widthWorld || map.heightWorld != field.mapping.heightWorld
				|| map.widthPixels != field.mapping.widthPixels || map.heightPixels != field.mapping.heightPixels;
			// At most ten uploads per elapsed water second for a stable camera. The shader ages
			// coverage between uploads, and pause freezes both fields. Camera changes may reproject.
			if (!field.valid || field.channels != channels || remap || m_modernWaveTime - field.timestamp >= 0.1f
				|| m_modernWaveTime < field.timestamp) {
				std::vector<std::uint32_t> pixels, direction;
				Bool atlas = TRUE;
				if (vessel) atlas = m_modernFoamHistory.BuildAtlas(map, level, pixels, &direction);
				else {
					pixels.assign(size_t(map.widthPixels) * map.heightPixels, 0);
					direction.assign(pixels.size(), 0);
				}
				if (atlas && coast) {
					const auto &states = m_modernShoreHistory.Preview(m_modernShoreSource);
					const WaterShoreHistory::Mapping shoreMap = {map.originX, map.originY, map.widthWorld, map.heightWorld,
						map.widthPixels, map.heightPixels};
					auto coverage = WaterShoreSource::Coverage;
					atlas = WaterShoreHistory::RasterizeAlpha(m_modernShoreHistory.Domain(), states, shoreMap, level, m_modernWaveTime,
						coverage, pixels, m_modernShoreSource.Support());
				}
				if (atlas) {
					// Merge coastal alpha before appending the direction tile: its rasterizer
					// owns exactly the original logical window. Both tiles share one upload/lifetime.
					pixels.insert(pixels.end(), direction.begin(), direction.end());
					const unsigned physicalHeight = map.heightPixels * 2;
					if (field.texture && (field.textureWidth != map.widthPixels || field.textureHeight != physicalHeight)) {
						REF_PTR_RELEASE(field.texture);
						field.valid = FALSE;
					}
					if (!field.texture) {
						field.texture = NEW_REF(TextureClass, (map.widthPixels, physicalHeight, WW3D_FORMAT_A8R8G8B8, MIP_LEVELS_1));
						field.textureWidth = map.widthPixels;
						field.textureHeight = physicalHeight;
					}
					SurfaceClass *surface = field.texture ? field.texture->Get_Surface_Level() : NULL;
					Int pitch = 0;
					void * bits = surface ? surface->Lock(&pitch) : NULL;
					const Bool uploaded = bits && pitch >= Int(map.widthPixels * 4);
					if (uploaded)
						for (unsigned y = 0; y < physicalHeight; ++y) memcpy((char*) bits + y * pitch,
							pixels.data() + size_t(y) * map.widthPixels, map.widthPixels * 4);
					if (bits) surface->Unlock();
					if (uploaded) {
						field.mapping = map;
						field.timestamp = m_modernWaveTime;
						field.channels = channels;
						field.valid = TRUE;
					}
					else field.valid = FALSE; // missing surface/lock must not expose a stale or partial pair
					REF_PTR_RELEASE(surface);
				} else field.valid = FALSE;
			}
			if (field.valid) {
				texture = field.texture;
				area = D3DXVECTOR4(float(field.mapping.originX), float(field.mapping.originY), float(1.0 / field.mapping.widthWorld),
					float(1.0 / field.mapping.heightWorld));
				parameters = D3DXVECTOR4(float(field.channels), level, float(max(field.mapping.widthWorld / field.mapping.widthPixels,
					field.mapping.heightWorld / field.mapping.heightPixels)), field.timestamp);
			}
		}
	}
	setModernWaterConstants(88, area, 1);
	setModernWaterConstants(89, parameters, 1);
	DX8Wrapper::Set_DX8_Texture(14, texture ? texture->Peek_D3D_Texture() : NULL);
#endif
}


void WaterRenderObjClass::registerWaterStyleShaders(void)
{
	// The Classic names draw the transcription of EA's shaders; the plain names draw the Modern text.
#if ZH_MODERN_WATER
	const Bool modern = m_useModernWater;
#else
	const Bool modern = FALSE;
#endif
	if (m_riverWaterPixelShader)
	Direct3D11_Register_Engine_Shader(m_riverWaterPixelShader, modern
	? "river water ps.1.1" : "classic river water ps.1.1");
	if (m_trapezoidWaterPixelShader)
	Direct3D11_Register_Engine_Shader(m_trapezoidWaterPixelShader, modern
	? "trapezoid water ps.1.1" : "classic trapezoid water ps.1.1");
}

void WaterRenderObjClass::synchronizeWaterStyle(void)
{
#if ZH_MODERN_WATER
	const Bool modern = wantsModernWater();
	if (modern == m_useModernWater) return;

	// Apply before any reflected or main-scene water draw. Existing recorded draws keep
	// their immutable GPU programs; both styles have separate program/pipeline cache keys.
	m_useModernWater = modern;
	registerWaterStyleShaders();
	clearModernWaterFoam();
	clearModernWaterHulls();
	invalidateModernWaterShore();
	m_modernProjectedSamples.clear();
	m_modernObstacles.clear();
	m_modernPatchObstacles.clear();
	m_modernObstacleIndex.Clear();

	// Classic clears the mirror to white; Modern clears it transparent. Bypass the
	// stationary-camera reuse once, so neither style samples the other's background.
	m_reflectionReused = TRUE;
	// Keep wave time, capture/pause clock, gameplay levels and water-grid motion intact.
#endif
}

void WaterRenderObjClass::setupModernWaterShader(Bool river, Bool heightCorrection, Bool waveCage)
{
#if ZH_MODERN_WATER
	if (!m_useModernWater) return;
	updateModernWaterTextures();
	const Bool reflection = m_pReflectionTexture && !m_renderingMirror &&
		m_reflectionAreaMax.X > m_reflectionAreaMin.X && m_reflectionAreaMax.Y > m_reflectionAreaMin.Y;
	TextureClass *mirror = reflection ? m_pReflectionTexture : m_whiteTexture;
	if (!mirror->Is_Initialized()) mirror->Init();
	DX8Wrapper::Set_DX8_Texture(1, mirror->Peek_D3D_Texture());
	DX8Wrapper::Set_DX8_Texture_Stage_State(1, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
	DX8Wrapper::Set_DX8_Texture_Stage_State(1, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
	DX8Wrapper::Set_DX8_Texture_Stage_State(1, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
	const D3DXVECTOR4 parameters(m_modernWaveTime,
		river ? 1.0f : 0.0f, reflection ? 1.0f : 0.0f, m_reflectionLevel);
	const D3DXVECTOR4 area(m_reflectionAreaMin.X, m_reflectionAreaMin.Y,
		m_reflectionAreaMax.X, m_reflectionAreaMax.Y);
	const Coord3D &sun = TheGlobalData->m_terrainLightPos[0];
	const RGBColor &colour = TheGlobalData->m_terrainDiffuse[0];
	const D3DXVECTOR4 direction(-sun.x, -sun.y, -sun.z, 0.0f);
	const D3DXVECTOR4 light(colour.red, colour.green, colour.blue, 0.0f);
	setModernWaterConstants(1, parameters, 1);
	setModernWaterConstants(2, area, 1);
	setModernWaterConstants(3, direction, 1);
	setModernWaterConstants(4, light, 1);
	WorldHeightMap *map = TheTerrainRenderObject ? TheTerrainRenderObject->getMap() : NULL;
	const D3DXVECTOR4 terrain(map ? map->getBorderSize() * MAP_XY_FACTOR : 0.0f,
		m_modernHeightSide * MAP_XY_FACTOR, 255.0f * MAP_HEIGHT_SCALE, m_modernHeightTexture ? 1.0f : 0.0f);
	setModernWaterConstants(5, terrain, 1);
	TextureClass *optical = river ? NULL : modernWaterOpticalTexture(m_modernOpticalLevel);
	const D3DXVECTOR4 optics(TheWaterTransparency->m_transparentWaterDepth,
		TheWaterTransparency->m_minWaterOpacity, optical ? 1.0f : 0.0f, heightCorrection && !river ? 1.0f : 0.0f);
	setModernWaterConstants(6, optics, 1);
	D3DXVECTOR4 materialColor, materialParameters;
	getModernWaterMaterial(&materialColor.x, &materialParameters.x);
	setModernWaterConstants(ModernWaterMaterial::RegisterColor, materialColor, 1);
	setModernWaterConstants(ModernWaterMaterial::RegisterParameters, materialParameters, 1);
	const RGBColor &ambient = TheGlobalData->m_terrainAmbient[0];
	const D3DXVECTOR4 hullParameters(river ? 0.0f : float(m_modernHulls.size()), ambient.red, ambient.green, ambient.blue);
	setModernWaterConstants(7, hullParameters, 1);
	// Match the terrain's authored ambient + up to three directional lights. Reset all
	// fill slots on every draw so a later one-light map cannot inherit old illumination.
	D3DXVECTOR4 foamFillLights[4] = {D3DXVECTOR4(0, 0, 0, 0), D3DXVECTOR4(0, 0, 0, 0),
		D3DXVECTOR4(0, 0, 0, 0), D3DXVECTOR4(0, 0, 0, 0)};
	foamFillLights[0].w = TheGlobalData->m_numGlobalLights > 0 ? 1.0f : 0.0f;
	for (Int lightIndex = 1; lightIndex < min(3, TheGlobalData->m_numGlobalLights); ++lightIndex) {
		const Coord3D &position = TheGlobalData->m_terrainLightPos[lightIndex];
		const RGBColor &diffuse = TheGlobalData->m_terrainDiffuse[lightIndex];
		Vector3 fillDirection(-position.x, -position.y, -position.z);
		const float magnitude = fillDirection.Length();
		if (magnitude > 0.000001f) fillDirection /= magnitude;
		else fillDirection = Vector3(0, 0, 0);
		const Int slot = (lightIndex - 1) * 2;
		foamFillLights[slot].x = fillDirection.X;
		foamFillLights[slot].y = fillDirection.Y;
		foamFillLights[slot].z = fillDirection.Z;
		foamFillLights[slot + 1] = D3DXVECTOR4(diffuse.red, diffuse.green, diffuse.blue, 0);
	}
	setModernWaterConstants(90, &foamFillLights[0].x, 4);
	if (!river && !m_modernHulls.empty()) {
		setModernWaterConstants(8, &m_modernHulls[0].data[0][0],
			m_modernHulls.size() * WaterHullModel::RegistersPerHull);
		setModernWaterConstants(96, &m_modernContactHulls[0].data[0][0],
			m_modernContactHulls.size() * WaterHullModel::RegistersPerHull);
	}
	{
		// Every slot is initialized, including absent hulls and river draws. The backend takes
		// immutable draw snapshots before a different surface can replace these registers.
		float metadata[WaterHullVisualLayout::MaxHulls][4] = {};
		float packets[WaterHullVisualLayout::PacketRegisters][4] = {};
		if (!river) for (unsigned h = 0; h < m_modernVisualHulls.size() && h < WaterHullVisualLayout::MaxHulls; ++h) {
			memcpy(metadata[h], m_modernVisualHulls[h].metadata, sizeof(metadata[h]));
			memcpy(packets[h * WaterHullVisualLayout::RegistersPerHull], m_modernVisualHulls[h].data, sizeof(m_modernVisualHulls[h].data));
		}
		setModernWaterConstants(WaterHullVisualLayout::MetadataRegister,
			&metadata[0][0], WaterHullVisualLayout::MaxHulls);
		setModernWaterConstants(WaterHullVisualLayout::PacketRegister,
			&packets[0][0], WaterHullVisualLayout::PacketRegisters);
	}
	DX8Wrapper::Set_DX8_Texture(4, optical ? optical->Peek_D3D_Texture() : NULL);
	bindModernWaterFoam(m_modernOpticalLevel, river);
	TextureClass *heightTexture = m_modernHeightTexture;
	if (waveCage && !river && heightCorrection)
		for (const ModernOpticalTexture &cached : m_modernOpticalTextures)
			if (cached.level == m_modernOpticalLevel && cached.waveCage) {
				heightTexture = cached.waveCage;
				break;
			}
	DX8Wrapper::Set_DX8_Texture(2, heightTexture ? heightTexture->Peek_D3D_Texture() : NULL);
	if (m_modernBedTexture) {
		DX8Wrapper::Set_DX8_Texture(7, m_modernBedTexture->Peek_D3D_Texture());
		// Sampling policy is local to the POSIX water draw, leaving the device's stage-7 state intact.
	}
	// The modern surface carries displacement damping and its original elevation in UV1.
	// Only the new flat-patch mesh uses UV0 for a geometric height correction. Legacy grids
	// and rivers retain their authored base UVs; stale texture matrices must not transform metadata.
	if (heightCorrection && !river) {
		DX8Wrapper::Set_DX8_Texture_Stage_State(0, D3DTSS_TEXCOORDINDEX, D3DTSS_TCI_PASSTHRU|0);
		DX8Wrapper::Set_DX8_Texture_Stage_State(0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
	}
	DX8Wrapper::Set_DX8_Texture_Stage_State(1, D3DTSS_TEXCOORDINDEX, 1);
	DX8Wrapper::Set_DX8_Texture_Stage_State(1, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
	DX8Wrapper::Set_DX8_Texture_Stage_State(2, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
	DX8Wrapper::Set_DX8_Texture_Stage_State(2, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
	DX8Wrapper::Set_DX8_Texture_Stage_State(2, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
	DX8Wrapper::Set_DX8_Texture_Stage_State(2, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
	DX8Wrapper::Set_DX8_Texture_Stage_State(2, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
#endif
}

// Bed material detail and its mip chain are generated once and ship with the executable.
// The terrain fallback refreshes when the map heights or dimensions change.
void WaterRenderObjClass::updateModernWaterTextures(void)
{
#if ZH_MODERN_WATER
	if (!m_modernBedTexture) {
		const Int side=256;
		std::vector<UnsignedInt> pixels(side*side);
		for (Int y=0;y<side;++y) for (Int x=0;x<side;++x) pixels[y*side+x]=WaterBedMaterial::Texel(x,y,side);
		m_modernBedTexture=NEW_REF(TextureClass,(side,side,WW3D_FORMAT_A8R8G8B8,MIP_LEVELS_ALL));
		for (Int size=side,level=0;size>0;size/=2,++level) {
			SurfaceClass *surface=m_modernBedTexture->Get_Surface_Level(level);
			Int pitch; void *bits=surface->Lock(&pitch);
			if (bits) {
				for (Int y=0;y<size;++y) memcpy((char*)bits+y*pitch,&pixels[y*size],size*4);
				surface->Unlock();
			}
			REF_PTR_RELEASE(surface);
			if (size==1) break;
			std::vector<UnsignedInt> mip(size*size/4);
			for (Int y=0;y<size/2;++y) for (Int x=0;x<size/2;++x) {
				UnsignedInt packed=0;
				for (Int shift=0;shift<32;shift+=8) {
					UnsignedInt sum=0;
					for (Int j=0;j<2;++j) for (Int i=0;i<2;++i) sum+=(pixels[(2*y+j)*size+2*x+i]>>shift)&255;
					packed|=((sum+2)/4)<<shift;
				}
				mip[y*(size/2)+x]=packed;
			}
			pixels.swap(mip);
		}
	}
	WorldHeightMap *map = TheTerrainRenderObject ? TheTerrainRenderObject->getMap() : NULL;
	if (!map || map->getXExtent()<=0 || map->getYExtent()<=0 || !map->getDataPtr()) {
		m_modernBowAdmissions.Clear();
		if(m_modernHeightMap || m_modernShoreReady) invalidateModernWaterShore();
		clearModernWaterOpticalTextures();
		REF_PTR_RELEASE(m_modernHeightTexture);
		m_modernHeights.clear();
		m_modernHeightWidth=0; m_modernHeightHeight=0; m_modernHeightSide=0; m_modernHeightBorder=-1;
		m_modernHeightMap=NULL; m_modernHeightData=NULL;
		return;
	}
	const Int width = map->getXExtent(), height = map->getYExtent();
	const size_t count = (size_t)width * height;
	const Bool sameTerrain=map == m_modernHeightMap && map->getDataPtr() == m_modernHeightData &&
		map->getBorderSize() == m_modernHeightBorder && width == m_modernHeightWidth && height == m_modernHeightHeight &&
		m_modernHeights.size() == count && memcmp(m_modernHeights.data(), map->getDataPtr(), count) == 0;
	if(sameTerrain && m_modernHeightTexture) return;
	if(!sameTerrain) invalidateModernWaterShore();
	if(m_modernHeightMap && (m_modernHeightMap!=map || m_modernHeightData!=map->getDataPtr())) { clearModernWaterFoam();m_modernBowAdmissions.Clear(); }
	clearModernWaterOpticalTextures();
	m_modernHeightMap=map; m_modernHeightData=map->getDataPtr();
	m_modernHeightBorder=map->getBorderSize();
	m_modernHeights.assign(map->getDataPtr(), map->getDataPtr() + count);
	if (width != m_modernHeightWidth || height != m_modernHeightHeight || !m_modernHeightTexture) {
		REF_PTR_RELEASE(m_modernHeightTexture);
		m_modernHeightSide = 1; while (m_modernHeightSide < max(width,height)) m_modernHeightSide *= 2;
		m_modernHeightTexture = NEW_REF(TextureClass, (m_modernHeightSide, m_modernHeightSide, WW3D_FORMAT_A8R8G8B8, MIP_LEVELS_1));
	}
	m_modernHeightWidth = width; m_modernHeightHeight = height;
	SurfaceClass *surface = m_modernHeightTexture->Get_Surface_Level();
	Int pitch; void *bits = surface->Lock(&pitch);
	if (bits) {
		for (Int y = 0; y < m_modernHeightSide; ++y) for (Int x = 0; x < m_modernHeightSide; ++x) {
			const UnsignedInt h = map->getHeight(min(x,width-1), min(y,height-1));
			// Alpha zero makes the height-only fallback explicitly opt out of wave cages.
			((UnsignedInt *)((char *)bits+y*pitch))[x] = (h<<16) | (h<<8) | h;
		}
		surface->Unlock();
	}
	REF_PTR_RELEASE(surface);
#endif
}

void WaterRenderObjClass::clearModernWaterOpticalTextures(void)
{
	m_modernProjectedSamples.clear();
	m_modernObstacleIndex.Clear();
	for (size_t i=0;i<m_modernOpticalTextures.size();++i) {
		REF_PTR_RELEASE(m_modernOpticalTextures[i].texture);
		REF_PTR_RELEASE(m_modernOpticalTextures[i].waveCage);
	}
	m_modernOpticalTextures.clear();
}

void WaterRenderObjClass::refreshModernWaterOpticalPolygons(void)
{
#if ZH_MODERN_WATER
	std::vector<WaterOpticalContext::Polygon> polygons;
	size_t points=0;
	Bool valid=TRUE;
	for (const WaterPolygonForDraw &waterPolygon:m_waterPolygonsForDraw) {
		const WaterPolygonForDraw *trigger=&waterPolygon;
		if (!trigger->isWaterArea() || trigger->isRiver() || trigger->getNumPoints()<3) continue;
		if ((size_t)trigger->getNumPoints()>WaterOpticalContext::MaxPoints-points) { valid=FALSE; break; }
		points+=trigger->getNumPoints();
		WaterOpticalContext::Polygon polygon;
		polygon.points.reserve(trigger->getNumPoints());
		for (Int i=0;i<trigger->getNumPoints();++i) {
			const ICoord3D *p=trigger->getPoint(i);
			polygon.points.push_back({(float)p->x,(float)p->y,(float)p->z});
		}
		polygons.push_back(polygon);
	}
	if (valid!=m_modernOpticalPolygonsValid || !WaterOpticalContext::SamePolygons(polygons,m_modernOpticalPolygons)) {
		invalidateModernWaterShore();
		clearModernWaterOpticalTextures();
		if(m_modernOpticalPolygonsValid) { clearModernWaterFoam();m_modernBowAdmissions.Clear(); }
		m_modernOpticalPolygons.swap(polygons);
		m_modernOpticalPolygonsValid=valid;
	}
#endif
}

TextureClass *WaterRenderObjClass::modernWaterOpticalTexture(Real level)
{
#if ZH_MODERN_WATER
	if (!m_modernOpticalPolygonsValid) return NULL;
	for (size_t i=0;i<m_modernOpticalTextures.size();++i)
		if (m_modernOpticalTextures[i].level==level) return m_modernOpticalTextures[i].texture;
	// Keep the first levels for this map revision. Overflow stays on the clear-water profile instead
	// of evicting/rebuilding textures every frame on maps with many water elevations.
	if (m_modernOpticalTextures.size()>=WaterOpticalContext::MaxLevels) return NULL;
	m_modernOpticalTextures.push_back({level,NULL});
	WaterOpticalContext::Field field;
	if (!WaterOpticalContext::Build(m_modernHeights.empty()?NULL:m_modernHeights.data(),
		m_modernHeightWidth,m_modernHeightHeight,m_modernHeightBorder,MAP_XY_FACTOR,MAP_HEIGHT_SCALE,
		level,m_modernOpticalPolygons,field)) return NULL;
	m_modernOpticalTextures.back().shoreContext=field;
	TextureClass *texture=NEW_REF(TextureClass,(m_modernHeightSide,m_modernHeightSide,WW3D_FORMAT_A8R8G8B8,MIP_LEVELS_1));
	SurfaceClass *surface=texture->Get_Surface_Level();
	Int pitch; void *bits=surface->Lock(&pitch);
	if (bits) {
		for (Int y=0;y<m_modernHeightSide;++y) for (Int x=0;x<m_modernHeightSide;++x) {
			const size_t n=(size_t)min(y,m_modernHeightHeight-1)*m_modernHeightWidth+min(x,m_modernHeightWidth-1);
			((UnsignedInt*)((char*)bits+y*pitch))[x]=WaterOpticalContext::Texel(field,n);
		}
		surface->Unlock();
	}
	REF_PTR_RELEASE(surface);
	if (!bits) { REF_PTR_RELEASE(texture); return NULL; }
	ModernOpticalTexture &cached=m_modernOpticalTextures.back();
	cached.texture=texture;
	{
		WaterWaveCage::Field cage;
		if(WaterWaveCage::Build(m_modernHeights.data(),m_modernHeightWidth,m_modernHeightHeight,
			m_modernHeightBorder,MAP_XY_FACTOR,MAP_HEIGHT_SCALE,level,field,cage)) {
			TextureClass *cageTexture=NEW_REF(TextureClass,(m_modernHeightSide,m_modernHeightSide,WW3D_FORMAT_A8R8G8B8,MIP_LEVELS_1));
			SurfaceClass *cageSurface=cageTexture?cageTexture->Get_Surface_Level():NULL;
			Int cagePitch=0;void *cageBits=cageSurface?cageSurface->Lock(&cagePitch):NULL;
			const Bool uploaded=cageBits && cagePitch>=m_modernHeightSide*4;
			if(uploaded)
				for(Int y=0;y<m_modernHeightSide;++y) for(Int x=0;x<m_modernHeightSide;++x) {
					const size_t n=size_t(min(y,m_modernHeightHeight-1))*m_modernHeightWidth+min(x,m_modernHeightWidth-1);
					((UnsignedInt*)((char*)cageBits+y*cagePitch))[x]=WaterWaveCage::Texel(cage,n);
				}
			if(cageBits) cageSurface->Unlock();
			REF_PTR_RELEASE(cageSurface);
			if(uploaded) {
				cached.waveCage=cageTexture;
				cached.waveCageSupport.resize(cage.texels.size());
				for(size_t n=0;n<cage.texels.size();++n) cached.waveCageSupport[n]=UnsignedByte(cage.texels[n]>>24);
			}
			else REF_PTR_RELEASE(cageTexture); // fall back to the original height texture, with zero support
		}
	}
	cached.ocean.swap(field.ocean);
	// Retain exactly the bytes uploaded to Texture4, including its bilinear shoreline blend.
	Int oceanMinX=m_modernHeightWidth,oceanMinY=m_modernHeightHeight,oceanMaxX=-1,oceanMaxY=-1;
	for (Int y=0;y<m_modernHeightHeight;++y) for (Int x=0;x<m_modernHeightWidth;++x)
		if (cached.ocean[y*m_modernHeightWidth+x]) {
			oceanMinX=min(oceanMinX,x);oceanMinY=min(oceanMinY,y);oceanMaxX=max(oceanMaxX,x);oceanMaxY=max(oceanMaxY,y);
		}
	if (oceanMaxX>=0) cached.oceanExtent=max(oceanMaxX-oceanMinX,oceanMaxY-oceanMinY)*MAP_XY_FACTOR;
	return texture;
#else
	return NULL;
#endif
}



//-------------------------------------------------------------------------------------------------
/** Destructor. Releases w3d assets. */
//-------------------------------------------------------------------------------------------------
WaterRenderObjClass::~WaterRenderObjClass(void)
{
	REF_PTR_RELEASE(m_meshVertexMaterialClass);
	REF_PTR_RELEASE(m_vertexMaterialClass);
	REF_PTR_RELEASE(m_meshLight);
	REF_PTR_RELEASE(m_alphaClippingTexture);
	REF_PTR_RELEASE (m_skyBox);

	REF_PTR_RELEASE (m_riverTexture);
	REF_PTR_RELEASE (m_whiteTexture);
	REF_PTR_RELEASE (m_waterNoiseTexture);
	REF_PTR_RELEASE (m_riverAlphaEdge);
	REF_PTR_RELEASE (m_waterSparklesTexture);

	Int i;

	for(i=0; i<TIME_OF_DAY_COUNT; i++)
	{	REF_PTR_RELEASE(m_settings[i].skyTexture);
		REF_PTR_RELEASE(m_settings[i].waterTexture);
	}

	i=NUM_BUMP_FRAMES;
	while (i--)
	{	SAFE_RELEASE( m_pBumpTexture[i]);
		SAFE_RELEASE( m_pBumpTexture2[i]);
	}

	//Release strings allocated inside global water settings.
	for  (i=0; i<TIME_OF_DAY_COUNT; i++)
	{	WaterSettings[i].m_skyTextureFile.clear();
		WaterSettings[i].m_waterTextureFile.clear();
	}
	((WaterTransparencySetting*)TheWaterTransparency.getNonOverloadedPointer())->deleteInstance();
	TheWaterTransparency = NULL;
	ReleaseResources();

	if (m_waterTrackSystem)
		delete m_waterTrackSystem;
}

//-------------------------------------------------------------------------------------------------
/** Constructor. Just nulls out some variables. */
//-------------------------------------------------------------------------------------------------
WaterRenderObjClass::WaterRenderObjClass(void)
{
#if ZH_MODERN_WATER
	m_useModernWater = wantsModernWater();
#endif
	memset( &m_settings, 0, sizeof( m_settings ) );
	m_dx=0;
	m_dy=0;
	m_indexBuffer=NULL;
	m_waterTrackSystem = NULL;
	m_meshVertexMaterialClass=NULL;
	m_meshLight=NULL;
	m_vertexMaterialClass=NULL;
	m_alphaClippingTexture=NULL;
	m_useCloudLayer=true;
	m_waterType = WATER_TYPE_0_TRANSLUCENT;
	m_tod=TIME_OF_DAY_AFTERNOON;
	m_pReflectionTexture=NULL;
	m_reflectionLevel=0;
	m_reflectionAreaMin.Set(-1.0f, -1.0f);
	m_reflectionAreaMax.Set(1.0f, 1.0f);
	m_reflectionCameraTransform.Make_Identity();
	m_reflectionReused=FALSE;
	m_skyBox=NULL;
	m_vertexBufferD3D=NULL;
	m_indexBufferD3D=NULL;
	m_vertexBufferD3DOffset=0;

	m_dwWavePixelShader=NULL;
	m_dwWaveVertexShader=NULL;
	m_waveVertexDeclaration=NULL;
	m_meshGeneration = 0;
	m_drawnCellsX = 0;
	m_drawnCellsY = 0;
	m_drawnCellSize = 0.0f;
	m_drawnMotionEnabled = FALSE;
	m_drawnPushed = FALSE;
	m_drawnTransform.Make_Identity();

	Int i=NUM_BUMP_FRAMES;
	while (i--) {
		m_pBumpTexture[i]=NULL;
		m_pBumpTexture2[i]=NULL;
	}

	m_riverVOrigin=0;
	m_riverTexture=NULL;
	m_whiteTexture=NULL;
	m_waterNoiseTexture=NULL;
	m_riverAlphaEdge=NULL;
	m_waterPixelShader=0;		///<D3D handle to pixel shader.
	m_riverWaterPixelShader=0;		///<D3D handle to pixel shader.
	m_trapezoidWaterPixelShader=0;		///<D3D handle to pixel shader.
	m_reflectionPixelShader=0;
	m_riverWaterFoamPixelShader=0;
	m_trapezoidWaterFoamPixelShader=0;
	m_waterSparklesTexture=0;
	m_riverXOffset=0;
	m_riverYOffset=0;
	m_iBumpFrame=0;
	m_bumpFrameAccum=0.0f;
}

//-------------------------------------------------------------------------------------------------
/** WW3D method that returns object bounding sphere used in frustum culling*/
//-------------------------------------------------------------------------------------------------
void WaterRenderObjClass::Get_Obj_Space_Bounding_Sphere(SphereClass & sphere) const
{
	//Since this object is more of a system (containing lots of water pieces),
	//let's disable culling by making bounds huge.  Let each piece do it's own cull.
	Vector3	ObjSpaceCenter(0,0,0);
//	Vector3	ObjSpaceRadius(m_dx,m_dy,0);
	Vector3	ObjSpaceRadius(50000,50000,0);

	sphere.Init(ObjSpaceCenter,ObjSpaceRadius.Length());
}

//-------------------------------------------------------------------------------------------------
/** WW3D method that returns object bounding box used in collision detection*/
//-------------------------------------------------------------------------------------------------
void WaterRenderObjClass::Get_Obj_Space_Bounding_Box(AABoxClass & box) const
{
	//Since this object is more of a system (containing lots of water pieces),
	//let's disable culling by making bounds huge.  Let each piece do it's own cull.

	Vector3	ObjSpaceCenter(0,0,0);
	Vector3	ObjSpaceExtents(50000,50000,max(0.001f*m_dy,WaterSurfaceModel::MaxDisplacement));	//since mirror is a plane, it has no thickness. Set to m_dy/1000.

	box.Init(ObjSpaceCenter,ObjSpaceExtents);
}

//-------------------------------------------------------------------------------------------------
/** returns the class id, so the scene can tell what kind of render object it has. */
//-------------------------------------------------------------------------------------------------
Int WaterRenderObjClass::Class_ID(void) const
{
	return RenderObjClass::CLASSID_UNKNOWN;
}

//-------------------------------------------------------------------------------------------------
/** Not used, but required virtual method. */
//-------------------------------------------------------------------------------------------------
RenderObjClass *	 WaterRenderObjClass::Clone(void) const
{
	assert(false);
	return NULL;
}

//-------------------------------------------------------------------------------------------------
/** Copies raw bits from pBumpSrc (a regular grayscale texture) into a D3D
	*   bump-map format. */
//-------------------------------------------------------------------------------------------------
RenderResult WaterRenderObjClass::initBumpMap(LPDIRECT3DTEXTURE9 *pTex, TextureClass *pBumpSource)
{
    SurfaceClass::SurfaceDescription    d3dsd;
	SurfaceClass * surf;
    D3DLOCKED_RECT     d3dlr;
	UnsignedInt dwSrcPitch;
	BYTE* pSrc;
	Int numLevels;

#ifdef MIPMAP_BUMP_TEXTURE

	pBumpSource->Get_Level_Description(d3dsd);

	if (Get_Bytes_Per_Pixel(d3dsd.Format) != 4)
	{
		// LORENZEN WAS BUGGED BY THIS, 
		//		DEBUG_CRASH(("WaterRenderObjClass::Invalid BumpMap format - Was it compressed?") );
		return D3D_OK;
	}

	if (pBumpSource->Peek_D3D_Texture())
	{
		numLevels=pBumpSource->Peek_D3D_Texture()->GetLevelCount();
	}
	else
		return D3D_OK;

	pTex[0]=DX8Wrapper::_Create_DX8_Texture(d3dsd.Width,d3dsd.Height,WW3D_FORMAT_U8V8,MIP_LEVELS_ALL,D3DPOOL_MANAGED,false);

	for (Int level=0; level < numLevels; level++)
	{
		surf=pBumpSource->Get_Surface_Level(level);
		surf->Get_Description(d3dsd);
		pSrc=(unsigned char *)surf->Lock((int *)&dwSrcPitch);

		pTex[0]->LockRect( level, &d3dlr, 0, 0 );
		UnsignedInt dwDstPitch = (UnsignedInt)d3dlr.Pitch;
		BYTE* pDst       = (BYTE*)d3dlr.pBits;

		for( UnsignedInt y=0; y<d3dsd.Height; y++ )
		{
			BYTE* pDstT  = pDst;
			BYTE* pSrcB0 = (BYTE*)pSrc;
			BYTE* pSrcB1 = ( pSrcB0 + dwSrcPitch );
			BYTE* pSrcB2 = ( pSrcB0 - dwSrcPitch );

			if( y == d3dsd.Height-1 )  // Don't go past the last line
				pSrcB1 = pSrcB0;
			if( y == 0 )               // Don't go before first line
				pSrcB2 = pSrcB0;

			for( UnsignedInt x=0; x<d3dsd.Width; x++ )
			{
				Int v00 = 256-*(pSrcB0+0); // Get the current pixel
				Int v01 = 256-*(pSrcB0+4); // and the pixel to the right
				Int vM1 = 256-*(pSrcB0-4); // and the pixel to the left
				Int v10 = 256-*(pSrcB1+0); // and the pixel one line below.
				Int v1M = 256-*(pSrcB2+0); // and the pixel one line above.

				Int iDu = (vM1-v01); // The delta-u bump value
				Int iDv = (v1M-v10); // The delta-v bump value

				if( (v00 < vM1) && (v00 < v01) )  // If we are at valley
				{
					iDu = vM1-v00;                 // Choose greater of 1st order diffs
					if( iDu < v00-v01 )
						iDu = v00-v01;
				}

				// The luminance bump value (land masses are less shiny)
				WORD uL = ( v00>1 ) ? 63 : 127;

				switch( D3DFMT_V8U8)//m_BumpMapFormat )
				{
					case D3DFMT_V8U8:
						*pDstT++ = (BYTE)iDu;
						*pDstT++ = (BYTE)iDv;
						break;

					case D3DFMT_L6V5U5:
						*(WORD*)pDstT  = (WORD)( ( (iDu>>3) & 0x1f ) <<  0 );
						*(WORD*)pDstT |= (WORD)( ( (iDv>>3) & 0x1f ) <<  5 );
						*(WORD*)pDstT |= (WORD)( ( ( uL>>2) & 0x3f ) << 10 );
						pDstT += 2;
						break;

					case D3DFMT_X8L8V8U8:
						*pDstT++ = (BYTE)iDu;
						*pDstT++ = (BYTE)iDv;
						*pDstT++ = (BYTE)uL;
						*pDstT++ = (BYTE)0L;
						break;
				}

				// Move one pixel to the left (src is 32-bpp)
				pSrcB0+=4;   pSrcB1+=4;   pSrcB2+=4;
			}

			// Move to the next line
			pSrc += dwSrcPitch;    pDst += dwDstPitch;
		}

		pTex[0]->UnlockRect(level);
		surf->Unlock();
		REF_PTR_RELEASE (surf);
	}//for each level

#else
	surf=pBumpSource->Get_Surface_Level();
	surf->Get_Description(d3dsd);
	pSrc=(unsigned char *)surf->Lock((int *)&dwSrcPitch);

    // Create the bumpmap's surface and texture objects
	m_pBumpTexture[i]=DX8Wrapper::_Create_DX8_Texture(d3dsd.Width,d3dsd.Height,WW3D_FORMAT_U8V8,TextureClass::MIP_LEVELS_1,D3DPOOL_MANAGED,false);

    // Fill the bits of the new texture surface with bits from
    // a private format.

    m_pBumpTexture[i]->LockRect( 0, &d3dlr, 0, 0 );
    UnsignedInt dwDstPitch = (UnsignedInt)d3dlr.Pitch;
    BYTE* pDst       = (BYTE*)d3dlr.pBits;

    for( UnsignedInt y=0; y<d3dsd.Height; y++ )
    {
        BYTE* pDstT  = pDst;
        BYTE* pSrcB0 = (BYTE*)pSrc;
        BYTE* pSrcB1 = ( pSrcB0 + dwSrcPitch );
        BYTE* pSrcB2 = ( pSrcB0 - dwSrcPitch );

        if( y == d3dsd.Height-1 )  // Don't go past the last line
            pSrcB1 = pSrcB0;
        if( y == 0 )               // Don't go before first line
            pSrcB2 = pSrcB0;

        for( UnsignedInt x=0; x<d3dsd.Width; x++ )
        {
            Int v00 = 256-*(pSrcB0+0); // Get the current pixel
            Int v01 = 256-*(pSrcB0+4); // and the pixel to the right
            Int vM1 = 256-*(pSrcB0-4); // and the pixel to the left
            Int v10 = 256-*(pSrcB1+0); // and the pixel one line below.
            Int v1M = 256-*(pSrcB2+0); // and the pixel one line above.

            Int iDu = (vM1-v01); // The delta-u bump value
            Int iDv = (v1M-v10); // The delta-v bump value

            if( (v00 < vM1) && (v00 < v01) )  // If we are at valley
            {
                iDu = vM1-v00;                 // Choose greater of 1st order diffs
                if( iDu < v00-v01 )
                    iDu = v00-v01;
            }

            // The luminance bump value (land masses are less shiny)
            WORD uL = ( v00>1 ) ? 63 : 127;

            switch( D3DFMT_V8U8)//m_BumpMapFormat )
            {
                case D3DFMT_V8U8:
                    *pDstT++ = (BYTE)iDu;
                    *pDstT++ = (BYTE)iDv;
                    break;

                case D3DFMT_L6V5U5:
                    *(WORD*)pDstT  = (WORD)( ( (iDu>>3) & 0x1f ) <<  0 );
                    *(WORD*)pDstT |= (WORD)( ( (iDv>>3) & 0x1f ) <<  5 );
                    *(WORD*)pDstT |= (WORD)( ( ( uL>>2) & 0x3f ) << 10 );
                    pDstT += 2;
                    break;

                case D3DFMT_X8L8V8U8:
                    *pDstT++ = (BYTE)iDu;
                    *pDstT++ = (BYTE)iDv;
                    *pDstT++ = (BYTE)uL;
                    *pDstT++ = (BYTE)0L;
                    break;
            }

            // Move one pixel to the left (src is 32-bpp)
            pSrcB0+=4;   pSrcB1+=4;   pSrcB2+=4;
        }

        // Move to the next line
        pSrc += dwSrcPitch;    pDst += dwDstPitch;
    }

    m_pBumpTexture[i]->UnlockRect(0);
    surf->Unlock();
#endif

    return D3D_OK;
}

//-------------------------------------------------------------------------------------------------
/** Create and fill a D3D vertex buffer with water surface vertices */
//-------------------------------------------------------------------------------------------------
RenderResult WaterRenderObjClass::generateVertexBuffer( Int sizeX, Int sizeY, Int vertexSize, Bool doStatic)
{
	m_numVertices=sizeX*sizeY;
	//Assuming dynamic vertex buffer, allocate maximum multiple of required size to allow rendering from
 	//different parts of the buffer. 5-15-03: Disabled this since we use DISCARD mode instead to avoid Nvidia Runtime bug. -MW
 	//m_numVertices=(65536 / (sizeX*sizeY))*sizeX*sizeY;

	SEA_PATCH_VERTEX* pVertices;

	Setting *setting=&m_settings[m_tod];

	RenderResult hr;

	//default setting for a dynamic vertex buffer
	D3DPOOL pool = D3DPOOL_DEFAULT;
	UnsignedInt usage = D3DUSAGE_WRITEONLY | D3DUSAGE_DYNAMIC;
	UnsignedInt fvf = WATER_MESH_FVF;

	if (doStatic)
	{	//change settings for a static vertex buffer
		pool = D3DPOOL_MANAGED;
		usage = D3DUSAGE_WRITEONLY;
		fvf=0;// DX8 Docs confusing on this. Say no FVF for vertex shaders. Else DX8_FVF_XYZDUV1;
		m_numVertices=sizeX*sizeY;
	}

	if (m_vertexBufferD3D == NULL)
	{	// Create vertex buffer

		if (Render_Failed(hr=m_pDev->CreateVertexBuffer
		(
			m_numVertices*vertexSize,
			usage,
			fvf,
			pool,
			&m_vertexBufferD3D,
			NULL	// pSharedHandle, D3D9's extra parameter, reserved and always null
		)))
			return hr;

		delete waterVertexTwin;
		waterVertexTwin=Direct3D11_Twin_Vertex_Buffer(m_numVertices*vertexSize,
			(usage & D3DUSAGE_DYNAMIC)!=0);
	}

	m_vertexBufferD3DOffset=0;

	if (!doStatic)
		return D3D_OK;	//only create the buffer, other code will fill it.

	// load results into buffer
	if (Render_Failed(hr=m_vertexBufferD3D->Lock
	(
		0,
		m_numVertices*sizeof(SEA_PATCH_VERTEX),
		(void**)&pVertices,
		0//D3DLOCK_DISCARD
	)))
		return hr;

	DX11BufferLockClass vertexLock;
	if (void *mirror=vertexLock.Begin(waterVertexTwin, pVertices, 0,
			m_numVertices*sizeof(SEA_PATCH_VERTEX), 0))
		pVertices=(SEA_PATCH_VERTEX*)mirror;

	Int x,z;
	for (z=0; z<sizeY; z++)
	{
		for (x=0; x<sizeX; x++)
		{
			pVertices->x=(float)x;
			pVertices->y=m_level;
			pVertices->z=(float)z;

			pVertices->tu=(float)x*PATCH_UV_SCALE;
			pVertices->tv=(float)z*PATCH_UV_SCALE;
			pVertices->c=setting->transparentWaterDiffuse;	//vertex alpha/color
			pVertices++;
		}
	}

	vertexLock.End();

	if (Render_Failed(hr=m_vertexBufferD3D->Unlock())) return hr;

	return D3D_OK;
}

//-------------------------------------------------------------------------------------------------
/** Create and fill a D3D index buffer with water surface strip indices */
//-------------------------------------------------------------------------------------------------
RenderResult WaterRenderObjClass::generateIndexBuffer(Int sizeX, Int sizeY)
{
	RenderResult hr;

	//Will need SizeY-1 strips, each of length SizeX*2 (2 indices per strip segment).
	//Will also need 2 extra indices to connect each strip to next one (except last strip)
	//Total index buffer size = (SizeY-1)*(SizeX*2+2) - 2 (drop the extra 2 indices from last strip)

	m_numIndices=(sizeY-1)*(sizeX*2+2) - 2;

	//old way

	// Create index buffer
	WORD* pIndices;

	if (Render_Failed(hr=m_pDev->CreateIndexBuffer
	(
		(m_numIndices+2)*sizeof(WORD), 
		D3DUSAGE_WRITEONLY, 
		D3DFMT_INDEX16, 
		D3DPOOL_MANAGED,
		&m_indexBufferD3D,
		NULL	// pSharedHandle, D3D9's extra parameter, reserved and always null
	)))
		return hr;

	delete waterIndexTwin;
	waterIndexTwin=Direct3D11_Twin_Index_Buffer((m_numIndices+2)*sizeof(WORD), false);

	if (Render_Failed(hr=m_indexBufferD3D->Lock
	(
		0,
		m_numIndices*sizeof(WORD),
		(void**)&pIndices,
		0
	)))
		return hr;

	DX11BufferLockClass indexLock;
	if (void *mirror=indexLock.Begin(waterIndexTwin, pIndices, 0, m_numIndices*sizeof(WORD), 0))
		pIndices=(WORD*)mirror;

	Int i,j,k;

	for (i=0,j=0,k=0; i<m_numIndices; j++)
	{
		for (;k<(sizeX*(j+1)); k++,i+=2)
		{
			pIndices[i]=(UnsignedShort) k+sizeX;
			pIndices[i+1]=(UnsignedShort) k;
		}
		//Generate 4 degenerate triangle to connect current strip to next strip/row of map
		//To do this, we just repeat the last index of first strip and first index of new strip.
		//Any triangles with repeated vertices will be skipped during rendering.
		if (i<m_numIndices) //check if there is at least 1 more strip to go
		{
			pIndices[i]=k-1;
			pIndices[i+1]=k+sizeX;
			i+=2;
		}
	}

	/*Old way
	Int step=1;
	Int psize=(size-1)/step;

	m_numIndices=psize*((psize+1)*2)+(psize*2)-2;

	Int x,z,s_toggle=1;
	for (z=step; z<size; z+=step)
	{
		if (s_toggle)
		{
			for (x=0; x<(size-step); x+=step)
			{
				*pIndices++=(WORD)((z-0)*size+(x));
				*pIndices++=(WORD)((z-step)*size+(x));
			}
				*pIndices++=(WORD)((z-0)*size+(size-1));
			*pIndices++=(WORD)((z-step)*size+(size-1));
			// insert additional degenerate to start next row
			*pIndices++=pIndices[-2];
			*pIndices++=pIndices[-1];
		}
		else
		{
			*pIndices++=(WORD)((z-step)*size+(size-1));
			*pIndices++=(WORD)((z-0)*size+(size-1));
			for (x=size-1; x>0; x-=step)
			{
				*pIndices++=(WORD)((z-step)*size+(x-step));
				*pIndices++=(WORD)((z-0)*size+(x-step));
			}
			// insert additional degenerate to start next row
			*pIndices++=pIndices[-1];
			*pIndices++=pIndices[-1];
		}

		s_toggle=!s_toggle;
	}
*/
	indexLock.End();

	if (Render_Failed(hr=m_indexBufferD3D->Unlock())) return hr;

	return D3D_OK;
}

//-------------------------------------------------------------------------------------------------
/** Releases all w3d assets, to prepare for Reset device call. */
//-------------------------------------------------------------------------------------------------
void WaterRenderObjClass::ReleaseResources(void)
{
	clearModernWaterFoam(FALSE);
	// CPU admissions survive device resource refresh, including a resize while paused.
	clearModernWaterHulls(FALSE);
	REF_PTR_RELEASE(m_modernHeightTexture);
	REF_PTR_RELEASE(m_modernBedTexture);
	clearModernWaterOpticalTextures();
	m_modernObstacles.clear();

	// CPU terrain/source/history survive texture-only resize; the next update compares real geometry.

	REF_PTR_RELEASE(m_indexBuffer);

	REF_PTR_RELEASE(m_pReflectionTexture);
	SAFE_RELEASE(m_vertexBufferD3D);
	SAFE_RELEASE(m_indexBufferD3D);
	releaseWaterTwins();

	if (m_waterTrackSystem)
		m_waterTrackSystem->ReleaseResources();

	if (m_dwWavePixelShader)
		m_dwWavePixelShader->Release();

	if (m_dwWaveVertexShader)
		m_dwWaveVertexShader->Release();
	if (m_waveVertexDeclaration)
	{	m_waveVertexDeclaration->Release();
		m_waveVertexDeclaration=NULL;
	}

	if (m_waterPixelShader)
		m_waterPixelShader->Release();

	if (m_trapezoidWaterPixelShader)
		m_trapezoidWaterPixelShader->Release();

	if (m_reflectionPixelShader)
		m_reflectionPixelShader->Release();

	if (m_riverWaterPixelShader)
		m_riverWaterPixelShader->Release();

	if (m_riverWaterFoamPixelShader)
		m_riverWaterFoamPixelShader->Release();

	if (m_trapezoidWaterFoamPixelShader)
		m_trapezoidWaterFoamPixelShader->Release();

	m_dwWavePixelShader=0;
	m_dwWaveVertexShader=0;
	m_waterPixelShader = 0;
	m_trapezoidWaterPixelShader=0;
	m_reflectionPixelShader=0;
	m_riverWaterPixelShader=0;
	m_riverWaterFoamPixelShader=0;
	m_trapezoidWaterFoamPixelShader=0;
}

//-------------------------------------------------------------------------------------------------
/** (Re)allocates all W3D assets after a reset.. */
//-------------------------------------------------------------------------------------------------
void WaterRenderObjClass::ReAcquireResources(void)
{
	RenderResult hr;

	m_indexBuffer=NEW_REF(DX8IndexBufferClass,(6));
	// Fill up the IB
	{
		DX8IndexBufferClass::WriteLockClass lockIdxBuffer(m_indexBuffer);
		UnsignedShort *ib=lockIdxBuffer.Get_Index_Array();
		//quad of 2 triangles:
		//	3-----2
		//  |    /|
		//  |  /  |
		//	|/    |
		//  0-----1
		ib[0]=3;
		ib[1]=0;
		ib[2]=2;
		ib[3]=2;
		ib[4]=0;
		ib[5]=1;
	}

	m_pDev=DX8Wrapper::_Get_D3D_Device();

	//We're using the same grid for either 3D Water Mesh or Pixel/Vertex shader.  Just
	//allocate the right size depending on usage.  The grid's mesh, once allocated, is never freed
	//for good, so "buffers built for an allocation" is the old "m_meshData != NULL"; and before the
	//first one (init's call here, ahead of TerrainLogic) there is no grid to ask.
	if (m_meshGeneration != 0)
	{
		WaterGrid *grid = TheTerrainLogic->getWaterGrid();
		//Create new grid data
		if (Render_Failed(generateIndexBuffer(grid->getCellsX()+1,grid->getCellsY()+1)))
			return;
		if (Render_Failed(generateVertexBuffer(grid->getCellsX()+1,grid->getCellsY()+1,sizeof(MaterMeshVertexFormat),false)))
			return;
	}
	else
	if (m_waterType == WATER_TYPE_2_PVSHADER)
	{	//pixel/vertex shader based water assets.
		if (Render_Failed(hr=generateIndexBuffer(PATCH_SIZE,PATCH_SIZE)))
			return;

		if (Render_Failed(hr=generateVertexBuffer(PATCH_SIZE,PATCH_SIZE,sizeof(SEA_PATCH_VERTEX),true)))
			return;

		//shader decleration
		RenderUInt32 Declaration[]=
		{
			(D3DVSD_STREAM(0)),
			(D3DVSD_REG(0, D3DVSDT_FLOAT3)), // Position
			(D3DVSD_REG(1, D3DVSDT_D3DCOLOR)), // Diffuse
			(D3DVSD_REG(2, D3DVSDT_FLOAT2)), // Bump map texture	
			(D3DVSD_END())
		};

		hr = W3DShaderManager::LoadAndCreateD3DPixelShader("shaders\\wave.pso", &m_dwWavePixelShader);
		if (Render_Failed(hr))
			return;

		hr = W3DShaderManager::LoadAndCreateD3DVertexShader("shaders\\wave.vso", &Declaration[0], &m_dwWaveVertexShader, &m_waveVertexDeclaration);
		if (Render_Failed(hr))
			return;

		// Create reflection texture
		m_pReflectionTexture = DX8Wrapper::Create_Render_Target (SEA_REFLECTION_SIZE, SEA_REFLECTION_SIZE);
	}

	if (m_waterType == WATER_TYPE_0_TRANSLUCENT)
		m_pReflectionTexture = DX8Wrapper::Create_Render_Target (WATER_REFLECTION_SIZE, WATER_REFLECTION_SIZE);

	if (m_waterTrackSystem)
		m_waterTrackSystem->ReAcquireResources();

	if (W3DShaderManager::getChipset() >= DC_GENERIC_PIXEL_SHADER_1_1)
	{
		ID3DXBuffer *compiledShader;
		char *shader = 
			"ps.1.1\n \
			tex t0 \n\
			tex t1	\n\
			tex t2	\n\
			tex t3\n\
			mul r0.rgb, v0, t0 ; blend vertex color into t0. \n\
			mov r0.a, t0 ; the vertex alpha now carries the shroud, and must not fade the water itself. \n\
			mul r1, t1, t2 ; mul\n\
			add r1.rgb, r1, t3\n\
			mul r1.rgb, r1, v0.a ; sparkles and the edge glow do get darkened by the shroud. \n\
			+mul r0.a, r0, t3\n\
			add r0.rgb, r0, r1\n";
		hr = D3DXAssembleShader( shader, (UINT)strlen(shader), NULL, NULL, 0, &compiledShader, NULL);
		if (hr==0) {
			hr = 	DX8Wrapper::_Get_D3D_Device()->CreatePixelShader((RenderUInt32*)compiledShader->GetBufferPointer(), &m_riverWaterPixelShader);
			// the same bytecode under a second name, which the D3D11 backend draws with foam (WaterFoam)
			DX8Wrapper::_Get_D3D_Device()->CreatePixelShader((RenderUInt32*)compiledShader->GetBufferPointer(), &m_riverWaterFoamPixelShader);
			compiledShader->Release();
			Direct3D11_Register_Engine_Shader(m_riverWaterFoamPixelShader, "river water foam ps.1.1");
		}
		shader = 
			"ps.1.1\n \
			tex t0 \n\
			tex t1	\n\
			texbem t2, t1 ; use t1 as env map adjustment on t2.\n\
			mul r0,v0,t0 ; blend vertex color into t0. \n\
			mul r1.rgb,t2,c0 ; reduce t2 (environment mapped reflection) by constant\n\
			add r0.rgb, r0, r1";
		hr = D3DXAssembleShader( shader, (UINT)strlen(shader), NULL, NULL, 0, &compiledShader, NULL);
		if (hr==0) {
			hr = 	DX8Wrapper::_Get_D3D_Device()->CreatePixelShader((RenderUInt32*)compiledShader->GetBufferPointer(), &m_waterPixelShader);
			compiledShader->Release();
			Direct3D11_Register_Engine_Shader(m_waterPixelShader, "environment water ps.1.1");
		}
		shader = 
			"ps.1.1\n \
			tex t0 ;get water texture\n\
			tex t1 ;get white highlights on black background\n\
			tex t2 ;get white highlights with more tiling\n\
			tex t3	; get black shroud \n\
			mul r0,v0,t0 ; blend vertex color and alpha into base texture. \n\
			mad r0.rgb, t1, t2, r0	; blend sparkles and noise \n\
			mul r0.rgb, r0, t3 ; blend in black shroud \n\
			;\n";
		hr = D3DXAssembleShader( shader, (UINT)strlen(shader), NULL, NULL, 0, &compiledShader, NULL);
		if (hr==0) {
			hr = 	DX8Wrapper::_Get_D3D_Device()->CreatePixelShader((RenderUInt32*)compiledShader->GetBufferPointer(), &m_trapezoidWaterPixelShader);
			DX8Wrapper::_Get_D3D_Device()->CreatePixelShader((RenderUInt32*)compiledShader->GetBufferPointer(), &m_trapezoidWaterFoamPixelShader);
			compiledShader->Release();
			Direct3D11_Register_Engine_Shader(m_trapezoidWaterFoamPixelShader, "trapezoid water foam ps.1.1");
		}
		shader =
			"ps.1.1\n \
			def c1, 0.333333, 0.333333, 0.333333, 0\n\
			tex t0 ; the mirrored scene, looked up through the projected stage, white where nothing stands\n\
			dp3 r1.rgb, t0, c1 ; how bright the mirror is here\n\
			mov r1.rgb, 1-r1 ; how far that is from the white it was cleared to\n\
			add r1.rgb, r1, r1\n\
			add r1.rgb, r1, r1 ; four times and clamped, so anything standing there counts in full whatever its colour\n\
			mul r0.rgb, r1, c0 ; darkened by the strength\n\
			mov r0.rgb, 1-r0 ; multiplied into the water, so white leaves it as it was\n\
			+mov r0.a, c0\n";
		hr = D3DXAssembleShader( shader, (UINT)strlen(shader), NULL, NULL, 0, &compiledShader, NULL);
		if (hr==0) {
			hr = 	DX8Wrapper::_Get_D3D_Device()->CreatePixelShader((RenderUInt32*)compiledShader->GetBufferPointer(), &m_reflectionPixelShader);
			compiledShader->Release();
			Direct3D11_Register_Engine_Shader(m_reflectionPixelShader, "water reflection ps.1.1");
		}
	}

	registerWaterStyleShaders();

	//W3D Invalidate textures after losing the device and since we peek at the textures directly, it won't
	//know to reinit them for us.  Do it here manually:
	if (m_riverTexture && !m_riverTexture->Is_Initialized())
		m_riverTexture->Init();
	if (m_waterNoiseTexture && !m_waterNoiseTexture->Is_Initialized())
		m_waterNoiseTexture->Init();
	if (m_riverAlphaEdge && !m_riverAlphaEdge->Is_Initialized())
		m_riverAlphaEdge->Init();
	if (m_waterSparklesTexture && !m_waterSparklesTexture->Is_Initialized())
		m_waterSparklesTexture->Init();
	if (m_whiteTexture && !m_whiteTexture->Is_Initialized())
	{	m_whiteTexture->Init();
		SurfaceClass *surface=m_whiteTexture->Get_Surface_Level();
		surface->DrawPixel(0,0,0xffffffff);
		REF_PTR_RELEASE(surface);
	}
}

void WaterRenderObjClass::load(void)
{
	if (m_waterTrackSystem)
		m_waterTrackSystem->loadTracks();
}

//-------------------------------------------------------------------------------------------------
/** Initializes water with dimensions and parent scene.
	* During rendering, we will render a water surface of given dimensions
	* and reflect the parent scene in its surface.  For now, waters are
	* forced to be rectangles. */
//-------------------------------------------------------------------------------------------------
Int WaterRenderObjClass::init(Real waterLevel, Real dx, Real dy, SceneClass *parentScene, WaterType type)
{
	// The water is a mesh, a bump map painted pixel by pixel and a set of render targets, all of
	// which want a device.  Under -nodevice there is none, and nothing looks at water.
	if (TheGlobalData && TheGlobalData->m_noRenderDevice)
		return 0;

	m_iBumpFrame=0;
	m_bumpFrameAccum=0.0f;
	m_fBumpScale=SEA_BUMP_SCALE;

	m_dx=dx;
	m_dy=dy;
	m_level=waterLevel;

	m_LastUpdateTime=Clock_Milliseconds();
	m_uScrollPerMs=0.001f;
	m_vScrollPerMs=0.001f;
	m_uOffset=0;
	m_vOffset=0;

	m_parentScene=parentScene;
	m_waterType = type;

	/// Hack for now
	//m_waterType = WATER_TYPE_0_TRANSLUCENT;

	///@todo: calculate a real normal/distance for arbitrary planes.
	m_planeNormal=Vector3(0,0,1);		//water plane normal
	m_planeDistance=m_level;	//water plane distance(always at zero for now)

	m_meshLight=NEW_REF(LightClass,(LightClass::DIRECTIONAL));	
	m_meshLight->Set_Ambient(Vector3(0.1f,0.1f,0.1f));
	m_meshLight->Set_Diffuse(Vector3(1.0f,1.0f,1.0f));
	m_meshLight->Set_Specular(Vector3(1.0f,1.0f,1.0f));
	m_meshLight->Set_Position(Vector3(1000,1000,1000));
	//testLight->Set_Spot_Direction(Vector3(TheGlobalData->m_terrainLightX,TheGlobalData->m_terrainLightY,TheGlobalData->m_terrainLightZ));
	m_meshLight->Set_Spot_Direction(Vector3(-0.57f,-0.57f,-0.57f));

	//Setup material for 3D Mesh water.
	m_meshVertexMaterialClass=NEW_REF(VertexMaterialClass,());
	m_meshVertexMaterialClass->Set_Shininess(20.0);
	m_meshVertexMaterialClass->Set_Ambient(1.0f,1.0f,1.0f);
	m_meshVertexMaterialClass->Set_Diffuse(1.0f,1.0f,1.0f);
	m_meshVertexMaterialClass->Set_Specular(0.5,0.5,0.5);
	m_meshVertexMaterialClass->Set_Opacity(WATER_MESH_OPACITY);
	m_meshVertexMaterialClass->Set_Lighting(true);

	//
	// assign the data from the WaterSettings[] global to the data for this
	// render object (we at present only have one water plane)
	//
	loadSetting( &m_settings[ TIME_OF_DAY_MORNING ], TIME_OF_DAY_MORNING );
	loadSetting( &m_settings[ TIME_OF_DAY_AFTERNOON ], TIME_OF_DAY_AFTERNOON );
	loadSetting( &m_settings[ TIME_OF_DAY_EVENING ], TIME_OF_DAY_EVENING );
	loadSetting( &m_settings[ TIME_OF_DAY_NIGHT ], TIME_OF_DAY_NIGHT );

	Set_Sort_Level(2);	//force water to be drawn after all other non translucent objects in scene.
	Set_Force_Visible(TRUE);	//water is always visible since it's a composite object made of multiple planes all over the map.

	ReAcquireResources();
#if 0	//MD does not support the old bump-mapped water at all so no point loading textures. -MW 8-11-03
	if (type == WATER_TYPE_2_PVSHADER || (W3DShaderManager::getChipset() >= DC_GENERIC_PIXEL_SHADER_1_1))
	{	//geforce3 specific water requires some extra D3D assets
		m_pDev=DX8Wrapper::_Get_D3D_Device();
		//save previous thumbnail mode
		bool thumbnails_enabled = WW3D::Get_Thumbnail_Enabled();
		WW3D::Set_Thumbnail_Enabled(false);

		//load bump map textures off disk
		TextureClass *pBumpSource;	//temporary textures in a format W3D understands
		TextureClass *pBumpSource2;	//temporary textures in a format W3D understands
		Int i;
		i=NUM_BUMP_FRAMES;
		while (i--)
		{
			char bump_name[128];

			sprintf(bump_name,"caust%.2d.tga",i);
			pBumpSource=WW3DAssetManager::Get_Instance()->Get_Texture(bump_name);
			sprintf(bump_name,"caustS%.2d.tga",i);
			pBumpSource2=WW3DAssetManager::Get_Instance()->Get_Texture(bump_name);
			initBumpMap(m_pBumpTexture+i, pBumpSource);
			initBumpMap(m_pBumpTexture2+i, pBumpSource2);
			WW3DAssetManager::Get_Instance()->Release_Texture(pBumpSource);
			WW3DAssetManager::Get_Instance()->Release_Texture(pBumpSource2);
			REF_PTR_RELEASE(pBumpSource);
			REF_PTR_RELEASE(pBumpSource2);
		}
		//restore previous thumpnail mode
		WW3D::Set_Thumbnail_Enabled(thumbnails_enabled);
	}
#endif

	//Setup material for regular water
	m_vertexMaterialClass=VertexMaterialClass::Get_Preset(VertexMaterialClass::PRELIT_DIFFUSE);


	m_shaderClass = zFillAlphaShader;//ShaderClass::_PresetAlphaShader;ShaderClass::_PresetOpaqueShader;//detailOpaqueShader;
	m_shaderClass.Set_Cull_Mode(ShaderClass::CULL_MODE_DISABLE);	//water should be visible from both sides

	//Assets used for all types of water
	m_alphaClippingTexture=WW3DAssetManager::Get_Instance()->Get_Texture(SKYBODY_TEXTURE);

#ifdef CLIP_GEOMETRY_TO_PLANE
	m_alphaClippingTexture=WW3DAssetManager::Get_Instance()->Get_Texture("alphaclip.tga");
#endif

	m_skyBox = ((W3DAssetManager*)W3DAssetManager::Get_Instance())->Create_Render_Obj( "new_skybox", TheGlobalData->m_skyBoxScale, 0);

	//Enable clamping on all textures used by the skybox (to reduce corner seams).
	if (m_skyBox && m_skyBox->Class_ID() == RenderObjClass::CLASSID_MESH)
	{
		MeshClass *mesh=(MeshClass*) m_skyBox;
		MaterialInfoClass	*material = mesh->Get_Material_Info();

		for (Int i=0; i<material->Texture_Count(); i++)
		{
			if (material->Peek_Texture(i))
			{		
				material->Peek_Texture(i)->Get_Filter().Set_U_Addr_Mode(TextureFilterClass::TEXTURE_ADDRESS_CLAMP);
				material->Peek_Texture(i)->Get_Filter().Set_V_Addr_Mode(TextureFilterClass::TEXTURE_ADDRESS_CLAMP);
			}
		}

		REF_PTR_RELEASE(material);
	}

	m_riverTexture=WW3DAssetManager::Get_Instance()->Get_Texture(TheWaterTransparency->m_standingWaterTexture.str()); 

	//For some reason setting a NULL texture does not result in 0xffffffff for pixel shaders so using explicit "white" texture.
	m_whiteTexture=MSGNEW("TextureClass") TextureClass(1,1,WW3D_FORMAT_A4R4G4B4,MIP_LEVELS_1);
	SurfaceClass *surface=m_whiteTexture->Get_Surface_Level();
	surface->DrawPixel(0,0,0xffffffff);
	REF_PTR_RELEASE(surface);

	m_waterNoiseTexture=WW3DAssetManager::Get_Instance()->Get_Texture("Noise0000.tga");
	m_riverAlphaEdge=WW3DAssetManager::Get_Instance()->Get_Texture("TWAlphaEdge.tga");
	m_waterSparklesTexture=WW3DAssetManager::Get_Instance()->Get_Texture("WaterSurfaceBubbles.tga");
#ifdef DRAW_WATER_WAKES
	m_waterTrackSystem = NEW WaterTracksRenderSystem;
	m_waterTrackSystem->init();
#endif

	return 0;
}

void WaterRenderObjClass::updateMapOverrides(void)
{
	if (m_riverTexture && TheWaterTransparency->m_standingWaterTexture.compareNoCase(m_riverTexture->Get_Texture_Name()) != 0)
	{
		REF_PTR_RELEASE(m_riverTexture);
		m_riverTexture = WW3DAssetManager::Get_Instance()->Get_Texture(TheWaterTransparency->m_standingWaterTexture.str());
	}
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void WaterRenderObjClass::reset( void )
{
#if ZH_MODERN_WATER
	m_modernObjectsForDraw.clear();m_modernObstacleObjectIDs.clear();m_waterPolygonsForDraw.clear();
	m_modernReadsReady=FALSE;
#endif
	invalidateModernWaterShore();
	clearModernWaterFoam();
	clearModernWaterHulls();
	// TerrainVisual reuses this object across map lifetimes. Release per-level optical resources
	// even if the next map happens to reuse the same terrain allocation and polygon coordinates.
	clearModernWaterOpticalTextures();
	m_modernOpticalPolygons.clear();
	m_modernOpticalPolygonsValid=FALSE;
	m_modernHeightMap=NULL; m_modernHeightData=NULL;

	// the vertex animated water mesh is zeroed by TerrainLogic::reset, which owns it now

	if (m_waterTrackSystem)
		m_waterTrackSystem->reset();
}

// ------------------------------------------------------------------------------------------------
/** TerrainLogic::enableWaterGrid has enabled or disabled its WaterGrid, and W3DTerrainVisual::load
	* calls this with FALSE for a new map.  The grid's flag, mesh and m_disableRiver are the grid's;
	* what is left is the river flag and the buffers, rebuilt when the grid has a new allocation - the
	* old "state && m_meshData == NULL", and the reallocation setGridResolution made through here. */
// ------------------------------------------------------------------------------------------------
void WaterRenderObjClass::enableWaterGrid(Bool state)
{
	m_drawingRiver = false;

	WaterGrid *grid = TheTerrainLogic->getWaterGrid();
	if (grid->getMeshGeneration() != m_meshGeneration)
	{	//water type has changed, must allocate necessary assets for new water.
		m_meshGeneration = grid->getMeshGeneration();

		// the grid was zeroed as it was allocated; reset() did that and this
		if (m_waterTrackSystem)
			m_waterTrackSystem->reset();

		//Release existing grid data
		SAFE_RELEASE(m_vertexBufferD3D);
		SAFE_RELEASE(m_indexBufferD3D);
		releaseWaterTwins();

		//Create new grid data
		if (Render_Failed(generateIndexBuffer(grid->getCellsX()+1,grid->getCellsY()+1)))
			return;
		if (Render_Failed(generateVertexBuffer(grid->getCellsX()+1,grid->getCellsY()+1,sizeof(MaterMeshVertexFormat),false)))
			return;
	}
}

// ------------------------------------------------------------------------------------------------
/** The grid's transform was this object's own: the grid setters called Set_Transform.  They are
	* TerrainLogic's now, so the drawing takes it from there before it draws. */
// ------------------------------------------------------------------------------------------------
void WaterRenderObjClass::syncGridTransform(void)
{
	if (memcmp(&m_drawnTransform, &Transform, sizeof(Matrix3D)) != 0)
		Set_Transform(m_drawnTransform);
}

// ------------------------------------------------------------------------------------------------
/** LOGIC-THREAD.md phase 3: the grid as the draw will read it, taken from TerrainLogic at the sync
	* point (update(), from the client's pass ahead of the draw), so the tick that steps the grid beside
	* the draw writes nothing the draw reads.  A mesh only while it moves, as only then is it drawn. */
// ------------------------------------------------------------------------------------------------
void WaterRenderObjClass::takeGridForDraw(void)
{
	WaterGrid *grid = TheTerrainLogic->getWaterGrid();
	grid->getTransform(&m_drawnTransform);
	m_drawnMotionEnabled = grid->isMotionEnabled();
	m_drawnPushed = grid->wasPushed();
	m_drawnCellsX = grid->getCellsX();
	m_drawnCellsY = grid->getCellsY();
	m_drawnCellSize = grid->getCellSize();
	if (m_drawnMotionEnabled && grid->getMeshData())
	{
		const WaterMeshData *mesh = grid->getMeshData();
		m_drawnMesh.assign(mesh, mesh + (m_drawnCellsX+1+2)*(m_drawnCellsY+1+2));
	}
}

// ------------------------------------------------------------------------------------------------
/** Update phase for water if we need it.  This called once per client frame reguardless
	* of how fast the logic framerate is running */
// ------------------------------------------------------------------------------------------------
void WaterRenderObjClass::update( void )
{
	// the client's pass, ahead of the draw and with no tick running: the grid the draw will read
	takeGridForDraw();

	synchronizeWaterStyle();
#if ZH_MODERN_WATER
	takeWaterPolygonsForDraw();
	takeMotionReadsForRender();
#endif
	//
	// The river scroll and the bump flipbook used to advance once per call, which is once per
	// RENDER frame. That was fine when the loop was capped and wrong now that it is not, and
	// putting them on the logic clock instead is only half right: the logic rate is the game
	// speed setting (30 in the shell, 60 in a skirmish), so the water sped up with it. Neither
	// clock is the water's - it is scenery, so it runs off the wall clock.
	//
	// The per-second figures below are EA's per-frame steps times the 30 fps they were authored
	// against, so a speed of 1.0f reproduces the shipped tempo exactly. It reads as a boil rather
	// than a swell at that rate, most visibly on the shell map behind the main menu.
	//
	// Two rates, not one. The scroll and the noise wobble are the swell and are continuous, so
	// scaling them just slows the motion. The bump texture is a 32-frame flipbook and is the fine
	// ripple, and a flipbook has no in-between state: the only way to slow it is to hold each
	// frame longer, and at 0.2 that is six frames a second - the eye reads that as a stutter, not
	// as slow water. So the flipbook keeps its authored rate and only the scroll is scaled.
	//
	const Real BUMP_FRAMES_PER_SEC = AUTHORED_FPS;

	{
		//
		// timeGetTime ticks at 1ms even under timeBeginPeriod(1), and an uncapped shell runs
		// several hundred frames a second, so the per-frame delta alternated between 0 and 1ms
		// and the surface advanced in visible lurches. The performance counter is sub-microsecond.
		//
		static long long perfFreq = 0;
		static long long lastCount = 0;

		if (perfFreq == 0)
			perfFreq = Clock_Ticks_Per_Second();
		const long long nowCount = Clock_Ticks();
		if (lastCount == 0)
			lastCount = nowCount;

		Real elapsedSec = (perfFreq != 0)
			? (Real)((double)(nowCount - lastCount) / (double)perfFreq) : 0.0f;
		lastCount = nowCount;
		if (elapsedSec > 0.1f)
			elapsedSec = 0.1f;		// a level load must not jump the surface forward

		// a pause or a script's time freeze stops every unit and particle; the water stops with them
#if ZH_MODERN_WATER
		const Bool waterFrozen=m_modernMotionFrozen;
#else
		const Bool waterFrozen=TheGameLogic->isGamePaused() || TheScriptEngine->isTimeFrozenScript() || TheScriptEngine->isTimeFrozenDebug();
#endif
		if (waterFrozen) elapsedSec = 0.0f;

		m_modernWaveTime = WaterSurfaceModel::AdvanceTime(m_modernWaveTime, elapsedSec, false, false, false);
#if ZH_MODERN_WATER
		// Client updates are independent of water visibility and reflected render recursion.
		// Build every level once, then advance the fixed-world source even when the camera pans away.
		if(m_useModernWater && m_trapezoidWaterPixelShader) {
			updateModernWaterTextures();refreshModernWaterOpticalPolygons();updateModernWaterShore();
		}
#endif
		const Real step = elapsedSec * AUTHORED_FPS * (TheGlobalData->m_classicGraphics ? 1.0f : WATER_SCROLL_SPEED);

		m_riverVOrigin += 0.002f * step;
		m_riverXOffset += (Real)(0.0125*33/5000) * step;
		m_riverYOffset += (Real)(2*0.0125*33/5000) * step;
		if (m_riverXOffset > 1) m_riverXOffset -= 1;
		if (m_riverYOffset > 1) m_riverYOffset -= 1;
		if (m_riverXOffset < -1) m_riverXOffset += 1;
		if (m_riverYOffset < -1) m_riverYOffset += 1;

		// the flipbook runs at its authored rate - see the two-rates note above
		m_bumpFrameAccum += elapsedSec * BUMP_FRAMES_PER_SEC;
		while (m_bumpFrameAccum >= 1.0f)
		{
			m_bumpFrameAccum -= 1.0f;
			m_iBumpFrame++;
			if (m_iBumpFrame >= NUM_BUMP_FRAMES) {
				m_iBumpFrame = 0;
			}
		}
	}

	/* The mesh motion that was here - the part of the water the simulation reads - is stepped by the
	   logic now, once per logic frame: TerrainLogic's WaterGrid::update, from GameLogic::update (T1c,
	   defect 17; the grid moved to TerrainLogic in M7). */

}  // end update

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
void WaterRenderObjClass::replaceSkyboxTexture(const AsciiString& oldTexName, const AsciiString& newTextName)
{
	W3DAssetManager* assetManager = ((W3DAssetManager*)W3DAssetManager::Get_Instance());

	assetManager->replacePrototypeTexture(m_skyBox, oldTexName.str(), newTextName.str());

	//Enable clamping on all textures used by the skybox (to reduce corner seams).
	if (m_skyBox && m_skyBox->Class_ID() == RenderObjClass::CLASSID_MESH)
	{
		MeshClass *mesh=(MeshClass*) m_skyBox;
		MaterialInfoClass	*material = mesh->Get_Material_Info();

		for (Int i=0; i<material->Texture_Count(); i++)
		{
			if (material->Peek_Texture(i))
			{		
				material->Peek_Texture(i)->Get_Filter().Set_U_Addr_Mode(TextureFilterClass::TEXTURE_ADDRESS_CLAMP);
				material->Peek_Texture(i)->Get_Filter().Set_V_Addr_Mode(TextureFilterClass::TEXTURE_ADDRESS_CLAMP);
			}
		}
	}

}

//-------------------------------------------------------------------------------------------------
/** Adjusts various water/sky rendering settings that depend on time of day. */
//-------------------------------------------------------------------------------------------------
void WaterRenderObjClass::setTimeOfDay(TimeOfDay tod)
{
	m_tod=tod;
	if (m_waterType == WATER_TYPE_2_PVSHADER)
		generateVertexBuffer(PATCH_SIZE,PATCH_SIZE,sizeof(SEA_PATCH_VERTEX),true);	//update the water mesh with new lighting/alpha
}

//-------------------------------------------------------------------------------------------------
/**Copies GDF settings dealing with a particular time of day into our own
	* structures.  Also allocates any required W3D assets (textures). */
//-------------------------------------------------------------------------------------------------
void WaterRenderObjClass::loadSetting( Setting *setting, TimeOfDay timeOfDay )
{
	SurfaceClass::SurfaceDescription surfaceDesc;

	// sanity
	DEBUG_ASSERTCRASH( setting, ("WaterRenderObjClass::loadSetting, NULL setting\n") );

	// textures
	setting->skyTexture = WW3DAssetManager::Get_Instance()->Get_Texture( WaterSettings[ timeOfDay ].m_skyTextureFile.str() );
	setting->waterTexture = WW3DAssetManager::Get_Instance()->Get_Texture( WaterSettings[ timeOfDay ].m_waterTextureFile.str() );

	// texelss per unit
	setting->skyTexelsPerUnit = WaterSettings[ timeOfDay ].m_skyTexelsPerUnit;
	setting->waterTexture->Get_Level_Description( surfaceDesc, 0 );
	setting->skyTexelsPerUnit /= (Real)surfaceDesc.Width;

	// water repeat
	setting->waterRepeatCount = WaterSettings[ timeOfDay ].m_waterRepeatCount;

	// U and V scroll per ms
	setting->uScrollPerMs = WaterSettings[ timeOfDay ].m_uScrollPerMs;
	setting->vScrollPerMs = WaterSettings[ timeOfDay ].m_vScrollPerMs;

	//
	// vertex colors
	//
	// bottom left
	setting->vertex00Diffuse = (WaterSettings[ timeOfDay ].m_vertex00Diffuse.red << 16) |
														 (WaterSettings[ timeOfDay ].m_vertex00Diffuse.green << 8) |
														  WaterSettings[ timeOfDay ].m_vertex00Diffuse.blue;
	// top left
	setting->vertex01Diffuse = (WaterSettings[ timeOfDay ].m_vertex01Diffuse.red << 16) |
														 (WaterSettings[ timeOfDay ].m_vertex01Diffuse.green << 8) |
														  WaterSettings[ timeOfDay ].m_vertex01Diffuse.blue;
	// bottom right
	setting->vertex10Diffuse = (WaterSettings[ timeOfDay ].m_vertex10Diffuse.red << 16) |
														 (WaterSettings[ timeOfDay ].m_vertex10Diffuse.green << 8) |
														  WaterSettings[ timeOfDay ].m_vertex10Diffuse.blue;
	// top right
	setting->vertex11Diffuse = (WaterSettings[ timeOfDay ].m_vertex11Diffuse.red << 16) |
														 (WaterSettings[ timeOfDay ].m_vertex11Diffuse.green << 8) |
														  WaterSettings[ timeOfDay ].m_vertex11Diffuse.blue;

	// diffuse water color
	setting->waterDiffuse = (WaterSettings[ timeOfDay ].m_waterDiffuseColor.alpha << 24) |
												  (WaterSettings[ timeOfDay ].m_waterDiffuseColor.red		<< 16) |
													(WaterSettings[ timeOfDay ].m_waterDiffuseColor.green << 8) |
												   WaterSettings[ timeOfDay ].m_waterDiffuseColor.blue;

	// transparent water color
	setting->transparentWaterDiffuse = (WaterSettings[ timeOfDay ].m_transparentWaterDiffuse.alpha << 24) |
																		 (WaterSettings[ timeOfDay ].m_transparentWaterDiffuse.red	 << 16) |
																		 (WaterSettings[ timeOfDay ].m_transparentWaterDiffuse.green << 8) |
																		  WaterSettings[ timeOfDay ].m_transparentWaterDiffuse.blue;

}

//-------------------------------------------------------------------------------------------------
/** Our water may use effects that require run-time rendered textures.  These
	*	textures need to be updated before we start rendering to the main screen
	* render target because D3D doesn't multiple render targets. */
//-------------------------------------------------------------------------------------------------
void WaterRenderObjClass::updateRenderTargetTextures(CameraClass *cam)
{
	// This render boundary also runs while the game/options menu is paused.
	synchronizeWaterStyle();
	if (!cam || !TheTerrainRenderObject || !TheTerrainRenderObject->getMap())
		return;

	if (m_waterType == WATER_TYPE_2_PVSHADER && getClippedWaterPlane(cam, NULL))
	{
		m_reflectionAreaMin.Set(-1.0f, -1.0f);
		m_reflectionAreaMax.Set(1.0f, 1.0f);
		renderMirror(cam, m_level);	//generate texture containing reflected scene
	}

	if (m_waterType != WATER_TYPE_0_TRANSLUCENT || !m_pReflectionTexture)
		return;

	// A camera that has not moved looks at the same water from the same place, so a reflection one
	// frame old lines up with it exactly and only what moved in it is a frame late.  Such a frame
	// keeps the last texture, and the one after it draws a new one.
	const Bool cameraMoved = memcmp(&cam->Get_Transform(), &m_reflectionCameraTransform, sizeof(Matrix3D)) != 0;
	if (!cameraMoved && !m_reflectionReused)
	{
		m_reflectionReused = TRUE;
		return;
	}
	m_reflectionReused = FALSE;
	m_reflectionCameraTransform = cam->Get_Transform();

	// One mirror a frame, so the first water area in view decides the plane.  A map whose visible
	// water sits at two different heights reflects the second one at the first one's level.  The
	// mirror only covers the part of the screen the water is drawn on: whatever stands outside that
	// part of its frustum can never show in the water, and is culled before it costs a draw.
	Bool waterInView = FALSE;
	Bool waterBehindCamera = FALSE;
	Vector2 areaMin(1.0f, 1.0f);
	Vector2 areaMax(-1.0f, -1.0f);
#if ZH_MODERN_WATER
	for (const WaterPolygonForDraw &polygon:m_waterPolygonsForDraw)
#else
	for (PolygonTrigger *pTrig=PolygonTrigger::getFirstPolygonTrigger(); pTrig; pTrig = pTrig->getNext())
#endif
	{
#if ZH_MODERN_WATER
		const WaterPolygonForDraw *pTrig=&polygon;
#endif
		if (!pTrig->isWaterArea() || pTrig->getNumPoints() < 3)
			continue;

		Coord3D center;
		pTrig->getCenterXY(&center);
		center.z = TheTerrainRenderObject->getDrawHeight(center.x, center.y);
		if (cam->Cull_Sphere(SphereClass(Vector3(center.x, center.y, center.z), pTrig->getRadius())))
			continue;

		if (!waterInView)
			m_reflectionLevel = pTrig->getPoint(0)->z;
		waterInView = TRUE;

		for (Int pointIndex = 0; pointIndex < pTrig->getNumPoints(); ++pointIndex)
		{
			const ICoord3D *point = pTrig->getPoint(pointIndex);
			Vector3 projected;
			if (cam->Project(projected, Vector3(point->x, point->y, point->z)) == CameraClass::OUTSIDE_NEAR_CLIP)
			{
				waterBehindCamera = TRUE;
				continue;
			}
			areaMin.X = min(areaMin.X, projected.X);
			areaMin.Y = min(areaMin.Y, projected.Y);
			areaMax.X = max(areaMax.X, projected.X);
			areaMax.Y = max(areaMax.Y, projected.Y);
		}
	}

	if (!waterInView)
		return;

	if (waterBehindCamera)
	{	//a corner the camera cannot project says nothing about where the rest lands
		areaMin.Set(-1.0f, -1.0f);
		areaMax.Set(1.0f, 1.0f);
	}

	m_reflectionAreaMin.Set(max(areaMin.X - WATER_REFLECTION_AREA_MARGIN, -1.0f), max(areaMin.Y - WATER_REFLECTION_AREA_MARGIN, -1.0f));
	m_reflectionAreaMax.Set(min(areaMax.X + WATER_REFLECTION_AREA_MARGIN, 1.0f), min(areaMax.Y + WATER_REFLECTION_AREA_MARGIN, 1.0f));
	if (m_reflectionAreaMin.X >= m_reflectionAreaMax.X || m_reflectionAreaMin.Y >= m_reflectionAreaMax.Y)
		return;	//every corner is off one side of the screen

	renderMirror(cam, m_reflectionLevel);
}

//-------------------------------------------------------------------------------------------------
/** Renders the reflected scene into an offscreen texture. */
//-------------------------------------------------------------------------------------------------
void WaterRenderObjClass::renderMirror(CameraClass *cam, Real level)
{
#ifdef EXTENDED_STATS
	if (DX8Wrapper::stats.m_disableWater) {
		return;
	}
#endif
	Matrix3D	OldCameraMatrix=cam->Get_Transform();
	Matrix4x4	FullMatrix4(cam->Get_Transform());	//copy 3x4 matrix into a 4x4
	Vector3		WaterNormal(0,0,1);	//normal of plane used for reflection
	Vector4		WaterPlane(WaterNormal.X,WaterNormal.Y,WaterNormal.Z,level);
	Vector3		rRight,rUp,rN,rPos;	//orientation and translation vectors of camera

	Matrix4x4	FullMatrix(FullMatrix4.Transpose());	//swap rows/columns

	//reflect camera right vector
	Real axis_distance=Vector3::Dot_Product((Vector3&)FullMatrix[0],WaterNormal);
	rRight = (Vector3&)FullMatrix[0] - (2.0f*axis_distance*WaterNormal);

	//reflect camera up vector
	axis_distance=Vector3::Dot_Product((Vector3&)FullMatrix[1],WaterNormal);
	rUp = (Vector3&)FullMatrix[1] - (2.0f*axis_distance*WaterNormal);

	//reflect camera n vector
	axis_distance=Vector3::Dot_Product((Vector3&)FullMatrix[2],WaterNormal);
	rN = (Vector3&)FullMatrix[2] - (2.0f*axis_distance*WaterNormal);

	//reflect camera position
	axis_distance=Vector3::Dot_Product((Vector3&)FullMatrix[3],WaterNormal);	//distance cam to origin
	axis_distance -= WaterPlane.W;	// subtract mirror plane distance to get distance camera to plane
	rPos = (Vector3&)FullMatrix[3] - (2.0f*axis_distance*WaterNormal);

	//generate a new camera matrix from reflected vectors
	Matrix3D reflectedTransform(rRight,rUp,rN,rPos);

	DX8Wrapper::Set_Render_Target_With_Z((TextureClass*)m_pReflectionTexture);

	// Clear the backbuffer
	// The sea's cloud plane fills its background; map water has no such plane, so its target is
	// cleared to white, which the overlay multiplies in as no change at all.
#if !ZH_MODERN_WATER
	WW3D::Begin_Render(m_waterType == WATER_TYPE_0_TRANSLUCENT,true,WATER_REFLECTION_SKY_COLOR);
#else
	// Modern water supplies its own sky. Classic multiplies a white-background mirror.
	if (m_useModernWater)
		WW3D::Begin_Render(m_waterType == WATER_TYPE_0_TRANSLUCENT,true,Vector3(0,0,0),0.0f);
	else
		WW3D::Begin_Render(m_waterType == WATER_TYPE_0_TRANSLUCENT,true,WATER_REFLECTION_SKY_COLOR);
#endif

	cam->Set_Transform( reflectedTransform );

	//Force reflected image to be drawn into full texture size - not a viewport inside texture.
	Vector2 vMin,vMax,vOldMax,vOldMin;
 	cam->Get_Viewport(vOldMin,vOldMax);
 	vMax.X=vMax.Y=1.0f;
	vMin.X=vMin.Y=0.0f;
 	cam->Set_Viewport(vMin,vMax);

	// A point on the water plane lands on the same view plane position for both cameras, so the
	// water's screen area is the same slice of the mirror's view plane, stretched over the texture.
	Vector2 planeOldMin, planeOldMax;
	cam->Get_View_Plane(planeOldMin, planeOldMax);
	const Vector2 planeSize = planeOldMax - planeOldMin;
	cam->Set_View_Plane(
		Vector2(planeOldMin.X + planeSize.X * (m_reflectionAreaMin.X + 1.0f) * 0.5f, planeOldMin.Y + planeSize.Y * (m_reflectionAreaMin.Y + 1.0f) * 0.5f),
		Vector2(planeOldMin.X + planeSize.X * (m_reflectionAreaMax.X + 1.0f) * 0.5f, planeOldMin.Y + planeSize.Y * (m_reflectionAreaMax.Y + 1.0f) * 0.5f));

	cam->Apply();	//force an update of all the camera dependent parameters like frustum clip planes

	//flip the winding order of polygons to draw the reflected back sides.
	ShaderClass::Invert_Backface_Culling(true);

	// Render the scene
	if (m_waterType == WATER_TYPE_2_PVSHADER)
	{
		renderSky();
		if (m_tod == TIME_OF_DAY_NIGHT)
			renderSkyBody(&reflectedTransform);
	}

	// The mirrored camera sits under the water, so everything under the water is in front of it.
	PlaneClass waterSurface(WaterNormal, level);
	cam->Set_Oblique_Near_Plane(&waterSurface);
	cam->Apply();

	m_renderingMirror = TRUE;
	WW3D::Render(m_parentScene,cam);
	m_renderingMirror = FALSE;

	cam->Set_Oblique_Near_Plane(NULL);
	cam->Set_Transform(OldCameraMatrix);	//restore original non-reflected matrix
 	cam->Set_Viewport(vOldMin,vOldMax);
	cam->Set_View_Plane(planeOldMin, planeOldMax);

	cam->Apply();	//force an update of all the camera dependent parameters like frustum clip planes

	ShaderClass::Invert_Backface_Culling(false);

	WW3D::End_Render(false);

	// Change the rendertarget back to the main backbuffer
	DX8Wrapper::Set_Render_Target((IDirect3DSurface9 *)NULL);
}

//-------------------------------------------------------------------------------------------------
/** Renders (draws) the water.
	*	Algorithm:
	*	Draw reflected scene.
	*	Draw reflected sky layer(s) and bodies.
	*	Clear Zbuffer
	*	Fill Zbuffer by drawing water surface (allows proper sorting into regular scene).
	*	Draw non-reflected scene (done in regular app render loop).
	*
	*	This algorithm doesn't apply to translucent water, which is rendered into a
	*   texture and rendered at end of scene. */
//-------------------------------------------------------------------------------------------------
DECLARE_PERF_TIMER(Water)
void WaterRenderObjClass::Render(RenderInfoClass & rinfo)
{
	USE_PERF_TIMER(Water)
	if (TheTerrainRenderObject && !TheTerrainRenderObject->getMap())
		return;	//no map has been loaded yet.

	syncGridTransform();	// the grid's place in the world, which every water surface below is drawn at

	if (((RTS3DScene *)rinfo.Camera.Get_User_Data())->getCustomPassMode() == SCENE_PASS_ALPHA_MASK ||
		((SceneClass *)rinfo.Camera.Get_User_Data())->Get_Extra_Pass_Polygon_Mode() == SceneClass::EXTRA_PASS_CLEAR_LINE)
		return;	//water is not drawn in wireframe or custom scene passes

#ifdef EXTENDED_STATS
	if (DX8Wrapper::stats.m_disableWater) {
		return;
	}
#endif
	if (ShaderClass::Is_Backface_Culling_Inverted())
		return;	//the water object will not reflect in itself, so don't do anything if rendering a mirror.

	//this water type needs to rendered after the rest of scene, so buffer it up for later

	// If static sort lists are enabled and this mesh has a sort level, put it on the list instead
	// of rendering it.
	unsigned int sort_level = (unsigned int)Get_Sort_Level();

	if (WW3D::Are_Static_Sort_Lists_Enabled() && sort_level != SORT_LEVEL_NONE) 
	{	
		WW3D::Add_To_Static_Sort_List(this, sort_level);
		return;
	}

	// The foam reads how deep the water is out of the scene's depth, which the ground and every
	// opaque object have written by now: a copy, taken once here, before the water writes any.
	if (waterFoam())
		Direct3D11_Take_Scene_Depth();

#if ZH_MODERN_WATER
	WaterShoreOverlay::Scope shoreScope(DX8Wrapper::_Get_D3D_Device(),
		m_useModernWater && m_waterType==WATER_TYPE_0_TRANSLUCENT && m_trapezoidWaterPixelShader &&
		!TheGlobalData->m_isWorldBuilder && !TheGlobalData->m_usingWaterTrackEditor);
	m_modernFoamDrawFrame=~0u;m_modernFoamDrawObjectIDs.clear();
	unsigned oldModernCoordIndex=0,oldModernTransformFlags=0;
	unsigned oldModernBaseCoordIndex=0,oldModernBaseTransformFlags=0;
	Bool oldModernStagesKnown=FALSE;
	IDirect3DBaseTexture9 *oldModernBed=NULL,*oldModernOptical=NULL,*oldModernFoam=NULL;
	if (m_useModernWater) {
		// The wrapper's own record: under Direct3D 11 the Direct3D 9 device is behind it (Defer_D3D9_State).
		// Stage 14 is past the wrapper's stages and is set on the device at once.
		oldModernStagesKnown=DX8Wrapper::Get_DX8_Texture_Stage_State(0,D3DTSS_TEXCOORDINDEX,oldModernBaseCoordIndex)
			& DX8Wrapper::Get_DX8_Texture_Stage_State(0,D3DTSS_TEXTURETRANSFORMFLAGS,oldModernBaseTransformFlags)
			& DX8Wrapper::Get_DX8_Texture_Stage_State(1,D3DTSS_TEXCOORDINDEX,oldModernCoordIndex)
			& DX8Wrapper::Get_DX8_Texture_Stage_State(1,D3DTSS_TEXTURETRANSFORMFLAGS,oldModernTransformFlags);
		oldModernBed=DX8Wrapper::Get_DX8_Texture(7);
		if (oldModernBed) oldModernBed->AddRef();
		oldModernOptical=DX8Wrapper::Get_DX8_Texture(4);
		if (oldModernOptical) oldModernOptical->AddRef();
		DX8Wrapper::_Get_D3D_Device()->GetTexture(14,&oldModernFoam);
		updateModernWaterTextures();
		refreshModernWaterOpticalPolygons();
		m_modernRenderCamera=&rinfo.Camera;
		m_modernCamera=rinfo.Camera.Get_Position();
		Int modernPatches=0,modernFlatPatches=0,visibleFlatPatches=0;
		Real highestModernWater=m_level;
		Bool hasModernWater=FALSE;
		for (const WaterPolygonForDraw &polygon:m_waterPolygonsForDraw) {
			const WaterPolygonForDraw *trigger=&polygon;
			if (!trigger->isWaterArea() || trigger->getNumPoints()<3) continue;
			modernPatches+=trigger->isRiver() ? max(0,trigger->getNumPoints()/2-1) :
				((trigger->getNumPoints()-1)/2);
			if (!trigger->isRiver()) {
				modernFlatPatches+=(trigger->getNumPoints()-1)/2;
				// This count only controls sample reuse, never topology or density. Conservatively
				// include every visible planar flat patch that could use the projected path.
				for (Int k=1;k<trigger->getNumPoints()-1;k+=2) {
					const Int corner[4]={k+2<trigger->getNumPoints()?k+2:k+1,k+1,k,0};
					Vector3 minimum,maximum;float levels[4];
					for (Int n=0;n<4;++n) {
						const ICoord3D &p=*trigger->getPoint(corner[n]);levels[n]=float(p.z);
						if (n==0) minimum=maximum=Vector3(float(p.x),float(p.y),float(p.z));
						else { minimum.X=min(minimum.X,float(p.x));minimum.Y=min(minimum.Y,float(p.y));minimum.Z=min(minimum.Z,float(p.z));
							maximum.X=max(maximum.X,float(p.x));maximum.Y=max(maximum.Y,float(p.y));maximum.Z=max(maximum.Z,float(p.z)); }
					}
					minimum.Z-=WaterSurfaceModel::MaxDisplacement;maximum.Z+=WaterSurfaceModel::MaxDisplacement;
					minimum.X-=WaterFftGeometry::MaxHorizontalDisplacement;minimum.Y-=WaterFftGeometry::MaxHorizontalDisplacement;
					maximum.X+=WaterFftGeometry::MaxHorizontalDisplacement;maximum.Y+=WaterFftGeometry::MaxHorizontalDisplacement;
					if (WaterProjectedMesh::HasSingleLevel(levels) && fabsf(m_modernCamera.Z-levels[0])>WaterSurfaceModel::MaxDisplacement*2 &&
						!rinfo.Camera.Cull_Box(AABoxClass((minimum+maximum)*0.5f,(maximum-minimum)*0.5f))) ++visibleFlatPatches;
				}
			}
			for (Int point=0;point<trigger->getNumPoints();++point) {
				const Real elevation=trigger->getPoint(point)->z;
				if (!hasModernWater || elevation>highestModernWater) highestModernWater=elevation;
				hasModernWater=TRUE;
			}
		}
		// One map-wide subdivision count keeps all common edges welded. The authored polygon count
		// itself is irreducible; added tessellation stays within the shared model's vertex budget.
		for (size_t i=0;i<m_modernOpticalPolygons.size();++i) if (!m_modernOpticalPolygons[i].points.empty())
			modernWaterOpticalTexture(m_modernOpticalPolygons[i].points[0].z);
		updateModernWaterShore(); // same time is a no-op; also initializes before a first draw without a client update
		// Rivers retain their old camera-height detail. Ocean interiors use a visible projected
		// lattice and constrain boundary samples to these same morphed edge polylines.
		const Int baseCells=WaterSurfaceModel::CellsForPatches(modernPatches);
		const WaterSurfaceModel::MeshLOD modernLOD=WaterSurfaceModel::LODForCameraHeight(
			baseCells,m_modernCamera.Z-highestModernWater);
		m_modernMeshCells=modernLOD.cells;
		m_modernMeshMorph=modernLOD.morph;
		const size_t baseVertices=size_t(baseCells+1)*(baseCells+1);
		const size_t retainedVertices=size_t(max(0,modernPatches-modernFlatPatches))*baseVertices;
		const size_t available=retainedVertices<WaterSurfaceModel::VertexBudget?WaterSurfaceModel::VertexBudget-retainedVertices:0;
		Int targetWidth,targetHeight,targetBits;bool targetWindowed;
		WW3D::Get_Render_Target_Resolution(targetWidth,targetHeight,targetBits,targetWindowed);
		const WaterProjectedMesh::Resolution projected=WaterProjectedMesh::ChooseResolution(targetWidth,targetHeight,modernFlatPatches,available,4u*(baseCells+1));
		m_modernProjectedColumns=projected.columns;m_modernProjectedRows=projected.rows;
		m_modernProjectedWidth=targetWidth;m_modernProjectedHeight=targetHeight;
		m_modernProjectedBudget=min(size_t(60000),available/size_t(max(1,modernFlatPatches)));
		// Per-frame reuse shares lattice vertices across flat patches/levels, while never keeping
		// stale terrain, moving hulls, wave time or camera state from a previous render.
		m_modernProjectedSamples.clear();
		m_modernObstacleIndex.Clear();

		m_modernProjectedCacheEnabled=visibleFlatPatches>1;
		m_modernBowVertices=m_modernBowTriangles=0;
		m_modernBowAdmissions.Begin(m_modernWaveTime);

		updateModernWaterHulls();

		for (size_t i=0;i<m_modernObstacles.size();++i)
			m_modernObstacles[i].hasHull=modernHasHull(m_modernObstacleObjectIDs[i]);

	}
#endif

	switch(m_waterType)
	{
		case WATER_TYPE_0_TRANSLUCENT:
		case WATER_TYPE_3_GRIDMESH:
			//Draw the water surface as a bunch of alpha blended tiles covering areas where water is visible
			renderWater();
			if (!m_drawingRiver || m_drawnPushed) {
				renderWaterMesh();	//Draw water surface as 3D deforming mesh if it's enabled on this map.
			}
			break;

		case WATER_TYPE_2_PVSHADER:
			//Pixel/Vertex Shader based water which uses an off-screen rendered reflection texture
			drawSea(rinfo);	//draw water surface
			break;

		case WATER_TYPE_1_FB_REFLECTION:
			{
				//Normal frame buffer reflection water type. Non translucent.  Legacy code we're not using anymore.
				Matrix3D	OldCameraMatrix=rinfo.Camera.Get_Transform();
				Matrix4x4	FullMatrix4(rinfo.Camera.Get_Transform());	//copy 3x4 matrix into a 4x4
				Vector3		WaterNormal(0,0,1);	//normal of plane used for reflection
				Vector4		WaterPlane(WaterNormal.X,WaterNormal.Y,WaterNormal.Z,m_level);	//assume distance to origin 0
				Vector3		rRight,rUp,rN,rPos;	//orientation and translation vectors of camera

				Matrix4x4	FullMatrix(FullMatrix4.Transpose());	//swap rows/columns

				//reflect camera right vector
				Real axis_distance=Vector3::Dot_Product((Vector3&)FullMatrix[0],WaterNormal);
				rRight = (Vector3&)FullMatrix[0] - (2.0f*axis_distance*WaterNormal);

				//reflect camera up vector
				axis_distance=Vector3::Dot_Product((Vector3&)FullMatrix[1],WaterNormal);
				rUp = (Vector3&)FullMatrix[1] - (2.0f*axis_distance*WaterNormal);

				//reflect camera n vector
				axis_distance=Vector3::Dot_Product((Vector3&)FullMatrix[2],WaterNormal);
				rN = (Vector3&)FullMatrix[2] - (2.0f*axis_distance*WaterNormal);

				//reflect camera position
				axis_distance=Vector3::Dot_Product((Vector3&)FullMatrix[3],WaterNormal);	//distance cam to origin
				axis_distance -= WaterPlane.W;	// subtract mirror plane distance to get distance camera to plane
				rPos = (Vector3&)FullMatrix[3] - (2.0f*axis_distance*WaterNormal);

				//generate a new camera matrix from reflected vectors
				Matrix3D reflectedTransform(rRight,rUp,rN,rPos);

				//flip the winding order of polygons to draw the reflected back sides.
				ShaderClass::Invert_Backface_Culling(true);

			#ifdef CLIP_GEOMETRY_TO_PLANE
			  // Set a clip plane, so that only objects above the water are reflected
				WaterPlane.W *= -1.0f;	//flip sign of plane distance for D3D use.

			//	DX8Wrapper::Set_DX8_Clip_Plane( 0, &WaterPlane.X );
			//	DX8Wrapper::Set_DX8_Render_State(D3DRS_CLIPPLANEENABLE, D3DCLIPPLANE0 );	//turn on first clip plane

				// Alternate Clipping Method using alpha testing hack!
				/**************************************************************************************/

				D3DXMATRIX inv;
				D3DXMATRIX clipMatrix;
				Real det;
				Matrix4x4 curView;

				//get current view matrix
				DX8Wrapper::_Get_DX8_Transform(D3DTS_VIEW, curView);

				//get inverse of view matrix(= view to world matrix)
				D3DXMatrixInverse(&inv, &det, (D3DXMATRIX*)&curView);

				//create clipping matrix by inserting our plane equation into the 1st column
				D3DXMatrixIdentity(&clipMatrix);
				clipMatrix(0,0)=WaterNormal.X;
				clipMatrix(1,0)=WaterNormal.Y;
				clipMatrix(2,0)=WaterNormal.Z;
				clipMatrix(3,0)=WaterPlane.W+0.5f;
				inv *=clipMatrix;

				// Change texture wrapping mode to 'clamp' for texture stage 1
				DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
				DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);

				// Use CameraSpace vertices as input to matrix and use texture wrap mode from stage 1
				DX8Wrapper::Set_DX8_Texture_Stage_State(1, D3DTSS_TEXCOORDINDEX, D3DTSS_TCI_CAMERASPACEPOSITION|1);
				// Two output coordinates are used.
				DX8Wrapper::Set_DX8_Texture_Stage_State(1, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_COUNT2);	

				// Set texture generation matrix for stage 1
				DX8Wrapper::_Set_DX8_Transform(D3DTS_TEXTURE1, *((Matrix4*)&inv));

				// Disable bilinear filtering
				DX8Wrapper::Set_DX8_Texture_Stage_State(1, D3DSAMP_MINFILTER, D3DTEXF_POINT);
				DX8Wrapper::Set_DX8_Texture_Stage_State(1, D3DSAMP_MAGFILTER, D3DTEXF_POINT);

				// Pass stage 0 texture data untouched(by modulating with white)
				DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_COLORARG1, D3DTA_TEXTURE );	//stage 1 texture
				DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_COLORARG2, D3DTA_CURRENT );	//previous stage texture
				DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_COLOROP,   D3DTOP_MODULATE );	//module with white => does nothing

				DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_ALPHAARG1, D3DTA_TEXTURE );	//stage 1 texture
				DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_ALPHAARG2, D3DTA_CURRENT );	//previous stage texture
				DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_ALPHAOP,   D3DTOP_MODULATE );	//modulate with clipping texture

				DX8Wrapper::Set_DX8_Render_State(D3DRS_ALPHAREF,0x00);
				DX8Wrapper::Set_DX8_Render_State(D3DRS_ALPHAFUNC,D3DCMP_NOTEQUAL);	//pass pixels who's alpha is not zero
				DX8Wrapper::Set_DX8_Render_State(D3DRS_ALPHATESTENABLE, true);	//test pixels if transparent(clipped) before rendering.

				// Set clipping texture
				m_alphaClippingTexture->Set_U_Addr_Mode(TextureClass::TEXTURE_ADDRESS_CLAMP);
				m_alphaClippingTexture->Set_V_Addr_Mode(TextureClass::TEXTURE_ADDRESS_CLAMP);
				m_alphaClippingTexture->Set_Min_Filter(TextureClass::FILTER_TYPE_NONE);
				m_alphaClippingTexture->Set_Mag_Filter(TextureClass::FILTER_TYPE_NONE);
				m_alphaClippingTexture->Set_Mip_Mapping(TextureClass::FILTER_TYPE_NONE);

				DX8Wrapper::Set_Texture(0,m_alphaClippingTexture);

				//TODO: Will have to make sure that the shader system is not resetting my stage 1 setup
				//while rendering the scene

				/*************************************************************************************/
			#endif

			#ifdef CLIP_GEOMETRY_TO_PLANE
				//restore default culling mode
			//	DX8Wrapper::Set_DX8_Render_State(D3DRS_CLIPPLANEENABLE, 0 );	//turn off first clip plane

				//disable texture coordinate generation
				DX8Wrapper::Set_DX8_Texture_Stage_State(1, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
				DX8Wrapper::Set_DX8_Render_State(D3DRS_ALPHATESTENABLE, false);	//disable alpha testing
			#endif

				ShaderClass::Invert_Backface_Culling(false);	//return culling back to normal

				ShaderClass::Invalidate();	//reset shading system so it forces full state set.

				renderWater();
			}	//WATER_TYPE_1

		default:
			break;
	}//switch
#if ZH_MODERN_WATER
	if (m_useModernWater) {
		if (oldModernStagesKnown) {
			DX8Wrapper::Set_DX8_Texture_Stage_State(0,D3DTSS_TEXCOORDINDEX,oldModernBaseCoordIndex);
			DX8Wrapper::Set_DX8_Texture_Stage_State(0,D3DTSS_TEXTURETRANSFORMFLAGS,oldModernBaseTransformFlags);
			DX8Wrapper::Set_DX8_Texture_Stage_State(1,D3DTSS_TEXCOORDINDEX,oldModernCoordIndex);
			DX8Wrapper::Set_DX8_Texture_Stage_State(1,D3DTSS_TEXTURETRANSFORMFLAGS,oldModernTransformFlags);
		}
		DX8Wrapper::Set_DX8_Texture(7,oldModernBed);
		if (oldModernBed) oldModernBed->Release();
		DX8Wrapper::Set_DX8_Texture(4,oldModernOptical);
		if (oldModernOptical) oldModernOptical->Release();
		DX8Wrapper::Set_DX8_Texture(14,oldModernFoam);
		if(oldModernFoam) oldModernFoam->Release();
		m_modernBowAdmissions.End();

		m_modernRenderCamera=NULL;
	}
#endif

	if (TheGlobalData && TheGlobalData->m_drawSkyBox)
	{	//center skybox around camera
		Vector3 pos=rinfo.Camera.Get_Position();
		pos.Z = TheGlobalData->m_skyBoxPositionZ;
		m_skyBox->Set_Position(pos);
		m_skyBox->Render(rinfo);
	}

	//Clean up after any pixel shaders.
	//Force render state apply so that the "NULL" texture gets applied to D3D, thus releasing shroud reference count.
	DX8Wrapper::Apply_Render_State_Changes();
	DX8Wrapper::Invalidate_Cached_Render_States();

	if (m_waterTrackSystem)
		m_waterTrackSystem->flush(rinfo);
#if ZH_MODERN_WATER

#endif

//	renderWaterMesh();
//	renderWaterWave();
}

//-------------------------------------------------------------------------------------------------
/** Clips the water plane to the current camera frustum and returns a bounding
	* box enclosing the clipped plane.  Returns false if water plane is not visible. */
//-------------------------------------------------------------------------------------------------
Bool WaterRenderObjClass::getClippedWaterPlane(CameraClass *cam, AABoxClass *box)
{
	const FrustumClass & frustum = cam->Get_Frustum();

	ClipPolyClass	ClippedPoly0;
	ClipPolyClass	ClippedPoly1;

	///@todo: generate proper sized polygon
	ClippedPoly0.Reset();
	ClippedPoly0.Add_Vertex(Vector3(0,0,m_level));
	ClippedPoly0.Add_Vertex(Vector3(0,m_dy,m_level));
	ClippedPoly0.Add_Vertex(Vector3(m_dx,m_dy,m_level));
	ClippedPoly0.Add_Vertex(Vector3(m_dx,0,m_level));

	//clip against all 6 frustum planes
	ClippedPoly0.Clip(frustum.Planes[0],ClippedPoly1);
	ClippedPoly1.Clip(frustum.Planes[1],ClippedPoly0);
	ClippedPoly0.Clip(frustum.Planes[2],ClippedPoly1);
	ClippedPoly1.Clip(frustum.Planes[3],ClippedPoly0);
	ClippedPoly0.Clip(frustum.Planes[4],ClippedPoly1);
	ClippedPoly1.Clip(frustum.Planes[5],ClippedPoly0);

	Int final_vcount = ClippedPoly0.Verts.Count();

	//make sure the polygon is visible
	if (final_vcount >= 3)
	{	
		//find axis aligned bounding box around visible polygon
		if (box)
  			box->Init(&(ClippedPoly0.Verts[0]),final_vcount);
		return TRUE;
	}

	return FALSE;	//water plane is not visible
}

//-------------------------------------------------------------------------------------------------
/** Draws the water surface using a custom D3D vertex/pixel shader and a
	* reflection texture.  Only tested to work on GeForce3. */
//-------------------------------------------------------------------------------------------------
void WaterRenderObjClass::drawSea(RenderInfoClass & rinfo)
{
	AABoxClass	seaBox;

	if (!getClippedWaterPlane(&rinfo.Camera,&seaBox))
		return;	//the sea is not visible

	D3DXMATRIX matProj, matView, matWW3D;

	//create a transform which will flip the y and z coordinates to fit our system
	memset(&matWW3D,0,sizeof(D3DMATRIX));
	matWW3D._11=1.0f;
	matWW3D._32=1.0f;
	matWW3D._23=1.0f;
	matWW3D._44=1.0f;

	Matrix3D tm(Transform);

	DX8Wrapper::Set_Transform(D3DTS_WORLD,tm);	//position the water surface
	DX8Wrapper::Set_Texture(0,NULL);	//we'll be setting our own textures, so reset W3D
	DX8Wrapper::Set_Texture(1,NULL);	//we'll be setting our own textures, so reset W3D

	DX8Wrapper::Apply_Render_State_Changes();	//force update of view and projection matrices

	Vector3 camTran;

	rinfo.Camera.Get_Transform().Get_Translation(&camTran);

	DX8Wrapper::_Get_DX8_Transform(D3DTS_VIEW, *(Matrix4x4*)&matView);
	DX8Wrapper::_Get_DX8_Transform(D3DTS_PROJECTION, *(Matrix4x4*)&matProj);

	//default setup from Kenny's demo
	DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_COLORARG1, D3DTA_TEXTURE );
	DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_COLORARG2, D3DTA_DIFFUSE );
	DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_COLOROP,   D3DTOP_MODULATE);
	DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_ALPHAOP,   D3DTOP_DISABLE );
	DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_TEXCOORDINDEX, 0 );

	DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_COLORARG1, D3DTA_TEXTURE );
	DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_COLORARG2, D3DTA_CURRENT );
	DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_COLOROP,   D3DTOP_MODULATE);
	DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_ALPHAOP,   D3DTOP_DISABLE );
	DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_TEXCOORDINDEX, 1 );

	DX8Wrapper::Set_DX8_Texture_Stage_State( 2, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
	DX8Wrapper::Set_DX8_Texture_Stage_State( 2, D3DTSS_TEXCOORDINDEX, D3DTSS_TCI_PASSTHRU|2);

	DX8Wrapper::Set_DX8_Texture_Stage_State( 3, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
	DX8Wrapper::Set_DX8_Texture_Stage_State( 3, D3DTSS_TEXCOORDINDEX, D3DTSS_TCI_PASSTHRU|3);

//	DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR );
//	DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR );
//	DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DSAMP_MIPFILTER, D3DTEXF_POINT );

//	DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DSAMP_MINFILTER, D3DTEXF_POINT );
//	DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DSAMP_MAGFILTER, D3DTEXF_POINT );
//	DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DSAMP_MIPFILTER, D3DTEXF_NONE );
	//end of default setup

	DX8Wrapper::Set_DX8_Texture_Stage_State(0, D3DSAMP_ADDRESSU, D3DTADDRESS_WRAP);
	DX8Wrapper::Set_DX8_Texture_Stage_State(0, D3DSAMP_ADDRESSV, D3DTADDRESS_WRAP);
	DX8Wrapper::Set_DX8_Render_State( D3DRS_WRAP0, D3DWRAP_U | D3DWRAP_V);

	DX8Wrapper::Set_DX8_Texture_Stage_State(1, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
	DX8Wrapper::Set_DX8_Texture_Stage_State(1, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);

	DX8Wrapper::Set_DX8_Texture( 0, m_pBumpTexture[m_iBumpFrame]);
#ifdef MIPMAP_BUMP_TEXTURE
	DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DSAMP_MIPFILTER, D3DTEXF_POINT );
	DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR );
	DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR );
#endif
	DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_BUMPENVMAT00, F2DW(m_fBumpScale) );
	DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_BUMPENVMAT01, F2DW(0.0f) );
	DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_BUMPENVMAT10, F2DW(0.0f) );
	DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_BUMPENVMAT11, F2DW(m_fBumpScale) );
	DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_BUMPENVLSCALE, F2DW(1.0f) );
	DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_BUMPENVLOFFSET, F2DW(0.0f) );

	DX8Wrapper::Set_DX8_Texture_Stage_State( 2, D3DTSS_COLOROP,   D3DTOP_DISABLE );
	DX8Wrapper::Set_DX8_Texture_Stage_State( 2, D3DTSS_ALPHAOP,   D3DTOP_DISABLE );

	DX8Wrapper::Set_DX8_Render_State(D3DRS_ZWRITEENABLE , FALSE);

	D3DXMATRIX mat;
	memset(&mat,0,sizeof(D3DXMATRIX));

	mat._11 = 0.5f; mat._12 = -0.5f; mat._13 = 0.5f;   mat._14=0.5f;
	mat._21 = 0.5f; mat._22 = 0.5f; mat._23 = 0.0f;   mat._24=0.0f;
	mat._31 = 0.0f; mat._32 = 0.0f; mat._33 = 0.0f;   mat._34=1.0f;
	mat._41 = 0.0f; mat._42 = 0.0f; mat._43 = 0.0f;   mat._44=1.0f;

	m_pDev->SetVertexShaderConstantF(CV_TEXPROJ_0, (const float*)&mat, 4);

	// Setup constants
	m_pDev->SetVertexShaderConstantF(CV_ZERO,   D3DXVECTOR4(0.0f, 0.0f, 0.0f, 0.0f), 1);
	m_pDev->SetVertexShaderConstantF(CV_ONE,    D3DXVECTOR4(1.0f, 1.0f, 1.0f, 1.0f), 1);

	// The declaration D3D8 carried beside the shader has to be bound separately in D3D9, or the
	// shader's dcl_ inputs read zero out of a stream the current FVF describes differently.
	m_pDev->SetVertexDeclaration(m_waveVertexDeclaration);
	m_pDev->SetVertexShader(m_dwWaveVertexShader);
	DX8Wrapper::Set_Pixel_Shader(m_dwWavePixelShader);

//	Make reflection brighter to compensate for darker coloring on sea floor
//	DX8Wrapper::Set_DX8_Render_State( D3DRS_SRCBLEND, D3DBLEND_ONE );
//	DX8Wrapper::Set_DX8_Render_State( D3DRS_DESTBLEND, D3DBLEND_SRCCOLOR );

	DX8Wrapper::Set_DX8_Render_State( D3DRS_SRCBLEND, D3DBLEND_SRCALPHA );
	DX8Wrapper::Set_DX8_Render_State( D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA );

	DX8Wrapper::Set_DX8_Render_State(D3DRS_ALPHABLENDENABLE , TRUE);
	DX8Wrapper::Set_DX8_Texture( 1, m_pReflectionTexture->Peek_D3D_Texture());

//	DX8Wrapper::Set_DX8_Render_State(D3DRS_FILLMODE,D3DFILL_WIREFRAME);//LORENZEN

	Int patchX,patchY,startX,startY;

	D3DXMATRIX patchMatrix;
	memset(&patchMatrix,0,sizeof(D3DXMATRIX));
	patchMatrix._11=PATCH_SCALE;
	patchMatrix._22=1.0f;
	patchMatrix._33=PATCH_SCALE;
	patchMatrix._44=1.0f;

	m_pDev->SetStreamSource(0,m_vertexBufferD3D,0,sizeof(WaterRenderObjClass::SEA_PATCH_VERTEX));
	m_pDev->SetIndices(m_indexBufferD3D);

	for (startY=patchY=(seaBox.Center.Y-seaBox.Extent.Y)/(PATCH_WIDTH*PATCH_SCALE); (patchY*PATCH_WIDTH*PATCH_SCALE)<(seaBox.Center.Y+seaBox.Extent.Y); patchY++)
	{
		for (startX=patchX=(seaBox.Center.X-seaBox.Extent.X)/(PATCH_WIDTH*PATCH_SCALE); (patchX*PATCH_WIDTH*PATCH_SCALE)<(seaBox.Center.X+seaBox.Extent.X); patchX++)
		{
			D3DXMATRIX matWorldViewProj, matTemp, matTempWorld;
			patchMatrix._41=(float)(patchX*PATCH_WIDTH*PATCH_SCALE );
			patchMatrix._43=(float)(patchY*PATCH_WIDTH*PATCH_SCALE );
			//convert the default D3D coordinate system into ours
			D3DXMatrixMultiply(&matTempWorld, &patchMatrix, &matWW3D);

			D3DXMatrixMultiply(&matTemp, &matTempWorld, &matView);
			D3DXMatrixMultiply(&matWorldViewProj, &matTemp, &matProj);
			//matrices must be transposed before loading into vertex shader registers
			D3DXMatrixTranspose(&matWorldViewProj, &matWorldViewProj);
			m_pDev->SetVertexShaderConstantF(CV_WORLDVIEWPROJ_0, (const float*)&matWorldViewProj, 4);	//pass transform matrix into shader

			// Nobody sees a Direct3D 9 draw while Direct3D 11 presents; renderWaterMesh skips its own
			// the same way, and this patch has no Direct3D 11 draw beside it.
			if (!Direct3D11_Present_Is_Enabled())
				m_pDev->DrawIndexedPrimitive(D3DPT_TRIANGLESTRIP,0,0,m_numVertices,0,m_numIndices);
		}
	}
//	DX8Wrapper::Set_DX8_Render_State(D3DRS_FILLMODE,D3DFILL_SOLID);
	DX8Wrapper::Set_DX8_Render_State(D3DRS_ALPHABLENDENABLE , FALSE);
	DX8Wrapper::Set_DX8_Texture( 0, NULL);	//release reference to bump texture
	DX8Wrapper::Set_DX8_Texture( 1, NULL);	//release reference to reflection texture
	DX8Wrapper::Set_DX8_Texture( 2, NULL);	//release reference to reflection texture

	DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
	DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_TEXCOORDINDEX, D3DTSS_TCI_PASSTHRU|0);
	DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
	DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_TEXCOORDINDEX, D3DTSS_TCI_PASSTHRU|1);
	DX8Wrapper::Set_DX8_Render_State(D3DRS_ZWRITEENABLE , TRUE);

	DX8Wrapper::Set_DX8_Texture_Stage_State(1, D3DSAMP_ADDRESSU, D3DTADDRESS_WRAP);
	DX8Wrapper::Set_DX8_Texture_Stage_State(1, D3DSAMP_ADDRESSV, D3DTADDRESS_WRAP);

	DX8Wrapper::Set_DX8_Render_State( D3DRS_WRAP0, 0);	//turn off texture wrapping

	DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_COLOROP,   D3DTOP_DISABLE );
	DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_ALPHAOP,   D3DTOP_DISABLE );
	DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_COLOROP,   D3DTOP_DISABLE );
	DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_ALPHAOP,   D3DTOP_DISABLE );
	DX8Wrapper::Set_DX8_Texture_Stage_State( 2, D3DTSS_COLOROP,   D3DTOP_DISABLE );
	DX8Wrapper::Set_DX8_Texture_Stage_State( 2, D3DTSS_ALPHAOP,   D3DTOP_DISABLE );

	//Restore old transforms
	DX8Wrapper::_Set_DX8_Transform(D3DTS_VIEW, *(Matrix4x4*)&matView);
	DX8Wrapper::_Set_DX8_Transform(D3DTS_PROJECTION, *(Matrix4x4*)&matProj);

	DX8Wrapper::Set_Pixel_Shader(0);	//turn off pixel shader
	m_pDev->SetVertexShader(NULL);	//turn off custom vertex shader
	m_pDev->SetFVF(DX8_FVF_XYZDUV1);

	DX8Wrapper::Invalidate_Cached_Render_States();

	if (TheTerrainRenderObject->getShroud())
	{
		//do second pass to apply the shroud on water plane
		W3DShaderManager::setTexture(0,TheTerrainRenderObject->getShroud()->getShroudTexture());
		W3DShaderManager::setShader(W3DShaderManager::ST_SHROUD_TEXTURE, 0);
		m_pDev->SetStreamSource(0,m_vertexBufferD3D,0,sizeof(WaterRenderObjClass::SEA_PATCH_VERTEX));
		m_pDev->SetIndices(m_indexBufferD3D);
		for (startY=patchY=(seaBox.Center.Y-seaBox.Extent.Y)/(PATCH_WIDTH*PATCH_SCALE); (patchY*PATCH_WIDTH*PATCH_SCALE)<(seaBox.Center.Y+seaBox.Extent.Y); patchY++)
		{
			for (startX=patchX=(seaBox.Center.X-seaBox.Extent.X)/(PATCH_WIDTH*PATCH_SCALE); (patchX*PATCH_WIDTH*PATCH_SCALE)<(seaBox.Center.X+seaBox.Extent.X); patchX++)
			{
				D3DXMATRIX matTemp;
				patchMatrix._41=(float)(patchX*PATCH_WIDTH*PATCH_SCALE);
				patchMatrix._43=(float)(patchY*PATCH_WIDTH*PATCH_SCALE);

				D3DXMatrixMultiply(&matTemp, &patchMatrix, &matWW3D);

				DX8Wrapper::_Set_DX8_Transform(D3DTS_WORLD, *(Matrix4x4*)&matTemp);

				if (!Direct3D11_Present_Is_Enabled())
					m_pDev->DrawIndexedPrimitive(D3DPT_TRIANGLESTRIP,0,0,m_numVertices,0,m_numIndices);
			}
		}
		W3DShaderManager::resetShader(W3DShaderManager::ST_SHROUD_TEXTURE);
	}

}

#define FEATHER_LAYER_COUNT (5.0f)
#define FEATHER_THICKNESS   (4.0f)

//-------------------------------------------------------------------------------------------------
/** Renders (draws) the water surface.*/
//-------------------------------------------------------------------------------------------------
void WaterRenderObjClass::renderWater(void)
{
#if ZH_MODERN_WATER
	for (const WaterPolygonForDraw &polygon:m_waterPolygonsForDraw)
#else
	for (PolygonTrigger *pTrig=PolygonTrigger::getFirstPolygonTrigger(); pTrig; pTrig = pTrig->getNext())
#endif
	{
#if ZH_MODERN_WATER
		const WaterPolygonForDraw *pTrig=&polygon;
#endif
		if (pTrig->isWaterArea()) {
			if (pTrig->getNumPoints()>2) {
				if (pTrig->isRiver()) {
					drawRiverWater(pTrig);
					continue;
				} 
				Int k;
				for (k=1; k<pTrig->getNumPoints()-1; k=k+2) {
					ICoord3D pt3 = *pTrig->getPoint(0);
					ICoord3D pt2 = *pTrig->getPoint(k);
					ICoord3D pt1 = *pTrig->getPoint(k+1);
					ICoord3D pt0 = *pTrig->getPoint(k+1);
					if (k+2<pTrig->getNumPoints()) {
						pt0 = *pTrig->getPoint(k+2);
					}
					Vector3 points[4];
					points[0].Set(pt0.x, pt0.y, pt0.z);
					points[1].Set(pt1.x, pt1.y, pt1.z);
					points[2].Set(pt2.x, pt2.y, pt2.z);
					points[3].Set(pt3.x, pt3.y, pt3.z);

#if ZH_MODERN_WATER
					// Modern depth intersections replace legacy tilted feather copies. Render exactly
					// one surface at the authored elevation; duplicate layers could expose patch seams.
					if (m_useModernWater && m_trapezoidWaterPixelShader) {
						m_modernBowPatchOwner=pTrig->owner;m_modernBowPatchPart=unsigned(k);
						drawTrapezoidWater(points); continue;
					}
#endif
					if ( TheGlobalData->m_featherWater )
					{
						for (int r = 0; r < TheGlobalData->m_featherWater; ++r)
						{
							drawTrapezoidWater(points);
							points[0].Z += (FEATHER_THICKNESS/TheGlobalData->m_featherWater);
						}
					}

					else
						drawTrapezoidWater(points);

				}
			}
		}
	}

}

//-------------------------------------------------------------------------------------------------
/** Renders (draws) the sky plane.  Will apply current time-of-day settings including
	* some simple UV scrolling animation. */
//-------------------------------------------------------------------------------------------------
void WaterRenderObjClass::renderSky(void)
{
	Int timeNow,timeDiff;
	Real fu,fv;

	Setting *setting=&m_settings[m_tod];

	timeNow=Clock_Milliseconds();

	timeDiff=timeNow-m_LastUpdateTime;
	m_LastUpdateTime=timeNow;

	m_uOffset += timeDiff * setting->uScrollPerMs * setting->skyTexelsPerUnit;
	m_vOffset += timeDiff * setting->vScrollPerMs * setting->skyTexelsPerUnit;

	//clamp uv coordinate into 0,1 range
	m_uOffset = m_uOffset - (Real)((Int) m_uOffset);
	m_vOffset = m_vOffset - (Real)((Int) m_vOffset);

	fu= m_uOffset + (SKYPLANE_SIZE * 2) * setting->skyTexelsPerUnit;
	fv= m_vOffset + (SKYPLANE_SIZE * 2) * setting->skyTexelsPerUnit;

	VertexMaterialClass *vmat=VertexMaterialClass::Get_Preset(VertexMaterialClass::PRELIT_DIFFUSE);
	DX8Wrapper::Set_Material(vmat);
	REF_PTR_RELEASE(vmat);

	ShaderClass m_shader2=ShaderClass::_PresetOpaqueShader;
	m_shader2.Set_Cull_Mode(ShaderClass::CULL_MODE_DISABLE);
	m_shader2.Set_Depth_Compare(ShaderClass::PASS_ALWAYS);	//no need to check against z-buffer, sky always rendered first.
	m_shader2.Set_Depth_Mask(ShaderClass::DEPTH_WRITE_DISABLE);	//sky is always behind everything so no need to update z-buffer

	DX8Wrapper::Set_Shader(m_shader2);

	DX8Wrapper::Set_Texture(0,setting->skyTexture);

	//draw an infinite sky plane
	DynamicVBAccessClass vb_access(BUFFER_TYPE_DYNAMIC_DX8,dynamic_fvf_type,4);
	{
		DynamicVBAccessClass::WriteLockClass lock(&vb_access);
		VertexFormatXYZNDUV2* verts=lock.Get_Formatted_Vertex_Array();
		if(verts)
		{
			verts[0].x=-SKYPLANE_SIZE;
			verts[0].y=SKYPLANE_SIZE;
			verts[0].z=SKYPLANE_HEIGHT;
			verts[0].u1=m_uOffset;
			verts[0].v1=fv;
			verts[0].diffuse=setting->vertex01Diffuse;

			verts[1].x=SKYPLANE_SIZE;
			verts[1].y=SKYPLANE_SIZE;
			verts[1].z=SKYPLANE_HEIGHT;
			verts[1].u1=fu;
			verts[1].v1=fv;
			verts[1].diffuse=setting->vertex11Diffuse;

			verts[2].x=SKYPLANE_SIZE;
			verts[2].y=-SKYPLANE_SIZE;
			verts[2].z=SKYPLANE_HEIGHT;
			verts[2].u1=fu;
			verts[2].v1=m_vOffset;
			verts[2].diffuse=setting->vertex10Diffuse;

			verts[3].x=-SKYPLANE_SIZE;
			verts[3].y=-SKYPLANE_SIZE;
			verts[3].z=SKYPLANE_HEIGHT;
			verts[3].u1=m_uOffset;
			verts[3].v1=m_vOffset;
			verts[3].diffuse=setting->vertex00Diffuse;
		}
	}

	DX8Wrapper::Set_Index_Buffer(m_indexBuffer,0);
	DX8Wrapper::Set_Vertex_Buffer(vb_access);

	Matrix3D tm(1);
	tm.Set_Translation(Vector3(0,0,0));
	DX8Wrapper::Set_Transform(D3DTS_WORLD,tm);

	DX8Wrapper::Draw_Triangles(	0,2, 0,	4);	//draw a quad, 2 triangles, 4 verts
}

//-------------------------------------------------------------------------------------------------
/** Renders (draws) the sky body.  Used for moon and sun.  We rotate the image
	* so that it always faces the camera.  This removes perspective and helps hide that
	* it's a flat image. */
//-------------------------------------------------------------------------------------------------
///	@todo: Add code to render properly sorted sun sky body.
void WaterRenderObjClass::renderSkyBody(Matrix3D *mat)
{	
	Vector3 cPos;

	Vector3 pView,pRight,pUp,pPos(SKYBODY_X,SKYBODY_Y,SKYBODY_HEIGHT);

	mat->Get_Translation(&cPos);

	pView=cPos-pPos;	//billboard to camera
	pView.Normalize();	//particle view direction

	Vector3 WorldUp(0,0,-1);	///@todo: hacked so only works for reflections across xy plane

#ifdef ALLOW_TEMPORARIES
	Vector3 rotAxis=Vector3::Cross_Product(WorldUp,pView);	//get axis of rotation.
	rotAxis.Normalize();
#else
	Vector3 rotAxis;
	Vector3::Normalized_Cross_Product(WorldUp, pView, &rotAxis);	
#endif

	Real angle=Vector3::Dot_Product(WorldUp,pView);

	angle = acos(angle);

	Matrix3D tm(1);
	tm.Set(rotAxis,angle);
	tm.Adjust_Translation(Vector3(SKYBODY_X,SKYBODY_Y,SKYBODY_HEIGHT));

	DX8Wrapper::Set_Transform(D3DTS_WORLD,tm);

	VertexMaterialClass *vmat=VertexMaterialClass::Get_Preset(VertexMaterialClass::PRELIT_DIFFUSE);
	DX8Wrapper::Set_Material(vmat);
	REF_PTR_RELEASE(vmat);

	ShaderClass m_shader2=ShaderClass::_PresetAlphaShader;
	m_shader2.Set_Cull_Mode(ShaderClass::CULL_MODE_DISABLE);
	m_shader2.Set_Depth_Compare(ShaderClass::PASS_ALWAYS);	//no need to check against z-buffer, sky always rendered first.
	m_shader2.Set_Depth_Mask(ShaderClass::DEPTH_WRITE_DISABLE);	//sky is always behind everything so no need to update z-buffer

	DX8Wrapper::Set_Shader(m_shader2);

//	DX8Wrapper::Set_Shader(ShaderClass::/*_PresetAdditiveShader*//*_PresetOpaqueShader*/_PresetAlphaShader);
//	DX8Wrapper::Set_Texture(0,setting->skyBodyTexture);

	DX8Wrapper::Set_Texture(0,m_alphaClippingTexture);

	//draw an infinite sky plane
	DynamicVBAccessClass vb_access(BUFFER_TYPE_DYNAMIC_DX8,dynamic_fvf_type,4);
	{
		DynamicVBAccessClass::WriteLockClass lock(&vb_access);
		VertexFormatXYZNDUV2* verts=lock.Get_Formatted_Vertex_Array();
		if(verts)
		{
			verts[0].x=-SKYBODY_SIZE;
			verts[0].y=SKYBODY_SIZE;
			verts[0].z=0;
			verts[0].u2=0;
			verts[0].v2=1;
			verts[0].diffuse=0xffffffff;

			verts[1].x=SKYBODY_SIZE;
			verts[1].y=SKYBODY_SIZE;
			verts[1].z=0;
			verts[1].u2=1;
			verts[1].v2=1;
			verts[1].diffuse=0xffffffff;

			verts[2].x=SKYBODY_SIZE;
			verts[2].y=-SKYBODY_SIZE;
			verts[2].z=0;
			verts[2].u2=1;
			verts[2].v2=0;
			verts[2].diffuse=0xffffffff;

			verts[3].x=-SKYBODY_SIZE;
			verts[3].y=-SKYBODY_SIZE;
			verts[3].z=0;
			verts[3].u2=0;
			verts[3].v2=0;
			verts[3].diffuse=0xffffffff;
		}
	}

	DX8Wrapper::Set_Index_Buffer(m_indexBuffer,0);
	DX8Wrapper::Set_Vertex_Buffer(vb_access);

	DX8Wrapper::Draw_Triangles(	0,2, 0,	4);	//draw a quad, 2 triangles, 4 verts
}

//Defines for procedural water animation.
#define WATER_FREQ	(2.0*3.2831/4.0)	//2pi (full cycle) cover 4 units
#define WATER_AMP	(1.0f)
#define	WATER_OFFSET (0.1f)

//-------------------------------------------------------------------------------------------------
/** Renders (draws) the water surface mesh geometry.
	*	This is a work-in-progress!  Do not use this code! */
//-------------------------------------------------------------------------------------------------
void WaterRenderObjClass::renderWaterMesh(void)
{

	// the grid is TerrainLogic's, read here from the copy takeGridForDraw made at the sync point
	if (!m_drawnMotionEnabled || m_drawnMesh.empty())
		return;	//the water grid is disabled.
	m_modernOpticalLevel=Get_Position().Z;

 	//According to Nvidia there's a D3D bug that happens if you don't start with a
 	//new dynamic VB each frame - so we force a DISCARD by overflowing the counter.
 	m_vertexBufferD3DOffset = 0xffff;

	Setting *setting=&m_settings[m_tod];

	WaterMeshData *pData;
	WaterMeshData *meshData=&m_drawnMesh[0];
	Int	mx=m_drawnCellsX+1;
	Int my=m_drawnCellsY+1;
	Int i,j;

	Real cellSizeX=m_drawnCellSize;
	Real cellSizeY=m_drawnCellSize;
//	Real	uScale2=5.0f*setting->waterRepeatCount/(128.0f)*cellSizeX/10.0f;
//	Real	vScale2=5.0f*setting->waterRepeatCount/(128.0f)*cellSizeY/10.0f;

	//Old waterRepeatCount settings in INI were based on 128x128 water grid of cellsize=10
	//Scale values to correct size.
	Real	uScale=setting->waterRepeatCount/(128.0f)*cellSizeX/10.0f*0.2f;
	Real	vScale=setting->waterRepeatCount/(128.0f)*cellSizeY/10.0f*0.2f;

	Vector3	nx(cellSizeX*2.0f,0,0);
	Vector3 ny(0,cellSizeY*2.0f,0);
	Vector3 C;

#ifdef DO_WATER_SIMULATION		//Debug code used to create a dummy water animation
	//
	// Mark: If you re-enable this water simulation, you might want to consider moving
	// this code to the update() method of the water render object (Colin)
	//

	static Real PhasePerFrameX=0.1f;
	static Real PhasePerFrameY=0.1f;

	//update the mesh heights for this frame (update buffer is 2 samples wider/taller due to border)
	for (j=0,pData=meshData; j<(my+2); j++)
	{	
		for (i=0; i<(mx+2); i++)
		{
			//*pData = WATER_AMP * sin(WATER_FREQ*(0.7f*i + 0.7f*j) - PhasePerFrame);

			pData->height=WATER_OFFSET+WATER_AMP*(sin((float)i*WATER_FREQ*0.4+PhasePerFrameX*0.5)+sin((float)i*WATER_FREQ*0.6+PhasePerFrameX*0.2)+sin((float)j*WATER_FREQ+PhasePerFrameX)+sin((float)j*WATER_FREQ*0.7+PhasePerFrameX*0.3));
//			*pData=WATER_OFFSET+WATER_AMP*(sin((float)i*WATER_FREQ*0.4+PhasePerFrameX*0.5)+sin((float)i*WATER_FREQ*0.6+PhasePerFrameX*0.2)+sin((float)j*WATER_FREQ+PhasePerFrameX)+sin((float)j*WATER_FREQ*0.7+PhasePerFrameX*0.3));
			pData++;
		}
	}

	PhasePerFrameX -= 0.08f;
	PhasePerFrameY -= 0.1f;
#endif

	MaterMeshVertexFormat *vb;
	DX11BufferLockClass vertexLock;
	const unsigned lockBytes=mx*my*sizeof(MaterMeshVertexFormat);
	if (m_vertexBufferD3DOffset < m_numVertices)
	{	//we have room in current VB, append new verts
		if(m_vertexBufferD3D->Lock(m_vertexBufferD3DOffset*sizeof(MaterMeshVertexFormat),lockBytes,(void**)&vb,D3DLOCK_NOOVERWRITE) != D3D_OK)
			return;
		if (void *mirror=vertexLock.Begin(waterVertexTwin, vb,
				m_vertexBufferD3DOffset*sizeof(MaterMeshVertexFormat), lockBytes,
				D3DLOCK_NOOVERWRITE))
			vb=(MaterMeshVertexFormat*)mirror;
	}
	else
	{	//ran out of room in last VB, request a substitute VB.
		if(m_vertexBufferD3D->Lock(0,lockBytes,(void**)&vb,D3DLOCK_DISCARD) != D3D_OK)
			return;
		m_vertexBufferD3DOffset=0;	//reset start of page to first vertex
		if (void *mirror=vertexLock.Begin(waterVertexTwin, vb, 0, lockBytes, D3DLOCK_DISCARD))
			vb=(MaterMeshVertexFormat*)mirror;
	}
	Int diffuse;
	diffuse = setting->waterDiffuse&0x00ffffff;
	Int alpha = (setting->waterDiffuse & 0xff000000)>>24;
	// Reduce alpha for wave mesh
	alpha -= 0x20;
	diffuse |= alpha<<24;

	//I pulled some of these constants out of the loops for speed:
	Real uvCosScale=0.02*cos(3*m_riverVOrigin);
	Real sinOffset=25*m_riverVOrigin;
	Real originScale=m_riverVOrigin/vScale; 
	Real bumpSizeDiv=cellSizeY/BUMP_SIZE;
	Real bumpSizeDiv2=0.3f*cellSizeY/BUMP_SIZE;

	//Data has a 1 vertex padding all around it so we don't need to special-case edges.  Improves performance
	for (j=0,pData=meshData+mx+2+1; j<my; j++,pData+=2)	//skip 2 horizontal border samples after each row
	{	
		Real y=(float)j*cellSizeY;
		Real v1Offset=m_riverVOrigin+(float)j*vScale + uvCosScale*WWMath::Fast_Sin(sinOffset+y*PI/(8*MAP_XY_FACTOR));
		Real v2Offset=((float)j+originScale)*bumpSizeDiv + (float)j*bumpSizeDiv2;

		for (i=0; i<mx; i++)
		{
			//compute normal by looking at 4 vertex neightbors
#ifdef USE_MESH_NORMALS
			nx.Z=(pData+1)->height - (pData-1)->height;
			ny.Z=(pData+mx+2)->height - (pData-mx-2)->height;
//			nx.Z=*(pData+1)-*(pData-1);
//			ny.Z=*(pData+mx+2)-*(pData-mx-2);
			Vector3::Cross_Product(nx,ny,&C);
			C.Normalize();
			vb->nx = C.X;
			vb->ny = C.X;
			vb->nz = C.X;
#endif
			Real x = (float)i*cellSizeX;
			vb->x=	x;
			vb->y=	y;
			vb->z=  pData->height;//WATER_OFFSET+WATER_AMP*(sin((float)i*WATER_FREQ+PhasePerFrame)+cos((float)j*WATER_FREQ+PhasePerFrame));

			vb->diffuse = diffuse;
#ifdef SCROLL_UV
//			vb->diffuse=0x80ffffff;
			vb->u1=(float)i*uScale;
			vb->v1=v1Offset;

			//old slow version
			//vb->v1=m_riverVOrigin+(float)j*vScale + 0.02*cos(3*m_riverVOrigin)*sin(25*m_riverVOrigin+y*PI/(8*MAP_XY_FACTOR));

//			vb->u2=m_initialGridU2+(float)i*uScale2;
//			vb->v2=m_initialGridV2+(float)j*vScale2;
#else
			vb->u1=(float)i*uScale;
			vb->v1=(float)j*vScale;
#endif
			vb->u2=(float)(i)*cellSizeX/BUMP_SIZE; 
			vb->v2=v2Offset;
#if ZH_MODERN_WATER
			if (m_useModernWater && m_trapezoidWaterPixelShader) {
				Vector3 world;
				Matrix3D::Transform_Vector(Transform,Vector3(x,y,pData->height),&world);
				vb->u2=0.0f;
				vb->v2=world.Z;
			}
#endif
			//old slow code
			//vb->v2=(float)(j+m_riverVOrigin/vScale )*cellSizeY/BUMP_SIZE+ 0.3f*(float)j*cellSizeY/BUMP_SIZE;
			vb++;
			pData++;
		}
	}

	vertexLock.End();

	m_vertexBufferD3D->Unlock();

	Matrix3D tm(Transform);

	DX8Wrapper::Set_Transform(D3DTS_WORLD,tm);	//position the water surface
	DX8Wrapper::Set_Material(m_meshVertexMaterialClass);

	ShaderClass::CullModeType oldCullMode=m_shaderClass.Get_Cull_Mode();

	ShaderClass::DepthMaskType oldDepthMask=m_shaderClass.Get_Depth_Mask();
	m_shaderClass.Set_Depth_Mask(ShaderClass::DEPTH_WRITE_DISABLE);	//disable writing to z-buffer to prevent particle clipping.

	m_shaderClass.Set_Cull_Mode(ShaderClass::CULL_MODE_ENABLE);	//water should be visible from both sides

	DX8Wrapper::Set_Shader(m_shaderClass);
	const unsigned oldTextureFactor = DX8Wrapper::Get_DX8_Render_State(D3DRS_TEXTUREFACTOR);	//the foam borrows it
#if 1
	setupFlatWaterShader();
#else 
	//DX8Wrapper::Set_Shader(ShaderClass::_PresetOpaqueShader);
	DX8Wrapper::Set_Texture(0,setting->waterTexture);
	DX8Wrapper::Set_Texture(1,setting->waterTexture);

	DX8Wrapper::Set_Light(0,*m_meshLight);
	DX8Wrapper::Set_Light(1,NULL);
	DX8Wrapper::Set_Light(2,NULL);
	DX8Wrapper::Set_Light(3,NULL);
/*
	DX8Wrapper::Set_DX8_Render_State(D3DRS_AMBIENT,0);	//turn off scene ambient
	DX8Wrapper::Set_DX8_Render_State(D3DRS_SPECULARENABLE,TRUE);
	DX8Wrapper::Set_DX8_Render_State(D3DRS_LOCALVIEWER,TRUE); 
*/

	DX8Wrapper::Apply_Render_State_Changes();	//force update of view and projection matrices
#endif

//	DX8Wrapper::Set_DX8_Render_State(D3DRS_ZFUNC,D3DCMP_ALWAYS);	//used to display grid under map.

	// D3D9 takes the base vertex index on the draw call, not here.
	m_pDev->SetIndices(m_indexBufferD3D);
	m_pDev->SetStreamSource(0,m_vertexBufferD3D,0,sizeof(MaterMeshVertexFormat));
	DX8Wrapper::Set_Vertex_Format(WATER_MESH_FVF);

	Direct3D11_Mirror_Stream_Source(waterVertexTwin,sizeof(MaterMeshVertexFormat),0);
	Direct3D11_Mirror_Indices(waterIndexTwin);

	if (TheTerrainRenderObject->getShroud() && !m_trapezoidWaterPixelShader)
	{	//we have a shroud to apply and can't do it inside the pixel shader.
		//so do it in stage1
		W3DShaderManager::setTexture(0,TheTerrainRenderObject->getShroud()->getShroudTexture());
		W3DShaderManager::setShader(W3DShaderManager::ST_SHROUD_TEXTURE, 1);

		//modulate with shroud texture
		DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_COLORARG1, D3DTA_TEXTURE );	//stage 1 texture
		DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_COLORARG2, D3DTA_CURRENT );	//previous stage texture
		DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_COLOROP,   D3DTOP_MODULATE );
		DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_ALPHAOP,   D3DTOP_MODULATE );

		//Shroud shader uses z-compare of EQUAL which wouldn't work on water because it doesn't
		//write to the zbuffer.  Change to LESSEQUAL.
		DX8Wrapper::Set_DX8_Render_State(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
		if (!Direct3D11_Present_Is_Enabled())
			m_pDev->DrawIndexedPrimitive(D3DPT_TRIANGLESTRIP,m_vertexBufferD3DOffset,0,mx*my,0,m_numIndices-2);
		Direct3D11_Draw_Indexed_Strip(m_numIndices,0,m_vertexBufferD3DOffset);
		DX8Wrapper::Set_DX8_Render_State(D3DRS_ZFUNC, D3DCMP_EQUAL);
		W3DShaderManager::resetShader(W3DShaderManager::ST_SHROUD_TEXTURE);
	}
	else
	{
		if (!Direct3D11_Present_Is_Enabled())
			m_pDev->DrawIndexedPrimitive(D3DPT_TRIANGLESTRIP,m_vertexBufferD3DOffset,0,mx*my,0,m_numIndices-2);
		Direct3D11_Draw_Indexed_Strip(m_numIndices,0,m_vertexBufferD3DOffset);
	}

	Debug_Statistics::Record_DX8_Polys_And_Vertices(m_numIndices-2,mx*my,ShaderClass::_PresetOpaqueShader);

//	DX8Wrapper::Set_DX8_Render_State(D3DRS_FILLMODE,D3DFILL_SOLID);

	if (m_trapezoidWaterPixelShader) DX8Wrapper::Set_Pixel_Shader(NULL);
	if (waterFoam())
		DX8Wrapper::Set_DX8_Render_State(D3DRS_TEXTUREFACTOR, oldTextureFactor);

	m_vertexBufferD3DOffset += mx*my;	//advance past vertices already in buffer

	DX8Wrapper::Set_Texture(0,NULL);
	DX8Wrapper::Set_Texture(1,NULL);
	ShaderClass::Invalidate();
	m_shaderClass.Set_Cull_Mode(oldCullMode);	//water should be visible from both sides

	// restore shader to old mask
	m_shaderClass.Set_Depth_Mask(oldDepthMask);	 

	//W3DShaderManager::resetShader(W3DShaderManager::ST_SHROUD_TEXTURE);

}

/* The grid's setters, addVelocity and changeGridHeight that stood here are TerrainLogic's WaterGrid's
	 (GameLogic/WaterGridMotion.h), moved with the members they write (LOGIC-THREAD M7). */

static Real wobble(Real baseV, Real offset, Bool wobble)
{
	if (!wobble) return 0;
	offset = sin(2*PI*baseV - 3*offset);
	return offset/22;
}

/**Utility function used to query water heights in a manner that works in both RTS and WB.*/
Real WaterRenderObjClass::getWaterHeight(Real x, Real y)
{
	const WaterHandle *waterHandle = NULL;
	Real waterZ = 0.0f;
	ICoord3D iLoc;

	iLoc.x = REAL_TO_INT_FLOOR( x + 0.5f );
	iLoc.y = REAL_TO_INT_FLOOR( y + 0.5f );
	iLoc.z = 0;

	for( PolygonTrigger *pTrig = PolygonTrigger::getFirstPolygonTrigger(); pTrig; pTrig = pTrig->getNext() ) 
	{

		if( !pTrig->isWaterArea() ) 
			continue;

		// See if point is in a water area
		if( pTrig->pointInTrigger( iLoc ) ) 
		{

			if( pTrig->getPoint( 0 )->z >= waterZ )
			{

				waterZ = pTrig->getPoint( 0 )->z;
				waterHandle = pTrig->getWaterHandle();

			}  // end if

		}  // end if

	}  // end for

	if (waterHandle)
		return waterHandle->m_polygon->getPoint( 0 )->z;
	return INVALID_WATER_HEIGHT;	//point not underwater
}

// Scene geometry attenuates the render mesh only. It never changes the simulation water grid.
Real WaterRenderObjClass::modernWaterDamping(Real x, Real y, Real z, Real offshore, Real *obstacleAttenuation, Real *terrainDepth, Real *hullFreeDamping, Real *hullFreeObstacle) const
{
	if (obstacleAttenuation) *obstacleAttenuation=1.0f;
	if (hullFreeObstacle) *hullFreeObstacle=1.0f;
	if (terrainDepth) *terrainDepth=0.0f;
	// Early zero returns remain zero for both the complete CPU fallback and the FFT envelope.
	if (hullFreeDamping) *hullFreeDamping=0.0f;
	WorldHeightMap *map=TheTerrainRenderObject?TheTerrainRenderObject->getMap():NULL;
	Real damping=1.0f;
	if (map && map->getXExtent()>1 && map->getYExtent()>1) {
		const Real depth=WaterTerrainDepth(map,x,y,z);
		if (terrainDepth) *terrainDepth=depth;
		damping=WaterSurfaceModel::TerrainDamping(depth,offshore);
		if (damping<=0.0f && !hullFreeObstacle) return 0.0f;
	}
	// The broad phase only omits objects whose exact narrow phase returns one. River points
	// and failed/oversized index builds retain the original ordered full list.
	const WaterObstacleIndex::Range candidates=offshore>0.0f?m_modernObstacleIndex.Query(x,y):WaterObstacleIndex::Range{};
	const size_t obstacleCount=candidates.indexed?candidates.count:m_modernPatchObstacles.size();
	for (size_t i=0;i<obstacleCount;++i) {
		const ModernWaterObstacle &o=m_modernPatchObstacles[candidates.indexed?candidates.indices[i]:i];
		if (z<o.position.Z-12.0f || z>o.position.Z+o.height+3.0f) continue;
		const Real dx=x-o.position.X,dy=y-o.position.Y;
		if (offshore>0.0f && WaterSurfaceModel::BeyondObstacleFade(dx,dy,o.radius)) continue;
		Real obstacleDamping;
		if (o.hasHull && offshore>0.0f) {
			// An actual rendered waterline replaces the broad collision box offshore. Preserve
			// the original river footprint exactly where the offshore field is zero.
			const Real legacy=offshore<1.0f?WaterSurfaceModel::ShoreDamping(sqrtf(dx*dx+dy*dy)-o.radius):1.0f;
			obstacleDamping=legacy+(1.0f-legacy)*offshore;
		} else if (o.box && offshore>0.0f) {
			const Real localX=dx*o.cosine+dy*o.sine,localY=-dx*o.sine+dy*o.cosine;
			obstacleDamping=WaterSurfaceModel::BoxDamping(localX,localY,o.major,o.minor);
			if (offshore<1.0f) {
				const Real legacy=WaterSurfaceModel::ShoreDamping(sqrtf(dx*dx+dy*dy)-o.radius);
				obstacleDamping=legacy+(obstacleDamping-legacy)*offshore;
			}
		} else obstacleDamping=WaterSurfaceModel::ShoreDamping(sqrtf(dx*dx+dy*dy)-o.radius);
		// Cage displacement applies the exact rendered hull in the shader, including R0.
		// Its independent object envelope must not inherit the legacy circular hull proxy.
		if (hullFreeObstacle && !o.hasHull) *hullFreeObstacle=min(*hullFreeObstacle,obstacleDamping);
		// Preserve the legacy outputs at their original early-out point while collecting
		// the independent cage envelope through every remaining obstacle.
		if(damping>0.0f) {
			if (obstacleAttenuation) *obstacleAttenuation=min(*obstacleAttenuation,obstacleDamping);
			damping=min(damping,obstacleDamping);
		}
		if (damping<=0.0f && !hullFreeObstacle) return 0.0f;
	}
	if (hullFreeDamping) *hullFreeDamping=damping;
	if (offshore>0.0f && damping>0.0f) {
		const Real hull=1.0f+(modernHullDamping(x,y,z)-1.0f)*offshore;
		damping=min(damping,hull);
		if (obstacleAttenuation) *obstacleAttenuation=min(*obstacleAttenuation,hull);
	}
	return damping;
}

// Render-only geometry keeps a complete analytic CPU fallback. Projected ocean interiors carry
// optional FFT displacement metadata in the unlit normal channel; every authored edge retains the
// same canonical analytic samples regardless of whether the GPU ocean field is available.
void WaterRenderObjClass::drawModernWaterPatch(const Vector3 points[4], Bool river, Real riverStart, Real riverEnd)
{
#if ZH_MODERN_WATER
	m_modernOpticalLevel = points[0].Z;
	Vector3 boundsMin = points[0], boundsMax = points[0];
	for (Int i = 1; i < 4; ++i) {
		boundsMin.X = min(boundsMin.X, points[i].X);
		boundsMin.Y = min(boundsMin.Y, points[i].Y);
		boundsMin.Z = min(boundsMin.Z, points[i].Z);
		boundsMax.X = max(boundsMax.X, points[i].X);
		boundsMax.Y = max(boundsMax.Y, points[i].Y);
		boundsMax.Z = max(boundsMax.Z, points[i].Z);
	}
	boundsMin.Z -= WaterSurfaceModel::MaxDisplacement;
	boundsMax.Z += WaterSurfaceModel::MaxDisplacement;
	if (!river) {
		boundsMin.X -= WaterFftGeometry::MaxHorizontalDisplacement;
		boundsMin.Y -= WaterFftGeometry::MaxHorizontalDisplacement;
		boundsMax.X += WaterFftGeometry::MaxHorizontalDisplacement;
		boundsMax.Y += WaterFftGeometry::MaxHorizontalDisplacement;
	}
	// Include all wave crests in culling; offscreen authored patches do not evaluate any modes.
	if (m_modernRenderCamera && m_modernRenderCamera->Cull_Box(AABoxClass((boundsMin + boundsMax) * 0.5f, (boundsMax - boundsMin) * 0.5f))) return;
	const ModernOpticalTexture *oceanContext = NULL;
	if (!river && modernWaterOpticalTexture(m_modernOpticalLevel))
		for (size_t field = 0; field < m_modernOpticalTextures.size(); ++field)
			if (m_modernOpticalTextures[field].level == m_modernOpticalLevel) {
				oceanContext = &m_modernOpticalTextures[field];
				break;
			}
	const Bool waveCageAvailable = oceanContext && oceanContext->waveCage;
	const Int cells = m_modernMeshCells, side = cells + 1;
	Int vertexCount = side * side;
	WaterSurfaceModel::Point corners[4];
	for (Int i = 0; i < 4; ++i) corners[i] = {points[i].X, points[i].Y, points[i].Z};
	const WaterSurfaceModel::Point camera = {m_modernCamera.X, m_modernCamera.Y, m_modernCamera.Z};
	WaterSurfaceModel::Point edges[4][WaterSurfaceModel::MeshCells + 1];
	for (Int i = 0; i <= cells; ++i) {
		const Real t = (Real) i / cells;
		edges[0][i] = WaterSurfaceModel::EdgePoint(corners[0], corners[1], t, camera);
		edges[1][i] = WaterSurfaceModel::EdgePoint(corners[1], corners[2], t, camera);
		edges[2][i] = WaterSurfaceModel::EdgePoint(corners[3], corners[2], t, camera);
		edges[3][i] = WaterSurfaceModel::EdgePoint(corners[0], corners[3], t, camera);
	}
	Real minX = points[0].X, maxX = minX, minY = points[0].Y, maxY = minY;
	for (Int i = 1; i < 4; ++i) {
		minX = min(minX, points[i].X);
		maxX = max(maxX, points[i].X);
		minY = min(minY, points[i].Y);
		maxY = max(maxY, points[i].Y);
	}
	m_modernPatchObstacles.clear();
	for (size_t i = 0; i < m_modernObstacles.size(); ++i) {
		const ModernWaterObstacle &o = m_modernObstacles[i];
		const Real reach = o.radius + 9.0f;
		if (o.position.X + reach >= minX && o.position.X - reach <= maxX && o.position.Y + reach >= minY
			&& o.position.Y - reach <= maxY) m_modernPatchObstacles.push_back(o);
	}
	m_modernObstacleIndex.Clear();
	m_modernObstacleInfluences.clear();
	if (oceanContext && oceanContext->oceanExtent > 0 && m_modernPatchObstacles.size() <= WaterObstacleIndex::Grid::MaxObstacles) {
		m_modernObstacleInfluences.reserve(m_modernPatchObstacles.size());
		for (const ModernWaterObstacle &o : m_modernPatchObstacles)
			m_modernObstacleInfluences.push_back({o.position.X, o.position.Y, o.radius});
		m_modernObstacleIndex.Build(m_modernObstacleInfluences, {minX, minY, maxX, maxY});
	}
	Real shadeR = TheWaterTransparency->m_standingWaterColor.red;
	Real shadeG = TheWaterTransparency->m_standingWaterColor.green;
	Real shadeB = TheWaterTransparency->m_standingWaterColor.blue;

	//If the water color is not overridden, use legacy lighting code.
	if ( shadeR == 1.0f && shadeG == 1.0f && shadeB == 1.0f)
	{
		shadeR = TheGlobalData->m_terrainAmbient[0].red;
		shadeG = TheGlobalData->m_terrainAmbient[0].green;
		shadeB = TheGlobalData->m_terrainAmbient[0].blue;

		//Add in diffuse lighting from each terrain light
		for (Int lightIndex = 0; lightIndex < TheGlobalData->m_numGlobalLights; lightIndex++)
		{
			if (-TheGlobalData->m_terrainLightPos[lightIndex].z > 0)
			{
				shadeR += -TheGlobalData->m_terrainLightPos[lightIndex].z * TheGlobalData->m_terrainDiffuse[lightIndex].red;
				shadeG += -TheGlobalData->m_terrainLightPos[lightIndex].z * TheGlobalData->m_terrainDiffuse[lightIndex].green;
				shadeB += -TheGlobalData->m_terrainLightPos[lightIndex].z * TheGlobalData->m_terrainDiffuse[lightIndex].blue;
			}
		}

		//Get water material colors
		Real waterShadeR = (m_settings[m_tod].waterDiffuse & 0xff) / 255.0f;
		Real waterShadeG = ((m_settings[m_tod].waterDiffuse >> 8) & 0xff) / 255.0f;
		Real waterShadeB = ((m_settings[m_tod].waterDiffuse >> 16) & 0xff) / 255.0f;

		shadeR = shadeR * waterShadeR * 255.0f;
		shadeG = shadeG * waterShadeG * 255.0f;
		shadeB = shadeB * waterShadeB * 255.0f;
	}
	else
	{
		shadeR = shadeR * 255.0f;
		shadeG = shadeG * 255.0f;
		shadeB = shadeB * 255.0f;

		if (shadeR == 0 && shadeG == 0 && shadeB == 0)
		{
			//special case where we disable lighting
			shadeR = 255;
			shadeG = 255;
			shadeB = 255;
		}
	}

	Int diffuse = REAL_TO_INT(shadeB) | (REAL_TO_INT(shadeG) << 8) | (REAL_TO_INT(shadeR) << 16);

	//Keep diffuse from lighting calculations but substitute custom alpha
	diffuse |= m_settings[m_tod].waterDiffuse & 0xff000000; //copy alpha/opacity from ini setting

	WaterProjectedMesh::Mesh projectedMesh;
	WaterFftGeometry::SeamFade fftSeam;

	Bool projected = FALSE;
	const float levels[4] = {points[0].Z, points[1].Z, points[2].Z, points[3].Z};
	// Homography is defined on one plane. Authored sloped patches retain every original Z.
	if (!river && oceanContext && oceanContext->oceanExtent > 0 && m_modernRenderCamera && WaterProjectedMesh::HasSingleLevel(levels)) {
		WaterProjectedMesh::Projection projection;
		Matrix3D view;
		m_modernRenderCamera->Get_View_Matrix(&view);
		const Matrix4x4 &clip = m_modernRenderCamera->Get_Projection_Matrix();
		for (Int row = 0; row < 3; ++row) {
			const Int clipRow = row == 2 ? 3 : row;
			for (Int column = 0; column < 2; ++column) {
				projection.matrix[row * 3 + column] = 0;
				for (Int k = 0; k < 3; ++k) projection.matrix[row * 3 + column] += double(clip[clipRow][k]) * view[k][column];
			}
			projection.matrix[row * 3 + 2] = clip[clipRow][3];
			for (Int k = 0; k < 3; ++k) projection.matrix[row * 3 + 2] += double(clip[clipRow][k]) * (double(view[k][2]) * points[0].Z + view[k][3]);
		}
		projection.depth = {-double(view[2][0]), -double(view[2][1]), -double(view[2][2]) * points[0].Z - view[2][3]};
		Real nearDistance, farDistance;
		m_modernRenderCamera->Get_Clip_Planes(nearDistance, farDistance);
		projection.nearDistance = max(0.01f, nearDistance);
		projection.farDistance = farDistance + WaterSurfaceModel::MaxDisplacement;
		const Real separation = fabsf(m_modernCamera.Z - points[0].Z);
		projection.margin = clamp(4.0f * WaterSurfaceModel::MaxDisplacement / max(separation, 1.0f), 0.12f, 0.8f);
		WaterProjectedMesh::Point polygon[4];
		for (Int i = 0; i < 4; ++i) polygon[i] = {points[i].X, points[i].Y};
		std::vector<WaterProjectedMesh::BoundaryKnot> boundaryKnots;
		boundaryKnots.reserve(4 * (cells + 1));
		for (Int edge = 0; edge < 4; ++edge) {
			WaterSurfaceModel::Point a = corners[edge], b = corners[(edge + 1) % 4];
			if (a.x > b.x || (a.x == b.x && a.y > b.y)) {
				const WaterSurfaceModel::Point swap = a;
				a = b;
				b = swap;
			}
			const double dx = double(b.x) - a.x, dy = double(b.y) - a.y, len2 = dx * dx + dy * dy;
			if (len2 < 1e-12) continue;
			WaterSurfaceModel::Point knot[WaterSurfaceModel::MeshCells + 1];
			for (Int i = 0; i <= cells; ++i) knot[i] = WaterSurfaceModel::EdgePoint(a, b, Real(i) / cells, camera);
			for (Int i = 1; i < cells; i += 2) {
				knot[i].x = WaterSurfaceModel::MorphCoordinate(knot[i].x, knot[i - 1].x, knot[i + 1].x, m_modernMeshMorph);
				knot[i].y = WaterSurfaceModel::MorphCoordinate(knot[i].y, knot[i - 1].y, knot[i + 1].y, m_modernMeshMorph);
			}
			for (Int i = 0; i <= cells; ++i) {
				const double t = ((knot[i].x - a.x) * dx + (knot[i].y - a.y) * dy) / len2;
				// Use the authored straight XY edge for clipping; retain the exact float knot in
				// the final vertex. The difference is at most float interpolation roundoff.
				boundaryKnots.push_back({{a.x + dx * t, a.y + dy * t}, edge, i, t});
			}
		}
		if (separation > WaterSurfaceModel::MaxDisplacement*2 && projection.Prepare() &&
			WaterProjectedMesh::Build(polygon, projection, {m_modernProjectedColumns, m_modernProjectedRows}, projectedMesh,
				m_modernProjectedBudget, boundaryKnots)) {
			projected = TRUE;
			vertexCount = (Int) projectedMesh.vertices.size();
			fftSeam.Prepare(polygon, projection, {m_modernProjectedColumns, m_modernProjectedRows});
			// Optional bow detail only replaces fresh interior triangles. The same submitted
			// contact contours drive the shader packet and its projected geometric demand.
			{
				WaterBowRefinement::View bowView;
				bowView.projection = projection;
				bowView.resolution = {m_modernProjectedColumns, m_modernProjectedRows};
				bowView.width = m_modernProjectedWidth;
				bowView.height = m_modernProjectedHeight;
				for (Int row = 0; row < 3; ++row) {
					bowView.verticalClip[row] = 0;
					const Int clipRow = row == 2 ? 3 : row;
					for (Int k = 0; k < 3; ++k) bowView.verticalClip[row] += double(clip[clipRow][k]) * view[k][2];
				}
				const auto patchKey = WaterBowRefinement::MakePatchKey(polygon, points[0].Z, m_modernBowPatchOwner, m_modernBowPatchPart);
				auto prepareBow = [&](const WaterBowRefinement::Bow &bow, WaterBowRefinement::Prepared &prepared) {
					const auto offshore = WaterSurfaceModel::SampleOffshore(oceanContext->ocean.data(), m_modernHeightWidth, m_modernHeightHeight,
					Real(bow.centre.x) / MAP_XY_FACTOR + m_modernHeightBorder,
						Real(bow.centre.y) / MAP_XY_FACTOR + m_modernHeightBorder, MAP_XY_FACTOR);
					Real terrainDepth = 0;
					const Real damping = modernWaterDamping(Real(bow.centre.x), Real(bow.centre.y), points[0].Z, offshore.height, NULL,
						&terrainDepth);
					const Real exposure = WaterSurfaceModel::SampleOffshore(oceanContext->shoreContext.exposure.data(),
					m_modernHeightWidth, m_modernHeightHeight, Real(bow.centre.x) / MAP_XY_FACTOR + m_modernHeightBorder,
					Real(bow.centre.y) / MAP_XY_FACTOR + m_modernHeightBorder, MAP_XY_FACTOR).height;
					const Real amplitude = fftSeam.Weight(bow.centre, exposure) * WaterFftGeometry::SafeDamping(damping, terrainDepth);
					return WaterBowRefinement::PrepareBow(bow, bowView, prepared, amplitude);
				};
				std::vector<size_t> hullOrder;
				hullOrder.reserve(m_modernHulls.size());
				for (size_t i = 0; i < m_modernHulls.size(); ++i) hullOrder.push_back(i);
				std::sort(hullOrder.begin(), hullOrder.end(), [&](size_t a, size_t b) {
					return m_modernHullObjectIDs[a] < m_modernHullObjectIDs[b];
				});
				for (size_t i : hullOrder) {
					WaterBowRefinement::Bow bow;
					WaterBowRefinement::Prepared prepared;
					const auto &contact = i < m_modernContactHulls.size() ? m_modernContactHulls[i] : m_modernHulls[i];
					if (!WaterBowRefinement::MakeBow(m_modernHullObjectIDs[i], m_modernHulls[i], contact, points[0].Z, bow) ||
					!prepareBow(bow, prepared) || prepared.heightGate <= 0 || prepared.desiredDepth <= 0) continue;
					if (prepared.cell.x < - WaterBowRefinement::WindowRadius || prepared.cell.y < - WaterBowRefinement::WindowRadius ||
					prepared.cell.x > m_modernProjectedColumns + WaterBowRefinement::WindowRadius ||
					prepared.cell.y > m_modernProjectedRows + WaterBowRefinement::WindowRadius) continue;
					if (!WaterBowRefinement::HasRefinableDemand(projectedMesh, bowView, polygon, prepared)) continue;
					m_modernBowAdmissions.Request(patchKey, bow, m_modernProjectedBudget);
				}
				std::vector<WaterBowRefinement::Active> active;
				active.reserve(WaterBowRefinement::MaxRegions);
				for (const auto &slot : m_modernBowAdmissions.Slots()) if (slot.occupied && slot.patch == patchKey && slot.amount > 0) {
					WaterBowRefinement::Prepared prepared;
					if (prepareBow(slot.bow, prepared)) active.push_back({prepared, slot.amount});
				}
				WaterBowRefinement::Stats bowStats;
				const size_t bowBudget = min(size_t(65535), m_modernProjectedBudget + active.size() * WaterBowRefinement::RegionVertices);
				WaterBowRefinement::Refine(projectedMesh, bowView, polygon, active, bowBudget, bowStats,
				WaterBowRefinement::MaxAddedVertices - m_modernBowVertices, WaterBowRefinement::MaxAddedTriangles - m_modernBowTriangles);
				m_modernBowVertices += bowStats.addedVertices;
				m_modernBowTriangles += bowStats.addedTriangles;
			}
			// Authored ocean polygons often extend beneath the entire land mass. The
			// projected lattice covers that hidden area too; discard only triangles
			// whose complete displaced footprint is buried, before evaluating waves,
			// obstacles or uploading their vertices. Endpoint depths alone can miss a
			// small pond inside a triangle, so inspect every overlapping terrain cell.
			// Keep the original mesh for an underwater/below-terrain free camera.
			if (m_modernCamera.Z > points[0].Z + WaterSurfaceModel::MaxDisplacement + 0.01f &&
				WaterTerrainCulling::CameraAboveTerrain(
					m_modernHeights.data(), m_modernHeightWidth, m_modernHeightHeight,
					m_modernHeightBorder, MAP_XY_FACTOR, MAP_HEIGHT_SCALE,
					m_modernCamera.X, m_modernCamera.Y, m_modernCamera.Z))
			{
				WaterTerrainCulling::CullDryTriangles(
					projectedMesh, m_modernHeights.data(), m_modernHeightWidth, m_modernHeightHeight,
					m_modernHeightBorder, MAP_XY_FACTOR, MAP_HEIGHT_SCALE, points[0].Z,
					WaterSurfaceModel::MaxDisplacement + 0.01f,
					WaterFftGeometry::MaxHorizontalDisplacement + 0.01f);
			}

			if (waveCageAvailable)
			{
				const auto depth = [&](const WaterProjectedMesh::Point &p)
				{
					return points[0].Z - WaterWaveCage::Height(
						m_modernHeights.data(), m_modernHeightWidth, m_modernHeightHeight,
						m_modernHeightBorder, MAP_XY_FACTOR, MAP_HEIGHT_SCALE, float(p.x), float(p.y));
				};
				const auto supported = [&](const WaterProjectedMesh::Point &p)
				{
					return WaterSurfaceModel::SampleOffshore(
						oceanContext->waveCageSupport.data(), m_modernHeightWidth, m_modernHeightHeight,
						float(p.x) / MAP_XY_FACTOR + m_modernHeightBorder,
						float(p.y) / MAP_XY_FACTOR + m_modernHeightBorder, MAP_XY_FACTOR).height > 0.0f;
				};

				// Split only supported interior triangles at the rest waterline. The helper
				// retains authored seams and leaves the original mesh intact if its budget fails.
				WaterWaveCageMesh::ClipRestWaterline(projectedMesh, depth, supported);
			}

			// Waterline clipping also leaves unreferenced dry vertices behind.
			WaterTerrainCulling::CompactVertices(projectedMesh);
			vertexCount = (Int) projectedMesh.vertices.size();
		}
	}

	if (projected && projectedMesh.indices.empty()) return;
	const Bool waveCage = projected && waveCageAvailable;
	DynamicVBAccessClass vertices(BUFFER_TYPE_DYNAMIC_DX8, dynamic_fvf_type, vertexCount);
	{
		DynamicVBAccessClass::WriteLockClass lock(&vertices);
		VertexFormatXYZNDUV2 *grid = lock.Get_Formatted_Vertex_Array();
		VertexFormatXYZNDUV2 *vertex = grid;
		W3DShroud *shroud = TheTerrainRenderObject ? TheTerrainRenderObject->getShroud() : NULL;
		if (projected) {
			auto makeBaseVertex = [&](const WaterSurfaceModel::Point &p, Bool boundarySample = FALSE) {
				const Bool share = m_modernProjectedCacheEnabled || boundarySample;
				const WaterProjectedMesh::WorldKey key = WaterProjectedMesh::SampleKey(p.x, p.y, p.z);
				auto cached = share ? m_modernProjectedSamples.find(key) : m_modernProjectedSamples.end();
				ModernProjectedSample sample;
				if (cached != m_modernProjectedSamples.end()) {
					sample = cached->second;
				} else {
					const WaterSurfaceModel::Sample offshore = WaterSurfaceModel::SampleOffshore(oceanContext->ocean.data(),
						m_modernHeightWidth, m_modernHeightHeight, p.x / MAP_XY_FACTOR + m_modernHeightBorder,
							p.y / MAP_XY_FACTOR + m_modernHeightBorder, MAP_XY_FACTOR);
					sample.obstacle = sample.hullFreeObstacle = 1;
					Real terrainDepth = 0, hullFreeDamping = 0;
					sample.damping = modernWaterDamping(p.x, p.y, p.z, offshore.height, &sample.obstacle, &terrainDepth, &hullFreeDamping,
						waveCage ? &sample.hullFreeObstacle : NULL);
					sample.offshore = offshore.height;
					sample.exposure = WaterSurfaceModel::SampleOffshore(oceanContext->shoreContext.exposure.data(),
						m_modernHeightWidth, m_modernHeightHeight, p.x / MAP_XY_FACTOR + m_modernHeightBorder,
						p.y / MAP_XY_FACTOR + m_modernHeightBorder, MAP_XY_FACTOR).height;
					sample.fftDamping = WaterFftGeometry::SafeDamping(hullFreeDamping, terrainDepth);
					sample.height = sample.damping > 0 ? WaterSurfaceModel::EvaluateBlendedHeight(p.x, p.y, m_modernWaveTime,
						offshore) * sample.damping : 0;
					if (share && m_modernProjectedSamples.size() < WaterSurfaceModel::VertexBudget) m_modernProjectedSamples.emplace(key, sample);
				}
				VertexFormatXYZNDUV2 result;
				result.x = p.x;
				result.y = p.y;
				result.z = p.z + sample.height;
				// The native FFT stages apply exact hull damping to this hull-free floor/obstacle
				// envelope. Interpolating the hull envelope itself produces triangular normals.
				// CPU fallback height and authored edges retain the original combined damping.
				// Connected coastal water shares the ocean's spectral phases. Optical R
				// still controls colour and horizontal chop, not the wave family.
				result.nx = boundarySample ? 0.0f : fftSeam.Weight({p.x, p.y}, sample.exposure);
				result.ny = sample.fftDamping;
				result.nz = sample.offshore;
				result.u1 = 0;
				result.v1 = p.y / 150.0f;
				result.u2 = sample.damping;
				result.v2 = p.z;
				result.diffuse = WaterSurfaceModel::EncodeObstacleAttenuation(diffuse, boundarySample
					|| !waveCage ? sample.obstacle : sample.hullFreeObstacle);
				return result;
			};
			auto interpolate = [](const VertexFormatXYZNDUV2 &a, const VertexFormatXYZNDUV2 &b, Real t) {
				VertexFormatXYZNDUV2 value = a;
				value.x = a.x + (b.x - a.x) * t;
				value.y = a.y + (b.y - a.y) * t;
				value.z = a.z + (b.z - a.z) * t;
				value.u1 = a.u1 + (b.u1 - a.u1) * t;
				value.v1 = a.v1 + (b.v1 - a.v1) * t;
				value.u2 = a.u2 + (b.u2 - a.u2) * t;
				value.v2 = a.v2 + (b.v2 - a.v2) * t;
				value.diffuse = 0;
				for (Int shift = 0; shift < 32; shift += 8) {
					const Real ca = (a.diffuse >> shift) & 255, cb = (b.diffuse >> shift) & 255;
					value.diffuse |= UnsignedInt(ca + (cb - ca) * t + 0.5f) << shift;
				}
				return value;
			};
			VertexFormatXYZNDUV2 edgeVertices[4][WaterSurfaceModel::MeshCells + 1];
			Bool prepared[4] = {FALSE, FALSE, FALSE, FALSE};

			WaterProjectedMesh::Point authored[4];
			for (Int i = 0; i < 4; ++i) authored[i] = {points[i].X, points[i].Y};
			auto prepareEdge = [&](Int edge) {
				if (prepared[edge]) return;
				WaterSurfaceModel::Point a = corners[edge], b = corners[(edge + 1) % 4];
				if (a.x > b.x || (a.x == b.x && a.y > b.y)) {
					const WaterSurfaceModel::Point swap = a;
					a = b;
					b = swap;
				}
				for (Int i = 0; i <= cells; ++i) edgeVertices[edge][i] = makeBaseVertex(WaterSurfaceModel::EdgePoint(a, b,
					Real(i) / cells, camera), TRUE);
				for (Int i = 1; i < cells; i += 2) {
					VertexFormatXYZNDUV2 &v = edgeVertices[edge][i];
					const VertexFormatXYZNDUV2 &lo = edgeVertices[edge][i - 1], &hi = edgeVertices[edge][i + 1];
					v.x = WaterSurfaceModel::MorphCoordinate(v.x, lo.x, hi.x, m_modernMeshMorph);
					v.y = WaterSurfaceModel::MorphCoordinate(v.y, lo.y, hi.y, m_modernMeshMorph);
					v.z = WaterSurfaceModel::MorphCoordinate(v.z, lo.z, hi.z, m_modernMeshMorph);
					v.u1 = WaterSurfaceModel::MorphCoordinate(v.u1, lo.u1, hi.u1, m_modernMeshMorph);
					v.v1 = WaterSurfaceModel::MorphCoordinate(v.v1, lo.v1, hi.v1, m_modernMeshMorph);
					v.u2 = WaterSurfaceModel::MorphCoordinate(v.u2, lo.u2, hi.u2, m_modernMeshMorph);
					v.v2 = WaterSurfaceModel::MorphCoordinate(v.v2, lo.v2, hi.v2, m_modernMeshMorph);
					v.diffuse = WaterSurfaceModel::MorphColour(v.diffuse, lo.diffuse, hi.diffuse, m_modernMeshMorph);
				}
				prepared[edge] = TRUE;
			};
			auto edgeSample = [&](Int edge, double parameter) {
				prepareEdge(edge);
				WaterSurfaceModel::Point a = corners[edge], b = corners[(edge + 1) % 4];
				if (a.x > b.x || (a.x == b.x && a.y > b.y)) {
					const WaterSurfaceModel::Point swap = a;
					a = b;
					b = swap;
				}
				const double ex = b.x - a.x, ey = b.y - a.y, len2 = ex * ex + ey * ey;
				Int i = 0;
				Real t = 0;
				for (; i < cells; ++i) {
					const VertexFormatXYZNDUV2 &lo = edgeVertices[edge][i], &hi = edgeVertices[edge][i + 1];
					const double lt = ((lo.x - a.x) * ex + (lo.y - a.y) * ey) / len2, ht = ((hi.x - a.x) * ex + (hi.y - a.y) * ey) / len2;
					if (parameter <= ht || i == cells - 1) {
						t = ht > lt ? Real((parameter - lt) / (ht - lt)) : 0;
						t = clamp(t, 0.0f, 1.0f);
						break;
					}
				}
				return interpolate(edgeVertices[edge][i], edgeVertices[edge][i + 1], t);
			};
			auto correctedVertex = [&](WaterProjectedMesh::Point p, const WaterProjectedMesh::BoundaryCollar &collar, Bool share = FALSE) {
				VertexFormatXYZNDUV2 result = makeBaseVertex({Real(p.x), Real(p.y), points[0].Z}, share);
				double correction = 0;
				for (Int i = 0; i < collar.count; ++i) {
					const VertexFormatXYZNDUV2 edge = edgeSample(collar.edge[i], collar.parameter[i]);
					correction += collar.weight[i] * (edge.z - result.z);
				}
				result.u1 = Real(correction);
				result.z += result.u1;
				return result;
			};
			for (size_t n = 0; n < projectedMesh.vertices.size(); ++n) {
				const WaterProjectedMesh::Vertex &p = projectedMesh.vertices[n];
				if (p.boundary < 0) {
					grid[n] = p.collar >= 0 ? correctedVertex(p.world,
						projectedMesh.collars[p.collar]) : makeBaseVertex({Real(p.world.x), Real(p.world.y), points[0].Z});
					if (p.transition >= 0) {
						const WaterProjectedMesh::CentreTransition &transition = projectedMesh.transitions[p.transition];
						VertexFormatXYZNDUV2 source[3];
						for (Int i = 0; i < 3; ++i) source[i] = correctedVertex(transition.source[i],
							WaterProjectedMesh::CollarAt(authored, transition.source[i]), TRUE);
						double values[3];
						for (Int i = 0; i < 3; ++i) values[i] = source[i].z;
						const Real height = Real(WaterProjectedMesh::InterpolateCentre(transition, grid[n].z, values));
						grid[n].u1 += height - grid[n].z;
						grid[n].z = height;
					}
					continue;
				}
				const Int edge = p.boundary;
				WaterSurfaceModel::Point a = corners[edge], b = corners[(edge + 1) % 4];
				if (a.x > b.x || (a.x == b.x && a.y > b.y)) {
					const WaterSurfaceModel::Point swap = a;
					a = b;
					b = swap;
				}
				prepareEdge(edge);
				grid[n] = p.knot >= 0 ? edgeVertices[edge][p.knot] : edgeSample(edge, p.edgeParameter);
				// The fragment receives the exact additional height, including coarse-edge morph.
				const VertexFormatXYZNDUV2 analytic = makeBaseVertex({grid[n].x, grid[n].y, points[0].Z}, TRUE);
				grid[n].u1 = grid[n].z - analytic.z;
				grid[n].u2 = analytic.u2;
				grid[n].diffuse = analytic.diffuse;
				grid[n].nx = grid[n].ny = 0;
				grid[n].nz = 1;
			}
		} else {
			for (Int j = 0; j <= cells; ++j) for (Int i = 0; i <= cells; ++i) {
				const Real u = (Real) i / cells, v = (Real) j / cells;
				const WaterSurfaceModel::Point boundary[4] = {edges[0][i], edges[1][j], edges[2][i], edges[3][j]};
				const WaterSurfaceModel::Point p = WaterSurfaceModel::PatchPoint(corners, boundary, u, v);
				const WaterSurfaceModel::Sample offshore = oceanContext ? WaterSurfaceModel::SampleOffshore(
				oceanContext->ocean.data(), m_modernHeightWidth, m_modernHeightHeight,
				p.x / MAP_XY_FACTOR + m_modernHeightBorder, p.y / MAP_XY_FACTOR + m_modernHeightBorder,
					MAP_XY_FACTOR) : WaterSurfaceModel::Sample{0, 0, 0};
				Real obstacleAttenuation = 1.0f;
				Real terrainDepth = 0;
				const Real damping = modernWaterDamping(p.x, p.y, p.z, offshore.height, river ? NULL : &obstacleAttenuation, &terrainDepth);
				// Broad water polygons also cover dry land. Those vertices cannot move, and evaluating
				// all wave modes there was needlessly expensive on maps with many shallow pools.
				WaterSurfaceModel::Sample wave = {0, 0, 0};
				if (damping > 0.0f) {
					if (offshore.height > 0.0f) wave.height = WaterSurfaceModel::EvaluateBlendedHeight(p.x, p.y, m_modernWaveTime, offshore);
					else wave = WaterSurfaceModel::EvaluateBlended(p.x, p.y, m_modernWaveTime, offshore);
				}
				vertex->x = p.x;
				vertex->y = p.y;
				vertex->z = p.z + damping * wave.height;
				// The flat modern shader uses only alpha for colour/shroud. PRELIT_DIFFUSE passes RGB
				// through unchanged, so red carries the obstacle profile independently of terrain.
				vertex->diffuse = river ? applyShroudToRiverDiffuse(shroud, p.x, p.y, diffuse) :
				WaterSurfaceModel::EncodeObstacleAttenuation(diffuse, obstacleAttenuation);
				vertex->u1 = river ? (1.0f - u) * 0.5f : 0.0f;
				vertex->v1 = river ? riverStart + (riverEnd - riverStart) * v - m_riverVOrigin : p.y / 150.0f;
				vertex->u2 = damping;
				vertex->v2 = p.z;
				if (!river) {
					vertex->nx = vertex->ny = 0.0f;
					vertex->nz = 1.0f;
				} else {
					const Real nx = -wave.dx * damping, ny = -wave.dy * damping, inv = 1.0f / sqrtf(1.0f + nx * nx + ny * ny);
					vertex->nx = nx * inv;
					vertex->ny = ny * inv;
					vertex->nz = inv;
				}
				++vertex;
			}
			if (m_modernMeshMorph > 0.0f) {
				for (Int j = 0; j <= cells; ++j) for (Int i = 0; i <= cells; ++i) {
					if (((i|j) & 1) == 0) continue; // Coarse vertices are unchanged and safe to reuse.
					const WaterSurfaceModel::CoarsePair pair = WaterSurfaceModel::CoarsePairForVertex(i, j, cells);
					const VertexFormatXYZNDUV2 &a = grid[pair.first], &b = grid[pair.second];
					VertexFormatXYZNDUV2 &v = grid[j * side + i];
					const Real amount = m_modernMeshMorph;
					v.x = WaterSurfaceModel::MorphCoordinate(v.x, a.x, b.x, amount);
					v.y = WaterSurfaceModel::MorphCoordinate(v.y, a.y, b.y, amount);
					v.z = WaterSurfaceModel::MorphCoordinate(v.z, a.z, b.z, amount);
					v.u1 = WaterSurfaceModel::MorphCoordinate(v.u1, a.u1, b.u1, amount);
					v.v1 = WaterSurfaceModel::MorphCoordinate(v.v1, a.v1, b.v1, amount);
					v.u2 = WaterSurfaceModel::MorphCoordinate(v.u2, a.u2, b.u2, amount);
					v.v2 = WaterSurfaceModel::MorphCoordinate(v.v2, a.v2, b.v2, amount);
					v.nx = WaterSurfaceModel::MorphCoordinate(v.nx, a.nx, b.nx, amount);
					v.ny = WaterSurfaceModel::MorphCoordinate(v.ny, a.ny, b.ny, amount);
					v.nz = WaterSurfaceModel::MorphCoordinate(v.nz, a.nz, b.nz, amount);
					v.diffuse = WaterSurfaceModel::MorphColour(v.diffuse, a.diffuse, b.diffuse, amount);
				}
			}
		}
	}

	Matrix3D transform(1);
	DX8Wrapper::Set_Transform(D3DTS_WORLD, transform);
	DX8Wrapper::Set_Vertex_Buffer(vertices);
	DX8Wrapper::Set_Texture(0, m_riverTexture);
	if (river) setupJbaWaterShader();
	else setupFlatWaterShader(TRUE, waveCage);
	RenderUInt32 cull;
	DX8Wrapper::_Get_D3D_Device()->GetRenderState(D3DRS_CULLMODE, &cull);
	DX8Wrapper::Set_DX8_Render_State(D3DRS_CULLMODE, D3DCULL_NONE);
	if (TheWaterTransparency->m_additiveBlend) DX8Wrapper::Set_DX8_Render_State(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
	const Bool meshDebug = wireframeForDebug;
	if (meshDebug) DX8Wrapper::Set_DX8_Render_State(D3DRS_FILLMODE, D3DFILL_WIREFRAME);
	WaterShoreOverlay::Quad shorePatch;
	shorePatch.level = points[0].Z;
	Bool planar = TRUE;
	for (Int i = 0; i < 4; ++i) {
		shorePatch.xy[i * 2] = points[i].X;
		shorePatch.xy[i * 2 + 1] = points[i].Y;
		planar = planar && points[i].Z == points[0].Z;
	}
	PosixWaterShorePatch(DX8Wrapper::_Get_D3D_Device(), !river && !meshDebug && planar ? &shorePatch : NULL);
	// Legacy dynamic index buffers store their count in 16 bits. Keep a shared vertex grid and
	// submit bounded row batches; even a 128-cell ocean patch cannot truncate its index count.
	if (projected) {
		for (size_t first = 0; first < projectedMesh.indices.size(); first += 65532) {
			const size_t count = min(size_t(65532), projectedMesh.indices.size() - first);
			DynamicIBAccessClass indices(BUFFER_TYPE_DYNAMIC_DX8, (Int) count);
			{
				DynamicIBAccessClass::WriteLockClass lock(&indices);
				memcpy(lock.Get_Index_Array(), projectedMesh.indices.data() + first, count * sizeof(UnsignedShort));
			}
			DX8Wrapper::Set_Index_Buffer(indices, 0);
			DX8Wrapper::Draw_Triangles(0, (Int) count / 3, 0, vertexCount);
		}
	} else {
		const Int batchRows = WaterSurfaceModel::IndexBatchRows(cells);
		for (Int row = 0; row < cells; row += batchRows) {
			const Int rows = min(batchRows, cells - row), triangles = rows * cells * 2;
			DynamicIBAccessClass indices(BUFFER_TYPE_DYNAMIC_DX8, triangles * 3);
			{
				DynamicIBAccessClass::WriteLockClass lock(&indices);
				UnsignedShort *index = lock.Get_Index_Array();
				for (Int j = row; j < row + rows; ++j) for (Int i = 0; i < cells; ++i) {
					const Int a = j * side + i, b = a + 1, c = a + side, d = c + 1;
					*index++ = a;
					*index++ = d;
					*index++ = c;
					*index++ = a;
					*index++ = b;
					*index++ = d;
				}
			}
			DX8Wrapper::Set_Index_Buffer(indices, 0);
			DX8Wrapper::Draw_Triangles(0, triangles, 0, vertexCount);
		}
	}
	PosixWaterShorePatch(DX8Wrapper::_Get_D3D_Device(), NULL);
	if (meshDebug) DX8Wrapper::Set_DX8_Render_State(D3DRS_FILLMODE, D3DFILL_SOLID);
	// Legacy water particles can be replaced only after this view has submitted the
	// modern surface for their hull's level. Cached or offscreen hulls alone are not enough.
	if (!river && !meshDebug) {
		m_modernFoamDrawFrame = WW3D::Get_Frame_Count();
		for (size_t i = 0; i < m_modernHulls.size(); ++i) if (m_modernHulls[i].data[0][2] == points[0].Z &&
			std::find(m_modernFoamDrawObjectIDs.begin(), m_modernFoamDrawObjectIDs.end(),
				m_modernHullObjectIDs[i]) == m_modernFoamDrawObjectIDs.end())
			m_modernFoamDrawObjectIDs.push_back(m_modernHullObjectIDs[i]);
	}
	DX8Wrapper::Set_Pixel_Shader(NULL);
	if (TheWaterTransparency->m_additiveBlend) DX8Wrapper::Set_DX8_Render_State(D3DRS_SRCBLEND, D3DBLEND_ONE);
	if (!river && TheTerrainRenderObject->getShroud()) {
		W3DShaderManager::resetShader(W3DShaderManager::ST_SHROUD_TEXTURE);
		DX8Wrapper::Set_DX8_Texture(3, NULL);
		DX8Wrapper::Set_DX8_Render_State(D3DRS_ZFUNC, D3DCMP_EQUAL);
	}
	DX8Wrapper::Set_DX8_Render_State(D3DRS_CULLMODE, cull);

#endif
}

//-------------------------------------------------------------------------------------------------
//Draw a many sided river polygon.
//-------------------------------------------------------------------------------------------------
#if ZH_MODERN_WATER
void WaterRenderObjClass::drawRiverWater(const WaterPolygonForDraw *pTrig)
#else
void WaterRenderObjClass::drawRiverWater(PolygonTrigger *pTrig)
#endif
{
	DX8Wrapper::Invalidate_Cached_Render_States();	///@todo: Figure out why rivers don't draw without reset of all states.

	Int rectangleCount = pTrig->getNumPoints()/2;
	rectangleCount--;

	Real bumpFactor = 5;
	static Bool doWobble = true;

	if (m_drawnPushed) return;
	m_drawingRiver = true;
#if ZH_MODERN_WATER
	if (m_useModernWater && m_riverWaterPixelShader) {
		Int inner=pTrig->getRiverStart(),outer=inner+1;
		if (inner<0 || inner>=pTrig->getNumPoints()-1) return;
		Vector3 previous[2];
		Real along=0;
		for (Int row=0;row<=rectangleCount;++row) {
			const ICoord3D &a=*pTrig->getPoint(outer),&b=*pTrig->getPoint(inner);
			Vector3 next[2]={Vector3(a.x,a.y,a.z),Vector3(b.x,b.y,b.z)};
			if (row) {
				const Real length=((next[0]-previous[0]).Length()+(next[1]-previous[1]).Length())*0.5f;
				const Real width=max((next[1]-next[0]).Length(),1.0f);
				Vector3 patch[4]={previous[0],previous[1],next[1],next[0]};
				drawModernWaterPatch(patch,TRUE,along,along+length/width);
				along+=length/width;
			}
			previous[0]=next[0]; previous[1]=next[1];
			outer=(outer+1)%pTrig->getNumPoints();
			inner=(inner+pTrig->getNumPoints()-1)%pTrig->getNumPoints();
		}
		return;
	}
#endif

	//allocate 2 triangles per side with 3 indices per triangle
	DynamicIBAccessClass ib_access(BUFFER_TYPE_DYNAMIC_DX8,(rectangleCount+1)*2*3);
	{
		DynamicIBAccessClass::WriteLockClass lockib(&ib_access);
 		UnsignedShort *curIb = lockib.Get_Index_Array();
		try {
		for (Int i=0; i<rectangleCount; i++)
		{
			//triangle 1
			curIb[0] = i*2;
			curIb[1] = i*2+1;
			curIb[2] = i*2+3;

			//triangle 2
			curIb[3] = i*2;
			curIb[4] = i*2+3;
			curIb[5] = i*2+2;

			curIb += 6;	//skip the 6 indices we just added.
		}
		IndexBufferExceptionFunc();
		} catch(...) {
			IndexBufferExceptionFunc();
		}
	}

	Real shadeR=TheWaterTransparency->m_standingWaterColor.red;
	Real shadeG=TheWaterTransparency->m_standingWaterColor.green;
	Real shadeB=TheWaterTransparency->m_standingWaterColor.blue;

	//If the water color is not overridden, use legacy lighting code.
	if ( shadeR==1.0f && shadeG==1.0f && shadeB==1.0f)
	{
		shadeR = TheGlobalData->m_terrainAmbient[0].red;
		shadeG = TheGlobalData->m_terrainAmbient[0].green;
		shadeB = TheGlobalData->m_terrainAmbient[0].blue;

		//Add in diffuse lighting from each terrain light
		for (Int lightIndex=0; lightIndex < TheGlobalData->m_numGlobalLights; lightIndex++)
		{
			if (-TheGlobalData->m_terrainLightPos[lightIndex].z > 0)
			{	shadeR += -TheGlobalData->m_terrainLightPos[lightIndex].z * TheGlobalData->m_terrainDiffuse[lightIndex].red;
				shadeG += -TheGlobalData->m_terrainLightPos[lightIndex].z * TheGlobalData->m_terrainDiffuse[lightIndex].green;
				shadeB += -TheGlobalData->m_terrainLightPos[lightIndex].z * TheGlobalData->m_terrainDiffuse[lightIndex].blue;
			}
		}

		//Get water material colors
		Real waterShadeR = (m_settings[m_tod].waterDiffuse & 0xff) / 255.0f;
		Real waterShadeG = ((m_settings[m_tod].waterDiffuse >> 8) & 0xff) / 255.0f;
		Real waterShadeB = ((m_settings[m_tod].waterDiffuse >> 16) & 0xff) / 255.0f;

		shadeR=shadeR*waterShadeR*255.0f;
		shadeG=shadeG*waterShadeG*255.0f;
		shadeB=shadeB*waterShadeB*255.0f;
	}
	else
	{
		shadeR=shadeR*255.0f;
		shadeG=shadeG*255.0f;
		shadeB=shadeB*255.0f;

		if (shadeR == 0 && shadeG == 0 && shadeB == 0)
		{	//special case where we disable lighting
			shadeR=255;
			shadeG=255;
			shadeB=255;
		}
	}

	Int diffuse=REAL_TO_INT(shadeB) | (REAL_TO_INT(shadeG) << 8) | (REAL_TO_INT(shadeR) << 16);

	//Keep diffuse from lighting calculations but substitute custom alpha
	diffuse |= m_settings[m_tod].waterDiffuse & 0xff000000;	//copy alpha/opacity from ini setting

	Int innerNdx = pTrig->getRiverStart();
	Int outerNdx = innerNdx+1;

	Real endLen=0;
	Real totalLen=0;
	Int i;
	for (i=0; i<pTrig->getNumPoints()-1; i++) {
		ICoord3D innerPt = *pTrig->getPoint(i);
		ICoord3D outerPt = *pTrig->getPoint(i+1);
		Real dx = innerPt.x-outerPt.x;
		Real dy = innerPt.y-outerPt.y;
		Real curLen = sqrt(dx*dx+dy*dy);
		totalLen += curLen;
		if ( i==innerNdx) {
			endLen = curLen;
		}
	}
	bumpFactor = endLen/BUMP_SIZE;

	Real lengthOfRiver = (totalLen/2)-endLen;
	Real repeatCount = lengthOfRiver / (endLen); 

	Real vScale=(Real)repeatCount/(Real)rectangleCount;

#define HEIGHT_TO_USE (0.5f)
	if (innerNdx >= pTrig->getNumPoints()-1) return;
	//allocate 2 vertices per side
	DynamicVBAccessClass vb_access(BUFFER_TYPE_DYNAMIC_DX8,dynamic_fvf_type,(rectangleCount+1)*2);
	{
		DynamicVBAccessClass::WriteLockClass lock(&vb_access);
		VertexFormatXYZNDUV2* vb=lock.Get_Formatted_Vertex_Array();

		Real constA=3*m_riverVOrigin;

		W3DShroud *shroud = TheTerrainRenderObject ? TheTerrainRenderObject->getShroud() : NULL;

		for (i=0; i<(pTrig->getNumPoints()/2); i++)
		{
			Real x,y;
			ICoord3D innerPt = *pTrig->getPoint(outerNdx);
			ICoord3D outerPt = *pTrig->getPoint(innerNdx);
			outerNdx++;
			innerNdx--;
			if (innerNdx<0) {
				innerNdx = pTrig->getNumPoints()-1;
			}
			if (outerNdx >= pTrig->getNumPoints()) {
				outerNdx = 0;
			}
			x=innerPt.x;
			y=innerPt.y;

			vb->x=x;
			vb->y=y;

			vb->z=innerPt.z;
			vb->diffuse= applyShroudToRiverDiffuse( shroud, x, y, diffuse );

			Real wobbleConst=-m_riverVOrigin+vScale*(Real)i + WWMath::Fast_Sin(2*PI*(vScale*(Real)i) - constA)/22.0f;
 			//old slower version
			//vb->v1=-m_riverVOrigin+vScale*(Real)i + wobble(vScale*i, m_riverVOrigin, doWobble);
			vb->v1=wobbleConst;
			vb->u1=HEIGHT_TO_USE ;
			//old slower version
			//vb->v2 = -m_riverVOrigin+vScale*(Real)i + wobble(vScale*i, m_riverVOrigin, doWobble);
			vb->v2=wobbleConst;
			vb->u2 = 1.0f;
			vb->nx = 0;
			vb->ny = 0;
			vb->nz = 1.0f;
			vb++;

			x=outerPt.x;
			y=outerPt.y;

			vb->x=x;
			vb->y=y;
			vb->z=outerPt.z;
			vb->diffuse= applyShroudToRiverDiffuse( shroud, x, y, diffuse );
 			//old slower version
			//vb->v1=-m_riverVOrigin+vScale*(Real)i + wobble(vScale*i, m_riverVOrigin, doWobble);
			vb->v1=wobbleConst;
			vb->u1=0;
			//old slower version
 			//vb->v2 = -m_riverVOrigin+vScale*(Real)i + wobble(vScale*i, m_riverVOrigin, doWobble);
			vb->v2 =wobbleConst;
			vb->u2 = 0;
			vb->nx = 0;
			vb->ny = 0;
			vb->nz = 1.0f;
			vb++;

		}
	}

	Matrix3D tm(1);

	DX8Wrapper::Set_Transform(D3DTS_WORLD,tm);	//position the water surface
	DX8Wrapper::Set_Index_Buffer(ib_access,0);
	DX8Wrapper::Set_Vertex_Buffer(vb_access);
	DX8Wrapper::Set_Texture(0,m_riverTexture);	//set to blue

	const unsigned oldTextureFactor = DX8Wrapper::Get_DX8_Render_State(D3DRS_TEXTUREFACTOR);	//the foam borrows it
	setupJbaWaterShader();

	//In additive blending we need to use the alpha at the edges of river to darken
	//rgb instead.
	if (TheWaterTransparency->m_additiveBlend)
		DX8Wrapper::Set_DX8_Render_State(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA );

	if (m_riverWaterPixelShader) DX8Wrapper::Set_Pixel_Shader(waterFoam() ? m_riverWaterFoamPixelShader : m_riverWaterPixelShader);
 	RenderUInt32 cull;
	DX8Wrapper::_Get_D3D_Device()->GetRenderState(D3DRS_CULLMODE, &cull);
	DX8Wrapper::Set_DX8_Render_State(D3DRS_CULLMODE, D3DCULL_NONE);


	if (wireframeForDebug) {
		DX8Wrapper::Set_DX8_Render_State(D3DRS_FILLMODE,D3DFILL_WIREFRAME);
	}
	DX8Wrapper::Draw_Triangles(	0,rectangleCount*2, 0,	(rectangleCount+1)*2);
	if (wireframeForDebug) {
		DX8Wrapper::Set_DX8_Render_State(D3DRS_FILLMODE,D3DFILL_SOLID);
	}

	if (m_riverWaterPixelShader) DX8Wrapper::Set_Pixel_Shader(NULL);
	if (waterFoam())
		DX8Wrapper::Set_DX8_Render_State(D3DRS_TEXTUREFACTOR, oldTextureFactor);

	//restore blend mode to what W3D expects.
	if (TheWaterTransparency->m_additiveBlend)
		DX8Wrapper::Set_DX8_Render_State(D3DRS_SRCBLEND, D3DBLEND_ONE );

	drawReflection(rectangleCount*2, (rectangleCount+1)*2);

	// the shroud is in the vertex colour now - see applyShroudToRiverDiffuse.  The second pass that
	// used to apply it here darkened the banks twice.
	DX8Wrapper::Set_DX8_Render_State(D3DRS_CULLMODE, cull);

}

//-------------------------------------------------------------------------------------------------
/** Lays the reflection over water geometry still bound from the draw before.  The texture was
	* rendered from a camera mirrored in the water plane, and a point on that plane lands on the
	* same screen position in both views, so the ordinary camera's projection is the whole lookup. */
//-------------------------------------------------------------------------------------------------
void WaterRenderObjClass::drawReflection(Int triangleCount, Int vertexCount)
{
#if ZH_MODERN_WATER
	if (m_useModernWater) return; // Modern water samples the mirror in its surface shader.
#endif
	if (!m_pReflectionTexture || !m_reflectionPixelShader)
		return;	//the card refused a render target or a pixel shader

	// multiplied into the water, so it keeps the colour it was drawn with and the shroud already on it
	DX8Wrapper::Set_Shader(ShaderClass::_PresetMultiplicativeShader);
	DX8Wrapper::Set_Texture(0, NULL);
	DX8Wrapper::Apply_Render_State_Changes();

	// camera space -> clip space -> 0..1 texture space, divided by w in the projected stage
	D3DXMATRIX projection;
	DX8Wrapper::_Get_DX8_Transform(D3DTS_PROJECTION, *(Matrix4x4*)&projection);
	D3DXMATRIX clipToTexture;
	memset(&clipToTexture, 0, sizeof(D3DXMATRIX));
	clipToTexture._11 = 0.5f;
	clipToTexture._22 = -0.5f;
	clipToTexture._41 = 0.5f;
	clipToTexture._42 = 0.5f;
	clipToTexture._43 = 1.0f;
	// then from the whole screen onto the slice of it the texture was rendered for, still before
	// the divide, so the offsets are scaled by w
	const Real areaWidth = m_reflectionAreaMax.X - m_reflectionAreaMin.X;
	const Real areaHeight = m_reflectionAreaMax.Y - m_reflectionAreaMin.Y;
	D3DXMATRIX screenToArea;
	D3DXMatrixIdentity(&screenToArea);
	screenToArea._11 = 2.0f / areaWidth;
	screenToArea._31 = -(1.0f + m_reflectionAreaMin.X) / areaWidth;
	screenToArea._22 = 2.0f / areaHeight;
	screenToArea._32 = (m_reflectionAreaMax.Y - 1.0f) / areaHeight;
	D3DXMATRIX viewToTexture = projection * clipToTexture * screenToArea;
	DX8Wrapper::_Set_DX8_Transform(D3DTS_TEXTURE0, *(Matrix4x4*)&viewToTexture);

	DX8Wrapper::Set_DX8_Texture(0, m_pReflectionTexture->Peek_D3D_Texture());
	DX8Wrapper::Set_DX8_Texture_Stage_State(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
	DX8Wrapper::Set_DX8_Texture_Stage_State(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
	DX8Wrapper::Set_DX8_Texture_Stage_State(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
	DX8Wrapper::Set_DX8_Texture_Stage_State(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
	DX8Wrapper::Set_DX8_Texture_Stage_State(0, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
	DX8Wrapper::Set_DX8_Texture_Stage_State(0, D3DTSS_TEXCOORDINDEX, D3DTSS_TCI_CAMERASPACEPOSITION);
	DX8Wrapper::Set_DX8_Texture_Stage_State(0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_COUNT3 | D3DTTFF_PROJECTED);

	// D3D9 reads the strength from c0 and the D3D11 transcription from the texture factor's alpha
	DX8Wrapper::_Get_D3D_Device()->SetPixelShaderConstantF(0, D3DXVECTOR4(WATER_REFLECTION_STRENGTH, WATER_REFLECTION_STRENGTH, WATER_REFLECTION_STRENGTH, WATER_REFLECTION_STRENGTH), 1);
	DX8Wrapper::Set_DX8_Render_State(D3DRS_TEXTUREFACTOR, REAL_TO_INT(WATER_REFLECTION_STRENGTH * 255.0f) << 24);
	DX8Wrapper::Set_Pixel_Shader(m_reflectionPixelShader);
	DX8Wrapper::Set_DX8_Render_State(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
	DX8Wrapper::Set_DX8_Render_State(D3DRS_CULLMODE, D3DCULL_NONE);

	DX8Wrapper::Draw_Triangles(0, triangleCount, 0, vertexCount);

	DX8Wrapper::Set_Pixel_Shader(NULL);
	DX8Wrapper::Set_DX8_Texture_Stage_State(0, D3DTSS_TEXCOORDINDEX, D3DTSS_TCI_PASSTHRU|0);
	DX8Wrapper::Set_DX8_Texture_Stage_State(0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
	DX8Wrapper::Set_DX8_Texture(0, NULL);
	ShaderClass::Invalidate();	//everything above was set behind the shader's back
}

void WaterRenderObjClass::setupFlatWaterShader(Bool heightCorrection,Bool waveCage)
{

	DX8Wrapper::Set_Texture(0,m_riverTexture);
	if (!TheWaterTransparency->m_additiveBlend)
		DX8Wrapper::Set_Shader(ShaderClass::_PresetAlphaShader);
	else
		DX8Wrapper::Set_Shader(ShaderClass::_PresetAdditiveShader);

	VertexMaterialClass *vmat=VertexMaterialClass::Get_Preset(VertexMaterialClass::PRELIT_DIFFUSE);
	DX8Wrapper::Set_Material(vmat);
	REF_PTR_RELEASE(vmat);
	m_riverTexture->Get_Filter().Set_Mag_Filter(TextureFilterClass::FILTER_TYPE_BEST);
	m_riverTexture->Get_Filter().Set_Min_Filter(TextureFilterClass::FILTER_TYPE_BEST);
	m_riverTexture->Get_Filter().Set_Mip_Mapping(TextureFilterClass::FILTER_TYPE_BEST);

	DX8Wrapper::Apply_Render_State_Changes();	//force update of view and projection matrices

	//Setup shroud to render in same pass as water
	if (m_trapezoidWaterPixelShader)
	{	if (TheTerrainRenderObject->getShroud())
		{
			W3DShaderManager::setTexture(0,TheTerrainRenderObject->getShroud()->getShroudTexture());
			//Use stage 3 to apply the shroud
			W3DShaderManager::setShader(W3DShaderManager::ST_SHROUD_TEXTURE, 3);
			//Shroud shader uses z-compare of EQUAL which wouldn't work on water because it doesn't
			//write to the zbuffer.  Change to LESSEQUAL.
			DX8Wrapper::Set_DX8_Render_State(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
		}
		else
		{	//Assume no shroud, so stage 3 will be "NULL" texture but using actual white because
			//pixel shader on GF4 generates random colors with SetTexture(3,NULL).
			if (!m_whiteTexture->Is_Initialized())
			{	m_whiteTexture->Init();
				SurfaceClass *surface=m_whiteTexture->Get_Surface_Level();
				surface->DrawPixel(0,0,0xffffffff);
				REF_PTR_RELEASE(surface);
			}
			DX8Wrapper::Set_DX8_Texture(3,m_whiteTexture->Peek_D3D_Texture());	
		}
	}

	DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_ALPHAOP,   D3DTOP_ADD );
	DX8Wrapper::Set_DX8_Texture_Stage_State(0,  D3DTSS_TEXCOORDINDEX, 0);
	DX8Wrapper::Set_DX8_Texture_Stage_State(1,  D3DTSS_TEXCOORDINDEX, 0);

	Bool doSparkles = true;

	if (m_trapezoidWaterPixelShader && doSparkles) {

		if (!m_waterSparklesTexture->Is_Initialized())
			m_waterSparklesTexture->Init();

		DX8Wrapper::Set_DX8_Texture(1,m_waterSparklesTexture->Peek_D3D_Texture());	

		if (!m_waterNoiseTexture->Is_Initialized())
			m_waterNoiseTexture->Init();

		DX8Wrapper::Set_DX8_Texture(2,m_waterNoiseTexture->Peek_D3D_Texture());	

		DX8Wrapper::Set_DX8_Texture_Stage_State(1,  D3DSAMP_ADDRESSU, D3DTADDRESS_WRAP);
		DX8Wrapper::Set_DX8_Texture_Stage_State(1,  D3DSAMP_ADDRESSV, D3DTADDRESS_WRAP);

		DX8Wrapper::Set_DX8_Texture_Stage_State(2,  D3DTSS_TEXCOORDINDEX, D3DTSS_TCI_CAMERASPACEPOSITION);
		// Two output coordinates are used.
		DX8Wrapper::Set_DX8_Texture_Stage_State(2,  D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_COUNT2);	
		DX8Wrapper::Set_DX8_Texture_Stage_State(2,  D3DSAMP_ADDRESSU, D3DTADDRESS_WRAP);
		DX8Wrapper::Set_DX8_Texture_Stage_State(2,  D3DSAMP_ADDRESSV, D3DTADDRESS_WRAP);
		D3DXMATRIX inv;
		float det;

		Matrix4x4 curView;
		DX8Wrapper::_Get_DX8_Transform(D3DTS_VIEW, curView);
		D3DXMatrixInverse(&inv, &det, (D3DXMATRIX*)&curView);
		D3DXMATRIX scale;

		D3DXMatrixScaling(&scale, NOISE_REPEAT_FACTOR, NOISE_REPEAT_FACTOR,1);
		D3DXMATRIX destMatrix = inv * scale;
		D3DXMatrixTranslation(&scale, m_riverVOrigin, m_riverVOrigin,0);
		destMatrix = destMatrix*scale;
		DX8Wrapper::_Set_DX8_Transform(D3DTS_TEXTURE2, *(Matrix4x4*)&destMatrix);

	}
	DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR );
	DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR );
	DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DSAMP_MINFILTER, D3DTEXF_LINEAR );
	DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR );
	DX8Wrapper::Set_DX8_Texture_Stage_State( 2, D3DSAMP_MINFILTER, D3DTEXF_LINEAR );
	DX8Wrapper::Set_DX8_Texture_Stage_State( 2, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR );
	if (m_trapezoidWaterPixelShader){
		DX8Wrapper::_Get_D3D_Device()->SetPixelShaderConstantF(0,   D3DXVECTOR4(REFLECTION_FACTOR, REFLECTION_FACTOR, REFLECTION_FACTOR, 1.0f), 1);
		DX8Wrapper::Set_Pixel_Shader(m_trapezoidWaterPixelShader);
		if (waterFoam()) {
			// unfeathered unless drawTrapezoidWater hands over the depth curve
			DX8Wrapper::Set_DX8_Render_State(D3DRS_TEXTUREFACTOR, waterFoamFactor(m_riverVOrigin, 0.0f, 0.0f));
			DX8Wrapper::Set_Pixel_Shader(m_trapezoidWaterFoamPixelShader);
		}
		setupModernWaterShader(FALSE,heightCorrection,waveCage);
	}
}

//-------------------------------------------------------------------------------------------------
//Draw a 4 sided flat water area.
//-------------------------------------------------------------------------------------------------
void WaterRenderObjClass::drawTrapezoidWater(Vector3 points[4])
{
#if ZH_MODERN_WATER
	if (m_useModernWater && m_trapezoidWaterPixelShader) { drawModernWaterPatch(points,FALSE); return; }
#endif
	Vector3 origin(points[0]);
	Vector3 uVec1(points[1]);
	Vector3 vVec1(points[3]);
	Vector3 uVec2(points[2]);
	Vector3 vVec2(points[2]);
	uVec2 -= vVec1;
	vVec2	-= uVec1;
	uVec1 -= origin;
	vVec1 -= origin;
	Int uCount = (uVec1.Length()+uVec2.Length()) / (8*MAP_XY_FACTOR);
	if (uCount<1) uCount = 1;
	Int vCount = (vVec1.Length()+vVec2.Length()) / (8*MAP_XY_FACTOR);
	if (vCount<1) vCount = 1;	

	if (uCount>50) uCount = 50;
	if (vCount>50) vCount = 50;

	static Bool doWobble = true;

	Int rectangleCount = uCount*vCount;

	uCount++;
	vCount++;

	Int i, j;
	//allocate 2 triangles per side with 3 indices per triangle
	DynamicIBAccessClass ib_access(BUFFER_TYPE_DYNAMIC_DX8,(rectangleCount+1)*2*3);
	{
		DynamicIBAccessClass::WriteLockClass lockib(&ib_access);
 		UnsignedShort *curIb = lockib.Get_Index_Array();
		try {
		for (j=0; j<vCount-1; j++)
		{	for (i=0; i<uCount-1; i++)
			{
				//triangle 1
				curIb[0] = (j)*uCount + i;
				curIb[1] = (j+1)*uCount + i+1;
				curIb[2] = (j+1)*uCount + i;

				//triangle 2
				curIb[3] = (j)*uCount + i;
				curIb[4] = (j)*uCount + i+1;
				curIb[5] = (j+1)*uCount + i+1;

				curIb += 6;	//skip the 6 indices we just added.
			}
		}
		IndexBufferExceptionFunc();
		} catch(...) {
			IndexBufferExceptionFunc();
		}
	}

	Real	waterFactor=150;
	Real shadeR=TheWaterTransparency->m_standingWaterColor.red;
	Real shadeG=TheWaterTransparency->m_standingWaterColor.green;
	Real shadeB=TheWaterTransparency->m_standingWaterColor.blue;

	//If the water color is not overridden, use legacy lighting code.
	if ( shadeR==1.0f && shadeG==1.0f && shadeB==1.0f)
	{
		shadeR = TheGlobalData->m_terrainAmbient[0].red;
		shadeG = TheGlobalData->m_terrainAmbient[0].green;
		shadeB = TheGlobalData->m_terrainAmbient[0].blue;

		//Add in diffuse lighting from each terrain light
		for (Int lightIndex=0; lightIndex < TheGlobalData->m_numGlobalLights; lightIndex++)
		{
			if (-TheGlobalData->m_terrainLightPos[lightIndex].z > 0)
			{	shadeR += -TheGlobalData->m_terrainLightPos[lightIndex].z * TheGlobalData->m_terrainDiffuse[lightIndex].red;
				shadeG += -TheGlobalData->m_terrainLightPos[lightIndex].z * TheGlobalData->m_terrainDiffuse[lightIndex].green;
				shadeB += -TheGlobalData->m_terrainLightPos[lightIndex].z * TheGlobalData->m_terrainDiffuse[lightIndex].blue;
			}
		}

		//Get water material colors
		Real waterShadeR = (m_settings[m_tod].waterDiffuse & 0xff) / 255.0f;
		Real waterShadeG = ((m_settings[m_tod].waterDiffuse >> 8) & 0xff) / 255.0f;
		Real waterShadeB = ((m_settings[m_tod].waterDiffuse >> 16) & 0xff) / 255.0f;

		shadeR=shadeR*waterShadeR*255.0f;
		shadeG=shadeG*waterShadeG*255.0f;
		shadeB=shadeB*waterShadeB*255.0f;
	}
	else
	{
		shadeR=shadeR*255.0f;
		shadeG=shadeG*255.0f;
		shadeB=shadeB*255.0f;

		if (shadeR == 0 && shadeG == 0 && shadeB == 0)
		{	//special case where we disable lighting
			shadeR=255;
			shadeG=255;
			shadeB=255;
		}
	}

	Int diffuse=REAL_TO_INT(shadeB) | (REAL_TO_INT(shadeG) << 8) | (REAL_TO_INT(shadeR) << 16);

	//Keep diffuse from lighting calculations but substitute custom alpha
	diffuse |= m_settings[m_tod].waterDiffuse & 0xff000000;	//copy alpha/opacity from ini setting

	DynamicVBAccessClass vb_access(BUFFER_TYPE_DYNAMIC_DX8,dynamic_fvf_type,(rectangleCount+1)*2);

//#define WAVY_WATER
//#define FEATHER_LAYER_COUNT (3) //LORENZEN
//#define FEATHER_LAYER_THICKNESS (2.5f)
//#define FEATHER_WATER

//#ifdef WAVY_WATER // the NEW WATER a'la LORENZEN
	if ( TheGlobalData->m_featherWater )
	{

		DynamicVBAccessClass::WriteLockClass lock(&vb_access);
		VertexFormatXYZNDUV2* vb=lock.Get_Formatted_Vertex_Array();

		Real phase = 0;
		Real mapCoeff = PI/(4*MAP_XY_FACTOR);
		Real wave = 0;
		Real amplitude = 0.5f;

		//The first (high order) byte is the Alpha value for this patch
		// It needs to be set proportional to the number of feather layers
		// this comes from TheGlobalData->m_featherWater, which is a count of layers

		Int Alpha = 0;
		if ( TheGlobalData->m_featherWater == 5) Alpha = 80;
		if ( TheGlobalData->m_featherWater == 4) Alpha = 110;
		if ( TheGlobalData->m_featherWater == 3) Alpha = 140;
		if ( TheGlobalData->m_featherWater == 2) Alpha = 200;
		if ( TheGlobalData->m_featherWater == 1) Alpha = 255;

		//Keep diffuse from lighting calculations but substitute custom alpha
		Int customDiffuse = (diffuse & 0x00ffffff) | (Alpha<< 24);//(0x80 << 16)|(0x90 << 8)|0xa0;

		for (j=0; j<vCount; j++)
		{
			Real dv = j;
			dv /= (vCount-1);
			for (i=0; i<uCount; i++)
			{
				Real du = i;
				du /= (uCount-1);
				Vector3 vertex = origin;
				vertex += uVec1*du;
				vertex += vVec1*dv;
				vertex += (dv)*(du)*(vVec2-vVec1);

				vb->x=vertex.X;
				vb->y=vertex.Y;

				// common to all the waving effects
				phase = 25 * m_riverVOrigin + vertex.X * mapCoeff;
				wave = (sin(phase) - 1.0f) * amplitude;

				vb->z = (vertex.Z + wave);
				vb->diffuse = customDiffuse;
				vb->u1 = (vertex.X/waterFactor) + 0.02*cos(11*m_riverVOrigin)*wave;
				vb->v1 = (vertex.Y/waterFactor) + 0.02*cos(5*m_riverVOrigin)*wave;
				vb->u2 = vertex.X/BUMP_SIZE;
				vb->v2 = vertex.Y/BUMP_SIZE + 0.3f*vertex.X/BUMP_SIZE;
				vb->nx = 0;
				vb->ny = 0;
				vb->nz = 1.0f;
				vb++;
			}
		}
	}
//#else // STILL THE OLD FLAT WATER
	else

	{
		DynamicVBAccessClass::WriteLockClass lock(&vb_access);
		VertexFormatXYZNDUV2* vb=lock.Get_Formatted_Vertex_Array();

		//Pulling some constants out of the inner loops to improve performance -MW
		Real constA=0.02*cos(11*m_riverVOrigin);
		Real constB=0.02*cos(5*m_riverVOrigin);
		Real constC=25*m_riverVOrigin;
		Real ooWaterFactor = 1.0f/waterFactor;
		const Real constD=PI/(4*MAP_XY_FACTOR);
		Real constE=1.0f/(Real)(vCount-1);
		Real constF=1.0f/(Real)(uCount-1);

		for (j=0; j<vCount; j++)
		{
			Real dv = (Real)j * constE;

			for (i=0; i<uCount; i++)
			{
				Real du = (Real)i * constF;
				Vector3 vertex = origin;
				vertex += uVec1*du;
				vertex += vVec1*dv;
				vertex += (dv)*(du)*(vVec2-vVec1);

				vb->x=vertex.X;
				vb->y=vertex.Y;
				vb->z=vertex.Z;

				vb->diffuse= diffuse;
				//Old slower version
 				//vb->u1=(vertex.X/waterFactor) + 0.02*cos(11*m_riverVOrigin)*sin(25*m_riverVOrigin+vertex.X*PI/(4*MAP_XY_FACTOR));
 				//vb->v1=(vertex.Y/waterFactor) + 0.02*cos(5*m_riverVOrigin)*sin(25*m_riverVOrigin+vertex.Y*PI/(4*MAP_XY_FACTOR));
				vb->u1=vertex.X*ooWaterFactor + constA*WWMath::Fast_Sin(constC+vertex.X*constD);
				vb->v1=vertex.Y*ooWaterFactor + constB*WWMath::Fast_Sin(constC+vertex.Y*constD);
				vb->u2 = vertex.X/BUMP_SIZE;
				//Old slower version
 				//vb->v2 = vertex.Y/BUMP_SIZE + 0.3f*vertex.X/BUMP_SIZE;
				vb->v2 = (vertex.Y+0.3f*vertex.X)/BUMP_SIZE;
				vb->nx = 0;
				vb->ny = 0;
				vb->nz = 1.0f;
				vb++;
			}
		}
	}

//#endif // OLD VS NEW WATER


	Matrix3D tm(1);

	DX8Wrapper::Set_Transform(D3DTS_WORLD,tm);	//position the water surface
	DX8Wrapper::Set_Index_Buffer(ib_access,0);
	DX8Wrapper::Set_Vertex_Buffer(vb_access);

	const unsigned oldTextureFactor = DX8Wrapper::Get_DX8_Render_State(D3DRS_TEXTUREFACTOR);	//the foam borrows it
	setupFlatWaterShader();// lorenzen sez use the alpha shader

	//If video card supports it and it's enabled, feather the water edge using destination alpha
	if (DX8Wrapper::getBackBufferFormat() == WW3D_FORMAT_A8R8G8B8 && TheGlobalData->m_showSoftWaterEdge && TheWaterTransparency->m_transparentWaterDepth !=0
		&& waterFoam() && !TheWaterTransparency->m_additiveBlend)
	{	// The foam program feathers per pixel instead, by the same curve the shoreline tiles write into
		// the frame's alpha (BaseHeightMapRenderObjClass::updateShorelineTile): the depth over
		// TransparentWaterDepth * TransparentWaterMinOpacity, held at TransparentWaterMinOpacity.
		// The source alpha has to stay the blend factor, or the shallow foam would fade out with it.
		DX8Wrapper::Set_DX8_Render_State(D3DRS_TEXTUREFACTOR, waterFoamFactor(m_riverVOrigin,
			TheWaterTransparency->m_transparentWaterDepth * TheWaterTransparency->m_minWaterOpacity,
			TheWaterTransparency->m_minWaterOpacity));
	}
	else if (DX8Wrapper::getBackBufferFormat() == WW3D_FORMAT_A8R8G8B8 && TheGlobalData->m_showSoftWaterEdge && TheWaterTransparency->m_transparentWaterDepth !=0)
	{		DX8Wrapper::Set_DX8_Render_State(D3DRS_SRCBLEND, D3DBLEND_DESTALPHA );
			if (!TheWaterTransparency->m_additiveBlend)
				DX8Wrapper::Set_DX8_Render_State(D3DRS_DESTBLEND, D3DBLEND_INVDESTALPHA );
	}

 	RenderUInt32 cull;
	DX8Wrapper::_Get_D3D_Device()->GetRenderState(D3DRS_CULLMODE, &cull);
	DX8Wrapper::Set_DX8_Render_State(D3DRS_CULLMODE, D3DCULL_NONE);


//#ifdef FEATHER_WATER // the NEW WATER a'la LORENZEN

//	int layer = 0;//LORENZEN
//	for (layer = 0; layer < FEATHER_LAYER_COUNT; ++layer)//LORENZEN
//#endif // FEATHER_WATER
	{
//#ifdef WAVY_WATER // the NEW WATER a'la LORENZEN

		//increment the depth of the water's surface for every vert in the buffer
//#ifdef  FEATHER_WATER
//		VertexFormatXYZNDUV2 *vertBuf = vertexBufferStart;
//		while (vertBuf < vertexBufferStart + vCount * uCount)
//		{
//			vertBuf->z *= FEATHER_LAYER_THICKNESS;
//			++vertBuf;
//		}
//#endif // FEATHER_WATER
//#endif //WAVY_WATER
		DX8Wrapper::Draw_Triangles(	0,rectangleCount*2, 0,	(rectangleCount+1)*2);//lorenzen thinks this is where to itereate the soft shoreline effect
	}



	if (false) {
		DX8Wrapper::Set_DX8_Render_State(D3DRS_FILLMODE,D3DFILL_WIREFRAME);
		DX8Wrapper::Set_DX8_Render_State(D3DRS_ALPHABLENDENABLE , false);
		DX8Wrapper::Draw_Triangles(	0,rectangleCount*2, 0,	(rectangleCount+1)*2);
		DX8Wrapper::Set_DX8_Render_State(D3DRS_ALPHABLENDENABLE , true);
		DX8Wrapper::Set_DX8_Render_State(D3DRS_FILLMODE,D3DFILL_SOLID);
	}

	if (m_riverWaterPixelShader) DX8Wrapper::Set_Pixel_Shader(NULL);
	if (waterFoam())
		DX8Wrapper::Set_DX8_Render_State(D3DRS_TEXTUREFACTOR, oldTextureFactor);
	//Restore alpha blend to default values since we may have changed them to feather edges.
	if (!TheWaterTransparency->m_additiveBlend)
	{	DX8Wrapper::Set_DX8_Render_State(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA );
		DX8Wrapper::Set_DX8_Render_State(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA );
	}
	else
	{
		DX8Wrapper::Set_DX8_Render_State(D3DRS_SRCBLEND, D3DBLEND_ONE );
		DX8Wrapper::Set_DX8_Render_State(D3DRS_DESTBLEND, D3DBLEND_ONE );
	}

	if (TheTerrainRenderObject->getShroud())
	{
		if (m_trapezoidWaterPixelShader)
		{	//shroud was applied in stage3 of main pass so just need to restore state here.
			W3DShaderManager::resetShader(W3DShaderManager::ST_SHROUD_TEXTURE);
			DX8Wrapper::Set_DX8_Texture(3,NULL);	//free possible reference to shroud texture
			DX8Wrapper::Set_DX8_Render_State(D3DRS_ZFUNC, D3DCMP_EQUAL);
		}
		else
		{	//do second pass to apply the shroud on water plane for cards that can't do it in main pass.
			W3DShaderManager::setTexture(0,TheTerrainRenderObject->getShroud()->getShroudTexture());
			W3DShaderManager::setShader(W3DShaderManager::ST_SHROUD_TEXTURE, 0);
			DX8Wrapper::Set_DX8_Render_State(D3DRS_CULLMODE, D3DCULL_NONE);
			//Shroud shader uses z-compare of EQUAL which wouldn't work on water because it doesn't
			//write to the zbuffer.  Change to LESSEQUAL.
			DX8Wrapper::Set_DX8_Render_State(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
			DX8Wrapper::Draw_Triangles(	0,rectangleCount*2, 0,	(rectangleCount+1)*2);
			DX8Wrapper::Set_DX8_Render_State(D3DRS_ZFUNC, D3DCMP_EQUAL);
			W3DShaderManager::resetShader(W3DShaderManager::ST_SHROUD_TEXTURE);
		}
	}

	drawReflection(rectangleCount*2, (rectangleCount+1)*2);

	DX8Wrapper::Set_DX8_Render_State(D3DRS_CULLMODE, cull);
}


//-------------------------------------------------------------------------------------------------
//debug version where moon rotates with the camera	(always upright on screen)
//-------------------------------------------------------------------------------------------------
#if 0
void WaterRenderObjClass::renderSkyBody(Matrix3D *mat)
{	
	Vector3 vRight,vUp,V0,V1,V2,V3;

	mat->Get_X_Vector(&vRight);
	mat->Get_Y_Vector(&vUp);

	//calculate offsets from quad center to each of the 4 corners
	//	0-----1
	//  |    /|
	//  |  /  |
	//	|/    |
	//  3-----2
	V0=-vRight+vUp;
	V2=vRight+vUp;
	V2=vRight-vUp;
	V3=-vRight-vUp;

	VertexMaterialClass *vmat=VertexMaterialClass::Get_Preset(VertexMaterialClass::PRELIT_DIFFUSE);
	DX8Wrapper::Set_Material(vmat);
	REF_PTR_RELEASE(vmat);
	DX8Wrapper::Set_Shader(ShaderClass::/*_PresetAdditiveShader*//*_PresetOpaqueShader*/_PresetAlphaShader);
//	DX8Wrapper::Set_Texture(0,setting->skyBodyTexture);

	DX8Wrapper::Set_Texture(0,m_alphaClippingTexture);

	//draw an infinite sky plane
	DynamicVBAccessClass vb_access(BUFFER_TYPE_DYNAMIC_DX8,4);
	{
		DynamicVBAccessClass::WriteLockClass lock(&vb_access);
		VertexFormatXYZNDUV2* verts=lock.Get_Formatted_Vertex_Array();
		if(verts)
		{
			verts[0].x=SKYBODY_SIZE*V0.X;
			verts[0].y=SKYBODY_SIZE*V0.Y;
			verts[0].z=SKYBODY_SIZE*V0.Z;
			verts[0].u2=0;
			verts[0].v2=1;
			verts[0].diffuse=0xffffffff;

			verts[1].x=SKYBODY_SIZE*V1.X;
			verts[1].y=SKYBODY_SIZE*V1.Y;
			verts[1].z=SKYBODY_SIZE*V1.Z;
			verts[1].u2=1;
			verts[1].v2=1;
			verts[1].diffuse=0xffffffff;

			verts[2].x=SKYBODY_SIZE*V2.X;
			verts[2].y=SKYBODY_SIZE*V2.Y;
			verts[2].z=SKYBODY_SIZE*V2.Z;
			verts[2].u2=1;
			verts[2].v2=0;
			verts[2].diffuse=0xffffffff;

			verts[3].x=SKYBODY_SIZE*V3.X;
			verts[3].y=SKYBODY_SIZE*V3.Y;
			verts[3].z=SKYBODY_SIZE*V3.Z;
			verts[3].u2=0;
			verts[3].v2=0;
			verts[3].diffuse=0xffffffff;
		}
	}

	DX8Wrapper::Set_Index_Buffer(m_indexBuffer,0);
	DX8Wrapper::Set_Vertex_Buffer(vb_access);

	Matrix3D tm(1);
	//set postion of skybody in world
//	tm.Set_Translation(Vector3(40,0,0));
	DX8Wrapper::Set_Transform(D3DTS_WORLD,tm);

	DX8Wrapper::Draw_Triangles(	0,2, 0,	4);	//draw a quad, 2 triangles, 4 verts
}
#endif

/* The save's grid data (the snapshot that was here) is WaterGrid::xfer's; W3DTerrainVisual::xfer
	 still writes it in the same place. */

