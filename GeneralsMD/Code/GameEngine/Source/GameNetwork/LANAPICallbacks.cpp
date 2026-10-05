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
// FILE: LANAPICallbacks.cpp
// Author: Chris Huybregts, October 2001
// Description: LAN API Callbacks
///////////////////////////////////////////////////////////////////////////////////////
#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine
#include "Lib/Clock.h"

#include "strtok_r.h"
#include "Common/GameEngine.h"
#include "Common/GlobalData.h"
#include "Common/MessageStream.h"
#include "Common/MultiplayerSettings.h"
#include "Common/PlayerTemplate.h"
#include "Common/QuotedPrintable.h"
#include "Common/RandomValue.h"
#include "Common/UserPreferences.h"
#include "GameClient/GameText.h"
#include "GameClient/GUICallbacks.h"
#include "GameClient/LanguageFilter.h"
#include "GameClient/MapUtil.h"
#include "GameClient/MessageBox.h"
#include "GameLogic/GameLogic.h"
#include "GameNetwork/FileTransfer.h"
#include "GameNetwork/LANAPICallbacks.h"
#include "GameNetwork/NetworkUtil.h"

LANAPI *TheLAN = NULL;
extern Bool LANbuttonPushed;

#ifdef _INTERNAL
// for occasional debugging...
//#pragma optimize("", off)
//#pragma MESSAGE("************************************** WARNING, optimization disabled for debugging purposes")
#endif

//Colors used for the chat dialogs
const Color playerColor =  GameMakeColor(255,255,255,255);
const Color gameColor =  GameMakeColor(255,255,255,255);
const Color gameInProgressColor =  GameMakeColor(128,128,128,255);
const Color chatNormalColor =  GameMakeColor(50,215,230,255);
const Color chatActionColor =  GameMakeColor(255,0,255,255);
const Color chatLocalNormalColor =  GameMakeColor(255,128,0,255);
const Color chatLocalActionColor =  GameMakeColor(128,255,255,255);
const Color chatSystemColor =  GameMakeColor(255,255,255,255);
const Color acceptTrueColor =  GameMakeColor(0,255,0,255);
const Color acceptFalseColor =  GameMakeColor(255,0,0,255);


UnicodeString LANAPIInterface::getErrorStringFromReturnType( ReturnType ret )
{
	switch (ret)
	{
		case RET_OK:
			return TheGameText->fetch("LAN:OK");
		case RET_TIMEOUT:
			return TheGameText->fetch("LAN:ErrorTimeout");
		case RET_GAME_FULL:
			return TheGameText->fetch("LAN:ErrorGameFull");
		case RET_DUPLICATE_NAME:
			return TheGameText->fetch("LAN:ErrorDuplicateName");
		case RET_CRC_MISMATCH:
			return TheGameText->fetch("LAN:ErrorCRCMismatch");
		case RET_GAME_STARTED:
			return TheGameText->fetch("LAN:ErrorGameStarted");
		case RET_GAME_EXISTS:
			return TheGameText->fetch("LAN:ErrorGameExists");
		case RET_GAME_GONE:
			return TheGameText->fetch("LAN:ErrorGameGone");
		case RET_BUSY:
			return TheGameText->fetch("LAN:ErrorBusy");
		case RET_SERIAL_DUPE:
			return TheGameText->fetch("WOL:ChatErrorSerialDup");
		default:
			return TheGameText->fetch("LAN:ErrorUnknown");
	}
}

// On functions are (generally) the result of network traffic

void LANAPI::OnAccept( UnsignedInt playerIP, Bool status ) 
{ 
	if( AmIHost() )
	{
		
		// i is used after the loop; VC6 for-scope let it escape.
		Int i;
		for (i = 0; i < MAX_SLOTS; i++)
		{
			if (m_currentGame->getIP(i) == playerIP)
			{
				if(status)
					m_currentGame->getLANSlot(i)->setAccept();
				else
					m_currentGame->getLANSlot(i)->unAccept();
				break;
			}// if
		}// for
		if (i != MAX_SLOTS ) 
		{
			RequestGameOptions( GenerateGameOptionsString(), false );
			lanUpdateSlotList();
		}
	}//if
	else 
	{
		//i'm not the host but if the accept came from the host...
		if( m_currentGame->getIP(0) == playerIP )
		{
			UnicodeString text;
			text = TheGameText->fetch("GUI:HostWantsToStart");
			OnChat(UnicodeString(u"SYSTEM"), m_localIP, text, LANCHAT_SYSTEM);				
		}
	}
}// void LANAPI::OnAccept( UnicodeString player, Bool status ) 

