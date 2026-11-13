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
/*
 * C2's SDL side of two engine seams, without a screen: SDL3's offscreen video driver stands in for the
 * machine's displays.
 *   - Monitors.h over SdlDisplays: with no table the one 800x600 monitor; with it, SDL's displays in
 *     pixels, named "\\.\DISPLAY<n>", found by name with the primary for an unknown one, and each
 *     one's sizes sorted, once each, none under the game's floor, the desktop's own among them.
 *   - SdlMessageBox's layout for MessageBoxWrapper's flags: the buttons Windows shows, in its order,
 *     answering its IDs, the default button and the icon.  No box is shown: that needs a person.
 * What it cannot see: a real display's modes (the offscreen driver has one), and a box on screen.
 */
#include "test_harness.h"

#include "PreRTS.h"
#include "Common/MessageBoxFlags.h"
#include "Common/Monitors.h"
#include "SdlDevice/Common/SdlDisplays.h"
#include "SdlDevice/Common/SdlMessageBox.h"
#include "SdlDevice/Common/SdlPanel.h"
#include "SdlDevice/Common/SdlWindowPolicy.h"

#include <SDL3/SDL.h>

#include <math.h>
#include <string.h>
#include <string>

namespace {

bool start_offscreen_video()
{
	static bool started = false;
	if (!started) {
		SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "offscreen");
		started = SDL_Init(SDL_INIT_VIDEO);
		if (!started) printf("  SDL_Init: %s\n", SDL_GetError());
	}
	return started;
}

} // namespace

TEST(monitors_without_a_platform_table_are_the_one_floor_sized_monitor)
{
	ThePlatformDisplays = NULL;
	MonitorEntry monitors[MAX_MONITOR_ENTRIES];
	CHECK_EQ(listMonitors(monitors, MAX_MONITOR_ENTRIES), 1);
	CHECK_EQ((int)monitors[0].rect.right, 800);
	CHECK_EQ((int)monitors[0].rect.bottom, 600);
	CHECK(monitors[0].primary);
	DisplayModeEntry modes[MAX_DISPLAY_MODE_ENTRIES];
	CHECK_EQ(listDisplayModes("", modes, MAX_DISPLAY_MODE_ENTRIES), 1);
	CHECK_EQ(modes[0].width, 800);
}

TEST(monitors_come_from_sdl_in_pixels_and_by_name)
{
	CHECK(start_offscreen_video());
	ThePlatformDisplays = &TheSdlDisplays;

	int count = 0;
	SDL_DisplayID *displays = SDL_GetDisplays(&count);
	CHECK(displays != NULL && count >= 1);

	MonitorEntry monitors[MAX_MONITOR_ENTRIES];
	const int listed = listMonitors(monitors, MAX_MONITOR_ENTRIES);
	CHECK_EQ(listed, count);
	int primaries = 0;
	for (int i = 0; i < listed && displays != NULL; ++i) {
		char device[32];
		snprintf(device, sizeof(device), "\\\\.\\DISPLAY%d", i + 1);
		CHECK_STR(monitors[i].device, device);
		CHECK_EQ(monitors[i].number, i + 1);
		SDL_Rect bounds;
		CHECK(SDL_GetDisplayBounds(displays[i], &bounds));
		const SDL_DisplayMode *desktop = SDL_GetDesktopDisplayMode(displays[i]);
		const float density = (desktop && desktop->pixel_density > 0) ? desktop->pixel_density : 1.0f;
		CHECK_EQ((long)(monitors[i].rect.right - monitors[i].rect.left), lround(bounds.w * (double)density));
		CHECK_EQ((long)(monitors[i].rect.bottom - monitors[i].rect.top), lround(bounds.h * (double)density));
		if (monitors[i].primary) {
			++primaries;
			CHECK_EQ(displays[i], SDL_GetPrimaryDisplay());
		}
		printf("  %s \"%s\" %ldx%ld%s\n", monitors[i].device, monitors[i].name,
			(long)(monitors[i].rect.right - monitors[i].rect.left), (long)(monitors[i].rect.bottom - monitors[i].rect.top),
			monitors[i].primary ? " primary" : "");
	}
	CHECK_EQ(primaries, 1);

	// by name, and the primary for a name no monitor has (Options.ini naming an unplugged one)
	CHECK_STR(findMonitor("\\\\.\\display1").device, "\\\\.\\DISPLAY1");
	CHECK(findMonitor("\\\\.\\DISPLAY99").primary);
	CHECK(findMonitor(NULL).primary);
	SDL_free(displays);
	ThePlatformDisplays = NULL;
}

