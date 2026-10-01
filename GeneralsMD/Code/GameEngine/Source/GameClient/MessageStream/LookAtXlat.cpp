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

////////////////////////////////////////////////////////////////////////////////
//																																						//
//  (c) 2001-2003 Electronic Arts Inc.																				//
//																																						//
////////////////////////////////////////////////////////////////////////////////

// LookAtXlat.cpp
// Translate raw input events into camera movement commands
// Author: Michael S. Booth, April 2001

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "windows.h"

#include "Common/GameType.h"
#include "Common/MessageStream.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/Recorder.h"
#include "Common/StatsCollector.h"
#include "GameLogic/Object.h"
#include "GameLogic/PartitionManager.h"
#include "GameClient/CinemaDirector.h"
#include "GameClient/Display.h"
#include "GameClient/GameText.h"
#include "GameClient/GameWindowManager.h"	// for what the pointer is over, at the screen's edges
#include "GameClient/Mouse.h"
#include "GameClient/Shell.h"
#include "GameClient/GameClient.h"
#include "GameClient/KeyDefs.h"
#include "GameClient/Keyboard.h"		// for the ctrl+wheel building rotation
#include "GameClient/View.h"
#include "GameClient/Drawable.h"
#include "GameClient/LookAtXlat.h"
#include "GameClient/ObserverCamera.h"
#include "GameLogic/Module/UpdateModule.h"
#include "GameLogic/GameLogic.h"

#include "Common/GlobalData.h"			// for camera pitch angle only

LookAtTranslator *TheLookAtTranslator = NULL;

static enum
{
	DIR_UP = 0,
	DIR_DOWN,
	DIR_LEFT,
	DIR_RIGHT
};

static Bool scrollDir[4] = { false, false, false, false };

Int SCROLL_AMT = 100;

// The last pixels of the screen, where the edge scroll has always run at full speed.
static const Int edgeScrollSize = 3;

// One wheel notch with Ctrl held counts as this many.  The band from the closest zoom to the
// farthest is about fifteen notches, so three of these cross it.
static const Real CTRL_WHEEL_ZOOM_NOTCHES = 5.0f;

//-----------------------------------------------------------------------------
Int EdgeScroll_bandForHeight( Int displayHeight )
{
	const Int band = displayHeight * 3 / 100;	// 32 pixels at 1080, 43 at 1440
	return band > edgeScrollSize ? band : edgeScrollSize;
}

//-----------------------------------------------------------------------------
Real EdgeScroll_strength( Int distanceFromEdge, Int band )
{
	if (distanceFromEdge >= band)
		return 0.0f;
	if (distanceFromEdge < edgeScrollSize)
		return 1.0f;
	return (Real)(band - distanceFromEdge) / (Real)(band - edgeScrollSize);
}

//-----------------------------------------------------------------------------
/** The command bar, the minimap and every other window that draws stand on the screen's edges.
	* The same walk pickDrawable makes: a window under the pointer counts unless it and everything
	* it sits in is see-through. */
static Bool isOverWindow( const ICoord2D& pos )
{
	GameWindow *window = NULL;
	if (TheWindowManager)
		window = TheWindowManager->getWindowUnderCursor( pos.x, pos.y );

	for( ; window; window = window->winGetParent() )
		if (!BitTest( window->winGetStatus(), WIN_STATUS_SEE_THRU ))
			return TRUE;

	return FALSE;
}

//-----------------------------------------------------------------------------
/** How hard the screen's edges pull the view with the pointer here: -1..1 on each axis, and zero
	* on both when the pointer is not in the band at all.  Over a window only the last three pixels
	* count, as they always did, so reaching for a button on the command bar neither moves the map
	* nor turns the pointer into a scroll arrow. */
static Coord2D edgeScrollPull( const ICoord2D& pos )
{
	const Int width  = (Int)TheDisplay->getWidth();
	const Int height = (Int)TheDisplay->getHeight();
	const Int band = isOverWindow( pos ) ? edgeScrollSize : EdgeScroll_bandForHeight( height );

	Coord2D pull;
	pull.x = EdgeScroll_strength( width - 1 - pos.x, band ) - EdgeScroll_strength( pos.x, band );
	pull.y = EdgeScroll_strength( height - 1 - pos.y, band ) - EdgeScroll_strength( pos.y, band );
	return pull;
}

