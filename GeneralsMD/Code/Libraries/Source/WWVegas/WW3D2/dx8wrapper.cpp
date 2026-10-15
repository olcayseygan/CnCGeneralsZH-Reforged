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

/***********************************************************************************************
 ***              C O N F I D E N T I A L  ---  W E S T W O O D  S T U D I O S               ***
 ***********************************************************************************************
 *                                                                                             *
 *                 Project Name : WW3D                                                         *
 *                                                                                             *
 *                     $Archive:: /Commando/Code/ww3d2/dx8wrapper.cpp                         $*
 *                                                                                             *
 *              Original Author:: Jani Penttinen                                               *
 *                                                                                             *
 *                      $Author:: Kenny Mitchell                                               * 
 *                                                                                             * 
 *                     $Modtime:: 08/05/02 1:27p                                              $*
 *                                                                                             *
 *                    $Revision:: 170                                                         $*
 *                                                                                             *
 * 06/26/02 KM Matrix name change to avoid MAX conflicts                                       *
 * 06/27/02 KM Render to shadow buffer texture support														*
 * 06/27/02 KM Shader system updates																				*
 * 08/05/02 KM Texture class redesign 
 *---------------------------------------------------------------------------------------------*
 * Functions:                                                                                  *
 *   DX8Wrapper::_Update_Texture -- Copies a texture from system memory to video memory        *
 * - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

//#define CREATE_DX8_MULTI_THREADED
//#define CREATE_DX8_FPU_PRESERVE
#if defined(_WIN32)
#define WW3D_DEVTYPE Requested_Device_Type()	// HAL unless ZH_D3D9_DEVTYPE says otherwise: see below
#else
#define WW3D_DEVTYPE D3DDEVTYPE_HAL
#endif

#include "dx8wrapper.h"
#if defined(_WIN32)
#include "dx8webbrowser.h"	// the embedded browser: Windows only
#endif
#include "dx8fvf.h"
#include "fullscreenfit.h"
#include "dx8vertexbuffer.h"
#include "dx8indexbuffer.h"
#include "dx8renderer.h"
#include "ww3d.h"
#include "camera.h"
#include "wwstring.h"
#include "matrix4.h"
#include "vertmaterial.h"
#include "rddesc.h"
#include "lightenvironment.h"
#include "statistics.h"
#if defined(_WIN32)
#include "registry.h"
#endif
#include "boxrobj.h"
#include "pointgr.h"
#include "render2d.h"
#include "sortingrenderer.h"
#include "shattersystem.h"
#include "light.h"
#include "assetmgr.h"
#include "textureloader.h"
#include "missingtexture.h"
#include "thread.h"
#include <stdio.h>
#include "d3dx9runtime.h"
#include "ffprobe.h"
#include "ffshadercache.h"
#include "dx11runtime.h"
#include "pot.h"
#include "wwprofile.h"
#include "ffactory.h"
#include "dx8caps.h"
#include "formconv.h"
#include "dx8texman.h"
#include "bound.h"
#if defined(_WIN32)
#include "dx8webbrowser.h"	// the embedded browser: Windows only
#endif

#include "shdlib.h"
#include <string.h>	// memset, strcpy, strlen

#if defined(_WIN32)
#include <stdlib.h>	// getenv

/* ZH_D3D9_DEVTYPE=nullref is for measuring, not playing: the reference rasterizer's NULL device
	 takes every call and draws nothing, so a run times the game's own work in the Direct3D 9
	 runtime with no driver or rasterizer under it.  It exists only where d3dref9.dll does (the
	 DirectX SDK's debug runtime); without it CreateDevice fails as it would on a machine with no
	 device.  Unset, or anything else, the device is the hardware one it has always been.  The
	 Direct3D 11 side's counterpart is ZH_DX11_DRIVER (dx11device.cpp). */
static D3DDEVTYPE Requested_Device_Type()
{
	const char * requested = getenv("ZH_D3D9_DEVTYPE");
	return (requested != NULL && _stricmp(requested, "nullref") == 0) ? D3DDEVTYPE_NULLREF : D3DDEVTYPE_HAL;
}
#endif

const int DEFAULT_RESOLUTION_WIDTH = 640;
const int DEFAULT_RESOLUTION_HEIGHT = 480;
const int DEFAULT_BIT_DEPTH = 32;
const int DEFAULT_TEXTURE_BIT_DEPTH = 16;

bool DX8Wrapper_IsWindowed = true;

// FPU_PRESERVE
int DX8Wrapper_PreserveFPU = 0;

/***********************************************************************************
**
** DX8Wrapper Static Variables
**
***********************************************************************************/

static RenderWindow						_Hwnd															= NULL;
bool								DX8Wrapper::IsInitted									= false;
bool								DX8Wrapper::_EnableTriangleDraw						= true;

int								DX8Wrapper::CurRenderDevice							= -1;
int								DX8Wrapper::ResolutionWidth							= DEFAULT_RESOLUTION_WIDTH;
int								DX8Wrapper::ResolutionHeight							= DEFAULT_RESOLUTION_HEIGHT;
int								DX8Wrapper::BitDepth										= DEFAULT_BIT_DEPTH;
int								DX8Wrapper::TextureBitDepth							= DEFAULT_TEXTURE_BIT_DEPTH;
bool								DX8Wrapper::IsWindowed									= false;
D3DFORMAT					DX8Wrapper::DisplayFormat	= D3DFMT_UNKNOWN;

D3DMATRIX						DX8Wrapper::old_world;
D3DMATRIX						DX8Wrapper::old_view;
D3DMATRIX						DX8Wrapper::old_prj;

// shader system additions KJM v
uint32								DX8Wrapper::Vertex_Format								= 0;
IDirect3DVertexShader9 *	DX8Wrapper::Vertex_Shader								= NULL;
IDirect3DPixelShader9 *		DX8Wrapper::Pixel_Shader								= NULL;

Vector4							DX8Wrapper::Vertex_Shader_Constants[MAX_VERTEX_SHADER_CONSTANTS];
Vector4							DX8Wrapper::Pixel_Shader_Constants[MAX_PIXEL_SHADER_CONSTANTS];

LightEnvironmentClass*		DX8Wrapper::Light_Environment							= NULL;
RenderInfoClass*				DX8Wrapper::Render_Info									= NULL;

uint32								DX8Wrapper::Vertex_Processing_Behavior				= 0;
ZTextureClass*					DX8Wrapper::Shadow_Map[MAX_SHADOW_MAPS];

Vector3							DX8Wrapper::Ambient_Color;
// shader system additions KJM ^

bool								DX8Wrapper::world_identity;
unsigned							DX8Wrapper::RenderStates[256];
unsigned							DX8Wrapper::TextureStageStates[MAX_TEXTURE_STAGES][32];
unsigned							DX8Wrapper::SamplerStates[MAX_TEXTURE_STAGES][MAX_SAMPLER_STATES];
float								DX8Wrapper::DepthBufferEpsilon=0.0f;

// One depth-buffer level, negative, in the normalised range D3DRS_DEPTHBIAS reads.  The
// 24 and 32 bit cases deliberately shift by 20 rather than by their own width: a float
// cannot hold 1/(2^24 - 1) accurately enough for the bias to come out where D3D8's
// integer ZBIAS put it.  This is d3d8to9's CalcDepthBias, which is what has been running
// under this renderer since the port (d3d8types.cpp).
float DX8Wrapper::_Depth_Buffer_Epsilon(D3DFORMAT depth_stencil_format)
{
	const int USABLE_FLOAT_DEPTH_BITS=20;
	const int SHORT_DEPTH_BITS=16;
	const int PACKED_DEPTH_BITS=15;

	switch (depth_stencil_format) {
	case D3DFMT_D32:
	case D3DFMT_D24X4S4:
	case D3DFMT_D24S8:
	case D3DFMT_D24X8:
		return -1.0f/((1<<USABLE_FLOAT_DEPTH_BITS)-1);
	case D3DFMT_D15S1:
		return -1.0f/((1<<PACKED_DEPTH_BITS)-1);
	default:
		return -1.0f/((1<<SHORT_DEPTH_BITS)-1);
	}
}

void DX8Wrapper::_Set_DX8_Render_Target(IDirect3DSurface9 * render_target, IDirect3DSurface9 * depth_buffer)
{
	DX8CALL(SetRenderTarget(PRIMARY_RENDER_TARGET,render_target));
	DX8CALL(SetDepthStencilSurface(depth_buffer));
	// Every render target the engine sets passes through here, the device's own back buffer
	// included; the backend works out which is which from whether the surface has a texture behind
	// it.
	Direct3D11_Mirror_Render_Target(render_target);
}

void DX8Wrapper::_Draw_DX8_Primitive_UP(D3DPRIMITIVETYPE type, unsigned primitive_count,
	const void * vertices, unsigned stride)
{
	// Drawn on Direct3D 9 whichever device presents: the smudge test reads this draw back.
	Flush_Deferred_D3D9_State();
	DX8CALL(DrawPrimitiveUP(type, primitive_count, vertices, stride));
	if (type == D3DPT_TRIANGLESTRIP) {
		Direct3D11_Draw_User_Strip(vertices, primitive_count, stride);
	}
}

// D3D9 has no CopyRects.  Which call replaces it depends on where the two surfaces live,
// so this asks them: a default-pool pair stretches, a system-memory source updates, and
// anything else goes through D3DX, which is the only one of the three that can cross a
// managed pool.  The branch order is d3d8to9's (d3d8to9_device.cpp:447), because that is
// the routing the game's own copies have been taking.
void DX8Wrapper::_Copy_DX8_Rects(
	IDirect3DSurface9* pSourceSurface,
	const RenderRect* pSourceRectsArray,
	UINT cRects,
	IDirect3DSurface9* pDestinationSurface,
	const RenderPoint* pDestPointsArray)
{
	if (pSourceSurface==NULL || pDestinationSurface==NULL || pSourceSurface==pDestinationSurface) {
		WWASSERT(0);
		return;
	}

	D3DSURFACE_DESC source_desc;
	D3DSURFACE_DESC destination_desc;
	if (Render_Failed(pSourceSurface->GetDesc(&source_desc))
		|| Render_Failed(pDestinationSurface->GetDesc(&destination_desc))) {
		WWASSERT(0);
		return;
	}

	const UINT rect_count=cRects==0 ? 1 : cRects;
	for (UINT rect_index=0; rect_index<rect_count; ++rect_index) {
		RenderRect source_rect;
		if (pSourceRectsArray!=NULL) {
			source_rect=pSourceRectsArray[rect_index];
		}
		else {
			source_rect.left=0;
			source_rect.top=0;
			source_rect.right=source_desc.Width;
			source_rect.bottom=source_desc.Height;
		}

		RenderRect destination_rect;
		if (pDestPointsArray!=NULL) {
			destination_rect.left=pDestPointsArray[rect_index].x;
			destination_rect.top=pDestPointsArray[rect_index].y;
			destination_rect.right=destination_rect.left+(source_rect.right-source_rect.left);
			destination_rect.bottom=destination_rect.top+(source_rect.bottom-source_rect.top);
		}
		else {
			destination_rect=source_rect;
		}

		RenderResult hr;
		if (source_desc.Pool==D3DPOOL_MANAGED || destination_desc.Pool!=D3DPOOL_DEFAULT) {
			hr=D3DXLoadSurfaceFromSurface(pDestinationSurface,NULL,&destination_rect,
				pSourceSurface,NULL,&source_rect,D3DX_FILTER_NONE,0);
			if (Render_Succeeded(hr)) {
				// A surface written this way does not tell the texture that owns it, and
				// a managed texture that is never marked dirty is never uploaded again.
				IDirect3DTexture9* container=NULL;
				if (Render_Succeeded(pDestinationSurface->GetContainer(IID_IDirect3DTexture9,(void**)&container))
					&& container!=NULL) {
					container->AddDirtyRect(&destination_rect);
					container->Release();
				}
			}
		}
		else if (source_desc.Pool==D3DPOOL_SYSTEMMEM) {
			const RenderPoint destination_point={destination_rect.left,destination_rect.top};
			hr=D3DDevice->UpdateSurface(pSourceSurface,&source_rect,pDestinationSurface,&destination_point);
		}
		else {
			hr=D3DDevice->StretchRect(pSourceSurface,&source_rect,
				pDestinationSurface,&destination_rect,D3DTEXF_NONE);
		}

		if (Render_Failed(hr)) {
			Non_Fatal_Log_DX8_ErrorCode(hr,__FILE__,__LINE__);
			return;
		}

		// The same copy into the D3D11 twin of whatever texture the destination belongs to.  It
		// reads the source surface rather than the destination, because a default-pool destination
		// is exactly the one D3D9 will not let anything read back.
		const RenderPoint destination_point={destination_rect.left,destination_rect.top};
		Direct3D11_Mirror_Surface_Copy(pDestinationSurface,pSourceSurface,&source_rect,
			&destination_point);
	}
}
IDirect3DBaseTexture9 *		DX8Wrapper::Textures[MAX_TEXTURE_STAGES];
RenderStateStruct				DX8Wrapper::render_state;
unsigned							DX8Wrapper::render_state_changed;

bool								DX8Wrapper::FogEnable									= false;
D3DCOLOR							DX8Wrapper::FogColor										= 0;

IDirect3D9 *					DX8Wrapper::D3DInterface								= NULL;
IDirect3DDevice9 *			DX8Wrapper::D3DDevice									= NULL;
IDirect3DSurface9 *			DX8Wrapper::CurrentRenderTarget						= NULL;
IDirect3DSurface9 *			DX8Wrapper::CurrentDepthBuffer						= NULL;
IDirect3DSurface9 *			DX8Wrapper::DefaultRenderTarget						= NULL;
IDirect3DSurface9 *			DX8Wrapper::DefaultDepthBuffer						= NULL;
bool								DX8Wrapper::IsRenderToTexture							= false;

unsigned							DX8Wrapper::matrix_changes								= 0;
unsigned							DX8Wrapper::material_changes							= 0;
unsigned							DX8Wrapper::vertex_buffer_changes					= 0;
unsigned							DX8Wrapper::index_buffer_changes                = 0;
unsigned							DX8Wrapper::light_changes								= 0;
unsigned							DX8Wrapper::texture_changes							= 0;
unsigned							DX8Wrapper::render_state_changes						= 0;
unsigned							DX8Wrapper::texture_stage_state_changes			= 0;
unsigned							DX8Wrapper::draw_calls									= 0;
unsigned							DX8Wrapper::_MainThreadID								= 0;
bool								DX8Wrapper::CurrentDX8LightEnables[4];
D3DMATERIAL9					DX8Wrapper::CurrentDX8Material;
bool								DX8Wrapper::CurrentDX8MaterialValid					= false;
D3DLIGHT9						DX8Wrapper::CurrentDX8Lights[4];
unsigned							DX8Wrapper::CurrentDX8LightsValid						= 0;
bool								DX8Wrapper::D3D9StatePending							= false;
unsigned							DX8Wrapper::PendingRenderStates[256/32];
unsigned							DX8Wrapper::PendingTextureStageStates[MAX_TEXTURE_STAGES];
unsigned							DX8Wrapper::PendingSamplerStates[MAX_TEXTURE_STAGES];
unsigned							DX8Wrapper::PendingTextures							= 0;
bool								DX8Wrapper::PendingMaterial							= false;
D3DMATERIAL9					DX8Wrapper::PendingMaterialValue;
unsigned							DX8Wrapper::PendingLights								= 0;
unsigned							DX8Wrapper::PendingLightEnables						= 0;
D3DLIGHT9						DX8Wrapper::PendingLightValues[4];
bool								DX8Wrapper::IsDeviceLost;
int								DX8Wrapper::ZBias;
float								DX8Wrapper::ZNear;
float								DX8Wrapper::ZFar;
Matrix4x4						DX8Wrapper::ProjectionMatrix;
Matrix4x4						DX8Wrapper::DX8Transforms[D3DTS_WORLD+1];

DX8Caps*							DX8Wrapper::CurrentCaps = 0;

// Hack test... this disables rendering of batches of too few polygons.
unsigned							DX8Wrapper::DrawPolygonLowBoundLimit=0;

D3DADAPTER_IDENTIFIER9		DX8Wrapper::CurrentAdapterIdentifier;

unsigned long DX8Wrapper::FrameCount = 0;

bool								_DX8SingleThreaded										= false;

unsigned							number_of_DX8_calls										= 0;
static unsigned				last_frame_matrix_changes								= 0;
static unsigned				last_frame_material_changes							= 0;
static unsigned				last_frame_vertex_buffer_changes						= 0;
static unsigned				last_frame_index_buffer_changes						= 0;
static unsigned				last_frame_light_changes								= 0;
static unsigned				last_frame_texture_changes								= 0;
static unsigned				last_frame_render_state_changes						= 0;
static unsigned				last_frame_texture_stage_state_changes				= 0;
static unsigned				last_frame_number_of_DX8_calls						= 0;
static unsigned				last_frame_draw_calls									= 0;

static D3DDISPLAYMODE DesktopMode;

static D3DPRESENT_PARAMETERS								_PresentParameters;

// Multisampling (anti-aliasing) is opt-in and off by default, which is exactly what retail did.
// The sample count is pushed in from the app layer before the device is created, because WW3D2 has
// no view of GlobalData and is deliberately not given one.  The highest level the device actually
// reports for BOTH the back buffer and the depth/stencil format is the one used, so an unsupported
// request quietly degrades instead of failing device creation.
static unsigned _RequestedMultiSampleLevel = 0;

void DX8Wrapper::Set_Requested_MultiSample_Level(unsigned samples)
{
	if (samples < 2) samples = 0;	// 0 and 1 both mean "no multisampling"
	if (samples > 16) samples = 16;
	_RequestedMultiSampleLevel = samples;
}

// Off is the default, matching the uncapped picture: present as soon as the frame is ready.
static bool _RequestedVSync = false;

void DX8Wrapper::Set_Requested_VSync(bool enabled)
{
	_RequestedVSync = enabled;
	_PresentParameters.PresentationInterval = enabled
		? D3DPRESENT_INTERVAL_ONE
		: D3DPRESENT_INTERVAL_IMMEDIATE;
}

bool DX8Wrapper::Get_Requested_VSync(void)
{
	return _RequestedVSync;
}

// Non-multisampled depth/stencil for render-to-texture.  A multisampled back buffer gets a
// multisampled auto depth/stencil, and Direct3D requires the depth buffer to match the render
// target - so the plain render target textures the screen filters and the water reflection draw
// into need a non-multisampled one of their own.  (The retail runtime does not reject the
// mismatch at bind time; the debug runtime and the docs do.)
// NULL means "not multisampling, keep the depth buffer you already have".
static IDirect3DSurface9 * _RTTDepthBuffer = NULL;

static DynamicVectorClass<StringClass>					_RenderDeviceNameTable;
static DynamicVectorClass<StringClass>					_RenderDeviceShortNameTable;
static DynamicVectorClass<RenderDeviceDescClass>	_RenderDeviceDescriptionTable;


#if defined(_WIN32)
// d3d9.dll is loaded by hand rather than imported, the way d3d8.dll was, so a machine
// without it gets the engine's own "no Direct3D" path instead of a loader error box.
typedef IDirect3D9* (WINAPI *Direct3DCreate9Type) (UINT SDKVersion);
Direct3DCreate9Type	Direct3DCreate9Ptr = NULL;
HINSTANCE D3D9Lib = NULL;
#endif

static int Score_Render_Adapter(const RenderDeviceDescClass &desc)
{
	const D3DADAPTER_IDENTIFIER9 &id = desc.Get_Adapter_Identifier();
	if (strstr(id.Description, "Basic Render") != NULL)
		return -1000;
	// Discrete first. Adapter 0 on this machine is Intel UHD 630; 16x MSAA on it TDRs
	// and the lost device then dies in Bink's surface lock. NVIDIA is 0x10DE, AMD 0x1002.
	if (id.VendorId == 0x10DE || id.VendorId == 0x1002)
		return 200;
	if (id.VendorId == 0x8086)
		return 0;
	return 100;
}

static int Find_Preferred_Render_Device(void)
{
	int best = 0;
	int bestScore = -10000;
	for (int i = 0; i < _RenderDeviceDescriptionTable.Count(); ++i)
	{
		const int score = Score_Render_Adapter(_RenderDeviceDescriptionTable[i]);
		if (score > bestScore)
		{
			bestScore = score;
			best = i;
		}
	}
	return best;
}

DX8_CleanupHook	 *DX8Wrapper::m_pCleanupHook=NULL;
#ifdef EXTENDED_STATS
DX8_Stats	 DX8Wrapper::stats;
#endif
/***********************************************************************************
**
** DX8Wrapper Implementation
**
***********************************************************************************/

// D3DX9 dropped D3DXGetErrorStringA, which used to fill a caller's buffer and could
// fail; Get_D3D_Error_String names the code itself and always returns something.
void Log_DX8_ErrorCode(unsigned res)
{
	WWDEBUG_SAY((Get_D3D_Error_String(res)));
	WWASSERT(0);
}

void Non_Fatal_Log_DX8_ErrorCode(unsigned res,const char * file,int line)
{
	WWDEBUG_SAY(("DX8 Error: %s, File: %s, Line: %d\n",Get_D3D_Error_String(res),file,line));
}



bool DX8Wrapper::Init(void * hwnd, bool lite)
{
	WWASSERT(!IsInitted);

	// zero memory
	memset(Textures,0,sizeof(IDirect3DBaseTexture9*)*MAX_TEXTURE_STAGES);
	memset(RenderStates,0,sizeof(unsigned)*256);
	memset(TextureStageStates,0,sizeof(unsigned)*32*MAX_TEXTURE_STAGES);
	memset(SamplerStates,0,sizeof(SamplerStates));
	memset(Vertex_Shader_Constants,0,sizeof(Vector4)*MAX_VERTEX_SHADER_CONSTANTS);
	memset(Pixel_Shader_Constants,0,sizeof(Vector4)*MAX_PIXEL_SHADER_CONSTANTS);
	memset(&render_state,0,sizeof(RenderStateStruct));
	memset(Shadow_Map,0,sizeof(ZTextureClass*)*MAX_SHADOW_MAPS);

	// D3DX9 has no import library and is bound by hand, so this has to happen before
	// anything reaches a D3DX entry point.  A machine without d3dx9_43.dll binds the
	// port's own D3DX, the one ARM64 runs; W3DDisplay::init logs which one it got.
	if (!Bind_D3DX9_Runtime()) {
		WWDEBUG_SAY(("DX8Wrapper: d3dx9_43.dll did not bind; textures and shaders will not load\n"));
	}

	/*
	** Initialize all variables!
	*/
	_Hwnd = (RenderWindow)hwnd;
	_MainThreadID=ThreadClass::_Get_Current_Thread_ID();
	WWDEBUG_SAY(("DX8Wrapper main thread: 0x%x\n",_MainThreadID));
	CurRenderDevice = -1;
	ResolutionWidth = DEFAULT_RESOLUTION_WIDTH;
	ResolutionHeight = DEFAULT_RESOLUTION_HEIGHT;
	// Initialize Render2DClass Screen Resolution
	Render2DClass::Set_Screen_Resolution( RectClass( 0, 0, ResolutionWidth, ResolutionHeight ) );
	BitDepth = DEFAULT_BIT_DEPTH;
	IsWindowed = false;	
	DX8Wrapper_IsWindowed = false;

	for (int light=0;light<4;++light) CurrentDX8LightEnables[light]=false;

	memset(&old_world,0, sizeof(D3DMATRIX));
	memset(&old_view,0, sizeof(D3DMATRIX));
	memset(&old_prj,0, sizeof(D3DMATRIX));

	//old_vertex_shader; TODO
	//old_sr_shader;
	//current_shader;

	//world_identity;
	//CurrentFogColor;

	D3DInterface = NULL;
	D3DDevice = NULL;

	WWDEBUG_SAY(("Reset DX8Wrapper statistics\n"));
	Reset_Statistics();

	Invalidate_Cached_Render_States();

	if (!lite) {
#if defined(_WIN32)
		D3D9Lib = LoadLibrary("D3D9.DLL");

		if (D3D9Lib == NULL) return false;	// Return false at this point if init failed

		Direct3DCreate9Ptr = (Direct3DCreate9Type) GetProcAddress(D3D9Lib, "Direct3DCreate9");
		if (Direct3DCreate9Ptr == NULL) return false;
#endif

		/*
		** Create the D3D interface object
		*/
		WWDEBUG_SAY(("Create Direct3D9\n"));
#if defined(_WIN32)
		D3DInterface = Direct3DCreate9Ptr(D3D_SDK_VERSION);
#else
		D3DInterface = Direct3DCreate9(D3D_SDK_VERSION);	// the device is linked in (posixd3d9): nothing to load
#endif
		if (D3DInterface == NULL) {
			return(false);
		}
		IsInitted = true;

		/*
		** Enumerate the available devices
		*/
		WWDEBUG_SAY(("Enumerate devices\n"));
		Enumerate_Devices();
		WWDEBUG_SAY(("DX8Wrapper Init completed\n"));
	}

	return(true);
}

static void restore_desktop_display();

void DX8Wrapper::Shutdown(void)
{
	// Before the device goes: the inventory is read off it, and the device owns every shader the
	// combiner cache compiled.
	FixedFunctionProbe_Dump("ffprobe.txt");
	CombinerShaderCache_Release();
	// The setters stop deferring once Direct3D 11 is gone, so nothing may be left owed across it.
	Flush_Deferred_D3D9_State();
	Direct3D11_Release();
	restore_desktop_display();

	if (D3DDevice) {

		Set_Render_Target ((IDirect3DSurface9 *)NULL);
		Release_Device();
	}

	if (D3DInterface) {
		D3DInterface->Release();
		D3DInterface=NULL;
	}

	Unbind_D3DX9_Runtime();

	if (CurrentCaps)
	{
		int max=CurrentCaps->Get_Max_Textures_Per_Pass();
		for (int i = 0; i < max; i++) 
		{
			if (Textures[i]) 
			{
				Textures[i]->Release();
				Textures[i] = NULL;
			}
		}
	}

	if (D3DInterface) {
		UINT newRefCount=D3DInterface->Release();
		D3DInterface=NULL;
	}

	// D3D9.DLL stays loaded until the process ends: it is not freed here.  Textures outlive this call -
	// the particle system manager is shut down after the game client that owns the display, and frees
	// its point groups' textures then - and each Release is a call into this DLL.  This handle is the
	// only reference to it (the exe and d3dx9_43.dll do not import it), so FreeLibrary unmapped the code
	// those calls go to.  Required under Proton/Wine: an exit with particle textures still alive faulted
	// in ~TextureBaseClass with the unload and exits cleanly without it (measured both ways).  Harmless on
	// native Windows: two real scenes exited cleanly there before and after this change.  A synthetic
	// probe in this order (texture alive, FreeLibrary, then its Release) shows the mechanism can fault on
	// Windows too.  Nothing is gained by unloading it moments before the process exits.

	_RenderDeviceNameTable.Clear();		 // note - Delete_All() resizes the vector, causing a reallocation.  Clear is better. jba.
	_RenderDeviceShortNameTable.Clear();
	_RenderDeviceDescriptionTable.Clear();	

	DX8Caps::Shutdown();
	IsInitted = false;		// 010803 srj
}

void DX8Wrapper::Do_Onetime_Device_Dependent_Inits(void)
{
	/*
	** Set Global render states (some of which depend on caps)
	*/
	Compute_Caps(D3DFormat_To_WW3DFormat(DisplayFormat));

   /*
	** Initalize any other subsystems inside of WW3D
	*/
	MissingTexture::_Init();
	TextureFilterClass::_Init_Filters((TextureFilterClass::TextureFilterMode)WW3D::Get_Texture_Filter());
	TheDX8MeshRenderer.Init();
	SHD_INIT;
	BoxRenderObjClass::Init();
	VertexMaterialClass::Init();
	PointGroupClass::_Init(); // This needs the VertexMaterialClass to be initted
	ShatterSystem::Init();
	TextureLoader::Init();

	Set_Default_Global_Render_States();
}

/*
** The subset of the list above that never calls D3D, for a run started with no render device.
** Deliberately a second list rather than a piece factored out of the first: the order in
** Do_Onetime_Device_Dependent_Inits is EA's and nothing here is worth the risk of disturbing it on
** the path that every player takes.
**
** What makes the subset necessary is that the rest of WW3D2 assumes all of it exists the moment
** anything is built - VertexMaterialClass::Get_Preset is reached from the constructor of almost
** every render object, and without Init() it walks an empty preset table.
*/
void DX8Wrapper::Do_Onetime_Device_Independent_Inits(void)
{
	TheDX8MeshRenderer.Init();
	BoxRenderObjClass::Init();
	VertexMaterialClass::Init();
	PointGroupClass::_Init(); // This needs the VertexMaterialClass to be initted
	ShatterSystem::Init();
	TextureLoader::Init();
}