void LANAPI::OnHasMap( UnsignedInt playerIP, Bool status ) 
{ 
	if( AmIHost() )
	{
		
		// i is used after the loop; VC6 for-scope let it escape.
		Int i;
		for (i = 0; i < MAX_SLOTS; i++)
		{
			if (m_currentGame->getIP(i) == playerIP)
			{
				m_currentGame->getLANSlot(i)->setMapAvailability( status );
				break;
			}// if
		}// for
		if (i != MAX_SLOTS ) 
		{
			UnicodeString mapDisplayName;
			const MapMetaData *mapData = TheMapCache->findMap( m_currentGame->getMap() );
			Bool willTransfer = TRUE;
			if (mapData)
			{
				mapDisplayName.format(u"%ls", mapData->m_displayName.str());
				if (mapData->m_isOfficial)
					willTransfer = FALSE;
			}
			else
			{
				mapDisplayName.format(u"%hs", m_currentGame->getMap().str());
				willTransfer = WouldMapTransfer(m_currentGame->getMap());
			}
			if (!status)
			{
				UnicodeString text;
				if (willTransfer)
					text.format(TheGameText->fetch("GUI:PlayerNoMapWillTransfer"), m_currentGame->getLANSlot(i)->getName().str(), mapDisplayName.str());
				else
					text.format(TheGameText->fetch("GUI:PlayerNoMap"), m_currentGame->getLANSlot(i)->getName().str(), mapDisplayName.str());
				OnChat(UnicodeString(u"SYSTEM"), m_localIP, text, LANCHAT_SYSTEM);
			}
			lanUpdateSlotList();
		}
	}//if
}// void LANAPI::OnHasMap( UnicodeString player, Bool status ) 

/** -----------------------------------------------------------------------------------------------
 * -netgame: start a LAN game from a slot list given on the command line instead of one negotiated
 * in the lobby.  Everything the lobby ever produces is here - who is in which slot, at which
 * address, on which map, with which seed - so the announce/join/accept round trips can be skipped
 * entirely and the game set up in one call.  This is OnGameStart() with the preferences, the GUI
 * and the map transfer taken out; the network setup below is deliberately kept identical to it,
 * because that is what decides who talks to whom.
 *
 * Every machine must be given the same list in the same order: the slot order is the player order,
 * and ConnectionManager::parseUserList picks the local slot out of it by IP, so each copy also
 * needs its own address (-netslot picks which one it is).  Two copies on one machine therefore
 * want two addresses out of 127.0.0.0/8, which Windows routes to loopback in its entirety.
 */
