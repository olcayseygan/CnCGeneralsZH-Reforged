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

// Does the generated text compile.
//
// test_ffvertex.cpp checks the shape of the program - which term is there, which register it
// reads - and every one of those checks passes on text that no compiler would accept.  A missing
// declaration, a swizzle off the end of a float3, a semantic the profile does not have: all of
// them look like correct strings.  So this hands the generator's output to the real HLSL compiler,
// both profiles, every description the game is known to ask for, and reports what it says.
//
// It compiles with D3DCompile out of d3dcompiler_47.dll, which ships with Windows.  A machine
// without it skips rather than fails: the check is worth having where it can run and it is not
// worth making the suite unrunnable somewhere else.

#include "test_harness.h"

#include "ffshader.h"
#include "ffvertex.h"

#include <d3dcommon.h>
#include <string.h>
#include <windows.h>

typedef HRESULT (WINAPI *D3DCompileFunction)(LPCVOID source_data, SIZE_T source_size,
	LPCSTR source_name, const D3D_SHADER_MACRO * defines, ID3DInclude * include,
	LPCSTR entry_point, LPCSTR target, UINT flags1, UINT flags2, ID3DBlob ** code,
	ID3DBlob ** error_messages);

static const char * const COMPILER_MODULE = "d3dcompiler_47.dll";
static const char * const ENTRY_POINT = "main";
static const char * const D3D9_PROFILE = "vs_3_0";
static const char * const D3D11_PROFILE = "vs_4_0";

static D3DCompileFunction load_compiler()
{
	HMODULE module = LoadLibraryA(COMPILER_MODULE);
	if (module == NULL) {
		return NULL;
	}
	return reinterpret_cast<D3DCompileFunction>(GetProcAddress(module, "D3DCompile"));
}

// Compiles and, when the compiler refuses, prints what it said.  A test that only reports "did not
// compile" leaves the next person running fxc by hand to find out why.
static bool compiles(D3DCompileFunction compile, const std::string & hlsl, const char * profile)
{
	ID3DBlob * code = NULL;
	ID3DBlob * errors = NULL;
	const HRESULT result = compile(hlsl.c_str(), hlsl.size(), "ffvertex", NULL, NULL,
		ENTRY_POINT, profile, 0, 0, &code, &errors);

	if (FAILED(result) && errors != NULL) {
		printf("  %s: %s\n", profile, static_cast<const char *>(errors->GetBufferPointer()));
	}
	if (code != NULL) {
		code->Release();
	}
	if (errors != NULL) {
		errors->Release();
	}
	return SUCCEEDED(result);
}

static VertexPipelineDescription plain_description()
{
	VertexPipelineDescription description;
	memset(&description, 0, sizeof(description));
	description.FVF = D3DFVF_XYZ|D3DFVF_NORMAL|D3DFVF_TEX2|D3DFVF_DIFFUSE;
	description.ColourVertexEnabled = true;
	description.DiffuseMaterialSource = D3DMCS_COLOR1;
	description.AmbientMaterialSource = D3DMCS_MATERIAL;
	description.EmissiveMaterialSource = D3DMCS_MATERIAL;
	description.SpecularMaterialSource = D3DMCS_MATERIAL;
	description.StageCount = 1;
	description.Stages[0].TextureCoordinateIndex = D3DTSS_TCI_PASSTHRU;
	description.Stages[0].TextureTransformFlags = D3DTTFF_DISABLE;
	description.FogVertexMode = D3DFOG_NONE;
	return description;
}

// One entry per shape the game is known to produce, so a refusal names the shape that broke.
struct CompileCase
{
	const char * Name;
	bool LightingEnabled;
	bool SpecularEnabled;
	unsigned LightCount;
	DWORD LightType;
	DWORD TextureCoordinateIndex;
	DWORD TextureTransformFlags;
	bool FogEnabled;
	DWORD FogVertexMode;
};

