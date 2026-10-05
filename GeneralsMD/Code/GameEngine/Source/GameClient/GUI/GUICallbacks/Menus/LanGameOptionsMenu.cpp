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
// FILE: LanGameOptionsMenu.cpp
// Author: Chris Huybregts, October 2001
// Description: Lan Game Options Menu
///////////////////////////////////////////////////////////////////////////////////////
#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine


#include "Common/PlayerTemplate.h"
#include "Common/GameEngine.h"
#include "Common/UserPreferences.h"
#include "Common/QuotedPrintable.h"
#include "GameClient/AnimateWindowManager.h"
#include "GameClient/WindowLayout.h"
#include "GameClient/Gadget.h"
#include "GameClient/Shell.h"
#include "GameClient/KeyDefs.h"
#include "GameClient/GameWindowManager.h"
#include "GameClient/GadgetListBox.h"
#include "GameClient/GadgetCheckBox.h"
#include "GameClient/GadgetComboBox.h"
#include "GameClient/GadgetTextEntry.h"
#include "GameClient/GadgetStaticText.h"
#include "GameClient/GadgetPushButton.h"
#include "GameClient/GadgetCheckBox.h"
#include "GameClient/MapUtil.h"
#include "GameClient/Mouse.h"
#include "GameClient/GameWindowTransitions.h"
#include "GameClient/ChallengeGenerals.h"
#include "GameNetwork/GameSpy/LobbyUtils.h"

#include "GameNetwork/FirewallHelper.h"
#include "GameNetwork/LANAPI.h"
#include "GameNetwork/IPEnumeration.h"
#include "GameNetwork/LANAPICallbacks.h"
#include "Common/MultiplayerSettings.h"
#include "GameClient/GameText.h"
#include "GameNetwork/GUIUtil.h"

#ifdef _INTERNAL
// for occasional debugging...
//#pragma optimize("", off)
//#pragma MESSAGE("************************************** WARNING, optimization disabled for debugging purposes")
#endif

extern char *LANnextScreen;
extern Bool LANisShuttingDown;
extern Bool LANbuttonPushed;
extern void MapSelectorTooltip(GameWindow *window, WinInstanceData *instData,	UnsignedInt mouse);
extern void gameAcceptTooltip(GameWindow *window, WinInstanceData *instData, UnsignedInt mouse);
Color white = GameMakeColor( 255, 255, 255, 255 );
static bool s_isIniting = FALSE;
// window ids ------------------------------------------------------------------------------
static NameKeyType parentLanGameOptionsID = NAMEKEY_INVALID;

static NameKeyType comboBoxPlayerID[MAX_SLOTS] = { NAMEKEY_INVALID,NAMEKEY_INVALID,
																											NAMEKEY_INVALID,NAMEKEY_INVALID,
																											NAMEKEY_INVALID,NAMEKEY_INVALID,
																											NAMEKEY_INVALID,NAMEKEY_INVALID };

static NameKeyType buttonAcceptID[MAX_SLOTS] = { NAMEKEY_INVALID,NAMEKEY_INVALID,
																									NAMEKEY_INVALID,NAMEKEY_INVALID,
																									NAMEKEY_INVALID,NAMEKEY_INVALID,
																									NAMEKEY_INVALID,NAMEKEY_INVALID };

static NameKeyType comboBoxColorID[MAX_SLOTS] = { NAMEKEY_INVALID,NAMEKEY_INVALID,
																										NAMEKEY_INVALID,NAMEKEY_INVALID,
																										NAMEKEY_INVALID,NAMEKEY_INVALID,
																										NAMEKEY_INVALID,NAMEKEY_INVALID };

static NameKeyType comboBoxPlayerTemplateID[MAX_SLOTS] = { NAMEKEY_INVALID,NAMEKEY_INVALID,
																										NAMEKEY_INVALID,NAMEKEY_INVALID,
																										NAMEKEY_INVALID,NAMEKEY_INVALID,
																										NAMEKEY_INVALID,NAMEKEY_INVALID };

static NameKeyType comboBoxTeamID[MAX_SLOTS] = { NAMEKEY_INVALID,NAMEKEY_INVALID,
																										NAMEKEY_INVALID,NAMEKEY_INVALID,
																										NAMEKEY_INVALID,NAMEKEY_INVALID,
																										NAMEKEY_INVALID,NAMEKEY_INVALID };

//static NameKeyType buttonStartPositionID[MAX_SLOTS] = { NAMEKEY_INVALID,NAMEKEY_INVALID,
//																										NAMEKEY_INVALID,NAMEKEY_INVALID,
//																										NAMEKEY_INVALID,NAMEKEY_INVALID,
//																										NAMEKEY_INVALID,NAMEKEY_INVALID };

static NameKeyType buttonMapStartPositionID[MAX_SLOTS] = { NAMEKEY_INVALID,NAMEKEY_INVALID,
																										NAMEKEY_INVALID,NAMEKEY_INVALID,
																										NAMEKEY_INVALID,NAMEKEY_INVALID,
																										NAMEKEY_INVALID,NAMEKEY_INVALID };

static NameKeyType textEntryChatID = NAMEKEY_INVALID;
static NameKeyType textEntryMapDisplayID = NAMEKEY_INVALID;
static NameKeyType buttonBackID = NAMEKEY_INVALID;
static NameKeyType buttonStartID = NAMEKEY_INVALID;
static NameKeyType buttonEmoteID = NAMEKEY_INVALID;
static NameKeyType buttonSelectMapID = NAMEKEY_INVALID;
static NameKeyType comboBoxSuperweaponsID = NAMEKEY_INVALID;
static NameKeyType comboBoxStartingCashID = NAMEKEY_INVALID;
static NameKeyType comboBoxPeaceTimeID = NAMEKEY_INVALID;
static NameKeyType checkBoxUnitLimitID = NAMEKEY_INVALID;
static NameKeyType checkBoxProRulesID = NAMEKEY_INVALID;
static NameKeyType comboBoxIncomeSharingID = NAMEKEY_INVALID;
static NameKeyType comboBoxTechRespawnID = NAMEKEY_INVALID;
static NameKeyType comboBoxSupplyPileLimitID = NAMEKEY_INVALID;
static NameKeyType comboBoxGameModeID = NAMEKEY_INVALID;
static NameKeyType windowMapID = NAMEKEY_INVALID;
// Window Pointers ------------------------------------------------------------------------
static GameWindow *parentLanGameOptions = NULL;
static GameWindow *buttonBack = NULL;
static GameWindow *buttonStart = NULL;
static GameWindow *buttonSelectMap = NULL;
static GameWindow *buttonEmote = NULL;
static GameWindow *textEntryChat = NULL;
static GameWindow *textEntryMapDisplay = NULL;
static GameWindow *comboBoxSuperweapons = NULL;
static GameWindow *comboBoxStartingCash = NULL;
static GameWindow *comboBoxPeaceTime = NULL;
static GameWindow *checkBoxUnitLimit = NULL;
static GameWindow *checkBoxProRules = NULL;
static GameWindow *comboBoxIncomeSharing = NULL;
static GameWindow *comboBoxTechRespawn = NULL;
static GameWindow *comboBoxSupplyPileLimit = NULL;
static GameWindow *comboBoxGameMode = NULL;
static GameWindow *windowMap = NULL;

static GameWindow *comboBoxPlayer[MAX_SLOTS] = {NULL,NULL,NULL,NULL,
																									 NULL,NULL,NULL,NULL };
static GameWindow *buttonAccept[MAX_SLOTS] = {NULL,NULL,NULL,NULL,
																								NULL,NULL,NULL,NULL };

static GameWindow *comboBoxColor[MAX_SLOTS] = {NULL,NULL,NULL,NULL,
																								NULL,NULL,NULL,NULL };

static GameWindow *comboBoxPlayerTemplate[MAX_SLOTS] = {NULL,NULL,NULL,NULL,
																								NULL,NULL,NULL,NULL };

static GameWindow *comboBoxTeam[MAX_SLOTS] = {NULL,NULL,NULL,NULL,
																								NULL,NULL,NULL,NULL };

//static GameWindow *buttonStartPosition[MAX_SLOTS] = {NULL,NULL,NULL,NULL,
//																								NULL,NULL,NULL,NULL };
//
static GameWindow *buttonMapStartPosition[MAX_SLOTS] = {NULL,NULL,NULL,NULL,
																								NULL,NULL,NULL,NULL };

//external declarations of the Gadgets the callbacks can use
GameWindow *listboxChatWindowLanGame = NULL;
NameKeyType listboxChatWindowLanGameID = NAMEKEY_INVALID;
WindowLayout *mapSelectLayout = NULL;

static Int getNextSelectablePlayer(Int start)
{
	LANGameInfo *game = TheLAN->GetMyGame();
	if (!game->amIHost())
		return -1;
	for (Int j=start; j<MAX_SLOTS; ++j)
	{
		LANGameSlot *slot = game->getLANSlot(j);
		if (slot && slot->getStartPos() == -1 &&
			( (j==game->getLocalSlotNum() && game->getConstSlot(j)->getPlayerTemplate()!=PLAYERTEMPLATE_OBSERVER)
			|| slot->isAI()))
		{
			return j;
		}
	}
	return -1;
}

