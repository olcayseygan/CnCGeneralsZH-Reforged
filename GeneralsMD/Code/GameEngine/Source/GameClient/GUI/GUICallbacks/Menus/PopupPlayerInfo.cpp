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
// FILE: PopupPlayerInfo.cpp
// Author: Matthew D. Campbell, July 2002
// Description: Player info right-click popup screen
///////////////////////////////////////////////////////////////////////////////////////

// INCLUDES ///////////////////////////////////////////////////////////////////////////////////////
#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine
#include "Platform/IsWindows9x.h"

#include <algorithm>

#include "Common/PlayerTemplate.h"
#include "Common/BattleHonors.h"
#include "Common/CustomMatchPreferences.h"
#include "Common/GameSpyMiscPreferences.h"
#include "Common/FileSystem.h"
#include "Common/NameKeyGenerator.h"
#include "Common/RandomMapGenerator.h"
#include "GameClient/Mouse.h"
#include "GameClient/GameText.h"
#include "GameClient/WindowLayout.h"
#include "GameClient/Gadget.h"
#include "GameClient/KeyDefs.h"
#include "GameClient/GameWindowManager.h"
#include "GameClient/GadgetListBox.h"
#include "GameClient/GadgetCheckBox.h"
#include "GameClient/GadgetProgressBar.h"
#include "GameClient/GadgetPushButton.h"
#include "GameClient/GadgetStaticText.h"
#include "GameClient/Display.h"
#include "GameClient/DisplayString.h"
#include "GameClient/DisplayStringManager.h"
#include "GameClient/GameFont.h"
#include "GameClient/Image.h"
#include "GameClient/MapUtil.h"
#include "GameClient/UIStyle.h"
#include "GameClient/MessageBox.h"
#include "GameNetwork/GameSpyOverlay.h"
#include "GameNetwork/GameSpy/PeerDefs.h"
#include "GameNetwork/GameSpy/StagingRoomGameInfo.h"
#include "GameNetwork/GameSpy/PeerThread.h"
#include "GameNetwork/GameSpy/PersistentStorageDefs.h"
#include "GameNetwork/GameSpy/PersistentStorageThread.h"
#include "GameNetwork/RankPointValue.h"
#include "GameNetwork/GameSpy/BuddyThread.h"
#include "GameNetwork/GameSpy/GSConfig.h"
#include "GameNetwork/GameSpy/LobbyUtils.h"
#include "GameNetwork/Online/OnlineQueues.h"
#include "Lib/WideCharFns.h"

#include "WWDownload/Registry.h"

#ifdef _INTERNAL
// for occasional debugging...
//#pragma optimize("", off)
//#pragma MESSAGE("************************************** WARNING, optimization disabled for debugging purposes")
#endif

// PRIVATE DATA ///////////////////////////////////////////////////////////////////////////////////
// window ids ------------------------------------------------------------------------------
static NameKeyType parentID = NAMEKEY_INVALID;
static NameKeyType listboxInfoID = NAMEKEY_INVALID;
static NameKeyType buttonCloseID = NAMEKEY_INVALID;
static NameKeyType buttonBuddiesID = NAMEKEY_INVALID;
//static NameKeyType buttonOptionsID = NAMEKEY_INVALID;
static NameKeyType buttonSetLocaleID = NAMEKEY_INVALID;
static NameKeyType buttonDeleteAccountID = NAMEKEY_INVALID;
static NameKeyType checkBoxAsianFontID = NAMEKEY_INVALID;
static NameKeyType checkBoxNonAsianFontID = NAMEKEY_INVALID;

// Window Pointers ------------------------------------------------------------------------
static GameWindow *parent = NULL;
static GameWindow *listboxInfo = NULL;
static GameWindow *buttonClose = NULL;
static GameWindow *buttonBuddies = NULL;
//static GameWindow *buttonbuttonOptions = NULL;
static GameWindow *buttonSetLocale = NULL;
static GameWindow *buttonDeleteAccount = NULL;
static GameWindow *checkBoxAsianFont = NULL;
static GameWindow *checkBoxNonAsianFont = NULL;

static Bool isOverlayActive = false;
static Bool raiseMessageBox = false;
static Int lookAtPlayerID = 0;
static std::string lookAtPlayerName;
static UnsignedInt s_onlineInfoSerial = 0;		///< the looked-at player's card serial when the popup last drew


static Int getTotalDisconnectsFromFile(Int playerID)
{
	Int retval = 0;
	if (playerID == 0)
	{
		return 0;
	}

	UserPreferences pref;
	AsciiString userPrefFilename;
	userPrefFilename.format("GeneralsOnline\\MiscPref%d.ini", playerID);
	DEBUG_LOG(("getTotalDisconnectsFromFile - reading stats from file %s\n", userPrefFilename.str()));
	pref.load(userPrefFilename);

	// if there is a file override, use that data instead.
	if (pref.find("0") != pref.end()) {
		retval = atoi(pref.find("0")->second.str());
	}
	if (pref.find("1") != pref.end()) {
		retval += atoi(pref.find("1")->second.str());
	}
	if (pref.find("2") != pref.end()) {
		retval += atoi(pref.find("2")->second.str());
	}
	if (pref.find("3") != pref.end()) {
		retval += atoi(pref.find("3")->second.str());
	}
	if (pref.find("4") != pref.end()) {
		retval += atoi(pref.find("4")->second.str());
	}
	if (pref.find("5") != pref.end()) {
		retval += atoi(pref.find("5")->second.str());
	}

	return retval;	
}

Int GetAdditionalDisconnectsFromUserFile(Int playerID)
{
	Int retval = getTotalDisconnectsFromFile(playerID);

	if (playerID == 0) {
		return 0;
	}

	if (TheGameSpyInfo->getAdditionalDisconnects() > 0 && !retval)
	{
		DEBUG_LOG(("Clearing additional disconnects\n"));
		TheGameSpyInfo->clearAdditionalDisconnects();
	}

	if (TheGameSpyInfo->getAdditionalDisconnects() != -1)
	{
		return TheGameSpyInfo->getAdditionalDisconnects();
	}

	return retval;
}

void GetAdditionalDisconnectsFromUserFile(PSPlayerStats *stats)
{
	if (!stats || stats->id == 0) {
		return;
	}

	if (TheGameSpyInfo->getAdditionalDisconnects() > 0 && !getTotalDisconnectsFromFile(stats->id))
	{
		DEBUG_LOG(("Clearing additional disconnects\n"));
		TheGameSpyInfo->clearAdditionalDisconnects();
	}

	if (TheGameSpyInfo->getAdditionalDisconnects() < 1)
	{
		return;
	}

	UserPreferences pref;
	AsciiString userPrefFilename;
	userPrefFilename.format("GeneralsOnline\\MiscPref%d.ini", stats->id);
	DEBUG_LOG(("GetAdditionalDisconnectsFromUserFile - reading stats from file %s\n", userPrefFilename.str()));
	pref.load(userPrefFilename);

	// if there is a file override, use that data instead.
	if (pref.find("0") != pref.end()) {
		stats->desyncs[2] += abs(atoi(pref.find("0")->second.str()));
	}
	if (pref.find("1") != pref.end()) {
		stats->desyncs[3] += abs(atoi(pref.find("1")->second.str()));
	}
	if (pref.find("2") != pref.end()) {
		stats->desyncs[4] += abs(atoi(pref.find("2")->second.str()));
	}
	if (pref.find("3") != pref.end()) {
		stats->discons[2] += abs(atoi(pref.find("3")->second.str()));
	}
	if (pref.find("4") != pref.end()) {
		stats->discons[3] += abs(atoi(pref.find("4")->second.str()));
	}
	if (pref.find("5") != pref.end()) {
		stats->discons[4] += abs(atoi(pref.find("5")->second.str()));
	}
}

// default values
RankPoints::RankPoints(void)
{
	m_ranks[RANK_PRIVATE]							= 0;
	m_ranks[RANK_CORPORAL]						= TheGameSpyConfig->getPointsForRank(RANK_CORPORAL); // 5
	m_ranks[RANK_SERGEANT]						= TheGameSpyConfig->getPointsForRank(RANK_SERGEANT); // 10
	m_ranks[RANK_LIEUTENANT]					= TheGameSpyConfig->getPointsForRank(RANK_LIEUTENANT); // 20
	m_ranks[RANK_CAPTAIN]							= TheGameSpyConfig->getPointsForRank(RANK_CAPTAIN); // 50
	m_ranks[RANK_MAJOR]								= TheGameSpyConfig->getPointsForRank(RANK_MAJOR); // 100
	m_ranks[RANK_COLONEL]							= TheGameSpyConfig->getPointsForRank(RANK_COLONEL); // 200
	m_ranks[RANK_BRIGADIER_GENERAL]		= TheGameSpyConfig->getPointsForRank(RANK_BRIGADIER_GENERAL); // 500
	m_ranks[RANK_GENERAL]							= TheGameSpyConfig->getPointsForRank(RANK_GENERAL); // 1000
	m_ranks[RANK_COMMANDER_IN_CHIEF]	= TheGameSpyConfig->getPointsForRank(RANK_COMMANDER_IN_CHIEF); // 2000

	m_winMultiplier = 3.0f;
	m_lostMultiplier = 0.0f;
	m_hourSpentOnlineMultiplier = 1.0f;
	m_completedSoloCampaigns = 5.0f;
	m_disconnectMultiplier = -1.0f;
}

RankPoints *TheRankPointValues = NULL;

void SetLookAtPlayer( Int id, AsciiString nick)
{
	lookAtPlayerID = id;
	lookAtPlayerName = nick.str();
}

//	BATTLE_HONOR_LADDER_CHAMP		= 0x0000001,
//	BATTLE_HONOR_STREAK					= 0x0000002,
//	BATTLE_HONOR_STREAK_5				= 0x0000004,
//	BATTLE_HONOR_STREAK_10			= 0x0000008,
//	BATTLE_HONOR_STREAK_20			= 0x0000010,
//	BATTLE_HONOR_LOYALTY_USA		= 0x0000020,
//	BATTLE_HONOR_LOYALTY_CHINA	= 0x0000040,
//	BATTLE_HONOR_LOYALTY_GLA		= 0x0000060,
//	BATTLE_HONOR_BATTLE_TANK		= 0x0000080,
//	BATTLE_HONOR_AIR_WING				= 0x0000100,
//	BATTLE_HONOR_SPECIAL_FORCES	= 0x0000200,
//	BATTLE_HONOR_ENDURANCE			= 0x0000400,
//	BATTLE_HONOR_CAMPAIGN_USA		= 0x0000800,
//	BATTLE_HONOR_CAMPAIGN_CHINA	= 0x0001000,
//	BATTLE_HONOR_CAMPAIGN_GLA  	= 0x0002000,
//	BATTLE_HONOR_BLITZ5					= 0x0004000,
//	BATTLE_HONOR_BLITZ10				= 0x0008000,
//	BATTLE_HONOR_DOMINATION			= 0x0010000,
//	BATTLE_HONOR_CHALLENGE			= 0x0020000,
//	BATTLE_HONOR_ULTIMATE				= 0x0040000,
//	BATTLE_HONOR_GLOBAL_GENERAL	= 0x0080000,
//	BATTLE_HONOR_DOMINATION_ONLINE = 0x0100000,
//	BATTLE_HONOR_SOLO_CHINA_G		= 0x0200000,
//	BATTLE_HONOR_SOLO_GLA_B			= 0x0400000,
//	BATTLE_HONOR_SOLO_GLA_S			= 0x0800000,
//	BATTLE_HONOR_SOLO_GLA_G			= 0x1000000,

