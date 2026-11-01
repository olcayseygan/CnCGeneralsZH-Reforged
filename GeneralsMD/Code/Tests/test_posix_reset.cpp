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

// Faults at the SDL allocation/acquire boundary exercise the actual reset, batch and mirror code.
// No window or game is run. SDL wrappers are confined to this selfcheck.
#include "PosixDevice9.h"
#include "PosixResources9.h"
#include "SdlGpuFrame.h"
#include "SdlResourceMirror.h"
#include "../Libraries/Source/WWVegas/WW3D2/sdl3shadercompile.h"
#include <SDL3/SDL.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

static int failures = 0;
#define CHECK(condition) \
	do { if (!(condition)) { ++failures; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); } } while (0)

static bool fail_acquire = false;
static int fail_texture = 0;
static bool fail_all_textures = false;
static unsigned texture_calls = 0, acquire_calls = 0;
static bool fail_sampler = false, fail_transfer = false, fail_map = false, fail_copy = false, fail_submit = false;
static unsigned sampler_calls = 0, transfer_calls = 0, map_calls = 0, copy_calls = 0, submit_calls = 0;
// On Mach-O, test-only renderer objects call __wrap_ names and these aliases call SDL itself.
// ELF's --wrap supplies the __real_ names at link time.
#if defined(__APPLE__)
#define REAL_SDL(name) __asm__("_" #name)
#else
#define REAL_SDL(name)
#endif
extern "C" SDL_GPUCommandBuffer *__real_SDL_AcquireGPUCommandBuffer(SDL_GPUDevice *) REAL_SDL(SDL_AcquireGPUCommandBuffer);
extern "C" SDL_GPUTexture *__real_SDL_CreateGPUTexture(SDL_GPUDevice *, const SDL_GPUTextureCreateInfo *) REAL_SDL(SDL_CreateGPUTexture);
extern "C" SDL_GPUSampler *__real_SDL_CreateGPUSampler(SDL_GPUDevice *, const SDL_GPUSamplerCreateInfo *) REAL_SDL(SDL_CreateGPUSampler);
extern "C" SDL_GPUTransferBuffer *__real_SDL_CreateGPUTransferBuffer(SDL_GPUDevice *, const SDL_GPUTransferBufferCreateInfo *) REAL_SDL(SDL_CreateGPUTransferBuffer);
extern "C" void *__real_SDL_MapGPUTransferBuffer(SDL_GPUDevice *, SDL_GPUTransferBuffer *, bool) REAL_SDL(SDL_MapGPUTransferBuffer);
extern "C" SDL_GPUCopyPass *__real_SDL_BeginGPUCopyPass(SDL_GPUCommandBuffer *) REAL_SDL(SDL_BeginGPUCopyPass);
extern "C" bool __real_SDL_SubmitGPUCommandBuffer(SDL_GPUCommandBuffer *) REAL_SDL(SDL_SubmitGPUCommandBuffer);
extern "C" SDL_GPUCommandBuffer *__wrap_SDL_AcquireGPUCommandBuffer(SDL_GPUDevice *device)
{
	++acquire_calls;
	if (fail_acquire) {
		SDL_SetError("reset selfcheck: command acquisition refused");
		return NULL;
	}
	return __real_SDL_AcquireGPUCommandBuffer(device);
}
extern "C" SDL_GPUTexture *__wrap_SDL_CreateGPUTexture(SDL_GPUDevice *device, const SDL_GPUTextureCreateInfo *info)
{
	++texture_calls;
	if (fail_all_textures || (fail_texture && --fail_texture == 0)) {
		SDL_SetError("reset selfcheck: target allocation refused");
		return NULL;
	}
	return __real_SDL_CreateGPUTexture(device, info);
}
extern "C" SDL_GPUSampler *__wrap_SDL_CreateGPUSampler(SDL_GPUDevice *device, const SDL_GPUSamplerCreateInfo *info)
{
	++sampler_calls;
	if (fail_sampler) { SDL_SetError("GPU selfcheck: sampler allocation refused"); return NULL; }
	return __real_SDL_CreateGPUSampler(device, info);
}
extern "C" SDL_GPUTransferBuffer *__wrap_SDL_CreateGPUTransferBuffer(SDL_GPUDevice *device, const SDL_GPUTransferBufferCreateInfo *info)
{
	++transfer_calls;
	if (fail_transfer) { SDL_SetError("GPU selfcheck: transfer allocation refused"); return NULL; }
	return __real_SDL_CreateGPUTransferBuffer(device, info);
}
extern "C" void *__wrap_SDL_MapGPUTransferBuffer(SDL_GPUDevice *device, SDL_GPUTransferBuffer *transfer, bool cycle)
{
	++map_calls;
	if (fail_map) { SDL_SetError("GPU selfcheck: transfer mapping refused"); return NULL; }
	return __real_SDL_MapGPUTransferBuffer(device, transfer, cycle);
}
extern "C" SDL_GPUCopyPass *__wrap_SDL_BeginGPUCopyPass(SDL_GPUCommandBuffer *commands)
{
	++copy_calls;
	if (fail_copy) { SDL_SetError("GPU selfcheck: copy pass refused"); return NULL; }
	return __real_SDL_BeginGPUCopyPass(commands);
}
extern "C" bool __wrap_SDL_SubmitGPUCommandBuffer(SDL_GPUCommandBuffer *commands)
{
	++submit_calls;
	if (fail_submit) {
		SDL_CancelGPUCommandBuffer(commands);
		SDL_SetError("GPU selfcheck: submission refused");
		return false;
	}
	return __real_SDL_SubmitGPUCommandBuffer(commands);
}

