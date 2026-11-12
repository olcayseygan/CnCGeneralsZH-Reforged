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

// FILE: QuitMenu.cpp /////////////////////////////////////////////////////////////////////////////
// Author: Colin Day, October 2001
// Description: Quit menu window callbacks
///////////////////////////////////////////////////////////////////////////////////////////////////

// INCLUDES ///////////////////////////////////////////////////////////////////////////////////////
#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "Common/GameEngine.h"
#include "Common/GameState.h"
#include "Common/MessageStream.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/RandomValue.h"
#include "Common/Recorder.h"
#include "GameClient/GUICallbacks.h"
#include "GameClient/WindowLayout.h"
#include "GameClient/Display.h"
#include "GameClient/GameWindowManager.h"
#include "GameClient/Gadget.h"
#include "GameClient/GadgetPushButton.h"
#include "GameClient/GameText.h"
#include "GameClient/MessageBox.h"
#include "GameClient/Shell.h"
#include "GameClient/InGameUI.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/VictoryConditions.h"
#include "GameClient/ControlBar.h"
#include "GameClient/GameWindowTransitions.h"
#include "GameClient/DisconnectMenu.h"
#include "GameLogic/ScriptEngine.h"
#include "GameNetwork/GameInfo.h"

#ifdef _INTERNAL
// for occasional debugging...
//#pragma optimize("", off)
//#pragma MESSAGE("************************************** WARNING, optimization disabled for debugging purposes")
#endif


// PRIVATE DATA ///////////////////////////////////////////////////////////////////////////////////
static WindowLayout *quitMenuLayout = NULL;
static WindowLayout *fullQuitMenuLayout = NULL;
static WindowLayout *noSaveLoadQuitMenuLayout = NULL;

static Bool isVisible = FALSE;

static GameWindow *quitConfirmationWindow = NULL;

//external declarations of the Gadgets the callbacks can use
static WindowLayout *saveLoadMenuLayout = NULL;

static GameWindow *buttonRestartWin	= NULL;
static GameWindow *buttonSaveLoadWin = NULL;
static GameWindow *buttonOptionsWin = NULL;
static GameWindow *buttonExitWin = NULL;

static NameKeyType buttonExit = NAMEKEY_INVALID;
static NameKeyType buttonRestart = NAMEKEY_INVALID;
static NameKeyType buttonReturn = NAMEKEY_INVALID;
static NameKeyType buttonOptions = NAMEKEY_INVALID;
static NameKeyType buttonSaveLoad = NAMEKEY_INVALID;

static void initGadgetsFullQuit( void )
{
	buttonExit = TheNameKeyGenerator->nameToKey( AsciiString( "QuitMenu.wnd:ButtonExit" ) );
	buttonRestart = TheNameKeyGenerator->nameToKey( AsciiString( "QuitMenu.wnd:ButtonRestart" ) );
	buttonReturn = TheNameKeyGenerator->nameToKey( AsciiString( "QuitMenu.wnd:ButtonReturn" ) );
	buttonOptions = TheNameKeyGenerator->nameToKey( AsciiString( "QuitMenu.wnd:ButtonOptions" ) );
	buttonSaveLoad = TheNameKeyGenerator->nameToKey( AsciiString( "QuitMenu.wnd:ButtonSaveLoad" ) );
	
	buttonRestartWin	= TheWindowManager->winGetWindowFromId( NULL, buttonRestart );
	buttonSaveLoadWin = TheWindowManager->winGetWindowFromId( NULL, buttonSaveLoad );
	buttonOptionsWin = TheWindowManager->winGetWindowFromId( NULL, buttonOptions );
	buttonExitWin = TheWindowManager->winGetWindowFromId( NULL, buttonExit );
}

static void initGadgetsNoSaveQuit( void )
{
	buttonExit = TheNameKeyGenerator->nameToKey( AsciiString( "QuitNoSave.wnd:ButtonExit" ) );
	buttonRestart = TheNameKeyGenerator->nameToKey( AsciiString( "QuitNoSave.wnd:ButtonRestart" ) );
	buttonReturn = TheNameKeyGenerator->nameToKey( AsciiString( "QuitNoSave.wnd:ButtonReturn" ) );
	buttonOptions = TheNameKeyGenerator->nameToKey( AsciiString( "QuitNoSave.wnd:ButtonOptions" ) );
	buttonSaveLoad = NAMEKEY_INVALID;

	buttonRestartWin	= TheWindowManager->winGetWindowFromId( NULL, buttonRestart );
	buttonOptionsWin = TheWindowManager->winGetWindowFromId( NULL, buttonOptions );
	buttonSaveLoadWin = NULL;
	buttonExitWin = TheWindowManager->winGetWindowFromId( NULL, buttonExit );

}

