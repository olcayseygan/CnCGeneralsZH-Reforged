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

// Exact shader model 4 HLSL shared by native backends. No resources or compiler are owned here.
#ifndef POSTSHADER_H
#define POSTSHADER_H

#include <string>

#include "postprocess.h"

enum PostShaderProgram
{
	POST_SHADER_COPY,
	POST_SHADER_TONE_MAP,
	POST_SHADER_TONE_MAP_WIDE,
	POST_SHADER_FXAA,
	POST_SHADER_SHARPEN,
	POST_SHADER_SHARPEN_MULTISAMPLED,
	POST_SHADER_AO,
	POST_SHADER_AO_SCENE,
	POST_SHADER_WORLD_AO,
	POST_SHADER_WORLD_AO_BLUR,
	POST_SHADER_WORLD_AO_UPSAMPLE,
	POST_SHADER_HAZE,
	POST_SHADER_SKY,
	POST_SHADER_ATMOSPHERE,
	POST_SHADER_BLOOM_EXTRACT,
	POST_SHADER_BLOOM_BLUR,
	POST_SHADER_BLOOM_COMPOSITE,
	POST_SHADER_BLOOM_COMPOSITE_WIDE,
	POST_SHADER_WARP,
	POST_SHADER_HEAT_SPLAT,
	POST_SHADER_HEAT_HAZE,
	POST_SHADER_COUNT
};

// Every pixel program is the same common prologue followed by its unchanged effect body.
// Invalid IDs return NULL/an empty string. Source generation does not compile or retarget HLSL.
const char * PostShader_Pixel_Prologue();
const char * PostShader_Pixel_Body(PostShaderProgram program);
std::string PostShader_Pixel_Source(PostShaderProgram program);
PostShaderProgram PostShader_Simple_Effect(DX11PostEffect effect);

// Both stages use entry point "main", DX11 profiles vs_4_0 / ps_4_0. The fullscreen vertex
// stage uses SV_VertexID (3 vertices); heat splats use six vertices and a typed Buffer<float4>
// at vertex t0, four float4 records per splat (shape, heat, planes, beam exclusion).
const char * PostShader_Vertex_Source();
const char * PostShader_Heat_Splat_Vertex_Source();

// Pixel bindings (declared prologue resources can be optimized out):
// t0 Source, t1 Extra, s0 bilinear clamp, b0 PostProcessConstants c0..c12.
// Haze/world AO/sky/atmosphere additionally bind the SAME whole constants block at b1,
// with explicit packoffsets c13..c21, row-major WorldFromClip at c15..c18.
// Haze/world AO/atmosphere use shroud t2 (green channel); sky uses t3/s1 bilinear wrap.
// Extra is depth for chain/world AO, haze, atmosphere and heat splats; bloom composite reads
// bloom through Extra; heat composite reads the half-resolution distortion field through Extra.
// World AO/blur write half-resolution RG32_FLOAT (occlusion, eye distance); bloom extract/blur
// write quarter-resolution RGBA16_FLOAT; heat splats add into half-resolution RGBA16_FLOAT.
// World AO upsample and scene AO multiply scene RGB and preserve destination alpha. Haze and
// atmosphere use source-alpha RGB blending and preserve destination alpha. Sky overwrites the
// scene. Final chain/tone/composite passes write the presentation's eight-bit UNORM format.
// Copy may also copy an intermediate scene in its existing format. All depth is D3D [0,1].
//
// Retargeting must preserve separate texture/sampler registers and b1's padded offsets, convert
// SV_VertexID for the target, and support Texture2D.Load/GetDimensions and typed vertex buffers.
// SDL/Vulkan must also map the legacy top-left texture convention and D3D [0,1] depth without
// adding the scene geometry's half-pixel offset to the fullscreen triangle. Descriptor sets and
// dense texture/sampler/uniform bindings must be assigned by the consuming retargeter; compiling
// the original register text directly is a syntax check, not an SDL-compatible pipeline layout.
// No D3D or SDL headers, descriptor set choices, or SPIR-V generation are imposed here.

#endif // POSTSHADER_H
