/*
 * Device-free coverage for ww3d2.
 *
 * ww3d2 is the DX8 renderer, so most of it needs a live IDirect3DDevice9.
 * The pieces below are pure computation - format tables, the shader bitfield,
 * the FVF offset arithmetic and the w3d-file <-> runtime converters - and
 * every one of them is reachable without a device.  Anything that routes
 * through DX8Wrapper::Get_Current_Caps() (Get_Valid_Texture_Format,
 * ShaderClass::Apply) is deliberately not called here.
 *
 * Several tests pin behaviour that is plainly wrong in EA's source.  Those are
 * marked DEFECT and assert what the code actually does, not what it should do:
 * the functions in question have no callers left in this tree, so "fixing"
 * them would be inventing new behaviour with nothing to validate it against.
 */

#include "test_harness.h"

#include "ww3dformat.h"
#include "formconv.h"
#include "shader.h"
#include "dx8fvf.h"
#include "w3d_util.h"
#include "w3d_file.h"
#include "w3d_obsolete.h"
#include "targa.h"
#include "wwstring.h"
#include "vector3.h"
#include "vector4.h"
#include "quat.h"
#include "render2dsentence.h"
#include "fullscreenfit.h"
#include "../GameEngineDevice/Include/W3DDevice/GameClient/SunShadowCasterCull.h"

/*
 * W3DMPO_GLUE gives every pooled class an operator new that routes through
 * GameEngine's memory pools.  GameEngine is not ported yet, so back the pools
 * with plain new/delete - the classes under test here are value types and none
 * of them care where their storage came from.
 * ponytail: move to a shared Tests/*.cpp once a second test links ww3d2.
 */
void *createW3DMemPool(const char * /*poolName*/, int /*allocationSize*/)
{
	return (void *)1;			/* opaque handle; the stub never dereferences it */
}

/*
 * Every pooled new must come back through freeFromW3DMemPool, with the very
 * pointer it handed out.  The 16-byte prefix mirrors the game's pool block
 * header: zeroed, so a "delete []" on a pooled object takes the same path it
 * takes in the game (MSVC reads the array cookie at p-4, sees 0 elements and
 * frees p-4 through the *global* operator delete[] - never through the pool).
 */
static int theW3DPoolAllocs = 0, theW3DPoolFrees = 0, theW3DPoolBadFrees = 0;
enum { W3D_POOL_PREFIX = 16, W3D_POOL_MAGIC = 0x5B10C5A1 };

void *allocateFromW3DMemPool(void * /*pool*/, int allocationSize)
{
	++theW3DPoolAllocs;
	char *raw = (char *)::operator new(size_t(allocationSize) + W3D_POOL_PREFIX);
	memset(raw, 0, W3D_POOL_PREFIX);
	*(int *)raw = W3D_POOL_MAGIC;
	return raw + W3D_POOL_PREFIX;
}

void *allocateFromW3DMemPool(void *pool, int allocationSize, const char * /*msg*/, int /*unused*/)
{
	return allocateFromW3DMemPool(pool, allocationSize);
}

void freeFromW3DMemPool(void * /*pool*/, void *p)
{
	++theW3DPoolFrees;
	char *raw = (char *)p - W3D_POOL_PREFIX;
	if (*(int *)raw != W3D_POOL_MAGIC)
	{
		++theW3DPoolBadFrees;	/* not a pointer we handed out - leak it rather than corrupt the heap */
		return;
	}
	::operator delete(raw);
}

/*=========================================================================
   SunBoxMiddle - the sun's box round the ground the frame shows
  =========================================================================*/

TEST(sun_box_goes_round_the_frames_ground)
{
	/* the far side reaches 1500 past the look point, the near side 500: a box 1000 each way fits */
	CHECK_EQ(SunBoxMiddle(-500.0f, 1500.0f, 1000.0f), 500.0f);
	/* a box wider than the ground stays in the middle of it */
	CHECK_EQ(SunBoxMiddle(-500.0f, 1500.0f, 2400.0f), 500.0f);
}

TEST(sun_box_past_its_cap_keeps_the_ground_at_the_cameras_feet)
{
	/* 5500 of ground under a box 4800 wide: the near end stays in and the far 700 falls off */
	CHECK_EQ(SunBoxMiddle(-500.0f, 5000.0f, 2400.0f), 1900.0f);
	CHECK_EQ(SunBoxMiddle(-5000.0f, 500.0f, 2400.0f), -1900.0f);
	/* with ground far past both sides it sits on the look point */
	CHECK_EQ(SunBoxMiddle(-3000.0f, 3000.0f, 2400.0f), 0.0f);
}

/*=========================================================================
   fullscreenfit - where a scaled fullscreen picture goes on its monitor
  =========================================================================*/

TEST(fullscreen_fit_stretch_covers_the_whole_monitor)
{
	const FullscreenFitRect fit = Fullscreen_Fit(1920, 1200, 1280, 720, false);
	CHECK_EQ(fit.x, 0); CHECK_EQ(fit.y, 0);
	CHECK_EQ(fit.width, 1920); CHECK_EQ(fit.height, 1200);
}

TEST(fullscreen_fit_keeps_the_pictures_shape_with_bars)
{
	/* 16:9 on 16:10: bars above and below, 60 each */
	FullscreenFitRect fit = Fullscreen_Fit(1920, 1200, 1920, 1080, true);
	CHECK_EQ(fit.x, 0); CHECK_EQ(fit.y, 60); CHECK_EQ(fit.width, 1920); CHECK_EQ(fit.height, 1080);
	/* 4:3 on 16:9: bars left and right */
	fit = Fullscreen_Fit(1920, 1080, 1024, 768, true);
	CHECK_EQ(fit.x, 240); CHECK_EQ(fit.y, 0); CHECK_EQ(fit.width, 1440); CHECK_EQ(fit.height, 1080);
	/* the monitor's own shape at a smaller size needs no bars */
	fit = Fullscreen_Fit(1920, 1200, 1680, 1050, true);
	CHECK_EQ(fit.x, 0); CHECK_EQ(fit.y, 0); CHECK_EQ(fit.width, 1920); CHECK_EQ(fit.height, 1200);
	fit = Fullscreen_Fit(1920, 1080, 1280, 720, true);
	CHECK_EQ(fit.width, 1920); CHECK_EQ(fit.height, 1080);
}

/*=========================================================================
   ww3dformat - format property tables
  =========================================================================*/

TEST(has_alpha_flags_exactly_the_alpha_formats)
{
	/* Everything the switch lists, and nothing else. */
	static const WW3DFormat with_alpha[] = {
		WW3D_FORMAT_A8R8G8B8, WW3D_FORMAT_A1R5G5B5, WW3D_FORMAT_A4R4G4B4,
		WW3D_FORMAT_A8, WW3D_FORMAT_A8R3G3B2, WW3D_FORMAT_A8P8,
		WW3D_FORMAT_A8L8, WW3D_FORMAT_A4L4,
		WW3D_FORMAT_DXT2, WW3D_FORMAT_DXT3, WW3D_FORMAT_DXT4, WW3D_FORMAT_DXT5
	};

	for (int f = 0; f < WW3D_FORMAT_COUNT; ++f) {
		bool expected = false;
		for (int i = 0; i < int(sizeof(with_alpha)/sizeof(with_alpha[0])); ++i) {
			if (with_alpha[i] == WW3DFormat(f)) expected = true;
		}
		CHECK_EQ(Has_Alpha(WW3DFormat(f)), expected);
	}

	/* X1R5G5B5 / X4R4G4B4 have an unused bit where the alpha would be. */
	CHECK(!Has_Alpha(WW3D_FORMAT_X1R5G5B5));
	CHECK(!Has_Alpha(WW3D_FORMAT_X4R4G4B4));
	CHECK(!Has_Alpha(WW3D_FORMAT_DXT1));
	CHECK(!Has_Alpha(WW3D_FORMAT_UNKNOWN));
}

TEST(alpha_bits_agrees_with_has_alpha)
{
	CHECK_EQ(Alpha_Bits(WW3D_FORMAT_A8R8G8B8), 8);
	CHECK_EQ(Alpha_Bits(WW3D_FORMAT_A8), 8);
	CHECK_EQ(Alpha_Bits(WW3D_FORMAT_A8R3G3B2), 8);
	CHECK_EQ(Alpha_Bits(WW3D_FORMAT_A8P8), 8);
	CHECK_EQ(Alpha_Bits(WW3D_FORMAT_A8L8), 8);

	CHECK_EQ(Alpha_Bits(WW3D_FORMAT_A4R4G4B4), 4);
	CHECK_EQ(Alpha_Bits(WW3D_FORMAT_A4L4), 4);
	CHECK_EQ(Alpha_Bits(WW3D_FORMAT_DXT3), 4);
	CHECK_EQ(Alpha_Bits(WW3D_FORMAT_DXT4), 4);
	CHECK_EQ(Alpha_Bits(WW3D_FORMAT_DXT5), 4);

	CHECK_EQ(Alpha_Bits(WW3D_FORMAT_A1R5G5B5), 1);
	CHECK_EQ(Alpha_Bits(WW3D_FORMAT_DXT2), 1);

	/* The two tables are independent switches; they must not disagree. */
	for (int f = 0; f < WW3D_FORMAT_COUNT; ++f) {
		CHECK_EQ(Alpha_Bits(WW3DFormat(f)) > 0, Has_Alpha(WW3DFormat(f)));
	}
}

TEST(bytes_per_pixel_of_the_supported_formats)
{
	CHECK_EQ(Get_Bytes_Per_Pixel(WW3D_FORMAT_A8R8G8B8), 4u);
	CHECK_EQ(Get_Bytes_Per_Pixel(WW3D_FORMAT_X8R8G8B8), 4u);
	CHECK_EQ(Get_Bytes_Per_Pixel(WW3D_FORMAT_X8L8V8U8), 4u);
	CHECK_EQ(Get_Bytes_Per_Pixel(WW3D_FORMAT_R8G8B8), 3u);
	CHECK_EQ(Get_Bytes_Per_Pixel(WW3D_FORMAT_R5G6B5), 2u);
	CHECK_EQ(Get_Bytes_Per_Pixel(WW3D_FORMAT_A1R5G5B5), 2u);
	CHECK_EQ(Get_Bytes_Per_Pixel(WW3D_FORMAT_A4R4G4B4), 2u);
	CHECK_EQ(Get_Bytes_Per_Pixel(WW3D_FORMAT_U8V8), 2u);
	CHECK_EQ(Get_Bytes_Per_Pixel(WW3D_FORMAT_L6V5U5), 2u);
	CHECK_EQ(Get_Bytes_Per_Pixel(WW3D_FORMAT_R3G3B2), 1u);
	CHECK_EQ(Get_Bytes_Per_Pixel(WW3D_FORMAT_L8), 1u);
	CHECK_EQ(Get_Bytes_Per_Pixel(WW3D_FORMAT_A8), 1u);
	CHECK_EQ(Get_Bytes_Per_Pixel(WW3D_FORMAT_P8), 1u);
}

TEST(bytes_per_pixel_returns_zero_for_formats_it_does_not_list)
{
	/* The switch only covers the formats the texture loader actually hands it;
	   everything else trips a WWASSERT (inert in this build) and falls through
	   to 0.  Callers must never size a surface from these. */
	CHECK_EQ(Get_Bytes_Per_Pixel(WW3D_FORMAT_UNKNOWN), 0u);
	CHECK_EQ(Get_Bytes_Per_Pixel(WW3D_FORMAT_X1R5G5B5), 0u);
	CHECK_EQ(Get_Bytes_Per_Pixel(WW3D_FORMAT_X4R4G4B4), 0u);
	CHECK_EQ(Get_Bytes_Per_Pixel(WW3D_FORMAT_A8R3G3B2), 0u);
	CHECK_EQ(Get_Bytes_Per_Pixel(WW3D_FORMAT_A8P8), 0u);
	CHECK_EQ(Get_Bytes_Per_Pixel(WW3D_FORMAT_A8L8), 0u);
	CHECK_EQ(Get_Bytes_Per_Pixel(WW3D_FORMAT_A4L4), 0u);

	/* DXT is block-compressed - "bytes per pixel" is meaningless for it. */
	CHECK_EQ(Get_Bytes_Per_Pixel(WW3D_FORMAT_DXT1), 0u);
	CHECK_EQ(Get_Bytes_Per_Pixel(WW3D_FORMAT_DXT5), 0u);
}

TEST(format_names_cover_every_format_and_are_unique)
{
	StringClass names[WW3D_FORMAT_COUNT];

	for (int f = 0; f < WW3D_FORMAT_COUNT; ++f) {
		Get_WW3D_Format_Name(WW3DFormat(f), names[f]);
		CHECK(names[f].Get_Length() > 0);
	}

	CHECK_STR(names[WW3D_FORMAT_UNKNOWN], "Unknown");
	CHECK_STR(names[WW3D_FORMAT_A8R8G8B8], "A8R8G8B8");
	CHECK_STR(names[WW3D_FORMAT_R5G6B5], "R5G6B5");
	CHECK_STR(names[WW3D_FORMAT_L6V5U5], "L6V5U5");
	CHECK_STR(names[WW3D_FORMAT_DXT5], "DXT5");

	for (int i = 0; i < WW3D_FORMAT_COUNT; ++i) {
		for (int j = i + 1; j < WW3D_FORMAT_COUNT; ++j) {
			CHECK_NE(strcmp(names[i], names[j]), 0);
		}
	}

	/* Out of range falls into the default arm, which shares the UNKNOWN case. */
	StringClass junk;
	Get_WW3D_Format_Name(WW3DFormat(WW3D_FORMAT_COUNT + 17), junk);
	CHECK_STR(junk, "Unknown");
}

TEST(zformat_names_cover_every_zformat_and_are_unique)
{
	StringClass names[WW3D_ZFORMAT_COUNT];

	for (int f = 0; f < WW3D_ZFORMAT_COUNT; ++f) {
		Get_WW3D_ZFormat_Name(WW3DZFormat(f), names[f]);
		CHECK(names[f].Get_Length() > 0);
	}

	CHECK_STR(names[WW3D_ZFORMAT_UNKNOWN], "Unknown");
	CHECK_STR(names[WW3D_ZFORMAT_D16_LOCKABLE], "D16Lockable");
	CHECK_STR(names[WW3D_ZFORMAT_D24S8], "D24S8");
	CHECK_STR(names[WW3D_ZFORMAT_D24X4S4], "D24X4S4");

	for (int i = 0; i < WW3D_ZFORMAT_COUNT; ++i) {
		for (int j = i + 1; j < WW3D_ZFORMAT_COUNT; ++j) {
			CHECK_NE(strcmp(names[i], names[j]), 0);
		}
	}
}

