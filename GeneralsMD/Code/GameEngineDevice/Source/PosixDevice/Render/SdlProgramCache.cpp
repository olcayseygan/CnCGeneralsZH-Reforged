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

// The generated programs on SDL3 GPU (decision 7, phase A3c).  See SdlProgramCache.h.

#include "SdlProgramCache.h"
#include "SdlCreationLog.h"
#include "sdl3shadercompile.h"
#include "engineshader.h"
#include "ffshader.h"
#include "ffvertex.h"
#include "sdl3target.h"
#include "Platform/WaterFftSampling.h"
#include "Platform/WaterPerformanceSettings.h"
#include "Platform/WaterShoreOverlay.h"

#include <SDL3/SDL.h>

#include <stdio.h>
#include "SdlSunShadowDebug.h"
#include <stdlib.h>
#include <string.h>

void Sdl_Read_Slot_Lines(const std::string &hlsl, unsigned int slots, std::vector<int> &textures, std::vector<int> &samplers)
{
	textures.assign(slots, 0);
	samplers.assign(slots, 0);
	for (unsigned int slot = 0; slot < slots; ++slot) {
		textures[slot] = (int)slot;
		samplers[slot] = (int)slot;
	}
	static const char PREFIX[] = "// SDL3 slot ";
	for (size_t at = hlsl.find(PREFIX); at != std::string::npos; at = hlsl.find(PREFIX, at + 1)) {
		int slot = -1, texture = -1, sampler = -1;
		if (sscanf(hlsl.c_str() + at, "// SDL3 slot %d: texture t%d, sampler state s%d", &slot, &texture, &sampler) == 3
			&& slot >= 0 && (unsigned int)slot < slots) {
			textures[slot] = texture;
			samplers[slot] = sampler;
		}
	}
}

SdlProgramCache::SdlProgramCache(SDL_GPUDevice *device) :
	Device(device),
	Built(0),
	Refused(0),
	CompileMilliseconds(0.0)
{
}

SdlProgramCache::~SdlProgramCache()
{
	for (int stage = 0; stage < 2; ++stage) {
		std::map<std::string, SdlProgram> &cache = stage == 0 ? VertexPrograms : PixelPrograms;
		for (std::map<std::string, SdlProgram>::iterator it = cache.begin(); it != cache.end(); ++it) {
			if (it->second.Shader != NULL) {
				SDL_ReleaseGPUShader(Device, it->second.Shader);
			}
		}
	}
}

const SdlProgram &SdlProgramCache::Make(std::map<std::string, SdlProgram> &cache, const std::string &key, bool generated,
	const std::string &hlsl, bool vertex_stage)
{
	SdlProgram &program = cache[key];
	program.Shader = NULL;
	program.Key = key;
	program.SamplerSlots = 0;
	program.UniformBuffers = 0;
	program.Uses = 1;
	if (!generated) {
		program.Refusal = "the generator refuses the description";
	}
	else {
		const Uint64 start = SDL_GetTicksNS();
		std::vector<unsigned char> spirv;
		std::string log;
		Dump_Program(key, vertex_stage, hlsl);
		if (!SDL3_Compile_HLSL_To_SPIRV(hlsl, vertex_stage, spirv, log)) {
			program.Refusal = "glslang: " + log;
		}
		else {
			SDL3_Shader_Slots(spirv, vertex_stage, program.SamplerSlots, program.UniformBuffers);
			program.Shader = SDL3_Create_Shader(Device, spirv, vertex_stage, log);
			if (program.Shader == NULL) {
				program.Refusal = "the device: " + log;
			}
		}
		CompileMilliseconds += (double)(SDL_GetTicksNS() - start) / 1.0e6;
		if (Sdl_Creation_Log_Asked()) {
			Sdl_Creation_Log(vertex_stage ? "vprogram" : "pprogram", (double)start / 1.0e6,
				(double)(SDL_GetTicksNS() - start) / 1.0e6, key.substr(0, 90).c_str());
		}
	}
	if (program.Shader == NULL) {
		++Refused;
		// Once per key: the draws that ask for it again are counted by the caller, not logged.
		fprintf(stderr, "SdlProgramCache: %s program %s refused: %s\n", vertex_stage ? "vertex" : "pixel", key.c_str(),
			program.Refusal.c_str());
		return program;
	}
	Sdl_Read_Slot_Lines(hlsl, program.SamplerSlots, program.SlotTexture, program.SlotSampler);
	++Built;
	return program;
}

