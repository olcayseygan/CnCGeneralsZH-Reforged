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

// FILE: SkirmishMapSelectMenu.cpp ////////////////////////////////////////////////////////////////////////
// Author: Chris Brue, August 2002
// Description: MapSelect menu window callbacks
///////////////////////////////////////////////////////////////////////////////////////////////////

// INCLUDES ///////////////////////////////////////////////////////////////////////////////////////
#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine
#include "Lib/Clock.h"

#include "Common/GameEngine.h"
#include "Common/MessageStream.h"
#include "Common/UserPreferences.h"
#include "GameClient/WindowLayout.h"
#include "GameClient/Gadget.h"
#include "GameClient/GameText.h"
#include "GameClient/Mouse.h"
#include "GameClient/Shell.h"
#include "GameClient/GameWindowManager.h"
#include "GameClient/GadgetListBox.h"
#include "GameClient/GadgetRadioButton.h"
#include "GameClient/GadgetStaticText.h"
#include "GameClient/GUICallbacks.h"
#include "GameNetwork/LANAPICallbacks.h"
#include "GameClient/MapUtil.h"
#include "Common/RandomMapGenerator.h"

// PRIVATE DATA ///////////////////////////////////////////////////////////////////////////////////
static NameKeyType buttonBack = NAMEKEY_INVALID;
static NameKeyType buttonOK = NAMEKEY_INVALID;
static NameKeyType listboxMap = NAMEKEY_INVALID;
static GameWindow *parent = NULL;
static GameWindow *mapList = NULL;

static NameKeyType radioButtonSystemMapsID = NAMEKEY_INVALID;
static NameKeyType radioButtonUserMapsID = NAMEKEY_INVALID;

/** The random map rows, one per size.  There is no window layout file in this fork to add a
	button or a dropdown to - every .wnd lives inside the shipped archives - so the size choice is
	three rows in the map list instead of a control beside it.  None of them is a map in the cache,
	so each carries its tag where every other row carries its file name, and choosing one generates
	a map of that size and picks it. */
static const char *theRandomMapRowTags[RANDOM_MAP_SIZE_COUNT] =
{
	"**random-small**",
	"**random-normal**",
	"**random-large**"
};

/// Which size a row asks for, or RANDOM_MAP_SIZE_COUNT for a row that is a real map.
static RandomMapSize randomMapSizeOfRow( const char *itemData )
{
	if( itemData != NULL )
	{
		for( Int size = 0; size < RANDOM_MAP_SIZE_COUNT; size++ )
		{
			if( strcmp( itemData, theRandomMapRowTags[size] ) == 0 )
				return (RandomMapSize)size;
		}
	}

	return RANDOM_MAP_SIZE_COUNT;
}

static Bool isRandomMapRow( const char *itemData )
{
	return randomMapSizeOfRow( itemData ) != RANDOM_MAP_SIZE_COUNT;
}

/** The three generated map rows, which are only offered when -randommaps asks for them.  The
	generator still hands out seeds a match should not be played on - a supply dock behind a cliff,
	a start with one way out - and the map list is the wrong place to meet one, so until it is
	finished the maps are reached from the command line, where -randommap names the seed being
	played and a bad one can simply be dropped. */
static void addRandomMapRows( GameWindow *listbox )
{
	if( listbox == NULL || TheGlobalData == NULL || !TheGlobalData->m_randomMapsInMenus )
		return;

	// Spelled out here rather than fetched: the string tables live in the shipped
	// archives, and a missing label would read "MISSING: GUI:RandomMap" on screen.
	static const WideChar *theRandomMapRowLabels[RANDOM_MAP_SIZE_COUNT] =
	{
		u"Random map, small - new seed every time (8)",
		u"Random map - new seed every time (8)",
		u"Random map, large - new seed every time (8)"
	};

	Color color = GameMakeColor( 255, 255, 255, 255 );
	Int numColumns = GadgetListBoxGetNumColumns( listbox );

	for( Int size = 0; size < RANDOM_MAP_SIZE_COUNT; size++ )
	{
		UnicodeString label( theRandomMapRowLabels[size] );
		Int index = GadgetListBoxAddEntryText( listbox, label, color, -1, numColumns - 1 );
		GadgetListBoxSetItemData( listbox, (void *)theRandomMapRowTags[size], index );
	}
}

