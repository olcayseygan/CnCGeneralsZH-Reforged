/*
**	Copyright 2026 İlyas Akın
**	Additional terms under GNU GPL section 7 apply: see LICENSE.md.
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
/*
 * test_shader_sdl - the POSIX twin of test_ffshadercompile and test_ffvertexcompile: every program
 * the generators write, under the SDL3 GPU target, taken the whole way to what SDL's GPU backends
 * accept (decision 4).
 *
 * For each program in shader_cases.h - the 49 decision 4 was measured on less the three bumped
 * terrain programs that went with the normal maps, and the extras - it:
 *   1. generates the SDL3_GPU text,
 *   2. compiles it to SPIR-V through SDL3_Compile_HLSL_To_SPIRV (the one place the HLSL front end is
 *      named, so this test follows the compiler if decision 4's exit is ever taken),
 *   3. validates the SPIR-V with spirv-val for Vulkan 1.0, when the build found one,
 *   4. translates it to MSL through SDL_shadercross, and checks the slot contract in that MSL: every
 *      [[texture(n)]] has its [[sampler(n)]], and n is below the slot count SDL3_Shader_Slots gives
 *      the device - SDL binds slots 0 to n - 1 as texture-sampler pairs, and a texture outside that
 *      range is one Metal accepts and nothing ever binds,
 *   5. on macOS, has Metal compile it: SDL_CreateGPUShader on a real device, which hands the MSL to
 *      the driver (newLibraryWithSource).  Not the offline `xcrun metal`, which is not installed.
 *
 * A second test, on macOS, links every generated vertex program with every pixel program it meets
 * in the game into a real SDL pipeline: the stages agree by the locations sdl3target.h pins, or Metal
 * says which input is not written.  Which programs meet is the game's pairing, not every pair:
 *   - the engine's Trees vertex program meets ffshader's combiner programs only.  W3DTreeBuffer
 *     binds Trees.vso and no pixel shader (it loads Trees.pso and never binds it), and engineshader
 *     transcribes no Trees pixel program;
 *   - the engine's pixel programs (water, terrain, road, monochrome) meet ffvertex's programs only:
 *     their draws bind a .pso over the fixed-function vertex pipeline.
 * Trees against a water or terrain pixel program does not link - they read coordinate sets 2 and 3
 * and Trees writes two - and the game never draws it.
 *
 * Steps 3 and 5 can be unavailable - no spirv-val on the machine, no GPU in the session - and then
 * they say SKIPPED on a line of their own, with the reason and the count they did not check.  A
 * skipped step is never folded into a pass.
 */
#include "test_harness.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <string>
#include <vector>
#include <utility>
#include <complex>

#include <SDL3/SDL.h>
#if defined(__linux__)
#include <spirv_cross_c.h>
#include "../GameEngineDevice/Source/PosixDevice/Render/SdlConstants.h"
#endif

#include "sdl3shadercompile.h"
#include "shader_cases.h"
#include "Platform/WaterBedMaterial.h"
#include "Platform/WaterOpticalModel.h"
#include "Platform/WaterHullModel.h"
#include "Platform/WaterHullVisual.h"
#include "Platform/WaterSurfaceModel.h"
#include "Platform/WaterFftSampling.h"
#include "Platform/WaterScreenReflection.h"
#include "Platform/WaterPerformanceSettings.h"
#include "Platform/WaterFoamEnvironment.h"
#include "sdl3target.h"
#include <limits>

namespace {

struct Tally
{
	unsigned programs;
	unsigned generated;
	unsigned compiled;
	unsigned validated;
	unsigned translated;
	unsigned slots_kept;
	unsigned accepted;
};

// The [[texture(n)]] or [[sampler(n)]] indices in an MSL entry point's signature.
std::vector<int> msl_indices(const std::string & msl, const char * kind)
{
	std::vector<int> indices;
	const std::string opening = std::string("[[") + kind + "(";
	for (size_t at = msl.find(opening); at != std::string::npos; at = msl.find(opening, at + 1)) {
		indices.push_back(atoi(msl.c_str() + at + opening.size()));
	}
	return indices;
}

// Every texture has the sampler of its own index, and every index is one SDL binds.
bool slots_kept(const std::string & msl, unsigned samplers, std::string & why)
{
	const std::vector<int> textures = msl_indices(msl, "texture");
	const std::vector<int> sampler_indices = msl_indices(msl, "sampler");
	for (size_t i = 0; i < textures.size(); ++i) {
		if (textures[i] < 0 || (unsigned)textures[i] >= samplers) {
			why = "texture " + std::to_string(textures[i]) + " is outside the " + std::to_string(samplers) + " slots SDL binds";
			return false;
		}
		bool paired = false;
		for (size_t k = 0; k < sampler_indices.size(); ++k) paired = paired || sampler_indices[k] == textures[i];
		if (!paired) {
			why = "texture " + std::to_string(textures[i]) + " has no sampler of its own index";
			return false;
		}
		for (size_t j = i + 1; j < textures.size(); ++j) {
			if (textures[j] == textures[i]) {
				why = "two textures share slot " + std::to_string(textures[i]);
				return false;
			}
		}
	}
	for (size_t k = 0; k < sampler_indices.size(); ++k) {
		if (sampler_indices[k] < 0 || (unsigned)sampler_indices[k] >= samplers) {
			why = "sampler " + std::to_string(sampler_indices[k]) + " is outside the slots SDL binds";
			return false;
		}
	}
	return true;
}

// spirv-val on one module, through a file, because it reads files.
bool validate(const char * spirv_val, const std::vector<unsigned char> & spirv, std::string & output)
{
	const char * temp = getenv("TMPDIR");
	char path[1024];
	snprintf(path, sizeof(path), "%s/test_shader_sdl_%d.spv", (temp != NULL && *temp) ? temp : "/tmp", (int)getpid());
	FILE * file = fopen(path, "wb");
	if (file == NULL) return false;
	fwrite(&spirv[0], 1, spirv.size(), file);
	fclose(file);

	std::string command = std::string("\"") + spirv_val + "\" --target-env vulkan1.0 \"" + path + "\" 2>&1";
	FILE * pipe = popen(command.c_str(), "r");
	output.clear();
	if (pipe == NULL) return false;
	char line[512];
	while (fgets(line, sizeof(line), pipe) != NULL) output += line;
	const int status = pclose(pipe);
	remove(path);
	return status == 0;
}

void print_tally(const char * what, const Tally & t, bool validating, bool metal)
{
	printf("  %-15s %3u programs: generated %u, compiled %u, spirv-val %s, MSL %u (slots kept %u), Metal %s\n", what,
		t.programs, t.generated, t.compiled,
		validating ? (std::to_string(t.validated) + "/" + std::to_string(t.compiled)).c_str() : "SKIPPED",
		t.translated, t.slots_kept,
		metal ? (std::to_string(t.accepted) + "/" + std::to_string(t.translated)).c_str() : "SKIPPED");
}

} // namespace

// "[[vk::location(4)]] float2 TexCoord0 : TEXCOORD0;" lines of a vertex program's Input structure,
// as SDL vertex attributes over one interleaved buffer.  D3DCOLOR is four bytes read as UBYTE4_NORM,
// which is why sdl3target.h swaps it back to RGBA in the program.
bool vertex_layout(const std::string & hlsl, std::vector<SDL_GPUVertexAttribute> & attributes, Uint32 & pitch)
{
	attributes.clear();
	pitch = 0;
	const size_t start = hlsl.find("struct Input\n{\n");
	if (start == std::string::npos) return false;
	const size_t end = hlsl.find("};", start);
	size_t line = hlsl.find('\n', start + 14) + 1;
	while (line < end) {
		const size_t stop = hlsl.find('\n', line);
		const std::string text = hlsl.substr(line, stop - line);
		line = stop + 1;
		const size_t open = text.find("[[vk::location(");
		if (open == std::string::npos) continue;
		SDL_GPUVertexAttribute attribute;
		SDL_zero(attribute);
		attribute.location = (Uint32)atoi(text.c_str() + open + 15);
		attribute.offset = pitch;
		if (text.find(": COLOR") != std::string::npos) {
			attribute.format = SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM;
			pitch += 4;
		}
		else if (text.find(" float4 ") != std::string::npos) {
			attribute.format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4;
			pitch += 16;
		}
		else if (text.find(" float3 ") != std::string::npos) {
			attribute.format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
			pitch += 12;
		}
		else if (text.find(" float2 ") != std::string::npos) {
			attribute.format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2;
			pitch += 8;
		}
		else return false;
		attributes.push_back(attribute);
	}
	return !attributes.empty();
}

TEST(sdl_cloak_caster_fits_the_eight_slot_draw_contract)
{
	for (unsigned textured = 0; textured < 2; ++textured) {
		CombinerDescription description = {};
		description.StageCount = 1;
		description.Stages[0].ColourOperation = FF_TOP_SELECTARG1;
		description.Stages[0].AlphaOperation = FF_TOP_SELECTARG1;
		description.Stages[0].ColourArgument1 = textured ? FF_TA_TEXTURE : FF_TA_DIFFUSE;
		description.Stages[0].AlphaArgument1 = description.Stages[0].ColourArgument1;
		description.Stages[0].TextureBound = textured != 0;
		description.DrawEffect = DRAW_EFFECT_CLOAK_SHADOW;
		std::string generated;
		CHECK(CombinerShader_Generate(description, COMBINER_SHADER_TARGET_SDL3_GPU, generated));
		// The cosmetic cloak's scene texture is outside SDL's eight-slot layout.
		// Ordered caster coverage must not inherit that unused declaration.
		CHECK(generated.find("SceneColour") == std::string::npos);
		CHECK(generated.find("effect_cloak_shadow") != std::string::npos);
		for (unsigned depth_only = 0; depth_only < 2; ++depth_only) {
			std::string hlsl = generated;
			if (depth_only) {
				// Compile the same depth-only entry point used by Sun_Pixel_Program.
				const std::string entry = "float4 main(Input input) : SV_Target";
				const size_t at = hlsl.find(entry);
				CHECK(at != std::string::npos);
				if (at == std::string::npos) continue;
				hlsl.replace(at, entry.size(), "float4 sunCasterPixel(Input input)");
				hlsl += "\nvoid main(Input input) { sunCasterPixel(input); }\n";
			}
			std::vector<unsigned char> spirv;
			std::string log;
			const bool compiled = SDL3_Compile_HLSL_To_SPIRV(hlsl, false, spirv, log);
			CHECK(compiled);
			if (!compiled) {
				printf("  cloak caster: %s\n", log.c_str());
				continue;
			}
			unsigned samplers = 0, uniforms = 0;
			SDL3_Shader_Slots(spirv, false, samplers, uniforms);
			CHECK(samplers <= 8);
			CHECK_EQ(uniforms, 1u);
		}
	}
}

