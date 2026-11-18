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

#include "dx11post.h"
#include "postshader.h"

#include "dx11device.h"
#include "dx11resource.h"
#include "dx11runtime.h"

#include <stdio.h>
#include <string.h>
#include <string>
#include <windows.h>

typedef HRESULT (WINAPI *D3DCompileFunction)(LPCVOID source_data, SIZE_T source_size,
	LPCSTR source_name, const D3D_SHADER_MACRO * defines, ID3DInclude * include,
	LPCSTR entry_point, LPCSTR target, UINT flags1, UINT flags2, ID3DBlob ** code,
	ID3DBlob ** error_messages);

// The same hand binding dx11backend.cpp does, and deliberately its own copy: this is a separate
// static library and linking it against the backend to share twelve lines would drag the whole
// state translation in behind them.
static const char * const COMPILER_MODULE = "d3dcompiler_47.dll";
static const char * const ENTRY_POINT = "main";
static const char * const VERTEX_PROFILE = "vs_4_0";
static const char * const PIXEL_PROFILE = "ps_4_0";

static const unsigned FULL_SCREEN_VERTEX_COUNT = 3;

// The swap chain's own format for the eight bit stages, so the last pass in a chain writes what it
// reads and nothing converts anything on the way out.
static const DXGI_FORMAT EIGHT_BIT_FORMAT = DXGI_FORMAT_B8G8R8A8_UNORM;

// Half floats rather than full ones: the scene is a colour, sixteen bits of it are more range than
// this game's art can fill, and it halves the bandwidth of every pass over the frame.
static const DXGI_FORMAT FLOAT_FORMAT = DXGI_FORMAT_R16G16B16A16_FLOAT;

// Bloom runs at a quarter of the window on each axis.  A glow is low frequency by definition, so
// the resolution it is computed at is the one thing about it nobody can see, and a quarter costs a
// sixteenth of the samples.
static const unsigned BLOOM_DIVISOR = 4;

// Pairs of blur passes, one horizontal and one vertical, the n-th pair stepping n texels between
// its taps.  Two reach about fifteen pixels of the finished frame; the Glow option's Medium asks
// for three, High and Ultra for four, about twenty five and thirty five.  More than four and a
// fire's halo covers the units fighting in it.
static const unsigned BLOOM_BLUR_PASSES = 2;
static const unsigned BLOOM_BLUR_PASS_LIMIT = 4;

// How far past the threshold the bright pass takes to reach full strength.  A pixel just over it
// keeps the square of its excess, so a flame flickering across the threshold fades its glow in
// and out instead of switching it.
static const float BLOOM_KNEE = 0.5f;

// What counts as bright enough to bleed, what the bleed is worth when it is added back, and where
// the tone curve starts bending.
//
// A threshold of one means brighter than white, which is a thing an eight bit target could not have
// said at all, and it was worth checking that the game ever gets there rather than assuming it.
// Eight inferno cannons firing into a column does not: at a threshold of one the frame comes back
// unchanged even with the intensity at four, because a fire a few pixels across never stacks enough
// additive particles to pass white.  Thirty of them does, and at the same threshold of one the
// fireballs come back with white centres and a glow on the ground around them.  So the float target
// earns its place, and the threshold stays where it is: a lower one blooms pale buildings and the
// sky, which is the look this is trying not to have.
//
// An intensity of 1.5 against a quiet frame with nothing burning costs half a level a channel,
// which is the cost of having this on when there is nothing for it to do.
//
// Those two are the defaults.  The game replaces them every frame from its Glow option, which
// also raises what an additive draw is worth (DX11Post_Additive_Gain): that, and not a lower
// threshold, is what gets fire and lasers past white while the art stays under it.
static const float BLOOM_THRESHOLD = 1.0f;
static const float BLOOM_INTENSITY = 1.5f;
static const float TONE_CURVE_KNEE = 0.8f;

static float BloomThreshold = BLOOM_THRESHOLD;
static float BloomIntensity = BLOOM_INTENSITY;
static float BloomAdditiveGain = 1.0f;
static unsigned BloomBlurPasses = BLOOM_BLUR_PASSES;
static float BloomSaturation = 1.0f;

// The bright pass's colour is pushed this far from its own grey at most.  ZH Enhanced's ReShade bloom
// runs at 1.53; past about 3 a fire's halo turns a red no flame has.
static const float BLOOM_SATURATION_LIMIT = 3.0f;

void DX11Post_Set_Bloom(float threshold, float intensity, float additive_gain, unsigned blur_passes,
	float saturation)
{
	BloomThreshold = (threshold > 0.0f) ? threshold : 0.0f;
	BloomIntensity = (intensity > 0.0f) ? intensity : 0.0f;
	BloomAdditiveGain = (additive_gain > 1.0f) ? additive_gain : 1.0f;
	BloomBlurPasses = (blur_passes < 1) ? 1
		: (blur_passes > BLOOM_BLUR_PASS_LIMIT) ? BLOOM_BLUR_PASS_LIMIT : blur_passes;
	BloomSaturation = (saturation < 0.0f) ? 0.0f
		: (saturation > BLOOM_SATURATION_LIMIT) ? BLOOM_SATURATION_LIMIT : saturation;
}

// Clarity: local contrast on the luminance, measured against a blur of it about fifteen pixels wide
// at 1080p (two of the bloom's blur pairs at a quarter of the window), so what it lifts is the shape
// of a tank, a crater or a cliff rather than the grain of a texture, which the sharpen pass owns.
// Zero, the default and what the tests start from, is no pass at all and leaves the bloom's programs
// drawing what they drew.  The game sets it every frame from the LightingStyle option.
static const unsigned CLARITY_BLUR_PASSES = 2;
static const float CLARITY_LIMIT = 2.0f;
static float ClarityStrength = 0.0f;

void DX11Post_Set_Clarity(float strength)
{
	ClarityStrength = (strength < 0.0f) ? 0.0f : (strength > CLARITY_LIMIT) ? CLARITY_LIMIT : strength;
}

float DX11Post_Additive_Gain()
{
	return (BloomIntensity > 0.0f) ? BloomAdditiveGain : 1.0f;
}

// How far the grade bends the picture at full strength: the power the S raises each side of its
// pivot to, less one, what is left of the colour around its grey, and how far a channel moves warm
// or cool for each step away from the pivot.
static const float GRADE_CONTRAST = 0.18f;
static const float GRADE_SATURATION = 0.86f;
static const float GRADE_SPLIT = 0.05f;

static float GradeAmount = 0.0f;

void DX11Post_Set_Grade(float amount)
{
	GradeAmount = (amount < 0.0f) ? 0.0f : (amount > 1.0f) ? 1.0f : amount;
}

static bool InternalHdr = false;

void DX11Post_Set_Internal_Hdr(bool on)
{
	InternalHdr = on;
}

static float HazeColour[3] = { 0.0f, 0.0f, 0.0f };
static float HazeTop = 0.0f;
static float HazeThickness = 1.0f;
static float HazeStrength = 0.0f;
static float HazeReach = 1.0f;
static float HazeWater = -1.0e6f;

static float HazeSun[4] = { 0.0f, 0.0f, 1.0f, 0.0f };

void DX11Post_Set_Haze(const float colour[3], float top, float thickness, float strength, float reach,
	float water, const float toward_sun[3], float warmth)
{
	HazeWater = water;
	HazeColour[0] = colour[0];
	HazeColour[1] = colour[1];
	HazeColour[2] = colour[2];
	HazeTop = top;
	HazeThickness = thickness;
	HazeStrength = strength;
	HazeReach = reach;
	HazeSun[0] = toward_sun[0];
	HazeSun[1] = toward_sun[1];
	HazeSun[2] = toward_sun[2];
	HazeSun[3] = warmth;
}

static ID3D11ShaderResourceView * HazeShroudSource = NULL;
static float HazeShroudRect[4] = { 0.0f, 0.0f, 1.0f, 1.0f };

void DX11Post_Set_Haze_Shroud(ID3D11ShaderResourceView * view, const float rect[4])
{
	HazeShroudSource = view;
	memcpy(HazeShroudRect, rect, sizeof(HazeShroudRect));
}

static float AtmosphereHorizon[3] = { 0.0f, 0.0f, 0.0f };
static float AtmosphereTint[3] = { 0.0f, 0.0f, 0.0f };
static float AtmosphereFade[4] = { 0.0f, 1.0f, 0.0f, 0.0f };
static float AtmosphereMapSize[2] = { 0.0f, 0.0f };
static ID3D11ShaderResourceView * AtmosphereSky = NULL;