/** Generate a map, keep it where the map cache will find it, and hand back its path.  The seed
	comes off the clock: this is the client picking a map, not the simulation, so it may be as
	random as the player expects a reroll to be. */
static Bool generateRandomMapForSkirmish( RandomMapSize size, AsciiString& mapPathOut )
{
	RandomMapSettings settings;
	settings.m_seed = (Int)Clock_Milliseconds_Coarse();
	settings.m_numPlayers = RandomMapGenerator::MAX_PLAYERS;
	settings.m_playableCells = RandomMapGenerator::cellsFor( size, settings.m_numPlayers );

	if( !stageRandomMap( settings, mapPathOut ) )
		return FALSE;

	mapPathOut.toLower();

	if( TheMapCache )
		TheMapCache->updateCache();

	return TRUE;
}

static GameWindow *buttonMapStartPosition[MAX_SLOTS] = {NULL,NULL,NULL,NULL,
																								NULL,NULL,NULL,NULL };
static NameKeyType buttonMapStartPositionID[MAX_SLOTS] = { NAMEKEY_INVALID,NAMEKEY_INVALID,
																									NAMEKEY_INVALID,NAMEKEY_INVALID,
																										NAMEKEY_INVALID,NAMEKEY_INVALID,
																										NAMEKEY_INVALID,NAMEKEY_INVALID };

static GameWindow *winMapPreview = NULL;
static NameKeyType winMapPreviewID = NAMEKEY_INVALID;

static void NullifyControls()
{
	mapList = NULL;
	winMapPreview = NULL;
	parent = NULL;
	for (Int i=0; i<MAX_SLOTS; ++i)
	{
		buttonMapStartPosition[i] = NULL;
	}
}

extern WindowLayout *skirmishMapSelectLayout;

// Tooltips -------------------------------------------------------------------------------

static void mapListTooltipFunc(GameWindow *window,
													WinInstanceData *instData,
													UnsignedInt mouse)
{
	Int x, y, row, col;
	x = LOLONGTOSHORT(mouse);
	y = HILONGTOSHORT(mouse);

	GadgetListBoxGetEntryBasedOnXY(window, x, y, row, col);

	if (row == -1 || col == -1)
	{
		TheMouse->setCursorTooltip( UnicodeString::TheEmptyString);
		return;
	}

	Int imageItemData = (Int)(intptr_t)GadgetListBoxGetItemData(window, row, 1);
	UnicodeString tooltip;
	switch (imageItemData)
	{
		case 0:
			tooltip = TheGameText->fetch("TOOLTIP:MapNoSuccess");
			break;
		case 1:
			tooltip = TheGameText->fetch("TOOLTIP:MapEasySuccess");
			break;
		case 2:
			tooltip = TheGameText->fetch("TOOLTIP:MapMediumSuccess");
			break;
		case 3:
			tooltip = TheGameText->fetch("TOOLTIP:MapHardSuccess");
			break;
		case 4:
			tooltip = TheGameText->fetch("TOOLTIP:MapMaxBrutalSuccess");
			break;
	}

	TheMouse->setCursorTooltip( tooltip );
}

