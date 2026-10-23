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

// The device off Windows (decision 7): its state, bindings, scenes, presentation, shaders and draws.
// The resource and surface methods, Clear and the caps are in PosixDevice9Resources.cpp and
// PosixD3D9Caps.cpp.  See PosixDevice9.h for which file holds what and how a device without a window behaves.

#include "PosixDevice9.h"
#include "PosixD3D9Caps.h"
#include "SdlCreationLog.h"
#include "Platform/EngineShaderName.h"
#include "Platform/RendererName.h"
#include "PosixImageOps.h"
#include "PosixResources9.h"
#include "SdlGpuFrame.h"
#include "SdlPipelineCache.h"
#include "SdlProgramCache.h"
#include "SdlResourceMirror.h"

#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <map>
#include <mutex>
#include <string>
#include <vector>

#include <SDL3/SDL.h>

// The HUD's renderer name, set when the GPU frame is made (below, with PosixRenderer_Name).
static void Set_Renderer_Name(const char *driver);

//-------------------------------------------------------------------------------------------------
// Shaders and vertex declarations: held as given.  A3 translates them at the draw.
//-------------------------------------------------------------------------------------------------

namespace
{

/// What every shader the device makes carries besides its tokens: the name the engine registered it under
/// (Platform/EngineShaderName.h), which is what the draw reads (A3e).  Empty until it is named.
class PosixShaderName
{
public:
	std::string EngineName;
	std::vector<RenderUInt32> Tokens;	///< as CreateVertexShader or CreatePixelShader received them, end token included
};

/// The shaders alive, by the interface pointer the engine holds, so a registration can find the object
/// it names.  Each shader adds itself when made and leaves when destroyed, so an address the allocator
/// hands out again never inherits a dead shader's name.
std::mutex LiveShadersLock;
std::map<const void *, PosixShaderName *> LiveShaders;

/// A vertex or pixel shader's tokens, up to and including D3D's end token.
template <class Interface>
class PosixShader9 : public PosixRefCounted<Interface>, public PosixShaderName
{
public:
	explicit PosixShader9(const RenderUInt32 *function)
	{
		const RenderUInt32 END_TOKEN = 0x0000FFFF;
		do {
			this->Tokens.push_back(*function);
		} while (*function++ != END_TOKEN);
		std::lock_guard<std::mutex> hold(LiveShadersLock);
		LiveShaders[static_cast<Interface *>(this)] = this;
	}
	~PosixShader9() override
	{
		std::lock_guard<std::mutex> hold(LiveShadersLock);
		LiveShaders.erase(static_cast<Interface *>(this));
	}
};

class PosixVertexDeclaration9 : public PosixRefCounted<IDirect3DVertexDeclaration9>
{
public:
	explicit PosixVertexDeclaration9(const D3DVERTEXELEMENT9 *elements)
	{
		// D3DDECL_END is the element whose stream is 0xFF.
		do {
			Elements.push_back(*elements);
		} while ((elements++)->Stream != 0xFF);
	}
	std::vector<D3DVERTEXELEMENT9> Elements;
	std::vector<RenderUInt32> D3D8Tokens;	///< the engine's D3D8 declaration, when it came from one (capture v3)
};

}

//-------------------------------------------------------------------------------------------------
// Construction and teardown.
//-------------------------------------------------------------------------------------------------

PosixDevice9::PosixDevice9(PosixDirect3D9 *adapter, RenderWindow window, const D3DPRESENT_PARAMETERS &parameters) :
	Adapter(adapter),
	Gpu(NULL),
	Programs(NULL),
	Pipelines(NULL),
	Samplers(NULL),
	Mirrors(NULL),
	DrawsRecorded(0),
	PresentCount(0),
	Window(window),
	Parameters(parameters),
	BackBuffer(NULL),
	DepthSurface(NULL),
	DepthStencil(NULL),
	InScene(false),
	Indices(NULL),
	Declaration(NULL),
	DeclarationIsCurrent(false),
	FVF(0),
	VertexShader(NULL),
	PixelShader(NULL)
{
	Adapter->AddRef();

	memset(RenderTargets, 0, sizeof(RenderTargets));
	memset(RenderStates, 0, sizeof(RenderStates));
	memset(TextureStageStates, 0, sizeof(TextureStageStates));
	memset(SamplerStates, 0, sizeof(SamplerStates));
	Set_Default_States();
	// Every transform starts as the identity, as D3D9's do.
	memset(Transforms, 0, sizeof(Transforms));
	for (int index = 0; index < TRANSFORM_COUNT; ++index) {
		Transforms[index]._11 = Transforms[index]._22 = Transforms[index]._33 = Transforms[index]._44 = 1.0f;
	}
	// The viewport starts as the whole back buffer.
	Viewport.X = 0;
	Viewport.Y = 0;
	Viewport.Width = Parameters.BackBufferWidth;
	Viewport.Height = Parameters.BackBufferHeight;
	Viewport.MinZ = 0.0f;
	Viewport.MaxZ = 1.0f;
	memset(&Material, 0, sizeof(Material));
	memset(Lights, 0, sizeof(Lights));
	memset(LightsEnabled, 0, sizeof(LightsEnabled));
	memset(ClipPlanes, 0, sizeof(ClipPlanes));
	memset(Textures, 0, sizeof(Textures));
	memset(Streams, 0, sizeof(Streams));
	memset(StreamOffsets, 0, sizeof(StreamOffsets));
	memset(StreamStrides, 0, sizeof(StreamStrides));
	memset(VertexShaderConstants, 0, sizeof(VertexShaderConstants));
	memset(PixelShaderConstants, 0, sizeof(PixelShaderConstants));
	for (int index = 0; index < 256; ++index) {
		GammaRamp.red[index] = GammaRamp.green[index] = GammaRamp.blue[index] = (uint16_t)(index * 257);
	}
}

