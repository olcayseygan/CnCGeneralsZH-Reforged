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
// FILE: SkirmishGameOptionsMenu.cpp
// Author: Chris Brue, August 2002
// Description: Lan Game Options Menu
///////////////////////////////////////////////////////////////////////////////////////

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine
#include "Lib/Clock.h"


#include "Common/BattleHonors.h"
#include "Common/FileSystem.h"
#include "Common/GameEngine.h"
#include "Common/PlayerTemplate.h"
#include "Common/QuotedPrintable.h"
#include "Common/RandomValue.h"
#include "Common/SkirmishBattleHonors.h"
#include "Common/SkirmishPreferences.h"
#include "GameLogic/GameLogic.h"
#include "GameClient/AnimateWindowManager.h"
#include "GameClient/WindowLayout.h"
#include "GameClient/Gadget.h"
#include "GameClient/Shell.h"
#include "GameClient/ShellHooks.h"
#include "GameClient/KeyDefs.h"
#include "GameClient/GameWindowManager.h"
#include "GameClient/GadgetListBox.h"
#include "GameClient/GadgetCheckBox.h"
#include "GameClient/GadgetComboBox.h"
#include "GameClient/GadgetTextEntry.h"
#include "GameClient/GadgetPushButton.h"
#include "GameClient/GadgetStaticText.h"
#include "GameClient/MapUtil.h"
#include "GameClient/Mouse.h"
#include "GameClient/GameWindowTransitions.h"
#include "GameNetwork/GameSpy/LobbyUtils.h"

#include "Common/MultiplayerSettings.h"
#include "GameClient/GameText.h"
#include "GameClient/CDCheck.h"
#include "GameClient/ExtendedMessageBox.h"
#include "GameClient/MessageBox.h"
#include "GameNetwork/GameInfo.h"
#include "GameNetwork/GUIUtil.h"
#include "GameNetwork/IPEnumeration.h"
#include "WWDownload/Registry.h"

#ifdef _INTERNAL
// for occasional debugging...
//#pragma optimize("", off)
//#pragma MESSAGE("************************************** WARNING, optimization disabled for debugging purposes")
#endif

SkirmishGameInfo *TheSkirmishGameInfo = NULL;
Bool SkirmishApocalypse = FALSE;

AsciiString SkirmishRoomLayout( void )
{
	return AsciiString( SkirmishApocalypse ? "Menus/ApocalypseGameOptionsMenu.wnd" : "Menus/SkirmishGameOptionsMenu.wnd" );
}

