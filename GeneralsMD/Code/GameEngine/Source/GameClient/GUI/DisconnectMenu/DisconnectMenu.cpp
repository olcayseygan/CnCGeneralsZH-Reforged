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
// Modified 2026 by İlyas Akın for the macOS/Linux port; see NOTICE.md and the git history.

////////////////////////////////////////////////////////////////////////////////
//																																						//
//  (c) 2001-2003 Electronic Arts Inc.																				//
//																																						//
////////////////////////////////////////////////////////////////////////////////

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "GameClient/DisconnectMenu.h"
#include "GameClient/GUICallbacks.h"
#include "Common/GlobalData.h"
#include "Common/MultiplayerSettings.h"
#include "Common/NameKeyGenerator.h"
#include "GameClient/GameWindow.h"
#include "GameClient/GameWindowManager.h"
#include "GameClient/GadgetListBox.h"
#include "GameClient/GameText.h"
#include "GameClient/MenuBoxParts.h"
#include "GameClient/Mouse.h"
#include "GameNetwork/GameInfo.h"
#include "GameNetwork/NetworkInterface.h"

//-------------------------------------------------------------------------------------------------
// The disconnect box in the menu language (docs/plans/MENU-REDESIGN.md, package 5; the boxes
// README's boxes-3 and -3b).  EA's screen had seven fixed rows, one a seat whatever it was doing;
// the engine seats 32.  The menu keeps what the disconnect manager tells it per seat and draws one
// list: a row for each seat that has gone quiet or is rejoining (its state, the seconds before it
// leaves the lockstep as a draining figure, the vote key and the votes against the count that drops
// it), two seats to a line past eight, everyone still talking on lines of names under them, the
// chat, and the chat line with QUIT GAME in the foot.  No key is lit: Enter must never leave a match.
//-------------------------------------------------------------------------------------------------

static const Color chatNormalColor =  GameMakeColor(255,0,0,255);
static const char *TEXT_DISPLAY = "DisconnectScreen.wnd:ListboxTextDisplay";

DisconnectMenu *TheDisconnectMenu = NULL;

DisconnectBoxSize DisconnectBoxSizeFor(Int waiting, Int connectedLines, Bool hostRow) {
	DisconnectBoxSize size;
	size.halves = waiting > 8;
	Int rows = size.halves ? (waiting + 1) / 2 : waiting;
	const Int fixed = UIStyle::BOX_HEAD + 20 + DISCONNECT_HEADS + (hostRow ? DISCONNECT_ROW : 0)
		+ connectedLines * DISCONNECT_CONNECTED_LINE + 16 + 20 + 12 + UIStyle::BOX_FOOT;
	const Int most = max(0, (DISCONNECT_BOX_MAX - fixed - 2 * DISCONNECT_CHAT_LINE) / DISCONNECT_ROW);
	rows = min(rows, most);
	size.rows = rows;
	size.shown = size.halves ? min(waiting, 2 * rows) : rows;
	const Int rest = fixed + rows * DISCONNECT_ROW;
	size.chatLines = max(2, min(4, (DISCONNECT_BOX_MAX - rest) / DISCONNECT_CHAT_LINE));
	size.height = rest + size.chatLines * DISCONNECT_CHAT_LINE;
	return size;
}

DisconnectMenu::DisconnectMenu() {
	m_disconnectManager = NULL;
	m_menuState = DISCONNECTMENUSTATETYPE_SCREENOFF;
	for (Int i = 0; i < MAX_SLOTS; ++i) {
		m_seats[i].state = SEAT_NONE;
		m_seats[i].msLeft = m_seats[i].msFull = m_seats[i].votes = 0;
		m_seats[i].voted = FALSE;
		m_seats[i].voteKey = MenuBox::region(0, 0, 0, 0);
	}
	m_votesToDrop = 0;
	m_hostRow = FALSE;
	m_hostSeconds = 0;
	m_chatCount = 0;
}

DisconnectMenu::~DisconnectMenu() {
	/* The window outlives the menu: its layout is made once and kept.  A screen still up when the match's
		 network went away stayed over the shell with its rows hidden, an empty frame whose Quit led nowhere.
		 The engine's own shutdown deletes the network after the window manager has gone. */
	if (TheWindowManager != NULL) {
		HideDisconnectWindow();
	}
}

