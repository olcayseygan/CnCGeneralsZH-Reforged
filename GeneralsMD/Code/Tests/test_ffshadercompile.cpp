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

// Every combiner program the game asks for, compiled.
//
// The running game compiles these for ps_2_0 and refuses the draw when one will not build, so the
// D3D9 half is proved every time -ffshader runs.  ps_4_0 is not: nothing has ever handed one of
// these to the D3D11 compiler, and the two profiles disagree about more than the number after the
// underscore - sampler declarations, the COLOR output semantic, and what may be written to a
// register.  This finds that out here rather than on the day the D3D11 backend first draws.
//
// The descriptions are -ffprobe's, measured across Flash Effect, Golden Oasis, ForgottenForestZH
// and Alpine Assault.

#include "test_harness.h"

#include "ffshader.h"

#include <d3dcommon.h>
#include <math.h>
#include <string.h>
#include <windows.h>

typedef HRESULT (WINAPI *D3DCompileFunction)(LPCVOID source_data, SIZE_T source_size,
	LPCSTR source_name, const D3D_SHADER_MACRO * defines, ID3DInclude * include,
	LPCSTR entry_point, LPCSTR target, UINT flags1, UINT flags2, ID3DBlob ** code,
	ID3DBlob ** error_messages);

static const char * const COMPILER_MODULE = "d3dcompiler_47.dll";
static const char * const ENTRY_POINT = "main";
static const char * const D3D9_PROFILE = "ps_2_0";
static const char * const D3D11_PROFILE = "ps_4_0";

static D3DCompileFunction load_compiler()
{
	HMODULE module = LoadLibraryA(COMPILER_MODULE);
	if (module == NULL) {
		return NULL;
	}
	return reinterpret_cast<D3DCompileFunction>(GetProcAddress(module, "D3DCompile"));
}

