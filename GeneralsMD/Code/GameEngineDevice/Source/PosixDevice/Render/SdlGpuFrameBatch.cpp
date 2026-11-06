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

// The frame's recorded batch (decision 7, phase A3c): what the draws record, and the flush that runs it
// as one copy pass and then the render passes.  See SdlGpuFrame.h.

#include "SdlGpuFrame.h"
#include "sdl3shadercompile.h"

#include <SDL3/SDL.h>

#include <stdio.h>
#include <string.h>

// Past this much staged or uploaded data the next draw flushes first, so one batch never needs an
// unbounded transfer buffer.
static const size_t BATCH_LIMIT = 64u * 1024u * 1024u;

// Every staged and uploaded range starts on this: enough for any vertex or index offset and for a texel
// block's copy on every backend.
static const uint32_t STAGING_ALIGNMENT = 16;

// The clear draw: one triangle over the whole target at the clear's depth, in the clear's colour, drawn
// through a scissor of the rectangle.  The values are uniforms (sdl3target.h's spaces: vertex b in
// space1, pixel b in space3).
static const char CLEAR_VERTEX_HLSL[] =
	"cbuffer ClearValues : register(b0, space1) { float4 Colour; float4 Depth; };\n"
	"float4 main(uint id : SV_VertexID) : SV_Position\n"
	"{\n"
	"	float2 corner = float2(id == 1 ? 3.0 : -1.0, id == 2 ? 3.0 : -1.0);\n"
	"	return float4(corner, Depth.x, 1.0);\n"
	"}\n";
static const char CLEAR_PIXEL_HLSL[] =
	"cbuffer ClearColour : register(b0, space3) { float4 Colour; };\n"
	"float4 main() : SV_Target0 { return Colour; }\n";

static_assert(sizeof(SdlGpuFrame::ArenaByte) == 1, "an arena byte is a byte, so the arena is contiguous bytes");

static uint8_t * grow(std::vector<SdlGpuFrame::ArenaByte> & bytes, uint32_t size, uint32_t & offset)
{
	offset = (uint32_t)((bytes.size() + STAGING_ALIGNMENT - 1) & ~(size_t)(STAGING_ALIGNMENT - 1));
	bytes.resize((size_t)offset + size);
	return size == 0 ? NULL : &bytes[offset].Value;
}

uint8_t * SdlGpuFrame::Stage(uint32_t size, uint32_t & offset)
{
	return grow(StreamBytes, size, offset);
}

uint32_t SdlGpuFrame::Stage_Offset_Next() const
{
	return (uint32_t)((StreamBytes.size() + STAGING_ALIGNMENT - 1) & ~(size_t)(STAGING_ALIGNMENT - 1));
}

uint8_t * SdlGpuFrame::Upload_Space(uint32_t size, uint32_t & offset)
{
	return grow(UploadBytes, size, offset);
}

void SdlGpuFrame::Queue_Buffer_Upload(SDL_GPUBuffer * buffer, uint32_t upload_offset, uint32_t size)
{
	Upload upload;
	memset(&upload, 0, sizeof(upload));
	upload.Buffer = buffer;
	upload.Offset = upload_offset;
	upload.Size = size;
	Uploads.push_back(upload);
}

void SdlGpuFrame::Queue_Texture_Upload(SDL_GPUTexture * texture, unsigned int level, unsigned int width,
	unsigned int height, uint32_t upload_offset)
{
	Queue_Texture_Upload(texture, level, width, height, upload_offset, level == 0);
}

void SdlGpuFrame::Queue_Texture_Upload(SDL_GPUTexture * texture, unsigned int level, unsigned int width,
	unsigned int height, uint32_t upload_offset, bool cycle)
{
	Upload upload;
	memset(&upload, 0, sizeof(upload));
	upload.Texture = texture;
	upload.Level = level;
	upload.Width = width;
	upload.Height = height;
	upload.Offset = upload_offset;
	upload.CycleTexture = cycle;
	Uploads.push_back(upload);
}

