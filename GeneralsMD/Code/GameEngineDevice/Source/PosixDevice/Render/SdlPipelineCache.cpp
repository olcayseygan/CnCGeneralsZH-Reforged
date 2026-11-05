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

// Pipelines and samplers on SDL3 GPU (decision 7, phase A3c).  See SdlPipelineCache.h.

#include "SdlPipelineCache.h"
#include "SdlSunShadowDebug.h"
#include "SdlCreationLog.h"

#include <SDL3/SDL.h>

#include <stdio.h>
#include <string.h>

//-------------------------------------------------------------------------------------------------
// The vertex layout.
//-------------------------------------------------------------------------------------------------

static void add_attribute(SdlVertexLayout &layout, unsigned int location, SDL_GPUVertexElementFormat format, unsigned int offset)
{
	const unsigned int at = layout.AttributeCount++;
	layout.Location[at] = location;
	layout.Format[at] = format;
	layout.Offset[at] = offset;
}

bool Sdl_Vertex_Layout(RenderUInt32 fvf, SdlVertexLayout &layout, std::string &refusal)
{
	memset(&layout, 0, sizeof(layout));
	unsigned int offset = 0;
	const RenderUInt32 position = fvf & D3DFVF_POSITION_MASK;
	if (position == D3DFVF_XYZ) {
		add_attribute(layout, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, offset);
		offset += 12;
	}
	else if (position == D3DFVF_XYZRHW) {
		// Four floats, all read: the pretransformed program takes w from the vertex's RHW, which is
		// what makes its colours and coordinates interpolate with perspective.
		add_attribute(layout, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, offset);
		offset += 16;
	}
	else {
		refusal = "a blend-weighted position (vertex blending)";
		return false;
	}
	if (fvf & D3DFVF_NORMAL) {
		add_attribute(layout, 1, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, offset);
		offset += 12;
	}
	if (fvf & D3DFVF_PSIZE) {
		offset += 4;	// not read: point sizes are refused elsewhere
	}
	if (fvf & D3DFVF_DIFFUSE) {
		add_attribute(layout, 2, SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM, offset);
		offset += 4;
	}
	if (fvf & D3DFVF_SPECULAR) {
		// COLOR1, a D3DCOLOR like the diffuse, which the program swaps the same way.
		add_attribute(layout, 3, SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM, offset);
		offset += 4;
	}
	const unsigned int sets = (fvf & D3DFVF_TEXCOUNT_MASK) >> D3DFVF_TEXCOUNT_SHIFT;
	static const SDL_GPUVertexElementFormat FLOATS[4] = { SDL_GPU_VERTEXELEMENTFORMAT_FLOAT,
		SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4 };
	for (unsigned int set = 0; set < sets; ++set) {
		// D3DFVF_TEXCOORDSIZEn: two bits per set, 0 two floats, 1 three, 2 four, 3 one.
		static const unsigned int COUNT_OF_BITS[4] = { 2, 3, 4, 1 };
		const unsigned int floats = COUNT_OF_BITS[(fvf >> (16 + set * 2)) & 3];
		// The program declares the first four sets, as two floats: a wider set gives its first two, a
		// one-float set its first and a zero.
		if (set < 4) {
			add_attribute(layout, 4 + set, FLOATS[floats - 1], offset);
		}
		offset += floats * 4;
	}
	layout.Stride = offset;
	return true;
}

//-------------------------------------------------------------------------------------------------
// The pipeline key.
//-------------------------------------------------------------------------------------------------

