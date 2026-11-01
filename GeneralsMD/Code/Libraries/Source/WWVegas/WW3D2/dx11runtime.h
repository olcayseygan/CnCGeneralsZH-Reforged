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
// Modified 2026 by İlyas Akın for the macOS/Linux port; see NOTICE.md and the git history.

/*
** The one Direct3D 11 device and backend the process has, and the switch that asks for them.
**
** WW3D2 cannot see GlobalData, so -dx11 arrives the way -msaa and -ffshader do: pushed in from
** W3DDisplay::init before the device is made.  Asking for it is not the same as getting it - a
** machine with no Direct3D 11 driver and no WARP has neither - so Is_Active answers what actually
** happened rather than what was asked for, and everything that branches on the backend branches on
** that.
**
** The funnel is not finished while this is here: the engine still reaches the Direct3D 9 device
** past DX8Wrapper, and until those calls go through the wrapper a -dx11 run has a backend that
** only the wrapper's own state calls reach.
**
** Measured 2026-09-22, before the port changed the renderer: 125 references to _Get_D3D_Device()/_Get_D3D() in
** GeneralsMD/Code, 89 of them outside dx8wrapper.{cpp,h}, which between them make 148 calls on the
** device - 47 straight through the accessor and 101 through a pointer captured into a local or a
** member first.  There are 22 DX8CALL sites outside the wrapper on top of that.
**
** This comment used to say 236, counted on 2026-09-09 by bfb60e17, which is the same measure taken
** before ef8303a9 removed 111 of them the same day.  A call-site survey (D1) recorded the
** categories, the file list and the order the rest of them move in; re-measure rather than quoting
** either number.
**
** What the shutdown log reports - the refusal counters and the foreign shader list at the bottom
** of this header, written out by W3DDisplay - says how far that has got on any given run rather
** than leaving it to be guessed.  (An earlier draft of this comment named an Enable_Reports; there
** has never been one.)
*/

#ifndef DX11RUNTIME_H
#define DX11RUNTIME_H

#if defined(_WIN32)
#include <windows.h>
#endif
// RenderWindow, RenderRect and RenderPoint: on Windows HWND, RECT and POINT themselves (A0).  Off
// Windows there is no Direct3D 11, and dx11runtime_posix.cpp answers every call as a machine without
// it would: nothing is ever active.
#include "Platform/RenderTypes.h"

#include <string>

class DX11BackendClass;
class DX11BufferTwinClass;
class DX11DeviceClass;

// Asked for on the command line, before any device exists.
void Direct3D11_Enable(bool enabled);
bool Direct3D11_Is_Enabled();

// Built once the window and its size are known.  False means the machine could not make one, and
// the caller carries on with Direct3D 9 rather than failing to start.
bool Direct3D11_Create(RenderWindow window, unsigned width, unsigned height);
void Direct3D11_Release();

// The scene's multisampling, asked for before Create (DX11DeviceClass::Request_Sample_Count): 0 or 1
// is none, otherwise 2 to 16.  Sample_Count is what the device granted, 0 for none or no device.
void Direct3D11_Set_Sample_Count(unsigned samples);
unsigned Direct3D11_Sample_Count();

// The swap chain's buffers at a new resolution.  Nothing if they already are that size.
bool Direct3D11_Resize(unsigned width, unsigned height);

// True only between a successful Create and the Release that follows it.
bool Direct3D11_Is_Active();

DX11DeviceClass * Direct3D11_Device();
DX11BackendClass * Direct3D11_Backend();

// The state the wrapper is setting on the Direct3D 9 device, copied into the backend as it goes.
// Cheap enough to be unconditional - a comparison against a bool and a return when there is no
// backend - and it is what makes a -dx11 run's backend hold the same state the frame was drawn
// with, which is the thing every later step is measured against.  Declared here rather than on
// DX11BackendClass so dx8wrapper.h does not have to include the D3D11 headers.
void Direct3D11_Mirror_Render_State(unsigned state, unsigned value);
void Direct3D11_Mirror_Texture_Stage_State(unsigned stage, unsigned state, unsigned value);
void Direct3D11_Mirror_Sampler_State(unsigned sampler, unsigned state, unsigned value);

