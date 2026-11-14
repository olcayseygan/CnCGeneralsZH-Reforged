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

// The transcribed engine shaders: what they say, and whether the compiler takes them.
//
// A transcription has no generator to argue with - it is one hand-written program per shipped
// file - so what is worth pinning is the reading of the assembly rather than the shape of the
// text.  Each check below names the instruction it came from, so a term that turns out wrong on
// screen can be traced back to the line of vs_1_1 it was read out of.
//
// The compile check loads D3DCompile out of d3dcompiler_47.dll, which ships with Windows; a
// machine without it skips rather than fails.

#include "test_harness.h"

#include "engineshader.h"
#include "ffvertex.h"

#include <d3dcommon.h>
#include <stdio.h>
#include <string>
#include <windows.h>

typedef HRESULT (WINAPI *D3DCompileFunction)(LPCVOID source_data, SIZE_T source_size,
	LPCSTR source_name, const D3D_SHADER_MACRO * defines, ID3DInclude * include,
	LPCSTR entry_point, LPCSTR target, UINT flags1, UINT flags2, ID3DBlob ** code,
	ID3DBlob ** error_messages);

static const char * const COMPILER_MODULE = "d3dcompiler_47.dll";
static const char * const ENTRY_POINT = "main";
static const char * const VERTEX_PROFILE = "vs_4_0";
static const char * const PIXEL_PROFILE = "ps_4_0";

static bool contains(const std::string & text, const char * fragment)
{
	return text.find(fragment) != std::string::npos;
}

// Everything off, which is what the water draws with: no alpha test and no fog.
static PixelPipelineDescription plain_pipeline()
{
	PixelPipelineDescription pipeline;
	memset(&pipeline, 0, sizeof(pipeline));
	return pipeline;
}

TEST(engineshader_names_a_shader_by_its_file_whatever_case_the_path_is_in)
{
	CHECK(EngineShader_From_File("shaders\\Trees.vso") == ENGINE_SHADER_TREES);
	CHECK(EngineShader_From_File("SHADERS/TREES.VSO") == ENGINE_SHADER_TREES);
	CHECK(EngineShader_From_File("Trees.vso") == ENGINE_SHADER_TREES);
	CHECK(EngineShader_From_File("trapezoid water ps.1.1") == ENGINE_SHADER_WATER_TRAPEZOID);
	CHECK(EngineShader_From_File("river water ps.1.1") == ENGINE_SHADER_WATER_RIVER);
	CHECK(EngineShader_From_File("water reflection ps.1.1") == ENGINE_SHADER_WATER_REFLECTION);
	CHECK(EngineShader_From_File("shaders\\terrainnoise2.pso") == ENGINE_SHADER_TERRAIN_NOISE_2);
	CHECK(EngineShader_From_File("shaders\\fterrain0.pso") == ENGINE_SHADER_FLAT_TERRAIN_BASE);
	CHECK(EngineShader_From_File("shaders\\roadnoise2.pso") == ENGINE_SHADER_ROAD_NOISE_2);
	CHECK(EngineShader_From_File("shaders\\monochrome.pso") == ENGINE_SHADER_MONOCHROME);
	CHECK(EngineShader_From_File("shaders\\grass.vso") == ENGINE_SHADER_GRASS);
	CHECK(EngineShader_From_File("SHADERS/GRASS.VSO") == ENGINE_SHADER_GRASS);
}

// The grass is Trees.vso registered under a second name: the same shear by height above the root,
// the same shroud and output, and on top of it a fold down to the root past c19.w from the focus in
// c19.xy, across a band one over c20.x wide.  The tree text must not gain the fold.
TEST(engineshader_grass_keeps_the_tree_sway_and_folds_down_past_the_radius)
{
	std::string grass;
	std::string trees;
	CHECK(EngineShader_Vertex_Program(ENGINE_SHADER_GRASS, grass));
	CHECK(EngineShader_Vertex_Program(ENGINE_SHADER_TREES, trees));
	CHECK(contains(grass, "height * c[8 + sway_type].xyz + input.Position"));
	CHECK(contains(grass, "dot(swayed, c[4])"));
	CHECK(contains(grass, "((float4(input.Position, 1.0) + c[32]) * c[33]).xy"));
	CHECK(contains(grass,
		"float grown = saturate((c[19].w - distance(input.Position.xy, c[19].xy)) * c[20].x);"));
	CHECK(contains(grass,
		"swayed.xyz = lerp(float3(input.Position.xy, input.Sway.z), swayed.xyz, grown);"));
	// The fold lands between the sway and the transform, or it would fold the unswayed tuft.
	CHECK(grass.find("float4 swayed") < grass.find("float grown"));
	CHECK(grass.find("swayed.xyz = lerp") < grass.find("dot(swayed, c[4])"));
	CHECK(!contains(trees, "c[19]"));
	CHECK(!contains(trees, "c[20]"));

	// ffvertex's output structure, as the tree program writes it.
	const size_t output = grass.find("struct Output");
	CHECK(output != std::string::npos);
	CHECK(grass.find("float Fog : FOG;", output) != std::string::npos);
	CHECK(!EngineShader_Pixel_Program(ENGINE_SHADER_GRASS, plain_pipeline(), grass));
	CHECK(!EngineShader_Paints_Ground(ENGINE_SHADER_GRASS));
}