uint32_t SdlGpuFrame::Constants(unsigned int stage, const void * bytes, uint32_t size, unsigned int slot)
{
	SDL_assert(stage<2 && slot<SdlRecordedDraw::MAXIMUM_UNIFORM_SLOTS && bytes!=NULL && size>0);
	if (LastConstantsSize[stage][slot] == size && memcmp(&ConstantBytes[LastConstants[stage][slot]], bytes, size) == 0) {
		return LastConstants[stage][slot];
	}
	uint32_t offset = 0;
	memcpy(grow(ConstantBytes, size, offset), bytes, size);
	LastConstants[stage][slot] = offset;
	LastConstantsSize[stage][slot] = size;
	return offset;
}

void SdlGpuFrame::Clear_Rect(int x, int y, int width, int height, bool colour, bool depth, bool stencil, uint32_t argb,
	float z, uint32_t stencil_value)
{
	if ((!colour && !depth && !stencil) || width <= 0 || height <= 0) {
		return;
	}
	const SdlTarget target = TargetSet ? CurrentTarget : Back_Buffer_Target();
	Invalidate_Water_Snapshots(colour ? target.Colour : NULL, depth ? target.Depth : NULL);
	Command command;
	memset(&command, 0, sizeof(command));
	command.IsRect = true;
	command.Target = Target_Index();
	command.Rect[0] = x;
	command.Rect[1] = y;
	command.Rect[2] = width;
	command.Rect[3] = height;
	command.Colour = colour;
	command.Depth = depth;
	command.Stencil = stencil;
	command.Argb = argb;
	command.Z = z;
	command.StencilValue = stencil_value;
	Commands.push_back(command);
}

// Bit 0 colour, bit 1 depth, bit 2 stencil: what the clear draw writes.  Depth and stencil always pass
// and are replaced; what is not cleared is masked off.
SDL_GPUGraphicsPipeline * SdlGpuFrame::Clear_Pipeline(unsigned int which)
{
	if (ClearPipelines[which] != NULL) {
		return ClearPipelines[which];
	}
	std::string log;
	if (ClearVertex == NULL) {
		std::vector<unsigned char> spirv;
		if (!SDL3_Compile_HLSL_To_SPIRV(CLEAR_VERTEX_HLSL, true, spirv, log)
			|| (ClearVertex = SDL3_Create_Shader(GpuDevice, spirv, true, log)) == NULL) {
			fprintf(stderr, "SdlGpuFrame: the clear draw's vertex program: %s\n", log.c_str());
			return NULL;
		}
	}
	if (ClearPixel == NULL) {
		std::vector<unsigned char> spirv;
		if (!SDL3_Compile_HLSL_To_SPIRV(CLEAR_PIXEL_HLSL, false, spirv, log)
			|| (ClearPixel = SDL3_Create_Shader(GpuDevice, spirv, false, log)) == NULL) {
			fprintf(stderr, "SdlGpuFrame: the clear draw's pixel program: %s\n", log.c_str());
			return NULL;
		}
	}
	SDL_GPUColorTargetDescription target;
	SDL_zero(target);
	target.format = (SDL_GPUTextureFormat)Target_Format();
	target.blend_state.enable_color_write_mask = true;
	target.blend_state.color_write_mask = (which & 1) ? 0xF : 0;
	SDL_GPUGraphicsPipelineCreateInfo info;
	SDL_zero(info);
	info.vertex_shader = ClearVertex;
	info.fragment_shader = ClearPixel;
	info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
	info.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
	info.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
	info.depth_stencil_state.enable_depth_test = (which & 2) != 0;
	info.depth_stencil_state.enable_depth_write = (which & 2) != 0;
	info.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_ALWAYS;
	info.depth_stencil_state.enable_stencil_test = (which & 4) != 0;
	SDL_GPUStencilOpState replace;
	replace.fail_op = SDL_GPU_STENCILOP_REPLACE;
	replace.pass_op = SDL_GPU_STENCILOP_REPLACE;
	replace.depth_fail_op = SDL_GPU_STENCILOP_REPLACE;
	replace.compare_op = SDL_GPU_COMPAREOP_ALWAYS;
	info.depth_stencil_state.front_stencil_state = replace;
	info.depth_stencil_state.back_stencil_state = replace;
	info.depth_stencil_state.compare_mask = 0xFF;
	info.depth_stencil_state.write_mask = 0xFF;
	info.target_info.color_target_descriptions = &target;
	info.target_info.num_color_targets = 1;
	info.target_info.depth_stencil_format = (SDL_GPUTextureFormat)DepthFormat;
	info.target_info.has_depth_stencil_target = true;
	ClearPipelines[which] = SDL_CreateGPUGraphicsPipeline(GpuDevice, &info);
	if (ClearPipelines[which] == NULL) {
		fprintf(stderr, "SdlGpuFrame: the clear draw's pipeline: %s\n", SDL_GetError());
	}
	return ClearPipelines[which];
}