PosixDevice9::~PosixDevice9()
{
	Clear_Sun_Shadow();
	Sun.reset();
	Shore_End();
	Release_Surfaces();
	for (int index = 0; index < SAMPLER_COUNT; ++index) {
		Posix_Bind(Textures[index], (IDirect3DBaseTexture9 *)NULL);
	}
	for (int index = 0; index < STREAM_COUNT; ++index) {
		Posix_Bind(Streams[index], (IDirect3DVertexBuffer9 *)NULL);
	}
	Posix_Bind(Indices, (IDirect3DIndexBuffer9 *)NULL);
	Posix_Bind(Declaration, (IDirect3DVertexDeclaration9 *)NULL);
	Posix_Bind(VertexShader, (IDirect3DVertexShader9 *)NULL);
	Posix_Bind(PixelShader, (IDirect3DPixelShader9 *)NULL);
	// With a GPU, what the draws came to: the count, and each refusal's reason once with its count.
	if (Gpu != NULL) {
		if (Sdl_Creation_Log_Asked()) {
			Sdl_Creation_Log_Flush();
		}
		fprintf(stderr, "PosixDevice9: %u draws recorded, %u presents, %u of them to a window that was not visible, %u with"
			" no drawable\n", DrawsRecorded, PresentCount, Gpu->Presents_Not_Visible(), Gpu->Presents_Not_Shown());
		fprintf(stderr, "PosixDevice9: %u mid-frame flushes, at most %u in flight at once, %u waited for the ring (limit %u);"
			" %u of the flushes for the draw limit (%u draws)\n",
			Gpu->Flushes_Total(), Gpu->Flushes_In_Flight_Most(), Gpu->Flush_Waits(), Gpu->Flush_Limit(),
			Gpu->Draw_Limit_Flushes(), Gpu->Batch_Draw_Limit());
		for (std::map<std::string, unsigned int>::const_iterator it = DrawRefusals.begin(); it != DrawRefusals.end(); ++it) {
			fprintf(stderr, "PosixDevice9:   refused %u: %s\n", it->second, it->first.c_str());
		}
		for (std::map<std::string, unsigned int>::const_iterator it = EngineProgramDraws.begin(); it != EngineProgramDraws.end(); ++it) {
			fprintf(stderr, "PosixDevice9:   drawn with the engine's %s: %u\n", it->first.c_str(), it->second);
		}
		Capture_Report();
		Timing_Report();
	}
	// The GPU objects before the device that made them; the pipelines before their shaders.
	delete Pipelines;
	delete Samplers;
	delete Programs;
	delete Mirrors;
	delete Gpu;
	Adapter->Release();
}

RenderResult PosixDevice9::Create_Gpu_Frame(bool offscreen)
{
	if (Gpu != NULL || (Window == NULL && !offscreen)) {
		return D3D_OK;		// -headless: no GPU device at all
	}
	std::string error;
	Gpu = SdlGpuFrame::Create(Window, Parameters.BackBufferWidth, Parameters.BackBufferHeight, error);
	if (Gpu == NULL) {
		fprintf(stderr, "PosixDevice9: a window, and no SDL3 GPU device for it: %s\n", error.c_str());
		return D3DERR_NOTAVAILABLE;
	}
	Set_Renderer_Name(SDL_GetGPUDeviceDriver(Gpu->Device()));
	Programs = new SdlProgramCache(Gpu->Device());
	Pipelines = new SdlPipelineCache(Gpu->Device());
	Pipelines->Describe_Shader = [this](const SDL_GPUShader *shader) { return Programs->Key_Of(shader); };
	Samplers = new SdlSamplerCache(Gpu->Device());
	Mirrors = new SdlResourceMirrors(Gpu);
	return D3D_OK;
}

void PosixDevice9::Present_Offscreen(unsigned int hz)
{
	if (Gpu != NULL) {
		Gpu->Set_Offscreen_Presents(hz);
		const bool paced = hz > 0 && hz <= 1000;
		fprintf(stderr, "PosixDevice9: offscreen, %ux%u, %s\n", Gpu->Width(), Gpu->Height(), paced ? "paced" : "unpaced");
		if (paced) {
			fprintf(stderr, "PosixDevice9:   at %u frames a second\n", hz);
		} else if (hz > 1000) {
			fprintf(stderr, "PosixDevice9:   ZH_OFFSCREEN_HZ %u is out of range (1 to 1000)\n", hz);
		}
	}
}