// A full-screen triangle consumes both kinds of resource mirror, so a later upload cannot
// silently overwrite the bytes of a draw retained after command acquisition fails.
class MirrorDraw
{
public:
	SdlGpuFrame *Frame;
	SDL_GPUShader *Vertex = NULL, *Pixel = NULL;
	SDL_GPUGraphicsPipeline *Pipeline = NULL;
	SDL_GPUSampler *Sampler = NULL;
	PosixTexture9 *Texture = NULL;
	PosixVertexBuffer9 *Buffer = NULL;
	explicit MirrorDraw(SdlGpuFrame *frame) : Frame(frame)
	{
		const char *vertex =
			"struct Output { float4 position:SV_Position; float4 colour:TEXCOORD0; };\n"
			"Output main(float4 colour:TEXCOORD0, uint id:SV_VertexID) { Output o;\n"
			"o.position=float4(id==1?3.0:-1.0,id==2?3.0:-1.0,0,1); o.colour=colour; return o; }";
		const char *pixel =
			"Texture2D Image:register(t0,space2); SamplerState ImageSampler:register(s0,space2);\n"
			"float4 main(float4 colour:TEXCOORD0):SV_Target0 {\n"
			"return Image.SampleLevel(ImageSampler,float2(0.5,0.5),0)*colour; }";
		std::vector<unsigned char> spirv;
		std::string error;
		CHECK(SDL3_Compile_HLSL_To_SPIRV(vertex, true, spirv, error));
		Vertex = SDL3_Create_Shader(frame->Device(), spirv, true, error); CHECK(Vertex);
		CHECK(SDL3_Compile_HLSL_To_SPIRV(pixel, false, spirv, error));
		Pixel = SDL3_Create_Shader(frame->Device(), spirv, false, error); CHECK(Pixel);
		SDL_GPUVertexBufferDescription buffer = {};
		buffer.pitch = sizeof(float) * 4;
		buffer.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
		SDL_GPUVertexAttribute attribute = {};
		attribute.format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4;
		SDL_GPUColorTargetDescription colour = {};
		colour.format = (SDL_GPUTextureFormat)SdlGpuFrame::Target_Format();
		SDL_GPUGraphicsPipelineCreateInfo info = {};
		info.vertex_shader = Vertex; info.fragment_shader = Pixel;
		info.vertex_input_state.vertex_buffer_descriptions = &buffer;
		info.vertex_input_state.num_vertex_buffers = 1;
		info.vertex_input_state.vertex_attributes = &attribute;
		info.vertex_input_state.num_vertex_attributes = 1;
		info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
		info.target_info.color_target_descriptions = &colour;
		info.target_info.num_color_targets = 1;
		info.target_info.has_depth_stencil_target = true;
		info.target_info.depth_stencil_format = (SDL_GPUTextureFormat)frame->Depth_Format();
		if (Vertex && Pixel) Pipeline = SDL_CreateGPUGraphicsPipeline(frame->Device(), &info);
		CHECK(Pipeline);
		SDL_GPUSamplerCreateInfo sampler = {};
		sampler.address_mode_u = sampler.address_mode_v = sampler.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
		Sampler = SDL_CreateGPUSampler(frame->Device(), &sampler); CHECK(Sampler);
		Texture = PosixTexture9::create(1, 1, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED); CHECK(Texture);
		Buffer = PosixVertexBuffer9::create(sizeof(float) * 12, 0, 0, D3DPOOL_MANAGED); CHECK(Buffer);
	}
	~MirrorDraw()
	{
		Frame->Flush();
		if (Texture) Texture->Release();
		if (Buffer) Buffer->Release();
		if (Sampler) SDL_ReleaseGPUSampler(Frame->Device(), Sampler);
		if (Pipeline) SDL_ReleaseGPUGraphicsPipeline(Frame->Device(), Pipeline);
		if (Pixel) SDL_ReleaseGPUShader(Frame->Device(), Pixel);
		if (Vertex) SDL_ReleaseGPUShader(Frame->Device(), Vertex);
	}
	void Set_Texture(uint32_t colour)
	{
		D3DLOCKED_RECT locked = {};
		CHECK(Texture->LockRect(0, &locked, NULL, 0) == D3D_OK);
		if (locked.pBits) *(uint32_t *)locked.pBits = colour;
		CHECK(Texture->UnlockRect(0) == D3D_OK);
	}
	void Set_Buffer(float red, float green, float blue)
	{
		float *bytes = NULL;
		CHECK(Buffer->Lock(0, 0, (void **)&bytes, 0) == D3D_OK);
		if (bytes) for (unsigned i = 0; i < 3; ++i) {
			bytes[i * 4] = red; bytes[i * 4 + 1] = green;
			bytes[i * 4 + 2] = blue; bytes[i * 4 + 3] = 1;
		}
		CHECK(Buffer->Unlock() == D3D_OK);
	}
	void Draw(SDL_GPUTexture *texture, SDL_GPUBuffer *buffer)
	{
		Frame->Set_Target(Frame->Back_Buffer_Target());
		SdlRecordedDraw draw = {};
		draw.Pipeline = Pipeline; draw.VertexBuffer = buffer; draw.Count = 3;
		draw.SamplerCount = 1; draw.Textures[0] = texture; draw.Samplers[0] = Sampler;
		draw.Viewport[2] = Frame->Width(); draw.Viewport[3] = Frame->Height(); draw.Viewport[5] = 1;
		draw.Scissor[2] = Frame->Width(); draw.Scissor[3] = Frame->Height();
		Frame->Record_Draw(draw);
	}
	void Check_Colour(uint32_t expected)
	{
		std::vector<uint8_t> pixels;
		CHECK(Frame->Read_Back(Frame->Back_Buffer(), Frame->Width(), Frame->Height(), pixels));
		CHECK(pixels.size() == (size_t)Frame->Width() * Frame->Height() * 4);
		bool equal = !pixels.empty();
		for (size_t i = 0; i + 3 < pixels.size(); i += 4)
			for (unsigned channel = 0; channel < 4; ++channel)
				equal = equal && pixels[i + channel] == ((expected >> (channel * 8)) & 255);
		CHECK(equal);
	}
};