static const CompileCase COMPILE_CASES[] = {
	{ "unlit pass-through",      false, false, 0, 0,
	  D3DTSS_TCI_PASSTHRU, D3DTTFF_DISABLE, false, D3DFOG_NONE },
	{ "one directional light",   true,  false, 1, D3DLIGHT_DIRECTIONAL,
	  D3DTSS_TCI_PASSTHRU, D3DTTFF_DISABLE, false, D3DFOG_NONE },
	{ "three directional lights",true,  false, 3, D3DLIGHT_DIRECTIONAL,
	  D3DTSS_TCI_PASSTHRU, D3DTTFF_DISABLE, false, D3DFOG_NONE },
	{ "point light",             true,  false, 1, D3DLIGHT_POINT,
	  D3DTSS_TCI_PASSTHRU, D3DTTFF_DISABLE, false, D3DFOG_NONE },
	{ "spot light",              true,  false, 1, D3DLIGHT_SPOT,
	  D3DTSS_TCI_PASSTHRU, D3DTTFF_DISABLE, false, D3DFOG_NONE },
	{ "specular highlight",      true,  true,  1, D3DLIGHT_DIRECTIONAL,
	  D3DTSS_TCI_PASSTHRU, D3DTTFF_DISABLE, false, D3DFOG_NONE },
	{ "camera space position",   false, false, 0, 0,
	  D3DTSS_TCI_CAMERASPACEPOSITION, D3DTTFF_COUNT2, false, D3DFOG_NONE },
	{ "camera space normal",     false, false, 0, 0,
	  D3DTSS_TCI_CAMERASPACENORMAL, D3DTTFF_DISABLE, false, D3DFOG_NONE },
	{ "reflection vector",       false, false, 0, 0,
	  D3DTSS_TCI_CAMERASPACEREFLECTIONVECTOR, D3DTTFF_DISABLE, false, D3DFOG_NONE },
	{ "projected transform",     false, false, 0, 0,
	  D3DTSS_TCI_CAMERASPACEPOSITION, D3DTTFF_COUNT3|D3DTTFF_PROJECTED, false, D3DFOG_NONE },
	{ "linear fog",              false, false, 0, 0,
	  D3DTSS_TCI_PASSTHRU, D3DTTFF_DISABLE, true, D3DFOG_LINEAR },
	{ "exponential fog",         false, false, 0, 0,
	  D3DTSS_TCI_PASSTHRU, D3DTTFF_DISABLE, true, D3DFOG_EXP },
	{ "squared exponential fog", false, false, 0, 0,
	  D3DTSS_TCI_PASSTHRU, D3DTTFF_DISABLE, true, D3DFOG_EXP2 }
};

static VertexPipelineDescription description_for(const CompileCase & compile_case)
{
	VertexPipelineDescription description = plain_description();
	description.LightingEnabled = compile_case.LightingEnabled;
	description.SpecularEnabled = compile_case.SpecularEnabled;
	description.LightCount = compile_case.LightCount;
	for (unsigned index = 0; index < compile_case.LightCount; ++index) {
		description.Lights[index].Type = compile_case.LightType;
	}
	description.Stages[0].TextureCoordinateIndex = compile_case.TextureCoordinateIndex;
	description.Stages[0].TextureTransformFlags = compile_case.TextureTransformFlags;
	description.FogEnabled = compile_case.FogEnabled;
	description.FogVertexMode = compile_case.FogVertexMode;
	return description;
}

TEST(ffvertexcompile_every_shape_the_game_produces_compiles_on_both_profiles)
{
	D3DCompileFunction compile = load_compiler();
	if (compile == NULL) {
		printf("  d3dcompiler_47.dll not present, skipping\n");
		return;
	}

	const size_t case_count = sizeof(COMPILE_CASES)/sizeof(COMPILE_CASES[0]);
	for (size_t index = 0; index < case_count; ++index) {
		const CompileCase & compile_case = COMPILE_CASES[index];
		const VertexPipelineDescription description = description_for(compile_case);

		std::string nine;
		std::string eleven;
		CHECK(VertexShader_Generate(description, VERTEX_SHADER_TARGET_D3D9, nine));
		CHECK(VertexShader_Generate(description, VERTEX_SHADER_TARGET_D3D11, eleven));

		if (!compiles(compile, nine, D3D9_PROFILE)) {
			printf("  case refused on %s: %s\n", D3D9_PROFILE, compile_case.Name);
			CHECK(false);
		}
		if (!compiles(compile, eleven, D3D11_PROFILE)) {
			printf("  case refused on %s: %s\n", D3D11_PROFILE, compile_case.Name);
			CHECK(false);
		}
	}
}

