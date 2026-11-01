// SPDX-License-Identifier: GPL-3.0-or-later
// Additional terms under GNU GPL section 7 apply: see LICENSE.md.
// Additional terms under GNU GPL section 7 apply: see LICENSE.md.
// Synthetic production D3D9 draw recording; no original game content is needed.
#include "PosixDevice9.h"
#include "SdlGpuFrame.h"
#include "RenderBenchTiming.h"
#include "Platform/EngineShaderName.h"

#include <SDL3/SDL.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace {
constexpr unsigned Width = 32, Height = 32;
constexpr uint32_t Colour = 0xFF4080C0u;
constexpr RenderUInt32 Fvf = D3DFVF_XYZ | D3DFVF_NORMAL | D3DFVF_DIFFUSE | D3DFVF_TEX1;

struct Options {
	unsigned frames = 30, warmup = 5, draws = 1024, lights = 4;
	bool engine = true;
};

bool number(const char *text, unsigned minimum, unsigned maximum, unsigned &value)
{
	if (!text[0] || text[0] < '0' || text[0] > '9') return false;
	errno = 0;
	char *end = nullptr;
	const unsigned long parsed = std::strtoul(text, &end, 10);
	if (errno || *end || parsed < minimum || parsed > maximum) return false;
	value = static_cast<unsigned>(parsed);
	return true;
}

bool options(int argc, char **argv, Options &out)
{
	for (int i = 1; i < argc; ++i) {
		if (i + 1 == argc) return false;
		const char *key = argv[i], *value = argv[++i];
		if (!std::strcmp(key, "--frames")) {
			if (!number(value, 1, 1000, out.frames)) return false;
		} else if (!std::strcmp(key, "--warmup")) {
			if (!number(value, 1, 1000, out.warmup)) return false;
		} else if (!std::strcmp(key, "--draws")) {
			if (!number(value, 1, 1000000, out.draws)) return false;
		} else if (!std::strcmp(key, "--lights")) {
			if (!number(value, 0, 4, out.lights)) return false;
		} else if (!std::strcmp(key, "--mode")) {
			if (std::strcmp(value, "engine") && std::strcmp(value, "fixed")) return false;
			out.engine = !std::strcmp(value, "engine");
		} else return false;
	}
	return true;
}

struct Context {
	IDirect3D9 *d3d = nullptr;
	PosixDevice9 *device = nullptr;
	IDirect3DVertexBuffer9 *vertices = nullptr;
	IDirect3DVertexShader9 *shader = nullptr;
	bool sdl = false;
	~Context()
	{
		if (device) {
			device->SetStreamSource(0, nullptr, 0, 0);
			device->SetVertexShader(nullptr);
		}
		if (vertices) vertices->Release();
		if (shader) shader->Release();
		if (device) device->Release();
		if (d3d) d3d->Release();
		if (sdl) SDL_Quit();
	}
};