void DisconnectMenu::init() {
	m_disconnectManager = NULL;
	HideDisconnectWindow();
	m_menuState = DISCONNECTMENUSTATETYPE_SCREENOFF;
}

void DisconnectMenu::attachDisconnectManager(DisconnectManager *disconnectManager) {
	m_disconnectManager = disconnectManager;
}

void DisconnectMenu::showScreen() {
	HideDiplomacy();
	HideInGameChat();
	HideQuitMenu();
	for (Int i = 0; i < MAX_SLOTS; ++i) {
		m_seats[i].voted = FALSE;
	}
	m_chatCount = 0;
	ShowDisconnectWindow();
	m_menuState = DISCONNECTMENUSTATETYPE_SCREENON;
}

void DisconnectMenu::hideScreen() {
	HideDisconnectWindow();
	m_menuState = DISCONNECTMENUSTATETYPE_SCREENOFF;
}

void DisconnectMenu::setSeat(Int slot, SeatState state) {
	if (slot < 0 || slot >= MAX_SLOTS)
		return;
	m_seats[slot].state = state;
}

void DisconnectMenu::setPlayerName(Int playerNum, UnicodeString name) {
	if (playerNum < 0 || playerNum >= MAX_SLOTS)
		return;
	m_seats[playerNum].name = name;
	setSeat(playerNum, name.getLength() > 0 ? SEAT_TALKING : SEAT_NONE);
}

void DisconnectMenu::setPlayerTimeoutTime(Int playerNum, time_t newTime) {
	if (playerNum < 0 || playerNum >= MAX_SLOTS || m_seats[playerNum].state == SEAT_NONE)
		return;
	m_seats[playerNum].msLeft = (Int)newTime;
	setSeat(playerNum, SEAT_TALKING);
}

void DisconnectMenu::setPlayerReconnectTime(Int playerNum, UnicodeString name, time_t msLeft) {
	if (playerNum < 0 || playerNum >= MAX_SLOTS)
		return;
	Seat &seat = m_seats[playerNum];
	seat.name = name;
	/* The manager waits on a seat two ways, and says which only through the time it gives: one gone
		 quiet counts down from NetworkSilentPlayerDropTime, one readmitted and replaying the match up to
		 the room is held longer than that (DisconnectManager::timeBeforeDrop). */
	const Int drop = (Int)TheGlobalData->m_networkSilentPlayerDropTime;
	const SeatState state = ((Int)msLeft > drop) ? SEAT_REJOINING : SEAT_SILENT;
	if (state != seat.state)
		seat.msFull = (state == SEAT_SILENT) ? drop : (Int)msLeft;
	seat.msFull = max(seat.msFull, (Int)msLeft);
	seat.msLeft = (Int)msLeft;
	seat.state = state;
}

void DisconnectMenu::showPlayerControls(Int slot) {
	if (slot >= 0 && slot < MAX_SLOTS && m_seats[slot].state == SEAT_NONE && m_seats[slot].name.getLength() > 0)
		setSeat(slot, SEAT_TALKING);
}

void DisconnectMenu::hidePlayerControls(Int slot) {
	setSeat(slot, SEAT_NONE);
}

void DisconnectMenu::showPacketRouterTimeout() {
	m_hostRow = TRUE;
	m_hostSeconds = 0;
}

void DisconnectMenu::hidePacketRouterTimeout() {
	m_hostRow = FALSE;
}

void DisconnectMenu::setPacketRouterTimeoutTime(time_t newTime) {
	m_hostSeconds = (Int)((newTime + 999) / 1000);
}