TEST(display_modes_come_from_sdl_sorted_once_each_and_above_the_floor)
{
	CHECK(start_offscreen_video());
	ThePlatformDisplays = &TheSdlDisplays;

	DisplayModeEntry modes[MAX_DISPLAY_MODE_ENTRIES];
	const int count = listDisplayModes("\\\\.\\DISPLAY1", modes, MAX_DISPLAY_MODE_ENTRIES);
	CHECK(count >= 1);
	for (int i = 0; i < count; ++i) {
		CHECK(modes[i].width >= MIN_DISPLAY_MODE_WIDTH && modes[i].height >= MIN_DISPLAY_MODE_HEIGHT);
		if (i > 0)
			CHECK(modes[i - 1].width < modes[i].width
				|| (modes[i - 1].width == modes[i].width && modes[i - 1].height < modes[i].height));
	}
	const SDL_DisplayMode *desktop = SDL_GetDesktopDisplayMode(SDL_GetPrimaryDisplay());
	bool hasDesktop = false;
	for (int i = 0; i < count && desktop; ++i)
		hasDesktop |= modes[i].width == (int)lround(desktop->w * (double)desktop->pixel_density)
			&& modes[i].height == (int)lround(desktop->h * (double)desktop->pixel_density);
	CHECK(hasDesktop);
	ThePlatformDisplays = NULL;
}

namespace {

// Two monitors as a desk might have them: a 2560x1440 primary, and a Retina panel of 1512x982 points
// to its right, which is 3024x1964 pixels.
int fake_monitors(MonitorEntry *entries, int capacity)
{
	if (capacity < 2) return 0;
	memset(entries, 0, 2 * sizeof(entries[0]));
	strcpy(entries[0].device, "\\\\.\\DISPLAY1");
	entries[0].number = 1;
	entries[0].rect.right = 2560;
	entries[0].rect.bottom = 1440;
	entries[0].primary = true;
	strcpy(entries[1].device, "\\\\.\\DISPLAY2");
	entries[1].number = 2;
	entries[1].rect.left = 2560;
	entries[1].rect.right = 2560 + 3024;
	entries[1].rect.bottom = 1964;
	return 2;
}

int fake_modes(const char *, DisplayModeEntry *entries, int capacity)
{
	if (capacity < 1) return 0;
	entries[0].width = 800;
	entries[0].height = 600;
	return 1;
}

const PlatformDisplays TheFakeDisplays = { fake_monitors, fake_modes };

// An M2 MacBook Air in its "More Space" mode, a 3420x2224 store on a 2560x1664 panel, and beside it a display whose
// panel the platform cannot name.
int fake_scaled_monitors(MonitorEntry *entries, int capacity)
{
	if (capacity < 2) return 0;
	fake_monitors(entries, capacity);
	entries[0].rect.right = 3420;
	entries[0].rect.bottom = 2224;
	entries[1].rect.left = 3420;
	entries[1].rect.right = 3420 + 3024;
	return 2;
}

bool fake_panel(const char *device, int *width, int *height)
{
	if (strcmp(device, "\\\\.\\DISPLAY1") != 0) return false;
	*width = 2560;
	*height = 1664;
	return true;
}

const PlatformDisplays TheFakeScaledDisplays = { fake_scaled_monitors, fake_modes, fake_panel };

} // namespace