RenderResult PosixDevice9::Gpu_Clear(RenderUInt32 count, const D3DRECT *rects, RenderUInt32 flags, D3DCOLOR color,
	float z, RenderUInt32 stencil)
{
	if (Gpu == NULL) {
		return D3DERR_INVALIDCALL;
	}
	// The current target (A3d): the back buffer or a render target's GPU texture.
	std::string refusal;
	SdlTarget target;
	if (!Resolve_Target(target, refusal)) {
		static bool said = false;
		if (!said) {
			said = true;
			fprintf(stderr, "PosixDevice9::Clear: %s; refused\n", refusal.c_str());
		}
		return D3DERR_INVALIDCALL;
	}
	Gpu->Set_Target(target);
	const bool colour = (flags & D3DCLEAR_TARGET) != 0;
	const bool depth = (flags & D3DCLEAR_ZBUFFER) != 0;
	const bool stencil_too = (flags & D3DCLEAR_STENCIL) != 0;
	// D3D9 clears each rectangle cut to the viewport, or the viewport itself when there are none.  What
	// covers the whole target is the next pass's load operation; anything less is a clear draw.
	const int target_width = (int)target.Width;
	const int target_height = (int)target.Height;
	const RenderUInt32 passes = (count == 0 || rects == NULL) ? 1 : count;
	for (RenderUInt32 index = 0; index < passes; ++index) {
		int left = (int)Viewport.X, top = (int)Viewport.Y;
		int right = left + (int)Viewport.Width, bottom = top + (int)Viewport.Height;
		if (count != 0 && rects != NULL) {
			if (rects[index].x1 > left) left = rects[index].x1;
			if (rects[index].y1 > top) top = rects[index].y1;
			if (rects[index].x2 < right) right = rects[index].x2;
			if (rects[index].y2 < bottom) bottom = rects[index].y2;
		}
		if (left < 0) left = 0;
		if (top < 0) top = 0;
		if (right > target_width) right = target_width;
		if (bottom > target_height) bottom = target_height;
		if (right <= left || bottom <= top) {
			continue;
		}
		if (left == 0 && top == 0 && right == target_width && bottom == target_height) {
			Gpu->Clear_Back_Buffer(colour, depth, stencil_too, color, z, stencil);
		}
		else {
			Gpu->Clear_Rect(left, top, right - left, bottom - top, colour, depth, stencil_too, color, z, stencil);
		}
	}
	return D3D_OK;
}

// ---- The HUD's renderer name (Platform/RendererName.h): the SDL3 backend drawing, or "Headless".

static char16_t RendererName[32] = u"Headless";

const char16_t *PosixRenderer_Name(void)
{
	return RendererName;
}

// "Metal arm64", "Vulkan x64": the SDL3 GPU driver's name, capitalised as its API spells it, and the
// architecture as Windows' "DX11 x64" carries its own.
static void Set_Renderer_Name(const char *driver)
{
	const char *api = driver;
	if (strcmp(driver, "metal") == 0) api = "Metal";
	else if (strcmp(driver, "vulkan") == 0) api = "Vulkan";
#if defined(__aarch64__) || defined(__arm64__)
	const char *arch = " arm64";
#elif defined(__x86_64__)
	const char *arch = " x64";
#else
	const char *arch = "";
#endif
	char name[32];
	snprintf(name, sizeof(name), "%s%s", api, arch);
	size_t i = 0;
	for (; name[i] != '\0' && i + 1 < sizeof(RendererName) / sizeof(RendererName[0]); ++i) {
		RendererName[i] = (char16_t)(unsigned char)name[i];
	}
	RendererName[i] = 0;
}

// ---- The render-target seam (A3d).  See PosixDevice9.h.

bool PosixDevice9::Gpu_Owns(IDirect3DSurface9 *surface) const
{
	if (Gpu == NULL || surface == NULL) {
		return false;
	}
	if (surface == BackBuffer || surface == DepthSurface) {
		return true;
	}
	D3DSURFACE_DESC desc;
	return surface->GetDesc(&desc) == D3D_OK && (desc.Usage & (D3DUSAGE_RENDERTARGET | D3DUSAGE_DEPTHSTENCIL)) != 0;
}

RenderResult PosixDevice9::Gpu_Download(IDirect3DSurface9 *surface)
{
	if (!Gpu_Owns(surface) || surface == DepthSurface) {
		return D3DERR_INVALIDCALL;
	}
	D3DSURFACE_DESC desc;
	surface->GetDesc(&desc);
	if ((desc.Usage & D3DUSAGE_DEPTHSTENCIL) != 0) {
		return D3DERR_INVALIDCALL;		// nothing in the engine reads depth back
	}
	// Nothing has drawn into a render target that has no GPU copy yet: its CPU image is the current one.
	if (surface != BackBuffer) {
		IDirect3DTexture9 *container = NULL;
		const void *owner = surface;
		if (surface->GetContainer(IID_IDirect3DTexture9, (void **)&container) == D3D_OK && container != NULL) {
			owner = container;
			container->Release();
		}
		if (!Mirrors->Has_Copy(owner)) {
			return D3D_OK;
		}
	}
	std::string refusal;
	SDL_GPUTexture *texture = Gpu_Texture_Of(surface, refusal);
	std::vector<uint8_t> bgra;
	if (texture == NULL || !Gpu->Read_Back(texture, desc.Width, desc.Height, bgra)) {
		return D3DERR_DRIVERINTERNALERROR;
	}
	PosixImage &image = static_cast<PosixSurface9 *>(surface)->image();
	return posixWriteFromBgra(image, &bgra[0], desc.Width, desc.Height) ? D3D_OK : D3DERR_INVALIDCALL;
}