// Every program the table names has to write something, or a draw that binds it is refused with a
// name that says it is covered.
TEST(engineshader_writes_every_program_it_names)
{
	static const EngineShaderProgram PIXEL_PROGRAMS[] = {
		ENGINE_SHADER_WATER_TRAPEZOID, ENGINE_SHADER_WATER_RIVER, ENGINE_SHADER_WATER_REFLECTION,
		ENGINE_SHADER_TERRAIN, ENGINE_SHADER_TERRAIN_NOISE, ENGINE_SHADER_TERRAIN_NOISE_2,
		ENGINE_SHADER_FLAT_TERRAIN, ENGINE_SHADER_FLAT_TERRAIN_BASE,
		ENGINE_SHADER_FLAT_TERRAIN_NOISE, ENGINE_SHADER_FLAT_TERRAIN_NOISE_2,
		ENGINE_SHADER_ROAD_NOISE_2, ENGINE_SHADER_MONOCHROME
	};

	for (unsigned index = 0; index < sizeof(PIXEL_PROGRAMS) / sizeof(PIXEL_PROGRAMS[0]); ++index) {
		std::string hlsl;
		CHECK(EngineShader_Pixel_Program(PIXEL_PROGRAMS[index], plain_pipeline(), hlsl));
		CHECK(contains(hlsl, "return current;"));
	}
}

// The shaders with no transcription yet stay foreign, which is what keeps a draw that binds one a
// refusal instead of a draw with the wrong program.
TEST(engineshader_has_no_program_for_a_shader_nobody_transcribed)
{
	CHECK(EngineShader_From_File("shaders\\wave.vso") == ENGINE_SHADER_NONE);
	CHECK(EngineShader_From_File("environment water ps.1.1") == ENGINE_SHADER_NONE);
	CHECK(EngineShader_From_File("Treesomething.vso") == ENGINE_SHADER_NONE);
	CHECK(EngineShader_From_File(NULL) == ENGINE_SHADER_NONE);

	std::string hlsl;
	CHECK(!EngineShader_Vertex_Program(ENGINE_SHADER_NONE, hlsl));
	CHECK(!EngineShader_Pixel_Program(ENGINE_SHADER_NONE, plain_pipeline(), hlsl));
}

// The two halves are not interchangeable: asking for the tree program as a pixel shader, or a water
// one as a vertex shader, is a mistake the caller has to be told about rather than a program.
TEST(engineshader_keeps_the_two_halves_apart)
{
	std::string hlsl;
	CHECK(!EngineShader_Pixel_Program(ENGINE_SHADER_TREES, plain_pipeline(), hlsl));
	CHECK(!EngineShader_Vertex_Program(ENGINE_SHADER_WATER_TRAPEZOID, hlsl));
	CHECK(!EngineShader_Vertex_Program(ENGINE_SHADER_WATER_RIVER, hlsl));
}

// mul r0, v0, t0 ; mad r0.rgb, t1, t2, r0 ; mul r0.rgb, r0, t3, with every ps_1_1 result clamped.
// The shroud multiplies the colour and never the alpha: shrouded water is dark, not transparent.
TEST(engineshader_adds_the_sparkles_and_then_shrouds_the_trapezoid_water)
{
	std::string hlsl;
	CHECK(EngineShader_Pixel_Program(ENGINE_SHADER_WATER_CLASSIC_TRAPEZOID, plain_pipeline(), hlsl));
	CHECK(contains(hlsl, "float4 current = saturate(input.Diffuse * texel0);"));
	CHECK(contains(hlsl, "current.rgb = saturate(texel1.rgb * texel2.rgb + current.rgb);"));
	CHECK(contains(hlsl, "current.rgb = saturate(current.rgb * texel3.rgb);"));
}