void SdlGpuFrame::Record_Draw(const SdlRecordedDraw & draw)
{
	Command command;
	memset(&command, 0, sizeof(command));
	command.IsDraw = true;
	command.Draw = (uint32_t)Draws.size();
	command.Target = Target_Index();
	Draws.push_back(draw);
	Commands.push_back(command);
}

void SdlGpuFrame::Release_After_Batch(SDL_GPUTexture * texture, SDL_GPUBuffer * buffer)
{
	if (texture != NULL) {
		// Forget source identities before SDL can recycle their handles. A queued snapshot/draw keeps
		// its textures alive through the same submission; SDL retires them after GPU completion.
		for (size_t i = WaterSnapshots.size(); i-- > 0; ) {
			const WaterSnapshot &snapshot = WaterSnapshots[i];
			if (snapshot.Source.Colour == texture || snapshot.Source.Depth == texture) {
				if (snapshot.Colour) DeadTextures.push_back(snapshot.Colour);
				if (snapshot.Depth) DeadTextures.push_back(snapshot.Depth);
				WaterSnapshots.erase(WaterSnapshots.begin() + i);
			}
		}
		DeadTextures.push_back(texture);
	}
	if (buffer != NULL) DeadBuffers.push_back(buffer);
}

bool SdlGpuFrame::Batch_Is_Full()
{
	if (StreamBytes.size() + UploadBytes.size() + ConstantBytes.size() > BATCH_LIMIT) {
		return true;
	}
	if (BatchDrawLimit > 0 && Draws.size() >= BatchDrawLimit) {
		++DrawLimitFlushes;
		return true;
	}
	return false;
}

void SdlGpuFrame::End_Batch()
{
	Commands.clear();
	Targets.clear();
	Draws.clear();
	StreamBytes.clear();
	UploadBytes.clear();
	ConstantBytes.clear();
	memset(LastConstants,0,sizeof(LastConstants));memset(LastConstantsSize,0,sizeof(LastConstantsSize));
	Uploads.clear();
	// The batch that could use these has been submitted: SDL keeps each until the GPU is done with it.
	for (size_t i = 0; i < DeadTextures.size(); ++i) SDL_ReleaseGPUTexture(GpuDevice, DeadTextures[i]);
	for (size_t i = 0; i < DeadBuffers.size(); ++i) SDL_ReleaseGPUBuffer(GpuDevice, DeadBuffers[i]);
	DeadTextures.clear();
	DeadBuffers.clear();
	++BatchNumber;
}