RenderResult PosixDevice9::Gpu_Download_Front(IDirect3DSurface9 *dest)
{
	if (Gpu == NULL || dest == NULL) {
		return D3DERR_INVALIDCALL;
	}
	std::vector<uint8_t> presented;
	const unsigned int width = Gpu->Width(), height = Gpu->Height();
	if (!Gpu->Read_Back(Gpu->Front_Copy(), width, height, presented)) {
		return D3DERR_DRIVERINTERNALERROR;
	}
	// Windowed, D3D9's front buffer is the display's size and the window is a part of it.  The presented
	// picture goes at the top left, and the rest is black.
	D3DSURFACE_DESC desc;
	dest->GetDesc(&desc);
	std::vector<uint8_t> bgra((size_t)desc.Width * desc.Height * 4, 0);
	for (size_t i = 3; i < bgra.size(); i += 4) {
		bgra[i] = 0xFF;
	}
	const unsigned int rows = height < desc.Height ? height : desc.Height;
	const unsigned int columns = width < desc.Width ? width : desc.Width;
	for (unsigned int y = 0; y < rows; ++y) {
		memcpy(&bgra[(size_t)y * desc.Width * 4], &presented[(size_t)y * width * 4], (size_t)columns * 4);
	}
	PosixImage &image = static_cast<PosixSurface9 *>(dest)->image();
	return posixWriteFromBgra(image, &bgra[0], desc.Width, desc.Height) ? D3D_OK : D3DERR_INVALIDCALL;
}

RenderResult PosixDevice9::Gpu_StretchRect(IDirect3DSurface9 *source, const RenderRect *source_rect, IDirect3DSurface9 *dest,
	const RenderRect *dest_rect, D3DTEXTUREFILTERTYPE filter)
{
	if (!Gpu_Owns(source) || !Gpu_Owns(dest)) {
		return D3DERR_INVALIDCALL;
	}
	D3DSURFACE_DESC source_desc, dest_desc;
	source->GetDesc(&source_desc);
	dest->GetDesc(&dest_desc);
	if ((source_desc.Usage & D3DUSAGE_DEPTHSTENCIL) != 0 || (dest_desc.Usage & D3DUSAGE_DEPTHSTENCIL) != 0
		|| source == DepthSurface || dest == DepthSurface) {
		return D3DERR_INVALIDCALL;		// no depth copies: nothing in the engine makes one
	}
	std::string refusal;
	SDL_GPUTexture *from = Gpu_Texture_Of(source, refusal);
	SDL_GPUTexture *to = from != NULL ? Gpu_Texture_Of(dest, refusal) : NULL;
	if (to == NULL) {
		fprintf(stderr, "PosixDevice9::StretchRect: %s\n", refusal.c_str());
		return D3DERR_INVALIDCALL;
	}
	int32_t from_rect[4] = { 0, 0, (int32_t)source_desc.Width, (int32_t)source_desc.Height };
	int32_t to_rect[4] = { 0, 0, (int32_t)dest_desc.Width, (int32_t)dest_desc.Height };
	if (source_rect != NULL) {
		from_rect[0] = source_rect->left; from_rect[1] = source_rect->top;
		from_rect[2] = source_rect->right - source_rect->left; from_rect[3] = source_rect->bottom - source_rect->top;
	}
	if (dest_rect != NULL) {
		to_rect[0] = dest_rect->left; to_rect[1] = dest_rect->top;
		to_rect[2] = dest_rect->right - dest_rect->left; to_rect[3] = dest_rect->bottom - dest_rect->top;
	}
	if (from_rect[2] <= 0 || from_rect[3] <= 0 || to_rect[2] <= 0 || to_rect[3] <= 0) {
		return D3DERR_INVALIDCALL;
	}
	Gpu->Record_Blit(from, from_rect, to, to_rect, filter == D3DTEXF_LINEAR);
	return D3D_OK;
}

void PosixDevice9::Release_Surfaces()
{
	for (int index = 0; index < RENDER_TARGET_COUNT; ++index) {
		Posix_Bind(RenderTargets[index], (IDirect3DSurface9 *)NULL);
	}
	Posix_Bind(DepthStencil, (IDirect3DSurface9 *)NULL);
	Posix_Bind(BackBuffer, (IDirect3DSurface9 *)NULL);
	Posix_Bind(DepthSurface, (IDirect3DSurface9 *)NULL);
}

//-------------------------------------------------------------------------------------------------
// The device as a whole.
//-------------------------------------------------------------------------------------------------

RenderResult PosixDevice9::TestCooperativeLevel()
{
	return D3D_OK;		// never lost: there is no exclusive mode to lose
}

unsigned int PosixDevice9::GetAvailableTextureMem()
{
	return 512u * 1024u * 1024u;	// what the engine sizes its texture reduction by; memory is the host's
}

RenderResult PosixDevice9::EvictManagedResources()
{
	return D3D_OK;
}

RenderResult PosixDevice9::GetDisplayMode(unsigned int swap_chain, D3DDISPLAYMODE *mode)
{
	if (swap_chain != 0 || mode == NULL) {
		return D3DERR_INVALIDCALL;
	}
	if (Parameters.Windowed) {
		return Adapter->GetAdapterDisplayMode(0, mode);
	}
	mode->Width = Parameters.BackBufferWidth;
	mode->Height = Parameters.BackBufferHeight;
	mode->RefreshRate = Parameters.FullScreen_RefreshRateInHz;
	mode->Format = Parameters.BackBufferFormat;
	return D3D_OK;
}