void DX11Post_Set_Atmosphere(const float horizon[3], const float tint[3], float start, float scale,
	float strength, float ground, float map_width, float map_height, ID3D11ShaderResourceView * sky)
{
	AtmosphereMapSize[0] = map_width;
	AtmosphereMapSize[1] = map_height;
	memcpy(AtmosphereHorizon, horizon, sizeof(AtmosphereHorizon));
	memcpy(AtmosphereTint, tint, sizeof(AtmosphereTint));
	AtmosphereFade[0] = start;
	AtmosphereFade[1] = scale;
	AtmosphereFade[2] = strength;
	AtmosphereFade[3] = ground;
	AtmosphereSky = sky;
}

static DX11PostWarp Warps[DX11_POST_WARP_LIMIT];
static unsigned WarpCount = 0;

void DX11Post_Set_Warps(const DX11PostWarp * warps, unsigned count)
{
	WarpCount = (count > DX11_POST_WARP_LIMIT) ? DX11_POST_WARP_LIMIT : count;
	for (unsigned index = 0; index < WarpCount; ++index) {
		Warps[index] = warps[index];
	}
}

// How dark a fully occluded pixel goes, how far a neighbour may be in front before it counts as a
// different object rather than a corner, and how wide the ring reaches at one unit of depth.  The
// clip planes come from the frame itself.
static const float AO_STRENGTH = 0.45f;
static const float AO_RADIUS = 14.0f;
static const float AO_SPREAD = 26.0f;

struct PostConstantBlock
{
	// One over the size of the texture being sampled, and its size.  Per pass rather than per
	// frame, because the bloom passes do not run at the size of the window.
	float TexelSize[4];

	// The step a separable blur takes, already in texture coordinates.  Then two numbers the
	// prologue's text could not grow fields for without moving every cached program's key: z is the
	// bloom's saturation, which the bright pass reads; w is the clarity's strength in the composite,
	// and in a blur pass whether it carries the clarity's luminance along (above nought) or leaves it.
	float BlurDirection[4];

	// Threshold, intensity, knee.
	float Tuning[4];

	// How many warps, and the width of the frame over its height.  Then per warp the centre, the
	// radius and the pull, and the ring's radius, width and strength.
	float WarpInfo[4];
	float WarpCentre[DX11_POST_WARP_LIMIT][4];
	float WarpRing[DX11_POST_WARP_LIMIT][4];

	// The grade's amount, contrast, saturation and warm/cool split.
	float Grade[4];

	// The ground haze: its top, thickness, strength and reach, its colour, and the matrix taking
	// the frame's clip space back to the world.  Only the haze program declares them (at c13, in a
	// cbuffer of its own over the same buffer), so every other pass compiles from the text it
	// always did.
	float Haze[4];
	float HazeColour[4];
	float WorldFromClip[16];
	// Then the way toward the sun with its warmth.
	float HazeSun[4];
	// The shroud light's world rectangle (origin, one over size).
	float HazeShroudRect[4];
	// How the scene's projection scales x and y, for the occlusion in world units to find its reach
	// on the screen.  Only that program declares it.
	float WorldAoProjection[4];
};

// A triangle big enough to cover the screen, made out of nothing.  No vertex buffer and no input
// layout: SV_VertexID walks 0, 1, 2 and the arithmetic turns them into (0,0), (2,0), (0,2) in
// texture coordinates, whose triangle covers the whole of the unit square.
static const char * const VERTEX_SHADER_SOURCE = PostShader_Vertex_Source();
static const char * const PIXEL_SHADER_PROLOGUE = PostShader_Pixel_Prologue();
static const char * const COPY_SHADER_BODY = PostShader_Pixel_Body(POST_SHADER_COPY);
static const char * const TONE_MAP_SHADER_BODY = PostShader_Pixel_Body(POST_SHADER_TONE_MAP);
static const char * const TONE_MAP_WIDE_SHADER_BODY = PostShader_Pixel_Body(POST_SHADER_TONE_MAP_WIDE);
static const char * const FXAA_SHADER_BODY = PostShader_Pixel_Body(POST_SHADER_FXAA);
static const char * const SHARPEN_SHADER_BODY = PostShader_Pixel_Body(POST_SHADER_SHARPEN);
static const char * const SHARPEN_MULTISAMPLED_SHADER_BODY = PostShader_Pixel_Body(POST_SHADER_SHARPEN_MULTISAMPLED);
static const char * const AO_SHADER_BODY = PostShader_Pixel_Body(POST_SHADER_AO);
static const char * const AO_SCENE_SHADER_BODY = PostShader_Pixel_Body(POST_SHADER_AO_SCENE);
static const char * const WORLD_AO_SHADER_BODY = PostShader_Pixel_Body(POST_SHADER_WORLD_AO);
static const char * const WORLD_AO_BLUR_SHADER_BODY = PostShader_Pixel_Body(POST_SHADER_WORLD_AO_BLUR);
static const char * const WORLD_AO_UPSAMPLE_SHADER_BODY = PostShader_Pixel_Body(POST_SHADER_WORLD_AO_UPSAMPLE);
static const char * const HAZE_SHADER_BODY = PostShader_Pixel_Body(POST_SHADER_HAZE);
static const char * const SKY_SHADER_BODY = PostShader_Pixel_Body(POST_SHADER_SKY);
static const char * const ATMOSPHERE_SHADER_BODY = PostShader_Pixel_Body(POST_SHADER_ATMOSPHERE);
static const char * const BLOOM_EXTRACT_SHADER_BODY = PostShader_Pixel_Body(POST_SHADER_BLOOM_EXTRACT);
static const char * const BLOOM_BLUR_SHADER_BODY = PostShader_Pixel_Body(POST_SHADER_BLOOM_BLUR);
static const char * const BLOOM_COMPOSITE_SHADER_BODY = PostShader_Pixel_Body(POST_SHADER_BLOOM_COMPOSITE);
static const char * const BLOOM_COMPOSITE_WIDE_SHADER_BODY = PostShader_Pixel_Body(POST_SHADER_BLOOM_COMPOSITE_WIDE);
static const char * const WARP_SHADER_BODY = PostShader_Pixel_Body(POST_SHADER_WARP);
static const char * const HEAT_SPLAT_VERTEX_SOURCE = PostShader_Heat_Splat_Vertex_Source();
static const char * const HEAT_SPLAT_SHADER_BODY = PostShader_Pixel_Body(POST_SHADER_HEAT_SPLAT);
static const char * const HEAT_HAZE_SHADER_BODY = PostShader_Pixel_Body(POST_SHADER_HEAT_HAZE);

static D3DCompileFunction compiler_function()
{
	static D3DCompileFunction compiler = NULL;
	static bool attempted = false;
	if (!attempted) {
		attempted = true;
		HMODULE module = LoadLibraryA(COMPILER_MODULE);
		if (module != NULL) {
			compiler = reinterpret_cast<D3DCompileFunction>(GetProcAddress(module, "D3DCompile"));
		}
	}
	return compiler;
}

static void release_interface(IUnknown ** object)
{
	if (*object != NULL) {
		(*object)->Release();
		*object = NULL;
	}
}

static const char * simple_effect_body(DX11PostEffect effect)
{
	switch (effect) {
	case DX11_POST_FXAA:
		return FXAA_SHADER_BODY;
	case DX11_POST_SHARPEN:
		return SHARPEN_SHADER_BODY;
	case DX11_POST_AO:
		return AO_SHADER_BODY;
	default:
		return COPY_SHADER_BODY;
	}
}

DX11PostProcessClass::DX11PostProcessClass()
	: Device(NULL)
	, VertexShader(NULL)
	, ToneMapShader(NULL)
	, ToneMapWideShader(NULL)
	, SharpenMultisampledShader(NULL)
	, BloomExtractShader(NULL)
	, NearPlane(1.0f)
	, FarPlane(1200.0f)
	, BloomBlurShader(NULL)
	, BloomCompositeShader(NULL)
	, BloomCompositeWideShader(NULL)
	, WarpShader(NULL)
	, SceneOcclusionShader(NULL)
	, HazeShader(NULL)
	, WrapSampler(NULL)
	, Sampler(NULL)
	, BlendState(NULL)
	, MultiplyBlendState(NULL)
	, AlphaBlendState(NULL)
	, DepthState(NULL)
	, RasterizerState(NULL)
	, ConstantBuffer(NULL)
	, ChainLength(0)
	, Width(0)
	, Height(0)
	, BloomWidth(0)
	, BloomHeight(0)
	, TargetSamples(1)
	, TargetsAreFloat(false)
	, ShadersReady(false)
	, Resolved(false)
	, Occluded(false)
	, Hazed(false)
	, HazeRefused(false)
	, HazeShroudView(NULL)
	, AtmosphereRefused(false)
	, WorldAoRefused(false)
	, HeatSplatBuffer(NULL)
	, HeatSplatView(NULL)
	, HeatSplatVertexShader(NULL)
	, HeatSplatShader(NULL)
	, HeatHazeShader(NULL)
	, HeatBlendState(NULL)
	, HeatHazeRefused(false)
{
	memset(&HeatTarget, 0, sizeof(HeatTarget));
	memset(HazeWorldFromClip, 0, sizeof(HazeWorldFromClip));
	memset(WorldAoShaders, 0, sizeof(WorldAoShaders));
	memset(AtmosphereShaders, 0, sizeof(AtmosphereShaders));
	memset(WorldAoTargets, 0, sizeof(WorldAoTargets));
	memset(WorldAoProjection, 0, sizeof(WorldAoProjection));
	memset(PixelShaders, 0, sizeof(PixelShaders));
	memset(&SceneTarget, 0, sizeof(SceneTarget));
	memset(&SceneResolve, 0, sizeof(SceneResolve));
	memset(&SceneCopy, 0, sizeof(SceneCopy));
	memset(ChainTargets, 0, sizeof(ChainTargets));
	memset(BloomTargets, 0, sizeof(BloomTargets));
	memset(Chain, 0, sizeof(Chain));
	strcpy(Diagnostic, "post-process off");
}