bool setup(Context &context, const Options &config)
{
	context.d3d = Direct3DCreate9(D3D_SDK_VERSION);
	if (!context.d3d) return false;
	D3DPRESENT_PARAMETERS parameters = {};
	parameters.BackBufferWidth = Width;
	parameters.BackBufferHeight = Height;
	parameters.BackBufferFormat = D3DFMT_X8R8G8B8;
	parameters.Windowed = 1;
	IDirect3DDevice9 *created = nullptr;
	if (context.d3d->CreateDevice(0, D3DDEVTYPE_HAL, nullptr, 0, &parameters, &created) != D3D_OK) return false;
	context.device = static_cast<PosixDevice9 *>(created);
	context.sdl = SDL_Init(SDL_INIT_VIDEO);
	if (!context.sdl || context.device->Create_Gpu_Frame(true) != D3D_OK) return false;
	PosixDevice9 *device = context.device;
	D3DMATRIX identity = {};
	identity._11 = identity._22 = identity._33 = identity._44 = 1;
	for (D3DTRANSFORMSTATETYPE transform : { D3DTS_WORLD, D3DTS_VIEW, D3DTS_PROJECTION })
		device->SetTransform(transform, &identity);
	device->SetRenderState(D3DRS_ZENABLE, 0);
	device->SetRenderState(D3DRS_ZWRITEENABLE, 0);
	device->SetRenderState(D3DRS_LIGHTING, 0);
	device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
	device->SetRenderState(D3DRS_ALPHABLENDENABLE, 0);
	device->SetRenderState(D3DRS_ALPHATESTENABLE, 0);
	device->SetRenderState(D3DRS_FOGENABLE, 0);
	device->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG2);
	device->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
	device->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG2);
	device->SetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);
	device->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
	// Carried light state is intentionally identical in both modes. These unlit shaders do not
	// consume it; --lights 0 also measures the no-enabled-lights case without changing the draw.
	for (unsigned i = 0; i < config.lights; ++i) {
		D3DLIGHT9 light = {};
		light.Type = D3DLIGHT_SPOT;
		light.Position.x = static_cast<float>(i);
		light.Direction.z = 1;
		light.Diffuse.r = light.Diffuse.g = light.Diffuse.b = light.Diffuse.a = 1;
		light.Attenuation0 = 1;
		light.Range = 100;
		light.Theta = 0.75f;
		light.Phi = 1.5f;
		light.Falloff = 1;
		if (device->SetLight(i, &light) != D3D_OK || device->LightEnable(i, 1) != D3D_OK) return false;
	}
	if (config.engine) {
		const RenderUInt32 tokens[] = { 0xFFFE0101u, 0x0000FFFFu };
		if (device->CreateVertexShader(tokens, &context.shader) != D3D_OK) return false;
		PosixDevice_Name_Shader(context.shader, "trees.vso");
		device->SetVertexShader(context.shader);
		device->SetVertexShaderConstantF(4, &identity._11, 4);
		const float no_sway[4] = {};
		device->SetVertexShaderConstantF(8, no_sway, 1);
	}
	device->SetFVF(Fvf);
	// The tree program reads sway type/shade/base from the normal slot. Type 0 and zero sway
	// preserve the geometry; shade 1 preserves its distinct RGB channels in both shader modes.
	struct Vertex { float x, y, z, type, shade, base; uint32_t colour; float u, v; };
	const Vertex quad[] = {
		{ -.75f, .5f, .25f, 0, 1, 0, Colour, 0, 0 }, { .75f, .5f, .25f, 0, 1, 0, Colour, 1, 0 },
		{ -.75f, -.5f, .25f, 0, 1, 0, Colour, 0, 1 }, { .75f, .5f, .25f, 0, 1, 0, Colour, 1, 0 },
		{ .75f, -.5f, .25f, 0, 1, 0, Colour, 1, 1 }, { -.75f, -.5f, .25f, 0, 1, 0, Colour, 0, 1 }
	};
	if (device->CreateVertexBuffer(sizeof(quad), D3DUSAGE_WRITEONLY, Fvf, D3DPOOL_MANAGED,
		&context.vertices, nullptr) != D3D_OK) return false;
	void *mapped = nullptr;
	if (context.vertices->Lock(0, 0, &mapped, 0) != D3D_OK || !mapped) return false;
	std::memcpy(mapped, quad, sizeof(quad));
	if (context.vertices->Unlock() != D3D_OK) return false;
	return device->SetStreamSource(0, context.vertices, 0, sizeof(Vertex)) == D3D_OK;
}

bool check_pixels(SdlGpuFrame *gpu, uint64_t &checksum)
{
	std::vector<uint8_t> pixels;
	if (!gpu->Read_Back(gpu->Back_Buffer(), Width, Height, pixels)
		|| pixels.size() != Width * Height * 4) return false;
	checksum = 14695981039346656037ull;
	for (uint8_t byte : pixels) checksum = (checksum ^ byte) * 1099511628211ull;
	for (unsigned y = 0; y < Height; ++y) for (unsigned x = 0; x < Width; ++x) {
		const bool inside = x >= 5 && x <= 27 && y >= 9 && y <= 23;
		const bool outside = x < 3 || x > 29 || y < 7 || y > 25;
		if (!inside && !outside) continue; // Leave rasterization boundary rules out of this benchmark.
		const uint32_t expected = inside ? Colour : 0xFF000000u;
		const uint8_t *pixel = &pixels[(y * Width + x) * 4];
		for (unsigned channel = 0; channel < 4; ++channel) {
			const int difference = static_cast<int>(pixel[channel]) - static_cast<int>((expected >> (8 * channel)) & 255);
			if (difference < -1 || difference > 1) return false;
		}
	}
	return true;
}
} // namespace

