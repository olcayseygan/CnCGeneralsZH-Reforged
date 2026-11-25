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


#pragma once

#ifndef __DISCONNECTDIALOG_H
#define __DISCONNECTDIALOG_H

#include "GameNetwork/DisconnectManager.h"

class GameWindow;

enum DisconnectMenuStateType {
	DISCONNECTMENUSTATETYPE_SCREENON,
	DISCONNECTMENUSTATETYPE_SCREENOFF
};

class DisconnectMenu {
public:
	DisconnectMenu();
	virtual ~DisconnectMenu();

	void init();

	void attachDisconnectManager(DisconnectManager *disconnectManager);

	void showScreen();
	void hideScreen();
	Bool isScreenVisible( void ) { return m_menuState == DISCONNECTMENUSTATETYPE_SCREENON; }

	void showPlayerControls(Int slot);
	void hidePlayerControls(Int slot);
	void showPacketRouterTimeout();
	void hidePacketRouterTimeout();

	void setPlayerName(Int playerNum, UnicodeString name);
	void setPlayerTimeoutTime(Int playerNum, time_t newTime);
	void setPlayerReconnectTime(Int playerNum, UnicodeString name, time_t msLeft);	///< "Waiting for <name> to reconnect (m:ss)" in place of the name and countdown
	void setPacketRouterTimeoutTime(time_t newTime);

	void sendChat(UnicodeString text);
	void showChat(UnicodeString text);

	void quitGame();
	void removePlayer(Int slot, UnicodeString playerName, Bool mayComeBack);	///< "lost connection" when the seat can rejoin, "has left the game" when not
	void voteForPlayer(Int slot);
	/// votes cast against a seat, and the count that drops it (isPlayerVotedOut's: every other connected
	/// player); 0 leaves the count the menu last had
	void updateVotes(Int slot, Int votes, Int votesToDrop = 0);

	/// the box in the menu language (docs/plans/MENU-REDESIGN.md, package 5), drawn on the layout's
	/// DisconnectWindow; the vote keys are drawn here too, and a click on one lands in voteAt
	void drawBox(GameWindow *box);
	Bool voteAt(Int x, Int y);

	enum SeatState { SEAT_NONE, SEAT_TALKING, SEAT_SILENT, SEAT_REJOINING };

protected:
	struct Seat {
		UnicodeString name;
		SeatState state;
		Int msLeft;				///< before the seat leaves the lockstep
		Int msFull;				///< what the drain bar empties from
		Int votes;
		Bool voted;				///< the local player voted against it
		IRegion2D voteKey;		///< where its vote key was drawn last, on the screen
	};

	void setSeat(Int slot, SeatState state);
	void addChat(UnicodeString text, Bool system);

	DisconnectManager *m_disconnectManager;		///< For retrieving status updates from the disconnect manager.
	DisconnectMenuStateType m_menuState;			///< The current state of the menu screen.
	Seat m_seats[MAX_SLOTS];
	Int m_votesToDrop;
	Bool m_hostRow;							///< the packet router has stopped answering
	Int m_hostSeconds;
	enum { CHAT_KEPT = 8 };
	UnicodeString m_chat[CHAT_KEPT];
	Bool m_chatSystem[CHAT_KEPT];
	Int m_chatCount;
};

/// The disconnect box's height and what it holds (boxes README, boxes-3): a full row a waiting seat
/// up to eight, two half rows to a line past eight, the connected names on lines of their own, two to
/// four chat lines, never past the tallest box a match holds (732 at 1080).  Pure, for tests.
struct DisconnectBoxSize {
	Bool halves;			///< two seats to a line
	Int rows;					///< lines of seats drawn
	Int shown;				///< waiting seats drawn; the rest do not fit
	Int chatLines;
	Int height;				///< at 1080
};
DisconnectBoxSize DisconnectBoxSizeFor(Int waiting, Int connectedLines, Bool hostRow);
enum { DISCONNECT_ROW = 50, DISCONNECT_HEADS = 26, DISCONNECT_CONNECTED_LINE = 34, DISCONNECT_CHAT_LINE = 26,
	DISCONNECT_BOX_MAX = 732 };

extern DisconnectMenu *TheDisconnectMenu;

#endif // #ifndef __DISCONNECTDIALOG_H