void BattleHonorTooltip(GameWindow *window,
												WinInstanceData *instData,
												UnsignedInt mouse)
{
	Int x, y, row, col;
	x = LOLONGTOSHORT(mouse);
	y = HILONGTOSHORT(mouse);

	GadgetListBoxGetEntryBasedOnXY(window, x, y, row, col);

	if (row == -1 || col == -1)
	{
		TheMouse->setCursorTooltip( TheGameText->fetch("TOOLTIP:BattleHonors") );
		return;
	}

	Int battleHonor = (Int)(intptr_t)GadgetListBoxGetItemData( window, row, col );
	Int extraValue = (Int)(intptr_t)GadgetListBoxGetItemData( window, row - 1, col );
	if (battleHonor == 0)
	{
		//DEBUG_CRASH(("No Battle Honor in listbox row %d, col %d!", row, col));
		TheMouse->setCursorTooltip( TheGameText->fetch("TOOLTIP:BattleHonors") );
		return;
	}
	Real tooltipWidth = 1.5f;
	if (BitTest(battleHonor, BATTLE_HONOR_NOT_GAINED))
	{
		if(BitTest(battleHonor, BATTLE_HONOR_LOYALTY_USA))
			TheMouse->setCursorTooltip( TheGameText->fetch("TOOLTIP:BattleHonorLoyaltyUSADisabled"), -1, NULL, tooltipWidth );
		else if(BitTest(battleHonor, BATTLE_HONOR_LOYALTY_CHINA))
			TheMouse->setCursorTooltip( TheGameText->fetch("TOOLTIP:BattleHonorLoyaltyChinaDisabled"), -1, NULL, tooltipWidth );
		else if(BitTest(battleHonor, BATTLE_HONOR_LOYALTY_GLA))
			TheMouse->setCursorTooltip( TheGameText->fetch("TOOLTIP:BattleHonorLoyaltyGLADisabled"), -1, NULL, tooltipWidth );
		else if(BitTest(battleHonor, BATTLE_HONOR_BATTLE_TANK))
			TheMouse->setCursorTooltip( TheGameText->fetch("TOOLTIP:BattleHonorBattleTankDisabled"), -1, NULL, tooltipWidth );
		else if(BitTest(battleHonor, BATTLE_HONOR_AIR_WING))
			TheMouse->setCursorTooltip( TheGameText->fetch("TOOLTIP:BattleHonorAirWingDisabled"), -1, NULL, tooltipWidth );
		else if(BitTest(battleHonor, BATTLE_HONOR_ENDURANCE))
			TheMouse->setCursorTooltip( TheGameText->fetch("TOOLTIP:BattleHonorEnduranceDisabled"), -1, NULL, tooltipWidth );
		else if(BitTest(battleHonor, BATTLE_HONOR_CAMPAIGN_USA))
			TheMouse->setCursorTooltip( TheGameText->fetch("TOOLTIP:BattleHonorCampaignUSADisabled"), -1, NULL, tooltipWidth );
		else if(BitTest(battleHonor, BATTLE_HONOR_CAMPAIGN_CHINA))
			TheMouse->setCursorTooltip( TheGameText->fetch("TOOLTIP:BattleHonorCampaignChinaDisabled"), -1, NULL, tooltipWidth );
		else if(BitTest(battleHonor, BATTLE_HONOR_CAMPAIGN_GLA))
			TheMouse->setCursorTooltip( TheGameText->fetch("TOOLTIP:BattleHonorCampaignGLADisabled"), -1, NULL, tooltipWidth );
		else if(BitTest(battleHonor, BATTLE_HONOR_BLITZ10))
			TheMouse->setCursorTooltip( TheGameText->fetch("TOOLTIP:BattleHonorBlitzDisabled"), -1, NULL, tooltipWidth );
		else if(BitTest(battleHonor, BATTLE_HONOR_FAIR_PLAY))
			TheMouse->setCursorTooltip( TheGameText->fetch("TOOLTIP:BattleHonorFairPlayDisabled"), -1, NULL, tooltipWidth );
		else if(BitTest(battleHonor, BATTLE_HONOR_APOCALYPSE))
			TheMouse->setCursorTooltip( TheGameText->fetch("TOOLTIP:BattleHonorApocalypseDisabled"), -1, NULL, tooltipWidth );
		else if(BitTest(battleHonor, BATTLE_HONOR_CHALLENGE_MODE))
			TheMouse->setCursorTooltip( TheGameText->fetch("TOOLTIP:BattleHonorCampaignChallengeDisabled"), -1, NULL, tooltipWidth );
		else if(BitTest(battleHonor, BATTLE_HONOR_ULTIMATE))
			TheMouse->setCursorTooltip( TheGameText->fetch("TOOLTIP:BattleHonorUltimateDisabled"), -1, NULL, tooltipWidth );
		else if(BitTest(battleHonor, BATTLE_HONOR_GLOBAL_GENERAL))
			TheMouse->setCursorTooltip( TheGameText->fetch("TOOLTIP:BattleHonorGlobalGeneralDisabled"), -1, NULL, tooltipWidth );
		else if(BitTest(battleHonor, BATTLE_HONOR_CHALLENGE))
			TheMouse->setCursorTooltip( TheGameText->fetch("TOOLTIP:BattleHonorChallengeDisabled"), -1, NULL, tooltipWidth );
		else if(BitTest(battleHonor, BATTLE_HONOR_STREAK))
			TheMouse->setCursorTooltip( TheGameText->fetch("TOOLTIP:BattleHonorStreakDisabled"), -1, NULL, tooltipWidth );
		else if(BitTest(battleHonor, BATTLE_HONOR_STREAK_ONLINE))
			TheMouse->setCursorTooltip( TheGameText->fetch("TOOLTIP:BattleHonorStreakOnlineDisabled"), -1, NULL, tooltipWidth );
		else if(BitTest(battleHonor, BATTLE_HONOR_DOMINATION))
			TheMouse->setCursorTooltip( TheGameText->fetch("TOOLTIP:BattleHonorDominationDisabled"), -1, NULL, tooltipWidth );
		else if(BitTest(battleHonor, BATTLE_HONOR_DOMINATION_ONLINE))
			TheMouse->setCursorTooltip( TheGameText->fetch("TOOLTIP:BattleHonorDominationOnlineDisabled"), -1, NULL, tooltipWidth );
	}
	else
	{
		if(BitTest(battleHonor, BATTLE_HONOR_LOYALTY_USA))
			TheMouse->setCursorTooltip( TheGameText->fetch("TOOLTIP:BattleHonorLoyaltyUSA"), -1, NULL, tooltipWidth );
		else if(BitTest(battleHonor, BATTLE_HONOR_LOYALTY_CHINA))
			TheMouse->setCursorTooltip( TheGameText->fetch("TOOLTIP:BattleHonorLoyaltyChina"), -1, NULL, tooltipWidth );
		else if(BitTest(battleHonor, BATTLE_HONOR_LOYALTY_GLA))
			TheMouse->setCursorTooltip( TheGameText->fetch("TOOLTIP:BattleHonorLoyaltyGLA"), -1, NULL, tooltipWidth );
		else if(BitTest(battleHonor, BATTLE_HONOR_BATTLE_TANK))
			TheMouse->setCursorTooltip( TheGameText->fetch("TOOLTIP:BattleHonorBattleTank"), -1, NULL, tooltipWidth );
		else if(BitTest(battleHonor, BATTLE_HONOR_AIR_WING))
			TheMouse->setCursorTooltip( TheGameText->fetch("TOOLTIP:BattleHonorAirWing"), -1, NULL, tooltipWidth );
		else if(BitTest(battleHonor, BATTLE_HONOR_ENDURANCE))
			TheMouse->setCursorTooltip( TheGameText->fetch("TOOLTIP:BattleHonorEndurance"), -1, NULL, tooltipWidth );
		else if(BitTest(battleHonor, BATTLE_HONOR_CAMPAIGN_USA))
			TheMouse->setCursorTooltip( TheGameText->fetch("TOOLTIP:BattleHonorCampaignUSA"), -1, NULL, tooltipWidth );
		else if(BitTest(battleHonor, BATTLE_HONOR_CAMPAIGN_CHINA))
			TheMouse->setCursorTooltip( TheGameText->fetch("TOOLTIP:BattleHonorCampaignChina"), -1, NULL, tooltipWidth );
		else if(BitTest(battleHonor, BATTLE_HONOR_CAMPAIGN_GLA))
			TheMouse->setCursorTooltip( TheGameText->fetch("TOOLTIP:BattleHonorCampaignGLA"), -1, NULL, tooltipWidth );
		else if(BitTest(battleHonor, BATTLE_HONOR_BLITZ5))
			TheMouse->setCursorTooltip( TheGameText->fetch("TOOLTIP:BattleHonorBlitz5"), -1, NULL, tooltipWidth );
		else if(BitTest(battleHonor, BATTLE_HONOR_BLITZ10))
			TheMouse->setCursorTooltip( TheGameText->fetch("TOOLTIP:BattleHonorBlitz10"), -1, NULL, tooltipWidth );
		else if(BitTest(battleHonor, BATTLE_HONOR_FAIR_PLAY))
			TheMouse->setCursorTooltip( TheGameText->fetch("TOOLTIP:BattleHonorFairPlay"), -1, NULL, tooltipWidth );
		else if(BitTest(battleHonor, BATTLE_HONOR_APOCALYPSE))
			TheMouse->setCursorTooltip( TheGameText->fetch("TOOLTIP:BattleHonorApocalypse"), -1, NULL, tooltipWidth );
		else if(BitTest(battleHonor, BATTLE_HONOR_OFFICERSCLUB))
			TheMouse->setCursorTooltip( TheGameText->fetch("TOOLTIP:BattleHonorOfficersClub"), -1, NULL, tooltipWidth );
		else if(BitTest(battleHonor, BATTLE_HONOR_CHALLENGE_MODE))
			TheMouse->setCursorTooltip( TheGameText->fetch("TOOLTIP:BattleHonorCampaignChallenge"), -1, NULL, tooltipWidth );
		else if(BitTest(battleHonor, BATTLE_HONOR_ULTIMATE))
			TheMouse->setCursorTooltip( TheGameText->fetch("TOOLTIP:BattleHonorUltimate"), -1, NULL, tooltipWidth );
		else if(BitTest(battleHonor, BATTLE_HONOR_GLOBAL_GENERAL))
			TheMouse->setCursorTooltip( TheGameText->fetch("TOOLTIP:BattleHonorGlobalGeneral"), -1, NULL, tooltipWidth );
		else if(BitTest(battleHonor, BATTLE_HONOR_CHALLENGE))
			TheMouse->setCursorTooltip( TheGameText->fetch("TOOLTIP:BattleHonorChallenge"), -1, NULL, tooltipWidth );
		else if(BitTest(battleHonor, BATTLE_HONOR_STREAK))
		{
			if (extraValue >= 1000)
				TheMouse->setCursorTooltip( TheGameText->fetch("TOOLTIP:BattleHonorStreak1000"), -1, NULL, tooltipWidth );
			else if (extraValue >= 500)
				TheMouse->setCursorTooltip( TheGameText->fetch("TOOLTIP:BattleHonorStreak500"), -1, NULL, tooltipWidth );
			else if (extraValue >= 100)
				TheMouse->setCursorTooltip( TheGameText->fetch("TOOLTIP:BattleHonorStreak100"), -1, NULL, tooltipWidth );
			else if (extraValue >= 25)
				TheMouse->setCursorTooltip( TheGameText->fetch("TOOLTIP:BattleHonorStreak25"), -1, NULL, tooltipWidth );
			else if (extraValue >= 10)
				TheMouse->setCursorTooltip( TheGameText->fetch("TOOLTIP:BattleHonorStreak10"), -1, NULL, tooltipWidth );
			else if (extraValue >= 3)
				TheMouse->setCursorTooltip( TheGameText->fetch("TOOLTIP:BattleHonorStreak3"), -1, NULL, tooltipWidth );
			else
				TheMouse->setCursorTooltip( TheGameText->fetch("TOOLTIP:BattleHonorStreakDisabled"), -1, NULL, tooltipWidth );
		}
		else if(BitTest(battleHonor, BATTLE_HONOR_STREAK_ONLINE))
		{
			if (extraValue >= 1000)
				TheMouse->setCursorTooltip( TheGameText->fetch("TOOLTIP:BattleHonorStreak1000Online"), -1, NULL, tooltipWidth );
			else if (extraValue >= 500)
				TheMouse->setCursorTooltip( TheGameText->fetch("TOOLTIP:BattleHonorStreak500Online"), -1, NULL, tooltipWidth );
			else if (extraValue >= 100)
				TheMouse->setCursorTooltip( TheGameText->fetch("TOOLTIP:BattleHonorStreak100Online"), -1, NULL, tooltipWidth );
			else if (extraValue >= 25)
				TheMouse->setCursorTooltip( TheGameText->fetch("TOOLTIP:BattleHonorStreak25Online"), -1, NULL, tooltipWidth );
			else if (extraValue >= 10)
				TheMouse->setCursorTooltip( TheGameText->fetch("TOOLTIP:BattleHonorStreak10Online"), -1, NULL, tooltipWidth );
			else if (extraValue >= 3)
				TheMouse->setCursorTooltip( TheGameText->fetch("TOOLTIP:BattleHonorStreak3Online"), -1, NULL, tooltipWidth );
			else
				TheMouse->setCursorTooltip( TheGameText->fetch("TOOLTIP:BattleHonorStreakOnlineDisabled"), -1, NULL, tooltipWidth );
		}
		else if(BitTest(battleHonor, BATTLE_HONOR_DOMINATION))
		{
			if (extraValue >= 10000)
				TheMouse->setCursorTooltip( TheGameText->fetch("TOOLTIP:BattleHonorDomination10000"), -1, NULL, tooltipWidth );
			else if (extraValue >= 1000)
				TheMouse->setCursorTooltip( TheGameText->fetch("TOOLTIP:BattleHonorDomination1000"), -1, NULL, tooltipWidth );
			else if (extraValue >= 500)
				TheMouse->setCursorTooltip( TheGameText->fetch("TOOLTIP:BattleHonorDomination500"), -1, NULL, tooltipWidth );
			else if (extraValue >= 100)
				TheMouse->setCursorTooltip( TheGameText->fetch("TOOLTIP:BattleHonorDomination100"), -1, NULL, tooltipWidth );
			else
				TheMouse->setCursorTooltip( TheGameText->fetch("TOOLTIP:BattleHonorDominationDisabled"), -1, NULL, tooltipWidth );
		}
		else if(BitTest(battleHonor, BATTLE_HONOR_DOMINATION_ONLINE))
		{
			if (extraValue >= 10000)
				TheMouse->setCursorTooltip( TheGameText->fetch("TOOLTIP:BattleHonorDomination10000Online"), -1, NULL, tooltipWidth );
			else if (extraValue >= 1000)
				TheMouse->setCursorTooltip( TheGameText->fetch("TOOLTIP:BattleHonorDomination1000Online"), -1, NULL, tooltipWidth );
			else if (extraValue >= 500)
				TheMouse->setCursorTooltip( TheGameText->fetch("TOOLTIP:BattleHonorDomination500Online"), -1, NULL, tooltipWidth );
			else if (extraValue >= 100)
				TheMouse->setCursorTooltip( TheGameText->fetch("TOOLTIP:BattleHonorDomination100Online"), -1, NULL, tooltipWidth );
			else
				TheMouse->setCursorTooltip( TheGameText->fetch("TOOLTIP:BattleHonorDominationOnlineDisabled"), -1, NULL, tooltipWidth );
		}
	}
	
}

static Int rowsToSkip = 0;
void ResetBattleHonorInsertion(void)
{
	rowsToSkip = 0;
}
void InsertBattleHonor(GameWindow *list, const Image *image, Bool enabled, Int itemData, Int& row, Int& column, UnicodeString text = UnicodeString::TheEmptyString, Int extra = 0)
{
	// one scale for both axes.  Taken per axis, a 5120x1440 screen stretched the honor badge 6.4
	// wide and 2.4 tall and the medal came out an oval.
	const Real honorScale = UIScaleForScreen( TheDisplay->getWidth(), TheDisplay->getHeight() );
	Int width = MAX_BATTLE_HONOR_IMAGE_WIDTH * honorScale;
	Int height = MAX_BATTLE_HONOR_IMAGE_HEIGHT * honorScale;

	static Int enabledColor = 0xFFFFFFFF;
	static Int disabledColor = GameMakeColor(80, 80, 80, 255);
	Int color;
	if (enabled)
		color = enabledColor;
	else
		color = disabledColor;
	
	if (!enabled)
		itemData |= BATTLE_HONOR_NOT_GAINED;

	GadgetListBoxAddEntryImage(list, image, row, column, height, width, TRUE, color);
	GadgetListBoxSetItemData(list, (void *)(intptr_t)itemData, row, column );
	GadgetListBoxSetItemData(list, (void *)(intptr_t)extra, row - 1, column );

	/*
	** removing text, since every place that adds text has alternate displays of the same thing
	if (!text.isEmpty())
	{
		GadgetListBoxAddEntryText(list, text, GameSpyColor[GSCOLOR_DEFAULT], row+1, column, TRUE );
		GadgetListBoxSetItemData(list, (void *)itemData, row+1, column );
		rowsToSkip++;
	}
	*/

	if(++column >= GadgetListBoxGetNumColumns(list))
	{
		column = 0;
		row = row + 1 + rowsToSkip;
		rowsToSkip = max(rowsToSkip-1, 0);
	}
}