// The river keeps its sparkles apart from the base colour and darkens only those by the vertex
// alpha, which is where the shroud rides.  The co-issued +mul is what puts the shroud in the alpha.
TEST(engineshader_darkens_only_the_river_waters_sparkles_by_the_vertex_alpha)
{
	std::string hlsl;
	CHECK(EngineShader_Pixel_Program(ENGINE_SHADER_WATER_CLASSIC_RIVER, plain_pipeline(), hlsl));
	CHECK(contains(hlsl, "current.rgb = saturate(input.Diffuse.rgb * texel0.rgb);"));
	CHECK(contains(hlsl, "current.a = saturate(texel0.a * texel3.a);"));
	CHECK(contains(hlsl, "sparkle = saturate(sparkle * input.Diffuse.a);"));
	CHECK(contains(hlsl, "current.rgb = saturate(current.rgb + sparkle);"));
}

// mul r0.rgb, t0, t3 ; +mov r0.a, c0.  D3D9 takes the opacity from c0, which a transcribed program
// cannot see, so the alpha has to be the texture factor W3DWater sets to the same value.  Reading
// c0 here would come back zero and the reflection would not show at all.
// The reflection multiplies the water, so white has to come out white or open sky tints every pool.
TEST(engineshader_fades_the_water_reflection_towards_white_by_the_texture_factor)
{
	std::string hlsl;
	CHECK(EngineShader_Pixel_Program(ENGINE_SHADER_WATER_REFLECTION, plain_pipeline(), hlsl));
	CHECK(contains(hlsl, "float coverage = saturate((1.0 - dot(texel0.rgb, float3(0.333333, 0.333333, 0.333333))) * 4.0);"));
	CHECK(contains(hlsl, "float4 current = float4((1.0 - TextureFactor.a * coverage).xxx, TextureFactor.a);"));
	CHECK(!contains(hlsl, "* texel3"));
}

// dp3 r1, t0, c0 ; mul r1, r1, c1 ; lrp r0, c2, r1, t0.  Without it the briefing grey was a refused
// draw over a frame that went into a texture and never came out, which is a black screen.
TEST(engineshader_greys_the_frame_by_the_texture_factors_tint_and_fade)
{
	std::string hlsl;
	CHECK(EngineShader_Pixel_Program(ENGINE_SHADER_MONOCHROME, plain_pipeline(), hlsl));
	CHECK(contains(hlsl, "float luminance = saturate(dot(texel0.rgb, float3(0.3, 0.59, 0.11)));"));
	CHECK(contains(hlsl, "float4 tinted = saturate(luminance * float4(TextureFactor.rgb, 1.0));"));
	CHECK(contains(hlsl, "float4 current = lerp(texel0, tinted, float4(TextureFactor.aaa, 1.0));"));
}

// D3D9 applies the alpha test and the fog around a bound pixel shader and D3D11 applies neither, so
// a transcription that leaves them out draws water through the fog at full strength.
TEST(engineshader_writes_the_alpha_test_and_the_fog_into_a_water_program)
{
	PixelPipelineDescription pipeline = plain_pipeline();
	pipeline.FogEnabled = true;
	pipeline.AlphaTestEnabled = true;
	pipeline.AlphaFunction = D3DCMP_GREATEREQUAL;

	const EngineShaderProgram programs[] = { ENGINE_SHADER_WATER_CLASSIC_TRAPEZOID,
		ENGINE_SHADER_WATER_TRAPEZOID, ENGINE_SHADER_WATER_RIVER };
	for (const EngineShaderProgram program : programs) {
		std::string hlsl;
		CHECK(EngineShader_Pixel_Program(program, pipeline, hlsl));
		CHECK(contains(hlsl, "lerp(FogColour.rgb, current.rgb, saturate(input.Fog))"));
		CHECK(contains(hlsl, "AlphaReference"));
	}
}

// The Modern names are the Modern text on Direct3D 11 as well, and the Classic names keep the
// transcription that target always wrote, so a Classic frame compiles exactly what it did.
TEST(engineshader_writes_modern_water_only_for_the_modern_names)
{
	std::string modern, classic;
	CHECK(EngineShader_Pixel_Program(ENGINE_SHADER_WATER_TRAPEZOID, plain_pipeline(), modern));
	CHECK(contains(modern, "Texture14 : register(t14)"));
	CHECK(contains(modern, "cbuffer WaterVisualBlock2 : register(b3)"));
	CHECK(EngineShader_Pixel_Program(ENGINE_SHADER_WATER_CLASSIC_TRAPEZOID, plain_pipeline(), classic));
	CHECK(!contains(classic, "Texture4"));
	CHECK(EngineShader_Is_Modern_Water(ENGINE_SHADER_WATER_RIVER));
	CHECK(!EngineShader_Is_Modern_Water(ENGINE_SHADER_WATER_CLASSIC_RIVER));
}