// PUBLIC FUNCTIONS ///////////////////////////////////////////////////////////////////////////////

void destroyQuitMenu()
{
  // destroy the quit menu
	quitConfirmationWindow = NULL;
	if(fullQuitMenuLayout)
	{
		fullQuitMenuLayout->destroyWindows();
		fullQuitMenuLayout->deleteInstance();
	}
	fullQuitMenuLayout = NULL;
	if(noSaveLoadQuitMenuLayout)
	{
		noSaveLoadQuitMenuLayout->destroyWindows();
		noSaveLoadQuitMenuLayout->deleteInstance();
	}
	noSaveLoadQuitMenuLayout = NULL;
	quitMenuLayout = NULL;

	//
	// The save/load layout is the shell's, not this file's - it is handed over by
	// getSaveLoadMenuLayout and destroyed with the shell.  Holding the pointer past that is holding
	// a freed layout, and ToggleQuitMenu reads isHidden() off it before it does anything else: a
	// read that says "showing" sends a Back press into freed memory and returns without opening the
	// menu, leaving a paused game with nothing on screen to unpause it.
	//
	saveLoadMenuLayout = NULL;

	isVisible = FALSE;
}

//
// A .wnd is stretched to the screen it is built on, once, when it is built.  The quit menu on
// screen when the resolution changes is therefore laid out for the screen that has just gone: on a
// smaller one its buttons sit past the edge, and Return is the only thing that takes the game back
// out of the pause the menu put it into, so a player who goes down a resolution from inside a match
// is left looking at a paused game he cannot click his way out of.  Building it again costs a
// dropped animation and nothing else.
//
void RecreateQuitMenu( void )
{
	const Bool wasVisible = isVisible;

	destroyQuitMenu();

	if( !wasVisible )
		return;

	// destroyQuitMenu clears isVisible, so this takes ToggleQuitMenu's open branch.  The game is
	// already paused and setGamePaused ignores a pause it is already holding.
	ToggleQuitMenu();

	//
	// ToggleQuitMenu has half a dozen ways to decline - a cinematic playing, the disconnect screen
	// up, an options or save/load layout it thinks is still on screen - and every one of them
	// returns without opening anything.  The menu is what holds the pause, so a menu that does not
	// come back has to hand the pause back instead; the alternative is a match that runs no logic
	// with nothing on screen to press.  Unpausing a game that is not paused is a no-op.
	//
	if( !isVisible )
	{
		DEBUG_LOG(("QUITMENU: it would not reopen after the resolution change, releasing the pause\n"));
		TheGameLogic->setGamePaused( FALSE );
		TheInGameUI->setQuitMenuVisible( FALSE );
	}
}

//
// Where Return actually is, for -resdrill.  A drill closes the menu by calling ToggleQuitMenu and so
// can never be told that the button is out of reach; the rectangle against the screen size is what
// says it, and a button whose right edge is past the screen width is a paused game with no way out.
//
void QuitMenuLogPlacement( const char *tag )
{
	GameWindow *returnButton = ( quitMenuLayout == NULL )
														 ? NULL
														 : TheWindowManager->winGetWindowFromId( NULL, buttonReturn );

	if( returnButton == NULL )
	{
		DEBUG_LOG(("QUITDRILL: %s, no quit menu on screen\n", tag));
		return;
	}

	ICoord2D position, size;
	returnButton->winGetScreenPosition( &position.x, &position.y );
	returnButton->winGetSize( &size.x, &size.y );

	DEBUG_LOG(("QUITDRILL: %s, return button (%d,%d %dx%d) screen %dx%d\n",
		tag, position.x, position.y, size.x, size.y,
		TheDisplay->getWidth(), TheDisplay->getHeight()));
}

/**
 *  quits the program
 */
