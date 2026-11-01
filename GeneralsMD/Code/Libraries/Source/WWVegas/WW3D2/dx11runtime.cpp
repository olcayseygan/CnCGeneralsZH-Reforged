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

#include "dx11runtime.h"
#include "Platform/SunShadow.h"

#include <map>
#include <stdio.h>
#include <string.h>
#include <string>

#include "dx11backend.h"
#include "dx11device.h"
#include "dx11post.h"
#include "dx11texture.h"
#include "dx11twin.h"

static bool Requested = false;
static bool PresentRequested = false;
// The first refused present of the session, handed out once so a frozen picture leaves one line.
static HRESULT PresentFailure = S_OK;
static bool PresentFailureReported = false;
static bool VSyncRequested = false;
static unsigned SamplesRequested = 1;
static bool Active = false;
static DX11DeviceClass Device;
static DX11BackendClass Backend;
static DX11PostProcessClass Post;
static unsigned TwinBuffers = 0;
static unsigned long long TwinBytes = 0;
static std::string DumpDirectory;

// The user's compiled programs: in the directory Direct3D11_Set_Shader_Cache_Directory names (the
// game's user data folder), or else the working directory, which for the game is Run/, next to the exe.
static const char * const SHADER_CACHE_FILE = "dx11shaders.cache";
static std::string ShaderCacheDirectory;
// The programs that ship with the game (DX11BackendClass::Load_Shipped_Programs), next to the exe.
static const char * const SHIPPED_SHADER_FILE = "dx11shaders.shipped";

// What -dx11post asked for, kept as the effects rather than as the text so a name nobody knows is
// refused when the switch is read and not once a frame.
static DX11PostEffect PostChain[DX11_POST_CHAIN_LIMIT];
static unsigned PostChainLength = 0;

// Which shipped shader each created Direct3D 9 shader came from.  Registered at load, read at every
// bind.  It outlives the shaders themselves, which cost the map an entry each and nothing else: the
// engine loads ten of them once and releases them when the game ends.
struct EngineShaderRecord
{
	EngineShaderProgram Program;
	std::string FileName;
};
static std::map<const void *, EngineShaderRecord> EngineShaders;

static const EngineShaderRecord & engine_shader_record(const void * shader)
{
	static const EngineShaderRecord UNKNOWN = { ENGINE_SHADER_NONE, "an unregistered shader" };

	std::map<const void *, EngineShaderRecord>::const_iterator entry = EngineShaders.find(shader);
	return (entry != EngineShaders.end()) ? entry->second : UNKNOWN;
}

// The GPU's own clock over every presented frame, read back without ever waiting for it.  A ring of
// GPU_TIMING_SLOTS disjoint brackets, each around four timestamps: the frame's first Begin_Scene since
// the last flip (so the water's reflection pass, which comes before the shadow map, counts as
// shadow), the end of the sun's depth map, the entry to Finish_Scene (the world is drawn, the post
// chain and the interface are not) and the last thing before Present.  Once a frame the oldest closed
// bracket is asked for its answer with DONOTFLUSH; one still unanswered when its slot comes round
// again is dropped, and so is one the driver says the clock jumped in.  Five queries a frame, cheap
// enough for every build; Direct3D11_Take_GPU_Report sums them.
enum GpuStamp { GPU_FRAME_BEGIN, GPU_SHADOW_END, GPU_SCENE_END, GPU_FRAME_END, GPU_STAMPS };
enum { GPU_TIMING_SLOTS = 4 };

struct GpuTimingSlot
{
	ID3D11Query * Clock;					// D3D11_QUERY_TIMESTAMP_DISJOINT around the stamps
	ID3D11Query * Stamps[GPU_STAMPS];
	bool Issued[GPU_STAMPS];
	bool Pending;							// closed, not read back yet
	unsigned Frame;
};
static GpuTimingSlot GpuSlots[GPU_TIMING_SLOTS];
static bool GpuTiming = false;
static bool FrameOpen = false;				// a Begin_Scene since the last flip
static unsigned FrameNumber = 0;			// flips so far; frame n writes slot n % GPU_TIMING_SLOTS

// What the frames read back since the last report add up to.
struct GpuTotals
{
	unsigned Frames;
	unsigned Samples;
	unsigned DroppedBusy;
	unsigned DroppedDisjoint;
	unsigned BubblePairs;
	unsigned WaitTimeouts;
	unsigned long long ConstantWrites;
	unsigned long long ConstantRenames;
	double FrameMs;
	double FrameMaxMs;
	double ShadowMs;
	double SceneMs;
	double PostMs;
	double BubbleMs;
	double WaitMs;
};
static GpuTotals GpuWindow;
static LARGE_INTEGER GpuWindowStart;
// The last frame read back, for the gap between its end and the next one's start.
static bool HaveLastGpuFrame = false;
static unsigned LastGpuFrame = 0;
static UINT64 LastGpuFrameEnd = 0;

static const double GPU_REPORT_INTERVAL_MS = 1000.0;
// The longest the top of a frame waits for room in the swap chain's queue.  Two queued frames at
// 20 Hz fit in it; anything past it is a queue that will not drain (a window the compositor stopped
// taking frames from), and the frame goes ahead rather than holding the logic with it.
static const unsigned FRAME_WAIT_LIMIT_MS = 100;

static double elapsed_ms(const LARGE_INTEGER & from, const LARGE_INTEGER & to)
{
	static LARGE_INTEGER frequency = { 0 };
	if (frequency.QuadPart == 0) {
		QueryPerformanceFrequency(&frequency);
	}
	return (double)(to.QuadPart - from.QuadPart) * 1000.0 / (double)frequency.QuadPart;
}

static void release_query(ID3D11Query *& query)
{
	if (query != NULL) {
		query->Release();
		query = NULL;
	}
}

static void release_gpu_timing()
{
	for (unsigned slot = 0; slot < GPU_TIMING_SLOTS; ++slot) {
		release_query(GpuSlots[slot].Clock);
		for (unsigned stamp = 0; stamp < GPU_STAMPS; ++stamp) {
			release_query(GpuSlots[slot].Stamps[stamp]);
		}
		GpuSlots[slot].Pending = false;
	}
	GpuTiming = false;
	FrameOpen = false;
	HaveLastGpuFrame = false;
}

