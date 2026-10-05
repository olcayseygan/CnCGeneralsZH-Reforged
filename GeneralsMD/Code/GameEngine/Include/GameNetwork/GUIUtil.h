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

////////////////////////////////////////////////////////////////////////////////
//																																						//
//  (c) 2001-2003 Electronic Arts Inc.																				//
//																																						//
////////////////////////////////////////////////////////////////////////////////

// FILE: GUIUtil.h //////////////////////////////////////////////////////
// Author: Matthew D. Campbell, Sept 2002

#pragma once

#ifndef __GUIUTIL_H__
#define __GUIUTIL_H__

#include "Common/NameKeyGenerator.h"		// NameKeyType, which LobbyTabClicked takes

class GameWindow;
class GameInfo;

void ShowUnderlyingGUIElements( Bool show, const char *layoutFilename, const char *parentName,
															 const char **gadgetsToHide, const char **perPlayerGadgetsToHide );

// Every seat a lobby can offer - Open, Closed and the six rungs of the AI ladder - in one list,
// each entry tagged with the SlotState it stands for.  allowTakeover adds the empty seat you
// take over in game, which only the skirmish lobby offers; allowAI FALSE leaves the AI ladder out,
// for Apocalypse, which is humans only.
void PopulatePlayerSlotComboBox(GameWindow *comboBox, Int color, Bool allowTakeover, Bool allowAI = TRUE);
void PopulateColorComboBox(Int comboBox, GameWindow *comboArray[], GameInfo *myGame, Bool isObserver = FALSE);
void PopulatePlayerTemplateComboBox(Int comboBox, GameWindow *comboArray[], GameInfo *myGame, Bool allowObservers );
void PopulateTeamComboBox(Int comboBox, GameWindow *comboArray[], GameInfo *myGame, Bool isObserver = FALSE);
void PopulateStartingCashComboBox(GameWindow *comboBox, GameInfo *myGame);

// The peace time rungs both network lobbies offer, from one list so they cannot drift apart.
// UpdatePeaceTimeComboBox puts the box back on whatever the host last sent and greys it out when
// nobody may touch it - a client, or any lobby with a computer player in it, which has no peace
// time.  PeaceTimeFromComboBox reads the minutes back out of the entry the user picked.
void PopulatePeaceTimeComboBox(GameWindow *comboBox, GameInfo *myGame, Bool hostMayEdit);
void UpdatePeaceTimeComboBox(GameWindow *comboBox, GameInfo *myGame, Bool hostMayEdit);
Int PeaceTimeFromComboBox(GameWindow *comboBox);

// The superweapon rule, which the host picks and which travels in the options string as SR.  EA
// shipped it as one number for everybody, the cap on how many of each superweapon a player may have
// standing; it is a mode now, because one number cannot be fair to the USA Superweapon General,
// whose three superweapons are what he pays for a weaker everything else with.  SuperweaponBuildCap
// in Player.h turns a mode into that player's cap.
enum
{
	SUPERWEAPONS_ALLOW = 0,
	SUPERWEAPONS_LIMIT = 1,
	SUPERWEAPONS_NONE  = 2
};

// What each mode leaves a player, per superweapon type.  Allow leaves no cap at all, and under No
// everybody but the Superweapon General is barred outright.
enum
{
	SUPERWEAPONS_LIMIT_GENERAL = 4,
	SUPERWEAPONS_LIMIT_OTHERS  = 1,
	SUPERWEAPONS_NONE_GENERAL  = 1
};

void PopulateSuperweaponComboBox(GameWindow *comboBox, GameInfo *myGame, Bool hostMayEdit);
void UpdateSuperweaponComboBox(GameWindow *comboBox, GameInfo *myGame, Bool hostMayEdit);
Int SuperweaponRestrictionFromComboBox(GameWindow *comboBox);

// The unit limit check box, which the host ticks and which travels in the options string as UL.
// UpdateUnitLimitCheckBox puts the box on what the game says and greys it out for anyone who may not
// touch it, and it only sets the box when the box is wrong.
void UpdateUnitLimitCheckBox(GameWindow *checkBox, GameInfo *myGame, Bool hostMayEdit);
// The Pro Rules check box, the same thing for PR.
void UpdateProRulesCheckBox(GameWindow *checkBox, GameInfo *myGame, Bool hostMayEdit);

// The income sharing dropdown, one entry per IncomeSharing in GameInfo.h, travelling as IS.
void PopulateIncomeSharingComboBox(GameWindow *comboBox, GameInfo *myGame, Bool hostMayEdit);
void UpdateIncomeSharingComboBox(GameWindow *comboBox, GameInfo *myGame, Bool hostMayEdit);
Int IncomeSharingFromComboBox(GameWindow *comboBox);

// The tech building respawn dropdown, minutes in each entry's item data, travelling as TR.
void PopulateTechRespawnComboBox(GameWindow *comboBox, GameInfo *myGame, Bool hostMayEdit);
void UpdateTechRespawnComboBox(GameWindow *comboBox, GameInfo *myGame, Bool hostMayEdit);
Int TechRespawnFromComboBox(GameWindow *comboBox);

// The supply pile limit dropdown, players in each entry's item data, travelling as PL.
void PopulateSupplyPileLimitComboBox(GameWindow *comboBox, GameInfo *myGame, Bool hostMayEdit);
void UpdateSupplyPileLimitComboBox(GameWindow *comboBox, GameInfo *myGame, Bool hostMayEdit);
Int SupplyPileLimitFromComboBox(GameWindow *comboBox);

// The game mode dropdown, an ApocalypseMode in each entry's item data, travelling as AP.  The LAN
// room lists every mode from APOCALYPSE_OFF at its top right; single player Apocalypse's room starts
// at APOCALYPSE_WAVES, on a row labelled Zombie Flow, so its entries are just "Waves" and
// "Continuous".  -1 from the getter while nothing is selected.
void PopulateGameModeComboBox(GameWindow *comboBox, Int firstMode, GameInfo *myGame, Bool hostMayEdit);
void UpdateGameModeComboBox(GameWindow *comboBox, GameInfo *myGame, Bool hostMayEdit);
Int GameModeFromComboBox(GameWindow *comboBox);

// The lobby's own tab strip: one page of host settings, and the window that page covers - the chat
// log in the two network lobbies, the map info list in the skirmish one.  All three screens share
// these because only one lobby is ever up, and because a tab strip written three times drifts.
// InitLobbyTabs finds the windows and opens on the chat log; LobbyTabClicked answers TRUE when the
// click was one of the two tabs and it has already switched pages.
void InitLobbyTabs(GameWindow *parent, const char *layoutFilename,
									 const char *otherTabName, const char *otherWindowName);
Bool LobbyTabClicked(NameKeyType controlID);
void ShutdownLobbyTabs(void);

void EnableSlotListUpdates( Bool val );
Bool AreSlotListUpdatesEnabled( void );

void UpdateSlotList( GameInfo *myGame, GameWindow *comboPlayer[],
										GameWindow *comboColor[], GameWindow *comboPlayerTemplate[],
										GameWindow *comboTeam[], GameWindow *buttonAccept[], 
										GameWindow *buttonStart, GameWindow *buttonMapStartPosition[] );

void EnableAcceptControls(Bool Enabled, GameInfo *myGame, GameWindow *comboPlayer[],
										GameWindow *comboColor[], GameWindow *comboPlayerTemplate[],
										GameWindow *comboTeam[], GameWindow *buttonAccept[], GameWindow *buttonStart,
										GameWindow *buttonMapStartPosition[], Int slotNum = -1);

#endif // __GUIUTIL_H__