DX11PostProcessClass::~DX11PostProcessClass()
{
	Shutdown();
}

bool DX11PostProcessClass::Initialise(DX11DeviceClass * device)
{
	Shutdown();
	Device = device;
	if (Device == NULL || Device->Get_Device() == NULL) {
		strcpy(Diagnostic, "post-process off: no device");
		return false;
	}

	ShadersReady = Create_Shaders() && Create_States();
	if (!ShadersReady) {
		Shutdown();
		strcpy(Diagnostic, "post-process off: the compiler refused the passes");
		return false;
	}

	Describe();
	return true;
}

void DX11PostProcessClass::Shutdown()
{
	Release_Targets();
	Release_Heat_Haze();
	HeatSplats.clear();
	HeatHazeRefused = false;
	HazeShroudView = NULL;
	HazeShroudSource = NULL;
	AtmosphereSky = NULL;
	for (unsigned index = 0; index < ATMOSPHERE_PROGRAMS; ++index) {
		release_interface(reinterpret_cast<IUnknown **>(&AtmosphereShaders[index]));
	}
	release_interface(reinterpret_cast<IUnknown **>(&WrapSampler));
	release_interface(reinterpret_cast<IUnknown **>(&ConstantBuffer));
	release_interface(reinterpret_cast<IUnknown **>(&RasterizerState));
	release_interface(reinterpret_cast<IUnknown **>(&DepthState));
	release_interface(reinterpret_cast<IUnknown **>(&AlphaBlendState));
	release_interface(reinterpret_cast<IUnknown **>(&MultiplyBlendState));
	release_interface(reinterpret_cast<IUnknown **>(&BlendState));
	release_interface(reinterpret_cast<IUnknown **>(&Sampler));
	release_interface(reinterpret_cast<IUnknown **>(&HazeShader));
	for (unsigned index = 0; index < WORLD_AO_PROGRAMS; ++index) {
		release_interface(reinterpret_cast<IUnknown **>(&WorldAoShaders[index]));
	}
	release_interface(reinterpret_cast<IUnknown **>(&SceneOcclusionShader));
	release_interface(reinterpret_cast<IUnknown **>(&WarpShader));
	release_interface(reinterpret_cast<IUnknown **>(&BloomCompositeWideShader));
	release_interface(reinterpret_cast<IUnknown **>(&BloomCompositeShader));
	release_interface(reinterpret_cast<IUnknown **>(&BloomBlurShader));
	release_interface(reinterpret_cast<IUnknown **>(&BloomExtractShader));
	release_interface(reinterpret_cast<IUnknown **>(&ToneMapWideShader));
	release_interface(reinterpret_cast<IUnknown **>(&ToneMapShader));
	release_interface(reinterpret_cast<IUnknown **>(&SharpenMultisampledShader));
	for (unsigned index = 0; index < DX11_POST_SIMPLE_EFFECTS; ++index) {
		release_interface(reinterpret_cast<IUnknown **>(&PixelShaders[index]));
	}
	release_interface(reinterpret_cast<IUnknown **>(&VertexShader));
	Device = NULL;
	ShadersReady = false;
	Resolved = false;
	HazeRefused = false;
	AtmosphereRefused = false;
	WorldAoRefused = false;
}

void DX11PostProcessClass::Set_Chain(const DX11PostEffect effects[DX11_POST_CHAIN_LIMIT],
	unsigned count)
{
	ChainLength = (count > DX11_POST_CHAIN_LIMIT) ? DX11_POST_CHAIN_LIMIT : count;
	for (unsigned index = 0; index < ChainLength; ++index) {
		Chain[index] = effects[index];
	}
	Describe();
}

bool DX11PostProcessClass::Scene_Is_Float() const
{
	return ChainLength > 0 && Chain[0] == DX11_POST_BLOOM;
}

static bool compile_pixel_shader(D3DCompileFunction compiler, ID3D11Device * device,
	const char * body, ID3D11PixelShader ** shader)
{
	const std::string source = std::string(PIXEL_SHADER_PROLOGUE) + body;
	ID3DBlob * code = NULL;
	ID3DBlob * errors = NULL;
	HRESULT result = compiler(source.c_str(), source.size(), "dx11post.ps", NULL, NULL, ENTRY_POINT,
		PIXEL_PROFILE, 0, 0, &code, &errors);
	release_interface(reinterpret_cast<IUnknown **>(&errors));
	if (FAILED(result) || code == NULL) {
		release_interface(reinterpret_cast<IUnknown **>(&code));
		return false;
	}
	result = device->CreatePixelShader(code->GetBufferPointer(), code->GetBufferSize(), NULL, shader);
	release_interface(reinterpret_cast<IUnknown **>(&code));
	return SUCCEEDED(result);
}

bool DX11PostProcessClass::Create_Shaders()
{
	D3DCompileFunction compiler = compiler_function();
	if (compiler == NULL) {
		return false;
	}

	ID3DBlob * code = NULL;
	ID3DBlob * errors = NULL;
	HRESULT result = compiler(VERTEX_SHADER_SOURCE, strlen(VERTEX_SHADER_SOURCE), "dx11post.vs",
		NULL, NULL, ENTRY_POINT, VERTEX_PROFILE, 0, 0, &code, &errors);
	release_interface(reinterpret_cast<IUnknown **>(&errors));
	if (FAILED(result) || code == NULL) {
		release_interface(reinterpret_cast<IUnknown **>(&code));
		return false;
	}
	result = Device->Get_Device()->CreateVertexShader(code->GetBufferPointer(),
		code->GetBufferSize(), NULL, &VertexShader);
	release_interface(reinterpret_cast<IUnknown **>(&code));
	if (FAILED(result)) {
		return false;
	}

	ID3D11Device * device = Device->Get_Device();
	for (unsigned index = 0; index < DX11_POST_SIMPLE_EFFECTS; ++index) {
		if (!compile_pixel_shader(compiler, device,
				simple_effect_body(static_cast<DX11PostEffect>(index)), &PixelShaders[index])) {
			return false;
		}
	}

	return compile_pixel_shader(compiler, device, TONE_MAP_SHADER_BODY, &ToneMapShader)
		&& compile_pixel_shader(compiler, device, TONE_MAP_WIDE_SHADER_BODY, &ToneMapWideShader)
		&& compile_pixel_shader(compiler, device, SHARPEN_MULTISAMPLED_SHADER_BODY,
			&SharpenMultisampledShader)
		&& compile_pixel_shader(compiler, device, BLOOM_EXTRACT_SHADER_BODY, &BloomExtractShader)
		&& compile_pixel_shader(compiler, device, BLOOM_BLUR_SHADER_BODY, &BloomBlurShader)
		&& compile_pixel_shader(compiler, device, BLOOM_COMPOSITE_SHADER_BODY,
			&BloomCompositeShader)
		&& compile_pixel_shader(compiler, device, BLOOM_COMPOSITE_WIDE_SHADER_BODY,
			&BloomCompositeWideShader)
		&& compile_pixel_shader(compiler, device, WARP_SHADER_BODY, &WarpShader)
		&& compile_pixel_shader(compiler, device, AO_SCENE_SHADER_BODY, &SceneOcclusionShader);
}

