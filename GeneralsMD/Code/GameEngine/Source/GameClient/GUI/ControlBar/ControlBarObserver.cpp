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
#include "GameClient/ControlBarScheme.h"
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
enum { MAX_BUTTONS = 8};
static NameKeyType buttonPlayerID[MAX_BUTTONS] = { NAMEKEY_INVALID,NAMEKEY_INVALID,
																										NAMEKEY_INVALID,NAMEKEY_INVALID,
																										NAMEKEY_INVALID,NAMEKEY_INVALID,
																										NAMEKEY_INVALID,NAMEKEY_INVALID };
static NameKeyType staticTextPlayerID[MAX_BUTTONS] = { NAMEKEY_INVALID,NAMEKEY_INVALID,
																										NAMEKEY_INVALID,NAMEKEY_INVALID,
																										NAMEKEY_INVALID,NAMEKEY_INVALID,
																										NAMEKEY_INVALID,NAMEKEY_INVALID };
static GameWindow *ObserverPlayerInfoWindow = NULL;
static GameWindow *ObserverPlayerListWindow = NULL;

static GameWindow *buttonPlayer[MAX_BUTTONS] = {NULL,NULL,NULL,NULL,
																							NULL,NULL,NULL,NULL };
static GameWindow *staticTextPlayer[MAX_BUTTONS] = {NULL,NULL,NULL,NULL,
																							NULL,NULL,NULL,NULL };


static NameKeyType buttonCancelID = NAMEKEY_INVALID;

static GameWindow *winFlag = NULL;
static GameWindow *winGeneralPortrait = NULL;
static GameWindow *staticTextNumberOfUnits = NULL;
static GameWindow *staticTextNumberOfBuildings = NULL;
static GameWindow *staticTextNumberOfUnitsKilled = NULL;
static GameWindow *staticTextNumberOfUnitsLost = NULL;
static GameWindow *staticTextPlayerName = NULL;

static GameWinDrawFunc buttonPlayerDraw = NULL;		///< the push button's own draw, which the row draw ends with
static const Image *rowHover = NULL;
static const Image *rowWatched = NULL;

//-------------------------------------------------------------------------------------------------
/** A row of the observer's column: his colour's lamp on the plate beside his button, and the ring
	* round it while the pointer is over it (ObserverProRowH) or while he is the one watched or
	* followed (ObserverProRowS, which wins).  Numbers are the button's own at 1080, 45x30. */
//-------------------------------------------------------------------------------------------------
static void drawObserverRow( GameWindow *window, WinInstanceData *instData )
{
	enum { BUTTON_H = 30, RING_OUT = 4, RING_W = 53, RING_H = 38, LAMP_X = 49, LAMP_Y = 4, LAMP_W = 7, LAMP_H = 22,
				 SHINE_X = 50, SHINE_Y = 6, SHINE_W = 2, SHINE_H = 18, SHINE_LIFT = 90 };
	ICoord2D at, size;
	window->winGetScreenPosition( &at.x, &at.y );
	window->winGetSize( &size.x, &size.y );
	const Real k = (Real)size.y / BUTTON_H;
	auto px = [ k ]( Int at1080 ) { return REAL_TO_INT( at1080 * k + 0.5f ); };

	const Player *player = (const Player *)GadgetButtonGetData( window );
	const Bool watched = player == TheControlBar->getObserverLookAtPlayer()
		|| player->getPlayerIndex() == TheObserverCamera.getFollowedPlayerIndex();
	const Image *ring = watched ? rowWatched : ( BitTest( instData->getState(), WIN_STATE_HILITED ) ? rowHover : NULL );
	if( ring )
		TheDisplay->drawImage( ring, at.x - px( RING_OUT ), at.y - px( RING_OUT ),
													 at.x - px( RING_OUT ) + px( RING_W ), at.y - px( RING_OUT ) + px( RING_H ) );

	const Color colour = clientPlayerColor( player );
	UnsignedByte r, g, b, a;
	GameGetColorComponents( colour, &r, &g, &b, &a );
	auto lift = []( UnsignedByte c ) { return (UnsignedByte)min( (Int)c + SHINE_LIFT, 255 ); };
	TheDisplay->drawFillRect( at.x + px( LAMP_X ), at.y + px( LAMP_Y ), px( LAMP_W ), px( LAMP_H ), GameMakeColor( r, g, b, 255 ) );
	TheDisplay->drawFillRect( at.x + px( SHINE_X ), at.y + px( SHINE_Y ), px( SHINE_W ), px( SHINE_H ),
														GameMakeColor( lift( r ), lift( g ), lift( b ), 255 ) );

	buttonPlayerDraw( window, instData );
}

