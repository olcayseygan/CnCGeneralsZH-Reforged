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

// FILE: ControlBarCallback.cpp ///////////////////////////////////////////////////////////////////
// Author: Colin Day - October 2001
// Desc: GUI Control bar at the bottom of the screen that houses the
//			 the build buttons, radar etc.
///////////////////////////////////////////////////////////////////////////////////////////////////

// INCLUDES ///////////////////////////////////////////////////////////////////////////////////////
#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "Common/NameKeyGenerator.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/MessageStream.h"
#include "Common/ThingFactory.h"
#include "Common/ThingTemplate.h"
#include "Common/Radar.h"
#include "GameClient/AnimateWindowManager.h"
#include "GameClient/Drawable.h"
#include "GameClient/GameWindow.h"
#include "GameClient/Gadget.h"
#include "GameClient/GadgetTextEntry.h"
#include "GameClient/GameClient.h"
#include "GameClient/GUICallbacks.h"
#include "GameClient/InGameUI.h"
#include "GameClient/ControlBar.h"
#include "GameClient/GameWindowManager.h"
#include "GameClient/LanguageFilter.h"
#include "GameClient/CommandXlat.h"
#include "GameClient/ObserverCamera.h"

#include "GameLogic/GameLogic.h"
#include "GameLogic/ScriptEngine.h"

//external declarations of the Gadgets the callbacks can use
WindowLayout *popupCommunicatorLayout = NULL;

// The drag message of the button whose press on the radar moved the camera, or GWM_NONE.  Dragging
// that button on keeps the camera under the cursor; a drag that came onto the radar from the
// battlefield, a selection box say, was never pressed here and leaves the camera alone.
static UnsignedInt s_radarLookDrag = GWM_NONE;

//-------------------------------------------------------------------------------------------------
/** The mouse position packed into a window message, relative to that window's corner */
//-------------------------------------------------------------------------------------------------
static ICoord2D windowLocalMouse( GameWindow *window, WindowMsgData mData1 )
{
	ICoord2D screenPos;
	window->winGetScreenPosition( &screenPos.x, &screenPos.y );

	ICoord2D mouse;
	mouse.x = (mData1 & 0xFFFF) - screenPos.x;
	mouse.y = (mData1 >> 16) - screenPos.y;
	return mouse;
}

//-------------------------------------------------------------------------------------------------
/** The spot on the map under a pixel of the radar window.  FALSE off the radar's picture. */
//-------------------------------------------------------------------------------------------------
static Bool radarPixelToWorld( const ICoord2D *pixel, Coord3D *world )
{
	ICoord2D radar;
	return (TheRadar->isRadarHidden() == FALSE || TheRadar->isRadarForced()) &&
		TheRadar->localPixelToRadar( pixel, &radar ) &&
		TheRadar->radarToWorld( &radar, world );
}

//-------------------------------------------------------------------------------------------------
/** The camera to the spot under the radar.  A watcher's camera is his from then on: left with the
	* director or a player's screen it was eased straight back, a click less than a jump away and every
	* step of a drag (ObserverCamera.h). */
//-------------------------------------------------------------------------------------------------
static void radarLookAt( const Coord3D *world )
{
	TheTacticalView->lookAt( world );
	TheObserverCamera.setMode( OBSERVER_CAMERA_FREE );
}


