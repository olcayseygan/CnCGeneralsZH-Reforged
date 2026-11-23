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

// The post-process chain, run for real on a device with a swap chain.
//
// A known picture is written into the texture the scene would have been drawn into, the chain is
// run, and the swap chain's back buffer is read back.  So these are not tests that the code was
// called; they are tests of what came out the other end.
//
// The two that matter are opposites.  The copy pass has to give back every pixel unchanged, which
// is what says the offscreen target, the full screen triangle, the sampler and the viewport are all
// exactly one to one - a half pixel anywhere in that path shows up here as a whole frame that moved.
// FXAA has to change a stepped diagonal and leave a flat field alone, which is what says the shader
// is doing the thing it is named after rather than blurring or nothing.
//
// A machine with no Direct3D 11 device at all reports these as skipped: there is nothing to check
// there, and the build agents that cannot make a device still have to come back green.

#include "test_harness.h"

#include "dx11device.h"
#include "dx11post.h"
#include "dx11runtime.h"

#include <d3d11.h>
#include <windows.h>
#include <stdio.h>
#include <string.h>

static const unsigned SURFACE_SIZE = 64;
static const unsigned BYTES_A_PIXEL = 4;
static const unsigned char DARK_LEVEL = 0;
static const unsigned char LIGHT_LEVEL = 255;
static const unsigned char OPAQUE_ALPHA = 255;

// A device with a swap chain, and the window it needs, torn down together.
class DeviceFixture
{
public:
	DeviceFixture() : Window(NULL) {}

	~DeviceFixture()
	{
		Post.Shutdown();
		Device.Release();
		if (Window != NULL) {
			DestroyWindow(Window);
		}
	}

	bool Create()
	{
		Window = CreateWindowExA(0, "STATIC", "dx11post", WS_OVERLAPPED, 0, 0, SURFACE_SIZE,
			SURFACE_SIZE, NULL, NULL, GetModuleHandle(NULL), NULL);
		if (Window == NULL) {
			return false;
		}
		if (!Device.Create(Window, SURFACE_SIZE, SURFACE_SIZE)) {
			return false;
		}
		return Post.Initialise(&Device);
	}

	DX11DeviceClass & Get_Device() { return Device; }
	DX11PostProcessClass & Get_Post() { return Post; }

private:
	DeviceFixture(const DeviceFixture &);
	DeviceFixture & operator=(const DeviceFixture &);

	HWND Window;
	DX11DeviceClass Device;
	DX11PostProcessClass Post;
};

// One pixel of the test picture, in the swap chain's own blue-green-red-alpha order.
static void set_pixel(unsigned char * pixels, unsigned x, unsigned y, unsigned char level)
{
	unsigned char * pixel = pixels + (y * SURFACE_SIZE + x) * BYTES_A_PIXEL;
	pixel[0] = level;
	pixel[1] = level;
	pixel[2] = level;
	pixel[3] = OPAQUE_ALPHA;
}

static unsigned char pixel_level(const unsigned char * pixels, unsigned pitch, unsigned x,
	unsigned y)
{
	return pixels[y * pitch + x * BYTES_A_PIXEL];
}

// Everything one level, which has no edge in it for an edge filter to find.
static void fill_flat(unsigned char * pixels, unsigned char level)
{
	for (unsigned y = 0; y < SURFACE_SIZE; ++y) {
		for (unsigned x = 0; x < SURFACE_SIZE; ++x) {
			set_pixel(pixels, x, y, level);
		}
	}
}

// A diagonal, which is the shape that aliases.  A vertical or a horizontal edge lands exactly on
// the pixel grid and has nothing to antialias, and FXAA leaves those alone by design, so a test
// built on one would pass whether the shader worked or not.
static void fill_diagonal(unsigned char * pixels)
{
	for (unsigned y = 0; y < SURFACE_SIZE; ++y) {
		for (unsigned x = 0; x < SURFACE_SIZE; ++x) {
			set_pixel(pixels, x, y, (x > y) ? LIGHT_LEVEL : DARK_LEVEL);
		}
	}
}

// Write the picture into the texture the scene would have been drawn into.
static bool write_scene(DX11DeviceClass & device, DX11PostProcessClass & post,
	const unsigned char * pixels)
{
	ID3D11RenderTargetView * view = post.Scene_View();
	if (view == NULL) {
		return false;
	}

	ID3D11Resource * resource = NULL;
	view->GetResource(&resource);
	if (resource == NULL) {
		return false;
	}
	device.Get_Context()->UpdateSubresource(resource, 0, NULL, pixels,
		SURFACE_SIZE * BYTES_A_PIXEL, 0);
	resource->Release();
	return true;
}

// The swap chain's back buffer, copied into a staging texture and mapped.  The caller frees it.
static unsigned char * read_back_buffer(DX11DeviceClass & device, unsigned & pitch)
{
	pitch = 0;
	ID3D11RenderTargetView * view = device.Get_Back_Buffer_View();
	if (view == NULL) {
		return NULL;
	}

	ID3D11Resource * back_buffer = NULL;
	view->GetResource(&back_buffer);
	if (back_buffer == NULL) {
		return NULL;
	}

	D3D11_TEXTURE2D_DESC description;
	((ID3D11Texture2D *)back_buffer)->GetDesc(&description);
	description.Usage = D3D11_USAGE_STAGING;
	description.BindFlags = 0;
	description.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
	description.MiscFlags = 0;

	ID3D11Texture2D * staging = NULL;
	if (FAILED(device.Get_Device()->CreateTexture2D(&description, NULL, &staging))) {
		back_buffer->Release();
		return NULL;
	}
	device.Get_Context()->CopyResource(staging, back_buffer);

	D3D11_MAPPED_SUBRESOURCE mapped;
	unsigned char * pixels = NULL;
	if (SUCCEEDED(device.Get_Context()->Map(staging, 0, D3D11_MAP_READ, 0, &mapped))) {
		pitch = SURFACE_SIZE * BYTES_A_PIXEL;
		pixels = new unsigned char[pitch * SURFACE_SIZE];
		for (unsigned row = 0; row < SURFACE_SIZE; ++row) {
			memcpy(pixels + row * pitch, (const unsigned char *)mapped.pData + row * mapped.RowPitch,
				pitch);
		}
		device.Get_Context()->Unmap(staging, 0);
	}

	staging->Release();
	back_buffer->Release();
	return pixels;
}

// How many pixels of the result differ from the picture that went in.
static unsigned differing_pixels(const unsigned char * source, const unsigned char * result,
	unsigned pitch)
{
	unsigned differing = 0;
	for (unsigned y = 0; y < SURFACE_SIZE; ++y) {
		for (unsigned x = 0; x < SURFACE_SIZE; ++x) {
			if (pixel_level(source, SURFACE_SIZE * BYTES_A_PIXEL, x, y)
					!= pixel_level(result, pitch, x, y)) {
				++differing;
			}
		}
	}
	return differing;
}

// Runs one chain over one picture and hands back what reached the swap chain.  NULL when this
// machine has no device to run it on.
static unsigned char * run_chain(DeviceFixture & fixture, const DX11PostEffect * effects,
	unsigned count, const unsigned char * picture, unsigned & pitch)
{
	fixture.Get_Post().Set_Chain(effects, count);
	fixture.Get_Post().Begin_Frame();
	if (!write_scene(fixture.Get_Device(), fixture.Get_Post(), picture)) {
		return NULL;
	}
	fixture.Get_Post().Run_Chain();
	return read_back_buffer(fixture.Get_Device(), pitch);
}

TEST(dx11post_chain_names_parse)
{
	DX11PostEffect effects[DX11_POST_CHAIN_LIMIT];
	unsigned count = 0;

	CHECK(DX11Post_Parse_Chain("fxaa", effects, count));
	CHECK_EQ(1u, count);
	CHECK_EQ((int)DX11_POST_FXAA, (int)effects[0]);

	CHECK(DX11Post_Parse_Chain("fxaa,sharpen", effects, count));
	CHECK_EQ(2u, count);
	CHECK_EQ((int)DX11_POST_FXAA, (int)effects[0]);
	CHECK_EQ((int)DX11_POST_SHARPEN, (int)effects[1]);

	CHECK(DX11Post_Parse_Chain("copy", effects, count));
	CHECK_EQ(1u, count);
	CHECK_EQ((int)DX11_POST_COPY, (int)effects[0]);

	CHECK(DX11Post_Parse_Chain("bloom", effects, count));
	CHECK_EQ(1u, count);
	CHECK_EQ((int)DX11_POST_BLOOM, (int)effects[0]);

	// Bloom first and antialiasing after it is the order that means something: the frame is back in
	// eight bits by then and FXAA is looking at what the player will see.
	CHECK(DX11Post_Parse_Chain("bloom,fxaa", effects, count));
	CHECK_EQ(2u, count);
	CHECK_EQ((int)DX11_POST_BLOOM, (int)effects[0]);

	// The other way round cannot be answered, so it is refused rather than quietly reordered.
	CHECK(!DX11Post_Parse_Chain("fxaa,bloom", effects, count));
	CHECK_EQ(0u, count);

	CHECK_STR("fxaa", DX11Post_Effect_Name(DX11_POST_FXAA));
	CHECK_STR("sharpen", DX11Post_Effect_Name(DX11_POST_SHARPEN));
	CHECK_STR("copy", DX11Post_Effect_Name(DX11_POST_COPY));
	CHECK_STR("bloom", DX11Post_Effect_Name(DX11_POST_BLOOM));
}

TEST(dx11post_off_and_nonsense_both_leave_no_chain)
{
	DX11PostEffect effects[DX11_POST_CHAIN_LIMIT];
	unsigned count = 0;

	// "off" is understood and asks for nothing.
	CHECK(DX11Post_Parse_Chain("off", effects, count));
	CHECK_EQ(0u, count);
	CHECK(DX11Post_Parse_Chain("none", effects, count));
	CHECK_EQ(0u, count);
	CHECK(DX11Post_Parse_Chain("", effects, count));
	CHECK_EQ(0u, count);

	// A name nobody knows is refused rather than skipped, so -dx11post motionblur does not quietly
	// run something else.
	CHECK(!DX11Post_Parse_Chain("motionblur", effects, count));
	CHECK_EQ(0u, count);
	CHECK(!DX11Post_Parse_Chain("fxaa,motionblur", effects, count));
	CHECK_EQ(0u, count);
	CHECK(!DX11Post_Parse_Chain(NULL, effects, count));
	CHECK_EQ(0u, count);

	// Longer than the chain can hold is refused too, rather than being cut short.
	CHECK(!DX11Post_Parse_Chain("fxaa,fxaa,fxaa,fxaa,fxaa", effects, count));
	CHECK_EQ(0u, count);
}

TEST(dx11post_no_chain_leaves_the_scene_on_the_swap_chain)
{
	DeviceFixture fixture;
	if (!fixture.Create()) {
		printf("skip: no Direct3D 11 device on this machine\n");
		return;
	}

	fixture.Get_Post().Set_Chain(NULL, 0);
	CHECK_EQ(0u, fixture.Get_Post().Chain_Length());
	// NULL is the device's word for the swap chain, so an absent chain costs nothing at all: the
	// scene is drawn where it has always been drawn.
	CHECK(fixture.Get_Post().Scene_View() == NULL);
}

