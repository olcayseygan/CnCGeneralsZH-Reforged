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

#include "sdl3shadercompile.h"

#include <SDL3/SDL.h>
#include <SDL3_shadercross/SDL_shadercross.h>
#include <glslang/Include/glslang_c_interface.h>
#include <glslang/Public/resource_limits_c.h>

#include <string.h>

#include <mutex>

namespace {

// Both libraries want one process-wide initialisation and neither says what happens on a second.
std::once_flag glslang_ready;
std::once_flag shadercross_ready;
bool shadercross_ok = false;

void start_shadercross()
{
	std::call_once(shadercross_ready, []() { shadercross_ok = SDL_ShaderCross_Init(); });
}

SDL_ShaderCross_SPIRV_Info spirv_info(const std::vector<unsigned char> & spirv, bool vertex_stage)
{
	SDL_ShaderCross_SPIRV_Info info;
	SDL_zero(info);
	info.bytecode = spirv.empty() ? NULL : &spirv[0];
	info.bytecode_size = spirv.size();
	info.entrypoint = "main";
	info.shader_stage = vertex_stage ? SDL_SHADERCROSS_SHADERSTAGE_VERTEX : SDL_SHADERCROSS_SHADERSTAGE_FRAGMENT;
	return info;
}

} // namespace

// The one place the HLSL front end is named (see the header).  glslang's defaults are what the
// D-spike measured all 49 of the game's programs through: Vulkan 1.0, SPIR-V 1.0, default limits.
bool SDL3_Compile_HLSL_To_SPIRV(const std::string & hlsl, bool vertex_stage, std::vector<unsigned char> & spirv,
	std::string & log)
{
	std::call_once(glslang_ready, []() { glslang_initialize_process(); });
	spirv.clear();
	log.clear();

	const glslang_stage_t stage = vertex_stage ? GLSLANG_STAGE_VERTEX : GLSLANG_STAGE_FRAGMENT;
	glslang_input_t input;
	memset(&input, 0, sizeof(input));
	input.language = GLSLANG_SOURCE_HLSL;
	input.stage = stage;
	input.client = GLSLANG_CLIENT_VULKAN;
	input.client_version = GLSLANG_TARGET_VULKAN_1_0;
	input.target_language = GLSLANG_TARGET_SPV;
	input.target_language_version = GLSLANG_TARGET_SPV_1_0;
	input.code = hlsl.c_str();
	input.default_version = 100;
	input.default_profile = GLSLANG_NO_PROFILE;
	input.messages = GLSLANG_MSG_READ_HLSL_BIT;
	input.resource = glslang_default_resource();

	glslang_shader_t * shader = glslang_shader_create(&input);
	glslang_shader_set_entry_point(shader, "main");
	bool ok = glslang_shader_preprocess(shader, &input) && glslang_shader_parse(shader, &input);
	if (!ok) {
		log = glslang_shader_get_info_log(shader);
		glslang_shader_delete(shader);
		return false;
	}

	glslang_program_t * program = glslang_program_create();
	glslang_program_add_shader(program, shader);
	ok = glslang_program_link(program, GLSLANG_MSG_SPV_RULES_BIT | GLSLANG_MSG_VULKAN_RULES_BIT | GLSLANG_MSG_READ_HLSL_BIT);
	if (ok) {
		glslang_program_SPIRV_generate(program, stage);
		const size_t words = glslang_program_SPIRV_get_size(program);
		const unsigned char * bytes = reinterpret_cast<const unsigned char *>(glslang_program_SPIRV_get_ptr(program));
		spirv.assign(bytes, bytes + words * 4);
		const char * messages = glslang_program_SPIRV_get_messages(program);
		if (messages != NULL) log = messages;
		ok = !spirv.empty();
	}
	else {
		log = glslang_program_get_info_log(program);
	}
	glslang_program_delete(program);
	glslang_shader_delete(shader);
	return ok;
}

bool SDL3_Translate_SPIRV_To_MSL(const std::vector<unsigned char> & spirv, bool vertex_stage, std::string & msl,
	std::string & log)
{
	start_shadercross();
	msl.clear();
	log.clear();
	if (!shadercross_ok) {
		log = SDL_GetError();
		return false;
	}
	const SDL_ShaderCross_SPIRV_Info info = spirv_info(spirv, vertex_stage);
	char * text = static_cast<char *>(SDL_ShaderCross_TranspileMSLFromSPIRV(&info));
	if (text == NULL) {
		log = SDL_GetError();
		return false;
	}
	msl = text;
	SDL_free(text);
	return true;
}

