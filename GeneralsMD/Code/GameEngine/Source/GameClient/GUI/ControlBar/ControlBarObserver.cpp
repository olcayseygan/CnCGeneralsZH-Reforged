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

// FILE: ControlBarObserver.cpp /////////////////////////////////////////////////
//-----------------------------------------------------------------------------
//                                                                          
//                       Electronic Arts Pacific.                          
//                                                                          
//                       Confidential Information                           
//                Copyright (C) 2002 - All Rights Reserved                  
//                                                                          
//-----------------------------------------------------------------------------
//
//	created:	Aug 2002
//
//	Filename: 	ControlBarObserver.cpp
//
//	author:		Chris Huybregts
//	
//	purpose:	All things related to the Observer Control bar, are in here.
//
//-----------------------------------------------------------------------------
///////////////////////////////////////////////////////////////////////////////

//-----------------------------------------------------------------------------
// SYSTEM INCLUDES ////////////////////////////////////////////////////////////
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// USER INCLUDES //////////////////////////////////////////////////////////////
//-----------------------------------------------------------------------------
#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "Common/NameKeyGenerator.h"
#include "Common/PlayerList.h"
#include "Common/Player.h"
#include "Common/PlayerTemplate.h"
#include "Common/KindOf.h"
#include "Common/Recorder.h"
#include "GameClient/ControlBar.h"
#include "GameClient/PlayerColorScheme.h"
#include "GameClient/GameWindowManager.h"
#include "GameClient/GadgetPushButton.h"
#include "GameClient/GadgetStaticText.h"
#include "GameClient/GameText.h"
#include "GameClient/InGameUI.h"
#include "GameClient/ObserverCamera.h"
#include "GameClient/Display.h"
#include "GameClient/Image.h"
#include "GameNetwork/NetworkDefs.h"
//-----------------------------------------------------------------------------
// DEFINES ////////////////////////////////////////////////////////////////////
//-----------------------------------------------------------------------------
// EA's column of eight player buttons beside the radar is gone: the watcher picks a player on the
// standings, their follow tabs or the queue's badges (InGameUI), which list every seat MAX_SLOTS allows,
// and the column's window stays down with EA's readouts window
static GameWindow *ObserverPlayerInfoWindow = NULL;
static GameWindow *ObserverPlayerListWindow = NULL;

static NameKeyType buttonCancelID = NAMEKEY_INVALID;

static GameWindow *winFlag = NULL;
static GameWindow *winGeneralPortrait = NULL;
static GameWindow *staticTextNumberOfUnits = NULL;
static GameWindow *staticTextNumberOfBuildings = NULL;
static GameWindow *staticTextNumberOfUnitsKilled = NULL;
static GameWindow *staticTextNumberOfUnitsLost = NULL;
static GameWindow *staticTextPlayerName = NULL;

//-----------------------------------------------------------------------------
// PUBLIC FUNCTIONS ///////////////////////////////////////////////////////////
//-----------------------------------------------------------------------------


void ControlBar::initObserverControls( void )
{
	ObserverPlayerInfoWindow = TheWindowManager->winGetWindowFromId(NULL, TheNameKeyGenerator->nameToKey("ControlBar.wnd:ObserverPlayerInfoWindow"));
	ObserverPlayerListWindow = TheWindowManager->winGetWindowFromId(NULL, TheNameKeyGenerator->nameToKey("ControlBar.wnd:ObserverPlayerListWindow"));

	staticTextNumberOfUnits = TheWindowManager->winGetWindowFromId(NULL, TheNameKeyGenerator->nameToKey("ControlBar.wnd:StaticTextNumberOfUnits"));
	staticTextNumberOfBuildings = TheWindowManager->winGetWindowFromId(NULL, TheNameKeyGenerator->nameToKey("ControlBar.wnd:StaticTextNumberOfBuildings"));
	staticTextNumberOfUnitsKilled = TheWindowManager->winGetWindowFromId(NULL, TheNameKeyGenerator->nameToKey("ControlBar.wnd:StaticTextNumberOfUnitsKilled"));
	staticTextNumberOfUnitsLost = TheWindowManager->winGetWindowFromId(NULL, TheNameKeyGenerator->nameToKey("ControlBar.wnd:StaticTextNumberOfUnitsLost"));
	staticTextPlayerName = TheWindowManager->winGetWindowFromId(NULL, TheNameKeyGenerator->nameToKey("ControlBar.wnd:StaticTextPlayerName"));
	winFlag = TheWindowManager->winGetWindowFromId(NULL, TheNameKeyGenerator->nameToKey("ControlBar.wnd:WinFlag"));
	winGeneralPortrait = TheWindowManager->winGetWindowFromId(NULL, TheNameKeyGenerator->nameToKey("ControlBar.wnd:WinGeneralPortrait"));
	
	buttonCancelID = TheNameKeyGenerator->nameToKey("ControlBar.wnd:ButtonCancel");
}