TEST(dx11post_copy_gives_every_pixel_back_unchanged)
{
	DeviceFixture fixture;
	if (!fixture.Create()) {
		printf("skip: no Direct3D 11 device on this machine\n");
		return;
	}

	unsigned char * picture = new unsigned char[SURFACE_SIZE * SURFACE_SIZE * BYTES_A_PIXEL];
	fill_diagonal(picture);

	const DX11PostEffect chain[] = { DX11_POST_COPY };
	unsigned pitch = 0;
	unsigned char * result = run_chain(fixture, chain, 1, picture, pitch);
	CHECK(result != NULL);
	if (result != NULL) {
		// Exactly zero. A sampler set to point instead of linear, a viewport carrying the scene's
		// half pixel, or a triangle that does not land on the corners would all put a number here.
		CHECK_EQ(0u, differing_pixels(picture, result, pitch));
		delete [] result;
	}
	delete [] picture;
}

TEST(dx11post_a_warp_bends_the_edge_its_ring_crosses_and_clearing_it_gives_the_frame_back)
{
	DeviceFixture fixture;
	if (!fixture.Create()) {
		printf("skip: no Direct3D 11 device on this machine\n");
		return;
	}

	unsigned char * picture = new unsigned char[SURFACE_SIZE * SURFACE_SIZE * BYTES_A_PIXEL];
	fill_diagonal(picture);

	// The blast sits in the dark half and its ring touches the diagonal square on, where pushing
	// the picture outward moves the edge rather than sliding it along itself.
	DX11PostWarp warp;
	warp.CentreX = 0.3f;
	warp.CentreY = 0.7f;
	warp.Radius = 0.4f;
	warp.Pull = 0.0f;
	warp.RingRadius = 0.28f;
	warp.RingWidth = 0.05f;
	warp.RingStrength = 0.1f;
	warp.RingSquash = 1.0f;
	DX11Post_Set_Warps(&warp, 1);

	const DX11PostEffect chain[] = { DX11_POST_COPY };
	unsigned pitch = 0;
	unsigned char * result = run_chain(fixture, chain, 1, picture, pitch);
	CHECK(result != NULL);
	if (result != NULL) {
		CHECK(differing_pixels(picture, result, pitch) > 8u);
		// Far from the ring nothing moves.
		CHECK_EQ(DARK_LEVEL, pixel_level(result, pitch, 1, SURFACE_SIZE - 2));
		CHECK_EQ(LIGHT_LEVEL, pixel_level(result, pitch, SURFACE_SIZE - 2, 1));
		delete [] result;
	}

	// The game clears the warps on the frame after the last blast ends, and that frame has to be
	// the copy pass's exact one again, not a frame with a last trace of the ring in it.
	DX11Post_Set_Warps(NULL, 0);
	result = run_chain(fixture, chain, 1, picture, pitch);
	CHECK(result != NULL);
	if (result != NULL) {
		CHECK_EQ(0u, differing_pixels(picture, result, pitch));
		delete [] result;
	}
	delete [] picture;
}

// -heathaze: one splat in the middle of the frame over a far depth bends the diagonal under it and
// nothing in the corners; the next frame, with no splat set, is the copy pass's exact one, and so is
// one whose depth stands at the flame's.
TEST(dx11post_a_heat_splat_bends_the_picture_under_it_and_none_leaves_it_alone)
{
	DeviceFixture fixture;
	if (!fixture.Create()) {
		printf("skip: no Direct3D 11 device on this machine\n");
		return;
	}
	DX11DeviceClass & device = fixture.Get_Device();
	DX11PostProcessClass & post = fixture.Get_Post();

	unsigned char * picture = new unsigned char[SURFACE_SIZE * SURFACE_SIZE * BYTES_A_PIXEL];
	fill_diagonal(picture);
	const DX11PostEffect chain[] = { DX11_POST_COPY };
	post.Set_Chain(chain, 1);
	CHECK(post.Wants_Heat_Haze_Programs());
	post.Compile_Heat_Haze_Programs();
	CHECK(!post.Wants_Heat_Haze_Programs());

	// Centre, half widths, then a flame at depth a half, a shift of 5% of the frame, time 0, then
	// planes 1 and 100, a clearance of 5 and a softness of 10: the cleared depth of 1 is the far
	// plane, far behind it.
	const float splat[DIRECT3D11_HEAT_SPLAT_FLOATS] = {
		0.0f, 0.0f, 0.5f, 0.5f,
		0.5f, 0.05f, 0.0f, 0.0f,
		1.0f, 100.0f, 5.0f, 10.0f,
		0.0f, 0.0f, 0.0f, 0.0f };
	// A laser's splat, the same with a level beam through the middle row and a still band 6% of
	// the frame's height either side of it (3.8 pixels), full past another 2%: the rows on the beam
	// keep every pixel, and the diagonal still bends in rows clear of the band.
	float beam[DIRECT3D11_HEAT_SPLAT_FLOATS];
	memcpy(beam, splat, sizeof(beam));
	beam[12] = 1.0f;
	beam[14] = 0.06f;
	beam[15] = 0.02f;
	const unsigned middle = SURFACE_SIZE / 2;
	// Frame 2 sets the splat again over a depth of a half, the flame's own: a unit standing in the
	// fire, which has to keep every pixel.  Frame 3 sets the beam's.
	for (unsigned frame = 0; frame < 4; ++frame) {
		post.Open_Frame();
		post.Begin_Frame();
		device.Set_Scene_View(post.Scene_View());
		device.Get_Context()->ClearDepthStencilView(device.Get_Depth_Stencil_View(), D3D11_CLEAR_DEPTH,
			(frame == 2) ? 0.5f : 1.0f, 0);
		CHECK(write_scene(device, post, picture));
		if (frame != 1) {
			post.Set_Heat_Splats((frame == 3) ? beam : splat, 1);
		}
		CHECK_EQ(frame != 1, post.Heat_Haze_Scene(device.Get_Depth_Texture()));
		CHECK(post.Run_Chain());
		unsigned pitch = 0;
		unsigned char * result = read_back_buffer(device, pitch);
		CHECK(result != NULL);
		if (result != NULL) {
			if (frame == 0) {
				CHECK(differing_pixels(picture, result, pitch) > 8u);
				CHECK_EQ(DARK_LEVEL, pixel_level(result, pitch, 1, SURFACE_SIZE - 2));
				CHECK_EQ(LIGHT_LEVEL, pixel_level(result, pitch, SURFACE_SIZE - 2, 1));
			} else if (frame == 3) {
				unsigned on_beam = 0;
				unsigned clear_of_it = 0;
				for (unsigned y = 0; y < SURFACE_SIZE; ++y) {
					for (unsigned x = 0; x < SURFACE_SIZE; ++x) {
						if (pixel_level(picture, SURFACE_SIZE * BYTES_A_PIXEL, x, y) == pixel_level(result, pitch, x, y)) {
							continue;
						}
						if (y + 2 >= middle && y <= middle + 1) {
							++on_beam;
						} else if (y + 6 <= middle || y >= middle + 6) {
							++clear_of_it;
						}
					}
				}
				CHECK_EQ(0u, on_beam);
				CHECK(clear_of_it > 2u);
			} else {
				CHECK_EQ(0u, differing_pixels(picture, result, pitch));
			}
			delete [] result;
		}
	}
	device.Set_Scene_View(NULL);
	delete [] picture;
}

TEST(dx11post_fxaa_leaves_a_flat_picture_alone)
{
	DeviceFixture fixture;
	if (!fixture.Create()) {
		printf("skip: no Direct3D 11 device on this machine\n");
		return;
	}

	unsigned char * picture = new unsigned char[SURFACE_SIZE * SURFACE_SIZE * BYTES_A_PIXEL];
	fill_flat(picture, 128);

	const DX11PostEffect chain[] = { DX11_POST_FXAA };
	unsigned pitch = 0;
	unsigned char * result = run_chain(fixture, chain, 1, picture, pitch);
	CHECK(result != NULL);
	if (result != NULL) {
		// The edge threshold is what keeps the filter off a surface that has no edge in it. Without
		// it every flat wall in the game would be run through a blur.
		CHECK_EQ(0u, differing_pixels(picture, result, pitch));
		delete [] result;
	}
	delete [] picture;
}

TEST(dx11post_fxaa_works_on_a_stepped_diagonal)
{
	DeviceFixture fixture;
	if (!fixture.Create()) {
		printf("skip: no Direct3D 11 device on this machine\n");
		return;
	}

	unsigned char * picture = new unsigned char[SURFACE_SIZE * SURFACE_SIZE * BYTES_A_PIXEL];
	fill_diagonal(picture);

	const DX11PostEffect chain[] = { DX11_POST_FXAA };
	unsigned pitch = 0;
	unsigned char * result = run_chain(fixture, chain, 1, picture, pitch);
	CHECK(result != NULL);
	if (result != NULL) {
		// The staircase is 64 steps long and every step has pixels either side of it that should
		// have come out between the two levels. Well short of that and the shader is not running.
		const unsigned changed = differing_pixels(picture, result, pitch);
		CHECK(changed > 32u);

		// And it is antialiasing rather than repainting: the far corners are nowhere near the edge
		// and have to come back untouched.
		CHECK_EQ(DARK_LEVEL, pixel_level(result, pitch, 1, SURFACE_SIZE - 2));
		CHECK_EQ(LIGHT_LEVEL, pixel_level(result, pitch, SURFACE_SIZE - 2, 1));
		delete [] result;
	}
	delete [] picture;
}

TEST(dx11post_a_frame_with_no_world_in_it_goes_through_untouched)
{
	DeviceFixture fixture;
	if (!fixture.Create()) {
		printf("skip: no Direct3D 11 device on this machine\n");
		return;
	}

	unsigned char * picture = new unsigned char[SURFACE_SIZE * SURFACE_SIZE * BYTES_A_PIXEL];
	fill_diagonal(picture);

	// A menu draws no world, so nothing calls Run_Chain and the frame would sit in the offscreen
	// texture with the screen black behind it. Copy_Through is what the present path falls back to,
	// and because it is a copy rather than the chain, a screen made entirely of text is not run
	// through an edge filter on its way out.
	const DX11PostEffect chain[] = { DX11_POST_FXAA };
	fixture.Get_Post().Set_Chain(chain, 1);
	fixture.Get_Post().Begin_Frame();
	CHECK(write_scene(fixture.Get_Device(), fixture.Get_Post(), picture));
	CHECK(fixture.Get_Post().Copy_Through());

	unsigned pitch = 0;
	unsigned char * result = read_back_buffer(fixture.Get_Device(), pitch);
	CHECK(result != NULL);
	if (result != NULL) {
		CHECK_EQ(0u, differing_pixels(picture, result, pitch));
		delete [] result;
	}
	delete [] picture;
}

// Half floats, written by hand.  Only three values are needed and a general converter would be more
// code than the constants: 4.0 is sign 0, exponent 17 biased, mantissa 0, and 1.0 is exponent 15.
static const unsigned short HALF_ZERO = 0x0000;
static const unsigned short HALF_ONE = 0x3C00;
static const unsigned short HALF_FOUR = 0x4400;

static const unsigned BRIGHT_BLOCK_SIZE = 4;
static const unsigned HALF_CHANNELS = 4;