// Options.ini names no resolution on a first run: the game starts at the monitor's own size in pixels,
// the one Options.ini's Monitor names or the primary, and at the floor when there is no display at all.
TEST(a_first_run_starts_at_the_monitors_own_size)
{
	int width = 0, height = 0;
	ThePlatformDisplays = NULL;
	firstRunResolution("", &width, &height);
	CHECK_EQ(width, 800);		// headless: no displays, the floor as before
	CHECK_EQ(height, 600);

	ThePlatformDisplays = &TheFakeDisplays;
	firstRunResolution("", &width, &height);
	CHECK_EQ(width, 2560);
	CHECK_EQ(height, 1440);
	firstRunResolution("\\\\.\\DISPLAY2", &width, &height);
	CHECK_EQ(width, 3024);		// pixels, not the panel's points
	CHECK_EQ(height, 1964);
	firstRunResolution("\\\\.\\DISPLAY7", &width, &height);		// unplugged since: the primary
	CHECK_EQ(width, 2560);
	CHECK_EQ(height, 1440);

	// Over SDL's own list: the offscreen driver's display, at its desktop size in pixels, brought down to a
	// panel only where CoreGraphics has a display at the same place and size.
	CHECK(start_offscreen_video());
	ThePlatformDisplays = &TheSdlDisplays;
	firstRunResolution(NULL, &width, &height);
	const SDL_DisplayMode *desktop = SDL_GetDesktopDisplayMode(SDL_GetPrimaryDisplay());
	SDL_Rect bounds;
	CHECK(desktop != NULL);
	CHECK(SDL_GetDisplayBounds(SDL_GetPrimaryDisplay(), &bounds));
	if (desktop != NULL) {
		const double density = desktop->pixel_density > 0 ? desktop->pixel_density : 1.0;
		int panelWidth = 0, panelHeight = 0, expectWidth = 0, expectHeight = 0;
		if (!SdlPanel_nativePixels(bounds.x, bounds.y, bounds.w, bounds.h, &panelWidth, &panelHeight))
			panelWidth = panelHeight = 0;
		fitToPanel((int)lround(desktop->w * density), (int)lround(desktop->h * density), panelWidth, panelHeight,
			&expectWidth, &expectHeight);
		CHECK_EQ(width, expectWidth);
		CHECK_EQ(height, expectHeight);
	}
	ThePlatformDisplays = NULL;
}

// A scaled Mac draws its desktop into a backing store bigger than the panel: a first run draws at the
// panel's pixels instead, keeping the monitor's shape.
TEST(a_first_run_on_a_scaled_display_starts_at_the_panels_pixels)
{
	int width = 0, height = 0;
	fitToPanel(3420, 2224, 2560, 1664, &width, &height);		// an M2 MacBook Air's "More Space": the panel
	CHECK_EQ(width, 2560);
	CHECK_EQ(height, 1664);
	fitToPanel(2560, 1664, 2560, 1664, &width, &height);		// the default mode: the panel already
	CHECK_EQ(width, 2560);
	CHECK_EQ(height, 1664);
	fitToPanel(2048, 1332, 2560, 1664, &width, &height);		// "Larger Text": a smaller store is kept
	CHECK_EQ(width, 2048);
	CHECK_EQ(height, 1332);
	fitToPanel(3420, 2136, 2560, 1664, &width, &height);		// another shape (16:10): the monitor's, fitted
	CHECK_EQ(width, 2560);
	CHECK_EQ(height, 1599);
	fitToPanel(3840, 2160, 0, 0, &width, &height);					// no panel known
	CHECK_EQ(width, 3840);
	CHECK_EQ(height, 2160);
	fitToPanel(6016, 3384, 5120, 2880, &width, &height);		// a scaled 5K external, 16:9 on both
	CHECK_EQ(width, 5120);
	CHECK_EQ(height, 2880);

	// Through the table: the monitor Options.ini names, and a platform that cannot say.
	ThePlatformDisplays = &TheFakeScaledDisplays;
	firstRunResolution("\\\\.\\DISPLAY1", &width, &height);
	CHECK_EQ(width, 2560);
	CHECK_EQ(height, 1664);
	firstRunResolution("\\\\.\\DISPLAY2", &width, &height);
	CHECK_EQ(width, 3024);
	CHECK_EQ(height, 1964);
	ThePlatformDisplays = &TheFakeDisplays;		// no panelSize: the monitor's pixels, as before
	firstRunResolution("", &width, &height);
	CHECK_EQ(width, 2560);
	CHECK_EQ(height, 1440);
	ThePlatformDisplays = NULL;
}