bool DX11PostProcessClass::Create_States()
{
	ID3D11Device * device = Device->Get_Device();

	D3D11_SAMPLER_DESC sampler;
	ZeroMemory(&sampler, sizeof(sampler));
	// Bilinear, because FXAA samples between texels on purpose and the bloom passes read a smaller
	// texture than they write.  At the one to one mapping the full size passes have, a sample on a
	// texel centre still returns that texel exactly.
	sampler.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
	sampler.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
	sampler.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
	sampler.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
	sampler.ComparisonFunc = D3D11_COMPARISON_NEVER;
	sampler.MaxLOD = D3D11_FLOAT32_MAX;
	if (FAILED(device->CreateSamplerState(&sampler, &Sampler))) {
		return false;
	}
	sampler.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
	sampler.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
	if (FAILED(device->CreateSamplerState(&sampler, &WrapSampler))) {
		return false;
	}

	D3D11_BLEND_DESC blend;
	ZeroMemory(&blend, sizeof(blend));
	blend.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
	if (FAILED(device->CreateBlendState(&blend, &BlendState))) {
		return false;
	}

	// The scene's colour times the pass's, its alpha kept.
	blend.RenderTarget[0].BlendEnable = TRUE;
	blend.RenderTarget[0].SrcBlend = D3D11_BLEND_ZERO;
	blend.RenderTarget[0].DestBlend = D3D11_BLEND_SRC_COLOR;
	blend.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
	blend.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ZERO;
	blend.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ONE;
	blend.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
	if (FAILED(device->CreateBlendState(&blend, &MultiplyBlendState))) {
		return false;
	}

	// The pass's colour over the scene's by the pass's alpha, the scene's alpha kept.
	blend.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
	blend.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
	if (FAILED(device->CreateBlendState(&blend, &AlphaBlendState))) {
		return false;
	}

	D3D11_DEPTH_STENCIL_DESC depth;
	ZeroMemory(&depth, sizeof(depth));
	if (FAILED(device->CreateDepthStencilState(&depth, &DepthState))) {
		return false;
	}

	D3D11_RASTERIZER_DESC rasterizer;
	ZeroMemory(&rasterizer, sizeof(rasterizer));
	rasterizer.FillMode = D3D11_FILL_SOLID;
	// The full screen triangle is written once and its winding is whatever the arithmetic gives; not
	// culling means the pass cannot be lost to a sign somewhere in it.
	rasterizer.CullMode = D3D11_CULL_NONE;
	rasterizer.DepthClipEnable = TRUE;
	if (FAILED(device->CreateRasterizerState(&rasterizer, &RasterizerState))) {
		return false;
	}

	D3D11_BUFFER_DESC constants;
	ZeroMemory(&constants, sizeof(constants));
	constants.ByteWidth = sizeof(PostConstantBlock);
	constants.Usage = D3D11_USAGE_DYNAMIC;
	constants.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	constants.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	return SUCCEEDED(device->CreateBuffer(&constants, NULL, &ConstantBuffer));
}

bool DX11PostProcessClass::Create_Target(Target & target, unsigned width, unsigned height,
	DXGI_FORMAT format, unsigned samples)
{
	ID3D11Device * device = Device->Get_Device();

	D3D11_TEXTURE2D_DESC description;
	ZeroMemory(&description, sizeof(description));
	description.Width = width;
	description.Height = height;
	description.MipLevels = 1;
	description.ArraySize = 1;
	description.Format = format;
	description.SampleDesc.Count = samples;
	description.Usage = D3D11_USAGE_DEFAULT;
	description.BindFlags = D3D11_BIND_RENDER_TARGET
		| (samples > 1 ? 0 : D3D11_BIND_SHADER_RESOURCE);

	if (FAILED(device->CreateTexture2D(&description, NULL, &target.Texture))) {
		return false;
	}
	if (FAILED(device->CreateRenderTargetView(target.Texture, NULL, &target.View))) {
		return false;
	}
	return samples > 1
		|| SUCCEEDED(device->CreateShaderResourceView(target.Texture, NULL, &target.Resource));
}

bool DX11PostProcessClass::Create_Targets(unsigned width, unsigned height, bool floating,
	unsigned samples)
{
	const DXGI_FORMAT scene_format = floating ? FLOAT_FORMAT : EIGHT_BIT_FORMAT;
	if (!Create_Target(SceneTarget, width, height, scene_format, samples)
			|| (samples > 1 && !Create_Target(SceneResolve, width, height, scene_format))) {
		Release_Targets();
		return false;
	}

	const unsigned chain_target_count = sizeof(ChainTargets) / sizeof(ChainTargets[0]);
	for (unsigned index = 0; index < chain_target_count; ++index) {
		if (!Create_Target(ChainTargets[index], width, height, EIGHT_BIT_FORMAT)) {
			Release_Targets();
			return false;
		}
	}

	// A window narrower than the divisor would ask for a zero wide texture, which is refused.
	BloomWidth = (width / BLOOM_DIVISOR) > 0 ? (width / BLOOM_DIVISOR) : 1;
	BloomHeight = (height / BLOOM_DIVISOR) > 0 ? (height / BLOOM_DIVISOR) : 1;
	if (floating) {
		const unsigned bloom_target_count = sizeof(BloomTargets) / sizeof(BloomTargets[0]);
		for (unsigned index = 0; index < bloom_target_count; ++index) {
			if (!Create_Target(BloomTargets[index], BloomWidth, BloomHeight, FLOAT_FORMAT)) {
				Release_Targets();
				return false;
			}
		}
	}

	Width = width;
	Height = height;
	TargetSamples = samples;
	TargetsAreFloat = floating;
	return true;
}

void DX11PostProcessClass::Release_Target(Target & target)
{
	release_interface(reinterpret_cast<IUnknown **>(&target.Resource));
	release_interface(reinterpret_cast<IUnknown **>(&target.View));
	release_interface(reinterpret_cast<IUnknown **>(&target.Texture));
}

void DX11PostProcessClass::Release_Targets()
{
	Release_Target(SceneTarget);
	Release_Target(SceneResolve);
	Release_Target(SceneCopy);
	Release_Target(WorldAoTargets[0]);
	Release_Target(WorldAoTargets[1]);
	Release_Target(HeatTarget);
	const unsigned chain_target_count = sizeof(ChainTargets) / sizeof(ChainTargets[0]);
	for (unsigned index = 0; index < chain_target_count; ++index) {
		Release_Target(ChainTargets[index]);
	}
	const unsigned bloom_target_count = sizeof(BloomTargets) / sizeof(BloomTargets[0]);
	for (unsigned index = 0; index < bloom_target_count; ++index) {
		Release_Target(BloomTargets[index]);
	}
	Width = 0;
	Height = 0;
	BloomWidth = 0;
	BloomHeight = 0;
	TargetSamples = 1;
	TargetsAreFloat = false;
}

unsigned DX11PostProcessClass::Scene_Samples() const
{
	return (Device != NULL) ? Device->Get_Sample_Count() : 1;
}

bool DX11PostProcessClass::Ensure_Buffers()
{
	const unsigned samples = Scene_Samples();
	if (!ShadersReady || Device == NULL || (ChainLength == 0 && samples == 1)) {
		return false;
	}

	const unsigned width = Device->Get_Width();
	const unsigned height = Device->Get_Height();
	if (width == 0 || height == 0) {
		return false;
	}

	const bool floating = Scene_Is_Float();
	if (SceneTarget.View != NULL && Width == width && Height == height
			&& TargetsAreFloat == floating && TargetSamples == samples) {
		return true;
	}

	// A resolution change makes the old set the wrong size, and a chain that gained or lost bloom
	// makes the scene target the wrong format.  Both are for the rest of the run, not for a frame.
	Release_Targets();
	if (!Create_Targets(width, height, floating, samples)) {
		Release_Targets();
		strcpy(Diagnostic, "post-process off: the device refused the offscreen targets");
		return false;
	}
	Describe();
	return true;
}

ID3D11RenderTargetView * DX11PostProcessClass::Scene_View()
{
	return Ensure_Buffers() ? SceneTarget.View : NULL;
}

ID3D11ShaderResourceView * DX11PostProcessClass::Scene_Source()
{
	if (SceneResolve.Texture == NULL) {
		return SceneTarget.Resource;
	}
	DX11Resource_Copy_Readable(Device->Get_Context(), SceneResolve.Texture, SceneTarget.Texture);
	return SceneResolve.Resource;
}

void DX11PostProcessClass::Begin_Frame()
{
	Resolved = false;
}

void DX11PostProcessClass::Open_Frame()
{
	Occluded = false;
	Hazed = false;
}

bool DX11PostProcessClass::Wants_Atmosphere_Programs() const
{
	return AtmosphereFade[2] > 0.0f && AtmosphereShaders[ATMOSPHERE_PROGRAMS - 1] == NULL && !AtmosphereRefused
		&& ShadersReady;
}

const char * DX11PostProcessClass::Atmosphere_Program_Source(unsigned index) const
{
	static const std::string sources[ATMOSPHERE_PROGRAMS] = {
		std::string(PIXEL_SHADER_PROLOGUE) + SKY_SHADER_BODY,
		std::string(PIXEL_SHADER_PROLOGUE) + ATMOSPHERE_SHADER_BODY
	};
	return sources[index].c_str();
}

void DX11PostProcessClass::Load_Atmosphere_Program(unsigned index, const void * bytecode, SIZE_T size)
{
	if (bytecode == NULL || FAILED(Device->Get_Device()->CreatePixelShader(bytecode, size, NULL,
			&AtmosphereShaders[index]))) {
		AtmosphereShaders[index] = NULL;
		AtmosphereRefused = true;
	}
}

