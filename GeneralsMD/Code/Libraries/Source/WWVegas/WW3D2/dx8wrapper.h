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
 *                 Project Name : ww3d                                                         *
 *                                                                                             *
 *                     $Archive:: /Commando/Code/ww3d2/dx8wrapper.h                           $*
 *                                                                                             *
 *              Original Author:: Jani Penttinen                                               *
 *                                                                                             *
 *                       Author : Kenny Mitchell                                               * 
 *                                                                                             * 
 *                     $Modtime:: 08/05/02 2:40p                                              $*
 *                                                                                             *
 *                    $Revision:: 92                                                          $*
 *                                                                                             *
 * 06/26/02 KM Matrix name change to avoid MAX conflicts                                       *
 * 06/27/02 KM Render to shadow buffer texture support														*
 * 08/05/02 KM Texture class redesign 
 *---------------------------------------------------------------------------------------------*
 * Functions:                                                                                  *
 * - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

#if defined(_MSC_VER)
#pragma once
#endif

#ifndef DX8_WRAPPER_H
#define DX8_WRAPPER_H

#include "always.h"
#include "dllist.h"
#include <d3d9.h>
#include "Platform/RenderTypes.h"	// the Win32 types the device interface crosses with, engine-named
#include "d3dx9runtime.h"
#include "d3dx9math.h"
#include "matrix4.h"
#include "statistics.h"
#include "wwstring.h"
#include "lightenvironment.h"
#include "shader.h"
#include "vector4.h"
#include "cpudetect.h"
#include "dx8caps.h"
#include "dx11runtime.h"

#include "texture.h"
#include "dx8vertexbuffer.h"
#include "dx8indexbuffer.h"
#include "vertmaterial.h"
#include "Platform/MsvcFloatCasts.h"

/*
** Registry value names
*/
#define	VALUE_NAME_RENDER_DEVICE_NAME					"RenderDeviceName"
#define	VALUE_NAME_RENDER_DEVICE_WIDTH				"RenderDeviceWidth"
#define	VALUE_NAME_RENDER_DEVICE_HEIGHT				"RenderDeviceHeight"
#define	VALUE_NAME_RENDER_DEVICE_DEPTH				"RenderDeviceDepth"
#define	VALUE_NAME_RENDER_DEVICE_WINDOWED			"RenderDeviceWindowed"
#define	VALUE_NAME_RENDER_DEVICE_TEXTURE_DEPTH		"RenderDeviceTextureDepth"

const unsigned MAX_TEXTURE_STAGES=8;
// D3D9 moved the filter, address and anisotropy states off the texture stage and onto
// the sampler, so they are cached separately: the two enums renumber from 1 and would
// otherwise collide in one array.  D3DSAMP_DMAPOFFSET is 13, the last of them.
const unsigned MAX_SAMPLER_STATES=16;
// D3D8's D3DRS_ZBIAS clamped its level here, and the levels are what the engine's own
// callers still pass.
const int MAX_Z_BIAS_LEVEL=15;
// D3D9 added a swap chain index to GetDisplayMode, GetBackBuffer and GetFrontBufferData.
// This renderer creates one implicit swap chain and never asks about another.
const UINT PRIMARY_SWAP_CHAIN=0;
// D3D9 can bind several render targets at once and indexes them; this renderer binds one.
const uint32 PRIMARY_RENDER_TARGET=0;
// D3D8's GetAdapterIdentifier took D3DENUM_NO_WHQL_LEVEL to skip a slow WHQL signature
// check.  D3D9 removed the flag and never does that check unless asked, so zero is what
// the old call was actually asking for.
const uint32 NO_ADAPTER_IDENTIFIER_FLAGS=0;
const unsigned MAX_VERTEX_STREAMS=2;
const unsigned MAX_VERTEX_SHADER_CONSTANTS=96;
const unsigned MAX_PIXEL_SHADER_CONSTANTS=8;
const unsigned MAX_SHADOW_MAPS=1;

#define prevVer
#define nextVer
#define __volatile unsigned


enum {
	BUFFER_TYPE_DX8,
	BUFFER_TYPE_SORTING,
	BUFFER_TYPE_DYNAMIC_DX8,
	BUFFER_TYPE_DYNAMIC_SORTING,
	BUFFER_TYPE_INVALID
};

class VertexMaterialClass;
class CameraClass;
class LightEnvironmentClass;
class RenderDeviceDescClass;
class VertexBufferClass;
class DynamicVBAccessClass;
class IndexBufferClass;
class DynamicIBAccessClass;
class TextureClass;
class ZTextureClass;
class LightClass;
class SurfaceClass;
class DX8Caps;

#define DX8_RECORD_MATRIX_CHANGE()				matrix_changes++
#define DX8_RECORD_MATERIAL_CHANGE()			material_changes++
#define DX8_RECORD_VERTEX_BUFFER_CHANGE()		vertex_buffer_changes++
#define DX8_RECORD_INDEX_BUFFER_CHANGE()		index_buffer_changes++
#define DX8_RECORD_LIGHT_CHANGE()				light_changes++
#define DX8_RECORD_TEXTURE_CHANGE()				texture_changes++
#define DX8_RECORD_RENDER_STATE_CHANGE()		render_state_changes++
#define DX8_RECORD_TEXTURE_STAGE_STATE_CHANGE() texture_stage_state_changes++
#define DX8_RECORD_DRAW_CALLS()					draw_calls++

extern unsigned number_of_DX8_calls;
extern bool _DX8SingleThreaded;

void DX8_Assert();
void Log_DX8_ErrorCode(unsigned res);
void Non_Fatal_Log_DX8_ErrorCode(unsigned res,const char * file,int line);

WWINLINE void DX8_ErrorCode(unsigned res)
{
	if (res==D3D_OK) return;
	Log_DX8_ErrorCode(res);
}

#ifdef WWDEBUG
#define DX8CALL_HRES(x,res) DX8_Assert(); res = DX8Wrapper::_Get_D3D_Device()->x; DX8_ErrorCode(res); number_of_DX8_calls++;
#define DX8CALL(x) DX8_Assert(); DX8_ErrorCode(DX8Wrapper::_Get_D3D_Device()->x); number_of_DX8_calls++;
#define DX8CALL_D3D(x) DX8_Assert(); DX8_ErrorCode(DX8Wrapper::_Get_D3D()->x); number_of_DX8_calls++;
#define DX8_THREAD_ASSERT() if (_DX8SingleThreaded) { WWASSERT_PRINT(DX8Wrapper::_Get_Main_Thread_ID()==ThreadClass::_Get_Current_Thread_ID(),"DX8Wrapper::DX8 calls must be called from the main thread!"); }
#else
#define DX8CALL_HRES(x,res) res = DX8Wrapper::_Get_D3D_Device()->x; number_of_DX8_calls++;
#define DX8CALL(x) DX8Wrapper::_Get_D3D_Device()->x; number_of_DX8_calls++;
#define DX8CALL_D3D(x) DX8Wrapper::_Get_D3D()->x; number_of_DX8_calls++;
#define DX8_THREAD_ASSERT() ;
#endif


#define no_EXTENDED_STATS
// EXTENDED_STATS collects additional timing statistics by turning off parts
// of the 3D drawing system (terrain, objects, etc.)
#ifdef EXTENDED_STATS
class DX8_Stats
{
public:
	bool m_showingStats;
	bool m_disableTerrain;
	bool m_disableWater;
	bool m_disableObjects;
	bool m_disableOverhead;
	bool m_disableConsole;
	int  m_debugLinesToShow;
	int	 m_sleepTime;
public:
	DX8_Stats::DX8_Stats(void) {
		m_disableConsole = m_showingStats = m_disableTerrain = m_disableWater = m_disableOverhead = m_disableObjects = false;
		m_sleepTime = 0;
		m_debugLinesToShow = -1; // -1 means show all expected lines of output
	}
};
#endif


// This virtual interface was added for the Generals RTS.
// It is called before resetting the dx8 device to ensure
// that all dx8 resources are released.  Otherwise reset fails. jba.
class DX8_CleanupHook
{
public:
	virtual void ReleaseResources(void)=0;
	virtual void ReAcquireResources(void)=0;
};


struct RenderStateStruct
{
	ShaderClass shader;
	VertexMaterialClass* material;
	TextureBaseClass * Textures[MAX_TEXTURE_STAGES];
	D3DLIGHT9 Lights[4];
	bool LightEnable[4];
  //unsigned lightsHash;
	Matrix4x4 world;
	Matrix4x4 view;
	unsigned vertex_buffer_types[MAX_VERTEX_STREAMS];
	unsigned index_buffer_type;
	unsigned short vba_offset;
	unsigned short vba_count;
	unsigned short iba_offset;
	VertexBufferClass* vertex_buffers[MAX_VERTEX_STREAMS];
	IndexBufferClass* index_buffer;
	unsigned short index_base_offset;

	RenderStateStruct();
	~RenderStateStruct();

	RenderStateStruct& operator= (const RenderStateStruct& src);
};

/*
** Geometry a caller builds and draws itself.
**
** Five places in the engine keep vertices of their own outside the mesh renderer's buffers - the
** sea patch and the wave grid, the shadow volumes, the shadow decals, the snow - and every one of
** them was making a Direct3D 9 buffer, binding it on the device and drawing from it by hand.  That
** is forty of the call sites D1's survey counts, and it is one problem rather than six: what each
** of them is asking for is somewhere to put vertices it will draw itself.
**
** One of these is that somewhere.  It holds the Direct3D 9 buffers and, on a -dx11 run, the
** Direct3D 11 copies that have to be filled with the same bytes, which is what lets the draw go
** through the backend rather than past it.  A lock hands out the copy's block when there is one,
** exactly as DX8VertexBufferClass's does, so a caller writes its vertices once and both buffers
** end up holding them.
**
** It is deliberately not a VertexBufferClass.  Those are reference counted, registered
** engine-wide, capped at 65535 vertices and carry an FVF, and none of the five callers wants any
** of that - the water grid alone is larger than the cap, and two of them describe their vertices
** with a declaration rather than an FVF.
**
** Nothing names Direct3D in the interface, which is the point: D2 replaces what is behind Create
** and the locks and no caller changes.
*/
class OwnedGeometryClass
{
public:
	OwnedGeometryClass();
	~OwnedGeometryClass();