static Int getFirstSelectablePlayer(const GameInfo *game)
{
	const GameSlot *slot = game->getConstSlot(game->getLocalSlotNum());
	if (!game->amIHost() || slot && slot->getPlayerTemplate() != PLAYERTEMPLATE_OBSERVER)
		return game->getLocalSlotNum();

	for (Int i=0; i<MAX_SLOTS; ++i)
	{
		slot = game->getConstSlot(i);
		if (slot && slot->isAI())
			return i;
	}

	return game->getLocalSlotNum();
}

void updateMapStartSpots( GameInfo *myGame, GameWindow *buttonMapStartPositions[], Bool onLoadScreen = FALSE );
void positionStartSpots( GameInfo *myGame, GameWindow *buttonMapStartPositions[], GameWindow *mapWindow);
void LanPositionStartSpots( void )
{
	
	positionStartSpots( TheLAN->GetMyGame(), buttonMapStartPosition, windowMap);
}
static void playerTooltip(GameWindow *window,
													WinInstanceData *instData,
													UnsignedInt mouse)
{
	Int idx = -1;
	// i is used after the loop; VC6 for-scope let it escape.
	Int i;
	for (i =0; i<MAX_SLOTS; ++i)
	{
		if (window && window == GadgetComboBoxGetEditBox(comboBoxPlayer[i]))
		{
			idx = i;
			break;
		}
	}
	if (idx == -1)
		return;

	// was i, the loop counter above: it only escaped its scope under VC6, and the
	// loop breaks the instant it sets idx = i, so the two are the same value here.
	LANGameSlot *slot = TheLAN->GetMyGame()->getLANSlot(idx);
	if (!slot)
		return;

	LANPlayer *player = slot->getUser();
	if (!player)
	{
		DEBUG_ASSERTCRASH(TheLAN->GetMyGame()->getIP(i) == 0, ("No player info in listbox!"));
		TheMouse->setCursorTooltip( UnicodeString::TheEmptyString );
		return;
	}
	UnicodeString tooltip;
	tooltip.format(TheGameText->fetch("TOOLTIP:LANPlayer"), player->getName().str(), player->getLogin().str(), player->getHost().str());
	TheMouse->setCursorTooltip( tooltip );
}

void StartPressed(void)
{
	LANGameInfo *myGame = TheLAN->GetMyGame();

	Bool isReady = true;
	Bool allHaveMap = true;
	Int playerCount = 0;
	if (!myGame)
	{
		return;
	}
	myGame->getLANSlot(0)->setAccept(); // cause we are, of course!

	int i;

	int numUsers = 0;
	int numHumans = 0;
	for (i=0; i<MAX_SLOTS; ++i)
	{
		GameSlot *slot = myGame->getSlot(i);
		if (slot && slot->isOccupied() && slot->getPlayerTemplate() != PLAYERTEMPLATE_OBSERVER)
		{
			if (slot && slot->isHuman())
				numHumans++;
			numUsers++;
		}
	}

	// Apocalypse is every player on one team against the zombies, so one player is enough, one team
	// is the point, and the player and team counts below do not apply
	const Bool apocalypse = myGame->getApocalypseMode() != APOCALYPSE_OFF;

	// Check for too many players
	const MapMetaData *md = TheMapCache->findMap( myGame->getMap() );
	if (!md || md->m_numPlayers < numUsers)
	{
		if (TheLAN->AmIHost())
		{
			UnicodeString text;
			text.format(TheGameText->fetch("LAN:TooManyPlayers"), (md)?md->m_numPlayers:0);
			TheLAN->OnChat(UnicodeString(u"SYSTEM"), TheLAN->GetLocalIP(), text, LANAPI::LANCHAT_SYSTEM);
		}
		return;
	}

	// Check for observer + AI players.  Apocalypse needs one human seat whatever the minimum says:
	// the waves are sized by the seats, and a room of observers has none to size them by.
	if ((TheGlobalData->m_netMinPlayers || apocalypse) && !numHumans)
	{
		if (TheLAN->AmIHost())
		{
			UnicodeString text = TheGameText->fetch("GUI:NeedHumanPlayers");
			TheLAN->OnChat(UnicodeString(u"SYSTEM"), TheLAN->GetLocalIP(), text, LANAPI::LANCHAT_SYSTEM);
		}
		return;
	}

	// Check for too few players
	if (!apocalypse && numUsers < TheGlobalData->m_netMinPlayers)
	{
		if (TheLAN->AmIHost())
		{
			UnicodeString text;
			text.format(TheGameText->fetch("LAN:NeedMorePlayers"),numUsers);
			TheLAN->OnChat(UnicodeString(u"SYSTEM"), TheLAN->GetLocalIP(), text, LANAPI::LANCHAT_SYSTEM);
		}
		return;
	}

	// Check for too few teams
	int numRandom = 0;
	std::set<Int> teams; 
	for (i=0; i<MAX_SLOTS; ++i)
	{
		GameSlot *slot = myGame->getSlot(i);
		if (slot && slot->isOccupied() && slot->getPlayerTemplate() != PLAYERTEMPLATE_OBSERVER)
		{
			if (slot->getTeamNumber() >= 0)
			{
				teams.insert(slot->getTeamNumber());
			}
			else
			{
				++numRandom;
			}
		}
	}
	if (!apocalypse && numRandom + teams.size() < TheGlobalData->m_netMinPlayers)
	{
		if (TheLAN->AmIHost())
		{
			UnicodeString text;
			text.format(TheGameText->fetch("LAN:NeedMoreTeams"));
			TheLAN->OnChat(UnicodeString(u"SYSTEM"), TheLAN->GetLocalIP(), text, LANAPI::LANCHAT_SYSTEM);
		}
		return;
	}

	if (!apocalypse && numRandom + teams.size() < 2)
	{
		UnicodeString text;
		text.format(TheGameText->fetch("GUI:SandboxMode"));
			TheLAN->OnChat(UnicodeString(u"SYSTEM"), TheLAN->GetLocalIP(), text, LANAPI::LANCHAT_SYSTEM);
	}

	// see if everyone's accepted and count the number of players in the game
	UnicodeString mapDisplayName;
	const MapMetaData *mapData = TheMapCache->findMap( myGame->getMap() );
	Bool willTransfer = TRUE;
	if (mapData)
	{
		mapDisplayName.format(u"%ls", mapData->m_displayName.str());
		willTransfer = !mapData->m_isOfficial;
	}
	else
	{
		mapDisplayName.format(u"%hs", myGame->getMap().str());
		willTransfer = WouldMapTransfer(myGame->getMap());
	}
	for( i = 0; i < MAX_SLOTS; i++ )
	{
		LANGameSlot *slot = myGame->getLANSlot(i);
		if( slot->isHuman() && !slot->isAccepted())
		{
			isReady = false;
			if (!willTransfer)
			{
				if (!slot->hasMap())
				{
					UnicodeString msg;
					msg.format(TheGameText->fetch("GUI:PlayerNoMap"), slot->getName().str(), mapDisplayName.str());
					GadgetListBoxAddEntryText(listboxChatWindowLanGame, msg , chatSystemColor, -1, 0);
					allHaveMap = false;
				}
			}
		}
		if( slot->isHuman() && slot->getPlayerTemplate() != PLAYERTEMPLATE_OBSERVER )
			playerCount++;
	}

	if(isReady)
	{
		for( i = 0; i < MAX_SLOTS; i++ )
		{
			LANGameSlot *slot = myGame->getLANSlot(i);
			if (slot && slot->isOpen())
			{
				slot->setState( SLOT_CLOSED );
				GadgetComboBoxSetSelectedPos(comboBoxPlayer[i], SLOT_CLOSED);
			}
		}
		Int seconds = TheMultiplayerSettings->getStartCountdownTimerSeconds();
		if (seconds)
			TheLAN->RequestGameStartTimer(seconds);
		else
			TheLAN->RequestGameStart();
		LANEnableStartButton(false);
	}
	else
	{
		// Does everyone have the map?
		if (allHaveMap)
		{
			GadgetListBoxAddEntryText(listboxChatWindowLanGame, TheGameText->fetch("GUI:NotifiedStartIntent") , chatSystemColor, -1, 0);
			TheLAN->RequestAccept();
		}
	}

}//void StartPressed(void)

void LANEnableStartButton(Bool enabled)
{
	buttonStart->winEnable(enabled);
	buttonSelectMap->winEnable(enabled);
}