Bool LANAPI::StartAutomatedGame( AsciiString mapName, Int seed, const UnsignedInt *slotIPs,
																 Int numSlots, Int localSlot )
{
	if (numSlots < 2 || numSlots > MAX_SLOTS || localSlot < 0 || localSlot >= numSlots)
	{
		DEBUG_LOG(("-netgame: %d slots with the local player at %d makes no game\n", numSlots, localSlot));
		return FALSE;
	}

	/* init() binds the lobby socket to m_localIP, so the address has to be ours before it runs.
		 Nothing is ever sent on that socket here - the lobby is only pumped from the LAN menus - but
		 leaving it bound to INADDR_ANY would mean the second copy on a machine fails to bind. */
	m_localIP = slotIPs[localSlot];
	init();
	m_isInLANMenu = FALSE;
	m_inLobby = FALSE;

	LANGameInfo *game = NEW LANGameInfo;
	game->enterGame();
	/* enterGame() resets, and reset() seeds itself from Clock_Milliseconds_Coarse() - so the seed we were
		 handed has to be set after it, or the replay header records a clock reading and plays back
		 a different game than the one that ran. */
	game->setSeed( seed );

	UnicodeString gameName;
	gameName.format( u"%8.8X", slotIPs[0] );
	game->setName( gameName );

	/* -teams splits the slot list into allied blocks the same way it splits an -autoskirmish lobby:
		 with four slots and two teams the first two are team 0 and the rest team 1.  Every machine is
		 handed the same slot count and the same team count, so every machine works out the same
		 split.  Without it every slot is -1, "no team", and everybody fights everybody - which is
		 what a scripted throughput test wants and what a test of anything allied cannot use. */
	const Int teams = TheGlobalData->m_autoSkirmishTeams;
	const Bool apocalypse = TheGlobalData->m_apocalypseMode > APOCALYPSE_OFF;
	// -netai seats come after the addresses and take part in the team split like any other seat
	const Int numSeats = numSlots + TheGlobalData->m_netGameAISlots;
	const Int slotsPerTeam = (teams > 1 && numSeats >= teams) ? ((numSeats + teams - 1) / teams) : 0;

	for (Int i = 0; i < numSeats; ++i)
	{
		/* The names have to differ: GameInfo looks players up by name, and the player list ends up
			 with one side per slot named after it. */
		UnicodeString playerName;
		playerName.format( u"Player%d", i + 1 );

		// Apocalypse puts every seat on team 1, the way its lobby does, whatever -teams says
		Int teamNumber = apocalypse ? 0 : -1;
		if (slotsPerTeam > 0 && !apocalypse)
		{
			teamNumber = i / slotsPerTeam;
			if (teamNumber >= teams)
				teamNumber = teams - 1;		// an uneven split puts the remainder on the last team
		}

		LANGameSlot slot;
		if (i < numSlots)
		{
			slot.setState( SLOT_PLAYER, playerName );
			slot.setIP( slotIPs[i] );
			slot.setPort( NETWORK_BASE_PORT_NUMBER );	// one address per player, so one port does for all
			slot.setLastHeard( Clock_Milliseconds() );
			slot.setLogin( m_userName );
			slot.setHost( m_hostName );
		}
		else
		{
			slot.setState( (SlotState)TheGlobalData->m_autoSkirmishAIState );
		}
		slot.setPlayerTemplate( PLAYERTEMPLATE_RANDOM );
		slot.setColor( -1 );			// -1 is "random" to populateRandomSideAndColor
		slot.setStartPos( -1 );		// and to populateRandomStartPosition
		slot.setTeamNumber( teamNumber );
		slot.setAccept();
		game->setSlot( i, slot );

		if (i == localSlot)
			m_name = playerName;
	}

	game->setNext( NULL );
	game->setMap( mapName );
	game->setIncomeSharing( TheGlobalData->m_incomeSharing );
	game->setTechRespawn( TheGlobalData->m_techRespawn );
	game->setSupplyPileLimit( TheGlobalData->m_supplyPileLimit );
	game->setApocalypseMode( TheGlobalData->m_apocalypseMode );
	game->setIsDirectConnect( FALSE );
	game->setLastHeard( Clock_Milliseconds() );
	game->setLocalIP( m_localIP );

	/* The map is not transferred, so both machines have to already have it - but say what we have,
		 the way the game options screen does, since the replay header carries it. */
	const MapMetaData *md = TheMapCache->findMap( mapName );
	if (md != NULL)
	{
		game->setMapCRC( md->m_CRC );
		game->setMapSize( md->m_filesize );
	}

	m_currentGame = game;
	addGame( game );

	// Set up the game network - same order and same arguments as OnGameStart()
	if (TheNetwork != NULL)
	{
		delete TheNetwork;
		TheNetwork = NULL;
	}

	TheNetwork = NetworkInterface::createNetwork();
	TheNetwork->init();
	TheNetwork->setLocalAddress( m_localIP, NETWORK_BASE_PORT_NUMBER );
	TheNetwork->initTransport();
	TheNetwork->parseUserList( m_currentGame );

	if (TheGameLogic->isInGame())
		TheGameLogic->clearGameData();

	m_currentGame->startGame( 0 );

	TheWritableGlobalData->m_pendingFile = m_currentGame->getMap();
	TheWritableGlobalData->m_shellMapOn = FALSE;
	TheWritableGlobalData->m_playIntro = FALSE;

	GameMessage *msg = TheMessageStream->appendMessage( GameMessage::MSG_NEW_GAME );
	msg->appendIntegerArgument( GAME_LAN );

	InitRandom( seed );

	DEBUG_LOG(("-netgame: %d players on '%s', seed %d, we are slot %d at %8.8X\n",
		numSlots, mapName.str(), seed, localSlot, m_localIP));
	return TRUE;
}