	// refilled_every_frame is the caller's own dynamic/static choice and decides both the
	// Direct3D 9 pool and how the Direct3D 11 copy is written.  False from either means the
	// device refused the buffer and the caller draws nothing, which is what the hand-written
	// creations did.
	bool Create_Vertices(unsigned byte_count, bool refilled_every_frame);

	// Index buffers in this engine are 16 bit without exception, so there is no format here and
	// the backend's own mirror does not take one either.
	bool Create_Indices(unsigned index_count, bool refilled_every_frame);

	void Release();

	bool Has_Vertices() const { return VertexByteCount != 0; }
	bool Has_Indices() const { return IndexCount != 0; }
	unsigned Get_Vertex_Byte_Count() const { return VertexByteCount; }
	unsigned Get_Index_Count() const { return IndexCount; }

	/*
	** Scoped write access.  Construct one, write into what Get_Vertices/Get_Indices answers, let
	** it go out of scope.  A null pointer back means the buffer could not be locked and the
	** caller writes nothing; it must still let the lock go out of scope.
	**
	** discard says the rest of the buffer is not worth keeping, which is D3DLOCK_DISCARD and is
	** what lets a driver rename a dynamic buffer instead of waiting for the draws still reading
	** it.  Every caller that refills every frame wants it.
	*/
	class VertexLockClass
	{
	public:
		VertexLockClass(OwnedGeometryClass & geometry, unsigned byte_offset, unsigned byte_count,
			bool discard);
		~VertexLockClass();
		void * Get_Vertices() const { return Vertices; }
	private:
		VertexLockClass(const VertexLockClass &);
		VertexLockClass & operator = (const VertexLockClass &);
		OwnedGeometryClass * Geometry;
		void * Vertices;
		DX11BufferLockClass DX11Lock;
	};

	class IndexLockClass
	{
	public:
		IndexLockClass(OwnedGeometryClass & geometry, unsigned first_index, unsigned index_count,
			bool discard);
		~IndexLockClass();
		unsigned short * Get_Indices() const { return Indices; }
	private:
		IndexLockClass(const IndexLockClass &);
		IndexLockClass & operator = (const IndexLockClass &);
		OwnedGeometryClass * Geometry;
		unsigned short * Indices;
		DX11BufferLockClass DX11Lock;
	};

private:
	friend class DX8Wrapper;
	friend VertexLockClass;			// the nested names, as dx8vertexbuffer.h befriends its own
	friend IndexLockClass;

	OwnedGeometryClass(const OwnedGeometryClass &);
	OwnedGeometryClass & operator = (const OwnedGeometryClass &);

	IDirect3DVertexBuffer9 *	Vertices;
	IDirect3DIndexBuffer9 *		Indices;
	DX11BufferTwinClass *		VertexTwin;			// null on a run without -dx11
	DX11BufferTwinClass *		IndexTwin;
	unsigned					VertexByteCount;
	unsigned					IndexCount;
	bool						VerticesAreDynamic;
	bool						IndicesAreDynamic;
};


/*
** A vertex program the engine loaded out of the archives, with the vertex layout it came with.
**
** Direct3D 8 carried the layout inside the shader; Direct3D 9 split them, and since that split the
** two halves have been bound one after the other at each of the two call sites that use them - the
** wave grid and the trees.  Holding them together is not tidiness: a backend that has one form of
** layout, or no separate layout at all, needs to be told about both at once, and a caller that
** binds a shader without its layout reads zeros out of the stream.
**
** Take is handed what W3DShaderManager::LoadAndCreateD3DVertexShader produced.  It is the one
** place in the new interface that still names Direct3D 9 types, because the loader that makes
** them has not moved yet; D3 moves it, and this signature is what it changes.
*/
class EngineVertexShaderClass
{
public:
	EngineVertexShaderClass() : Shader(NULL), Layout(NULL) {}

	void Take(IDirect3DVertexShader9 * shader, IDirect3DVertexDeclaration9 * layout)
		{ Shader = shader; Layout = layout; }
	bool Is_Loaded() const { return Shader != NULL; }

private:
	friend class DX8Wrapper;
	IDirect3DVertexShader9 *		Shader;
	IDirect3DVertexDeclaration9 *	Layout;		// null when the shader is described by an FVF
};


/*
** A pixel program the engine carries as assembly text and builds at startup.
**
** Four of these live in the water: the river, the environment-mapped sea, the trapezoid and the
** reflection.  Each was assembled, handed to the device and then registered with the Direct3D 11
** backend by hand, and a registration that is left out is not an error anywhere - the draw is
** simply refused later, under a cause that names no file.  Building and registering in one call is
** what stops that being possible.
*/
class AssembledPixelShaderClass
{
public:
	AssembledPixelShaderClass() : Shader(NULL) {}
	~AssembledPixelShaderClass();

	// name is what the backend's reports call this program when a draw with it bound is refused.
	// False means the text did not assemble or the device refused it; the caller draws without it,
	// which is what the hand-written versions did with a failed HRESULT.
	bool Assemble(const char * source, const char * name);
	void Release();
	bool Is_Built() const { return Shader != NULL; }

private:
	friend class DX8Wrapper;
	AssembledPixelShaderClass(const AssembledPixelShaderClass &);
	AssembledPixelShaderClass & operator = (const AssembledPixelShaderClass &);
	IDirect3DPixelShader9 * Shader;
};

/**
** DX8Wrapper
**
** DX8 interface wrapper class.  This encapsulates the DX8 interface; adding redundant state
** detection, stat tracking, etc etc.  In general, we will wrap all DX8 calls with at least
** an WWINLINE function so that we can add stat tracking, etc if needed.  Direct access to the
** D3D device will require "friend" status and should be granted only in extreme circumstances :-)
*/
class DX8Wrapper
{
	enum ChangedStates {
		WORLD_CHANGED	=	1<<0,
		VIEW_CHANGED	=	1<<1,
		LIGHT0_CHANGED	=	1<<2,
		LIGHT1_CHANGED	=	1<<3,
		LIGHT2_CHANGED	=	1<<4,
		LIGHT3_CHANGED	=	1<<5,
		TEXTURE0_CHANGED=	1<<6,
		TEXTURE1_CHANGED=	1<<7,
		TEXTURE2_CHANGED=	1<<8,
		TEXTURE3_CHANGED=	1<<9,
		MATERIAL_CHANGED=	1<<14,
		SHADER_CHANGED	=	1<<15,
		VERTEX_BUFFER_CHANGED = 1<<16,
		INDEX_BUFFER_CHANGED = 1 << 17,
		WORLD_IDENTITY=	1<<18,
		VIEW_IDENTITY=		1<<19,

		TEXTURES_CHANGED=
			TEXTURE0_CHANGED|TEXTURE1_CHANGED|TEXTURE2_CHANGED|TEXTURE3_CHANGED,
		LIGHTS_CHANGED=
			LIGHT0_CHANGED|LIGHT1_CHANGED|LIGHT2_CHANGED|LIGHT3_CHANGED,
	};

	static void Draw_Sorting_IB_VB(
		unsigned primitive_type,
		unsigned short start_index,
		unsigned short polygon_count,
		unsigned short min_vertex_index,
		unsigned short vertex_count);

	static void Draw(
		unsigned primitive_type,
		unsigned short start_index,
		unsigned short polygon_count,
		unsigned short min_vertex_index=0,
		unsigned short vertex_count=0);

public:
#ifdef EXTENDED_STATS
	static DX8_Stats stats;
#endif

	static bool Init(void * hwnd, bool lite = false);
	static void Shutdown(void);

	static void SetCleanupHook(DX8_CleanupHook *pCleanupHook) {m_pCleanupHook = pCleanupHook;};
	/*
	** Some WW3D sub-systems need to be initialized after the device is created and shutdown
	** before the device is released.
	*/
	static void	Do_Onetime_Device_Dependent_Inits(void);
	// The half of the above that never touches D3D: preset materials, the mesh renderer's
	// containers, the texture loader's thread.  A run with no render device still needs these,
	// because the rest of WW3D2 assumes they exist the moment anything is built.
	static void	Do_Onetime_Device_Independent_Inits(void);
	static void Do_Onetime_Device_Dependent_Shutdowns(void);

	static bool Is_Device_Lost() { return IsDeviceLost; }
	static bool Is_Initted(void) { return IsInitted; }

	static bool Has_Stencil (void);
	static void Get_Format_Name(unsigned int format, StringClass *tex_format);

	/*
	** Rendering
	*/
	static void Begin_Scene(void);
	static void End_Scene(bool flip_frame = true);

	// Flip until the primary buffer is visible.
	static void Flip_To_Primary(void);

	static void Clear(bool clear_color, bool clear_z_stencil, const Vector3 &color, float dest_alpha=0.0f, float z=1.0f, unsigned int stencil=0);

	static void	Set_Viewport(const D3DVIEWPORT9* pViewport);

	static void Set_Vertex_Buffer(const VertexBufferClass* vb, unsigned stream=0);
	static void Set_Vertex_Buffer(const DynamicVBAccessClass& vba);
	/// A range of a sorting vertex array reserved earlier, bound the way a dynamic sorting access binds it
	static void Set_Sorting_Vertex_Range(VertexBufferClass* vb, unsigned short offset, unsigned short count);
	static void Set_Index_Buffer(const IndexBufferClass* ib,unsigned short index_base_offset);
	static void Set_Index_Buffer(const DynamicIBAccessClass& iba,unsigned short index_base_offset);
	static void Set_Index_Buffer_Index_Offset(unsigned offset);

	static void Get_Render_State(RenderStateStruct& state);
	static void Set_Render_State(const RenderStateStruct& state);
	static void Release_Render_State();

	static void Set_DX8_Material(const D3DMATERIAL9* mat);

	static void Set_Gamma(float gamma,float bright,float contrast,bool calibrate=true,bool uselimit=true);

	// Set_ and Get_Transform() functions take the matrix in Westwood convention format.

	static void Set_DX8_ZBias(int zbias);
	static void Set_Projection_Transform_With_Z_Bias(const Matrix4x4& matrix,float znear, float zfar);	// pointer to 16 matrices

	static void Set_Transform(D3DTRANSFORMSTATETYPE transform,const Matrix4x4& m);
	static void Set_Transform(D3DTRANSFORMSTATETYPE transform,const Matrix3D& m);
	static void Get_Transform(D3DTRANSFORMSTATETYPE transform, Matrix4x4& m);
	static void	Set_World_Identity();
	static void Set_View_Identity();
	static bool	Is_World_Identity();
	static bool Is_View_Identity();