TEST(message_box_layout_is_windows_buttons_ids_default_and_icon)
{
	// The assertion box: Abort, Retry, Ignore, Ignore the default, an error.
	SdlMessageBoxLayout layout = sdlMessageBoxLayout(MSGBOX_ABORTRETRYIGNORE | MSGBOX_ICONERROR | MSGBOX_DEFBUTTON3 | MSGBOX_TASKMODAL);
	CHECK_EQ(layout.numButtons, 3);
	CHECK_EQ(layout.buttons[0].buttonID, (int)MSGBOX_ID_ABORT);
	CHECK_EQ(layout.buttons[1].buttonID, (int)MSGBOX_ID_RETRY);
	CHECK_EQ(layout.buttons[2].buttonID, (int)MSGBOX_ID_IGNORE);
	CHECK((layout.buttons[2].flags & SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT) != 0);
	CHECK((layout.buttons[0].flags & SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT) == 0);
	CHECK((layout.flags & SDL_MESSAGEBOX_ERROR) != 0);
	CHECK((layout.flags & SDL_MESSAGEBOX_BUTTONS_LEFT_TO_RIGHT) != 0);

	// Yes/No with a warning: MSGBOX_ICONWARNING (0x30) shares a bit with MSGBOX_ICONERROR (0x10).
	layout = sdlMessageBoxLayout(MSGBOX_YESNO | MSGBOX_ICONWARNING);
	CHECK_EQ(layout.numButtons, 2);
	CHECK_EQ(layout.buttons[0].buttonID, (int)MSGBOX_ID_YES);
	CHECK_EQ(layout.buttons[1].buttonID, (int)MSGBOX_ID_NO);
	CHECK((layout.buttons[0].flags & SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT) != 0);
	CHECK((layout.flags & SDL_MESSAGEBOX_WARNING) != 0);
	CHECK((layout.flags & SDL_MESSAGEBOX_ERROR) == 0);

	// OK alone: one button, information.
	layout = sdlMessageBoxLayout(MSGBOX_OK);
	CHECK_EQ(layout.numButtons, 1);
	CHECK_EQ(layout.buttons[0].buttonID, (int)MSGBOX_ID_OK);
	CHECK((layout.flags & SDL_MESSAGEBOX_INFORMATION) != 0);

	// DEFBUTTON3 on a two-button box has no third button: the first stays the default.
	layout = sdlMessageBoxLayout(MSGBOX_YESNO | MSGBOX_DEFBUTTON3);
	CHECK((layout.buttons[0].flags & SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT) != 0);
}

TEST(message_box_hook_is_set_only_while_there_is_an_owner)
{
	CHECK(TheMessageBoxHook == NULL);
	CHECK(start_offscreen_video());
	SDL_Window *window = SDL_CreateWindow("test_sdl_platform", 320, 240, SDL_WINDOW_HIDDEN);
	CHECK(window != NULL);
	setSdlMessageBoxOwner(window);
	CHECK(TheMessageBoxHook != NULL);
	setSdlMessageBoxOwner(NULL);
	CHECK(TheMessageBoxHook == NULL);
	SDL_DestroyWindow(window);
}