static void check_mirror_update_failure(PosixDevice9 *device, bool texture_update)
{
	SdlGpuFrame *frame = device->Get_Gpu();
	SdlResourceMirrors *mirrors = device->Get_Mirrors();
	CHECK(frame->Flush());
	MirrorDraw draw(frame);
	if (!draw.Texture || !draw.Buffer || !draw.Pipeline || !draw.Sampler) return;
	draw.Set_Texture(texture_update ? 0xFF123456 : 0xFFFFFFFF);
	draw.Set_Buffer(1, texture_update ? 1 : 0, texture_update ? 1 : 0);
	std::string refusal;
	SDL_GPUTexture *texture = mirrors->Texture(draw.Texture, refusal); CHECK(texture);
	SDL_GPUBuffer *buffer = mirrors->Buffer(draw.Buffer, draw.Buffer->storage(), refusal); CHECK(buffer);
	if (!texture || !buffer) return;
	draw.Draw(texture, buffer);
	const uint64_t batch = frame->Batch();
	const unsigned uploads = texture_update ? mirrors->Textures_Uploaded() : mirrors->Buffers_Uploaded();
	const unsigned flushes = mirrors->Stale_Flushes();
	if (texture_update) draw.Set_Texture(0xFF654321);
	else draw.Set_Buffer(0, 1, 0);
	fail_acquire = true; acquire_calls = 0;
	// Retrying while the queue is unavailable must not accept/cache B or mark a new batch used.
	for (unsigned attempt = 0; attempt < 2; ++attempt) {
		refusal.clear();
		if (texture_update) CHECK(mirrors->Texture(draw.Texture, refusal) == NULL);
		else CHECK(mirrors->Buffer(draw.Buffer, draw.Buffer->storage(), refusal) == NULL);
		CHECK(refusal.find("flush") != std::string::npos);
		CHECK(refusal.find("command acquisition refused") != std::string::npos);
		CHECK(frame->Batch() == batch);
		CHECK((texture_update ? mirrors->Textures_Uploaded() : mirrors->Buffers_Uploaded()) == uploads);
		CHECK(mirrors->Stale_Flushes() == flushes);
	}
	fail_acquire = false;
	CHECK(acquire_calls == 2);
	draw.Check_Colour(texture_update ? 0xFF123456 : 0xFFFF0000); // retained draw still sees A
	if (texture_update) CHECK(mirrors->Texture(draw.Texture, refusal) == texture);
	else CHECK(mirrors->Buffer(draw.Buffer, draw.Buffer->storage(), refusal) == buffer);
	CHECK((texture_update ? mirrors->Textures_Uploaded() : mirrors->Buffers_Uploaded()) == uploads + 1);
	draw.Draw(texture, buffer);
	draw.Check_Colour(texture_update ? 0xFF654321 : 0xFF00FF00);
	// B is current after recovery; recording it again must reuse the accepted upload.
	if (texture_update) CHECK(mirrors->Texture(draw.Texture, refusal) == texture);
	else CHECK(mirrors->Buffer(draw.Buffer, draw.Buffer->storage(), refusal) == buffer);
	CHECK((texture_update ? mirrors->Textures_Uploaded() : mirrors->Buffers_Uploaded()) == uploads + 1);
}

