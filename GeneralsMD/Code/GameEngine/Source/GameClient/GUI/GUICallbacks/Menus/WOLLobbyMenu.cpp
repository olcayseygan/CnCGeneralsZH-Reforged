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
// FILE: WOLLobbyMenu.cpp
// Author: Chris Huybregts, November 2001
// Description: WOL Lobby Menu
///////////////////////////////////////////////////////////////////////////////////////

// INCLUDES ///////////////////////////////////////////////////////////////////////////////////////
#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine
#include "Lib/Clock.h"

#include "Common/GameEngine.h"
#include "Common/GameState.h"
#include "Common/MiniLog.h"
#include "Common/MultiplayerSettings.h"
#include "Common/PlayerTemplate.h"
#include "Common/CustomMatchPreferences.h"
#include "Common/version.h"
#include "GameClient/AnimateWindowManager.h"
#include "GameClient/WindowLayout.h"
#include "GameClient/Gadget.h"
#include "GameClient/GameClient.h"
#include "GameClient/Shell.h"
#include "GameClient/ShellHooks.h"
#include "GameClient/KeyDefs.h"
#include "GameClient/GameWindowManager.h"
#include "GameClient/GadgetComboBox.h"
#include "GameClient/GadgetListBox.h"
#include "GameClient/GadgetSlider.h"
#include "GameClient/GadgetTextEntry.h"
#include "GameClient/GameText.h"
#include "GameClient/MessageBox.h"
#include "GameClient/Mouse.h"
#include "GameClient/Display.h"
#include "GameClient/DisplayString.h"
#include "GameClient/DisplayStringManager.h"
#include "GameClient/UIStyle.h"
#include "GameNetwork/GameSpyOverlay.h"
#include "GameNetwork/Online/OnlineQueues.h"
#include "Lib/WideCharFns.h"
#include "GameClient/GameWindowTransitions.h"

#include "GameLogic/GameLogic.h"

#include "GameClient/LanguageFilter.h"
#include "GameNetwork/GameSpy/BuddyDefs.h"
#include "GameNetwork/GameSpy/GSConfig.h"
#include "GameNetwork/GameSpy/LadderDefs.h"
#include "GameNetwork/GameSpy/PeerDefs.h"
#include "GameNetwork/GameSpy/PeerThread.h"
#include "GameNetwork/GameSpy/ThreadUtils.h"
#include "GameNetwork/GameSpy/PersistentStorageDefs.h"
#include "GameNetwork/GameSpy/PersistentStorageThread.h"
#include "GameNetwork/GameSpy/LobbyUtils.h"
#include "GameNetwork/RankPointValue.h"
#include "GameNetwork/GameDataMatch.h"

#include <algorithm>
#include <vector>

// Which half of the room's pair differs from ours: a build the player updates, or data files he can put back
static const char *joinRefusalLabel( const GameSpyStagingRoom *room )
{
	if (!room)
		return "GUI:JoinFailedCRCMismatch";
	switch (compareGameData(room->getExeCRC(), room->getIniCRC(), TheGlobalData->m_exeCRC, TheGlobalData->m_iniCRC))
	{
	case GAMEDATA_EXE_DIFFERS:	return "GUI:JoinFailedDifferentBuild";
	case GAMEDATA_INI_DIFFERS:	return "GUI:JoinFailedDifferentData";
	default:					return "GUI:JoinFailedCRCMismatch";
	}
}

#ifdef _INTERNAL
// for occasional debugging...
//#pragma optimize("", off)
//#pragma MESSAGE("************************************** WARNING, optimization disabled for debugging purposes")
#endif
static void refreshGameList( Bool forceRefresh = FALSE );
static void refreshPlayerList( Bool forceRefresh = FALSE );

#ifdef DEBUG_LOGGING
#define PERF_TEST
static LogClass s_perfLog("Perf.txt");
#define PERF_LOG(x) s_perfLog.log x
#else // DEBUG_LOGGING
#define PERF_LOG(x) {}
#endif // DEBUG_LOGGING

// PRIVATE DATA ///////////////////////////////////////////////////////////////////////////////////
static Bool isShuttingDown = false;
static Bool buttonPushed = false;
static const char *nextScreen = NULL;
static Bool raiseMessageBoxes = false;
static time_t gameListRefreshTime = 0;
static const time_t gameListRefreshInterval = 10000;
static time_t playerListRefreshTime = 0;
static const time_t playerListRefreshInterval = 5000;

void setUnignoreText( WindowLayout *layout, AsciiString nick, GPProfile id);
static void doSliderTrack(GameWindow *control, Int val);
Bool DontShowMainMenu = FALSE;
/* The player list's cells (drawPlayerRow draws them): the nick as shown, the tier, the ladder rating,
	 the ping light, whose image is a row high, and the whole nick in a column of no width, which is what
	 the right-click menu and the tooltip look a player up by. */
enum { PLAYER_NICK, PLAYER_TIER, PLAYER_RATING, PLAYER_LIGHT, PLAYER_KEY, PLAYER_COLUMNS };
// the mockup's 190, 136, 104 and 58 of a 488 list
static const Int PLAYER_COLUMN_PERCENT[ PLAYER_COLUMNS ] = { 39, 28, 21, 12, 0 };
static const Real PLAYER_ROW_HEIGHT = 36.0f;		///< at 1080
static const Real LOBBY_PAD = 10.0f;

// window ids ------------------------------------------------------------------------------
static NameKeyType parentWOLLobbyID = NAMEKEY_INVALID;
static NameKeyType buttonBackID = NAMEKEY_INVALID;
static NameKeyType buttonHostID = NAMEKEY_INVALID;
static NameKeyType buttonRefreshID = NAMEKEY_INVALID;
static NameKeyType buttonJoinID = NAMEKEY_INVALID;
static NameKeyType buttonBuddyID = NAMEKEY_INVALID;
static NameKeyType buttonEmoteID = NAMEKEY_INVALID;
static NameKeyType textEntryChatID = NAMEKEY_INVALID;
static NameKeyType listboxLobbyPlayersID = NAMEKEY_INVALID;
static NameKeyType listboxLobbyChatID = NAMEKEY_INVALID;
static NameKeyType comboLobbyGroupRoomsID = NAMEKEY_INVALID;
//static NameKeyType // sliderChatAdjustID = NAMEKEY_INVALID;

// Window Pointers ------------------------------------------------------------------------
static GameWindow *parentWOLLobby = NULL;
static GameWindow *buttonBack = NULL;
static GameWindow *buttonHost = NULL;
static GameWindow *buttonRefresh = NULL;
static GameWindow *buttonJoin = NULL;
static GameWindow *buttonBuddy = NULL;
static GameWindow *buttonEmote = NULL;
static GameWindow *textEntryChat = NULL;
static GameWindow *listboxLobbyPlayers = NULL;
static GameWindow *listboxLobbyChat = NULL;
static GameWindow *comboLobbyGroupRooms = NULL;
static GameWindow *parent = NULL;

static Int groupRoomToJoin = 0;
static Int	initialGadgetDelay = 2;
static Bool justEntered = FALSE;

#if defined(_INTERNAL) || defined(_DEBUG)
Bool g_fakeCRC = FALSE;
Bool g_debugSlots = FALSE;
#endif

std::list<PeerResponse> TheLobbyQueuedUTMs;

// Slash commands -------------------------------------------------------------------------
extern "C" {
int getQR2HostingStatus(void);
}
extern int isThreadHosting;

Bool handleLobbySlashCommands(UnicodeString uText)
{
	AsciiString message;
	message.translate(uText);

	if (message.getCharAt(0) != '/')
	{
		return FALSE; // not a slash command
	}

	AsciiString remainder = message.str() + 1;
	AsciiString token;
	remainder.nextToken(&token);
	token.toLower();

	if (token == "host")
	{
		UnicodeString s;
		s.format(u"Hosting qr2:%d thread:%d", getQR2HostingStatus(), isThreadHosting);
		TheGameSpyInfo->addText(s, GameSpyColor[GSCOLOR_DEFAULT], NULL);
		return TRUE; // was a slash command
	}
	else if (token == "me" && uText.getLength()>4)
	{
		TheGameSpyInfo->sendChat(UnicodeString(uText.str()+4), TRUE, listboxLobbyPlayers);
		return TRUE; // was a slash command
	}
	else if (token == "refresh")
	{
		// Added 2/19/03 added the game refresh
		refreshGameList(TRUE);	
		refreshPlayerList(TRUE);
		return TRUE; // was a slash command
	}
	/*
	if (token == "togglegamelist")
	{
		NameKeyType buttonID = NAMEKEY("WOLCustomLobby.wnd:ButtonGameListToggle");
		GameWindow *button = TheWindowManager->winGetWindowFromId(parent, buttonID);
		if (button)
		{
			button->winHide(!button->winIsHidden());
		}
		return TRUE; // was a slash command
	}
	else if (token == "adjustchat")
	{
		NameKeyType sliderID = NAMEKEY("WOLCustomLobby.wnd:SliderChatAdjust");
		GameWindow *slider = TheWindowManager->winGetWindowFromId(parent, sliderID);
		if (slider)
		{
			slider->winHide(!slider->winIsHidden());
		}
		return TRUE; // was a slash command
	}
	*/
#if defined(_INTERNAL) || defined(_DEBUG)
	else if (token == "fakecrc")
	{
		g_fakeCRC = !g_fakeCRC;
		TheGameSpyInfo->addText(UnicodeString(u"Toggled CRC fakery"), GameSpyColor[GSCOLOR_DEFAULT], NULL);
		return TRUE; // was a slash command
	}
	else if (token == "slots")
	{
		g_debugSlots = !g_debugSlots;
		TheGameSpyInfo->addText(UnicodeString(u"Toggled SlotList debug"), GameSpyColor[GSCOLOR_DEFAULT], NULL);
		return TRUE; // was a slash command
	}
#endif

	return FALSE; // not a slash command
}

static Bool s_tryingToHostOrJoin = FALSE;
void SetLobbyAttemptHostJoin(Bool start)
{
	s_tryingToHostOrJoin = start;
	if (!start)
		LobbySetJoiningGame( 0 );		// the join came back, or the password box was cancelled
}

/* -online scripting in the lobby, once per process each: -onlinesay says its line once somebody else is in
	 the room to read it, -onlinehost asks for a staging room the way the host popup's createGame does (no
	 password, no observers, unrated, no ladder) three seconds after that line so the answer still reaches
	 the lobby, -onlinejoin joins the first listed game of that name the way the Join button does. */
