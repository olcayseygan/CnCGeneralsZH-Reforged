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
// No window or game is run. The linker wrappers are confined to this Linux selfcheck.
#include "PosixDevice9.h"
#include "SdlGpuFrame.h"
#include "SdlResourceMirror.h"
#include <SDL3/SDL.h>
#include <cstdio>
#include <cstring>
#include <vector>

static int failures = 0;
#define CHECK(condition) \
	do { if (!(condition)) { ++failures; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); } } while (0)

static bool fail_acquire = false;
static int fail_texture = 0;
static bool fail_all_textures = false;
static unsigned texture_calls = 0, acquire_calls = 0;
extern "C" SDL_GPUCommandBuffer *__real_SDL_AcquireGPUCommandBuffer(SDL_GPUDevice *);
extern "C" SDL_GPUTexture *__real_SDL_CreateGPUTexture(SDL_GPUDevice *, const SDL_GPUTextureCreateInfo *);
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
	check_capture_reset(device);
	check_capture_restore_allocation(device);
	device->Release(); d3d->Release(); SDL_Quit();
	printf("posix_reset_selfcheck: %s\n", failures ? "FAILED" : "PASS: acquire/allocation failures retain reset state and queued uploads; retry succeeds");
	return failures ? 1 : 0;
}