// window ids ------------------------------------------------------------------------------
static NameKeyType parentSkirmishGameOptionsID = NAMEKEY_INVALID;
static NameKeyType textEntryPlayerNameID = NAMEKEY_INVALID;
static NameKeyType comboBoxPlayerID[MAX_SLOTS] = { NAMEKEY_INVALID,NAMEKEY_INVALID,
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

static NameKeyType textEntryMapDisplayID = NAMEKEY_INVALID;
static NameKeyType buttonExitID = NAMEKEY_INVALID;
static NameKeyType buttonStartID = NAMEKEY_INVALID;
static NameKeyType buttonSelectMapID = NAMEKEY_INVALID;
static NameKeyType buttonResetID = NAMEKEY_INVALID;
static NameKeyType windowMapID = NAMEKEY_INVALID;
static NameKeyType comboBoxGameSpeedID = NAMEKEY_INVALID;
static NameKeyType comboBoxSuperweaponsID = NAMEKEY_INVALID;
static NameKeyType comboBoxStartingCashID = NAMEKEY_INVALID;
static NameKeyType checkBoxUnitLimitID = NAMEKEY_INVALID;
static NameKeyType comboBoxIncomeSharingID = NAMEKEY_INVALID;
static NameKeyType comboBoxTechRespawnID = NAMEKEY_INVALID;
static NameKeyType comboBoxSupplyPileLimitID = NAMEKEY_INVALID;
static NameKeyType comboBoxGameModeID = NAMEKEY_INVALID;

// Window Pointers ------------------------------------------------------------------------
static GameWindow *parentSkirmishGameOptions = NULL;
static GameWindow *buttonExit = NULL;
static GameWindow *buttonStart = NULL;
static GameWindow *buttonSelectMap = NULL;
static GameWindow *textEntryMapDisplay = NULL;
static GameWindow *buttonReset = NULL;
static GameWindow *windowMap = NULL;
static GameWindow *textEntryPlayerName = NULL;
static GameWindow *comboBoxSuperweapons = NULL;
static GameWindow *comboBoxStartingCash = NULL;
static GameWindow *checkBoxUnitLimit = NULL;
static GameWindow *comboBoxIncomeSharing = NULL;
static GameWindow *comboBoxTechRespawn = NULL;
static GameWindow *comboBoxSupplyPileLimit = NULL;
static GameWindow *comboBoxGameMode = NULL;
static GameWindow *comboBoxPlayer[MAX_SLOTS] = {NULL,NULL,NULL,NULL,
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

WindowLayout *skirmishMapSelectLayout = NULL;

static Int	initialGadgetDelay = 2;
static Bool justEntered = FALSE;
static Bool buttonPushed = FALSE;
static Bool stillNeedsToSetOptions = FALSE;
void skirmishUpdateSlotList( void );
static void populateSkirmishBattleHonors( void );
// The game speed dropdown, as logic frames a second against the normal 30.  Half speed to double.
// x0.75 is 22.5, which a whole frame rate cannot hold, so it runs at 23.  The preference keeps the
// frame rate, so one saved by the old 15 to 60 slider lands on the nearest entry.
static const Int SKIRMISH_GAME_SPEEDS[] = { 15, 23, 30, 45, 60 };
static const WideChar *SKIRMISH_GAME_SPEED_CAPTIONS[] = { u"x0.5", u"x0.75", u"x1", u"x1.5", u"x2" };
static const Int SKIRMISH_GAME_SPEED_COUNT = sizeof( SKIRMISH_GAME_SPEEDS ) / sizeof( SKIRMISH_GAME_SPEEDS[ 0 ] );

static Int nearestGameSpeedIndex( Int framesPerSecond )
{
	Int nearest = 0;
	for( Int i = 1; i < SKIRMISH_GAME_SPEED_COUNT; ++i )
	{
		if( abs( SKIRMISH_GAME_SPEEDS[ i ] - framesPerSecond ) < abs( SKIRMISH_GAME_SPEEDS[ nearest ] - framesPerSecond ) )
			nearest = i;
	}
	return nearest;
}

static Int selectedGameSpeed( void )
{
	GameWindow *comboBoxGameSpeed = TheWindowManager->winGetWindowFromId( parentSkirmishGameOptions, comboBoxGameSpeedID );
	Int selected = 0;
	GadgetComboBoxGetSelectedPos( comboBoxGameSpeed, &selected );
	return SKIRMISH_GAME_SPEEDS[ selected ];
}
Bool doUpdateSlotList = TRUE;

static Int getNextSelectablePlayer(Int start)
{
	if (!TheSkirmishGameInfo->amIHost())
		return -1;
	for (Int j=start; j<MAX_SLOTS; ++j)
	{
		GameSlot *slot = TheSkirmishGameInfo->getSlot(j);
		if (slot && slot->getStartPos() == -1 && (j==TheSkirmishGameInfo->getLocalSlotNum() || slot->isAI()))
		{
			return j;
		}
	}
	return -1;
}

SkirmishPreferences::SkirmishPreferences( void )
{
	// note, the superclass will put this in the right dir automatically, this is just a leaf name
	load("Skirmish.ini");
}

SkirmishPreferences::~SkirmishPreferences()
{
}

AsciiString SkirmishPreferences::getSlotList(void)
{
	return getAsciiString("SlotList", AsciiString::TheEmptyString);
}

void SkirmishPreferences::setSlotList(void)
{
	setAsciiString("SlotList", GameInfoToAsciiString(TheSkirmishGameInfo));
}

UnicodeString SkirmishPreferences::getUserName(void)
{
	UnicodeString ret;
	SkirmishPreferences::const_iterator it = find("UserName");
	if (it == end())
	{
		IPEnumeration IPs;
		ret.translate(IPs.getMachineName());
		return ret;
	}

	ret = QuotedPrintableToUnicodeString(it->second);
	ret.trim();
	if (ret.isEmpty())
	{
		IPEnumeration IPs;
		ret.translate(IPs.getMachineName());
		return ret;
	}
	
	return ret;
}

Int SkirmishPreferences::getPreferredColor(void)
{
	Int ret;
	SkirmishPreferences::const_iterator it = find("Color");
	if (it == end())
	{
		return -1;
	}

	ret = atoi(it->second.str());
	if (ret < -1 || ret >= TheMultiplayerSettings->getNumColors())
		ret = -1;

	return ret;
}

Int SkirmishPreferences::getPreferredFaction(void)
{
	Int ret;
	SkirmishPreferences::const_iterator it = find("PlayerTemplate");
	if (it == end())
	{
		return PLAYERTEMPLATE_RANDOM;
	}

	ret = atoi(it->second.str());
	if (ret < PLAYERTEMPLATE_MIN || ret >= ThePlayerTemplateStore->getPlayerTemplateCount())
		ret = PLAYERTEMPLATE_RANDOM;

	if (ret >= 0)
	{
		const PlayerTemplate *fac = ThePlayerTemplateStore->getNthPlayerTemplate(ret);
		if (!fac)
			ret = PLAYERTEMPLATE_RANDOM;
		else if (fac->getStartingBuilding().isEmpty())
			ret = PLAYERTEMPLATE_RANDOM;
	}

	return ret;
}

Bool SkirmishPreferences::usesSystemMapDir(void)
{
	OptionPreferences::const_iterator it = find("UseSystemMapDir");
	if (it == end())
		return TRUE;

	if (strcasecmp(it->second.str(), "yes") == 0) {
		return TRUE;
	}
	return FALSE;
}

AsciiString SkirmishPreferences::getPreferredMap(void)
{
	AsciiString ret;
	SkirmishPreferences::const_iterator it = find("Map");
	if (it == end())
	{
		ret = getDefaultMap(TRUE);
		return ret;
	}

	ret = QuotedPrintableToAsciiString(it->second);
	ret.trim();
	if (ret.isEmpty() || !isValidMap(ret, TRUE))
	{
		ret = getDefaultMap(TRUE);
		return ret;
	}
	
	return ret;
}

static const char superweaponRestrictionKey[] = "SuperweaponRestrict";

Int SkirmishPreferences::getSuperweaponRestriction(void) const
{
  const_iterator it = find(superweaponRestrictionKey);
  if (it == end())
  {
    return SUPERWEAPONS_ALLOW;
  }

  return SuperweaponRestrictionFromPreference( it->second );
}

void SkirmishPreferences::setSuperweaponRestriction( Int restriction )
{
  AsciiString val;
  val.format( "%d", restriction );
  (*this)[superweaponRestrictionKey] = val;
}

static const char startingCashKey[] = "StartingCash";
Money SkirmishPreferences::getStartingCash(void) const
{
  const_iterator it = find(startingCashKey);
  if (it == end())
  {
    return TheMultiplayerSettings->getDefaultStartingMoney();
  }
  
  Money money;
  money.deposit( strtoulAsWindows( it->second.str() ), FALSE  );
  
  return money;
}

void SkirmishPreferences::setStartingCash( const Money & startingCash )
{
  AsciiString option;
  
  option.format( "%d", startingCash.countMoney() );
  
  (*this)[startingCashKey] = option;
}



Bool SkirmishPreferences::write(void)
{
	if (!TheSkirmishGameInfo)
		return FALSE;

	AsciiString tmp;

	tmp.format("%d", TheSkirmishGameInfo->getConstSlot(0)->getColor());
	(*this)["Color"] = tmp;

	tmp.format("%d", TheSkirmishGameInfo->getConstSlot(0)->getPlayerTemplate());
	(*this)["PlayerTemplate"] = tmp;

	(*this)["Map"] = TheSkirmishGameInfo->getMap();

	(*this)["UserName"] = UnicodeStringToQuotedPrintable(TheSkirmishGameInfo->getConstSlot(0)->getName());

  setStartingCash( TheSkirmishGameInfo->getStartingCash() );
  setSuperweaponRestriction( TheSkirmishGameInfo->getSuperweaponRestriction() );
  setInt( "UnitLimit", TheSkirmishGameInfo->getUnitLimit() ? 1 : 0 );
  setInt( "IncomeSharing", TheSkirmishGameInfo->getIncomeSharing() );
  setInt( "TechRespawn", TheSkirmishGameInfo->getTechRespawn() );
  setInt( "SupplyPileLimit", TheSkirmishGameInfo->getSupplyPileLimit() );

	// Apocalypse's one seat is not the skirmish table the slot list keeps for next time
	if (!SkirmishApocalypse)
		setSlotList();

	setInt("FPS", selectedGameSpeed());

	return UserPreferences::write();
}

/*
static void playerTooltip(GameWindow *window,
													WinInstanceData *instData,
													UnsignedInt mouse)
{
	Int idx = -1;
	for (Int i=1; i<MAX_SLOTS; ++i)
	{
		if (window && window == GadgetComboBoxGetEditBox(comboBoxPlayer[i]))
		{
			idx = i;
			break;
		}
	}
	if (idx == -1)
		return;

	UnicodeString tooltip;
	/// @todo get this to display the correct tooltip for the player field CCB
	tooltip.format(TheGameText->fetch("TOOLTIP:LANPlayer") );
	TheMouse->setCursorTooltip( tooltip );
}
*/

void reallyDoStart( void )
{
	if (TheGameLogic->isInGame())
		TheGameLogic->clearGameData(FALSE);

	const Int maxFPS = selectedGameSpeed();
	DEBUG_LOG(("Game speed dropdown is at %d logic frames a second\n", maxFPS));

  TheWritableGlobalData->m_mapName = TheSkirmishGameInfo->getMap();
  TheSkirmishGameInfo->startGame(0);

		Bool isSkirmish = TRUE;
	const MapMetaData *md = TheMapCache->findMap(TheSkirmishGameInfo->getMap());
	if (md)
	{
		isSkirmish = md->m_isMultiplayer; // we can now select solo campaign maps in Skirmish.
	}

	if (isSkirmish)
	{
		InitRandom(TheSkirmishGameInfo->getSeed());

		GameMessage *msg = TheMessageStream->appendMessage( GameMessage::MSG_NEW_GAME );
		msg->appendIntegerArgument(GAME_SKIRMISH);
		msg->appendIntegerArgument(DIFFICULTY_NORMAL);	// not really used; just specified so we can add the game speed last
		msg->appendIntegerArgument(0);									// not really used; just specified so we can add the game speed last
		msg->appendIntegerArgument(maxFPS);							// FPS limit
	}
	else
	{
		/* A solo campaign map can be launched from the skirmish menu, and it runs as
			 GAME_SINGLE_PLAYER - which is seeded with 0 everywhere else in the game.  EA seeded it
			 with the skirmish lobby's seed here and nowhere else, so the same mission played from the
			 two menus was not the same mission. */
		InitRandom(0);

		GameMessage *msg = TheMessageStream->appendMessage( GameMessage::MSG_NEW_GAME );
		msg->appendIntegerArgument(GAME_SINGLE_PLAYER);
		msg->appendIntegerArgument(DIFFICULTY_NORMAL);	// not really used; just specified so we can add the game speed last
		msg->appendIntegerArgument(0);									// not really used; just specified so we can add the game speed last
		msg->appendIntegerArgument(maxFPS);							// FPS limit
	}
}

static MessageBoxReturnType cancelStartBecauseOfNoCD( void *userData )
{
	buttonPushed = FALSE;
	return MB_RETURN_CLOSE;
}

Bool IsFirstCDPresent(void)
{
#if !defined(_INTERNAL) && !defined(_DEBUG)
	return TheFileSystem->areMusicFilesOnCD();
#else
	return TRUE;
#endif
}

static MessageBoxReturnType checkCDCallback( void *userData )
{
	if (!IsFirstCDPresent())
	{
		return (IsFirstCDPresent())?MB_RETURN_CLOSE:MB_RETURN_KEEPOPEN;
	}
	else
	{
		gameStartCallback callback = (gameStartCallback)userData;
		if (callback)
			callback();
		return MB_RETURN_CLOSE;
	}
}

void CheckForCDAtGameStart( gameStartCallback callback )
{
	if (!IsFirstCDPresent())
	{
		// popup a dialog asking for a CD
		ExMessageBoxOkCancel(TheGameText->fetch("GUI:InsertCDPrompt"), TheGameText->fetch("GUI:InsertCDMessage"),
			(void *)callback, checkCDCallback, cancelStartBecauseOfNoCD);
	}
	else
	{
		callback();
	}
}

Bool sandboxOk = FALSE;
static void startPressed(void)
{

	Bool isReady = FALSE;
	Int playerCount = TheSkirmishGameInfo->getNumPlayers();
	AsciiString lowerMap = TheSkirmishGameInfo->getMap();
	lowerMap.toLower();
	std::map<AsciiString, MapMetaData>::iterator it = TheMapCache->find(lowerMap);
	if (it == TheMapCache->end())
	{
		buttonPushed = FALSE;
		MessageBoxOk(TheGameText->fetch("GUI:ErrorStartingGame"), TheGameText->fetch("GUI:CantFindMap"), NULL);
	}
	MapMetaData mmd = it->second;
	if(playerCount > mmd.m_numPlayers)
	{
		buttonPushed = FALSE;
		UnicodeString msg;
		msg.format(TheGameText->fetch("GUI:TooManyPlayers"), mmd.m_numPlayers);
		MessageBoxOk(TheGameText->fetch("GUI:ErrorStartingGame"), msg, NULL);
	}
	/*
  else if (playerCount < 2 && !sandboxOk)
  {
		sandboxOk = TRUE;
		MessageBoxOk(TheGameText->fetch("GUI:ErrorStartingGame"), TheGameText->fetch("GUI:SandboxWarning"), NULL);
  }
	*/
	else
	{
		isReady = TRUE;
	}
	
	if(isReady)
	{
		CheckForCDAtGameStart( reallyDoStart );
	}

}//void startPressed(void)

/////////////////////////////////////////////////////
// MapSelectorTooltip - shows tooltips for the tech buildings 
//											and supply depots
// Added By : Sadullah Nader
/////////////////////////////////////////////////////
void MapSelectorTooltip(GameWindow *window,
												WinInstanceData *instData,
												UnsignedInt mouse)
{
	Int x, y;
	x = LOLONGTOSHORT(mouse);
	y = HILONGTOSHORT(mouse);

	Int pixelX, pixelY;
	window->winGetScreenPosition(&pixelX, &pixelY);

	const Image *image = TheMappedImageCollection->findImageByName("TecBuilding");
	const Image *image2 = TheMappedImageCollection->findImageByName("Cash");


	ICoord2DList::iterator it = TheSupplyAndTechImageLocations.m_techPosList.begin();
	ICoord2DList::iterator it2 = TheSupplyAndTechImageLocations.m_supplyPosList.begin();
	
	if( image )
	{
		// Check to see if we mouse over a tech building 
		while(it != TheSupplyAndTechImageLocations.m_techPosList.end())
		{
			if ((x > (pixelX + it->x) && x < (pixelX + it->x + SUPPLY_TECH_SIZE)) 
				  && ( y > (pixelY + it->y) && y < (pixelY + it->y + SUPPLY_TECH_SIZE)))
			{
				TheMouse->setCursorTooltip( TheGameText->fetch("TOOLTIP:TechBuilding"), -1, NULL); //, 1.5f
				return;
			}
			it++;
		}
	}

	if (image2)
	{
		// Check to see if we mouse over a supply dock
		while (it2 != TheSupplyAndTechImageLocations.m_supplyPosList.end())
		{
			if ((x > (pixelX + it2->x) && x < (pixelX + it2->x + SUPPLY_TECH_SIZE))
					 && ( y > (pixelY + it2->y) && y < (pixelY + it2->y + SUPPLY_TECH_SIZE)))
			{
				TheMouse->setCursorTooltip( TheGameText->fetch("TOOLTIP:SupplyDock"), -1, NULL); // , 1.5f
				break;
			}
			it2++;
		}
	}
	
}


void positionStartSpotControls( GameWindow *win, GameWindow *mapWindow, Coord3D *pos, MapMetaData *mmd, GameWindow *buttonMapStartPositions[])
{
	if(!win || !mmd || !mapWindow || !buttonMapStartPositions)
		return;
	DEBUG_ASSERTCRASH(win && mmd,("positionStartSpotControls:: we don't have a window to position or any mapmetadata"));
	ICoord2D winMapSize, winMapPos, gadgetPos, gadgetSize;
	mapWindow->winGetSize(&winMapSize.x, &winMapSize.y);
	mapWindow->winGetScreenPosition(&winMapPos.x, &winMapPos.y);
	win->winGetSize(&gadgetSize.x, &gadgetSize.y);
	ICoord2D ul, lr;
	findDrawPositions(0,0, winMapSize.x, winMapSize.y,mmd->m_extent, &ul, &lr);
	Int smallWidth = lr.x - ul.x;
	Int smallHeight= lr.y - ul.y;
	
	// When we actually draw the map, save off it's screen position and use that instead of the map window's position/size
	Real position;
	position = (pos->x - mmd->m_extent.lo.x) / (mmd->m_extent.hi.x - mmd->m_extent.lo.x);
	gadgetPos.x = (position * smallWidth) - gadgetSize.x /2 + ul.x;// + winMapPos.x;

	position = (pos->y - mmd->m_extent.lo.y) / (mmd->m_extent.hi.y - mmd->m_extent.lo.y);
	gadgetPos.y = ((1- position) * smallHeight) - gadgetSize.y /2 + ul.y;// + winMapPos.y;
	
	

	// loop through and make sure we're not on top of anyone else
	for(Int i = 0; i < MAX_SLOTS; ++i)
	{
		if(buttonMapStartPositions[i] == win)
			break;
		ICoord2D tempPos;
		buttonMapStartPositions[i]->winGetScreenPosition(&tempPos.x, &tempPos.y);
		// we're inside the other gadget
		if(gadgetPos.x > tempPos.x && gadgetPos.x < tempPos.x + gadgetSize.x
				&& gadgetPos.y > tempPos.y && gadgetPos.y < tempPos.y + gadgetSize.y)
		{
			Int closerRight = tempPos.x + gadgetSize.x - gadgetPos.x;
			Int closerBottom = tempPos.y + gadgetSize.y - gadgetPos.y;
			// we're closer to the right then the bottom
			if( closerRight < closerBottom)
				gadgetPos.x = tempPos.x + gadgetSize.x + 1;
			else if( closerBottom < closerRight)
				gadgetPos.y = tempPos.y + gadgetSize.y + 1;
			else
			{
				gadgetPos.x = tempPos.x + gadgetSize.x + 1;
				gadgetPos.y = tempPos.y + gadgetSize.y + 1;
			}
		}
	}
	win->winSetPosition(gadgetPos.x,gadgetPos.y);
}
TechAndSupplyImages TheSupplyAndTechImageLocations;

void positionAdditionalImages( MapMetaData *mmd, GameWindow *mapWindow, Bool force)
{
	TheSupplyAndTechImageLocations.m_supplyPosList.clear();
	TheSupplyAndTechImageLocations.m_techPosList.clear();

	if( !mmd || !mapWindow || mapWindow->winIsHidden())
		return;
	static MapMetaData *prevMMD = NULL;
	if(force)
		prevMMD = NULL;
	// we already populated the supply and tech image locations.
	if(mmd == prevMMD)
		return;
	ICoord2D winMapSize, winMapPos;
	mapWindow->winGetSize(&winMapSize.x, &winMapSize.y);
	mapWindow->winGetScreenPosition(&winMapPos.x, &winMapPos.y);

	//SUPPLY_TECH_SIZE
	ICoord2D ul, lr;
	findDrawPositions(0,0, winMapSize.x, winMapSize.y,mmd->m_extent, &ul, &lr);
	Int smallWidth = lr.x - ul.x;
	Int smallHeight= lr.y - ul.y;
	
	Coord3DList::iterator it = mmd->m_supplyPositions.begin();
		// loop through and make sure we're not on top of anyone else
	while( it != mmd->m_supplyPositions.end())
	{
		
		ICoord2D markerPos;

		// When we actually draw the map, save off it's screen position and use that instead of the map window's position/size
		Real position;
		position = (it->x - mmd->m_extent.lo.x) / (mmd->m_extent.hi.x - mmd->m_extent.lo.x);
		markerPos.x = (position * smallWidth) - SUPPLY_TECH_SIZE /2 + ul.x;// + winMapPos.x;

		position = (it->y - mmd->m_extent.lo.y) / (mmd->m_extent.hi.y - mmd->m_extent.lo.y);
		markerPos.y = ((1- position) * smallHeight) - SUPPLY_TECH_SIZE /2 + ul.y;// + winMapPos.y;
		TheSupplyAndTechImageLocations.m_supplyPosList.push_front(markerPos);
		it++;
	}

	it = mmd->m_techPositions.begin();
		// loop through and make sure we're not on top of anyone else
	while( it != mmd->m_techPositions.end())
	{

		ICoord2D markerPos;
		// When we actually draw the map, save off it's screen position and use that instead of the map window's position/size
		Real position;
		position = (it->x - mmd->m_extent.lo.x) / (mmd->m_extent.hi.x - mmd->m_extent.lo.x);
		markerPos.x = (position * smallWidth) - SUPPLY_TECH_SIZE /2 + ul.x;// + winMapPos.x;

		position = (it->y - mmd->m_extent.lo.y) / (mmd->m_extent.hi.y - mmd->m_extent.lo.y);
		markerPos.y = ((1- position) * smallHeight) - SUPPLY_TECH_SIZE /2 + ul.y;// + winMapPos.y;
		TheSupplyAndTechImageLocations.m_techPosList.push_front(markerPos);
		it++;
	}


	//TheSupplyAndTechImageLocations
}

void positionStartSpots( AsciiString mapName, GameWindow *buttonMapStartPositions[], GameWindow *mapWindow)
{
	AsciiString lowerMap = mapName;
	lowerMap.toLower();
	std::map<AsciiString, MapMetaData>::iterator it = TheMapCache->find(lowerMap);
	if (it == TheMapCache->end())
	{
		mapWindow->winSetUserData(NULL);

		static const Image *unknownImage = NULL;
		if (!unknownImage)
			unknownImage = TheMappedImageCollection->findImageByName("UnknownMap");
		if (unknownImage)
		{
			mapWindow->winSetStatus(WIN_STATUS_IMAGE);
			mapWindow->winSetEnabledImage(0, unknownImage);
		}
		else
		{
			mapWindow->winClearStatus(WIN_STATUS_IMAGE);
		}

		positionAdditionalImages(NULL, mapWindow, TRUE);
		// i is used after the loop; VC6 for-scope let it escape.
		Int i;
		for (i = 0; i < MAX_SLOTS; ++i)
		{
			if (buttonMapStartPositions[i] != NULL)
			{
				buttonMapStartPositions[i]->winHide(TRUE);
			}
		}
	}
	else
	{
		MapMetaData mmd = it->second;
		
		Image *image = getMapPreviewImage(mapName);
		if (mapWindow != NULL) {
			mapWindow->winSetUserData((void *)TheMapCache->findMap(mapName));
			if(image)
			{
				mapWindow->winSetStatus(WIN_STATUS_IMAGE);
				mapWindow->winSetEnabledImage(0, image);
			}
			else
			{
				static const Image *unknownImage = NULL;
				if (!unknownImage)
					unknownImage = TheMappedImageCollection->findImageByName("UnknownMap");
				if (unknownImage)
				{
					mapWindow->winSetStatus(WIN_STATUS_IMAGE);
					mapWindow->winSetEnabledImage(0, unknownImage);
				}
				else
				{
					mapWindow->winClearStatus(WIN_STATUS_IMAGE);
				}
			}
		}

		positionAdditionalImages(&mmd, mapWindow, TRUE);

		AsciiString waypointName;				
		// The "hide the rest" loop below picks up where this one stopped, so i has
		// to outlive the VC6 for-scope.
		Int i;
		for(i = 0; i < mmd.m_numPlayers && mmd.m_isMultiplayer; ++i )
		{
			waypointName.format("Player_%d_Start", i+1); // start pos waypoints are 1-based
			WaypointMap::iterator wmIt = mmd.m_waypoints.find(waypointName);
			if( wmIt != mmd.m_waypoints.end())
			{
				Coord3D *pos = &wmIt->second;
				positionStartSpotControls(buttonMapStartPositions[i], mapWindow,pos, &mmd, buttonMapStartPositions);
				if (buttonMapStartPositions[i] != NULL)
				{
					buttonMapStartPositions[i]->winHide(FALSE);
				}
			}
			else
			{
				DEBUG_ASSERTCRASH(FALSE,("positionStartSpots:: someone messed with the map cash.  We couldn't find waypoint <%s> in map <%s>", waypointName.str(),lowerMap.str()));
			}
		}	
		// hide the rest
		for (; i < MAX_SLOTS; ++i)
		{
			if (buttonMapStartPositions[i] != NULL)
			{
				buttonMapStartPositions[i]->winHide(TRUE);
			}
		}
	}
}

void positionStartSpots( GameInfo *myGame, GameWindow *buttonMapStartPositions[], GameWindow *mapWindow)
{
	AsciiString localMapFname = myGame->getMap();
	if (!myGame->isGameInProgress())
	{
		Int localIdx = myGame->getLocalSlotNum();
		if (localIdx == -1)
			localMapFname = AsciiString::TheEmptyString;
		else
		{
			if (!myGame->getConstSlot(localIdx)->hasMap())
			{
				localMapFname = AsciiString::TheEmptyString;
			}
		}
	}
	positionStartSpots(localMapFname, buttonMapStartPositions, mapWindow);	
}

void updateMapStartSpots( GameInfo *myGame, GameWindow *buttonMapStartPositions[], Bool onLoadScreen )
{
	AsciiString lowerMap = myGame->getMap();
	lowerMap.toLower();
	std::map<AsciiString, MapMetaData>::iterator it = TheMapCache->find(lowerMap);
	if (it == TheMapCache->end())
	{
		for (Int i = 0; i < MAX_SLOTS; ++i)
    {
      if ( buttonMapStartPositions[i] != NULL )
      {
  			buttonMapStartPositions[i]->winHide(TRUE);
      }
    }
		return;
	}
	MapMetaData mmd = it->second;

	for(Int i = 0; i < MAX_SLOTS; ++i)
	{
    if ( buttonMapStartPositions[i] != NULL )
    {
		  GadgetButtonSetText(buttonMapStartPositions[i], UnicodeString::TheEmptyString);
		  if (!onLoadScreen)
		  {
			  buttonMapStartPositions[i]->winSetTooltip(TheGameText->fetch("TOOLTIP:StartPosition"));
		  }
    }
	}
	for( Int i = 0; i < MAX_SLOTS; ++i)
	{
    if ( buttonMapStartPositions[i] == NULL )
      continue;

		GameSlot *gs =myGame->getSlot(i);
		if(onLoadScreen)
		{
			if(gs->getApparentStartPos() >=0 && gs->getApparentStartPos() < mmd.m_numPlayers && gs->getPlayerTemplate() > PLAYERTEMPLATE_MIN )
			{
				AsciiString displayNumber;
				displayNumber.format("NUMBER:%d",i + 1);
				GadgetButtonSetText(buttonMapStartPositions[gs->getApparentStartPos()], TheGameText->fetch(displayNumber));
			}
		}
		else
		{
			if(gs->getStartPos() >=0 && gs->getStartPos() < mmd.m_numPlayers && gs->getPlayerTemplate() > PLAYERTEMPLATE_MIN )
			{
				AsciiString displayNumber;
				displayNumber.format("NUMBER:%d",i + 1);
				GadgetButtonSetText(buttonMapStartPositions[gs->getStartPos()], TheGameText->fetch(displayNumber));
				//Added By Sadullah Nader
				//Fix for no tooltips at start positions
				//added start position tooltip
				//Fixed again to show the right number , ie "i + 1"
				UnicodeString temp;
				temp.format(TheGameText->fetch("TOOLTIP:StartPositionN"), i + 1);
				buttonMapStartPositions[gs->getStartPos()]->winSetTooltip(temp);
			}
		}
	}
}

static void handlePlayerSelection(int index)
{
  if( index == 0 || index >=MAX_SLOTS)
    return;

	GameWindow *combo = comboBoxPlayer[index];
	Int playerType, selIndex;
	GadgetComboBoxGetSelectedPos(combo, &selIndex);
  UnicodeString title = GadgetComboBoxGetText(combo);
	playerType = (Int)(intptr_t)GadgetComboBoxGetItemData(combo, selIndex);
	GameInfo *myGame = TheSkirmishGameInfo;

	if (myGame)
	{
		GameSlot * slot = myGame->getSlot(index);
    if(!slot)
      return;
    slot->setState(SlotState(playerType), title);
    
	}
  //skirmishUpdateSlotList();
}

static void handleColorSelection(int index)
{
	GameWindow *combo = comboBoxColor[index];
	Int color, selIndex;
	GadgetComboBoxGetSelectedPos(combo, &selIndex);
	color = (Int)(intptr_t)GadgetComboBoxGetItemData(combo, selIndex);

	GameInfo *myGame = TheSkirmishGameInfo;

	if (myGame)
	{
		GameSlot * slot = myGame->getSlot(index);
    if(!slot)
      return;
		if (color == slot->getColor())
			return;

		if (color >= -1 && color < TheMultiplayerSettings->getNumColors())
		{
			Bool colorAvailable = TRUE;
			if(color != -1 )
			{
				for(Int i=0; i <MAX_SLOTS; i++)
				{
					GameSlot *checkSlot = myGame->getSlot(i);
					if(checkSlot && color == checkSlot->getColor() && slot != checkSlot)
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
  }
  //skirmishUpdateSlotList();
}

static void handlePlayerTemplateSelection(int index)
{
	GameWindow *combo = comboBoxPlayerTemplate[index];
	Int playerTemplate, selIndex;
	GadgetComboBoxGetSelectedPos(combo, &selIndex);
	playerTemplate = (Int)(intptr_t)GadgetComboBoxGetItemData(combo, selIndex);
	GameInfo *myGame = TheSkirmishGameInfo;

	if (myGame)
	{
		GameSlot * slot = myGame->getSlot(index);
    if(!slot)
      return;
		if (playerTemplate == slot->getPlayerTemplate())
			return;

		slot->setPlayerTemplate(playerTemplate);
	}
  //skirmishUpdateSlotList();
}

static void handleStartPositionSelection(int index, Int position)
{

	GameInfo *myGame = TheSkirmishGameInfo;

	if (myGame)
	{
		GameSlot * slot = myGame->getSlot(index);
		if (position == slot->getStartPos())
			return;
		if( position < 0)
		{
			slot->setStartPos(position);
			return;
		}

		Bool isAvailable = TRUE;
		for(Int i = 0; i < MAX_SLOTS; ++i)
		{
			if(i != index && myGame->getSlot(i)->getStartPos() == position)
			{
				isAvailable = FALSE;
				break;
			}
		}
		if(isAvailable)
		{
			slot->setStartPos(position);
		}
	}
}

static void handleTeamSelection(int index)
{
	GameWindow *combo = comboBoxTeam[index];
	Int team, selIndex;
	GadgetComboBoxGetSelectedPos(combo, &selIndex);
	team = (Int)(intptr_t)GadgetComboBoxGetItemData(combo, selIndex);
	GameInfo *myGame = TheSkirmishGameInfo;

	if (myGame)
	{
		GameSlot * slot = myGame->getSlot(index);
    if(!slot)
      return;
		if (team == slot->getTeamNumber())
			return;

		slot->setTeamNumber(team);
	}
  //skirmishUpdateSlotList();
}

static void handleStartingCashSelection()
{
  GameInfo *myGame = TheSkirmishGameInfo;
  
  if (myGame)
  {
    //
    // Uninitialised, and the getter leaves it alone when there is nothing selected or no window to
    // ask - so what got handed to GadgetComboBoxGetItemData as a list index was whatever had been
    // on the stack.
    //
    Int selIndex = -1;
    GadgetComboBoxGetSelectedPos(comboBoxStartingCash, &selIndex);
    if (selIndex < 0)
      return;

    Money startingCash;
    startingCash.deposit( (UnsignedInt)(uintptr_t)GadgetComboBoxGetItemData( comboBoxStartingCash, selIndex ), FALSE );
    myGame->setStartingCash( startingCash );
  }
}

static void handleSuperweaponSelection()
{
  GameInfo *myGame = TheSkirmishGameInfo;

  if (myGame && comboBoxSuperweapons)
  {
    myGame->setSuperweaponRestriction( SuperweaponRestrictionFromComboBox( comboBoxSuperweapons ) );
  }
}

static void handleIncomeSharingSelection()
{
  if (comboBoxIncomeSharing)
    TheSkirmishGameInfo->setIncomeSharing( IncomeSharingFromComboBox( comboBoxIncomeSharing ) );
}

static void handleTechRespawnSelection()
{
  if (comboBoxTechRespawn)
    TheSkirmishGameInfo->setTechRespawn( TechRespawnFromComboBox( comboBoxTechRespawn ) );
}

static void handleSupplyPileLimitSelection()
{
  if (comboBoxSupplyPileLimit)
    TheSkirmishGameInfo->setSupplyPileLimit( SupplyPileLimitFromComboBox( comboBoxSupplyPileLimit ) );
}

static void handleGameModeSelection()
{
  // emptying the box deselects it, and that must not read as a mode
  const Int mode = GameModeFromComboBox( comboBoxGameMode );
  if ( mode >= 0 )
    TheSkirmishGameInfo->setApocalypseMode( mode );
}

//-------------------------------------------------------------------------------------------------
/** Initialize the Gadgets Options Menu */
//-------------------------------------------------------------------------------------------------
void InitSkirmishGameGadgets( void )
{
	//Initialize the gadget IDs
	parentSkirmishGameOptionsID = TheNameKeyGenerator->nameToKey( AsciiString( "SkirmishGameOptionsMenu.wnd:SkirmishGameOptionsMenuParent" ) );
	buttonExitID = TheNameKeyGenerator->nameToKey( AsciiString( "SkirmishGameOptionsMenu.wnd:ButtonBack" ) );
	buttonStartID = TheNameKeyGenerator->nameToKey( AsciiString( "SkirmishGameOptionsMenu.wnd:ButtonStart" ) );
	textEntryMapDisplayID = TheNameKeyGenerator->nameToKey( AsciiString( "SkirmishGameOptionsMenu.wnd:TextEntryMapDisplay" ) );
	buttonSelectMapID = TheNameKeyGenerator->nameToKey( AsciiString( "SkirmishGameOptionsMenu.wnd:ButtonSelectMap" ) );
	buttonResetID = TheNameKeyGenerator->nameToKey( AsciiString( "SkirmishGameOptionsMenu.wnd:ButtonReset" ) );
	windowMapID = TheNameKeyGenerator->nameToKey( AsciiString( "SkirmishGameOptionsMenu.wnd:MapWindow" ) );
  comboBoxSuperweaponsID = TheNameKeyGenerator->nameToKey( AsciiString( "SkirmishGameOptionsMenu.wnd:ComboBoxSuperweapons" ) );
  comboBoxStartingCashID = TheNameKeyGenerator->nameToKey( AsciiString( "SkirmishGameOptionsMenu.wnd:ComboBoxStartingCash" ) );
  checkBoxUnitLimitID = TheNameKeyGenerator->nameToKey( AsciiString( "SkirmishGameOptionsMenu.wnd:CheckBoxUnitLimit" ) );
  comboBoxIncomeSharingID = TheNameKeyGenerator->nameToKey( AsciiString( "SkirmishGameOptionsMenu.wnd:ComboBoxIncomeSharing" ) );
  comboBoxTechRespawnID = TheNameKeyGenerator->nameToKey( AsciiString( "SkirmishGameOptionsMenu.wnd:ComboBoxTechRespawn" ) );
  comboBoxSupplyPileLimitID = TheNameKeyGenerator->nameToKey( AsciiString( "SkirmishGameOptionsMenu.wnd:ComboBoxSupplyPileLimit" ) );
  comboBoxGameModeID = TheNameKeyGenerator->nameToKey( AsciiString( "SkirmishGameOptionsMenu.wnd:ComboBoxGameMode" ) );

	// Initialize the pointers to our gadgets
	parentSkirmishGameOptions = TheWindowManager->winGetWindowFromId( NULL, parentSkirmishGameOptionsID );
	DEBUG_ASSERTCRASH(parentSkirmishGameOptions, ("Could not find the parentSkirmishGameOptions" ));
	buttonSelectMap = TheWindowManager->winGetWindowFromId( parentSkirmishGameOptions,buttonSelectMapID  );
	DEBUG_ASSERTCRASH(buttonSelectMap, ("Could not find the buttonSelectMap"));
	buttonStart = TheWindowManager->winGetWindowFromId( parentSkirmishGameOptions,buttonStartID  );
	DEBUG_ASSERTCRASH(buttonStart, ("Could not find the buttonStart"));
	buttonExit = TheWindowManager->winGetWindowFromId( parentSkirmishGameOptions,  buttonExitID);
	DEBUG_ASSERTCRASH(buttonExit, ("Could not find the buttonExit"));
	textEntryMapDisplay = TheWindowManager->winGetWindowFromId( parentSkirmishGameOptions, textEntryMapDisplayID );
	DEBUG_ASSERTCRASH(textEntryMapDisplay, ("Could not find the textEntryMapDisplay"));
	buttonReset = TheWindowManager->winGetWindowFromId( parentSkirmishGameOptions, buttonResetID );
	DEBUG_ASSERTCRASH(buttonReset, ("Could not find the buttonReset"));
  comboBoxSuperweapons = TheWindowManager->winGetWindowFromId( parentSkirmishGameOptions, comboBoxSuperweaponsID );
  DEBUG_ASSERTCRASH(comboBoxSuperweapons, ("Could not find the comboBoxSuperweapons"));
  if ( comboBoxSuperweapons )
    PopulateSuperweaponComboBox( comboBoxSuperweapons, TheSkirmishGameInfo, TRUE );
  comboBoxStartingCash = TheWindowManager->winGetWindowFromId( parentSkirmishGameOptions, comboBoxStartingCashID );
  DEBUG_ASSERTCRASH(comboBoxStartingCash, ("Could not find the comboBoxStartingCash"));
  PopulateStartingCashComboBox(comboBoxStartingCash, TheSkirmishGameInfo );
  // the fork's own control; a stale Run/Window has a layout without it, and that is survivable
  checkBoxUnitLimit = TheWindowManager->winGetWindowFromId( parentSkirmishGameOptions, checkBoxUnitLimitID );
  DEBUG_ASSERTCRASH(checkBoxUnitLimit, ("Could not find the checkBoxUnitLimit"));
  if ( checkBoxUnitLimit )
    UpdateUnitLimitCheckBox( checkBoxUnitLimit, TheSkirmishGameInfo, TRUE );
  comboBoxIncomeSharing = TheWindowManager->winGetWindowFromId( parentSkirmishGameOptions, comboBoxIncomeSharingID );
  DEBUG_ASSERTCRASH(comboBoxIncomeSharing, ("Could not find the comboBoxIncomeSharing"));
  if ( comboBoxIncomeSharing )
    PopulateIncomeSharingComboBox( comboBoxIncomeSharing, TheSkirmishGameInfo, TRUE );
  comboBoxTechRespawn = TheWindowManager->winGetWindowFromId( parentSkirmishGameOptions, comboBoxTechRespawnID );
  DEBUG_ASSERTCRASH(comboBoxTechRespawn, ("Could not find the comboBoxTechRespawn"));
  if ( comboBoxTechRespawn )
    PopulateTechRespawnComboBox( comboBoxTechRespawn, TheSkirmishGameInfo, TRUE );
  comboBoxSupplyPileLimit = TheWindowManager->winGetWindowFromId( parentSkirmishGameOptions, comboBoxSupplyPileLimitID );
  DEBUG_ASSERTCRASH(comboBoxSupplyPileLimit, ("Could not find the comboBoxSupplyPileLimit"));
  if ( comboBoxSupplyPileLimit )
    PopulateSupplyPileLimitComboBox( comboBoxSupplyPileLimit, TheSkirmishGameInfo, TRUE );
  // only Apocalypse's room has the zombie flow row, so this is NULL in the skirmish one: a standard
  // skirmish is the other button on the main menu
  comboBoxGameMode = TheWindowManager->winGetWindowFromId( parentSkirmishGameOptions, comboBoxGameModeID );
  if ( SkirmishApocalypse )
    PopulateGameModeComboBox( comboBoxGameMode, APOCALYPSE_WAVES, TheSkirmishGameInfo, TRUE );

	textEntryPlayerNameID = TheNameKeyGenerator->nameToKey( AsciiString( "SkirmishGameOptionsMenu.wnd:TextEntryPlayerName" ) );
  textEntryPlayerName = TheWindowManager->winGetWindowFromId( NULL, textEntryPlayerNameID );
	DEBUG_ASSERTCRASH(textEntryPlayerName, ("Could not find the textEntryPlayerName" ));
	
	windowMap = TheWindowManager->winGetWindowFromId( parentSkirmishGameOptions,windowMapID  );
	DEBUG_ASSERTCRASH(windowMap, ("Could not find the SkirmishGameOptionsMenu.wnd:MapWindow" ));

	windowMap->winSetTooltipFunc(MapSelectorTooltip);

	for (Int i = 0; i < MAX_SLOTS; i++)
	{
		AsciiString tmpString;
		tmpString.format("SkirmishGameOptionsMenu.wnd:ComboBoxPlayer%d", i);
		if(i != 0)
    {
      comboBoxPlayerID[i] = TheNameKeyGenerator->nameToKey( tmpString );
		  comboBoxPlayer[i] = TheWindowManager->winGetWindowFromId( parentSkirmishGameOptions, comboBoxPlayerID[i] );
		  GadgetComboBoxReset(comboBoxPlayer[i]);
		  //GadgetComboBoxGetEditBox(comboBoxPlayer[i])->winSetTooltipFunc(playerTooltip);
    }    
		Color white = GameMakeColor( 255, 255, 255, 255 );

		if( i == 0 )
		{
      GadgetTextEntrySetText(textEntryPlayerName, TheGameText->fetch("GUI:Player") );
			//GadgetComboBoxAddEntry(comboBoxPlayer[i], TheGameText->fetch( "GUI:Start" ), white);
			//GadgetComboBoxSetSelectedPos(comboBoxPlayer[i],0);
		}
		else
		{
			PopulatePlayerSlotComboBox(comboBoxPlayer[i], white, TRUE);
		}

		tmpString.format("SkirmishGameOptionsMenu.wnd:ComboBoxColor%d", i);
		comboBoxColorID[i] = TheNameKeyGenerator->nameToKey( tmpString );
		comboBoxColor[i] = TheWindowManager->winGetWindowFromId( parentSkirmishGameOptions, comboBoxColorID[i] );
		DEBUG_ASSERTCRASH(comboBoxColor[i], ("Could not find the comboBoxColor[%d]",i ));
		
		tmpString.format("SkirmishGameOptionsMenu.wnd:ComboBoxPlayerTemplate%d", i);
		comboBoxPlayerTemplateID[i] = TheNameKeyGenerator->nameToKey( tmpString );
		comboBoxPlayerTemplate[i] = TheWindowManager->winGetWindowFromId( parentSkirmishGameOptions, comboBoxPlayerTemplateID[i] );
		DEBUG_ASSERTCRASH(comboBoxPlayerTemplate[i], ("Could not find the comboBoxPlayerTemplate[%d]",i ));

		// add tooltips to the player template combobox and listbox
		comboBoxPlayerTemplate[i]->winSetTooltipFunc(playerTemplateComboBoxTooltip);
		GadgetComboBoxGetListBox(comboBoxPlayerTemplate[i])->winSetTooltipFunc(playerTemplateListBoxTooltip);

		tmpString.format("SkirmishGameOptionsMenu.wnd:ComboBoxTeam%d", i);
		comboBoxTeamID[i] = TheNameKeyGenerator->nameToKey( tmpString );
		comboBoxTeam[i] = TheWindowManager->winGetWindowFromId( parentSkirmishGameOptions, comboBoxTeamID[i] );
		DEBUG_ASSERTCRASH(comboBoxTeam[i], ("Could not find the comboBoxTeam[%d]",i ));
		

//		tmpString.format("SkirmishGameOptionsMenu.wnd:ButtonStartPosition%d", i);
//		buttonStartPositionID[i] = TheNameKeyGenerator->nameToKey( tmpString );
//		buttonStartPosition[i] = TheWindowManager->winGetWindowFromId( parentSkirmishGameOptions, buttonStartPositionID[i] );
//		DEBUG_ASSERTCRASH(buttonStartPosition[i], ("Could not find the ButtonStartPosition[%d]",i ));
//
		tmpString.format("SkirmishGameOptionsMenu.wnd:ButtonMapStartPosition%d", i);
		buttonMapStartPositionID[i] = TheNameKeyGenerator->nameToKey( tmpString );
		buttonMapStartPosition[i] = TheWindowManager->winGetWindowFromId( parentSkirmishGameOptions, buttonMapStartPositionID[i] );
		DEBUG_ASSERTCRASH(buttonMapStartPosition[i], ("Could not find the ButtonMapStartPosition[%d]",i ));
	}
   
	for (Int i = 0; i < MAX_SLOTS; ++i)
	{
		PopulateColorComboBox(i, comboBoxColor, TheSkirmishGameInfo );
		GadgetComboBoxSetSelectedPos(comboBoxColor[i], 0);
		PopulatePlayerTemplateComboBox(i, comboBoxPlayerTemplate, TheSkirmishGameInfo, FALSE );
		PopulateTeamComboBox(i, comboBoxTeam, TheSkirmishGameInfo );

//		if (buttonStartPosition[i])
//			buttonStartPosition[i]->winHide(TRUE); // not using these right now
	}
	
	populateSkirmishBattleHonors();

	// the settings page and the map info list share one rectangle; this opens on the info list.
	// Apocalypse's room has no info list: its settings page is the whole form, always shown.
	if ( !SkirmishApocalypse )
		InitLobbyTabs( parentSkirmishGameOptions, "SkirmishGameOptionsMenu.wnd", "TabInfo", "ListboxInfo" );
}

void skirmishUpdateSlotList( void )
{
  if(doUpdateSlotList)
  {
    doUpdateSlotList = FALSE;

	  AsciiString lowerMap = TheSkirmishGameInfo->getMap();
	  lowerMap.toLower();
	  std::map<AsciiString, MapMetaData>::iterator it = TheMapCache->find(lowerMap);
	  if (it != TheMapCache->end())
	  {
      GadgetStaticTextSetText(textEntryMapDisplay, it->second.m_displayName);
    }
    GadgetTextEntrySetText( textEntryPlayerName, TheSkirmishGameInfo->getSlot(0)->getName() );
    UpdateSlotList( TheSkirmishGameInfo, comboBoxPlayer,
										comboBoxColor, comboBoxPlayerTemplate,
									  comboBoxTeam, NULL, buttonStart, buttonMapStartPosition );
		updateMapStartSpots(TheSkirmishGameInfo, buttonMapStartPosition, FALSE);
    doUpdateSlotList = TRUE;
  }
}
void updateSkirmishGameOptions( void );
void skirmishPositionStartSpots( void )
{
	positionStartSpots( TheSkirmishGameInfo, buttonMapStartPosition, windowMap);

	updateSkirmishGameOptions();
}
//-------------------------------------------------------------------------------------------------
/** Init TextEntryMapDisplay */
//-------------------------------------------------------------------------------------------------
void initSkirmishGameOptions( void )
{

}

static const char *layoutFilename = "SkirmishGameOptionsMenu.wnd";
static const char *parentName = "SkirmishGameOptionsMenuParent";
static const char *gadgetsToHide[] =
{
	"TextEntryPlayerName",
	"StaticTextPlayers",
	"StaticTextColor",
	"StaticTextTeam",
	"StaticTextFaction",
	NULL // keep this last
};
static const char *perPlayerGadgetsToHide[] =
{
	"ComboBoxPlayer",
	"ComboBoxTeam",
	"ComboBoxColor",
	"ComboBoxPlayerTemplate",
	NULL // keep this last
};

//-------------------------------------------------------------------------------------------------
/** Update options on screen */
//-------------------------------------------------------------------------------------------------
void updateSkirmishGameOptions( void )
{
	Bool isSkirmish = TRUE;
	const MapMetaData *md = TheMapCache->findMap(TheSkirmishGameInfo->getMap());
	if (md)
	{
		isSkirmish = md->m_isMultiplayer; // we can now select solo campaign maps in Skirmish.
    GadgetStaticTextSetText(textEntryMapDisplay, md->m_displayName);
	}
	else
	{
		DEBUG_CRASH(("map not found, should not happen"));
		UnicodeString mapDisplay;
		mapDisplay.translate(AsciiString(TheSkirmishGameInfo->getMap().str()));
		GadgetStaticTextSetText(textEntryMapDisplay, mapDisplay);
	}
	if (isSkirmish)
	{
		ShowUnderlyingGUIElements(TRUE, layoutFilename, parentName, gadgetsToHide, perPlayerGadgetsToHide );
	}
	else
	{
		ShowUnderlyingGUIElements(FALSE, layoutFilename, parentName, gadgetsToHide, perPlayerGadgetsToHide );
		for (Int i=1; i<MAX_SLOTS; ++i)
		{
			GadgetComboBoxSetSelectedPos(comboBoxPlayer[i], 0);
		}
	}

  if ( comboBoxSuperweapons )
    UpdateSuperweaponComboBox( comboBoxSuperweapons, TheSkirmishGameInfo, TRUE );
  if ( checkBoxUnitLimit )
    UpdateUnitLimitCheckBox( checkBoxUnitLimit, TheSkirmishGameInfo, TRUE );
  if ( comboBoxIncomeSharing )
    UpdateIncomeSharingComboBox( comboBoxIncomeSharing, TheSkirmishGameInfo, TRUE );
  if ( comboBoxTechRespawn )
    UpdateTechRespawnComboBox( comboBoxTechRespawn, TheSkirmishGameInfo, TRUE );
  if ( comboBoxSupplyPileLimit )
    UpdateSupplyPileLimitComboBox( comboBoxSupplyPileLimit, TheSkirmishGameInfo, TRUE );
  Int itemCount = GadgetComboBoxGetLength(comboBoxStartingCash);
  Int index;
  for ( index = 0; index < itemCount; index++ )
  {
    Int value  = (Int)(intptr_t)GadgetComboBoxGetItemData(comboBoxStartingCash, index);
    if ( value == TheSkirmishGameInfo->getStartingCash().countMoney() )
    {
      GadgetComboBoxSetSelectedPos(comboBoxStartingCash, index, TRUE);
      break;
    }
  }

  // see LanGameOptionsMenu: an amount from the player's INI that the list does not carry
  if ( index == itemCount )
    PopulateStartingCashComboBox( comboBoxStartingCash, TheSkirmishGameInfo );

  if ( SkirmishApocalypse )
    UpdateGameModeComboBox( comboBoxGameMode, TheSkirmishGameInfo, TRUE );
}

//-------------------------------------------------------------------------------------------------
/** Initialize the Skirmish Game Options Menu */
//-------------------------------------------------------------------------------------------------
void SkirmishGameOptionsMenuInit( WindowLayout *layout, void *userData )
{
	if (TheGameEngine->getQuitting())
		return;

	stillNeedsToSetOptions = FALSE;

	comboBoxGameSpeedID = TheNameKeyGenerator->nameToKey( AsciiString( "SkirmishGameOptionsMenu.wnd:ComboBoxGameSpeed" ) );
	
	sandboxOk = FALSE;
  doUpdateSlotList = FALSE;
  if( !TheSkirmishGameInfo )
	{
    TheSkirmishGameInfo = NEW SkirmishGameInfo;
		SignalUIInteraction(SHELL_SCRIPT_HOOK_SKIRMISH_OPENED);
	}
  else if( TheSkirmishGameInfo->isInGame() )
	{
    TheSkirmishGameInfo->endGame();
	}
	else
	{
		SignalUIInteraction(SHELL_SCRIPT_HOOK_SKIRMISH_OPENED);
	}
  TheSkirmishGameInfo->init();  
  TheSkirmishGameInfo->clearSlotList();
	TheSkirmishGameInfo->reset();
  Int localIP = TheSkirmishGameInfo->getSlot(0)->getIP();
	TheSkirmishGameInfo->setLocalIP(localIP);

  TheSkirmishGameInfo->enterGame();

  EnableSlotListUpdates(FALSE);
	//initialize the gadgets
	InitSkirmishGameGadgets();
  EnableSlotListUpdates(TRUE);

  SkirmishPreferences prefs;
  GameSlot gSlot;
  gSlot.setName(prefs.getUserName());
  gSlot.setState( SLOT_PLAYER, prefs.getUserName() );
  gSlot.setColor(prefs.getPreferredColor());
  gSlot.setPlayerTemplate(prefs.getPreferredFaction());
  TheSkirmishGameInfo->setSlot(0,gSlot);

	// Apocalypse is the player alone against the zombies: no computer seat, and none of the last
	// skirmish's from the slot list, so every seat past his own stays closed
	if (!SkirmishApocalypse)
	{
		SkirmishBattleHonors honors;
		if (honors.getWins() > 10)
			gSlot.setState(SLOT_BRUTAL_AI);
		else if (honors.getWins() > 5)
			gSlot.setState(SLOT_MED_AI);
		else
			gSlot.setState(SLOT_EASY_AI);
		TheSkirmishGameInfo->setSlot(1, gSlot);

		ParseAsciiStringToGameInfo(TheSkirmishGameInfo, prefs.getSlotList());
	}
	// the slot list carries the last game's mode; the button that opened the room decides it instead,
	// waves unless -apocalypse 2 asked for the stream
	TheSkirmishGameInfo->setApocalypseMode( !SkirmishApocalypse ? APOCALYPSE_OFF
		: ( TheGlobalData->m_apocalypseMode == APOCALYPSE_CONTINUOUS ? APOCALYPSE_CONTINUOUS : APOCALYPSE_WAVES ) );
	// -seed repeats a match set up here too, as it does -autoskirmish's (GameEngine.cpp): the same match from the
	// same choices
	TheSkirmishGameInfo->setSeed((TheGlobalData->m_fixedSeed >= 0) ? TheGlobalData->m_fixedSeed : Clock_Milliseconds_Coarse());

	UnsignedInt isPreorder = 0;
	GetUnsignedIntFromRegistry("", "Preorder", isPreorder);
	if (isPreorder != 0)
	{
		TheSkirmishGameInfo->markPlayerAsPreorder(0);
	}

  TheSkirmishGameInfo->setStartingCash( prefs.getStartingCash() );
  TheSkirmishGameInfo->setSuperweaponRestriction( prefs.getSuperweaponRestriction() );
  TheSkirmishGameInfo->setUnitLimit( prefs.getInt( "UnitLimit", 0 ) != 0 );
  TheSkirmishGameInfo->setIncomeSharing( prefs.getInt( "IncomeSharing", INCOME_SHARING_OFF ) );
  TheSkirmishGameInfo->setTechRespawn( prefs.getInt( "TechRespawn", 0 ) );
  TheSkirmishGameInfo->setSupplyPileLimit( prefs.getInt( "SupplyPileLimit", 0 ) );
  // Pro Rules are for people playing each other; a skirmish against the computer never has them.
  TheSkirmishGameInfo->setProRules( FALSE );
 
  TheSkirmishGameInfo->setMap(prefs.getPreferredMap());
	const MapMetaData *md = TheMapCache->findMap(TheSkirmishGameInfo->getMap());
	if (!md)
	{
		TheSkirmishGameInfo->setMapCRC(0);
		TheSkirmishGameInfo->setMapSize(0);
	}
	else
	{
		TheSkirmishGameInfo->setMapCRC(md->m_CRC);
		TheSkirmishGameInfo->setMapSize(md->m_filesize);
	}

	AsciiString lowerMap = prefs.getPreferredMap();
	lowerMap.toLower();
	std::map<AsciiString, MapMetaData>::iterator it = TheMapCache->find(lowerMap);
	if (it != TheMapCache->end())
	{
    GadgetStaticTextSetText(textEntryMapDisplay, it->second.m_displayName);
  }

	skirmishPositionStartSpots();
	//updateSkirmishGameOptions();
	//initSkirmishGameOptions();

	// set up the game speed dropdown
	GameWindow *comboBoxGameSpeed = TheWindowManager->winGetWindowFromId( parentSkirmishGameOptions, comboBoxGameSpeedID );
	GadgetComboBoxReset( comboBoxGameSpeed );
	for( Int i = 0; i < SKIRMISH_GAME_SPEED_COUNT; ++i )
		GadgetComboBoxAddEntry( comboBoxGameSpeed, UnicodeString( SKIRMISH_GAME_SPEED_CAPTIONS[ i ] ), comboBoxGameSpeed->winGetEnabledTextColor() );
	GadgetComboBoxSetSelectedPos( comboBoxGameSpeed, nearestGameSpeedIndex( prefs.getInt( "FPS", LOGICFRAMES_PER_SECOND ) ) );
	buttonStart->winSetText(TheGameText->fetch("GUI:Start"));
	/* hey, for now we're also going to disable the map select button until it doesn't crash */
	//buttonSelectMap->winEnable( FALSE );
	//buttonSelectMap->winEnable( TRUE );
	//updateSkirmishGameOptions();

	for (Int i = 0; i < MAX_SLOTS; ++i)
	{
		comboBoxColor[i]->winEnable(TRUE);
		comboBoxPlayerTemplate[i]->winEnable(TRUE);
		comboBoxTeam[i]->winEnable(TRUE);
	}

	// Show the Menu
	layout->hide( FALSE );
	
	// Set Keyboard to Main Parent
	TheWindowManager->winSetFocus( parentSkirmishGameOptions );

	// animate controls
  TheShell->showShellMap(TRUE);
//	TheShell->registerWithAnimateManager(buttonExit, WIN_ANIMATION_SLIDE_RIGHT, TRUE, 1);
  doUpdateSlotList = TRUE;
  skirmishUpdateSlotList();
	justEntered = TRUE;
	initialGadgetDelay = 2;
	GameWindow *win = TheWindowManager->winGetWindowFromId(NULL, TheNameKeyGenerator->nameToKey("SkirmishGameOptionsMenu.wnd:SubParent"));
	if(win)
		win->winHide(TRUE);
	buttonPushed = FALSE;
}// void SkirmishGameOptionsMenuInit( WindowLayout *layout, void *userData )

//-------------------------------------------------------------------------------------------------
/** This is called when a shutdown is complete for this menu */
//-------------------------------------------------------------------------------------------------
static void shutdownComplete( WindowLayout *layout )
{

	// hide the layout
	layout->hide( TRUE );
  TheShell->shutdownComplete( layout );

	// our shutdown is complete
	// what the munkees does this do?

	//TheShell->shutdownComplete( layout, (LANnextScreen != NULL) );

	//if (LANnextScreen != NULL)
	//{
	//	TheShell->push(LANnextScreen);
	//}

	//LANnextScreen = NULL;

}  // end if

//-------------------------------------------------------------------------------------------------
/** Skirmish Game Options menu shutdown method */
//-------------------------------------------------------------------------------------------------
void SkirmishGameOptionsMenuShutdown( WindowLayout *layout, void *userData )
{
	SignalUIInteraction(SHELL_SCRIPT_HOOK_SKIRMISH_CLOSED);

	ShutdownLobbyTabs();

	TheMouse->setCursor(Mouse::ARROW);
	TheMouse->setMouseText(UnicodeString::TheEmptyString,NULL,NULL);
  // if we are shutting down for an immediate pop, skip the animations
	Bool popImmediate = *(Bool *)userData;
	if( popImmediate )
	{

		shutdownComplete( layout );
		return;

	}  //end if

	TheShell->reverseAnimatewindow();

	
	// hide menu
//	layout->hide( TRUE );

	// our shutdown is complete
	TheTransitionHandler->reverse("SkirmishGameOptionsMenuFade");
	
}  // void SkirmishGameOptionsMenuShutdown( WindowLayout *layout, void *userData )

//-------------------------------------------------------------------------------------------------
/** Skirmish Game Options menu update method */
//-------------------------------------------------------------------------------------------------
void SkirmishGameOptionsMenuUpdate( WindowLayout * layout, void *userData)
{
	if (TheGameLogic->isInShellGame() && TheGameLogic->getFrame() == 1)
	{
		SignalUIInteraction(SHELL_SCRIPT_HOOK_SKIRMISH_ENTERED_FROM_GAME);
	}

	if(justEntered)
	{
		if(initialGadgetDelay == 1)
		{
			stillNeedsToSetOptions = TRUE;
			TheWindowManager->winSetFocus( parentSkirmishGameOptions );
			initialGadgetDelay = 2;
			justEntered = FALSE;
		}
		else
			initialGadgetDelay--;
	}

	// Coming back from a game, the shell map will be reloading,
	// in which case this transition isn't wanted until the load is done.
	if(stillNeedsToSetOptions && !TheGameLogic->isLoadingMap())
	{
		TheTransitionHandler->setGroup("SkirmishGameOptionsMenuFade");
		stillNeedsToSetOptions = FALSE;
	}

	if(TheShell->isAnimFinished() && TheTransitionHandler->isFinished())
			TheShell->shutdownComplete( layout );
}// void SkirmishGameOptionsMenuUpdate( WindowLayout * layout, void *userData)

//-------------------------------------------------------------------------------------------------
/** Skirmish Game Options menu input callback */
//-------------------------------------------------------------------------------------------------
WindowMsgHandledType SkirmishGameOptionsMenuInput( GameWindow *window, UnsignedInt msg,
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
				case KEY_ESC:
				{
					//
					// send a simulated selected event to the parent window of the
					// back/exit button
					//
					if( BitTest( state, KEY_STATE_UP ) )
					{
						TheWindowManager->winSendSystemMsg( window, GBM_SELECTED, 
																							(WindowMsgData)buttonExit, buttonExitID );
					}  // end if
					// don't let key fall through anywhere else
					return MSG_HANDLED;
				}  // end escape
			}  // end switch( key )
		}  // end char
		break;
	}  // end switch( msg )
	return MSG_IGNORED;
}//WindowMsgHandledType SkirmishGameOptionsMenuInput( GameWindow *window, UnsignedInt msg,


//-------------------------------------------------------------------------------------------------
/** Skirmish Game Options menu window system callback */
//-------------------------------------------------------------------------------------------------
WindowMsgHandledType SkirmishGameOptionsMenuSystem( GameWindow *window, UnsignedInt msg, 
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
				GameWindow *control = (GameWindow *)mData1;
				Int controlID = control->winGetWindowId();
        if ( controlID == comboBoxStartingCashID )
        {
          handleStartingCashSelection();
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
            else if (controlID == comboBoxPlayerID[i])
            {
              handlePlayerSelection(i);
            }
				  }
        }
				sandboxOk = FALSE;
        skirmishUpdateSlotList();
        break;
			}// case GCM_SELECTED:
		//-------------------------------------------------------------------------------------------------
		case GBM_SELECTED:
			{
				
				GameWindow *control = (GameWindow *)mData1;
				Int controlID = control->winGetWindowId();
///				static NameKeyType buttonResetFPSID = TheNameKeyGenerator->nameToKey( AsciiString( "SkirmishGameOptionsMenu.wnd:ButtonResetFPS" ) );
				if(buttonPushed)
					break;
				if ( controlID == checkBoxUnitLimitID )
				{
					TheSkirmishGameInfo->setUnitLimit( GadgetCheckBoxIsChecked( control ) );
					break;
				}
				if ( LobbyTabClicked( (NameKeyType)controlID ) )
					break;
				if ( controlID == buttonExitID )
				{
					buttonPushed = TRUE;
          SkirmishPreferences prefs;
          prefs.write();
					if( skirmishMapSelectLayout )
						{
							skirmishMapSelectLayout->destroyWindows();
							skirmishMapSelectLayout->deleteInstance();
							skirmishMapSelectLayout = NULL;
						}
					TheShell->pop();
          if (TheGameInfo == TheSkirmishGameInfo)
            TheGameInfo = NULL;	// or it is left pointing at freed memory
          delete TheSkirmishGameInfo;
          TheSkirmishGameInfo = NULL;

				} //if ( controlID == buttonBack )
//				else if ( controlID == buttonResetFPSID )
//				{
//					static NameKeyType sliderGameSpeedID = TheNameKeyGenerator->nameToKey( AsciiString( "SkirmishGameOptionsMenu.wnd:SliderGameSpeed" ) );
//					GameWindow *sliderGameSpeed = TheWindowManager->winGetWindowFromId( parentSkirmishGameOptions, sliderGameSpeedID );
//					GadgetSliderSetPosition( sliderGameSpeed, TheGlobalData->m_framesPerSecondLimit );
//				}
				else if ( controlID == buttonSelectMapID )
				{
					sandboxOk = FALSE;
					//buttonBack->winEnable( false );
					skirmishMapSelectLayout = TheWindowManager->winCreateLayout( AsciiString( "Menus/SkirmishMapSelectMenu.wnd" ) );
					skirmishMapSelectLayout->runInit();
					skirmishMapSelectLayout->hide( FALSE );
					skirmishMapSelectLayout->bringForward();

				}
				else if ( controlID == buttonStartID )
				{
					buttonPushed = TRUE;
          SkirmishPreferences prefs;
          prefs.write();
					startPressed();
				}
				else if ( controlID == buttonResetID )
				{
					SkirmishBattleHonors stats;
					stats.clear();
					stats.write();
					populateSkirmishBattleHonors();
				}
				else
				{
					for (Int i = 0; i < MAX_SLOTS; i++)
					{
						if (controlID == buttonMapStartPositionID[i])
						{
							Int playerIdxInPos = -1;
							for (Int j=0; j<MAX_SLOTS; ++j)
							{
								GameSlot *slot = TheSkirmishGameInfo->getSlot(j);
								if (slot && slot->getStartPos() == i)
								{
									playerIdxInPos = j;
									break;
								}
							}
							if (playerIdxInPos >= 0)
							{
								GameSlot *slot = TheSkirmishGameInfo->getSlot(playerIdxInPos);
								if (playerIdxInPos == TheSkirmishGameInfo->getLocalSlotNum() || (TheSkirmishGameInfo->amIHost() && slot && slot->isAI()))
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
									nextPlayer = TheSkirmishGameInfo->getLocalSlotNum();
								handleStartPositionSelection(nextPlayer, i);
							}
							skirmishUpdateSlotList();
							sandboxOk = FALSE;
						}
					}
				}
				break;
			}// case GBM_SELECTED:
		//-------------------------------------------------------------------------------------------------
		case GBM_SELECTED_RIGHT:
		{
			GameWindow *control = (GameWindow *)mData1;
			Int controlID = control->winGetWindowId();
			for (Int i = 0; i < MAX_SLOTS; i++)
			{
				if (controlID == buttonMapStartPositionID[i])
				{
					Int playerIdxInPos = -1;
					for (Int j=0; j<MAX_SLOTS; ++j)
					{
						GameSlot *slot = TheSkirmishGameInfo->getSlot(j);
						if (slot && slot->getStartPos() == i)
						{
							playerIdxInPos = j;
							break;
						}
					}
					if (playerIdxInPos >= 0)
					{
						GameSlot *slot = TheSkirmishGameInfo->getSlot(playerIdxInPos);
						if (playerIdxInPos == TheSkirmishGameInfo->getLocalSlotNum() || (TheSkirmishGameInfo->amIHost() && slot && slot->isAI()))
						{
							// it's one of my type.  Remove it.
							handleStartPositionSelection(playerIdxInPos, -1);
						}
						skirmishUpdateSlotList();
						sandboxOk = FALSE;
					}
				}
			}					
			break;
		}
		//-------------------------------------------------------------------------------------------------
	  case GEM_EDIT_DONE:
	  case GEM_UPDATE_TEXT:
      {
        GameWindow *control = (GameWindow *)mData1;
				Int controlID = control->winGetWindowId();
        if( controlID == textEntryPlayerNameID)
        {
          TheSkirmishGameInfo->getSlot(0)->setName(GadgetTextEntryGetText(textEntryPlayerName));
          //GadgetTextEntrySetText( textEntryPlayerName, TheSkirmishGameInfo->getSlot(0)->getName() );
        }
        break;
      }

		//-------------------------------------------------------------------------------------------------
		default:
			return MSG_IGNORED;
	}//Switch
	return MSG_HANDLED;
}