// The rest of what the fixed-function vertex pipeline reads.  The matrix is sixteen floats in the
// order D3D9 stores them, which is by rows; the material is five colours and a power; a light is
// the seven four-float fields ffvertex declares, and a null one disables that index.
void Direct3D11_Mirror_Transform(unsigned transform, const float matrix[16]);
void Direct3D11_Mirror_Material(const float ambient[4], const float diffuse[4],
	const float specular[4], const float emissive[4], float power);
void Direct3D11_Mirror_Light(unsigned index, unsigned type, const float position[4],
	const float direction[4], const float diffuse[4], const float specular[4],
	const float attenuation[4], const float spot[4], const float ambient[4]);
void Direct3D11_Mirror_Light_Disabled(unsigned index);

// The Direct3D 11 copy of a vertex or index buffer the engine is about to create, or null on an
// ordinary run.  The buffer classes own what comes back and free it with themselves.
DX11BufferTwinClass * Direct3D11_Twin_Vertex_Buffer(unsigned byte_count, bool dynamic);
DX11BufferTwinClass * Direct3D11_Twin_Index_Buffer(unsigned byte_count, bool dynamic);

// The texture bound to a stage.  The Direct3D 11 copy is built from the Direct3D 9 texture on the
// first bind and kept on it, so nothing in the loaders has to know this exists.
void Direct3D11_Mirror_Texture(unsigned stage, struct IDirect3DBaseTexture9 * texture);

// The shroud texture the terrain draws its fog with, handed to the Warm Light haze with the
// terrain's world to texture mapping (DX11Post_Set_Haze_Shroud).  Null reads everything as in sight.
void Direct3D11_Set_Haze_Shroud(struct IDirect3DBaseTexture9 * texture, const float rect[4]);

// The Atmosphere option's fade and sky for this frame (DX11Post_Set_Atmosphere), with the sky's D3D9
// texture.  Strength zero is no pass.
void Direct3D11_Set_Atmosphere(const float horizon[3], const float tint[3], float start, float scale,
	float strength, float ground, float map_width, float map_height, struct IDirect3DBaseTexture9 * sky);
// That sky, over the whole frame, before anything is drawn on it.  The fade goes on with the haze.
void Direct3D11_Atmosphere_Sky();

// The vehicle headlights for the frame, nearest the camera first, eight floats each: world
// position, reach, world direction, cosine of the cone's edge; and what a pixel gains per channel
// per unit of their light.  A Direct3D 9 run ignores them.
void Direct3D11_Set_Headlights(const float * lights, unsigned count, const float gain[3]);

// The scene's dynamic point lights for the frame, nearest the camera first, twelve floats each
// (BLAST_LIGHT_FLOATS): world position and far reach, diffuse over the map's terrain light and near
// reach, ambient over the same and one unused.  A Direct3D 9 run ignores them.
void Direct3D11_Set_Blast_Lights(const float * lights, unsigned count);

// True while the Direct3D 11 frame's shadow-receiving programs are lighting the dynamic lights per
// pixel, which is when the engine's own vertex lighting has to leave them out or light them twice.
// False under -d3d9, -headless and the Classic graphics setting.
bool Direct3D11_Lights_Per_Pixel();

// The draws that follow are sorted particle billboards, which fade where they meet the scene's
// depth.  The sorting pool sets it around the billboards' runs.
void Direct3D11_Set_Soft_Particles(bool soft);

// Copy the scene's depth for the soft particles, if anything wrote it since the last copy.  The
// sorting pool calls it at the top of each flush, before any particle run resolves its program.
void Direct3D11_Take_Scene_Depth();

// Whether the soft particles run at all.  Off under the Classic graphics setting, so its picture
// matches -d3d9's.  On by default.
void Direct3D11_Allow_Soft_Particles(bool allowed);

// The sun's depth buffer.  Between Begin and End every draw lands in it and nowhere else, which is
// how the caster pass is written without the engine knowing what a render target is.  False from
// Begin means there is no Direct3D 11 backend or the device refused the surface, and the caller
// draws nothing rather than drawing the casters over the frame.  SHADOW-MAP-PLAN.md.
bool Direct3D11_Begin_Shadow_Map(unsigned size);
void Direct3D11_End_Shadow_Map();
bool Direct3D11_Shadow_Map_Bound();
std::string Direct3D11_Shadow_Map_Report();