static void populateBattleHonors(const PSPlayerStats& stats, Int battleHonors, Int gamesInRow, Int lastGen, Int challenge, GameWindow *list)
{
	if( !list )
		return;

	list->winSetTooltipFunc(BattleHonorTooltip);
	GadgetListBoxReset( list );
	Int column = 0;
	Int row = 0;

	Bool isFairPlayer = FALSE;
	Int numGames = 0;
	Int numDiscons = 0;
	PerGeneralMap::const_iterator it;
	for(it = stats.games.begin(); it != stats.games.end(); ++it)
	{
		numGames += it->second;
	}
	for(it = stats.discons.begin(); it != stats.discons.end(); ++it)
	{
		numDiscons += it->second;
	}
	for(it = stats.desyncs.begin(); it != stats.desyncs.end(); ++it)
	{
		numDiscons += it->second;
	}
	if (numGames >= 10 && numDiscons * 10 < numGames)
	{
		isFairPlayer = TRUE;
	}

	ResetBattleHonorInsertion();
	GadgetListBoxAddEntryImage(list, NULL, 0, 0, 10, 10, TRUE, GameMakeColor(255,255,255,255));
	row = 1;

	InsertBattleHonor(list, TheMappedImageCollection->findImageByName("FairPlay"), isFairPlayer,
		BATTLE_HONOR_FAIR_PLAY, row, column);

	InsertBattleHonor(list, TheMappedImageCollection->findImageByName("HonorAirWing"), BitTest(battleHonors, BATTLE_HONOR_AIR_WING),
		BATTLE_HONOR_AIR_WING, row, column);
	InsertBattleHonor(list, TheMappedImageCollection->findImageByName("HonorBattleTank"), BitTest(battleHonors, BATTLE_HONOR_BATTLE_TANK),
		BATTLE_HONOR_BATTLE_TANK, row, column);
	InsertBattleHonor(list, TheMappedImageCollection->findImageByName("Apocalypse"), BitTest(battleHonors, BATTLE_HONOR_APOCALYPSE),
		BATTLE_HONOR_APOCALYPSE, row, column);

	// create a spacer for row 2 and start the images on row 3
	GadgetListBoxAddEntryImage(list, NULL, 2, 0, 10, 10, TRUE, GameMakeColor(255,255,255,255));
	row = 3;

	if (BitTest(battleHonors, BATTLE_HONOR_BLITZ5))
	{
		InsertBattleHonor(list, TheMappedImageCollection->findImageByName("HonorBlitz5"), TRUE,
			BATTLE_HONOR_BLITZ5, row, column);
	}
	else if (BitTest(battleHonors, BATTLE_HONOR_BLITZ10))
	{
		InsertBattleHonor(list, TheMappedImageCollection->findImageByName("HonorBlitz10"), TRUE,
			BATTLE_HONOR_BLITZ10, row, column);
	}
	else
	{
		InsertBattleHonor(list, TheMappedImageCollection->findImageByName("HonorBlitz10"), FALSE,
			BATTLE_HONOR_BLITZ10, row, column);
	}

	// TEST FOR STREAK HONOR
	UnicodeString uStr;
	Int streak = stats.winsInARow;
	uStr.format(u"%10d", streak);
	if (streak >= 1000)
	{
		InsertBattleHonor(list, TheMappedImageCollection->findImageByName("HonorStreak_1000"), TRUE,
			BATTLE_HONOR_STREAK_ONLINE, row, column, uStr);
	}
	else if (streak >= 500)
	{
		InsertBattleHonor(list, TheMappedImageCollection->findImageByName("HonorStreak_500"), TRUE,
			BATTLE_HONOR_STREAK_ONLINE, row, column, uStr);
	}
	else if (streak >= 100)
	{
		InsertBattleHonor(list, TheMappedImageCollection->findImageByName("HonorStreak_100"), TRUE,
			BATTLE_HONOR_STREAK_ONLINE, row, column, uStr);
	}
	else if (streak >= 25)
	{
		InsertBattleHonor(list, TheMappedImageCollection->findImageByName("HonorStreak_G"), TRUE,
			BATTLE_HONOR_STREAK_ONLINE, row, column, uStr);
	}
	else if (streak >= 10)
	{
		InsertBattleHonor(list, TheMappedImageCollection->findImageByName("HonorStreak_S"), TRUE,
			BATTLE_HONOR_STREAK_ONLINE, row, column, uStr);
	}
	else if (streak >= 3)
	{
		InsertBattleHonor(list, TheMappedImageCollection->findImageByName("HonorStreak_B"), TRUE,
			BATTLE_HONOR_STREAK_ONLINE, row, column, uStr);
	}
	else
	{
		InsertBattleHonor(list, TheMappedImageCollection->findImageByName("HonorStreak_B"), FALSE,
			BATTLE_HONOR_STREAK_ONLINE, row, column, uStr);
	}

	// TEST FOR DOMINATION HONOR
	Int totalWins = 0;
	PerGeneralMap::const_iterator pit;
	for(pit = stats.wins.begin(); pit != stats.wins.end(); ++pit)
	{
		totalWins += pit->second;
	}
	uStr.format(u"%10d", totalWins);
	if (totalWins >= 10000)
	{
		InsertBattleHonor(list, TheMappedImageCollection->findImageByName("Domination_10000"), TRUE,
			BATTLE_HONOR_DOMINATION_ONLINE, row, column, uStr, totalWins);
	}
	else if (totalWins >= 1000)
	{
		InsertBattleHonor(list, TheMappedImageCollection->findImageByName("Domination_1000"), TRUE,
			BATTLE_HONOR_DOMINATION_ONLINE, row, column, uStr, totalWins);
	}
	else if (totalWins >= 500)
	{
		InsertBattleHonor(list, TheMappedImageCollection->findImageByName("Domination_500"), TRUE,
			BATTLE_HONOR_DOMINATION_ONLINE, row, column, uStr, totalWins);
	}
	else if (totalWins >= 100)
	{
		InsertBattleHonor(list, TheMappedImageCollection->findImageByName("Domination_100"), TRUE,
			BATTLE_HONOR_DOMINATION_ONLINE, row, column, uStr, totalWins);
	}
	else
	{
		InsertBattleHonor(list, TheMappedImageCollection->findImageByName("Domination_100"), FALSE,
			BATTLE_HONOR_DOMINATION_ONLINE, row, column, uStr, totalWins);
	}

	// TEST FOR GLOBAL GENERAL HONOR
	InsertBattleHonor(list, TheMappedImageCollection->findImageByName("GlobalGen"), BitTest(battleHonors, BATTLE_HONOR_GLOBAL_GENERAL),
		BATTLE_HONOR_GLOBAL_GENERAL, row, column);

	/*
	Bool isLoyal;
	isLoyal = ThePlayerTemplateStore->getNthPlayerTemplate(lastGen)->getSide().compareNoCase( "america") == 0 && gamesInRow >= 20;
	InsertBattleHonor(list, TheMappedImageCollection->findImageByName("Loyalty_USA"), isLoyal,
		BATTLE_HONOR_LOYALTY_USA, row, column);
	isLoyal = ThePlayerTemplateStore->getNthPlayerTemplate(lastGen)->getSide().compareNoCase( "china") == 0 && gamesInRow >= 20;
	InsertBattleHonor(list, TheMappedImageCollection->findImageByName("Loyalty_China"), isLoyal,
		BATTLE_HONOR_LOYALTY_CHINA, row, column);
	isLoyal = ThePlayerTemplateStore->getNthPlayerTemplate(lastGen)->getSide().compareNoCase( "gla") == 0 && gamesInRow >= 20;
	InsertBattleHonor(list, TheMappedImageCollection->findImageByName("Loyalty_GLA"), isLoyal,
		BATTLE_HONOR_LOYALTY_GLA, row, column);
		*/


	//insertBattleHonor(list, TheMappedImageCollection->findImageByName("Endurance"), BitTest(battleHonors, BATTLE_HONOR_ENDURANCE),
		//BATTLE_HONOR_ENDURANCE, row, column);

	/*
	InsertBattleHonor(list, TheMappedImageCollection->findImageByName("Campaign_USA"), BitTest(battleHonors, BATTLE_HONOR_CAMPAIGN_USA),
		BATTLE_HONOR_CAMPAIGN_USA, row, column);
	InsertBattleHonor(list, TheMappedImageCollection->findImageByName("Campaign_China"), BitTest(battleHonors, BATTLE_HONOR_CAMPAIGN_CHINA),
		BATTLE_HONOR_CAMPAIGN_CHINA, row, column);
	InsertBattleHonor(list, TheMappedImageCollection->findImageByName("Campaign_GLA"), BitTest(battleHonors, BATTLE_HONOR_CAMPAIGN_GLA),
		BATTLE_HONOR_CAMPAIGN_GLA, row, column);
	*/

	/*
	if(BitTest(challenge, BH_CHALLENGE_MASK_7))
	{
		InsertBattleHonor(list, TheMappedImageCollection->findImageByName("HonorChallenge7"), TRUE,
			BATTLE_HONOR_CHALLENGE, row, column);
	}
	else if (BitTest(challenge, BH_CHALLENGE_MASK_6))
	{
		InsertBattleHonor(list, TheMappedImageCollection->findImageByName("HonorChallenge6"), TRUE,
			BATTLE_HONOR_CHALLENGE, row, column);
	}
	else if (BitTest(challenge, BH_CHALLENGE_MASK_5))
	{
		InsertBattleHonor(list, TheMappedImageCollection->findImageByName("HonorChallenge5"), TRUE,
			BATTLE_HONOR_CHALLENGE, row, column);
	}
	else if (BitTest(challenge, BH_CHALLENGE_MASK_4))
	{
		InsertBattleHonor(list, TheMappedImageCollection->findImageByName("HonorChallenge4"), TRUE,
			BATTLE_HONOR_CHALLENGE, row, column);
	}
	else if (BitTest(challenge, BH_CHALLENGE_MASK_3))
	{
		InsertBattleHonor(list, TheMappedImageCollection->findImageByName("HonorChallenge3"), TRUE,
			BATTLE_HONOR_CHALLENGE, row, column);
	}
	else if (BitTest(challenge, BH_CHALLENGE_MASK_2))
	{
		InsertBattleHonor(list, TheMappedImageCollection->findImageByName("HonorChallenge2"), TRUE,
			BATTLE_HONOR_CHALLENGE, row, column);
	}
	else if (BitTest(challenge, BH_CHALLENGE_MASK_1))
	{
		InsertBattleHonor(list, TheMappedImageCollection->findImageByName("HonorChallenge1"), TRUE,
			BATTLE_HONOR_CHALLENGE, row, column);
	}
	else
	{
		InsertBattleHonor(list, TheMappedImageCollection->findImageByName("HonorChallenge1"), FALSE,
			BATTLE_HONOR_CHALLENGE, row, column);
	}
	*/

	if (TheGameSpyInfo->didPlayerPreorder(stats.id))
	{
		InsertBattleHonor(list, TheMappedImageCollection->findImageByName("OfficersClub"), TRUE,
			BATTLE_HONOR_OFFICERSCLUB, row, column);
	}
}

Int GetFavoriteSide( const PSPlayerStats& stats )
{
	PerGeneralMap::const_iterator it;
	Int numGames = 0;
	Int favorite = 0;
	for(it =stats.games.begin(); it != stats.games.end(); ++it)
	{
		if(it->second >= numGames)
		{
			numGames = it->second;
			favorite = it->first;
		}
	}
	if(numGames == 0)
		return -1;
	else if( stats.gamesAsRandom >= numGames )
		return 0;

	return favorite;
}

Int CalculateRank( const PSPlayerStats& stats )
{
	if(stats.id == 0 || !TheRankPointValues)	
		return 0;
	PerGeneralMap::const_iterator it;
	Int rankPoints = 0;
	Int numGames = 0;

	for(it =stats.wins.begin(); it != stats.wins.end(); ++it)
	{
		numGames += it->second;
	}
	// Each sum is converted as Windows converts it (Platform/MsvcFloatCasts.h): a hostile stats record past
	// the int range is INT_MIN there, which the max() below turns into 0, where ARM64 saturated to INT_MAX.
	rankPoints = floatToIntAsMsvc(rankPoints + numGames * TheRankPointValues->m_winMultiplier);
	
	numGames = 0;
	for(it =stats.losses.begin(); it != stats.losses.end(); ++it)
	{
		numGames += it->second;
	}
	rankPoints = floatToIntAsMsvc(rankPoints + numGames * TheRankPointValues->m_lostMultiplier);

	numGames = 0;
	for(it =stats.duration.begin(); it != stats.duration.end(); ++it)
	{
		numGames += it->second;
	}
	rankPoints = floatToIntAsMsvc(rankPoints + (numGames / 60) * TheRankPointValues->m_hourSpentOnlineMultiplier);

	numGames = 0;
	for(it =stats.discons.begin(); it != stats.discons.end(); ++it)
	{
		numGames += it->second;
	}
	for(it =stats.desyncs.begin(); it != stats.desyncs.end(); ++it)
	{
		numGames += it->second;
	}
	rankPoints = floatToIntAsMsvc(rankPoints + numGames * TheRankPointValues->m_disconnectMultiplier);

	if(BitTest(stats.battleHonors, BATTLE_HONOR_CAMPAIGN_USA | BATTLE_HONOR_CAMPAIGN_CHINA |BATTLE_HONOR_CAMPAIGN_GLA))
	{
		rankPoints = floatToIntAsMsvc(rankPoints + 1 * TheRankPointValues->m_completedSoloCampaigns);
	}

	rankPoints = max(0, rankPoints); // clip off negative values, since discons can push us below 0.

	return rankPoints;


}

static GameWindow* findWindow(GameWindow *parent, AsciiString baseWindow, AsciiString gadgetName)
{
	AsciiString fullPath;
	fullPath.format("%s:%s", baseWindow.str(), gadgetName.str());
	// a layout without one (the hub has no statistics) is no fault: every caller checks for NULL
	return TheWindowManager->winGetWindowFromId(parent, NAMEKEY(fullPath));
}