void DX11PostProcessClass::Compile_Atmosphere_Programs()
{
	const char * const bodies[ATMOSPHERE_PROGRAMS] = { SKY_SHADER_BODY, ATMOSPHERE_SHADER_BODY };
	for (unsigned index = 0; index < ATMOSPHERE_PROGRAMS; ++index) {
		if (AtmosphereShaders[index] == NULL && !compile_pixel_shader(compiler_function(), Device->Get_Device(),
				bodies[index], &AtmosphereShaders[index])) {
			AtmosphereShaders[index] = NULL;
			AtmosphereRefused = true;
		}
	}
}

bool DX11PostProcessClass::Draw_Atmosphere(unsigned index, ID3D11ShaderResourceView * depth,
	const float world_from_clip[16])
{
	if (AtmosphereFade[2] <= 0.0f || AtmosphereShaders[index] == NULL || Resolved || !ShadersReady
			|| SceneTarget.View == NULL || Device->Get_Scene_View() != SceneTarget.View) {
		return false;
	}
	memcpy(HazeWorldFromClip, world_from_clip, sizeof(HazeWorldFromClip));
	// The game pushes the shroud every frame this is on.  The haze, when it ran, has taken it for the
	// frame's view already.
	if (!Hazed) {
		HazeShroudView = HazeShroudSource;
	}

	PassSetup pass;
	memset(&pass, 0, sizeof(pass));
	pass.Shader = AtmosphereShaders[index];
	pass.Extra = depth;
	pass.Blend = depth != NULL;
	pass.World = true;
	pass.Atmosphere = true;
	pass.Destination = SceneTarget.View;
	pass.DestinationWidth = Width;
	pass.DestinationHeight = Height;
	pass.SourceWidth = Width;
	pass.SourceHeight = Height;
	Draw_Pass(pass);
	return true;
}

bool DX11PostProcessClass::Sky_Scene(const float world_from_clip[16])
{
	return Draw_Atmosphere(0, NULL, world_from_clip);
}

bool DX11PostProcessClass::Atmosphere_Scene(ID3D11ShaderResourceView * depth,
	const float world_from_clip[16])
{
	return Draw_Atmosphere(1, depth, world_from_clip);
}

bool DX11PostProcessClass::Wants_Haze_Program() const
{
	return HazeStrength > 0.0f && HazeShader == NULL && !HazeRefused && ShadersReady;
}

const char * DX11PostProcessClass::Haze_Program_Source() const
{
	static const std::string source = std::string(PIXEL_SHADER_PROLOGUE) + HAZE_SHADER_BODY;
	return source.c_str();
}

void DX11PostProcessClass::Load_Haze_Program(const void * bytecode, SIZE_T size)
{
	if (bytecode == NULL || FAILED(Device->Get_Device()->CreatePixelShader(bytecode, size, NULL, &HazeShader))) {
		HazeShader = NULL;
		HazeRefused = true;
	}
}

void DX11PostProcessClass::Compile_Haze_Program()
{
	if (HazeShader == NULL && !compile_pixel_shader(compiler_function(), Device->Get_Device(),
			HAZE_SHADER_BODY, &HazeShader)) {
		HazeShader = NULL;
		HazeRefused = true;
	}
}

bool DX11PostProcessClass::Wants_World_Ao_Programs() const
{
	return WorldAoShaders[WORLD_AO_PROGRAMS - 1] == NULL && !WorldAoRefused && ShadersReady;
}

const char * DX11PostProcessClass::World_Ao_Program_Source(unsigned index) const
{
	static const std::string sources[WORLD_AO_PROGRAMS] = {
		std::string(PIXEL_SHADER_PROLOGUE) + WORLD_AO_SHADER_BODY,
		std::string(PIXEL_SHADER_PROLOGUE) + WORLD_AO_BLUR_SHADER_BODY,
		std::string(PIXEL_SHADER_PROLOGUE) + WORLD_AO_UPSAMPLE_SHADER_BODY
	};
	return sources[index].c_str();
}

void DX11PostProcessClass::Load_World_Ao_Program(unsigned index, const void * bytecode, SIZE_T size)
{
	if (bytecode == NULL || FAILED(Device->Get_Device()->CreatePixelShader(bytecode, size, NULL,
			&WorldAoShaders[index]))) {
		WorldAoShaders[index] = NULL;
		WorldAoRefused = true;
	}
}

void DX11PostProcessClass::Compile_World_Ao_Programs()
{
	const char * const bodies[WORLD_AO_PROGRAMS] = { WORLD_AO_SHADER_BODY, WORLD_AO_BLUR_SHADER_BODY,
		WORLD_AO_UPSAMPLE_SHADER_BODY };
	for (unsigned index = 0; index < WORLD_AO_PROGRAMS; ++index) {
		if (!compile_pixel_shader(compiler_function(), Device->Get_Device(), bodies[index], &WorldAoShaders[index])) {
			WorldAoShaders[index] = NULL;
			WorldAoRefused = true;
		}
	}
}

bool DX11PostProcessClass::World_Occlude_Scene(ID3D11ShaderResourceView * depth,
	const float world_from_clip[16], float projection_x, float projection_y)
{
	if (Occluded || Resolved || !ShadersReady || SceneTarget.View == NULL
			|| Device->Get_Scene_View() != SceneTarget.View || WorldAoRefused
			|| WorldAoShaders[WORLD_AO_PROGRAMS - 1] == NULL) {
		return false;
	}
	const unsigned half_width = (Width + 1) / 2;
	const unsigned half_height = (Height + 1) / 2;
	if (WorldAoTargets[0].Texture == NULL
			&& (!Create_Target(WorldAoTargets[0], half_width, half_height, DXGI_FORMAT_R32G32_FLOAT)
				|| !Create_Target(WorldAoTargets[1], half_width, half_height, DXGI_FORMAT_R32G32_FLOAT))) {
		Release_Target(WorldAoTargets[0]);
		Release_Target(WorldAoTargets[1]);
		return false;
	}
	memcpy(HazeWorldFromClip, world_from_clip, sizeof(HazeWorldFromClip));
	WorldAoProjection[0] = projection_x;
	WorldAoProjection[1] = projection_y;
	// Read and left for the haze, which comes later in the same frame and takes it for good.
	HazeShroudView = HazeShroudSource;

	PassSetup pass;
	memset(&pass, 0, sizeof(pass));
	pass.Shader = WorldAoShaders[0];
	pass.Extra = depth;
	pass.World = true;
	pass.Destination = WorldAoTargets[0].View;
	pass.DestinationWidth = half_width;
	pass.DestinationHeight = half_height;
	pass.SourceWidth = Width;
	pass.SourceHeight = Height;
	Draw_Pass(pass);

	memset(&pass, 0, sizeof(pass));
	pass.Shader = WorldAoShaders[1];
	pass.World = true;
	pass.Source = WorldAoTargets[0].Resource;
	pass.Destination = WorldAoTargets[1].View;
	pass.DestinationWidth = half_width;
	pass.DestinationHeight = half_height;
	pass.SourceWidth = half_width;
	pass.SourceHeight = half_height;
	pass.BlurX = 1.0f;
	Draw_Pass(pass);

	pass.Source = WorldAoTargets[1].Resource;
	pass.Destination = WorldAoTargets[0].View;
	pass.BlurX = 0.0f;
	pass.BlurY = 1.0f;
	Draw_Pass(pass);

	memset(&pass, 0, sizeof(pass));
	pass.Shader = WorldAoShaders[2];
	pass.Source = WorldAoTargets[0].Resource;
	pass.Extra = depth;
	pass.World = true;
	pass.Multiply = true;
	pass.Destination = SceneTarget.View;
	pass.DestinationWidth = Width;
	pass.DestinationHeight = Height;
	pass.SourceWidth = half_width;
	pass.SourceHeight = half_height;
	Draw_Pass(pass);
	Occluded = true;
	return true;
}

bool DX11PostProcessClass::Haze_Scene(ID3D11ShaderResourceView * depth,
	const float world_from_clip[16])
{
	if (HazeStrength <= 0.0f || Hazed || Resolved || !ShadersReady || SceneTarget.View == NULL
			|| Device->Get_Scene_View() != SceneTarget.View) {
		return false;
	}
	// Loaded the first time the prototype asks for it (Load_Haze_Program), so a refusal costs the
	// haze alone and a launch without the switch loads nothing more.
	if (HazeShader == NULL) {
		return false;
	}
	memcpy(HazeWorldFromClip, world_from_clip, sizeof(HazeWorldFromClip));

	// Borrowed for this frame only: the shroud's texture (and so its mirror) can be freed by the
	// next map or a shroud reset, and a frame that set none draws with none.
	HazeShroudView = HazeShroudSource;
	HazeShroudSource = NULL;

	PassSetup pass;
	memset(&pass, 0, sizeof(pass));
	pass.Shader = HazeShader;
	pass.Extra = depth;
	pass.Blend = true;
	pass.Destination = SceneTarget.View;
	pass.DestinationWidth = Width;
	pass.DestinationHeight = Height;
	pass.SourceWidth = Width;
	pass.SourceHeight = Height;
	Draw_Pass(pass);
	Hazed = true;
	return true;
}

