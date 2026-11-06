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

// The device's fixed-function draws on this machine's GPU (A3c), through D3D9 calls only, read back
// from the back buffer:
//   - a pre-transformed quad whose edges sit a quarter pixel past an integer lands on the columns D3D9
//     covers (the half-pixel viewport), and its clockwise triangles are front-facing (D3D9's CULL_CCW);
//   - a transformed, unlit quad through the generated vertex program;
//   - textures: A8R8G8B8 as it is (four texels to four quadrants, point sampled), DXT1 as BC1,
//     X8R8G8B8 expanded with alpha 1, and an unbound slot sampling white;
//   - a fan (DrawPrimitiveUP and indexed), static and dynamic buffers, and a static buffer written
//     again after a draw used it, which must flush so both draws show what D3D9 would have shown;
//   - indexed draws from a dynamic buffer whose indices start past its first vertex (MinVertex), with
//     and without a base vertex, byte for byte the picture the same draws make from a static buffer;
//   - a clear between draws, and the depth test ordering two quads whatever order they are drawn in;
//   - partial clears as clear draws: one cut to a smaller viewport, a list of rectangles, and a depth-only
//     rectangle that a following depth-tested draw sees (and whose own state does not leak into it);
//   - a texture's GPU copy goes when the texture does (posixResourceDestroyed);
//   - a cube texture is refused, counted, and draws nothing.
//
// WHAT THIS DOES NOT PROVE: that the shading matches D3D9's beyond these solid colours.  That is the
// reference comparison's (A3b).

#include "PosixDevice9.h"
#include "SdlGpuFrame.h"
#include "SdlResourceMirror.h"
#include "SdlSunShadowDebug.h"
#include "Platform/SunShadow.h"
#include "Platform/SunSmoke.h"
#include "../Libraries/Source/WWVegas/WW3D2/ffshader.h"
#include "Platform/EngineShaderName.h"

#include <SDL3/SDL.h>

#include <stdio.h>
#include <string.h>
#include <vector>

static int failures = 0;

#define CHECK(condition) \
	do { if (!(condition)) { ++failures; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); } } while (0)

static const unsigned SIZE = 32;

struct ScreenVertex
{
	float x, y, z, rhw;
	uint32_t colour;
	float u, v;
};
static const RenderUInt32 SCREEN_FVF = D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1;

// Two clockwise triangles (D3D9's front face under CULL_CCW), as a list.
static void quad(ScreenVertex *out, float left, float top, float right, float bottom, float z, uint32_t colour)
{
	const ScreenVertex corners[4] = {
		{ left, top, z, 1.0f, colour, 0.0f, 0.0f }, { right, top, z, 1.0f, colour, 1.0f, 0.0f },
		{ left, bottom, z, 1.0f, colour, 0.0f, 1.0f }, { right, bottom, z, 1.0f, colour, 1.0f, 1.0f } };
	out[0] = corners[0]; out[1] = corners[1]; out[2] = corners[2];
	out[3] = corners[1]; out[4] = corners[3]; out[5] = corners[2];
}

static std::vector<uint8_t> pixels;

static void read_back(PosixDevice9 *device)
{
	SdlGpuFrame *gpu = device->Get_Gpu();
	CHECK(gpu->Read_Back(gpu->Back_Buffer(), SIZE, SIZE, pixels));
}

// The pixel as D3DCOLOR (A8R8G8B8).
static uint32_t at(unsigned x, unsigned y)
{
	const uint8_t *p = &pixels[(y * SIZE + x) * 4];
	return ((uint32_t)p[3] << 24) | ((uint32_t)p[2] << 16) | ((uint32_t)p[1] << 8) | p[0];
}

static bool near_colour(uint32_t a, uint32_t b)
{
	for (int shift = 0; shift < 32; shift += 8) {
		const int d = (int)((a >> shift) & 0xFF) - (int)((b >> shift) & 0xFF);
		if (d > 2 || d < -2) return false;
	}
	return true;
}

#define CHECK_PIXEL(x, y, expected) \
	do { const uint32_t got_ = at(x, y); if (!near_colour(got_ | 0xFF000000u, (expected) | 0xFF000000u)) { ++failures; \
		printf("FAIL %s:%d: pixel (%u, %u) is %08x, expected %08x\n", __FILE__, __LINE__, (unsigned)(x), (unsigned)(y), got_, (unsigned)(expected)); } } while (0)

static const uint32_t BLUE = 0xFF0000FF, RED = 0xFFFF0000, GREEN = 0xFF00FF00, WHITE = 0xFFFFFFFF, YELLOW = 0xFFFFFF00;

static void begin(PosixDevice9 *device, uint32_t colour)
{
	device->Clear(0, NULL, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, colour, 1.0f, 0);
	device->SetFVF(SCREEN_FVF);
	device->SetTexture(0, NULL);
	device->SetRenderState(D3DRS_LIGHTING, 0);
	device->SetRenderState(D3DRS_ZENABLE, D3DZB_FALSE);
	device->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG2);
	device->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG2);
}

static void check_half_pixel_and_winding(PosixDevice9 *device)
{
	begin(device, BLUE);
	ScreenVertex v[6];
	quad(v, 8.25f, 8.25f, 24.25f, 24.25f, 0.5f, RED);
	CHECK(device->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 2, v, sizeof(ScreenVertex)) == D3D_OK);
	read_back(device);
	// D3D9 covers the pixels whose integer centres lie inside: 9 to 24.
	CHECK_PIXEL(8, 16, BLUE);
	CHECK_PIXEL(9, 16, RED);
	CHECK_PIXEL(24, 16, RED);
	CHECK_PIXEL(25, 16, BLUE);
	CHECK_PIXEL(16, 8, BLUE);
	CHECK_PIXEL(16, 9, RED);
	CHECK_PIXEL(16, 24, RED);
	CHECK_PIXEL(16, 25, BLUE);

	// The same quad wound the other way is culled.
	begin(device, BLUE);
	std::swap(v[1], v[2]);
	std::swap(v[4], v[5]);
	device->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 2, v, sizeof(ScreenVertex));
	read_back(device);
	CHECK_PIXEL(16, 16, BLUE);
	device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
	device->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 2, v, sizeof(ScreenVertex));
	read_back(device);
	CHECK_PIXEL(16, 16, RED);
	device->SetRenderState(D3DRS_CULLMODE, D3DCULL_CCW);
}

static void check_transformed(PosixDevice9 *device)
{
	begin(device, BLUE);
	struct Vertex { float x, y, z; uint32_t colour; };
	// Clip space straight through: identity world, view and projection.  y is up, so the clockwise
	// order on screen is (-0.5, 0.5), (0.5, 0.5), (-0.5, -0.5).
	const Vertex v[6] = {
		{ -0.5f, 0.5f, 0.5f, GREEN }, { 0.5f, 0.5f, 0.5f, GREEN }, { -0.5f, -0.5f, 0.5f, GREEN },
		{ 0.5f, 0.5f, 0.5f, GREEN }, { 0.5f, -0.5f, 0.5f, GREEN }, { -0.5f, -0.5f, 0.5f, GREEN } };
	device->SetFVF(D3DFVF_XYZ | D3DFVF_DIFFUSE);
	device->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 2, v, sizeof(Vertex));
	read_back(device);
	CHECK_PIXEL(16, 16, GREEN);
	CHECK_PIXEL(2, 2, BLUE);
	CHECK_PIXEL(29, 29, BLUE);
}

static IDirect3DTexture9 *make_texture(PosixDevice9 *device, unsigned w, unsigned h, D3DFORMAT format, const void *bytes,
	unsigned size)
{
	IDirect3DTexture9 *texture = NULL;
	CHECK(device->CreateTexture(w, h, 1, 0, format, D3DPOOL_MANAGED, &texture, NULL) == D3D_OK);
	D3DLOCKED_RECT locked;
	CHECK(texture->LockRect(0, &locked, NULL, 0) == D3D_OK);
	memcpy(locked.pBits, bytes, size);
	texture->UnlockRect(0);
	return texture;
}

static void check_textures(PosixDevice9 *device)
{
	ScreenVertex v[6];
	// The classic D3D9 quad: edges on half pixels, so texel centres fall on pixel centres.
	quad(v, 7.5f, 7.5f, 23.5f, 23.5f, 0.5f, WHITE);

	// A8R8G8B8, four texels.
	const uint32_t texels[4] = { RED, GREEN, BLUE, YELLOW };
	IDirect3DTexture9 *argb = make_texture(device, 2, 2, D3DFMT_A8R8G8B8, texels, sizeof(texels));
	begin(device, 0xFF000000);
	device->SetTexture(0, argb);
	device->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
	device->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 2, v, sizeof(ScreenVertex));
	read_back(device);
	CHECK_PIXEL(10, 10, RED);
	CHECK_PIXEL(20, 10, GREEN);
	CHECK_PIXEL(10, 20, BLUE);
	CHECK_PIXEL(20, 20, YELLOW);

	// DXT1: one block, colour 0 pure red (RGB565 0xF800), every index 0.
	const uint8_t block[8] = { 0x00, 0xF8, 0x00, 0x00, 0, 0, 0, 0 };
	IDirect3DTexture9 *dxt = make_texture(device, 4, 4, D3DFMT_DXT1, block, sizeof(block));
	begin(device, 0xFF000000);
	device->SetTexture(0, dxt);
	device->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
	device->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 2, v, sizeof(ScreenVertex));
	read_back(device);
	CHECK_PIXEL(16, 16, RED);

	// X8R8G8B8 with its unused byte 0: alpha must read 1, so blending over the clear shows the texel.
	const uint32_t x8[1] = { 0x0000FF00 };
	IDirect3DTexture9 *xrgb = make_texture(device, 1, 1, D3DFMT_X8R8G8B8, x8, sizeof(x8));
	begin(device, BLUE);
	device->SetTexture(0, xrgb);
	device->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
	device->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
	device->SetRenderState(D3DRS_ALPHABLENDENABLE, 1);
	device->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
	device->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
	device->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 2, v, sizeof(ScreenVertex));
	device->SetRenderState(D3DRS_ALPHABLENDENABLE, 0);
	read_back(device);
	CHECK_PIXEL(16, 16, GREEN);

	// A stage that reads a texture with none bound samples white.
	begin(device, BLUE);
	device->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
	quad(v, 7.5f, 7.5f, 23.5f, 23.5f, 0.5f, YELLOW);
	device->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 2, v, sizeof(ScreenVertex));
	read_back(device);
	CHECK_PIXEL(16, 16, YELLOW);

	// The copies go with their textures.
	const size_t live = device->Get_Mirrors()->Live_Copies();
	device->SetTexture(0, NULL);
	argb->Release();
	dxt->Release();
	xrgb->Release();
	CHECK(device->Get_Mirrors()->Live_Copies() == live - 3);
}