static void check_state(PosixDevice9 *device, const D3DPRESENT_PARAMETERS &parameters,
	const D3DVIEWPORT9 &viewport, IDirect3DSurface9 *back, IDirect3DSurface9 *depth,
	IDirect3DSurface9 *target, const SdlTarget &gpu_target)
{
	const D3DPRESENT_PARAMETERS &current = device->Get_Present_Parameters();
	CHECK(memcmp(&current, &parameters, sizeof(current)) == 0);
	D3DVIEWPORT9 got;
	CHECK(device->GetViewport(&got) == D3D_OK);
	CHECK(memcmp(&got, &viewport, sizeof(got)) == 0);
	IDirect3DSurface9 *surface = NULL;
	CHECK(device->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &surface) == D3D_OK);
	CHECK(surface == back); if (surface) surface->Release();
	CHECK(device->GetDepthStencilSurface(&surface) == D3D_OK);
	CHECK(surface == depth); if (surface) surface->Release();
	CHECK(device->GetRenderTarget(0, &surface) == D3D_OK);
	CHECK(surface == target); if (surface) surface->Release();
	const SdlTarget gpu = device->Get_Gpu()->Back_Buffer_Target();
	CHECK(gpu.Colour == gpu_target.Colour && gpu.Depth == gpu_target.Depth);
	CHECK(gpu.Width == gpu_target.Width && gpu.Height == gpu_target.Height);
}