static void onlineLobbyScript( void )
{
	static Bool s_said = FALSE, s_hosted = FALSE, s_joined = FALSE;
	static UnsignedInt s_saidAt = 0;
	if (!OnlineIsEnabled() || !TheGameSpyInfo->getCurrentGroupRoom() || s_tryingToHostOrJoin)
		return;

	const Bool saying = TheGlobalData->m_onlineSay.isNotEmpty();
	if (saying && !s_said && TheGameSpyInfo->getPlayerInfoMap()->size() >= 2)
	{
		s_said = TRUE;
		s_saidAt = Clock_Milliseconds();
		// straight to the queue: sendChat would drop the staging room's copy as a repeat of this one
		UnicodeString line( MultiByteToWideCharSingleLine( TheGlobalData->m_onlineSay.str() ).c_str() );
		PeerRequest req;
		req.peerRequestType = PeerRequest::PEERREQUEST_MESSAGEROOM;
		req.text = line.str();
		req.message.isAction = FALSE;
		TheGameSpyPeerMessageQueue->addRequest( req );
		DEBUG_LOG(("ONLINE SAY where=lobby text=%s\n", TheGlobalData->m_onlineSay.str()));
	}

	if (!s_hosted && TheGlobalData->m_onlineHostGame.isNotEmpty()
		&& (!saying || (s_said && Clock_Milliseconds() - s_saidAt >= 3000)))
	{
		s_hosted = TRUE;
		/* The Host button, then the host popup's name and Create Game, as a player clicks them, so the room
			 is made under the popup's own rules.  Building the request here once hid that the popup made
			 every online room a stats room, with every option locked for its host. */
		GameWindow *popupName = NULL, *popupCreate = NULL, *popup = NULL;
		if (buttonHost)
		{
			TheWindowManager->winSendSystemMsg( parent, GBM_SELECTED, (WindowMsgData)buttonHost, buttonHostID );
			popup = TheWindowManager->winGetWindowFromId( NULL, NAMEKEY( "PopupHostGame.wnd:ParentHostPopUp" ) );
			popupName = TheWindowManager->winGetWindowFromId( popup, NAMEKEY( "PopupHostGame.wnd:TextEntryGameName" ) );
			popupCreate = TheWindowManager->winGetWindowFromId( popup, NAMEKEY( "PopupHostGame.wnd:ButtonCreateGame" ) );
		}
		if (!popup || !popupName || !popupCreate)
		{
			DEBUG_LOG(("ONLINE HOST fail popup=%d\n", popup != NULL));
			return;
		}
		UnicodeString gameName;
		gameName.translate( TheGlobalData->m_onlineHostGame );
		GadgetTextEntrySetText( popupName, gameName );
		TheWindowManager->winSendSystemMsg( popupCreate->winGetOwner(), GBM_SELECTED, (WindowMsgData)popupCreate, popupCreate->winGetWindowId() );
		DEBUG_LOG(("ONLINE HOST name=%s stats=%d\n", TheGlobalData->m_onlineHostGame.str(), (Int)TheGameSpyGame->getUseStats()));
		return;
	}

	if (!s_joined && TheGlobalData->m_onlineJoinGame.isNotEmpty())
	{
		UnicodeString wanted;
		wanted.translate( TheGlobalData->m_onlineJoinGame );
		StagingRoomMap *srm = TheGameSpyInfo->getStagingRoomList();
		for (StagingRoomMap::iterator it = srm->begin(); it != srm->end(); ++it)
		{
			GameSpyStagingRoom *room = it->second;
			if (!room || room->getGameName().compare( wanted ) != 0)
				continue;
			s_joined = TRUE;
			TheLobbyQueuedUTMs.clear();
			groupRoomToJoin = TheGameSpyInfo->getCurrentGroupRoom();
			TheGameSpyInfo->markAsStagingRoomJoiner( it->first );
			TheGameSpyGame->setGameName( room->getGameName() );
			TheGameSpyGame->setLadderIP( room->getLadderIP() );
			TheGameSpyGame->setLadderPort( room->getLadderPort() );
			SetLobbyAttemptHostJoin( TRUE );
			LobbySetJoiningGame( it->first );
			PeerRequest req;
			req.peerRequestType = PeerRequest::PEERREQUEST_JOINSTAGINGROOM;
			req.text = room->getGameName().str();
			req.stagingRoom.id = it->first;
			req.password = "";
			TheGameSpyPeerMessageQueue->addRequest( req );
			DEBUG_LOG(("ONLINE JOIN name=%s id=%d\n", TheGlobalData->m_onlineJoinGame.str(), it->first));
			break;
		}
	}
}

// Tooltips -------------------------------------------------------------------------------

/// the whole nick of a player-list row (the shown one may be cut with an ellipsis)
static AsciiString nickInRow( GameWindow *listbox, Int row )
{
	AsciiString nick;
	nick.translate( GadgetListBoxGetText( listbox, row, PLAYER_KEY ) );
	return nick;
}

static void playerTooltip(GameWindow *window,
													WinInstanceData *instData,
													UnsignedInt mouse)
{
	Int x, y, row, col;
	x = LOLONGTOSHORT(mouse);
	y = HILONGTOSHORT(mouse);

	GadgetListBoxGetEntryBasedOnXY(window, x, y, row, col);

	if (row == -1 || col == -1)
	{
		TheMouse->setCursorTooltip( UnicodeString::TheEmptyString);//TheGameText->fetch("TOOLTIP:PlayersInLobby") );
		return;
	}

	AsciiString aName = nickInRow(window, row);
	UnicodeString uName;
	uName.translate(aName);

	PlayerInfoMap::iterator it = TheGameSpyInfo->getPlayerInfoMap()->find(aName);
	if (it == TheGameSpyInfo->getPlayerInfoMap()->end())
	{
		TheMouse->setCursorTooltip( UnicodeString::TheEmptyString );
		return;
	}
	PlayerInfo *info = &(it->second);
	Bool isLocalPlayer = (TheGameSpyInfo->getLocalName().compareNoCase(info->m_name) == 0);

	AsciiString	playerLocale = info->m_locale;
	AsciiString localeIdentifier;
	localeIdentifier.format("WOL:Locale%2.2d", atoi(playerLocale.str()));
	Int					playerWins   = info->m_wins;
	Int					playerLosses = info->m_losses;
	UnicodeString	playerInfo;
	if (OnlineIsEnabled())		// no locale on this service: EA's "Region: Unknown" goes
		playerInfo.format(TheGameText->fetch("TOOLTIP:OnlinePlayerInfo"), playerWins, playerLosses);
	else
		playerInfo.format(TheGameText->fetch("TOOLTIP:PlayerInfo"), TheGameText->fetch(localeIdentifier).str(), playerWins, playerLosses);

	UnicodeString tooltip = UnicodeString::TheEmptyString;//TheGameText->fetch("TOOLTIP:PlayersInLobby");
	if (isLocalPlayer)
	{
		tooltip.format(TheGameText->fetch("TOOLTIP:LocalPlayer"), uName.str());
	}
	else
	{
		// not us
		if (TheGameSpyInfo->getBuddyMap()->find(info->m_profileID) != TheGameSpyInfo->getBuddyMap()->end())
		{
			// buddy
			tooltip.format(TheGameText->fetch("TOOLTIP:BuddyPlayer"), uName.str());
		}
		else
		{
			if (info->m_profileID)
			{
				// non-buddy profiled player
				tooltip.format(TheGameText->fetch("TOOLTIP:ProfiledPlayer"), uName.str());
			}
			else
			{
				// non-profiled player
				tooltip.format(TheGameText->fetch("TOOLTIP:GenericPlayer"), uName.str());
			}
		}
	}

	if (info->isIgnored())
	{
		tooltip.concat(TheGameText->fetch("TOOLTIP:IgnoredModifier"));
	}

	if (info->m_profileID)
	{
		tooltip.concat(playerInfo);
	}

	Int rank = rankForPoints( TheRankPointValues->m_ranks, info->m_rankPoints );
	AsciiString sideName = "GUI:RandomSide";
	if (info->m_side > 0)
	{		
		const PlayerTemplate *fac = ThePlayerTemplateStore->getNthPlayerTemplate(info->m_side);
		if (fac)
		{
			sideName.format("SIDE:%s", fac->getSide().str());
		}
	}
	AsciiString rankName;
	rankName.format("GUI:GSRank%d", rank);
	UnicodeString tmp;
	tmp.format(u"\n%ls %ls", TheGameText->fetch(sideName).str(), TheGameText->fetch(rankName).str());
	tooltip.concat(tmp);

	OnlinePlayerInfo card;
	if (OnlineIsEnabled() && OnlineGetPlayerInfo(aName, card))
	{
		tmp = OnlineRatingText(card);
		if (!tmp.isEmpty())
		{
			tooltip.concat(u"\n");
			tooltip.concat(tmp);
		}
	}

	TheMouse->setCursorTooltip( tooltip, -1, NULL, 1.5f ); // the text and width are the only params used.  the others are the default values.
}

static void populateGroupRoomListbox(GameWindow *lb)
{
	if (!lb)
		return;

	GadgetComboBoxReset(lb);
	Int indexToSelect = -1;
	GroupRoomMap::iterator iter;

	// now populate the combo box
	for (iter = TheGameSpyInfo->getGroupRoomList()->begin(); iter != TheGameSpyInfo->getGroupRoomList()->end(); ++iter)
	{
		GameSpyGroupRoom room = iter->second;
		if (room.m_groupID != TheGameSpyConfig->getQMChannel())
		{
			DEBUG_LOG(("populateGroupRoomListbox(): groupID %d\n", room.m_groupID));
			if (room.m_groupID == TheGameSpyInfo->getCurrentGroupRoom())
			{
				Int selected = GadgetComboBoxAddEntry(lb, room.m_translatedName, GameSpyColor[GSCOLOR_CURRENTROOM]);
				GadgetComboBoxSetItemData(lb, selected, (void *)(intptr_t)(room.m_groupID));
				indexToSelect = selected;
			}
			else
			{
				Int selected = GadgetComboBoxAddEntry(lb, room.m_translatedName, GameSpyColor[GSCOLOR_ROOM]);
				GadgetComboBoxSetItemData(lb, selected, (void *)(intptr_t)(room.m_groupID));
			}
		}
		else
		{
			DEBUG_LOG(("populateGroupRoomListbox(): skipping QM groupID %d\n", room.m_groupID));
		}
	}

	GadgetComboBoxSetSelectedPos(lb, indexToSelect);
}

static const char *rankNames[] = {
	"Private",
	"Corporal",
	"Sergeant",
	"Lieutenant",
	"Captain",
	"Major",
	"Colonel",
	"General",
	"Brigadier",
	"Commander",
};

const Image* LookupSmallRankImage(Int side, Int rankPoints)
{
	if (rankPoints == 0)
		return NULL;

	Int rank = rankForPoints( TheRankPointValues->m_ranks, rankPoints );

	if (rank < 0 || rank >= 10)
		return NULL;

	AsciiString sideStr = "N";
	switch(side)
	{
		case 2:  //USA
		case 5:  //Super Weapon
		case 6:  //Laser
		case 7:  //Air Force
			sideStr = "USA";
			break;

		case 3:  //China
		case 8:  //Tank
		case 9:  //Infantry
		case 10: //Nuke
			sideStr = "CHA";
			break;

		case 4:  //GLA
		case 11: //Toxin
		case 12: //Demolition
		case 13: //Stealth
			sideStr = "GLA";
			break;
	}

	AsciiString fullImageName;
	fullImageName.format("%s-%s", rankNames[rank], sideStr.str());
	const Image *img = TheMappedImageCollection->findImageByName(fullImageName);
	DEBUG_ASSERTLOG(img, ("*** Could not load small rank image '%s' from TheMappedImageCollection!\n", fullImageName.str()));
	return img;
}

static Int insertPlayerInListbox(const PlayerInfo& info, Color color)
{
	UnicodeString uStr;
	uStr.translate(info.m_name);

	/* The nick (cut with an ellipsis to its column), the tier and the ladder rating at two decimals from the
		 server's card for the nick, and the light for its round trip to the server.  A player the server has
		 sent no rating for is Unranked with a dash. */
	GameWindow *list = listboxLobbyPlayers;
	const Int pad = 2 * UIStyle::tallPx( LOBBY_PAD );
	OnlinePlayerInfo card;
	const Bool known = OnlineIsEnabled() && OnlineGetPlayerInfo(info.m_name, card);
	Int index = GadgetListBoxAddEntryText(list, UIStyle::fitName(uStr, list->winGetFont(), GadgetListBoxGetColumnWidth(list, PLAYER_NICK) - pad),
		color, -1, PLAYER_NICK);
	GadgetListBoxAddEntryText(list, uStr, color, index, PLAYER_KEY);

	const Bool rated = known && card.hasRating;
	const AsciiString tier( UIStyle::tierKey( known ? UIStyle::playerTier( card ) : UIStyle::TIER_UNRANKED ) );
	GadgetListBoxAddEntryText(list, UIStyle::fitText(OnlineTierName(tier), list->winGetFont(), GadgetListBoxGetColumnWidth(list, PLAYER_TIER) - pad),
		UIStyle::MUTED, index, PLAYER_TIER);
	GadgetListBoxAddEntryText(list, rated ? UIStyle::ratingText(card.exactRating) : UnicodeString(u"\x2013"), color, index, PLAYER_RATING);

	// the light's cell is a row high, which is what sets the row's height
	const Image *light = known ? UIStyle::pingImage(card.ping) : NULL;
	GadgetListBoxAddEntryImage(list, light, index, PLAYER_LIGHT, UIStyle::tallPx(PLAYER_ROW_HEIGHT) - 1,
		light ? light->getImageWidth() : 1);
	return index;
}

