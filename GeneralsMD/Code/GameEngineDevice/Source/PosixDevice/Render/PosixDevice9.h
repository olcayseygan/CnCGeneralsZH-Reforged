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

/*
** The Direct3D 9-shaped device off Windows (decision 7): what d3d9.dll is on Windows, behind the same
** interfaces (Platform/D3D9Posix.h), so WW3D2 and W3DDevice keep every D3D9 call they make.
**
** The device's files, and what each holds:
**   - this header; PosixDirect3D9.cpp (the adapter, its modes, CreateDevice and Reset);
**     PosixDevice9.cpp (the device's state, bindings, scenes, presentation, shaders and draws);
**   - phase A2: PosixDevice9Resources.cpp (every resource-creating and surface method, Clear,
**     and Create_Implicit_Surfaces); PosixD3D9Caps.cpp (the caps, the Check* answers and the adapter
**     identifier); the resource classes, in files of their own.
**
** No window is a real case, not an error: -headless starts no video, so the device is made with a null
** RenderWindow and must work as Windows' -headless device on its hidden window does - resources and
** state for real, and draws that are never seen.  So with no window a draw succeeds and does nothing;
** with a window, a fixed-function draw is recorded on SDL3 GPU (A3c), and what A3c cannot draw yet is
** refused: counted by reason, logged once, and D3D_OK, as dx11backend does.
**
** Reference counting is COM's: every object is born with one reference, Release deletes at zero, a Get
** method AddRefs what it hands out, and binding an object (SetTexture, SetStreamSource, ...) holds a
** reference until something else is bound or the device goes.  Counts are atomic: WW3D2's texture
** loader thread creates textures while the main thread draws.
*/

#pragma once

#ifndef POSIXDEVICE9_H
#define POSIXDEVICE9_H

#include "Platform/RenderTypes.h"
#include "Platform/D3D9Posix.h"
#include "Platform/WaterShoreOverlay.h"
#include "Platform/WaterHullVisualLayout.h"
#include <memory>

#include <atomic>
#include <memory>
#include <map>
#include <string>
#include <vector>

class SdlGpuFrame;
class SdlPipelineCache;
class SdlProgramCache;
class SdlResourceMirrors;
class SdlSamplerCache;
struct CombinerDescription;
struct VertexPipelineDescription;
struct SdlVertexConstants;
struct SdlPixelConstants;
struct SdlWaterPixelConstants;
struct SdlWaterVisualPixelConstants;

/// AddRef and Release for one of the interfaces, counted as COM counts.
template <class Interface>
class PosixRefCounted : public Interface
{
public:
	uint32_t AddRef() override { return ++References; }
	uint32_t Release() override
	{
		const uint32_t left = --References;
		if (left == 0) {
			delete this;
		}
		return left;
	}

protected:
	PosixRefCounted() : References(1) {}
	virtual ~PosixRefCounted() {}

private:
	std::atomic<uint32_t> References;
};

/// Holds a reference to whatever it is given, dropping the one it held: a binding slot.
template <class Object>
inline void Posix_Bind(Object *&slot, Object *object)
{
	if (object != NULL) {
		object->AddRef();
	}
	if (slot != NULL) {
		slot->Release();
	}
	slot = object;
}

/// Hands out what a slot holds, with a reference of the caller's own, as D3D9's Get methods do.
template <class Object>
inline RenderResult Posix_Hand_Out(Object *slot, Object **out)
{
	if (out == NULL) {
		return D3DERR_INVALIDCALL;
	}
	*out = slot;
	if (slot != NULL) {
		slot->AddRef();
	}
	return D3D_OK;
}

//-------------------------------------------------------------------------------------------------
// The adapter: Direct3DCreate9's object.
//-------------------------------------------------------------------------------------------------

class PosixDirect3D9 : public PosixRefCounted<IDirect3D9>
{
public:
	PosixDirect3D9();