void SDL3_Shader_Slots(const std::vector<unsigned char> & spirv, bool vertex_stage, unsigned & samplers,
	unsigned & uniform_buffers)
{
	// SDL's sets: a vertex program's textures in 0 and constant buffers in 1, a pixel program's in 2
	// and 3 (SDL_CreateGPUShader).  Read straight from the OpDecorate instructions: the reflection
	// SDL_shadercross offers counts what is used, and the count is what SDL cannot take.
	const unsigned texture_set = vertex_stage ? 0 : 2;
	const unsigned uniform_set = vertex_stage ? 1 : 3;
	const size_t words = spirv.size() / 4;
	std::vector<unsigned> set_of(0), binding_of(0);
	const unsigned char * bytes = spirv.empty() ? NULL : &spirv[0];
	unsigned id_bound = words > 3 ? (unsigned)(bytes[12] | (bytes[13] << 8) | (bytes[14] << 16) | ((unsigned)bytes[15] << 24)) : 0;
	set_of.assign(id_bound, ~0u);
	binding_of.assign(id_bound, ~0u);
	for (size_t at = 5; at < words;) {
		const unsigned char * w = bytes + at * 4;
		const unsigned word = w[0] | (w[1] << 8) | (w[2] << 16) | ((unsigned)w[3] << 24);
		const unsigned opcode = word & 0xffff, length = word >> 16;
		if (length == 0) break;
		if (opcode == 71 && length >= 4 && at + 3 < words) {	// OpDecorate target decoration value
			const unsigned char * a = bytes + (at + 1) * 4;
			const unsigned char * b = bytes + (at + 2) * 4;
			const unsigned char * c = bytes + (at + 3) * 4;
			const unsigned target = a[0] | (a[1] << 8) | (a[2] << 16) | ((unsigned)a[3] << 24);
			const unsigned decoration = b[0] | (b[1] << 8) | (b[2] << 16) | ((unsigned)b[3] << 24);
			const unsigned value = c[0] | (c[1] << 8) | (c[2] << 16) | ((unsigned)c[3] << 24);
			if (target < id_bound && decoration == 34) set_of[target] = value;		// DescriptorSet
			if (target < id_bound && decoration == 33) binding_of[target] = value;	// Binding
		}
		at += length;
	}
	samplers = 0;
	uniform_buffers = 0;
	for (unsigned id = 0; id < id_bound; ++id) {
		if (binding_of[id] == ~0u) continue;
		if (set_of[id] == texture_set && binding_of[id] + 1 > samplers) samplers = binding_of[id] + 1;
		if (set_of[id] == uniform_set && binding_of[id] + 1 > uniform_buffers) uniform_buffers = binding_of[id] + 1;
	}
}

SDL_GPUShader * SDL3_Create_Shader(SDL_GPUDevice * device, const std::vector<unsigned char> & spirv,
	bool vertex_stage, std::string & log)
{
	start_shadercross();
	log.clear();
	if (!shadercross_ok) {
		log = SDL_GetError();
		return NULL;
	}
	const SDL_ShaderCross_SPIRV_Info info = spirv_info(spirv, vertex_stage);
	SDL_ShaderCross_GraphicsShaderResourceInfo resources;
	SDL_zero(resources);
	SDL3_Shader_Slots(spirv, vertex_stage, resources.num_samplers, resources.num_uniform_buffers);
	// shadercross leaves an error from its start-up (it looks for libraries it may not need) where a
	// later failure that sets none would report it.  Cleared, so a refusal says its own reason.
	SDL_ClearError();
	SDL_GPUShader *shader = NULL;
	if (SDL_GetGPUShaderFormats(device) & SDL_GPU_SHADERFORMAT_MSL) {
		// shadercross re-reflects MSL and ignores the supplied binding counts. In
		// particular, Load-only textures have no sampler variables, so SDL binds
		// none of their images. Use our actual slot extents for Metal as on Vulkan.
		std::string msl;
		if (!SDL3_Translate_SPIRV_To_MSL(spirv, vertex_stage, msl, log)) return NULL;
		// Match the engine FP contract: physical water must be identical across shader variants.
		msl = "#pragma clang fp contract(off) reassociate(off)\n"+msl;
		SDL_GPUShaderCreateInfo create = {};
		create.code = (const Uint8 *)msl.c_str(); create.code_size = msl.size()+1;
		// All input entry points here are main, cleansed by SPIRV-Cross to main0.
		create.entrypoint = "main0"; create.format = SDL_GPU_SHADERFORMAT_MSL;
		create.stage = vertex_stage ? SDL_GPU_SHADERSTAGE_VERTEX : SDL_GPU_SHADERSTAGE_FRAGMENT;
		create.num_samplers = resources.num_samplers;
		create.num_uniform_buffers = resources.num_uniform_buffers;
		shader = SDL_CreateGPUShader(device, &create);
	} else {
		shader = SDL_ShaderCross_CompileGraphicsShaderFromSPIRV(device, &info, &resources, 0);
	}
	if (shader == NULL) {
		log = SDL_GetError();
		if (log.empty()) log = "refused, and neither SDL nor SDL_shadercross said why";
	}
	return shader;
}