	// Note that *_DX8_Transform() functions take the matrix in DX8 format - transposed from Westwood convention.

	static void _Set_DX8_Transform(D3DTRANSFORMSTATETYPE transform,const Matrix4x4& m);
	static void _Set_DX8_Transform(D3DTRANSFORMSTATETYPE transform,const Matrix3D& m);
	static void _Get_DX8_Transform(D3DTRANSFORMSTATETYPE transform, Matrix4x4& m);

	static void Set_DX8_Light(int index,D3DLIGHT9* light);
	static void Set_DX8_Render_State(D3DRENDERSTATETYPE state, unsigned value);
	static void Set_DX8_Clip_Plane(uint32 Index, const float* pPlane);
	static void Set_DX8_Texture_Stage_State(unsigned stage, D3DTEXTURESTAGESTATETYPE state, unsigned value);
	static void Set_DX8_Texture_Stage_State(unsigned stage, D3DSAMPLERSTATETYPE state, unsigned value);
	static void Set_DX8_Texture(unsigned int stage, IDirect3DBaseTexture9* texture);
	// What the wrapper last set, which a deferred Direct3D 9 device (Defer_D3D9_State) may not hold yet.
	// False while Invalidate_Cached_Render_States has left the state unknown.
	static bool Get_DX8_Texture_Stage_State(unsigned stage, D3DTEXTURESTAGESTATETYPE state, unsigned & value)
		{ value = TextureStageStates[stage][state]; return value != 0x12345678; }
	static IDirect3DBaseTexture9* Get_DX8_Texture(unsigned stage) { return Textures[stage]; }
	static void Set_Light_Environment(LightEnvironmentClass* light_env);
	static LightEnvironmentClass* Get_Light_Environment() { return Light_Environment; }
	static void Set_Fog(bool enable, const Vector3 &color, float start, float end);

	static WWINLINE const D3DLIGHT9& Peek_Light(unsigned index);
	static WWINLINE bool Is_Light_Enabled(unsigned index);

	static bool Validate_Device(void);

	// While Direct3D 11 presents, the Direct3D 9 device is not told the render states, stage and
	// sampler states, textures, material or lights as they are set, only marked as owing them.
	// Anything that draws on that device or reads its state back calls this first.
	static void Flush_Deferred_D3D9_State(void);

	// Deferred

	static void Set_Shader(const ShaderClass& shader);
	static void Get_Shader(ShaderClass& shader);
	static void Set_Texture(unsigned stage,TextureBaseClass* texture);
	static void Set_Material(const VertexMaterialClass* material);
	static void Set_Light(unsigned index,const D3DLIGHT9* light);
	static void Set_Light(unsigned index,const LightClass &light);

	static void Apply_Render_State_Changes();	// Apply deferred render state changes (will be called automatically by Draw...)

	static void Draw_Triangles(
		unsigned buffer_type,
		unsigned short start_index,
		unsigned short polygon_count,
		unsigned short min_vertex_index,
		unsigned short vertex_count);
	static void Draw_Triangles(
		unsigned short start_index,
		unsigned short polygon_count,
		unsigned short min_vertex_index,
		unsigned short vertex_count);
	static void Draw_Strip(
		unsigned short start_index,
		unsigned short index_count,
		unsigned short min_vertex_index,
		unsigned short vertex_count);

	/*
	** Geometry the caller built, and what it takes to draw it.  See OwnedGeometryClass above.
	**
	** These are additive: D1's survey found the engine drawing its own geometry straight on the
	** device from forty places, where a second backend could see neither the buffers, nor the
	** binding, nor the draw.  Nothing calls them yet - the call sites move in their own pull
	** requests, because the acceptance test for moving one is a pixel comparison.
	*/

	// Bind a caller's geometry as the stream and index source.  vertex_stride is the size of one
	// of its vertices; first_vertex_byte is where in the buffer the vertices start, which is how
	// the snow draws successive batches out of one buffer.
	static void Set_Owned_Geometry(const OwnedGeometryClass & geometry, unsigned vertex_stride,
		unsigned first_vertex_byte = 0);

	// Draw a run of the bound geometry's indices.  as_strip is the topology both water grids are
	// indexed for; everything else is a list.  first_vertex and vertex_count are the range of the
	// vertex buffer the indices reach into, which Direct3D 9 wants and a backend may not.
	static void Draw_Owned_Triangles(unsigned first_index, unsigned triangle_count,
		unsigned first_vertex, unsigned vertex_count, bool as_strip = false);

	// Vertices with no indices behind them: the snow's particles, which are drawn as points and
	// sized by the point-scale render states.  The Direct3D 11 backend has no point path, so this
	// one draws on Direct3D 9 alone and says so rather than pretending to mirror.
	static void Draw_Owned_Points(unsigned first_vertex, unsigned point_count);

	// A shipped vertex program and the layout that came with it, bound together.  See
	// EngineVertexShaderClass above for why the two halves are not separable.
	static void Set_Engine_Vertex_Shader(const EngineVertexShaderClass & shader);

	// A pixel program the engine assembled at startup.  Set_Pixel_Shader's overload; it exists so
	// that a caller holding one of these does not have to reach inside it.
	static void Set_Assembled_Pixel_Shader(const AssembledPixelShaderClass & shader);

	/*
	** The target the frame is being drawn into, and reading it back.
	*/

	// How big the colour target being drawn into is, and in what format.  False when there is no
	// device or no target, and the caller makes nothing.
	//
	// There is deliberately no Save/Restore pair here.  Writing one showed the wrapper already
	// has it: Set_Render_Target(surface, depth) keeps the surfaces it displaced and
	// Set_Render_Target(NULL, NULL) puts them back, so the five call sites that save and restore
	// by hand are a swap onto what exists rather than anything new.  A second stack of one on top
	// of that one would fight it.
	static bool Get_Render_Target_Description(unsigned & width, unsigned & height,
		WW3DFormat & format);

	// The colour target currently bound, copied into memory the CPU can read.  Null when there is
	// nothing to read, which is what a device that has gone away gives.  The caller releases what
	// comes back.
	static SurfaceClass * Read_Back_Render_Target();

	// The frame the device is about to present, likewise.  Multisampling is resolved first -
	// Direct3D 9 refuses to read a multisampled surface back, and with anti-aliasing on the back
	// buffer is exactly that - so this is not the same call as the one above with a different
	// surface.  Has to be called before the present that discards the frame.
	static SurfaceClass * Read_Back_Frame();

	// Whether the device can be drawn to at this moment, asked of the device rather than
	// remembered.  Four places in the engine ask it before touching anything that would build
	// geometry, and all four ask it the same way; Is_Device_Lost above answers from a flag the
	// present path last set, which is a different question.
	static bool Device_Is_Ready();

	/*
	** Resources
	*/

	static IDirect3DVolumeTexture9* _Create_DX8_Volume_Texture
	(
		unsigned int width,
		unsigned int height,
		unsigned int depth,
		WW3DFormat format,
		MipCountType mip_level_count,
		D3DPOOL pool=D3DPOOL_MANAGED
	);

	static IDirect3DCubeTexture9* _Create_DX8_Cube_Texture
	(
		unsigned int width,
		unsigned int height,
		WW3DFormat format,
		MipCountType mip_level_count,
		D3DPOOL pool=D3DPOOL_MANAGED,
		bool rendertarget=false
	);


	static IDirect3DTexture9* _Create_DX8_ZTexture
	(
		unsigned int width,
		unsigned int height,
		WW3DZFormat zformat,
		MipCountType mip_level_count,
		D3DPOOL pool=D3DPOOL_MANAGED
	);


	static IDirect3DTexture9 * _Create_DX8_Texture
	(
		unsigned int width,
		unsigned int height,
		WW3DFormat format,
		MipCountType mip_level_count,
		D3DPOOL pool=D3DPOOL_MANAGED,
		bool rendertarget=false
	);
	static IDirect3DTexture9 * _Create_DX8_Texture(const char *filename, MipCountType mip_level_count);
	static IDirect3DTexture9 * _Create_DX8_Texture(IDirect3DSurface9 *surface, MipCountType mip_level_count);

	static IDirect3DSurface9 * _Create_DX8_Surface(unsigned int width, unsigned int height, WW3DFormat format);
	static IDirect3DSurface9 * _Create_DX8_Surface(const char *filename);
	static IDirect3DSurface9 * _Get_DX8_Front_Buffer();
	static SurfaceClass * _Get_DX8_Back_Buffer(unsigned int num=0);

	// One depth-buffer level of bias, negative, as D3DRS_DEPTHBIAS wants it.  Set from
	// the depth stencil format when the device comes up; see Set_DX8_ZBias.
	static float Get_Depth_Buffer_Epsilon() { return DepthBufferEpsilon; }
	static float _Depth_Buffer_Epsilon(D3DFORMAT depth_stencil_format);

	// D3D8's SetRenderTarget bound the colour and depth surfaces in one call; D3D9 has
	// two, and the depth one is not indexed.  A null depth surface means no depth at all,
	// which is what the D3D8 call meant by it too.
	static void _Set_DX8_Render_Target(IDirect3DSurface9 * render_target, IDirect3DSurface9 * depth_buffer);

	// Every screen-space quad in the game is a four vertex triangle strip drawn straight off the
	// stack, and there are thirteen of them: the filter that puts the rendered scene back on the
	// screen, the smudges, the shadow volume's darkening pass.  They went to the device directly
	// and so were invisible to any second backend; this is the one place they pass through.
	//
	// While Direct3D 11 presents, Direct3D 9 draws one only for a caller that reads the Direct3D 9
	// target back afterwards, which is what d3d9_reads_back says: the smudge hardware test.  Every
	// other quad would cost a full flush of the deferred state and a draw nobody sees.
	static void _Draw_DX8_Primitive_UP(D3DPRIMITIVETYPE type, unsigned primitive_count,
		const void * vertices, unsigned stride, bool d3d9_reads_back = false);

	static void _Copy_DX8_Rects(
			IDirect3DSurface9* pSourceSurface,
			const RenderRect* pSourceRectsArray,
			UINT cRects,
			IDirect3DSurface9* pDestinationSurface,
			const RenderPoint* pDestPointsArray
	);