// The countdown's last second: shutting the menu down clears every gadget pointer below, and a
// click that lands after that dereferences one of them.
void LANDisableButtons()
{
	buttonStart->winEnable(false);
	buttonBack->winEnable(false);
	buttonSelectMap->winEnable(false);
	comboBoxStartingCash->winEnable(false);
	comboBoxGameMode->winEnable(false);

	GameWindow *optionalGadgets[] = { comboBoxSuperweapons, comboBoxPeaceTime, checkBoxUnitLimit,
		checkBoxProRules, comboBoxIncomeSharing, comboBoxTechRespawn, comboBoxSupplyPileLimit };
	for (Int i = 0; i < (Int)ARRAY_SIZE(optionalGadgets); ++i)
	{
		if (optionalGadgets[i])
			optionalGadgets[i]->winEnable(false);
	}

	for (Int i = 0; i < MAX_SLOTS; ++i)
	{
		comboBoxPlayer[i]->winEnable(false);
		comboBoxColor[i]->winEnable(false);
		comboBoxPlayerTemplate[i]->winEnable(false);
		comboBoxTeam[i]->winEnable(false);
		buttonAccept[i]->winEnable(false);
		buttonMapStartPosition[i]->winEnable(false);
	}
}

static void handleColorSelection(int index)
{
	GameWindow *combo = comboBoxColor[index];
	Int color, selIndex;
	GadgetComboBoxGetSelectedPos(combo, &selIndex);
	color = (Int)(intptr_t)GadgetComboBoxGetItemData(combo, selIndex);

	LANGameInfo *myGame = TheLAN->GetMyGame();

	if (myGame)
	{
		LANGameSlot * slot = myGame->getLANSlot(index);
		if (color == slot->getColor())
			return;

		if (color >= -1 && color < TheMultiplayerSettings->getNumColors())
		{
			Bool colorAvailable = TRUE;
			if(color != -1 )
			{
				for(Int i=0; i <MAX_SLOTS; i++)
				{
					LANGameSlot *checkSlot = myGame->getLANSlot(i);
					if(color == checkSlot->getColor() && slot != checkSlot)
					{
						colorAvailable = FALSE;
						break;
					}
				}
			}
			if(!colorAvailable)
				return;
		}

		slot->setColor(color);

		if (myGame->amIHost())
		{
			if (!s_isIniting)
			{
				// send around a new slotlist
				TheLAN->RequestGameOptions(GenerateGameOptionsString(), true);
				lanUpdateSlotList();
			}
		}
		else
		{
			// request the color from the host
			if (!slot->isLocalPlayer() || !AreSlotListUpdatesEnabled())
				return;

			AsciiString options;
			options.format("Color=%d", color);
			TheLAN->RequestGameOptions(options, true);
		}
	}
}

static void handlePlayerTemplateSelection(int index)
{
	GameWindow *combo = comboBoxPlayerTemplate[index];
	Int playerTemplate, selIndex;
	GadgetComboBoxGetSelectedPos(combo, &selIndex);
	playerTemplate = (Int)(intptr_t)GadgetComboBoxGetItemData(combo, selIndex);
	LANGameInfo *myGame = TheLAN->GetMyGame();

	if (myGame)
	{
		LANGameSlot * slot = myGame->getLANSlot(index);
		if (playerTemplate == slot->getPlayerTemplate())
			return;

		Int oldTemplate = slot->getPlayerTemplate();
		slot->setPlayerTemplate(playerTemplate);

		if (oldTemplate == PLAYERTEMPLATE_OBSERVER)
		{
			// was observer, so populate color & team with all, and enable
			GadgetComboBoxSetSelectedPos(comboBoxColor[index], 0);
			GadgetComboBoxSetSelectedPos(comboBoxTeam[index], 0);
			slot->setStartPos(-1);
		}
		else if (playerTemplate == PLAYERTEMPLATE_OBSERVER)
		{
			// is becoming observer, so populate color & team with random only, and disable
			GadgetComboBoxSetSelectedPos(comboBoxColor[index], 0);
			GadgetComboBoxSetSelectedPos(comboBoxTeam[index], 0);
			slot->setStartPos(-1);
		}

		myGame->resetAccepted();

		if (myGame->amIHost())
		{
			if (!s_isIniting)
			{
				// send around a new slotlist
				TheLAN->RequestGameOptions(GenerateGameOptionsString(), true);
				lanUpdateSlotList();
			}
		}
		else
		{
			// request the playerTemplate from the host
			if (AreSlotListUpdatesEnabled())
			{
				AsciiString options;
				options.format("PlayerTemplate=%d", playerTemplate);
				TheLAN->RequestGameOptions(options, true);
			}
		}
	}
}

static void handleStartPositionSelection(Int player, int startPos)
{
	LANGameInfo *myGame = TheLAN->GetMyGame();
	
	if (myGame)
	{
		LANGameSlot * slot = myGame->getLANSlot(player);
		if (startPos == slot->getStartPos())
			return;
		Bool skip = FALSE;
		if (startPos < 0)
		{
			skip = TRUE;
		}

		if(!skip)
		{	
			Bool isAvailable = TRUE;
			for(Int i = 0; i < MAX_SLOTS; ++i)
			{
				if(i != player && myGame->getSlot(i)->getStartPos() == startPos)
				{
					isAvailable = FALSE;
					break;
				}
			}
			if( !isAvailable )
				return;
		}
		slot->setStartPos(startPos);

		if (myGame->amIHost())
		{
			if (!s_isIniting)
			{
				// send around a new slotlist
				myGame->resetAccepted();
				TheLAN->RequestGameOptions(GenerateGameOptionsString(), true);
				lanUpdateSlotList();
			}
		}
		else
		{
			// request the color from the host
			if (AreSlotListUpdatesEnabled())
			{
				AsciiString options;
				options.format("StartPos=%d", slot->getStartPos());
				TheLAN->RequestGameOptions(options, true);
			}
		}
	}
}



static void handleTeamSelection(int index)
{
	GameWindow *combo = comboBoxTeam[index];
	Int team, selIndex;
	GadgetComboBoxGetSelectedPos(combo, &selIndex);
	team = (Int)(intptr_t)GadgetComboBoxGetItemData(combo, selIndex);
	LANGameInfo *myGame = TheLAN->GetMyGame();

	if (myGame)
	{
		LANGameSlot * slot = myGame->getLANSlot(index);
		if (team == slot->getTeamNumber())
			return;

		slot->setTeamNumber(team);
		myGame->resetAccepted();

		if (myGame->amIHost())
		{
			if (!s_isIniting)
			{
				// send around a new slotlist
				myGame->resetAccepted();
				TheLAN->RequestGameOptions(GenerateGameOptionsString(), true);
				lanUpdateSlotList();
			}
		}
		else
		{
			// request the team from the host
			if (AreSlotListUpdatesEnabled())
			{
				AsciiString options;
				options.format("Team=%d", team);
				TheLAN->RequestGameOptions(options, true);
			}
		}
	}
}

static void handleStartingCashSelection()
{
  LANGameInfo *myGame = TheLAN->GetMyGame();
  
  if (myGame)
  {
    Int selIndex;
    GadgetComboBoxGetSelectedPos(comboBoxStartingCash, &selIndex);

    Money startingCash;
    startingCash.deposit( (UnsignedInt)(uintptr_t)GadgetComboBoxGetItemData( comboBoxStartingCash, selIndex ), FALSE );
    myGame->setStartingCash( startingCash );
    myGame->resetAccepted();

    if (myGame->amIHost())
    {
      if (!s_isIniting)
      {
        // send around the new data
        TheLAN->RequestGameOptions(GenerateGameOptionsString(), true);
        lanUpdateSlotList(); // Update the accepted button UI
      }
    }
  }
}

static void handlePeaceTimeSelection()
{
  LANGameInfo *myGame = TheLAN->GetMyGame();

  if (myGame && comboBoxPeaceTime)
  {
    myGame->setPeaceTime( PeaceTimeFromComboBox( comboBoxPeaceTime ) );
    myGame->resetAccepted();

    if (myGame->amIHost() && !s_isIniting)
    {
      TheLAN->RequestGameOptions(GenerateGameOptionsString(), true);
      lanUpdateSlotList(); // Update the accepted button UI
    }
  }
}

static void handleIncomeSharingSelection()
{
  LANGameInfo *myGame = TheLAN->GetMyGame();

  // setting the box from the host's options selects it too; only a real change is worth a new options string
  if (myGame == NULL || comboBoxIncomeSharing == NULL || myGame->getIncomeSharing() == IncomeSharingFromComboBox( comboBoxIncomeSharing ))
    return;

  myGame->setIncomeSharing( IncomeSharingFromComboBox( comboBoxIncomeSharing ) );
  myGame->resetAccepted();

  if (myGame->amIHost() && !s_isIniting)
  {
    TheLAN->RequestGameOptions(GenerateGameOptionsString(), true);
    lanUpdateSlotList(); // Update the accepted button UI
  }
}

static void handleTechRespawnSelection()
{
  LANGameInfo *myGame = TheLAN->GetMyGame();

  // the same guard as income sharing above
  if (myGame == NULL || comboBoxTechRespawn == NULL || myGame->getTechRespawn() == TechRespawnFromComboBox( comboBoxTechRespawn ))
    return;

  myGame->setTechRespawn( TechRespawnFromComboBox( comboBoxTechRespawn ) );
  myGame->resetAccepted();

  if (myGame->amIHost() && !s_isIniting)
  {
    TheLAN->RequestGameOptions(GenerateGameOptionsString(), true);
    lanUpdateSlotList(); // Update the accepted button UI
  }
}

