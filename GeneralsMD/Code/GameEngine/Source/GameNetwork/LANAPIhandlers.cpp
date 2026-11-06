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
// Modified 2025-2026 by Olcay Seygan for Zero Hour Reforged; see the git history.
// Modified 2026 by İlyas Akın for the macOS/Linux port; see NOTICE.md and the git history.

////////////////////////////////////////////////////////////////////////////////
//																																						//
//  (c) 2001-2003 Electronic Arts Inc.																				//
//																																						//
////////////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////////////////
// FILE: LANAPIHandlers.cpp
// Author: Matthew D. Campbell, October 2001
// Description: LAN callback handlers
///////////////////////////////////////////////////////////////////////////////////////

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine
#include "Lib/Clock.h"

#include "Lib/WideCharFns.h"

#include "Common/crc.h"
#include "Common/GameState.h"
#include "Common/Registry.h"
#include "Common/GlobalData.h"
#include "Common/QuotedPrintable.h"
#include "Common/UserPreferences.h"
#include "GameNetwork/GameDataMatch.h"
#include "GameNetwork/LANAPI.h"
#include "GameNetwork/LANAPICallbacks.h"
#include "GameNetwork/NetworkUtil.h"
#include "GameClient/MapUtil.h"

void LANAPI::handleRequestLocations( LANMessage *msg, UnsignedInt senderIP )
{
	if (m_inLobby)
	{
		LANMessage reply;
		fillInLANMessage( &reply );
		reply.LANMessageType = LANMessage::MSG_LOBBY_ANNOUNCE;

		sendMessage(&reply);
		m_lastResendTime = Clock_Milliseconds();
	}
	else
	{
		// In game - are we a game host?
		if (m_currentGame)
		{
			if (m_currentGame->getIP(0) == m_localIP)
			{
				LANMessage reply;
				fillInLANMessage( &reply );
				reply.LANMessageType = LANMessage::MSG_GAME_ANNOUNCE;
				AsciiString gameOpts = GenerateGameOptionsString();
				strlcpy(reply.GameInfo.options,gameOpts.str(),ARRAY_SIZE(reply.GameInfo.options));
				WideCharNCpy(reply.GameInfo.gameName, m_currentGame->getName().str(), g_lanGameNameLength);
				reply.GameInfo.gameName[g_lanGameNameLength] = 0;
				reply.GameInfo.inProgress = m_currentGame->isGameInProgress();

				// Empty when the room's options cannot fit the packet at all; announce nothing then.
				if (!gameOpts.isEmpty())
					sendMessage(&reply);
			}
			else
			{
				// We're a joiner
			}
		}
	}
	// Add the player to the lobby player list
	LANPlayer *player = LookupPlayer(senderIP);
	if (!player)
	{
		player = NEW LANPlayer;
		player->setIP(senderIP);
	}
	else
	{
		removePlayer(player);
	}
	player->setName(UnicodeString(msg->name));
	player->setHost(msg->hostName);
	player->setLogin(msg->userName);
	player->setLastHeard(Clock_Milliseconds());

	addPlayer(player);

	OnNameChange(player->getIP(), player->getName());
}

void LANAPI::handleGameAnnounce( LANMessage *msg, UnsignedInt senderIP )
{
	if (senderIP == m_localIP)
	{
		return; // Don't try to update own info
	}
	else if (m_currentGame && m_currentGame->isGameInProgress())
	{
		return; // Don't care about games if we're playing
	}
	else if (senderIP == m_directConnectRemoteIP)
	{

		if (m_currentGame == NULL)
		{
			LANGameInfo *game = LookupGame(UnicodeString(msg->GameInfo.gameName));
			if (!game)
			{
				game = NEW LANGameInfo;
				game->setName(UnicodeString(msg->GameInfo.gameName));
				addGame(game);
			}
			Bool success = ParseGameOptionsString(game,AsciiString(msg->GameInfo.options));
			game->setGameInProgress(msg->GameInfo.inProgress);
			game->setIsDirectConnect(msg->GameInfo.isDirectConnect);
			game->setLastHeard(Clock_Milliseconds());
			if (!success)
			{
				// remove from list
				removeGame(game);
				delete game;
				game = NULL;
				return;
			}
			RequestGameJoin(game, m_directConnectRemoteIP);
		}
	}
	else
	{
		LANGameInfo *game = LookupGame(UnicodeString(msg->GameInfo.gameName));
		// Games are listed by name, so an announce under a name already listed used to rewrite that
		// game, or remove it when the options did not parse, whoever sent it.  Only its host may.
		if (game && game->getIP(0) != senderIP)
		{
			DEBUG_LOG(("LANAPI::handleGameAnnounce - 0x%08x announced game '%s' hosted by 0x%08x, ignored\n",
				senderIP, WideCharAsUtf8( msg->GameInfo.gameName ).str(), game->getIP(0)));
			return;
		}
		if (!game)
		{
			game = NEW LANGameInfo;
			game->setName(UnicodeString(msg->GameInfo.gameName));
			addGame(game);
		}
		Bool success = ParseGameOptionsString(game,AsciiString(msg->GameInfo.options));
		game->setGameInProgress(msg->GameInfo.inProgress);
		game->setIsDirectConnect(msg->GameInfo.isDirectConnect);
		game->setLastHeard(Clock_Milliseconds());
		if (!success)
		{
			// remove from list
			removeGame(game);
			delete game;
			game = NULL;
		}

		OnGameList( m_games );
	//	if (game == m_currentGame && !m_inLobby)
	//		OnSlotList(RET_OK, game);
	}
}