// ZH_GPU_DUMP_PROGRAMS=<folder>: every program as the generator wrote it (N-v.hlsl, N-p.hlsl), with
// programs.txt naming each N's key.  For reading what a backend refuses; off unless asked for.
void SdlProgramCache::Dump_Program(const std::string &key, bool vertex_stage, const std::string &hlsl)
{
	static const char *const folder = getenv("ZH_GPU_DUMP_PROGRAMS");
	if (folder == NULL || folder[0] == '\0') {
		return;
	}
	static unsigned dumped = 0;
	const unsigned index = ++dumped;
	const char stage = vertex_stage ? 'v' : 'p';
	char path[1024];
	snprintf(path, sizeof(path), "%s/%u-%c.hlsl", folder, index, stage);
	if (FILE *file = fopen(path, "wb")) {
		fwrite(hlsl.data(), 1, hlsl.size(), file);
		fclose(file);
	}
	snprintf(path, sizeof(path), "%s/programs.txt", folder);
	if (FILE *file = fopen(path, "ab")) {
		fprintf(file, "%u %c %s\n", index, stage, key.c_str());
		fclose(file);
	}
}

std::string SdlProgramCache::Key_Of(const SDL_GPUShader *shader) const
{
	for (int stage = 0; stage < 2; ++stage) {
		const std::map<std::string, SdlProgram> &cache = stage == 0 ? VertexPrograms : PixelPrograms;
		for (std::map<std::string, SdlProgram>::const_iterator it = cache.begin(); it != cache.end(); ++it) {
			if (it->second.Shader == shader) {
				return it->first;
			}
		}
	}
	return std::string();
}

static uint64_t bytes_hash(const void *bytes, size_t size)
{
	const uint8_t *b = (const uint8_t *)bytes;
	uint64_t hash = 14695981039346656037ull;
	for (size_t i = 0; i < size; ++i) {
		hash = (hash ^ b[i]) * 1099511628211ull;
	}
	return hash;
}

SdlProgram *SdlProgramCache::ByBytes::Find(const void *bytes, size_t size)
{
	if (Last != NULL && LastBytes.size() == size && memcmp(&LastBytes[0], bytes, size) == 0) {
		return Last;
	}
	std::unordered_map<uint64_t, std::vector<std::pair<std::vector<uint8_t>, SdlProgram *> > >::iterator bucket =
		Table.find(bytes_hash(bytes, size));
	if (bucket == Table.end()) {
		return NULL;
	}
	for (size_t i = 0; i < bucket->second.size(); ++i) {
		const std::vector<uint8_t> &kept = bucket->second[i].first;
		if (kept.size() == size && memcmp(&kept[0], bytes, size) == 0) {
			LastBytes = kept;
			Last = bucket->second[i].second;
			return Last;
		}
	}
	return NULL;
}

void SdlProgramCache::ByBytes::Add(const void *bytes, size_t size, SdlProgram *program)
{
	const uint8_t *b = (const uint8_t *)bytes;
	// A description whose padding varied would add itself on every draw: past this many, the lookup stops
	// growing and such draws go to the key, as they did before.
	const size_t MAXIMUM_ENTRIES = 16384;
	if (Entries < MAXIMUM_ENTRIES) {
		Table[bytes_hash(bytes, size)].push_back(std::make_pair(std::vector<uint8_t>(b, b + size), program));
		++Entries;
	}
	LastBytes.assign(b, b + size);
	Last = program;
}

const SdlProgram &SdlProgramCache::Vertex_Program(const VertexPipelineDescription &description)
{
	if (SdlProgram *found = VertexByBytes.Find(&description, sizeof(description))) {
		++found->Uses;
		return *found;
	}
	const std::string key = VertexShader_Key(description);
	std::map<std::string, SdlProgram>::iterator existing = VertexPrograms.find(key);
	if (existing != VertexPrograms.end()) {
		++existing->second.Uses;
		VertexByBytes.Add(&description, sizeof(description), &existing->second);
		return existing->second;
	}
	std::string hlsl;
	const bool generated = VertexShader_Generate(description, VERTEX_SHADER_TARGET_SDL3_GPU, hlsl);
	const SdlProgram &made = Make(VertexPrograms, key, generated, hlsl, true);
	VertexByBytes.Add(&description, sizeof(description), &VertexPrograms[key]);
	return made;
}