/// a player's row: nick, tier and rating, the light at the middle of its column
static void drawPlayerRow( GameWindow *list, Int row, const IRegion2D &rect, const IRegion2D &clip )
{
	LobbyDrawCell( list, row, PLAYER_NICK, rect, clip, LOBBY_ALIGN_LEFT );
	LobbyDrawCell( list, row, PLAYER_TIER, rect, clip, LOBBY_ALIGN_LEFT );
	LobbyDrawCell( list, row, PLAYER_RATING, rect, clip, LOBBY_ALIGN_RIGHT );
	LobbyDrawCell( list, row, PLAYER_LIGHT, rect, clip, LOBBY_ALIGN_CENTER );
	if (rect.hi.y <= clip.hi.y)
		TheDisplay->drawFillRect( rect.lo.x, rect.hi.y - 1, rect.hi.x - rect.lo.x, 1, UIStyle::GRID );
}

/* The player's own row, lit in the selection's art but never selected: GameSpyInfo::sendChat reads the
	 players list's selection as the whisper list, so a selected own row would send every line to him alone. */
static Int s_ownPlayerRow = -1;

static void drawPlayerList( GameWindow *window, WinInstanceData *instData )
{
	LobbyDrawRows( window, drawPlayerRow, s_ownPlayerRow );
}

/// highest ladder rating first, a player with none after everyone rated; the order before it otherwise
struct PlayerByRating
{
	bool operator()( const std::pair<PlayerInfo, Color> &a, const std::pair<PlayerInfo, Color> &b ) const
	{
		OnlinePlayerInfo first, second;
		const Bool firstRated = OnlineGetPlayerInfo( a.first.m_name, first ) && first.hasRating;
		const Bool secondRated = OnlineGetPlayerInfo( b.first.m_name, second ) && second.hasRating;
		if (firstRated != secondRated)
			return firstRated;
		return firstRated && first.exactRating > second.exactRating;
	}
};


void PopulateLobbyPlayerListbox(void)
{

	if (!listboxLobbyPlayers)
		return;

	// Display players
	PlayerInfoMap *players = TheGameSpyInfo->getPlayerInfoMap();
	PlayerInfoMap::iterator it;
	BuddyInfoMap *buddies = TheGameSpyInfo->getBuddyMap();
	if (listboxLobbyPlayers)
	{
		// save off old selection
		Int maxSelectedItems = GadgetListBoxGetNumEntries(listboxLobbyPlayers);
		Int *selectedIndices;
		GadgetListBoxGetSelected(listboxLobbyPlayers, (Int *)(&selectedIndices));
		std::set<AsciiString> selectedNames;
		std::set<AsciiString>::const_iterator selIt;
		std::set<Int> indicesToSelect;
		UnicodeString uStr;
		Int numSelected = 0;
		for (Int i=0; i<maxSelectedItems; ++i)
		{
			if (selectedIndices[i] < 0)
			{
				break;
			}
			++numSelected;
			AsciiString selectedName = nickInRow(listboxLobbyPlayers, selectedIndices[i]);
			selectedNames.insert(selectedName);
			DEBUG_LOG(("Saving off old selection %d (%s)\n", selectedIndices[i], selectedName.str()));
		}

		// save off old top entry
		Int previousTopIndex = GadgetListBoxGetTopVisibleEntry(listboxLobbyPlayers);

		GadgetListBoxReset(listboxLobbyPlayers);

		// EA's order, ops, then buddies, then everyone else; online, highest rating first over it
		std::vector< std::pair<PlayerInfo, Color> > rows;
		for (Int group = 0; group < 3; ++group)
		{
			for (it = players->begin(); it != players->end(); ++it)
			{
				PlayerInfo info = it->second;	// isIgnored is not const
				const Bool op = (info.m_flags & PEER_FLAG_OP) || TheGameSpyConfig->isPlayerVIP(info.m_profileID);
				const Bool buddy = !op && buddies->find(info.m_profileID) != buddies->end();
				if (group != (op ? 0 : buddy ? 1 : 2))
					continue;
				Color color = GameSpyColor[op ? GSCOLOR_PLAYER_OWNER : buddy ? GSCOLOR_PLAYER_BUDDY : GSCOLOR_PLAYER_NORMAL];
				if (OnlineIsEnabled())
					color = UIStyle::INK;
				if (info.isIgnored())
					color = OnlineIsEnabled() ? UIStyle::MUTED : GameSpyColor[GSCOLOR_PLAYER_IGNORED];
				rows.push_back(std::make_pair(info, color));
			}
		}
		if (OnlineIsEnabled())
			std::stable_sort(rows.begin(), rows.end(), PlayerByRating());

		s_ownPlayerRow = -1;
		for (size_t each = 0; each < rows.size(); ++each)
		{
			const PlayerInfo &info = rows[each].first;
			Int index = insertPlayerInListbox(info, rows[each].second);
			if (TheGameSpyInfo->getLocalName().compareNoCase(info.m_name) == 0)
				s_ownPlayerRow = index;

			selIt = selectedNames.find(info.m_name);
			if (selIt != selectedNames.end())
			{
				DEBUG_LOG(("Marking index %d (%s) to re-select\n", index, info.m_name.str()));
				indicesToSelect.insert(index);
			}
		}

		// restore selection
		if (indicesToSelect.size())
		{
			std::set<Int>::const_iterator indexIt;
			Int *newIndices = NEW Int[indicesToSelect.size()];
			Int i;	// used after the loop; VC6 for-scope let it escape
			for (i=0, indexIt = indicesToSelect.begin(); indexIt != indicesToSelect.end(); ++i, ++indexIt)
			{
				newIndices[i] = *indexIt;
				DEBUG_LOG(("Queueing up index %d to re-select\n", *indexIt));
			}
			GadgetListBoxSetSelected(listboxLobbyPlayers, newIndices, indicesToSelect.size());
			delete[] newIndices;
		}

		if (indicesToSelect.size() != numSelected)
		{
			TheWindowManager->winSetLoneWindow(NULL);
		}

		// restore top visible entry
		GadgetListBoxSetTopVisibleEntry(listboxLobbyPlayers, previousTopIndex);
	}

}

//-------------------------------------------------------------------------------------------------
// The page in the online pages' frame (ONLINE-UI-REDESIGN.md, "Custom games"): the header, the two
// boxes' headings, the chat rooms as tabs over the chat, the chat entry's hint
//-------------------------------------------------------------------------------------------------
enum LobbyText
{
	TEXT_GAMES,					///< the game box's heading
	TEXT_GAMES_COUNT,		///< "12 open"
	TEXT_PLAYERS,				///< "In General"
	TEXT_PLAYERS_COUNT,
	TEXT_SAY,						///< the chat entry's hint while it is empty
	TEXT_COUNT
};
enum { MAX_ROOM_TABS = 8 };
static DisplayString *s_text[ TEXT_COUNT ];
static UnicodeString s_playersWords;							///< the player box's heading before it was fitted
static DisplayString *s_roomTab[ MAX_ROOM_TABS ];
static UnicodeString s_roomTabWords[ MAX_ROOM_TABS ];	///< each tab's label before it was fitted
static Int s_roomTabGroup[ MAX_ROOM_TABS ];
static IRegion2D s_roomTabRect[ MAX_ROOM_TABS ];
static Int s_roomTabsShown = 0;
static GameWinDrawFunc s_gadgetParentDraw = NULL;
static GameWinDrawFunc s_panelGamesDraw = NULL;
static GameWinDrawFunc s_panelPlayersDraw = NULL;
static GameWinDrawFunc s_chatEntryDraw = NULL;

/// a chat room's name; the online server's one room (OnlinePeer.cpp's "Lobby") is the design's General
static UnicodeString roomName( const GameSpyGroupRoom &room )
{
	if (OnlineIsEnabled() && room.m_name.compareNoCase( "Lobby" ) == 0)
		return TheGameText->fetch( "GUI:OnlineLobbyRoomGeneral" );
	return room.m_translatedName;
}

static const GameSpyGroupRoom *currentRoom( void )
{
	GroupRoomMap *rooms = TheGameSpyInfo->getGroupRoomList();
	GroupRoomMap::const_iterator it = rooms->find( TheGameSpyInfo->getCurrentGroupRoom() );
	return it != rooms->end() ? &it->second : NULL;
}

/// who is in a room: the players heard in this one, the server's count for the others
static Int roomCount( const GameSpyGroupRoom &room )
{
	if (room.m_groupID == TheGameSpyInfo->getCurrentGroupRoom())
		return (Int)TheGameSpyInfo->getPlayerInfoMap()->size();
	return room.m_numWaiting;
}

/// the game on a row of the game list has started: it is listed as Playing, which no Join can take
static Bool gameRowPlaying( GameWindow *list, Int row )
{
	StagingRoomMap *games = TheGameSpyInfo->getStagingRoomList();
	StagingRoomMap::iterator it = games->find( (Int)(intptr_t)GadgetListBoxGetItemData( list, row ) );
	return it != games->end() && it->second && it->second->isGameInProgress();
}

/// moves to another chat room, as EA's room combo box did
static void joinRoom( Int groupID )
{
	if (!groupID || groupID == TheGameSpyInfo->getCurrentGroupRoom())
		return;
	TheGameSpyInfo->leaveGroupRoom();
	TheGameSpyInfo->joinGroupRoom(groupID);

	if (TheGameSpyConfig->restrictGamesToLobby())
	{
		TheGameSpyInfo->clearStagingRoomList();
		RefreshGameListBoxes();
		PeerRequest req;
		req.peerRequestType = PeerRequest::PEERREQUEST_STARTGAMELIST;
		req.gameList.restrictGameList = TRUE;
		TheGameSpyPeerMessageQueue->addRequest(req);
	}
}

/* The chat rooms the server listed, as tabs in the strip the hidden room combo box keeps: "General 41",
	 the open one's label ACTIVE, all of them MUTED while a game is being hosted or joined.  A click lands in
	 WOLLobbyMenuInput, which finds the tab by the rectangles drawn here. */
static void drawRoomTabs( void )
{
	s_roomTabsShown = 0;
	if (!comboLobbyGroupRooms)
		return;
	Int x, y, width, height;
	comboLobbyGroupRooms->winGetScreenPosition( &x, &y );
	comboLobbyGroupRooms->winGetSize( &width, &height );
	const Int right = x + width;
	const Int pad = UIStyle::tallPx( 22.0f );		// the page tabs' pad either side of a label
	const Int gap = UIStyle::px( 8.0f );
	GameFont *body = UIStyle::readableFont( UIStyle::TEXT_BODY );
	const MouseIO *mouse = TheMouse ? TheMouse->getMouseStatus() : NULL;
	GroupRoomMap *rooms = TheGameSpyInfo->getGroupRoomList();
	for (GroupRoomMap::const_iterator it = rooms->begin(); it != rooms->end() && s_roomTabsShown < MAX_ROOM_TABS; ++it)
	{
		const GameSpyGroupRoom &room = it->second;
		if (room.m_groupID == TheGameSpyConfig->getQMChannel() || !s_roomTab[ s_roomTabsShown ])
			continue;
		const Int tab = s_roomTabsShown;
		UnicodeString words;
		words.format( TheGameText->fetch( "GUI:OnlineLobbyRoomCount" ), roomName( room ).str(), roomCount( room ) );
		if (words.compare( s_roomTabWords[ tab ] ) != 0)
		{
			s_roomTabWords[ tab ] = words;
			s_roomTab[ tab ]->setText( UIStyle::fitText( words, body, max( width - 2 * pad, 1 ) ) );
		}
		Int w, h;
		s_roomTab[ tab ]->getSize( &w, &h );
		const Int tabWidth = w + 2 * pad;
		if (x + tabWidth > right)
			break;

		IRegion2D &rect = s_roomTabRect[ tab ];
		rect.lo.x = x;
		rect.lo.y = y;
		rect.hi.x = x + tabWidth;
		rect.hi.y = y + height;
		const Bool open = room.m_groupID == TheGameSpyInfo->getCurrentGroupRoom();
		const Bool hovered = mouse && !open && mouse->pos.x >= rect.lo.x && mouse->pos.x < rect.hi.x
			&& mouse->pos.y >= rect.lo.y && mouse->pos.y < rect.hi.y;
		const Color color = UIStyle::tab( x, y, tabWidth, height, !s_tryingToHostOrJoin, open, hovered );
		s_roomTab[ tab ]->draw( x + pad, y + ( height - h ) / 2, color, UIStyle::SHADOW );
		s_roomTabGroup[ tab ] = room.m_groupID;
		++s_roomTabsShown;
		x += tabWidth + gap;
	}
}