inline uint32 F2DW(float f) { return *((unsigned*)&f); }
void DX8Wrapper::Set_Default_Global_Render_States(void)
{
	DX8_THREAD_ASSERT();
	const D3DCAPS9 &caps = Get_Current_Caps()->Get_DX8_Caps();

	Set_DX8_Render_State(D3DRS_RANGEFOGENABLE, (caps.RasterCaps & D3DPRASTERCAPS_FOGRANGE) ? true : false);
	Set_DX8_Render_State(D3DRS_FOGTABLEMODE, D3DFOG_NONE);
	Set_DX8_Render_State(D3DRS_FOGVERTEXMODE, D3DFOG_LINEAR);
	Set_DX8_Render_State(D3DRS_SPECULARMATERIALSOURCE, D3DMCS_MATERIAL);
	Set_DX8_Render_State(D3DRS_COLORVERTEX, true);
	Set_DX8_ZBias(0);
	Set_DX8_Texture_Stage_State(1, D3DTSS_BUMPENVLSCALE, F2DW(1.0f));
	Set_DX8_Texture_Stage_State(1, D3DTSS_BUMPENVLOFFSET, F2DW(0.0f));
	Set_DX8_Texture_Stage_State(0, D3DTSS_BUMPENVMAT00,F2DW(1.0f));
	Set_DX8_Texture_Stage_State(0, D3DTSS_BUMPENVMAT01,F2DW(0.0f));
	Set_DX8_Texture_Stage_State(0, D3DTSS_BUMPENVMAT10,F2DW(0.0f));
	Set_DX8_Texture_Stage_State(0, D3DTSS_BUMPENVMAT11,F2DW(1.0f));

//	Set_DX8_Render_State(D3DRS_CULLMODE, D3DCULL_CW);
	// Set dither mode here?
}

//MW: I added this for 'Generals'.
bool DX8Wrapper::Validate_Device(void)
{	RenderUInt32 numPasses=0;
	RenderResult hRes;

	Flush_Deferred_D3D9_State();
	hRes=_Get_D3D_Device()->ValidateDevice(&numPasses);

	return (hRes == D3D_OK);
}

void DX8Wrapper::Invalidate_Cached_Render_States(void)
{
	// The caches below are about to stop holding the values the device is owed.
	Flush_Deferred_D3D9_State();
	render_state_changed=0;
	CurrentDX8MaterialValid=false;
	CurrentDX8LightsValid=0;

	int a;
	for (a=0;a<sizeof(RenderStates)/sizeof(unsigned);++a) {
		RenderStates[a]=0x12345678;
	}
	for (a=0;a<MAX_TEXTURE_STAGES;++a) 
	{
		for (int b=0; b<32;b++)
		{
			TextureStageStates[a][b]=0x12345678;
		}
		// The sampler states D3D9 split off the texture stage have their own cache and
		// have to be invalidated with it, or the first filter the renderer sets after a
		// reset is skipped as redundant and the device keeps its own default instead.
		for (int sampler_state=0; sampler_state<MAX_SAMPLER_STATES; sampler_state++)
		{
			SamplerStates[a][sampler_state]=0x12345678;
		}
		//Need to explicitly set texture to NULL, otherwise app will not be able to
		//set it to null because of redundant state checker. MW
		if (_Get_D3D_Device())
			_Get_D3D_Device()->SetTexture(a,NULL);
		if (Textures[a] != NULL) {
			Textures[a]->Release();
		}
		Textures[a]=NULL;
		// The Direct3D 11 backend borrows the view the D3D9 texture owns, and the release above can
		// be that texture's last reference: a reset drops the render targets first.  Left bound, the
		// view dangles, and the next draw into a target (the water's reflection) reads it and dies.
		Direct3D11_Mirror_Texture(a, NULL);
	}

	ShaderClass::Invalidate();

	//Need to explicitly set render_state texture pointers to NULL. MW
	Release_Render_State();

	// (gth) clear the matrix shadows too
	for (int i=0; i<D3DTS_WORLD+1; i++) {
		DX8Transforms[i][0].Set(0,0,0,0);
		DX8Transforms[i][1].Set(0,0,0,0);
		DX8Transforms[i][2].Set(0,0,0,0);
		DX8Transforms[i][3].Set(0,0,0,0);
	}

}

void DX8Wrapper::Do_Onetime_Device_Dependent_Shutdowns(void)
{
	/*
	** Shutdown ww3d systems
	*/
	int i;
	for (i=0;i<MAX_VERTEX_STREAMS;++i) {
		if (render_state.vertex_buffers[i]) render_state.vertex_buffers[i]->Release_Engine_Ref();
		REF_PTR_RELEASE(render_state.vertex_buffers[i]);
	}
	if (render_state.index_buffer) render_state.index_buffer->Release_Engine_Ref();
	REF_PTR_RELEASE(render_state.index_buffer);
	REF_PTR_RELEASE(render_state.material);
	for (i=0;i<CurrentCaps->Get_Max_Textures_Per_Pass();++i) REF_PTR_RELEASE(render_state.Textures[i]);


	TextureLoader::Deinit();
	SortingRendererClass::Deinit();
	DynamicVBAccessClass::_Deinit();
	DynamicIBAccessClass::_Deinit();
	ShatterSystem::Shutdown();
	PointGroupClass::_Shutdown();
	VertexMaterialClass::Shutdown();
	BoxRenderObjClass::Shutdown();
	SHD_SHUTDOWN;
	TheDX8MeshRenderer.Shutdown();
	MissingTexture::_Deinit();

	if (CurrentCaps) {
		delete CurrentCaps;
		CurrentCaps=NULL;
	}

}


bool DX8Wrapper::Create_Device(void)
{
	WWASSERT(D3DDevice==NULL);	// for now, once you've created a device, you're stuck with it!

	D3DCAPS9 caps;
	if 
	(
		Render_Failed
		(
			D3DInterface->GetDeviceCaps
			(
				CurRenderDevice,
				WW3D_DEVTYPE,
				&caps
			)
		)
	)
	{
		return false;
	}

	memset(&CurrentAdapterIdentifier,0, sizeof(D3DADAPTER_IDENTIFIER9));
	
	if
	(
		Render_Failed
		( 
			D3DInterface->GetAdapterIdentifier
			(
				CurRenderDevice,
				NO_ADAPTER_IDENTIFIER_FLAGS,
				&CurrentAdapterIdentifier
			)
			)	
	) 
	{
		return false;
	}

#ifndef _XBOX
	
	Vertex_Processing_Behavior=(caps.DevCaps&D3DDEVCAPS_HWTRANSFORMANDLIGHT) ?
		D3DCREATE_MIXED_VERTEXPROCESSING : D3DCREATE_SOFTWARE_VERTEXPROCESSING;

	// enable this when all 'get' dx calls are removed KJM
	/*if (caps.DevCaps&D3DDEVCAPS_PUREDEVICE)
	{
		Vertex_Processing_Behavior|=D3DCREATE_PUREDEVICE;
	}*/

#else // XBOX
	Vertex_Processing_Behavior=D3DCREATE_PUREDEVICE;
#endif // XBOX

#ifdef CREATE_DX8_MULTI_THREADED
	Vertex_Processing_Behavior|=D3DCREATE_MULTITHREADED;
	_DX8SingleThreaded=false;
#else
	_DX8SingleThreaded=true;
#endif

	if (DX8Wrapper_PreserveFPU)
		Vertex_Processing_Behavior |= D3DCREATE_FPU_PRESERVE;

#ifdef CREATE_DX8_FPU_PRESERVE
	Vertex_Processing_Behavior|=D3DCREATE_FPU_PRESERVE;
#endif

	RenderResult hr=D3DInterface->CreateDevice
	(
		CurRenderDevice,
		WW3D_DEVTYPE,
		_Hwnd,
		Vertex_Processing_Behavior,
		&_PresentParameters,
		&D3DDevice
	);

	if (Render_Failed(hr) && CurRenderDevice != 0)
	{
		CurRenderDevice = 0;
		hr=D3DInterface->CreateDevice
		(
			CurRenderDevice,
			WW3D_DEVTYPE,
			_Hwnd,
			Vertex_Processing_Behavior,
			&_PresentParameters,
			&D3DDevice
		);
	}

	if (Render_Failed(hr)) 
	{
		// The device selection may fail because the device lied that it supports 32 bit zbuffer with 16 bit
		// display. This happens at least on Voodoo2.

		if ((_PresentParameters.BackBufferFormat==D3DFMT_R5G6B5 ||
			_PresentParameters.BackBufferFormat==D3DFMT_X1R5G5B5 ||
			_PresentParameters.BackBufferFormat==D3DFMT_A1R5G5B5) &&
			(_PresentParameters.AutoDepthStencilFormat==D3DFMT_D32 ||
			_PresentParameters.AutoDepthStencilFormat==D3DFMT_D24S8 ||
			_PresentParameters.AutoDepthStencilFormat==D3DFMT_D24X8)) 
		{
			_PresentParameters.AutoDepthStencilFormat=D3DFMT_D16;
			hr = D3DInterface->CreateDevice
			(
				CurRenderDevice,
				WW3D_DEVTYPE,
				_Hwnd,
				Vertex_Processing_Behavior,
				&_PresentParameters,
				&D3DDevice 
			);

			if (Render_Failed(hr)) 
			{
				return false;
			}
        }
		else
		{
				return false;
		}
	}

	DepthBufferEpsilon=_Depth_Buffer_Epsilon(_PresentParameters.AutoDepthStencilFormat);

	/*
	** Initialize all subsystems
	*/
	Do_Onetime_Device_Dependent_Inits();
	return true;
}

#if defined(_WIN32)
// What the fullscreen display has changed on the desktop, so leaving the game can put it back: only
// the gamma now, the desktop's own ramp, read before the game's first one.
static bool DesktopGammaSaved = false;
static bool GameGammaSet = false;
static D3DGAMMARAMP DesktopGammaRamp;
static D3DGAMMARAMP GameGammaRamp;

// The monitor all of that happens on, as its GDI device name ("\\.\DISPLAY2").  Empty until the app
// layer pushes one in, and then the primary's DC and the primary's mode are what a null name gets.
static char RequestedMonitor[CCHDEVICENAME] = "";

// A gamma ramp belongs to one monitor.  The whole-screen DC GetDC(NULL) hands out sets the primary's,
// so a game on another monitor would move the brightness of the one it is not on.
static HDC create_monitor_dc()
{
	return CreateDCA(RequestedMonitor[0] ? RequestedMonitor : "DISPLAY", NULL, NULL, NULL);
}

static void set_desktop_gamma(D3DGAMMARAMP * ramp)
{
	HDC hdc = create_monitor_dc();
	if (hdc) {
		SetDeviceGammaRamp(hdc, ramp);
		DeleteDC(hdc);
	}
}

static void save_desktop_gamma()
{
	if (DesktopGammaSaved) {
		return;
	}
	HDC hdc = create_monitor_dc();
	if (hdc) {
		DesktopGammaSaved = GetDeviceGammaRamp(hdc, &DesktopGammaRamp) != false;
		DeleteDC(hdc);
	}
}

static void restore_desktop_display()
{
	if (DesktopGammaSaved) {
		set_desktop_gamma(&DesktopGammaRamp);
	}
}

// Black over the parts of the monitor a picture of another shape leaves bare.  It never takes the
// focus and has no taskbar button; the game's window sits on top of it.
static HWND FullscreenBackdrop = NULL;
static bool FullscreenKeepAspect = false;

static void hide_backdrop()
{
	if (FullscreenBackdrop != NULL) {
		::ShowWindow(FullscreenBackdrop, SW_HIDE);
	}
}

static void show_backdrop(int x, int y, int width, int height)
{
	if (FullscreenBackdrop == NULL) {
		WNDCLASSA backdrop_class;
		ZeroMemory(&backdrop_class, sizeof(backdrop_class));
		backdrop_class.lpfnWndProc = DefWindowProcA;
		backdrop_class.hInstance = ::GetModuleHandleA(NULL);
		backdrop_class.hbrBackground = (HBRUSH)::GetStockObject(BLACK_BRUSH);
		backdrop_class.hCursor = ::LoadCursor(NULL, IDC_ARROW);
		backdrop_class.lpszClassName = "ZHFullscreenBackdrop";
		::RegisterClassA(&backdrop_class);
		FullscreenBackdrop = ::CreateWindowExA(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TOPMOST,
			backdrop_class.lpszClassName, "", WS_POPUP, x, y, width, height, NULL, NULL,
			backdrop_class.hInstance, NULL);
		if (FullscreenBackdrop == NULL) {
			return;
		}
	}
	::SetWindowPos(FullscreenBackdrop, HWND_TOPMOST, x, y, width, height, SWP_SHOWWINDOW | SWP_NOACTIVATE);
}

void DX8Wrapper::Set_Requested_Fullscreen_Keep_Aspect(bool keep)
{
	FullscreenKeepAspect = keep;
}

void DX8Wrapper::Set_Requested_Monitor(const char * device)
{
	if (strcmp(device, RequestedMonitor) == 0) {
		return;
	}
	// The monitor being left gets its own mode and ramp back before the game takes another one, and
	// the next ramp saved is the new monitor's.
	restore_desktop_display();
	DesktopGammaSaved = false;
	strncpy(RequestedMonitor, device, sizeof(RequestedMonitor) - 1);
	RequestedMonitor[sizeof(RequestedMonitor) - 1] = 0;
}

void DX8Wrapper::Apply_Fullscreen_Display(bool shown)
{
	// Nothing here activates a window.  This runs when the game is activated or deactivated, and a
	// window activated from inside that bounced focus between the game and the desktop until it
	// ended minimized for good (issue #45): SW_MINIMIZE hands activation to whatever is next in the
	// z-order, SW_RESTORE and a SetWindowPos without SWP_NOACTIVATE take it back.
	//
	// No display mode is changed, whatever the resolution.  The game used to set the monitor to the
	// picture's size and lay its window over it, and that is what put a fullscreen game below the
	// monitor's own size in a window: wherever the change was refused, or the driver or Windows'
	// display scaling did not stretch the new mode to the panel, a window of the picture's size sat
	// in the corner of the desktop.  Measured on 1920x1080 here: 1280x720 also moved the second
	// monitor from x 1920 to x 1280 and every window on it with it.  The picture is scaled instead:
	// the swap chain's buffers keep the game's size and Present stretches them to the window.
	//
	const bool owns_display = !IsWindowed;
	if (!owns_display || !shown) {
		restore_desktop_display();
		hide_backdrop();
		if (owns_display) {
			::ShowWindow(_Hwnd, SW_SHOWMINNOACTIVE);
		}
		return;
	}

	const char * monitor = RequestedMonitor[0] ? RequestedMonitor : NULL;
	DEVMODEA current;
	memset(&current,0, sizeof(current));
	current.dmSize = sizeof(current);
	if (!EnumDisplaySettingsExA(monitor, ENUM_CURRENT_SETTINGS, &current, 0)) {
		current.dmPelsWidth = ResolutionWidth;
		current.dmPelsHeight = ResolutionHeight;
	}

	// Only the primary starts at the desktop's origin.
	POINT origin = { 0, 0 };
	if (monitor) {
		origin.x = current.dmPosition.x;
		origin.y = current.dmPosition.y;
	}

	const FullscreenFitRect fit = Fullscreen_Fit(current.dmPelsWidth, current.dmPelsHeight,
		ResolutionWidth, ResolutionHeight, FullscreenKeepAspect);
	if (fit.width != (int)current.dmPelsWidth || fit.height != (int)current.dmPelsHeight) {
		show_backdrop(origin.x, origin.y, current.dmPelsWidth, current.dmPelsHeight);
	} else {
		hide_backdrop();
	}

	if (::IsIconic(_Hwnd)) {
		::ShowWindow(_Hwnd, SW_SHOWNOACTIVATE);
	}
	// after the backdrop, so the game is the topmost of the two
	::SetWindowPos(_Hwnd, HWND_TOPMOST, origin.x + fit.x, origin.y + fit.y, fit.width, fit.height,
		SWP_SHOWWINDOW | SWP_NOACTIVATE);

	if (GameGammaSet) {
		save_desktop_gamma();	// the monitor may have changed since the game's ramp was last set
		set_desktop_gamma(&GameGammaRamp);
	}
}

#else
// Off Windows the display mode, the monitor and the desktop's gamma ramp are the window's (C2) and the
// swap chain's (A3); the device changes none of them, so there is nothing here to set or put back.
static bool GameGammaSet = false;
static D3DGAMMARAMP GameGammaRamp;
static void set_desktop_gamma(D3DGAMMARAMP *) {}
static void save_desktop_gamma() {}
static void restore_desktop_display() {}
void DX8Wrapper::Set_Requested_Monitor(const char *) {}
void DX8Wrapper::Apply_Fullscreen_Display(bool) {}
void DX8Wrapper::Set_Requested_Fullscreen_Keep_Aspect(bool) {}
#endif

bool DX8Wrapper::Reset_Device(bool reload_assets)
{
	WWDEBUG_SAY(("Resetting device.\n"));
	DX8_THREAD_ASSERT();
	if ((IsInitted) && (D3DDevice != NULL)) {
		// The textures the device holds bound at Reset() are the ones it held before deferring.
		Flush_Deferred_D3D9_State();

		// Release all non-MANAGED stuff
		WW3D::_Invalidate_Textures();

		for (unsigned i=0;i<MAX_VERTEX_STREAMS;++i) 
		{
			Set_Vertex_Buffer (NULL,i);
		}
		Set_Index_Buffer (NULL, 0);
		if (m_pCleanupHook) {
			m_pCleanupHook->ReleaseResources();
		}
		DynamicVBAccessClass::_Deinit();
		DynamicIBAccessClass::_Deinit();
		DX8TextureManagerClass::Release_Textures();
		SHD_SHUTDOWN_SHADERS;

		// D3DPOOL_DEFAULT: must be gone before Reset(), and the back buffer may change size
		if (_RTTDepthBuffer) { _RTTDepthBuffer->Release(); _RTTDepthBuffer = NULL; }

		// Reset frame count to reflect the flipping chain being reset by Reset()
		FrameCount = 0;

		memset(Vertex_Shader_Constants,0,sizeof(Vector4)*MAX_VERTEX_SHADER_CONSTANTS);
		memset(Pixel_Shader_Constants,0,sizeof(Vector4)*MAX_PIXEL_SHADER_CONSTANTS);

		RenderResult hr=_Get_D3D_Device()->TestCooperativeLevel();
		if (hr != D3DERR_DEVICELOST )
		{	DX8CALL_HRES(Reset(&_PresentParameters),hr)
			if (hr != D3D_OK)
				return false;	//reset failed.
		}
		else
			return false;	//device is lost and can't be reset.

		if (reload_assets)
		{
			DX8TextureManagerClass::Recreate_Textures();
			if (m_pCleanupHook) {
				m_pCleanupHook->ReAcquireResources();
			}
		}
		Invalidate_Cached_Render_States();
		Set_Default_Global_Render_States();
		SHD_INIT_SHADERS;
		WWDEBUG_SAY(("Device reset completed\n"));
		return true;
	}
	WWDEBUG_SAY(("Device reset failed\n"));
	return false;
}

void DX8Wrapper::Release_Device(void)
{
	if (D3DDevice) {

		// So that no pending bit outlives this device and is sent to the next one.
		Flush_Deferred_D3D9_State();

		// The device made every shader in the combiner cache and outliving it is not something they
		// can do.  Nothing recompiles them: the next draw asks the cache again and it is empty.
		CombinerShaderCache_Release();

		if (_RTTDepthBuffer) { _RTTDepthBuffer->Release(); _RTTDepthBuffer = NULL; }

		for (int a=0;a<MAX_TEXTURE_STAGES;++a)
		{	//release references to any textures that were used in last rendering call
			DX8CALL(SetTexture(a,NULL));
		}

		DX8CALL(SetStreamSource(0, NULL, 0, 0));	//release reference count on last rendered vertex buffer
		DX8CALL(SetIndices(NULL));	//release reference count on last rendered index buffer


		/*
		** Release the current vertex and index buffers
		*/
		for (unsigned i=0;i<MAX_VERTEX_STREAMS;++i) 
		{
			if (render_state.vertex_buffers[i]) render_state.vertex_buffers[i]->Release_Engine_Ref();
			REF_PTR_RELEASE(render_state.vertex_buffers[i]);
		}
		if (render_state.index_buffer) render_state.index_buffer->Release_Engine_Ref();
		REF_PTR_RELEASE(render_state.index_buffer);

		/*
		** Shutdown all subsystems
		*/
		Do_Onetime_Device_Dependent_Shutdowns();

		/*
		** Release the device
		*/

		D3DDevice->Release();
		D3DDevice=NULL;
	}
}

void DX8Wrapper::Enumerate_Devices()
{
	DX8_Assert();

	int adapter_count = D3DInterface->GetAdapterCount();
	for (int adapter_index=0; adapter_index<adapter_count; adapter_index++) {

		D3DADAPTER_IDENTIFIER9 id;
		memset(&id,0, sizeof(D3DADAPTER_IDENTIFIER9));
		RenderResult res = D3DInterface->GetAdapterIdentifier(adapter_index,NO_ADAPTER_IDENTIFIER_FLAGS,&id);

		if (res == D3D_OK) {

			/*
			** Set up the render device description
			** TODO: Fill in more fields of the render device description?  (need some lookup tables)
			*/
			RenderDeviceDescClass desc;
			desc.set_device_name(id.Description);
			desc.set_driver_name(id.Driver);

			char buf[64];
#if defined(_WIN32)
			sprintf(buf,"%d.%d.%d.%d", //"%04x.%04x.%04x.%04x",
				HIWORD(id.DriverVersion.HighPart),
				LOWORD(id.DriverVersion.HighPart),
				HIWORD(id.DriverVersion.LowPart),
				LOWORD(id.DriverVersion.LowPart));
#else
			// DriverVersion is an int64_t off Windows: LARGE_INTEGER's halves, then their 16-bit words.
			const unsigned long long driver_version = (unsigned long long)id.DriverVersion;
			sprintf(buf,"%d.%d.%d.%d",
				(int)((driver_version >> 48) & 0xFFFF),
				(int)((driver_version >> 32) & 0xFFFF),
				(int)((driver_version >> 16) & 0xFFFF),
				(int)(driver_version & 0xFFFF));
#endif

			desc.set_driver_version(buf);

			D3DInterface->GetDeviceCaps(adapter_index,WW3D_DEVTYPE,&desc.Caps);
			D3DInterface->GetAdapterIdentifier(adapter_index,NO_ADAPTER_IDENTIFIER_FLAGS,&desc.AdapterIdentifier);

			DX8Caps dx8caps(D3DInterface,desc.Caps,WW3D_FORMAT_UNKNOWN,desc.AdapterIdentifier);

			/*
			** Enumerate the resolutions
			*/
			desc.reset_resolution_list();
			// D3D8 enumerated every display mode of an adapter in one list and left the
			// caller to read the format off each one.  D3D9 enumerates per format, so the
			// formats the switch below used to recognise are the loop now, and the bit
			// depth comes from the format asked for rather than the mode returned.
			static const struct { D3DFORMAT Format; int Bits; } DISPLAY_FORMATS[] =
			{
				{ D3DFMT_X8R8G8B8, 32 },
				{ D3DFMT_A8R8G8B8, 32 },
				{ D3DFMT_R8G8B8,   32 },
				{ D3DFMT_R5G6B5,   16 },
				{ D3DFMT_X1R5G5B5, 16 }
			};
			const int DISPLAY_FORMAT_COUNT = sizeof(DISPLAY_FORMATS)/sizeof(DISPLAY_FORMATS[0]);

			for (int format_index=0; format_index<DISPLAY_FORMAT_COUNT; format_index++) {
				const D3DFORMAT display_format = DISPLAY_FORMATS[format_index].Format;
				int mode_count = D3DInterface->GetAdapterModeCount(adapter_index,display_format);
				for (int mode_index=0; mode_index<mode_count; mode_index++) {
					D3DDISPLAYMODE d3dmode;
					memset(&d3dmode,0, sizeof(D3DDISPLAYMODE));
					RenderResult res = D3DInterface->EnumAdapterModes(adapter_index,display_format,mode_index,&d3dmode);
					if (res != D3D_OK) continue;

					// Some cards fail in certain modes, DX8Caps keeps list of those.
					if (!dx8caps.Is_Valid_Display_Format(d3dmode.Width,d3dmode.Height,D3DFormat_To_WW3DFormat(d3dmode.Format))) {
						continue;
					}

					desc.add_resolution(d3dmode.Width,d3dmode.Height,DISPLAY_FORMATS[format_index].Bits);
				}
			}

			// IML: If the device has one or more valid resolutions add it to the device list.
			// NOTE: Testing has shown that there are drivers with zero resolutions.
			if (desc.Enumerate_Resolutions().Count() > 0) {

				/*
				** Set up the device name
				*/
				StringClass device_name(id.Description,true);
				_RenderDeviceNameTable.Add(device_name);
				_RenderDeviceShortNameTable.Add(device_name);	// for now, just add the same name to the "pretty name table"

				/*
				** Add the render device to our table
				*/
				_RenderDeviceDescriptionTable.Add(desc);
			}
		}
	}
}

bool DX8Wrapper::Set_Any_Render_Device(void)
{
	// Then fullscreen
	int dev_number;
	for (dev_number = 0; dev_number < _RenderDeviceNameTable.Count(); dev_number++) {
		if (Set_Render_Device(dev_number,-1,-1,-1,0,false)) {
			return true;
		}
	}

	// Try windowed first
	for (dev_number = 0; dev_number < _RenderDeviceNameTable.Count(); dev_number++) {
		if (Set_Render_Device(dev_number,-1,-1,-1,1,false)) {
			return true;
		}
	}

	return false;
}

bool DX8Wrapper::Set_Render_Device
(
	const char * dev_name,
	int width,
	int height,
	int bits,
	int windowed,
	bool resize_window
)
{
	for ( int dev_number = 0; dev_number < _RenderDeviceNameTable.Count(); dev_number++) {
		if ( strcmp( dev_name, _RenderDeviceNameTable[dev_number]) == 0) {
			return Set_Render_Device( dev_number, width, height, bits, windowed, resize_window );
		}

		if ( strcmp( dev_name, _RenderDeviceShortNameTable[dev_number]) == 0) {
			return Set_Render_Device( dev_number, width, height, bits, windowed, resize_window );
		}
	}
	return false;
}

void DX8Wrapper::Get_Format_Name(unsigned int format, StringClass *tex_format)
{
		*tex_format="Unknown";
		switch (format) {
		case D3DFMT_A8R8G8B8: *tex_format="D3DFMT_A8R8G8B8"; break;
		case D3DFMT_R8G8B8: *tex_format="D3DFMT_R8G8B8"; break;
		case D3DFMT_A4R4G4B4: *tex_format="D3DFMT_A4R4G4B4"; break;
		case D3DFMT_A1R5G5B5: *tex_format="D3DFMT_A1R5G5B5"; break;
		case D3DFMT_R5G6B5: *tex_format="D3DFMT_R5G6B5"; break;
		case D3DFMT_L8: *tex_format="D3DFMT_L8"; break;
		case D3DFMT_A8: *tex_format="D3DFMT_A8"; break;
		case D3DFMT_P8: *tex_format="D3DFMT_P8"; break;
		case D3DFMT_X8R8G8B8: *tex_format="D3DFMT_X8R8G8B8"; break;
		case D3DFMT_X1R5G5B5: *tex_format="D3DFMT_X1R5G5B5"; break;
		case D3DFMT_R3G3B2: *tex_format="D3DFMT_R3G3B2"; break;
		case D3DFMT_A8R3G3B2: *tex_format="D3DFMT_A8R3G3B2"; break;
		case D3DFMT_X4R4G4B4: *tex_format="D3DFMT_X4R4G4B4"; break;
		case D3DFMT_A8P8: *tex_format="D3DFMT_A8P8"; break;
		case D3DFMT_A8L8: *tex_format="D3DFMT_A8L8"; break;
		case D3DFMT_A4L4: *tex_format="D3DFMT_A4L4"; break;
		case D3DFMT_V8U8: *tex_format="D3DFMT_V8U8"; break;
		case D3DFMT_L6V5U5: *tex_format="D3DFMT_L6V5U5"; break;  
		case D3DFMT_X8L8V8U8: *tex_format="D3DFMT_X8L8V8U8"; break;
		case D3DFMT_Q8W8V8U8: *tex_format="D3DFMT_Q8W8V8U8"; break;
		case D3DFMT_V16U16: *tex_format="D3DFMT_V16U16"; break;
		case D3DFMT_UYVY: *tex_format="D3DFMT_UYVY"; break;
		case D3DFMT_YUY2: *tex_format="D3DFMT_YUY2"; break;
		case D3DFMT_DXT1: *tex_format="D3DFMT_DXT1"; break;
		case D3DFMT_DXT2: *tex_format="D3DFMT_DXT2"; break;
		case D3DFMT_DXT3: *tex_format="D3DFMT_DXT3"; break;
		case D3DFMT_DXT4: *tex_format="D3DFMT_DXT4"; break;
		case D3DFMT_DXT5: *tex_format="D3DFMT_DXT5"; break;
		case D3DFMT_D16_LOCKABLE: *tex_format="D3DFMT_D16_LOCKABLE"; break;
		case D3DFMT_D32: *tex_format="D3DFMT_D32"; break;
		case D3DFMT_D15S1: *tex_format="D3DFMT_D15S1"; break;
		case D3DFMT_D24S8: *tex_format="D3DFMT_D24S8"; break;
		case D3DFMT_D16: *tex_format="D3DFMT_D16"; break;
		case D3DFMT_D24X8: *tex_format="D3DFMT_D24X8"; break;
		case D3DFMT_D24X4S4: *tex_format="D3DFMT_D24X4S4"; break;
		default:	break;
		}
}