void populateSkirmishBattleHonors(void)
{
	GameWindow *list = TheWindowManager->winGetWindowFromId(NULL, NAMEKEY("SkirmishGameOptionsMenu.wnd:ListboxInfo"));
	if (!list)
		return;

	SkirmishBattleHonors stats;

	list->winSetTooltipFunc(BattleHonorTooltip);
	GadgetListBoxReset( list );
	Int column = 0;
	Int row = 0;
	Int honors = stats.getHonors();

	UnicodeString uStr;
	GameWindow *streakWindow = TheWindowManager->winGetWindowFromId( NULL, NAMEKEY("SkirmishGameOptionsMenu.wnd:StaticTextStreakValue") );
	if (streakWindow)
	{
		uStr.format(u"%d", stats.getWinStreak());
		GadgetStaticTextSetText(streakWindow, uStr);
	}
	GameWindow *bestStreakWindow = TheWindowManager->winGetWindowFromId( NULL, NAMEKEY("SkirmishGameOptionsMenu.wnd:StaticTextBestStreakValue") );
	if (bestStreakWindow)
	{
		uStr.format(u"%d", stats.getBestWinStreak());
		GadgetStaticTextSetText(bestStreakWindow, uStr);
	}
	GameWindow *winsWindow = TheWindowManager->winGetWindowFromId( NULL, NAMEKEY("SkirmishGameOptionsMenu.wnd:StaticTextWinsValue") );
	if (winsWindow)
	{
		uStr.format(u"%d", stats.getWins());
		GadgetStaticTextSetText(winsWindow, uStr);
	}
	GameWindow *lossesWindow = TheWindowManager->winGetWindowFromId( NULL, NAMEKEY("SkirmishGameOptionsMenu.wnd:StaticTextLossesValue") );
	if (lossesWindow)
	{
		uStr.format(u"%d", stats.getLosses());
		GadgetStaticTextSetText(lossesWindow, uStr);
	}

	ResetBattleHonorInsertion();
	GadgetListBoxAddEntryImage(list, NULL, 0, 0, 10, 10, TRUE, GameMakeColor(255,255,255,255));
	
	// FIRST ROW OF HONORS
	row = 1; column = 0;

	// TEST FOR CHINA CAMPAIGN HONOR (GOLD, SILVER, BRONZE or GREYED OUT)
	if (stats.getCHINACampaignComplete(DIFFICULTY_HARD))
	{
		InsertBattleHonor(list, TheMappedImageCollection->findImageByName("ChinaCampaign_G"), TRUE,
			BATTLE_HONOR_CAMPAIGN_CHINA, row, column);
	} 
	else if (stats.getCHINACampaignComplete(DIFFICULTY_NORMAL))
	{
		InsertBattleHonor(list, TheMappedImageCollection->findImageByName("ChinaCampaign_S"), TRUE,
			BATTLE_HONOR_CAMPAIGN_CHINA, row, column);
	}
	else if (stats.getCHINACampaignComplete(DIFFICULTY_EASY))
	{
		InsertBattleHonor(list, TheMappedImageCollection->findImageByName("ChinaCampaign_B"), TRUE,
			BATTLE_HONOR_CAMPAIGN_CHINA, row, column);
	}
	else
	{
		InsertBattleHonor(list, TheMappedImageCollection->findImageByName("ChinaCampaign_B"), (honors & BATTLE_HONOR_CAMPAIGN_CHINA),
			BATTLE_HONOR_CAMPAIGN_CHINA, row, column);
	}

	// TEST FOR GLA CAMPAIGN HONOR (GOLD, SILVER, BRONZE or GREYED OUT)
	if (stats.getGLACampaignComplete(DIFFICULTY_HARD))
	{
		InsertBattleHonor(list, TheMappedImageCollection->findImageByName("GLACampaign_G"), TRUE,
			BATTLE_HONOR_CAMPAIGN_GLA, row, column);
	} 
	else if (stats.getGLACampaignComplete(DIFFICULTY_NORMAL))
	{
		InsertBattleHonor(list, TheMappedImageCollection->findImageByName("GLACampaign_S"), TRUE,
			BATTLE_HONOR_CAMPAIGN_GLA, row, column);
	}
	else if (stats.getGLACampaignComplete(DIFFICULTY_EASY))
	{
		InsertBattleHonor(list, TheMappedImageCollection->findImageByName("GLACampaign_B"), TRUE,
			BATTLE_HONOR_CAMPAIGN_GLA, row, column);
	}
	else
	{
		InsertBattleHonor(list, TheMappedImageCollection->findImageByName("GLACampaign_B"), (honors & BATTLE_HONOR_CAMPAIGN_GLA),
			BATTLE_HONOR_CAMPAIGN_GLA, row, column);
	}

	// TEST FOR GLA CAMPAIGN HONOR (GOLD, SILVER, BRONZE or GREYED OUT)
	if (stats.getUSACampaignComplete(DIFFICULTY_HARD))
	{
		InsertBattleHonor(list, TheMappedImageCollection->findImageByName("USACampaign_G"), TRUE,
			BATTLE_HONOR_CAMPAIGN_USA, row, column);
	} 
	else if (stats.getUSACampaignComplete(DIFFICULTY_NORMAL))
	{
		InsertBattleHonor(list, TheMappedImageCollection->findImageByName("USACampaign_S"), TRUE,
			BATTLE_HONOR_CAMPAIGN_USA, row, column);
	}
	else if (stats.getUSACampaignComplete(DIFFICULTY_EASY))
	{
		InsertBattleHonor(list, TheMappedImageCollection->findImageByName("USACampaign_B"), TRUE,
			BATTLE_HONOR_CAMPAIGN_USA, row, column);
	}
	else
	{
		InsertBattleHonor(list, TheMappedImageCollection->findImageByName("USACampaign_B"), (honors & BATTLE_HONOR_CAMPAIGN_USA),
			BATTLE_HONOR_CAMPAIGN_USA, row, column);
	}

	// TEST FOR GENERALS CHALLENGE HONOR (GOLD, SILVER, BRONZE or GREYED OUT)
	Bool completedOnHard		= FALSE;
	Bool completedOnNormal	= FALSE;
	Bool completedOnEasy		= FALSE;
	for (int i = 0; i < MAX_GLOBAL_GENERAL_TYPES; ++i)
	{
		if (stats.getChallengeCampaignComplete(i, DIFFICULTY_HARD))
			completedOnHard = TRUE;

		if (stats.getChallengeCampaignComplete(i, DIFFICULTY_NORMAL))
			completedOnNormal = TRUE;

		if (stats.getChallengeCampaignComplete(i, DIFFICULTY_EASY))
			completedOnEasy = TRUE;
	}

	if (completedOnHard)
	{
		InsertBattleHonor(list, TheMappedImageCollection->findImageByName("Challenge_Gold"), TRUE,
			BATTLE_HONOR_CHALLENGE_MODE, row, column);
	} 
	else if (completedOnNormal)
	{
		InsertBattleHonor(list, TheMappedImageCollection->findImageByName("Challenge_Silver"), TRUE,
			BATTLE_HONOR_CHALLENGE_MODE, row, column);
	}
	else if (completedOnEasy)
	{
		InsertBattleHonor(list, TheMappedImageCollection->findImageByName("Challenge_Bronz"), TRUE,
			BATTLE_HONOR_CHALLENGE_MODE, row, column);
	}
	else
	{
		InsertBattleHonor(list, TheMappedImageCollection->findImageByName("Challenge_Bronz"), FALSE,
			BATTLE_HONOR_CHALLENGE_MODE, row, column);
	}

	// TEST FOR AIR WING HONOR
	InsertBattleHonor(list, TheMappedImageCollection->findImageByName("HonorAirWing"), (honors & BATTLE_HONOR_AIR_WING),
		BATTLE_HONOR_AIR_WING, row, column);

	// TEST FOR BATTLE TANK HONOR
	InsertBattleHonor(list, TheMappedImageCollection->findImageByName("HonorBattleTank"), (honors & BATTLE_HONOR_BATTLE_TANK),
		BATTLE_HONOR_BATTLE_TANK, row, column);
	GadgetListBoxAddEntryImage(list, NULL, 2, 0, 10, 10, TRUE, GameMakeColor(255,255,255,255));

	// NEXT ROW OF HONORS
	row = 3; column = 0;

	// TEST FOR ENDURANCE HONOR (GOLD, SILVER, BRONZE or GREYED OUT)
	MapCache::const_iterator it;
	Bool missingEasy = FALSE;
	Bool missingMedium = FALSE;
	Bool missingBrutal = FALSE;
	for (it = TheMapCache->begin(); it != TheMapCache->end(); ++it)
	{
		if (!it->second.m_isOfficial || !it->second.m_isMultiplayer)
			continue;

		Bool easy = TRUE;
		Bool med = TRUE;
		Bool hard = TRUE;
		if (stats.getEnduranceMedal(it->first, SLOT_EASY_AI) == 0)
			easy = FALSE;
		if (stats.getEnduranceMedal(it->first, SLOT_MED_AI) == 0)
			med = FALSE;
		if (stats.getEnduranceMedal(it->first, SLOT_BRUTAL_AI) == 0)
			hard = FALSE;

		if (!easy && !med && !hard)
			missingEasy = TRUE;

		if (!med && !hard)
			missingMedium = TRUE;

		if (!hard)
			missingBrutal = TRUE;
	}
	if (!missingBrutal)
	{
		InsertBattleHonor(list, TheMappedImageCollection->findImageByName("Endurance_G"), TRUE,
			BATTLE_HONOR_ENDURANCE, row, column);
	}
	else if (!missingMedium)
	{
		InsertBattleHonor(list, TheMappedImageCollection->findImageByName("Endurance_S"), TRUE,
			BATTLE_HONOR_ENDURANCE, row, column);
	}
	else if (!missingEasy)
	{
		InsertBattleHonor(list, TheMappedImageCollection->findImageByName("Endurance_B"), TRUE,
			BATTLE_HONOR_ENDURANCE, row, column);
	}
	else
	{
		InsertBattleHonor(list, TheMappedImageCollection->findImageByName("Endurance_B"), FALSE,
			BATTLE_HONOR_ENDURANCE, row, column);
	}

	// TEST FOR APOCALYPSE HONOR
	InsertBattleHonor(list, TheMappedImageCollection->findImageByName("Apocalypse"), (honors & BATTLE_HONOR_APOCALYPSE),
		BATTLE_HONOR_APOCALYPSE, row, column);

	// TEST FOR BLITZ HONOR
	if (honors & BATTLE_HONOR_BLITZ5)
	{
		InsertBattleHonor(list, TheMappedImageCollection->findImageByName("HonorBlitz5"), TRUE,
			BATTLE_HONOR_BLITZ5, row, column);
	}
	else if (honors & BATTLE_HONOR_BLITZ10)
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
	Int streak = stats.getBestWinStreak();
	uStr.format(u"%10d", streak);
	if (streak >= 1000)
	{
		InsertBattleHonor(list, TheMappedImageCollection->findImageByName("HonorStreak_1000"), TRUE,
			BATTLE_HONOR_STREAK, row, column, uStr, streak);
	}
	else if (streak >= 500)
	{
		InsertBattleHonor(list, TheMappedImageCollection->findImageByName("HonorStreak_500"), TRUE,
			BATTLE_HONOR_STREAK, row, column, uStr, streak);
	}
	else if (streak >= 100)
	{
		InsertBattleHonor(list, TheMappedImageCollection->findImageByName("HonorStreak_100"), TRUE,
			BATTLE_HONOR_STREAK, row, column, uStr, streak);
	}
	else if (streak >= 25)
	{
		InsertBattleHonor(list, TheMappedImageCollection->findImageByName("HonorStreak_G"), TRUE,
			BATTLE_HONOR_STREAK, row, column, uStr, streak);
	}
	else if (streak >= 10)
	{
		InsertBattleHonor(list, TheMappedImageCollection->findImageByName("HonorStreak_S"), TRUE,
			BATTLE_HONOR_STREAK, row, column, uStr, streak);
	}
	else if (streak >= 3)
	{
		InsertBattleHonor(list, TheMappedImageCollection->findImageByName("HonorStreak_B"), TRUE,
			BATTLE_HONOR_STREAK, row, column, uStr, streak);
	}
	else
	{
		InsertBattleHonor(list, TheMappedImageCollection->findImageByName("HonorStreak_B"), FALSE,
			BATTLE_HONOR_STREAK, row, column, uStr, streak);
	}

	// TEST FOR DOMINATION HONOR
	Int totalWins = stats.getWins();
	uStr.format(u"%10d", totalWins);
	if (totalWins >= 10000)
	{
		InsertBattleHonor(list, TheMappedImageCollection->findImageByName("Domination_10000"), TRUE,
			BATTLE_HONOR_DOMINATION, row, column, uStr, totalWins);
	}
	else if (totalWins >= 1000)
	{
		InsertBattleHonor(list, TheMappedImageCollection->findImageByName("Domination_1000"), TRUE,
			BATTLE_HONOR_DOMINATION, row, column, uStr, totalWins);
	}
	else if (totalWins >= 500)
	{
		InsertBattleHonor(list, TheMappedImageCollection->findImageByName("Domination_500"), TRUE,
			BATTLE_HONOR_DOMINATION, row, column, uStr, totalWins);
	}
	else if (totalWins >= 100)
	{
		InsertBattleHonor(list, TheMappedImageCollection->findImageByName("Domination_100"), TRUE,
			BATTLE_HONOR_DOMINATION, row, column, uStr, totalWins);
	}
	else
	{
		InsertBattleHonor(list, TheMappedImageCollection->findImageByName("Domination_100"), FALSE,
			BATTLE_HONOR_DOMINATION, row, column, uStr, totalWins);
	}

	// TEST FOR ULTIMATE HONOR
	Bool perfect = TRUE;
	for (it = TheMapCache->begin(); it != TheMapCache->end(); ++it)
	{
		if (!it->second.m_isOfficial || !it->second.m_isMultiplayer)
			continue;
	
		Int totalOpponentSlots = it->second.m_numPlayers - 1;
		Int numBrutalOpponentsBeaten = stats.getEnduranceMedal(it->first, SLOT_BRUTAL_AI);
		if (numBrutalOpponentsBeaten < totalOpponentSlots)
		{
			perfect = FALSE;
			break;
		}
	}
	InsertBattleHonor(list, TheMappedImageCollection->findImageByName("Ultimate"), perfect,
		BATTLE_HONOR_ULTIMATE, row, column);

	// TEST FOR CHALLENGE HONOR

	/*
	InsertBattleHonor(list, TheMappedImageCollection->findImageByName("Loyalty_USA"), (honors & BATTLE_HONOR_LOYALTY_USA),
		BATTLE_HONOR_LOYALTY_USA, row, column);
	InsertBattleHonor(list, TheMappedImageCollection->findImageByName("Loyalty_China"), (honors & BATTLE_HONOR_LOYALTY_CHINA),
		BATTLE_HONOR_LOYALTY_CHINA, row, column);
	InsertBattleHonor(list, TheMappedImageCollection->findImageByName("Loyalty_GLA"), (honors & BATTLE_HONOR_LOYALTY_GLA),
		BATTLE_HONOR_LOYALTY_GLA, row, column);
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

	UnsignedInt isPreorder = 0;
	GetUnsignedIntFromRegistry("", "Preorder", isPreorder);
	if (isPreorder != 0)
	{
		InsertBattleHonor(list, TheMappedImageCollection->findImageByName("OfficersClub"), TRUE,
			BATTLE_HONOR_OFFICERSCLUB, row, column);
	}
}