	unsigned int GetAdapterCount() override;
	RenderResult GetAdapterIdentifier(unsigned int adapter, RenderUInt32 flags, D3DADAPTER_IDENTIFIER9 *identifier) override;
	unsigned int GetAdapterModeCount(unsigned int adapter, D3DFORMAT format) override;
	RenderResult EnumAdapterModes(unsigned int adapter, D3DFORMAT format, unsigned int mode, D3DDISPLAYMODE *display_mode) override;
	RenderResult GetAdapterDisplayMode(unsigned int adapter, D3DDISPLAYMODE *mode) override;
	RenderResult CheckDeviceType(unsigned int adapter, D3DDEVTYPE type, D3DFORMAT display_format,
		D3DFORMAT back_buffer_format, int windowed) override;
	RenderResult CheckDeviceFormat(unsigned int adapter, D3DDEVTYPE type, D3DFORMAT adapter_format, RenderUInt32 usage,
		D3DRESOURCETYPE resource_type, D3DFORMAT check_format) override;
	RenderResult CheckDeviceMultiSampleType(unsigned int adapter, D3DDEVTYPE type, D3DFORMAT surface_format,
		int windowed, D3DMULTISAMPLE_TYPE multisample, RenderUInt32 *quality_levels) override;
	RenderResult CheckDepthStencilMatch(unsigned int adapter, D3DDEVTYPE type, D3DFORMAT adapter_format,
		D3DFORMAT render_target_format, D3DFORMAT depth_stencil_format) override;
	RenderResult GetDeviceCaps(unsigned int adapter, D3DDEVTYPE type, D3DCAPS9 *caps) override;
	RenderResult CreateDevice(unsigned int adapter, D3DDEVTYPE type, RenderWindow focus_window,
		RenderUInt32 behaviour_flags, D3DPRESENT_PARAMETERS *parameters, IDirect3DDevice9 **device) override;
};

//-------------------------------------------------------------------------------------------------
// The device.
//-------------------------------------------------------------------------------------------------

class PosixDevice9 : public PosixRefCounted<IDirect3DDevice9>
{
public:
	enum
	{
		RENDER_STATE_COUNT = 256,		// D3DRENDERSTATETYPE's values are all below this
		TEXTURE_STAGE_COUNT = 8,
		TEXTURE_STAGE_STATE_COUNT = 33,	// D3DTEXTURESTAGESTATETYPE's values are all below this
		SAMPLER_COUNT = 16,
		SAMPLER_STATE_COUNT = 14,		// D3DSAMPLERSTATETYPE's values are all below this
		TRANSFORM_COUNT = 512,			// D3DTS_WORLDMATRIX(255) is 511
		LIGHT_COUNT = 8,
		CLIP_PLANE_COUNT = 6,
		STREAM_COUNT = 16,
		RENDER_TARGET_COUNT = 4,
		VERTEX_SHADER_CONSTANT_COUNT = 256,
		// Existing water uses c0-c175; exact mesh metadata is c176-c183, c184-c191
		// remains spare, and c192-c895 supplies the three bounded fragment packet blocks.
		PIXEL_SHADER_CONSTANT_COUNT = WaterHullVisualLayout::PixelRegisterCount
	};

	PosixDevice9(PosixDirect3D9 *adapter, RenderWindow window, const D3DPRESENT_PARAMETERS &parameters);

	bool Fill_Sun_Smoke(const float *casters, unsigned count, float strength);
	void Set_Sun_Caster_Effect(unsigned effect, const float parameters[4]);
	void Get_Sun_Caster_Effect(unsigned &effect, float parameters[4]) const;
	void Set_Smoke_Glow(bool glow) { SmokeGlow = glow; }
	bool Begin_Sun_Shadow(unsigned size);
	// Optional submission status is independent of whether the shadow image can be used.
	bool End_Sun_Shadow(bool *submission_ok = NULL);
	bool Set_Sun_Shadow_Parameters(float bias, float strength, float widest, float narrowest,
		float texelsPerGap, float unitsPerDepth, float skyFill);
	bool Sun_Receives_View();
	bool Sun_Owns_Scene_Shadows();
	bool Enter_Sun_Scene(bool eligible);
	void Leave_Sun_Scene(bool eligible);
	void Exclude_Sun_Receivers(bool exclude);
	bool Sun_Receivers_Excluded() const { return SunExclusions != 0 || SunSceneExclusions != 0; }
	bool Sun_Scene_Excluded() const { return SunSceneExclusions != 0; }
	void Clear_Sun_Shadow();
	std::string Sun_Shadow_Report();
	bool Read_Sun_Shadow(std::vector<float> &depth);
	// Render-only explicit metadata: never infers .wak ownership from texture names.
	void Shore_Begin(bool enabled);
	void Shore_End();
	void Shore_Patch(const WaterShoreOverlay::Quad *patch);
	void Shore_Track(const WaterShoreOverlay::Quad *track);