static void check_fans_and_buffers(PosixDevice9 *device)
{
	// A fan through DrawPrimitiveUP: centre and four corners, clockwise.
	begin(device, BLUE);
	const ScreenVertex fan[6] = {
		{ 16, 16, 0.5f, 1, RED, 0, 0 }, { 4, 4, 0.5f, 1, RED, 0, 0 }, { 28, 4, 0.5f, 1, RED, 0, 0 },
		{ 28, 28, 0.5f, 1, RED, 0, 0 }, { 4, 28, 0.5f, 1, RED, 0, 0 }, { 4, 4, 0.5f, 1, RED, 0, 0 } };
	device->DrawPrimitiveUP(D3DPT_TRIANGLEFAN, 4, fan, sizeof(ScreenVertex));
	read_back(device);
	CHECK_PIXEL(10, 16, RED);
	CHECK_PIXEL(22, 16, RED);
	CHECK_PIXEL(16, 6, RED);
	CHECK_PIXEL(16, 26, RED);
	CHECK_PIXEL(1, 1, BLUE);

	// A static vertex buffer, drawn, written, and drawn again in one batch: two quads, each as it was.
	IDirect3DVertexBuffer9 *buffer = NULL;
	CHECK(device->CreateVertexBuffer(6 * sizeof(ScreenVertex), D3DUSAGE_WRITEONLY, SCREEN_FVF, D3DPOOL_MANAGED, &buffer, NULL) == D3D_OK);
	void *data = NULL;
	buffer->Lock(0, 0, &data, 0);
	quad((ScreenVertex *)data, 0, 0, 16, 16, 0.5f, RED);
	buffer->Unlock();
	begin(device, BLUE);
	device->SetStreamSource(0, buffer, 0, sizeof(ScreenVertex));
	const unsigned int stale_before = device->Get_Mirrors()->Stale_Flushes();
	device->DrawPrimitive(D3DPT_TRIANGLELIST, 0, 2);
	buffer->Lock(0, 0, &data, 0);
	quad((ScreenVertex *)data, 16, 16, 32, 32, 0.5f, GREEN);
	buffer->Unlock();
	device->DrawPrimitive(D3DPT_TRIANGLELIST, 0, 2);
	read_back(device);
	CHECK_PIXEL(8, 8, RED);
	CHECK_PIXEL(24, 24, GREEN);
	CHECK_PIXEL(24, 8, BLUE);
	CHECK(device->Get_Mirrors()->Stale_Flushes() == stale_before + 1);

	// A dynamic buffer, written between draws with DISCARD, staged at each draw instead: no flush.
	IDirect3DVertexBuffer9 *dynamic = NULL;
	CHECK(device->CreateVertexBuffer(12 * sizeof(ScreenVertex), D3DUSAGE_WRITEONLY | D3DUSAGE_DYNAMIC, SCREEN_FVF,
		D3DPOOL_DEFAULT, &dynamic, NULL) == D3D_OK);
	begin(device, BLUE);
	device->SetStreamSource(0, dynamic, 0, sizeof(ScreenVertex));
	dynamic->Lock(0, 0, &data, D3DLOCK_DISCARD);
	quad((ScreenVertex *)data + 6, 16, 0, 32, 16, 0.5f, YELLOW);
	dynamic->Unlock();
	device->DrawPrimitive(D3DPT_TRIANGLELIST, 6, 2);
	dynamic->Lock(0, 0, &data, D3DLOCK_DISCARD);
	quad((ScreenVertex *)data + 6, 0, 16, 16, 32, 0.5f, GREEN);
	dynamic->Unlock();
	device->DrawPrimitive(D3DPT_TRIANGLELIST, 6, 2);
	read_back(device);
	CHECK_PIXEL(24, 8, YELLOW);
	CHECK_PIXEL(8, 24, GREEN);
	CHECK_PIXEL(8, 8, BLUE);
	CHECK(device->Get_Mirrors()->Stale_Flushes() == stale_before + 1);

	// An indexed fan over a static buffer with a base vertex: the fan's vertices from the UP draw above,
	// placed after two padding vertices.
	IDirect3DVertexBuffer9 *fan_buffer = NULL;
	IDirect3DIndexBuffer9 *indices = NULL;
	CHECK(device->CreateVertexBuffer(8 * sizeof(ScreenVertex), 0, SCREEN_FVF, D3DPOOL_MANAGED, &fan_buffer, NULL) == D3D_OK);
	CHECK(device->CreateIndexBuffer(8 * 2, 0, D3DFMT_INDEX16, D3DPOOL_MANAGED, &indices, NULL) == D3D_OK);
	fan_buffer->Lock(0, 0, &data, 0);
	memset(data, 0, 2 * sizeof(ScreenVertex));
	memcpy((ScreenVertex *)data + 2, fan, sizeof(fan));
	for (int i = 0; i < 6; ++i) ((ScreenVertex *)data)[2 + i].colour = GREEN;
	fan_buffer->Unlock();
	indices->Lock(0, 0, &data, 0);
	const uint16_t fan_indices[8] = { 99, 99, 0, 1, 2, 3, 4, 5 };
	memcpy(data, fan_indices, sizeof(fan_indices));
	indices->Unlock();
	begin(device, BLUE);
	device->SetStreamSource(0, fan_buffer, 0, sizeof(ScreenVertex));
	device->SetIndices(indices);
	device->DrawIndexedPrimitive(D3DPT_TRIANGLEFAN, 2, 0, 6, 2, 4);
	read_back(device);
	CHECK_PIXEL(10, 16, GREEN);
	CHECK_PIXEL(16, 26, GREEN);
	CHECK_PIXEL(1, 1, BLUE);

	device->SetStreamSource(0, NULL, 0, 0);
	device->SetIndices(NULL);
	buffer->Release();
	dynamic->Release();
	fan_buffer->Release();
	indices->Release();
}

// Indexed draws from a dynamic buffer read only vertices MinVertex onwards, and only those are staged: the
// stream is bound MinVertex vertices before them.  The frame's first draw has nothing staged before it to
// bind to and stages from its base instead.  The buffer's first quad is never read: a draw that reached it
// would paint the whole target red.  The same draws from a static buffer take the other path (no staging),
// and the two pictures must be the same bytes.
static void check_min_vertex(PosixDevice9 *device)
{
	ScreenVertex vertices[24];
	quad(vertices, 0, 0, 32, 32, 0.5f, RED);
	quad(vertices + 6, 16, 0, 32, 16, 0.5f, YELLOW);
	quad(vertices + 12, 0, 16, 16, 32, 0.5f, GREEN);
	quad(vertices + 18, 16, 16, 32, 32, 0.5f, WHITE);
	const uint16_t past_first[12] = { 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17 };
	IDirect3DIndexBuffer9 *indices = NULL;
	void *data = NULL;
	CHECK(device->CreateIndexBuffer(sizeof(past_first), 0, D3DFMT_INDEX16, D3DPOOL_MANAGED, &indices, NULL) == D3D_OK);
	indices->Lock(0, 0, &data, 0);
	memcpy(data, past_first, sizeof(past_first));
	indices->Unlock();
	std::vector<uint8_t> pictures[2];
	for (int dynamic = 1; dynamic >= 0; --dynamic) {
		IDirect3DVertexBuffer9 *buffer = NULL;
		CHECK(device->CreateVertexBuffer(sizeof(vertices), dynamic ? D3DUSAGE_WRITEONLY | D3DUSAGE_DYNAMIC : D3DUSAGE_WRITEONLY,
			SCREEN_FVF, dynamic ? D3DPOOL_DEFAULT : D3DPOOL_MANAGED, &buffer, NULL) == D3D_OK);
		buffer->Lock(0, 0, &data, dynamic ? D3DLOCK_DISCARD : 0);
		memcpy(data, vertices, sizeof(vertices));
		buffer->Unlock();
		begin(device, BLUE);
		device->SetStreamSource(0, buffer, 0, sizeof(ScreenVertex));
		device->SetIndices(indices);
		device->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 6, 6, 0, 2);		// the frame's first: from its base
		device->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 12, 6, 6, 2);
		device->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 6, 12, 6, 6, 2);		// a base vertex too: vertices 18 to 23
		read_back(device);
		CHECK_PIXEL(24, 8, YELLOW);
		CHECK_PIXEL(8, 24, GREEN);
		CHECK_PIXEL(24, 24, WHITE);
		CHECK_PIXEL(8, 8, BLUE);
		pictures[dynamic] = pixels;
		device->SetStreamSource(0, NULL, 0, 0);
		device->SetIndices(NULL);
		buffer->Release();
	}
	CHECK(pictures[0] == pictures[1]);
	indices->Release();
}

static void check_clears_and_depth(PosixDevice9 *device)
{
	// A clear between draws hides the first.
	begin(device, BLUE);
	ScreenVertex v[6];
	quad(v, 0, 0, 16, 32, 0.5f, RED);
	device->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 2, v, sizeof(ScreenVertex));
	device->Clear(0, NULL, D3DCLEAR_TARGET, GREEN, 1.0f, 0);
	quad(v, 16, 0, 32, 32, 0.5f, RED);
	device->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 2, v, sizeof(ScreenVertex));
	read_back(device);
	CHECK_PIXEL(8, 16, GREEN);
	CHECK_PIXEL(24, 16, RED);

	// The depth test: the nearer quad wins whichever is drawn first.
	for (int order = 0; order < 2; ++order) {
		begin(device, BLUE);
		device->SetRenderState(D3DRS_ZENABLE, D3DZB_TRUE);
		ScreenVertex near_quad[6], far_quad[6];
		quad(near_quad, 0, 0, 20, 20, 0.25f, RED);
		quad(far_quad, 10, 10, 32, 32, 0.75f, GREEN);
		device->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 2, order == 0 ? near_quad : far_quad, sizeof(ScreenVertex));
		device->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 2, order == 0 ? far_quad : near_quad, sizeof(ScreenVertex));
		read_back(device);
		CHECK_PIXEL(15, 15, RED);
		CHECK_PIXEL(25, 25, GREEN);
		CHECK_PIXEL(5, 5, RED);
	}
	device->SetRenderState(D3DRS_ZENABLE, D3DZB_FALSE);

	// A clear with a smaller viewport clears only the viewport.
	begin(device, BLUE);
	D3DVIEWPORT9 whole, part = { 8, 8, 16, 16, 0.0f, 1.0f };
	device->GetViewport(&whole);
	device->SetViewport(&part);
	device->Clear(0, NULL, D3DCLEAR_TARGET, GREEN, 1.0f, 0);
	device->SetViewport(&whole);
	read_back(device);
	CHECK_PIXEL(7, 7, BLUE);
	CHECK_PIXEL(8, 8, GREEN);
	CHECK_PIXEL(23, 23, GREEN);
	CHECK_PIXEL(24, 24, BLUE);

	// Rectangles, each cut to the viewport.
	begin(device, BLUE);
	const D3DRECT rects[2] = { { 0, 0, 8, 8 }, { 24, 24, 40, 40 } };
	device->Clear(2, rects, D3DCLEAR_TARGET, RED, 1.0f, 0);
	read_back(device);
	CHECK_PIXEL(0, 0, RED);
	CHECK_PIXEL(7, 7, RED);
	CHECK_PIXEL(8, 8, BLUE);
	CHECK_PIXEL(31, 31, RED);
	CHECK_PIXEL(16, 16, BLUE);

	// Depth only, over the left half: depth 0 everywhere, then 1 on the left, then a quad at 0.5 over
	// everything shows on the left alone, and in its own colour.
	begin(device, BLUE);
	device->Clear(0, NULL, D3DCLEAR_ZBUFFER, 0, 0.0f, 0);
	const D3DRECT left_half = { 0, 0, 16, 32 };
	device->Clear(1, &left_half, D3DCLEAR_ZBUFFER, 0, 1.0f, 0);
	device->SetRenderState(D3DRS_ZENABLE, D3DZB_TRUE);
	quad(v, 0, 0, 32, 32, 0.5f, YELLOW);
	device->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 2, v, sizeof(ScreenVertex));
	device->SetRenderState(D3DRS_ZENABLE, D3DZB_FALSE);
	read_back(device);
	CHECK_PIXEL(8, 16, YELLOW);
	CHECK_PIXEL(24, 16, BLUE);
}

