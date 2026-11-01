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

// SdlInput.h: SDL3's input events to the engine's keyboard, mouse and text input (C3, decision 3).
//
// What WndProc does on Windows for WM_KEY*, WM_CHAR, WM_IME_* and the mouse messages.
// SdlGameEngine::serviceWindowsOS polls SDL's events, keeps quit and focus for itself, and hands every
// other event here.  Each goes where its Windows counterpart went:
//   - key presses and releases to SdlKeyboard as DIK codes (SdlKeyTable), SDL's repeats dropped;
//   - text to the IME manager (PosixIMEManager): committed text, the composition, and Return/keypad
//     Enter as the '\r' WM_CHAR carries;
//   - mouse motion, buttons and the wheel to SdlMouse, in the game's pixels.
// Window geometry and display changes refresh the active text field's candidate rectangle.
// A device that does not exist yet (the keyboard and mouse are made in GameClient::init, after the
// first events can arrive) is skipped, as WndProc skips a NULL TheWin32Mouse.

#pragma once

#ifndef __SDLINPUT_H
#define __SDLINPUT_H

#include "Lib/BaseType.h"

union SDL_Event;
struct SDL_Window;

/// One event; TRUE when input or text-input geometry was handled
Bool SdlInput_dispatch( const SDL_Event &event );

/** The hints and hooks the rest needs, once: Control+click stays a left click (force fire), the
	* engine draws the composition itself, and the IME manager's text input goes through SDL. */
void SdlInput_install( void );

/// The game's window: the one SDL window there is; NULL without one
SDL_Window *SdlInput_gameWindow( void );

/// A position in the window's points to the game's pixels, held inside the game's screen
void SdlInput_toGamePixels( Real windowX, Real windowY, Int &gameX, Int &gameY );

/** The arithmetic of that: scaled from a windowWidth x windowHeight window to a gameWidth x gameHeight
	* screen and held inside it.  A size of 0 leaves that axis unscaled and unheld. */
void SdlInput_scaleToGame( Real windowX, Real windowY, Int windowWidth, Int windowHeight,
	Int gameWidth, Int gameHeight, Int &gameX, Int &gameY );

/// Drops the wheel's carried fraction (for the test; the game never needs it)
void SdlInput_resetWheel( void );

#endif // __SDLINPUT_H