// What turns the map into a shadow: the matrix that takes a pixel's clip space position into the
// sun's clip space, the depth bias that keeps a surface from shadowing itself, how dark a fully
// blocked pixel goes and how wide the filter reaches, in texels.  Set once a frame, cleared when
// the frame has no map.
void Direct3D11_Set_Shadow_Parameters(float bias, float strength, float widest_radius_in_texels,
	float narrowest_radius_in_texels, float texels_per_unit_of_gap, float units_per_unit_of_depth,
	float sky_fill);
void Direct3D11_Clear_Shadow_Parameters();

// The smoke in the sun's light, after Direct3D11_End_Shadow_Map and from the sun it drew with: five
// floats a caster (world x, y, z, radius, optical depth through its middle) and how dark the
// thickest smoke leaves what is behind it.  Called every frame the map is drawn; no casters is a
// frame without smoke.  False when there is no backend or the device cannot hold the map, and the
// caller keeps its older shade under the clouds.
bool Direct3D11_Fill_Smoke_Map(const float * casters, unsigned count, float strength);

// The sun's opaque casters, depth only and instanced, while the map is bound
// (DX11BackendClass::Upload_Shadow_Casters and Draw_Shadow_Casters): the pass's instances go up
// once, a world an instance as the three rows of its Matrix3D (NULL for an identity world) and the
// first bone of a skinned one, with the pass's bones, three rows of a Matrix3D each.  A run is
// one piece of one mesh model drawn once for each of its instances.  An alpha tested run samples
// the texture set on stage 0, so the runs are drawn a texture at a time.  False with nothing drawn
// when there is no backend, it refused a program or a buffer, or alpha tested runs find no texture
// on stage 0, and the caller draws those casters the old way.
enum DX11ShadowCasterKind
{
	DX11_SHADOW_CASTER_RIGID,
	DX11_SHADOW_CASTER_ALPHA_TESTED,
	DX11_SHADOW_CASTER_SKINNED
};
struct DX11ShadowCasterRun
{
	DX11ShadowCasterKind Kind;
	DX11BufferTwinClass * Vertices;		///< NULL for a skinned run, which reads the skin source
	unsigned Stride;
	unsigned TextureOffset;					///< bytes into a vertex to stage 0's coordinates
	DX11BufferTwinClass * Indices;
	unsigned StartIndex;
	unsigned IndexCount;
	unsigned BaseVertex;
	bool Strip;
	unsigned long CullMode;			///< D3DCULL
	unsigned long DepthFunction;	///< D3DCMPFUNC
	unsigned FirstInstance;
	unsigned InstanceCount;
};
bool Direct3D11_Upload_Shadow_Casters(const float * const * worlds, const unsigned * first_bones,
	unsigned instance_count, const float * const * bones, unsigned bone_count);
bool Direct3D11_Draw_Shadow_Casters(const DX11ShadowCasterRun * runs, unsigned run_count);

// A skin's bind pose for the depth pass to skin on the GPU (DX11BackendClass::Upload_Shadow_Skin):
// a position in its bone's space and the bone, a vertex at a time.  The first vertex comes back
// with the generation it is good for; a model uploaded under an older one uploads again.
struct DX11ShadowSkinVertex
{
	float Position[3];
	unsigned Bone;
};
bool Direct3D11_Upload_Shadow_Skin(const DX11ShadowSkinVertex * vertices, unsigned count,
	unsigned & first_vertex);
unsigned Direct3D11_Shadow_Skin_Generation();

// The scene camera's view, in the device's layout, set by the shadow pass once the frame's camera is
// back.  A draw made in camera space (identity view, perspective projection: the sorted particles)
// looks its shadow up through it.
void Direct3D11_Set_Scene_View(const float view[16]);

// The draws that follow carry a fire's glow in their normals, to be added after the shade
// (DX11BackendClass::Set_Smoke_Glow).  The sorting pool sets it around the smoke billboards' runs.
void Direct3D11_Set_Smoke_Glow(bool glow);