// A3d: a render-target texture drawn into on the GPU and then sampled, a StretchRect out of it, and a
// draw that would sample the target it draws into refused.
static void check_render_targets(PosixDevice9 *device)
{
	IDirect3DTexture9 *target_texture = NULL;
	CHECK(device->CreateTexture(16, 16, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &target_texture, NULL) == D3D_OK);
	IDirect3DSurface9 *target = NULL, *back = NULL;
	CHECK(target_texture->GetSurfaceLevel(0, &target) == D3D_OK);
	CHECK(device->GetRenderTarget(0, &back) == D3D_OK);
	CHECK(device->Gpu_Owns(target) && device->Gpu_Owns(back));

	// Into the target: red all over, then green on its left half.  The implicit depth surface is bigger
	// than the target, so the pass takes a matching scratch one.  The back buffer's own clear, with no
	// draw after it there, must still happen, on the back buffer and not on the target.
	begin(device, BLUE);
	device->Clear(0, NULL, D3DCLEAR_TARGET, YELLOW, 1.0f, 0);
	CHECK(device->SetRenderTarget(0, target) == D3D_OK);
	device->Clear(0, NULL, D3DCLEAR_TARGET, RED, 1.0f, 0);
	ScreenVertex v[6];
	quad(v, 0, 0, 8, 16, 0.5f, GREEN);
	device->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 2, v, sizeof(ScreenVertex));

	// Back to the back buffer: its yellow clear happened, and only there.
	CHECK(device->SetRenderTarget(0, back) == D3D_OK);
	read_back(device);
	CHECK_PIXEL(16, 16, YELLOW);

	// And the target drawn over all of it, point sampled.
	device->Clear(0, NULL, D3DCLEAR_TARGET, BLUE, 1.0f, 0);
	device->SetTexture(0, target_texture);
	device->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
	device->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
	quad(v, -0.5f, -0.5f, 31.5f, 31.5f, 0.5f, WHITE);
	device->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 2, v, sizeof(ScreenVertex));
	read_back(device);
	CHECK_PIXEL(4, 16, GREEN);
	CHECK_PIXEL(27, 16, RED);

	// A draw into the target that samples it is refused, and leaves the target as it was.
	const unsigned int recorded = device->Draws_Recorded();
	CHECK(device->SetRenderTarget(0, target) == D3D_OK);
	device->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 2, v, sizeof(ScreenVertex));
	CHECK(device->Draws_Recorded() == recorded);
	CHECK(device->Draw_Refusals().count("sampling the render target it draws into") == 1);

	// The target read back through GetRenderTargetData into a system-memory surface: the GPU's pixels,
	// green on the left and red on the right, not the CPU image's zeroes.
	IDirect3DSurface9 *copy = NULL;
	CHECK(device->CreateOffscreenPlainSurface(16, 16, D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM, &copy, NULL) == D3D_OK);
	CHECK(device->GetRenderTargetData(target, copy) == D3D_OK);
	D3DLOCKED_RECT locked;
	CHECK(copy->LockRect(&locked, NULL, D3DLOCK_READONLY) == D3D_OK);
	const uint32_t *row = (const uint32_t *)((const uint8_t *)locked.pBits + 8 * locked.Pitch);
	CHECK(row[2] == GREEN && row[13] == RED);
	copy->UnlockRect();
	copy->Release();

	// The target's right half into the back buffer's top-left quarter, a GPU blit through StretchRect.
	CHECK(device->SetRenderTarget(0, back) == D3D_OK);
	device->SetTexture(0, NULL);
	device->Clear(0, NULL, D3DCLEAR_TARGET, BLUE, 1.0f, 0);
	const RenderRect from = { 8, 0, 16, 16 };
	const RenderRect to = { 0, 0, 16, 16 };
	CHECK(device->StretchRect(target, &from, back, &to, D3DTEXF_POINT) == D3D_OK);
	read_back(device);
	CHECK_PIXEL(8, 8, RED);
	CHECK_PIXEL(24, 24, BLUE);

	// The front buffer is what Present showed, even after the back buffer has been cleared again.
	CHECK(device->Present(NULL, NULL, NULL, NULL) == D3D_OK);
	device->Clear(0, NULL, D3DCLEAR_TARGET, GREEN, 1.0f, 0);
	IDirect3DSurface9 *front = NULL;
	CHECK(device->CreateOffscreenPlainSurface(SIZE, SIZE, D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM, &front, NULL) == D3D_OK);
	CHECK(device->GetFrontBufferData(0, front) == D3D_OK);
	CHECK(front->LockRect(&locked, NULL, D3DLOCK_READONLY) == D3D_OK);
	CHECK(((const uint32_t *)((const uint8_t *)locked.pBits + 8 * locked.Pitch))[8] == RED);
	CHECK(((const uint32_t *)((const uint8_t *)locked.pBits + 24 * locked.Pitch))[24] == BLUE);
	front->UnlockRect();
	front->Release();

	// The target cleared on its own: nothing else moves, and the back buffer keeps its green.
	CHECK(device->SetRenderTarget(0, target) == D3D_OK);
	device->Clear(0, NULL, D3DCLEAR_TARGET, YELLOW, 1.0f, 0);
	CHECK(device->SetRenderTarget(0, back) == D3D_OK);
	read_back(device);
	CHECK_PIXEL(24, 24, GREEN);

	target->Release();
	back->Release();
	target_texture->Release();
}

// Keep an encoded copy alive when requested: SDL must then preserve that backing texture if a
// later upload cycles it. This makes accidental cycling of a partial mip update observable.
struct MipReadback
{
	SDL_GPUDevice *device;
	SDL_GPUTransferBuffer *transfer;
	SDL_GPUCommandBuffer *commands;
	unsigned bytes;

	MipReadback(SDL_GPUDevice *gpu, SDL_GPUTexture *texture, unsigned level, unsigned edge)
		: device(gpu), transfer(NULL), commands(NULL), bytes(edge * edge * 4)
	{
		SDL_GPUTransferBufferCreateInfo info = {};
		info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
		info.size = bytes;
		transfer = SDL_CreateGPUTransferBuffer(device, &info);
		if (!transfer) return;
		commands = SDL_AcquireGPUCommandBuffer(device);
		if (!commands) return;
		SDL_GPUCopyPass *pass = SDL_BeginGPUCopyPass(commands);
		if (!pass) { SDL_CancelGPUCommandBuffer(commands); commands = NULL; return; }
		SDL_GPUTextureRegion source = {};
		source.texture = texture; source.mip_level = level;
		source.w = edge; source.h = edge; source.d = 1;
		SDL_GPUTextureTransferInfo destination = {};
		destination.transfer_buffer = transfer;
		destination.pixels_per_row = edge; destination.rows_per_layer = edge;
		SDL_DownloadFromGPUTexture(pass, &source, &destination);
		SDL_EndGPUCopyPass(pass);
	}

	~MipReadback()
	{
		if (commands) SDL_CancelGPUCommandBuffer(commands);
		if (transfer) SDL_ReleaseGPUTransferBuffer(device, transfer);
	}

	bool Ready() const { return commands != NULL; }

	bool Has_Colour(uint32_t expected)
	{
		if (!Ready()) return false;
		SDL_GPUFence *fence = SDL_SubmitGPUCommandBufferAndAcquireFence(commands);
		commands = NULL;
		const bool finished = fence && SDL_WaitForGPUFences(device, true, &fence, 1);
		if (fence) SDL_ReleaseGPUFence(device, fence);
		if (!finished) return false;
		const uint32_t *pixels = static_cast<const uint32_t *>(SDL_MapGPUTransferBuffer(device, transfer, false));
		if (!pixels) return false;
		bool matches = true;
		for (unsigned pixel = 0; pixel < bytes / 4; ++pixel) matches = matches && pixels[pixel] == expected;
		SDL_UnmapGPUTransferBuffer(device, transfer);
		return matches;
	}
};

static void fill_mip(IDirect3DTexture9 *texture, unsigned level, unsigned edge, uint32_t colour)
{
	D3DLOCKED_RECT locked;
	const RenderResult result = texture->LockRect(level, &locked, NULL, 0);
	CHECK(result == D3D_OK);
	if (result != D3D_OK) return;
	for (unsigned y = 0; y < edge; ++y) {
		uint32_t *row = reinterpret_cast<uint32_t *>(static_cast<uint8_t *>(locked.pBits) + y * locked.Pitch);
		for (unsigned x = 0; x < edge; ++x) row[x] = colour;
	}
	CHECK(texture->UnlockRect(level) == D3D_OK);
}

static void check_mip_colours(SdlGpuFrame *frame, SDL_GPUTexture *texture, const uint32_t *colours, unsigned levels)
{
	for (unsigned level = 0; level < levels; ++level) {
		MipReadback readback(frame->Device(), texture, level, 16u >> level);
		if (!readback.Has_Colour(colours[level])) {
			++failures;
			printf("FAIL dirty mip upload: level %u did not retain colour %08x\n", level, colours[level]);
		}
	}
}

static void check_dirty_mips(PosixDevice9 *device)
{
	SdlGpuFrame *frame = device->Get_Gpu();
	SdlResourceMirrors *mirrors = device->Get_Mirrors();
	CHECK(frame->Flush());
	std::string error;
	IDirect3DTexture9 *texture = NULL;
	CHECK(device->CreateTexture(16, 16, 0, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &texture, NULL) == D3D_OK);
	if (!texture) return;
	const unsigned levels = 5;
	uint32_t colours[levels] = { RED, GREEN, BLUE, YELLOW, WHITE };
	for (unsigned level = 0; level < levels; ++level) fill_mip(texture, level, 16u >> level, colours[level]);
	SDL_GPUTexture *gpuTexture = mirrors->Texture(texture, error);
	CHECK(gpuTexture != NULL);
	CHECK(frame->Pending_Upload_Bytes() == 1364); // 16x16 through 1x1, four bytes per pixel.
	CHECK(frame->Flush());
	if (gpuTexture) {
		check_mip_colours(frame, gpuTexture, colours, levels);

		colours[2] = 0xff00ffff;
		fill_mip(texture, 2, 4, colours[2]);
		CHECK(mirrors->Texture(texture, error) == gpuTexture);
		CHECK(frame->Pending_Upload_Bytes() == 64);
		CHECK(frame->Flush());
		check_mip_colours(frame, gpuTexture, colours, levels);

		{
			MipReadback held(frame->Device(), gpuTexture, 1, 8);
			CHECK(held.Ready());
			colours[0] = 0xffff00ff;
			fill_mip(texture, 0, 16, colours[0]);
			CHECK(mirrors->Texture(texture, error) == gpuTexture);
			CHECK(frame->Pending_Upload_Bytes() == 1024);
			CHECK(frame->Flush());
			CHECK(held.Has_Colour(colours[1]));
			check_mip_colours(frame, gpuTexture, colours, levels);
		}

		for (unsigned level = 0; level < levels; ++level) {
			colours[level] ^= 0x00010101;
			fill_mip(texture, level, 16u >> level, colours[level]);
		}
		CHECK(mirrors->Texture(texture, error) == gpuTexture);
		CHECK(frame->Pending_Upload_Bytes() == 1364);
		CHECK(frame->Flush());
		check_mip_colours(frame, gpuTexture, colours, levels);
	}
	texture->Release();

	// Even levels never locked (version zero) need their initial zero-filled CPU image uploaded.
	CHECK(device->CreateTexture(16, 16, 0, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &texture, NULL) == D3D_OK);
	if (!texture) return;
	gpuTexture = mirrors->Texture(texture, error);
	CHECK(gpuTexture != NULL);
	CHECK(frame->Pending_Upload_Bytes() == 1364);
	CHECK(frame->Flush());
	const uint32_t zeroes[levels] = {};
	if (gpuTexture) check_mip_colours(frame, gpuTexture, zeroes, levels);
	texture->Release();

	// The POSIX backend also accepts CPU writes to render-target texture mips. Preserve that existing
	// interop: updating a sibling must not replace the GPU-rendered base with its stale CPU image.
	CHECK(device->CreateTexture(16, 16, 2, D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8,
		D3DPOOL_DEFAULT, &texture, NULL) == D3D_OK);
	if (!texture) return;
	IDirect3DSurface9 *target = NULL, *back = NULL;
	CHECK(texture->GetSurfaceLevel(0, &target) == D3D_OK);
	CHECK(device->GetRenderTarget(0, &back) == D3D_OK);
	CHECK(device->SetRenderTarget(0, target) == D3D_OK);
	CHECK(device->Clear(0, NULL, D3DCLEAR_TARGET, RED, 1.0f, 0) == D3D_OK);
	CHECK(frame->Flush());
	CHECK(device->SetRenderTarget(0, back) == D3D_OK);
	fill_mip(texture, 1, 8, GREEN);
	gpuTexture = mirrors->Texture(texture, error);
	CHECK(gpuTexture != NULL);
	CHECK(frame->Pending_Upload_Bytes() == 256);
	CHECK(frame->Flush());
	const uint32_t rendered[2] = { RED, GREEN };
	if (gpuTexture) check_mip_colours(frame, gpuTexture, rendered, 2);
	target->Release();
	back->Release();
	texture->Release();
}