bool DX8Wrapper::Set_Render_Device(int dev, int width, int height, int bits, int windowed,
								   bool resize_window,bool reset_device, bool restore_assets)
{
	WWASSERT(IsInitted);
	WWASSERT(dev >= -1);
	WWASSERT(dev < _RenderDeviceNameTable.Count());

	/*
	** If user has never selected a render device, start out with device 0
	*/
	if (CurRenderDevice == -1) {
		// 0 from W3DDisplay is "the default adapter", not "whichever Intel sits at index 0".
		if (dev == -1 || dev == 0)
			CurRenderDevice = Find_Preferred_Render_Device();
		else
			CurRenderDevice = dev;
	} else if (dev != -1 && dev != 0) {
		CurRenderDevice = dev;
	}
	
	/*
	** If user doesn't want to change res, set the res variables to match the 
	** current resolution
	*/
	if (width != -1)		ResolutionWidth = width;
	if (height != -1)		ResolutionHeight = height;
	
	// Initialize Render2DClass Screen Resolution
	Render2DClass::Set_Screen_Resolution( RectClass( 0, 0, ResolutionWidth, ResolutionHeight ) );

	if (bits != -1)		BitDepth = bits;
	if (windowed != -1)	IsWindowed = (windowed != 0);
	DX8Wrapper_IsWindowed = IsWindowed;

	WWDEBUG_SAY(("Attempting Set_Render_Device: name: %s (%s:%s), width: %d, height: %d, windowed: %d\n",
		_RenderDeviceNameTable[CurRenderDevice],_RenderDeviceDescriptionTable[CurRenderDevice].Get_Driver_Name(),
		_RenderDeviceDescriptionTable[CurRenderDevice].Get_Driver_Version(),ResolutionWidth,ResolutionHeight,(IsWindowed ? 1 : 0)));

#ifdef _WINDOWS
	// PWG 4/13/2000 - changed so that if you say to resize the window it resizes
	// regardless of whether its windowed or not as OpenGL resizes its self around
	// the caption and edges of the window type you provide, so its important to 
	// push the client area to be the size you really want.
	// if ( resize_window && windowed ) {
	if (resize_window) {

		// Get the current dimensions of the 'render area' of the window
		RenderRect rect = { 0 };
		::GetClientRect (_Hwnd, &rect);

		// Is the window the correct size for this resolution?
		if ((rect.right-rect.left) != ResolutionWidth ||
			 (rect.bottom-rect.top) != ResolutionHeight) {			
			
			// Calculate what the main window's bounding rectangle should be to
			// accomodate this resolution
			rect.left = 0;
			rect.top = 0;
			rect.right = ResolutionWidth;
			rect.bottom = ResolutionHeight;
			uint32 dwstyle = ::GetWindowLong (_Hwnd, GWL_STYLE);
			AdjustWindowRect (&rect, dwstyle, false);

			// Resize the window to fit this resolution
			if (!windowed)
				::SetWindowPos(_Hwnd, HWND_TOPMOST, 0, 0, rect.right-rect.left, rect.bottom-rect.top,SWP_NOSIZE |SWP_NOMOVE);
			else
				::SetWindowPos (_Hwnd,
								 NULL,
								 0,
								 0,
								 rect.right-rect.left,
								 rect.bottom-rect.top,
								 SWP_NOZORDER | SWP_NOMOVE);
		}
	}
#endif
	//must be either resetting existing device or creating a new one.
	WWASSERT(reset_device || D3DDevice == NULL);

	// The Direct3D 11 device is built beside the Direct3D 9 one rather than instead of it: the
	// engine still calls the D3D9 device directly from 148 places outside this wrapper (measured
	// 2026-09-22 by a call-site survey), so taking it away would be a black
	// screen.  It is on unless -d3d9 or -headless turned it off.  It comes first, so everything the
	// Direct3D 9 device makes has a Direct3D 11 copy, and only with a new device: a device refused
	// at startup and created on a reset (an Alt-Tab) drew every building already standing as its
	// shadow alone.
	if (!reset_device && Direct3D11_Is_Enabled() && !Direct3D11_Is_Active()) {
		const bool created = Direct3D11_Create(_Hwnd, ResolutionWidth, ResolutionHeight);	// RenderWindow: the HWND on Windows
		WWDEBUG_SAY(("-dx11: Direct3D 11 device %s\n", created ? "created" : "refused"));
	}

	// The Direct3D 9 device is windowed even in a fullscreen game.  One that owns the display refuses
	// the Direct3D 11 swap chain its window, which kept every fullscreen game on the old picture, and
	// under -d3d9 it would change the monitor's mode, which is what fullscreen no longer does (see
	// Apply_Fullscreen_Display).  A windowed device's Present stretches its back buffer to the window.
	// A player who alt-tabbed away during the splash is not handed a topmost window over whatever he
	// went to.  The game's activation applies the display when he comes back.
#if defined(_WIN32)
	const bool device_windowed = true;
#else
	const bool device_windowed = IsWindowed || Direct3D11_Present_Is_Enabled();
#endif
#if defined(_WIN32)
	if (::GetForegroundWindow() == _Hwnd) {
		Apply_Fullscreen_Display(true);
	}
#else
	Apply_Fullscreen_Display(true);	// empty off Windows: the window's owner sizes it (W3DDisplay's sizeWindowToClient)
#endif
	
	/*
	** Initialize values for D3DPRESENT_PARAMETERS members. 	
	*/
	memset(&_PresentParameters,0, sizeof(D3DPRESENT_PARAMETERS));

	_PresentParameters.BackBufferWidth = ResolutionWidth;
	_PresentParameters.BackBufferHeight = ResolutionHeight;
	_PresentParameters.BackBufferCount = device_windowed ? 1 : 2;
	
	_PresentParameters.MultiSampleType = D3DMULTISAMPLE_NONE;
	//I changed this to discard all the time (even when full-screen) since that the most efficient. 07-16-03 MW:
	_PresentParameters.SwapEffect = D3DSWAPEFFECT_DISCARD;//IsWindowed ? D3DSWAPEFFECT_DISCARD : D3DSWAPEFFECT_FLIP;		// Shouldn't this be D3DSWAPEFFECT_FLIP?
	_PresentParameters.hDeviceWindow = _Hwnd;
	_PresentParameters.Windowed = device_windowed;

	_PresentParameters.EnableAutoDepthStencil = true;				// Driver will attempt to match Z-buffer depth
	_PresentParameters.Flags=0;											// We're not going to lock the backbuffer
	
	//
	// D3D9 honours this in a window as well as fullscreen, unlike D3D8.  Off (IMMEDIATE) is the
	// uncapped picture; on (ONE) waits for the monitor.  The request is pushed in from the app
	// layer because WW3D2 cannot see GlobalData.
	//
	_PresentParameters.PresentationInterval = _RequestedVSync
		? D3DPRESENT_INTERVAL_ONE
		: D3DPRESENT_INTERVAL_IMMEDIATE;
	_PresentParameters.FullScreen_RefreshRateInHz = D3DPRESENT_RATE_DEFAULT;

	/*
	** Set up the buffer formats.  Several issues here:
	** - if in windowed mode, the backbuffer must use the current display format.
	** - the depth buffer must use 
	*/
	if (device_windowed) {

		D3DDISPLAYMODE desktop_mode;
		memset(&desktop_mode,0, sizeof(D3DDISPLAYMODE));
		D3DInterface->GetAdapterDisplayMode( CurRenderDevice, &desktop_mode );

		DisplayFormat=_PresentParameters.BackBufferFormat = desktop_mode.Format;

		// In windowed mode, define the bitdepth from desktop mode (as it can't be changed)
		switch (_PresentParameters.BackBufferFormat) {
		case D3DFMT_X8R8G8B8:
		case D3DFMT_A8R8G8B8:
		case D3DFMT_R8G8B8: BitDepth=32; break;
		case D3DFMT_A4R4G4B4:
		case D3DFMT_A1R5G5B5:
		case D3DFMT_R5G6B5: BitDepth=16; break;
		case D3DFMT_L8:
		case D3DFMT_A8:
		case D3DFMT_P8: BitDepth=8; break;
		default:
			// Unknown backbuffer format probably means the device can't do windowed
			return false;
		}

		if (BitDepth==32 && D3DInterface->CheckDeviceType(CurRenderDevice,D3DDEVTYPE_HAL,desktop_mode.Format,D3DFMT_A8R8G8B8, true) == D3D_OK)
		{	//promote 32-bit modes to include destination alpha
			_PresentParameters.BackBufferFormat = D3DFMT_A8R8G8B8;
		}

		/*
		** Find a appropriate Z buffer
		*/
		if (!Find_Z_Mode(DisplayFormat,_PresentParameters.BackBufferFormat,&_PresentParameters.AutoDepthStencilFormat))
		{
			// If opening 32 bit mode failed, try 16 bit, even if the desktop happens to be 32 bit
			if (BitDepth==32) {
				BitDepth=16;
				_PresentParameters.BackBufferFormat=D3DFMT_R5G6B5;
				if (!Find_Z_Mode(_PresentParameters.BackBufferFormat,_PresentParameters.BackBufferFormat,&_PresentParameters.AutoDepthStencilFormat)) {
					_PresentParameters.AutoDepthStencilFormat=D3DFMT_UNKNOWN;
				}
			}
			else {
				_PresentParameters.AutoDepthStencilFormat=D3DFMT_UNKNOWN;
			}
		}

	} else {

		/*
		** Try to find a mode that matches the user's desired bit-depth.
		*/
		if (!Find_Color_And_Z_Mode(ResolutionWidth,ResolutionHeight,BitDepth,&DisplayFormat,
			&_PresentParameters.BackBufferFormat,&_PresentParameters.AutoDepthStencilFormat)) {
			/* A size the adapter's mode list does not hold - the POSIX device lists fourteen, up to
			   2560x1440, and a fullscreen game takes the desktop's size, 3840x2160 on a 4K screen - left
			   both formats UNKNOWN.  The device was made anyway, but the caps then supported no texture
			   format at all, so no video buffer could be made: the EA logo, the sizzle and the Generals
			   Challenge load screen, portraits and all, never played.  Take the desktop's format, as the
			   windowed branch above does. */
			D3DDISPLAYMODE desktop_mode;
			memset(&desktop_mode,0, sizeof(D3DDISPLAYMODE));
			D3DInterface->GetAdapterDisplayMode( CurRenderDevice, &desktop_mode );
			DisplayFormat=_PresentParameters.BackBufferFormat=desktop_mode.Format;
			if (BitDepth==32 && D3DInterface->CheckDeviceType(CurRenderDevice,D3DDEVTYPE_HAL,desktop_mode.Format,D3DFMT_A8R8G8B8, true) == D3D_OK) {
				_PresentParameters.BackBufferFormat = D3DFMT_A8R8G8B8;
			}
			if (!Find_Z_Mode(DisplayFormat,_PresentParameters.BackBufferFormat,&_PresentParameters.AutoDepthStencilFormat)) {
				_PresentParameters.AutoDepthStencilFormat=D3DFMT_UNKNOWN;
			}
		}
	}

	/*
	** Time to actually create the device.
	*/
	if (_PresentParameters.AutoDepthStencilFormat==D3DFMT_UNKNOWN) {
		if (BitDepth==32) {
			_PresentParameters.AutoDepthStencilFormat=D3DFMT_D32;
		}
		else {
			_PresentParameters.AutoDepthStencilFormat=D3DFMT_D16;
		}
	}

	/*
	** Pick the multisampling level now that both formats are known.  D3D only allows
	** multisampling with SWAPEFFECT_DISCARD, which is what we use unconditionally.
	*/
	_PresentParameters.MultiSampleType = D3DMULTISAMPLE_NONE;
	{
		unsigned wanted = _RequestedMultiSampleLevel;
		for (unsigned samples = wanted; samples >= 2; --samples) {
			D3DMULTISAMPLE_TYPE type = (D3DMULTISAMPLE_TYPE)samples;
			if (Render_Failed(D3DInterface->CheckDeviceMultiSampleType(CurRenderDevice,D3DDEVTYPE_HAL,
					_PresentParameters.BackBufferFormat,_PresentParameters.Windowed,type,NULL))) continue;
			if (Render_Failed(D3DInterface->CheckDeviceMultiSampleType(CurRenderDevice,D3DDEVTYPE_HAL,
					_PresentParameters.AutoDepthStencilFormat,_PresentParameters.Windowed,type,NULL))) continue;
			_PresentParameters.MultiSampleType = type;
			break;
		}
		if (wanted) {
			WWDEBUG_SAY(("Multisampling: %ux requested, %ux in use\n",
				wanted,(unsigned)_PresentParameters.MultiSampleType));
		}
	}

	StringClass displayFormat;
	StringClass backbufferFormat;

	Get_Format_Name(DisplayFormat,&displayFormat);
	Get_Format_Name(_PresentParameters.BackBufferFormat,&backbufferFormat);

	WWDEBUG_SAY(("Using Display/BackBuffer Formats: %s/%s\n",displayFormat,backbufferFormat));
	
	bool ret;

	if (reset_device)
		ret = Reset_Device(restore_assets);	//reset device without restoring data - we're likely switching out of the app.
	else
		ret = Create_Device();

	WWDEBUG_SAY(("Reset/Create_Device done, reset_device=%d, restore_assets=%d\n", reset_device, restore_assets));

	Direct3D11_Resize(ResolutionWidth, ResolutionHeight);
	return ret;
}

bool DX8Wrapper::Set_Next_Render_Device(void)
{
	int new_dev = (CurRenderDevice + 1) % _RenderDeviceNameTable.Count();
	return Set_Render_Device(new_dev);
}

bool DX8Wrapper::Toggle_Windowed(void)
{
#ifdef WW3D_DX8
	// State OK?
	assert (IsInitted);
	if (IsInitted) {

		// Get information about the current render device's resolutions
		const RenderDeviceDescClass &render_device = Get_Render_Device_Desc ();
		const DynamicVectorClass<ResolutionDescClass> &resolutions = render_device.Enumerate_Resolutions ();

		// Loop through all the resolutions supported by the current device.
		// If we aren't currently running under one of these resolutions,
		// then we should probably		 to the closest resolution before
		// toggling the windowed state.
		int curr_res = -1;
		for (int res = 0;
		     (res < resolutions.Count ()) && (curr_res == -1);
			  res ++) {

			// Is this the resolution we are looking for?
			if ((resolutions[res].Width == ResolutionWidth) &&
				 (resolutions[res].Height == ResolutionHeight) &&
				 (resolutions[res].BitDepth == BitDepth)) {
				curr_res = res;
			}
		}

		if (curr_res == -1) {

			// We don't match any of the standard resolutions,
			// so set the first resolution and toggle the windowed state.
			return Set_Device_Resolution (resolutions[0].Width,
								 resolutions[0].Height,
								 resolutions[0].BitDepth,
								 !IsWindowed, true);
		} else {

			// Toggle the windowed state
			return Set_Device_Resolution (-1, -1, -1, !IsWindowed, true);
		}
	}
#endif //WW3D_DX8

	return false;
}

void DX8Wrapper::Set_Swap_Interval(int swap)
{
	switch (swap) {
		case 0: _PresentParameters.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE; break;
		case 1: _PresentParameters.PresentationInterval = D3DPRESENT_INTERVAL_ONE ; break;
		case 2: _PresentParameters.PresentationInterval = D3DPRESENT_INTERVAL_TWO; break;
		case 3: _PresentParameters.PresentationInterval = D3DPRESENT_INTERVAL_THREE; break;
		default: _PresentParameters.PresentationInterval = D3DPRESENT_INTERVAL_ONE ; break;
	}

	Reset_Device();
}

int DX8Wrapper::Get_Swap_Interval(void)
{
	return _PresentParameters.PresentationInterval;
}

bool DX8Wrapper::Has_Stencil(void)
{
	bool has_stencil = (_PresentParameters.AutoDepthStencilFormat == D3DFMT_D24S8 ||
						_PresentParameters.AutoDepthStencilFormat == D3DFMT_D24X4S4);
	return has_stencil;
}

int DX8Wrapper::Get_Render_Device_Count(void)
{
	return _RenderDeviceNameTable.Count();

}
int DX8Wrapper::Get_Render_Device(void)
{
	assert(IsInitted);
	return CurRenderDevice;
}

const RenderDeviceDescClass & DX8Wrapper::Get_Render_Device_Desc(int deviceidx)
{
	WWASSERT(IsInitted);

	if ((deviceidx == -1) && (CurRenderDevice == -1)) {
		CurRenderDevice = 0;
	}

	// if the device index is -1 then we want the current device
	if (deviceidx == -1) {
		WWASSERT(CurRenderDevice >= 0);
		WWASSERT(CurRenderDevice < _RenderDeviceNameTable.Count());
		return _RenderDeviceDescriptionTable[CurRenderDevice];
	}

	// We can only ask for multiple device information if the devices
	// have been detected.
	WWASSERT(deviceidx >= 0);
	WWASSERT(deviceidx < _RenderDeviceNameTable.Count());
	return _RenderDeviceDescriptionTable[deviceidx];
}

const char * DX8Wrapper::Get_Render_Device_Name(int device_index)
{
	device_index = device_index % _RenderDeviceShortNameTable.Count();
	return _RenderDeviceShortNameTable[device_index];
}

bool DX8Wrapper::Set_Device_Resolution(int width,int height,int bits,int windowed, bool resize_window)
{
	if (D3DDevice != NULL) {

		if (width != -1) {
			_PresentParameters.BackBufferWidth = ResolutionWidth = width;
		}
		if (height != -1) {
			_PresentParameters.BackBufferHeight = ResolutionHeight = height;
		}
#if defined(_WIN32)	// off Windows the window is C2's to size
		if (resize_window)
		{

			// Get the current dimensions of the 'render area' of the window
			RenderRect rect = { 0 };
			::GetClientRect (_Hwnd, &rect);

			// Is the window the correct size for this resolution?
			if ((rect.right-rect.left) != ResolutionWidth ||
				 (rect.bottom-rect.top) != ResolutionHeight)
			{			
				
				// Calculate what the main window's bounding rectangle should be to
				// accomodate this resolution
				rect.left = 0;
				rect.top = 0;
				rect.right = ResolutionWidth;
				rect.bottom = ResolutionHeight;
				uint32 dwstyle = ::GetWindowLong (_Hwnd, GWL_STYLE);
				AdjustWindowRect (&rect, dwstyle, false);

				// Resize the window to fit this resolution
				if (!windowed)
					::SetWindowPos(_Hwnd, HWND_TOPMOST, 0, 0, rect.right-rect.left, rect.bottom-rect.top,SWP_NOSIZE |SWP_NOMOVE);
				else
					::SetWindowPos (_Hwnd,
									 NULL,
									 0,
									 0,
									 rect.right-rect.left,
									 rect.bottom-rect.top,
									 SWP_NOZORDER | SWP_NOMOVE);
			}
		}
#endif
#pragma message("TODO: support changing windowed status and changing the bit depth")
		Apply_Fullscreen_Display(true);
		const bool reset = Reset_Device();
		Direct3D11_Resize(ResolutionWidth, ResolutionHeight);
		return reset;
	} else {
		return false;
	}
}

void DX8Wrapper::Get_Device_Resolution(int & set_w,int & set_h,int & set_bits,bool & set_windowed)
{
	WWASSERT(IsInitted);

	set_w = ResolutionWidth;
	set_h = ResolutionHeight;
	set_bits = BitDepth;
	set_windowed = IsWindowed;

	return ;
}

void DX8Wrapper::Get_Render_Target_Resolution(int & set_w,int & set_h,int & set_bits,bool & set_windowed)
{
	WWASSERT(IsInitted);

	if (CurrentRenderTarget != NULL) {
		D3DSURFACE_DESC info;
		CurrentRenderTarget->GetDesc (&info);

		set_w				= info.Width;
		set_h				= info.Height;
		set_bits			= BitDepth;		// should we get the actual bit depth of the target?
		set_windowed	= IsWindowed;	// this doesn't really make sense for render targets (shouldn't matter)...

	} else {
		Get_Device_Resolution (set_w, set_h, set_bits, set_windowed);
	}

	return ;
}

#if defined(_WIN32)	// the render device's settings in the Windows registry, which only Windows has
bool DX8Wrapper::Registry_Save_Render_Device( const char * sub_key )
{
	int	width, height, depth;
	bool	windowed;
	Get_Device_Resolution(width, height, depth, windowed);
	return Registry_Save_Render_Device(sub_key, CurRenderDevice, ResolutionWidth, ResolutionHeight, BitDepth, IsWindowed, TextureBitDepth);
}

bool DX8Wrapper::Registry_Save_Render_Device( const char *sub_key, int device, int width, int height, int depth, bool windowed, int texture_depth)
{
	RegistryClass * registry = W3DNEW RegistryClass( sub_key );
	WWASSERT( registry );

	if ( !registry->Is_Valid() ) {
		delete registry;
		WWDEBUG_SAY(( "Error getting Registry\n" ));
		return false;
	}

	registry->Set_String( VALUE_NAME_RENDER_DEVICE_NAME,
		_RenderDeviceShortNameTable[device] );
	registry->Set_Int( VALUE_NAME_RENDER_DEVICE_WIDTH,	width );
	registry->Set_Int( VALUE_NAME_RENDER_DEVICE_HEIGHT, height );
	registry->Set_Int( VALUE_NAME_RENDER_DEVICE_DEPTH, depth );
	registry->Set_Int( VALUE_NAME_RENDER_DEVICE_WINDOWED, windowed );
	registry->Set_Int( VALUE_NAME_RENDER_DEVICE_TEXTURE_DEPTH, texture_depth );

	delete registry;
	return true;
}

bool DX8Wrapper::Registry_Load_Render_Device( const char * sub_key, bool resize_window )
{
	char	name[ 200 ];
	int	width,height,depth,windowed;

	if (	Registry_Load_Render_Device(	sub_key,
													name,
													sizeof(name),
													width,
													height,
													depth,
													windowed,
													TextureBitDepth) &&
			(*name != 0))
	{
		WWDEBUG_SAY(( "Device %s (%d X %d) %d bit windowed:%d\n", name,width,height,depth,windowed));

		if (TextureBitDepth==16 || TextureBitDepth==32) {
//			WWDEBUG_SAY(( "Texture depth %d\n", TextureBitDepth));
		} else {
			WWDEBUG_SAY(( "Invalid texture depth %d, switching to 16 bits\n", TextureBitDepth));
			TextureBitDepth=16;
		}


//		_RenderDeviceDescriptionTable.


		if ( Set_Render_Device( name, width,height,depth,windowed, resize_window ) != true) {
			if (depth==16) depth=32;
			else depth=16;
			if ( Set_Render_Device( name, width,height,depth,windowed, resize_window ) == true) {
				return true;
			}
			if (depth==16) depth=32;
			else depth=16;
			// we'll test resolutions down, so if start is 640, increase to begin with...
			if (width==640) {
				width=1024;
				height=768;
			}
			for(;;) {
				if (width>2048) {
					width=2048;
					height=1536;
				}
				else if (width>1920) {
					width=1920;
					height=1440;
				}
				else if (width>1600) {
					width=1600;
					height=1200;
				}
				else if (width>1280) {
					width=1280;
					height=1024;
				}
				else if (width>1024) {
					width=1024;
					height=768;
				}
				else if (width>800) {
					width=800;
					height=600;
				}
				else if (width!=640) {
					width=640;
					height=480;
				}
				else {
					return Set_Any_Render_Device();
				}
				for (int i=0;i<2;++i) {
					if ( Set_Render_Device( name, width,height,depth,windowed, resize_window ) == true) {
						return true;
					}
					if (depth==16) depth=32;
					else depth=16;
				}
			}
		}

		return true;
	}

	WWDEBUG_SAY(( "Error getting Registry\n" ));

	return Set_Any_Render_Device();
}

bool DX8Wrapper::Registry_Load_Render_Device( const char * sub_key, char *device, int device_len, int &width, int &height, int &depth, int &windowed, int &texture_depth)
{
	RegistryClass registry( sub_key );

	if ( registry.Is_Valid() ) {
		registry.Get_String( VALUE_NAME_RENDER_DEVICE_NAME,
			device, device_len);

		width =		registry.Get_Int( VALUE_NAME_RENDER_DEVICE_WIDTH, -1 );
		height =		registry.Get_Int( VALUE_NAME_RENDER_DEVICE_HEIGHT, -1 );
		depth =		registry.Get_Int( VALUE_NAME_RENDER_DEVICE_DEPTH, -1 );
		windowed =	registry.Get_Int( VALUE_NAME_RENDER_DEVICE_WINDOWED, -1 );
		texture_depth = registry.Get_Int( VALUE_NAME_RENDER_DEVICE_TEXTURE_DEPTH, -1 );
		return true;
	}
	*device=0;
	width=-1;
	height=-1;
	depth=-1;
	windowed=-1;
	texture_depth=-1;
	return false;
}
#endif // _WIN32


bool DX8Wrapper::Find_Color_And_Z_Mode(int resx,int resy,int bitdepth,D3DFORMAT * set_colorbuffer,D3DFORMAT * set_backbuffer,D3DFORMAT * set_zmode)
{
	static D3DFORMAT _formats16[] =
	{
		D3DFMT_R5G6B5,
		D3DFMT_X1R5G5B5,
		D3DFMT_A1R5G5B5
	};

	static D3DFORMAT _formats32[] =
	{
		D3DFMT_A8R8G8B8,
		D3DFMT_X8R8G8B8,
		D3DFMT_R8G8B8,
	};

	/*
	** Select the table that we're going to use to search for a valid backbuffer format
	*/
	D3DFORMAT * format_table = NULL;
	int format_count = 0;

	if (BitDepth == 16) {
		format_table = _formats16;
		format_count = sizeof(_formats16) / sizeof(D3DFORMAT);
	} else {
		format_table = _formats32;
		format_count = sizeof(_formats32) / sizeof(D3DFORMAT);
	}

	/*
	** now search for a valid format
	*/
	bool found = false;
	unsigned int mode = 0;

	int format_index;
	for (format_index =0; format_index < format_count; format_index++) {
		found |= Find_Color_Mode(format_table[format_index],resx,resy,&mode);
		if (found) break;
	}

	if (!found) {
		return false;
	} else {
		*set_backbuffer=*set_colorbuffer = format_table[format_index];
	}

	if (bitdepth==32 && *set_colorbuffer == D3DFMT_X8R8G8B8 && D3DInterface->CheckDeviceType(0,D3DDEVTYPE_HAL,*set_colorbuffer,D3DFMT_A8R8G8B8, true) == D3D_OK)
	{	//promote 32-bit modes to include destination alpha when supported
		*set_backbuffer = D3DFMT_A8R8G8B8;
	}

	/*
	** We found a backbuffer format, now find a zbuffer format
	*/
	return Find_Z_Mode(*set_colorbuffer,*set_backbuffer, set_zmode);
};


// find the resolution mode with at least resx,resy with the highest supported
// refresh rate
bool DX8Wrapper::Find_Color_Mode(D3DFORMAT colorbuffer, int resx, int resy, UINT *mode)
{
	UINT i,j,modemax;
	UINT rx,ry;
	D3DDISPLAYMODE dmode;
	memset(&dmode,0, sizeof(D3DDISPLAYMODE));

	rx=(unsigned int) resx;
	ry=(unsigned int) resy;

	bool found=false;

	modemax=D3DInterface->GetAdapterModeCount(D3DADAPTER_DEFAULT,colorbuffer);

	i=0;

	while (i<modemax && !found)
	{
		D3DInterface->EnumAdapterModes(D3DADAPTER_DEFAULT, colorbuffer, i, &dmode);
		if (dmode.Width==rx && dmode.Height==ry && dmode.Format==colorbuffer) {
			WWDEBUG_SAY(("Found valid color mode.  Width = %d Height = %d Format = %d\r\n",dmode.Width,dmode.Height,dmode.Format));
			found=true;
		}
		i++;
	}

	i--; // this is the first valid mode

	// no match
	if (!found) {
		WWDEBUG_SAY(("Failed to find a valid color mode\r\n"));
		return false;
	}

	// go to the highest refresh rate in this mode
	bool stillok=true;

	j=i;
	while (j<modemax && stillok)
	{
		D3DInterface->EnumAdapterModes(D3DADAPTER_DEFAULT, colorbuffer, j, &dmode);
		if (dmode.Width==rx && dmode.Height==ry && dmode.Format==colorbuffer)
			stillok=true; else stillok=false;
		j++;
	}

	if (stillok==false) *mode=j-2;
	else *mode=i;

	return true;
}

// Helper function to find a Z buffer mode for the colorbuffer
// Will look for greatest Z precision
bool DX8Wrapper::Find_Z_Mode(D3DFORMAT colorbuffer,D3DFORMAT backbuffer, D3DFORMAT *zmode)
{
	//MW: Swapped the next 2 tests so that Stencil modes get tested first.
	if (Test_Z_Mode(colorbuffer,backbuffer,D3DFMT_D24S8))
	{
		*zmode=D3DFMT_D24S8;
		WWDEBUG_SAY(("Found zbuffer mode D3DFMT_D24S8\n"));
		return true;
	}

	if (Test_Z_Mode(colorbuffer,backbuffer,D3DFMT_D32))
	{
		*zmode=D3DFMT_D32;
		WWDEBUG_SAY(("Found zbuffer mode D3DFMT_D32\n"));
		return true;
	}

	if (Test_Z_Mode(colorbuffer,backbuffer,D3DFMT_D24X8))
	{
		*zmode=D3DFMT_D24X8;
		WWDEBUG_SAY(("Found zbuffer mode D3DFMT_D24X8\n"));
		return true;
	}

	if (Test_Z_Mode(colorbuffer,backbuffer,D3DFMT_D24X4S4))
	{
		*zmode=D3DFMT_D24X4S4;
		WWDEBUG_SAY(("Found zbuffer mode D3DFMT_D24X4S4\n"));
		return true;
	}

	if (Test_Z_Mode(colorbuffer,backbuffer,D3DFMT_D16))
	{
		*zmode=D3DFMT_D16;
		WWDEBUG_SAY(("Found zbuffer mode D3DFMT_D16\n"));
		return true;
	}

	if (Test_Z_Mode(colorbuffer,backbuffer,D3DFMT_D15S1))
	{
		*zmode=D3DFMT_D15S1;
		WWDEBUG_SAY(("Found zbuffer mode D3DFMT_D15S1\n"));
		return true;
	}

	// can't find a match
	WWDEBUG_SAY(("Failed to find a valid zbuffer mode\r\n"));
	return false;
}

