// SPDX-License-Identifier: GPL-3.0-or-later
// Additional terms under GNU GPL section 7 apply: see LICENSE.md.
#pragma once
struct SDL_Window;
struct SDL_Rect;

// Called before SDL video initialization; an explicit SDL hint takes priority.
void SdlMacWindow_configureFullscreen() noexcept;
// Returns the camera housing in window coordinates when it overlaps the window.
bool SdlMacWindow_getDisplayCutout(SDL_Window *window, SDL_Rect *rect) noexcept;