static void handleSupplyPileLimitSelection()
{
  LANGameInfo *myGame = TheLAN->GetMyGame();

  // the same guard as income sharing above
  if (myGame == NULL || comboBoxSupplyPileLimit == NULL || myGame->getSupplyPileLimit() == SupplyPileLimitFromComboBox( comboBoxSupplyPileLimit ))
    return;

  myGame->setSupplyPileLimit( SupplyPileLimitFromComboBox( comboBoxSupplyPileLimit ) );
  myGame->resetAccepted();

  if (myGame->amIHost() && !s_isIniting)
  {
    TheLAN->RequestGameOptions(GenerateGameOptionsString(), true);
    lanUpdateSlotList(); // Update the accepted button UI
  }
}

// What an Apocalypse game changes on this screen: its title, the mode box, no peace time (the zombie
// side is a computer side, and a computer side turns peace time off anyway) and no team column
// (everybody is on team 1).  The mode can change under a client at any time, so both ways are set.
static void showApocalypseGadgets( LANGameInfo *myGame )
{
  const Bool apocalypse = myGame->getApocalypseMode() != APOCALYPSE_OFF;

  GadgetStaticTextSetText( TheWindowManager->winGetWindowFromId( parentLanGameOptions, TheNameKeyGenerator->nameToKey( "LanGameOptionsMenu.wnd:StaticTextTitle" ) ),
    TheGameText->fetch( apocalypse ? "GUI:Apocalypse" : "GUI:GameOptions" ) );
  UpdateGameModeComboBox( comboBoxGameMode, myGame, TheLAN->AmIHost() );

  const char *hidden[] = { "LabelPeaceTime", "ComboBoxPeaceTime", "StaticTextTeam" };
  for ( Int i = 0; i < (Int)ARRAY_SIZE(hidden); ++i )
  {
    AsciiString name;
    name.format( "LanGameOptionsMenu.wnd:%s", hidden[i] );
    TheWindowManager->winGetWindowFromId( parentLanGameOptions, TheNameKeyGenerator->nameToKey( name ) )->winHide( apocalypse );
  }
  for ( Int i = 0; i < MAX_SLOTS; ++i )
    comboBoxTeam[i]->winHide( apocalypse );
}

static void handleGameModeSelection()
{
  LANGameInfo *myGame = TheLAN->GetMyGame();

  // emptying the box deselects it, and that must not read as a mode; setting it from the host's
  // options selects it too, and only a real change is worth a new options string
  const Int mode = GameModeFromComboBox( comboBoxGameMode );
  if ( mode < 0 || myGame->getApocalypseMode() == mode )
    return;

  myGame->setApocalypseMode( mode );
  myGame->resetAccepted();
  showApocalypseGadgets( myGame );

  if (myGame->amIHost() && !s_isIniting)
  {
    TheLAN->RequestGameOptions(GenerateGameOptionsString(), true);
    lanUpdateSlotList(); // Update the accepted button UI
  }
}

static void handleProRulesSelection()
{
  LANGameInfo *myGame = TheLAN->GetMyGame();

  // the same guard as the unit limit box below
  if (myGame == NULL || checkBoxProRules == NULL || myGame->getProRules() == GadgetCheckBoxIsChecked( checkBoxProRules ))
    return;

  myGame->setProRules( GadgetCheckBoxIsChecked( checkBoxProRules ) );
  myGame->resetAccepted();

  if (myGame->amIHost() && !s_isIniting)
  {
    TheLAN->RequestGameOptions(GenerateGameOptionsString(), true);
    lanUpdateSlotList(); // Update the accepted button UI
  }
}

static void handleUnitLimitSelection()
{
  LANGameInfo *myGame = TheLAN->GetMyGame();

  // setting the box from the host's options clicks it too; only a real change is worth a new options string
  if (myGame == NULL || checkBoxUnitLimit == NULL || myGame->getUnitLimit() == GadgetCheckBoxIsChecked( checkBoxUnitLimit ))
    return;

  myGame->setUnitLimit( GadgetCheckBoxIsChecked( checkBoxUnitLimit ) );
  myGame->resetAccepted();

  if (myGame->amIHost() && !s_isIniting)
  {
    TheLAN->RequestGameOptions(GenerateGameOptionsString(), true);
    lanUpdateSlotList(); // Update the accepted button UI
  }
}

static void handleSuperweaponSelection()
{
  LANGameInfo *myGame = TheLAN->GetMyGame();

  if (myGame && comboBoxSuperweapons)
  {
    myGame->setSuperweaponRestriction( SuperweaponRestrictionFromComboBox( comboBoxSuperweapons ) );
    myGame->resetAccepted();

    if (myGame->amIHost())
    {
      if (!s_isIniting)
      {
        // send around a new slotlist
        TheLAN->RequestGameOptions(GenerateGameOptionsString(), true);
        lanUpdateSlotList(); // Update the accepted button UI
      }
    }
  }
}

void lanUpdateSlotList( void )
{
	if(!AreSlotListUpdatesEnabled() || s_isIniting)
		return;
	UpdateSlotList( TheLAN->GetMyGame(), comboBoxPlayer, comboBoxColor,
		comboBoxPlayerTemplate, comboBoxTeam, buttonAccept, buttonStart, buttonMapStartPosition);
	
	updateMapStartSpots(TheLAN->GetMyGame(), buttonMapStartPosition);
}

