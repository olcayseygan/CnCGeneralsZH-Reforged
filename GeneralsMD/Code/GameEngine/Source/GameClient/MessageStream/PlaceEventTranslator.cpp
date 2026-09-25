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

// FILE: PlaceEventTranslator.cpp ///////////////////////////////////////////////////////////
// Author: Steven Johnson, Dec 2001

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "GameClient/Keyboard.h"

#include "Common/BuildAssistant.h"
#include "Common/GameAudio.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/SpecialPower.h"
#include "Common/ThingTemplate.h"

#include "GameClient/CommandXlat.h"
#include "GameClient/ControlBar.h"
#include "GameClient/Drawable.h"
#include "GameClient/Eva.h"
#include "GameClient/Mouse.h"
#include "GameClient/PlaceEventTranslator.h"

#include "GameLogic/GameLogic.h"
#include "GameLogic/Object.h"

#include "GameLogic/Module/ProductionUpdate.h"


#ifdef _INTERNAL
// for occasional debugging...
//#pragma optimize("", off)
//#pragma MESSAGE("************************************** WARNING, optimization disabled for debugging purposes")
#endif

//-------------------------------------------------------------------------------------------------
/** How far from the anchor the cursor has to travel before the drag counts as aiming the structure
	* rather than as a plain click, in pixels. */
static const Int PLACEMENT_DRAG_THRESHOLD_DIST = 5;

//-------------------------------------------------------------------------------------------------
/** How far the cursor is from the placement anchor right now, in pixels.  This reads the mouse
	* rather than the pixel carried by the message on purpose: Mouse::createStreamMessages appends
	* MSG_RAW_MOUSE_POSITION before it processes this frame's events, so that pixel is already one
	* batch of movement old, and the structure would be aimed at where the cursor had been rather than
	* where it was let go. */
//-------------------------------------------------------------------------------------------------
static Bool getPlacementDrag( ICoord2D *start, ICoord2D *end )
{
	ICoord2D mouse = TheMouse->getMouseStatus()->pos;

	TheInGameUI->getPlacementPoints( start, NULL );

	Int x = mouse.x - start->x;
	Int y = mouse.y - start->y;

	if( ( x * x ) + ( y * y ) < PLACEMENT_DRAG_THRESHOLD_DIST * PLACEMENT_DRAG_THRESHOLD_DIST )
	{
		// no drag: there is one point, not two
		*end = *start;
		return FALSE;
	}

	*end = mouse;
	return TRUE;

}  // end getPlacementDrag

//-------------------------------------------------------------------------------------------------
/** The object that is to do the building: the one the placement was started from, while it lives. */
//-------------------------------------------------------------------------------------------------
static Object *resolvePlacementBuilder( void )
{
	Object *builder = TheGameLogic->findObjectByID( TheInGameUI->getPendingPlaceSourceObjectID() );
	if( builder != NULL && !builder->isEffectivelyDead() )
		return builder;

	return NULL;
}

//-------------------------------------------------------------------------------------------------
PlaceEventTranslator::PlaceEventTranslator() : m_frameOfUpButton(-1), m_stripClickTaken(FALSE), m_stripRightClickTaken(FALSE)
{
}

//-------------------------------------------------------------------------------------------------
PlaceEventTranslator::~PlaceEventTranslator()
{
}

