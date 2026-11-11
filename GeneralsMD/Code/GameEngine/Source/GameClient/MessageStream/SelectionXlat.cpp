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

// SelectionXlat.cpp
// Message stream translator
// Author: Michael S. Booth, January 2001

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "Common/ActionManager.h"
#include "Common/GameAudio.h"
#include "Common/GameEngine.h"
#include "Common/MessageStream.h"
#include "Common/MiscAudio.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/ThingTemplate.h"

#include "GameLogic/Damage.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object.h"
#include "GameLogic/Squad.h"
#include "GameLogic/Module/BodyModule.h"
#include "GameLogic/Module/ContainModule.h"
#include "GameLogic/Module/UpdateModule.h"

#include "GameClient/ControlBar.h"
#include "GameClient/Display.h"
#include "GameClient/Drawable.h"
#include "GameClient/GameClient.h"
#include "GameClient/GameText.h"
#include "GameClient/GameWindowManager.h"
#include "GameClient/GroupPanel.h"
#include "GameClient/Keyboard.h"
#include "GameClient/LookAtXlat.h"
#include "GameClient/SelectionInfo.h"
#include "GameClient/SelectionPriority.h"
#include "GameClient/SelectionXlat.h"
#include "GameClient/TerrainVisual.h"
#include "GameNetwork/GameInfo.h"
#include "GameNetwork/NetworkInterface.h"

#ifdef _INTERNAL
// for occasional debugging...
//#pragma optimize("", off)
//#pragma MESSAGE("************************************** WARNING, optimization disabled for debugging purposes")
#endif

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
// Lorenzen changed this to a member of SelectionTranslator, providing external access
// name ly in rebuildholeexposedie, where we decide whether to create GLA Holes when hand-of-Godding
//#if defined(_DEBUG) || defined(_INTERNAL) || defined(_ALLOW_DEBUG_CHEATS_IN_RELEASE)
//static Bool TheHandOfGodSelectionMode = false;
//#endif

#if defined(_DEBUG) || defined(_INTERNAL)
static Bool TheHurtSelectionMode = false;
static Bool TheDebugSelectionMode = false;
#endif

//-----------------------------------------------------------------------------
/** The takeover click is for the seats the skirmish screen gave a person: your own and the Human
	* (takeover) opponents.  A computer seat can be human too, when no skirmish scripts were found for
	* its general, and clicking one of those used to hand you the enemy's base. */
static Bool isHumanSeat( const Player *p )
{
	if (TheGameInfo == NULL)
		return FALSE;
	for (Int i = 0; i < MAX_SLOTS; ++i)
	{
		AsciiString name;
		name.format("player%d", i);
		if (p->getPlayerNameKey() != NAMEKEY(name))
			continue;
		const GameSlot *slot = TheGameInfo->getConstSlot(i);
		return slot != NULL && (slot->isHuman() || slot->isTakeover());
	}
	return FALSE;
}

//-----------------------------------------------------------------------------
static Bool currentlyLookingForSelection( )
{
	// This needs to check if we are currently targetting for special weapons fire.
	return TheInGameUI->getGUICommand() == NULL;
}

//-----------------------------------------------------------------------------
static Bool areAllSelected( const DrawableList& listToCheck )
{
	DrawableListCIt it;
	for ( it = listToCheck.begin(); it != listToCheck.end(); ++it ) {
		if (!*it)
			continue;

		if (!(*it)->isSelected())
			return FALSE;
	}

	return TRUE;
}

//-----------------------------------------------------------------------------
struct SFWRec
{
	SelectionTranslator *translator;
	GameMessage *createTeamMsg;
	Bool dragSelecting;
};

//-----------------------------------------------------------------------------
/*friend*/ Bool selectFriendsWrapper( Drawable *draw, void *userData )
{
	SFWRec *info = (SFWRec *)userData;
	return info->translator->selectFriends(draw, info->createTeamMsg, info->dragSelecting) != 0;
}  // end selectFriendsWrapper

/*friend*/ Bool killThemKillThemAllWrapper( Drawable *draw, void *userData )
{
	SFWRec *info = (SFWRec *)userData;
	info->translator->killThemKillThemAll( draw, info->createTeamMsg );
	return true;
}

//-----------------------------------------------------------------------------
/** 
 * Returns true if the drawable can be selected under the current rules
 * of the system 
 */
Bool CanSelectDrawable( const Drawable *draw, Bool dragSelecting )
{

	if(!draw || !draw->getObject())
	{
		return FALSE;  // can't select
	}
	const Object *obj = draw->getObject();
	
	if( obj->isEffectivelyDead() && !obj->isKindOf(KINDOF_ALWAYS_SELECTABLE))
	{
		//Don't select dead/dying units.
		return FALSE;
	}

	//Added this to support attacking cargo planes without being able to select them.
	//I added the KINDOF_FORCEATTACKABLE to them, but unsure if it's possible to select
	//something without the KINDOF_SELECTABLE -- so doing a LATE code change. My gut
	//says we should simply have the KINDOF_SELECTABLE check only... but best to be safe.
	if( !obj->isKindOf( KINDOF_SELECTABLE ) && obj->isKindOf( KINDOF_FORCEATTACKABLE ) )
	{
		return FALSE;		
	}

	// hidden objects cannot be selected
	if( draw->isDrawableEffectivelyHidden() )
	{
		return FALSE;  // can't select
	}

	// ignore objects obscured by the GUI
	GameWindow *window = NULL;
	if (TheWindowManager)
	{
		const Coord3D *c = draw->getPosition();
		ICoord2D c2;
		TheTacticalView->worldToScreen(c, &c2);
		window = TheWindowManager->getWindowUnderCursor(c2.x, c2.y);
	}

	while (window)
	{
		// check to see if it or any of its parents are opaque.  If so, we can't select anything.
		if (!BitTest( window->winGetStatus(), WIN_STATUS_SEE_THRU ))
		{
			return FALSE;
		}

		window = window->winGetParent();
	}

	//
	// structures cannot be selected by a drag select, you must individually pick them
	// NOTE that this is really a convenience for the multi select context sensitive UI,
	// later we might want to allow you to drag select buildings if only one building is
	// actually in the selection area, but don't forget complications like holding down
	// a key to "add" to an already existing selection list
	//
	// not allowing you to have multiple buildings selected drastically simplifies the
	// user interface ... including all those context sensitive commands that we
	// can just assume are for a single building selected.
	//
	if( dragSelecting && draw->isKindOf( KINDOF_STRUCTURE ) )
	{
		return FALSE;
	}

	// You cannot select something that has a logic override of unselectability or masked
	if( obj->getStatusBits().testForAny( MAKE_OBJECT_STATUS_MASK2( OBJECT_STATUS_UNSELECTABLE, OBJECT_STATUS_MASKED ) ) )
	{
		return FALSE;
	}

	if (!obj->isSelectable())
	{
		return false;
	}
	//Now allowing the selection of everything including enemies... but only if not drag selecting.
	//In fact the only way you can drag select is if the unit is on your team.
	if( dragSelecting && !obj->isLocallyControlled() )
	{
		return FALSE;
	}

	//Now we can select anything that is selectable.
	return TRUE;

}  // end canSelect

//-----------------------------------------------------------------------------
static Bool isSelectableMilitaryInBox(Drawable *draw)
{
	if (!CanSelectDrawable(draw, TRUE))
		return FALSE;
	const Object *obj = draw->getObject();
	return obj->getContainedBy() == NULL && isMilitaryBoxSelectionUnit(obj);
}

//-----------------------------------------------------------------------------
static Bool canSelectWrapper( Drawable *draw, void *userData )
{
	Bool dragSelecting = *((Bool *)userData);
	return CanSelectDrawable( draw, dragSelecting );
}