TEST(runtime_bed_material_is_tileable_deterministic_and_has_distinct_relief)
{
	// Sample every texel: wrapping must be identical without a random generator shared with gameplay.
	// Each channel must carry useful contrast; a flat texture or an accidental repeated channel fails.
	double sum[4] = {}, squares[4] = {};
	unsigned different = 0, raised = 0;
	const int side = 256;
	for (int y = 0; y < side; ++y) for (int x = 0; x < side; ++x) {
		const uint32_t value = WaterBedMaterial::Texel(x,y,side);
		CHECK_EQ(value, WaterBedMaterial::Texel(x+side,y-side,side));
		const unsigned relief = value & 255, pebbles = (value>>8)&255, sand = (value>>16)&255;
		different += pebbles != sand;
		raised += relief > 100;
		for (unsigned c = 0; c < 4; ++c) {
			const double b = (value >> (c*8)) & 255;
			sum[c] += b; squares[c] += b*b;
		}
	}
	CHECK(different > unsigned(side*side/2));
	CHECK(raised > unsigned(side*side/8) && raised < unsigned(side*side*3/4));
	for (unsigned c = 0; c < 4; ++c) {
		const double mean = sum[c]/(side*side);
		CHECK(mean > 10.0 && mean < 240.0);
		CHECK(squares[c]/(side*side)-mean*mean > 200.0);
	}
}

TEST(classic_water_uses_the_original_program_and_separate_cache_identity)
{
    // D3D11's Classic names keep the original transcription. Classic on SDL must be exactly
    // that program after binding retargeting, including fog/alpha-test variants.
    for (unsigned river=0; river<2; ++river) for (unsigned state=0; state<4; ++state) {
        const auto original=river?ENGINE_SHADER_WATER_RIVER:ENGINE_SHADER_WATER_TRAPEZOID;
        const auto classic=river?ENGINE_SHADER_WATER_CLASSIC_RIVER:ENGINE_SHADER_WATER_CLASSIC_TRAPEZOID;
        PixelPipelineDescription pipeline={};
        pipeline.FogEnabled=(state&1)!=0;
        pipeline.AlphaTestEnabled=(state&2)!=0;
        pipeline.AlphaFunction=FF_CMP_GREATEREQUAL;
        std::string expected,actual,modern;
        CHECK(EngineShader_Pixel_Program(classic,pipeline,expected,COMBINER_SHADER_TARGET_D3D11));
        CHECK(SDL3_Shader_Retarget(expected,false));
        CHECK(EngineShader_Pixel_Program(classic,pipeline,actual,COMBINER_SHADER_TARGET_SDL3_GPU));
        CHECK(actual==expected);
        CHECK(EngineShader_Pixel_Program(original,pipeline,modern,COMBINER_SHADER_TARGET_SDL3_GPU));
#if defined(__linux__)
        CHECK(actual!=modern);
#else
        CHECK(actual==modern);
#endif
        CHECK(std::string(EngineShader_Name(original))!=EngineShader_Name(classic));
        CHECK(EngineShader_From_File(river?"classic river water ps.1.1":"classic trapezoid water ps.1.1")==classic);
        std::vector<unsigned char> spirv;std::string log,msl;
        const bool compiled=SDL3_Compile_HLSL_To_SPIRV(actual,false,spirv,log);
        CHECK(compiled);
        if (!compiled) { printf("Classic water: %s\n",log.c_str()); continue; }
        unsigned samplers=0,buffers=0;SDL3_Shader_Slots(spirv,false,samplers,buffers);
        // The original preamble also declares ShadowMap at t5, retaining six binding slots.
        CHECK_EQ(samplers,6u);CHECK_EQ(buffers,1u);
        CHECK(SDL3_Translate_SPIRV_To_MSL(spirv,false,msl,log));
        const char *validator=getenv("ZH_SPIRV_VAL");
        if(validator && *validator) CHECK(validate(validator,spirv,log));
    }
}

#if defined(__linux__)
// These programs are selected only by the Linux Modern water runtime.
TEST(fft_water_vertex_and_pixel_compile_with_separate_texture_bindings)
{
	VertexPipelineDescription description=shader_cases_detail::plain_vertex();
	description.FVF=0x252; // XYZ, normal, diffuse, two texture coordinate sets, the actual water mesh.
	description.LightingEnabled=false;
	description.StageCount=4;
	for(unsigned i=0;i<4;++i) {
		description.Stages[i].TextureCoordinateIndex=i<2?i:0;
		description.Stages[i].TextureTransformFlags=FF_TTFF_DISABLE;
	}
	std::string vertex;
	CHECK(VertexShader_Generate(description,VERTEX_SHADER_TARGET_D3D11,vertex));
	// Hull constants must remain a water-only second slot. The ordinary fixed vertex program
	// keeps its existing single block even when it has the same water-compatible FVF.
	{
		std::string fixed=vertex,log;std::vector<unsigned char> spirv;
		CHECK(SDL3_Shader_Retarget(fixed,true));
		const bool compiled=SDL3_Compile_HLSL_To_SPIRV(fixed,true,spirv,log);CHECK(compiled);
		if(compiled) {
			unsigned samplers=0,buffers=0;SDL3_Shader_Slots(spirv,true,samplers,buffers);
			CHECK_EQ(samplers,0u);CHECK_EQ(buffers,1u);
		}
	}
	CHECK(WaterFftSampling::WrapVertex(vertex,true));
	CHECK(SDL3_Shader_Retarget(vertex,true));
	const size_t swap=vertex.find("input.Diffuse = input.Diffuse.bgra");
	CHECK(swap!=std::string::npos);
	CHECK(vertex.find("input.Diffuse = input.Diffuse.bgra",swap+1)==std::string::npos);
	PixelPipelineDescription pipeline={};
	std::string pixel;
	CHECK(EngineShader_Pixel_Program(ENGINE_SHADER_WATER_TRAPEZOID,pipeline,pixel,COMBINER_SHADER_TARGET_SDL3_GPU));
	for(int stage=0;stage<2;++stage) {
		const bool isVertex=stage==0;
		std::vector<unsigned char> spirv;std::string log,msl,why;
		const bool compiled=SDL3_Compile_HLSL_To_SPIRV(isVertex?vertex:pixel,isVertex,spirv,log);
		if(!compiled) printf("FFT water stage%d: %s\n",stage,log.c_str());
		CHECK(compiled);if(!compiled) continue;
		unsigned samplers=0,buffers=0;
		SDL3_Shader_Slots(spirv,isVertex,samplers,buffers);
		CHECK_EQ(samplers,isVertex?4u:15u);CHECK_EQ(buffers,isVertex?2u:4u);
		const char *validator=getenv("ZH_SPIRV_VAL");
		if(validator && *validator) CHECK(validate(validator,spirv,log));
		CHECK(SDL3_Translate_SPIRV_To_MSL(spirv,isVertex,msl,log));
		CHECK(slots_kept(msl,samplers,why));
	}
}

namespace {

struct WaterUniformMember
{
    const char *name;
    size_t offset;
    size_t size;
};

// The first three fields are flattened in HLSL, while C++ nests that common prefix.
#define WATER_COMBINER_MEMBER(name) { #name, offsetof(SdlWaterPixelConstants, Combiner) \
    + offsetof(SdlPixelConstants, name), sizeof(((SdlPixelConstants *)NULL)->name) }
#define WATER_UNIFORM_MEMBER(name) { #name, offsetof(SdlWaterPixelConstants, name), \
    sizeof(((SdlWaterPixelConstants *)NULL)->name) }
const WaterUniformMember WaterUniformMembers[] = {
    WATER_COMBINER_MEMBER(TextureFactor),
    WATER_COMBINER_MEMBER(FogColour),
    WATER_COMBINER_MEMBER(AlphaReference),
    WATER_UNIFORM_MEMBER(WorldFromClip),
    WATER_UNIFORM_MEMBER(CameraPosition),
    WATER_UNIFORM_MEMBER(SunDirection),
    WATER_UNIFORM_MEMBER(SunColour),
    WATER_UNIFORM_MEMBER(Viewport),
    WATER_UNIFORM_MEMBER(ReflectionArea),
    WATER_UNIFORM_MEMBER(WaterParameters),
    WATER_UNIFORM_MEMBER(TerrainParameters),
    WATER_UNIFORM_MEMBER(SceneParameters),
    WATER_UNIFORM_MEMBER(OpticalParameters),
    WATER_UNIFORM_MEMBER(HullParameters),
    WATER_UNIFORM_MEMBER(HullData),
    WATER_UNIFORM_MEMBER(FftParameters),
    WATER_UNIFORM_MEMBER(FoamHistoryArea),
    WATER_UNIFORM_MEMBER(FoamHistoryParameters),
    WATER_UNIFORM_MEMBER(ContactHullData),
    WATER_UNIFORM_MEMBER(FoamFillLights),
    WATER_UNIFORM_MEMBER(MaterialColor),
    WATER_UNIFORM_MEMBER(MaterialParameters),
    WATER_UNIFORM_MEMBER(ClipFromWorld)
};
#undef WATER_COMBINER_MEMBER
#undef WATER_UNIFORM_MEMBER

void checkCompiledWaterUniformLayout(const std::string &source, bool vertex,
    const char *blockName, bool visual)
{
    std::vector<unsigned char> spirv;
    std::string log;
    const bool compiled = SDL3_Compile_HLSL_To_SPIRV(source, vertex, spirv, log);
    if (!compiled) printf("Water uniform reflection: %s\n", log.c_str());
    CHECK(compiled);
    if (!compiled) return;

    struct ReflectionContext {
        spvc_context context = NULL;
        ~ReflectionContext() { if (context) spvc_context_destroy(context); }
    } reflection;
    const bool created = spvc_context_create(&reflection.context) == SPVC_SUCCESS;
    CHECK(created);
    if (!created) return;

    // Copy into aligned words; the production compiler exposes its bytes as unsigned char.
    CHECK(spirv.size() % sizeof(SpvId) == 0);
    if (spirv.size() % sizeof(SpvId) != 0) return;
    std::vector<SpvId> words(spirv.size() / sizeof(SpvId));
    memcpy(words.data(), spirv.data(), spirv.size());
    spvc_parsed_ir ir = NULL;
    spvc_compiler compiler = NULL;
    spvc_resources resources = NULL;
    const spvc_reflected_resource *blocks = NULL;
    size_t blockCount = 0;
    const bool reflected =
        spvc_context_parse_spirv(reflection.context, words.data(), words.size(), &ir) == SPVC_SUCCESS
        && spvc_context_create_compiler(reflection.context, SPVC_BACKEND_NONE, ir,
            SPVC_CAPTURE_MODE_TAKE_OWNERSHIP, &compiler) == SPVC_SUCCESS
        && spvc_compiler_create_shader_resources(compiler, &resources) == SPVC_SUCCESS
        && spvc_resources_get_resource_list_for_type(resources, SPVC_RESOURCE_TYPE_UNIFORM_BUFFER,
            &blocks, &blockCount) == SPVC_SUCCESS;
    CHECK(reflected);
    if (!reflected) return;

    const spvc_reflected_resource *block = NULL;
    for (size_t i = 0; i < blockCount; ++i)
        if (strcmp(blocks[i].name, blockName) == 0) block = &blocks[i];
    CHECK(block != NULL);
    if (!block) return;

    const spvc_type type = spvc_compiler_get_type_handle(compiler, block->base_type_id);
    size_t blockSize = 0;
    CHECK(spvc_compiler_get_declared_struct_size(compiler, type, &blockSize) == SPVC_SUCCESS);
    CHECK_EQ(blockSize, visual ? sizeof(SdlWaterVisualPixelConstants) : sizeof(SdlWaterPixelConstants));
    const unsigned commonMembers = sizeof(WaterUniformMembers) / sizeof(WaterUniformMembers[0]);
    const unsigned memberCount = spvc_type_get_num_member_types(type);
    CHECK_EQ(memberCount, commonMembers + (visual ? 1u : 0u));
    for (unsigned i = 0; i < memberCount; ++i) {
        const WaterUniformMember metadata = { "VisualHullMeta",
            offsetof(SdlWaterVisualPixelConstants, VisualHullMeta),
            sizeof(((SdlWaterVisualPixelConstants *)NULL)->VisualHullMeta) };
        if (i >= commonMembers && (!visual || i != commonMembers)) continue;
        const WaterUniformMember &expected = i < commonMembers ? WaterUniformMembers[i] : metadata;
        const char *name = spvc_compiler_get_member_name(compiler, block->base_type_id, i);
        unsigned offset = 0;
        size_t size = 0;
        CHECK(strcmp(name, expected.name) == 0);
        CHECK(spvc_compiler_type_struct_member_offset(compiler, type, i, &offset) == SPVC_SUCCESS);
        CHECK(spvc_compiler_get_declared_struct_member_size(compiler, type, i, &size) == SPVC_SUCCESS);
        CHECK_EQ(size_t(offset), expected.offset);
        CHECK_EQ(size, expected.size);
    }
}

} // namespace