void LANAPI::handleLobbyAnnounce( LANMessage *msg, UnsignedInt senderIP )
{
	LANPlayer *player = LookupPlayer(senderIP);
	if (!player)
	{
		player = NEW LANPlayer;
		player->setIP(senderIP);
	}
	else
	{
		removePlayer(player);
	}
	player->setName(UnicodeString(msg->name));
	player->setHost(msg->hostName);
	player->setLogin(msg->userName);
	player->setLastHeard(Clock_Milliseconds());

	addPlayer(player);

	OnNameChange(player->getIP(), player->getName());
}

void LANAPI::handleRequestGameInfo( LANMessage *msg, UnsignedInt senderIP )
{
	// In game - are we a game host?
	if (m_currentGame)
	{
		if (m_currentGame->getIP(0) == m_localIP || (m_currentGame->isGameInProgress() && TheNetwork && TheNetwork->isPacketRouter())) // if we're in game we should reply if we're the packet router
		{
			LANMessage reply;
			fillInLANMessage( &reply );
			reply.LANMessageType = LANMessage::MSG_GAME_ANNOUNCE;

			AsciiString gameOpts = GameInfoToAsciiString(m_currentGame);
			if (gameOpts.isEmpty())
				return;
			strlcpy(reply.GameInfo.options,gameOpts.str(),ARRAY_SIZE(reply.GameInfo.options));
			WideCharNCpy(reply.GameInfo.gameName, m_currentGame->getName().str(), g_lanGameNameLength);
			reply.GameInfo.gameName[g_lanGameNameLength] = 0;
			reply.GameInfo.inProgress = m_currentGame->isGameInProgress();
			reply.GameInfo.isDirectConnect = m_currentGame->getIsDirectConnect();

			sendMessage(&reply, senderIP);
		}
	}
}