static bool blend_factor(RenderUInt32 value, uint8_t &out)
{
	switch (value) {
		case D3DBLEND_ZERO:				out = SDL_GPU_BLENDFACTOR_ZERO; return true;
		case D3DBLEND_ONE:				out = SDL_GPU_BLENDFACTOR_ONE; return true;
		case D3DBLEND_SRCCOLOR:			out = SDL_GPU_BLENDFACTOR_SRC_COLOR; return true;
		case D3DBLEND_INVSRCCOLOR:		out = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_COLOR; return true;
		case D3DBLEND_SRCALPHA:			out = SDL_GPU_BLENDFACTOR_SRC_ALPHA; return true;
		case D3DBLEND_INVSRCALPHA:		out = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA; return true;
		case D3DBLEND_DESTALPHA:		out = SDL_GPU_BLENDFACTOR_DST_ALPHA; return true;
		case D3DBLEND_INVDESTALPHA:		out = SDL_GPU_BLENDFACTOR_ONE_MINUS_DST_ALPHA; return true;
		case D3DBLEND_DESTCOLOR:		out = SDL_GPU_BLENDFACTOR_DST_COLOR; return true;
		case D3DBLEND_INVDESTCOLOR:		out = SDL_GPU_BLENDFACTOR_ONE_MINUS_DST_COLOR; return true;
		case D3DBLEND_SRCALPHASAT:		out = SDL_GPU_BLENDFACTOR_SRC_ALPHA_SATURATE; return true;
		case D3DBLEND_BLENDFACTOR:		out = SDL_GPU_BLENDFACTOR_CONSTANT_COLOR; return true;
		case D3DBLEND_INVBLENDFACTOR:	out = SDL_GPU_BLENDFACTOR_ONE_MINUS_CONSTANT_COLOR; return true;
		default: return false;
	}
}

static bool blend_operation(RenderUInt32 value, uint8_t &out)
{
	switch (value) {
		case D3DBLENDOP_ADD:			out = SDL_GPU_BLENDOP_ADD; return true;
		case D3DBLENDOP_SUBTRACT:		out = SDL_GPU_BLENDOP_SUBTRACT; return true;
		case D3DBLENDOP_REVSUBTRACT:	out = SDL_GPU_BLENDOP_REVERSE_SUBTRACT; return true;
		case D3DBLENDOP_MIN:			out = SDL_GPU_BLENDOP_MIN; return true;
		case D3DBLENDOP_MAX:			out = SDL_GPU_BLENDOP_MAX; return true;
		default: return false;
	}
}

// A source and destination pair, with D3D9's two shorthands that set both from the source state.
static bool blend_pair(RenderUInt32 source, RenderUInt32 destination, uint8_t &source_out, uint8_t &destination_out)
{
	if (source == D3DBLEND_BOTHSRCALPHA) {
		source_out = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
		destination_out = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
		return true;
	}
	if (source == D3DBLEND_BOTHINVSRCALPHA) {
		source_out = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
		destination_out = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
		return true;
	}
	return blend_factor(source, source_out) && blend_factor(destination, destination_out);
}

static bool compare_operation(RenderUInt32 value, uint8_t &out)
{
	switch (value) {
		case D3DCMP_NEVER:			out = SDL_GPU_COMPAREOP_NEVER; return true;
		case D3DCMP_LESS:			out = SDL_GPU_COMPAREOP_LESS; return true;
		case D3DCMP_EQUAL:			out = SDL_GPU_COMPAREOP_EQUAL; return true;
		case D3DCMP_LESSEQUAL:		out = SDL_GPU_COMPAREOP_LESS_OR_EQUAL; return true;
		case D3DCMP_GREATER:		out = SDL_GPU_COMPAREOP_GREATER; return true;
		case D3DCMP_NOTEQUAL:		out = SDL_GPU_COMPAREOP_NOT_EQUAL; return true;
		case D3DCMP_GREATEREQUAL:	out = SDL_GPU_COMPAREOP_GREATER_OR_EQUAL; return true;
		case D3DCMP_ALWAYS:			out = SDL_GPU_COMPAREOP_ALWAYS; return true;
		default: return false;
	}
}

