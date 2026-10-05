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

// FILE: GUIUtil.cpp //////////////////////////////////////////////////////
// Author: Matthew D. Campbell, Sept 2002

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "GameNetwork/GUIUtil.h"
#include "GameNetwork/NetworkDefs.h"
#include "GameClient/GameWindowManager.h"
#include "GameClient/MapUtil.h"
#include "Common/NameKeyGenerator.h"

#include "Common/MultiplayerSettings.h"
#include "GameClient/GadgetCheckBox.h"
#include "GameClient/GadgetListBox.h"
#include "GameClient/GadgetComboBox.h"
#include "GameClient/GadgetTextEntry.h"
#include "GameClient/GadgetStaticText.h"
#include "GameClient/GadgetPushButton.h"
#include "GameClient/GameText.h"
#include "GameLogic/GameLogic.h" // SUPERWEAPON_RESTRICT_COUNT
#include "GameNetwork/GameInfo.h"
#include "Common/PlayerTemplate.h"
#include "GameNetwork/LANAPICallbacks.h" // for acceptTrueColor, etc
#include "GameClient/ChallengeGenerals.h"

#ifdef _INTERNAL
// for occasional debugging...
//#pragma optimize("", off)
//#pragma MESSAGE("************************************** WARNING, optimization disabled for debugging purposes")
#endif

// -----------------------------------------------------------------------------

static Bool winInitialized = FALSE;

void EnableSlotListUpdates( Bool val )
{
	winInitialized = val;
}

Bool AreSlotListUpdatesEnabled( void )
{
	return winInitialized;
}

// -----------------------------------------------------------------------------

void EnableAcceptControls(Bool Enabled, GameInfo *myGame, GameWindow *comboPlayer[],
										GameWindow *comboColor[], GameWindow *comboPlayerTemplate[],
										GameWindow *comboTeam[], GameWindow *buttonAccept[], GameWindow *buttonStart,
										GameWindow *buttonMapStartPosition[], Int slotNum)
{
	if(slotNum == -1 || slotNum >= MAX_SLOTS )
		slotNum = myGame->getLocalSlotNum();

	Bool isObserver = myGame->getConstSlot(slotNum)->getPlayerTemplate() == PLAYERTEMPLATE_OBSERVER;

	if( !myGame->amIHost() && (buttonStart != NULL) )
		buttonStart->winEnable(Enabled);
	if(comboColor[slotNum])
	{
		if (isObserver)
		{
			GadgetComboBoxHideList(comboColor[slotNum]);
		}
		comboColor[slotNum]->winEnable(Enabled && !isObserver);
	}
	if(comboPlayerTemplate[slotNum])
		comboPlayerTemplate[slotNum]->winEnable(Enabled);
	if(comboTeam[slotNum])
	{
		if (isObserver)
		{
			GadgetComboBoxHideList(comboTeam[slotNum]);
		}
		comboTeam[slotNum]->winEnable(Enabled && !isObserver);
	}

	Bool canChooseStartSpot = FALSE;
	if (!isObserver)
		canChooseStartSpot = TRUE;
	for (Int i=0; i<MAX_SLOTS && !canChooseStartSpot && myGame->amIHost(); ++i)
	{
		if (myGame->getConstSlot(i) && myGame->getConstSlot(i)->isAI())
			canChooseStartSpot = TRUE;
	}

	if (slotNum == myGame->getLocalSlotNum())
	{
		if (myGame->getConstSlot(myGame->getLocalSlotNum())->hasMap())
		{
			for (Int i=0; i<MAX_SLOTS; ++i)
			{
				if (buttonMapStartPosition[i])
				{
					buttonMapStartPosition[i]->winEnable(Enabled && canChooseStartSpot);
				}
			}
		}
		else
		{
			for (Int i=0; i<MAX_SLOTS; ++i)
			{
				if (buttonMapStartPosition[i])
					buttonMapStartPosition[i]->winEnable(FALSE);
			}
		}
	}
}

// -----------------------------------------------------------------------------

void ShowUnderlyingGUIElements( Bool show, const char *layoutFilename, const char *parentName,
															 const char **gadgetsToHide, const char **perPlayerGadgetsToHide )
{
	AsciiString parentNameStr;
	parentNameStr.format("%s:%s", layoutFilename, parentName);
	NameKeyType parentID = NAMEKEY(parentNameStr);
	GameWindow *parent = TheWindowManager->winGetWindowFromId( NULL, parentID );
	if (!parent)
	{
		DEBUG_CRASH(("Window %s not found\n", parentNameStr.str()));
		return;
	}

	// hide some GUI elements of the screen underneath
	GameWindow *win;

	Int player;
	const char **text;

	text = gadgetsToHide;
	while (*text)
	{
		AsciiString gadgetName;
		gadgetName.format("%s:%s", layoutFilename, *text);
		win	= TheWindowManager->winGetWindowFromId( parent, NAMEKEY(gadgetName) );
		//DEBUG_ASSERTCRASH(win, ("Cannot find %s to show/hide it", gadgetName.str()));
		if (win)
		{
			win->winHide( !show );
		}
		++text;
	}

	text = perPlayerGadgetsToHide;
	while (*text)
	{
		for (player = 0; player < MAX_SLOTS; ++player)
		{
			AsciiString gadgetName;
			gadgetName.format("%s:%s%d", layoutFilename, *text, player);
			win	= TheWindowManager->winGetWindowFromId( parent, NAMEKEY(gadgetName) );
			//DEBUG_ASSERTCRASH(win, ("Cannot find %s to show/hide it", gadgetName.str()));
			if (win)
			{
				win->winHide( !show );
			}
		}
		++text;
	}
}