static void check_reset(PosixDevice9 *device)
{
	const D3DPRESENT_PARAMETERS original = device->Get_Present_Parameters();
	D3DPRESENT_PARAMETERS next = original;
	next.BackBufferWidth = 48; next.BackBufferHeight = 40;
	IDirect3DSurface9 *back = NULL, *depth = NULL, *target = NULL;
	CHECK(device->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &back) == D3D_OK);
	CHECK(device->GetDepthStencilSurface(&depth) == D3D_OK);
	CHECK(device->CreateRenderTarget(32, 32, D3DFMT_A8R8G8B8, D3DMULTISAMPLE_NONE, 0, false,
		&target, NULL) == D3D_OK);
	CHECK(device->SetRenderTarget(0, target) == D3D_OK);
	const D3DVIEWPORT9 viewport = { 2, 3, 24, 20, 0.1f, 0.9f };
	CHECK(device->SetViewport(&viewport) == D3D_OK);
	SdlGpuFrame *frame = device->Get_Gpu();
	const SdlTarget old_gpu = frame->Back_Buffer_Target();
	// Queue a real mirror upload plus a clear referencing a standalone target held by the device.
	IDirect3DTexture9 *texture = NULL;
	CHECK(device->CreateTexture(1, 1, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &texture, NULL) == D3D_OK);
	D3DLOCKED_RECT locked;
	CHECK(texture->LockRect(0, &locked, NULL, 0) == D3D_OK);
	*(uint32_t *)locked.pBits = 0xFF123456;
	CHECK(texture->UnlockRect(0) == D3D_OK);
	std::string refusal;
	SDL_GPUTexture *mirror = device->Get_Mirrors()->Texture(texture, refusal);
	CHECK(mirror != NULL);
	CHECK(device->Clear(0, NULL, D3DCLEAR_TARGET, 0xFFABCDEF, 1, 0) == D3D_OK);
	SDL_GPUTexture *target_gpu = device->Get_Mirrors()->Surface(target, false, refusal);
	CHECK(target_gpu != NULL);
	target->Release(); // The device binding is now the standalone target's only owner.
	const uint64_t batch = frame->Batch();
	const unsigned uploaded = device->Get_Mirrors()->Textures_Uploaded();
	texture_calls = 0; acquire_calls = 0; fail_acquire = true;
	CHECK(device->Reset(&next) == D3DERR_DEVICELOST);
	fail_acquire = false;
	CHECK(acquire_calls == 1 && texture_calls == 0);
	CHECK(frame->Batch() == batch);
	check_state(device, original, viewport, back, depth, target, old_gpu);
	std::vector<uint8_t> pixels;
	CHECK(frame->Read_Back(target_gpu, 32, 32, pixels));
	CHECK(pixels.size() == 32 * 32 * 4);
	if (pixels.size() == 32 * 32 * 4) {
		const uint8_t *pixel = &pixels[(3 * 32 + 2) * 4];
		CHECK(pixel[0] == 0xEF && pixel[1] == 0xCD && pixel[2] == 0xAB);
	}
	CHECK(frame->Read_Back(mirror, 1, 1, pixels));
	if (pixels.size() == 4) CHECK(pixels[0] == 0x56 && pixels[1] == 0x34 && pixels[2] == 0x12);
	else CHECK(false);
	CHECK(device->Get_Mirrors()->Texture(texture, refusal) == mirror);
	CHECK(device->Get_Mirrors()->Textures_Uploaded() == uploaded);
	CHECK(frame->Flush());

	for (int allocation = 1; allocation <= 3; ++allocation) {
		CHECK(device->Clear(0, NULL, D3DCLEAR_TARGET, 0xFF102030, 1, 0) == D3D_OK);
		texture_calls = 0; fail_texture = allocation;
		CHECK(device->Reset(&next) == D3DERR_OUTOFVIDEOMEMORY);
		fail_texture = 0;
		CHECK(texture_calls == 3);
		check_state(device, original, viewport, back, depth, target, old_gpu);
		CHECK(frame->Read_Back(target_gpu, 32, 32, pixels));
		if (pixels.size() == 32 * 32 * 4) {
			const uint8_t *pixel = &pixels[(3 * 32 + 2) * 4];
			CHECK(pixel[0] == 0x30 && pixel[1] == 0x20 && pixel[2] == 0x10);
		}
	}
	// Invalid CPU surface formats fail before the queue is touched.
	D3DPRESENT_PARAMETERS invalid = next;
	invalid.AutoDepthStencilFormat = D3DFMT_A8R8G8B8;
	acquire_calls = 0;
	CHECK(device->Reset(&invalid) == D3DERR_INVALIDCALL);
	CHECK(acquire_calls == 0);
	check_state(device, original, viewport, back, depth, target, old_gpu);
	CHECK(device->Reset(&next) == D3D_OK);
	CHECK(frame->Width() == 48 && frame->Height() == 40);
	D3DVIEWPORT9 got;
	CHECK(device->GetViewport(&got) == D3D_OK);
	CHECK(got.X == 0 && got.Y == 0 && got.Width == 48 && got.Height == 40 && got.MinZ == 0 && got.MaxZ == 1);
	CHECK(device->Clear(0, NULL, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, 0xFF654321, 1, 0) == D3D_OK);
	CHECK(frame->Read_Back(frame->Back_Buffer(), 48, 40, pixels));
	if (!pixels.empty()) CHECK(pixels[0] == 0x21 && pixels[1] == 0x43 && pixels[2] == 0x65);
	back->Release(); depth->Release(); texture->Release();
}