void PopulatePlayerInfoWindows( AsciiString parentWindowName )
{
	Int lookupID = TheGameSpyInfo->getLocalProfileID();
	if(parentWindowName == "PopupPlayerInfo.wnd")
	{
		lookupID = lookAtPlayerID;
		if (lookAtPlayerID <= 0 || !parent)
			return;
		// -online keeps EA's statistics hidden: the card draws itself (layOutCard)
		if (OnlineIsEnabled())
			return;
	}

	PSPlayerStats stats = TheGameSpyPSMessageQueue->findPlayerStatsByID(lookupID);

	Bool weHaveStats = (stats.id != 0);

	// if we don't have the stats from the server, see if we have cached stats
	if( !weHaveStats && lookupID == TheGameSpyInfo->getLocalProfileID() )
	{
		stats = TheGameSpyInfo->getCachedLocalPlayerStats();

		weHaveStats = TRUE;
	}
	
	Int rankPoints = CalculateRank(stats);
	Int currentRank = rankForPoints( TheRankPointValues->m_ranks, rankPoints );

	PerGeneralMap::iterator it;
	Int numWins = 0;
	Int numLosses = 0;
	Int numDiscons = 0;
	Int numGames = 0;
	for(it =stats.wins.begin(); it != stats.wins.end(); ++it)
	{
		numWins += it->second;
	}
	for(it =stats.losses.begin(); it != stats.losses.end(); ++it)
	{
		numLosses += it->second;
	}
	for(it =stats.discons.begin(); it != stats.discons.end(); ++it)
	{
		numDiscons += it->second;
	}
	for(it =stats.desyncs.begin(); it != stats.desyncs.end(); ++it)
	{
		numDiscons += it->second;
	}

	numDiscons += GetAdditionalDisconnectsFromUserFile(lookupID);

	numGames = numWins + numLosses + numDiscons;
	
	GameWindow *win = NULL;
	UnicodeString uStr;
	win = findWindow(NULL, parentWindowName, "StaticTextPlayerStatisticsLabel");
	if(win)
	{
		AsciiString localeID = "WOL:Locale00";
		if (stats.locale >= LOC_MIN && stats.locale <= LOC_MAX)
			localeID.format("WOL:Locale%2.2d", stats.locale);
		// -online keeps no locale ("from Unknown" for everybody): the nick alone
		if (OnlineIsEnabled())
			uStr.format(u"%hs", TheGameSpyInfo->getLocalName().str());
		else
			uStr.format(TheGameText->fetch("GUI:PlayerStatistics"), lookAtPlayerName.c_str(), TheGameText->fetch(localeID).str());
		GadgetStaticTextSetText(win, uStr);
	}
	win = findWindow(NULL, parentWindowName, "StaticTextGamesPlayedValue");
	if(win)
	{
		uStr.format(u"%d", numGames);
		GadgetStaticTextSetText(win, uStr);
	}
	win = findWindow(NULL, parentWindowName, "StaticTextWinsValue");
	if(win)
	{
		uStr.format(u"%d", numWins);
		GadgetStaticTextSetText(win, uStr);
	}
	win = findWindow(NULL, parentWindowName, "StaticTextLossesValue");
	if(win)
	{
		uStr.format(u"%d", numLosses);
		GadgetStaticTextSetText(win, uStr);
	}
	win = findWindow(NULL, parentWindowName, "StaticTextDisconnectsValue");
	if(win)
	{
		uStr.format(u"%d", numDiscons);
		GadgetStaticTextSetText(win, uStr);
	}

	win = findWindow(NULL, parentWindowName, "StaticTextBestStreakValue");
	if (win)
	{
		uStr.format(u"%d", stats.maxWinsInARow);
		GadgetStaticTextSetText(win, uStr);
	}

	win = findWindow(NULL, parentWindowName, "StaticTextStreak");
	if (win)
	{
		if (stats.lossesInARow > 0)
		{
			GadgetStaticTextSetText(win, TheGameText->fetch("GUI:CurrentLossStreak"));
		}
		else
		{
			GadgetStaticTextSetText(win, TheGameText->fetch("GUI:CurrentWinStreak"));
		}
	}
	win = findWindow(NULL, parentWindowName, "StaticTextStreakValue");
	if(win)
	{
		Int streak = max(stats.lossesInARow, stats.winsInARow);
		uStr.format(u"%d", streak);
		GadgetStaticTextSetText(win, uStr);
	}

	win = findWindow(NULL, parentWindowName, "StaticTextTotalKillsValue");
	if(win)
	{
		Int numGames = 0;
		for(it =stats.unitsKilled.begin(); it != stats.unitsKilled.end(); ++it)
		{
			numGames += it->second;
		}
		uStr.format(u"%d", numGames);
		GadgetStaticTextSetText(win, uStr);
	}
	win = findWindow(NULL, parentWindowName, "StaticTextTotalDeathsValue");
	if(win)
	{
		Int numGames = 0;
		for(it =stats.unitsLost.begin(); it != stats.unitsLost.end(); ++it)
		{
			numGames += it->second;
		}
		uStr.format(u"%d", numGames);
		GadgetStaticTextSetText(win, uStr);
	}
	win = findWindow(NULL, parentWindowName, "StaticTextTotalBuiltValue");
	if(win)
	{
		Int numGames = 0;
		for(it =stats.unitsBuilt.begin(); it != stats.unitsBuilt.end(); ++it)
		{
			numGames += it->second;
		}
		uStr.format(u"%d", numGames);
		GadgetStaticTextSetText(win, uStr);
	}
	win = findWindow(NULL, parentWindowName, "StaticTextBuildingsKilledValue");
	if(win)
	{
		Int numGames = 0;
		for(it =stats.buildingsKilled.begin(); it != stats.buildingsKilled.end(); ++it)
		{
			numGames += it->second;
		}
		uStr.format(u"%d", numGames);
		GadgetStaticTextSetText(win, uStr);
	}
	win = findWindow(NULL, parentWindowName, "StaticTextBuildingsLostValue");
	if(win)
	{
		Int numGames = 0;
		for(it =stats.buildingsLost.begin(); it != stats.buildingsLost.end(); ++it)
		{
			numGames += it->second;
		}
		uStr.format(u"%d", numGames);
		GadgetStaticTextSetText(win, uStr);
	}
	win = findWindow(NULL, parentWindowName, "StaticTextBuildingsBuiltValue");
	if(win)
	{
		Int numGames = 0;
		for(it =stats.buildingsBuilt.begin(); it != stats.buildingsBuilt.end(); ++it)
		{
			numGames += it->second;
		}
		uStr.format(u"%d", numGames);
		GadgetStaticTextSetText(win, uStr);
	}

	win = findWindow(NULL, parentWindowName, "StaticTextWinPercentValue");
	if(win)
	{
		//GS  prevent divide by zero
		if( numGames > 0 )
			uStr.format(TheGameText->fetch("GUI:WinPercent"), REAL_TO_INT(numWins/(Real)numGames*100.0f));
		else
			uStr.format(TheGameText->fetch("GUI:WinPercent"), 0);
		GadgetStaticTextSetText(win, uStr);
	}

	win = findWindow(NULL, parentWindowName, "ProgressBarRank");
	if(win && TheRankPointValues)
	{
		if( currentRank == MAX_RANKS - 1)
		{
			// we've reached the max rank
			win->winHide(TRUE);
		}
		else
		{
			GadgetProgressBarSetProgress(win, 100 * INT_TO_REAL(rankPoints - TheRankPointValues->m_ranks[currentRank])/( TheRankPointValues->m_ranks[currentRank + 1] - TheRankPointValues->m_ranks[currentRank]));
		}
	}

	//calculate favorite side and rank overlay image
	UnicodeString rankStr; //, sideStr, sideRankStr;
	const PlayerTemplate* pPlayerTemplate = NULL;  //NULL == newbie
	{	//search all stats for side favorite side (highest numGames)
		Int mostGames = 0;
		Int favorite = 0;
		for(it =stats.games.begin(); it != stats.games.end(); ++it)
		{
			if(it->second >= mostGames)
			{
				mostGames = it->second;
				favorite = it->first;
			}
		}
		if( mostGames > 0 )
			pPlayerTemplate = ThePlayerTemplateStore->getNthPlayerTemplate(favorite);

		//rank (ex: Corporal)
		AsciiString rank;
		rank.format("GUI:GSRank%d", currentRank);
		rankStr = TheGameText->fetch(rank);

//		//favorite side  (ex: Toxin, Tank, Stealth, etc.)
//		AsciiString side;
//		if( mostGames > 0  &&  pPlayerTemplate != NULL )
//		{
//			if( stats.gamesAsRandom >= mostGames )
//				side = "GUI:Random";
//			else
//				side.format("SIDE:%s", pPlayerTemplate->getSide().str());
//		}
//
//		//combined text (Ex: Toxin Corporal)
//		sideStr = TheGameText->fetch(side);
//		sideRankStr.format(L"%s - %s", sideStr.str(), rankStr.str() );
	}

	//rank image;  based on rank and primary faction (USA, China, GLA)
	win = findWindow(NULL, parentWindowName, "WinRank");
	if(win && TheRankPointValues)
	{
		if (rankPoints == 0 || pPlayerTemplate == NULL)
			win->winSetEnabledImage(0, TheMappedImageCollection->findImageByName("NewPlayer"));
		else
			win->winSetEnabledImage(0, UIStyle::rankImage(pPlayerTemplate->getBaseSide(), currentRank));
//x		win->setTooltipText(rankStr);  //ex: Corporal
	}

	//sub-faction overlay icon  (ex: Tank General, Toxin General, etc.)
	win = findWindow(NULL, parentWindowName, "FactionImage");
	if(win && pPlayerTemplate && TheRankPointValues && rankPoints)
	{
		win->winSetEnabledImage(0, pPlayerTemplate->getGeneralImage());
//x		win->setTooltipText( sideStr );  //ex: Toxin General
	}

	//favorite side and rank text (Ex: Tank Corporal)
	win = findWindow(NULL, parentWindowName, "StaticTextRank");
	if(win)
	{
		GadgetStaticTextSetText(win, rankStr);  //just rank
//x		win->setTooltipText(sideRankStr);  //ex: Toxin General - Corporal
	}

	win = findWindow(NULL, parentWindowName, "StaticTextInProgress");
	if (win)
	{
		if (weHaveStats)
		{
			win->winHide(TRUE);
		}
		else
		{
			win->winHide(FALSE);
			GadgetStaticTextSetText(win, TheGameText->fetch("GUI:FetchingPlayerInfo"));
		}
	}
	
	win = findWindow(NULL, parentWindowName, "ListboxInfo");
	if(win)
	{
		populateBattleHonors(stats, stats.battleHonors,stats.gamesInRowWithLastGeneral,stats.lastGeneral,stats.challengeMedals, win);
	}

	// -online, on the hub and Quick Match: the site's numbers over EA's.  The rank line says tier and
	// rating instead of EA's rank-point title, and the record is the site's.
	if (OnlineIsEnabled())
	{
		OnlinePlayerInfo card;
		if (OnlineGetPlayerInfo(TheGameSpyInfo->getLocalName(), card))
		{
			if (card.hasStats && card.wins >= 0 && card.losses >= 0)
			{
				const Int games = card.games >= 0 ? card.games : card.wins + card.losses;
				if ((win = findWindow(NULL, parentWindowName, "StaticTextWinsValue")) != NULL)
				{
					uStr.format(u"%d", card.wins);
					GadgetStaticTextSetText(win, uStr);
				}
				if ((win = findWindow(NULL, parentWindowName, "StaticTextLossesValue")) != NULL)
				{
					uStr.format(u"%d", card.losses);
					GadgetStaticTextSetText(win, uStr);
				}
				if ((win = findWindow(NULL, parentWindowName, "StaticTextGamesPlayedValue")) != NULL)
				{
					uStr.format(u"%d", games);
					GadgetStaticTextSetText(win, uStr);
				}
				if ((win = findWindow(NULL, parentWindowName, "StaticTextWinPercentValue")) != NULL)
				{
					uStr.format(TheGameText->fetch("GUI:WinPercent"), games > 0 ? REAL_TO_INT(card.wins / (Real)games * 100.0f) : 0);
					GadgetStaticTextSetText(win, uStr);
				}
			}
			UnicodeString rating = OnlineRatingText(card);
			if (card.hasRating && card.tier.isNotEmpty())
				rating.format(u"%ls %d", OnlineTierName(card.tier).str(), card.rating);
			if (card.notOnline)
				rating = TheGameText->fetch("GUI:OnlinePlayerNotOnline");
			if (!rating.isEmpty() && (win = findWindow(NULL, parentWindowName, "StaticTextRank")) != NULL)
				GadgetStaticTextSetText(win, rating);
			if (card.hasStats && (win = findWindow(NULL, parentWindowName, "StaticTextInProgress")) != NULL)
				win->winHide(TRUE);
		}
	}
}



void HandlePersistentStorageResponses( void )
{
	if (TheGameSpyPSMessageQueue)
	{
		PSResponse resp;
		if (TheGameSpyPSMessageQueue->getResponse( resp ))
		{
			switch (resp.responseType)
			{
			case PSResponse::PSRESPONSE_COULDNOTCONNECT:
				{
					// message box & hide the window
					GSMessageBoxOk(TheGameText->fetch("GUI:Error"), TheGameText->fetch("GUI:PSCannotConnect"), NULL);
					GameSpyCloseOverlay(GSOVERLAY_PLAYERINFO);
				}
				break;
			case PSResponse::PSRESPONSE_PREORDER:
				{
					if (resp.preorder)
					{
						SetUnsignedIntInRegistry("", "Preorder", 1);
						TheGameSpyInfo->markPlayerAsPreorder( TheGameSpyInfo->getLocalProfileID() );

						// force an update of our shtuff
						PSResponse newResp;
						newResp.responseType = PSResponse::PSRESPONSE_PLAYERSTATS;
						newResp.player = TheGameSpyPSMessageQueue->findPlayerStatsByID(TheGameSpyInfo->getLocalProfileID());
						TheGameSpyPSMessageQueue->addResponse(newResp);
					}
				}
				break;
			case PSResponse::PSRESPONSE_PLAYERSTATS:
				{
					DEBUG_LOG(("LocalProfileID %d, resp.player.id %d, resp.player.locale %d\n", TheGameSpyInfo->getLocalProfileID(), resp.player.id, resp.player.locale));
					/*
					if(resp.player.id == TheGameSpyInfo->getLocalProfileID() && resp.player.locale < LOC_MIN)
					{
						if (!GameSpyIsOverlayOpen(GSOVERLAY_LOCALESELECT))
							GameSpyOpenOverlay(GSOVERLAY_LOCALESELECT);
					}
					else
					*/
					if (resp.player.id == TheGameSpyInfo->getLocalProfileID())
					{
						PeerRequest req;
						req.peerRequestType = PeerRequest::PEERREQUEST_PUSHSTATS;
						GameSpyMiscPreferences cPref;
						req.statsToPush.locale = cPref.getLocale();
						Int wins = 0, losses = 0;
						PerGeneralMap::const_iterator it;
						for (it = resp.player.wins.begin(); it != resp.player.wins.end(); ++it)
						{
							wins += it->second;
						}
						for (it = resp.player.losses.begin(); it != resp.player.losses.end(); ++it)
						{
							losses += it->second;
						}
						req.statsToPush.wins = wins;
						req.statsToPush.losses = losses;
						req.statsToPush.rankPoints = CalculateRank( resp.player );

						Int numGames = 0;
						Int favorite = 0;
						for(it =resp.player.games.begin(); it != resp.player.games.end(); ++it)
						{
							if(it->second >= numGames)
							{
								numGames = it->second;
								favorite = it->first;
							}
						}
						if(numGames == 0)
							req.statsToPush.side = 0;
						else if( resp.player.gamesAsRandom >= numGames )
							req.statsToPush.side = 0;
						else
							req.statsToPush.side = favorite;

						Bool isPreorder = TheGameSpyInfo->didPlayerPreorder( TheGameSpyInfo->getLocalProfileID() );
						req.statsToPush.preorder = isPreorder;

						DEBUG_LOG(("PEERREQUEST_PUSHSTATS: stats will be %d,%d,%d,%d,%d,%d\n",
							req.statsToPush.locale, req.statsToPush.wins, req.statsToPush.losses, req.statsToPush.rankPoints, req.statsToPush.side, req.statsToPush.preorder));
						TheGameSpyPeerMessageQueue->addRequest(req);
					}
					TheGameSpyPSMessageQueue->trackPlayerStats(resp.player);
					if (resp.player.id == TheGameSpyInfo->getLocalProfileID())
					{
						UpdateLocalPlayerStats();
					}
					DEBUG_LOG(("PopulatePlayerInfoWindows() - lookAtPlayerID is %d, got %d\n", lookAtPlayerID, resp.player.id));
					PopulatePlayerInfoWindows("PopupPlayerInfo.wnd");
					//GadgetListBoxAddEntryText(listboxInfo, UnicodeString(L"Got info!"), GameSpyColor[GSCOLOR_DEFAULT], -1);
					
					// also update info for player list in lobby
					PlayerInfoMap::iterator it = TheGameSpyInfo->getPlayerInfoMap()->begin();
					while (it != TheGameSpyInfo->getPlayerInfoMap()->end())
					{
						PlayerInfo *info = &(it->second);
						if (info && info->m_profileID == resp.player.id)
						{
							// update m_wins, m_losses, m_rankPoints
							Int wins = 0, losses = 0;
							PerGeneralMap::const_iterator it;
							for (it = resp.player.wins.begin(); it != resp.player.wins.end(); ++it)
							{
								wins += it->second;
							}
							for (it = resp.player.losses.begin(); it != resp.player.losses.end(); ++it)
							{
								losses += it->second;
							}
							info->m_wins = wins;
							info->m_losses = losses;
							info->m_rankPoints = CalculateRank( resp.player );
							Int numGames = 0;
							Int favorite = 0;
							for(it = resp.player.games.begin(); it != resp.player.games.end(); ++it)
							{
								if(it->second >= numGames)
								{
									numGames = it->second;
									favorite = it->first;
								}
							}
							if(numGames == 0)
								info->m_side = 0;
							else if( resp.player.gamesAsRandom >= numGames )
								info->m_side = 0;
							else
								info->m_side = favorite;

							PeerResponse r;
							r.peerResponseType = PeerResponse::PEERRESPONSE_PLAYERINFO;
							r.nick = info->m_name.str();
							r.player.profileID = info->m_profileID;
							r.player.flags = info->m_flags;
							r.player.wins = info->m_wins;
							r.player.losses = info->m_losses;
							r.locale = info->m_locale.str();
							r.player.rankPoints = info->m_rankPoints;
							r.player.side = info->m_side;
							r.player.preorder = info->m_preorder;
							TheGameSpyPeerMessageQueue->addResponse(r);
							break;
						}
						++it;
					}

				}
				break;
			}
		}
	}
}