// The draws that follow are a fire or explosion system's billboards, with each particle's age and
// seed in the second coordinate set (DX11BackendClass::Set_Fire_Look).  The sorting pool sets it.
void Direct3D11_Set_Fire_Look(bool fire);

// The draws that follow are an Emissive particle system's billboards, which take no shadow and no
// smoke shade (DX11BackendClass::Set_Emissive).  The sorting pool sets it.
void Direct3D11_Set_Emissive(bool emissive);

// The draws that follow are a house colour mesh that glows past white for the bloom
// (DX11BackendClass::Set_Team_Glow, the Team Glow option).  The mesh renderer sets it around the mesh.
void Direct3D11_Set_Team_Glow(bool glow);

// The Hemispheric Ambient option (DX11BackendClass::Set_Hemi_Ambient): lit models take the scene
// ambient from the sky above and the ground below.  The scene hands it over every frame.  -d3d9,
// -headless and the SDL3 port keep the flat ambient.
void Direct3D11_Set_Hemi_Ambient(bool hemi);

// The look one object's draws are dressed in (DRAW_EFFECT_* in ffshader.h) and its four numbers
// (DX11BackendClass::Set_Draw_Effect).  The mesh renderer sets it around a mesh's draws and puts
// zero back after them.  -d3d9, -headless and the SDL3 port draw the object as they always did.
void Direct3D11_Set_Draw_Effect(unsigned effect, const float parameters[4]);

// Whether the looks are drawn at all: a Direct3D 11 frame is presenting and the Classic graphics
// setting is off.
bool Direct3D11_Draw_Effects();
void Direct3D11_Allow_Draw_Effects(bool allowed);

// Whether one look asked for now would be drawn (DX11BackendClass::Takes_Draw_Effect).  The scene
// asks before it leaves the heat vision pass or the disabled tint out for it.
bool Direct3D11_Takes_Draw_Effect(unsigned effect);

// The look the mesh renderer has set and its numbers, zero and zeros with none.  The sorting pool
// keeps it with a sorted mesh's triangles and sets it again when it draws them.
unsigned Direct3D11_Requested_Draw_Effect(float parameters[4]);

// The post chain's ambient occlusion, applied to the scene where it stands, once a frame: called
// after the opaque world and before the particles, so fire and smoke, which write no depth, are
// not shaded by the corners of what is behind them.  The chain then skips its own occlusion pass.
// Nothing happens without "ao" in the chain or while a filter or a mirror holds the target.
void Direct3D11_Occlude_Scene();

// The Ambient Occlusion option (DX11PostProcessClass::World_Occlude_Scene), called once the opaque world
// is down and before the water.  The chain's own occlusion stands down for the frame.  -d3d9, Classic
// graphics, -headless and the SDL3 port draw nothing here.
void Direct3D11_Ambient_Occlusion();

// Whether the draws that follow take the Glow option's additive gain (DX11BackendClass::
// Set_Glow_Draw).  ShaderClass::Apply sends it from the shader's GLOW_ENABLE bit.
void Direct3D11_Set_Glow_Draw(bool glow);

// The texture and its level-zero surface the engine redirects the scene into for its screen
// filters (W3DShaderManager's render texture), or both NULL.  The Direct3D 11 frame keeps the scene
// in its own target while that surface is bound, and a draw sampling the texture samples a copy of
// the scene (DX11BackendClass::Set_Scene_Stand_In).
void Direct3D11_Set_Scene_Stand_In(struct IDirect3DBaseTexture9 * texture,
	struct IDirect3DSurface9 * surface);

// The CPU has just written this surface.  The next bind of its texture fills the Direct3D 11 copy
// again.  A no-op when the backend is not running.
void Direct3D11_Mark_Surface_Dirty(struct IDirect3DSurface9 * surface);

// Send the draws into the copy of whatever texture this surface belongs to.  A null surface, or one
// with no texture behind it, is the back buffer.
void Direct3D11_Mirror_Render_Target(struct IDirect3DSurface9 * surface);