//-----------------------------------------------------------------------------
/**
 * Deselect all drawables, and emit a "TEAM_DESTROY" message, since
 * the "team" was the group of currently selected units.
 */
static void deselectAll()
{

	// deselect it all
	TheInGameUI->deselectAllDrawables();
}

//-----------------------------------------------------------------------------
/**
 * Select the given drawable, without playing its sound.
 * Returns true.
 */
static Bool selectSingleDrawableWithoutSound( Drawable *draw )
{

	// since we are single selecting a drawable, unselect everything else
	deselectAll();

	// do the drawble selection
	TheInGameUI->selectDrawable( draw );

	Object *obj = draw->getObject();
	if (obj != NULL) {
		GameMessage *msg = TheMessageStream->appendMessage(GameMessage::MSG_CREATE_SELECTED_GROUP_NO_SOUND);
		msg->appendBooleanArgument(TRUE);
		msg->appendObjectIDArgument(obj->getID());
	}

	return true;

}

SelectionTranslator *TheSelectionTranslator = NULL;
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
SelectionTranslator::SelectionTranslator()
{
	m_leftMouseButtonIsDown = FALSE;
	m_dragSelecting = FALSE;
	m_leftDragIsLine = FALSE;
	m_dragOrderJustIssued = FALSE;
	m_lastGroupSelTime = 0;
	m_lastGroupSelGroup = -1;
	m_selectFeedbackAnchor.x = 0;
	m_selectFeedbackAnchor.y = 0;
	m_deselectFeedbackAnchor.x = 0;
	m_deselectFeedbackAnchor.y = 0;
	m_lastClick = 0;
	m_deselectDownCameraPosition.zero();
	m_displayedMaxWarning = FALSE;
	m_selectCountMap.clear();
	forgetPendingSquads();

	TheSelectionTranslator = this;

#if defined(_DEBUG) || defined(_INTERNAL) || defined(_ALLOW_DEBUG_CHEATS_IN_RELEASE)
  m_HandOfGodSelectionMode = FALSE;
#endif
}

//-----------------------------------------------------------------------------
SelectionTranslator::~SelectionTranslator()
{
}

//-----------------------------------------------------------------------------
/** 
 * If this drawable is a 'friend' of mine, select it.
 */
Bool SelectionTranslator::selectFriends( Drawable *draw, GameMessage *createTeamMsg, 
																				 Bool dragSelecting )
{
	if (CanSelectDrawable( draw, dragSelecting ))
	{
		// enforce an optional selection size limit
		if (TheInGameUI->getMaxSelectCount() > 0 && TheInGameUI->getSelectCount() >= TheInGameUI->getMaxSelectCount())
		{
			if (!m_displayedMaxWarning)
			{
				m_displayedMaxWarning = TRUE;
				UnicodeString msg;
				msg.format(TheGameText->fetch("GUI:MaxSelectionSize").str(), TheInGameUI->getMaxSelectCount());
				TheInGameUI->message(msg);
			}
			return false;
		}

		TheInGameUI->selectDrawable( draw );

		m_selectCountMap[draw->getTemplate()]++;

		// add to message's argument list if an object is present
		if( draw->getObject() && createTeamMsg )
			createTeamMsg->appendObjectIDArgument( draw->getObject()->getID() );

		return true;  // selected

	}  // end if

	return false;  // not selected

}  // end selectFriends


//-----------------------------------------------------------------------------
Bool SelectionTranslator::killThemKillThemAll( Drawable *draw, GameMessage *killThemAllMsg )
{
	if( draw )
	{
		Object *obj = draw->getObject();
		if( obj )
		{
			// enforce an optional selection size limit
			if (TheInGameUI->getMaxSelectCount() > 0 && TheInGameUI->getSelectCount() >= TheInGameUI->getMaxSelectCount())
			{
				if (!m_displayedMaxWarning)
				{
					m_displayedMaxWarning = TRUE;
					UnicodeString msg;
					msg.format(TheGameText->fetch("GUI:MaxSelectionSize").str(), TheInGameUI->getMaxSelectCount());
					TheInGameUI->message(msg);
				}
				return false;
			}

			// add to message's argument list if an object is present
			if( killThemAllMsg )
			{
				killThemAllMsg->appendObjectIDArgument( draw->getObject()->getID() );
			}

			return true;  // selected
		}
	}
	return false;
}  // end selectFriends

//-----------------------------------------------------------------------------
/**
 * The SelectionTranslator is responsible for all selection semantics, 
 * including click selection, area drag selection, right-click de-selection, 
 * and CTRL-key group selection.
 * NOTE: This handler changes the event semantics for mouse buttons from 
 * LEFT_DOWN -> LEFT_UP  to  LEFT_DOWN -> { LEFT_UP, AREA_SELECTION, or DRAWABLE_PICKED }
 */