	static void _Update_Texture(TextureClass *system, TextureClass *video);
	static void Flush_DX8_Resource_Manager(unsigned int bytes=0);
	static unsigned int Get_Free_Texture_RAM();

	static unsigned _Get_Main_Thread_ID() { return _MainThreadID; }
	static const D3DADAPTER_IDENTIFIER9& Get_Current_Adapter_Identifier() { return CurrentAdapterIdentifier; }

	/*
	** Statistics
	*/
	static void Begin_Statistics();
	static void End_Statistics();
	static unsigned Get_Last_Frame_Matrix_Changes();
	static unsigned Get_Last_Frame_Material_Changes();
	static unsigned Get_Last_Frame_Vertex_Buffer_Changes();
	static unsigned Get_Last_Frame_Index_Buffer_Changes();
	static unsigned Get_Last_Frame_Light_Changes();
	static unsigned Get_Last_Frame_Texture_Changes();
	static unsigned Get_Last_Frame_Render_State_Changes();
	static unsigned Get_Last_Frame_Texture_Stage_State_Changes();
	static unsigned Get_Last_Frame_DX8_Calls();
	static unsigned Get_Last_Frame_Draw_Calls();
	static unsigned Get_Draw_Calls() { return draw_calls; }	///< so far this frame, to count what one pass costs

	static unsigned long Get_FrameCount(void);

	// Needed by shader class
	static bool						Get_Fog_Enable() { return FogEnable; }
	static D3DCOLOR				Get_Fog_Color() { return FogColor; }

	// Utilities
	static Vector4 Convert_Color(unsigned color);
	static unsigned int Convert_Color(const Vector4& color);
	static unsigned int Convert_Color(const Vector3& color, const float alpha);
	static void Clamp_Color(Vector4& color);
	static unsigned int Convert_Color_Clamp(const Vector4& color);

	static void			  Set_Alpha (const float alpha, unsigned int &color);

	static void _Enable_Triangle_Draw(bool enable) { _EnableTriangleDraw=enable; }
	static bool _Is_Triangle_Draw_Enabled() { return _EnableTriangleDraw; }

	/*
	** Additional swap chain interface
	**
	**		Use this interface to render to multiple windows (in windowed mode).
	**	To render to an additional window, the sequence of calls should look
	**	something like this:
	**
	**	DX8Wrapper::Set_Render_Target (swap_chain_ptr);
	**
	**	WW3D::Begin_Render (true, true, Vector3 (0, 0, 0));
	**	WW3D::Render (scene, camera, FALSE, FALSE);
	**	WW3D::End_Render ();
	**
	**	swap_chain_ptr->Present (NULL, NULL, NULL, NULL);
	**
	**	DX8Wrapper::Set_Render_Target ((IDirect3DSurface9 *)NULL);
	**
	*/
	static IDirect3DSwapChain9 *	Create_Additional_Swap_Chain (RenderWindow render_window);

	/*
	** Render target interface. If render target format is WW3D_FORMAT_UNKNOWN, current display format is used.
	*/
	static TextureClass *	Create_Render_Target (int width, int height, WW3DFormat format = WW3D_FORMAT_UNKNOWN);

	static void					Set_Render_Target (IDirect3DSurface9 *render_target, bool use_default_depth_buffer = false);
	static void					Set_Render_Target (IDirect3DSurface9* render_target, IDirect3DSurface9* dpeth_buffer);

	static void					Set_Render_Target (IDirect3DSwapChain9 *swap_chain);
	// Samples per pixel to ask the next device for; 0, or 2..16.  Set this before Set_Render_Device
	// or the device is created without multisampling.
	static void					Set_Requested_MultiSample_Level (unsigned samples);
	// Wait for the monitor on the next device create or reset.  WW3D2 cannot see GlobalData, so
	// the app layer pushes this in the same way it pushes the sample count.
	static void					Set_Requested_VSync (bool enabled);
	static bool					Get_Requested_VSync (void);
	// Samples per pixel actually in use, 0 if not multisampling.
	static unsigned				Get_MultiSample_Level (void);
	// True when the swap chain is FlipEx (9Ex).  MSAA then lives on a scene target.
	static bool					Is_Flip_Present (void);
	// Depth/stencil to use with a non-multisampled render target; NULL if not multisampling.
	static IDirect3DSurface9 *	_Get_Non_MultiSampled_Depth_Buffer (void);
	static bool					Is_Render_To_Texture(void) { return IsRenderToTexture; }

	// for depth map support KJM V
	static void Create_Render_Target
	(
		int width, 
		int height, 
		WW3DFormat format,
		WW3DZFormat zformat,
		TextureClass** target,
		ZTextureClass** depth_buffer
	);
	static void					Set_Render_Target_With_Z (TextureClass * texture, ZTextureClass* ztexture=NULL);

	static void Set_Shadow_Map(int idx, ZTextureClass* ztex) { Shadow_Map[idx]=ztex; }
	static ZTextureClass* Get_Shadow_Map(int idx) { return Shadow_Map[idx]; }
	// for depth map support KJM ^

	// shader system udpates KJM v
	static void Apply_Default_State();

	// D3D8's SetVertexShader took a DWORD that was either an FVF code or a shader
	// handle, and the engine used it both ways.  D3D9 splits them, so these do too.
	static void Set_Vertex_Format(uint32 fvf);
	static void Set_Vertex_Shader(IDirect3DVertexShader9 * vertex_shader);
	static void Set_Pixel_Shader(IDirect3DPixelShader9 * pixel_shader);

	static void Set_Vertex_Shader_Constant(int reg, const void* data, int count);
	static void Set_Pixel_Shader_Constant(int reg, const void* data, int count);

	static uint32 Get_Vertex_Processing_Behavior() { return Vertex_Processing_Behavior; }

	// Needed by scene lighting class
	static void						Set_Ambient(const Vector3& color);
	static const Vector3&		Get_Ambient() { return Ambient_Color; }
	// shader system updates KJM ^




	static IDirect3DDevice9* _Get_D3D_Device() { return D3DDevice; }
	static IDirect3D9* _Get_D3D() { return D3DInterface; }
	/// Returns the display format - added by TR for video playback - not part of W3D
	static WW3DFormat	getBackBufferFormat( void );
	static bool Reset_Device(bool reload_assets=true);

	// The Direct3D 9 device is always windowed, and a fullscreen game is a topmost window over its
	// monitor, placed here.  The monitor keeps its own mode: a picture smaller than it is scaled up
	// onto all of it, or onto the largest part of its own shape with black either side.  shown false
	// gives the desktop its gamma back and minimises the window, which is what leaving the game does;
	// true puts the game's back.  A windowed game only gets the desktop's gamma restored.
	static void Apply_Fullscreen_Display(bool shown);
	// Stretch a fullscreen picture whose shape is not the monitor's (false), or keep its shape with
	// black bars (true).  Pushed in from the app layer like the monitor below.
	static void Set_Requested_Fullscreen_Keep_Aspect(bool keep);
	// The monitor that display mode and window go on, as its GDI device name.  WW3D2 cannot see
	// GlobalData, so the app layer pushes it in before the device is made or reset, like the vsync.
	static void Set_Requested_Monitor(const char * device);

	static const DX8Caps*	Get_Current_Caps() { WWASSERT(CurrentCaps); return CurrentCaps; }
	// Caps exist only while a device does: initted with no device yet, or after one was refused or
	// released, Get_Current_Caps is NULL.
	static bool				Has_Current_Caps() { return CurrentCaps != NULL; }
	// Every refusal met on the way to a device since the start, "; " between them; empty when none.
	static const char *	Get_Device_Failures(void);

#if defined(_WIN32)	// the registry: Windows only
	static bool Registry_Save_Render_Device( const char * sub_key );
	static bool Registry_Load_Render_Device( const char * sub_key, bool resize_window );
#endif

	static const char* Get_DX8_Render_State_Name(D3DRENDERSTATETYPE state);
	static const char* Get_DX8_Texture_Stage_State_Name(D3DTEXTURESTAGESTATETYPE state);
	static const char* Get_DX8_Sampler_State_Name(D3DSAMPLERSTATETYPE state);
	static unsigned Get_DX8_Render_State(D3DRENDERSTATETYPE state) { return RenderStates[state]; }

	// Names of the specific values of render states and texture stage states
	static void Get_DX8_Texture_Stage_State_Value_Name(StringClass& name, D3DTEXTURESTAGESTATETYPE state, unsigned value);
	static void Get_DX8_Sampler_State_Value_Name(StringClass& name, D3DSAMPLERSTATETYPE state, unsigned value);
	static void Get_DX8_Render_State_Value_Name(StringClass& name, D3DRENDERSTATETYPE state, unsigned value);

	static const char* Get_DX8_Texture_Address_Name(unsigned value);
	static const char* Get_DX8_Texture_Filter_Name(unsigned value);
	static const char* Get_DX8_Texture_Arg_Name(unsigned value);
	static const char* Get_DX8_Texture_Op_Name(unsigned value);
	static const char* Get_DX8_Texture_Transform_Flag_Name(unsigned value);
	static const char* Get_DX8_ZBuffer_Type_Name(unsigned value);
	static const char* Get_DX8_Fill_Mode_Name(unsigned value);
	static const char* Get_DX8_Shade_Mode_Name(unsigned value);
	static const char* Get_DX8_Blend_Name(unsigned value);
	static const char* Get_DX8_Cull_Mode_Name(unsigned value);
	static const char* Get_DX8_Cmp_Func_Name(unsigned value);
	static const char* Get_DX8_Fog_Mode_Name(unsigned value);
	static const char* Get_DX8_Stencil_Op_Name(unsigned value);
	static const char* Get_DX8_Material_Source_Name(unsigned value);
	static const char* Get_DX8_Vertex_Blend_Flag_Name(unsigned value);
	static const char* Get_DX8_Patch_Edge_Style_Name(unsigned value);
	static const char* Get_DX8_Debug_Monitor_Token_Name(unsigned value);
	static const char* Get_DX8_Blend_Op_Name(unsigned value);

	static void Invalidate_Cached_Render_States(void);

	static void Set_Draw_Polygon_Low_Bound_Limit(unsigned n) { DrawPolygonLowBoundLimit=n; }

protected:

	static bool	Create_Device(void);
	static void Release_Device(void);