#if defined(__linux__)
// Runtime sun capture and diagnostic mode overrides are enabled only on Linux.
static void check_sun_capture(PosixDevice9 *device)
{
    begin(device, BLUE);
    device->SetRenderState(D3DRS_ZENABLE, D3DZB_TRUE);
    device->SetRenderState(D3DRS_ZWRITEENABLE, 1);
    device->SetRenderState(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
    device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    device->SetRenderState(D3DRS_ALPHABLENDENABLE, 0);
    device->SetRenderState(D3DRS_ALPHATESTENABLE, 0);
    D3DVIEWPORT9 sceneView = {3, 5, 24, 20, 0, 1};
    device->SetViewport(&sceneView);
    ScreenVertex v[6];
    quad(v, 0, 0, 32, 32, .4f, RED);
    CHECK(device->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 2, v, sizeof(v[0])) == D3D_OK);
    read_back(device);
    const std::vector<uint8_t> scene = pixels;

    IDirect3DTexture9 *savedTexture = NULL;
    CHECK(device->CreateTexture(1,1,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&savedTexture,NULL)==D3D_OK);
    device->SetTexture(7,savedTexture); // unused by this program, owned across the pass
    D3DMATRIX sceneWorld;
    device->GetTransform(D3DTS_WORLD,&sceneWorld);
    CHECK(!device->Begin_Sun_Shadow(0));
    CHECK(device->Begin_Sun_Shadow(32));
    CHECK(!device->Begin_Sun_Shadow(64)); // re-entry leaves the first capture intact
    device->SetTexture(7,NULL);
    savedTexture->Release(); // the capture now holds the only reference
    D3DMATRIX casterWorld=sceneWorld; casterWorld._41=13.0f;
    device->SetTransform(D3DTS_WORLD,&casterWorld);
    device->SetRenderState(D3DRS_ZWRITEENABLE, 0); // a sorted rotor still writes sun depth
    device->SetRenderState(D3DRS_ALPHABLENDENABLE, 1);
    device->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
    device->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    quad(v, 0, 0, 16, 32, .3f, 0x0700FF00); // below the existing 8/255 caster threshold
    device->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 2, v, sizeof(v[0]));
    quad(v, 16, 0, 32, 32, .3f, 0x8000FF00);
    device->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 2, v, sizeof(v[0]));
    device->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_ONE); // additive light must not occlude
    quad(v, 0, 0, 32, 32, .1f, WHITE);
    device->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 2, v, sizeof(v[0]));
    CHECK(device->End_Sun_Shadow());
    D3DVIEWPORT9 restored;
    device->GetViewport(&restored);
    CHECK(memcmp(&restored, &sceneView, sizeof(restored)) == 0);
    RenderUInt32 blend=1,write=0;
    device->GetRenderState(D3DRS_ALPHABLENDENABLE,&blend);
    device->GetRenderState(D3DRS_ZWRITEENABLE,&write);
    CHECK(blend==0 && write==1);
    D3DMATRIX restoredWorld;device->GetTransform(D3DTS_WORLD,&restoredWorld);
    CHECK(memcmp(&sceneWorld,&restoredWorld,sizeof(sceneWorld))==0);
    IDirect3DBaseTexture9 *restoredTexture=NULL;
    device->GetTexture(7,&restoredTexture);
    CHECK(restoredTexture==savedTexture && restoredTexture->GetLevelCount()==1);
    if(restoredTexture) restoredTexture->Release();
    device->SetTexture(7,NULL);
    read_back(device);
    CHECK(pixels == scene);
    std::vector<float> depth;
    CHECK(device->Read_Sun_Shadow(depth));
    if (depth.size()==32*32) {
        CHECK(depth[16*32+8]==1.0f);
        CHECK(depth[16*32+24]>.299f && depth[16*32+24]<.301f);
    } else CHECK(false);
    // Scene depth and the restored state must survive too, not just its color.
    quad(v, 0, 0, 32, 32, .6f, GREEN);
    device->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 2, v, sizeof(v[0]));
    read_back(device);
    CHECK(pixels == scene);

    CHECK(device->Begin_Sun_Shadow(64));
    CHECK(device->End_Sun_Shadow()); // no casters must erase stale depth
    CHECK(device->Read_Sun_Shadow(depth));
    for(float d:depth) CHECK(d==1.0f);
    CHECK(device->Begin_Sun_Shadow(32));
    CHECK(device->DrawPrimitiveUP(D3DPT_TRIANGLELIST,1,NULL,sizeof(v[0]))==D3DERR_INVALIDCALL);
    CHECK(!device->End_Sun_Shadow());
    CHECK(device->Begin_Sun_Shadow(32));
    CHECK(device->DrawPrimitiveUP((D3DPRIMITIVETYPE)999,1,v,sizeof(v[0]))==D3DERR_INVALIDCALL);
    CHECK(!device->End_Sun_Shadow()); // rejected capture must not authorize stencil suppression
    CHECK(!device->Read_Sun_Shadow(depth));
    device->Clear_Sun_Shadow();
    sceneView={0,0,SIZE,SIZE,0,1};device->SetViewport(&sceneView);
    device->SetRenderState(D3DRS_CULLMODE,D3DCULL_CCW);
    printf("sun capture: alpha/rotor and additive rules, scene color/depth/state, empty/resize/refusal checked\n");
}

// An independent world-space fixture: a left-half blocker at z=.25, white ground
// at z=.75, and a translated/scaled scene camera with a nonzero viewport origin.
static void check_sun_receivers(PosixDevice9 *device)
{
    {
    SunShadow::SceneScope mainScene(device,true);
    D3DMATRIX identity = {}; identity._11=identity._22=identity._33=identity._44=1;
    device->SetTransform(D3DTS_WORLD,&identity);
    device->SetTransform(D3DTS_VIEW,&identity);
    device->SetTransform(D3DTS_PROJECTION,&identity);
    begin(device,BLUE);
    device->SetRenderState(D3DRS_ZENABLE,1);
    device->SetRenderState(D3DRS_ZWRITEENABLE,1);
    device->SetRenderState(D3DRS_ZFUNC,D3DCMP_LESSEQUAL);
    device->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE);
    device->SetRenderState(D3DRS_ALPHABLENDENABLE,0);
    device->SetRenderState(D3DRS_ALPHATESTENABLE,0);
    device->SetFVF(D3DFVF_XYZ|D3DFVF_DIFFUSE);
    struct V { float x,y,z; uint32_t color; };
    const V caster[] = {{-1,1,.25f,WHITE},{0,1,.25f,WHITE},{-1,-1,.25f,WHITE},
        {0,1,.25f,WHITE},{0,-1,.25f,WHITE},{-1,-1,.25f,WHITE}};
    const V receiver[] = {{-1,1,.75f,WHITE},{1,1,.75f,WHITE},{-1,-1,.75f,WHITE},
        {1,1,.75f,WHITE},{1,-1,.75f,WHITE},{-1,-1,.75f,WHITE}};
    CHECK(device->Begin_Sun_Shadow(32));
    CHECK(device->DrawPrimitiveUP(D3DPT_TRIANGLELIST,2,caster,sizeof(V))==D3D_OK);
    CHECK(device->End_Sun_Shadow());
    D3DMATRIX view=identity,projection=identity;
    view._41=.25f; projection._11=.8f; projection._22=.8f;
    device->SetTransform(D3DTS_VIEW,&view);
    device->SetTransform(D3DTS_PROJECTION,&projection);
    D3DVIEWPORT9 viewport={3,5,24,20,.2f,.8f}; device->SetViewport(&viewport);
    const auto parameters = [&] { return SunShadow::Parameters(device,0,.5f,0,0,0,1,0); };
    CHECK(parameters()); CHECK(SunShadow::ReceivesView(device));
    CHECK(SunShadow::OwnsSceneShadows(device));
    const auto draw = [&] {
        D3DVIEWPORT9 current; device->GetViewport(&current);
        D3DVIEWPORT9 whole={0,0,SIZE,SIZE,0,1}; device->SetViewport(&whole);
        device->Clear(0,NULL,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER,BLUE,1,0);
        device->SetViewport(&current);
        CHECK(device->DrawPrimitiveUP(D3DPT_TRIANGLELIST,2,receiver,sizeof(V))==D3D_OK);
        read_back(device);
    };
    draw();
    CHECK_PIXEL(10,15,0xFF808080); CHECK_PIXEL(17,15,0xFF808080);
    CHECK_PIXEL(18,15,WHITE); CHECK_PIXEL(24,15,WHITE);
    CHECK_PIXEL(1,15,BLUE); CHECK_PIXEL(15,3,BLUE);
    const auto shadowed = pixels;
    CHECK(SunShadow::Parameters(device,0,0,0,0,0,1,0)); draw(); CHECK_PIXEL(10,15,WHITE);
    CHECK(SunShadow::Parameters(device,2,.5f,0,0,0,1,0)); draw(); CHECK_PIXEL(10,15,WHITE);
    CHECK(SunShadow::Parameters(device,0,.5f,4,0,0,1,0)); draw(); CHECK_PIXEL(17,15,0xFF808080);
    CHECK(SunShadow::Parameters(device,0,.5f,4,0,8,1,0)); draw();
    // A .5 depth gap opens the existing radius to four texels: the hard edge now
    // contains both lit and blocked taps. Full sky fill raises the interior to white.
    CHECK((at(17,15)&255)>128 && (at(17,15)&255)<255);
    CHECK(SunShadow::Parameters(device,0,.5f,4,0,8,1,1)); draw(); CHECK_PIXEL(10,15,WHITE);
    CHECK(!SunShadow::Parameters(device,0,.5f,1,2,0,1,0));
    CHECK(!SunShadow::ReceivesView(device));
    CHECK(parameters()); draw(); CHECK(pixels==shadowed);
    // Exercise every transcribed ground pixel program, including the road's blended
    // no-depth-write layer. All-white inputs yield the same analytic .5/1 sunlight.
    IDirect3DTexture9 *white=make_texture(device,1,1,D3DFMT_A8R8G8B8,&WHITE,sizeof(WHITE));
    for(unsigned i=0;i<5;++i) device->SetTexture(i,white);
    const RenderUInt32 tokens[]={0xffff0101u,0x0000ffffu};
    IDirect3DPixelShader9 *ground=NULL; CHECK(device->CreatePixelShader(tokens,&ground)==D3D_OK);
    const char *names[]={"terrain.pso","terrainnoise.pso","terrainnoise2.pso","fterrain.pso",
        "fterrain0.pso","fterrainnoise.pso","fterrainnoise2.pso","roadnoise2.pso"};
    device->SetPixelShader(ground);
    for(const char *name:names) {
        PosixDevice_Name_Shader(ground,name);
        draw(); CHECK_PIXEL(10,15,0xFF808080); CHECK_PIXEL(24,15,WHITE);
    }
    device->SetRenderState(D3DRS_ZWRITEENABLE,0);
    device->SetRenderState(D3DRS_ALPHABLENDENABLE,1);
    device->SetRenderState(D3DRS_SRCBLEND,D3DBLEND_SRCALPHA);
    device->SetRenderState(D3DRS_DESTBLEND,D3DBLEND_INVSRCALPHA);
    draw(); CHECK_PIXEL(10,15,0xFF808080); CHECK_PIXEL(24,15,WHITE);
    device->SetRenderState(D3DRS_ZWRITEENABLE,1);device->SetRenderState(D3DRS_ALPHABLENDENABLE,0);
    device->SetPixelShader(NULL);ground->Release();
    for(unsigned i=0;i<5;++i) device->SetTexture(i,NULL);
    white->Release();
    // Scoped exclusions include nesting. Deferred draws keep their own constant bytes.
    {
        SunShadow::ReceiverExclusion exclusion(device);
        { SunShadow::ReceiverExclusion nested(device); draw(); CHECK_PIXEL(10,15,WHITE); }
        draw(); CHECK_PIXEL(10,15,WHITE);
    }
    draw(); CHECK(pixels==shadowed);
    // A nested scene is independent even if its matrices and viewport match. Its
    // attempted clear/capture must not destroy the outer scene's completed map.
    for (bool eligible : {false,true}) {
        {
            SunShadow::SceneScope nestedScene(device,eligible);
            CHECK(SunShadow::ReceiversExcluded(device));
            CHECK(!SunShadow::ReceivesView(device));
            CHECK(!SunShadow::OwnsSceneShadows(device));
            SunShadow::Clear(device);
            CHECK(!SunShadow::Begin(device,64));
            CHECK(!parameters());
            draw(); CHECK_PIXEL(10,15,WHITE);
        }
        CHECK(!SunShadow::ReceiversExcluded(device));
        CHECK(SunShadow::OwnsSceneShadows(device));
        draw(); CHECK(pixels==shadowed);
    }
    // An independent target cannot borrow shadow ownership even with the exact
    // same camera, depth attachment and viewport. Read both targets independently.
    IDirect3DTexture9 *otherTexture=NULL;
    IDirect3DSurface9 *otherTarget=NULL,*back=NULL,*copy=NULL;
    CHECK(device->CreateTexture(SIZE,SIZE,1,D3DUSAGE_RENDERTARGET,D3DFMT_A8R8G8B8,
        D3DPOOL_DEFAULT,&otherTexture,NULL)==D3D_OK);
    CHECK(otherTexture->GetSurfaceLevel(0,&otherTarget)==D3D_OK);
    CHECK(device->GetRenderTarget(0,&back)==D3D_OK);
    CHECK(device->SetRenderTarget(0,otherTarget)==D3D_OK);
    device->SetViewport(&viewport);
    CHECK(!SunShadow::ReceivesView(device)); CHECK(!SunShadow::OwnsSceneShadows(device));
    draw(); CHECK(pixels==shadowed); // draw() reads the original back buffer
    CHECK(device->CreateOffscreenPlainSurface(SIZE,SIZE,D3DFMT_A8R8G8B8,D3DPOOL_SYSTEMMEM,&copy,NULL)==D3D_OK);
    CHECK(device->GetRenderTargetData(otherTarget,copy)==D3D_OK);
    D3DLOCKED_RECT locked;
    CHECK(copy->LockRect(&locked,NULL,D3DLOCK_READONLY)==D3D_OK);
    CHECK(((const uint32_t*)((const uint8_t*)locked.pBits+15*locked.Pitch))[10]==WHITE);
    copy->UnlockRect(); copy->Release();
    CHECK(device->SetRenderTarget(0,back)==D3D_OK); device->SetViewport(&viewport);
    otherTarget->Release(); otherTexture->Release(); back->Release();
    CHECK(SunShadow::OwnsSceneShadows(device)); draw(); CHECK(pixels==shadowed);
    // A secondary camera/viewport must keep legacy coverage and receive no stale map.
    D3DVIEWPORT9 other=viewport; other.X=4; device->SetViewport(&other);
    CHECK(!SunShadow::ReceivesView(device)); CHECK(!SunShadow::OwnsSceneShadows(device));
    draw(); CHECK_PIXEL(10,15,WHITE);
    device->SetViewport(&viewport);
    D3DMATRIX otherView=view; otherView._41+=.1f; device->SetTransform(D3DTS_VIEW,&otherView);
    CHECK(!SunShadow::ReceivesView(device));
    // Temporary camera-space drawing within the owning scene must not bring
    // stencil volumes back after other geometry already received mapped shadows.
    CHECK(SunShadow::OwnsSceneShadows(device)); draw(); CHECK_PIXEL(10,15,WHITE);
    device->SetTransform(D3DTS_VIEW,&view);
    draw(); CHECK(pixels==shadowed);
    // Ordinary solid geometry can mark stencil for the later occlusion pass.
    device->SetRenderState(D3DRS_STENCILENABLE,1);
    device->SetRenderState(D3DRS_STENCILFUNC,D3DCMP_ALWAYS);
    device->SetRenderState(D3DRS_STENCILPASS,D3DSTENCILOP_REPLACE);
    device->SetRenderState(D3DRS_STENCILREF,7);
    draw(); CHECK(pixels==shadowed);
    device->SetRenderState(D3DRS_STENCILENABLE,0);
    // The bridge's depth-writing alpha-tested material also uses ordinary alpha
    // blending. Ordinary transparent materials receive too, even without depth writes.
    // Tree exclusion still takes precedence.
    device->SetRenderState(D3DRS_ALPHABLENDENABLE,1);
    device->SetRenderState(D3DRS_SRCBLEND,D3DBLEND_SRCALPHA);
    device->SetRenderState(D3DRS_DESTBLEND,D3DBLEND_INVSRCALPHA);
    device->SetRenderState(D3DRS_ALPHATESTENABLE,1);
    device->SetRenderState(D3DRS_ALPHAFUNC,D3DCMP_GREATER);
    device->SetRenderState(D3DRS_ALPHAREF,8);
    draw(); CHECK(pixels==shadowed);
    { SunShadow::ReceiverExclusion tree(device); draw(); CHECK_PIXEL(10,15,WHITE); }
    device->SetRenderState(D3DRS_ZWRITEENABLE,0); draw(); CHECK(pixels==shadowed);
    device->SetRenderState(D3DRS_ZWRITEENABLE,1);
    device->SetRenderState(D3DRS_ALPHATESTENABLE,0); draw(); CHECK(pixels==shadowed);
    device->SetRenderState(D3DRS_ALPHABLENDENABLE,0);
    // Additive light must stay unshadowed; lack of depth writes alone is not an exclusion.
    device->SetRenderState(D3DRS_ZWRITEENABLE,0); draw(); CHECK(pixels==shadowed);
    device->SetRenderState(D3DRS_ZWRITEENABLE,1);
    device->SetRenderState(D3DRS_ALPHABLENDENABLE,1);
    device->SetRenderState(D3DRS_SRCBLEND,D3DBLEND_ONE);
    device->SetRenderState(D3DRS_DESTBLEND,D3DBLEND_ONE);
    draw(); CHECK_PIXEL(10,15,WHITE);
    device->SetRenderState(D3DRS_ALPHABLENDENABLE,0);
    CHECK(parameters()); draw(); CHECK(pixels==shadowed);
    // Leave a receiver queued against the old allocation, then replace it. End
    // submits both in order; the queued draw must still sample the old blocker.
    device->Clear(0,NULL,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER,BLUE,1,0);
    device->DrawPrimitiveUP(D3DPT_TRIANGLELIST,2,receiver,sizeof(V));
    CHECK(device->Begin_Sun_Shadow(64)); CHECK(device->End_Sun_Shadow());
    read_back(device); CHECK_PIXEL(10,15,0xFF808080);
    // An empty replacement clears the previous blocker; parameters must be renewed.
    CHECK(!SunShadow::ReceivesView(device)); CHECK(parameters()); draw(); CHECK_PIXEL(10,15,WHITE);
    // Invalid allocation requests fall back immediately; the last map cannot
    // silently remain the current frame's shadow source.
    CHECK(!SunShadow::Begin(device,0)); CHECK(!SunShadow::OwnsSceneShadows(device));
    CHECK(!parameters());
    CHECK(SunShadow::Begin(device,32)); CHECK(SunShadow::End(device)); CHECK(parameters());
    device->SetTransform(D3DTS_VIEW,&identity); device->SetTransform(D3DTS_PROJECTION,&identity);
    viewport={0,0,SIZE,SIZE,0,1};device->SetViewport(&viewport);
    device->SetRenderState(D3DRS_CULLMODE,D3DCULL_CCW);
    } // no capture or stencil ownership may escape its scene
    CHECK(!SunShadow::ReceivesView(device)); CHECK(!SunShadow::OwnsSceneShadows(device));
    std::vector<float> expiredDepth; CHECK(!device->Read_Sun_Shadow(expiredDepth));
    {
        SunShadow::SceneScope nextScene(device,true);
        CHECK(!SunShadow::OwnsSceneShadows(device));
    }
    printf("sun receivers: shared filter, offset/depth-range matrix, exclusions, nested scenes, targets and empty replacement checked\n");
}