static bool stencil_operation(RenderUInt32 value, uint8_t &out)
{
	switch (value) {
		case D3DSTENCILOP_KEEP:		out = SDL_GPU_STENCILOP_KEEP; return true;
		case D3DSTENCILOP_ZERO:		out = SDL_GPU_STENCILOP_ZERO; return true;
		case D3DSTENCILOP_REPLACE:	out = SDL_GPU_STENCILOP_REPLACE; return true;
		case D3DSTENCILOP_INCRSAT:	out = SDL_GPU_STENCILOP_INCREMENT_AND_CLAMP; return true;
		case D3DSTENCILOP_DECRSAT:	out = SDL_GPU_STENCILOP_DECREMENT_AND_CLAMP; return true;
		case D3DSTENCILOP_INVERT:	out = SDL_GPU_STENCILOP_INVERT; return true;
		case D3DSTENCILOP_INCR:		out = SDL_GPU_STENCILOP_INCREMENT_AND_WRAP; return true;
		case D3DSTENCILOP_DECR:		out = SDL_GPU_STENCILOP_DECREMENT_AND_WRAP; return true;
		default: return false;
	}
}

static float float_of(RenderUInt32 bits)
{
	float value;
	memcpy(&value, &bits, sizeof(value));
	return value;
}

// D3D9 measures the depth bias in depth values and SDL, as D3D11, in units of the depth buffer's
// resolution: 2^-24 for D24, and for D32F the same wherever the depth is in [0.5, 1), which is where
// a perspective scene's depths are.  dx11state.cpp scales the same way.
static const float DEPTH_BIAS_UNIT_SCALE = 16777216.0f;