void LANAPI::handleRequestJoin( LANMessage *msg, UnsignedInt senderIP )
{
	UnsignedInt responseIP = senderIP;	// need this cause the player may or may not be
																			// in the player list at the sendMessage.

	if (msg->GameToJoin.gameIP != m_localIP)
	{
		return; // Not us.  Ignore it.
	}
	LANMessage reply;
	fillInLANMessage( &reply );
	if (!m_inLobby && m_currentGame && m_currentGame->getIP(0) == m_localIP)
	{
		if (m_currentGame->isGameInProgress())
		{
			reply.LANMessageType = LANMessage::MSG_JOIN_DENY;
			reply.GameNotJoined.reason = LANAPIInterface::RET_GAME_STARTED;
			reply.GameNotJoined.gameIP = m_localIP;
			reply.GameNotJoined.playerIP = senderIP;
			DEBUG_LOG(("LANAPI::handleRequestJoin - join denied because game already started.\n"));
		}
		else
		{
			int player;
			Bool canJoin = true;

			/* See if the CRCs match.  EA wrote this check and then commented it out, so a LAN game
				 between two machines with different data started happily and desynced a few minutes in,
				 with nothing to tell either player why.  It is back: the join request already carries
				 both CRCs, RET_CRC_MISMATCH already exists, and the string it maps to is already
				 translated.  A dev build with -netMinPlayers still skips it, the way EA left it. */
#if defined(_DEBUG) || defined(_INTERNAL)
			if (TheGlobalData->m_netMinPlayers > 0) {
#endif
			GameDataMatchResult dataMatch = compareGameData(
				msg->GameToJoin.exeCRC, msg->GameToJoin.iniCRC,
				TheGlobalData->m_exeCRC, TheGlobalData->m_iniCRC );
			if (dataMatch != GAMEDATA_MATCHES)
			{
				DEBUG_LOG(("LANAPI::handleRequestJoin - join denied, %s. CRCs are them/us INI:%X/%X exe:%X/%X\n",
					gameDataMatchName(dataMatch),
					msg->GameToJoin.iniCRC, TheGlobalData->m_iniCRC,
					msg->GameToJoin.exeCRC, TheGlobalData->m_exeCRC));
				reply.LANMessageType = LANMessage::MSG_JOIN_DENY;
				reply.GameNotJoined.reason = LANAPIInterface::RET_CRC_MISMATCH;
				reply.GameNotJoined.gameIP = m_localIP;
				reply.GameNotJoined.playerIP = senderIP;
				canJoin = false;
			}
#if defined(_DEBUG) || defined(_INTERNAL)
			}
#endif
			
			/* Check for a duplicate serial - unless both ends are on loopback, which is two copies
				 of the game on one machine and therefore one installation, one registry and one
				 serial by construction.  EA's check is the retail one-key-one-seat rule and it stays
				 on for every address that could be a second machine; here it would refuse the join
				 every time and there would be no way to play a LAN game against yourself at all. */
			AsciiString s;
			const Bool sameMachine = IsLoopbackIP( m_localIP ) && IsLoopbackIP( senderIP );
			for (player = 0; canJoin && !sameMachine && player<MAX_SLOTS; ++player)
			{
				LANGameSlot *slot = m_currentGame->getLANSlot(player);
				s.clear();
				if (player == 0)
				{
					GetStringFromRegistry("\\ergc", "", s);
				}
				else if (slot->isHuman())
				{
					s = slot->getSerial();
					if (s.isEmpty())
						s = "<Munkee>";
				}

				if (s.isNotEmpty())
				{
					DEBUG_LOG(("Checking serial '%s' in slot %d\n", s.str(), player));

					if (!strncmp(s.str(), msg->GameToJoin.serial, g_maxSerialLength))
					{
						// serials match!  kick the punk!
						reply.LANMessageType = LANMessage::MSG_JOIN_DENY;
						reply.GameNotJoined.reason = LANAPIInterface::RET_SERIAL_DUPE;
						reply.GameNotJoined.gameIP = m_localIP;
						reply.GameNotJoined.playerIP = senderIP;
						canJoin = false;

						DEBUG_LOG(("LANAPI::handleRequestJoin - join denied because of duplicate serial # (%s).\n", s.str()));
						break;
					}
				}
			}

			// We're the host, and the name he sends is his own choosing.  It ends up in the game
			// state string the lobby passes around, which is parsed on commas, colons and
			// semicolons - so a name holding one of those rewrites everyone's idea of who is in
			// the room.  Control characters and lone surrogates do their own damage in a text
			// field, and a name of nothing but spaces looks like an empty seat.
			if (canJoin && !IsUsablePlayerName(msg->name))
			{
				// deny as a duplicate name: the client already knows how to show that one
				reply.LANMessageType = LANMessage::MSG_JOIN_DENY;
				reply.GameNotJoined.reason = LANAPIInterface::RET_DUPLICATE_NAME;
				reply.GameNotJoined.gameIP = m_localIP;
				reply.GameNotJoined.playerIP = senderIP;
				canJoin = false;

				DEBUG_LOG(("LANAPI::handleRequestJoin - join denied, the name holds characters a name may not.\n"));
			}

			// Then see if he has a duplicate name
			for (player = 0; canJoin && player<MAX_SLOTS; ++player)
			{
				LANGameSlot *slot = m_currentGame->getLANSlot(player);
				if (slot->isHuman() && slot->getName().compare(msg->name) == 0)
				{
					// just deny duplicates
					reply.LANMessageType = LANMessage::MSG_JOIN_DENY;
					reply.GameNotJoined.reason = LANAPIInterface::RET_DUPLICATE_NAME;
					reply.GameNotJoined.gameIP = m_localIP;
					reply.GameNotJoined.playerIP = senderIP;
					canJoin = false;

					DEBUG_LOG(("LANAPI::handleRequestJoin - join denied because of duplicate names.\n"));
					break;
				}
			}

			// See if there's room.  An open slot is room: the host is the one who opens and closes
			// them, and the map's own number of starting positions used to overrule that - so a
			// four position map turned away the fifth player even when the host had left a seat
			// open for him, and the slot he was refused stayed open on everyone's screen.
			for (player = 0; canJoin && player<MAX_SLOTS; ++player)
			{
				if (m_currentGame->getLANSlot(player)->isOpen())
				{
					// OK, add him in.
					reply.LANMessageType = LANMessage::MSG_JOIN_ACCEPT;
					WideCharNCpy(reply.GameJoined.gameName, m_currentGame->getName().str(), g_lanGameNameLength);
					reply.GameJoined.gameName[g_lanGameNameLength] = 0;
					reply.GameJoined.slotPosition = player;
					reply.GameJoined.gameIP = m_localIP;
					reply.GameJoined.playerIP = senderIP;

					LANGameSlot newSlot;
					newSlot.setState(SLOT_PLAYER, UnicodeString(msg->name));
					newSlot.setIP(senderIP);
					newSlot.setPort(NETWORK_BASE_PORT_NUMBER);
					newSlot.setLastHeard(Clock_Milliseconds());
					newSlot.setSerial(msg->GameToJoin.serial);
					m_currentGame->setSlot(player,newSlot);
					DEBUG_LOG(("LANAPI::handleRequestJoin - added player %s at ip 0x%08x to the game\n", WideCharAsUtf8( msg->name ).str(), senderIP));

					OnPlayerJoin(player, UnicodeString(msg->name));
					responseIP = 0;

					break;
				}
			}
			if (canJoin && player == MAX_SLOTS)
			{
				reply.LANMessageType = LANMessage::MSG_JOIN_DENY;
				WideCharNCpy(reply.GameNotJoined.gameName, m_currentGame->getName().str(), g_lanGameNameLength);
				reply.GameNotJoined.gameName[g_lanGameNameLength] = 0;
				reply.GameNotJoined.reason = LANAPIInterface::RET_GAME_FULL;
				reply.GameNotJoined.gameIP = m_localIP;
				reply.GameNotJoined.playerIP = senderIP;
				DEBUG_LOG(("LANAPI::handleRequestJoin - join denied because game is full.\n"));
			}
		}
	}
	else
	{
		reply.LANMessageType = LANMessage::MSG_JOIN_DENY;
		reply.GameNotJoined.reason = LANAPIInterface::RET_GAME_GONE;
		reply.GameNotJoined.gameIP = m_localIP;
		reply.GameNotJoined.playerIP = senderIP;
	}
	sendMessage(&reply, responseIP);
	RequestGameOptions(GenerateGameOptionsString(), true);
}