//-------------------------------------------------------------------------------------------------
// -online: the player card (the wrapper's experiments/proofs/2026-11-24/screens/online-self/
// README-online-self.md, "Player card").  The master hides EA's statistics and keeps the card, Close,
// Profile and two tabs; the glass and frame A, the head, the open tab's figures, its two army boxes and
// its last five matches are drawn here over PopupParentOld, on the card's 1240x900 as online_layout.py
// authors it, scaled with the fitted window as its buttons are.  While the history waits the card
// closes under the figures, 410 high.  fitText measures with display strings of its own, so the words
// are laid out once per change into a list the draw only replays.
//-------------------------------------------------------------------------------------------------
enum CardTab { CARD_TAB_QUICK_MATCH, CARD_TAB_AI, CARD_TAB_COUNT };
static const OnlineHistoryMode CARD_HISTORY[ CARD_TAB_COUNT ] = { ONLINE_HISTORY_QM, ONLINE_HISTORY_AI };
static const char *const CARD_TAB_WINDOW[ CARD_TAB_COUNT ] = { "PopupPlayerInfo.wnd:TabQuickMatch", "PopupPlayerInfo.wnd:TabAI" };
static const Real CARD_WIDTH = 1240.0f;		///< online_layout.py CARD
static const Real CARD_HEIGHT = 900.0f;
static const Real CARD_HEIGHT_WAITING = 410.0f;
enum { CARD_ARMY_ROWS = 4, CARD_LAST_ROWS = 5 };
enum { CARD_FILL, CARD_OPEN, CARD_LIT };	///< a CardBox's kind

static GameWindow *s_cardTabs[ CARD_TAB_COUNT ];
static NameKeyType s_cardTabIDs[ CARD_TAB_COUNT ] = { NAMEKEY_INVALID, NAMEKEY_INVALID };
static Int s_cardTab = CARD_TAB_AI;
static Bool s_historyAsked[ CARD_TAB_COUNT ];
static Int s_historyState[ CARD_TAB_COUNT ];		///< OnlineLadderState as last copied, -1 before
static UnsignedInt s_historySerial[ CARD_TAB_COUNT ];	///< OnlineGetHistorySerial as last copied, 0 before
static OnlineHistory s_history[ CARD_TAB_COUNT ];
static GameWinDrawFunc s_cardWindowDraw = NULL;	///< PopupParentOld's own: GROUND under FRAME
static Bool s_cardDirty = TRUE;
static Int s_cardLaidWidth = 0;
static Real s_cardScale = 1.0f;
static Int s_cardFullTop = -1;			///< the card's top and height as the master loaded them, -1 before
static Int s_cardFullHeight = 0;
static GameWindow *s_addFriend = NULL;	///< ADD FRIEND left of Profile (package F4), made in code
static Int s_addFriendState = -1;				///< what it shows: AddFriendState, -1 before
enum AddFriendState { ADD_FRIEND_HIDDEN, ADD_FRIEND_OFFER, ADD_FRIEND_SENT };
static GameWindow *s_inviteParty = NULL;	///< INVITE TO PARTY left of ADD FRIEND (Team vs AI), made in code
static Int s_invitePartyState = -1;
enum InvitePartyState { INVITE_PARTY_HIDDEN, INVITE_PARTY_OFFER, INVITE_PARTY_FULL, INVITE_PARTY_SENT };

struct CardWords { DisplayString *words; Int x, y; Color color; Bool tracked; };
struct CardPicture { const Image *image; Int x, y, size; };
struct CardBox { Int x, y, width, height; Color color; Int kind; };
static std::vector<DisplayString *> s_cardStrings;		///< kept from one layout to the next, freed at shutdown
static std::vector<CardWords> s_cardWords;			///< relative to the card's top left, in pixels
static std::vector<CardPicture> s_cardPictures;
static std::vector<CardBox> s_cardBoxes;

/// a length on the card as authored at 1080, in the window's pixels
static Int at( Real at1080 )
{
	return REAL_TO_INT( at1080 * s_cardScale + 0.5f );
}

static void cardBox( Int x, Int y, Int width, Int height, Color color, Int kind = CARD_FILL )
{
	const CardBox box = { x, y, width, height, color, kind };
	s_cardBoxes.push_back( box );
}

static void cardPicture( const Image *image, Int x, Int y, Int size )
{
	if( image == NULL )
		return;
	const CardPicture picture = { image, x, y, size };
	s_cardPictures.push_back( picture );
}

/// words from x (ending at x when right), centred on the band from top, height tall; their width
static Int cardWords( const UnicodeString &text, GameFont *font, Int x, Int top, Int height, Color color, Bool right = FALSE )
{
	if( text.isEmpty() || font == NULL )
		return 0;
	if( s_cardWords.size() == s_cardStrings.size() )
		s_cardStrings.push_back( TheDisplayStringManager->newDisplayString() );
	DisplayString *words = s_cardStrings[ s_cardWords.size() ];
	words->setFont( font );
	words->setText( text );
	Int width, textHeight;
	words->getSize( &width, &textHeight );
	const CardWords put = { words, right ? x - width : x, top + ( height - textHeight ) / 2, color, FALSE };
	s_cardWords.push_back( put );
	return width;
}

/// tracked 14 px bold capitals (labels and heads) from x (ending at x when right), centred on the band
static Int cardCaps( const UnicodeString &text, Int x, Int top, Int height, Color color, Bool right = FALSE )
{
	GameFont *font = UIStyle::menuFont( UIStyle::MENU_TEXT_LABEL, TRUE );
	const size_t index = s_cardWords.size();
	if( cardWords( text, font, x, top, height, color ) == 0 )
		return 0;
	const Int width = UIStyle::trackedWidth( text, font, UIStyle::MENU_LABEL_TRACKING );
	s_cardWords[ index ].tracked = TRUE;
	s_cardWords[ index ].x = right ? x - width : x;
	return width;
}

/// a label key's capitals as cardCaps draws them, its _Short form when the whole is wider than room
static UnicodeString capsFit( const char *key, Int room )
{
	GameFont *font = UIStyle::menuFont( UIStyle::MENU_TEXT_LABEL, TRUE );
	const UnicodeString whole = UIStyle::capitals( TheGameText->fetch( key ) );
	if( UIStyle::trackedWidth( whole, font, UIStyle::MENU_LABEL_TRACKING ) <= room )
		return whole;
	AsciiString shortKey = key;
	shortKey.concat( "_Short" );
	return UIStyle::hasText( shortKey.str() ) ? UIStyle::capitals( TheGameText->fetch( shortKey ) ) : whole;
}

/// a track on the card: a faint white channel, its share a lit EDGE line
static void cardTrack( Int x, Int y, Int width, Int height, Real fraction )
{
	cardBox( x, y, width, height, GameMakeColor( 255, 255, 255, 24 ) );
	const Int filled = REAL_TO_INT( width * max( 0.0f, min( 1.0f, fraction ) ) + 0.5f );
	if( filled > 0 )
		cardBox( x, y, max( 2, filled ), height, UIStyle::EDGE, CARD_LIT );
}

/// a key the strings may not have yet: its words, else the English
static UnicodeString cardWord( const char *key, const WideChar *english )
{
	Bool exists = FALSE;
	const UnicodeString word = TheGameText->fetch( key, &exists );
	return exists ? word : UnicodeString( english );
}

static const PlayerTemplate *armyTemplate( const AsciiString &army )
{
	if( army.isEmpty() || ThePlayerTemplateStore == NULL )
		return NULL;
	return ThePlayerTemplateStore->findPlayerTemplate( TheNameKeyGenerator->nameToKey( army ) );
}

/// an army as the card names it ("China Tank"); EA's own name where the strings have no short one
static UnicodeString armyName( const AsciiString &army, const PlayerTemplate *found )
{
	static const struct { const char *army; const char *key; } NAMES[] =
	{
		{ "FactionAmerica", "GUI:USA" },
		{ "FactionAmericaSuperWeaponGeneral", "GUI:OnlineCardArmyUsaSuper" },
		{ "FactionAmericaLaserGeneral", "GUI:OnlineCardArmyUsaLaser" },
		{ "FactionAmericaAirForceGeneral", "GUI:OnlineCardArmyUsaAir" },
		{ "FactionChina", "GUI:China" },
		{ "FactionChinaTankGeneral", "GUI:OnlineCardArmyChinaTank" },
		{ "FactionChinaInfantryGeneral", "GUI:OnlineCardArmyChinaInf" },
		{ "FactionChinaNukeGeneral", "GUI:OnlineCardArmyChinaNuke" },
		{ "FactionGLA", "GUI:GLA" },
		{ "FactionGLAToxinGeneral", "GUI:OnlineCardArmyGlaToxin" },
		{ "FactionGLADemolitionGeneral", "GUI:OnlineCardArmyGlaDemo" },
		{ "FactionGLAStealthGeneral", "GUI:OnlineCardArmyGlaStealth" },
	};
	for( Int each = 0; each < (Int)ARRAY_SIZE( NAMES ); each++ )
	{
		if( army.compareNoCase( NAMES[ each ].army ) != 0 )
			continue;
		Bool exists = FALSE;
		const UnicodeString name = TheGameText->fetch( NAMES[ each ].key, &exists );
		if( exists )
			return name;
		break;
	}
	if( found )
		return found->getDisplayName();
	UnicodeString name;
	name.translate( army );
	return name;
}

/// a history row's map: a generated one by the shipped map it was built from, else as the row has it
static UnicodeString historyMapName( const AsciiString &map )
{
	AsciiString shipped;
	if( generatedMapTemplate( map, shipped ) && TheMapCache )
	{
		const MapMetaData *found = TheMapCache->findMap( shipped );
		if( found && !found->m_displayName.isEmpty() )
			return found->m_displayName;
	}
	// another generator version's name reads back to nothing
	if( map.startsWithNoCase( "rmg_" ) )
		return TheGameText->fetch( "GUI:Random" );
	UnicodeString name;
	name.translate( map );
	return name;
}

static Bool moreGames( const OnlineHistoryArmy &a, const OnlineHistoryArmy &b )
{
	return a.games > b.games;
}

/// the armies played most, most first, four at most
static std::vector<OnlineHistoryArmy> topArmies( std::vector<OnlineHistoryArmy> armies )
{
	std::stable_sort( armies.begin(), armies.end(), moreGames );
	if( armies.size() > CARD_ARMY_ROWS )
		armies.resize( CARD_ARMY_ROWS );
	return armies;
}

/// the side of the army he played most, Quick Match's before the AI's; empty, a random army's
/// portrait, until a history says
static AsciiString cardSide( void )
{
	for( Int tab = 0; tab < CARD_TAB_COUNT; tab++ )
	{
		const std::vector<OnlineHistoryArmy> most = topArmies( s_history[ tab ].byArmy );
		const PlayerTemplate *army = most.empty() ? NULL : armyTemplate( most[ 0 ].army );
		if( army )
			return army->getBaseSide();
	}
	return AsciiString::TheEmptyString;
}

/** Where he is: the staging room this machine sits in with him, else the chat room it shows him in,
	* with the server's name when the welcome gave one; else only that he is online.  keep is the room's
	* and the server's names, kept whole when the line is fitted. */
static UnicodeString cardWhere( const AsciiString &nick, const OnlinePlayerInfo &card, UnicodeString &keep )
{
	UnicodeString room;
	keep.clear();
	if( card.notOnline )
		return TheGameText->fetch( "GUI:OnlinePlayerNotOnline" );
	if( TheGameSpyGame && TheGameSpyGame->isInGame() )
	{
		for( Int i = 0; i < MAX_SLOTS && room.isEmpty(); ++i )
		{
			GameSpyGameSlot *slot = TheGameSpyGame->getGameSpySlot( i );
			AsciiString seated;
			if( slot && slot->isHuman() )
				seated.translate( slot->getName() );
			if( seated.isNotEmpty() && seated.compareNoCase( nick ) == 0 )
				room = TheGameSpyGame->getGameName();
		}
	}
	if( room.isEmpty() && TheGameSpyInfo && TheGameSpyInfo->getCurrentGroupRoom() != 0 )
	{
		PlayerInfoMap *players = TheGameSpyInfo->getPlayerInfoMap();
		GroupRoomMap *rooms = TheGameSpyInfo->getGroupRoomList();
		if( players && rooms && players->find( nick ) != players->end() )
		{
			GroupRoomMap::const_iterator it = rooms->find( TheGameSpyInfo->getCurrentGroupRoom() );
			if( it != rooms->end() )
			{
				room = it->second.m_translatedName;
				if( room.isEmpty() )
					room.translate( it->second.m_name );
			}
		}
	}
	if( room.isEmpty() )
		return TheGameText->fetch( "GUI:OnlineLoginOnlineNow" );
	keep = room;
	UnicodeString where;
	AsciiString serverName, city;
	Int serverPing = -1;
	if( OnlineGetServerInfo( serverName, city, serverPing ) && serverName.isNotEmpty() )
	{
		WideChar server[ 64 ];		// the site writes it in UTF-8
		WideCharFromUtf8( serverName.str(), server, ARRAY_SIZE( server ) );
		where.format( TheGameText->fetch( "GUI:OnlineCardInRoom" ), room.str(), server );
		keep.concat( u" " );
		keep.concat( server );
	}
	else
		where.format( TheGameText->fetch( "GUI:OnlineLobbyInRoom" ), room.str() );
	return where;
}

/** His page on the site: /player/<nick> beside the account page the welcome named (the register page
	* without one), under the same locale prefix.  Empty when the welcome named neither. */
static AsciiString profileUrl( const AsciiString &nick )
{
	AsciiString page = OnlineGetAccountUrl();
	if( page.isEmpty() )
		page = OnlineGetRegisterUrl();
	const char *scheme = strstr( page.str(), "://" );
	if( nick.isEmpty() || scheme == NULL )
		return AsciiString::TheEmptyString;
	const size_t hostStart = ( scheme - page.str() ) + 3;
	std::string url( page.str(), strcspn( page.str(), "?#" ) );
	while( url.size() > hostStart && url[ url.size() - 1 ] == '/' )
		url.erase( url.size() - 1 );
	if( url.find( '/', hostStart ) == std::string::npos )
		url += '/';
	else
		url.erase( url.rfind( '/' ) + 1 );
	url += "player/";
	static const char HEX[] = "0123456789ABCDEF";
	for( const char *c = nick.str(); *c; ++c )
	{
		const UnsignedByte byte = (UnsignedByte)*c;
		if( isalnum( byte ) || byte == '-' || byte == '.' || byte == '_' || byte == '~' )
			url += (char)byte;
		else
		{
			url += '%';
			url += HEX[ byte >> 4 ];
			url += HEX[ byte & 15 ];
		}
	}
	return AsciiString( url.c_str() );
}

/// a rating at the head's right: its label in capitals over the figure at 40 and the tier MUTED, their
/// left edge shared and the widest ending at right, the tier's portrait 60 square 16 before them; returns
/// the portrait's left edge
static Int ratingSummary( UIStyle::RatingTier tier, const AsciiString &side, const char *label, const UnicodeString &value, Int right )
{
	GameFont *body = UIStyle::menuFont( UIStyle::MENU_TEXT_BODY );
	GameFont *figure = UIStyle::menuFont( UIStyle::MENU_TEXT_TITLE );
	const size_t first = s_cardWords.size();
	const Int labelWidth = cardCaps( UIStyle::capitals( TheGameText->fetch( label ) ), 0, at( 26 ), at( 18 ), UIStyle::MUTED );
	const Int valueWidth = cardWords( value, figure, 0, at( 46 ), at( 48 ), UIStyle::INK );
	const Int tierWidth = cardWords( OnlineTierName( AsciiString( UIStyle::tierKey( tier ) ) ), body, 0, at( 94 ), at( 26 ), UIStyle::MUTED );
	const Int left = right - max( max( labelWidth, valueWidth ), tierWidth ) - at( 2 );
	for( size_t each = first; each < s_cardWords.size(); each++ )
		s_cardWords[ each ].x += left;
	const Int portrait = left - at( 76 );
	cardPicture( UIStyle::tierImage( tier, side ), portrait, at( 40 ), at( 60 ) );
	return portrait;
}