bool Sdl_Pipeline_Key(const RenderUInt32 rs[256], D3DPRIMITIVETYPE primitive, SDL_GPUShader *vertex_shader,
	SDL_GPUShader *pixel_shader, RenderUInt32 fvf, uint32_t colour_format, uint32_t depth_format, SdlPipelineKey &key,
	std::string &refusal)
{
	memset(&key, 0, sizeof(key));
	key.VertexShader = vertex_shader;
	key.PixelShader = pixel_shader;
	key.FVF = fvf;
	key.ColourFormat = colour_format;
	key.DepthFormat = depth_format;

	switch (primitive) {
		case D3DPT_POINTLIST:		key.Primitive = SDL_GPU_PRIMITIVETYPE_POINTLIST; break;
		case D3DPT_LINELIST:		key.Primitive = SDL_GPU_PRIMITIVETYPE_LINELIST; break;
		case D3DPT_LINESTRIP:		key.Primitive = SDL_GPU_PRIMITIVETYPE_LINESTRIP; break;
		case D3DPT_TRIANGLELIST:	key.Primitive = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST; break;
		case D3DPT_TRIANGLESTRIP:	key.Primitive = SDL_GPU_PRIMITIVETYPE_TRIANGLESTRIP; break;
		case D3DPT_TRIANGLEFAN:		key.Primitive = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST; break;	// expanded by the draw
		default: refusal = "an unknown primitive type"; return false;
	}

	switch (rs[D3DRS_FILLMODE]) {
		case D3DFILL_SOLID:			key.Fill = SDL_GPU_FILLMODE_FILL; break;
		case D3DFILL_WIREFRAME:		key.Fill = SDL_GPU_FILLMODE_LINE; break;
		default: refusal = "point fill mode"; return false;
	}
	// Clockwise triangles face front (SdlPipelineCache::Pipeline), so D3D9's cull modes are these.
	switch (rs[D3DRS_CULLMODE]) {
		case D3DCULL_NONE:	key.Cull = SDL_GPU_CULLMODE_NONE; break;
		case D3DCULL_CW:	key.Cull = SDL_GPU_CULLMODE_FRONT; break;
		case D3DCULL_CCW:	key.Cull = SDL_GPU_CULLMODE_BACK; break;
		default: refusal = "an unknown cull mode"; return false;
	}
	key.DepthBias = float_of(rs[D3DRS_DEPTHBIAS]) * DEPTH_BIAS_UNIT_SCALE;
	key.SlopeBias = float_of(rs[D3DRS_SLOPESCALEDEPTHBIAS]);

	// Blending.  Disabled, the factors are left at zero so every disabled blend is one key.
	key.BlendEnable = rs[D3DRS_ALPHABLENDENABLE] != 0;
	if (key.BlendEnable) {
		if (!blend_pair(rs[D3DRS_SRCBLEND], rs[D3DRS_DESTBLEND], key.SourceColour, key.DestinationColour)
			|| !blend_operation(rs[D3DRS_BLENDOP], key.ColourOperation)) {
			refusal = "an unknown blend factor or operation";
			return false;
		}
		if (rs[D3DRS_SEPARATEALPHABLENDENABLE] != 0) {
			if (!blend_pair(rs[D3DRS_SRCBLENDALPHA], rs[D3DRS_DESTBLENDALPHA], key.SourceAlpha, key.DestinationAlpha)
				|| !blend_operation(rs[D3DRS_BLENDOPALPHA], key.AlphaOperation)) {
				refusal = "an unknown alpha blend factor or operation";
				return false;
			}
		}
		else {
			key.SourceAlpha = key.SourceColour;
			key.DestinationAlpha = key.DestinationColour;
			key.AlphaOperation = key.ColourOperation;
		}
	}
	key.WriteMask = (uint8_t)(rs[D3DRS_COLORWRITEENABLE] & 0xF);

	// Depth: only with a depth-stencil to test against.
	key.DepthTest = depth_format != 0 && rs[D3DRS_ZENABLE] != D3DZB_FALSE;
	if (key.DepthTest) {
		key.DepthWrite = rs[D3DRS_ZWRITEENABLE] != 0;
		if (!compare_operation(rs[D3DRS_ZFUNC], key.DepthCompare)) {
			refusal = "an unknown depth compare function";
			return false;
		}
	}

	// Stencil: front is the clockwise face; the CCW_ states are the other face's when two-sided.
	key.StencilEnable = depth_format != 0 && rs[D3DRS_STENCILENABLE] != 0;
	if (key.StencilEnable) {
		key.StencilRead = (uint8_t)(rs[D3DRS_STENCILMASK] & 0xFF);
		key.StencilWrite = (uint8_t)(rs[D3DRS_STENCILWRITEMASK] & 0xFF);
		const bool two_sided = rs[D3DRS_TWOSIDEDSTENCILMODE] != 0;
		if (!stencil_operation(rs[D3DRS_STENCILFAIL], key.FrontFail)
			|| !stencil_operation(rs[D3DRS_STENCILZFAIL], key.FrontDepthFail)
			|| !stencil_operation(rs[D3DRS_STENCILPASS], key.FrontPass)
			|| !compare_operation(rs[D3DRS_STENCILFUNC], key.FrontCompare)
			|| !stencil_operation(rs[two_sided ? D3DRS_CCW_STENCILFAIL : D3DRS_STENCILFAIL], key.BackFail)
			|| !stencil_operation(rs[two_sided ? D3DRS_CCW_STENCILZFAIL : D3DRS_STENCILZFAIL], key.BackDepthFail)
			|| !stencil_operation(rs[two_sided ? D3DRS_CCW_STENCILPASS : D3DRS_STENCILPASS], key.BackPass)
			|| !compare_operation(rs[two_sided ? D3DRS_CCW_STENCILFUNC : D3DRS_STENCILFUNC], key.BackCompare)) {
			refusal = "an unknown stencil operation or function";
			return false;
		}
	}
	return true;
}

size_t SdlPipelineKeyHash::operator()(const SdlPipelineKey &key) const
{
	// FNV-1a over the key's bytes, padding included: the key is built with memset first.
	const unsigned char *bytes = reinterpret_cast<const unsigned char *>(&key);
	uint64_t hash = 14695981039346656037ull;
	for (size_t i = 0; i < sizeof(key); ++i) {
		hash = (hash ^ bytes[i]) * 1099511628211ull;
	}
	return (size_t)hash;
}

bool SdlPipelineKeyEqual::operator()(const SdlPipelineKey &left, const SdlPipelineKey &right) const
{
	return memcmp(&left, &right, sizeof(left)) == 0;
}

//-------------------------------------------------------------------------------------------------
// The pipeline cache.
//-------------------------------------------------------------------------------------------------