//-------------------------------------------------------------------------------------------------
/** Translator to process raw input messages into the "place something" message(s) */
//-------------------------------------------------------------------------------------------------
GameMessageDisposition PlaceEventTranslator::translateGameMessage(const GameMessage *msg)
{
	GameMessageDisposition disp = KEEP_MESSAGE;

	// MetaEvent runs before us and inserts a logical click after the raw release.
	// Consume both: otherwise cancelling on the strip can also issue a world order.
	if( m_stripClickTaken && (msg->getType() == GameMessage::MSG_MOUSE_LEFT_CLICK ||
			msg->getType() == GameMessage::MSG_MOUSE_LEFT_DOUBLE_CLICK) )
	{
		m_stripClickTaken = FALSE;
		return DESTROY_MESSAGE;
	}
	if( m_stripClickTaken && (msg->getType() == GameMessage::MSG_RAW_MOUSE_LEFT_BUTTON_UP ||
			msg->getType() == GameMessage::MSG_RAW_MOUSE_LEFT_DRAG) )
		return DESTROY_MESSAGE;
	if( m_stripRightClickTaken )
	{
		if( msg->getType() == GameMessage::MSG_MOUSE_RIGHT_CLICK ||
				msg->getType() == GameMessage::MSG_MOUSE_RIGHT_DOUBLE_CLICK )
		{
			m_stripRightClickTaken = FALSE;
			return DESTROY_MESSAGE;
		}
		if( msg->getType() == GameMessage::MSG_RAW_MOUSE_RIGHT_BUTTON_UP ||
				msg->getType() == GameMessage::MSG_RAW_MOUSE_RIGHT_DRAG )
			return DESTROY_MESSAGE;
	}

	switch(msg->getType())
	{
		case GameMessage::MSG_RAW_MOUSE_RIGHT_BUTTON_DOWN:
		case GameMessage::MSG_RAW_MOUSE_RIGHT_DOUBLE_CLICK:
		{
			m_stripRightClickTaken = FALSE;
			if( TheInGameUI->handleProductionStripClick( &msg->getArgument(0)->pixel, TRUE ) )
			{
				m_stripRightClickTaken = TRUE;
				return DESTROY_MESSAGE;
			}
			break;
		}

		//---------------------------------------------------------------------------------------------
		//
		// Windows sends the second press of a quick pair as WM_LBUTTONDBLCLK, not WM_LBUTTONDOWN,
		// so it reaches the stream as a double click (see Mouse::createStreamMessages).  Placing a
		// row of structures with shift held is exactly that: a fast series of clicks.  Without this
		// label the second one set no placement anchor, the click that followed found none and was
		// left for the selection translator, and a run of buildings ended with the placement gone
		// and whatever was under the cursor selected instead.
		//
		case GameMessage::MSG_RAW_MOUSE_LEFT_BUTTON_DOWN:
		case GameMessage::MSG_RAW_MOUSE_LEFT_DOUBLE_CLICK:
		{
			m_stripClickTaken = FALSE;
			//
			// The global production strip lies over the world rather than inside a window, so
			// nothing above us has claimed a click on it. Take it here, before placement and long
			// before selection turns it into a move order. Right-click (or Ctrl-click) cancels one, a plain click
			// takes the camera to the building it is queued on.
			//
			if( TheInGameUI->handleProductionStripClick( &msg->getArgument(0)->pixel,
																									 TheKeyboard && TheKeyboard->isCtrl() ) )
			{
				m_stripClickTaken = TRUE;
				return DESTROY_MESSAGE;
			}

			// if we're in a building placement mode, do the place and send to all players
			const ThingTemplate *build = TheInGameUI->getPendingPlaceType();
			if( build && TheInGameUI->isPlacementAnchored() == FALSE )
			{
				ICoord2D mouse = msg->getArgument(0)->pixel;
				Coord3D world;

				// translate mouse position to world position
				TheTacticalView->screenToTerrain( &mouse, &world );

				//
				// placing things causes a dozer to go over and build it ... get the dozer in question
				// from the in game UI
				//
				Object *builderObject = resolvePlacementBuilder();

				// if there is no builder left at all, cancel this whole placement process
				if( builderObject == NULL )
				{

					TheInGameUI->placeBuildAvailable( NULL, NULL );
					break;

				}  // end if

				// set this location as the placement anchor
				TheInGameUI->setPlacementStart( &mouse );	

/*
//
// This block of code checks for valid placement on a down mouse click, but since we can
// rotate a building into a valid location, this check prevents us from placing things
// down in some legal locations
//
				// get the type of thing we want to build
				const ThingTemplate *whatToBuild = TheInGameUI->getPendingPlaceType();

				//
				// if the spot at which they choose to place this thing is illegal we won't start
				// the placement anchor, instead we play a "can't do that" sound
				//
				LegalBuildCode lbc;
				lbc = TheBuildAssistant->isLocationLegalToBuild( &world,
																												 whatToBuild, 
																												 TheInGameUI->getPlacementAngle(),
																												 BuildAssistant::USE_QUICK_PATHFIND |
																												 BuildAssistant::TERRAIN_RESTRICTIONS | 
																												 BuildAssistant::CLEAR_PATH |
																												 BuildAssistant::NO_OBJECT_OVERLAP, 
																												 builderObject );
				if( lbc != LBC_OK )
				{
					static const Sound *noCanDoSound = TheAudio->Sounds->getSound( "NoCanDoSound" );

					// play a can't do that sound
					TheAudio->Sounds->playSound( noCanDoSound );

					// display a message to the user as to why you can't build there
					TheInGameUI->displayCantBuildMessage( lbc );

				}  // end if
				else
				{

					// start placement anchor
					TheInGameUI->setPlacementStart(&mouse);	

				}  // end else
*/
															
				// used the input
				disp = DESTROY_MESSAGE;

			}  
			break;
		}  

		//---------------------------------------------------------------------------------------------
		case GameMessage::MSG_MOUSE_LEFT_DOUBLE_CLICK:
		case GameMessage::MSG_MOUSE_LEFT_CLICK:
		{
			// if we're in a building placement mode, do the place and send to all players
			const ThingTemplate *build = TheInGameUI->getPendingPlaceType();

			// ... and also remove any radius cursor that is active.
			// (srj sez: not sure if this is always necessary... more of a failsafe to make it go away.)
			TheInGameUI->setRadiusCursorNone();

			if (build && TheInGameUI->isPlacementAnchored())
			{
				GameMessage *placeMsg;
//				Player *player = ThePlayerList->getLocalPlayer();
				Coord3D world;
				Real angle;
				ICoord2D anchorStart, anchorEnd;
				Bool isLineBuild = TheBuildAssistant->isLineBuildTemplate( build );

				// get start point from the anchor arrow used to place and select angles
				TheInGameUI->getPlacementPoints( &anchorStart, &anchorEnd );

				//
				// The structure faces wherever the player dragged to.  Take that from where the
				// cursor is at this instant rather than from the heading the ghost happens to be
				// wearing: the ghost is turned once a frame in InGameUI::update, which runs before
				// the message stream is propagated and off an anchor end that is itself a frame
				// behind, so a quick drag-and-release used to build the structure facing two frames
				// of mouse travel back.  A press that never left the anchor is not aiming at
				// anything, so that one keeps the heading already on the ghost, and neither does a
				// shift-drag, which lays a row (see InGameUI::placesRow).
				//
				angle = TheInGameUI->getPlacementAngle();
				const Bool dragged = getPlacementDrag( &anchorStart, &anchorEnd );
				const Bool row = dragged && TheInGameUI->placesRow();
				if( dragged && !row )
				{
					angle = TheInGameUI->computePlacementAngle( &anchorStart, &anchorEnd );

					// and the next one goes down facing the same way, as with the wheel
					TheInGameUI->setPlacementAngle( angle );
				}

				// translate the screen position of start to world target location
				TheTacticalView->screenToTerrain( &anchorStart, &world );

				// and put it on the build grid, exactly as the ghost the player was looking at
				TheInGameUI->snapPlacementToGrid( &world, build, angle );

				// get the source object ID of the thing that is "building" the object 
				ObjectID builderID = INVALID_ID;
				Object *builderObj = resolvePlacementBuilder();
				if( builderObj )
					builderID = builderObj->getID();

				//Kris: September 27, 2002
				//Make sure we have enough CASH to build it! It's possible that between the
				//time we initiated it and the time we confirm it, a hacker has stolen some of
				//our cash!
				CanMakeType cmt = TheBuildAssistant->canMakeUnit( builderObj, build );
				if( cmt != CANMAKE_OK )
				{
					//
					// Whatever the reason, the click was spent on the attempt: unhook the anchor and
					// eat the message. It used to just break out - the placement stayed anchored to
					// the spot that had been clicked, so the ghost sat there stuck to the map with no
					// way to place or aim it, and the click itself fell through to the world and
					// became a move order on whatever was selected. Running out of money halfway
					// through a shift-held row of structures hit this every time.
					//
					if (cmt == CANMAKE_NO_MONEY)
					{
						TheEva->setShouldPlay(EVA_InsufficientFunds);
						TheInGameUI->message( "GUI:NotEnoughMoneyToBuild" );
					} 
					else if (cmt == CANMAKE_QUEUE_FULL)
					{
						TheInGameUI->message( "GUI:ProductionQueueFull" );
					}
					else if (cmt == CANMAKE_PARKING_PLACES_FULL)
					{
						TheInGameUI->message( "GUI:ParkingPlacesFull" );
					}
					else if( cmt == CANMAKE_MAXED_OUT_FOR_PLAYER )
					{
						TheInGameUI->message( "GUI:UnitMaxedOut" );
					} 
					else
					{
						// nothing to say about it, so drop out of placement mode altogether
						TheInGameUI->placeBuildAvailable( NULL, NULL );
					}

					// the ghost keeps following the cursor for the cases that may pass (the money
					// comes in, the queue drains), it just is not nailed to the map any more
					TheInGameUI->setPlacementStart( NULL );
					disp = DESTROY_MESSAGE;
					m_frameOfUpButton = TheGameLogic->getFrame();
					break;
				} 

				// check to see if this is a legal location to build something at
				// what the ghost was asking all along, plus the one question it does not ask: the
				// placement cursor ignores stealthed units, committing to the spot does not
				const UnsignedInt placeOptions = InGameUI::placementCheckOptions() |
																				 BuildAssistant::FAIL_STEALTHED_WITHOUT_FEEDBACK;
				LegalBuildCode lbc;
				std::vector<Coord3D> placements;
				if( row )
				{
					//
					// A shift-dragged row is one order per structure, each asked on its own: a piece
					// that lands on a rock is left out and the rest still go up.  The logic hands each
					// order to the idle selected builder nearest it, and once they are all busy the rest
					// stand at 0% for whichever comes free first (BuildAssistant::buildObjectNow).
					//
					Coord3D worldEnd;
					TheTacticalView->screenToTerrain( &anchorEnd, &worldEnd );
					TheInGameUI->snapPlacementToGrid( &worldEnd, build, angle );

					std::vector<Coord3D> pieces;
					TheInGameUI->computePlacementRow( build, angle, &world, &worldEnd, &pieces );

					lbc = LBC_OK;
					for( size_t i = 0; i < pieces.size(); i++ )
					{
						LegalBuildCode piece = TheBuildAssistant->isLocationLegalToBuild( &pieces[ i ], build, angle,
																																						 placeOptions, builderObj,
																																						 NULL );
						if( piece == LBC_OK && TheInGameUI->overlapsPendingPlacement( &pieces[ i ], build, angle ) )
							piece = LBC_OBJECTS_IN_THE_WAY;

						if( piece == LBC_OK )
							placements.push_back( pieces[ i ] );
						else
							lbc = piece;
					}

					// the row goes ahead on any piece that can; the reason is only for a row with none
					if( !placements.empty() )
						lbc = LBC_OK;
				}
				else
				{
					lbc = TheBuildAssistant->isLocationLegalToBuild( &world, build, angle, placeOptions,
																													 builderObj, NULL );

					// plus the ground your own last click already spent - see recordPendingPlacement
					if( lbc == LBC_OK && TheInGameUI->overlapsPendingPlacement( &world, build, angle ) )
						lbc = LBC_OBJECTS_IN_THE_WAY;

					//
					// NudgeBuildPlacement: blocked where they clicked, so build at the nearest spot that
					// is not - the same search the ghost was already showing them (InGameUI::update ->
					// handleBuildPlacements), so the structure goes down where they were looking.  The
					// verdict is taken again at the new spot rather than assumed: this check is the
					// stricter one, it fails on a stealthed unit the ghost is told to ignore.
					//
					if( lbc != LBC_OK && lbc != LBC_SHROUD &&
							TheInGameUI->nudgePlacementToLegal( &world, build, angle, builderObj ) )
					{
						lbc = TheBuildAssistant->isLocationLegalToBuild( &world, build, angle, placeOptions,
																														 builderObj, NULL );
					}
					placements.push_back( world );
				}
				if( lbc == LBC_OK )
				{
					//Are we building this structure via the special power system? (special case for sneak attack)
					if( builderObj )
					{
						ProductionUpdateInterface *puInterface = builderObj->getProductionUpdateInterface();
						if( puInterface )
						{
							const CommandButton *commandButton = puInterface->getSpecialPowerConstructionCommandButton();
							if( commandButton )
							{
								//If we get this far, then we aren't going to really build the object using the production update
								//interface. Instead, we're going to trigger the special power to create it magically without a 
								//dozer/worker.
								placeMsg = TheMessageStream->appendMessage( GameMessage::MSG_DO_SPECIAL_POWER_AT_LOCATION );
								placeMsg->appendIntegerArgument( commandButton->getSpecialPowerTemplate()->getID() ); //The ID of the special power template.
								placeMsg->appendLocationArgument( world ); //Position of special to be fired.
								placeMsg->appendRealArgument( angle ); //Angle of special to be fired.
								placeMsg->appendObjectIDArgument( INVALID_ID ); //There is no object in the way.
								placeMsg->appendIntegerArgument( commandButton->getOptions() ); //Command button options.
								placeMsg->appendObjectIDArgument( builderObj->getID() ); //The source object responsible for firing the special.
								
								// get out of pending placement mode, this will also clear the arrow anchor status
								TheInGameUI->placeBuildAvailable( NULL, NULL );

								// used the input
								disp = DESTROY_MESSAGE;
								m_frameOfUpButton = TheGameLogic->getFrame();
								break;
							}
						}
					}

					for( size_t i = 0; i < placements.size(); i++ )
					{
						// create the right kind of message
						if( isLineBuild )
							placeMsg = TheMessageStream->appendMessage( GameMessage::MSG_DOZER_CONSTRUCT_LINE );
						else
							placeMsg = TheMessageStream->appendMessage( GameMessage::MSG_DOZER_CONSTRUCT );

						placeMsg->appendIntegerArgument(build->getTemplateID());
						placeMsg->appendLocationArgument(placements[ i ]);
						placeMsg->appendRealArgument(angle);
						if( isLineBuild )
						{
							Coord3D worldEnd;

							TheTacticalView->screenToTerrain( &anchorEnd, &worldEnd );
							TheInGameUI->snapPlacementToGrid( &worldEnd, build, angle );
							placeMsg->appendLocationArgument( worldEnd );

						}  // end if

						//
						// The order is on its way; the structure will not be on the ground for another
						// few frames, and on a network game that is however long the link takes.  Hold on
						// to the spot so the next click in a shift-held run does not put a second
						// structure on top of this one.
						//
						TheInGameUI->recordPendingPlacement( build, &placements[ i ], angle );
					}

					pickAndPlayUnitVoiceResponse( TheInGameUI->getAllSelectedDrawables(), placeMsg->getType() );

					//
					// get out of pending placement mode, this will also clear the arrow anchor status -
					// unless shift is held, which keeps placing the same structure until released.
					//
					Drawable *nextBuilder = builderObj ? builderObj->getDrawable() : NULL;
					if( TheKeyboard && TheKeyboard->isShift() && nextBuilder )
						TheInGameUI->placeBuildAvailable( build, nextBuilder );
					else
						TheInGameUI->placeBuildAvailable( NULL, NULL );

				}  // end if, location legal to build at
				else
				{
					// can't place, display why
					TheInGameUI->displayCantBuildMessage( lbc );

					//Cannot build here -- play the voice sound from the dozer
					AudioEventRTS sound = *builderObj->getTemplate()->getPerUnitSound( "VoiceNoBuild" );
					sound.setObjectID( builderObj->getID() );
					TheAudio->addAudioEvent( &sound );

					// play a can't do that sound (UI beep type sound)
					static AudioEventRTS noCanDoSound( "NoCanDoSound" );
					TheAudio->addAudioEvent( &noCanDoSound );

					// unhook the anchor so they can try again
					TheInGameUI->setPlacementStart( NULL );

				}  // end else
								
				// used the input
				disp = DESTROY_MESSAGE;
				m_frameOfUpButton = TheGameLogic->getFrame();

			}

			if (disp == DESTROY_MESSAGE) 
				TheInGameUI->clearAttackMoveToMode();

			break;

		}  

		//---------------------------------------------------------------------------------------------
		case GameMessage::MSG_RAW_MOUSE_LEFT_DRAG:
		case GameMessage::MSG_RAW_MOUSE_POSITION:
		{
			// if a building placement is in progress update the destination position
			if (TheInGameUI->isPlacementAnchored())
			{
				//
				// we will only process placement end point sets (clicking, and dragging to set angles)
				// if we have moved far enough away from the start point
				//
				ICoord2D start, end;
				if( getPlacementDrag( &start, &end ) )
				{

					TheInGameUI->setPlacementEnd( &end );
					disp = DESTROY_MESSAGE;

				}  // end if

			}
			break;
		}
	}  

	return disp;
}  