const SdlProgram &SdlProgramCache::Water_Vertex_Program(const VertexPipelineDescription &description)
{
	if (SdlProgram *found = WaterVertexByBytes.Find(&description, sizeof(description))) {
		++found->Uses;
		return *found;
	}
	const std::string key = "water-fft:" + VertexShader_Key(description);
	std::map<std::string, SdlProgram>::iterator existing = VertexPrograms.find(key);
	if (existing != VertexPrograms.end()) {
		++existing->second.Uses;
		WaterVertexByBytes.Add(&description, sizeof(description), &existing->second);
		return existing->second;
	}
	std::string hlsl;
	const bool generated = !description.LightingEnabled
		&& VertexShader_Generate(description, VERTEX_SHADER_TARGET_D3D11, hlsl)
		&& WaterFftSampling::WrapVertex(hlsl,true) && SDL3_Shader_Retarget(hlsl, true);
	const SdlProgram &made = Make(VertexPrograms, key, generated, hlsl, true);
	WaterVertexByBytes.Add(&description, sizeof(description), &VertexPrograms[key]);
	return made;
}

const SdlProgram &SdlProgramCache::Engine_Vertex_Program(int program)
{
	if (SdlProgram *found = EngineVertexByBytes.Find(&program, sizeof(program))) {
		++found->Uses;
		return *found;
	}
	const EngineShaderProgram engine = (EngineShaderProgram)program;
	const std::string key = std::string("engine:") + EngineShader_Name(engine);
	std::map<std::string, SdlProgram>::iterator existing = VertexPrograms.find(key);
	if (existing != VertexPrograms.end()) {
		++existing->second.Uses;
		EngineVertexByBytes.Add(&program, sizeof(program), &existing->second);
		return existing->second;
	}
	std::string hlsl;
	const bool written = EngineShader_Vertex_Program(engine, hlsl, VERTEX_SHADER_TARGET_SDL3_GPU);
	const SdlProgram &made = Make(VertexPrograms, key, written, hlsl, true);
	EngineVertexByBytes.Add(&program, sizeof(program), &VertexPrograms[key]);
	return made;
}

const SdlProgram &SdlProgramCache::Engine_Pixel_Program(int program, const PixelPipelineDescription &pipeline)
{
	// The program and the pipeline description, side by side, as the lookup's bytes.
	struct {
		int Program;
		PixelPipelineDescription Pipeline;
		unsigned Resolution, Refraction, Reflections;
	} lookup;
	memset(&lookup, 0, sizeof(lookup));
	lookup.Program = program;
	const bool water = program == ENGINE_SHADER_WATER_TRAPEZOID || program == ENGINE_SHADER_WATER_RIVER;
	if (water) {
		const auto &settings = WaterPerformanceSettings::Applied();
		lookup.Resolution = settings.activeFftResolution;
		lookup.Refraction = unsigned(settings.refraction);
		lookup.Reflections = settings.sceneReflections ? 1 : 0;
	}
	memcpy(&lookup.Pipeline, &pipeline, sizeof(pipeline));	// padding and all, as the caller built it
	if (SdlProgram *found = EnginePixelByBytes.Find(&lookup, sizeof(lookup))) {
		++found->Uses;
		return *found;
	}
	const EngineShaderProgram engine = (EngineShaderProgram)program;
	const std::string key = std::string("engine:") + EngineShader_Name(engine) + CombinerShader_Pipeline_Key(pipeline)
		+ (water ? ":water:" + std::to_string(lookup.Resolution) + ":" + std::to_string(lookup.Refraction)
			+ ":" + std::to_string(lookup.Reflections) : std::string());
	std::map<std::string, SdlProgram>::iterator existing = PixelPrograms.find(key);
	if (existing != PixelPrograms.end()) {
		++existing->second.Uses;
		EnginePixelByBytes.Add(&lookup, sizeof(lookup), &existing->second);
		return existing->second;
	}
	std::string hlsl;
	const bool written = EngineShader_Pixel_Program(engine, pipeline, hlsl, COMBINER_SHADER_TARGET_SDL3_GPU);
	const SdlProgram &made = Make(PixelPrograms, key, written, hlsl, false);
	EnginePixelByBytes.Add(&lookup, sizeof(lookup), &PixelPrograms[key]);
	return made;
}

