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

// Byte fingerprints came from HEAD f7e94b27 dx11post.cpp HLSL before extraction, independently
// compiled as literal C++ strings. They were not generated from postshader.cpp.
// They pin ALL 21 complete pixel programs and both vertex programs, including tone/grade math,
// so extracting a shader cannot silently retune it. A deliberate shader edit must review/update
// these baselines. This is source equivalence, not a substitute for a graphics compiler or GPU test.
// HAZE was subsequently reviewed for optional water-surface blending (2026-11-15).
// The four bloom baselines come independently from upstream 419d88bee dx11post.cpp (2026-11-17).
// WORLD_AO takes main's edge normalisation (ffb8a5fa1), read from main 3989c3e60 dx11post.cpp.
// WORLD_AO, HAZE and ATMOSPHERE read the shroud's light as its brightest channel, not green, so a
// tinted shroud (the Apocalypse map's red) is not taken for a dark one (2026-11-22, reviewed).
// ATMOSPHERE takes the terrain skirt's closing over the land past the map (MapSize.zw), reviewed 2026-11-22;
// its baseline is the merge of both (2026-11-23).
// The prologue's grade leans only the shade cool, min(grey - GRADE_PIVOT, 0), so a light pixel keeps
// its hue; every program carries the prologue, so all 21 moved together (2026-11-28, reviewed).
#include "test_harness.h"
#include "postshader.h"

#include <cstdint>

static std::uint64_t fingerprint(const char * source)
{
	std::uint64_t hash = UINT64_C(14695981039346656037);
	for (; *source; ++source) {
		hash ^= static_cast<unsigned char>(*source);
		hash *= UINT64_C(1099511628211);
	}
	return hash;
}

TEST(postshader_sources_match_reviewed_program_baselines)
{
	struct Baseline { PostShaderProgram Program; size_t Length; std::uint64_t Hash; };
	const Baseline baselines[] = {
		{ POST_SHADER_COPY, 1350, UINT64_C(10734151328771839445) },
		{ POST_SHADER_TONE_MAP, 1386, UINT64_C(5702653905324543581) },
		{ POST_SHADER_TONE_MAP_WIDE, 1617, UINT64_C(13118679341905792021) },
		{ POST_SHADER_FXAA, 3959, UINT64_C(4002882395850129582) },
		{ POST_SHADER_SHARPEN, 2278, UINT64_C(18080822530346257557) },
		{ POST_SHADER_SHARPEN_MULTISAMPLED, 2279, UINT64_C(6947028675578759409) },
		{ POST_SHADER_AO, 2765, UINT64_C(13303644823682223018) },
		{ POST_SHADER_AO_SCENE, 2761, UINT64_C(8621108872865826918) },
		{ POST_SHADER_WORLD_AO, 4143, UINT64_C(1716841766262472392) },
		{ POST_SHADER_WORLD_AO_BLUR, 2604, UINT64_C(4953505251404564433) },
		{ POST_SHADER_WORLD_AO_UPSAMPLE, 2486, UINT64_C(7560091268909909059) },
		{ POST_SHADER_HAZE, 4152, UINT64_C(10071296929812489873) },
		{ POST_SHADER_SKY, 2555, UINT64_C(6531709859262192000) },
		{ POST_SHADER_ATMOSPHERE, 3565, UINT64_C(7056402554130005299) },
		{ POST_SHADER_BLOOM_EXTRACT, 2180, UINT64_C(14055860132813879431) },
		{ POST_SHADER_BLOOM_BLUR, 1848, UINT64_C(2737993325844061467) },
		{ POST_SHADER_BLOOM_COMPOSITE, 1990, UINT64_C(7139389112068253337) },
		{ POST_SHADER_BLOOM_COMPOSITE_WIDE, 2221, UINT64_C(5019133795433370493) },
		{ POST_SHADER_WARP, 2565, UINT64_C(4992731714228952485) },
		{ POST_SHADER_HEAT_SPLAT, 3167, UINT64_C(13194278196789512744) },
		{ POST_SHADER_HEAT_HAZE, 1678, UINT64_C(5600505564467842153) },
	};
	CHECK_EQ(static_cast<unsigned>(POST_SHADER_COUNT), sizeof(baselines) / sizeof(baselines[0]));
	for (const Baseline & baseline : baselines) {
		const std::string source = PostShader_Pixel_Source(baseline.Program);
		CHECK_EQ(baseline.Length, source.size());
		CHECK_EQ(baseline.Hash, fingerprint(source.c_str()));
		CHECK_EQ(source, std::string(PostShader_Pixel_Prologue()) + PostShader_Pixel_Body(baseline.Program));
	}
	CHECK_EQ(351u, strlen(PostShader_Vertex_Source()));
	CHECK_EQ(UINT64_C(2534264844340035922), fingerprint(PostShader_Vertex_Source()));
	CHECK_EQ(999u, strlen(PostShader_Heat_Splat_Vertex_Source()));
	CHECK_EQ(UINT64_C(7325456401838661949), fingerprint(PostShader_Heat_Splat_Vertex_Source()));
}

TEST(postshader_invalid_programs_fail_without_emitting_a_partial_program)
{
	CHECK(PostShader_Pixel_Body(POST_SHADER_COUNT) == NULL);
	CHECK(PostShader_Pixel_Source(POST_SHADER_COUNT).empty());
	CHECK(PostShader_Pixel_Body(static_cast<PostShaderProgram>(-1)) == NULL);
	CHECK(PostShader_Pixel_Source(static_cast<PostShaderProgram>(-1)).empty());
}

TEST(postshader_simple_effect_selection_matches_dx11_fallbacks)
{
	CHECK_EQ(POST_SHADER_COPY, PostShader_Simple_Effect(DX11_POST_COPY));
	CHECK_EQ(POST_SHADER_FXAA, PostShader_Simple_Effect(DX11_POST_FXAA));
	CHECK_EQ(POST_SHADER_SHARPEN, PostShader_Simple_Effect(DX11_POST_SHARPEN));
	CHECK_EQ(POST_SHADER_AO, PostShader_Simple_Effect(DX11_POST_AO));
	CHECK_EQ(POST_SHADER_COPY, PostShader_Simple_Effect(DX11_POST_BLOOM));
}