//-------------------------------------------------------------------------------------------------
/** Initialize the Gadgets Options Menu */
//-------------------------------------------------------------------------------------------------
void InitLanGameGadgets( void )
{
	//Initialize the gadget IDs
	parentLanGameOptionsID = TheNameKeyGenerator->nameToKey( AsciiString( "LanGameOptionsMenu.wnd:LanGameOptionsMenuParent" ) );
	buttonBackID = TheNameKeyGenerator->nameToKey( AsciiString( "LanGameOptionsMenu.wnd:ButtonBack" ) );
	buttonStartID = TheNameKeyGenerator->nameToKey( AsciiString( "LanGameOptionsMenu.wnd:ButtonStart" ) );
	textEntryChatID = TheNameKeyGenerator->nameToKey( AsciiString( "LanGameOptionsMenu.wnd:TextEntryChat" ) );
	textEntryMapDisplayID = TheNameKeyGenerator->nameToKey( AsciiString( "LanGameOptionsMenu.wnd:TextEntryMapDisplay" ) );
	listboxChatWindowLanGameID = TheNameKeyGenerator->nameToKey( AsciiString( "LanGameOptionsMenu.wnd:ListboxChatWindowLanGame" ) );
	buttonEmoteID = TheNameKeyGenerator->nameToKey( AsciiString( "LanGameOptionsMenu.wnd:ButtonEmote" ) );
	buttonSelectMapID = TheNameKeyGenerator->nameToKey( AsciiString( "LanGameOptionsMenu.wnd:ButtonSelectMap" ) );
  comboBoxSuperweaponsID = TheNameKeyGenerator->nameToKey( AsciiString( "LanGameOptionsMenu.wnd:ComboBoxSuperweapons" ) );
  comboBoxStartingCashID = TheNameKeyGenerator->nameToKey( AsciiString( "LanGameOptionsMenu.wnd:ComboBoxStartingCash" ) );
  comboBoxPeaceTimeID = TheNameKeyGenerator->nameToKey( AsciiString( "LanGameOptionsMenu.wnd:ComboBoxPeaceTime" ) );
  checkBoxUnitLimitID = TheNameKeyGenerator->nameToKey( AsciiString( "LanGameOptionsMenu.wnd:CheckBoxUnitLimit" ) );
  checkBoxProRulesID = TheNameKeyGenerator->nameToKey( AsciiString( "LanGameOptionsMenu.wnd:CheckBoxProRules" ) );
  comboBoxIncomeSharingID = TheNameKeyGenerator->nameToKey( AsciiString( "LanGameOptionsMenu.wnd:ComboBoxIncomeSharing" ) );
  comboBoxTechRespawnID = TheNameKeyGenerator->nameToKey( AsciiString( "LanGameOptionsMenu.wnd:ComboBoxTechRespawn" ) );
  comboBoxSupplyPileLimitID = TheNameKeyGenerator->nameToKey( AsciiString( "LanGameOptionsMenu.wnd:ComboBoxSupplyPileLimit" ) );
  comboBoxGameModeID = TheNameKeyGenerator->nameToKey( AsciiString( "LanGameOptionsMenu.wnd:ComboBoxGameMode" ) );
	windowMapID = TheNameKeyGenerator->nameToKey( AsciiString( "LanGameOptionsMenu.wnd:MapWindow" ) );

	// Initialize the pointers to our gadgets
	parentLanGameOptions = TheWindowManager->winGetWindowFromId( NULL, parentLanGameOptionsID );
	DEBUG_ASSERTCRASH(parentLanGameOptions, ("Could not find the parentLanGameOptions"));
	buttonEmote = TheWindowManager->winGetWindowFromId( parentLanGameOptions,buttonEmoteID  );
	DEBUG_ASSERTCRASH(buttonEmote, ("Could not find the buttonEmote"));
	buttonSelectMap = TheWindowManager->winGetWindowFromId( parentLanGameOptions,buttonSelectMapID  );
	DEBUG_ASSERTCRASH(buttonSelectMap, ("Could not find the buttonSelectMap"));
	buttonStart = TheWindowManager->winGetWindowFromId( parentLanGameOptions,buttonStartID  );
	DEBUG_ASSERTCRASH(buttonStart, ("Could not find the buttonStart"));
	buttonBack = TheWindowManager->winGetWindowFromId( parentLanGameOptions,  buttonBackID);
	DEBUG_ASSERTCRASH(buttonBack, ("Could not find the buttonBack"));
	listboxChatWindowLanGame = TheWindowManager->winGetWindowFromId( parentLanGameOptions, listboxChatWindowLanGameID );
	DEBUG_ASSERTCRASH(listboxChatWindowLanGame, ("Could not find the listboxChatWindowLanGame"));
	textEntryChat = TheWindowManager->winGetWindowFromId( parentLanGameOptions, textEntryChatID );
	DEBUG_ASSERTCRASH(textEntryChat, ("Could not find the textEntryChat"));
	textEntryMapDisplay = TheWindowManager->winGetWindowFromId( parentLanGameOptions, textEntryMapDisplayID );
	DEBUG_ASSERTCRASH(textEntryMapDisplay, ("Could not find the textEntryMapDisplay"));
  comboBoxSuperweapons = TheWindowManager->winGetWindowFromId( parentLanGameOptions, comboBoxSuperweaponsID );
  DEBUG_ASSERTCRASH(comboBoxSuperweapons, ("Could not find the comboBoxSuperweapons"));
	if (comboBoxSuperweapons)
		PopulateSuperweaponComboBox(comboBoxSuperweapons, TheLAN->GetMyGame(), TheLAN->AmIHost());
  comboBoxStartingCash = TheWindowManager->winGetWindowFromId( parentLanGameOptions, comboBoxStartingCashID );
  DEBUG_ASSERTCRASH(comboBoxStartingCash, ("Could not find the comboBoxStartingCash"));
	PopulateStartingCashComboBox(comboBoxStartingCash, TheLAN->GetMyGame());
	// this one is the fork's own control, and it lives in the fork's copy of the layout under
	// Run/Window - a stale Run/ has EA's file, which does not carry it.  Missing is survivable
	// (peace time then reads as off), a null dereference in Release is not.
  comboBoxPeaceTime = TheWindowManager->winGetWindowFromId( parentLanGameOptions, comboBoxPeaceTimeID );
  DEBUG_ASSERTCRASH(comboBoxPeaceTime, ("Could not find the comboBoxPeaceTime"));
	if (comboBoxPeaceTime)
		PopulatePeaceTimeComboBox(comboBoxPeaceTime, TheLAN->GetMyGame(), TheLAN->AmIHost());
	// the fork's own control too, and missing is survivable the same way
  checkBoxUnitLimit = TheWindowManager->winGetWindowFromId( parentLanGameOptions, checkBoxUnitLimitID );
  DEBUG_ASSERTCRASH(checkBoxUnitLimit, ("Could not find the checkBoxUnitLimit"));
	if (checkBoxUnitLimit)
		UpdateUnitLimitCheckBox(checkBoxUnitLimit, TheLAN->GetMyGame(), TheLAN->AmIHost());
  checkBoxProRules = TheWindowManager->winGetWindowFromId( parentLanGameOptions, checkBoxProRulesID );
  DEBUG_ASSERTCRASH(checkBoxProRules, ("Could not find the checkBoxProRules"));
	if (checkBoxProRules)
		UpdateProRulesCheckBox(checkBoxProRules, TheLAN->GetMyGame(), TheLAN->AmIHost());
  comboBoxIncomeSharing = TheWindowManager->winGetWindowFromId( parentLanGameOptions, comboBoxIncomeSharingID );
  DEBUG_ASSERTCRASH(comboBoxIncomeSharing, ("Could not find the comboBoxIncomeSharing"));
	if (comboBoxIncomeSharing)
		PopulateIncomeSharingComboBox(comboBoxIncomeSharing, TheLAN->GetMyGame(), TheLAN->AmIHost());
  comboBoxTechRespawn = TheWindowManager->winGetWindowFromId( parentLanGameOptions, comboBoxTechRespawnID );
  DEBUG_ASSERTCRASH(comboBoxTechRespawn, ("Could not find the comboBoxTechRespawn"));
	if (comboBoxTechRespawn)
		PopulateTechRespawnComboBox(comboBoxTechRespawn, TheLAN->GetMyGame(), TheLAN->AmIHost());
  comboBoxSupplyPileLimit = TheWindowManager->winGetWindowFromId( parentLanGameOptions, comboBoxSupplyPileLimitID );
  DEBUG_ASSERTCRASH(comboBoxSupplyPileLimit, ("Could not find the comboBoxSupplyPileLimit"));
	if (comboBoxSupplyPileLimit)
		PopulateSupplyPileLimitComboBox(comboBoxSupplyPileLimit, TheLAN->GetMyGame(), TheLAN->AmIHost());
	// hidden in the layout, as in the skirmish room's; here every game has a mode
	comboBoxGameMode = TheWindowManager->winGetWindowFromId( parentLanGameOptions, comboBoxGameModeID );
	comboBoxGameMode->winHide(FALSE);
	PopulateGameModeComboBox(comboBoxGameMode, APOCALYPSE_OFF, TheLAN->GetMyGame(), TheLAN->AmIHost());

	windowMap = TheWindowManager->winGetWindowFromId( parentLanGameOptions,windowMapID  );
	DEBUG_ASSERTCRASH(windowMap, ("Could not find the LanGameOptionsMenu.wnd:MapWindow" ));

	Int localSlotNum = TheLAN->GetMyGame()->getLocalSlotNum();
	DEBUG_ASSERTCRASH(localSlotNum >= 0, ("Bad slot number!"));

	//Added By Sadullah Nader
	//Tooltip function is being set for techBuildings, and supplyDocks

	windowMap->winSetTooltipFunc(MapSelectorTooltip);
	//End Add

	for (Int i = 0; i < MAX_SLOTS; i++)
	{
		AsciiString tmpString;
		tmpString.format("LanGameOptionsMenu.wnd:ComboBoxPlayer%d", i);
		comboBoxPlayerID[i] = TheNameKeyGenerator->nameToKey( tmpString );
		comboBoxPlayer[i] = TheWindowManager->winGetWindowFromId( parentLanGameOptions, comboBoxPlayerID[i] );
		GadgetComboBoxReset(comboBoxPlayer[i]);
		GadgetComboBoxGetEditBox(comboBoxPlayer[i])->winSetTooltipFunc(playerTooltip);

		if(localSlotNum == i)
		{
			GadgetComboBoxAddEntry(comboBoxPlayer[i],TheLAN->GetMyName(),white);
		}
		else
		{
			PopulatePlayerSlotComboBox(comboBoxPlayer[i], white, FALSE);
		}
		/*
		if(i != 0)
		{
			TheLAN->GetMyGame()->getLANSlot(i)->setState(SLOT_OPEN);
		}
		*/

		tmpString.format("LanGameOptionsMenu.wnd:ComboBoxColor%d", i);
		comboBoxColorID[i] = TheNameKeyGenerator->nameToKey( tmpString );
		comboBoxColor[i] = TheWindowManager->winGetWindowFromId( parentLanGameOptions, comboBoxColorID[i] );
		DEBUG_ASSERTCRASH(comboBoxColor[i], ("Could not find the comboBoxColor[%d]",i ));
		PopulateColorComboBox(i, comboBoxColor, TheLAN->GetMyGame());
		GadgetComboBoxSetSelectedPos(comboBoxColor[i], 0);
		
		tmpString.format("LanGameOptionsMenu.wnd:ComboBoxPlayerTemplate%d", i);
		comboBoxPlayerTemplateID[i] = TheNameKeyGenerator->nameToKey( tmpString );
		comboBoxPlayerTemplate[i] = TheWindowManager->winGetWindowFromId( parentLanGameOptions, comboBoxPlayerTemplateID[i] );
		DEBUG_ASSERTCRASH(comboBoxPlayerTemplate[i], ("Could not find the comboBoxPlayerTemplate[%d]",i ));
		PopulatePlayerTemplateComboBox(i, comboBoxPlayerTemplate, TheLAN->GetMyGame(), TRUE);

		// add tooltips to the player template combobox and listbox
		comboBoxPlayerTemplate[i]->winSetTooltipFunc(playerTemplateComboBoxTooltip);
		GadgetComboBoxGetListBox(comboBoxPlayerTemplate[i])->winSetTooltipFunc(playerTemplateListBoxTooltip);

		tmpString.format("LanGameOptionsMenu.wnd:ComboBoxTeam%d", i);
		comboBoxTeamID[i] = TheNameKeyGenerator->nameToKey( tmpString );
		comboBoxTeam[i] = TheWindowManager->winGetWindowFromId( parentLanGameOptions, comboBoxTeamID[i] );
		DEBUG_ASSERTCRASH(comboBoxTeam[i], ("Could not find the comboBoxTeam[%d]",i ));
		PopulateTeamComboBox(i, comboBoxTeam, TheLAN->GetMyGame());

		tmpString.clear();
		tmpString.format("LanGameOptionsMenu.wnd:ButtonAccept%d", i); 
		buttonAcceptID[i] = TheNameKeyGenerator->nameToKey( tmpString );
		buttonAccept[i] = TheWindowManager->winGetWindowFromId( parentLanGameOptions, buttonAcceptID[i] );
		DEBUG_ASSERTCRASH(buttonAccept[i], ("Could not find the buttonAccept[%d]",i ));
		//Added by Saad for the tooltips on the MultiPlayer icons
		buttonAccept[i]->winSetTooltipFunc(gameAcceptTooltip);
//		
//		tmpString.format("LanGameOptionsMenu.wnd:ButtonStartPosition%d", i);
//		buttonStartPositionID[i] = TheNameKeyGenerator->nameToKey( tmpString );
//		buttonStartPosition[i] = TheWindowManager->winGetWindowFromId( parentLanGameOptions, buttonStartPositionID[i] );
//		DEBUG_ASSERTCRASH(buttonStartPosition[i], ("Could not find the ButtonStartPosition[%d]",i ));

		tmpString.format("LanGameOptionsMenu.wnd:ButtonMapStartPosition%d", i);
		buttonMapStartPositionID[i] = TheNameKeyGenerator->nameToKey( tmpString );
		buttonMapStartPosition[i] = TheWindowManager->winGetWindowFromId( parentLanGameOptions, buttonMapStartPositionID[i] );
		DEBUG_ASSERTCRASH(buttonMapStartPosition[i], ("Could not find the ButtonMapStartPosition[%d]",i ));

		if(i !=0 && buttonAccept[i])
			buttonAccept[i]->winHide(TRUE);
	}
	if( buttonAccept[0] )
		GadgetButtonSetEnabledColor(buttonAccept[0], acceptTrueColor );

	showApocalypseGadgets( TheLAN->GetMyGame() );

	// the settings page and the chat log share one rectangle; this opens on the chat log
	InitLobbyTabs( parentLanGameOptions, "LanGameOptionsMenu.wnd", "TabChat", "ListboxChatWindowLanGame" );
}

