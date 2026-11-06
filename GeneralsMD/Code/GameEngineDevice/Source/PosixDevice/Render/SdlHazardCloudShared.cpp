// Shared hazard cloud volume with immutable, incrementally baked snapshots.
// Distributed under the repository's GPL-3.0 terms; see LICENSE.md.
#include "PreRTS.h"
#include "Platform/HazardCloudShared.h"
#include "Platform/HazardCloudSharedModel.h"
#include "Platform/HazardCloudBake.h"
#include "SdlHazardCloudSharedShader.h"
#include "Platform/HazardCloudCore.h"
#include "SdlGpuFrame.h"
#include "SdlHazardCloudEmbers.h"
#include "PosixDevice9.h"
#include "SdlResourceMirror.h"
#include "WW3D2/dx8wrapper.h"
#include "WW3D2/sdl3shadercompile.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <vector>

namespace HazardCloudShared {
namespace {
struct Constants {
	HazardCloud::Uniforms common;
	float minimum[4], maximum[4], grid[4], bins[4], data[4];
};
static_assert(sizeof(Constants) == 560, "shared cloud shader constants");
class CameraCore final : public HazardCloud::LayerCore {
public:
	bool initialize() override { initialized = true; return true; }
	bool render(const HazardCloud::Camera &) override { return false; }
	std::uint64_t estimatedCacheBytes() const override { return 0; }
	void shutdown() override {}
	using LayerCore::buildUniforms;
};
constexpr SDL_GPUTextureFormat VolumeFormat = SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT;
constexpr SDL_GPUTextureFormat DataFormat = SDL_GPU_TEXTUREFORMAT_R32G32B32A32_FLOAT;
constexpr unsigned DataWidth = 1024;

bool sceneTarget(SdlGpuFrame *frame, SdlTarget &target, std::string &error)
{
	auto *device = dynamic_cast<PosixDevice9 *>(DX8Wrapper::_Get_D3D_Device());
	if (!device || device->Get_Gpu() != frame) { error = "the shared cloud GPU device was replaced"; return false; }
	IDirect3DSurface9 *current = nullptr, *back = nullptr, *depth = nullptr;
	device->GetRenderTarget(0,&current);
	device->GetBackBuffer(0,0,D3DBACKBUFFER_TYPE_MONO,&back);
	device->GetDepthStencilSurface(&depth);
	SdlResourceMirrors *mirrors = device->Get_Mirrors();
	target = {};
	if (current && current == back) target = frame->Back_Buffer_Target();
	else if (current && mirrors) {
		D3DSURFACE_DESC description;
		if (current->GetDesc(&description) == D3D_OK) {
			target.Width = description.Width; target.Height = description.Height;
			IDirect3DTexture9 *texture = nullptr;
			if (current->GetContainer(IID_IDirect3DTexture9,reinterpret_cast<void **>(&texture)) == D3D_OK && texture) {
				IDirect3DSurface9 *level = nullptr;
				texture->GetSurfaceLevel(0,&level);
				if (level == current) target.Colour = mirrors->Texture(texture,error);
				if (level) level->Release();
				texture->Release();
			} else target.Colour = mirrors->Surface(current,false,error);
		}
	}
	if (target.Colour && mirrors) {
		D3DSURFACE_DESC description;
		if (depth && mirrors->Has_Copy(depth) && depth->GetDesc(&description) == D3D_OK
			&& description.Width == target.Width && description.Height == target.Height)
			target.Depth = mirrors->Surface(depth,true,error);
		else target.Depth = frame->Depth_For(target.Width,target.Height);
	}
	if (current) current->Release();
	if (back) back->Release();
	if (depth) depth->Release();
	if (!target.Colour || !target.Depth) {
		if (error.empty()) error = "could not resolve shared cloud scene target";
		return false;
	}
	return true;
}
}

class SdlRenderer final : public Renderer {
public:
	SdlRenderer();
	~SdlRenderer() override;
	bool initialize(std::string &error) override;
	bool initialize(SdlGpuFrame *frame, const RendererConfig &config, std::string &error);
	bool render(const HazardCloud::Camera &camera, const Model &model, float seconds, std::string &error) override;
	void setTerrain(const float *heights, unsigned side) override;
	void setHeatEnabled(bool enabled) override;
	void setBodies(const HazardCloud::Body *bodies, unsigned count) override;
	void reset() override;
	void shutdown() override;
	const RendererStats &stats() const override;
private:
	struct Impl;
	std::unique_ptr<Impl> impl;
};

struct SdlRenderer::Impl {
	SdlGpuFrame *frame = nullptr;
	SDL_GPUDevice *gpu = nullptr;
	RendererConfig config;
	RendererStats statistics;
	CameraCore cameraCore;
	SDL_GPUShader *vertex = nullptr, *pixels[5] = {};
	SDL_GPUGraphicsPipeline *pipelines[5] = {};
	SDL_GPUSampler *point = nullptr, *linear = nullptr;
	SdlTarget material = {}, low = {}, background = {};
	struct Snapshot {
		SdlTarget lighting = {}, sources = {}, bins = {}, indices = {}, flow = {}, terrain = {};
		std::vector<float> terrainValues;
		std::vector<Source> fields;
		Bounds bounds = {};
		Grid grid;
		float seconds = 0;
		unsigned activeSources = 0, terrainSide = 0;
		bool nuclear = false;
		std::uint64_t usedBatch = ~std::uint64_t(0);
	} snapshots[2];
	unsigned front = 0, pending = 1;
	bool frontValid = false, baking = false, rebakeRequested = true;
	BakeSlices progress;
	long long pendingTick = -1;
	std::vector<float> terrainValues, previousSources, lastRenderSources;
	std::vector<HazardCloud::Body> bodies;
	HazardCloudFlow::Flow wake;
	unsigned terrainSide = 0;
	bool terrainDirty = false;
	long long lastBakeTick = -1;
	float lastTime = -1;
	Bounds previousBounds = {};
	bool boundsValid = false;
	std::uint64_t usedBatch = ~std::uint64_t(0);