TEST(modern_water_compiled_uniform_layout_matches_every_cpu_upload_field)
{
    for (const EngineShaderProgram program : {ENGINE_SHADER_WATER_TRAPEZOID, ENGINE_SHADER_WATER_RIVER}) {
        PixelPipelineDescription pipeline = {};
        std::string source;
        const bool generated = EngineShader_Pixel_Program(program, pipeline, source,
            COMBINER_SHADER_TARGET_SDL3_GPU);
        CHECK(generated);
        if (generated) checkCompiledWaterUniformLayout(source, false, "CombinerConstants", true);
    }

    VertexPipelineDescription description = shader_cases_detail::plain_vertex();
    description.FVF = 0x252;
    description.LightingEnabled = false;
    description.StageCount = 4;
    for (unsigned i = 0; i < 4; ++i) {
        description.Stages[i].TextureCoordinateIndex = i < 2 ? i : 0;
        description.Stages[i].TextureTransformFlags = FF_TTFF_DISABLE;
    }
    std::string vertex;
    const bool generated = VertexShader_Generate(description, VERTEX_SHADER_TARGET_D3D11, vertex)
        && WaterFftSampling::WrapVertex(vertex,true) && SDL3_Shader_Retarget(vertex, true);
    CHECK(generated);
    if (generated) checkCompiledWaterUniformLayout(vertex, true, "WaterVertexParameters", false);
}

#endif // __linux__

TEST(fft_spectral_power_bands_partition_and_join_smoothly)
{
	using namespace WaterFftSpectrum;
	// Requested world-space band boundaries. Assertions concern conservation and
	// continuity, not copied ramp math.
	const double joins[]={24.0,40.0,128.0,256.0,1024.0,1792.0};
	for(unsigned i=1;i<=20000;++i) {
		const double wavelength=2000.0*i/20000;
		double sum=0;
		for(unsigned c=0;c<Cascades;++c) {
			const double power=BandPower(c,wavelength);
			CHECK(std::isfinite(power) && power>=0 && power<=1);
			sum+=power;
		}
		if(wavelength<=1024.0) CHECK_NEAR(sum,1,1e-12);
		else CHECK(sum>=0 && sum<=1+1e-12);
		if(wavelength>=1792.0) CHECK(sum==0);
	}
	for(double join:joins) for(unsigned c=0;c<Cascades;++c) {
		const double step=1e-5,mid=BandPower(c,join);
		const double left=BandPower(c,join-step),right=BandPower(c,join+step);
		CHECK(std::abs(left-right)<1e-7);
		// Each adjoining constant segment has derivative zero. Check both sides,
		// so a merely continuous triangular fade cannot satisfy this contract.
		CHECK(std::abs((mid-left)/step)<1e-5);
		CHECK(std::abs((right-mid)/step)<1e-5);
	}
	// Direction of the crossfades and no gap or duplicate full-strength band.
	for(unsigned i=0;i<100;++i) {
		const double fine=24.0+16.0*i/100,medium=128.0+128.0*i/100,longer=1024.0+768.0*i/100;
		CHECK(BandPower(2,fine+.16)<=BandPower(2,fine));
		CHECK(BandPower(1,fine+.16)>=BandPower(1,fine));
		CHECK(BandPower(0,medium+1.28)>=BandPower(0,medium));
		CHECK(BandPower(1,medium+1.28)<=BandPower(1,medium));
		CHECK(BandPower(0,longer+7.68)<=BandPower(0,longer));
	}
	CHECK(BandPower(0,180.0)>0 && BandPower(1,180.0)>0);
	CHECK(BandPower(1,32.0)>0 && BandPower(2,32.0)>0);
	CHECK(BandPower(0,32.0)==0 && BandPower(2,180.0)==0);
}

TEST(fft_generated_spectrum_has_normalized_separated_directional_energy)
{
	using namespace WaterFftSpectrum;
	const std::vector<float> initial=InitialSpectrum(),again=InitialSpectrum();
	CHECK(initial==again); // repeated generation must not consume gameplay RNG/state
	const unsigned n=Resolution,texels=n*n;
	CHECK_EQ(initial.size(),size_t(texels*Cascades*4));
	if(initial.size()!=texels*Cascades*4) return;
	double energy[Cascades]={},slope[Cascades]={},broad[Cascades]={};
	double directionCos[Cascades]={},directionSin[Cascades]={};
	bool finite=true,zeroSpecial=true,support=true,oneWay=true,paired=true;
	unsigned active[Cascades]={};
	const double axis=std::atan2(WindDirectionY,WindDirectionX);
	for(unsigned c=0;c<Cascades;++c) for(unsigned y=0;y<n;++y) for(unsigned x=0;x<n;++x) {
		const unsigned opposite=((n-y)%n)*n+(n-x)%n;
		const float *h=&initial[(c*texels+y*n+x)*4],*other=&initial[(c*texels+opposite)*4];
		double power=0;
		for(unsigned j=0;j<4;++j) {finite=finite && std::isfinite(h[j]);power+=double(h[j])*h[j];}
		if(x==0 || y==0 || (x==n/2 && y==n/2)) zeroSpecial=zeroSpecial && power==0;
		const double forward=double(h[0])*h[0]+double(h[1])*h[1];
		const double backward=double(h[2])*h[2]+double(h[3])*h[3];
		oneWay=oneWay && (forward==0 || backward==0);
		// h0(k) == conjugate(packed negative branch at -k), exactly after float stores.
		paired=paired && h[0]==other[2] && h[1]==-other[3] && h[2]==other[0] && h[3]==-other[1];
		const double kx=2*Pi*(int(x)-int(n/2))/Sizes[c],ky=2*Pi*(int(y)-int(n/2))/Sizes[c];
		const double k2=kx*kx+ky*ky;
		if(k2==0) continue;
		const double wavelength=2*Pi*WorldScale/std::sqrt(k2);
		// Independent generated-support checks: a broken generator cannot pass
		// merely because it and the BandPower function agree on the same mistake.
		const bool outside=(c==0 && (wavelength<=128.0 || wavelength>=1792.0)) ||
			(c==1 && (wavelength<=24.0 || wavelength>=256.0)) || (c==2 && wavelength>=40.0);
		if(outside) support=support && power==0;
		if(power==0) continue;
		++active[c];energy[c]+=power;slope[c]+=power*k2;
		if(wavelength>512) broad[c]+=power;
		const double angle=std::atan2(ky,kx)-axis;
		directionCos[c]+=power*std::cos(2*angle);directionSin[c]+=power*std::sin(2*angle);
	}
	CHECK(finite && zeroSpecial && support && oneWay && paired);
	double totalSlope=0;
	const auto &unresolved=WaterFftFiltering::SpectrumVariance();
	for(unsigned c=0;c<Cascades;++c) {
		CHECK(active[c]>100);
		CHECK(std::abs(std::sqrt(energy[c])*WorldScale-HeightRms[c])<HeightRms[c]*2e-7);
		// For angular power |cos(theta)|^p, the continuous axial second moment
		// is p/(p+2). This independent integral tests actual generated direction,
		// allowing finite seeded-grid variation without pinning exact coefficients.
		const double expectedMoment=DirectionPowers[c]/(DirectionPowers[c]+2);
		CHECK(std::abs(directionCos[c]/energy[c]-expectedMoment)<.08);
		CHECK(std::abs(directionSin[c]/energy[c])<.08);
		// Final mip averages the entire periodic field. With zero DC, all slope
		// power is unresolved: a stale M2 spectrum table must not silently survive.
		CHECK(std::abs(unresolved[Phases][c]-slope[c])<1e-10*(1+slope[c]));
		totalSlope+=slope[c];
	}
	// Broad swell must actually exist, while medium/fine cannot counterfeit it.
	CHECK(broad[0]/energy[0]>.25 && broad[0]/energy[0]<.55);
	CHECK(broad[1]==0 && broad[2]==0);
	CHECK(energy[0]>3*energy[1] && energy[1]>10*energy[2]);
	// Guard against recovering old height by amplifying the high-frequency slope.
	CHECK(std::sqrt(totalSlope)<.15 && std::sqrt(slope[2])<.10);
	printf("  FFT bands: height RMS %.8g/%.8g/%.8g; slope RMS %.8g/%.8g/%.8g; long power >512 %.3f%%\n",
		std::sqrt(energy[0])*WorldScale,std::sqrt(energy[1])*WorldScale,std::sqrt(energy[2])*WorldScale,
		std::sqrt(slope[0]),std::sqrt(slope[1]),std::sqrt(slope[2]),100*broad[0]/energy[0]);
}

