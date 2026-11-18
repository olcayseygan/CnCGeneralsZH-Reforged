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

#include "ffshader.h"
#include "ffvertex.h"
#include "sdl3target.h"

#include <stdio.h>

// The texture factor and the sampler registers the generated shader expects the caller to have
// filled.  They are the constant register D3DRS_TEXTUREFACTOR is copied into and the stage samplers
// the device already has bound, so nothing about how a draw is set up has to change.
static const char * const TEXTURE_FACTOR_REGISTER = "c0";

// D3DTA_SELECTMASK is the argument itself; the two flags above it are modifiers the device applies
// to whatever the argument turned out to be.
static const FixedFunctionValue ARGUMENT_SELECT_MASK = FF_TA_SELECTMASK;

// The low half of D3DTSS_TEXCOORDINDEX is the coordinate set; the high half is a generation mode.
static const FixedFunctionValue COORDINATE_SET_MASK = 0xffff;

// The shader declares two coordinate sets, which is every one the four measured maps ask for.  A
// third is a refusal rather than a shader that samples a register it never declared.
static const unsigned DECLARED_COORDINATE_SETS = 2;

// The coordinate set a stage's TEXCOORDINDEX names: its low half.
static unsigned coordinate_register(FixedFunctionValue texture_coordinate_index)
{
	return static_cast<unsigned>(texture_coordinate_index & COORDINATE_SET_MASK);
}

// Which interpolated coordinate a stage samples with.  The two profiles differ, because their vertex
// halves do.
//
// D3D11, and SDL3 which is its text: the vertex half is ffvertex, which writes each STAGE's
// coordinates to TexCoord[stage] after the stage's set selection, generation and transform
// (ffvertex.cpp, "output.TexCoord%u = generated%u.xy").  So a stage reads its own register, by
// construction.  Reading the set's instead gave a stage that generates its coordinates (the shroud,
// camera-space position, set 0) the first stage's texture coordinates, and a blend-tile stage 0 on set
// 1 a disabled stage's zeroes (port defect 27).
//
// D3D9: the vertex half is D3D9's own fixed function, and what it puts in each register was measured
// when a D3D9 device was there to compare with (bfb60e17).  Routing a generating stage to its own
// stage register took Flash Effect at frame 400 from 0.25% against the fixed-function frame to 0.81%,
// so the set is what the device means, and the D3D9 profile reads the set's register.  A reading of
// the documentation pointed the other way by inference (for port defect 27); the measurement stands until a
// Windows D3D9 frame says otherwise.
static unsigned stage_register(CombinerShaderTarget target, unsigned stage, FixedFunctionValue texture_coordinate_index)
{
	return target == COMBINER_SHADER_TARGET_D3D11 ? stage : coordinate_register(texture_coordinate_index);
}

// "current" is initialised to the diffuse colour, which is what the device gives D3DTA_CURRENT at
// stage 0, so the two need no distinguishing here.
static bool argument_expression(FixedFunctionValue argument, std::string & expression)
{
	switch (argument & ARGUMENT_SELECT_MASK) {
	case FF_TA_DIFFUSE:  expression = "input.Diffuse";  break;
	case FF_TA_CURRENT:  expression = "current"; break;
	case FF_TA_TEXTURE:  expression = "texel"; break;
	case FF_TA_TFACTOR:  expression = "TextureFactor"; break;
	case FF_TA_SPECULAR: expression = "input.Specular"; break;
	default:
		return false;
	}

	// Replicate first, complement second: that is the order the reference rasteriser applies them,
	// so 1 - a.a and not (1 - a).a for an argument carrying both.
	if ((argument & FF_TA_ALPHAREPLICATE) != 0) {
		expression = "(" + expression + ").aaaa";
	}
	if ((argument & FF_TA_COMPLEMENT) != 0) {
		expression = "(1.0 - " + expression + ")";
	}
	return true;
}