// lrp r0, v0.w, t1, t0 is t0 + v0.w * (t1 - t0), so the vertex alpha is the weight of the second
// layer.  Reading it the other way round paints every blended ground tile inside out.
TEST(engineshader_blends_the_terrains_second_layer_by_the_vertex_alpha)
{
	std::string hlsl;
	CHECK(EngineShader_Pixel_Program(ENGINE_SHADER_TERRAIN, plain_pipeline(), hlsl));
	CHECK(contains(hlsl, "float4 current = saturate(lerp(texel0, texel1, input.Diffuse.a));"));
	CHECK(contains(hlsl, "current = saturate(current * input.Diffuse);"));

	// The preamble samples all four stages whatever the program reads, so what says this one stops
	// at two layers is that neither of the other two is multiplied in.
	CHECK(!contains(hlsl, "current * texel2"));
	CHECK(!contains(hlsl, "current * texel3"));
}

// The three noise variants are the same program with one and then two more multiplies: the cloud
// shadow at stage two and the shroud at stage three.
TEST(engineshader_adds_one_stage_per_noise_variant)
{
	std::string plain;
	std::string noise;
	std::string noise_two;
	CHECK(EngineShader_Pixel_Program(ENGINE_SHADER_TERRAIN, plain_pipeline(), plain));
	CHECK(EngineShader_Pixel_Program(ENGINE_SHADER_TERRAIN_NOISE, plain_pipeline(), noise));
	CHECK(EngineShader_Pixel_Program(ENGINE_SHADER_TERRAIN_NOISE_2, plain_pipeline(), noise_two));

	CHECK(!contains(plain, "current * texel2"));
	CHECK(contains(noise, "current = saturate(current * texel2);"));
	CHECK(!contains(noise, "current * texel3"));
	CHECK(contains(noise_two, "current = saturate(current * texel2);"));
	CHECK(contains(noise_two, "current = saturate(current * texel3);"));
}

// The flat terrain multiplies its two layers instead of blending them, and fterrain0 reads only the
// second: t1 with no t0 at all.
TEST(engineshader_multiplies_the_flat_terrains_layers)
{
	std::string flat;
	std::string base;
	CHECK(EngineShader_Pixel_Program(ENGINE_SHADER_FLAT_TERRAIN, plain_pipeline(), flat));
	CHECK(EngineShader_Pixel_Program(ENGINE_SHADER_FLAT_TERRAIN_BASE, plain_pipeline(), base));
	CHECK(contains(flat, "float4 current = saturate(texel1 * texel0);"));
	CHECK(contains(base, "float4 current = saturate(texel1);"));
}

// The road multiplies the vertex colour in last where the terrain does it second.  Same arithmetic
// until a term saturates, and ps_1_1 saturates every one.
TEST(engineshader_leaves_the_roads_vertex_colour_until_last)
{
	std::string hlsl;
	CHECK(EngineShader_Pixel_Program(ENGINE_SHADER_ROAD_NOISE_2, plain_pipeline(), hlsl));
	CHECK(contains(hlsl,
		"    float4 current = saturate(texel0 * texel1);\n"
		"    current = saturate(current * texel2);\n"
		"    current = saturate(current * input.Diffuse);\n"));
}

// Four samplers and four coordinate sets, the same ones ffshader declares, because the backend
// binds one set of textures whichever half wrote the program.
TEST(engineshader_declares_the_four_stages_the_water_samples)
{
	std::string hlsl;
	CHECK(EngineShader_Pixel_Program(ENGINE_SHADER_WATER_CLASSIC_RIVER, plain_pipeline(), hlsl));
	for (unsigned stage = 0; stage < MAXIMUM_COMBINER_STAGES; ++stage) {
		char sampler[64];
		snprintf(sampler, sizeof(sampler), "Texture%u.Sample(Sampler%u, input.TexCoord%u)",
			stage, stage, stage);
		CHECK(contains(hlsl, sampler));
	}
}

