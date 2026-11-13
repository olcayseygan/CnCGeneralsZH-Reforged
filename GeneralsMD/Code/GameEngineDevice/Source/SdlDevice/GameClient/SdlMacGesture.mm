// SPDX-License-Identifier: GPL-3.0-or-later
// Additional terms under GNU GPL section 7 apply: see LICENSE.md.

// AppKit's BOOL/Rect definitions must remain separate from engine headers/PreRTS.h.
#include "SdlDevice/GameClient/SdlMacGesture.h"
#import <AppKit/AppKit.h>
#include <SDL3/SDL.h>

namespace {
id monitor = nil;
SDL_Window *gameWindow = nullptr;
SdlMacScrollHandler scrollHandler = nullptr;

SdlGesturePhase phase(NSEventPhase value)
{
	if (value & NSEventPhaseCancelled) return SdlGesturePhase::Cancel;
	if (value & NSEventPhaseBegan) return SdlGesturePhase::Begin;
	if (value & NSEventPhaseEnded) return SdlGesturePhase::End;
	if (value & NSEventPhaseChanged) return SdlGesturePhase::Update;
	return SdlGesturePhase::None; // MayBegin is not yet an acquired gesture.
}

NSEvent *handleScroll(NSEvent *event)
{
	if (!gameWindow || !scrollHandler || !event.hasPreciseScrollingDeltas) return event;
	NSWindow *native = (__bridge NSWindow *)SDL_GetPointerProperty(SDL_GetWindowProperties(gameWindow),
		SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, nullptr);
	if (!native || event.window != native) return event;
	NSView *view = native.contentView;
	if (!view) return event;
	const NSPoint point = [view convertPoint:event.locationInWindow fromView:nil];
	const NSRect bounds = view.bounds;
	SdlMacScroll scroll = {};
	scroll.windowID = SDL_GetWindowID(gameWindow);
	scroll.x = point.x - bounds.origin.x;
	scroll.y = [view isFlipped] ? point.y - bounds.origin.y : NSMaxY(bounds) - point.y;
	// NSScrollView moves content right/down for positive precise X/Y respectively.
	// These values already include natural scrolling; do not invert them a second time.
	scroll.deltaX = event.scrollingDeltaX;
	scroll.deltaY = event.scrollingDeltaY;
	scroll.phase = phase(event.phase);
	scroll.momentum = phase(event.momentumPhase);
	scroll.focused = NSApp.active && native.keyWindow && native.visible;
	scroll.inside = NSPointInRect(point, bounds);
	scroll.buttonsDown = NSEvent.pressedMouseButtons != 0;
	return scrollHandler(scroll) ? nil : event;
}
} // namespace

bool SdlMacGesture_buttonsDown() noexcept
{
	return [NSThread isMainThread] && NSEvent.pressedMouseButtons != 0;
}

void SdlMacGesture_remove() noexcept
{
	if (![NSThread isMainThread]) return;
	if (monitor) [NSEvent removeMonitor:monitor];
	monitor = nil;
	gameWindow = nullptr;
	scrollHandler = nullptr;
}

bool SdlMacGesture_install(SDL_Window *window, SdlMacScrollHandler handler) noexcept
{
	if (![NSThread isMainThread]) return false;
	SdlMacGesture_remove();
	if (!window || !handler) return false;
	@autoreleasepool {
		@try {
			gameWindow = window;
			scrollHandler = handler;
			monitor = [NSEvent addLocalMonitorForEventsMatchingMask:NSEventMaskScrollWheel
				handler:^NSEvent *(NSEvent *event) { return handleScroll(event); }];
			if (!monitor) SdlMacGesture_remove();
			return monitor != nil;
		} @catch (NSException *exception) {
			(void)exception;
			SdlMacGesture_remove();
			SDL_LogWarn(SDL_LOG_CATEGORY_INPUT, "macOS trackpad pan monitor unavailable; keeping wheel input.");
			return false;
		}
	}
}