static Bool isAtScreenEdge( const ICoord2D& pos )
{
	const Coord2D pull = edgeScrollPull( pos );
	return pull.x != 0.0f || pull.y != 0.0f;
}

static Mouse::MouseCursor prevCursor = Mouse::ARROW;

//-----------------------------------------------------------------------------
void LookAtTranslator::setScrolling(Int x)
{
	if (!TheInGameUI->getInputEnabled())
		return;

	prevCursor = TheMouse->getMouseCursor();
	m_isScrolling = true;
	TheInGameUI->setScrolling( TRUE );
	TheTacticalView->setMouseLock( TRUE );
	m_scrollType = x;
	// A manual pan restores map constraints widened by scripted camera paths.
	if (TheGlobalData->m_useCameraConstraints && TheGlobalData->m_cameraBoundaryMargin > 0)
		TheTacticalView->forceCameraConstraintRecalc();
	if(TheStatsCollector)
		TheStatsCollector->startScrollTime();
}

//-----------------------------------------------------------------------------
void LookAtTranslator::stopScrolling( void )
{
	m_isScrolling = false;
	TheInGameUI->setScrolling( FALSE );
	TheTacticalView->setMouseLock( FALSE );
	TheMouse->setCursor(prevCursor);
	m_scrollType = SCROLL_NONE;
		
	// if we have a stats collectore increment the stats
	if(TheStatsCollector)
		TheStatsCollector->endScrollTime();

}

//-----------------------------------------------------------------------------
LookAtTranslator::LookAtTranslator() :
	m_rightPanArmed(false),
	m_isScrolling(false),
	m_isRotating(false),
	m_freeRotateAngle(0.0f),
	m_isPitching(false),
	m_isChangingFOV(false),
	m_timestamp(0),
	m_lastPlaneID(INVALID_DRAWABLE_ID),
	m_lastMouseMoveFrame(0),
	m_cameraSentFrame(0),
	m_scrollType(SCROLL_NONE)
{
	//Added By Sadullah Nader
	//Initializations misssing and needed
	m_anchor.x = m_anchor.y = 0;
	m_currentPos.x = m_currentPos.y = 0;
	m_originalAnchor.x = m_originalAnchor.y = 0;
	//

	DEBUG_ASSERTCRASH(!TheLookAtTranslator, ("Already have a LookAtTranslator - why do you need two?"));
	TheLookAtTranslator = this;
}

//-----------------------------------------------------------------------------
LookAtTranslator::~LookAtTranslator()
{
	if (TheLookAtTranslator == this)
		TheLookAtTranslator = NULL;
}

const ICoord2D* LookAtTranslator::getScrollAnchor(void)
{
	if (isRightDragPanning())
	{
		return &m_anchor;
	}
	return NULL;
}

Bool LookAtTranslator::hasMouseMovedRecently( void )
{
	if (m_lastMouseMoveFrame > TheGameLogic->getFrame())
		m_lastMouseMoveFrame = 0; // reset for new game

	if (m_lastMouseMoveFrame + LOGICFRAMES_PER_SECOND < TheGameLogic->getFrame())
		return false;

	return true;
}

void LookAtTranslator::setCurrentPos( const ICoord2D& pos )
{
	m_currentPos = pos;
}

/// how often at most a player's camera goes out to the other machines: five times a second, about
/// fifty bytes each, and the observer smooths the steps between
static const UnsignedInt CAMERA_NETWORK_FRAMES = LOGICFRAMES_PER_SECOND / 5;

//-----------------------------------------------------------------------------
/** Is this playing player's camera due to go out over the network?  Only in a match between
	* machines, only from somebody still playing, and only when it moved since the last one went. */