TEST(fft_generated_spectrum_preserves_real_field_and_energy_over_time)
{
	using namespace WaterFftSpectrum;using Complex=std::complex<double>;
	const auto initial=InitialSpectrum();const unsigned n=Resolution,texels=n*n;
	const double times[]={0.0,2.75,61.0};
	for(unsigned c=0;c<Cascades;++c) for(double seconds:times) {
		double packedEnergy=0,evolvedEnergy=0,maxConjugateError=0;
		for(unsigned y=0;y<n;++y) for(unsigned x=0;x<n;++x) {
			const float *h=&initial[(c*texels+y*n+x)*4];
			const float *other=&initial[(c*texels+((n-y)%n)*n+(n-x)%n)*4];
			const double kx=2*Pi*(int(x)-int(n/2))/Sizes[c],ky=2*Pi*(int(y)-int(n/2))/Sizes[c];
			// Independently evolve the actual packed coefficients with the retained
			// dispersion. Conjugate symmetry gives a real spatial inverse transform.
			const Complex phase=std::polar(1.0,std::sqrt(9.81*std::hypot(kx,ky))*seconds);
			const Complex forward(h[0],h[1]),backward(h[2],h[3]);
			const Complex evolved=forward*phase+backward*std::conj(phase);
			const Complex opposite=Complex(other[0],other[1])*phase+Complex(other[2],other[3])*std::conj(phase);
			maxConjugateError=(std::max)(maxConjugateError,std::abs(evolved-std::conj(opposite)));
			packedEnergy+=std::norm(forward)+std::norm(backward);evolvedEnergy+=std::norm(evolved);
		}
		CHECK(maxConjugateError<1e-12);
		CHECK(std::abs(packedEnergy-evolvedEnergy)<1e-12*(1+packedEnergy));
		CHECK(std::abs(std::sqrt(evolvedEnergy)*WorldScale-HeightRms[c])<HeightRms[c]*2e-7);
	}
}

TEST(fft_filter_variance_matches_independent_discrete_box_averages)
{
	using namespace WaterFftSpectrum;
	const int modes[][2]={{1,0},{3,2},{19,-11},{83,47}};
	double largest=0;
	for(unsigned cascade=0;cascade<Cascades;++cascade) for(const auto &mode:modes) {
		std::vector<float> input(Resolution*Resolution*Cascades*4,0);
		const unsigned x=mode[0]+Resolution/2,y=mode[1]+Resolution/2;
		float *v=&input[((cascade*Resolution+y)*Resolution+x)*4];v[0]=.75f;v[1]=.25f;
		const auto table=WaterFftFiltering::UnresolvedSlopeVariance(input);
		// Explicitly average complex samples of one Fourier mode over the texel box.
		// This oracle does not use the production sine-ratio transfer function.
		const double kx=2*Pi*mode[0]/Sizes[cascade],ky=2*Pi*mode[1]/Sizes[cascade];
		const double power=(.75*.75+.25*.25)*(kx*kx+ky*ky);
		for(unsigned level=0;level<WaterFftFiltering::Levels;++level) {
			const unsigned width=1u<<level;std::complex<double> ax=0,ay=0;
			for(unsigned i=0;i<width;++i) {
				ax+=std::polar(1.0,2*Pi*mode[0]*i/Resolution);
				ay+=std::polar(1.0,2*Pi*mode[1]*i/Resolution);
			}
			const double retained=std::norm(ax/double(width))*std::norm(ay/double(width));
			const double expected=power*(1-retained),error=std::abs(table[level][cascade]-expected);
			largest=(std::max)(largest,error);CHECK(error<1e-10*(1+power));
			for(unsigned c=0;c<Cascades;++c) if(c!=cascade) CHECK(table[level][c]==0);
		}
	}
	const auto &table=WaterFftFiltering::SpectrumVariance();
	CHECK(&table==&WaterFftFiltering::SpectrumVariance());
	for(unsigned c=0;c<Cascades;++c) {
		CHECK(table[0][c]==0 && table[8][c]>0);
		for(unsigned l=1;l<WaterFftFiltering::Levels;++l)
			CHECK(std::isfinite(table[l][c]) && table[l][c]>=table[l-1][c]-1e-12 && table[l][c]<=table[8][c]+1e-12);
	}
	printf("  FFT unresolved slope power: independent discrete box error %.9g; per-cascade zero-LOD and bounded mip hierarchy checked\n",largest);
}

TEST(every_generated_program_reaches_sdl3_gpu)
{
	const char * spirv_val = getenv("ZH_SPIRV_VAL");
	const bool validating = spirv_val != NULL && *spirv_val != '\0';
	if (!validating) {
		printf("  SKIPPED spirv-val: ZH_SPIRV_VAL is not set (CMake found no spirv-val; install spirv-tools)\n");
	}

	// Metal only.  On Linux the SPIR-V is the program, and spirv-val is the check there is.
	SDL_GPUDevice * device = NULL;
#if defined(__APPLE__)
	if (SDL_Init(SDL_INIT_VIDEO)) {
		device = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_MSL, false, NULL);
	}
	if (device == NULL) {
		printf("  SKIPPED Metal: no GPU device in this session (%s)\n", SDL_GetError());
	}
#endif
	const bool metal = device != NULL;

	Tally reference, extra;
	memset(&reference, 0, sizeof(reference));
	memset(&extra, 0, sizeof(extra));

	const std::vector<ShaderCase> cases = Shader_Cases();
	for (size_t i = 0; i < cases.size(); ++i) {
		const ShaderCase & c = cases[i];
		Tally & t = c.Reference ? reference : extra;
		++t.programs;

		std::string hlsl;
		if (!Shader_Case_Generate(c, SHADER_CASE_SDL3_GPU, hlsl)) {
			printf("  %s: the generator refused the SDL3 target\n", c.Name.c_str());
			CHECK(false);
			continue;
		}
		++t.generated;

		std::vector<unsigned char> spirv;
		std::string log;
		if (!SDL3_Compile_HLSL_To_SPIRV(hlsl, c.VertexStage, spirv, log)) {
			printf("  %s: HLSL -> SPIR-V failed:\n%s\n", c.Name.c_str(), log.c_str());
			CHECK(false);
			continue;
		}
		++t.compiled;

		if (validating) {
			std::string output;
			if (validate(spirv_val, spirv, output)) {
				++t.validated;
			}
			else {
				printf("  %s: spirv-val refused it:\n%s\n", c.Name.c_str(), output.c_str());
				CHECK(false);
			}
		}

		std::string msl;
		if (!SDL3_Translate_SPIRV_To_MSL(spirv, c.VertexStage, msl, log)) {
			printf("  %s: SPIR-V -> MSL failed: %s\n", c.Name.c_str(), log.c_str());
			CHECK(false);
			continue;
		}
		++t.translated;

		unsigned samplers = 0, uniform_buffers = 0;
		SDL3_Shader_Slots(spirv, c.VertexStage, samplers, uniform_buffers);
		std::string why;
		if (slots_kept(msl, samplers, why)) {
			++t.slots_kept;
		}
		else {
			printf("  %s: slot contract broken in its MSL: %s\n", c.Name.c_str(), why.c_str());
			CHECK(false);
		}

		if (metal) {
			SDL_GPUShader * shader = SDL3_Create_Shader(device, spirv, c.VertexStage, log);
			if (shader != NULL) {
				++t.accepted;
				SDL_ReleaseGPUShader(device, shader);
			}
			else {
				printf("  %s: Metal refused it: %s\n", c.Name.c_str(), log.c_str());
				CHECK(false);
			}
		}
	}

	print_tally("reference set", reference, validating, metal);
	print_tally("beyond it", extra, validating, metal);
	CHECK_EQ(reference.programs, 46u);
	CHECK(extra.programs > 0);

	if (device != NULL) {
		SDL_DestroyGPUDevice(device);
	}
	SDL_Quit();
}

TEST(every_vertex_program_links_with_every_pixel_program_it_can_meet)
{
#if !defined(__APPLE__)
	printf("  SKIPPED pipeline linking: Metal only; on Linux the stages are checked by spirv-val alone\n");
#else
	SDL_GPUDevice * device = NULL;
	if (SDL_Init(SDL_INIT_VIDEO)) {
		device = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_MSL, true, NULL);
	}
	if (device == NULL) {
		printf("  SKIPPED pipeline linking: no GPU device in this session (%s)\n", SDL_GetError());
		return;
	}

	struct Built
	{
		std::string name;
		std::string hlsl;
		SDL_GPUShader * shader;
		bool engine;
	};
	std::vector<Built> vertex, pixel;
	const std::vector<ShaderCase> cases = Shader_Cases();
	for (size_t i = 0; i < cases.size(); ++i) {
		Built built;
		built.name = cases[i].Name;
		std::vector<unsigned char> spirv;
		std::string log;
		if (!Shader_Case_Generate(cases[i], SHADER_CASE_SDL3_GPU, built.hlsl)
			|| !SDL3_Compile_HLSL_To_SPIRV(built.hlsl, cases[i].VertexStage, spirv, log)) {
			CHECK(false);
			continue;
		}
		built.shader = SDL3_Create_Shader(device, spirv, cases[i].VertexStage, log);
		built.engine = cases[i].Kind == SHADER_CASE_ENGINE;
		if (built.shader == NULL) {
			CHECK(false);
			continue;
		}
		(cases[i].VertexStage ? vertex : pixel).push_back(built);
	}

	unsigned pairs = 0, linked = 0;
	for (size_t v = 0; v < vertex.size(); ++v) {
		std::vector<SDL_GPUVertexAttribute> attributes;
		Uint32 pitch = 0;
		if (!vertex_layout(vertex[v].hlsl, attributes, pitch)) {
			printf("  %s: no vertex layout could be read from its Input structure\n", vertex[v].name.c_str());
			CHECK(false);
			continue;
		}
		SDL_GPUVertexBufferDescription buffer;
		SDL_zero(buffer);
		buffer.pitch = pitch;
		buffer.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
		SDL_GPUColorTargetDescription target;
		SDL_zero(target);
		target.format = SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM;

		for (size_t p = 0; p < pixel.size(); ++p) {
			if (vertex[v].engine && pixel[p].engine) continue;	// the game never draws that pair
			++pairs;
			SDL_GPUGraphicsPipelineCreateInfo info;
			SDL_zero(info);
			info.vertex_shader = vertex[v].shader;
			info.fragment_shader = pixel[p].shader;
			info.vertex_input_state.vertex_buffer_descriptions = &buffer;
			info.vertex_input_state.num_vertex_buffers = 1;
			info.vertex_input_state.vertex_attributes = &attributes[0];
			info.vertex_input_state.num_vertex_attributes = (Uint32)attributes.size();
			info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
			info.target_info.color_target_descriptions = &target;
			info.target_info.num_color_targets = 1;
			SDL_ClearError();
			SDL_GPUGraphicsPipeline * pipeline = SDL_CreateGPUGraphicsPipeline(device, &info);
			if (pipeline != NULL) {
				++linked;
				SDL_ReleaseGPUGraphicsPipeline(device, pipeline);
			}
			else {
				printf("  %s + %s: Metal refused the pipeline: %s\n", vertex[v].name.c_str(), pixel[p].name.c_str(), SDL_GetError());
				CHECK(false);
			}
		}
	}
	printf("  %zu vertex x %zu pixel programs: %u pairs the game can form, %u linked on Metal\n", vertex.size(), pixel.size(),
		pairs, linked);
	CHECK(pairs > 0);
	CHECK_EQ(linked, pairs);

	for (size_t i = 0; i < vertex.size(); ++i) SDL_ReleaseGPUShader(device, vertex[i].shader);
	for (size_t i = 0; i < pixel.size(); ++i) SDL_ReleaseGPUShader(device, pixel[i].shader);
	SDL_DestroyGPUDevice(device);
	SDL_Quit();
#endif
}