bool DX8Wrapper::Test_Z_Mode(D3DFORMAT colorbuffer,D3DFORMAT backbuffer, D3DFORMAT zmode)
{
	// See if we have this mode first
	if (Render_Failed(D3DInterface->CheckDeviceFormat(D3DADAPTER_DEFAULT,WW3D_DEVTYPE,
		colorbuffer,D3DUSAGE_DEPTHSTENCIL,D3DRTYPE_SURFACE,zmode)))
	{
		WWDEBUG_SAY(("CheckDeviceFormat failed.  Colorbuffer format = %d  Zbufferformat = %d\n",colorbuffer,zmode));
		return false;
	}

	// Then see if it matches the color buffer
	if(Render_Failed(D3DInterface->CheckDepthStencilMatch(D3DADAPTER_DEFAULT, WW3D_DEVTYPE,
		colorbuffer,backbuffer,zmode)))
	{
		WWDEBUG_SAY(("CheckDepthStencilMatch failed.  Colorbuffer format = %d  Backbuffer format = %d Zbufferformat = %d\n",colorbuffer,backbuffer,zmode));
		return false;
	}
	return true;
}


void DX8Wrapper::Reset_Statistics()
{
	matrix_changes	= 0;
	material_changes = 0;
	vertex_buffer_changes = 0;
	index_buffer_changes = 0;
	light_changes = 0;
	texture_changes = 0;
	render_state_changes =0;
	texture_stage_state_changes =0;
	draw_calls =0;

	number_of_DX8_calls = 0;
	last_frame_matrix_changes = 0;
	last_frame_material_changes = 0;
	last_frame_vertex_buffer_changes = 0;
	last_frame_index_buffer_changes = 0;
	last_frame_light_changes = 0;
	last_frame_texture_changes = 0;
	last_frame_render_state_changes = 0;
	last_frame_texture_stage_state_changes = 0;
	last_frame_number_of_DX8_calls = 0;
	last_frame_draw_calls =0;
}

void DX8Wrapper::Begin_Statistics()
{
	matrix_changes=0;
	material_changes=0;
	vertex_buffer_changes=0;
	index_buffer_changes=0;
	light_changes=0;
	texture_changes = 0;
	render_state_changes =0;
	texture_stage_state_changes =0;
	number_of_DX8_calls=0;
	draw_calls=0;
}

void DX8Wrapper::End_Statistics()
{
	last_frame_matrix_changes=matrix_changes;
	last_frame_material_changes=material_changes;
	last_frame_vertex_buffer_changes=vertex_buffer_changes;
	last_frame_index_buffer_changes=index_buffer_changes;
	last_frame_light_changes=light_changes;
	last_frame_texture_changes = texture_changes;
	last_frame_render_state_changes = render_state_changes;
	last_frame_texture_stage_state_changes = texture_stage_state_changes;
	last_frame_number_of_DX8_calls=number_of_DX8_calls;
	last_frame_draw_calls=draw_calls;
}

unsigned DX8Wrapper::Get_Last_Frame_Matrix_Changes()			{ return last_frame_matrix_changes; }
unsigned DX8Wrapper::Get_Last_Frame_Material_Changes()		{ return last_frame_material_changes; }
unsigned DX8Wrapper::Get_Last_Frame_Vertex_Buffer_Changes()	{ return last_frame_vertex_buffer_changes; }
unsigned DX8Wrapper::Get_Last_Frame_Index_Buffer_Changes()	{ return last_frame_index_buffer_changes; }
unsigned DX8Wrapper::Get_Last_Frame_Light_Changes()			{ return last_frame_light_changes; }
unsigned DX8Wrapper::Get_Last_Frame_Texture_Changes()			{ return last_frame_texture_changes; }
unsigned DX8Wrapper::Get_Last_Frame_Render_State_Changes()	{ return last_frame_render_state_changes; }
unsigned DX8Wrapper::Get_Last_Frame_Texture_Stage_State_Changes()	{ return last_frame_texture_stage_state_changes; }
unsigned DX8Wrapper::Get_Last_Frame_DX8_Calls()					{ return last_frame_number_of_DX8_calls; }
unsigned DX8Wrapper::Get_Last_Frame_Draw_Calls()				{ return last_frame_draw_calls; }
unsigned long DX8Wrapper::Get_FrameCount(void) {return FrameCount;}

void DX8_Assert()
{
	WWASSERT(DX8Wrapper::_Get_D3D());
	DX8_THREAD_ASSERT();
}

void DX8Wrapper::Begin_Scene(void)
{
	DX8_THREAD_ASSERT();

#if ENABLE_EMBEDDED_BROWSER
	DX8WebBrowser::Update();
#endif
	
	DX8CALL(BeginScene());
	Direct3D11_Begin_Scene();

#if defined(_WIN32)
	DX8WebBrowser::Update();
#endif
}

void DX8Wrapper::End_Scene(bool flip_frames)
{
	DX8_THREAD_ASSERT();
	DX8CALL(EndScene());
	Direct3D11_End_Scene(flip_frames);

#if defined(_WIN32)
	DX8WebBrowser::Render(0);
#endif

	if (flip_frames) {
		DX8_Assert();
		RenderResult hr;
		{
			WWPROFILE("DX8Device::Present()");
			//
			// -dx11present owns the window.  Presenting both swap chains into it loses the Direct3D 9
			// device (DEVICELOST on the next call, then a null shroud surface when a match starts).
			// Direct3D11_End_Scene has already shown the frame.
			//
			// Nothing presents on the Direct3D 9 device then, so the lost device Present reports has to
			// be asked for: a display mode change loses a windowed device too, and every view update
			// waits for TestCooperativeLevel to say D3D_OK before it draws anything.
			//
			if (Direct3D11_Present_Is_Enabled()) {
				hr = _Get_D3D_Device()->TestCooperativeLevel();
				if (hr == D3DERR_DEVICENOTRESET) {
					hr = D3DERR_DEVICELOST;
				}
			} else {
				hr=_Get_D3D_Device()->Present(NULL, NULL, NULL, NULL);
			}
		}

		number_of_DX8_calls++;

		if (Render_Succeeded(hr)) {
#ifdef EXTENDED_STATS
			if (stats.m_sleepTime) {
				::Sleep(stats.m_sleepTime);
			}
#endif
			IsDeviceLost=false;
			FrameCount++;
		}
		else {
			IsDeviceLost=true;
		}

		// If the device was lost we need to check for cooperative level and possibly reset the device
		if (hr==D3DERR_DEVICELOST) {
			hr=_Get_D3D_Device()->TestCooperativeLevel();
			if (hr==D3DERR_DEVICENOTRESET) {
				Reset_Device();
			}
			else {
				// Sleep it not active
				ThreadClass::Sleep_Ms(200);
			}
		}
		else {
			DX8_ErrorCode(hr);
		}
	}

	// Each frame, release all of the buffers and textures.
	Set_Vertex_Buffer(NULL);
	Set_Index_Buffer(NULL,0);
	for (int i=0;i<CurrentCaps->Get_Max_Textures_Per_Pass();++i) Set_Texture(i,NULL);
	Set_Material(NULL);
}


void DX8Wrapper::Flip_To_Primary(void)
{
	// If we are fullscreen and the current frame is odd then we need
	// to force a page flip to ensure that the first buffer in the flipping
	// chain is the one visible.
	if (!IsWindowed) {
		DX8_Assert();

		int numBuffers = (_PresentParameters.BackBufferCount + 1);
		int visibleBuffer = (FrameCount % numBuffers);
		int flipCount = ((numBuffers - visibleBuffer) % numBuffers);
		int resetAttempts = 0;

		while ((flipCount > 0) && (resetAttempts < 3)) {
			RenderResult hr = _Get_D3D_Device()->TestCooperativeLevel();

			if (Render_Failed(hr)) {
				WWDEBUG_SAY(("TestCooperativeLevel Failed!\n"));

				if (D3DERR_DEVICELOST == hr) {
					IsDeviceLost=true;
					WWDEBUG_SAY(("DEVICELOST: Cannot flip to primary.\n"));
					return;
				}
				IsDeviceLost=false;

				if (D3DERR_DEVICENOTRESET == hr) {
					WWDEBUG_SAY(("DEVICENOTRESET\n"));
					Reset_Device();
					resetAttempts++;
				}
			} else {
				WWDEBUG_SAY(("Flipping: %ld\n", FrameCount));
				hr = _Get_D3D_Device()->Present(NULL, NULL, NULL, NULL);

				if (Render_Succeeded(hr)) {
					IsDeviceLost=false;
					FrameCount++;
					WWDEBUG_SAY(("Flip to primary succeeded %ld\n", FrameCount));
				}
				else {
					IsDeviceLost=true;
				}
			}

			--flipCount;
		}
	}
}


//**********************************************************************************************
//! Clear current render device
/*! KM
/* 5/17/02 KM Fixed support for render to texture with depth/stencil buffers
*/
void DX8Wrapper::Clear(bool clear_color, bool clear_z_stencil, const Vector3 &color, float dest_alpha, float z, unsigned int stencil)
{
	DX8_THREAD_ASSERT();

	// If we try to clear a stencil buffer which is not there, the entire call will fail
	// KJM fixed this to get format from back buffer (incase render to texture is used)
	/*bool has_stencil = (	_PresentParameters.AutoDepthStencilFormat == D3DFMT_D15S1 ||
								_PresentParameters.AutoDepthStencilFormat == D3DFMT_D24S8 ||
								_PresentParameters.AutoDepthStencilFormat == D3DFMT_D24X4S4);*/
	bool has_stencil=false;
	// GetDepthStencilSurface leaves this alone when it fails - a lost device is how - and the test
	// below then reads whatever was on the stack and calls GetDesc through it.
	IDirect3DSurface9* depthbuffer = NULL;

	_Get_D3D_Device()->GetDepthStencilSurface(&depthbuffer);
	number_of_DX8_calls++;

	if (depthbuffer)
	{
		D3DSURFACE_DESC desc;
		depthbuffer->GetDesc(&desc);
		has_stencil=
		(
			desc.Format==D3DFMT_D15S1 ||
			desc.Format==D3DFMT_D24S8 ||
			desc.Format==D3DFMT_D24X4S4
		);

		// release ref
		depthbuffer->Release();
	}

	uint32 flags = 0;
	if (clear_color) flags |= D3DCLEAR_TARGET;
	if (clear_z_stencil) flags |= D3DCLEAR_ZBUFFER;
	if (clear_z_stencil && has_stencil) flags |= D3DCLEAR_STENCIL;
	if (flags)
	{
		DX8CALL(Clear(0, NULL, flags, Convert_Color(color,dest_alpha), z, stencil));
	}
	Direct3D11_Mirror_Clear(clear_color, clear_z_stencil, color.X, color.Y, color.Z, dest_alpha);
}

void DX8Wrapper::Set_Viewport(const D3DVIEWPORT9* pViewport)
{
	DX8_THREAD_ASSERT();
	DX8CALL(SetViewport(pViewport));
}

// ----------------------------------------------------------------------------
//
// Set vertex buffer. A reference to previous vertex buffer is released and
// this one is assigned the current vertex buffer. The DX8 vertex buffer will
// actually be set in Apply() which is called by Draw_Indexed_Triangles().
//
// ----------------------------------------------------------------------------

void DX8Wrapper::Set_Vertex_Buffer(const VertexBufferClass* vb, unsigned stream)
{
	render_state.vba_offset=0;
	render_state.vba_count=0;
	if (render_state.vertex_buffers[stream]) {
		render_state.vertex_buffers[stream]->Release_Engine_Ref();
	}
	REF_PTR_SET(render_state.vertex_buffers[stream],const_cast<VertexBufferClass*>(vb));
	if (vb) {
		vb->Add_Engine_Ref();
		render_state.vertex_buffer_types[stream]=vb->Type();
	}
	else {
		render_state.vertex_buffer_types[stream]=BUFFER_TYPE_INVALID;
	}
	render_state_changed|=VERTEX_BUFFER_CHANGED;
}

// ----------------------------------------------------------------------------
//
// Set index buffer. A reference to previous index buffer is released and
// this one is assigned the current index buffer. The DX8 index buffer will
// actually be set in Apply() which is called by Draw_Indexed_Triangles().
//
// ----------------------------------------------------------------------------

void DX8Wrapper::Set_Index_Buffer(const IndexBufferClass* ib,unsigned short index_base_offset)
{
	render_state.iba_offset=0;
	if (render_state.index_buffer) {
		render_state.index_buffer->Release_Engine_Ref();
	}
	REF_PTR_SET(render_state.index_buffer,const_cast<IndexBufferClass*>(ib));
	render_state.index_base_offset=index_base_offset;
	if (ib) {
		ib->Add_Engine_Ref();
		render_state.index_buffer_type=ib->Type();
	}
	else {
		render_state.index_buffer_type=BUFFER_TYPE_INVALID;
	}
	render_state_changed|=INDEX_BUFFER_CHANGED;
}

// ----------------------------------------------------------------------------
//
// Set vertex buffer using dynamic access object.
//
// ----------------------------------------------------------------------------

void DX8Wrapper::Set_Vertex_Buffer(const DynamicVBAccessClass& vba_)
{
	// Release all streams (only one stream allowed in the legacy pipeline)
	for (int i=1;i<MAX_VERTEX_STREAMS;++i) {
		DX8Wrapper::Set_Vertex_Buffer(NULL, i);
	}

	if (render_state.vertex_buffers[0]) render_state.vertex_buffers[0]->Release_Engine_Ref();
	DynamicVBAccessClass& vba=const_cast<DynamicVBAccessClass&>(vba_);
	render_state.vertex_buffer_types[0]=vba.Get_Type();
	render_state.vba_offset=vba.VertexBufferOffset;
	render_state.vba_count=vba.Get_Vertex_Count();
	REF_PTR_SET(render_state.vertex_buffers[0],vba.VertexBuffer);
	render_state.vertex_buffers[0]->Add_Engine_Ref();
	render_state_changed|=VERTEX_BUFFER_CHANGED;
	render_state_changed|=INDEX_BUFFER_CHANGED;		// vba_offset changes so index buffer needs to be reset as well.
}

// ----------------------------------------------------------------------------
//
// Bind a range of a sorting vertex array that was reserved and written before, the way the dynamic
// access above binds one it is holding.  The particle fill reserves every system's range first,
// writes them on the job pool, and only then binds each one to insert it into the sorting pool.
//
// ----------------------------------------------------------------------------

void DX8Wrapper::Set_Sorting_Vertex_Range(VertexBufferClass* vb, unsigned short offset, unsigned short count)
{
	for (int i=1;i<MAX_VERTEX_STREAMS;++i) {
		DX8Wrapper::Set_Vertex_Buffer(NULL, i);
	}

	if (render_state.vertex_buffers[0]) render_state.vertex_buffers[0]->Release_Engine_Ref();
	render_state.vertex_buffer_types[0]=BUFFER_TYPE_DYNAMIC_SORTING;
	render_state.vba_offset=offset;
	render_state.vba_count=count;
	REF_PTR_SET(render_state.vertex_buffers[0],vb);
	render_state.vertex_buffers[0]->Add_Engine_Ref();
	render_state_changed|=VERTEX_BUFFER_CHANGED;
	render_state_changed|=INDEX_BUFFER_CHANGED;		// vba_offset changes so index buffer needs to be reset as well.
}

// ----------------------------------------------------------------------------
//
// Set index buffer using dynamic access object.
//
// ----------------------------------------------------------------------------

void DX8Wrapper::Set_Index_Buffer(const DynamicIBAccessClass& iba_,unsigned short index_base_offset)
{
	if (render_state.index_buffer) render_state.index_buffer->Release_Engine_Ref();

	DynamicIBAccessClass& iba=const_cast<DynamicIBAccessClass&>(iba_);
	render_state.index_base_offset=index_base_offset;
	render_state.index_buffer_type=iba.Get_Type();
	render_state.iba_offset=iba.IndexBufferOffset;
	REF_PTR_SET(render_state.index_buffer,iba.IndexBuffer);
	render_state.index_buffer->Add_Engine_Ref();
	render_state_changed|=INDEX_BUFFER_CHANGED;
}

// ----------------------------------------------------------------------------
//
// Private function for the special case of rendering polygons from sorting
// index and vertex buffers.
//
// ----------------------------------------------------------------------------

void DX8Wrapper::Draw_Sorting_IB_VB(
	unsigned primitive_type,
	unsigned short start_index,
	unsigned short polygon_count,
	unsigned short min_vertex_index,
	unsigned short vertex_count)
{
	WWASSERT(render_state.vertex_buffer_types[0]==BUFFER_TYPE_SORTING || render_state.vertex_buffer_types[0]==BUFFER_TYPE_DYNAMIC_SORTING);
	WWASSERT(render_state.index_buffer_type==BUFFER_TYPE_SORTING || render_state.index_buffer_type==BUFFER_TYPE_DYNAMIC_SORTING);

	// Fill dynamic vertex buffer with sorting vertex buffer vertices
	DynamicVBAccessClass dyn_vb_access(BUFFER_TYPE_DYNAMIC_DX8,dynamic_fvf_type,vertex_count);
	{
		DynamicVBAccessClass::WriteLockClass lock(&dyn_vb_access);
		VertexFormatXYZNDUV2* src = static_cast<SortingVertexBufferClass*>(render_state.vertex_buffers[0])->VertexBuffer;
		VertexFormatXYZNDUV2* dest= lock.Get_Formatted_Vertex_Array();
		src += render_state.vba_offset + render_state.index_base_offset + min_vertex_index;
		unsigned  size = dyn_vb_access.FVF_Info().Get_FVF_Size()*vertex_count/sizeof(unsigned);
		unsigned *dest_u =(unsigned*) dest;
		unsigned *src_u = (unsigned*) src;

		for (unsigned i=0;i<size;++i) {
			*dest_u++=*src_u++;
		}
	}

	// refused by the card, as in Draw: nothing to draw from on either device
	if (static_cast<DX8VertexBufferClass*>(dyn_vb_access.VertexBuffer)->Get_DX8_Vertex_Buffer()==NULL) return;

	DX8CALL(SetStreamSource(
		0,
		static_cast<DX8VertexBufferClass*>(dyn_vb_access.VertexBuffer)->Get_DX8_Vertex_Buffer(),
		0,	// OffsetInBytes, D3D9's extra parameter; the offset is carried in the draw call
		dyn_vb_access.FVF_Info().Get_FVF_Size()));
	// If using FVF format VB, set the FVF as vertex shader (may not be needed here KM)
	unsigned fvf=dyn_vb_access.FVF_Info().Get_FVF();
	if (fvf!=0) {
		DX8CALL(SetFVF(fvf));
	}
	DX8_RECORD_VERTEX_BUFFER_CHANGE();

	unsigned index_count=0;
	switch (primitive_type) {
	case D3DPT_TRIANGLELIST: index_count=polygon_count*3; break;
	case D3DPT_TRIANGLESTRIP: index_count=polygon_count+2; break;
	case D3DPT_TRIANGLEFAN: index_count=polygon_count+2; break;
	default: WWASSERT(0); break; // Unsupported primitive type
	}

	// Fill dynamic index buffer with sorting index buffer vertices
	DynamicIBAccessClass dyn_ib_access(BUFFER_TYPE_DYNAMIC_DX8,index_count);
	{
		DynamicIBAccessClass::WriteLockClass lock(&dyn_ib_access);
		unsigned short* dest=lock.Get_Index_Array();
		unsigned short* src=NULL;
		src=static_cast<SortingIndexBufferClass*>(render_state.index_buffer)->index_buffer;
		src+=render_state.iba_offset+start_index;

		try {
		for (unsigned short i=0;i<index_count;++i) {
			unsigned short index=*src++;
			index-=min_vertex_index;
			WWASSERT(index<vertex_count);
			*dest++=index;
		}
		IndexBufferExceptionFunc();
		} catch(...) {
			IndexBufferExceptionFunc();
		}
	}

	if (static_cast<DX8IndexBufferClass*>(dyn_ib_access.IndexBuffer)->Get_DX8_Index_Buffer()==NULL) return;

	// D3D8 handed SetIndices the base vertex index; D3D9 takes it on the draw call
	// instead, so it travels with the DrawIndexedPrimitive below.
	DX8CALL(SetIndices(
		static_cast<DX8IndexBufferClass*>(dyn_ib_access.IndexBuffer)->Get_DX8_Index_Buffer()));
	DX8_RECORD_INDEX_BUFFER_CHANGE();

	DX8_RECORD_DRAW_CALLS();
	if (!Direct3D11_Present_Is_Enabled()) {
		DX8CALL(DrawIndexedPrimitive(
			D3DPT_TRIANGLELIST,
			dyn_vb_access.VertexBufferOffset,	// base vertex index, was SetIndices' second argument
			0,		// start vertex
			vertex_count,
			dyn_ib_access.IndexBufferOffset,
			polygon_count));
	}

	DX8_RECORD_RENDER(polygon_count,vertex_count,render_state.shader);
}

// ----------------------------------------------------------------------------
//
//
//
// ----------------------------------------------------------------------------

// The constant register the generated shader reads D3DRS_TEXTUREFACTOR out of.  The generator
// writes register(c0) into the HLSL, so the two have to agree and nothing else may use it while a
// generated shader is bound.
static const UINT COMBINER_TEXTURE_FACTOR_REGISTER = 0;

// -ffshader only: puts a generated pixel shader in place of the texture stage combiners for the
// length of one draw call, and takes it off again however the draw ends.
//
// Bound and unbound around each draw rather than left in place, because the next draw may be one
// the generator refuses and a shader left bound would take that draw over without saying so.  A
// draw that brought its own pixel shader - the six W3DShaderManager builds - is left alone: those
// are already programmable and are not what this replaces.
class CombinerShaderBinding
{
public:
	explicit CombinerShaderBinding(IDirect3DDevice9 * device)
		: Device(device), Bound(false)
	{
		// While Direct3D 11 presents, this device skips the indexed draw the shader would be bound
		// for, and reading the stages back to format a cache key for it was a string build and a map
		// lookup per draw for nothing.
		if (!CombinerShaders_Are_Enabled() || Device == NULL || Direct3D11_Present_Is_Enabled()) {
			return;
		}

		IDirect3DPixelShader9 * existing = NULL;
		Device->GetPixelShader(&existing);
		if (existing != NULL) {
			existing->Release();
			return;
		}

		CombinerDescription description;
		CombinerShaderCache_Read_Device(Device, description);
		IDirect3DPixelShader9 * generated = CombinerShaderCache_Get(Device, description);
		if (generated == NULL) {
			return;
		}

		RenderUInt32 texture_factor = 0;
		Device->GetRenderState(D3DRS_TEXTUREFACTOR, &texture_factor);
		const float factor[4] = {
			((texture_factor >> 16) & 0xff) / 255.0f,
			((texture_factor >>  8) & 0xff) / 255.0f,
			( texture_factor        & 0xff) / 255.0f,
			((texture_factor >> 24) & 0xff) / 255.0f
		};
		Device->SetPixelShaderConstantF(COMBINER_TEXTURE_FACTOR_REGISTER, factor, 1);
		Device->SetPixelShader(generated);
		Bound = true;
	}

	~CombinerShaderBinding()
	{
		if (Bound) {
			Device->SetPixelShader(NULL);
		}
	}

private:
	CombinerShaderBinding(const CombinerShaderBinding &);
	CombinerShaderBinding & operator=(const CombinerShaderBinding &);

	IDirect3DDevice9 * Device;
	bool Bound;
};

void DX8Wrapper::Draw(
	unsigned primitive_type,
	unsigned short start_index,
	unsigned short polygon_count,
	unsigned short min_vertex_index,
	unsigned short vertex_count)
{
	if (DrawPolygonLowBoundLimit && DrawPolygonLowBoundLimit>=polygon_count) return;

	DX8_THREAD_ASSERT();
	SNAPSHOT_SAY(("DX8 - draw\n"));

	Apply_Render_State_Changes();

	// -ffprobe only: the fixed-function inventory phase 2 has to write as HLSL, counted off the
	// device rather than off this wrapper's cache.  Under D3D11 the device is behind that cache, so
	// the sample flushes the wrapper's cached values to it first; only the draws it samples pay.
	if (FixedFunctionProbe_Samples_Draw()) {
		Flush_Deferred_D3D9_State();
		FixedFunctionProbe_Record(D3DDevice);
	}

	// -ffshader only.  Binds the generated shader for this one draw and takes it off again on every
	// path out, including the early return below and Draw_Sorting_IB_VB's own draws.
	CombinerShaderBinding combiner_shader(D3DDevice);

	// Debug feature to disable triangle drawing...
	if (!_Is_Triangle_Draw_Enabled()) return;

	// A buffer the card refused is a scratch block with no Direct3D buffer and no twin behind it
	// (Create_Vertex_Buffer), so neither device has anything to draw from; skip it on both.
	const bool vertices_refused = render_state.vertex_buffers[0]
		&& (render_state.vertex_buffer_types[0]==BUFFER_TYPE_DX8 || render_state.vertex_buffer_types[0]==BUFFER_TYPE_DYNAMIC_DX8)
		&& static_cast<DX8VertexBufferClass*>(render_state.vertex_buffers[0])->Get_DX8_Vertex_Buffer()==NULL;
	const bool indices_refused = render_state.index_buffer
		&& (render_state.index_buffer_type==BUFFER_TYPE_DX8 || render_state.index_buffer_type==BUFFER_TYPE_DYNAMIC_DX8)
		&& static_cast<DX8IndexBufferClass*>(render_state.index_buffer)->Get_DX8_Index_Buffer()==NULL;
	if (vertices_refused || indices_refused) return;

#ifdef MESH_RENDER_SNAPSHOT_ENABLED
	if (WW3D::Is_Snapshot_Activated()) {
		RenderUInt32 passes=0;	// DWORD on Windows, which ValidateDevice writes
		SNAPSHOT_SAY(("ValidateDevice: "));
		Flush_Deferred_D3D9_State();
		RenderResult res=D3DDevice->ValidateDevice(&passes);
		switch (res) {
		case D3D_OK:
			SNAPSHOT_SAY(("OK\n"));
			break;

		case D3DERR_CONFLICTINGTEXTUREFILTER:
			SNAPSHOT_SAY(("D3DERR_CONFLICTINGTEXTUREFILTER\n"));
			break;
		case D3DERR_CONFLICTINGTEXTUREPALETTE:
			SNAPSHOT_SAY(("D3DERR_CONFLICTINGTEXTUREPALETTE\n"));
			break;
		case D3DERR_DEVICELOST:
			SNAPSHOT_SAY(("D3DERR_DEVICELOST\n"));
			break;
		case D3DERR_TOOMANYOPERATIONS:
			SNAPSHOT_SAY(("D3DERR_TOOMANYOPERATIONS\n"));
			break;
		case D3DERR_UNSUPPORTEDALPHAARG:
			SNAPSHOT_SAY(("D3DERR_UNSUPPORTEDALPHAARG\n"));
			break;
		case D3DERR_UNSUPPORTEDALPHAOPERATION:
			SNAPSHOT_SAY(("D3DERR_UNSUPPORTEDALPHAOPERATION\n"));
			break;
		case D3DERR_UNSUPPORTEDCOLORARG:
			SNAPSHOT_SAY(("D3DERR_UNSUPPORTEDCOLORARG\n"));
			break;
		case D3DERR_UNSUPPORTEDCOLOROPERATION:
			SNAPSHOT_SAY(("D3DERR_UNSUPPORTEDCOLOROPERATION\n"));
			break;
		case D3DERR_UNSUPPORTEDFACTORVALUE:
			SNAPSHOT_SAY(("D3DERR_UNSUPPORTEDFACTORVALUE\n"));
			break;
		case D3DERR_UNSUPPORTEDTEXTUREFILTER:
			SNAPSHOT_SAY(("D3DERR_UNSUPPORTEDTEXTUREFILTER\n"));
			break;
		case D3DERR_WRONGTEXTUREFORMAT:
			SNAPSHOT_SAY(("D3DERR_WRONGTEXTUREFORMAT\n"));
			break;
		default:
			SNAPSHOT_SAY(("UNKNOWN Error\n"));
			break;
		}
	}
#endif	// MESH_RENDER_SHAPSHOT_ENABLED


	SNAPSHOT_SAY(("DX8 - draw %d polygons (%d vertices)\n",polygon_count,vertex_count));

	if (vertex_count<3) {
		min_vertex_index=0;
		switch (render_state.vertex_buffer_types[0]) {
		case BUFFER_TYPE_DX8:
		case BUFFER_TYPE_SORTING:
			vertex_count=render_state.vertex_buffers[0]->Get_Vertex_Count()-render_state.index_base_offset-render_state.vba_offset-min_vertex_index;
			break;
		case BUFFER_TYPE_DYNAMIC_DX8:
		case BUFFER_TYPE_DYNAMIC_SORTING:
			vertex_count=render_state.vba_count;
			break;
		}
	}

	switch (render_state.vertex_buffer_types[0]) {
	case BUFFER_TYPE_DX8:
	case BUFFER_TYPE_DYNAMIC_DX8:
		switch (render_state.index_buffer_type) {
		case BUFFER_TYPE_DX8:
		case BUFFER_TYPE_DYNAMIC_DX8:
			{
/*				if ((start_index+render_state.iba_offset+polygon_count*3) > render_state.index_buffer->Get_Index_Count())
				{	WWASSERT_PRINT(0,"OVERFLOWING INDEX BUFFER");
					///@todo: MUST FIND OUT WHY THIS HAPPENS WITH LOTS OF PARTICLES ON BIG FIGHT!  -MW
					break;
				}*/
				DX8_RECORD_RENDER(polygon_count,vertex_count,render_state.shader);
				DX8_RECORD_DRAW_CALLS();
				// When Direct3D 11 owns the window, the Direct3D 9 frame is never shown and nothing
				// reads its pixels back, so drawing it is only GPU time.  Most of the state for it is
				// deferred as well (Defer_D3D9_State) and sent only before the probe's sampled draws.
				// _Draw_DX8_Primitive_UP keeps its D3D9 draw, because the smudge hardware test draws
				// with it and reads the D3D9 target back.
				if (!Direct3D11_Present_Is_Enabled()) {
					DX8CALL(DrawIndexedPrimitive(
						(D3DPRIMITIVETYPE)primitive_type,
						render_state.index_base_offset+render_state.vba_offset,	// base vertex index, was SetIndices' second argument
						min_vertex_index,
						vertex_count,
						start_index+render_state.iba_offset,
						polygon_count));
				}

				// The same draw through the Direct3D 11 backend, into its own back buffer.  Lists
				// and strips both go: Direct3D 11 has a topology for each.  A strip used to be
				// dropped here on the belief that nothing reached this point still striped, and
				// the beach surf did - so with Direct3D 11 presenting, the D3D9 draw above was
				// skipped and no draw replaced it, and the shell map's waves were drawn nowhere.
				// A fan is still dropped: Direct3D 11 has no fan topology, and its indices would
				// have to be rebuilt.
				if (primitive_type==D3DPT_TRIANGLELIST) {
					Direct3D11_Draw_Indexed_Triangles(
						polygon_count*3,
						start_index+render_state.iba_offset,
						render_state.index_base_offset+render_state.vba_offset);
				}
				else if (primitive_type==D3DPT_TRIANGLESTRIP) {
					// n indices make n-2 triangles, which is the count the caller passed.
					Direct3D11_Draw_Indexed_Strip(
						polygon_count+2,
						start_index+render_state.iba_offset,
						render_state.index_base_offset+render_state.vba_offset);
				}
			}
			break;
		case BUFFER_TYPE_SORTING:
		case BUFFER_TYPE_DYNAMIC_SORTING:
			WWASSERT_PRINT(0,"VB and IB must of same type (sorting or dx8)");
			break;
		case BUFFER_TYPE_INVALID:
			WWASSERT(0);
			break;
		}
		break;
	case BUFFER_TYPE_SORTING:
	case BUFFER_TYPE_DYNAMIC_SORTING:
		switch (render_state.index_buffer_type) {
		case BUFFER_TYPE_DX8:
		case BUFFER_TYPE_DYNAMIC_DX8:
			WWASSERT_PRINT(0,"VB and IB must of same type (sorting or dx8)");
			break;
		case BUFFER_TYPE_SORTING:
		case BUFFER_TYPE_DYNAMIC_SORTING:
			Draw_Sorting_IB_VB(primitive_type,start_index,polygon_count,min_vertex_index,vertex_count);
			break;
		case BUFFER_TYPE_INVALID:
			WWASSERT(0);
			break;
		}
		break;
	case BUFFER_TYPE_INVALID:
		WWASSERT(0);
		break;
	}
}