GameMessageDisposition SelectionTranslator::translateGameMessage(const GameMessage *msg)
{
	GameMessageDisposition disp = KEEP_MESSAGE;

	if(	!TheInGameUI->getInputEnabled() ) 
	{
		//Keep the message so the other translaters (WindowXlat) can handle.
		if( m_dragSelecting )
		{
			//Turn off drag select
			m_dragSelecting = FALSE;
			TheInGameUI->setSelecting( FALSE );
			TheInGameUI->endAreaSelectHint(NULL);
			TheTacticalView->setMouseLock( FALSE );
		}
		return KEEP_MESSAGE;
	}

	GameMessage::Type t = msg->getType();
	switch (t)
	{
		case GameMessage::MSG_META_BEGIN_FORCEATTACK:
			TheInGameUI->setForceAttackMode( true );
			break;

		case GameMessage::MSG_META_END_FORCEATTACK:
			TheInGameUI->setForceAttackMode( false );
			break;

		//-----------------------------------------------------------------------------
		case GameMessage::MSG_RAW_MOUSE_POSITION:
		{
			ICoord2D pixel;
			pixel = msg->getArgument( 0 )->pixel;


			// modifier appears to be unused, and the argument doesn't exist.  jba.
			//Int modifier = msg->getArgument( 1 )->integer;

			// an attack circle owns the button while it is being dragged, so no box grows behind it
			if( TheInGameUI->isAttackCircling() )
			{
				TheInGameUI->updateAttackCircle( pixel );
				break;
			}

			// a left drag that draws an order line over in CommandXlat grows no selection box under it.
			// A drag with no line wanted, or Shift's move line where the right button draws it, is the box
			const Bool leftDragIsOrder = m_leftDragIsLine && TheInGameUI->getSelectCount() > 0;
			if (m_leftMouseButtonIsDown && !leftDragIsOrder)
			{
				ICoord2D delta;

				delta.x = abs(pixel.x - m_selectFeedbackAnchor.x);
				delta.y = abs(pixel.y - m_selectFeedbackAnchor.y);

				// if mouse has moved while left button is down, begin drag selection
				if (delta.x > TheMouse->m_dragTolerance || delta.y > TheMouse->m_dragTolerance)
				{
					if (m_dragSelecting == false)
					{
						m_dragSelecting = true;
						TheTacticalView->setMouseLock( TRUE );
						TheInGameUI->setSelecting( TRUE );
					}
				}

				// create "hint" messages defining selection region under construction
				if (m_dragSelecting)
				{
					// insert area selection "hint" message into stream
					GameMessage *hintMsg = TheMessageStream->appendMessage( GameMessage::MSG_BEGIN_AREA_SELECTION_HINT );

					// build rectangular region defined by the drag selection
					IRegion2D pixelRegion;
					buildRegion( &m_selectFeedbackAnchor, &pixel, &pixelRegion );
					hintMsg->appendPixelRegionArgument( pixelRegion );
				}
			}
			else //left button is not down (not drag select)
			{
				// insert Mouseover hint into stream for CommandTranslator and HintSpy to see.
				GameMessage *mouseoverMessage;

				//Kris: We want to show information such as the popup text on objects that are forceattackable even
				//      when we're not in force attackable mode!
				UnsignedInt pickType = getPickTypesForContext( true /*TheInGameUI->isInForceAttackMode()*/ );
				
				Drawable *underCursor = TheTacticalView->pickDrawable( &pixel, TheInGameUI->isForceFireOn(), (PickType) pickType );

				//
				// a click on a health bar selects its owner, so the cursor has to say so before the
				// click - the same fallback, in the same order, as the point pick in
				// W3DView::iterateDrawablesInRegion
				//
				if( underCursor == NULL )
					underCursor = TheGameClient->pickDrawableByHealthBar( &pixel );

				Object *objUnderCursor = underCursor ? underCursor->getObject() : NULL;

				if( objUnderCursor && (!objUnderCursor->isEffectivelyDead() || objUnderCursor->isKindOf( KINDOF_ALWAYS_SELECTABLE )) )
				{
					mouseoverMessage = TheMessageStream->appendMessage( GameMessage::MSG_MOUSEOVER_DRAWABLE_HINT );
					mouseoverMessage->appendDrawableIDArgument( underCursor->getID() );
				}
				else// else this is a mouseover terrain
				{
					Coord3D position;

					TheTacticalView->screenToTerrain( &pixel, &position );
					mouseoverMessage = TheMessageStream->appendMessage( GameMessage::MSG_MOUSEOVER_LOCATION_HINT );
					mouseoverMessage->appendLocationArgument( position );
				}
			}

			break;
		}

		//-----------------------------------------------------------------------------
		case GameMessage::MSG_MOUSE_LEFT_DOUBLE_CLICK:
		{
			Int modifiers = msg->getArgument(1)->integer;

			// the release that ended a circle arrives as a double click when the circle was the
			// second of a quick pair.  Letting it through would select everything of one kind on the
			// screen, and that new selection is exactly what drops the orders just given
			if( m_dragOrderJustIssued )
			{
				m_dragOrderJustIssued = FALSE;
				disp = DESTROY_MESSAGE;
				break;
			}

			// an armed order key makes this click an order, which CommandXlat gives
			if (TheInGameUI->isOrderKeyArmed() || TheInGameUI->isForceFireOn())
				break;

			const IRegion2D& region = msg->getArgument(0)->pixelRegion;
	
			// Single point. If there's a unit in there, double click will select all of them.
			if (region.height() == 0 && region.width() == 0) 
			{
				Bool selectAcrossMap = (BitTest(modifiers, KEY_STATE_ALT) ? TRUE : FALSE);

				// only allow things that are selectable. Also, we aren't allowed to 
				Drawable *picked = TheTacticalView->pickDrawable( &region.lo, FALSE, PICK_TYPE_SELECTABLE);

				// If there wasn't anyone to pick, then we want to propagate this double click.
				if (picked == NULL)
					break;

				if (!picked->isMassSelectable())
					break;

				Object *pickedObj = picked->getObject();

				// We have to have an object in order to be able to do interesting double click stuff on
				// him. Also, if it is a structure, it is already selected, so don't select all the units
				// like him.
				if (pickedObj == NULL || !pickedObj->isLocallyControlled())
					break;

				// Ok. The logic is a little bit weird here. What we need to do is deselect everything 
				// except for this one picked thing. Store off the old selection, pick the single clicked thing.
				// Then if 
				DrawableList listOfSelectedDrawables;
				if (TheInGameUI->isInPreferSelectionMode()) {
					listOfSelectedDrawables	= *TheInGameUI->getAllSelectedDrawables();
				}

				// Pick just that one guy.
				selectSingleDrawableWithoutSound(picked);

				// Yay. Either select across the screen or the world depending on selectAcrossMap
				if (selectAcrossMap)
					TheInGameUI->selectMatchingAcrossMap();
				else 
					TheInGameUI->selectMatchingAcrossScreen();

				// emit "picked" message
				GameMessage *pickMsg = TheMessageStream->appendMessage( GameMessage::MSG_END_AREA_SELECTION_HINT );
				pickMsg->appendDrawableIDArgument( picked->getID() );  /// note we are putting in a drawable id

				if (TheInGameUI->isInPreferSelectionMode() && !listOfSelectedDrawables.empty()) {
					GameMessage *selectMore = TheMessageStream->appendMessage( GameMessage::MSG_CREATE_SELECTED_GROUP_NO_SOUND );
					selectMore->appendBooleanArgument(FALSE);
					for (DrawableListIt it = listOfSelectedDrawables.begin(); it != listOfSelectedDrawables.end(); ++it) {
						Drawable *draw = *it;
						// buildings and units stay in separate selections, and a double click that
						// gathers everything of a kind may only gather what a box would - things
						// you can click one at a time but never sweep up used to come along.  Classic keeps
						// 1.04's rule, which put back everything that was selected of the same kind
						const Bool keep = draw && draw->isKindOf( KINDOF_STRUCTURE ) == picked->isKindOf( KINDOF_STRUCTURE )
							&& ( TheGlobalData->isClassicMouse() ? draw->isSelectable() : draw->isMassSelectable() );
						if (keep) {
							TheInGameUI->selectDrawable(draw);
							selectMore->appendObjectIDArgument(draw->getObject()->getID());
						}
					}
				}

				disp = DESTROY_MESSAGE;
			}
			break;
		}

		//-----------------------------------------------------------------------------
		case GameMessage::MSG_MOUSEOVER_DRAWABLE_HINT:
		{
			if (TheInGameUI->isScrolling()) {
				// dont show this now.
				break;
			}

			DrawableID id = msg->getArgument(0)->drawableID;
			Drawable *draw = TheGameClient->findDrawableByID(id);
			if (!draw) {
				break;
			}

			GameMessage::Type msgType = TheGameClient->evaluateContextCommand(draw, draw->getPosition(), CommandTranslator::EVALUATE_ONLY);
			if( msgType == GameMessage::MSG_INVALID )
			{
				TheInGameUI->createMouseoverHint(msg); // this sets the cursor
				disp = DESTROY_MESSAGE;
				const CommandButton *command = TheInGameUI->getGUICommand();

				Bool ignoreCommand = FALSE;

				//
				// A structure riding the cursor owns the pointer - InGameUI::createCommandHint's
				// MOUSEMODE_BUILD_PLACE case keeps it on the build cursor.  Moving over a building
				// used to turn it into the selection hand here, on top of that, which reads as "this
				// click selects" when the click still places.
				//
				if( TheInGameUI->getPendingPlaceType() )
					ignoreCommand = TRUE;

				if( command )
				{
					if( command->getCommandType() == GUI_COMMAND_ATTACK_MOVE )
					{
						//These GUI commands can take care of themselves -- don't let
						//the selection translator meddle.
						ignoreCommand = TRUE;
					}
				}
				if( !ignoreCommand && !draw->getTemplate()->isKindOf( KINDOF_SHRUBBERY ) )
				{
					if( CanSelectDrawable( draw, FALSE ) )
					{
						TheMouse->setCursor(Mouse::SELECTING);
					}
					else
					{
						TheMouse->setCursor( Mouse::ARROW );
					}
				}
			}

			break;
		}

		//-----------------------------------------------------------------------------
		case GameMessage::MSG_MOUSE_LEFT_CLICK:
		{
			// a signal armed off the command bar goes down where this click is, and the click selects nothing
			if( TheInGameUI->isSignalArmed() )
			{
				const IRegion2D &clicked = msg->getArgument(0)->pixelRegion;
				const ICoord2D centre = { ( clicked.lo.x + clicked.hi.x ) / 2, ( clicked.lo.y + clicked.hi.y ) / 2 };
				Coord3D world;
				if( TheTacticalView->screenToTerrain( &centre, &world ) )
					TheInGameUI->placeArmedSignal( world );
				disp = DESTROY_MESSAGE;
				break;
			}

			// the release that ended an attack circle still arrives here as a click.  Letting it
			// through would reselect whatever sat under the anchor, and a changed selection is exactly
			// what drops the queue that was just built
			if( m_dragOrderJustIssued )
			{
				m_dragOrderJustIssued = FALSE;
				disp = DESTROY_MESSAGE;
				break;
			}

			// If the quit menu is visible, we need to not process left clicks through the selection translator.
			if (TheInGameUI->isQuitMenuVisible()) 
			{
				disp = DESTROY_MESSAGE;
				break;
			}

			// With the attack, attack move or guard key armed and something selected, a point click is
			// the order and not a selection.  It goes on to CommandXlat untouched, drawable under it or not
			if (TheInGameUI->isOrderKeyArmed() && TheInGameUI->getSelectCount() > 0)
				break;

			// Basically, we need to first determine if there are any drawables in the region of interest.
			// If there aren't then this click should move forward.
			IRegion2D selectionRegion = msg->getArgument(0)->pixelRegion;
			Bool isPoint = (selectionRegion.height() == 0 && selectionRegion.width() == 0);
			
			DrawableList drawablesThatWillSelect;
			PickDrawableStruct pds;
			pds.drawableListToFill = &drawablesThatWillSelect;
			TheTacticalView->iterateDrawablesInRegion(&selectionRegion, addDrawableToList, &pds);

			if (drawablesThatWillSelect.empty()) 
			{
				break;
			}

			// if there were drawables in the region, then we should determine if there is a context 
			// sensitive command that should take place. If there is, then this isn't a selection thing
			const DrawableList *currentList = TheInGameUI->getAllSelectedDrawables();
			if (!currentlyLookingForSelection())
			{
				break;
			}

			/* Box select modifiers, taken from Beyond All Reason, where they are the difference
				 between a selection and the right selection.  Shift already adds, so what was missing
				 was a way to take things back out and a way to leave the base staff behind: Alt keeps
				 only what can shoot, Ctrl removes the box from the selection instead of replacing it.
				 Ctrl+Shift selects the box: both meta modes drop when the second modifier goes
				 down, the fire circle never starts, and this Ctrl-remove used to eat the click.
				 Both filters are drag-only.  A point click has to stay exactly what it was - a filter
				 that eats single clicks reads as a broken mouse.  Classic has neither: its Ctrl is
				 force fire. */
			if (!isPoint && !TheGlobalData->isClassicMouse())
			{
				if (TheKeyboard->isAlt())
				{
					DrawableListIt fit = drawablesThatWillSelect.begin();
					while (fit != drawablesThatWillSelect.end())
					{
						Object *candidate = (*fit) ? (*fit)->getObject() : NULL;
						const Bool fights = candidate && candidate->hasAnyWeapon() &&
																!candidate->isKindOf(KINDOF_STRUCTURE);
						if (fights)
							++fit;
						else
							fit = drawablesThatWillSelect.erase(fit);
					}

					// a box with nothing armed in it is a miss, not an order to select the base
					if (drawablesThatWillSelect.empty())
					{
						disp = DESTROY_MESSAGE;
						break;
					}
				}

				if (TheKeyboard->isCtrl() && !TheKeyboard->isShift())
				{
					GameMessage *removeMsg = NULL;
					DrawableListIt rit;
					for (rit = drawablesThatWillSelect.begin(); rit != drawablesThatWillSelect.end(); ++rit)
					{
						Drawable *draw = *rit;
						if (draw == NULL || !draw->isSelected())
						{
							continue;
						}

						Object *objToDeselect = draw->getObject();
						if (objToDeselect == NULL)
						{
							continue;
						}

						// the message is only worth making once something is actually coming out
						if (removeMsg == NULL)
						{
							removeMsg = TheMessageStream->appendMessage(GameMessage::MSG_REMOVE_FROM_SELECTED_GROUP);
						}

						removeMsg->appendObjectIDArgument(objToDeselect->getID());
						TheInGameUI->deselectDrawable(draw);
					}

					m_lastGroupSelGroup = -1;
					disp = DESTROY_MESSAGE;
					break;
				}
			}

			// A box with an army in it takes the army and leaves the dozers and trucks standing among
			// it; a box of workers alone still takes the workers.  Drag-only, like the filters above,
			// but in Classic too: Classic is what every player gets, and there a box round an army
			// still came back with the dozer standing in it.
			if (!isPoint)
				prioritizeMilitaryBoxSelection(drawablesThatWillSelect, isSelectableMilitaryInBox);

			SelectionInfo si;
			if (contextCommandForNewSelection(currentList, &drawablesThatWillSelect, &si, isPoint))
			{
				break;
			}

			/* Single-player test hook: a click that is not a command (nothing of yours selected, or
				 nothing to do to it) on a base that nobody is driving - a Human seat from the skirmish
				 screen, or your own seat after you left it - hands you that player, so the command bar,
				 the money and the shroud become its own.  Computer seats are left alone, one you took
				 and left again and one that ended up human for want of scripts included (isHumanSeat);
				 only Shift-Ctrl-T takes those.  takeControlOfPlayer refuses network games and replays, and the logic
				 drops the message in a network game whoever sends it. */
			for (DrawableListIt cit = drawablesThatWillSelect.begin(); cit != drawablesThatWillSelect.end(); ++cit)
			{
				Object *clicked = (*cit) ? (*cit)->getObject() : NULL;
				Player *owner = clicked ? clicked->getControllingPlayer() : NULL;
				if (owner == NULL || owner->getPlayerType() != PLAYER_HUMAN || !isHumanSeat(owner))
					continue;

				if (takeControlOfPlayer(owner))
				{
					selectSingleDrawableWithoutSound(*cit);
					disp = DESTROY_MESSAGE;
				}
				break;
			}
			if (disp == DESTROY_MESSAGE)
				break;

			// There isn't a context command, so this is a selection thing. Now, based on the keys, 
			// determine whether or not we should create a new group, or append these guys to our existing
			// group.
			
			Bool addToGroup = TheInGameUI->isInPreferSelectionMode();

			if (si.currentCountEnemies > 0 ||
					si.currentCountCivilians > 0 ||
					si.currentCountFriends > 0)
			{
				// force a new group creation
				addToGroup = FALSE;
			}

			// If there are any of my units, then select those.
			if (si.newCountMine > 0)
			{
				si.selectMine = TRUE;

				//
				// Buildings can be group-selected (click, drag, shift-add) like units, but buildings and
				// units never share one selection: a pick of one kind replaces a selection of the other.
				// A pick containing both keeps only the units, as it always did.
				//
				if ( si.newCountMine == si.newCountMineBuildings )
				{
					si.selectMineBuildings = TRUE;
					if ( si.currentCountMine > si.currentCountMineBuildings )
						addToGroup = FALSE;
				}
				else if ( si.currentCountMineBuildings > 0 )
				{
					addToGroup = FALSE;
				}
			}
			else if (si.newCountEnemies > 0 && si.newCountCivilians > 0 && si.newCountFriends > 0) 
			{
				// No go here
				break;
			} 
			else if (si.newCountEnemies == 1) 
			{
				addToGroup = FALSE;
				si.selectEnemies = TRUE;
			} 
			else if (si.newCountCivilians == 1) 
			{
				addToGroup = FALSE;
				si.selectCivilians = TRUE;
			} 
			else if (si.newCountFriends == 1) 
			{
				addToGroup = FALSE;
				si.selectFriends = TRUE;
			}

			// If we're not going to select anything, just bail now.
			if (!(si.selectMine || si.selectEnemies || si.selectCivilians || si.selectFriends)) 
			{
				break;
			}

			// If we've made it here, its time to do some selecting.
			disp = DESTROY_MESSAGE;

			// Whenever we manually select something, reset the last selected group.
			m_lastGroupSelGroup = -1;
			
			if (TheInGameUI->isInPreferSelectionMode() && isPoint && areAllSelected(drawablesThatWillSelect)) 
			{
				// If this was a point, shift was pressed and we already have that unit selected, then we
				// need to deselect those units.
				GameMessage *newMsg = TheMessageStream->appendMessage(GameMessage::MSG_REMOVE_FROM_SELECTED_GROUP);
				Drawable *draw = NULL;
				DrawableListIt it;
				for (it = drawablesThatWillSelect.begin(); it != drawablesThatWillSelect.end(); ++it) 
				{
					draw = *it;
					if (!draw) 
					{
						continue;
					}

					Object *objToDeselect = draw->getObject();
					if (!objToDeselect) 
					{
						continue;
					}

					newMsg->appendObjectIDArgument(objToDeselect->getID());
					TheInGameUI->deselectDrawable(draw);
				}
			} 
			else 
			{
				if (!addToGroup) 
				{
					deselectAll();
				}

				GameMessage *newMsg = TheMessageStream->appendMessage(GameMessage::MSG_CREATE_SELECTED_GROUP);
				newMsg->appendBooleanArgument(!addToGroup);
				
				Player *localPlayer = ThePlayerList->getLocalPlayer();

				Int newDrawablesSelected = 0;
				Drawable *draw = NULL;
				DrawableListIt it;
				for (it = drawablesThatWillSelect.begin(); it != drawablesThatWillSelect.end(); ++it) 
				{
					draw = *it;
					if (!draw) 
					{
						continue;
					}

					Object *obj = draw->getObject();
					if (!obj) 
					{
						continue;
					}
					
					if (obj && obj->getContainedBy() != NULL) 
					{
						// we're contained, and so we shouldn't be selectable.
						continue;
					}

					Drawable *drawToSelect = NULL;
					ObjectID objToAppend = INVALID_ID;
					if (si.selectMine && obj->isLocallyControlled()) 
					{
						if (!obj->isKindOf(KINDOF_STRUCTURE) || si.selectMineBuildings) 
						{
							drawToSelect = draw;
							objToAppend = obj->getID();
						}
					} 
					else 
					{
						Relationship rel = localPlayer->getRelationship(obj->getTeam());
						if (si.selectEnemies && rel == ENEMIES) 
						{
							drawToSelect = draw;
							objToAppend = obj->getID();
						} 
						else if (si.selectCivilians && rel == NEUTRAL) 
						{
							drawToSelect = draw;
							objToAppend = obj->getID();
						} 
						else if (si.selectFriends && rel == ALLIES) 
						{
							drawToSelect = draw;
							objToAppend = obj->getID();
						}
					}

					if (drawToSelect && objToAppend != INVALID_ID) 
					{
						newMsg->appendObjectIDArgument(objToAppend);
						TheInGameUI->selectDrawable(drawToSelect);
						++newDrawablesSelected;
					}
				}

				if( newDrawablesSelected > 1 )
				{
					localPlayer->getAcademyStats()->recordDragSelection();
				}

				if (newDrawablesSelected == 1 && draw) 
				{


#if defined(_DEBUG) || defined(_INTERNAL) 


          if (m_HandOfGodSelectionMode && draw)
					{
						Object* obj = draw->getObject();
						if (obj)
						{
							TheAudio->addAudioEvent(&TheAudio->getMiscAudio()->m_noCanDoSound);
							GameMessage* msg = TheMessageStream->appendMessage( GameMessage::MSG_DEBUG_KILL_OBJECT );
							msg->appendObjectIDArgument(obj->getID());
						}
						disp = DESTROY_MESSAGE;
						break;
					}
					else

          if (TheHurtSelectionMode && draw)
					{
						Object* obj = draw->getObject();
						if (obj)
						{
							TheAudio->addAudioEvent(&TheAudio->getMiscAudio()->m_noCanDoSound);
							GameMessage* msg = TheMessageStream->appendMessage( GameMessage::MSG_DEBUG_HURT_OBJECT );
							msg->appendObjectIDArgument(obj->getID());
						}
						disp = DESTROY_MESSAGE;
						break;
					}

  #ifdef DEBUG_OBJECT_ID_EXISTS
					if (TheDebugSelectionMode && draw && draw->getObject())
					{
						if (TheObjectIDToDebug == 0)
						{
							TheObjectIDToDebug = draw->getObject()->getID();
							AsciiString msg;
							msg.format("Item %s %08x selected for debugging",draw->getTemplate()->getName().str(),TheObjectIDToDebug);
							UnicodeString msgu;
							msgu.translate(msg);
							TheInGameUI->message(msgu);
							disp = DESTROY_MESSAGE;
							break;
						}
					}
  #endif

#endif


#if defined(_ALLOW_DEBUG_CHEATS_IN_RELEASE)
          if (m_HandOfGodSelectionMode && draw)
					{
						Object* obj = draw->getObject();
						if (obj)
						{
							TheAudio->addAudioEvent(&TheAudio->getMiscAudio()->m_noCanDoSound);
							GameMessage* msg = TheMessageStream->appendMessage( GameMessage::MSG_DEBUG_KILL_OBJECT );
							msg->appendObjectIDArgument(obj->getID());
						}
						disp = DESTROY_MESSAGE;
						break;
					}
#endif    

        
        }
			}

			if (disp == DESTROY_MESSAGE)
				TheInGameUI->clearAttackMoveToMode();
			
			break;
		}

		//-----------------------------------------------------------------------------
		// Note that the raw left messages are only used to draw feedback now when 
		// appropriate. All actual selection code takes place in 
		// MSG_MOUSE_LEFT_CLICK & MSG_MOUSE_LEFT_DOUBLE_CLICK
		//
		// Windows sends the second press of a quick pair as WM_LBUTTONDBLCLK rather than
		// WM_LBUTTONDOWN, so it arrives here as a double click.  Sweeping a second circle straight
		// after the first is exactly that pair: without this label no circle began, the drag grew a
		// selection box instead, and the box swapped the group out from under the order.
		//
		case GameMessage::MSG_RAW_MOUSE_LEFT_BUTTON_DOWN:
		case GameMessage::MSG_RAW_MOUSE_LEFT_DOUBLE_CLICK:
		{
			// cannot actually start area selection yet - have to wait for cursor to move a bit
			m_leftMouseButtonIsDown = true;
			m_selectFeedbackAnchor = msg->getArgument( 0 )->pixel;

			// latched here, the same moment CommandXlat latches the line, so letting go of shift
			// mid-drag cannot grow a box under a line that is already being drawn.  Shift's move line
			// belongs to the button that moves; where that is the right one, the left drag is the box
			m_leftDragIsLine = TheInGameUI->isOrderDragButton( FALSE ) && TheInGameUI->wantsDragLine();

			// with the attack key armed or the Force Attack hold down the left button draws a circle
			// in every mouse instead of a selection box, and everything hostile inside it becomes a
			// target list.  An armed line key (move, attack move, guard) keeps its line under the hold,
			// and shift on top of the hold draws the attack line over in CommandXlat instead.
			if( TheInGameUI->isOrderDragButton( FALSE ) && TheInGameUI->wantsDragCircle() )
				TheInGameUI->beginAttackCircle( m_selectFeedbackAnchor );
			break;
		}

		//-----------------------------------------------------------------------------
		// Note that the raw left messages are only used to draw feedback now when 
		// appropriate. All actual selection code takes place in 
		// MSG_MOUSE_LEFT_CLICK & MSG_MOUSE_LEFT_DOUBLE_CLICK
		case GameMessage::MSG_RAW_MOUSE_LEFT_BUTTON_UP:
		{
			m_leftMouseButtonIsDown = FALSE;

			// the circle is finished on the button that drew it: the targets go out here, and the
			// click that follows this release has to be eaten or it would reselect under the anchor
			if( TheInGameUI->isAttackCircling() )
			{
				// a circle Ctrl started belongs to no key: undragged, the press was an ordinary Ctrl click
				const Bool keyed = TheInGameUI->isOrderKeyArmed() || TheInGameUI->getGUICommand() != NULL;

				// a press with the attack key that never became a drag is an ordinary attack click,
				// and the click that follows this release is the order.  Only a real circle eats it
				if( TheInGameUI->issueAttackCircle() )
				{
					TheInGameUI->spendOrderKey();
					m_dragOrderJustIssued = TRUE;
					break;
				}

				//
				// A press under an armed key belonged to that key rather than to selection, so the
				// release stops here.  Falling through reaches the deselect that ends an ordinary left
				// click, and for a press that was not a drag - a plain attack click, or a circle small
				// enough to have no radius - that threw the group away a moment before the click behind
				// it arrived as the order, leaving the order with nobody to give it to.
				//
				if( keyed )
					break;
			}

			// a left drag that drew an attack move, guard or attack line is turned into its order by
			// CommandXlat, which sees this release next.  The click behind the release would select
			// whatever sat under it and drop the group the line was drawn for
			if( m_leftDragIsLine && TheInGameUI->isFormationDragging() )
			{
				m_dragOrderJustIssued = TRUE;
				break;
			}

			if (m_dragSelecting) {
				// Stop drag selecting now, thanks.
				m_dragSelecting = FALSE;

				TheTacticalView->setMouseLock( FALSE );
				TheInGameUI->setSelecting( FALSE );
				TheInGameUI->endAreaSelectHint(NULL);

				// insert area selection message into stream
				GameMessage *dragMsg = TheMessageStream->appendMessage( GameMessage::MSG_END_AREA_SELECTION_HINT );

				IRegion2D selectionRegion;
				buildRegion( &m_selectFeedbackAnchor, &msg->getArgument(0)->pixel, &selectionRegion );
				dragMsg->appendPixelRegionArgument( selectionRegion );
			}
			else 
			{
				// left click behavior (not right drag)

				//Added support to cancel the GUI command without deselecting the unit(s) involved
				//when you right click.
				// an armed order key keeps the group: the click behind this release is its order
				if( !TheInGameUI->getGUICommand() && !TheInGameUI->isOrderKeyArmed()
						&& !TheKeyboard->isShift() && !TheKeyboard->isCtrl() && !TheKeyboard->isAlt() )
				{
					//No GUI command mode, so a click on empty ground deselects everyone.  Not in Classic's
					//default mouse, where that click is a move order and the right button is what deselects.
					if( TheInGameUI->getPendingPlaceSourceObjectID() == INVALID_ID && !TheGlobalData->leftButtonOrders() )
					{
						if( !TheInGameUI->getPreventLeftClickDeselectionInAlternateMouseModeForOneClick() )
						{
							deselectAll();
						}
						else
						{
							//Prevent deselection of unit if it just issued some type of UI order such as attack move, guard, 
							//initiating construction of a new structure.
							TheInGameUI->setPreventLeftClickDeselectionInAlternateMouseModeForOneClick( FALSE );
						}
					}
				}
			}

			break;
		}

		//-----------------------------------------------------------------------------
		// Classic measures a right click the way 1.04 did, from this press to the release below
		case GameMessage::MSG_RAW_MOUSE_RIGHT_BUTTON_DOWN:
		{
			m_deselectFeedbackAnchor = msg->getArgument( 0 )->pixel;
			m_lastClick = (UnsignedInt) msg->getArgument( 2 )->integer;
			TheTacticalView->getPosition( &m_deselectDownCameraPosition );
			break;
		}

		//-----------------------------------------------------------------------------
		case GameMessage::MSG_RAW_MOUSE_RIGHT_BUTTON_UP:
		{
			// a signal armed off its key is all the right button takes back, in either mouse style.  This
			// stood below the Classic branch, which breaks out on every release, so with Classic's mouse,
			// the default, a right click never reached it.  A right drag that panned still takes nothing back
			if( TheInGameUI->isSignalArmed() && !TheLookAtTranslator->isRightDragPanning() )
			{
				TheInGameUI->disarmSignal();
				break;
			}

			// Classic is 1.04's right release.  Only a click counts: the press is the scroll as well
			// (LookAtXlat), so a release that travelled, was held, or rode the camera too far is not one.
			// The click takes back a waiting GUI command and nothing else, and eats the release, which
			// leaves CommandXlat measuring the alternate mouse's right click against the last release
			// instead, so that click gives no order either.  Otherwise it deselects everyone: always in
			// the default mouse, where it is the cancel button, and under the Alternate Mouse Setup only
			// with a dozer's structure on the cursor.  The structure itself comes off in CommandXlat.
			if( TheGlobalData->isClassicMouse() )
			{
				Coord3D cameraAtRelease;
				TheTacticalView->getPosition( &cameraAtRelease );
				const ICoord2D release = msg->getArgument( 0 )->pixel;
				if( !TheMouse->isClick( &m_deselectFeedbackAnchor, &release,
																&m_deselectDownCameraPosition, &cameraAtRelease,
																m_lastClick, (UnsignedInt) msg->getArgument( 2 )->integer ) )
					break;

				if( TheInGameUI->getGUICommand() )
				{
					TheInGameUI->setGUICommand( NULL );
					disp = DESTROY_MESSAGE;
					TheInGameUI->setScrolling( FALSE );
				}
				else if( TheGlobalData->leftButtonOrders() || TheInGameUI->getPendingPlaceSourceObjectID() != INVALID_ID )
				{
					deselectAll();
				}
				break;
			}

			// a right drag panned the camera, and a pan takes nothing back: a player can look around
			// with a structure on the cursor or a GUI command waiting and still have it on release
			if( TheLookAtTranslator->isRightDragPanning() )
				break;

			//
			// The right button drops whatever is armed, and it does so on a release that wobbled as
			// well as on a strict click.  A dozer placing a row of structures with shift held is the
			// case that made this matter: the player right-clicks to send it somewhere, the cursor
			// travels a few pixels while the button is down, and the ghost used to stay riding the
			// cursor because the cancel only ran on a strict click.
			//
			// Nothing here eats the message.  The order itself arrives as the separate
			// MSG_MOUSE_RIGHT_CLICK the meta translator inserts, and LookAtXlat still needs this
			// release to end a pan.
			//
			if( TheInGameUI->getGUICommand() )
			{
				//Cancel GUI command mode... don't deselect units.
				TheInGameUI->setGUICommand( NULL );
				TheInGameUI->setScrolling( FALSE );
			}
			else if( TheInGameUI->getPendingPlaceType() != NULL )
			{
				//Cancel the building placement, and keep the builder selected: you almost always
				//want to place something else with it, and reselecting it costs a click.
				TheInGameUI->placeBuildAvailable( NULL, NULL );
				TheInGameUI->setScrolling( FALSE );
			}
			// The right button used to deselect here in the classic mouse mode.  It does not any
			// more: it is the order button, and an order that hits nothing is simply no order.

			break;
		}

		//-----------------------------------------------------------------------------
		case GameMessage::MSG_META_CREATE_TEAM0:
		case GameMessage::MSG_META_CREATE_TEAM1:
		case GameMessage::MSG_META_CREATE_TEAM2:
		case GameMessage::MSG_META_CREATE_TEAM3:
		case GameMessage::MSG_META_CREATE_TEAM4:
		case GameMessage::MSG_META_CREATE_TEAM5:
		case GameMessage::MSG_META_CREATE_TEAM6:
		case GameMessage::MSG_META_CREATE_TEAM7:
		case GameMessage::MSG_META_CREATE_TEAM8:
		case GameMessage::MSG_META_CREATE_TEAM9:
		{
			Int group = t - GameMessage::MSG_META_CREATE_TEAM0;
			if ( isValidHotkeySquadIndex( group ) )
			{
				DEBUG_LOG(("META: create team %d\n",group));
				// Assign selected items to a group
				GameMessage *newmsg = TheMessageStream->appendMessage((GameMessage::Type)(GameMessage::MSG_CREATE_TEAM0 + group));
				m_pendingSquad[ group ].clear();
				Drawable *drawable = TheGameClient->getDrawableList();
				while (drawable != NULL)
				{
					if (drawable->isSelected() && drawable->getObject() && drawable->getObject()->isLocallyControlled())
					{
						newmsg->appendObjectIDArgument(drawable->getObject()->getID());
						m_pendingSquad[ group ].push_back(drawable->getObject()->getID());
					}
					drawable = drawable->getNextDrawable();
				}
				// A game on one machine runs it on the next logic frame, before any select could see the old
				// squad.  One frame over in a network game, for a message the network sends out a frame late:
				// the list stands in for the squad it becomes, so reading it once too often costs nothing.
				m_pendingSquadLands[ group ] = TheNetwork ? (UnsignedInt)TheNetwork->getExecutionFrame() + 1 : 0;
			}
			disp = DESTROY_MESSAGE;
			break;
		}

		//-----------------------------------------------------------------------------
		case GameMessage::MSG_META_SELECT_TEAM0:
		case GameMessage::MSG_META_SELECT_TEAM1:
		case GameMessage::MSG_META_SELECT_TEAM2:
		case GameMessage::MSG_META_SELECT_TEAM3:
		case GameMessage::MSG_META_SELECT_TEAM4:
		case GameMessage::MSG_META_SELECT_TEAM5:
		case GameMessage::MSG_META_SELECT_TEAM6:
		case GameMessage::MSG_META_SELECT_TEAM7:
		case GameMessage::MSG_META_SELECT_TEAM8:
		case GameMessage::MSG_META_SELECT_TEAM9:
		{
			Int group = t - GameMessage::MSG_META_SELECT_TEAM0;

			//
			// while the general's promotion screen is open the number keys belong to it: 1 to 5 are
			// its five columns, and the science a key would buy wears that number in its corner.
			// A screen that fills the middle of the display and answers nothing on the keyboard is
			// the thing being fixed, so group selection gives way for as long as it is up.
			//
			if( TheControlBar && TheControlBar->isPurchaseScienceVisible()
					&& group >= 1 && group <= PURCHASE_SCIENCE_COLUMNS )
			{
				TheControlBar->pressPurchaseScienceColumn( group - 1 );
				disp = DESTROY_MESSAGE;
				break;
			}

			if ( isValidHotkeySquadIndex( group ) )
			{
				DEBUG_LOG(("META: select team %d\n",group));
				GroupPanel_squadChosen( group );	// the group panel's build tray opens for a squad picked by its number

				UnsignedInt now = TheGameLogic->getFrame();
				if ( m_lastGroupSelTime == 0 )
				{
					m_lastGroupSelTime = now;
				}

				// check for double-press to jump view
				if ( now - m_lastGroupSelTime < ConvertRetailFrames( 20U ) && group == m_lastGroupSelGroup )
				{
					DEBUG_LOG(("META: DOUBLETAP select team %d\n",group));
					Player *player = ThePlayerList->getLocalPlayer();
					if (player)
					{
						Squad *selectedSquad = player->getHotkeySquad(group);
						if (selectedSquad != NULL)
						{
							VecObjectPtr objlist = selectedSquad->getLiveObjects();
							Int numObjs = objlist.size();
							if (numObjs > 0)
							{
								// if theres someone in the group, center the camera on them.
								TheTacticalView->lookAt( objlist[numObjs-1]->getDrawable()->getPosition() );
							}
						}
					}
				} 
				else 
				{
					TheInGameUI->deselectAllDrawables(); //The clear is dropped in the stream, the new group follows it

					// no need to send two messages for selecting the same group.
					TheMessageStream->appendMessage((GameMessage::Type)(GameMessage::MSG_SELECT_TEAM0 + group));
					Player *player = ThePlayerList->getLocalPlayer();
					if (player)
					{
						selectHotkeySquad( player, group, TRUE );
					}
				}
				m_lastGroupSelTime = now;
				m_lastGroupSelGroup = group;
			}
			disp = DESTROY_MESSAGE;
			break;
		}

		case GameMessage::MSG_META_ADD_TEAM0:
		case GameMessage::MSG_META_ADD_TEAM1:
		case GameMessage::MSG_META_ADD_TEAM2:
		case GameMessage::MSG_META_ADD_TEAM3:
		case GameMessage::MSG_META_ADD_TEAM4:
		case GameMessage::MSG_META_ADD_TEAM5:
		case GameMessage::MSG_META_ADD_TEAM6:
		case GameMessage::MSG_META_ADD_TEAM7:
		case GameMessage::MSG_META_ADD_TEAM8:
		case GameMessage::MSG_META_ADD_TEAM9:
		{
			// Shift and a number puts the selection into that squad and keeps what the squad already
			// held.  Retail used the key the other way round, to add the squad to the selection.  The
			// logic only fills a squad from scratch, so the squad goes out again whole as a create: its
			// members as the logic will hold them when this lands, then the selection after them.
			Int group = t - GameMessage::MSG_META_ADD_TEAM0;

			if ( isValidHotkeySquadIndex( group ) )
			{
				std::vector<ObjectID> members;
				if ( isSquadPending( group ) )
				{
					members = m_pendingSquad[ group ];
				}
				else
				{
					// every member, not just the selectable ones: a unit riding in a transport stays in the squad
					const VecObjectPtr &objlist = ThePlayerList->getLocalPlayer()->getHotkeySquad( group )->getAllObjects();
					for ( size_t i = 0; i < objlist.size(); ++i )
						members.push_back( objlist[ i ]->getID() );
				}

				std::vector<ObjectID> squad;
				for ( size_t i = 0; i < members.size(); ++i )
				{
					if ( !isTakenByLaterSquad( members[ i ], group ) )
						squad.push_back( members[ i ] );
				}

				Bool added = FALSE;
				for ( Drawable *drawable = TheGameClient->getDrawableList(); drawable != NULL; drawable = drawable->getNextDrawable() )
				{
					Object *obj = drawable->getObject();
					if ( drawable->isSelected() && obj && obj->isLocallyControlled()
							&& std::find( squad.begin(), squad.end(), obj->getID() ) == squad.end() )
					{
						squad.push_back( obj->getID() );
						added = TRUE;
					}
				}

				if ( added )
				{
					DEBUG_LOG(("META: add selection to team %d\n",group));
					GameMessage *newmsg = TheMessageStream->appendMessage((GameMessage::Type)(GameMessage::MSG_CREATE_TEAM0 + group));
					for ( size_t i = 0; i < squad.size(); ++i )
						newmsg->appendObjectIDArgument( squad[ i ] );
					m_pendingSquad[ group ] = squad;
					m_pendingSquadLands[ group ] = TheNetwork ? (UnsignedInt)TheNetwork->getExecutionFrame() + 1 : 0;
				}
			}
			disp = DESTROY_MESSAGE;
			break;
		}

		//-----------------------------------------------------------------------------
		case GameMessage::MSG_META_VIEW_TEAM0:
		case GameMessage::MSG_META_VIEW_TEAM1:
		case GameMessage::MSG_META_VIEW_TEAM2:
		case GameMessage::MSG_META_VIEW_TEAM3:
		case GameMessage::MSG_META_VIEW_TEAM4:
		case GameMessage::MSG_META_VIEW_TEAM5:
		case GameMessage::MSG_META_VIEW_TEAM6:
		case GameMessage::MSG_META_VIEW_TEAM7:
		case GameMessage::MSG_META_VIEW_TEAM8:
		case GameMessage::MSG_META_VIEW_TEAM9:
		{
			Int group = t - GameMessage::MSG_META_VIEW_TEAM0;
			if ( isValidHotkeySquadIndex( group ) )
			{
				DEBUG_LOG(("META: view team %d\n",group));
				Player *player = ThePlayerList->getLocalPlayer();
				if (player) 
				{
					Squad *selectedSquad = player->getHotkeySquad(group);
					if (selectedSquad != NULL) 
					{
						VecObjectPtr objlist = selectedSquad->getLiveObjects();
						Int numObjs = objlist.size();
						if (numObjs > 0) 
						{
							// if theres someone in the group, center the camera on them.
							TheTacticalView->lookAt( objlist[ numObjs-1 ]->getDrawable()->getPosition() );
						}
					}
				}
			}
			disp = DESTROY_MESSAGE;
			break;
		}
		
		//-----------------------------------------------------------------------------------------
		case GameMessage::MSG_META_OPTIONS:
		{
			// stop drawing selection feedback, as we're going to ignore the selection.
			m_leftMouseButtonIsDown = FALSE;
			// let this message drop through, the commandXLat will show the options screen itself.
			break;
		}


#if defined(_DEBUG) || defined(_INTERNAL) 
		//-----------------------------------------------------------------------------------------
		case GameMessage::MSG_META_DEMO_TOGGLE_HAND_OF_GOD_MODE:
		{
			if ( !TheGameLogic->isInMultiplayerGame() )
			{
				m_HandOfGodSelectionMode = !m_HandOfGodSelectionMode;
				TheInGameUI->message( UnicodeString( u"Meta Hand-Of-God Mode is %s" ), m_HandOfGodSelectionMode ? u"ON" : u"OFF" );
				disp = DESTROY_MESSAGE;
			}
			break;
		}
#endif

#if defined(_ALLOW_DEBUG_CHEATS_IN_RELEASE)
		//-----------------------------------------------------------------------------------------
		case GameMessage::MSG_CHEAT_TOGGLE_HAND_OF_GOD_MODE://NOTICE THE DIFFERENT NAME!!!!!!!!!!!!!!!!!!!!!!!!!!ML
		{
			if ( !TheGameLogic->isInMultiplayerGame() )
			{
				m_HandOfGodSelectionMode = !m_HandOfGodSelectionMode;
				TheInGameUI->message( UnicodeString( u"Hand-Of-God Mode is %s" ), m_HandOfGodSelectionMode ? u"ON" : u"OFF" );
				disp = DESTROY_MESSAGE;
			}
			break;
		}
#endif

#if defined(_DEBUG) || defined(_INTERNAL)
		//-----------------------------------------------------------------------------------------
		case GameMessage::MSG_META_DEMO_TOGGLE_HURT_ME_MODE:
		{
			if ( !TheGameLogic->isInMultiplayerGame() )
			{
				TheHurtSelectionMode = !TheHurtSelectionMode;
				TheInGameUI->message( UnicodeString( u"Hurt-Me Mode is %s" ), TheHurtSelectionMode ? u"ON" : u"OFF" );
				disp = DESTROY_MESSAGE;
			}
			break;
		}
#endif

#if defined(_DEBUG) || defined(_INTERNAL)
		//-----------------------------------------------------------------------------------------
		case GameMessage::MSG_META_DEMO_DEBUG_SELECTION:
		{
			TheDebugSelectionMode = !TheDebugSelectionMode;
			TheInGameUI->message( UnicodeString( u"Debug-Selected-Item Mode is %s" ), TheDebugSelectionMode ? u"ON" : u"OFF" );
		#ifdef DEBUG_OBJECT_ID_EXISTS
			TheObjectIDToDebug = INVALID_ID;
		#endif
			disp = DESTROY_MESSAGE;
			break;
		}
#endif
	}

	return disp;
}