// m4x4 oPos, r1, c4 is four dot products against four consecutive registers, and the engine
// transposes the matrix before it uploads it for exactly that reason.
TEST(engineshader_puts_the_tree_through_four_dot_products_against_c4)
{
	std::string hlsl;
	CHECK(EngineShader_Vertex_Program(ENGINE_SHADER_TREES, hlsl));
	CHECK(contains(hlsl, "dot(swayed, c[4])"));
	CHECK(contains(hlsl, "dot(swayed, c[5])"));
	CHECK(contains(hlsl, "dot(swayed, c[6])"));
	CHECK(contains(hlsl, "dot(swayed, c[7])"));
}

// mov a0.x, v1 then mov r0, c8[a0.x]: the sway type rides in the normal slot's x, and c8 is the
// entry a tree with no sway reads.  mad r1, r2.zzzw, r0, v0 scales that by the height above the
// tree's own base, which r2.z holds.
TEST(engineshader_leans_a_tree_by_its_height_above_its_own_base)
{
	std::string hlsl;
	CHECK(EngineShader_Vertex_Program(ENGINE_SHADER_TREES, hlsl));
	CHECK(contains(hlsl, "int sway_type = (int)(input.Sway.x + 0.5);"));
	CHECK(contains(hlsl, "float height = input.Position.z - input.Sway.z;"));
	CHECK(contains(hlsl, "height * c[8 + sway_type].xyz + input.Position"));
}

// mov r2, v1.yyyw then mul oD0, v2, r2: the shade multiplies the colour and leaves the alpha,
// because a float3 input reads its w as one.  Getting that swizzle wrong is a tree whose leaves
// have the crown's shading in their alpha and no gaps between them.
TEST(engineshader_shades_the_tree_colour_and_leaves_its_alpha_alone)
{
	std::string hlsl;
	CHECK(EngineShader_Vertex_Program(ENGINE_SHADER_TREES, hlsl));
	CHECK(contains(hlsl, "output.Diffuse = input.Diffuse * float4(input.Sway.yyy, 1.0);"));
}

// add r1, v0, c32 then mul oT1, r1, c33, with the shroud's origin in c32 and its texel size in
// c33.  This is the second coordinate set and the only thing that reads it is the shroud stage.
TEST(engineshader_builds_the_shroud_coordinate_from_c32_and_c33)
{
	std::string hlsl;
	CHECK(EngineShader_Vertex_Program(ENGINE_SHADER_TREES, hlsl));
	CHECK(contains(hlsl, "((float4(input.Position, 1.0) + c[32]) * c[33]).xy"));
}

// The pixel half is still generated by ffshader, and shader model 4 links the two by slot in
// declaration order.  A transcription that writes a different set of members links against nothing.
// Every one of ffvertex's coordinate sets, in order, and Fog after them: with two, Fog sat where the pixel
// programs read TexCoord2, and Direct3D 12 refused the pair (X1).
TEST(engineshader_writes_the_output_structure_ffvertex_writes)
{
	std::string hlsl;
	CHECK(EngineShader_Vertex_Program(ENGINE_SHADER_TREES, hlsl));
	CHECK(contains(hlsl, "float4 Position : SV_Position;"));
	CHECK(contains(hlsl, "float4 Diffuse  : COLOR0;"));
	CHECK(contains(hlsl, "float4 Specular : COLOR1;"));
	// Searched from struct Output: the trees' struct Input comes first and has its own TexCoord0.
	const size_t output = hlsl.find("struct Output");
	CHECK(output != std::string::npos);
	size_t previous = hlsl.find("float4 Specular : COLOR1;", output);
	for (unsigned stage = 0; stage < MAXIMUM_VERTEX_STAGES; ++stage) {
		char member[64];
		snprintf(member, sizeof(member), "float2 TexCoord%u : TEXCOORD%u;", stage, stage);
		const size_t at = hlsl.find(member, output);
		CHECK(at != std::string::npos && previous != std::string::npos && at > previous);
		previous = at;
	}
	const size_t fog = hlsl.find("float Fog : FOG;", output);
	CHECK(fog != std::string::npos && previous != std::string::npos && fog > previous);
}

// The input has to be the byte layout of DX8_FVF_XYZNDUV1, because that is what the tree vertex
// buffer holds and what dx11layout builds the input layout from.  CreateInputLayout refuses a
// program whose inputs the layout does not have.
TEST(engineshader_reads_the_vertex_the_tree_buffer_actually_holds)
{
	std::string hlsl;
	CHECK(EngineShader_Vertex_Program(ENGINE_SHADER_TREES, hlsl));
	CHECK(contains(hlsl, "float3 Position : POSITION;"));
	CHECK(contains(hlsl, "float3 Sway : NORMAL;"));
	CHECK(contains(hlsl, "float4 Diffuse : COLOR0;"));
	CHECK(contains(hlsl, "float2 TexCoord0 : TEXCOORD0;"));
}