	// What the resource code (PosixDevice9Resources.cpp) reads.
	const D3DPRESENT_PARAMETERS & Get_Present_Parameters() const { return Parameters; }
	RenderWindow Get_Window() const { return Window; }

	/// With a window, the SDL3 GPU frame the device draws and presents through (A3); without one, none.
	/// CreateDevice calls it; a window whose GPU device cannot be made fails the device, loudly.  A test
	/// asks for an offscreen frame on a device made without a window: it draws, and Present only flushes.
	RenderResult Create_Gpu_Frame(bool offscreen = false);
	/// -offscreen's presents, paced at hz a second (0: unpaced); SdlGpuFrame::Set_Offscreen_Presents.
	void Present_Offscreen(unsigned int hz);
	SdlGpuFrame * Get_Gpu() const { return Gpu; }
	SdlResourceMirrors * Get_Mirrors() const { return Mirrors; }
	/// Draws recorded on the GPU, and the draws refused, by reason.
	unsigned int Draws_Recorded() const { return DrawsRecorded; }
	const std::map<std::string, unsigned int> & Draw_Refusals() const { return DrawRefusals; }

	/// Clear with a window, where the back buffer's pixels are the GPU's (the A3 design's render-target
	/// seam): Clear calls this when Get_Gpu() is not null.  Each rectangle is cut to the viewport, as
	/// D3D9's are; one covering the whole target is a load operation, a smaller one a clear draw.  A
	/// colour clear of another render target is refused until A3d.
	RenderResult Gpu_Clear(RenderUInt32 count, const D3DRECT *rects, RenderUInt32 flags, D3DCOLOR color, float z,
		RenderUInt32 stencil);

	// ---- The render-target seam (A3d), agreed 2026-09-26.  With a window, the pixels of every
	// surface a render target or depth-stencil can be - the back buffer, the depth surfaces, CreateRenderTarget
	// surfaces and the levels of D3DUSAGE_RENDERTARGET textures - are the GPU's; A2's CPU image is stale
	// until downloaded.  The surface code (PosixDevice9Resources.cpp) calls these:
	//   - Clear: colour to the GPU when Gpu_Owns(RenderTargets[0]), depth and stencil when
	//     Gpu_Owns(DepthStencil), each decided on its own.
	//   - GetRenderTargetData, and LockRect on a GPU-owned surface: Gpu_Download first.
	//   - StretchRect with both sides GPU-owned: Gpu_StretchRect.  A GPU-owned source only: Gpu_Download it.
	//   - Any CPU write into a GPU-owned image that does not cover the whole image (StretchRect, UpdateSurface
	//     or UpdateTexture into it, a writing lock of part of it): Gpu_Download it first, so the version bump
	//     after the write uploads a whole image that is current everywhere.
	//   - GetFrontBufferData: Gpu_Download_Front.

	/// Whether the surface's pixels are the GPU's: with a GPU frame, for the surfaces listed above.
	bool Gpu_Owns(IDirect3DSurface9 *surface) const;
	/// Flushes the batch and reads the surface's GPU pixels back into its image with posixWriteFromBgra, which
	/// does not bump version().  D3DERR_INVALIDCALL for a depth surface or a surface the GPU does not own.
	RenderResult Gpu_Download(IDirect3DSurface9 *surface);
	/// The last presented frame into dest's image (GetFrontBufferData).  The frame keeps a copy of what each
	/// Present showed, so this is the presented picture whenever it is called, not the back buffer's
	/// current, half-drawn or cleared, state.
	RenderResult Gpu_Download_Front(IDirect3DSurface9 *dest);
	/// A GPU copy between two GPU-owned surfaces (StretchRect), recorded in order with the draws.  NULL
	/// rectangles are the whole surfaces.
	RenderResult Gpu_StretchRect(IDirect3DSurface9 *source, const RenderRect *source_rect, IDirect3DSurface9 *dest,
		const RenderRect *dest_rect, D3DTEXTUREFILTERTYPE filter);

