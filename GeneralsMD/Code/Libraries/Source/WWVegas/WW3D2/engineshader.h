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

/*
** The engine's own shaders, rewritten as HLSL.
**
** ffvertex and ffshader generate a program out of fixed-function state.  Six draws a frame do not
** use fixed-function state at all: they load a shipped .vso or .pso out of the big archives and
** bind it, and the state that would describe them is not set.  Those are the last draws the
** Direct3D 11 backend refuses, and no generator can write them, because the program is the shipped
** bytecode and nothing else says what it does.
**
** So it is transcribed by hand, once, from the assembly d3d8shadertranslate hands back.  The .vso
** files ship compiled and their source is not in this tree; -dx11dump writes what the translator
** disassembled next to the generated programs, and that disassembly is what each function below is
** a line-for-line reading of.  The reading is written into the comment above each one so the next
** person can check it against the same dump rather than decoding vs_1_1 again.
**
** The output structure is ffvertex's, member for member.  Shader model 4 links the stages by slot
** in declaration order, so a program that writes a different set of members links against nothing
** the pixel half generates.
*/

#ifndef ENGINESHADER_H
#define ENGINESHADER_H

#include "ffshader.h"
#include "ffvertex.h"

#include <string>

// The engine sets its shader constants as float4 registers, the way D3D9 takes them, and the
// backend mirrors the whole bank rather than tracking which register belongs to which program.
// This is DX8Wrapper's MAX_VERTEX_SHADER_CONSTANTS; the two have to agree or the mirror writes
// past the buffer the shader declares.
const unsigned ENGINE_SHADER_CONSTANTS = 96;

// Which of the engine's own shaders a draw has bound.  A shader that is not one of these has no
// D3D11 counterpart, and a draw made with it is refused rather than drawn wrongly.  The water's
// third shader, the environment-mapped one, is not here: it is the only shipped program that uses
// texbem, and nothing in the measured maps binds it.
enum EngineShaderProgram
{
	ENGINE_SHADER_NONE = 0,
	ENGINE_SHADER_TREES,
	ENGINE_SHADER_WATER_TRAPEZOID,
	ENGINE_SHADER_WATER_RIVER,
	ENGINE_SHADER_WATER_REFLECTION,
	ENGINE_SHADER_TERRAIN,
	ENGINE_SHADER_TERRAIN_NOISE,
	ENGINE_SHADER_TERRAIN_NOISE_2,
	ENGINE_SHADER_FLAT_TERRAIN,
	ENGINE_SHADER_FLAT_TERRAIN_BASE,
	ENGINE_SHADER_FLAT_TERRAIN_NOISE,
	ENGINE_SHADER_FLAT_TERRAIN_NOISE_2,
	ENGINE_SHADER_ROAD_NOISE_2,
	ENGINE_SHADER_MONOCHROME,
	// Explicit Classic variants keep the SDL shader cache independent of the selected water style.
	ENGINE_SHADER_WATER_CLASSIC_TRAPEZOID,
	ENGINE_SHADER_WATER_CLASSIC_RIVER,
	// The Classic trapezoid and river water again with foam, shore depth and the sun's shadow
	// (the Water Foam option).  Same D3D9 bytecode, a second name; after the table's last shipped program
	// so the shader case sweep and its reference set stay as they were.
	ENGINE_SHADER_WATER_TRAPEZOID_FOAM,
	ENGINE_SHADER_WATER_RIVER_FOAM,
	// The ground foliage (the GroundFoliage option): Trees.vso's bytecode registered a second time as
	// "shaders\\grass.vso", so D3D9 runs the tree shader unchanged and D3D11 gets the tree program
	// plus a shrink with distance from the camera's focus.
	ENGINE_SHADER_GRASS
};

// The name the engine loaded the shader under: a path for the ones that ship as files
// ("shaders\\Trees.vso"), and the name the registration passes for the ones the water assembles
// from text at run time.  The comparison is on the last path component and it ignores case.
EngineShaderProgram EngineShader_From_File(const char * file_path);

// The program as HLSL for the D3D11 profile, or for SDL3_GPU, which is the same text with its
// bindings rewritten (sdl3target.h).  False for ENGINE_SHADER_NONE, for a program of the other kind,
// for the D3D9 target, and for anything whose transcription is not written yet, which keeps that
// draw a refusal.
bool EngineShader_Vertex_Program(EngineShaderProgram program, std::string & hlsl,
	VertexShaderTarget target = VERTEX_SHADER_TARGET_D3D11);

// The pixel half.  The alpha test and the fog come from the render state and are written into the
// program the way ffshader writes them, because D3D9 applies both around a bound pixel shader and
// D3D11 applies neither.
bool EngineShader_Pixel_Program(EngineShaderProgram program,
	const PixelPipelineDescription & pipeline, std::string & hlsl,
	CombinerShaderTarget target = COMBINER_SHADER_TARGET_D3D11);

// Which transcribed programs paint the ground, and so read the sun's shadow map.  The water and
// the trees are not among them: both have shadows of their own.
bool EngineShader_Paints_Ground(EngineShaderProgram program);

// Which transcribed programs read the scene's depth copy at t7: the foam water, for how deep the
// water is under each pixel.  The copy is the soft particles' (DX11BackendClass::Take_Scene_Depth).
bool EngineShader_Reads_Scene_Depth(EngineShaderProgram program);

// The two programs the Modern water style draws with: the plain names, which W3DWater registers
// only under that style.  Their text reads the water's own constant block and fifteen stages.
inline bool EngineShader_Is_Modern_Water(EngineShaderProgram program)
{
	return program == ENGINE_SHADER_WATER_TRAPEZOID || program == ENGINE_SHADER_WATER_RIVER;
}

// What the pipeline cache keys this program under and what the dump calls its file.
const char * EngineShader_Name(EngineShaderProgram program);

#endif // ENGINESHADER_H
