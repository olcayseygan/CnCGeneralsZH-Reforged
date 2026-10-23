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

#ifndef SDL_GPU_DEPTH_FORMAT_H
#define SDL_GPU_DEPTH_FORMAT_H

#include <SDL3/SDL_gpu.h>

struct SdlGpuDepthFormat
{
	SDL_GPUTextureFormat Format;
	bool Sampleable;
};

// Keep the capability policy testable without creating a GPU device. The query has the same
// format/type/usage arguments as SDL_GPUTextureSupportsFormat after its device argument.
template <typename SupportsFormat>
SdlGpuDepthFormat Sdl_Select_Depth_Format(SupportsFormat supports)
{
	// Sampling scene depth also needs a sampled R32F colour target for the resolve. If it cannot
	// be created, retain the ordinary D24-first attachment preference for compatibility.
	const bool scene_depth = supports(SDL_GPU_TEXTUREFORMAT_R32_FLOAT, SDL_GPU_TEXTURETYPE_2D,
		SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER);
	const SDL_GPUTextureFormat formats[] = {
		SDL_GPU_TEXTUREFORMAT_D24_UNORM_S8_UINT, SDL_GPU_TEXTUREFORMAT_D32_FLOAT_S8_UINT };
	for (unsigned int sampled = scene_depth ? 1 : 0; ; --sampled) {
		const SDL_GPUTextureUsageFlags usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET
			| (sampled ? SDL_GPU_TEXTUREUSAGE_SAMPLER : 0);
		for (unsigned int i = 0; i < 2; ++i) {
			if (supports(formats[i], SDL_GPU_TEXTURETYPE_2D, usage)) {
				return { formats[i], sampled != 0 };
			}
		}
		if (!sampled) break;
	}
	return { SDL_GPU_TEXTUREFORMAT_INVALID, false };
}

#endif