// The copy pass: the staging stream into its GPU buffer, and the queued uploads into the GPU copies,
// all from one transfer buffer.  Then the passes.
bool SdlGpuFrame::Record_Batch(SDL_GPUCommandBuffer * commands)
{
	if (Commands.empty() && Uploads.empty()) {
		return true;
	}
	const uint32_t stream_size = (uint32_t)((StreamBytes.size() + 3) & ~(size_t)3);
	const uint32_t upload_base = (uint32_t)((stream_size + STAGING_ALIGNMENT - 1) & ~(STAGING_ALIGNMENT - 1));
	const uint32_t total = upload_base + (uint32_t)UploadBytes.size();
	if (total > 0) {
		if (TransferSize < total) {
			if (Transfer != NULL) SDL_ReleaseGPUTransferBuffer(GpuDevice, Transfer);
			TransferSize = total + total / 2;
			SDL_GPUTransferBufferCreateInfo info;
			SDL_zero(info);
			info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
			info.size = TransferSize;
			Transfer = SDL_CreateGPUTransferBuffer(GpuDevice, &info);
			if (Transfer == NULL) {
				TransferSize = 0;
				return false;
			}
		}
		if (StreamBufferSize < stream_size) {
			if (StreamBuffer != NULL) SDL_ReleaseGPUBuffer(GpuDevice, StreamBuffer);
			StreamBufferSize = stream_size + stream_size / 2;
			SDL_GPUBufferCreateInfo info;
			SDL_zero(info);
			info.usage = SDL_GPU_BUFFERUSAGE_VERTEX | SDL_GPU_BUFFERUSAGE_INDEX;
			info.size = StreamBufferSize;
			StreamBuffer = SDL_CreateGPUBuffer(GpuDevice, &info);
			if (StreamBuffer == NULL) {
				StreamBufferSize = 0;
				return false;
			}
		}
		// Cycled: the batch before may still be reading the last contents.
		uint8_t * mapped = (uint8_t *)SDL_MapGPUTransferBuffer(GpuDevice, Transfer, true);
		if (mapped == NULL) {
			return false;
		}
		if (!StreamBytes.empty()) memcpy(mapped, &StreamBytes[0], StreamBytes.size());
		if (!UploadBytes.empty()) memcpy(mapped + upload_base, &UploadBytes[0], UploadBytes.size());
		SDL_UnmapGPUTransferBuffer(GpuDevice, Transfer);

		SDL_GPUCopyPass * copy = SDL_BeginGPUCopyPass(commands);
		if (copy == NULL) {
			return false;
		}
		if (stream_size > 0) {
			SDL_GPUTransferBufferLocation source = { Transfer, 0 };
			SDL_GPUBufferRegion region = { StreamBuffer, 0, stream_size };
			SDL_UploadToGPUBuffer(copy, &source, &region, true);
		}
		for (size_t i = 0; i < Uploads.size(); ++i) {
			const Upload & upload = Uploads[i];
			if (upload.Buffer != NULL) {
				SDL_GPUTransferBufferLocation source = { Transfer, upload_base + upload.Offset };
				SDL_GPUBufferRegion region = { upload.Buffer, 0, upload.Size };
				SDL_UploadToGPUBuffer(copy, &source, &region, true);
			}
			else {
				SDL_GPUTextureTransferInfo source;
				SDL_zero(source);
				source.transfer_buffer = Transfer;
				source.offset = upload_base + upload.Offset;
				SDL_GPUTextureRegion region;
				SDL_zero(region);
				region.texture = upload.Texture;
				region.mip_level = upload.Level;
				region.w = upload.Width;
				region.h = upload.Height;
				region.d = 1;
				// Only a complete replacement may discard the old backing texture.
				SDL_UploadToGPUTexture(copy, &source, &region, upload.CycleTexture);
			}
		}
		SDL_EndGPUCopyPass(copy);
	}
	return Record_Passes(commands, StreamBuffer);
}

// A pass over the target with these load operations.
bool SdlGpuFrame::Begin_Pass(SDL_GPUCommandBuffer * commands, const SdlTarget & target, bool clear_colour, bool clear_depth,
	bool clear_stencil, uint32_t argb, float z, uint32_t stencil_value, SDL_GPURenderPass ** pass)
{
	SDL_GPUColorTargetInfo colour;
	SDL_zero(colour);
	colour.texture = target.Colour;
	colour.clear_color.a = (float)((argb >> 24) & 0xFF) / 255.0f;
	colour.clear_color.r = (float)((argb >> 16) & 0xFF) / 255.0f;
	colour.clear_color.g = (float)((argb >> 8) & 0xFF) / 255.0f;
	colour.clear_color.b = (float)(argb & 0xFF) / 255.0f;
	colour.load_op = clear_colour ? SDL_GPU_LOADOP_CLEAR : SDL_GPU_LOADOP_LOAD;
	colour.store_op = SDL_GPU_STOREOP_STORE;
	SDL_GPUDepthStencilTargetInfo depth;
	SDL_zero(depth);
	depth.texture = target.Depth;
	depth.clear_depth = z;
	depth.clear_stencil = (Uint8)stencil_value;
	depth.load_op = clear_depth ? SDL_GPU_LOADOP_CLEAR : SDL_GPU_LOADOP_LOAD;
	depth.store_op = SDL_GPU_STOREOP_STORE;
	const unsigned format = Target_Depth_Format(target);
	const bool has_stencil = format == SDL_GPU_TEXTUREFORMAT_D24_UNORM_S8_UINT
		|| format == SDL_GPU_TEXTUREFORMAT_D32_FLOAT_S8_UINT;
	depth.stencil_load_op = !has_stencil ? SDL_GPU_LOADOP_DONT_CARE
		: clear_stencil ? SDL_GPU_LOADOP_CLEAR : SDL_GPU_LOADOP_LOAD;
	depth.stencil_store_op = has_stencil ? SDL_GPU_STOREOP_STORE : SDL_GPU_STOREOP_DONT_CARE;
	*pass = SDL_BeginGPURenderPass(commands, target.Colour ? &colour : NULL, target.Colour ? 1 : 0,
		target.Depth ? &depth : NULL);
	return *pass != NULL;
}

