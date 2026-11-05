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
** Pipelines and samplers on SDL3 GPU (decision 7, phase A3c).  SDL3 bakes blend, depth, stencil and
** rasterizer state into the pipeline, so a pipeline is keyed by all of it, with the program pair, the
** vertex layout, the primitive type and the target's formats: a POD compared and hashed by its bytes
** (phase A3's design).  The stencil reference, the viewport, the scissor and the
** blend constant are dynamic and not in it.  A sampler is keyed by its D3D9 sampler states.
**
** The translations are free functions, so their test needs no GPU.  What D3D9 has and SDL3 GPU has
** not (point fill, border and mirror-once addressing, blend-weighted positions) is a refusal with its
** reason, never an approximation.
*/

#pragma once

#ifndef SDLPIPELINECACHE_H
#define SDLPIPELINECACHE_H

#include "Platform/RenderTypes.h"
#include "Platform/D3D9Posix.h"

#include <stdint.h>
#include <string.h>
#include <functional>
#include <string>
#include <unordered_map>

struct SDL_GPUDevice;
struct SDL_GPUGraphicsPipeline;
struct SDL_GPUSampler;
struct SDL_GPUShader;

/// A flexible vertex format as SDL3 vertex attributes, at D3's locations (sdl3target.h): position 0,
/// normal 1, the diffuse colour 2 (bytes B, G, R, A, read as UBYTE4_NORM and swapped by the program),
/// the specular colour 3, texture coordinate set n at 4 + n.  Only what the generated program declares
/// is an attribute: the point size is skipped over, and sets past the fourth.
struct SdlVertexLayout
{
	enum { MAXIMUM_ATTRIBUTES = 8 };
	unsigned int AttributeCount;
	unsigned int Location[MAXIMUM_ATTRIBUTES];
	unsigned int Format[MAXIMUM_ATTRIBUTES];	///< SDL_GPUVertexElementFormat
	unsigned int Offset[MAXIMUM_ATTRIBUTES];
	unsigned int Stride;
};
bool Sdl_Vertex_Layout(RenderUInt32 fvf, SdlVertexLayout &layout, std::string &refusal);

/// Everything a pipeline is made from.  Built with memset first, so the padding is zero and two keys
/// compare equal exactly when their bytes do.
struct SdlPipelineKey
{
	SDL_GPUShader *VertexShader;
	SDL_GPUShader *PixelShader;
	uint32_t FVF;
	uint32_t ColourFormat;			///< SDL_GPUTextureFormat of the target
	uint32_t DepthFormat;			///< SDL_GPUTextureFormat of the depth-stencil, or 0 for none
	float DepthBias;				///< in the depth buffer's units, as SDL takes it
	float SlopeBias;
	uint8_t Primitive;				///< SDL_GPUPrimitiveType
	uint8_t Fill;					///< SDL_GPUFillMode
	uint8_t Cull;					///< SDL_GPUCullMode, with clockwise triangles facing front
	uint8_t BlendEnable;
	uint8_t SourceColour, DestinationColour, ColourOperation;	///< SDL_GPUBlendFactor / SDL_GPUBlendOp
	uint8_t SourceAlpha, DestinationAlpha, AlphaOperation;
	uint8_t WriteMask;				///< SDL_GPUColorComponentFlags, which are D3D9's bits
	uint8_t DepthTest, DepthWrite, DepthCompare;
	uint8_t StencilEnable, StencilRead, StencilWrite;
	uint8_t FrontFail, FrontDepthFail, FrontPass, FrontCompare;
	uint8_t BackFail, BackDepthFail, BackPass, BackCompare;
};

/// The key for the render states, programs, vertex format, primitive and target.  False, with the
/// reason, for what SDL3 GPU cannot draw.
bool Sdl_Pipeline_Key(const RenderUInt32 render_states[256], D3DPRIMITIVETYPE primitive, SDL_GPUShader *vertex_shader,
	SDL_GPUShader *pixel_shader, RenderUInt32 fvf, uint32_t colour_format, uint32_t depth_format, SdlPipelineKey &key,
	std::string &refusal);

struct SdlPipelineKeyHash
{
	size_t operator()(const SdlPipelineKey &key) const;
};
struct SdlPipelineKeyEqual
{
	bool operator()(const SdlPipelineKey &left, const SdlPipelineKey &right) const;
};

class SdlPipelineCache
{
public:
	explicit SdlPipelineCache(SDL_GPUDevice *device);
	~SdlPipelineCache();
	/// Null when the device refuses it; logged once per key.
	SDL_GPUGraphicsPipeline *Pipeline(const SdlPipelineKey &key, bool sunReceiver = false);
	unsigned int Pipelines_Built() const { return Built; }
	/// Names a program for a refusal's log line (the device points it at SdlProgramCache::Key_Of).
	std::function<std::string(const SDL_GPUShader *)> Describe_Shader;

private:
	SDL_GPUDevice *Device;
	std::unordered_map<SdlPipelineKey, SDL_GPUGraphicsPipeline *, SdlPipelineKeyHash, SdlPipelineKeyEqual> Pipelines;
	SdlPipelineKey LastKey;
	SDL_GPUGraphicsPipeline *LastPipeline;
	unsigned int Built;
};

/// A sampler's description from D3D9's sampler states (the device's SamplerStates row).  False, with
/// the reason, for an address mode or filter SDL3 GPU has not got.  The description is an
/// SDL_GPUSamplerCreateInfo; void * keeps SDL's header out of this one.
bool Sdl_Sampler_Description(const RenderUInt32 sampler_states[14], void *create_info, std::string &refusal);

class SdlSamplerCache
{
public:
	explicit SdlSamplerCache(SDL_GPUDevice *device);
	~SdlSamplerCache();
	/// Null for a refusal (Refusal() says why) or when the device refuses it.
	SDL_GPUSampler *Sampler(const RenderUInt32 sampler_states[14]);
	const std::string &Refusal() const { return LastRefusal; }
	size_t Table_Lookups() const {
		return TableLookups;
	}

private:
	/// A stage's 14 sampler states, compared exactly: PERF1 found the std::string key
	/// this replaced (56 bytes, past the string's inline buffer) allocating on every lookup, 8 slots a draw.
	struct Key
	{
		RenderUInt32 States[14];
		bool operator==(const Key &other) const { return memcmp(States, other.States, sizeof(States)) == 0; }
	};
	struct KeyHash
	{
		size_t operator()(const Key &key) const;
	};
	SDL_GPUDevice *Device;
	std::unordered_map<Key, SDL_GPUSampler *, KeyHash> Samplers;
	// Consecutive stages often alternate a few settings. Retain the last-hit shortcut,
	// with a bounded ring for the other recent keys, including cached refusals.
	static constexpr unsigned RecentCount = 4;
	struct RecentEntry {
		Key key;
		SDL_GPUSampler *sampler;
	};
	RecentEntry Recent[RecentCount];
	unsigned RecentSize, LastRecent, NextRecent;
	size_t TableLookups;
	void Remember(const Key &key, SDL_GPUSampler *sampler);
	std::string LastRefusal;
};

#endif // SDLPIPELINECACHE_H