namespace {
float opticalChannel(const WaterOpticalModel::Rgb &rgb,int channel)
{
	return channel==0?rgb.r:(channel==1?rgb.g:rgb.b);
}
float displayedBed(float bed,float scatter,float transmission)
{
	return std::pow(std::pow(bed,2.2f)*transmission+scatter*(1.0f-transmission),1.0f/2.2f);
}
float displayedBedContrast(float transmission,float scatter)
{
	return (displayedBed(0.65f,scatter,transmission)-displayedBed(0.25f,scatter,transmission))/0.4f;
}
}

TEST(water_optics_shores_and_unclassified_pools_keep_existing_shallow_transmission)
{
	using namespace WaterOpticalModel;
	for (float river:{0.0f,0.25f,0.5f,1.0f}) for (float distance:{0.0f,0.25f,2.0f,5.0f,10.0f,20.0f,80.0f}) {
		const float materialPath=distance*(2.4f+(1.0f-2.4f)*river);
		const Rgb original={std::exp(-materialPath*0.065f),std::exp(-materialPath*0.026f),std::exp(-materialPath*0.019f)};
		for (float opacity:{0.0f,0.5f,0.75f,1.0f}) for (float depth:{0.25f,2.7f,3.0f,10.0f}) {
			const Rgb shore=Transmission(distance,river,0.0f,depth,opacity);
			CHECK(memcmp(&shore,&original,sizeof(Rgb))==0);
			const Rgb unauthored=Transmission(distance,river,1.0f,depth,0.0f);
			CHECK(memcmp(&unauthored,&original,sizeof(Rgb))==0);
		}
	}
	CHECK(std::abs(OffshoreExtinction(2.7f,0.75f)-(-std::log(0.25f)/2.7f))<0.000001f);
	CHECK(std::abs(OffshoreExtinction(3.0f,1.0f)-(-std::log(0.01f)/3.0f))<0.000001f);
	const Rgb zero=Transmission(0,0,1,0,1);
	CHECK(zero.r==1 && zero.g==1 && zero.b==1);
	CHECK(std::abs(displayedBed(0.43f,0.028f,zero.g)-0.43f)<0.000001f);
}

TEST(water_optics_authored_extinction_uses_unscaled_physical_ray_length)
{
	using namespace WaterOpticalModel;
	for (float distance:{0.25f,1.0f,3.2f,10.0f,40.0f}) for (float river:{0.0f,0.5f,1.0f}) {
		const Rgb base=Transmission(distance,river,0,2.7f,0.75f);
		const Rgb offshore=Transmission(distance,river,1,2.7f,0.75f);
		const Rgb halfway=Transmission(distance,river,0.5f,2.7f,0.75f);
		const float authored=std::exp(-distance*OffshoreExtinction(2.7f,0.75f));
		for (int channel=0;channel<3;++channel) {
			const float a=opticalChannel(base,channel),b=opticalChannel(offshore,channel),m=opticalChannel(halfway,channel);
			CHECK(std::abs(b-a*authored)<0.000001f);
			CHECK(m>=b && m<=a);
			CHECK(std::abs(m*m-a*b)<0.000001f); // Optical thickness interpolates, not display colour.
		}
	}
}

TEST(water_optics_transmission_is_finite_bounded_and_monotone)
{
	using namespace WaterOpticalModel;
	const float infinity=std::numeric_limits<float>::infinity(),nan=std::numeric_limits<float>::quiet_NaN();
	const float inputs[]={-infinity,nan,-3,0,0.1f,0.5f,1,3,100,infinity};
	for (float distance:inputs) for (float river:inputs) for (float offshore:inputs)
		for (float depth:inputs) for (float opacity:inputs) {
		const Rgb t=Transmission(distance,river,offshore,depth,opacity);
		CHECK(std::isfinite(t.r) && std::isfinite(t.g) && std::isfinite(t.b));
		CHECK(t.r>=0 && t.r<=1 && t.g>=0 && t.g<=1 && t.b>=0 && t.b<=1);
	}
	for (float river:{0.0f,1.0f}) for (float offshore:{0.0f,0.5f,1.0f}) {
		Rgb previous=Transmission(0,river,offshore,2.7f,0.75f);
		for (int i=1;i<=1000;++i) {
			const Rgb current=Transmission(i*0.5f,river,offshore,2.7f,0.75f);
			CHECK(current.r<=previous.r && current.g<=previous.g && current.b<=previous.b);
			previous=current;
		}
	}
}

TEST(water_optics_deep_water_converges_without_a_180_unit_visibility_plateau)
{
	using namespace WaterOpticalModel;
	for (float river:{0.0f,1.0f}) {
		const Rgb at180=Transmission(180,river,0,3,0),at360=Transmission(360,river,0,3,0),deep=Transmission(100000,river,0,3,0);
		CHECK(at360.r<at180.r && at360.g<at180.g && at360.b<at180.b);
		CHECK(deep.r<1e-30f && deep.g<1e-30f && deep.b<1e-30f);
		CHECK(std::abs(displayedBed(0.65f,0.041f,deep.b)-std::pow(0.041f,1.0f/2.2f))<0.000001f);
	}
	for (float distance:{0.0f,1.0f,10.0f,100.0f}) {
		const Rgb t=Transmission(distance,0,1,3,1);
		// A bed already equal to the scattering radiance must not brighten/darken with distance.
		const float scatter=0.028f,displayScatter=std::pow(scatter,1.0f/2.2f);
		CHECK(std::abs(displayedBed(displayScatter,scatter,t.g)-displayScatter)<0.000001f);
	}
}

TEST(water_optics_authored_offshore_examples_hide_bed_in_display_space)
{
	using namespace WaterOpticalModel;
	const float scatter[3]={0.002f,0.028f,0.041f};
	// Optical interpretations of the maps' existing artistic settings: mission water at Z=7
	// has a ~10-unit ray, opacity .75 at reference depth 2.7; shell water at Z=21 has ~2.25
	// units of vertical depth and a ~3.2-unit ray, opacity 1 at reference depth 3.
	const float scenarios[][3]={{10.0f,0.75f,2.7f},{3.2f,1.0f,3.0f}};
	for (const auto &scenario:scenarios) {
		const Rgb shore=Transmission(scenario[0],0,0,scenario[2],scenario[1]);
		const Rgb offshore=Transmission(scenario[0],0,1,scenario[2],scenario[1]);
		for (int channel=0;channel<3;++channel) {
			CHECK(displayedBedContrast(opticalChannel(offshore,channel),scatter[channel])<0.05f);
			CHECK(displayedBedContrast(opticalChannel(offshore,channel),scatter[channel])<
				displayedBedContrast(opticalChannel(shore,channel),scatter[channel]));
		}
	}
}

TEST(water_optical_ray_correction_preserves_camera_rays_and_zero_offshore)
{
	using namespace WaterOpticalModel;
	for (float vz:{.25f,.5f,.8f,1.0f}) for (float delta:{-3.0f,-.7f,0.0f,.4f,2.0f})
		for (float offshore:{0.0f,.25f,.5f,1.0f}) {
			const float vx=std::sqrt(1-vz*vz),shift=RayShift(delta,vz,offshore,7,100,100);
			const float x=5+vx*shift,z=12+vz*shift;
			// Independent intersection with a horizontal analytic surface: sufficient room to both
			// the camera and the scene means the new point must land exactly at its blended height.
			CHECK(std::abs(z-(12+delta*offshore))<0.000003f);
			CHECK(std::abs((x-5)*vz-(z-12)*vx)<0.000003f); // parallel to the same camera ray
			const float sceneX=5-vx*100,sceneZ=12-vz*100;
			const float distance=std::sqrt((x-sceneX)*(x-sceneX)+(z-sceneZ)*(z-sceneZ));
			CHECK(std::abs(distance-(100+shift))<0.00003f);
			if (offshore==0) {
				CHECK(shift==0 && x==5 && z==12);
				const Rgb before=Transmission(100,0,0,3,.75f),after=Transmission(100+shift,0,0,3,.75f);
				CHECK(before.r==after.r && before.g==after.g && before.b==after.b);
			}
		}
}

TEST(water_optical_ray_correction_cannot_cross_scene_hits_or_the_camera)
{
	using namespace WaterOpticalModel;
	for (float delta:{-1000000.0f,-100.0f,-1.0f,0.0f,1.0f,100.0f,1000000.0f})
		for (float vz:{-.8f,0.0f,.001f,.05f,.2f,.8f,1.0f})
			for (float offshore:{0.0f,.25f,.5f,1.0f})
				for (float hit:{0.0f,.01f,.08f,.2f,2.0f,50.0f})
					for (float camera:{0.0f,.04f,.08f,.3f,12.0f,100.0f}) {
						const float shift=RayShift(delta,vz,offshore,7,hit,camera);
						CHECK(std::isfinite(shift));
						CHECK(std::abs(shift)<=70.00001f); // 2*7 height / .2 grazing denominator
						CHECK(hit+shift>=std::min(hit,RaySurfaceMargin)-0.00001f);
						CHECK(camera-shift>=std::min(camera,RaySurfaceMargin)-0.00001f);
						if (vz<=0 || offshore==0) CHECK(shift==0);
					}
	const float nan=std::numeric_limits<float>::quiet_NaN(),inf=std::numeric_limits<float>::infinity();
	for (float invalid:{nan,inf,-inf}) {
		CHECK(RayShift(invalid,.8f,1,7,10,100)==0);
		CHECK(RayShift(1,invalid,1,7,10,100)==0);
		CHECK(std::isfinite(RayShift(-1,.8f,1,7,invalid,100)));
		CHECK(std::isfinite(RayShift(1,.8f,1,7,10,invalid)));
		CHECK(std::isfinite(RayShift(1,.8f,invalid,7,10,100)));
		CHECK(std::isfinite(RayShift(1,.8f,1,invalid,10,100)));
	}
}

TEST(water_optical_ray_correction_keeps_submerged_objects_clearer_than_the_bed)
{
	using namespace WaterOpticalModel;
	// An object lies .25 along the rendered camera ray and the terrain lies 4 units away. The
	// analytic trough would move the surface 2.5 units along that ray, behind the close object.
	const float objectShift=RayShift(-2,.8f,1,7,.25f,100),bedShift=RayShift(-2,.8f,1,7,4,100);
	const float objectGap=.25f+objectShift,bedGap=4+bedShift;
	CHECK(std::abs(objectGap-.08f)<0.000001f && std::abs(bedGap-1.5f)<0.000001f);
	const Rgb object=Transmission(objectGap,0,1,3,.75f),bed=Transmission(bedGap,0,1,3,.75f);
	CHECK(object.r>bed.r && object.g>bed.g && object.b>bed.b);
	// Armed control reproduces the old bug: a length() taken after passing the hit becomes
	// positive again and makes the near object *darker* than the farther bed.
	const Rgb crossed=Transmission(std::abs(.25f-2.5f),0,1,3,.75f);
	CHECK(crossed.r<bed.r && crossed.g<bed.g && crossed.b<bed.b);
	CHECK(RayShift(-2,.8f,1,7,.03f,100)==0); // preserve a hit closer than the safety margin
	CHECK(std::abs(RayShift(2,.8f,1,7,10,.3f)-.22f)<0.000001f);
}

