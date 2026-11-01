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

// SdlInput.cpp: see SdlInput.h.

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "GameClient/Display.h"
#include "Common/GlobalData.h"
#include "Common/GameEngine.h"
#include "GameClient/IMEManagerPosix.h"
#include "GameClient/LookAtXlat.h"
#include "SdlDevice/GameClient/SdlInput.h"
#include "SdlDevice/GameClient/SdlKeyTable.h"
#include "SdlDevice/GameClient/SdlKeyboard.h"
#include "SdlDevice/GameClient/SdlMouse.h"
#include "SdlDevice/GameClient/SdlMacGesture.h"

#include <SDL3/SDL.h>

#include <math.h>
#include <cmath>

namespace {

Real theWheelCarry = 0.0f;		///< the fraction of a WM_MOUSEWHEEL unit not yet reported

SDL_Window *theGestureWindow = NULL; // Borrowed; detached before SDL destroys it.
Uint32 theScrollEventType = 0;
SdlScrollGestureTarget thePanTarget, thePinchTarget;
SDL_WindowID thePinchWindow = 0;
uint64_t thePanSequence = 0, theCancelledPanSequence = 0;
SdlScrollGestureQueue theScrolls;
SdlPendingCameraGestures thePendingGestures;

bool scrollEventType()
{
	if (!theScrollEventType) theScrollEventType = SDL_RegisterEvents(1);
	return theScrollEventType != 0;
}

SDL_Window *gestureWindow()
{
	return theGestureWindow ? theGestureWindow : SdlInput_gameWindow();
}

bool resolveGesture(SDL_WindowID windowID, float x, float y, Real deltaX, Real deltaY, Real scale,
	bool pan, SdlResolvedCameraGesture &gesture)
{
	if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(deltaX)
		|| !std::isfinite(deltaY) || !std::isfinite(scale) || scale <= 0 || !TheLookAtTranslator)
		return false;
	SDL_Window *window = gestureWindow();
	if (!window || (windowID && windowID != SDL_GetWindowID(window))
		|| !(SDL_GetWindowFlags(window) & SDL_WINDOW_INPUT_FOCUS) || SDL_GetMouseFocus() != window)
		return false;
	Int width = 0, height = 0;
	SDL_GetWindowSize(window, &width, &height);
	if (width <= 0 || height <= 0 || x < 0 || y < 0 || x >= width || y >= height) return false;
	const Int gameWidth = TheDisplay && TheDisplay->getWidth() > 0 ? (Int)TheDisplay->getWidth() : width;
	const Int gameHeight = TheDisplay && TheDisplay->getHeight() > 0 ? (Int)TheDisplay->getHeight() : height;
	Int gameX, gameY;
	SdlInput_scaleToGame(x, y, width, height, gameWidth, gameHeight, gameX, gameY);
	// Scale displacement without flooring it: a Retina/subpixel drag must retain small changes.
	const Real sx = (Real)gameWidth / width;
	const Real sy = (Real)gameHeight / height;
	gesture = { SDL_GetWindowID(window), gameX, gameY, deltaX * sx, deltaY * sy, scale, pan };
	return std::isfinite(gesture.panX) && std::isfinite(gesture.panY);
}

bool cameraGestureEligibleAt(SDL_WindowID windowID, float x, float y)
{
	SdlResolvedCameraGesture gesture;
	if (!resolveGesture(windowID, x, y, 0, 0, 1, false, gesture)) return false;
	const ICoord2D pixel = { gesture.x, gesture.y };
	return TheLookAtTranslator->applyCameraGesture(pixel, 0, 0, 1);
}

bool queueResolvedGesture(SDL_WindowID windowID, float x, float y, Real deltaX, Real deltaY, Real scale, bool pan)
{
	SdlResolvedCameraGesture gesture;
	if (!resolveGesture(windowID, x, y, deltaX, deltaY, scale, pan, gesture)
		|| !thePendingGestures.push(gesture)) {
		SdlInput_cancelGestures();
		return false;
	}
	return true;
}