// Clear the scene to one colour.  Works whatever the target's format, which an UpdateSubresource of
// eight bit pixels does not: with bloom in the chain the scene target holds half floats.
static bool clear_scene(DX11DeviceClass & device, DX11PostProcessClass & post, float level)
{
	ID3D11RenderTargetView * view = post.Scene_View();
	if (view == NULL) {
		return false;
	}
	const float colour[4] = { level, level, level, 1.0f };
	device.Get_Context()->ClearRenderTargetView(view, colour);
	return true;
}

// A small block of light in the middle of a black scene, at four times the brightness of white.
static bool write_bright_block(DX11DeviceClass & device, DX11PostProcessClass & post)
{
	ID3D11RenderTargetView * view = post.Scene_View();
	if (view == NULL) {
		return false;
	}

	unsigned short block[BRIGHT_BLOCK_SIZE * BRIGHT_BLOCK_SIZE * HALF_CHANNELS];
	for (unsigned index = 0; index < BRIGHT_BLOCK_SIZE * BRIGHT_BLOCK_SIZE; ++index) {
		block[index * HALF_CHANNELS + 0] = HALF_FOUR;
		block[index * HALF_CHANNELS + 1] = HALF_FOUR;
		block[index * HALF_CHANNELS + 2] = HALF_FOUR;
		block[index * HALF_CHANNELS + 3] = HALF_ONE;
	}

	ID3D11Resource * resource = NULL;
	view->GetResource(&resource);
	if (resource == NULL) {
		return false;
	}

	const unsigned left = SURFACE_SIZE / 2;
	const unsigned top = SURFACE_SIZE / 2;
	D3D11_BOX box;
	box.left = left;
	box.top = top;
	box.front = 0;
	box.right = left + BRIGHT_BLOCK_SIZE;
	box.bottom = top + BRIGHT_BLOCK_SIZE;
	box.back = 1;
	device.Get_Context()->UpdateSubresource(resource, 0, &box, block,
		BRIGHT_BLOCK_SIZE * HALF_CHANNELS * sizeof(unsigned short), 0);
	resource->Release();
	return true;
}

// A square of one half float colour written into the scene target, its top left corner at left, top.
static bool write_half_block(DX11DeviceClass & device, DX11PostProcessClass & post, unsigned left,
	unsigned top, unsigned size, unsigned short red, unsigned short green, unsigned short blue)
{
	ID3D11RenderTargetView * view = post.Scene_View();
	if (view == NULL || size == 0 || left + size > SURFACE_SIZE || top + size > SURFACE_SIZE) {
		return false;
	}
	unsigned short block[SURFACE_SIZE * SURFACE_SIZE * HALF_CHANNELS];
	for (unsigned index = 0; index < size * size; ++index) {
		block[index * HALF_CHANNELS + 0] = red;
		block[index * HALF_CHANNELS + 1] = green;
		block[index * HALF_CHANNELS + 2] = blue;
		block[index * HALF_CHANNELS + 3] = HALF_ONE;
	}
	ID3D11Resource * resource = NULL;
	view->GetResource(&resource);
	if (resource == NULL) {
		return false;
	}
	D3D11_BOX box;
	box.left = left;
	box.top = top;
	box.front = 0;
	box.right = left + size;
	box.bottom = top + size;
	box.back = 1;
	device.Get_Context()->UpdateSubresource(resource, 0, &box, block,
		size * HALF_CHANNELS * sizeof(unsigned short), 0);
	resource->Release();
	return true;
}

// One channel of one pixel of the back buffer, which is blue, green, red, alpha.
static int pixel_channel(const unsigned char * pixels, unsigned pitch, unsigned x, unsigned y,
	unsigned channel)
{
	return pixels[y * pitch + x * BYTES_A_PIXEL + channel];
}

static const unsigned short HALF_QUARTER = 0x3400;
static const unsigned short HALF_FIVE_EIGHTHS = 0x3900;
static const unsigned short HALF_TWO = 0x4000;
static const unsigned CHANNEL_BLUE = 0;
static const unsigned CHANNEL_RED = 2;

TEST(dx11post_bloom_saturation_leaves_an_orange_halo_less_blue_and_no_less_red)
{
	DeviceFixture fixture;
	if (!fixture.Create()) {
		printf("skip: no Direct3D 11 device on this machine\n");
		return;
	}

	const DX11PostEffect chain[] = { DX11_POST_BLOOM };
	fixture.Get_Post().Set_Chain(chain, 1);

	// A fire past white, orange: four, two and one times white.  Ten pixels above it the scene is
	// black, so what reaches the screen there is the glow alone.
	int red[2];
	int blue[2];
	const float saturations[2] = { 1.0f, 1.5f };
	for (unsigned run = 0; run < 2; ++run) {
		DX11Post_Set_Bloom(1.0f, 1.5f, 1.0f, 2, saturations[run]);
		fixture.Get_Post().Begin_Frame();
		CHECK(clear_scene(fixture.Get_Device(), fixture.Get_Post(), 0.0f));
		CHECK(write_half_block(fixture.Get_Device(), fixture.Get_Post(), SURFACE_SIZE / 2,
			SURFACE_SIZE / 2, BRIGHT_BLOCK_SIZE, HALF_FOUR, HALF_TWO, HALF_ONE));
		CHECK(fixture.Get_Post().Run_Chain());
		unsigned pitch = 0;
		unsigned char * result = read_back_buffer(fixture.Get_Device(), pitch);
		CHECK(result != NULL);
		red[run] = (result != NULL) ? pixel_channel(result, pitch, SURFACE_SIZE / 2, SURFACE_SIZE / 2 - 10, CHANNEL_RED) : -1;
		blue[run] = (result != NULL) ? pixel_channel(result, pitch, SURFACE_SIZE / 2, SURFACE_SIZE / 2 - 10, CHANNEL_BLUE) : -1;
		delete [] result;
	}
	DX11Post_Set_Bloom(1.0f, 1.5f, 1.0f, 2);

	CHECK(red[0] > 0);
	CHECK(red[1] >= red[0]);
	CHECK(blue[1] < blue[0]);
}

TEST(dx11post_clarity_parts_an_edge_and_leaves_a_flat_field_and_the_off_frame_alone)
{
	DeviceFixture fixture;
	if (!fixture.Create()) {
		printf("skip: no Direct3D 11 device on this machine\n");
		return;
	}

	const DX11PostEffect chain[] = { DX11_POST_BLOOM };
	fixture.Get_Post().Set_Chain(chain, 1);
	DX11Post_Set_Bloom(1.0f, 0.0f, 1.0f, 2);

	// A lit square on dull ground, nothing near white, so nothing blooms.  Just inside its edge and
	// just outside it, with the clarity off and then on.
	const unsigned left = 24;
	const unsigned size = 16;
	int inside[3];
	int outside[3];
	const float strengths[3] = { 0.0f, 0.75f, 0.0f };
	for (unsigned run = 0; run < 3; ++run) {
		DX11Post_Set_Clarity(strengths[run]);
		fixture.Get_Post().Begin_Frame();
		CHECK(clear_scene(fixture.Get_Device(), fixture.Get_Post(), 0.25f));
		CHECK(write_half_block(fixture.Get_Device(), fixture.Get_Post(), left, left, size,
			HALF_FIVE_EIGHTHS, HALF_FIVE_EIGHTHS, HALF_FIVE_EIGHTHS));
		CHECK(fixture.Get_Post().Run_Chain());
		unsigned pitch = 0;
		unsigned char * result = read_back_buffer(fixture.Get_Device(), pitch);
		CHECK(result != NULL);
		inside[run] = (result != NULL) ? pixel_level(result, pitch, left + 1, left + size / 2) : -1;
		outside[run] = (result != NULL) ? pixel_level(result, pitch, left - 2, left + size / 2) : -1;
		delete [] result;
	}

	// Off is the frame that went in; on, the light side of the edge rises and the dark side sinks;
	// off again is the first frame to the bit.
	CHECK(inside[0] >= 158 && inside[0] <= 160);
	CHECK(outside[0] >= 63 && outside[0] <= 64);
	CHECK(inside[1] > inside[0] + 4);
	CHECK(outside[1] < outside[0] - 2);
	CHECK_EQ(inside[0], inside[2]);
	CHECK_EQ(outside[0], outside[2]);

	// A field with no edge in it has no detail to lift.
	DX11Post_Set_Clarity(0.75f);
	fixture.Get_Post().Begin_Frame();
	CHECK(clear_scene(fixture.Get_Device(), fixture.Get_Post(), 0.25f));
	CHECK(fixture.Get_Post().Run_Chain());
	unsigned pitch = 0;
	unsigned char * result = read_back_buffer(fixture.Get_Device(), pitch);
	CHECK(result != NULL);
	if (result != NULL) {
		const int flat = pixel_level(result, pitch, SURFACE_SIZE / 2, SURFACE_SIZE / 2);
		CHECK(flat >= outside[0] - 1 && flat <= outside[0] + 1);
		delete [] result;
	}

	DX11Post_Set_Clarity(0.0f);
	DX11Post_Set_Bloom(1.0f, 1.5f, 1.0f, 2);
}

TEST(dx11post_bloom_keeps_ordinary_levels_and_folds_overbright_to_white)
{
	DeviceFixture fixture;
	if (!fixture.Create()) {
		printf("skip: no Direct3D 11 device on this machine\n");
		return;
	}

	const DX11PostEffect chain[] = { DX11_POST_BLOOM };
	fixture.Get_Post().Set_Chain(chain, 1);
	CHECK(fixture.Get_Post().Scene_Is_Float());

	// Half of white is below the tone curve's knee and below the bright threshold, so it has to
	// come back out as half of white. If it does not, the curve is being applied to the whole range
	// and every ordinary pixel in the game has been moved.
	fixture.Get_Post().Begin_Frame();
	CHECK(clear_scene(fixture.Get_Device(), fixture.Get_Post(), 0.5f));
	CHECK(fixture.Get_Post().Run_Chain());
	unsigned pitch = 0;
	unsigned char * result = read_back_buffer(fixture.Get_Device(), pitch);
	CHECK(result != NULL);
	if (result != NULL) {
		const unsigned char level = pixel_level(result, pitch, SURFACE_SIZE / 2, SURFACE_SIZE / 2);
		CHECK(level >= 127 && level <= 128);
		delete [] result;
	}

	// Four times white is what an eight bit target could not have held at all. It arrives as white
	// rather than as anything darker, which is the curve reaching its asymptote.
	fixture.Get_Post().Begin_Frame();
	CHECK(clear_scene(fixture.Get_Device(), fixture.Get_Post(), 4.0f));
	CHECK(fixture.Get_Post().Run_Chain());
	result = read_back_buffer(fixture.Get_Device(), pitch);
	CHECK(result != NULL);
	if (result != NULL) {
		CHECK_EQ(255, (int)pixel_level(result, pitch, SURFACE_SIZE / 2, SURFACE_SIZE / 2));
		delete [] result;
	}
}

