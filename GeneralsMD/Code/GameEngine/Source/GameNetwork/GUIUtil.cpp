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
#include "GameClient/Image.h"
#include "GameClient/Mouse.h"
#include "GameClient/UIStyle.h"
#include "GameLogic/GameLogic.h" // SUPERWEAPON_RESTRICT_COUNT
#include "GameLogic/AI.h" // AIPersonality, for the personality column
#include "GameNetwork/GameInfo.h"
#include "Common/PlayerTemplate.h"
#include "GameNetwork/LANAPICallbacks.h" // for acceptTrueColor, etc
#include "GameClient/ChallengeGenerals.h"
#include "GameClient/FactionPicker.h"
#include "Common/GlobalData.h"
#include "Lib/WideCharFns.h"
#include "Lib/Clock.h"
#include "Common/RandomMapGenerator.h"
#include "Common/RandomMapTemplate.h"
#include "Common/UserPreferences.h"
#include "GameClient/GadgetRadioButton.h"
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
void PopulatePlayerSlotComboBox(GameWindow *comboBox, Int color, Bool allowTakeover)
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

	// The combo box only grows its listbox when an add crosses the current length, and both
	// GadgetListBoxSetItemData and the add itself silently do nothing past it - so make room for
	// the whole list first, and tell the box how many rows to drop down before the first add.
	Int numSeats = (Int)(sizeof(seats)/sizeof(seats[0]));
	if (!allowTakeover)
		--numSeats;
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
		// a colour's name is words in that colour; the swatch beside it keeps the colour itself
		newIndex = GadgetComboBoxAddEntry(comboArray[comboBox], colorName, UIStyle::readable(def->getColor()));
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

	// retail's armies, then each mod's own block; the fork's own factions load after the mods' and still
	// belong with retail's
	std::vector<Int> order(numPlayerTemplates);
	for (Int c=0; c<numPlayerTemplates; ++c)
		order[c] = c;
	std::stable_sort(order.begin(), order.end(), [](Int a, Int b) {
		return ThePlayerTemplateStore->getNthPlayerTemplate(a)->getModIndex() < ThePlayerTemplateStore->getNthPlayerTemplate(b)->getModIndex();
	});

	for (Int n=0; n<numPlayerTemplates; ++n)
	{
		const Int c = order[n];
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
		// each mod's America is a side of its own, "<mod name> - USA"
		AsciiString seenKey;
		seenKey.format("%d:%s", fac->getModIndex(), side.str());
		if (seenSides.find(seenKey) != seenSides.end())
			continue;

		seenSides.insert(seenKey);

		UnicodeString label = TheGameText->fetch(side);
		if (fac->getModIndex() >= 0)
		{
			WideChar modName[128];
			WideCharFromUtf8(TheGlobalData->m_modNames[fac->getModIndex()].str(), modName, ARRAY_SIZE(modName));
			// the mod's own name for the faction: The End of Days' Russia sits on our China Nuke General's side
			static const WideChar MissingLabel[] = u"MISSING";
			const Int missingLength = ARRAY_SIZE(MissingLabel) - 1;
			UnicodeString sideLabel = fac->getDisplayName();
			if (sideLabel.isEmpty() || (sideLabel.getLength() >= missingLength
				&& std::char_traits<WideChar>::compare(sideLabel.str(), MissingLabel, missingLength) == 0))
			{
				AsciiString modSide;
				modSide.format("M%d~%s", fac->getModIndex() + 1, side.str());
				sideLabel = TheGameText->fetch(modSide);
			}
			label = UnicodeString(modName);
			label.concat(UnicodeString(u" - "));
			label.concat(sideLabel);
		}
		newIndex = GadgetComboBoxAddEntry(comboArray[comboBox], label, def->getColor());
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

// The personality column has no heading and only a computer's seat fills it; on any other seat the
// player box (and the online room's name over it) reaches across it.  The two widths are taken from
// the persona box and the army box after it, which never move, so a second init of a layout whose
// boxes are already wide measures the same.
static GameWindow *thePersonaSeatPlayer[MAX_SLOTS];
static GameWindow *thePersonaSeatName[MAX_SLOTS];
static GameWindow *thePersonaSeatArmy[MAX_SLOTS];
static Int thePersonaSeatNarrow[MAX_SLOTS];
static Int thePersonaSeatWide[MAX_SLOTS];

static GameWindow *findSeatWindow(GameWindow *parent, const char *layoutFilename, const char *name, Int seat)
{
	AsciiString full;
	full.format("%s:%s%d", layoutFilename, name, seat);
	return TheWindowManager->winGetWindowFromId(parent, NAMEKEY(full));
}

static void measurePersonaSeat(GameWindow *parent, const char *layoutFilename, GameWindow *persona, Int seat)
{
	GameWindow *player = findSeatWindow(parent, layoutFilename, "ComboBoxPlayer", seat);
	if (!player)
	{
		// the skirmish room's own seat is a text entry
		AsciiString full;
		full.format("%s:TextEntryPlayerName", layoutFilename);
		player = TheWindowManager->winGetWindowFromId(parent, NAMEKEY(full));
	}
	thePersonaSeatPlayer[seat] = player;
	thePersonaSeatName[seat] = findSeatWindow(parent, layoutFilename, "StaticTextPlayer", seat);
	GameWindow *army = findSeatWindow(parent, layoutFilename, "ComboBoxPlayerTemplate", seat);
	thePersonaSeatArmy[seat] = army;

	Int playerX, personaX, armyX, y, personaWidth, height;
	player->winGetScreenPosition(&playerX, &y);
	persona->winGetScreenPosition(&personaX, &y);
	persona->winGetSize(&personaWidth, &height);
	army->winGetScreenPosition(&armyX, &y);
	const Int gap = armyX - (personaX + personaWidth);
	thePersonaSeatNarrow[seat] = personaX - gap - playerX;
	thePersonaSeatWide[seat] = personaX + personaWidth - playerX;
}

static void setSeatPlayerWidth(GameWindow *window, Int width)
{
	if (!window)
		return;	// only the online room has a name over the box
	Int oldWidth, height;
	window->winGetSize(&oldWidth, &height);
	if (oldWidth != width)
		window->winSetSize(width, height);
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

void InitAIPersonaComboBoxes(GameWindow *parent, const char *layoutFilename,
														 GameWindow *comboPersona[], NameKeyType comboPersonaID[])
{
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
			AsciiString label;
			label.format("GUI:AIPersona%s", aiPersonalityName((AIPersonality)p));
			Int index = GadgetComboBoxAddEntry(comboPersona[i], TheGameText->fetch(label), color);
			GadgetComboBoxSetItemData(comboPersona[i], index, (void *)(intptr_t)p);
		}
		GadgetComboBoxSetSelectedPos(comboPersona[i], AIPERSONA_NORMAL, TRUE);
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
		comboPersona[i]->winHide(!computer || thePersonaSeatArmy[i]->winIsHidden());
		const Int width = computer ? thePersonaSeatNarrow[i] : thePersonaSeatWide[i];
		setSeatPlayerWidth(thePersonaSeatPlayer[i], width);
		setSeatPlayerWidth(thePersonaSeatName[i], width);
		if (!computer)
			continue;
		comboPersona[i]->winEnable(myGame->amIHost());
		Int pos = -1;
		GadgetComboBoxGetSelectedPos(comboPersona[i], &pos);
		if (pos != slot->getAIPersonality())
			GadgetComboBoxSetSelectedPos(comboPersona[i], slot->getAIPersonality(), TRUE);
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
			GadgetComboBoxSetSelectedPos(comboPersona[i], was, TRUE);	// entries are in enum order
			return FALSE;
		}
		return TRUE;
	}
	return FALSE;
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
		holeBottom - holeTop - 2 * gap, window->winGetEnabledColor(0));
}