	// ---- The draw's resolve (A3c, PosixDevice9Draw.cpp): the state as set, read the way dx11backend
	// reads it, into D3's generator descriptions and the constants their programs read.

	/// The texture stages, walked until one ends the cascade (Stage_Ends_Cascade).  Ended at stage 0 means
	/// no texturing, which D3D9 defines as the diffuse colour and alpha: one SELECTARG1(DIFFUSE) stage.
	void Build_Combiner_Description(CombinerDescription &description) const;
	/// COLOROP DISABLE, or a COLORARG1 of D3DTA_TEXTURE with no texture bound.
	bool Stage_Ends_Cascade(unsigned int stage) const;
	/// Lighting, material sources, the enabled lights packed down, fog and each stage's coordinates,
	/// with D3D9's own simplifications made first: no lighting for pretransformed vertices, and a
	/// material source naming a colour the vertex does not supply reads the material.  False, with the
	/// reason, for what the generator does not carry: more lights than it has, table fog, fog from the
	/// specular alpha.
	bool Build_Vertex_Description(VertexPipelineDescription &description, std::string *refusal = NULL) const;
	/// The two constant blocks, packed to match the descriptions above.
	void Build_Constants(SdlVertexConstants &vertex, SdlPixelConstants &pixel) const;
	void Build_Water_Constants(const SdlPixelConstants &pixel, SdlWaterPixelConstants &water) const;
	void Build_Water_Visual_Constants(const SdlWaterPixelConstants &water, SdlWaterVisualPixelConstants &visual) const;
	bool Water_Foam_History_Available() const;

	/// Makes the implicit back buffer (and depth surface, when the present parameters ask for one) from
	/// the present parameters, and binds them as render target 0 and the depth surface.  CreateDevice and
	/// Reset call it, with every implicit surface already released.  In PosixDevice9Resources.cpp.
	RenderResult Create_Implicit_Surfaces();

	RenderResult TestCooperativeLevel() override;
	unsigned int GetAvailableTextureMem() override;
	RenderResult EvictManagedResources() override;
	RenderResult GetDeviceCaps(D3DCAPS9 *caps) override;
	RenderResult GetDisplayMode(unsigned int swap_chain, D3DDISPLAYMODE *mode) override;
	RenderResult SetCursorProperties(unsigned int hotspot_x, unsigned int hotspot_y, IDirect3DSurface9 *bitmap) override;
	void SetCursorPosition(int x, int y, RenderUInt32 flags) override;
	int ShowCursor(int show) override;
	RenderResult CreateAdditionalSwapChain(D3DPRESENT_PARAMETERS *parameters, IDirect3DSwapChain9 **swap_chain) override;
	RenderResult Reset(D3DPRESENT_PARAMETERS *parameters) override;
	RenderResult Present(const RenderRect *source, const RenderRect *dest, RenderWindow override_window,
		const void *dirty_region) override;
	RenderResult GetBackBuffer(unsigned int swap_chain, unsigned int index, D3DBACKBUFFER_TYPE type,
		IDirect3DSurface9 **surface) override;
	void SetGammaRamp(unsigned int swap_chain, RenderUInt32 flags, const D3DGAMMARAMP *ramp) override;

