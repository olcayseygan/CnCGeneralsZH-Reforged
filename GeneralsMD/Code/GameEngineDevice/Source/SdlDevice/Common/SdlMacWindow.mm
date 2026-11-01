// SPDX-License-Identifier: GPL-3.0-or-later
// Additional terms under GNU GPL section 7 apply: see LICENSE.md.

// Keep AppKit's types separate from PreRTS.h and the engine's platform typedefs.
#include "SdlDevice/Common/SdlMacWindow.h"

#import <AppKit/AppKit.h>
#include <SDL3/SDL.h>

namespace {

const NSUInteger DockOptions = NSApplicationPresentationAutoHideDock | NSApplicationPresentationHideDock;
const NSUInteger MenuOptions = NSApplicationPresentationAutoHideMenuBar | NSApplicationPresentationHideMenuBar;
// AutoHideToolbar depends on FullScreen and AutoHideMenuBar, so hiding the menu
// must temporarily clear it too. The FullScreen bit belongs to AppKit.
const NSUInteger ManagedOptions = DockOptions | MenuOptions | NSApplicationPresentationAutoHideToolbar;

NSUInteger savedOptions = 0;
NSUInteger appliedOptions = 0;
bool ownsOptions = false;

bool validOptions( NSUInteger options )
{
	if ((options & DockOptions) == DockOptions || (options & MenuOptions) == MenuOptions)
		return false;
	if ((options & NSApplicationPresentationHideMenuBar) && !(options & NSApplicationPresentationHideDock))
		return false;
	if ((options & NSApplicationPresentationAutoHideMenuBar) && !(options & DockOptions))
		return false;
	const NSUInteger needsDock = NSApplicationPresentationDisableProcessSwitching
		| NSApplicationPresentationDisableForceQuit | NSApplicationPresentationDisableSessionTermination
		| NSApplicationPresentationDisableMenuBarTransparency;
	if ((options & needsDock) && !(options & DockOptions))
		return false;
	if ((options & NSApplicationPresentationAutoHideToolbar)
		&& (!(options & NSApplicationPresentationFullScreen) || !(options & NSApplicationPresentationAutoHideMenuBar)))
		return false;
	return true;
}

} // namespace

void SdlMacWindow_updatePresentation( SDL_Window *window ) noexcept
{
	if (![NSThread isMainThread])
		return;
	@autoreleasepool {
		@try {
			NSApplication *application = NSApp;
			if (application == nil)
				return;
			NSWindow *nativeWindow = nil;
			if (window != NULL) {
				nativeWindow = (__bridge NSWindow *)SDL_GetPointerProperty( SDL_GetWindowProperties( window ),
					SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, NULL );
			}
			const bool hidesMenu = !SDL_GetHintBoolean( SDL_HINT_VIDEO_MAC_FULLSCREEN_MENU_VISIBILITY, true );
			const bool foregroundFullscreen = hidesMenu && nativeWindow != nil && application.active
				&& nativeWindow.keyWindow && nativeWindow.visible
				&& (nativeWindow.styleMask & NSWindowStyleMaskFullScreen) != 0;
			const NSUInteger current = application.presentationOptions;
			const NSUInteger currentManaged = current & ManagedOptions;
			// AppKit resets these options together during Spaces transitions. A
			// change to any managed flag supersedes the whole saved group.
			if (ownsOptions && currentManaged != appliedOptions)
				ownsOptions = false;
			if (foregroundFullscreen) {
				const NSUInteger desired = (current & ~ManagedOptions)
					| NSApplicationPresentationHideDock | NSApplicationPresentationHideMenuBar;
				if (desired != current) {
					const NSUInteger saved = ownsOptions ? savedOptions : currentManaged;
					application.presentationOptions = static_cast<NSApplicationPresentationOptions>( desired );
					savedOptions = saved;
					appliedOptions = desired & ManagedOptions;
					ownsOptions = true;
				}
			} else if (ownsOptions) {
				// Restore our group only while it is still intact; preserve all
				// presentation options outside the Dock/menu group.
				NSUInteger desired = (current & ~ManagedOptions) | savedOptions;
				if (!(desired & NSApplicationPresentationFullScreen) || !(desired & NSApplicationPresentationAutoHideMenuBar))
					desired &= ~NSApplicationPresentationAutoHideToolbar;
				if (validOptions( desired )) {
					if (desired != current)
						application.presentationOptions = static_cast<NSApplicationPresentationOptions>( desired );
				} else {
					SDL_LogWarn( SDL_LOG_CATEGORY_APPLICATION,
						"Window presentation changed externally; keeping the current valid options." );
				}
				ownsOptions = false;
			}
		} @catch (NSException *exception) {
			(void)exception;
			SDL_LogWarn( SDL_LOG_CATEGORY_APPLICATION, "macOS declined the window presentation change." );
		}
	}
}