void DisconnectMenu::addChat(UnicodeString text, Bool system) {
	if (m_chatCount == CHAT_KEPT) {
		for (Int i = 1; i < CHAT_KEPT; ++i) {
			m_chat[i - 1] = m_chat[i];
			m_chatSystem[i - 1] = m_chatSystem[i];
		}
		--m_chatCount;
	}
	m_chat[m_chatCount] = text;
	m_chatSystem[m_chatCount] = system;
	++m_chatCount;

	// the layout's list box keeps the whole conversation, as EA's did, for anything that reads it
	GameWindow *display = TheWindowManager->winGetWindowFromId(NULL, TheNameKeyGenerator->nameToKey(TEXT_DISPLAY));
	if (display != NULL) {
		GadgetListBoxAddEntryText(display, text, chatNormalColor, -1, -1);
	}
}

void DisconnectMenu::sendChat(UnicodeString text) {
	TheNetwork->sendDisconnectChat(text);
}

void DisconnectMenu::showChat(UnicodeString text) {
	addChat(text, FALSE);
}

void DisconnectMenu::quitGame() {
	TheNetwork->quitGame();
}

void DisconnectMenu::removePlayer(Int slot, UnicodeString playerName, Bool mayComeBack) {
	hidePlayerControls(slot);
	UnicodeString text;
	text.format(TheGameText->fetch(mayComeBack ? "Network:PlayerLostConnection" : "Network:PlayerLeftGame"), playerName.str());
	addChat(text, TRUE);
}

void DisconnectMenu::voteForPlayer(Int slot) {
	DEBUG_LOG(("Casting vote for disconnect slot %d\n", slot));
	if (slot >= 0 && slot < MAX_SLOTS)
		m_seats[slot].voted = TRUE;
	TheNetwork->voteForPlayerDisconnect(slot); // Do this next.
}

void DisconnectMenu::updateVotes(Int slot, Int votes, Int votesToDrop) {
	if (slot < 0 || slot >= MAX_SLOTS)
		return;
	m_seats[slot].votes = votes;
	if (votesToDrop > 0)
		m_votesToDrop = votesToDrop;
}

Bool DisconnectMenu::voteAt(Int x, Int y) {
	for (Int i = 0; i < MAX_SLOTS; ++i) {
		const Seat &seat = m_seats[i];
		if ((seat.state == SEAT_SILENT || seat.state == SEAT_REJOINING) && !seat.voted
				&& x >= seat.voteKey.lo.x && x < seat.voteKey.hi.x && y >= seat.voteKey.lo.y && y < seat.voteKey.hi.y) {
			voteForPlayer(i);
			return TRUE;
		}
	}
	return FALSE;
}

//-------------------------------------------------------------------------------------------------
// drawing
//-------------------------------------------------------------------------------------------------

/// a seat's colour: the disconnect slots skip the local player's own
static Color seatColour(Int slot) {
	if (TheGameInfo == NULL || TheMultiplayerSettings == NULL)
		return UIStyle::MUTED;
	const Int local = TheGameInfo->getLocalSlotNum();
	const Int gameSlot = (local >= 0 && slot >= local) ? slot + 1 : slot;
	const GameSlot *game = TheGameInfo->getConstSlot(gameSlot);
	const MultiplayerColorDefinition *def = game ? TheMultiplayerSettings->getColor(game->getApparentColor()) : NULL;
	return def ? def->getColor() : UIStyle::MUTED;
}

static UnicodeString figureText(const Int ms, Bool rejoining) {
	const Int seconds = max(0, (ms + 999) / 1000);
	UnicodeString text;
	if (rejoining)
		text.format(u"%d:%02d", seconds / 60, seconds % 60);
	else
		text.format(u"%d s", seconds);
	return text;
}

/// the drop's drain: a faint track, white as far as the time left goes
static void drain(Int x0, Int x1, Int cy, Real share) {
	const Int h = max(2, UIStyle::px(4.0f));
	share = max(0.0f, min(1.0f, share));
	TheDisplay->drawFillRect(x0, cy - h / 2, x1 - x0, h, UIStyle::withAlpha(UIStyle::FRAME, 90));
	TheDisplay->drawFillRect(x0, cy - h / 2, REAL_TO_INT((x1 - x0) * share), h, GameMakeColor(255, 255, 255, 200));
}