bool DX11PostProcessClass::Occlude_Scene(ID3D11ShaderResourceView * depth)
{
	if (Occluded || Resolved || !ShadersReady || SceneTarget.View == NULL
			|| Device->Get_Scene_View() != SceneTarget.View) {
		return false;
	}
	bool wanted = false;
	for (unsigned index = 0; index < ChainLength; ++index) {
		wanted = wanted || Chain[index] == DX11_POST_AO;
	}
	if (!wanted) {
		return false;
	}
	// The pass keys on the scene's colour and cannot read the target it multiplies, so it reads a
	// copy, made the first time and kept with the other targets.
	if (SceneCopy.Texture == NULL
			&& !Create_Target(SceneCopy, Width, Height, TargetsAreFloat ? FLOAT_FORMAT : EIGHT_BIT_FORMAT)) {
		Release_Target(SceneCopy);
		return false;
	}
	DX11Resource_Copy_Readable(Device->Get_Context(), SceneCopy.Texture, SceneTarget.Texture);

	PassSetup pass;
	memset(&pass, 0, sizeof(pass));
	pass.Shader = SceneOcclusionShader;
	pass.Source = SceneCopy.Resource;
	pass.Extra = depth;
	pass.Occlusion = true;
	pass.Multiply = true;
	pass.Destination = SceneTarget.View;
	pass.DestinationWidth = Width;
	pass.DestinationHeight = Height;
	pass.SourceWidth = Width;
	pass.SourceHeight = Height;
	Draw_Pass(pass);
	Occluded = true;
	return true;
}

void DX11PostProcessClass::Set_Heat_Splats(const float * splats, unsigned count)
{
	if (count > DIRECT3D11_HEAT_SPLAT_LIMIT) {
		count = DIRECT3D11_HEAT_SPLAT_LIMIT;
	}
	HeatSplats.assign(splats, splats + count * DIRECT3D11_HEAT_SPLAT_FLOATS);
}

void DX11PostProcessClass::Release_Heat_Haze()
{
	release_interface(reinterpret_cast<IUnknown **>(&HeatBlendState));
	release_interface(reinterpret_cast<IUnknown **>(&HeatHazeShader));
	release_interface(reinterpret_cast<IUnknown **>(&HeatSplatShader));
	release_interface(reinterpret_cast<IUnknown **>(&HeatSplatVertexShader));
	release_interface(reinterpret_cast<IUnknown **>(&HeatSplatView));
	release_interface(reinterpret_cast<IUnknown **>(&HeatSplatBuffer));
}

bool DX11PostProcessClass::Wants_Heat_Haze_Programs() const
{
	return ShadersReady && HeatHazeShader == NULL && !HeatHazeRefused;
}

const char * DX11PostProcessClass::Heat_Haze_Program_Source(unsigned index) const
{
	static const std::string sources[HEAT_HAZE_PROGRAMS] = {
		std::string(HEAT_SPLAT_VERTEX_SOURCE),
		std::string(PIXEL_SHADER_PROLOGUE) + HEAT_SPLAT_SHADER_BODY,
		std::string(PIXEL_SHADER_PROLOGUE) + HEAT_HAZE_SHADER_BODY
	};
	return sources[index].c_str();
}

const char * DX11PostProcessClass::Heat_Haze_Program_Profile(unsigned index) const
{
	return (index == 0) ? VERTEX_PROFILE : PIXEL_PROFILE;
}

void DX11PostProcessClass::Load_Heat_Haze_Program(unsigned index, const void * bytecode, SIZE_T size)
{
	if (HeatHazeRefused) {
		return;
	}
	ID3D11Device * device = Device->Get_Device();
	HRESULT result = E_FAIL;
	if (bytecode != NULL) {
		result = (index == 0) ? device->CreateVertexShader(bytecode, size, NULL, &HeatSplatVertexShader)
			: device->CreatePixelShader(bytecode, size, NULL, (index == 1) ? &HeatSplatShader : &HeatHazeShader);
	}
	// the last program in makes the rest of what the pass needs
	if (FAILED(result) || (index + 1 == HEAT_HAZE_PROGRAMS && !Create_Heat_Haze())) {
		Release_Heat_Haze();
		HeatHazeRefused = true;
	}
}

void DX11PostProcessClass::Compile_Heat_Haze_Programs()
{
	D3DCompileFunction compiler = compiler_function();
	for (unsigned index = 0; index < HEAT_HAZE_PROGRAMS; ++index) {
		ID3DBlob * code = NULL;
		ID3DBlob * errors = NULL;
		const char * source = Heat_Haze_Program_Source(index);
		HRESULT result = (compiler == NULL) ? E_FAIL : compiler(source, strlen(source), "dx11heat", NULL, NULL,
			ENTRY_POINT, Heat_Haze_Program_Profile(index), 0, 0, &code, &errors);
		release_interface(reinterpret_cast<IUnknown **>(&errors));
		const bool made = SUCCEEDED(result) && code != NULL;
		Load_Heat_Haze_Program(index, made ? code->GetBufferPointer() : NULL, made ? code->GetBufferSize() : 0);
		release_interface(reinterpret_cast<IUnknown **>(&code));
	}
}

bool DX11PostProcessClass::Create_Heat_Haze()
{
	ID3D11Device * device = Device->Get_Device();
	D3D11_BUFFER_DESC buffer;
	ZeroMemory(&buffer, sizeof(buffer));
	buffer.ByteWidth = DIRECT3D11_HEAT_SPLAT_LIMIT * DIRECT3D11_HEAT_SPLAT_FLOATS * sizeof(float);
	buffer.Usage = D3D11_USAGE_DYNAMIC;
	buffer.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	buffer.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	if (FAILED(device->CreateBuffer(&buffer, NULL, &HeatSplatBuffer))) {
		return false;
	}
	D3D11_SHADER_RESOURCE_VIEW_DESC view;
	ZeroMemory(&view, sizeof(view));
	view.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
	view.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
	view.Buffer.FirstElement = 0;
	view.Buffer.NumElements = DIRECT3D11_HEAT_SPLAT_LIMIT * DIRECT3D11_HEAT_SPLAT_FLOATS / 4;
	if (FAILED(device->CreateShaderResourceView(HeatSplatBuffer, &view, &HeatSplatView))) {
		return false;
	}

	// Every splat's shift and weight added to what the target holds.
	D3D11_BLEND_DESC blend;
	ZeroMemory(&blend, sizeof(blend));
	blend.RenderTarget[0].BlendEnable = TRUE;
	blend.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE;
	blend.RenderTarget[0].DestBlend = D3D11_BLEND_ONE;
	blend.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
	blend.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
	blend.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ONE;
	blend.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
	blend.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
	return SUCCEEDED(device->CreateBlendState(&blend, &HeatBlendState));
}