static void check_capture_reset(PosixDevice9 *device)
{
	D3DPRESENT_PARAMETERS next = device->Get_Present_Parameters();
	next.BackBufferWidth += 4;
	CHECK(device->Begin_Sun_Shadow(32));
	// A refused caster makes the image unusable, but successful draining still permits Reset.
	CHECK(device->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 1, NULL, sizeof(float) * 3) == D3DERR_INVALIDCALL);
	CHECK(device->Reset(&next) == D3D_OK);
	const D3DPRESENT_PARAMETERS original = device->Get_Present_Parameters();
	IDirect3DSurface9 *back = NULL, *depth = NULL;
	CHECK(device->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &back) == D3D_OK);
	CHECK(device->GetDepthStencilSurface(&depth) == D3D_OK);
	D3DVIEWPORT9 viewport;
	CHECK(device->GetViewport(&viewport) == D3D_OK);
	SdlGpuFrame *frame = device->Get_Gpu();
	const SdlTarget old_gpu = frame->Back_Buffer_Target();
	CHECK(device->Begin_Sun_Shadow(32));
	const uint64_t batch = frame->Batch();
	next.BackBufferWidth += 4;
	fail_acquire = true; texture_calls = 0; acquire_calls = 0;
	CHECK(device->Reset(&next) == D3DERR_DEVICELOST);
	fail_acquire = false;
	CHECK(acquire_calls == 1 && texture_calls == 0 && frame->Batch() == batch);
	// The failed reset ends the active capture and restores its scene bindings/viewport.
	check_state(device, original, viewport, back, depth, back, old_gpu);
	CHECK(device->Reset(&next) == D3D_OK);
	CHECK(device->Clear(0, NULL, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, 0xFF765432, 1, 0) == D3D_OK);
	std::vector<uint8_t> pixels;
	CHECK(frame->Read_Back(frame->Back_Buffer(), next.BackBufferWidth, next.BackBufferHeight, pixels));
	if (!pixels.empty()) CHECK(pixels[0] == 0x32 && pixels[1] == 0x54 && pixels[2] == 0x76);
	back->Release(); depth->Release();
}