//-------------------------------------------------------------------------------------------------
/** Input procedure for the left HUD */
//-------------------------------------------------------------------------------------------------
WindowMsgHandledType LeftHUDInput( GameWindow *window, UnsignedInt msg,
																	 WindowMsgData mData1, WindowMsgData mData2 )
{
	
	// get player
	Player *player = ThePlayerList->getLocalPlayer();

	//
	// if the player doesn't have a radar, or the radar is hidden, and the radar is not being
	// forced to on, we just eat input over the radar window
	//
	if( !TheRadar->isRadarForced() && (TheRadar->isRadarHidden() || !player->hasRadar()) )
		return MSG_HANDLED;
	
	// If the middle mouse button is depressed, then just let the message fall all the 
	// way back to the usual middle mouse button processing.
	// jkmcd
	if( TheMouse->getMouseStatus()->middleState == MBS_Down )
		return MSG_IGNORED;

	switch( msg ) 
	{

		/** @todo
			This is wrong.  The radar should be in the message stream, and eat all messages and propagate them
			as a new message with the coords converted to world coords and the message flagged as being from 
			the radar.  This would let all of the normal processing occur, and allow individual commands to easily 
			reject being used on the radar.
		*/

		// ------------------------------------------------------------------------
		case GWM_NONE:
		case GWM_MOUSE_ENTERING:
		case GWM_MOUSE_LEAVING:
		{

			// A left press holds the radar and never sees these until it lets go.  A right drag does,
			// and one that leaves the radar has finished moving the camera.
			if( msg != GWM_NONE )
				s_radarLookDrag = GWM_NONE;

			//
			// consider changing the mouse cursor if we are not in the process of firing
			// targeted "superweapons" which we can use the radar itself to fire
			//
			Bool targeting = FALSE;
			const CommandButton *command = TheInGameUI->getGUICommand();
			if( command 
					&& (command->getCommandType() == GUI_COMMAND_SPECIAL_POWER || command->getCommandType() == GUI_COMMAND_SPECIAL_POWER_FROM_SHORTCUT)
					&& BitTest( command->getOptions(), NEED_TARGET_POS ) )
				targeting = TRUE;

			if( targeting == FALSE )
			{
				const DrawableList *drawableList = TheInGameUI->getAllSelectedLocalDrawables();
				Mouse::MouseCursor cur = Mouse::ARROW;

				if (!(drawableList->empty() || msg == GWM_MOUSE_LEAVING))
				{
					if (TheInGameUI->isInAttackMoveToMode() || (command && command->getCommandType() == GUI_COMMAND_ATTACK_MOVE))
					{
						cur = Mouse::ATTACKMOVETO;
					}
					else
					{
						cur = Mouse::MOVETO;
					}
				}

				// Groovy
				TheMouse->setCursor(cur);

			}  // end if

			return MSG_HANDLED;
		}

		// --------------------------------------------------------------------------------------------
		case GWM_MOUSE_POS:
		{

			// get mouse position
			ICoord2D mouse;
			mouse.x = mData1 & 0xFFFF;
			mouse.y = mData1 >> 16;

			// get window screen position
			ICoord2D screenPos;
			window->winGetScreenPosition( &screenPos.x, &screenPos.y );

			// set mouse position to be relative to this window
			mouse.x -= screenPos.x;
			mouse.y -= screenPos.y;

			// is the mouse in the radar window
			ICoord2D radar;
			if( (TheRadar->isRadarHidden() == FALSE || TheRadar->isRadarForced()) &&
					TheRadar->localPixelToRadar( &mouse, &radar ) )
			{

/*
//
// this is an example piece of code to find the object under the pixel position 
// of the radar ... should we in the future wish to allow commands to be executed
// on objects throught he radar.  note tho that this is extremely hard to do because
// the pixels on the radar are very small and it's hard to do accurate targeting
//

				Object *obj = TheRadar->objectUnderRadarPixel( &mouse );
				UnicodeString msg;
				if( obj )
					msg.format( L"Object under mouse on radar '%S'(%d)", 
											obj->getTemplate()->getName().str(), obj->getID() );
				else
					msg.format( L"Mouse (%d,%d) in Radar window L(%d,%d)", mouse.x, mouse.y, radar.x, radar.y );
				TheInGameUI->message( msg );
*/

				// keep the cursor for any context commands
				const CommandButton *command = TheInGameUI->getGUICommand();
				if( command 
						&& (command->getCommandType() == GUI_COMMAND_SPECIAL_POWER || command->getCommandType() == GUI_COMMAND_SPECIAL_POWER_FROM_SHORTCUT)
						&& BitTest( command->getOptions(), NEED_TARGET_POS ) )
				{
					Int index = TheMouse->getCursorIndex( command->getCursorName() );

					if( index != Mouse::INVALID_MOUSE_CURSOR )
						TheMouse->setCursor( (Mouse::MouseCursor)index );
					else
						TheMouse->setCursor( Mouse::CROSS );

				}  // end if
				else
				{
					// Else we are not super targeting, so we have to try to refresh the move cursor.
					// We can't just do this on Enter and Exit, because hotkeys allow state to change
					// while we are in the radar.
					const DrawableList *drawableList = TheInGameUI->getAllSelectedLocalDrawables();
					Mouse::MouseCursor cur = Mouse::ARROW;

					if (!(drawableList->empty() || msg == GWM_MOUSE_LEAVING))
					{
						if (TheInGameUI->isInAttackMoveToMode() || (command && command->getCommandType() == GUI_COMMAND_ATTACK_MOVE))
						{
							cur = Mouse::ATTACKMOVETO;
						}
						else
						{
							cur = Mouse::MOVETO;
						}
					}

					// Groovy
					TheMouse->setCursor(cur);
				}

			}  // end if

			break;

		}  // end case mouse position

		// ------------------------------------------------------------------------
		case GWM_RIGHT_UP:// Here to eat
		case GWM_LEFT_UP:// Here to eat
			s_radarLookDrag = GWM_NONE;
			break;

		// ------------------------------------------------------------------------
		// Holding the button that moved the camera and dragging keeps the camera under the cursor.
		case GWM_LEFT_DRAG:
		case GWM_RIGHT_DRAG:
		{
			if( msg != s_radarLookDrag )
				return MSG_IGNORED;

			// A left press on the radar holds it until the release, so a left drag that is not held
			// here started somewhere else, even if the release that ended the last one never reached us
			if( msg == GWM_LEFT_DRAG && TheWindowManager->winGetGrabWindow() != window )
				return MSG_IGNORED;

			// A left press holds the radar for the whole drag, so the cursor can run off the picture.
			// It pins to the picture's edge there and the camera stops on the map's edge, rather than
			// wherever the last move inside the picture happened to leave it.
			ICoord2D mouse = windowLocalMouse( window, mData1 );
			ICoord2D size, ul, lr;
			window->winGetSize( &size.x, &size.y );
			TheRadar->findDrawPositions( 0, 0, size.x, size.y, &ul, &lr );
			mouse.x = max( ul.x, min( mouse.x, lr.x - 1 ) );
			mouse.y = max( ul.y, min( mouse.y, lr.y - 1 ) );

			Coord3D world;
			if( radarPixelToWorld( &mouse, &world ) )
				radarLookAt( &world );
			break;
		}

		case GWM_RIGHT_DOWN:
		case GWM_LEFT_DOWN:
		{
			s_radarLookDrag = GWM_NONE;

			ICoord2D mouse = windowLocalMouse( window, mData1 );
			Coord3D world;

			//
			// translate mouse position to radar position ... we know that the mouse
			// location given to us here is relative to the HUD window, which is
			// completely drawn with the radar ... so it's just a translation from
			// our window size we're drawing into to the radar cell size
			//
			if( radarPixelToWorld( &mouse, &world ) )
			{

				// a signal armed off the command bar goes down on the map spot a left press is over,
				// and a right press takes it back
				if( TheInGameUI->isSignalArmed() )
				{
					if( msg == GWM_LEFT_DOWN )
						TheInGameUI->placeArmedSignal( world );
					else
						TheInGameUI->disarmSignal();
					break;
				}

				// No drawables, or a press of the look button, is a look at, and leaves an armed key armed
				// the way a pan of the main view does.  The other button with drawables is an order.

				const DrawableList *drawableList = TheInGameUI->getAllSelectedLocalDrawables(); // locally-owned only

				// see if the user wants to move the tactical view
				// Left on the radar looks and right orders, the same division the world has.  Classic
				// divides the world the other way round, and so did the radar in the game as shipped,
				// except under EA's Alternate Mouse Setup, which looked with the left.  An armed key or a
				// command waiting for its target is aimed with the left button in every mouse, as it is in
				// the world, so the right one looks then.
				const Bool leftAims = TheGlobalData->leftButtonOrders() || TheInGameUI->isOrderKeyArmed()
					|| TheInGameUI->getGUICommand() != NULL;
				const UnsignedInt lookButton = leftAims ? GWM_RIGHT_DOWN : GWM_LEFT_DOWN;
				// that look button held on the radar keeps the camera under the cursor, Classic included
				if( drawableList->empty() || msg == lookButton )
				{
					radarLookAt( &world );
					s_radarLookDrag = (msg == GWM_LEFT_DOWN) ? GWM_LEFT_DRAG : GWM_RIGHT_DRAG;
					break;
				}

				// The order the same press on that spot of the ground gives: a special power, an attack
				// move or a guard off an armed key, a rally point, a move, each one queued under shift.
				TheGameClient->evaluateContextCommand( NULL, &world, CommandTranslator::DO_COMMAND );

				// the order spends an armed key, unless shift is keeping it for the next point
				if( TheInGameUI->isOrderKeyArmed() )
					TheInGameUI->spendOrderKey();

			}
			

	break;

		}  // end left down

		// ------------------------------------------------------------------------
		default:
			return MSG_IGNORED;

	}  // end switch( msg )

	return MSG_HANDLED;

}  // end LeftHUDInput