// -----------------------------------------------------------------------------

/** Fill a lobby's player slot combo box with every seat it can offer.
	*
	* All three lobbies used to keep their own list.  The LAN and online ones held five entries in
	* SlotState order and read the choice back as SlotState(position), so a seat appended to the enum
	* - the takeover seat - could not be offered there at all.  Every entry now carries the state it
	* stands for and the position it sits at means nothing. */
void PopulatePlayerSlotComboBox(GameWindow *comboBox, Int color, Bool allowTakeover, Bool allowAI)
{
	if (!comboBox)
		return;

	// the order the ladder climbs in; what each one is called is SlotStateName's business
	static const SlotState seats[] =
	{
		SLOT_OPEN,
		SLOT_CLOSED,
		SLOT_TAKEOVER,
		SLOT_EASY_AI,
		SLOT_MED_AI,
		SLOT_BRUTAL_AI,
	};
	const Int computerSeats = 3;

	// The combo box only grows its listbox when an add crosses the current length, and both
	// GadgetListBoxSetItemData and the add itself silently do nothing past it - so make room for
	// the whole list first, and tell the box how many rows to drop down before the first add.
	Int numSeats = (Int)(sizeof(seats)/sizeof(seats[0]));
	if (!allowTakeover)
		--numSeats;
	if (!allowAI)
		numSeats -= computerSeats;
	GameWindow *listBox = GadgetComboBoxGetListBox(comboBox);
	if (listBox)
	{
		ListboxData *listData = (ListboxData *)listBox->winGetUserData();
		if (listData && listData->listLength < numSeats)
			GadgetListBoxSetListLength(listBox, numSeats);
	}
	GadgetComboBoxSetMaxDisplay(comboBox, numSeats);

	Int shown = 0;
	for (Int i = 0; i < (Int)(sizeof(seats)/sizeof(seats[0])); ++i)
	{
		if (seats[i] == SLOT_TAKEOVER && !allowTakeover)
			continue;
		if (seats[i] >= SLOT_EASY_AI && seats[i] <= SLOT_BRUTAL_AI && !allowAI)
			continue;
		GadgetComboBoxAddEntry(comboBox, SlotStateName(seats[i]), color);
		GadgetComboBoxSetItemData(comboBox, shown, (void *)seats[i]);
		++shown;
	}
	GadgetComboBoxSetSelectedPos(comboBox, 0);
}

void PopulateColorComboBox(Int comboBox, GameWindow *comboArray[], GameInfo *myGame, Bool isObserver)
{
	Int numColors = TheMultiplayerSettings->getNumColors();
	UnicodeString colorName;
	std::vector<bool> availableColors;

	// i is used after the loop; VC6 for-scope let it escape.
	Int i;
	for (i = 0; i < numColors; i++)
		availableColors.push_back(true);

	for (i = 0; i < MAX_SLOTS; i++)
	{
		GameSlot *slot = myGame->getSlot(i);	
		if( slot && (i != comboBox) && (slot->getColor() >= 0 )&& (slot->getColor() < numColors))
		{
			DEBUG_ASSERTCRASH(slot->getColor() >= 0,("We've tried to access array %d and that ain't good",slot->getColor()));
			availableColors[slot->getColor()] = false;
		}
	}

	Bool wasObserver = (GadgetComboBoxGetLength(comboArray[comboBox]) == 1);
	GadgetComboBoxReset(comboArray[comboBox]);

	MultiplayerColorDefinition *def = TheMultiplayerSettings->getColor(PLAYERTEMPLATE_RANDOM);
	Int newIndex = GadgetComboBoxAddEntry(comboArray[comboBox],
		(isObserver)?TheGameText->fetch("GUI:None"):TheGameText->fetch("GUI:???"), def->getColor());
	GadgetComboBoxSetItemData(comboArray[comboBox], newIndex, (void *)-1);

	if (isObserver)
	{
		GadgetComboBoxSetSelectedPos(comboArray[comboBox], 0);
		return;
	}

	for (Int c=0; c<numColors; ++c)
	{
		def = TheMultiplayerSettings->getColor(c);
		if (!def || availableColors[c] == false)
			continue;

		colorName = TheGameText->fetch(def->getTooltipName().str());
		newIndex = GadgetComboBoxAddEntry(comboArray[comboBox], colorName, def->getColor());
		GadgetComboBoxSetItemData(comboArray[comboBox], newIndex, (void *)(intptr_t)c);
	}
	if (wasObserver)
		GadgetComboBoxSetSelectedPos(comboArray[comboBox], 0);
}

// -----------------------------------------------------------------------------