SdlTarget SdlGpuFrame::Back_Buffer_Target() const
{
	SdlTarget target = { BackBuffer, DepthStencil, BackWidth, BackHeight, DepthFormat };
	return target;
}

unsigned int SdlGpuFrame::Target_Depth_Format(const SdlTarget &target) const
{
	return target.Depth ? (target.DepthFormat ? target.DepthFormat : DepthFormat) : 0;
}

void SdlGpuFrame::Set_Target(const SdlTarget & target)
{
	CurrentTarget = target;
	TargetSet = true;
}

uint32_t SdlGpuFrame::Target_Index()
{
	const SdlTarget target = TargetSet ? CurrentTarget : Back_Buffer_Target();
	for (size_t i = Targets.size(); i-- > 0; ) {
		if (Targets[i].Colour == target.Colour && Targets[i].Depth == target.Depth
			&& Targets[i].DepthFormat == target.DepthFormat && Targets[i].Width == target.Width && Targets[i].Height == target.Height) {
			return (uint32_t)i;
		}
	}
	Targets.push_back(target);
	return (uint32_t)(Targets.size() - 1);
}

SDL_GPUTexture * SdlGpuFrame::Depth_For(unsigned int width, unsigned int height)
{
	if (width == BackWidth && height == BackHeight) {
		return DepthStencil;
	}
	for (size_t i = 0; i < ScratchDepths.size(); ++i) {
		if (ScratchDepths[i].Width == width && ScratchDepths[i].Height == height) {
			return ScratchDepths[i].Texture;
		}
	}
	SDL_GPUTextureCreateInfo info;
	SDL_zero(info);
	info.type = SDL_GPU_TEXTURETYPE_2D;
	info.format = (SDL_GPUTextureFormat)DepthFormat;
	info.usage = Depth_Usage();
	info.width = width;
	info.height = height;
	info.layer_count_or_depth = 1;
	info.num_levels = 1;
	info.sample_count = SDL_GPU_SAMPLECOUNT_1;
	ScratchDepth scratch = { SDL_CreateGPUTexture(GpuDevice, &info), width, height };
	if (scratch.Texture != NULL) {
		ScratchDepths.push_back(scratch);
	}
	return scratch.Texture;
}

void SdlGpuFrame::Record_Blit(SDL_GPUTexture * source, const int32_t source_rect[4], SDL_GPUTexture * destination,
	const int32_t destination_rect[4], bool linear)
{
	Invalidate_Water_Snapshots(destination, NULL);
	Command command;
	memset(&command, 0, sizeof(command));
	command.IsBlit = true;
	command.BlitSource = source;
	command.BlitDestination = destination;
	memcpy(command.Rect, source_rect, sizeof(command.Rect));
	memcpy(command.BlitRect, destination_rect, sizeof(command.BlitRect));
	command.BlitLinear = linear;
	Commands.push_back(command);
}