	// Resources and surfaces (PosixDevice9Resources.cpp).
	RenderResult CreateTexture(unsigned int width, unsigned int height, unsigned int levels, RenderUInt32 usage,
		D3DFORMAT format, D3DPOOL pool, IDirect3DTexture9 **texture, void **shared) override;
	RenderResult CreateVolumeTexture(unsigned int width, unsigned int height, unsigned int depth, unsigned int levels,
		RenderUInt32 usage, D3DFORMAT format, D3DPOOL pool, IDirect3DVolumeTexture9 **texture, void **shared) override;
	RenderResult CreateCubeTexture(unsigned int edge, unsigned int levels, RenderUInt32 usage, D3DFORMAT format,
		D3DPOOL pool, IDirect3DCubeTexture9 **texture, void **shared) override;
	RenderResult CreateVertexBuffer(unsigned int length, RenderUInt32 usage, RenderUInt32 fvf, D3DPOOL pool,
		IDirect3DVertexBuffer9 **buffer, void **shared) override;
	RenderResult CreateIndexBuffer(unsigned int length, RenderUInt32 usage, D3DFORMAT format, D3DPOOL pool,
		IDirect3DIndexBuffer9 **buffer, void **shared) override;
	RenderResult CreateRenderTarget(unsigned int width, unsigned int height, D3DFORMAT format,
		D3DMULTISAMPLE_TYPE multisample, RenderUInt32 quality, int lockable, IDirect3DSurface9 **surface, void **shared) override;
	RenderResult CreateDepthStencilSurface(unsigned int width, unsigned int height, D3DFORMAT format,
		D3DMULTISAMPLE_TYPE multisample, RenderUInt32 quality, int discard, IDirect3DSurface9 **surface, void **shared) override;
	RenderResult UpdateSurface(IDirect3DSurface9 *source, const RenderRect *source_rect, IDirect3DSurface9 *dest,
		const RenderPoint *dest_point) override;
	RenderResult UpdateTexture(IDirect3DBaseTexture9 *source, IDirect3DBaseTexture9 *dest) override;
	RenderResult GetRenderTargetData(IDirect3DSurface9 *render_target, IDirect3DSurface9 *dest) override;
	RenderResult GetFrontBufferData(unsigned int swap_chain, IDirect3DSurface9 *dest) override;
	RenderResult StretchRect(IDirect3DSurface9 *source, const RenderRect *source_rect, IDirect3DSurface9 *dest,
		const RenderRect *dest_rect, D3DTEXTUREFILTERTYPE filter) override;
	RenderResult CreateOffscreenPlainSurface(unsigned int width, unsigned int height, D3DFORMAT format, D3DPOOL pool,
		IDirect3DSurface9 **surface, void **shared) override;
	RenderResult SetRenderTarget(RenderUInt32 index, IDirect3DSurface9 *surface) override;
	RenderResult GetRenderTarget(RenderUInt32 index, IDirect3DSurface9 **surface) override;
	RenderResult SetDepthStencilSurface(IDirect3DSurface9 *surface) override;
	RenderResult GetDepthStencilSurface(IDirect3DSurface9 **surface) override;
	RenderResult Clear(RenderUInt32 count, const D3DRECT *rects, RenderUInt32 flags, D3DCOLOR color, float z,
		RenderUInt32 stencil) override;