void PopulatePlayerTemplateComboBox(Int comboBox, GameWindow *comboArray[], GameInfo *myGame, Bool allowObservers)
{
	Int numPlayerTemplates = ThePlayerTemplateStore->getPlayerTemplateCount();
	UnicodeString playerTemplateName;

	GadgetComboBoxReset(comboArray[comboBox]);

	MultiplayerColorDefinition *def = TheMultiplayerSettings->getColor(PLAYERTEMPLATE_RANDOM);
	Int newIndex = GadgetComboBoxAddEntry(comboArray[comboBox], TheGameText->fetch("GUI:Random"), def->getColor());
	GadgetComboBoxSetItemData(comboArray[comboBox], newIndex, (void *)PLAYERTEMPLATE_RANDOM);

	std::set<AsciiString> seenSides;

	for (Int c=0; c<numPlayerTemplates; ++c)
	{
		const PlayerTemplate *fac = ThePlayerTemplateStore->getNthPlayerTemplate(c);
		if (!fac)
			continue;

		if (fac->getStartingBuilding().isEmpty())
			continue;

		if ( myGame->oldFactionsOnly() && !fac->isOldFaction() )
		  continue;

		// Prevent players from selecting the disabled Generals for use.
		// This is also enforced at game loading (GameLogic.cpp and UserPreferences.cpp).
		// @todo: unlock these when something rad happens
		Bool disallowLockedGenerals = TRUE;
		const GeneralPersona *general = TheChallengeGenerals->getGeneralByTemplateName(fac->getName());
		Bool startsLocked = general ? !general->isStartingEnabled() : FALSE;
		if (disallowLockedGenerals && startsLocked)
			continue;


		AsciiString side;
		side.format("SIDE:%s", fac->getSide().str());
		if (seenSides.find(side) != seenSides.end())
			continue;

		seenSides.insert(side);

		newIndex = GadgetComboBoxAddEntry(comboArray[comboBox], TheGameText->fetch(side), def->getColor());
		GadgetComboBoxSetItemData(comboArray[comboBox], newIndex, (void *)(intptr_t)c);
	}
	seenSides.clear();

	// disabling observers for Multiplayer test
	if (allowObservers)
	{
		def = TheMultiplayerSettings->getColor(PLAYERTEMPLATE_OBSERVER);
		newIndex = GadgetComboBoxAddEntry(comboArray[comboBox], TheGameText->fetch("GUI:Observer"), def->getColor());
		GadgetComboBoxSetItemData(comboArray[comboBox], newIndex, (void *)PLAYERTEMPLATE_OBSERVER);
	}
	GadgetComboBoxSetSelectedPos(comboArray[comboBox], 0);

}

// -----------------------------------------------------------------------------

void PopulateTeamComboBox(Int comboBox, GameWindow *comboArray[], GameInfo *myGame, Bool isObserver)
{
	Int numTeams = MAX_SLOTS/2;
	UnicodeString teamName;

	GadgetComboBoxReset(comboArray[comboBox]);

	MultiplayerColorDefinition *def = TheMultiplayerSettings->getColor(PLAYERTEMPLATE_RANDOM);
	Int newIndex = GadgetComboBoxAddEntry(comboArray[comboBox], TheGameText->fetch("Team:0"), def->getColor());
	GadgetComboBoxSetItemData(comboArray[comboBox], newIndex, (void *)-1);

	if (isObserver)
	{
		GadgetComboBoxSetSelectedPos(comboArray[comboBox], 0);
		return;
	}

	for (Int c=0; c<numTeams; ++c)
	{
		AsciiString teamStr;
		teamStr.format("Team:%d", c + 1);
		teamName = TheGameText->fetch(teamStr.str());
		newIndex = GadgetComboBoxAddEntry(comboArray[comboBox], teamName, def->getColor());
		GadgetComboBoxSetItemData(comboArray[comboBox], newIndex, (void *)(intptr_t)c);
	}
	GadgetComboBoxSetSelectedPos(comboArray[comboBox], 0);
}

// -----------------------------------------------------------------------------
static UnicodeString formatMoneyForStartingCashComboBox( const Money & moneyAmount )
{
  UnicodeString rtn;
  rtn.format( TheGameText->fetch( "GUI:StartingMoneyFormat" ), moneyAmount.countMoney() );
  return rtn;
}

void PopulateStartingCashComboBox(GameWindow *comboBox, GameInfo *myGame)
{
  GadgetComboBoxReset(comboBox);

  const MultiplayerStartingMoneyList & startingCashMap = TheMultiplayerSettings->getStartingMoneyList(); 
  Int currentSelectionIndex = -1;
  
  // it is used after the loop; VC6 for-scope let it escape.
  MultiplayerStartingMoneyList::const_iterator it;
  for ( it = startingCashMap.begin(); it != startingCashMap.end(); it++ )
  {
    Int newIndex = GadgetComboBoxAddEntry(comboBox, formatMoneyForStartingCashComboBox( *it ), 
                                          comboBox->winGetEnabled() ? comboBox->winGetEnabledTextColor() : comboBox->winGetDisabledTextColor());
    GadgetComboBoxSetItemData(comboBox, newIndex, (void *)(uintptr_t)it->countMoney());

    if ( myGame->getStartingCash().amountEqual( *it ) )
    {
      currentSelectionIndex = newIndex;
    }
  }

  if ( currentSelectionIndex == -1 )
  {
    // a StartingCash from the player's own INI that Multiplayer.ini does not list: show it as an
    // entry of its own. The item data read the loop's end iterator here.
    currentSelectionIndex = GadgetComboBoxAddEntry(comboBox, formatMoneyForStartingCashComboBox( myGame->getStartingCash() ),
                                          comboBox->winGetEnabled() ? comboBox->winGetEnabledTextColor() : comboBox->winGetDisabledTextColor());
    GadgetComboBoxSetItemData(comboBox, currentSelectionIndex, (void *)(uintptr_t)myGame->getStartingCash().countMoney() );
  }

  GadgetComboBoxSetSelectedPos(comboBox, currentSelectionIndex);
}