// The blue of one whole frame of colour through the chain.  pixel_level reads the first byte of a
// pixel, and the back buffer is blue first.
static int frame_blue(DeviceFixture & fixture, float red, float green, float blue, bool chain)
{
	fixture.Get_Post().Begin_Frame();
	ID3D11RenderTargetView * view = fixture.Get_Post().Scene_View();
	CHECK(view != NULL);
	const float colour[4] = { red, green, blue, 1.0f };
	fixture.Get_Device().Get_Context()->ClearRenderTargetView(view, colour);
	CHECK(chain ? fixture.Get_Post().Run_Chain() : fixture.Get_Post().Copy_Through());
	unsigned pitch = 0;
	unsigned char * result = read_back_buffer(fixture.Get_Device(), pitch);
	CHECK(result != NULL);
	const int level = (result != NULL) ? pixel_level(result, pitch, SURFACE_SIZE / 2, SURFACE_SIZE / 2) : -1;
	delete [] result;
	return level;
}

TEST(dx11post_internal_hdr_keeps_colour_past_white_and_moves_nothing_under_it)
{
	DeviceFixture fixture;
	if (!fixture.Create()) {
		printf("skip: no Direct3D 11 device on this machine\n");
		return;
	}

	const DX11PostEffect chain[] = { DX11_POST_BLOOM };
	fixture.Get_Post().Set_Chain(chain, 1);

	// A fire at eight times white, with the option off: every channel is past the curve's room, so
	// it arrives white, blue and all.  A pale wall under white is the off figure to hold.
	const int offFire = frame_blue(fixture, 8.0f, 4.0f, 1.6f, true);
	const int offWall = frame_blue(fixture, 0.95f, 0.95f, 0.9f, true);
	CHECK_EQ(255, offFire);

	DX11Post_Set_Internal_Hdr(true);

	// The same fire stays orange, a hotter one is whiter than it, and white-hot still reaches white.
	// Art under white comes out to the bit as it did.
	const int onFire = frame_blue(fixture, 8.0f, 4.0f, 1.6f, true);
	const int onHotter = frame_blue(fixture, 32.0f, 16.0f, 6.4f, true);
	CHECK(onFire < 160);
	CHECK(onHotter > onFire);
	CHECK(frame_blue(fixture, 64.0f, 64.0f, 64.0f, true) >= 250);
	CHECK_EQ(offWall, frame_blue(fixture, 0.95f, 0.95f, 0.9f, true));

	// The plain tone pass, with the Glow option off, takes the same curve.
	DX11Post_Set_Bloom(1.0f, 0.0f, 1.0f, 2);
	CHECK(frame_blue(fixture, 8.0f, 4.0f, 1.6f, true) < 160);
	DX11Post_Set_Bloom(1.0f, 1.5f, 1.0f, 2);

	// A frame with no match on it, a loading screen with its interface in the scene, keeps the old one.
	CHECK(frame_blue(fixture, 8.0f, 4.0f, 1.6f, false) >= 250);

	DX11Post_Set_Internal_Hdr(false);
	CHECK_EQ(offFire, frame_blue(fixture, 8.0f, 4.0f, 1.6f, true));
}

// Blue, green and red of one flat grey frame through the chain, the back buffer's own order.
static void frame_colour(DeviceFixture & fixture, float level, int colour[3])
{
	fixture.Get_Post().Begin_Frame();
	CHECK(clear_scene(fixture.Get_Device(), fixture.Get_Post(), level));
	CHECK(fixture.Get_Post().Run_Chain());
	unsigned pitch = 0;
	unsigned char * result = read_back_buffer(fixture.Get_Device(), pitch);
	CHECK(result != NULL);
	for (unsigned channel = 0; channel < 3; ++channel) {
		colour[channel] = (result != NULL)
			? pixel_channel(result, pitch, SURFACE_SIZE / 2, SURFACE_SIZE / 2, channel) : -1;
	}
	delete [] result;
}

static int level_gap(int a, int b) { return (a > b) ? a - b : b - a; }

TEST(dx11post_full_grade_holds_black_white_and_the_pivot_and_bends_the_rest)
{
	DeviceFixture fixture;
	if (!fixture.Create()) {
		printf("skip: no Direct3D 11 device on this machine\n");
		return;
	}

	const DX11PostEffect chain[] = { DX11_POST_BLOOM };
	fixture.Get_Post().Set_Chain(chain, 1);

	// Black, the grade's pivot grey (under the tone curve's knee, so it reaches the grade as 0.28),
	// a shade under it, a light over it, and four times white, which the curve brings to white.
	const float levels[5] = { 0.0f, 0.28f, 0.15f, 0.6f, 4.0f };
	int off[5][3];
	int on[5][3];
	for (unsigned index = 0; index < 5; ++index) {
		frame_colour(fixture, levels[index], off[index]);
	}
	DX11Post_Set_Grade(1.0f);
	for (unsigned index = 0; index < 5; ++index) {
		frame_colour(fixture, levels[index], on[index]);
	}
	DX11Post_Set_Grade(0.0f);

	// The S turns about black, white and the pivot, so red and green stay where they were.  Blue
	// takes the split toning alone, the shade pushed cool and the light warm by five hundredths of
	// how far each is from the pivot: a few levels at black and white, none at the pivot.
	const unsigned BLACK = 0, PIVOT = 1, SHADE = 2, LIGHT = 3, WHITE = 4;
	for (unsigned channel = 0; channel < 3; ++channel) {
		CHECK(level_gap(off[PIVOT][channel], on[PIVOT][channel]) <= 1);
	}
	for (unsigned channel = 1; channel < 3; ++channel) {
		CHECK(level_gap(off[BLACK][channel], on[BLACK][channel]) <= 1);
		CHECK(level_gap(off[WHITE][channel], on[WHITE][channel]) <= 1);
	}
	CHECK(level_gap(off[BLACK][CHANNEL_BLUE], on[BLACK][CHANNEL_BLUE]) <= 10);
	CHECK(level_gap(off[WHITE][CHANNEL_BLUE], on[WHITE][CHANNEL_BLUE]) <= 10);
	CHECK_EQ(0, off[BLACK][CHANNEL_RED]);
	CHECK_EQ(255, off[WHITE][CHANNEL_RED]);

	// And it is not a grade that does nothing: the shade sinks and the light rises.
	CHECK(on[SHADE][CHANNEL_RED] < off[SHADE][CHANNEL_RED] - 2);
	CHECK(on[LIGHT][CHANNEL_RED] > off[LIGHT][CHANNEL_RED] + 2);

	// Back to nought is the ungraded frame to the bit.
	int again[3];
	frame_colour(fixture, levels[LIGHT], again);
	for (unsigned channel = 0; channel < 3; ++channel) {
		CHECK_EQ(off[LIGHT][channel], again[channel]);
	}
}

TEST(dx11post_bloom_spreads_light_past_the_pixels_that_carry_it)
{
	DeviceFixture fixture;
	if (!fixture.Create()) {
		printf("skip: no Direct3D 11 device on this machine\n");
		return;
	}

	const DX11PostEffect chain[] = { DX11_POST_BLOOM };
	fixture.Get_Post().Set_Chain(chain, 1);
	fixture.Get_Post().Begin_Frame();
	CHECK(clear_scene(fixture.Get_Device(), fixture.Get_Post(), 0.0f));
	CHECK(write_bright_block(fixture.Get_Device(), fixture.Get_Post()));
	CHECK(fixture.Get_Post().Run_Chain());

	unsigned pitch = 0;
	unsigned char * result = read_back_buffer(fixture.Get_Device(), pitch);
	CHECK(result != NULL);
	if (result != NULL) {
		// Ten pixels above the block there was nothing at all in the scene. The glow reaches it,
		// which is the whole of what bloom is; without the bright pass or the blur this is zero.
		// "near" is a macro in windows.h, which is why this is not called that.
		const unsigned char aboveTheBlock = pixel_level(result, pitch, SURFACE_SIZE / 2,
			SURFACE_SIZE / 2 - 10);
		CHECK(aboveTheBlock > 0);

		// The opposite corner is far outside the blur's reach and has to stay black, or the glow is
		// not a glow, it is a wash over the frame.
		CHECK_EQ(0, (int)pixel_level(result, pitch, 1, 1));
		delete [] result;
	}
}

TEST(dx11post_resolves_once_a_frame)
{
	DeviceFixture fixture;
	if (!fixture.Create()) {
		printf("skip: no Direct3D 11 device on this machine\n");
		return;
	}

	unsigned char * first = new unsigned char[SURFACE_SIZE * SURFACE_SIZE * BYTES_A_PIXEL];
	unsigned char * second = new unsigned char[SURFACE_SIZE * SURFACE_SIZE * BYTES_A_PIXEL];
	fill_flat(first, LIGHT_LEVEL);
	fill_flat(second, DARK_LEVEL);

	const DX11PostEffect chain[] = { DX11_POST_COPY };
	unsigned pitch = 0;
	unsigned char * result = run_chain(fixture, chain, 1, first, pitch);
	CHECK(result != NULL);
	delete [] result;

	// A second finish without a Begin_Frame between them is the screenshot path asking for a frame
	// the present path has already finished. It has to do nothing, or the shot and the screen would
	// be two different pictures.
	CHECK(write_scene(fixture.Get_Device(), fixture.Get_Post(), second));
	CHECK(!fixture.Get_Post().Run_Chain());
	CHECK(!fixture.Get_Post().Copy_Through());
	result = read_back_buffer(fixture.Get_Device(), pitch);
	CHECK(result != NULL);
	if (result != NULL) {
		CHECK_EQ(LIGHT_LEVEL, pixel_level(result, pitch, SURFACE_SIZE / 2, SURFACE_SIZE / 2));
		delete [] result;
	}

	// Armed again, the new picture goes through.
	fixture.Get_Post().Begin_Frame();
	CHECK(fixture.Get_Post().Run_Chain());
	result = read_back_buffer(fixture.Get_Device(), pitch);
	CHECK(result != NULL);
	if (result != NULL) {
		CHECK_EQ(DARK_LEVEL, pixel_level(result, pitch, SURFACE_SIZE / 2, SURFACE_SIZE / 2));
		delete [] result;
	}

	delete [] first;
	delete [] second;
}

TEST(dx11post_the_scene_takes_its_occlusion_once_a_frame_and_the_chain_then_skips_it)
{
	DeviceFixture fixture;
	if (!fixture.Create()) {
		printf("skip: no Direct3D 11 device on this machine\n");
		return;
	}
	DX11DeviceClass & device = fixture.Get_Device();
	DX11PostProcessClass & post = fixture.Get_Post();

	// Nothing to do without ao in the chain.
	const DX11PostEffect copy_only[] = { DX11_POST_COPY };
	post.Set_Chain(copy_only, 1);
	post.Open_Frame();
	post.Begin_Frame();
	device.Set_Scene_View(post.Scene_View());
	CHECK(!post.Occlude_Scene(device.Get_Depth_Texture()));

	// With it, once a frame, and the chain of ao alone still brings the scene to the screen.  The
	// depth is all far plane, so nothing is occluded and the multiply has to give every pixel back.
	unsigned char * picture = new unsigned char[SURFACE_SIZE * SURFACE_SIZE * BYTES_A_PIXEL];
	fill_diagonal(picture);
	const DX11PostEffect ao_only[] = { DX11_POST_AO };
	post.Set_Chain(ao_only, 1);
	post.Open_Frame();
	post.Begin_Frame();
	device.Set_Scene_View(post.Scene_View());
	device.Get_Context()->ClearDepthStencilView(device.Get_Depth_Stencil_View(), D3D11_CLEAR_DEPTH,
		1.0f, 0);
	CHECK(write_scene(device, post, picture));
	CHECK(post.Occlude_Scene(device.Get_Depth_Texture()));
	CHECK(!post.Occlude_Scene(device.Get_Depth_Texture()));
	CHECK(post.Run_Chain());
	unsigned pitch = 0;
	unsigned char * result = read_back_buffer(device, pitch);
	CHECK(result != NULL);
	if (result != NULL) {
		CHECK_EQ(0u, differing_pixels(picture, result, pitch));
		delete [] result;
	}

	// A Begin_Frame alone (a mirror's scene, then the frame's own) does not arm it again; the top of
	// the next frame does.
	post.Begin_Frame();
	CHECK(!post.Occlude_Scene(device.Get_Depth_Texture()));
	post.Open_Frame();
	CHECK(post.Occlude_Scene(device.Get_Depth_Texture()));

	device.Set_Scene_View(NULL);
	delete [] picture;
}

