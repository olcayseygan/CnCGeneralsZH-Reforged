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
** The generated programs on SDL3 GPU (decision 7, phase A3c): a description to D3's generator (target
** SDL3_GPU), its HLSL through glslang to SPIR-V and on to what the device takes (sdl3shadercompile), and
** the result kept by D3's key for the description.
**
** Each program carries its binding table: how many texture-sampler slots and uniform buffers it binds
** (SDL3_Shader_Slots), and which texture and which sampler state each slot takes - slot n is stage n's
** texture with stage n's sampler state unless the program's "// SDL3 slot N: texture tT, sampler state
** sS" lines say otherwise (sdl3target.h).
**
** A description the generator refuses, or a program the compiler or the device refuses, is kept too,
** as a refusal with its reason, so it is tried once and counted every time (the design's rule: refused,
** never approximated).
*/

#pragma once

#ifndef SDLPROGRAMCACHE_H
#define SDLPROGRAMCACHE_H

#include <map>
#include <stddef.h>
#include <stdint.h>
#include <string>
#include <unordered_map>
#include <vector>
#include <vector>

struct SDL_GPUDevice;
struct SDL_GPUShader;
struct CombinerDescription;
struct VertexPipelineDescription;

struct SdlProgram
{
	SDL_GPUShader *Shader;				///< null for a refusal
	std::string Key;					///< D3's key for the description
	std::string Refusal;				///< why, for a refusal
	unsigned int SamplerSlots;			///< texture-sampler pairs to bind, 0 .. SamplerSlots - 1
	unsigned int UniformBuffers;
	std::vector<int> SlotTexture;		///< per slot: which stage's texture
	std::vector<int> SlotSampler;		///< per slot: which stage's sampler state
	unsigned int Uses;
};

class SdlProgramCache
{
public:
	explicit SdlProgramCache(SDL_GPUDevice *device);
	~SdlProgramCache();

	/// The program for the description, made the first time it is asked for.  Never null: a refusal
	/// comes back with a null Shader and its Refusal.
	const SdlProgram &Vertex_Program(const VertexPipelineDescription &description);
	/// Water-only GPU displacement, retaining the generated transform/fog/shroud path.
	const SdlProgram &Water_Vertex_Program(const VertexPipelineDescription &description, bool waveHeading = false);
	const SdlProgram &Pixel_Program(const CombinerDescription &description);
	const SdlProgram &Sun_Pixel_Program(const CombinerDescription &description, int engine);
	const SdlProgram &Shore_Pixel_Program(const CombinerDescription &description);
	/// One of the engine's own programs (A3e), from D3's transcription (engineshader.cpp), keyed as the
	/// Direct3D 11 backend keys it: the name, and for a pixel program the alpha test and fog written into
	/// it.  A refusal for a program with no transcription, or of the other stage.
	const SdlProgram &Engine_Vertex_Program(int program);
	const SdlProgram &Engine_Pixel_Program(int program, const struct PixelPipelineDescription &pipeline, bool waveHeading = false);

	unsigned int Programs_Built() const { return Built; }
	/// The key a program was made under, for a refusal's log line: "" for a shader this cache did not make.
	std::string Key_Of(const SDL_GPUShader *shader) const;
	unsigned int Programs_Refused() const { return Refused; }
	double Milliseconds_Compiling() const { return CompileMilliseconds; }

private:
	void Dump_Program(const std::string &key, bool vertex_stage, const std::string &hlsl);
	const SdlProgram &Make(std::map<std::string, SdlProgram> &cache, const std::string &key, bool generated,
		const std::string &hlsl, bool vertex_stage);

	/// The programs by the bytes of the description that asked for them (PERF1: D3's key strings are made
	/// with snprintf, and making both for every draw was a fifth of the main thread).  Descriptions are
	/// built with memset first, so equal ones are equal byte for byte, padding included; an unequal
	/// padding byte could only cost a miss, which goes to the key and finds the same program.  The last
	/// description found is checked first, and the rest by a hash of their bytes.
	class ByBytes
	{
	public:
		SdlProgram *Find(const void *bytes, size_t size);
		void Add(const void *bytes, size_t size, SdlProgram *program);

	private:
		std::unordered_map<uint64_t, std::vector<std::pair<std::vector<uint8_t>, SdlProgram *> > > Table;
		std::vector<uint8_t> LastBytes;
		SdlProgram *Last = NULL;
		size_t Entries = 0;
	};
	ByBytes VertexByBytes, PixelByBytes, EngineVertexByBytes, EnginePixelByBytes, SunPixelByBytes, WaterVertexByBytes,
		ShorePixelByBytes;

	SDL_GPUDevice *Device;
	std::map<std::string, SdlProgram> VertexPrograms;
	std::map<std::string, SdlProgram> PixelPrograms;
	unsigned int Built;
	unsigned int Refused;
	double CompileMilliseconds;
};

/// The slot table from a program's HLSL: slot n is texture n and sampler n, except where a
/// "// SDL3 slot N: texture tT, sampler state sS" line says otherwise.  Exposed for its test.
void Sdl_Read_Slot_Lines(const std::string &hlsl, unsigned int slots, std::vector<int> &textures, std::vector<int> &samplers);

#endif // SDLPROGRAMCACHE_H