void DeinitLanGameGadgets( void )
{
	ShutdownLobbyTabs();
	parentLanGameOptions = NULL;
	buttonEmote = NULL;
	buttonSelectMap = NULL;
	buttonStart = NULL;
	buttonBack = NULL;
	listboxChatWindowLanGame = NULL;
	textEntryChat = NULL;
	textEntryMapDisplay = NULL;
  comboBoxSuperweapons = NULL;
  comboBoxStartingCash = NULL;
  comboBoxPeaceTime = NULL;
  checkBoxUnitLimit = NULL;
  checkBoxProRules = NULL;
  comboBoxIncomeSharing = NULL;
  comboBoxTechRespawn = NULL;
  comboBoxSupplyPileLimit = NULL;
  comboBoxGameMode = NULL;
	windowMap = NULL;
	for (Int i = 0; i < MAX_SLOTS; i++)
	{
		comboBoxPlayer[i] = NULL;
		comboBoxColor[i] = NULL;
		comboBoxPlayerTemplate[i] = NULL;
		comboBoxTeam[i] = NULL;
		buttonAccept[i] = NULL;
//		buttonStartPosition[i] = NULL;
		buttonMapStartPosition[i] = NULL;
	}
}

//-------------------------------------------------------------------------------------------------
/** Initialize the Lan Game Options Menu */
//-------------------------------------------------------------------------------------------------
void LanGameOptionsMenuInit( WindowLayout *layout, void *userData )
{
	if (TheLAN->GetMyGame() && TheLAN->GetMyGame()->isGameInProgress())
	{
		// If we init while the game is in progress, we are really returning to the menu
		// after the game.  So, we pop the menu and go back to the lobby.  Whee!
		DEBUG_LOG(("Popping to lobby after a game!\n"));
		TheShell->popImmediate();
		return;
	}
	s_isIniting = TRUE;

	LANbuttonPushed = false;
	LANisShuttingDown = false;

	//initialize the gadgets
	EnableSlotListUpdates(FALSE);
	InitLanGameGadgets();
	EnableSlotListUpdates(TRUE);
	Int start = 0;

	// Make sure the text fields are clear
	GadgetListBoxReset( listboxChatWindowLanGame );
	GadgetTextEntrySetText(textEntryChat, UnicodeString::TheEmptyString);	

	//The dialog needs to react differently depending on whether it's the host or not.
	TheMapCache->updateCache();
	if (TheLAN->AmIHost())
	{
		// read in some prefs
		LANGameInfo *game = TheLAN->GetMyGame();
		LANGameSlot *slot = game->getLANSlot(0);
		LANPreferences pref;
		slot->setColor( pref.getPreferredColor() );
		slot->setPlayerTemplate( pref.getPreferredFaction() );
		slot->setNATBehavior(FirewallHelperClass::FIREWALL_TYPE_SIMPLE);
		game->setMap( pref.getPreferredMap() );
    game->setStartingCash( pref.getStartingCash() );
    game->setSuperweaponRestriction( pref.getSuperweaponRestriction() );
    game->setUnitLimit( pref.getInt( "UnitLimit", 0 ) != 0 );
    game->setProRules( pref.getInt( "ProRules", 1 ) != 0 );
    game->setIncomeSharing( pref.getInt( "IncomeSharing", INCOME_SHARING_OFF ) );
    game->setTechRespawn( pref.getInt( "TechRespawn", 0 ) );
    game->setSupplyPileLimit( pref.getInt( "SupplyPileLimit", 0 ) );
		AsciiString lowerMap = pref.getPreferredMap();
		lowerMap.toLower();
		std::map<AsciiString, MapMetaData>::iterator it = TheMapCache->find(lowerMap);
		if (it != TheMapCache->end())
		{
			TheLAN->GetMyGame()->getSlot(0)->setMapAvailability(true);
			TheLAN->GetMyGame()->setMapCRC( it->second.m_CRC );
			TheLAN->GetMyGame()->setMapSize( it->second.m_filesize );

			TheLAN->GetMyGame()->adjustSlotsForMap(); // BGC- adjust the slots for the selected map.
		}

		//GadgetTextEntrySetText(comboBoxPlayer[0], TheLAN->GetMyName());
		lanUpdateSlotList();
		updateGameOptions();
		start = 1; // leave my combo boxes usable
	}
	else
	{

		//DEBUG_LOG(("LanGameOptionsMenuInit(): map is %s\n", TheLAN->GetMyGame()->getMap().str()));
		buttonStart->winSetText(TheGameText->fetch("GUI:Accept"));
		buttonSelectMap->winEnable( FALSE );
    comboBoxStartingCash->winEnable( FALSE );      // Can look but only host can touch
    // the superweapon box is left to UpdateSuperweaponComboBox, for the same reason as peace time
    // the peace time box is left to UpdatePeaceTimeComboBox: being a client is only one of the two
    // reasons it can be dead, and one place deciding beats two disagreeing
		TheLAN->GetMyGame()->setMapCRC( TheLAN->GetMyGame()->getMapCRC() );		// force a recheck
		TheLAN->GetMyGame()->setMapSize( TheLAN->GetMyGame()->getMapSize() ); // of if we have the map
		TheLAN->RequestHasMap();
		lanUpdateSlotList();
		updateGameOptions();
	}
	for (Int i = start; i < MAX_SLOTS; ++i)
	{
		//I'm a client, disable the controls I can't touch.
		if (!TheLAN->AmIHost())
			comboBoxPlayer[i]->winEnable(FALSE);

		comboBoxColor[i]->winEnable(FALSE);
		comboBoxPlayerTemplate[i]->winEnable(FALSE);
		comboBoxTeam[i]->winEnable(FALSE);
//		buttonStartPosition[i]->winEnable(FALSE);
	}

//	for (i = 0; i < MAX_SLOTS; ++i)
//	{
//		if (buttonStartPosition[i])
//			buttonStartPosition[i]->winHide(TRUE); // not picking start spots this way any more
//	}
//
	// Show the Menu
	layout->hide( FALSE );
	
	// Set Keyboard to Main Parent
	TheWindowManager->winSetFocus( parentLanGameOptions );

	s_isIniting = FALSE;

	if (TheLAN->AmIHost())
	{
		TheLAN->RequestGameOptions(GenerateGameOptionsString(),true);
		TheLAN->RequestGameAnnounce();
	}
	lanUpdateSlotList();
	LanPositionStartSpots();
	TheTransitionHandler->setGroup("LanGameOptionsFade");

	// animate controls
	//TheShell->registerWithAnimateManager(buttonBack, WIN_ANIMATION_SLIDE_RIGHT, TRUE, 1);
	
}// void LanGameOptionsMenuInit( WindowLayout *layout, void *userData )