void dispatchScroll(const SDL_Event &event)
{
	const Uint32 code = static_cast<Uint32>(event.user.code);
	SdlScrollGestureQueue::Payload pending;
	if (!theScrolls.pop(code, pending)) return;
	if (pending.sequence == theCancelledPanSequence) return;
	const SdlMacScroll &scroll = pending.scroll;
	if ((TheGlobalData && !TheGlobalData->m_macTrackpadPan)
		|| event.user.windowID != scroll.windowID || !scroll.focused || !scroll.inside || scroll.buttonsDown
		|| !queueResolvedGesture(scroll.windowID, scroll.x, scroll.y, scroll.deltaX, scroll.deltaY, 1, true))
		SdlInput_cancelGestures();
}

void startTextInput( Int x, Int y, Int width, Int height )
{
	SDL_Window *window = SdlInput_gameWindow();
	if (window == NULL)
		return;
	/* The field's rectangle is in the game's pixels; SDL wants the window's points.  It is where the
		 platform puts its candidate list, so near enough is enough. */
	Int windowWidth = 0, windowHeight = 0;
	SDL_GetWindowSize( window, &windowWidth, &windowHeight );
	const Real sx = (TheDisplay != NULL && TheDisplay->getWidth() > 0) ? (Real)windowWidth / TheDisplay->getWidth() : 1.0f;
	const Real sy = (TheDisplay != NULL && TheDisplay->getHeight() > 0) ? (Real)windowHeight / TheDisplay->getHeight() : 1.0f;
	SDL_Rect area = { (int)(x * sx), (int)(y * sy), (int)(width * sx), (int)(height * sy) };
	SDL_SetTextInputArea( window, &area, 0 );
	SDL_StartTextInput( window );
}

void stopTextInput( void )
{
	SDL_Window *window = SdlInput_gameWindow();
	if (window != NULL)
		SDL_StopTextInput( window );
}

PosixIMEManager *theIME( void )
{
	// Off Windows CreateIMEManagerInterface makes a PosixIMEManager and nothing else
	return static_cast<PosixIMEManager *>( TheIMEManager );
}

UnsignedInt milliseconds( Uint64 timestampNs )
{
	return (UnsignedInt)(timestampNs / 1000000u);
}

}  // namespace

void SdlInput_cancelGestures(void)
{
	if (theScrollEventType && SDL_WasInit(SDL_INIT_EVENTS)) SDL_FlushEvent(theScrollEventType);
	theScrolls.clear();
	thePendingGestures.clear();
	thePanTarget.cancel();
	thePinchTarget.cancel();
	theCancelledPanSequence = thePanSequence;
}

void SdlInput_applyPendingGestures(void)
{
	const bool accepted = thePendingGestures.drain([](const SdlResolvedCameraGesture &gesture) {
		SDL_Window *window = gestureWindow();
		float pointerX = 0, pointerY = 0;
		const SDL_MouseButtonFlags buttons = SDL_GetMouseState(&pointerX, &pointerY);
		if (!window || gesture.windowID != SDL_GetWindowID(window) || !TheLookAtTranslator
			|| !(SDL_GetWindowFlags(window) & SDL_WINDOW_INPUT_FOCUS) || SDL_GetMouseFocus() != window
			|| buttons != 0
			|| (gesture.pan && TheGlobalData && !TheGlobalData->m_macTrackpadPan)) return false;
#if defined(__APPLE__)
		if (SdlMacGesture_buttonsDown()) return false;
#endif
		// A later mouse/UI message may have moved the pointer over a control after this action
		// was queued. Recheck that context too, while keeping the action's captured pixel/scale.
		if (!cameraGestureEligibleAt(gesture.windowID, pointerX, pointerY)) return false;
		const ICoord2D pixel = { gesture.x, gesture.y };
		return TheLookAtTranslator->applyCameraGesture(pixel, gesture.panX, gesture.panY, gesture.scale) != FALSE;
	});
	if (!accepted) SdlInput_cancelGestures();
}