void LANAPI::handleJoinAccept( LANMessage *msg, UnsignedInt senderIP )
{
	if (msg->GameJoined.playerIP == m_localIP) // Is it for us?
	{
		// The slot is the sender's number and indexes our slot array, and the accept has to come from
		// the game's own host; anyone else could seat us in a game, or past the end of one.  A direct
		// connect host behind a router lists his inside address, so the address we dialled counts too.
		LANGameInfo *joined = LookupGame(UnicodeString(msg->GameJoined.gameName));
		const Int pos = msg->GameJoined.slotPosition;
		const Bool fromHost = joined && (joined->getIP(0) == senderIP || senderIP == m_directConnectRemoteIP);
		if (joined && (!fromHost || pos <= 0 || pos >= MAX_SLOTS))
		{
			DEBUG_LOG(("LANAPI::handleJoinAccept - accept from 0x%08x for slot %d is not the host's, ignored\n", senderIP, pos));
			return;
		}

		if (m_pendingAction == ACT_JOIN) // Are we trying to join?
		{
			m_currentGame = joined;

			if (!m_currentGame)
			{
				DEBUG_ASSERTCRASH(false, ("Could not find game to join!"));
				OnGameJoin(RET_UNKNOWN, NULL);
			}
			else
			{
				m_inLobby = false;
				AsciiString options = GameInfoToAsciiString(m_currentGame);
				m_currentGame->enterGame();
				ParseAsciiStringToGameInfo(m_currentGame, options);

				Int pos = msg->GameJoined.slotPosition;

				LANGameSlot slot;
				slot.setState(SLOT_PLAYER, m_name);
				slot.setIP(m_localIP);
				slot.setPort(NETWORK_BASE_PORT_NUMBER);
				slot.setLastHeard(0);
				slot.setLogin(m_userName);
				slot.setHost(m_hostName);
				m_currentGame->setSlot(pos, slot);

				m_currentGame->getLANSlot(0)->setHost(msg->hostName);
				m_currentGame->getLANSlot(0)->setLogin(msg->userName);

				LANPreferences prefs;
				AsciiString entry;
				entry.format("%d.%d.%d.%d:%s", senderIP >> 24, (senderIP & 0xff0000) >> 16, (senderIP & 0xff00) >> 8, senderIP & 0xff, UnicodeStringToQuotedPrintable(m_currentGame->getSlot(0)->getName()).str());
				prefs["RemoteIP0"] = entry;
				prefs.write();

				OnGameJoin(RET_OK, m_currentGame);
				//DEBUG_ASSERTCRASH(false, ("setting host to %ls@%ls\n", m_currentGame->getLANSlot(0)->getUser()->getLogin().str(),
				//	m_currentGame->getLANSlot(0)->getUser()->getHost().str()));
			}
			m_pendingAction = ACT_NONE;
			m_expiration = 0;
		}
	}
}