/// the room panel, then the header on it (the title, the server line, the wordmark, the player's line)
static void drawGadgetParent( GameWindow *window, WinInstanceData *instData )
{
	if (s_gadgetParentDraw)
		s_gadgetParentDraw( window, instData );
	UIStyle::onlineHeader( TheGameText->fetch( "GUI:OnlineTabCustom" ) );
	drawRoomTabs();
}

/// a box's heading ACTIVE in the section face and its count MUTED at its right, in the band over its list
static void drawBoxHeading( GameWindow *box, DisplayString *heading, DisplayString *count )
{
	if (!heading || !count)
		return;
	Int x, y, width, height;
	box->winGetScreenPosition( &x, &y );
	box->winGetSize( &width, &height );
	const Int band = UIStyle::px( 56.0f );		// the layout starts a box's list this far under its top
	Int w, h;
	heading->getSize( &w, &h );
	UIStyle::heading( heading, x + UIStyle::px( 24.0f ), y + ( band - h ) / 2 );
	count->getSize( &w, &h );
	count->draw( x + width - UIStyle::px( 48.0f ) - w, y + ( band - h ) / 2, UIStyle::MUTED, UIStyle::SHADOW );
}

/// the game box: "Games", and how many a Join can take, with how many are being played when any are
static void drawPanelGames( GameWindow *window, WinInstanceData *instData )
{
	if (s_panelGamesDraw)
		s_panelGamesDraw( window, instData );
	Int open = 0;
	Int playing = 0;
	StagingRoomMap *games = TheGameSpyInfo->getStagingRoomList();
	for (StagingRoomMap::iterator it = games->begin(); it != games->end(); ++it)
	{
		GameSpyStagingRoom *game = it->second;
		if (!game)
			continue;
		if (game->isGameInProgress())
		{
			++playing;
			continue;
		}
		if (!LobbyGameFull(game) && game->getExeCRC() == TheGlobalData->m_exeCRC && game->getIniCRC() == TheGlobalData->m_iniCRC)
			++open;
	}
	UnicodeString count;
	if (playing > 0)
		count.format( TheGameText->fetch( "GUI:OnlineLobbyCount" ), open, playing );
	else
		count.format( TheGameText->fetch( "GUI:OnlineLobbyCountOpen" ), open );
	if (s_text[ TEXT_GAMES_COUNT ])
		s_text[ TEXT_GAMES_COUNT ]->setText( count );
	drawBoxHeading( window, s_text[ TEXT_GAMES ], s_text[ TEXT_GAMES_COUNT ] );
}

/// the player box: "In General" and how many are in it
static void drawPanelPlayers( GameWindow *window, WinInstanceData *instData )
{
	if (s_panelPlayersDraw)
		s_panelPlayersDraw( window, instData );
	if (!s_text[ TEXT_PLAYERS ] || !s_text[ TEXT_PLAYERS_COUNT ])
		return;
	const GameSpyGroupRoom *room = currentRoom();
	const UnicodeString name = room ? roomName( *room ) : UnicodeString::TheEmptyString;
	UnicodeString words;
	words.format( TheGameText->fetch( "GUI:OnlineLobbyInRoom" ), name.str() );
	if (words.compare( s_playersWords ) != 0)
	{
		Int width, height;
		window->winGetSize( &width, &height );
		s_playersWords = words;
		s_text[ TEXT_PLAYERS ]->setText( UIStyle::fitText( words, UIStyle::sectionFont(), max( width - UIStyle::px( 120.0f ), 1 ) ) );
	}
	UnicodeString count;
	count.format( u"%d", (Int)TheGameSpyInfo->getPlayerInfoMap()->size() );
	s_text[ TEXT_PLAYERS_COUNT ]->setText( count );
	drawBoxHeading( window, s_text[ TEXT_PLAYERS ], s_text[ TEXT_PLAYERS_COUNT ] );
}

/// the chat entry, and while it is empty "Say something to General" MUTED where its text starts
static void drawChatEntry( GameWindow *window, WinInstanceData *instData )
{
	if (s_chatEntryDraw)
		s_chatEntryDraw( window, instData );
	DisplayString *hint = s_text[ TEXT_SAY ];
	if (!hint || !GadgetTextEntryGetText( window ).isEmpty())
		return;
	const GameSpyGroupRoom *room = currentRoom();
	if (!room)
		return;
	UnicodeString words;
	words.format( TheGameText->fetch( "GUI:OnlineLobbySay" ), roomName( *room ).str() );
	hint->setText( words );
	Int x, y, width, height, w, h;
	window->winGetScreenPosition( &x, &y );
	window->winGetSize( &width, &height );
	hint->getSize( &w, &h );
	// W3DTextEntry starts its text seven pixels in; the hint stands a little after the caret
	hint->draw( x + 7 + UIStyle::tallPx( 8.0f ), y + ( height - h ) / 2, UIStyle::MUTED, UIStyle::SHADOW );
}

/// puts draw over a window's own; a second init on the same layout keeps the first capture, or the
/// hook would call itself
static void hookDraw( GameWindow *window, GameWinDrawFunc &saved, GameWinDrawFunc draw )
{
	if (!window || window->winGetDrawFunc() == draw)
		return;
	saved = window->winGetDrawFunc();
	window->winSetDrawFunc( draw );
}

static GameWindow *lobbyWindow( const char *name )
{
	AsciiString full;
	full.format( "WOLCustomLobby.wnd:%s", name );
	return TheWindowManager->winGetWindowFromId( parent, NAMEKEY( full ) );
}

static void freeLobbyText( void )
{
	for (Int each = 0; each < TEXT_COUNT; ++each)
	{
		if (s_text[ each ])
			TheDisplayStringManager->freeDisplayString( s_text[ each ] );
		s_text[ each ] = NULL;
	}
	for (Int tab = 0; tab < MAX_ROOM_TABS; ++tab)
	{
		if (s_roomTab[ tab ])
			TheDisplayStringManager->freeDisplayString( s_roomTab[ tab ] );
		s_roomTab[ tab ] = NULL;
		s_roomTabWords[ tab ].clear();
	}
	s_playersWords.clear();
	s_roomTabsShown = 0;
}

/// the page's frame on the layout's windows, from its init
static void initLobbyLook( void )
{
	UIStyle::onlinePageInit( parent, "WOLCustomLobby.wnd", UIStyle::ONLINE_TAB_CUSTOM );
	// the header draws the title with its subtitle; the layout's title would stand under it twice
	GameWindow *title = lobbyWindow( "StaticTextTitle" );
	if (title)
		title->winHide( TRUE );

	hookDraw( lobbyWindow( "GadgetParent" ), s_gadgetParentDraw, drawGadgetParent );
	hookDraw( lobbyWindow( "PanelGames" ), s_panelGamesDraw, drawPanelGames );
	hookDraw( lobbyWindow( "PanelPlayers" ), s_panelPlayersDraw, drawPanelPlayers );
	hookDraw( textEntryChat, s_chatEntryDraw, drawChatEntry );

	// the player list: a line a row, the page's columns, the page's rows
	if (listboxLobbyPlayers)
	{
		listboxLobbyPlayers->winSetStatus( WIN_STATUS_ONE_LINE );
		LobbySetColumns( listboxLobbyPlayers, PLAYER_COLUMNS, PLAYER_COLUMN_PERCENT );
		listboxLobbyPlayers->winSetDrawFunc( drawPlayerList );
	}

	freeLobbyText();
	GameFont *body = UIStyle::readableFont( UIStyle::TEXT_BODY );
	GameFont *const faces[ TEXT_COUNT ] = { UIStyle::sectionFont(), body, UIStyle::sectionFont(), body,
		textEntryChat ? textEntryChat->winGetFont() : body };
	for (Int each = 0; each < TEXT_COUNT; ++each)
	{
		s_text[ each ] = TheDisplayStringManager->newDisplayString();
		s_text[ each ]->setFont( faces[ each ] );
	}
	s_text[ TEXT_GAMES ]->setText( TheGameText->fetch( "GUI:Games" ) );
	for (Int tab = 0; tab < MAX_ROOM_TABS; ++tab)
	{
		s_roomTab[ tab ] = TheDisplayStringManager->newDisplayString();
		s_roomTab[ tab ]->setFont( body );
	}
}

/// Back, or a tab: out of the chat room, Home by popping to the hub under this page, a tab by pushing its
/// page over the hub; refused while a game is being hosted or joined, as Back always was
static void leaveLobby( UIStyle::OnlineTab to )
{
	if (s_tryingToHostOrJoin)
		return;
	TheGameSpyInfo->leaveGroupRoom();
	SetLobbyAttemptHostJoin( TRUE ); // pretend, since we don't want to queue up another action
	buttonPushed = true;
	nextScreen = UIStyle::onlineTabScreen( to );
	TheShell->pop();
}