// The hardware cursor is C3's (W3DMouse), and it does not go through the device off Windows.
RenderResult PosixDevice9::SetCursorProperties(unsigned int, unsigned int, IDirect3DSurface9 *)
{
	return D3D_OK;
}

void PosixDevice9::SetCursorPosition(int, int, RenderUInt32)
{
}

int PosixDevice9::ShowCursor(int)
{
	return 0;
}

RenderResult PosixDevice9::CreateAdditionalSwapChain(D3DPRESENT_PARAMETERS *, IDirect3DSwapChain9 **swap_chain)
{
	if (swap_chain != NULL) {
		*swap_chain = NULL;
	}
	return D3DERR_NOTAVAILABLE;	// the engine makes none
}

RenderResult PosixDevice9::Reset(D3DPRESENT_PARAMETERS *parameters)
{
	if (parameters == NULL || parameters->BackBufferWidth == 0 || parameters->BackBufferHeight == 0) {
		return D3DERR_INVALIDCALL;
	}
	// Stage the CPU surfaces too: once the GPU attachments change, committing the reset cannot fail.
	const D3DFORMAT format = parameters->BackBufferFormat == D3DFMT_UNKNOWN
		? D3DFMT_X8R8G8B8 : parameters->BackBufferFormat;
	if (!posixFormatSupported(D3DUSAGE_RENDERTARGET, D3DRTYPE_SURFACE, format)
		|| (parameters->EnableAutoDepthStencil && !posixFormatSupported(D3DUSAGE_DEPTHSTENCIL,
			D3DRTYPE_SURFACE, parameters->AutoDepthStencilFormat))) {
		return D3DERR_INVALIDCALL;
	}
	PosixSurface9 *back = PosixSurface9::createStandalone(parameters->BackBufferWidth,
		parameters->BackBufferHeight, format, D3DUSAGE_RENDERTARGET, D3DPOOL_DEFAULT,
		parameters->MultiSampleType, parameters->MultiSampleQuality);
	if (!back) return D3DERR_OUTOFVIDEOMEMORY;
	PosixSurface9 *depth = NULL;
	if (parameters->EnableAutoDepthStencil) {
		depth = PosixSurface9::createStandalone(parameters->BackBufferWidth, parameters->BackBufferHeight,
			parameters->AutoDepthStencilFormat, D3DUSAGE_DEPTHSTENCIL, D3DPOOL_DEFAULT,
			parameters->MultiSampleType, parameters->MultiSampleQuality);
		if (!depth) {
			back->Release();
			return D3DERR_OUTOFVIDEOMEMORY;
		}
	}
	SdlGpuFrame::ResizeFailure failure = SdlGpuFrame::RESIZE_NONE;
	// Restore an active capture before Resize can retire the scene attachments it restores to.
	bool capture_submitted = true;
	if (SunActive) End_Sun_Shadow(&capture_submitted);
	if (!capture_submitted || (Gpu != NULL && !Gpu->Resize(parameters->BackBufferWidth,
		parameters->BackBufferHeight, &failure))) {
		back->Release();
		if (depth) depth->Release();
		return !capture_submitted || failure == SdlGpuFrame::RESIZE_FLUSH_FAILED
			? D3DERR_DEVICELOST : D3DERR_OUTOFVIDEOMEMORY;
	}
	Shore_End();
	Clear_Sun_Shadow();
	Sun.reset();
	// A failed flush must never drop these bindings: a standalone target can have no other owner,
	// while the retained batch still references its mirror. Resize has now submitted that batch.
	Release_Surfaces();
	Parameters = *parameters;
	if (Parameters.hDeviceWindow != NULL) {
		Window = Parameters.hDeviceWindow;
	}
	Viewport.X = 0;
	Viewport.Y = 0;
	Viewport.Width = Parameters.BackBufferWidth;
	Viewport.Height = Parameters.BackBufferHeight;
	Viewport.MinZ = 0.0f;
	Viewport.MaxZ = 1.0f;
	BackBuffer = back;
	Posix_Bind(RenderTargets[0], BackBuffer);
	DepthSurface = depth;
	Posix_Bind(DepthStencil, DepthSurface);
	return D3D_OK;
}

RenderResult PosixDevice9::Present(const RenderRect *, const RenderRect *, RenderWindow, const void *)
{
	// With no window there is nothing to present to.  With one, the back buffer goes to it through the
	// gamma ramp (SdlGpuFrame::Present); D3DGAMMARAMP is the three 256-entry ramps, red, green, blue.
	if (Gpu == NULL) {
		return D3D_OK;
	}
	static_assert(offsetof(D3DGAMMARAMP, green) == 512 && offsetof(D3DGAMMARAMP, blue) == 1024,
		"D3DGAMMARAMP is the three ramps back to back, as SdlGpuFrame::Present reads it");
	++PresentCount;
	Dump_Frame_If_Asked();
	if (Sdl_Creation_Log_Asked()) {
		// Every frame over 50 ms, beside the creation log's lines, to line them up.
		static double last_present = 0.0;
		const double now = Sdl_Now_Ms();
		if (last_present != 0.0 && now - last_present > 50.0) {
			char line[160];
			snprintf(line, sizeof(line), "PosixDevice9 create: t %10.1f ms  LONG FRAME %8.2f ms  (present %u, %u draws recorded so far)",
				last_present, now - last_present, PresentCount, DrawsRecorded);
			Sdl_Creation_Log_Line(line);
		}
		last_present = now;
		// The frame's lines, written here outside the draws and the present that are timed.
		Sdl_Creation_Log_Flush();
	}
	const bool timing = Timing_Is_Asked();
	const Uint64 present_start = timing ? SDL_GetTicksNS() : 0;
	const bool presented = Gpu->Present(reinterpret_cast<const uint16_t (*)[256]>(&GammaRamp));
	if (timing) {
		Timing_Present((double)(SDL_GetTicksNS() - present_start) / 1.0e6, DrawsRecorded - TimingDrawsAtPresent);
		TimingDrawsAtPresent = DrawsRecorded;
		Timing_Frame_Start();
	}
	if (!presented) {
		static bool said = false;
		if (!said) {
			said = true;
			fprintf(stderr, "PosixDevice9::Present: the GPU refused the frame\n");
		}
		return D3DERR_DRIVERINTERNALERROR;
	}
	return D3D_OK;
}