static bool operation_expression(FixedFunctionValue operation, const std::string & argument0,
	const std::string & argument1, const std::string & argument2, std::string & expression)
{
	switch (operation) {
	case FF_TOP_SELECTARG1:
		expression = argument1;
		return true;
	case FF_TOP_SELECTARG2:
		expression = argument2;
		return true;
	case FF_TOP_MODULATE:
		expression = "(" + argument1 + " * " + argument2 + ")";
		return true;
	case FF_TOP_MODULATE2X:
		expression = "(" + argument1 + " * " + argument2 + " * 2.0)";
		return true;
	case FF_TOP_MODULATE4X:
		expression = "(" + argument1 + " * " + argument2 + " * 4.0)";
		return true;
	case FF_TOP_ADD:
		expression = "(" + argument1 + " + " + argument2 + ")";
		return true;
	case FF_TOP_ADDSIGNED:
		expression = "(" + argument1 + " + " + argument2 + " - 0.5)";
		return true;
	case FF_TOP_SUBTRACT:
		expression = "(" + argument1 + " - " + argument2 + ")";
		return true;
	case FF_TOP_MULTIPLYADD:
		expression = "(" + argument0 + " + " + argument1 + " * " + argument2 + ")";
		return true;
	case FF_TOP_LERP:
		expression = "lerp(" + argument2 + ", " + argument1 + ", " + argument0 + ")";
		return true;
	case FF_TOP_ADDSIGNED2X:
		expression = "((" + argument1 + " + " + argument2 + " - 0.5) * 2.0)";
		return true;
	case FF_TOP_ADDSMOOTH:
		expression = "(" + argument1 + " + " + argument2 + " - " + argument1 + " * " + argument2
			+ ")";
		return true;

	// The four alpha blends differ only in where the blending alpha comes from, and the terrain
	// uses three of them: the ground layers are blended by the vertex alpha and the noise layers by
	// the alpha carried in the stage before them.
	case FF_TOP_BLENDDIFFUSEALPHA:
		expression = "lerp(" + argument2 + ", " + argument1 + ", input.Diffuse.a)";
		return true;
	case FF_TOP_BLENDTEXTUREALPHA:
		expression = "lerp(" + argument2 + ", " + argument1 + ", texel.a)";
		return true;
	case FF_TOP_BLENDFACTORALPHA:
		expression = "lerp(" + argument2 + ", " + argument1 + ", TextureFactor.a)";
		return true;
	case FF_TOP_BLENDCURRENTALPHA:
		expression = "lerp(" + argument2 + ", " + argument1 + ", current.a)";
		return true;

	// Premultiplied: the first argument already carries its own alpha, so it is added rather than
	// interpolated and only the second is faded out.
	case FF_TOP_BLENDTEXTUREALPHAPM:
		expression = "(" + argument1 + " + " + argument2 + " * (1.0 - texel.a))";
		return true;

	// The four modulate-and-add operations, which write a colour built from one argument's alpha
	// and the other's colour.  D3D9 leaves the alpha channel of the result undefined for these and
	// the engine does not read it, so the alpha here is whatever the arithmetic produces.
	case FF_TOP_MODULATEALPHA_ADDCOLOR:
		expression = "(" + argument1 + " + " + argument1 + ".a * " + argument2 + ")";
		return true;
	case FF_TOP_MODULATECOLOR_ADDALPHA:
		expression = "(" + argument1 + " * " + argument2 + " + " + argument1 + ".a)";
		return true;
	case FF_TOP_MODULATEINVALPHA_ADDCOLOR:
		expression = "((1.0 - " + argument1 + ".a) * " + argument2 + " + " + argument1 + ")";
		return true;
	case FF_TOP_MODULATEINVCOLOR_ADDALPHA:
		expression = "((1.0 - " + argument1 + ") * " + argument2 + " + " + argument1 + ".a)";
		return true;

	case FF_TOP_DOTPRODUCT3:
		// The device works on signed values here and saturates the result into all four channels.
		expression = "saturate(dot((" + argument1 + ").rgb * 2.0 - 1.0, ("
			+ argument2 + ").rgb * 2.0 - 1.0)).xxxx";
		return true;
	default:
		return false;
	}
}