// Compiles and, when the compiler refuses, prints what it said.  A test that only reports "did not
// compile" leaves the next person running fxc by hand to find out why.
static bool compiles(D3DCompileFunction compile, const std::string & hlsl, const char * name,
	const char * profile)
{
	ID3DBlob * code = NULL;
	ID3DBlob * errors = NULL;
	const HRESULT result = compile(hlsl.c_str(), hlsl.size(), name, NULL, NULL,
		ENTRY_POINT, profile, 0, 0, &code, &errors);

	if (FAILED(result) && errors != NULL) {
		printf("  %s: %s\n", name, static_cast<const char *>(errors->GetBufferPointer()));
	}
	if (code != NULL) {
		code->Release();
	}
	if (errors != NULL) {
		errors->Release();
	}
	return SUCCEEDED(result);
}

TEST(engineshader_writes_programs_the_compiler_accepts)
{
	HMODULE module = LoadLibraryA(COMPILER_MODULE);
	if (module == NULL) {
		printf("  %s is not on this machine, skipping the compile\n", COMPILER_MODULE);
		return;
	}

	D3DCompileFunction compile =
		reinterpret_cast<D3DCompileFunction>(GetProcAddress(module, "D3DCompile"));
	CHECK(compile != NULL);
	if (compile == NULL) {
		return;
	}

	std::string trees;
	CHECK(EngineShader_Vertex_Program(ENGINE_SHADER_TREES, trees));
	CHECK(compiles(compile, trees, EngineShader_Name(ENGINE_SHADER_TREES), VERTEX_PROFILE));

	std::string grass;
	CHECK(EngineShader_Vertex_Program(ENGINE_SHADER_GRASS, grass));
	CHECK(compiles(compile, grass, EngineShader_Name(ENGINE_SHADER_GRASS), VERTEX_PROFILE));

	PixelPipelineDescription pipeline = plain_pipeline();
	pipeline.FogEnabled = true;
	pipeline.AlphaTestEnabled = true;
	pipeline.AlphaFunction = D3DCMP_GREATER;

	std::string trapezoid;
	CHECK(EngineShader_Pixel_Program(ENGINE_SHADER_WATER_CLASSIC_TRAPEZOID, pipeline, trapezoid));
	CHECK(compiles(compile, trapezoid, EngineShader_Name(ENGINE_SHADER_WATER_CLASSIC_TRAPEZOID),
		PIXEL_PROFILE));

	std::string river;
	CHECK(EngineShader_Pixel_Program(ENGINE_SHADER_WATER_CLASSIC_RIVER, pipeline, river));
	CHECK(compiles(compile, river, EngineShader_Name(ENGINE_SHADER_WATER_CLASSIC_RIVER),
		PIXEL_PROFILE));

	std::string reflection;
	CHECK(EngineShader_Pixel_Program(ENGINE_SHADER_WATER_REFLECTION, pipeline, reflection));
	CHECK(compiles(compile, reflection, EngineShader_Name(ENGINE_SHADER_WATER_REFLECTION),
		PIXEL_PROFILE));

	std::string monochrome;
	CHECK(EngineShader_Pixel_Program(ENGINE_SHADER_MONOCHROME, pipeline, monochrome));
	CHECK(compiles(compile, monochrome, EngineShader_Name(ENGINE_SHADER_MONOCHROME), PIXEL_PROFILE));

	std::string trapezoid_foam;
	CHECK(EngineShader_Pixel_Program(ENGINE_SHADER_WATER_TRAPEZOID_FOAM, pipeline, trapezoid_foam));
	CHECK(contains(trapezoid_foam, "SceneDepth"));
	CHECK(compiles(compile, trapezoid_foam, EngineShader_Name(ENGINE_SHADER_WATER_TRAPEZOID_FOAM),
		PIXEL_PROFILE));

	std::string river_foam;
	CHECK(EngineShader_Pixel_Program(ENGINE_SHADER_WATER_RIVER_FOAM, pipeline, river_foam));
	CHECK(compiles(compile, river_foam, EngineShader_Name(ENGINE_SHADER_WATER_RIVER_FOAM),
		PIXEL_PROFILE));
}