void DrawMapListTab(GameWindow *window, WinInstanceData *instData)
{
	Int x, y, width, height;
	window->winGetScreenPosition(&x, &y);
	window->winGetSize(&width, &height);
	UIStyle::tab(x, y, width, height);
	DisplayString *text = instData->getTextDisplayString();
	if (text->getFont() != window->winGetFont())
		text->setFont(window->winGetFont());
	Color color = UIStyle::INK;
	if (!BitTest(window->winGetStatus(), WIN_STATUS_ENABLED))
		color = UIStyle::MUTED;
	else if (BitTest(instData->getState(), WIN_STATE_SELECTED))
		color = UIStyle::ACTIVE;
	Int textWidth, textHeight;
	text->getSize(&textWidth, &textHeight);
	text->draw(x + (width - textWidth) / 2, y + (height - textHeight) / 2, color, UIStyle::SHADOW);
}

// The Random tab ------------------------------------------------------------------------------
// Tools/mapselect_layout.py clones its windows into all three pickers under the same names.  The
// choice outlives the picker in RandomMap.ini; the windows go with it.  Nothing is generated until
// Accept: the preview shows the shipped map a template row names.

void positionStartSpots(AsciiString mapName, GameWindow *buttonMapStartPositions[], GameWindow *mapWindow);

static const char *const theRandomMapPrefsFile = "RandomMap.ini";

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

