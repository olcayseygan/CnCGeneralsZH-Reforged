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

// The backend driven the way the engine drives a device: set a transform, set a stage state, set a
// stream, draw.  Nothing here builds a shader or a state object by hand, because the point is that
// the engine does not either.
//
// What it checks is the colour that lands in the render target, which is the only thing that tells
// the whole chain apart from a chain that merely runs: a transform applied on the wrong side, a
// vertex colour read as the wrong format, a combiner argument crossed, all of them draw something.

#include "test_harness.h"

#include "dx11backend.h"
#include "dx11post.h"
#include "dx11resource.h"
#include "dx11water.h"
#include "Platform/HazardCloudCore.h"
#include "Platform/WaterConstants.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <chrono>
#include <thread>
#include <vector>
#include <windows.h>

static const unsigned TARGET_SIZE = 64;
static const DWORD BACKEND_FVF = D3DFVF_XYZ|D3DFVF_NORMAL|D3DFVF_TEX2|D3DFVF_DIFFUSE;

// All four bytes different, so a channel read in the wrong order is a wrong answer rather than a
// lucky one.  D3DCOLOR order is alpha, red, green, blue.
static const DWORD VERTEX_COLOUR = 0xff112233;
static const unsigned char EXPECTED_BLUE = 0x33;
static const unsigned char EXPECTED_GREEN = 0x22;
static const unsigned char EXPECTED_RED = 0x11;

struct BackendVertex
{
	float Position[3];
	float Normal[3];
	DWORD Diffuse;
	float TexCoord0[2];
	float TexCoord1[2];
};

static void set_identity(float matrix[16])
{
	memset(matrix, 0, sizeof(float) * 16);
	matrix[0] = 1.0f;
	matrix[5] = 1.0f;
	matrix[10] = 1.0f;
	matrix[15] = 1.0f;
}

// A quad already in clip space, drawn as two triangles the way every mesh in the engine is.
static const BackendVertex QUAD_VERTICES[4] = {
	{ { -1.0f, -1.0f, 0.5f }, { 0.0f, 0.0f, 1.0f }, VERTEX_COLOUR, { 0.0f, 0.0f }, { 0.0f, 0.0f } },
	{ { -1.0f,  1.0f, 0.5f }, { 0.0f, 0.0f, 1.0f }, VERTEX_COLOUR, { 0.0f, 0.0f }, { 0.0f, 0.0f } },
	{ {  1.0f,  1.0f, 0.5f }, { 0.0f, 0.0f, 1.0f }, VERTEX_COLOUR, { 0.0f, 0.0f }, { 0.0f, 0.0f } },
	{ {  1.0f, -1.0f, 0.5f }, { 0.0f, 0.0f, 1.0f }, VERTEX_COLOUR, { 0.0f, 0.0f }, { 0.0f, 0.0f } }
};

static const unsigned short QUAD_INDICES[6] = { 0, 1, 2, 0, 2, 3 };