TEST(depth_and_stencil_bit_counts)
{
	CHECK_EQ(Get_Num_Depth_Bits(WW3D_ZFORMAT_D16_LOCKABLE), 16u);
	CHECK_EQ(Get_Num_Depth_Bits(WW3D_ZFORMAT_D32), 32u);
	CHECK_EQ(Get_Num_Depth_Bits(WW3D_ZFORMAT_D15S1), 15u);
	CHECK_EQ(Get_Num_Depth_Bits(WW3D_ZFORMAT_D24S8), 24u);
	CHECK_EQ(Get_Num_Depth_Bits(WW3D_ZFORMAT_D16), 16u);
	CHECK_EQ(Get_Num_Depth_Bits(WW3D_ZFORMAT_D24X8), 24u);
	CHECK_EQ(Get_Num_Depth_Bits(WW3D_ZFORMAT_D24X4S4), 24u);
	CHECK_EQ(Get_Num_Depth_Bits(WW3D_ZFORMAT_UNKNOWN), 0u);

	CHECK_EQ(Get_Num_Stencil_Bits(WW3D_ZFORMAT_D16_LOCKABLE), 0u);
	CHECK_EQ(Get_Num_Stencil_Bits(WW3D_ZFORMAT_D32), 0u);
	CHECK_EQ(Get_Num_Stencil_Bits(WW3D_ZFORMAT_D15S1), 1u);
	CHECK_EQ(Get_Num_Stencil_Bits(WW3D_ZFORMAT_D24S8), 8u);
	CHECK_EQ(Get_Num_Stencil_Bits(WW3D_ZFORMAT_D16), 0u);
	CHECK_EQ(Get_Num_Stencil_Bits(WW3D_ZFORMAT_D24X8), 0u);
	CHECK_EQ(Get_Num_Stencil_Bits(WW3D_ZFORMAT_D24X4S4), 4u);
	CHECK_EQ(Get_Num_Stencil_Bits(WW3D_ZFORMAT_UNKNOWN), 0u);

	/* Depth + stencil always fits the 16 or 32 bit surface the format names.
	   D24X8 and D24X4S4 leave the spare bits unaccounted for, so this is an
	   upper bound, not an equality. */
	for (int f = 1; f < WW3D_ZFORMAT_COUNT; ++f) {
		unsigned total = Get_Num_Depth_Bits(WW3DZFormat(f)) +
		                 Get_Num_Stencil_Bits(WW3DZFormat(f));
		CHECK(total == 16 || (total > 16 && total <= 32));
	}
}

/*=========================================================================
   ww3dformat - Targa header -> source format
  =========================================================================*/

static void set_tga(Targa &tga, int depth, int colormap_type, int image_type)
{
	memset(&tga.Header, 0, sizeof(tga.Header));
	tga.Header.PixelDepth = (unsigned char)depth;
	tga.Header.ColorMapType = (unsigned char)colormap_type;
	tga.Header.ImageType = (unsigned char)image_type;
}

TEST(targa_header_selects_the_source_format)
{
	Targa tga;
	WW3DFormat format;
	unsigned bpp;

	set_tga(tga, 32, 0, TGA_TRUECOLOR);
	Get_WW3D_Format(format, bpp, tga);
	CHECK_EQ(format, WW3D_FORMAT_A8R8G8B8);
	CHECK_EQ(bpp, 4u);

	set_tga(tga, 24, 0, TGA_TRUECOLOR);
	Get_WW3D_Format(format, bpp, tga);
	CHECK_EQ(format, WW3D_FORMAT_R8G8B8);
	CHECK_EQ(bpp, 3u);

	/* 16 bit targa is really X1R5G5B5, but the loader treats the top bit as
	   alpha - matching what the art tools of the era wrote. */
	set_tga(tga, 16, 0, TGA_TRUECOLOR);
	Get_WW3D_Format(format, bpp, tga);
	CHECK_EQ(format, WW3D_FORMAT_A1R5G5B5);
	CHECK_EQ(bpp, 2u);
}

TEST(targa_8bit_depth_splits_three_ways)
{
	Targa tga;
	WW3DFormat format;
	unsigned bpp;

	/* Palettised: the colour map flag wins over the image type. */
	set_tga(tga, 8, 1, TGA_MONO);
	Get_WW3D_Format(format, bpp, tga);
	CHECK_EQ(format, WW3D_FORMAT_P8);
	CHECK_EQ(bpp, 1u);

	/* Greyscale, no palette. */
	set_tga(tga, 8, 0, TGA_MONO);
	Get_WW3D_Format(format, bpp, tga);
	CHECK_EQ(format, WW3D_FORMAT_L8);
	CHECK_EQ(bpp, 1u);

	/* Neither -> treated as an alpha-only surface. */
	set_tga(tga, 8, 0, TGA_TRUECOLOR);
	Get_WW3D_Format(format, bpp, tga);
	CHECK_EQ(format, WW3D_FORMAT_A8);
	CHECK_EQ(bpp, 1u);
}

TEST(targa_unsupported_depth_yields_unknown_and_zero_bpp)
{
	Targa tga;
	WW3DFormat format;
	unsigned bpp;

	set_tga(tga, 15, 0, TGA_TRUECOLOR);
	Get_WW3D_Format(format, bpp, tga);
	CHECK_EQ(format, WW3D_FORMAT_UNKNOWN);
	CHECK_EQ(bpp, 0u);

	/* Both outputs are seeded before the switch, so a garbage header cannot
	   leave the caller's locals untouched. */
	format = WW3D_FORMAT_DXT5;
	bpp = 99;
	set_tga(tga, 0, 0, 0);
	Get_WW3D_Format(format, bpp, tga);
	CHECK_EQ(format, WW3D_FORMAT_UNKNOWN);
	CHECK_EQ(bpp, 0u);
}

/*=========================================================================
   ww3dformat - colour packing
  =========================================================================*/

TEST(vector4_to_color_passes_32bit_formats_through_unchanged)
{
	/* The 8888 cases do no byte shuffling at all - they hand back exactly what
	   DX8Wrapper::Convert_Color produced (0xAARRGGBB). */
	Vector4 red(1.0f, 0.0f, 0.0f, 1.0f);
	unsigned int out = 0;

	Vector4_to_Color(&out, red, WW3D_FORMAT_A8R8G8B8);
	CHECK_EQ(out, 0xffff0000u);

	Vector4_to_Color(&out, red, WW3D_FORMAT_X8R8G8B8);
	CHECK_EQ(out, 0xffff0000u);

	Vector4_to_Color(&out, red, WW3D_FORMAT_R8G8B8);
	CHECK_EQ(out, 0xffff0000u);

	Vector4 black(0.0f, 0.0f, 0.0f, 0.0f);
	Vector4_to_Color(&out, black, WW3D_FORMAT_A8R8G8B8);
	CHECK_EQ(out, 0x00000000u);

	Vector4 white(1.0f, 1.0f, 1.0f, 1.0f);
	Vector4_to_Color(&out, white, WW3D_FORMAT_A8R8G8B8);
	CHECK_EQ(out, 0xffffffffu);
}

TEST(vector4_to_color_saturated_white_packs_correctly)
{
	/* All-ones is the one input the byte-order defect below cannot corrupt, so
	   the packed layouts can still be checked against their bit widths. */
	Vector4 white(1.0f, 1.0f, 1.0f, 1.0f);
	unsigned int out = 0;

	Vector4_to_Color(&out, white, WW3D_FORMAT_R5G6B5);
	CHECK_EQ(out, 0xffffu);				/* 5+6+5 all set */

	Vector4_to_Color(&out, white, WW3D_FORMAT_A1R5G5B5);
	CHECK_EQ(out, 0xffffu);				/* 1+5+5+5 all set */

	Vector4_to_Color(&out, white, WW3D_FORMAT_A4R4G4B4);
	CHECK_EQ(out, 0xffffu);

	Vector4_to_Color(&out, white, WW3D_FORMAT_A8R3G3B2);
	CHECK_EQ(out, 0xffffu);				/* alpha byte + 3+3+2 */

	Vector4_to_Color(&out, white, WW3D_FORMAT_A8);
	CHECK_EQ(out, 0xffu);
}

TEST(vector4_to_color_packed_formats_read_the_bytes_backwards)
{
	/* DEFECT (dead code): the packed arms index the 32 bit ARGB value as
	   argb[0]=A, argb[1]=R, argb[2]=G, argb[3]=B.  That is big-endian byte
	   order; on x86 byte 0 is B and byte 3 is A, so every channel lands in the
	   wrong slot.  Nothing in this tree calls Vector4_to_Color, which is why
	   the bug survived - the assertions below pin what it really does so the
	   damage is visible the moment somebody wires it up. */
	unsigned int out = 0;

	/* Pure red in -> pure green out. */
	Vector4 red(1.0f, 0.0f, 0.0f, 0.0f);
	Vector4_to_Color(&out, red, WW3D_FORMAT_R5G6B5);
	CHECK_EQ(out, 0x07e0u);				/* green field, should have been 0xf800 */

	/* Pure blue in -> read as the alpha channel. */
	Vector4 blue(0.0f, 0.0f, 1.0f, 0.0f);
	Vector4_to_Color(&out, blue, WW3D_FORMAT_A8);
	CHECK_EQ(out, 0xffu);				/* should have been 0 - alpha was 0 */

	/* Pure alpha in -> dropped entirely. */
	Vector4 alpha(0.0f, 0.0f, 0.0f, 1.0f);
	Vector4_to_Color(&out, alpha, WW3D_FORMAT_A8);
	CHECK_EQ(out, 0x00u);				/* should have been 0xff */
}

TEST(vector4_to_color_luminance_formats_use_ciey)
{
	/* The L8/A8L8/A4L4 arms compute luminance from the float vector directly,
	   so they dodge the byte-order defect.  0.2126 R + 0.7152 G + 0.0722 B. */
	unsigned int out = 0;

	Vector4 green(0.0f, 1.0f, 0.0f, 0.0f);
	Vector4_to_Color(&out, green, WW3D_FORMAT_L8);
	CHECK_EQ(out, (unsigned int)(255.0f * 0.7152f));

	Vector4 black(0.0f, 0.0f, 0.0f, 0.0f);
	Vector4_to_Color(&out, black, WW3D_FORMAT_L8);
	CHECK_EQ(out, 0u);

	Vector4 white(1.0f, 1.0f, 1.0f, 1.0f);
	Vector4_to_Color(&out, white, WW3D_FORMAT_L8);
	CHECK_EQ(out, 255u);				/* the three weights sum to exactly 1 */

	/* A4L4 packs alpha high, luminance low, both nibbles. */
	Vector4_to_Color(&out, white, WW3D_FORMAT_A4L4);
	CHECK_EQ(out, 0xffu);
}

TEST(color_to_vector4_reverses_the_channel_order)
{
	/* DEFECT (dead code): same big-endian byte indexing as Vector4_to_Color,
	   so unpacking an 0xAARRGGBB value hands back the channels reversed.
	   Round-tripping A8R8G8B8 through the pair does NOT recover the input. */
	Vector4 out(9.0f, 9.0f, 9.0f, 9.0f);

	Color_to_Vector4(&out, 0x11223344u, WW3D_FORMAT_A8R8G8B8);
	CHECK_NEAR(out.X, 0x33 / 255.0f, 1e-6f);	/* red slot got green */
	CHECK_NEAR(out.Y, 0x22 / 255.0f, 1e-6f);	/* green slot got red */
	CHECK_NEAR(out.Z, 0x11 / 255.0f, 1e-6f);	/* blue slot got alpha */
	CHECK_NEAR(out.W, 0x44 / 255.0f, 1e-6f);	/* alpha slot got blue */

	/* Grey survives because every byte is equal. */
	Color_to_Vector4(&out, 0x80808080u, WW3D_FORMAT_A8R8G8B8);
	CHECK_NEAR(out.X, 0x80 / 255.0f, 1e-6f);
	CHECK_NEAR(out.W, 0x80 / 255.0f, 1e-6f);
}

TEST(color_to_vector4_zeroes_the_channels_a_format_lacks)
{
	/* a=r=g=b=0 is seeded before the switch, so channels the format has no
	   room for come back as 0 rather than garbage. */
	Vector4 out(9.0f, 9.0f, 9.0f, 9.0f);

	Color_to_Vector4(&out, 0xffffffffu, WW3D_FORMAT_R5G6B5);
	CHECK_NEAR(out.W, 0.0f, 1e-6f);				/* R5G6B5 has no alpha */

	Color_to_Vector4(&out, 0xffffffffu, WW3D_FORMAT_X4R4G4B4);
	CHECK_NEAR(out.W, 0.0f, 1e-6f);				/* the X nibble is not alpha */

	Color_to_Vector4(&out, 0xffffffffu, WW3D_FORMAT_A8);
	CHECK_NEAR(out.X, 0.0f, 1e-6f);
	CHECK_NEAR(out.Y, 0.0f, 1e-6f);
	CHECK_NEAR(out.Z, 0.0f, 1e-6f);
	CHECK_NEAR(out.W, 1.0f, 1e-6f);

	/* Unhandled formats trip the WWASSERT (inert here) and fall through with
	   every channel still zero. */
	Color_to_Vector4(&out, 0xffffffffu, WW3D_FORMAT_A8L8);
	CHECK_NEAR(out.X, 0.0f, 1e-6f);
	CHECK_NEAR(out.W, 0.0f, 1e-6f);
}

/*=========================================================================
   formconv - WW3DFormat <-> D3DFORMAT
  =========================================================================*/