// -----------------------------------------------------------------------------
// Peace time: minutes at the head of the match during which nobody can shoot anybody and a
// command center's yard burns.  One list for both network lobbies; the skirmish lobby has no such
// control, because a skirmish is played against computer players and those have no peace time.
static const Int thePeaceTimeChoices[] = { 0, 3, 5, 10, 15 };

void PopulatePeaceTimeComboBox(GameWindow *comboBox, GameInfo *myGame, Bool hostMayEdit)
{
  GadgetComboBoxReset(comboBox);

  Color color = comboBox->winGetEnabled() ? comboBox->winGetEnabledTextColor() : comboBox->winGetDisabledTextColor();
  for ( Int i = 0; i < (Int)(sizeof(thePeaceTimeChoices)/sizeof(thePeaceTimeChoices[0])); i++ )
  {
    UnicodeString text;
    if ( thePeaceTimeChoices[i] == 0 )
      text = TheGameText->fetch( "GUI:PeaceTimeOff" );
    else
      text.format( TheGameText->fetch( "GUI:PeaceTimeFormat" ), thePeaceTimeChoices[i] );

    Int newIndex = GadgetComboBoxAddEntry(comboBox, text, color);
    GadgetComboBoxSetItemData(comboBox, newIndex, (void *)(intptr_t)thePeaceTimeChoices[i]);
  }

  UpdatePeaceTimeComboBox(comboBox, myGame, hostMayEdit);
}

void UpdatePeaceTimeComboBox(GameWindow *comboBox, GameInfo *myGame, Bool hostMayEdit)
{
  // getPeaceTime() already answers 0 for a lobby with a computer player in it, so the box shows
  // "No Peace Time" on its own; this is what tells the host why the box has stopped responding
  comboBox->winEnable( hostMayEdit && !myGame->hasAIPlayers() );

  Int itemCount = GadgetComboBoxGetLength(comboBox);
  for ( Int index = 0; index < itemCount; index++ )
  {
    if ( (Int)(intptr_t)GadgetComboBoxGetItemData(comboBox, index) == myGame->getPeaceTime() )
    {
      Int selected = -1;
      GadgetComboBoxGetSelectedPos( comboBox, &selected );
      if ( selected != index )
        GadgetComboBoxSetSelectedPos(comboBox, index, TRUE);
      return;
    }
  }

  // a host on a build with a longer list than ours: show the nearest thing we have rather than
  // leaving the box blank
  GadgetComboBoxSetSelectedPos(comboBox, 0, TRUE);
}

Int PeaceTimeFromComboBox(GameWindow *comboBox)
{
  Int selIndex = -1;
  GadgetComboBoxGetSelectedPos(comboBox, &selIndex);
  if ( selIndex < 0 )
    return 0;
  return (Int)(intptr_t)GadgetComboBoxGetItemData(comboBox, selIndex);
}

// -----------------------------------------------------------------------------
// The superweapon rule.  EA shipped a checkbox, which can say two things about a rule that has
// three, and the number behind it was a cap for everybody alike.  The three entries here are modes;
// what each one leaves a given player is SuperweaponBuildCap's business.
static const Int theSuperweaponChoices[] =
{
  SUPERWEAPONS_ALLOW, SUPERWEAPONS_LIMIT, SUPERWEAPONS_NONE
};

static const char * theSuperweaponCaptions[] =
{
  "GUI:SuperweaponsAllow", "GUI:SuperweaponsLimit", "GUI:SuperweaponsNone"
};

void PopulateSuperweaponComboBox(GameWindow *comboBox, GameInfo *myGame, Bool hostMayEdit)
{
  GadgetComboBoxReset(comboBox);

  Color color = comboBox->winGetEnabled() ? comboBox->winGetEnabledTextColor() : comboBox->winGetDisabledTextColor();
  for ( Int i = 0; i < (Int)(sizeof(theSuperweaponChoices)/sizeof(theSuperweaponChoices[0])); i++ )
  {
    Int newIndex = GadgetComboBoxAddEntry(comboBox, TheGameText->fetch( theSuperweaponCaptions[i] ), color);
    GadgetComboBoxSetItemData(comboBox, newIndex, (void *)(intptr_t)theSuperweaponChoices[i]);
  }

  UpdateSuperweaponComboBox(comboBox, myGame, hostMayEdit);
}

