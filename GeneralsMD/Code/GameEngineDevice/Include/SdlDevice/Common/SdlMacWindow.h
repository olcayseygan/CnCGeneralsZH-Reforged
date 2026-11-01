// SPDX-License-Identifier: GPL-3.0-or-later
// Additional terms under GNU GPL section 7 apply: see LICENSE.md.

#pragma once

struct SDL_Window;
struct SDL_Rect;

// Call after SdlWindowPolicy_configure(), before SDL video initialization. A display with a
// camera housing defaults to full-panel fullscreen; an explicit SDL Spaces hint takes priority.
// Main thread only. No active display keeps the native fullscreen default.
void SdlMacWindow_configureFullscreen() noexcept;

// Main thread only. NULL releases the presentation options changed by this helper.
// The window is borrowed for this call; no native window pointer is retained.
void SdlMacWindow_updatePresentation( SDL_Window *window ) noexcept;

// The physical camera housing intersecting this borrowed window, in SDL window coordinates
// (logical points, origin at the top left). False means no obscured content; rect is zeroed.
// Main thread only. This is a geometry query and retains no native window or screen.
bool SdlMacWindow_getDisplayCutout( SDL_Window *window, SDL_Rect *rect ) noexcept;