// ----------------------------------------------------------------------------
//
//
//
// ----------------------------------------------------------------------------

void DX8Wrapper::Draw_Triangles(
	unsigned buffer_type,
	unsigned short start_index,
	unsigned short polygon_count,
	unsigned short min_vertex_index,
	unsigned short vertex_count)
{
	if (buffer_type==BUFFER_TYPE_SORTING || buffer_type==BUFFER_TYPE_DYNAMIC_SORTING) {
		SortingRendererClass::Insert_Triangles(start_index,polygon_count,min_vertex_index,vertex_count);
	}
	else {
		Draw(D3DPT_TRIANGLELIST,start_index,polygon_count,min_vertex_index,vertex_count);
	}
}

// ----------------------------------------------------------------------------
//
//
//
// ----------------------------------------------------------------------------

void DX8Wrapper::Draw_Triangles(
	unsigned short start_index,
	unsigned short polygon_count,
	unsigned short min_vertex_index,
	unsigned short vertex_count)
{
	Draw(D3DPT_TRIANGLELIST,start_index,polygon_count,min_vertex_index,vertex_count);
}

// ----------------------------------------------------------------------------
//
//
//
// ----------------------------------------------------------------------------

void DX8Wrapper::Draw_Strip(
	unsigned short start_index,
	unsigned short polygon_count,
	unsigned short min_vertex_index,
	unsigned short vertex_count)
{
	Draw(D3DPT_TRIANGLESTRIP,start_index,polygon_count,min_vertex_index,vertex_count);
}

// ----------------------------------------------------------------------------
//
//
//
// ----------------------------------------------------------------------------

void DX8Wrapper::Apply_Render_State_Changes()
{
	SNAPSHOT_SAY(("DX8Wrapper::Apply_Render_State_Changes()\n"));
	
	if (!render_state_changed) return;
	if (render_state_changed&SHADER_CHANGED) {
		SNAPSHOT_SAY(("DX8 - apply shader\n"));
		render_state.shader.Apply();
	}

	unsigned mask=TEXTURE0_CHANGED;
	int i;
	for (i =0;i<CurrentCaps->Get_Max_Textures_Per_Pass();++i,mask<<=1) 
	{
		if (render_state_changed&mask) 
		{
			SNAPSHOT_SAY(("DX8 - apply texture %d (%s)\n",i,render_state.Textures[i] ? render_state.Textures[i]->Get_Full_Path() : "NULL"));

			if (render_state.Textures[i]) 
			{
				render_state.Textures[i]->Apply(i);
			}
			else 
			{
				TextureBaseClass::Apply_Null(i);
			}
		}
	}

	if (render_state_changed&MATERIAL_CHANGED) 
	{
		SNAPSHOT_SAY(("DX8 - apply material\n"));
		VertexMaterialClass* material=const_cast<VertexMaterialClass*>(render_state.material);
		if (material) 
		{
			material->Apply();
		}
		else VertexMaterialClass::Apply_Null();
	}

	if (render_state_changed&LIGHTS_CHANGED)
	{
		unsigned mask=LIGHT0_CHANGED;
		for (unsigned index=0;index<4;++index,mask<<=1) {
			if (render_state_changed&mask) {
				SNAPSHOT_SAY(("DX8 - apply light %d\n",index));
				if (render_state.LightEnable[index]) {
#ifdef MESH_RENDER_SNAPSHOT_ENABLED		
					if ( WW3D::Is_Snapshot_Activated() ) {
						D3DLIGHT9 * light = &(render_state.Lights[index]);
						static char * _light_types[] = { "Unknown", "Point","Spot", "Directional" };
						WWASSERT((light->Type >= 0) && (light->Type <= 3));					

						SNAPSHOT_SAY((" type = %s amb = %4.2f,%4.2f,%4.2f  diff = %4.2f,%4.2f,%4.2f spec = %4.2f, %4.2f, %4.2f\n",
							_light_types[light->Type],
							light->Ambient.r,light->Ambient.g,light->Ambient.b,
							light->Diffuse.r,light->Diffuse.g,light->Diffuse.b,
							light->Specular.r,light->Specular.g,light->Specular.b ));
						SNAPSHOT_SAY((" pos = %f, %f, %f  dir = %f, %f, %f\n",
							light->Position.x, light->Position.y, light->Position.z,
							light->Direction.x, light->Direction.y, light->Direction.z ));
					}
#endif

					Set_DX8_Light(index,&render_state.Lights[index]);
				}
				else {
					Set_DX8_Light(index,NULL);
					SNAPSHOT_SAY((" clearing light to NULL\n"));
				}
			}
		}
	}

	if (render_state_changed&WORLD_CHANGED) {
		SNAPSHOT_SAY(("DX8 - apply world matrix\n"));
		_Set_DX8_Transform(D3DTS_WORLD,render_state.world);
	}
	if (render_state_changed&VIEW_CHANGED) {
		SNAPSHOT_SAY(("DX8 - apply view matrix\n"));
		_Set_DX8_Transform(D3DTS_VIEW,render_state.view);
	}
	if (render_state_changed&VERTEX_BUFFER_CHANGED) {
		SNAPSHOT_SAY(("DX8 - apply vb change\n"));
		for (i=0;i<MAX_VERTEX_STREAMS;++i) {
			if (render_state.vertex_buffers[i]) {
				switch (render_state.vertex_buffer_types[i]) {//->Type()) {
				case BUFFER_TYPE_DX8:
				case BUFFER_TYPE_DYNAMIC_DX8:
					DX8CALL(SetStreamSource(
						i,
						static_cast<DX8VertexBufferClass*>(render_state.vertex_buffers[i])->Get_DX8_Vertex_Buffer(),
						0,	// OffsetInBytes, D3D9's extra parameter
						render_state.vertex_buffers[i]->FVF_Info().Get_FVF_Size()));
					DX8_RECORD_VERTEX_BUFFER_CHANGE();
					// The backend reads one stream.  Streams above the first carry the skinning
					// and shadow data the .vso files take, and nothing generated consumes them yet.
					if (i==0) {
						Direct3D11_Mirror_Stream_Source(
							static_cast<DX8VertexBufferClass*>(render_state.vertex_buffers[i])->Get_DX11_Twin(),
							render_state.vertex_buffers[i]->FVF_Info().Get_FVF_Size(),
							0);
					}
					{
						// If the VB format is FVF, set the FVF as a vertex shader
						unsigned fvf=render_state.vertex_buffers[i]->FVF_Info().Get_FVF();
						if (fvf!=0) {
							Set_Vertex_Format(fvf);
						}
					}
					break;
				case BUFFER_TYPE_SORTING:
				case BUFFER_TYPE_DYNAMIC_SORTING:
					break;
				default:
					WWASSERT(0);
				}
			} else {
				DX8CALL(SetStreamSource(i,NULL,0,0));
				if (i==0) {
					Direct3D11_Mirror_Stream_Source(NULL,0,0);
				}
				DX8_RECORD_VERTEX_BUFFER_CHANGE();
			}
		}
	}
	if (render_state_changed&INDEX_BUFFER_CHANGED) {
		SNAPSHOT_SAY(("DX8 - apply ib change\n"));
		if (render_state.index_buffer) {
			switch (render_state.index_buffer_type) {//->Type()) {
			case BUFFER_TYPE_DX8:
			case BUFFER_TYPE_DYNAMIC_DX8:
				// The base vertex index that used to travel here now goes on the draw call.
				DX8CALL(SetIndices(
					static_cast<DX8IndexBufferClass*>(render_state.index_buffer)->Get_DX8_Index_Buffer()));
				Direct3D11_Mirror_Indices(
					static_cast<DX8IndexBufferClass*>(render_state.index_buffer)->Get_DX11_Twin());
				DX8_RECORD_INDEX_BUFFER_CHANGE();
				break;
			case BUFFER_TYPE_SORTING:
			case BUFFER_TYPE_DYNAMIC_SORTING:
				break;
			default:
				WWASSERT(0);
			}
		}
		else {
			DX8CALL(SetIndices(NULL));
			Direct3D11_Mirror_Indices(NULL);
			DX8_RECORD_INDEX_BUFFER_CHANGE();
		}
	}

	render_state_changed&=((unsigned)WORLD_IDENTITY|(unsigned)VIEW_IDENTITY);

	SNAPSHOT_SAY(("DX8Wrapper::Apply_Render_State_Changes() - finished\n"));
}

// Each owed value is the latest one the cache holds, sent once however often it changed since the
// last flush.  Nothing outside this wrapper writes these states on the device on a modern card
// (shader.cpp's stage 2 writes are Voodoo3 only), so the device ends where it would have been.
void DX8Wrapper::Flush_Deferred_D3D9_State(void)
{
	if (!D3D9StatePending) return;
	D3D9StatePending=false;

	for (unsigned word=0; word<256/32; ++word) {
		unsigned bits=PendingRenderStates[word];
		PendingRenderStates[word]=0;
		for (unsigned bit=0; bits!=0; ++bit, bits>>=1) {
			if (bits&1) {
				const unsigned state=word*32+bit;
				DX8CALL(SetRenderState((D3DRENDERSTATETYPE)state, RenderStates[state]));
			}
		}
	}

	for (unsigned stage=0; stage<MAX_TEXTURE_STAGES; ++stage) {
		unsigned bits=PendingTextureStageStates[stage];
		PendingTextureStageStates[stage]=0;
		for (unsigned state=0; bits!=0; ++state, bits>>=1) {
			if (bits&1) {
				DX8CALL(SetTextureStageState(stage, (D3DTEXTURESTAGESTATETYPE)state, TextureStageStates[stage][state]));
			}
		}
		bits=PendingSamplerStates[stage];
		PendingSamplerStates[stage]=0;
		for (unsigned state=0; bits!=0; ++state, bits>>=1) {
			if (bits&1) {
				DX8CALL(SetSamplerState(stage, (D3DSAMPLERSTATETYPE)state, SamplerStates[stage][state]));
			}
		}
		if (PendingTextures&(1u<<stage)) {
			DX8CALL(SetTexture(stage, Textures[stage]));
		}
	}
	PendingTextures=0;

	if (PendingMaterial) {
		PendingMaterial=false;
		DX8CALL(SetMaterial(&PendingMaterialValue));
	}

	for (unsigned index=0; index<4; ++index) {
		if (PendingLights&(1u<<index)) {
			DX8CALL(SetLight(index, &PendingLightValues[index]));
		}
		if (PendingLightEnables&(1u<<index)) {
			DX8CALL(LightEnable(index, CurrentDX8LightEnables[index]));
		}
	}
	PendingLights=0;
	PendingLightEnables=0;
}

IDirect3DTexture9 * DX8Wrapper::_Create_DX8_Texture
(
	unsigned int width,
	unsigned int height,
	WW3DFormat format,
	MipCountType mip_level_count,
	D3DPOOL pool,
	bool rendertarget
)
{
	DX8_THREAD_ASSERT();
	DX8_Assert();
	IDirect3DTexture9 *texture = NULL;

	// Paletted textures not supported!
	WWASSERT(format!=D3DFMT_P8);

	// NOTE: If 'format' is not supported as a texture format, this function will find the closest
	// format that is supported and use that instead.

	// Render target may return NOTAVAILABLE, in
	// which case we return NULL.
	if (rendertarget) {
		unsigned ret=D3DXCreateTexture(
			DX8Wrapper::_Get_D3D_Device(),
			width,
			height,
			mip_level_count,
			D3DUSAGE_RENDERTARGET,
			WW3DFormat_To_D3DFormat(format),
			pool,
			&texture);

		if (ret==D3DERR_NOTAVAILABLE) {
			Non_Fatal_Log_DX8_ErrorCode(ret,__FILE__,__LINE__);
			return NULL;
		}

		// If ran out of texture ram, try invalidating some textures and mesh cache.
		if (ret==D3DERR_OUTOFVIDEOMEMORY) {
			WWDEBUG_SAY(("Error: Out of memory while creating render target. Trying to release assets...\n"));
			// Free all textures that haven't been used in the last 5 seconds
			TextureClass::Invalidate_Old_Unused_Textures(5000);

			// Invalidate the mesh cache
			WW3D::_Invalidate_Mesh_Cache();

			ret=D3DXCreateTexture(
				DX8Wrapper::_Get_D3D_Device(),
				width,
				height,
				mip_level_count,
				D3DUSAGE_RENDERTARGET,
				WW3DFormat_To_D3DFormat(format),
				pool,
				&texture);

			if (Render_Succeeded(ret)) {
				WWDEBUG_SAY(("...Render target creation succesful.\n"));
			}
			else {
				WWDEBUG_SAY(("...Render target creation failed.\n"));
			}
			if (ret==D3DERR_OUTOFVIDEOMEMORY) {
				Non_Fatal_Log_DX8_ErrorCode(ret,__FILE__,__LINE__);
				return NULL;
			}
		}

		DX8_ErrorCode(ret);
		// Just return the texture, no reduction
		// allowed for render targets.
		return texture;
	}

	// We should never run out of video memory when allocating a non-rendertarget texture.
	// However, it seems to happen sometimes when there are a lot of textures in memory and so
	// if it happens we'll release assets and try again (anything is better than crashing).
	unsigned ret=D3DXCreateTexture(
		DX8Wrapper::_Get_D3D_Device(),
		width,
		height,
		mip_level_count,
		0,
		WW3DFormat_To_D3DFormat(format),
		pool,
		&texture);

	// If ran out of texture ram, try invalidating some textures and mesh cache.
	if (ret==D3DERR_OUTOFVIDEOMEMORY) {
		WWDEBUG_SAY(("Error: Out of memory while creating texture. Trying to release assets...\n"));
		// Free all textures that haven't been used in the last 5 seconds
		TextureClass::Invalidate_Old_Unused_Textures(5000);

		// Invalidate the mesh cache
		WW3D::_Invalidate_Mesh_Cache();

		ret=D3DXCreateTexture(
			DX8Wrapper::_Get_D3D_Device(),
			width,
			height,
			mip_level_count,
			0,
			WW3DFormat_To_D3DFormat(format),
			pool,
			&texture);
		if (Render_Succeeded(ret)) {
			WWDEBUG_SAY(("...Texture creation succesful.\n"));
		}
		else {
			StringClass format_name(0,true);
			Get_WW3D_Format_Name(format, format_name);
			WWDEBUG_SAY(("...Texture creation failed. (%d x %d, format: %s, mips: %d\n",width,height,format_name,mip_level_count));
		}

	}
	DX8_ErrorCode(ret);

	return texture;
}

IDirect3DTexture9 * DX8Wrapper::_Create_DX8_Texture
(
	const char *filename,
	MipCountType mip_level_count
)
{
	DX8_THREAD_ASSERT();
	DX8_Assert();
	IDirect3DTexture9 *texture = NULL;

	// NOTE: If the original image format is not supported as a texture format, it will
	// automatically be converted to an appropriate format.
	// NOTE: It is possible to get the size and format of the original image file from this
	// function as well, so if we later want to second-guess D3DX's format conversion decisions
	// we can do so after this function is called..
	unsigned result = D3DXCreateTextureFromFileExA(
		_Get_D3D_Device(),
		filename,
		D3DX_DEFAULT,
		D3DX_DEFAULT,
		mip_level_count,//create_mipmaps ? 0 : 1,
		0,
		D3DFMT_UNKNOWN,
		D3DPOOL_MANAGED,
		D3DX_FILTER_BOX,
		D3DX_FILTER_BOX,
		0,
		NULL,
		NULL,
		&texture);

	if (result != D3D_OK) {
		return MissingTexture::_Get_Missing_Texture();
	}

	// Make sure texture wasn't paletted!
	D3DSURFACE_DESC desc;
	texture->GetLevelDesc(0,&desc);
	if (desc.Format==D3DFMT_P8) {
		texture->Release();
		return MissingTexture::_Get_Missing_Texture();
	}
	return texture;
}

IDirect3DTexture9 * DX8Wrapper::_Create_DX8_Texture
(
	IDirect3DSurface9 *surface,
	MipCountType mip_level_count
)
{
	DX8_THREAD_ASSERT();
	DX8_Assert();
	IDirect3DTexture9 *texture = NULL;

	D3DSURFACE_DESC surface_desc;
	memset(&surface_desc,0, sizeof(D3DSURFACE_DESC));
	surface->GetDesc(&surface_desc);

	// This function will create a texture with a different (but similar) format if the surface is
	// not in a supported texture format.
	WW3DFormat format=D3DFormat_To_WW3DFormat(surface_desc.Format);
	texture = _Create_DX8_Texture(surface_desc.Width, surface_desc.Height, format, mip_level_count);

	// Copy the surface to the texture
	IDirect3DSurface9 *tex_surface = NULL;
	texture->GetSurfaceLevel(0, &tex_surface);
	DX8_ErrorCode(D3DXLoadSurfaceFromSurface(tex_surface, NULL, NULL, surface, NULL, NULL, D3DX_FILTER_BOX, 0));
	tex_surface->Release();

	// Create mipmaps if needed
	if (mip_level_count!=MIP_LEVELS_1) 
	{
		DX8_ErrorCode(D3DXFilterTexture(texture, NULL, 0, D3DX_FILTER_BOX));
	}

	return texture;

}

/*!
 * KJM create depth stencil texture
 */
IDirect3DTexture9 * DX8Wrapper::_Create_DX8_ZTexture
(
	unsigned int width,
	unsigned int height,
	WW3DZFormat zformat,
	MipCountType mip_level_count,
	D3DPOOL pool
)
{
	DX8_THREAD_ASSERT();
	DX8_Assert();
	IDirect3DTexture9* texture = NULL;

	D3DFORMAT zfmt=WW3DZFormat_To_D3DFormat(zformat);

	unsigned ret=DX8Wrapper::_Get_D3D_Device()->CreateTexture
	(
		width,
		height,
		mip_level_count,
		D3DUSAGE_DEPTHSTENCIL,
		zfmt,
		pool,
		&texture,
		NULL	// pSharedHandle, D3D9's extra parameter, reserved and always null
	);

	if (ret==D3DERR_NOTAVAILABLE) 
	{
		Non_Fatal_Log_DX8_ErrorCode(ret,__FILE__,__LINE__);
		return NULL;
	}

	// If ran out of texture ram, try invalidating some textures and mesh cache.
	if (ret==D3DERR_OUTOFVIDEOMEMORY) 
	{
		WWDEBUG_SAY(("Error: Out of memory while creating render target. Trying to release assets...\n"));
		// Free all textures that haven't been used in the last 5 seconds
		TextureClass::Invalidate_Old_Unused_Textures(5000);

		// Invalidate the mesh cache
		WW3D::_Invalidate_Mesh_Cache();

		ret=DX8Wrapper::_Get_D3D_Device()->CreateTexture
		(
			width,
			height,
			mip_level_count,
			D3DUSAGE_DEPTHSTENCIL,
			zfmt,
			pool,
			&texture,
			NULL	// pSharedHandle, D3D9's extra parameter, reserved and always null
		);

		if (Render_Succeeded(ret)) 
		{
			WWDEBUG_SAY(("...Render target creation succesful.\n"));
		}
		else 
		{
			WWDEBUG_SAY(("...Render target creation failed.\n"));
		}
		if (ret==D3DERR_OUTOFVIDEOMEMORY) 
		{
			Non_Fatal_Log_DX8_ErrorCode(ret,__FILE__,__LINE__);
			return NULL;
		}
	}

	DX8_ErrorCode(ret);

	texture->AddRef(); // don't release this texture

	// Just return the texture, no reduction
	// allowed for render targets.

	return texture;
}

/*!
 * KJM create cube map texture
 */
IDirect3DCubeTexture9* DX8Wrapper::_Create_DX8_Cube_Texture
(
	unsigned int width,
	unsigned int height,
	WW3DFormat format,
	MipCountType mip_level_count,
	D3DPOOL pool,
	bool rendertarget
)
{
	WWASSERT(width==height);
	DX8_THREAD_ASSERT();
	DX8_Assert();
	IDirect3DCubeTexture9* texture=NULL;

	// Paletted textures not supported!
	WWASSERT(format!=D3DFMT_P8);

	// NOTE: If 'format' is not supported as a texture format, this function will find the closest
	// format that is supported and use that instead.

	// Render target may return NOTAVAILABLE, in
	// which case we return NULL.
	if (rendertarget) 
	{
		unsigned ret=D3DXCreateCubeTexture
		(
			DX8Wrapper::_Get_D3D_Device(),
			width,
			mip_level_count,
			D3DUSAGE_RENDERTARGET,
			WW3DFormat_To_D3DFormat(format),
			pool,
			&texture
		);

		if (ret==D3DERR_NOTAVAILABLE) 
		{
			Non_Fatal_Log_DX8_ErrorCode(ret,__FILE__,__LINE__);
			return NULL;
		}

		// If ran out of texture ram, try invalidating some textures and mesh cache.
		if (ret==D3DERR_OUTOFVIDEOMEMORY) 
		{
			WWDEBUG_SAY(("Error: Out of memory while creating render target. Trying to release assets...\n"));
			// Free all textures that haven't been used in the last 5 seconds
			TextureClass::Invalidate_Old_Unused_Textures(5000);

			// Invalidate the mesh cache
			WW3D::_Invalidate_Mesh_Cache();

			ret=D3DXCreateCubeTexture
			(
				DX8Wrapper::_Get_D3D_Device(),
				width,
				mip_level_count,
				D3DUSAGE_RENDERTARGET,
				WW3DFormat_To_D3DFormat(format),
				pool,
				&texture
			);

			if (Render_Succeeded(ret))
			{
				WWDEBUG_SAY(("...Render target creation succesful.\n"));
			}
			else 
			{
				WWDEBUG_SAY(("...Render target creation failed.\n"));
			}
			if (ret==D3DERR_OUTOFVIDEOMEMORY) 
			{
				Non_Fatal_Log_DX8_ErrorCode(ret,__FILE__,__LINE__);
				return NULL;
			}
		}

		DX8_ErrorCode(ret);
		// Just return the texture, no reduction
		// allowed for render targets.
		return texture;
	}

	// We should never run out of video memory when allocating a non-rendertarget texture.
	// However, it seems to happen sometimes when there are a lot of textures in memory and so
	// if it happens we'll release assets and try again (anything is better than crashing).
	unsigned ret=D3DXCreateCubeTexture
	(
		DX8Wrapper::_Get_D3D_Device(),
		width,
		mip_level_count,
		0,
		WW3DFormat_To_D3DFormat(format),
		pool,
		&texture
	);

	// If ran out of texture ram, try invalidating some textures and mesh cache.
	if (ret==D3DERR_OUTOFVIDEOMEMORY) 
	{
		WWDEBUG_SAY(("Error: Out of memory while creating texture. Trying to release assets...\n"));
		// Free all textures that haven't been used in the last 5 seconds
		TextureClass::Invalidate_Old_Unused_Textures(5000);

		// Invalidate the mesh cache
		WW3D::_Invalidate_Mesh_Cache();

		ret=D3DXCreateCubeTexture
		(
			DX8Wrapper::_Get_D3D_Device(),
			width,
			mip_level_count,
			0,
			WW3DFormat_To_D3DFormat(format),
			pool,
			&texture
		);
		if (Render_Succeeded(ret)) 
		{
			WWDEBUG_SAY(("...Texture creation succesful.\n"));
		}
		else 
		{
			StringClass format_name(0,true);
			Get_WW3D_Format_Name(format, format_name);
			WWDEBUG_SAY(("...Texture creation failed. (%d x %d, format: %s, mips: %d\n",width,height,format_name,mip_level_count));
		}

	}
	DX8_ErrorCode(ret);

	return texture;
}

/*!
 * KJM create volume texture
 */
IDirect3DVolumeTexture9* DX8Wrapper::_Create_DX8_Volume_Texture
(
	unsigned int width,
	unsigned int height,
	unsigned int depth,
	WW3DFormat format,
	MipCountType mip_level_count,
	D3DPOOL pool
)
{
	DX8_THREAD_ASSERT();
	DX8_Assert();
	IDirect3DVolumeTexture9* texture=NULL;

	// Paletted textures not supported!
	WWASSERT(format!=D3DFMT_P8);

	// NOTE: If 'format' is not supported as a texture format, this function will find the closest
	// format that is supported and use that instead.


	// We should never run out of video memory when allocating a non-rendertarget texture.
	// However, it seems to happen sometimes when there are a lot of textures in memory and so
	// if it happens we'll release assets and try again (anything is better than crashing).
	unsigned ret=D3DXCreateVolumeTexture
	(
		DX8Wrapper::_Get_D3D_Device(),
		width,
		height,
		depth,
		mip_level_count,
		0,
		WW3DFormat_To_D3DFormat(format),
		pool,
		&texture
	);

	// If ran out of texture ram, try invalidating some textures and mesh cache.
	if (ret==D3DERR_OUTOFVIDEOMEMORY) 
	{
		WWDEBUG_SAY(("Error: Out of memory while creating texture. Trying to release assets...\n"));
		// Free all textures that haven't been used in the last 5 seconds
		TextureClass::Invalidate_Old_Unused_Textures(5000);

		// Invalidate the mesh cache
		WW3D::_Invalidate_Mesh_Cache();

		ret=D3DXCreateVolumeTexture
		(
			DX8Wrapper::_Get_D3D_Device(),
			width,
			height,
			depth,
			mip_level_count,
			0,
			WW3DFormat_To_D3DFormat(format),
			pool,
			&texture
		);
		if (Render_Succeeded(ret)) 
		{
			WWDEBUG_SAY(("...Texture creation succesful.\n"));
		}
		else 
		{
			StringClass format_name(0,true);
			Get_WW3D_Format_Name(format, format_name);
			WWDEBUG_SAY(("...Texture creation failed. (%d x %d, format: %s, mips: %d\n",width,height,format_name,mip_level_count));
		}

	}
	DX8_ErrorCode(ret);

	return texture;
}