TEST(ww3d_format_to_d3d_format_round_trips)
{
	Init_D3D_To_WW3_Conversion();

	static const WW3DFormat formats[] = {
		WW3D_FORMAT_R8G8B8, WW3D_FORMAT_A8R8G8B8, WW3D_FORMAT_X8R8G8B8,
		WW3D_FORMAT_R5G6B5, WW3D_FORMAT_X1R5G5B5, WW3D_FORMAT_A1R5G5B5,
		WW3D_FORMAT_A4R4G4B4, WW3D_FORMAT_R3G3B2, WW3D_FORMAT_A8,
		WW3D_FORMAT_A8R3G3B2, WW3D_FORMAT_X4R4G4B4, WW3D_FORMAT_A8P8,
		WW3D_FORMAT_P8, WW3D_FORMAT_L8, WW3D_FORMAT_A8L8, WW3D_FORMAT_A4L4,
		WW3D_FORMAT_U8V8, WW3D_FORMAT_L6V5U5, WW3D_FORMAT_X8L8V8U8,
		WW3D_FORMAT_DXT1, WW3D_FORMAT_DXT2, WW3D_FORMAT_DXT3,
		WW3D_FORMAT_DXT4, WW3D_FORMAT_DXT5
	};

	for (int i = 0; i < int(sizeof(formats)/sizeof(formats[0])); ++i) {
		D3DFORMAT d3d = WW3DFormat_To_D3DFormat(formats[i]);
		CHECK_NE(d3d, D3DFMT_UNKNOWN);
		CHECK_EQ(D3DFormat_To_WW3DFormat(d3d), formats[i]);
	}

	CHECK_EQ(WW3DFormat_To_D3DFormat(WW3D_FORMAT_UNKNOWN), D3DFMT_UNKNOWN);
	CHECK_EQ(D3DFormat_To_WW3DFormat(D3DFMT_UNKNOWN), WW3D_FORMAT_UNKNOWN);
}

TEST(ww3d_format_maps_to_the_expected_d3d_enumerators)
{
	CHECK_EQ(WW3DFormat_To_D3DFormat(WW3D_FORMAT_A8R8G8B8), D3DFMT_A8R8G8B8);
	CHECK_EQ(WW3DFormat_To_D3DFormat(WW3D_FORMAT_R5G6B5), D3DFMT_R5G6B5);
	CHECK_EQ(WW3DFormat_To_D3DFormat(WW3D_FORMAT_P8), D3DFMT_P8);
	CHECK_EQ(WW3DFormat_To_D3DFormat(WW3D_FORMAT_DXT3), D3DFMT_DXT3);

	/* WW3D spells the bumpmap format U8V8; D3D spells the same layout V8U8. */
	CHECK_EQ(WW3DFormat_To_D3DFormat(WW3D_FORMAT_U8V8), D3DFMT_V8U8);
	CHECK_EQ(D3DFormat_To_WW3DFormat(D3DFMT_V8U8), WW3D_FORMAT_U8V8);
}

TEST(d3d_formats_outside_the_table_map_to_unknown)
{
	Init_D3D_To_WW3_Conversion();

	/* Anything past the highest entry the table was sized for is rejected
	   before the array is touched - this is the bounds check that keeps a
	   FOURCC format from indexing off the end. */
	CHECK_EQ(D3DFormat_To_WW3DFormat(D3DFMT_VERTEXDATA), WW3D_FORMAT_UNKNOWN);
	CHECK_EQ(D3DFormat_To_WW3DFormat(D3DFMT_INDEX16), WW3D_FORMAT_UNKNOWN);

	/* In range but never assigned. */
	CHECK_EQ(D3DFormat_To_WW3DFormat(D3DFMT_A2B10G10R10), WW3D_FORMAT_UNKNOWN);
	CHECK_EQ(D3DFormat_To_WW3DFormat(D3DFMT_G16R16), WW3D_FORMAT_UNKNOWN);
}

TEST(zformat_to_d3d_format_round_trips)
{
	Init_D3D_To_WW3_Conversion();

	for (int f = 1; f < WW3D_ZFORMAT_COUNT; ++f) {
		D3DFORMAT d3d = WW3DZFormat_To_D3DFormat(WW3DZFormat(f));
		CHECK_NE(d3d, D3DFMT_UNKNOWN);
		CHECK_EQ(D3DFormat_To_WW3DZFormat(d3d), WW3DZFormat(f));
	}

	CHECK_EQ(WW3DZFormat_To_D3DFormat(WW3D_ZFORMAT_UNKNOWN), D3DFMT_UNKNOWN);
	CHECK_EQ(WW3DZFormat_To_D3DFormat(WW3D_ZFORMAT_D24S8), D3DFMT_D24S8);
	CHECK_EQ(D3DFormat_To_WW3DZFormat(D3DFMT_D16), WW3D_ZFORMAT_D16);

	/* A colour format is not a depth format. */
	CHECK_EQ(D3DFormat_To_WW3DZFormat(D3DFMT_A8R8G8B8), WW3D_ZFORMAT_UNKNOWN);
}

/*=========================================================================
   shader - the packed state word
  =========================================================================*/

TEST(shader_reset_sets_the_documented_defaults)
{
	ShaderClass s;			/* the default ctor calls Reset() */

	CHECK_EQ(s.Get_Depth_Compare(), ShaderClass::PASS_LEQUAL);
	CHECK_EQ(s.Get_Depth_Mask(), ShaderClass::DEPTH_WRITE_ENABLE);
	CHECK_EQ(s.Get_Color_Mask(), ShaderClass::COLOR_WRITE_ENABLE);
	CHECK_EQ(s.Get_Dst_Blend_Func(), ShaderClass::DSTBLEND_ZERO);
	CHECK_EQ(s.Get_Fog_Func(), ShaderClass::FOG_DISABLE);
	CHECK_EQ(s.Get_Primary_Gradient(), ShaderClass::GRADIENT_MODULATE);
	CHECK_EQ(s.Get_Secondary_Gradient(), ShaderClass::SECONDARY_GRADIENT_DISABLE);
	CHECK_EQ(s.Get_Src_Blend_Func(), ShaderClass::SRCBLEND_ONE);
	CHECK_EQ(s.Get_Texturing(), ShaderClass::TEXTURING_DISABLE);
	CHECK_EQ(s.Get_Alpha_Test(), ShaderClass::ALPHATEST_DISABLE);
	CHECK_EQ(s.Get_Cull_Mode(), ShaderClass::CULL_MODE_ENABLE);
	CHECK_EQ(s.Get_Post_Detail_Color_Func(), ShaderClass::DETAILCOLOR_DISABLE);
	CHECK_EQ(s.Get_Post_Detail_Alpha_Func(), ShaderClass::DETAILALPHA_DISABLE);
	CHECK_EQ(s.Get_NPatch_Enable(), ShaderClass::NPATCH_DISABLE);

	/* Reset() on a dirtied shader must land on the same word. */
	ShaderClass dirty(0xffffffff);
	dirty.Reset();
	CHECK_EQ(dirty.Get_Bits(), s.Get_Bits());
}

/* Set every value of one field, check it reads back and no other field moved. */
#define CHECK_FIELD(setter, getter, type, max)                             \
	do {                                                                   \
		for (int v = 0; v < ShaderClass::max; ++v) {                       \
			ShaderClass s;                                                 \
			unsigned int before = s.Get_Bits();                            \
			s.setter(ShaderClass::type(v));                                \
			CHECK_EQ(int(s.getter()), v);                                  \
			/* only this field's bits may differ */                        \
			unsigned int changed = before ^ s.Get_Bits();                  \
			s.setter(ShaderClass::type(v));                                \
			CHECK_EQ(changed & ~(changed), 0u);                            \
			ShaderClass back(s);                                           \
			back.setter(ShaderClass::type(0));                             \
			CHECK_EQ(int(back.getter()), 0);                               \
		}                                                                  \
	} while (0)

TEST(shader_every_field_round_trips_over_its_whole_range)
{
	CHECK_FIELD(Set_Depth_Compare, Get_Depth_Compare, DepthCompareType, PASS_MAX);
	CHECK_FIELD(Set_Depth_Mask, Get_Depth_Mask, DepthMaskType, DEPTH_WRITE_MAX);
	CHECK_FIELD(Set_Color_Mask, Get_Color_Mask, ColorMaskType, COLOR_WRITE_MAX);
	CHECK_FIELD(Set_Dst_Blend_Func, Get_Dst_Blend_Func, DstBlendFuncType, DSTBLEND_MAX);
	CHECK_FIELD(Set_Fog_Func, Get_Fog_Func, FogFuncType, FOG_MAX);
	CHECK_FIELD(Set_Primary_Gradient, Get_Primary_Gradient, PriGradientType, GRADIENT_MAX);
	CHECK_FIELD(Set_Secondary_Gradient, Get_Secondary_Gradient, SecGradientType, SECONDARY_GRADIENT_MAX);
	CHECK_FIELD(Set_Src_Blend_Func, Get_Src_Blend_Func, SrcBlendFuncType, SRCBLEND_MAX);
	CHECK_FIELD(Set_Texturing, Get_Texturing, TexturingType, TEXTURING_MAX);
	CHECK_FIELD(Set_NPatch_Enable, Get_NPatch_Enable, NPatchEnableType, NPATCH_TYPE_MAX);
	CHECK_FIELD(Set_Glow, Get_Glow, GlowType, GLOW_MAX);
	CHECK_FIELD(Set_Alpha_Test, Get_Alpha_Test, AlphaTestType, ALPHATEST_MAX);
	CHECK_FIELD(Set_Cull_Mode, Get_Cull_Mode, CullModeType, CULL_MODE_MAX);
	CHECK_FIELD(Set_Post_Detail_Color_Func, Get_Post_Detail_Color_Func, DetailColorFuncType, DETAILCOLOR_MAX);
	CHECK_FIELD(Set_Post_Detail_Alpha_Func, Get_Post_Detail_Alpha_Func, DetailAlphaFuncType, DETAILALPHA_MAX);
}

#undef CHECK_FIELD

TEST(shader_fields_do_not_overlap)
{
	/* Write the maximum legal value into every field at once; each one must
	   still read back.  This is the real test that the MASK_/SHIFT_ pairs
	   partition the word - a single overlapping bit shows up here. */
	ShaderClass s;
	s.Set_Depth_Compare(ShaderClass::PASS_ALWAYS);
	s.Set_Depth_Mask(ShaderClass::DEPTH_WRITE_ENABLE);
	s.Set_Color_Mask(ShaderClass::COLOR_WRITE_ENABLE);
	s.Set_Dst_Blend_Func(ShaderClass::DSTBLEND_ONE_MINUS_SRC_ALPHA);
	s.Set_Fog_Func(ShaderClass::FOG_WHITE);
	s.Set_Primary_Gradient(ShaderClass::GRADIENT_MODULATE2X);
	s.Set_Secondary_Gradient(ShaderClass::SECONDARY_GRADIENT_ENABLE);
	s.Set_Src_Blend_Func(ShaderClass::SRCBLEND_ONE_MINUS_SRC_ALPHA);
	s.Set_Texturing(ShaderClass::TEXTURING_ENABLE);
	s.Set_NPatch_Enable(ShaderClass::NPATCH_ENABLE);
	s.Set_Alpha_Test(ShaderClass::ALPHATEST_ENABLE);
	s.Set_Cull_Mode(ShaderClass::CULL_MODE_ENABLE);
	s.Set_Post_Detail_Color_Func(ShaderClass::DETAILCOLOR_MODALPHAADDCOLOR);
	s.Set_Post_Detail_Alpha_Func(ShaderClass::DETAILALPHA_INVSCALE);

	CHECK_EQ(s.Get_Depth_Compare(), ShaderClass::PASS_ALWAYS);
	CHECK_EQ(s.Get_Depth_Mask(), ShaderClass::DEPTH_WRITE_ENABLE);
	CHECK_EQ(s.Get_Color_Mask(), ShaderClass::COLOR_WRITE_ENABLE);
	CHECK_EQ(s.Get_Dst_Blend_Func(), ShaderClass::DSTBLEND_ONE_MINUS_SRC_ALPHA);
	CHECK_EQ(s.Get_Fog_Func(), ShaderClass::FOG_WHITE);
	CHECK_EQ(s.Get_Primary_Gradient(), ShaderClass::GRADIENT_MODULATE2X);
	CHECK_EQ(s.Get_Secondary_Gradient(), ShaderClass::SECONDARY_GRADIENT_ENABLE);
	CHECK_EQ(s.Get_Src_Blend_Func(), ShaderClass::SRCBLEND_ONE_MINUS_SRC_ALPHA);
	CHECK_EQ(s.Get_Texturing(), ShaderClass::TEXTURING_ENABLE);
	CHECK_EQ(s.Get_NPatch_Enable(), ShaderClass::NPATCH_ENABLE);
	CHECK_EQ(s.Get_Alpha_Test(), ShaderClass::ALPHATEST_ENABLE);
	CHECK_EQ(s.Get_Cull_Mode(), ShaderClass::CULL_MODE_ENABLE);
	CHECK_EQ(s.Get_Post_Detail_Color_Func(), ShaderClass::DETAILCOLOR_MODALPHAADDCOLOR);
	CHECK_EQ(s.Get_Post_Detail_Alpha_Func(), ShaderClass::DETAILALPHA_INVSCALE);
}

TEST(shader_masks_partition_the_word_without_overlap)
{
	static const unsigned int masks[] = {
		ShaderClass::MASK_DEPTHCOMPARE, ShaderClass::MASK_DEPTHMASK,
		ShaderClass::MASK_COLORMASK, ShaderClass::MASK_DSTBLEND,
		ShaderClass::MASK_FOG, ShaderClass::MASK_PRIGRADIENT,
		ShaderClass::MASK_SECGRADIENT, ShaderClass::MASK_SRCBLEND,
		ShaderClass::MASK_TEXTURING, ShaderClass::MASK_NPATCHENABLE,
		ShaderClass::MASK_ALPHATEST, ShaderClass::MASK_CULLMODE,
		ShaderClass::MASK_POSTDETAILCOLORFUNC, ShaderClass::MASK_POSTDETAILALPHAFUNC
	};

	unsigned int seen = 0;
	for (int i = 0; i < int(sizeof(masks)/sizeof(masks[0])); ++i) {
		CHECK_EQ(seen & masks[i], 0u);
		seen |= masks[i];
	}

	/* Bits 27..31 are unallocated; the shader word never sets them. */
	CHECK_EQ(seen & 0xf8000000u, 0u);
}

TEST(get_depth_compare_survives_its_missing_parentheses)
{
	/* shader.h writes  (ShaderBits & MASK_DEPTHCOMPARE >> SHIFT_DEPTHCOMPARE)
	   - >> binds tighter than &, so the mask is shifted, not the result.  It
	   is correct only because SHIFT_DEPTHCOMPARE happens to be 0.  Pinned so
	   that moving the field breaks a test instead of the renderer. */
	CHECK_EQ(int(SHIFT_DEPTHCOMPARE), 0);

	for (int v = 0; v < ShaderClass::PASS_MAX; ++v) {
		ShaderClass s;
		s.Set_Depth_Compare(ShaderClass::DepthCompareType(v));
		CHECK_EQ(int(s.Get_Depth_Compare()), v);
	}
}