void LANAPI::OnGameStartTimer( Int seconds )
{
	UnicodeString text;
	if (seconds == 1)
		text.format(TheGameText->fetch("LAN:GameStartTimerSingular"), seconds);
	else
		text.format(TheGameText->fetch("LAN:GameStartTimerPlural"), seconds);
	OnChat(UnicodeString(u"SYSTEM"), m_localIP, text, LANCHAT_SYSTEM);
}

void LANAPI::OnGameStart( void )
{
	//DEBUG_LOG(("Map is '%s', preview is '%s'\n", m_currentGame->getMap().str(), GetPreviewFromMap(m_currentGame->getMap()).str()));
	//DEBUG_LOG(("Map is '%s', INI is '%s'\n", m_currentGame->getMap().str(), GetINIFromMap(m_currentGame->getMap()).str()));

	if (m_currentGame)
	{
		LANPreferences pref;
		AsciiString option;
		option.format("%d", m_currentGame->getLANSlot( m_currentGame->getLocalSlotNum() )->getPlayerTemplate());
		pref["PlayerTemplate"] = option;
		option.format("%d", m_currentGame->getLANSlot( m_currentGame->getLocalSlotNum() )->getColor());
		pref["Color"] = option;
		if (m_currentGame->amIHost())
    {
    	pref["Map"] = AsciiStringToQuotedPrintable(m_currentGame->getMap());
      pref.setSuperweaponRestriction( m_currentGame->getSuperweaponRestriction() );
      pref.setInt( "UnitLimit", m_currentGame->getUnitLimit() ? 1 : 0 );
      pref.setInt( "ProRules", m_currentGame->getProRules() ? 1 : 0 );
      pref.setInt( "IncomeSharing", m_currentGame->getIncomeSharing() );
      pref.setInt( "TechRespawn", m_currentGame->getTechRespawn() );
      pref.setInt( "SupplyPileLimit", m_currentGame->getSupplyPileLimit() );
      pref.setStartingCash( m_currentGame->getStartingCash() );
    }
		pref.write();

		m_isInLANMenu = FALSE;

		//m_currentGame->startGame(0);

		// Set up the game network
		DEBUG_ASSERTCRASH(TheNetwork == NULL, ("For some reason TheNetwork isn't NULL at the start of this game.  Better look into that."));

		if (TheNetwork != NULL) {
			delete TheNetwork;
			TheNetwork = NULL;
		}

		// Time to initialize TheNetwork for this game.
		TheNetwork = NetworkInterface::createNetwork();
		TheNetwork->init();
		TheNetwork->setLocalAddress(m_localIP, 8088);
		TheNetwork->initTransport();

		TheNetwork->parseUserList(m_currentGame);

		if (TheGameLogic->isInGame())
			TheGameLogic->clearGameData();

		Bool filesOk = DoAnyMapTransfers(m_currentGame);

		// see if we really have the map.  if not, back out.
		TheMapCache->updateCache();
		if (!filesOk || TheMapCache->findMap(m_currentGame->getMap()) == NULL)
		{
			DEBUG_LOG(("After transfer, we didn't really have the map.  Bailing...\n"));
			OnPlayerLeave(m_name);
			removeGame(m_currentGame);
			m_currentGame = NULL;
			m_inLobby = TRUE;
			if (TheNetwork != NULL) {
				delete TheNetwork;
				TheNetwork = NULL;
			}
			OnChat(UnicodeString::TheEmptyString, 0, TheGameText->fetch("GUI:CouldNotTransferMap"), LANCHAT_SYSTEM);
			return;
		}

		m_currentGame->startGame(0);

		// shutdown the top, but do not pop it off the stack
		//TheShell->hideShell();
		// setup the Global Data with the Map and Seed
		TheWritableGlobalData->m_pendingFile = m_currentGame->getMap();

		// send a message to the logic for a new game
		GameMessage *msg = TheMessageStream->appendMessage( GameMessage::MSG_NEW_GAME );
		msg->appendIntegerArgument(GAME_LAN);

		TheWritableGlobalData->m_useFpsLimit = false;

		// Set the random seed
		InitRandom( m_currentGame->getSeed() );
		DEBUG_LOG(("InitRandom( %d )\n", m_currentGame->getSeed()));
	}
}