// PUBLIC FUNCTIONS ///////////////////////////////////////////////////////////////////////////////
void positionStartSpots( AsciiString mapName, GameWindow *buttonMapStartPositions[], GameWindow *mapWindow);
void skirmishPositionStartSpots( void );
void skirmishUpdateSlotList( void );
void showSkirmishGameOptionsUnderlyingGUIElements( Bool show )
{                          
	AsciiString parentName( "SkirmishGameOptionsMenu.wnd:SkirmishGameOptionsMenuParent" );
	NameKeyType parentID = TheNameKeyGenerator->nameToKey( parentName );
	GameWindow *parent = TheWindowManager->winGetWindowFromId( NULL, parentID );
	if (!parent)
		return;

	// hide some GUI elements of the screen underneath
	GameWindow *win;

	win	= TheWindowManager->winGetWindowFromId( parent, TheNameKeyGenerator->nameToKey("SkirmishGameOptionsMenu.wnd:MapWindow") );
	win->winHide( !show );

	//win	= TheWindowManager->winGetWindowFromId( parent, TheNameKeyGenerator->nameToKey("SkirmishGameOptionsMenu.wnd:StaticTextTitle") );
	//win->winHide( !show );

	win	= TheWindowManager->winGetWindowFromId( parent, TheNameKeyGenerator->nameToKey("SkirmishGameOptionsMenu.wnd:StaticTextTeam") );
	win->winHide( !show );

	win	= TheWindowManager->winGetWindowFromId( parent, TheNameKeyGenerator->nameToKey("SkirmishGameOptionsMenu.wnd:StaticTextFaction") );
	win->winHide( !show );

	win	= TheWindowManager->winGetWindowFromId( parent, TheNameKeyGenerator->nameToKey("SkirmishGameOptionsMenu.wnd:StaticTextColor") );
	win->winHide( !show );

	win	= TheWindowManager->winGetWindowFromId( parent, TheNameKeyGenerator->nameToKey("SkirmishGameOptionsMenu.wnd:ComboBoxTeam0") );
	win->winHide( !show );

	win	= TheWindowManager->winGetWindowFromId( parent, TheNameKeyGenerator->nameToKey("SkirmishGameOptionsMenu.wnd:ComboBoxTeam1") );
	win->winHide( !show );

	win	= TheWindowManager->winGetWindowFromId( parent, TheNameKeyGenerator->nameToKey("SkirmishGameOptionsMenu.wnd:ComboBoxTeam2") );
	win->winHide( !show );

	win	= TheWindowManager->winGetWindowFromId( parent, TheNameKeyGenerator->nameToKey("SkirmishGameOptionsMenu.wnd:ComboBoxTeam3") );
	win->winHide( !show );

	win	= TheWindowManager->winGetWindowFromId( parent, TheNameKeyGenerator->nameToKey("SkirmishGameOptionsMenu.wnd:ComboBoxTeam4") );
	win->winHide( !show );

	win	= TheWindowManager->winGetWindowFromId( parent, TheNameKeyGenerator->nameToKey("SkirmishGameOptionsMenu.wnd:ComboBoxTeam5") );
	win->winHide( !show );

	win	= TheWindowManager->winGetWindowFromId( parent, TheNameKeyGenerator->nameToKey("SkirmishGameOptionsMenu.wnd:ComboBoxTeam6") );
	win->winHide( !show );

	win	= TheWindowManager->winGetWindowFromId( parent, TheNameKeyGenerator->nameToKey("SkirmishGameOptionsMenu.wnd:ComboBoxTeam7") );
	win->winHide( !show );

	win	= TheWindowManager->winGetWindowFromId( parent, TheNameKeyGenerator->nameToKey("SkirmishGameOptionsMenu.wnd:ComboBoxColor0") );
	win->winHide( !show );

	win	= TheWindowManager->winGetWindowFromId( parent, TheNameKeyGenerator->nameToKey("SkirmishGameOptionsMenu.wnd:ComboBoxColor1") );
	win->winHide( !show );

	win	= TheWindowManager->winGetWindowFromId( parent, TheNameKeyGenerator->nameToKey("SkirmishGameOptionsMenu.wnd:ComboBoxColor2") );
	win->winHide( !show );

	win	= TheWindowManager->winGetWindowFromId( parent, TheNameKeyGenerator->nameToKey("SkirmishGameOptionsMenu.wnd:ComboBoxColor3") );
	win->winHide( !show );

	win	= TheWindowManager->winGetWindowFromId( parent, TheNameKeyGenerator->nameToKey("SkirmishGameOptionsMenu.wnd:ComboBoxColor4") );
	win->winHide( !show );

	win	= TheWindowManager->winGetWindowFromId( parent, TheNameKeyGenerator->nameToKey("SkirmishGameOptionsMenu.wnd:ComboBoxColor5") );
	win->winHide( !show );

	win	= TheWindowManager->winGetWindowFromId( parent, TheNameKeyGenerator->nameToKey("SkirmishGameOptionsMenu.wnd:ComboBoxColor6") );
	win->winHide( !show );

	win	= TheWindowManager->winGetWindowFromId( parent, TheNameKeyGenerator->nameToKey("SkirmishGameOptionsMenu.wnd:ComboBoxColor7") );
	win->winHide( !show );

	win	= TheWindowManager->winGetWindowFromId( parent, TheNameKeyGenerator->nameToKey("SkirmishGameOptionsMenu.wnd:ComboBoxPlayerTemplate0") );
	win->winHide( !show );

	win	= TheWindowManager->winGetWindowFromId( parent, TheNameKeyGenerator->nameToKey("SkirmishGameOptionsMenu.wnd:ComboBoxPlayerTemplate1") );
	win->winHide( !show );

	win	= TheWindowManager->winGetWindowFromId( parent, TheNameKeyGenerator->nameToKey("SkirmishGameOptionsMenu.wnd:ComboBoxPlayerTemplate2") );
	win->winHide( !show );

	win	= TheWindowManager->winGetWindowFromId( parent, TheNameKeyGenerator->nameToKey("SkirmishGameOptionsMenu.wnd:ComboBoxPlayerTemplate3") );
	win->winHide( !show );

	win	= TheWindowManager->winGetWindowFromId( parent, TheNameKeyGenerator->nameToKey("SkirmishGameOptionsMenu.wnd:ComboBoxPlayerTemplate4") );
	win->winHide( !show );

	win	= TheWindowManager->winGetWindowFromId( parent, TheNameKeyGenerator->nameToKey("SkirmishGameOptionsMenu.wnd:ComboBoxPlayerTemplate5") );
	win->winHide( !show );

	win	= TheWindowManager->winGetWindowFromId( parent, TheNameKeyGenerator->nameToKey("SkirmishGameOptionsMenu.wnd:ComboBoxPlayerTemplate6") );
	win->winHide( !show );

	win	= TheWindowManager->winGetWindowFromId( parent, TheNameKeyGenerator->nameToKey("SkirmishGameOptionsMenu.wnd:ComboBoxPlayerTemplate7") );
	win->winHide( !show );

	win	= TheWindowManager->winGetWindowFromId( parent, TheNameKeyGenerator->nameToKey("SkirmishGameOptionsMenu.wnd:TextEntryMapDisplay") );
	win->winHide( !show );

	win	= TheWindowManager->winGetWindowFromId( parent, TheNameKeyGenerator->nameToKey("SkirmishGameOptionsMenu.wnd:ButtonSelectMap") );
	win->winHide( !show );

	win	= TheWindowManager->winGetWindowFromId( parent, TheNameKeyGenerator->nameToKey("SkirmishGameOptionsMenu.wnd:ButtonStart") );
	win->winHide( !show );

	win	= TheWindowManager->winGetWindowFromId( parent, TheNameKeyGenerator->nameToKey("SkirmishGameOptionsMenu.wnd:StaticTextMapPreview") );
	win->winHide( !show );

	win	= TheWindowManager->winGetWindowFromId( parent, TheNameKeyGenerator->nameToKey("SkirmishGameOptionsMenu.wnd:ButtonReset") );
	win->winEnable( show );

	win	= TheWindowManager->winGetWindowFromId( parent, TheNameKeyGenerator->nameToKey("SkirmishGameOptionsMenu.wnd:ButtonBack") );
	win->winEnable( show );

	// Apocalypse's room is a form on its settings page, half of it under the map list
	if( SkirmishApocalypse )
	{
		win	= TheWindowManager->winGetWindowFromId( parent, TheNameKeyGenerator->nameToKey("SkirmishGameOptionsMenu.wnd:PageLobbySettings") );
		win->winHide( !show );
	}
}

