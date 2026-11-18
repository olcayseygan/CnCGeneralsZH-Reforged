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

// These parser tests run on every platform, without a graphics device or D3D headers.
#include "test_harness.h"
#include "postprocess.h"

#include <cstddef>
#include <type_traits>

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


TEST(postprocess_parser_preserves_empty_tokens_and_early_off_rules)
{
	DX11PostEffect effects[DX11_POST_CHAIN_LIMIT];
	unsigned count = 99;
	CHECK(DX11Post_Parse_Chain("", effects, count));
	CHECK_EQ(0u, count);
	CHECK(DX11Post_Parse_Chain(",,bloom,,,fxaa,", effects, count));
	CHECK_EQ(2u, count);
	CHECK_EQ(DX11_POST_BLOOM, effects[0]);
	CHECK_EQ(DX11_POST_FXAA, effects[1]);
	CHECK(DX11Post_Parse_Chain("copy,copy,copy,copy,off,nonsense", effects, count));
	CHECK_EQ(0u, count);
	CHECK(!DX11Post_Parse_Chain("nonsense,off", effects, count));
	CHECK_EQ(0u, count);
	CHECK(!DX11Post_Parse_Chain("FXAA", effects, count));
	CHECK_EQ(0u, count);
	CHECK(!DX11Post_Parse_Chain("fxaa, sharpen", effects, count));
	CHECK_EQ(0u, count);
	CHECK(!DX11Post_Parse_Chain("bloom,bloom", effects, count));
	CHECK_EQ(0u, count);
	CHECK(!DX11Post_Parse_Chain("copy,copy,copy,copy,copy,off", effects, count));
	CHECK_EQ(0u, count);
}

TEST(postprocess_constant_layout_matches_shader_registers)
{
	CHECK(std::is_standard_layout<PostProcessConstants>::value);
	CHECK(std::is_trivial<PostProcessConstants>::value);
	CHECK(std::is_standard_layout<DX11PostWarp>::value);
	CHECK(std::is_trivial<DX11PostWarp>::value);
	CHECK_EQ(32u, sizeof(DX11PostWarp));
	CHECK_EQ(352u, sizeof(PostProcessConstants));
	CHECK_EQ(0u, offsetof(PostProcessConstants, TexelSize));
	CHECK_EQ(16u, offsetof(PostProcessConstants, BlurDirection));
	CHECK_EQ(32u, offsetof(PostProcessConstants, Tuning));
	CHECK_EQ(48u, offsetof(PostProcessConstants, WarpInfo));
	CHECK_EQ(64u, offsetof(PostProcessConstants, WarpCentre));
	CHECK_EQ(128u, offsetof(PostProcessConstants, WarpRing));
	CHECK_EQ(192u, offsetof(PostProcessConstants, Grade));
	CHECK_EQ(208u, offsetof(PostProcessConstants, Haze));
	CHECK_EQ(224u, offsetof(PostProcessConstants, HazeColour));
	CHECK_EQ(240u, offsetof(PostProcessConstants, WorldFromClip));
	CHECK_EQ(304u, offsetof(PostProcessConstants, HazeSun));
	CHECK_EQ(320u, offsetof(PostProcessConstants, HazeShroudRect));
	CHECK_EQ(336u, offsetof(PostProcessConstants, WorldAoProjection));
}