	RenderResult BeginScene() override;
	RenderResult EndScene() override;
	RenderResult SetTransform(D3DTRANSFORMSTATETYPE state, const D3DMATRIX *matrix) override;
	RenderResult GetTransform(D3DTRANSFORMSTATETYPE state, D3DMATRIX *matrix) override;
	RenderResult SetViewport(const D3DVIEWPORT9 *viewport) override;
	RenderResult GetViewport(D3DVIEWPORT9 *viewport) override;
	RenderResult SetMaterial(const D3DMATERIAL9 *material) override;
	RenderResult SetLight(RenderUInt32 index, const D3DLIGHT9 *light) override;
	RenderResult LightEnable(RenderUInt32 index, int enable) override;
	RenderResult SetClipPlane(RenderUInt32 index, const float *plane) override;
	RenderResult SetRenderState(D3DRENDERSTATETYPE state, RenderUInt32 value) override;
	RenderResult GetRenderState(D3DRENDERSTATETYPE state, RenderUInt32 *value) override;
	RenderResult GetTexture(RenderUInt32 stage, IDirect3DBaseTexture9 **texture) override;
	RenderResult SetTexture(RenderUInt32 stage, IDirect3DBaseTexture9 *texture) override;
	RenderResult GetTextureStageState(RenderUInt32 stage, D3DTEXTURESTAGESTATETYPE type, RenderUInt32 *value) override;
	RenderResult SetTextureStageState(RenderUInt32 stage, D3DTEXTURESTAGESTATETYPE type, RenderUInt32 value) override;
	RenderResult SetSamplerState(RenderUInt32 sampler, D3DSAMPLERSTATETYPE type, RenderUInt32 value) override;
	RenderResult ValidateDevice(RenderUInt32 *passes) override;
	RenderResult SetSoftwareVertexProcessing(int software) override;
	RenderResult DrawPrimitive(D3DPRIMITIVETYPE type, unsigned int start_vertex, unsigned int primitive_count) override;
	RenderResult DrawIndexedPrimitive(D3DPRIMITIVETYPE type, int base_vertex, unsigned int min_vertex,
		unsigned int vertex_count, unsigned int start_index, unsigned int primitive_count) override;
	RenderResult DrawPrimitiveUP(D3DPRIMITIVETYPE type, unsigned int primitive_count, const void *vertices,
		unsigned int stride) override;
	RenderResult ProcessVertices(unsigned int source_start, unsigned int dest_index, unsigned int vertex_count,
		IDirect3DVertexBuffer9 *dest, IDirect3DVertexDeclaration9 *declaration, RenderUInt32 flags) override;
	RenderResult CreateVertexDeclaration(const D3DVERTEXELEMENT9 *elements, IDirect3DVertexDeclaration9 **declaration) override;
	RenderResult SetVertexDeclaration(IDirect3DVertexDeclaration9 *declaration) override;
	RenderResult SetFVF(RenderUInt32 fvf) override;
	RenderResult CreateVertexShader(const RenderUInt32 *function, IDirect3DVertexShader9 **shader) override;
	RenderResult SetVertexShader(IDirect3DVertexShader9 *shader) override;
	RenderResult GetVertexShader(IDirect3DVertexShader9 **shader) override;
	RenderResult SetVertexShaderConstantF(unsigned int start, const float *data, unsigned int count) override;
	RenderResult SetStreamSource(unsigned int stream, IDirect3DVertexBuffer9 *buffer, unsigned int offset,
		unsigned int stride) override;
	RenderResult SetIndices(IDirect3DIndexBuffer9 *buffer) override;
	RenderResult GetIndices(IDirect3DIndexBuffer9 **buffer) override;
	RenderResult CreatePixelShader(const RenderUInt32 *function, IDirect3DPixelShader9 **shader) override;
	RenderResult SetPixelShader(IDirect3DPixelShader9 *shader) override;
	RenderResult GetPixelShader(IDirect3DPixelShader9 **shader) override;
	RenderResult SetPixelShaderConstantF(unsigned int start, const float *data, unsigned int count) override;

protected:
	struct SunCapture;
	std::shared_ptr<SunCapture> Sun;
	bool SunActive = false, SunFailed = false, SunReady = false;
	bool SmokeGlow = false;
	unsigned SunCasterEffect = 0;
	float SunCasterOpacity = 1;
	bool Sun_Camera_Space() const;
	bool Sun_Target(struct SdlTarget &target) const;
	unsigned SunExclusions = 0;
	unsigned SunSceneDepth = 0, SunSceneExclusions = 0;
	uint64_t SunSceneGeneration = 0;
	uint64_t SunSolidReceivers = 0, SunGroundReceivers = 0;
	uint64_t SunCameraReceivers = 0, SunAlphaReceivers = 0, SunCloakCasters = 0;
	uint64_t SunReceiversAtSceneStart = 0;
	std::string SunReceiverFailure, SunCaptureFailure;
	void Fail_Sun_Receiver(const char *reason);
	bool Sun_Receives_Target(const struct SdlTarget &target) const;
	bool Sun_Receiving(int vertexProgram, int pixelProgram, const struct SdlTarget &target) const;
	void Build_Sun_Constants(struct SdlEnginePixelConstants &constants) const;
	struct SDL_GPUTexture *Sun_Texture() const;
	struct SDL_GPUSampler *Sun_Sampler() const;
	struct ShoreRegion {
		WaterShoreOverlay::Quad patch;
		WaterShoreOverlay::View view;
		float terrain[4];
		std::shared_ptr<IDirect3DBaseTexture9> height, optical;
	};
	bool ShoreActive=false, ShoreHasPatch=false, ShoreHasTrack=false;
	WaterShoreOverlay::Quad ShorePatch, ShoreTrack;
	std::vector<ShoreRegion> ShoreRegions; // <=128; COM refs released at scope end/reset/destruction

