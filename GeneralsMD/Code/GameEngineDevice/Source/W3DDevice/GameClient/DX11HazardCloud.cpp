// Direct3D 11 backend for the volumetric hazard clouds: the same passes the SDL
// GPU backend records, drawn on the context between two of the backend's draws.
// Distributed under the repository's GPL-3.0 terms; see LICENSE.md.
#include "Platform/HazardCloudCore.h"
#include "Platform/HazardCloudShared.h"
#include "Common/Debug.h"
#include "dx11backend.h"
#include "dx11device.h"
#include "dx11runtime.h"
#include "dx11resource.h"

#include <d3d11.h>
#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

namespace {
using namespace HazardCloud;

const DXGI_FORMAT LowFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
const DXGI_FORMAT DensityFormat = DXGI_FORMAT_R16_FLOAT;
const DXGI_FORMAT FlowFormat = DXGI_FORMAT_R32G32B32A32_FLOAT;
const DXGI_FORMAT TerrainFormat = DXGI_FORMAT_R32_FLOAT;
const unsigned PassCount = 5;
// The texture and sampler slots the cloud text declares (t0-t6, s0-s6).
const unsigned Slots = 7;

template <class T> void drop(T *&object)
{
	if (object) {
		object->Release();
		object = nullptr;
	}
}

struct Surface {
	ID3D11Texture2D *texture = nullptr;
	ID3D11RenderTargetView *target = nullptr;
	ID3D11ShaderResourceView *view = nullptr;
	unsigned width = 0, height = 0;
};

void drop(Surface &surface)
{
	drop(surface.view);
	drop(surface.target);
	drop(surface.texture);
	surface.width = surface.height = 0;
}

bool make(ID3D11Device *device, Surface &surface, unsigned width, unsigned height, DXGI_FORMAT format,
	bool renderTarget)
{
	if (surface.texture && surface.width == width && surface.height == height) return true;
	drop(surface);
	D3D11_TEXTURE2D_DESC description = {};
	description.Width = width;
	description.Height = height;
	description.MipLevels = 1;
	description.ArraySize = 1;
	description.Format = format;
	description.SampleDesc.Count = 1;
	description.Usage = D3D11_USAGE_DEFAULT;
	description.BindFlags = D3D11_BIND_SHADER_RESOURCE | (renderTarget ? D3D11_BIND_RENDER_TARGET : 0);
	if (FAILED(device->CreateTexture2D(&description, nullptr, &surface.texture))
		|| FAILED(device->CreateShaderResourceView(surface.texture, nullptr, &surface.view))
		|| (renderTarget && FAILED(device->CreateRenderTargetView(surface.texture, nullptr, &surface.target)))) {
		drop(surface);
		return false;
	}
	surface.width = width;
	surface.height = height;
	return true;
}

bool upload(ID3D11DeviceContext *context, ID3D11Buffer *buffer, const void *data, size_t bytes)
{
	D3D11_MAPPED_SUBRESOURCE mapped;
	if (FAILED(context->Map(buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) return false;
	std::memcpy(mapped.pData, data, bytes);
	context->Unmap(buffer, 0);
	return true;
}

// Programs, states and the screen-sized targets every layer shares, made with
// the first layer and kept until ReleaseDevice.
struct Shared {
	ID3D11Device *device = nullptr;
	bool refused = false;
	ID3D11VertexShader *fullscreen = nullptr;
	ID3D11PixelShader *pixel[PassCount] = {};
	ID3D11VertexShader *emberVertex = nullptr;
	ID3D11PixelShader *emberPixel = nullptr;
	ID3D11SamplerState *point = nullptr;
	ID3D11SamplerState *linear = nullptr;
	ID3D11BlendState *premultiplied = nullptr;
	ID3D11BlendState *additive = nullptr;
	ID3D11RasterizerState *rasterizer = nullptr;
	ID3D11Buffer *constants = nullptr;
	ID3D11Buffer *emberConstants = nullptr;
	ID3D11Buffer *emberPixelConstants = nullptr;
	Surface low;
	// A copy of the scene for the heat haze to bend, at the scene's size and format.
	ID3D11Texture2D *heat = nullptr;
	ID3D11ShaderResourceView *heatView = nullptr;
};
Shared shared;

void releaseShared()
{
	drop(shared.fullscreen);
	for (ID3D11PixelShader *&program : shared.pixel) drop(program);
	drop(shared.emberVertex);
	drop(shared.emberPixel);
	drop(shared.point);
	drop(shared.linear);
	drop(shared.premultiplied);
	drop(shared.additive);
	drop(shared.rasterizer);
	drop(shared.constants);
	drop(shared.emberConstants);
	drop(shared.emberPixelConstants);
	drop(shared.low);
	drop(shared.heatView);
	drop(shared.heat);
	shared = Shared();
}

bool compile(DX11BackendClass *backend, ID3D11Device *device, const std::string &source,
	const char *name, bool vertex, void **program)
{
	std::vector<unsigned char> code;
	if (!backend->Compile_Program(source, name, vertex ? "vs_5_0" : "ps_5_0", code)) {
		DEBUG_LOG(("HAZARDCLOUD: %s did not compile: %s\n", name, backend->First_Compiler_Error()));
		return false;
	}
	return vertex
		? SUCCEEDED(device->CreateVertexShader(code.data(), code.size(), nullptr, (ID3D11VertexShader **)program))
		: SUCCEEDED(device->CreatePixelShader(code.data(), code.size(), nullptr, (ID3D11PixelShader **)program));
}

bool makeShared(DX11BackendClass *backend, ID3D11Device *device)
{
	if (shared.device == device) return !shared.refused;
	const double started = DX11Resource_Milliseconds_Now();
	releaseShared();
	shared.device = device;
	static const char *const names[PassCount] = {
		"hazardcloud volume", "hazardcloud composite", "hazardcloud density", "hazardcloud lighting",
		"hazardcloud heat" };
	bool made = compile(backend, device, vertexShader(), "hazardcloud fullscreen", true, (void **)&shared.fullscreen)
		&& compile(backend, device, emberVertexShader(false), "hazardcloud embers", true, (void **)&shared.emberVertex)
		&& compile(backend, device, emberPixelShader(false), "hazardcloud embers", false, (void **)&shared.emberPixel);
	for (unsigned pass = 0; made && pass < PassCount; ++pass)
		made = compile(backend, device, pixelShader(static_cast<Pass>(pass), false), names[pass], false,
			(void **)&shared.pixel[pass]);

	D3D11_SAMPLER_DESC sampler = {};
	sampler.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
	sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
	sampler.ComparisonFunc = D3D11_COMPARISON_NEVER;
	sampler.MaxLOD = D3D11_FLOAT32_MAX;
	made = made && SUCCEEDED(device->CreateSamplerState(&sampler, &shared.point));
	sampler.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
	made = made && SUCCEEDED(device->CreateSamplerState(&sampler, &shared.linear));

	// Premultiplied radiance over the scene, for the composite and the heat; the
	// embers add light and leave the scene's alpha alone.
	D3D11_BLEND_DESC blend = {};
	D3D11_RENDER_TARGET_BLEND_DESC &target = blend.RenderTarget[0];
	target.BlendEnable = TRUE;
	target.SrcBlend = target.SrcBlendAlpha = D3D11_BLEND_ONE;
	target.DestBlend = target.DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
	target.BlendOp = target.BlendOpAlpha = D3D11_BLEND_OP_ADD;
	target.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
	made = made && SUCCEEDED(device->CreateBlendState(&blend, &shared.premultiplied));
	target.DestBlend = D3D11_BLEND_ONE;
	target.SrcBlendAlpha = D3D11_BLEND_ZERO;
	target.DestBlendAlpha = D3D11_BLEND_ONE;
	made = made && SUCCEEDED(device->CreateBlendState(&blend, &shared.additive));

	D3D11_RASTERIZER_DESC rasterizer = {};
	rasterizer.FillMode = D3D11_FILL_SOLID;
	rasterizer.CullMode = D3D11_CULL_NONE;
	rasterizer.DepthClipEnable = TRUE;
	rasterizer.ScissorEnable = TRUE;
	made = made && SUCCEEDED(device->CreateRasterizerState(&rasterizer, &shared.rasterizer));

	D3D11_BUFFER_DESC buffer = {};
	buffer.Usage = D3D11_USAGE_DYNAMIC;
	buffer.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	buffer.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	buffer.ByteWidth = sizeof(Uniforms);
	made = made && SUCCEEDED(device->CreateBuffer(&buffer, nullptr, &shared.constants));
	buffer.ByteWidth = sizeof(EmberConstants);
	made = made && SUCCEEDED(device->CreateBuffer(&buffer, nullptr, &shared.emberConstants));
	buffer.ByteWidth = 16;
	made = made && SUCCEEDED(device->CreateBuffer(&buffer, nullptr, &shared.emberPixelConstants));
	if (!made) {
		DEBUG_LOG(("HAZARDCLOUD: the Direct3D 11 device refused the cloud programs; Classic clouds stay\n"));
		releaseShared();
		shared.device = device;
		shared.refused = true;
	}
	// A program the shader cache lacks costs seconds of FXC here; the field tracking asks for
	// this at the first frame with Volumetric clouds, never on the frame a hazard opens.
	DEBUG_LOG(("HAZARDCLOUD: Direct3D 11 cloud programs made in %.1f ms\n", DX11Resource_Milliseconds_Now() - started));
	return made;
}

// The heat's copy of the scene, remade when the scene's size or format changes.
bool makeHeatCopy(ID3D11Device *device, ID3D11RenderTargetView *scene, ID3D11Resource *resource)
{
	D3D11_TEXTURE2D_DESC description;
	static_cast<ID3D11Texture2D *>(resource)->GetDesc(&description);
	if (shared.heat) {
		D3D11_TEXTURE2D_DESC held;
		shared.heat->GetDesc(&held);
		if (held.Width == description.Width && held.Height == description.Height && held.Format == description.Format)
			return true;
		drop(shared.heatView);
		drop(shared.heat);
	}
	description.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	description.MiscFlags = 0;
	description.Usage = D3D11_USAGE_DEFAULT;
	description.CPUAccessFlags = 0;
	description.SampleDesc.Count = 1;	// a multisampled scene is resolved into it
	description.SampleDesc.Quality = 0;
	D3D11_RENDER_TARGET_VIEW_DESC written;
	scene->GetDesc(&written);
	D3D11_SHADER_RESOURCE_VIEW_DESC view = {};
	view.Format = written.Format;
	view.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	view.Texture2D.MipLevels = 1;
	if (FAILED(device->CreateTexture2D(&description, nullptr, &shared.heat))
		|| FAILED(device->CreateShaderResourceView(shared.heat, &view, &shared.heatView))) {
		drop(shared.heatView);
		drop(shared.heat);
		return false;
	}
	return true;
}

bool supports(ID3D11Device *device, DXGI_FORMAT format, UINT wanted)
{
	UINT support = 0;
	return SUCCEEDED(device->CheckFormatSupport(format, &support)) && (support & wanted) == wanted;
}

class DX11Layer final : public LayerCore {
public:
	~DX11Layer() override { shutdown(); }
	bool initialize() override;
	bool render(const Camera &camera) override;
	std::uint64_t estimatedCacheBytes() const override;
	void shutdown() override;

private:
	struct FieldCache {
		Surface density, lighting;
		unsigned side = 0;
	};
	bool refuse(const char *reason);
	bool prepareCaches(Uniforms &uniforms);
	void bake(ID3D11DeviceContext *context, ID3D11ShaderResourceView *depth, const Uniforms &uniforms);
	void unbindViews(ID3D11DeviceContext *context);
	void bindViews(ID3D11DeviceContext *context, ID3D11ShaderResourceView *const views[Slots]);

	Surface flowTexture, terrainTexture;
	FieldCache caches[2];
};

bool DX11Layer::refuse(const char *reason)
{
	if (!failed) DEBUG_LOG(("HAZARDCLOUD: Direct3D 11 clouds stopped: %s\n", reason));
	fail(reason);
	return false;
}

bool DX11Layer::initialize()
{
	if (initialized) return !failed;
	DX11DeviceClass *device = Direct3D11_Device();
	DX11BackendClass *backend = Direct3D11_Backend();
	if (!Supported() || !device || !backend) return false;
	failed = false;
	if (!makeShared(backend, device->Get_Device())) return false;
	if (!make(device->Get_Device(), flowTexture, flow.width(), flow.height(), FlowFormat, false))
		return refuse("the interaction texture was refused");
	initialized = true;
	if (!explicitDomain && !fieldCount) flow.setDomain(-400, -220, 400, 220);
	flowDirty = true;
	return true;
}

void DX11Layer::shutdown()
{
	if (!initialized) return;
	drop(flowTexture);
	drop(terrainTexture);
	for (FieldCache &cache : caches) {
		drop(cache.density);
		drop(cache.lighting);
		cache.side = 0;
	}
	initialized = false;
	resetCounters();
}

std::uint64_t DX11Layer::estimatedCacheBytes() const
{
	std::uint64_t result = flowTexture.texture ? 192u * 96u * 4u * sizeof(float) : 0;
	if (terrainTexture.texture) result += TerrainGridSide * TerrainGridSide * 2u * sizeof(float);
	for (const FieldCache &cache : caches)
		result += std::uint64_t(cache.side) * cache.side * CacheSlices * 10u;
	return result;
}

bool DX11Layer::prepareCaches(Uniforms &uniforms)
{
	float *grids[] = { uniforms.Cache0, uniforms.Cache1 };
	for (unsigned i = 0; i < fieldCount; ++i) {
		if (!fieldActive(i)) continue;
		const unsigned side = cacheSide(fieldStates[i]);
		FieldCache &cache = caches[i];
		if (cache.side != side) {
			if (!make(shared.device, cache.density, side * CacheColumns, side * CacheRows, DensityFormat, true)
				|| !make(shared.device, cache.lighting, side * CacheColumns, side * CacheRows, LowFormat, true))
				return refuse("the cloud atlases were refused");
			cache.side = side;
			invalidateBake();
		}
		grids[i][0] = grids[i][1] = static_cast<float>(side);
		grids[i][2] = static_cast<float>(CacheSlices);
		grids[i][3] = static_cast<float>(CacheColumns);
	}
	return true;
}

void DX11Layer::unbindViews(ID3D11DeviceContext *context)
{
	ID3D11ShaderResourceView *const none[Slots] = {};
	context->PSSetShaderResources(0, Slots, none);
}

void DX11Layer::bindViews(ID3D11DeviceContext *context, ID3D11ShaderResourceView *const views[Slots])
{
	context->PSSetShaderResources(0, Slots, views);
}

void DX11Layer::bake(ID3D11DeviceContext *context, ID3D11ShaderResourceView *depth, const Uniforms &uniforms)
{
	const float clear[4] = {};
	for (unsigned i = 0; i < fieldCount; ++i) {
		if (!fieldActive(i)) continue;
		const FieldCache &cache = caches[i];
		Uniforms pass = uniforms;
		pass.CachePass[0] = static_cast<float>(i);
		upload(context, shared.constants, &pass, sizeof(pass));
		const D3D11_VIEWPORT viewport = { 0, 0, float(cache.density.width), float(cache.density.height), 0, 1 };
		const D3D11_RECT all = { 0, 0, LONG(cache.density.width), LONG(cache.density.height) };
		context->RSSetViewports(1, &viewport);
		context->RSSetScissorRects(1, &all);

		unbindViews(context);
		context->OMSetRenderTargets(1, &cache.density.target, nullptr);
		context->ClearRenderTargetView(cache.density.target, clear);
		ID3D11ShaderResourceView *const densityViews[Slots] = {
			depth, flowTexture.view, nullptr, nullptr, nullptr, nullptr, terrainTexture.view };
		bindViews(context, densityViews);
		context->PSSetShader(shared.pixel[int(Pass::Density)], nullptr, 0);
		context->Draw(3, 0);

		// Switching the target ends the density pass before the light reads it.
		unbindViews(context);
		context->OMSetRenderTargets(1, &cache.lighting.target, nullptr);
		context->ClearRenderTargetView(cache.lighting.target, clear);
		ID3D11ShaderResourceView *const lightViews[Slots] = {
			depth, flowTexture.view, flowTexture.view, caches[0].density.view, caches[1].density.view, nullptr,
			terrainTexture.view };
		bindViews(context, lightViews);
		context->PSSetShader(shared.pixel[int(Pass::Lighting)], nullptr, 0);
		context->Draw(3, 0);
	}
}

bool DX11Layer::render(const Camera &camera)
{
	if (!initialized || failed || !anyFieldActive()) return false;
	DX11BackendClass *backend = Direct3D11_Backend();
	DX11DeviceClass *device = Direct3D11_Device();
	if (!backend || !device || device->Get_Device() != shared.device)
		return refuse("the Direct3D 11 device was replaced");
	// A mirror or the sun's map: Classic draws there. A screen filter's texture
	// carries the scene's depth, so the clouds go into it.
	ID3D11RenderTargetView *scene = backend->Scene_Target();
	if (!scene) return false;
	ID3D11DepthStencilView *sceneDepth = backend->Scene_Depth_Target();
	backend->Resolve_Scene_Depth();
	ID3D11DeviceContext *context = device->Get_Context();
	ID3D11ShaderResourceView *depth = device->Get_Depth_Texture();
	if (!depth) return refuse("the scene has no readable depth");
	ID3D11Resource *sceneResource = nullptr;
	scene->GetResource(&sceneResource);
	D3D11_TEXTURE2D_DESC sceneDescription;
	static_cast<ID3D11Texture2D *>(sceneResource)->GetDesc(&sceneDescription);
	sceneResource->Release();	// the device's own view keeps it alive
	const unsigned width = sceneDescription.Width, height = sceneDescription.Height;

	Uniforms uniforms;
	float viewProjection[16];
	if (!buildUniforms(camera, width, height, uniforms, viewProjection)) return false;
	if (!make(shared.device, shared.low, width / 2 + width % 2, height / 2 + height % 2, LowFormat, true))
		return refuse("the half resolution target was refused");
	const bool heat = uniforms.Settings[1] > 0;
	if (heat && !makeHeatCopy(shared.device, scene, sceneResource))
		return refuse("the heat haze's copy of the scene was refused");
	if ((terrainEnabled[0] || terrainEnabled[1]) && !terrainTexture.texture) {
		if (!make(shared.device, terrainTexture, TerrainGridSide * 2, TerrainGridSide, TerrainFormat, false))
			return refuse("the terrain height texture was refused");
		terrainDirty = true;
	}
	if (!prepareCaches(uniforms)) return false;

	// Everything below binds on the context behind the backend's back. What it
	// expects to find again is put back at the end, and it forgets the rest.
	// The backend never turns the scissor on, so only the viewport needs keeping.
	D3D11_VIEWPORT savedViewport;
	UINT viewportCount = 1;
	context->RSGetViewports(&viewportCount, &savedViewport);

	if (flowNeedsUpload()) {
		if (!flow.valid()) return refuse("interaction simulation produced invalid state");
		context->UpdateSubresource(flowTexture.texture, 0, nullptr, flow.data(),
			UINT(flow.width() * 4 * sizeof(float)), 0);
		flowUploaded();
	}
	if (terrainDirty && terrainTexture.texture) {
		context->UpdateSubresource(terrainTexture.texture, 0, nullptr, terrainPixels.data(),
			UINT(TerrainGridSide * 2 * sizeof(float)), 0);
		terrainDirty = false;
		invalidateBake();
	}

	ID3D11SamplerState *const samplers[Slots] = {
		shared.point, shared.point, shared.point, shared.linear, shared.linear, shared.linear, shared.point };
	context->IASetInputLayout(nullptr);
	context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	context->VSSetShader(shared.fullscreen, nullptr, 0);
	context->RSSetState(shared.rasterizer);
	context->OMSetDepthStencilState(nullptr, 0);
	context->OMSetBlendState(nullptr, nullptr, 0xffffffff);
	context->PSSetSamplers(0, Slots, samplers);
	context->PSSetConstantBuffers(0, 1, &shared.constants);
	if (takeBake(uniforms)) bake(context, depth, uniforms);

	// The volume at half resolution, unblended.
	upload(context, shared.constants, &uniforms, sizeof(uniforms));
	const float clear[4] = {};
	const D3D11_VIEWPORT lowViewport = { 0, 0, float(shared.low.width), float(shared.low.height), 0, 1 };
	const D3D11_RECT lowRect = { 0, 0, LONG(shared.low.width), LONG(shared.low.height) };
	context->RSSetViewports(1, &lowViewport);
	context->RSSetScissorRects(1, &lowRect);
	unbindViews(context);
	context->OMSetRenderTargets(1, &shared.low.target, nullptr);
	context->ClearRenderTargetView(shared.low.target, clear);
	ID3D11ShaderResourceView *const volumeViews[Slots] = {
		depth, flowTexture.view, nullptr, caches[0].lighting.view, caches[1].lighting.view, nullptr,
		terrainTexture.view };
	bindViews(context, volumeViews);
	context->PSSetShader(shared.pixel[int(Pass::Volume)], nullptr, 0);
	context->Draw(3, 0);

	// Up into the scene over the camera's viewport. The depth is read as a
	// texture, so no depth buffer is bound and nothing tests against it.
	const D3D11_VIEWPORT sceneViewport = { float(camera.x), float(camera.y), float(camera.width),
		float(camera.height), camera.minZ, camera.maxZ };
	const LONG right = LONG((std::min)(camera.x + camera.width, width));
	const LONG bottom = LONG((std::min)(camera.y + camera.height, height));
	const D3D11_RECT sceneRect = { LONG(camera.x), LONG(camera.y), right, bottom };
	context->RSSetViewports(1, &sceneViewport);
	context->RSSetScissorRects(1, &sceneRect);
	unbindViews(context);
	context->OMSetRenderTargets(1, &scene, nullptr);
	ID3D11ShaderResourceView *const compositeViews[Slots] = {
		depth, flowTexture.view, shared.low.view, caches[0].lighting.view, caches[1].lighting.view, nullptr,
		terrainTexture.view };
	bindViews(context, compositeViews);
	context->OMSetBlendState(shared.premultiplied, nullptr, 0xffffffff);
	context->PSSetShader(shared.pixel[int(Pass::Composite)], nullptr, 0);
	context->Draw(3, 0);

	// The embers, projected the way the scene is: half a pixel over, as the
	// backend's own viewport sits for Direct3D 9's pixel centres.
	const D3D11_VIEWPORT emberViewport = { float(camera.x) + 0.5f, float(camera.y) + 0.5f, float(camera.width),
		float(camera.height), camera.minZ, camera.maxZ };
	bool embers = false;
	for (unsigned i = 0; i < fieldCount; ++i) {
		if (!emberField(fieldStates[i])) continue;
		EmberConstants constants;
		const unsigned count = emberConstants(fieldStates[i], i, terrainEnabled[i] && terrainTexture.view,
			viewProjection, uniforms.Viewport, simulationTime, constants);
		if (!embers) {
			const float pixelConstants[4] = { 1.0f / width, 1.0f / height, 0, 0 };
			upload(context, shared.emberPixelConstants, pixelConstants, sizeof(pixelConstants));
			context->RSSetViewports(1, &emberViewport);
			context->VSSetShader(shared.emberVertex, nullptr, 0);
			context->VSSetConstantBuffers(0, 1, &shared.emberConstants);
			context->VSSetShaderResources(0, 1, &terrainTexture.view);
			context->VSSetSamplers(0, 1, &shared.point);
			context->PSSetShader(shared.emberPixel, nullptr, 0);
			context->PSSetConstantBuffers(0, 1, &shared.emberPixelConstants);
			context->OMSetBlendState(shared.additive, nullptr, 0xffffffff);
			embers = true;
		}
		upload(context, shared.emberConstants, &constants, sizeof(constants));
		context->Draw(count * 6, 0);
	}
	if (embers) {
		ID3D11ShaderResourceView *const none = nullptr;
		context->VSSetShaderResources(0, 1, &none);
		context->VSSetShader(shared.fullscreen, nullptr, 0);
		context->RSSetViewports(1, &sceneViewport);
		context->PSSetConstantBuffers(0, 1, &shared.constants);
		context->OMSetBlendState(shared.premultiplied, nullptr, 0xffffffff);
	}

	// The nuclear heat bends the scene as it stands now, smoke and embers in it.
	if (heat) {
		int rectangle[4];
		heatScissor(viewProjection, camera, width, height, rectangle);
		if (rectangle[2] > 0 && rectangle[3] > 0) {
			const D3D11_RECT heatRect = { rectangle[0], rectangle[1], rectangle[0] + rectangle[2],
				rectangle[1] + rectangle[3] };
			context->RSSetScissorRects(1, &heatRect);
			ID3D11Resource *source = nullptr;
			scene->GetResource(&source);
			DX11Resource_Copy_Readable(context, shared.heat, source);
			source->Release();
			ID3D11ShaderResourceView *const heatViews[Slots] = {
				depth, flowTexture.view, shared.low.view, caches[0].lighting.view, caches[1].lighting.view,
				shared.heatView, terrainTexture.view };
			bindViews(context, heatViews);
			context->PSSetShader(shared.pixel[int(Pass::Heat)], nullptr, 0);
			context->Draw(3, 0);
		}
	}

	unbindViews(context);
	context->OMSetRenderTargets(1, &scene, sceneDepth);
	context->RSSetViewports(viewportCount, &savedViewport);
	backend->Forget_Bindings();
	if (renderedFrameCount++ == 0)
		DEBUG_LOG(("HAZARDCLOUD: Direct3D 11 cloud drawn, palette %d, radius %.0f, %u bakes so far\n",
			int(fieldStates[0].palette + 0.5f), fieldStates[0].radius, bakes()));
	return true;
}

} // namespace

namespace HazardCloud {

bool Supported()
{
	DX11DeviceClass *device = Direct3D11_Device();
	if (!Direct3D11_Present_Is_Enabled() || !Direct3D11_Backend() || !device || !device->Get_Device()
		|| !device->Get_Depth_Texture() || device->Get_Device()->GetFeatureLevel() < D3D_FEATURE_LEVEL_11_0)
		return false;
	ID3D11Device *d3d = device->Get_Device();
	const UINT sampled = D3D11_FORMAT_SUPPORT_TEXTURE2D | D3D11_FORMAT_SUPPORT_SHADER_SAMPLE;
	const UINT drawn = sampled | D3D11_FORMAT_SUPPORT_RENDER_TARGET;
	return supports(d3d, LowFormat, drawn) && supports(d3d, DensityFormat, drawn)
		&& supports(d3d, FlowFormat, sampled) && supports(d3d, TerrainFormat, sampled);
}

std::unique_ptr<Layer> CreateLayer()
{
	if (!Supported()) return nullptr;
	return std::make_unique<DX11Layer>();
}

void ReleaseDevice() { releaseShared(); }

} // namespace HazardCloud

namespace HazardCloudShared {

bool Supported() { return false; }
std::unique_ptr<Renderer> CreateRenderer() { return nullptr; }

} // namespace HazardCloudShared