void SdlInput_setGestureWindow(SDL_Window *window)
{
#if defined(__APPLE__)
	SdlMacGesture_remove();
#endif
	SdlInput_cancelGestures();
	theGestureWindow = window;
	thePanTarget.reset();
	thePinchTarget.reset();
	thePinchWindow = 0;
	if (!window) {
		theScrollEventType = 0;
		return;
	}
#if defined(__APPLE__)
	if (scrollEventType()) SdlMacGesture_install(window, SdlInput_queueTrackpadScroll);
#endif
}

bool SdlInput_queueTrackpadScroll(const SdlMacScroll &scroll) noexcept
{
	const bool begin = scroll.phase == SdlGesturePhase::Begin;
	if (begin) ++thePanSequence;
	const bool enabled = !TheGlobalData || TheGlobalData->m_macTrackpadPan;
	SDL_Window *window = gestureWindow();
	bool eligible = enabled && scroll.focused && scroll.inside && !scroll.buttonsDown
		&& window && scroll.windowID == SDL_GetWindowID(window)
		&& std::isfinite(scroll.x) && std::isfinite(scroll.y)
		&& std::isfinite(scroll.deltaX) && std::isfinite(scroll.deltaY);
	// The only synchronous engine call is a mutation-free BEGIN eligibility probe. Movement
	// waits in SDL's queue, behind earlier keyboard/button events from the same native pump.
	if (begin && eligible) eligible = cameraGestureEligibleAt(scroll.windowID, scroll.x, scroll.y);
	const bool moves = scroll.deltaX != 0 || scroll.deltaY != 0;
	const auto decision = thePanTarget.advance(scroll.phase, scroll.momentum, eligible, moves);
	if (decision == SdlScrollGestureTarget::Decision::PassThrough) return false;
	if (!eligible || scroll.phase == SdlGesturePhase::Cancel || scroll.momentum == SdlGesturePhase::Cancel)
		SdlInput_cancelGestures();
	if (decision != SdlScrollGestureTarget::Decision::Apply) return true;
	Uint32 code = 0;
	if (!scrollEventType() || !theScrolls.push(scroll, thePanSequence, code)) {
		SdlInput_cancelGestures();
		return true;
	}
	SDL_Event event;
	SDL_zero(event);
	event.type = theScrollEventType;
	event.user.windowID = scroll.windowID;
	event.user.code = static_cast<Sint32>(code);
	if (!SDL_PushEvent(&event)) {
		SdlScrollGestureQueue::Payload discarded;
		theScrolls.pop(code, discarded);
		SdlInput_cancelGestures();
	}
	return true;
}

void SdlInput_install( void )
{
	static Bool installed = FALSE;
	if (installed)
		return;
	installed = TRUE;
	TheClientInputAfterMessagesHook = SdlInput_applyPendingGestures;
	SDL_SetHint( SDL_HINT_MAC_CTRL_CLICK_EMULATE_RIGHT_CLICK, "0" );	// force fire is Control + LEFT click
	SDL_SetHint( SDL_HINT_IME_IMPLEMENTED_UI, "composition" );				// W3DTextEntry draws it; the platform draws candidates
	PosixIMEManager::DeviceHooks hooks = { startTextInput, stopTextInput };
	PosixIMEManager::setDeviceHooks( hooks );
}

SDL_Window *SdlInput_gameWindow( void )
{
	int count = 0;
	SDL_Window **windows = SDL_GetWindows( &count );
	SDL_Window *window = (windows != NULL && count > 0) ? windows[0] : NULL;
	SDL_free( windows );
	return window;
}

void SdlInput_toGamePixels( Real windowX, Real windowY, Int &gameX, Int &gameY )
{
	Int windowWidth = 0, windowHeight = 0;
	SDL_Window *window = SdlInput_gameWindow();
	if (window != NULL)
		SDL_GetWindowSize( window, &windowWidth, &windowHeight );
	Int gameWidth = TheDisplay != NULL ? (Int)TheDisplay->getWidth() : 0;
	Int gameHeight = TheDisplay != NULL ? (Int)TheDisplay->getHeight() : 0;
	if (gameWidth <= 0 || gameHeight <= 0)
	{
		gameWidth = windowWidth;		// no display yet: the window is the screen
		gameHeight = windowHeight;
	}
	SdlInput_scaleToGame( windowX, windowY, windowWidth, windowHeight, gameWidth, gameHeight, gameX, gameY );
}