void LANAPI::OnGameOptions( UnsignedInt playerIP, Int playerSlot, AsciiString options )
{
	if (!m_currentGame)
		return;

	if (m_currentGame->getIP(playerSlot) != playerIP)
		return; // He's not in our game?!?
	
	
	if (m_currentGame->isGameInProgress())
		return; // we don't want to process any game options while in game.

	if (playerSlot == 0 && !m_currentGame->amIHost())
	{
		m_currentGame->setLastHeard(Clock_Milliseconds());
		AsciiString oldOptions = GameInfoToAsciiString(m_currentGame); // save these off for if we get booted
		if(ParseGameOptionsString(m_currentGame,options))
		{
			lanUpdateSlotList();
			updateGameOptions();
		}
		Bool booted = true;
		for(Int player = 1; player< MAX_SLOTS; player++)
		{
			if(m_currentGame->getIP(player) == m_localIP)
			{
				booted = false;
				break;
			}				
		}
		if(booted)
		{
			// restore the options with us in so we can save prefs
			ParseGameOptionsString(m_currentGame, oldOptions);
			OnPlayerLeave(m_name);
		}

	}
	else
	{
		// Check for user/host updates
		{
			AsciiString key;
			AsciiString munkee = options;
			munkee.nextToken(&key, "=");
			//DEBUG_LOG(("GameOpt request: key=%s, val=%s from player %d\n", key.str(), munkee.str(), playerSlot));

			LANGameSlot *slot = m_currentGame->getLANSlot(playerSlot);
			if (!slot)
				return;

			if (key == "User")
			{
				slot->setLogin(munkee.str()+1);
				return;
			}
			else if (key == "Host")
			{
				slot->setHost(munkee.str()+1);
				return;
			}
		}

		// Parse player requests (side, color, etc)
		if( AmIHost() && m_localIP != playerIP)
		{
			if (options.compare("HELLO") == 0)
			{
				m_currentGame->setPlayerLastHeard(playerSlot, Clock_Milliseconds());
			}
			else
			{
				m_currentGame->setPlayerLastHeard(playerSlot, Clock_Milliseconds());
				Bool change = false;
				Bool shouldUnaccept = false;
				AsciiString key;
				options.nextToken(&key, "=");
				Int val = atoi(options.str()+1);
				DEBUG_LOG(("GameOpt request: key=%s, val=%s from player %d\n", key.str(), options.str(), playerSlot));

				LANGameSlot *slot = m_currentGame->getLANSlot(playerSlot);
				if (!slot)
					return;

				if (key == "Color")
				{
					if (val >= -1 && val < TheMultiplayerSettings->getNumColors() && val != slot->getColor() && slot->getPlayerTemplate() != PLAYERTEMPLATE_OBSERVER)
					{
						Bool colorAvailable = TRUE;
						if(val != -1 )
						{
							for(Int i=0; i <MAX_SLOTS; i++)
							{
								LANGameSlot *checkSlot = m_currentGame->getLANSlot(i);
								if(val == checkSlot->getColor() && slot != checkSlot)
								{
									colorAvailable = FALSE;
									break;
								}
							}
						}
						if(colorAvailable)
							slot->setColor(val);
						change = true;
					}
					else
					{
						DEBUG_LOG(("Rejecting invalid color %d\n", val));
					}
				}
				else if (key == "PlayerTemplate")
				{
					if (val >= PLAYERTEMPLATE_MIN && val < ThePlayerTemplateStore->getPlayerTemplateCount() && val != slot->getPlayerTemplate())
					{
						slot->setPlayerTemplate(val);
						if (val == PLAYERTEMPLATE_OBSERVER)
						{
							slot->setColor(-1);
							slot->setStartPos(-1);
							slot->setTeamNumber(-1);
						}
						change = true;
						shouldUnaccept = true;
					}
					else
					{
						DEBUG_LOG(("Rejecting invalid PlayerTemplate %d\n", val));
					}
				}
				else if (key == "StartPos" && slot->getPlayerTemplate() != PLAYERTEMPLATE_OBSERVER)
				{
						
					if (val >= -1 && val < MAX_SLOTS && val != slot->getStartPos())
					{
						Bool startPosAvailable = TRUE;
						if(val != -1)
							for(Int i=0; i <MAX_SLOTS; i++)
							{
								LANGameSlot *checkSlot = m_currentGame->getLANSlot(i);
								if(val == checkSlot->getStartPos() && slot != checkSlot)
								{
									startPosAvailable = FALSE;
									break;
								}
							}
						if(startPosAvailable)
							slot->setStartPos(val);
						change = true;
						shouldUnaccept = true;
					}
					else
					{
						DEBUG_LOG(("Rejecting invalid startPos %d\n", val));
					}
				}
				else if (key == "Team")
				{
					if (val >= -1 && val < MAX_SLOTS/2 && val != slot->getTeamNumber() && slot->getPlayerTemplate() != PLAYERTEMPLATE_OBSERVER)
					{
						slot->setTeamNumber(val);
						change = true;
						shouldUnaccept = true;
					}
					else
					{
						DEBUG_LOG(("Rejecting invalid team %d\n", val));
					}
				}
				else if (key == "NAT")
				{
					if ((val >= FirewallHelperClass::FIREWALL_TYPE_SIMPLE) &&
							(val <= FirewallHelperClass::FIREWALL_TYPE_DESTINATION_PORT_DELTA))
					{
						slot->setNATBehavior((FirewallHelperClass::FirewallBehaviorType)val);
						DEBUG_LOG(("NAT behavior set to %d for player %d\n", val, playerSlot));
						change = true;
					}
					else
					{
						DEBUG_LOG(("Rejecting invalid NAT behavior %d\n", (Int)val));
					}
				}

				if (change)
				{
					if (shouldUnaccept)
						m_currentGame->resetAccepted();
					RequestGameOptions(GenerateGameOptionsString(), true);
					lanUpdateSlotList();
					DEBUG_LOG(("Slot value is color=%d, PlayerTemplate=%d, startPos=%d, team=%d\n",
						slot->getColor(), slot->getPlayerTemplate(), slot->getStartPos(), slot->getTeamNumber()));
					DEBUG_LOG(("Slot list updated to %s\n", GenerateGameOptionsString().str()));
				}
			}
		}
	}
}


