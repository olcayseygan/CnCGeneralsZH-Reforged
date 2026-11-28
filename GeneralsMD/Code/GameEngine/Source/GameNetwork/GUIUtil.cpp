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
#include "GameClient/Display.h"
#include "GameClient/DisplayString.h"
#include "GameClient/DisplayStringManager.h"
#include "GameClient/Gadget.h"
#include "GameClient/Image.h"
#include "GameNetwork/Online/OnlineQueues.h"
#include "GameClient/Mouse.h"
#include "GameClient/UIStyle.h"
#include "GameLogic/GameLogic.h" // SUPERWEAPON_RESTRICT_COUNT
#include "GameLogic/AI.h" // AIPersonality, for the personality column
#include "GameNetwork/GameInfo.h"
#include "Common/PlayerTemplate.h"
#include "GameClient/MessageBox.h"
#include "GameNetwork/LANAPICallbacks.h" // for acceptTrueColor, etc
#include "GameClient/ChallengeGenerals.h"
#include "GameClient/DirectStrikeLayout.h"
#include "GameClient/DirectStrikeLobby.h"
#include "GameClient/DirectStrikeTheme.h"
#include "GameClient/KeyDefs.h"
#include "GameClient/FactionPicker.h"
#include "GameClient/MapPicker.h"
#include "Lib/Clock.h"
#include "Common/RandomMapGenerator.h"
#include "Common/RandomMapTemplate.h"
#include "Common/UserPreferences.h"
#include "GameClient/GadgetRadioButton.h"
#include "GameLogic/ApocalypseDirector.h"	// the Apocalypse room's nights and roster, read only
#include "Common/ThingFactory.h"
#include "Common/ThingTemplate.h"
#include <algorithm>

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

// What the hiding took down, so showing brings back exactly that: a seat's personality box, ready
// mark or ping that was already hidden for its own reason stays hidden.  Kept by name, since the
// windows can be destroyed while the list waits.
static std::vector<NameKeyType> theUnderlyingHidden;
// a map picker lies over the seat column: the room look draws no seats under it
static Bool theSeatsUnderPicker = FALSE;

static void hideUnderlying( GameWindow *parent, NameKeyType id )
{
	GameWindow *win = TheWindowManager->winGetWindowFromId( parent, id );
	if (win == NULL || win->winIsHidden())
		return;
	win->winHide( TRUE );
	theUnderlyingHidden.push_back( id );
}

void ShowUnderlyingGUIElements( Bool show, const char *layoutFilename, const char *parentName,
															 const char **gadgetsToHide, const char **perPlayerGadgetsToHide )
{
	theSeatsUnderPicker = !show;
	AsciiString parentNameStr;
	parentNameStr.format("%s:%s", layoutFilename, parentName);
	NameKeyType parentID = NAMEKEY(parentNameStr);
	GameWindow *parent = TheWindowManager->winGetWindowFromId( NULL, parentID );
	if (!parent)
	{
		DEBUG_CRASH(("Window %s not found\n", parentNameStr.str()));
		return;
	}

	if (show)
	{
		for (size_t i = 0; i < theUnderlyingHidden.size(); ++i)
		{
			GameWindow *win = TheWindowManager->winGetWindowFromId( parent, theUnderlyingHidden[i] );
			if (win)
				win->winHide( FALSE );
		}
		theUnderlyingHidden.clear();
		return;
	}

	// hide some GUI elements of the screen underneath; a window the layout does not have is skipped
	for (const char **text = gadgetsToHide; *text; ++text)
	{
		AsciiString gadgetName;
		gadgetName.format("%s:%s", layoutFilename, *text);
		hideUnderlying( parent, NAMEKEY(gadgetName) );
	}

	for (const char **text = perPlayerGadgetsToHide; *text; ++text)
	{
		for (Int player = 0; player < MAX_SLOTS; ++player)
		{
			AsciiString gadgetName;
			gadgetName.format("%s:%s%d", layoutFilename, *text, player);
			hideUnderlying( parent, NAMEKEY(gadgetName) );
		}
	}
}

// -----------------------------------------------------------------------------

/** Fill a lobby's player slot combo box with every seat it can offer.
	*
	* All three lobbies used to keep their own list.  The LAN and online ones held five entries in
	* SlotState order and read the choice back as SlotState(position), so a seat appended to the enum
	* - the takeover seat - could not be offered there at all.  Every entry now carries the state it
	* stands for and the position it sits at means nothing. */
void PopulatePlayerSlotComboBox(GameWindow *comboBox, Int color, Bool allowTakeover, Bool allowComputers)
{
	if (!comboBox)
		return;

	// the order the ladder climbs in, the levels above Hard last (SlotPickOf); what each one is called
	// is SlotStateName's business
	static const Int seats[] =
	{
		SLOT_OPEN,
		SLOT_CLOSED,
		SLOT_TAKEOVER,
		SLOT_EASY_AI,
		SLOT_MED_AI,
		SLOT_BRUTAL_AI,
		AILevelAboveHardRating[0],
		AILevelAboveHardRating[1],
		AILevelAboveHardRating[2],
		AILevelAboveHardRating[3],
	};

	// The combo box only grows its listbox when an add crosses the current length, and both
	// GadgetListBoxSetItemData and the add itself silently do nothing past it - so make room for
	// the whole list first, and tell the box how many rows to drop down before the first add.
	// Open and Closed come first, the takeover seat next, the ladder after it
	const Int firstComputer = 3;
	const Int ladder = (Int)(sizeof(seats)/sizeof(seats[0])) - firstComputer;
	Int numSeats = (Int)(sizeof(seats)/sizeof(seats[0]));
	if (!allowTakeover)
		--numSeats;
	if (!allowComputers)
		numSeats -= ladder;
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
		if (i >= firstComputer && !allowComputers)
			continue;
		const UnicodeString name = seats[i] > SLOT_TAKEOVER ? SlotStateName(SLOT_BRUTAL_AI, seats[i]) : SlotStateName((SlotState)seats[i]);
		GadgetComboBoxAddEntry(comboBox, name, color);
		GadgetComboBoxSetItemData(comboBox, shown, (void *)(intptr_t)seats[i]);
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
		// a colour's name is words in that colour; the swatch beside it keeps the colour itself
		newIndex = GadgetComboBoxAddEntry(comboArray[comboBox], colorName, UIStyle::readable(def->getColor()));
		GadgetComboBoxSetItemData(comboArray[comboBox], newIndex, (void *)(intptr_t)c);
	}
	if (wasObserver)
		GadgetComboBoxSetSelectedPos(comboArray[comboBox], 0);
}

// -----------------------------------------------------------------------------

/// a side Direct Strike leaves out: Turkiye is not in the mode (DIRECT-STRIKE.md, "Sides")
static Bool notInDirectStrike(const PlayerTemplate *fac)
{
	return fac->getSide().compareNoCase("Turkiye") == 0;
}

void PopulatePlayerTemplateComboBox(Int comboBox, GameWindow *comboArray[], GameInfo *myGame, Bool allowObservers, Bool directStrike)
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

		if ( directStrike && notInDirectStrike( fac ) )
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
  if ( moneyAmount.countMoney() == Money::UNLIMITED )
    return TheGameText->fetch( "GUI:StartingMoneyUnlimited" );
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