bool DX11PostProcessClass::Heat_Haze_Scene(ID3D11ShaderResourceView * depth)
{
	if (HeatSplats.empty()) {
		return false;
	}
	const unsigned count = static_cast<unsigned>(HeatSplats.size() / DIRECT3D11_HEAT_SPLAT_FLOATS);
	std::vector<float> splats;
	splats.swap(HeatSplats);	// spent whether or not this frame can draw them
	if (Resolved || !ShadersReady || HeatHazeRefused || depth == NULL || SceneTarget.View == NULL
			|| Device->Get_Scene_View() != SceneTarget.View) {
		return false;
	}
	// loaded when the switch was first seen on (Direct3D11_Set_Heat_Splats), never on a frame's draw
	if (HeatHazeShader == NULL || HeatBlendState == NULL) {
		return false;
	}
	const unsigned half_width = (Width + 1) / 2;
	const unsigned half_height = (Height + 1) / 2;
	if (HeatTarget.Texture == NULL
			&& !Create_Target(HeatTarget, half_width, half_height, DXGI_FORMAT_R16G16B16A16_FLOAT)) {
		Release_Target(HeatTarget);
		return false;
	}
	if (SceneCopy.Texture == NULL
			&& !Create_Target(SceneCopy, Width, Height, TargetsAreFloat ? FLOAT_FORMAT : EIGHT_BIT_FORMAT)) {
		Release_Target(SceneCopy);
		return false;
	}

	ID3D11DeviceContext * context = Device->Get_Context();
	D3D11_MAPPED_SUBRESOURCE mapped;
	if (FAILED(context->Map(HeatSplatBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
		return false;
	}
	memcpy(mapped.pData, &splats[0], splats.size() * sizeof(float));
	context->Unmap(HeatSplatBuffer, 0);

	const float nothing[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	context->ClearRenderTargetView(HeatTarget.View, nothing);
	context->OMSetRenderTargets(1, &HeatTarget.View, NULL);
	D3D11_VIEWPORT viewport;
	viewport.TopLeftX = 0.0f;
	viewport.TopLeftY = 0.0f;
	viewport.Width = static_cast<float>(half_width);
	viewport.Height = static_cast<float>(half_height);
	viewport.MinDepth = 0.0f;
	viewport.MaxDepth = 1.0f;
	context->RSSetViewports(1, &viewport);
	context->OMSetBlendState(HeatBlendState, nothing, 0xffffffff);
	context->OMSetDepthStencilState(DepthState, 0);
	context->RSSetState(RasterizerState);
	context->IASetInputLayout(NULL);
	context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	context->VSSetShader(HeatSplatVertexShader, NULL, 0);
	context->VSSetShaderResources(0, 1, &HeatSplatView);
	context->PSSetShader(HeatSplatShader, NULL, 0);
	context->PSSetShaderResources(1, 1, &depth);
	context->Draw(count * 6, 0);
	ID3D11ShaderResourceView * none = NULL;
	context->VSSetShaderResources(0, 1, &none);
	context->PSSetShaderResources(1, 1, &none);

	DX11Resource_Copy_Readable(context, SceneCopy.Texture, SceneTarget.Texture);

	PassSetup pass;
	memset(&pass, 0, sizeof(pass));
	pass.Shader = HeatHazeShader;
	pass.Source = SceneCopy.Resource;
	pass.Extra = HeatTarget.Resource;
	pass.Destination = SceneTarget.View;
	pass.DestinationWidth = Width;
	pass.DestinationHeight = Height;
	pass.SourceWidth = Width;
	pass.SourceHeight = Height;
	Draw_Pass(pass);
	return true;
}

void DX11PostProcessClass::Draw_Pass(const PassSetup & pass)
{
	ID3D11DeviceContext * context = Device->Get_Context();

	D3D11_MAPPED_SUBRESOURCE mapped;
	if (FAILED(context->Map(ConstantBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
		return;
	}
	PostConstantBlock block;
	block.TexelSize[0] = 1.0f / static_cast<float>(pass.SourceWidth);
	block.TexelSize[1] = 1.0f / static_cast<float>(pass.SourceHeight);
	block.TexelSize[2] = static_cast<float>(pass.SourceWidth);
	block.TexelSize[3] = static_cast<float>(pass.SourceHeight);
	block.BlurDirection[0] = pass.BlurX;
	block.BlurDirection[1] = pass.BlurY;
	block.BlurDirection[2] = pass.Saturation;
	block.BlurDirection[3] = pass.Clarity;
	block.Tuning[0] = BloomThreshold;
	block.Tuning[1] = BloomIntensity;
	block.Tuning[2] = TONE_CURVE_KNEE;
	block.Tuning[3] = BLOOM_KNEE;
	if (pass.Occlusion) {
		block.Tuning[0] = NearPlane;
		block.Tuning[1] = FarPlane;
		block.Tuning[2] = AO_RADIUS;
		block.Tuning[3] = AO_SPREAD;
	}
	block.WarpInfo[0] = static_cast<float>(WarpCount);
	block.WarpInfo[1] = static_cast<float>(pass.SourceWidth) / static_cast<float>(pass.SourceHeight);
	block.WarpInfo[2] = 0.0f;
	block.WarpInfo[3] = 0.0f;
	for (unsigned index = 0; index < DX11_POST_WARP_LIMIT; ++index) {
		const DX11PostWarp & warp = Warps[index];
		block.WarpCentre[index][0] = warp.CentreX;
		block.WarpCentre[index][1] = warp.CentreY;
		block.WarpCentre[index][2] = warp.Radius;
		block.WarpCentre[index][3] = warp.Pull;
		block.WarpRing[index][0] = warp.RingRadius;
		block.WarpRing[index][1] = warp.RingWidth;
		block.WarpRing[index][2] = warp.RingStrength;
		block.WarpRing[index][3] = warp.RingSquash;
	}
	block.Grade[0] = pass.Ungraded ? 0.0f : GradeAmount;
	block.Grade[1] = GRADE_CONTRAST;
	block.Grade[2] = GRADE_SATURATION;
	block.Grade[3] = GRADE_SPLIT;
	block.Haze[0] = HazeTop;
	block.Haze[1] = HazeThickness;
	block.Haze[2] = HazeStrength;
	block.Haze[3] = HazeReach;
	block.HazeColour[0] = HazeColour[0];
	block.HazeColour[1] = HazeColour[1];
	block.HazeColour[2] = HazeColour[2];
	block.HazeColour[3] = HazeWater;
	memcpy(block.WorldFromClip, HazeWorldFromClip, sizeof(block.WorldFromClip));
	memcpy(block.HazeSun, HazeSun, sizeof(block.HazeSun));
	// the program reads only the sun's bearing, so its z says whether a shroud texture is bound
	block.HazeSun[2] = (HazeShroudView != NULL) ? 1.0f : 0.0f;
	memcpy(block.HazeShroudRect, HazeShroudRect, sizeof(block.HazeShroudRect));
	memcpy(block.WorldAoProjection, WorldAoProjection, sizeof(block.WorldAoProjection));
	if (pass.Atmosphere) {
		memcpy(block.Haze, AtmosphereFade, sizeof(block.Haze));
		memcpy(block.HazeColour, AtmosphereHorizon, sizeof(AtmosphereHorizon));
		memcpy(block.BlurDirection, AtmosphereTint, sizeof(AtmosphereTint));
		memcpy(block.WorldAoProjection, AtmosphereMapSize, sizeof(AtmosphereMapSize));
	}
	memcpy(mapped.pData, &block, sizeof(block));
	context->Unmap(ConstantBuffer, 0);

	// No depth: the pass covers every pixel exactly once and a depth buffer left bound would test
	// the triangle against the scene it is reading.
	context->OMSetRenderTargets(1, &pass.Destination, NULL);

	D3D11_VIEWPORT viewport;
	viewport.TopLeftX = 0.0f;
	viewport.TopLeftY = 0.0f;
	viewport.Width = static_cast<float>(pass.DestinationWidth);
	viewport.Height = static_cast<float>(pass.DestinationHeight);
	viewport.MinDepth = 0.0f;
	viewport.MaxDepth = 1.0f;
	context->RSSetViewports(1, &viewport);

	const float blend_factor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	context->OMSetBlendState(pass.Multiply ? MultiplyBlendState : pass.Blend ? AlphaBlendState : BlendState,
		blend_factor, 0xffffffff);
	context->OMSetDepthStencilState(DepthState, 0);
	context->RSSetState(RasterizerState);

	context->IASetInputLayout(NULL);
	context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	context->VSSetShader(VertexShader, NULL, 0);
	context->PSSetShader(pass.Shader, NULL, 0);
	context->PSSetSamplers(0, 1, &Sampler);
	ID3D11ShaderResourceView * resources[2] = { pass.Source, pass.Extra };
	context->PSSetShaderResources(0, 2, resources);
	context->PSSetConstantBuffers(0, 1, &ConstantBuffer);
	const bool haze = pass.World || pass.Shader == HazeShader;
	if (haze) {
		// The same buffer at b1 as well, where the haze program reads its own fields of it, and
		// the shroud at t2.
		context->PSSetConstantBuffers(1, 1, &ConstantBuffer);
		context->PSSetShaderResources(2, 1, &HazeShroudView);
	}
	if (pass.Atmosphere) {
		context->PSSetShaderResources(3, 1, &AtmosphereSky);
		context->PSSetSamplers(1, 1, &WrapSampler);
	}

	context->Draw(FULL_SCREEN_VERTEX_COUNT, 0);

	if (haze) {
		ID3D11ShaderResourceView * no_haze_resource = NULL;
		context->PSSetShaderResources(2, 1, &no_haze_resource);
	}
	if (pass.Atmosphere) {
		ID3D11ShaderResourceView * no_sky = NULL;
		context->PSSetShaderResources(3, 1, &no_sky);
	}

	// The texture this pass read is the target the next one writes, and D3D11 unbinds a resource
	// silently when it is bound both ways at once.  Letting go here says which of the two bindings
	// was meant.
	ID3D11ShaderResourceView * no_resources[2] = { NULL, NULL };
	context->PSSetShaderResources(0, 2, no_resources);
}

ID3D11ShaderResourceView * DX11PostProcessClass::Run_Bloom(ID3D11RenderTargetView * destination,
	ID3D11ShaderResourceView * scene)
{
	const float bloom_texel_x = 1.0f / static_cast<float>(BloomWidth);
	const float bloom_texel_y = 1.0f / static_cast<float>(BloomHeight);

	PassSetup pass;
	memset(&pass, 0, sizeof(pass));

	// The bright pass reads the full size scene and writes the quarter size target, so it is the
	// one pass whose source and destination sizes differ.
	pass.Shader = BloomExtractShader;
	pass.Source = scene;
	pass.Destination = BloomTargets[0].View;
	pass.DestinationWidth = BloomWidth;
	pass.DestinationHeight = BloomHeight;
	pass.SourceWidth = Width;
	pass.SourceHeight = Height;
	pass.Saturation = BloomSaturation;
	Draw_Pass(pass);
	pass.Saturation = 0.0f;

	// The clarity's blur is the first two pairs, whatever the glow asks for; with the glow off
	// those two pairs are all that run.
	const bool clarity = (ClarityStrength > 0.0f);
	unsigned pairs = (BloomIntensity > 0.0f) ? BloomBlurPasses : 0;
	if (clarity && pairs < CLARITY_BLUR_PASSES) {
		pairs = CLARITY_BLUR_PASSES;
	}

	pass.Shader = BloomBlurShader;
	pass.SourceWidth = BloomWidth;
	pass.SourceHeight = BloomHeight;
	unsigned source = 0;
	for (unsigned iteration = 0; iteration < pairs; ++iteration) {
		pass.Clarity = (clarity && iteration < CLARITY_BLUR_PASSES) ? 1.0f : 0.0f;
		// Each pair steps a texel wider than the last.  What the earlier pairs left is smooth
		// enough by then that the taps landing between texels miss nothing, and the widths add
		// up in squares, which is how four pairs reach twice as far as two.
		const float spread = static_cast<float>(iteration + 1);
		for (unsigned axis = 0; axis < 2; ++axis) {
			const unsigned other = 1 - source;
			pass.Source = BloomTargets[source].Resource;
			pass.Destination = BloomTargets[other].View;
			pass.BlurX = (axis == 0) ? bloom_texel_x * spread : 0.0f;
			pass.BlurY = (axis == 0) ? 0.0f : bloom_texel_y * spread;
			Draw_Pass(pass);
			source = other;
		}
	}

	pass.Shader = InternalHdr ? BloomCompositeWideShader : BloomCompositeShader;
	pass.Source = scene;
	pass.Extra = BloomTargets[source].Resource;
	pass.Destination = destination;
	pass.DestinationWidth = Width;
	pass.DestinationHeight = Height;
	pass.SourceWidth = Width;
	pass.SourceHeight = Height;
	pass.BlurX = 0.0f;
	pass.BlurY = 0.0f;
	pass.Clarity = ClarityStrength;
	Draw_Pass(pass);

	return ChainTargets[0].Resource;
}

bool DX11PostProcessClass::Finish(const DX11PostEffect * effects, unsigned count)
{
	if (Resolved || count == 0 || !Ensure_Buffers()) {
		return false;
	}

	ID3D11RenderTargetView * back_buffer = Device->Get_Back_Buffer_View();
	if (back_buffer == NULL) {
		return false;
	}

	// A blast on screen puts one more pass on the end of the chain, so whatever was last writes a
	// chain target instead of the screen and the warp writes the screen from it.
	const bool warp = (WarpCount > 0);

	unsigned first = 0;
	ID3D11ShaderResourceView * const scene = Scene_Source();
	ID3D11ShaderResourceView * source = scene;
	if (effects[0] == DX11_POST_BLOOM) {
		// Bloom is the only effect that reads the half float scene, and the frame is eight bits by
		// the time it hands over. If it is also the last thing in the chain it writes the screen.
		const bool alone = (count == 1) && !warp;
		ID3D11RenderTargetView * bloom_destination = alone ? back_buffer : ChainTargets[0].View;
		if (BloomIntensity > 0.0f || ClarityStrength > 0.0f) {
			// The clarity's blur rides the bloom's targets, so with the glow off and the clarity
			// on this still runs, with nothing in the glow.
			source = Run_Bloom(bloom_destination, scene);
		} else {
			// Bloom switched off in the options: no glow, but the scene is still half floats and
			// still has to come down to eight bits, the same way Copy_Through brings it down.  The
			// chain keeps its shape so turning it back on costs no rebuild of the targets.
			PassSetup tone;
			memset(&tone, 0, sizeof(tone));
			tone.Shader = InternalHdr ? ToneMapWideShader : ToneMapShader;
			tone.Source = scene;
			tone.Destination = bloom_destination;
			tone.DestinationWidth = Width;
			tone.DestinationHeight = Height;
			tone.SourceWidth = Width;
			tone.SourceHeight = Height;
			Draw_Pass(tone);
			source = ChainTargets[0].Resource;
		}
		if (alone) {
			Resolved = true;
			return true;
		}
		first = 1;
	}

	// Each remaining pass reads one eight bit target and writes the other, and the last one writes
	// the swap chain instead, so an odd or an even chain both end up on the screen.
	unsigned destination = (first == 0) ? 0u : 1u;
	PassSetup pass;
	memset(&pass, 0, sizeof(pass));
	pass.DestinationWidth = Width;
	pass.DestinationHeight = Height;
	pass.SourceWidth = Width;
	pass.SourceHeight = Height;
	for (unsigned index = first; index < count; ++index) {
		const bool last = (index + 1 == count) && !warp;
		pass.Shader = (effects[index] == DX11_POST_SHARPEN && TargetSamples > 1)
			? SharpenMultisampledShader : PixelShaders[effects[index]];
		// The occlusion pass is the one effect that reads something other than the frame: the
		// frame's own depth, which the device keeps as a texture as well as a depth buffer.
		pass.Extra = (effects[index] == DX11_POST_AO) ? Device->Get_Depth_Texture() : NULL;
		pass.Occlusion = (effects[index] == DX11_POST_AO);
		pass.Source = source;
		pass.Destination = last ? back_buffer : ChainTargets[destination].View;
		Draw_Pass(pass);
		source = ChainTargets[destination].Resource;
		destination = 1 - destination;
	}

	if (warp) {
		pass.Shader = WarpShader;
		pass.Extra = NULL;
		pass.Occlusion = false;
		pass.Source = source;
		pass.Destination = back_buffer;
		Draw_Pass(pass);
	}

	Resolved = true;
	return true;
}

bool DX11PostProcessClass::Run_Chain()
{
	// The scene took its occlusion before the particles, and a multisampled one has no use for
	// FXAA; a chain of nothing else, or none at all over a multisampled scene, still has to bring
	// the scene to the screen.  Without either the chain runs as it was set, and an empty one with
	// it, since an empty chain at one sample has no scene target to bring.
	const bool multisampled = Scene_Samples() > 1;
	DX11PostEffect effects[DX11_POST_CHAIN_LIMIT];
	unsigned count = 0;
	for (unsigned index = 0; index < ChainLength; ++index) {
		if ((Occluded && Chain[index] == DX11_POST_AO) || (multisampled && Chain[index] == DX11_POST_FXAA)) {
			continue;
		}
		effects[count++] = Chain[index];
	}
	if (count == 0) {
		effects[count++] = DX11_POST_COPY;
	}
	return Finish(effects, count);
}

bool DX11PostProcessClass::Copy_Through()
{
	if (Resolved || !Ensure_Buffers()) {
		return false;
	}

	ID3D11RenderTargetView * back_buffer = Device->Get_Back_Buffer_View();
	if (back_buffer == NULL) {
		return false;
	}

	// The tone curve rather than the copy when the scene is half float, because the swap chain
	// cannot hold what the scene target holds and a raw write would clamp it. In eight bits the
	// copy is exact, which is what the plumbing test measures.
	PassSetup pass;
	memset(&pass, 0, sizeof(pass));
	pass.Shader = TargetsAreFloat ? ToneMapShader : PixelShaders[DX11_POST_COPY];
	pass.Ungraded = true;
	pass.Source = Scene_Source();
	pass.Destination = back_buffer;
	pass.DestinationWidth = Width;
	pass.DestinationHeight = Height;
	pass.SourceWidth = Width;
	pass.SourceHeight = Height;
	Draw_Pass(pass);

	Resolved = true;
	return true;
}

void DX11PostProcessClass::Describe()
{
	if (!ShadersReady) {
		return;
	}
	const unsigned samples = Scene_Samples();
	if (ChainLength == 0 && samples == 1) {
		strcpy(Diagnostic, "post-process off");
		return;
	}

	std::string names = (ChainLength == 0) ? "copy" : "";
	for (unsigned index = 0; index < ChainLength; ++index) {
		if (index > 0) {
			names += ",";
		}
		names += DX11Post_Effect_Name(Chain[index]);
	}
	char multisampling[64] = "";
	if (samples > 1) {
		snprintf(multisampling, sizeof(multisampling), ", %ux multisampled without fxaa, sharpen softened",
			samples);
	}
	snprintf(Diagnostic, sizeof(Diagnostic), "post-process %s over %ux%u, scene in %s%s", names.c_str(),
		Width, Height, TargetsAreFloat ? "half floats" : "eight bits", multisampling);
}