//-------------------------------------------------------------------------------------------------
/** Initialize the WOL Lobby Menu */
//-------------------------------------------------------------------------------------------------
void WOLLobbyMenuInit( WindowLayout *layout, void *userData )
{
	nextScreen = NULL;
	buttonPushed = false;
	isShuttingDown = false;

	SetLobbyAttemptHostJoin(FALSE); // not trying to host or join

	gameListRefreshTime = 0;
	playerListRefreshTime = 0;

	parentWOLLobbyID = TheNameKeyGenerator->nameToKey( AsciiString( "WOLCustomLobby.wnd:WOLLobbyMenuParent" ) );
	parent = TheWindowManager->winGetWindowFromId(NULL, parentWOLLobbyID);

	buttonBackID = TheNameKeyGenerator->nameToKey(AsciiString("WOLCustomLobby.wnd:ButtonBack"));
	buttonBack = TheWindowManager->winGetWindowFromId(parent, buttonBackID);

	buttonHostID = TheNameKeyGenerator->nameToKey(AsciiString("WOLCustomLobby.wnd:ButtonHost"));
	buttonHost = TheWindowManager->winGetWindowFromId(parent, buttonHostID);

	buttonRefreshID = TheNameKeyGenerator->nameToKey(AsciiString("WOLCustomLobby.wnd:ButtonRefresh"));
	buttonRefresh = TheWindowManager->winGetWindowFromId(parent, buttonRefreshID);

	buttonJoinID = TheNameKeyGenerator->nameToKey(AsciiString("WOLCustomLobby.wnd:ButtonJoin"));
	buttonJoin = TheWindowManager->winGetWindowFromId(parent, buttonJoinID);
	buttonJoin->winEnable(FALSE);

	buttonBuddyID = TheNameKeyGenerator->nameToKey(AsciiString("WOLCustomLobby.wnd:ButtonBuddy"));
	buttonBuddy = TheWindowManager->winGetWindowFromId(parent, buttonBuddyID);

	buttonEmoteID = TheNameKeyGenerator->nameToKey(AsciiString("WOLCustomLobby.wnd:ButtonEmote"));
	buttonEmote = TheWindowManager->winGetWindowFromId(parent, buttonEmoteID);

	textEntryChatID = TheNameKeyGenerator->nameToKey(AsciiString("WOLCustomLobby.wnd:TextEntryChat"));
	textEntryChat = TheWindowManager->winGetWindowFromId(parent, textEntryChatID);

	listboxLobbyPlayersID = TheNameKeyGenerator->nameToKey(AsciiString("WOLCustomLobby.wnd:ListboxPlayers"));
	listboxLobbyPlayers = TheWindowManager->winGetWindowFromId(parent, listboxLobbyPlayersID);
	listboxLobbyPlayers->winSetTooltipFunc(playerTooltip);

	listboxLobbyChatID = TheNameKeyGenerator->nameToKey(AsciiString("WOLCustomLobby.wnd:ListboxChat"));
	listboxLobbyChat = TheWindowManager->winGetWindowFromId(parent, listboxLobbyChatID);
	TheGameSpyInfo->registerTextWindow(listboxLobbyChat);

	comboLobbyGroupRoomsID = TheNameKeyGenerator->nameToKey(AsciiString("WOLCustomLobby.wnd:ComboBoxGroupRooms"));
	comboLobbyGroupRooms = TheWindowManager->winGetWindowFromId(parent, comboLobbyGroupRoomsID);

	GadgetTextEntrySetText(textEntryChat, UnicodeString::TheEmptyString);

	populateGroupRoomListbox(comboLobbyGroupRooms);
	initLobbyLook();

	// Show Menu
	layout->hide( FALSE );

	// if we're not in a room, this will join the best available one
	if (!TheGameSpyInfo->getCurrentGroupRoom())
	{
		if (groupRoomToJoin)
		{
			DEBUG_LOG(("WOLLobbyMenuInit() - rejoining group room %d\n", groupRoomToJoin));
			TheGameSpyInfo->joinGroupRoom(groupRoomToJoin);
			groupRoomToJoin = 0;
		}
		else
		{
			DEBUG_LOG(("WOLLobbyMenuInit() - joining best group room\n"));
			TheGameSpyInfo->joinBestGroupRoom();
		}
	}
	else
	{
		DEBUG_LOG(("WOLLobbyMenuInit() - not joining group room because we're already in one\n"));
	}

	GrabWindowInfo();

	TheGameSpyInfo->clearStagingRoomList();
	PeerRequest req;
	req.peerRequestType = PeerRequest::PEERREQUEST_STARTGAMELIST;
	req.gameList.restrictGameList = TheGameSpyConfig->restrictGamesToLobby();
	TheGameSpyPeerMessageQueue->addRequest(req);

	// animate controls
//	TheShell->registerWithAnimateManager(parent, WIN_ANIMATION_SLIDE_TOP, TRUE);
	TheShell->showShellMap(TRUE);
	TheGameSpyGame->reset();
	
	CustomMatchPreferences pref;
//	GameWindow *slider = TheWindowManager->winGetWindowFromId(parent, sliderChatAdjustID);
//	if (slider)
//	{
//		GadgetSliderSetPosition(slider, pref.getChatSizeSlider());
//		doSliderTrack(slider, pref.getChatSizeSlider());
//	}
//
	if (pref.usesLongGameList())
	{
		ToggleGameListType();
	}

	// Set Keyboard to chat window
	TheWindowManager->winSetFocus( textEntryChat );
	raiseMessageBoxes = true;

	TheLobbyQueuedUTMs.clear();
	justEntered = TRUE;
	initialGadgetDelay = 2;
	GameWindow *win = TheWindowManager->winGetWindowFromId(NULL, TheNameKeyGenerator->nameToKey("WOLCustomLobby.wnd:GadgetParent"));
	if(win)
		win->winHide(TRUE);
	DontShowMainMenu = TRUE;

} // WOLLobbyMenuInit

//-------------------------------------------------------------------------------------------------
/** This is called when a shutdown is complete for this menu */
//-------------------------------------------------------------------------------------------------
static void shutdownComplete( WindowLayout *layout )
{

	isShuttingDown = false;

	// hide the layout
	layout->hide( TRUE );

	// the frame drew through the fade; the next page's init, from the push below, makes its own
	UIStyle::onlinePageShutdown( parent );
	freeLobbyText();

	// our shutdown is complete
	TheShell->shutdownComplete( layout, (nextScreen != NULL) );

	if (nextScreen != NULL)
	{
		TheShell->push(nextScreen);
	}

	nextScreen = NULL;

}  // end if

//-------------------------------------------------------------------------------------------------
/** WOL Lobby Menu shutdown method */
//-------------------------------------------------------------------------------------------------
void WOLLobbyMenuShutdown( WindowLayout *layout, void *userData )
{
	CustomMatchPreferences pref;
//	GameWindow *slider = TheWindowManager->winGetWindowFromId(parent, sliderChatAdjustID);
//	if (slider)
//	{
//		pref.setChatSizeSlider(GadgetSliderGetPosition(slider));
//	}
	if (GetGameInfoListBox())
	{
		pref.setUsesLongGameList(FALSE);
	}
	else
	{
		pref.setUsesLongGameList(TRUE);
	}
	pref.write();

	ReleaseWindowInfo();

	TheGameSpyInfo->unregisterTextWindow(listboxLobbyChat);

	//TheGameSpyChat->stopListingGames();
	PeerRequest req;
	req.peerRequestType = PeerRequest::PEERREQUEST_STOPGAMELIST;
	TheGameSpyPeerMessageQueue->addRequest(req);

	listboxLobbyChat = NULL;
	listboxLobbyPlayers = NULL;

	isShuttingDown = true;

	// if we are shutting down for an immediate pop, skip the animations
	Bool popImmediate = *(Bool *)userData;
	if( popImmediate )
	{

		shutdownComplete( layout );
		return;

	}  //end if

	TheShell->reverseAnimatewindow();
	DontShowMainMenu = FALSE;

	RaiseGSMessageBox();
	TheTransitionHandler->reverse("WOLCustomLobbyFade");

}  // WOLLobbyMenuShutdown

static void fillPlayerInfo(const PeerResponse *resp, PlayerInfo *info)
{
	info->m_name			= resp->nick.c_str();
	info->m_profileID	= resp->player.profileID;
	info->m_flags			= resp->player.flags;
	info->m_wins			= resp->player.wins;
	info->m_losses		= resp->player.losses;
	info->m_locale		= resp->locale.c_str();
	info->m_rankPoints= resp->player.rankPoints;
	info->m_side			= resp->player.side;
	info->m_preorder	= resp->player.preorder;
}

#ifdef PERF_TEST
static const char* getMessageString(Int t)
{
	switch(t)
	{
		case PeerResponse::PEERRESPONSE_LOGIN:
			return "login";
		case PeerResponse::PEERRESPONSE_DISCONNECT:
			return "disconnect";
		case PeerResponse::PEERRESPONSE_MESSAGE:
			return "message";
		case PeerResponse::PEERRESPONSE_GROUPROOM:
			return "group room";
		case PeerResponse::PEERRESPONSE_STAGINGROOM:
			return "staging room";
		case PeerResponse::PEERRESPONSE_STAGINGROOMPLAYERINFO:
			return "staging room player info";
		case PeerResponse::PEERRESPONSE_JOINGROUPROOM:
			return "group room join";
		case PeerResponse::PEERRESPONSE_CREATESTAGINGROOM:
			return "staging room create";
		case PeerResponse::PEERRESPONSE_JOINSTAGINGROOM:
			return "staging room join";
		case PeerResponse::PEERRESPONSE_PLAYERJOIN:
			return "player join";
		case PeerResponse::PEERRESPONSE_PLAYERLEFT:
			return "player part";
		case PeerResponse::PEERRESPONSE_PLAYERCHANGEDNICK:
			return "player nick";
		case PeerResponse::PEERRESPONSE_PLAYERINFO:
			return "player info";
		case PeerResponse::PEERRESPONSE_PLAYERCHANGEDFLAGS:
			return "player flags";
		case PeerResponse::PEERRESPONSE_ROOMUTM:
			return "room UTM";
		case PeerResponse::PEERRESPONSE_PLAYERUTM:
			return "player UTM";
		case PeerResponse::PEERRESPONSE_QUICKMATCHSTATUS:
			return "QM status";
		case PeerResponse::PEERRESPONSE_GAMESTART:
			return "game start";
		case PeerResponse::PEERRESPONSE_FAILEDTOHOST:
			return "host failure";
	}
	return "unknown";
}
#endif // PERF_TEST

//-------------------------------------------------------------------------------------------------
/** refreshGameList 
		The Bool is used to force refresh if the refresh button was hit.*/
//-------------------------------------------------------------------------------------------------
static void refreshGameList( Bool forceRefresh )
{
	Int refreshInterval = gameListRefreshInterval;

	if (forceRefresh || ((gameListRefreshTime == 0) || ((gameListRefreshTime + refreshInterval) <= Clock_Milliseconds())))
	{
		if (TheGameSpyInfo->hasStagingRoomListChanged())
		{
			//DEBUG_LOG(("################### refreshing game list\n"));
			//DEBUG_LOG(("gameRefreshTime=%d, refreshInterval=%d, now=%d\n", gameListRefreshTime, refreshInterval, timeGetTime()));
			RefreshGameListBoxes();
			gameListRefreshTime = Clock_Milliseconds();
		} else {
			//DEBUG_LOG(("-"));
		}
	} else {
		//DEBUG_LOG(("gameListRefreshTime: %d refreshInterval: %d\n"));
	}
}
//-------------------------------------------------------------------------------------------------
/** refreshPlayerList 
		The Bool is used to force refresh if the refresh button was hit.*/