//-------------------------------------------------------------------------------------------------
/** Input procedure for the control bar */
//-------------------------------------------------------------------------------------------------
WindowMsgHandledType ControlBarInput( GameWindow *window, UnsignedInt msg,
																			WindowMsgData mData1, WindowMsgData mData2 )
{

	return MSG_IGNORED;

}  // end ControlBarInput
void ToggleQuitMenu(void);
//-------------------------------------------------------------------------------------------------
/** System callback for the control bar parent */
//-------------------------------------------------------------------------------------------------
WindowMsgHandledType ControlBarSystem( GameWindow *window, UnsignedInt msg, 
																			 WindowMsgData mData1, WindowMsgData mData2 )
{
	static NameKeyType buttonCommunicator = NAMEKEY_INVALID;
	if(TheScriptEngine && TheScriptEngine->isGameEnding())
		return MSG_IGNORED;
	switch( msg ) 
	{
		// --------------------------------------------------------------------------------------------
		case GWM_CREATE:
		{

			// get ids for our children controls
			buttonCommunicator = TheNameKeyGenerator->nameToKey( AsciiString("ControlBar.wnd:PopupCommunicator") );

			break;

		}  // end create

		//---------------------------------------------------------------------------------------------
		case GBM_MOUSE_ENTERING:
		case GBM_MOUSE_LEAVING:
		{
			GameWindow *control = (GameWindow *)mData1;

			TheControlBar->processContextSensitiveButtonTransition( control, (GadgetGameMessage)msg);
			break;
		}

		//---------------------------------------------------------------------------------------------
		case GBM_SELECTED:
		case GBM_SELECTED_RIGHT:
		{
			GameWindow *control = (GameWindow *)mData1;
			static NameKeyType beaconPlacementButtonID = NAMEKEY("ControlBar.wnd:ButtonPlaceBeacon");
			static NameKeyType beaconDeleteButtonID = NAMEKEY("ControlBar.wnd:ButtonDeleteBeacon");
			static NameKeyType beaconClearTextButtonID = NAMEKEY("ControlBar.wnd:ButtonClearBeaconText");
			static NameKeyType beaconGeneralButtonID = NAMEKEY("ControlBar.wnd:ButtonGeneral");
//			static NameKeyType buttonSmallID = NAMEKEY("ControlBar.wnd:ButtonSmall");
//			static NameKeyType buttonMediumID = NAMEKEY("ControlBar.wnd:ButtonMedium");
			static NameKeyType buttonLargeID = NAMEKEY("ControlBar.wnd:ButtonLarge");
			static NameKeyType buttonOptions = NAMEKEY("ControlBar.wnd:ButtonOptions");
			static NameKeyType buttonIdleWorker = NAMEKEY("ControlBar.wnd:ButtonIdleWorker");
			static NameKeyType buttonIdleInfantry = NAMEKEY("ControlBar.wnd:ButtonIdleInfantry");
			static NameKeyType buttonIdleVehicle = NAMEKEY("ControlBar.wnd:ButtonIdleVehicle");
			static NameKeyType buttonIdleAircraft = NAMEKEY("ControlBar.wnd:ButtonIdleAircraft");

			Int controlID = control->winGetWindowId();
			if( controlID == buttonCommunicator )
			{
				ToggleDiplomacy(FALSE);
			}
			else if( controlID == beaconPlacementButtonID && TheGameLogic->isInMultiplayerGame() &&
				ThePlayerList->getLocalPlayer()->isPlayerActive())
			{
				const CommandButton *commandButton = TheControlBar->findCommandButton( "Command_PlaceBeacon" );
				TheInGameUI->setGUICommand( commandButton );
			}
			else if( controlID == beaconDeleteButtonID && TheGameLogic->isInMultiplayerGame() )
			{
				TheMessageStream->appendMessage( GameMessage::MSG_REMOVE_BEACON );
			}
			else if( controlID == beaconClearTextButtonID && TheGameLogic->isInMultiplayerGame() )
			{
				static NameKeyType textID = NAMEKEY("ControlBar.wnd:EditBeaconText");
				GameWindow *win = TheWindowManager->winGetWindowFromId(NULL, textID);
				if (win)
				{
					GadgetTextEntrySetText( win, UnicodeString::TheEmptyString );
				}
			}
			else if( controlID == beaconGeneralButtonID)
			{
				HideQuitMenu( );
				TheControlBar->togglePurchaseScience();
			}
			//else if( controlID == buttonSmallID)
			//			{
			//				TheControlBar->switchControlBarStage( CONTROL_BAR_STAGE_LOW	);
			//			}
			//			else if( controlID == buttonMediumID)
			//			{
			//				TheControlBar->switchControlBarStage( CONTROL_BAR_STAGE_SQUISHED	);
			//			}
			
			else if( controlID == buttonLargeID)
			{
				TheControlBar->toggleControlBarStage();
			}
			else if( controlID == buttonOptions)
			{
				ToggleQuitMenu();
			}
			else if( controlID == buttonIdleWorker)
			{
				HideQuitMenu( );
				Object *worker = TheInGameUI->selectNextIdleWorker();
				if( worker )	// every worker busy: nothing was selected
					TheTacticalView->lookAt( worker->getPosition() );
			}
			else if( controlID == buttonIdleInfantry || controlID == buttonIdleVehicle || controlID == buttonIdleAircraft )
			{
				// the Next Idle Infantry, Vehicle and Aircraft keys' cycle, with the worker button's camera
				HideQuitMenu( );
				KindOfMaskType mustBeSet, mustBeClear;
				InGameUI::idleUnitKinds( controlID == buttonIdleInfantry ? GameMessage::MSG_META_SELECT_NEXT_IDLE_INFANTRY
																 : controlID == buttonIdleVehicle ? GameMessage::MSG_META_SELECT_NEXT_IDLE_VEHICLE
																 : GameMessage::MSG_META_SELECT_NEXT_IDLE_AIRCRAFT, &mustBeSet, &mustBeClear );
				Object *unit = TheInGameUI->selectNextIdleUnit( mustBeSet, mustBeClear );
				if( unit )
					TheTacticalView->lookAt( unit->getPosition() );
			}
			else
			{
				//
				// all buttons from all the context sensitive user interface windows are part of the
				// control bar, send the button processing that way
				//
				TheControlBar->processContextSensitiveButtonClick( control, (GadgetGameMessage)msg );
			}
			break;

		}  // end button selected

		//---------------------------------------------------------------------------------------------
		case GEM_EDIT_DONE:
		{
			GameWindow *control = (GameWindow *)mData1;
			Int controlID = control->winGetWindowId();
			static NameKeyType textID = NAMEKEY("ControlBar.wnd:EditBeaconText");
			if (controlID == textID)
			{
				// set beacon text
				if (TheInGameUI->getSelectCount() == 1)
				{
					GameMessage *msg = TheMessageStream->appendMessage( GameMessage::MSG_SET_BEACON_TEXT );
					UnicodeString newText = GadgetTextEntryGetText( control );
					TheLanguageFilter->filterLine(newText);
					const WideChar * c = newText.str();
					while ( c && *c )
					{
						msg->appendWideCharArgument( *c++ );
					}
					msg->appendWideCharArgument( u'\0' ); // trailing NULL
				}
			}
			break;
		} // end edit done

		//---------------------------------------------------------------------------------------------
		default:
			return MSG_IGNORED;

	}  // end switch( msg )

	return MSG_HANDLED;

}  // end ControlBarSystem