// Two stages, each generating a different way, which is the terrain's shape and the widest program
// the generator writes.
TEST(ffvertexcompile_a_two_stage_program_compiles)
{
	D3DCompileFunction compile = load_compiler();
	if (compile == NULL) {
		printf("  d3dcompiler_47.dll not present, skipping\n");
		return;
	}

	VertexPipelineDescription description = plain_description();
	description.LightingEnabled = true;
	description.LightCount = 2;
	description.Lights[0].Type = D3DLIGHT_DIRECTIONAL;
	description.Lights[1].Type = D3DLIGHT_POINT;
	description.StageCount = 2;
	description.Stages[0].TextureCoordinateIndex = D3DTSS_TCI_PASSTHRU;
	description.Stages[0].TextureTransformFlags = D3DTTFF_DISABLE;
	description.Stages[1].TextureCoordinateIndex = D3DTSS_TCI_CAMERASPACENORMAL;
	description.Stages[1].TextureTransformFlags = D3DTTFF_COUNT2;

	std::string nine;
	std::string eleven;
	CHECK(VertexShader_Generate(description, VERTEX_SHADER_TARGET_D3D9, nine));
	CHECK(VertexShader_Generate(description, VERTEX_SHADER_TARGET_D3D11, eleven));
	CHECK(compiles(compile, nine, D3D9_PROFILE));
	CHECK(compiles(compile, eleven, D3D11_PROFILE));
}

// The smoke billboards' glow: the unlit vertex half hands the normal on in the specular slot, keyed
// apart from the same program without it, and refuses a lit draw, whose normal is a normal.
TEST(ffvertexcompile_the_smoke_glow_rides_the_specular_slot)
{
	VertexPipelineDescription plain = plain_description();
	VertexPipelineDescription glow = plain;
	glow.SmokeGlow = true;

	std::string plain_hlsl;
	std::string glow_hlsl;
	CHECK(VertexShader_Generate(plain, VERTEX_SHADER_TARGET_D3D11, plain_hlsl));
	CHECK(VertexShader_Generate(glow, VERTEX_SHADER_TARGET_D3D11, glow_hlsl));
	CHECK(glow_hlsl.find("output.Specular = float4(input.Normal, 0.0);") != std::string::npos);
	CHECK(plain_hlsl.find("output.Specular = float4(input.Normal") == std::string::npos);
	CHECK(VertexShader_Key(glow) == VertexShader_Key(plain) + ":G");

	VertexPipelineDescription lit = glow;
	lit.LightingEnabled = true;
	CHECK(!VertexShader_Generate(lit, VERTEX_SHADER_TARGET_D3D11, glow_hlsl));
	VertexPipelineDescription no_normal = glow;
	no_normal.FVF = D3DFVF_XYZ|D3DFVF_TEX2|D3DFVF_DIFFUSE;
	CHECK(!VertexShader_Generate(no_normal, VERTEX_SHADER_TARGET_D3D11, glow_hlsl));

	D3DCompileFunction compile = load_compiler();
	if (compile == NULL) {
		printf("  d3dcompiler_47.dll not present, skipping the compile\n");
		return;
	}
	CHECK(VertexShader_Generate(glow, VERTEX_SHADER_TARGET_D3D11, glow_hlsl));
	CHECK(compiles(compile, glow_hlsl, D3D11_PROFILE));
}

// A soft billboard's half width rides the specular alpha out of the second coordinate set, after
// the glow wrote the colour, so the pixel half's fade can read it.  Keyed apart, and refused on a
// format with one coordinate set.
TEST(ffvertexcompile_a_soft_billboard_hands_its_half_width_on)
{
	VertexPipelineDescription glow = plain_description();
	glow.SmokeGlow = true;
	VertexPipelineDescription soft = glow;
	soft.SoftParticle = true;

	std::string hlsl;
	CHECK(VertexShader_Generate(soft, VERTEX_SHADER_TARGET_D3D11, hlsl));
	const size_t colour = hlsl.find("output.Specular = float4(input.Normal, 0.0);");
	const size_t width = hlsl.find("output.Specular.a = input.TexCoord1.x;");
	CHECK(colour != std::string::npos);
	CHECK(width != std::string::npos);
	CHECK(colour < width);
	CHECK(VertexShader_Key(soft) == VertexShader_Key(glow) + ":Z");

	VertexPipelineDescription one_set = soft;
	one_set.FVF = D3DFVF_XYZ|D3DFVF_NORMAL|D3DFVF_TEX1|D3DFVF_DIFFUSE;
	CHECK(!VertexShader_Generate(one_set, VERTEX_SHADER_TARGET_D3D11, hlsl));

	D3DCompileFunction compile = load_compiler();
	if (compile == NULL) {
		printf("  d3dcompiler_47.dll not present, skipping the compile\n");
		return;
	}
	CHECK(VertexShader_Generate(soft, VERTEX_SHADER_TARGET_D3D11, hlsl));
	CHECK(compiles(compile, hlsl, D3D11_PROFILE));
}

