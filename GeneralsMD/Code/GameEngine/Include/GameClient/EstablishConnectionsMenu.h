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

//// EstablishConnectionsMenu.h /////////////////////////

#include "GameNetwork/NetworkDefs.h"
#include "GameNetwork/NAT.h"

enum EstablishConnectionsMenuStateType {
	ESTABLISHCONNECTIONSMENUSTATETYPE_SCREENON,
	ESTABLISHCONNECTIONSMENUSTATETYPE_SCREENOFF
};

class EstablishConnectionsMenu {
public:
	EstablishConnectionsMenu();
	virtual ~EstablishConnectionsMenu();

	void initMenu();
	void endMenu();
	void abortGame();

	void setPlayerName(Int slot, UnicodeString name);
	void setPlayerStatus(Int slot, NATConnectionState state);

	/// what the box draws (EstablishConnectionsWindow.cpp): a seat's name, empty when nobody sits
	/// there, and where its connection stands.  EA's layout held seven; the engine seats 32
	const UnicodeString &getPlayerName(Int slot) const { return m_names[slot]; }
	NATConnectionState getPlayerStatus(Int slot) const { return m_states[slot]; }

protected:
	EstablishConnectionsMenuStateType m_menuState;
	UnicodeString m_names[MAX_SLOTS];
	NATConnectionState m_states[MAX_SLOTS];
};

extern EstablishConnectionsMenu *TheEstablishConnectionsMenu;