//-------------------------------------------------------------------------------------------------
/** Initialize the MapSelect menu */
//-------------------------------------------------------------------------------------------------
void SkirmishMapSelectMenuInit( WindowLayout *layout, void *userData )
{

	// set keyboard focus to main parent
	AsciiString parentName( "SkirmishMapSelectMenu.wnd:SkrimishMapSelectMenuParent" );
	NameKeyType parentID = TheNameKeyGenerator->nameToKey( parentName );
	parent = TheWindowManager->winGetWindowFromId( NULL, parentID );

	TheWindowManager->winSetFocus( parent );

	LANPreferences pref;
	Bool usesSystemMapDir = pref.usesSystemMapDir();

	const MapMetaData *mmd = TheMapCache->findMap(TheSkirmishGameInfo->getMap());
	if (mmd)
	{
		usesSystemMapDir = mmd->m_isOfficial;
	}

	winMapPreviewID = TheNameKeyGenerator->nameToKey( AsciiString("SkirmishMapSelectMenu.wnd:WinMapPreview") );
	winMapPreview = TheWindowManager->winGetWindowFromId(parent, winMapPreviewID);


	buttonBack = TheNameKeyGenerator->nameToKey( AsciiString("SkirmishMapSelectMenu.wnd:ButtonBack") );
	buttonOK = TheNameKeyGenerator->nameToKey( AsciiString("SkirmishMapSelectMenu.wnd:ButtonOK") );
	listboxMap = TheNameKeyGenerator->nameToKey( AsciiString("SkirmishMapSelectMenu.wnd:ListboxMap") );

	radioButtonSystemMapsID = TheNameKeyGenerator->nameToKey( "SkirmishMapSelectMenu.wnd:RadioButtonSystemMaps" );
	radioButtonUserMapsID = TheNameKeyGenerator->nameToKey( "SkirmishMapSelectMenu.wnd:RadioButtonUserMaps" );
	GameWindow *radioButtonSystemMaps = TheWindowManager->winGetWindowFromId( parent, radioButtonSystemMapsID );
	GameWindow *radioButtonUserMaps = TheWindowManager->winGetWindowFromId( parent, radioButtonUserMapsID );
	if (usesSystemMapDir)
		GadgetRadioSetSelection( radioButtonSystemMaps, FALSE );
	else
		GadgetRadioSetSelection( radioButtonUserMaps, FALSE );

	AsciiString tmpString;
	for (Int i = 0; i < MAX_SLOTS; i++)
	{
		tmpString.format("SkirmishMapSelectMenu.wnd:ButtonMapStartPosition%d", i);
		buttonMapStartPositionID[i] = TheNameKeyGenerator->nameToKey( tmpString );
		buttonMapStartPosition[i] = TheWindowManager->winGetWindowFromId( winMapPreview, buttonMapStartPositionID[i] );
		DEBUG_ASSERTCRASH(buttonMapStartPosition[i], ("Could not find the ButtonMapStartPosition[%d]",i ));
		buttonMapStartPosition[i]->winHide(TRUE);
		buttonMapStartPosition[i]->winEnable(FALSE);
	}

	showSkirmishGameOptionsUnderlyingGUIElements(FALSE);

	// get the listbox window
	AsciiString listString( "SkirmishMapSelectMenu.wnd:ListboxMap" );
	NameKeyType mapListID = TheNameKeyGenerator->nameToKey( listString );
	mapList = TheWindowManager->winGetWindowFromId( parent, mapListID );
	if( mapList )
	{
		if (TheMapCache)
			TheMapCache->updateCache();
		if (usesSystemMapDir)
		{
			populateMapListbox( mapList, TRUE, TRUE, TheSkirmishGameInfo->getMap() );
		}
		else
		{
			populateMapListbox( mapList, FALSE, FALSE, TheSkirmishGameInfo->getMap() );
			populateMapListboxNoReset( mapList, FALSE, TRUE, TheSkirmishGameInfo->getMap() );
		}
		addRandomMapRows( mapList );
		mapList->winSetTooltipFunc(mapListTooltipFunc);
	}

}  // end SkirmishMapSelectMenuInit

