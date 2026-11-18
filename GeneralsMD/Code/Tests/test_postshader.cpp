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
		{ POST_SHADER_COPY, 1342, UINT64_C(10507744383638973675) },
		{ POST_SHADER_TONE_MAP, 1378, UINT64_C(4804014401532655335) },
		{ POST_SHADER_TONE_MAP_WIDE, 1609, UINT64_C(1880817659996431783) },
		{ POST_SHADER_FXAA, 3951, UINT64_C(14305520391137227756) },
		{ POST_SHADER_SHARPEN, 2270, UINT64_C(17489482920193854239) },
		{ POST_SHADER_SHARPEN_MULTISAMPLED, 2271, UINT64_C(8540435591871154879) },
		{ POST_SHADER_AO, 2757, UINT64_C(17818794792923536144) },
		{ POST_SHADER_AO_SCENE, 2753, UINT64_C(4404110208642369224) },
		{ POST_SHADER_WORLD_AO, 4001, UINT64_C(17413997056080837798) },
		{ POST_SHADER_WORLD_AO_BLUR, 2596, UINT64_C(13133616914098004519) },
		{ POST_SHADER_WORLD_AO_UPSAMPLE, 2478, UINT64_C(803955814330322769) },
		{ POST_SHADER_HAZE, 4062, UINT64_C(10236842732324706847) },
		{ POST_SHADER_SKY, 2547, UINT64_C(15455093490457882870) },
		{ POST_SHADER_ATMOSPHERE, 3352, UINT64_C(6521191355011135268) },
		{ POST_SHADER_BLOOM_EXTRACT, 2172, UINT64_C(8169773055433301573) },
		{ POST_SHADER_BLOOM_BLUR, 1840, UINT64_C(15383801789143368049) },
		{ POST_SHADER_BLOOM_COMPOSITE, 1982, UINT64_C(17820278394050563199) },
		{ POST_SHADER_BLOOM_COMPOSITE_WIDE, 2213, UINT64_C(5207253355562644035) },
		{ POST_SHADER_WARP, 2557, UINT64_C(16796992523208899611) },
		{ POST_SHADER_HEAT_SPLAT, 3159, UINT64_C(4496426983940522882) },
		{ POST_SHADER_HEAT_HAZE, 1670, UINT64_C(14227170186255655971) },
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