// D3D11 has no alpha test, so the comparison becomes a clip.  clip() throws the pixel away when its
// argument is negative, which turns each comparison into one subtraction.
//
// Both sides of that subtraction are whole levels.  Direct3D 9's alpha test compared the eight bit
// alpha about to be written against an eight bit reference, so the comparison this replaces is an
// integer one, and doing it in floats moves every alpha tested edge in the frame by up to half a
// level.  On a quad whose alpha runs from nothing to full across 64 pixels, tested at reference 64,
// Direct3D 9 keeps from column 16 and the float comparison kept from 17 - one pixel along every
// foliage edge there is, and the views that carry the most of them are the ones that were still
// outside the margin.  test_pixelcentre draws exactly that quad on both runtimes.  An alpha that
// lands exactly on half a level is rounded whichever way the Direct3D 9 driver likes, so nothing
// here can match every card there and the test stays off that case.
//
// Rounding to a level first also makes the strict comparisons and the two equalities exact.  A
// strict greater is one whole level further along; an equality is a level either side rather than
// the guessed tolerance this used to carry.
static const char * const ALPHA_LEVEL =
	"    float alpha_level = floor(current.a * 255.0 + 0.5);\n";

// The sharpened alpha in place of the clip, for the two tests that keep the high alphas.
static bool alpha_to_coverage(const PixelPipelineDescription & pipeline)
{
	return pipeline.AlphaToCoverage && pipeline.AlphaTestEnabled
		&& (pipeline.AlphaFunction == FF_CMP_GREATER || pipeline.AlphaFunction == FF_CMP_GREATEREQUAL);
}

static bool alpha_test_expression(FixedFunctionValue function, std::string & expression)
{
	switch (function) {
	case FF_CMP_NEVER:
		expression = "clip(-1.0);\n";
		return true;
	case FF_CMP_LESS:
		expression = "clip(AlphaReference.x - alpha_level - 1.0);\n";
		return true;
	case FF_CMP_LESSEQUAL:
		expression = "clip(AlphaReference.x - alpha_level);\n";
		return true;
	case FF_CMP_GREATER:
		expression = "clip(alpha_level - AlphaReference.x - 1.0);\n";
		return true;
	case FF_CMP_GREATEREQUAL:
		expression = "clip(alpha_level - AlphaReference.x);\n";
		return true;
	case FF_CMP_EQUAL:
		expression = "clip(0.5 - abs(alpha_level - AlphaReference.x));\n";
		return true;
	case FF_CMP_NOTEQUAL:
		expression = "clip(abs(alpha_level - AlphaReference.x) - 0.5);\n";
		return true;
	case FF_CMP_ALWAYS:
		// Nothing to write: every pixel survives, which is what no alpha test at all means.
		expression.clear();
		return true;
	default:
		return false;
	}
}

static bool generate_combiners(const CombinerDescription & description,
	CombinerShaderTarget target, std::string & hlsl, bool volumetric);

bool CombinerShader_Generate(const CombinerDescription & description, CombinerShaderTarget target,
	std::string & hlsl)
{
	// SDL shares D3D11 arithmetic and constant layouts, then rewrites resource bindings.
	// Its cloak caster needs only ordered coverage, without cosmetic scene resources.
	if (target == COMBINER_SHADER_TARGET_SDL3_GPU) {
		CombinerDescription supported = description;
        // Sorted native billboards now supply the original width/age payload
        // and an independently validated pretransparent depth snapshot.
		if (supported.DrawEffect != DRAW_EFFECT_CLOAK_SHADOW) supported.DrawEffect = DRAW_EFFECT_NONE;
		// SDL has no heights at t4; its ground keeps the vertex light.
		supported.PixelPipeline.TerrainRelight = false;
		return generate_combiners(supported, target, hlsl, true)
			&& SDL3_Shader_Retarget(hlsl, false);
	}
	return generate_combiners(description, target, hlsl, target == COMBINER_SHADER_TARGET_D3D11);
}