void UpdateSuperweaponComboBox(GameWindow *comboBox, GameInfo *myGame, Bool hostMayEdit)
{
  comboBox->winEnable( hostMayEdit );

  Int restriction = myGame->getSuperweaponRestriction();
  Int itemCount = GadgetComboBoxGetLength(comboBox);
  for ( Int index = 0; index < itemCount; index++ )
  {
    if ( (Int)(intptr_t)GadgetComboBoxGetItemData(comboBox, index) == restriction )
    {
      Int selected = -1;
      GadgetComboBoxGetSelectedPos( comboBox, &selected );
      if ( selected != index )
        GadgetComboBoxSetSelectedPos(comboBox, index, TRUE);
      return;
    }
  }

  // a mode this build does not have an entry for, which is what a preferences file or a host from
  // another build can hand us: the game plays unrestricted then, so the box says so
  GadgetComboBoxSetSelectedPos(comboBox, 0, TRUE);
}

Int SuperweaponRestrictionFromComboBox(GameWindow *comboBox)
{
  Int selIndex = -1;
  GadgetComboBoxGetSelectedPos(comboBox, &selIndex);
  if ( selIndex < 0 )
    return SUPERWEAPONS_ALLOW;
  return (Int)(intptr_t)GadgetComboBoxGetItemData(comboBox, selIndex);
}

void UpdateUnitLimitCheckBox(GameWindow *checkBox, GameInfo *myGame, Bool hostMayEdit)
{
  checkBox->winEnable( hostMayEdit );

  const Bool unitLimit = myGame->getUnitLimit();
  if ( GadgetCheckBoxIsChecked( checkBox ) != unitLimit )
    GadgetCheckBoxSetChecked( checkBox, unitLimit );
}

void UpdateProRulesCheckBox(GameWindow *checkBox, GameInfo *myGame, Bool hostMayEdit)
{
  checkBox->winEnable( hostMayEdit );

  const Bool proRules = myGame->getProRules();
  if ( GadgetCheckBoxIsChecked( checkBox ) != proRules )
    GadgetCheckBoxSetChecked( checkBox, proRules );
}

// -----------------------------------------------------------------------------
// Income sharing.  The entries are the IncomeSharing values in order, so an entry's position is
// its value.
static const char * theIncomeSharingCaptions[ INCOME_SHARING_COUNT ] =
{
  "GUI:IncomeSharingOff", "GUI:IncomeSharingTech", "GUI:IncomeSharingAll"
};

void PopulateIncomeSharingComboBox(GameWindow *comboBox, GameInfo *myGame, Bool hostMayEdit)
{
  GadgetComboBoxReset(comboBox);

  Color color = comboBox->winGetEnabled() ? comboBox->winGetEnabledTextColor() : comboBox->winGetDisabledTextColor();
  for ( Int i = 0; i < INCOME_SHARING_COUNT; i++ )
    GadgetComboBoxAddEntry(comboBox, TheGameText->fetch( theIncomeSharingCaptions[i] ), color);

  UpdateIncomeSharingComboBox(comboBox, myGame, hostMayEdit);
}

void UpdateIncomeSharingComboBox(GameWindow *comboBox, GameInfo *myGame, Bool hostMayEdit)
{
  comboBox->winEnable( hostMayEdit );

  Int selected = -1;
  GadgetComboBoxGetSelectedPos( comboBox, &selected );
  if ( selected != myGame->getIncomeSharing() )
    GadgetComboBoxSetSelectedPos( comboBox, myGame->getIncomeSharing(), TRUE );
}

Int IncomeSharingFromComboBox(GameWindow *comboBox)
{
  Int selIndex = -1;
  GadgetComboBoxGetSelectedPos(comboBox, &selIndex);
  return selIndex < 0 ? INCOME_SHARING_OFF : selIndex;
}

// -----------------------------------------------------------------------------
// Tech building respawn: minutes a destroyed tech building lies in ruins before a neutral one
// comes back, the rungs GitHub #23 asked for.
static const Int theTechRespawnChoices[] = { 0, 3, 5, 10 };

void PopulateTechRespawnComboBox(GameWindow *comboBox, GameInfo *myGame, Bool hostMayEdit)
{
  GadgetComboBoxReset(comboBox);

  Color color = comboBox->winGetEnabled() ? comboBox->winGetEnabledTextColor() : comboBox->winGetDisabledTextColor();
  for ( Int i = 0; i < (Int)(sizeof(theTechRespawnChoices)/sizeof(theTechRespawnChoices[0])); i++ )
  {
    UnicodeString text;
    if ( theTechRespawnChoices[i] == 0 )
      text = TheGameText->fetch( "GUI:TechRespawnOff" );
    else
      text.format( TheGameText->fetch( "GUI:TechRespawnFormat" ), theTechRespawnChoices[i] );

    Int newIndex = GadgetComboBoxAddEntry(comboBox, text, color);
    GadgetComboBoxSetItemData(comboBox, newIndex, (void *)(intptr_t)theTechRespawnChoices[i]);
  }

  UpdateTechRespawnComboBox(comboBox, myGame, hostMayEdit);
}