int main(int argc, char **argv)
{
	Options config;
	if (!options(argc, argv, config)) {
		std::fprintf(stderr, "Usage: sdl_draw_bench [--frames 1..1000] [--warmup 1..1000] "
			"[--draws 1..1000000] [--mode engine|fixed] [--lights 0..4]\n");
		return 2;
	}
	Context context;
	const char *qos = RenderBench::Configure_Thread();
	if (!setup(context, config)) {
		std::fprintf(stderr, "sdl_draw_bench: setup failed: %s\n", SDL_GetError());
		return 1;
	}
	SdlGpuFrame *gpu = context.device->Get_Gpu();
	uint64_t record_ns = 0, record_cpu_ns = 0, complete_ns = 0, checksum = 0;
	for (unsigned frame = 0; frame < config.warmup + config.frames; ++frame) {
		if (context.device->Clear(0, nullptr, D3DCLEAR_TARGET, 0xFF000000u, 1, 0) != D3D_OK) return 1;
		const unsigned previous_draws = context.device->Draws_Recorded();
		const uint64_t cpu_start = RenderBench::Thread_Nanoseconds();
		const uint64_t start = SDL_GetTicksNS();
		for (unsigned draw = 0; draw < config.draws; ++draw) {
			if (context.device->DrawPrimitive(D3DPT_TRIANGLELIST, 0, 2) != D3D_OK) {
				std::fprintf(stderr, "sdl_draw_bench: draw failed\n");
				return 1;
			}
		}
		const uint64_t recorded = SDL_GetTicksNS();
		const uint64_t cpu_recorded = RenderBench::Thread_Nanoseconds();
		if (!gpu->Flush() || !SDL_WaitForGPUIdle(gpu->Device())) {
			std::fprintf(stderr, "sdl_draw_bench: submission failed: %s\n", SDL_GetError());
			return 1;
		}
		const uint64_t completed = SDL_GetTicksNS();
		// Count and framebuffer validation are deliberately outside both timing spans, every frame.
		uint64_t actual_checksum = 0;
		if (context.device->Draws_Recorded() - previous_draws != config.draws
			|| !check_pixels(gpu, actual_checksum) || (frame && actual_checksum != checksum)) {
			std::fprintf(stderr, "sdl_draw_bench: draw count or framebuffer mismatch at frame %u\n", frame);
			return 1;
		}
		checksum = actual_checksum;
		if (frame >= config.warmup) {
			record_ns += recorded - start;
			record_cpu_ns += cpu_recorded - cpu_start;
			complete_ns += completed - start;
		}
	}
	std::printf("{\"benchmark\":\"sdl_draw\",\"backend\":\"%s\",\"mode\":\"%s\","
		"\"width\":%u,\"height\":%u,\"frames\":%u,\"warmup\":%u,\"draws\":%u,\"lights\":%u,"
		"\"qos\":\"%s\",\"record_cpu_ns_per_frame\":%.3f,"
		"\"record_ns_per_frame\":%.3f,\"complete_ns_per_frame\":%.3f,\"checksum\":\"%016llx\"}\n",
		SDL_GetGPUDeviceDriver(gpu->Device()), config.engine ? "engine" : "fixed", Width, Height,
		config.frames, config.warmup, config.draws, config.lights,
		qos, static_cast<double>(record_cpu_ns) / config.frames,
		static_cast<double>(record_ns) / config.frames, static_cast<double>(complete_ns) / config.frames,
		static_cast<unsigned long long>(checksum));
	return 0;
}