// One of the engine's surface copies, carried into the copy of the destination texture.  This is
// how a default-pool texture the CPU cannot read - the shroud - reaches D3D11 at all.
void Direct3D11_Mirror_Surface_Copy(struct IDirect3DSurface9 * destination,
	struct IDirect3DSurface9 * source, const RenderRect * source_rectangle,
	const RenderPoint * destination_point);

// The frame, alongside Direct3D 9's own.  Begin binds the back buffer and the viewport, Clear
// takes the same arguments DX8Wrapper::Clear was given, and End presents only when the run asked
// to see the Direct3D 11 picture - otherwise the frame is drawn into a back buffer nothing shows,
// which is what makes it safe to leave both paths running.
void Direct3D11_Begin_Scene();
void Direct3D11_Mirror_Clear(bool colour, bool depth, float red, float green, float blue,
	float alpha);
void Direct3D11_End_Scene(bool flip_frames);

// -dx11present: show the Direct3D 11 frame in the window instead of Direct3D 9's, and read
// screenshots back from it.  Without it nothing on screen changes when -dx11 is passed.
// -dx11dump: write every generated program to this directory as it is built.
void Direct3D11_Dump_Programs_To(const char * directory);
// Where the user's compiled programs are kept (the game's user data folder); before the device exists.
// Unset, the working directory.
void Direct3D11_Set_Shader_Cache_Directory(const char * directory);

void Direct3D11_Present_Enable(bool enabled);
bool Direct3D11_Present_Is_Enabled();
// The swap chain is the flip model with tearing, so vsync off runs past the monitor's refresh.
bool Direct3D11_Can_Tear();
// True once, the first time a present was refused, with the HRESULT it was refused with.  WW3D2
// has no logging in a shipping build, so the display asks and writes the line.
bool Direct3D11_Take_Present_Failure(long & result);
// After a refused present, why the device went: DXGI's removed reason (a driver reset, a hung or
// removed card), or 0 while the device is alive.  A removed device never comes back.
long Direct3D11_Device_Removed_Reason();

// Wait for the monitor on the Direct3D 11 present.  DXGI takes the interval per Present, so this
// does not need a device reset the way Direct3D 9's presentation interval does.
void Direct3D11_Set_VSync(bool enabled);

// -dx11post: the effects run over the finished frame, named in the order they run, as in "fxaa" or
// "fxaa,sharpen".  "off" or an empty chain draws straight into the swap chain the way the backend
// always has.  Returns false for a name it does not know and turns the chain off, so a misspelt
// switch is a plain frame and a report line rather than a different picture.
//
// This is the first thing in the backend that draws a frame Direct3D 9 does not draw.  The game
// hands it "bloom,fxaa,sharpen" unless -dx11post names another chain; dx11-check.ps1's exit
// measurement passes "-dx11post off".
bool Direct3D11_Post_Chain(const char * chain);

// The chain and the size it runs at, for the shutdown report, or why there is no chain.
const char * Direct3D11_Post_Diagnostic();

// Run the chain into the swap chain's back buffer, now, because the world is drawn and the command
// bar is about to go on top of it.  Everything drawn after this lands in the swap chain directly.
// An edge filter over a bitmap font does not antialias the letters, it doubles them, so where this
// is called from is as much a part of the feature as the shader is.
void Direct3D11_Finish_Scene();

// The scene in front of the player with nothing over it, for a frame that never reached the world:
// a menu, a loading screen. The present path and the screenshot path both call it and neither knows
// whether the other has, so the first one to ask pays and the second finds the work done.
void Direct3D11_Finish_Frame();

// The back buffer as it stands, in the eight-bit blue-green-red-alpha order the screenshot writer
// wants, top row first.  Has to be called before the present that discards it.  Null when there is
// nothing to read; what comes back is freed with Direct3D11_Release_Capture and nothing else.
unsigned char * Direct3D11_Capture_Back_Buffer(unsigned & width, unsigned & height,
	unsigned & pitch);
void Direct3D11_Release_Capture(unsigned char * pixels);