IDirect3DSurface9 * DX8Wrapper::_Create_DX8_Surface(unsigned int width, unsigned int height, WW3DFormat format)
{
	DX8_THREAD_ASSERT();
	DX8_Assert();

	IDirect3DSurface9 *surface = NULL;

	// Paletted surfaces not supported!
	WWASSERT(format!=D3DFMT_P8);

	//	No device, no surface.  SurfaceClass carries the null and the callers that matter in such a
	//	run check it; the alternative is CreateImageSurface on a null device.
	if (_Get_D3D_Device() == NULL) {
		return NULL;
	}

	// D3D9 replaced CreateImageSurface with CreateOffscreenPlainSurface, which names the
	// pool the D3D8 call always used implicitly.
	DX8CALL(CreateOffscreenPlainSurface(width, height, WW3DFormat_To_D3DFormat(format), D3DPOOL_SYSTEMMEM, &surface, NULL));

	return surface;
}

IDirect3DSurface9 * DX8Wrapper::_Create_DX8_Surface(const char *filename_)
{
	DX8_THREAD_ASSERT();
	DX8_Assert();

	// Note: Since there is no "D3DXCreateSurfaceFromFile" and no "GetSurfaceInfoFromFile" (the
	// latter is supposed to be added to D3DX in a future version), we create a texture from the
	// file (w/o mipmaps), check that its surface is equal to the original file data (which it
	// will not be if the file is not in a texture-supported format or size). If so, copy its
	// surface (we might be able to just get its surface and add a ref to it but I'm not sure so
	// I'm not going to risk it) and release the texture. If not, create a surface according to
	// the file data and use D3DXLoadSurfaceFromFile. This is a horrible hack, but it saves us
	// having to write file loaders. Will fix this when D3DX provides us with the right functions.
	// Create a surface the size of the file image data
	IDirect3DSurface9 *surface = NULL;

	{

		file_auto_ptr myfile(_TheFileFactory,filename_);
		// If file not found, create a surface with missing texture in it

		if (!myfile->Is_Available()) {
			// If file not found, try the dds format
			// else create a surface with missing texture in it
			char compressed_name[200];
			strncpy(compressed_name,filename_, 200);
			compressed_name[199] = 0;
			// the last dot: the first one turned "a.b.tga" into nothing, and a name without one was a strlen of NULL
			char *ext = strrchr(compressed_name, '.');
			if ( ext && (strlen(ext)==4) &&
				  ( (ext[1] == 't') || (ext[1] == 'T') ) && 
				  ( (ext[2] == 'g') || (ext[2] == 'G') ) && 
				  ( (ext[3] == 'a') || (ext[3] == 'A') ) ) {
				ext[1]='d';
				ext[2]='d';
				ext[3]='s';
			}
			file_auto_ptr myfile2(_TheFileFactory,compressed_name);
			if (!myfile2->Is_Available())
				return MissingTexture::_Create_Missing_Surface();
		}
	}

	StringClass filename_string(filename_,true);
	surface=TextureLoader::Load_Surface_Immediate(
		filename_string,
		WW3D_FORMAT_UNKNOWN,
		true);
	return surface;
}


/***********************************************************************************************
 * DX8Wrapper::_Update_Texture -- Copies a texture from system memory to video memory          *
 *                                                                                             *
 *                                                                                             *
 *                                                                                             *
 *                                                                                             *
 * INPUT:                                                                                      *
 *                                                                                             *
 * OUTPUT:                                                                                     *
 *                                                                                             *
 * WARNINGS:                                                                                   *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   4/26/2001  hy : Created.                                                                  *
 *=============================================================================================*/
void DX8Wrapper::_Update_Texture(TextureClass *system, TextureClass *video)
{
	WWASSERT(system);
	WWASSERT(video);
	WWASSERT(system->Get_Pool()==TextureClass::POOL_SYSTEMMEM);
	WWASSERT(video->Get_Pool()==TextureClass::POOL_DEFAULT);
	DX8CALL(UpdateTexture(system->Peek_D3D_Base_Texture(),video->Peek_D3D_Base_Texture()));
}

void DX8Wrapper::Compute_Caps(WW3DFormat display_format)
{
	DX8_THREAD_ASSERT();
	DX8_Assert();
	delete CurrentCaps;
	CurrentCaps=new DX8Caps(_Get_D3D(),D3DDevice,display_format,Get_Current_Adapter_Identifier());
}


void DX8Wrapper::Set_Light(unsigned index, const D3DLIGHT9* light)
{
	if (light) {
		render_state.Lights[index]=*light;
		render_state.LightEnable[index]=true;
	}
	else {
		render_state.LightEnable[index]=false;
	}
	render_state_changed|=(LIGHT0_CHANGED<<index);
}

void DX8Wrapper::Set_Light(unsigned index,const LightClass &light)
{
	D3DLIGHT9 dlight;
	Vector3 temp;
	memset(&dlight,0,sizeof(D3DLIGHT9));

	switch (light.Get_Type())
	{
	case LightClass::POINT:
		{
			dlight.Type=D3DLIGHT_POINT;
		}
		break;
	case LightClass::DIRECTIONAL:
		{
			dlight.Type=D3DLIGHT_DIRECTIONAL;
		}
		break;
	case LightClass::SPOT:
		{
			dlight.Type=D3DLIGHT_SPOT;
		}
		break;
	}

	light.Get_Diffuse(&temp);
	temp*=light.Get_Intensity();
	dlight.Diffuse.r=temp.X;
	dlight.Diffuse.g=temp.Y;
	dlight.Diffuse.b=temp.Z;
	dlight.Diffuse.a=1.0f;

	light.Get_Specular(&temp);
	temp*=light.Get_Intensity();
	dlight.Specular.r=temp.X;
	dlight.Specular.g=temp.Y;
	dlight.Specular.b=temp.Z;
	dlight.Specular.a=1.0f;

	light.Get_Ambient(&temp);
	temp*=light.Get_Intensity();
	dlight.Ambient.r=temp.X;
	dlight.Ambient.g=temp.Y;
	dlight.Ambient.b=temp.Z;
	dlight.Ambient.a=1.0f;

	temp=light.Get_Position();
	dlight.Position=*(D3DVECTOR*) &temp;

	light.Get_Spot_Direction(temp);
	dlight.Direction=*(D3DVECTOR*) &temp;

	dlight.Range=light.Get_Attenuation_Range();
	dlight.Falloff=light.Get_Spot_Exponent();
	dlight.Theta=light.Get_Spot_Angle();
	dlight.Phi=light.Get_Spot_Angle();

	// Inverse linear light 1/(1+D)
	double a,b;
	light.Get_Far_Attenuation_Range(a,b);
	dlight.Attenuation0=1.0f;
	if (fabs(a-b)<1e-5)
		// if the attenuation range is too small assume uniform with cutoff
		dlight.Attenuation1=0.0f;
	else
		// this will cause the light to drop to half intensity at the first far attenuation
		dlight.Attenuation1=(float) 1.0/a;
	dlight.Attenuation2=0.0f;

	Set_Light(index,&dlight);
}

//**********************************************************************************************
//! Set the light environment. This is a lighting model which used up to four
//! directional lights to produce the lighting.
/*! 5/27/02 KJM Added shader light environment support
*/
void DX8Wrapper::Set_Light_Environment(LightEnvironmentClass* light_env)
{
	// Shader light environment support															*
//	if (Light_Environment && light_env && (*Light_Environment)==(*light_env)) return;

	Light_Environment=light_env;

	if (light_env) 
	{
		int light_count = light_env->Get_Light_Count();
		unsigned int color=Convert_Color(light_env->Get_Equivalent_Ambient(),0.0f);
		if (RenderStates[D3DRS_AMBIENT]!=color)
		{
			Set_DX8_Render_State(D3DRS_AMBIENT,color);
//buggy Radeon 9700 driver doesn't apply new ambient unless the material also changes.
#if 1
			render_state_changed|=MATERIAL_CHANGED;
#endif
		}

		D3DLIGHT9 light;		
		int l;
		for (l =0;l<light_count;++l) {
			
			memset(&light,0, sizeof(D3DLIGHT9));
			
			light.Type=D3DLIGHT_DIRECTIONAL;
			(Vector3&)light.Diffuse=light_env->Get_Light_Diffuse(l);
			Vector3 dir=-light_env->Get_Light_Direction(l);
			light.Direction=(const D3DVECTOR&)(dir);

			// (gth) TODO: put specular into LightEnvironment?  Much work to be done on lights :-)'
			if (l==0) {
				light.Specular.r = light.Specular.g = light.Specular.b = 1.0f;
			}

			if (light_env->isPointLight(l)) {
				light.Type = D3DLIGHT_POINT;
				(Vector3&)light.Diffuse=light_env->getPointDiffuse(l);
				(Vector3&)light.Ambient=light_env->getPointAmbient(l);
				light.Position = (const D3DVECTOR&)light_env->getPointCenter(l);
				light.Range = light_env->getPointOrad(l);
				
				// Inverse linear light 1/(1+D)
				double a,b;
				b = light_env->getPointOrad(l);
				a = light_env->getPointIrad(l);

//(gth) CNC3 Generals code for the attenuation factors is causing the lights to over-brighten
//I'm changing the Attenuation0 parameter to 1.0 to avoid this problem.				
#if 0
				light.Attenuation0=0.01f;
#else
				light.Attenuation0=1.0f;
#endif
				if (fabs(a-b)<1e-5)
					// if the attenuation range is too small assume uniform with cutoff
					light.Attenuation1=0.0f;
				else
					// this will cause the light to drop to half intensity at the first far attenuation
					light.Attenuation1=(float) 0.1/a;
	
				light.Attenuation2=8.0f/(b*b);
			}

			Set_Light(l,&light);
		}

		for (;l<4;++l) {
			Set_Light(l,NULL);
		}
	}
/*	else {
		for (int l=0;l<4;++l) {
			Set_Light(l,NULL);
		}
	}
*/
}

IDirect3DSurface9 * DX8Wrapper::_Get_DX8_Front_Buffer()
{
	DX8_THREAD_ASSERT();
	D3DDISPLAYMODE mode;

	DX8CALL(GetDisplayMode(PRIMARY_SWAP_CHAIN,&mode));

	IDirect3DSurface9 * fb=NULL;

	// GetFrontBufferData insists on a system-memory A8R8G8B8 surface of display size,
	// which is exactly what this was already asking CreateImageSurface for.
	DX8CALL(CreateOffscreenPlainSurface(mode.Width,mode.Height,D3DFMT_A8R8G8B8,D3DPOOL_SYSTEMMEM,&fb,NULL));

	DX8CALL(GetFrontBufferData(PRIMARY_SWAP_CHAIN,fb));
	return fb;
}

SurfaceClass * DX8Wrapper::_Get_DX8_Back_Buffer(unsigned int num)
{
	DX8_THREAD_ASSERT();

	IDirect3DSurface9 * bb = NULL;		// left untouched when the call fails; the test below is not
	SurfaceClass *surf=NULL;
	DX8CALL(GetBackBuffer(PRIMARY_SWAP_CHAIN,num,D3DBACKBUFFER_TYPE_MONO,&bb));
	if (bb)
	{
		surf=NEW_REF(SurfaceClass,(bb));
		bb->Release();
	}

	return surf;
}


TextureClass *
DX8Wrapper::Create_Render_Target (int width, int height, WW3DFormat format)
{
	DX8_THREAD_ASSERT();
	DX8_Assert();
	number_of_DX8_calls++;

	// Use the current display format if format isn't specified
	if (format==WW3D_FORMAT_UNKNOWN) {
		D3DDISPLAYMODE mode;
		DX8CALL(GetDisplayMode(PRIMARY_SWAP_CHAIN,&mode));
		format=D3DFormat_To_WW3DFormat(mode.Format);
	}

	// If render target format isn't supported return NULL
	if (!Get_Current_Caps()->Support_Render_To_Texture_Format(format)) {
		WWDEBUG_SAY(("DX8Wrapper - Render target format is not supported\r\n"));
		return NULL;
	}

	//
	//	Note: We're going to force the width and height to be powers of two and equal
	//
	const D3DCAPS9& dx8caps=Get_Current_Caps()->Get_DX8_Caps();
	float poweroftwosize = width;
	if (height > 0 && height < width) {
		poweroftwosize = height;
	}
	poweroftwosize = ::Find_POT (poweroftwosize);

	if (poweroftwosize>dx8caps.MaxTextureWidth) {
		poweroftwosize=dx8caps.MaxTextureWidth;
	}
	if (poweroftwosize>dx8caps.MaxTextureHeight) {
		poweroftwosize=dx8caps.MaxTextureHeight;
	}

	width = height = poweroftwosize;

	//
	//	Attempt to create the render target
	//
	TextureClass * tex = NEW_REF(TextureClass,(width,height,format,MIP_LEVELS_1,TextureClass::POOL_DEFAULT,true));

	// 3dfx drivers are lying in the CheckDeviceFormat call and claiming
	// that they support render targets!
	if (tex->Peek_D3D_Base_Texture() == NULL) 
	{
		WWDEBUG_SAY(("DX8Wrapper - Render target creation failed!\r\n"));
		REF_PTR_RELEASE(tex);
	}

	return tex;
}

//**********************************************************************************************
//! Create render target with associated depth stencil buffer
/*! KJM
*/
void DX8Wrapper::Create_Render_Target
(
	int width, 
	int height, 
	WW3DFormat format,
	WW3DZFormat zformat,
	TextureClass** target,
	ZTextureClass** depth_buffer
)
{
	DX8_THREAD_ASSERT();
	DX8_Assert();
	number_of_DX8_calls++;

	// Use the current display format if format isn't specified
	if (format==WW3D_FORMAT_UNKNOWN) 
	{
		*target=NULL;
		*depth_buffer=NULL;
		return;
/*		D3DDISPLAYMODE mode;
		DX8CALL(GetDisplayMode(PRIMARY_SWAP_CHAIN,&mode));
		format=D3DFormat_To_WW3DFormat(mode.Format);*/
	}

	// If render target format isn't supported return NULL
	if (!Get_Current_Caps()->Support_Render_To_Texture_Format(format) ||
		 !Get_Current_Caps()->Support_Depth_Stencil_Format(zformat)) 
	{
		WWDEBUG_SAY(("DX8Wrapper - Render target with depth format is not supported\r\n"));
		return;
	}

	//	Note: We're going to force the width and height to be powers of two and equal
	const D3DCAPS9& dx8caps=Get_Current_Caps()->Get_DX8_Caps();
	float poweroftwosize = width;
	if (height > 0 && height < width) 
	{
		poweroftwosize = height;
	}
	poweroftwosize = ::Find_POT (poweroftwosize);

	if (poweroftwosize>dx8caps.MaxTextureWidth) 
	{
		poweroftwosize=dx8caps.MaxTextureWidth;
	}

	if (poweroftwosize>dx8caps.MaxTextureHeight) 
	{
		poweroftwosize=dx8caps.MaxTextureHeight;
	}

	width = height = poweroftwosize;

	//	Attempt to create the render target
	TextureClass* tex=NEW_REF(TextureClass,(width,height,format,MIP_LEVELS_1,TextureClass::POOL_DEFAULT,true));

	// 3dfx drivers are lying in the CheckDeviceFormat call and claiming
	// that they support render targets!
	if (tex->Peek_D3D_Base_Texture() == NULL) 
	{
		WWDEBUG_SAY(("DX8Wrapper - Render target creation failed!\r\n"));
		REF_PTR_RELEASE(tex);
	}

	*target=tex;

	// attempt to create the depth stencil buffer
	*depth_buffer=NEW_REF
	(
		ZTextureClass,
		(
			width,
			height,
			zformat,
			MIP_LEVELS_1,
			TextureClass::POOL_DEFAULT
		)
	);
}

/*!
 * Set render target
 * KM Added optional custom z target
 */
void DX8Wrapper::Set_Render_Target_With_Z
(
	TextureClass* texture,
	ZTextureClass* ztexture
)
{
	WWASSERT(texture!=NULL);
	IDirect3DSurface9 * d3d_surf = texture->Get_D3D_Surface_Level();
	WWASSERT(d3d_surf != NULL);

	IDirect3DSurface9* d3d_zbuf=NULL;
	if (ztexture!=NULL)
	{

		d3d_zbuf=ztexture->Get_D3D_Surface_Level();
		WWASSERT(d3d_zbuf!=NULL);
		Set_Render_Target(d3d_surf,d3d_zbuf);
		d3d_zbuf->Release();
	}
	else
	{
		Set_Render_Target(d3d_surf,true);
	}
	d3d_surf->Release();

	IsRenderToTexture = true;
}

//**********************************************************************************************
//! Depth/stencil surface to pair with a non-multisampled render target
/*! Returns NULL when the back buffer is not multisampled - callers then keep whatever depth
**  buffer they already have, which is what every one of them did before multisampling existed.
**  Created lazily at back buffer size, which covers every render target this game binds.
*/
// Samples per pixel actually in use: 0 when not multisampling.  The exe logs this, so a
// player can tell whether "-msaa N" was honoured or quietly degraded.
unsigned DX8Wrapper::Get_MultiSample_Level(void)
{
	return (unsigned)_PresentParameters.MultiSampleType;
}

bool DX8Wrapper::Is_Flip_Present(void)
{
	return false;
}

IDirect3DSurface9 * DX8Wrapper::_Get_Non_MultiSampled_Depth_Buffer(void)
{
	if (_PresentParameters.MultiSampleType == D3DMULTISAMPLE_NONE) return NULL;
	if (_RTTDepthBuffer == NULL && D3DDevice != NULL) {
		if (Render_Failed(D3DDevice->CreateDepthStencilSurface(
				_PresentParameters.BackBufferWidth,_PresentParameters.BackBufferHeight,
				_PresentParameters.AutoDepthStencilFormat,D3DMULTISAMPLE_NONE,
				0,		// MultisampleQuality, meaningless without multisampling
				true,	// Discard, which is what a depth buffer nobody reads back wants
				&_RTTDepthBuffer,
				NULL))) {
			_RTTDepthBuffer = NULL;
		}
	}
	return _RTTDepthBuffer;
}

void
DX8Wrapper::Set_Render_Target(IDirect3DSwapChain9 *swap_chain)
{
	DX8_THREAD_ASSERT();
	WWASSERT (swap_chain != NULL);

	//
	//	Get the back buffer for the swap chain
	//
	LPDIRECT3DSURFACE9 render_target = NULL;
	swap_chain->GetBackBuffer (0, D3DBACKBUFFER_TYPE_MONO, &render_target);

	//
	//	Set this back buffer as the render targer
	//
	Set_Render_Target (render_target, true);

	//
	//	Release our hold on the back buffer
	//
	if (render_target != NULL) {
		render_target->Release ();
		render_target = NULL;
	}

	IsRenderToTexture = false;

	return ;
}

void
DX8Wrapper::Set_Render_Target(IDirect3DSurface9 *render_target, bool use_default_depth_buffer)
{
//#ifndef _XBOX
	DX8_THREAD_ASSERT();
	DX8_Assert();

	//
	//	Should we restore the default render target set a new one?
	//
	if (render_target == NULL || render_target == DefaultRenderTarget) 
	{
		// If there is currently a custom render target, default must NOT be NULL.
		if (CurrentRenderTarget) 
		{
			WWASSERT(DefaultRenderTarget!=NULL);
		}

		//
		//	Restore the default render target
		//
		if (DefaultRenderTarget != NULL) 
		{
			_Set_DX8_Render_Target(DefaultRenderTarget, DefaultDepthBuffer);
			DefaultRenderTarget->Release ();
			DefaultRenderTarget = NULL;
			if (DefaultDepthBuffer) 
			{
				DefaultDepthBuffer->Release ();
				DefaultDepthBuffer = NULL;
			}
		}

		//
		//	Release our hold on the "current" render target
		//
		if (CurrentRenderTarget != NULL) 
		{
			CurrentRenderTarget->Release ();
			CurrentRenderTarget = NULL;
		}

		if (CurrentDepthBuffer!=NULL)
		{
			CurrentDepthBuffer->Release();
			CurrentDepthBuffer=NULL;
		}

	} 
	else if (render_target != CurrentRenderTarget) 
	{
		WWASSERT(DefaultRenderTarget==NULL);

		//
		//	We'll need the depth buffer later...
		//
		if (DefaultDepthBuffer == NULL) 
		{
//		IDirect3DSurface9 *depth_buffer = NULL;
			DX8CALL(GetDepthStencilSurface (&DefaultDepthBuffer));
		}

		//
		//	Get a pointer to the default render target (if necessary)
		//
		if (DefaultRenderTarget == NULL) 
		{
			DX8CALL(GetRenderTarget (PRIMARY_RENDER_TARGET, &DefaultRenderTarget));
		}

		//
		//	Release our hold on the old "current" render target
		//
		if (CurrentRenderTarget != NULL) 
		{
			CurrentRenderTarget->Release ();
			CurrentRenderTarget = NULL;
		}

		if (CurrentDepthBuffer!=NULL)
		{
			CurrentDepthBuffer->Release();
			CurrentDepthBuffer=NULL;
		}

		//
		//	Keep a copy of the current render target (for housekeeping)
		//
		CurrentRenderTarget = render_target;
		WWASSERT (CurrentRenderTarget != NULL);
		if (CurrentRenderTarget != NULL) 
		{
			CurrentRenderTarget->AddRef ();

			//
			//	Switch render targets
			//
			if (use_default_depth_buffer) 
			{
				//	The default depth buffer is multisampled when the back buffer is; this
				//	target is not, so it needs the plain one instead.
				IDirect3DSurface9 * z = _Get_Non_MultiSampled_Depth_Buffer();
				_Set_DX8_Render_Target(CurrentRenderTarget, z ? z : DefaultDepthBuffer);
			}
			else
			{
				_Set_DX8_Render_Target(CurrentRenderTarget, NULL);
			}
		}
	}

	//
	//	Free our hold on the depth buffer
	//
//	if (depth_buffer != NULL) {
//		depth_buffer->Release ();
//		depth_buffer = NULL;
//	}

	IsRenderToTexture = false;
	return ;
//#endif // XBOX
}


//**********************************************************************************************
//! Set render target with depth stencil buffer
/*! KJM
*/
void DX8Wrapper::Set_Render_Target
(
	IDirect3DSurface9* render_target, 
	IDirect3DSurface9* depth_buffer 
)
{
//#ifndef _XBOX
	DX8_THREAD_ASSERT();
	DX8_Assert();

	//
	//	Should we restore the default render target set a new one?
	//
	if (render_target == NULL || render_target == DefaultRenderTarget) 
	{
		// If there is currently a custom render target, default must NOT be NULL.
		if (CurrentRenderTarget) 
		{
			WWASSERT(DefaultRenderTarget!=NULL);
		}

		//
		//	Restore the default render target
		//
		if (DefaultRenderTarget != NULL) 
		{
			_Set_DX8_Render_Target(DefaultRenderTarget, DefaultDepthBuffer);
			DefaultRenderTarget->Release ();
			DefaultRenderTarget = NULL;
			if (DefaultDepthBuffer) 
			{
				DefaultDepthBuffer->Release ();
				DefaultDepthBuffer = NULL;
			}
		}

		//
		//	Release our hold on the "current" render target
		//
		if (CurrentRenderTarget != NULL) 
		{
			CurrentRenderTarget->Release ();
			CurrentRenderTarget = NULL;
		}

		if (CurrentDepthBuffer!=NULL)
		{
			CurrentDepthBuffer->Release();
			CurrentDepthBuffer=NULL;
		}
	}
	else if (render_target != CurrentRenderTarget) 
	{
		WWASSERT(DefaultRenderTarget==NULL);

		//
		//	We'll need the depth buffer later...
		//
		if (DefaultDepthBuffer == NULL) 
		{
//		IDirect3DSurface9 *depth_buffer = NULL;
			DX8CALL(GetDepthStencilSurface (&DefaultDepthBuffer));
		}

		//
		//	Get a pointer to the default render target (if necessary)
		//
		if (DefaultRenderTarget == NULL) 
		{
			DX8CALL(GetRenderTarget (PRIMARY_RENDER_TARGET, &DefaultRenderTarget));
		}

		//
		//	Release our hold on the old "current" render target
		//
		if (CurrentRenderTarget != NULL) 
		{
			CurrentRenderTarget->Release ();
			CurrentRenderTarget = NULL;
		}

		if (CurrentDepthBuffer!=NULL)
		{
			CurrentDepthBuffer->Release();
			CurrentDepthBuffer=NULL;
		}

		//
		//	Keep a copy of the current render target (for housekeeping)
		//
		CurrentRenderTarget = render_target;
		CurrentDepthBuffer = depth_buffer;
		WWASSERT (CurrentRenderTarget != NULL);
		if (CurrentRenderTarget != NULL) 
		{
			CurrentRenderTarget->AddRef ();
			CurrentDepthBuffer->AddRef();

			//
			//	Switch render targets
			//
			_Set_DX8_Render_Target(CurrentRenderTarget, CurrentDepthBuffer);
		}
	}

	IsRenderToTexture=true;
//#endif // XBOX
}


IDirect3DSwapChain9 *
DX8Wrapper::Create_Additional_Swap_Chain (RenderWindow render_window)
{
	DX8_Assert();

	//
	//	Configure the presentation parameters for a windowed render target
	//
	D3DPRESENT_PARAMETERS params				= { 0 };
	params.BackBufferFormat						= _PresentParameters.BackBufferFormat;
	params.BackBufferCount						= 1;
	params.MultiSampleType						= D3DMULTISAMPLE_NONE;
	// D3D9 dropped D3DSWAPEFFECT_COPY_VSYNC: the vsync half of it is a presentation
	// interval now, and the swap effect is only the copy.
	params.SwapEffect								= D3DSWAPEFFECT_COPY;
	params.hDeviceWindow							= render_window;
	params.Windowed								= true;
	params.EnableAutoDepthStencil				= true;
	params.AutoDepthStencilFormat				= _PresentParameters.AutoDepthStencilFormat;
	params.Flags									= 0;
	params.FullScreen_RefreshRateInHz		= D3DPRESENT_RATE_DEFAULT;
	params.PresentationInterval	= D3DPRESENT_INTERVAL_ONE;	// the vsync half of the old COPY_VSYNC swap effect

	//
	//	Create the swap chain
	//
	IDirect3DSwapChain9 *swap_chain = NULL;
	DX8CALL(CreateAdditionalSwapChain(&params, &swap_chain));
	return swap_chain;
}

void DX8Wrapper::Flush_DX8_Resource_Manager(unsigned int bytes)
{
	DX8_Assert();
	// D3D9's EvictManagedResources takes no byte count and discards the lot; the argument
	// is kept because every caller passes 0, which meant the same thing to D3D8.
	WWASSERT(bytes==0);
	DX8CALL(EvictManagedResources());
}

unsigned int DX8Wrapper::Get_Free_Texture_RAM()
{
	DX8_Assert();
	number_of_DX8_calls++;
	return DX8Wrapper::_Get_D3D_Device()->GetAvailableTextureMem();
}

// Converts a linear gamma ramp to one that is controlled by:
// Gamma - controls the curvature of the middle of the curve
// Bright - controls the minimum value of the curve
// Contrast - controls the difference between the maximum and the minimum of the curve
void DX8Wrapper::Set_Gamma(float gamma,float bright,float contrast,bool calibrate,bool uselimit)
{
	gamma=Bound(gamma,0.6f,6.0f);
	bright=Bound(bright,-0.5f,0.5f);
	contrast=Bound(contrast,0.5f,2.0f);
	float oo_gamma=1.0f/gamma;

	DX8_Assert();
	number_of_DX8_calls++;

	uint32 flag=(calibrate?D3DSGR_CALIBRATE:D3DSGR_NO_CALIBRATION);

	D3DGAMMARAMP ramp;
	float			 limit;	

	// IML: I'm not really sure what the intent of the 'limit' variable is. It does not produce useful results for my purposes.
	if (uselimit) {
		limit=(contrast-1)/2*contrast;
	} else {
		limit = 0.0f;
	}

	// HY - arrived at this equation after much trial and error.
	for (int i=0; i<256; i++) {
		float in,out;
		in=i/256.0f;
		float x=in-limit;
		x=Bound(x,0.0f,1.0f);
		x=powf(x,oo_gamma);
		out=contrast*x+bright;
		out=Bound(out,0.0f,1.0f);
		ramp.red[i]=(WORD) (out*65535);
		ramp.green[i]=(WORD) (out*65535);
		ramp.blue[i]=(WORD) (out*65535);
	}

	if (Get_Current_Caps()->Support_Gamma() && !_PresentParameters.Windowed)	{
		DX8Wrapper::_Get_D3D_Device()->SetGammaRamp(PRIMARY_SWAP_CHAIN,flag,&ramp);
	} else if (!IsWindowed || Direct3D11_Present_Is_Enabled()) {
		// A windowed Direct3D 9 device ignores its gamma ramp, and a fullscreen game's device is a
		// windowed one under either renderer, so the fullscreen display sets the desktop's, keeps the
		// desktop's own to give back on the way out, and puts the game's on again when it comes back.
		save_desktop_gamma();
		GameGammaRamp = ramp;
		GameGammaSet = true;
		set_desktop_gamma(&GameGammaRamp);
	} else {
#if defined(_WIN32)	// the desktop's ramp; off Windows the gamma is the swap chain's (A3)
		HWND hwnd = GetDesktopWindow();
		HDC hdc = GetDC(hwnd);
		if (hdc)
		{
			SetDeviceGammaRamp (hdc, &ramp);
			ReleaseDC (hwnd, hdc);
		}
#endif
	}
}