	static void Reset_Statistics();
	static void Enumerate_Devices();
	static void Set_Default_Global_Render_States(void);

	/*
	** Device Selection Code.
	** For backward compatibility, the public interface for these functions is in the ww3d.
	** header file.  These functions are protected so that we aren't exposing two interfaces.
	*/
	static bool Set_Any_Render_Device(void);
	static bool	Set_Render_Device(const char * dev_name,int width=-1,int height=-1,int bits=-1,int windowed=-1,bool resize_window=false);
	static bool	Set_Render_Device(int dev=-1,int resx=-1,int resy=-1,int bits=-1,int windowed=-1,bool resize_window = false, bool reset_device = false, bool restore_assets=true);
	static bool Set_Next_Render_Device(void);
	static bool Toggle_Windowed(void);

	static int	Get_Render_Device_Count(void);
	static int	Get_Render_Device(void);
	static const RenderDeviceDescClass & Get_Render_Device_Desc(int deviceidx);
	static const char * Get_Render_Device_Name(int device_index);
	static bool Set_Device_Resolution(int width=-1,int height=-1,int bits=-1,int windowed=-1, bool resize_window=false);
	static void Get_Device_Resolution(int & set_w,int & set_h,int & set_bits,bool & set_windowed);
	static void Get_Render_Target_Resolution(int & set_w,int & set_h,int & set_bits,bool & set_windowed);
	static int	Get_Device_Resolution_Width(void) { return ResolutionWidth; }
	static int	Get_Device_Resolution_Height(void) { return ResolutionHeight; }

#if defined(_WIN32)	// the registry: Windows only
	static bool Registry_Save_Render_Device( const char *sub_key, int device, int width, int height, int depth, bool windowed, int texture_depth);
	static bool Registry_Load_Render_Device( const char * sub_key, char *device, int device_len, int &width, int &height, int &depth, int &windowed, int &texture_depth);
#endif
	static bool Is_Windowed(void) { return IsWindowed; }

	static void	Set_Texture_Bitdepth(int depth)	{ WWASSERT(depth==16 || depth==32); TextureBitDepth = depth; }
	static int	Get_Texture_Bitdepth(void)			{ return TextureBitDepth; }

	static void	Set_Swap_Interval(int swap);
	static int	Get_Swap_Interval(void);
	static void Set_Polygon_Mode(int mode);

	/*
	** Internal functions
	*/
	// One render target surface copied into a system-memory surface the CPU can read.  Both
	// read-back entry points above end here; they differ only in which surface they hand it and
	// in whether multisampling had to be resolved first.
	static SurfaceClass * Copy_Surface_To_System_Memory(IDirect3DSurface9 * source);

	static bool Find_Color_And_Z_Mode(int resx,int resy,int bitdepth,D3DFORMAT * set_colorbuffer,D3DFORMAT * set_backbuffer, D3DFORMAT * set_zmode);
	static bool Find_Color_Mode(D3DFORMAT colorbuffer, int resx, int resy, UINT *mode);
	static bool Find_Z_Mode(D3DFORMAT colorbuffer,D3DFORMAT backbuffer, D3DFORMAT *zmode);
	static bool Test_Z_Mode(D3DFORMAT colorbuffer,D3DFORMAT backbuffer, D3DFORMAT zmode);
	static void Compute_Caps(WW3DFormat display_format);

	/*
	** Protected Member Variables
	*/

	static DX8_CleanupHook *m_pCleanupHook;

	static RenderStateStruct			render_state;
	static unsigned						render_state_changed;
	static Matrix4x4						DX8Transforms[D3DTS_WORLD+1];

	static bool								IsInitted;
	static bool								IsDeviceLost;
	static void *							Hwnd;
	static unsigned						_MainThreadID;

	static bool								_EnableTriangleDraw;

	static int								CurRenderDevice;
	static int								ResolutionWidth;
	static int								ResolutionHeight;
	static int								BitDepth;
	static int								TextureBitDepth;
	static bool								IsWindowed;
	static D3DFORMAT					DisplayFormat;
	
	static D3DMATRIX						old_world;
	static D3DMATRIX						old_view;
	static D3DMATRIX						old_prj;

	// shader system updates KJM v
	static uint32							Vertex_Format;
	static IDirect3DVertexShader9 *			Vertex_Shader;
	static IDirect3DPixelShader9 *			Pixel_Shader;

	static Vector4							Vertex_Shader_Constants[MAX_VERTEX_SHADER_CONSTANTS];
	static Vector4							Pixel_Shader_Constants[MAX_PIXEL_SHADER_CONSTANTS];
	static bool Vertex_Shader_Constants_Valid[MAX_VERTEX_SHADER_CONSTANTS];
	static bool Pixel_Shader_Constants_Valid[MAX_PIXEL_SHADER_CONSTANTS];

	static LightEnvironmentClass*		Light_Environment;
	static RenderInfoClass*				Render_Info;

	static uint32							Vertex_Processing_Behavior;

	static ZTextureClass*				Shadow_Map[MAX_SHADOW_MAPS];

	static Vector3							Ambient_Color;
	// shader system updates KJM ^

	static bool								world_identity;
	static unsigned						RenderStates[256];
	static unsigned						TextureStageStates[MAX_TEXTURE_STAGES][32];
	static unsigned						SamplerStates[MAX_TEXTURE_STAGES][MAX_SAMPLER_STATES];
	static float							DepthBufferEpsilon;
	static IDirect3DBaseTexture9 *	Textures[MAX_TEXTURE_STAGES];

	// These fog settings are constant for all objects in a given scene,
	// unlike the matching renderstates which vary based on shader settings.
	static bool								FogEnable;
	static D3DCOLOR						FogColor;

	static unsigned						matrix_changes;
	static unsigned						material_changes;
	static unsigned						vertex_buffer_changes;
	static unsigned						index_buffer_changes;
	static unsigned						light_changes;
	static unsigned						texture_changes;
	static unsigned						render_state_changes;
	static unsigned						texture_stage_state_changes;
	static unsigned						draw_calls;
	static bool								CurrentDX8LightEnables[4];

	// The material and lights last handed to both devices.  VertexMaterialClass::Apply and
	// Set_Light_Environment send the same values mesh after mesh, and a repeat is dropped before it
	// reaches either device.  Invalidate_Cached_Render_States forgets them with the rest.
	static D3DMATERIAL9					CurrentDX8Material;
	static bool								CurrentDX8MaterialValid;
	static D3DLIGHT9						CurrentDX8Lights[4];
	static unsigned						CurrentDX8LightsValid;

	// What the Direct3D 9 device has not been told yet, one bit per state, stage or light; the
	// values are the caches above.  Defer_D3D9_State says whether a setter marks or calls.
	static bool								Defer_D3D9_State();
	static bool								D3D9StatePending;
	static unsigned						PendingRenderStates[256/32];
	static unsigned						PendingTextureStageStates[MAX_TEXTURE_STAGES];
	static unsigned						PendingSamplerStates[MAX_TEXTURE_STAGES];
	static unsigned						PendingTextures;
	static bool								PendingMaterial;
	static D3DMATERIAL9					PendingMaterialValue;
	static unsigned						PendingLights;
	static unsigned						PendingLightEnables;
	static D3DLIGHT9						PendingLightValues[4];

	// While Direct3D 11 presents, the Direct3D 9 scene is begun by the first draw that still goes to
	// that device, not by Begin_Scene, and End_Scene ends it only if one was begun.
	static void								Open_D3D9_Scene();
	static bool								D3D9SceneOpen;

	static unsigned long FrameCount;

	static DX8Caps*						CurrentCaps;

	static D3DADAPTER_IDENTIFIER9		CurrentAdapterIdentifier;

	static IDirect3D9 *					D3DInterface;			//d3d8;
	static IDirect3DDevice9 *			D3DDevice;				//d3ddevice8;

	static IDirect3DSurface9 *			CurrentRenderTarget;
	static IDirect3DSurface9 *			CurrentDepthBuffer;
	static IDirect3DSurface9 *			DefaultRenderTarget;
	static IDirect3DSurface9 *			DefaultDepthBuffer;

	static unsigned							DrawPolygonLowBoundLimit;

	static bool								IsRenderToTexture;

	static int								ZBias;
	static float							ZNear;
	static float							ZFar;
	static Matrix4x4						ProjectionMatrix;

	friend void DX8_Assert();
	friend class WW3D;
	friend class DX8IndexBufferClass;
	friend class DX8VertexBufferClass;
};

// shader system updates KJM v
WWINLINE void DX8Wrapper::Set_Vertex_Format(uint32 fvf)
{
#if 0 //(gth) some code is bypassing this acessor function so we can't count on this variable...
	// may be incorrect if shaders are created and destroyed dynamically
	if (Vertex_Format==fvf) return;
#endif

	Vertex_Format=fvf;
	// A vertex shader and an FVF are mutually exclusive in D3D9: whichever was set
	// last is the one that describes the stream, so setting a format clears the shader.
	Vertex_Shader=NULL;
	DX8CALL(SetVertexShader(NULL));
	DX8CALL(SetFVF(Vertex_Format));
	Direct3D11_Mirror_Vertex_Shader(NULL);
	Direct3D11_Mirror_Vertex_Format(Vertex_Format);
}

WWINLINE void DX8Wrapper::Set_Vertex_Shader(IDirect3DVertexShader9 * vertex_shader)
{
	Vertex_Shader=vertex_shader;
	DX8CALL(SetVertexShader(Vertex_Shader));
	Direct3D11_Mirror_Vertex_Shader(vertex_shader);
}

WWINLINE void DX8Wrapper::Set_Pixel_Shader(IDirect3DPixelShader9 * pixel_shader)
{
	// may be incorrect if shaders are created and destroyed dynamically
	if (Pixel_Shader==pixel_shader) return;

	Pixel_Shader=pixel_shader;
	DX8CALL(SetPixelShader(Pixel_Shader));
	Direct3D11_Mirror_Pixel_Shader(pixel_shader);
}