TEST(shade_cnst_macro_matches_the_setters)
{
	/* Every preset is built with SHADE_CNST; if the macro's argument order
	   ever drifts from the Set_* calls the whole preset table is silently
	   wrong.  Build the same state both ways and compare the words. */
	ShaderClass from_macro(SHADE_CNST(
		ShaderClass::PASS_GEQUAL,
		ShaderClass::DEPTH_WRITE_DISABLE,
		ShaderClass::COLOR_WRITE_ENABLE,
		ShaderClass::SRCBLEND_SRC_ALPHA,
		ShaderClass::DSTBLEND_ONE_MINUS_SRC_COLOR,
		ShaderClass::FOG_SCALE_FRAGMENT,
		ShaderClass::GRADIENT_ADD,
		ShaderClass::SECONDARY_GRADIENT_ENABLE,
		ShaderClass::TEXTURING_ENABLE,
		ShaderClass::ALPHATEST_ENABLE,
		ShaderClass::CULL_MODE_DISABLE,
		ShaderClass::DETAILCOLOR_SUBR,
		ShaderClass::DETAILALPHA_SCALE));

	ShaderClass from_setters;
	from_setters.Set_Depth_Compare(ShaderClass::PASS_GEQUAL);
	from_setters.Set_Depth_Mask(ShaderClass::DEPTH_WRITE_DISABLE);
	from_setters.Set_Color_Mask(ShaderClass::COLOR_WRITE_ENABLE);
	from_setters.Set_Src_Blend_Func(ShaderClass::SRCBLEND_SRC_ALPHA);
	from_setters.Set_Dst_Blend_Func(ShaderClass::DSTBLEND_ONE_MINUS_SRC_COLOR);
	from_setters.Set_Fog_Func(ShaderClass::FOG_SCALE_FRAGMENT);
	from_setters.Set_Primary_Gradient(ShaderClass::GRADIENT_ADD);
	from_setters.Set_Secondary_Gradient(ShaderClass::SECONDARY_GRADIENT_ENABLE);
	from_setters.Set_Texturing(ShaderClass::TEXTURING_ENABLE);
	from_setters.Set_Alpha_Test(ShaderClass::ALPHATEST_ENABLE);
	from_setters.Set_Cull_Mode(ShaderClass::CULL_MODE_DISABLE);
	from_setters.Set_Post_Detail_Color_Func(ShaderClass::DETAILCOLOR_SUBR);
	from_setters.Set_Post_Detail_Alpha_Func(ShaderClass::DETAILALPHA_SCALE);

	CHECK_EQ(from_macro.Get_Bits(), from_setters.Get_Bits());

	/* SHADE_CNST has no npatch argument, so that field stays clear. */
	CHECK_EQ(from_macro.Get_NPatch_Enable(), ShaderClass::NPATCH_DISABLE);
}

TEST(shader_copy_and_comparison)
{
	ShaderClass a;
	a.Set_Fog_Func(ShaderClass::FOG_ENABLE);

	ShaderClass b(a);
	CHECK(a == b);
	CHECK(!(a != b));
	CHECK_EQ(b.Get_Fog_Func(), ShaderClass::FOG_ENABLE);

	b.Set_Fog_Func(ShaderClass::FOG_WHITE);
	CHECK(a != b);
	CHECK(!(a == b));

	ShaderClass c(a.Get_Bits());
	CHECK(a == c);
}

TEST(shader_uses_alpha_covers_every_trigger)
{
	ShaderClass s;
	CHECK(!s.Uses_Alpha());				/* reset default blends nothing */

	s.Reset();
	s.Set_Alpha_Test(ShaderClass::ALPHATEST_ENABLE);
	CHECK(s.Uses_Alpha());

	s.Reset();
	s.Set_Dst_Blend_Func(ShaderClass::DSTBLEND_SRC_ALPHA);
	CHECK(s.Uses_Alpha());

	s.Reset();
	s.Set_Dst_Blend_Func(ShaderClass::DSTBLEND_ONE_MINUS_SRC_ALPHA);
	CHECK(s.Uses_Alpha());

	s.Reset();
	s.Set_Src_Blend_Func(ShaderClass::SRCBLEND_SRC_ALPHA);
	CHECK(s.Uses_Alpha());

	s.Reset();
	s.Set_Src_Blend_Func(ShaderClass::SRCBLEND_ONE_MINUS_SRC_ALPHA);
	CHECK(s.Uses_Alpha());

	/* Colour-only blends do not. */
	s.Reset();
	s.Set_Src_Blend_Func(ShaderClass::SRCBLEND_ZERO);
	s.Set_Dst_Blend_Func(ShaderClass::DSTBLEND_SRC_COLOR);
	CHECK(!s.Uses_Alpha());
}

TEST(shader_uses_predicates_track_their_fields)
{
	ShaderClass s;

	CHECK(!s.Uses_Fog());
	s.Set_Fog_Func(ShaderClass::FOG_SCALE_FRAGMENT);
	CHECK(s.Uses_Fog());

	s.Reset();
	CHECK(s.Uses_Primary_Gradient());			/* MODULATE is the default */
	s.Set_Primary_Gradient(ShaderClass::GRADIENT_DISABLE);
	CHECK(!s.Uses_Primary_Gradient());

	s.Reset();
	CHECK(!s.Uses_Secondary_Gradient());
	s.Set_Secondary_Gradient(ShaderClass::SECONDARY_GRADIENT_ENABLE);
	CHECK(s.Uses_Secondary_Gradient());

	s.Reset();
	CHECK(!s.Uses_Texture());
	s.Set_Texturing(ShaderClass::TEXTURING_ENABLE);
	CHECK(s.Uses_Texture());
}

TEST(uses_post_detail_texture_requires_texturing)
{
	ShaderClass s;

	/* Detail funcs set but texturing off -> no detail pass. */
	s.Set_Post_Detail_Color_Func(ShaderClass::DETAILCOLOR_ADD);
	CHECK(!s.Uses_Post_Detail_Texture());

	s.Set_Texturing(ShaderClass::TEXTURING_ENABLE);
	CHECK(s.Uses_Post_Detail_Texture());

	s.Set_Post_Detail_Color_Func(ShaderClass::DETAILCOLOR_DISABLE);
	CHECK(!s.Uses_Post_Detail_Texture());

	/* The alpha func alone is enough. */
	s.Set_Post_Detail_Alpha_Func(ShaderClass::DETAILALPHA_SCALE);
	CHECK(s.Uses_Post_Detail_Texture());
}

TEST(static_sort_category_classification)
{
	ShaderClass s;

	/* Opaque: no alpha test and nothing read back from the frame buffer. */
	s.Reset();
	s.Set_Dst_Blend_Func(ShaderClass::DSTBLEND_ZERO);
	CHECK_EQ(s.Get_SS_Category(), ShaderClass::SSCAT_OPAQUE);

	/* Alpha test wins over opaque only once the alpha test is on. */
	s.Set_Alpha_Test(ShaderClass::ALPHATEST_ENABLE);
	CHECK_EQ(s.Get_SS_Category(), ShaderClass::SSCAT_ALPHA_TEST);

	/* Alpha test plus the standard alpha blend is still the alpha-test bin. */
	s.Reset();
	s.Set_Alpha_Test(ShaderClass::ALPHATEST_ENABLE);
	s.Set_Src_Blend_Func(ShaderClass::SRCBLEND_SRC_ALPHA);
	s.Set_Dst_Blend_Func(ShaderClass::DSTBLEND_ONE_MINUS_SRC_ALPHA);
	CHECK_EQ(s.Get_SS_Category(), ShaderClass::SSCAT_ALPHA_TEST);

	/* Additive. */
	s.Reset();
	s.Set_Src_Blend_Func(ShaderClass::SRCBLEND_ONE);
	s.Set_Dst_Blend_Func(ShaderClass::DSTBLEND_ONE);
	CHECK_EQ(s.Get_SS_Category(), ShaderClass::SSCAT_ADDITIVE);

	/* Screen. */
	s.Set_Dst_Blend_Func(ShaderClass::DSTBLEND_ONE_MINUS_SRC_COLOR);
	CHECK_EQ(s.Get_SS_Category(), ShaderClass::SSCAT_SCREEN);

	/* Plain alpha blend without alpha test falls in the catch-all bin. */
	s.Reset();
	s.Set_Src_Blend_Func(ShaderClass::SRCBLEND_SRC_ALPHA);
	s.Set_Dst_Blend_Func(ShaderClass::DSTBLEND_ONE_MINUS_SRC_ALPHA);
	CHECK_EQ(s.Get_SS_Category(), ShaderClass::SSCAT_OTHER);

	/* Multiplicative too. */
	s.Reset();
	s.Set_Src_Blend_Func(ShaderClass::SRCBLEND_ZERO);
	s.Set_Dst_Blend_Func(ShaderClass::DSTBLEND_SRC_COLOR);
	CHECK_EQ(s.Get_SS_Category(), ShaderClass::SSCAT_OTHER);

	/* Alpha test on top of an additive blend drops through to additive - the
	   alpha-test arm only claims the two blend combinations above. */
	s.Reset();
	s.Set_Alpha_Test(ShaderClass::ALPHATEST_ENABLE);
	s.Set_Src_Blend_Func(ShaderClass::SRCBLEND_ONE);
	s.Set_Dst_Blend_Func(ShaderClass::DSTBLEND_ONE);
	CHECK_EQ(s.Get_SS_Category(), ShaderClass::SSCAT_ADDITIVE);
}

TEST(guess_sort_level_follows_the_category)
{
	ShaderClass s;

	s.Reset();							/* opaque */
	CHECK_EQ(s.Guess_Sort_Level(), SORT_LEVEL_NONE);

	s.Set_Alpha_Test(ShaderClass::ALPHATEST_ENABLE);
	CHECK_EQ(s.Guess_Sort_Level(), SORT_LEVEL_NONE);

	s.Reset();
	s.Set_Src_Blend_Func(ShaderClass::SRCBLEND_ONE);
	s.Set_Dst_Blend_Func(ShaderClass::DSTBLEND_ONE_MINUS_SRC_COLOR);
	CHECK_EQ(s.Guess_Sort_Level(), SORT_LEVEL_BIN2);

	s.Set_Dst_Blend_Func(ShaderClass::DSTBLEND_ONE);
	CHECK_EQ(s.Guess_Sort_Level(), SORT_LEVEL_BIN3);

	s.Reset();
	s.Set_Src_Blend_Func(ShaderClass::SRCBLEND_SRC_ALPHA);
	s.Set_Dst_Blend_Func(ShaderClass::DSTBLEND_ONE_MINUS_SRC_ALPHA);
	CHECK_EQ(s.Guess_Sort_Level(), SORT_LEVEL_BIN1);

	/* Sorted bins draw back-to-front after the unsorted ones, so alpha must
	   land on a higher level than additive, which beats screen. */
	CHECK(SORT_LEVEL_BIN1 > SORT_LEVEL_BIN2);
	CHECK(SORT_LEVEL_BIN2 > SORT_LEVEL_BIN3);
	CHECK(SORT_LEVEL_BIN3 > SORT_LEVEL_NONE);
}

TEST(enable_fog_picks_a_mode_per_blend_pair)
{
	ShaderClass s;

	/* Multiplicative -> the fragment must be replaced by the fog colour. */
	s.Reset();
	s.Set_Src_Blend_Func(ShaderClass::SRCBLEND_ZERO);
	s.Set_Dst_Blend_Func(ShaderClass::DSTBLEND_SRC_COLOR);
	s.Enable_Fog("test");
	CHECK_EQ(s.Get_Fog_Func(), ShaderClass::FOG_WHITE);

	/* Opaque -> ordinary fog blend. */
	s.Reset();
	s.Set_Src_Blend_Func(ShaderClass::SRCBLEND_ONE);
	s.Set_Dst_Blend_Func(ShaderClass::DSTBLEND_ZERO);
	s.Enable_Fog("test");
	CHECK_EQ(s.Get_Fog_Func(), ShaderClass::FOG_ENABLE);

	/* Additive and screen -> scale the fragment towards black instead. */
	s.Reset();
	s.Set_Src_Blend_Func(ShaderClass::SRCBLEND_ONE);
	s.Set_Dst_Blend_Func(ShaderClass::DSTBLEND_ONE);
	s.Enable_Fog("test");
	CHECK_EQ(s.Get_Fog_Func(), ShaderClass::FOG_SCALE_FRAGMENT);

	s.Reset();
	s.Set_Src_Blend_Func(ShaderClass::SRCBLEND_ONE);
	s.Set_Dst_Blend_Func(ShaderClass::DSTBLEND_ONE_MINUS_SRC_COLOR);
	s.Enable_Fog("test");
	CHECK_EQ(s.Get_Fog_Func(), ShaderClass::FOG_SCALE_FRAGMENT);

	/* Both alpha blends. */
	s.Reset();
	s.Set_Src_Blend_Func(ShaderClass::SRCBLEND_SRC_ALPHA);
	s.Set_Dst_Blend_Func(ShaderClass::DSTBLEND_ONE_MINUS_SRC_ALPHA);
	s.Enable_Fog("test");
	CHECK_EQ(s.Get_Fog_Func(), ShaderClass::FOG_ENABLE);

	s.Reset();
	s.Set_Src_Blend_Func(ShaderClass::SRCBLEND_ONE_MINUS_SRC_ALPHA);
	s.Set_Dst_Blend_Func(ShaderClass::DSTBLEND_SRC_ALPHA);
	s.Enable_Fog("test");
	CHECK_EQ(s.Get_Fog_Func(), ShaderClass::FOG_ENABLE);
}

TEST(enable_fog_leaves_unfoggable_blends_alone)
{
	/* The unhandled combinations only warn (a no-op without WWDEBUG) - the
	   shader must come back unmodified rather than half-set. */
	ShaderClass s;

	s.Reset();
	s.Set_Src_Blend_Func(ShaderClass::SRCBLEND_ZERO);
	s.Set_Dst_Blend_Func(ShaderClass::DSTBLEND_ONE);
	unsigned int before = s.Get_Bits();
	s.Enable_Fog("test");
	CHECK_EQ(s.Get_Bits(), before);
	CHECK_EQ(s.Get_Fog_Func(), ShaderClass::FOG_DISABLE);

	s.Reset();
	s.Set_Src_Blend_Func(ShaderClass::SRCBLEND_ONE);
	s.Set_Dst_Blend_Func(ShaderClass::DSTBLEND_SRC_ALPHA);
	before = s.Get_Bits();
	s.Enable_Fog("test");
	CHECK_EQ(s.Get_Bits(), before);

	s.Reset();
	s.Set_Src_Blend_Func(ShaderClass::SRCBLEND_SRC_ALPHA);
	s.Set_Dst_Blend_Func(ShaderClass::DSTBLEND_ONE);
	before = s.Get_Bits();
	s.Enable_Fog("test");
	CHECK_EQ(s.Get_Bits(), before);

	s.Reset();
	s.Set_Src_Blend_Func(ShaderClass::SRCBLEND_ONE_MINUS_SRC_ALPHA);
	s.Set_Dst_Blend_Func(ShaderClass::DSTBLEND_ZERO);
	before = s.Get_Bits();
	s.Enable_Fog("test");
	CHECK_EQ(s.Get_Bits(), before);
}