void LANAPI::handleJoinDeny( LANMessage *msg, UnsignedInt senderIP )
{
	if (msg->GameJoined.playerIP == m_localIP) // Is it for us?
	{
		if (m_pendingAction == ACT_JOIN) // Are we trying to join?
		{
			OnGameJoin(msg->GameNotJoined.reason, LookupGame(UnicodeString(msg->GameNotJoined.gameName)));
			m_pendingAction = ACT_NONE;
			m_expiration = 0;
		}
	}
}

void LANAPI::handleRequestGameLeave( LANMessage *msg, UnsignedInt senderIP )
{
	if (!m_inLobby && m_currentGame && !m_currentGame->isGameInProgress())
	{
		int player;
		for (player = 0; player < MAX_SLOTS; ++player)
		{
			if (m_currentGame->getIP(player) == senderIP)
			{
				if (player == 0)
				{
					OnHostLeave();
					removeGame(m_currentGame);
					delete m_currentGame;
					m_currentGame = NULL;

					/// @todo re-add myself to lobby?  Or just keep me there all the time?  If we send a LOBBY_ANNOUNCE things'll work out...
					LANPlayer *lanPlayer = LookupPlayer(m_localIP);
					if (!lanPlayer)
					{
						lanPlayer = NEW LANPlayer;
						lanPlayer->setIP(m_localIP);
					}
					else
					{
						removePlayer(lanPlayer);
					}
					lanPlayer->setName(UnicodeString(m_name));
					lanPlayer->setHost(m_hostName);
					lanPlayer->setLogin(m_userName);
					lanPlayer->setLastHeard(Clock_Milliseconds());
					addPlayer(lanPlayer);

				}
				else
				{
					if (AmIHost())
					{
						// remove the deadbeat
						LANGameSlot slot;
						slot.setState(SLOT_OPEN);
						m_currentGame->setSlot( player, slot );
					}
					OnPlayerLeave(UnicodeString(msg->name));
					m_currentGame->getLANSlot(player)->setState(SLOT_OPEN);
					m_currentGame->resetAccepted();
					RequestGameOptions(GenerateGameOptionsString(), false, senderIP);
					//m_currentGame->endGame();
				}
				break;
			}
			DEBUG_ASSERTCRASH(player < MAX_SLOTS, ("Didn't find player!"));
		}
	}
	else if (m_inLobby)
	{
		// Look for dissappearing games
		LANGameInfo *game = m_games;
		while (game)
		{
			// only the host closing his own game takes it off the list, not anyone who names it
			if (game->getName().compare(msg->GameToLeave.gameName) == 0 && game->getIP(0) == senderIP)
			{
				removeGame(game);
				delete game;
				OnGameList(m_games);
				break;
			}
			game = game->getNext();
		}
	}
}

void LANAPI::handleRequestLobbyLeave( LANMessage *msg, UnsignedInt senderIP )
{
	if (m_inLobby)
	{
		LANPlayer *player = m_lobbyPlayers;
		while (player)
		{
			if (player->getIP() == senderIP)
			{
				removePlayer(player);
				OnPlayerList(m_lobbyPlayers);
				break;
			}
			player = player->getNext();
		}
	}
}

void LANAPI::handleSetAccept( LANMessage *msg, UnsignedInt senderIP )
{
	if (!m_inLobby && m_currentGame && !m_currentGame->isGameInProgress())
	{
		int player;
		for (player = 0; player < MAX_SLOTS; ++player)
		{
			if (m_currentGame->getIP(player) == senderIP)
			{
				OnAccept(senderIP, msg->Accept.isAccepted);
				break;
			}
		}
	}
}