WWINLINE void DX8Wrapper::Set_Vertex_Shader_Constant(int reg, const void* data, int count)
{
	int memsize=sizeof(Vector4)*count;

	// The mirrored backend owns its own cache and must see every requested update.
	Direct3D11_Mirror_Vertex_Shader_Constant((unsigned)reg, (const float*)data, (unsigned)count);

#if defined(__linux__)
	bool valid = true;
	for (int i = reg; i < reg + count; ++i)
		valid = valid && Vertex_Shader_Constants_Valid[i];
	if (valid && memcmp(data, &Vertex_Shader_Constants[reg],memsize)==0) return;
#else
	if (memcmp(data, &Vertex_Shader_Constants[reg],memsize)==0) return;
#endif

	memcpy(&Vertex_Shader_Constants[reg],data,memsize);
	DX8CALL(SetVertexShaderConstantF(reg,(const float*)data,count));
#if defined(__linux__)
	for (int i = reg; i < reg + count; ++i)
		Vertex_Shader_Constants_Valid[i] = true;
#endif
}

WWINLINE void DX8Wrapper::Set_Pixel_Shader_Constant(int reg, const void* data, int count)
{
	int memsize=sizeof(Vector4)*count;

#if defined(__linux__)
	bool valid = true;
	for (int i = reg; i < reg + count; ++i)
		valid = valid && Pixel_Shader_Constants_Valid[i];
	if (valid && memcmp(data, &Pixel_Shader_Constants[reg],memsize)==0) return;
#else
	if (memcmp(data, &Pixel_Shader_Constants[reg],memsize)==0) return;
#endif

	memcpy(&Pixel_Shader_Constants[reg],data,memsize);
	DX8CALL(SetPixelShaderConstantF(reg,(const float*)data,count));
#if defined(__linux__)
	for (int i = reg; i < reg + count; ++i)
		Pixel_Shader_Constants_Valid[i] = true;
#endif
}
// shader system updates KJM ^


WWINLINE void DX8Wrapper::_Set_DX8_Transform(D3DTRANSFORMSTATETYPE transform,const Matrix4x4& m)
{
	WWASSERT(transform<=D3DTS_WORLD);
#if 0 // (gth) this optimization is breaking generals because they set the transform behind our backs.
	if (m!=DX8Transforms[transform]) 
#endif
	{
		DX8Transforms[transform]=m;
		SNAPSHOT_SAY(("DX8 - SetTransform %d [%f,%f,%f,%f][%f,%f,%f,%f][%f,%f,%f,%f][%f,%f,%f,%f]\n",transform,m[0][0],m[0][1],m[0][2],m[0][3],m[1][0],m[1][1],m[1][2],m[1][3],m[2][0],m[2][1],m[2][2],m[2][3],m[3][0],m[3][1],m[3][2],m[3][3]));
		DX8_RECORD_MATRIX_CHANGE();
		DX8CALL(SetTransform(transform,(D3DMATRIX*)&m));
		Direct3D11_Mirror_Transform(transform, (const float*)&DX8Transforms[transform]);
	}
}


WWINLINE void DX8Wrapper::_Set_DX8_Transform(D3DTRANSFORMSTATETYPE transform,const Matrix3D& m)
{
	WWASSERT(transform<=D3DTS_WORLD);
	Matrix4x4 mtx(m);
#if 0 // (gth) this optimization is breaking generals because they set the transform behind our backs.
	if (mtx!=DX8Transforms[transform]) 
#endif
	{
		DX8Transforms[transform]=mtx;
		SNAPSHOT_SAY(("DX8 - SetTransform %d [%f,%f,%f,%f][%f,%f,%f,%f][%f,%f,%f,%f]\n",transform,m[0][0],m[0][1],m[0][2],m[0][3],m[1][0],m[1][1],m[1][2],m[1][3],m[2][0],m[2][1],m[2][2],m[2][3]));
		DX8_RECORD_MATRIX_CHANGE();
		DX8CALL(SetTransform(transform,(D3DMATRIX*)&m));
		Direct3D11_Mirror_Transform(transform, (const float*)&DX8Transforms[transform]);
	}
}

WWINLINE void DX8Wrapper::_Get_DX8_Transform(D3DTRANSFORMSTATETYPE transform, Matrix4x4& m)
{
	DX8CALL(GetTransform(transform,(D3DMATRIX*)&m));
}

// ----------------------------------------------------------------------------
//
// Set the index offset for the current index buffer
//
// ----------------------------------------------------------------------------

WWINLINE void DX8Wrapper::Set_Index_Buffer_Index_Offset(unsigned offset)
{
	if (render_state.index_base_offset==offset) return;
	render_state.index_base_offset=offset;
	render_state_changed|=INDEX_BUFFER_CHANGED;
}

// ----------------------------------------------------------------------------
// Set the fog settings. This function should be used, rather than setting the
// appropriate renderstates directly, because the shader sets some of the
// renderstates on a per-mesh / per-pass basis depending on global fog states
// (stored in the wrapper) as well as the shader settings.
// This function should be called rarely - once per scene would be appropriate.
// ----------------------------------------------------------------------------

WWINLINE void DX8Wrapper::Set_Fog(bool enable, const Vector3 &color, float start, float end)
{
	// Set global states
	FogEnable = enable;
	FogColor = Convert_Color(color,0.0f);

	// Invalidate the current shader (since the renderstates set by the shader
	// depend on the global fog settings as well as the actual shader settings)
	ShaderClass::Invalidate();

	// Set renderstates which are not affected by the shader
	Set_DX8_Render_State(D3DRS_FOGSTART, *(uint32 *)(&start));
	Set_DX8_Render_State(D3DRS_FOGEND,   *(uint32 *)(&end));
}


WWINLINE void DX8Wrapper::Set_Ambient(const Vector3& color)
{
	Ambient_Color=color;
	Set_DX8_Render_State(D3DRS_AMBIENT, DX8Wrapper::Convert_Color(color,0.0f));
}

// ----------------------------------------------------------------------------
//
// Set vertex buffer to be used in the subsequent render calls. If there was
// a vertex buffer being used earlier, release the reference to it. Passing
// NULL just will release the vertex buffer.
//
// ----------------------------------------------------------------------------

// While Direct3D 11 presents, the Direct3D 9 device skips its indexed draws, and what it was told
// for them was 7-8% of the main thread spent inside d3d9.dll at 1080p.  The few readers left - the
// fixed-function probe's sampled draws, the draws that still go to the device, the smudge test's
// read back - call Flush_Deferred_D3D9_State first.  True means mark the state pending, not call.
WWINLINE bool DX8Wrapper::Defer_D3D9_State()
{
	if (!Direct3D11_Present_Is_Enabled()) return false;
	D3D9StatePending=true;
	return true;
}

WWINLINE void DX8Wrapper::Set_DX8_Material(const D3DMATERIAL9* mat)
{
	WWASSERT(mat);
	if (CurrentDX8MaterialValid && memcmp(&CurrentDX8Material, mat, sizeof(D3DMATERIAL9))==0) return;
	DX8_RECORD_MATERIAL_CHANGE();
	CurrentDX8Material=*mat;
	CurrentDX8MaterialValid=true;
	SNAPSHOT_SAY(("DX8 - SetMaterial\n"));
	if (Defer_D3D9_State()) {
		PendingMaterialValue=*mat;
		PendingMaterial=true;
	}
	else {
		DX8CALL(SetMaterial(mat));
	}

	// D3DCOLORVALUE is four floats in red, green, blue, alpha order, which is the order the
	// generated shader reads them, so each one goes across as it stands.
	Direct3D11_Mirror_Material((const float*)&mat->Ambient, (const float*)&mat->Diffuse,
		(const float*)&mat->Specular, (const float*)&mat->Emissive, mat->Power);
}

WWINLINE void DX8Wrapper::Set_DX8_Light(int index, D3DLIGHT9* light)
{
	if (light) {
		// An enabled light holding these exact bits already: the device and the backend have it.
		if (CurrentDX8LightEnables[index] && (CurrentDX8LightsValid&(1u<<index))
				&& memcmp(&CurrentDX8Lights[index], light, sizeof(D3DLIGHT9))==0) return;
		CurrentDX8Lights[index]=*light;
		CurrentDX8LightsValid|=1u<<index;
		DX8_RECORD_LIGHT_CHANGE();
		if (Defer_D3D9_State()) {
			PendingLightValues[index]=*light;
			PendingLights|=1u<<index;
			PendingLightEnables|=1u<<index;
		}
		else {
			DX8CALL(SetLight(index,light));
			DX8CALL(LightEnable(index,true));
		}
		CurrentDX8LightEnables[index]=true;
		SNAPSHOT_SAY(("DX8 - SetLight %d\n",index));

		// D3DLIGHT9 keeps the position and the direction as three floats each and the attenuation
		// and the cone as loose scalars; ffvertex reads seven four-float fields, so they are packed
		// here rather than in the backend, which never sees a D3DLIGHT9.
		const float position[4] = { light->Position.x, light->Position.y, light->Position.z, 1.0f };
		const float direction[4] = { light->Direction.x, light->Direction.y, light->Direction.z, 0.0f };
		const float attenuation[4] = { light->Attenuation0, light->Attenuation1,
			light->Attenuation2, light->Range };
		const float spot[4] = { cosf(light->Theta * 0.5f), cosf(light->Phi * 0.5f),
			light->Falloff, 0.0f };
		Direct3D11_Mirror_Light(index, light->Type, position, direction,
			(const float*)&light->Diffuse, (const float*)&light->Specular, attenuation, spot,
			(const float*)&light->Ambient);
	}
	else if (CurrentDX8LightEnables[index]) {
		DX8_RECORD_LIGHT_CHANGE();
		CurrentDX8LightEnables[index]=false;
		if (Defer_D3D9_State()) {
			PendingLightEnables|=1u<<index;
		}
		else {
			DX8CALL(LightEnable(index,false));
		}
		Direct3D11_Mirror_Light_Disabled(index);
		SNAPSHOT_SAY(("DX8 - DisableLight %d\n",index));
	}
}