// A development aid until A3d's capture: ZH_GPU_DUMP_FRAMES="100,300" writes those presents' back
// buffers to ZH_GPU_DUMP_DIR (default the working directory) as frame_<n>.ppm, before the gamma ramp.
void PosixDevice9::Dump_Frame_If_Asked()
{
	const char *frames = getenv("ZH_GPU_DUMP_FRAMES");
	if (frames == NULL) {
		return;
	}
	bool wanted = false;
	for (const char *p = frames; *p != '\0' && !wanted; ) {
		char *end = NULL;
		const unsigned long frame = strtoul(p, &end, 10);
		if (end == p) break;
		wanted = frame == PresentCount;
		p = (*end == ',') ? end + 1 : end;
	}
	if (!wanted) {
		return;
	}
	std::vector<uint8_t> bgra;
	const unsigned int width = Gpu->Width(), height = Gpu->Height();
	if (!Gpu->Read_Back(Gpu->Back_Buffer(), width, height, bgra)) {
		fprintf(stderr, "PosixDevice9: frame %u could not be read back\n", PresentCount);
		return;
	}
	const char *dir = getenv("ZH_GPU_DUMP_DIR");
	char path[1024];
	snprintf(path, sizeof(path), "%s/frame_%u.ppm", dir != NULL ? dir : ".", PresentCount);
	FILE *file = fopen(path, "wb");
	if (file == NULL) {
		return;
	}
	fprintf(file, "P6\n%u %u\n255\n", width, height);
	for (size_t i = 0; i < bgra.size(); i += 4) {
		const uint8_t rgb[3] = { bgra[i + 2], bgra[i + 1], bgra[i] };
		fwrite(rgb, 1, 3, file);
	}
	fclose(file);
	fprintf(stderr, "PosixDevice9: frame %u written to %s\n", PresentCount, path);
}

void PosixDevice9::SetGammaRamp(unsigned int, RenderUInt32, const D3DGAMMARAMP *ramp)
{
	if (ramp != NULL) {
		GammaRamp = *ramp;	// kept for A3, which applies it to what it presents
	}
}

//-------------------------------------------------------------------------------------------------
// Scenes and state.
//-------------------------------------------------------------------------------------------------

RenderResult PosixDevice9::BeginScene()
{
	if (InScene) {
		return D3DERR_INVALIDCALL;
	}
	InScene = true;
	return D3D_OK;
}

RenderResult PosixDevice9::EndScene()
{
	if (!InScene) {
		return D3DERR_INVALIDCALL;
	}
	InScene = false;
	return D3D_OK;
}

RenderResult PosixDevice9::SetTransform(D3DTRANSFORMSTATETYPE state, const D3DMATRIX *matrix)
{
	if ((unsigned int)state >= TRANSFORM_COUNT || matrix == NULL) {
		return D3DERR_INVALIDCALL;
	}
	Transforms[state] = *matrix;
	return D3D_OK;
}

RenderResult PosixDevice9::GetTransform(D3DTRANSFORMSTATETYPE state, D3DMATRIX *matrix)
{
	if ((unsigned int)state >= TRANSFORM_COUNT || matrix == NULL) {
		return D3DERR_INVALIDCALL;
	}
	*matrix = Transforms[state];
	return D3D_OK;
}

RenderResult PosixDevice9::SetViewport(const D3DVIEWPORT9 *viewport)
{
	if (viewport == NULL) {
		return D3DERR_INVALIDCALL;
	}
	Viewport = *viewport;
	return D3D_OK;
}

RenderResult PosixDevice9::GetViewport(D3DVIEWPORT9 *viewport)
{
	if (viewport == NULL) {
		return D3DERR_INVALIDCALL;
	}
	*viewport = Viewport;
	return D3D_OK;
}

RenderResult PosixDevice9::SetMaterial(const D3DMATERIAL9 *material)
{
	if (material == NULL) {
		return D3DERR_INVALIDCALL;
	}
	Material = *material;
	return D3D_OK;
}

RenderResult PosixDevice9::SetLight(RenderUInt32 index, const D3DLIGHT9 *light)
{
	if (index >= LIGHT_COUNT || light == NULL) {
		return D3DERR_INVALIDCALL;
	}
	Lights[index] = *light;
	return D3D_OK;
}

RenderResult PosixDevice9::LightEnable(RenderUInt32 index, int enable)
{
	if (index >= LIGHT_COUNT) {
		return D3DERR_INVALIDCALL;
	}
	LightsEnabled[index] = enable != 0;
	return D3D_OK;
}