static void check_capture_restore_allocation(PosixDevice9 *device)
{
	for (int replacement_fails = 0; replacement_fails <= 1; ++replacement_fails) {
		const D3DPRESENT_PARAMETERS original = device->Get_Present_Parameters();
		D3DPRESENT_PARAMETERS next = original;
		next.BackBufferWidth += 4;
		IDirect3DSurface9 *back = NULL, *depth = NULL, *target = NULL;
		CHECK(device->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &back) == D3D_OK);
		CHECK(device->GetDepthStencilSurface(&depth) == D3D_OK);
		CHECK(device->CreateRenderTarget(original.BackBufferWidth, original.BackBufferHeight,
			D3DFMT_A8R8G8B8, D3DMULTISAMPLE_NONE, 0, false, &target, NULL) == D3D_OK);
		CHECK(device->SetRenderTarget(0, target) == D3D_OK);
		target->Release(); // Deliberately no mirror yet; only the scene binding owns the surface.
		D3DVIEWPORT9 viewport;
		CHECK(device->GetViewport(&viewport) == D3D_OK);
		SdlGpuFrame *frame = device->Get_Gpu();
		const SdlTarget old_gpu = frame->Back_Buffer_Target();
		CHECK(device->Begin_Sun_Shadow(32));
		texture_calls = 0; fail_texture = 1; fail_all_textures = replacement_fails;
		const RenderResult result = device->Reset(&next);
		fail_texture = 0; fail_all_textures = false;
		CHECK(texture_calls == 4); // Scene mirror restoration, then all three replacement attachments.
		CHECK(result == (replacement_fails ? D3DERR_OUTOFVIDEOMEMORY : D3D_OK));
		if (replacement_fails) {
			check_state(device, original, viewport, back, depth, target, old_gpu);
			// Restoration left the GPU's current target at the sun capture. A device clear must resolve
			// the retained CPU scene binding again before recording, and the sun target stays alive.
			CHECK(device->Clear(0, NULL, D3DCLEAR_TARGET, 0xFF2468AC, 1, 0) == D3D_OK);
			std::string refusal;
			SDL_GPUTexture *scene = device->Get_Mirrors()->Surface(target, false, refusal);
			CHECK(scene != NULL);
			std::vector<uint8_t> pixels;
			CHECK(frame->Read_Back(scene, original.BackBufferWidth, original.BackBufferHeight, pixels));
			if (!pixels.empty()) CHECK(pixels[0] == 0xAC && pixels[1] == 0x68 && pixels[2] == 0x24);
			CHECK(device->Reset(&next) == D3D_OK);
		}
		back->Release(); depth->Release();
	}
}

static void check_gamma_pixels(SdlGpuFrame *frame, SDL_GPUTexture *target, const uint16_t (*ramp)[256])
{
	std::vector<uint8_t> pixels;
	CHECK(frame->Read_Back(target, 4, 4, pixels));
	CHECK(pixels.size() == 4 * 4 * 4);
	const int expected[4] = { (int)((ramp[2][0x10] * 255u + 32767u) / 65535u),
		(int)((ramp[1][0x20] * 255u + 32767u) / 65535u),
		(int)((ramp[0][0x40] * 255u + 32767u) / 65535u), 255 };
	for (size_t pixel = 0; pixel + 3 < pixels.size(); pixel += 4)
		for (int channel = 0; channel < 4; ++channel)
			CHECK(abs((int)pixels[pixel + channel] - expected[channel]) <= 1);
}