/*
void LANAPI::OnSlotList( ReturnType ret, LANGameInfo *theGame )
{
	if (!theGame || theGame != m_currentGame)
		return;

	Bool foundMe = false;
	for (int player = 0; player < MAX_SLOTS; ++player)
	{
		if (m_currentGame->getIP(player) == m_localIP)
		{
			foundMe = true;
			break;
		}
	}
	if (!foundMe)
	{
		// I've been kicked - back to the lobby for me!
		// We're counting on the fact that OnPlayerLeave winds up calling reset on TheLAN.
		OnPlayerLeave(m_name);
		return;
	}

	lanUpdateSlotList();
}
*/
void LANAPI::OnPlayerJoin( Int slot, UnicodeString playerName )
{
	if (m_currentGame && m_currentGame->getIP(0) == m_localIP)
	{
		// Someone New Joined.. lets reset the accepts
		m_currentGame->resetAccepted();

		// Send out the game options
		RequestGameOptions(GenerateGameOptionsString(), true);
	}

	lanUpdateSlotList();
}

void LANAPI::OnGameJoin( ReturnType ret, LANGameInfo *theGame )
{
	if (ret == RET_OK)
	{
		LANbuttonPushed = true;
		TheShell->push( AsciiString("Menus/LanGameOptionsMenu.wnd") );
		//lanUpdateSlotList();

		LANPreferences pref;
		AsciiString options;
		options.format("PlayerTemplate=%d", pref.getPreferredFaction());
		RequestGameOptions(options, true);
		options.format("Color=%d", pref.getPreferredColor());
		RequestGameOptions(options, true);
#if TELL_COMPUTER_IDENTITY_IN_LAN_LOBBY
		options.format("User=%s", m_userName.str());
		RequestGameOptions( options, true );
		options.format("Host=%s", m_hostName.str());
		RequestGameOptions( options, true );
#endif
		options.format("NAT=%d", FirewallHelperClass::FIREWALL_TYPE_SIMPLE); // BGC: This is a LAN game, so there is no firewall.
		RequestGameOptions( options, true );
	}
	else if (ret != RET_BUSY)
	{
		/// @todo: re-enable lobby controls?  Error msgs?
		UnicodeString title, body;
		title = TheGameText->fetch("LAN:JoinFailed");
		body = getErrorStringFromReturnType(ret);
		MessageBoxOk(title, body, NULL);
	}
}

