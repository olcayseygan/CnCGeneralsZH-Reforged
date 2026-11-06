// SPDX-License-Identifier: GPL-3.0-or-later
// Additional terms under GNU GPL section 7 apply: see LICENSE.md.

// Explicit synthetic upload benchmark. No draws or original game data. Record
// time covers a managed mip write, mirror update and submission; total time also
// waits for GPU completion. Readbacks/checksums happen outside the timed span.
#include "PosixResources9.h"
#include "SdlGpuFrame.h"
#include "SdlResourceMirror.h"
#include "RenderBenchTiming.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>

namespace {

uint32_t initial_colour(unsigned level)
{
	return 0xff000000u | (((level * 29 + 17) & 255) << 16) | (((level * 13 + 34) & 255) << 8) | 51;
}

uint32_t update_colour(unsigned iteration)
{
	return 0xff000000u | (((iteration * 19 + 71) & 255) << 16) | (((iteration * 7 + 23) & 255) << 8) | 199;
}

bool write_level(PosixTexture9 *texture, unsigned level, uint32_t colour)
{
	D3DLOCKED_RECT locked;
	if (texture->LockRect(level, &locked, NULL, 0) != D3D_OK) return false;
	const PosixImage &image = texture->level(level);
	for (unsigned y = 0; y < image.height(); ++y) {
		uint32_t *row = reinterpret_cast<uint32_t *>(static_cast<uint8_t *>(locked.pBits) + y * locked.Pitch);
		std::fill(row, row + image.width(), colour);
	}
	return texture->UnlockRect(level) == D3D_OK;
}

bool read_level(SDL_GPUDevice *device, SDL_GPUTexture *texture, unsigned level, unsigned edge,
	std::vector<uint8_t> &bytes)
{
	SDL_GPUTransferBufferCreateInfo info = {};
	info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
	info.size = edge * edge * 4;
	SDL_GPUTransferBuffer *transfer = SDL_CreateGPUTransferBuffer(device, &info);
	if (!transfer) return false;
	SDL_GPUCommandBuffer *commands = SDL_AcquireGPUCommandBuffer(device);
	if (!commands) { SDL_ReleaseGPUTransferBuffer(device, transfer); return false; }
	SDL_GPUCopyPass *pass = SDL_BeginGPUCopyPass(commands);
	if (!pass) {
		SDL_CancelGPUCommandBuffer(commands);
		SDL_ReleaseGPUTransferBuffer(device, transfer);
		return false;
	}
	SDL_GPUTextureRegion source = {};
	source.texture = texture; source.mip_level = level;
	source.w = edge; source.h = edge; source.d = 1;
	SDL_GPUTextureTransferInfo destination = {};
	destination.transfer_buffer = transfer;
	destination.pixels_per_row = edge; destination.rows_per_layer = edge;
	SDL_DownloadFromGPUTexture(pass, &source, &destination);
	SDL_EndGPUCopyPass(pass);
	SDL_GPUFence *fence = SDL_SubmitGPUCommandBufferAndAcquireFence(commands);
	bool ok = fence && SDL_WaitForGPUFences(device, true, &fence, 1);
	if (fence) SDL_ReleaseGPUFence(device, fence);
	if (ok) {
		const uint8_t *mapped = static_cast<const uint8_t *>(SDL_MapGPUTransferBuffer(device, transfer, false));
		ok = mapped != NULL;
		if (ok) {
			bytes.assign(mapped, mapped + info.size);
			SDL_UnmapGPUTransferBuffer(device, transfer);
		}
	}
	SDL_ReleaseGPUTransferBuffer(device, transfer);
	return ok;
}

struct Resources
{
	SdlGpuFrame *frame = NULL;
	SdlResourceMirrors *mirrors = NULL;
	PosixTexture9 *texture = NULL;
	~Resources()
	{
		if (texture) texture->Release();
		delete mirrors;
		if (frame) { frame->Flush(); SDL_WaitForGPUIdle(frame->Device()); }
		delete frame;
		SDL_Quit();
	}
};

int usage()
{
	fprintf(stderr, "Usage: sdl_mip_bench [--frames N] [--warmup N] [--size 1024|2048] [--level N]\n");
	return 2;
}

bool number(const char *text, unsigned &value)
{
	char *end = NULL;
	errno = 0;
	const unsigned long parsed = strtoul(text, &end, 10);
	if (errno || !text[0] || *end || text[0] == '-' || parsed > 10000) return false;
	value = static_cast<unsigned>(parsed);
	return true;
}

} // namespace