// The same back buffer for a recording, without waiting for it.  Queue copies it into one of
// DX11_FRAME_COPY_SLOTS staging textures kept from call to call; Map hands that copy back a frame
// later, by when the GPU has long made it, and is null for a slot that holds nothing.  A Map is
// followed by an Unmap of the same slot before the slot is queued again.
enum { DX11_FRAME_COPY_SLOTS = 16 };
bool Direct3D11_Queue_Frame_Copy(unsigned slot);
const unsigned char * Direct3D11_Map_Frame_Copy(unsigned slot, unsigned & width, unsigned & height,
	unsigned & pitch);
void Direct3D11_Unmap_Frame_Copy(unsigned slot);
void Direct3D11_Release_Frame_Copies();

// The buffers and the vertex format a draw is about to read, mirrored as DX8Wrapper binds them.
// A null twin unbinds, which is what a stream with no buffer means.  Index buffers in this engine
// are 16-bit without exception, so the format is not a parameter.
void Direct3D11_Mirror_Stream_Source(DX11BufferTwinClass * twin, unsigned stride, unsigned offset);
void Direct3D11_Mirror_Indices(DX11BufferTwinClass * twin);
void Direct3D11_Mirror_Vertex_Format(unsigned fvf);

// Whether the engine has one of its own Direct3D 9 shaders bound.  The backend generates its own
// programs out of the fixed-function state, so a draw made with a shipped .vso or .pso is a draw it
// has no equivalent for: it refuses that one rather than drawing the fixed-function approximation,
// which is how the terrain came out white instead of missing.  The exceptions are the shaders
// engineshader transcribes, which is why the vertex half is mirrored by pointer: the pointer is the
// only thing that says which file a bound shader came from.
void Direct3D11_Mirror_Pixel_Shader(const void * shader);
void Direct3D11_Mirror_Vertex_Shader(const void * shader);

// The engine loaded a shader out of the big archives and the device made it.  Registering it here
// is what lets a later bind name it; a shader that is never registered is foreign, which is the
// state everything was in before this existed.
void Direct3D11_Register_Engine_Shader(const void * shader, const char * file_path);

// The float4 register bank the engine's own shaders read, mirrored as DX8Wrapper sets it.
void Direct3D11_Mirror_Vertex_Shader_Constant(unsigned first_register, const float * values,
	unsigned count);

// The Modern water's pixel registers, c0 to the last hull packet (DX11_WATER_REGISTERS), which
// W3DWater sets for its own draws and nothing else reads; and what its draws did, for the log.
void Direct3D11_Set_Water_Constants(unsigned first_register, const float * values, unsigned count);
std::string Direct3D11_Water_Report();

// The draw, made alongside the Direct3D 9 one rather than instead of it: phase 2 is finished when
// the D3D11 picture can replace the D3D9 one, and until then both have to be able to produce it.
// False means the backend could not resolve a pipeline for this state and drew nothing, which is
// counted and reported rather than asserted.
bool Direct3D11_Draw_Indexed_Triangles(unsigned index_count, unsigned start_index,
	unsigned base_vertex);

// The same draw over a triangle strip, which is the topology both water grids are indexed for.
bool Direct3D11_Draw_Indexed_Strip(unsigned index_count, unsigned start_index,
	unsigned base_vertex);

// How many twins the run made and what they cost in video memory, counted as they are created.  A
// -dx11 run that mirrors nothing draws exactly like one that mirrors everything until the draws
// move over, so the count is the only thing that says the buffers went across.
void Direct3D11_Twin_Statistics(unsigned & buffers_made, unsigned long long & bytes_made);

// The same for textures: how many were copied, how many binds reused a copy, and how many could
// not be copied.
void Direct3D11_Texture_Statistics(unsigned & textures_made, unsigned & textures_reused,
	unsigned & textures_refused);
const char * Direct3D11_Texture_First_Refusal();

// One line per sixteen bit colour texture copied, naming its size and the average colour of its top
// level.  The terrain atlas is one of these, and its average is what says whether the copy holds
// the ground.
unsigned Direct3D11_Texture_Note_Count();
const char * Direct3D11_Texture_Note(unsigned index);

// The texture shapes copied most often, most first.  Count before reading the lines.
unsigned Direct3D11_Texture_Copy_Shape_Count();
const char * Direct3D11_Texture_Copy_Shape(unsigned index);