static bool generate_combiners(const CombinerDescription & description,
	CombinerShaderTarget target, std::string & hlsl, bool volumetric)
{
	const bool sdl = target == COMBINER_SHADER_TARGET_SDL3_GPU;
	if (sdl) target = COMBINER_SHADER_TARGET_D3D11;
	// Native sorted billboards bind the phase-owned scene depth at t7.
	const bool soft = volumetric && description.SoftParticle != SOFT_PARTICLE_NONE;
	// SDL accepts only the cloak caster variant; Windows also supplies the cosmetic scene effects.
	const unsigned effect = (volumetric && description.DrawEffect < DRAW_EFFECT_COUNT)
		? description.DrawEffect : DRAW_EFFECT_NONE;
	// The fire look reads no constant, but the vertex half that feeds it is the D3D11 one's.
	const unsigned fire = volumetric ? description.FireLook : FIRE_LOOK_NONE;
	// The ground's sun per pixel, which needs WorldFromClip and the heights whether or not a shadow lands.
	const bool relight = volumetric && description.PixelPipeline.TerrainRelight;
	volumetric = volumetric && description.ShadowReceiving;
	if (description.StageCount == 0 || description.StageCount > MAXIMUM_COMBINER_STAGES) {
		return false;
	}

	std::string body;
	// Black until stage 0 samples, so a team glow draw with no stage glows not at all.
	if (description.TeamGlow) {
		body += "    float4 team_texel = float4(0.0, 0.0, 0.0, 0.0);\n";
	}
	for (unsigned stage = 0; stage < description.StageCount; ++stage) {
		const CombinerStage & source = description.Stages[stage];

		char line[256];
		if (source.TextureBound) {
			const unsigned register_index = stage_register(target, stage, source.TextureCoordinateIndex);
			if (register_index >= DECLARED_COORDINATE_SETS) {
				return false;
			}
			if (target == COMBINER_SHADER_TARGET_D3D11) {
				snprintf(line, sizeof(line),
					"    texel = Texture%u.Sample(Sampler%u, input.TexCoord%u);\n",
					stage, stage, register_index);
			}
			else {
				snprintf(line, sizeof(line), "    texel = tex2D(Sampler%u, input.TexCoord%u);\n",
					stage, register_index);
			}
		}
		else {
			// What a stage with no texture bound gives D3DTA_TEXTURE is not something this can
			// assert: the documentation does not say and reports of what hardware does disagree.
			// Opaque white is the choice that makes MODULATE a no-op, which is what the call sites
			// that leave a stage untextured appear to want, and one measured combination reads
			// D3DTA_TEXTURE with nothing bound - SELECTARG1 on an untextured stage - so the two
			// answers are distinguishable on screen.  tree-check.ps1 settles it, not this comment.
			snprintf(line, sizeof(line), "    texel = float4(1.0, 1.0, 1.0, 1.0);\n");
		}
		body += line;
		// The paint, kept for the team glow's brightness: a later stage's texel is a detail or an
		// environment map, not the house colour's pattern.
		if (stage == 0 && description.TeamGlow) {
			body += "    team_texel = texel;\n";
		}

		std::string colour_argument0, colour_argument1, colour_argument2;
		std::string alpha_argument0, alpha_argument1, alpha_argument2;
		if (!argument_expression(source.ColourArgument0, colour_argument0)
			|| !argument_expression(source.ColourArgument1, colour_argument1)
			|| !argument_expression(source.ColourArgument2, colour_argument2)
			|| !argument_expression(source.AlphaArgument0, alpha_argument0)
			|| !argument_expression(source.AlphaArgument1, alpha_argument1)
			|| !argument_expression(source.AlphaArgument2, alpha_argument2)) {
			return false;
		}

		std::string colour_expression;
		if (!operation_expression(source.ColourOperation, colour_argument0, colour_argument1,
				colour_argument2, colour_expression)) {
			return false;
		}

		// A stage may disable its alpha operation while its colour operation runs on: the alpha
		// channel then keeps whatever the stage before it left there.  D3D9 says so in one line and
		// the terrain relies on it, blending its ground layers by an alpha two stages older than
		// the colour being written.
		std::string alpha_expression = "current";
		if (source.ColourOperation == FF_TOP_DOTPRODUCT3) {
			// DOTPRODUCT3 as the colour operation writes its sum to alpha too, whatever the alpha
			// operation says (D3DTEXTUREOP: "replicate the sum to all color channels, including
			// alpha").
			alpha_expression = colour_expression;
		}
		else if (source.AlphaOperation != FF_TOP_DISABLE
			&& !operation_expression(source.AlphaOperation, alpha_argument0, alpha_argument1,
				alpha_argument2, alpha_expression)) {
			return false;
		}

		// Every stage of the fixed-function combiner writes a clamped result, and the next stage
		// reads that clamped value out of D3DTA_CURRENT.  A shader that carries the raw arithmetic
		// forward instead comes out brighter wherever MODULATE2X, MODULATE4X, ADD or MULTIPLYADD
		// left a term above one, and darker where SUBTRACT or ADDSIGNED went below zero.
		body += "    current.rgb = saturate(" + colour_expression + ").rgb;\n";
		body += "    current.a   = saturate(" + alpha_expression + ").a;\n";
	}

	// The full input signature is declared whether the program reads it or not.  Trimming it to
	// what the body names is the obvious economy and it was measured: on Flash Effect at frame 400
	// it takes the difference against the fixed-function frame from 0.25% to 1.20%.  Why a program
	// that reads no diffuse colour draws differently for having declared one is not understood.
	hlsl = "// Generated from a fixed-function texture stage description.  See ffshader.h.\n";
	if (target == COMBINER_SHADER_TARGET_D3D11) {
		// A texture and the sampler that reads it are separate objects here, and the texture
		// factor is a constant buffer field rather than a register.
		for (unsigned stage = 0; stage < MAXIMUM_COMBINER_STAGES; ++stage) {
			char line[128];
			snprintf(line, sizeof(line),
				"Texture2D Texture%u : register(t%u);\n"
				"SamplerState Sampler%u : register(s%u);\n",
				stage, stage, stage, stage);
			hlsl += line;
		}
		hlsl +=
			"cbuffer CombinerConstants : register(b0)\n"
			"{\n"
			"    float4 TextureFactor;\n"
			"    float4 FogColour;\n"
			"    float4 AlphaReference;\n";
		// A shader declares a prefix of the block the backend uploads, so a field is found by what
		// comes before it and not by its name.  The shadow fields sit behind the old normal mapped
		// ones and the terrain's sun, so a program that reads them has to declare those too, unused:
		// without them a shadow matrix lands where the first light's direction is.  A soft particle's
		// field closes the block, so a program that fades declares all of it.
		if (description.ShadowReceiving || soft || effect != DRAW_EFFECT_NONE || relight) {
			char line[256];
			snprintf(line, sizeof(line),
				"    float4 NormalLightDirection[%u];\n"
				"    float4 NormalLightDiffuse[%u];\n"
				"    float4 NormalMapParameters;\n",
				NORMAL_MAPPED_LIGHTS, NORMAL_MAPPED_LIGHTS);
			hlsl += line;
			hlsl +=
				"    float4 TerrainSunDirection;\n"
				// row_major for the reason the vertex half gives: every matrix the engine has is a
				// D3D9 matrix and D3D9 stores one by rows.
				"    row_major float4x4 ShadowFromClip;\n"
				"    float4 ShadowParameters;\n"
				"    float4 ShadowViewport;\n"
				"    float4 ShadowSoftness;\n"
				// The sky a metal surface mirrors, and which way up it is in camera space.  Last in
				// the block, so a program that wants it declares everything in front of it.
				"    float4 Sky;\n"
				"    float4 SkyUp;\n";
		}
		if (volumetric || soft || effect != DRAW_EFFECT_NONE || relight) {
			hlsl += VOLUMETRIC_CONSTANTS;
		}
		if (effect != DRAW_EFFECT_NONE) {
			hlsl += DRAW_EFFECT_CONSTANTS;
		}
		hlsl += "};\n";
		if (description.ShadowReceiving) {
			hlsl += SHADOW_SAMPLING;
		}
		if (volumetric) {
			hlsl += VOLUMETRIC_SAMPLING;
		}
		if (soft) {
			hlsl += SOFT_PARTICLE_SAMPLING;
		}
		if (effect != DRAW_EFFECT_NONE) {
			// SDL admits only the cloak caster. The complete cosmetic helper declares
			// SceneColour at t8 even when unused, exceeding its eight-slot draw layout.
			hlsl += sdl ? CLOAK_SHADOW_SAMPLING : DRAW_EFFECT_SAMPLING;
		}
		if (fire != FIRE_LOOK_NONE) {
			hlsl += FIRE_LOOK_SAMPLING;
		}
		if (relight) {
			hlsl += TERRAIN_RELIGHT_SAMPLING;
		}
	}
	else {
		for (unsigned stage = 0; stage < MAXIMUM_COMBINER_STAGES; ++stage) {
			char line[64];
			snprintf(line, sizeof(line), "sampler2D Sampler%u : register(s%u);\n", stage, stage);
			hlsl += line;
		}
		hlsl += "float4 TextureFactor : register(";
		hlsl += TEXTURE_FACTOR_REGISTER;
		hlsl += ");\n";
	}
	hlsl +=
		"\n"
		"struct Input\n"
		"{\n";

	// Shader model 4 links the two stages by register slot, not by name, and it assigns those
	// slots in declaration order.  A vertex shader whose output opens with SV_Position and closes
	// with the fog factor puts the diffuse colour in the second slot; a pixel shader that declares
	// neither puts it in the first, and D3D11 refuses the draw with a linkage error naming COLOR
	// and TEXCOORD.  So the D3D11 input structure is ffvertex's output structure, member for
	// member, whether this program reads all of it or not.  ps_2_0 has no such rule.
	if (target == COMBINER_SHADER_TARGET_D3D11) {
		hlsl += "    float4 Position  : SV_Position;\n";
	}
	hlsl +=
		"    float4 Diffuse   : COLOR0;\n"
		"    float4 Specular  : COLOR1;\n";
	for (unsigned stage = 0; stage < MAXIMUM_COMBINER_STAGES; ++stage) {
		char line[64];
		snprintf(line, sizeof(line), "    float2 TexCoord%u : TEXCOORD%u;\n", stage, stage);
		hlsl += line;
	}
	if (target == COMBINER_SHADER_TARGET_D3D11) {
		hlsl += "    float Fog        : FOG;\n";
	}
	hlsl +=
		"};\n"
		"\n";
	hlsl += (target == COMBINER_SHADER_TARGET_D3D11)
		? "float4 main(Input input) : SV_Target\n"
		: "float4 main(Input input) : COLOR\n";
	hlsl +=
		"{\n"
		"    float4 texel;\n";
	if (relight) {
		hlsl += TERRAIN_RELIGHT_APPLY;
	}
	hlsl += "    float4 current = input.Diffuse;\n";
	if (fire != FIRE_LOOK_NONE) {
		hlsl += FIRE_LOOK_BEND;
	}
	hlsl += body;
	// Before the shade and the fog, as the art's own colour would be.
	if (fire != FIRE_LOOK_NONE) {
		hlsl += (fire == FIRE_LOOK_COLOUR)
			? "    float fire_shape = " FIRE_LOOK_BURN_SHAPE_COLOUR ";\n" FIRE_LOOK_BURN
				"    current *= fire_keep;\n"
			: "    float fire_shape = " FIRE_LOOK_BURN_SHAPE_ALPHA ";\n" FIRE_LOOK_BURN
				"    current.a *= fire_keep;\n";
	}
	// The house colour's hue at full brightness, taken from the lit colour before the shade touches
	// it, so the light's level drops out.  Only the texture's bright texels get it, and only as much
	// as the hue is saturated: a grey or white team, or a dull panel, would glow white.
	if (description.TeamGlow) {
		hlsl += "    float team_peak = max(max(current.r, current.g), max(current.b, 1.0 / 64.0));\n"
			"    float3 team_hue = current.rgb / team_peak;\n"
			"    float team_saturation = 1.0 - min(min(team_hue.r, team_hue.g), team_hue.b);\n"
			"    float team_bright = smoothstep(" TEAM_GLOW_LOW ", " TEAM_GLOW_HIGH ","
			" dot(team_texel.rgb, float3(0.299, 0.587, 0.114)));\n"
			"    float3 team_glow = team_hue * (team_bright * team_saturation * " TEAM_GLOW_GAIN ");\n";
	}
	if (description.SpecularAdd && !description.SmokeGlow) {
		// Before the fog and the alpha test, as D3D9 orders them; the alpha is the stages'.
		hlsl += "    current.rgb = saturate(current.rgb + input.Specular.rgb);\n";
	}

	// Before the fog: a shadow is a thing in the world and the fog is between the world and the eye.
	if (description.ShadowReceiving) {
		hlsl += volumetric ? VOLUMETRIC_SHADOW_APPLY : SHADOW_APPLY;
	}

	// After the shade, so the light a fire throws on smoke is not dimmed by the smoke it lights.
	if (description.SmokeGlow) {
		hlsl += "    current.rgb = saturate(current.rgb + input.Specular.rgb * texel.rgb);\n";
	}

	// After the shade, past 1.0 for the bloom on the half float target, then the whole colour scaled
	// down until its brightest channel is at TEAM_GLOW_CEILING.  One factor on all three channels
	// keeps their ratio, where a per-channel clamp (or the tone curve on a channel far past it)
	// flattens the top one and moves the hue toward white.  Before the fog, so a fogged base still
	// fades with distance.
	if (description.TeamGlow) {
		hlsl += "    current.rgb += team_glow;\n"
			"    current.rgb *= min(1.0, " TEAM_GLOW_CEILING
			" / max(max(current.r, current.g), max(current.b, 1.0 / 64.0)));\n";
	}

	if (target == COMBINER_SHADER_TARGET_D3D11
		&& !CombinerShader_Append_Pixel_Pipeline(description.PixelPipeline, hlsl)) {
		return false;
	}

	// After the fog and the alpha test: the cloak bends a picture that is fogged already, and a
	// texel the test threw away has no surface to dress.
	static const char * const effect_calls[DRAW_EFFECT_COUNT] = {
		"",
		"    current = effect_outage(current, input.Position);\n",
		"    current = effect_emp(current, input.Position);\n",
		"    current = effect_cloak(current, input.Position);\n",
		"    current = effect_revealed(current, input.Position);\n",
		"    current = effect_cloak_shadow(current, input.Position);\n"
	};
	hlsl += effect_calls[effect];

	// After the alpha test: faded before it, a sprite's edge against the ground would be clipped to
	// a hard line again, only a little further out.
	if (soft) {
		hlsl += (description.SoftParticle == SOFT_PARTICLE_COLOUR)
			? "    current *= soft_particle_fade(input.Position, input.Specular.a);\n"
			: "    current.a *= soft_particle_fade(input.Position, input.Specular.a);\n";
	}

	hlsl +=
		"    return current;\n"
		"}\n";
	return true;
}

