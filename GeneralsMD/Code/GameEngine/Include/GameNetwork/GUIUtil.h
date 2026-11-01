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
class GameSlot;

// Hides the named windows of a room while a popup (a map list) lies over them; showing brings back
// only the ones the last hide took down, so a box hidden for its own reason stays hidden.
void ShowUnderlyingGUIElements( Bool show, const char *layoutFilename, const char *parentName,
															 const char **gadgetsToHide, const char **perPlayerGadgetsToHide );

// Every seat a lobby can offer - Open, Closed and the six rungs of the AI ladder - in one list,
// each entry tagged with the SlotState it stands for.  allowTakeover adds the empty seat you
// take over in game, which only the skirmish lobby offers.
void PopulatePlayerSlotComboBox(GameWindow *comboBox, Int color, Bool allowTakeover);
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

// The lobby's own tab strip: one page of host settings, and the window that page covers - the chat
// log in the two network lobbies, the map info list in the skirmish one.  All three screens share
// these because only one lobby is ever up, and because a tab strip written three times drifts.
// InitLobbyTabs finds the windows and opens on the chat log; LobbyTabClicked answers TRUE when the
// click was one of the two tabs and it has already switched pages.
void InitLobbyTabs(GameWindow *parent, const char *layoutFilename,
									 const char *otherTabName, const char *otherWindowName);
Bool LobbyTabClicked(NameKeyType controlID);
void ShutdownLobbyTabs(void);

// The personality column: one combo box a seat, one entry per AIPersonality in enum order with the
// value as item data, shown only on a computer player's seat and changed only by the host; on any
// other seat UpdateAIPersonaComboBoxes widens the player box over the empty column.  The
// layout in a stale Run/Window may not have the column, so a box that is not there stays NULL and
// its seat plays Normal.  HandleAIPersonaSelection answers TRUE when the click was one of the boxes
// and changed its slot, so a network lobby knows to send the options round.
void InitAIPersonaComboBoxes(GameWindow *parent, const char *layoutFilename,
														 GameWindow *comboPersona[], NameKeyType comboPersonaID[]);
void UpdateAIPersonaComboBoxes(GameInfo *myGame, GameWindow *comboPersona[]);
Bool HandleAIPersonaSelection(GameInfo *myGame, GameWindow *comboPersona[], NameKeyType comboPersonaID[],
															NameKeyType controlID);

// The handicap column: one combo box a seat, 100 down to 10 percent with the value as item data.
// Whoever may change a seat's team may change its handicap, so UpdateHandicapComboBoxes, called after
// UpdateSlotList, copies the team box's enabled state.  HandleHandicapSelection answers the new
// percent when the click was one of the boxes and is a change, 0 otherwise.  The host's pick goes into
// the slot, unless the options string has no room for it; a guest's waits for the host's slot list.
void InitHandicapComboBoxes(GameWindow *parent, const char *layoutFilename,
														GameWindow *comboHandicap[], NameKeyType comboHandicapID[]);
void UpdateHandicapComboBoxes(GameInfo *myGame, GameWindow *comboHandicap[], GameWindow *comboTeam[]);
Int HandleHandicapSelection(GameInfo *myGame, GameWindow *comboHandicap[], NameKeyType comboHandicapID[],
														NameKeyType controlID);

// What a seat row shows beside its combo boxes: a bar and a swatch in the seat's colour, a swatch in
// the team's colour (both framed like the check boxes), and in the network rooms a green card for
// a player who is ready and a gold edge round the host's.  All of it is drawn from the slot on every
// update and none of it takes input.  A layout without the windows (a stale Run/Window) leaves them
// NULL and the room looks as it did.
void InitLobbySeatVisuals(GameWindow *parent, const char *layoutFilename);
void UpdateLobbySeatVisuals(GameInfo *myGame);
void ShutdownLobbySeatVisuals(void);
// Team 0 to 3 (shown as 1 to 4) each have a colour none of the players can pick; no team is the
// empty seat's slate.  The map's start spots wear
// them too.
Color LobbyTeamColor(Int team);
Color LobbySeatColor(const GameSlot *slot);
// Black or white, whichever has the higher WCAG contrast on this fill.
Color LobbyTextColorOn(Color fill);
// A taken start spot over EA's PlayerStart drawn in this rectangle: its middle in the colour and its
// ring as EA drew it.  The room's spot buttons and the load screen's map both draw it.
void LobbyStartSpotFill(Int x, Int y, Int width, Int height, Color fill);
// The map pickers' Official / Unofficial radio buttons drawn as UIStyle tabs: the open one's label
// ACTIVE, a disabled one's MUTED.  The pickers' init sets it as both buttons' draw function.
void DrawMapListTab(GameWindow *window, class WinInstanceData *instData);
// The pickers' third tab, Random: a map type and a size.  The preview shows the shipped map the type
// names, as the Official tab would; the generated map is made on Accept, with a fresh seed, for the
// players seated then.  The picker's init calls RandomMapTabInit after filling its list (filling it
// moves the preview), its system callback offers every message to RandomMapTabHandle first, and
// Accept takes RandomMapTabMap(), which stages that map, whenever that is not empty.  allowed FALSE greys the tab out (a ranked
// online room plays official maps only).  RandomMapTabShutdown forgets the windows.
void RandomMapTabInit(const char *layoutFilename, GameWindow *parent, GameWindow *preview,
										GameWindow *startSpots[], const GameInfo *game, Bool allowed);
Bool RandomMapTabHandle(UnsignedInt msg, UnsignedIntPtr mData1, UnsignedIntPtr mData2);
AsciiString RandomMapTabMap(void);
void RandomMapTabShutdown(void);
// A generated map is for the players in the room, humans and AI, observers left out.  The host's
// room calls this every frame and again on Start: when the seat count no longer matches the map,
// the same seed, template and size are staged for the new count and become the room's map, CRC,
// size and start spots, so joiners rebuild it from the name.  TRUE when the map changed; the room
// then repaints and sends its options.
Bool FitRandomMapToRoom(GameInfo *game);

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