// A driver that refuses a timestamp query leaves the frame untimed and nothing else.
static void create_gpu_timing()
{
	ID3D11Device * device = Device.Get_Device();
	const D3D11_QUERY_DESC clock = { D3D11_QUERY_TIMESTAMP_DISJOINT, 0 };
	const D3D11_QUERY_DESC stamp = { D3D11_QUERY_TIMESTAMP, 0 };
	GpuTiming = true;
	for (unsigned slot = 0; slot < GPU_TIMING_SLOTS && GpuTiming; ++slot) {
		GpuTiming = SUCCEEDED(device->CreateQuery(&clock, &GpuSlots[slot].Clock));
		for (unsigned index = 0; index < GPU_STAMPS && GpuTiming; ++index) {
			GpuTiming = SUCCEEDED(device->CreateQuery(&stamp, &GpuSlots[slot].Stamps[index]));
		}
	}
	if (!GpuTiming) {
		release_gpu_timing();
	}
}

// The oldest closed bracket, if the GPU has answered it.  Never waits and never flushes: an answer
// that is not there yet is asked for again next frame.
static void read_oldest_gpu_frame()
{
	ID3D11DeviceContext * context = Device.Get_Context();
	for (unsigned age = GPU_TIMING_SLOTS; age > 0; --age) {
		GpuTimingSlot & slot = GpuSlots[(FrameNumber - age) % GPU_TIMING_SLOTS];
		if (!slot.Pending) {
			continue;
		}
		D3D11_QUERY_DATA_TIMESTAMP_DISJOINT clock;
		if (context->GetData(slot.Clock, &clock, sizeof(clock), D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK) {
			return;
		}
		UINT64 stamps[GPU_STAMPS];
		for (unsigned index = 0; index < GPU_STAMPS; ++index) {
			if (slot.Issued[index] && context->GetData(slot.Stamps[index], &stamps[index],
					sizeof(stamps[index]), D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK) {
				return;
			}
		}
		slot.Pending = false;
		if (clock.Disjoint) {
			GpuWindow.DroppedDisjoint++;
			HaveLastGpuFrame = false;
			return;
		}
		// A frame with no shadow map spent nothing on one, and a frame that never reached
		// Finish_Scene (a menu, a loading screen) was scene to the end.
		if (!slot.Issued[GPU_SHADOW_END]) {
			stamps[GPU_SHADOW_END] = stamps[GPU_FRAME_BEGIN];
		}
		if (!slot.Issued[GPU_SCENE_END]) {
			stamps[GPU_SCENE_END] = stamps[GPU_FRAME_END];
		}
		const double ms_per_tick = 1000.0 / (double)clock.Frequency;
		const double frame_ms = (double)(stamps[GPU_FRAME_END] - stamps[GPU_FRAME_BEGIN]) * ms_per_tick;
		GpuWindow.Samples++;
		GpuWindow.FrameMs += frame_ms;
		if (frame_ms > GpuWindow.FrameMaxMs) {
			GpuWindow.FrameMaxMs = frame_ms;
		}
		GpuWindow.ShadowMs += (double)(stamps[GPU_SHADOW_END] - stamps[GPU_FRAME_BEGIN]) * ms_per_tick;
		GpuWindow.SceneMs += (double)(stamps[GPU_SCENE_END] - stamps[GPU_SHADOW_END]) * ms_per_tick;
		GpuWindow.PostMs += (double)(stamps[GPU_FRAME_END] - stamps[GPU_SCENE_END]) * ms_per_tick;
		// The GPU sat idle from the last frame's end to this one's start: the frame it had to wait for.
		if (HaveLastGpuFrame && slot.Frame == LastGpuFrame + 1 && stamps[GPU_FRAME_BEGIN] >= LastGpuFrameEnd) {
			GpuWindow.BubbleMs += (double)(stamps[GPU_FRAME_BEGIN] - LastGpuFrameEnd) * ms_per_tick;
			GpuWindow.BubblePairs++;
		}
		HaveLastGpuFrame = true;
		LastGpuFrame = slot.Frame;
		LastGpuFrameEnd = stamps[GPU_FRAME_END];
		return;
	}
}

static void stamp_gpu_frame(GpuStamp stamp)
{
	if (!GpuTiming || !FrameOpen) {
		return;
	}
	GpuTimingSlot & slot = GpuSlots[FrameNumber % GPU_TIMING_SLOTS];
	// The first of each per frame: a second view (the observer's split) draws its own shadow map and
	// finishes its own scene, and those count with what follows them.
	if (!slot.Issued[stamp]) {
		Device.Get_Context()->End(slot.Stamps[stamp]);
		slot.Issued[stamp] = true;
	}
}

// The top of a presented frame, its first Begin_Scene since the last flip.  The swap chain's queue is
// waited on here, before the frame is built, and the GPU's clock starts.
static void begin_frame()
{
	if (PresentRequested) {
		LARGE_INTEGER before;
		LARGE_INTEGER after;
		QueryPerformanceCounter(&before);
		if (!Device.Wait_For_Frame(FRAME_WAIT_LIMIT_MS)) {
			GpuWindow.WaitTimeouts++;
		}
		QueryPerformanceCounter(&after);
		GpuWindow.WaitMs += elapsed_ms(before, after);
	}
	FrameOpen = true;
	if (!GpuTiming) {
		return;
	}
	read_oldest_gpu_frame();
	GpuTimingSlot & slot = GpuSlots[FrameNumber % GPU_TIMING_SLOTS];
	if (slot.Pending) {
		slot.Pending = false;
		GpuWindow.DroppedBusy++;
	}
	ID3D11DeviceContext * context = Device.Get_Context();
	context->Begin(slot.Clock);
	context->End(slot.Stamps[GPU_FRAME_BEGIN]);
	for (unsigned index = 0; index < GPU_STAMPS; ++index) {
		slot.Issued[index] = (index == GPU_FRAME_BEGIN);
	}
	slot.Frame = FrameNumber;
}

// The flip: the last stamp and the bracket close, and the next Begin_Scene opens a new frame.
static void end_frame()
{
	if (GpuTiming && FrameOpen) {
		stamp_gpu_frame(GPU_FRAME_END);
		GpuTimingSlot & slot = GpuSlots[FrameNumber % GPU_TIMING_SLOTS];
		Device.Get_Context()->End(slot.Clock);
		slot.Pending = true;
	}
	FrameOpen = false;
	FrameNumber++;
	GpuWindow.Frames++;
}

const char * Direct3D11_Take_GPU_Report()
{
	if (!Active) {
		return NULL;
	}
	LARGE_INTEGER now;
	QueryPerformanceCounter(&now);
	if (GpuWindowStart.QuadPart == 0) {
		GpuWindowStart = now;
		return NULL;
	}
	const double wall_ms = elapsed_ms(GpuWindowStart, now);
	if (wall_ms < GPU_REPORT_INTERVAL_MS) {
		return NULL;
	}

	const GpuTotals & window = GpuWindow;
	const double samples = (window.Samples > 0) ? (double)window.Samples : 1.0;
	const double frames = (window.Frames > 0) ? (double)window.Frames : 1.0;
	const double seconds = wall_ms / 1000.0;
	static char line[512];
	snprintf(line, sizeof(line),
		"%.1f fps | gpu frame avg %.2f max %.2f ms | shadow %.2f scene %.2f post+ui %.2f ms | bubble %.2f ms"
		" | busy %.0f%% | %u samples, dropped %u busy %u disjoint | latency wait %.2f ms a frame, %u timed out"
		" | %.0f constant writes/s, %.0f renames/s",
		window.Frames / seconds, window.FrameMs / samples, window.FrameMaxMs,
		window.ShadowMs / samples, window.SceneMs / samples, window.PostMs / samples,
		(window.BubblePairs > 0) ? window.BubbleMs / window.BubblePairs : 0.0,
		100.0 * window.FrameMs / wall_ms, window.Samples, window.DroppedBusy, window.DroppedDisjoint,
		window.WaitMs / frames, window.WaitTimeouts,
		(double)window.ConstantWrites / seconds, (double)window.ConstantRenames / seconds);
	memset(&GpuWindow, 0, sizeof(GpuWindow));
	GpuWindowStart = now;
	return line;
}

bool Direct3D11_Has_Frame_Wait()
{
	return Active && Device.Has_Frame_Wait();
}

const char * Direct3D11_Constant_Path()
{
	return Active ? Backend.Constant_Path() : "";
}

void Direct3D11_Enable(bool enabled)
{
	Requested = enabled;
}

bool Direct3D11_Is_Enabled()
{
	return Requested;
}

bool Direct3D11_Create(HWND window, unsigned width, unsigned height)
{
	if (!Requested || Active) {
		return Active;
	}

	Device.Request_Sample_Count(SamplesRequested);
	if (!Device.Create(window, width, height)) {
		return false;
	}
	if (!Backend.Initialise(&Device)) {
		Device.Release();
		return false;
	}

	Backend.Set_Dump_Directory(DumpDirectory.c_str());
	std::string cache_path = ShaderCacheDirectory;
	if (!cache_path.empty() && cache_path[cache_path.size() - 1] != '\\' && cache_path[cache_path.size() - 1] != '/') {
		cache_path += '\\';
	}
	cache_path += SHADER_CACHE_FILE;
	Backend.Set_Shader_Cache_Path(cache_path.c_str());
	Backend.Load_Shipped_Programs(SHIPPED_SHADER_FILE);
	Backend.Set_Compile_Off_Frame(true);
	if (Device.Get_Sample_Count() > 1 && !Backend.Make_Depth_Resolve()) {
		Device.Drop_Multisampling();
	}

	// The chain is the one thing here that is allowed to fail without taking the backend with it:
	// a machine whose compiler refuses the passes still gets the frame, just not the effect.
	if (Post.Initialise(&Device)) {
		Post.Set_Chain(PostChain, PostChainLength);
	}

	create_gpu_timing();
	Active = true;
	return true;
}

void Direct3D11_Release()
{
	if (!Active) {
		return;
	}
	release_gpu_timing();
	Post.Shutdown();
	Device.Set_Scene_View(NULL);
	Backend.Shutdown();
	Direct3D11_Release_Frame_Copies();
	Device.Release();
	Active = false;
}

void Direct3D11_Set_Sample_Count(unsigned samples)
{
	SamplesRequested = samples;
}

unsigned Direct3D11_Sample_Count()
{
	return (Active && Device.Get_Sample_Count() > 1) ? Device.Get_Sample_Count() : 0;
}

bool Direct3D11_Resize(unsigned width, unsigned height)
{
	if (!Active) {
		return false;
	}
	if (Device.Get_Width() == width && Device.Get_Height() == height) {
		return true;
	}
	return Device.Resize(width, height);
}

bool Direct3D11_Is_Active()
{
	return Active;
}

DX11DeviceClass * Direct3D11_Device()
{
	return Active ? &Device : NULL;
}

DX11BackendClass * Direct3D11_Backend()
{
	return Active ? &Backend : NULL;
}

void Direct3D11_Mirror_Render_State(unsigned state, unsigned value)
{
	if (Active) {
		Backend.Set_Render_State(static_cast<D3DRENDERSTATETYPE>(state), value);
	}
}

void Direct3D11_Mirror_Texture_Stage_State(unsigned stage, unsigned state, unsigned value)
{
	if (Active) {
		Backend.Set_Texture_Stage_State(stage, static_cast<D3DTEXTURESTAGESTATETYPE>(state), value);
	}
}

void Direct3D11_Mirror_Sampler_State(unsigned sampler, unsigned state, unsigned value)
{
	if (Active) {
		Backend.Set_Sampler_State(sampler, static_cast<D3DSAMPLERSTATETYPE>(state), value);
	}
}

void Direct3D11_Mirror_Transform(unsigned transform, const float matrix[16])
{
	if (Active) {
		Backend.Set_Transform(static_cast<D3DTRANSFORMSTATETYPE>(transform), matrix);
	}
}

void Direct3D11_Mirror_Material(const float ambient[4], const float diffuse[4],
	const float specular[4], const float emissive[4], float power)
{
	if (Active) {
		Backend.Set_Material(ambient, diffuse, specular, emissive, power);
	}
}

void Direct3D11_Mirror_Light(unsigned index, unsigned type, const float position[4],
	const float direction[4], const float diffuse[4], const float specular[4],
	const float attenuation[4], const float spot[4], const float ambient[4])
{
	if (Active) {
		Backend.Set_Light(index, type, position, direction, diffuse, specular, attenuation, spot, ambient);
	}
}

void Direct3D11_Mirror_Light_Disabled(unsigned index)
{
	if (Active) {
		Backend.Disable_Light(index);
	}
}

static DX11BufferTwinClass * count_twin(DX11BufferTwinClass * twin, unsigned byte_count)
{
	if (twin != NULL) {
		TwinBuffers++;
		TwinBytes += byte_count;
	}
	return twin;
}

DX11BufferTwinClass * Direct3D11_Twin_Vertex_Buffer(unsigned byte_count, bool dynamic)
{
	if (!Active) {
		return NULL;
	}
	return count_twin(
		DX11Twin_Create_Vertex_Buffer(Device.Get_Device(), Device.Get_Context(), byte_count,
			dynamic),
		byte_count);
}

DX11BufferTwinClass * Direct3D11_Twin_Index_Buffer(unsigned byte_count, bool dynamic)
{
	if (!Active) {
		return NULL;
	}
	return count_twin(
		DX11Twin_Create_Index_Buffer(Device.Get_Device(), Device.Get_Context(), byte_count,
			dynamic),
		byte_count);
}

void Direct3D11_Mark_Surface_Dirty(struct IDirect3DSurface9 * surface)
{
	if (!Active || surface == NULL) {
		return;
	}
	DX11Texture_Mark_Dirty(surface);
}

// Direct3D11_Set_Scene_Stand_In's texture and surface.
static struct IDirect3DBaseTexture9 * SceneStandInTexture = NULL;
static struct IDirect3DSurface9 * SceneStandInSurface = NULL;

void Direct3D11_Mirror_Texture(unsigned stage, struct IDirect3DBaseTexture9 * texture)
{
	if (!Active) {
		return;
	}
	ID3D11ShaderResourceView * view = texture == NULL
		? NULL
		: DX11Texture_Mirror(Device.Get_Device(), Device.Get_Context(), texture);
	if (texture != NULL && texture == SceneStandInTexture) {
		Backend.Set_Scene_Stand_In(view);
	}
	Backend.Set_Texture(stage, view);
	Backend.Set_Texture_Missing(stage, texture != NULL && view == NULL);
}

void Direct3D11_Set_Haze_Shroud(struct IDirect3DBaseTexture9 * texture, const float rect[4])
{
	if (!Active) {
		return;
	}
	DX11Post_Set_Haze_Shroud(texture == NULL
		? NULL
		: DX11Texture_Mirror(Device.Get_Device(), Device.Get_Context(), texture), rect);
}

void Direct3D11_Set_Atmosphere(const float horizon[3], const float tint[3], float start, float scale,
	float strength, float ground, float map_width, float map_height, struct IDirect3DBaseTexture9 * sky)
{
	if (!Active) {
		return;
	}
	DX11Post_Set_Atmosphere(horizon, tint, start, scale, strength, ground, map_width, map_height, sky == NULL
		? NULL
		: DX11Texture_Mirror(Device.Get_Device(), Device.Get_Context(), sky));
}

void Direct3D11_Set_Headlights(const float * lights, unsigned count, const float gain[3])
{
	if (Active) {
		Backend.Set_Headlights(lights, count, gain);
	}
}

void Direct3D11_Set_Blast_Lights(const float * lights, unsigned count)
{
	if (Active) {
		Backend.Set_Blast_Lights(lights, count);
	}
}

bool Direct3D11_Lights_Per_Pixel()
{
	return Active && Backend.Lights_Per_Pixel();
}

void Direct3D11_Set_Soft_Particles(bool soft)
{
	if (Active) {
		Backend.Set_Soft_Particles(soft);
	}
}

void Direct3D11_Take_Scene_Depth()
{
	if (Active) {
		Backend.Take_Scene_Depth();
	}
}

void Direct3D11_Allow_Soft_Particles(bool allowed)
{
	Backend.Allow_Soft_Particles(allowed);
}

bool Direct3D11_Begin_Shadow_Map(unsigned size)
{
	return Active ? Backend.Begin_Shadow_Map(size) : false;
}

void Direct3D11_End_Shadow_Map()
{
	if (Active) {
		Backend.End_Shadow_Map();
		stamp_gpu_frame(GPU_SHADOW_END);
	}
}

bool Direct3D11_Shadow_Map_Bound()
{
	return Active ? Backend.Shadow_Map_Bound() : false;
}

void Direct3D11_Set_Shadow_Parameters(float bias, float strength, float widest_radius_in_texels,
	float narrowest_radius_in_texels, float texels_per_unit_of_gap, float units_per_unit_of_depth,
	float sky_fill)
{
	if (Active) {
		Backend.Set_Shadow_Parameters(bias, strength, widest_radius_in_texels,
			narrowest_radius_in_texels, texels_per_unit_of_gap, units_per_unit_of_depth, sky_fill);
	}
}

bool Direct3D11_Fill_Smoke_Map(const float * casters, unsigned count, float strength)
{
	return Active ? Backend.Fill_Smoke_Map(casters, count, strength) : false;
}

bool Direct3D11_Upload_Shadow_Casters(const float * const * worlds, const unsigned * first_bones,
	unsigned instance_count, const float * const * bones, unsigned bone_count)
{
	return Active && Backend.Upload_Shadow_Casters(worlds, first_bones, instance_count, bones, bone_count);
}

bool Direct3D11_Upload_Shadow_Skin(const DX11ShadowSkinVertex * vertices, unsigned count,
	unsigned & first_vertex)
{
	return Active && Backend.Upload_Shadow_Skin(
		reinterpret_cast<const DX11BackendClass::ShadowSkinVertex *>(vertices), count, first_vertex);
}

unsigned Direct3D11_Shadow_Skin_Generation()
{
	return Backend.Shadow_Skin_Generation();
}

bool Direct3D11_Draw_Shadow_Casters(const DX11ShadowCasterRun * runs, unsigned run_count)
{
	if (!Active) {
		return false;
	}
	static std::vector<DX11BackendClass::ShadowCasterRun> backend_runs;
	backend_runs.resize(run_count);
	for (unsigned index = 0; index < run_count; ++index) {
		const DX11ShadowCasterRun & run = runs[index];
		DX11BackendClass::ShadowCasterRun & backend_run = backend_runs[index];
		backend_run.Kind = static_cast<DX11BackendClass::ShadowCasterKind>(run.Kind);
		backend_run.Vertices = (run.Vertices != NULL) ? run.Vertices->Buffer() : NULL;
		backend_run.Stride = run.Stride;
		backend_run.TextureOffset = run.TextureOffset;
		backend_run.Indices = run.Indices->Buffer();
		backend_run.StartIndex = run.StartIndex;
		backend_run.IndexCount = run.IndexCount;
		backend_run.BaseVertex = run.BaseVertex;
		backend_run.Strip = run.Strip;
		backend_run.CullMode = run.CullMode;
		backend_run.DepthFunction = run.DepthFunction;
		backend_run.FirstInstance = run.FirstInstance;
		backend_run.InstanceCount = run.InstanceCount;
	}
	return Backend.Draw_Shadow_Casters(run_count ? &backend_runs[0] : NULL, run_count);
}

void Direct3D11_Set_Scene_View(const float view[16])
{
	if (Active) {
		Backend.Set_Scene_View(view);
	}
}

void Direct3D11_Set_Smoke_Glow(bool glow)
{
	if (Active) {
		Backend.Set_Smoke_Glow(glow);
	}
}

void Direct3D11_Set_Fire_Look(bool fire)
{
	if (Active) {
		Backend.Set_Fire_Look(fire);
	}
}

void Direct3D11_Set_Emissive(bool emissive)
{
	if (Active) {
		Backend.Set_Emissive(emissive);
	}
}

void Direct3D11_Set_Team_Glow(bool glow)
{
	if (Active) {
		Backend.Set_Team_Glow(glow);
	}
}

void Direct3D11_Set_Hemi_Ambient(bool hemi)
{
	if (Active) {
		Backend.Set_Hemi_Ambient(hemi);
	}
}

static bool DrawEffectsAllowed = true;

void Direct3D11_Set_Draw_Effect(unsigned effect, const float parameters[4])
{
	if (Active && DrawEffectsAllowed) {
		Backend.Set_Draw_Effect(effect, parameters);
	}
}

bool Direct3D11_Draw_Effects()
{
	return Active && DrawEffectsAllowed;
}

bool Direct3D11_Takes_Draw_Effect(unsigned effect)
{
	return Active && DrawEffectsAllowed && Backend.Takes_Draw_Effect(effect);
}

unsigned Direct3D11_Requested_Draw_Effect(float parameters[4])
{
	if (Active && DrawEffectsAllowed) {
		return Backend.Requested_Draw_Effect(parameters);
	}
	parameters[0] = parameters[1] = parameters[2] = parameters[3] = 0.0f;
	return 0;
}

void Direct3D11_Occlude_Scene()
{
	if (!Active || !Backend.Draws_Into_Scene()) {
		return;
	}
	Backend.Resolve_Scene_Depth();
	bool drew = Post.Occlude_Scene(Device.Get_Depth_Texture());
	// The prototype ground haze goes on at the same moment, over the occlusion and under the fire.
	if (Post.Wants_Haze_Program()) {
		std::vector<unsigned char> code;
		if (Backend.Compile_Pixel_Program(Post.Haze_Program_Source(), "dx11haze", code) && !code.empty()) {
			Post.Load_Haze_Program(&code[0], code.size());
		} else {
			Post.Load_Haze_Program(NULL, 0);
		}
	}
	float world_from_clip[16];
	if (Backend.Scene_World_From_Clip(world_from_clip)) {
		drew = Post.Haze_Scene(Device.Get_Depth_Texture(), world_from_clip) || drew;
		drew = Post.Atmosphere_Scene(Device.Get_Depth_Texture(), world_from_clip) || drew;
	}
	if (drew) {
		Backend.Resume_Scene();
	}
}

void Direct3D11_Atmosphere_Sky()
{
	if (!Active || !DrawEffectsAllowed || !Backend.Draws_Into_Scene()) {
		return;
	}
	if (Post.Wants_Atmosphere_Programs()) {
		for (unsigned index = 0; index < DX11PostProcessClass::ATMOSPHERE_PROGRAMS; ++index) {
			std::vector<unsigned char> code;
			if (Backend.Compile_Pixel_Program(Post.Atmosphere_Program_Source(index), "dx11atmosphere", code) && !code.empty()) {
				Post.Load_Atmosphere_Program(index, &code[0], code.size());
			} else {
				Post.Load_Atmosphere_Program(index, NULL, 0);
			}
		}
	}
	float world_from_clip[16];
	if (Backend.Scene_World_From_Clip(world_from_clip) && Post.Sky_Scene(world_from_clip)) {
		Backend.Resume_Scene();
	}
}

void Direct3D11_Ambient_Occlusion()
{
	if (!Active || !DrawEffectsAllowed || !Backend.Draws_Into_Scene()) {
		return;
	}
	if (Post.Wants_World_Ao_Programs()) {
		for (unsigned index = 0; index < DX11PostProcessClass::WORLD_AO_PROGRAMS; ++index) {
			std::vector<unsigned char> code;
			if (Backend.Compile_Pixel_Program(Post.World_Ao_Program_Source(index), "dx11worldao", code) && !code.empty()) {
				Post.Load_World_Ao_Program(index, &code[0], code.size());
			} else {
				Post.Load_World_Ao_Program(index, NULL, 0);
			}
		}
	}
	float world_from_clip[16];
	if (!Backend.Scene_World_From_Clip(world_from_clip)) {
		return;
	}
	Backend.Resolve_Scene_Depth();
	float projection_x = 0.0f;
	float projection_y = 0.0f;
	Backend.Scene_Projection_Scale(projection_x, projection_y);
	if (Post.World_Occlude_Scene(Device.Get_Depth_Texture(), world_from_clip, projection_x, projection_y)) {
		Backend.Resume_Scene();
	}
}

void Direct3D11_Allow_Draw_Effects(bool allowed)
{
	DrawEffectsAllowed = allowed;
}

void Direct3D11_Set_Glow_Draw(bool glow)
{
	if (Active) {
		Backend.Set_Glow_Draw(glow);
	}
}

void Direct3D11_Set_Scene_Stand_In(struct IDirect3DBaseTexture9 * texture,
	struct IDirect3DSurface9 * surface)
{
	SceneStandInTexture = texture;
	SceneStandInSurface = surface;
	if (Active && texture == NULL) {
		Backend.Set_Scene_Stand_In(NULL);
	}
}

void Direct3D11_Clear_Shadow_Parameters()
{
	if (Active) {
		Backend.Clear_Shadow_Parameters();
	}
}

std::string Direct3D11_Shadow_Map_Report()
{
	return Active ? Backend.Shadow_Map_Report() : std::string("no Direct3D 11 backend");
}

void Direct3D11_Mirror_Render_Target(struct IDirect3DSurface9 * surface)
{
	if (!Active) {
		return;
	}
	// The scene's stand-in is not drawn into here: the scene stays where the gain and the bloom are.
	Backend.Set_Render_Target((surface != NULL && surface == SceneStandInSurface)
		? NULL
		: DX11Texture_Target(Device.Get_Device(), Device.Get_Context(), surface));
}

void Direct3D11_Mirror_Surface_Copy(struct IDirect3DSurface9 * destination,
	struct IDirect3DSurface9 * source, const struct tagRECT * source_rectangle,
	const struct tagPOINT * destination_point)
{
	if (!Active) {
		return;
	}
	DX11Texture_Update(Device.Get_Device(), Device.Get_Context(), destination, source,
		source_rectangle, destination_point);
}

void Direct3D11_Texture_Statistics(unsigned & textures_made, unsigned & textures_reused,
	unsigned & textures_refused)
{
	DX11Texture_Statistics(textures_made, textures_reused, textures_refused);
}

const char * Direct3D11_Texture_First_Refusal()
{
	return DX11Texture_First_Refusal();
}

unsigned Direct3D11_Texture_Note_Count()
{
	return DX11Texture_Note_Count();
}

const char * Direct3D11_Texture_Note(unsigned index)
{
	return DX11Texture_Note(index);
}

unsigned Direct3D11_Texture_Copy_Shape_Count()
{
	return DX11Texture_Copy_Shape_Count();
}

const char * Direct3D11_Texture_Copy_Shape(unsigned index)
{
	return DX11Texture_Copy_Shape(index);
}

void Direct3D11_Set_Shader_Cache_Directory(const char * directory)
{
	ShaderCacheDirectory = (directory == NULL) ? "" : directory;
}

void Direct3D11_Dump_Programs_To(const char * directory)
{
	DumpDirectory = directory == NULL ? "" : directory;
	if (Active) {
		Backend.Set_Dump_Directory(DumpDirectory.c_str());
	}
}

void Direct3D11_Present_Enable(bool enabled)
{
	PresentRequested = enabled;
}

void Direct3D11_Set_VSync(bool enabled)
{
	VSyncRequested = enabled;
}

bool Direct3D11_Present_Is_Enabled()
{
	return PresentRequested && Active;
}

bool Direct3D11_Can_Tear()
{
	return Active && Device.Can_Tear();
}

bool Direct3D11_Post_Chain(const char * chain)
{
	const bool understood = DX11Post_Parse_Chain(chain, PostChain, PostChainLength);
	if (Active) {
		Post.Set_Chain(PostChain, PostChainLength);
	}
	return understood;
}

const char * Direct3D11_Post_Diagnostic()
{
	return Active ? Post.Diagnostic_Line() : "post-process off";
}

// Whatever put the frame in front of the player, the swap chain is where everything after it has to
// be drawn.  Rebinding through the backend rather than by hand is what gets the depth buffer and the
// scene's own viewport offset back at the same time.
static void take_the_frame_to_the_screen()
{
	Device.Set_Scene_View(NULL);
	Backend.Set_Additive_Gain(1.0f);	// eight bits from here on: a boosted interface draw would only clip
	Backend.Begin_Scene();
}

// The chain binds its own shaders, states and views on the context the backend's draws use.
void Direct3D11_Finish_Scene()
{
	if (Active) {
		stamp_gpu_frame(GPU_SCENE_END);
		// The chain's occlusion and whatever tests against the depth over the interface read it
		// single-sampled.
		Backend.Resolve_Scene_Depth();
	}
	if (Active && Post.Run_Chain()) {
		take_the_frame_to_the_screen();
	}
	Backend.Forget_Bindings();
}

void Direct3D11_Finish_Frame()
{
	if (Active && Post.Copy_Through()) {
		take_the_frame_to_the_screen();
	}
	Backend.Forget_Bindings();
}

void Direct3D11_Begin_Scene()
{
	if (Active) {
		// A frame draws the water's reflection and then itself, a Begin_Scene each, and only the
		// first of them is the top of the frame.
		if (!FrameOpen) {
			begin_frame();
			Post.Open_Frame();
			Backend.Open_Frame();
		}
		// Before the backend binds anything: the scene goes wherever this says it goes, and a frame
		// that asked for a chain and could not have one falls back to the swap chain here rather
		// than half way through.
		Post.Begin_Frame();
		ID3D11RenderTargetView * scene = Post.Scene_View();
		Device.Set_Scene_View(scene);
		// The Glow option's boost needs the half float scene: in eight bits it would only clip.
		Backend.Set_Additive_Gain((scene != NULL && Post.Scene_Is_Float())
			? DX11Post_Additive_Gain() : 1.0f);
		Backend.Begin_Scene();
	}
}

void Direct3D11_Mirror_Clear(bool colour, bool depth, float red, float green, float blue,
	float alpha)
{
	if (Active) {
		const float value[4] = { red, green, blue, alpha };
		Backend.Clear(colour, depth, value);
	}
}

// A texture the CPU can read that the back buffer copies into whole.
static D3D11_TEXTURE2D_DESC staging_description_of(ID3D11Resource * back_buffer)
{
	D3D11_TEXTURE2D_DESC description;
	((ID3D11Texture2D *)back_buffer)->GetDesc(&description);
	description.Usage = D3D11_USAGE_STAGING;
	description.BindFlags = 0;
	description.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
	description.MiscFlags = 0;
	// A multisampled back buffer cannot be copied into a single-sample staging texture, and this
	// swap chain never asks for one, so the copy is the whole of it.
	description.SampleDesc.Count = 1;
	description.SampleDesc.Quality = 0;
	return description;
}

unsigned char * Direct3D11_Capture_Back_Buffer(unsigned & width, unsigned & height,
	unsigned & pitch)
{
	width = 0;
	height = 0;
	pitch = 0;
	if (!Active || Device.Get_Back_Buffer_View() == NULL) {
		return NULL;
	}

	// A screenshot is of what the player sees, so the chain runs first.  The present path asks for
	// the same thing a moment later and gets it for nothing, because the second Resolve of a frame
	// does not run.
	Direct3D11_Finish_Frame();

	ID3D11Resource * back_buffer = NULL;
	Device.Get_Back_Buffer_View()->GetResource(&back_buffer);

	const D3D11_TEXTURE2D_DESC description = staging_description_of(back_buffer);
	ID3D11Texture2D * staging = NULL;
	if (FAILED(Device.Get_Device()->CreateTexture2D(&description, NULL, &staging))) {
		back_buffer->Release();
		return NULL;
	}

	Device.Get_Context()->CopyResource(staging, back_buffer);

	D3D11_MAPPED_SUBRESOURCE mapped;
	unsigned char * pixels = NULL;
	if (SUCCEEDED(Device.Get_Context()->Map(staging, 0, D3D11_MAP_READ, 0, &mapped))) {
		width = description.Width;
		height = description.Height;
		pitch = description.Width * 4;
		pixels = new unsigned char[pitch * height];
		for (unsigned row = 0; row < height; ++row) {
			memcpy(pixels + row * pitch,
				(const unsigned char *)mapped.pData + row * mapped.RowPitch, pitch);
		}
		Device.Get_Context()->Unmap(staging, 0);
	}

	staging->Release();
	back_buffer->Release();
	return pixels;
}

void Direct3D11_Release_Capture(unsigned char * pixels)
{
	delete [] pixels;
}

static ID3D11Texture2D * FrameCopies[DX11_FRAME_COPY_SLOTS];

bool Direct3D11_Queue_Frame_Copy(unsigned slot)
{
	if (!Active || Device.Get_Back_Buffer_View() == NULL) {
		return false;
	}

	// what the player sees, the way a screenshot takes it
	Direct3D11_Finish_Frame();

	ID3D11Resource * back_buffer = NULL;
	Device.Get_Back_Buffer_View()->GetResource(&back_buffer);
	const D3D11_TEXTURE2D_DESC description = staging_description_of(back_buffer);

	ID3D11Texture2D *& copy = FrameCopies[slot];
	if (copy != NULL) {
		D3D11_TEXTURE2D_DESC held;
		copy->GetDesc(&held);
		if (held.Width != description.Width || held.Height != description.Height
				|| held.Format != description.Format) {
			copy->Release();
			copy = NULL;
		}
	}
	if (copy == NULL && FAILED(Device.Get_Device()->CreateTexture2D(&description, NULL, &copy))) {
		copy = NULL;
		back_buffer->Release();
		return false;
	}

	Device.Get_Context()->CopyResource(copy, back_buffer);
	back_buffer->Release();
	return true;
}

const unsigned char * Direct3D11_Map_Frame_Copy(unsigned slot, unsigned & width, unsigned & height,
	unsigned & pitch)
{
	width = 0;
	height = 0;
	pitch = 0;
	ID3D11Texture2D * copy = FrameCopies[slot];
	D3D11_MAPPED_SUBRESOURCE mapped;
	if (!Active || copy == NULL || FAILED(Device.Get_Context()->Map(copy, 0, D3D11_MAP_READ, 0, &mapped))) {
		return NULL;
	}

	D3D11_TEXTURE2D_DESC description;
	copy->GetDesc(&description);
	width = description.Width;
	height = description.Height;
	pitch = mapped.RowPitch;
	return (const unsigned char *)mapped.pData;
}

void Direct3D11_Unmap_Frame_Copy(unsigned slot)
{
	Device.Get_Context()->Unmap(FrameCopies[slot], 0);
}

void Direct3D11_Release_Frame_Copies()
{
	for (unsigned slot = 0; slot < DX11_FRAME_COPY_SLOTS; ++slot) {
		if (FrameCopies[slot] != NULL) {
			FrameCopies[slot]->Release();
			FrameCopies[slot] = NULL;
		}
	}
}

void Direct3D11_End_Scene(bool flip_frames)
{
	if (!Active || !flip_frames) {
		return;
	}
	if (PresentRequested) {
		Direct3D11_Finish_Frame();
	}
	end_frame();
	if (PresentRequested && !Device.Present(VSyncRequested ? 1 : 0) && PresentFailure == S_OK) {
		PresentFailure = Device.Last_Present_Result();
	}
	Backend.Save_Shader_Cache_If_Due();
}

bool Direct3D11_Take_Present_Failure(long & result)
{
	if (PresentFailure == S_OK || PresentFailureReported) {
		return false;
	}
	PresentFailureReported = true;
	result = PresentFailure;
	return true;
}

long Direct3D11_Device_Removed_Reason()
{
	if (!Active || SUCCEEDED(Device.Last_Present_Result())) {
		return S_OK;
	}
	return Device.Device_Removed_Reason();
}

void Direct3D11_Mirror_Stream_Source(DX11BufferTwinClass * twin, unsigned stride, unsigned offset)
{
	if (Active) {
		Backend.Set_Stream_Source(twin ? twin->Buffer() : NULL, stride, offset);
	}
}

void Direct3D11_Mirror_Indices(DX11BufferTwinClass * twin)
{
	if (Active) {
		Backend.Set_Indices(twin ? twin->Buffer() : NULL, DXGI_FORMAT_R16_UINT);
	}
}

void Direct3D11_Mirror_Vertex_Format(unsigned fvf)
{
	if (Active) {
		Backend.Set_Vertex_Format(fvf);
	}
}

void Direct3D11_Mirror_Pixel_Shader(const void * shader)
{
	if (Active) {
		const EngineShaderRecord & record = engine_shader_record(shader);
		Backend.Set_Pixel_Program(record.Program, shader != NULL, record.FileName.c_str());
	}
}

void Direct3D11_Register_Engine_Shader(const void * shader, const char * file_path)
{
	if (shader == NULL || file_path == NULL) {
		return;
	}

	// The whole path, not the file name alone, so a report line reads the way the loader's own
	// argument does and can be grepped for as it stands.
	EngineShaderRecord record;
	record.Program = EngineShader_From_File(file_path);
	record.FileName = file_path;
	EngineShaders[shader] = record;
}

void Direct3D11_Mirror_Vertex_Shader(const void * shader)
{
	if (Active) {
		const EngineShaderRecord & record = engine_shader_record(shader);
		Backend.Set_Vertex_Program(record.Program, shader != NULL, record.FileName.c_str());
	}
}

void Direct3D11_Mirror_Vertex_Shader_Constant(unsigned first_register, const float * values,
	unsigned count)
{
	if (Active) {
		Backend.Set_Vertex_Program_Constant(first_register, values, count);
	}
}

void Direct3D11_Set_Water_Constants(unsigned first_register, const float * values, unsigned count)
{
	if (Active) {
		Backend.Set_Water_Constants(first_register, values, count);
	}
}

std::string Direct3D11_Water_Report()
{
	return Active ? Backend.Water_Report() : std::string("no Direct3D 11 backend");
}

bool Direct3D11_Draw_Indexed_Triangles(unsigned index_count, unsigned start_index,
	unsigned base_vertex)
{
	if (!Active) {
		return false;
	}
	return Backend.Draw_Indexed_Triangles(index_count, start_index, base_vertex);
}

bool Direct3D11_Draw_Indexed_Strip(unsigned index_count, unsigned start_index,
	unsigned base_vertex)
{
	if (!Active) {
		return false;
	}
	return Backend.Draw_Indexed_Strip(index_count, start_index, base_vertex);
}

bool Direct3D11_Draw_User_Strip(const void * vertices, unsigned primitive_count, unsigned stride)
{
	if (!Active) {
		return false;
	}
	return Backend.Draw_User_Strip(vertices, primitive_count, stride);
}

void Direct3D11_Twin_Statistics(unsigned & buffers_made, unsigned long long & bytes_made)
{
	buffers_made = TwinBuffers;
	bytes_made = TwinBytes;
}

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
	if (Active) {
		Backend.Refusals(no_buffer, no_stage, no_layout, no_program, no_object, foreign_shader,
			no_texture);
	}
}

unsigned Direct3D11_Refused_Description_Count()
{
	return Active ? Backend.Refused_Description_Count() : 0;
}

const char * Direct3D11_Refused_Description(unsigned index)
{
	return Active ? Backend.Refused_Description(index) : "";
}

void Direct3D11_Target_Statistics(unsigned long long & bound, unsigned long long & restored,
	unsigned long long & draws)
{
	bound = 0;
	restored = 0;
	draws = 0;
	if (Active) {
		Backend.Target_Statistics(bound, restored, draws);
	}
}

const char * Direct3D11_Diagnostic()
{
	return Active ? Backend.Diagnostic_Line() : "";
}

unsigned Direct3D11_Target_Trace_Count()
{
	return Active ? Backend.Target_Trace_Count() : 0;
}

const char * Direct3D11_Target_Trace(unsigned index)
{
	return Active ? Backend.Target_Trace(index) : "";
}

unsigned Direct3D11_Pipeline_Report_Count()
{
	return Active ? Backend.Pipeline_Report_Count() : 0;
}

const char * Direct3D11_Pipeline_Report(unsigned index)
{
	return Active ? Backend.Pipeline_Report(index) : "";
}

unsigned Direct3D11_Foreign_Report_Count()
{
	return Active ? Backend.Foreign_Report_Count() : 0;
}

const char * Direct3D11_Foreign_Report(unsigned index)
{
	return Active ? Backend.Foreign_Report(index) : "";
}

const char * Direct3D11_First_Compiler_Error()
{
	return Active ? Backend.First_Compiler_Error() : "";
}

void Direct3D11_Statistics(unsigned & pipelines_built, unsigned long long & draws_made,
	unsigned long long & draws_refused)
{
	pipelines_built = 0;
	draws_made = 0;
	draws_refused = 0;
	if (Active) {
		Backend.Statistics(pipelines_built, draws_made, draws_refused);
	}
}

void Direct3D11_Program_Statistics(unsigned & shipped, unsigned & held, unsigned & off_frame,
	double & longest_milliseconds, unsigned long long & draws_waited)
{
	shipped = Active ? Backend.Shipped_Program_Count() : 0;
	held = Active ? Backend.Compiled_Program_Count() : 0;
	Backend.Compile_Statistics(off_frame, longest_milliseconds, draws_waited);
}

void Direct3D11_Take_Frame_Cost(double & pipeline_milliseconds, unsigned & pipelines,
	double & texture_milliseconds, unsigned & textures, unsigned & depth_copies,
	unsigned & constant_writes, unsigned & constant_renames)
{
	pipeline_milliseconds = 0.0;
	pipelines = 0;
	depth_copies = 0;
	constant_writes = 0;
	constant_renames = 0;
	if (Active) {
		Backend.Take_Frame_Build_Cost(pipeline_milliseconds, pipelines);
		depth_copies = Backend.Take_Frame_Depth_Copies();
		Backend.Take_Frame_Constant_Writes(constant_writes, constant_renames);
		// the frame cost line prints only past its threshold; the once a second line takes every frame
		GpuWindow.ConstantWrites += constant_writes;
		GpuWindow.ConstantRenames += constant_renames;
	}
	DX11Texture_Take_Frame_Cost(texture_milliseconds, textures);
}

// Shadow-only dispatch; does not change per-pixel lighting capability.
namespace SunShadow {
bool FillSmoke(IDirect3DDevice9 *,const float *c,unsigned n,float s) { return Direct3D11_Fill_Smoke_Map(c,n,s); }
bool CasterEffects(IDirect3DDevice9 *) { return Direct3D11_Draw_Effects(); }
void SetCasterEffect(IDirect3DDevice9 *,unsigned e,const float p[4]) { Direct3D11_Set_Draw_Effect(e,p); }
CasterEffect GetCasterEffect(IDirect3DDevice9 *) {
    CasterEffect effect;
    // Queued caster coverage is Linux-only; preserve Windows sorting behavior.
    return effect;
}
void SetSmokeGlow(IDirect3DDevice9 *,bool g) { Direct3D11_Set_Smoke_Glow(g); }
static bool receiving = false;
bool Begin(IDirect3DDevice9 *, unsigned size) { return Direct3D11_Begin_Shadow_Map(size); }
bool End(IDirect3DDevice9 *) {
    const bool bound = Direct3D11_Shadow_Map_Bound();
    Direct3D11_End_Shadow_Map();
    return bound;
}
// The pass's start only hands the frame back to the volumes, as theShadowMapHoldsTheFrame did.
void Clear(IDirect3DDevice9 *) { receiving = false; }
bool Parameters(IDirect3DDevice9 *, float bias, float strength, float widest, float narrowest,
    float texelsPerGap, float unitsPerDepth, float skyFill) {
    if (!Direct3D11_Is_Active()) return false;
    Direct3D11_Set_Shadow_Parameters(bias,strength,widest,narrowest,texelsPerGap,unitsPerDepth,skyFill);
    receiving = true;
    return true;
}
bool ReceivesView(IDirect3DDevice9 *) { return receiving && Direct3D11_Is_Active(); }
bool OwnsSceneShadows(IDirect3DDevice9 *d) { return ReceivesView(d); }
bool EnterScene(IDirect3DDevice9 *,bool eligible) { return eligible; }
void LeaveScene(IDirect3DDevice9 *,bool) {}
void ExcludeReceivers(IDirect3DDevice9 *, bool) {} // SDL phase-one exclusions; existing D3D11 behavior unchanged.
bool ReceiversExcluded(IDirect3DDevice9 *) { return false; }
std::string Report(IDirect3DDevice9 *) { return Direct3D11_Shadow_Map_Report(); }
}