//-------------------------------------------------------------------------------------------------
/** System callback for the ControlBarObserverSystem */
//-------------------------------------------------------------------------------------------------
WindowMsgHandledType ControlBarObserverSystem( GameWindow *window, UnsignedInt msg, 
																			 WindowMsgData mData1, WindowMsgData mData2 )
{
	static NameKeyType buttonCommunicator = NAMEKEY_INVALID;
	
	switch( msg ) 
	{
		// --------------------------------------------------------------------------------------------
		case GWM_CREATE:
		{
				break;

		}  // end create

		//---------------------------------------------------------------------------------------------
		case GBM_MOUSE_ENTERING:
		case GBM_MOUSE_LEAVING:
		{
			break;
		}

		//---------------------------------------------------------------------------------------------
		case GBM_SELECTED:
		case GBM_SELECTED_RIGHT:
		{
			GameWindow *control = (GameWindow *)mData1;

			Int controlID = control->winGetWindowId();
			// the cancel button lets go of the player picked, his look and his follow, as Esc does
			if( controlID == buttonCancelID)
			{
				TheObserverCamera.stopLooking();
				TheObserverCamera.followPlayer( ObserverCamera::NO_PLAYER );
				TheControlBar->watchPlayer( NULL );
			}

		//	if( controlID == buttonCommunicator && TheGameLogic->getGameMode() == GAME_INTERNET )
	/*
		{
				popupCommunicatorLayout = TheWindowManager->winCreateLayout( AsciiString( "Menus/PopupCommunicator.wnd" ) );
				popupCommunicatorLayout->runInit();
				popupCommunicatorLayout->hide( FALSE );
				popupCommunicatorLayout->bringForward();
			}
*/

			break;

		}  // end button selected

		//---------------------------------------------------------------------------------------------
		default:
			return MSG_IGNORED;

	}  // end switch( msg )

	return MSG_HANDLED;

}  // end ControlBarSystem

//-----------------------------------------------------------------------------
// PRIVATE FUNCTIONS //////////////////////////////////////////////////////////
//-----------------------------------------------------------------------------

//-------------------------------------------------------------------------------------------------
/** Both of EA's observer windows stay down: the column of eight player buttons and the readouts
	* window that took its place.  Every seat is on the standings and the queue now, and the picked
	* player's numbers are the card InGameUI draws beside the radar. */
//-------------------------------------------------------------------------------------------------
void ControlBar::populateObserverList( void )
{
	if( ObserverPlayerInfoWindow != NULL )
		ObserverPlayerInfoWindow->winHide( TRUE );
	if( ObserverPlayerListWindow != NULL )
		ObserverPlayerListWindow->winHide( TRUE );
}

void ControlBar::showObserverPlayerInfo( void )
{
	populateObserverList();
}

void ControlBar::showObserverPlayerList( void )
{
	populateObserverList();
}

void ControlBar::populateObserverInfoWindow ( void )
{
	if(ObserverPlayerInfoWindow->winIsHidden())
		return;

	if( !m_observerLookAtPlayer )
	{
		ObserverPlayerInfoWindow->winHide(TRUE);
		ObserverPlayerListWindow->winHide(FALSE);
		populateObserverList();
		return;
	}
							
	UnicodeString uString;
	KindOfMaskType mask,clearmask;
	mask.set(KINDOF_SCORE);
	clearmask.set(KINDOF_STRUCTURE);
	
	uString.format(u"%d",m_observerLookAtPlayer->countObjects(mask,clearmask));
	GadgetStaticTextSetText(staticTextNumberOfUnits, uString);

	Int numBuildings = 0;
	mask.clear();
	mask.set(KINDOF_SCORE);
	mask.set(KINDOF_STRUCTURE);
	clearmask.clear();
	numBuildings = m_observerLookAtPlayer->countObjects(mask,clearmask);
	mask.clear();
	mask.set(KINDOF_SCORE_CREATE);
	mask.set(KINDOF_STRUCTURE);
	numBuildings += m_observerLookAtPlayer->countObjects(mask,clearmask);
	mask.clear();
	mask.set(KINDOF_SCORE_DESTROY);
	mask.set(KINDOF_STRUCTURE);
	numBuildings += m_observerLookAtPlayer->countObjects(mask,clearmask);
	uString.format(u"%d",numBuildings);
	GadgetStaticTextSetText(staticTextNumberOfBuildings, uString);
	uString.format(u"%d",m_observerLookAtPlayer->getScoreKeeper()->getTotalUnitsDestroyed());
	GadgetStaticTextSetText(staticTextNumberOfUnitsKilled, uString);
	uString.format(u"%d",m_observerLookAtPlayer->getScoreKeeper()->getTotalUnitsLost());
	GadgetStaticTextSetText(staticTextNumberOfUnitsLost, uString);
	GadgetStaticTextSetText(staticTextPlayerName, m_observerLookAtPlayer->getPlayerDisplayName());
	Color color = clientPlayerColor( m_observerLookAtPlayer );
	staticTextPlayerName->winSetEnabledTextColors(color, GameMakeColor(0,0,0,255));
	winFlag->winSetEnabledImage(0, m_observerLookAtPlayer->getPlayerTemplate()->getFlagWaterMarkImage());
	winGeneralPortrait->winHide(FALSE);
}