//-------------------------------------------------------------------------------------------------
static void refreshPlayerList( Bool forceRefresh )
{
		Int refreshInterval = playerListRefreshInterval;

		if (forceRefresh ||((playerListRefreshTime == 0) || ((playerListRefreshTime + refreshInterval) <= Clock_Milliseconds())))
		{
				PopulateLobbyPlayerListbox();
				playerListRefreshTime = Clock_Milliseconds();
		}
}
//-------------------------------------------------------------------------------------------------
/** WOL Lobby Menu update method */
//-------------------------------------------------------------------------------------------------
void WOLLobbyMenuUpdate( WindowLayout * layout, void *userData)
{
		if(justEntered)
	{
		if(initialGadgetDelay == 1)
		{
			TheTransitionHandler->remove("MainMenuDefaultMenuLogoFade");
			TheTransitionHandler->setGroup("WOLCustomLobbyFade");
			initialGadgetDelay = 2;
			justEntered = FALSE;
		}
		else
			initialGadgetDelay--;
	}
	if (TheGameLogic->isInShellGame() && TheGameLogic->getFrame() == 1)
	{
		SignalUIInteraction(SHELL_SCRIPT_HOOK_GENERALS_ONLINE_ENTERED_FROM_GAME);
	}
	

	// We'll only be successful if we've requested to 
	if(isShuttingDown && TheShell->isAnimFinished() && TheTransitionHandler->isFinished())
		shutdownComplete(layout);

	if (raiseMessageBoxes)
	{
		RaiseGSMessageBox();
		raiseMessageBoxes = false;
	}

	if (TheShell->isAnimFinished() && TheTransitionHandler->isFinished() && !buttonPushed && TheGameSpyPeerMessageQueue)
	{
		HandleBuddyResponses();
		HandlePersistentStorageResponses();

#ifdef PERF_TEST
		UnsignedInt start = Clock_Milliseconds();
		UnsignedInt end = Clock_Milliseconds();
		std::list<Int> responses;
		Int numMessages = 0;
#endif // PERF_TEST

		Int allowedMessages = TheGameSpyInfo->getMaxMessagesPerUpdate();
		Bool sawImportantMessage = FALSE;
		Bool shouldRepopulatePlayers = FALSE;
		PeerResponse resp;
		while (allowedMessages-- && !sawImportantMessage && TheGameSpyPeerMessageQueue->getResponse( resp ))
		{
#ifdef PERF_TEST
			++numMessages;
			responses.push_back(resp.peerResponseType);
#endif // PERF_TEST
			switch (resp.peerResponseType)
			{
			case PeerResponse::PEERRESPONSE_JOINGROUPROOM:
				sawImportantMessage = TRUE;
				if (resp.joinGroupRoom.ok)
				{
					//buttonPushed = true;
					TheGameSpyInfo->setCurrentGroupRoom(resp.joinGroupRoom.id);
					TheGameSpyInfo->getPlayerInfoMap()->clear();
					if (OnlineIsEnabled())
						DEBUG_LOG(("ONLINE LOBBY joined room=%d\n", resp.joinGroupRoom.id));
					GroupRoomMap::iterator iter = TheGameSpyInfo->getGroupRoomList()->find(resp.joinGroupRoom.id);
					if (iter != TheGameSpyInfo->getGroupRoomList()->end())
					{
						GameSpyGroupRoom room = iter->second;
						UnicodeString msg;
						msg.format(TheGameText->fetch("GUI:LobbyJoined"), roomName(room).str());
						TheGameSpyInfo->addText(msg, GameSpyColor[GSCOLOR_DEFAULT], NULL);
					}
				}
				else
				{
					DEBUG_LOG(("WOLLobbyMenuUpdate() - joining best group room\n"));
					TheGameSpyInfo->joinBestGroupRoom();
				}
				populateGroupRoomListbox(comboLobbyGroupRooms);
				shouldRepopulatePlayers = TRUE;
				break;
			case PeerResponse::PEERRESPONSE_PLAYERCHANGEDFLAGS:
				{
					PlayerInfo p;
					fillPlayerInfo(&resp, &p);
					TheGameSpyInfo->updatePlayerInfo(p);
					shouldRepopulatePlayers = TRUE;
				}
				break;
			case PeerResponse::PEERRESPONSE_PLAYERCHANGEDNICK:
				{
					PlayerInfo p;
					fillPlayerInfo(&resp, &p);
					TheGameSpyInfo->updatePlayerInfo(p);
					shouldRepopulatePlayers = TRUE;
				}
				break;
			case PeerResponse::PEERRESPONSE_PLAYERINFO:
				{
					PlayerInfo p;
					fillPlayerInfo(&resp, &p);
					TheGameSpyInfo->updatePlayerInfo(p);
					shouldRepopulatePlayers = TRUE;
				}
				break;
			case PeerResponse::PEERRESPONSE_PLAYERJOIN:
				{
					if (resp.player.roomType == GroupRoom)
					{
						PlayerInfo p;
						fillPlayerInfo(&resp, &p);
						TheGameSpyInfo->updatePlayerInfo(p);
						shouldRepopulatePlayers = TRUE;
					}
				}
				break;
			case PeerResponse::PEERRESPONSE_PLAYERUTM:
			case PeerResponse::PEERRESPONSE_ROOMUTM:
				{
					DEBUG_LOG(("Putting off a UTM in the lobby\n"));
					TheLobbyQueuedUTMs.push_back(resp);
				}
				break;
			case PeerResponse::PEERRESPONSE_PLAYERLEFT:
				{
					PlayerInfo p;
					fillPlayerInfo(&resp, &p);
					TheGameSpyInfo->playerLeftGroupRoom(resp.nick.c_str());
					shouldRepopulatePlayers = TRUE;
				}
				break;
			case PeerResponse::PEERRESPONSE_MESSAGE:
				{
					if (OnlineIsEnabled())
						DEBUG_LOG(("ONLINE CHAT where=lobby from=%s text=%s\n", resp.nick.c_str(), WideCharAsUtf8( resp.text.c_str() ).str()));
					TheGameSpyInfo->addChat(resp.nick.c_str(), resp.message.profileID,
						UnicodeString(resp.text.c_str()), !resp.message.isPrivate, resp.message.isAction, listboxLobbyChat);
				}
				break;
			case PeerResponse::PEERRESPONSE_DISCONNECT:
				{
					sawImportantMessage = TRUE;
					UnicodeString title, body;
					AsciiString disconMunkee;
					disconMunkee.format("GUI:GSDisconReason%d", resp.discon.reason);
					title = TheGameText->fetch( "GUI:GSErrorTitle" );
					body = TheGameText->fetch( disconMunkee );
					GameSpyCloseAllOverlays();
					GSMessageBoxOk( title, body );
					TheGameSpyInfo->reset();
					TheShell->pop();
				}
			case PeerResponse::PEERRESPONSE_CREATESTAGINGROOM:
				{
					sawImportantMessage = TRUE;
					SetLobbyAttemptHostJoin(FALSE);
					if (OnlineIsEnabled())
						DEBUG_LOG(("ONLINE HOSTED result=%d\n", (Int)resp.createStagingRoom.result));
					if (resp.createStagingRoom.result == PEERJoinSuccess)
					{
						// Woohoo!  On to our next screen!
						buttonPushed = true;
						nextScreen = "Menus/GameSpyGameOptionsMenu.wnd";
						TheShell->pop();
						TheGameSpyInfo->markAsStagingRoomHost();
						TheGameSpyInfo->setGameOptions();
					}
				}
				break;
			case PeerResponse::PEERRESPONSE_JOINSTAGINGROOM:
				{
					sawImportantMessage = TRUE;
					SetLobbyAttemptHostJoin(FALSE);
					if (OnlineIsEnabled())
						DEBUG_LOG(("ONLINE JOINED ok=%d\n", (Int)resp.joinStagingRoom.ok));
					Bool isHostPresent = TRUE;
					if (resp.joinStagingRoom.ok == PEERTrue)
					{
						GameSpyStagingRoom *room = TheGameSpyInfo->getCurrentStagingRoom();
						if (!room)
						{
							isHostPresent = FALSE;
						}
						else
						{
							isHostPresent = FALSE;
							for (Int i=0; i<MAX_SLOTS; ++i)
							{
								AsciiString hostName;
								hostName.translate(room->getConstSlot(0)->getName());
								const char *firstPlayer = resp.stagingRoomPlayerNames[i].c_str();
								if (!strcmp(hostName.str(), firstPlayer))
								{
									DEBUG_LOG(("Saw host %s == %s in slot %d\n", hostName.str(), firstPlayer, i));
									isHostPresent = TRUE;
								}
							}
							if (!isHostPresent)
							{
								AsciiString hostName, names;
								hostName.translate(room->getConstSlot(0)->getName());
								for (Int i=0; i<MAX_SLOTS; ++i)
								{
									if (resp.stagingRoomPlayerNames[i].empty())
										continue;
									if (!names.isEmpty())
										names.concat(',');
									names.concat(resp.stagingRoomPlayerNames[i].c_str());
								}
								DEBUG_LOG(("Join: host '%s' (seat 0 state %d) is not among the room's players [%s]; leaving\n",
									hostName.str(), (Int)room->getConstSlot(0)->getState(), names.str()));
								SendStagingLeaveReason("NOHOST", hostName);
							}
						}
					}
					if (resp.joinStagingRoom.ok == PEERTrue && isHostPresent)
					{
						// Woohoo!  On to our next screen!
						buttonPushed = true;
						nextScreen = "Menus/GameSpyGameOptionsMenu.wnd";
						TheShell->pop();
					}
					else
					{
						UnicodeString s;

						switch(resp.joinStagingRoom.result)
						{
						case PEERFullRoom:        // The room is full.
							s = TheGameText->fetch("GUI:JoinFailedRoomFull");
							break;
						case PEERInviteOnlyRoom:  // The room is invite only.
							s = TheGameText->fetch("GUI:JoinFailedInviteOnly");
							break;
						case PEERBannedFromRoom:  // The local user is banned from the room.
							s = TheGameText->fetch("GUI:JoinFailedBannedFromRoom");
							break;
						case PEERBadPassword:     // An incorrect password (or none) was given for a passworded room.
							s = TheGameText->fetch("GUI:JoinFailedBadPassword");
							break;
						case PEERAlreadyInRoom:   // The local user is already in or entering a room of the same type.
							s = TheGameText->fetch("GUI:JoinFailedAlreadyInRoom");
							break;
						case PEERNoConnection:    // Can't join a room if there's no chat connection.
							s = TheGameText->fetch("GUI:JoinFailedNoConnection");
							break;
						default:
							s = TheGameText->fetch("GUI:JoinFailedDefault");
							break;
						}
						GSMessageBoxOk(TheGameText->fetch("GUI:JoinFailedDefault"), s);
						if (groupRoomToJoin)
						{
							DEBUG_LOG(("WOLLobbyMenuUpdate() - rejoining group room %d\n", groupRoomToJoin));
							TheGameSpyInfo->joinGroupRoom(groupRoomToJoin);
							groupRoomToJoin = 0;
						}
						else
						{
							DEBUG_LOG(("WOLLobbyMenuUpdate() - joining best group room\n"));
							TheGameSpyInfo->joinBestGroupRoom();
						}
					}
				}
				break;
			case PeerResponse::PEERRESPONSE_STAGINGROOMLISTCOMPLETE:
				TheGameSpyInfo->sawFullGameList();
				break;
			case PeerResponse::PEERRESPONSE_STAGINGROOM:
				{
					GameSpyStagingRoom room;
					switch(resp.stagingRoom.action)
					{
					case PEER_CLEAR:
						TheGameSpyInfo->clearStagingRoomList();
						//TheGameSpyInfo->addText( UnicodeString(L"gameList: PEER_CLEAR"), GameSpyColor[GSCOLOR_DEFAULT], listboxLobbyChat );
						break;
					case PEER_ADD:
					case PEER_UPDATE:
					{
						if (resp.stagingRoom.percentComplete == 100)
						{
							TheGameSpyInfo->sawFullGameList();
						}

						//if (ParseAsciiStringToGameInfo(&room, resp.stagingRoomMapName.c_str()))
						//if (ParseAsciiStringToGameInfo(&room, resp.stagingServerGameOptions.c_str()))
						Bool serverOk = TRUE;
						if (!resp.stagingRoomMapName.length())
						{
							serverOk = FALSE;
						}
						// fix for ghost game problem - need to iterate over all resp.stagingRoomPlayerNames[i]
						Bool sawSelf = FALSE;
						//for (Int i=0; i<MAX_SLOTS; ++i)
						//{
							if (TheGameSpyInfo->getLocalName() == resp.stagingRoomPlayerNames[0].c_str())
							{
								sawSelf = TRUE; // don't show ghost games for myself
							}
						//}
						if (sawSelf)
							serverOk = FALSE;

						if (serverOk)
						{
							room.setGameName(UnicodeString(resp.stagingServerName.c_str()));
							room.setID(resp.stagingRoom.id);
							room.setHasPassword(resp.stagingRoom.requiresPassword);
							room.setVersion(resp.stagingRoom.version);
							room.setExeCRC(resp.stagingRoom.exeCRC);
							room.setIniCRC(resp.stagingRoom.iniCRC);
							room.setAllowObservers(resp.stagingRoom.allowObservers);
              room.setUseStats(resp.stagingRoom.useStats);
							room.setPingString(resp.stagingServerPingString.c_str());
							room.setLadderIP(resp.stagingServerLadderIP.c_str());
							room.setLadderPort(resp.stagingRoom.ladderPort);
							room.setReportedNumPlayers(resp.stagingRoom.numPlayers);
							room.setReportedMaxPlayers(resp.stagingRoom.maxPlayers);
							room.setReportedNumObservers(resp.stagingRoom.numObservers);
							// a started game stays listed, as Playing, which no Join can take
							room.setGameInProgress(!resp.stagingRoom.isStaging);

							Int i;
							AsciiString gsMapName = resp.stagingRoomMapName.c_str();
							AsciiString mapName = "";
							for (i=0; i<gsMapName.getLength(); ++i)
							{
								char c = gsMapName.getCharAt(i);
								if (c != '/')
									mapName.concat(c);
								else
									mapName.concat('\\');
							}
							room.setMap(TheGameState->portableMapPathToRealMapPath(mapName));

							Int numPlayers = 0;
							for (i=0; i<MAX_SLOTS; ++i)
							{
								GameSpyGameSlot *slot = room.getGameSpySlot(i);
								if (slot)
								{
									slot->setWins( resp.stagingRoom.wins[i] );
									slot->setLosses( resp.stagingRoom.losses[i] );
									slot->setProfileID( resp.stagingRoom.profileID[i] );
									slot->setPlayerTemplate( resp.stagingRoom.faction[i] );
									slot->setColor( resp.stagingRoom.color[i] );
									// Every AI seat the ladder has, asked in one place: an advertised seat this list did not
									// recognise used to show up in the browser as an empty one.
									const SlotState listed = ListedSeatState(resp.stagingRoomPlayerNames[i].c_str(), resp.stagingRoom.profileID[i]);
									if (IsAISlotState(listed))
									{
										slot->setState(listed);
										++numPlayers;
									}
									else if (listed == SLOT_PLAYER)
									{
										UnicodeString nameUStr;
										nameUStr.translate(resp.stagingRoomPlayerNames[i].c_str());
										slot->setState(SLOT_PLAYER, nameUStr);
										++numPlayers;
									}
									else
									{
										slot->setState(SLOT_OPEN);
									}
								}
							}
							DEBUG_ASSERTCRASH(numPlayers, ("Game had no players!\n"));
							//DEBUG_LOG(("Saw room: hasPass=%d, allowsObservers=%d\n", room.getHasPassword(), room.getAllowObservers()));
							if (resp.stagingRoom.action == PEER_ADD)
							{
								TheGameSpyInfo->addStagingRoom(room);
								//TheGameSpyInfo->addText( UnicodeString(L"gameList: PEER_ADD"), GameSpyColor[GSCOLOR_DEFAULT], listboxLobbyChat );
							}
							else
							{
								TheGameSpyInfo->updateStagingRoom(room);
								//TheGameSpyInfo->addText( UnicodeString(L"gameList: PEER_UPDATE"), GameSpyColor[GSCOLOR_DEFAULT], listboxLobbyChat );
							}
						}
						else
						{
							room.setID(resp.stagingRoom.id);
							TheGameSpyInfo->removeStagingRoom(room);
							//TheGameSpyInfo->addText( UnicodeString(L"gameList: PEER_UPDATE FAILED"), GameSpyColor[GSCOLOR_DEFAULT], listboxLobbyChat );
						}
						break;
					}
					case PEER_REMOVE:
						room.setID(resp.stagingRoom.id);
						TheGameSpyInfo->removeStagingRoom(room);
						//TheGameSpyInfo->addText( UnicodeString(L"gameList: PEER_REMOVE"), GameSpyColor[GSCOLOR_DEFAULT], listboxLobbyChat );
						break;
					default:
						//TheGameSpyInfo->addText( UnicodeString(L"gameList: Unknown"), GameSpyColor[GSCOLOR_DEFAULT], listboxLobbyChat );
						break;
					}
				}
				break;
			}
		}
#if 0
		if (shouldRepopulatePlayers)
		{
			PopulateLobbyPlayerListbox();
		}
#else
		refreshPlayerList();
#endif

#ifdef PERF_TEST
		// check performance
		end = Clock_Milliseconds();
		PERF_LOG(("Frame time was %d ms\n", end-start));
		std::list<Int>::const_iterator it;
		for (it = responses.begin(); it != responses.end(); ++it)
		{
			PERF_LOG(("  %s\n", getMessageString(*it)));
		}
		PERF_LOG(("\n"));
#endif // PERF_TEST

#if 0
// Removed 2-17-03 to pull out into a function so we can do the same checks 
		Int refreshInterval = gameListRefreshInterval;

		if ((gameListRefreshTime == 0) || ((gameListRefreshTime + refreshInterval) <= Clock_Milliseconds()))
		{
			if (TheGameSpyInfo->hasStagingRoomListChanged())
			{
				//DEBUG_LOG(("################### refreshing game list\n"));
				//DEBUG_LOG(("gameRefreshTime=%d, refreshInterval=%d, now=%d\n", gameListRefreshTime, refreshInterval, timeGetTime()));
				RefreshGameListBoxes();
				gameListRefreshTime = Clock_Milliseconds();
			} else {
				//DEBUG_LOG(("-"));
			}
		} else {
			//DEBUG_LOG(("gameListRefreshTime: %d refreshInterval: %d\n"));
		}
#else
	refreshGameList();
#endif
		if (!buttonPushed)
			onlineLobbyScript();
	}
}// WOLLobbyMenuUpdate