WWINLINE void DX8Wrapper::Set_DX8_Render_State(D3DRENDERSTATETYPE state, unsigned value)
{
	// Can't monitor state changes because setShader call to GERD may change the states!
	if (RenderStates[state]==value) return;

#ifdef MESH_RENDER_SNAPSHOT_ENABLED
	if (WW3D::Is_Snapshot_Activated()) {
		StringClass value_name(0,true);
		Get_DX8_Render_State_Value_Name(value_name,state,value);
		SNAPSHOT_SAY(("DX8 - SetRenderState(state: %s, value: %s)\n",
			Get_DX8_Render_State_Name(state),
			(const char *)value_name));	// a StringClass is not a vararg
	}
#endif

	RenderStates[state]=value;
	// The water reads the cull mode back off the device and the stencil passes the colour write
	// mask, and each puts what it read back through here into the Direct3D 11 mirror.  Those two
	// still reach the device on every call, so what is read is what it always was.
	if (state!=D3DRS_CULLMODE && state!=D3DRS_COLORWRITEENABLE && Defer_D3D9_State()) {
		PendingRenderStates[state>>5]|=1u<<(state&31);
	}
	else {
		DX8CALL(SetRenderState( state, value ));
	}
	Direct3D11_Mirror_Render_State(state, value);
	DX8_RECORD_RENDER_STATE_CHANGE();
}

WWINLINE void DX8Wrapper::Set_DX8_Clip_Plane(uint32 Index, const float* pPlane)
{
	DX8CALL(SetClipPlane( Index, pPlane ));
}

WWINLINE void DX8Wrapper::Set_DX8_Texture_Stage_State(unsigned stage, D3DTEXTURESTAGESTATETYPE state, unsigned value)
{
  	if (stage >= MAX_TEXTURE_STAGES)
  	{	DX8CALL(SetTextureStageState( stage, state, value ));
  		return;
  	}

	// Can't monitor state changes because setShader call to GERD may change the states!
	if (TextureStageStates[stage][(unsigned int)state]==value) return;
#ifdef MESH_RENDER_SNAPSHOT_ENABLED
	if (WW3D::Is_Snapshot_Activated()) {
		StringClass value_name(0,true);
		Get_DX8_Texture_Stage_State_Value_Name(value_name,state,value);
		SNAPSHOT_SAY(("DX8 - SetTextureStageState(stage: %d, state: %s, value: %s)\n",
			stage,
			Get_DX8_Texture_Stage_State_Name(state),
			(const char *)value_name));	// a StringClass is not a vararg
	}
#endif

	TextureStageStates[stage][(unsigned int)state]=value;
	if (Defer_D3D9_State()) {
		PendingTextureStageStates[stage]|=1u<<(unsigned)state;
	}
	else {
		DX8CALL(SetTextureStageState( stage, state, value ));
	}
	Direct3D11_Mirror_Texture_Stage_State(stage, state, value);
	DX8_RECORD_TEXTURE_STAGE_STATE_CHANGE();
}

// The D3D9 half of the state D3D8 called a texture stage state.  Overloaded rather than
// renamed so a call site says which kind it is by the constant it passes, which is the
// only thing that actually changed about it.
WWINLINE void DX8Wrapper::Set_DX8_Texture_Stage_State(unsigned stage, D3DSAMPLERSTATETYPE state, unsigned value)
{
  	if (stage >= MAX_TEXTURE_STAGES || (unsigned)state >= MAX_SAMPLER_STATES)
  	{	DX8CALL(SetSamplerState( stage, state, value ));
  		return;
  	}

	if (SamplerStates[stage][(unsigned int)state]==value) return;
#ifdef MESH_RENDER_SNAPSHOT_ENABLED
	if (WW3D::Is_Snapshot_Activated()) {
		StringClass value_name(0,true);
		Get_DX8_Sampler_State_Value_Name(value_name,state,value);
		SNAPSHOT_SAY(("DX8 - SetSamplerState(stage: %d, state: %s, value: %s)\n",
			stage,
			Get_DX8_Sampler_State_Name(state),
			(const char *)value_name));	// a StringClass is not a vararg
	}
#endif

	SamplerStates[stage][(unsigned int)state]=value;
	if (Defer_D3D9_State()) {
		PendingSamplerStates[stage]|=1u<<(unsigned)state;
	}
	else {
		DX8CALL(SetSamplerState( stage, state, value ));
	}
	Direct3D11_Mirror_Sampler_State(stage, state, value);
	DX8_RECORD_TEXTURE_STAGE_STATE_CHANGE();
}

WWINLINE void DX8Wrapper::Set_DX8_Texture(unsigned int stage, IDirect3DBaseTexture9* texture)
{
  	if (stage >= MAX_TEXTURE_STAGES)
  	{	DX8CALL(SetTexture(stage, texture));
  		Direct3D11_Mirror_Texture(stage, texture);
  		return;
  	}

	if (Textures[stage]==texture) return;

	SNAPSHOT_SAY(("DX8 - SetTexture(%x) \n",texture));

	if (Textures[stage]) Textures[stage]->Release();
	Textures[stage] = texture;
	if (Textures[stage]) Textures[stage]->AddRef();
	if (Defer_D3D9_State()) {
		PendingTextures|=1u<<stage;
	}
	else {
		DX8CALL(SetTexture(stage, texture));
	}
	Direct3D11_Mirror_Texture(stage, texture);
	DX8_RECORD_TEXTURE_CHANGE();
}

WWINLINE Vector4 DX8Wrapper::Convert_Color(unsigned color)
{
	Vector4 col;
	col[3]=((color&0xff000000)>>24)/255.0f;
	col[0]=((color&0xff0000)>>16)/255.0f;
	col[1]=((color&0xff00)>>8)/255.0f;
	col[2]=((color&0xff)>>0)/255.0f;
//	col=Vector4(1.0f,1.0f,1.0f,1.0f);
	return col;
}

// EA converted colours in x87 assembly, with the rounding mode set to truncate and every register
// saved by hand around the block.  This is the same arithmetic in C: scale by 255 and truncate, so
// the byte values are the ones the assembler produced.

WWINLINE unsigned int DX8Wrapper::Convert_Color(const Vector3& color, const float alpha)
{
	// A channel outside 0..1 wraps, as Windows' conversion did (-0.1 is 231); C leaves converting a
	// negative float to unsigned undefined, and ARM64 made it 0.  Platform/MsvcFloatCasts.h.
	const unsigned int a = (unsigned int)floatToIntAsMsvc(alpha * 255.0f) & 0xff;
	const unsigned int r = (unsigned int)floatToIntAsMsvc(color.X * 255.0f) & 0xff;
	const unsigned int g = (unsigned int)floatToIntAsMsvc(color.Y * 255.0f) & 0xff;
	const unsigned int b = (unsigned int)floatToIntAsMsvc(color.Z * 255.0f) & 0xff;
	return (a << 24) | (r << 16) | (g << 8) | b;
}

WWINLINE void DX8Wrapper::Clamp_Color(Vector4& color)
{
	for (int i=0;i<4;++i) {
		const float f=(color[i]<0.0f) ? 0.0f : color[i];
		color[i]=(f>1.0f) ? 1.0f : f;
	}
}

WWINLINE unsigned int DX8Wrapper::Convert_Color(const Vector4& color)
{
	return Convert_Color(reinterpret_cast<const Vector3&>(color),color[3]);
}

WWINLINE unsigned int DX8Wrapper::Convert_Color_Clamp(const Vector4& color)
{
	Vector4 clamped_color=color;
	DX8Wrapper::Clamp_Color(clamped_color);
	return Convert_Color(reinterpret_cast<const Vector3&>(clamped_color),clamped_color[3]);
}



WWINLINE void DX8Wrapper::Set_Alpha (const float alpha, unsigned int &color)
{
	unsigned char *component = (unsigned char*) &color;

	component [3] = 255.0f * alpha;
}

WWINLINE void DX8Wrapper::Get_Render_State(RenderStateStruct& state)
{
	state=render_state;
}

WWINLINE void DX8Wrapper::Get_Shader(ShaderClass& shader)
{
	shader=render_state.shader;
}

WWINLINE void DX8Wrapper::Set_Texture(unsigned stage,TextureBaseClass* texture)
{
	WWASSERT(stage<(unsigned int)CurrentCaps->Get_Max_Textures_Per_Pass());
	if (texture==render_state.Textures[stage]) return;
	REF_PTR_SET(render_state.Textures[stage],texture);
	render_state_changed|=(TEXTURE0_CHANGED<<stage);
}

WWINLINE void DX8Wrapper::Set_Material(const VertexMaterialClass* material)
{
/*	if (material && render_state.material &&
		// !strcasecmp(material->Get_Name(),render_state.material->Get_Name())) {
		material->Get_CRC()!=render_state.material->Get_CRC()) {
		return;
	}
*/
//	if (material==render_state.material) {
//		return;
//	}
	REF_PTR_SET(render_state.material,const_cast<VertexMaterialClass*>(material));
	render_state_changed|=MATERIAL_CHANGED;
	SNAPSHOT_SAY(("DX8Wrapper::Set_Material(%s)\n",material ? material->Get_Name() : "NULL"));
}

WWINLINE void DX8Wrapper::Set_Shader(const ShaderClass& shader)
{
	if (!ShaderClass::ShaderDirty && ((unsigned&)shader==(unsigned&)render_state.shader)) {
		return;
	}
	render_state.shader=shader;
	render_state_changed|=SHADER_CHANGED;
#ifdef MESH_RENDER_SNAPSHOT_ENABLED
	StringClass str;
#endif
	SNAPSHOT_SAY(("DX8Wrapper::Set_Shader(%s)\n",(const char *)shader.Get_Description(str)));
}

WWINLINE void DX8Wrapper::Set_Projection_Transform_With_Z_Bias(const Matrix4x4& matrix, float znear, float zfar)
{
	ZFar=zfar;
	ZNear=znear;
	ProjectionMatrix=matrix.Transpose();

	if (!Get_Current_Caps()->Support_ZBias() && ZNear!=ZFar) {
		Matrix4x4 tmp=ProjectionMatrix;
		float tmp_zbias=ZBias;
		tmp_zbias*=(1.0f/16.0f);
		tmp_zbias*=1.0f / (ZFar - ZNear);
		tmp[2][2]-=tmp_zbias*tmp[3][2];
		DX8CALL(SetTransform(D3DTS_PROJECTION,(D3DMATRIX*)&tmp));
		Direct3D11_Mirror_Transform(D3DTS_PROJECTION,(const float*)&tmp);
	}
	else {
		DX8CALL(SetTransform(D3DTS_PROJECTION,(D3DMATRIX*)&ProjectionMatrix));
		Direct3D11_Mirror_Transform(D3DTS_PROJECTION,(const float*)&ProjectionMatrix);
	}
}