// The ground haze, white over a black scene, drawn for real: the program compiles, finds its constants
// where the pass writes them and lays the layer over the ground.  The clip to world matrix is the
// identity, so a pixel's world x and y are its own clip x and y and the depth, cleared to a half, is
// its height: under the haze's top of one.
TEST(dx11post_the_ground_haze_lies_over_a_scene_under_its_top)
{
	DeviceFixture fixture;
	if (!fixture.Create()) {
		printf("skip: no Direct3D 11 device on this machine\n");
		return;
	}

	const float HAZE_TOP = 1.0f;
	const float HAZE_THICKNESS = 1.0f;
	const float HAZE_STRENGTH = 1.0f;
	const float HAZE_REACH = 1000.0f;
	const float NO_WATER = -1.0e6f;
	const float SCENE_DEPTH = 0.5f;
	const unsigned char LIFTED_BY_AT_LEAST = 16;
	DX11DeviceClass & device = fixture.Get_Device();
	DX11PostProcessClass & post = fixture.Get_Post();

	const float white[3] = { 1.0f, 1.0f, 1.0f };
	const float overhead[3] = { 0.0f, 0.0f, 1.0f };
	DX11Post_Set_Haze(white, HAZE_TOP, HAZE_THICKNESS, HAZE_STRENGTH, HAZE_REACH, NO_WATER, overhead, 0.0f);

	unsigned char picture[SURFACE_SIZE * SURFACE_SIZE * BYTES_A_PIXEL];
	fill_flat(picture, DARK_LEVEL);
	const DX11PostEffect copy_only[] = { DX11_POST_COPY };
	post.Set_Chain(copy_only, 1);
	post.Compile_Haze_Program();
	post.Open_Frame();
	post.Begin_Frame();
	device.Set_Scene_View(post.Scene_View());
	device.Get_Context()->ClearDepthStencilView(device.Get_Depth_Stencil_View(), D3D11_CLEAR_DEPTH,
		SCENE_DEPTH, 0);
	const float identity[16] = {
		1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f };
	const bool drawn = write_scene(device, post, picture)
		&& post.Haze_Scene(device.Get_Depth_Texture(), identity) && post.Run_Chain();
	device.Set_Scene_View(NULL);
	DX11Post_Set_Haze(white, 0.0f, HAZE_THICKNESS, 0.0f, HAZE_REACH, NO_WATER, overhead, 0.0f);

	CHECK(drawn);
	unsigned pitch = 0;
	unsigned char * result = drawn ? read_back_buffer(device, pitch) : NULL;
	CHECK(result != NULL);
	if (result != NULL) {
		CHECK(pixel_level(result, pitch, SURFACE_SIZE / 2, SURFACE_SIZE / 2) >= DARK_LEVEL + LIFTED_BY_AT_LEAST);
		delete [] result;
	}
}

// The Atmosphere option drawn for real, as the haze's test is.  With depth at a half the ground lies half
// a unit from the eye, a third of the way into a fade of scale one, and takes the white horizon.  With
// depth at the far plane and the eye looking up the fade leaves the frame alone: it is sky by then.
// Looking down, the same empty depth takes the horizon, over the sea that draws past the terrain.  The sky
// itself reads no depth and covers everything, a null texture here, so it goes black.  Half a shroud cell
// past the map's edge the shroud's border is black, so the ground there is the horizon however near it is,
// unless the terrain says it draws land out there, which then fades over that land's reach.
TEST(dx11post_the_atmosphere_fades_the_far_ground_and_the_sky_fills_the_frame)
{
	DeviceFixture fixture;
	if (!fixture.Create()) {
		printf("skip: no Direct3D 11 device on this machine\n");
		return;
	}

	const unsigned char LIFTED_BY_AT_LEAST = 16;
	DX11DeviceClass & device = fixture.Get_Device();
	DX11PostProcessClass & post = fixture.Get_Post();
	const float white[3] = { 1.0f, 1.0f, 1.0f };
	const float identity[16] = {
		1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f };
	// The eye at height nought looks down its depth, its focus on the ground right there, so no start.
	// It stands 500 units into the map, or the map's corner closes the fade over the frame's centre.
	const float looking_down[16] = {
		1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.0f, 500.0f, 500.0f, 0.0f, 1.0f };
	DX11Post_Set_Atmosphere(white, white, 0.0f, 1.0f, 1.0f, 0.0f, 1.0e6f, 1.0e6f, NULL);

	unsigned char picture[SURFACE_SIZE * SURFACE_SIZE * BYTES_A_PIXEL];
	fill_flat(picture, DARK_LEVEL);
	const DX11PostEffect copy_only[] = { DX11_POST_COPY };
	post.Set_Chain(copy_only, 1);
	post.Compile_Atmosphere_Programs();

	const float DEPTHS[6] = { 0.5f, 1.0f, 0.5f, 1.0f, 0.5f, 0.5f };
	unsigned char levels[6] = { 0, 0, 0, 0, 0, 0 };
	for (unsigned run = 0; run < 6; ++run) {
		if (run == 4) {
			// A fade too long to count, and a map whose edge lies 25 units short of the eye.
			DX11Post_Set_Atmosphere(white, white, 0.0f, 1.0e6f, 1.0f, 0.0f, 475.0f, 475.0f, NULL);
		}
		if (run == 5) {
			// The same edge with land drawn a thousand units past it: 25 units out is still ground.
			DX11Post_Set_Atmosphere_Edge(0.0f, 1000.0f);
		}
		post.Open_Frame();
		post.Begin_Frame();
		device.Set_Scene_View(post.Scene_View());
		device.Get_Context()->ClearDepthStencilView(device.Get_Depth_Stencil_View(), D3D11_CLEAR_DEPTH,
			DEPTHS[run], 0);
		const bool drawn = write_scene(device, post, picture)
			&& (run == 2 ? post.Sky_Scene(identity)
				: post.Atmosphere_Scene(device.Get_Depth_Texture(), run == 1 ? identity : looking_down))
			&& post.Run_Chain();
		device.Set_Scene_View(NULL);
		CHECK(drawn);
		unsigned pitch = 0;
		unsigned char * result = drawn ? read_back_buffer(device, pitch) : NULL;
		CHECK(result != NULL);
		if (result != NULL) {
			levels[run] = pixel_level(result, pitch, SURFACE_SIZE / 2, SURFACE_SIZE / 2);
			delete [] result;
		}
	}
	DX11Post_Set_Atmosphere(white, white, 0.0f, 1.0f, 0.0f, 0.0f, 1.0e6f, 1.0e6f, NULL);
	DX11Post_Set_Atmosphere_Edge(0.0f, 0.0f);

	CHECK(levels[0] >= DARK_LEVEL + LIFTED_BY_AT_LEAST);
	CHECK(levels[0] < 70);
	CHECK(levels[1] == DARK_LEVEL);
	CHECK(levels[2] == 0);
	CHECK(levels[3] >= DARK_LEVEL + LIFTED_BY_AT_LEAST);
	// All of the horizon's colour, dimmed below the horizon by BELOW_DIM, 0.55 of white.
	CHECK(levels[4] >= 130);
	CHECK(levels[5] < DARK_LEVEL + LIFTED_BY_AT_LEAST);

	// Strength zero is no pass at all.
	post.Open_Frame();
	CHECK(!post.Atmosphere_Scene(device.Get_Depth_Texture(), identity));
}

// The Ambient Occlusion option drawn for real over a white scene.  The world is the clip space times
// forty across and a thousand deep, so the frame is 80 units across at 0.8 of a pixel a unit, the left
// half stands 10 units nearer the eye than the right, and the near plane, which a tap past the frame's
// edge reads, is out of reach.  The floor at that step's foot goes dark; the floor past the reach and
// the top of the step stay exactly white, and the chain's own "ao" stands down.
TEST(dx11post_world_occlusion_darkens_the_foot_of_a_step_and_nothing_else)
{
	DeviceFixture fixture;
	if (!fixture.Create()) {
		printf("skip: no Direct3D 11 device on this machine\n");
		return;
	}

	const float FLOOR_DEPTH = 0.5f;
	const float TOP_DEPTH = 0.49f;
	const float UNITS_DEEP = 1000.0f;
	const float UNITS_A_CLIP = 40.0f;
	const float PROJECTION_SCALE = 1.0f / UNITS_A_CLIP;
	const unsigned char DARKENED_BY_AT_LEAST = 10;
	DX11DeviceClass & device = fixture.Get_Device();
	DX11PostProcessClass & post = fixture.Get_Post();

	float depth[SURFACE_SIZE * SURFACE_SIZE];
	for (unsigned y = 0; y < SURFACE_SIZE; ++y) {
		for (unsigned x = 0; x < SURFACE_SIZE; ++x) {
			depth[y * SURFACE_SIZE + x] = (x < SURFACE_SIZE / 2) ? TOP_DEPTH : FLOOR_DEPTH;
		}
	}
	D3D11_TEXTURE2D_DESC description;
	ZeroMemory(&description, sizeof(description));
	description.Width = SURFACE_SIZE;
	description.Height = SURFACE_SIZE;
	description.MipLevels = 1;
	description.ArraySize = 1;
	description.Format = DXGI_FORMAT_R32_FLOAT;
	description.SampleDesc.Count = 1;
	description.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	D3D11_SUBRESOURCE_DATA initial;
	ZeroMemory(&initial, sizeof(initial));
	initial.pSysMem = depth;
	initial.SysMemPitch = SURFACE_SIZE * sizeof(float);
	ID3D11Texture2D * texture = NULL;
	ID3D11ShaderResourceView * view = NULL;
	CHECK(SUCCEEDED(device.Get_Device()->CreateTexture2D(&description, &initial, &texture)));
	CHECK(SUCCEEDED(device.Get_Device()->CreateShaderResourceView(texture, NULL, &view)));

	unsigned char picture[SURFACE_SIZE * SURFACE_SIZE * BYTES_A_PIXEL];
	fill_flat(picture, LIGHT_LEVEL);
	const DX11PostEffect ao_only[] = { DX11_POST_AO };
	post.Set_Chain(ao_only, 1);
	post.Compile_World_Ao_Programs();
	post.Open_Frame();
	post.Begin_Frame();
	device.Set_Scene_View(post.Scene_View());
	const float world_from_clip[16] = {
		UNITS_A_CLIP, 0.0f, 0.0f, 0.0f, 0.0f, UNITS_A_CLIP, 0.0f, 0.0f, 0.0f, 0.0f, UNITS_DEEP, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f };
	const bool drawn = write_scene(device, post, picture)
		&& post.World_Occlude_Scene(view, world_from_clip, PROJECTION_SCALE, PROJECTION_SCALE);
	CHECK(drawn);
	CHECK(!post.Occlude_Scene(device.Get_Depth_Texture()));
	CHECK(post.Run_Chain());
	device.Set_Scene_View(NULL);

	unsigned pitch = 0;
	unsigned char * result = drawn ? read_back_buffer(device, pitch) : NULL;
	CHECK(result != NULL);
	if (result != NULL) {
		CHECK(pixel_level(result, pitch, SURFACE_SIZE / 2 + 2, SURFACE_SIZE / 2) <= LIGHT_LEVEL - DARKENED_BY_AT_LEAST);
		CHECK_EQ((unsigned)LIGHT_LEVEL, (unsigned)pixel_level(result, pitch, SURFACE_SIZE - 3, SURFACE_SIZE / 2));
		CHECK_EQ((unsigned)LIGHT_LEVEL, (unsigned)pixel_level(result, pitch, 3, SURFACE_SIZE / 2));
		delete [] result;
	}
	view->Release();
	texture->Release();
}