//-------------------------------------------------------------------------------------------------
/** WOL Lobby Menu input callback */
//-------------------------------------------------------------------------------------------------
WindowMsgHandledType WOLLobbyMenuInput( GameWindow *window, UnsignedInt msg,
																			 WindowMsgData mData1, WindowMsgData mData2 )
{
	switch( msg ) 
	{

		// --------------------------------------------------------------------------------------------
		case GWM_CHAR:
		{
			UnsignedByte key = mData1;
			UnsignedByte state = mData2;
			if (buttonPushed)
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

			break;

		}  // end char

		// --------------------------------------------------------------------------------------------
		// a click on a chat room's tab, which nothing under it took (drawRoomTabs drew it)
		case GWM_LEFT_UP:
		{
			if (buttonPushed || s_tryingToHostOrJoin)
				break;
			const Int x = LOLONGTOSHORT( mData1 );
			const Int y = HILONGTOSHORT( mData1 );
			for (Int tab = 0; tab < s_roomTabsShown; ++tab)
			{
				const IRegion2D &rect = s_roomTabRect[ tab ];
				if (x >= rect.lo.x && x < rect.hi.x && y >= rect.lo.y && y < rect.hi.y)
				{
					joinRoom( s_roomTabGroup[ tab ] );
					return MSG_HANDLED;
				}
			}
			break;
		}

	}  // end switch( msg )

	return MSG_IGNORED;
}// WOLLobbyMenuInput

//static void doSliderTrack(GameWindow *control, Int val)
//{
//	Int sliderW, sliderH, sliderX, sliderY;
//	control->winGetPosition(&sliderX, &sliderY);
//	control->winGetSize(&sliderW, &sliderH);
//	Real cursorY = sliderY + (100-val)*0.01f*sliderH;
//
//	extern GameWindow *listboxLobbyGamesSmall;
//	extern GameWindow *listboxLobbyGamesLarge;
//	extern GameWindow *listboxLobbyGameInfo;
//
//	static Int gwsX = 0, gwsY = 0, gwsW = 0, gwsH = 0;
//	static Int gwlX = 0, gwlY = 0, gwlW = 0, gwlH = 0;
//	static Int gwiX = 0, gwiY = 0, gwiW = 0, gwiH = 0;
//	static Int pwX = 0, pwY = 0, pwW = 0, pwH = 0;
//	static Int chatPosX = 0, chatPosY = 0, chatW = 0, chatH = 0;
//	static Int spacing = 0;
//	if (chatPosX == 0)
//	{
//		listboxLobbyChat->winGetPosition(&chatPosX, &chatPosY);
//		listboxLobbyChat->winGetSize(&chatW, &chatH);
//
////		listboxLobbyGamesSmall->winGetPosition(&gwsX, &gwsY);
////		listboxLobbyGamesSmall->winGetSize(&gwsW, &gwsH);
//
//		listboxLobbyGamesLarge->winGetPosition(&gwlX, &gwlY);
//		listboxLobbyGamesLarge->winGetSize(&gwlW, &gwlH);
//
////		listboxLobbyGameInfo->winGetPosition(&gwiX, &gwiY);
////		listboxLobbyGameInfo->winGetSize(&gwiW, &gwiH);
////
//		listboxLobbyPlayers->winGetPosition(&pwX, &pwY);
//		listboxLobbyPlayers->winGetSize(&pwW, &pwH);
//
//		spacing = chatPosY - pwY - pwH;
//	}
//
//	Int newChatY = cursorY;
//	Int newChatH = chatH + chatPosY - newChatY;
//	listboxLobbyChat->winSetPosition(chatPosX, newChatY);
//	listboxLobbyChat->winSetSize(chatW, newChatH);
//
//	Int newH = cursorY - pwY - spacing;
//	listboxLobbyPlayers->winSetSize(pwW, newH);
////	listboxLobbyGamesSmall->winSetSize(gwsW, newH);
//	listboxLobbyGamesLarge->winSetSize(gwlW, newH);
////	listboxLobbyGameInfo->winSetSize(gwiW, newH);