// The records in order.  A pass covers one target: it is opened by the first draw or clear draw on its
// target, with a waiting whole-target clear of that target as its load operation, and it ends at a
// blit, a whole-target clear, a change of target, or the end.  A clear that no draw on its target
// follows gets a pass of its own.  Every pass has a depth-stencil: a pipeline's depth format is always
// the frame's, and a draw with no depth surface bound has its depth state off instead.
bool SdlGpuFrame::Record_Passes(SDL_GPUCommandBuffer * commands, SDL_GPUBuffer * stream)
{
	SDL_GPURenderPass * pass = NULL;
	uint32_t pass_target = 0;
	// A whole-target clear waits to be the load operation of the next pass over its own target.
	bool pending = false, clear_colour = false, clear_depth = false, clear_stencil = false;
	uint32_t pending_target = 0, argb = 0, stencil_value = 0;
	float z = 1.0f;

	// What the pass has bound, to bind only a change.  Pushed uniforms outlive a pass (SDL3 keeps them for
	// the rest of the command buffer), so those are tracked for the whole batch.
	const SdlRecordedDraw * last = NULL;
	uint32_t pushed_vertex = 0xFFFFFFFFu, pushed_water_vertex = 0xFFFFFFFFu, pushed_pixel = 0xFFFFFFFFu;
	uint32_t pushed_water_pixel[SdlRecordedDraw::WATER_PIXEL_BLOCKS];
	memset(pushed_water_pixel,0xFF,sizeof(pushed_water_pixel));

	for (size_t i = 0; i <= Commands.size(); ++i) {
		const bool end = i == Commands.size();
		const Command * command = end ? NULL : &Commands[i];
		const bool transfer = !end && (command->IsBlit || command->IsDepthSnapshot);
		const bool whole_clear = !end && !command->IsDraw && !command->IsRect && !transfer;

		// A pass ends at a blit, at a whole-target clear, at a change of target, and at the end.
		if (pass != NULL && (end || transfer || whole_clear || command->Target != pass_target)) {
			SDL_EndGPURenderPass(pass);
			pass = NULL;
		}
		// A waiting clear whose target is not the next thing drawn into gets a pass of its own.
		if (pending && (end || transfer || command->Target != pending_target)) {
			if (!Begin_Pass(commands, Targets[pending_target], clear_colour, clear_depth, clear_stencil, argb, z,
				stencil_value, &pass)) {
				return false;
			}
			SDL_EndGPURenderPass(pass);
			pass = NULL;
			pending = clear_colour = clear_depth = clear_stencil = false;
		}
		if (end) {
			break;
		}
		if (command->IsDepthSnapshot) {
			if (!Resolve_Scene_Depth(commands, command->BlitSource, command->BlitDestination,
				(unsigned int)command->Rect[2], (unsigned int)command->Rect[3])) return false;
			continue;
		}
		if (command->IsBlit) {
			SDL_GPUBlitInfo blit;
			SDL_zero(blit);
			blit.source.texture = command->BlitSource;
			blit.source.x = (Uint32)command->Rect[0];
			blit.source.y = (Uint32)command->Rect[1];
			blit.source.w = (Uint32)command->Rect[2];
			blit.source.h = (Uint32)command->Rect[3];
			blit.destination.texture = command->BlitDestination;
			blit.destination.x = (Uint32)command->BlitRect[0];
			blit.destination.y = (Uint32)command->BlitRect[1];
			blit.destination.w = (Uint32)command->BlitRect[2];
			blit.destination.h = (Uint32)command->BlitRect[3];
			blit.load_op = SDL_GPU_LOADOP_LOAD;
			blit.filter = command->BlitLinear ? SDL_GPU_FILTER_LINEAR : SDL_GPU_FILTER_NEAREST;
			SDL_BlitGPUTexture(commands, &blit);
			continue;
		}
		if (whole_clear) {
			if (command->Colour) { clear_colour = true; argb = command->Argb; }
			if (command->Depth) { clear_depth = true; z = command->Z; }
			if (command->Stencil) { clear_stencil = true; stencil_value = command->StencilValue; }
			pending = true;
			pending_target = command->Target;
			continue;
		}
		if (pass == NULL) {
			if (!Begin_Pass(commands, Targets[command->Target], pending && clear_colour, pending && clear_depth,
				pending && clear_stencil, argb, z, stencil_value, &pass)) {
				return false;
			}
			pending = clear_colour = clear_depth = clear_stencil = false;
			pass_target = command->Target;
			last = NULL;
		}

		if (Commands[i].IsRect) {
			const Command & clear = Commands[i];
			SDL_GPUGraphicsPipeline * pipeline = Clear_Pipeline((clear.Colour ? 1 : 0) | (clear.Depth ? 2 : 0) | (clear.Stencil ? 4 : 0));
			if (pipeline == NULL) {
				continue;
			}
			SDL_BindGPUGraphicsPipeline(pass, pipeline);
			SDL_GPUViewport viewport = { 0.0f, 0.0f, (float)Targets[pass_target].Width, (float)Targets[pass_target].Height,
				0.0f, 1.0f };
			SDL_SetGPUViewport(pass, &viewport);
			SDL_Rect scissor = { clear.Rect[0], clear.Rect[1], clear.Rect[2], clear.Rect[3] };
			SDL_SetGPUScissor(pass, &scissor);
			SDL_SetGPUStencilReference(pass, (Uint8)clear.StencilValue);
			const float values[8] = {
				(float)((clear.Argb >> 16) & 0xFF) / 255.0f, (float)((clear.Argb >> 8) & 0xFF) / 255.0f,
				(float)(clear.Argb & 0xFF) / 255.0f, (float)((clear.Argb >> 24) & 0xFF) / 255.0f,
				clear.Z, 0.0f, 0.0f, 0.0f };
			SDL_PushGPUVertexUniformData(commands, 0, values, sizeof(values));
			SDL_PushGPUFragmentUniformData(commands, 0, values, 16);
			SDL_DrawGPUPrimitives(pass, 3, 1, 0, 0);
			// Everything the draws set is the clear's now: the next draw binds it all again.
			last = NULL;
			pushed_vertex = pushed_water_vertex = pushed_pixel = 0xFFFFFFFFu;
			memset(pushed_water_pixel,0xFF,sizeof(pushed_water_pixel));
			continue;
		}
		const SdlRecordedDraw & draw = Draws[Commands[i].Draw];
		if (last == NULL || last->Pipeline != draw.Pipeline) {
			SDL_BindGPUGraphicsPipeline(pass, draw.Pipeline);
		}
		if (last == NULL || memcmp(last->Viewport, draw.Viewport, sizeof(draw.Viewport)) != 0) {
			SDL_GPUViewport viewport = { draw.Viewport[0], draw.Viewport[1], draw.Viewport[2], draw.Viewport[3],
				draw.Viewport[4], draw.Viewport[5] };
			SDL_SetGPUViewport(pass, &viewport);
		}
		if (last == NULL || memcmp(last->Scissor, draw.Scissor, sizeof(draw.Scissor)) != 0) {
			SDL_Rect scissor = { draw.Scissor[0], draw.Scissor[1], draw.Scissor[2], draw.Scissor[3] };
			SDL_SetGPUScissor(pass, &scissor);
		}
		if (last == NULL || last->StencilReference != draw.StencilReference) {
			SDL_SetGPUStencilReference(pass, (Uint8)draw.StencilReference);
		}
		if (last == NULL || last->BlendFactor != draw.BlendFactor) {
			SDL_FColor factor = { (float)((draw.BlendFactor >> 16) & 0xFF) / 255.0f,
				(float)((draw.BlendFactor >> 8) & 0xFF) / 255.0f, (float)(draw.BlendFactor & 0xFF) / 255.0f,
				(float)((draw.BlendFactor >> 24) & 0xFF) / 255.0f };
			SDL_SetGPUBlendConstants(pass, factor);
		}
		SDL_GPUBuffer * vertices = draw.VertexBuffer != NULL ? draw.VertexBuffer : stream;
		if (last == NULL || last->VertexBuffer != draw.VertexBuffer || last->VertexOffset != draw.VertexOffset) {
			SDL_GPUBufferBinding binding = { vertices, draw.VertexOffset };
			SDL_BindGPUVertexBuffers(pass, 0, &binding, 1);
		}
		if (draw.IndexSize != 0 && (last == NULL || last->IndexSize == 0 || last->IndexBuffer != draw.IndexBuffer
			|| last->IndexOffset != draw.IndexOffset || last->IndexSize != draw.IndexSize)) {
			SDL_GPUBufferBinding binding = { draw.IndexBuffer != NULL ? draw.IndexBuffer : stream, draw.IndexOffset };
			SDL_BindGPUIndexBuffer(pass, &binding,
				draw.IndexSize == 4 ? SDL_GPU_INDEXELEMENTSIZE_32BIT : SDL_GPU_INDEXELEMENTSIZE_16BIT);
		}
		if (draw.SamplerCount > 0 && (last == NULL || last->SamplerCount != draw.SamplerCount
			|| memcmp(last->Textures, draw.Textures, sizeof(draw.Textures[0]) * draw.SamplerCount) != 0
			|| memcmp(last->Samplers, draw.Samplers, sizeof(draw.Samplers[0]) * draw.SamplerCount) != 0)) {
			SDL_GPUTextureSamplerBinding bindings[SdlRecordedDraw::MAXIMUM_SAMPLERS];
			for (uint32_t slot = 0; slot < draw.SamplerCount; ++slot) {
				bindings[slot].texture = draw.Textures[slot];
				bindings[slot].sampler = draw.Samplers[slot];
			}
			SDL_BindGPUFragmentSamplers(pass, 0, bindings, draw.SamplerCount);
		}
		if (draw.VertexSamplerCount > 0 && (last == NULL || last->VertexSamplerCount != draw.VertexSamplerCount
			|| memcmp(last->VertexTextures, draw.VertexTextures, sizeof(draw.VertexTextures[0]) * draw.VertexSamplerCount) != 0
			|| memcmp(last->VertexSamplers, draw.VertexSamplers, sizeof(draw.VertexSamplers[0]) * draw.VertexSamplerCount) != 0)) {
			SDL_GPUTextureSamplerBinding bindings[SdlRecordedDraw::MAXIMUM_SAMPLERS];
			for (uint32_t slot = 0; slot < draw.VertexSamplerCount; ++slot) {
				bindings[slot].texture = draw.VertexTextures[slot];
				bindings[slot].sampler = draw.VertexSamplers[slot];
			}
			SDL_BindGPUVertexSamplers(pass, 0, bindings, draw.VertexSamplerCount);
		}
		if (draw.VertexConstantsSize != 0 && draw.VertexConstants != pushed_vertex) {
			SDL_PushGPUVertexUniformData(commands, 0, &ConstantBytes[draw.VertexConstants], draw.VertexConstantsSize);
			pushed_vertex = draw.VertexConstants;
		}
		if (draw.VertexWaterConstantsSize != 0 && draw.VertexWaterConstants != pushed_water_vertex) {
			SDL_PushGPUVertexUniformData(commands, 1, &ConstantBytes[draw.VertexWaterConstants], draw.VertexWaterConstantsSize);
			pushed_water_vertex = draw.VertexWaterConstants;
		}
		if (draw.PixelConstantsSize != 0 && draw.PixelConstants != pushed_pixel) {
			SDL_PushGPUFragmentUniformData(commands, 0, &ConstantBytes[draw.PixelConstants], draw.PixelConstantsSize);
			pushed_pixel = draw.PixelConstants;
		}
		for(unsigned block=0;block<SdlRecordedDraw::WATER_PIXEL_BLOCKS;++block) {
			if(draw.PixelWaterConstantsSize[block]!=0 && draw.PixelWaterConstants[block]!=pushed_water_pixel[block]) {
				SDL_PushGPUFragmentUniformData(commands,block+1,&ConstantBytes[draw.PixelWaterConstants[block]],draw.PixelWaterConstantsSize[block]);
				pushed_water_pixel[block]=draw.PixelWaterConstants[block];
			}
		}
		if (draw.IndexSize != 0) {
			SDL_DrawGPUIndexedPrimitives(pass, draw.Count, 1, draw.First, draw.BaseVertex, 0);
		}
		else {
			SDL_DrawGPUPrimitives(pass, draw.Count, 1, draw.First, 0);
		}
		last = &draw;
	}
	return true;
}
