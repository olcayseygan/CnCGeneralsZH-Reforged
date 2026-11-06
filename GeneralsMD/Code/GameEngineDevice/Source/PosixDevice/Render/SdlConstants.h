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
// Portions adapted from GeneralsMD/Code/Libraries/Source/WWVegas/WW3D2/dx11backend.h by Olcay Seygan (upstream CnCGeneralsZH-Reforged), GPL-3.0-or-later.

/*
** The constants a generated program reads (decision 7, phase A3c), pushed with SDL_PushGPUVertexUniformData
** and SDL_PushGPUFragmentUniformData at slot 0.  Laid out as the generators declare their blocks
** (ffvertex.cpp's `cbuffer VertexPipeline`, ffshader.cpp's `cbuffer CombinerConstants`), which is
** dx11backend's VertexConstantBlock and the first three fields of its PixelConstantBlock: a program
** declares a prefix of its block, and a longer push is legal.  Every field is a float4 or a row-major
** float4x4, so HLSL's packing and std140, which SDL3 asks for, agree.
**
** Ordinary combiners upload the small prefix. Sun/smoke receivers and cloak casters upload
** the shared extended engine block without changing the emulated register banks.
*/

#pragma once

#ifndef SDLCONSTANTS_H
#define SDLCONSTANTS_H

#include <cstddef>
#include "Platform/WaterHullVisualLayout.h"
#include "Platform/ModernWaterMaterial.h"
#include "../../../../Libraries/Source/WWVegas/WW3D2/ffvertex.h" // MAXIMUM_VERTEX_STAGES, MAXIMUM_VERTEX_LIGHTS

struct SdlVertexConstants
{
	float WorldViewProjection[16];
	float WorldView[16];
	float NormalTransform[16];		///< the inverse transpose of WorldView
	float TextureMatrix[MAXIMUM_VERTEX_STAGES][16];
	float MaterialAmbient[4];
	float MaterialDiffuse[4];
	float MaterialSpecular[4];
	float MaterialEmissive[4];
	float MaterialPower[4];
	float GlobalAmbient[4];
	float FogParameters[4];			///< start, end, density
	float ViewportInverse[4];		///< one over the viewport's width and height
	float LightFields[MAXIMUM_VERTEX_LIGHTS][VERTEX_REGISTERS_PER_LIGHT][4];	///< position, direction, diffuse, specular, attenuation, spot, ambient
};

struct SdlPixelConstants
{
	float TextureFactor[4];
	float FogColour[4];
	float AlphaReference[4];		///< .x is D3DRS_ALPHAREF as a whole level, 0 to 255
};

// Optional authored-surf wrapper. The original fixed-function prefix is byte-identical.
struct SdlShorePixelConstants
{
	SdlPixelConstants Combiner;
	float WorldFromClip[16];
	float Viewport[4];
	float Terrain[4];
	float Scene[4];
	float Surface[4];
};
static_assert(sizeof(SdlShorePixelConstants)==11*16,"shore wrapper packing");