	void invalidate()
	{
		frontValid = baking = false;
		rebakeRequested = true;
		progress.cancel();
		lastBakeTick = -1;
		statistics.bakedSources = statistics.pendingSlices = 0;
		statistics.publicationAgeSeconds = 0;
	}
	void constantsFor(Constants &constants, const Snapshot &snapshot) const
	{
		const Bounds &bounds = snapshot.bounds;
		const Grid &grid = snapshot.grid;
		constants.common.Scene[3] = snapshot.seconds; constants.common.Settings[0] = 1;
		constants.common.Interaction[0] = bounds.minX; constants.common.Interaction[1] = bounds.minY;
		constants.common.Interaction[2] = 1.0f / (bounds.maxX - bounds.minX);
		constants.common.Interaction[3] = 1.0f / (bounds.maxY - bounds.minY);
		constants.common.FlowSettings[0] = 1;
		constants.common.FlowSettings[1] = HazardCloud::MaximumDisplacement;
		constants.minimum[0] = bounds.minX; constants.minimum[1] = bounds.minY; constants.minimum[2] = bounds.minZ;
		constants.maximum[0] = bounds.maxX; constants.maximum[1] = bounds.maxY; constants.maximum[2] = bounds.maxZ;
		constants.grid[0] = config.volumeSide; constants.grid[1] = config.slices;
		constants.grid[2] = 6; constants.grid[3] = config.slices / 6;
		constants.bins[0] = grid.minX; constants.bins[1] = grid.minY;
		constants.bins[2] = grid.cellSize; constants.bins[3] = grid.width;
		constants.data[0] = grid.height; constants.data[1] = snapshot.fields.size();
		constants.data[2] = snapshot.terrainSide;
	}
	void updateMetadataBytes()
	{
		statistics.metadataBytes = 0;
		for (const auto &snapshot : snapshots) {
			statistics.metadataBytes += std::uint64_t(snapshot.terrain.Width) * snapshot.terrain.Height * 4;
			const SdlTarget *targets[] = {&snapshot.sources, &snapshot.bins, &snapshot.indices, &snapshot.flow};
			for (const auto *target : targets)
				statistics.metadataBytes += std::uint64_t(target->Width) * target->Height * 16;
		}
	}