void UpdateTechRespawnComboBox(GameWindow *comboBox, GameInfo *myGame, Bool hostMayEdit)
{
  comboBox->winEnable( hostMayEdit );

  Int itemCount = GadgetComboBoxGetLength(comboBox);
  for ( Int index = 0; index < itemCount; index++ )
  {
    if ( (Int)(intptr_t)GadgetComboBoxGetItemData(comboBox, index) == myGame->getTechRespawn() )
    {
      Int selected = -1;
      GadgetComboBoxGetSelectedPos( comboBox, &selected );
      if ( selected != index )
        GadgetComboBoxSetSelectedPos(comboBox, index, TRUE);
      return;
    }
  }

  // a host on a build with a longer list than ours
  GadgetComboBoxSetSelectedPos(comboBox, 0, TRUE);
}

Int TechRespawnFromComboBox(GameWindow *comboBox)
{
  Int selIndex = -1;
  GadgetComboBoxGetSelectedPos(comboBox, &selIndex);
  if ( selIndex < 0 )
    return 0;
  return (Int)(intptr_t)GadgetComboBoxGetItemData(comboBox, selIndex);
}

// -----------------------------------------------------------------------------
// Supply pile limit: how many players may gather from one pile at the same time, GitHub #30.
static const Int theSupplyPileLimitChoices[] = { 0, 1, 2, 3 };

void PopulateSupplyPileLimitComboBox(GameWindow *comboBox, GameInfo *myGame, Bool hostMayEdit)
{
  GadgetComboBoxReset(comboBox);

  Color color = comboBox->winGetEnabled() ? comboBox->winGetEnabledTextColor() : comboBox->winGetDisabledTextColor();
  for ( Int i = 0; i < (Int)(sizeof(theSupplyPileLimitChoices)/sizeof(theSupplyPileLimitChoices[0])); i++ )
  {
    UnicodeString text;
    if ( theSupplyPileLimitChoices[i] == 0 )
      text = TheGameText->fetch( "GUI:SupplyPileLimitOff" );
    else
      text.format( TheGameText->fetch( "GUI:SupplyPileLimitFormat" ), theSupplyPileLimitChoices[i] );

    Int newIndex = GadgetComboBoxAddEntry(comboBox, text, color);
    GadgetComboBoxSetItemData(comboBox, newIndex, (void *)(intptr_t)theSupplyPileLimitChoices[i]);
  }

  UpdateSupplyPileLimitComboBox(comboBox, myGame, hostMayEdit);
}

void UpdateSupplyPileLimitComboBox(GameWindow *comboBox, GameInfo *myGame, Bool hostMayEdit)
{
  comboBox->winEnable( hostMayEdit );

  Int itemCount = GadgetComboBoxGetLength(comboBox);
  for ( Int index = 0; index < itemCount; index++ )
  {
    if ( (Int)(intptr_t)GadgetComboBoxGetItemData(comboBox, index) == myGame->getSupplyPileLimit() )
    {
      Int selected = -1;
      GadgetComboBoxGetSelectedPos( comboBox, &selected );
      if ( selected != index )
        GadgetComboBoxSetSelectedPos(comboBox, index, TRUE);
      return;
    }
  }

  // a host on a build with a longer list than ours
  GadgetComboBoxSetSelectedPos(comboBox, 0, TRUE);
}

Int SupplyPileLimitFromComboBox(GameWindow *comboBox)
{
  return TechRespawnFromComboBox( comboBox ); // the same read: the selected entry's item data
}

// -----------------------------------------------------------------------------
// Game mode: the retail game or one of Apocalypse's two zombie flows.
static const char * theGameModeCaptions[ APOCALYPSE_COUNT ] =
{
  "GUI:GameModeStandard", "GUI:ApocalypseWaves", "GUI:ApocalypseContinuous"
};
// the same modes under a Zombie Flow label, where "Apocalypse:" would say it twice
static const char * theZombieFlowCaptions[ APOCALYPSE_COUNT ] =
{
  "GUI:GameModeStandard", "GUI:ApocalypseFlowWaves", "GUI:ApocalypseFlowContinuous"
};

void PopulateGameModeComboBox(GameWindow *comboBox, Int firstMode, GameInfo *myGame, Bool hostMayEdit)
{
  GadgetComboBoxReset(comboBox);

  const char **captions = ( firstMode == APOCALYPSE_OFF ) ? theGameModeCaptions : theZombieFlowCaptions;
  Color color = comboBox->winGetEnabled() ? comboBox->winGetEnabledTextColor() : comboBox->winGetDisabledTextColor();
  for ( Int mode = firstMode; mode < APOCALYPSE_COUNT; mode++ )
  {
    Int newIndex = GadgetComboBoxAddEntry(comboBox, TheGameText->fetch( captions[mode] ), color);
    GadgetComboBoxSetItemData(comboBox, newIndex, (void *)(intptr_t)mode);
  }

  UpdateGameModeComboBox(comboBox, myGame, hostMayEdit);
}

void UpdateGameModeComboBox(GameWindow *comboBox, GameInfo *myGame, Bool hostMayEdit)
{
  comboBox->winEnable( hostMayEdit );

  Int itemCount = GadgetComboBoxGetLength(comboBox);
  for ( Int index = 0; index < itemCount; index++ )
  {
    if ( (Int)(intptr_t)GadgetComboBoxGetItemData(comboBox, index) == myGame->getApocalypseMode() )
    {
      Int selected = -1;
      GadgetComboBoxGetSelectedPos( comboBox, &selected );
      if ( selected != index )
        GadgetComboBoxSetSelectedPos(comboBox, index, TRUE);
      return;
    }
  }
}