// The Hemispheric Ambient program is keyed apart and swings the scene ambient on the normal's height
// through the unused half of MaterialPower.  With the option off the text does not move, which is
// what keeps the shipped shader cache whole.  Refused on an unlit draw.
TEST(ffvertexcompile_a_lit_draw_takes_the_ambient_from_sky_and_ground)
{
	VertexPipelineDescription flat = plain_description();
	flat.LightingEnabled = true;
	flat.LightCount = 1;
	flat.Lights[0].Type = FF_LIGHT_DIRECTIONAL;
	VertexPipelineDescription hemi = flat;
	hemi.HemiAmbient = true;

	std::string flat_hlsl;
	std::string hemi_hlsl;
	CHECK(VertexShader_Generate(flat, VERTEX_SHADER_TARGET_D3D11, flat_hlsl));
	CHECK(VertexShader_Generate(hemi, VERTEX_SHADER_TARGET_D3D11, hemi_hlsl));
	CHECK(flat_hlsl.find("MaterialPower.yzw") == std::string::npos);
	CHECK(flat_hlsl.find("(GlobalAmbient.rgb + ambient_light)") != std::string::npos);
	CHECK(hemi_hlsl.find("dot(view_normal, MaterialPower.yzw)") != std::string::npos);
	CHECK(hemi_hlsl.find("(hemi_ambient + ambient_light)") != std::string::npos);
	CHECK(VertexShader_Key(hemi) == VertexShader_Key(flat) + ":H");

	VertexPipelineDescription unlit = plain_description();
	unlit.HemiAmbient = true;
	CHECK(!VertexShader_Generate(unlit, VERTEX_SHADER_TARGET_D3D11, hemi_hlsl));

	D3DCompileFunction compile = load_compiler();
	if (compile == NULL) {
		printf("  d3dcompiler_47.dll not present, skipping the compile\n");
		return;
	}
	CHECK(VertexShader_Generate(hemi, VERTEX_SHADER_TARGET_D3D11, hemi_hlsl));
	CHECK(compiles(compile, hemi_hlsl, D3D11_PROFILE));
}

// A fire billboard's age and seed ride the second coordinate set through to TexCoord1, written after
// the empty stages are zeroed so the zero does not win.  Keyed apart; refused with one set or a
// second stage.
TEST(ffvertexcompile_a_fire_billboard_hands_its_age_on)
{
	VertexPipelineDescription soft = plain_description();
	soft.SoftParticle = true;
	VertexPipelineDescription fire = soft;
	fire.FireLook = true;

	std::string hlsl;
	CHECK(VertexShader_Generate(fire, VERTEX_SHADER_TARGET_D3D11, hlsl));
	const size_t zero = hlsl.find("output.TexCoord1 = float2(0.0, 0.0);");
	const size_t pass = hlsl.find("output.TexCoord1 = input.TexCoord1;");
	CHECK(zero != std::string::npos);
	CHECK(pass != std::string::npos);
	CHECK(zero < pass);
	CHECK(VertexShader_Key(fire) == VertexShader_Key(soft) + ":B");

	VertexPipelineDescription one_set = fire;
	one_set.FVF = D3DFVF_XYZ|D3DFVF_NORMAL|D3DFVF_TEX1|D3DFVF_DIFFUSE;
	CHECK(!VertexShader_Generate(one_set, VERTEX_SHADER_TARGET_D3D11, hlsl));
	VertexPipelineDescription two_stages = fire;
	two_stages.StageCount = 2;
	two_stages.Stages[1] = two_stages.Stages[0];
	CHECK(!VertexShader_Generate(two_stages, VERTEX_SHADER_TARGET_D3D11, hlsl));

	D3DCompileFunction compile = load_compiler();
	if (compile == NULL) {
		printf("  d3dcompiler_47.dll not present, skipping the compile\n");
		return;
	}
	CHECK(VertexShader_Generate(fire, VERTEX_SHADER_TARGET_D3D11, hlsl));
	CHECK(compiles(compile, hlsl, D3D11_PROFILE));
}