const char *SuperweaponCaption(Int restriction)
{
  for ( Int i = 0; i < (Int)(sizeof(theSuperweaponChoices)/sizeof(theSuperweaponChoices[0])); i++ )
  {
    if ( theSuperweaponChoices[i] == restriction )
      return theSuperweaponCaptions[i];
  }
  // as the dropdown shows a mode it has no entry for
  return theSuperweaponCaptions[0];
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

void UpdateDayNightCheckBox(GameWindow *checkBox, GameInfo *myGame, Bool hostMayEdit)
{
  checkBox->winEnable( hostMayEdit );

  const Bool dayNight = myGame->getDayNight();
  if ( GadgetCheckBoxIsChecked( checkBox ) != dayNight )
    GadgetCheckBoxSetChecked( checkBox, dayNight );
}

// -----------------------------------------------------------------------------
// Income sharing.  The entries are the IncomeSharing values in order, so an entry's position is
// its value.
static const char * theIncomeSharingCaptions[ INCOME_SHARING_COUNT ] =
{
  "GUI:IncomeSharingOff", "GUI:IncomeSharingTech", "GUI:IncomeSharingAll"
};

const char *IncomeSharingCaption(Int incomeSharing)
{
  if ( incomeSharing < 0 || incomeSharing >= INCOME_SHARING_COUNT )
    return theIncomeSharingCaptions[ INCOME_SHARING_OFF ];
  return theIncomeSharingCaptions[ incomeSharing ];
}

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
// The network rooms' game speed, PLAYER_GAME_SPEED_PERCENTS as 1.0x, 1.25x, 1.5x, 2.0x of retail: the
// skirmish room's list, which the host picks from and a joiner sees greyed, following the host.
void PopulateGameSpeedComboBox(GameWindow *comboBox, GameInfo *myGame, Bool hostMayEdit)
{
  GadgetComboBoxReset(comboBox);

  Color color = comboBox->winGetEnabled() ? comboBox->winGetEnabledTextColor() : comboBox->winGetDisabledTextColor();
  for ( Int i = 0; i < PLAYER_GAME_SPEED_COUNT; i++ )
  {
    const Int speed = PLAYER_GAME_SPEED_PERCENTS[i];
    Int newIndex = GadgetComboBoxAddEntry(comboBox, GameSpeedText( speed ), color);
    GadgetComboBoxSetItemData(comboBox, newIndex, (void *)(intptr_t)speed);
  }

  UpdateGameSpeedComboBox(comboBox, myGame, hostMayEdit);
}

UnicodeString GameSpeedText(Int percent)
{
  UnicodeString text;
  // one decimal unless the second one counts, as the skirmish room writes them
  if ( percent % 10 == 0 )
    text.format( u"%d.%dx", percent / 100, ( percent % 100 ) / 10 );
  else
    text.format( u"%d.%02dx", percent / 100, percent % 100 );
  return text;
}

// The speed a player prefers for a match the server sets up (Quick Match, Play vs AI): the same list,
// always his to pick, percent selected without a selection message.
void PopulatePlayerGameSpeedComboBox(GameWindow *comboBox, Int percent)
{
  GadgetComboBoxReset(comboBox);
  const Color color = comboBox->winGetEnabledTextColor();
  for ( Int i = 0; i < PLAYER_GAME_SPEED_COUNT; i++ )
  {
    const Int speed = PLAYER_GAME_SPEED_PERCENTS[i];
    const Int row = GadgetComboBoxAddEntry(comboBox, GameSpeedText( speed ), color);
    GadgetComboBoxSetItemData(comboBox, row, (void *)(intptr_t)speed);
    if ( speed == percent )
      GadgetComboBoxSetSelectedPos(comboBox, row, TRUE);
  }
}

Int PlayerGameSpeedFromComboBox(GameWindow *comboBox, Int fallback)
{
  Int selIndex = -1;
  if ( comboBox )
    GadgetComboBoxGetSelectedPos(comboBox, &selIndex);
  if ( selIndex < 0 )
    return fallback;
  return ClampPlayerGameSpeedPercent( (Int)(intptr_t)GadgetComboBoxGetItemData(comboBox, selIndex) );
}

void UpdateGameSpeedComboBox(GameWindow *comboBox, GameInfo *myGame, Bool hostMayEdit)
{
  comboBox->winEnable( hostMayEdit );

  Int itemCount = GadgetComboBoxGetLength(comboBox);
  for ( Int index = 0; index < itemCount; index++ )
  {
    if ( (Int)(intptr_t)GadgetComboBoxGetItemData(comboBox, index) == myGame->getGameSpeedPercent() )
    {
      Int selected = -1;
      GadgetComboBoxGetSelectedPos( comboBox, &selected );
      if ( selected != index )
        GadgetComboBoxSetSelectedPos(comboBox, index, TRUE);
      return;
    }
  }
}

Int GameSpeedFromComboBox(GameWindow *comboBox, GameInfo *myGame)
{
  Int selIndex = -1;
  GadgetComboBoxGetSelectedPos(comboBox, &selIndex);
  if ( selIndex < 0 )
    return myGame->getDefaultGameSpeedPercent();
  return ClampPlayerGameSpeedPercent( (Int)(intptr_t)GadgetComboBoxGetItemData(comboBox, selIndex) );
}

// -----------------------------------------------------------------------------
// The lobby tab strip.
static GameWindow *theLobbySettingsPage = NULL;
static GameWindow *theLobbyOtherWindow = NULL;
static GameWindow *theLobbySettingsTab = NULL;
static GameWindow *theLobbyOtherTab = NULL;
static NameKeyType theLobbySettingsTabID = NAMEKEY_INVALID;
static NameKeyType theLobbyOtherTabID = NAMEKEY_INVALID;
static GameWindow *theLobbyChatEntry = NULL;
static GameWindow *theLobbyChatSend = NULL;

static void showLobbySettings( Bool settings )
{
  if ( theLobbySettingsPage == NULL )
    return;

  theLobbySettingsPage->winHide( !settings );
  theLobbyOtherWindow->winHide( settings );
  // the chat's line to write in and its SEND go with it
  if ( theLobbyChatEntry )
    theLobbyChatEntry->winHide( settings );
  if ( theLobbyChatSend )
    theLobbyChatSend->winHide( settings );
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
  name.format( "%s:TextEntryChat", layoutFilename );
  theLobbyChatEntry = TheWindowManager->winGetWindowFromId( parent, TheNameKeyGenerator->nameToKey( name ) );
  name.format( "%s:ButtonEmote", layoutFilename );
  theLobbyChatSend = TheWindowManager->winGetWindowFromId( parent, TheNameKeyGenerator->nameToKey( name ) );

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
  theLobbyChatEntry = NULL;
  theLobbyChatSend = NULL;
}

// -----------------------------------------------------------------------------

// The personality box stands under a computer's level and only a computer's seat shows it (the room
// look places both); it goes with the seat's army box, which a map list over the seats hides.
// Both are kept by the layout's row, and the row is found from the personality box itself: a
// skirmish room hands its boxes over by seat, and after the room's seat visuals shut down
// theSeatRow says seat 20 is on row 20, which has no army box (report 5025).
static GameWindow *thePersonaRowBox[MAX_SLOTS];
static GameWindow *thePersonaSeatArmy[MAX_SLOTS];
// the arrays above and the seat windows below are kept by the layout's row; this says which row a
// seat is on (SetLobbySeatRows)
static Int theSeatRow[MAX_SLOTS];

static void resetSeatRows(void)
{
	for (Int i = 0; i < MAX_SLOTS; ++i)
		theSeatRow[i] = i;
}

void SetLobbySeatRows(const Int seatRow[])
{
	for (Int i = 0; i < MAX_SLOTS; ++i)
		theSeatRow[i] = seatRow[i];
}

static GameWindow *findSeatWindow(GameWindow *parent, const char *layoutFilename, const char *name, Int seat)
{
	AsciiString full;
	full.format("%s:%s%d", layoutFilename, name, seat);
	return TheWindowManager->winGetWindowFromId(parent, NAMEKEY(full));
}

static void measurePersonaSeat(GameWindow *parent, const char *layoutFilename, GameWindow *persona, Int row)
{
	thePersonaRowBox[row] = persona;
	thePersonaSeatArmy[row] = findSeatWindow(parent, layoutFilename, "ComboBoxPlayerTemplate", row);
}

/// the army box on the personality box's own row, NULL when the layout has none
static GameWindow *personaArmyBox(GameWindow *persona)
{
	for (Int row = 0; row < MAX_SLOTS; ++row)
		if (thePersonaRowBox[row] == persona)
			return thePersonaSeatArmy[row];
	return NULL;
}

static UnicodeString personaTooltip(Int persona)
{
	AsciiString label;
	label.format("GUI:AIPersonaTooltip%s", aiPersonalityName((AIPersonality)persona));
	return TheGameText->fetch(label);
}

// the box's own text, its arrow and its edit field all say what the picked personality does
static void personaComboTooltip(GameWindow *window, WinInstanceData *, UnsignedInt)
{
	GameWindow *combo = BitTest(window->winGetStyle(), GWS_COMBO_BOX) ? window : window->winGetParent();
	Int pos = 0;
	GadgetComboBoxGetSelectedPos(combo, &pos);
	TheMouse->setCursorTooltip(personaTooltip((Int)(intptr_t)GadgetComboBoxGetItemData(combo, pos)));
}

// an open list says what the line under the pointer does
static void personaListTooltip(GameWindow *listBox, WinInstanceData *, UnsignedInt mouse)
{
	Int row, col;
	GadgetListBoxGetEntryBasedOnXY(listBox, LOLONGTOSHORT(mouse), HILONGTOSHORT(mouse), row, col);
	if (row == -1 || col == -1)
		return;
	TheMouse->setCursorTooltip(personaTooltip((Int)(intptr_t)GadgetListBoxGetItemData(listBox, row, col)), 0);
}

// the line that shows a seat's personality; one the list does not offer shows as its nearest
static Int personaComboPos(GameWindow *combo, Int persona)
{
	const Int shown = aiPersonalityOffered((AIPersonality)persona);
	for (Int pos = GadgetComboBoxGetLength(combo) - 1; pos > 0; --pos)
		if ((Int)(intptr_t)GadgetComboBoxGetItemData(combo, pos) == shown)
			return pos;
	return 0;	// Normal heads the list
}

void InitAIPersonaComboBoxes(GameWindow *parent, const char *layoutFilename,
														 GameWindow *comboPersona[], NameKeyType comboPersonaID[])
{
	resetSeatRows();
	for (Int i = 0; i < MAX_SLOTS; ++i)
		thePersonaRowBox[i] = thePersonaSeatArmy[i] = NULL;	// the last screen's windows are gone
	for (Int i = 0; i < MAX_SLOTS; ++i)
	{
		AsciiString name;
		name.format("%s:ComboBoxAIPersona%d", layoutFilename, i);
		comboPersonaID[i] = NAMEKEY(name);
		comboPersona[i] = TheWindowManager->winGetWindowFromId(parent, comboPersonaID[i]);
		if (!comboPersona[i])
			continue;	// a layout from before the column
		measurePersonaSeat(parent, layoutFilename, comboPersona[i], i);

		// make room for the whole list before the first add, as PopulatePlayerSlotComboBox does
		GameWindow *listBox = GadgetComboBoxGetListBox(comboPersona[i]);
		ListboxData *listData = (ListboxData *)listBox->winGetUserData();
		if (listData->listLength < AIPERSONA_COUNT)
			GadgetListBoxSetListLength(listBox, AIPERSONA_COUNT);
		// sixteen lines from a lower seat ran off the bottom of the screen; eight and a scroll bar
		GadgetComboBoxSetMaxDisplay(comboPersona[i], min((Int)AIPERSONA_COUNT, 8));
		comboPersona[i]->winSetTooltipFunc(personaComboTooltip);
		GadgetComboBoxGetEditBox(comboPersona[i])->winSetTooltipFunc(personaComboTooltip);
		GadgetComboBoxGetDropDownButton(comboPersona[i])->winSetTooltipFunc(personaComboTooltip);
		listBox->winSetTooltipFunc(personaListTooltip);

		GadgetComboBoxReset(comboPersona[i]);
		const Color color = comboPersona[i]->winGetEnabledTextColor();
		for (Int p = 0; p < AIPERSONA_COUNT; ++p)
		{
			if (aiPersonalityOffered((AIPersonality)p) != p)
				continue;
			AsciiString label;
			label.format("GUI:AIPersona%s", aiPersonalityName((AIPersonality)p));
			Int index = GadgetComboBoxAddEntry(comboPersona[i], TheGameText->fetch(label), color);
			GadgetComboBoxSetItemData(comboPersona[i], index, (void *)(intptr_t)p);
		}
		GadgetComboBoxSetSelectedPos(comboPersona[i], 0, TRUE);	// Normal
	}
}

void UpdateAIPersonaComboBoxes(GameInfo *myGame, GameWindow *comboPersona[])
{
	if (!AreSlotListUpdatesEnabled())
		return;
	for (Int i = 0; i < MAX_SLOTS; ++i)
	{
		if (!comboPersona[i])
			continue;
		const GameSlot *slot = myGame->getConstSlot(i);
		// a takeover seat has no AI behind it, so nothing for a personality to steer
		const Bool computer = slot->isAI() && !slot->isTakeover();
		// the personality goes with the seat's army box, which ShowUnderlyingGUIElements hides while a
		// map list lies over the seats, so a network update then cannot bring it back through the list
		GameWindow *army = personaArmyBox(comboPersona[i]);
		comboPersona[i]->winHide(!computer || (army && army->winIsHidden()));
		// the room lays the player box out over the personality itself (the room look); a Direct
		// Strike lobby lies over both
		if (!computer)
			continue;
		comboPersona[i]->winEnable(myGame->amIHost());
		Int pos = -1;
		GadgetComboBoxGetSelectedPos(comboPersona[i], &pos);
		const Int want = personaComboPos(comboPersona[i], slot->getAIPersonality());
		if (pos != want)
			GadgetComboBoxSetSelectedPos(comboPersona[i], want, TRUE);
	}
}

Bool HandleAIPersonaSelection(GameInfo *myGame, GameWindow *comboPersona[], NameKeyType comboPersonaID[],
															NameKeyType controlID)
{
	for (Int i = 0; i < MAX_SLOTS; ++i)
	{
		if (!comboPersona[i] || controlID != comboPersonaID[i])
			continue;
		Int pos = -1;
		GadgetComboBoxGetSelectedPos(comboPersona[i], &pos);
		GameSlot *slot = myGame->getSlot(i);
		// Random goes into the slot as Random; the AI draws the real one from the logic stream
		const Int persona = (Int)(intptr_t)GadgetComboBoxGetItemData(comboPersona[i], pos);
		if (!myGame->amIHost() || !slot->isAI() || persona == slot->getAIPersonality())
			return FALSE;
		const Int was = slot->getAIPersonality();
		slot->setAIPersonality(persona);
		// an options string past the LAN budget comes back empty and is never sent: the host would
		// start with a persona nobody else has, so the pick does not stand
		if (GameInfoToAsciiString(myGame).isEmpty())
		{
			slot->setAIPersonality(was);
			GadgetComboBoxSetSelectedPos(comboPersona[i], personaComboPos(comboPersona[i], was), TRUE);
			return FALSE;
		}
		return TRUE;
	}
	return FALSE;
}

// -----------------------------------------------------------------------------

// the box's lines run 100, 90 ... 10
static Int handicapComboPos(Int handicap)
{
	return (HANDICAP_FULL - handicap) / HANDICAP_STEP;
}

void InitHandicapComboBoxes(GameWindow *parent, const char *layoutFilename,
														GameWindow *comboHandicap[], NameKeyType comboHandicapID[])
{
	const Int lines = HANDICAP_FULL / HANDICAP_STEP;
	for (Int i = 0; i < MAX_SLOTS; ++i)
	{
		AsciiString name;
		name.format("%s:ComboBoxHandicap%d", layoutFilename, i);
		comboHandicapID[i] = NAMEKEY(name);
		comboHandicap[i] = TheWindowManager->winGetWindowFromId(parent, comboHandicapID[i]);
		if (!comboHandicap[i])
			continue;	// a layout from before the column

		GameWindow *listBox = GadgetComboBoxGetListBox(comboHandicap[i]);
		ListboxData *listData = (ListboxData *)listBox->winGetUserData();
		if (listData->listLength < lines)
			GadgetListBoxSetListLength(listBox, lines);
		GadgetComboBoxSetMaxDisplay(comboHandicap[i], min(lines, 8));	// as the personality box, for the lower seats

		GadgetComboBoxReset(comboHandicap[i]);
		const Color color = comboHandicap[i]->winGetEnabledTextColor();
		for (Int handicap = HANDICAP_FULL; handicap >= HANDICAP_STEP; handicap -= HANDICAP_STEP)
		{
			UnicodeString label;
			label.format(u"%d%%", handicap);
			Int index = GadgetComboBoxAddEntry(comboHandicap[i], label, color);
			GadgetComboBoxSetItemData(comboHandicap[i], index, (void *)(intptr_t)handicap);
		}
		GadgetComboBoxSetSelectedPos(comboHandicap[i], handicapComboPos(HANDICAP_FULL), TRUE);
		comboHandicap[i]->winEnable(FALSE);	// until UpdateHandicapComboBoxes knows whose seat it is
	}
}

void UpdateHandicapComboBoxes(GameInfo *myGame, GameWindow *comboHandicap[], GameWindow *comboTeam[])
{
	if (!AreSlotListUpdatesEnabled())
		return;
	for (Int i = 0; i < MAX_SLOTS; ++i)
	{
		if (!comboHandicap[i])
			continue;
		comboHandicap[i]->winEnable(comboTeam[i] && BitTest(comboTeam[i]->winGetStatus(), WIN_STATUS_ENABLED));
		const Int wanted = handicapComboPos(myGame->getConstSlot(i)->getHandicap());
		Int pos = -1;
		GadgetComboBoxGetSelectedPos(comboHandicap[i], &pos);
		if (pos != wanted)
			GadgetComboBoxSetSelectedPos(comboHandicap[i], wanted, TRUE);
	}
}

Int HandleHandicapSelection(GameInfo *myGame, GameWindow *comboHandicap[], NameKeyType comboHandicapID[],
														NameKeyType controlID)
{
	// setting a box's selection sends this too, and filling the boxes at init is no pick
	if (!AreSlotListUpdatesEnabled())
		return 0;
	for (Int i = 0; i < MAX_SLOTS; ++i)
	{
		if (!comboHandicap[i] || controlID != comboHandicapID[i])
			continue;
		Int pos = -1;
		GadgetComboBoxGetSelectedPos(comboHandicap[i], &pos);
		GameSlot *slot = myGame->getSlot(i);
		const Int was = slot->getHandicap();
		const Int handicap = (Int)(intptr_t)GadgetComboBoxGetItemData(comboHandicap[i], pos);
		if (handicap == was)
			return 0;
		// A guest's slot takes it from the host's list. Written here, a start already on its way would
		// have this seat play a handicap nobody else ever saw.
		if (!myGame->amIHost())
			return handicap;
		slot->setHandicap(handicap);
		// an options string past the LAN budget comes back empty and is never sent
		if (GameInfoToAsciiString(myGame).isEmpty())
		{
			slot->setHandicap(was);
			GadgetComboBoxSetSelectedPos(comboHandicap[i], handicapComboPos(was), TRUE);
			return 0;
		}
		return handicap;
	}
	return 0;
}

// -----------------------------------------------------------------------------

static GameWindow *theSeatCard[MAX_SLOTS];
static GameWindow *theSeatBar[MAX_SLOTS];
static GameWindow *theSeatSwatch[MAX_SLOTS];
static GameWindow *theSeatTeam[MAX_SLOTS];
static Bool theSeatsShowReady;

// The eight player colours (gold, red, blue, green, orange, sky blue, purple, pink) take every bright
// hue, so the teams go by lightness as much as hue: white, black, brown and a dark teal.  Even with
// no colour vision at all white, teal, brown and black come out as four different greys, and the
// spot's number stays besides.
static const Color theTeamColors[] =
{
	GameMakeColor(245, 245, 245, 255),
	GameMakeColor(16, 16, 20, 255),
	GameMakeColor(150, 88, 30, 255),
	GameMakeColor(0, 128, 128, 255),
};
static const Color theSeatEmptyColor = GameMakeColor(48, 54, 76, 255);
static const Color theSeatUnpickedColor = GameMakeColor(128, 128, 128, 255);
// a card is UIStyle's panel in its frame, the host's framed in gold; ready is a state of its own
static const Color theCardReadyColor = GameMakeColor(22, 74, 40, 210);

Color LobbyTeamColor(Int team)
{
	return team >= 0 && team < (Int)ARRAY_SIZE(theTeamColors) ? theTeamColors[team] : theSeatEmptyColor;
}

Color LobbySeatColor(const GameSlot *slot)
{
	if (!slot->isOccupied())
		return theSeatEmptyColor;
	if (slot->getColor() >= 0 && slot->getColor() < TheMultiplayerSettings->getNumColors())
		return TheMultiplayerSettings->getColor(slot->getColor())->getColor();
	return theSeatUnpickedColor;
}

// WCAG's relative luminance of one 0-255 channel
static Real linearChannel(Int channel)
{
	const Real c = channel / 255.0f;
	return c <= 0.03928f ? c / 12.92f : (Real)pow((c + 0.055f) / 1.055f, 2.4f);
}

Color LobbyTextColorOn(Color fill)
{
	UnsignedByte r, g, b, a;
	GameGetColorComponents(fill, &r, &g, &b, &a);
	const Real luminance = 0.2126f * linearChannel(r) + 0.7152f * linearChannel(g) + 0.0722f * linearChannel(b);
	// black on it beats white on it in contrast exactly when the luminance is over about 0.179
	const Real blackContrast = (luminance + 0.05f) / 0.05f;
	const Real whiteContrast = 1.05f / (luminance + 0.05f);
	return blackContrast > whiteContrast ? GameMakeColor(0, 0, 0, 255) : GameMakeColor(255, 255, 255, 255);
}

void LobbyStartSpotFill(Int x, Int y, Int width, Int height, Color fill)
{
	// both halves are cut from EA's PlayerStart cell (Tools/lobby_spot.py), so they lie on it pixel for
	// pixel: the white middle multiplied by the colour, then the ring over it untinted, its inner edge
	// fading into the colour as it faded into the black
	TheDisplay->drawImage(TheMappedImageCollection->findImageByName("ReforgedLobbySpot"), x, y, x + width, y + height, fill);
	TheDisplay->drawImage(TheMappedImageCollection->findImageByName("ReforgedLobbySpotRing"), x, y, x + width, y + height);
}

// A colour swatch drawn as one of the rules page's check boxes: their own empty box, and the colour
// inside its hole with the same room on all four sides.
static void drawSwatch(GameWindow *window, WinInstanceData *instData)
{
	Int x, y, width, height;
	window->winGetScreenPosition(&x, &y);
	window->winGetSize(&width, &height);
	LobbySwatchFill(x, y, width, height, window->winGetEnabledColor(0));
}

void LobbySwatchFill(Int x, Int y, Int width, Int height, Color fill)
{
	TheDisplay->drawImage(TheMappedImageCollection->findImageByName("Active-Unchecked"), x, y, x + width, y + height);
	// the art is a 22 pixel cell whose black hole runs from 3 to 17 on both axes, up and left of the
	// middle because the drop shadow takes the last two; each edge is rounded on its own so the hole
	// is found again at whatever size the room was scaled to
	const Int CELL = 22, HOLE_START = 3, HOLE_END = 17;
	const Int holeLeft = x + (HOLE_START * width + CELL / 2) / CELL;
	const Int holeRight = x + (HOLE_END * width + CELL / 2) / CELL;
	const Int holeTop = y + (HOLE_START * height + CELL / 2) / CELL;
	const Int holeBottom = y + (HOLE_END * height + CELL / 2) / CELL;
	const Int gap = max(1, min(holeRight - holeLeft, holeBottom - holeTop) / 8);
	TheDisplay->drawFillRect(holeLeft + gap, holeTop + gap, holeRight - holeLeft - 2 * gap,
		holeBottom - holeTop - 2 * gap, fill);
}

void DrawMapListTab(GameWindow *window, WinInstanceData *instData)
{
	// the menu's tab, a list tab's count after its name (MapPicker.cpp)
	MapPickerDrawTab(window, instData);
}

// The Random tab ------------------------------------------------------------------------------
// Tools/mapselect_layout.py clones its windows into all three pickers under the same names.  The
// choice outlives the picker in RandomMap.ini; the windows go with it.  Nothing is generated until
// Accept: the preview shows the shipped map a template row names.

void positionStartSpots(AsciiString mapName, GameWindow *buttonMapStartPositions[], GameWindow *mapWindow);

static const char *const theRandomMapPrefsFile = "RandomMap.ini";
static const char *const theAodRow = "style:aod";		// the type list's Art of Defense row: a style, not a shipped map

static const char *theRandomLayout = NULL;
static GameWindow *theRandomParent = NULL;
static GameWindow *theRandomTab = NULL;					// NULL while no picker is open
static GameWindow *theListTabs[2] = { NULL, NULL };	// Official, Unofficial
static GameWindow *theMapListWindow = NULL;
static GameWindow *theTemplateList = NULL;
static GameWindow *theSizeButtons[RANDOM_MAP_SIZE_COUNT];
static std::vector<GameWindow *> theRandomPanel;		// everything the tab shows in the list's place
static GameWindow *theRandomPreview = NULL;
static GameWindow **theRandomStartSpots = NULL;
static const GameInfo *theRandomRoom = NULL;

static Bool theRandomTabOpen = FALSE;
static AsciiString theRandomTemplate;					// a MapCache key; empty lets the generator pick
static Int theRandomSize = RANDOM_MAP_SIZE_NORMAL;
static std::vector<AsciiString> theTemplates;			// the template list's rows; item data is the index
static UnsignedInt theRolls = 0;

/// Who a generated map is for: every occupied seat that plays, human or AI, two to eight.
static Int roomRandomMapPlayers(const GameInfo *game)
{
	const Int players = game->getNumNonObserverPlayers();
	return max((Int)RandomMapGenerator::MIN_PLAYERS, min((Int)RandomMapGenerator::MAX_PLAYERS, players));
}

static GameWindow *randomTabWindow(const char *name, Int index = -1)
{
	AsciiString fullName;
	if (index < 0)
		fullName.format("%s:%s", theRandomLayout, name);
	else
		fullName.format("%s:%s%d", theRandomLayout, name, index);
	return TheWindowManager->winGetWindowFromId(theRandomParent, NAMEKEY(fullName));
}

static void saveRandomTab(void)
{
	UserPreferences pref;
	pref.load(theRandomMapPrefsFile);
	pref.setBool("Open", theRandomTabOpen);
	pref.setInt("Size", theRandomSize);
	pref.setAsciiString("Template", theRandomTemplate);
	pref.write();
}

/// The template row's own shipped map, the Official tab's picture, tagged STYLE by the grid; its start
/// spots are not the new map's, so none shows.  Surprise me shows the unknown map.
static void showRandomTemplate(void)
{
	positionStartSpots(theRandomTemplate, theRandomStartSpots, theRandomPreview);
	for (Int i = 0; theRandomStartSpots && i < MAX_SLOTS; ++i)
		if (theRandomStartSpots[i])
			theRandomStartSpots[i]->winHide(TRUE);
}

static void showRandomTab(Bool open)
{
	theRandomTabOpen = open;
	theMapListWindow->winHide(open);
	for (size_t i = 0; i < theRandomPanel.size(); ++i)
		theRandomPanel[i]->winHide(!open);
	if (open)
		showRandomTemplate();
}

/** Every official multiplayer map in the Official list's order, fewest players first and then by
	shown name (populateMapListboxNoReset), under one row that leaves the kind of map to the seed.
	Selecting the remembered row fires GLM_SELECTED, which changes nothing. */
static void fillTemplateList(void)
{
	struct Row { UnicodeString name; AsciiString map; Int players; };
	std::vector<Row> rows;
	AsciiString token;
	for (MapCache::const_iterator it = TheMapCache->begin(); it != TheMapCache->end(); ++it)
	{
		// only a map the generated name can carry back, "maps\<name>\<name>.map"
		if (it->second.m_isOfficial && it->second.m_isMultiplayer && !isGeneratedMapPath(it->first)
			&& rmgTemplateToken(it->first, token))
		{
			Row row = { it->second.m_displayName, it->first, (Int)it->second.m_numPlayers };
			rows.push_back(row);
		}
	}
	std::sort(rows.begin(), rows.end(), [](const Row &a, const Row &b)
		{ return a.players != b.players ? a.players < b.players : a.name.compareNoCase(b.name) < 0; });

	theTemplates.clear();
	GadgetListBoxReset(theTemplateList);
	const Int column = GadgetListBoxGetNumColumns(theTemplateList) - 1;
	const Color color = GameMakeColor(255, 255, 255, 255);
	Int selected = GadgetListBoxAddEntryText(theTemplateList, TheGameText->fetch("GUI:RandomMapAnyTemplate"), color, -1, column);
	GadgetListBoxSetItemData(theTemplateList, (void *)(intptr_t)0, selected);
	theTemplates.push_back(AsciiString::TheEmptyString);
	// the Art of Defense style, a row of its own: no shipped map behind it, the generator's aod ground
	Bool found = FALSE;
	{
		const Int at = GadgetListBoxAddEntryText(theTemplateList, TheGameText->fetch("GUI:RandomMapAod"), color, -1, column);
		GadgetListBoxSetItemData(theTemplateList, (void *)(intptr_t)theTemplates.size(), at);
		theTemplates.push_back(AsciiString(theAodRow));
		if (theRandomTemplate == theAodRow)
		{
			selected = at;
			found = TRUE;
		}
	}
	for (size_t i = 0; i < rows.size(); ++i)
	{
		Int at = GadgetListBoxAddEntryText(theTemplateList, rows[i].name, color, -1, column);
		GadgetListBoxSetItemData(theTemplateList, (void *)(intptr_t)theTemplates.size(), at);
		theTemplates.push_back(rows[i].map);
		if (rows[i].map == theRandomTemplate)
		{
			selected = at;
			found = TRUE;
		}
	}
	if (!found)
		theRandomTemplate.clear();	// a map the cache no longer has, or none
	GadgetListBoxSetSelected(theTemplateList, selected);
}

void RandomMapTabInit(const char *layoutFilename, GameWindow *parent, GameWindow *preview,
										GameWindow *startSpots[], const GameInfo *game, Bool allowed)
{
	theRandomLayout = layoutFilename;
	theRandomParent = parent;
	theRandomPreview = preview;
	theRandomStartSpots = startSpots;
	theRandomRoom = game;

	theRandomTab = randomTabWindow("RadioButtonRandomMaps");
	theListTabs[0] = randomTabWindow("RadioButtonSystemMaps");
	theListTabs[1] = randomTabWindow("RadioButtonUserMaps");
	theMapListWindow = randomTabWindow("ListboxMap");
	theTemplateList = randomTabWindow("ListboxRandomTemplate");

	theRandomPanel.clear();
	theRandomPanel.push_back(randomTabWindow("StaticTextRandomTemplate"));
	theRandomPanel.push_back(theTemplateList);
	theRandomPanel.push_back(randomTabWindow("StaticTextRandomSize"));
	for (Int size = 0; size < RANDOM_MAP_SIZE_COUNT; ++size)
	{
		theSizeButtons[size] = randomTabWindow("RadioButtonRandomSize", size);
		theRandomPanel.push_back(theSizeButtons[size]);
	}

	// a layout without the tab (EA's own, or a mod's loose copy beating ours): the picker works as
	// retail's did, Official and Unofficial only
	Bool complete = theRandomTab && theListTabs[0] && theListTabs[1] && theMapListWindow;
	for (size_t i = 0; i < theRandomPanel.size(); ++i)
		complete = complete && theRandomPanel[i] != NULL;
	if (!complete)
	{
		DEBUG_LOG(("RandomMapTabInit: %s has no Random tab; the picker offers shipped maps only\n", layoutFilename));
		if (theRandomTab)
			theRandomTab->winHide(TRUE);
		RandomMapTabShutdown();
		return;
	}

	theRandomTab->winSetDrawFunc(DrawMapListTab);
	theRandomTab->winEnable(allowed);
	for (Int size = 0; size < RANDOM_MAP_SIZE_COUNT; ++size)
		theSizeButtons[size]->winSetDrawFunc(DrawMapListTab);

	UserPreferences pref;
	pref.load(theRandomMapPrefsFile);
	theRandomSize = pref.getInt("Size", RANDOM_MAP_SIZE_NORMAL);
	if (theRandomSize < 0 || theRandomSize >= RANDOM_MAP_SIZE_COUNT)
		theRandomSize = RANDOM_MAP_SIZE_NORMAL;
	theRandomTemplate = pref.getAsciiString("Template", AsciiString::TheEmptyString);

	fillTemplateList();
	GadgetRadioSetSelection(theSizeButtons[theRandomSize], FALSE);
	// the type list as the map list's grid of pictures, MAP SIZE and MAP TYPE as heads
	MapPickerAttachTypes(theTemplateList, randomTabWindow("StaticTextRandomSize"), randomTabWindow("StaticTextRandomTemplate"));

	// more players than a shipped map holds: the lists keep the maps with a start spot for each of
	// them (the picker fills them so, and the grid says so under the tabs), and a room already on a
	// generated map opens on this tab
	const Bool crowded = allowed && game->getNumNonObserverPlayers() > SHIPPED_MAP_MAX_PLAYERS;

	const Bool open = allowed && ((crowded && isGeneratedMapPath(game->getMap())) || pref.getBool("Open", FALSE));
	if (open)
		GadgetRadioSetSelection(theRandomTab, FALSE);
	showRandomTab(open);
}

Bool RandomMapTabHandle(UnsignedInt msg, UnsignedIntPtr mData1, UnsignedIntPtr mData2)
{
	if (theRandomTab == NULL)
		return FALSE;
	GameWindow *control = (GameWindow *)mData1;

	if ((msg == GLM_SELECTED || msg == GLM_DOUBLE_CLICKED) && control == theTemplateList)
	{
		const Int row = (Int)mData2;
		if (row < 0)
			return TRUE;
		// a row picked again (row 0, Surprise me, is the pick on a first visit) still answers.  Only the
		// open tab shows its type: the list's own fill at the picker's init selects a row too, and with
		// the map list open that put Surprise me's unknown map over the picked map's preview
		const AsciiString chosen = RandomMapTabTemplateAt(row);
		if (chosen != theRandomTemplate)
		{
			theRandomTemplate = chosen;
			saveRandomTab();
		}
		if (theRandomTabOpen)
			showRandomTemplate();
		if (msg == GLM_DOUBLE_CLICKED)
		{
			GameWindow *ok = randomTabWindow("ButtonOK");
			TheWindowManager->winSendSystemMsg(theRandomParent, GBM_SELECTED, (WindowMsgData)ok, ok->winGetWindowId());
		}
		return TRUE;
	}
	if (msg != GBM_SELECTED)
		return FALSE;

	if (control == theRandomTab)
	{
		showRandomTab(TRUE);
		saveRandomTab();
		return TRUE;
	}
	if (control == theListTabs[0] || control == theListTabs[1])
	{
		showRandomTab(FALSE);
		saveRandomTab();
		return FALSE;	// the picker fills its list
	}
	for (Int size = 0; size < RANDOM_MAP_SIZE_COUNT; ++size)
	{
		if (control == theSizeButtons[size])
		{
			theRandomSize = size;
			saveRandomTab();
			return TRUE;
		}
	}
	return FALSE;
}

/** A new map from the choice, for everybody seated now.  The seed comes off the clock: this is the
	client choosing a map, not the simulation, and the joining machines rebuild it from the name the
	host sends. */
static AsciiString stageRoomRandomMap(const GameInfo *game, Int size, const AsciiString &templatePath)
{
	const Int players = roomRandomMapPlayers(game);
	RandomMapSettings settings;
	settings.m_seed = (Int)((Clock_Milliseconds() + 7919u * ++theRolls) & 0x7fffffff);
	settings.m_numPlayers = players;
	if (templatePath == theAodRow)
	{
		// its own ground, the size the style's, for the humans in the room: no computer seat plays it
		settings.m_style = RandomMapGenerator::styleNamed("aod");
		Int humans = 0;
		for (Int i = 0; i < MAX_SLOTS; ++i)
			if (game->getConstSlot(i) != NULL && game->getConstSlot(i)->isHuman() && game->getConstSlot(i)->getPlayerTemplate() != PLAYERTEMPLATE_OBSERVER)
				++humans;
		settings.m_numPlayers = max(1, humans);
	}
	else
	{
		settings.m_playableCells = RandomMapGenerator::cellsFor((RandomMapSize)size, players, templatePath);
		settings.m_template = templatePath;
	}
	AsciiString map;
	stageRandomMap(settings, map);
	map.toLower();
	TheMapCache->updateCache();
	return map;
}

AsciiString RandomMapTabMap(void)
{
	if (!theRandomTabOpen)
		return AsciiString::TheEmptyString;
	return stageRoomRandomMap(theRandomRoom, theRandomSize, theRandomTemplate);
}

// what the grid (MapPicker.cpp) reads of the tab
Bool RandomMapTabIsOpen(void)
{
	return theRandomTab != NULL && theRandomTabOpen;
}

AsciiString RandomMapTabTemplateAt(Int row)
{
	if (theRandomTab == NULL || theTemplateList == NULL || row < 0 || row >= GadgetListBoxGetNumEntries(theTemplateList))
		return AsciiString::TheEmptyString;
	const Int index = (Int)(intptr_t)GadgetListBoxGetItemData(theTemplateList, row);
	return index >= 0 && index < (Int)theTemplates.size() ? theTemplates[index] : AsciiString::TheEmptyString;
}

AsciiString RandomMapTabTemplate(void)
{
	return theRandomTemplate;
}

Int RandomMapTabSize(void)
{
	return theRandomSize;
}

Int RandomMapTabPlayers(void)
{
	return theRandomRoom ? roomRandomMapPlayers(theRandomRoom) : (Int)RandomMapGenerator::MIN_PLAYERS;
}

GameWindow *RandomMapTabButton(Int which)
{
	if (theRandomTab == NULL || which < 0 || which > 2)
		return NULL;
	return which == 2 ? theRandomTab : theListTabs[which];
}

Bool GenerateMapForCrowdedRoom(GameInfo *game, Bool whenTooSmall)
{
	if (isGeneratedMapPath(game->getMap()))
		return FALSE;
	const Int players = game->getNumNonObserverPlayers();
	if (players <= SHIPPED_MAP_MAX_PLAYERS && !whenTooSmall)
		return FALSE;
	// a map with a start spot for every seat stays, a custom one past eight included
	const MapMetaData *current = TheMapCache->findMap(game->getMap());
	if (current != NULL && (Int)current->m_numPlayers >= players)
		return FALSE;
	if (current == NULL && players <= SHIPPED_MAP_MAX_PLAYERS)
		return FALSE;
	// a room whose map could not be made is not asked again every frame until its map changes
	static AsciiString theFailedMap;
	if (game->getMap().compareNoCase(theFailedMap) == 0)
		return FALSE;

	UserPreferences pref;
	pref.load(theRandomMapPrefsFile);
	Int size = pref.getInt("Size", RANDOM_MAP_SIZE_NORMAL);
	if (size < 0 || size >= RANDOM_MAP_SIZE_COUNT)
		size = RANDOM_MAP_SIZE_NORMAL;
	AsciiString map = stageRoomRandomMap(game, size, pref.getAsciiString("Template", AsciiString::TheEmptyString));
	const MapMetaData *md = TheMapCache->findMap(map);
	// a template that builds nothing for this many leaves the plain generator, which always builds
	if (md == NULL)
	{
		map = stageRoomRandomMap(game, size, AsciiString::TheEmptyString);
		md = TheMapCache->findMap(map);
	}
	if (md == NULL)
	{
		theFailedMap = game->getMap();
		return FALSE;
	}
	DEBUG_LOG(("random map: the room seats %d, more than its map holds; map now '%s'\n",
		game->getNumNonObserverPlayers(), map.str()));
	game->setMap(map);
	game->setMapCRC(md->m_CRC);
	game->setMapSize(md->m_filesize);
	game->getSlot(0)->setMapAvailability(TRUE);
	game->resetStartSpots();
	return TRUE;
}

void RandomMapTabShutdown(void)
{
	theRandomTab = NULL;
	theRandomParent = NULL;
	theRandomRoom = NULL;
	theRandomPanel.clear();
	theRandomTabOpen = FALSE;
}

Bool FitRandomMapToRoom(GameInfo *game)
{
	// Art of Defense is for people alone: a computer seat in the room opens again, and the map is the humans'
	Bool opened = FALSE;
	const Bool aod = IsAodMap(game->getMap());
	if (aod)
	{
		for (Int i = 0; i < MAX_SLOTS; ++i)
		{
			GameSlot *slot = game->getSlot(i);
			if (slot != NULL && slot->isAI())
			{
				slot->setState(SLOT_OPEN);
				opened = TRUE;
			}
		}
	}

	// the rooms ask every frame; a map and a count already answered cost a compare
	static AsciiString theFittedMap;
	static Int theFittedPlayers = 0;
	const Int players = aod ? max(1, game->getNumNonObserverPlayers()) : roomRandomMapPlayers(game);
	if (players == theFittedPlayers && game->getMap().compareNoCase(theFittedMap) == 0)
		return opened;
	theFittedMap = game->getMap();
	theFittedPlayers = players;

	AsciiString map;
	if (!restageRandomMap(game->getMap(), players, map) || map.compareNoCase(game->getMap()) == 0)
		return opened;
	map.toLower();
	TheMapCache->updateCache();
	const MapMetaData *md = TheMapCache->findMap(map);
	// a template that built nothing leaves no map under the new name; the room keeps the one it has
	if (md == NULL)
		return opened;
	game->setMap(map);
	game->setMapCRC(md->m_CRC);
	game->setMapSize(md->m_filesize);
	game->getSlot(0)->setMapAvailability(TRUE);
	game->resetStartSpots();
	theFittedMap = map;
	DEBUG_LOG(("random map: the room seats %d, map now '%s'\n", players, map.str()));
	return TRUE;
}

// Direct Strike ------------------------------------------------------------------------------

enum { DIRECT_STRIKE_SEATS = DSLOBBY_SEATS, DIRECT_STRIKE_TEAM_SEATS = DSLOBBY_ROWS };

static Int directStrikeTeamOfSeat(Int seat) { return DirectStrikeLobby_team(seat); }

void DirectStrikeSeats(const GameInfo *game, Int seatOf[], Int slotAt[])
{
	Int start[DSLOBBY_SEATS];
	for (Int i = 0; i < DSLOBBY_SEATS; ++i)
	{
		const GameSlot *slot = game->getConstSlot(i);
		start[i] = slot != NULL && slot->isOccupied() ? slot->getStartPos() : -1;
	}
	DirectStrikeLobby_seats(start, seatOf, slotAt);
}

Bool DirectStrikeCanTakeSeat(const GameInfo *game, Int slot, Int seat)
{
	if (seat < 0 || seat >= DSLOBBY_SEATS || slot < 0 || slot >= DSLOBBY_SEATS)
		return FALSE;
	const GameSlot *mover = game->getConstSlot(slot);
	if (mover == NULL || !mover->isOccupied() || mover->getPlayerTemplate() == PLAYERTEMPLATE_OBSERVER)
		return FALSE;
	Int seatOf[DSLOBBY_SEATS], slotAt[DSLOBBY_SEATS];
	DirectStrikeSeats(game, seatOf, slotAt);
	const GameSlot *there = slotAt[seat] != slot ? game->getConstSlot(slotAt[seat]) : NULL;
	return there != NULL && (there->getState() == SLOT_OPEN || there->isAI());
}

Bool DirectStrikeTakeSeat(GameInfo *game, Int slot, Int seat)
{
	if (!DirectStrikeCanTakeSeat(game, slot, seat))
		return FALSE;
	Int seatOf[DSLOBBY_SEATS], slotAt[DSLOBBY_SEATS];
	DirectStrikeSeats(game, seatOf, slotAt);
	GameSlot *there = game->getSlot(slotAt[seat]);
	// a computer takes the seat the player left; an open slot has no start and keeps showing where it is free
	if (there->isAI())
	{
		there->setStartPos(seatOf[slot]);
		there->setTeamNumber(directStrikeTeamOfSeat(seatOf[slot]));
	}
	GameSlot *mover = game->getSlot(slot);
	mover->setStartPos(seat);
	mover->setTeamNumber(directStrikeTeamOfSeat(seat));
	DEBUG_LOG(("Direct Strike: slot %d takes seat %d from slot %d, which goes to seat %d\n", slot, seat, slotAt[seat], seatOf[slot]));
	return TRUE;
}

Bool IsDirectStrikeMap(const AsciiString &map)
{
	UnsignedInt line, edge;
	return LaneGridColours(map, &line, &edge);	// TRUE for a generated lane map and nothing else
}

Bool SetDirectStrikeMap(GameInfo *game)
{
	static UnsignedInt theDirectStrikeRolls = 0;
	RandomMapSettings settings;
	// the client choosing a map, not the simulation: every other machine rebuilds it from the name
	settings.m_seed = (Int)((Clock_Milliseconds() + 7919u * ++theDirectStrikeRolls) & 0x7fffffff);
	settings.m_style = RandomMapGenerator::styleNamed("lane");
	settings.m_numPlayers = DIRECT_STRIKE_SEATS;
	AsciiString map;
	if (!stageRandomMap(settings, map))
		return FALSE;
	map.toLower();
	TheMapCache->updateCache();
	const MapMetaData *md = TheMapCache->findMap(map);
	if (md == NULL)
		return FALSE;
	game->setMap(map);
	game->setMapCRC(md->m_CRC);
	game->setMapSize(md->m_filesize);
	game->getSlot(0)->setMapAvailability(TRUE);
	DEBUG_LOG(("Direct Strike: seed %d, map '%s'\n", settings.m_seed, map.str()));
	return TRUE;
}

/// a seat's army as the mode allows it: Turkiye and the observer become Random, a pool loses Turkiye
static Int directStrikeArmy(Int pick)
{
	if (pick == PLAYERTEMPLATE_OBSERVER)
		return PLAYERTEMPLATE_RANDOM;
	if (pick >= 0)
	{
		const PlayerTemplate *fac = ThePlayerTemplateStore->getNthPlayerTemplate(pick);
		return fac && notInDirectStrike(fac) ? (Int)PLAYERTEMPLATE_RANDOM : pick;
	}
	if (pick >= PLAYERTEMPLATE_RANDOM_POOL)
		return pick;	// plain Random keeps Turkiye out already (RandomExcluded)
	std::vector<Int> candidates;
	GetRandomPoolCandidates(candidates);
	UnsignedInt mask = DecodeRandomPool(pick);
	for (size_t n = 0; n < candidates.size() && n < RANDOM_POOL_BITS; ++n)
		if (notInDirectStrike(ThePlayerTemplateStore->getNthPlayerTemplate(candidates[n])))
			mask &= ~(1u << n);
	return EncodeRandomPool(mask);
}

Bool FitDirectStrikeRoom(GameInfo *game)
{
	Bool changed = FALSE;
	// where every slot shows, read once before anything below moves a start
	Int seatOf[DSLOBBY_SEATS], slotAt[DSLOBBY_SEATS];
	DirectStrikeSeats(game, seatOf, slotAt);
	for (Int i = 0; i < MAX_SLOTS; ++i)
	{
		GameSlot *slot = game->getSlot(i);
		if (slot == NULL)
			continue;
		if (i >= DIRECT_STRIKE_SEATS)
		{
			// a human is never thrown out; seven players cannot start (the map has six starts)
			if (!slot->isHuman() && slot->getState() != SLOT_CLOSED)
			{
				slot->setState(SLOT_CLOSED);
				changed = TRUE;
			}
			continue;
		}
		if (!slot->isOccupied())
		{
			if (slot->getStartPos() != -1)
			{
				slot->setStartPos(-1);
				changed = TRUE;
			}
			continue;
		}

		const Int army = directStrikeArmy(slot->getPlayerTemplate());
		if (army != slot->getPlayerTemplate())
		{
			slot->setPlayerTemplate(army);
			changed = TRUE;
		}

		// The seat is the place: seats 1, 3, 5 are team 1's, on the left, and 2, 4, 6 team 2's, on the
		// right, and seat n is start n, which is how the director reads the teams (DirectStrike_teamOfStart:
		// start 1 is team A's first, start 2 team B's first, start 3 A's second).  A player who moved
		// keeps the start he took; one with none yet (a joiner, a new AI) takes the seat he was shown at.
		const Int seat = seatOf[i];
		const Int team = directStrikeTeamOfSeat(seat);
		if (team != slot->getTeamNumber())
		{
			slot->setTeamNumber(team);
			changed = TRUE;
		}
		if (seat != slot->getStartPos())
		{
			slot->setStartPos(seat);
			changed = TRUE;
		}
	}

	if (game->getUnitLimit()) { game->setUnitLimit(FALSE); changed = TRUE; }
	if (game->getPeaceTime() != 0) { game->setPeaceTime(0); changed = TRUE; }
	if (game->getIncomeSharing() != INCOME_SHARING_OFF) { game->setIncomeSharing(INCOME_SHARING_OFF); changed = TRUE; }
	if (game->getTechRespawn() != 0) { game->setTechRespawn(0); changed = TRUE; }
	if (game->getSupplyPileLimit() != 0) { game->setSupplyPileLimit(0); changed = TRUE; }
	if (game->getProRules()) { game->setProRules(FALSE); changed = TRUE; }
	if (game->getDayNight()) { game->setDayNight(FALSE); changed = TRUE; }	// the lane runs its own light
	// the speed is the host's pick on the lobby's own chip, 1.0x when the room opens (the owner, 2026-11-23);
	// the director's clock is game time, so every speed plays the same match
	return changed;
}

void LayoutDirectStrikeRoom(GameWindow *parent, const char *layoutFilename, const GameInfo *game)
{
	SetLobbyRoomClassic(FALSE);		// the lobby reads the seats by the layout's rows
	DirectStrikeLobby_attach(parent, layoutFilename, game);
}

// Apocalypse ---------------------------------------------------------------------------------

Bool SetApocalypseMap(GameInfo *game, Bool allSeats)
{
	static UnsignedInt theApocalypseRolls = 0;
	RandomMapSettings settings;
	// the client choosing a map, as for Direct Strike: the replay rebuilds it from the name
	settings.m_seed = (Int)((Clock_Milliseconds() + 7919u * ++theApocalypseRolls) & 0x7fffffff);
	settings.m_style = RandomMapGenerator::styleNamed("apocalypse");
	// a network room opens on four starts so adjustSlotsForMap leaves four seats open; Start makes it again
	// for the seats taken (FitRandomMapToRoom)
	settings.m_numPlayers = allSeats ? (Int)APOCALYPSE_SEATS : min((Int)APOCALYPSE_SEATS, roomRandomMapPlayers(game));
	AsciiString map;
	if (!stageRandomMap(settings, map))
		return FALSE;
	map.toLower();
	TheMapCache->updateCache();
	const MapMetaData *md = TheMapCache->findMap(map);
	if (md == NULL)
		return FALSE;
	game->setMap(map);
	game->setMapCRC(md->m_CRC);
	game->setMapSize(md->m_filesize);
	game->getSlot(0)->setMapAvailability(TRUE);
	DEBUG_LOG(("Apocalypse: seed %d, map '%s'\n", settings.m_seed, map.str()));
	return TRUE;
}

Int ApocalypseSeatState(Int seat, Int seats, Int state)
{
	/* No computer in Apocalypse (the owner, 2026-11-27: solo alone, co-op for up to four players).  A
		 computer seat, a saved slot list's or the room's default one, opens for a player within the room's
		 seats; past them every seat but a human's closes (the map has four starts; a human is never thrown
		 out). */
	if (state == SLOT_PLAYER)
		return state;
	if (seat >= seats)
		return SLOT_CLOSED;
	return IsAISlotState((SlotState)state) ? (Int)SLOT_OPEN : state;
}

Bool FitApocalypseRoom(GameInfo *game, Int seats)
{
	Bool changed = FALSE;
	Int nextStart = 0;
	for (Int i = 0; i < MAX_SLOTS; ++i)
	{
		GameSlot *slot = game->getSlot(i);
		if (slot == NULL)
			continue;
		const SlotState fit = (SlotState)ApocalypseSeatState(i, seats, slot->getState());
		if (slot->getState() != fit)
		{
			slot->setState(fit);
			changed = TRUE;
		}
		/* The seats stand on starts 1, 2, 3 in seat order.  The day waves rotate over as many fronts as
			 there are seats and the first front faces start 1, so a player alone on start 2 watched them
			 walk into an empty lobe. */
		const Bool seated = slot->isOccupied() && slot->getPlayerTemplate() != PLAYERTEMPLATE_OBSERVER;
		const Int start = seated ? nextStart++ : -1;
		if (slot->getStartPos() != start)
		{
			slot->setStartPos(start);
			changed = TRUE;
		}
	}
	// every seat holds out together (the director allies them), so the rules that set players apart go
	if (game->getUnitLimit()) { game->setUnitLimit(FALSE); changed = TRUE; }
	if (game->getPeaceTime() != 0) { game->setPeaceTime(0); changed = TRUE; }
	if (game->getTechRespawn() != 0) { game->setTechRespawn(0); changed = TRUE; }
	if (game->getSupplyPileLimit() != 0) { game->setSupplyPileLimit(0); changed = TRUE; }
	if (game->getProRules()) { game->setProRules(FALSE); changed = TRUE; }
	if (game->getDayNight()) { game->setDayNight(FALSE); changed = TRUE; }	// the mode runs its own day and night
	return changed;
}

Int ApocalypseNightFallsAt(Int night)
{
	// the phases run day, dusk, night, dawn from the start (Apocalypse_phaseAt)
	const Int cycle = APOCALYPSE_DAY_SECONDS + APOCALYPSE_DUSK_SECONDS + APOCALYPSE_NIGHT_SECONDS + APOCALYPSE_DAWN_SECONDS;
	return APOCALYPSE_DAY_SECONDS + APOCALYPSE_DUSK_SECONDS + max(0, night - 1) * cycle;
}

Int ApocalypseWallSeconds(Int gameSeconds, Int speedPercent)
{
	return speedPercent > 0 ? (gameSeconds * 100 + speedPercent / 2) / speedPercent : gameSeconds;
}

Int ApocalypseNewKindsOfNight(Int night, Int kinds[], Int most)
{
	Int count = 0;
	for (Int k = 0; k < Apocalypse_kindCount(); ++k)
	{
		const ApocalypseKind &kind = Apocalypse_kind(k);
		Bool rolled = FALSE;
		for (Int phase = 0; phase < APOCALYPSE_PHASES; ++phase)
			rolled = rolled || kind.weight[phase] > 0;
		if (!rolled || (kind.flags & APOCALYPSE_KIND_BOSS) || kind.firstNight != night)
			continue;
		if (count < most)
			kinds[count] = k;
		++count;
	}
	return count;
}

void ApocalypsePlanRoom(Int count, Int mostShown, LobbyRoomPlan &plan)
{
	count = max(0, min(min(count, (Int)APOCALYPSE_SEATS), mostShown));
	const LobbyRoomItem head = { TRUE, LOBBY_GROUP_ALONE, -1, count, 0, LOBBY_ROW_HEAD };
	plan.items[0] = head;
	plan.itemCount = 1;
	for (Int i = 0; i < count; ++i)
	{
		const LobbyRoomItem row = { FALSE, LOBBY_GROUP_ALONE, i, 1, LOBBY_ROW_HEAD + i * LOBBY_ROW_SEAT, LOBBY_ROW_SEAT };
		plan.items[plan.itemCount++] = row;
		plan.order[i] = i;
	}
	plan.orderCount = count;
	plan.scroll = plan.maxScroll = 0;
	plan.above = plan.below = 0;
}

UnicodeString ModeNameWithVersion(const char *nameKey, const char *versionKey)
{
	UnicodeString name = TheGameText->fetch(nameKey);
	Bool exists = FALSE;
	const UnicodeString version = TheGameText->fetch(versionKey, &exists);
	if (exists && !version.isEmpty())
	{
		name.concat(u" ");
		name.concat(version);
	}
	return name;
}

Int TheHostGameMode = HOST_MODE_CLASSIC;

void HideLobbySeatRowsFrom(GameWindow *parent, const char *layoutFilename, Int firstHiddenRow)
{
	// every window lobbyroom_layout.py gives a seat row, by the row's number
	static const char *const names[] = { "ComboBoxPlayer", "StaticTextPlayer", "ComboBoxAIPersona", "ComboBoxPlayerTemplate",
		"ComboBoxColor", "ComboBoxTeam", "ComboBoxHandicap", "SeatCard", "SeatBar", "SeatSwatch", "SeatTeam",
		"GenericPing", "ButtonAccept" };
	for (Int row = max(0, firstHiddenRow); row < MAX_SLOTS; ++row)
	{
		if (findSeatWindow(parent, layoutFilename, "ComboBoxColor", row) == NULL)
			break;	// past the layout's rows
		for (Int n = 0; n < (Int)ARRAY_SIZE(names); ++n)
			if (GameWindow *window = findSeatWindow(parent, layoutFilename, names[n], row))
				if (!window->winIsHidden())
					window->winHide(TRUE);
	}
	// the chat is a tab in the right column now (lobbyroom_layout.py), so nothing under the seats grows
}

Bool IsApocalypseMap(const AsciiString &map)
{
	const char *path = map.str();
	const char *leaf = path;
	for (const char *c = path; *c; ++c)
		if (*c == '\\' || *c == '/')
			leaf = c + 1;
	// the generator's own name only: a template map's tail is its shipped map's folder, which could say anything
	if (!AsciiString(leaf).startsWithNoCase("rmg_v"))
		return FALSE;
	Int version = 0, seed = 0, players = 0, cells = 0, rest = -1;
	if (sscanf(leaf + 5, "%d_%d_%dp_%dc_%n", &version, &seed, &players, &cells, &rest) != 4 || rest < 0)
		return FALSE;
	const AsciiString tail(leaf + 5 + rest);
	return tail.compareNoCase("apocalypse") == 0 || tail.compareNoCase("apocalypse.map") == 0;
}

Bool IsAodMap(const AsciiString &map)
{
	const char *path = map.str();
	const char *leaf = path;
	for (const char *c = path; *c; ++c)
		if (*c == '\\' || *c == '/')
			leaf = c + 1;
	if (!AsciiString(leaf).startsWithNoCase("rmg_v"))
		return FALSE;
	Int version = 0, seed = 0, players = 0, cells = 0, rest = -1;
	if (sscanf(leaf + 5, "%d_%d_%dp_%dc_%n", &version, &seed, &players, &cells, &rest) != 4 || rest < 0)
		return FALSE;
	const AsciiString tail(leaf + 5 + rest);
	return tail.compareNoCase("aod") == 0 || tail.compareNoCase("aod.map") == 0;
}

/// what AskGameMode's box answers with; GameModeBoxSystem deletes it with the box
struct GameModeBoxData
{
	void (*answer)(Int mode);
};

// GameModeBox.wnd's system callback (FunctionLexicon): a mode button answers and closes the box, Cancel closes it
WindowMsgHandledType GameModeBoxSystem(GameWindow *window, UnsignedInt msg, WindowMsgData mData1, WindowMsgData mData2)
{
	switch (msg)
	{
		case GWM_DESTROY:
			delete (GameModeBoxData *)window->winGetUserData();
			window->winSetUserData(NULL);
			break;
		case GWM_INPUT_FOCUS:
			if (mData1 == TRUE)
				*(Bool *)mData2 = TRUE;
			break;
		case GBM_SELECTED:
		{
			const Int controlID = ((GameWindow *)mData1)->winGetWindowId();
			static const NameKeyType classicID = NAMEKEY("GameModeBox.wnd:ButtonClassic");
			static const NameKeyType directStrikeID = NAMEKEY("GameModeBox.wnd:ButtonDirectStrike");
			static const NameKeyType apocalypseID = NAMEKEY("GameModeBox.wnd:ButtonApocalypse");
			static const NameKeyType cancelID = NAMEKEY("GameModeBox.wnd:ButtonCancel");
			const Int mode = controlID == classicID ? HOST_MODE_CLASSIC : controlID == directStrikeID ? HOST_MODE_DIRECT_STRIKE
				: controlID == apocalypseID ? HOST_MODE_APOCALYPSE : -1;
			if (mode < 0 && controlID != cancelID)
				return MSG_IGNORED;
			GameModeBoxData *data = (GameModeBoxData *)window->winGetUserData();
			void (*answer)(Int) = data ? data->answer : NULL;
			// the box goes first: an answer opens the next screen or a popup over where it stood
			TheWindowManager->winDestroy(window);
			if (answer && mode >= 0)
				answer(mode);
			break;
		}
		default:
			return MSG_IGNORED;
	}
	return MSG_HANDLED;
}

/* The mode question's face, a box in the menu language (README-classic-room.md, "The box pattern";
	 mode.py draws it).  GameModeBox.wnd (Tools/gamemode_layout.py) brings the windows: the modal root,
	 its panel, and four buttons whose clicks GameModeBoxSystem answers.  Nothing of EA's message box is
	 drawn: the panel is the box (frame A on its glass, the title with its question after it, the lit
	 rule), the three mode buttons are cards 340 by 300 (40 over Cancel) with a drawn picture, the name and its version
	 tag, a line and the seats, the default one lit as Enter's key, and Cancel is a key at the foot's
	 right with the hint at its left.  Every number is at 1080 on the menu board. */
namespace
{
	enum
	{
		PICKER_CARD_W = 340, PICKER_CARD_H = 300, PICKER_GAP = 24, PICKER_PAD = 32, PICKER_CARDS_TOP = 106,
		PICKER_INSET = 14, PICKER_PICTURE_H = 176, PICKER_KEY_W = 220, PICKER_KEY_H = 48,
		PICKER_PANEL_H = 514,
	};
	/// the box round `cards` cards in a row
	Int pickerPanelW(Int cards) { return 2 * PICKER_PAD + cards * PICKER_CARD_W + (cards - 1) * PICKER_GAP; }
	Bool thePickerModsOnly = TRUE;		///< the box up now is the Mods buttons' (no Classic card)
	Bool thePickerSolo = FALSE;				///< Solo Play's words
	struct PickerCard { const char *button, *nameKey, *versionKey, *lineKey, *seatsKey, *soloKey; Int mode; };
	const PickerCard thePickerCards[] =
	{
		{ "GameModeBox.wnd:ButtonClassic", "GUI:GameModeClassic", NULL, "GUI:GameModeClassicFact", "GUI:GameModeClassicSeats", "GUI:GameModeClassicSolo", HOST_MODE_CLASSIC },
		{ "GameModeBox.wnd:ButtonDirectStrike", "GUI:GameModeDirectStrike", "GUI:DirectStrikeVersion", "GUI:GameModeDirectStrikeFact", "GUI:GameModeDirectStrikeSeats", "GUI:GameModeDirectStrikeSolo", HOST_MODE_DIRECT_STRIKE },
		{ "GameModeBox.wnd:ButtonApocalypse", "GUI:GameModeApocalypse", "GUI:ApocalypseVersion", "GUI:GameModeApocalypseFact", "GUI:GameModeApocalypseSeats", "GUI:GameModeApocalypseSolo", HOST_MODE_APOCALYPSE },
	};
	/// the card Enter picks: Classic, or Direct Strike when the box has no Classic card
	Int pickerLitMode(void) { return thePickerModsOnly ? HOST_MODE_DIRECT_STRIKE : HOST_MODE_CLASSIC; }
	enum { PICKER_TEXTS = 16 };
	DisplayString *thePickerTexts[PICKER_TEXTS];
	DisplayString *pickerText(Int key, const UnicodeString &text, GameFont *font)
	{
		DisplayString *&s = thePickerTexts[key];
		if (s == NULL)
			s = TheDisplayStringManager->newDisplayString();
		if (s->getFont() != font)
			s->setFont(font);
		if (s->getText().compare(text) != 0)
			s->setText(text);
		return s;
	}
	/// words centred on x and y, or from x when left; their width
	Int pickerDraw(Int key, const UnicodeString &text, GameFont *font, Int x, Int y, Color color, Bool left = FALSE)
	{
		DisplayString *s = pickerText(key, text, font);
		Int w, h;
		s->getSize(&w, &h);
		s->draw(left ? x : x - w / 2, y - h / 2, color, GameMakeColor(0, 0, 0, 160));
		return w;
	}

	Real pickerScale(void)
	{
		return TheDisplay ? UIStyle::boardFit(TheDisplay->getWidth(), TheDisplay->getHeight()).scale : 1.0f;
	}
	Int px(Int v1080)
	{
		return REAL_TO_INT(v1080 * pickerScale() + 0.5f);
	}
	DirectStrikeHudBox screenBox(GameWindow *window)
	{
		DirectStrikeHudBox b;
		window->winGetScreenPosition(&b.x, &b.y);
		window->winGetSize(&b.w, &b.h);
		return b;
	}
	DirectStrikeHudBox inset(const DirectStrikeHudBox &b, Int by)
	{
		DirectStrikeHudBox r = { b.x + by, b.y + by, b.w - 2 * by, b.h - 2 * by };
		return r;
	}
	void fill(Int x, Int y, Int w, Int h, Color color)
	{
		TheDisplay->drawFillRect(x, y, w, h, color);
	}

	/// a picture of the mode from flat shapes: four bases round a field, a lane between two forts, a walled basin in red fog
	void drawModeEmblem(Int mode, const DirectStrikeHudBox &b)
	{
		const Color blue = GameMakeColor(70, 120, 220, 255), red = GameMakeColor(210, 60, 50, 255);
		const Color green = GameMakeColor(80, 170, 80, 255), gold = GameMakeColor(230, 190, 60, 255);
		const Int u = max(1, min(b.w, b.h) / 12);
		const Int cx = b.x + b.w / 2, cy = b.y + b.h / 2;
		if (mode == HOST_MODE_DIRECT_STRIKE)
		{
			fill(b.x, b.y, b.w, b.h, GameMakeColor(18, 38, 64, 255));					// sea
			fill(b.x + u, cy - 2 * u, b.w - 2 * u, 4 * u, GameMakeColor(92, 104, 70, 255));	// the island's lane
			fill(b.x + 2 * u, cy - 2 * u, 3 * u, 4 * u, blue);							// the two fortresses
			fill(b.x + b.w - 5 * u, cy - 2 * u, 3 * u, 4 * u, red);
			for (Int i = 0; i < 3; ++i)																	// a wave down each side
			{
				fill(cx - (4 - i) * u * 3 / 2, cy - u / 2, u, u, blue);
				fill(cx + (2 + i) * u * 3 / 2 - u, cy - u / 2, u, u, red);
			}
		}
		else if (mode == HOST_MODE_APOCALYPSE)
		{
			fill(b.x, b.y, b.w, b.h, GameMakeColor(96, 20, 18, 255));					// the red fog
			const Int r = min(b.w, b.h) / 3;
			fill(cx - r, cy - r, 2 * r, 2 * r, GameMakeColor(58, 54, 48, 255));		// the walled basin
			TheDisplay->drawOpenRect(cx - r, cy - r, 2 * r, 2 * r, (Real)max(2, u / 2), GameMakeColor(28, 24, 22, 255));
			const Int s = max(2, u);
			fill(cx - u - s / 2, cy - u - s / 2, s, s, blue);							// four seats together
			fill(cx + u - s / 2, cy - u - s / 2, s, s, gold);
			fill(cx - u - s / 2, cy + u - s / 2, s, s, green);
			fill(cx + u - s / 2, cy + u - s / 2, s, s, GameMakeColor(200, 200, 200, 255));
			for (Int i = 0; i < 6; ++i)																	// the dead at the edge
			{
				const Int ex = b.x + u + (i % 3) * (b.w - 3 * u) / 2, ey = i < 3 ? b.y + u : b.y + b.h - 2 * u;
				fill(ex, ey, s, s, GameMakeColor(30, 6, 6, 255));
			}
		}
		else
		{
			fill(b.x, b.y, b.w, b.h, GameMakeColor(52, 60, 44, 255));					// open ground
			TheDisplay->drawLine(b.x + 3 * u, b.y + 3 * u, b.x + b.w - 3 * u, b.y + b.h - 3 * u, (Real)max(1, u / 3), GameMakeColor(120, 112, 90, 255));
			TheDisplay->drawLine(b.x + b.w - 3 * u, b.y + 3 * u, b.x + 3 * u, b.y + b.h - 3 * u, (Real)max(1, u / 3), GameMakeColor(120, 112, 90, 255));
			const Int s = 3 * u;
			fill(b.x + u, b.y + u, s, s, blue);												// four bases, a corner each
			fill(b.x + b.w - u - s, b.y + u, s, s, red);
			fill(b.x + u, b.y + b.h - u - s, s, s, green);
			fill(b.x + b.w - u - s, b.y + b.h - u - s, s, s, gold);
		}
	}

	IRegion2D windowRegion(GameWindow *window)
	{
		IRegion2D r;
		Int w, h;
		window->winGetScreenPosition(&r.lo.x, &r.lo.y);
		window->winGetSize(&w, &h);
		r.hi.x = r.lo.x + w;
		r.hi.y = r.lo.y + h;
		return r;
	}

	void drawPickerRoot(GameWindow *, WinInstanceData *)
	{
		// boxShell steps the screen back itself
	}

	void drawPickerPanel(GameWindow *window, WinInstanceData *)
	{
		const IRegion2D box = windowRegion(window);
		DisplayString *title = pickerText(0, UIStyle::capitals(TheGameText->fetch(thePickerModsOnly ? "GUI:Mods" : thePickerSolo ? "GUI:GameModeSoloTitle" : "GUI:GameModeTitle")),
			UIStyle::menuFont(UIStyle::MENU_TEXT_BOX_TITLE, TRUE));
		UIStyle::boxShell(box, title, FALSE);
		// the question after the title on its line, MUTED
		Int titleW, titleH;
		title->getSize(&titleW, &titleH);
		if (!thePickerModsOnly)
			pickerDraw(1, TheGameText->fetch(thePickerSolo ? "GUI:GameModeSoloQuestion" : "GUI:GameModeBody"), UIStyle::menuFont(UIStyle::MENU_TEXT_BODY),
				box.lo.x + px(PICKER_PAD) + titleW + px(18), box.lo.y + px(46), UIStyle::MUTED, TRUE);
		UIStyle::boxFootRule(box);
		pickerDraw(2, TheGameText->fetch("GUI:GameModeHint"), UIStyle::menuFont(UIStyle::MENU_TEXT_HINT),
			box.lo.x + px(PICKER_PAD), box.hi.y - px(44), UIStyle::MUTED, TRUE);
	}

	void drawPickerCard(GameWindow *window, WinInstanceData *instData)
	{
		const PickerCard *card = NULL;
		Int index = 0;
		for (Int i = 0; i < (Int)ARRAY_SIZE(thePickerCards); ++i)
			if (window->winGetWindowId() == NAMEKEY(thePickerCards[i].button))
			{
				card = &thePickerCards[i];
				index = i;
			}
		if (card == NULL)
			return;
		const Bool hot = BitTest(instData->getState(), WIN_STATE_HILITED);
		const Bool pressed = BitTest(instData->getState(), WIN_STATE_SELECTED);
		const Bool lit = card->mode == pickerLitMode();
		const IRegion2D box = windowRegion(window);
		const Int w = box.hi.x - box.lo.x, h = box.hi.y - box.lo.y;
		// the card: a FRAME line, the lit one the primary key's light, the pointer's a white lift
		TheDisplay->drawFillRect(box.lo.x, box.lo.y, w, h, GameMakeColor(0, 0, 0, 150));
		if (lit)
		{
			TheDisplay->drawFillRect(box.lo.x, box.lo.y, w, h, UIStyle::withAlpha(UIStyle::EDGE, pressed ? 110 : 60));
			UIStyle::litEdge(box, UIStyle::EDGE_LIT);
		}
		else
			TheDisplay->drawOpenRect(box.lo.x, box.lo.y, w, h, 1.0f, hot ? UIStyle::EDGE_LIT : UIStyle::FRAME);
		if (hot)
			TheDisplay->drawFillRect(box.lo.x, box.lo.y, w, h, GameMakeColor(255, 255, 255, 14));
		const DirectStrikeHudBox picture = { box.lo.x + px(PICKER_INSET), box.lo.y + px(PICKER_INSET), w - 2 * px(PICKER_INSET), px(PICKER_PICTURE_H) };
		drawModeEmblem(card->mode, picture);
		TheDisplay->drawOpenRect(picture.x, picture.y, picture.w, picture.h, 1.0f, UIStyle::withAlpha(UIStyle::EDGE, 160));
		if (pressed)
			fill(picture.x, picture.y, picture.w, picture.h, GameMakeColor(0, 0, 0, 90));

		// the name in spaced capitals with its version tag small after it, the two centred together
		GameFont *nameFont = UIStyle::menuFont(UIStyle::MENU_TEXT_BOX_TITLE, TRUE);
		GameFont *tagFont = UIStyle::menuFont(UIStyle::MENU_TEXT_HINT);
		Bool tagged = FALSE;
		const UnicodeString tag = card->versionKey ? TheGameText->fetch(card->versionKey, &tagged) : UnicodeString::TheEmptyString;
		DisplayString *name = pickerText(3 + 4 * index, TheGameText->fetch(card->nameKey), nameFont);
		Int nameW, nameH;
		name->getSize(&nameW, &nameH);
		Int tagW = 0, tagH = 0;
		DisplayString *tagText = NULL;
		if (tagged && !tag.isEmpty())
		{
			tagText = pickerText(4 + 4 * index, tag, tagFont);
			tagText->getSize(&tagW, &tagH);
		}
		const Int pairW = nameW + (tagW > 0 ? px(10) + tagW : 0);
		const Int nameY = box.lo.y + px(221);
		const Int left = box.lo.x + (w - pairW) / 2;
		name->draw(left, nameY - nameH / 2, UIStyle::INK, GameMakeColor(0, 0, 0, 160));
		if (tagText)
			tagText->draw(left + nameW + px(10), nameY - tagH / 2 + px(2), UIStyle::MUTED, GameMakeColor(0, 0, 0, 160));
		const Int centre = box.lo.x + w / 2;
		pickerDraw(5 + 4 * index, TheGameText->fetch(card->lineKey), UIStyle::menuFont(UIStyle::MENU_TEXT_BODY), centre, box.lo.y + px(257), UIStyle::INK);
		pickerDraw(6 + 4 * index, TheGameText->fetch(thePickerSolo ? card->soloKey : card->seatsKey), UIStyle::menuFont(UIStyle::MENU_TEXT_LABEL, TRUE),
			centre, box.lo.y + px(284), UIStyle::EDGE_LIT);
	}

	void drawPickerCancel(GameWindow *window, WinInstanceData *instData)
	{
		const Bool hot = BitTest(instData->getState(), WIN_STATE_HILITED);
		const Bool pressed = BitTest(instData->getState(), WIN_STATE_SELECTED);
		DisplayString *words = pickerText(15, UIStyle::capitals(TheGameText->fetch("GUI:Cancel")), UIStyle::menuFont(UIStyle::MENU_TEXT_KEY, TRUE));
		UIStyle::menuKey(windowRegion(window), words, pressed ? UIStyle::KEY_PRESSED : hot ? UIStyle::KEY_HOVER : UIStyle::KEY_NORMAL, FALSE);
	}

	/// the card Enter picks, by its mode
	GameWindow *pickerCard(GameWindow *box, Int mode)
	{
		for (Int i = 0; i < (Int)ARRAY_SIZE(thePickerCards); ++i)
			if (thePickerCards[i].mode == mode)
				return TheWindowManager->winGetWindowFromId(box, NAMEKEY(thePickerCards[i].button));
		return NULL;
	}

	// Escape closes the question unanswered, as Cancel does; Enter picks the lit card
	WindowMsgHandledType pickerInput(GameWindow *window, UnsignedInt msg, WindowMsgData mData1, WindowMsgData mData2)
	{
		if (msg == GWM_CHAR && mData1 == KEY_ESC)
		{
			if (BitTest(mData2, KEY_STATE_UP) && window->winGetParent())
				TheWindowManager->winDestroy(window->winGetParent());
			return MSG_HANDLED;
		}
		if (msg == GWM_CHAR && (mData1 == KEY_ENTER || mData1 == KEY_KPENTER))
		{
			GameWindow *root = window->winGetParent();
			GameWindow *card = root ? pickerCard(root, pickerLitMode()) : NULL;
			if (BitTest(mData2, KEY_STATE_UP) && card)
				TheWindowManager->winSendSystemMsg(root, GBM_SELECTED, (WindowMsgData)card, card->winGetWindowId());
			return MSG_HANDLED;
		}
		return MSG_IGNORED;
	}
}

void AskGameMode(void (*answer)(Int mode), Bool modsOnly, Bool solo)
{
	thePickerModsOnly = modsOnly;
	thePickerSolo = solo && !modsOnly;
	GameWindow *box = TheWindowManager->winCreateFromScript(AsciiString("Menus/GameModeBox.wnd"));
	if (box == NULL)
		return;
	GameWindow *panel = TheWindowManager->winGetWindowFromId(box, NAMEKEY("GameModeBox.wnd:MessageBoxParent"));
	static const char *const keys[] = { "GameModeBox.wnd:ButtonClassic", "GameModeBox.wnd:ButtonDirectStrike",
		"GameModeBox.wnd:ButtonApocalypse", "GameModeBox.wnd:ButtonCancel" };
	const Int count = (Int)ARRAY_SIZE(keys);
	GameWindow *buttons[ARRAY_SIZE(keys)];
	for (Int i = 0; i < count; ++i)
	{
		buttons[i] = TheWindowManager->winGetWindowFromId(box, NAMEKEY(keys[i]));
		if (buttons[i] == NULL || panel == NULL)
		{
			TheWindowManager->winDestroy(box);
			return;
		}
	}
	GameModeBoxData *data = NEW GameModeBoxData;
	data->answer = answer;
	box->winSetUserData(data);
	TheWindowManager->winSetModal(box);
	TheWindowManager->winSetFocus(NULL);
	TheWindowManager->winSetFocus(panel);
	// the layout's own text and header band stay hidden; the plate, the cards and Cancel are drawn
	for (GameWindow *child = panel->winGetChild(); child; child = child->winGetNext())
		child->winHide(TRUE);
	box->winSetDrawFunc(drawPickerRoot);
	panel->winSetDrawFunc(drawPickerPanel);
	panel->winSetInputFunc(pickerInput);

	// the box centred on the board, the cards in a row (the Mods buttons' without Classic), Cancel at the foot's right
	Int rootW, rootH;
	box->winGetSize(&rootW, &rootH);
	const Int first = modsOnly ? 1 : 0;
	const Int panelW = px(pickerPanelW(3 - first)), panelH = px(PICKER_PANEL_H);
	panel->winSetSize(panelW, panelH);
	panel->winSetPosition((rootW - panelW) / 2, (rootH - panelH) / 2);
	const Int cardsY = px(PICKER_CARDS_TOP);
	for (Int i = first; i < 3; ++i)
	{
		buttons[i]->winSetSize(px(PICKER_CARD_W), px(PICKER_CARD_H));
		buttons[i]->winSetPosition(px(PICKER_PAD + (i - first) * (PICKER_CARD_W + PICKER_GAP)), cardsY);
		buttons[i]->winSetDrawFunc(drawPickerCard);
		buttons[i]->winHide(FALSE);
	}
	buttons[3]->winSetSize(px(PICKER_KEY_W), px(PICKER_KEY_H));
	buttons[3]->winSetPosition(panelW - px(PICKER_PAD + PICKER_KEY_W), panelH - px(20 + PICKER_KEY_H));
	buttons[3]->winSetDrawFunc(drawPickerCancel);
	buttons[3]->winHide(FALSE);
	panel->winHide(FALSE);
	panel->winBringToTop();
}

/* The online room's seats name each human with his ladder rating after the nick, MUTED, read from the
	 card the server sent the lobby for that nick (ONLINE-UI-REDESIGN.md, "Game setup room").  The player
	 box keeps EA's art and its text; the rating is drawn after it, so the room's combo code is untouched. */
static GameWindow *theSeatPlayer[MAX_SLOTS];			///< each seat's player box's text entry
static AsciiString theSeatNick[MAX_SLOTS];				///< the human in it, empty for anyone else
static DisplayString *theSeatRating[MAX_SLOTS];
static GameWinDrawFunc theSeatPlayerDraw = NULL;

static void drawSeatPlayer(GameWindow *window, WinInstanceData *instData)
{
	if (theSeatPlayerDraw)
		theSeatPlayerDraw(window, instData);
	Int seat = -1;
	for (Int i = 0; i < MAX_SLOTS && seat < 0; ++i)
		if (theSeatPlayer[i] == window)
			seat = i;
	OnlinePlayerInfo card;
	if (seat < 0 || theSeatNick[seat].isEmpty() || !OnlineGetPlayerInfo(theSeatNick[seat], card) || !card.hasRating)
		return;
	const EntryData *entry = (const EntryData *)window->winGetUserData();
	if (!entry || !entry->text)
		return;
	if (!theSeatRating[seat])
		theSeatRating[seat] = TheDisplayStringManager->newDisplayString();
	DisplayString *rating = theSeatRating[seat];
	if (rating->getFont() != window->winGetFont())
		rating->setFont(window->winGetFont());
	rating->setText(UIStyle::ladderText(card.shown(), card.hasElo));

	// where W3DTextEntry ends the nick: its text starts seven pixels in (ten from the start), centred on
	// the box by the font's height
	Int x, y, width, height, w, h;
	window->winGetScreenPosition(&x, &y);
	window->winGetSize(&width, &height);
	rating->getSize(&w, &h);
	const Int at = x + (entry->drawTextFromStart ? 10 : 7) + entry->text->getWidth() + UIStyle::px(10.0f);
	if (at + w > x + width - 5)
		return;		// a nick that fills the box keeps it to itself
	const Int top = y + max(1, (height - TheWindowManager->winFontHeight(instData->getFont())) / 2);
	rating->draw(at, top, UIStyle::MUTED, UIStyle::SHADOW);
}

static void attachSeatRatings(GameWindow *parent, const char *layoutFilename)
{
	for (Int i = 0; i < MAX_SLOTS; ++i)
	{
		theSeatPlayer[i] = NULL;
		theSeatNick[i].clear();
		if (!OnlineIsEnabled() || strcmp(layoutFilename, "GameSpyGameOptionsMenu.wnd") != 0)
			continue;
		AsciiString name;
		name.format("%s:ComboBoxPlayer%d", layoutFilename, i);
		GameWindow *combo = TheWindowManager->winGetWindowFromId(parent, NAMEKEY(name));
		GameWindow *box = combo ? GadgetComboBoxGetEditBox(combo) : NULL;
		if (!box)
			continue;
		theSeatPlayer[i] = box;
		if (box->winGetDrawFunc() == drawSeatPlayer)
			continue;		// a second init on the same layout: the box is hooked already, and capturing again would recurse
		theSeatPlayerDraw = box->winGetDrawFunc();
		box->winSetDrawFunc(drawSeatPlayer);
	}
}

static void detachSeatRatings(void)
{
	for (Int i = 0; i < MAX_SLOTS; ++i)
	{
		if (theSeatRating[i])
			TheDisplayStringManager->freeDisplayString(theSeatRating[i]);
		theSeatRating[i] = NULL;
		theSeatPlayer[i] = NULL;
		theSeatNick[i].clear();
	}
}

// =============================================================================================
// The Classic room in the menu language (MENU-REDESIGN.md package 3a, README-classic-room.md).
// The order and the window are pure and tested (test_room_layout.inc); the look below them draws
// on the room's container and moves its rows.
// =============================================================================================

/// alone first, then the teams by size and number, the unseated last
static Bool lobbyGroupBefore(Int group, Int size, Int other, Int otherSize)
{
	const Int rank = group == LOBBY_GROUP_ALONE ? 0 : group == LOBBY_GROUP_UNSEATED ? 2 : 1;
	const Int otherRank = other == LOBBY_GROUP_ALONE ? 0 : other == LOBBY_GROUP_UNSEATED ? 2 : 1;
	if (rank != otherRank)
		return rank < otherRank;
	if (size != otherSize)
		return size < otherSize;
	return group < other;
}

void LobbyOrderSeats(const LobbySeatState seats[], Int count, Int order[], Int group[])
{
	count = max(0, min(count, (Int)MAX_SLOTS));
	Int groupOf[MAX_SLOTS];
	Int groups[MAX_SLOTS], sizes[MAX_SLOTS];
	Int groupCount = 0;
	for (Int i = 0; i < count; ++i)
	{
		groupOf[i] = seats[i].team >= 0 ? seats[i].team : seats[i].seated ? LOBBY_GROUP_ALONE : LOBBY_GROUP_UNSEATED;
		Int g = 0;
		while (g < groupCount && groups[g] != groupOf[i])
			++g;
		if (g == groupCount)
		{
			groups[g] = groupOf[i];
			sizes[g] = 0;
			++groupCount;
		}
		++sizes[g];
	}
	// an insertion sort: a room holds 32 groups at most
	for (Int a = 1; a < groupCount; ++a)
		for (Int b = a; b > 0 && lobbyGroupBefore(groups[b], sizes[b], groups[b - 1], sizes[b - 1]); --b)
		{
			std::swap(groups[b], groups[b - 1]);
			std::swap(sizes[b], sizes[b - 1]);
		}
	Int n = 0;
	for (Int g = 0; g < groupCount; ++g)
		for (Int i = 0; i < count; ++i)
			if (groupOf[i] == groups[g])
			{
				order[n] = i;
				group[n] = groups[g];
				++n;
			}
}

/// the window from the ordered seat `first`: what it shows, into plan when given; the seats shown
static Int lobbyLayRoom(const LobbySeatState seats[], const Int order[], const Int group[], Int count, Int first,
	Int height, Int mostShown, LobbyRoomPlan *plan)
{
	Int y = first > 0 ? LOBBY_ROW_STRIP : 0;
	Int shown = 0, items = 0;
	for (Int k = first; k < count && shown < mostShown; ++k)
	{
		const Bool newGroup = k == first || group[k] != group[k - 1];
		const Int rowHeight = LOBBY_ROW_SEAT;
		// a seat after this one needs the foot's strip should it not fit
		const Int reserve = k < count - 1 ? LOBBY_ROW_STRIP : 0;
		if (y + (newGroup ? LOBBY_ROW_HEAD : 0) + rowHeight + reserve > height)
			break;
		if (newGroup)
		{
			if (plan)
			{
				Int size = 0;
				for (Int j = 0; j < count; ++j)
					size += group[j] == group[k] ? 1 : 0;
				const LobbyRoomItem head = { TRUE, group[k], -1, size, y, LOBBY_ROW_HEAD };
				plan->items[items++] = head;
			}
			y += LOBBY_ROW_HEAD;
		}
		if (plan)
		{
			const LobbyRoomItem row = { FALSE, group[k], order[k], 1, y, rowHeight };
			plan->items[items++] = row;
		}
		y += rowHeight;
		++shown;
	}
	if (plan)
	{
		plan->itemCount = items;
		plan->above = first;
		plan->below = count - first - shown;
	}
	return shown;
}

void LobbyPlanRoom(const LobbySeatState seats[], Int count, Int scroll, Int height, Int mostShown, LobbyRoomPlan &plan)
{
	count = max(0, min(count, (Int)MAX_SLOTS));
	Int group[MAX_SLOTS];
	LobbyOrderSeats(seats, count, plan.order, group);
	plan.orderCount = count;
	// the furthest the list scrolls: the first seat from which the rest all fit
	plan.maxScroll = 0;
	while (plan.maxScroll < count - 1
		&& lobbyLayRoom(seats, plan.order, group, count, plan.maxScroll, height, mostShown, NULL) < count - plan.maxScroll)
		++plan.maxScroll;
	plan.scroll = max(0, min(scroll, plan.maxScroll));
	lobbyLayRoom(seats, plan.order, group, count, plan.scroll, height, mostShown, &plan);
}

namespace
{
	enum RoomKind { ROOM_NONE, ROOM_SKIRMISH, ROOM_LAN, ROOM_ONLINE };
	struct RoomRow
	{
		GameWindow *player, *name, *persona, *army, *colour, *team, *handicap, *ping;
	};
	RoomKind theRoomKind = ROOM_NONE;
	Bool theRoomClassic = TRUE;
	GameWindow *theRoomParent = NULL;
	GameWindow *theRoomContainer = NULL;
	GameWindow *theRoomTitle = NULL;
	GameWindow *theRoomMap = NULL;
	GameWindow *theRoomMapName = NULL;
	GameWindow *theRoomSelectMap = NULL;
	GameWindow *theRoomPresets = NULL;
	GameWindow *theRoomStats[4];					///< the skirmish record's wins, losses, streak, best streak
	RoomRow theRoomRows[MAX_SLOTS];
	Int theRoomRowCount = 0;
	GameInfo *theRoomGame = NULL;
	LobbyRoomPlan theRoomPlan;
	Int theRoomScroll = 0;
	Int theRoomSortTeam[MAX_SLOTS];
	Bool theRoomHeld = FALSE;							///< a team change waits for its list to close
	Bool theRoomDirty = FALSE;
	Int theRoomMoved = -1;								///< the seat that just changed group, lit for a second
	UnsignedInt theRoomMovedUntil = 0;
	Int theRoomBound[MAX_SLOTS];					///< the seats the binder last got
	Int theRoomBoundCount = -1;
	LobbyRowBinder theRoomBinder = NULL;
	GameWindow *theRoomChat = NULL;
	Int theRoomChatSeen = 0;
	const Int ROOM_SORT_UNSET = -99;
	const Int ROOM_SEAT_COLUMN_RIGHT = 1294;
	const Int ROOM_CELLS_RIGHT = 1262;
	const Int ROOM_COLUMN_LEFT = 1318;
	const Int ROOM_RIGHT = 1860;
	const Int ROOM_LEFT = 60;
	const char *theRoomLayout = NULL;
	GameWinDrawFunc theRoomMapDraw = NULL;		///< the map window's own draw, under the Apocalypse fog edge

	// The Apocalypse room (README-apocalypse-room.md): the nights band under the seats, the chat under it
	// in a network room, the rules the mode pins read only in the right column's foot
	const Int APOC_CELL_LEFT = 152;
	const Int APOC_RIGHT = 1270;							///< the seat rows' and the band's right edge
	const Color APOC_BLOOD_TEXT = GameMakeColor(236, 92, 78, 255);	///< the Blood Moon's red lifted for words on the glass
	const Int APOC_NIGHT_CELLS = 10;					///< nights 1 to 9, then the twelfth and on
	// the nights band stands 14 under the four seats' rows, whatever a row's height; the chat under it takes the rest
	const Int APOC_NIGHTS_TOP = LOBBY_SEATS_TOP + LOBBY_ROW_HEAD + APOCALYPSE_SEATS * LOBBY_ROW_SEAT + 14;
	const Int APOC_DAY_TOP = APOC_NIGHTS_TOP + 40, APOC_DAY_BOTTOM = APOC_NIGHTS_TOP + 82;
	const Int APOC_TABLE_TOP = APOC_NIGHTS_TOP + 98;
	const Int APOC_CHAT_TOP = APOC_NIGHTS_TOP + 218;
	const Int APOC_RULES_TOP = 542;
	const Int APOC_MODE_TOP = 804;
	const Color APOC_BLOOD = GameMakeColor(184, 38, 31, 255);	///< BRAND.md's red, the Blood Moon's only

	// one display string a place on the screen, so a string's texture is rebuilt only when its words change
	enum
	{
		TEXT_HEADS = 0, TEXT_TITLE = 8, TEXT_SUBTITLE, TEXT_TENANT, TEXT_TENANT_NAME, TEXT_TENANT_SEATS,
		TEXT_FOOT = 16, TEXT_STRIPS = 28, TEXT_MAP = 32, TEXT_TAB = 36, TEXT_GROUPS = 40, TEXT_ROWS = 120,
		// a seat's words: its number, the portrait's "?", the rating, the state, the name fitted before them
		TEXT_ROW_NUMBER = 0, TEXT_ROW_UNKNOWN, TEXT_ROW_RATING, TEXT_ROW_STATE, TEXT_ROW_NAME, TEXT_ROW_KEYS,
		TEXT_APOCALYPSE = TEXT_ROWS + TEXT_ROW_KEYS * MAX_SLOTS,
		// the Apocalypse room's own words, from TEXT_APOCALYPSE on
		APOC_HEAD = 0,				///< survivors, their count, then three facts as a word and a note each
		APOC_OPEN_SEAT = 8,		///< one a row
		APOC_NIGHTS = 12,			///< the band's head, the clock's speed, the Blood Moon's key and note
		APOC_DAY = 16,				///< ONE DAY, then four phase words, four lengths, the first wave and its time
		APOC_LABELS = 28,			///< the table's row words, DAY 1 and the walker
		APOC_CELLS = 36,			///< night numbers, falls at, new dead, boss: ten cells each
		APOC_NAMES = 76,			///< the skirmish table's names, six a cell
		APOC_MAP = 136,				///< the map's two facts and its four key lines
		APOC_MODE = 142,			///< SET BY THE MODE, its eight pairs
		APOC_FOOT = 159,			///< what the mode counts, the RULES head
		APOC_COUNT = 162,
		TEXT_COUNT = TEXT_APOCALYPSE + APOC_COUNT
	};
	DisplayString *theRoomTexts[TEXT_COUNT];

	DisplayString *roomText(Int key, const UnicodeString &text, GameFont *font)
	{
		if (key < 0 || key >= TEXT_COUNT)
			return NULL;
		DisplayString *&s = theRoomTexts[key];
		if (s == NULL)
			s = TheDisplayStringManager->newDisplayString();
		if (s->getFont() != font)
			s->setFont(font);
		if (s->getText().compare(text) != 0)
			s->setText(text);
		return s;
	}

	Int roomX(Int x) { return UIStyle::boardRect(x, 0, x, 0).lo.x; }
	Int roomY(Int y) { return UIStyle::boardRect(0, y, 0, y).lo.y; }
	Int roomPx(Int length) { return UIStyle::pxOn((Real)length, TheDisplay->getWidth(), TheDisplay->getHeight()); }
	void roomFill(Int l, Int t, Int r, Int b, Color color)
	{
		const IRegion2D box = UIStyle::boardRect(l, t, r, b);
		TheDisplay->drawFillRect(box.lo.x, box.lo.y, max(1, box.hi.x - box.lo.x), max(1, box.hi.y - box.lo.y), color);
	}

	/// words at board x (left, centre or right by align 0, 1, 2), centred on board y; their width on screen
	Int roomDraw(Int key, const UnicodeString &text, GameFont *font, Int x, Int middle, Color color, Int align = 0)
	{
		DisplayString *s = roomText(key, text, font);
		if (s == NULL || text.isEmpty())
			return 0;
		Int w, h;
		s->getSize(&w, &h);
		Int left = roomX(x);
		if (align == 1)
			left -= w / 2;
		else if (align == 2)
			left -= w;
		s->draw(left, roomY(middle) - h / 2, color, GameMakeColor(0, 0, 0, 160));
		return w;
	}

	Bool roomComboOpen(GameWindow *combo)
	{
		GameWindow *list = combo ? GadgetComboBoxGetListBox(combo) : NULL;
		return list != NULL && !list->winIsHidden();
	}

	/// a combo at a size: its caret block 28 wide (narrower than the box, so a team's or a handicap's
	/// narrow box keeps room for its value), the field the rest
	void roomSizeCombo(GameWindow *combo, Int width, Int height)
	{
		const Int caret = min(height, roomPx(28));
		combo->winSetSize(width, height);
		if (GameWindow *button = GadgetComboBoxGetDropDownButton(combo))
		{
			button->winSetSize(caret, height);
			button->winSetPosition(width - caret, 0);
		}
		if (GameWindow *edit = GadgetComboBoxGetEditBox(combo))
		{
			edit->winSetSize(width - caret, height);
			edit->winSetPosition(0, 0);
		}
	}

	/// a window on a board rectangle, its parent's offset taken off
	void roomPlace(GameWindow *window, Int l, Int t, Int r, Int b)
	{
		if (window == NULL)
			return;
		const IRegion2D box = UIStyle::boardRect(l, t, r, b);
		Int parentX = 0, parentY = 0;
		if (GameWindow *parent = window->winGetParent())
			parent->winGetScreenPosition(&parentX, &parentY);
		Int x, y, w, h;
		/* An open combo is its box plus the list it dropped (placeOpenList grows the window, and one that
			 opened upward is moved up by the list, its field moved down), and HideListBox undoes exactly
			 that.  Moved or sized while open, it was left a list's height taller than its box, or a list's
			 height off its row once the list closed: an unseen window over the seats that took their
			 clicks.  Its field stands where the closed box does, so a combo whose field is already on
			 the rectangle keeps its list open; any other closes it first. */
		if (BitTest(window->winGetStyle(), GWS_COMBO_BOX) && roomComboOpen(window))
		{
			GameWindow *field = GadgetComboBoxGetEditBox(window);
			Int fx = 0, fy = 0, fw = 0, fh = 0;
			if (field)
			{
				field->winGetScreenPosition(&fx, &fy);
				field->winGetSize(&fw, &fh);
			}
			window->winGetSize(&w, &h);
			if (field == NULL || fx != box.lo.x || fy != box.lo.y || fh != box.hi.y - box.lo.y || w != box.hi.x - box.lo.x)
				GadgetComboBoxHideList(window);
			else
				return;
		}
		window->winGetPosition(&x, &y);
		if (x != box.lo.x - parentX || y != box.lo.y - parentY)
			window->winSetPosition(box.lo.x - parentX, box.lo.y - parentY);
		window->winGetSize(&w, &h);
		const Int width = box.hi.x - box.lo.x, height = box.hi.y - box.lo.y;
		if (w == width && h == height)
			return;
		if (BitTest(window->winGetStyle(), GWS_COMBO_BOX))
			roomSizeCombo(window, width, height);
		else
			window->winSetSize(width, height);
	}

	/// ponytail: a row out of the window is parked off the board rather than hidden, so the hidden
	/// states the room's own code keeps (a computer's personality, a map list over the seats) stay its own
	void roomPark(GameWindow *window)
	{
		if (window == NULL)
			return;
		if (BitTest(window->winGetStyle(), GWS_COMBO_BOX))
			GadgetComboBoxHideList(window);
		Int x, y;
		window->winGetPosition(&x, &y);
		if (x != -20000)
			window->winSetPosition(-20000, -20000);
	}

	void roomFont(GameWindow *window, Int px1080, Bool bold = FALSE)
	{
		if (window == NULL)
			return;
		GameFont *font = UIStyle::menuFont(px1080, bold);
		if (BitTest(window->winGetStyle(), GWS_COMBO_BOX))
		{
			// GadgetComboBoxSetFont leaves the combo's own font as the layout had it: ask its field
			GameWindow *edit = GadgetComboBoxGetEditBox(window);
			if (edit == NULL || edit->winGetFont() != font)
				GadgetComboBoxSetFont(window, font);
			return;
		}
		if (window->winGetFont() == font)
			return;
		if (BitTest(window->winGetStyle(), GWS_COMBO_BOX))
			GadgetComboBoxSetFont(window, font);
		else if (BitTest(window->winGetStyle(), GWS_ENTRY_FIELD))
			GadgetTextEntrySetFont(window, font);
		else if (BitTest(window->winGetStyle(), GWS_SCROLL_LISTBOX))
			GadgetListBoxSetFont(window, font);
		else if (BitTest(window->winGetStyle(), GWS_STATIC_TEXT))
			GadgetStaticTextSetFont(window, font);
		else
			window->winSetFont(font);
	}

	const GameSlot *roomSlot(Int seat)
	{
		return theRoomGame && seat >= 0 && seat < MAX_SLOTS ? theRoomGame->getConstSlot(seat) : NULL;
	}

	/// the row a seat's combo is on, and which of its boxes it is: 0 colour, 1 team, 2 player, -1 none
	Int roomFieldOf(GameWindow *combo, Int *seat)
	{
		*seat = -1;
		for (Int row = 0; row < theRoomRowCount; ++row)
		{
			const RoomRow &w = theRoomRows[row];
			const Int kind = combo == w.colour ? 0 : combo == w.team ? 1 : combo == w.player ? 2 : -1;
			if (kind < 0)
				continue;
			for (Int s = 0; s < MAX_SLOTS; ++s)
				if (theSeatRow[s] == row)
					*seat = s;
			return kind;
		}
		return -1;
	}

	/// a team's pennant: a pole and a flag, filled for a team (TEAM 1 the light one), hollow for none
	void roomPennant(Int x, Int y, Int size, Color color, Bool filled)
	{
		const Real line = (Real)max(1, size / 8);
		TheDisplay->drawLine(x, y, x, y + size, line, color);
		const Int flagW = size * 3 / 4, flagH = size / 2;
		if (filled)
			for (Int row = 0; row < flagH; ++row)
			{
				const Int reach = flagW * (flagH - abs(2 * row - flagH)) / flagH;
				TheDisplay->drawFillRect(x, y + row, max(1, reach), 1, color);
			}
		else
		{
			TheDisplay->drawLine(x, y, x + flagW, y + flagH / 2, line, color);
			TheDisplay->drawLine(x + flagW, y + flagH / 2, x, y + flagH, line, color);
		}
	}

	/// a combo's field, the room's way: its words in body type, a colour's swatch or a team's pennant
	/// before them, the local player's name in gold; nothing of EA's art
	void drawRoomField(GameWindow *window, WinInstanceData *)
	{
		EntryData *e = (EntryData *)window->winGetUserData();
		if (e == NULL || e->text == NULL)
			return;
		GameWindow *combo = window->winGetParent();
		Int x, y, w, h;
		window->winGetScreenPosition(&x, &y);
		window->winGetSize(&w, &h);
		Int seat = -1;
		const Int kind = combo ? roomFieldOf(combo, &seat) : -1;
		const GameSlot *slot = roomSlot(seat);
		Int left = x + roomPx(10);
		if (kind == 0 && slot && slot->isOccupied())
		{
			const Int side = roomPx(16);
			const Int top = y + (h - side) / 2;
			TheDisplay->drawFillRect(left, top, side, side, LobbySeatColor(slot));
			TheDisplay->drawOpenRect(left, top, side, side, 1.0f, GameMakeColor(0, 0, 0, 200));
			left += roomPx(26);
		}
		else if (kind == 1 && slot && slot->isOccupied() && slot->getTeamNumber() >= 0)
		{
			const Int side = roomPx(16);
			roomPennant(left + roomPx(2), y + (h - side) / 2, side, UIStyle::INK, slot->getTeamNumber() == 0);
			left += roomPx(26);
		}
		GameFont *font = window->winGetFont();
		if (e->text->getFont() != font)
			e->text->setFont(font);
		IRegion2D clip;
		clip.lo.x = x;
		clip.lo.y = y;
		clip.hi.x = x + w;
		clip.hi.y = y + h;
		e->text->setClipRegion(&clip);
		Int textW, textH;
		e->text->getSize(&textW, &textH);
		const Bool local = kind == 2 && theRoomGame && seat >= 0 && seat == theRoomGame->getLocalSlotNum();
		const Color ink = local ? UIStyle::GOLD : UIStyle::INK;
		if (kind != 2 || slot == NULL || !slot->isHuman() || theRoomKind == ROOM_SKIRMISH)
		{
			e->text->draw(left, y + (h - textH) / 2, ink, GameMakeColor(0, 0, 0, 160));
			return;
		}

		/* A network player's one line: his name, then at the field's right his rating (online) and the
			 seat's state (HOST, READY with its tick, NOT READY), the name cut with an ellipsis before them. */
		const Int key = TEXT_ROWS + TEXT_ROW_KEYS * (seat % MAX_SLOTS);
		const Int middle = y + h / 2;
		Int right = x + w - roomPx(10);
		const Bool ready = seat != 0 && slot->isAccepted();
		const char *state = seat == 0 ? "GUI:RoomHost" : ready ? "GUI:RoomReady" : "GUI:RoomNotReady";
		DisplayString *words = roomText(key + TEXT_ROW_STATE, TheGameText->fetch(state), UIStyle::menuFont(UIStyle::MENU_TEXT_LABEL, TRUE));
		Int sw, sh;
		words->getSize(&sw, &sh);
		right -= sw;
		words->draw(right, middle - sh / 2, ready ? UIStyle::ACTIVE : UIStyle::MUTED, GameMakeColor(0, 0, 0, 160));
		if (ready)
		{
			// the tick before READY
			const Int size = roomPx(10);
			const Int tx = right - roomPx(6) - size;
			const Real line = (Real)max(1, roomPx(2));
			TheDisplay->drawLine(tx, middle, tx + size / 3, middle + size / 3, line, UIStyle::ACTIVE);
			TheDisplay->drawLine(tx + size / 3, middle + size / 3, tx + size, middle - size / 2, line, UIStyle::ACTIVE);
			right = tx;
		}
		if (theRoomKind == ROOM_ONLINE)
		{
			AsciiString nick;
			nick.translate(slot->getName());
			OnlinePlayerInfo card;
			if (OnlineGetPlayerInfo(nick, card) && card.hasRating)
			{
				DisplayString *rating = roomText(key + TEXT_ROW_RATING, UIStyle::ladderText(card.shown(), card.hasElo),
					UIStyle::menuFont(UIStyle::MENU_TEXT_HINT));
				Int rw, rh;
				rating->getSize(&rw, &rh);
				right -= roomPx(10) + rw;
				rating->draw(right, middle - rh / 2, UIStyle::MUTED, GameMakeColor(0, 0, 0, 160));
			}
		}
		const UnicodeString name = UIStyle::fitName(e->text->getText(), font, max(0, right - roomPx(10) - left));
		DisplayString *fitted = roomText(key + TEXT_ROW_NAME, name, font);
		Int nw, nh;
		fitted->getSize(&nw, &nh);
		fitted->draw(left, middle - nh / 2, ink, GameMakeColor(0, 0, 0, 160));
	}

	/// the skirmish room's own name, a text entry: the field's box and the name in gold
	void drawRoomNameEntry(GameWindow *window, WinInstanceData *instData)
	{
		const Bool focused = window == TheWindowManager->winGetFocus();
		Int x, y, w, h;
		window->winGetScreenPosition(&x, &y);
		window->winGetSize(&w, &h);
		TheDisplay->drawFillRect(x, y, w, h, GameMakeColor(0, 0, 0, 120));
		TheDisplay->drawOpenRect(x, y, w, h, 1.0f, focused ? UIStyle::EDGE_LIT : UIStyle::FRAME);
		EntryData *e = (EntryData *)window->winGetUserData();
		if (e == NULL || e->text == NULL)
			return;
		if (e->text->getFont() != window->winGetFont())
			e->text->setFont(window->winGetFont());
		Int textW, textH;
		e->text->getSize(&textW, &textH);
		const Int left = x + roomPx(10);
		e->text->draw(left, y + (h - textH) / 2, UIStyle::GOLD, GameMakeColor(0, 0, 0, 160));
		// the caret while it is being typed in, blinking as EA's did
		if (focused && ((Clock_Milliseconds() / 400) & 1))
			TheDisplay->drawFillRect(left + e->text->getWidth() + roomPx(2), y + roomPx(8), max(1, roomPx(2)), h - 2 * roomPx(8), UIStyle::GOLD);
	}

	/// a combo nobody here may change is pinned (its value as plain words, no box and no caret); its
	/// field is the room's own, with a colour's swatch, a team's pennant and the local player's gold
	void styleRoomCombo(GameWindow *combo, Int px1080)
	{
		if (combo == NULL)
			return;
		UIStyle::setMenuComboBoxPinned(combo);
		if (GameWindow *edit = GadgetComboBoxGetEditBox(combo))
			edit->winSetDrawFunc(drawRoomField);
		roomFont(combo, px1080);
		// the room's narrow caret from the start, whatever the layout made it
		Int w, h;
		combo->winGetSize(&w, &h);
		roomSizeCombo(combo, w, h);
	}

	/// the skirmish room's PRESET field in the header: a combo's look over the presets key
	void drawRoomPresetField(GameWindow *window, WinInstanceData *instData)
	{
		Int x, y, w, h;
		window->winGetScreenPosition(&x, &y);
		window->winGetSize(&w, &h);
		const Bool enabled = BitTest(window->winGetStatus(), WIN_STATUS_ENABLED);
		const Bool hovered = enabled && BitTest(instData->getState(), WIN_STATE_HILITED);
		TheDisplay->drawFillRect(x, y, w, h, GameMakeColor(0, 0, 0, 120));
		TheDisplay->drawOpenRect(x, y, w, h, 1.0f, hovered ? UIStyle::EDGE_LIT : UIStyle::FRAME);
		IRegion2D caret;
		caret.lo.x = x + w - h;
		caret.lo.y = y;
		caret.hi.x = x + w;
		caret.hi.y = y + h;
		UIStyle::caretBlock(caret, hovered);
		DisplayString *words = instData->getTextDisplayString();
		if (words && words->getTextLength())
		{
			if (words->getFont() != UIStyle::menuFont(UIStyle::MENU_TEXT_BODY))
				words->setFont(UIStyle::menuFont(UIStyle::MENU_TEXT_BODY));
			Int tw, th;
			words->getSize(&tw, &th);
			words->draw(x + roomPx(10), y + (h - th) / 2, enabled ? UIStyle::INK : UIStyle::MUTED, GameMakeColor(0, 0, 0, 160));
		}
	}

	/// a record or presets box over the room: frame A on its glass
	void drawRoomBox(GameWindow *window, WinInstanceData *)
	{
		Int x, y, w, h;
		window->winGetScreenPosition(&x, &y);
		window->winGetSize(&w, &h);
		IRegion2D box;
		box.lo.x = x;
		box.lo.y = y;
		box.hi.x = x + w;
		box.hi.y = y + h;
		UIStyle::glass(box, 220);
		UIStyle::frameA(box);
	}

	/// RULES and CHAT: the open one is the disabled one (showLobbySettings), drawn lit; CHAT closed
	/// counts the lines it has not shown
	void drawRoomTab(GameWindow *window, WinInstanceData *instData)
	{
		Int x, y, w, h;
		window->winGetScreenPosition(&x, &y);
		window->winGetSize(&w, &h);
		IRegion2D box;
		box.lo.x = x;
		box.lo.y = y;
		box.hi.x = x + w;
		box.hi.y = y + h;
		const Bool open = !BitTest(window->winGetStatus(), WIN_STATUS_ENABLED);
		DisplayString *label = instData->getTextDisplayString();
		// the chat's own tab while the chat is closed counts the lines that came since it was open
		Int unread = 0;
		if (theRoomChat && open != theRoomChat->winIsHidden() && label && !open)
			unread = max(0, GadgetListBoxGetNumEntries(theRoomChat) - theRoomChatSeen);
		if (theRoomChat && !theRoomChat->winIsHidden())
			theRoomChatSeen = GadgetListBoxGetNumEntries(theRoomChat);
		UIStyle::menuTab(box, label, open, !open && BitTest(instData->getState(), WIN_STATE_HILITED), unread);
	}

	void drawRoomChevrons(Int x, Int middle, Bool up)
	{
		const Int size = roomPx(8);
		const Int cx = roomX(x), cy = roomY(middle);
		const Real line = (Real)max(1, roomPx(2));
		for (Int i = 0; i < 2; ++i)
		{
			const Int left = cx + i * (size + roomPx(3));
			const Int tip = up ? cy - size / 2 : cy + size / 2;
			const Int base = up ? cy + size / 2 : cy - size / 2;
			TheDisplay->drawLine(left, base, left + size / 2, tip, line, UIStyle::EDGE_LIT);
			TheDisplay->drawLine(left + size / 2, tip, left + size, base, line, UIStyle::EDGE_LIT);
		}
	}

	UnicodeString roomCountText(const char *key, Int count)
	{
		UnicodeString words;
		words.format(TheGameText->fetch(key).str(), count);
		return words;
	}

	/// a group's head: the pennant, TEAM n or the no-team words, the seat count, a faint rule
	void drawRoomHead(Int index, const LobbyRoomItem &item, Int top)
	{
		const Int middle = top + item.height / 2 + 2;
		const Int pennant = roomPx(16);
		if (item.group >= 0)
			roomPennant(roomX(ROOM_LEFT + 10), roomY(middle) - pennant / 2, pennant, UIStyle::INK, item.group == 0);
		UnicodeString name;
		if (item.group >= 0)
			name = roomCountText("GUI:RoomTeam", item.group + 1);
		else
			name = TheGameText->fetch(item.group == LOBBY_GROUP_ALONE ? "GUI:RoomNoTeam" : "GUI:RoomNoTeamYet");
		GameFont *label = UIStyle::menuFont(UIStyle::MENU_TEXT_LABEL, TRUE);
		const Int nameX = item.group >= 0 ? ROOM_LEFT + 34 : ROOM_LEFT + 8;
		const Int nameW = roomDraw(TEXT_GROUPS + 2 * index, name, label, nameX, middle, UIStyle::INK);
		const UnicodeString seats = roomCountText(item.count == 1 ? "GUI:RoomSeat" : "GUI:RoomSeats", item.count);
		const Int seatsX = roomX(nameX) + nameW + roomPx(14);
		DisplayString *s = roomText(TEXT_GROUPS + 2 * index + 1, seats, label);
		Int w, h;
		s->getSize(&w, &h);
		s->draw(seatsX, roomY(middle) - h / 2, UIStyle::MUTED, GameMakeColor(0, 0, 0, 160));
		const Int ruleX = seatsX + w + roomPx(16);
		TheDisplay->drawFillRect(ruleX, roomY(middle), max(0, roomX(ROOM_CELLS_RIGHT) - ruleX), 1, UIStyle::withAlpha(UIStyle::FRAME, 110));
	}

	/* A seat's row, one line of boxes whatever sits there, its gaps on the 8 px grid down the whole list:
		 every box 40 tall from +8 (16 to the next row's, 48 across a team's head), all in body type; a
		 computer's level and its personality side by side in the player cell; the portrait at the row's
		 left, the online ping light in its corner.  Board pixels at 1080. */
	const Int ROW_BOX_TOP = 8, ROW_BOX_HEIGHT = 40;
	const Int ROW_PORTRAIT_LEFT = 96, ROW_PORTRAIT_SIZE = 44;
	const Int ROW_PING_SIZE = 18;
	const Int ROOM_PLAYER_LEFT = 152, ROOM_PLAYER_RIGHT = 444;
	const Int ROOM_LEVEL_RIGHT = 292;	///< a computer's level; its personality from 8 after it to the cell's right

	/// a seat's row: its wash, the slot's number, the army's portrait or an open seat's empty frame
	void drawRoomSeat(Int index, const LobbyRoomItem &item, Int top)
	{
		const GameSlot *slot = roomSlot(item.slot);
		if (slot == NULL)
			return;
		const Bool network = theRoomKind != ROOM_SKIRMISH;
		const Bool local = item.slot == theRoomGame->getLocalSlotNum();
		const Bool ready = network && slot->isHuman() && item.slot != 0 && slot->isAccepted();
		const IRegion2D row = UIStyle::boardRect(ROOM_LEFT, top + 3, ROOM_SEAT_COLUMN_RIGHT - 24, top + item.height - 3);
		if (ready)
			TheDisplay->drawFillRect(row.lo.x, row.lo.y, row.hi.x - row.lo.x, row.hi.y - row.lo.y, GameMakeColor(22, 74, 40, 150));
		const Bool moved = item.slot == theRoomMoved && Clock_Milliseconds() < theRoomMovedUntil;
		UIStyle::menuRow(row, local ? UIStyle::ROW_LOCAL : moved ? UIStyle::ROW_HOVER : UIStyle::ROW_PLAIN, index % 2 == 1);

		const Int middle = top + item.height / 2;
		UnicodeString number;
		number.format(u"%d", item.slot + 1);
		const Int key = TEXT_ROWS + TEXT_ROW_KEYS * (item.slot % MAX_SLOTS);
		roomDraw(key + TEXT_ROW_NUMBER, number, UIStyle::menuFont(UIStyle::MENU_TEXT_BODY), ROOM_LEFT + 17, middle, UIStyle::MUTED, 1);

		// the portrait, 44 square and on the row's middle: the army's picture with the seat's colour under
		// it, a "?" for a random army, an empty dashed frame for an open or closed seat
		const IRegion2D picture = UIStyle::boardRect(ROW_PORTRAIT_LEFT, middle - ROW_PORTRAIT_SIZE / 2,
			ROW_PORTRAIT_LEFT + ROW_PORTRAIT_SIZE, middle + ROW_PORTRAIT_SIZE / 2);
		const Int pw = picture.hi.x - picture.lo.x, ph = picture.hi.y - picture.lo.y;
		if (!slot->isOccupied())
		{
			const Color dash = UIStyle::withAlpha(UIStyle::FRAME, slot->isOpen() ? 200 : 90);
			const Int on = max(2, roomPx(4)), period = on + max(2, roomPx(4));
			for (Int d = 0; d < pw; d += period)
			{
				TheDisplay->drawFillRect(picture.lo.x + d, picture.lo.y, min(on, pw - d), 1, dash);
				TheDisplay->drawFillRect(picture.lo.x + d, picture.hi.y - 1, min(on, pw - d), 1, dash);
			}
			for (Int d = 0; d < ph; d += period)
			{
				TheDisplay->drawFillRect(picture.lo.x, picture.lo.y + d, 1, min(on, ph - d), dash);
				TheDisplay->drawFillRect(picture.hi.x - 1, picture.lo.y + d, 1, min(on, ph - d), dash);
			}
			return;
		}
		const Int pick = slot->getPlayerTemplate();
		const PlayerTemplate *army = pick >= 0 && pick < ThePlayerTemplateStore->getPlayerTemplateCount()
			? ThePlayerTemplateStore->getNthPlayerTemplate(pick) : NULL;
		const Image *portrait = army ? UIStyle::armyPortrait(army) : NULL;
		TheDisplay->drawFillRect(picture.lo.x, picture.lo.y, pw, ph, GameMakeColor(0, 0, 0, 170));
		if (portrait)
			UIStyle::coverImage(portrait, picture.lo.x, picture.lo.y, pw, ph);
		else
		{
			TheDisplay->drawOpenRect(picture.lo.x, picture.lo.y, pw, ph, 1.0f, UIStyle::FRAME);
			roomDraw(key + TEXT_ROW_UNKNOWN, UnicodeString(u"?"), UIStyle::menuFont(UIStyle::MENU_TEXT_ROW_FIGURE),
				ROW_PORTRAIT_LEFT + ROW_PORTRAIT_SIZE / 2, middle - 1, UIStyle::INK, 1);
		}
		const Int stripe = max(2, roomPx(3));
		TheDisplay->drawFillRect(picture.lo.x, picture.hi.y - stripe, pw, stripe, LobbySeatColor(slot));
		// the online ping light sits in the portrait's top right corner on a dark square of its own
		if (theRoomKind == ROOM_ONLINE && slot->isHuman())
			roomFill(ROW_PORTRAIT_LEFT + ROW_PORTRAIT_SIZE - ROW_PING_SIZE - 2, middle - ROW_PORTRAIT_SIZE / 2,
				ROW_PORTRAIT_LEFT + ROW_PORTRAIT_SIZE, middle - ROW_PORTRAIT_SIZE / 2 + ROW_PING_SIZE + 2, GameMakeColor(0, 0, 0, 200));
		// a network player's rating and state stand in his name's box (drawRoomField)
	}

	// ---- the Apocalypse room's own parts (README-apocalypse-room.md) ----

	GameWindow *roomWindow(const char *layoutFilename, const char *name);

	Bool roomApocalypse(void)
	{
		return theRoomGame != NULL && IsApocalypseMap(theRoomGame->getMap());
	}

	/// the seats an Apocalypse room lists: the player alone in a skirmish, four in a network room
	Int apocRoomSeats(void)
	{
		return theRoomKind == ROOM_SKIRMISH ? (Int)APOCALYPSE_SKIRMISH_SEATS : (Int)APOCALYPSE_SEATS;
	}

	/// words from a screen x, centred on board y; their width on screen
	Int roomDrawAt(Int key, const UnicodeString &text, GameFont *font, Int screenX, Int middle, Color color)
	{
		DisplayString *s = roomText(key, text, font);
		if (s == NULL || text.isEmpty())
			return 0;
		Int w, h;
		s->getSize(&w, &h);
		s->draw(screenX, roomY(middle) - h / 2, color, GameMakeColor(0, 0, 0, 160));
		return w;
	}

	Int roomTextWidth(Int key, const UnicodeString &text, GameFont *font)
	{
		DisplayString *s = roomText(key, text, font);
		Int w = 0, h = 0;
		if (s && !text.isEmpty())
			s->getSize(&w, &h);
		return w;
	}

	void roomFrame(Int l, Int t, Int r, Int b, Color color)
	{
		const IRegion2D box = UIStyle::boardRect(l, t, r, b);
		TheDisplay->drawOpenRect(box.lo.x, box.lo.y, max(1, box.hi.x - box.lo.x), max(1, box.hi.y - box.lo.y), 1.0f, color);
	}

	/// the seats the waves size for: humans and computers, the watchers left out
	Int apocSeated(void)
	{
		Int seated = 0;
		for (Int i = 0; i < MAX_SLOTS; ++i)
		{
			const GameSlot *slot = theRoomGame->getConstSlot(i);
			if (slot && slot->isOccupied() && slot->getPlayerTemplate() != PLAYERTEMPLATE_OBSERVER)
				++seated;
		}
		return seated;
	}

	/// game seconds as the room's speed plays them on the wall, m:ss
	UnicodeString apocClock(Int gameSeconds)
	{
		Int speed = theRoomGame->getGameSpeedPercent();
		if (speed <= 0)
			speed = theRoomGame->getDefaultGameSpeedPercent();
		const Int wall = ApocalypseWallSeconds(gameSeconds, speed);
		UnicodeString clock;
		clock.format(u"%d:%02d", wall / 60, wall % 60);
		return clock;
	}

	/// a roster row's name as the game's strings give it, its template's name without one
	UnicodeString apocKindName(Int kind)
	{
		const ApocalypseKind &row = Apocalypse_kind(kind);
		const ThingTemplate *thing = TheThingFactory ? TheThingFactory->findTemplate(AsciiString(row.name), FALSE) : NULL;
		if (thing && !thing->getDisplayName().isEmpty())
			return thing->getDisplayName();
		UnicodeString name;
		name.translate(AsciiString(row.name));
		return name;
	}

	/// capitals tracked as the designs set them (14 px labels at 1.2), from a screen x, centred on board y; their width
	Int apocCaps(const UnicodeString &text, Int screenX, Int middle, Color color, Int px1080 = UIStyle::MENU_TEXT_LABEL,
		Real tracking = UIStyle::MENU_LABEL_TRACKING)
	{
		if (text.isEmpty())
			return 0;
		GameFont *font = UIStyle::menuFont(px1080, TRUE);
		const Int h = TheWindowManager->winFontHeight(font);
		return UIStyle::drawTracked(text, font, screenX, roomY(middle) - h / 2, color, tracking);
	}

	Int apocCapsWidth(const UnicodeString &text, Int px1080 = UIStyle::MENU_TEXT_LABEL, Real tracking = UIStyle::MENU_LABEL_TRACKING)
	{
		return text.isEmpty() ? 0 : UIStyle::trackedWidth(text, UIStyle::menuFont(px1080, TRUE), tracking);
	}

	/// a section's mark: the short lit bar before its head
	void apocMark(Int middle, Int height)
	{
		roomFill(ROOM_LEFT + 6, middle - height / 2, ROOM_LEFT + 9, middle + height / 2, UIStyle::EDGE);
	}

	/// SURVIVORS n / 4, then what the seats change, right-aligned on the rows' edge
	void drawApocalypseHead(Int top)
	{
		const Int middle = top + LOBBY_ROW_HEAD / 2 + 2;
		const Int key = TEXT_APOCALYPSE + APOC_HEAD;
		GameFont *note = UIStyle::menuFont(UIStyle::MENU_TEXT_HINT);
		const Int seated = apocSeated();
		apocMark(middle, 16);
		const Int nameW = apocCaps(TheGameText->fetch("GUI:ApocalypseSurvivors"), roomX(ROOM_LEFT + 24), middle, UIStyle::INK);
		UnicodeString count;
		count.format(u"%d / %d", seated, apocRoomSeats());
		const Int countX = roomX(ROOM_LEFT + 24) + nameW + roomPx(12);
		const Int countW = roomDrawAt(key + 1, count, UIStyle::menuFont(UIStyle::MENU_TEXT_BODY), countX, middle, UIStyle::INK);
		const Int health = Apocalypse_bossHealthPercent(max(1, seated));
		UnicodeString times;
		times.format(u"\x00D7%d.%d", health / 100, (health % 100) / 10);
		const UnicodeString words[3] = { TheGameText->fetch("GUI:ApocalypseAllied"), TheGameText->fetch("GUI:ApocalypseWaves"),
			TheGameText->fetch("GUI:ApocalypseBossHealth") };
		const UnicodeString notes[3] = { TheGameText->fetch("GUI:ApocalypseAlliedNote"),
			roomCountText("GUI:ApocalypseWavesNote", max(1, seated)), times };
		Int x = roomX(APOC_RIGHT);
		for (Int f = 2; f >= 0; --f)
		{
			x -= roomTextWidth(key + 3 + 2 * f, notes[f], note);
			roomDrawAt(key + 3 + 2 * f, notes[f], note, x, middle, UIStyle::INK);
			x -= roomPx(8) + apocCapsWidth(words[f]);
			apocCaps(words[f], x, middle, UIStyle::MUTED);
			x -= roomPx(26);
		}
		const Int ruleX = countX + countW + roomPx(16);
		if (x > ruleX)
			TheDisplay->drawFillRect(ruleX, roomY(middle), x - ruleX, 1, UIStyle::withAlpha(UIStyle::FRAME, 110));
	}

	/// THE NIGHTS: one day to scale, then nights 1 to 9 and the twelfth on, each with its clock, the
	/// kinds it adds and its boss; the skirmish room, with no chat, names every kind
	void drawApocalypseNights(Bool named)
	{
		const Int key = TEXT_APOCALYPSE;
		GameFont *hint = UIStyle::menuFont(UIStyle::MENU_TEXT_HINT);
		GameFont *figure = UIStyle::menuFont(UIStyle::MENU_TEXT_ROW_FIGURE);
		const Int span = APOC_RIGHT - 2 - APOC_CELL_LEFT;

		// the head, the speed every clock here is read at, the Blood Moon's key at the right
		const Int headMiddle = APOC_NIGHTS_TOP + 15;
		apocMark(headMiddle, 22);
		const Int headW = apocCaps(TheGameText->fetch("GUI:ApocalypseNights"), roomX(ROOM_LEFT + 16), headMiddle, UIStyle::INK, 20, 2.4f);
		Int speed = theRoomGame->getGameSpeedPercent();
		if (speed <= 0)
			speed = theRoomGame->getDefaultGameSpeedPercent();
		UnicodeString clockAt;
		clockAt.format(TheGameText->fetch("GUI:ApocalypseClockAt").str(), GameSpeedText(speed).str());
		roomDrawAt(key + APOC_NIGHTS + 1, clockAt, hint, roomX(ROOM_LEFT + 16) + headW + roomPx(14), headMiddle, UIStyle::MUTED);
		Int x = roomX(APOC_RIGHT);
		const UnicodeString moonNote = TheGameText->fetch("GUI:ApocalypseBloodMoonNote");
		const UnicodeString moon = TheGameText->fetch("GUI:ApocalypseBloodMoon");
		x -= roomTextWidth(key + APOC_NIGHTS + 3, moonNote, hint);
		roomDrawAt(key + APOC_NIGHTS + 3, moonNote, hint, x, headMiddle, UIStyle::MUTED);
		x -= roomPx(10) + apocCapsWidth(moon);
		apocCaps(moon, x, headMiddle, APOC_BLOOD_TEXT);
		const Int dash = roomPx(16);
		x -= roomPx(12) + dash;
		TheDisplay->drawFillRect(x, roomY(headMiddle), dash, max(1, roomPx(2)), APOC_BLOOD_TEXT);

		// one day to scale, the first wave's tick on it
		const Int dayMiddle = (APOC_DAY_TOP + APOC_DAY_BOTTOM) / 2;
		apocCaps(TheGameText->fetch("GUI:ApocalypseOneDay"), roomX(ROOM_LEFT), dayMiddle, UIStyle::MUTED);
		static const Int seconds[APOCALYPSE_PHASES] = { APOCALYPSE_DAY_SECONDS, APOCALYPSE_DUSK_SECONDS, APOCALYPSE_NIGHT_SECONDS, APOCALYPSE_DAWN_SECONDS };
		static const char *const phases[APOCALYPSE_PHASES] = { "GUI:ApocalypseDay", "GUI:ApocalypseDusk", "GUI:ApocalypseNight", "GUI:ApocalypseDawn" };
		const Color fills[APOCALYPSE_PHASES] = { GameMakeColor(96, 84, 44, 190), GameMakeColor(112, 52, 34, 200),
			GameMakeColor(22, 28, 64, 220), GameMakeColor(64, 70, 86, 190) };
		const Int cycle = seconds[0] + seconds[1] + seconds[2] + seconds[3];
		Int at = 0;
		for (Int p = 0; p < APOCALYPSE_PHASES; ++p)
		{
			const Int l = APOC_CELL_LEFT + span * at / cycle;
			at += seconds[p];
			const Int r = APOC_CELL_LEFT + span * at / cycle;
			roomFill(l, APOC_DAY_TOP, r, APOC_DAY_BOTTOM, fills[p]);
			apocCaps(TheGameText->fetch(phases[p]), roomX(l + 8), APOC_DAY_TOP + 11, UIStyle::INK);
			roomDraw(key + APOC_DAY + 5 + p, apocClock(seconds[p]), hint, l + 8, APOC_DAY_TOP + 29, UIStyle::INK);
		}
		const Int tick = APOC_CELL_LEFT + span * APOCALYPSE_FIRST_WAVE_SECONDS / cycle;
		roomFill(tick, APOC_DAY_TOP - 2, tick + 2, APOC_DAY_BOTTOM + 2, UIStyle::INK);
		apocCaps(TheGameText->fetch("GUI:ApocalypseFirstWave"), roomX(tick + 8), APOC_DAY_TOP + 11, UIStyle::INK);
		roomDraw(key + APOC_DAY + 10, apocClock(APOCALYPSE_FIRST_WAVE_SECONDS), hint, tick + 8, APOC_DAY_TOP + 29, UIStyle::INK);

		// the table: a row's word at the left, a night a cell; the skirmish room, with no chat, names
		// every kind in a column under its night (README-apocalypse-room.md)
		const Int nightMiddle = APOC_TABLE_TOP + (named ? 17 : 15);
		const Int fallsMiddle = APOC_TABLE_TOP + (named ? 50 : 42);
		const Int deadMiddle = APOC_TABLE_TOP + (named ? 110 : 66);
		const Int bossMiddle = APOC_TABLE_TOP + (named ? 78 : 90);
		const Int tableTop = APOC_TABLE_TOP - 4;
		const Int tableBottom = named ? LOBBY_SEATS_BOTTOM : APOC_TABLE_TOP + 104;
		static const char *const rows[4] = { "GUI:ApocalypseNight", "GUI:ApocalypseFallsAt", "GUI:ApocalypseNewDead", "GUI:ApocalypseBoss" };
		const Int rowMiddles[4] = { nightMiddle, fallsMiddle, deadMiddle, bossMiddle };
		for (Int r = 0; r < 4; ++r)
			apocCaps(TheGameText->fetch(rows[r]), roomX(ROOM_LEFT), rowMiddles[r], UIStyle::MUTED);
		Int walker[1] = { APOCALYPSE_KIND_WALKER };
		if (named && ApocalypseNewKindsOfNight(0, walker, 1) > 0)
		{
			// day one sends the walker alone, under the row's word
			apocCaps(TheGameText->fetch("GUI:ApocalypseDayOne"), roomX(ROOM_LEFT), deadMiddle + 27, UIStyle::MUTED);
			roomDraw(key + APOC_LABELS + 5, UIStyle::fitName(apocKindName(walker[0]), hint, roomPx(APOC_CELL_LEFT - ROOM_LEFT - 8)), hint,
				ROOM_LEFT, deadMiddle + 48, UIStyle::INK);
		}
		const UnicodeString none(u"\x2013");
		for (Int i = 0; i < APOC_NIGHT_CELLS; ++i)
		{
			const Bool tail = i == APOC_NIGHT_CELLS - 1;
			const Int night = tail ? (Int)APOCALYPSE_BOSS_PAIR_NIGHT : i + 1;
			const Int l = APOC_CELL_LEFT + span * i / APOC_NIGHT_CELLS;
			const Int r = APOC_CELL_LEFT + span * (i + 1) / APOC_NIGHT_CELLS;
			const Int textX = l + 10;
			const Int cellW = roomPx(r - l - 14);
			if (night % APOCALYPSE_BLOOD_MOON_EVERY == 0)
			{
				roomFill(l + 1, tableTop, r - 1, tableBottom, UIStyle::withAlpha(APOC_BLOOD, 44));
				roomFill(l + 1, tableTop, r - 1, tableTop + 2, APOC_BLOOD);
			}
			if (i > 0)
				roomFill(l, tableTop, l + 1, tableBottom, UIStyle::withAlpha(UIStyle::FRAME, 90));
			UnicodeString number;
			if (tail)
				number.format(u"%d, %d\x2026", night, night + APOCALYPSE_BLOOD_MOON_EVERY);	// every Blood Moon from the pair on
			else
				number.format(u"%d", night);
			roomDraw(key + APOC_CELLS + i, number, figure, textX, nightMiddle, UIStyle::INK);
			roomDraw(key + APOC_CELLS + 10 + i, apocClock(ApocalypseNightFallsAt(night)), hint, textX, fallsMiddle, UIStyle::INK);
			Int kinds[8];
			const Int count = ApocalypseNewKindsOfNight(night, kinds, (Int)ARRAY_SIZE(kinds));
			if (count == 0)
				roomDraw(key + APOC_CELLS + 20 + i, none, hint, textX, deadMiddle, UIStyle::MUTED);
			else if (named)
			{
				for (Int k = 0; k < min(count, 6); ++k)
					roomDraw(key + APOC_NAMES + 6 * i + k, UIStyle::fitName(apocKindName(kinds[k]), hint, cellW), hint, textX,
						deadMiddle + 26 * k, UIStyle::INK);
			}
			else
				roomDraw(key + APOC_CELLS + 20 + i, count == 1 ? TheGameText->fetch("GUI:ApocalypseKind") : roomCountText("GUI:ApocalypseKinds", count),
					hint, textX, deadMiddle, UIStyle::INK);
			Int bosses[2];
			const Int bossCount = Apocalypse_bossesOfNight((UnsignedInt)night, 0, bosses);
			if (bossCount > 0)
				roomDraw(key + APOC_CELLS + 30 + i, bossCount > 1 ? TheGameText->fetch("GUI:ApocalypseBossPair")
					: UIStyle::fitName(apocKindName(bosses[0]), hint, cellW), hint, textX, bossMiddle, APOC_BLOOD_TEXT);
		}
	}

	/// the generated map's facts beside its preview, and what its marks mean
	void drawApocalypseMapFacts(const MapMetaData *md)
	{
		const Int key = TEXT_APOCALYPSE + APOC_MAP;
		GameFont *hint = UIStyle::menuFont(UIStyle::MENU_TEXT_HINT);
		const Int left = ROOM_COLUMN_LEFT + 348;
		const Int starts = md ? md->m_numPlayers : apocSeated();
		UnicodeString facts = TheGameText->fetch("GUI:ApocalypseRandom");
		facts.concat(UIStyle::HEADER_JOINER);
		facts.concat(starts == 1 ? TheGameText->fetch("GUI:ApocalypseStart") : roomCountText("GUI:ApocalypseStarts", starts));
		roomDraw(key, facts, hint, left, 201, UIStyle::MUTED);
		roomDraw(key + 1, TheGameText->fetch("GUI:ApocalypseNewEveryMatch"), hint, left, 223, UIStyle::MUTED);
		// the key: a start in its seat's ring, the basin's floor, the wilderness, the fog's red edge
		static const char *const lines[4] = { "GUI:ApocalypseKeyStart", "GUI:ApocalypseKeyBasin", "GUI:ApocalypseKeyWild", "GUI:ApocalypseKeyFog" };
		const Color marks[4] = { GameMakeColor(40, 150, 64, 255), GameMakeColor(112, 104, 96, 255), GameMakeColor(206, 186, 146, 255), APOC_BLOOD };
		const Int mark = roomPx(16);
		for (Int i = 0; i < 4; ++i)
		{
			const Int middle = 274 + 32 * i;
			const Int mx = roomX(left + 2), my = roomY(middle) - mark / 2;
			TheDisplay->drawFillRect(mx, my, mark, mark, marks[i]);
			TheDisplay->drawOpenRect(mx, my, mark, mark, 1.0f, GameMakeColor(0, 0, 0, 200));
			roomDraw(key + 2 + i, UIStyle::fitKey(lines[i], hint, roomPx(ROOM_RIGHT - left - 28)), hint, left + 28, middle, UIStyle::INK);
		}
	}

	/// what the mode pins, read only: eight pairs, two to a line
	void drawApocalypseModeSet(void)
	{
		const Int key = TEXT_APOCALYPSE + APOC_MODE;
		GameFont *hint = UIStyle::menuFont(UIStyle::MENU_TEXT_HINT);
		apocCaps(TheGameText->fetch("GUI:ApocalypseModeSet"), roomX(ROOM_COLUMN_LEFT + 2), APOC_MODE_TOP + 11, UIStyle::MUTED);
		roomFill(ROOM_COLUMN_LEFT, APOC_MODE_TOP + 26, ROOM_RIGHT, APOC_MODE_TOP + 27, UIStyle::withAlpha(UIStyle::FRAME, 150));
		static const char *const names[8] = { "GUI:ApocalypseDifficulty", "GUI:ApocalypseTeams", "GUI:DayNight", "GUI:PeaceTimeLabel",
			"GUI:UnitLimit", "GUI:SupplyPileLimit", "GUI:TechRespawn", "GUI:ProRules" };
		static const char *const values[8] = { "GUI:ApocalypseAdapts", "GUI:ApocalypseAllAllied", "GUI:ApocalypseItsOwn", "GUI:ApocalypseNone",
			"GUI:ApocalypseOff", "GUI:SupplyPileLimitOff", "GUI:TechRespawnOff", "GUI:ApocalypseOff" };
		// two columns 259 wide with a gap of 24: the word at a column's left, its value against its right
		const Int columnW = 259;
		for (Int i = 0; i < 8; ++i)
		{
			const Int l = ROOM_COLUMN_LEFT + (i % 2) * (columnW + 24);
			const Int middle = APOC_MODE_TOP + 47 + 28 * (i / 2);
			const UnicodeString value = UIStyle::fitKey(values[i], hint, roomPx(columnW / 2));
			const Int valueW = roomDraw(key + 2 + 2 * i, value, hint, l + columnW, middle, UIStyle::INK, 2);
			roomDraw(key + 1 + 2 * i, UIStyle::fitKey(names[i], hint, roomPx(columnW) - valueW - roomPx(12)), hint, l, middle, UIStyle::MUTED);
		}
	}

	/// the preview with the fog ring's deadly red round its edge
	void drawRoomMap(GameWindow *window, WinInstanceData *instData)
	{
		if (theRoomMapDraw)
			theRoomMapDraw(window, instData);
		if (!theRoomClassic || !roomApocalypse())
			return;
		Int x, y, w, h;
		window->winGetScreenPosition(&x, &y);
		window->winGetSize(&w, &h);
		const Int band = max(4, roomPx(16));
		const Int step = max(1, band / 4);
		for (Int i = 0; i < 4; ++i)
		{
			const Color red = UIStyle::withAlpha(APOC_BLOOD, 150 - 35 * i);
			const Int in = i * step;
			TheDisplay->drawFillRect(x + in, y + in, max(1, w - 2 * in), step, red);
			TheDisplay->drawFillRect(x + in, y + h - in - step, max(1, w - 2 * in), step, red);
			TheDisplay->drawFillRect(x + in, y + in + step, step, max(1, h - 2 * in - 2 * step), red);
			TheDisplay->drawFillRect(x + w - in - step, y + in + step, step, max(1, h - 2 * in - 2 * step), red);
		}
	}

	/// the windows where the Apocalypse room's design puts them: the rules the mode pins and the map's
	/// choice parked (SET BY THE MODE says them), the four the host sets from the RULES head down, the
	/// chat always open under the nights in a network room, no tabs
	void placeApocalypseRoom(void)
	{
		if (theRoomLayout == NULL)
			return;
		static const char *const pinned[] = { "LabelPeaceTime", "ComboBoxPeaceTime", "LabelSupplyPileLimit", "ComboBoxSupplyPileLimit",
			"LabelTechRespawn", "ComboBoxTechRespawn", "CheckBoxUnitLimit", "CheckBoxProRules", "CheckBoxDayNight", "TabLobbySettings", "TabChat" };
		for (Int i = 0; i < (Int)ARRAY_SIZE(pinned); ++i)
			roomPark(roomWindow(theRoomLayout, pinned[i]));
		roomPark(theRoomSelectMap);
		roomPark(theRoomPresets);
		/* The preview square at the column's left, the mode's name and the map's facts beside it.  The
			 generated basin is square, while the room's map fit reads the shape of the shipped map the
			 generator borrows its look from (a wide one stretched the basin to twice its width). */
		roomPlace(theRoomMap, ROOM_COLUMN_LEFT, LOBBY_SEATS_TOP, ROOM_COLUMN_LEFT + 324, LOBBY_SEATS_TOP + 324);
		roomPlace(theRoomMapName, ROOM_COLUMN_LEFT + 348, LOBBY_SEATS_TOP, ROOM_RIGHT, LOBBY_SEATS_TOP + 30);
		const UnicodeString mode = TheGameText->fetch("GUI:ApocalypseLower");
		if (theRoomMapName && GadgetStaticTextGetText(theRoomMapName).compare(mode) != 0)
			GadgetStaticTextSetText(theRoomMapName, mode);

		static const char *const rules[4][2] = { { "LabelGameSpeed", "ComboBoxGameSpeed" }, { "StartingCashLabel", "ComboBoxStartingCash" },
			{ "LabelSuperweapons", "ComboBoxSuperweapons" }, { "LabelIncomeSharing", "ComboBoxIncomeSharing" } };
		// the online room's two rules it pins itself (Record Stats, Original Armies) keep a line at the foot
		GameWindow *checks[2] = { roomWindow(theRoomLayout, "CheckBoxUseStats"), roomWindow(theRoomLayout, "CheckBoxLimitArmies") };
		const Int checksTop = APOC_MODE_TOP - 14 - 34;
		const Int pitch = checks[0] || checks[1] ? min(60, (checksTop - 4 - APOC_RULES_TOP) / 4) : 60;
		Bool moved = FALSE;
		for (Int r = 0; r < 4; ++r)
		{
			const Int top = APOC_RULES_TOP + r * pitch;
			GameWindow *combo = roomWindow(theRoomLayout, rules[r][1]);
			Int was = 0, wasY = 0;
			if (combo)
				combo->winGetScreenPosition(&was, &wasY);
			roomPlace(roomWindow(theRoomLayout, rules[r][0]), ROOM_COLUMN_LEFT, top, ROOM_COLUMN_LEFT + 160, top + 44);
			roomPlace(combo, ROOM_COLUMN_LEFT + 160, top, ROOM_RIGHT, top + 44);
			Int now = 0, nowY = 0;
			if (combo)
				combo->winGetScreenPosition(&now, &nowY);
			moved = moved || nowY != wasY;
		}
		const Int checkW = (ROOM_RIGHT - ROOM_COLUMN_LEFT - 16) / 3;
		for (Int c = 0; c < 2; ++c)
			roomPlace(checks[c], ROOM_COLUMN_LEFT + c * (checkW + 8), checksTop, ROOM_COLUMN_LEFT + c * (checkW + 8) + checkW, checksTop + 34);
		// the rules page ends over SET BY THE MODE, which the room draws under it and the page's glass dimmed
		GameWindow *page = roomWindow(theRoomLayout, "PageLobbySettings");
		if (page)
		{
			Int was = 0, wasH = 0;
			page->winGetSize(&was, &wasH);
			roomPlace(page, ROOM_COLUMN_LEFT, APOC_RULES_TOP, ROOM_RIGHT, APOC_MODE_TOP - 14);
			Int now = 0, nowH = 0;
			page->winGetSize(&now, &nowH);
			moved = moved || nowH != wasH;
			if (page->winIsHidden())
				page->winHide(FALSE);
		}
		if (moved && theRoomParent)
			SnapLobbyRulesLists(theRoomParent, theRoomLayout);	// the lists end on the rows where they now stand

		if (theRoomChat == NULL)
			return;
		// the chat box 60,784 to 1270,952: its lines, then the line to write in and SEND 8 in from its edges
		const Int entryTop = LOBBY_SEATS_BOTTOM - 8 - 38;
		roomPlace(theRoomChat, ROOM_LEFT + 8, APOC_CHAT_TOP + 6, APOC_RIGHT - 8, entryTop - 8);
		GameWindow *entry = roomWindow(theRoomLayout, "TextEntryChat");
		GameWindow *send = roomWindow(theRoomLayout, "ButtonEmote");
		roomPlace(entry, ROOM_LEFT + 8, entryTop, APOC_RIGHT - 8 - 122 - 8, entryTop + 38);
		roomPlace(send, APOC_RIGHT - 8 - 122, entryTop, APOC_RIGHT - 8, entryTop + 38);
		GameWindow *shown[] = { theRoomChat, entry, send };
		for (Int i = 0; i < (Int)ARRAY_SIZE(shown); ++i)
			if (shown[i] && shown[i]->winIsHidden())
				shown[i]->winHide(FALSE);
	}

	/// the room's own look, on its container under every window it holds
	void drawRoom(GameWindow *window, WinInstanceData *)
	{
		if (!theRoomClassic || theRoomGame == NULL)
			return;
		const IRegion2D panel = UIStyle::boardRect(UIStyle::MENU_PANEL_LEFT, UIStyle::MENU_PANEL_TOP, UIStyle::MENU_PANEL_RIGHT, UIStyle::MENU_PANEL_BOTTOM);
		UIStyle::glass(panel, 168);
		UIStyle::frameA(panel);

		// the title, the map and the format after it, the header's tenant at the right
		UnicodeString title = theRoomTitle ? GadgetStaticTextGetText(theRoomTitle) : UnicodeString::TheEmptyString;
		if (theRoomKind != ROOM_ONLINE)
			title = UIStyle::capitals(title);
		UnicodeString subtitle;
		const MapMetaData *md = TheMapCache ? TheMapCache->findMap(theRoomGame->getMap()) : NULL;
		const Bool apocalypse = roomApocalypse();
		if (apocalypse)
		{
			// the mode's name in the title and its version quiet at the subtitle's head, as the design sets
			// them (an online room's title is its own name); then the Create Game card's two lines
			if (theRoomKind != ROOM_ONLINE)
			{
				title = UIStyle::capitals(TheGameText->fetch("GUI:Apocalypse"));
				subtitle = TheGameText->fetch("GUI:ApocalypseVersion");
				subtitle.concat(u"   ");
			}
			subtitle.concat(TheGameText->fetch("GUI:GameModeApocalypseFact"));
			subtitle.concat(UIStyle::HEADER_JOINER);
			subtitle.concat(TheGameText->fetch("GUI:GameModeApocalypseSeats"));
		}
		else if (md)
			subtitle = md->m_displayName;
		if (!apocalypse)
		{
			// the format: each team's seated count, a player with no team on his own
			Int sizes[MAX_SLOTS], teams[MAX_SLOTS], groups = 0;
			for (Int i = 0; i < MAX_SLOTS; ++i)
			{
				const GameSlot *slot = theRoomGame->getConstSlot(i);
				if (slot == NULL || !slot->isOccupied() || slot->getPlayerTemplate() == PLAYERTEMPLATE_OBSERVER)
					continue;
				Int g = 0;
				while (slot->getTeamNumber() >= 0 && g < groups && teams[g] != slot->getTeamNumber())
					++g;
				if (slot->getTeamNumber() < 0 || g == groups)
				{
					teams[groups] = slot->getTeamNumber();
					sizes[groups++] = 0;
					g = groups - 1;
				}
				++sizes[g];
			}
			std::sort(sizes, sizes + groups);
			UnicodeString format;
			for (Int g = 0; g < groups; ++g)
			{
				UnicodeString one;
				one.format(u"%d", sizes[g]);
				if (g > 0)
				{
					// the string file trims a value's spaces, so the joiner's are the code's
					format.concat(u" ");
					format.concat(TheGameText->fetch("GUI:RoomVersus"));
					format.concat(u" ");
				}
				format.concat(one);
			}
			if (groups > 1)
			{
				if (!subtitle.isEmpty())
					subtitle.concat(UIStyle::HEADER_JOINER);
				subtitle.concat(format);
			}
		}
		UIStyle::menuHeader(roomText(TEXT_TITLE, title, UIStyle::menuFont(UIStyle::MENU_TEXT_TITLE, TRUE)),
			roomText(TEXT_SUBTITLE, subtitle, UIStyle::menuFont(UIStyle::MENU_TEXT_BODY)));
		GameFont *label = UIStyle::menuFont(UIStyle::MENU_TEXT_LABEL, TRUE);
		if (theRoomKind == ROOM_SKIRMISH)
		{
			if (theRoomPresets && !theRoomPresets->winIsHidden() && !apocalypse)
				roomDraw(TEXT_TENANT, TheGameText->fetch("GUI:RoomPreset"), label, 1486, 71, UIStyle::MUTED, 2);
		}
		else
		{
			const GameSlot *host = theRoomGame->getConstSlot(0);
			roomDraw(TEXT_TENANT, TheGameText->fetch("GUI:RoomHost"), label, 1440, 59, UIStyle::MUTED);
			if (host && host->isHuman())
				roomDraw(TEXT_TENANT_NAME, UIStyle::fitName(host->getName(), UIStyle::menuFont(UIStyle::MENU_TEXT_KEY), roomPx(240)),
					UIStyle::menuFont(UIStyle::MENU_TEXT_KEY), 1440, 82, theRoomGame->amIHost() ? UIStyle::GOLD : UIStyle::INK);
			Int taken = 0, open = 0;
			for (Int i = 0; i < theRoomRowCount; ++i)
			{
				const GameSlot *slot = theRoomGame->getConstSlot(i);
				if (slot && slot->isOccupied())
					++taken;
				if (slot && (slot->isOccupied() || slot->isOpen()))
					++open;
			}
			UnicodeString seats;
			seats.format(TheGameText->fetch("GUI:RoomSeatsTaken").str(), taken, open);
			roomDraw(TEXT_TENANT_SEATS, seats, UIStyle::menuFont(UIStyle::MENU_TEXT_BODY), ROOM_RIGHT, 82, UIStyle::MUTED, 2);
		}

		// the column heads and the rule under them
		static const char *const heads[] = { "GUI:RoomPlayer", "GUI:RoomArmy", "GUI:RoomColour", "GUI:RoomTeamHead", "GUI:RoomHandicap", "GUI:RoomMap" };
		static const Int headX[] = { ROOM_PLAYER_LEFT + 2, 462, 798, 994, 1122, ROOM_COLUMN_LEFT + 2 };
		// the Apocalypse room has no team column (the director allies every seat); its army and colour grow
		static const char *const apocalypseHeads[] = { "GUI:RoomPlayer", "GUI:RoomArmy", "GUI:RoomColour", "GUI:RoomHandicap", "GUI:RoomMap" };
		static const Int apocalypseHeadX[] = { ROOM_PLAYER_LEFT + 2, 462, 874, 1122, ROOM_COLUMN_LEFT + 2 };
		const char *const *headKeys = apocalypse ? apocalypseHeads : heads;
		const Int *headAt = apocalypse ? apocalypseHeadX : headX;
		const Int headCount = apocalypse ? (Int)ARRAY_SIZE(apocalypseHeads) : (Int)ARRAY_SIZE(heads);
		// a map picker over the seats has the seat column to itself
		for (Int h = theSeatsUnderPicker ? headCount - 1 : 0; h < headCount; ++h)
		{
			UIStyle::columnHeadAt(roomText(TEXT_HEADS + h, TheGameText->fetch(headKeys[h]), label), roomX(headAt[h]), roomY(137), UIStyle::INK);
		}
		roomFill(ROOM_COLUMN_LEFT, 152, ROOM_RIGHT, 153, UIStyle::withAlpha(UIStyle::FRAME, 150));

		// the seats as the plan lays them, the strips and the track
		Int seatIndex = 0, headIndex = 0;
		if (!theSeatsUnderPicker)
			roomFill(ROOM_LEFT, 152, ROOM_CELLS_RIGHT, 153, UIStyle::withAlpha(UIStyle::FRAME, 150));
		for (Int i = 0; i < (theSeatsUnderPicker ? 0 : theRoomPlan.itemCount); ++i)
		{
			const LobbyRoomItem &item = theRoomPlan.items[i];
			const Int top = LOBBY_SEATS_TOP + item.top;
			if (item.head && apocalypse)
				drawApocalypseHead(top);
			else if (item.head)
				drawRoomHead(min(headIndex++, (Int)(TEXT_ROWS - TEXT_GROUPS) / 2 - 1), item, top);
			else
			{
				drawRoomSeat(seatIndex++, item, top);
				const GameSlot *slot = roomSlot(item.slot);
				if (apocalypse && slot && !slot->isOccupied())
				{
					// an open seat says what it adds; its empty frame is drawRoomSeat's, as in every room
					const Int middle = top + item.height / 2;
					if (slot->isOpen())
						roomDraw(TEXT_APOCALYPSE + APOC_OPEN_SEAT + item.slot % 4, roomCountText("GUI:ApocalypseOpenSeat", APOCALYPSE_WAVE_PER_SEAT),
							UIStyle::menuFont(UIStyle::MENU_TEXT_HINT), 470, middle, UIStyle::MUTED);
				}
			}
		}
		if (apocalypse && !theSeatsUnderPicker)
		{
			drawApocalypseNights(theRoomChat == NULL);
			if (theRoomChat)
				roomFrame(ROOM_LEFT, APOC_CHAT_TOP, APOC_RIGHT, LOBBY_SEATS_BOTTOM, UIStyle::withAlpha(UIStyle::FRAME, 160));
		}
		if (apocalypse)
			drawApocalypseModeSet();
		if (theRoomPlan.above > 0 && !theSeatsUnderPicker)
		{
			drawRoomChevrons(ROOM_LEFT + 12, LOBBY_SEATS_TOP + 13, TRUE);
			roomDraw(TEXT_STRIPS, roomCountText(theRoomPlan.above == 1 ? "GUI:RoomSeatAbove" : "GUI:RoomSeatsAbove", theRoomPlan.above), label, ROOM_LEFT + 44, LOBBY_SEATS_TOP + 13, UIStyle::EDGE_LIT);
		}
		if (theRoomPlan.below > 0 && !theSeatsUnderPicker)
		{
			// the rows under the window fade out over its foot
			for (Int step = 0; step < 7; ++step)
				roomFill(ROOM_LEFT, LOBBY_SEATS_BOTTOM - 54 + 4 * step, ROOM_CELLS_RIGHT, LOBBY_SEATS_BOTTOM - 50 + 4 * step, GameMakeColor(0, 0, 0, 12 * step));
			drawRoomChevrons(ROOM_LEFT + 12, LOBBY_SEATS_BOTTOM - 13, FALSE);
			roomDraw(TEXT_STRIPS + 1, roomCountText(theRoomPlan.below == 1 ? "GUI:RoomSeatBelow" : "GUI:RoomSeatsBelow", theRoomPlan.below), label, ROOM_LEFT + 44, LOBBY_SEATS_BOTTOM - 13, UIStyle::EDGE_LIT);
		}
		if (theRoomPlan.maxScroll > 0 && theRoomPlan.orderCount > 0 && !theSeatsUnderPicker)
		{
			const Int span = LOBBY_SEATS_BOTTOM - LOBBY_SEATS_TOP;
			const Int shown = theRoomPlan.orderCount - theRoomPlan.above - theRoomPlan.below;
			const Int thumbTop = LOBBY_SEATS_TOP + span * theRoomPlan.above / theRoomPlan.orderCount;
			const Int thumbBottom = thumbTop + max(24, span * shown / theRoomPlan.orderCount);
			roomFill(1286, LOBBY_SEATS_TOP, 1288, LOBBY_SEATS_BOTTOM, UIStyle::withAlpha(UIStyle::FRAME, 160));
			roomFill(1282, thumbTop, 1292, thumbBottom, UIStyle::withAlpha(UIStyle::EDGE, 200));
			const IRegion2D thumb = UIStyle::boardRect(1282, thumbTop, 1292, thumbBottom);
			UIStyle::litLine(thumb.lo.x, thumb.lo.y, thumb.hi.x - thumb.lo.x, 1, UIStyle::EDGE_LIT);
			UIStyle::litLine(thumb.lo.x, thumb.hi.y - 1, thumb.hi.x - thumb.lo.x, 1, UIStyle::EDGE_LIT);
			roomFill(1284, (thumbTop + thumbBottom) / 2 - 3, 1290, (thumbTop + thumbBottom) / 2 - 2, UIStyle::EDGE_LIT);
			roomFill(1284, (thumbTop + thumbBottom) / 2 + 2, 1290, (thumbTop + thumbBottom) / 2 + 3, UIStyle::EDGE_LIT);
		}

		// the map's facts under its name; a joiner reads who picks it.  A map picker over the room says
		// its own map's words in that band, and its legend and hint in the foot
		if (apocalypse)
			drawApocalypseMapFacts(md);
		else if (md && theRoomMapName && !theSeatsUnderPicker)
		{
			Int x, y, w, h;
			theRoomMapName->winGetScreenPosition(&x, &y);
			theRoomMapName->winGetSize(&w, &h);
			UnicodeString players;
			players.format(TheGameText->fetch("GUI:RoomMapPlayers").str(), md->m_numPlayers);
			DisplayString *s = roomText(TEXT_MAP, players, UIStyle::menuFont(UIStyle::MENU_TEXT_HINT));
			Int sw, sh;
			s->getSize(&sw, &sh);
			s->draw(x, y + h + roomPx(2), UIStyle::MUTED, GameMakeColor(0, 0, 0, 160));
		}
		if (theRoomSelectMap && theRoomSelectMap->winIsHidden() && !apocalypse && !theSeatsUnderPicker)
		{
			Int x, y, w, h;
			theRoomSelectMap->winGetScreenPosition(&x, &y);
			theRoomSelectMap->winGetSize(&w, &h);
			DisplayString *s = roomText(TEXT_MAP + 1, TheGameText->fetch("GUI:RoomHostPicksMap"), UIStyle::menuFont(UIStyle::MENU_TEXT_HINT));
			Int sw, sh;
			s->getSize(&sw, &sh);
			s->draw(x + w - sw, y + (h - sh) / 2, UIStyle::MUTED, GameMakeColor(0, 0, 0, 160));
		}
		// the network rooms' RULES is a tab, but in an Apocalypse room the chat is always open under the nights
		if (theRoomKind == ROOM_SKIRMISH || apocalypse)
		{
			UIStyle::columnHeadAt(roomText(TEXT_MAP + 2, TheGameText->fetch("GUI:RoomRules"), label), roomX(ROOM_COLUMN_LEFT + 2), roomY(517), UIStyle::INK);
			roomFill(ROOM_COLUMN_LEFT, 532, ROOM_RIGHT, 533, UIStyle::withAlpha(UIStyle::FRAME, 150));
		}

		// the foot: its rule, then the record (skirmish) or who is ready (network) beside the keys
		roomFill(ROOM_LEFT, 962, ROOM_RIGHT, 963, UIStyle::withAlpha(UIStyle::FRAME, 110));
		GameFont *figure = UIStyle::menuFont(UIStyle::MENU_TEXT_ROW_FIGURE);
		if (apocalypse)
		{
			// Apocalypse never counts a win or a loss (ScoreScreen.cpp); the foot says what it counts instead,
			// after the second key (the rows say who is ready)
			GameFont *hint = UIStyle::menuFont(UIStyle::MENU_TEXT_HINT);
			roomDraw(TEXT_APOCALYPSE + APOC_FOOT, UIStyle::fitKey("GUI:ApocalypseScore", hint, roomPx(ROOM_COLUMN_LEFT - 20 - 580)), hint,
				580, 1002, UIStyle::MUTED);
		}
		else if (theSeatsUnderPicker)
		{
			// the map picker's legend and hint take the foot (MapPicker.cpp)
		}
		else if (theRoomKind == ROOM_SKIRMISH)
		{
			static const char *const words[] = { "GUI:RoomWins", "GUI:RoomLosses", "GUI:RoomStreak", "GUI:RoomBestStreak" };
			static const Int at[] = { 588, 765, 940, 1116 };
			for (Int i = 0; i < 4; ++i)
			{
				if (theRoomStats[i] == NULL)
					continue;
				const Int w = roomDraw(TEXT_FOOT + 2 * i, GadgetStaticTextGetText(theRoomStats[i]), figure, at[i], 1002, UIStyle::INK);
				DisplayString *s = roomText(TEXT_FOOT + 2 * i + 1, TheGameText->fetch(words[i]), label);
				Int sw, sh;
				s->getSize(&sw, &sh);
				s->draw(roomX(at[i]) + w + roomPx(10), roomY(1002) - sh / 2, UIStyle::MUTED, GameMakeColor(0, 0, 0, 160));
			}
		}
		else
		{
			Int humans = 0, ready = 0;
			for (Int i = 1; i < theRoomRowCount; ++i)
			{
				const GameSlot *slot = theRoomGame->getConstSlot(i);
				if (slot && slot->isHuman())
				{
					++humans;
					ready += slot->isAccepted() ? 1 : 0;
				}
			}
			UnicodeString count;
			count.format(u"%d / %d", ready, humans);
			const Int w = roomDraw(TEXT_FOOT, count, figure, 588, 1002, UIStyle::INK);
			DisplayString *s = roomText(TEXT_FOOT + 1, TheGameText->fetch("GUI:RoomPlayersReady"), label);
			Int sw, sh;
			s->getSize(&sw, &sh);
			s->draw(roomX(588) + w + roomPx(12), roomY(1002) - sh / 2, UIStyle::MUTED, GameMakeColor(0, 0, 0, 160));
		}
	}

	/// a seat's row's windows round its top, one line: a computer's level beside its personality, a
	/// player's name (a network player's rating and state inside its box), an open or closed seat's one box
	void placeRoomRow(Int row, Int seat, Int top)
	{
		const GameSlot *slot = roomSlot(seat);
		RoomRow &w = theRoomRows[row];
		roomPark(w.name);
		if (slot == NULL)
			return;
		const Bool seated = slot->isOccupied();
		// a takeover seat ("Human" on a skirmish seat past the first) counts as AI to the lobby but has no
		// AI and no personality (UpdateAIPersonaComboBoxes hides it): it is laid out as a player's row
		const Bool computer = slot->isAI() && !slot->isTakeover();
		const Bool apocalypse = roomApocalypse();
		const Int boxTop = top + ROW_BOX_TOP, boxBottom = boxTop + ROW_BOX_HEIGHT;
		roomFont(w.player, UIStyle::MENU_TEXT_BODY);
		if (!seated)
		{
			roomPlace(w.player, ROOM_PLAYER_LEFT, boxTop, ROOM_PLAYER_RIGHT, boxBottom);
			GameWindow *rest[] = { w.persona, w.army, w.colour, w.team, w.handicap, w.ping };
			for (Int i = 0; i < (Int)ARRAY_SIZE(rest); ++i)
				roomPark(rest[i]);
			return;
		}
		if (computer)
		{
			roomPlace(w.player, ROOM_PLAYER_LEFT, boxTop, ROOM_LEVEL_RIGHT, boxBottom);
			roomPlace(w.persona, ROOM_LEVEL_RIGHT + 8, boxTop, ROOM_PLAYER_RIGHT, boxBottom);
		}
		else
		{
			roomPlace(w.player, ROOM_PLAYER_LEFT, boxTop, ROOM_PLAYER_RIGHT, boxBottom);
			roomPark(w.persona);
		}
		roomFont(w.persona, UIStyle::MENU_TEXT_HINT);
		if (w.ping && theRoomKind == ROOM_ONLINE && slot->isHuman())
		{
			const Int pingRight = ROW_PORTRAIT_LEFT + ROW_PORTRAIT_SIZE - 1;
			const Int pingTop = top + (LOBBY_ROW_SEAT - ROW_PORTRAIT_SIZE) / 2 + 1;
			roomPlace(w.ping, pingRight - ROW_PING_SIZE, pingTop, pingRight, pingTop + ROW_PING_SIZE);
		}
		else
			roomPark(w.ping);
		if (apocalypse)
		{
			// no team box: the director allies every seat whatever it says (README-apocalypse-room.md, A)
			roomPlace(w.army, 460, boxTop, 856, boxBottom);
			roomPlace(w.colour, 872, boxTop, 1104, boxBottom);
			roomPark(w.team);
			roomPlace(w.handicap, 1120, boxTop, 1262, boxBottom);
			return;
		}
		roomPlace(w.army, 460, boxTop, 780, boxBottom);
		roomPlace(w.colour, 796, boxTop, 976, boxBottom);
		roomPlace(w.team, 992, boxTop, 1104, boxBottom);
		roomPlace(w.handicap, 1120, boxTop, 1262, boxBottom);
	}

	void parkRoomRow(Int row)
	{
		RoomRow &w = theRoomRows[row];
		GameWindow *all[] = { w.player, w.name, w.persona, w.army, w.colour, w.team, w.handicap, w.ping };
		for (Int i = 0; i < (Int)ARRAY_SIZE(all); ++i)
			roomPark(all[i]);
	}

	/// the order, the window, the binding and the rows' places from the game as it stands
	void roomRefresh(void)
	{
		theRoomDirty = FALSE;
		if (theRoomGame == NULL || theRoomParent == NULL || !theRoomClassic || theRoomRowCount == 0)
			return;
		Int count = theRoomRowCount;
		if (theRoomKind == ROOM_SKIRMISH)
		{
			// the seats the map holds, and any seat past them someone sits in or left open; the closed
			// tail of the 32 is not listed (README-classic-room.md: the list shows what the map allows)
			count = 0;
			for (Int i = 0; i < MAX_SLOTS; ++i)
			{
				const GameSlot *slot = theRoomGame->getConstSlot(i);
				if (slot && slot->getState() != SLOT_CLOSED)
					count = i + 1;
			}
			const MapMetaData *md = TheMapCache ? TheMapCache->findMap(theRoomGame->getMap()) : NULL;
			if (md)
				count = max(count, min((Int)MAX_SLOTS, md->m_numPlayers));
		}
		/* The Apocalypse room lists its seats always, four in a network room and the player's own alone in
			 the skirmish room.  Its skirmish map is made for the seats taken, so the count above (the last seat
			 not closed, or the map's starts) dropped a seat closed at the end of the four off the list, and
			 nothing could open it again: its row was parked and the walker's picks on it went nowhere. */
		if (IsApocalypseMap(theRoomGame->getMap()))
			count = min(theRoomRowCount, apocRoomSeats());
		Bool listOpen = FALSE;
		for (Int row = 0; row < theRoomRowCount; ++row)
			listOpen = listOpen || roomComboOpen(theRoomRows[row].team);
		LobbySeatState seats[MAX_SLOTS];
		Int moved = -1, movedCount = 0;
		theRoomHeld = FALSE;
		for (Int i = 0; i < count; ++i)
		{
			const GameSlot *slot = theRoomGame->getConstSlot(i);
			const Int team = slot ? slot->getTeamNumber() : -1;
			if (theRoomSortTeam[i] != team)
			{
				if (listOpen && theRoomSortTeam[i] != ROOM_SORT_UNSET)
					theRoomHeld = TRUE;		// the seat goes to its new group when the list closes
				else
				{
					if (theRoomSortTeam[i] != ROOM_SORT_UNSET)
					{
						moved = i;
						++movedCount;
					}
					theRoomSortTeam[i] = team;
				}
			}
			seats[i].team = theRoomSortTeam[i];
			seats[i].seated = slot && slot->isOccupied();
		}
		// a joiner reads who picks the map where Select Map would be (drawRoom)
		if (theRoomSelectMap && theRoomKind != ROOM_SKIRMISH && theRoomSelectMap->winIsHidden() == theRoomGame->amIHost())
			theRoomSelectMap->winHide(!theRoomGame->amIHost());
		const Int height = LOBBY_SEATS_BOTTOM - LOBBY_SEATS_TOP;
		// the skirmish room's first row is the local seat's, wherever it shows
		const Int mostShown = theRoomBinder ? theRoomRowCount - 1 : theRoomRowCount;
		// one seat's team change is followed and lit; a preset or a host's options that move many are not
		if (movedCount != 1)
			moved = -1;
		const Bool apocalypse = IsApocalypseMap(theRoomGame->getMap());
		if (apocalypse)
		{
			// four seats in seat order under the survivors' head: there are no teams to group by
			ApocalypsePlanRoom(count, mostShown, theRoomPlan);
			moved = -1;
			placeApocalypseRoom();
		}
		else
			LobbyPlanRoom(seats, count, theRoomScroll, height, mostShown, theRoomPlan);
		if (moved >= 0)
		{
			Bool shown = FALSE;
			for (Int i = 0; i < theRoomPlan.itemCount; ++i)
				shown = shown || (!theRoomPlan.items[i].head && theRoomPlan.items[i].slot == moved);
			if (!shown)
			{
				// the window follows the seat to its new group
				for (Int k = 0; k < theRoomPlan.orderCount; ++k)
					if (theRoomPlan.order[k] == moved)
						LobbyPlanRoom(seats, count, k, height, mostShown, theRoomPlan);
			}
			theRoomMoved = moved;
			theRoomMovedUntil = Clock_Milliseconds() + 1000;
		}
		theRoomScroll = theRoomPlan.scroll;

		if (theRoomBinder)
		{
			Int visible[MAX_SLOTS], n = 0;
			for (Int i = 0; i < theRoomPlan.itemCount; ++i)
				if (!theRoomPlan.items[i].head)
					visible[n++] = theRoomPlan.items[i].slot;
			Bool same = n == theRoomBoundCount;
			for (Int i = 0; same && i < n; ++i)
				same = visible[i] == theRoomBound[i];
			if (!same)
			{
				for (Int i = 0; i < n; ++i)
					theRoomBound[i] = visible[i];
				theRoomBoundCount = n;
				theRoomBinder(visible, n);
			}
		}

		Bool placed[MAX_SLOTS];
		for (Int row = 0; row < MAX_SLOTS; ++row)
			placed[row] = FALSE;
		for (Int i = 0; i < theRoomPlan.itemCount; ++i)
		{
			const LobbyRoomItem &item = theRoomPlan.items[i];
			if (item.head)
				continue;
			const Int row = theSeatRow[item.slot];
			if (row < 0 || row >= theRoomRowCount)
				continue;
			placeRoomRow(row, item.slot, LOBBY_SEATS_TOP + item.top);
			placed[row] = TRUE;
		}
		for (Int row = 0; row < theRoomRowCount; ++row)
			if (!placed[row])
				parkRoomRow(row);
	}

	GameWindow *roomWindow(const char *layoutFilename, const char *name)
	{
		AsciiString full;
		full.format("%s:%s", layoutFilename, name);
		return TheWindowManager->winGetWindowFromId(theRoomParent, NAMEKEY(full));
	}

	void initRoomLook(GameWindow *parent, const char *layoutFilename)
	{
		theRoomParent = parent;
		theSeatsUnderPicker = FALSE;
		theRoomKind = strcmp(layoutFilename, "SkirmishGameOptionsMenu.wnd") == 0 ? ROOM_SKIRMISH
			: strcmp(layoutFilename, "LanGameOptionsMenu.wnd") == 0 ? ROOM_LAN : ROOM_ONLINE;
		static const char *const containers[] = { "SubParent", "GadgetParent", "FadeParent" };
		theRoomContainer = roomWindow(layoutFilename, containers[theRoomKind - ROOM_SKIRMISH]);
		theRoomTitle = roomWindow(layoutFilename, theRoomKind == ROOM_ONLINE ? "StaticTextGameName" : "StaticTextTitle");
		theRoomLayout = layoutFilename;
		theRoomMap = roomWindow(layoutFilename, "MapWindow");
		// the Apocalypse room's fog edge goes over the preview; a second init finds the map hooked already
		if (theRoomMap && theRoomMap->winGetDrawFunc() != drawRoomMap)
		{
			theRoomMapDraw = theRoomMap->winGetDrawFunc();
			theRoomMap->winSetDrawFunc(drawRoomMap);
		}
		theRoomMapName = roomWindow(layoutFilename, "TextEntryMapDisplay");
		theRoomSelectMap = roomWindow(layoutFilename, "ButtonSelectMap");
		theRoomPresets = theRoomKind == ROOM_SKIRMISH ? roomWindow(layoutFilename, "ButtonPresets") : NULL;
		static const char *const stats[] = { "StaticTextWinsValue", "StaticTextLossesValue", "StaticTextStreakValue", "StaticTextBestStreakValue" };
		for (Int i = 0; i < 4; ++i)
			theRoomStats[i] = theRoomKind == ROOM_SKIRMISH ? roomWindow(layoutFilename, stats[i]) : NULL;
		theRoomGame = NULL;
		theRoomScroll = 0;
		theRoomHeld = FALSE;
		theRoomDirty = FALSE;
		theRoomMoved = -1;
		theRoomBoundCount = -1;
		theRoomBinder = NULL;
		theRoomChat = NULL;
		theRoomChatSeen = 0;
		theRoomPlan.itemCount = 0;
		theRoomPlan.orderCount = 0;
		theRoomPlan.maxScroll = 0;
		theRoomPlan.above = theRoomPlan.below = 0;
		for (Int i = 0; i < MAX_SLOTS; ++i)
			theRoomSortTeam[i] = ROOM_SORT_UNSET;

		// the rows the layout has, each row's windows by its number
		theRoomRowCount = 0;
		while (theRoomRowCount < MAX_SLOTS && findSeatWindow(parent, layoutFilename, "ComboBoxColor", theRoomRowCount))
		{
			const Int row = theRoomRowCount++;
			RoomRow &w = theRoomRows[row];
			w.player = findSeatWindow(parent, layoutFilename, "ComboBoxPlayer", row);
			if (theRoomKind == ROOM_SKIRMISH && row == 0)
				w.player = roomWindow(layoutFilename, "TextEntryPlayerName");
			w.name = findSeatWindow(parent, layoutFilename, "StaticTextPlayer", row);
			w.persona = findSeatWindow(parent, layoutFilename, "ComboBoxAIPersona", row);
			w.army = findSeatWindow(parent, layoutFilename, "ComboBoxPlayerTemplate", row);
			w.colour = findSeatWindow(parent, layoutFilename, "ComboBoxColor", row);
			w.team = findSeatWindow(parent, layoutFilename, "ComboBoxTeam", row);
			w.handicap = findSeatWindow(parent, layoutFilename, "ComboBoxHandicap", row);
			w.ping = findSeatWindow(parent, layoutFilename, "GenericPing", row);
			GameWindow *combos[] = { w.persona, w.army, w.colour, w.team, w.handicap };
			for (Int c = 0; c < (Int)ARRAY_SIZE(combos); ++c)
				styleRoomCombo(combos[c], combos[c] == w.persona ? UIStyle::MENU_TEXT_HINT : UIStyle::MENU_TEXT_BODY);
			if (w.player && BitTest(w.player->winGetStyle(), GWS_COMBO_BOX))
				styleRoomCombo(w.player, UIStyle::MENU_TEXT_BODY);
			else if (w.player)
			{
				w.player->winSetDrawFunc(drawRoomNameEntry);
				roomFont(w.player, UIStyle::MENU_TEXT_BODY);
			}
			// a seat's list opens over the seats around it, as far as the seat column goes
			GameWindow *lists[] = { w.player, w.persona, w.army, w.colour, w.team, w.handicap };
			for (Int c = 0; c < (Int)ARRAY_SIZE(lists); ++c)
				if (lists[c] && BitTest(lists[c]->winGetStyle(), GWS_COMBO_BOX))
					GadgetComboBoxSetDropDownPitch(lists[c], 1, roomPx(300), roomPx(300));
		}
		if (theRoomContainer)
			theRoomContainer->winSetDrawFunc(drawRoom);
		if (theRoomTitle && !theRoomTitle->winIsHidden())
			theRoomTitle->winHide(TRUE);

		// the rules: every combo and check box on the page, its labels in body type
		if (GameWindow *page = roomWindow(layoutFilename, "PageLobbySettings"))
			for (GameWindow *child = page->winGetChild(); child; child = child->winGetNext())
			{
				if (BitTest(child->winGetStyle(), GWS_COMBO_BOX))
					styleRoomCombo(child, UIStyle::MENU_TEXT_BODY);
				else if (BitTest(child->winGetStyle(), GWS_CHECK_BOX))
					UIStyle::setMenuCheckBox(child);
				else if (BitTest(child->winGetStyle(), GWS_STATIC_TEXT))
					roomFont(child, UIStyle::MENU_TEXT_BODY);
			}
		roomFont(theRoomMapName, UIStyle::MENU_TEXT_ROW_FIGURE);

		// the keys: Play / Start the one lit, the rest outlined; the presets key is the header's field
		static const char *const keys[] = { "ButtonBack", "ButtonRecord", "ButtonCommunicator", "ButtonSelectMap", "ButtonEmote",
			"ButtonRecordClose", "ButtonPresetsClose", "ButtonPresetLoad", "ButtonPresetSave", "ButtonPresetClear" };
		for (Int k = 0; k < (Int)ARRAY_SIZE(keys); ++k)
			UIStyle::setMenuKey(roomWindow(layoutFilename, keys[k]), FALSE);
		UIStyle::setMenuKey(roomWindow(layoutFilename, "ButtonStart"), TRUE);
		if (theRoomPresets)
			theRoomPresets->winSetDrawFunc(drawRoomPresetField);
		static const char *const boxes[] = { "RecordPopover", "PresetPopover" };
		for (Int b = 0; b < (Int)ARRAY_SIZE(boxes); ++b)
			if (GameWindow *box = roomWindow(layoutFilename, boxes[b]))
				box->winSetDrawFunc(drawRoomBox);

		// the chat's tabs sized to their words, its lines and the line to write in body type
		static const char *const chats[] = { "ListboxChatWindowLanGame", "ListboxChatWindowGameSpyGameSetup" };
		for (Int c = 0; c < (Int)ARRAY_SIZE(chats) && theRoomChat == NULL; ++c)
			theRoomChat = roomWindow(layoutFilename, chats[c]);
		roomFont(theRoomChat, UIStyle::MENU_TEXT_HINT);
		roomFont(roomWindow(layoutFilename, "TextEntryChat"), UIStyle::MENU_TEXT_HINT);
		Int tabLeft = ROOM_COLUMN_LEFT;
		static const char *const tabs[] = { "TabLobbySettings", "TabChat" };
		for (Int t = 0; t < (Int)ARRAY_SIZE(tabs); ++t)
		{
			GameWindow *tab = roomWindow(layoutFilename, tabs[t]);
			if (tab == NULL)
				continue;
			tab->winSetDrawFunc(drawRoomTab);
			DisplayString *words = roomText(TEXT_TAB + 1 + t, tab->winGetText(), UIStyle::menuFont(UIStyle::MENU_TEXT_LABEL + 2, TRUE));
			Int w, h;
			words->getSize(&w, &h);
			const Int width = REAL_TO_INT(w / UIStyle::boardFit(TheDisplay->getWidth(), TheDisplay->getHeight()).scale) + 36 + 28;
			roomPlace(tab, tabLeft, 506, tabLeft + width, 536);
			tabLeft += width + 8;
		}
	}

	void shutdownRoomLook(void)
	{
		for (Int i = 0; i < TEXT_COUNT; ++i)
		{
			if (theRoomTexts[i])
				TheDisplayStringManager->freeDisplayString(theRoomTexts[i]);
			theRoomTexts[i] = NULL;
		}
		theRoomParent = theRoomContainer = theRoomTitle = theRoomMap = theRoomMapName = theRoomSelectMap = theRoomPresets = NULL;
		theRoomChat = NULL;
		theRoomLayout = NULL;
		for (Int i = 0; i < 4; ++i)
			theRoomStats[i] = NULL;
		theRoomRowCount = 0;
		theRoomGame = NULL;
		theRoomBinder = NULL;
		theRoomKind = ROOM_NONE;
		theRoomClassic = TRUE;
	}
}

void SetLobbyRoomClassic(Bool classic)
{
	theRoomClassic = classic;
	if (!classic)
		theRoomPlan.itemCount = 0;
}

void SetLobbyRowBinder(LobbyRowBinder binder)
{
	theRoomBinder = binder;
	theRoomBoundCount = -1;
}

void LobbyRoomUpdate(void)
{
	if (theRoomParent == NULL || !theRoomClassic)
		return;
	Bool listOpen = FALSE;
	for (Int row = 0; row < theRoomRowCount; ++row)
		listOpen = listOpen || roomComboOpen(theRoomRows[row].team);
	if (theRoomDirty || (theRoomHeld && !listOpen))
		roomRefresh();
}

/// the board pixels between the pointer and the top of the seat track's thumb while it is dragged, -1 when not
static Int theRoomThumbGrip = -1;

Bool LobbyRoomInput(UnsignedInt msg, UnsignedIntPtr mData1)
{
	const Bool dragging = theRoomThumbGrip >= 0;
	if (msg == GWM_LEFT_UP || msg == GWM_LEFT_DRAG)
	{
		// the thumb's drag: the press that took it made the room the window the pointer is held by, so
		// the moves and the release come here wherever the pointer goes
		if (!dragging)
			return FALSE;
		if (msg == GWM_LEFT_UP)
		{
			theRoomThumbGrip = -1;
			return TRUE;
		}
	}
	if (theRoomParent == NULL || !theRoomClassic || theRoomGame == NULL || theRoomPlan.maxScroll == 0 || theSeatsUnderPicker)
	{
		theRoomThumbGrip = -1;
		return dragging;
	}
	const Int mouseX = (Short)LOLONGTOSHORT(mData1), mouseY = (Short)HILONGTOSHORT(mData1);
	const UIStyle::BoardFit fit = UIStyle::boardFit(TheDisplay->getWidth(), TheDisplay->getHeight());
	const Int boardY = fit.scale > 0.0f ? REAL_TO_INT((mouseY - fit.y) / fit.scale) : mouseY;
	const Int span = LOBBY_SEATS_BOTTOM - LOBBY_SEATS_TOP;
	const Int count = max(1, theRoomPlan.orderCount);
	const Int thumbTop = LOBBY_SEATS_TOP + span * theRoomPlan.above / count;
	if (msg == GWM_LEFT_DRAG)
	{
		// the seat whose share of the track the thumb's top stands on is the first one shown
		const Int top = boardY - theRoomThumbGrip;
		const Int seat = max(0, min(theRoomPlan.maxScroll, (Int)((top - LOBBY_SEATS_TOP) * count + span / 2) / span));
		if (seat != theRoomScroll)
		{
			TheWindowManager->winSetLoneWindow(NULL);
			theRoomScroll = seat;
			roomRefresh();
		}
		return TRUE;
	}
	const IRegion2D seats = UIStyle::boardRect(ROOM_LEFT, LOBBY_SEATS_TOP, ROOM_SEAT_COLUMN_RIGHT, LOBBY_SEATS_BOTTOM);
	if (mouseX < seats.lo.x || mouseX >= seats.hi.x || mouseY < seats.lo.y || mouseY >= seats.hi.y)
		return FALSE;
	if (msg == GWM_LEFT_DOWN && mouseX >= UIStyle::boardRect(1276, 0, 1276, 0).lo.x)
	{
		// a press on the thumb takes it; the drag moves it a seat at a time
		const Int shown = max(1, theRoomPlan.orderCount - theRoomPlan.above - theRoomPlan.below);
		const Int thumbBottom = thumbTop + max(24, span * shown / count);
		if (boardY >= thumbTop && boardY < thumbBottom)
		{
			theRoomThumbGrip = boardY - thumbTop;
			return TRUE;
		}
	}
	Int step = 0;
	if (msg == GWM_WHEEL_UP || msg == GWM_WHEEL_DOWN)
		step = msg == GWM_WHEEL_UP ? -1 : 1;
	else if (msg == GWM_LEFT_DOWN)
	{
		// a strip moves a seat; the track a window's worth above or below its thumb
		const Int shown = max(1, theRoomPlan.orderCount - theRoomPlan.above - theRoomPlan.below);
		const IRegion2D above = UIStyle::boardRect(ROOM_LEFT, LOBBY_SEATS_TOP, ROOM_CELLS_RIGHT, LOBBY_SEATS_TOP + LOBBY_ROW_STRIP);
		const IRegion2D below = UIStyle::boardRect(ROOM_LEFT, LOBBY_SEATS_BOTTOM - LOBBY_ROW_STRIP, ROOM_CELLS_RIGHT, LOBBY_SEATS_BOTTOM);
		if (theRoomPlan.above > 0 && mouseY < above.hi.y && mouseX < above.hi.x)
			step = -1;
		else if (theRoomPlan.below > 0 && mouseY >= below.lo.y && mouseX < below.hi.x)
			step = 1;
		else if (mouseX >= UIStyle::boardRect(1276, 0, 1276, 0).lo.x)
			step = boardY < thumbTop ? -shown : shown;
	}
	if (step == 0)
		return FALSE;
	// an open list goes with the scroll: its box moves to another seat's row or off the board
	TheWindowManager->winSetLoneWindow(NULL);
	theRoomScroll = max(0, min(theRoomPlan.maxScroll, theRoomScroll + step));
	roomRefresh();
	return TRUE;
}

void FitLobbyRoomMap(GameWindow *mapWindow, const MapMetaData *map)
{
	// a map picker's preview stands where the room's does, by the same rule
	if (MapPickerFitPreview(mapWindow, map))
		return;
	if (mapWindow == NULL || mapWindow != theRoomMap || !theRoomClassic)
		return;
	if (roomApocalypse())
	{
		placeApocalypseRoom();	// its square preview, whatever shipped map the generator borrowed its look from
		return;
	}
	Real ratio = 542.0f / 278.0f;
	if (map)
	{
		const Real w = map->m_extent.hi.x - map->m_extent.lo.x;
		const Real h = map->m_extent.hi.y - map->m_extent.lo.y;
		if (w > 0.0f && h > 0.0f)
			ratio = w / h;
	}
	if (ratio >= 1.3f)
	{
		// wide: the column's width at its own shape, at most 278 tall, the name under it
		const Int height = min(278, REAL_TO_INT(542.0f / ratio + 0.5f));
		roomPlace(mapWindow, ROOM_COLUMN_LEFT, LOBBY_SEATS_TOP, ROOM_RIGHT, LOBBY_SEATS_TOP + height);
		roomPlace(theRoomMapName, ROOM_COLUMN_LEFT, 448, ROOM_RIGHT - 208, 478);
		roomPlace(theRoomSelectMap, ROOM_RIGHT - 200, 448, ROOM_RIGHT, 494);
	}
	else
	{
		// squarer: 324 tall at the left, its name and Select Map beside it
		const Int width = min(324, REAL_TO_INT(324.0f * ratio + 0.5f));
		roomPlace(mapWindow, ROOM_COLUMN_LEFT, LOBBY_SEATS_TOP, ROOM_COLUMN_LEFT + width, LOBBY_SEATS_TOP + 324);
		// two lines for a long name beside the picture, Select Map under it on the same left edge, 24
		// from the 324 square (they stood 2 apart, 1662 and 1660)
		roomPlace(theRoomMapName, ROOM_COLUMN_LEFT + 348, LOBBY_SEATS_TOP, ROOM_RIGHT, LOBBY_SEATS_TOP + 64);
		roomPlace(theRoomSelectMap, ROOM_COLUMN_LEFT + 348, LOBBY_SEATS_TOP + 278, ROOM_RIGHT, LOBBY_SEATS_TOP + 324);
	}
}

void InitLobbySeatVisuals(GameWindow *parent, const char *layoutFilename)
{
	DirectStrikeLobby_restoreRoom();	// what a Direct Strike room before this one hid
	theRoomThumbGrip = -1;
	resetSeatRows();
	attachSeatRatings(parent, layoutFilename);
	initRoomLook(parent, layoutFilename);
	GameWindow **const windows[] = { theSeatCard, theSeatBar, theSeatSwatch, theSeatTeam };
	const char *const names[] = { "SeatCard", "SeatBar", "SeatSwatch", "SeatTeam" };
	for (Int kind = 0; kind < (Int)ARRAY_SIZE(names); ++kind)
		for (Int i = 0; i < MAX_SLOTS; ++i)
		{
			AsciiString name;
			name.format("%s:%s%d", layoutFilename, names[kind], i);
			windows[kind][i] = TheWindowManager->winGetWindowFromId(parent, NAMEKEY(name));
		}
	for (Int i = 0; i < MAX_SLOTS; ++i)
	{
		if (theSeatSwatch[i])
			theSeatSwatch[i]->winSetDrawFunc(drawSwatch);
		if (theSeatTeam[i])
			theSeatTeam[i]->winSetDrawFunc(drawSwatch);
	}
	theSeatsShowReady = strcmp(layoutFilename, "SkirmishGameOptionsMenu.wnd") != 0;
	FactionPickerAttach(parent, layoutFilename);
}

// A rules row is 38 pixels at 1080 and a list row 23, so four entries used to end halfway down the
// third box under the open one.  Each box's list now grows to whole rows, down to the page's foot;
// a list with no room left below (the page's last boxes) opens upwards over the rows above, so none
// spills onto the Play button.  The pitch is the smallest gap
// between two boxes' tops, so a layout that moves the rows takes it along.
void SnapLobbyRulesLists(GameWindow *parent, const char *layoutFilename)
{
	AsciiString name;
	name.format("%s:PageLobbySettings", layoutFilename);
	GameWindow *page = TheWindowManager->winGetWindowFromId(parent, NAMEKEY(name));
	if (!page)
		return;	// a layout from before the rules page

	Int x, pageTop, width, pageHeight;
	page->winGetScreenPosition(&x, &pageTop);
	page->winGetSize(&width, &pageHeight);

	Int pitch = 0;
	for (GameWindow *box = page->winGetChild(); box; box = box->winGetNext())
	{
		if (!BitTest(box->winGetStyle(), GWS_COMBO_BOX))
			continue;
		Int top, otherTop;
		box->winGetScreenPosition(&x, &top);
		for (GameWindow *other = page->winGetChild(); other; other = other->winGetNext())
		{
			if (!BitTest(other->winGetStyle(), GWS_COMBO_BOX))
				continue;
			other->winGetScreenPosition(&x, &otherTop);
			if (otherTop > top && (pitch == 0 || otherTop - top < pitch))
				pitch = otherTop - top;
		}
	}
	if (pitch == 0)
		return;	// one box, no rows to end on

	for (GameWindow *box = page->winGetChild(); box; box = box->winGetNext())
	{
		if (!BitTest(box->winGetStyle(), GWS_COMBO_BOX))
			continue;
		Int top, height;
		box->winGetScreenPosition(&x, &top);
		box->winGetSize(&width, &height);
		GadgetComboBoxSetDropDownPitch(box, pitch, pageTop + pageHeight - (top + height), top - pageTop);
	}
}

void ShutdownLobbySeatVisuals(void)
{
	detachSeatRatings();
	shutdownRoomLook();
	FactionPickerDetach();
	DirectStrikeLobby_detach();
	for (Int i = 0; i < MAX_SLOTS; ++i)
		theSeatCard[i] = theSeatBar[i] = theSeatSwatch[i] = theSeatTeam[i] = NULL;
	resetSeatRows();
}

static void showColor(GameWindow *window, Color color)
{
	if (window)
		window->winSetEnabledColor(0, color);
}

void UpdateLobbySeatVisuals(GameInfo *myGame)
{
	for (Int i = 0; i < MAX_SLOTS; ++i)
	{
		const GameSlot *slot = myGame->getConstSlot(i);
		const Bool occupied = slot->isOccupied();
		const Color color = LobbySeatColor(slot);
		if (theSeatPlayer[i])
		{
			if (slot->isHuman())
				theSeatNick[i].translate(slot->getName());
			else
				theSeatNick[i].clear();
		}
		const Int row = theSeatRow[i];
		if (row < 0)
			continue;
		showColor(theSeatBar[row], color);
		showColor(theSeatSwatch[row], color);
		showColor(theSeatTeam[row], occupied ? LobbyTeamColor(slot->getTeamNumber()) : theSeatEmptyColor);

		if (theSeatCard[row] == NULL || !theSeatsShowReady)
			continue;
		// the host always sits in the first seat of a network room and is never asked to ready up
		const Bool host = i == 0 && slot->isHuman();
		const Bool ready = slot->isHuman() && !host && slot->isAccepted();
		theSeatCard[row]->winSetEnabledColor(0, ready ? theCardReadyColor : UIStyle::PANEL);
		theSeatCard[row]->winSetEnabledBorderColor(0, host ? UIStyle::GOLD : UIStyle::FRAME);
	}
	// the room's order, window and rows from the slots as they now stand
	theRoomGame = myGame;
	roomRefresh();
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
					if ((Int)(intptr_t)GadgetComboBoxGetItemData(comboPlayer[i], item) == SlotPickOf(slot))
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
				FactionPickerSyncRandom(comboPlayerTemplate[i], slot->getPlayerTemplate());
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