void SdlInput_scaleToGame( Real windowX, Real windowY, Int windowWidth, Int windowHeight,
	Int gameWidth, Int gameHeight, Int &gameX, Int &gameY )
{
	Real x = windowX, y = windowY;
	if (windowWidth > 0 && gameWidth > 0)
		x = windowX * gameWidth / windowWidth;
	if (windowHeight > 0 && gameHeight > 0)
		y = windowY * gameHeight / windowHeight;
	gameX = (Int)floorf( x );
	gameY = (Int)floorf( y );
	// the screen's edge, as far as a captured drag outside the window can go
	if (gameWidth > 0)
		gameX = gameX < 0 ? 0 : (gameX >= gameWidth ? gameWidth - 1 : gameX);
	if (gameHeight > 0)
		gameY = gameY < 0 ? 0 : (gameY >= gameHeight ? gameHeight - 1 : gameY);
}

void SdlInput_resetWheel( void )
{
	theWheelCarry = 0.0f;
}

Bool SdlInput_dispatch( const SDL_Event &event )
{
	SdlInput_install();
	if (theScrollEventType && event.type == theScrollEventType) {
		dispatchScroll(event);
		return TRUE;
	}
	switch (event.type)
	{
		case SDL_EVENT_PINCH_BEGIN:
		case SDL_EVENT_PINCH_UPDATE:
		case SDL_EVENT_PINCH_END:
		{
			SDL_Window *window = gestureWindow();
			if (!window || (event.pinch.windowID && event.pinch.windowID != SDL_GetWindowID(window))) return TRUE;
			const SDL_WindowID windowID = SDL_GetWindowID(window);
			if (event.type == SDL_EVENT_PINCH_BEGIN) thePinchWindow = windowID;
			if (thePinchWindow != windowID) return TRUE;
			if (event.type == SDL_EVENT_PINCH_END) {
				thePinchTarget.reset();
				thePinchWindow = 0;
				return TRUE; // Cocoa BEGIN/END carry scale 0, not a zoom ratio.
			}
			const bool begin = event.type == SDL_EVENT_PINCH_BEGIN;
			const Real scale = begin ? 1 : event.pinch.scale;
			float x = 0, y = 0;
			const SDL_MouseButtonFlags buttons = SDL_GetMouseState(&x, &y);
			const bool eligible = !buttons && cameraGestureEligibleAt(windowID, x, y)
				&& std::isfinite(scale) && scale > 0;
			if (!begin && !eligible) SdlInput_cancelGestures();
			const auto decision = thePinchTarget.advance(begin ? SdlGesturePhase::Begin : SdlGesturePhase::Update,
				SdlGesturePhase::None, eligible, !begin && scale != 1);
			if (decision == SdlScrollGestureTarget::Decision::Apply)
				queueResolvedGesture(windowID, x, y, 0, 0, scale, false);
			return TRUE;
		}

		case SDL_EVENT_WINDOW_RESIZED:
		case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
		case SDL_EVENT_WINDOW_DISPLAY_CHANGED:
		case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED:
		{
			// An attached field survives native fullscreen and Retina/display changes.
			// Recompute its rectangle in the current window's points while keeping the
			// active input session and composition. Ignore events from other windows.
			SDL_Window *window = SdlInput_gameWindow();
			if (window != NULL && event.window.windowID == SDL_GetWindowID( window )) {
				// A queued movement belongs to the geometry in which it started. Do not apply
				// its old point/pixel conversion after a resize, Retina change or display move.
				SdlInput_cancelGestures();
				if (SDL_TextInputActive( window ) && theIME() != NULL)
					theIME()->refreshTextInputArea();
			}
			return TRUE;
		}

		case SDL_EVENT_KEY_DOWN:
		case SDL_EVENT_KEY_UP:
		{
			if (event.key.repeat)
				return TRUE;		// the engine repeats keys itself; DirectInput never did
			const Bool down = event.type == SDL_EVENT_KEY_DOWN;
			if (down && (event.key.scancode == SDL_SCANCODE_RETURN || event.key.scancode == SDL_SCANCODE_KP_ENTER)
					&& theIME() != NULL)
				theIME()->enterPressed();
			const UnsignedByte dik = SdlKeyTable_dikFor( event.key.scancode );
			if (dik != 0 && SdlKeyboard::active() != NULL)
				SdlKeyboard::active()->addKey( dik, down );
			return TRUE;
		}

		case SDL_EVENT_TEXT_INPUT:
			if (theIME() != NULL)
				theIME()->commitText( event.text.text );
			return TRUE;

		case SDL_EVENT_TEXT_EDITING:
			if (theIME() != NULL)
				theIME()->setComposition( event.edit.text, event.edit.start );
			return TRUE;

		case SDL_EVENT_MOUSE_MOTION:
		{
			if (SdlMouse::active() == NULL)
				return TRUE;
			Int x, y;
			SdlInput_toGamePixels( event.motion.x, event.motion.y, x, y );
			SdlMouse::active()->addEvent( SdlMouse::EVENT_MOVE, x, y, SdlMouse::BUTTON_LEFT, 0, 0, milliseconds( event.motion.timestamp ) );
			return TRUE;
		}

		case SDL_EVENT_MOUSE_BUTTON_DOWN:
		case SDL_EVENT_MOUSE_BUTTON_UP:
		{
			SdlMouse::Button button;
			switch (event.button.button)
			{
				case SDL_BUTTON_LEFT:		button = SdlMouse::BUTTON_LEFT; break;
				case SDL_BUTTON_MIDDLE:	button = SdlMouse::BUTTON_MIDDLE; break;
				case SDL_BUTTON_RIGHT:	button = SdlMouse::BUTTON_RIGHT; break;		// a trackpad's secondary click too
				case SDL_BUTTON_X1:			button = SdlMouse::BUTTON_X1; break;		// back: a camera tilt step, as WM_XBUTTONDOWN
				case SDL_BUTTON_X2:			button = SdlMouse::BUTTON_X2; break;
				default:								return TRUE;
			}
			if (SdlMouse::active() == NULL)
				return TRUE;
			Int x, y;
			SdlInput_toGamePixels( event.button.x, event.button.y, x, y );
			SdlMouse::active()->addEvent( event.button.down ? SdlMouse::EVENT_BUTTON_DOWN : SdlMouse::EVENT_BUTTON_UP,
				x, y, button, event.button.clicks, 0, milliseconds( event.button.timestamp ) );
			return TRUE;
		}

		case SDL_EVENT_MOUSE_WHEEL:
		{
			// 120 a notch, WHEEL_DELTA; SDL has already applied the user's natural-scrolling setting
			theWheelCarry += event.wheel.y * 120.0f;
			const Int delta = (Int)theWheelCarry;		// toward zero: the fraction waits for the next event
			theWheelCarry -= (Real)delta;
			if (delta == 0 || SdlMouse::active() == NULL)
				return TRUE;
			Int x, y;
			SdlInput_toGamePixels( event.wheel.mouse_x, event.wheel.mouse_y, x, y );
			SdlMouse::active()->addEvent( SdlMouse::EVENT_WHEEL, x, y, SdlMouse::BUTTON_LEFT, 0, delta, milliseconds( event.wheel.timestamp ) );
			return TRUE;
		}

		case SDL_EVENT_WINDOW_MOUSE_ENTER:
		case SDL_EVENT_WINDOW_MOUSE_LEAVE:
			return TRUE;		// SdlMouse::update asks SDL's mouse focus each frame, as Win32Mouse asks Windows

		default:
			return FALSE;
	}
}
