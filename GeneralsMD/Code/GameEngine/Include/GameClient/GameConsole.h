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

// FILE: GameConsole.h //////////////////////////////////////////////////////////////////////////
// Desc: Drop-down command console on the key above Tab, drawn straight to the display rather
//       than through a .wnd layout, so it works in the shell and in a match alike.
///////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#ifndef __GAMECONSOLE_H_
#define __GAMECONSOLE_H_

#include "Common/MessageStream.h"
#include "Common/AsciiString.h"
#include "Common/UnicodeString.h"

#include <deque>
#include <string>
#include <vector>

class DisplayString;
class GameFont;
class GameWindow;
class WindowLayout;

/**
 * The console's own scrollback, input line and command table.  One global, TheGameConsole,
 * created beside the rest of the client overlays in GameClient::init.
 */
class GameConsole
{
public:

	GameConsole();
	~GameConsole();

	Bool isOpen( void ) const { return m_isOpen; }
	void toggle( void );
	void close( void );

	/// Feed one raw key event in.  The console owns every key while it is open.
	void handleKey( UnsignedByte key, UnsignedShort keyState );

	/// The load screen's pass: no message stream runs while a map loads, so the console reads the
	/// keyboard itself, and every key read there is the console's.
	void readLoadScreenKeys( void );

	/// Append one line to the scrollback.
	void printLine( UnicodeString line );
	void printLine( AsciiString line );

	/// Draw the panel.  Called from the display's 2D overlay pass.
	void render( void );

	/// The cheat panel that "trainer" opens: every cheat a click away, on three tabs.  The draggable
	/// window is Window/Trainer.wnd; the rest is drawn in the side's Tab screen pieces.
	Bool isCheatPanelOpen( void ) const { return m_cheatPanelOpen; }
	void closeCheatPanel( void ) { m_cheatPanelOpen = FALSE; }
	/// What a button of the panel sends: a cheat and its amount as typed.
	void runCheatPanelAction( const std::string &action );
	/// Button `button` of the panel was clicked: a tab, or a cheat's.
	void runCheatWindowButton( Int button );
	/// Throw the panel's windows away now, where diplomacy's are thrown away.
	void resetCheatWindow( void );

private:

	void submitInputLine( void );
	void runCommand( AsciiString commandLine );
	void recallHistory( Int direction );
	void renderCheatPanel( void );
	void updateCheatWindow( void );

	typedef std::deque<UnicodeString> ScrollbackLines;
	typedef std::vector<AsciiString> CommandHistory;

	Bool m_isOpen;
	UnicodeString m_inputLine;
	ScrollbackLines m_scrollback;
	CommandHistory m_history;
	Int m_historyCursor;					///< index into m_history, or m_history.size() when typing fresh

	GameFont *m_font;
	DisplayString **m_lineStrings;	///< one per drawable row, reused every frame
	Int m_lineStringCount;

	Bool m_cheatPanelOpen;
	WindowLayout *m_cheatLayout;	///< the panel while it is up
};

extern GameConsole *TheGameConsole;

/// -freecam x y z heading tilt: the console's "freecam" with that pose, run once the match is up
void GameConsole_setStartupFreeCamera( const char *pose );

/**
 * Sits ahead of the window system on the message stream so the console gets the key above Tab
 * before anything bound in CommandMap.ini does, and swallows every key while it is open.
 */
class GameConsoleTranslator : public GameMessageTranslator
{
public:
	virtual GameMessageDisposition translateGameMessage( const GameMessage *msg );
};

#endif // __GAMECONSOLE_H_