SdlPipelineCache::SdlPipelineCache(SDL_GPUDevice *device) :
	Device(device),
	LastPipeline(NULL),
	Built(0)
{
	memset(&LastKey, 0, sizeof(LastKey));
}

SdlPipelineCache::~SdlPipelineCache()
{
	for (auto it = Pipelines.begin(); it != Pipelines.end(); ++it) {
		if (it->second != NULL) {
			SDL_ReleaseGPUGraphicsPipeline(Device, it->second);
		}
	}
}

SDL_GPUGraphicsPipeline *SdlPipelineCache::Pipeline(const SdlPipelineKey &key, bool sunReceiver)
{
	if (LastPipeline != NULL && memcmp(&key, &LastKey, sizeof(key)) == 0) {
		return LastPipeline;
	}
	auto existing = Pipelines.find(key);
	if (existing != Pipelines.end()) {
		LastKey = key;
		LastPipeline = existing->second;
		return existing->second;
	}

	SdlVertexLayout layout;
	memset(&layout, 0, sizeof(layout));
	std::string refusal;
	SDL_GPUGraphicsPipeline *pipeline = NULL;
	if (Sdl_Vertex_Layout(key.FVF, layout, refusal)) {
		SDL_GPUVertexBufferDescription buffer;
		SDL_zero(buffer);
		buffer.slot = 0;
		buffer.pitch = layout.Stride;
		buffer.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
		SDL_GPUVertexAttribute attributes[SdlVertexLayout::MAXIMUM_ATTRIBUTES];
		for (unsigned int i = 0; i < layout.AttributeCount; ++i) {
			attributes[i].location = layout.Location[i];
			attributes[i].buffer_slot = 0;
			attributes[i].format = (SDL_GPUVertexElementFormat)layout.Format[i];
			attributes[i].offset = layout.Offset[i];
		}

		SDL_GPUColorTargetDescription target;
		SDL_zero(target);
		target.format = (SDL_GPUTextureFormat)key.ColourFormat;
		target.blend_state.enable_blend = key.BlendEnable != 0;
		target.blend_state.src_color_blendfactor = (SDL_GPUBlendFactor)(key.BlendEnable ? key.SourceColour : SDL_GPU_BLENDFACTOR_ONE);
		target.blend_state.dst_color_blendfactor = (SDL_GPUBlendFactor)(key.BlendEnable ? key.DestinationColour : SDL_GPU_BLENDFACTOR_ZERO);
		target.blend_state.color_blend_op = (SDL_GPUBlendOp)(key.BlendEnable ? key.ColourOperation : SDL_GPU_BLENDOP_ADD);
		target.blend_state.src_alpha_blendfactor = (SDL_GPUBlendFactor)(key.BlendEnable ? key.SourceAlpha : SDL_GPU_BLENDFACTOR_ONE);
		target.blend_state.dst_alpha_blendfactor = (SDL_GPUBlendFactor)(key.BlendEnable ? key.DestinationAlpha : SDL_GPU_BLENDFACTOR_ZERO);
		target.blend_state.alpha_blend_op = (SDL_GPUBlendOp)(key.BlendEnable ? key.AlphaOperation : SDL_GPU_BLENDOP_ADD);
		target.blend_state.color_write_mask = (SDL_GPUColorComponentFlags)key.WriteMask;
		target.blend_state.enable_color_write_mask = true;

		SDL_GPUGraphicsPipelineCreateInfo info;
		SDL_zero(info);
		info.vertex_shader = key.VertexShader;
		info.fragment_shader = key.PixelShader;
		info.vertex_input_state.vertex_buffer_descriptions = &buffer;
		info.vertex_input_state.num_vertex_buffers = 1;
		info.vertex_input_state.vertex_attributes = attributes;
		info.vertex_input_state.num_vertex_attributes = layout.AttributeCount;
		info.primitive_type = (SDL_GPUPrimitiveType)key.Primitive;
		info.rasterizer_state.fill_mode = (SDL_GPUFillMode)key.Fill;
		info.rasterizer_state.cull_mode = (SDL_GPUCullMode)key.Cull;
		info.rasterizer_state.front_face = SDL_GPU_FRONTFACE_CLOCKWISE;
		info.rasterizer_state.enable_depth_bias = key.DepthBias != 0.0f || key.SlopeBias != 0.0f;
		info.rasterizer_state.depth_bias_constant_factor = key.DepthBias;
		info.rasterizer_state.depth_bias_slope_factor = key.SlopeBias;
		info.rasterizer_state.enable_depth_clip = true;
		info.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
		info.depth_stencil_state.enable_depth_test = key.DepthTest != 0;
		info.depth_stencil_state.enable_depth_write = key.DepthWrite != 0;
		info.depth_stencil_state.compare_op = key.DepthTest ? (SDL_GPUCompareOp)key.DepthCompare : SDL_GPU_COMPAREOP_ALWAYS;
		info.depth_stencil_state.enable_stencil_test = key.StencilEnable != 0;
		if (key.StencilEnable) {
			info.depth_stencil_state.front_stencil_state.fail_op = (SDL_GPUStencilOp)key.FrontFail;
			info.depth_stencil_state.front_stencil_state.depth_fail_op = (SDL_GPUStencilOp)key.FrontDepthFail;
			info.depth_stencil_state.front_stencil_state.pass_op = (SDL_GPUStencilOp)key.FrontPass;
			info.depth_stencil_state.front_stencil_state.compare_op = (SDL_GPUCompareOp)key.FrontCompare;
			info.depth_stencil_state.back_stencil_state.fail_op = (SDL_GPUStencilOp)key.BackFail;
			info.depth_stencil_state.back_stencil_state.depth_fail_op = (SDL_GPUStencilOp)key.BackDepthFail;
			info.depth_stencil_state.back_stencil_state.pass_op = (SDL_GPUStencilOp)key.BackPass;
			info.depth_stencil_state.back_stencil_state.compare_op = (SDL_GPUCompareOp)key.BackCompare;
			info.depth_stencil_state.compare_mask = key.StencilRead;
			info.depth_stencil_state.write_mask = key.StencilWrite;
		}
		info.target_info.color_target_descriptions = key.ColourFormat ? &target : NULL;
		info.target_info.num_color_targets = key.ColourFormat ? 1 : 0;
		info.target_info.has_depth_stencil_target = key.DepthFormat != 0;
		info.target_info.depth_stencil_format = (SDL_GPUTextureFormat)key.DepthFormat;
		const double started = Sdl_Creation_Log_Asked() ? Sdl_Now_Ms() : 0.0;
		if (sunReceiver && Sdl_Sun_Shadow_Fault("receiver-pipeline"))
			SDL_SetError("injected sun receiver pipeline failure");
		else pipeline = SDL_CreateGPUGraphicsPipeline(Device, &info);
		if (Sdl_Creation_Log_Asked()) {
			char detail[96];
			snprintf(detail, sizeof(detail), "#%u, %u attributes, blend %u, depth %u/%u", Built + 1, layout.AttributeCount,
				(unsigned)key.BlendEnable, (unsigned)key.DepthTest, (unsigned)key.DepthWrite);
			Sdl_Creation_Log("pipeline", started, Sdl_Now_Ms() - started, detail);
		}
		if (pipeline == NULL) {
			refusal = std::string("the device: ") + SDL_GetError();
		}
	}
	if (pipeline == NULL) {
		// Once per key, with what the key holds: a driver's refusal names no field, and the one that differs
		// from a built pipeline's is what it refused (X1: Direct3D 12 refuses what Metal and Vulkan build).
		fprintf(stderr, "SdlPipelineCache: a pipeline refused: %s\n", refusal.c_str());
		fprintf(stderr, "SdlPipelineCache:   fvf 0x%x, %u attributes", (unsigned)key.FVF, layout.AttributeCount);
		for (unsigned int i = 0; i < layout.AttributeCount; ++i) {
			fprintf(stderr, " [loc %u fmt %u off %u]", (unsigned)layout.Location[i], (unsigned)layout.Format[i],
				(unsigned)layout.Offset[i]);
		}
		fprintf(stderr, "; colour %u depth %u, primitive %u fill %u cull %u, blend %u (%u %u %u / %u %u %u) mask 0x%x,"
			" depth test %u write %u compare %u, stencil %u, bias %g/%g\n", (unsigned)key.ColourFormat,
			(unsigned)key.DepthFormat, (unsigned)key.Primitive, (unsigned)key.Fill, (unsigned)key.Cull,
			(unsigned)key.BlendEnable, (unsigned)key.SourceColour, (unsigned)key.DestinationColour,
			(unsigned)key.ColourOperation, (unsigned)key.SourceAlpha, (unsigned)key.DestinationAlpha,
			(unsigned)key.AlphaOperation, (unsigned)key.WriteMask, (unsigned)key.DepthTest, (unsigned)key.DepthWrite,
			(unsigned)key.DepthCompare, (unsigned)key.StencilEnable, key.DepthBias, key.SlopeBias);
		if (Describe_Shader) {
			fprintf(stderr, "SdlPipelineCache:   vertex program %s\nSdlPipelineCache:   pixel program %s\n",
				Describe_Shader(key.VertexShader).c_str(), Describe_Shader(key.PixelShader).c_str());
		}
	}
	else {
		++Built;
	}
	Pipelines[key] = pipeline;
	LastKey = key;
	LastPipeline = pipeline;
	return pipeline;
}