/** The card's height: the master's, or 410 of its 900 while the history waits, centred where the master
	* stands it; the keys keep their place over its foot. */
static void fitCard( Bool compact )
{
	GameWindow *card = TheWindowManager->winGetWindowFromId( parent, TheNameKeyGenerator->nameToKey( "PopupPlayerInfo.wnd:PopupParentOld" ) );
	if( parent == NULL || card == NULL )
		return;
	Int x, y, width, height;
	parent->winGetPosition( &x, &y );
	parent->winGetSize( &width, &height );
	if( s_cardFullTop < 0 )
	{
		s_cardFullTop = y;
		s_cardFullHeight = height;
	}
	const Int wanted = compact ? REAL_TO_INT( s_cardFullHeight * CARD_HEIGHT_WAITING / CARD_HEIGHT + 0.5f ) : s_cardFullHeight;
	if( wanted == height )
		return;
	parent->winSetPosition( x, s_cardFullTop + ( s_cardFullHeight - wanted ) / 2 );
	parent->winSetSize( width, wanted );
	card->winSetSize( width, wanted );
	GameWindow *keys[] = { buttonClose, buttonBuddies, s_addFriend, s_inviteParty };
	for( Int each = 0; each < (Int)ARRAY_SIZE( keys ); each++ )
	{
		if( keys[ each ] == NULL )
			continue;
		Int keyX, keyY;
		keys[ each ]->winGetPosition( &keyX, &keyY );
		keys[ each ]->winSetPosition( keyX, keyY + wanted - height );
	}
}

static UnicodeString winsAndLosses( Int wins, Int games )
{
	UnicodeString text;
	if( wins >= 0 && games >= 0 )
		text.format( u"%d\x2013%d", wins, max( 0, games - wins ) );
	return text;
}

static void layOutCard( Int width )
{
	s_cardDirty = FALSE;
	s_cardLaidWidth = width;
	s_cardScale = width / CARD_WIDTH;
	s_cardWords.clear();
	s_cardPictures.clear();
	s_cardBoxes.clear();

	const AsciiString nick( lookAtPlayerName.c_str() );
	OnlinePlayerInfo card;
	OnlineGetPlayerInfo( nick, card );
	const Bool ai = s_cardTab == CARD_TAB_AI;
	const OnlineHistory &history = s_history[ s_cardTab ];
	const AsciiString side = cardSide();
	const UnicodeString none( u"\x2013" );		// an en dash for a number nobody sent
	GameFont *body = UIStyle::menuFont( UIStyle::MENU_TEXT_BODY );
	GameFont *strong = UIStyle::menuFont( UIStyle::MENU_TEXT_TITLE, TRUE );
	GameFont *figure = UIStyle::menuFont( UIStyle::MENU_TEXT_TITLE );
	GameFont *rowFigure = UIStyle::menuFont( UIStyle::MENU_TEXT_ROW_FIGURE );
	const UIStyle::RatingTier ladderTier = UIStyle::playerTier( card );
	const UIStyle::RatingTier aiTier = UIStyle::aiRatingTier( card.aiRating, card.hasAIRating ? card.aiGames : -1 );
	const Int x0 = at( 28 ), x1 = at( CARD_WIDTH - 28.0f );

	// while the history waits the card closes under the figures; its foot, the faint rule and the hint
	fitCard( !history.arrived );
	Int cardWidth = width;
	Int height = REAL_TO_INT( ( history.arrived ? CARD_HEIGHT : CARD_HEIGHT_WAITING ) * s_cardScale + 0.5f );
	if( parent )
		parent->winGetSize( &cardWidth, &height );
	cardBox( x0, height - at( 84 ), x1 - x0, 1, UIStyle::withAlpha( UIStyle::FRAME, 110 ) );
	cardWords( TheGameText->fetch( "GUI:OnlineCardEsc" ), UIStyle::menuFont( UIStyle::MENU_TEXT_HINT ), x0, height - at( 68 ), at( 48 ),
		UIStyle::MUTED );

	// the head: the portrait of his ladder tier, the nick at 40, where he is, both ratings at the right
	// and the lit rule closing it.  An unranked player has EA's new player's portrait here.
	const Image *portrait = UIStyle::tierImage( ladderTier, side );
	if( portrait == NULL )
		portrait = TheMappedImageCollection->findImageByName( "NewPlayer" );
	cardBox( x0 - 1, at( 24 ) - 1, at( 96 ) + 2, at( 96 ) + 2, UIStyle::withAlpha( UIStyle::FRAME, 220 ), CARD_OPEN );
	cardPicture( portrait, x0, at( 24 ), at( 96 ) );
	const Int aiLeft = ratingSummary( aiTier, side, "GUI:OnlineAIRatingLabel", card.hasAIRating ? UIStyle::eloText( card.aiRating ) : none, x1 );
	const Int ratingsLeft = ratingSummary( ladderTier, side, "GUI:OnlineLadderRatingLabel",
		card.hasRating ? UIStyle::ladderText( card.shown(), card.hasElo ) : none, aiLeft - at( 40 ) );
	UnicodeString shown;
	shown.translate( nick );
	const Int nickLeft = x0 + at( 120 );
	cardWords( UIStyle::fitName( shown, strong, ratingsLeft - at( 24 ) - nickLeft ), strong, nickLeft, at( 26 ), at( 50 ), UIStyle::INK );
	UnicodeString room;
	const UnicodeString where = cardWhere( nick, card, room );
	Int whereLeft = nickLeft;
	const Image *light = card.notOnline ? NULL : UIStyle::pingImage( card.ping );
	if( light )
	{
		cardPicture( light, whereLeft, at( 89 ), at( 16 ) );
		whereLeft += at( 26 );
	}
	cardWords( UIStyle::fitText( where, body, ratingsLeft - at( 24 ) - whereLeft, UnicodeString::TheEmptyString, room ), body,
		whereLeft, at( 82 ), at( 30 ), UIStyle::MUTED );
	cardBox( at( 2 ), at( 144 ), width - at( 4 ), max( 1, at( 2 ) ), UIStyle::FRAME, CARD_LIT );

	// the tab's figures: the rating with its tier, then games with the record and the win rate, with the
	// ladder place before them on Quick Match (vs AI has three: each match plays the speed its player picked)
	Int wins = -1;
	Int games = -1;
	if( ai && card.hasAIRating )
	{
		wins = card.aiWins;
		games = card.aiGames;
	}
	else if( !ai && card.hasStats && card.wins >= 0 && card.losses >= 0 )
	{
		wins = card.wins;
		games = card.games >= 0 ? card.games : card.wins + card.losses;
	}
	else if( history.arrived )
	{
		wins = history.wins;
		games = history.games;
	}
	UnicodeString label[ 4 ], value[ 4 ], under[ 4 ];
	const Int gamesAt = ai ? 1 : 2;
	const Int figures = ai ? 3 : 4;
	label[ 0 ] = TheGameText->fetch( ai ? "GUI:OnlineAIRatingLabel" : "GUI:OnlineLadderRatingLabel" );
	if( ai ? card.hasAIRating : card.hasRating )
		value[ 0 ] = ai ? UIStyle::eloText( card.aiRating ) : UIStyle::ladderText( card.shown(), card.hasElo );
	else
		value[ 0 ] = none;
	under[ 0 ] = OnlineTierName( AsciiString( UIStyle::tierKey( ai ? aiTier : ladderTier ) ) );
	label[ gamesAt ] = TheGameText->fetch( "GUI:OnlineGamesLabel" );
	if( games >= 0 )
		value[ gamesAt ].format( u"%d", games );
	else
		value[ gamesAt ] = none;
	under[ gamesAt ] = winsAndLosses( wins, games );
	label[ gamesAt + 1 ] = TheGameText->fetch( "GUI:OnlineWinRateLabel" );
	value[ gamesAt + 1 ] = games > 0 ? UIStyle::percentText( UIStyle::percent( wins, games ) ) : none;
	if( !ai )
	{
		label[ 1 ] = TheGameText->fetch( "GUI:OnlinePlaceLabel" );
		if( card.rank > 0 )
			value[ 1 ].format( TheGameText->fetch( "GUI:OnlinePlaceValue" ), card.rank );
		else
			value[ 1 ] = none;
	}
	// across the card, each its label, its figure at 40 and the tier or record after it; the win rate
	// a track after its figure
	const Int column = ( x1 - x0 ) / figures;
	for( Int each = 0; each < figures; each++ )
	{
		const Int left = x0 + each * column;
		const Int room = column - at( 24 );
		cardCaps( UIStyle::fitText( UIStyle::capitals( label[ each ] ), UIStyle::menuFont( UIStyle::MENU_TEXT_LABEL, TRUE ), room ), left,
			at( 218 ), at( 18 ), UIStyle::MUTED );
		const Int valueWidth = cardWords( UIStyle::fitText( value[ each ], figure, room ), figure, left, at( 238 ), at( 48 ), UIStyle::INK );
		if( each == gamesAt + 1 )
		{
			const Int trackLeft = left + valueWidth + at( 18 );
			if( left + room > trackLeft )
				cardTrack( trackLeft, at( 266 ), left + room - trackLeft, at( 5 ), games > 0 ? (Real)wins / games : 0.0f );
		}
		else if( !under[ each ].isEmpty() )
		{
			const Int underLeft = left + valueWidth + at( 14 );
			cardWords( UIStyle::fitText( under[ each ], body, left + room - underLeft ), body, underLeft, at( 254 ), at( 28 ), UIStyle::MUTED );
		}
	}

	// the history: a line while it is on its way or when the server has none to give
	if( !history.arrived )
	{
		const UnicodeString line = s_historyState[ s_cardTab ] == ONLINE_LADDER_WAITING
			? TheGameText->fetch( "GUI:FetchingPlayerInfo" )
			: cardWord( "GUI:OnlineCardUnavailable", u"Match history is not available right now." );
		cardWords( UIStyle::fitText( line, body, x1 - x0 ), body, x0, at( 300 ), at( 30 ), UIStyle::MUTED );
		return;
	}

	// two boxes: the win rate by his own army and by the computer's or the opponent's, four rows each
	const char *const boxHeading[ 2 ] = { "GUI:OnlineCardByOwn", ai ? "GUI:OnlineCardByAI" : "GUI:OnlineCardByOpp" };
	const std::vector<OnlineHistoryArmy> boxArmies[ 2 ] = { topArmies( history.byArmy ), topArmies( history.byOpponentArmy ) };
	const Int boxWidth = ( x1 - x0 - at( 24 ) ) / 2;
	for( Int box = 0; box < 2; box++ )
	{
		const Int left = box == 0 ? x0 : x1 - boxWidth;
		const Int right = left + boxWidth;
		cardBox( left, at( 304 ), boxWidth, at( 512 ) - at( 304 ), UIStyle::withAlpha( UIStyle::FRAME, 200 ), CARD_OPEN );
		cardCaps( capsFit( boxHeading[ box ], boxWidth - at( 328 ) ), left + at( 18 ), at( 318 ), at( 18 ), UIStyle::ACTIVE );
		cardCaps( capsFit( "GUI:OnlineGamesLabel", at( 68 ) ), right - at( 232 ), at( 318 ), at( 18 ), UIStyle::MUTED, TRUE );
		cardCaps( capsFit( "GUI:OnlineWinRateLabel", at( 182 ) ), right - at( 18 ), at( 318 ), at( 18 ), UIStyle::MUTED, TRUE );
		for( size_t row = 0; row < boxArmies[ box ].size(); row++ )
		{
			const OnlineHistoryArmy &army = boxArmies[ box ][ row ];
			const PlayerTemplate *found = armyTemplate( army.army );
			const Int top = at( 348.0f + 38.0f * row );
			const Int height = at( 348.0f + 38.0f * ( row + 1 ) ) - top;
			if( found )
				cardPicture( found->getGeneralImage(), left + at( 18 ), top + ( height - at( 28 ) ) / 2, at( 28 ) );
			cardWords( UIStyle::fitText( armyName( army.army, found ), body, right - at( 300 ) - left - at( 58 ) ), body, left + at( 58 ), top,
				height, UIStyle::INK );
			UnicodeString count;
			count.format( u"%d", army.games );
			cardWords( count, body, right - at( 232 ), top, height, UIStyle::MUTED, TRUE );
			cardTrack( right - at( 200 ), top + ( height - at( 5 ) ) / 2, at( 110 ), at( 5 ), army.games > 0 ? (Real)army.wins / army.games : 0.0f );
			cardWords( UIStyle::percentText( UIStyle::percent( army.wins, army.games ) ), body, right - at( 18 ), top, height, UIStyle::INK, TRUE );
		}
	}

	// the last five: result, map, both armies' logos, the computer's rating or the opponent with his,
	// length and change
	cardCaps( capsFit( ai ? "GUI:OnlineCardLastAI" : "GUI:OnlineCardLastQM", x1 - x0 ), x0, at( 532 ), at( 18 ), UIStyle::ACTIVE );
	const Int headTop = at( 560 );
	const Int headHeight = at( 18 );
	cardCaps( capsFit( "GUI:OnlineMapLabel", at( 250 ) ), x0 + at( 130 ), headTop, headHeight, UIStyle::MUTED );
	cardCaps( capsFit( "GUI:HudArmies", at( 160 ) ), x0 + at( 400 ), headTop, headHeight, UIStyle::MUTED );
	if( ai )
		cardCaps( capsFit( "GUI:OnlineCardColAI", at( 270 ) ), x0 + at( 860 ), headTop, headHeight, UIStyle::MUTED, TRUE );
	else
		cardCaps( capsFit( "GUI:OnlineOpponentLabel", at( 270 ) ), x0 + at( 590 ), headTop, headHeight, UIStyle::MUTED );
	cardCaps( capsFit( "GUI:ScoreLength", at( 120 ) ), x0 + at( 1000 ), headTop, headHeight, UIStyle::MUTED, TRUE );
	cardCaps( capsFit( "GUI:OnlineLadderShort", at( 160 ) ), x1, headTop, headHeight, UIStyle::MUTED, TRUE );
	cardBox( x0, at( 584 ), x1 - x0, 1, UIStyle::FRAME, CARD_LIT );
	const UnicodeString versus = TheGameText->fetch( "GUI:QMVs" );
	for( size_t row = 0; row < history.rows.size() && row < CARD_LAST_ROWS; row++ )
	{
		const OnlineHistoryRow &match = history.rows[ row ];
		const Real y = 590.0f + 38.0f * row;
		const Int top = at( y );
		const Int height = at( y + 38.0f ) - top;
		if( row % 2 == 1 )
			cardBox( x0, top, x1 - x0, height, GameMakeColor( 255, 255, 255, 7 ) );
		cardCaps( UIStyle::capitals( TheGameText->fetch( match.won ? "GUI:OnlineWinLabel" : "GUI:OnlineLossLabel" ) ), x0 + at( 10 ), top, height,
			match.won ? UIStyle::ACTIVE : UIStyle::MUTED );
		cardWords( match.map.isEmpty() ? none : UIStyle::fitText( historyMapName( match.map ), body, at( 250 ) ), body, x0 + at( 130 ), top,
			height, UIStyle::INK );

		// old Quick Match rows may carry no armies
		const PlayerTemplate *mine = armyTemplate( match.playerArmy );
		const PlayerTemplate *theirs = armyTemplate( match.opponentArmy );
		if( mine || theirs )
		{
			const Int logo = at( 26 );
			const Int logoTop = top + ( height - logo ) / 2;
			Int x = x0 + at( 400 );
			if( mine )
				cardPicture( mine->getGeneralImage(), x, logoTop, logo );
			x += logo + at( 8 );
			x += cardWords( versus, body, x, top, height, UIStyle::MUTED ) + at( 8 );
			if( theirs )
				cardPicture( theirs->getGeneralImage(), x, logoTop, logo );
		}
		else
			cardWords( none, body, x0 + at( 400 ), top, height, UIStyle::MUTED );

		if( ai )
			cardWords( UIStyle::eloText( match.opponentRating ), body, x0 + at( 860 ), top, height, UIStyle::INK, TRUE );
		else
		{
			// the opponent's nick, then his rating MUTED after it
			const size_t ratingIndex = s_cardWords.size();
			const Int ratingWidth = cardWords( UIStyle::ladderText( match.opponentRating, match.hasElo ), body, 0, top, height, UIStyle::MUTED );
			UnicodeString opponent;
			opponent.translate( match.opponent );
			const Int nickWidth = cardWords( UIStyle::fitName( opponent, body, at( 270 ) - ratingWidth - at( 10 ) ), body, x0 + at( 590 ), top,
				height, UIStyle::INK );
			s_cardWords[ ratingIndex ].x = x0 + at( 590 ) + ( nickWidth > 0 ? nickWidth + at( 10 ) : 0 );
		}

		UnicodeString length;
		if( match.seconds > 0 )
			length.format( u"%d:%02d", match.seconds / 60, match.seconds % 60 );
		else
			length = none;
		cardWords( length, body, x0 + at( 1000 ), top, height, match.seconds > 0 ? UIStyle::INK : UIStyle::MUTED, TRUE );
		cardWords( UIStyle::ladderText( match.change, match.hasElo, TRUE ), rowFigure, x1 - at( 6 ), top, height,
			match.change > 0.0f ? UIStyle::ACTIVE : UIStyle::MUTED, TRUE );
	}
}