static void check_gamma_failure_recovery()
{
	// A new frame ensures that texture/sampler faults hit their first use. Later
	// faults replace a known curve, so caching an unsubmitted upload is observable.
	for (int fault = 0; fault < 6; ++fault) {
		std::string error;
		SdlGpuFrame *frame = SdlGpuFrame::Create(NULL, 4, 4, error);
		CHECK(frame != NULL);
		if (!frame) continue;
		SDL_GPUTextureCreateInfo info = {};
		info.type = SDL_GPU_TEXTURETYPE_2D;
		info.format = (SDL_GPUTextureFormat)SdlGpuFrame::Target_Format();
		info.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
		info.width = info.height = 4;
		info.layer_count_or_depth = info.num_levels = 1;
		SDL_GPUTexture *target = SDL_CreateGPUTexture(frame->Device(), &info);
		CHECK(target != NULL);
		if (!target) { delete frame; continue; }
		frame->Clear_Back_Buffer(true, false, false, 0xFF402010u, 1.0f, 0);
		uint16_t original[3][256], changed[3][256];
		for (int index = 0; index < 256; ++index) {
			original[0][index] = (uint16_t)((255 - index) * 257);
			original[1][index] = original[2][index] = (uint16_t)(index * 257);
			changed[0][index] = (uint16_t)(index * 257);
			changed[1][index] = (uint16_t)((255 - index) * 257);
			changed[2][index] = (uint16_t)((index / 2) * 257);
		}
		if (fault >= 2) {
			CHECK(frame->Present_To(target, 4, 4, original));
			check_gamma_pixels(frame, target, original);
		}
		const uint16_t (*requested)[256] = fault >= 2 ? changed : original;
		const unsigned samplers_before = sampler_calls;
		fail_texture = fault == 0 ? 1 : 0;
		fail_sampler = fault == 1;
		fail_transfer = fault == 2;
		fail_map = fault == 3;
		fail_copy = fault == 4;
		fail_submit = fault == 5;
		CHECK(!frame->Present_To(target, 4, 4, requested));
		fail_texture = 0;
		fail_sampler = fail_transfer = fail_map = fail_copy = fail_submit = false;
		if (fault == 0) CHECK(sampler_calls == samplers_before);
		const unsigned transfers_before = transfer_calls;
		const unsigned samplers_retry = sampler_calls;
		CHECK(frame->Present_To(target, 4, 4, requested));
		CHECK(transfer_calls == transfers_before + 1);
		if (fault <= 1) CHECK(sampler_calls == samplers_retry + 1);
		check_gamma_pixels(frame, target, requested);
		const unsigned transfers_repeated = transfer_calls;
		CHECK(frame->Present_To(target, 4, 4, requested));
		CHECK(transfer_calls == transfers_repeated);
		check_gamma_pixels(frame, target, requested);
		if (fault >= 2) {
			CHECK(frame->Present_To(target, 4, 4, original));
			check_gamma_pixels(frame, target, original);
		}
		SDL_ReleaseGPUTexture(frame->Device(), target);
		delete frame;
	}
}

int main()
{
	IDirect3D9 *d3d = Direct3DCreate9(D3D_SDK_VERSION);
	D3DPRESENT_PARAMETERS parameters = {};
	parameters.BackBufferWidth = parameters.BackBufferHeight = 32;
	parameters.BackBufferFormat = D3DFMT_X8R8G8B8;
	parameters.Windowed = parameters.EnableAutoDepthStencil = 1;
	parameters.AutoDepthStencilFormat = D3DFMT_D24S8;
	IDirect3DDevice9 *created = NULL;
	CHECK(d3d->CreateDevice(0, D3DDEVTYPE_HAL, NULL, 0, &parameters, &created) == D3D_OK);
	if (!created) return 1;
	PosixDevice9 *device = static_cast<PosixDevice9 *>(created);
	if (!SDL_Init(SDL_INIT_VIDEO) || device->Create_Gpu_Frame(true) != D3D_OK) {
		printf("posix_reset_selfcheck: SKIP - no GPU device: %s\n", SDL_GetError());
		device->Release(); d3d->Release(); SDL_Quit();
		return failures ? 1 : 77;
	}
	check_reset(device);
	check_mirror_update_failure(device, true);
	check_mirror_update_failure(device, false);
	check_capture_reset(device);
	check_capture_restore_allocation(device);
	check_gamma_failure_recovery();
	device->Release(); d3d->Release(); SDL_Quit();
	printf("posix_reset_selfcheck: %s\n", failures ? "FAILED" : "PASS: reset, mirror updates and gamma uploads preserve valid state across failures and retries");
	return failures ? 1 : 0;
}