RenderResult PosixDevice9::SetClipPlane(RenderUInt32 index, const float *plane)
{
	if (index >= CLIP_PLANE_COUNT || plane == NULL) {
		return D3DERR_INVALIDCALL;
	}
	memcpy(ClipPlanes[index], plane, sizeof(ClipPlanes[index]));
	return D3D_OK;
}

RenderResult PosixDevice9::SetRenderState(D3DRENDERSTATETYPE state, RenderUInt32 value)
{
	if ((unsigned int)state >= RENDER_STATE_COUNT) {
		return D3DERR_INVALIDCALL;
	}
	RenderStates[state] = value;
	return D3D_OK;
}

RenderResult PosixDevice9::GetRenderState(D3DRENDERSTATETYPE state, RenderUInt32 *value)
{
	if ((unsigned int)state >= RENDER_STATE_COUNT || value == NULL) {
		return D3DERR_INVALIDCALL;
	}
	*value = RenderStates[state];
	return D3D_OK;
}

RenderResult PosixDevice9::GetTexture(RenderUInt32 stage, IDirect3DBaseTexture9 **texture)
{
	if (stage >= SAMPLER_COUNT) {
		return D3DERR_INVALIDCALL;
	}
	return Posix_Hand_Out(Textures[stage], texture);
}

RenderResult PosixDevice9::SetTexture(RenderUInt32 stage, IDirect3DBaseTexture9 *texture)
{
	if (stage >= SAMPLER_COUNT) {
		return D3DERR_INVALIDCALL;
	}
	Posix_Bind(Textures[stage], texture);
	return D3D_OK;
}

RenderResult PosixDevice9::GetTextureStageState(RenderUInt32 stage, D3DTEXTURESTAGESTATETYPE type, RenderUInt32 *value)
{
	if (stage >= TEXTURE_STAGE_COUNT || (unsigned int)type >= TEXTURE_STAGE_STATE_COUNT || value == NULL) {
		return D3DERR_INVALIDCALL;
	}
	*value = TextureStageStates[stage][type];
	return D3D_OK;
}

RenderResult PosixDevice9::SetTextureStageState(RenderUInt32 stage, D3DTEXTURESTAGESTATETYPE type, RenderUInt32 value)
{
	if (stage >= TEXTURE_STAGE_COUNT || (unsigned int)type >= TEXTURE_STAGE_STATE_COUNT) {
		return D3DERR_INVALIDCALL;
	}
	TextureStageStates[stage][type] = value;
	return D3D_OK;
}

RenderResult PosixDevice9::SetSamplerState(RenderUInt32 sampler, D3DSAMPLERSTATETYPE type, RenderUInt32 value)
{
	if (sampler >= SAMPLER_COUNT || (unsigned int)type >= SAMPLER_STATE_COUNT) {
		return D3DERR_INVALIDCALL;
	}
	SamplerStates[sampler][type] = value;
	return D3D_OK;
}

RenderResult PosixDevice9::ValidateDevice(RenderUInt32 *passes)
{
	if (passes != NULL) {
		*passes = 1;
	}
	return D3D_OK;
}

RenderResult PosixDevice9::SetSoftwareVertexProcessing(int)
{
	return D3D_OK;
}

//-------------------------------------------------------------------------------------------------
// Draws.
//-------------------------------------------------------------------------------------------------

RenderResult PosixDevice9::Draw_Unavailable(const char *what)
{
	if (SunActive) SunFailed = true;
	// -headless: Windows draws into a hidden window nobody sees, so succeeding and drawing nothing is
	// the same effect (decision 8's refinement, 2026-09-26).
	if (Window == NULL) {
		return D3D_OK;
	}
	static bool said = false;
	if (!said) {
		said = true;
		fprintf(stderr, "PosixDevice9::%s: there is no draw into a window until the SDL3 GPU draw (A3); refused\n", what);
	}
	return D3DERR_INVALIDCALL;
}

RenderResult PosixDevice9::ProcessVertices(unsigned int, unsigned int, unsigned int, IDirect3DVertexBuffer9 *,
	IDirect3DVertexDeclaration9 *, RenderUInt32)
{
	return Draw_Unavailable("ProcessVertices");
}

//-------------------------------------------------------------------------------------------------
// Vertex input and shaders.
//-------------------------------------------------------------------------------------------------

RenderResult PosixDevice9::CreateVertexDeclaration(const D3DVERTEXELEMENT9 *elements, IDirect3DVertexDeclaration9 **declaration)
{
	if (elements == NULL || declaration == NULL) {
		return D3DERR_INVALIDCALL;
	}
	*declaration = new PosixVertexDeclaration9(elements);
	return D3D_OK;
}

RenderResult PosixDevice9::SetVertexDeclaration(IDirect3DVertexDeclaration9 *declaration)
{
	Posix_Bind(Declaration, declaration);
	DeclarationIsCurrent = declaration != NULL;
	return D3D_OK;
}

RenderResult PosixDevice9::SetFVF(RenderUInt32 fvf)
{
	FVF = fvf;
	DeclarationIsCurrent = false;
	return D3D_OK;
}