//-----------------------------------------------------------------------------
// PUBLIC FUNCTIONS ///////////////////////////////////////////////////////////
//-----------------------------------------------------------------------------


void ControlBar::initObserverControls( void )
{
	rowHover = TheMappedImageCollection->findImageByName( "ObserverProRowH" );
	rowWatched = TheMappedImageCollection->findImageByName( "ObserverProRowS" );
	ObserverPlayerInfoWindow = TheWindowManager->winGetWindowFromId(NULL, TheNameKeyGenerator->nameToKey("ControlBar.wnd:ObserverPlayerInfoWindow"));
	ObserverPlayerListWindow = TheWindowManager->winGetWindowFromId(NULL, TheNameKeyGenerator->nameToKey("ControlBar.wnd:ObserverPlayerListWindow"));

	for (Int i = 0; i < MAX_BUTTONS; i++)
	{
		AsciiString tmpString;
		tmpString.format("ControlBar.wnd:ButtonPlayer%d", i);
		buttonPlayerID[i] = TheNameKeyGenerator->nameToKey( tmpString );
		buttonPlayer[i] = TheWindowManager->winGetWindowFromId( ObserverPlayerListWindow, buttonPlayerID[i] );
		// the windows are made again on a new screen size, each with the button's own draw again
		if( buttonPlayer[i]->winGetDrawFunc() != drawObserverRow )
		{
			buttonPlayerDraw = buttonPlayer[i]->winGetDrawFunc();
			buttonPlayer[i]->winSetDrawFunc( drawObserverRow );
		}
		tmpString.format("ControlBar.wnd:StaticTextPlayer%d", i);
		staticTextPlayerID[i] = TheNameKeyGenerator->nameToKey( tmpString );
		staticTextPlayer[i] = TheWindowManager->winGetWindowFromId( ObserverPlayerListWindow, staticTextPlayerID[i] );
	}
	
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
			if( controlID == buttonCancelID)
			{
				TheObserverCamera.stopLooking();
				TheControlBar->setObserverLookAtPlayer(NULL);
				ObserverPlayerInfoWindow->winHide(TRUE);
				ObserverPlayerListWindow->winHide(FALSE);
				TheControlBar->populateObserverList();

			}
			for(Int i = 0; i <MAX_BUTTONS; ++i)
			{
				if( controlID == buttonPlayerID[i])
				{
					// his button is a look through his eyes: his fog, his camera; the same button again,
					// the cancel button or Esc gives the whole map back.  All of it is this screen's
					// picture, nothing reaches the logic
					Player *picked = (Player *) GadgetButtonGetData( buttonPlayer[i] );
					if( TheObserverCamera.isFogOn() && TheObserverCamera.getFollowedPlayerIndex() == picked->getPlayerIndex() )
					{
						TheObserverCamera.stopLooking();
						TheControlBar->watchPlayer( NULL );
						return MSG_HANDLED;
					}
					TheInGameUI->deselectAllDrawables();
					TheObserverCamera.lookThrough( picked->getPlayerIndex() );
					TheInGameUI->message( AsciiString( "CONTROLBAR:ObsLookingThrough" ), picked->getPlayerDisplayName().str() );

					ObserverPlayerInfoWindow->winHide(FALSE);
					ObserverPlayerListWindow->winHide(TRUE);
					TheControlBar->setObserverLookAtPlayer(picked);
					TheControlBar->populateObserverInfoWindow();
					return MSG_HANDLED;
				}
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

void ControlBar::populateObserverList( void )
{
	Int currentButton = 0, i;
	if(TheRecorder->isMultiplayer())
	{
	
		for (i = 0; i < MAX_SLOTS; ++i)
		{
			AsciiString name;
			name.format("player%d", i);
			Player *p = ThePlayerList->findPlayerWithNameKey(TheNameKeyGenerator->nameToKey(name));
			if(p)
			{
				if(p->isPlayerObserver())
					continue;
				DEBUG_ASSERTCRASH(currentButton < MAX_BUTTONS, ("ControlBar::populateObserverList trying to populate more buttons then we have"));
				GadgetButtonSetData(buttonPlayer[currentButton], (void *)p);
				GadgetButtonSetEnabledImage( buttonPlayer[currentButton], ControlBarSideEmblem( p->getPlayerTemplate() ) );
				//GadgetButtonSetHiliteImage( buttonPlayer[currentButton], p->getPlayerTemplate()->getHiliteImage() );
				//GadgetButtonSetHiliteSelectedImage( buttonPlayer[currentButton], p->getPlayerTemplate()->getPushedImage() );
				//GadgetButtonSetDisabledImage( buttonPlayer[currentButton], p->getPlayerTemplate()->getDisabledImage() );
				buttonPlayer[currentButton]->winSetTooltip(p->getPlayerDisplayName());
				buttonPlayer[currentButton]->winHide(FALSE);
				buttonPlayer[currentButton]->winSetStatus( WIN_STATUS_USE_OVERLAY_STATES );

				// The slot this player actually sits in is i, not currentButton - currentButton only
				// counts the players put on the bar so far, so one empty slot or one observer earlier
				// in the list shifted every player after it onto somebody else's team.
				const GameSlot *slot = TheGameInfo ? TheGameInfo->getConstSlot(i) : NULL;
				Color playerColor = clientPlayerColor( p );
				Color backColor = GameMakeColor(0, 0, 0, 255);
				staticTextPlayer[currentButton]->winSetEnabledTextColors( playerColor, backColor );
				staticTextPlayer[currentButton]->winHide(FALSE);
				AsciiString teamStr;
				if (slot == NULL)
					teamStr = "Team:AI";
				else if (slot->isAI() && slot->getTeamNumber() == -1)
					teamStr = "Team:AI";
				else
					teamStr.format("Team:%d", slot->getTeamNumber() + 1);

				UnicodeString text;
				text.format(TheGameText->fetch("CONTROLBAR:ObsPlayerLabel"), p->getPlayerDisplayName().str(),
					TheGameText->fetch(teamStr).str());

				GadgetStaticTextSetText(staticTextPlayer[currentButton], text );

				++currentButton;
			}
		}
		for(currentButton; currentButton<MAX_BUTTONS; ++currentButton)
		{
			buttonPlayer[currentButton]->winHide(TRUE);
			staticTextPlayer[currentButton]->winHide(TRUE);
		}
	}
	else
	{
		for(i =0; i < MAX_PLAYER_COUNT; ++i)
		{
			Player *p = ThePlayerList->getNthPlayer(i);
			if(p && !p->isPlayerObserver() && p->getPlayerType() == PLAYER_HUMAN)
			{
				DEBUG_ASSERTCRASH(currentButton < MAX_BUTTONS, ("ControlBar::populateObserverList trying to populate more buttons then we have"));
				GadgetButtonSetData(buttonPlayer[currentButton], (void *)p);
				GadgetButtonSetEnabledImage( buttonPlayer[currentButton], ControlBarSideEmblem( p->getPlayerTemplate() ) );
				//GadgetButtonSetHiliteImage( buttonPlayer[currentButton], p->getPlayerTemplate()->getHiliteImage() );
				//GadgetButtonSetHiliteSelectedImage( buttonPlayer[currentButton], p->getPlayerTemplate()->getPushedImage() );
				//GadgetButtonSetDisabledImage( buttonPlayer[currentButton], p->getPlayerTemplate()->getDisabledImage() );
				buttonPlayer[currentButton]->winSetTooltip(p->getPlayerDisplayName());
				buttonPlayer[currentButton]->winHide(FALSE);
				buttonPlayer[currentButton]->winSetStatus( WIN_STATUS_USE_OVERLAY_STATES );

				Color playerColor = clientPlayerColor( p );
				Color backColor = GameMakeColor(0, 0, 0, 255);
				staticTextPlayer[currentButton]->winSetEnabledTextColors( playerColor, backColor );
				staticTextPlayer[currentButton]->winHide(FALSE);
				GadgetStaticTextSetText(staticTextPlayer[currentButton], p->getPlayerDisplayName());

				++currentButton;
				break;
			}
		}
		for(currentButton; currentButton<MAX_BUTTONS; ++currentButton)
		{
			buttonPlayer[currentButton]->winHide(TRUE);
			staticTextPlayer[currentButton]->winHide(TRUE);
		}
	}
}

//-------------------------------------------------------------------------------------------------
/** Put up the readouts of the player the bar is looking at, and take the list down - what pressing
	* his button in the list does, reached from the selection as well. */
//-------------------------------------------------------------------------------------------------
void ControlBar::showObserverPlayerInfo( void )
{
	if( ObserverPlayerInfoWindow == NULL || ObserverPlayerListWindow == NULL )
		return;

	ObserverPlayerInfoWindow->winHide( FALSE );
	ObserverPlayerListWindow->winHide( TRUE );
	populateObserverInfoWindow();
}

//-------------------------------------------------------------------------------------------------
/** ... and back to the list of everybody, which is what the cancel button does. */
//-------------------------------------------------------------------------------------------------
void ControlBar::showObserverPlayerList( void )
{
	if( ObserverPlayerInfoWindow == NULL || ObserverPlayerListWindow == NULL )
		return;

	ObserverPlayerInfoWindow->winHide( TRUE );
	ObserverPlayerListWindow->winHide( FALSE );
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