// The fog is applied after the combiners and before the alpha test, which is the order the D3D9
// pipeline applies them in.  The factor is the weight of the unfogged colour, so one is no fog, and
// it never touches the alpha - a fogged pixel is the same shape as an unfogged one.
bool CombinerShader_Append_Pixel_Pipeline(const PixelPipelineDescription & pipeline,
	std::string & hlsl)
{
	if (pipeline.FogEnabled) {
		hlsl += "    current.rgb = lerp(FogColour.rgb, current.rgb, saturate(input.Fog));\n";
	}

	if (pipeline.AlphaTestEnabled) {
		std::string clip;
		if (!alpha_test_expression(pipeline.AlphaFunction, clip)) {
			return false;
		}
		if (alpha_to_coverage(pipeline)) {
			// The edge half a level past the reference on the side the test keeps, so a strict and
			// an inclusive test part where their clips do.  Nothing is clipped: no coverage is no
			// sample, depth included.
			hlsl += (pipeline.AlphaFunction == FF_CMP_GREATER)
				? "    float coverage_edge = (AlphaReference.x + 0.5) / 255.0;\n"
				: "    float coverage_edge = (AlphaReference.x - 0.5) / 255.0;\n";
			hlsl += "    current.a = saturate((current.a - coverage_edge) / max(fwidth(current.a), 0.0001) + 0.5);\n";
		}
		else if (!clip.empty()) {
			hlsl += ALPHA_LEVEL;
			hlsl += "    " + clip;
		}
	}
	return true;
}