//Added By Sadullah Nader

//setDragSelecting(Bool dragSelect)
//Added to fix the drag selection problem in control bar
////////////////////////////////////////////////////////////////////////
void SelectionTranslator::setDragSelecting(Bool dragSelect)
{
	m_dragSelecting = dragSelect; 
}

//setLeftMouseButton(Bool state)
//Added to turn of Left button down when left button goes up
////////////////////////////////////////////////////////////////////////
void SelectionTranslator::setLeftMouseButton(Bool state)
{
	m_leftMouseButtonIsDown = state;
}

//-----------------------------------------------------------------------------
void SelectionTranslator::forgetPendingSquads()
{
	for( Int group = 0; group < NUM_HOTKEY_SQUADS; ++group )
	{
		m_pendingSquad[ group ].clear();
		m_pendingSquadLands[ group ] = 0;
	}
}

//-----------------------------------------------------------------------------
Bool SelectionTranslator::isSquadPending( Int group ) const
{
	return m_pendingSquadLands[ group ] != 0 && TheGameLogic->getFrame() <= m_pendingSquadLands[ group ];
}

//-----------------------------------------------------------------------------
/** A team key on the way takes this unit into another squad after this squad's own key lands, and
	* the logic removes a unit from every squad but its new one. */
//-----------------------------------------------------------------------------
Bool SelectionTranslator::isTakenByLaterSquad( ObjectID id, Int group ) const
{
	const UnsignedInt groupLands = isSquadPending( group ) ? m_pendingSquadLands[ group ] : 0;
	for( Int other = 0; other < NUM_HOTKEY_SQUADS; ++other )
	{
		if( other == group || !isSquadPending( other ) || m_pendingSquadLands[ other ] <= groupLands )
			continue;
		const std::vector<ObjectID> &sent = m_pendingSquad[ other ];
		if( std::find( sent.begin(), sent.end(), id ) != sent.end() )
			return TRUE;
	}
	return FALSE;
}

//-----------------------------------------------------------------------------
/** Select on screen what the logic will hold in this squad by the time a select sent now lands:
	* the members a team key sent for it if they are still on the way, and never a unit that a key
	* on the way takes into another squad. */
//-----------------------------------------------------------------------------
void SelectionTranslator::selectHotkeySquad( Player *player, Int group, Bool ownOnly )
{
	VecObjectPtr objlist;
	if( isSquadPending( group ) )
	{
		for( size_t i = 0; i < m_pendingSquad[ group ].size(); ++i )
		{
			Object *obj = TheGameLogic->findObjectByID( m_pendingSquad[ group ][ i ] );
			if( obj && obj->isSelectable() )
				objlist.push_back( obj );
		}
	}
	else
	{
		Squad *selectedSquad = player->getHotkeySquad( group );
		if( selectedSquad == NULL )
			return;
		objlist = selectedSquad->getLiveObjects();
	}

	for( size_t i = 0; i < objlist.size(); ++i )
	{
		Object *obj = objlist[ i ];
		if( ownOnly && obj->getControllingPlayer() != player )
			continue;

		if( !isTakenByLaterSquad( obj->getID(), group ) )
			TheInGameUI->selectDrawable( obj->getDrawable() );
	}
}