//**********************************************************************************************
//! Resets render device to default state
/*!
*/
void DX8Wrapper::Apply_Default_State()
{
	SNAPSHOT_SAY(("DX8Wrapper::Apply_Default_State()\n"));
	
	// only set states used in game
	Set_DX8_Render_State(D3DRS_ZENABLE, true);
//	Set_DX8_Render_State(D3DRS_FILLMODE, D3DFILL_SOLID);
	Set_DX8_Render_State(D3DRS_SHADEMODE, D3DSHADE_GOURAUD);
	//Set_DX8_Render_State(D3DRS_LINEPATTERN, 0);
	Set_DX8_Render_State(D3DRS_ZWRITEENABLE, true);
	Set_DX8_Render_State(D3DRS_ALPHATESTENABLE, false);
	//Set_DX8_Render_State(D3DRS_LASTPIXEL, FALSE);
	Set_DX8_Render_State(D3DRS_SRCBLEND, D3DBLEND_ONE);
	Set_DX8_Render_State(D3DRS_DESTBLEND, D3DBLEND_ZERO);
	Set_DX8_Render_State(D3DRS_CULLMODE, D3DCULL_CW);
	Set_DX8_Render_State(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
	Set_DX8_Render_State(D3DRS_ALPHAREF, 0);
	Set_DX8_Render_State(D3DRS_ALPHAFUNC, D3DCMP_LESSEQUAL);
	Set_DX8_Render_State(D3DRS_DITHERENABLE, false);
	Set_DX8_Render_State(D3DRS_ALPHABLENDENABLE, false);
	Set_DX8_Render_State(D3DRS_FOGENABLE, false);
	Set_DX8_Render_State(D3DRS_SPECULARENABLE, false);
//	Set_DX8_Render_State(D3DRS_ZVISIBLE, FALSE);
//	Set_DX8_Render_State(D3DRS_FOGCOLOR, 0);
//	Set_DX8_Render_State(D3DRS_FOGTABLEMODE, D3DFOG_NONE);
//	Set_DX8_Render_State(D3DRS_FOGSTART, 0);

//	Set_DX8_Render_State(D3DRS_FOGEND, WWMath::Float_As_Int(1.0f));
//	Set_DX8_Render_State(D3DRS_FOGDENSITY, WWMath::Float_As_Int(1.0f));

	//Set_DX8_Render_State(D3DRS_EDGEANTIALIAS, FALSE);
	Set_DX8_ZBias(0);
//	Set_DX8_Render_State(D3DRS_RANGEFOGENABLE, FALSE);
	Set_DX8_Render_State(D3DRS_STENCILENABLE, false);
	Set_DX8_Render_State(D3DRS_STENCILFAIL, D3DSTENCILOP_KEEP);
	Set_DX8_Render_State(D3DRS_STENCILZFAIL, D3DSTENCILOP_KEEP);
	Set_DX8_Render_State(D3DRS_STENCILPASS, D3DSTENCILOP_KEEP);
	Set_DX8_Render_State(D3DRS_STENCILFUNC, D3DCMP_ALWAYS);
	Set_DX8_Render_State(D3DRS_STENCILREF, 0);
	Set_DX8_Render_State(D3DRS_STENCILMASK, 0xffffffff);
	Set_DX8_Render_State(D3DRS_STENCILWRITEMASK, 0xffffffff);
	Set_DX8_Render_State(D3DRS_TEXTUREFACTOR, 0);
/*	Set_DX8_Render_State(D3DRS_WRAP0, D3DWRAP_U| D3DWRAP_V);
	Set_DX8_Render_State(D3DRS_WRAP1, D3DWRAP_U| D3DWRAP_V);
	Set_DX8_Render_State(D3DRS_WRAP2, D3DWRAP_U| D3DWRAP_V);
	Set_DX8_Render_State(D3DRS_WRAP3, D3DWRAP_U| D3DWRAP_V);
	Set_DX8_Render_State(D3DRS_WRAP4, D3DWRAP_U| D3DWRAP_V);
	Set_DX8_Render_State(D3DRS_WRAP5, D3DWRAP_U| D3DWRAP_V);
	Set_DX8_Render_State(D3DRS_WRAP6, D3DWRAP_U| D3DWRAP_V);
	Set_DX8_Render_State(D3DRS_WRAP7, D3DWRAP_U| D3DWRAP_V);*/
	Set_DX8_Render_State(D3DRS_CLIPPING, true);
	Set_DX8_Render_State(D3DRS_LIGHTING, false);
	//Set_DX8_Render_State(D3DRS_AMBIENT, 0);
//	Set_DX8_Render_State(D3DRS_FOGVERTEXMODE, D3DFOG_NONE);
	Set_DX8_Render_State(D3DRS_COLORVERTEX, true);
/*	Set_DX8_Render_State(D3DRS_LOCALVIEWER, TRUE);
	Set_DX8_Render_State(D3DRS_NORMALIZENORMALS, FALSE);
	Set_DX8_Render_State(D3DRS_DIFFUSEMATERIALSOURCE, D3DMCS_COLOR1);
	Set_DX8_Render_State(D3DRS_SPECULARMATERIALSOURCE, D3DMCS_COLOR2);
	Set_DX8_Render_State(D3DRS_AMBIENTMATERIALSOURCE, D3DMCS_MATERIAL);
	Set_DX8_Render_State(D3DRS_EMISSIVEMATERIALSOURCE, D3DMCS_MATERIAL);
	Set_DX8_Render_State(D3DRS_VERTEXBLEND, D3DVBF_DISABLE);*/
	//Set_DX8_Render_State(D3DRS_CLIPPLANEENABLE, 0);
	// D3D9 took this off the render state list; it has its own call now.
	DX8CALL(SetSoftwareVertexProcessing(false));
	//Set_DX8_Render_State(D3DRS_POINTSIZE, 0x3f800000);
	//Set_DX8_Render_State(D3DRS_POINTSIZE_MIN, 0);
	//Set_DX8_Render_State(D3DRS_POINTSPRITEENABLE, FALSE);
	//Set_DX8_Render_State(D3DRS_POINTSCALEENABLE, FALSE);
	//Set_DX8_Render_State(D3DRS_POINTSCALE_A, 0);
	//Set_DX8_Render_State(D3DRS_POINTSCALE_B, 0);
	//Set_DX8_Render_State(D3DRS_POINTSCALE_C, 0);
	//Set_DX8_Render_State(D3DRS_MULTISAMPLEANTIALIAS, TRUE);
	//Set_DX8_Render_State(D3DRS_MULTISAMPLEMASK, 0xffffffff);
	//Set_DX8_Render_State(D3DRS_PATCHEDGESTYLE, D3DPATCHEDGE_DISCRETE);
	//Set_DX8_Render_State(D3DRS_PATCHSEGMENTS, 0x3f800000);
	//Set_DX8_Render_State(D3DRS_DEBUGMONITORTOKEN, D3DDMT_ENABLE);
	//Set_DX8_Render_State(D3DRS_POINTSIZE_MAX, Float_At_Int(64.0f));
	//Set_DX8_Render_State(D3DRS_INDEXEDVERTEXBLENDENABLE, FALSE);
	Set_DX8_Render_State(D3DRS_COLORWRITEENABLE, 0x0000000f);
	//Set_DX8_Render_State(D3DRS_TWEENFACTOR, 0);
	Set_DX8_Render_State(D3DRS_BLENDOP, D3DBLENDOP_ADD);
	//Set_DX8_Render_State(D3DRS_POSITIONORDER, D3DORDER_CUBIC);
	//Set_DX8_Render_State(D3DRS_NORMALORDER, D3DORDER_LINEAR);

	// disable TSS stages
	int i;
	for (i=0; i<CurrentCaps->Get_Max_Textures_Per_Pass(); i++)
	{
		Set_DX8_Texture_Stage_State(i, D3DTSS_COLOROP, D3DTOP_DISABLE);
		Set_DX8_Texture_Stage_State(i, D3DTSS_COLORARG1, D3DTA_TEXTURE);
		Set_DX8_Texture_Stage_State(i, D3DTSS_COLORARG2, D3DTA_DIFFUSE);

		Set_DX8_Texture_Stage_State(i, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
		Set_DX8_Texture_Stage_State(i, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
		Set_DX8_Texture_Stage_State(i, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);
	
		/*Set_DX8_Texture_Stage_State(i, D3DTSS_BUMPENVMAT00, 0);
		Set_DX8_Texture_Stage_State(i, D3DTSS_BUMPENVMAT01, 0);
		Set_DX8_Texture_Stage_State(i, D3DTSS_BUMPENVMAT10, 0);
		Set_DX8_Texture_Stage_State(i, D3DTSS_BUMPENVMAT11, 0);
		Set_DX8_Texture_Stage_State(i, D3DTSS_BUMPENVLSCALE, 0);
		Set_DX8_Texture_Stage_State(i, D3DTSS_BUMPENVLOFFSET, 0);*/

		Set_DX8_Texture_Stage_State(i, D3DTSS_TEXCOORDINDEX, i);
		

		Set_DX8_Texture_Stage_State(i, D3DSAMP_ADDRESSU, D3DTADDRESS_WRAP);
		Set_DX8_Texture_Stage_State(i, D3DSAMP_ADDRESSV, D3DTADDRESS_WRAP);
		Set_DX8_Texture_Stage_State(i, D3DSAMP_BORDERCOLOR, 0);
//		Set_DX8_Texture_Stage_State(i, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
//		Set_DX8_Texture_Stage_State(i, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
//		Set_DX8_Texture_Stage_State(i, D3DSAMP_MIPFILTER, D3DTEXF_LINEAR);
//		Set_DX8_Texture_Stage_State(i, D3DSAMP_MIPMAPLODBIAS, 0);
//		Set_DX8_Texture_Stage_State(i, D3DTSS_MAXMIPLEVEL, 0);
//		Set_DX8_Texture_Stage_State(i, D3DSAMP_MAXANISOTROPY, 1);
		//Set_DX8_Texture_Stage_State(i, D3DSAMP_ADDRESSW, D3DTADDRESS_WRAP);
		//Set_DX8_Texture_Stage_State(i, D3DTSS_COLORARG0, D3DTA_CURRENT);
		//Set_DX8_Texture_Stage_State(i, D3DTSS_ALPHAARG0, D3DTA_CURRENT);
		//Set_DX8_Texture_Stage_State(i, D3DTSS_RESULTARG, D3DTA_CURRENT);

		Set_DX8_Texture_Stage_State(i, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
		Set_Texture(i,NULL);
	}

//	DX8Wrapper::Set_Material(NULL);
	VertexMaterialClass::Apply_Null();

	for (unsigned index=0;index<4;++index) {
		SNAPSHOT_SAY(("Clearing light %d to NULL\n",index));
		Set_DX8_Light(index,NULL);
	}

	// set up simple default TSS 
	Vector4 vconst[MAX_VERTEX_SHADER_CONSTANTS];
	memset(vconst,0,sizeof(Vector4)*MAX_VERTEX_SHADER_CONSTANTS);
	Set_Vertex_Shader_Constant(0, vconst, MAX_VERTEX_SHADER_CONSTANTS);

	Vector4 pconst[MAX_PIXEL_SHADER_CONSTANTS];
	memset(pconst,0,sizeof(Vector4)*MAX_PIXEL_SHADER_CONSTANTS);
	Set_Pixel_Shader_Constant(0, pconst, MAX_PIXEL_SHADER_CONSTANTS);

	Set_Vertex_Format(DX8_FVF_XYZNDUV2);
	Set_Pixel_Shader(NULL);

	ShaderClass::Invalidate();
}

const char* DX8Wrapper::Get_DX8_Render_State_Name(D3DRENDERSTATETYPE state)
{
	switch (state) {
	case D3DRS_ZENABLE                       : return "D3DRS_ZENABLE";
	case D3DRS_FILLMODE                      : return "D3DRS_FILLMODE";
	case D3DRS_SHADEMODE                     : return "D3DRS_SHADEMODE";
	case D3DRS_ZWRITEENABLE                  : return "D3DRS_ZWRITEENABLE";
	case D3DRS_ALPHATESTENABLE               : return "D3DRS_ALPHATESTENABLE";
	case D3DRS_LASTPIXEL                     : return "D3DRS_LASTPIXEL";
	case D3DRS_SRCBLEND                      : return "D3DRS_SRCBLEND";
	case D3DRS_DESTBLEND                     : return "D3DRS_DESTBLEND";
	case D3DRS_CULLMODE                      : return "D3DRS_CULLMODE";
	case D3DRS_ZFUNC                         : return "D3DRS_ZFUNC";
	case D3DRS_ALPHAREF                      : return "D3DRS_ALPHAREF";
	case D3DRS_ALPHAFUNC                     : return "D3DRS_ALPHAFUNC";
	case D3DRS_DITHERENABLE                  : return "D3DRS_DITHERENABLE";
	case D3DRS_ALPHABLENDENABLE              : return "D3DRS_ALPHABLENDENABLE";
	case D3DRS_FOGENABLE                     : return "D3DRS_FOGENABLE";
	case D3DRS_SPECULARENABLE                : return "D3DRS_SPECULARENABLE";
	case D3DRS_FOGCOLOR                      : return "D3DRS_FOGCOLOR";
	case D3DRS_FOGTABLEMODE                  : return "D3DRS_FOGTABLEMODE";
	case D3DRS_FOGSTART                      : return "D3DRS_FOGSTART";
	case D3DRS_FOGEND                        : return "D3DRS_FOGEND";
	case D3DRS_FOGDENSITY                    : return "D3DRS_FOGDENSITY";
	case D3DRS_DEPTHBIAS                     : return "D3DRS_DEPTHBIAS";
	case D3DRS_RANGEFOGENABLE                : return "D3DRS_RANGEFOGENABLE";
	case D3DRS_STENCILENABLE                 : return "D3DRS_STENCILENABLE";
	case D3DRS_STENCILFAIL                   : return "D3DRS_STENCILFAIL";
	case D3DRS_STENCILZFAIL                  : return "D3DRS_STENCILZFAIL";
	case D3DRS_STENCILPASS                   : return "D3DRS_STENCILPASS";
	case D3DRS_STENCILFUNC                   : return "D3DRS_STENCILFUNC";
	case D3DRS_STENCILREF                    : return "D3DRS_STENCILREF";
	case D3DRS_STENCILMASK                   : return "D3DRS_STENCILMASK";
	case D3DRS_STENCILWRITEMASK              : return "D3DRS_STENCILWRITEMASK";
	case D3DRS_TEXTUREFACTOR                 : return "D3DRS_TEXTUREFACTOR";
	case D3DRS_WRAP0                         : return "D3DRS_WRAP0";
	case D3DRS_WRAP1                         : return "D3DRS_WRAP1";
	case D3DRS_WRAP2                         : return "D3DRS_WRAP2";
	case D3DRS_WRAP3                         : return "D3DRS_WRAP3";
	case D3DRS_WRAP4                         : return "D3DRS_WRAP4";
	case D3DRS_WRAP5                         : return "D3DRS_WRAP5";
	case D3DRS_WRAP6                         : return "D3DRS_WRAP6";
	case D3DRS_WRAP7                         : return "D3DRS_WRAP7";
	case D3DRS_CLIPPING                      : return "D3DRS_CLIPPING";
	case D3DRS_LIGHTING                      : return "D3DRS_LIGHTING";
	case D3DRS_AMBIENT                       : return "D3DRS_AMBIENT";
	case D3DRS_FOGVERTEXMODE                 : return "D3DRS_FOGVERTEXMODE";
	case D3DRS_COLORVERTEX                   : return "D3DRS_COLORVERTEX";
	case D3DRS_LOCALVIEWER                   : return "D3DRS_LOCALVIEWER";
	case D3DRS_NORMALIZENORMALS              : return "D3DRS_NORMALIZENORMALS";
	case D3DRS_DIFFUSEMATERIALSOURCE         : return "D3DRS_DIFFUSEMATERIALSOURCE";
	case D3DRS_SPECULARMATERIALSOURCE        : return "D3DRS_SPECULARMATERIALSOURCE";
	case D3DRS_AMBIENTMATERIALSOURCE         : return "D3DRS_AMBIENTMATERIALSOURCE";
	case D3DRS_EMISSIVEMATERIALSOURCE        : return "D3DRS_EMISSIVEMATERIALSOURCE";
	case D3DRS_VERTEXBLEND                   : return "D3DRS_VERTEXBLEND";
	case D3DRS_CLIPPLANEENABLE               : return "D3DRS_CLIPPLANEENABLE";
	case D3DRS_POINTSIZE                     : return "D3DRS_POINTSIZE";
	case D3DRS_POINTSIZE_MIN                 : return "D3DRS_POINTSIZE_MIN";
	case D3DRS_POINTSPRITEENABLE             : return "D3DRS_POINTSPRITEENABLE";
	case D3DRS_POINTSCALEENABLE              : return "D3DRS_POINTSCALEENABLE";
	case D3DRS_POINTSCALE_A                  : return "D3DRS_POINTSCALE_A";
	case D3DRS_POINTSCALE_B                  : return "D3DRS_POINTSCALE_B";
	case D3DRS_POINTSCALE_C                  : return "D3DRS_POINTSCALE_C";
	case D3DRS_MULTISAMPLEANTIALIAS          : return "D3DRS_MULTISAMPLEANTIALIAS";
	case D3DRS_MULTISAMPLEMASK               : return "D3DRS_MULTISAMPLEMASK";
	case D3DRS_PATCHEDGESTYLE                : return "D3DRS_PATCHEDGESTYLE";
	case D3DRS_DEBUGMONITORTOKEN             : return "D3DRS_DEBUGMONITORTOKEN";
	case D3DRS_POINTSIZE_MAX                 : return "D3DRS_POINTSIZE_MAX";
	case D3DRS_INDEXEDVERTEXBLENDENABLE      : return "D3DRS_INDEXEDVERTEXBLENDENABLE";
	case D3DRS_COLORWRITEENABLE              : return "D3DRS_COLORWRITEENABLE";
	case D3DRS_TWEENFACTOR                   : return "D3DRS_TWEENFACTOR";
	case D3DRS_BLENDOP                       : return "D3DRS_BLENDOP";
//	case D3DRS_POSITIONORDER                 : return "D3DRS_POSITIONORDER";
//	case D3DRS_NORMALORDER                   : return "D3DRS_NORMALORDER";
	default											  : return "UNKNOWN";
	}
}

const char* DX8Wrapper::Get_DX8_Texture_Stage_State_Name(D3DTEXTURESTAGESTATETYPE state)
{
	switch (state) {
	case D3DTSS_COLOROP                   : return "D3DTSS_COLOROP";
	case D3DTSS_COLORARG1                 : return "D3DTSS_COLORARG1";
	case D3DTSS_COLORARG2                 : return "D3DTSS_COLORARG2";
	case D3DTSS_ALPHAOP                   : return "D3DTSS_ALPHAOP";
	case D3DTSS_ALPHAARG1                 : return "D3DTSS_ALPHAARG1";
	case D3DTSS_ALPHAARG2                 : return "D3DTSS_ALPHAARG2";
	case D3DTSS_BUMPENVMAT00              : return "D3DTSS_BUMPENVMAT00";
	case D3DTSS_BUMPENVMAT01              : return "D3DTSS_BUMPENVMAT01";
	case D3DTSS_BUMPENVMAT10              : return "D3DTSS_BUMPENVMAT10";
	case D3DTSS_BUMPENVMAT11              : return "D3DTSS_BUMPENVMAT11";
	case D3DTSS_TEXCOORDINDEX             : return "D3DTSS_TEXCOORDINDEX";
	case D3DTSS_BUMPENVLSCALE             : return "D3DTSS_BUMPENVLSCALE";
	case D3DTSS_BUMPENVLOFFSET            : return "D3DTSS_BUMPENVLOFFSET";
	case D3DTSS_TEXTURETRANSFORMFLAGS     : return "D3DTSS_TEXTURETRANSFORMFLAGS";
	case D3DTSS_COLORARG0                 : return "D3DTSS_COLORARG0";
	case D3DTSS_ALPHAARG0                 : return "D3DTSS_ALPHAARG0";
	case D3DTSS_RESULTARG                 : return "D3DTSS_RESULTARG";
	default										  : return "UNKNOWN";
	}
}

// The half of the old texture stage state list that D3D9 moved onto the sampler.
const char* DX8Wrapper::Get_DX8_Sampler_State_Name(D3DSAMPLERSTATETYPE state)
{
	switch (state) {
	case D3DSAMP_ADDRESSU                 : return "D3DSAMP_ADDRESSU";
	case D3DSAMP_ADDRESSV                 : return "D3DSAMP_ADDRESSV";
	case D3DSAMP_ADDRESSW                 : return "D3DSAMP_ADDRESSW";
	case D3DSAMP_BORDERCOLOR              : return "D3DSAMP_BORDERCOLOR";
	case D3DSAMP_MAGFILTER                : return "D3DSAMP_MAGFILTER";
	case D3DSAMP_MINFILTER                : return "D3DSAMP_MINFILTER";
	case D3DSAMP_MIPFILTER                : return "D3DSAMP_MIPFILTER";
	case D3DSAMP_MIPMAPLODBIAS            : return "D3DSAMP_MIPMAPLODBIAS";
	case D3DSAMP_MAXMIPLEVEL              : return "D3DSAMP_MAXMIPLEVEL";
	case D3DSAMP_MAXANISOTROPY            : return "D3DSAMP_MAXANISOTROPY";
	default										  : return "UNKNOWN";
	}
}

void DX8Wrapper::Get_DX8_Render_State_Value_Name(StringClass& name, D3DRENDERSTATETYPE state, unsigned value)
{
	switch (state) {
	case D3DRS_ZENABLE:
		name=Get_DX8_ZBuffer_Type_Name(value);
		break;

	case D3DRS_FILLMODE:
		name=Get_DX8_Fill_Mode_Name(value);
		break;

	case D3DRS_SHADEMODE:
		name=Get_DX8_Shade_Mode_Name(value);
		break;

	case D3DRS_FOGCOLOR:
	case D3DRS_ALPHAREF:
	case D3DRS_STENCILMASK:
	case D3DRS_STENCILWRITEMASK:
	case D3DRS_TEXTUREFACTOR:
	case D3DRS_AMBIENT:
	case D3DRS_CLIPPLANEENABLE:
	case D3DRS_MULTISAMPLEMASK:
		name.Format("0x%x",value);
		break;

	case D3DRS_ZWRITEENABLE:
	case D3DRS_ALPHATESTENABLE:
	case D3DRS_LASTPIXEL:
	case D3DRS_DITHERENABLE:
	case D3DRS_ALPHABLENDENABLE:
	case D3DRS_FOGENABLE:
	case D3DRS_SPECULARENABLE:
	case D3DRS_STENCILENABLE:
	case D3DRS_RANGEFOGENABLE:
	case D3DRS_CLIPPING:
	case D3DRS_LIGHTING:
	case D3DRS_COLORVERTEX:
	case D3DRS_LOCALVIEWER:
	case D3DRS_NORMALIZENORMALS:
	case D3DRS_POINTSPRITEENABLE:
	case D3DRS_POINTSCALEENABLE:
	case D3DRS_MULTISAMPLEANTIALIAS:
	case D3DRS_INDEXEDVERTEXBLENDENABLE:
		name=value ? "TRUE" : "FALSE";
		break;

	case D3DRS_SRCBLEND:
	case D3DRS_DESTBLEND:
		name=Get_DX8_Blend_Name(value);
		break;

	case D3DRS_CULLMODE:
		name=Get_DX8_Cull_Mode_Name(value);
		break;

	case D3DRS_ZFUNC:
	case D3DRS_ALPHAFUNC:
	case D3DRS_STENCILFUNC:
		name=Get_DX8_Cmp_Func_Name(value);
		break;

	case D3DRS_FOGTABLEMODE:
	case D3DRS_FOGVERTEXMODE:
		name=Get_DX8_Fog_Mode_Name(value);
		break;

	case D3DRS_FOGSTART:
	case D3DRS_FOGEND:
	case D3DRS_FOGDENSITY:
	case D3DRS_POINTSIZE:
	case D3DRS_POINTSIZE_MIN:
	case D3DRS_POINTSCALE_A:
	case D3DRS_POINTSCALE_B:
	case D3DRS_POINTSCALE_C:
	case D3DRS_POINTSIZE_MAX:
	case D3DRS_TWEENFACTOR:
	case D3DRS_DEPTHBIAS:
		name.Format("%f",*(float*)&value);
		break;

	case D3DRS_STENCILREF:
		name.Format("%d",value);
		break;

	case D3DRS_STENCILFAIL:
	case D3DRS_STENCILZFAIL:
	case D3DRS_STENCILPASS:
		name=Get_DX8_Stencil_Op_Name(value);
		break;

	case D3DRS_WRAP0:
	case D3DRS_WRAP1:
	case D3DRS_WRAP2:
	case D3DRS_WRAP3:
	case D3DRS_WRAP4:
	case D3DRS_WRAP5:
	case D3DRS_WRAP6:
	case D3DRS_WRAP7:
		name="0";
		if (value&D3DWRAP_U) name+="|D3DWRAP_U";
		if (value&D3DWRAP_V) name+="|D3DWRAP_V";
		if (value&D3DWRAP_W) name+="|D3DWRAP_W";
		break;

	case D3DRS_DIFFUSEMATERIALSOURCE:
	case D3DRS_SPECULARMATERIALSOURCE:
	case D3DRS_AMBIENTMATERIALSOURCE:
	case D3DRS_EMISSIVEMATERIALSOURCE:
		name=Get_DX8_Material_Source_Name(value);
		break;

	case D3DRS_VERTEXBLEND:
		name=Get_DX8_Vertex_Blend_Flag_Name(value);
		break;

	case D3DRS_PATCHEDGESTYLE:
		name=Get_DX8_Patch_Edge_Style_Name(value);
		break;

	case D3DRS_DEBUGMONITORTOKEN:
		name=Get_DX8_Debug_Monitor_Token_Name(value);
		break;

	case D3DRS_COLORWRITEENABLE:
		name="0";
		if (value&D3DCOLORWRITEENABLE_RED) name+="|D3DCOLORWRITEENABLE_RED";
		if (value&D3DCOLORWRITEENABLE_GREEN) name+="|D3DCOLORWRITEENABLE_GREEN";
		if (value&D3DCOLORWRITEENABLE_BLUE) name+="|D3DCOLORWRITEENABLE_BLUE";
		if (value&D3DCOLORWRITEENABLE_ALPHA) name+="|D3DCOLORWRITEENABLE_ALPHA";
		break;
	case D3DRS_BLENDOP:
		name=Get_DX8_Blend_Op_Name(value);
		break;
	default:
		name.Format("UNKNOWN (%d)",value);
		break;
	}
}

void DX8Wrapper::Get_DX8_Texture_Stage_State_Value_Name(StringClass& name, D3DTEXTURESTAGESTATETYPE state, unsigned value)
{
	switch (state) {
	case D3DTSS_COLOROP:
	case D3DTSS_ALPHAOP:
		name=Get_DX8_Texture_Op_Name(value);
		break;

	case D3DTSS_COLORARG0:
	case D3DTSS_COLORARG1:
	case D3DTSS_COLORARG2:
	case D3DTSS_ALPHAARG0:
	case D3DTSS_ALPHAARG1:
	case D3DTSS_ALPHAARG2:
	case D3DTSS_RESULTARG:
		name=Get_DX8_Texture_Arg_Name(value);
		break;

	case D3DTSS_TEXTURETRANSFORMFLAGS:
		name=Get_DX8_Texture_Transform_Flag_Name(value);

	// Floating point values
	case D3DTSS_BUMPENVMAT00:
	case D3DTSS_BUMPENVMAT01:
	case D3DTSS_BUMPENVMAT10:
	case D3DTSS_BUMPENVMAT11:
	case D3DTSS_BUMPENVLSCALE:
	case D3DTSS_BUMPENVLOFFSET:
		name.Format("%f",*(float*)&value);
		break;

	case D3DTSS_TEXCOORDINDEX:
		if ((value&0xffff0000)==D3DTSS_TCI_CAMERASPACENORMAL) {
			name.Format("D3DTSS_TCI_CAMERASPACENORMAL|%d",value&0xffff);
		}
		else if ((value&0xffff0000)==D3DTSS_TCI_CAMERASPACEPOSITION) {
			name.Format("D3DTSS_TCI_CAMERASPACEPOSITION|%d",value&0xffff);
		}
		else if ((value&0xffff0000)==D3DTSS_TCI_CAMERASPACEREFLECTIONVECTOR) {
			name.Format("D3DTSS_TCI_CAMERASPACEREFLECTIONVECTOR|%d",value&0xffff);
		}
		else {
			name.Format("%d",value);
		}
		break;

	default:
		name.Format("UNKNOWN (%d)",value);
		break;
	}
}

// The value side of the states D3D9 moved onto the sampler.
void DX8Wrapper::Get_DX8_Sampler_State_Value_Name(StringClass& name, D3DSAMPLERSTATETYPE state, unsigned value)
{
	switch (state) {
	case D3DSAMP_ADDRESSU:
	case D3DSAMP_ADDRESSV:
	case D3DSAMP_ADDRESSW:
		name=Get_DX8_Texture_Address_Name(value);
		break;

	case D3DSAMP_MAGFILTER:
	case D3DSAMP_MINFILTER:
	case D3DSAMP_MIPFILTER:
		name=Get_DX8_Texture_Filter_Name(value);
		break;

	case D3DSAMP_MIPMAPLODBIAS:
		name.Format("%f",*(float*)&value);
		break;

	case D3DSAMP_MAXMIPLEVEL:
	case D3DSAMP_MAXANISOTROPY:
		name.Format("%d",value);
		break;

	case D3DSAMP_BORDERCOLOR:
		name.Format("0x%x",value);
		break;

	default:
		name.Format("UNKNOWN (%d)",value);
		break;
	}
}

const char* DX8Wrapper::Get_DX8_Texture_Op_Name(unsigned value)
{
	switch (value) {
	case D3DTOP_DISABLE                      : return "D3DTOP_DISABLE";
	case D3DTOP_SELECTARG1                   : return "D3DTOP_SELECTARG1";
	case D3DTOP_SELECTARG2                   : return "D3DTOP_SELECTARG2";
	case D3DTOP_MODULATE                     : return "D3DTOP_MODULATE";
	case D3DTOP_MODULATE2X                   : return "D3DTOP_MODULATE2X";
	case D3DTOP_MODULATE4X                   : return "D3DTOP_MODULATE4X";
	case D3DTOP_ADD                          : return "D3DTOP_ADD";
	case D3DTOP_ADDSIGNED                    : return "D3DTOP_ADDSIGNED";
	case D3DTOP_ADDSIGNED2X                  : return "D3DTOP_ADDSIGNED2X";
	case D3DTOP_SUBTRACT                     : return "D3DTOP_SUBTRACT";
	case D3DTOP_ADDSMOOTH                    : return "D3DTOP_ADDSMOOTH";
	case D3DTOP_BLENDDIFFUSEALPHA            : return "D3DTOP_BLENDDIFFUSEALPHA";
	case D3DTOP_BLENDTEXTUREALPHA            : return "D3DTOP_BLENDTEXTUREALPHA";
	case D3DTOP_BLENDFACTORALPHA             : return "D3DTOP_BLENDFACTORALPHA";
	case D3DTOP_BLENDTEXTUREALPHAPM          : return "D3DTOP_BLENDTEXTUREALPHAPM";
	case D3DTOP_BLENDCURRENTALPHA            : return "D3DTOP_BLENDCURRENTALPHA";
	case D3DTOP_PREMODULATE                  : return "D3DTOP_PREMODULATE";
	case D3DTOP_MODULATEALPHA_ADDCOLOR       : return "D3DTOP_MODULATEALPHA_ADDCOLOR";
	case D3DTOP_MODULATECOLOR_ADDALPHA       : return "D3DTOP_MODULATECOLOR_ADDALPHA";
	case D3DTOP_MODULATEINVALPHA_ADDCOLOR    : return "D3DTOP_MODULATEINVALPHA_ADDCOLOR";
	case D3DTOP_MODULATEINVCOLOR_ADDALPHA    : return "D3DTOP_MODULATEINVCOLOR_ADDALPHA";
	case D3DTOP_BUMPENVMAP                   : return "D3DTOP_BUMPENVMAP";
	case D3DTOP_BUMPENVMAPLUMINANCE          : return "D3DTOP_BUMPENVMAPLUMINANCE";
	case D3DTOP_DOTPRODUCT3                  : return "D3DTOP_DOTPRODUCT3";
	case D3DTOP_MULTIPLYADD                  : return "D3DTOP_MULTIPLYADD";
	case D3DTOP_LERP                         : return "D3DTOP_LERP";
	default										     : return "UNKNOWN";
	}
}

const char* DX8Wrapper::Get_DX8_Texture_Arg_Name(unsigned value)
{
	switch (value) {
	case D3DTA_CURRENT			: return "D3DTA_CURRENT";
	case D3DTA_DIFFUSE			: return "D3DTA_DIFFUSE";
	case D3DTA_SELECTMASK		: return "D3DTA_SELECTMASK";
	case D3DTA_SPECULAR			: return "D3DTA_SPECULAR";
	case D3DTA_TEMP				: return "D3DTA_TEMP";
	case D3DTA_TEXTURE			: return "D3DTA_TEXTURE";
	case D3DTA_TFACTOR			: return "D3DTA_TFACTOR";
	case D3DTA_ALPHAREPLICATE	: return "D3DTA_ALPHAREPLICATE";
	case D3DTA_COMPLEMENT		: return "D3DTA_COMPLEMENT";
	default					      : return "UNKNOWN";
	}
}

const char* DX8Wrapper::Get_DX8_Texture_Filter_Name(unsigned value)
{
	switch (value) {
	case D3DTEXF_NONE				: return "D3DTEXF_NONE";
	case D3DTEXF_POINT			: return "D3DTEXF_POINT";
	case D3DTEXF_LINEAR			: return "D3DTEXF_LINEAR";
	case D3DTEXF_ANISOTROPIC	: return "D3DTEXF_ANISOTROPIC";
	case D3DTEXF_PYRAMIDALQUAD	: return "D3DTEXF_PYRAMIDALQUAD";
	case D3DTEXF_GAUSSIANQUAD	: return "D3DTEXF_GAUSSIANQUAD";
	default					      : return "UNKNOWN";
	}
}

const char* DX8Wrapper::Get_DX8_Texture_Address_Name(unsigned value)
{
	switch (value) {
	case D3DTADDRESS_WRAP		: return "D3DTADDRESS_WRAP";
	case D3DTADDRESS_MIRROR		: return "D3DTADDRESS_MIRROR";
	case D3DTADDRESS_CLAMP		: return "D3DTADDRESS_CLAMP";
	case D3DTADDRESS_BORDER		: return "D3DTADDRESS_BORDER";
	case D3DTADDRESS_MIRRORONCE: return "D3DTADDRESS_MIRRORONCE";
	default					      : return "UNKNOWN";
	}
}

const char* DX8Wrapper::Get_DX8_Texture_Transform_Flag_Name(unsigned value)
{
	switch (value) {
	case D3DTTFF_DISABLE			: return "D3DTTFF_DISABLE";
	case D3DTTFF_COUNT1			: return "D3DTTFF_COUNT1";
	case D3DTTFF_COUNT2			: return "D3DTTFF_COUNT2";
	case D3DTTFF_COUNT3			: return "D3DTTFF_COUNT3";
	case D3DTTFF_COUNT4			: return "D3DTTFF_COUNT4";
	case D3DTTFF_PROJECTED		: return "D3DTTFF_PROJECTED";
	default					      : return "UNKNOWN";
	}
}

const char* DX8Wrapper::Get_DX8_ZBuffer_Type_Name(unsigned value)
{
	switch (value) {
	case D3DZB_FALSE				: return "D3DZB_FALSE";
	case D3DZB_TRUE				: return "D3DZB_TRUE";
	case D3DZB_USEW				: return "D3DZB_USEW";
	default					      : return "UNKNOWN";
	}
}

const char* DX8Wrapper::Get_DX8_Fill_Mode_Name(unsigned value)
{
	switch (value) {
	case D3DFILL_POINT			: return "D3DFILL_POINT";
	case D3DFILL_WIREFRAME		: return "D3DFILL_WIREFRAME";
	case D3DFILL_SOLID			: return "D3DFILL_SOLID";
	default					      : return "UNKNOWN";
	}
}

const char* DX8Wrapper::Get_DX8_Shade_Mode_Name(unsigned value)
{
	switch (value) {
	case D3DSHADE_FLAT			: return "D3DSHADE_FLAT";
	case D3DSHADE_GOURAUD		: return "D3DSHADE_GOURAUD";
	case D3DSHADE_PHONG			: return "D3DSHADE_PHONG";
	default							: return "UNKNOWN";
	}
}

const char* DX8Wrapper::Get_DX8_Blend_Name(unsigned value)
{
	switch (value) {
	case D3DBLEND_ZERO                : return "D3DBLEND_ZERO";
	case D3DBLEND_ONE                 : return "D3DBLEND_ONE";
	case D3DBLEND_SRCCOLOR            : return "D3DBLEND_SRCCOLOR";
	case D3DBLEND_INVSRCCOLOR         : return "D3DBLEND_INVSRCCOLOR";
	case D3DBLEND_SRCALPHA            : return "D3DBLEND_SRCALPHA";
	case D3DBLEND_INVSRCALPHA         : return "D3DBLEND_INVSRCALPHA";
	case D3DBLEND_DESTALPHA           : return "D3DBLEND_DESTALPHA";
	case D3DBLEND_INVDESTALPHA        : return "D3DBLEND_INVDESTALPHA";
	case D3DBLEND_DESTCOLOR           : return "D3DBLEND_DESTCOLOR";
	case D3DBLEND_INVDESTCOLOR        : return "D3DBLEND_INVDESTCOLOR";
	case D3DBLEND_SRCALPHASAT         : return "D3DBLEND_SRCALPHASAT";
	case D3DBLEND_BOTHSRCALPHA        : return "D3DBLEND_BOTHSRCALPHA";
	case D3DBLEND_BOTHINVSRCALPHA     : return "D3DBLEND_BOTHINVSRCALPHA";
	default									 : return "UNKNOWN";
	}
}

const char* DX8Wrapper::Get_DX8_Cull_Mode_Name(unsigned value)
{
	switch (value) {
	case D3DCULL_NONE				: return "D3DCULL_NONE";
	case D3DCULL_CW				: return "D3DCULL_CW";
	case D3DCULL_CCW				: return "D3DCULL_CCW";
	default							: return "UNKNOWN";
	}
}

const char* DX8Wrapper::Get_DX8_Cmp_Func_Name(unsigned value)
{
	switch (value) {
	case D3DCMP_NEVER          : return "D3DCMP_NEVER";
	case D3DCMP_LESS           : return "D3DCMP_LESS";
	case D3DCMP_EQUAL          : return "D3DCMP_EQUAL";
	case D3DCMP_LESSEQUAL      : return "D3DCMP_LESSEQUAL";
	case D3DCMP_GREATER        : return "D3DCMP_GREATER";
	case D3DCMP_NOTEQUAL       : return "D3DCMP_NOTEQUAL";
	case D3DCMP_GREATEREQUAL   : return "D3DCMP_GREATEREQUAL";
	case D3DCMP_ALWAYS         : return "D3DCMP_ALWAYS";
	default							: return "UNKNOWN";
	}
}

const char* DX8Wrapper::Get_DX8_Fog_Mode_Name(unsigned value)
{
	switch (value) {
	case D3DFOG_NONE				: return "D3DFOG_NONE";
	case D3DFOG_EXP				: return "D3DFOG_EXP";
	case D3DFOG_EXP2				: return "D3DFOG_EXP2";
	case D3DFOG_LINEAR			: return "D3DFOG_LINEAR";
	default							: return "UNKNOWN";
	}
}

const char* DX8Wrapper::Get_DX8_Stencil_Op_Name(unsigned value)
{
	switch (value) {
	case D3DSTENCILOP_KEEP		: return "D3DSTENCILOP_KEEP";
	case D3DSTENCILOP_ZERO		: return "D3DSTENCILOP_ZERO";
	case D3DSTENCILOP_REPLACE	: return "D3DSTENCILOP_REPLACE";
	case D3DSTENCILOP_INCRSAT	: return "D3DSTENCILOP_INCRSAT";
	case D3DSTENCILOP_DECRSAT	: return "D3DSTENCILOP_DECRSAT";
	case D3DSTENCILOP_INVERT	: return "D3DSTENCILOP_INVERT";
	case D3DSTENCILOP_INCR		: return "D3DSTENCILOP_INCR";
	case D3DSTENCILOP_DECR		: return "D3DSTENCILOP_DECR";
	default							: return "UNKNOWN";
	}
}

const char* DX8Wrapper::Get_DX8_Material_Source_Name(unsigned value)
{
	switch (value) {
	case D3DMCS_MATERIAL			: return "D3DMCS_MATERIAL";
	case D3DMCS_COLOR1			: return "D3DMCS_COLOR1";
	case D3DMCS_COLOR2			: return "D3DMCS_COLOR2";
	default							: return "UNKNOWN";
	}
}

const char* DX8Wrapper::Get_DX8_Vertex_Blend_Flag_Name(unsigned value)
{
	switch (value) {
	case D3DVBF_DISABLE			: return "D3DVBF_DISABLE";
	case D3DVBF_1WEIGHTS			: return "D3DVBF_1WEIGHTS";
	case D3DVBF_2WEIGHTS			: return "D3DVBF_2WEIGHTS";
	case D3DVBF_3WEIGHTS			: return "D3DVBF_3WEIGHTS";
	case D3DVBF_TWEENING			: return "D3DVBF_TWEENING";
	case D3DVBF_0WEIGHTS			: return "D3DVBF_0WEIGHTS";
	default							: return "UNKNOWN";
	}
}

const char* DX8Wrapper::Get_DX8_Patch_Edge_Style_Name(unsigned value)
{
	switch (value) {
	case D3DPATCHEDGE_DISCRETE	: return "D3DPATCHEDGE_DISCRETE";
   case D3DPATCHEDGE_CONTINUOUS:return "D3DPATCHEDGE_CONTINUOUS";
	default							: return "UNKNOWN";
	}
}

const char* DX8Wrapper::Get_DX8_Debug_Monitor_Token_Name(unsigned value)
{
	switch (value) {
	case D3DDMT_ENABLE			: return "D3DDMT_ENABLE";
	case D3DDMT_DISABLE			: return "D3DDMT_DISABLE";
	default							: return "UNKNOWN";
	}
}

const char* DX8Wrapper::Get_DX8_Blend_Op_Name(unsigned value)
{
	switch (value) {
	case D3DBLENDOP_ADD			: return "D3DBLENDOP_ADD";
	case D3DBLENDOP_SUBTRACT	: return "D3DBLENDOP_SUBTRACT";
	case D3DBLENDOP_REVSUBTRACT: return "D3DBLENDOP_REVSUBTRACT";
	case D3DBLENDOP_MIN			: return "D3DBLENDOP_MIN";
	case D3DBLENDOP_MAX			: return "D3DBLENDOP_MAX";
	default							: return "UNKNOWN";
	}
}


//============================================================================
// DX8Wrapper::getBackBufferFormat
//============================================================================

WW3DFormat	DX8Wrapper::getBackBufferFormat( void )
{
	return D3DFormat_To_WW3DFormat( _PresentParameters.BackBufferFormat );
}


/*******************************************************************************************
** Geometry a caller builds and draws itself, and the rest of what D1 needs to stop the engine
** reaching past this wrapper.  See dx8wrapper.h for why each of these is shaped the way it is.
**
** Nothing in the engine calls any of it yet.  The call sites move in their own pull requests,
** because the acceptance test for moving one is a pixel comparison and that needs a device.
*******************************************************************************************/

OwnedGeometryClass::OwnedGeometryClass()
	:
	Vertices(NULL),
	Indices(NULL),
	VertexTwin(NULL),
	IndexTwin(NULL),
	VertexByteCount(0),
	IndexCount(0),
	VerticesAreDynamic(false),
	IndicesAreDynamic(false)
{
}

OwnedGeometryClass::~OwnedGeometryClass()
{
	Release();
}

bool OwnedGeometryClass::Create_Vertices(unsigned byte_count, bool refilled_every_frame)
{
	WWASSERT(Vertices==NULL);
	if (byte_count==0) return false;

	IDirect3DDevice9 * device=DX8Wrapper::_Get_D3D_Device();
	if (device==NULL) return false;

	// Write-only because nothing reads these back, and that is also what lets the Direct3D 11
	// copy below be the only readable record of what was written.
	uint32 usage=D3DUSAGE_WRITEONLY;
	D3DPOOL pool=D3DPOOL_MANAGED;
	if (refilled_every_frame) {
		usage|=D3DUSAGE_DYNAMIC;
		pool=D3DPOOL_DEFAULT;		// a dynamic buffer cannot live in the managed pool
	}

	// The FVF argument is zero: these vertices are described by the format or the layout the
	// caller binds before it draws, not by the buffer.
	if (Render_Failed(device->CreateVertexBuffer(byte_count, usage, 0, pool, &Vertices, NULL))) {
		Vertices=NULL;
		return false;
	}

	VertexByteCount=byte_count;
	VerticesAreDynamic=refilled_every_frame;
	VertexTwin=Direct3D11_Twin_Vertex_Buffer(byte_count, refilled_every_frame);
	return true;
}

bool OwnedGeometryClass::Create_Indices(unsigned index_count, bool refilled_every_frame)
{
	WWASSERT(Indices==NULL);
	if (index_count==0) return false;

	IDirect3DDevice9 * device=DX8Wrapper::_Get_D3D_Device();
	if (device==NULL) return false;

	const unsigned byte_count=(unsigned)(index_count*sizeof(unsigned short));

	uint32 usage=D3DUSAGE_WRITEONLY;
	D3DPOOL pool=D3DPOOL_MANAGED;
	if (refilled_every_frame) {
		usage|=D3DUSAGE_DYNAMIC;
		pool=D3DPOOL_DEFAULT;
	}

	if (Render_Failed(device->CreateIndexBuffer(byte_count, usage, D3DFMT_INDEX16, pool, &Indices, NULL))) {
		Indices=NULL;
		return false;
	}

	IndexCount=index_count;
	IndicesAreDynamic=refilled_every_frame;
	IndexTwin=Direct3D11_Twin_Index_Buffer(byte_count, refilled_every_frame);
	return true;
}

void OwnedGeometryClass::Release()
{
	if (Vertices!=NULL) {
		Vertices->Release();
		Vertices=NULL;
	}
	if (Indices!=NULL) {
		Indices->Release();
		Indices=NULL;
	}
	delete VertexTwin;
	VertexTwin=NULL;
	delete IndexTwin;
	IndexTwin=NULL;
	VertexByteCount=0;
	IndexCount=0;
	VerticesAreDynamic=false;
	IndicesAreDynamic=false;
}

// ----------------------------------------------------------------------------

OwnedGeometryClass::VertexLockClass::VertexLockClass(OwnedGeometryClass & geometry,
	unsigned byte_offset, unsigned byte_count, bool discard)
	:
	Geometry(&geometry),
	Vertices(NULL)
{
	DX8_THREAD_ASSERT();
	if (geometry.Vertices==NULL) return;

	// D3DLOCK_DISCARD is only legal on a dynamic buffer; asking for it on a static one fails the
	// lock outright, which would lose the caller's vertices silently.
	const unsigned flags=(discard && geometry.VerticesAreDynamic) ? D3DLOCK_DISCARD : 0;

	void * memory=NULL;
	if (Render_Failed(geometry.Vertices->Lock(byte_offset, byte_count, &memory, flags)) || memory==NULL) {
		return;
	}

	// With a Direct3D 11 copy the write is redirected into the copy's block and written out to
	// both buffers on unlock; without one this answers null and the Direct3D 9 pointer stands.
	void * redirected=DX11Lock.Begin(geometry.VertexTwin, memory, byte_offset, byte_count, flags);
	Vertices=(redirected!=NULL) ? redirected : memory;
}

OwnedGeometryClass::VertexLockClass::~VertexLockClass()
{
	if (Vertices==NULL) return;

	// The copy has to be written while the Direct3D 9 memory is still mapped.
	DX11Lock.End();
	Geometry->Vertices->Unlock();
	Vertices=NULL;
}

OwnedGeometryClass::IndexLockClass::IndexLockClass(OwnedGeometryClass & geometry,
	unsigned first_index, unsigned index_count, bool discard)
	:
	Geometry(&geometry),
	Indices(NULL)
{
	DX8_THREAD_ASSERT();
	if (geometry.Indices==NULL) return;

	const unsigned flags=(discard && geometry.IndicesAreDynamic) ? D3DLOCK_DISCARD : 0;
	const unsigned byte_offset=(unsigned)(first_index*sizeof(unsigned short));
	const unsigned byte_count=(unsigned)(index_count*sizeof(unsigned short));

	void * memory=NULL;
	if (Render_Failed(geometry.Indices->Lock(byte_offset, byte_count, &memory, flags)) || memory==NULL) {
		return;
	}

	void * redirected=DX11Lock.Begin(geometry.IndexTwin, memory, byte_offset, byte_count, flags);
	Indices=(unsigned short *)((redirected!=NULL) ? redirected : memory);
}

OwnedGeometryClass::IndexLockClass::~IndexLockClass()
{
	if (Indices==NULL) return;

	DX11Lock.End();
	Geometry->Indices->Unlock();
	Indices=NULL;
}

// ----------------------------------------------------------------------------

AssembledPixelShaderClass::~AssembledPixelShaderClass()
{
	Release();
}

bool AssembledPixelShaderClass::Assemble(const char * source, const char * name)
{
	Release();

	if (source==NULL) return false;

	IDirect3DDevice9 * device=DX8Wrapper::_Get_D3D_Device();
	if (device==NULL || D3DXAssembleShader==NULL) return false;

	LPD3DXBUFFER compiled=NULL;
	if (Render_Failed(D3DXAssembleShader(source, (UINT)strlen(source), NULL, NULL, 0, &compiled, NULL))
		|| compiled==NULL) {
		return false;
	}

	const RenderResult made=device->CreatePixelShader((RenderUInt32 *)compiled->GetBufferPointer(), &Shader);
	compiled->Release();
	if (Render_Failed(made)) {
		Shader=NULL;
		return false;
	}

	// In the same call as the creation on purpose.  Registering is what lets the backend's reports
	// name the file a refused draw had bound, and a registration that is left out is not an error
	// anywhere - it turns into a draw refused under a cause that names nothing.
	Direct3D11_Register_Engine_Shader(Shader, (name!=NULL) ? name : "assembled pixel shader");
	return true;
}

void AssembledPixelShaderClass::Release()
{
	if (Shader!=NULL) {
		Shader->Release();
		Shader=NULL;
	}
}

// ----------------------------------------------------------------------------

void DX8Wrapper::Set_Owned_Geometry(const OwnedGeometryClass & geometry, unsigned vertex_stride,
	unsigned first_vertex_byte)
{
	DX8_THREAD_ASSERT();
	WWASSERT(geometry.Vertices!=NULL);

	DX8CALL(SetStreamSource(0, geometry.Vertices, first_vertex_byte, vertex_stride));
	Direct3D11_Mirror_Stream_Source(geometry.VertexTwin, vertex_stride, first_vertex_byte);

	if (geometry.Indices!=NULL) {
		DX8CALL(SetIndices(geometry.Indices));
		Direct3D11_Mirror_Indices(geometry.IndexTwin);
	}

	// What the device holds is no longer what this wrapper's cache says it holds.  Without this
	// the next mesh draw would find its own buffer "already bound" and read the caller's vertices
	// instead - which is the same shape of bug as a texture stage set behind the wrapper's back.
	render_state_changed|=VERTEX_BUFFER_CHANGED;
	render_state_changed|=INDEX_BUFFER_CHANGED;
}

void DX8Wrapper::Draw_Owned_Triangles(unsigned first_index, unsigned triangle_count,
	unsigned first_vertex, unsigned vertex_count, bool as_strip)
{
	DX8_THREAD_ASSERT();
	if (triangle_count==0) return;

	// A strip of n triangles is n+2 indices; a list is three each.  Direct3D 9 counts primitives
	// and the backend counts indices, so both forms are spelled out here rather than at the call
	// sites, which had them written out by hand and not always the same way.
	const unsigned index_count=as_strip ? (triangle_count+2) : (triangle_count*3);

	Flush_Deferred_D3D9_State();
	DX8CALL(DrawIndexedPrimitive(
		as_strip ? D3DPT_TRIANGLESTRIP : D3DPT_TRIANGLELIST,
		0,						// BaseVertexIndex: the stream offset already moved the vertices
		first_vertex,
		vertex_count,
		first_index,
		triangle_count));

	if (as_strip) {
		Direct3D11_Draw_Indexed_Strip(index_count, first_index, 0);
	}
	else {
		Direct3D11_Draw_Indexed_Triangles(index_count, first_index, 0);
	}

	DX8_RECORD_DRAW_CALLS();
}

void DX8Wrapper::Draw_Owned_Points(unsigned first_vertex, unsigned point_count)
{
	DX8_THREAD_ASSERT();
	if (point_count==0) return;

	Flush_Deferred_D3D9_State();
	DX8CALL(DrawPrimitive(D3DPT_POINTLIST, first_vertex, point_count));

	// No mirror: the Direct3D 11 backend resolves triangles out of the fixed-function state and
	// has no point path at all, and a point sized by D3DRS_POINTSCALE_* is not a triangle anybody
	// can generate from what it is told.  This draw is Direct3D 9 only until something gives the
	// backend points, and saying so here is better than mirroring it into a draw it refuses.
	DX8_RECORD_DRAW_CALLS();
}

void DX8Wrapper::Set_Engine_Vertex_Shader(const EngineVertexShaderClass & shader)
{
	DX8_THREAD_ASSERT();

	// The layout first, then the program: a program bound against the previous layout reads its
	// dcl_ inputs out of a stream described the wrong way, which is zeros rather than an error.
	if (shader.Layout!=NULL) {
		DX8CALL(SetVertexDeclaration(shader.Layout));
	}

	Set_Vertex_Shader(shader.Shader);
}

void DX8Wrapper::Set_Assembled_Pixel_Shader(const AssembledPixelShaderClass & shader)
{
	Set_Pixel_Shader(shader.Shader);
}

// ----------------------------------------------------------------------------

bool DX8Wrapper::Get_Render_Target_Description(unsigned & width, unsigned & height,
	WW3DFormat & format)
{
	width=0;
	height=0;
	format=WW3D_FORMAT_UNKNOWN;

	IDirect3DDevice9 * device=_Get_D3D_Device();
	if (device==NULL) return false;

	IDirect3DSurface9 * target=NULL;
	if (Render_Failed(device->GetRenderTarget(PRIMARY_RENDER_TARGET, &target)) || target==NULL) {
		return false;
	}

	D3DSURFACE_DESC description;
	const bool described=Render_Succeeded(target->GetDesc(&description));
	target->Release();
	if (!described) return false;

	width=description.Width;
	height=description.Height;
	format=D3DFormat_To_WW3DFormat(description.Format);
	return true;
}

SurfaceClass * DX8Wrapper::Read_Back_Render_Target()
{
	DX8_THREAD_ASSERT();

	IDirect3DDevice9 * device=_Get_D3D_Device();
	if (device==NULL) return NULL;

	IDirect3DSurface9 * target=NULL;
	if (Render_Failed(device->GetRenderTarget(PRIMARY_RENDER_TARGET, &target)) || target==NULL) {
		return NULL;
	}

	SurfaceClass * read_back=Copy_Surface_To_System_Memory(target);
	target->Release();
	return read_back;
}

SurfaceClass * DX8Wrapper::Read_Back_Frame()
{
	DX8_THREAD_ASSERT();

	IDirect3DDevice9 * device=_Get_D3D_Device();
	if (device==NULL) return NULL;

	IDirect3DSurface9 * back_buffer=NULL;
	if (Render_Failed(device->GetBackBuffer(PRIMARY_SWAP_CHAIN, 0, D3DBACKBUFFER_TYPE_MONO, &back_buffer))
		|| back_buffer==NULL) {
		return NULL;
	}

	D3DSURFACE_DESC description;
	if (Render_Failed(back_buffer->GetDesc(&description))) {
		back_buffer->Release();
		return NULL;
	}

	// GetRenderTargetData refuses a multisampled source, and with anti-aliasing on the back buffer
	// is exactly that.  StretchRect between two render targets of the same size is Direct3D 9's
	// resolve, so the samples are averaged into a plain target and the read comes off that.  Miss
	// this and a screenshot falls through to photographing the desktop.
	IDirect3DSurface9 * source=back_buffer;
	IDirect3DSurface9 * resolved=NULL;
	if (description.MultiSampleType!=D3DMULTISAMPLE_NONE
		&& Render_Succeeded(device->CreateRenderTarget(description.Width, description.Height,
				description.Format, D3DMULTISAMPLE_NONE, 0, false, &resolved, NULL))
		&& resolved!=NULL
		&& Render_Succeeded(device->StretchRect(back_buffer, NULL, resolved, NULL, D3DTEXF_NONE))) {
		source=resolved;
	}

	SurfaceClass * read_back=Copy_Surface_To_System_Memory(source);

	if (resolved!=NULL) resolved->Release();
	back_buffer->Release();
	return read_back;
}

SurfaceClass * DX8Wrapper::Copy_Surface_To_System_Memory(IDirect3DSurface9 * source)
{
	if (source==NULL) return NULL;

	IDirect3DDevice9 * device=_Get_D3D_Device();
	if (device==NULL) return NULL;

	D3DSURFACE_DESC description;
	if (Render_Failed(source->GetDesc(&description))) return NULL;
	if (description.MultiSampleType!=D3DMULTISAMPLE_NONE) return NULL;

	IDirect3DSurface9 * copy=NULL;
	if (Render_Failed(device->CreateOffscreenPlainSurface(description.Width, description.Height,
			description.Format, D3DPOOL_SYSTEMMEM, &copy, NULL)) || copy==NULL) {
		return NULL;
	}

	if (Render_Failed(device->GetRenderTargetData(source, copy))) {
		copy->Release();
		return NULL;
	}

	SurfaceClass * surface=NEW_REF(SurfaceClass,(copy));
	copy->Release();
	return surface;
}

bool DX8Wrapper::Device_Is_Ready()
{
	IDirect3DDevice9 * device=_Get_D3D_Device();
	if (device==NULL) return false;

	// Asked of the device, not of Is_Device_Lost: that flag is what the last present reported, and
	// the four callers ask this before building geometry, which can be a long way from a present.
	// Deliberately not a DX8CALL - a lost device is the answer here, not an error to log.
	return device->TestCooperativeLevel()==D3D_OK;
}