// Analytic two-sphere moments, then the production sampler/constant/camera paths.
static void check_sun_smoke(PosixDevice9 *device)
{
    D3DMATRIX identity={}; identity._11=identity._22=identity._33=identity._44=1;
    const float casters[]={0,0,.25f,.5f,1, 0,0,.75f,.5f,2};
    auto *gpu=device->Get_Gpu();
    CHECK(gpu->Fill_Sun_Smoke(&identity._11,casters,2));
    CHECK(gpu->Sun_Smoke_Count()==2);
    std::vector<float> moments;
    CHECK(gpu->Read_Sun_Smoke(moments));
    if(moments.size()==512*512*4) {
        const unsigned middle=(256*512+256)*4;
        const float rim=1-2*(1.0f/256)*(1.0f/256), shape=rim*rim;
        CHECK(std::fabs(moments[middle]-3*shape)<1e-5f);
        CHECK(std::fabs(moments[middle+1]-1.75f*shape)<1e-5f);
        CHECK(std::fabs(moments[middle+2]-1.375f*shape)<1e-5f);
        CHECK(moments[middle+3]==.25f);
        CHECK(moments[0]==0 && moments[1]==0 && moments[2]==0 && moments[3]==1);
    }
    std::vector<float> projected;
    // A quadrant away from the origin catches an inverted Y axis or swapped instance fields.
    const float offcentre[]={.5f,.5f,.3f,.2f,2};
    CHECK(gpu->Fill_Sun_Smoke(&identity._11,offcentre,1));
    CHECK(gpu->Read_Sun_Smoke(moments));
    if(moments.size()==512*512*4) {
        CHECK(moments[(128*512+384)*4]>1.99f);
        CHECK(moments[(128*512+384)*4+3]==.3f);
        CHECK(moments[(384*512+384)*4]==0 && moments[(128*512+128)*4]==0);
    }
    const float invalid[]={4,0,.5f,.1f,1, 0,0,.5f,0,1, 0,0,.5f,.1f,-1,
        0,0,.5f,.1f,NAN};
    CHECK(SunSmoke::Project(&identity._11,invalid,4,projected)==0);
    std::vector<float> many;
    for(unsigned i=0;i<SunSmoke::MaxCasters+1;++i) many.insert(many.end(),casters,casters+5);
    CHECK(SunSmoke::Project(&identity._11,many.data(),SunSmoke::MaxCasters+1,projected)==SunSmoke::MaxCasters);
    {
    SunShadow::SceneScope scene(device,true);
    device->SetTransform(D3DTS_WORLD,&identity); device->SetTransform(D3DTS_VIEW,&identity);
    device->SetTransform(D3DTS_PROJECTION,&identity);
    D3DVIEWPORT9 viewport={0,0,SIZE,SIZE,0,1};device->SetViewport(&viewport);
    begin(device,0);
    device->SetRenderState(D3DRS_ZENABLE,1);device->SetRenderState(D3DRS_ZWRITEENABLE,1);
    device->SetRenderState(D3DRS_ZFUNC,D3DCMP_LESSEQUAL);device->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE);
    device->SetRenderState(D3DRS_ALPHABLENDENABLE,0);device->SetRenderState(D3DRS_ALPHATESTENABLE,0);
    CHECK(SunShadow::Begin(device,32)); CHECK(SunShadow::End(device));
    const float cloud[]={0,0,.5f,.5f,2};
    CHECK(SunShadow::FillSmoke(device,cloud,1,.6f));
    viewport={3,5,24,20,.2f,.8f};device->SetViewport(&viewport);
    CHECK(SunShadow::Parameters(device,0,0,0,0,0,1,0));
    struct V {float x,y,z;uint32_t color;};
    const auto draw=[&](float z,uint32_t colour=WHITE,float offset=0,bool read=true,uint32_t background=0) {
        const V v[]={{-2+offset,2,z,colour},{2+offset,2,z,colour},{-2+offset,-2,z,colour},
            {2+offset,2,z,colour},{2+offset,-2,z,colour},{-2+offset,-2,z,colour}};
        device->Clear(0,nullptr,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER,background,1,0);
        device->SetFVF(D3DFVF_XYZ|D3DFVF_DIFFUSE);
        CHECK(device->DrawPrimitiveUP(D3DPT_TRIANGLELIST,2,v,sizeof(V))==D3D_OK);
        if(read) read_back(device);
    };
    draw(.9f); const int behind=at(15,15)&255;
    CHECK(behind>=123 && behind<=128); // logistic integral, gain .6, optical depth 2
    CHECK_PIXEL(4,6,WHITE); // outside the projected disc, even with viewport/depth offsets
    draw(.1f); CHECK((at(15,15)&255)>=235 && (at(15,15)&255)<=240);
    // Refill must submit the queued old-map reader before overwriting its texture.
    draw(.9f,WHITE,0,false);
    const float moved[]={.8f,.8f,.5f,.1f,2};
    CHECK(SunShadow::FillSmoke(device,moved,1,.6f)); read_back(device);
    CHECK(int(at(15,15)&255)==behind);
    const V farther[]={{-2,2,.95f,GREEN},{2,2,.95f,GREEN},{-2,-2,.95f,GREEN},
        {2,2,.95f,GREEN},{2,-2,.95f,GREEN},{-2,-2,.95f,GREEN}};
    device->DrawPrimitiveUP(D3DPT_TRIANGLELIST,2,farther,sizeof(V));read_back(device);
    CHECK(int(at(15,15)&255)==behind); // refill preserves scene depth as well as colour
    draw(.9f); CHECK_PIXEL(15,15,WHITE);
    CHECK(SunShadow::FillSmoke(device,offcentre,1,.6f));
    draw(.9f); CHECK((at(21,10)&255)>120 && (at(21,10)&255)<140);
    CHECK_PIXEL(9,10,WHITE);CHECK_PIXEL(21,20,WHITE);
    CHECK(SunShadow::FillSmoke(device,cloud,1,.6f));
    device->SetRenderState(D3DRS_ZWRITEENABLE,0);
    device->SetRenderState(D3DRS_ALPHABLENDENABLE,1);
    device->SetRenderState(D3DRS_SRCBLEND,D3DBLEND_SRCALPHA);
    device->SetRenderState(D3DRS_DESTBLEND,D3DBLEND_INVSRCALPHA);
    draw(.9f,0x80ffffff); CHECK(std::abs(int(at(15,15)&255)-behind/2)<=2);
    device->SetRenderState(D3DRS_SRCBLEND,D3DBLEND_ONE);device->SetRenderState(D3DRS_DESTBLEND,D3DBLEND_ONE);
    draw(.9f);CHECK_PIXEL(15,15,WHITE);
    device->SetRenderState(D3DRS_SRCBLEND,D3DBLEND_ZERO);device->SetRenderState(D3DRS_DESTBLEND,D3DBLEND_SRCCOLOR);
    draw(.9f,0xff808080,0,true,WHITE);CHECK_PIXEL(15,15,0xff808080); // multiplier remains unshaded
    device->SetRenderState(D3DRS_ALPHABLENDENABLE,0);
    // Perspective main view, followed by the sorting renderer's camera-space vertices.
    D3DMATRIX view=identity,projection=identity; view._41=.25f;projection._34=.5f;
    device->SetTransform(D3DTS_VIEW,&view);device->SetTransform(D3DTS_PROJECTION,&projection);
    CHECK(SunShadow::Parameters(device,0,0,0,0,0,1,0));
    device->SetTransform(D3DTS_VIEW,&identity);CHECK(SunShadow::ReceivesView(device));
    draw(.25f,WHITE,.25f);CHECK_PIXEL(17,15,WHITE); // sunward face has no smoke ahead
    draw(.9f,WHITE,.25f);CHECK((at(17,15)&255)>70 && (at(17,15)&255)<95);
    const auto particle=pixels;
    // Firelight in normals reaches the pixel after shadowing, exactly once.
    struct G {float x,y,z,nx,ny,nz;uint32_t colour;};
    const G glow[]={{-1.75f,2,.9f,.2f,0,0,0xff808080},{2.25f,2,.9f,.2f,0,0,0xff808080},
        {-1.75f,-2,.9f,.2f,0,0,0xff808080},{2.25f,2,.9f,.2f,0,0,0xff808080},
        {2.25f,-2,.9f,.2f,0,0,0xff808080},{-1.75f,-2,.9f,.2f,0,0,0xff808080}};
    device->Clear(0,nullptr,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER,0,1,0);
    device->SetFVF(D3DFVF_XYZ|D3DFVF_NORMAL|D3DFVF_DIFFUSE);
    device->SetRenderState(D3DRS_SPECULARENABLE,1);
    SunShadow::SetSmokeGlow(device,true);
    device->DrawPrimitiveUP(D3DPT_TRIANGLELIST,2,glow,sizeof(G)); read_back(device);
    const uint32_t lit=at(17,15);
    CHECK(std::abs(int((lit>>16)&255)-int((lit>>8)&255)-51)<=2);
    SunShadow::SetSmokeGlow(device,false);device->SetRenderState(D3DRS_SPECULARENABLE,0);
    { SunShadow::ReceiverExclusion excluded(device);draw(.9f,WHITE,.25f);CHECK_PIXEL(17,15,WHITE); }
    { SunShadow::SceneScope nested(device,true);
      CHECK(!SunShadow::FillSmoke(device,nullptr,0,0));draw(.9f,WHITE,.25f);CHECK_PIXEL(17,15,WHITE); }
    draw(.9f,WHITE,.25f);CHECK(pixels==particle);
    CHECK(SunShadow::FillSmoke(device,nullptr,0,0));draw(.9f,WHITE,.25f);CHECK_PIXEL(17,15,WHITE);
    CHECK(!gpu->Sun_Smoke_Texture());
    device->SetTransform(D3DTS_VIEW,&identity);device->SetTransform(D3DTS_PROJECTION,&identity);
    viewport={0,0,SIZE,SIZE,0,1};device->SetViewport(&viewport);
    device->SetRenderState(D3DRS_ZWRITEENABLE,1);
    // Cloaked casters use Windows' Bayer coverage; resetting the effect restores full coverage.
    for(float opacity : {0.0f,.5f,1.0f,2.0f}) {
        CHECK(SunShadow::Begin(device,32));
        const float parameters[]={0,0,0,opacity};
        SunShadow::SetCasterEffect(device,DRAW_EFFECT_CLOAK_SHADOW,parameters);
        ScreenVertex v[6];quad(v,0,0,32,32,.3f,WHITE);device->SetFVF(SCREEN_FVF);
        device->DrawPrimitiveUP(D3DPT_TRIANGLELIST,2,v,sizeof(ScreenVertex));
        CHECK(SunShadow::End(device));std::vector<float> depth;CHECK(device->Read_Sun_Shadow(depth));
        unsigned occupied=0;for(float z:depth) occupied+=z<1;
        CHECK(occupied==unsigned(std::min(opacity,1.0f)*.5f*1024));
    }
    CHECK(SunShadow::Begin(device,32));
    ScreenVertex v[6];quad(v,0,0,32,32,.3f,WHITE);device->SetFVF(SCREEN_FVF);
    device->DrawPrimitiveUP(D3DPT_TRIANGLELIST,2,v,sizeof(ScreenVertex));CHECK(SunShadow::End(device));
    std::vector<float> depth;CHECK(device->Read_Sun_Shadow(depth));for(float z:depth) CHECK(z<1);
    }
    printf("smoke: moments/front, offset/depth-range reconstruction, queued refill, alpha particles, fire glow, exclusions, empty map and cloak coverage checked\n");
}

