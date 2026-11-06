/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2025 Electronic Arts Inc.
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
// Modified 2026 by İlyas Akın for the macOS/Linux port: parts come from GeneralsMD/Code/GameEngine/Source/GameClient/GUI/IMEManager.cpp, the rest is new code, copyright 2026 İlyas Akın; see NOTICE.md and the git history.

// FILE: IMEManagerPosix.cpp //////////////////////////////////////////////////////////////////////
// Desc:   GameClient/IMEManager.h off Windows: see GameClient/IMEManagerPosix.h.
///////////////////////////////////////////////////////////////////////////////////////////////////

/* IMEManager.cpp is the Windows body, on IMM32, and is not built here; this one is built instead. */

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "GameClient/GameWindow.h"
#include "GameClient/GameWindowManager.h"
#include "GameClient/IMEManagerPosix.h"
#include "Lib/WideCharFns.h"

#include <string.h>
#include <vector>

IMEManagerInterface *TheIMEManager = NULL;

IMEManagerInterface *CreateIMEManagerInterface( void )
{
	return NEW PosixIMEManager;
}

namespace {

PosixIMEManager::DeviceHooks theHooks = { NULL, NULL };

// UTF-8 to WideChar units, the whole of it
std::vector<WideChar> wideFromUtf8( const char *utf8 )
{
	const size_t bytes = utf8 != NULL ? strlen( utf8 ) : 0;
	std::vector<WideChar> units( bytes + 1 );		// never more units than bytes, plus the terminator
	const size_t count = WideCharFromUtf8( utf8 != NULL ? utf8 : "", &units[0], units.size() );
	units.resize( count );
	return units;
}

}  // namespace

void PosixIMEManager::setDeviceHooks( const DeviceHooks &hooks )
{
	theHooks = hooks;
}

PosixIMEManager::PosixIMEManager( void )
	: m_window( NULL ), m_disabled( 0 ), m_textInputOn( FALSE ), m_composing( FALSE ), m_compositionCursor( 0 )
{
}

PosixIMEManager::~PosixIMEManager( void )
{
	m_window = NULL;
	startOrStopTextInput();
}

void PosixIMEManager::reset( void )
{
	detatch();
}

// IMEManager::attach's shape: a different window detaches the old one first
void PosixIMEManager::attach( GameWindow *window )
{
	if (m_window != window)
	{
		detatch();
		m_window = window;
		startOrStopTextInput();
	}
}

void PosixIMEManager::detatch( void )
{
	endComposition();
	m_window = NULL;
	startOrStopTextInput();
}

void PosixIMEManager::enable( void )
{
	if (--m_disabled <= 0)
		m_disabled = 0;
	startOrStopTextInput();
}

void PosixIMEManager::disable( void )
{
	m_disabled++;
	endComposition();
	startOrStopTextInput();
}

/* The platform's text input is on exactly while a window is attached and the manager is enabled, the
	 two conditions on which IMEManager associates its IMM context with the game's window. */
void PosixIMEManager::startOrStopTextInput( void )
{
	if (m_window != NULL && m_disabled == 0)
	{
		// on every attach, even when already on: the new field's rectangle is where the candidates go
		if (theHooks.startTextInput != NULL)
		{
			Int x = 0, y = 0, width = 0, height = 0;
			windowArea( m_window, x, y, width, height );
			theHooks.startTextInput( x, y, width, height );
		}
		m_textInputOn = TRUE;
	}
	else if (m_textInputOn)
	{
		if (theHooks.stopTextInput != NULL)
			theHooks.stopTextInput();
		m_textInputOn = FALSE;
	}
}

void PosixIMEManager::refreshTextInputArea( void )
{
	if (m_textInputOn && m_window != NULL && m_disabled == 0)
		startOrStopTextInput();
}

void PosixIMEManager::endComposition( void )
{
	m_composing = FALSE;
	m_composition.clear();
	m_compositionCursor = 0;
}

void PosixIMEManager::commitText( const char *utf8 )
{
	endComposition();
	if (m_window == NULL || m_disabled != 0)
		return;
	const std::vector<WideChar> units = wideFromUtf8( utf8 );
	for (size_t i = 0; i < units.size(); ++i)
	{
		// re-read each time: a character the window takes can end the edit and detach us
		if (m_window != NULL && units[i] >= 32)
			sendChar( m_window, units[i] );
	}
}

void PosixIMEManager::setComposition( const char *utf8, Int cursorCodePoints )
{
	if (m_window == NULL || m_disabled != 0 || utf8 == NULL || utf8[0] == 0)
	{
		endComposition();
		return;
	}
	const std::vector<WideChar> units = wideFromUtf8( utf8 );
	m_composition.clear();
	Int cursor = (Int)units.size(), codePoints = 0;
	for (size_t i = 0; i < units.size(); ++i)
	{
		m_composition.concat( units[i] );
		if (units[i] >= 0xDC00 && units[i] <= 0xDFFF)
			continue;		// the second half of a surrogate pair starts no code point
		if (codePoints == cursorCodePoints)
			cursor = (Int)i;
		++codePoints;
	}
	m_compositionCursor = cursor;
	m_composing = TRUE;
}

void PosixIMEManager::enterPressed( void )
{
	if (m_window != NULL && m_disabled == 0 && !m_composing)
		sendChar( m_window, (WideChar)u'\r' );
}

void PosixIMEManager::sendChar( GameWindow *window, WideChar ch )
{
	if (TheWindowManager != NULL)
		TheWindowManager->winSendInputMsg( window, GWM_IME_CHAR, (WindowMsgData)ch, 0 );
}

void PosixIMEManager::windowArea( GameWindow *window, Int &x, Int &y, Int &width, Int &height )
{
	x = y = width = height = 0;
	if (window != NULL)
	{
		window->winGetScreenPosition( &x, &y );
		window->winGetSize( &width, &height );
	}
}
