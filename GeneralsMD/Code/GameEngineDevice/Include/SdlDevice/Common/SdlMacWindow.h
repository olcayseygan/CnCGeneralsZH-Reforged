// SPDX-License-Identifier: GPL-3.0-or-later
// Additional terms under GNU GPL section 7 apply: see LICENSE.md.

#pragma once

struct SDL_Window;

// Main thread only. NULL releases the presentation options changed by this helper.
// The window is borrowed for this call; no native window pointer is retained.
void SdlMacWindow_updatePresentation( SDL_Window *window ) noexcept;