// The tree vertex program stores sway metadata in the normal slot. Verify its
// actual displaced alpha silhouette, then receive sunlight through that same program.
static void check_sun_trees(PosixDevice9 *device)
{
    {
    SunShadow::SceneScope scene(device,true);
    D3DMATRIX identity={};identity._11=identity._22=identity._33=identity._44=1;
    for(auto transform:{D3DTS_WORLD,D3DTS_VIEW,D3DTS_PROJECTION}) device->SetTransform(transform,&identity);
    begin(device,BLUE);
    device->SetRenderState(D3DRS_ZENABLE,1);device->SetRenderState(D3DRS_ZWRITEENABLE,1);
    device->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE);
    device->SetRenderState(D3DRS_ALPHABLENDENABLE,0);
    device->SetRenderState(D3DRS_ALPHATESTENABLE,1);
    device->SetRenderState(D3DRS_ALPHAFUNC,D3DCMP_GREATEREQUAL);
    device->SetRenderState(D3DRS_ALPHAREF,96);
    const uint32_t leaf[]={WHITE,0x00ffffff};
    IDirect3DTexture9 *texture=make_texture(device,2,1,D3DFMT_A8R8G8B8,leaf,sizeof(leaf));
    device->SetTexture(0,texture);
    device->SetSamplerState(0,D3DSAMP_MINFILTER,D3DTEXF_POINT);
    device->SetSamplerState(0,D3DSAMP_MAGFILTER,D3DTEXF_POINT);
    device->SetTextureStageState(0,D3DTSS_COLOROP,D3DTOP_MODULATE);
    device->SetTextureStageState(0,D3DTSS_COLORARG1,D3DTA_TEXTURE);
    device->SetTextureStageState(0,D3DTSS_COLORARG2,D3DTA_DIFFUSE);
    device->SetTextureStageState(0,D3DTSS_ALPHAOP,D3DTOP_MODULATE);
    device->SetTextureStageState(0,D3DTSS_ALPHAARG1,D3DTA_TEXTURE);
    device->SetTextureStageState(0,D3DTSS_ALPHAARG2,D3DTA_DIFFUSE);
    device->SetTextureStageState(1,D3DTSS_COLOROP,D3DTOP_DISABLE);
    const RenderUInt32 tokens[]={0xfffe0101u,0x0000ffffu};
    IDirect3DVertexShader9 *shader=nullptr;
    CHECK(device->CreateVertexShader(tokens,&shader)==D3D_OK);
    PosixDevice_Name_Shader(shader,"trees.vso");device->SetVertexShader(shader);
    device->SetFVF(D3DFVF_XYZ|D3DFVF_NORMAL|D3DFVF_DIFFUSE|D3DFVF_TEX1);
    device->SetVertexShaderConstantF(4,&identity._11,4);
    const float sway[]={1,0,0,0};device->SetVertexShaderConstantF(9,sway,1);
    const float noSway[]={0,0,0,0};device->SetVertexShaderConstantF(8,noSway,1);
    struct TreeVertex {float x,y,z,type,shade,base;uint32_t color;float u,v;};
    TreeVertex leafQuad[]={
        {-.75f,.5f,.25f,1,1,0,WHITE,0,0},{.25f,.5f,.25f,1,1,0,WHITE,1,0},
        {-.75f,-.5f,.25f,1,1,0,WHITE,0,1},{.25f,.5f,.25f,1,1,0,WHITE,1,0},
        {.25f,-.5f,.25f,1,1,0,WHITE,1,1},{-.75f,-.5f,.25f,1,1,0,WHITE,0,1}};
    CHECK(SunShadow::Begin(device,32));
    device->DrawPrimitiveUP(D3DPT_TRIANGLELIST,2,leafQuad,sizeof(TreeVertex));
    CHECK(SunShadow::End(device));
    std::vector<float> depth;CHECK(device->Read_Sun_Shadow(depth));
    if(depth.size()==32*32) {
        CHECK(depth[16*32+6]==1); // unswayed location is now clear
        CHECK(depth[16*32+12]>.249f && depth[16*32+12]<.251f);
        CHECK(depth[16*32+20]==1); // transparent half of the displaced leaf
    }
    CHECK(SunShadow::Parameters(device,0,.5f,0,0,0,1,0));
    device->SetTexture(0,nullptr);
    device->SetTextureStageState(0,D3DTSS_COLOROP,D3DTOP_SELECTARG2);
    device->SetTextureStageState(0,D3DTSS_ALPHAOP,D3DTOP_SELECTARG2);
    for(auto &v:leafQuad) {v.x=(v.x+.75f)*2-1;v.y*=2;v.z=.75f;v.type=0;}
    device->Clear(0,nullptr,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER,BLUE,1,0);
    device->DrawPrimitiveUP(D3DPT_TRIANGLELIST,2,leafQuad,sizeof(TreeVertex));read_back(device);
    CHECK_PIXEL(12,16,0xff808080);CHECK_PIXEL(20,16,WHITE);
    device->SetVertexShader(nullptr);shader->Release();texture->Release();
    device->SetRenderState(D3DRS_ALPHATESTENABLE,0);
    device->SetRenderState(D3DRS_CULLMODE,D3DCULL_CCW);
    }
    printf("sun trees: displaced alpha silhouette, transparent gaps and receiving checked\n");
}