Bool LookAtTranslator::networkCameraDue( const ViewLocation &view )
{
	if( !TheGameLogic->isInMultiplayerGame() || !ThePlayerList->getLocalPlayer()->isPlayerActive() )
		return FALSE;

	const UnsignedInt frame = TheGameLogic->getFrame();
	if( frame >= m_cameraSentFrame && frame < m_cameraSentFrame + CAMERA_NETWORK_FRAMES )
		return FALSE;
	if( view.m_pos.x == m_cameraSent.m_pos.x && view.m_pos.y == m_cameraSent.m_pos.y
			&& view.m_angle == m_cameraSent.m_angle && view.m_pitch == m_cameraSent.m_pitch && view.m_zoom == m_cameraSent.m_zoom )
		return FALSE;

	m_cameraSentFrame = frame;
	m_cameraSent = view;
	return TRUE;
}

//-----------------------------------------------------------------------------
/**
 * The LookAt Translator is responsible for camera movements. It is directly responsible for
 * right mouse button scrolling, and CTRL-<F key> bookmarking. It also responds to certain
 * LOOKAT message on the message stream.
 */
GameMessageDisposition LookAtTranslator::translateGameMessage(const GameMessage *msg)
{
	GameMessageDisposition disp = KEEP_MESSAGE;

	GameMessage::Type t = msg->getType();
	switch (t)
	{
		//-----------------------------------------------------------------------------
		case GameMessage::MSG_RAW_KEY_DOWN:
		case GameMessage::MSG_RAW_KEY_UP:
		{
			// get key and state from args
			UnsignedByte key		= msg->getArgument( 0 )->integer;
			UnsignedByte state	= msg->getArgument( 1 )->integer;
			Bool isPressed = !(BitTest( state, KEY_STATE_UP ));
			
			if (TheShell && TheShell->isShellActive())
				break;

			switch (key)
			{
			case KEY_UP:
				scrollDir[DIR_UP] = isPressed;
				break;
			case KEY_DOWN:
				scrollDir[DIR_DOWN] = isPressed;
				break;
			case KEY_LEFT:
				scrollDir[DIR_LEFT] = isPressed;
				break;
			case KEY_RIGHT:
				scrollDir[DIR_RIGHT] = isPressed;
				break;
			}

			if (TheInGameUI->isSelecting() || (m_isScrolling && m_scrollType != SCROLL_KEY))
				break;

			// see if we need to start/stop scrolling
			Int numDirs = 0;
			for (Int i=0; i<4; ++i)
			{
				if (scrollDir[i])
					numDirs++;
			}

			if (numDirs && !m_isScrolling)
			{
				setScrolling( SCROLL_KEY );
			}
			else if (!numDirs && m_isScrolling)
			{
				stopScrolling();
			}
			break;
		}

		//-----------------------------------------------------------------------------
		// A right click is an order and a right drag pans.  The press only arms the pan: it starts
		// once the cursor has left the mouse's drag tolerance, so a click that wobbles a pixel or two
		// still reaches CommandXlat as an order and never nudges the camera.  The anchor stays where
		// the button went down and the camera runs away from it, faster the further the cursor gets.
		case GameMessage::MSG_RAW_MOUSE_RIGHT_BUTTON_DOWN:
		case GameMessage::MSG_RAW_MOUSE_RIGHT_DOUBLE_CLICK:
		{
			m_lastMouseMoveFrame = TheGameLogic->getFrame();
			m_anchor = msg->getArgument( 0 )->pixel;
			m_rightPanArmed = true;
			break;
		}

		case GameMessage::MSG_RAW_MOUSE_RIGHT_BUTTON_UP:
		{
			m_lastMouseMoveFrame = TheGameLogic->getFrame();
			m_rightPanArmed = false;
			if (isRightDragPanning())
				stopScrolling();
			break;
		}

		//-----------------------------------------------------------------------------
		case GameMessage::MSG_RAW_MOUSE_MIDDLE_BUTTON_DOWN:
		{
			m_lastMouseMoveFrame = TheGameLogic->getFrame();

			m_anchor = msg->getArgument( 0 )->pixel;
			m_originalAnchor = msg->getArgument( 0 )->pixel;
			m_currentPos = msg->getArgument( 0 )->pixel;
			m_timestamp = TheGameClient->getFrame();

			// The middle button turns the camera: drag it sideways and the heading follows.  The pan
			// is on the right button, so no modifier is needed.  A click without a drag puts the
			// camera back, below.
			m_isRotating = true;
			// the drag turns this, and under SnapCameraRotateTo45 the camera stands on whichever
			// eighth it is nearest - so start it where the camera already is.
			m_freeRotateAngle = TheTacticalView->getAngle();
			break;
		}

		//-----------------------------------------------------------------------------
		case GameMessage::MSG_RAW_MOUSE_MIDDLE_BUTTON_UP:
		{
			m_lastMouseMoveFrame = TheGameLogic->getFrame();

			const UnsignedInt CLICK_DURATION = 5;
			const UnsignedInt PIXEL_OFFSET = 5;

			m_isRotating = false;
			Int dx = m_currentPos.x-m_originalAnchor.x;
			if (dx<0) dx = -dx;
			Int dy = m_currentPos.y-m_originalAnchor.y;
			Bool didMove = dx>PIXEL_OFFSET || dy>PIXEL_OFFSET;
			// if middle button is "clicked", reset to "home" orientation
			if (!didMove && TheGameClient->getFrame() - m_timestamp < CLICK_DURATION)
			{
				TheTacticalView->setAngleAndPitchToDefault();
				TheTacticalView->setZoomToDefault();
			}
			// nothing to settle on release: under SnapCameraRotateTo45 the heading was already on an
			// eighth for the whole drag.

			break;
		}

		//-----------------------------------------------------------------------------
		case GameMessage::MSG_RAW_MOUSE_POSITION:
		{
			if (m_currentPos.x != msg->getArgument( 0 )->pixel.x || m_currentPos.y != msg->getArgument( 0 )->pixel.y)
				m_lastMouseMoveFrame = TheGameLogic->getFrame();

			m_currentPos = msg->getArgument( 0 )->pixel;

			if (TheInGameUI->getInputEnabled() == FALSE) {
				// We don't care how we're scrolling, just stop.
				if (m_isScrolling)
					stopScrolling();
				m_rightPanArmed = false;
				break;
			}

			// a right press that has travelled past the drag tolerance is a pan, not an order
			if (m_rightPanArmed
					&& ((UnsignedInt)abs(m_currentPos.x - m_anchor.x) > TheMouse->m_dragTolerance
							|| (UnsignedInt)abs(m_currentPos.y - m_anchor.y) > TheMouse->m_dragTolerance))
			{
				m_rightPanArmed = false;
				if (!TheInGameUI->isSelecting() && !m_isScrolling)
					setScrolling(SCROLL_RMB);
			}

			// retail disables edge scrolling entirely in a window; EdgeScrollInWindowedMode in
			// Options.ini turns it back on for people who play windowed or borderless.  Either way
			// the pointer has to be over the window: the position below is the last one the mouse
			// device was told about, and a pointer that has left a windowed game left through an
			// edge, so believing it would scroll the map for as long as the mouse sat on the desktop.
			// Nor while a watcher's director or player camera drives: the spectator page stands on the
			// top and right edges, and the pointer on its way to it took the camera away (ObserverCamera.h).
			// Nor under -cinema with the interface off: a watched training run is recorded from the
			// camera the control socket's follow holds, and a pointer resting on an edge broke the lock.
			const Bool edgeScrollAllowed = (!TheGlobalData->m_windowed || TheGlobalData->m_edgeScrollInWindowedMode)
																			&& TheMouse->isCursorInWindow() && !TheObserverCamera.isDriving()
																			&& !CinemaDirector_hidesHud();

			if (m_isScrolling)
			{
				if ( m_scrollType == SCROLL_SCREENEDGE && (!edgeScrollAllowed || !isAtScreenEdge(m_currentPos)) )
				{
					stopScrolling();
				}
			}
			else if (edgeScrollAllowed && isAtScreenEdge(m_currentPos))
			{
				setScrolling(SCROLL_SCREENEDGE);
			}

			// rotate the view
			if (m_isRotating)
			{
				const Real FACTOR = 0.01f;

				Real angle = FACTOR * (m_currentPos.x - m_anchor.x);

				if (TheGlobalData->m_snapCameraRotateTo45)
				{
					// discrete heading: the drag turns an angle we keep to ourselves and the camera
					// jumps to the eighth it is nearest, as the mouse crosses each halfway point.
					m_freeRotateAngle += angle;
					TheTacticalView->setAngle( View_snapAngleToEighth( m_freeRotateAngle ) );
				}
				else
				{
					TheTacticalView->setAngle( TheTacticalView->getAngle() + angle );
				}
				m_anchor = msg->getArgument( 0 )->pixel;
			}

			// rotate the view up/down
			if (m_isPitching)
			{
				const Real FACTOR = 0.01f;

				Real angle = FACTOR * (m_currentPos.y - m_anchor.y);

				// Dragging down tips the camera down.  The sign used to be the other way round.
				TheTacticalView->setPitch( TheTacticalView->getPitch() - angle );
				m_anchor = msg->getArgument( 0 )->pixel;
			}

#if defined(_DEBUG) || defined(_INTERNAL)
			// adjust the field of view
			if (m_isChangingFOV)
			{
				const Real FACTOR = 0.01f;

				Real angle = FACTOR * (m_currentPos.y - m_anchor.y);

				TheTacticalView->setFieldOfView( TheTacticalView->getFieldOfView() + angle );
				m_anchor = msg->getArgument( 0 )->pixel;
			}
#endif
			break;
		}

		//-----------------------------------------------------------------------------
		case GameMessage::MSG_RAW_MOUSE_WHEEL:
		{
			m_lastMouseMoveFrame = TheGameLogic->getFrame();

			// Notches, and a touchpad sends fractions of one.
			Real spin = msg->getArgument( 1 )->real;

			//
			// Ctrl+wheel turns the structure on the cursor by 45 degrees a notch instead of zooming.
			// The wheel is otherwise wasted while placing, and the drag-to-aim interface it replaces
			// cannot hit an exact eighth of a turn.  Eaten so the same notch does not also zoom.
			//
			// Whole notches only: half a touchpad swipe is not a 45 degree turn.
			const Int rotateSteps = (Int)spin;
			if (TheKeyboard->isCtrl() && rotateSteps != 0 &&
					TheInGameUI->rotatePendingPlacement( rotateSteps ))
				return DESTROY_MESSAGE;

			// ZoomToCursor: the view holds the ground under the cursor while the zoom eases in.  It does
			// it inside its own update, between the zoom moving and the frame being drawn; held from
			// here, the correction always landed a frame late.
			if (TheGlobalData->m_zoomToCursor && TheInGameUI->getInputEnabled())
				TheTacticalView->anchorZoomAt( &msg->getArgument( 0 )->pixel );

			// Ctrl+wheel with nothing to place crosses the zoom range in three notches instead of
			// fifteen.  Ctrl and not Shift: Shift is what lays a row of structures, and the wheel
			// under it sets that row's gap (PlaceEventTranslator).
			if (TheKeyboard->isCtrl() && TheInGameUI->getPendingPlaceType() == NULL)
				spin *= CTRL_WHEEL_ZOOM_NOTCHES;

			if (spin > 0.0f)
				TheTacticalView->zoomIn( spin );
			else if (spin < 0.0f)
				TheTacticalView->zoomOut( -spin );

			break;	// without this the case fell into MSG_META_OPTIONS below and every wheel
					// notch called stopScrolling(), killing zoom-while-panning and leaving
					// m_isScrolling/m_scrollType torn.
		}


		//-----------------------------------------------------------------------------
		case GameMessage::MSG_META_OPTIONS:
		{
			// stop the scrolling
			stopScrolling();
			// let the message drop through, cause we need to process this message for 
			// selection as well.
			break;
		}

		//-----------------------------------------------------------------------------
		case GameMessage::MSG_FRAME_TICK:
		{
			Coord2D offset = {0, 0};

			// If we've been forced to stop scrolling (script action?) then stop
			if (m_isScrolling && !TheInGameUI->isScrolling())
			{
				TheInGameUI->setScrollAmount(offset);
				stopScrolling();
			}
			else
			// scroll the view
			if (m_isScrolling)
			{
				switch (m_scrollType)
				{
				case SCROLL_RMB:
					{
						// The anchor stays where the button went down and the camera runs away from it,
						// faster the further the cursor gets.  This is the retail right-drag scroll, and it
						// is what the hand expects; a one-to-one drag of the world is not the same
						// gesture and reads as sluggish at these scroll factors.
						if (TheInGameUI->shouldMoveScrollAnchor())
						{
							Int maxX = TheDisplay->getWidth()/2;
							Int maxY = TheDisplay->getHeight()/2;

							if (m_currentPos.x + maxX < m_anchor.x)
								m_anchor.x = m_currentPos.x + maxX;
							else if (m_currentPos.x - maxX > m_anchor.x)
								m_anchor.x = m_currentPos.x - maxX;

							if (m_currentPos.y + maxY < m_anchor.y)
								m_anchor.y = m_currentPos.y + maxY;
							else if (m_currentPos.y - maxY > m_anchor.y)
								m_anchor.y = m_currentPos.y - maxY;
						}

						offset.x = TheGlobalData->m_horizontalScrollSpeedFactor * (m_currentPos.x - m_anchor.x);
						offset.y = TheGlobalData->m_verticalScrollSpeedFactor * (m_currentPos.y - m_anchor.y);
						Coord2D vec;
						vec.x = offset.x;
						vec.y = offset.y;
						vec.normalize();
						// Add in the window scroll amount as the minimum.
						offset.x += TheGlobalData->m_horizontalScrollSpeedFactor * vec.x * sqr(TheGlobalData->m_keyboardScrollFactor);
						offset.y += TheGlobalData->m_verticalScrollSpeedFactor * vec.y * sqr(TheGlobalData->m_keyboardScrollFactor);
					}
					break;
				case SCROLL_KEY:
					{
						if (scrollDir[DIR_UP])
						{
							offset.y -= TheGlobalData->m_verticalScrollSpeedFactor * SCROLL_AMT * TheGlobalData->m_keyboardScrollFactor;
						}
						if (scrollDir[DIR_DOWN])
						{
							offset.y += TheGlobalData->m_verticalScrollSpeedFactor * SCROLL_AMT * TheGlobalData->m_keyboardScrollFactor;
						}
						if (scrollDir[DIR_LEFT])
						{
							offset.x -= TheGlobalData->m_horizontalScrollSpeedFactor * SCROLL_AMT * TheGlobalData->m_keyboardScrollFactor;
						}
						if (scrollDir[DIR_RIGHT])
						{
							offset.x += TheGlobalData->m_horizontalScrollSpeedFactor * SCROLL_AMT * TheGlobalData->m_keyboardScrollFactor;
						}
					}
					break;
				case SCROLL_SCREENEDGE:
					{
						// The speed the last three pixels always gave, scaled by how deep into the band
						// the pointer is: barely moving where it enters, all of it at the edge itself.
						const Coord2D pull = edgeScrollPull( m_currentPos );
						offset.x = TheGlobalData->m_horizontalScrollSpeedFactor * SCROLL_AMT * TheGlobalData->m_keyboardScrollFactor * pull.x;
						offset.y = TheGlobalData->m_verticalScrollSpeedFactor * SCROLL_AMT * TheGlobalData->m_keyboardScrollFactor * pull.y;
					}
					break;
				}

				TheInGameUI->setScrollAmount(offset);
			}
			else	//not scrolling so reset amount
				TheInGameUI->setScrollAmount(offset);

			// Advance the pan clock even while stationary, so restarting does not include idle time.
			TheTacticalView->scrollBy( &offset );

			// A playing player's camera goes to the other machines too, so an observer can watch his
			// screen: every CAMERA_NETWORK_FRAMES logic frames at most and only when it moved, where
			// a game on one machine records it every frame as EA did.
			ViewLocation currentView;
			TheTacticalView->getLocation(&currentView);
			const Bool recordCamera = TheGlobalData->m_saveCameraInReplay && (TheGameLogic->isInSinglePlayerGame() || TheGameLogic->isInSkirmishGame());
			if (recordCamera || networkCameraDue(currentView))
			{
				GameMessage *msg = TheMessageStream->appendMessage( GameMessage::MSG_SET_REPLAY_CAMERA );
				msg->appendLocationArgument( currentView.m_pos );
				msg->appendRealArgument( currentView.m_angle );
				msg->appendRealArgument( currentView.m_pitch );
				msg->appendRealArgument( currentView.m_zoom );
				msg->appendIntegerArgument( (Int)TheMouse->getMouseCursor() );
				msg->appendPixelArgument( m_currentPos );
			}
			break;
		}

		// ------------------------------------------------------------------------
#if defined(_DEBUG) || defined(_INTERNAL)
		case GameMessage::MSG_META_DEMO_BEGIN_ADJUST_PITCH:
		{
			DEBUG_ASSERTCRASH(!m_isPitching, ("hmm, mismatched m_isPitching"));
			m_isPitching = true;
			// Anchor where the drag starts, or the first frame measures against wherever the last
			// rotate or scroll left the anchor and the camera snaps.
			m_anchor = m_currentPos;
			disp = DESTROY_MESSAGE;
			break;
		}
#endif // #if defined(_DEBUG) || defined(_INTERNAL)

		// ------------------------------------------------------------------------
#if defined(_DEBUG) || defined(_INTERNAL)
		case GameMessage::MSG_META_DEMO_END_ADJUST_PITCH:
		{
			DEBUG_ASSERTCRASH(m_isPitching, ("hmm, mismatched m_isPitching"));
			m_isPitching = false;
			disp = DESTROY_MESSAGE;
			break;
		}
#endif // #if defined(_DEBUG) || defined(_INTERNAL)

		// ------------------------------------------------------------------------
#if defined(_DEBUG) || defined(_INTERNAL)
		case GameMessage::MSG_META_DEMO_DESHROUD:
		{
			ThePartitionManager->revealMapForPlayerPermanently( ThePlayerList->getLocalPlayer()->getPlayerIndex() );
			break;
		}
#endif // #if defined(_DEBUG) || defined(_INTERNAL)

		// ------------------------------------------------------------------------
#if defined(_ALLOW_DEBUG_CHEATS_IN_RELEASE)
		case GameMessage::MSG_CHEAT_DESHROUD: 
		{
			if (!TheGameLogic->isInMultiplayerGame())
			{
				ThePartitionManager->revealMapForPlayerPermanently( ThePlayerList->getLocalPlayer()->getPlayerIndex() );
			}
			break;
		}
#endif // #if defined(_ALLOW_DEBUG_CHEATS_IN_RELEASE)

		// ------------------------------------------------------------------------
#if defined(_DEBUG) || defined(_INTERNAL)
		case GameMessage::MSG_META_DEMO_ENSHROUD:
		{
			// Need to first undo the permanent Look laid down by DEMO_DESHROUD, then blast a shroud dollop.
			ThePartitionManager->undoRevealMapForPlayerPermanently( ThePlayerList->getLocalPlayer()->getPlayerIndex() );
			ThePartitionManager->shroudMapForPlayer( ThePlayerList->getLocalPlayer()->getPlayerIndex() );
			break;
		}
#endif // #if defined(_DEBUG) || defined(_INTERNAL)

		// ------------------------------------------------------------------------
#if defined(_DEBUG) || defined(_INTERNAL)
		case GameMessage::MSG_META_DEMO_BEGIN_ADJUST_FOV:
		{
			//DEBUG_ASSERTCRASH(!m_isChangingFOV, ("hmm, mismatched m_isChangingFOV"));
			m_isChangingFOV = true;
			m_anchor = m_currentPos;
			break;
		}
#endif // #if defined(_DEBUG) || defined(_INTERNAL)

		// ------------------------------------------------------------------------
#if defined(_DEBUG) || defined(_INTERNAL)
		case GameMessage::MSG_META_DEMO_END_ADJUST_FOV:
		{
		//	DEBUG_ASSERTCRASH(m_isChangingFOV, ("hmm, mismatched m_isChangingFOV"));
			m_isChangingFOV = false;
			break;
		}
#endif // #if defined(_DEBUG) || defined(_INTERNAL)

		//-----------------------------------------------------------------------------------------
		case GameMessage::MSG_META_SAVE_VIEW1:
		case GameMessage::MSG_META_SAVE_VIEW2:
		case GameMessage::MSG_META_SAVE_VIEW3:
		case GameMessage::MSG_META_SAVE_VIEW4:
		case GameMessage::MSG_META_SAVE_VIEW5:
		case GameMessage::MSG_META_SAVE_VIEW6:
		case GameMessage::MSG_META_SAVE_VIEW7:
		case GameMessage::MSG_META_SAVE_VIEW8:
		{
			Int slot = t - GameMessage::MSG_META_SAVE_VIEW1 + 1;
			if ( slot > 0 && slot <= MAX_VIEW_LOCS )
			{
				TheTacticalView->getLocation( &m_viewLocation[slot-1] );
				UnicodeString msg;
				msg.format( TheGameText->fetch( "GUI:BookmarkXSet" ), slot );
				TheInGameUI->message( msg );
			}
			disp = DESTROY_MESSAGE;
			break;
		}

		//-----------------------------------------------------------------------------------------
		case GameMessage::MSG_META_VIEW_VIEW1:
		case GameMessage::MSG_META_VIEW_VIEW2:
		case GameMessage::MSG_META_VIEW_VIEW3:
		case GameMessage::MSG_META_VIEW_VIEW4:
		case GameMessage::MSG_META_VIEW_VIEW5:
		case GameMessage::MSG_META_VIEW_VIEW6:
		case GameMessage::MSG_META_VIEW_VIEW7:
		case GameMessage::MSG_META_VIEW_VIEW8:
		{
			Int slot = t - GameMessage::MSG_META_VIEW_VIEW1 + 1;
			if ( slot > 0 && slot <= MAX_VIEW_LOCS )
			{
				TheTacticalView->setLocation( &m_viewLocation[slot-1] );
			}
			disp = DESTROY_MESSAGE;
			break;
		}

		//-----------------------------------------------------------------------------
#if defined(_DEBUG) || defined(_INTERNAL)
		case GameMessage::MSG_META_DEMO_LOCK_CAMERA_TO_PLANES:
		{
			Drawable *first = NULL;

			if (m_lastPlaneID)
				first = TheGameClient->findDrawableByID( m_lastPlaneID );

			if (first == NULL)
				first = TheGameClient->firstDrawable();

			if (first)
			{
				Drawable *d = first;
				Bool done = false;

				while(!done)
				{
					// get next Drawable, wrapping around to head of list if necessary
					d = d->getNextDrawable();
					if (d == NULL)
						d = TheGameClient->firstDrawable();

					// if we've found an airborne object, lock onto it
// "isAboveTerrain" only indicates that we are currently in the air, but that
// could be the case if we are a buggy jumping a hill, or a unit being paradropped.
// the right thing would be to look at the locomotors.
// so this isn't really right, but will suffice for demo purposes.
					if (d->getObject() && d->getObject()->isAboveTerrain() )
					{
						Bool doLock = true;

						// but don't lock onto projectiles
						ProjectileUpdateInterface* pui = NULL;
						for (BehaviorModule** u = d->getObject()->getBehaviorModules(); *u; ++u)
						{
							if ((pui = (*u)->getProjectileUpdateInterface()) != NULL)
							{
								doLock = false;
								break;
							}
						}

						if (doLock)
						{
							TheTacticalView->setCameraLock( d->getObject()->getID() );
							m_lastPlaneID = d->getID();
							done = true;
							break;
						}
					} // if airborne found

					// if we're back to the first, quit
					if (d == first)
						break;
				} // while
			}	// end plane lock

			disp = DESTROY_MESSAGE;
			break;
		}
#endif // #if defined(_DEBUG) || defined(_INTERNAL)

	}  // end switch

	return disp;

}  // end LookAtTranslator

void LookAtTranslator::resetModes()
{
	//
	// A key or a mouse button held at the moment the window loses focus never sends its release:
	// alt-tab away with an arrow key down and the view keeps sliding that way when you come back,
	// until that key is pressed and let go once more.  A drag that ends outside the window is the
	// same story with the mouse.  So this has to put back everything a scroll turned on - the
	// direction flags the keyboard scroll reads, the mouse lock, the cursor - and not just clear
	// the flags that say a scroll is in progress.
	//
	for( Int i = 0; i < 4; ++i )
		scrollDir[i] = false;

	if( m_isScrolling && TheInGameUI && TheTacticalView && TheMouse )
		stopScrolling();

	m_isScrolling = FALSE;
	m_isRotating = FALSE;
	m_freeRotateAngle = 0.0f;
	m_isPitching = FALSE;
	m_isChangingFOV = FALSE;
	m_scrollType = SCROLL_NONE;
}