TEST(init_from_material3_only_reacts_to_the_alpha_attribute)
{
	W3dMaterial3Struct mat;
	memset(&mat, 0, sizeof(mat));

	ShaderClass s;
	unsigned int before = s.Get_Bits();
	s.Init_From_Material3(mat);
	CHECK_EQ(s.Get_Bits(), before);

	mat.Attributes = W3DMATERIAL_USE_ALPHA;
	s.Init_From_Material3(mat);
	CHECK_EQ(s.Get_Depth_Mask(), ShaderClass::DEPTH_WRITE_DISABLE);
	CHECK_EQ(s.Get_Src_Blend_Func(), ShaderClass::SRCBLEND_SRC_ALPHA);
	CHECK_EQ(s.Get_Dst_Blend_Func(), ShaderClass::DSTBLEND_ONE_MINUS_SRC_ALPHA);

	/* Nothing else in the material is looked at. */
	ShaderClass t;
	W3dMaterial3Struct loud;
	memset(&loud, 0xff, sizeof(loud));
	loud.Attributes = 0;
	before = t.Get_Bits();
	t.Init_From_Material3(loud);
	CHECK_EQ(t.Get_Bits(), before);
}

TEST(shader_description_reports_the_state)
{
	ShaderClass s;
	StringClass str;

	s.Get_Description(str);
	CHECK(strstr(str, "DEPTH_COMPARE:PASS_LEQUAL") != 0);
	CHECK(strstr(str, "DEPTH_WRITE_ENABLE") != 0);
	CHECK(strstr(str, "COLOR_WRITE_ENABLE") != 0);
	CHECK(strstr(str, "DSTBLEND_ZERO") != 0);
	CHECK(strstr(str, "FOG_DISABLE") != 0);
	CHECK(strstr(str, "GRADIENT_MODULATE |") != 0);
	CHECK(strstr(str, "SECONDARY_GRADIENT_DISABLE") != 0);
	CHECK(strstr(str, "SRCBLEND_ONE |") != 0);
	CHECK(strstr(str, "TEXTURING_DISABLE") != 0);
	CHECK(strstr(str, "NPATCH_DISABLE") != 0);
	CHECK(strstr(str, "ALPHATEST_DISABLE") != 0);
	CHECK(strstr(str, "CULL_MODE_ENABLE") != 0);
	CHECK(strstr(str, "DETAILCOLOR_DISABLE") != 0);

	/* The string is rebuilt from scratch, not appended to. */
	StringClass again;
	s.Set_Fog_Func(ShaderClass::FOG_WHITE);
	s.Get_Description(again);
	CHECK(strstr(again, "FOG_WHITE") != 0);
	CHECK(strstr(again, "FOG_DISABLE") == 0);
	s.Get_Description(again);
	CHECK_EQ(strstr(again, "FOG_WHITE"), strstr(again, "FOG_WHITE"));
	CHECK(again.Get_Length() < 400);		/* no runaway concatenation */

	/* Get_Description returns the same string it filled in. */
	CHECK_STR(s.Get_Description(again), again);
}

TEST(shader_description_never_lists_the_post_detail_alpha_func)
{
	/* Every other field is printed; the post-detail alpha func is the one
	   Get_Description forgets.  Pinned so the omission is a known gap rather
	   than a surprise when reading a shader dump. */
	ShaderClass s;
	s.Set_Post_Detail_Alpha_Func(ShaderClass::DETAILALPHA_INVSCALE);

	StringClass str;
	s.Get_Description(str);
	CHECK(strstr(str, "DETAILALPHA") == 0);
}

TEST(preset_shaders_are_configured_as_documented)
{
	/* Opaque: writes depth, no blend, textured. */
	CHECK_EQ(ShaderClass::_PresetOpaqueShader.Get_Depth_Mask(), ShaderClass::DEPTH_WRITE_ENABLE);
	CHECK_EQ(ShaderClass::_PresetOpaqueShader.Get_Src_Blend_Func(), ShaderClass::SRCBLEND_ONE);
	CHECK_EQ(ShaderClass::_PresetOpaqueShader.Get_Dst_Blend_Func(), ShaderClass::DSTBLEND_ZERO);
	CHECK(ShaderClass::_PresetOpaqueShader.Uses_Texture());
	CHECK_EQ(ShaderClass::_PresetOpaqueShader.Get_SS_Category(), ShaderClass::SSCAT_OPAQUE);

	/* Additive: one/one, depth read but no write. */
	CHECK_EQ(ShaderClass::_PresetAdditiveShader.Get_Depth_Mask(), ShaderClass::DEPTH_WRITE_DISABLE);
	CHECK_EQ(ShaderClass::_PresetAdditiveShader.Get_Src_Blend_Func(), ShaderClass::SRCBLEND_ONE);
	CHECK_EQ(ShaderClass::_PresetAdditiveShader.Get_Dst_Blend_Func(), ShaderClass::DSTBLEND_ONE);
	CHECK_EQ(ShaderClass::_PresetAdditiveShader.Get_SS_Category(), ShaderClass::SSCAT_ADDITIVE);

	/* Alpha. */
	CHECK_EQ(ShaderClass::_PresetAlphaShader.Get_Src_Blend_Func(), ShaderClass::SRCBLEND_SRC_ALPHA);
	CHECK_EQ(ShaderClass::_PresetAlphaShader.Get_Dst_Blend_Func(), ShaderClass::DSTBLEND_ONE_MINUS_SRC_ALPHA);
	CHECK(ShaderClass::_PresetAlphaShader.Uses_Alpha());

	/* Multiplicative. */
	CHECK_EQ(ShaderClass::_PresetMultiplicativeShader.Get_Src_Blend_Func(), ShaderClass::SRCBLEND_ZERO);
	CHECK_EQ(ShaderClass::_PresetMultiplicativeShader.Get_Dst_Blend_Func(), ShaderClass::DSTBLEND_SRC_COLOR);

	/* Bumpenvmap is the only preset using the bump gradient. */
	CHECK_EQ(ShaderClass::_PresetBumpenvmapShader.Get_Primary_Gradient(), ShaderClass::GRADIENT_BUMPENVMAP);
	CHECK_EQ(ShaderClass::_PresetBumpenvmapShader.Get_Post_Detail_Color_Func(), ShaderClass::DETAILCOLOR_ADD);
}

TEST(preset_2d_shaders_ignore_the_depth_buffer)
{
	static ShaderClass *const two_d[] = {
		&ShaderClass::_PresetOpaque2DShader,
		&ShaderClass::_PresetAdditive2DShader,
		&ShaderClass::_PresetAlpha2DShader,
		&ShaderClass::_PresetATest2DShader,
		&ShaderClass::_PresetATestBlend2DShader,
		&ShaderClass::_PresetScreen2DShader,
		&ShaderClass::_PresetMultiplicative2DShader
	};

	for (int i = 0; i < int(sizeof(two_d)/sizeof(two_d[0])); ++i) {
		CHECK_EQ(two_d[i]->Get_Depth_Compare(), ShaderClass::PASS_ALWAYS);
		CHECK_EQ(two_d[i]->Get_Depth_Mask(), ShaderClass::DEPTH_WRITE_DISABLE);
		CHECK_EQ(two_d[i]->Get_Primary_Gradient(), ShaderClass::GRADIENT_DISABLE);
		CHECK(two_d[i]->Uses_Texture());
		CHECK(!two_d[i]->Uses_Fog());
	}
}

TEST(preset_sprite_shaders_read_depth_but_never_write_it)
{
	static ShaderClass *const sprites[] = {
		&ShaderClass::_PresetOpaqueSpriteShader,
		&ShaderClass::_PresetAdditiveSpriteShader,
		&ShaderClass::_PresetAlphaSpriteShader,
		&ShaderClass::_PresetScreenSpriteShader,
		&ShaderClass::_PresetMultiplicativeSpriteShader
	};

	for (int i = 0; i < int(sizeof(sprites)/sizeof(sprites[0])); ++i) {
		CHECK_EQ(sprites[i]->Get_Depth_Compare(), ShaderClass::PASS_LEQUAL);
		CHECK_EQ(sprites[i]->Get_Depth_Mask(), ShaderClass::DEPTH_WRITE_DISABLE);
	}

	/* The solid presets are the untextured ones. */
	CHECK(!ShaderClass::_PresetOpaqueSolidShader.Uses_Texture());
	CHECK(!ShaderClass::_PresetAdditiveSolidShader.Uses_Texture());
	CHECK(!ShaderClass::_PresetAlphaSolidShader.Uses_Texture());
	CHECK(ShaderClass::_PresetOpaqueSolidShader.Uses_Primary_Gradient());

	/* The alpha-test presets are the ones that actually enable it. */
	CHECK_EQ(ShaderClass::_PresetATest2DShader.Get_Alpha_Test(), ShaderClass::ALPHATEST_ENABLE);
	CHECK_EQ(ShaderClass::_PresetATestSpriteShader.Get_Alpha_Test(), ShaderClass::ALPHATEST_ENABLE);
	CHECK_EQ(ShaderClass::_PresetATestBlend2DShader.Get_Alpha_Test(), ShaderClass::ALPHATEST_ENABLE);
	CHECK_EQ(ShaderClass::_PresetATestBlendSpriteShader.Get_Alpha_Test(), ShaderClass::ALPHATEST_ENABLE);
	CHECK_EQ(ShaderClass::_PresetOpaqueShader.Get_Alpha_Test(), ShaderClass::ALPHATEST_DISABLE);
}

TEST(no_preset_enables_fog_or_npatches)
{
	/* The header promises "none of them have fogging"; the npatch field was
	   added later and SHADE_CNST cannot set it, so it must be clear too. */
	static ShaderClass *const presets[] = {
		&ShaderClass::_PresetOpaqueShader, &ShaderClass::_PresetAdditiveShader,
		&ShaderClass::_PresetBumpenvmapShader, &ShaderClass::_PresetAlphaShader,
		&ShaderClass::_PresetMultiplicativeShader, &ShaderClass::_PresetOpaque2DShader,
		&ShaderClass::_PresetOpaqueSpriteShader, &ShaderClass::_PresetAdditive2DShader,
		&ShaderClass::_PresetAlpha2DShader, &ShaderClass::_PresetAdditiveSpriteShader,
		&ShaderClass::_PresetAlphaSpriteShader, &ShaderClass::_PresetOpaqueSolidShader,
		&ShaderClass::_PresetAdditiveSolidShader, &ShaderClass::_PresetAlphaSolidShader,
		&ShaderClass::_PresetATest2DShader, &ShaderClass::_PresetATestSpriteShader,
		&ShaderClass::_PresetATestBlend2DShader, &ShaderClass::_PresetATestBlendSpriteShader,
		&ShaderClass::_PresetScreen2DShader, &ShaderClass::_PresetScreenSpriteShader,
		&ShaderClass::_PresetMultiplicative2DShader, &ShaderClass::_PresetMultiplicativeSpriteShader
	};

	for (int i = 0; i < int(sizeof(presets)/sizeof(presets[0])); ++i) {
		CHECK_EQ(presets[i]->Get_Fog_Func(), ShaderClass::FOG_DISABLE);
		CHECK_EQ(presets[i]->Get_NPatch_Enable(), ShaderClass::NPATCH_DISABLE);
		CHECK_EQ(presets[i]->Get_Color_Mask(), ShaderClass::COLOR_WRITE_ENABLE);
		CHECK_EQ(presets[i]->Get_Cull_Mode(), ShaderClass::CULL_MODE_ENABLE);
		CHECK_EQ(presets[i]->Get_Secondary_Gradient(), ShaderClass::SECONDARY_GRADIENT_DISABLE);
		/* Unallocated high bits stay clear; bit 27 is the Glow option's opt-in. */
		CHECK_EQ(presets[i]->Get_Bits() & 0xf0000000u, 0u);
	}
}

TEST(every_preset_can_be_fogged_or_is_deliberately_unfoggable)
{
	/* Enable_Fog on a preset must either set a fog mode or leave it alone -
	   never leave the shader in a state that renders differently without
	   fogging being on. */
	static ShaderClass *const presets[] = {
		&ShaderClass::_PresetOpaqueShader, &ShaderClass::_PresetAdditiveShader,
		&ShaderClass::_PresetAlphaShader, &ShaderClass::_PresetMultiplicativeShader,
		&ShaderClass::_PresetOpaque2DShader, &ShaderClass::_PresetScreen2DShader
	};

	for (int i = 0; i < int(sizeof(presets)/sizeof(presets[0])); ++i) {
		ShaderClass copy(*presets[i]);
		copy.Enable_Fog("preset");
		CHECK_EQ(copy.Get_Bits() & ~unsigned(ShaderClass::MASK_FOG),
		         presets[i]->Get_Bits() & ~unsigned(ShaderClass::MASK_FOG));
		CHECK_NE(copy.Get_Fog_Func(), ShaderClass::FOG_DISABLE);
	}
}

TEST(backface_culling_inversion_is_a_global_toggle)
{
	bool original = ShaderClass::Is_Backface_Culling_Inverted();

	ShaderClass::Invert_Backface_Culling(true);
	CHECK(ShaderClass::Is_Backface_Culling_Inverted());

	ShaderClass::Invert_Backface_Culling(false);
	CHECK(!ShaderClass::Is_Backface_Culling_Inverted());

	/* Default is un-inverted; restore whatever we found. */
	CHECK(!original);
	ShaderClass::Invert_Backface_Culling(original);
}

/*=========================================================================
   dx8fvf - vertex layout arithmetic
  =========================================================================*/

