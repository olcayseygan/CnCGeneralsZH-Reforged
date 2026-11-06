// SPDX-License-Identifier: GPL-3.0-or-later
// Additional terms under GNU GPL section 7 apply: see LICENSE.md.

#include "SdlDevice/Common/SdlWindowPolicy.h"

#include <SDL3/SDL_hints.h>

void SdlWindowPolicy_configure()
{
#if defined(__APPLE__)
	// Native fullscreen permits macOS Game Mode. Hide the menu at the scrolling edge;
	// users can still select legacy fullscreen through SDL_VIDEO_MAC_FULLSCREEN_SPACES=0.
	SDL_SetHintWithPriority( SDL_HINT_VIDEO_MAC_FULLSCREEN_SPACES, "1", SDL_HINT_DEFAULT );
	SDL_SetHintWithPriority( SDL_HINT_VIDEO_MAC_FULLSCREEN_MENU_VISIBILITY, "0", SDL_HINT_DEFAULT );
#endif
}

void SdlWindowPolicy_updateScreenSaver( SDL_WindowFlags flags )
{
#if defined(__APPLE__)
	const bool visible = (flags & (SDL_WINDOW_HIDDEN | SDL_WINDOW_MINIMIZED | SDL_WINDOW_OCCLUDED)) == 0;
	const bool focused = (flags & SDL_WINDOW_INPUT_FOCUS) != 0;
	if (visible && focused && !SDL_GetHintBoolean( SDL_HINT_VIDEO_ALLOW_SCREENSAVER, false ))
		SDL_DisableScreenSaver();
	else
		SDL_EnableScreenSaver();
#else
	(void)flags;
#endif
}