// Dedicated map-water uniforms. The common prefix keeps alpha testing and fog compatible with
// the fixed-function pipeline; the remaining fields are used only by the SDL water shader.
struct SdlWaterPixelConstants
{
	SdlPixelConstants Combiner;
	float WorldFromClip[16];
	float CameraPosition[4];
	float SunDirection[4];
	float SunColour[4];
	float Viewport[4];
	float ReflectionArea[4];
	float WaterParameters[4]; // animation seconds, river flag, reflection available, mirror height
	float TerrainParameters[4]; // height texture border, world size, height scale, available
	float SceneParameters[4]; // refraction available, inverse scene-target width/height, scene depth available
	float OpticalParameters[4]; // authored depth/opacity, offshore context available, flat UV0 height correction available
	float HullParameters[4]; // number of visible vessel contours, authored ambient RGB for vessel foam
	float HullData[80][4]; // eight vessels, ten float4 registers each; runtime waterline geometry
	float FftParameters[4]; // x: the dedicated vertex displacement and six sampled ocean maps are bound
	float FoamHistoryArea[4]; // world origin XY, inverse world width/height; sampled at undisplaced surface XY
	float FoamHistoryParameters[4]; // available, water level, cell world size, simulation time of atlas
	float ContactHullData[80][4]; // optional foam-only contours; count < 3 falls back to the matching mean hull
	float FoamFillLights[4][4]; // two direction/colour pairs; first direction.w enables the main light for foam
#define MODERN_WATER_CPP_UNIFORM(name) float name[4];
	MODERN_WATER_MATERIAL_UNIFORMS(MODERN_WATER_CPP_UNIFORM)
#undef MODERN_WATER_CPP_UNIFORM
	float ClipFromWorld[16]; // inverse of depth-range-adjusted WorldFromClip for SSR
};
static_assert(sizeof(SdlWaterPixelConstants) == (WaterHullVisualLayout::VertexCommonRegisters + 4) * 16, "water uniform packing");
static_assert(offsetof(SdlWaterPixelConstants,MaterialColor)==184*16
	&& offsetof(SdlWaterPixelConstants,MaterialParameters)==185*16,"water material tail packing");

// Fragment-only extension after the shared water vertex/pixel prefix.
struct SdlWaterVisualPixelConstants
{
	SdlWaterPixelConstants Water;
	float VisualHullMeta[WaterHullVisualLayout::MaxHulls][4];
};
static_assert(sizeof(SdlWaterVisualPixelConstants)==(WaterHullVisualLayout::FragmentCommonRegisters + 4)*16,
	"fragment hull metadata packing");

// A generator adding a block must have an explicit recording/binding path. The mesh packet
// is the only four-block fragment layout; ordinary shaders cannot silently inherit its data.
inline bool Sdl_Draw_Uniform_Buffers_Supported(bool water_vertex, bool water_pixel,
	unsigned vertex_buffers, unsigned pixel_buffers)
{
	return vertex_buffers<=(water_vertex?2u:1u)
		&& (pixel_buffers<=1u || (water_pixel && pixel_buffers==4u));
}

/// Shared engine/generated receiver b0 layout. Normal-map and sky fields remain zero;
/// eligible receivers fill sun/smoke fields; Windows per-pixel light counts remain zero.
struct SdlEnginePixelConstants
{
	SdlPixelConstants Combiner;
	float NormalLightDirection[4][4];
	float NormalLightDiffuse[4][4];
	float NormalMapParameters[4];
	float TerrainSunDirection[4];
	float ShadowFromClip[16];
	float ShadowParameters[4];
	float ShadowViewport[4];
	float ShadowSoftness[4];
	float Sky[4];
	float SkyUp[4];
	float VolumeParameters[4];
	float WorldFromClip[16];
	float HeadlightParameters[4];
	float HeadlightPosition[16][4], HeadlightDirection[16][4];
	float BlastLightParameters[4];
	float BlastLightPosition[32][4], BlastLightDiffuse[32][4], BlastLightAmbient[32][4];
	float SoftParticleDepth[4];
	float DrawEffectParameters[4];
};
static_assert(sizeof(SdlEnginePixelConstants) == 159 * 16, "shared PixelConstantBlock packing");
static_assert(offsetof(SdlEnginePixelConstants, VolumeParameters) == 22 * 16, "smoke follows SkyUp");
static_assert(offsetof(SdlEnginePixelConstants, DrawEffectParameters) == 158 * 16, "caster opacity follows soft depth");

static_assert(sizeof(SdlVertexConstants) % 16 == 0 && sizeof(SdlPixelConstants) % 16 == 0,
	"whole float4s, as std140 lays them out");
static_assert(sizeof(SdlVertexConstants) == (3 + MAXIMUM_VERTEX_STAGES) * 64 + 8 * 16 + MAXIMUM_VERTEX_LIGHTS * VERTEX_REGISTERS_PER_LIGHT * 16,
	"the generated VertexPipeline block, field for field");

#endif // SDLCONSTANTS_H