	void retire(SdlTarget &target)
	{
		if (target.Colour) frame->Release_After_Batch(target.Colour, nullptr);
		target = {};
	}
	bool texture(SdlTarget &target, unsigned width, unsigned height, SDL_GPUTextureFormat format,
				 bool renderable, std::string &error)
	{
		if (target.Colour && target.Width == width && target.Height == height) return true;
		SDL_GPUTextureCreateInfo info = {};
		info.type = SDL_GPU_TEXTURETYPE_2D;
		info.format = format;
		info.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER | (renderable ? SDL_GPU_TEXTUREUSAGE_COLOR_TARGET : 0);
		info.width = width; info.height = height;
		info.layer_count_or_depth = info.num_levels = 1;
		info.sample_count = SDL_GPU_SAMPLECOUNT_1;
		SDL_GPUTexture *made = SDL_CreateGPUTexture(gpu, &info);
		if (!made) { error = SDL_GetError(); return false; }
		retire(target);
		target = {made, nullptr, width, height};
		return true;
	}
	bool upload(SdlTarget &target, const std::vector<float> &values, unsigned width,
				unsigned components, std::string &error)
	{
		const unsigned texels = std::max<std::size_t>(1, (values.size() + components - 1) / components);
		const unsigned height = (texels + width - 1) / width;
		if (!texture(target, width, height, components == 1 ? SDL_GPU_TEXTUREFORMAT_R32_FLOAT : DataFormat,
					 false, error)) return false;
		const unsigned bytes = width * height * components * sizeof(float);
		std::uint32_t offset = 0;
		void *destination = frame->Upload_Space(bytes, offset);
		std::memset(destination, 0, bytes);
		if (!values.empty()) std::memcpy(destination, values.data(), values.size() * sizeof(float));
		frame->Queue_Texture_Upload(target.Colour, 0, width, height, offset);
		return true;
	}
	bool programs(std::string &error)
	{
		std::vector<unsigned char> spirv;
		if (!SDL3_Compile_HLSL_To_SPIRV(HazardCloud::vertexShader(), true, spirv, error)
			|| !(vertex = SDL3_Create_Shader(gpu, spirv, true, error))) return false;
		for (unsigned i = 0; i < 5; ++i) {
			if (!SDL3_Compile_HLSL_To_SPIRV(sharedPixelShader(static_cast<ShaderPass>(i)), false, spirv, error)
				|| !(pixels[i] = SDL3_Create_Shader(gpu, spirv, false, error))) {
				error = "shared cloud shader " + std::to_string(i) + ": " + error;
				return false;
			}
			SDL_GPUColorTargetDescription colour = {};
			colour.format = i < 3 ? VolumeFormat : static_cast<SDL_GPUTextureFormat>(SdlGpuFrame::Target_Format());
			if (i >= 3) {
				auto &blend = colour.blend_state;
				blend.enable_blend = true;
				blend.src_color_blendfactor = blend.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
				blend.dst_color_blendfactor = blend.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
				blend.color_blend_op = blend.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
			}
			SDL_GPUGraphicsPipelineCreateInfo description = {};
			description.vertex_shader = vertex; description.fragment_shader = pixels[i];
			description.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
			description.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
			description.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
			description.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_ALWAYS;
			description.target_info.color_target_descriptions = &colour;
			description.target_info.num_color_targets = 1;
			description.target_info.has_depth_stencil_target = i >= 3;
			description.target_info.depth_stencil_format = i >= 3
				? static_cast<SDL_GPUTextureFormat>(frame->Depth_Format()) : SDL_GPU_TEXTUREFORMAT_INVALID;
			pipelines[i] = SDL_CreateGPUGraphicsPipeline(gpu, &description);
			if (!pipelines[i]) { error = SDL_GetError(); return false; }
		}
		SDL_GPUSamplerCreateInfo sampler = {};
		sampler.min_filter = sampler.mag_filter = SDL_GPU_FILTER_NEAREST;
		sampler.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
		sampler.address_mode_u = sampler.address_mode_v = sampler.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
		point = SDL_CreateGPUSampler(gpu, &sampler);
		sampler.min_filter = sampler.mag_filter = SDL_GPU_FILTER_LINEAR;
		linear = SDL_CreateGPUSampler(gpu, &sampler);
		if (!point || !linear) { error = SDL_GetError(); return false; }
		return true;
	}
	void draw(unsigned pass, const SdlTarget &target, SDL_GPUTexture *depth, const Constants &constants,
			  std::uint32_t vertexOffset, const HazardCloud::Camera &camera, Snapshot &snapshot,
			  unsigned slice = ~0u)
	{
		SdlRecordedDraw command = {};
		command.Pipeline = pipelines[pass]; command.VertexOffset = vertexOffset; command.Count = 3;
		command.PixelConstants = frame->Constants(1, &constants, sizeof(constants));
		command.PixelConstantsSize = sizeof(constants);
		command.SamplerCount = 10;
		SDL_GPUTexture *placeholder = snapshot.sources.Colour;
		for (unsigned i = 0; i < 10; ++i) { command.Textures[i] = placeholder; command.Samplers[i] = point; }
		command.Textures[0] = depth;
		command.Textures[1] = snapshot.flow.Colour;
		command.Textures[2] = pass >= 3 ? low.Colour : placeholder;
		command.Textures[3] = pass >= 1 ? material.Colour : placeholder;
		command.Textures[4] = pass >= 2 ? snapshot.lighting.Colour : placeholder;
		command.Textures[5] = pass == 4 ? background.Colour : placeholder;
		command.Textures[6] = snapshot.terrainSide ? snapshot.terrain.Colour : placeholder;
		command.Textures[7] = snapshot.sources.Colour; command.Textures[8] = snapshot.bins.Colour;
		command.Textures[9] = snapshot.indices.Colour;
		if (pass >= 1) command.Samplers[3] = linear;
		if (pass >= 2) command.Samplers[4] = linear;
		if (pass == 4) command.Samplers[5] = linear;
		if (pass >= 3) {
			std::copy(constants.common.Viewport, constants.common.Viewport + 4, command.Viewport);
			command.Viewport[4] = camera.minZ; command.Viewport[5] = camera.maxZ;
			command.Scissor[0] = (std::min)(camera.x,target.Width);
			command.Scissor[1] = (std::min)(camera.y,target.Height);
			command.Scissor[2] = (std::min)(camera.width,target.Width - command.Scissor[0]);
			command.Scissor[3] = (std::min)(camera.height,target.Height - command.Scissor[1]);
		} else {
			command.Viewport[2] = target.Width; command.Viewport[3] = target.Height; command.Viewport[5] = 1;
			command.Scissor[2] = target.Width; command.Scissor[3] = target.Height;
			if (pass == 0 && slice != ~0u) {
				// Preserve atlas-global SV_Position; only limit shaded pixels.
				command.Scissor[0] = (slice % 6) * config.volumeSide;
				command.Scissor[1] = (slice / 6) * config.volumeSide;
				command.Scissor[2] = command.Scissor[3] = config.volumeSide;
			}
		}
		frame->Set_Target(target);
		// Injection overwrites every tile, and lighting overwrites its complete
		// target. Clearing an atlas here would erase earlier slices in the job.
		if (pass == 2) frame->Clear_Back_Buffer(true,false,false,0,1,0);
		frame->Record_Draw(command);
		snapshot.usedBatch = frame->Batch();
	}
};

SdlRenderer::SdlRenderer() : impl(new Impl) {}
SdlRenderer::~SdlRenderer() { shutdown(); }
bool SdlRenderer::initialize(std::string &error)
{
	auto *device = dynamic_cast<PosixDevice9 *>(DX8Wrapper::_Get_D3D_Device());
	if (!device || !device->Get_Gpu()) {
		error = "shared clouds require the current SDL GPU device";
		return false;
	}
	return initialize(device->Get_Gpu(), RendererConfig{}, error);
}
bool SdlRenderer::initialize(SdlGpuFrame *frame, const RendererConfig &config, std::string &error)
{
	shutdown();
	if (!frame || !frame->Device() || config.volumeSide < 32 || config.volumeSide > 512
		|| config.slices < 6 || config.slices > 96 || config.slices % 6
		|| !std::isfinite(config.binCellSize) || config.binCellSize <= 0
		|| !std::isfinite(config.bakesPerSecond) || config.bakesPerSecond <= 0
		|| !config.bakeSlicesPerFrame) {
		error = "invalid shared-volume renderer configuration"; return false;
	}
	impl.reset(new Impl);
	impl->frame = frame; impl->gpu = frame->Device(); impl->config = config;
	impl->cameraCore.setFlowDomain(-1,-1,1,1);
	impl->cameraCore.initialize();
	if (config.embers && !SdlHazardCloudEmbers::initialize(frame)) {
		error = "nuclear ember initialization failed";
		shutdown(); return false;
	}
	const unsigned width = config.volumeSide * 6, height = config.volumeSide * (config.slices / 6);
	if (!impl->programs(error)
		|| !impl->texture(impl->material,width,height,VolumeFormat,true,error)
		|| !impl->texture(impl->snapshots[0].lighting,width,height,VolumeFormat,true,error)
		|| !impl->texture(impl->snapshots[1].lighting,width,height,VolumeFormat,true,error)) {
		shutdown(); return false;
	}
	impl->statistics.volumeBytes = std::uint64_t(config.volumeSide) * config.volumeSide * config.slices * 24;
	return true;
}

void SdlRenderer::setTerrain(const float *heights, unsigned side)
{
	if (heights && side >= 2) impl->terrainValues.assign(heights,heights + std::size_t(side) * side);
	else impl->terrainValues.clear();
	impl->terrainSide = impl->terrainValues.empty() ? 0 : side;
	impl->terrainDirty = true;
}
void SdlRenderer::setHeatEnabled(bool enabled) { impl->config.heat = enabled; }
void SdlRenderer::setBodies(const HazardCloud::Body *bodies, unsigned count)
{
	if (bodies && count) impl->bodies.assign(bodies,bodies + count);
	else impl->bodies.clear();
}
void SdlRenderer::reset()
{
	impl->wake.reset();
	impl->invalidate();
	impl->lastRenderSources.clear();
	impl->lastTime = -1;
	impl->statistics.activeBodies = 0;
	impl->statistics.maxDisturbance = 0;
}
const RendererStats &SdlRenderer::stats() const { return impl->statistics; }

bool SdlRenderer::render(const HazardCloud::Camera &camera, const Model &model, float seconds, std::string &error)
{
	Impl &p = *impl;
	if (!p.frame || !p.gpu || !std::isfinite(seconds)) { error = "shared-volume renderer is not initialized"; return false; }
	SdlTarget target;
	if (!sceneTarget(p.frame,target,error)) return false;
	// Even resource/preflight failures must restore the scene target before
	// the caller records Classic fallback or any subsequent scene geometry.
	struct RestoreTarget {
		SdlGpuFrame *frame;
		SdlTarget target;
		~RestoreTarget() { frame->Set_Target(target); }
	} restore{p.frame, target};
	SDL_GPUTexture *depth = p.frame->Scene_Depth(target);
	if (!target.Colour || !depth) { error = "shared-volume renderer needs readable scene depth"; return false; }
	if (p.frame->Batch_Is_Full() && !p.frame->Flush()) { error = "could not submit scene"; return false; }
	if (!p.texture(p.low,(target.Width + 1) / 2,(target.Height + 1) / 2,VolumeFormat,true,error)) return false;
	Bounds bounds = model.bounds();
	// Model bounds include the taller heat envelope; smoke needs all slices in
	// its thin slab. Terrain extrema expand it without losing elevated support.
	float lowZ = bounds.minZ, highZ = lowZ + 0.01f;
	for (const auto &source : model.sources()) highZ = (std::max)(highZ,source.field.ground.z + source.field.height);
	if (!p.terrainValues.empty()) {
		const auto extrema = std::minmax_element(p.terrainValues.begin(),p.terrainValues.end());
		lowZ = (std::min)(lowZ,*extrema.first);
		float height = 0;
		for (const auto &source : model.sources()) height = (std::max)(height,source.field.height);
		highZ = (std::max)(highZ,*extrema.second + height);
	}
	bounds.minZ = lowZ; bounds.maxZ = highZ;
	const bool domainChanged = !p.boundsValid || std::memcmp(&bounds,&p.previousBounds,sizeof(bounds)) != 0;
	if (domainChanged) {
		p.wake.setDomain(bounds.minX,bounds.minY,bounds.maxX,bounds.maxY);
		p.previousBounds = bounds; p.boundsValid = true;
	}
	const float elapsed = p.lastTime >= 0 && seconds >= p.lastTime ? seconds - p.lastTime : 0;
	const bool rewound = p.lastTime >= 0 && seconds < p.lastTime;
	if (rewound) p.wake.reset();
	if (elapsed > 0) p.wake.step(elapsed,p.bodies.empty() ? nullptr : p.bodies.data(),p.bodies.size());
	p.statistics.activeBodies = p.wake.activeBodies();
	p.statistics.maxDisturbance = p.wake.maxDisturbance();
	p.lastTime = seconds;
	if (!p.wake.valid()) { error = "shared wake simulation is invalid"; return false; }
	const Grid grid = model.buildBins(p.config.binCellSize);
	if (!grid.valid()) { error = "shared-volume source bins could not be represented"; return false; }
	std::vector<float> sourceValues, binValues, indexValues;
	sourceValues.reserve(model.sources().size() * 16);
	p.statistics.sources = model.sources().size(); p.statistics.activeSources = 0; p.statistics.maximumBinSources = 0;
	bool nuclear = false;
	for (const auto &source : model.sources()) {
		const auto &field = source.field;
		if (source.active) { ++p.statistics.activeSources; nuclear |= field.palette >= 0.5f && field.palette < 1.5f; }
		const float values[] = {field.ground.x,field.ground.y,field.ground.z,field.radius,
			field.height,source.active ? field.density : 0,field.palette,field.formation,
			field.release,source.sourceTravel,source.dissipation,source.sourceAge,field.fringe,source.seed,source.terrain ? 1.0f : 0.0f,0};
		sourceValues.insert(sourceValues.end(),values,values + 16);
	}
	for (std::size_t i = 0; i + 1 < grid.offsets.size(); ++i) {
		const unsigned count = grid.offsets[i + 1] - grid.offsets[i];
		p.statistics.maximumBinSources = (std::max)(p.statistics.maximumBinSources,count);
		binValues.insert(binValues.end(),{float(grid.offsets[i]),float(count),0,0});
	}
	for (unsigned id : grid.indices) indexValues.push_back(float(id));
	while (indexValues.size() % 4) indexValues.push_back(0);
	bool supportChanged = sourceValues.size() != p.previousSources.size();
	if (!supportChanged) {
		for (std::size_t i = 0; i < sourceValues.size(); i += 16) {
			for (unsigned slot : {0u,1u,2u,3u,4u,6u,12u,13u,14u})
				supportChanged |= sourceValues[i + slot] != p.previousSources[i + slot];
		}
	}
	const long long bakeTick = static_cast<long long>(std::floor(double(seconds) * p.config.bakesPerSecond + 0.0001));
	const bool pausedChange = elapsed == 0 && sourceValues != p.lastRenderSources;
	p.rebakeRequested |= domainChanged || p.terrainDirty || supportChanged || pausedChange;
	p.lastRenderSources = sourceValues;
	p.previousSources = sourceValues;
	// Moving supports, births, terrain and domain changes coalesce into the next
	// snapshot. Never restart immutable partial work: continuous motion must
	// still publish within ceil(slices / budget) display renders.
	if (rewound) p.invalidate();
	Constants constants = {};
	float viewProjection[16];
	if (!p.cameraCore.buildUniforms(camera,target.Width,target.Height,constants.common,viewProjection)) {
		error = "invalid shared-volume scene camera"; return false;
	}
	const bool requestBake = !p.frontValid || bakeTick != p.lastBakeTick || p.rebakeRequested;
	if (!p.baking && requestBake) {
		auto &snapshot = p.snapshots[p.pending];
		// Uploads precede ALL draws in a batch. A slot about to become pending
		// may still have old front users recorded earlier in this same batch.
		if (snapshot.usedBatch == p.frame->Batch() && !p.frame->Flush()) {
			error = "could not submit prior shared-volume users"; return false;
		}
		if (!p.upload(snapshot.sources,sourceValues,DataWidth,4,error)
			|| !p.upload(snapshot.bins,binValues,DataWidth,4,error)
			|| !p.upload(snapshot.indices,indexValues,DataWidth,4,error)) return false;
		std::vector<float> wakes(p.wake.data(),p.wake.data() + p.wake.width() * p.wake.height() * 4);
		if (!p.upload(snapshot.flow,wakes,p.wake.width(),4,error)) return false;
		++p.statistics.interactionUploads;
		snapshot.terrainValues = p.terrainValues;
		if (p.terrainSide && !p.upload(snapshot.terrain,snapshot.terrainValues,p.terrainSide,1,error)) return false;
		snapshot.fields = model.sources();
		snapshot.bounds = bounds; snapshot.grid = grid; snapshot.seconds = seconds;
		snapshot.activeSources = p.statistics.activeSources; snapshot.nuclear = nuclear;
		snapshot.terrainSide = p.terrainSide;
		p.terrainDirty = false;
		p.pendingTick = bakeTick;
		p.rebakeRequested = false;
		p.progress.begin(p.config.slices,p.config.bakeSlicesPerFrame);
		p.baking = true;
		p.updateMetadataBytes();
	}
	std::uint32_t vertexOffset;
	std::memset(p.frame->Stage(16,vertexOffset),0,16);
	if (p.baking) {
		auto &snapshot = p.snapshots[p.pending];
		p.constantsFor(constants,snapshot);
		unsigned written = 0;
		// The allowance is per display render and never accumulates catch-up debt.
		while (!p.progress.complete() && written < p.config.bakeSlicesPerFrame) {
			p.draw(0,p.material,depth,constants,vertexOffset,camera,snapshot,p.progress.next());
			++written;
		}
		p.statistics.maxSlicesPerFrame = (std::max)(p.statistics.maxSlicesPerFrame,written);
		if (p.progress.complete()) {
			// Every material texel belongs to this immutable snapshot before
			// lighting can sample neighboring slices. Only completed lighting
			// becomes visible, with its matching source/wake/terrain metadata.
			p.draw(1,snapshot.lighting,depth,constants,vertexOffset,camera,snapshot);
			std::swap(p.front,p.pending);
			p.frontValid = true; p.baking = false;
			p.lastBakeTick = p.pendingTick;
			++p.statistics.bakes;
		}
	}
	p.statistics.pendingSlices = p.baking ? p.progress.remaining() : 0;
	p.statistics.bakedSources = 0;
	p.statistics.publicationAgeSeconds = 0;
	if (p.frontValid && p.statistics.activeSources) {
		auto &snapshot = p.snapshots[p.front];
		p.constantsFor(constants,snapshot);
		p.statistics.bakedSources = snapshot.activeSources;
		p.statistics.publicationAgeSeconds = (std::max)(0.0f,seconds - snapshot.seconds);
		if (p.config.heat && snapshot.nuclear
			&& !p.texture(p.background,target.Width,target.Height,
				static_cast<SDL_GPUTextureFormat>(SdlGpuFrame::Target_Format()),true,error)) return false;
		if (p.config.embers && snapshot.nuclear) {
			for (const auto &source : snapshot.fields) {
				if (!source.active || !HazardCloud::emberField(source.field)) continue;
				const float emberTime = source.sourceAge + (std::max)(0.0f,seconds - snapshot.seconds);
				if (!std::isfinite(emberTime)
					|| !SdlHazardCloudEmbers::prepare(p.frame,target,&source.field,1)) {
					error = "nuclear ember preflight failed";
					return false;
				}
			}
		}
		p.draw(2,p.low,depth,constants,vertexOffset,camera,snapshot);
		p.draw(3,target,depth,constants,vertexOffset,camera,snapshot);
		if (p.config.embers && snapshot.nuclear) {
			float viewport[6] = {constants.common.Viewport[0],constants.common.Viewport[1],
				constants.common.Viewport[2],constants.common.Viewport[3],camera.minZ,camera.maxZ};
			for (const auto &source : snapshot.fields) {
				if (!source.active || !HazardCloud::emberField(source.field)) continue;
				// Keep the published field's support/formation/density, while
				// advancing billboard phase smoothly between publications.
				const float emberTime = source.sourceAge + (std::max)(0.0f,seconds - snapshot.seconds);
				const bool terrainEnabled = source.terrain && snapshot.terrainSide >= 2;
				const float terrainDomain[4] = {snapshot.bounds.minX,snapshot.bounds.minY,
					snapshot.bounds.maxX,snapshot.bounds.maxY};
				if (!SdlHazardCloudEmbers::render(p.frame,target,depth,viewProjection,viewport,
						&source.field,1,emberTime,snapshot.terrain.Colour,&terrainEnabled,terrainDomain)) {
					// Preflight covered every failure condition before the cloud draw.
					// Once scene colour is touched, report consumption to prevent
					// a duplicate classic fallback if a backend invariant fails.
					error = "nuclear ember recording failed after preflight";
				}
			}
		}
		if (p.config.heat && snapshot.nuclear) {
			const std::int32_t rectangle[4] = {0,0,std::int32_t(target.Width),std::int32_t(target.Height)};
			p.frame->Record_Blit(target.Colour,rectangle,p.background.Colour,rectangle,false);
			p.draw(4,target,depth,constants,vertexOffset,camera,snapshot);
		}
	}
	p.frame->Set_Target(target); p.usedBatch = p.frame->Batch();
	++p.statistics.frames;
	return true;
}

void SdlRenderer::shutdown()
{
	if (!impl || !impl->frame || !impl->gpu) return;
	Impl &p = *impl;
	auto *device = dynamic_cast<PosixDevice9 *>(DX8Wrapper::_Get_D3D_Device());
	// A device teardown may precede adapter destruction. Saved frame/GPU
	// pointers then dangle and must never be dereferenced or released through.
	if (!device || device->Get_Gpu() != p.frame || device->Get_Gpu()->Device() != p.gpu) {
		SdlHazardCloudEmbers::abandon();
		impl.reset(new Impl);
		return;
	}
	p.frame->Flush();
	SDL_WaitForGPUIdle(p.gpu);
	SdlHazardCloudEmbers::shutdown();
	SdlTarget *targets[] = {&p.material,&p.low,&p.background};
	for (auto *target : targets) if (target->Colour) SDL_ReleaseGPUTexture(p.gpu,target->Colour);
	for (auto &snapshot : p.snapshots) {
		SdlTarget *snapshotTargets[] = {&snapshot.lighting,&snapshot.sources,&snapshot.bins,&snapshot.indices,&snapshot.flow,&snapshot.terrain};
		for (auto *target : snapshotTargets) if (target->Colour) SDL_ReleaseGPUTexture(p.gpu,target->Colour);
	}
	for (auto *pipeline : p.pipelines) if (pipeline) SDL_ReleaseGPUGraphicsPipeline(p.gpu,pipeline);
	for (auto *shader : p.pixels) if (shader) SDL_ReleaseGPUShader(p.gpu,shader);
	if (p.vertex) SDL_ReleaseGPUShader(p.gpu,p.vertex);
	if (p.point) SDL_ReleaseGPUSampler(p.gpu,p.point);
	if (p.linear) SDL_ReleaseGPUSampler(p.gpu,p.linear);
	impl.reset(new Impl);
}
bool Supported()
{
	return HazardCloud::Supported();
}

std::unique_ptr<Renderer> CreateRenderer()
{
	if (!Supported()) return nullptr;
	return std::make_unique<SdlRenderer>();
}
} // namespace HazardCloudShared