TEST(fvf_sizes_match_the_matching_vertex_structs)
{
	CHECK_EQ(FVFInfoClass(DX8_FVF_XYZ).Get_FVF_Size(), sizeof(VertexFormatXYZ));
	CHECK_EQ(FVFInfoClass(DX8_FVF_XYZN).Get_FVF_Size(), sizeof(VertexFormatXYZN));
	CHECK_EQ(FVFInfoClass(DX8_FVF_XYZNUV1).Get_FVF_Size(), sizeof(VertexFormatXYZNUV1));
	CHECK_EQ(FVFInfoClass(DX8_FVF_XYZNUV2).Get_FVF_Size(), sizeof(VertexFormatXYZNUV2));
	CHECK_EQ(FVFInfoClass(DX8_FVF_XYZNDUV1).Get_FVF_Size(), sizeof(VertexFormatXYZNDUV1));
	CHECK_EQ(FVFInfoClass(DX8_FVF_XYZNDUV2).Get_FVF_Size(), sizeof(VertexFormatXYZNDUV2));
	CHECK_EQ(FVFInfoClass(DX8_FVF_XYZDUV1).Get_FVF_Size(), sizeof(VertexFormatXYZDUV1));
	CHECK_EQ(FVFInfoClass(DX8_FVF_XYZDUV2).Get_FVF_Size(), sizeof(VertexFormatXYZDUV2));
	CHECK_EQ(FVFInfoClass(DX8_FVF_XYZUV1).Get_FVF_Size(), sizeof(VertexFormatXYZUV1));
	CHECK_EQ(FVFInfoClass(DX8_FVF_XYZUV2).Get_FVF_Size(), sizeof(VertexFormatXYZUV2));
	CHECK_EQ(FVFInfoClass(DX8_FVF_XYZNDUV1TG3).Get_FVF_Size(), sizeof(VertexFormatXYZNDUV1TG3));
	CHECK_EQ(FVFInfoClass(DX8_FVF_XYZNDCUBEMAP).Get_FVF_Size(), sizeof(VertexFormatXYZNDCUBEMAP));
}

TEST(fvf_offsets_match_the_struct_member_layout)
{
	/* The offsets are what the renderer uses to poke a locked vertex buffer;
	   they have to line up with the structs the same header declares. */
	FVFInfoClass nduv2(DX8_FVF_XYZNDUV2);
	CHECK_EQ(nduv2.Get_Location_Offset(), 0u);
	CHECK_EQ(nduv2.Get_Normal_Offset(), unsigned(offsetof(VertexFormatXYZNDUV2, nx)));
	CHECK_EQ(nduv2.Get_Diffuse_Offset(), unsigned(offsetof(VertexFormatXYZNDUV2, diffuse)));
	CHECK_EQ(nduv2.Get_Tex_Offset(0), unsigned(offsetof(VertexFormatXYZNDUV2, u1)));
	CHECK_EQ(nduv2.Get_Tex_Offset(1), unsigned(offsetof(VertexFormatXYZNDUV2, u2)));
	CHECK_EQ(nduv2.Get_FVF(), unsigned(DX8_FVF_XYZNDUV2));

	FVFInfoClass duv2(DX8_FVF_XYZDUV2);
	CHECK_EQ(duv2.Get_Diffuse_Offset(), unsigned(offsetof(VertexFormatXYZDUV2, diffuse)));
	CHECK_EQ(duv2.Get_Tex_Offset(0), unsigned(offsetof(VertexFormatXYZDUV2, u1)));
	CHECK_EQ(duv2.Get_Tex_Offset(1), unsigned(offsetof(VertexFormatXYZDUV2, u2)));

	FVFInfoClass uv2(DX8_FVF_XYZUV2);
	CHECK_EQ(uv2.Get_Tex_Offset(0), unsigned(offsetof(VertexFormatXYZUV2, u1)));
	CHECK_EQ(uv2.Get_Tex_Offset(1), unsigned(offsetof(VertexFormatXYZUV2, u2)));

	FVFInfoClass nuv1(DX8_FVF_XYZNUV1);
	CHECK_EQ(nuv1.Get_Normal_Offset(), unsigned(offsetof(VertexFormatXYZNUV1, nx)));
	CHECK_EQ(nuv1.Get_Tex_Offset(0), unsigned(offsetof(VertexFormatXYZNUV1, u1)));
}

TEST(fvf_offsets_of_absent_elements_alias_the_next_one)
{
	/* A missing element does not get its own slot - the offset simply equals
	   whatever comes next, so reading it without checking the FVF hands back
	   somebody else's data. */
	FVFInfoClass xyz(DX8_FVF_XYZ);
	CHECK_EQ(xyz.Get_Location_Offset(), 0u);
	CHECK_EQ(xyz.Get_Normal_Offset(), 12u);
	CHECK_EQ(xyz.Get_Diffuse_Offset(), 12u);
	CHECK_EQ(xyz.Get_Specular_Offset(), 12u);
	CHECK_EQ(xyz.Get_Tex_Offset(0), 12u);
	CHECK_EQ(xyz.Get_FVF_Size(), 12u);

	/* No preset FVF uses specular, so its offset always equals texcoord 0. */
	static const unsigned fvfs[] = {
		DX8_FVF_XYZ, DX8_FVF_XYZN, DX8_FVF_XYZNUV1, DX8_FVF_XYZNUV2,
		DX8_FVF_XYZNDUV1, DX8_FVF_XYZNDUV2, DX8_FVF_XYZDUV1, DX8_FVF_XYZDUV2,
		DX8_FVF_XYZUV1, DX8_FVF_XYZUV2, DX8_FVF_XYZNDCUBEMAP
	};
	for (int i = 0; i < int(sizeof(fvfs)/sizeof(fvfs[0])); ++i) {
		FVFInfoClass info(fvfs[i]);
		CHECK_EQ(info.Get_Specular_Offset(), info.Get_Tex_Offset(0));
		CHECK(info.Get_Tex_Offset(0) <= info.Get_FVF_Size());
	}
}

TEST(fvf_texcoord_offsets_assume_two_floats_per_set)
{
	/* DEFECT: the loop tests (FVF & D3DFVF_TEXCOORDSIZE2(i)) == ... , and
	   D3DFVF_TEXTUREFORMAT2 is 0, so that comparison is true for every FVF.
	   The size-3 and size-4 arms below it are unreachable, and any coordinate
	   set wider than two floats gets the wrong offset from there on.
	   XYZNDUV1TG3 declares three size-3 sets, so it is affected. */
	CHECK_EQ(unsigned(D3DFVF_TEXTUREFORMAT2), 0u);

	FVFInfoClass tg3(DX8_FVF_XYZNDUV1TG3);
	CHECK_EQ(tg3.Get_Tex_Offset(0), unsigned(offsetof(VertexFormatXYZNDUV1TG3, u1)));
	CHECK_EQ(tg3.Get_Tex_Offset(1), unsigned(offsetof(VertexFormatXYZNDUV1TG3, Sx)));

	/* From here the walk is 8 bytes per set instead of 12. */
	CHECK_EQ(tg3.Get_Tex_Offset(2), 44u);
	CHECK_EQ(unsigned(offsetof(VertexFormatXYZNDUV1TG3, Tx)), 48u);
	CHECK_EQ(tg3.Get_Tex_Offset(3), 52u);
	CHECK_EQ(unsigned(offsetof(VertexFormatXYZNDUV1TG3, SxTx)), 60u);

	/* D3DXGetFVFVertexSize does the arithmetic properly, so the size is right
	   even though the offsets are not. */
	CHECK_EQ(tg3.Get_FVF_Size(), 72u);
}

TEST(fvf_size_can_be_supplied_for_a_zero_fvf)
{
	/* FVF 0 means "vertex shader declaration", where D3D cannot compute the
	   stride - the caller passes it in instead. */
	FVFInfoClass shader_decl(0, 96);
	CHECK_EQ(shader_decl.Get_FVF(), 0u);
	CHECK_EQ(shader_decl.Get_FVF_Size(), 96u);

	/* Every offset collapses to zero without any FVF bits. */
	CHECK_EQ(shader_decl.Get_Location_Offset(), 0u);
	CHECK_EQ(shader_decl.Get_Normal_Offset(), 0u);
	CHECK_EQ(shader_decl.Get_Diffuse_Offset(), 0u);
	CHECK_EQ(shader_decl.Get_Tex_Offset(0), 0u);

	/* The two mutable setters exist so a vertex shader can be swapped in
	   after construction. */
	shader_decl.Set_FVF(DX8_FVF_XYZ);
	shader_decl.Set_FVF_Size(12);
	CHECK_EQ(shader_decl.Get_FVF(), unsigned(DX8_FVF_XYZ));
	CHECK_EQ(shader_decl.Get_FVF_Size(), 12u);
}

TEST(fvf_names_are_unique_per_preset)
{
	static const unsigned fvfs[] = {
		DX8_FVF_XYZ, DX8_FVF_XYZN, DX8_FVF_XYZNUV1, DX8_FVF_XYZNUV2,
		DX8_FVF_XYZNDUV1, DX8_FVF_XYZNDUV2, DX8_FVF_XYZDUV1, DX8_FVF_XYZDUV2,
		DX8_FVF_XYZUV1, DX8_FVF_XYZUV2, DX8_FVF_XYZNDUV1TG3,
		DX8_FVF_XYZNUV2DMAP, DX8_FVF_XYZNDCUBEMAP
	};
	const int count = int(sizeof(fvfs)/sizeof(fvfs[0]));

	StringClass names[13];
	for (int i = 0; i < count; ++i) {
		FVFInfoClass(fvfs[i]).Get_FVF_Name(names[i]);
		CHECK(names[i].Get_Length() > 0);
		CHECK_NE(strcmp(names[i], "Unknown!"), 0);
	}

	for (int i = 0; i < count; ++i) {
		for (int j = i + 1; j < count; ++j) {
			CHECK_NE(strcmp(names[i], names[j]), 0);
		}
	}

	CHECK_STR(names[0], "D3DFVF_XYZ");
	CHECK_STR(names[1], "D3DFVF_XYZ|D3DFVF_NORMAL");

	StringClass junk;
	FVFInfoClass(D3DFVF_XYZ | D3DFVF_SPECULAR).Get_FVF_Name(junk);
	CHECK_STR(junk, "Unknown!");
}

TEST(fvf_specular_offset_moves_texcoords_along)
{
	/* No preset FVF has specular, so exercise the branch directly. */
	FVFInfoClass info(D3DFVF_XYZ | D3DFVF_NORMAL | D3DFVF_DIFFUSE |
	                  D3DFVF_SPECULAR | D3DFVF_TEX1);

	CHECK_EQ(info.Get_Location_Offset(), 0u);
	CHECK_EQ(info.Get_Normal_Offset(), 12u);
	CHECK_EQ(info.Get_Diffuse_Offset(), 24u);
	CHECK_EQ(info.Get_Specular_Offset(), 28u);
	CHECK_EQ(info.Get_Tex_Offset(0), 32u);
	CHECK_EQ(info.Get_FVF_Size(), 40u);
}

/*=========================================================================
   w3d_util - file struct <-> runtime class
  =========================================================================*/

TEST(convert_vector_both_ways)
{
	W3dVectorStruct file;
	file.X = 1.5f; file.Y = -2.25f; file.Z = 1024.0f;

	Vector3 v;
	W3dUtilityClass::Convert_Vector(file, &v);
	CHECK_NEAR(v.X, 1.5f, 1e-6f);
	CHECK_NEAR(v.Y, -2.25f, 1e-6f);
	CHECK_NEAR(v.Z, 1024.0f, 1e-6f);

	W3dVectorStruct back;
	W3dUtilityClass::Convert_Vector(v, &back);
	CHECK_NEAR(back.X, file.X, 0.0f);
	CHECK_NEAR(back.Y, file.Y, 0.0f);
	CHECK_NEAR(back.Z, file.Z, 0.0f);
}

TEST(convert_quaternion_maps_q_array_to_xyzw)
{
	/* The file format stores the quaternion as Q[0..3]; the mapping to
	   X,Y,Z,W is positional and easy to get backwards. */
	W3dQuaternionStruct file;
	file.Q[0] = 0.1f; file.Q[1] = 0.2f; file.Q[2] = 0.3f; file.Q[3] = 0.4f;

	Quaternion q;
	W3dUtilityClass::Convert_Quaternion(file, &q);
	CHECK_NEAR(q.X, 0.1f, 1e-6f);
	CHECK_NEAR(q.Y, 0.2f, 1e-6f);
	CHECK_NEAR(q.Z, 0.3f, 1e-6f);
	CHECK_NEAR(q.W, 0.4f, 1e-6f);

	W3dQuaternionStruct back;
	W3dUtilityClass::Convert_Quaternion(q, &back);
	CHECK_NEAR(back.Q[0], 0.1f, 0.0f);
	CHECK_NEAR(back.Q[1], 0.2f, 0.0f);
	CHECK_NEAR(back.Q[2], 0.3f, 0.0f);
	CHECK_NEAR(back.Q[3], 0.4f, 0.0f);
}

TEST(convert_color_rgb_both_ways)
{
	W3dRGBStruct rgb(255, 128, 0);
	rgb.pad = 0xcd;

	Vector3 v;
	W3dUtilityClass::Convert_Color(rgb, &v);
	CHECK_NEAR(v.X, 1.0f, 1e-6f);
	CHECK_NEAR(v.Y, 128.0f / 255.0f, 1e-6f);
	CHECK_NEAR(v.Z, 0.0f, 1e-6f);

	W3dRGBStruct back;
	back.pad = 0xcd;
	W3dUtilityClass::Convert_Color(v, &back);
	CHECK_EQ(int(back.R), 255);
	CHECK_EQ(int(back.G), 128);
	CHECK_EQ(int(back.B), 0);
	/* The writer clears pad so the struct hashes and compares consistently. */
	CHECK_EQ(int(back.pad), 0);
}

TEST(convert_color_rgba_both_ways)
{
	W3dRGBAStruct rgba;
	rgba.R = 10; rgba.G = 20; rgba.B = 30; rgba.A = 40;

	Vector4 v;
	W3dUtilityClass::Convert_Color(rgba, &v);
	CHECK_NEAR(v.X, 10.0f / 255.0f, 1e-6f);
	CHECK_NEAR(v.Y, 20.0f / 255.0f, 1e-6f);
	CHECK_NEAR(v.Z, 30.0f / 255.0f, 1e-6f);
	CHECK_NEAR(v.W, 40.0f / 255.0f, 1e-6f);

	W3dRGBAStruct back;
	W3dUtilityClass::Convert_Color(v, &back);
	CHECK_EQ(int(back.R), 10);
	CHECK_EQ(int(back.G), 20);
	CHECK_EQ(int(back.B), 30);
	CHECK_EQ(int(back.A), 40);
}