extern void showReplayControls( void );
extern void hideReplayControls( void );
extern void toggleReplayControls( void );

//-------------------------------------------------------------------------------------------------
/** Force the control bar to be shown */
//-------------------------------------------------------------------------------------------------
void ShowControlBar( Bool immediate )
{
	showReplayControls();
	if(TheControlBar)
		TheControlBar->showSpecialPowerShortcut();
	if (TheWindowManager)
	{
		Int id = (Int)TheNameKeyGenerator->nameToKey(AsciiString("ControlBar.wnd:ControlBarParent"));
		GameWindow *window = TheWindowManager->winGetWindowFromId(NULL, id);

		if (window)
		{	
			TheControlBar->switchControlBarStage(CONTROL_BAR_STAGE_DEFAULT);
			// full height: the three panels are drawn over the world, they do not crop it (see InGameUI::init)
			TheTacticalView->setHeight((Int)(TheDisplay->getHeight()));
			if (TheControlBar->m_animateWindowManager && !immediate)
			{
				TheControlBar->m_animateWindowManager->reset();
				//TheControlBar->m_animateWindowManager->registerGameWindow(window, WIN_ANIMATION_SLIDE_BOTTOM_TIMED, TRUE, 1000, 0);
				TheControlBar->m_animateWindowManager->registerGameWindow(window, WIN_ANIMATION_SLIDE_BOTTOM, TRUE, 500, 0);
				TheControlBar->animateSpecialPowerShortcut(TRUE);
			}
			window->winHide(FALSE);
		}

	}  

	// We want to get everything recalced since this is a major state change.
	if(TheControlBar)
		TheControlBar->markUIDirty();

}// void ShowControlBar(void)