// What the run did, for the log: how many pipelines were built and how many draws the backend
// refused.  A backend that refuses most of the draws looks like a renderer with a lot missing and
// says nothing about it otherwise.
void Direct3D11_Statistics(unsigned & pipelines_built, unsigned long long & draws_made,
	unsigned long long & draws_refused);

// How many compiled programs came with the game (dx11shaders.shipped) and how many the backend holds now,
// shipped, cached and compiled together: a run whose held count exceeds the shipped one compiled something.
// Of those compiled, how many the worker built off the frame, the longest one from queue to landing, and
// the draws left out while they were built.
void Direct3D11_Program_Statistics(unsigned & shipped, unsigned & held, unsigned & off_frame,
	double & longest_milliseconds, unsigned long long & draws_waited);

// What the frame since the last call spent building pipelines and copying textures, and how many
// of each, how many times the soft particles copied the scene's depth, and how many constant blocks
// were written and how many of those writes renamed their buffer.  Taking it resets it.
void Direct3D11_Take_Frame_Cost(double & pipeline_milliseconds, unsigned & pipelines,
	double & texture_milliseconds, unsigned & textures, unsigned & depth_copies,
	unsigned & constant_writes, unsigned & constant_renames);

// One line for the log once a second, null in between: the frame rate, the GPU's own time for a
// presented frame (average and worst, split into shadow map, world, and post chain with interface),
// the GPU's idle gap between two frames, busy as GPU time over wall time, how many frames were read
// back and how many were dropped, the time the top of the frame waited for the swap chain's queue,
// and the constant writes and renames a second.  Timestamp queries read back a few frames late and
// never waited on, so it costs the frame nothing it can measure.  Taking it resets it.
const char * Direct3D11_Take_GPU_Report();

// The swap chain holds its queue with a wait at the top of the frame rather than inside Present.
bool Direct3D11_Has_Frame_Wait();

// How the backend writes its constant blocks (DX11BackendClass::Constant_Path), for the shutdown
// report.  Empty with no backend.
const char * Direct3D11_Constant_Path();

// The refusals split by cause: no buffer bound, no texture stage enabled, a vertex format with no
// input layout, and a program that could not be generated or compiled.
void Direct3D11_Refusals(unsigned long long & no_buffer, unsigned long long & no_stage,
	unsigned long long & no_layout, unsigned long long & no_program,
	unsigned long long & no_object, unsigned long long & foreign_shader,
	unsigned long long & no_texture);

// The distinct refused states, as the keys the backend cached them under, and the first thing the
// shader compiler objected to.  Both are strings for the log and nothing else reads them.
unsigned Direct3D11_Refused_Description_Count();
const char * Direct3D11_Refused_Description(unsigned index);

// The same listing for the pipelines that did draw, with the draws each took and the size of the
// texture bound at stage zero the first time it was taken.
// How many times a texture was bound as the render target, and how many draws landed in one.
void Direct3D11_Target_Statistics(unsigned long long & bound, unsigned long long & restored,
	unsigned long long & draws);

// A screen-space quad handed over as vertices rather than as a buffer.  Returns false when the
// backend refused it, the way the other draw mirrors do.
bool Direct3D11_Draw_User_Strip(const void * vertices, unsigned primitive_count, unsigned stride);

// One line about the first screen-space quad drawn: where it was, and what was in the texture it
// sampled.  Empty when none was drawn.
const char * Direct3D11_Diagnostic();

// The first few target changes in order, each with the draw count when it happened.
unsigned Direct3D11_Target_Trace_Count();
const char * Direct3D11_Target_Trace(unsigned index);

unsigned Direct3D11_Pipeline_Report_Count();
const char * Direct3D11_Pipeline_Report(unsigned index);

// One line per shipped shader the refused draws had bound, with what each cost.  This is the list
// of what is still to be transcribed, and nothing else in the run names them.
unsigned Direct3D11_Foreign_Report_Count();
const char * Direct3D11_Foreign_Report(unsigned index);
const char * Direct3D11_First_Compiler_Error();

#endif // DX11RUNTIME_H