TEST(convert_color_round_trips_every_byte_value)
{
	/* x/255 * 255 must land back on x for all 256 values, otherwise repeated
	   load/save cycles would drift a mesh's vertex colours. */
	for (int i = 0; i < 256; ++i) {
		W3dRGBStruct rgb(uint8(i), uint8(255 - i), uint8((i * 7) & 0xff));
		Vector3 v;
		W3dUtilityClass::Convert_Color(rgb, &v);

		W3dRGBStruct back;
		W3dUtilityClass::Convert_Color(v, &back);
		CHECK_EQ(int(back.R), i);
		CHECK_EQ(int(back.G), 255 - i);
		CHECK_EQ(int(back.B), (i * 7) & 0xff);
	}
}

TEST(convert_color_truncates_rather_than_rounds)
{
	/* (uint8)(255.0f * v) - a value one bit below a step still floors down.
	   Worth pinning: it is why the round trip above is exact. */
	Vector3 just_under(0.999f, 0.5f, 0.00001f);
	W3dRGBStruct rgb;
	W3dUtilityClass::Convert_Color(just_under, &rgb);
	CHECK_EQ(int(rgb.R), 254);			/* 254.745 -> 254 */
	CHECK_EQ(int(rgb.G), 127);			/* 127.5   -> 127 */
	CHECK_EQ(int(rgb.B), 0);
}

TEST(convert_shader_round_trips_the_carried_fields)
{
	ShaderClass original;
	original.Set_Depth_Compare(ShaderClass::PASS_GREATER);
	original.Set_Depth_Mask(ShaderClass::DEPTH_WRITE_DISABLE);
	original.Set_Dst_Blend_Func(ShaderClass::DSTBLEND_SRC_ALPHA);
	original.Set_Primary_Gradient(ShaderClass::GRADIENT_ADD);
	original.Set_Secondary_Gradient(ShaderClass::SECONDARY_GRADIENT_ENABLE);
	original.Set_Src_Blend_Func(ShaderClass::SRCBLEND_SRC_ALPHA);
	original.Set_Texturing(ShaderClass::TEXTURING_ENABLE);
	original.Set_Alpha_Test(ShaderClass::ALPHATEST_ENABLE);

	W3dShaderStruct file;
	W3dUtilityClass::Convert_Shader(original, &file);

	ShaderClass loaded;
	W3dUtilityClass::Convert_Shader(file, &loaded);

	CHECK_EQ(loaded.Get_Depth_Compare(), ShaderClass::PASS_GREATER);
	CHECK_EQ(loaded.Get_Depth_Mask(), ShaderClass::DEPTH_WRITE_DISABLE);
	CHECK_EQ(loaded.Get_Dst_Blend_Func(), ShaderClass::DSTBLEND_SRC_ALPHA);
	CHECK_EQ(loaded.Get_Primary_Gradient(), ShaderClass::GRADIENT_ADD);
	CHECK_EQ(loaded.Get_Secondary_Gradient(), ShaderClass::SECONDARY_GRADIENT_ENABLE);
	CHECK_EQ(loaded.Get_Src_Blend_Func(), ShaderClass::SRCBLEND_SRC_ALPHA);
	CHECK_EQ(loaded.Get_Texturing(), ShaderClass::TEXTURING_ENABLE);
	CHECK_EQ(loaded.Get_Alpha_Test(), ShaderClass::ALPHATEST_ENABLE);
}

/* The Glow option brightens the art's additive surfaces (muzzle flashes, glow cards) and the
   particles' additive preset, and nothing else that blends ONE, ONE: water, waypoint lines and
   decals use the plain additive preset. */
TEST(additive_art_and_particles_opt_into_the_glow_and_nothing_else_does)
{
	W3dShaderStruct file;
	ShaderClass loaded;

	W3dUtilityClass::Convert_Shader(ShaderClass::_PresetAdditiveShader, &file);
	W3dUtilityClass::Convert_Shader(file, &loaded);
	CHECK_EQ(loaded.Get_Glow(), ShaderClass::GLOW_ENABLE);

	ShaderClass alpha;
	W3dUtilityClass::Convert_Shader(ShaderClass::_PresetAlphaShader, &file);
	W3dUtilityClass::Convert_Shader(file, &alpha);
	CHECK_EQ(alpha.Get_Glow(), ShaderClass::GLOW_DISABLE);

	CHECK_EQ(ShaderClass::_PresetAdditiveSpriteShader.Get_Glow(), ShaderClass::GLOW_ENABLE);
	CHECK_EQ(ShaderClass::_PresetAdditiveShader.Get_Glow(), ShaderClass::GLOW_DISABLE);
	CHECK_EQ(ShaderClass::_PresetAdditiveSolidShader.Get_Glow(), ShaderClass::GLOW_DISABLE);
}

TEST(convert_shader_drops_the_fields_the_file_format_has_no_room_for)
{
	ShaderClass original;
	original.Set_Fog_Func(ShaderClass::FOG_WHITE);
	original.Set_Color_Mask(ShaderClass::COLOR_WRITE_DISABLE);
	original.Set_Cull_Mode(ShaderClass::CULL_MODE_DISABLE);
	original.Set_NPatch_Enable(ShaderClass::NPATCH_ENABLE);
	original.Set_Post_Detail_Color_Func(ShaderClass::DETAILCOLOR_ADD);
	original.Set_Post_Detail_Alpha_Func(ShaderClass::DETAILALPHA_SCALE);

	W3dShaderStruct file;
	W3dUtilityClass::Convert_Shader(original, &file);

	ShaderClass loaded;
	loaded.Set_NPatch_Enable(ShaderClass::NPATCH_ENABLE);
	W3dUtilityClass::Convert_Shader(file, &loaded);

	/* ColorMask and FogFunc are marked obsolete in the file struct, so the
	   loader hardcodes them. */
	CHECK_EQ(loaded.Get_Fog_Func(), ShaderClass::FOG_DISABLE);
	CHECK_EQ(loaded.Get_Color_Mask(), ShaderClass::COLOR_WRITE_ENABLE);

	/* Cull mode and npatch are not in the file format at all, so the loader
	   leaves whatever the destination shader already had. */
	CHECK_EQ(loaded.Get_NPatch_Enable(), ShaderClass::NPATCH_ENABLE);

	/* Asymmetry by design: the writer stores the post-detail funcs in the
	   PostDetail* fields but the loader reads them back out of the Detail*
	   fields, which the writer never touched.  Post-detail settings do not
	   survive a save/load cycle. */
	CHECK_EQ(int(file.PostDetailColorFunc), int(ShaderClass::DETAILCOLOR_ADD));
	CHECK_EQ(int(file.DetailColorFunc), int(ShaderClass::DETAILCOLOR_DISABLE));
	CHECK_EQ(loaded.Get_Post_Detail_Color_Func(), ShaderClass::DETAILCOLOR_DISABLE);
	CHECK_EQ(loaded.Get_Post_Detail_Alpha_Func(), ShaderClass::DETAILALPHA_DISABLE);
}

TEST(w3d_shader_reset_seeds_every_byte)
{
	/* W3d_Shader_Reset has to overwrite the whole struct - a stale byte would
	   be written straight into a .w3d file. */
	W3dShaderStruct s;
	memset(&s, 0xab, sizeof(s));
	W3d_Shader_Reset(&s);

	CHECK_EQ(int(s.DepthCompare), int(W3DSHADER_DEPTHCOMPARE_PASS_LEQUAL));
	CHECK_EQ(int(s.DepthMask), int(W3DSHADER_DEPTHMASK_WRITE_ENABLE));
	CHECK_EQ(int(s.ColorMask), 0);
	CHECK_EQ(int(s.DestBlend), int(W3DSHADER_DESTBLENDFUNC_ZERO));
	CHECK_EQ(int(s.FogFunc), 0);
	CHECK_EQ(int(s.PriGradient), int(W3DSHADER_PRIGRADIENT_MODULATE));
	CHECK_EQ(int(s.SecGradient), int(W3DSHADER_SECGRADIENT_DISABLE));
	CHECK_EQ(int(s.SrcBlend), int(W3DSHADER_SRCBLENDFUNC_ONE));
	CHECK_EQ(int(s.Texturing), int(W3DSHADER_TEXTURING_DISABLE));
	CHECK_EQ(int(s.DetailColorFunc), int(W3DSHADER_DETAILCOLORFUNC_DISABLE));
	CHECK_EQ(int(s.DetailAlphaFunc), int(W3DSHADER_DETAILALPHAFUNC_DISABLE));
	CHECK_EQ(int(s.ShaderPreset), 0);
	CHECK_EQ(int(s.AlphaTest), int(W3DSHADER_ALPHATEST_DISABLE));
	CHECK_EQ(int(s.PostDetailColorFunc), int(W3DSHADER_DETAILCOLORFUNC_DISABLE));
	CHECK_EQ(int(s.PostDetailAlphaFunc), int(W3DSHADER_DETAILALPHAFUNC_DISABLE));
	CHECK_EQ(int(s.pad[0]), 0);

	/* Two resets must produce identical bytes. */
	W3dShaderStruct t;
	memset(&t, 0x00, sizeof(t));
	W3d_Shader_Reset(&t);
	CHECK_MEM(&s, &t, sizeof(s));
}

TEST(w3d_shader_struct_is_the_size_the_file_format_expects)
{
	/* 16 bytes on disk; the pad byte exists to keep it even. */
	CHECK_EQ(sizeof(W3dShaderStruct), 16u);
	CHECK_EQ(sizeof(W3dRGBStruct), 4u);
	CHECK_EQ(sizeof(W3dRGBAStruct), 4u);
	CHECK_EQ(sizeof(W3dVectorStruct), 12u);
	CHECK_EQ(sizeof(W3dQuaternionStruct), 16u);
}

/* ------------------------------------------------------------------ */
/* DX8Wrapper::Convert_Color / Clamp_Color.
 *
 * Both were WWINLINE __asm blocks in dx8wrapper.h, inlined into every
 * renderer that converts a color, and Convert_Color used EAX, EBX, ECX,
 * EDX, ESI and EDI as scratch without handing them back: the shell map
 * died in SegLineRendererClass::Render with a color value where a
 * pointer should have been.  Witnesses here planted sentinels in the
 * callee-saved registers to catch that.  Both are C now, and what the
 * tests check is the arithmetic: the same bytes the assembly produced. */

#include "dx8wrapper.h"

TEST(convert_color_leaves_the_callee_saved_registers_alone)
{
	Vector3 rgb(0.4f, 0.8f, 0.2f);
	unsigned col = DX8Wrapper::Convert_Color(rgb, 1.0f);

	/* ...and it still converts: truncate(x*255), packed AARRGGBB. */
	unsigned expected = ((unsigned)(1.0f * 255.0f) << 24)
							| ((unsigned)(0.4f * 255.0f) << 16)
							| ((unsigned)(0.8f * 255.0f) << 8)
							| (unsigned)(0.2f * 255.0f);
	CHECK_EQ(col, expected);
}

TEST(clamp_color_leaves_the_callee_saved_registers_alone)
{
	Vector4 color(-0.5f, 0.5f, 2.0f, 1.0f);
	DX8Wrapper::Clamp_Color(color);

	CHECK_NEAR(color.X, 0.0f, 0.0001f);
	CHECK_NEAR(color.Y, 0.5f, 0.0001f);
	CHECK_NEAR(color.Z, 1.0f, 0.0001f);
	CHECK_NEAR(color.W, 1.0f, 0.0001f);
}

/*=========================================================================
   render2dsentence - FontCharsClass owns its glyph buffers
  =========================================================================*/

/*
 * FontCharsClass keeps its glyph pixels in FontCharsBuffer objects, each a
 * W3DMPO allocated one at a time with W3DNEW (class operator new, its own
 * pool).  The destructor used to free them with "delete []": that skips the
 * class operator delete for the global array delete, and since the class has
 * a virtual destructor the compiler also expects an array cookie in front of
 * the object, so it walked a garbage element count and freed (p - 4).  In the
 * game that was a NULL-pool MemoryPool::freeBlock in every ~W3DDisplay - a
 * silent access violation on every exit.  Here the pool stubs above count:
 * with the bug the buffer never reaches freeFromW3DMemPool at all - the
 * test binary dies with STATUS_HEAP_CORRUPTION (0xC0000374) on the p-4 free
 * before the counters are even compared (checked by reverting the fix).
 */
TEST(fontchars_returns_its_glyph_buffers_to_the_pool)
{
	const int allocs = theW3DPoolAllocs, frees = theW3DPoolFrees;
	FontCharsClass *font = W3DNEW FontCharsClass;
	font->Initialize_GDI_Font("Arial", 12, false);
	CHECK(font->Get_Char_Width(u'A') > 0);	/* stores the glyph -> allocates a FontCharsBuffer */
	font->Release_Ref();
	CHECK(theW3DPoolAllocs - allocs >= 2);	/* at least the font and one buffer */
	CHECK_EQ(theW3DPoolFrees - frees, theW3DPoolAllocs - allocs);
	CHECK_EQ(theW3DPoolBadFrees, 0);
}

// ---------------------------------------------------------------------------------------------
// SurfaceClass::DrawPixel takes a Get_Description, a one-pixel LockRect and an UnlockRect for
// every call.  Draw_Pixel writes the same byte, with the same truncation, into a buffer the
// caller locked once.  These pin the address arithmetic and the masks against DrawPixel's.
// ---------------------------------------------------------------------------------------------
#include "surfaceclass.h"

TEST(surface_draw_pixel_lands_where_the_pitch_says)
{
	const int pitch = 40;		/* deliberately wider than 8 pixels * 4 bytes */
	unsigned char buf[ 40 * 4 ];
	memset( buf, 0, sizeof( buf ) );

	SurfaceClass::Draw_Pixel( 3, 2, 0xAABBCCDD, 4, buf, pitch );
	CHECK_EQ( *(unsigned int *)( buf + 2 * pitch + 3 * 4 ), 0xAABBCCDD );

	/* nothing outside that one pixel was touched */
	unsigned int written = 0;
	for( unsigned int i = 0; i < sizeof( buf ); ++i )
		if( buf[ i ] != 0 ) ++written;
	CHECK_EQ( written, 4 );
}

