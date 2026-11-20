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
#include "GameClient/IMEManagerPosix.h"
#include "SdlDevice/GameClient/SdlInput.h"
#include "SdlDevice/GameClient/SdlKeyTable.h"
#include "SdlDevice/GameClient/SdlKeyboard.h"
#include "SdlDevice/GameClient/SdlMouse.h"

#include <SDL3/SDL.h>

#include <math.h>

namespace {

Real theWheelCarry = 0.0f;		///< the fraction of a WM_MOUSEWHEEL unit not yet reported

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

void SdlInput_install( void )
{
	static Bool installed = FALSE;
	if (installed)
		return;
	installed = TRUE;
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
	switch (event.type)
	{
		case SDL_EVENT_WINDOW_RESIZED:
		case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
		case SDL_EVENT_WINDOW_DISPLAY_CHANGED:
		case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED:
		{
			// An attached field survives native fullscreen and Retina/display changes.
			// Recompute its rectangle in the current window's points while keeping the
			// active input session and composition. Ignore events from other windows.
			SDL_Window *window = SdlInput_gameWindow();
			if (window != NULL && event.window.windowID == SDL_GetWindowID( window )
					&& SDL_TextInputActive( window ) && theIME() != NULL)
				theIME()->refreshTextInputArea();
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