TEST(water_optics_shared_hlsl_compiles_and_translates_without_gpu_execution)
{
	std::string hlsl=WaterOpticalModel::GetHlslSource();
	hlsl+="float4 main(float4 position : SV_Position) : SV_Target { float shift=WaterOpticalRayShift(position.x,.8,position.z,7,10,100); return float4(WaterOpticalTransmission(abs(position.x), position.y, position.z, 2.7, 0.75), shift); }\n";
	std::vector<unsigned char> spirv;
	std::string log,msl;
	const bool compiled=SDL3_Compile_HLSL_To_SPIRV(hlsl,false,spirv,log);
	if (!compiled) printf("  water optics HLSL compilation failed: %s\n",log.c_str());
	CHECK(compiled);
	if (!compiled) return;
	const char *spirvVal=getenv("ZH_SPIRV_VAL");
	if (spirvVal && *spirvVal) {
		std::string output;
		const bool valid=validate(spirvVal,spirv,output);
		if (!valid) printf("  water optics SPIR-V validation failed: %s\n",output.c_str());
		CHECK(valid);
	}
	const bool translated=SDL3_Translate_SPIRV_To_MSL(spirv,false,msl,log);
	if (!translated) printf("  water optics MSL translation failed: %s\n",log.c_str());
	CHECK(translated);
}

TEST(water_source_zero_path_keeps_transmitted_scene_independent_of_face_lighting)
{
    using namespace WaterOpticalModel;
    const Rgb scene={.17f,.38f,.61f};
    unsigned oldControls=0;
    for(float river:{0.0f,.25f,1.0f})for(float rawOcean:{0.0f,.5f,1.0f})
    for(float daylight:{0.0f,.5f,1.0f})for(float face:{0.0f,.25f,1.0f})for(float caustic:{.82f,1.0f,1.55f}) {
        const float offshore=rawOcean*(1-river);
        const Rgb source=ScatteringSource(river,offshore,daylight,face);
        const Rgb body=Body(scene,Transmission(0,river,offshore,3,.75f),source,caustic,1);
        for(int c=0;c<3;++c)CHECK(std::abs(opticalChannel(body,c)-opticalChannel(scene,c)*caustic)<1e-6f);
        // The pre-fix face multiplier also scales scene at T=1; an offshore face must expose it.
        if(offshore==1 && face==0) {
            const float oldRed=scene.r*caustic*.4f;
            CHECK(std::abs(body.r-oldRed)>.05f);++oldControls;
        }
    }
    CHECK(oldControls>0);
}

TEST(water_source_face_lighting_does_not_change_transmitted_object_contrast)
{
    using namespace WaterOpticalModel;
    const Rgb a={.72f,.51f,.29f},b={.12f,.08f,.04f};
    for(float path:{.1f,1.0f,4.0f,12.0f})for(float offshore:{0.0f,.4f,1.0f})
    for(float caustic:{.82f,1.0f,1.55f}) {
        const Rgb t=Transmission(path,0,offshore,3,.75f);
        Rgb contrasts[2];
        for(int f=0;f<2;++f) {
            const Rgb source=ScatteringSource(0,offshore,1,float(f));
            const Rgb ba=Body(a,t,source,caustic,1),bb=Body(b,t,source,caustic,1);
            contrasts[f]={ba.r-bb.r,ba.g-bb.g,ba.b-bb.b};
            for(int c=0;c<3;++c) {
                const float expected=(opticalChannel(a,c)-opticalChannel(b,c))*opticalChannel(t,c)*caustic;
                CHECK(std::abs(opticalChannel(contrasts[f],c)-expected)<2e-6f);
            }
        }
        CHECK(std::abs(contrasts[0].r-contrasts[1].r)<2e-6f);
        CHECK(std::abs(contrasts[0].g-contrasts[1].g)<2e-6f);
        CHECK(std::abs(contrasts[0].b-contrasts[1].b)<2e-6f);
    }
}

TEST(water_source_deep_limit_fallback_and_radiance_equilibrium)
{
    using namespace WaterOpticalModel;
    const Rgb scene={.71f,.31f,.13f};
    for(float river:{0.0f,1.0f})for(float ocean:{0.0f,.5f,1.0f})for(float face:{0.0f,.4f,1.0f}) {
        const float offshore=ocean*(1-river);
        const Rgb source=ScatteringSource(river,offshore,.7f,face);
        for(float path:{0.0f,.1f,3.0f,20.0f,100000.0f}) {
            const Rgb t=Transmission(path,river,offshore,3,.75f);
            const Rgb equilibrium=Body(source,t,source,1,1),fallback=Body(scene,t,source,1.5f,0);
            for(int c=0;c<3;++c) {
                CHECK(std::abs(opticalChannel(equilibrium,c)-opticalChannel(source,c))<1e-7f);
                CHECK(std::abs(opticalChannel(fallback,c)-opticalChannel(source,c))<1e-7f);
            }
            if(path>1000) {
                const Rgb deep=Body(scene,t,source,1.5f,1);
                for(int c=0;c<3;++c)CHECK(std::abs(opticalChannel(deep,c)-opticalChannel(source,c))<1e-7f);
            }
        }
    }
}

TEST(water_source_context_is_continuous_and_does_not_gate_offshore_twice)
{
    using namespace WaterOpticalModel;
    const Rgb clear=MaterialSource(0,0),ocean=MaterialSource(0,1),river=MaterialSource(1,0);
    // Migration checks pin existing art endpoints; they intentionally do not derive expected
    // values from the new preset constants, which would allow an accidental retint to self-pass.
    CHECK(std::abs(clear.r-.002f)<1e-8f && std::abs(clear.g-.028f)<1e-8f && std::abs(clear.b-.041f)<1e-8f);
    CHECK(std::abs(ocean.r-.002f)<1e-8f && std::abs(ocean.g-.035f)<1e-8f && std::abs(ocean.b-.075f)<1e-8f);
    CHECK(river.r==clear.r && river.g==clear.g && river.b==clear.b);
    Rgb previous=clear;
    for(int i=1;i<=100;++i) {
        const float offshore=i/100.0f;const Rgb current=MaterialSource(0,offshore);
        for(int c=0;c<3;++c) {
            const float lo=opticalChannel(clear,c),hi=opticalChannel(ocean,c),value=opticalChannel(current,c);
            CHECK(value>=std::min(lo,hi)-1e-8f && value<=std::max(lo,hi)+1e-8f);
            CHECK(std::abs(value-opticalChannel(previous,c))<=std::abs(hi-lo)/100+1e-7f);
        }
        previous=current;
    }
    // Valid continuous context: river .4 + effective offshore .6 = 1. River currently shares
    // the clear source, so changing the river share cannot shrink this already-gated ocean share.
    const Rgb partial=MaterialSource(.4f,.6f),ungated=MaterialSource(0,.6f);
    CHECK(std::abs(partial.r-ungated.r)<1e-8f && std::abs(partial.g-ungated.g)<1e-8f && std::abs(partial.b-ungated.b)<1e-8f);
    const Rgb capped=MaterialSource(1,1);CHECK(capped.r==river.r && capped.g==river.g && capped.b==river.b);
    // Shared source colour does not erase the existing river/standing-water transmission distinction.
    const Rgb riverT=Transmission(5,1,0,3,.75f),poolT=Transmission(5,0,0,3,.75f);
    CHECK(riverT.r>poolT.r && riverT.g>poolT.g && riverT.b>poolT.b);
}

TEST(water_hull_slices_actual_triangles_at_water_not_the_deck_or_collision_box)
{
	using namespace WaterHullModel;
	// An asymmetric, tapered hull widens from 10 below water to 30 at its elevated deck.
	// At z=0 its waterline must be exactly half way: width20, not the deck/collision width30.
	const Point outline[]={{-40,-10},{20,-10},{40,0},{20,10},{-40,10}};
	std::vector<Point> intersections;
	for(unsigned i=0;i<5;++i) {
		const Point a=outline[i],b=outline[(i+1)%5];
		const Point3 lowA={a.x,a.y,-5},lowB={b.x,b.y,-5};
		const Point3 highA={a.x,a.y*3,5},highB={b.x,b.y*3,5};
		CHECK(SliceTriangle(lowA,lowB,highB,intersections));
		CHECK(SliceTriangle(lowA,highB,highA,intersections));
	}
	const Polygon hull=BuildPolygon(intersections);
	// The slanted bow quad is not planar: its authored triangle diagonal intersects at
	// (30,15), outside the straight line joining (40,0) and (20,20). Keep that real mesh kink.
	CHECK(hull.count==6 && hull.error<0.0001f);
	CHECK(std::abs(SignedDistance({30,15},hull))<0.0001f);
	CHECK(std::abs(SignedDistance({0,20},hull))<0.0001f);
	CHECK(std::abs(SignedDistance({0,25},hull)-5)<0.0001f);
	CHECK(std::abs(SignedDistance({0,0},hull)+20)<0.0001f);
	CHECK(SignedDistance({38,10},hull)>0); // pointed bow, not a rectangular collision box
	CHECK(SignedDistance({-38,10},hull)<0); // broad stern retained
	const Polygon wrongDeck=BuildPolygon({{-40,-30},{20,-30},{40,0},{20,30},{-40,30}});
	CHECK(SignedDistance({0,25},wrongDeck)<0); // armed deck-footprint control
	std::vector<Point> dry;
	CHECK(SliceTriangle({0,0,1},{1,0,2},{0,1,3},dry));CHECK(dry.empty());
	CHECK(SliceTriangle({0,0,0},{10,0,0},{0,10,0},dry));CHECK(BuildPolygon(dry).count==3);
}

TEST(water_hull_contours_have_bounded_cost_and_report_simplification_error)
{
	using namespace WaterHullModel;
	std::vector<Point> ellipse;
	for(unsigned i=0;i<256;++i) {
		const float angle=float(i)*6.28318530718f/256;
		ellipse.push_back({283.0f*std::cos(angle),36.0f*std::sin(angle)});
	}
	const Polygon reduced=BuildPolygon(ellipse);
	CHECK(reduced.count==MaxVertices && reduced.error>0 && reduced.error<6);
	float independentlyMeasured=0;
	for(Point p:ellipse) independentlyMeasured=std::max(independentlyMeasured,SignedDistance(p,reduced));
	CHECK(std::abs(independentlyMeasured-reduced.error)<0.0001f);
	for(unsigned i=0;i<reduced.count;++i) {
		const Point p=reduced.points[i];
		CHECK(p.x*p.x/(283*283)+p.y*p.y/(36*36)<1.00001f);
	}
	CHECK(BuildPolygon({{0,0},{1,1},{2,2}}).count==0);
	CHECK(BuildPolygon({{0,0},{0,0},{0,0}}).count==0);
	ellipse.resize(MaxSlicePoints+1);CHECK(BuildPolygon(ellipse).count==0);
	std::vector<Point> full(MaxSlicePoints);
	CHECK(!SliceTriangle({0,0,-1},{1,0,1},{0,1,1},full));CHECK(full.size()==MaxSlicePoints);
	std::vector<Point> invalid;
	CHECK(!SliceTriangle({0,0,0},{1,std::numeric_limits<float>::quiet_NaN(),0},{0,1,0},invalid));
}

