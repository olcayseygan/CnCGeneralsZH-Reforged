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
** A generated program on its way to SDL3's GPU API, off Windows (decision 4).
**
** The generators write HLSL for the SDL3_GPU target (sdl3target.h).  From there:
**
**   SDL3_Compile_HLSL_To_SPIRV   HLSL to SPIR-V, which is what SDL's Vulkan backend takes.  Today
**                                through glslang's HLSL front end.
**   SDL3_Translate_SPIRV_To_MSL  SPIR-V to MSL text, through SDL_shadercross (SPIRV-Cross), for
**                                checking and dumping.
**   SDL3_Create_Shader           the SDL_GPUShader for a device: the SPIR-V itself on Vulkan, MSL
**                                compiled by the driver on Metal.
**
** The first is the only place in the tree that names the HLSL front end, on purpose.  glslang has
** deprecated its HLSL front end (KhronosGroup/glslang#4210, removal not before about 2027-10), and
** decision 4 keeps a way out - DXC through SDL_shadercross, then Slang.  Taking it is this one
** function and its link line.  Everything that compiles a program, test_shader_sdl included, comes
** through here.
**
** POSIX only.  Windows compiles the D3D11 target with d3dcompiler_47.dll.
*/

#ifndef SDL3SHADERCOMPILE_H
#define SDL3SHADERCOMPILE_H

#include <string>
#include <vector>

struct SDL_GPUDevice;
struct SDL_GPUShader;

// Explicit stages for compute/storage clients. The legacy bool graphics API stays unchanged.
enum class SDL3_ShaderStage { Vertex, Fragment, Compute };
bool SDL3_Compile_HLSL_To_SPIRV(const std::string &hlsl, SDL3_ShaderStage stage,
	std::vector<unsigned char> &spirv, std::string &log);
bool SDL3_Translate_SPIRV_To_MSL(const std::vector<unsigned char> &spirv, SDL3_ShaderStage stage,
	std::string &msl, std::string &log);

// False with the compiler's log when glslang refuses the text.  The entry point is main, which is
// what every generator writes.
bool SDL3_Compile_HLSL_To_SPIRV(const std::string & hlsl, bool vertex_stage, std::vector<unsigned char> & spirv,
	std::string & log);

// False with SDL_shadercross's error.  MSL renames an entry point called main, so the program's
// function is main0 in the text this returns.
bool SDL3_Translate_SPIRV_To_MSL(const std::vector<unsigned char> & spirv, bool vertex_stage, std::string & msl,
	std::string & log);

// The slots a program's SPIR-V binds, as SDL counts them: one more than the highest texture slot and
// the highest constant buffer slot it uses.  Not how many it uses: glslang drops a declared texture
// the program never reads, so a program reading t0 and t5 uses two and needs six, and SDL binds
// slots 0 to n - 1.  The backend binds that many texture-sampler pairs, filling the unused ones.
void SDL3_Shader_Slots(const std::vector<unsigned char> & spirv, bool vertex_stage, unsigned & samplers,
	unsigned & uniform_buffers);

// NULL with the reason when the device refuses the program.  Created with SDL3_Shader_Slots' counts.
SDL_GPUShader * SDL3_Create_Shader(SDL_GPUDevice * device, const std::vector<unsigned char> & spirv,
	bool vertex_stage, std::string & log);

#endif