//-------------------------------------------------------------------------------------------------
// Samplers.
//-------------------------------------------------------------------------------------------------

static bool address_mode(RenderUInt32 value, SDL_GPUSamplerAddressMode &out)
{
	switch (value) {
		case D3DTADDRESS_WRAP:		out = SDL_GPU_SAMPLERADDRESSMODE_REPEAT; return true;
		case D3DTADDRESS_MIRROR:	out = SDL_GPU_SAMPLERADDRESSMODE_MIRRORED_REPEAT; return true;
		case D3DTADDRESS_CLAMP:		out = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE; return true;
		default: return false;		// BORDER and MIRRORONCE: SDL3 GPU has neither
	}
}

bool Sdl_Sampler_Description(const RenderUInt32 ss[14], void *create_info, std::string &refusal)
{
	SDL_GPUSamplerCreateInfo &info = *static_cast<SDL_GPUSamplerCreateInfo *>(create_info);
	SDL_zero(info);
	const RenderUInt32 minification = ss[D3DSAMP_MINFILTER];
	const RenderUInt32 magnification = ss[D3DSAMP_MAGFILTER];
	const RenderUInt32 mip = ss[D3DSAMP_MIPFILTER];
	if (minification > D3DTEXF_ANISOTROPIC || magnification > D3DTEXF_ANISOTROPIC || mip > D3DTEXF_ANISOTROPIC) {
		refusal = "a pyramidal or gaussian filter";
		return false;
	}
	const bool anisotropic = minification == D3DTEXF_ANISOTROPIC || magnification == D3DTEXF_ANISOTROPIC;
	info.min_filter = (minification == D3DTEXF_LINEAR || anisotropic) ? SDL_GPU_FILTER_LINEAR : SDL_GPU_FILTER_NEAREST;
	info.mag_filter = (magnification == D3DTEXF_LINEAR || anisotropic) ? SDL_GPU_FILTER_LINEAR : SDL_GPU_FILTER_NEAREST;
	info.mipmap_mode = (mip == D3DTEXF_LINEAR || mip == D3DTEXF_ANISOTROPIC)
		? SDL_GPU_SAMPLERMIPMAPMODE_LINEAR : SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
	if (!address_mode(ss[D3DSAMP_ADDRESSU], info.address_mode_u)
		|| !address_mode(ss[D3DSAMP_ADDRESSV], info.address_mode_v)
		|| !address_mode(ss[D3DSAMP_ADDRESSW], info.address_mode_w)) {
		refusal = "border or mirror-once addressing";
		return false;
	}
	info.mip_lod_bias = float_of(ss[D3DSAMP_MIPMAPLODBIAS]);
	// D3DSAMP_MAXMIPLEVEL is the most detailed level the sampler may use.  With no mip filter the
	// sampler reads that level and no other, and still magnifies or minifies by the LOD: SDL3 GPU's
	// backends (Metal, Vulkan) choose between the two filters on the LOD after this clamp, so pinning
	// both ends to the level would magnify everywhere.  A quarter level of room keeps the nearest level
	// the same one and leaves a minified pixel's LOD above zero.
	info.min_lod = (float)ss[D3DSAMP_MAXMIPLEVEL];
	info.max_lod = mip == D3DTEXF_NONE ? info.min_lod + 0.25f : 1000.0f;
	// MAXANISOTROPY held to 1..16: the caps' MaxAnisotropy (PosixD3D9Caps.cpp), and the range Metal's
	// maxAnisotropy takes (SDL3 passes the value through as it is).
	const RenderUInt32 anisotropy = ss[D3DSAMP_MAXANISOTROPY];
	info.enable_anisotropy = anisotropic;
	info.max_anisotropy = anisotropic ? (float)(anisotropy < 1 ? 1 : anisotropy > 16 ? 16 : anisotropy) : 1.0f;
	return true;
}