void LANAPI::handleHasMap( LANMessage *msg, UnsignedInt senderIP )
{
	if (!m_inLobby && m_currentGame)
	{
		CRC mapNameCRC;
//	mapNameCRC.computeCRC(m_currentGame->getMap().str(), m_currentGame->getMap().getLength());
		AsciiString portableMapName = TheGameState->realMapPathToPortableMapPath(m_currentGame->getMap());
		mapNameCRC.computeCRC(portableMapName.str(), portableMapName.getLength());
		if (mapNameCRC.get() != msg->MapStatus.mapCRC)
		{
			return;
		}

		int player;
		for (player = 0; player < MAX_SLOTS; ++player)
		{
			if (m_currentGame->getIP(player) == senderIP)
			{
				OnHasMap(senderIP, msg->MapStatus.hasMap);
				break;
			}
		}
	}
}

void LANAPI::handleChat( LANMessage *msg, UnsignedInt senderIP )
{
	if (m_inLobby)
	{
		LANPlayer *player;
		if((player=LookupPlayer(senderIP)) != 0)
		{
			OnChat(UnicodeString(player->getName()), player->getIP(), UnicodeString(msg->Chat.message), msg->Chat.chatType);
			player->setLastHeard(Clock_Milliseconds());
		}
	}
	else
	{
		if (LookupGame(UnicodeString(msg->Chat.gameName)) != m_currentGame)
		{
			DEBUG_LOG(("Game '%s' is not my game\n", WideCharAsUtf8( msg->Chat.gameName ).str()));
			if (m_currentGame)
			{
				DEBUG_LOG(("Current game is '%s'\n", WideCharAsUtf8( m_currentGame->getName().str() ).str()));
			}
			return;
		}

		int player;
		for (player = 0; player < MAX_SLOTS; ++player)
		{
			if (m_currentGame && m_currentGame->getIP(player) == senderIP)
			{
				OnChat(UnicodeString(msg->name), m_currentGame->getIP(player), UnicodeString(msg->Chat.message), msg->Chat.chatType);
				break;
			}
		}
	}
}

void LANAPI::handleGameStart( LANMessage *msg, UnsignedInt senderIP )
{
	if (!m_inLobby && m_currentGame && m_currentGame->getIP(0) == senderIP && !m_currentGame->isGameInProgress())
	{
		OnGameStart();
	}
}

void LANAPI::handleGameStartTimer( LANMessage *msg, UnsignedInt senderIP )
{
	if (!m_inLobby && m_currentGame && m_currentGame->getIP(0) == senderIP && !m_currentGame->isGameInProgress())
	{
		OnGameStartTimer(msg->StartTimer.seconds);
	}
}

void LANAPI::handleGameOptions( LANMessage *msg, UnsignedInt senderIP )
{
	if (!m_inLobby && m_currentGame && !m_currentGame->isGameInProgress())
	{
		int player;
		for (player = 0; player < MAX_SLOTS; ++player)
		{
			if (m_currentGame->getIP(player) == senderIP)
			{
				OnGameOptions(senderIP, player, AsciiString(msg->GameOptions.options));
				break;
			}
		}
	}
}

void LANAPI::handleInActive(LANMessage *msg, UnsignedInt senderIP) {
	if (m_inLobby || (m_currentGame == NULL) || (m_currentGame->isGameInProgress())) {
		return;
	}

	// check to see if we are the host of this game.
	if (m_currentGame->amIHost() == FALSE) {
		return;
	}

	UnicodeString playerName;
	playerName = msg->name;

	Int slotNum = m_currentGame->getSlotNum(playerName);
	if (slotNum < 0)
		return;
	GameSlot *slot = m_currentGame->getSlot(slotNum);
	if (slot == NULL) {
		return;
	}

	if (senderIP != slot->getIP()) {
		return;
	}

	// don't want to unaccept the host, that's silly.  They can't hit start alt-tabbed anyways.
	if (senderIP == TheLAN->GetLocalIP()) {
		return;
	}

	// only unaccept if the timer hasn't started yet.
	if (m_gameStartTime != 0) {
		return;
	}

	slot->unAccept();
	AsciiString options = GenerateGameOptionsString();
	RequestGameOptions(options, FALSE);
	lanUpdateSlotList();
}