TEST(water_hull_packing_rotation_damping_and_distinct_water_levels)
{
	using namespace WaterHullModel;
	Hull hull;
	hull.data[0][0]=3028;hull.data[0][1]=588;hull.data[0][2]=7;hull.data[0][3]=4;
	hull.data[1][0]=0;hull.data[1][1]=-1;hull.data[1][2]=100;hull.data[1][3]=20;
	const Point p[]={{-100,-20},{100,-20},{100,20},{-100,20}};
	for(unsigned i=0;i<4;++i) {hull.data[4+i/2][(i%2)*2]=p[i].x;hull.data[4+i/2][(i%2)*2+1]=p[i].y;}
	CHECK(Damping(hull,3028,588,7)==0);
	CHECK(Damping(hull,3028,588,9)==1); // another water plane cannot flatten this one
	CHECK(Damping(hull,3052.5f,588,7)==0.5f);
	CHECK(Damping(hull,3057,588,7)==1);
	CHECK(Damping(hull,10000,10000,7)==1);
	for(float blend:{0.0f,.25f,.5f,1.0f}) {
		const float atten=1+(Damping(hull,3052.5f,588,7)-1)*blend;
		CHECK(std::abs(atten-(1-.5f*blend))<0.000001f);
	}
	const Point bowWorld={3028,488};
	const Point local=LocalPoint(hull,bowWorld.x,bowWorld.y);
	CHECK(local.x==100 && local.y==0); // authored negative-X bow can become positive local bow
	CHECK(sizeof(Hull)==RegistersPerHull*16);
}

TEST(water_hull_shared_distance_hlsl_compiles_and_translates)
{
	std::string hlsl="cbuffer Hulls : register(b0, space3) { float4 HullData[80]; };\n";
	hlsl+=WaterHullModel::GetHlslSource();
	hlsl+="float4 main(float4 position : SV_Position) : SV_Target { return WaterHullSignedDistance(position.xy,70,12); }\n";
	std::vector<unsigned char> spirv;std::string log,msl;
	const bool compiled=SDL3_Compile_HLSL_To_SPIRV(hlsl,false,spirv,log);
	if(!compiled) printf("  hull HLSL compilation failed: %s\n",log.c_str());
	CHECK(compiled);if(!compiled) return;
	const bool translated=SDL3_Translate_SPIRV_To_MSL(spirv,false,msl,log);
	if(!translated) printf("  hull HLSL MSL translation failed: %s\n",log.c_str());
	CHECK(translated);
}

TEST(water_macro_swell_gradients_and_curvature_include_warp_and_packet_derivatives)
{
	using namespace WaterSurfaceModel;
	unsigned gradientControls=0,curvatureControls=0;
	for(float time:{0.0f,1.7f,12.0f,38.0f}) for(float x:{-80.0f,0.0f,17.0f}) for(float y:{-21.0f,0.0f,43.0f}) {
		const OceanSurface s=EvaluateOceanSwell(x,y,time);
		const float step=.125f;
		const float xp=EvaluateOceanSwell(x+step,y,time).height,xm=EvaluateOceanSwell(x-step,y,time).height;
		const float yp=EvaluateOceanSwell(x,y+step,time).height,ym=EvaluateOceanSwell(x,y-step,time).height;
		CHECK(std::abs((xp-xm)/(2*step)-s.dx)<.0015f);
		CHECK(std::abs((yp-ym)/(2*step)-s.dy)<.0015f);
		CHECK(std::abs((xp+xm+yp+ym-4*s.height)/(step*step)-s.laplacian)<.0015f);
		const float q0=x*.0241f+y*.0157f-time*.23f+.4f,q1=x*-.0173f+y*.0299f-time*.19f+2.1f;
		const float s0=std::sin(q0),s1=std::sin(q1),px=x+5*s0,py=y+4*s1,envelope=.86f+.14f*s0*s1;
		float wrongX=0,wrongY=0,wrongLaplacian=0,height=0;
#define TEST_MACRO_SWELL(kx,ky,amplitude,omega,phase) if((kx)*(kx)+(ky)*(ky)<.18f) { \
		const float p=px*(kx)+py*(ky)-time*(omega)+(phase),sn=std::sin(p),cs=std::cos(p); \
		height+=(amplitude)*(sn+.16f*sn*cs); \
		const float derivative=(amplitude)*(cs+.16f*(cs*cs-sn*sn)); \
		wrongX+=derivative*(kx);wrongY+=derivative*(ky); \
		wrongLaplacian+=(amplitude)*(-sn-.64f*sn*cs)*((kx)*(kx)+(ky)*(ky)); }
		WATER_OCEAN_MODES(TEST_MACRO_SWELL)
#undef TEST_MACRO_SWELL
		CHECK(std::abs(height*envelope-s.height)<.000001f);
		if(std::max(std::abs(wrongX*envelope-s.dx),std::abs(wrongY*envelope-s.dy))>.002f) ++gradientControls;
		if(std::abs(wrongLaplacian*envelope-s.laplacian)>.002f) ++curvatureControls;
	}
	CHECK(gradientControls>=8 && curvatureControls>=8);
}

TEST(foam_environment_positive_quadrature_preserves_energy_without_renormalizing)
{
	using namespace WaterFoamEnvironment;
	const Table white=Build([](Vector){return Rgb{1,1,1};});
	const Table dark=Build([](Vector){return Rgb{0,0,0};});
	// Independent analytic irradiance: for L(w)=1+.5*w.z, E/pi=1+n.z/3.
	// A coloured HDR environment also proves neither radiance nor irradiance is saturated.
	const Table gradient=Build([](Vector w){const double a=1+.5*w.z;return Rgb{2*a,.75*a,3.5*a};});
	double maximumLoss=0,maximumWhiteLoss=0;unsigned finiteBinLosses=0;
	for(unsigned i=0;i<256;++i) {
		const double z=1-2*(i+.5)/256,phase=i*2.399963229728653,r=std::sqrt(1-z*z);
		const Vector n={r*std::cos(phase),r*std::sin(phase),z};
		const Rgb unit=Evaluate(white,n),black=Evaluate(dark,n),colour=Evaluate(gradient,n);
		CHECK(unit.r>=0 && unit.g>=0 && unit.b>=0);
		CHECK(unit.r<=1.00002 && unit.g<=1.00002 && unit.b<=1.00002);
		CHECK(black.r==0 && black.g==0 && black.b==0);
		const double exact=1+z/3,values[3]={colour.r/2,colour.g/.75,colour.b/3.5};
		for(double value:values) {
			CHECK(std::isfinite(value) && value>=0 && value<=exact+.00002);
			const double loss=(exact-value)/exact;if(loss>maximumLoss)maximumLoss=loss;
		}
		if(1-unit.r>maximumWhiteLoss)maximumWhiteLoss=1-unit.r;
		if(1-unit.r>.001)++finiteBinLosses;
	}
	// Retain the measured finite-bin loss. A renormalized approximation could pass
	// positivity while incorrectly increasing the current bright cloud's incident energy.
	CHECK(finiteBinLosses>64);
	CHECK(maximumLoss<.10 && maximumWhiteLoss<.10);
	const Rgb up=Evaluate(white,{0,0,1}),down=Evaluate(white,{0,0,-1});
	CHECK(std::abs(up.r-1)<1e-12 && std::abs(down.r-1)<1e-12);
	printf("  foam environment constant/linear maximum finite-bin losses %.6g/%.6g\n",maximumWhiteLoss,maximumLoss);
}

TEST(foam_environment_canonical_rotation_daylight_and_generated_constants_are_stable)
{
	using namespace WaterFoamEnvironment;
	CHECK(&CachedTable()==&CachedTable());
	CHECK(&HlslSource()==&HlslSource());
	CHECK(HlslSource().find("waterFoamSkyMoments[144]")!=std::string::npos);
	CHECK(HlslSource().find("i<48")!=std::string::npos);
	// Rotate sun azimuth and normal together. Adding the inverse of oceanSky's small
	// sun-axis bias retains exactly the same canonical direction at every orientation.
	const Vector canonical={.3,.4,std::sqrt(.75)};
	const Rgb expected=Evaluate(CachedTable(),canonical);
	for(double angle:{0.0,.2,1.7,3.1,5.4})for(double day:{0.0,.4,1.0}) {
		const double x=std::cos(angle),y=std::sin(angle),scale=.06+.94*day;
		const Vector normal={canonical.x*x-canonical.y*y,canonical.x*y+canonical.y*x,canonical.z};
		const Rgb actual=Evaluate(normal,{x-.00001,y,.45},day);
		CHECK(std::abs(actual.r-expected.r*scale)<1e-12);
		CHECK(std::abs(actual.g-expected.g*scale)<1e-12);
		CHECK(std::abs(actual.b-expected.b*scale)<1e-12);
	}
	// All normals, including overturned FFT tangents, receive nonnegative radiance.
	for(unsigned i=0;i<128;++i) {
		const double z=1-2*(i+.5)/128,phase=i*2.399963229728653,r=std::sqrt(1-z*z);
		const Rgb value=Evaluate(CachedTable(),{r*std::cos(phase),r*std::sin(phase),z});
		CHECK(std::isfinite(value.r) && value.r>=0 && std::isfinite(value.g) && value.g>=0 && std::isfinite(value.b) && value.b>=0);
	}
}

#include "Platform/WaterShoreOverlay.h"
TEST(authored_surf_wrapper_compiles_with_original_fixed_function_fog_and_alpha)
{
    CombinerDescription d={};d.StageCount=2;d.PixelPipeline.FogEnabled=true;
    d.PixelPipeline.AlphaTestEnabled=true;d.PixelPipeline.AlphaFunction=FF_CMP_GREATER;
    for(unsigned i=0;i<2;++i) {
        auto &s=d.Stages[i];s.TextureBound=true;s.TextureCoordinateIndex=i;
        s.ColourOperation=FF_TOP_MODULATE;s.ColourArgument1=FF_TA_TEXTURE;s.ColourArgument2=FF_TA_CURRENT;
        s.AlphaOperation=i==0?FF_TOP_MODULATE:FF_TOP_SELECTARG2;s.AlphaArgument1=FF_TA_TEXTURE;s.AlphaArgument2=FF_TA_CURRENT;
    }
    std::string source;CHECK(CombinerShader_Generate(d,COMBINER_SHADER_TARGET_D3D11,source));
    const std::string original=source;CHECK(WaterShoreOverlay::WrapPixel(source));
    CHECK(source.find("shoreOriginalPixel(input)")!=std::string::npos);
    CHECK(source.find("result.a*=1-shoreReplacement")!=std::string::npos);
    CHECK(SDL3_Shader_Retarget(source,false));
    std::vector<unsigned char> spirv;std::string log,msl;
    const bool compiled=SDL3_Compile_HLSL_To_SPIRV(source,false,spirv,log);
    if(!compiled)printf("  authored surf wrapper: %s\n",log.c_str());CHECK(compiled);if(!compiled)return;
    unsigned samplers=0,buffers=0;SDL3_Shader_Slots(spirv,false,samplers,buffers);
    CHECK(samplers<=8 && buffers==1);
    CHECK(SDL3_Translate_SPIRV_To_MSL(spirv,false,msl,log));
    std::string bad="not a generated combiner";const std::string saved=bad;
    CHECK(!WaterShoreOverlay::WrapPixel(bad));CHECK(bad==saved);
}