/// PopupParentOld: the screen stepped back, the card's glass and frame A, then the card
static void drawCardWindow( GameWindow *window, WinInstanceData *instData )
{
	if( !OnlineIsEnabled() )
	{
		if( s_cardWindowDraw )
			s_cardWindowDraw( window, instData );
		return;
	}
	Int x, y, width, height;
	window->winGetSize( &width, &height );
	if( s_cardDirty || width != s_cardLaidWidth )
		layOutCard( width );		// may close the card while the history waits
	window->winGetScreenPosition( &x, &y );
	window->winGetSize( &width, &height );
	IRegion2D card;
	card.lo.x = x;
	card.lo.y = y;
	card.hi.x = x + width;
	card.hi.y = y + height;
	UIStyle::veil( 150 );
	UIStyle::glass( card, 200 );
	UIStyle::frameA( card );
	for( size_t each = 0; each < s_cardBoxes.size(); each++ )
	{
		const CardBox &box = s_cardBoxes[ each ];
		if( box.kind == CARD_OPEN )
			TheDisplay->drawOpenRect( x + box.x, y + box.y, box.width, box.height, 1.0f, box.color );
		else if( box.kind == CARD_LIT )
			UIStyle::litLine( x + box.x, y + box.y, box.width, box.height, box.color );
		else
			TheDisplay->drawFillRect( x + box.x, y + box.y, box.width, box.height, box.color );
	}
	for( size_t each = 0; each < s_cardPictures.size(); each++ )
	{
		const CardPicture &picture = s_cardPictures[ each ];
		TheDisplay->drawImage( picture.image, x + picture.x, y + picture.y, x + picture.x + picture.size, y + picture.y + picture.size );
	}
	for( size_t each = 0; each < s_cardWords.size(); each++ )
	{
		const CardWords &put = s_cardWords[ each ];
		if( put.tracked )
			UIStyle::drawTracked( put.words->getText(), put.words->getFont(), x + put.x, y + put.y, put.color, UIStyle::MENU_LABEL_TRACKING );
		else
			put.words->draw( x + put.x, y + put.y, put.color, UIStyle::SHADOW );
	}
}

/// a card tab: a menu tab, the open one green and never lit by the mouse
static void drawCardTab( GameWindow *window, WinInstanceData *instData )
{
	Int x, y, width, height;
	window->winGetScreenPosition( &x, &y );
	window->winGetSize( &width, &height );
	IRegion2D box;
	box.lo.x = x;
	box.lo.y = y;
	box.hi.x = x + width;
	box.hi.y = y + height;
	const Bool open = window == s_cardTabs[ s_cardTab ];
	UIStyle::menuTab( box, instData->getTextDisplayString(), open, !open && BitTest( instData->getState(), WIN_STATE_HILITED ) );
}

/// a history whose serial moved (a new state or a new answer) is copied and lays the card out again
static void pollCardHistory( void )
{
	for( Int tab = 0; tab < CARD_TAB_COUNT; tab++ )
	{
		if( !s_historyAsked[ tab ] )
			continue;
		const UnsignedInt serial = OnlineGetHistorySerial( lookAtPlayerID, CARD_HISTORY[ tab ] );
		if( serial == s_historySerial[ tab ] )
			continue;
		s_historySerial[ tab ] = serial;
		s_historyState[ tab ] = OnlineGetHistory( lookAtPlayerID, CARD_HISTORY[ tab ], s_history[ tab ] );
		s_cardDirty = TRUE;
	}
}

/// a tab opened: its history is asked for the first time it opens while the card is up
static void openCardTab( Int tab )
{
	s_cardTab = tab;
	s_cardDirty = TRUE;
	// the open tab is the disabled one, as on the online pages: a second click on it does nothing, and the
	// switch shows in the window tree (a script sees which page is open), not only in the drawing
	for( Int each = 0; each < CARD_TAB_COUNT; each++ )
		if( s_cardTabs[ each ] )
			s_cardTabs[ each ]->winEnable( each != tab );
	if( !s_historyAsked[ tab ] && lookAtPlayerID > 0 )
	{
		s_historyAsked[ tab ] = TRUE;
		OnlineRequestHistory( lookAtPlayerID, CARD_HISTORY[ tab ] );
	}
	pollCardHistory();
}

//-------------------------------------------------------------------------------------------------
/** Initialize the Overlay */
//-------------------------------------------------------------------------------------------------
/// whether the card is the local player's own
static Bool cardIsLocal( void )
{
	if( TheGameSpyInfo == NULL )
		return FALSE;
	if( lookAtPlayerID > 0 && lookAtPlayerID == TheGameSpyInfo->getLocalProfileID() )
		return TRUE;
	return TheGameSpyInfo->getLocalBaseName().compareNoCase( lookAtPlayerName.c_str() ) == 0;
}

/** ADD FRIEND on another player's card while the service has friends (package F4): Close's look, its
	* width, BOX_KEY_GAP (16) left of Profile (of Close when Profile is hidden), "PopupPlayerInfo.wnd:ButtonAddFriend". */
/// a key of the card's foot made in code with Close's size, placed by placeCardKeys
static GameWindow *makeCardKey( const char *name, const char *label )
{
	GameWindow *holder = buttonClose->winGetParent();
	WinInstanceData inst;
	inst.init();
	BitSet( inst.m_style, GWS_PUSH_BUTTON | GWS_MOUSE_TRACK );
	inst.m_decoratedNameString = name;
	inst.m_id = (Int)TheNameKeyGenerator->nameToKey( inst.m_decoratedNameString );
	Int closeWidth, closeHeight;
	buttonClose->winGetSize( &closeWidth, &closeHeight );
	GameWindow *key = TheWindowManager->gogoGadgetPushButton( holder, WIN_STATUS_ENABLED, 0, 0,
		closeWidth, closeHeight, &inst, buttonClose->winGetFont(), FALSE );
	if( key == NULL )
		return NULL;
	GadgetButtonSetText( key, UIStyle::capitals( TheGameText->fetch( label ) ) );
	UIStyle::setMenuKey( key, FALSE );
	return key;
}

/// the foot's keys right to left: Close, Profile where the master stands it, then ADD FRIEND and INVITE TO
/// PARTY while they show, BOX_KEY_GAP (16) apart, so a hidden one leaves no hole
static void placeCardKeys( void )
{
	if( buttonClose == NULL )
		return;
	Int closeX, closeY, closeWidth, closeHeight;
	buttonClose->winGetPosition( &closeX, &closeY );
	buttonClose->winGetSize( &closeWidth, &closeHeight );
	Int right = closeX;
	if( buttonBuddies && !buttonBuddies->winIsHidden() )
	{
		Int profileX, profileY;
		buttonBuddies->winGetPosition( &profileX, &profileY );
		right = profileX;
	}
	GameWindow *keys[] = { s_addFriend, s_inviteParty };
	for( Int each = 0; each < (Int)ARRAY_SIZE( keys ); each++ )
	{
		if( keys[ each ] == NULL || keys[ each ]->winIsHidden() )
			continue;
		right -= at( (Real)UIStyle::BOX_KEY_GAP ) + closeWidth;
		keys[ each ]->winSetPosition( right, closeY );
	}
}

static void makeAddFriendKey( void )
{
	s_addFriend = NULL;
	s_addFriendState = -1;
	s_inviteParty = NULL;
	s_invitePartyState = -1;
	if( buttonClose == NULL || buttonClose->winGetParent() == NULL || cardIsLocal() )
		return;
	if( OnlineFriendsAvailable() )
		s_addFriend = makeCardKey( "PopupPlayerInfo.wnd:ButtonAddFriend", "GUI:AddFriend" );
	// Team vs AI: anyone signed in may be asked into the party, friend or not (the server takes a nick)
	if( OnlineTeamAIAvailable() )
		s_inviteParty = makeCardKey( "PopupPlayerInfo.wnd:ButtonInviteParty", "GUI:OnlineInviteToParty" );
	placeCardKeys();
}

/** INVITE TO PARTY as the party and the screen allow: hidden in a room (an invite from there is the room's)
	* and when this player is in our party already; disabled while the party is full; SENT once asked from
	* here.  Then the keys stand again, so a key that hid leaves no gap. */
static void updateInvitePartyKey( void )
{
	if( s_inviteParty == NULL )
		return;
	Int state = INVITE_PARTY_OFFER;
	std::vector<OnlinePartyMember> party;
	OnlineGetParty( party );
	if( !OnlineTeamAIAvailable() || ( TheGameSpyInfo && TheGameSpyInfo->getCurrentStagingRoomID() != 0 ) )
		state = INVITE_PARTY_HIDDEN;
	for( size_t each = 0; each < party.size() && state != INVITE_PARTY_HIDDEN; ++each )
	{
		if( ( lookAtPlayerID > 0 && party[ each ].id == lookAtPlayerID ) || party[ each ].nick.compareNoCase( lookAtPlayerName.c_str() ) == 0 )
			state = INVITE_PARTY_HIDDEN;
	}
	if( state == INVITE_PARTY_OFFER && (Int)party.size() >= ONLINE_TEAM_SEATS )
		state = INVITE_PARTY_FULL;
	if( state == INVITE_PARTY_OFFER && s_invitePartyState == INVITE_PARTY_SENT )
		state = INVITE_PARTY_SENT;
	if( state != s_invitePartyState )
	{
		s_invitePartyState = state;
		s_inviteParty->winHide( state == INVITE_PARTY_HIDDEN );
		s_inviteParty->winEnable( state == INVITE_PARTY_OFFER );
		GadgetButtonSetText( s_inviteParty, UIStyle::capitals( TheGameText->fetch( state == INVITE_PARTY_SENT ? "GUI:FriendRequestSent" : "GUI:OnlineInviteToParty" ) ) );
	}
	placeCardKeys();
}

/// ADD FRIEND as the service's list has this player: hidden for a friend or a block, SENT while ours waits
static void updateAddFriendKey( void )
{
	if( s_addFriend == NULL )
		return;
	Int state = ADD_FRIEND_OFFER;
	if( !OnlineFriendsAvailable() )
		state = ADD_FRIEND_HIDDEN;
	else
	{
		std::vector<OnlineFriend> rows;
		OnlineGetFriends( rows );
		for( size_t each = 0; each < rows.size(); ++each )
		{
			const OnlineFriend &row = rows[ each ];
			if( ( lookAtPlayerID > 0 && row.id == lookAtPlayerID ) || row.nick.compareNoCase( lookAtPlayerName.c_str() ) == 0 )
			{
				if( row.status == ONLINE_FRIEND_PENDING_OUT )
					state = ADD_FRIEND_SENT;
				else if( row.status != ONLINE_FRIEND_PENDING_IN )
					state = ADD_FRIEND_HIDDEN;		// a friend already, or blocked
				break;
			}
		}
	}
	if( state == ADD_FRIEND_OFFER && s_addFriendState == ADD_FRIEND_SENT )
		state = ADD_FRIEND_SENT;		// asked from here; the server's row has not come back yet
	if( state == s_addFriendState )
		return;
	s_addFriendState = state;
	s_addFriend->winHide( state == ADD_FRIEND_HIDDEN );
	s_addFriend->winEnable( state == ADD_FRIEND_OFFER );
	GadgetButtonSetText( s_addFriend, UIStyle::capitals( TheGameText->fetch( state == ADD_FRIEND_SENT ? "GUI:FriendRequestSent" : "GUI:AddFriend" ) ) );
}