WWINLINE void DX8Wrapper::Set_DX8_ZBias(int zbias)
{
	if (zbias==ZBias) return;
	if (zbias>MAX_Z_BIAS_LEVEL) zbias=MAX_Z_BIAS_LEVEL;
	if (zbias<0) zbias=0;
	ZBias=zbias;

	if (!Get_Current_Caps()->Support_ZBias() && ZNear!=ZFar) {
		Matrix4x4 tmp=ProjectionMatrix;
		float tmp_zbias=ZBias;
		tmp_zbias*=(1.0f/16.0f);
		tmp_zbias*=1.0f / (ZFar - ZNear);
		tmp[2][2]-=tmp_zbias*tmp[3][2];
		DX8CALL(SetTransform(D3DTS_PROJECTION,(D3DMATRIX*)&tmp));
		Direct3D11_Mirror_Transform(D3DTS_PROJECTION,(const float*)&tmp);
	}
	else {
		// D3D8's D3DRS_ZBIAS took a level from 0 to 16; D3D9's D3DRS_DEPTHBIAS takes a
		// float in the depth buffer's own normalised range, passed through as a DWORD.
		// The conversion is the one d3d8to9 has been applying under this renderer all
		// along (d3d8types.cpp, CalcDepthBias), so the bias stays where the picture
		// already has it.
		const float depth_bias=ZBias*Get_Depth_Buffer_Epsilon();
		Set_DX8_Render_State(D3DRS_DEPTHBIAS,*(const uint32*)&depth_bias);
	}
}

WWINLINE void DX8Wrapper::Set_Transform(D3DTRANSFORMSTATETYPE transform,const Matrix4x4& m)
{
	switch ((int)transform) {
	case D3DTS_WORLD:
		render_state.world=m.Transpose();
		render_state_changed|=(unsigned)WORLD_CHANGED;
		render_state_changed&=~(unsigned)WORLD_IDENTITY;
		break;
	case D3DTS_VIEW:
		render_state.view=m.Transpose();
		render_state_changed|=(unsigned)VIEW_CHANGED;
		render_state_changed&=~(unsigned)VIEW_IDENTITY;
		break;
	case D3DTS_PROJECTION:
		{
			Matrix4x4 ProjectionMatrix=m.Transpose();
			ZFar=0.0f;
			ZNear=0.0f;
			DX8CALL(SetTransform(D3DTS_PROJECTION,(D3DMATRIX*)&ProjectionMatrix));
			Direct3D11_Mirror_Transform(D3DTS_PROJECTION,(const float*)&ProjectionMatrix);
		}
		break;
	default:
		DX8_RECORD_MATRIX_CHANGE();
		Matrix4x4 m2=m.Transpose();
		DX8CALL(SetTransform(transform,(D3DMATRIX*)&m2));
		// the texture-stage transforms (the scrolling and projecting mappers) reach Direct3D 11 as they reach Direct3D 9
		Direct3D11_Mirror_Transform(transform,(const float*)&m2);
		break;
	}
}

WWINLINE void DX8Wrapper::Set_Transform(D3DTRANSFORMSTATETYPE transform,const Matrix3D& m)
{
	Matrix4x4 m2(m);
	switch ((int)transform) {
	case D3DTS_WORLD:
		render_state.world=m2.Transpose();
		render_state_changed|=(unsigned)WORLD_CHANGED;
		render_state_changed&=~(unsigned)WORLD_IDENTITY;
		break;
	case D3DTS_VIEW:
		render_state.view=m2.Transpose();
		render_state_changed|=(unsigned)VIEW_CHANGED;
		render_state_changed&=~(unsigned)VIEW_IDENTITY;
		break;
	default:
		DX8_RECORD_MATRIX_CHANGE();
		m2=m2.Transpose();
		DX8CALL(SetTransform(transform,(D3DMATRIX*)&m2));
		Direct3D11_Mirror_Transform(transform,(const float*)&m2);
		break;
	}
}

WWINLINE void DX8Wrapper::Set_World_Identity()
{
	if (render_state_changed&(unsigned)WORLD_IDENTITY) return;
	render_state.world.Make_Identity();
	render_state_changed|=(unsigned)WORLD_CHANGED|(unsigned)WORLD_IDENTITY;
}

WWINLINE void DX8Wrapper::Set_View_Identity()
{
	if (render_state_changed&(unsigned)VIEW_IDENTITY) return;
	render_state.view.Make_Identity();
	render_state_changed|=(unsigned)VIEW_CHANGED|(unsigned)VIEW_IDENTITY;
}

WWINLINE bool DX8Wrapper::Is_World_Identity()
{
	return !!(render_state_changed&(unsigned)WORLD_IDENTITY);
}

WWINLINE bool DX8Wrapper::Is_View_Identity()
{
	return !!(render_state_changed&(unsigned)VIEW_IDENTITY);
}

WWINLINE void DX8Wrapper::Get_Transform(D3DTRANSFORMSTATETYPE transform, Matrix4x4& m)
{
	D3DMATRIX mat;

	switch ((int)transform) {
	case D3DTS_WORLD:
		if (render_state_changed&WORLD_IDENTITY) m.Make_Identity();
		else m=render_state.world.Transpose();
		break;
	case D3DTS_VIEW:
		if (render_state_changed&VIEW_IDENTITY) m.Make_Identity();
		else m=render_state.view.Transpose();
		break;
	default:
		DX8CALL(GetTransform(transform,&mat));
		m=*(Matrix4x4*)&mat;
		m=m.Transpose();
		break;
	}
}

WWINLINE const D3DLIGHT9& DX8Wrapper::Peek_Light(unsigned index)
{
	return render_state.Lights[index];;
}

WWINLINE bool DX8Wrapper::Is_Light_Enabled(unsigned index)
{
	return render_state.LightEnable[index];
}


WWINLINE void DX8Wrapper::Set_Render_State(const RenderStateStruct& state)
{
	int i;

	if (render_state.index_buffer) {
		render_state.index_buffer->Release_Engine_Ref();
	}

	for (i=0;i<MAX_VERTEX_STREAMS;++i) 
	{
		if (render_state.vertex_buffers[i]) 
		{
			render_state.vertex_buffers[i]->Release_Engine_Ref();
		}
	}

	render_state=state;
	render_state_changed=0xffffffff;

	if (render_state.index_buffer) {
		render_state.index_buffer->Add_Engine_Ref();
	}

	for (i=0;i<MAX_VERTEX_STREAMS;++i) 
	{
		if (render_state.vertex_buffers[i]) 
		{
			render_state.vertex_buffers[i]->Add_Engine_Ref();
		}
	}
}

WWINLINE void DX8Wrapper::Release_Render_State()
{
	int i;

	if (render_state.index_buffer) {
		render_state.index_buffer->Release_Engine_Ref();
	}

	for (i=0;i<MAX_VERTEX_STREAMS;++i) {
		if (render_state.vertex_buffers[i]) {
			render_state.vertex_buffers[i]->Release_Engine_Ref();
		}
	}

	for (i=0;i<MAX_VERTEX_STREAMS;++i) {
		REF_PTR_RELEASE(render_state.vertex_buffers[i]);
	}
	REF_PTR_RELEASE(render_state.index_buffer);
	REF_PTR_RELEASE(render_state.material);

	
	for (i=0;i<MAX_TEXTURE_STAGES;++i) 
	{
		REF_PTR_RELEASE(render_state.Textures[i]);
	}
}


WWINLINE RenderStateStruct::RenderStateStruct()
	:
	material(0),
	index_buffer(0)
{
	unsigned i;
	for (i=0;i<MAX_VERTEX_STREAMS;++i) vertex_buffers[i]=0;
	for (i=0;i<MAX_TEXTURE_STAGES;++i) Textures[i]=0;
  //lightsHash = (unsigned)this;
}

WWINLINE RenderStateStruct::~RenderStateStruct()
{
	unsigned i;
	REF_PTR_RELEASE(material);
	for (i=0;i<MAX_VERTEX_STREAMS;++i) {
		REF_PTR_RELEASE(vertex_buffers[i]);
	}
	REF_PTR_RELEASE(index_buffer);

	for (i=0;i<MAX_TEXTURE_STAGES;++i) 
	{
		REF_PTR_RELEASE(Textures[i]);
	}
}


WWINLINE unsigned flimby( char* name, unsigned crib )
{
  unsigned lnt prevVer = 0x00000000;  
  __volatile D3D2_BASE_VEC nextVer = 0;
  for( unsigned t = 0; t < crib; ++t )
  {
    (D3D2_BASE_VEC)nextVer += name[t];
    (D3D2_BASE_VEC)nextVer %= 32;
    (D3D2_BASE_VEC)nextVer-- ;
    (lnt) prevVer ^=  ( 1 << (D3D2_BASE_VEC)prevVer ); 
  }
  return (lnt) prevVer;
}

WWINLINE RenderStateStruct& RenderStateStruct::operator= (const RenderStateStruct& src)
{
	unsigned i;
	REF_PTR_SET(material,src.material);
	for (i=0;i<MAX_VERTEX_STREAMS;++i) {
		REF_PTR_SET(vertex_buffers[i],src.vertex_buffers[i]);
	}
	REF_PTR_SET(index_buffer,src.index_buffer);

	for (i=0;i<MAX_TEXTURE_STAGES;++i) 
	{
		REF_PTR_SET(Textures[i],src.Textures[i]);
	}

	LightEnable[0]=src.LightEnable[0];
	LightEnable[1]=src.LightEnable[1];
	LightEnable[2]=src.LightEnable[2];
	LightEnable[3]=src.LightEnable[3];
	if (LightEnable[0]) {
		Lights[0]=src.Lights[0];
		if (LightEnable[1]) {
			Lights[1]=src.Lights[1];
			if (LightEnable[2]) {
				Lights[2]=src.Lights[2];
				if (LightEnable[3]) {
					Lights[3]=src.Lights[3];
				}
			}
		}


    //lightsHash = flimby((char*)(&Lights[0]), sizeof(D3DLIGHT9)-1 );

	}

	shader=src.shader;
	world=src.world;
	view=src.view;
	for (i=0;i<MAX_VERTEX_STREAMS;++i) {
		vertex_buffer_types[i]=src.vertex_buffer_types[i];
	}
	index_buffer_type=src.index_buffer_type;
	vba_offset=src.vba_offset;
	vba_count=src.vba_count;
	iba_offset=src.iba_offset;
	index_base_offset=src.index_base_offset;

	return *this;
}


#endif