// Sets the backend up the way an unlit textureless draw is set up, and leaves it ready to draw.
static void configure_unlit_pass_through(DX11BackendClass & backend)
{
	float identity[16];
	set_identity(identity);
	backend.Set_Transform(D3DTS_WORLD, identity);
	backend.Set_Transform(D3DTS_VIEW, identity);
	backend.Set_Transform(D3DTS_PROJECTION, identity);

	backend.Set_Render_State(D3DRS_LIGHTING, FALSE);
	backend.Set_Render_State(D3DRS_COLORVERTEX, TRUE);
	backend.Set_Render_State(D3DRS_DIFFUSEMATERIALSOURCE, D3DMCS_COLOR1);
	backend.Set_Render_State(D3DRS_AMBIENTMATERIALSOURCE, D3DMCS_MATERIAL);
	backend.Set_Render_State(D3DRS_EMISSIVEMATERIALSOURCE, D3DMCS_MATERIAL);
	backend.Set_Render_State(D3DRS_SPECULARMATERIALSOURCE, D3DMCS_MATERIAL);
	backend.Set_Render_State(D3DRS_ZENABLE, D3DZB_FALSE);
	backend.Set_Render_State(D3DRS_ZWRITEENABLE, FALSE);
	backend.Set_Render_State(D3DRS_CULLMODE, D3DCULL_NONE);
	backend.Set_Render_State(D3DRS_ALPHABLENDENABLE, FALSE);

	// One stage, selecting the diffuse colour, with nothing bound: the combiner that hands the
	// vertex colour through untouched.
	backend.Set_Texture_Stage_State(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
	backend.Set_Texture_Stage_State(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
	backend.Set_Texture_Stage_State(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
	backend.Set_Texture_Stage_State(0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);
	backend.Set_Texture_Stage_State(0, D3DTSS_TEXCOORDINDEX, D3DTSS_TCI_PASSTHRU);
	backend.Set_Texture_Stage_State(0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
	backend.Set_Texture_Stage_State(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
	backend.Set_Texture_Stage_State(1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);

	backend.Set_Vertex_Format(BACKEND_FVF);
}

TEST(dx11backend_a_draw_set_up_the_d3d9_way_lands_in_the_render_target)
{
	DX11DeviceClass device;
	device.Request_Debug_Layer();
	CHECK(device.Create_Offscreen());
	ID3D11Device * d3d = device.Get_Device();

	DX11BackendClass backend;
	CHECK(backend.Initialise(&device));

	D3D11_TEXTURE2D_DESC target_description;
	memset(&target_description, 0, sizeof(target_description));
	target_description.Width = TARGET_SIZE;
	target_description.Height = TARGET_SIZE;
	target_description.MipLevels = 1;
	target_description.ArraySize = 1;
	target_description.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
	target_description.SampleDesc.Count = 1;
	target_description.Usage = D3D11_USAGE_DEFAULT;
	target_description.BindFlags = D3D11_BIND_RENDER_TARGET;

	ID3D11Texture2D * target = NULL;
	CHECK(SUCCEEDED(d3d->CreateTexture2D(&target_description, NULL, &target)));

	D3D11_TEXTURE2D_DESC staging_description = target_description;
	staging_description.Usage = D3D11_USAGE_STAGING;
	staging_description.BindFlags = 0;
	staging_description.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

	ID3D11Texture2D * staging = NULL;
	CHECK(SUCCEEDED(d3d->CreateTexture2D(&staging_description, NULL, &staging)));

	ID3D11RenderTargetView * target_view = NULL;
	CHECK(SUCCEEDED(d3d->CreateRenderTargetView(target, NULL, &target_view)));

	const float clear_colour[4] = { 0.0f, 1.0f, 0.0f, 1.0f };
	device.Get_Context()->ClearRenderTargetView(target_view, clear_colour);
	device.Get_Context()->OMSetRenderTargets(1, &target_view, NULL);

	backend.Set_Viewport(0, 0, TARGET_SIZE, TARGET_SIZE);
	configure_unlit_pass_through(backend);

	// The buffers, asked for the way the engine asks: a managed vertex buffer filled once and a
	// managed index buffer beside it.
	ID3D11Buffer * vertices = NULL;
	CHECK(DX11Resource_Create_Vertex_Buffer(d3d, sizeof(QUAD_VERTICES), D3DPOOL_MANAGED, 0,
		QUAD_VERTICES, &vertices));

	ID3D11Buffer * indices = NULL;
	CHECK(DX11Resource_Create_Index_Buffer(d3d, sizeof(QUAD_INDICES), D3DPOOL_MANAGED, 0,
		QUAD_INDICES, &indices));

	backend.Set_Stream_Source(vertices, sizeof(BackendVertex), 0);
	backend.Set_Indices(indices, DXGI_FORMAT_R16_UINT);

	CHECK(backend.Draw_Indexed_Triangles(6, 0, 0));

	device.Get_Context()->CopyResource(staging, target);

	D3D11_MAPPED_SUBRESOURCE mapped;
	CHECK(SUCCEEDED(device.Get_Context()->Map(staging, 0, D3D11_MAP_READ, 0, &mapped)));

	const unsigned char * centre = static_cast<const unsigned char *>(mapped.pData)
		+ mapped.RowPitch * (TARGET_SIZE / 2) + (TARGET_SIZE / 2) * 4;
	if (centre[0] != EXPECTED_BLUE) {
		printf("  centre pixel bgra %02x %02x %02x %02x\n",
			centre[0], centre[1], centre[2], centre[3]);
	}
	CHECK_EQ(centre[0], EXPECTED_BLUE);
	CHECK_EQ(centre[1], EXPECTED_GREEN);
	CHECK_EQ(centre[2], EXPECTED_RED);

	// The quad covers the target, so a corner says the same thing.  A transform applied on the
	// wrong side leaves the centre right and the corners green.
	const unsigned char * corner = static_cast<const unsigned char *>(mapped.pData);
	CHECK_EQ(corner[0], EXPECTED_BLUE);
	CHECK_EQ(corner[2], EXPECTED_RED);

	device.Get_Context()->Unmap(staging, 0);

	unsigned pipelines = 0;
	unsigned long long made = 0;
	unsigned long long refused = 0;
	backend.Statistics(pipelines, made, refused);
	CHECK_EQ(pipelines, 1u);
	CHECK_EQ(made, 1ull);
	CHECK_EQ(refused, 0ull);

	indices->Release();
	vertices->Release();
	target_view->Release();
	staging->Release();
	target->Release();
	backend.Shutdown();
}

// Direct3D 9 takes a light in world space and lights in camera space, carrying the light through the
// view matrix itself.  The generated shader lights in camera space too, so the backend has to make
// the same product before the light goes up; without it a world space direction is dotted against a
// camera space normal and every model is shaded from the wrong angle.
//
// The view here is a quarter turn about x and the world is its inverse, so the quad and its normal
// come out exactly where they started and the only thing the view matrix touches is the light.  A
// world direction of (0,-1,0) becomes (0,0,-1) in camera space, which is straight at the quad: the
// fix lights it fully and the fault lights it not at all.
TEST(dx11backend_a_light_is_carried_into_camera_space)
{
	DX11DeviceClass device;
	CHECK(device.Create_Offscreen());
	ID3D11Device * d3d = device.Get_Device();

	DX11BackendClass backend;
	CHECK(backend.Initialise(&device));

	D3D11_TEXTURE2D_DESC target_description;
	memset(&target_description, 0, sizeof(target_description));
	target_description.Width = TARGET_SIZE;
	target_description.Height = TARGET_SIZE;
	target_description.MipLevels = 1;
	target_description.ArraySize = 1;
	target_description.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
	target_description.SampleDesc.Count = 1;
	target_description.Usage = D3D11_USAGE_DEFAULT;
	target_description.BindFlags = D3D11_BIND_RENDER_TARGET;

	ID3D11Texture2D * target = NULL;
	CHECK(SUCCEEDED(d3d->CreateTexture2D(&target_description, NULL, &target)));

	D3D11_TEXTURE2D_DESC staging_description = target_description;
	staging_description.Usage = D3D11_USAGE_STAGING;
	staging_description.BindFlags = 0;
	staging_description.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

	ID3D11Texture2D * staging = NULL;
	CHECK(SUCCEEDED(d3d->CreateTexture2D(&staging_description, NULL, &staging)));

	ID3D11RenderTargetView * target_view = NULL;
	CHECK(SUCCEEDED(d3d->CreateRenderTargetView(target, NULL, &target_view)));

	const float clear_colour[4] = { 0.0f, 1.0f, 0.0f, 1.0f };
	device.Get_Context()->ClearRenderTargetView(target_view, clear_colour);
	device.Get_Context()->OMSetRenderTargets(1, &target_view, NULL);

	backend.Set_Viewport(0, 0, TARGET_SIZE, TARGET_SIZE);
	configure_unlit_pass_through(backend);

	// A quarter turn about x, and the world matrix that undoes it.
	float view[16];
	set_identity(view);
	view[5] = 0.0f;  view[6] = 1.0f;
	view[9] = -1.0f; view[10] = 0.0f;

	float world[16];
	set_identity(world);
	world[5] = 0.0f;  world[6] = -1.0f;
	world[9] = 1.0f;  world[10] = 0.0f;

	backend.Set_Transform(D3DTS_WORLD, world);
	backend.Set_Transform(D3DTS_VIEW, view);

	backend.Set_Render_State(D3DRS_LIGHTING, TRUE);
	backend.Set_Render_State(D3DRS_SPECULARENABLE, FALSE);
	backend.Set_Render_State(D3DRS_DIFFUSEMATERIALSOURCE, D3DMCS_MATERIAL);
	backend.Set_Render_State(D3DRS_AMBIENT, 0);

	const float white[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
	const float black[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	backend.Set_Material(black, white, black, black, 1.0f);

	// Blue brightest, red dimmest, so a channel swap is a wrong answer rather than a lucky one.
	const float light_diffuse[4] = { 0.25f, 0.5f, 0.75f, 1.0f };
	const float direction[4] = { 0.0f, -1.0f, 0.0f, 0.0f };
	const float attenuation[4] = { 1.0f, 0.0f, 0.0f, 100000.0f };
	backend.Set_Light(0, D3DLIGHT_DIRECTIONAL, black, direction, light_diffuse, black,
		attenuation, black, black);

	ID3D11Buffer * vertices = NULL;
	CHECK(DX11Resource_Create_Vertex_Buffer(d3d, sizeof(QUAD_VERTICES), D3DPOOL_MANAGED, 0,
		QUAD_VERTICES, &vertices));

	ID3D11Buffer * indices = NULL;
	CHECK(DX11Resource_Create_Index_Buffer(d3d, sizeof(QUAD_INDICES), D3DPOOL_MANAGED, 0,
		QUAD_INDICES, &indices));

	backend.Set_Stream_Source(vertices, sizeof(BackendVertex), 0);
	backend.Set_Indices(indices, DXGI_FORMAT_R16_UINT);

	CHECK(backend.Draw_Indexed_Triangles(6, 0, 0));

	device.Get_Context()->CopyResource(staging, target);

	D3D11_MAPPED_SUBRESOURCE mapped;
	CHECK(SUCCEEDED(device.Get_Context()->Map(staging, 0, D3D11_MAP_READ, 0, &mapped)));

	const unsigned char * centre = static_cast<const unsigned char *>(mapped.pData)
		+ mapped.RowPitch * (TARGET_SIZE / 2) + (TARGET_SIZE / 2) * 4;
	if (centre[0] < 189 || centre[0] > 193) {
		printf("  centre pixel bgra %02x %02x %02x %02x\n",
			centre[0], centre[1], centre[2], centre[3]);
	}
	CHECK(centre[0] >= 189 && centre[0] <= 193);
	CHECK(centre[1] >= 126 && centre[1] <= 130);
	CHECK(centre[2] >= 62 && centre[2] <= 66);

	device.Get_Context()->Unmap(staging, 0);

	indices->Release();
	vertices->Release();
	target_view->Release();
	staging->Release();
	target->Release();
	backend.Shutdown();
}

// The Hemispheric Ambient option on a quad lit by the scene ambient alone, whose normal stays (0,0,1)
// in camera space.  A view with no turn has the world's up at (0,0,1), so the quad faces straight up
// and takes one plus the spread; turned half way round x, up is (0,0,-1) and the same quad faces
// straight down.  If the program read the wrong half of MaterialPower the two pixels would come out
// the same.  Tilted 45 degrees round x with no world, the quad still faces straight up; the view's
// third column in place of its third row would read it as a wall.  Off, the flat ambient comes back.
TEST(dx11backend_the_hemispheric_ambient_follows_the_view)
{
	DX11DeviceClass device;
	CHECK(device.Create_Offscreen());
	ID3D11Device * d3d = device.Get_Device();

	DX11BackendClass backend;
	CHECK(backend.Initialise(&device));

	D3D11_TEXTURE2D_DESC target_description;
	memset(&target_description, 0, sizeof(target_description));
	target_description.Width = TARGET_SIZE;
	target_description.Height = TARGET_SIZE;
	target_description.MipLevels = 1;
	target_description.ArraySize = 1;
	target_description.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
	target_description.SampleDesc.Count = 1;
	target_description.Usage = D3D11_USAGE_DEFAULT;
	target_description.BindFlags = D3D11_BIND_RENDER_TARGET;

	ID3D11Texture2D * target = NULL;
	CHECK(SUCCEEDED(d3d->CreateTexture2D(&target_description, NULL, &target)));

	D3D11_TEXTURE2D_DESC staging_description = target_description;
	staging_description.Usage = D3D11_USAGE_STAGING;
	staging_description.BindFlags = 0;
	staging_description.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

	ID3D11Texture2D * staging = NULL;
	CHECK(SUCCEEDED(d3d->CreateTexture2D(&staging_description, NULL, &staging)));

	ID3D11RenderTargetView * target_view = NULL;
	CHECK(SUCCEEDED(d3d->CreateRenderTargetView(target, NULL, &target_view)));

	device.Get_Context()->OMSetRenderTargets(1, &target_view, NULL);

	backend.Set_Viewport(0, 0, TARGET_SIZE, TARGET_SIZE);
	configure_unlit_pass_through(backend);

	backend.Set_Render_State(D3DRS_LIGHTING, TRUE);
	backend.Set_Render_State(D3DRS_SPECULARENABLE, FALSE);
	backend.Set_Render_State(D3DRS_AMBIENTMATERIALSOURCE, D3DMCS_MATERIAL);
	backend.Set_Render_State(D3DRS_DIFFUSEMATERIALSOURCE, D3DMCS_MATERIAL);
	backend.Set_Render_State(D3DRS_EMISSIVEMATERIALSOURCE, D3DMCS_MATERIAL);
	backend.Set_Render_State(D3DRS_AMBIENT, 0x00808080);

	const float white[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
	const float black[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	backend.Set_Material(white, black, black, black, 1.0f);

	ID3D11Buffer * vertices = NULL;
	CHECK(DX11Resource_Create_Vertex_Buffer(d3d, sizeof(QUAD_VERTICES), D3DPOOL_MANAGED, 0,
		QUAD_VERTICES, &vertices));

	ID3D11Buffer * indices = NULL;
	CHECK(DX11Resource_Create_Index_Buffer(d3d, sizeof(QUAD_INDICES), D3DPOOL_MANAGED, 0,
		QUAD_INDICES, &indices));

	backend.Set_Stream_Source(vertices, sizeof(BackendVertex), 0);
	backend.Set_Indices(indices, DXGI_FORMAT_R16_UINT);

	float identity[16];
	set_identity(identity);
	float half_turn[16];		// as the view and as the world that undoes it, so the same quad is drawn
	set_identity(half_turn);
	half_turn[5] = -1.0f;
	half_turn[10] = -1.0f;

	float tilt[16];
	set_identity(tilt);
	tilt[5] = 0.70710678f;  tilt[6] = 0.70710678f;
	tilt[9] = -0.70710678f; tilt[10] = 0.70710678f;

	struct Shot { bool Hemi; const float * View; const float * World; unsigned char Bgr[3]; };
	Shot shots[4] = {
		{ false, identity, identity, { 0, 0, 0 } },
		{ true, identity, identity, { 0, 0, 0 } },
		{ true, half_turn, half_turn, { 0, 0, 0 } },
		{ true, tilt, identity, { 0, 0, 0 } }
	};
	for (unsigned index = 0; index < 4; ++index) {
		const float clear_colour[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
		device.Get_Context()->ClearRenderTargetView(target_view, clear_colour);
		backend.Set_Hemi_Ambient(shots[index].Hemi);
		backend.Set_Transform(D3DTS_VIEW, shots[index].View);
		backend.Set_Transform(D3DTS_WORLD, shots[index].World);
		CHECK(backend.Draw_Indexed_Triangles(6, 0, 0));
		device.Get_Context()->CopyResource(staging, target);
		D3D11_MAPPED_SUBRESOURCE mapped;
		CHECK(SUCCEEDED(device.Get_Context()->Map(staging, 0, D3D11_MAP_READ, 0, &mapped)));
		const unsigned char * centre = static_cast<const unsigned char *>(mapped.pData)
			+ mapped.RowPitch * (TARGET_SIZE / 2) + (TARGET_SIZE / 2) * 4;
		memcpy(shots[index].Bgr, centre, 3);
		device.Get_Context()->Unmap(staging, 0);
	}

	// 0x80 flat; times 1.4, 1.3, 1.25 facing up and 0.6, 0.7, 0.75 facing down (blue, green, red).
	CHECK(shots[0].Bgr[0] >= 126 && shots[0].Bgr[0] <= 130);
	CHECK(shots[1].Bgr[0] >= 177 && shots[1].Bgr[0] <= 181);
	CHECK(shots[1].Bgr[1] >= 164 && shots[1].Bgr[1] <= 168);
	CHECK(shots[1].Bgr[2] >= 158 && shots[1].Bgr[2] <= 162);
	CHECK(shots[2].Bgr[0] >= 75 && shots[2].Bgr[0] <= 79);
	CHECK(shots[2].Bgr[1] >= 88 && shots[2].Bgr[1] <= 92);
	CHECK(shots[2].Bgr[2] >= 94 && shots[2].Bgr[2] <= 98);
	CHECK(shots[3].Bgr[0] >= 177 && shots[3].Bgr[0] <= 181);

	indices->Release();
	vertices->Release();
	target_view->Release();
	staging->Release();
	target->Release();
	backend.Shutdown();
}

// The same state twice is one pipeline, and a changed stage program is a second.  The engine sets
// the same handful of combinations thousands of times a frame, so a cache that missed would compile
// a shader per draw and the frame time would be the symptom rather than the picture.
TEST(dx11backend_the_same_state_resolves_to_one_pipeline)
{
	DX11DeviceClass device;
	CHECK(device.Create_Offscreen());

	DX11BackendClass backend;
	CHECK(backend.Initialise(&device));

	ID3D11Buffer * vertices = NULL;
	CHECK(DX11Resource_Create_Vertex_Buffer(device.Get_Device(), sizeof(QUAD_VERTICES),
		D3DPOOL_MANAGED, 0, QUAD_VERTICES, &vertices));

	ID3D11Buffer * indices = NULL;
	CHECK(DX11Resource_Create_Index_Buffer(device.Get_Device(), sizeof(QUAD_INDICES),
		D3DPOOL_MANAGED, 0, QUAD_INDICES, &indices));

	backend.Set_Viewport(0, 0, TARGET_SIZE, TARGET_SIZE);
	configure_unlit_pass_through(backend);
	backend.Set_Stream_Source(vertices, sizeof(BackendVertex), 0);
	backend.Set_Indices(indices, DXGI_FORMAT_R16_UINT);

	CHECK(backend.Draw_Indexed_Triangles(6, 0, 0));
	CHECK(backend.Draw_Indexed_Triangles(6, 0, 0));

	unsigned pipelines = 0;
	unsigned long long made = 0;
	unsigned long long refused = 0;
	backend.Statistics(pipelines, made, refused);
	CHECK_EQ(pipelines, 1u);
	CHECK_EQ(made, 2ull);

	// A different combiner program is a different pipeline.
	backend.Set_Texture_Stage_State(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
	backend.Set_Texture_Stage_State(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
	CHECK(backend.Draw_Indexed_Triangles(6, 0, 0));

	backend.Statistics(pipelines, made, refused);
	CHECK_EQ(pipelines, 2u);
	CHECK_EQ(refused, 0ull);

	indices->Release();
	vertices->Release();
	backend.Shutdown();
}

// The pass-through state under a perspective projection, the only kind of scene draw that may wait
// for the worker: an identity projection is how Render2D draws the interface.
static void configure_perspective_pass_through(DX11BackendClass & backend)
{
	configure_unlit_pass_through(backend);
	float perspective[16];
	set_identity(perspective);
	perspective[11] = 1.0f;
	perspective[15] = 0.0f;
	backend.Set_Transform(D3DTS_PROJECTION, perspective);
}

// With the compile off the frame, a state neither cache holds leaves its draws out until the
// worker has its two programs: the second draw of the same state queues nothing more, and the
// draw after they land is made from them with no compile of its own.
TEST(dx11backend_a_missing_program_compiles_off_the_frame)
{
	DX11DeviceClass device;
	CHECK(device.Create_Offscreen());

	DX11BackendClass backend;
	CHECK(backend.Initialise(&device));
	backend.Set_Compile_Off_Frame(true);

	ID3D11Buffer * vertices = NULL;
	CHECK(DX11Resource_Create_Vertex_Buffer(device.Get_Device(), sizeof(QUAD_VERTICES),
		D3DPOOL_MANAGED, 0, QUAD_VERTICES, &vertices));

	ID3D11Buffer * indices = NULL;
	CHECK(DX11Resource_Create_Index_Buffer(device.Get_Device(), sizeof(QUAD_INDICES),
		D3DPOOL_MANAGED, 0, QUAD_INDICES, &indices));

	backend.Set_Viewport(0, 0, TARGET_SIZE, TARGET_SIZE);
	configure_perspective_pass_through(backend);
	backend.Set_Stream_Source(vertices, sizeof(BackendVertex), 0);
	backend.Set_Indices(indices, DXGI_FORMAT_R16_UINT);

	CHECK(!backend.Draw_Indexed_Triangles(6, 0, 0));
	CHECK(!backend.Draw_Indexed_Triangles(6, 0, 0));

	// FXC takes about a tenth of a second for these two; twenty seconds is a hung worker.
	const unsigned WAIT_STEPS = 2000;
	bool drawn = false;
	for (unsigned step = 0; step < WAIT_STEPS && !drawn; ++step) {
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
		drawn = backend.Draw_Indexed_Triangles(6, 0, 0);
	}
	CHECK(drawn);

	unsigned off_frame = 0;
	double longest = 0.0;
	unsigned long long waited = 0;
	backend.Compile_Statistics(off_frame, longest, waited);
	CHECK_EQ(off_frame, 2u);
	CHECK(waited >= 2ull);
	CHECK(longest > 0.0);

	unsigned pipelines = 0;
	unsigned long long made = 0;
	unsigned long long refused = 0;
	backend.Statistics(pipelines, made, refused);
	CHECK_EQ(pipelines, 1u);
	CHECK_EQ(made, 1ull);

	indices->Release();
	vertices->Release();
	backend.Shutdown();
}

// A key still waiting on the worker is drawn at once by a draw that may not wait: the same state
// under Render2D's identity projection compiles on the spot rather than leaving the interface out.
TEST(dx11backend_a_draw_that_may_not_wait_compiles_a_pending_key_on_the_spot)
{
	DX11DeviceClass device;
	CHECK(device.Create_Offscreen());

	DX11BackendClass backend;
	CHECK(backend.Initialise(&device));
	backend.Set_Compile_Off_Frame(true);

	ID3D11Buffer * vertices = NULL;
	CHECK(DX11Resource_Create_Vertex_Buffer(device.Get_Device(), sizeof(QUAD_VERTICES),
		D3DPOOL_MANAGED, 0, QUAD_VERTICES, &vertices));

	ID3D11Buffer * indices = NULL;
	CHECK(DX11Resource_Create_Index_Buffer(device.Get_Device(), sizeof(QUAD_INDICES),
		D3DPOOL_MANAGED, 0, QUAD_INDICES, &indices));

	backend.Set_Viewport(0, 0, TARGET_SIZE, TARGET_SIZE);
	configure_perspective_pass_through(backend);
	backend.Set_Stream_Source(vertices, sizeof(BackendVertex), 0);
	backend.Set_Indices(indices, DXGI_FORMAT_R16_UINT);

	CHECK(!backend.Draw_Indexed_Triangles(6, 0, 0));
	float identity[16];
	set_identity(identity);
	backend.Set_Transform(D3DTS_PROJECTION, identity);
	CHECK(backend.Draw_Indexed_Triangles(6, 0, 0));

	unsigned pipelines = 0;
	unsigned long long made = 0;
	unsigned long long refused = 0;
	backend.Statistics(pipelines, made, refused);
	CHECK_EQ(pipelines, 1u);
	CHECK_EQ(made, 1ull);

	indices->Release();
	vertices->Release();
	backend.Shutdown();
}

// The vertex block goes into a ring of 4096 one-kilobyte entries, so 5000 draws with a world matrix
// each wrap it once, and the draw after them reads an entry written NO_OVERWRITE past the discard.
// Every one of the 5000 puts the quad off the target; the last puts it back over the whole of it.
// A wrap that bound the wrong entry, or wrote into the allocation the discard threw away, draws
// that last quad off the target too and the centre stays the clear colour.  Then the bindings are
// forgotten and the same draw is made again with nothing changed: no block is written, and only a
// rebind at the entry the last write went to puts the quad back.
TEST(dx11backend_a_wrapped_constant_ring_draws_with_the_latest_block)
{
	const unsigned OFF_TARGET_DRAWS = 5000;

	DX11DeviceClass device;
	device.Request_Debug_Layer();
	CHECK(device.Create_Offscreen());
	ID3D11Device * d3d = device.Get_Device();

	DX11BackendClass backend;
	CHECK(backend.Initialise(&device));

	D3D11_TEXTURE2D_DESC target_description;
	memset(&target_description, 0, sizeof(target_description));
	target_description.Width = TARGET_SIZE;
	target_description.Height = TARGET_SIZE;
	target_description.MipLevels = 1;
	target_description.ArraySize = 1;
	target_description.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
	target_description.SampleDesc.Count = 1;
	target_description.Usage = D3D11_USAGE_DEFAULT;
	target_description.BindFlags = D3D11_BIND_RENDER_TARGET;

	ID3D11Texture2D * target = NULL;
	CHECK(SUCCEEDED(d3d->CreateTexture2D(&target_description, NULL, &target)));

	D3D11_TEXTURE2D_DESC staging_description = target_description;
	staging_description.Usage = D3D11_USAGE_STAGING;
	staging_description.BindFlags = 0;
	staging_description.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

	ID3D11Texture2D * staging = NULL;
	CHECK(SUCCEEDED(d3d->CreateTexture2D(&staging_description, NULL, &staging)));

	ID3D11RenderTargetView * target_view = NULL;
	CHECK(SUCCEEDED(d3d->CreateRenderTargetView(target, NULL, &target_view)));

	const float clear_colour[4] = { 0.0f, 1.0f, 0.0f, 1.0f };
	device.Get_Context()->OMSetRenderTargets(1, &target_view, NULL);

	backend.Set_Viewport(0, 0, TARGET_SIZE, TARGET_SIZE);
	configure_unlit_pass_through(backend);

	ID3D11Buffer * vertices = NULL;
	CHECK(DX11Resource_Create_Vertex_Buffer(d3d, sizeof(QUAD_VERTICES), D3DPOOL_MANAGED, 0,
		QUAD_VERTICES, &vertices));

	ID3D11Buffer * indices = NULL;
	CHECK(DX11Resource_Create_Index_Buffer(d3d, sizeof(QUAD_INDICES), D3DPOOL_MANAGED, 0,
		QUAD_INDICES, &indices));

	backend.Set_Stream_Source(vertices, sizeof(BackendVertex), 0);
	backend.Set_Indices(indices, DXGI_FORMAT_R16_UINT);

	// Clip x from 2 up, a hair further each draw so every block differs from the one before.
	float world[16];
	for (unsigned draw = 0; draw < OFF_TARGET_DRAWS; ++draw) {
		set_identity(world);
		world[12] = 3.0f + 0.0001f * static_cast<float>(draw);
		backend.Set_Transform(D3DTS_WORLD, world);
		CHECK(backend.Draw_Indexed_Triangles(6, 0, 0));
	}

	for (unsigned pass = 0; pass < 2; ++pass) {
		device.Get_Context()->ClearRenderTargetView(target_view, clear_colour);
		if (pass == 0) {
			set_identity(world);
			backend.Set_Transform(D3DTS_WORLD, world);
		} else {
			backend.Forget_Bindings();
		}
		CHECK(backend.Draw_Indexed_Triangles(6, 0, 0));

		device.Get_Context()->CopyResource(staging, target);
		D3D11_MAPPED_SUBRESOURCE mapped;
		CHECK(SUCCEEDED(device.Get_Context()->Map(staging, 0, D3D11_MAP_READ, 0, &mapped)));
		const unsigned char * centre = static_cast<const unsigned char *>(mapped.pData)
			+ mapped.RowPitch * (TARGET_SIZE / 2) + (TARGET_SIZE / 2) * 4;
		if (centre[0] != EXPECTED_BLUE) {
			printf("  pass %u centre pixel bgra %02x %02x %02x %02x\n",
				pass, centre[0], centre[1], centre[2], centre[3]);
		}
		CHECK_EQ(centre[0], EXPECTED_BLUE);
		CHECK_EQ(centre[1], EXPECTED_GREEN);
		CHECK_EQ(centre[2], EXPECTED_RED);
		device.Get_Context()->Unmap(staging, 0);
	}

	// One vertex block a draw before the second pass, which writes none, and one pixel block.  A
	// ring renames on its first write and on each wrap: the vertex ring twice, the pixel ring once.
	// Without offsets every write is a rename.
	unsigned writes = 0;
	unsigned renames = 0;
	backend.Take_Frame_Constant_Writes(writes, renames);
	CHECK_EQ(writes, OFF_TARGET_DRAWS + 2);
	if (backend.Constant_Offsets()) {
		CHECK_EQ(renames, 3u);
	} else {
		CHECK_EQ(renames, writes);
	}

	indices->Release();
	vertices->Release();
	target_view->Release();
	staging->Release();
	target->Release();
	backend.Shutdown();
}

// The sorted runs flip between a few pixel blocks, so a block still in the ring is bound again at
// the entry it was written to rather than written again (Rebind_Recent_Pixel_Constants).  Two texture
// factors alternate with two fresh ones after each pair, which takes both out of the four the
// backend remembers every other round, and the rounds go on until the ring has wrapped more than
// once.  A block bound again from before a wrap, where later writes have gone since, draws another
// colour.
TEST(dx11backend_a_pixel_block_bound_again_draws_its_own_colour_across_a_ring_wrap)
{
	const unsigned ROUNDS = 1500;
	const DWORD FACTORS[2] = { 0xff112233, 0xff445566 };
	const DWORD FRESH_FACTOR = 0xff800000;

	DX11DeviceClass device;
	CHECK(device.Create_Offscreen());
	ID3D11Device * d3d = device.Get_Device();

	DX11BackendClass backend;
	CHECK(backend.Initialise(&device));

	D3D11_TEXTURE2D_DESC target_description;
	memset(&target_description, 0, sizeof(target_description));
	target_description.Width = TARGET_SIZE;
	target_description.Height = TARGET_SIZE;
	target_description.MipLevels = 1;
	target_description.ArraySize = 1;
	target_description.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
	target_description.SampleDesc.Count = 1;
	target_description.Usage = D3D11_USAGE_DEFAULT;
	target_description.BindFlags = D3D11_BIND_RENDER_TARGET;

	ID3D11Texture2D * target = NULL;
	CHECK(SUCCEEDED(d3d->CreateTexture2D(&target_description, NULL, &target)));

	D3D11_TEXTURE2D_DESC staging_description = target_description;
	staging_description.Usage = D3D11_USAGE_STAGING;
	staging_description.BindFlags = 0;
	staging_description.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

	ID3D11Texture2D * staging = NULL;
	CHECK(SUCCEEDED(d3d->CreateTexture2D(&staging_description, NULL, &staging)));

	ID3D11RenderTargetView * target_view = NULL;
	CHECK(SUCCEEDED(d3d->CreateRenderTargetView(target, NULL, &target_view)));
	device.Get_Context()->OMSetRenderTargets(1, &target_view, NULL);

	backend.Set_Viewport(0, 0, TARGET_SIZE, TARGET_SIZE);
	configure_unlit_pass_through(backend);
	backend.Set_Texture_Stage_State(0, D3DTSS_COLORARG1, D3DTA_TFACTOR);
	backend.Set_Texture_Stage_State(0, D3DTSS_ALPHAARG1, D3DTA_TFACTOR);

	ID3D11Buffer * vertices = NULL;
	CHECK(DX11Resource_Create_Vertex_Buffer(d3d, sizeof(QUAD_VERTICES), D3DPOOL_MANAGED, 0,
		QUAD_VERTICES, &vertices));
	ID3D11Buffer * indices = NULL;
	CHECK(DX11Resource_Create_Index_Buffer(d3d, sizeof(QUAD_INDICES), D3DPOOL_MANAGED, 0,
		QUAD_INDICES, &indices));
	backend.Set_Stream_Source(vertices, sizeof(BackendVertex), 0);
	backend.Set_Indices(indices, DXGI_FORMAT_R16_UINT);

	unsigned wrong = 0;
	for (unsigned round = 0; round < ROUNDS; ++round) {
		for (unsigned pick = 0; pick < 2; ++pick) {
			backend.Set_Render_State(D3DRS_TEXTUREFACTOR, FACTORS[pick]);
			CHECK(backend.Draw_Indexed_Triangles(6, 0, 0));

			device.Get_Context()->CopyResource(staging, target);
			D3D11_MAPPED_SUBRESOURCE mapped;
			CHECK(SUCCEEDED(device.Get_Context()->Map(staging, 0, D3D11_MAP_READ, 0, &mapped)));
			const unsigned char * centre = static_cast<const unsigned char *>(mapped.pData)
				+ mapped.RowPitch * (TARGET_SIZE / 2) + (TARGET_SIZE / 2) * 4;
			const DWORD drawn = (static_cast<DWORD>(centre[3]) << 24) | (centre[2] << 16)
				| (centre[1] << 8) | centre[0];
			if (drawn != FACTORS[pick]) {
				if (wrong == 0) {
					printf("  round %u factor %08x drew %08x\n", round, FACTORS[pick], drawn);
				}
				++wrong;
			}
			device.Get_Context()->Unmap(staging, 0);
		}
		for (unsigned fresh = 0; fresh < 2; ++fresh) {
			backend.Set_Render_State(D3DRS_TEXTUREFACTOR, FRESH_FACTOR | (round * 2 + fresh));
			CHECK(backend.Draw_Indexed_Triangles(6, 0, 0));
		}
	}
	CHECK_EQ(wrong, 0u);

	// Fewer pixel blocks written than drawn, so the two were bound again, and the vertex ring's
	// first write and the pixel ring's first write and two wraps at least.
	unsigned writes = 0;
	unsigned renames = 0;
	backend.Take_Frame_Constant_Writes(writes, renames);
	printf("  %u constant writes, %u renames over %u draws\n", writes, renames, ROUNDS * 4);
	if (backend.Constant_Offsets()) {
		CHECK(writes < ROUNDS * 4);
		CHECK(renames >= 4u);
	}

	indices->Release();
	vertices->Release();
	target_view->Release();
	staging->Release();
	target->Release();
	backend.Shutdown();
}

// A pass multiplying what is under it goes up without the blast lights: the pass under it took
// them, and the multiply would light the surface twice.  A blend change raises no constant flag of
// its own, so the pixel block is built again only when Pixel_Blend_Key moves, and the multiply test
// is part of it.  Read through the block the draw went up with: the multiply passes that read the
// lights are the terrain's programs, and a generated multiply program samples none.
TEST(dx11backend_a_multiply_pass_goes_up_without_the_blast_lights)
{
	DX11DeviceClass device;
	CHECK(device.Create_Offscreen());
	ID3D11Device * d3d = device.Get_Device();

	DX11BackendClass backend;
	CHECK(backend.Initialise(&device));

	D3D11_TEXTURE2D_DESC target_description;
	memset(&target_description, 0, sizeof(target_description));
	target_description.Width = TARGET_SIZE;
	target_description.Height = TARGET_SIZE;
	target_description.MipLevels = 1;
	target_description.ArraySize = 1;
	target_description.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
	target_description.SampleDesc.Count = 1;
	target_description.Usage = D3D11_USAGE_DEFAULT;
	target_description.BindFlags = D3D11_BIND_RENDER_TARGET;

	ID3D11Texture2D * target = NULL;
	CHECK(SUCCEEDED(d3d->CreateTexture2D(&target_description, NULL, &target)));
	ID3D11RenderTargetView * target_view = NULL;
	CHECK(SUCCEEDED(d3d->CreateRenderTargetView(target, NULL, &target_view)));

	configure_unlit_pass_through(backend);
	CHECK(backend.Begin_Shadow_Map(64));
	backend.End_Shadow_Map();
	backend.Set_Shadow_Parameters(0.001f, 0.5f, 2.0f, 1.0f, 1.0f, 1.0f, 0.0f);
	const float blast[BLAST_LIGHT_FLOATS] = {
		0.0f, 0.0f, 0.5f, 10.0f,  1.0f, 0.5f, 0.25f, 1.0f,  0.1f, 0.1f, 0.1f, 0.0f };
	backend.Set_Blast_Lights(blast, 1);

	device.Get_Context()->OMSetRenderTargets(1, &target_view, NULL);
	backend.Set_Viewport(0, 0, TARGET_SIZE, TARGET_SIZE);

	ID3D11Buffer * vertices = NULL;
	CHECK(DX11Resource_Create_Vertex_Buffer(d3d, sizeof(QUAD_VERTICES), D3DPOOL_MANAGED, 0,
		QUAD_VERTICES, &vertices));
	ID3D11Buffer * indices = NULL;
	CHECK(DX11Resource_Create_Index_Buffer(d3d, sizeof(QUAD_INDICES), D3DPOOL_MANAGED, 0,
		QUAD_INDICES, &indices));
	backend.Set_Stream_Source(vertices, sizeof(BackendVertex), 0);
	backend.Set_Indices(indices, DXGI_FORMAT_R16_UINT);

	backend.Set_Render_State(D3DRS_ALPHABLENDENABLE, TRUE);
	for (unsigned pass = 0; pass < 3; ++pass) {
		const bool multiply = pass == 1;
		backend.Set_Render_State(D3DRS_SRCBLEND, multiply ? D3DBLEND_ZERO : D3DBLEND_SRCALPHA);
		backend.Set_Render_State(D3DRS_DESTBLEND, multiply ? D3DBLEND_SRCCOLOR : D3DBLEND_INVSRCALPHA);
		CHECK(backend.Draw_Indexed_Triangles(6, 0, 0));
		CHECK_EQ(backend.Pixel_Block_Blast_Lights(), multiply ? 0u : 1u);
	}

	indices->Release();
	vertices->Release();
	target_view->Release();
	target->Release();
	backend.Shutdown();
}

// The sorted smoke billboards carry a fire's glow in their normals, and the backend adds it to what
// the stages made.  The quad's normal is (0, 0, 1), so with the glow on the blue channel goes to full
// and the other two keep the vertex colour; the glow is its own pipeline and turning it off goes
// back to the first one without building a third.
TEST(dx11backend_the_smoke_glow_is_read_from_the_normal)
{
	DX11DeviceClass device;
	CHECK(device.Create_Offscreen());
	ID3D11Device * d3d = device.Get_Device();

	DX11BackendClass backend;
	CHECK(backend.Initialise(&device));

	D3D11_TEXTURE2D_DESC target_description;
	memset(&target_description, 0, sizeof(target_description));
	target_description.Width = TARGET_SIZE;
	target_description.Height = TARGET_SIZE;
	target_description.MipLevels = 1;
	target_description.ArraySize = 1;
	target_description.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
	target_description.SampleDesc.Count = 1;
	target_description.Usage = D3D11_USAGE_DEFAULT;
	target_description.BindFlags = D3D11_BIND_RENDER_TARGET;

	ID3D11Texture2D * target = NULL;
	CHECK(SUCCEEDED(d3d->CreateTexture2D(&target_description, NULL, &target)));

	D3D11_TEXTURE2D_DESC staging_description = target_description;
	staging_description.Usage = D3D11_USAGE_STAGING;
	staging_description.BindFlags = 0;
	staging_description.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

	ID3D11Texture2D * staging = NULL;
	CHECK(SUCCEEDED(d3d->CreateTexture2D(&staging_description, NULL, &staging)));

	ID3D11RenderTargetView * target_view = NULL;
	CHECK(SUCCEEDED(d3d->CreateRenderTargetView(target, NULL, &target_view)));
	device.Get_Context()->OMSetRenderTargets(1, &target_view, NULL);

	backend.Set_Viewport(0, 0, TARGET_SIZE, TARGET_SIZE);
	configure_unlit_pass_through(backend);

	ID3D11Buffer * vertices = NULL;
	CHECK(DX11Resource_Create_Vertex_Buffer(d3d, sizeof(QUAD_VERTICES), D3DPOOL_MANAGED, 0,
		QUAD_VERTICES, &vertices));
	ID3D11Buffer * indices = NULL;
	CHECK(DX11Resource_Create_Index_Buffer(d3d, sizeof(QUAD_INDICES), D3DPOOL_MANAGED, 0,
		QUAD_INDICES, &indices));
	backend.Set_Stream_Source(vertices, sizeof(BackendVertex), 0);
	backend.Set_Indices(indices, DXGI_FORMAT_R16_UINT);

	CHECK(backend.Draw_Indexed_Triangles(6, 0, 0));
	backend.Set_Smoke_Glow(true);
	CHECK(backend.Draw_Indexed_Triangles(6, 0, 0));

	device.Get_Context()->CopyResource(staging, target);
	D3D11_MAPPED_SUBRESOURCE mapped;
	CHECK(SUCCEEDED(device.Get_Context()->Map(staging, 0, D3D11_MAP_READ, 0, &mapped)));
	const unsigned char * centre = static_cast<const unsigned char *>(mapped.pData)
		+ mapped.RowPitch * (TARGET_SIZE / 2) + (TARGET_SIZE / 2) * 4;
	CHECK_EQ(centre[0], 0xff);
	CHECK_EQ(centre[1], EXPECTED_GREEN);
	CHECK_EQ(centre[2], EXPECTED_RED);
	device.Get_Context()->Unmap(staging, 0);

	unsigned pipelines = 0;
	unsigned long long made = 0;
	unsigned long long refused = 0;
	backend.Statistics(pipelines, made, refused);
	CHECK_EQ(pipelines, 2u);

	backend.Set_Smoke_Glow(false);
	CHECK(backend.Draw_Indexed_Triangles(6, 0, 0));
	backend.Statistics(pipelines, made, refused);
	CHECK_EQ(pipelines, 2u);
	CHECK_EQ(made, 3ull);
	CHECK_EQ(refused, 0ull);

	indices->Release();
	vertices->Release();
	target_view->Release();
	staging->Release();
	target->Release();
	backend.Shutdown();
}

static float half_to_float(unsigned short half)
{
	const int exponent = (half >> 10) & 0x1f;
	const float mantissa = static_cast<float>(half & 0x3ff) / 1024.0f;
	const float value = (exponent == 0) ? mantissa / 16384.0f
		: (1.0f + mantissa) * static_cast<float>(1 << exponent) / 32768.0f;
	return (half & 0x8000) ? -value : value;
}

// The Glow option's gain: an additive draw into the half float scene lands at its colour times the
// gain, past white where the colour times the gain is, and every other blend lands as it was.  The
// blue channel is the one that tells: 0.2 times four on a 0.25 clear is 1.05, which a factor
// clamped to one (as an eight bit target would clamp it) cannot reach.
TEST(dx11backend_an_additive_draw_takes_the_glow_gain_past_white)
{
	DX11DeviceClass device;
	CHECK(device.Create_Offscreen());
	ID3D11Device * d3d = device.Get_Device();

	DX11BackendClass backend;
	CHECK(backend.Initialise(&device));

	D3D11_TEXTURE2D_DESC target_description;
	memset(&target_description, 0, sizeof(target_description));
	target_description.Width = TARGET_SIZE;
	target_description.Height = TARGET_SIZE;
	target_description.MipLevels = 1;
	target_description.ArraySize = 1;
	target_description.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
	target_description.SampleDesc.Count = 1;
	target_description.Usage = D3D11_USAGE_DEFAULT;
	target_description.BindFlags = D3D11_BIND_RENDER_TARGET;

	ID3D11Texture2D * target = NULL;
	CHECK(SUCCEEDED(d3d->CreateTexture2D(&target_description, NULL, &target)));
	D3D11_TEXTURE2D_DESC staging_description = target_description;
	staging_description.Usage = D3D11_USAGE_STAGING;
	staging_description.BindFlags = 0;
	staging_description.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
	ID3D11Texture2D * staging = NULL;
	CHECK(SUCCEEDED(d3d->CreateTexture2D(&staging_description, NULL, &staging)));
	ID3D11RenderTargetView * target_view = NULL;
	CHECK(SUCCEEDED(d3d->CreateRenderTargetView(target, NULL, &target_view)));
	device.Get_Context()->OMSetRenderTargets(1, &target_view, NULL);

	backend.Set_Viewport(0, 0, TARGET_SIZE, TARGET_SIZE);
	configure_unlit_pass_through(backend);
	ID3D11Buffer * vertices = NULL;
	CHECK(DX11Resource_Create_Vertex_Buffer(d3d, sizeof(QUAD_VERTICES), D3DPOOL_MANAGED, 0,
		QUAD_VERTICES, &vertices));
	ID3D11Buffer * indices = NULL;
	CHECK(DX11Resource_Create_Index_Buffer(d3d, sizeof(QUAD_INDICES), D3DPOOL_MANAGED, 0,
		QUAD_INDICES, &indices));
	backend.Set_Stream_Source(vertices, sizeof(BackendVertex), 0);
	backend.Set_Indices(indices, DXGI_FORMAT_R16_UINT);
	backend.Set_Render_State(D3DRS_ALPHABLENDENABLE, TRUE);

	// Blue of the centre pixel after one draw over a 0.25 clear, at this gain, this blend and with
	// or without the draw's opt-in (ShaderClass::GLOW_ENABLE).
	struct Case { float Gain; bool Glow; DWORD Source; DWORD Destination; float Blue; };
	const Case cases[] = {
		{ 4.0f, true, D3DBLEND_ONE, D3DBLEND_ONE, 0.25f + 0.2f * 4.0f },
		{ 1.0f, true, D3DBLEND_ONE, D3DBLEND_ONE, 0.25f + 0.2f },
		{ 4.0f, true, D3DBLEND_SRCALPHA, D3DBLEND_INVSRCALPHA, 0.2f },
		{ 4.0f, false, D3DBLEND_ONE, D3DBLEND_ONE, 0.25f + 0.2f },
	};
	for (unsigned index = 0; index < sizeof(cases) / sizeof(cases[0]); ++index) {
		const float clear_colour[4] = { 0.25f, 0.25f, 0.25f, 1.0f };
		device.Get_Context()->ClearRenderTargetView(target_view, clear_colour);
		backend.Set_Glow_Draw(cases[index].Glow);
		backend.Set_Additive_Gain(cases[index].Gain);
		backend.Set_Render_State(D3DRS_SRCBLEND, cases[index].Source);
		backend.Set_Render_State(D3DRS_DESTBLEND, cases[index].Destination);
		CHECK(backend.Draw_Indexed_Triangles(6, 0, 0));

		device.Get_Context()->CopyResource(staging, target);
		D3D11_MAPPED_SUBRESOURCE mapped;
		CHECK(SUCCEEDED(device.Get_Context()->Map(staging, 0, D3D11_MAP_READ, 0, &mapped)));
		const unsigned short * centre = reinterpret_cast<const unsigned short *>(
			static_cast<const unsigned char *>(mapped.pData) + mapped.RowPitch * (TARGET_SIZE / 2))
			+ (TARGET_SIZE / 2) * 4;
		const float blue = half_to_float(centre[2]);
		if (blue < cases[index].Blue - 0.01f || blue > cases[index].Blue + 0.01f) {
			printf("  case %u: blue %.4f, wanted %.4f\n", index, blue, cases[index].Blue);
		}
		CHECK_NEAR(blue, cases[index].Blue, 0.01f);
		device.Get_Context()->Unmap(staging, 0);
	}

	indices->Release();
	vertices->Release();
	target_view->Release();
	staging->Release();
	target->Release();
	backend.Shutdown();
}

static const char * const CACHE_FILE_NAME = "dx11backend_test_shaders.cache";
static const long LAST_RECORD_CUT_BYTES = 4;

// A pipeline compiled in one run is not compiled again in the next: the backend writes what it
// compiled when it shuts down, and a second one reading the file holds both halves of the program
// before it has drawn anything.  A file cut short while it was written gives up its last record and
// keeps the rest, which is what a crash during the write leaves behind.
TEST(dx11backend_compiled_programs_are_kept_in_the_cache_file)
{
	char path[MAX_PATH];
	const DWORD directory_length = GetTempPathA(MAX_PATH, path);
	CHECK(directory_length > 0 && directory_length + strlen(CACHE_FILE_NAME) < MAX_PATH);
	strcat(path, CACHE_FILE_NAME);
	DeleteFileA(path);

	DX11DeviceClass device;
	CHECK(device.Create_Offscreen());

	DX11BackendClass writer;
	CHECK(writer.Initialise(&device));
	writer.Set_Shader_Cache_Path(path);
	CHECK_EQ(writer.Compiled_Program_Count(), 0u);

	ID3D11Buffer * vertices = NULL;
	CHECK(DX11Resource_Create_Vertex_Buffer(device.Get_Device(), sizeof(QUAD_VERTICES),
		D3DPOOL_MANAGED, 0, QUAD_VERTICES, &vertices));
	ID3D11Buffer * indices = NULL;
	CHECK(DX11Resource_Create_Index_Buffer(device.Get_Device(), sizeof(QUAD_INDICES),
		D3DPOOL_MANAGED, 0, QUAD_INDICES, &indices));

	writer.Set_Viewport(0, 0, TARGET_SIZE, TARGET_SIZE);
	configure_unlit_pass_through(writer);
	writer.Set_Stream_Source(vertices, sizeof(BackendVertex), 0);
	writer.Set_Indices(indices, DXGI_FORMAT_R16_UINT);
	CHECK(writer.Draw_Indexed_Triangles(6, 0, 0));
	CHECK_EQ(writer.Compiled_Program_Count(), 2u);

	indices->Release();
	vertices->Release();
	writer.Shutdown();

	DX11BackendClass reader;
	CHECK(reader.Initialise(&device));
	reader.Set_Shader_Cache_Path(path);
	CHECK_EQ(reader.Compiled_Program_Count(), 2u);
	reader.Shutdown();

	std::vector<unsigned char> bytes;
	FILE * file = fopen(path, "rb");
	CHECK(file != NULL);
	if (file != NULL) {
		fseek(file, 0, SEEK_END);
		bytes.resize(static_cast<size_t>(ftell(file)));
		fseek(file, 0, SEEK_SET);
		CHECK(fread(&bytes[0], bytes.size(), 1, file) == 1);
		fclose(file);
	}
	CHECK(bytes.size() > static_cast<size_t>(LAST_RECORD_CUT_BYTES));

	file = fopen(path, "wb");
	CHECK(file != NULL);
	if (file != NULL) {
		fwrite(&bytes[0], bytes.size() - LAST_RECORD_CUT_BYTES, 1, file);
		fclose(file);
	}

	DX11BackendClass cut_short;
	CHECK(cut_short.Initialise(&device));
	cut_short.Set_Shader_Cache_Path(path);
	CHECK_EQ(cut_short.Compiled_Program_Count(), 1u);
	cut_short.Shutdown();

	DeleteFileA(path);
}

// The alpha test, which on D3D11 is a clip inside the generated pixel shader rather than anything
// the pipeline does.  The vertices carry an alpha of 0xff, so a reference above it cuts the whole
// quad away and the target keeps the colour it was cleared to.  Get the comparison backwards and
// this passes at every reference, which is why the test is run at both ends.
TEST(dx11backend_the_alpha_test_cuts_the_pixels_the_comparison_rejects)
{
	DX11DeviceClass device;
	device.Request_Debug_Layer();
	CHECK(device.Create_Offscreen());
	ID3D11Device * d3d = device.Get_Device();

	DX11BackendClass backend;
	CHECK(backend.Initialise(&device));

	D3D11_TEXTURE2D_DESC target_description;
	memset(&target_description, 0, sizeof(target_description));
	target_description.Width = TARGET_SIZE;
	target_description.Height = TARGET_SIZE;
	target_description.MipLevels = 1;
	target_description.ArraySize = 1;
	target_description.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
	target_description.SampleDesc.Count = 1;
	target_description.Usage = D3D11_USAGE_DEFAULT;
	target_description.BindFlags = D3D11_BIND_RENDER_TARGET;

	ID3D11Texture2D * target = NULL;
	CHECK(SUCCEEDED(d3d->CreateTexture2D(&target_description, NULL, &target)));

	D3D11_TEXTURE2D_DESC staging_description = target_description;
	staging_description.Usage = D3D11_USAGE_STAGING;
	staging_description.BindFlags = 0;
	staging_description.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

	ID3D11Texture2D * staging = NULL;
	CHECK(SUCCEEDED(d3d->CreateTexture2D(&staging_description, NULL, &staging)));

	ID3D11RenderTargetView * target_view = NULL;
	CHECK(SUCCEEDED(d3d->CreateRenderTargetView(target, NULL, &target_view)));

	ID3D11Buffer * vertices = NULL;
	CHECK(DX11Resource_Create_Vertex_Buffer(d3d, sizeof(QUAD_VERTICES), D3DPOOL_MANAGED, 0,
		QUAD_VERTICES, &vertices));

	ID3D11Buffer * indices = NULL;
	CHECK(DX11Resource_Create_Index_Buffer(d3d, sizeof(QUAD_INDICES), D3DPOOL_MANAGED, 0,
		QUAD_INDICES, &indices));

	backend.Set_Viewport(0, 0, TARGET_SIZE, TARGET_SIZE);
	configure_unlit_pass_through(backend);
	backend.Set_Stream_Source(vertices, sizeof(BackendVertex), 0);
	backend.Set_Indices(indices, DXGI_FORMAT_R16_UINT);

	// The vertices carry an opaque alpha, so half is a reference it is above.  The two comparisons
	// below therefore disagree about every pixel in the quad, which is what makes this a pair
	// rather than one measurement: a clip written the wrong way round passes one of them.
	backend.Set_Render_State(D3DRS_ALPHATESTENABLE, TRUE);
	backend.Set_Render_State(D3DRS_ALPHAREF, 0x80);

	backend.Set_Render_State(D3DRS_ALPHAFUNC, D3DCMP_LESS);

	const float clear_colour[4] = { 0.0f, 1.0f, 0.0f, 1.0f };
	device.Get_Context()->ClearRenderTargetView(target_view, clear_colour);
	device.Get_Context()->OMSetRenderTargets(1, &target_view, NULL);
	CHECK(backend.Draw_Indexed_Triangles(6, 0, 0));

	device.Get_Context()->CopyResource(staging, target);
	D3D11_MAPPED_SUBRESOURCE mapped;
	CHECK(SUCCEEDED(device.Get_Context()->Map(staging, 0, D3D11_MAP_READ, 0, &mapped)));
	const unsigned char * cut = static_cast<const unsigned char *>(mapped.pData)
		+ mapped.RowPitch * (TARGET_SIZE / 2) + (TARGET_SIZE / 2) * 4;
	CHECK_EQ(cut[1], 0xff);
	CHECK_EQ(cut[0], 0x00);
	device.Get_Context()->Unmap(staging, 0);

	// The same reference, the opposite comparison: now every pixel passes.
	device.Get_Context()->ClearRenderTargetView(target_view, clear_colour);
	backend.Set_Render_State(D3DRS_ALPHAFUNC, D3DCMP_GREATEREQUAL);
	CHECK(backend.Draw_Indexed_Triangles(6, 0, 0));

	device.Get_Context()->CopyResource(staging, target);
	CHECK(SUCCEEDED(device.Get_Context()->Map(staging, 0, D3D11_MAP_READ, 0, &mapped)));
	const unsigned char * kept = static_cast<const unsigned char *>(mapped.pData)
		+ mapped.RowPitch * (TARGET_SIZE / 2) + (TARGET_SIZE / 2) * 4;
	CHECK_EQ(kept[0], EXPECTED_BLUE);
	CHECK_EQ(kept[2], EXPECTED_RED);
	device.Get_Context()->Unmap(staging, 0);

	// Only the world moves now, the quad off the target to the right.  Nothing a pipeline is built
	// from changed, so this is the draw that skips the descriptions and the constants both unless
	// Set_Transform says otherwise, and skipped wrongly it paints the quad where it was.
	float moved[16];
	set_identity(moved);
	moved[12] = 4.0f;
	device.Get_Context()->ClearRenderTargetView(target_view, clear_colour);
	backend.Set_Transform(D3DTS_WORLD, moved);
	CHECK(backend.Draw_Indexed_Triangles(6, 0, 0));

	device.Get_Context()->CopyResource(staging, target);
	CHECK(SUCCEEDED(device.Get_Context()->Map(staging, 0, D3D11_MAP_READ, 0, &mapped)));
	const unsigned char * gone = static_cast<const unsigned char *>(mapped.pData)
		+ mapped.RowPitch * (TARGET_SIZE / 2) + (TARGET_SIZE / 2) * 4;
	CHECK_EQ(gone[1], 0xff);
	CHECK_EQ(gone[0], 0x00);
	device.Get_Context()->Unmap(staging, 0);

	// The comparison is part of the program and the reference is not, so two comparisons are two
	// pipelines and the reference could have changed between them for free.
	unsigned pipelines = 0;
	unsigned long long made = 0;
	unsigned long long refused = 0;
	backend.Statistics(pipelines, made, refused);
	CHECK_EQ(pipelines, 2u);
	CHECK_EQ(made, 3ull);

	indices->Release();
	vertices->Release();
	target_view->Release();
	staging->Release();
	target->Release();
	backend.Shutdown();
}

// Heat haze samples the scene while the scene is still being drawn into that texture.  The target is
// cleared white and the quad multiplies what it samples by the vertex colour, so reading the scene
// gives the vertex colour, the sample Direct3D 11 unbinds gives black, and a refused draw leaves white.
TEST(dx11backend_a_draw_that_samples_its_own_target_reads_what_was_drawn)
{
	DX11DeviceClass device;
	device.Request_Debug_Layer();
	CHECK(device.Create_Offscreen());
	ID3D11Device * d3d = device.Get_Device();

	DX11BackendClass backend;
	CHECK(backend.Initialise(&device));

	D3D11_TEXTURE2D_DESC target_description;
	memset(&target_description, 0, sizeof(target_description));
	target_description.Width = TARGET_SIZE;
	target_description.Height = TARGET_SIZE;
	target_description.MipLevels = 1;
	target_description.ArraySize = 1;
	target_description.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
	target_description.SampleDesc.Count = 1;
	target_description.Usage = D3D11_USAGE_DEFAULT;
	target_description.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

	ID3D11Texture2D * target = NULL;
	CHECK(SUCCEEDED(d3d->CreateTexture2D(&target_description, NULL, &target)));

	D3D11_TEXTURE2D_DESC staging_description = target_description;
	staging_description.Usage = D3D11_USAGE_STAGING;
	staging_description.BindFlags = 0;
	staging_description.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

	ID3D11Texture2D * staging = NULL;
	CHECK(SUCCEEDED(d3d->CreateTexture2D(&staging_description, NULL, &staging)));

	ID3D11RenderTargetView * target_view = NULL;
	CHECK(SUCCEEDED(d3d->CreateRenderTargetView(target, NULL, &target_view)));

	ID3D11ShaderResourceView * target_texture = NULL;
	CHECK(SUCCEEDED(d3d->CreateShaderResourceView(target, NULL, &target_texture)));

	ID3D11Buffer * vertices = NULL;
	CHECK(DX11Resource_Create_Vertex_Buffer(d3d, sizeof(QUAD_VERTICES), D3DPOOL_MANAGED, 0,
		QUAD_VERTICES, &vertices));

	ID3D11Buffer * indices = NULL;
	CHECK(DX11Resource_Create_Index_Buffer(d3d, sizeof(QUAD_INDICES), D3DPOOL_MANAGED, 0,
		QUAD_INDICES, &indices));

	const float clear_colour[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
	device.Get_Context()->ClearRenderTargetView(target_view, clear_colour);

	backend.Set_Render_Target(target_view);
	configure_unlit_pass_through(backend);
	backend.Set_Texture_Stage_State(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
	backend.Set_Texture_Stage_State(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
	backend.Set_Texture_Stage_State(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
	backend.Set_Texture(0, target_texture);
	backend.Set_Stream_Source(vertices, sizeof(BackendVertex), 0);
	backend.Set_Indices(indices, DXGI_FORMAT_R16_UINT);

	CHECK(backend.Draw_Indexed_Triangles(6, 0, 0));

	device.Get_Context()->CopyResource(staging, target);
	D3D11_MAPPED_SUBRESOURCE mapped;
	CHECK(SUCCEEDED(device.Get_Context()->Map(staging, 0, D3D11_MAP_READ, 0, &mapped)));
	const unsigned char * centre = static_cast<const unsigned char *>(mapped.pData)
		+ mapped.RowPitch * (TARGET_SIZE / 2) + (TARGET_SIZE / 2) * 4;
	if (centre[0] != EXPECTED_BLUE) {
		printf("  centre pixel bgra %02x %02x %02x %02x\n",
			centre[0], centre[1], centre[2], centre[3]);
	}
	CHECK_EQ(centre[0], EXPECTED_BLUE);
	CHECK_EQ(centre[1], EXPECTED_GREEN);
	CHECK_EQ(centre[2], EXPECTED_RED);
	device.Get_Context()->Unmap(staging, 0);

	indices->Release();
	vertices->Release();
	target_texture->Release();
	target_view->Release();
	staging->Release();
	target->Release();
	backend.Shutdown();
}

// W3DShaderManager's render texture stands in for the scene: Direct3D 11 never draws into it, so a
// draw sampling it (the heat haze) has to read the scene, where the frame really is.  Without the
// stand-in it reads the texture's own black and the centre comes back 00 00 00.
TEST(dx11backend_a_draw_sampling_the_scene_stand_in_reads_the_scene)
{
	DX11DeviceClass device;
	CHECK(device.Create_Offscreen());
	ID3D11Device * d3d = device.Get_Device();

	DX11BackendClass backend;
	CHECK(backend.Initialise(&device));

	D3D11_TEXTURE2D_DESC description;
	memset(&description, 0, sizeof(description));
	description.Width = TARGET_SIZE;
	description.Height = TARGET_SIZE;
	description.MipLevels = 1;
	description.ArraySize = 1;
	description.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
	description.SampleDesc.Count = 1;
	description.Usage = D3D11_USAGE_DEFAULT;
	description.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

	ID3D11Texture2D * scene = NULL;
	CHECK(SUCCEEDED(d3d->CreateTexture2D(&description, NULL, &scene)));
	ID3D11Texture2D * stand_in = NULL;
	CHECK(SUCCEEDED(d3d->CreateTexture2D(&description, NULL, &stand_in)));
	D3D11_TEXTURE2D_DESC staging_description = description;
	staging_description.Usage = D3D11_USAGE_STAGING;
	staging_description.BindFlags = 0;
	staging_description.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
	ID3D11Texture2D * staging = NULL;
	CHECK(SUCCEEDED(d3d->CreateTexture2D(&staging_description, NULL, &staging)));

	ID3D11RenderTargetView * scene_view = NULL;
	CHECK(SUCCEEDED(d3d->CreateRenderTargetView(scene, NULL, &scene_view)));
	ID3D11RenderTargetView * stand_in_target = NULL;
	CHECK(SUCCEEDED(d3d->CreateRenderTargetView(stand_in, NULL, &stand_in_target)));
	ID3D11ShaderResourceView * stand_in_texture = NULL;
	CHECK(SUCCEEDED(d3d->CreateShaderResourceView(stand_in, NULL, &stand_in_texture)));

	const float white[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
	const float black[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
	device.Get_Context()->ClearRenderTargetView(scene_view, white);
	device.Get_Context()->ClearRenderTargetView(stand_in_target, black);

	ID3D11Buffer * vertices = NULL;
	CHECK(DX11Resource_Create_Vertex_Buffer(d3d, sizeof(QUAD_VERTICES), D3DPOOL_MANAGED, 0,
		QUAD_VERTICES, &vertices));
	ID3D11Buffer * indices = NULL;
	CHECK(DX11Resource_Create_Index_Buffer(d3d, sizeof(QUAD_INDICES), D3DPOOL_MANAGED, 0,
		QUAD_INDICES, &indices));

	device.Set_Scene_View(scene_view);
	backend.Set_Scene_Stand_In(stand_in_texture);
	backend.Set_Render_Target(NULL);
	backend.Set_Viewport(0, 0, TARGET_SIZE, TARGET_SIZE);	// an offscreen device has no size of its own
	configure_unlit_pass_through(backend);
	backend.Set_Texture_Stage_State(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
	backend.Set_Texture_Stage_State(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
	backend.Set_Texture_Stage_State(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
	backend.Set_Texture(0, stand_in_texture);
	backend.Set_Stream_Source(vertices, sizeof(BackendVertex), 0);
	backend.Set_Indices(indices, DXGI_FORMAT_R16_UINT);
	CHECK(backend.Draw_Indexed_Triangles(6, 0, 0));

	device.Get_Context()->CopyResource(staging, scene);
	D3D11_MAPPED_SUBRESOURCE mapped;
	CHECK(SUCCEEDED(device.Get_Context()->Map(staging, 0, D3D11_MAP_READ, 0, &mapped)));
	const unsigned char * centre = static_cast<const unsigned char *>(mapped.pData)
		+ mapped.RowPitch * (TARGET_SIZE / 2) + (TARGET_SIZE / 2) * 4;
	if (centre[0] != EXPECTED_BLUE) {
		printf("  centre pixel bgra %02x %02x %02x %02x\n",
			centre[0], centre[1], centre[2], centre[3]);
	}
	CHECK_EQ(centre[0], EXPECTED_BLUE);
	CHECK_EQ(centre[1], EXPECTED_GREEN);
	CHECK_EQ(centre[2], EXPECTED_RED);
	device.Get_Context()->Unmap(staging, 0);

	device.Set_Scene_View(NULL);
	indices->Release();
	vertices->Release();
	stand_in_texture->Release();
	stand_in_target->Release();
	scene_view->Release();
	staging->Release();
	stand_in->Release();
	scene->Release();
	backend.Shutdown();
}

// A draw with no stream bound is refused rather than attempted.  D3D9 would have drawn nothing and
// said nothing; a count that goes up is the difference between a missing pass and a silent one.
TEST(dx11backend_a_draw_with_nothing_bound_is_counted_as_refused)
{
	DX11DeviceClass device;
	CHECK(device.Create_Offscreen());

	DX11BackendClass backend;
	CHECK(backend.Initialise(&device));
	configure_unlit_pass_through(backend);

	CHECK(!backend.Draw_Indexed_Triangles(6, 0, 0));

	unsigned pipelines = 0;
	unsigned long long made = 0;
	unsigned long long refused = 0;
	backend.Statistics(pipelines, made, refused);
	CHECK_EQ(made, 0ull);
	CHECK_EQ(refused, 1ull);

	backend.Shutdown();
}

// The smoke's map holds three sums under each caster: its optical depth, that times its depth in the
// sun's clip space, and that times the depth squared plus its own spread squared.  The receivers
// rebuild the cloud along the ray out of exactly those, so a sum in the wrong channel, a depth
// through the wrong column of the sun's matrix or a disc off its centre is a shadow somewhere else.
//
// The sun here sees a hundred world units either side of the middle and puts depth z at
// z / 1000 + 0.5.  One caster at height 100, radius 50, optical depth 2 lands in the middle texel
// with depth 0.6 and a spread of half its radius, 0.025.  One outside the box draws nothing.
TEST(dx11backend_the_smoke_map_holds_each_casters_three_sums)
{
	DX11DeviceClass device;
	CHECK(device.Create_Offscreen());
	ID3D11Device * d3d = device.Get_Device();

	DX11BackendClass backend;
	CHECK(backend.Initialise(&device));

	float identity[16];
	set_identity(identity);
	float sun[16];
	set_identity(sun);
	sun[0] = 0.01f;
	sun[5] = 0.01f;
	sun[10] = 0.001f;
	sun[14] = 0.5f;
	backend.Set_Transform(D3DTS_VIEW, identity);
	backend.Set_Transform(D3DTS_PROJECTION, sun);
	CHECK(backend.Begin_Shadow_Map(64));
	backend.End_Shadow_Map();

	const float outside[5] = { 500.0f, 0.0f, 100.0f, 50.0f, 2.0f };
	CHECK(backend.Fill_Smoke_Map(outside, 1, 0.5f));
	CHECK(backend.Smoke_Map() == NULL);

	const float caster[5] = { 0.0f, 0.0f, 100.0f, 50.0f, 2.0f };
	CHECK(backend.Fill_Smoke_Map(caster, 1, 0.5f));
	ID3D11ShaderResourceView * map = backend.Smoke_Map();
	CHECK(map != NULL);
	if (map == NULL) {
		backend.Shutdown();
		return;
	}

	ID3D11Resource * resource = NULL;
	map->GetResource(&resource);
	ID3D11Texture2D * texture = static_cast<ID3D11Texture2D *>(resource);
	D3D11_TEXTURE2D_DESC description;
	texture->GetDesc(&description);
	description.Usage = D3D11_USAGE_STAGING;
	description.BindFlags = 0;
	description.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
	ID3D11Texture2D * staging = NULL;
	CHECK(SUCCEEDED(d3d->CreateTexture2D(&description, NULL, &staging)));
	device.Get_Context()->CopyResource(staging, texture);

	D3D11_MAPPED_SUBRESOURCE mapped;
	CHECK(SUCCEEDED(device.Get_Context()->Map(staging, 0, D3D11_MAP_READ, 0, &mapped)));
	const float * middle = reinterpret_cast<const float *>(static_cast<const unsigned char *>(mapped.pData)
		+ mapped.RowPitch * (description.Height / 2)) + (description.Width / 2) * 4;
	const float * corner = static_cast<const float *>(mapped.pData);
	printf("  middle texel %.5f %.5f %.5f %.5f\n", middle[0], middle[1], middle[2], middle[3]);
	CHECK_NEAR(middle[0], 2.0f, 0.05f);
	CHECK_NEAR(middle[1] / middle[0], 0.6f, 0.0001f);
	CHECK_NEAR(middle[2] / middle[0], 0.6f * 0.6f + 0.025f * 0.025f, 0.0001f);
	// the front, nearest the sun: the one caster's depth, and the far plane where nothing stands
	CHECK_NEAR(middle[3], 0.6f, 0.0001f);
	CHECK_EQ(corner[0], 0.0f);
	CHECK_EQ(corner[3], 1.0f);
	device.Get_Context()->Unmap(staging, 0);

	// Nothing handed over is a frame without smoke: the receivers stop reading the map.
	CHECK(backend.Fill_Smoke_Map(NULL, 0, 0.5f));
	CHECK(backend.Smoke_Map() == NULL);

	staging->Release();
	resource->Release();
	backend.Shutdown();
}

// The sun's depth map read back as its 24-bit depths, one a texel.
static void read_shadow_depths(DX11DeviceClass & device, DX11BackendClass & backend,
	std::vector<unsigned> & depths)
{
	ID3D11Resource * resource = NULL;
	backend.Shadow_Map()->GetResource(&resource);
	ID3D11Texture2D * texture = static_cast<ID3D11Texture2D *>(resource);
	D3D11_TEXTURE2D_DESC description;
	texture->GetDesc(&description);
	description.Usage = D3D11_USAGE_STAGING;
	description.BindFlags = 0;
	description.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
	ID3D11Texture2D * staging = NULL;
	CHECK(SUCCEEDED(device.Get_Device()->CreateTexture2D(&description, NULL, &staging)));
	device.Get_Context()->CopyResource(staging, texture);
	D3D11_MAPPED_SUBRESOURCE mapped;
	CHECK(SUCCEEDED(device.Get_Context()->Map(staging, 0, D3D11_MAP_READ, 0, &mapped)));
	depths.resize(description.Width * description.Height);
	for (unsigned y = 0; y < description.Height; ++y) {
		const unsigned * row = reinterpret_cast<const unsigned *>(
			static_cast<const unsigned char *>(mapped.pData) + mapped.RowPitch * y);
		for (unsigned x = 0; x < description.Width; ++x) {
			depths[y * description.Width + x] = row[x] & 0x00ffffff;
		}
	}
	device.Get_Context()->Unmap(staging, 0);
	staging->Release();
	resource->Release();
}

// The instanced depth-only casters (Draw_Shadow_Casters) have to leave the sun's map holding the
// depth the generated programs leave in it for the same mesh drawn once a copy, to the bit: a
// mesh drawn twice, turned and moved, and tilted in depth so every texel's value is its own.
TEST(dx11backend_instanced_shadow_casters_write_the_depth_the_old_draws_write)
{
	DX11DeviceClass device;
	CHECK(device.Create_Offscreen());
	ID3D11Device * d3d = device.Get_Device();

	DX11BackendClass backend;
	CHECK(backend.Initialise(&device));
	configure_unlit_pass_through(backend);

	// The sun's box: x and y across four units, depth over ten.
	float sun[16];
	set_identity(sun);
	sun[0] = 0.5f;
	sun[5] = 0.5f;
	sun[10] = 0.1f;
	backend.Set_Transform(D3DTS_PROJECTION, sun);

	// Two copies of the mesh, turned by different angles and moved, in Direct3D 9's layout.
	float worlds[2][16];
	const float angles[2] = { 0.5236f, -1.1f };
	const float moves[2][3] = { { 0.3f, -0.2f, 2.75f }, { -0.45f, 0.35f, 3.1f } };
	for (unsigned copy = 0; copy < 2; ++copy) {
		set_identity(worlds[copy]);
		worlds[copy][0] = cosf(angles[copy]);
		worlds[copy][1] = sinf(angles[copy]);
		worlds[copy][4] = -sinf(angles[copy]);
		worlds[copy][5] = cosf(angles[copy]);
		worlds[copy][12] = moves[copy][0];
		worlds[copy][13] = moves[copy][1];
		worlds[copy][14] = moves[copy][2];
	}

	// The quad, tilted from front to back.
	BackendVertex vertices_data[4];
	memcpy(vertices_data, QUAD_VERTICES, sizeof(vertices_data));
	const float depths_at[4] = { 0.37f, 1.91f, 4.13f, 2.59f };
	for (unsigned index = 0; index < 4; ++index) {
		vertices_data[index].Position[0] *= 1.3f;
		vertices_data[index].Position[1] *= 0.9f;
		vertices_data[index].Position[2] = depths_at[index];
	}
	ID3D11Buffer * vertices = NULL;
	CHECK(DX11Resource_Create_Vertex_Buffer(d3d, sizeof(vertices_data), D3DPOOL_MANAGED, 0,
		vertices_data, &vertices));
	ID3D11Buffer * indices = NULL;
	CHECK(DX11Resource_Create_Index_Buffer(d3d, sizeof(QUAD_INDICES), D3DPOOL_MANAGED, 0,
		QUAD_INDICES, &indices));

	// What renderShadowMap sets for the pass, and an opaque mesh's shader: both faces, so the
	// cull cannot hide a difference.
	backend.Set_Render_State(D3DRS_COLORWRITEENABLE, 0);
	backend.Set_Render_State(D3DRS_ZENABLE, D3DZB_TRUE);
	backend.Set_Render_State(D3DRS_ZWRITEENABLE, TRUE);
	backend.Set_Render_State(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
	backend.Set_Render_State(D3DRS_CULLMODE, D3DCULL_NONE);

	CHECK(backend.Begin_Shadow_Map(64));
	backend.Set_Stream_Source(vertices, sizeof(BackendVertex), 0);
	backend.Set_Indices(indices, DXGI_FORMAT_R16_UINT);
	for (unsigned copy = 0; copy < 2; ++copy) {
		backend.Set_Transform(D3DTS_WORLD, worlds[copy]);
		CHECK(backend.Draw_Indexed_Triangles(6, 0, 0));
	}
	backend.End_Shadow_Map();
	std::vector<unsigned> old_depths;
	read_shadow_depths(device, backend, old_depths);

	// The pass before the instanced one has to have drawn something, or the comparison proves
	// nothing: some texels near, some still at the far plane.
	unsigned covered = 0;
	for (size_t texel = 0; texel < old_depths.size(); ++texel) {
		if (old_depths[texel] != 0x00ffffff) {
			++covered;
		}
	}
	printf("  %u of %u texels covered\n", covered, (unsigned)old_depths.size());
	CHECK(covered > 0);
	CHECK(covered < old_depths.size());

	// The instanced draw, with the cull and depth test left somewhere else first: the run's own
	// have to be the ones it draws with.
	backend.Set_Render_State(D3DRS_CULLMODE, D3DCULL_CW);
	backend.Set_Render_State(D3DRS_ZFUNC, D3DCMP_ALWAYS);
	CHECK(backend.Begin_Shadow_Map(64));
	DX11BackendClass::ShadowCasterRun run;
	run.Kind = DX11BackendClass::SHADOW_CASTER_RIGID;
	run.Vertices = vertices;
	run.Stride = sizeof(BackendVertex);
	run.TextureOffset = 0;
	run.Indices = indices;
	run.StartIndex = 0;
	run.IndexCount = 6;
	run.BaseVertex = 0;
	run.Strip = false;
	run.CullMode = D3DCULL_NONE;
	run.DepthFunction = D3DCMP_LESSEQUAL;
	run.FirstInstance = 0;
	run.InstanceCount = 2;
	// The same worlds as the rows of a Matrix3D, which is what the mesh renderer hands over.
	float rows[2][12];
	for (unsigned copy = 0; copy < 2; ++copy) {
		for (unsigned row = 0; row < 3; ++row) {
			for (unsigned column = 0; column < 4; ++column) {
				rows[copy][row * 4 + column] = worlds[copy][column * 4 + row];
			}
		}
	}
	const float * const instance_rows[2] = { rows[0], rows[1] };
	const unsigned first_bones[2] = { 0, 0 };
	CHECK(backend.Upload_Shadow_Casters(instance_rows, first_bones, 2, NULL, 0));
	CHECK(backend.Draw_Shadow_Casters(&run, 1));
	backend.End_Shadow_Map();
	std::vector<unsigned> lean_depths;
	read_shadow_depths(device, backend, lean_depths);

	CHECK_EQ(lean_depths.size(), old_depths.size());
	unsigned differing = 0;
	for (size_t texel = 0; texel < old_depths.size() && texel < lean_depths.size(); ++texel) {
		if (old_depths[texel] != lean_depths[texel]) {
			++differing;
		}
	}
	if (differing != 0) {
		printf("  %u texels differ\n", differing);
	}
	CHECK_EQ(differing, 0u);

	indices->Release();
	vertices->Release();
	backend.Shutdown();
}

// A skin skinned in the depth pass's vertex program has to leave the depth the CPU fill leaves
// (Matrix3D::Transform_Vector per vertex, then drawn with an identity world), within a level or
// two of a 24-bit depth for the order the GPU adds the products in: two copies of a quad whose
// halves hang on two bones, each copy on its own pair of bones.
TEST(dx11backend_skinned_shadow_casters_write_the_depth_the_cpu_skinned_draws_write)
{
	static const unsigned BONES = 4;
	static const unsigned DEPTH_LEVELS_ALLOWED = 2;
	static const unsigned EDGE_TEXELS_ALLOWED = 4;

	DX11DeviceClass device;
	CHECK(device.Create_Offscreen());
	ID3D11Device * d3d = device.Get_Device();

	DX11BackendClass backend;
	CHECK(backend.Initialise(&device));
	configure_unlit_pass_through(backend);

	float sun[16];
	set_identity(sun);
	sun[0] = 0.5f;
	sun[5] = 0.5f;
	sun[10] = 0.1f;
	backend.Set_Transform(D3DTS_PROJECTION, sun);

	// Four bones, three rows of a Matrix3D each: turned about z, tipped about x, moved.
	float bone_rows[BONES][12];
	const float turns[BONES] = { 0.4f, -0.7f, 1.3f, 0.15f };
	const float tips[BONES] = { 0.2f, -0.35f, 0.1f, 0.45f };
	const float moves[BONES][3] = {
		{ 0.2f, -0.1f, 3.0f }, { -0.3f, 0.25f, 2.2f }, { 0.35f, 0.3f, 4.1f }, { -0.2f, -0.4f, 1.7f } };
	for (unsigned bone = 0; bone < BONES; ++bone) {
		const float c = cosf(turns[bone]);
		const float s = sinf(turns[bone]);
		const float ct = cosf(tips[bone]);
		const float st = sinf(tips[bone]);
		// The turn about z, then the tip about x: rows of tip * turn.
		const float rows[3][4] = {
			{ c, -s, 0.0f, moves[bone][0] },
			{ ct * s, ct * c, -st, moves[bone][1] },
			{ st * s, st * c, ct, moves[bone][2] } };
		memcpy(bone_rows[bone], rows, sizeof(rows));
	}

	// The bind pose: the quad, its first two corners on one bone and the other two on the next.
	DX11BackendClass::ShadowSkinVertex bind[4];
	for (unsigned index = 0; index < 4; ++index) {
		bind[index].Position[0] = QUAD_VERTICES[index].Position[0] * 0.8f;
		bind[index].Position[1] = QUAD_VERTICES[index].Position[1] * 0.6f;
		bind[index].Position[2] = 0.25f * (float)index - 0.3f;
		bind[index].Bone = (index < 2) ? 0 : 1;
	}

	// The CPU fill's result for each copy, drawn the old way with an identity world.
	BackendVertex deformed[2][4];
	for (unsigned copy = 0; copy < 2; ++copy) {
		for (unsigned index = 0; index < 4; ++index) {
			const float * row = bone_rows[copy * 2 + bind[index].Bone];
			const float * p = bind[index].Position;
			deformed[copy][index] = QUAD_VERTICES[index];
			for (unsigned axis = 0; axis < 3; ++axis) {
				const float * r = row + axis * 4;
				deformed[copy][index].Position[axis] = r[0] * p[0] + r[1] * p[1] + r[2] * p[2] + r[3];
			}
		}
	}
	ID3D11Buffer * vertices = NULL;
	CHECK(DX11Resource_Create_Vertex_Buffer(d3d, sizeof(deformed), D3DPOOL_MANAGED, 0, deformed, &vertices));
	ID3D11Buffer * indices = NULL;
	CHECK(DX11Resource_Create_Index_Buffer(d3d, sizeof(QUAD_INDICES), D3DPOOL_MANAGED, 0,
		QUAD_INDICES, &indices));

	backend.Set_Render_State(D3DRS_COLORWRITEENABLE, 0);
	backend.Set_Render_State(D3DRS_ZENABLE, D3DZB_TRUE);
	backend.Set_Render_State(D3DRS_ZWRITEENABLE, TRUE);
	backend.Set_Render_State(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
	backend.Set_Render_State(D3DRS_CULLMODE, D3DCULL_NONE);

	float identity[16];
	set_identity(identity);
	CHECK(backend.Begin_Shadow_Map(64));
	backend.Set_Transform(D3DTS_WORLD, identity);
	backend.Set_Stream_Source(vertices, sizeof(BackendVertex), 0);
	backend.Set_Indices(indices, DXGI_FORMAT_R16_UINT);
	for (unsigned copy = 0; copy < 2; ++copy) {
		CHECK(backend.Draw_Indexed_Triangles(6, 0, copy * 4));
	}
	backend.End_Shadow_Map();
	std::vector<unsigned> old_depths;
	read_shadow_depths(device, backend, old_depths);

	unsigned covered = 0;
	for (size_t texel = 0; texel < old_depths.size(); ++texel) {
		if (old_depths[texel] != 0x00ffffff) {
			++covered;
		}
	}
	printf("  %u of %u texels covered\n", covered, (unsigned)old_depths.size());
	CHECK(covered > 0);
	CHECK(covered < old_depths.size());

	// Uploaded behind a few vertices of another model, so the base vertex is not nought.
	DX11BackendClass::ShadowSkinVertex padding[3];
	memset(padding, 0, sizeof(padding));
	unsigned padding_first = 0;
	CHECK(backend.Upload_Shadow_Skin(padding, 3, padding_first));
	unsigned first_vertex = 0;
	CHECK(backend.Upload_Shadow_Skin(bind, 4, first_vertex));
	CHECK_EQ(first_vertex, padding_first + 3);

	CHECK(backend.Begin_Shadow_Map(64));
	DX11BackendClass::ShadowCasterRun run;
	run.Kind = DX11BackendClass::SHADOW_CASTER_SKINNED;
	run.Vertices = NULL;
	run.Stride = 0;
	run.TextureOffset = 0;
	run.Indices = indices;
	run.StartIndex = 0;
	run.IndexCount = 6;
	run.BaseVertex = first_vertex;
	run.Strip = false;
	run.CullMode = D3DCULL_NONE;
	run.DepthFunction = D3DCMP_LESSEQUAL;
	run.FirstInstance = 0;
	run.InstanceCount = 2;
	// Identity worlds, as a skin's always is.
	const float * const worlds[2] = { NULL, NULL };
	const unsigned first_bones[2] = { 0, 2 };
	const float * const bones[BONES] = { bone_rows[0], bone_rows[1], bone_rows[2], bone_rows[3] };
	CHECK(backend.Upload_Shadow_Casters(worlds, first_bones, 2, bones, BONES));
	CHECK(backend.Draw_Shadow_Casters(&run, 1));
	backend.End_Shadow_Map();
	std::vector<unsigned> lean_depths;
	read_shadow_depths(device, backend, lean_depths);

	CHECK_EQ(lean_depths.size(), old_depths.size());
	unsigned edge = 0;
	unsigned widest = 0;
	for (size_t texel = 0; texel < old_depths.size() && texel < lean_depths.size(); ++texel) {
		const bool old_covered = old_depths[texel] != 0x00ffffff;
		const bool lean_covered = lean_depths[texel] != 0x00ffffff;
		if (old_covered != lean_covered) {
			++edge;
		} else {
			const unsigned gap = (old_depths[texel] > lean_depths[texel])
				? old_depths[texel] - lean_depths[texel] : lean_depths[texel] - old_depths[texel];
			widest = (gap > widest) ? gap : widest;
		}
	}
	printf("  %u edge texels differ, widest depth gap %u\n", edge, widest);
	CHECK(edge <= EDGE_TEXELS_ALLOWED);
	CHECK(widest <= DEPTH_LEVELS_ALLOWED);

	indices->Release();
	vertices->Release();
	backend.Shutdown();
}

// The volumetric hazard clouds' text is the SDL GPU backend's too, written for DXC.  Every pass of
// it has to go through the compiler this backend uses at shader model 5, and the device has to take
// what comes out, or the clouds go Classic on the first hazard of the match.
TEST(dx11backend_the_hazard_cloud_programs_compile_at_shader_model_5)
{
	DX11DeviceClass device;
	CHECK(device.Create_Offscreen());
	DX11BackendClass backend;
	CHECK(backend.Initialise(&device));

	struct Program { std::string source; bool vertex; };
	const Program programs[] = {
		{ HazardCloud::vertexShader(), true },
		{ HazardCloud::pixelShader(HazardCloud::Pass::Volume, false), false },
		{ HazardCloud::pixelShader(HazardCloud::Pass::Composite, false), false },
		{ HazardCloud::pixelShader(HazardCloud::Pass::Density, false), false },
		{ HazardCloud::pixelShader(HazardCloud::Pass::Lighting, false), false },
		{ HazardCloud::pixelShader(HazardCloud::Pass::Heat, false), false },
		{ HazardCloud::emberVertexShader(false), true },
		{ HazardCloud::emberPixelShader(false), false },
	};
	for (const Program & program : programs) {
		std::vector<unsigned char> code;
		const bool compiled = backend.Compile_Program(program.source, "hazardcloud",
			program.vertex ? "vs_5_0" : "ps_5_0", code);
		if (!compiled) {
			printf("  %s\n", backend.First_Compiler_Error());
		}
		CHECK(compiled);
		if (!compiled) {
			continue;
		}
		ID3D11DeviceChild * made = NULL;
		const HRESULT result = program.vertex
			? device.Get_Device()->CreateVertexShader(&code[0], code.size(), NULL, (ID3D11VertexShader **)&made)
			: device.Get_Device()->CreatePixelShader(&code[0], code.size(), NULL, (ID3D11PixelShader **)&made);
		CHECK(SUCCEEDED(result));
		if (made != NULL) {
			made->Release();
		}
	}
	backend.Shutdown();
}

// The fixed-function vertex state the modern ocean patch draws with: an unlit XYZNDUV2 vertex,
// four stages (the base, the mirror's projection, the camera space noise and the shroud), fog.
static VertexPipelineDescription water_vertex_description()
{
	VertexPipelineDescription description;
	memset(&description, 0, sizeof(description));
	description.FVF = D3DFVF_XYZ | D3DFVF_NORMAL | D3DFVF_DIFFUSE | D3DFVF_TEX2;
	description.ColourVertexEnabled = true;
	description.DiffuseMaterialSource = D3DMCS_COLOR1;
	description.StageCount = 4;
	description.Stages[1].TextureCoordinateIndex = 1;
	description.Stages[2].TextureCoordinateIndex = D3DTSS_TCI_CAMERASPACEPOSITION;
	description.Stages[2].TextureTransformFlags = D3DTTFF_COUNT2;
	description.Stages[3].TextureCoordinateIndex = D3DTSS_TCI_CAMERASPACEPOSITION;
	description.Stages[3].TextureTransformFlags = D3DTTFF_COUNT2;
	description.FogEnabled = true;
	description.FogVertexMode = D3DFOG_LINEAR;
	return description;
}

// The Modern water's text is the SDL GPU backend's too.  Its two surfaces, the displaced ocean's
// vertex half and the FFT passes all have to come through the compiler at shader model 5 (the
// surfaces write a dynamically indexed array, which shader model 4 refuses) and be taken by the
// device, or the water draws nothing with Modern selected.
TEST(dx11backend_the_modern_water_programs_compile_at_shader_model_5)
{
	DX11DeviceClass device;
	CHECK(device.Create_Offscreen());
	DX11BackendClass backend;
	CHECK(backend.Initialise(&device));

	PixelPipelineDescription pipeline;
	memset(&pipeline, 0, sizeof(pipeline));
	pipeline.FogEnabled = true;
	struct Program { std::string source; const char * profile; const char * name; unsigned flags; };
	std::vector<Program> programs;
	for (const EngineShaderProgram water : { ENGINE_SHADER_WATER_TRAPEZOID, ENGINE_SHADER_WATER_RIVER }) {
		Program program = { std::string(), "ps_5_0", EngineShader_Name(water), DX11_MODERN_WATER_COMPILE_FLAGS };
		CHECK(EngineShader_Pixel_Program(water, pipeline, program.source));
		programs.push_back(program);
	}
	Program ocean = { std::string(), "vs_5_0", "water vertex", 0 };
	CHECK(DX11Water_Ocean_Vertex_Program(water_vertex_description(), ocean.source));
	programs.push_back(ocean);
	for (unsigned pass = 0; pass < DX11_WATER_OCEAN_PASSES; ++pass) {
		const Program fft = { DX11Water_Ocean_Pass_Program(pass), "ps_5_0", "ocean pass", 0 };
		programs.push_back(fft);
	}
	const Program fullscreen = { DX11Water_Ocean_Pass_Vertex_Program(), "vs_5_0", "ocean vertex", 0 };
	programs.push_back(fullscreen);

	for (const Program & program : programs) {
		std::vector<unsigned char> code;
		const bool vertex = program.profile[0] == 'v';
		const unsigned started = GetTickCount();
		const bool compiled = backend.Compile_Program(program.source, program.name, program.profile, code,
			program.flags);
		printf("  %s: %s in %u ms\n", program.name, compiled ? "compiled" : "refused",
			GetTickCount() - started);
		if (!compiled) {
			printf("  %s\n", backend.First_Compiler_Error());
		}
		CHECK(compiled);
		if (!compiled) {
			continue;
		}
		ID3D11DeviceChild * made = NULL;
		const HRESULT result = vertex
			? device.Get_Device()->CreateVertexShader(&code[0], code.size(), NULL, (ID3D11VertexShader **)&made)
			: device.Get_Device()->CreatePixelShader(&code[0], code.size(), NULL, (ID3D11PixelShader **)&made);
		CHECK(SUCCEEDED(result));
		if (made != NULL) {
			made->Release();
		}
	}
	backend.Shutdown();
}

// The block the Modern water's two stages read, built from W3DWater's registers and the draw's
// camera.  A camera standing at (10, 20, 30) has to come back out of the inverted view, the
// registers land in their fields, a river draw drops the hulls, and the foam atlas is read only
// when stage 14 holds one.
TEST(dx11backend_the_modern_water_constants_are_built_from_the_registers)
{
	static float registers[DX11_WATER_REGISTERS][4];
	memset(registers, 0, sizeof(registers));
	const float time = 12.5f;
	const float hulls = 3.0f;
	registers[1][0] = time;
	registers[6][2] = 1.0f;
	registers[7][0] = hulls;
	registers[88][0] = 100.0f;
	registers[88][2] = 0.01f;
	registers[88][3] = 0.01f;
	registers[89][0] = 1.0f;
	registers[89][2] = 4.0f;
	registers[WaterHullVisualLayout::MetadataRegister][3] = 1.0f;

	const float x = 10.0f, y = 20.0f, z = 30.0f;
	const float view[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, -x, -y, -z, 1 };
	const float projection[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 1, 0, 0, -1, 0 };
	const WaterConstants::Camera camera = { view, projection, 0.0f, 0.0f, 800.0f, 600.0f, 0.0f, 1.0f };

	CHECK(WaterConstants::FoamHistoryRegisters(registers));
	CHECK(WaterConstants::FoamHistoryAtlas(registers, 64, 64));
	WaterConstants::Block block;
	WaterConstants::Fill(registers, camera, true, block);
	CHECK(fabsf(block.CameraPosition[0] - x) < 0.001f);
	CHECK(fabsf(block.CameraPosition[1] - y) < 0.001f);
	CHECK(fabsf(block.CameraPosition[2] - z) < 0.001f);
	CHECK(block.WaterParameters[0] == time);
	CHECK(block.OpticalParameters[2] == 1.0f);
	CHECK(block.HullParameters[0] == hulls);
	CHECK(block.FoamHistoryArea[0] == 100.0f);
	CHECK(block.Viewport[0] == 0.5f && block.Viewport[2] == 800.0f);
	WaterConstants::VisualBlock visual;
	WaterConstants::FillVisual(block, registers, visual);
	CHECK(visual.VisualHullMeta[0][3] == 1.0f);

	WaterConstants::Fill(registers, camera, false, block);
	CHECK(block.FoamHistoryArea[0] == 0.0f);

	registers[1][1] = 1.0f;
	CHECK(!WaterConstants::FoamHistoryRegisters(registers));
	WaterConstants::Fill(registers, camera, false, block);
	CHECK(block.HullParameters[0] == 0.0f);
	WaterConstants::FillVisual(block, registers, visual);
	CHECK(visual.VisualHullMeta[0][3] == 0.0f);
}

// A multisampled scene the way the game draws one: four samples asked of the device, the post
// chain's target and the scene's depth made at four, the backend drawing into them, the chain
// resolving the colour onto the swap chain and Resolve_Scene_Depth the depth into the buffer every
// depth reader samples.  The left half is an opaque quad at depth 0.5 and has to come back as the
// vertex colour and as 0.5.  The right half is an alpha tested white quad whose alpha climbs across
// it and a little up it (0.75 of the width, 0.25 of the height), tested GREATER than a half, so the
// cut is a slanted line.  A plain clip cuts it with no grey at all; the alpha itself taken as
// coverage greys every pixel past the cut, which is what thinned the willows; the sharpened alpha
// greys the pixel or two each row's cut passes through and covers the rest.
TEST(dx11backend_a_multisampled_scene_resolves_its_colour_and_depth_and_covers_by_alpha)
{
	HWND window = CreateWindowExA(0, "STATIC", "dx11msaa", WS_OVERLAPPED, 0, 0, TARGET_SIZE,
		TARGET_SIZE, NULL, NULL, GetModuleHandle(NULL), NULL);
	CHECK(window != NULL);
	DX11DeviceClass device;
	device.Request_Debug_Layer();
	device.Request_Sample_Count(4);
	CHECK(device.Create(window, TARGET_SIZE, TARGET_SIZE));
	CHECK_EQ(device.Get_Sample_Count(), 4u);
	ID3D11Device * d3d = device.Get_Device();
	ID3D11DeviceContext * context = device.Get_Context();

	// No chain at all: a multisampled scene still goes into the post target and out through a copy.
	DX11PostProcessClass post;
	CHECK(post.Initialise(&device));
	post.Begin_Frame();
	device.Set_Scene_View(post.Scene_View());
	CHECK(device.Scene_Is_Multisampled());

	DX11BackendClass backend;
	CHECK(backend.Initialise(&device));
	backend.Begin_Scene();
	const float black[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
	backend.Clear(true, true, black);

	// White at alpha 0, 0.25, 1 and 0.75: bottom left, top left, top right, bottom right, one plane.
	const BackendVertex halves[8] = {
		{ { -1.0f, -1.0f, 0.5f }, { 0.0f, 0.0f, 1.0f }, VERTEX_COLOUR, { 0.0f, 0.0f }, { 0.0f, 0.0f } },
		{ { -1.0f,  1.0f, 0.5f }, { 0.0f, 0.0f, 1.0f }, VERTEX_COLOUR, { 0.0f, 0.0f }, { 0.0f, 0.0f } },
		{ {  0.0f,  1.0f, 0.5f }, { 0.0f, 0.0f, 1.0f }, VERTEX_COLOUR, { 0.0f, 0.0f }, { 0.0f, 0.0f } },
		{ {  0.0f, -1.0f, 0.5f }, { 0.0f, 0.0f, 1.0f }, VERTEX_COLOUR, { 0.0f, 0.0f }, { 0.0f, 0.0f } },
		{ {  0.0f, -1.0f, 0.5f }, { 0.0f, 0.0f, 1.0f }, 0x00ffffff, { 0.0f, 0.0f }, { 0.0f, 0.0f } },
		{ {  0.0f,  1.0f, 0.5f }, { 0.0f, 0.0f, 1.0f }, 0x40ffffff, { 0.0f, 0.0f }, { 0.0f, 0.0f } },
		{ {  1.0f,  1.0f, 0.5f }, { 0.0f, 0.0f, 1.0f }, 0xffffffff, { 0.0f, 0.0f }, { 0.0f, 0.0f } },
		{ {  1.0f, -1.0f, 0.5f }, { 0.0f, 0.0f, 1.0f }, 0xbfffffff, { 0.0f, 0.0f }, { 0.0f, 0.0f } },
	};
	ID3D11Buffer * vertices = NULL;
	CHECK(DX11Resource_Create_Vertex_Buffer(d3d, sizeof(halves), D3DPOOL_MANAGED, 0, halves, &vertices));
	ID3D11Buffer * indices = NULL;
	CHECK(DX11Resource_Create_Index_Buffer(d3d, sizeof(QUAD_INDICES), D3DPOOL_MANAGED, 0,
		QUAD_INDICES, &indices));

	configure_unlit_pass_through(backend);
	backend.Set_Render_State(D3DRS_ZENABLE, D3DZB_TRUE);
	backend.Set_Render_State(D3DRS_ZWRITEENABLE, TRUE);
	backend.Set_Render_State(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
	backend.Set_Stream_Source(vertices, sizeof(BackendVertex), 0);
	backend.Set_Indices(indices, DXGI_FORMAT_R16_UINT);
	CHECK(backend.Draw_Indexed_Triangles(6, 0, 0));
	backend.Set_Render_State(D3DRS_ALPHATESTENABLE, TRUE);
	backend.Set_Render_State(D3DRS_ALPHAREF, 0x80);
	backend.Set_Render_State(D3DRS_ALPHAFUNC, D3DCMP_GREATER);
	CHECK(backend.Draw_Indexed_Triangles(6, 0, 4));

	backend.Resolve_Scene_Depth();
	CHECK(post.Run_Chain());

	// The colour, off the swap chain.
	ID3D11Resource * back_buffer = NULL;
	device.Get_Back_Buffer_View()->GetResource(&back_buffer);
	D3D11_TEXTURE2D_DESC staging_description;
	static_cast<ID3D11Texture2D *>(back_buffer)->GetDesc(&staging_description);
	staging_description.Usage = D3D11_USAGE_STAGING;
	staging_description.BindFlags = 0;
	staging_description.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
	staging_description.MiscFlags = 0;
	ID3D11Texture2D * staging = NULL;
	CHECK(SUCCEEDED(d3d->CreateTexture2D(&staging_description, NULL, &staging)));
	context->CopyResource(staging, back_buffer);
	D3D11_MAPPED_SUBRESOURCE mapped;
	CHECK(SUCCEEDED(context->Map(staging, 0, D3D11_MAP_READ, 0, &mapped)));
	const unsigned char * pixels = static_cast<const unsigned char *>(mapped.pData);
	const unsigned char * opaque = pixels + mapped.RowPitch * (TARGET_SIZE / 2) + (TARGET_SIZE / 4) * 4;
	CHECK_EQ(opaque[0], EXPECTED_BLUE);
	CHECK_EQ(opaque[1], EXPECTED_GREEN);
	CHECK_EQ(opaque[2], EXPECTED_RED);
	// Row by row across the right half: black before the cut, white past it, grey only where it
	// passes.  Under a tenth of the way across every row is short of the reference, and past nine
	// tenths every row is well over it.
	unsigned grey = 0;
	unsigned widest = 0;
	unsigned lit_early = 0;
	unsigned dark_late = 0;
	for (unsigned y = TARGET_SIZE / 8; y < TARGET_SIZE * 7 / 8; ++y) {
		unsigned row_grey = 0;
		for (unsigned x = TARGET_SIZE / 2; x < TARGET_SIZE; ++x) {
			const unsigned value = pixels[mapped.RowPitch * y + x * 4];
			row_grey += (value > 8 && value < 247) ? 1u : 0u;
		}
		grey += row_grey;
		widest = (row_grey > widest) ? row_grey : widest;
		lit_early += (pixels[mapped.RowPitch * y + (TARGET_SIZE * 21 / 40) * 4] > 8) ? 1u : 0u;
		dark_late += (pixels[mapped.RowPitch * y + (TARGET_SIZE * 39 / 40) * 4] < 247) ? 1u : 0u;
	}
	if (grey < 8 || widest > 2 || lit_early != 0 || dark_late != 0) {
		printf("  alpha tested half: %u grey pixels, at most %u a row, %u lit early, %u dark late\n",
			grey, widest, lit_early, dark_late);
	}
	CHECK(grey >= 8);
	CHECK(widest <= 2);
	CHECK_EQ(lit_early, 0u);
	CHECK_EQ(dark_late, 0u);
	context->Unmap(staging, 0);

	// The depth, off the single-sample buffer the resolve wrote.  D24: the low 24 bits.
	ID3D11Resource * depth = NULL;
	device.Get_Depth_Texture()->GetResource(&depth);
	D3D11_TEXTURE2D_DESC depth_description;
	static_cast<ID3D11Texture2D *>(depth)->GetDesc(&depth_description);
	depth_description.Usage = D3D11_USAGE_STAGING;
	depth_description.BindFlags = 0;
	depth_description.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
	depth_description.MiscFlags = 0;
	ID3D11Texture2D * depth_staging = NULL;
	CHECK(SUCCEEDED(d3d->CreateTexture2D(&depth_description, NULL, &depth_staging)));
	context->CopyResource(depth_staging, depth);
	CHECK(SUCCEEDED(context->Map(depth_staging, 0, D3D11_MAP_READ, 0, &mapped)));
	const unsigned texel = *reinterpret_cast<const unsigned *>(static_cast<const unsigned char *>(mapped.pData)
		+ mapped.RowPitch * (TARGET_SIZE / 2) + (TARGET_SIZE / 4) * 4);
	const float resolved = static_cast<float>(texel & 0xffffff) / 16777215.0f;
	if (fabsf(resolved - 0.5f) > 0.001f) {
		printf("  resolved depth %f, wanted 0.5\n", resolved);
	}
	CHECK(fabsf(resolved - 0.5f) <= 0.001f);
	context->Unmap(depth_staging, 0);

	depth_staging->Release();
	depth->Release();
	staging->Release();
	back_buffer->Release();
	indices->Release();
	vertices->Release();
	backend.Shutdown();
	post.Shutdown();
	device.Release();
	DestroyWindow(window);
}

// The black and white view, the motion blur and the cross fade bind a texture the size of the screen
// right after the frame's clear and draw the scene into it over the single-sample depth (Depth_For).
// With multisampling the scene's clear lands on the multisampled depth, so the frame's clear has to
// take the single-sample one too: here it holds the last frame's resolve at 0.2, and a quad at 0.5
// drawn into the filter's texture fails the depth test and leaves the texture black without it.
TEST(dx11backend_a_screen_filter_after_a_multisampled_clear_draws_over_a_cleared_depth)
{
	HWND window = CreateWindowExA(0, "STATIC", "dx11msaafilter", WS_OVERLAPPED, 0, 0, TARGET_SIZE,
		TARGET_SIZE, NULL, NULL, GetModuleHandle(NULL), NULL);
	CHECK(window != NULL);
	DX11DeviceClass device;
	device.Request_Debug_Layer();
	device.Request_Sample_Count(4);
	CHECK(device.Create(window, TARGET_SIZE, TARGET_SIZE));
	CHECK_EQ(device.Get_Sample_Count(), 4u);
	ID3D11Device * d3d = device.Get_Device();
	ID3D11DeviceContext * context = device.Get_Context();

	DX11PostProcessClass post;
	CHECK(post.Initialise(&device));
	post.Begin_Frame();
	device.Set_Scene_View(post.Scene_View());
	CHECK(device.Scene_Is_Multisampled());

	DX11BackendClass backend;
	CHECK(backend.Initialise(&device));
	backend.Begin_Scene();

	const BackendVertex near_then_middle[8] = {
		{ { -1.0f, -1.0f, 0.2f }, { 0.0f, 0.0f, 1.0f }, VERTEX_COLOUR, { 0.0f, 0.0f }, { 0.0f, 0.0f } },
		{ { -1.0f,  1.0f, 0.2f }, { 0.0f, 0.0f, 1.0f }, VERTEX_COLOUR, { 0.0f, 0.0f }, { 0.0f, 0.0f } },
		{ {  1.0f,  1.0f, 0.2f }, { 0.0f, 0.0f, 1.0f }, VERTEX_COLOUR, { 0.0f, 0.0f }, { 0.0f, 0.0f } },
		{ {  1.0f, -1.0f, 0.2f }, { 0.0f, 0.0f, 1.0f }, VERTEX_COLOUR, { 0.0f, 0.0f }, { 0.0f, 0.0f } },
		{ { -1.0f, -1.0f, 0.5f }, { 0.0f, 0.0f, 1.0f }, VERTEX_COLOUR, { 0.0f, 0.0f }, { 0.0f, 0.0f } },
		{ { -1.0f,  1.0f, 0.5f }, { 0.0f, 0.0f, 1.0f }, VERTEX_COLOUR, { 0.0f, 0.0f }, { 0.0f, 0.0f } },
		{ {  1.0f,  1.0f, 0.5f }, { 0.0f, 0.0f, 1.0f }, VERTEX_COLOUR, { 0.0f, 0.0f }, { 0.0f, 0.0f } },
		{ {  1.0f, -1.0f, 0.5f }, { 0.0f, 0.0f, 1.0f }, VERTEX_COLOUR, { 0.0f, 0.0f }, { 0.0f, 0.0f } },
	};
	ID3D11Buffer * vertices = NULL;
	CHECK(DX11Resource_Create_Vertex_Buffer(d3d, sizeof(near_then_middle), D3DPOOL_MANAGED, 0,
		near_then_middle, &vertices));
	ID3D11Buffer * indices = NULL;
	CHECK(DX11Resource_Create_Index_Buffer(d3d, sizeof(QUAD_INDICES), D3DPOOL_MANAGED, 0,
		QUAD_INDICES, &indices));
	configure_unlit_pass_through(backend);
	backend.Set_Render_State(D3DRS_ZENABLE, D3DZB_TRUE);
	backend.Set_Render_State(D3DRS_ZWRITEENABLE, TRUE);
	backend.Set_Render_State(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
	backend.Set_Stream_Source(vertices, sizeof(BackendVertex), 0);
	backend.Set_Indices(indices, DXGI_FORMAT_R16_UINT);

	// Last frame: the near quad, resolved into the single-sample depth.
	const float black[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
	backend.Clear(true, true, black);
	CHECK(backend.Draw_Indexed_Triangles(6, 0, 0));
	backend.Resolve_Scene_Depth();

	// This frame: the clear, then the filter's texture and the quad behind last frame's depth.
	backend.Clear(true, true, black);
	D3D11_TEXTURE2D_DESC target_description;
	memset(&target_description, 0, sizeof(target_description));
	target_description.Width = TARGET_SIZE;
	target_description.Height = TARGET_SIZE;
	target_description.MipLevels = 1;
	target_description.ArraySize = 1;
	target_description.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
	target_description.SampleDesc.Count = 1;
	target_description.Usage = D3D11_USAGE_DEFAULT;
	target_description.BindFlags = D3D11_BIND_RENDER_TARGET;
	ID3D11Texture2D * target = NULL;
	CHECK(SUCCEEDED(d3d->CreateTexture2D(&target_description, NULL, &target)));
	ID3D11RenderTargetView * target_view = NULL;
	CHECK(SUCCEEDED(d3d->CreateRenderTargetView(target, NULL, &target_view)));
	backend.Set_Render_Target(target_view);
	backend.Clear(true, false, black);
	CHECK(backend.Draw_Indexed_Triangles(6, 0, 4));
	backend.Set_Render_Target(NULL);

	D3D11_TEXTURE2D_DESC staging_description = target_description;
	staging_description.Usage = D3D11_USAGE_STAGING;
	staging_description.BindFlags = 0;
	staging_description.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
	ID3D11Texture2D * staging = NULL;
	CHECK(SUCCEEDED(d3d->CreateTexture2D(&staging_description, NULL, &staging)));
	context->CopyResource(staging, target);
	D3D11_MAPPED_SUBRESOURCE mapped;
	CHECK(SUCCEEDED(context->Map(staging, 0, D3D11_MAP_READ, 0, &mapped)));
	const unsigned char * centre = static_cast<const unsigned char *>(mapped.pData)
		+ mapped.RowPitch * (TARGET_SIZE / 2) + (TARGET_SIZE / 2) * 4;
	if (centre[0] != EXPECTED_BLUE) {
		printf("  filter texture centre bgra %02x %02x %02x %02x\n", centre[0], centre[1], centre[2],
			centre[3]);
	}
	CHECK_EQ(centre[0], EXPECTED_BLUE);
	CHECK_EQ(centre[1], EXPECTED_GREEN);
	CHECK_EQ(centre[2], EXPECTED_RED);
	context->Unmap(staging, 0);

	staging->Release();
	target_view->Release();
	target->Release();
	indices->Release();
	vertices->Release();
	backend.Shutdown();
	post.Shutdown();
	device.Release();
	DestroyWindow(window);
}