void PosixDevice_Keep_D3D8_Declaration(void *declaration, const unsigned int *d3d8_tokens)
{
	if (declaration == NULL || d3d8_tokens == NULL) {
		return;
	}
	PosixVertexDeclaration9 *ours = static_cast<PosixVertexDeclaration9 *>(static_cast<IDirect3DVertexDeclaration9 *>(declaration));
	ours->D3D8Tokens.clear();
	const RenderUInt32 D3DVSD_END_TOKEN = 0xFFFFFFFF;
	do {
		ours->D3D8Tokens.push_back(*d3d8_tokens);
	} while (*d3d8_tokens++ != D3DVSD_END_TOKEN);
}

void PosixDevice9::Declaration_D3D8_Tokens_Of(IDirect3DVertexDeclaration9 *declaration, std::vector<RenderUInt32> &tokens)
{
	tokens.clear();
	if (declaration != NULL) {
		tokens = static_cast<PosixVertexDeclaration9 *>(declaration)->D3D8Tokens;
	}
}

void PosixDevice_Name_Shader(const void *shader, const char *name)
{
	std::lock_guard<std::mutex> hold(LiveShadersLock);
	std::map<const void *, PosixShaderName *>::iterator found = LiveShaders.find(shader);
	if (found != LiveShaders.end() && name != NULL) {
		found->second->EngineName = name;
	}
}

bool PosixDevice9::Shader_Tokens_Of(const void *shader, std::vector<RenderUInt32> &tokens)
{
	std::lock_guard<std::mutex> hold(LiveShadersLock);
	std::map<const void *, PosixShaderName *>::const_iterator found = LiveShaders.find(shader);
	if (found == LiveShaders.end()) {
		return false;
	}
	tokens = found->second->Tokens;
	return true;
}

void PosixDevice9::Declaration_Elements_Of(IDirect3DVertexDeclaration9 *declaration, std::vector<D3DVERTEXELEMENT9> &elements)
{
	elements.clear();
	if (declaration != NULL) {
		const std::vector<D3DVERTEXELEMENT9> &all = static_cast<PosixVertexDeclaration9 *>(declaration)->Elements;
		elements.assign(all.begin(), all.end() - 1);	// without D3DDECL_END
	}
}

std::string PosixDevice9::Engine_Name_Of(const void *shader)
{
	std::lock_guard<std::mutex> hold(LiveShadersLock);
	std::map<const void *, PosixShaderName *>::const_iterator found = LiveShaders.find(shader);
	return found != LiveShaders.end() ? found->second->EngineName : std::string();
}

RenderResult PosixDevice9::CreateVertexShader(const RenderUInt32 *function, IDirect3DVertexShader9 **shader)
{
	if (function == NULL || shader == NULL) {
		return D3DERR_INVALIDCALL;
	}
	*shader = new PosixShader9<IDirect3DVertexShader9>(function);
	return D3D_OK;
}

RenderResult PosixDevice9::SetVertexShader(IDirect3DVertexShader9 *shader)
{
	Posix_Bind(VertexShader, shader);
	return D3D_OK;
}

RenderResult PosixDevice9::GetVertexShader(IDirect3DVertexShader9 **shader)
{
	return Posix_Hand_Out(VertexShader, shader);
}

RenderResult PosixDevice9::SetVertexShaderConstantF(unsigned int start, const float *data, unsigned int count)
{
	if (data == NULL || start > VERTEX_SHADER_CONSTANT_COUNT || count > VERTEX_SHADER_CONSTANT_COUNT - start) {
		return D3DERR_INVALIDCALL;
	}
	memcpy(VertexShaderConstants[start], data, count * sizeof(VertexShaderConstants[0]));
	return D3D_OK;
}

RenderResult PosixDevice9::SetStreamSource(unsigned int stream, IDirect3DVertexBuffer9 *buffer, unsigned int offset,
	unsigned int stride)
{
	if (stream >= STREAM_COUNT) {
		return D3DERR_INVALIDCALL;
	}
	Posix_Bind(Streams[stream], buffer);
	StreamOffsets[stream] = offset;
	StreamStrides[stream] = stride;
	return D3D_OK;
}

RenderResult PosixDevice9::SetIndices(IDirect3DIndexBuffer9 *buffer)
{
	Posix_Bind(Indices, buffer);
	return D3D_OK;
}

RenderResult PosixDevice9::GetIndices(IDirect3DIndexBuffer9 **buffer)
{
	return Posix_Hand_Out(Indices, buffer);
}

RenderResult PosixDevice9::CreatePixelShader(const RenderUInt32 *function, IDirect3DPixelShader9 **shader)
{
	if (function == NULL || shader == NULL) {
		return D3DERR_INVALIDCALL;
	}
	*shader = new PosixShader9<IDirect3DPixelShader9>(function);
	return D3D_OK;
}

RenderResult PosixDevice9::SetPixelShader(IDirect3DPixelShader9 *shader)
{
	Posix_Bind(PixelShader, shader);
	return D3D_OK;
}

RenderResult PosixDevice9::GetPixelShader(IDirect3DPixelShader9 **shader)
{
	return Posix_Hand_Out(PixelShader, shader);
}

RenderResult PosixDevice9::SetPixelShaderConstantF(unsigned int start, const float *data, unsigned int count)
{
	if (data == NULL || start > PIXEL_SHADER_CONSTANT_COUNT || count > PIXEL_SHADER_CONSTANT_COUNT - start) {
		return D3DERR_INVALIDCALL;
	}
	memcpy(PixelShaderConstants[start], data, count * sizeof(PixelShaderConstants[0]));
	return D3D_OK;
}
