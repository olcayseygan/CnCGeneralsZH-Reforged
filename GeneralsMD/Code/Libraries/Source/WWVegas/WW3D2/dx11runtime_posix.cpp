/*
**	Copyright 2026 İlyas Akın
**	Additional terms under GNU GPL section 7 apply: see LICENSE.md.
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
// Portions adapted from GeneralsMD/Code/Libraries/Source/WWVegas/WW3D2/dx11runtime.cpp by Olcay Seygan (upstream CnCGeneralsZH-Reforged), GPL-3.0-or-later.

// dx11runtime.h off Windows (decision 7, phase A1): there is no Direct3D 11, so every call answers as
// dx11runtime.cpp does on a Windows machine where Direct3D11_Create failed.  Nothing is ever active:
// the mirrors do nothing, the draws are refused (the D3D9-shaped device draws instead), the counts
// are zero and the reports are the same empty strings or the same "off" lines.  What was asked for
// is still remembered, as it is there, so a -dx11 on the command line reads back as asked for and
// not granted.  (The other requests, normal maps and present, only ever read back combined with
// being active, so they are not kept.)

#include "dx11runtime.h"
#include "dx11twin.h"
#include "Platform/EngineShaderName.h"

#include <stddef.h>

#if defined(_WIN32)
#error "dx11runtime_posix.cpp is for builds without Direct3D 11; Windows builds dx11runtime.cpp"
#endif

static bool Requested = false;

void Direct3D11_Enable(bool enabled) { Requested = enabled; }
bool Direct3D11_Is_Enabled() { return Requested; }
bool Direct3D11_Create(RenderWindow, unsigned, unsigned) { return false; }
void Direct3D11_Release() {}
void Direct3D11_Set_Sample_Count(unsigned) {}
unsigned Direct3D11_Sample_Count() { return 0; }
bool Direct3D11_Resize(unsigned, unsigned) { return false; }
bool Direct3D11_Is_Active() { return false; }
DX11DeviceClass * Direct3D11_Device() { return NULL; }
DX11BackendClass * Direct3D11_Backend() { return NULL; }

void Direct3D11_Mirror_Render_State(unsigned, unsigned) {}
void Direct3D11_Mirror_Texture_Stage_State(unsigned, unsigned, unsigned) {}
void Direct3D11_Mirror_Sampler_State(unsigned, unsigned, unsigned) {}
void Direct3D11_Mirror_Transform(unsigned, const float [16]) {}
void Direct3D11_Mirror_Material(const float [4], const float [4], const float [4], const float [4], float) {}
void Direct3D11_Mirror_Light(unsigned, unsigned, const float [4], const float [4], const float [4],
	const float [4], const float [4], const float [4], const float [4]) {}
void Direct3D11_Mirror_Light_Disabled(unsigned) {}

DX11BufferTwinClass * Direct3D11_Twin_Vertex_Buffer(unsigned, bool) { return NULL; }
DX11BufferTwinClass * Direct3D11_Twin_Index_Buffer(unsigned, bool) { return NULL; }
void Direct3D11_Mirror_Texture(unsigned, struct IDirect3DBaseTexture9 *) {}
void Direct3D11_Set_Haze_Shroud(struct IDirect3DBaseTexture9 *, const float [4]) {}
void Direct3D11_Set_Atmosphere(const float [3], const float [3], float, float, float, float, float, float, struct IDirect3DBaseTexture9 *) {}
void Direct3D11_Atmosphere_Sky() {}

void Direct3D11_Set_Headlights(const float *, unsigned, const float [3]) {}
void Direct3D11_Set_Blast_Lights(const float *, unsigned) {}
bool Direct3D11_Lights_Per_Pixel() { return false; }
void Direct3D11_Set_Soft_Particles(bool) {}
void Direct3D11_Take_Scene_Depth() {}
void Direct3D11_Allow_Soft_Particles(bool) {}

bool Direct3D11_Begin_Shadow_Map(unsigned) { return false; }
void Direct3D11_End_Shadow_Map() {}
bool Direct3D11_Shadow_Map_Bound() { return false; }
std::string Direct3D11_Shadow_Map_Report() { return std::string("no Direct3D 11 backend"); }
void Direct3D11_Set_Shadow_Parameters(float, float, float, float, float, float, float) {}
void Direct3D11_Clear_Shadow_Parameters() {}
bool Direct3D11_Fill_Smoke_Map(const float *, unsigned, float) { return false; }
bool Direct3D11_Upload_Shadow_Casters(const float * const *, const unsigned *, unsigned, const float * const *, unsigned) { return false; }
bool Direct3D11_Draw_Shadow_Casters(const DX11ShadowCasterRun *, unsigned) { return false; }
bool Direct3D11_Upload_Shadow_Skin(const DX11ShadowSkinVertex *, unsigned, unsigned &) { return false; }
unsigned Direct3D11_Shadow_Skin_Generation() { return 0; }
void Direct3D11_Set_Scene_View(const float [16]) {}
void Direct3D11_Set_Smoke_Glow(bool) {}
void Direct3D11_Set_Fire_Look(bool) {}
void Direct3D11_Set_Emissive(bool) {}
void Direct3D11_Set_Team_Glow(bool) {}
void Direct3D11_Set_Hemi_Ambient(bool) {}
void Direct3D11_Set_Draw_Effect(unsigned, const float [4]) {}
bool Direct3D11_Draw_Effects() { return false; }
void Direct3D11_Allow_Draw_Effects(bool) {}
bool Direct3D11_Takes_Draw_Effect(unsigned) { return false; }
unsigned Direct3D11_Requested_Draw_Effect(float parameters[4])
{
	parameters[0] = parameters[1] = parameters[2] = parameters[3] = 0.0f;
	return 0;
}
void Direct3D11_Occlude_Scene() {}
void Direct3D11_Ambient_Occlusion() {}
void Direct3D11_Set_Glow_Draw(bool) {}
void Direct3D11_Set_Scene_Stand_In(struct IDirect3DBaseTexture9 *, struct IDirect3DSurface9 *) {}

void Direct3D11_Mark_Surface_Dirty(struct IDirect3DSurface9 *) {}
void Direct3D11_Mirror_Render_Target(struct IDirect3DSurface9 *) {}
void Direct3D11_Mirror_Surface_Copy(struct IDirect3DSurface9 *, struct IDirect3DSurface9 *,
	const RenderRect *, const RenderPoint *) {}

void Direct3D11_Begin_Scene() {}
void Direct3D11_Mirror_Clear(bool, bool, float, float, float, float) {}
void Direct3D11_End_Scene(bool) {}
void Direct3D11_Dump_Programs_To(const char *) {}
void Direct3D11_Set_Shader_Cache_Directory(const char *) {}
void Direct3D11_Present_Enable(bool) {}
bool Direct3D11_Present_Is_Enabled() { return false; }
bool Direct3D11_Can_Tear() { return false; }
bool Direct3D11_Take_Present_Failure(long &) { return false; }
long Direct3D11_Device_Removed_Reason() { return 0; }
void Direct3D11_Set_VSync(bool) {}
// Windows answers whether it understood the chain's text.  There is no post-process here to
// understand it for, so every chain is refused.
bool Direct3D11_Post_Chain(const char *) { return false; }
const char * Direct3D11_Post_Diagnostic() { return "post-process off"; }
void Direct3D11_Finish_Scene() {}
void Direct3D11_Finish_Frame() {}

unsigned char * Direct3D11_Capture_Back_Buffer(unsigned & width, unsigned & height, unsigned & pitch)
{
	width = 0;
	height = 0;
	pitch = 0;
	return NULL;
}
void Direct3D11_Release_Capture(unsigned char * pixels) { delete [] pixels; }

bool Direct3D11_Queue_Frame_Copy(unsigned) { return false; }
const unsigned char * Direct3D11_Map_Frame_Copy(unsigned, unsigned & width, unsigned & height, unsigned & pitch)
{
	width = 0;
	height = 0;
	pitch = 0;
	return NULL;
}
void Direct3D11_Unmap_Frame_Copy(unsigned) {}
void Direct3D11_Release_Frame_Copies() {}

void Direct3D11_Mirror_Stream_Source(DX11BufferTwinClass *, unsigned, unsigned) {}
void Direct3D11_Mirror_Indices(DX11BufferTwinClass *) {}
void Direct3D11_Mirror_Vertex_Format(unsigned) {}
void Direct3D11_Mirror_Pixel_Shader(const void *) {}
void Direct3D11_Mirror_Vertex_Shader(const void *) {}
// Not a Direct3D 11 call off Windows but the POSIX device's: it draws the engine's shaders from D3's
// transcriptions by the name they were registered under, as the Direct3D 11 backend does (A3e).
void Direct3D11_Register_Engine_Shader(const void * shader, const char * name) { PosixDevice_Name_Shader(shader, name); }
void Direct3D11_Mirror_Vertex_Shader_Constant(unsigned, const float *, unsigned) {}
void Direct3D11_Set_Water_Constants(unsigned, const float *, unsigned) {}
std::string Direct3D11_Water_Report() { return std::string("no Direct3D 11 backend"); }
bool Direct3D11_Draw_Indexed_Triangles(unsigned, unsigned, unsigned) { return false; }
bool Direct3D11_Draw_Indexed_Strip(unsigned, unsigned, unsigned) { return false; }
bool Direct3D11_Draw_User_Strip(const void *, unsigned, unsigned) { return false; }

void Direct3D11_Twin_Statistics(unsigned & buffers_made, unsigned long long & bytes_made)
{
	buffers_made = 0;
	bytes_made = 0;
}
void Direct3D11_Texture_Statistics(unsigned & textures_made, unsigned & textures_reused,
	unsigned & textures_refused)
{
	textures_made = 0;
	textures_reused = 0;
	textures_refused = 0;
}
const char * Direct3D11_Texture_First_Refusal() { return ""; }
unsigned Direct3D11_Texture_Note_Count() { return 0; }
const char * Direct3D11_Texture_Note(unsigned) { return ""; }
unsigned Direct3D11_Texture_Copy_Shape_Count() { return 0; }
const char * Direct3D11_Texture_Copy_Shape(unsigned) { return ""; }
void Direct3D11_Statistics(unsigned & pipelines_built, unsigned long long & draws_made,
	unsigned long long & draws_refused)
{
	pipelines_built = 0;
	draws_made = 0;
	draws_refused = 0;
}

void Direct3D11_Program_Statistics(unsigned & shipped, unsigned & held, unsigned & off_frame,
	double & longest_milliseconds, unsigned long long & draws_waited)
{
	shipped = 0;
	held = 0;
	off_frame = 0;
	longest_milliseconds = 0.0;
	draws_waited = 0;
}
void Direct3D11_Take_Frame_Cost(double & pipeline_milliseconds, unsigned & pipelines,
	double & texture_milliseconds, unsigned & textures, unsigned & depth_copies,
	unsigned & constant_writes, unsigned & constant_renames)
{
	pipeline_milliseconds = 0.0;
	pipelines = 0;
	texture_milliseconds = 0.0;
	textures = 0;
	depth_copies = 0;
	constant_writes = 0;
	constant_renames = 0;
}
const char * Direct3D11_Take_GPU_Report() { return NULL; }
bool Direct3D11_Has_Frame_Wait() { return false; }
const char * Direct3D11_Constant_Path() { return ""; }
void Direct3D11_Refusals(unsigned long long & no_buffer, unsigned long long & no_stage,
	unsigned long long & no_layout, unsigned long long & no_program,
	unsigned long long & no_object, unsigned long long & foreign_shader,
	unsigned long long & no_texture)
{
	no_buffer = 0;
	no_stage = 0;
	no_layout = 0;
	no_program = 0;
	no_object = 0;
	foreign_shader = 0;
	no_texture = 0;
}
unsigned Direct3D11_Refused_Description_Count() { return 0; }
const char * Direct3D11_Refused_Description(unsigned) { return ""; }
void Direct3D11_Target_Statistics(unsigned long long & bound, unsigned long long & restored,
	unsigned long long & draws)
{
	bound = 0;
	restored = 0;
	draws = 0;
}
const char * Direct3D11_Diagnostic() { return ""; }
unsigned Direct3D11_Target_Trace_Count() { return 0; }
const char * Direct3D11_Target_Trace(unsigned) { return ""; }
unsigned Direct3D11_Pipeline_Report_Count() { return 0; }
const char * Direct3D11_Pipeline_Report(unsigned) { return ""; }
unsigned Direct3D11_Foreign_Report_Count() { return 0; }
const char * Direct3D11_Foreign_Report(unsigned) { return ""; }
const char * Direct3D11_First_Compiler_Error() { return ""; }

// The buffer twins W3DProjectedShadow locks directly.  None is ever made off Windows (the two
// Direct3D11_Twin_ calls above return null), so every lock has none, and does what dx11twin.cpp's does
// for a null twin: hands back nothing, so the caller writes its Direct3D 9 copy, and ends doing nothing.
DX11BufferTwinClass::~DX11BufferTwinClass() {}

DX11BufferLockClass::DX11BufferLockClass() :
	Twin(NULL),
	D3D9Memory(NULL),
	ByteOffset(0),
	ByteCount(0),
	Discard(false),
	CopyToD3D9(true),
	Mapped(false)
{
}

void * DX11BufferLockClass::Begin(DX11BufferTwinClass * twin, void *, unsigned, unsigned, unsigned, bool)
{
	Twin = twin;
	return NULL;
}

void DX11BufferLockClass::End() {}