static void check_sun_reset(PosixDevice9 *device)
{
    begin(device,BLUE);
    CHECK(SunShadow::Begin(device,32)); CHECK(SunShadow::End(device));
    CHECK(SunShadow::Parameters(device,0,.5f,0,0,0,1,0));
    CHECK(SunShadow::OwnsSceneShadows(device));
    // Keep work queued against the old scene allocation while Reset drains it.
    ScreenVertex vertices[6]; quad(vertices,0,0,32,32,.5f,RED);
    CHECK(device->DrawPrimitiveUP(D3DPT_TRIANGLELIST,2,vertices,sizeof(vertices[0]))==D3D_OK);
    D3DPRESENT_PARAMETERS original=device->Get_Present_Parameters(), resized=original;
    resized.BackBufferWidth=48; resized.BackBufferHeight=40;
    CHECK(device->Reset(&resized)==D3D_OK);
    std::vector<float> depth;
    CHECK(!device->Read_Sun_Shadow(depth)); CHECK(!SunShadow::OwnsSceneShadows(device));
    CHECK(!SunShadow::Parameters(device,0,.5f,0,0,0,1,0));
    D3DVIEWPORT9 viewport; device->GetViewport(&viewport);
    CHECK(viewport.Width==48 && viewport.Height==40);
    begin(device,GREEN);
    CHECK(SunShadow::Begin(device,64)); CHECK(SunShadow::End(device));
    CHECK(SunShadow::Parameters(device,0,.5f,0,0,0,1,0));
    CHECK(SunShadow::OwnsSceneShadows(device));
    CHECK(device->Read_Sun_Shadow(depth)); CHECK(depth.size()==64*64);
    for(float value:depth) CHECK(value==1.0f);
    std::vector<uint8_t> resizedPixels;
    CHECK(device->Get_Gpu()->Read_Back(device->Get_Gpu()->Back_Buffer(),48,40,resizedPixels));
    for(size_t i=0;i<resizedPixels.size();i+=4)
        CHECK(resizedPixels[i]==0 && resizedPixels[i+1]==255 && resizedPixels[i+2]==0 && resizedPixels[i+3]==255);
    CHECK(device->Reset(&original)==D3D_OK);
    CHECK(!SunShadow::OwnsSceneShadows(device));
    printf("sun reset: pending work drained, stale capture invalidated, resized capture and scene verified\n");
}

static PosixDevice9 *sun_fault_device(IDirect3D9 *adapter)
{
    D3DPRESENT_PARAMETERS parameters={};
    parameters.BackBufferWidth=parameters.BackBufferHeight=SIZE;
    parameters.BackBufferFormat=D3DFMT_X8R8G8B8;
    parameters.Windowed=parameters.EnableAutoDepthStencil=1;
    parameters.AutoDepthStencilFormat=D3DFMT_D24S8;
    IDirect3DDevice9 *created=NULL;
    CHECK(adapter->CreateDevice(0,D3DDEVTYPE_HAL,NULL,0,&parameters,&created)==D3D_OK);
    if(!created) return NULL;
    PosixDevice9 *device=static_cast<PosixDevice9*>(created);
    if(device->Create_Gpu_Frame(true)!=D3D_OK) { CHECK(false);device->Release();return NULL; }
    begin(device,BLUE);
    D3DMATRIX identity={};identity._11=identity._22=identity._33=identity._44=1;
    device->SetTransform(D3DTS_WORLD,&identity);device->SetTransform(D3DTS_VIEW,&identity);
    device->SetTransform(D3DTS_PROJECTION,&identity);
    device->SetFVF(D3DFVF_XYZ|D3DFVF_DIFFUSE);
    device->SetRenderState(D3DRS_ZENABLE,1);device->SetRenderState(D3DRS_ZWRITEENABLE,1);
    device->SetRenderState(D3DRS_ZFUNC,D3DCMP_LESSEQUAL);
    device->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE);
    device->SetRenderState(D3DRS_ALPHATESTENABLE,0);device->SetRenderState(D3DRS_ALPHABLENDENABLE,0);
    return device;
}

static void check_sun_failures(IDirect3D9 *adapter)
{
    Sdl_Sun_Shadow_Fault_Point() = nullptr;
    struct V {float x,y,z;uint32_t color;};
    const V caster[]={{-1,1,.25f,WHITE},{1,1,.25f,WHITE},{-1,-1,.25f,WHITE},
        {1,1,.25f,WHITE},{1,-1,.25f,WHITE},{-1,-1,.25f,WHITE}};
    const V left[]={{-1,1,.75f,WHITE},{0,1,.75f,WHITE},{-1,-1,.75f,WHITE},
        {0,1,.75f,WHITE},{0,-1,.75f,WHITE},{-1,-1,.75f,WHITE}};
    const V right[]={{0,1,.75f,WHITE},{1,1,.75f,WHITE},{0,-1,.75f,WHITE},
        {1,1,.75f,WHITE},{1,-1,.75f,WHITE},{0,-1,.75f,WHITE}};
    const auto parameters=[](PosixDevice9 *device){return SunShadow::Parameters(device,0,.5f,0,0,0,1,0);};
    for(const char *fault : {"smoke-target","smoke-pipeline"}) {
        PosixDevice9 *smokeDevice=sun_fault_device(adapter);if(!smokeDevice) continue;
        { SunShadow::SceneScope scene(smokeDevice,true);
        CHECK(SunShadow::Begin(smokeDevice,32));
        CHECK(smokeDevice->DrawPrimitiveUP(D3DPT_TRIANGLELIST,2,caster,sizeof(V))==D3D_OK);
        CHECK(SunShadow::End(smokeDevice));
        const float cloud[]={0,0,.5f,.5f,2};
        Sdl_Sun_Shadow_Fault_Point() = fault;
        CHECK(!SunShadow::FillSmoke(smokeDevice,cloud,1,.6f));
        CHECK(!smokeDevice->Get_Gpu()->Sun_Smoke_Texture());
        CHECK(parameters(smokeDevice));CHECK(SunShadow::OwnsSceneShadows(smokeDevice));
        smokeDevice->DrawPrimitiveUP(D3DPT_TRIANGLELIST,2,left,sizeof(V));
        read_back(smokeDevice);CHECK_PIXEL(8,16,0xFF808080); // core depth map still works
        Sdl_Sun_Shadow_Fault_Point() = nullptr;
        CHECK(!SunShadow::FillSmoke(smokeDevice,cloud,1,.6f)); // bounded refusal, no per-frame retry spam
        CHECK(!SunShadow::FillSmoke(smokeDevice,nullptr,0,0));
        }
        smokeDevice->Release();
        printf("smoke fault %s: particle fallback signalled, core shadows retained, refusal latched\n",fault);
    }
    for(const char *fault: {"receiver-program","receiver-pipeline"}) for(bool late: {false,true}) {
        PosixDevice9 *device=sun_fault_device(adapter);if(!device) continue;
        {
            SunShadow::SceneScope scene(device,true);
            CHECK(SunShadow::Begin(device,32));
            CHECK(device->DrawPrimitiveUP(D3DPT_TRIANGLELIST,2,caster,sizeof(V))==D3D_OK);
            CHECK(SunShadow::End(device));CHECK(parameters(device));
            if(late) {
                CHECK(device->DrawPrimitiveUP(D3DPT_TRIANGLELIST,2,left,sizeof(V))==D3D_OK);
                read_back(device);CHECK_PIXEL(8,16,0xFF808080);CHECK_PIXEL(24,16,BLUE);
                // A new shader/pipeline key after an actual submission, so the fault
                // cannot be repaired just by discarding the current CPU draw batch.
                device->SetRenderState(D3DRS_ALPHATESTENABLE,1);
                device->SetRenderState(D3DRS_ALPHAFUNC,D3DCMP_GREATER);
                device->SetRenderState(D3DRS_ALPHAREF,8);
                CHECK(parameters(device)); // changing parameters cannot forget earlier draws
            }
            Sdl_Sun_Shadow_Fault_Point() = fault;
            const unsigned recorded=device->Draws_Recorded();
            CHECK(device->DrawPrimitiveUP(D3DPT_TRIANGLELIST,2,right,sizeof(V))==D3D_OK);
            CHECK(device->Draws_Recorded()==recorded+1);CHECK(device->Draw_Refusals().empty());
            read_back(device);CHECK_PIXEL(24,16,WHITE);
            CHECK_PIXEL(8,16,late ? 0xFF808080 : BLUE);
            CHECK(!SunShadow::ReceivesView(device));CHECK(SunShadow::OwnsSceneShadows(device)==late);
            CHECK(!parameters(device));CHECK(SunShadow::OwnsSceneShadows(device)==late);
            Sdl_Sun_Shadow_Fault_Point() = nullptr;
            // The failure latch now selects ordinary draws after clearing the fault point.
            CHECK(device->DrawPrimitiveUP(D3DPT_TRIANGLELIST,2,left,sizeof(V))==D3D_OK);
            read_back(device);CHECK_PIXEL(8,16,WHITE);CHECK_PIXEL(24,16,WHITE);
        }
        CHECK(!SunShadow::OwnsSceneShadows(device));
        {
            SunShadow::SceneScope next(device,true);
            CHECK(!SunShadow::Begin(device,32));CHECK(!parameters(device));
            CHECK(!SunShadow::OwnsSceneShadows(device));
            device->Clear(0,NULL,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER,BLUE,1,0);
            CHECK(device->DrawPrimitiveUP(D3DPT_TRIANGLELIST,2,right,sizeof(V))==D3D_OK);
            read_back(device);CHECK_PIXEL(24,16,WHITE);
        }
        D3DPRESENT_PARAMETERS reset=device->Get_Present_Parameters();CHECK(device->Reset(&reset)==D3D_OK);
        CHECK(!SunShadow::Begin(device,32)); // Reset retains the refused program/pipeline cache
        CHECK(!SunShadow::Begin(device,32));
        device->Release(); // the next case recreates the device and can receive again
        printf("sun fault %s %s: geometry retained, submitted pixels preserved, ownership and next-scene latch checked\n",
            fault,late ? "after submission" : "before receivers");
    }
    PosixDevice9 *device=sun_fault_device(adapter);
    if(device) {
        {
            SunShadow::SceneScope scene(device,true);
            Sdl_Sun_Shadow_Fault_Point() = "target";
            CHECK(!SunShadow::Begin(device,32));CHECK(!parameters(device));
            CHECK(!SunShadow::OwnsSceneShadows(device));
            CHECK(device->DrawPrimitiveUP(D3DPT_TRIANGLELIST,2,right,sizeof(V))==D3D_OK);
            read_back(device);CHECK_PIXEL(24,16,WHITE);
            Sdl_Sun_Shadow_Fault_Point() = nullptr;
            CHECK(SunShadow::Begin(device,32));
            CHECK(device->DrawPrimitiveUP(D3DPT_TRIANGLELIST,2,caster,sizeof(V))==D3D_OK);
            CHECK(SunShadow::End(device));CHECK(parameters(device));
            CHECK(device->DrawPrimitiveUP(D3DPT_TRIANGLELIST,2,left,sizeof(V))==D3D_OK);
            Sdl_Sun_Shadow_Fault_Point() = "target";
            CHECK(!SunShadow::Begin(device,64));CHECK(!parameters(device));
            CHECK(!SunShadow::OwnsSceneShadows(device));
            read_back(device);CHECK_PIXEL(8,16,0xFF808080);CHECK_PIXEL(24,16,WHITE);
            Sdl_Sun_Shadow_Fault_Point() = nullptr;
            CHECK(SunShadow::Begin(device,64));CHECK(SunShadow::End(device));CHECK(parameters(device));
            std::vector<float> depth;CHECK(device->Read_Sun_Shadow(depth));CHECK(depth.size()==64*64);
            for(float value:depth) CHECK(value==1.0f);
            CHECK(device->DrawPrimitiveUP(D3DPT_TRIANGLELIST,2,left,sizeof(V))==D3D_OK);
            read_back(device);CHECK_PIXEL(8,16,WHITE);CHECK_PIXEL(24,16,WHITE);
        }
        device->Release();
        printf("sun allocation fault: first allocation and replacement refused, queued old reader retained, recovery and empty clear checked\n");
    }
    Sdl_Sun_Shadow_Fault_Point() = nullptr;
}

#endif

static void check_refusal(PosixDevice9 *device)
{
	IDirect3DCubeTexture9 *cube = NULL;
	CHECK(device->CreateCubeTexture(4, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &cube, NULL) == D3D_OK);
	begin(device, BLUE);
	device->SetTexture(0, cube);
	device->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
	ScreenVertex v[6];
	quad(v, 0, 0, 32, 32, 0.5f, RED);
	const unsigned int recorded = device->Draws_Recorded();
	CHECK(device->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 2, v, sizeof(ScreenVertex)) == D3D_OK);
	CHECK(device->Draws_Recorded() == recorded);
	unsigned int refused = 0;
	for (std::map<std::string, unsigned int>::const_iterator it = device->Draw_Refusals().begin();
		it != device->Draw_Refusals().end(); ++it) {
		if (it->first.find("cube") != std::string::npos) refused += it->second;
	}
	CHECK(refused == 1);
	read_back(device);
	CHECK_PIXEL(16, 16, BLUE);
	device->SetTexture(0, NULL);
	cube->Release();
}