//-------------------------------------------------------------------------------------------------
/** WOL Lobby Menu window system callback */
//-------------------------------------------------------------------------------------------------
WindowMsgHandledType WOLLobbyMenuSystem( GameWindow *window, UnsignedInt msg, 
														 WindowMsgData mData1, WindowMsgData mData2 )
{
	UnicodeString txtInput;
	static NameKeyType buttonGameListTypeToggleID = NAMEKEY_INVALID;

	switch( msg )
	{
		
		
		//---------------------------------------------------------------------------------------------
		case GWM_CREATE:
			{
				buttonGameListTypeToggleID = NAMEKEY("WOLCustomLobby.wnd:ButtonGameListToggle");
//				sliderChatAdjustID = NAMEKEY("WOLCustomLobby.wnd:SliderChatAdjust");
				
				break;
			} // case GWM_DESTROY:

		//---------------------------------------------------------------------------------------------
		case GWM_DESTROY:
			{
				break;
			} // case GWM_DESTROY:

		//---------------------------------------------------------------------------------------------
		case GWM_INPUT_FOCUS:
			{	
				// if we're givin the opportunity to take the keyboard focus we must say we want it
				if( mData1 == TRUE )
					*(Bool *)mData2 = TRUE;

				return MSG_HANDLED;
			}//case GWM_INPUT_FOCUS:

		//---------------------------------------------------------------------------------------------
		case GLM_SELECTED:
			{
				GameWindow *control = (GameWindow *)mData1;
				Int controlID = control->winGetWindowId();
				if ( controlID == GetGameListBoxID() )
				{
					int rowSelected = mData2;
					if( rowSelected >= 0 )
					{
						buttonJoin->winEnable(!gameRowPlaying(control, rowSelected));
						static UnsignedInt lastFrame = 0;
						static Int lastID = -1;
						UnsignedInt now = TheGameClient->getFrame();

						PeerRequest req;
						req.peerRequestType = PeerRequest::PEERREQUEST_GETEXTENDEDSTAGINGROOMINFO;
						req.stagingRoom.id = (Int)(intptr_t)GadgetListBoxGetItemData(control, rowSelected, 0);

						if (lastID != req.stagingRoom.id || now > lastFrame + ConvertRetailFramesWall( 60U ))
						{
							TheGameSpyPeerMessageQueue->addRequest(req);
						}

						lastID = req.stagingRoom.id;
						lastFrame = now;
					}
					else
					{
						buttonJoin->winEnable(FALSE);
					}
					if (GetGameInfoListBox())
					{
						RefreshGameInfoListBox(GetGameListBox(), GetGameInfoListBox());
					}
				} //if ( controlID == GetGameListBoxID() )

				break;
			}

		//---------------------------------------------------------------------------------------------
		case GBM_SELECTED:
			{
				if (buttonPushed)
					break;

				GameWindow *control = (GameWindow *)mData1;
				Int controlID = control->winGetWindowId();

				if (UIStyle::onlineTabClicked(control, leaveLobby))
					break;
				if (HandleSortButton((NameKeyType)controlID))
					break;

				// If we back out, just bail - we haven't gotten far enough to need to log out
				if ( controlID == buttonBackID )
				{
					leaveLobby( UIStyle::ONLINE_TAB_HOME );
				} //if ( controlID == buttonBack )
				else if ( controlID == buttonRefreshID )
				{
					// Added 2/17/03 added the game refresh button
					refreshGameList(TRUE);	
					refreshPlayerList(TRUE);
				}
				else if ( controlID == buttonHostID )
				{
					if (s_tryingToHostOrJoin)
						break;

					SetLobbyAttemptHostJoin( TRUE );
					TheLobbyQueuedUTMs.clear();
					groupRoomToJoin = TheGameSpyInfo->getCurrentGroupRoom();
					GameSpyOpenOverlay(GSOVERLAY_GAMEOPTIONS);
				}
				else if ( controlID == buttonJoinID )
				{
					if (s_tryingToHostOrJoin)
						break;

					TheLobbyQueuedUTMs.clear();
					// Look for a game to join
					groupRoomToJoin = TheGameSpyInfo->getCurrentGroupRoom();
					Int selected;
					GadgetListBoxGetSelected(GetGameListBox(), &selected);
					if (selected >= 0)
					{
						Int selectedID = (Int)(intptr_t)GadgetListBoxGetItemData(GetGameListBox(), selected);
						if (selectedID > 0)
						{
							StagingRoomMap *srm = TheGameSpyInfo->getStagingRoomList();
							StagingRoomMap::iterator srmIt = srm->find(selectedID);
							if (srmIt != srm->end())
							{
								GameSpyStagingRoom *roomToJoin = srmIt->second;
								if (roomToJoin && roomToJoin->isGameInProgress())
									break;	// a double click reaches here past the disabled button
								if (!roomToJoin || roomToJoin->getExeCRC() != TheGlobalData->m_exeCRC || roomToJoin->getIniCRC() != TheGlobalData->m_iniCRC)
								{
									// bad crc.  don't go.
									DEBUG_LOG(("WOLLobbyMenuSystem - CRC mismatch with the game I'm trying to join. My CRC's - EXE:0x%08X INI:0x%08X  Their CRC's - EXE:0x%08x INI:0x%08x\n", TheGlobalData->m_exeCRC, TheGlobalData->m_iniCRC, roomToJoin->getExeCRC(), roomToJoin->getIniCRC()));
#if defined(_DEBUG) || defined(_INTERNAL)
									if (TheGlobalData->m_netMinPlayers)
									{
										GSMessageBoxOk(TheGameText->fetch("GUI:JoinFailedDefault"), TheGameText->fetch("GUI:JoinFailedCRCMismatch"));
										break;
									}
									else if (g_fakeCRC)
									{
										TheWritableGlobalData->m_exeCRC = roomToJoin->getExeCRC();
										TheWritableGlobalData->m_iniCRC = roomToJoin->getIniCRC();
									}
#else
									GSMessageBoxOk(TheGameText->fetch("GUI:JoinFailedDefault"), TheGameText->fetch(joinRefusalLabel(roomToJoin)));
									break;
#endif
								}
								Bool unknownLadder = (roomToJoin->getLadderPort() && TheLadderList->findLadder(roomToJoin->getLadderIP(), roomToJoin->getLadderPort()) == NULL);
								if (unknownLadder)
								{
									GSMessageBoxOk(TheGameText->fetch("GUI:JoinFailedDefault"), TheGameText->fetch("GUI:JoinFailedUnknownLadder"));
									break;
								}
								if (LobbyGameFull(roomToJoin))		// the row's own Full, by the listing's numbers
								{
									GSMessageBoxOk(TheGameText->fetch("GUI:JoinFailedDefault"), TheGameText->fetch("GUI:JoinFailedRoomFull"));
									break;
								}
								TheGameSpyInfo->markAsStagingRoomJoiner(selectedID);
								TheGameSpyGame->setGameName(roomToJoin->getGameName());
								TheGameSpyGame->setLadderIP(roomToJoin->getLadderIP());
								TheGameSpyGame->setLadderPort(roomToJoin->getLadderPort());
								SetLobbyAttemptHostJoin( TRUE );
								LobbySetJoiningGame( selectedID );
								if (roomToJoin->getHasPassword())
								{
									GameSpyOpenOverlay(GSOVERLAY_GAMEPASSWORD);
								}
								else
								{
									// no password - just join it
									PeerRequest req;
									req.peerRequestType = PeerRequest::PEERREQUEST_JOINSTAGINGROOM;
									req.text = srmIt->second->getGameName().str();
									req.stagingRoom.id = selectedID;
									req.password = "";
									TheGameSpyPeerMessageQueue->addRequest(req);
								}
							}
						}
						else
						{
							GSMessageBoxOk(TheGameText->fetch("GUI:Error"), TheGameText->fetch("GUI:NoGameInfo"), NULL);
						}
					}
					else
					{
						GSMessageBoxOk(TheGameText->fetch("GUI:Error"), TheGameText->fetch("GUI:NoGameSelected"), NULL);
					}
				}
				else if ( controlID == buttonBuddyID )
				{
					GameSpyToggleOverlay( GSOVERLAY_BUDDY );
				}
				else if ( controlID == buttonGameListTypeToggleID )
				{
					ToggleGameListType();
				}
				else if ( controlID == buttonEmoteID )
				{
				// read the user's input and clear the entry box
					UnicodeString txtInput;
					txtInput.set(GadgetTextEntryGetText( textEntryChat ));
					GadgetTextEntrySetText(textEntryChat, UnicodeString::TheEmptyString);
					txtInput.trim();
					if (!txtInput.isEmpty())
					{
						// Send the message
						TheGameSpyInfo->sendChat( txtInput, FALSE, listboxLobbyPlayers ); // 'emote' button now just sends text
					}
				}
				
				break;
			}// case GBM_SELECTED:

		//---------------------------------------------------------------------------------------------
		case GCM_SELECTED:
			{
				if (s_tryingToHostOrJoin)
					break;
				GameWindow *control = (GameWindow *)mData1;
				Int controlID = control->winGetWindowId();
				if( controlID == comboLobbyGroupRoomsID ) 
				{
					int rowSelected = -1;
					GadgetComboBoxGetSelectedPos(control, &rowSelected);
				
					DEBUG_LOG(("Row selected = %d\n", rowSelected));
					if (rowSelected >= 0)
					{
						Int groupID;
						groupID = (Int)(intptr_t)GadgetComboBoxGetItemData(comboLobbyGroupRooms, rowSelected);
						DEBUG_LOG(("ItemData was %d, current Group Room is %d\n", groupID, TheGameSpyInfo->getCurrentGroupRoom()));
						joinRoom(groupID);
					}
				}
			} // case GCM_SELECTED
			break;

		//---------------------------------------------------------------------------------------------
		case GLM_DOUBLE_CLICKED:
			{
				if (buttonPushed)
					break;

				GameWindow *control = (GameWindow *)mData1;
				Int controlID = control->winGetWindowId();
				if (controlID == GetGameListBoxID())
				{
					int rowSelected = mData2;
				
					if (rowSelected >= 0)
					{
						GadgetListBoxSetSelected( control, rowSelected );
						GameWindow *button = TheWindowManager->winGetWindowFromId( window, buttonJoinID );

						TheWindowManager->winSendSystemMsg( window, GBM_SELECTED, 
																								(WindowMsgData)button, buttonJoinID );
					}
				}
				break;
			}// case GLM_DOUBLE_CLICKED:

		//---------------------------------------------------------------------------------------------
		case GLM_RIGHT_CLICKED:
			{
				GameWindow *control = (GameWindow *)mData1;
				Int controlID = control->winGetWindowId();

				if( controlID == listboxLobbyPlayersID ) 
				{
					RightClickStruct *rc = (RightClickStruct *)mData2;
					WindowLayout *rcLayout = NULL;
					GameWindow *rcMenu;
					if(rc->pos < 0)
					{
						GadgetListBoxSetSelected(control, -1);
						break;
					}

					GPProfile profileID = 0;
					AsciiString aName = nickInRow(control, rc->pos);
					PlayerInfoMap::iterator it = TheGameSpyInfo->getPlayerInfoMap()->find(aName);
					if (it != TheGameSpyInfo->getPlayerInfoMap()->end())
						profileID = it->second.m_profileID;

					Bool isBuddy = FALSE;
					if (profileID <= 0)
						rcLayout = TheWindowManager->winCreateLayout(AsciiString("Menus/RCNoProfileMenu.wnd"));	
					else
					{
						if (profileID == TheGameSpyInfo->getLocalProfileID())
						{
							rcLayout = TheWindowManager->winCreateLayout(AsciiString("Menus/RCLocalPlayerMenu.wnd"));
						}
						else if(TheGameSpyInfo->isBuddy(profileID))
						{
							rcLayout = TheWindowManager->winCreateLayout(AsciiString("Menus/RCBuddiesMenu.wnd"));
							isBuddy = TRUE;
						}
						else
							rcLayout = TheWindowManager->winCreateLayout(AsciiString("Menus/RCNonBuddiesMenu.wnd"));
					}
					if(!rcLayout)
						break;
					
					GadgetListBoxSetSelected(control, rc->pos);
					
					rcMenu = rcLayout->getFirstWindow();
					rcMenu->winGetLayout()->runInit();
					rcMenu->winBringToTop();
					rcMenu->winHide(FALSE);
					setUnignoreText( rcLayout, aName, profileID);
					ICoord2D rcSize, rcPos;
					rcMenu->winGetSize(&rcSize.x, &rcSize.y);
					rcPos.x = rc->mouseX;
					rcPos.y = rc->mouseY;
					if(rc->mouseX + rcSize.x > TheDisplay->getWidth())
						rcPos.x = TheDisplay->getWidth() - rcSize.x;
					if(rc->mouseY + rcSize.y > TheDisplay->getHeight())
						rcPos.y = TheDisplay->getHeight() - rcSize.y;
					rcMenu->winSetPosition(rcPos.x, rcPos.y);
					
					GameSpyRCMenuData *rcData = NEW GameSpyRCMenuData;
					rcData->m_id = profileID;
					rcData->m_nick = aName;
					rcData->m_itemType = (isBuddy)?ITEM_BUDDY:ITEM_NONBUDDY;
					rcMenu->winSetUserData((void *)rcData);
					TheWindowManager->winSetLoneWindow(rcMenu);
				}
				else if( controlID == GetGameListBoxID() ) 
				{
					RightClickStruct *rc = (RightClickStruct *)mData2;
					WindowLayout *rcLayout = NULL;
					GameWindow *rcMenu;
					if(rc->pos < 0)
					{
						GadgetListBoxSetSelected(control, -1);
						break;
					}

					Int selectedID = (Int)(intptr_t)GadgetListBoxGetItemData(control, rc->pos);
					if (selectedID > 0)
					{
						StagingRoomMap *srm = TheGameSpyInfo->getStagingRoomList();
						StagingRoomMap::iterator srmIt = srm->find(selectedID);
						if (srmIt != srm->end())
						{
							GameSpyStagingRoom *theRoom = srmIt->second;
							if (!theRoom)
								break;
							const LadderInfo *linfo = TheLadderList->findLadder(theRoom->getLadderIP(), theRoom->getLadderPort());
							if (linfo)
							{
								rcLayout = TheWindowManager->winCreateLayout(AsciiString("Menus/RCGameDetailsMenu.wnd"));	
								if (!rcLayout)
									break;

								GadgetListBoxSetSelected(control, rc->pos);

								rcMenu = rcLayout->getFirstWindow();
								rcMenu->winGetLayout()->runInit();
								rcMenu->winBringToTop();
								rcMenu->winHide(FALSE);
								rcMenu->winSetPosition(rc->mouseX, rc->mouseY);
								
								rcMenu->winSetUserData((void *)(intptr_t)selectedID);
								TheWindowManager->winSetLoneWindow(rcMenu);
							}
						}
					}
				}
				break;
			}

//		//---------------------------------------------------------------------------------------------
//		case GSM_SLIDER_TRACK:
//		{
//				if (buttonPushed)
//					break;
//
//			GameWindow *control = (GameWindow *)mData1;
//			Int val = (Int)mData2;
//			Int controlID = control->winGetWindowId();
//			if (controlID == sliderChatAdjustID)
//			{
//				doSliderTrack(control, val);
//			}
//			break;
//		}

		//---------------------------------------------------------------------------------------------
		case GEM_EDIT_DONE:
			{
				if (buttonPushed)
					break;

				// read the user's input and clear the entry box
				UnicodeString txtInput;
				txtInput.set(GadgetTextEntryGetText( textEntryChat ));
				GadgetTextEntrySetText(textEntryChat, UnicodeString::TheEmptyString);
				txtInput.trim();
				if (!txtInput.isEmpty())
				{
					// Send the message
					if (!handleLobbySlashCommands(txtInput))
					{
						TheGameSpyInfo->sendChat( txtInput, false, listboxLobbyPlayers );
					}
				}
				break;
			}

		//---------------------------------------------------------------------------------------------
		default:
			return MSG_IGNORED;

	}//Switch

	return MSG_HANDLED;
}// WOLLobbyMenuSystem