int main(int argc, char **argv)
{
	unsigned frames = 120, warmup = 16, edge = 1024, dirtyLevel = ~0u;
	for (int i = 1; i < argc; i += 2) {
		if (i + 1 >= argc) return usage();
		unsigned value = 0;
		if (!number(argv[i + 1], value)) return usage();
		if (!strcmp(argv[i], "--frames")) frames = value;
		else if (!strcmp(argv[i], "--warmup")) warmup = value;
		else if (!strcmp(argv[i], "--size")) edge = value;
		else if (!strcmp(argv[i], "--level")) dirtyLevel = value;
		else return usage();
	}
	if (!frames || (edge != 1024 && edge != 2048)) return usage();
	if (dirtyLevel == ~0u) dirtyLevel = edge == 1024 ? 5 : 6; // 32x32 writes in either workload.
	unsigned levels = 1;
	for (unsigned size = edge; size > 1; size >>= 1) ++levels;
	if (dirtyLevel >= levels) return usage();

#if defined(__APPLE__)
	SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
	SDL_SetHint("ZH_SDL_GPU_METAL_WINDOWLESS", "1");
#else
	SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "offscreen");
#endif
	const char *qos = RenderBench::Configure_Thread();
	Resources resources;
	if (!SDL_Init(SDL_INIT_VIDEO)) { fprintf(stderr, "SKIP: %s\n", SDL_GetError()); return 77; }
	std::string error;
	resources.frame = SdlGpuFrame::Create(NULL, 16, 16, error);
	if (!resources.frame) { fprintf(stderr, "SKIP: %s\n", error.c_str()); return 77; }
	resources.mirrors = new SdlResourceMirrors(resources.frame);
	resources.texture = PosixTexture9::create(edge, edge, levels, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED);
	if (!resources.texture) { fprintf(stderr, "texture allocation failed\n"); return 1; }
	for (unsigned level = 0; level < levels; ++level)
		if (!write_level(resources.texture, level, initial_colour(level))) return 1;
	SDL_GPUTexture *gpuTexture = resources.mirrors->Texture(resources.texture, error);
	if (!gpuTexture || !resources.frame->Flush() || !SDL_WaitForGPUIdle(resources.frame->Device())) {
		fprintf(stderr, "initial upload failed: %s %s\n", error.c_str(), SDL_GetError()); return 1;
	}

	uint64_t recordNanoseconds = 0, cpuNanoseconds = 0, totalNanoseconds = 0, stagedBytes = 0;
	for (unsigned iteration = 0; iteration < warmup + frames; ++iteration) {
		const uint64_t cpuStart = RenderBench::Thread_Nanoseconds();
		const uint64_t start = SDL_GetTicksNS();
		if (!write_level(resources.texture, dirtyLevel, update_colour(iteration))) return 1;
		if (resources.mirrors->Texture(resources.texture, error) != gpuTexture) return 1;
		const uint64_t bytes = resources.frame->Pending_Upload_Bytes();
		if (!resources.frame->Flush()) return 1;
		const uint64_t recorded = SDL_GetTicksNS();
		const uint64_t cpuRecorded = RenderBench::Thread_Nanoseconds();
		if (!SDL_WaitForGPUIdle(resources.frame->Device())) return 1;
		const uint64_t completed = SDL_GetTicksNS();
		if (iteration >= warmup) {
			recordNanoseconds += recorded - start;
			cpuNanoseconds += cpuRecorded - cpuStart;
			totalNanoseconds += completed - start;
			stagedBytes += bytes;
		}
	}

	uint64_t checksum = 14695981039346656037ull;
	for (unsigned level = 0; level < levels; ++level) {
		std::vector<uint8_t> bytes;
		if (!read_level(resources.frame->Device(), gpuTexture, level, std::max(1u, edge >> level), bytes)) return 1;
		const uint32_t expected = level == dirtyLevel ? update_colour(warmup + frames - 1) : initial_colour(level);
		for (size_t at = 0; at < bytes.size(); at += 4) {
			uint32_t pixel = 0;
			memcpy(&pixel, &bytes[at], sizeof(pixel));
			if (pixel != expected) { fprintf(stderr, "mip %u changed unexpectedly\n", level); return 1; }
		}
		for (uint8_t byte : bytes) checksum = (checksum ^ byte) * 1099511628211ull;
	}
	printf("{\"workload\":\"managed_dirty_mip\",\"backend\":\"%s\",\"mode\":\"upload_flush_idle\","
		"\"frames\":%u,\"warmup\":%u,\"width\":%u,\"height\":%u,\"level\":%u,"
		"\"qos\":\"%s\",\"record_cpu_ns_per_frame\":%.3f,"
		"\"record_ns_per_frame\":%.3f,\"total_ns_per_frame\":%.3f,"
		"\"staged_bytes_per_frame\":%.3f,\"checksum\":\"%016llx\"}\n",
		SDL_GetGPUDeviceDriver(resources.frame->Device()), frames, warmup, edge, edge, dirtyLevel,
		qos, double(cpuNanoseconds) / frames,
		double(recordNanoseconds) / frames, double(totalNanoseconds) / frames,
		double(stagedBytes) / frames, static_cast<unsigned long long>(checksum));
	return 0;
}