//-------------------------------------------------------------------------------------------------
/** Update options on screen */
//-------------------------------------------------------------------------------------------------
void updateGameOptions( void )
{
	LANGameInfo *theGame = TheLAN->GetMyGame();
	UnicodeString mapDisplayName;
	if (theGame && AreSlotListUpdatesEnabled())
	{
		const GameSlot *localSlot = theGame->getConstSlot(theGame->getLocalSlotNum());
		const MapMetaData *mapData = TheMapCache->findMap( TheLAN->GetMyGame()->getMap() );
		if (mapData && localSlot && localSlot->hasMap())
		{
			mapDisplayName.format(u"%ls", mapData->m_displayName.str());
		}
		else
		{
			AsciiString s = TheLAN->GetMyGame()->getMap();
			if (s.reverseFind('\\'))
			{
				s = s.reverseFind('\\') + 1;
			}
			mapDisplayName.format(u"%hs", s.str());
		}
		UnicodeString old = GadgetStaticTextGetText(textEntryMapDisplay);
		if(old.compare(mapDisplayName) != 0)
			LanPositionStartSpots();
		GadgetStaticTextSetText(textEntryMapDisplay, mapDisplayName);

		if (comboBoxSuperweapons)
			UpdateSuperweaponComboBox( comboBoxSuperweapons, theGame, TheLAN->AmIHost() );
		Int itemCount = GadgetComboBoxGetLength(comboBoxStartingCash);
    Int index;
    for ( index = 0; index < itemCount; index++ )
    {
      Int value  = (Int)(intptr_t)GadgetComboBoxGetItemData(comboBoxStartingCash, index);
      if ( value == theGame->getStartingCash().countMoney() )
      {
        GadgetComboBoxSetSelectedPos(comboBoxStartingCash, index, TRUE);
        break;
      }
    }

    // the preferences set the cash after the box was filled, so an amount the list does not carry
    // (StartingCash in the player's INI) was in force but never shown
    if ( index == itemCount )
      PopulateStartingCashComboBox( comboBoxStartingCash, theGame );

		if (comboBoxPeaceTime)
			UpdatePeaceTimeComboBox( comboBoxPeaceTime, theGame, TheLAN->AmIHost() );
		if (checkBoxUnitLimit)
			UpdateUnitLimitCheckBox( checkBoxUnitLimit, theGame, TheLAN->AmIHost() );
		if (checkBoxProRules)
			UpdateProRulesCheckBox( checkBoxProRules, theGame, TheLAN->AmIHost() );
		if (comboBoxIncomeSharing)
			UpdateIncomeSharingComboBox( comboBoxIncomeSharing, theGame, TheLAN->AmIHost() );
		if (comboBoxTechRespawn)
			UpdateTechRespawnComboBox( comboBoxTechRespawn, theGame, TheLAN->AmIHost() );
		if (comboBoxSupplyPileLimit)
			UpdateSupplyPileLimitComboBox( comboBoxSupplyPileLimit, theGame, TheLAN->AmIHost() );
		showApocalypseGadgets( theGame );
	}
}



//-------------------------------------------------------------------------------------------------
/** This is called when a shutdown is complete for this menu */
//-------------------------------------------------------------------------------------------------
static void shutdownComplete( WindowLayout *layout )
{
	DeinitLanGameGadgets();
	textEntryMapDisplay = NULL;
	LANisShuttingDown = false;

	// hide the layout
	layout->hide( TRUE );

	// our shutdown is complete
	TheShell->shutdownComplete( layout, (LANnextScreen != NULL) );

	if (LANnextScreen != NULL)
	{
		TheShell->push(LANnextScreen);
	}

	LANnextScreen = NULL;

}  // end if

//-------------------------------------------------------------------------------------------------
/** Lan Game Options menu shutdown method */
//-------------------------------------------------------------------------------------------------
void LanGameOptionsMenuShutdown( WindowLayout *layout, void *userData )
{
	TheMouse->setCursor(Mouse::ARROW);
	TheMouse->setMouseText(UnicodeString::TheEmptyString,NULL,NULL);
	EnableSlotListUpdates(FALSE);
	LANisShuttingDown = true;

	// if we are shutting down for an immediate pop, skip the animations
	Bool popImmediate = *(Bool *)userData;
	if( popImmediate )
	{

		shutdownComplete( layout );
		return;

	}  //end if

	TheShell->reverseAnimatewindow();
	TheTransitionHandler->reverse("LanGameOptionsFade");
	if (TheLAN)
		TheLAN->ResetGameStartTimer();

	/*
	// hide menu
	layout->hide( TRUE );

	// Reset the LAN singleton
//	TheLAN->reset();

	// our shutdown is complete
	TheShell->shutdownComplete( layout );
	*/
}  // void LanGameOptionsMenuShutdown( WindowLayout *layout, void *userData )

//-------------------------------------------------------------------------------------------------
/** Lan Game Options menu update method */
//-------------------------------------------------------------------------------------------------
void LanGameOptionsMenuUpdate( WindowLayout * layout, void *userData)
{
	if(LANisShuttingDown && TheShell->isAnimFinished() && TheTransitionHandler->isFinished())
		shutdownComplete(layout);
	//TheLAN->update(); // this is handled in the lobby
}// void LanGameOptionsMenuUpdate( WindowLayout * layout, void *userData)

//-------------------------------------------------------------------------------------------------
/** Lan Game Options menu input callback */
//-------------------------------------------------------------------------------------------------
WindowMsgHandledType LanGameOptionsMenuInput( GameWindow *window, UnsignedInt msg,
																			 WindowMsgData mData1, WindowMsgData mData2 )
{
	switch( msg ) 
	{

		// --------------------------------------------------------------------------------------------
		case GWM_CHAR:
		{
			UnsignedByte key = mData1;
			UnsignedByte state = mData2;
			if (LANbuttonPushed)
				break;

			switch( key )
			{
				// ----------------------------------------------------------------------------------------
				case KEY_ESC:
				{
					//
					// send a simulated selected event to the parent window of the
					// back/exit button
					//
					if( BitTest( state, KEY_STATE_UP ) )
					{
						TheWindowManager->winSendSystemMsg( window, GBM_SELECTED, 
																							(WindowMsgData)buttonBack, buttonBackID );
					}  // end if
					// don't let key fall through anywhere else
					return MSG_HANDLED;
				}  // end escape
			}  // end switch( key )
		}  // end char
	}  // end switch( msg )
	return MSG_IGNORED;
}//WindowMsgHandledType LanGameOptionsMenuInput( GameWindow *window, UnsignedInt msg,


