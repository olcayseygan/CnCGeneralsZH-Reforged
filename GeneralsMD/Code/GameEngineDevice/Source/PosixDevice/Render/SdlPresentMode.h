// SPDX-License-Identifier: GPL-3.0-or-later
// Additional terms under GNU GPL section 7 apply: see LICENSE.md.
#pragma once

#include <SDL3/SDL_gpu.h>

// VSYNC is guaranteed by SDL. An unavailable immediate mode keeps that safe
// fallback; mailbox would change the meaning of the player's VSync setting.
inline SDL_GPUPresentMode Sdl_Choose_Present_Mode(bool vsync, bool immediate_supported)
{
	return !vsync && immediate_supported ? SDL_GPU_PRESENTMODE_IMMEDIATE : SDL_GPU_PRESENTMODE_VSYNC;
}

// A backend may partly change its swapchain before reporting failure. Explicitly
// restore VSYNC rather than assuming a failed immediate request left it intact.
// `applied` is published only after an operation has succeeded.
template <class Apply>
bool Sdl_Apply_Present_Mode(SDL_GPUPresentMode requested, Apply apply, SDL_GPUPresentMode &applied)
{
	if (apply(requested)) {
		applied = requested;
		return true;
	}
	if (requested != SDL_GPU_PRESENTMODE_VSYNC && apply(SDL_GPU_PRESENTMODE_VSYNC)) {
		applied = SDL_GPU_PRESENTMODE_VSYNC;
		return true;
	}
	return false;
}