//-------------------------------------------------------------------------------------------------
/** MapSelect menu shutdown method */
//-------------------------------------------------------------------------------------------------
void SkirmishMapSelectMenuShutdown( WindowLayout *layout, void *userData )
{

	// hide menu
	layout->hide( TRUE );
	
	NullifyControls();
	
	// our shutdown is complete
	TheShell->shutdownComplete( layout );

}  // end LanMapSelectMenuShutdown

//-------------------------------------------------------------------------------------------------
/** MapSelect menu update method */
//-------------------------------------------------------------------------------------------------
void SkirmishMapSelectMenuUpdate( WindowLayout *layout, void *userData )
{

}  // end SkirmishMapSelectMenuUpdate

//-------------------------------------------------------------------------------------------------
/** Map select menu input callback */
//-------------------------------------------------------------------------------------------------
WindowMsgHandledType SkirmishMapSelectMenuInput( GameWindow *window, UnsignedInt msg,
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
						AsciiString buttonName( "SkirmishMapSelectMenu.wnd:ButtonBack" );
						NameKeyType buttonID = TheNameKeyGenerator->nameToKey( buttonName );
						GameWindow *button = TheWindowManager->winGetWindowFromId( window, buttonID );

						TheWindowManager->winSendSystemMsg( window, GBM_SELECTED, 
																								(WindowMsgData)button, buttonID );

					}  // end if

					// don't let key fall through anywhere else
					return MSG_HANDLED;

				}  // end escape

			}  // end switch( key )

		}  // end char

	}  // end switch( msg )

	return MSG_IGNORED;

}  // end SkirmishMapSelectMenuInput

