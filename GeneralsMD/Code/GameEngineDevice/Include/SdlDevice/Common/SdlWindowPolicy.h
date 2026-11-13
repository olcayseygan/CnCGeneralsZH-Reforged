// SPDX-License-Identifier: GPL-3.0-or-later
// Additional terms under GNU GPL section 7 apply: see LICENSE.md.
#pragma once

#include <SDL3/SDL_video.h>

// Call before starting SDL video. Environment and explicit SDL hints take priority.
void SdlWindowPolicy_configure();

// Only a visible foreground game should prevent display sleep on macOS.
// Zero flags describe an offscreen run without a window. Other platforms keep SDL's policy.
void SdlWindowPolicy_updateScreenSaver( SDL_WindowFlags flags );