void LANAPI::OnHostLeave( void )
{
	DEBUG_ASSERTCRASH(!m_inLobby && m_currentGame, ("Game info is gone!"));
	if (m_inLobby || !m_currentGame)
		return;
	LANbuttonPushed = true;
	DEBUG_LOG(("Host left - popping to lobby\n"));
	TheShell->pop();
}

void LANAPI::OnPlayerLeave( UnicodeString player )
{
	DEBUG_ASSERTCRASH(!m_inLobby && m_currentGame, ("Game info is gone!"));
	if (m_inLobby || !m_currentGame || m_currentGame->isGameInProgress())
		return;

	if (m_name.compare(player) == 0)
	{
		// We're leaving.  Save options and Pop the shell up a screen.
		//DEBUG_ASSERTCRASH(false, ("Slot is %d\n", m_currentGame->getLocalSlotNum()));
		if (m_currentGame && m_currentGame->isInGame() && m_currentGame->getLocalSlotNum() >= 0)
		{
			LANPreferences pref;
			AsciiString option;
			option.format("%d", m_currentGame->getLANSlot( m_currentGame->getLocalSlotNum() )->getPlayerTemplate());
			pref["PlayerTemplate"] = option;
			option.format("%d", m_currentGame->getLANSlot( m_currentGame->getLocalSlotNum() )->getColor());
			pref["Color"] = option;
			if (m_currentGame->amIHost())
				pref["Map"] = AsciiStringToQuotedPrintable(m_currentGame->getMap());
			pref.write();
		}
		LANbuttonPushed = true;
		DEBUG_LOG(("OnPlayerLeave says we're leaving!  pop away!\n"));
		TheShell->pop();
	}
	else
	{
		if (m_currentGame && m_currentGame->getIP(0) == m_localIP)
		{
			// Force a new slotlist send
			m_lastResendTime = 0;
			
			lanUpdateSlotList();
			RequestGameOptions( GenerateGameOptionsString(), true );

		}
	}
}

void LANAPI::OnGameList( LANGameInfo *gameList )
{
		
	if (m_inLobby)
	{
		LANDisplayGameList(listboxGames, gameList);
	}
}//void LANAPI::OnGameList( LANGameInfo *gameList ) 

void LANAPI::OnGameCreate( ReturnType ret )
{
	if (ret == RET_OK)
	{
		// a game hosted from the Apocalypse lobby is an Apocalypse game, waves unless -apocalypse said otherwise
		if (LanLobbyApocalypse)
			m_currentGame->setApocalypseMode( TheGlobalData->m_apocalypseMode > APOCALYPSE_OFF ? TheGlobalData->m_apocalypseMode : APOCALYPSE_WAVES );

		LANbuttonPushed = true;
		TheShell->push( AsciiString("Menus/LanGameOptionsMenu.wnd") );

		RequestLobbyLeave( false );
		//RequestGameAnnounce( ); // can't do this here, since we don't have a map set
	}
	else
	{
		if(m_inLobby)
		{
			switch( ret )
			{
			case RET_GAME_EXISTS:
				GadgetListBoxAddEntryText(listboxChatWindow, TheGameText->fetch("LAN:ErrorGameExists"), chatSystemColor, -1, -1);
				break;
			case RET_BUSY:
				GadgetListBoxAddEntryText(listboxChatWindow, TheGameText->fetch("LAN:ErrorBusy"), chatSystemColor, -1, -1);
				break;
			default:
				GadgetListBoxAddEntryText(listboxChatWindow, TheGameText->fetch("LAN:ErrorUnknown"), chatSystemColor, -1, -1);
			}
		}
	}

}//void OnGameCreate( ReturnType ret )