//-------------------------------------------------------------------------------------------------
/** Force the control bar to be hidden */
//-------------------------------------------------------------------------------------------------
void HideControlBar( Bool immediate )
{
	hideReplayControls();
	if(TheControlBar)
		TheControlBar->hideSpecialPowerShortcut();
	if (TheWindowManager)
	{
		Int id = (Int)TheNameKeyGenerator->nameToKey(AsciiString("ControlBar.wnd:ControlBarParent"));
		GameWindow *window = TheWindowManager->winGetWindowFromId(NULL, id);

		if (window)
		{
#ifdef SLIDE_LETTERBOX
				TheTacticalView->setHeight((Int)(TheDisplay->getHeight() * 0.80f)); 
#else
				TheTacticalView->setHeight(TheDisplay->getHeight());
#endif
		}
		if (immediate)
		{
			window->winHide(TRUE);
			if(TheControlBar)
				TheControlBar->hideSpecialPowerShortcut();
	
		}
		else
		{
			TheControlBar->m_animateWindowManager->reverseAnimateWindow();
			TheControlBar->animateSpecialPowerShortcut(FALSE);
		}

		//Always get rid of the purchase science screen!
		if( TheControlBar )
		{
			TheControlBar->hidePurchaseScience();
		}
	}  
}//void HideControlBar( void )