static void exitQuitMenu()
{
  // destroy the quit menu
	destroyQuitMenu();
	
	// clear out all the game data
	if ( TheGameLogic->isInMultiplayerGame() && !TheGameLogic->isInSkirmishGame() && !TheGameInfo->isSandbox() )
	{
		GameMessage *msg = TheMessageStream->appendMessage(GameMessage::MSG_SELF_DESTRUCT);
		msg->appendBooleanArgument(TRUE);
	}
	/*GameMessage *msg =*/ TheMessageStream->appendMessage( GameMessage::MSG_CLEAR_GAME_DATA );
	if ( !TheGameLogic->isInMultiplayerGame() )
		TheGameLogic->setGamePaused(FALSE);
	// TheGameLogic->clearGameData();
	// display the menu on top of the shell stack
  // TheShell->showShell();

	// this will trigger an exit
  // TheGameEngine->setQuitting( TRUE );
	TheInGameUI->setClientQuiet( TRUE );
}
static void noExitQuitMenu()
{
	quitConfirmationWindow = NULL;
}

static void quitToDesktopQuitMenu()
{
  // destroy the quit menu
	destroyQuitMenu();

	if (TheGameLogic->isInGame())
	{
		if (TheRecorder->getMode() == RECORDERMODETYPE_RECORD)
		{
			TheRecorder->stopRecording();
		}
		TheGameLogic->clearGameData();
	}
	TheGameEngine->setQuitting(TRUE);
	TheInGameUI->setClientQuiet( TRUE );

}

static void surrenderQuitMenu()
{
  // destroy the quit menu
	destroyQuitMenu();

	if (TheVictoryConditions->isLocalAlliedVictory())
		return;

	GameMessage *msg = TheMessageStream->appendMessage(GameMessage::MSG_SELF_DESTRUCT);
	msg->appendBooleanArgument(TRUE);

	TheInGameUI->setClientQuiet( TRUE );
}

static void restartMissionMenu()
{
	// destroy the quit menu
	destroyQuitMenu();

	Int gameMode = TheGameLogic->getGameMode();
	AsciiString mapName = TheGlobalData->m_mapName;

	/* Read the seed before clearGameData() takes the game apart.  A skirmish restart has to be the
		 same match again - EA reseeded with a flat 0 whatever mode was running, so the replay of a
		 restarted skirmish was played back against a different random stream than it was recorded
		 with and diverged immediately.  Campaign, challenge and a solo map launched from the skirmish
		 menu all run as GAME_SINGLE_PLAYER and are seeded with 0 everywhere else, so that stays. */
	const Int seed = (gameMode == GAME_SKIRMISH && TheSkirmishGameInfo) ? TheSkirmishGameInfo->getSeed() : 0;

	//
	// if the map name was from a save game it will have "Save/" at the front of it,
	// we want to go back to the original pristine map string for the map name when restarting
	//
	if (TheGameState->isInSaveDirectory(mapName))
		mapName = TheGameState->getPristineMapName();

	// End the current game
	AsciiString replayFile = TheRecorder->getCurrentReplayFilename();
	if (TheRecorder->getMode() == RECORDERMODETYPE_RECORD)
	{
		TheRecorder->stopRecording();
	}
	
	Int rankPointsStartedWith = TheGameLogic->getRankPointsToAddAtGameStart();// must write down before reset
	GameDifficulty diff = TheScriptEngine->getGlobalDifficulty();
	Int fps = TheGameEngine->getFramesPerSecondLimit();
	const Int gameSpeedPercent = GAME_SPEED_PERCENT;	// the restart plays the speed this match had

	TheGameLogic->clearGameData(FALSE);
	TheGameEngine->setQuitting(FALSE);

	if (replayFile.isNotEmpty())
	{
		TheRecorder->playbackFile(replayFile);
	}
	else
	{
		// send a message to the logic for a new game
		TheWritableGlobalData->m_pendingFile = mapName;
		GameMessage *msg = TheMessageStream->appendMessage( GameMessage::MSG_NEW_GAME );
		msg->appendIntegerArgument(gameMode);
		msg->appendIntegerArgument(diff);
		msg->appendIntegerArgument(rankPointsStartedWith);
		msg->appendIntegerArgument(fps);
		msg->appendIntegerArgument(gameSpeedPercent);
		DEBUG_LOG(("Restarting game mode %d, Diff=%d, RankPoints=%d\n", gameMode, 
																																		TheScriptEngine->getGlobalDifficulty(), 
																																		rankPointsStartedWith)
							);
		InitRandom(seed);
	}
	//TheTransitionHandler->remove("QuitFull"); //KRISMORNESS ADD
	//quitMenuLayout = NULL; //KRISMORNESS ADD
	//isVisible = TRUE; //KRISMORNESS ADD
	//HideQuitMenu();	//KRISMORNESS ADD
	TheInGameUI->setClientQuiet( TRUE );
}