Int GameModeFromComboBox(GameWindow *comboBox)
{
  Int selIndex = -1;
  GadgetComboBoxGetSelectedPos(comboBox, &selIndex);
  if ( selIndex < 0 )
    return -1;
  return (Int)(intptr_t)GadgetComboBoxGetItemData(comboBox, selIndex);
}

// -----------------------------------------------------------------------------
// The lobby tab strip.
static GameWindow *theLobbySettingsPage = NULL;
static GameWindow *theLobbyOtherWindow = NULL;
static GameWindow *theLobbySettingsTab = NULL;
static GameWindow *theLobbyOtherTab = NULL;
static NameKeyType theLobbySettingsTabID = NAMEKEY_INVALID;
static NameKeyType theLobbyOtherTabID = NAMEKEY_INVALID;

static void showLobbySettings( Bool settings )
{
  if ( theLobbySettingsPage == NULL )
    return;

  theLobbySettingsPage->winHide( !settings );
  theLobbyOtherWindow->winHide( settings );
  // the tab you are on is the disabled one, which is the only tab feedback there is without
  // artwork nobody drew
  theLobbySettingsTab->winEnable( !settings );
  theLobbyOtherTab->winEnable( settings );
}

void InitLobbyTabs(GameWindow *parent, const char *layoutFilename,
                   const char *otherTabName, const char *otherWindowName)
{
  AsciiString name;

  name.format( "%s:%s", layoutFilename, "PageLobbySettings" );
  theLobbySettingsPage = TheWindowManager->winGetWindowFromId( parent, TheNameKeyGenerator->nameToKey( name ) );

  name.format( "%s:%s", layoutFilename, "TabLobbySettings" );
  theLobbySettingsTabID = TheNameKeyGenerator->nameToKey( name );
  theLobbySettingsTab = TheWindowManager->winGetWindowFromId( parent, theLobbySettingsTabID );

  name.format( "%s:%s", layoutFilename, otherTabName );
  theLobbyOtherTabID = TheNameKeyGenerator->nameToKey( name );
  theLobbyOtherTab = TheWindowManager->winGetWindowFromId( parent, theLobbyOtherTabID );

  name.format( "%s:%s", layoutFilename, otherWindowName );
  theLobbyOtherWindow = TheWindowManager->winGetWindowFromId( parent, TheNameKeyGenerator->nameToKey( name ) );

  // A player whose Window/Menus copy is older than his exe gets a lobby without the tabs rather
  // than one he cannot click; layouts are not in the multiplayer checksum, so that copy joins games.
  if ( theLobbySettingsPage == NULL || theLobbySettingsTab == NULL
       || theLobbyOtherTab == NULL || theLobbyOtherWindow == NULL )
  {
    DEBUG_LOG(("InitLobbyTabs: %s has no tab strip; the settings stay where the layout put them\n",
               layoutFilename));
    ShutdownLobbyTabs();
    return;
  }

  showLobbySettings( FALSE );
}

Bool LobbyTabClicked(NameKeyType controlID)
{
  if ( theLobbySettingsPage == NULL )
    return FALSE;

  if ( controlID == theLobbySettingsTabID )
  {
    showLobbySettings( TRUE );
    return TRUE;
  }

  if ( controlID == theLobbyOtherTabID )
  {
    showLobbySettings( FALSE );
    return TRUE;
  }

  return FALSE;
}

void ShutdownLobbyTabs(void)
{
  theLobbySettingsPage = NULL;
  theLobbyOtherWindow = NULL;
  theLobbySettingsTab = NULL;
  theLobbyOtherTab = NULL;
  theLobbySettingsTabID = NAMEKEY_INVALID;
  theLobbyOtherTabID = NAMEKEY_INVALID;
}

// -----------------------------------------------------------------------------