//-------------------------------------------------------------------------------------------------
/** Toggle the control bar on or off */
//-------------------------------------------------------------------------------------------------
void ToggleControlBar( Bool immediate )
{
	toggleReplayControls();

	if (TheWindowManager)
	{
		Int id = (Int)TheNameKeyGenerator->nameToKey(AsciiString("ControlBar.wnd:ControlBarParent"));
		GameWindow *window = TheWindowManager->winGetWindowFromId(NULL, id);

		if (window)
		{
			if (window->winIsHidden())
			{
				if(TheControlBar)	
					TheControlBar->showSpecialPowerShortcut();

				// full height: the three panels are drawn over the world, they do not crop it (see InGameUI::init)
				TheTacticalView->setHeight((Int)(TheDisplay->getHeight()));
				window->winHide(!window->winIsHidden());
				TheControlBar->switchControlBarStage(CONTROL_BAR_STAGE_DEFAULT);
				if (TheControlBar->m_animateWindowManager && !immediate)
				{
					TheControlBar->m_animateWindowManager->reset();
					//TheControlBar->m_animateWindowManager->registerGameWindow(window, WIN_ANIMATION_SLIDE_BOTTOM_TIMED, FALSE, 500, 0);
					TheControlBar->m_animateWindowManager->registerGameWindow(window, WIN_ANIMATION_SLIDE_BOTTOM, TRUE, 500, 0);
					TheControlBar->animateSpecialPowerShortcut(TRUE);
				}
			}
			else
			{
				if(TheControlBar)
					TheControlBar->hideSpecialPowerShortcut();
				TheTacticalView->setHeight(TheDisplay->getHeight());
				window->winHide(!window->winIsHidden());
			}
			
		}

	}
}// end void ToggleControlBar( void )

//-------------------------------------------------------------------------------------------------
/** Resize the control bar */
//-------------------------------------------------------------------------------------------------