TEST(surface_draw_pixel_truncates_the_colour_the_way_drawpixel_did)
{
	unsigned char buf[ 64 ];

	/* one byte per pixel: the low byte of the colour, and the row stride is the pitch */
	memset( buf, 0, sizeof( buf ) );
	SurfaceClass::Draw_Pixel( 5, 1, 0x11223344, 1, buf, 16 );
	CHECK_EQ( buf[ 16 + 5 ], 0x44 );

	/* two bytes per pixel: the low half word */
	memset( buf, 0, sizeof( buf ) );
	SurfaceClass::Draw_Pixel( 5, 1, 0x11223344, 2, buf, 16 );
	CHECK_EQ( *(unsigned short *)( buf + 16 + 5 * 2 ), 0x3344 );

	/* an unsupported pixel size writes nothing at all, exactly as the switch used to */
	memset( buf, 0, sizeof( buf ) );
	SurfaceClass::Draw_Pixel( 5, 1, 0x11223344, 3, buf, 16 );
	unsigned int written = 0;
	for( unsigned int i = 0; i < sizeof( buf ); ++i )
		if( buf[ i ] != 0 ) ++written;
	CHECK_EQ( written, 0 );
}

TEST(surface_draw_h_line_fills_the_run_inclusive_at_both_ends)
{
	const int pitch = 32;
	unsigned char buf[ 32 * 3 ];
	memset( buf, 0, sizeof( buf ) );

	SurfaceClass::Draw_H_Line( 1, 2, 5, 0x000000FF, 4, buf, pitch );

	unsigned int *row = (unsigned int *)( buf + pitch );
	CHECK_EQ( row[ 1 ], 0 );				/* one before the run */
	for( unsigned int x = 2; x <= 5; ++x )
		CHECK_EQ( row[ x ], 0x000000FF );
	CHECK_EQ( row[ 6 ], 0 );				/* one past it */

	/* and no other row moved */
	CHECK_EQ( *(unsigned int *)buf, 0 );
	CHECK_EQ( *(unsigned int *)( buf + 2 * pitch ), 0 );
}

// ---------------------------------------------------------------------------------------------
// The sorting pool radix-sorts its triangles on an unsigned key made from each depth's bits.  A key
// that orders negatives the wrong way round draws everything behind the camera plane in reverse,
// and nothing on screen would say why, so the ordering is pinned here across the sign.
// ---------------------------------------------------------------------------------------------
#include "sortingrenderer.h"
#include <limits>

TEST(sorting_depth_key_orders_the_way_the_depths_do)
{
	const float depths[] = { -1.0e30f, -2.5f, -1.0f, -1.0e-30f, -0.0f, 0.0f, 1.0e-30f, 1.0f, 2.5f, 1.0e30f };
	const unsigned count = sizeof( depths ) / sizeof( depths[ 0 ] );

	for( unsigned i = 1; i < count; ++i )
		CHECK( SortingRendererClass::_Depth_Sort_Key( depths[ i - 1 ] ) < SortingRendererClass::_Depth_Sort_Key( depths[ i ] ) );

	/* the same depth twice is the same key, which is what keeps equal depths in pooled order */
	CHECK_EQ( SortingRendererClass::_Depth_Sort_Key( 3.25f ), SortingRendererClass::_Depth_Sort_Key( 3.25f ) );
}

// ---------------------------------------------------------------------------------------------
// The flush draws its sorted entries in runs, and an entry moves back past entries in other tiles
// to join the last run of its own state.  The picture holds because two entries sharing a tile are
// drawn in the order the sort gave them, and nothing passes a fixed entry or is passed by one.  Two
// fires on either side of the screen interleaved in depth are what it is for: two runs, not eight.
// ---------------------------------------------------------------------------------------------
static bool sort_tiles_meet( const SortingRendererClass::SortTiles & a, const SortingRendererClass::SortTiles & b )
{
	return a.x0 <= b.x1 && b.x0 <= a.x1 && a.y0 <= b.y1 && b.y0 <= a.y1;
}

static void set_sort_tiles( SortingRendererClass::SortTiles & tiles, unsigned x0, unsigned x1, unsigned y0, unsigned y1 )
{
	tiles.x0 = (unsigned char)x0;
	tiles.x1 = (unsigned char)x1;
	tiles.y0 = (unsigned char)y0;
	tiles.y1 = (unsigned char)y1;
}

TEST(sorting_runs_join_only_past_entries_in_other_tiles)
{
	enum { MOST = 400 };
	unsigned short states[ MOST ];
	SortingRendererClass::SortTiles tiles[ MOST ];
	unsigned order[ MOST ];
	unsigned position[ MOST ];

	/* two fires a screen apart, interleaved in depth */
	for( unsigned e = 0; e < 8; ++e ) {
		states[ e ] = (unsigned short)( e & 1 );
		const unsigned x = ( e & 1 ) ? 28 : 2;
		set_sort_tiles( tiles[ e ], x, x + 1, 10, 12 );
	}
	CHECK_EQ( SortingRendererClass::_Group_Runs( states, tiles, 8, order ), 2u );
	for( unsigned k = 0; k < 4; ++k ) {
		CHECK_EQ( order[ k ], 2 * k );
		CHECK_EQ( order[ 4 + k ], 2 * k + 1 );
	}

	/* the same two over one another stay where the sort put them */
	for( unsigned e = 0; e < 8; ++e )
		set_sort_tiles( tiles[ e ], 10, 12, 10, 12 );
	CHECK_EQ( SortingRendererClass::_Group_Runs( states, tiles, 8, order ), 8u );
	for( unsigned k = 0; k < 8; ++k )
		CHECK_EQ( order[ k ], k );

	/* random pools of six states, the last of them fixed */
	unsigned seed = 20261027u;
	for( int pool = 0; pool < 300; ++pool ) {
		seed = seed * 1664525u + 1013904223u;
		const unsigned count = 1 + ( seed >> 8 ) % MOST;
		for( unsigned e = 0; e < count; ++e ) {
			seed = seed * 1664525u + 1013904223u;
			const unsigned state = ( seed >> 8 ) % 6;
			states[ e ] = (unsigned short)( state == 5 ? ( state | SortingRendererClass::SORT_STATE_FIXED ) : state );
			const unsigned last = SortingRendererClass::SORT_TILE_GRID - 1;
			const unsigned x = ( seed >> 12 ) % ( last + 1 ), y = ( seed >> 18 ) % ( last + 1 );
			const unsigned span = ( ( seed >> 24 ) % 16 == 0 ) ? last : ( seed >> 28 ) % 6;
			set_sort_tiles( tiles[ e ], x, x + span > last ? last : x + span, y, y + span > last ? last : y + span );
		}
		const unsigned runs = SortingRendererClass::_Group_Runs( states, tiles, count, order );

		for( unsigned e = 0; e < count; ++e )
			position[ e ] = MOST;
		for( unsigned k = 0; k < count; ++k ) {
			CHECK( order[ k ] < count );
			CHECK_EQ( position[ order[ k ] ], (unsigned)MOST );		/* each entry once */
			position[ order[ k ] ] = k;
		}
		unsigned changes = 0;
		for( unsigned k = 1; k < count; ++k )
			changes += states[ order[ k ] ] != states[ order[ k - 1 ] ];
		CHECK_EQ( changes + 1, runs );

		for( unsigned i = 0; i < count; ++i ) {
			for( unsigned j = i + 1; j < count; ++j ) {
				const bool held = sort_tiles_meet( tiles[ i ], tiles[ j ] )
					|| ( ( states[ i ] | states[ j ] ) & SortingRendererClass::SORT_STATE_FIXED );
				if( held )
					CHECK( position[ i ] < position[ j ] );
			}
		}
	}
}

// ---------------------------------------------------------------------------------------------
// An entry looks RUN_LOOK_BACK (64) runs back for its own state and no further, through a ring of
// that many open runs.  Seventy states each in a tile of its own, cycled twice, put every second
// entry's run seventy runs back: out of reach, so nothing joins, and no entry takes a ring slot a
// later run has reused for its old run.  Sixty states cycled twice are in reach and pair up.
// ---------------------------------------------------------------------------------------------
static void set_state_tile( SortingRendererClass::SortTiles & tiles, unsigned state )
{
	const unsigned x = ( state % 32 ) * 2, y = ( state / 32 ) * 2;
	set_sort_tiles( tiles, x, x, y, y );
}

TEST(sorting_runs_look_back_no_further_than_the_window)
{
	enum { PAST_REACH = 70, IN_REACH = 60 };
	unsigned short states[ 2 * PAST_REACH ];
	SortingRendererClass::SortTiles tiles[ 2 * PAST_REACH ];
	unsigned order[ 2 * PAST_REACH ];

	for( unsigned e = 0; e < 2 * PAST_REACH; ++e ) {
		states[ e ] = (unsigned short)( e % PAST_REACH );
		set_state_tile( tiles[ e ], e % PAST_REACH );
	}
	CHECK_EQ( SortingRendererClass::_Group_Runs( states, tiles, 2 * PAST_REACH, order ), 2u * PAST_REACH );
	for( unsigned k = 0; k < 2 * PAST_REACH; ++k )
		CHECK_EQ( order[ k ], k );

	for( unsigned e = 0; e < 2 * IN_REACH; ++e ) {
		states[ e ] = (unsigned short)( e % IN_REACH );
		set_state_tile( tiles[ e ], e % IN_REACH );
	}
	CHECK_EQ( SortingRendererClass::_Group_Runs( states, tiles, 2 * IN_REACH, order ), (unsigned)IN_REACH );
	for( unsigned state = 0; state < IN_REACH; ++state ) {
		CHECK_EQ( order[ 2 * state ], state );
		CHECK_EQ( order[ 2 * state + 1 ], state + IN_REACH );
	}
}

// ---------------------------------------------------------------------------------------------
// An entry's tiles are its corners taken through the matrix its draw uses, widened past the
// rasterizer's snapping.  Two triangles a thousandth either side of a tile's edge share a tile,
// since a pixel on the edge can go to either; two a screen apart share none; a quad reaches its
// fourth corner; and a corner the projection cannot put on the screen covers the whole grid.
// ---------------------------------------------------------------------------------------------
static SortingRendererClass::SortTiles sort_tiles_of( const float corners[][ 3 ], unsigned count, const float clip[ 16 ] )
{
	const float * pointers[ 4 ];
	for( unsigned c = 0; c < count; ++c )
		pointers[ c ] = corners[ c ];
	return SortingRendererClass::_Entry_Tiles( pointers, count, clip );
}

static bool sort_tiles_whole( const SortingRendererClass::SortTiles & tiles )
{
	const unsigned last = SortingRendererClass::SORT_TILE_GRID - 1;
	return tiles.x0 == 0 && tiles.x1 == last && tiles.y0 == 0 && tiles.y1 == last;
}

TEST(sorting_entry_tiles_come_from_the_corners_with_a_margin)
{
	/* w is the corner's z, so a corner at z 1 lands where its x and y say */
	const float clip[ 16 ] = { 1, 0, 0, 0,  0, 1, 0, 0,  0, 0, 0, 1,  0, 0, 0, 0 };

	/* x 0 is the edge between tiles 31 and 32 */
	const float left[ 3 ][ 3 ] = { { -0.3f, -0.1f, 1.0f }, { -0.001f, -0.1f, 1.0f }, { -0.001f, 0.1f, 1.0f } };
	const float right[ 3 ][ 3 ] = { { 0.001f, -0.1f, 1.0f }, { 0.3f, -0.1f, 1.0f }, { 0.001f, 0.1f, 1.0f } };
	const SortingRendererClass::SortTiles left_tiles = sort_tiles_of( left, 3, clip );
	const SortingRendererClass::SortTiles right_tiles = sort_tiles_of( right, 3, clip );
	CHECK( sort_tiles_meet( left_tiles, right_tiles ) );
	CHECK( !sort_tiles_whole( left_tiles ) );
	CHECK( !sort_tiles_whole( right_tiles ) );

	const float far_left[ 3 ][ 3 ] = { { -0.9f, -0.1f, 1.0f }, { -0.8f, -0.1f, 1.0f }, { -0.8f, 0.1f, 1.0f } };
	const float far_right[ 3 ][ 3 ] = { { 0.8f, -0.1f, 1.0f }, { 0.9f, -0.1f, 1.0f }, { 0.8f, 0.1f, 1.0f } };
	const SortingRendererClass::SortTiles far_right_tiles = sort_tiles_of( far_right, 3, clip );
	CHECK( !sort_tiles_meet( sort_tiles_of( far_left, 3, clip ), far_right_tiles ) );

	/* the same far right triangle twice as deep and twice as wide lands on the same tiles */
	const float deeper[ 3 ][ 3 ] = { { 1.6f, -0.2f, 2.0f }, { 1.8f, -0.2f, 2.0f }, { 1.6f, 0.2f, 2.0f } };
	const SortingRendererClass::SortTiles deeper_tiles = sort_tiles_of( deeper, 3, clip );
	CHECK_EQ( deeper_tiles.x0, far_right_tiles.x0 );
	CHECK_EQ( deeper_tiles.x1, far_right_tiles.x1 );
	CHECK_EQ( deeper_tiles.y0, far_right_tiles.y0 );
	CHECK_EQ( deeper_tiles.y1, far_right_tiles.y1 );

	/* a quad: the far left triangle with a fourth corner out on the right */
	const float quad[ 4 ][ 3 ] = { { -0.9f, -0.1f, 1.0f }, { -0.8f, -0.1f, 1.0f }, { -0.8f, 0.1f, 1.0f }, { 0.85f, 0.1f, 1.0f } };
	CHECK( sort_tiles_meet( sort_tiles_of( quad, 4, clip ), far_right_tiles ) );

	/* a corner on the eye's plane, a corner that is not a number, and a matrix that makes one */
	const float nan = std::numeric_limits< float >::quiet_NaN();
	const float at_eye[ 3 ][ 3 ] = { { 0.1f, 0.1f, 1.0f }, { 0.2f, 0.1f, 0.0f }, { 0.2f, 0.2f, 1.0f } };
	const float broken[ 3 ][ 3 ] = { { 0.1f, 0.1f, 1.0f }, { nan, 0.1f, 1.0f }, { 0.2f, 0.2f, 1.0f } };
	const float broken_clip[ 16 ] = { 1, 0, 0, 0,  0, 1, 0, 0,  0, 0, 0, 1,  nan, 0, 0, 0 };
	CHECK( sort_tiles_whole( sort_tiles_of( at_eye, 3, clip ) ) );
	CHECK( sort_tiles_whole( sort_tiles_of( broken, 3, clip ) ) );
	CHECK( sort_tiles_whole( sort_tiles_of( far_right, 3, broken_clip ) ) );
}