void LANAPI::OnPlayerList( LANPlayer *playerList )
{
	if (m_inLobby)
	{
		
		UnsignedInt selectedIP = 0;
		Int selectedIndex = -1;
		Int indexToSelect = -1;
		GadgetListBoxGetSelected(listboxPlayers, &selectedIndex);
		
		if (selectedIndex != -1 )
			selectedIP = (UnsignedInt) (uintptr_t)GadgetListBoxGetItemData(listboxPlayers, selectedIndex, 0);

		GadgetListBoxReset(listboxPlayers);

		LANPlayer *player = m_lobbyPlayers;
		while (player)
		{
			Int addedIndex = GadgetListBoxAddEntryText(listboxPlayers, player->getName(), playerColor, -1, -1);
			GadgetListBoxSetItemData(listboxPlayers, (void *)(uintptr_t)player->getIP(),addedIndex, 0 );

			if (selectedIP == player->getIP())
				indexToSelect = addedIndex;

			player = player->getNext();
		}

		if (indexToSelect >= 0)
			GadgetListBoxSetSelected(listboxPlayers, indexToSelect);
	}
}

void LANAPI::OnNameChange( UnsignedInt IP, UnicodeString newName )
{
	OnPlayerList(m_lobbyPlayers);
}

void LANAPI::OnInActive(UnsignedInt IP) {
	
}

void LANAPI::OnChat( UnicodeString player, UnsignedInt ip, UnicodeString message, ChatType format )
{
	GameWindow *chatWindow = NULL;

	if (m_inLobby)
	{
		chatWindow = listboxChatWindow;
	}
	else if( m_currentGame && m_currentGame->isGameInProgress() && TheShell->isShellActive())
	{
		chatWindow = listboxChatWindowScoreScreen;
	}
	else if( m_currentGame && !m_currentGame->isGameInProgress())
	{
		chatWindow = listboxChatWindowLanGame;	
	}
	if (chatWindow == NULL)
		return;
	Int index = -1;
	UnicodeString unicodeChat;
	switch (format)
	{
		case LANAPIInterface::LANCHAT_SYSTEM:
			unicodeChat = u"";
			unicodeChat.concat(message);
			unicodeChat.concat(u"");
			index =GadgetListBoxAddEntryText(chatWindow, unicodeChat, chatSystemColor, -1, -1);
			break;
		case LANAPIInterface::LANCHAT_EMOTE:
			unicodeChat = player;
			unicodeChat.concat(u' ');
			unicodeChat.concat(message);
			if (ip == m_localIP)
				index =GadgetListBoxAddEntryText(chatWindow, unicodeChat, chatLocalActionColor, -1, -1);
			else
				index =GadgetListBoxAddEntryText(chatWindow, unicodeChat, chatActionColor, -1, -1);
			break;
		case LANAPIInterface::LANCHAT_NORMAL:
		default:
		{
			// Do the language filtering.
			TheLanguageFilter->filterLine(message);

			Color chatColor = GameMakeColor(255, 255, 255, 255);
			if (m_currentGame)
			{
				Int slotNum = m_currentGame->getSlotNum(player);
				// it'll be -1 if its invalid.
				if (slotNum >= 0) {
					GameSlot *gs = m_currentGame->getSlot(slotNum);
					if (gs) {
						Int colorIndex = gs->getColor();
						MultiplayerColorDefinition *def = TheMultiplayerSettings->getColor(colorIndex);
						if (def)
							chatColor = def->getColor();
					}
				}
			}
			
			unicodeChat = u"[";
			unicodeChat.concat(player);
			unicodeChat.concat(u"] ");
			unicodeChat.concat(message);
			if (ip == m_localIP)
				index =GadgetListBoxAddEntryText(chatWindow, unicodeChat, chatColor, -1, -1);
			else
				index =GadgetListBoxAddEntryText(chatWindow, unicodeChat, chatColor, -1, -1);
			break;
		}
	}
	GadgetListBoxSetItemData(chatWindow, (void *)-1, index);
}