TEST(macos_fullscreen_defaults_preserve_environment_and_explicit_choices)
{
	SDL_ResetHint(SDL_HINT_VIDEO_MAC_FULLSCREEN_SPACES);
	SDL_ResetHint(SDL_HINT_VIDEO_MAC_FULLSCREEN_MENU_VISIBILITY);
	const char *spaces = SDL_GetHint(SDL_HINT_VIDEO_MAC_FULLSCREEN_SPACES);
	const char *menu = SDL_GetHint(SDL_HINT_VIDEO_MAC_FULLSCREEN_MENU_VISIBILITY);
	const bool hadSpaces = spaces != NULL, hadMenu = menu != NULL;
	const std::string previousSpaces = spaces != NULL ? spaces : "";
	const std::string previousMenu = menu != NULL ? menu : "";
	SdlWindowPolicy_configure();
#if defined(__APPLE__)
	CHECK_STR(SDL_GetHint(SDL_HINT_VIDEO_MAC_FULLSCREEN_SPACES), hadSpaces ? previousSpaces.c_str() : "1");
	CHECK_STR(SDL_GetHint(SDL_HINT_VIDEO_MAC_FULLSCREEN_MENU_VISIBILITY), hadMenu ? previousMenu.c_str() : "0");
#else
	CHECK(hadSpaces || SDL_GetHint(SDL_HINT_VIDEO_MAC_FULLSCREEN_SPACES) == NULL);
	CHECK(hadMenu || SDL_GetHint(SDL_HINT_VIDEO_MAC_FULLSCREEN_MENU_VISIBILITY) == NULL);
	if (hadSpaces) CHECK_STR(SDL_GetHint(SDL_HINT_VIDEO_MAC_FULLSCREEN_SPACES), previousSpaces.c_str());
	if (hadMenu) CHECK_STR(SDL_GetHint(SDL_HINT_VIDEO_MAC_FULLSCREEN_MENU_VISIBILITY), previousMenu.c_str());
#endif
	SDL_Environment *environment = SDL_GetEnvironment();
	CHECK(SDL_SetEnvironmentVariable(environment, SDL_HINT_VIDEO_MAC_FULLSCREEN_SPACES, "0", true));
	CHECK(SDL_SetEnvironmentVariable(environment, SDL_HINT_VIDEO_MAC_FULLSCREEN_MENU_VISIBILITY, "1", true));
	SdlWindowPolicy_configure();
	CHECK_STR(SDL_GetHint(SDL_HINT_VIDEO_MAC_FULLSCREEN_SPACES), "0");
	CHECK_STR(SDL_GetHint(SDL_HINT_VIDEO_MAC_FULLSCREEN_MENU_VISIBILITY), "1");
	if (hadSpaces) SDL_SetEnvironmentVariable(environment, SDL_HINT_VIDEO_MAC_FULLSCREEN_SPACES, previousSpaces.c_str(), true);
	else SDL_UnsetEnvironmentVariable(environment, SDL_HINT_VIDEO_MAC_FULLSCREEN_SPACES);
	if (hadMenu) SDL_SetEnvironmentVariable(environment, SDL_HINT_VIDEO_MAC_FULLSCREEN_MENU_VISIBILITY, previousMenu.c_str(), true);
	else SDL_UnsetEnvironmentVariable(environment, SDL_HINT_VIDEO_MAC_FULLSCREEN_MENU_VISIBILITY);

	SDL_SetHintWithPriority(SDL_HINT_VIDEO_MAC_FULLSCREEN_SPACES, "0", SDL_HINT_OVERRIDE);
	SDL_SetHintWithPriority(SDL_HINT_VIDEO_MAC_FULLSCREEN_MENU_VISIBILITY, "1", SDL_HINT_OVERRIDE);
	SdlWindowPolicy_configure();
	CHECK_STR(SDL_GetHint(SDL_HINT_VIDEO_MAC_FULLSCREEN_SPACES), "0");
	CHECK_STR(SDL_GetHint(SDL_HINT_VIDEO_MAC_FULLSCREEN_MENU_VISIBILITY), "1");
	SDL_ResetHint(SDL_HINT_VIDEO_MAC_FULLSCREEN_SPACES);
	SDL_ResetHint(SDL_HINT_VIDEO_MAC_FULLSCREEN_MENU_VISIBILITY);
}

TEST(macos_display_sleep_tracks_visibility_focus_and_user_preference)
{
	CHECK(start_offscreen_video());
	const bool previouslyEnabled = SDL_ScreenSaverEnabled();
	SDL_SetHintWithPriority(SDL_HINT_VIDEO_ALLOW_SCREENSAVER, "0", SDL_HINT_OVERRIDE);
#if defined(__APPLE__)
	SdlWindowPolicy_updateScreenSaver(SDL_WINDOW_INPUT_FOCUS);
	CHECK(!SDL_ScreenSaverEnabled());
	const SDL_WindowFlags inactive[] = {0, SDL_WINDOW_HIDDEN, SDL_WINDOW_MINIMIZED, SDL_WINDOW_OCCLUDED};
	for (unsigned int index = 0; index < sizeof(inactive) / sizeof(inactive[0]); ++index) {
		SdlWindowPolicy_updateScreenSaver(index == 0 ? 0 : SDL_WINDOW_INPUT_FOCUS | inactive[index]);
		CHECK(SDL_ScreenSaverEnabled());
		SdlWindowPolicy_updateScreenSaver(SDL_WINDOW_INPUT_FOCUS);
		CHECK(!SDL_ScreenSaverEnabled());
	}
	SDL_SetHintWithPriority(SDL_HINT_VIDEO_ALLOW_SCREENSAVER, "1", SDL_HINT_OVERRIDE);
	SdlWindowPolicy_updateScreenSaver(SDL_WINDOW_INPUT_FOCUS);
	CHECK(SDL_ScreenSaverEnabled());
#else
	SDL_EnableScreenSaver();
	SdlWindowPolicy_updateScreenSaver(SDL_WINDOW_INPUT_FOCUS);
	CHECK(SDL_ScreenSaverEnabled());
	SDL_DisableScreenSaver();
	SdlWindowPolicy_updateScreenSaver(0);
	CHECK(!SDL_ScreenSaverEnabled());
#endif
	SDL_ResetHint(SDL_HINT_VIDEO_ALLOW_SCREENSAVER);
	if (previouslyEnabled) SDL_EnableScreenSaver(); else SDL_DisableScreenSaver();
}