SdlSamplerCache::SdlSamplerCache(SDL_GPUDevice *device) :
	Device(device),
	RecentSize(0), LastRecent(0), NextRecent(0), TableLookups(0)
{
}

SdlSamplerCache::~SdlSamplerCache()
{
	for (auto it = Samplers.begin(); it != Samplers.end(); ++it) {
		if (it->second != NULL) {
			SDL_ReleaseGPUSampler(Device, it->second);
		}
	}
}

size_t SdlSamplerCache::KeyHash::operator()(const Key &key) const
{
	// This is an in-process hash, not a persisted identifier. Mix whole state words
	// instead of a 56-byte multiply chain; equality still checks every state bit.
	uint64_t hash = 1469598103934665603ull;
	for (RenderUInt32 state : key.States) {
		hash = (hash ^ state) * 1099511628211ull;
	}
	return (size_t)hash;
}

void SdlSamplerCache::Remember(const Key &key, SDL_GPUSampler *sampler)
{
	Recent[NextRecent] = {key, sampler};
	LastRecent = NextRecent;
	NextRecent = (NextRecent + 1) % RecentCount;
	if (RecentSize < RecentCount)
		++RecentSize;
}

SDL_GPUSampler *SdlSamplerCache::Sampler(const RenderUInt32 sampler_states[14])
{
	Key key;
	memcpy(key.States, sampler_states, sizeof(key.States));
	if (RecentSize && key == Recent[LastRecent].key) {
		return Recent[LastRecent].sampler;
	}
	for (unsigned i = 0; i < RecentSize; ++i) {
		if (i != LastRecent && key == Recent[i].key) {
			LastRecent = i;
			return Recent[i].sampler;
		}
	}
	++TableLookups;
	auto existing = Samplers.find(key);
	if (existing != Samplers.end()) {
		Remember(key, existing->second);
		return existing->second;
	}
	SDL_GPUSamplerCreateInfo info;
	std::string refusal;
	SDL_GPUSampler *sampler = NULL;
	if (Sdl_Sampler_Description(sampler_states, &info, refusal)) {
		const double started = Sdl_Creation_Log_Asked() ? Sdl_Now_Ms() : 0.0;
		sampler = SDL_CreateGPUSampler(Device, &info);
		if (Sdl_Creation_Log_Asked()) {
			Sdl_Creation_Log("sampler", started, Sdl_Now_Ms() - started, "");
		}
		if (sampler == NULL) {
			refusal = std::string("the device: ") + SDL_GetError();
		}
	}
	if (sampler == NULL) {
		LastRefusal = refusal;
		fprintf(stderr, "SdlSamplerCache: a sampler refused: %s\n", refusal.c_str());
	}
	Samplers[key] = sampler;
	Remember(key, sampler);
	return sampler;
}