//-------------------------------------------------------------------------------------------------
/** The menu's coming and going: the layout's own transitions, as the game shipped, or none at all
	* with window animation off. */
//-------------------------------------------------------------------------------------------------
static void showQuitMenuLayout( const char *group )
{
	TheTransitionHandler->remove( group );
	TheTransitionHandler->setGroup( group );
	if( !TheGlobalData->m_animateWindows )
		TheTransitionHandler->remove( group, TRUE );
}

static void hideQuitMenuLayout( void )
{
	if( quitMenuLayout == NULL )
		return;
	if( !TheGlobalData->m_animateWindows )
		quitMenuLayout->hide( TRUE );
	else
		TheTransitionHandler->reverse( quitMenuLayout == noSaveLoadQuitMenuLayout ? "QuitNoSaveBack" : "QuitFullBack" );
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
void HideQuitMenu( void )
{
	// Note: This is called as a safety a lot, without checking for the prescence of the quit menu.
	// So don't do anything that counts on that menu actually being here.
	if(!isVisible)
		return;
	hideQuitMenuLayout();

	TheInGameUI->setQuitMenuVisible( FALSE );
	isVisible = FALSE;
	if (quitConfirmationWindow)
		TheWindowManager->winDestroy(quitConfirmationWindow);
	quitConfirmationWindow = NULL;
	if ( !TheGameLogic->isInMultiplayerGame() )
			TheGameLogic->setGamePaused(FALSE);

}

//-------------------------------------------------------------------------------------------------
/** Toggle visibility of the quit menu */
//-------------------------------------------------------------------------------------------------
void ToggleQuitMenu()
{
	
	//Added By Sadullah Nader
	//Added a check to see if we're not in game yet
	if (TheGameLogic->isIntroMoviePlaying() || TheGameLogic->isLoadingMap() ||TheScriptEngine->isGameEnding())
		return;

	//End Add

	// BGC- If we are currently in the disconnect screen, don't let the quit menu come up.
	if (TheDisconnectMenu != NULL) {
		if (TheDisconnectMenu->isScreenVisible() == TRUE) {
			return;
		}
	}

	// BGC- this is kind of hackish, but its the safest way to do it I think.
	// Basically we're seeing if either the save/load window or the options window is up
	// and if one of them is, we quit out of them rather than toggle the quit menu.
	if (TheShell->getOptionsLayout(FALSE) != NULL) {
		WindowLayout *optLayout = TheShell->getOptionsLayout(FALSE);
		GameWindow *optionsParent = optLayout->getFirstWindow();
		DEBUG_ASSERTCRASH(optionsParent != NULL, ("Not able to get the options layout parent window"));
		GameWindow *optionsBack = TheWindowManager->winGetWindowFromId(optionsParent, TheNameKeyGenerator->nameToKey( AsciiString( "OptionsMenu.wnd:ButtonBack" ) ));
		DEBUG_ASSERTCRASH(optionsBack != NULL, ("Not able to get the back button window from the options menu"));
		TheWindowManager->winSendSystemMsg(optLayout->getFirstWindow(), GBM_SELECTED, (WindowMsgData)optionsBack, NULL);
		return;
	}
	if ((saveLoadMenuLayout != NULL) && (saveLoadMenuLayout->isHidden() == FALSE)) 
	{
		GameWindow *saveLoadParent = saveLoadMenuLayout->getFirstWindow();
		DEBUG_ASSERTCRASH(saveLoadParent != NULL, ("Not able to get the save/load layout parent window"));
		GameWindow *saveLoadBack = TheWindowManager->winGetWindowFromId(saveLoadParent, TheNameKeyGenerator->nameToKey( AsciiString( "PopupSaveLoad.wnd:ButtonBack" ) ));
		DEBUG_ASSERTCRASH(saveLoadBack != NULL, ("Not able to get the back button window from the save/load menu"));
		TheWindowManager->winSendSystemMsg(saveLoadMenuLayout->getFirstWindow(), GBM_SELECTED, (WindowMsgData)saveLoadBack, NULL);
		saveLoadMenuLayout = NULL;
		return;
	}
	
	// if we're visable hide our quit menu
	if(isVisible && quitMenuLayout)
	{
		
		isVisible = FALSE;
		
		//Added By Sadullah Nader
		//Bug: When Toggling the quit menu off, the quit confirmation should also go away
		if (quitConfirmationWindow)
			TheWindowManager->winDestroy(quitConfirmationWindow);
		quitConfirmationWindow = NULL;
		//
		
		if ( !TheGameLogic->isInMultiplayerGame() )
			TheGameLogic->setGamePaused(FALSE);
		hideQuitMenuLayout();
	}
	else
	{
		//Added By Sadullah Nader
		//Added to compensate for the quit confirmation window pop-up
		
		TheMouse->setCursor( Mouse::ARROW );

		TheControlBar->hidePurchaseScience();
		const char *group;
		if ( TheGameLogic->isInMultiplayerGame()  || TheGameLogic->isInReplayGame() )
		{
			// we don't want to show the save load button.
			if(!noSaveLoadQuitMenuLayout)
				noSaveLoadQuitMenuLayout = TheWindowManager->winCreateLayout( AsciiString( "Menus/QuitNoSave.wnd" ) );
			quitMenuLayout = noSaveLoadQuitMenuLayout;
			initGadgetsNoSaveQuit();
			group = "QuitNoSave";
		}
		else
		{
			if(!fullQuitMenuLayout)
				fullQuitMenuLayout= TheWindowManager->winCreateLayout( AsciiString( "Menus/QuitMenu.wnd" ) );
			quitMenuLayout = fullQuitMenuLayout;
			initGadgetsFullQuit();
			group = "QuitFull";
		}

		// load the quit menu from the layout file if needed
		if( quitMenuLayout == NULL )
		{
			DEBUG_ASSERTCRASH(FALSE, ("Could not load a quit menu layout"));
			isVisible = FALSE;
			TheInGameUI->setQuitMenuVisible(FALSE);
			return;
		}
		showQuitMenuLayout( group );

		//quitMenuLayout->hide(FALSE);

		// if we are watching a cinematic, we need to disable the save/load button
		// because the save load window doesn't fit in the screen in letterbox mode.
		if (TheInGameUI->getInputEnabled() == FALSE) {
			if(buttonSaveLoadWin)
				buttonSaveLoadWin->winEnable(FALSE);
			buttonOptionsWin->winEnable(FALSE);
		} else if (buttonSaveLoadWin)
		{
			if(buttonSaveLoadWin)
				buttonSaveLoadWin->winEnable(TRUE);
			buttonOptionsWin->winEnable(TRUE);
		}

		// Disable the restart, load, save, etc buttons in network games
		if ( TheGameLogic->isInMultiplayerGame() || TheGameLogic->isInSkirmishGame() )
		{
			buttonRestartWin->winEnable(TRUE);
			if (TheGameLogic->isInSkirmishGame() == FALSE) {
				GadgetButtonSetText(buttonRestartWin, TheGameText->fetch("GUI:Surrender"));
				
			}
	
			if (TheGameLogic->isInSkirmishGame() == TRUE) {
				TheGameLogic->setGamePaused(TRUE);
			}

			if ((!ThePlayerList->getLocalPlayer()->isPlayerActive() || TheVictoryConditions->isLocalAlliedVictory()) &&
						(TheGameLogic->isInSkirmishGame() == FALSE))
			{
				buttonRestartWin->winEnable(FALSE); // can't surrender when you're dead
			}
		}
		else
		{
			buttonRestartWin->winEnable(TRUE);
			if(!TheGameLogic->isInReplayGame())
			{
				GadgetButtonSetText(buttonRestartWin, TheGameText->fetch("GUI:RestartMission"));
				GadgetButtonSetText(buttonExitWin, TheGameText->fetch("GUI:ExitMission"));
				
			}
			//if we're not in a multiplayer game, pause the game
			TheGameLogic->setGamePaused(TRUE);
		}


		if (quitConfirmationWindow)
			TheWindowManager->winDestroy(quitConfirmationWindow);
		quitConfirmationWindow = NULL;
		HideDiplomacy();
		HideInGameChat();
		TheControlBar->hidePurchaseScience();
		isVisible = TRUE;
	}
		
	TheInGameUI->setQuitMenuVisible(isVisible);
	
}  // end ToggleQuitMenu

//-------------------------------------------------------------------------------------------------
/** Quit menu window system callback */
//-------------------------------------------------------------------------------------------------
WindowMsgHandledType QuitMenuSystem( GameWindow *window, UnsignedInt msg, 
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

			break;

		}  // end case

		//---------------------------------------------------------------------------------------------
		case GBM_SELECTED:
		{
			GameWindow *control = (GameWindow *)mData1;
			Int controlID = control->winGetWindowId();

      if( controlID == buttonSaveLoad )
      {

				//
        // these commented lines (12-11-2002) will allow access to load only when were
        // viewing an in-game cinema ... but it's brittle, so I'm disableing it for
        // now and just using the grey button for the whole save/load button for now
        // during a cinema
        //
              	
//				SaveLoadLayoutType layoutType = SLLT_SAVE_AND_LOAD;

				//
				// if input is disabled we are in an in-game cinematic and can load only, since we
				// are in the quit menu we are paused and therefore input is disabled all
				// the time ... in order to figure out if the *game* has input disabled we need
				// to query for the input enabled memory in the game logic which represents the
				// input enabled status of the game if we were not currently paused
				//
//				if( TheGameLogic->getInputEnabledMemory() == FALSE )
//					layoutType = SLLT_LOAD_ONLY;

        saveLoadMenuLayout = TheShell->getSaveLoadMenuLayout();
//				saveLoadMenuLayout->runInit( &layoutType );
				saveLoadMenuLayout->runInit();				
				saveLoadMenuLayout->hide( FALSE );
				saveLoadMenuLayout->bringForward();
      }
			else if( controlID == buttonExit )
			{
        quitConfirmationWindow = QuitMessageBoxYesNo(TheGameText->fetch("GUI:QuitPopupTitle"), TheGameText->fetch("GUI:QuitPopupMessage"),/*quitCallback*/exitQuitMenu,noExitQuitMenu);
			}  // end if
			else if( controlID == buttonReturn )
			{
			
				// hide this menu
				ToggleQuitMenu();

			}  // end else if
			else if( buttonOptions == controlID )
			{
				WindowLayout *optLayout = TheShell->getOptionsLayout(TRUE);
				DEBUG_ASSERTCRASH(optLayout != NULL, ("options menu layout is NULL"));
				optLayout->runInit();
				optLayout->hide(FALSE);
				optLayout->bringForward();
			}
//			else if( controlID == buttonQuitToDesktop )
//			{
//				quitConfirmationWindow = MessageBoxYesNo(TheGameText->fetch("GUI:QuitPopupTitle"), TheGameText->fetch("GUI:QuitToDesktopConf"),/*quitCallback*/quitToDesktopQuitMenu,noExitQuitMenu);
//
//			}  // end else if
			else if( controlID == buttonRestart )
			{
				if ( TheGameLogic->isInMultiplayerGame() )
				{
					// we really want to surrender
					quitConfirmationWindow = MessageBoxYesNo(TheGameText->fetch("GUI:SurrenderConfirmationTitle"), 
																			TheGameText->fetch("GUI:SurrenderConfirmation"),
																			/*quitCallback*/surrenderQuitMenu,noExitQuitMenu);
				}
				else
				{
					//we really want to restart
					quitConfirmationWindow = MessageBoxYesNo(TheGameText->fetch("GUI:RestartConfirmationTitle"), 
																			TheGameText->fetch("GUI:RestartConfirmation"),
																			/*quitCallback*/restartMissionMenu,noExitQuitMenu);
				}
			}  // end else if

			break;

		}  // end selected

		default:
			return MSG_IGNORED;

	}  // end switch

	return MSG_HANDLED;

}  // end QuitMenuSystem