/// The template row's own shipped map with its start spots, the Official tab's picture; Surprise me
/// shows the unknown map.
static void showRandomTemplate(void)
{
	positionStartSpots(theRandomTemplate, theRandomStartSpots, theRandomPreview);
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
	Bool found = FALSE;
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
	theRandomTab->winSetDrawFunc(DrawMapListTab);
	theRandomTab->winEnable(allowed);

	theRandomPanel.clear();
	theRandomPanel.push_back(randomTabWindow("StaticTextRandomTemplate"));
	theRandomPanel.push_back(theTemplateList);
	theRandomPanel.push_back(randomTabWindow("StaticTextRandomSize"));
	for (Int size = 0; size < RANDOM_MAP_SIZE_COUNT; ++size)
	{
		theSizeButtons[size] = randomTabWindow("RadioButtonRandomSize", size);
		theSizeButtons[size]->winSetDrawFunc(DrawMapListTab);
		theRandomPanel.push_back(theSizeButtons[size]);
	}

	UserPreferences pref;
	pref.load(theRandomMapPrefsFile);
	theRandomSize = pref.getInt("Size", RANDOM_MAP_SIZE_NORMAL);
	if (theRandomSize < 0 || theRandomSize >= RANDOM_MAP_SIZE_COUNT)
		theRandomSize = RANDOM_MAP_SIZE_NORMAL;
	theRandomTemplate = pref.getAsciiString("Template", AsciiString::TheEmptyString);

	fillTemplateList();
	GadgetRadioSetSelection(theSizeButtons[theRandomSize], FALSE);

	const Bool open = allowed && pref.getBool("Open", FALSE);
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
		const AsciiString &chosen = theTemplates[(Int)(intptr_t)GadgetListBoxGetItemData(theTemplateList, row)];
		if (chosen != theRandomTemplate)
		{
			theRandomTemplate = chosen;
			saveRandomTab();
			showRandomTemplate();
		}
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
AsciiString RandomMapTabMap(void)
{
	if (!theRandomTabOpen)
		return AsciiString::TheEmptyString;
	const Int players = roomRandomMapPlayers(theRandomRoom);
	RandomMapSettings settings;
	settings.m_seed = (Int)((Clock_Milliseconds() + 7919u * ++theRolls) & 0x7fffffff);
	settings.m_numPlayers = players;
	settings.m_playableCells = RandomMapGenerator::cellsFor((RandomMapSize)theRandomSize, players, theRandomTemplate);
	settings.m_template = theRandomTemplate;
	AsciiString map;
	stageRandomMap(settings, map);
	map.toLower();
	TheMapCache->updateCache();
	return map;
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
	// the rooms ask every frame; a map and a count already answered cost a compare
	static AsciiString theFittedMap;
	static Int theFittedPlayers = 0;
	const Int players = roomRandomMapPlayers(game);
	if (players == theFittedPlayers && game->getMap().compareNoCase(theFittedMap) == 0)
		return FALSE;
	theFittedMap = game->getMap();
	theFittedPlayers = players;

	AsciiString map;
	if (!restageRandomMap(game->getMap(), players, map) || map.compareNoCase(game->getMap()) == 0)
		return FALSE;
	map.toLower();
	TheMapCache->updateCache();
	const MapMetaData *md = TheMapCache->findMap(map);
	// a template that built nothing leaves no map under the new name; the room keeps the one it has
	if (md == NULL)
		return FALSE;
	game->setMap(map);
	game->setMapCRC(md->m_CRC);
	game->setMapSize(md->m_filesize);
	game->getSlot(0)->setMapAvailability(TRUE);
	game->resetStartSpots();
	theFittedMap = map;
	DEBUG_LOG(("random map: the room seats %d, map now '%s'\n", players, map.str()));
	return TRUE;
}

void InitLobbySeatVisuals(GameWindow *parent, const char *layoutFilename)
{
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

void ShutdownLobbySeatVisuals(void)
{
	FactionPickerDetach();
	for (Int i = 0; i < MAX_SLOTS; ++i)
		theSeatCard[i] = theSeatBar[i] = theSeatSwatch[i] = theSeatTeam[i] = NULL;
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
		showColor(theSeatBar[i], color);
		showColor(theSeatSwatch[i], color);
		showColor(theSeatTeam[i], occupied ? LobbyTeamColor(slot->getTeamNumber()) : theSeatEmptyColor);

		if (theSeatCard[i] == NULL || !theSeatsShowReady)
			continue;
		// the host always sits in the first seat of a network room and is never asked to ready up
		const Bool host = i == 0 && slot->isHuman();
		const Bool ready = slot->isHuman() && !host && slot->isAccepted();
		theSeatCard[i]->winSetEnabledColor(0, ready ? theCardReadyColor : UIStyle::PANEL);
		theSeatCard[i]->winSetEnabledBorderColor(0, host ? UIStyle::GOLD : UIStyle::FRAME);
	}
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