static bool compiles(D3DCompileFunction compile, const std::string & hlsl, const char * profile)
{
	ID3DBlob * code = NULL;
	ID3DBlob * errors = NULL;
	const HRESULT result = compile(hlsl.c_str(), hlsl.size(), "ffshader", NULL, NULL,
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

static CombinerStage one_stage(DWORD colour_operation, DWORD colour_argument1,
	DWORD colour_argument2, DWORD alpha_operation, DWORD alpha_argument1, DWORD alpha_argument2,
	DWORD coordinate_index, bool texture_bound)
{
	CombinerStage stage;
	memset(&stage, 0, sizeof(stage));
	stage.ColourOperation = colour_operation;
	stage.ColourArgument1 = colour_argument1;
	stage.ColourArgument2 = colour_argument2;
	stage.AlphaOperation = alpha_operation;
	stage.AlphaArgument1 = alpha_argument1;
	stage.AlphaArgument2 = alpha_argument2;
	stage.TextureCoordinateIndex = coordinate_index;
	stage.TextureBound = texture_bound;
	return stage;
}

// The operations -ffprobe found, one program each, so a refusal names the operation that broke.
static const DWORD MEASURED_OPERATIONS[] = {
	D3DTOP_SELECTARG1, D3DTOP_SELECTARG2, D3DTOP_MODULATE, D3DTOP_MODULATE2X, D3DTOP_MODULATE4X,
	D3DTOP_ADD, D3DTOP_ADDSIGNED, D3DTOP_SUBTRACT, D3DTOP_DOTPRODUCT3
};

TEST(ffshadercompile_every_operation_compiles_on_both_profiles)
{
	D3DCompileFunction compile = load_compiler();
	if (compile == NULL) {
		printf("  d3dcompiler_47.dll not present, skipping\n");
		return;
	}

	const size_t operation_count = sizeof(MEASURED_OPERATIONS)/sizeof(MEASURED_OPERATIONS[0]);
	for (size_t index = 0; index < operation_count; ++index) {
		CombinerDescription description;
		memset(&description, 0, sizeof(description));
		description.StageCount = 1;
		description.Stages[0] = one_stage(MEASURED_OPERATIONS[index], D3DTA_TEXTURE, D3DTA_DIFFUSE,
			MEASURED_OPERATIONS[index], D3DTA_TEXTURE, D3DTA_DIFFUSE, 0, true);

		std::string hlsl;
		std::string eleven;
		CHECK(CombinerShader_Generate(description, COMBINER_SHADER_TARGET_D3D9, hlsl));
		CHECK(CombinerShader_Generate(description, COMBINER_SHADER_TARGET_D3D11, eleven));
		CHECK(compiles(compile, hlsl, D3D9_PROFILE));
		CHECK(compiles(compile, eleven, D3D11_PROFILE));
	}
}

// The two-stage shroud pass: MULTIPLYADD against an alpha-replicated texture factor, then
// DOTPRODUCT3.  It is the widest program the generator writes and the only one using either
// operation, so it is the one most likely to say something different on ps_4_0.
TEST(ffshadercompile_the_widest_measured_program_compiles)
{
	D3DCompileFunction compile = load_compiler();
	if (compile == NULL) {
		printf("  d3dcompiler_47.dll not present, skipping\n");
		return;
	}

	CombinerDescription description;
	memset(&description, 0, sizeof(description));
	description.StageCount = 2;
	description.Stages[0] = one_stage(D3DTOP_MULTIPLYADD, D3DTA_TEXTURE, D3DTA_DIFFUSE,
		D3DTOP_SELECTARG1, D3DTA_TEXTURE, D3DTA_CURRENT, 0, true);
	description.Stages[0].ColourArgument0 = D3DTA_TFACTOR | D3DTA_ALPHAREPLICATE;
	description.Stages[1] = one_stage(D3DTOP_DOTPRODUCT3, D3DTA_TEXTURE, D3DTA_CURRENT,
		D3DTOP_MODULATE, D3DTA_TEXTURE, D3DTA_CURRENT, 1, true);

	std::string hlsl;
	std::string eleven;
	CHECK(CombinerShader_Generate(description, COMBINER_SHADER_TARGET_D3D9, hlsl));
	CHECK(CombinerShader_Generate(description, COMBINER_SHADER_TARGET_D3D11, eleven));
	CHECK(compiles(compile, hlsl, D3D9_PROFILE));
	CHECK(compiles(compile, eleven, D3D11_PROFILE));
}

// The alpha test and the fog exist only on the D3D11 profile, where they are shader instructions
// rather than pipeline state.  Every comparison is generated here, because a clip that will not
// compile is a pass that silently stays on the pipeline it was on.
TEST(ffshadercompile_every_alpha_comparison_and_the_fog_compile)
{
	D3DCompileFunction compile = load_compiler();
	if (compile == NULL) {
		printf("  d3dcompiler_47.dll not present, skipping\n");
		return;
	}

	static const DWORD COMPARISONS[] = {
		D3DCMP_NEVER, D3DCMP_LESS, D3DCMP_EQUAL, D3DCMP_LESSEQUAL,
		D3DCMP_GREATER, D3DCMP_NOTEQUAL, D3DCMP_GREATEREQUAL, D3DCMP_ALWAYS
	};
	const size_t comparison_count = sizeof(COMPARISONS)/sizeof(COMPARISONS[0]);

	for (size_t index = 0; index < comparison_count; ++index) {
		CombinerDescription description;
		memset(&description, 0, sizeof(description));
		description.StageCount = 1;
		description.Stages[0] = one_stage(D3DTOP_MODULATE, D3DTA_TEXTURE, D3DTA_DIFFUSE,
			D3DTOP_MODULATE, D3DTA_TEXTURE, D3DTA_DIFFUSE, 0, true);
		description.PixelPipeline.AlphaTestEnabled = true;
		description.PixelPipeline.AlphaFunction = COMPARISONS[index];
		description.PixelPipeline.FogEnabled = true;

		std::string hlsl;
		CHECK(CombinerShader_Generate(description, COMBINER_SHADER_TARGET_D3D11, hlsl));
		CHECK(compiles(compile, hlsl, D3D11_PROFILE));
	}
}

// The tree shadow pass reads the texture factor for its colour and its alpha both, and reads no
// varying but one coordinate set.  Under ps_4_0 an unread input is still a declared one.
TEST(ffshadercompile_the_tree_shadow_program_compiles)
{
	D3DCompileFunction compile = load_compiler();
	if (compile == NULL) {
		printf("  d3dcompiler_47.dll not present, skipping\n");
		return;
	}

	CombinerDescription description;
	memset(&description, 0, sizeof(description));
	description.StageCount = 1;
	description.Stages[0] = one_stage(D3DTOP_SELECTARG1, D3DTA_TFACTOR, D3DTA_DIFFUSE,
		D3DTOP_MODULATE, D3DTA_TEXTURE, D3DTA_TFACTOR, 0, true);

	std::string hlsl;
	std::string eleven;
	CHECK(CombinerShader_Generate(description, COMBINER_SHADER_TARGET_D3D9, hlsl));
	CHECK(CombinerShader_Generate(description, COMBINER_SHADER_TARGET_D3D11, eleven));
	CHECK(compiles(compile, hlsl, D3D9_PROFILE));
	CHECK(compiles(compile, eleven, D3D11_PROFILE));
}

// Windows and SDL receivers share the smoke map at t6 and its constants after SkyUp.
TEST(ffshadercompile_shadow_receivers_share_the_smoke_filter)
{
	CombinerDescription description;
	memset(&description, 0, sizeof(description));
	description.StageCount = 1;
	description.Stages[0] = one_stage(D3DTOP_MODULATE, D3DTA_TEXTURE, D3DTA_DIFFUSE,
		D3DTOP_MODULATE, D3DTA_TEXTURE, D3DTA_DIFFUSE, 0, true);
	description.ShadowReceiving = true;
	description.PixelPipeline.FogEnabled = true;

	std::string eleven;
	CHECK(CombinerShader_Generate(description, COMBINER_SHADER_TARGET_D3D11, eleven));
	CHECK(eleven.find("register(t6)") != std::string::npos);
	CHECK(eleven.find("VolumeParameters") > eleven.find("SkyUp"));
	CHECK(eleven.find("light_reaching(input.Position)") != std::string::npos);

	std::string sdl;
	CHECK(CombinerShader_Generate(description, COMBINER_SHADER_TARGET_SDL3_GPU, sdl));
	CHECK(sdl.find("register(t6, space2)") != std::string::npos);
	CHECK(sdl.find("VolumeParameters") > sdl.find("SkyUp"));
	CHECK(sdl.find("light_reaching(input.Position)") != std::string::npos);

	D3DCompileFunction compile = load_compiler();
	if (compile == NULL) {
		printf("  d3dcompiler_47.dll not present, skipping the compile\n");
		return;
	}
	CHECK(compiles(compile, eleven, D3D11_PROFILE));
}

// A fire's glow on smoke goes on after the sun's shadow and the smoke's own shade, and before the
// fog: added before them, the glow of a fire behind a plume went dark with the plume's far side.  It
// takes the specular slot, so a program asking for both does not add the slot twice.
TEST(ffshadercompile_the_smoke_glow_is_added_after_the_shade)
{
	CombinerDescription description;
	memset(&description, 0, sizeof(description));
	description.StageCount = 1;
	description.Stages[0] = one_stage(D3DTOP_MODULATE, D3DTA_TEXTURE, D3DTA_DIFFUSE,
		D3DTOP_MODULATE, D3DTA_TEXTURE, D3DTA_DIFFUSE, 0, true);
	description.ShadowReceiving = true;
	description.PixelPipeline.FogEnabled = true;

	std::string plain;
	CHECK(CombinerShader_Generate(description, COMBINER_SHADER_TARGET_D3D11, plain));
	CHECK(plain.find("input.Specular.rgb * texel.rgb") == std::string::npos);
	const std::string plain_key = CombinerShader_Key(description);

	description.SmokeGlow = true;
	description.SpecularAdd = true;
	std::string eleven;
	CHECK(CombinerShader_Generate(description, COMBINER_SHADER_TARGET_D3D11, eleven));
	const size_t shade = eleven.find("light_reaching(input.Position)");
	const size_t glow = eleven.find("current.rgb + input.Specular.rgb * texel.rgb");
	const size_t fog = eleven.find("FogColour.rgb, current.rgb");
	CHECK(shade != std::string::npos);
	CHECK(glow != std::string::npos);
	CHECK(fog != std::string::npos);
	CHECK(shade < glow && glow < fog);
	CHECK(eleven.find("current.rgb + input.Specular.rgb);") == std::string::npos);
	CHECK(CombinerShader_Key(description) == plain_key + ":SP:G");

	D3DCompileFunction compile = load_compiler();
	if (compile == NULL) {
		printf("  d3dcompiler_47.dll not present, skipping the compile\n");
		return;
	}
	CHECK(compiles(compile, eleven, D3D11_PROFILE));
}

// A particle sprite fades into the scene's depth at t7 on Direct3D 11, after the alpha test so its
// edge is not clipped hard again.  An additive sprite takes no shadow and still has to declare the
// whole constant block, whose last field the fade reads; a smoke sprite takes both.  The receiving
// programs carry the blast lights after the shade.  SDL3 and D3D9 get neither.
TEST(ffshadercompile_a_soft_particle_fades_into_the_scene_depth_on_d3d11_and_sdl)
{
	CombinerDescription description;
	memset(&description, 0, sizeof(description));
	description.StageCount = 1;
	description.Stages[0] = one_stage(D3DTOP_MODULATE, D3DTA_TEXTURE, D3DTA_DIFFUSE,
		D3DTOP_MODULATE, D3DTA_TEXTURE, D3DTA_DIFFUSE, 0, true);
	description.PixelPipeline.AlphaTestEnabled = true;
	description.PixelPipeline.AlphaFunction = D3DCMP_GREATEREQUAL;
	const std::string hard_key = CombinerShader_Key(description);

	description.SoftParticle = SOFT_PARTICLE_COLOUR;
	std::string additive;
	CHECK(CombinerShader_Generate(description, COMBINER_SHADER_TARGET_D3D11, additive));
	CHECK(additive.find("register(t7)") != std::string::npos);
	CHECK(additive.find("SoftParticleDepth") > additive.find("BlastLightAmbient"));
	CHECK(additive.find("register(t5)") == std::string::npos);
	const size_t clip = additive.find("clip(");
	const size_t fade = additive.find("current *= soft_particle_fade(input.Position, input.Specular.a);");
	CHECK(clip != std::string::npos);
	CHECK(fade != std::string::npos);
	CHECK(clip < fade);
	CHECK(CombinerShader_Key(description) == hard_key + ":Z2");

	std::string sdl;
	std::string nine;
	CHECK(CombinerShader_Generate(description, COMBINER_SHADER_TARGET_SDL3_GPU, sdl));
	CHECK(CombinerShader_Generate(description, COMBINER_SHADER_TARGET_D3D9, nine));
	// SDL takes the soft particles since pull request 244: its sorted billboards bind the scene's
	// depth at t7 as Direct3D 11 does.  Direct3D 9 still has none.
	CHECK(sdl.find("SceneDepth") != std::string::npos);
	CHECK(nine.find("SceneDepth") == std::string::npos);

	description.ShadowReceiving = true;
	description.SoftParticle = SOFT_PARTICLE_ALPHA;
	std::string smoke;
	CHECK(CombinerShader_Generate(description, COMBINER_SHADER_TARGET_D3D11, smoke));
	CHECK(smoke.find("current.a *= soft_particle_fade(input.Position, input.Specular.a);") != std::string::npos);
	CHECK(smoke.find("blast_reaching(input.Position)") > smoke.find("light_reaching(input.Position)"));

	D3DCompileFunction compile = load_compiler();
	if (compile == NULL) {
		printf("  d3dcompiler_47.dll not present, skipping the compile\n");
		return;
	}
	CHECK(compiles(compile, additive, D3D11_PROFILE));
	CHECK(compiles(compile, smoke, D3D11_PROFILE));
}

// Each per-object look is its own program on Direct3D 11, written after the fog and declaring the
// whole constant block up to its own field, with or without the sun's map.  Only the cloak calls
// the function that reads the scene's copy at t8.  SDL3 gets the same looks, D3D9 none of them.
TEST(ffshadercompile_each_draw_effect_compiles_on_d3d11_and_sdl)
{
	D3DCompileFunction compile = load_compiler();
	for (unsigned shadow = 0; shadow < 2; ++shadow) {
		for (unsigned effect = DRAW_EFFECT_OUTAGE; effect < DRAW_EFFECT_COUNT; ++effect) {
			CombinerDescription description;
			memset(&description, 0, sizeof(description));
			description.StageCount = 1;
			description.Stages[0] = one_stage(D3DTOP_MODULATE, D3DTA_TEXTURE, D3DTA_DIFFUSE,
				D3DTOP_MODULATE, D3DTA_TEXTURE, D3DTA_DIFFUSE, 0, true);
			description.PixelPipeline.FogEnabled = true;
			description.ShadowReceiving = shadow != 0;
			const std::string plain_key = CombinerShader_Key(description);
			description.DrawEffect = effect;
			char suffix[8];
			snprintf(suffix, sizeof(suffix), ":X%u", effect);
			CHECK(CombinerShader_Key(description) == plain_key + suffix);

			std::string eleven;
			CHECK(CombinerShader_Generate(description, COMBINER_SHADER_TARGET_D3D11, eleven));
			CHECK(eleven.find("DrawEffectParameters;") > eleven.find("SoftParticleDepth;"));
			const size_t fog = eleven.find("FogColour.rgb, current.rgb");
			const size_t call = eleven.find("current = effect_");
			CHECK(fog != std::string::npos);
			CHECK(call != std::string::npos);
			CHECK(fog < call);
			CHECK((eleven.find("current = effect_cloak(") != std::string::npos) == (effect == DRAW_EFFECT_CLOAK));
			if (effect == DRAW_EFFECT_CLOAK) {
				// fade 0 must be the unit's own paint, or cloak-in pops to glass: both mixes start at it
				CHECK(eleven.find("glass = lerp(lerp(current.rgb, veil, going * saturate(2.0 * glass_on)), glass,") != std::string::npos);
			}
			if (effect == DRAW_EFFECT_CLOAK || effect == DRAW_EFFECT_REVEALED) {
				// both stealth looks fade by the shared front, not by a mix
				CHECK(eleven.find("stealth_unpack(fade, paint, look, height)") != std::string::npos);
				CHECK(eleven.find("stealth_edge(") != std::string::npos);
			}

			std::string sdl;
			std::string nine;
			CHECK(CombinerShader_Generate(description, COMBINER_SHADER_TARGET_SDL3_GPU, sdl));
			CHECK(CombinerShader_Generate(description, COMBINER_SHADER_TARGET_D3D9, nine));
			CHECK((sdl.find("current = effect_cloak_shadow(") != std::string::npos) == (effect == DRAW_EFFECT_CLOAK_SHADOW));
			// SDL takes every look since pull request 251, the cloak reading its own colour copy at
			// t8 as Direct3D 11 does.  Direct3D 9 still has none.
			CHECK(sdl.find("current = effect_") != std::string::npos);
			CHECK((sdl.find("current = effect_cloak(") != std::string::npos) == (effect == DRAW_EFFECT_CLOAK));
			CHECK(nine.find("effect_") == std::string::npos);

			if (compile != NULL) {
				CHECK(compiles(compile, eleven, D3D11_PROFILE));
			}
		}
	}
	if (compile == NULL) {
		printf("  d3dcompiler_47.dll not present, skipping the compile\n");
	}
}

// The stealth looks' w packed as chooseDrawEffect packs it comes back out of stealth_unpack's own
// arithmetic, done here in float as the GPU does it, at every height up to the 4000 cap.  The
// program text is checked for those same lines, so the copy below cannot drift from the shader.
TEST(ffshadercompile_the_stealth_pack_round_trips_through_stealth_unpack)
{
	CombinerDescription description;
	memset(&description, 0, sizeof(description));
	description.StageCount = 1;
	description.Stages[0] = one_stage(D3DTOP_MODULATE, D3DTA_TEXTURE, D3DTA_DIFFUSE,
		D3DTOP_MODULATE, D3DTA_TEXTURE, D3DTA_DIFFUSE, 0, true);
	description.DrawEffect = DRAW_EFFECT_REVEALED;
	std::string eleven;
	CHECK(CombinerShader_Generate(description, COMBINER_SHADER_TARGET_D3D11, eleven));
	CHECK(eleven.find("    height = floor(w / 16.0);\n    w -= height * 16.0;\n"
		"    look = floor(w / 4.0);\n    w -= look * 4.0;\n    paint = step(1.5, w);\n"
		"    fade = saturate(w - 2.0 * paint);\n") != std::string::npos);

	static const int heights[] = { 1, 7, 13, 60, 200, 1000, 4000 };
	static const float fades[] = { 0.0f, 0.001f, 0.25f, 0.5f, 0.999f, 1.0f };
	for (int h = 0; h < (int)(sizeof(heights) / sizeof(heights[0])); ++h) {
		for (int front = STEALTH_FRONT_WIND; front <= STEALTH_FRONT_RIPPLE; ++front) {
			for (int paint = 0; paint < 2; ++paint) {
				for (int f = 0; f < (int)(sizeof(fades) / sizeof(fades[0])); ++f) {
					float w = CombinerShader_Pack_Stealth(heights[h], front, paint != 0, fades[f]);
					const float height = floorf(w / 16.0f);
					w -= height * 16.0f;
					const float look = floorf(w / 4.0f);
					w -= look * 4.0f;
					const float painted = (w >= 1.5f) ? 1.0f : 0.0f;
					float fade = w - 2.0f * painted;
					fade = fade < 0.0f ? 0.0f : (fade > 1.0f ? 1.0f : fade);
					CHECK_EQ((int)height, heights[h]);
					CHECK_EQ((int)look, front);
					CHECK_EQ((int)painted, paint);
					CHECK_NEAR(fade, fades[f], 0.005f);
				}
			}
		}
	}
	// out of range parts are held in their fields rather than spilling into the next one
	CHECK_EQ((int)floorf(CombinerShader_Pack_Stealth(0, 5, true, 3.0f) / 16.0f), 1);
	CHECK_NEAR(CombinerShader_Pack_Stealth(2, -1, false, -1.0f), 32.0f, 0.0001f);
}

// A fire billboard bends its coordinates before the stages sample and burns after them, before the
// fog and the soft fade; an additive one wears away through the colour, a blended one through the
// alpha.  It declares no constant of its own, so an additive flame with no fade keeps the short
// block.  SDL3 and D3D9 get none of it.
TEST(ffshadercompile_a_fire_billboard_burns_on_d3d11_and_sdl)
{
	CombinerDescription description;
	memset(&description, 0, sizeof(description));
	description.StageCount = 1;
	description.Stages[0] = one_stage(D3DTOP_MODULATE, D3DTA_TEXTURE, D3DTA_DIFFUSE,
		D3DTOP_MODULATE, D3DTA_TEXTURE, D3DTA_DIFFUSE, 0, true);
	description.PixelPipeline.FogEnabled = true;
	const std::string plain_key = CombinerShader_Key(description);

	description.FireLook = FIRE_LOOK_COLOUR;
	CHECK(CombinerShader_Key(description) == plain_key + ":B2");
	std::string flame;
	CHECK(CombinerShader_Generate(description, COMBINER_SHADER_TARGET_D3D11, flame));
	const size_t bend = flame.find("input.TexCoord0 = (fire_cell + saturate(");
	const size_t sample = flame.find("Texture0.Sample(");
	const size_t burn = flame.find("current *= fire_keep;");
	const size_t fog = flame.find("FogColour.rgb, current.rgb");
	CHECK(bend != std::string::npos && sample != std::string::npos);
	CHECK(burn != std::string::npos && fog != std::string::npos);
	CHECK(bend < sample && sample < burn && burn < fog);
	CHECK(flame.find("VolumeParameters") == std::string::npos);

	description.FireLook = FIRE_LOOK_ALPHA;
	description.SoftParticle = SOFT_PARTICLE_ALPHA;
	description.ShadowReceiving = true;
	std::string fireball;
	CHECK(CombinerShader_Generate(description, COMBINER_SHADER_TARGET_D3D11, fireball));
	CHECK(fireball.find("current.a *= fire_keep;") < fireball.find("soft_particle_fade(input.Position"));

	std::string sdl;
	std::string nine;
	CHECK(CombinerShader_Generate(description, COMBINER_SHADER_TARGET_SDL3_GPU, sdl));
	CHECK(CombinerShader_Generate(description, COMBINER_SHADER_TARGET_D3D9, nine));
	// SDL takes the fire look since pull request 244, its billboards carrying the age and seed in v2.
	CHECK(sdl.find("fire_") != std::string::npos);
	CHECK(nine.find("fire_") == std::string::npos);

	D3DCompileFunction compile = load_compiler();
	if (compile == NULL) {
		printf("  d3dcompiler_47.dll not present, skipping the compile\n");
		return;
	}
	CHECK(compiles(compile, flame, D3D11_PROFILE));
	CHECK(compiles(compile, fireball, D3D11_PROFILE));
}