// Keep BaseType.h's legacy min/max macros out of the independent shader cases above.
#include "Platform/HazardCloudCore.h"
#include "../GameEngineDevice/Source/PosixDevice/Render/SdlHazardCloudSharedShader.h"
// Shared clouds must retain SDL descriptor bindings on Vulkan and Metal.
TEST(shared_cloud_programs_compile_validate_and_translate)
{
	using HazardCloudShared::ShaderPass;
	const char *validator = getenv("ZH_SPIRV_VAL");
	std::vector<std::pair<std::string, bool>> programs = {
		{HazardCloud::vertexShader(), true},
		{HazardCloud::emberVertexShader(true), true},
		{HazardCloud::emberPixelShader(true), false}
	};
	for (unsigned pass = 0; pass < 5; ++pass)
		programs.push_back({HazardCloudShared::sharedPixelShader(static_cast<ShaderPass>(pass)), false});
	for (const auto &program : programs) {
		std::vector<unsigned char> spirv;
		std::string log, msl;
		const bool compiled = SDL3_Compile_HLSL_To_SPIRV(program.first, program.second, spirv, log);
		CHECK(compiled);
		if (!compiled) { printf("shared cloud shader: %s\n", log.c_str()); continue; }
		if (validator && *validator) CHECK(validate(validator, spirv, log));
		else printf("SKIPPED shared cloud spirv-val: validator unavailable\n");
		CHECK(SDL3_Translate_SPIRV_To_MSL(spirv, program.second, msl, log));
		unsigned samplers = 0, uniforms = 0;
		SDL3_Shader_Slots(spirv, program.second, samplers, uniforms);
		CHECK_EQ(uniforms, program.first == HazardCloud::vertexShader() ? 0u : 1u);
	}
}

TEST(fft_low_tier_preserves_energy_reality_and_filtering)
{
    using namespace WaterFftSpectrum;
    const auto configuration=ForResolution(128);
    CHECK(configuration.resolution==128 && configuration.phases==7 && configuration.levels==8);
    CHECK(ForResolution(0).resolution==256);
    const auto initial=InitialSpectrum(configuration),butterfly=Butterfly(configuration);
    CHECK(initial==InitialSpectrum(configuration));
    CHECK(initial.size()==128*128*3*4 && butterfly.size()==128*7*4);
    const auto &variance=WaterFftFiltering::SpectrumVariance(configuration);
    for(unsigned c=0;c<Cascades;++c) {
        double energy=0,slope=0;const unsigned n=configuration.resolution;
        for(unsigned y=0;y<n;++y) for(unsigned x=0;x<n;++x) {
            const float *h=&initial[((c*n+y)*n+x)*4];
            const float *opposite=&initial[((c*n+(n-y)%n)*n+(n-x)%n)*4];
            CHECK(h[0]==opposite[2] && h[1]==-opposite[3]);
            CHECK(h[2]==opposite[0] && h[3]==-opposite[1]);
            double power=0;for(unsigned i=0;i<4;++i) {CHECK(std::isfinite(h[i]));power+=double(h[i])*h[i];}
            if(x==0 || y==0 || (x==n/2 && y==n/2)) CHECK(power==0);
            const double kx=2*Pi*(int(x)-int(n/2))/Sizes[c],ky=2*Pi*(int(y)-int(n/2))/Sizes[c];
            energy+=power;slope+=power*(kx*kx+ky*ky);
        }
        CHECK(std::abs(std::sqrt(energy)*WorldScale-HeightRms[c])<HeightRms[c]*2e-7);
        CHECK(std::abs(variance[7][c]-slope)<1e-10*(1+slope));
        CHECK(variance[0][c]==0 && variance[8][c]==0);
    }
    CHECK(ButterflyHlsl(configuration).find("/7.0,")!=std::string::npos);
    const std::string filtering=WaterFftFiltering::Source(configuration);
    CHECK(filtering.find("fftLostSlopeVariance[8]")!=std::string::npos);
    CHECK(filtering.find("*128.0/")!=std::string::npos);
    CHECK(filtering.find("level,6)")!=std::string::npos);
}

#if defined(__linux__)
// The SDL water tiers and reflections exist only where the Linux Modern water runtime draws them.
TEST(water_performance_tiers_compile_both_materials_and_preserve_shared_defaults)
{
    struct Restore { WaterPerformanceSettings::Configuration saved=WaterPerformanceSettings::Applied();
        ~Restore() {WaterPerformanceSettings::Applied()=saved;} } restore;
    PixelPipelineDescription pipeline={};
    std::string d3dBaseline;
    CHECK(EngineShader_Pixel_Program(ENGINE_SHADER_WATER_TRAPEZOID,pipeline,d3dBaseline,COMBINER_SHADER_TARGET_D3D11));
    for(unsigned resolution:{128u,256u}) for(unsigned refraction=0;refraction<3;++refraction) for(bool reflections:{false,true}) {
        auto &settings=WaterPerformanceSettings::Applied();settings.activeFftResolution=resolution;
        settings.refraction=WaterPerformanceSettings::Refraction(refraction);settings.sceneReflections=reflections;
        for(auto program:{ENGINE_SHADER_WATER_TRAPEZOID,ENGINE_SHADER_WATER_RIVER}) {
            std::string source,error;std::vector<unsigned char> spirv;
            CHECK(EngineShader_Pixel_Program(program,pipeline,source,COMBINER_SHADER_TARGET_SDL3_GPU));
            const bool compiled=SDL3_Compile_HLSL_To_SPIRV(source,false,spirv,error);
            if(!compiled) printf("water tier %u/%u/%u program%d: %s\n",resolution,refraction,reflections,program,error.c_str());
            CHECK(compiled);
            if (compiled) {
                std::string msl, why; unsigned samplers = 0, buffers = 0;
                SDL3_Shader_Slots(spirv, false, samplers, buffers);
                CHECK(SDL3_Translate_SPIRV_To_MSL(spirv, false, msl, error));
                // Load-only textures need no sampler in Metal's entry point. Every
                // surviving binding still uses the slot recorded by the SDL draw.
                for (int slot : msl_indices(msl, "texture")) CHECK(slot >= 0 && unsigned(slot) < samplers);
                for (int slot : msl_indices(msl, "sampler")) CHECK(slot >= 0 && unsigned(slot) < samplers);
            }
            if(compiled && !reflections && refraction==WaterPerformanceSettings::Off) {
                spvc_context context=NULL;CHECK(spvc_context_create(&context)==SPVC_SUCCESS);
                if(context) {
                    std::vector<SpvId> words(spirv.size()/sizeof(SpvId));memcpy(words.data(),spirv.data(),spirv.size());
                    spvc_parsed_ir ir=NULL;spvc_compiler compiler=NULL;spvc_resources resources=NULL;spvc_set active=NULL;
                    bool reflected=spvc_context_parse_spirv(context,words.data(),words.size(),&ir)==SPVC_SUCCESS
                        && spvc_context_create_compiler(context,SPVC_BACKEND_NONE,ir,SPVC_CAPTURE_MODE_TAKE_OWNERSHIP,&compiler)==SPVC_SUCCESS
                        && spvc_compiler_get_active_interface_variables(compiler,&active)==SPVC_SUCCESS
                        && spvc_compiler_create_shader_resources_for_active_variables(compiler,&resources,active)==SPVC_SUCCESS;
                    const spvc_reflected_resource *images=NULL;size_t count=0;
                    reflected=reflected && spvc_resources_get_resource_list_for_type(resources,SPVC_RESOURCE_TYPE_SEPARATE_IMAGE,&images,&count)==SPVC_SUCCESS;
                    CHECK(reflected);bool depth=false;
                    if(reflected) for(size_t i=0;i<count;++i) {
                        const std::string name=images[i].name;
                        CHECK(name!="Texture1" && name!="Texture5" && name!="Texture7");
                        depth|=name=="Texture6";
                    }
                    CHECK(depth);spvc_context_destroy(context);
                }
            }
        }
        std::string d3d;
        CHECK(EngineShader_Pixel_Program(ENGINE_SHADER_WATER_TRAPEZOID,pipeline,d3d,COMBINER_SHADER_TARGET_D3D11));
        CHECK(d3d==d3dBaseline);
    }
}

TEST(screen_reflection_modes_are_bounded_and_selected_per_draw)
{
	using namespace WaterScreenReflection;
	CHECK(Resolve(0) == Mirror && Resolve(1) == ScreenSpace && Resolve(2) == Hybrid);
	CHECK(Resolve(-1) == Hybrid && Resolve(99) == Hybrid);
	for (int setting = 0; setting < 3; ++setting) CHECK(Resolve(setting, false) == SkyOnly);
	CHECK(UsesMirror(Mirror) && UsesMirror(Hybrid) && !UsesMirror(ScreenSpace) && !UsesMirror(SkyOnly));
	CHECK(Traces(ScreenSpace) && Traces(Hybrid) && !Traces(Mirror) && !Traces(SkyOnly));
	for (auto program : {ENGINE_SHADER_WATER_TRAPEZOID, ENGINE_SHADER_WATER_RIVER}) {
		PixelPipelineDescription pipeline = {}; std::string source;
		CHECK(EngineShader_Pixel_Program(program, pipeline, source, COMBINER_SHADER_TARGET_SDL3_GPU));
		CHECK(source.find("waterScreenReflection(world, view, environmentNormal") != std::string::npos);
		CHECK(source.find("MaterialParameters.w>0.5") != std::string::npos);
		CHECK(source.find("waterMirrorFiltered(reflectionUV+distortion") != std::string::npos);
	}
	CHECK(MirrorSize(Mirror) == 512 && MirrorSize(Hybrid) == 1024);
	CHECK(MirrorSize(Mirror, 0) == 256 && MirrorSize(Hybrid, 0) == 512);
}
#endif // __linux__

TEST(direct3d11_modern_water_source_matches_the_upstream_baseline)
{
	// FNV-1a of the complete generated v3.8.0 source, captured independently from e3e1a898.
	const uint64_t expected[] = {2644328293701272477ull, 15503000995417316327ull};
	unsigned index = 0;
	for (auto program : {ENGINE_SHADER_WATER_TRAPEZOID, ENGINE_SHADER_WATER_RIVER}) {
		PixelPipelineDescription pipeline = {}; std::string source;
		CHECK(EngineShader_Pixel_Program(program, pipeline, source, COMBINER_SHADER_TARGET_D3D11));
		uint64_t hash = 14695981039346656037ull;
		for (unsigned char value : source) { hash ^= value; hash *= 1099511628211ull; }
		CHECK(hash == expected[index++]);
	}
}