//-------------------------------------------------------------------------------------------------
/** Lan Game Options menu window system callback */
//-------------------------------------------------------------------------------------------------
WindowMsgHandledType LanGameOptionsMenuSystem( GameWindow *window, UnsignedInt msg, 
														 WindowMsgData mData1, WindowMsgData mData2 )
{
	UnicodeString txtInput;
	switch( msg )
	{
		//-------------------------------------------------------------------------------------------------	
		case GWM_CREATE:
			{
				break;
			} // case GWM_DESTROY:
		//-------------------------------------------------------------------------------------------------
		case GWM_DESTROY:
			{
				break;
			} // case GWM_DESTROY:
		//-------------------------------------------------------------------------------------------------
		case GWM_INPUT_FOCUS:
			{	
				// if we're givin the opportunity to take the keyboard focus we must say we want it
				if( mData1 == TRUE )
					*(Bool *)mData2 = TRUE;

				return MSG_HANDLED;
			}//case GWM_INPUT_FOCUS:
		//-------------------------------------------------------------------------------------------------
		case GCM_SELECTED:
			{
				if (LANbuttonPushed)
					break;
				GameWindow *control = (GameWindow *)mData1;
				Int controlID = control->winGetWindowId();
				LANGameInfo *myGame = TheLAN->GetMyGame();

        if ( controlID == comboBoxStartingCashID )
        {
          handleStartingCashSelection();
        }
        else if ( controlID == comboBoxPeaceTimeID )
        {
          handlePeaceTimeSelection();
        }
        else if ( controlID == comboBoxSuperweaponsID )
        {
          handleSuperweaponSelection();
        }
        else if ( controlID == comboBoxIncomeSharingID )
        {
          handleIncomeSharingSelection();
        }
        else if ( controlID == comboBoxTechRespawnID )
        {
          handleTechRespawnSelection();
        }
        else if ( controlID == comboBoxSupplyPileLimitID )
        {
          handleSupplyPileLimitSelection();
        }
        else if ( controlID == comboBoxGameModeID )
        {
          handleGameModeSelection();
        }
        else
        {
				  for (Int i = 0; i < MAX_SLOTS; i++)
				  {
					  if (controlID == comboBoxColorID[i])
					  {
						  handleColorSelection(i);
					  }
					  else if (controlID == comboBoxPlayerTemplateID[i])
					  {
						  handlePlayerTemplateSelection(i);
					  }
					  else if (controlID == comboBoxTeamID[i])
					  {
						  handleTeamSelection(i);
					  }
					  else if( controlID == comboBoxPlayerID[i] && myGame->amIHost() )
					  {
						  // We don't have anything that'll happen if we click on ourselves
						  if(i == myGame->getLocalSlotNum())
						   break;
						  // Get
						  Int pos = -1;
						  GadgetComboBoxGetSelectedPos(comboBoxPlayer[i], &pos);
						  SlotState pickedState = (pos >= 0) ? (SlotState)(Int)(intptr_t)GadgetComboBoxGetItemData(comboBoxPlayer[i], pos) : SLOT_OPEN;
						  if( pos >= 0 )
						  {
							  if( myGame->getLANSlot(i)->getState() == SLOT_PLAYER )
							  {
								  UnicodeString name = myGame->getPlayerName(i);
								  myGame->getLANSlot(i)->setState(pickedState);
								  myGame->resetAccepted();
								  TheLAN->OnPlayerLeave(name);
							  }
							  else if( myGame->getLANSlot(i)->getState() != pickedState )
							  {
								  Bool wasAI = (myGame->getLANSlot(i)->isAI());
								  myGame->getLANSlot(i)->setState(pickedState);
								  Bool isAI = (myGame->getLANSlot(i)->isAI());
								  if (wasAI || isAI)
									  myGame->resetAccepted();
								  if (wasAI ^ isAI)
									  PopulatePlayerTemplateComboBox(i, comboBoxPlayerTemplate, myGame, wasAI);
								  if (!s_isIniting)
								  {
									  TheLAN->RequestGameOptions(GenerateGameOptionsString(), true);
									  lanUpdateSlotList();
								  }
							  }
						  }
              break;
            }
          }
				}
        break;
			}// case GCM_SELECTED:
		//-------------------------------------------------------------------------------------------------
		case GBM_SELECTED:
			{
				if (LANbuttonPushed)
					break;
				GameWindow *control = (GameWindow *)mData1;
				Int controlID = control->winGetWindowId();

				if ( controlID == checkBoxUnitLimitID )
				{
					handleUnitLimitSelection();
					break;
				}

				if ( controlID == checkBoxProRulesID )
				{
					handleProRulesSelection();
					break;
				}

				if ( LobbyTabClicked( (NameKeyType)controlID ) )
					break;

				if ( controlID == buttonBackID )
				{
					if( mapSelectLayout )
						{
							mapSelectLayout->destroyWindows();
							mapSelectLayout->deleteInstance();
							mapSelectLayout = NULL;
						}
					TheLAN->RequestGameLeave();
					//TheShell->pop();

				} //if ( controlID == buttonBack )				
				else if ( controlID == buttonEmoteID )
				{
					// read the user's input
					txtInput.set(GadgetTextEntryGetText( textEntryChat ));
					// Clear the text entry line
					GadgetTextEntrySetText(textEntryChat, UnicodeString::TheEmptyString);
					// Clean up the text (remove leading/trailing chars, etc)
					txtInput.trim();
					// Echo the user's input to the chat window
					if (!txtInput.isEmpty())
						TheLAN->RequestChat(txtInput, LANAPIInterface::LANCHAT_EMOTE);
				} //if ( controlID == buttonEmote )
				else if ( controlID == buttonSelectMapID )
				{
					//buttonBack->winEnable( false );
				
					mapSelectLayout = TheWindowManager->winCreateLayout( AsciiString( "Menus/LanMapSelectMenu.wnd" ) );
					mapSelectLayout->runInit();
					mapSelectLayout->hide( FALSE );
					mapSelectLayout->bringForward();

				}
				else if ( controlID == buttonStartID )
				{
					if (TheLAN->AmIHost())
					{
						StartPressed();
						//TheLAN->RequestGameStart();
					}
					else
					{
						//I'm the Client... send an accept message to the host.
						TheLAN->RequestAccept();

						// Disable the accept button
						EnableAcceptControls(TRUE, TheLAN->GetMyGame(), comboBoxPlayer, comboBoxColor, comboBoxPlayerTemplate,
							comboBoxTeam, buttonAccept, buttonStart, buttonMapStartPosition);
						
					}
				}
				else
				{
					for (Int i = 0; i < MAX_SLOTS; i++)
					{
						if (controlID == buttonMapStartPositionID[i])
						{
							LANGameInfo *game = TheLAN->GetMyGame();
							Int playerIdxInPos = -1;
							for (Int j=0; j<MAX_SLOTS; ++j)
							{
								LANGameSlot *slot = game->getLANSlot(j);
								if (slot && slot->getStartPos() == i)
								{
									playerIdxInPos = j;
									break;
								}
							}
							if (playerIdxInPos >= 0)
							{
								LANGameSlot *slot = game->getLANSlot(playerIdxInPos);
								if (playerIdxInPos == game->getLocalSlotNum() || (game->amIHost() && slot && slot->isAI()))
								{
									// it's one of my type.  Try to change it.
									Int nextPlayer = getNextSelectablePlayer(playerIdxInPos+1);
									handleStartPositionSelection(playerIdxInPos, -1);
									if (nextPlayer >= 0)
									{
										handleStartPositionSelection(nextPlayer, i);
									}
								}
							}
							else
							{
								// nobody in the slot - put us in
								Int nextPlayer = getNextSelectablePlayer(0);
								if (nextPlayer < 0)
									nextPlayer = getFirstSelectablePlayer(game);
								handleStartPositionSelection(nextPlayer, i);
							}
						}
					}
				}

				break;
			}// case GBM_SELECTED:
		//-------------------------------------------------------------------------------------------------
		case GBM_SELECTED_RIGHT:
		{
			if (LANbuttonPushed)
				break;

			GameWindow *control = (GameWindow *)mData1;
			Int controlID = control->winGetWindowId();
			for (Int i = 0; i < MAX_SLOTS; i++)
			{
				if (controlID == buttonMapStartPositionID[i])
				{
					LANGameInfo *game = TheLAN->GetMyGame();
					Int playerIdxInPos = -1;
					for (Int j=0; j<MAX_SLOTS; ++j)
					{
						LANGameSlot *slot = game->getLANSlot(j);
						if (slot && slot->getStartPos() == i)
						{
							playerIdxInPos = j;
							break;
						}
					}
					if (playerIdxInPos >= 0)
					{
						LANGameSlot *slot = game->getLANSlot(playerIdxInPos);
						if (playerIdxInPos == game->getLocalSlotNum() || (game->amIHost() && slot && slot->isAI()))
						{
							// it's one of my type.  Remove it.
							handleStartPositionSelection(playerIdxInPos, -1);
						}
					}
				}
			}					
			break;
		}
		//-------------------------------------------------------------------------------------------------
		case GEM_EDIT_DONE:
			{
				if (LANbuttonPushed)
					break;
				GameWindow *control = (GameWindow *)mData1;
				Int controlID = control->winGetWindowId();

				// Take the user's input and echo it into the chat window as well as
				// send it to the other clients on the lan
				if ( controlID == textEntryChatID )
				{
					
					// read the user's input
					txtInput.set(GadgetTextEntryGetText( textEntryChat ));
					// Clear the text entry line
					GadgetTextEntrySetText(textEntryChat, UnicodeString::TheEmptyString);
					// Clean up the text (remove leading/trailing chars, etc)
					txtInput.trim();
					// Echo the user's input to the chat window
					if (!txtInput.isEmpty())
						TheLAN->RequestChat(txtInput, LANAPIInterface::LANCHAT_NORMAL);

				}// if ( controlID == textEntryChatID )
				break;
			}
		//-------------------------------------------------------------------------------------------------
		default:
			return MSG_IGNORED;
	}//Switch
	return MSG_HANDLED;
}//WindowMsgHandledType LanGameOptionsMenuSystem( GameWindow *window, UnsignedInt msg, 

//-------------------------------------------------------------------------------------------------
/** Utility FUnction used as a bridge from other windows to this one */
//-------------------------------------------------------------------------------------------------
void PostToLanGameOptions( PostToLanGameType post )
{
	if (post >= POST_TO_LAN_GAME_TYPE_COUNT)
		return;
	LanPositionStartSpots();
	switch (post)
	{
			//-------------------------------------------------------------------------------------------------
		case SEND_GAME_OPTS:
		{
			LANGameInfo *game = TheLAN->GetMyGame();
			game->resetAccepted();			
			updateGameOptions();		
			lanUpdateSlotList();
			
			//buttonBack->winEnable( true );
			
			for(Int i = 0; i < MAX_SLOTS; ++i)
			{
				game->getSlot(i)->setStartPos(-1);
			}

			TheLAN->RequestGameOptions(GenerateGameOptionsString(), true);
			break;
		}		//-------------------------------------------------------------------------------------------------
		case MAP_BACK:
		{
				//buttonBack->winEnable( true );
		}
		default:
			return;
	}
}