void GameSpyPlayerInfoOverlayInit( WindowLayout *layout, void *userData )
{
	parentID = TheNameKeyGenerator->nameToKey( "PopupPlayerInfo.wnd:PopupParent" );
	buttonCloseID = TheNameKeyGenerator->nameToKey( "PopupPlayerInfo.wnd:ButtonClose" );
	buttonBuddiesID = TheNameKeyGenerator->nameToKey( "PopupPlayerInfo.wnd:ButtonCommunicator" );
	listboxInfoID = TheNameKeyGenerator->nameToKey( "PopupPlayerInfo.wnd:ListboxInfo" );
	//buttonOptionsID = TheNameKeyGenerator->nameToKey( "PopupPlayerInfo.wnd:ButtonOptions" );
	buttonSetLocaleID = TheNameKeyGenerator->nameToKey( "PopupPlayerInfo.wnd:ButtonSetLocale" );
	buttonDeleteAccountID = TheNameKeyGenerator->nameToKey( "PopupPlayerInfo.wnd:ButtonDeleteAccount" );
	checkBoxAsianFontID = TheNameKeyGenerator->nameToKey( "PopupPlayerInfo.wnd:CheckBoxAsianText" );
	checkBoxNonAsianFontID = TheNameKeyGenerator->nameToKey( "PopupPlayerInfo.wnd:CheckBoxNonAsianText" );

	parent = TheWindowManager->winGetWindowFromId( NULL, parentID );
	buttonClose = TheWindowManager->winGetWindowFromId( parent,  buttonCloseID);
	buttonBuddies = TheWindowManager->winGetWindowFromId( parent,  buttonBuddiesID);
	listboxInfo = TheWindowManager->winGetWindowFromId( parent,  listboxInfoID);
	//buttonbuttonOptions = TheWindowManager->winGetWindowFromId( parent,  buttonOptionsID);
	buttonSetLocale = TheWindowManager->winGetWindowFromId( parent,  buttonSetLocaleID);
	buttonDeleteAccount = TheWindowManager->winGetWindowFromId( parent,  buttonDeleteAccountID);
	checkBoxAsianFont = TheWindowManager->winGetWindowFromId( parent,  checkBoxAsianFontID);
	checkBoxNonAsianFont = TheWindowManager->winGetWindowFromId( parent,  checkBoxNonAsianFontID);

	// Show Menu
	layout->hide( FALSE );

	// Set Keyboard to Main Parent
	TheWindowManager->winSetFocus( parent );

	isOverlayActive = true;

	//GadgetListBoxAddEntryText(listboxInfo, UnicodeString(L"Working"), GameSpyColor[GSCOLOR_DEFAULT], -1);

	GameSpyCloseOverlay(GSOVERLAY_BUDDY);
	raiseMessageBox = true;
	// the layout's GUI:Hide reads BACK; the card closes, as the record popup's CLOSE says
	if (buttonClose)
		GadgetButtonSetText( buttonClose, TheGameText->fetch( "GUI:LobbyRecordClose" ) );

	// -online's card: the tabs as menu tabs sized to their words (16 px bold capitals and 22 a side, 8
	// apart, 28 in and 164 down), the card drawn over PopupParentOld, vs AI open as the mockup opens,
	// Close the lit key and Profile only when the welcome named the site.  Without -online neither tab shows.
	GameWindow *cardWindow = TheWindowManager->winGetWindowFromId( parent, TheNameKeyGenerator->nameToKey( "PopupPlayerInfo.wnd:PopupParentOld" ) );
	s_cardFullTop = -1;
	if (cardWindow)
	{
		Int cardWidth, cardHeight;
		cardWindow->winGetSize( &cardWidth, &cardHeight );
		s_cardScale = cardWidth / CARD_WIDTH;
	}
	GameFont *tabFont = UIStyle::menuFont( UIStyle::MENU_TEXT_LABEL + 2, TRUE );
	Int tabLeft = at( 28 );
	for (Int tab = 0; tab < CARD_TAB_COUNT; ++tab)
	{
		s_cardTabIDs[tab] = TheNameKeyGenerator->nameToKey( CARD_TAB_WINDOW[tab] );
		s_cardTabs[tab] = TheWindowManager->winGetWindowFromId( parent, s_cardTabIDs[tab] );
		s_historyAsked[tab] = FALSE;
		s_historyState[tab] = -1;
		s_historySerial[tab] = 0;
		s_history[tab] = OnlineHistory();
		if (s_cardTabs[tab] == NULL)
			continue;
		s_cardTabs[tab]->winHide( !OnlineIsEnabled() );
		if (!OnlineIsEnabled())
			continue;
		GadgetButtonSetText( s_cardTabs[tab], UIStyle::capitals( s_cardTabs[tab]->winGetText() ) );
		DisplayString *label = s_cardTabs[tab]->winGetInstanceData()->getTextDisplayString();
		label->setFont( tabFont );
		Int labelWidth, labelHeight;
		label->getSize( &labelWidth, &labelHeight );
		const Int width = labelWidth + 2 * at( 22 );
		s_cardTabs[tab]->winSetPosition( tabLeft, at( 164 ) );
		s_cardTabs[tab]->winSetSize( width, at( 34 ) );
		tabLeft += width + at( 8 );
		s_cardTabs[tab]->winSetDrawFunc( drawCardTab );
	}
	if (OnlineIsEnabled())
	{
		const AsciiString nick( lookAtPlayerName.c_str() );
		OnlineRequestStats(nick);
		OnlinePlayerInfo card;
		OnlineGetPlayerInfo(nick, card);
		s_onlineInfoSerial = card.serial;
		if (cardWindow && cardWindow->winGetDrawFunc() != drawCardWindow)
		{
			s_cardWindowDraw = cardWindow->winGetDrawFunc();
			cardWindow->winSetDrawFunc( drawCardWindow );
		}
		UIStyle::setMenuKey( buttonClose, TRUE );
		UIStyle::setMenuKey( buttonBuddies, FALSE );
		// the screen under the card steps back and takes no clicks while it is up, as a box's does
		if (parent)
			TheWindowManager->winSetModal( parent );
		openCardTab( CARD_TAB_AI );
		if (buttonBuddies)
			buttonBuddies->winHide( profileUrl( nick ).isEmpty() );
		makeAddFriendKey();
		updateAddFriendKey();
		updateInvitePartyKey();
		placeCardKeys();
	}
	PopulatePlayerInfoWindows("PopupPlayerInfo.wnd");

	// we're on the myinfo screen; -online has no locale to set and filters no alphabet
	if(lookAtPlayerID == TheGameSpyInfo->getLocalProfileID() && !OnlineIsEnabled())
	{
		//buttonbuttonOptions->winHide(FALSE);
		buttonSetLocale->winHide(FALSE);
		buttonDeleteAccount->winHide(TRUE); // set back to false when we have this worked out.
		checkBoxAsianFont->winHide(FALSE);
		checkBoxNonAsianFont->winHide(FALSE);
	}
	else
	{
		//buttonbuttonOptions->winHide(TRUE);
		buttonSetLocale->winHide(TRUE);
		buttonDeleteAccount->winHide(TRUE);
		checkBoxAsianFont->winHide(TRUE);
		checkBoxNonAsianFont->winHide(TRUE);
	}

	// set the asian check boxes
	CustomMatchPreferences pref;
	GadgetCheckBoxSetChecked(checkBoxAsianFont,!pref.getDisallowAsianText());
	GadgetCheckBoxSetChecked(checkBoxNonAsianFont,!pref.getDisallowNonAsianText());

	if (isWindows9x())
	{	//check if we're running Win9x variant since they may need different fonts
		if (checkBoxAsianFont)
			checkBoxAsianFont->winEnable(FALSE);
		if (checkBoxNonAsianFont)
			checkBoxNonAsianFont->winEnable(FALSE);
	}

	//TheWindowManager->winSetModal(parent);
} // GameSpyPlayerInfoOverlayInit

//-------------------------------------------------------------------------------------------------
/** Overlay shutdown method */
//-------------------------------------------------------------------------------------------------
void GameSpyPlayerInfoOverlayShutdown( WindowLayout *layout, void *userData )
{
	// hide menu
	layout->hide( TRUE );

	if( parent && OnlineIsEnabled() )
		TheWindowManager->winUnsetModal( parent );
	parent = NULL;

	// the card's words go with it; its windows are destroyed after this
	for (size_t each = 0; each < s_cardStrings.size(); ++each)
		TheDisplayStringManager->freeDisplayString( s_cardStrings[each] );
	s_cardStrings.clear();
	s_cardWords.clear();
	s_cardPictures.clear();
	s_cardBoxes.clear();
	for (Int tab = 0; tab < CARD_TAB_COUNT; ++tab)
		s_cardTabs[tab] = NULL;
	s_addFriend = NULL;		// children of the card, destroyed with it
	s_addFriendState = -1;
	s_inviteParty = NULL;
	s_invitePartyState = -1;
	s_cardDirty = TRUE;

	// our shutdown is complete
	isOverlayActive = false;
}  // GameSpyPlayerInfoOverlayShutdown


//-------------------------------------------------------------------------------------------------
/** Overlay update method */
//-------------------------------------------------------------------------------------------------
void GameSpyPlayerInfoOverlayUpdate( WindowLayout * layout, void *userData)
{
	if (raiseMessageBox)
		RaiseGSMessageBox();
	raiseMessageBox = false;

	// -online: the stats{nick} answer (or its noplayer) and the history land in the session, not the
	// PS queue.  Only this player's numbers lay the card out again, not the room's pings.
	if (OnlineIsEnabled() && isOverlayActive)
	{
		OnlineUpdate();
		OnlinePlayerInfo card;
		if (OnlineGetPlayerInfo(AsciiString(lookAtPlayerName.c_str()), card) && card.serial != s_onlineInfoSerial)
		{
			s_onlineInfoSerial = card.serial;
			s_cardDirty = TRUE;
		}
		pollCardHistory();
		updateAddFriendKey();
		updateInvitePartyKey();
		placeCardKeys();
	}
}// GameSpyPlayerInfoOverlayUpdate

//-------------------------------------------------------------------------------------------------
/** Overlay input callback */
//-------------------------------------------------------------------------------------------------
WindowMsgHandledType GameSpyPlayerInfoOverlayInput( GameWindow *window, UnsignedInt msg,
																			 WindowMsgData mData1, WindowMsgData mData2 )
{
	switch( msg ) 
	{

		// --------------------------------------------------------------------------------------------
		case GWM_CHAR:
		{
			UnsignedByte key = mData1;
			UnsignedByte state = mData2;

			switch( key )
			{

				// ----------------------------------------------------------------------------------------
				case KEY_ENTER:		// -online: Close is the card's lit key, Enter's
					if( !OnlineIsEnabled() )
						break;
				case KEY_ESC:
				{

					//
					// send a simulated selected event to the parent window of the
					// back/exit button
					//
					if( BitTest( state, KEY_STATE_UP ) )
					{
						TheWindowManager->winSendSystemMsg( window, GBM_SELECTED,
																							(WindowMsgData)buttonClose, buttonCloseID );

					}  // end if

					// don't let key fall through anywhere else
					return MSG_HANDLED;

				}  // end escape

			}  // end switch( key )

		}  // end char

	}  // end switch( msg )

	return MSG_IGNORED;
}// GameSpyPlayerInfoOverlayInput
static void messageBoxYes( void );
//-------------------------------------------------------------------------------------------------
/** Overlay window system callback */
//-------------------------------------------------------------------------------------------------
WindowMsgHandledType GameSpyPlayerInfoOverlaySystem( GameWindow *window, UnsignedInt msg, 
														 WindowMsgData mData1, WindowMsgData mData2 )
{
	UnicodeString txtInput;

	switch( msg )
	{
		
		
		case GWM_CREATE:
			{
				
				break;
			} // case GWM_DESTROY:

		case GWM_DESTROY:
			{
				break;
			} // case GWM_DESTROY:

		case GWM_INPUT_FOCUS:
			{	
				// if we're givin the opportunity to take the keyboard focus we must say we want it
				if( mData1 == TRUE )
					*(Bool *)mData2 = TRUE;

				return MSG_HANDLED;
			}//case GWM_INPUT_FOCUS:
		case GBM_SELECTED:
			{
				GameWindow *control = (GameWindow *)mData1;
				Int controlID = control->winGetWindowId();

				if (controlID == buttonCloseID)
				{
					RefreshGameListBoxes();
					GameSpyCloseOverlay( GSOVERLAY_PLAYERINFO );
				}
				else if (controlID == buttonBuddiesID)
				{
					// -online: Profile on zerohour.gg; EA's buddy list otherwise
					if (OnlineIsEnabled())
					{
						const AsciiString url = profileUrl( AsciiString( lookAtPlayerName.c_str() ) );
						const Bool opened = url.isNotEmpty() && OnlineOpenURL( url.str() );
						DEBUG_LOG(( "ONLINE PROFILE url=%s opened=%d\n", url.str(), (Int)opened ));		// the browser, no window here
					}
					else
					{
						RefreshGameListBoxes();
						GameSpyOpenOverlay( GSOVERLAY_BUDDY );
					}
				}
				else if (s_inviteParty != NULL && control == s_inviteParty)
				{
					// -online Team vs AI: into our party by nick, friend or not; SENT until he answers
					OnlineInviteToParty( AsciiString( lookAtPlayerName.c_str() ) );
					s_invitePartyState = INVITE_PARTY_SENT;
					s_inviteParty->winEnable( FALSE );
					GadgetButtonSetText( s_inviteParty, UIStyle::capitals( TheGameText->fetch( "GUI:FriendRequestSent" ) ) );
				}
				else if (s_addFriend != NULL && control == s_addFriend)
				{
					// -online: a friend request by the card's nick; SENT until the service's row comes back
					OnlineAddFriend( AsciiString( lookAtPlayerName.c_str() ) );
					DEBUG_LOG(("ONLINE FRIEND screen add %s from the card\n", lookAtPlayerName.c_str()));
					s_addFriendState = ADD_FRIEND_SENT;
					s_addFriend->winEnable( FALSE );
					GadgetButtonSetText( s_addFriend, UIStyle::capitals( TheGameText->fetch( "GUI:FriendRequestSent" ) ) );
				}
				else if (controlID == s_cardTabIDs[CARD_TAB_QUICK_MATCH])
				{
					openCardTab( CARD_TAB_QUICK_MATCH );
				}
				else if (controlID == s_cardTabIDs[CARD_TAB_AI])
				{
					openCardTab( CARD_TAB_AI );
				}
//				else if (controlID == buttonOptionsID)
//				{
//					RefreshGameListBoxes();
//					GameSpyCloseOverlay( GSOVERLAY_PLAYERINFO );
//					GameSpyOpenOverlay( GSOVERLAY_OPTIONS );
//				}
				else if (controlID == buttonSetLocaleID)
				{
					RefreshGameListBoxes();
					GameSpyCloseOverlay( GSOVERLAY_PLAYERINFO );
					if (!GameSpyIsOverlayOpen(GSOVERLAY_LOCALESELECT))
						GameSpyOpenOverlay( GSOVERLAY_LOCALESELECT );
					ReOpenPlayerInfo();
				}
				else if (controlID == buttonDeleteAccountID)
				{
					RefreshGameListBoxes();
					GameSpyCloseOverlay( GSOVERLAY_PLAYERINFO );
					MessageBoxYesNo(TheGameText->fetch("GUI:DeleteAccount"), TheGameText->fetch("GUI:AreYouSureDeleteAccount"),messageBoxYes, NULL);
				}
				else if (controlID == checkBoxAsianFontID)
				{
					Bool isChecked = !GadgetCheckBoxIsChecked(control);
					CustomMatchPreferences pref;
					pref.setDisallowAsianText(isChecked);
					pref.write();
					if(TheGameSpyInfo)
						TheGameSpyInfo->setDisallowAsianText(isChecked);
					if(isChecked && !GadgetCheckBoxIsChecked(checkBoxNonAsianFont))
					{
						GadgetCheckBoxSetChecked(checkBoxNonAsianFont, TRUE);
						CustomMatchPreferences pref;
						pref.setDisallowNonAsianText(FALSE);
						pref.write();
						if(TheGameSpyInfo)
							TheGameSpyInfo->setDisallowNonAsianText(FALSE);
					}

				}
				else if (controlID == checkBoxNonAsianFontID)
				{
					Bool isChecked = !GadgetCheckBoxIsChecked(control);
					CustomMatchPreferences pref;
					pref.setDisallowNonAsianText(isChecked);
					pref.write();				
					if(TheGameSpyInfo)
						TheGameSpyInfo->setDisallowNonAsianText(isChecked);
					if(isChecked && !GadgetCheckBoxIsChecked(checkBoxAsianFont))
					{
						GadgetCheckBoxSetChecked(checkBoxAsianFont, TRUE);
						CustomMatchPreferences pref;
						pref.setDisallowAsianText(FALSE);
						pref.write();
						if(TheGameSpyInfo)
							TheGameSpyInfo->setDisallowAsianText(FALSE);
					}
				}

				break;
			}// case GBM_SELECTED:
	
		default:
			return MSG_IGNORED;

	}//Switch

	return MSG_HANDLED;
}// GameSpyPlayerInfoOverlaySystem

static void messageBoxYes( void )
{
	BuddyRequest breq;
	breq.buddyRequestType = BuddyRequest::BUDDYREQUEST_DELETEACCT;
	TheGameSpyBuddyMessageQueue->addRequest( breq );
	TheGameSpyInfo->setLocalProfileID(0);
	
}