/// VOTE TO DROP: an edged key; once pressed, the EDGE wash and VOTED.  Never cyan: Quit is never lit
static void voteKey(const IRegion2D &r, DisplayString *word, Bool voted, Bool hovered) {
	MenuBox::fill(r, GameMakeColor(0, 0, 0, 160));
	if (voted)
		MenuBox::fill(r, UIStyle::withAlpha(UIStyle::EDGE, 70));
	else if (hovered)
		MenuBox::fill(r, GameMakeColor(255, 255, 255, 16));
	MenuBox::outline(r, (hovered && !voted) ? UIStyle::EDGE_LIT : UIStyle::EDGE);
	MenuBox::words(word, UIStyle::menuFont(UIStyle::MENU_TEXT_LABEL, TRUE), (r.lo.x + r.hi.x) / 2, (r.lo.y + r.hi.y) / 2,
		UIStyle::INK, MenuBox::CENTRE);
}

/// the votes as squares, one a vote, lit when cast; past eight the figure alone
static Int pips(Int x, Int cy, Int votes, Int of) {
	if (of <= 0 || of > 8)
		return 0;
	const Int step = UIStyle::px(14.0f), side = UIStyle::px(10.0f);
	for (Int i = 0; i < of; ++i) {
		const IRegion2D r = MenuBox::region(x + i * step, cy - side / 2, x + i * step + side, cy - side / 2 + side);
		if (i < votes)
			MenuBox::fill(r, UIStyle::EDGE_LIT);
		else
			MenuBox::outline(r, UIStyle::FRAME);
	}
	return of * step + UIStyle::px(10.0f) - (step - side);
}