//-------------------------------------------------------------------------------------------------
/** MapSelect menu window system callback */
//-------------------------------------------------------------------------------------------------
WindowMsgHandledType SkirmishMapSelectMenuSystem( GameWindow *window, UnsignedInt msg, 
																				  WindowMsgData mData1, WindowMsgData mData2 )
{
	
	switch( msg ) 
	{

		// --------------------------------------------------------------------------------------------
		case GWM_CREATE:
		{
			break;

		}  // end create

		//---------------------------------------------------------------------------------------------
		case GWM_DESTROY:
		{
			NullifyControls();
			break;

		}  // end case

		// --------------------------------------------------------------------------------------------
		case GWM_INPUT_FOCUS:
		{

			// if we're givin the opportunity to take the keyboard focus we must say we want it
			if( mData1 == TRUE )
				*(Bool *)mData2 = TRUE;

			return MSG_HANDLED;

		}  // end input

		//---------------------------------------------------------------------------------------------
		case GLM_DOUBLE_CLICKED:
			{
				GameWindow *control = (GameWindow *)mData1;
				Int controlID = control->winGetWindowId();
				if( controlID == listboxMap ) 
				{
					int rowSelected = mData2;
				
					if (rowSelected >= 0)
					{
						GadgetListBoxSetSelected( control, rowSelected );
						GameWindow *button = TheWindowManager->winGetWindowFromId( window, buttonOK );

						TheWindowManager->winSendSystemMsg( window, GBM_SELECTED, 
																								(WindowMsgData)button, buttonOK );
					}
				}
				break;
			}
		//---------------------------------------------------------------------------------------------
			case GLM_SELECTED:
			{
				GameWindow *mapWindow = TheWindowManager->winGetWindowFromId( parent, listboxMap );

				GameWindow *control = (GameWindow *)mData1;
				Int controlID = control->winGetWindowId();
				if( controlID == listboxMap ) 
				{
					int rowSelected = mData2;
					if( rowSelected < 0 )
					{
						positionStartSpots( AsciiString::TheEmptyString, buttonMapStartPosition, winMapPreview);
						//winMapPreview->winClearStatus(WIN_STATUS_IMAGE);
						break;
					}
					winMapPreview->winSetStatus(WIN_STATUS_IMAGE);
					UnicodeString map;
					// get text of the map to load
					map = GadgetListBoxGetText( mapWindow, rowSelected, 0 );

					// set the map name in the global data map name
					AsciiString asciiMap;
					const char *mapFname = (const char *)GadgetListBoxGetItemData( mapWindow, rowSelected );

					// The random map row has no map behind it until it is chosen, so there is
					// nothing to preview and no start positions to lay out yet.
					if( isRandomMapRow( mapFname ) )
					{
						winMapPreview->winClearStatus(WIN_STATUS_IMAGE);
						positionStartSpots( AsciiString::TheEmptyString, buttonMapStartPosition, winMapPreview);
						break;
					}

					DEBUG_ASSERTCRASH(mapFname, ("No map item data"));
					if (mapFname)
						asciiMap = mapFname;
					else
						asciiMap.translate( map );
					asciiMap.toLower();
					Image *image = getMapPreviewImage(asciiMap);
					winMapPreview->winSetUserData((void *)TheMapCache->findMap(asciiMap));
					if(image)
					{
						winMapPreview->winSetEnabledImage(0, image);
					}
					else
					{
						winMapPreview->winClearStatus(WIN_STATUS_IMAGE);
					}
					positionStartSpots( asciiMap, buttonMapStartPosition, winMapPreview);
				}
				break;
			}

		//---------------------------------------------------------------------------------------------
		case GBM_SELECTED:
		{
			// this isn't fixed yet
			GameWindow *control = (GameWindow *)mData1;
			Int controlID = control->winGetWindowId();

			if ( controlID == radioButtonSystemMapsID )
			{
				if (TheMapCache)
					TheMapCache->updateCache();
				populateMapListbox( mapList, TRUE, TRUE, TheSkirmishGameInfo->getMap() );
				addRandomMapRows( mapList );
				//LANPreferences pref;
				//pref["UseSystemMapDir"] = "yes";
				//pref.write();
			}
			else if ( controlID == radioButtonUserMapsID )
			{
				if (TheMapCache)
					TheMapCache->updateCache();
				populateMapListbox( mapList, FALSE, FALSE, TheSkirmishGameInfo->getMap() );
				populateMapListboxNoReset( mapList, FALSE, TRUE, TheSkirmishGameInfo->getMap() );
				addRandomMapRows( mapList );
				//LANPreferences pref;
				//pref["UseSystemMapDir"] = "no";
				//pref.write();
			}
			else if ( controlID == buttonBack )
			{
				showSkirmishGameOptionsUnderlyingGUIElements(TRUE);

				skirmishMapSelectLayout->destroyWindows();
				skirmishMapSelectLayout->deleteInstance();
				skirmishMapSelectLayout = NULL;
				skirmishPositionStartSpots();
				//TheShell->pop();
				//do you need this ??
				//PostToLanGameOptions( MAP_BACK );
			}  // end if
			else if ( controlID == buttonOK )
			{

				Int selected;
				UnicodeString map;
				GameWindow *mapWindow = TheWindowManager->winGetWindowFromId( parent, listboxMap );

				// get the selected index
				GadgetListBoxGetSelected( mapWindow, &selected );

				if( selected != -1 )
				{
					//buttonPushed = true;
					// set the map name in the global data map name
					AsciiString asciiMap;
					const char *mapFname = (const char *)GadgetListBoxGetItemData( mapWindow, selected );
					DEBUG_ASSERTCRASH(mapFname, ("No map item data"));

					// Choosing a random map row is the reroll: a fresh seed, a fresh map built in
					// memory at the size the row asks for, and the map cache reloaded so the rest
					// of this runs unchanged.
					RandomMapSize randomSize = randomMapSizeOfRow( mapFname );
					if( randomSize != RANDOM_MAP_SIZE_COUNT )
					{
						AsciiString generatedMap;
						if( !generateRandomMapForSkirmish( randomSize, generatedMap ) )
							break;

						asciiMap = generatedMap;
					}
					else if (mapFname)
						asciiMap = mapFname;
					else
						asciiMap.translate( map );
					TheSkirmishGameInfo->setMap( asciiMap );

					const MapMetaData *md = TheMapCache->findMap(asciiMap);
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

					// reset the start positions
					for(Int i = 0; i < MAX_SLOTS; ++i)
						TheSkirmishGameInfo->getSlot(i)->setStartPos(-1);
					GameWindow *win;
					win	= TheWindowManager->winGetWindowFromId( parent, TheNameKeyGenerator->nameToKey("SkirmishGameOptionsMenu.wnd:TextEntryMapDisplay") );
					if(win)
					{
				    if (md)
				    {
  						GadgetStaticTextSetText(win, md->m_displayName);
                    }
		         }
					//if (mapFname)
						//setupGameStart(mapFname);

				showSkirmishGameOptionsUnderlyingGUIElements(TRUE);
				skirmishPositionStartSpots();
				skirmishUpdateSlotList();

				skirmishMapSelectLayout->destroyWindows();
				skirmishMapSelectLayout->deleteInstance();
				skirmishMapSelectLayout = NULL;
					//TheShell->pop();

				}  // end if
			}  // end else if

			break;

		}  // end selected

		default:
			return MSG_IGNORED;

	}  // end switch

	return MSG_HANDLED;

}  // end SkirmishMapSelectMenuSystem*/