// C1: whether one draw's triangles share derivatives.  Two triangles meeting on the diagonal, one
// sampling about a texel a pixel and one about 32, through a texture whose every level is its own solid
// colour and point sampled everywhere: a pixel's colour names the level it took.  Drawn as one call and
// as two; with derivatives formed per primitive the two maps agree.  Then a grid of triangles two and
// four pixels across, alternating the scales, one call against one per triangle: they agree too.  Then
// the same two triangles INDEXED, sharing their edge's vertices: there one call differs from two (C1).
// Measured on Metal (Apple silicon), 2026-09-26: the GPU forms a quad across an edge the index buffer
// shares, so a pixel beside it takes the neighbour's derivatives; unindexed, or a call per triangle, it
// does not.
static int level_of(uint32_t colour)
{
	return (int)((colour >> 16) & 0xFF) / 32;
}

static void check_quad_derivatives(PosixDevice9 *device)
{
	const unsigned EDGE = 64, LEVELS = 7;
	IDirect3DTexture9 *texture = NULL;
	device->CreateTexture(EDGE, EDGE, LEVELS, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &texture, NULL);
	for (unsigned level = 0; level < LEVELS; ++level) {
		D3DLOCKED_RECT locked;
		texture->LockRect(level, &locked, NULL, 0);
		const unsigned size = EDGE >> level;
		for (unsigned y = 0; y < size; ++y)
			for (unsigned x = 0; x < size; ++x)
				((uint32_t *)((uint8_t *)locked.pBits + y * locked.Pitch))[x] = 0xFF000000u | ((level * 32 + 16) << 16);
		texture->UnlockRect(level);
	}
	const float S = (float)SIZE;
	const ScreenVertex triangles[6] = {
		{ 0, 0, 0.5f, 1, WHITE, 0, 0 }, { S, 0, 0.5f, 1, WHITE, 1, 0 }, { 0, S, 0.5f, 1, WHITE, 0, 1 },
		{ S, 0, 0.5f, 1, WHITE, 16, 0 }, { S, S, 0.5f, 1, WHITE, 16, 16 }, { 0, S, 0.5f, 1, WHITE, 0, 16 } };
	int maps[2][SIZE * SIZE];
	for (int calls = 1; calls <= 2; ++calls) {
		begin(device, BLUE);
		device->SetTexture(0, texture);
		device->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
		device->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
		device->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_POINT);
		device->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
		device->SetSamplerState(0, D3DSAMP_MIPFILTER, D3DTEXF_POINT);
		device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
		if (calls == 1) {
			device->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 2, triangles, sizeof(ScreenVertex));
		}
		else {
			device->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 1, triangles, sizeof(ScreenVertex));
			device->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 1, triangles + 3, sizeof(ScreenVertex));
		}
		read_back(device);
		for (unsigned y = 0; y < SIZE; ++y)
			for (unsigned x = 0; x < SIZE; ++x)
				maps[calls - 1][y * SIZE + x] = level_of(at(x, y));
	}
	int differ = 0;
	for (unsigned i = 0; i < SIZE * SIZE; ++i) differ += maps[0][i] != maps[1][i];
	CHECK(differ == 0);
	CHECK(maps[0][2 * SIZE + 2] == 1 && maps[0][29 * SIZE + 29] == 5);	// the two scales took their levels
	// The same with triangles a few pixels across, as the small models C1 found are: a grid of cells,
	// each two triangles, alternating between the two scales, drawn as one call and as one per triangle.
	for (unsigned cell_size = 2; cell_size <= 4; cell_size *= 2) {
		std::vector<ScreenVertex> grid;
		for (unsigned cy = 0; cy < SIZE; cy += cell_size) {
			for (unsigned cx = 0; cx < SIZE; cx += cell_size) {
				const float l = (float)cx + 0.3f, t = (float)cy + 0.3f, r = l + cell_size, b = t + cell_size;
				const float scale = (((cx + cy) / cell_size) & 1) ? 16.0f / SIZE : 1.0f / SIZE;
				const ScreenVertex c[4] = {
					{ l, t, 0.5f, 1, WHITE, l * scale, t * scale }, { r, t, 0.5f, 1, WHITE, r * scale, t * scale },
					{ l, b, 0.5f, 1, WHITE, l * scale, b * scale }, { r, b, 0.5f, 1, WHITE, r * scale, b * scale } };
				grid.push_back(c[0]); grid.push_back(c[1]); grid.push_back(c[2]);
				grid.push_back(c[1]); grid.push_back(c[3]); grid.push_back(c[2]);
			}
		}
		for (int calls = 1; calls <= 2; ++calls) {
			begin(device, BLUE);
			device->SetTexture(0, texture);
			device->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
			device->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
			device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
			if (calls == 1) {
				device->DrawPrimitiveUP(D3DPT_TRIANGLELIST, (unsigned)grid.size() / 3, &grid[0], sizeof(ScreenVertex));
			}
			else {
				for (size_t i = 0; i < grid.size(); i += 3) {
					device->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 1, &grid[i], sizeof(ScreenVertex));
				}
			}
			read_back(device);
			for (unsigned y = 0; y < SIZE; ++y)
				for (unsigned x = 0; x < SIZE; ++x)
					maps[calls - 1][y * SIZE + x] = level_of(at(x, y));
		}
		int small_differ = 0;
		for (unsigned i = 0; i < SIZE * SIZE; ++i) small_differ += maps[0][i] != maps[1][i];
		if (small_differ != 0) {
			++failures;
			printf("FAIL quad derivatives, %ux%u cells: %d pixels take a different level in one draw than in one per triangle\n",
				cell_size, cell_size, small_differ);
		}
	}
	// Indexed, with the two triangles sharing their edge's vertices (and so those vertices' coordinates),
	// the third vertex of one far out in the texture: one indexed call, the same as two indexed calls,
	// and as one unindexed call of the same six vertices.
	{
		const ScreenVertex shared[4] = {
			{ 0, 0, 0.5f, 1, WHITE, 0, 0 }, { S, 0, 0.5f, 1, WHITE, 1, 0 }, { 0, S, 0.5f, 1, WHITE, 0, 1 },
			{ S, S, 0.5f, 1, WHITE, 16, 16 } };
		const uint16_t list[6] = { 0, 1, 2, 1, 3, 2 };
		IDirect3DVertexBuffer9 *vb = NULL;
		IDirect3DIndexBuffer9 *ib = NULL;
		void *locked = NULL;
		device->CreateVertexBuffer(sizeof(shared), D3DUSAGE_WRITEONLY, SCREEN_FVF, D3DPOOL_MANAGED, &vb, NULL);
		vb->Lock(0, 0, &locked, 0); memcpy(locked, shared, sizeof(shared)); vb->Unlock();
		device->CreateIndexBuffer(sizeof(list), D3DUSAGE_WRITEONLY, D3DFMT_INDEX16, D3DPOOL_MANAGED, &ib, NULL);
		ib->Lock(0, 0, &locked, 0); memcpy(locked, list, sizeof(list)); ib->Unlock();
		ScreenVertex expanded[6];
		for (int i = 0; i < 6; ++i) expanded[i] = shared[list[i]];
		int indexed[3][SIZE * SIZE];
		for (int way = 0; way < 3; ++way) {
			begin(device, BLUE);
			device->SetTexture(0, texture);
			device->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
			device->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
			device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
			if (way == 2) {
				device->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 2, expanded, sizeof(ScreenVertex));
			}
			else {
				device->SetStreamSource(0, vb, 0, sizeof(ScreenVertex));
				device->SetIndices(ib);
				if (way == 0) {
					device->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 0, 4, 0, 2);
				}
				else {
					device->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 0, 4, 0, 1);
					device->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 0, 4, 3, 1);
				}
				device->SetStreamSource(0, NULL, 0, 0);
				device->SetIndices(NULL);
			}
			read_back(device);
			for (unsigned y = 0; y < SIZE; ++y)
				for (unsigned x = 0; x < SIZE; ++x)
					indexed[way][y * SIZE + x] = level_of(at(x, y));
		}
		int one_two = 0, two_unindexed = 0;
		for (unsigned i = 0; i < SIZE * SIZE; ++i) {
			one_two += indexed[0][i] != indexed[1][i];
			two_unindexed += indexed[1][i] != indexed[2][i];
		}
		// Two indexed calls and one unindexed call are per primitive, as D3D9 drew them: they must agree.
		CHECK(two_unindexed == 0);
		// One indexed call is the GPU's own: on Metal (Apple silicon), 2026-09-26, 16 of the 32 pixels
		// along the shared edge take the other triangle's level, in 2x2 blocks - the quad is formed across
		// the edge the index buffer shares.  That is C1, a Mac-against-Windows difference, recorded and
		// not asserted: another GPU may not share its quads.
		printf("posix_gpu_draw_selfcheck: an indexed shared edge in one call differs from per-primitive at %d pixels"
			" (C1: this GPU forms quads across edges an index buffer shares)\n", one_two);
		vb->Release();
		ib->Release();
	}
	device->SetTexture(0, NULL);
	device->SetRenderState(D3DRS_CULLMODE, D3DCULL_CCW);
	texture->Release();
}

int main()
{
	IDirect3D9 *d3d = Direct3DCreate9(D3D_SDK_VERSION);
	D3DPRESENT_PARAMETERS parameters;
	memset(&parameters, 0, sizeof(parameters));
	parameters.BackBufferWidth = SIZE;
	parameters.BackBufferHeight = SIZE;
	parameters.BackBufferFormat = D3DFMT_X8R8G8B8;
	parameters.Windowed = 1;
	parameters.EnableAutoDepthStencil = 1;
	parameters.AutoDepthStencilFormat = D3DFMT_D24S8;
	IDirect3DDevice9 *created = NULL;
	CHECK(d3d->CreateDevice(0, D3DDEVTYPE_HAL, NULL, 0, &parameters, &created) == D3D_OK);
	PosixDevice9 *device = static_cast<PosixDevice9 *>(created);

	// Headless first: a draw succeeds and records nothing.
	ScreenVertex v[6];
	quad(v, 0, 0, 8, 8, 0.5f, RED);
	device->SetFVF(SCREEN_FVF);
	CHECK(device->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 2, v, sizeof(ScreenVertex)) == D3D_OK);
	CHECK(device->Draws_Recorded() == 0);
	CHECK(!SunShadow::Begin(device,32));
	CHECK(!SunShadow::Parameters(device,0,.5f,0,0,0,1,0));
	CHECK(!SunShadow::OwnsSceneShadows(device));

	if (!SDL_Init(SDL_INIT_VIDEO) || device->Create_Gpu_Frame(true) != D3D_OK) {
		printf("posix_gpu_draw_selfcheck: SKIP - no GPU device here: %s\n", SDL_GetError());
		device->Release();
		d3d->Release();
		return failures != 0 ? 1 : 77;
	}
	check_half_pixel_and_winding(device);
	check_transformed(device);
	check_textures(device);
	check_fans_and_buffers(device);
	check_min_vertex(device);
	check_clears_and_depth(device);
	check_render_targets(device);
	check_dirty_mips(device);
#if defined(__linux__)
	check_sun_capture(device);
	check_sun_receivers(device);
	check_sun_smoke(device);
	check_sun_trees(device);
	check_sun_reset(device);
#endif
	check_refusal(device);
	check_quad_derivatives(device);
#if defined(__linux__)
	check_sun_failures(d3d);
#endif

	printf("posix_gpu_draw_selfcheck: %s: %u draws recorded, %u textures and %u buffers uploaded, %u stale flushes\n",
		SDL_GetGPUDeviceDriver(device->Get_Gpu()->Device()), device->Draws_Recorded(),
		device->Get_Mirrors()->Textures_Uploaded(), device->Get_Mirrors()->Buffers_Uploaded(),
		device->Get_Mirrors()->Stale_Flushes());
	device->Release();
	d3d->Release();
	SDL_Quit();
	if (failures != 0) {
		printf("posix_gpu_draw_selfcheck: %d FAILED\n", failures);
		return 1;
	}
	return 0;
}