	~PosixDevice9() override;

	/// Drops every implicit surface and every render target and depth binding.  Reset and the destructor.
	void Release_Surfaces();
	/// D3D9's documented initial render, texture-stage and sampler states (PosixDevice9Draw.cpp).
	void Set_Default_States();
	/// A draw's answer where there is no draw yet: nothing to show with no window, so success; with one,
	/// a loud failure.  ProcessVertices.
	RenderResult Draw_Unavailable(const char *what);

	/// One Draw* call, as the recording reads it (PosixDevice9Draw.cpp).
	struct DrawCall
	{
		D3DPRIMITIVETYPE Type;
		unsigned int PrimitiveCount;
		bool Indexed;
		unsigned int StartVertex;		///< DrawPrimitive's
		int BaseVertex;					///< DrawIndexedPrimitive's
		unsigned int MinVertex;
		unsigned int VertexCount;
		unsigned int StartIndex;
		const void *UserVertices;		///< DrawPrimitiveUP's
		unsigned int UserStride;
	};
	/// Records a fixed-function draw on the GPU: the state resolved, the GPU copies brought up to date, and
	/// what can still change staged.  Headless, nothing.
	RenderResult Gpu_Draw(const DrawCall &call);
	void Refuse_Draw(const std::string &reason);
	/// The current render target and depth-stencil as the frame's target (A3d).  False, with the reason,
	/// for what cannot be drawn into.
	bool Resolve_Target(struct SdlTarget &target, std::string &refusal);
	/// A render-target surface's GPU texture: the back buffer's, a render-target texture's first level,
	/// or a standalone render target's.
	struct SDL_GPUTexture *Gpu_Texture_Of(IDirect3DSurface9 *surface, std::string &refusal);
	/// ZH_GPU_TRACE's draws: their state and first vertices, a development aid until A3d's capture.
	void Trace_Draw_If_Asked(const DrawCall &call, const std::string &programs, class PosixVertexBuffer9 *vertex_buffer,
		unsigned int stride);
	/// ZH_GPU_DUMP_FRAMES' frames to ZH_GPU_DUMP_DIR, a development aid until A3d's capture.
	void Dump_Frame_If_Asked();
	/// ZH_GPU_CAPTURE (DrawCapture.h, PosixDevice9Capture.cpp): whether it is set, the first draw of each
	/// signature written there, and at teardown what was written and what was not, and why.
	static bool Capture_Is_Asked();
	void Capture_Draw(const DrawCall &call, const std::string &signature, unsigned int stride, unsigned int reads,
		unsigned int sampled_stages, unsigned int target_width, unsigned int target_height);
	static void Capture_Report();
	/// PERF1's timing aid (PosixDevice9Timing.cpp): ZH_GPU_TIMING="delay,count", ZH_GPU_TIMING_SYNC=1.
	static bool Timing_Is_Asked();
	void Timing_Frame_Start();
	void Timing_Present(double present_ms, unsigned int draws);
	static void Timing_Report();
	double TimingDrawMs = 0.0;				///< CPU time in Gpu_Draw since the last Present, when timing
	unsigned int TimingDrawsAtPresent = 0;
	/// Capture version 2's draw_<n>.prog for the draw being captured (DrawCapture.h), with the draw's own
	/// stride; the bytes written.
	uint64_t Write_Programs(const std::string &path, unsigned int stride);
	/// The name the engine registered a shader of this device's under (Platform/EngineShaderName.h), or
	/// empty for one it never named (A3e).
	static std::string Engine_Name_Of(const void *shader);
	/// A shader of this device's tokens, as it was made with them; false for one it did not make.
	static bool Shader_Tokens_Of(const void *shader, std::vector<RenderUInt32> &tokens);
	/// A declaration of this device's elements, without the end element.
	static void Declaration_Elements_Of(IDirect3DVertexDeclaration9 *declaration, std::vector<D3DVERTEXELEMENT9> &elements);
	/// The engine's D3D8 declaration tokens a declaration was made from (PosixDevice_Keep_D3D8_Declaration);
	/// empty for one made otherwise.
	static void Declaration_D3D8_Tokens_Of(IDirect3DVertexDeclaration9 *declaration, std::vector<RenderUInt32> &tokens);
	/// The engineshader program a bound shader is (EngineShaderProgram), from its registered name; false,
	/// with the draw refused by that name, for one that has no transcription.
	bool Engine_Program_Of(const void *shader, const char *stage, int &program);

