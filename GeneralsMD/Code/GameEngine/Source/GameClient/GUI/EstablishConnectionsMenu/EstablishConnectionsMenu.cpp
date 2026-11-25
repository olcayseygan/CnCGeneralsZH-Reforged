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

////////////////////////////////////////////////////////////////////////////////
//																																						//
//  (c) 2001-2003 Electronic Arts Inc.																				//
//																																						//
////////////////////////////////////////////////////////////////////////////////

//// EstablishConnectionsMenu.cpp /////////////////////////////////////

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "GameClient/GUICallbacks.h"
#include "GameClient/EstablishConnectionsMenu.h"

EstablishConnectionsMenu *TheEstablishConnectionsMenu = NULL;

/**
 Constructor
 */
EstablishConnectionsMenu::EstablishConnectionsMenu() {
	m_menuState = ESTABLISHCONNECTIONSMENUSTATETYPE_SCREENOFF;
	for (Int i = 0; i < MAX_SLOTS; ++i) {
		m_states[i] = NATCONNECTIONSTATE_NOSTATE;
	}
}

/**
 Destructor
 */
EstablishConnectionsMenu::~EstablishConnectionsMenu() {
}

/**
 Initialize the menu
 */
void EstablishConnectionsMenu::initMenu() {
	for (Int i = 0; i < MAX_SLOTS; ++i) {
		m_names[i].clear();
		m_states[i] = NATCONNECTIONSTATE_NOSTATE;
	}
	ShowEstablishConnectionsWindow();
}

/**
 Close down the menu
 */
void EstablishConnectionsMenu::endMenu() {
	HideEstablishConnectionsWindow();
}

/**
 Abort the game gracefully...ok, as gracefully as possible considering
 the game was supposed to be started and now for some reason we have to
 stop it.  Its really sad that this game isn't going to be played
 considering how difficult it is to even get a game going in the first
 place, especially one with more than two players.
 */
void EstablishConnectionsMenu::abortGame() {
}

// the slot number passed in is the index we are to use for the menu.  The box draws a row a named
// seat (EstablishConnectionsWindow.cpp), so every seat the engine has is shown, not EA's seven.
void EstablishConnectionsMenu::setPlayerName(Int slot, UnicodeString name) {
	if (slot < 0 || slot >= MAX_SLOTS)
		return;
	m_names[slot] = name;
}

void EstablishConnectionsMenu::setPlayerStatus(Int slot, NATConnectionState state) {
	if (slot < 0 || slot >= MAX_SLOTS)
		return;
	m_states[slot] = state;
}
