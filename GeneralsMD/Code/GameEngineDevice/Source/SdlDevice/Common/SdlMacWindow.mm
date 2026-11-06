// SPDX-License-Identifier: GPL-3.0-or-later
// Additional terms under GNU GPL section 7 apply: see LICENSE.md.

#include "SdlDevice/Common/SdlMacWindow.h"
#import <AppKit/AppKit.h>
#include <SDL3/SDL.h>
#include <cmath>

void SdlMacWindow_configureFullscreen() noexcept
{
	if (![NSThread isMainThread])
		return;
	@autoreleasepool {
		@try {
			// SDL reads this policy once at video initialization. Check every attached display so
			// moving the game to the built-in panel later can still use its entire drawable area.
			for (NSScreen *screen in [NSScreen screens]) {
				if (screen.safeAreaInsets.top > 0
					&& (!NSIsEmptyRect( screen.auxiliaryTopLeftArea ) || !NSIsEmptyRect( screen.auxiliaryTopRightArea ))) {
					SDL_SetHintWithPriority( SDL_HINT_VIDEO_MAC_FULLSCREEN_SPACES, "0", SDL_HINT_DEFAULT );
					return;
				}
			}
		} @catch (NSException *exception) {
			(void)exception; // A headless session keeps the existing native-fullscreen policy.
		}
	}
}

bool SdlMacWindow_getDisplayCutout( SDL_Window *window, SDL_Rect *rect ) noexcept
{
	if (rect == NULL)
		return false;
	*rect = {};
	if (window == NULL || ![NSThread isMainThread])
		return false;
	@autoreleasepool {
		@try {
			NSWindow *native = (__bridge NSWindow *)SDL_GetPointerProperty( SDL_GetWindowProperties( window ),
				SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, NULL );
			NSScreen *screen = native.screen;
			NSView *view = native.contentView;
			if (screen == nil || view == nil || screen.safeAreaInsets.top <= 0)
				return false;
			const NSRect left = screen.auxiliaryTopLeftArea;
			const NSRect right = screen.auxiliaryTopRightArea;
			if (NSIsEmptyRect( left ) || NSIsEmptyRect( right ) || NSMinX( right ) <= NSMaxX( left ))
				return false;
			// NSScreen rectangles are global, bottom-left coordinates. Use the two unobscured
			// regions to obtain the actual housing width, including off-centre display layouts.
			const NSRect housing = NSMakeRect( NSMaxX( left ), NSMaxY( screen.frame ) - screen.safeAreaInsets.top,
				NSMinX( right ) - NSMaxX( left ), screen.safeAreaInsets.top );
			const NSRect bounds = view.bounds;
			const NSRect local = NSIntersectionRect( bounds,
				[view convertRect:[native convertRectFromScreen:housing] fromView:nil] );
			int width = 0, height = 0;
			if (NSIsEmptyRect( local ) || bounds.size.width <= 0 || bounds.size.height <= 0
				|| !SDL_GetWindowSize( window, &width, &height ) || width <= 0 || height <= 0)
				return false;
			const double scaleX = double(width) / bounds.size.width;
			const double scaleY = double(height) / bounds.size.height;
			const double top = view.flipped ? NSMinY( local ) - NSMinY( bounds ) : NSMaxY( bounds ) - NSMaxY( local );
			const int x1 = int(std::floor( (NSMinX( local ) - NSMinX( bounds )) * scaleX ));
			const int y1 = int(std::floor( top * scaleY ));
			const int x2 = int(std::ceil( (NSMaxX( local ) - NSMinX( bounds )) * scaleX ));
			const int y2 = int(std::ceil( (top + local.size.height) * scaleY ));
			*rect = { x1, y1, x2 - x1, y2 - y1 };
			return rect->w > 0 && rect->h > 0;
		} @catch (NSException *exception) {
			(void)exception;
			*rect = {};
			return false;
		}
	}
}