	PosixDirect3D9 *Adapter;			///< held, as D3D9's device holds its IDirect3D9
	SdlGpuFrame *Gpu;					///< the SDL3 GPU frame, with a window only
	SdlProgramCache *Programs;			///< the draw's caches and GPU copies, with the frame
	SdlPipelineCache *Pipelines;
	SdlSamplerCache *Samplers;
	SdlResourceMirrors *Mirrors;
	unsigned int DrawsRecorded;
	unsigned int PresentCount;
	std::map<std::string, unsigned int> DrawRefusals;
	std::map<std::string, unsigned int> EngineProgramDraws;	///< draws recorded with a transcribed half, by its name (A3e)
	RenderWindow Window;				///< null under -headless
	D3DPRESENT_PARAMETERS Parameters;

	// The surfaces: the implicit ones CreateDevice makes, and what is bound.  The resource code fills them; all of
	// them are references this device holds, released by Release_Surfaces.
	IDirect3DSurface9 *BackBuffer;
	IDirect3DSurface9 *DepthSurface;
	IDirect3DSurface9 *RenderTargets[RENDER_TARGET_COUNT];
	IDirect3DSurface9 *DepthStencil;

	// The state, as set.  A3 reads it at the draw.
	bool InScene;
	RenderUInt32 RenderStates[RENDER_STATE_COUNT];
	RenderUInt32 TextureStageStates[TEXTURE_STAGE_COUNT][TEXTURE_STAGE_STATE_COUNT];
	RenderUInt32 SamplerStates[SAMPLER_COUNT][SAMPLER_STATE_COUNT];
	D3DMATRIX Transforms[TRANSFORM_COUNT];
	D3DVIEWPORT9 Viewport;
	D3DMATERIAL9 Material;
	D3DLIGHT9 Lights[LIGHT_COUNT];
	bool LightsEnabled[LIGHT_COUNT];
	float ClipPlanes[CLIP_PLANE_COUNT][4];
	IDirect3DBaseTexture9 *Textures[SAMPLER_COUNT];
	IDirect3DVertexBuffer9 *Streams[STREAM_COUNT];
	unsigned int StreamOffsets[STREAM_COUNT];
	unsigned int StreamStrides[STREAM_COUNT];
	IDirect3DIndexBuffer9 *Indices;
	IDirect3DVertexDeclaration9 *Declaration;
	/// D3D9's one current vertex format is whichever of SetFVF and SetVertexDeclaration came last.  The draw
	/// always lays out by the FVF (A3e); capture version 2 records the declaration only while it is current.
	bool DeclarationIsCurrent;
	RenderUInt32 FVF;
	IDirect3DVertexShader9 *VertexShader;
	IDirect3DPixelShader9 *PixelShader;
	float VertexShaderConstants[VERTEX_SHADER_CONSTANT_COUNT][4];
	float PixelShaderConstants[PIXEL_SHADER_CONSTANT_COUNT][4];
	D3DGAMMARAMP GammaRamp;
};

#endif // POSIXDEVICE9_H