// A texture the passes read, every texel given.  The caller releases both.
static bool make_view(DX11DeviceClass & device, DXGI_FORMAT format, unsigned width, unsigned height,
	const void * texels, unsigned pitch, ID3D11Texture2D ** texture, ID3D11ShaderResourceView ** view)
{
	D3D11_TEXTURE2D_DESC description;
	ZeroMemory(&description, sizeof(description));
	description.Width = width;
	description.Height = height;
	description.MipLevels = 1;
	description.ArraySize = 1;
	description.Format = format;
	description.SampleDesc.Count = 1;
	description.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	D3D11_SUBRESOURCE_DATA initial;
	ZeroMemory(&initial, sizeof(initial));
	initial.pSysMem = texels;
	initial.SysMemPitch = pitch;
	*texture = NULL;
	*view = NULL;
	return SUCCEEDED(device.Get_Device()->CreateTexture2D(&description, &initial, texture))
		&& SUCCEEDED(device.Get_Device()->CreateShaderResourceView(*texture, NULL, view));
}

// One frame of the SunShafts pass over a grey scene: whether it drew, the middle row and the middle
// column of what reached the screen, and how many pixels differ from the scene.  Heights, when given, is
// a 64 square byte map laid over world x and y from -40 to 40 (0.8 cells a unit, the border 32 cells), a
// quarter unit a byte.
static unsigned char ShaftColumn[SURFACE_SIZE];
static const float SHAFT_HEIGHTS_MAPPING[3] = { 0.8f, 32.0f, 0.25f };
static bool shaft_frame(DeviceFixture & fixture, const unsigned char * picture, ID3D11ShaderResourceView * depth,
	const float world_from_clip[16], ID3D11ShaderResourceView * sun, const float sun_from_world[16],
	const DX11PostSunShafts & shafts, ID3D11ShaderResourceView * shroud, const float shroud_rect[4],
	unsigned char row[SURFACE_SIZE], unsigned & changed, float shadow_strength = 1.0f,
	ID3D11ShaderResourceView * heights = NULL)
{
	memset(ShaftColumn, 0, sizeof(ShaftColumn));
	DX11DeviceClass & device = fixture.Get_Device();
	DX11PostProcessClass & post = fixture.Get_Post();
	const float PROJECTION_SCALE = 1.0f / 40.0f;
	DX11Post_Set_Sun_Shafts(shafts);
	DX11Post_Set_Haze_Shroud(shroud, shroud_rect);
	post.Open_Frame();
	post.Begin_Frame();
	device.Set_Scene_View(post.Scene_View());
	const bool written = write_scene(device, post, picture);
	const bool drew = written && post.Sun_Shafts_Scene(depth, world_from_clip, sun, sun_from_world, 0.001f,
		shadow_strength, PROJECTION_SCALE, PROJECTION_SCALE, heights, SHAFT_HEIGHTS_MAPPING);
	CHECK(written && post.Run_Chain());
	device.Set_Scene_View(NULL);
	memset(row, 0, SURFACE_SIZE);
	changed = SURFACE_SIZE * SURFACE_SIZE;
	unsigned pitch = 0;
	unsigned char * result = written ? read_back_buffer(device, pitch) : NULL;
	CHECK(result != NULL);
	if (result != NULL) {
		for (unsigned x = 0; x < SURFACE_SIZE; ++x) {
			row[x] = pixel_level(result, pitch, x, SURFACE_SIZE / 2);
			ShaftColumn[x] = pixel_level(result, pitch, SURFACE_SIZE / 2, x);
		}
		changed = differing_pixels(picture, result, pitch);
		delete [] result;
	}
	return drew;
}

// The SunShafts option drawn for real over a mid grey scene.  The level camera looks straight down from
// 100 units onto ground at 0 (the clip space times forty across, its depth of a half the ground) through a
// layer of dust 50 units deep; the tilted one looks the same way along -x, its rays half as steep again.
// The test sun stands slanted over +x: a point at height z lands on the map where the ground x + z (x + 2z
// for the tilted camera) does, at a sun depth of a half less z over 200, so a map texel of 0.25 is a
// caster 50 units up that shades everything under it along its slant.  Its ground bearing runs along x, so
// the lift looks across it along y, where a world unit is 0.8 of a map texel (row 31.5 - 0.8 y).
//
// The lift, on the gap map (shade for 10 <= |y| <= 30, a lane 20 units wide down the middle): the
// lane's middle row takes the band; a shaded surface never takes any, however strong, the row next to
// each shadow's edge included; the side map (shade for y <= -10 only) gives a one-sided border, a share
// of the gap's; with a radius of 20 the lit ground more than 20 units (and the blur's reach) from the
// shade is exactly as it was; a trench along the bearing with no caster takes none once the terrain's
// heights are there; a block's sunlit top in the lane takes none; the lift goes with the strength in
// proportion, no key past its bound draws more than the owner's most, and the sum stops at the hard
// ceiling whatever the sun's colour.  Its numbers are worked by hand through the band's smoothstep, the
// blur's weights with the shaded taps left out and the composite's bilinear, so each range is an
// estimate a few levels wide.  Streaks are off (width nought, a flat half) so the numbers hold.
//
// The dimming, on the dark map: lit ground seen through shaded air dims, ground in the caster's own shadow
// does not dim again; the dimming stops at a share of the shadow's strength; on a map with both the lane
// and the caster the dimming and the lift add up apart, neither cancels the other.  Strength zero and an
// all lit map change nothing; a water area at 40 takes everything under its surface out; a black shroud
// changes nothing, and neither does a pixel over the black shroud whose ray crossed visible shaded air; a
// shallow ray toward the sun's bearing takes the glare, and away from it none.
// The trench's ground height at a world y.
static float trench_height(float y)
{
	const float across = (y < 0.0f) ? -y : y;
	return (across <= 10.0f) ? 0.0f : (across >= 12.0f) ? 30.0f : (across - 10.0f) * 15.0f;
}