std::string CombinerShader_Pipeline_Key(const PixelPipelineDescription & pipeline)
{
	char field[32];
	snprintf(field, sizeof(field), ":A%u,%lu,F%u%s%s",
		pipeline.AlphaTestEnabled ? 1u : 0u,
		pipeline.AlphaTestEnabled ? pipeline.AlphaFunction : 0ul,
		pipeline.FogEnabled ? 1u : 0u,
		alpha_to_coverage(pipeline) ? ",C" : "",
		pipeline.TerrainRelight ? ",T" : "");
	return field;
}

std::string CombinerShader_Key(const CombinerDescription & description)
{
	std::string key;
	char field[32];
	snprintf(field, sizeof(field), "%u", description.StageCount);
	key += field;

	for (unsigned stage = 0; stage < description.StageCount; ++stage) {
		const CombinerStage & source = description.Stages[stage];
		snprintf(field, sizeof(field), ":%lu,%lu,%lu,%lu",
			(unsigned long)source.ColourOperation, (unsigned long)source.ColourArgument0,
			(unsigned long)source.ColourArgument1, (unsigned long)source.ColourArgument2);
		key += field;
		snprintf(field, sizeof(field), ",%lu,%lu,%lu,%lu",
			(unsigned long)source.AlphaOperation, (unsigned long)source.AlphaArgument0,
			(unsigned long)source.AlphaArgument1, (unsigned long)source.AlphaArgument2);
		key += field;
		snprintf(field, sizeof(field), ",%u,%u",
			coordinate_register(source.TextureCoordinateIndex), source.TextureBound ? 1u : 0u);
		key += field;
	}

	// The alpha test and the fog change the program on the D3D11 profile.  They are in the key on
	// both, which costs a D3D9 cache entry that generates the same text as another and buys one
	// cache that is right for either profile.
	key += CombinerShader_Pipeline_Key(description.PixelPipeline);
	if (description.ShadowReceiving) {
		key += ":S";
	}
	if (description.SpecularAdd) {
		key += ":SP";
	}
	if (description.SmokeGlow) {
		key += ":G";
	}
	if (description.TeamGlow) {
		key += ":H";
	}
	if (description.SoftParticle != SOFT_PARTICLE_NONE) {
		snprintf(field, sizeof(field), ":Z%u", description.SoftParticle);
		key += field;
	}
	if (description.DrawEffect != DRAW_EFFECT_NONE) {
		snprintf(field, sizeof(field), ":X%u", description.DrawEffect);
		key += field;
	}
	if (description.FireLook != FIRE_LOOK_NONE) {
		snprintf(field, sizeof(field), ":B%u", description.FireLook);
		key += field;
	}
	return key;
}