const SdlProgram &SdlProgramCache::Pixel_Program(const CombinerDescription &description)
{
	if (SdlProgram *found = PixelByBytes.Find(&description, sizeof(description))) {
		++found->Uses;
		return *found;
	}
	const std::string key = CombinerShader_Key(description);
	std::map<std::string, SdlProgram>::iterator existing = PixelPrograms.find(key);
	if (existing != PixelPrograms.end()) {
		++existing->second.Uses;
		PixelByBytes.Add(&description, sizeof(description), &existing->second);
		return existing->second;
	}
	std::string hlsl;
	const bool generated = CombinerShader_Generate(description, COMBINER_SHADER_TARGET_SDL3_GPU, hlsl);
	if (description.ShadowReceiving && Sdl_Sun_Shadow_Fault("receiver-program"))
		hlsl += "\n#error injected sun receiver compilation failure\n";
	const SdlProgram &made = Make(PixelPrograms, key, generated, hlsl, false);
	PixelByBytes.Add(&description, sizeof(description), &PixelPrograms[key]);
	return made;
}

// Preserve the shared alpha/discard logic but expose no fragment color output on a depth-only pass.
const SdlProgram &SdlProgramCache::Sun_Pixel_Program(const CombinerDescription &description, int engine)
{
    struct { CombinerDescription Description; int Engine; } lookup = {};
    memcpy(&lookup.Description, &description, sizeof(description));
    lookup.Engine = engine;
    if (SdlProgram *found = SunPixelByBytes.Find(&lookup, sizeof(lookup))) {
        ++found->Uses;
        return *found;
    }
    const bool fixed = engine == ENGINE_SHADER_NONE;
    const std::string key = "sun-caster:" + (fixed ? CombinerShader_Key(description)
        : std::string(EngineShader_Name((EngineShaderProgram)engine)) + CombinerShader_Pipeline_Key(description.PixelPipeline));
    auto existing = PixelPrograms.find(key);
    if (existing != PixelPrograms.end()) {
        ++existing->second.Uses;
        SunPixelByBytes.Add(&lookup, sizeof(lookup), &existing->second);
        return existing->second;
    }
    std::string hlsl;
    bool generated = fixed ? CombinerShader_Generate(description, COMBINER_SHADER_TARGET_SDL3_GPU, hlsl)
        : EngineShader_Pixel_Program((EngineShaderProgram)engine, description.PixelPipeline, hlsl, COMBINER_SHADER_TARGET_SDL3_GPU);
    const std::string entry = "float4 main(Input input) : SV_Target";
    const size_t at = hlsl.find(entry);
    generated = generated && at != std::string::npos;
    if (generated) {
        hlsl.replace(at, entry.size(), "float4 sunCasterPixel(Input input)");
        hlsl += "\nvoid main(Input input) { sunCasterPixel(input); }\n";
    }
    const SdlProgram &made = Make(PixelPrograms, key, generated, hlsl, false);
    SunPixelByBytes.Add(&lookup, sizeof(lookup), &PixelPrograms[key]);
    return made;
}

// Only an explicit authored-track draw tag may select this optional variant.
const SdlProgram &SdlProgramCache::Shore_Pixel_Program(const CombinerDescription &description)
{
	if (SdlProgram *found=ShorePixelByBytes.Find(&description,sizeof(description))) {
		++found->Uses;return *found;
	}
	const std::string key="shore-overlay:"+CombinerShader_Key(description);
	std::string hlsl;
	bool generated=!description.ShadowReceiving && !description.SoftParticle && !description.DrawEffect
		&& !description.SmokeGlow && description.StageCount<=2
		&& CombinerShader_Generate(description,COMBINER_SHADER_TARGET_D3D11,hlsl)
		&& WaterShoreOverlay::WrapPixel(hlsl);
	if(generated) generated=SDL3_Shader_Retarget(hlsl,false);
	const SdlProgram &made=Make(PixelPrograms,key,generated,hlsl,false);
	ShorePixelByBytes.Add(&description,sizeof(description),&PixelPrograms[key]);
	return made;
}