TEST(dx11post_sun_shafts_light_the_gap_between_shadows_dim_shaded_air_and_leave_open_sun_shade_and_shroud_alone)
{
	DeviceFixture fixture;
	if (!fixture.Create()) {
		printf("skip: no Direct3D 11 device on this machine\n");
		return;
	}

	const unsigned char GREY = 128;
	const unsigned char MOVED_BY_AT_LEAST = 12;
	const unsigned LEFT = 8;
	const unsigned RIGHT = SURFACE_SIZE - 8;
	DX11DeviceClass & device = fixture.Get_Device();
	DX11PostProcessClass & post = fixture.Get_Post();

	// the caster stripes, by where on the ground they shade: x' = x + z for the level camera's sun, whose
	// clip x is x' / 100, and x + 2z for the tilted one's, x' / 300
	const float CASTER = 0.25f;
	const float TALL_CASTER = 0.1f;
	float depth[SURFACE_SIZE * SURFACE_SIZE];
	float dark_map[SURFACE_SIZE * SURFACE_SIZE];
	float gap_map[SURFACE_SIZE * SURFACE_SIZE];
	float side_map[SURFACE_SIZE * SURFACE_SIZE];
	float both_map[SURFACE_SIZE * SURFACE_SIZE];
	float tilted_map[SURFACE_SIZE * SURFACE_SIZE];
	float all_lit[SURFACE_SIZE * SURFACE_SIZE];
	// A trench running along the sun's bearing with no caster in it: ground at 0 for |y| < 10, rising 15 a
	// unit to 30 at |y| = 12 and flat beyond, seen by the camera (trench_depth), in the sun's map (the
	// terrain itself is the first thing each sun ray meets) and in the heights.  And a block 6 units tall
	// standing in the gap map's lane, cells 28 to 35 across and rows 29 to 35 down (world x -4.4 to 4.4, y
	// -4.4 to 3.1), its top at sun map cells 32 to 35, rows 29 to 35.
	float trench_depth[SURFACE_SIZE * SURFACE_SIZE];
	float trench_map[SURFACE_SIZE * SURFACE_SIZE];
	float box_depth[SURFACE_SIZE * SURFACE_SIZE];
	float box_map[SURFACE_SIZE * SURFACE_SIZE];
	unsigned char trench_heights[SURFACE_SIZE * SURFACE_SIZE];
	unsigned char flat_heights[SURFACE_SIZE * SURFACE_SIZE];
	for (unsigned y = 0; y < SURFACE_SIZE; ++y) {
		// the world y the sun's map row, the screen row and the heights row stand for
		const float row_y = (31.5f - (float)y) / 0.8f;
		const float screen_y = 40.0f * (1.0f - ((float)y + 0.5f) / 32.0f);
		const float heights_y = ((float)y - 32.0f) / 0.8f;
		const float across = (row_y < 0.0f) ? -row_y : row_y;
		const bool gap_shade = across >= 10.0f && across <= 30.0f;
		for (unsigned x = 0; x < SURFACE_SIZE; ++x) {
			const float clip = ((float)x + 0.5f) / (float)SURFACE_SIZE * 2.0f - 1.0f;
			const float level_x = clip * 100.0f;
			const float tilted_x = clip * 300.0f;
			const bool caster = level_x >= 0.0f && level_x <= 60.0f;
			const unsigned at = y * SURFACE_SIZE + x;
			depth[at] = 0.5f;
			dark_map[at] = caster ? CASTER : 1.0f;
			gap_map[at] = gap_shade ? TALL_CASTER : 1.0f;
			side_map[at] = (row_y <= -10.0f) ? TALL_CASTER : 1.0f;
			both_map[at] = gap_shade ? TALL_CASTER : caster ? CASTER : 1.0f;
			tilted_map[at] = (tilted_x >= 60.0f) ? CASTER : 1.0f;
			all_lit[at] = 1.0f;
			trench_depth[at] = 0.5f - trench_height(screen_y) / 200.0f;
			trench_map[at] = 0.5f - trench_height(row_y) / 200.0f;
			trench_heights[at] = (unsigned char)(trench_height(heights_y) / 0.25f + 0.5f);
			flat_heights[at] = 0;
			const bool block_seen = x >= 28 && x <= 35 && y >= 29 && y <= 35;
			const bool block_in_sun = x >= 32 && x <= 35 && y >= 29 && y <= 35;
			box_depth[at] = block_seen ? 0.5f - 6.0f / 200.0f : 0.5f;
			box_map[at] = block_in_sun ? 0.5f - 6.0f / 200.0f : gap_map[at];
		}
	}
	// the shroud black over world x under -5 and clear over x past 5
	unsigned char shroud_edge[8 * 4];
	for (unsigned x = 0; x < 8; ++x) {
		const unsigned char level = (x < 4) ? 0 : 255;
		shroud_edge[x * 4] = shroud_edge[x * 4 + 1] = shroud_edge[x * 4 + 2] = level;
		shroud_edge[x * 4 + 3] = 255;
	}
	const unsigned char black[4] = { 0, 0, 0, 255 };
	const unsigned VIEWS = 15;
	ID3D11Texture2D * textures[VIEWS];
	ID3D11ShaderResourceView * views[VIEWS];
	memset(textures, 0, sizeof(textures));
	memset(views, 0, sizeof(views));
	const unsigned MAP_PITCH = SURFACE_SIZE * sizeof(float);
	CHECK(make_view(device, DXGI_FORMAT_R32_FLOAT, SURFACE_SIZE, SURFACE_SIZE, depth, MAP_PITCH, &textures[0], &views[0]));
	CHECK(make_view(device, DXGI_FORMAT_R32_FLOAT, SURFACE_SIZE, SURFACE_SIZE, dark_map, MAP_PITCH, &textures[1], &views[1]));
	CHECK(make_view(device, DXGI_FORMAT_R32_FLOAT, SURFACE_SIZE, SURFACE_SIZE, tilted_map, MAP_PITCH, &textures[2], &views[2]));
	CHECK(make_view(device, DXGI_FORMAT_R32_FLOAT, SURFACE_SIZE, SURFACE_SIZE, all_lit, MAP_PITCH, &textures[3], &views[3]));
	CHECK(make_view(device, DXGI_FORMAT_R8G8B8A8_UNORM, 1, 1, black, 4, &textures[4], &views[4]));
	CHECK(make_view(device, DXGI_FORMAT_R8G8B8A8_UNORM, 8, 1, shroud_edge, 8 * 4, &textures[5], &views[5]));
	CHECK(make_view(device, DXGI_FORMAT_R32_FLOAT, SURFACE_SIZE, SURFACE_SIZE, gap_map, MAP_PITCH, &textures[6], &views[6]));
	CHECK(make_view(device, DXGI_FORMAT_R32_FLOAT, SURFACE_SIZE, SURFACE_SIZE, side_map, MAP_PITCH, &textures[7], &views[7]));
	CHECK(make_view(device, DXGI_FORMAT_R32_FLOAT, SURFACE_SIZE, SURFACE_SIZE, both_map, MAP_PITCH, &textures[8], &views[8]));
	ID3D11ShaderResourceView * const depth_view = views[0];
	ID3D11ShaderResourceView * const gap_view = views[6];
	ID3D11ShaderResourceView * const side_view = views[7];
	ID3D11ShaderResourceView * const both_view = views[8];
	CHECK(make_view(device, DXGI_FORMAT_R32_FLOAT, SURFACE_SIZE, SURFACE_SIZE, trench_depth, MAP_PITCH, &textures[9], &views[9]));
	CHECK(make_view(device, DXGI_FORMAT_R32_FLOAT, SURFACE_SIZE, SURFACE_SIZE, trench_map, MAP_PITCH, &textures[10], &views[10]));
	CHECK(make_view(device, DXGI_FORMAT_R8_UNORM, SURFACE_SIZE, SURFACE_SIZE, trench_heights, SURFACE_SIZE, &textures[11], &views[11]));
	CHECK(make_view(device, DXGI_FORMAT_R8_UNORM, SURFACE_SIZE, SURFACE_SIZE, flat_heights, SURFACE_SIZE, &textures[12], &views[12]));
	CHECK(make_view(device, DXGI_FORMAT_R32_FLOAT, SURFACE_SIZE, SURFACE_SIZE, box_depth, MAP_PITCH, &textures[13], &views[13]));
	CHECK(make_view(device, DXGI_FORMAT_R32_FLOAT, SURFACE_SIZE, SURFACE_SIZE, box_map, MAP_PITCH, &textures[14], &views[14]));
	ID3D11ShaderResourceView * const trench_depth_view = views[9];
	ID3D11ShaderResourceView * const trench_view = views[10];
	ID3D11ShaderResourceView * const trench_heights_view = views[11];
	ID3D11ShaderResourceView * const flat = views[12];
	ID3D11ShaderResourceView * const box_depth_view = views[13];
	ID3D11ShaderResourceView * const box_view = views[14];

	// level: world = (40 x, 40 y, 100 - 200 depth); tilted: world = (200 + 40 x - 400 depth, 40 y, 100 - 200 depth)
	const float level[16] = {
		40.0f, 0.0f, 0.0f, 0.0f, 0.0f, 40.0f, 0.0f, 0.0f, 0.0f, 0.0f, -200.0f, 0.0f, 0.0f, 0.0f, 100.0f, 1.0f };
	const float tilted[16] = {
		40.0f, 0.0f, 0.0f, 0.0f, 0.0f, 40.0f, 0.0f, 0.0f, -400.0f, 0.0f, -200.0f, 0.0f, 200.0f, 0.0f, 100.0f, 1.0f };
	// the sun's clip: ((x + z) / 100 or (x + 2z) / 300, y / 40, a half less height over 200)
	const float sun_level[16] = {
		1.0f / 100.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f / 40.0f, 0.0f, 0.0f, 1.0f / 100.0f, 0.0f, -1.0f / 200.0f, 0.0f, 0.0f, 0.0f, 0.5f, 1.0f };
	const float sun_tilted[16] = {
		1.0f / 300.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f / 40.0f, 0.0f, 0.0f, 2.0f / 300.0f, 0.0f, -1.0f / 200.0f, 0.0f, 0.0f, 0.0f, 0.5f, 1.0f };
	const float shroud_rect[4] = { -40.0f, -40.0f, 1.0f / 80.0f, 1.0f / 80.0f };

	DX11PostSunShafts shafts;
	memset(&shafts, 0, sizeof(shafts));
	shafts.Colour[0] = shafts.Colour[1] = shafts.Colour[2] = 1.0f;
	shafts.Strength = 1.0f;
	shafts.Ground = 0.0f;
	shafts.Top = 50.0f;
	shafts.Falloff = 60.0f;
	shafts.Samples = 12.0f;
	shafts.Scale = 1.0f / 25.0f;
	shafts.Dark = 0.25f;
	shafts.Band = 0.0f;
	shafts.Radius = 60.0f;
	shafts.Side = 0.3f;
	shafts.StreakWidth = 0.0f;
	shafts.StreakLength = 100.0f;
	shafts.Slack = 8.0f;
	shafts.DarkCap = 0.35f;
	shafts.ShadedMost = 1.0f;
	shafts.Floor = 0.0f;
	shafts.NormalOffset = 2.0f;
	shafts.TowardSun[0] = 1.0f;
	shafts.ShallowTop = 0.6f;
	shafts.ShallowSpan = 0.3f;

	unsigned char picture[SURFACE_SIZE * SURFACE_SIZE * BYTES_A_PIXEL];
	fill_flat(picture, GREY);
	const DX11PostEffect copy_only[] = { DX11_POST_COPY };
	post.Set_Chain(copy_only, 1);
	post.Compile_Sun_Shaft_Programs();
	post.Compile_World_Ao_Programs();

	unsigned char row[SURFACE_SIZE];
	unsigned changed = 0;

	// the left half's lit ground seen through the caster's slanted shade dims; the right half's ground lies in
	// the caster's own shadow and is not dimmed again
	CHECK(shaft_frame(fixture, picture, depth_view, level, views[1], sun_level, shafts, NULL, shroud_rect, row, changed));
	const unsigned char shaded_left = row[LEFT];
	CHECK(shaded_left <= GREY - MOVED_BY_AT_LEAST);
	CHECK_EQ((unsigned)GREY, (unsigned)row[RIGHT]);

	// With the receivers' shadow at a tenth, the dimming stops at 0.35 of that: 4.5 levels off the grey.
	CHECK(shaft_frame(fixture, picture, depth_view, level, views[1], sun_level, shafts, NULL, shroud_rect, row, changed, 0.1f));
	CHECK(row[LEFT] >= 120);
	CHECK(row[LEFT] > shaded_left);
	CHECK(row[LEFT] < GREY);

	// The gap, with the terrain's heights flat at nought, so each caster stands 80 units over the ground:
	// the lane's middle (y about -0.6, 10 units from the shade on each side) takes the nearer side's
	// 1 - smoothstep(10 / 60) = 0.92 of the band, about 0.87 once the blur has mixed in the rows nearer one
	// wall, times 0.12 and the plain streak's 0.5: about +13 levels.
	DX11PostSunShafts gap = shafts;
	gap.Dark = 0.0f;
	gap.Band = 0.12f;
	CHECK(shaft_frame(fixture, picture, depth_view, level, gap_view, sun_level, gap, NULL, shroud_rect, row, changed, 1.0f, flat));
	const int gap_lift = (int)row[SURFACE_SIZE / 2] - (int)GREY;
	CHECK(gap_lift >= 9);
	CHECK(gap_lift <= 18);

	// Linear in the strength: at a half, half the lift.
	DX11PostSunShafts half_gap = gap;
	half_gap.Strength = 0.5f;
	CHECK(shaft_frame(fixture, picture, depth_view, level, gap_view, sun_level, half_gap, NULL, shroud_rect, row, changed, 1.0f, flat));
	const int half_lift = (int)row[SURFACE_SIZE / 2] - (int)GREY;
	CHECK(half_lift * 2 - gap_lift <= 2);
	CHECK(gap_lift - half_lift * 2 <= 2);

	// A shaded surface takes nothing, however strong the band: the column's rows 12 to 23 and 40 to 51 lie
	// in the two strips of shade (y 24 down to 10.6 and -10.6 down to -25), rows 23 and 40 the first ones
	// past each edge, which the full size gate holds at exactly the grey whatever their lit neighbours hold.
	DX11PostSunShafts strong = gap;
	strong.Side = 1.0f;
	strong.Strength = 1.3f;
	CHECK(shaft_frame(fixture, picture, depth_view, level, gap_view, sun_level, strong, NULL, shroud_rect, row, changed, 1.0f, flat));
	CHECK(changed > 0u);
	for (unsigned y = 12; y <= 51; ++y) {
		if (y <= 23 || y >= 40) {
			CHECK_EQ((unsigned)GREY, (unsigned)ShaftColumn[y]);
		}
	}

	// The side map: the same row, 10 units from shade on one side only, takes Side (0.3) of the square of
	// its nearness, about 0.82 after the blur, times 0.12 and 0.5: about +4, under the gap's.
	CHECK(shaft_frame(fixture, picture, depth_view, level, side_view, sun_level, gap, NULL, shroud_rect, row, changed, 1.0f, flat));
	const int side_lift = (int)row[SURFACE_SIZE / 2] - (int)GREY;
	CHECK(side_lift >= 2);
	CHECK(side_lift <= gap_lift - 6);

	// The band fades to exactly nought at the radius.  At 20, on the side map, the column's rows 0 to 14 (y
	// 22 and up) read no half size pixel whose blur reaches ground within 20 units of the shade; on the gap
	// map the lane's middle, 10 units from each wall, keeps about +4.
	DX11PostSunShafts near_only = gap;
	near_only.Radius = 20.0f;
	CHECK(shaft_frame(fixture, picture, depth_view, level, side_view, sun_level, near_only, NULL, shroud_rect, row, changed, 1.0f, flat));
	for (unsigned y = 0; y <= 14; ++y) {
		CHECK_EQ((unsigned)GREY, (unsigned)ShaftColumn[y]);
	}
	CHECK(shaft_frame(fixture, picture, depth_view, level, gap_view, sun_level, near_only, NULL, shroud_rect, row, changed, 1.0f, flat));
	CHECK((int)row[SURFACE_SIZE / 2] - (int)GREY >= 2);
	CHECK((int)row[SURFACE_SIZE / 2] - (int)GREY < gap_lift);

	// Rising ground is no caster.  The trench's walls stand nearer the sun than its floor by far more than
	// the slack, so without the heights its floor takes a gap's band, the fault; with them each wall is
	// taken back to the world, found standing on the ground it is, and the whole frame is as it was.
	CHECK(shaft_frame(fixture, picture, trench_depth_view, level, trench_view, sun_level, gap, NULL, shroud_rect, row, changed));
	CHECK((int)row[SURFACE_SIZE / 2] - (int)GREY >= 5);
	CHECK(shaft_frame(fixture, picture, trench_depth_view, level, trench_view, sun_level, strong, NULL, shroud_rect, row, changed,
		1.0f, trench_heights_view));
	CHECK_EQ(0u, changed);

	// Only the ground takes the lift.  A block 6 units tall in the lane, its top in full sun: with the
	// heights its top stands 6 over the ground and takes nothing; without them its flat top passes for
	// ground and takes the lane's band.
	CHECK(shaft_frame(fixture, picture, box_depth_view, level, box_view, sun_level, strong, NULL, shroud_rect, row, changed, 1.0f, flat));
	CHECK_EQ((unsigned)GREY, (unsigned)row[SURFACE_SIZE / 2]);
	CHECK(shaft_frame(fixture, picture, box_depth_view, level, box_view, sun_level, strong, NULL, shroud_rect, row, changed));
	CHECK((int)row[SURFACE_SIZE / 2] - (int)GREY >= 4);

	// The dimming survives beside a lift: on the map with both the lane and the caster, the left half's lane
	// takes the band and the shaded air in front of it, and the two add up apart, the dimmed grey plus the lift.
	DX11PostSunShafts lift_only = gap;
	CHECK(shaft_frame(fixture, picture, depth_view, level, both_view, sun_level, lift_only, NULL, shroud_rect, row, changed, 1.0f, flat));
	const int lifted = (int)row[LEFT];
	DX11PostSunShafts dim_only = shafts;
	CHECK(shaft_frame(fixture, picture, depth_view, level, both_view, sun_level, dim_only, NULL, shroud_rect, row, changed, 1.0f, flat));
	const int dimmed = (int)row[LEFT];
	DX11PostSunShafts lift_and_dim = gap;
	lift_and_dim.Dark = shafts.Dark;
	CHECK(shaft_frame(fixture, picture, depth_view, level, both_view, sun_level, lift_and_dim, NULL, shroud_rect, row, changed, 1.0f, flat));
	const int together = (int)row[LEFT];
	CHECK(dimmed <= (int)GREY - MOVED_BY_AT_LEAST);
	CHECK(lifted >= (int)GREY + 8);
	CHECK(together <= lifted - 8);
	CHECK(together >= dimmed + 8);
	CHECK(together - (lifted + dimmed - (int)GREY) <= 2);
	CHECK((lifted + dimmed - (int)GREY) - together <= 2);

	// Open ground in full sun stays exactly as it was under a tall layer down to a low floor, with every
	// term on.
	DX11PostSunShafts open_sun = strong;
	open_sun.Dark = 0.25f;
	open_sun.Top = 150.0f;
	open_sun.Floor = -100.0f;
	open_sun.Samples = 32.0f;
	CHECK(shaft_frame(fixture, picture, depth_view, level, views[3], sun_level, open_sun, NULL, shroud_rect, row, changed, 1.0f, flat));
	CHECK_EQ(0u, changed);

	// Nonsense from a tuning key is held by the hard bounds.  The band, the strength, the one-sided share and
	// the radius at any height draw exactly the owner's most (band 0.12, strength 1.3, share 0.3, radius 60):
	// the gap at about 1.3 times 0.052, +17.  With the glare at its top as well, on the tilted camera looking
	// along the sun's bearing at full shallowness, the sum passes the ceiling and stops at 0.14 of the sun's
	// colour: 164, and a sun colour of three times white stays there too.  And the dimming stops at 0.35 of
	// the shadow, 0.3 here: 114 or 115.
	DX11PostSunShafts most = gap;
	most.Strength = 1.3f;
	CHECK(shaft_frame(fixture, picture, depth_view, level, gap_view, sun_level, most, NULL, shroud_rect, row, changed, 1.0f, flat));
	const int most_lift = (int)row[SURFACE_SIZE / 2] - (int)GREY;
	CHECK(most_lift >= 13);
	CHECK(most_lift <= 22);
	DX11PostSunShafts wild = gap;
	wild.Band = 50.0f;
	wild.Strength = 50.0f;
	wild.Side = 50.0f;
	wild.Radius = 500.0f;
	CHECK(shaft_frame(fixture, picture, depth_view, level, gap_view, sun_level, wild, NULL, shroud_rect, row, changed, 1.0f, flat));
	CHECK_EQ(most_lift, (int)row[SURFACE_SIZE / 2] - (int)GREY);
	wild.Glare = 10.0f;
	wild.TowardSun[0] = -1.0f;
	wild.ShallowTop = 1.0f;
	wild.ShallowSpan = 0.1f;
	CHECK(shaft_frame(fixture, picture, depth_view, tilted, gap_view, sun_tilted, wild, NULL, shroud_rect, row, changed, 1.0f, flat));
	CHECK(row[SURFACE_SIZE / 2] >= 161);
	CHECK(row[SURFACE_SIZE / 2] <= 165);
	wild.Colour[0] = wild.Colour[1] = wild.Colour[2] = 3.0f;
	CHECK(shaft_frame(fixture, picture, depth_view, tilted, gap_view, sun_tilted, wild, NULL, shroud_rect, row, changed, 1.0f, flat));
	CHECK(row[SURFACE_SIZE / 2] >= 161);
	CHECK(row[SURFACE_SIZE / 2] <= 165);
	wild = shafts;
	wild.Dark = 50.0f;
	wild.DarkCap = 50.0f;
	wild.ShadedMost = 50.0f;
	wild.Strength = 50.0f;
	CHECK(shaft_frame(fixture, picture, depth_view, level, views[1], sun_level, wild, NULL, shroud_rect, row, changed, 0.3f));
	CHECK(row[LEFT] >= 112);
	CHECK(row[LEFT] <= 117);

	// open ground in full sun, at strength one and at strength nought
	CHECK(shaft_frame(fixture, picture, depth_view, level, views[3], sun_level, shafts, NULL, shroud_rect, row, changed));
	CHECK_EQ(0u, changed);
	DX11PostSunShafts off = shafts;
	off.Strength = 0.0f;
	CHECK(!shaft_frame(fixture, picture, depth_view, level, views[1], sun_level, off, NULL, shroud_rect, row, changed));
	CHECK_EQ(0u, changed);

	// a water surface at 40 over the left half: water takes nothing
	DX11PostSunShafts water = shafts;
	water.WaterCount = 1.0f;
	water.WaterBoxes[0][0] = -1000.0f;
	water.WaterBoxes[0][1] = -1000.0f;
	water.WaterBoxes[0][2] = 0.0f;
	water.WaterBoxes[0][3] = 1000.0f;
	water.WaterHeights[0] = 40.0f;
	CHECK(shaft_frame(fixture, picture, depth_view, level, views[1], sun_level, water, NULL, shroud_rect, row, changed));
	CHECK_EQ((unsigned)GREY, (unsigned)row[LEFT]);
	CHECK_EQ((unsigned)GREY, (unsigned)row[RIGHT]);
	// and no band over the water either
	water.WaterBoxes[0][2] = 1000.0f;
	water.Band = 0.12f;
	water.Side = 1.0f;
	CHECK(shaft_frame(fixture, picture, depth_view, level, gap_view, sun_level, water, NULL, shroud_rect, row, changed));
	CHECK_EQ(0u, changed);

	// all of it under a black shroud, band and dimming both
	CHECK(shaft_frame(fixture, picture, depth_view, level, both_view, sun_level, lift_and_dim, views[4], shroud_rect, row, changed));
	CHECK_EQ(0u, changed);

	// the tilted camera over the shroud's edge, its lit ground behind shaded air: pixel 14 ends on black
	// shroud at world x -21 after crossing clear air out to x 79, pixel 50 ends on clear ground at x 24
	CHECK(shaft_frame(fixture, picture, depth_view, tilted, views[2], sun_tilted, shafts, views[5], shroud_rect, row, changed));
	CHECK_EQ((unsigned)GREY, (unsigned)row[14]);
	CHECK(row[50] <= GREY - MOVED_BY_AT_LEAST);

	// The tilted camera looking along the sun's bearing takes the glare, looking away from it none.  0.25
	// stops at 0.08, and the ray (0.45 of straight down) is about half shallow enough: about +10.
	DX11PostSunShafts glare = shafts;
	glare.Glare = 0.25f;
	glare.TowardSun[0] = -1.0f;
	CHECK(shaft_frame(fixture, picture, depth_view, tilted, views[3], sun_tilted, glare, NULL, shroud_rect, row, changed));
	CHECK(row[SURFACE_SIZE / 2] >= GREY + 8);
	CHECK(row[SURFACE_SIZE / 2] <= GREY + 13);
	glare.TowardSun[0] = 1.0f;
	CHECK(shaft_frame(fixture, picture, depth_view, tilted, views[3], sun_tilted, glare, NULL, shroud_rect, row, changed));
	CHECK_EQ(0u, changed);

	// at full shallowness the glare is Glare of the sun's colour and no more: 0.05 is 13 levels
	glare.Glare = 0.05f;
	glare.TowardSun[0] = -1.0f;
	glare.ShallowTop = 1.0f;
	glare.ShallowSpan = 0.1f;
	CHECK(shaft_frame(fixture, picture, depth_view, tilted, views[3], sun_tilted, glare, NULL, shroud_rect, row, changed));
	CHECK(row[SURFACE_SIZE / 2] >= GREY + 11);
	CHECK(row[SURFACE_SIZE / 2] <= GREY + 14);

	DX11Post_Set_Sun_Shafts(off);
	DX11Post_Set_Haze_Shroud(NULL, shroud_rect);
	for (unsigned index = 0; index < VIEWS; ++index) {
		if (views[index] != NULL) {
			views[index]->Release();
		}
		if (textures[index] != NULL) {
			textures[index]->Release();
		}
	}
}