void DisconnectMenu::drawBox(GameWindow *box) {
	static MenuBox::TextPool pool;
	static Int placedHeight = -1, placedWidth = -1, placedScreen = -1;
	static GameWindow *placedFor = NULL;
	pool.begin();

	GameFont *body = UIStyle::menuFont(UIStyle::MENU_TEXT_BODY);
	GameFont *label = UIStyle::menuFont(UIStyle::MENU_TEXT_LABEL, TRUE);
	GameFont *figure = UIStyle::menuFont(UIStyle::MENU_TEXT_ROW_FIGURE);
	GameFont *name20 = UIStyle::menuFont(UIStyle::MENU_TEXT_BODY + 2);

	Int waiting[MAX_SLOTS], talking[MAX_SLOTS];
	Int nWaiting = 0, nTalking = 0;
	for (Int i = 0; i < MAX_SLOTS; ++i) {
		if (m_seats[i].state == SEAT_SILENT || m_seats[i].state == SEAT_REJOINING)
			waiting[nWaiting++] = i;
		else if (m_seats[i].state == SEAT_TALKING)
			talking[nTalking++] = i;
		m_seats[i].voteKey = MenuBox::region(0, 0, 0, 0);
	}

	// the connected names on lines, measured on this screen: CONNECTED, then a stripe and a name each
	const Int left1080 = (UIStyle::BOARD_WIDTH - UIStyle::BOX_LIST) / 2 + UIStyle::BOX_PAD;
	const Int right1080 = (UIStyle::BOARD_WIDTH + UIStyle::BOX_LIST) / 2 - UIStyle::BOX_PAD;
	DisplayString *connected = pool.get(UIStyle::capitals(TheGameText->fetch("GUI:ConnectedCaps")));
	connected->setFont(label);
	const Int namesStart = UIStyle::boardX((Real)left1080 + 20) + connected->getWidth() + UIStyle::px(24.0f);
	const Int namesEnd = UIStyle::boardX((Real)right1080);
	Int nameLine[MAX_SLOTS], nameX[MAX_SLOTS];
	DisplayString *names[MAX_SLOTS];
	Int lines = 0, x = namesStart;
	for (Int i = 0; i < nTalking; ++i) {
		names[i] = pool.get(m_seats[talking[i]].name);
		names[i]->setFont(body);
		const Int w = UIStyle::px(12.0f) + names[i]->getWidth() + UIStyle::px(28.0f);
		if (x + w > namesEnd && x > namesStart) {
			++lines;
			x = namesStart;
		}
		nameLine[i] = lines;
		nameX[i] = x;
		x += w;
	}
	const Int connectedLines = nTalking ? lines + 1 : 0;

	const DisconnectBoxSize size = DisconnectBoxSizeFor(nWaiting, connectedLines, m_hostRow);
	const IRegion2D at1080 = UIStyle::boxRect1080(UIStyle::BOX_LIST, size.height, TRUE);
	if (box != placedFor || size.height != placedHeight || TheDisplay->getWidth() != placedWidth || TheDisplay->getHeight() != placedScreen) {
		placedFor = box;
		placedHeight = size.height;
		placedWidth = TheDisplay->getWidth();
		placedScreen = TheDisplay->getHeight();
		MenuBox::place(box, at1080);
		const IRegion2D quit = UIStyle::boxKeyRect1080(at1080, 0);
		MenuBox::place(TheWindowManager->winGetWindowFromId(box, NAMEKEY("DisconnectScreen.wnd:ButtonQuitGame")), quit);
		MenuBox::place(TheWindowManager->winGetWindowFromId(box, NAMEKEY("DisconnectScreen.wnd:TextEntry")),
			MenuBox::region(at1080.lo.x + UIStyle::BOX_PAD, at1080.hi.y - 68, quit.lo.x - 16, at1080.hi.y - 20));
	}

	const IRegion2D r = MenuBox::onBoard(at1080);
	DisplayString *title = pool.get(UIStyle::capitals(TheGameText->fetch("GUI:WaitingForPlayers")));
	UIStyle::boxShell(r, title, TRUE);
	UnicodeString count;
	count.format(TheGameText->fetch("GUI:NotRespondingCount"), nWaiting);
	MenuBox::words(pool.get(count), body, r.lo.x + UIStyle::px(32.0f) + title->getWidth() + UIStyle::px(20.0f),
		r.lo.y + UIStyle::px(44.0f), UIStyle::MUTED);

	const Int x0 = left1080, x1 = right1080;
	Int y = at1080.lo.y + UIStyle::BOX_HEAD + 20;
	const Int headMiddle = y + 9;
	const Int hw = (x1 - x0 - 24) / 2;
	const Int starts[2] = { x0, x0 + hw + 24 };
	if (size.halves) {
		for (Int c = 0; c < 2; ++c) {
			MenuBox::words(pool.get(UIStyle::capitals(TheGameText->fetch("GUI:DisconnectPlayer"))), label, UIStyle::boardX((Real)starts[c] + 20), UIStyle::boardY((Real)headMiddle), UIStyle::MUTED);
			MenuBox::words(pool.get(UIStyle::capitals(UIStyle::fitKey("GUI:DropsIn", label, UIStyle::px(110.0f)))), label, UIStyle::boardX((Real)starts[c] + 180), UIStyle::boardY((Real)headMiddle), UIStyle::MUTED);
			MenuBox::words(pool.get(UIStyle::capitals(TheGameText->fetch("GUI:DisconnectVotes"))), label, UIStyle::boardX((Real)starts[c] + hw - 66), UIStyle::boardY((Real)headMiddle), UIStyle::MUTED);
		}
	} else {
		MenuBox::words(pool.get(UIStyle::capitals(TheGameText->fetch("GUI:DisconnectPlayer"))), label, UIStyle::boardX((Real)x0 + 20), UIStyle::boardY((Real)headMiddle), UIStyle::MUTED);
		MenuBox::words(pool.get(UIStyle::capitals(TheGameText->fetch("GUI:DisconnectState"))), label, UIStyle::boardX((Real)x0 + 286), UIStyle::boardY((Real)headMiddle), UIStyle::MUTED);
		MenuBox::words(pool.get(UIStyle::capitals(UIStyle::fitKey("GUI:DropsIn", label, UIStyle::px(168.0f)))), label, UIStyle::boardX((Real)x0 + 476), UIStyle::boardY((Real)headMiddle), UIStyle::MUTED);
		MenuBox::words(pool.get(UIStyle::capitals(TheGameText->fetch("GUI:DisconnectVotes"))), label, UIStyle::boardX((Real)x0 + 828), UIStyle::boardY((Real)headMiddle), UIStyle::MUTED);
	}
	y += DISCONNECT_HEADS;
	UIStyle::litLine(UIStyle::boardX((Real)x0), UIStyle::boardY((Real)y - 3), UIStyle::px((Real)(x1 - x0)), 1, UIStyle::FRAME);

	const Int row = DISCONNECT_ROW;
	const Int mouseX = TheMouse ? TheMouse->getMouseStatus()->pos.x : -1;
	const Int mouseY = TheMouse ? TheMouse->getMouseStatus()->pos.y : -1;
	if (m_hostRow) {
		const Int cy = UIStyle::boardY((Real)y + row / 2);
		MenuBox::words(pool.get(TheGameText->fetch("GUI:HostNotResponding")), body, UIStyle::boardX((Real)x0 + 20), cy, UIStyle::INK);
		UnicodeString seconds;
		seconds.format(u"%d s", m_hostSeconds);
		const Int fw = MenuBox::words(pool.get(seconds), figure, UIStyle::boardX((Real)x0 + 476), cy, UIStyle::INK);
		drain(UIStyle::boardX((Real)x0 + 476) + fw + UIStyle::px(14.0f), UIStyle::boardX((Real)x0 + 644), cy,
			m_hostSeconds / (TheGlobalData->m_networkPlayerTimeoutTime / 1000.0f));
		y += row;
	}

	for (Int n = 0; n < size.shown; ++n) {
		Seat &seat = m_seats[waiting[n]];
		const Bool rejoining = seat.state == SEAT_REJOINING;
		const Real share = seat.msFull > 0 ? seat.msLeft / (Real)seat.msFull : 0.0f;
		const Color colour = seatColour(waiting[n]);
		UnicodeString votes;
		votes.format(u"%d / %d", seat.votes, max(m_votesToDrop, 1));
		if (size.halves) {
			const Int a = starts[n % 2];
			const Int yy = y + (n / 2) * row;
			const Int cy = UIStyle::boardY((Real)yy + row / 2);
			if ((n / 2) % 2)
				MenuBox::fill(UIStyle::boardRect(a, yy, a + hw, yy + row), GameMakeColor(255, 255, 255, 7));
			const IRegion2D stripe = UIStyle::boardRect(a + 2, yy + 8, a + 8, yy + row - 8);
			MenuBox::stripe(stripe.lo.x, stripe.lo.y, stripe.hi.y, stripe.hi.x - stripe.lo.x, colour);
			MenuBox::words(pool.get(UIStyle::fitName(seat.name, body, UIStyle::px(150.0f))), body, UIStyle::boardX((Real)a + 20), UIStyle::boardY((Real)yy + 15), UIStyle::INK);
			MenuBox::words(pool.get(UIStyle::fitKey(rejoining ? "GUI:Reconnecting" : "GUI:NotResponding", label, UIStyle::px(150.0f))),
				UIStyle::menuFont(UIStyle::MENU_TEXT_LABEL), UIStyle::boardX((Real)a + 20), UIStyle::boardY((Real)yy + 36), UIStyle::MUTED);
			const Int fw = MenuBox::words(pool.get(figureText(seat.msLeft, rejoining)), figure, UIStyle::boardX((Real)a + 180), cy, UIStyle::INK);
			drain(UIStyle::boardX((Real)a + 180) + fw + UIStyle::px(10.0f), UIStyle::boardX((Real)a + 280), cy, share);
			seat.voteKey = UIStyle::boardRect(a + 292, yy + 10, a + hw - 76, yy + row - 10);
			const Bool hovered = mouseX >= seat.voteKey.lo.x && mouseX < seat.voteKey.hi.x && mouseY >= seat.voteKey.lo.y && mouseY < seat.voteKey.hi.y;
			voteKey(seat.voteKey, pool.get(UIStyle::capitals(TheGameText->fetch(seat.voted ? "GUI:Voted_Short" : "Network:Vote_Short"))), seat.voted, hovered);
			MenuBox::words(pool.get(votes), body, UIStyle::boardX((Real)a + hw - 66), cy, UIStyle::INK);
		} else {
			const Int cy = UIStyle::boardY((Real)y + row / 2);
			if (n % 2)
				MenuBox::fill(UIStyle::boardRect(x0, y, x1, y + row), GameMakeColor(255, 255, 255, 7));
			const IRegion2D stripe = UIStyle::boardRect(x0 + 2, y + 8, x0 + 8, y + row - 8);
			MenuBox::stripe(stripe.lo.x, stripe.lo.y, stripe.hi.y, stripe.hi.x - stripe.lo.x, colour);
			MenuBox::words(pool.get(UIStyle::fitName(seat.name, name20, UIStyle::px(250.0f))), name20, UIStyle::boardX((Real)x0 + 20), cy, UIStyle::INK);
			MenuBox::words(pool.get(UIStyle::fitKey(rejoining ? "GUI:Reconnecting" : "GUI:NotResponding", body, UIStyle::px(174.0f))), body,
				UIStyle::boardX((Real)x0 + 286), cy, UIStyle::INK);
			const Int fw = MenuBox::words(pool.get(figureText(seat.msLeft, rejoining)), figure, UIStyle::boardX((Real)x0 + 476), cy, UIStyle::INK);
			drain(UIStyle::boardX((Real)x0 + 476) + fw + UIStyle::px(14.0f), UIStyle::boardX((Real)x0 + 644), cy, share);
			seat.voteKey = UIStyle::boardRect(x0 + 660, y + 9, x0 + 808, y + row - 9);
			const Bool hovered = mouseX >= seat.voteKey.lo.x && mouseX < seat.voteKey.hi.x && mouseY >= seat.voteKey.lo.y && mouseY < seat.voteKey.hi.y;
			voteKey(seat.voteKey, pool.get(UIStyle::capitals(UIStyle::fitKey(seat.voted ? "GUI:Voted" : "Network:Vote", label, UIStyle::px(136.0f)))), seat.voted, hovered);
			const Int pw = pips(UIStyle::boardX((Real)x0 + 828), cy, seat.votes, m_votesToDrop);
			MenuBox::words(pool.get(votes), body, UIStyle::boardX((Real)x0 + 828) + pw, cy, UIStyle::INK);
			y += row;
		}
	}
	if (size.halves)
		y += size.rows * row;

	// everyone still talking
	MenuBox::rule(UIStyle::boardX((Real)x0), UIStyle::boardX((Real)x1), UIStyle::boardY((Real)y), 70);
	const Int line = DISCONNECT_CONNECTED_LINE;
	MenuBox::words(connected, label, UIStyle::boardX((Real)x0 + 20), UIStyle::boardY((Real)y + line / 2), UIStyle::MUTED);
	for (Int i = 0; i < nTalking; ++i) {
		const Int cy = UIStyle::boardY((Real)y + nameLine[i] * line + line / 2);
		const Int half = UIStyle::px(9.0f);
		MenuBox::stripe(nameX[i], cy - half, cy + half, max(2, UIStyle::px(4.0f)), seatColour(talking[i]));
		MenuBox::words(names[i], body, nameX[i] + UIStyle::px(12.0f), cy, UIStyle::INK);
	}
	y += connectedLines * line + 16 + 20;

	// the chat, its last lines
	MenuBox::rule(UIStyle::boardX((Real)x0), UIStyle::boardX((Real)x1), UIStyle::boardY((Real)y - 10), 110);
	for (Int i = max(0, m_chatCount - size.chatLines); i < m_chatCount; ++i) {
		MenuBox::words(pool.get(m_chat[i]), body, UIStyle::boardX((Real)x0), UIStyle::boardY((Real)y + DISCONNECT_CHAT_LINE / 2),
			m_chatSystem[i] ? UIStyle::MUTED : UIStyle::INK);
		y += DISCONNECT_CHAT_LINE;
	}

	UIStyle::boxFootRule(r);
	// the chat line's frame, under EA's entry field
	const IRegion2D quit = UIStyle::boxKeyRect1080(at1080, 0);
	const IRegion2D entry = UIStyle::boardRect(at1080.lo.x + UIStyle::BOX_PAD, at1080.hi.y - 68, quit.lo.x - 16, at1080.hi.y - 20);
	MenuBox::fill(entry, GameMakeColor(0, 0, 0, 120));
	MenuBox::outline(entry, UIStyle::FRAME);
}
