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

// Exercise the production attachment selection against every combination of the five queried
// capabilities. No SDL initialization or GPU is needed, so unsupported-device fallbacks run too.
#include "SdlGpuDepthFormat.h"

#include <stdio.h>
#include <vector>

static int failures = 0;
#define CHECK(condition) \
	do { if (!(condition)) { ++failures; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); } } while (0)

enum Capability
{
	Resolve = 1,
	SampledD24 = 2,
	SampledD32 = 4,
	PlainD24 = 8,
	PlainD32 = 16
};

static SdlGpuDepthFormat select(unsigned int capabilities)
{
	std::vector<unsigned int> queries;
	const SdlGpuDepthFormat result = Sdl_Select_Depth_Format(
		[&](SDL_GPUTextureFormat format, SDL_GPUTextureType type, SDL_GPUTextureUsageFlags usage) {
			CHECK(type == SDL_GPU_TEXTURETYPE_2D);
			unsigned int capability = 0;
			if (format == SDL_GPU_TEXTUREFORMAT_R32_FLOAT) {
				CHECK(usage == (SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER));
				capability = Resolve;
			} else {
				CHECK(format == SDL_GPU_TEXTUREFORMAT_D24_UNORM_S8_UINT
					|| format == SDL_GPU_TEXTUREFORMAT_D32_FLOAT_S8_UINT);
				CHECK(usage == SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET
					|| usage == (SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER));
				const bool d24 = format == SDL_GPU_TEXTUREFORMAT_D24_UNORM_S8_UINT;
				capability = usage & SDL_GPU_TEXTUREUSAGE_SAMPLER
					? (d24 ? SampledD24 : SampledD32) : (d24 ? PlainD24 : PlainD32);
			}
			queries.push_back(capability);
			return (capabilities & capability) != 0;
		});
	CHECK(!queries.empty() && queries.front() == Resolve);
	if (!(capabilities & Resolve)) {
		for (unsigned int query : queries) CHECK(query != SampledD24 && query != SampledD32);
	}
	return result;
}

int main()
{
	// Ordered requirements describe the contract independently of the query implementation:
	// sampled D24, sampled D32, ordinary D24, ordinary D32, then unsupported.
	const struct Candidate {
		unsigned int Required;
		SDL_GPUTextureFormat Format;
		bool Sampleable;
	} candidates[] = {
		{ Resolve | SampledD24, SDL_GPU_TEXTUREFORMAT_D24_UNORM_S8_UINT, true },
		{ Resolve | SampledD32, SDL_GPU_TEXTUREFORMAT_D32_FLOAT_S8_UINT, true },
		{ PlainD24, SDL_GPU_TEXTUREFORMAT_D24_UNORM_S8_UINT, false },
		{ PlainD32, SDL_GPU_TEXTUREFORMAT_D32_FLOAT_S8_UINT, false },
		{ 0, SDL_GPU_TEXTUREFORMAT_INVALID, false }
	};
	for (unsigned int capabilities = 0; capabilities < 32; ++capabilities) {
		const SdlGpuDepthFormat result = select(capabilities);
		for (const Candidate &expected : candidates) {
			if ((capabilities & expected.Required) != expected.Required) continue;
			if (result.Format != expected.Format || result.Sampleable != expected.Sampleable) {
				++failures;
				printf("FAIL capabilities=%u: got format=%d sampled=%d; expected format=%d sampled=%d\n",
					capabilities, result.Format, result.Sampleable, expected.Format, expected.Sampleable);
			}
			break;
		}
	}

	// Regression: sampled D32 must not displace ordinary D24 when the R32F resolve is unavailable.
	const SdlGpuDepthFormat fallback = select(SampledD32 | PlainD24 | PlainD32);
	CHECK(fallback.Format == SDL_GPU_TEXTUREFORMAT_D24_UNORM_S8_UINT && !fallback.Sampleable);
	if (failures) {
		printf("sdl_gpu_depth_format_selfcheck: %d FAILED\n", failures);
		return 1;
	}
	printf("sdl_gpu_depth_format_selfcheck: 32 capability combinations and D24 compatibility fallback hold\n");
	return 0;
}