//  -----------------------------------------------------------------------------------------
// The slot list displaying function
//-------------------------------------------------------------------------------------------------
void UpdateSlotList( GameInfo *myGame, GameWindow *comboPlayer[],
										GameWindow *comboColor[], GameWindow *comboPlayerTemplate[],
										GameWindow *comboTeam[], GameWindow *buttonAccept[], 
										GameWindow *buttonStart, GameWindow *buttonMapStartPosition[] )
{
	if(!AreSlotListUpdatesEnabled())
		return;
	//LANGameInfo *myGame = TheLAN->GetMyGame();

	const MapMetaData *mapData = TheMapCache->findMap( myGame->getMap() );
	Bool willTransfer = TRUE;
	if (mapData)
	{
		willTransfer = !mapData->m_isOfficial;
	}
	else
	{
		willTransfer = WouldMapTransfer(myGame->getMap());
	}

	if (myGame)
	{
		for( int i =0; i < MAX_SLOTS; i++ )
		{
			GameSlot * slot = myGame->getSlot(i);
			// if i'm host, enable the controls for AI
			if(myGame->amIHost() && slot && slot->isAI())
			{
				EnableAcceptControls(TRUE, myGame, comboPlayer, comboColor, comboPlayerTemplate,
					comboTeam, buttonAccept, buttonStart, buttonMapStartPosition, i);
			}
			else if (slot && myGame->getLocalSlotNum() == i)
			{
				if(slot->isAccepted() && !myGame->amIHost())
				{
					EnableAcceptControls(FALSE, myGame, comboPlayer, comboColor, comboPlayerTemplate,
						comboTeam, buttonAccept, buttonStart, buttonMapStartPosition);
				}
				else
				{
					if (slot->hasMap()) {
						EnableAcceptControls(TRUE, myGame, comboPlayer, comboColor, comboPlayerTemplate,
							comboTeam, buttonAccept, buttonStart, buttonMapStartPosition);
					}
					else
					{
						EnableAcceptControls(willTransfer, myGame, comboPlayer, comboColor, comboPlayerTemplate,
							comboTeam, buttonAccept, buttonStart, buttonMapStartPosition);
					}
				}
				
			}
			else if(myGame->amIHost())
			{
				EnableAcceptControls(FALSE, myGame, comboPlayer, comboColor, comboPlayerTemplate,
					comboTeam, buttonAccept, buttonStart, buttonMapStartPosition, i);
			}
			if(slot && slot->isHuman())
			{
				UnicodeString newName = slot->getName();
				UnicodeString oldName = GadgetComboBoxGetText(comboPlayer[i]);
				if (comboPlayer[i] && newName.compare(oldName))
				{
					GadgetComboBoxSetText(comboPlayer[i], newName);
				}
				if(i!= 0 && buttonAccept && buttonAccept[i])
				{
					buttonAccept[i]->winHide(FALSE);
				//Color In the little accepted boxes
					if(slot->isAccepted())
					{
						if(BitTest(buttonAccept[i]->winGetStatus(), WIN_STATUS_IMAGE	))
							buttonAccept[i]->winEnable(TRUE);
						else
							GadgetButtonSetEnabledColor(buttonAccept[i], acceptTrueColor );
					}
					else
					{
						if(BitTest(buttonAccept[i]->winGetStatus(), WIN_STATUS_IMAGE	))
							buttonAccept[i]->winEnable(FALSE);
						else
							GadgetButtonSetEnabledColor(buttonAccept[i], acceptFalseColor );
					}
				}
			}
			else
			{				
				/* The LAN and online menus fill this box in SlotState order and count on position ==
					 state; the skirmish one has an extra entry and tags every line with its state, so
					 ask the box first and fall back to the plain state when no line claims it. */
				Int pos = slot->getState();
				for (Int item = 0; comboPlayer[i] && item < GadgetComboBoxGetLength(comboPlayer[i]); ++item)
				{
					if ((Int)(intptr_t)GadgetComboBoxGetItemData(comboPlayer[i], item) == slot->getState())
					{
						pos = item;
						break;
					}
				}
				GadgetComboBoxSetSelectedPos(comboPlayer[i], pos, TRUE);
        if( buttonAccept &&  buttonAccept[i] )
				  buttonAccept[i]->winHide(TRUE);
			}
/*
			if (myGame->getLocalSlotNum() == i && i!=0)
			{
				if (comboPlayer[i])
					comboPlayer[i]->winEnable( TRUE );
			}
			else*/ if (!myGame->amIHost())
			{
				if (comboPlayer[i])
					comboPlayer[i]->winEnable( FALSE );
			}
			//if( i == myGame->getLocalSlotNum())
      if((comboColor[i] != NULL) && BitTest(comboColor[i]->winGetStatus(), WIN_STATUS_ENABLED))
				PopulateColorComboBox(i, comboColor, myGame, myGame->getConstSlot(i)->getPlayerTemplate() == PLAYERTEMPLATE_OBSERVER);
			Int max, idx;
			if (comboColor[i] != NULL) {
				max = GadgetComboBoxGetLength(comboColor[i]);
				for (idx=0; idx<max; ++idx)
				{
					Int color = (Int)(intptr_t)GadgetComboBoxGetItemData(comboColor[i], idx);
					if (color == slot->getColor())
					{
						GadgetComboBoxSetSelectedPos(comboColor[i], idx, TRUE);
						break;
					}
				}
			}

			if (comboTeam[i] != NULL) {
				max = GadgetComboBoxGetLength(comboTeam[i]);
				for (idx=0; idx<max; ++idx)
				{
					Int team = (Int)(intptr_t)GadgetComboBoxGetItemData(comboTeam[i], idx);
					if (team == slot->getTeamNumber())
					{
						GadgetComboBoxSetSelectedPos(comboTeam[i], idx, TRUE);
						break;
					}
				}
			}

			if (comboPlayerTemplate[i] != NULL) {
				max = GadgetComboBoxGetLength(comboPlayerTemplate[i]);
				for (idx=0; idx<max; ++idx)
				{
					Int playerTemplate = (Int)(intptr_t)GadgetComboBoxGetItemData(comboPlayerTemplate[i], idx);
					if (playerTemplate == slot->getPlayerTemplate())
					{
						GadgetComboBoxSetSelectedPos(comboPlayerTemplate[i], idx, TRUE);
						break;
					}
				}
			}
		}
	}
}

// -----------------------------------------------------------------------------
