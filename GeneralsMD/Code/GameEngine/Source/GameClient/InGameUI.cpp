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

// InGameUI.cpp ///////////////////////////////////////////////////////////////////////////////////
// Implementation of in-game user interface singleton inteface
// Author: Michael S. Booth, March 2001
///////////////////////////////////////////////////////////////////////////////////////////////////

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#define DEFINE_SHADOW_NAMES

#include "Common/ActionManager.h"
#include "Common/DrawnPath.h"
#include "Common/GameAudio.h"
#include "Common/GameEngine.h"
#include "Common/GameState.h"
#include "Common/RandomValue.h"
#include "Common/GameType.h"
#include "Common/MessageStream.h"
#include "Common/PerfTimer.h"
#include "Common/Player.h"
#include "Common/PlayerTemplate.h"
#include "Common/Science.h"
#include "Common/Upgrade.h"
#include "Common/PlayerList.h"
#include "Common/Radar.h"
#include "Common/Team.h"
#include "Common/ThingFactory.h"
#include "Common/ThingTemplate.h"
#include "Common/TunnelTracker.h"
#include "Common/BuildAssistant.h"
#include "Common/Recorder.h"
#include "Common/BuildAssistant.h"
#include "Common/SpecialPower.h"

#include "GameClient/Anim2D.h"
#include "GameClient/ControlBar.h"
#include "GameClient/ControlBarScheme.h"
#include "GameClient/MetaEvent.h"
#include "GameClient/DisplayStringManager.h"
#include "GameClient/Diplomacy.h"
#include "GameClient/Eva.h"
#include "GameClient/GameText.h"
#include "Common/UserPreferences.h"
#include "Common/FileSystem.h"
#include "Common/file.h"
#include "GameNetwork/GameSpy/ThreadUtils.h"
#include "GameClient/HtmlOverlay.h"
#include "GameClient/GameWindowManager.h"
#include "GameClient/Drawable.h"
#include "GameClient/GadgetPushButton.h"
#include "GameClient/GameClient.h"
#include "GameClient/GameWindowGlobal.h"
#include "GameClient/GameWindowID.h"
#include "GameClient/GUICallbacks.h"
#include "GameClient/Image.h"
#include "GameClient/InGameUI.h"
#include "GameClient/PlayerColorScheme.h"
#include "GameClient/VideoPlayer.h"
#include "GameClient/Mouse.h"
#include "GameClient/ObserverCamera.h"
#include "GameClient/Keyboard.h"
#include "GameClient/GadgetStaticText.h"
#include "GameClient/View.h"
#include "GameClient/TerrainVisual.h"	
#include "GameClient/CinemaDirector.h"
#include "GameClient/ControlBar.h"
#include "GameClient/Display.h"
#include "GameClient/WindowLayout.h"
#include "GameClient/LookAtXlat.h"
#include "GameClient/ParticleSys.h"
#include "GameClient/SelectionXlat.h"
#include "GameClient/Shadow.h"
#include "GameClient/GlobalLanguage.h"

#include "GameNetwork/NetworkInterface.h"

#include "GameLogic/AI.h"
#include "GameLogic/AIGuard.h"
#include "GameLogic/VictoryConditions.h"
#include "GameNetwork/GameInfo.h"
#include "GameLogic/AIPathfind.h"
#include "GameLogic/AIStateMachine.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/JetAIUpdate.h"
#include "GameLogic/Weapon.h"
#include "GameLogic/Object.h"
#include "GameLogic/RankInfo.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/PartitionManager.h"
#include "GameLogic/PolygonTrigger.h"
#include "GameLogic/TerrainLogic.h"
#include "GameLogic/ScriptEngine.h"
#include "GameLogic/Module/ContainModule.h"
#include "GameLogic/Module/ProductionUpdate.h"
#include "GameLogic/Module/SpecialPowerModule.h"
#include "GameLogic/Module/StealthUpdate.h"
#include "GameLogic/Module/SupplyWarehouseDockUpdate.h"
#include "GameLogic/Module/MobMemberSlavedUpdate.h"//ML
#include "GameLogic/Module/SpawnBehavior.h"

#include "Common/UnitTimings.h" //Contains the DO_UNIT_TIMINGS define jba.		 

#ifdef _INTERNAL
// for occasional debugging...
//#pragma optimize("", off)
//#pragma MESSAGE("************************************** WARNING, optimization disabled for debugging purposes")
#endif


// ------------------------------------------------------------------------------------------------
/** How the structure riding the cursor is drawn. `BuildPlacementOpacity` and
	* `BuildPlacementShadows` in GameData.ini; the defaults are what the game always did. */
static void dressPlacementPreview( Drawable *draw )
{
	if( draw == NULL )
		return;

	draw->setDrawableOpacity( TheGlobalData->m_buildPlacementOpacity );
	draw->setShadowsEnabled( TheGlobalData->m_buildPlacementShadows );
}
static const RGBColor illegalBuildColor = { 1.0, 0.0, 0.0 };

// ------------------------------------------------------------------------------------------------
// Layout of the production strip; drawProductionStrip() further down is where it is all used.
// ------------------------------------------------------------------------------------------------
enum
{
	PRODUCTION_STRIP_CAMEO	= 16,		///< a cameo edge to hit-test against before the first draw has
																	///  measured the real one off the general's power bar
																	///  (the tray a queue cameo stands in is up in InGameUI.h)

	PRODUCTION_STRIP_GAP		= 2,		///< space between cameos, and between the two rows
	PRODUCTION_STRIP_LEFT		= 8,		///< inset from the left edge of the screen
	PRODUCTION_STRIP_LIFT		= 24,		///< clearance above the control bar
	PRODUCTION_STRIP_MORE		= 18,		///< width kept for the "+N" that closes an overflowing row
	PRODUCTION_STRIP_SECS		= 7,		///< point size of the countdown written inside a cameo

	STRIP_TRAY_OVERLAP_PARTS	= 8,	///< a strip's trays close up by one part in this many of a tray

	BLIND_SPOT_RAYS						= 180,	///< rays round a placed defence looking for the buildings it cannot shoot past

	REACH_OUTLINE_SEGMENTS		= 256,	///< straight pieces round one reach circle
	REACH_CROSSING_HALVINGS		= 8,		///< halvings that find where one circle's outline enters another
	REACH_OUTLINE_ALPHA				= 230,	///< the outline in the owner's colour, a touch see-through
	ELEVATED_REACH_PASSES			= 3			///< where the ground under the edge is read moves with the edge
};

static const Real REACH_OUTLINE_WIDTH = 1.0f;

static const Real BLIND_SPOT_TARGET_HEIGHT = 10.0f;	///< top of a tank, the height a defence has to see over a hill
static const Real LOS_TERRAIN_SLOP = 0.5f;					///< the terrain line-of-sight test's own fudge
static const Real BLIND_SPOT_RING_WIDTH = PATHFIND_CELL_SIZE_F * 0.5f;	///< two looks a pathfind cell, so a corner is not stepped over

//-------------------------------------------------------------------------------------------------
/** Every number above is an 800x600 one, the resolution the command bar right below the strip was
	* drawn at, so every one of them goes through here to become pixels.
	*
	* The scale is the command bar's own - one factor for both axes - and not the display over 800
	* wide.  The two are the same at 4:3 and a third apart at 16:9, and the strip stands on the bar:
	* measured the wide way its gaps grew away from the trays they space out. */
//-------------------------------------------------------------------------------------------------
static Int stripPixels( Int nominal )
{
	return REAL_TO_INT_CEIL( nominal * ControlBarUniformScale() );
}

//-------------------------------------------------------------------------------------------------
/** A countdown written inside a cameo, always in bare seconds with the unit on it: "45s", "200s".
	* A tank takes seconds and a superweapon charges for minutes, and both are read against every
	* other countdown on the screen, all of which are in seconds - m:ss was a number you had to
	* convert first. The trailing s is what stops a lone "45" reading as a count of something. */
//-------------------------------------------------------------------------------------------------
static void formatStripSeconds( UnicodeString *text, Int seconds )
{
	if( seconds < 0 )
		seconds = 0;

	text->format( L"%ds", seconds );
}

//-------------------------------------------------------------------------------------------------
/** The cameo a special power is fired from.  A SpecialPowerTemplate carries no art of its own -
	* the picture lives on whichever command button launches it - so the button list is what has to
	* be asked.  A power with no button anywhere (a scripted one, a mod's) draws as an empty box
	* with its countdown in it, which is still the timer it was asked for. */
//-------------------------------------------------------------------------------------------------
static const CommandButton *powerButton( const SpecialPowerTemplate *powerTemplate )
{
	if( powerTemplate == NULL || TheControlBar == NULL )
		return NULL;

	for( const CommandButton *button = TheControlBar->getCommandButtons(); button;
			 button = button->getNext() )
	{
		if( button->getSpecialPowerTemplate() == powerTemplate && button->getButtonImage() )
			return button;
	}

	return NULL;
}

static const Image *superweaponCameo( const SpecialPowerTemplate *powerTemplate )
{
	const CommandButton *button = powerButton( powerTemplate );
	return button ? button->getButtonImage() : NULL;
}

/** The promotion screen's button that buys a science, the picture a bought promotion goes by. */
static const CommandButton *scienceButton( ScienceType science )
{
	for( const CommandButton *button = TheControlBar->getCommandButtons(); button; button = button->getNext() )
	{
		const ScienceVec &sciences = button->getScienceVec();
		if( button->getButtonImage() && std::find( sciences.begin(), sciences.end(), science ) != sciences.end() )
			return button;
	}
	return NULL;
}

//-------------------------------------------------------------------------------------------------
/** Pointer to show while a structure rides the cursor.  Mouse::BUILD_PLACEMENT and
	* INVALID_BUILD_PLACEMENT are two names in CursorININames[] that no shipped Mouse.ini ever
	* defines, so asking for them left m_cursorInfo empty and drew nothing at all - the pointer
	* simply vanished for as long as something was being placed.  Honour the build cursors when art
	* for them does exist (a mod may add it), and otherwise fall back to cursors that are always
	* there: the crosshair for a spot that can be built on, the no-go pointer for one that cannot. */
//-------------------------------------------------------------------------------------------------
static Mouse::MouseCursor placementCursor( Bool legal )
{
	Mouse::MouseCursor wanted = legal ? Mouse::BUILD_PLACEMENT : Mouse::INVALID_BUILD_PLACEMENT;

	if( TheMouse->m_cursorInfo[ wanted ].cursorName.isEmpty() == FALSE )
		return wanted;

	return legal ? Mouse::CROSS : Mouse::GENERIC_INVALID;

}  // end placementCursor

//-------------------------------------------------------------------------------------------------
/// The InGameUI singleton instance.
InGameUI *TheInGameUI = NULL;

GameWindow *m_replayWindow = NULL;

// ------------------------------------------------------------------------------------------------
struct KindOfSelectionData
{
	KindOfMaskType m_mustbeSet;
	KindOfMaskType m_mustbeClear;

	DrawableList newlySelectedDrawables;
};
// ------------------------------------------------------------------------------------------------
static Bool kindOfUnitSelection( Drawable *test, void *userData )
{
	KindOfSelectionData *data = (KindOfSelectionData *) userData;

	if( test )
	{
		const Object *object = test->getObject();
		// Only things with objects can be selected, and the code below isn't 
		// safe unless you've verified that there is a valid object.
		if (!object)
			return FALSE;

		Bool isKindOfMatch = object->isKindOfMulti(data->m_mustbeSet, data->m_mustbeClear);

		// only select objects if not already selected
		if( object && isKindOfMatch 
					&& object->isLocallyControlled() 
					&& !object->isContained() 
					&& !object->getDrawable()->isSelected() 
					&& !object->isEffectivelyDead()
					&& object->isMassSelectable()
					&& !object->isOffMap()
				)
		{
			// enforce optional unit cap
			if (TheInGameUI->getMaxSelectCount() > 0 && TheInGameUI->getSelectCount() >= TheInGameUI->getMaxSelectCount())
			{
				if ( !TheInGameUI->getDisplayedMaxWarning() )
				{
					TheInGameUI->setDisplayedMaxWarning( TRUE );
					UnicodeString msg;
					msg.format(TheGameText->fetch("GUI:MaxSelectionSize").str(), TheInGameUI->getMaxSelectCount());
					TheInGameUI->message(msg);
				}
			}
			else
			{
				TheInGameUI->selectDrawable( test );
				TheInGameUI->setDisplayedMaxWarning( FALSE );
				data->newlySelectedDrawables.push_back(test);
				return TRUE;
			}	
		}
	}
	return FALSE;
}

// ------------------------------------------------------------------------------------------------
struct MatchingUnitSelectionData
{
	const ThingTemplate *templateToSelect;
	DrawableList newlySelectedDrawables;
	Bool isCarBomb;
};
// ------------------------------------------------------------------------------------------------
static Bool similarUnitSelection( Drawable *test, void *userData )
{
	MatchingUnitSelectionData *data = (MatchingUnitSelectionData *) userData;
	const ThingTemplate *selectedType = data->templateToSelect;

	if( test )
	{
		const Object *object = test->getObject();
		// Only things with objects can be selected, and the code below isn't 
		// safe unless you've verified that there is a valid object.
		if (!object)
			return FALSE;

		Bool isEquivalent = object->getTemplate()->isEquivalentTo( selectedType );
		if( data->isCarBomb && !isEquivalent && object->testStatus( OBJECT_STATUS_IS_CARBOMB ) )
		{
			isEquivalent = TRUE;
		}

		// only select objects if not already selected
		if( object && isEquivalent 
			  && object->isLocallyControlled() 
				&& !object->isContained()
				&& !( object->getDrawable()->isSelected() ) 
				&& object->isMassSelectable() // And only if they can be multiply selected. (otherwise the drawable will be, but the object will not be)
				&& !object->isOffMap()
				)
		{
			// enforce optional unit cap
			if (TheInGameUI->getMaxSelectCount() > 0 && TheInGameUI->getSelectCount() >= TheInGameUI->getMaxSelectCount())
			{
				if ( !TheInGameUI->getDisplayedMaxWarning() )
				{
					TheInGameUI->setDisplayedMaxWarning( TRUE );
					UnicodeString msg;
					msg.format(TheGameText->fetch("GUI:MaxSelectionSize").str(), TheInGameUI->getMaxSelectCount());
					TheInGameUI->message(msg);
				}
			}
			else
			{
				TheInGameUI->selectDrawable( test );
				TheInGameUI->setDisplayedMaxWarning( FALSE );
				data->newlySelectedDrawables.push_back(test);
				return TRUE;
			}	
		}
	}
	return FALSE;
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void showReplayControls( void )
{
	if (m_replayWindow)
	{
		Bool show = TheGameLogic->isInReplayGame();
		m_replayWindow->winHide(!show);
	}
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void hideReplayControls( void )
{
	if (m_replayWindow)
	{
		m_replayWindow->winHide(TRUE);
	}
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void toggleReplayControls( void )
{
	if (m_replayWindow)
	{
		Bool show = TheGameLogic->isInReplayGame() && m_replayWindow->winIsHidden();
		m_replayWindow->winHide(!show);
	}
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
SuperweaponInfo::SuperweaponInfo(
	ObjectID id,
	UnsignedInt timestamp,
	Bool hiddenByScript,
	Bool hiddenByScience,
	Bool ready,
  Bool evaReadyPlayed,
	const AsciiString& superweaponNormalFont, 
	Int superweaponNormalPointSize, 
	Bool superweaponNormalBold,
	Color c, 
	const SpecialPowerTemplate* spt
) :
	m_id(id),
	m_timestamp(timestamp),
	m_hiddenByScript(hiddenByScript),
	m_hiddenByScience(hiddenByScience),
	m_ready(ready),
  m_evaReadyPlayed( evaReadyPlayed ),
	m_forceUpdateText(false),
	m_nameDisplayString(NULL),
	m_timeDisplayString(NULL),
	m_color(c),
	m_powerTemplate(spt)
{
	m_nameDisplayString = TheDisplayStringManager->newDisplayString();
	m_nameDisplayString->reset();
	m_nameDisplayString->setText( UnicodeString::TheEmptyString );

	m_timeDisplayString = TheDisplayStringManager->newDisplayString();
	m_timeDisplayString->reset();
	m_timeDisplayString->setText( UnicodeString::TheEmptyString );

	setFont( superweaponNormalFont, superweaponNormalPointSize, superweaponNormalBold );
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
SuperweaponInfo::~SuperweaponInfo()
{
	if (m_nameDisplayString)
		TheDisplayStringManager->freeDisplayString( m_nameDisplayString );
	m_nameDisplayString = NULL;

	if (m_timeDisplayString)
		TheDisplayStringManager->freeDisplayString( m_timeDisplayString );
	m_timeDisplayString = NULL;
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void SuperweaponInfo::setFont(const AsciiString& superweaponNormalFont, Int superweaponNormalPointSize, Bool superweaponNormalBold)
{
	m_nameDisplayString->setFont( TheFontLibrary->getFont( superweaponNormalFont, 
		TheGlobalLanguageData->adjustFontSize(superweaponNormalPointSize), superweaponNormalBold ) );
	m_timeDisplayString->setFont( TheFontLibrary->getFont( superweaponNormalFont, 
		TheGlobalLanguageData->adjustFontSize(superweaponNormalPointSize), superweaponNormalBold ) );
}

// ------------------------------------------------------------------------------------------------
void SuperweaponInfo::setText(const UnicodeString& name, const UnicodeString& time)
{
	m_nameDisplayString->setText(name);
	m_timeDisplayString->setText(time);
}

// ------------------------------------------------------------------------------------------------
/** The countdowns sit straight on the battlefield, where light terrain swallows them. Put a
	* translucent plate under the whole line - the name is right-aligned to x, the time starts at
	* it - before either half is drawn. */
// ------------------------------------------------------------------------------------------------
void SuperweaponInfo::drawBackdrop(Int x, Int y)
{
	if( m_nameDisplayString == NULL || m_timeDisplayString == NULL )
		return;

	Int nameW = m_nameDisplayString->getWidth();
	Int timeW = m_timeDisplayString->getWidth();
	Int h = (Int)getHeight();

	const Int pad = 3;
	TheDisplay->drawFillRect( x - nameW - pad, y - 1, nameW + timeW + pad*2, h + 2,
														GameMakeColor( 0, 0, 0, 130 ) );
}

// ------------------------------------------------------------------------------------------------
void SuperweaponInfo::drawName(Int x, Int y, Color color, Color dropColor)
{
	if (color == 0)
		color = clientColor( m_color );	// the owner's colour was stored when the timer started
 	m_nameDisplayString->draw(x - m_nameDisplayString->getWidth(), y, color, dropColor);
}

// ------------------------------------------------------------------------------------------------
void SuperweaponInfo::drawTime(Int x, Int y, Color color, Color dropColor)
{
	if (color == 0)
		color = clientColor( m_color );
 	m_timeDisplayString->draw(x, y, color, dropColor);
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
Real SuperweaponInfo::getHeight() const
{
	return m_nameDisplayString->getFont()->height;
}

// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void InGameUI::crc( Xfer *xfer )
{

}  // end crc

// ------------------------------------------------------------------------------------------------
/** Xfer method
	* Version Info:
	* 1: Initial version 
	* 2: Save NamedTimers, but not specifically their Info structs.  We'll recreate them.
  * 3: Added m_evaReadyPlayed boolean to transfer
*/
// ------------------------------------------------------------------------------------------------
void InGameUI::xfer( Xfer *xfer )
{
	// version
	const XferVersion currentVersion = 3;
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );

	if( version >= 2 )
	{
		// Saving the named timer infos and their friends so we get script timers back after we load
		xfer->xferInt(&m_namedTimerLastFlashFrame);
		xfer->xferBool(&m_namedTimerUsedFlashColor);
		xfer->xferBool(&m_showNamedTimers);

		// For the timers themselves, all I need to save is the things that are used in the call to addNamedTimer.
		// It is okay to do this, because SuperweaponInfos pushes things on to a map; addNamedTimer is just a more
		// organized way to push things on the namedTimer Map.
		// addNamedTimer needs (const AsciiString& timerName, const UnicodeString& text, Bool isCountdown)
		if (xfer->getXferMode() == XFER_SAVE)
		{
			Int timerCount = m_namedTimers.size();
			xfer->xferInt( &timerCount );
			for( NamedTimerMapIt timerIter = m_namedTimers.begin(); timerIter != m_namedTimers.end(); ++timerIter )
			{
				xfer->xferAsciiString( &(timerIter->second->m_timerName) );
				xfer->xferUnicodeString( &(timerIter->second->timerText) );
				xfer->xferBool( &(timerIter->second->isCountdown) );
			}
		}
		else // iz a Load
		{
			Int timerCount;
			xfer->xferInt( &timerCount );
			for( Int timerIndex = 0; timerIndex < timerCount; ++timerIndex )
			{
				AsciiString timerName;
				UnicodeString timerText;
				Bool isCountdown;
				xfer->xferAsciiString( &timerName );
				xfer->xferUnicodeString( &timerText );
				xfer->xferBool( &isCountdown );

				addNamedTimer( timerName, timerText, isCountdown );
			}
		}
	}

	xfer->xferBool(&m_superweaponHiddenByScript);
	//xfer->xferBool(&m_inputEnabled);	// no, don't save this yet. somewhat problematic.

	if (xfer->getXferMode() == XFER_SAVE)
	{
		for (Int playerIndex = 0; playerIndex < MAX_PLAYER_COUNT; ++playerIndex)
		{
			for (SuperweaponMap::iterator mapIt = m_superweapons[playerIndex].begin(); mapIt != m_superweapons[playerIndex].end(); ++mapIt)
			{
				AsciiString powerName = mapIt->first;
				SuperweaponList& swList = mapIt->second;
				for (SuperweaponList::iterator listIt = swList.begin(); listIt != swList.end(); ++listIt)
				{
					SuperweaponInfo* swInfo = *listIt;

					// since this list tends to be somewhat sparse, we write stuff out pretty explicitly.
					xfer->xferInt(&playerIndex);
					
					AsciiString templateName = swInfo->getSpecialPowerTemplate()->getName();

					xfer->xferAsciiString(&templateName);
					xfer->xferAsciiString(&powerName);
					xfer->xferObjectID(&swInfo->m_id);
					xfer->xferUnsignedInt(&swInfo->m_timestamp);
					xfer->xferBool(&swInfo->m_hiddenByScript);
					xfer->xferBool(&swInfo->m_hiddenByScience);
					xfer->xferBool(&swInfo->m_ready);
          if ( currentVersion >= 3 )
          {
            xfer->xferBool( &swInfo->m_evaReadyPlayed );
          }
				}
			}
		}
		Int noMorePlayers = -1;		// our "done" sentinel
		xfer->xferInt(&noMorePlayers);
	}
	else if (xfer->getXferMode() == XFER_LOAD)
	{
		for (;;)
		{
			Int playerIndex;
			xfer->xferInt(&playerIndex);

			if (playerIndex == -1)
			{
				break;	// our "done" sentinel
			}
			else if (playerIndex < 0 || playerIndex >= MAX_PLAYER_COUNT)
			{
				DEBUG_CRASH(("SWInfo bad plyrindex\n"));
				throw INI_INVALID_DATA;
			}

			AsciiString templateName;
			xfer->xferAsciiString(&templateName);
			const SpecialPowerTemplate* powerTemplate = TheSpecialPowerStore->findSpecialPowerTemplate(templateName);
			if (powerTemplate == NULL)
			{
				DEBUG_CRASH(("power %s not found\n",templateName.str()));
				throw INI_INVALID_DATA;
			}

			AsciiString powerName;
			ObjectID id;
			UnsignedInt timestamp;
			Bool hiddenByScript, hiddenByScience, ready, evaReadyPlayed;

			xfer->xferAsciiString(&powerName);
			xfer->xferObjectID(&id);
			xfer->xferUnsignedInt(&timestamp);
			xfer->xferBool(&hiddenByScript);
			xfer->xferBool(&hiddenByScience);
			xfer->xferBool(&ready);
      if ( currentVersion >= 3 )
      {
        xfer->xferBool( &evaReadyPlayed );
      }
      else
      {
        evaReadyPlayed = ready;
      }

			// srj sez: due to order-of-operation stuff, sometimes these will already exist,
			// sometimes not. not sure why. so handle both cases. 
			SuperweaponInfo* swInfo = findSWInfo(playerIndex, powerName, id, powerTemplate);
			if (swInfo == NULL)
			{
				const Player* player = ThePlayerList->getNthPlayer(playerIndex);
				swInfo = newInstance(SuperweaponInfo)(
					id,
					timestamp,
					hiddenByScript,
					hiddenByScience,
					ready,
          evaReadyPlayed,
					m_superweaponNormalFont, 
					m_superweaponNormalPointSize, 
					m_superweaponNormalBold, 
					player->getPlayerColor(), 
					powerTemplate);
				m_superweapons[playerIndex][powerName].push_back(swInfo);
			}
			else
			{
				// swInfo->m_id = id;	// redundant, already matches
				swInfo->m_timestamp = timestamp;
				swInfo->m_hiddenByScript = hiddenByScript;
				swInfo->m_hiddenByScience = hiddenByScience;
				swInfo->m_ready = ready;
        swInfo->m_evaReadyPlayed = evaReadyPlayed;
			}
			swInfo->m_forceUpdateText = true;
		
		}
	}

}

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void InGameUI::loadPostProcess( void )
{

}  // end loadPostProcess

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void InGameUI::setMouseCursor(Mouse::MouseCursor c)
{
	if (!TheMouse)
		return;

	TheMouse->setCursor(c);

	if (m_mouseMode == MOUSEMODE_GUI_COMMAND && c != Mouse::ARROW && c != Mouse::SCROLL)
		m_mouseModeCursor = c;

}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
SuperweaponInfo* InGameUI::findSWInfo(Int playerIndex, const AsciiString& powerName, ObjectID id, const SpecialPowerTemplate *powerTemplate)
{
	SuperweaponMap::iterator mapIt = m_superweapons[playerIndex].find(powerName);
	if (mapIt != m_superweapons[playerIndex].end())
	{
		for (SuperweaponList::iterator listIt = mapIt->second.begin(); listIt != mapIt->second.end(); ++listIt)
		{
			if ((*listIt)->m_id == id)
			{
				return *listIt;
			}
		}
	}
	return NULL;
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void InGameUI::addSuperweapon(Int playerIndex, const AsciiString& powerName, ObjectID id, const SpecialPowerTemplate *powerTemplate)
{
	if (powerTemplate == NULL)
		return;

	// Pro Rules: a silo nobody may fire gets no countdown on everybody's screen and no "missile ready"
	if (ProRulesRefuseSpecialPower(ThePlayerList->getNthPlayer(playerIndex), powerTemplate->getSpecialPowerType()))
		return;

	// srj sez: don't allow adding the same superweapon more than once. it can happen. not sure how. (srj)
	SuperweaponInfo* swInfo = findSWInfo(playerIndex, powerName, id, powerTemplate);
	if (swInfo != NULL)
		return;

	const Player* player = ThePlayerList->getNthPlayer(playerIndex);
	Bool hiddenByScience = (powerTemplate->getRequiredScience() != SCIENCE_INVALID) && (player->hasScience(powerTemplate->getRequiredScience()) == false);

#ifndef DO_UNIT_TIMINGS
  DEBUG_LOG(("Adding superweapon UI timer\n"));
#endif
	SuperweaponInfo *info = newInstance(SuperweaponInfo)(
					id,
					-1,			// timestamp
					FALSE,	// hiddenByScript
					hiddenByScience,//Aaayeeee! This is meaningless and just clogs up the works, sez srj, nuke or repair or SHIP WITH(tm), ASAP
													// THe trouble is: There is no mechanism to clear this bit when the science is granted, thus,
													// the timer never, ever, ever get drawn.... unless the owning object is post-science constructed.
					FALSE,	// ready
          FALSE,  // evaReadyPlayed
					m_superweaponNormalFont, 
					m_superweaponNormalPointSize, 
					m_superweaponNormalBold, 
					player->getPlayerColor(), 
					powerTemplate);

	m_superweapons[playerIndex][powerName].push_back(info);
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
Bool InGameUI::removeSuperweapon(Int playerIndex, const AsciiString& powerName, ObjectID id, const SpecialPowerTemplate *powerTemplate)
{
	DEBUG_LOG(("Removing superweapon UI timer\n"));
	SuperweaponMap::iterator mapIt = m_superweapons[playerIndex].find(powerName);
	if (mapIt != m_superweapons[playerIndex].end())
	{
		SuperweaponList& swList = mapIt->second;
		for (SuperweaponList::iterator listIt = swList.begin(); listIt != swList.end(); ++listIt)
		{
			if ((*listIt)->m_id == id)
			{
				SuperweaponInfo *info = *listIt;
				swList.erase(listIt);
				info->deleteInstance();
				if (swList.size() == 0)
				{
					m_superweapons[playerIndex].erase(mapIt);
				}
				return TRUE;
			}
		}
	}

	return FALSE;
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void InGameUI::objectChangedTeam(const Object *obj, Int oldPlayerIndex, Int newPlayerIndex)
{
	// if we already had it listed, remove and re-add it
	if (obj && oldPlayerIndex >= 0 && newPlayerIndex >= 0)
	{
		ObjectID id = obj->getID();
		AsciiString powerName;
		for (BehaviorModule** m = obj->getBehaviorModules(); *m; ++m)
		{
			SpecialPowerModuleInterface* sp = (*m)->getSpecialPower();
			if (!sp)
				continue;

			const SpecialPowerTemplate *powerTemplate = sp->getSpecialPowerTemplate();
			powerName = powerTemplate->getName();

			SuperweaponMap::iterator mapIt = m_superweapons[oldPlayerIndex].find(powerName);
			Bool found = false;
			if (mapIt != m_superweapons[oldPlayerIndex].end())
			{
				for (SuperweaponList::iterator listIt = mapIt->second.begin(); listIt != mapIt->second.end(); ++listIt)
				{
					if ((*listIt)->m_id == id)
					{
						removeSuperweapon(oldPlayerIndex, powerName, id, powerTemplate);
						addSuperweapon(newPlayerIndex, powerName, id, powerTemplate);
						found = true;
						break;
					}
				}
			}
			if (!found)
			{
				if( TheGameLogic->getFrame() == 0 && !obj->getStatusBits().test( OBJECT_STATUS_UNDER_CONSTRUCTION ) &&
					obj->isKindOf( KINDOF_COMMANDCENTER ) == FALSE )
					addSuperweapon(newPlayerIndex, powerName, id, powerTemplate);
			}
		}
	}
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void InGameUI::hideObjectSuperweaponDisplayByScript(const Object *obj)
{
	ObjectID objID = obj->getID();
	for (Int playerIndex = 0; playerIndex < MAX_PLAYER_COUNT; ++playerIndex)
	{
		for (SuperweaponMap::iterator mapIt = m_superweapons[playerIndex].begin(); mapIt != m_superweapons[playerIndex].end(); ++mapIt)
		{
			for (SuperweaponList::iterator listIt = mapIt->second.begin(); listIt != mapIt->second.end(); ++listIt)
			{
				if ((*listIt)->m_id == objID)
				{
					(*listIt)->m_hiddenByScript = TRUE;
				}
			}
		}
	}
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void InGameUI::showObjectSuperweaponDisplayByScript(const Object *obj)
{
	ObjectID objID = obj->getID();
	for (Int playerIndex = 0; playerIndex < MAX_PLAYER_COUNT; ++playerIndex)
	{
		for (SuperweaponMap::iterator mapIt = m_superweapons[playerIndex].begin(); mapIt != m_superweapons[playerIndex].end(); ++mapIt)
		{
			for (SuperweaponList::iterator listIt = mapIt->second.begin(); listIt != mapIt->second.end(); ++listIt)
			{
				if ((*listIt)->m_id == objID)
				{
					(*listIt)->m_hiddenByScript = FALSE;
				}
			}
		}
	}
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void InGameUI::setSuperweaponDisplayEnabledByScript(Bool enable)
{
	m_superweaponHiddenByScript = !enable;
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
Bool InGameUI::getSuperweaponDisplayEnabledByScript(void) const
{
	// The getter asks whether the display is enabled, so it is the negation of the hidden flag.
	// Nothing calls it yet, which is why the inversion went unnoticed.
	return !m_superweaponHiddenByScript;
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void InGameUI::addNamedTimer( const AsciiString& timerName, const UnicodeString& text, Bool isCountdown )
{
	NamedTimerInfo *info = newInstance( NamedTimerInfo );	
	info->m_timerName = timerName;
	info->color = m_namedTimerNormalColor;
	info->timerText = text;
	info->displayString = TheDisplayStringManager->newDisplayString();
	info->displayString->reset();
	info->displayString->setFont( TheFontLibrary->getFont( m_namedTimerNormalFont, 
		TheGlobalLanguageData->adjustFontSize(m_namedTimerNormalPointSize), m_namedTimerNormalBold ) );
	info->displayString->setText( UnicodeString::TheEmptyString );
	info->timestamp = -1;
	info->isCountdown = isCountdown;

//	GameFont *font = info->displayString->getFont();

	removeNamedTimer(timerName);
	m_namedTimers[timerName] = info;
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void InGameUI::removeNamedTimer( const AsciiString& timerName )
{
	NamedTimerMapIt mapIt = m_namedTimers.find(timerName);
	if (mapIt != m_namedTimers.end())
	{
		TheDisplayStringManager->freeDisplayString( mapIt->second->displayString );
		mapIt->second->deleteInstance();
		m_namedTimers.erase(mapIt);
		return;
	}
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void InGameUI::showNamedTimerDisplay( Bool show )
{
	m_showNamedTimers = show;
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
const FieldParse InGameUI::s_fieldParseTable[] = 
{
	{ "MaxSelectionSize",								INI::parseInt,					NULL,		offsetof( InGameUI, m_maxSelectCount ) },

	{ "MessageColor1",									INI::parseColorInt,			NULL,		offsetof( InGameUI, m_messageColor1 ) },
	{ "MessageColor2",									INI::parseColorInt,			NULL,		offsetof( InGameUI, m_messageColor2 ) },
	{ "MessagePosition",								INI::parseICoord2D,			NULL,		offsetof( InGameUI, m_messagePosition ) },
	{ "MessageFont",										INI::parseAsciiString,	NULL,		offsetof( InGameUI, m_messageFont ) },
	{ "MessagePointSize",								INI::parseInt,					NULL,		offsetof( InGameUI, m_messagePointSize ) },
	{ "MessageBold",										INI::parseBool,					NULL,		offsetof( InGameUI, m_messageBold ) },
	{ "MessageDelayMS",									INI::parseInt,					NULL,		offsetof( InGameUI, m_messageDelayMS ) },

	{ "MilitaryCaptionColor",						INI::parseRGBAColorInt,	NULL,		offsetof( InGameUI, m_militaryCaptionColor ) },
	{ "MilitaryCaptionPosition",				INI::parseICoord2D,			NULL,		offsetof( InGameUI, m_militaryCaptionPosition ) },

	{ "MilitaryCaptionTitleFont",				INI::parseAsciiString,	NULL,		offsetof( InGameUI, m_militaryCaptionTitleFont ) },
	{ "MilitaryCaptionTitlePointSize",	INI::parseInt,					NULL,		offsetof( InGameUI, m_militaryCaptionTitlePointSize ) },
	{ "MilitaryCaptionTitleBold",				INI::parseBool,					NULL,		offsetof( InGameUI, m_militaryCaptionTitleBold ) },

	{ "MilitaryCaptionFont",						INI::parseAsciiString,	NULL,		offsetof( InGameUI, m_militaryCaptionFont ) },
	{ "MilitaryCaptionPointSize",				INI::parseInt,					NULL,		offsetof( InGameUI, m_militaryCaptionPointSize ) },
	{ "MilitaryCaptionBold",						INI::parseBool,					NULL,		offsetof( InGameUI, m_militaryCaptionBold ) },

	{ "MilitaryCaptionRandomizeTyping",	INI::parseBool,					NULL,		offsetof( InGameUI, m_militaryCaptionRandomizeTyping ) },
	{ "MilitaryCaptionSpeed",						INI::parseInt,					NULL,		offsetof( InGameUI, m_militaryCaptionSpeed ) },

	{ "MilitaryCaptionPosition",				INI::parseICoord2D,			NULL,		offsetof( InGameUI, m_militaryCaptionPosition ) },

	{ "SuperweaponCountdownPosition",					INI::parseCoord2D,			NULL,		offsetof( InGameUI, m_superweaponPosition ) },
	{ "SuperweaponCountdownFlashDuration",		INI::parseDurationReal,	NULL,		offsetof( InGameUI, m_superweaponFlashDuration ) },
	{ "SuperweaponCountdownFlashColor",				INI::parseColorInt,			NULL,		offsetof( InGameUI, m_superweaponFlashColor ) },

	{ "SuperweaponCountdownNormalFont",				INI::parseAsciiString,	NULL,		offsetof( InGameUI, m_superweaponNormalFont ) },
	{ "SuperweaponCountdownNormalPointSize",	INI::parseInt,					NULL,		offsetof( InGameUI, m_superweaponNormalPointSize ) },
	{ "SuperweaponCountdownNormalBold",				INI::parseBool,					NULL,		offsetof( InGameUI, m_superweaponNormalBold ) },

	{ "SuperweaponCountdownReadyFont",				INI::parseAsciiString,	NULL,		offsetof( InGameUI, m_superweaponReadyFont ) },
	{ "SuperweaponCountdownReadyPointSize",		INI::parseInt,					NULL,		offsetof( InGameUI, m_superweaponReadyPointSize ) },
	{ "SuperweaponCountdownReadyBold",				INI::parseBool,					NULL,		offsetof( InGameUI, m_superweaponReadyBold ) },

	{ "NamedTimerCountdownPosition",					INI::parseCoord2D,			NULL,		offsetof( InGameUI, m_namedTimerPosition ) },
	{ "NamedTimerCountdownFlashDuration",			INI::parseDurationReal,	NULL,		offsetof( InGameUI, m_namedTimerFlashDuration ) },
	{ "NamedTimerCountdownFlashColor",				INI::parseColorInt,			NULL,		offsetof( InGameUI, m_namedTimerFlashColor ) },

	{ "NamedTimerCountdownNormalFont",				INI::parseAsciiString,	NULL,		offsetof( InGameUI, m_namedTimerNormalFont ) },
	{ "NamedTimerCountdownNormalPointSize",		INI::parseInt,					NULL,		offsetof( InGameUI, m_namedTimerNormalPointSize ) },
	{ "NamedTimerCountdownNormalBold",				INI::parseBool,					NULL,		offsetof( InGameUI, m_namedTimerNormalBold ) },
	{ "NamedTimerCountdownNormalColor",				INI::parseColorInt,			NULL,		offsetof( InGameUI, m_namedTimerNormalColor ) },

	{ "NamedTimerCountdownReadyFont",					INI::parseAsciiString,	NULL,		offsetof( InGameUI, m_namedTimerReadyFont ) },
	{ "NamedTimerCountdownReadyPointSize",		INI::parseInt,					NULL,		offsetof( InGameUI, m_namedTimerReadyPointSize ) },
	{ "NamedTimerCountdownReadyBold",					INI::parseBool,					NULL,		offsetof( InGameUI, m_namedTimerReadyBold ) },
	{ "NamedTimerCountdownReadyColor",				INI::parseColorInt,			NULL,		offsetof( InGameUI, m_namedTimerReadyColor ) },

	{ "FloatingTextTimeOut",									INI::parseDurationUnsignedInt,		NULL,		offsetof( InGameUI, m_floatingTextTimeOut ) },
	{ "FloatingTextMoveUpSpeed",							INI::parseVelocityReal,	NULL,		offsetof( InGameUI, m_floatingTextMoveUpSpeed ) },
	{ "FloatingTextVanishRate",								INI::parseVelocityReal,	NULL,		offsetof( InGameUI, m_floatingTextMoveVanishRate ) },

	{ "PopupMessageColor",								INI::parseColorInt,					NULL,		offsetof( InGameUI, m_popupMessageColor ) },
	
	{ "DrawableCaptionFont",									INI::parseAsciiString,	NULL,		offsetof( InGameUI, m_drawableCaptionFont ) },
	{ "DrawableCaptionPointSize",							INI::parseInt,					NULL,		offsetof( InGameUI, m_drawableCaptionPointSize ) },
	{ "DrawableCaptionBold",									INI::parseBool,					NULL,		offsetof( InGameUI, m_drawableCaptionBold ) },
	{ "DrawableCaptionColor",									INI::parseColorInt,			NULL,		offsetof( InGameUI, m_drawableCaptionColor ) },

	{ "DrawRMBScrollAnchor",									INI::parseBool,					NULL,		offsetof( InGameUI, m_drawRMBScrollAnchor ) },
	{ "MoveRMBScrollAnchor",									INI::parseBool,					NULL,		offsetof( InGameUI, m_moveRMBScrollAnchor ) },

	{ "AttackDamageAreaRadiusCursor", RadiusDecalTemplate::parseRadiusDecalTemplate, NULL, offsetof( InGameUI, m_radiusCursors[RADIUSCURSOR_ATTACK_DAMAGE_AREA] ) },
	{ "AttackScatterAreaRadiusCursor", RadiusDecalTemplate::parseRadiusDecalTemplate, NULL, offsetof( InGameUI, m_radiusCursors[RADIUSCURSOR_ATTACK_SCATTER_AREA] ) },
	{ "AttackContinueAreaRadiusCursor", RadiusDecalTemplate::parseRadiusDecalTemplate, NULL, offsetof( InGameUI, m_radiusCursors[RADIUSCURSOR_ATTACK_CONTINUE_AREA] ) },
	{ "FriendlySpecialPowerRadiusCursor", RadiusDecalTemplate::parseRadiusDecalTemplate, NULL, offsetof( InGameUI, m_radiusCursors[RADIUSCURSOR_FRIENDLY_SPECIALPOWER] ) },
	{ "OffensiveSpecialPowerRadiusCursor", RadiusDecalTemplate::parseRadiusDecalTemplate, NULL, offsetof( InGameUI, m_radiusCursors[RADIUSCURSOR_OFFENSIVE_SPECIALPOWER] ) },
	{ "SuperweaponScatterAreaRadiusCursor", RadiusDecalTemplate::parseRadiusDecalTemplate, NULL, offsetof( InGameUI, m_radiusCursors[RADIUSCURSOR_SUPERWEAPON_SCATTER_AREA] ) },

	{ "GuardAreaRadiusCursor", RadiusDecalTemplate::parseRadiusDecalTemplate, NULL, offsetof( InGameUI, m_radiusCursors[RADIUSCURSOR_GUARD_AREA] ) },
	{ "EmergencyRepairRadiusCursor", RadiusDecalTemplate::parseRadiusDecalTemplate, NULL, offsetof( InGameUI, m_radiusCursors[RADIUSCURSOR_EMERGENCY_REPAIR] ) },

	{ "ParticleCannonRadiusCursor", RadiusDecalTemplate::parseRadiusDecalTemplate, NULL, offsetof( InGameUI, m_radiusCursors[	RADIUSCURSOR_PARTICLECANNON] ) },
	{ "A10StrikeRadiusCursor",			RadiusDecalTemplate::parseRadiusDecalTemplate, NULL, offsetof( InGameUI, m_radiusCursors[	RADIUSCURSOR_A10STRIKE] ) },
	{ "CarpetBombRadiusCursor",			RadiusDecalTemplate::parseRadiusDecalTemplate, NULL, offsetof( InGameUI, m_radiusCursors[	RADIUSCURSOR_CARPETBOMB] ) },
	{ "DaisyCutterRadiusCursor",		RadiusDecalTemplate::parseRadiusDecalTemplate, NULL, offsetof( InGameUI, m_radiusCursors[	RADIUSCURSOR_DAISYCUTTER] ) },
	{ "ParadropRadiusCursor",				RadiusDecalTemplate::parseRadiusDecalTemplate, NULL, offsetof( InGameUI, m_radiusCursors[	RADIUSCURSOR_PARADROP] ) },
	{ "SpySatelliteRadiusCursor",		RadiusDecalTemplate::parseRadiusDecalTemplate, NULL, offsetof( InGameUI, m_radiusCursors[	RADIUSCURSOR_SPYSATELLITE] ) },
	{ "SpectreGunshipRadiusCursor",	RadiusDecalTemplate::parseRadiusDecalTemplate, NULL, offsetof( InGameUI, m_radiusCursors[	RADIUSCURSOR_SPECTREGUNSHIP] ) },
	{ "HelixNapalmBombRadiusCursor",RadiusDecalTemplate::parseRadiusDecalTemplate, NULL, offsetof( InGameUI, m_radiusCursors[	RADIUSCURSOR_HELIX_NAPALM_BOMB] ) },
	
	{ "NuclearMissileRadiusCursor", RadiusDecalTemplate::parseRadiusDecalTemplate, NULL, offsetof( InGameUI, m_radiusCursors[	RADIUSCURSOR_NUCLEARMISSILE] ) }, 
	{ "EMPPulseRadiusCursor",		  	RadiusDecalTemplate::parseRadiusDecalTemplate, NULL, offsetof( InGameUI, m_radiusCursors[	RADIUSCURSOR_EMPPULSE] ) },
	{ "ArtilleryRadiusCursor",		  RadiusDecalTemplate::parseRadiusDecalTemplate, NULL, offsetof( InGameUI, m_radiusCursors[	RADIUSCURSOR_ARTILLERYBARRAGE] ) },
	{ "FrenzyRadiusCursor",				  RadiusDecalTemplate::parseRadiusDecalTemplate, NULL, offsetof( InGameUI, m_radiusCursors[	RADIUSCURSOR_FRENZY] ) },
	{ "NapalmStrikeRadiusCursor",		RadiusDecalTemplate::parseRadiusDecalTemplate, NULL, offsetof( InGameUI, m_radiusCursors[	RADIUSCURSOR_NAPALMSTRIKE] ) },
	{ "ClusterMinesRadiusCursor",		RadiusDecalTemplate::parseRadiusDecalTemplate, NULL, offsetof( InGameUI, m_radiusCursors[	RADIUSCURSOR_CLUSTERMINES] ) },
	
	{ "ScudStormRadiusCursor",			RadiusDecalTemplate::parseRadiusDecalTemplate, NULL, offsetof( InGameUI, m_radiusCursors[	RADIUSCURSOR_SCUDSTORM] ) }, 
	{ "AnthraxBombRadiusCursor",		RadiusDecalTemplate::parseRadiusDecalTemplate, NULL, offsetof( InGameUI, m_radiusCursors[	RADIUSCURSOR_ANTHRAXBOMB] ) },
	{ "AmbushRadiusCursor",					RadiusDecalTemplate::parseRadiusDecalTemplate, NULL, offsetof( InGameUI, m_radiusCursors[	RADIUSCURSOR_AMBUSH] ) }, 
	{ "RadarRadiusCursor",					RadiusDecalTemplate::parseRadiusDecalTemplate, NULL, offsetof( InGameUI, m_radiusCursors[	RADIUSCURSOR_RADAR] ) },
	{ "SpyDroneRadiusCursor",				RadiusDecalTemplate::parseRadiusDecalTemplate, NULL, offsetof( InGameUI, m_radiusCursors[ RADIUSCURSOR_SPYDRONE] ) },

	{ "ClearMinesRadiusCursor",			RadiusDecalTemplate::parseRadiusDecalTemplate, NULL, offsetof( InGameUI, m_radiusCursors[ RADIUSCURSOR_CLEARMINES] ) },
	{ "AmbulanceRadiusCursor",			RadiusDecalTemplate::parseRadiusDecalTemplate, NULL, offsetof( InGameUI, m_radiusCursors[ RADIUSCURSOR_AMBULANCE] ) },

	{ NULL,													NULL,										NULL,		0 }  // keep this last
};

//-------------------------------------------------------------------------------------------------
/** Parse MouseCursor entry */
//-------------------------------------------------------------------------------------------------
void INI::parseInGameUIDefinition( INI* ini )
{
	if( TheInGameUI )
	{
		// parse the ini weapon definition
		ini->initFromINI( TheInGameUI, TheInGameUI->getFieldParse() );
	}
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
InGameUI::InGameUI()
{
	Int i;

	
  m_inputEnabled = true;
	m_isDragSelecting = false;
	m_isFormationDragging = FALSE;
	m_formationDragSpacing = FORMATION_DRAG_MIN_SPACING;
	m_selectCount = 0;
	m_frameSelectionChanged = 0;
  m_duringDoubleClickAttackMoveGuardHintTimer = 0;
  m_duringDoubleClickAttackMoveGuardHintStashedPosition.zero();
	m_maxSelectCount = -1;
	m_isScrolling = FALSE;
	m_isSelecting = FALSE;
	m_mouseMode = MOUSEMODE_DEFAULT;
	m_mouseModeCursor = Mouse::ARROW;
	m_mousedOverDrawableID = INVALID_DRAWABLE_ID;
	
	//Added By Sadullah Nader
	//Initializations missing and needed
	m_currentlyPlayingMovie.clear();
	m_militarySubtitle = NULL;
	m_popupMessageData = NULL;
	m_waypointMode = FALSE;
	m_clientQuiet = FALSE;
	
	m_messageColor1 = GameMakeColor( 255, 255, 255, 255 );
	m_messageColor2 = GameMakeColor( 180, 180, 180, 255 );
	m_messagePosition.x = 10;
	m_messagePosition.y = 10;
	m_messageFont = "Arial";
	m_messagePointSize = 10;
	m_messageBold = FALSE;
	m_messageDelayMS = 5000;

	m_militaryCaptionColor.red   = 200;
	m_militaryCaptionColor.green = 200;
	m_militaryCaptionColor.blue  = 30;
	m_militaryCaptionColor.alpha = 255;
	m_militaryCaptionPosition.x = 10;
	m_militaryCaptionPosition.y = 380;

	m_militaryCaptionTitleFont = "Courier";
	m_militaryCaptionTitlePointSize = 12;
	m_militaryCaptionTitleBold = TRUE;

	m_militaryCaptionFont = "Courier";
	m_militaryCaptionPointSize = 12;
	m_militaryCaptionBold = FALSE;

	m_militaryCaptionRandomizeTyping = FALSE;
	m_militaryCaptionSpeed = 1;
	m_popupMessageColor = GameMakeColor(255,255,255,255);

	m_tooltipsDisabledUntil = 0;

	for( i = 0; i < MAX_BUILD_PROGRESS; i++ )
	{

		m_buildProgress[ i ].m_thingTemplate = NULL;
		m_buildProgress[ i ].m_percentComplete = 0.0f;
		m_buildProgress[ i ].m_control = NULL;

	}  // end for i

	m_pendingGUICommand = NULL;

	// allocate an array for the placement icons
	m_placeIcon = NEW Drawable* [ TheGlobalData->m_maxLineBuildObjects ];
	for( i = 0; i < TheGlobalData->m_maxLineBuildObjects; i++ )
		m_placeIcon[ i ] = NULL;
	m_pendingPlaceType = NULL;
	m_pendingPlaceSourceObjectID = INVALID_ID;
	m_preventLeftClickDeselectionInAlternateMouseModeForOneClick = FALSE;
	m_placeAnchorStart.x = m_placeAnchorStart.y = 0;
	m_placeAnchorEnd.x = m_placeAnchorEnd.y = 0;
	m_placeAnchorInProgress = FALSE;
	m_placeAngleOffset = 0.0f;
	m_placeAngleType = NULL;
	m_placementLegal = TRUE;
	m_placementNudge.zero();

	m_videoStream = NULL;
	m_videoBuffer = NULL;
	m_cameoVideoStream = NULL;
	m_cameoVideoBuffer = NULL;

	m_feedOverlay = NULL;
	m_feedPageLoaded = FALSE;
	m_feedFloor = 0;
	m_queueTrayTop = 0;
	m_armedSignal = SIGNAL_KIND_COUNT;
	m_dozerCheckFrame = 0;
	for( Int index = 0; index < MAX_PLAYER_COUNT; index++ )
		m_hadDozer[ index ] = FALSE;
	m_chatOverlay = NULL;
	m_chatPageLoaded = FALSE;

	m_replayWindow = NULL;
	m_messagesOn = TRUE;

	m_placementRangeRingUp = FALSE;
	m_placementRingRadius = 0.0f;
	forgetPendingPlacements();
	m_hudDisplayString = NULL;
	m_peaceTimeDisplayString = NULL;
	m_peaceTimeLabelDisplayString = NULL;
	m_peaceCountdownDisplayString = NULL;
	m_lastMoneyDisplayed = -1;
	m_hudDrawCount = 0;
	m_hudLastSampleFrame = 0;
	m_hudLastSampleMs = 0;
	m_cameraKeyLastMs = 0;
	m_cameraSnapRepeatMs = 0;
	m_subtitleFreezeStartMs = 0;
	m_subtitleFreezeSteps = 0;
	m_hudLastSampleLogicFrame = 0;
	m_hudRealClockBaseMs = 0;
	m_hudLastDrawMs = 0;
	m_hudOverlayBottom = 0;
	m_productionStripCount = 0;
	m_productionStripTotal = 0;
	m_productionStripCameoW = PRODUCTION_STRIP_CAMEO;
	m_productionStripCameoH = PRODUCTION_STRIP_CAMEO;
	m_productionStripThemed = FALSE;
	m_productionStripStep = 0;
	m_queueOverlay = NULL;
	m_queueFrontOverlay = NULL;
	m_queuePageLoaded = FALSE;
	m_productionStripTray = NULL;
	m_productionStripTraySource = NULL;
	for( Int stripString = 0; stripString < STRIP_OVERFLOW_STRINGS; stripString++ )
		m_productionStripOverflow[ stripString ] = NULL;
	m_spectatorOverlay = NULL;
	m_spectatorPageLoaded = FALSE;
	m_spectatorPageShown = FALSE;
	m_spectatorListsFrame = 0;
	m_spectatorListsWatched = NULL;
	m_scoreboardOpen = FALSE;
	m_scoreboardOverlay = NULL;
	m_scoreboardPageLoaded = FALSE;
	m_scoreboardHtmlFrame = 0;
	m_earnedReadingCount = 0;
	m_controlBarOverlay = NULL;
	m_controlBarPageLoaded = FALSE;
	m_controlBarPageHovered = FALSE;
	m_netOverlay = NULL;
	m_netPageLoaded = FALSE;
	m_promotionOverlay = NULL;
	m_promotionFrontOverlay = NULL;
	for( Int grid = 0; grid < CELL_GRID_COUNT; grid++ )
		m_cellFrontOverlay[ grid ] = NULL;
	m_promotionPageLoaded = FALSE;
	m_promotionShownMs = 0;
	m_promotionDrawnAt = 0;
	m_quitMenuOverlay = NULL;
	m_quitMenuPageLoaded = FALSE;
	m_quitMenuShownMs = 0;
	m_quitMenuDrawnAt = 0;
	m_controlBarPageShown = FALSE;
	m_tooltipOverlay = NULL;
	m_tooltipPageLoaded = FALSE;
	m_tooltipSize.x = 0;
	m_tooltipSize.y = 0;
	m_signalsWereShown = FALSE;
	m_signalsRiseStartMs = 0;
	for( Int stripSeconds = 0; stripSeconds < STRIP_SECONDS_STRINGS; stripSeconds++ )
		m_stripSecondsString[ stripSeconds ] = NULL;
	for( Int stripQuantity = 0; stripQuantity < STRIP_QUANTITY_STRINGS; stripQuantity++ )
		m_stripQuantityString[ stripQuantity ] = NULL;
	m_superweaponIconCount = 0;
	m_superweaponIconTotal = 0;

	m_superweaponPosition.x = 0.7f;
	m_superweaponPosition.y = 0.7f;
	m_superweaponFlashDuration = 1.0f;
	m_superweaponNormalFont = "Arial";
	m_superweaponNormalPointSize = 10;
	m_superweaponNormalBold = FALSE;
	m_superweaponReadyFont = "Arial";
	m_superweaponReadyPointSize = 10;
	m_superweaponReadyBold = FALSE;

	m_superweaponFlashColor = GameMakeColor(255, 255, 255, 255);
	m_superweaponLastFlashFrame = 0;
	m_superweaponUsedFlashColor = TRUE; // so next one is false
	m_superweaponHiddenByScript = FALSE;

	m_namedTimerPosition.x = 0.05f;
	m_namedTimerPosition.y = 0.7f;
	m_namedTimerFlashDuration = 1.0f;
	m_namedTimerNormalFont = "Arial";
	m_namedTimerNormalPointSize = 10;
	m_namedTimerNormalBold = FALSE;
	m_namedTimerReadyFont = "Arial";
	m_namedTimerReadyPointSize = 10;
	m_namedTimerReadyBold = FALSE;


	m_namedTimerNormalColor	= GameMakeColor(255, 255,   0, 255);
	m_namedTimerReadyColor	= GameMakeColor(255,   0, 255, 255);
	m_namedTimerFlashColor	= GameMakeColor(  0, 255, 255, 255);
	m_namedTimerLastFlashFrame = 0;
	m_namedTimerUsedFlashColor = TRUE; // so next one is false
	m_showNamedTimers = TRUE;

	m_floatingTextTimeOut = DEFAULT_FLOATING_TEXT_TIMEOUT;
	m_floatingTextMoveUpSpeed = 1.0f;
	m_floatingTextMoveVanishRate = 0.1f;

	m_drawableCaptionFont = "Arial";
	m_drawableCaptionPointSize = 10;
	m_drawableCaptionBold = FALSE;
	m_drawableCaptionColor = GameMakeColor(255, 255, 255, 255);

	m_drawRMBScrollAnchor = FALSE;
	m_moveRMBScrollAnchor = FALSE;
	m_displayedMaxWarning = FALSE; 

	m_idleWorkerWin = NULL;
	m_currentIdleWorkerDisplay = -1;

	m_waypointMode			= false;
	m_forceAttackMode		= false;
	m_forceMoveToMode		= false;
	m_attackMoveToMode	= false;
	m_forceAttackArmed	= false;
	m_guardArmed				= false;
	m_orderKeyKeptByShift	= false;
	m_preferSelection		= false;
	m_isAttackCircling	= FALSE;
	clearAllyCursors();

	m_curRcType = RADIUSCURSOR_NONE;
	
	m_soloNexusSelectedDrawableID = INVALID_DRAWABLE_ID;

}  // end InGameUI

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
InGameUI::~InGameUI()
{
	delete TheControlBar;
	TheControlBar = NULL;

	// free all the display strings if we're
	removeMilitarySubtitle();

	stopMovie();
	stopCameoMovie();

	// remove any build available status
	placeBuildAvailable( NULL, NULL );
	setRadiusCursorNone();

	// delete the message resources
	freeMessageResources();

	// delete the array for the drawbles
	delete [] m_placeIcon;
	m_placeIcon = NULL;

	// clear floating text
	clearFloatingText();

	// clear world animations
	clearWorldAnimations();
	resetIdleWorker();

	// our own mirrored copy of the power bar's tray - the bar keeps the original
	if( m_productionStripTray )
	{
		m_productionStripTray->deleteInstance();
		m_productionStripTray = NULL;
		m_productionStripTraySource = NULL;
	}

	delete m_spectatorOverlay;
	m_spectatorOverlay = NULL;
	delete m_feedOverlay;
	m_feedOverlay = NULL;
	delete m_chatOverlay;
	m_chatOverlay = NULL;
	delete m_scoreboardOverlay;
	m_scoreboardOverlay = NULL;
	delete m_controlBarOverlay;
	m_controlBarOverlay = NULL;
	delete m_netOverlay;
	m_netOverlay = NULL;
	delete m_promotionOverlay;
	m_promotionOverlay = NULL;
	delete m_promotionFrontOverlay;
	m_promotionFrontOverlay = NULL;
	delete m_quitMenuOverlay;
	m_quitMenuOverlay = NULL;
	for( size_t key = 0; key < m_quitMenuKeyOverlays.size(); key++ )
		delete m_quitMenuKeyOverlays[ key ];
	m_quitMenuKeyOverlays.clear();
	for( Int grid = 0; grid < CELL_GRID_COUNT; grid++ )
	{
		delete m_cellFrontOverlay[ grid ];
		m_cellFrontOverlay[ grid ] = NULL;
	}
	delete m_queueOverlay;
	m_queueOverlay = NULL;
	delete m_queueFrontOverlay;
	m_queueFrontOverlay = NULL;
	delete m_tooltipOverlay;
	m_tooltipOverlay = NULL;
}

//-------------------------------------------------------------------------------------------------
/** Initialize the in game user interface */
//-------------------------------------------------------------------------------------------------
void InGameUI::init( void )
{
	INI ini;
	ini.load( AsciiString( "Data\\INI\\InGameUI.ini" ), INI_LOAD_OVERWRITE, NULL );

	//override INI values with language localized values:
	if (TheGlobalLanguageData)
	{
		if (TheGlobalLanguageData->m_drawableCaptionFont.name.isNotEmpty())
		{	m_drawableCaptionFont = TheGlobalLanguageData->m_drawableCaptionFont.name;
			m_drawableCaptionPointSize = TheGlobalLanguageData->m_drawableCaptionFont.size;
			m_drawableCaptionBold = TheGlobalLanguageData->m_drawableCaptionFont.bold;
		}

		if (TheGlobalLanguageData->m_messageFont.name.isNotEmpty())
		{	m_messageFont = TheGlobalLanguageData->m_messageFont.name;
			m_messagePointSize = TheGlobalLanguageData->m_messageFont.size;
			m_messageBold = TheGlobalLanguageData->m_messageFont.bold;
		}

		if (TheGlobalLanguageData->m_militaryCaptionTitleFont.name.isNotEmpty())
		{	m_militaryCaptionTitleFont = TheGlobalLanguageData->m_militaryCaptionTitleFont.name;
			m_militaryCaptionTitlePointSize = TheGlobalLanguageData->m_militaryCaptionTitleFont.size;
			m_militaryCaptionTitleBold = TheGlobalLanguageData->m_militaryCaptionTitleFont.bold;
		}

		if (TheGlobalLanguageData->m_militaryCaptionFont.name.isNotEmpty())
		{	m_militaryCaptionFont = TheGlobalLanguageData->m_militaryCaptionFont.name;
			m_militaryCaptionPointSize = TheGlobalLanguageData->m_militaryCaptionFont.size;
			m_militaryCaptionBold = TheGlobalLanguageData->m_militaryCaptionFont.bold;
		}

		if (TheGlobalLanguageData->m_superweaponCountdownNormalFont.name.isNotEmpty())
		{	m_superweaponNormalFont = TheGlobalLanguageData->m_superweaponCountdownNormalFont.name;
			m_superweaponNormalPointSize = TheGlobalLanguageData->m_superweaponCountdownNormalFont.size;
			// these are overlay text on the battlefield, not panel text - take them down a notch
			m_superweaponNormalPointSize = max( 8, (m_superweaponNormalPointSize * 4) / 5 );
			m_superweaponNormalBold = TheGlobalLanguageData->m_superweaponCountdownNormalFont.bold;
		}

		if (TheGlobalLanguageData->m_superweaponCountdownReadyFont.name.isNotEmpty())
		{	m_superweaponReadyFont = TheGlobalLanguageData->m_superweaponCountdownReadyFont.name;
			m_superweaponReadyPointSize = TheGlobalLanguageData->m_superweaponCountdownReadyFont.size;
			m_superweaponReadyBold = TheGlobalLanguageData->m_superweaponCountdownReadyFont.bold;
		}

		if (TheGlobalLanguageData->m_namedTimerCountdownNormalFont.name.isNotEmpty())
		{	m_namedTimerNormalFont = TheGlobalLanguageData->m_namedTimerCountdownNormalFont.name;
			m_namedTimerNormalPointSize = TheGlobalLanguageData->m_namedTimerCountdownNormalFont.size;
			m_namedTimerNormalBold = TheGlobalLanguageData->m_namedTimerCountdownNormalFont.bold;
		}

		if (TheGlobalLanguageData->m_namedTimerCountdownReadyFont.name.isNotEmpty())
		{	m_namedTimerReadyFont = TheGlobalLanguageData->m_namedTimerCountdownReadyFont.name;
			m_namedTimerReadyPointSize = TheGlobalLanguageData->m_namedTimerCountdownReadyFont.size;
			m_namedTimerReadyBold = TheGlobalLanguageData->m_namedTimerCountdownReadyFont.bold;
		}
	}

	// the message list is overlay text in the top-left corner of the battlefield, not panel text -
	// take it down a notch, same as the superweapon countdown above. (line spacing follows the
	// font height in postDraw(), so the whole stack shrinks with it.)
	m_messagePointSize = max( 8, (m_messagePointSize * 4) / 5 );

	/**@ todo we used to put in the hint spy translator, but it's difficult
	to order the translators when the code is not centralized so it has
	been moved to where all the other translators are attached in game client */

	// create the tactical view
	if (TheDisplay)
	{
		TheTacticalView = createView();
		TheTacticalView->init();
		TheDisplay->attachView( TheTacticalView );

		// make the tactical display the full screen width for now
		TheTacticalView->setWidth( TheDisplay->getWidth());
		TheTacticalView->setHeight( TheDisplay->getHeight() * 0.77f);
	}
	TheTacticalView->setDefaultView(0.0f, 0.0f, 1.0f);

	/** @todo this may be the wrong place to create the sidebar, but for now
	this is where it lives */
	createControlBar();

	/** @todo This may be the wrong place to create the replay menu, but for now
	this is where it lives */
	createReplayControl();

	// create the command bar
	TheControlBar = NEW ControlBar;
	TheControlBar->init();

	m_windowLayouts.clear();

	m_soloNexusSelectedDrawableID = INVALID_DRAWABLE_ID;


}  // end init

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
void InGameUI::setRadiusCursor(RadiusCursorType cursorType, const SpecialPowerTemplate* specPowTempl, WeaponSlotType weaponSlot)
{
	if (cursorType == m_curRcType)
		return;

	m_curRadiusCursor.clear();
	m_curRcType = RADIUSCURSOR_NONE;

	if (cursorType == RADIUSCURSOR_NONE)
		return;

	Object* obj = NULL;
	if( m_pendingGUICommand && m_pendingGUICommand->getCommandType() == GUI_COMMAND_SPECIAL_POWER_FROM_SHORTCUT )
	{
		if( ThePlayerList && ThePlayerList->getLocalPlayer() && specPowTempl != NULL )
		{
			obj = ThePlayerList->getLocalPlayer()->findMostReadyShortcutSpecialPowerOfType( specPowTempl->getSpecialPowerType() );
		}
	}
	else
	{
		if (getSelectCount() == 0)
			return;

		Drawable *draw = getFirstSelectedDrawable();
		if (draw == NULL)
			return;

		obj = draw->getObject();
	}

	if (obj == NULL)
		return;
	
	Player* controller = obj->getControllingPlayer();
	if (controller == NULL)
		return;

	Real radius = 0.0f;
	const Weapon* w = NULL;
	switch (cursorType)
	{
		// already handled
		//case RADIUSCURSOR_NONE:
		//	return;
		case RADIUSCURSOR_ATTACK_DAMAGE_AREA:
			w = obj->getWeaponInWeaponSlot(weaponSlot);
			radius = w ? w->getPrimaryDamageRadius(obj) : 0.0f;
			break;
		case RADIUSCURSOR_ATTACK_SCATTER_AREA:
			w = obj->getWeaponInWeaponSlot(weaponSlot);
			radius = w ? (w->getScatterRadius() + w->getScatterTargetScalar()) : 0.0f;
			break;
		case RADIUSCURSOR_ATTACK_CONTINUE_AREA:
		case RADIUSCURSOR_CLEARMINES:
			w = obj->getWeaponInWeaponSlot(weaponSlot);
			radius = w ? w->getContinueAttackRange() : 0.0f;
			break;
		case RADIUSCURSOR_GUARD_AREA:
			radius = AIGuardMachine::getStdGuardRange(obj);
			break;
		case RADIUSCURSOR_FRIENDLY_SPECIALPOWER:
		case RADIUSCURSOR_OFFENSIVE_SPECIALPOWER:
		case RADIUSCURSOR_SUPERWEAPON_SCATTER_AREA:
		case RADIUSCURSOR_EMERGENCY_REPAIR:
		case RADIUSCURSOR_PARTICLECANNON: 
		case RADIUSCURSOR_A10STRIKE:
		case RADIUSCURSOR_SPECTREGUNSHIP:
    case RADIUSCURSOR_HELIX_NAPALM_BOMB:
		case RADIUSCURSOR_DAISYCUTTER:
		case RADIUSCURSOR_CARPETBOMB:
		case RADIUSCURSOR_PARADROP:
		case RADIUSCURSOR_SPYSATELLITE: 
		case RADIUSCURSOR_NUCLEARMISSILE: 
		case RADIUSCURSOR_EMPPULSE:
		case RADIUSCURSOR_ARTILLERYBARRAGE:
		case RADIUSCURSOR_FRENZY:
		case RADIUSCURSOR_NAPALMSTRIKE:
		case RADIUSCURSOR_CLUSTERMINES:
		case RADIUSCURSOR_SCUDSTORM: 
		case RADIUSCURSOR_ANTHRAXBOMB:
		case RADIUSCURSOR_AMBUSH: 
		case RADIUSCURSOR_RADAR:
		case RADIUSCURSOR_SPYDRONE:
		case RADIUSCURSOR_AMBULANCE:
			radius = specPowTempl ? specPowTempl->getRadiusCursorRadius() : 0.0f;
			break;

	}

	if (radius <= 0.0f)
		return;

	Coord3D pos = { 0, 0, 0 };	// will be updated right away
	m_radiusCursors[cursorType].createRadiusDecal(pos, radius, controller, m_curRadiusCursor);
	m_curRcType = cursorType;

	handleRadiusCursor();
}

//-------------------------------------------------------------------------------------------------
/** handle updating of "radius cursors" that follow the mouse pos */
//-------------------------------------------------------------------------------------------------
void InGameUI::handleRadiusCursor()
{
	if (!m_curRadiusCursor.isEmpty())
	{
		const MouseIO* mouseIO = TheMouse->getMouseStatus();
		Coord3D pos;

		//
		// if the mouse is in the radar window, the position in the world is that which is
		// represented by the radar, otherwise we use the mouse position itself transformed
		// from screen to world
		// But only if the radar is on.
		//
		Bool radarOn = TheRadar->isRadarForced() 
									|| ( !TheRadar->isRadarHidden() 
												&& ThePlayerList->getLocalPlayer() 
												&& ThePlayerList->getLocalPlayer()->hasRadar()
											);

		Bool hasPos = radarOn && TheRadar->screenPixelToWorld( &mouseIO->pos, &pos );
		if( !hasPos )// if radar off, or point not on radar
			hasPos = TheTacticalView->screenToTerrain( &mouseIO->pos, &pos );


    if ( TheGlobalData->m_doubleClickAttackMove && m_duringDoubleClickAttackMoveGuardHintTimer > 0 )
    {
      m_curRadiusCursor.setOpacity( m_duringDoubleClickAttackMoveGuardHintTimer * 0.1f );
  		m_curRadiusCursor.setPosition( m_duringDoubleClickAttackMoveGuardHintStashedPosition );	//world space position of center of decal

    }
    else if ( hasPos )
    {
  		m_curRadiusCursor.setPosition(pos);	//world space position of center of decal
      m_curRadiusCursor.update();
    }

  }
}


void InGameUI::triggerDoubleClickAttackMoveGuardHint( void ) 
{
	const MouseIO* mouseIO = TheMouse->getMouseStatus();
	// No ground under the cursor means there is nothing to stash and nothing to hint at, so the
	// timer stays down rather than parking the hint on the corner of the map for eleven frames.
	if( TheTacticalView->screenToTerrain( &mouseIO->pos, &m_duringDoubleClickAttackMoveGuardHintStashedPosition ) )
		m_duringDoubleClickAttackMoveGuardHintTimer = 11;
}


//-------------------------------------------------------------------------------------------------
/** Handle the placement "icons" that appear at the cursor when we're putting down a 
	* structure to build.  Note that this has additional logic to also show a line
	* of objects because when we build "walls" we want to draw a line of repeating
	* wall pieces on the map where we want to put all of them */
//-------------------------------------------------------------------------------------------------


void InGameUI::evaluateSoloNexus( Drawable *newlyAddedDrawable )
{

	m_soloNexusSelectedDrawableID = INVALID_DRAWABLE_ID;//failsafe...

	// short test: If the thing just added is a nonmobster, bail with NULL
	if ( newlyAddedDrawable )
	{
		const Object *newObj = newlyAddedDrawable->getObject();
		if ( newObj && ! ( newObj->isKindOf(KINDOF_MOB_NEXUS) || newObj->isKindOf(KINDOF_IGNORED_IN_GUI) ) )
			return;
	}

	//LoopAllSelectedDrawables
	UnsignedShort nexaeFound = 0;
	for( DrawableListCIt it = m_selectedDrawables.begin(); it != m_selectedDrawables.end(); ++it ) 
	{

		Drawable *draw = (*it);
		const Object *obj = draw->getObject();


		if ( ! obj )
			continue;
			
		if ( obj->isKindOf( KINDOF_MOB_NEXUS ) )
		{
			++nexaeFound;
			if ( nexaeFound == 1 )
			{
				m_soloNexusSelectedDrawableID = draw->getID();
			}
			else // darn! more than one!
			{
				m_soloNexusSelectedDrawableID = INVALID_DRAWABLE_ID;
				return;
			}
		}
		else if ( ! obj->isKindOf( KINDOF_IGNORED_IN_GUI ) )// darn! a non-angrymobster!
		{
			m_soloNexusSelectedDrawableID = INVALID_DRAWABLE_ID;
			return;
		}

	}  // end for


}


//-------------------------------------------------------------------------------------------------
/** The longest weapon range anything in this template's weapon sets can reach.  Every set is
	* walked, not just the one an empty condition mask happens to select: a defence whose gun lives
	* in a conditional set (an upgrade, a garrisoned variant) would otherwise report no range.  The
	* range is the one the weapon is tested with, which the game trims a little from the INI number. */
//-------------------------------------------------------------------------------------------------
static Real templateWeaponRange( const ThingTemplate *tmpl )
{
	if( tmpl == NULL )
		return 0.0f;

	const WeaponBonus noBonus;
	Real range = 0.0f;
	const WeaponTemplateSetVector& sets = tmpl->getWeaponTemplateSets();
	for( WeaponTemplateSetVector::const_iterator si = sets.begin(); si != sets.end(); ++si )
	{
		for( Int ws = PRIMARY_WEAPON; ws < WEAPONSLOT_COUNT; ++ws )
		{
			const WeaponTemplate *wt = si->getNth( (WeaponSlotType)ws );
			if( wt )
				range = max( range, wt->getAttackRange( noBonus ) );
		}
	}

	return range;
}

//-------------------------------------------------------------------------------------------------
/** The radius to ring while this structure is being placed.
	*
	* Its own weapons first - and if it has none, the weapons of whatever it puts on the ground.
	* Half the GLA's defences carry no gun at all: a stinger site is an empty shell with a
	* SpawnBehavior that keeps three stinger soldiers alive next to it, and the soldiers own the
	* missiles. Judged by its own template the site is unarmed, so no ring was ever drawn for the
	* one faction whose defences most need siting.
	*
	* Only a structure the INI marks SPAWNS_ARE_THE_WEAPONS counts its spawns.  A GLA supply stash
	* spawns workers too, and a worker carries a mine-disarming weapon, so reading every spawner's
	* spawns put a reach circle round the stash. */
//-------------------------------------------------------------------------------------------------
static Real templatePlacementRange( const ThingTemplate *tmpl )
{
	Real range = templateWeaponRange( tmpl );
	if( range > 0.0f || tmpl == NULL || TheThingFactory == NULL )
		return range;
	if( !tmpl->isKindOf( KINDOF_SPAWNS_ARE_THE_WEAPONS ) )
		return range;

	const ModuleInfo& modules = tmpl->getBehaviorModuleInfo();
	for( Int i = 0; i < modules.getCount(); i++ )
	{
		if( modules.getNthName( i ) != "SpawnBehavior" )
			continue;

		const SpawnBehaviorModuleData *data =
			(const SpawnBehaviorModuleData *)modules.getNthData( i );
		if( data == NULL )
			continue;

		for( size_t s = 0; s < data->m_spawnTemplateNameData.size(); ++s )
		{
			const ThingTemplate *spawn =
				TheThingFactory->findTemplate( data->m_spawnTemplateNameData[ s ] );
			Real spawnRange = templateWeaponRange( spawn );
			if( spawnRange > range )
				range = spawnRange;
		}
	}

	return range;
}

//-------------------------------------------------------------------------------------------------
/** How far from its centre a structure of this template hits.  The game measures a shot from the
	* edge of the shooter's bounding circle, so the weapon range starts there, not at the middle. */
//-------------------------------------------------------------------------------------------------
static Real templateReach( const ThingTemplate *tmpl )
{
	const Real range = templatePlacementRange( tmpl );
	return range > 0.0f ? range + tmpl->getTemplateGeometryInfo().getBoundingCircleRadius() : 0.0f;
}

//-------------------------------------------------------------------------------------------------
/** A structure's flat reach, templateReach, along one direction from where it stands: further
	* wherever the ground it looks down on lies below it, by the same high ground bonus its weapon,
	* of templatePlacementRange, is tested with.  How far the edge goes decides whose ground height
	* it gets, so it is settled over a few passes. */
//-------------------------------------------------------------------------------------------------
static Real elevatedReach( Real reach, Real range, const Coord3D &center, Real angle )
{
	const Real dirX = Cos( angle );
	const Real dirY = Sin( angle );

	Real along = reach;
	for( Int pass = 0; pass < ELEVATED_REACH_PASSES; pass++ )
	{
		const Real groundZ = TheTerrainLogic->getGroundHeight( center.x + dirX * along, center.y + dirY * along );
		along = reach + Weapon_elevationRangeBonus( range, center.z - groundZ );
	}
	return along;
}

//-------------------------------------------------------------------------------------------------
// The spectator's page.  A loose copy under Run/ beats the archive, so it can be edited between two
// matches without a build.  What a data-click names is also the {{name}} that reads it back:
// data-click="option:Key" flips that on/off option and {{option:Key}} is "on" while it is on;
// data-click="flip:name" flips a switch that lasts the match and {{flip:name}} is "flipped" while
// it is flipped.  data-click="pick:group:choice" makes choice the group's one pick, so {{pick:group}}
// is "choice" and {{pick:group:choice}} is "on", and folds up the flip of the same name: a drop-down
// that flip:group opened closes on the choice made in it.  {{text:Label}} is a string table label
// in the player's language.
//-------------------------------------------------------------------------------------------------
static const char *const SPECTATOR_PAGE = "Window\\Html\\Spectator.html";
static const std::string FLIP_ACTION = "flip:";
static const std::string PICK_ACTION = "pick:";
// data-click="camera:free", "camera:director" or "camera:player" picks who drives the camera
// (ObserverCamera.h) and folds up flip:camera; "follow:N" or "follow:none" picks the player it
// follows and folds up flip:follow; "fog" turns the followed player's fog on and off
static const std::string CAMERA_ACTION = "camera:";
static const std::string CAMERA_DIRECTOR = "director";
static const std::string CAMERA_FREE = "free";
static const std::string CAMERA_PLAYER = "player";
static const std::string CAMERA_GROUP = "camera";
static const std::string FOLLOW_ACTION = "follow:";
static const std::string FOLLOW_NOBODY = "none";
static const std::string FOLLOW_GROUP = "follow";
static const std::string FOG_ACTION = "fog";
static const std::string TEXT_LOOKUP = "text:";
static const std::string STAT_GROUP = "stat";

enum
{
	NET_WORTH_REFRESH_FRAMES	= LOGICFRAMES_PER_SECOND / 2,	///< how often every player's worth is counted again
	FRAMES_PER_MINUTE					= LOGICFRAMES_PER_SECOND * 60,
	ARMY_CHART_UNITS					= 6,		///< kinds of unit shown per player, the most money first
	PLAYER_NAME_CHARS					= 11,		///< a name longer than this is cut, there is no clipping to hide it
	SECONDS_IN_MINUTE					= 60,
	SECONDS_PER_HOUR					= 60 * 60,
	FEED_LINE_FRAMES					= LOGICFRAMES_PER_SECOND * 10,	///< how long a line of the event feed stays up
	FEED_LINES_KEPT						= 6,		///< the most of those on screen at once; the oldest goes first
	FEED_HISTORY_KEPT					= 12,		///< the lines held for the open chat to show, however old
	COMMAND_SLOTS_PER_COLUMN	= 2,		///< the command bar numbers its slots down each column, top then bottom
	PERCENT										= 100
};

//-------------------------------------------------------------------------------------------------
/** Read a page under Window/Html into `page`, empty when it is not there: a missing page draws
	* nothing rather than stopping the match. */
//-------------------------------------------------------------------------------------------------
static void readHtmlPage( const char *path, std::string &page )
{
	page.clear();
	File *file = TheFileSystem->openFile( path, File::READ | File::BINARY );
	if( file == NULL )
	{
		DEBUG_LOG(( "Html page: %s is missing, so nothing is drawn in its place\n", path ));
		return;
	}
	const Int size = file->size();
	char *text = file->readEntireAndClose();
	page.assign( text, size );
	delete [] text;
}

//-------------------------------------------------------------------------------------------------
/** Is the local player watching rather than playing - an observer, or knocked out and stayed? */
//-------------------------------------------------------------------------------------------------
static Bool localPlayerWatching( void )
{
	const Player *local = ThePlayerList ? ThePlayerList->getLocalPlayer() : NULL;
	return local != NULL && !local->isPlayerActive();
}

//-------------------------------------------------------------------------------------------------
/** Is this strip switched off from the drop-down?  Only a watcher has the drop-down, so a player
	* always has his strips, whatever he last chose while watching somebody else's match. */
//-------------------------------------------------------------------------------------------------
static Bool stripSwitchedOff( Bool GlobalData::* flag )
{
	return localPlayerWatching() && !( TheGlobalData->*flag );
}

//-------------------------------------------------------------------------------------------------
/** {{text:Label}}: a string table label, in the player's language. */
//-------------------------------------------------------------------------------------------------
static Bool lookupGameText( const std::string &name, std::string &value )
{
	if( name.compare( 0, TEXT_LOOKUP.size(), TEXT_LOOKUP ) != 0 )
		return FALSE;
	value = WideCharStringToMultiByte( TheGameText->fetch( name.substr( TEXT_LOOKUP.size() ).c_str() ).str() );
	return TRUE;
}

//-------------------------------------------------------------------------------------------------
static std::string cssColor( Color color )
{
	UnsignedByte red, green, blue, alpha;
	GameGetColorComponents( color, &red, &green, &blue, &alpha );
	char text[ sizeof( "#rrggbb" ) ];
	sprintf( text, "#%02x%02x%02x", red, green, blue );
	return text;
}

/** Everything the page can rank one player by, counted in one walk over what he owns. */
struct SpectatorStats
{
	Player *player;
	Int team;
	Int networth;		///< cash plus the build cost of everything standing
	Int cash;
	Int income;			///< money earned per minute of match, averaged over the whole of it
	Int army;				///< the build cost of everything standing that is not a structure
	Int kills;			///< units and buildings destroyed
	Int losses;
	Int rank;
	Int power;			///< produced less consumed, negative when his base is browned out
	std::map< const ThingTemplate *, Int > units;	///< how many of each unit with a cameo he has standing
};

/** One entry of the stat drop-down: what pick:stat:key names, the label it goes by, and the number
	* the list is ranked by when it is picked. */
struct SpectatorStat
{
	const char *key;
	const char *label;
	Int SpectatorStats::*value;
};

static const SpectatorStat SPECTATOR_STATS[] =
{
	{ "networth",	"GUI:HudNetWorth",		&SpectatorStats::networth },
	{ "cash",			"GUI:HudStatCash",		&SpectatorStats::cash },
	{ "income",		"GUI:HudStatIncome",	&SpectatorStats::income },
	{ "army",			"GUI:HudStatArmy",		&SpectatorStats::army },
	{ "kills",		"GUI:HudStatKills",		&SpectatorStats::kills },
	{ "rank",			"GUI:HudStatRank",		&SpectatorStats::rank },
	{ "power",		"GUI:HudStatPower",		&SpectatorStats::power }
};

/** The stat pick:stat:key names, the first until one is picked. */
static const SpectatorStat &spectatorStat( const std::map< std::string, std::string > &picked )
{
	std::map< std::string, std::string >::const_iterator pick = picked.find( STAT_GROUP );
	for( Int stat = 0; pick != picked.end() && stat < (Int)ARRAY_SIZE( SPECTATOR_STATS ); stat++ )
		if( pick->second == SPECTATOR_STATS[ stat ].key )
			return SPECTATOR_STATS[ stat ];
	return SPECTATOR_STATS[ 0 ];
}

static void addObjectStats( Object *obj, void *userData )
{
	SpectatorStats *stats = (SpectatorStats *)userData;
	if( obj->isEffectivelyDead() )
		return;

	const ThingTemplate *thing = obj->getTemplate();
	const Int cost = thing->calcCostToBuild( stats->player );
	stats->networth += cost;
	if( obj->isKindOf( KINDOF_STRUCTURE ) )
		return;

	stats->army += cost;
	if( cost > 0 && thing->getButtonImage() != NULL )
		stats->units[ thing ]++;
}

//-------------------------------------------------------------------------------------------------
/** Everyone still in the match with his numbers, ranked by `stat`.  Allies both ways share a team
	* number, counted in the order the teams first appear in the player list. */
//-------------------------------------------------------------------------------------------------
static std::vector< SpectatorStats > gatherSpectatorStats( const SpectatorStat &stat )
{
	std::vector< SpectatorStats > players;
	const Player *local = ThePlayerList->getLocalPlayer();
	const UnsignedInt frame = TheGameLogic->getFrame();
	Int teams = 0;
	for( Int index = 0; index < ThePlayerList->getPlayerCount(); index++ )
	{
		Player *player = ThePlayerList->getNthPlayer( index );
		if( player == NULL || player == local || !player->isPlayerActive() || !player->isPlayableSide() )
			continue;

		ScoreKeeper *score = player->getScoreKeeper();
		SpectatorStats stats;
		stats.player = player;
		stats.team = -1;
		stats.cash = player->getMoney()->countMoney();
		stats.networth = stats.cash;
		stats.income = frame > 0 ? (Int)( (Int64)score->getTotalMoneyEarned() * FRAMES_PER_MINUTE / frame ) : 0;
		stats.army = 0;
		stats.kills = score->getTotalUnitsDestroyed() + score->getTotalBuildingsDestroyed();
		stats.losses = score->getTotalUnitsLost() + score->getTotalBuildingsLost();
		stats.rank = player->getRankLevel();
		stats.power = player->getEnergy()->getProduction() - player->getEnergy()->getConsumption();
		player->iterateObjects( addObjectStats, &stats );

		for( size_t earlier = 0; earlier < players.size() && stats.team < 0; earlier++ )
		{
			const Player *other = players[ earlier ].player;
			if( player->getRelationship( other->getDefaultTeam() ) == ALLIES && other->getRelationship( player->getDefaultTeam() ) == ALLIES )
				stats.team = players[ earlier ].team;
		}
		if( stats.team < 0 )
			stats.team = teams++;
		players.push_back( stats );
	}

	const Int SpectatorStats::*value = stat.value;
	std::stable_sort( players.begin(), players.end(),
										[ value ]( const SpectatorStats &a, const SpectatorStats &b ) { return a.*value > b.*value; } );
	return players;
}

//-------------------------------------------------------------------------------------------------
/** The page's "players" list: one entry per player in the order `players` is ranked, the picked
	* number written out and as a percentage of the highest. */
//-------------------------------------------------------------------------------------------------
static void fillSpectatorPlayers( const std::vector< SpectatorStats > &players, const SpectatorStat &stat,
																	std::vector< HtmlValues > &rows )
{
	Int highest = 0;
	for( size_t index = 0; index < players.size(); index++ )
		highest = max( highest, players[ index ].*stat.value );

	rows.clear();
	for( size_t index = 0; index < players.size(); index++ )
	{
		const SpectatorStats &stats = players[ index ];
		const PlayerTemplate *side = stats.player->getPlayerTemplate();
		const Image *portrait = side ? side->getEnabledImage() : NULL;
		const Int value = stats.*stat.value;

		std::wstring name( stats.player->getPlayerDisplayName().str() );
		if( name.size() > PLAYER_NAME_CHARS )
			name.resize( PLAYER_NAME_CHARS );

		std::string text = std::to_string( value );
		if( stat.value == &SpectatorStats::kills )
			text += " / " + std::to_string( stats.losses );

		HtmlValues row;
		row[ "name" ] = WideCharStringToMultiByte( name.c_str() );
		row[ "value" ] = text;
		row[ "networth" ] = std::to_string( stats.networth );
		row[ "share" ] = std::to_string( highest > 0 && value > 0 ? value * PERCENT / highest : 0 );
		row[ "color" ] = cssColor( clientPlayerColor( stats.player ) );
		row[ "portrait" ] = portrait ? portrait->getName().str() : "";
		rows.push_back( row );
	}
}

//-------------------------------------------------------------------------------------------------
/** The page's "army" list, Dota's item chart for an army: per player an entry of kind "head" with
	* his portrait and colour, then his most expensive kinds of unit, each of kind "unit" with its
	* cameo and how many are standing.  Flat, so a page floats the entries and clears at each head. */
//-------------------------------------------------------------------------------------------------
static void fillSpectatorArmies( const std::vector< SpectatorStats > &players, std::vector< HtmlValues > &cells )
{
	typedef std::pair< const ThingTemplate *, Int > UnitCount;

	cells.clear();
	for( size_t index = 0; index < players.size(); index++ )
	{
		const SpectatorStats &stats = players[ index ];
		const PlayerTemplate *side = stats.player->getPlayerTemplate();
		const Image *portrait = side ? side->getEnabledImage() : NULL;

		HtmlValues head;
		head[ "kind" ] = "head";
		head[ "image" ] = portrait ? portrait->getName().str() : "";
		head[ "color" ] = cssColor( clientPlayerColor( stats.player ) );
		cells.push_back( head );

		const Player *owner = stats.player;
		std::vector< UnitCount > units( stats.units.begin(), stats.units.end() );
		std::stable_sort( units.begin(), units.end(), [ owner ]( const UnitCount &a, const UnitCount &b )
			{ return a.second * a.first->calcCostToBuild( owner ) > b.second * b.first->calcCostToBuild( owner ); } );
		if( units.size() > ARMY_CHART_UNITS )
			units.resize( ARMY_CHART_UNITS );

		for( size_t unit = 0; unit < units.size(); unit++ )
		{
			HtmlValues cell;
			cell[ "kind" ] = "unit";
			cell[ "image" ] = units[ unit ].first->getButtonImage()->getName().str();
			cell[ "count" ] = std::to_string( units[ unit ].second );
			cells.push_back( cell );
		}
	}
}

static Int gatherPlayerSkills( const Player *player, const CommandButton **buttons, Int count, Int max );

//-------------------------------------------------------------------------------------------------
/** The class the page dresses itself in: the side whose command bar is on screen, "america",
	* "china" or "gla".  The observer bar is America's art under a name of its own, and a watcher who
	* selects a unit gets its owner's bar, so this follows whoever is being watched. */
//-------------------------------------------------------------------------------------------------
static std::string spectatorSide( void )
{
	ControlBarSchemeManager *schemes = TheControlBar ? TheControlBar->getControlBarSchemeManager() : NULL;
	if( schemes == NULL )
		return "america";

	AsciiString side = schemes->getCurrentSide();
	if( side == "Observer" )
		side = schemes->getCurrentArtTwinSide();
	if( side.startsWith( "China" ) || side == "Boss" )
		return "china";
	if( side.startsWith( "GLA" ) )
		return "gla";
	return "america";
}

/** A player's name as the page writes it, cut where there is no clipping to hide the rest. */
static std::string spectatorName( Player *player )
{
	std::wstring name( player->getPlayerDisplayName().str() );
	if( name.size() > PLAYER_NAME_CHARS )
		name.resize( PLAYER_NAME_CHARS );
	return WideCharStringToMultiByte( name.c_str() );
}

/** The entry that opens a player's run in a flat list: kind "head", his general and his colour. */
static HtmlValues spectatorHead( Player *player )
{
	const PlayerTemplate *side = player->getPlayerTemplate();
	const Image *portrait = side ? side->getEnabledImage() : NULL;

	HtmlValues head;
	head[ "kind" ] = "head";
	head[ "image" ] = portrait ? portrait->getName().str() : "";
	head[ "color" ] = cssColor( clientPlayerColor( player ) );
	head[ "name" ] = spectatorName( player );
	return head;
}

/** m:ss of match time, or h:mm:ss once it runs past the hour. */
static std::string spectatorClock( UnsignedInt frame )
{
	const UnsignedInt seconds = frame / LOGICFRAMES_PER_SECOND;
	char text[ sizeof( "000:00:00" ) ];
	if( seconds >= SECONDS_PER_HOUR )
		sprintf( text, "%u:%02u:%02u", seconds / SECONDS_PER_HOUR, seconds / SECONDS_IN_MINUTE % SECONDS_IN_MINUTE, seconds % SECONDS_IN_MINUTE );
	else
		sprintf( text, "%u:%02u", seconds / SECONDS_IN_MINUTE, seconds % SECONDS_IN_MINUTE );
	return text;
}

//-------------------------------------------------------------------------------------------------
/** The players the camera can follow, grouped by team the way the scoreboard is.  Which one it is
	* following is marked every frame, not at the lists' rebuilds. */
//-------------------------------------------------------------------------------------------------
static void fillSpectatorFollows( std::vector< SpectatorStats > players, std::vector< HtmlValues > &entries )
{
	std::stable_sort( players.begin(), players.end(),
										[]( const SpectatorStats &a, const SpectatorStats &b ) { return a.team < b.team; } );

	entries.clear();
	for( size_t index = 0; index < players.size(); index++ )
	{
		HtmlValues entry = spectatorHead( players[ index ].player );
		entry[ "click" ] = FOLLOW_ACTION + std::to_string( players[ index ].player->getPlayerIndex() );
		entries.push_back( entry );
	}
}

//-------------------------------------------------------------------------------------------------
/** What the camera's and the followed player's drop-downs and the fog switch show this frame. */
//-------------------------------------------------------------------------------------------------
static void fillSpectatorCameraValues( std::vector< HtmlValues > &follows, HtmlValues &values )
{
	const Int followed = TheObserverCamera.getFollowedPlayerIndex();
	const ObserverCameraMode mode = TheObserverCamera.getMode();
	const std::string followedClick = FOLLOW_ACTION + std::to_string( followed );
	for( size_t index = 0; index < follows.size(); index++ )
		follows[ index ][ "on" ] = follows[ index ][ "click" ] == followedClick ? "on" : "";

	const char *label = mode == OBSERVER_CAMERA_DIRECTOR ? "GUI:HudCameraDirector"
		: mode == OBSERVER_CAMERA_PLAYER ? "GUI:HudCameraPlayer" : "GUI:HudCameraFree";
	values[ "camera" ] = WideCharStringToMultiByte( TheGameText->fetch( label ).str() );
	values[ "cameradirector" ] = mode == OBSERVER_CAMERA_DIRECTOR ? "on" : "";
	values[ "cameraplayer" ] = mode == OBSERVER_CAMERA_PLAYER ? "on" : "";
	values[ "camerafree" ] = mode == OBSERVER_CAMERA_FREE ? "on" : "";
	values[ "fog" ] = TheObserverCamera.isFogOn() ? "on" : "";
	values[ "follownobody" ] = followed == ObserverCamera::NO_PLAYER ? "on" : "";
	if( followed == ObserverCamera::NO_PLAYER )
	{
		values[ "follow" ] = WideCharStringToMultiByte( TheGameText->fetch( "GUI:HudFollowNobody" ).str() );
		values[ "followcolor" ] = "";
		return;
	}

	const HtmlValues head = spectatorHead( ThePlayerList->getNthPlayer( followed ) );
	values[ "follow" ] = head.at( "name" );
	values[ "followcolor" ] = head.at( "color" );
}

//-------------------------------------------------------------------------------------------------
// The replay strip, on the spectator's page where a player's command grid stands: the timeline and
// the playback speed.  data-click="replay:seek" on #track jumps to the frame under the pointer,
// "replay:pause" pauses and resumes, and "replay:speed:N" plays at N percent of the logic rate the
// game was played at.  A seek forward fast-forwards, one picture in thirty drawn, until the frame
// is reached.  A seek back loads the last checkpoint in front of the frame and runs forward from
// there: the simulation keeps no history to step back through, so the replay saves the whole world
// every half minute of it as it plays.  The load puts no loading screen up; the picture holds.
//-------------------------------------------------------------------------------------------------
static const std::string REPLAY_ACTION = "replay:";
static const std::string REPLAY_SEEK = REPLAY_ACTION + "seek";
static const std::string REPLAY_SEEK_TO = REPLAY_SEEK + ":";
static const std::string REPLAY_PAUSE = REPLAY_ACTION + "pause";
static const std::string REPLAY_SPEED = REPLAY_ACTION + "speed:";
static const char *const REPLAY_TRACK = "#track";
static const Int REPLAY_SPEEDS[] = { 50, 100, 200, 400, 800 };

/** The frame a seek runs to, 0 for none.  Not a member: a seek back resets the whole interface when
	* it starts the replay over, and the frame has to outlive that. */
static UnsignedInt TheReplaySeekFrame = 0;

/** 1x: the rate the game was played at.  A skirmish on its fast setting recorded 60. */
static Int replayNormalFramesPerSecond( void )
{
	const Int recorded = TheRecorder->getPlaybackFramesPerSecond();
	return recorded > 0 ? recorded : LOGICFRAMES_PER_SECOND;
}

/** The replay's length, or the frame reached if the header never had it written. */
static UnsignedInt replayLength( void )
{
	return max( TheRecorder->getPlaybackFrameDuration(), TheGameLogic->getFrame() );
}

static void fillReplayValues( HtmlValues &values )
{
	if( !TheGameLogic->isInReplayGame() )
		return;

	const UnsignedInt frame = TheGameLogic->getFrame();
	const UnsignedInt length = replayLength();
	values[ "replay" ] = "on";
	values[ "replaypaused" ] = TheGameLogic->isGamePaused() ? "paused" : "";
	values[ "replayseeking" ] = TheReplaySeekFrame > 0 ? "seeking" : "";
	values[ "replaytime" ] = spectatorClock( frame );
	values[ "replaylength" ] = spectatorClock( length );
	values[ "replayshare" ] = std::to_string( length > 0 ? frame * PERCENT / length : 0 );

	const Int speed = TheGameEngine->getFramesPerSecondLimit() * PERCENT / replayNormalFramesPerSecond();
	for( Int each = 0; each < (Int)ARRAY_SIZE( REPLAY_SPEEDS ); each++ )
		values[ REPLAY_SPEED + std::to_string( REPLAY_SPEEDS[ each ] ) ] = speed == REPLAY_SPEEDS[ each ] ? "on" : "";
}

/** A rewind checkpoint: the world saved between two logic frames, where playback stood in the
	* replay, and the CRCs the last logic frame posted that the next one has yet to compare.  Those
	* are messages on their way, and the load's reset empties the stream they are in. */
struct ReplayCheckpoint
{
	AsciiString path;
	RecorderClass::PlaybackCursor cursor;
	GameLogicRandomState random;			///< a save game does not carry the logic's random stream
	std::vector< std::pair< Int, Bool > > postedCRCs;
};

static const UnsignedInt REPLAY_CHECKPOINT_FRAMES = LOGICFRAMES_PER_SECOND * 30;
static const char *const REPLAY_CHECKPOINT_FOLDER = "ReplayRewind";

/** The checkpoints of the replay TheReplayCheckpointsOf names, by frame.  Not members, for the same
	* reason as the seek frame: loading one resets the interface. */
static std::map< UnsignedInt, ReplayCheckpoint > TheReplayCheckpoints;
static AsciiString TheReplayCheckpointsOf;

/** A seek back is carried out on the next client pass rather than in the click: the load resets the
	* message stream, and the click is a message the stream is in the middle of handing round. */
static Bool TheReplayRewindWaiting = FALSE;

static void seekReplay( UnsignedInt target )
{
	TheGameLogic->setGamePaused( FALSE );
	// the first checkpoint is taken on the first frame, so only frame 0 has none behind it
	TheReplayRewindWaiting = target <= TheGameLogic->getFrame() && !TheReplayCheckpoints.empty();
	TheReplaySeekFrame = target;
}

/** A folder of this process's own: two copies of the game watching replays at once each keep their
	* checkpoints apart, and a copy that died leaves nothing another will take for its own. */
static AsciiString replayCheckpointFolder( void )
{
	AsciiString leaf;
	leaf.format( "%s\\%u", REPLAY_CHECKPOINT_FOLDER, (UnsignedInt)GetCurrentProcessId() );
	return TheGameState->getFilePathInSaveDirectory( leaf );
}

/** Every checkpoint file in the folder, not only the ones in the map, so nothing outlives the replay. */
static void forgetReplayCheckpoints( void )
{
	const AsciiString folder = replayCheckpointFolder();
	AsciiString pattern;
	pattern.format( "%s\\*.sav", folder.str() );
	WIN32_FIND_DATAA found;
	HANDLE search = FindFirstFileA( pattern.str(), &found );
	if( search != INVALID_HANDLE_VALUE )
	{
		do
		{
			AsciiString path;
			path.format( "%s\\%s", folder.str(), found.cFileName );
			DeleteFileA( path.str() );
		} while( FindNextFileA( search, &found ) );
		FindClose( search );
	}
	TheReplayCheckpoints.clear();
}

static void collectPostedCRCs( GameMessageList *list, std::vector< std::pair< Int, Bool > > &crcs )
{
	for( GameMessage *message = list->getFirstMessage(); message; message = message->next() )
		if( message->getType() == GameMessage::MSG_LOGIC_CRC )
			crcs.push_back( std::make_pair( message->getArgument( 0 )->integer, message->getArgument( 1 )->boolean ) );
}

static void takeReplayCheckpoint( UnsignedInt frame )
{
	const DWORD startMs = timeGetTime();
	CreateDirectoryA( TheGameState->getSaveDirectory().str(), NULL );
	CreateDirectoryA( TheGameState->getFilePathInSaveDirectory( REPLAY_CHECKPOINT_FOLDER ).str(), NULL );
	const AsciiString folder = replayCheckpointFolder();
	CreateDirectoryA( folder.str(), NULL );

	ReplayCheckpoint &checkpoint = TheReplayCheckpoints[ frame ];
	checkpoint.path.format( "%s\\%u.sav", folder.str(), frame );
	checkpoint.cursor = TheRecorder->getPlaybackCursor();
	checkpoint.random = GetGameLogicRandomState();
	// the command list first: what it holds reaches the logic ahead of what the stream still holds
	collectPostedCRCs( TheCommandList, checkpoint.postedCRCs );
	collectPostedCRCs( TheMessageStream, checkpoint.postedCRCs );
	TheGameState->saveCheckpoint( checkpoint.path );
	DEBUG_LOG(( "REPLAY CHECKPOINT frame %u in %u ms\n", frame, (UnsignedInt)( timeGetTime() - startMs ) ));
}

/** Load the last checkpoint at or before the frame, or the first one if the frame is before it.  The
	* camera, the speed and the observer's view stay as they were: the checkpoint carries the camera it
	* was taken with, which is not where the person watching is looking now. */
static void rewindReplay( UnsignedInt target )
{
	std::map< UnsignedInt, ReplayCheckpoint >::const_iterator at = TheReplayCheckpoints.upper_bound( target );
	if( at != TheReplayCheckpoints.begin() )
		--at;
	const ReplayCheckpoint &checkpoint = at->second;

	const AsciiString replayFile = TheRecorder->getCurrentReplayFilename();
	Coord3D lookingAt;
	TheTacticalView->getPosition( &lookingAt );
	const Real angle = TheTacticalView->getAngle();
	const Real pitch = TheTacticalView->getPitch();
	const Real zoom = TheTacticalView->getZoom();
	const Int framesPerSecond = TheGameEngine->getFramesPerSecondLimit();
	const DWORD startMs = timeGetTime();

	TheGameState->loadCheckpoint( checkpoint.path,
		[ & ]() { TheRecorder->resumePlayback( replayFile, checkpoint.cursor ); } );
	SetGameLogicRandomState( checkpoint.random );

	for( size_t each = 0; each < checkpoint.postedCRCs.size(); each++ )
	{
		GameMessage *crc = TheMessageStream->appendMessage( GameMessage::MSG_LOGIC_CRC );
		crc->appendIntegerArgument( checkpoint.postedCRCs[ each ].first );
		crc->appendBooleanArgument( checkpoint.postedCRCs[ each ].second );
	}

	TheTacticalView->lookAt( &lookingAt );
	TheTacticalView->setAngle( angle );
	TheTacticalView->setPitch( pitch );
	TheTacticalView->setZoom( zoom );
	TheGameEngine->setFramesPerSecondLimit( framesPerSecond );
	DEBUG_LOG(( "REPLAY REWIND to frame %u from the checkpoint at %u, loaded in %u ms\n",
		target, at->first, (UnsignedInt)( timeGetTime() - startMs ) ));
}

/** Every client pass, between two logic frames: the checkpoints are taken here, a seek back is carried
	* out here, and a seek stops on its frame rather than on the next picture drawn. */
static void updateReplaySeek( void )
{
	if( !TheGameLogic->isInGame() || TheGameLogic->isLoadingMap() )
		return;
	if( !TheGameLogic->isInReplayGame() )
	{
		TheReplaySeekFrame = 0;
		TheReplayRewindWaiting = FALSE;
		if( TheReplayCheckpointsOf.isNotEmpty() )
		{
			forgetReplayCheckpoints();
			TheReplayCheckpointsOf.clear();
		}
		return;
	}

	if( TheReplayCheckpointsOf != TheRecorder->getCurrentReplayFilename() )
	{
		forgetReplayCheckpoints();
		TheReplayCheckpointsOf = TheRecorder->getCurrentReplayFilename();
	}

	if( TheReplayRewindWaiting )
	{
		TheReplayRewindWaiting = FALSE;
		rewindReplay( TheReplaySeekFrame );
	}

	const UnsignedInt frame = TheGameLogic->getFrame();
	if( TheRecorder->hasPlaybackLeft() && frame > 0
			&& ( TheReplayCheckpoints.empty() || frame >= TheReplayCheckpoints.rbegin()->first + REPLAY_CHECKPOINT_FRAMES ) )
		takeReplayCheckpoint( frame );

	if( TheReplaySeekFrame == 0 )
		return;
	const Bool seeking = frame < TheReplaySeekFrame;
	TheWritableGlobalData->m_TiVOFastMode = seeking;
	if( !seeking )
		TheReplaySeekFrame = 0;
}

//-------------------------------------------------------------------------------------------------
/** The key a command bar slot is bound to right now, "Q" for KEY_Q, so the page names the key the
	* player really has: the WASD camera moves the whole top row along by one.  Empty when unbound. */
//-------------------------------------------------------------------------------------------------
static std::string commandSlotKey( Int commandSlot )
{
	static const std::string KEY_PREFIX = "KEY_";
	const GameMessage::Type meta = (GameMessage::Type)( GameMessage::MSG_META_COMMAND_SLOT01 + commandSlot );
	for( const MetaMapRec *map = TheMetaMap ? TheMetaMap->getFirstMetaMapRec() : NULL; map; map = map->m_next )
	{
		if( map->m_meta != meta )
			continue;
		for( const LookupListRec *key = KeyNames; key->name; key++ )
			if( key->value == map->m_key )
				return std::string( key->name ).substr( KEY_PREFIX.size() );
	}
	return std::string();
}

//-------------------------------------------------------------------------------------------------
/** The command bar's top row while the spectator page is up: slots 1, 3, 5... - Q, W, E, R, T, Y,
	* U by default, Q, E, R, T, Y, U, I with the WASD camera - pick the stats in the order the
	* drop-down lists them, and {{statkey:stat}} names each one's key.  The bottom row and anything
	* past the last stat are left to the command bar. */
//-------------------------------------------------------------------------------------------------
Bool InGameUI::pickSpectatorStat( Int commandSlot )
{
	const Int stat = commandSlot / COMMAND_SLOTS_PER_COLUMN;
	if( !m_spectatorPageShown || commandSlot % COMMAND_SLOTS_PER_COLUMN != 0 || stat >= (Int)ARRAY_SIZE( SPECTATOR_STATS ) )
		return FALSE;

	m_spectatorPicked[ STAT_GROUP ] = SPECTATOR_STATS[ stat ].key;
	m_spectatorFlipped.erase( STAT_GROUP );
	m_spectatorLists.clear();
	return TRUE;
}

//-------------------------------------------------------------------------------------------------
/** The spectator's page: the camera, every player ranked by the number picked in the stat drop-down
	* and each army's most expensive units.  The players themselves, the promotions and the production
	* are on the Tab scoreboard, and what happens is in the feed over the radar.  Only while watching:
	* the other side's worth is not a player's to know. */
//-------------------------------------------------------------------------------------------------
void InGameUI::drawSpectatorPage( void )
{
	m_spectatorPageShown = FALSE;

	if( TheGameLogic == NULL || !TheGameLogic->isInGame() || TheGameLogic->isInShellGame() )
		return;
	if( !localPlayerWatching() )
		return;

	if( !m_spectatorPageLoaded )
	{
		m_spectatorPageLoaded = TRUE;
		readHtmlPage( SPECTATOR_PAGE, m_spectatorPage );
	}
	if( m_spectatorPage.empty() )
		return;
	if( m_spectatorOverlay == NULL )
		m_spectatorOverlay = new HtmlOverlay( m_superweaponNormalFont );

	// a unit selected turns the page to his side's steel the same frame, not half a second on
	const UnsignedInt frame = TheGameLogic->getFrame();
	const Player *watched = TheControlBar->getObserverLookAtPlayer();
	if( m_spectatorLists.empty() || watched != m_spectatorListsWatched
			|| frame < m_spectatorListsFrame || frame >= m_spectatorListsFrame + NET_WORTH_REFRESH_FRAMES )
	{
		const SpectatorStat &stat = spectatorStat( m_spectatorPicked );
		const std::vector< SpectatorStats > players = gatherSpectatorStats( stat );
		fillSpectatorPlayers( players, stat, m_spectatorLists[ "players" ] );
		fillSpectatorArmies( players, m_spectatorLists[ "army" ] );
		fillSpectatorFollows( players, m_spectatorLists[ "follows" ] );

		m_spectatorTotals.clear();
		m_spectatorTotals[ "stat" ] = WideCharStringToMultiByte( TheGameText->fetch( stat.label ).str() );
		m_spectatorTotals[ PICK_ACTION + STAT_GROUP + ":" + stat.key ] = "on";
		m_spectatorTotals[ "side" ] = spectatorSide();
		for( Int each = 0; each < (Int)ARRAY_SIZE( SPECTATOR_STATS ); each++ )
			m_spectatorTotals[ std::string( "statkey:" ) + SPECTATOR_STATS[ each ].key ] = commandSlotKey( each * COMMAND_SLOTS_PER_COLUMN );
		m_spectatorListsFrame = frame;
		m_spectatorListsWatched = watched;
	}

	HtmlValues values = m_spectatorTotals;
	fillSpectatorCameraValues( m_spectatorLists[ "follows" ], values );
	fillReplayValues( values );
	for( std::map< std::string, std::string >::const_iterator pick = m_spectatorPicked.begin(); pick != m_spectatorPicked.end(); ++pick )
	{
		values[ PICK_ACTION + pick->first ] = pick->second;
		values[ PICK_ACTION + pick->first + ":" + pick->second ] = "on";
	}
	for( std::set< std::string >::const_iterator name = m_spectatorFlipped.begin(); name != m_spectatorFlipped.end(); ++name )
		values[ FLIP_ACTION + *name ] = "flipped";

	m_spectatorOverlay->setPage( HtmlTemplate_expand( m_spectatorPage, values, m_spectatorLists, lookupGameText ) );
	m_spectatorOverlay->hover( TheMouse->getMouseStatus()->pos );
	m_spectatorOverlay->draw();
	m_spectatorPageShown = TRUE;
}

//-------------------------------------------------------------------------------------------------
/** A click on something the page drew is the page's, whatever button it was, so it never becomes a
	* move order into the ground under it.  Only a plain left click does anything. */
//-------------------------------------------------------------------------------------------------
Bool InGameUI::handleSpectatorPageClick( const ICoord2D *mouse, Bool act )
{
	if( !m_spectatorPageShown || !m_spectatorOverlay->hover( *mouse ) )
		return FALSE;
	if( !act )
		return TRUE;

	const std::string action = m_spectatorOverlay->click( *mouse );
	if( action == REPLAY_SEEK && TheGameLogic->isInReplayGame() )
	{
		std::vector< IRegion2D > tracks;
		m_spectatorOverlay->rectsOf( REPLAY_TRACK, tracks );
		const IRegion2D &track = tracks.front();
		const Real share = clamp( 0.0f, (Real)( mouse->x - track.lo.x ) / (Real)( track.hi.x - track.lo.x ), 1.0f );
		seekReplay( REAL_TO_UNSIGNEDINT( share * replayLength() ) );
		return TRUE;
	}

	runSpectatorAction( action );
	return TRUE;
}

/** The page's three drop-downs, each opened by a flip of its name. */
static const std::string *const SPECTATOR_DROP_DOWNS[] = { &CAMERA_GROUP, &FOLLOW_GROUP, &STAT_GROUP };

//-------------------------------------------------------------------------------------------------
/** Any button pressed anywhere but on the page, over a window or over the world, folds up whichever
	* drop-down is open.  A press on the page is the page's, and runSpectatorAction folds the rest. */
//-------------------------------------------------------------------------------------------------
void InGameUI::foldSpectatorDropDowns( const ICoord2D &mouse )
{
	if( !m_spectatorPageShown || m_spectatorOverlay->hover( mouse ) )
		return;
	for( Int each = 0; each < (Int)ARRAY_SIZE( SPECTATOR_DROP_DOWNS ); each++ )
		m_spectatorFlipped.erase( *SPECTATOR_DROP_DOWNS[ each ] );
}

//-------------------------------------------------------------------------------------------------
void InGameUI::runSpectatorAction( const std::string &action )
{
	// whatever the page was clicked for, every drop-down but the one it opens folds up
	for( Int each = 0; each < (Int)ARRAY_SIZE( SPECTATOR_DROP_DOWNS ); each++ )
		if( action != FLIP_ACTION + *SPECTATOR_DROP_DOWNS[ each ] )
			m_spectatorFlipped.erase( *SPECTATOR_DROP_DOWNS[ each ] );

	if( action.compare( 0, FLIP_ACTION.size(), FLIP_ACTION ) == 0 )
	{
		const std::string name = action.substr( FLIP_ACTION.size() );
		if( m_spectatorFlipped.erase( name ) == 0 )
			m_spectatorFlipped.insert( name );
	}
	else if( action.compare( 0, PICK_ACTION.size(), PICK_ACTION ) == 0 )
	{
		const std::string pick = action.substr( PICK_ACTION.size() );
		const size_t colon = pick.find( ':' );
		if( colon == std::string::npos )
		{
			DEBUG_LOG(( "Spectator page: data-click=\"%s\" is not pick:group:choice\n", action.c_str() ));
			return;
		}

		const std::string group = pick.substr( 0, colon );
		m_spectatorPicked[ group ] = pick.substr( colon + 1 );
		m_spectatorFlipped.erase( group );
		m_spectatorLists.clear();
	}
	else if( action.compare( 0, CAMERA_ACTION.size(), CAMERA_ACTION ) == 0 )
	{
		const std::string choice = action.substr( CAMERA_ACTION.size() );
		m_spectatorFlipped.erase( CAMERA_GROUP );
		if( choice == CAMERA_DIRECTOR )
			TheObserverCamera.setMode( OBSERVER_CAMERA_DIRECTOR );
		else if( choice == CAMERA_PLAYER )
			TheObserverCamera.setMode( OBSERVER_CAMERA_PLAYER );
		else if( choice == CAMERA_FREE )
			TheObserverCamera.setMode( OBSERVER_CAMERA_FREE );
		else
			DEBUG_LOG(( "Spectator page: data-click=\"%s\" names no camera\n", action.c_str() ));
	}
	else if( action.compare( 0, FOLLOW_ACTION.size(), FOLLOW_ACTION ) == 0 )
	{
		// following a player is watching him too: his side on the bar and the page's steel
		const std::string choice = action.substr( FOLLOW_ACTION.size() );
		Player *player = choice == FOLLOW_NOBODY ? NULL : ThePlayerList->getNthPlayer( atoi( choice.c_str() ) );
		m_spectatorFlipped.erase( FOLLOW_GROUP );
		deselectAllDrawables();
		TheControlBar->watchPlayer( player );
		TheObserverCamera.followPlayer( player != NULL ? player->getPlayerIndex() : ObserverCamera::NO_PLAYER );
	}
	else if( action == FOG_ACTION )
		TheObserverCamera.setFog( !TheObserverCamera.isFogOn() );
	// the pause key's own message, so the key and the button are one path
	else if( action == REPLAY_PAUSE && TheGameLogic->isInReplayGame() )
		TheMessageStream->appendMessage( GameMessage::MSG_META_TOGGLE_PAUSE );
	// replay:seek:N, a frame to jump to, for a script that has no pointer to put on the timeline
	else if( action.compare( 0, REPLAY_SEEK_TO.size(), REPLAY_SEEK_TO ) == 0 && TheGameLogic->isInReplayGame() )
		seekReplay( (UnsignedInt)atoi( action.substr( REPLAY_SEEK_TO.size() ).c_str() ) );
	else if( action.compare( 0, REPLAY_SPEED.size(), REPLAY_SPEED ) == 0 && TheGameLogic->isInReplayGame() )
		TheGameEngine->setFramesPerSecondLimit( atoi( action.substr( REPLAY_SPEED.size() ).c_str() ) * replayNormalFramesPerSecond() / PERCENT );
}

//-------------------------------------------------------------------------------------------------
/** Add the pixel rows a convex four-cornered shape covers to a per-row list of spans, one span a
	* row, as x = start and y = end.  Rows are sampled through their middle and both edges are rounded
	* the same way, so two shapes sharing an edge meet on the same pixel. */
//-------------------------------------------------------------------------------------------------
static void addQuadSpans( const ICoord2D *corners[ 4 ], std::vector< std::vector< ICoord2D > > &rows )
{
	Int top = corners[ 0 ]->y;
	Int bottom = corners[ 0 ]->y;
	for( Int c = 1; c < 4; c++ )
	{
		top = min( top, corners[ c ]->y );
		bottom = max( bottom, corners[ c ]->y );
	}
	top = max( top, 0 );
	bottom = min( bottom, (Int)rows.size() );

	for( Int y = top; y < bottom; y++ )
	{
		const Real rowY = y + 0.5f;
		Real left = FLT_MAX;
		Real right = -FLT_MAX;

		for( Int c = 0; c < 4; c++ )
		{
			const ICoord2D &from = *corners[ c ];
			const ICoord2D &to = *corners[ ( c + 1 ) % 4 ];
			if( ( rowY < from.y ) == ( rowY < to.y ) )
				continue;		// this edge does not cross the row

			const Real x = from.x + ( to.x - from.x ) * ( rowY - from.y ) / (Real)( to.y - from.y );
			left = min( left, x );
			right = max( right, x );
		}

		ICoord2D span;
		span.x = REAL_TO_INT_FLOOR( left + 0.5f );
		span.y = REAL_TO_INT_FLOOR( right + 0.5f );
		rows[ y ].push_back( span );
	}
}

static Bool spanStartsFirst( const ICoord2D &a, const ICoord2D &b )
{
	return a.x < b.x;
}

//-------------------------------------------------------------------------------------------------
/** Paint the rows addQuadSpans collected, each row's overlapping spans merged first, so ground two
	* shapes both cover is painted once and a see-through colour does not come out darker there. */
//-------------------------------------------------------------------------------------------------
static void fillSpanRows( std::vector< std::vector< ICoord2D > > &rows, Color color )
{
	const Int screenW = (Int)TheDisplay->getWidth();

	TheDisplay->beginBatch2D();

	for( Int y = 0; y < (Int)rows.size(); y++ )
	{
		std::vector< ICoord2D > &spans = rows[ y ];
		if( spans.empty() )
			continue;

		std::sort( spans.begin(), spans.end(), spanStartsFirst );
		ICoord2D run = spans[ 0 ];
		for( size_t s = 1; s <= spans.size(); s++ )
		{
			// a pixel's gap between two neighbours is rounding, not open ground
			if( s < spans.size() && spans[ s ].x <= run.y + 1 )
			{
				run.y = max( run.y, spans[ s ].y );
				continue;
			}

			const Int x0 = max( run.x, 0 );
			const Int x1 = min( run.y, screenW );
			if( x1 > x0 )
				TheDisplay->drawFillRect( x0, y, x1 - x0, 1, color );
			if( s < spans.size() )
				run = spans[ s ];
		}
	}

	TheDisplay->endBatch2D();
}

//-------------------------------------------------------------------------------------------------
/** A defence's reach cut into the blind-spot polar grid: sector ray covers the angles from ray to
	* ray + 1, ring ring the distances from ring to ring + 1 ring widths.  reach is how far each sector
	* goes, longer down a slope, and radius the longest of them.  blocked is empty for a defence that
	* shoots whatever is in range. */
//-------------------------------------------------------------------------------------------------
struct ReachView
{
	Coord3D center;
	Real radius;
	std::vector< Real > reach;
	Int rings;
	std::vector< Bool > blocked;
};

static void traceReachView( ReachView &view, const ThingTemplate *tmpl )
{
	const Real flatReach = templateReach( tmpl );
	const Real range = templatePlacementRange( tmpl );
	view.reach.resize( BLIND_SPOT_RAYS );
	view.radius = 0.0f;
	for( Int ray = 0; ray < BLIND_SPOT_RAYS; ray++ )
	{
		view.reach[ ray ] = elevatedReach( flatReach, range, view.center, 2.0f * PI * ( ray + 0.5f ) / BLIND_SPOT_RAYS );
		view.radius = max( view.radius, view.reach[ ray ] );
	}
}

static Bool templateNeedsLineOfSight( const ThingTemplate *tmpl )
{
	return TheAI->getAiData()->m_attackUsesLineOfSight && tmpl->isKindOf( KINDOF_ATTACK_NEEDS_LINE_OF_SIGHT );
}

//-------------------------------------------------------------------------------------------------
/** Fill in which cells of the grid a defence cannot see from eyeZ.  Along one sector the walk keeps
	* the steepest terrain seen so far, which is the horizon: a target whose top sits under that slope
	* is behind a hill.  Everything from the first building cell outwards is behind that building, the
	* building's own ground included; the defence's own cells, self, are not in its way. */
//-------------------------------------------------------------------------------------------------
static void lookRoundReach( ReachView &view, Real eyeZ, ObjectID self )
{
	view.rings = (Int)ceil( view.radius / BLIND_SPOT_RING_WIDTH );
	view.blocked.assign( BLIND_SPOT_RAYS * view.rings, FALSE );

	for( Int ray = 0; ray < BLIND_SPOT_RAYS; ray++ )
	{
		const Real angle = 2.0f * PI * ( ray + 0.5f ) / BLIND_SPOT_RAYS;
		const Real dx = Cos( angle );
		const Real dy = Sin( angle );

		Real horizonSlope = -FLT_MAX;
		Bool behindBuilding = FALSE;
		for( Int ring = 0; ring < view.rings; ring++ )
		{
			const Real along = min( ( ring + 0.5f ) * BLIND_SPOT_RING_WIDTH, view.radius );
			Coord3D look = { view.center.x + dx * along, view.center.y + dy * along, 0.0f };
			const Real groundZ = TheTerrainLogic->getGroundHeight( look.x, look.y );

			const PathfindCell *cell = TheAI->pathfinder()->getCell( LAYER_GROUND, &look );
			if( cell && cell->getType() == PathfindCell::CELL_OBSTACLE && !cell->isObstacleTransparent()
				&& !( self != INVALID_ID && cell->isObstaclePresent( self ) ) )
				behindBuilding = TRUE;

			const Real targetSlope = ( groundZ + BLIND_SPOT_TARGET_HEIGHT - eyeZ ) / along;
			const Bool behindHill = targetSlope < horizonSlope;
			horizonSlope = max( horizonSlope, ( groundZ - LOS_TERRAIN_SLOP - eyeZ ) / along );

			const Bool inReach = ( ring + 0.5f ) * BLIND_SPOT_RING_WIDTH < view.reach[ ray ];
			view.blocked[ ray * view.rings + ring ] = inReach && ( behindBuilding || behindHill );
		}
	}
}

static Bool reachViewHits( const ReachView &view, Real x, Real y )
{
	const Real dx = x - view.center.x;
	const Real dy = y - view.center.y;
	const Real distance = sqrtf( sqr( dx ) + sqr( dy ) );
	if( distance >= view.radius )
		return FALSE;

	Real angle = atan2( dy, dx );
	if( angle < 0.0f )
		angle += 2.0f * PI;
	const Int ray = min( (Int)( angle * BLIND_SPOT_RAYS / ( 2.0f * PI ) ), BLIND_SPOT_RAYS - 1 );
	if( distance >= view.reach[ ray ] )
		return FALSE;
	if( view.blocked.empty() )
		return TRUE;

	const Int ring = min( (Int)( distance / BLIND_SPOT_RING_WIDTH ), view.rings - 1 );
	return !view.blocked[ ray * view.rings + ring ];
}

/// a structure whose reach and blind spots the local player is shown: their own, an ally's, or any to
/// an observer.  An enemy defence keeps both to itself, so they are found out by losing units to it.
static Bool reachRevealedToLocal( const Object *obj )
{
	const Player *local = ThePlayerList->getLocalPlayer();
	return obj->getControllingPlayer() == local || local->isPlayerObserver()
		|| local->getRelationship( obj->getTeam() ) == ALLIES;
}

//-------------------------------------------------------------------------------------------------
/** Every structure on the map as the reach drawing sees it, gathered once a frame.  A building going
	* up or coming down changes what a defence can see past, and one coming out of the fog changes which
	* circles are drawn, so every cache below is thrown away when this list differs from the last
	* frame's.  Working the circles and the blind spots out again every frame is what made clicking a
	* turret in a base of forty defences slow: each circle's outline was tested against every other
	* circle, every frame. */
//-------------------------------------------------------------------------------------------------
struct StructureKey
{
	ObjectID id;
	Coord3D position;
	const Player *owner;
	Bool reachShown;		///< armed, revealed to the local player and out of the fog, so its circle is drawn

	Bool operator==( const StructureKey &other ) const
	{
		return id == other.id && position.x == other.position.x && position.y == other.position.y
			&& position.z == other.position.z && owner == other.owner && reachShown == other.reachShown;
	}
};

static std::vector< StructureKey > theStructureKeys;
static UnsignedInt theStructureKeysFrame = 0;
static Int theStructureGeneration = 0;		///< goes up every time theStructureKeys changes

static void refreshStructureKeys( void )
{
	const UnsignedInt frame = TheGameClient->getFrame();
	if( frame == theStructureKeysFrame && !theStructureKeys.empty() )
		return;
	theStructureKeysFrame = frame;

	const Player *local = ThePlayerList->getLocalPlayer();
	std::vector< StructureKey > keys;
	keys.reserve( theStructureKeys.size() );
	for( Object *obj = TheGameLogic->getFirstObject(); obj; obj = obj->getNextObject() )
	{
		if( !obj->isKindOf( KINDOF_STRUCTURE ) )
			continue;

		StructureKey key;
		key.id = obj->getID();
		key.position = *obj->getPosition();
		key.owner = obj->getControllingPlayer();
		key.reachShown = templateReach( obj->getTemplate() ) > 0.0f && reachRevealedToLocal( obj )
			&& ( key.owner == local || obj->getShroudedStatus( local->getPlayerIndex() ) < OBJECTSHROUD_FOGGED );
		keys.push_back( key );
	}

	if( keys == theStructureKeys )
		return;
	theStructureKeys.swap( keys );
	++theStructureGeneration;
}

//-------------------------------------------------------------------------------------------------
/** A defence's polar grid with the ground under every corner of it looked up: corner ( ray, ring )
	* sits on the ray's leading edge at the ring's inner radius. */
//-------------------------------------------------------------------------------------------------
struct BlindSpotShade
{
	ReachView view;
	std::vector< Coord3D > corners;
};

static void buildBlindSpotShade( BlindSpotShade &shade, const ThingTemplate *tmpl, const Coord3D &center, Real eyeZ, ObjectID self )
{
	shade.view.center = center;
	traceReachView( shade.view, tmpl );
	lookRoundReach( shade.view, eyeZ, self );

	const Int cornerRings = shade.view.rings + 1;
	shade.corners.resize( BLIND_SPOT_RAYS * cornerRings );
	for( Int ray = 0; ray < BLIND_SPOT_RAYS; ray++ )
	{
		const Real angle = 2.0f * PI * ray / BLIND_SPOT_RAYS;
		for( Int ring = 0; ring < cornerRings; ring++ )
		{
			const Real along = min( ring * BLIND_SPOT_RING_WIDTH, shade.view.radius );
			Coord3D &ground = shade.corners[ ray * cornerRings + ring ];
			ground.x = center.x + Cos( angle ) * along;
			ground.y = center.y + Sin( angle ) * along;
			ground.z = TheTerrainLogic->getGroundHeight( ground.x, ground.y );
		}
	}
}

//-------------------------------------------------------------------------------------------------
/** The blocked cells of a shade projected corner by corner and filled as one shape, row by row, so
	* the shade follows the ground and has no seams in it. */
//-------------------------------------------------------------------------------------------------
static void fillBlindSpotShade( const BlindSpotShade &shade )
{
	const ReachView &view = shade.view;
	if( std::find( view.blocked.begin(), view.blocked.end(), TRUE ) == view.blocked.end() )
		return;

	// only the corners of blocked cells are projected, each once
	enum { CORNER_UNPROJECTED, CORNER_ON_SCREEN, CORNER_OFF_SCREEN };
	const Int rings = view.rings;
	const Int cornerRings = rings + 1;
	std::vector< ICoord2D > corners( shade.corners.size() );
	std::vector< UnsignedByte > projected( shade.corners.size(), CORNER_UNPROJECTED );

	std::vector< std::vector< ICoord2D > > rows( TheDisplay->getHeight() );
	for( Int ray = 0; ray < BLIND_SPOT_RAYS; ray++ )
	{
		const Int next = ( ray + 1 ) % BLIND_SPOT_RAYS;
		for( Int ring = 0; ring < rings; ring++ )
		{
			if( !view.blocked[ ray * rings + ring ] )
				continue;

			const Int quad[ 4 ] = { ray * cornerRings + ring, next * cornerRings + ring,
				next * cornerRings + ring + 1, ray * cornerRings + ring + 1 };
			Bool allOnScreen = TRUE;
			for( Int q = 0; q < 4; q++ )
			{
				UnsignedByte &state = projected[ quad[ q ] ];
				if( state == CORNER_UNPROJECTED )
				{
					const Bool onScreen = TheTacticalView->worldToScreenTriReturn( &shade.corners[ quad[ q ] ], &corners[ quad[ q ] ] ) != View::WTS_INVALID;
					state = onScreen ? CORNER_ON_SCREEN : CORNER_OFF_SCREEN;
				}
				allOnScreen = allOnScreen && state == CORNER_ON_SCREEN;
			}
			if( !allOnScreen )
				continue;

			const ICoord2D *quadCorners[ 4 ] = { &corners[ quad[ 0 ] ], &corners[ quad[ 1 ] ], &corners[ quad[ 2 ] ], &corners[ quad[ 3 ] ] };
			addQuadSpans( quadCorners, rows );
		}
	}

	fillSpanRows( rows, GameMakeColor( 0, 0, 0, 120 ) );
}

//-------------------------------------------------------------------------------------------------
/** What each of the player's and the allies' defences hits from where it stands, kept until a
	* structure somewhere changes. */
//-------------------------------------------------------------------------------------------------
static std::map< ObjectID, ReachView > theGuardViews;
static Int theGuardViewsGeneration = -1;

static const ReachView &guardView( const Object *obj )
{
	if( theGuardViewsGeneration != theStructureGeneration )
	{
		theGuardViews.clear();
		theGuardViewsGeneration = theStructureGeneration;
	}

	std::map< ObjectID, ReachView >::iterator found = theGuardViews.find( obj->getID() );
	if( found != theGuardViews.end() )
		return found->second;

	ReachView &guard = theGuardViews[ obj->getID() ];
	guard.center = *obj->getPosition();
	guard.rings = 0;
	traceReachView( guard, obj->getTemplate() );
	if( templateNeedsLineOfSight( obj->getTemplate() ) )
		lookRoundReach( guard, obj->getPosition()->z + obj->getGeometryInfo().getMaxHeightAbovePosition(), obj->getID() );
	return guard;
}

//-------------------------------------------------------------------------------------------------
/** Ground a defence on the cursor cannot see but one of the player's or an ally's defences already
	* hits is left bright: a turret beside the building covers the corner behind it, and that corner is
	* not a hole.  Each of those defences has the same polar grid from where it stands, with its own
	* cells not counted as a building in its way, and the shaded cell's middle is looked up in it. */
//-------------------------------------------------------------------------------------------------
static void clearGuardedBlindSpots( ReachView &pending )
{
	std::vector< const ReachView * > guards;
	for( size_t k = 0; k < theStructureKeys.size(); k++ )
	{
		const StructureKey &key = theStructureKeys[ k ];
		const Object *obj = TheGameLogic->findObjectByID( key.id );
		if( !reachRevealedToLocal( obj ) )
			continue;

		const Real flatReach = templateReach( obj->getTemplate() );
		const Real furthest = flatReach + Weapon_elevationRangeBonus( templatePlacementRange( obj->getTemplate() ), FLT_MAX );
		if( flatReach <= 0.0f || sqr( key.position.x - pending.center.x ) + sqr( key.position.y - pending.center.y ) >= sqr( furthest + pending.radius ) )
			continue;

		guards.push_back( &guardView( obj ) );
	}

	for( Int ray = 0; ray < BLIND_SPOT_RAYS; ray++ )
	{
		const Real angle = 2.0f * PI * ( ray + 0.5f ) / BLIND_SPOT_RAYS;
		for( Int ring = 0; ring < pending.rings; ring++ )
		{
			const Int index = ray * pending.rings + ring;
			if( !pending.blocked[ index ] )
				continue;

			const Real along = min( ( ring + 0.5f ) * BLIND_SPOT_RING_WIDTH, pending.radius );
			const Real x = pending.center.x + Cos( angle ) * along;
			const Real y = pending.center.y + Sin( angle ) * along;
			for( size_t g = 0; g < guards.size() && pending.blocked[ index ]; g++ )
			{
				if( reachViewHits( *guards[ g ], x, y ) )
					pending.blocked[ index ] = FALSE;
			}
		}
	}
}

//-------------------------------------------------------------------------------------------------
/** A selected defence's blind spots, worked out once and kept until a structure somewhere changes. */
//-------------------------------------------------------------------------------------------------
static std::map< ObjectID, BlindSpotShade > theSelectedShades;
static Int theSelectedShadesGeneration = -1;

static const BlindSpotShade &selectedBlindSpotShade( const Object *obj )
{
	if( theSelectedShadesGeneration != theStructureGeneration )
	{
		theSelectedShades.clear();
		theSelectedShadesGeneration = theStructureGeneration;
	}

	std::map< ObjectID, BlindSpotShade >::iterator found = theSelectedShades.find( obj->getID() );
	if( found != theSelectedShades.end() )
		return found->second;

	BlindSpotShade &shade = theSelectedShades[ obj->getID() ];
	const Coord3D *position = obj->getPosition();
	buildBlindSpotShade( shade, obj->getTemplate(), *position,
		position->z + obj->getGeometryInfo().getMaxHeightAbovePosition(), obj->getID() );
	return shade;
}

//-------------------------------------------------------------------------------------------------
/** The ground a defence could not shoot into, darkened inside its reach: the one on the cursor while
	* it is being sited, and every selected one of the local player's or an ally's.
	*
	* A defence that needs a line of sight - a Patriot battery, a Gattling Cannon, a Fire Base - does
	* not fire through a building, and cannot pick a target a hill hides from it.  Both rules are the
	* game's own.  Target picking asks the terrain for a clear line from the top of the defence to the
	* target, and the pathfinder refuses a shot when a solid structure's cells lie between them; a
	* structure the art says can be seen through does not count.
	*
	* The range area is cut into a polar grid, a sector per ray and a ring per half pathfind cell, and
	* each cell of it is looked at from the defence.  Along one sector the walk keeps the steepest
	* terrain seen so far, which is the horizon: a target whose top sits under that slope is behind a
	* hill.  Everything from the first building cell outwards is behind that building, the building's
	* own ground included.  The blocked cells are projected corner by corner onto the terrain and
	* filled as one shape, row by row, so the shade follows the ground and has no seams in it.
	*
	* A Stinger Site, a bunker and anything else that does not need the line of sight gets no shading,
	* because none of that ground is out of its reach. */
//-------------------------------------------------------------------------------------------------
void InGameUI::drawBlindSpots( void )
{
	if( m_pendingPlaceType != NULL && m_placementRangeRingUp && templateNeedsLineOfSight( m_pendingPlaceType ) )
	{
		refreshStructureKeys();
		Coord3D center = *m_placeIcon[ 0 ]->getPosition();
		center.z = TheTerrainLogic->getGroundHeight( center.x, center.y );
		const Real eyeZ = center.z + m_pendingPlaceType->getTemplateGeometryInfo().getMaxHeightAbovePosition();

		BlindSpotShade pending;
		buildBlindSpotShade( pending, m_pendingPlaceType, center, eyeZ, INVALID_ID );
		clearGuardedBlindSpots( pending.view );
		fillBlindSpotShade( pending );
	}

	if( !TheGlobalData->m_showPlacementRangeRing )
		return;

	// a selected defence shows its own blind spots only, not what its neighbours cover for it
	for( DrawableListCIt it = m_selectedDrawables.begin(); it != m_selectedDrawables.end(); ++it )
	{
		const Object *obj = (*it)->getObject();
		if( obj == NULL || !obj->isKindOf( KINDOF_STRUCTURE ) || templateReach( obj->getTemplate() ) <= 0.0f )
			continue;
		if( !templateNeedsLineOfSight( obj->getTemplate() ) || !reachRevealedToLocal( obj ) )
			continue;

		refreshStructureKeys();
		fillBlindSpotShade( selectedBlindSpotShade( obj ) );
	}
}

struct ReachCircle
{
	Coord3D center;
	Real radius;								///< the furthest the outline goes
	std::vector< Real > outline;	///< the reach at the start of each outline segment, longer down a slope
	const Player *owner;
};

static void traceReachCircle( ReachCircle &circle, const ThingTemplate *tmpl )
{
	const Real flatReach = templateReach( tmpl );
	const Real range = templatePlacementRange( tmpl );
	circle.outline.resize( REACH_OUTLINE_SEGMENTS );
	circle.radius = 0.0f;
	for( Int segment = 0; segment < REACH_OUTLINE_SEGMENTS; segment++ )
	{
		circle.outline[ segment ] = elevatedReach( flatReach, range, circle.center, 2.0f * PI * segment / REACH_OUTLINE_SEGMENTS );
		circle.radius = max( circle.radius, circle.outline[ segment ] );
	}
}

/// the reach toward an angle from 0 to two pi, read between the outline corners either side of it
static Real reachAtAngle( const ReachCircle &circle, Real angle )
{
	const Real at = angle * REACH_OUTLINE_SEGMENTS / ( 2.0f * PI );
	const Int from = min( (Int)at, REACH_OUTLINE_SEGMENTS - 1 );
	const Real part = at - from;
	return circle.outline[ from ] * ( 1.0f - part ) + circle.outline[ ( from + 1 ) % REACH_OUTLINE_SEGMENTS ] * part;
}

static Bool insideReach( const ReachCircle &circle, Real x, Real y )
{
	const Real dx = x - circle.center.x;
	const Real dy = y - circle.center.y;
	const Real distanceSqr = sqr( dx ) + sqr( dy );
	if( distanceSqr >= sqr( circle.radius ) )
		return FALSE;

	Real angle = atan2( dy, dx );
	if( angle < 0.0f )
		angle += 2.0f * PI;
	return distanceSqr < sqr( reachAtAngle( circle, angle ) );
}

//-------------------------------------------------------------------------------------------------
/** What hides a stretch of one player's outline: another of that player's circles in circles, or
	* extra when it is that player's.  Two players' circles cross, each in its own colour. */
//-------------------------------------------------------------------------------------------------
struct ReachCover
{
	const std::vector< ReachCircle > *circles;	///< NULL when only extra covers
	size_t self;															///< the circle being cut, which never covers itself
	const ReachCircle *extra;									///< NULL when there is none
	const Player *owner;

	Bool covers( Real x, Real y ) const
	{
		for( size_t c = 0; circles != NULL && c < circles->size(); c++ )
		{
			const ReachCircle &other = (*circles)[ c ];
			if( c != self && other.owner == owner && insideReach( other, x, y ) )
				return TRUE;
		}
		return extra != NULL && extra->owner == owner && insideReach( *extra, x, y );
	}
};

struct ReachSegment
{
	size_t circle;
	Real angles[ 2 ];
	Coord3D ends[ 2 ];		///< on the ground
};

//-------------------------------------------------------------------------------------------------
/** One stretch of a circle's outline between two angles, cut where it runs under cover: with both
	* ends covered it is dropped, with one it walks the arc in halves to where it crosses and keeps the
	* outside. */
//-------------------------------------------------------------------------------------------------
static void clipReachSegment( const ReachCircle &circle, size_t index, Real fromAngle, Real toAngle,
															const ReachCover &cover, std::vector< ReachSegment > &out )
{
	ReachSegment segment;
	segment.circle = index;
	segment.angles[ 0 ] = fromAngle;
	segment.angles[ 1 ] = toAngle;

	Bool inside[ 2 ];
	for( Int e = 0; e < 2; e++ )
	{
		const Real reach = reachAtAngle( circle, segment.angles[ e ] );
		inside[ e ] = cover.covers( circle.center.x + Cos( segment.angles[ e ] ) * reach, circle.center.y + Sin( segment.angles[ e ] ) * reach );
	}
	if( inside[ 0 ] && inside[ 1 ] )
		return;

	if( inside[ 0 ] != inside[ 1 ] )
	{
		const Int cut = inside[ 0 ] ? 0 : 1;
		Real outsideAngle = segment.angles[ 1 - cut ];
		Real insideAngle = segment.angles[ cut ];
		for( Int halving = 0; halving < REACH_CROSSING_HALVINGS; halving++ )
		{
			const Real middle = 0.5f * ( outsideAngle + insideAngle );
			const Real middleReach = reachAtAngle( circle, middle );
			if( cover.covers( circle.center.x + Cos( middle ) * middleReach, circle.center.y + Sin( middle ) * middleReach ) )
				insideAngle = middle;
			else
				outsideAngle = middle;
		}
		segment.angles[ cut ] = outsideAngle;
	}

	for( Int e = 0; e < 2; e++ )
	{
		const Real reach = reachAtAngle( circle, segment.angles[ e ] );
		segment.ends[ e ].x = circle.center.x + Cos( segment.angles[ e ] ) * reach;
		segment.ends[ e ].y = circle.center.y + Sin( segment.angles[ e ] ) * reach;
		segment.ends[ e ].z = TheTerrainLogic->getGroundHeight( segment.ends[ e ].x, segment.ends[ e ].y );
	}
	out.push_back( segment );
}

static void outlineReachCircle( const std::vector< ReachCircle > &circles, size_t index, const ReachCover &cover,
																std::vector< ReachSegment > &out )
{
	for( Int segment = 0; segment < REACH_OUTLINE_SEGMENTS; segment++ )
	{
		clipReachSegment( circles[ index ], index, 2.0f * PI * segment / REACH_OUTLINE_SEGMENTS,
			2.0f * PI * ( segment + 1 ) / REACH_OUTLINE_SEGMENTS, cover, out );
	}
}

//-------------------------------------------------------------------------------------------------
/** The outlines of every armed building in sight, cut against each other, kept until a structure
	* somewhere changes.  Only the circle on the cursor is worked out every frame. */
//-------------------------------------------------------------------------------------------------
static std::vector< ReachCircle > theReachCircles;
static std::vector< ReachSegment > theReachSegments;
static Int theReachGeneration = -1;

static void refreshReachOutlines( void )
{
	if( theReachGeneration == theStructureGeneration )
		return;
	theReachGeneration = theStructureGeneration;

	theReachCircles.clear();
	theReachSegments.clear();
	for( size_t k = 0; k < theStructureKeys.size(); k++ )
	{
		const StructureKey &key = theStructureKeys[ k ];
		if( !key.reachShown )
			continue;

		ReachCircle placed;
		placed.center = key.position;
		placed.owner = key.owner;
		traceReachCircle( placed, TheGameLogic->findObjectByID( key.id )->getTemplate() );
		theReachCircles.push_back( placed );
	}

	for( size_t c = 0; c < theReachCircles.size(); c++ )
	{
		const ReachCover cover = { &theReachCircles, c, NULL, theReachCircles[ c ].owner };
		outlineReachCircle( theReachCircles, c, cover, theReachSegments );
	}
}

static void drawReachSegment( const ReachSegment &segment, const Player *owner )
{
	UnsignedByte red, green, blue, alpha;
	GameGetColorComponents( clientPlayerColor( owner ), &red, &green, &blue, &alpha );

	ICoord2D from, to;
	if( TheTacticalView->worldToScreenTriReturn( &segment.ends[ 0 ], &from ) != View::WTS_INVALID
		&& TheTacticalView->worldToScreenTriReturn( &segment.ends[ 1 ], &to ) != View::WTS_INVALID )
		TheDisplay->drawLine( from.x, from.y, to.x, to.y, REACH_OUTLINE_WIDTH, GameMakeColor( red, green, blue, REACH_OUTLINE_ALPHA ) );
}

//-------------------------------------------------------------------------------------------------
/** While a structure is on the cursor, the reach of every armed building in sight: yours, your
	* allies', and the one on the cursor if it is armed.  An enemy's is never drawn.
	*
	* Each circle is exactly the distance a shot is allowed at: the weapon range the game tests with,
	* measured from the edge of the shooter's bounding circle, so from the centre it is that range
	* plus the bounding radius.  Each is drawn in its owner's colour.  Where one player's circles
	* overlap they are one area: the thin outline leaves out every stretch of a circle that runs inside
	* another of the same player's, cutting it where the two cross.  An ally's building under fog is
	* skipped, so the circles tell nothing the map does not. */
//-------------------------------------------------------------------------------------------------
void InGameUI::drawPlacementReach( void )
{
	if( !TheGlobalData->m_showPlacementRangeRing )
		return;

	// on the cursor, or clicked: a selected armed building brings the circles up just the same
	Bool armedSelected = FALSE;
	for( DrawableListCIt it = m_selectedDrawables.begin(); it != m_selectedDrawables.end() && !armedSelected; ++it )
	{
		const Object *obj = (*it)->getObject();
		armedSelected = obj && obj->isKindOf( KINDOF_STRUCTURE ) && templateReach( obj->getTemplate() ) > 0.0f
			&& reachRevealedToLocal( obj );
	}
	if( m_pendingPlaceType == NULL && !armedSelected )
		return;

	refreshStructureKeys();
	refreshReachOutlines();

	// the circle on the cursor moves every frame, so it alone is cut afresh against the kept ones
	const Bool pendingUp = m_pendingPlaceType != NULL && m_placementRangeRingUp;
	std::vector< ReachCircle > pendingCircles;
	std::vector< ReachSegment > pendingSegments;
	if( pendingUp )
	{
		ReachCircle pending;
		pending.center.x = m_placeIcon[ 0 ]->getPosition()->x;
		pending.center.y = m_placeIcon[ 0 ]->getPosition()->y;
		pending.center.z = TheTerrainLogic->getGroundHeight( pending.center.x, pending.center.y );
		pending.owner = ThePlayerList->getLocalPlayer();
		traceReachCircle( pending, m_pendingPlaceType );
		pendingCircles.push_back( pending );

		const ReachCover cover = { &theReachCircles, theReachCircles.size(), NULL, pending.owner };
		outlineReachCircle( pendingCircles, 0, cover, pendingSegments );
	}

	TheDisplay->beginBatch2D();
	std::vector< ReachSegment > recut;
	for( size_t s = 0; s < theReachSegments.size(); s++ )
	{
		const ReachSegment &segment = theReachSegments[ s ];
		const ReachCircle &circle = theReachCircles[ segment.circle ];
		const Bool underPending = pendingUp && circle.owner == pendingCircles[ 0 ].owner
			&& ( insideReach( pendingCircles[ 0 ], segment.ends[ 0 ].x, segment.ends[ 0 ].y )
				|| insideReach( pendingCircles[ 0 ], segment.ends[ 1 ].x, segment.ends[ 1 ].y ) );
		if( !underPending )
		{
			drawReachSegment( segment, circle.owner );
			continue;
		}

		recut.clear();
		const ReachCover cover = { NULL, segment.circle, &pendingCircles[ 0 ], circle.owner };
		clipReachSegment( circle, segment.circle, segment.angles[ 0 ], segment.angles[ 1 ], cover, recut );
		for( size_t r = 0; r < recut.size(); r++ )
			drawReachSegment( recut[ r ], circle.owner );
	}
	for( size_t s = 0; s < pendingSegments.size(); s++ )
		drawReachSegment( pendingSegments[ s ], pendingCircles[ 0 ].owner );
	TheDisplay->endBatch2D();
}

void InGameUI::handleBuildPlacements( void )
{

	//
	// if we're in the process of placing something we need up update one or more drawables
	// based on the position of the mouse
	//
	if( m_pendingPlaceType )
	{
		ICoord2D loc;
		Coord3D world;
		Real angle = m_placeIcon[ 0 ]->getOrientation();

		//
		// ShowPlacementRangeRing: ring the structure's own weapon range while it is being placed,
		// so a defense can be sited against what it actually covers. The radius comes off the
		// template - there is no Object yet - and drawPlacementReach draws it under the cursor.
		//
		if( TheGlobalData->m_showPlacementRangeRing )
		{
			m_placementRingRadius = templateReach( m_pendingPlaceType );
			m_placementRangeRingUp = ( m_placementRingRadius > 0.0f );
		}

		// update the angle of the icon to match any placement angle and pick the
		// location the icon will be at (anchored is the start, otherwise it's the mouse)
		Bool row = FALSE;
		if( isPlacementAnchored() )
		{
			ICoord2D start, end;

			// get the placement arrow points
			getPlacementPoints( &start, &end );

			// set icon to anchor point
			loc = start;

			// only adjust angle if we've actually moved the mouse, and not into a row: that drag
			// lays structures, the heading stays the one they had
			const Bool dragged = start.x != end.x || start.y != end.y;
			row = dragged && placesRow();
			if( dragged && !row )
				angle = computePlacementAngle( &start, &end );

		}  // end if
		else
		{
			const MouseIO *mouseIO = TheMouse->getMouseStatus();

			// location is the mouse position
			loc = mouseIO->pos;

		}  // end else

		// set the location and angle of the place icon
		/**@todo this whole orientation vector thing is LAME! Must replace, all I want to
		to do is set a simple angle and have it automatically change, ug! */
		// Above the horizon the ray reaches no ground and the answer is the corner of the map, which
		// threw the ghost across the world and back.  Hold it where it was instead.
		if( !TheTacticalView->screenToTerrain( &loc, &world ) )
			world = *m_placeIcon[ 0 ]->getPosition();
		snapPlacementToGrid( &world, m_pendingPlaceType, angle );

		//
		// NudgeBuildPlacement: the ghost sits where the last legality check found room, which is
		// the cursor itself whenever the cursor works.  The check below runs every other frame and
		// always measures from the cursor, never from an already-nudged spot, so the offset cannot
		// walk away from the mouse; the ghost is at most one frame behind it.
		//
		const Coord3D cursorWorld = world;

		// a row starts where it was anchored; each piece is judged where it stands, not nudged
		if( row )
			m_placementNudge.zero();

		if( m_placementNudge.x != 0.0f || m_placementNudge.y != 0.0f )
		{
			world.x += m_placementNudge.x;
			world.y += m_placementNudge.y;
			world.z = TheTerrainLogic->getGroundHeight( world.x, world.y );
		}

		m_placeIcon[ 0 ]->setPosition( &world );
		m_placeIcon[ 0 ]->setOrientation( angle );


		//
		// check to see if this is a legal location to build something at and tint or "un-tint"
		// the cursor icons as appropriate.  This involves a pathfind which could be
		// expensive so we don't want to do it on every frame (althought that would be ideal)
		// If we discover there are cases that this is just too slow we should increase the
		// delay time between checks or we need to come up with a way of recording what is
		// valid and what isn't or "fudge" the results to feel "ok"
		//
		if( ( TheGameClient->getFrame() & 0x1 ) && !row )
		{
			TheTerrainVisual->removeAllBibs();

			Object *builderObject = TheGameLogic->findObjectByID( getPendingPlaceSourceObjectID() );

			const UnsignedInt checkOptions = placementCheckOptions();
			Coord3D spot = cursorWorld;
			LegalBuildCode lbc;
			lbc = TheBuildAssistant->isLocationLegalToBuild( &spot, m_pendingPlaceType, angle,
																											 checkOptions, builderObject, NULL );

			//
			// and the ground an order of your own has already been placed on but has not landed on
			// yet - see recordPendingPlacement.  Without this the ghost is green over a structure
			// that is already paid for, which is what a shift-held run of clicks sees on a laggy link.
			//
			if( lbc == LBC_OK && overlapsPendingPlacement( &spot, m_pendingPlaceType, angle ) )
				lbc = LBC_OBJECTS_IN_THE_WAY;

			//
			// Blocked: look for the nearest spot that is not, and move the ghost there - the click
			// does the same search (see PlaceEventTranslator), so what you see is where it lands.
			// Shroud is left alone on purpose: unscouted ground is not a placement mistake to fix,
			// and hunting around in it would answer questions about ground you cannot see.
			//
			m_placementNudge.zero();
			if( lbc != LBC_OK && lbc != LBC_SHROUD )
			{
				const Bool moved = nudgePlacementToLegal( &spot, m_pendingPlaceType, angle,
																									builderObject );

				//
				// Either way the search has just painted a red bib on everything it bumped into on
				// the way out, which is a report on spots nobody is proposing.  Clear them, and if
				// there was nowhere to go, ask about the spot under the cursor once more so the one
				// bib that does explain the refusal comes back.
				//
				TheTerrainVisual->removeAllBibs();

				if( moved )
				{
					m_placementNudge.x = spot.x - cursorWorld.x;
					m_placementNudge.y = spot.y - cursorWorld.y;
					lbc = LBC_OK;
				}
				else
				{
					TheBuildAssistant->isLocationLegalToBuild( &cursorWorld, m_pendingPlaceType, angle,
																										 checkOptions, builderObject, NULL );
				}
			}

			// the cursor reads this too - see createCommandHint's MOUSEMODE_BUILD_PLACE case
			m_placementLegal = ( lbc == LBC_OK );

			if( lbc != LBC_OK )
				m_placeIcon[ 0 ]->colorTint( &illegalBuildColor );
			else
				m_placeIcon[ 0 ]->colorTint( NULL );

			


			// Add the bibs around the structure.
			if (lbc != LBC_OK) 
			{
				TheTerrainVisual->addFactionBibDrawable(m_placeIcon[0], lbc != LBC_OK);
			} else {
				TheTerrainVisual->removeFactionBibDrawable(m_placeIcon[0]);
			}
		}  // end if



		//
		// we have additional place icons when we're placing down a line of walls or other
		// similarly placed object, or a shift-dragged row of structures ... for those we will
		// have them be oriented the same way as the first one, but we'll set their positions so
		// that they "tile" end to end
		//
		const Bool lineBuild = isPlacementAnchored() && TheBuildAssistant->isLineBuildTemplate( m_pendingPlaceType );
		Int iconsUsed = 1;
		if( lineBuild || row )
		{
			Int i;

			// get our line placement points
			ICoord2D screenStart, screenEnd;
			getPlacementPoints( &screenStart, &screenEnd );

			// project the start and the end points of the line anchor into the 3D world
			Coord3D worldStart, worldEnd;
			// An end that leaves the ground - the cursor dragged above the horizon - has no place on
			// the map, and the corner is not it.  Leave the line as it was last drawn.  This block is
			// the last thing handleBuildPlacements does.
			if( !TheTacticalView->screenToTerrain( &screenStart, &worldStart ) ||
					!TheTacticalView->screenToTerrain( &screenEnd, &worldEnd ) )
				return;

			// both ends, so a wall lands on the grid and tiles from a grid square
			snapPlacementToGrid( &worldStart, m_pendingPlaceType, angle );
			snapPlacementToGrid( &worldEnd, m_pendingPlaceType, angle );

			// get the builder object that will be constructing things
			Object *builderObject = TheGameLogic->findObjectByID( TheInGameUI->getPendingPlaceSourceObjectID() );

			const Coord3D *positions;
			std::vector<Coord3D> rowPositions;
			if( lineBuild )
			{
				// how big are each of our objects
				Real objectSize = m_pendingPlaceType->getTemplateGeometryInfo().getMajorRadius() * 2.0f;

				//
				// given the start/end points in the world and the the angle of the wall, fill
				// out an array of positions that "tile" this wall across the landscape
				//
				BuildAssistant::TileBuildInfo *tileBuildInfo;
				tileBuildInfo = TheBuildAssistant->buildTiledLocations( m_pendingPlaceType, angle,
																																&worldStart, &worldEnd,
																																objectSize,
																																TheGlobalData->m_maxLineBuildObjects,
																																builderObject );
				positions = tileBuildInfo->positions;
				iconsUsed = tileBuildInfo->tilesUsed;
			}
			else
			{
				computePlacementRow( m_pendingPlaceType, angle, &worldStart, &worldEnd, &rowPositions );
				positions = &rowPositions[ 0 ];
				iconsUsed = (Int)rowPositions.size();
			}

			// create any necessary drawables we need to "fill out" the line
			for( i = 0; i < iconsUsed; i++ )
			{

				if( m_placeIcon[ i ] == NULL )
					m_placeIcon[ i ] = TheThingFactory->newDrawable( m_pendingPlaceType,
																													 DRAWABLE_STATUS_NO_STATE_PARTICLES );

			}  // end for i

			//
			// A row is judged piece by piece, on the frames the single ghost would have been: red
			// where the click will leave a gap, and the cursor says yes while any piece can go up.
			//
			const Bool judgeRow = row && ( TheGameClient->getFrame() & 0x1 );
			if( judgeRow )
			{
				TheTerrainVisual->removeAllBibs();
				m_placementLegal = FALSE;
			}

			//
			// march down each drawable and set the position based on its position in the
			// line and set their angles all the same
			//
			for( i = 0; i < iconsUsed; i++ )
			{

				// set the drawble position
				m_placeIcon[ i ]->setPosition( &positions[ i ] );

				// set opacity and shadowing for the drawble
				dressPlacementPreview( m_placeIcon[ i ] );

				// set the drawable angle
				m_placeIcon[ i ]->setOrientation( angle );

				if( judgeRow )
				{
					const Bool legal =
						TheBuildAssistant->isLocationLegalToBuild( &positions[ i ], m_pendingPlaceType, angle,
																											 placementCheckOptions(), builderObject,
																											 NULL ) == LBC_OK &&
						!overlapsPendingPlacement( &positions[ i ], m_pendingPlaceType, angle );
					m_placeIcon[ i ]->colorTint( legal ? NULL : &illegalBuildColor );
					if( legal )
						m_placementLegal = TRUE;
				}

			}  // end for i

		}  // end if

		//
		// destroy any drawables that we're not using anymore because a previous line length was
		// longer, or the row was let go of
		//
		for( Int i = iconsUsed; i < TheGlobalData->m_maxLineBuildObjects; i++ )
		{

			if( m_placeIcon[ i ] != NULL )
				TheGameClient->destroyDrawable( m_placeIcon[ i ] );
			m_placeIcon[ i ] = NULL;

		}  // end for i

	}  // end if

}  // end handleBuildPlacements

//-------------------------------------------------------------------------------------------------
/** Pre-draw phase of the in game ui */
//-------------------------------------------------------------------------------------------------
void InGameUI::preDraw( void )
{

	// handle any "icons" for the act of building things and placing them in the world
	handleBuildPlacements();

	// handle radius-cursors, if any
	handleRadiusCursor();

	// where the selection is headed, read fresh from the units themselves
	updateOrderHints();

	// the build grid under a structure waiting to be placed is not drawn here: it is terrain
	// geometry now, and HeightMapRenderObjClass::Render puts it down with the ground itself

	// draw the floating text first;
	drawFloatingText();

	// draw world animations
	updateAndDrawWorldAnimations();

}  // end preDraw

//-------------------------------------------------------------------------------------------------
/** Update the in game user interface */
//-------------------------------------------------------------------------------------------------
DECLARE_PERF_TIMER(InGameUI_update)
void InGameUI::update( void )
{
	USE_PERF_TIMER(InGameUI_update)

	updateReplaySeek();

	/// @todo make sure this code gets called even when the UI is not being drawn
	if ( m_videoStream && m_videoBuffer )
	{
		if ( m_videoStream->isFrameReady())
		{
			m_videoStream->frameDecompress();
			m_videoStream->frameRender( m_videoBuffer );
			m_videoStream->frameNext();
			if ( m_videoStream->frameIndex() == 0 )
			{
				stopMovie();
			}
		}
	}

	if ( m_cameoVideoStream && m_cameoVideoBuffer )
	{
		if ( m_cameoVideoStream->isFrameReady())
		{
			m_cameoVideoStream->frameDecompress();
			m_cameoVideoStream->frameRender( m_cameoVideoBuffer );
			m_cameoVideoStream->frameNext();
//			if ( m_cameoVideoStream->frameIndex() == 0 )
//			{
//				stopMovie();
//			}
		}
	}

	// the messages are lines of the event feed now, which drops them at drawing by their frame
	UnsignedInt currLogicFrame = TheGameLogic->getFrame();
	if( TheGameLogic->isInGame() && !TheGameLogic->isInShellGame() )
		watchDozers();
	updateSignalMarks();
	UnsignedByte r, g, b, a;
	Int amount;

	//
	// Update the Military Subtitle display
	//
	if( m_militarySubtitle )		// if we have a subtitle, work on it
	{
		// if the timeis frozen by a script, then we still want the text to display
		if(TheScriptEngine->isTimeFrozenScript())
		{
			//
			// These are LOGIC-frame counters walked down by hand to fake time passing while the
			// logic clock is frozen.  They cannot be gated on the logic frame - that is exactly
			// what is not advancing - and one step per call was one step per RENDER frame, so
			// with the renderer uncapped a cutscene's subtitles ran down (fps/30) times too fast.
			// The wall clock supplies the missing 30Hz: count the frames the freeze has lasted
			// so far and apply only the ones not applied yet, so nothing drifts however long it
			// runs.  See FINDINGS.md 7.2.
			//
			const UnsignedInt nowMs = timeGetTime();
			if( m_subtitleFreezeStartMs == 0 )
			{
				m_subtitleFreezeStartMs = nowMs;
				m_subtitleFreezeSteps = 0;
			}
			const UnsignedInt elapsedFrames = (nowMs - m_subtitleFreezeStartMs) * LOGICFRAMES_PER_SECOND / 1000;
			const UnsignedInt steps = elapsedFrames - m_subtitleFreezeSteps;
			m_subtitleFreezeSteps = elapsedFrames;
			m_militarySubtitle->lifetime -= min( steps, m_militarySubtitle->lifetime );
			m_militarySubtitle->blockBeginFrame -= min( steps, m_militarySubtitle->blockBeginFrame );
			m_militarySubtitle->incrementOnFrame -= min( steps, m_militarySubtitle->incrementOnFrame );
		}
		else
		{
			m_subtitleFreezeStartMs = 0;
		}
		// if it's time to remove the subtitle, Then remove it
		if((Int)m_militarySubtitle->lifetime < (Int)currLogicFrame)
		{
			//steal colins fade from above :)
			GameGetColorComponents( m_militarySubtitle->color, &r, &g, &b, &a );
			// start fading the alpha on this color down
			amount = REAL_TO_INT( ((currLogicFrame - m_militarySubtitle->lifetime ) * 0.1f) );
			if( a - amount < 0 )
			{
				removeMilitarySubtitle();
			}
			else
			{
				a -= amount;
				m_militarySubtitle->color = GameMakeColor(r, g, b, a);
			}
		}
		else
		{
			// trigger whether or not we should draw the block	
			if( m_militarySubtitle->blockBeginFrame + 9 < currLogicFrame )
			{
				m_militarySubtitle->blockBeginFrame = currLogicFrame;
				m_militarySubtitle->blockDrawn = !m_militarySubtitle->blockDrawn;
			}

			// If it's time to add another letter to the display string, lets do that.
			if( m_militarySubtitle->incrementOnFrame < currLogicFrame )
			{
				// first grab the letter we want to add
				WideChar tempWChar = m_militarySubtitle->subtitle.getCharAt(m_militarySubtitle->index);
				// if that letter is a return, add a new line
				if(tempWChar == L'\n')
				{
					// increment the Block position's Y value to draw it on the next line
					Int height;
					m_militarySubtitle->displayStrings[m_militarySubtitle->currentDisplayString]->getSize(NULL, &height);
					m_militarySubtitle->blockPos.y = m_militarySubtitle->blockPos.y + height;

					// Now add a new display string
					m_militarySubtitle->currentDisplayString++;
					if(!(m_militarySubtitle->currentDisplayString >= MAX_SUBTITLE_LINES) )
					{	
						m_militarySubtitle->blockPos.x = m_militarySubtitle->position.x;
						m_militarySubtitle->displayStrings[m_militarySubtitle->currentDisplayString] = TheDisplayStringManager->newDisplayString();
						m_militarySubtitle->displayStrings[m_militarySubtitle->currentDisplayString]->reset();
						m_militarySubtitle->displayStrings[m_militarySubtitle->currentDisplayString]->setFont(	TheFontLibrary->getFont( m_militaryCaptionFont, TheGlobalLanguageData->adjustFontSize(m_militaryCaptionPointSize), m_militaryCaptionBold ) )	;

						m_militarySubtitle->blockDrawn = TRUE;
						m_militarySubtitle->incrementOnFrame = currLogicFrame + (Int)(((Real)LOGICFRAMES_PER_SECOND * TheGlobalLanguageData->m_militaryCaptionDelayMS)/1000.0f);
					}
					else
					{
						// if we've exceeded the allocated number of display strings, this will force us to essentially truncate the remaining text
						//
						// roll the index back to the last real line. It used to be left at
						// MAX_SUBTITLE_LINES, and displayStrings only has that many entries, so the
						// draw loops (which run i <= currentDisplayString) read displayStrings[4]
						// and removeMilitarySubtitle then freed that out-of-bounds pointer.
						//
						m_militarySubtitle->currentDisplayString = MAX_SUBTITLE_LINES - 1;
						m_militarySubtitle->index = m_militarySubtitle->subtitle.getLength();
						DEBUG_CRASH(("You're Only Allowed to use %d lines of subtitle text\n",MAX_SUBTITLE_LINES));
					}
				}
				else
				{
					// okay, we're not a \n, lets append this character to the display string
					m_militarySubtitle->displayStrings[m_militarySubtitle->currentDisplayString]->appendChar(tempWChar);
					// increment the draw position of the block
					Int width;
					m_militarySubtitle->displayStrings[m_militarySubtitle->currentDisplayString]->getSize(&width,NULL);
					m_militarySubtitle->blockPos.x = m_militarySubtitle->position.x + width;

					// lets make a sound
					static AudioEventRTS click("MilitarySubtitlesTyping");
					TheAudio->addAudioEvent(&click);
					if(TheGlobalLanguageData)
						m_militarySubtitle->incrementOnFrame = currLogicFrame + TheGlobalLanguageData->m_militaryCaptionSpeed;
					else
						m_militarySubtitle->incrementOnFrame = currLogicFrame + m_militaryCaptionSpeed;

				}
				// increment the index			
				m_militarySubtitle->index++;
				if(m_militarySubtitle->index >= m_militarySubtitle->subtitle.getLength())
				{
					// We're at the end of the subtitle, set everything to persist till the subtitle has expired
					m_militarySubtitle->incrementOnFrame = m_militarySubtitle->lifetime + 1;
				}
	/*
							else
								{
									// randomize the space between printing of characters
									if(GameClientRandomValueReal(0,1) < 0.95f)
									{
										m_militarySubtitle->incrementOnFrame = GameClientRandomValue(2, 5) + currLogicFrame;
									}
									else
									{
										m_militarySubtitle->incrementOnFrame = GameClientRandomValue(10, 13) + currLogicFrame;
									}
								}*/
				
			}
		}
	}

	//
	// Update the player money window if the money amount has changed.  The amount last written is
	// a member and not a static inside this function: the gadget is thrown away and built again
	// whenever the command bar is, and a cache that outlives the window it was filled for left the
	// new one showing the dollar signs its .wnd ships with until the player's money next moved.
	//
	// This is also as good a place as any to do the power hide/show.
	//
	static NameKeyType moneyWindowKey = TheNameKeyGenerator->nameToKey( "ControlBar.wnd:MoneyDisplay" );	
	static NameKeyType powerWindowKey = TheNameKeyGenerator->nameToKey( "ControlBar.wnd:PowerWindow" );	

	GameWindow *moneyWin = TheWindowManager->winGetWindowFromId( NULL, moneyWindowKey );
	GameWindow *powerWin = TheWindowManager->winGetWindowFromId( NULL, powerWindowKey );
//	if( moneyWin == NULL )
//	{
//		NameKeyType moneyWindowKey = TheNameKeyGenerator->nameToKey( "ControlBar.wnd:MoneyDisplay" );	
//
//		moneyWin = TheWindowManager->winGetWindowFromId( NULL, moneyWindowKey );
//
//	}  // end if
	Player *moneyPlayer = NULL;
	if( TheControlBar->isObserverControlBarOn())
		moneyPlayer = TheControlBar->getObserverLookAtPlayer();
	else
		moneyPlayer = ThePlayerList->getLocalPlayer();
	if( moneyPlayer)
	{
		Int currentMoney = moneyPlayer->getMoney()->countMoney();

		if( m_lastMoneyDisplayed != currentMoney )
		{
			UnicodeString buffer;

			buffer.format( TheGameText->fetch( "GUI:ControlBarMoneyDisplay" ), currentMoney );
			GadgetStaticTextSetText( moneyWin, buffer );
			m_lastMoneyDisplayed = currentMoney;

		}  // end if

		//
		// These two are put back on screen every frame, which is how they survive the context
		// switching that hides everything else on the bar.  It also used to beat the minimised bar:
		// the radar and the whole middle panel went away and the money still sat there over the
		// battlefield, plate and all gone from under it.  The bar's own stage has the last word.
		//
		const Bool centreUp = ( TheControlBar == NULL ||
														!TheControlBar->isPanelHidden( ControlBar::CB_PANEL_CENTER ) );
		moneyWin->winHide( !centreUp );
		powerWin->winHide( !centreUp );
	}
	else
	{
		moneyWin->winHide(TRUE);
		powerWin->winHide(TRUE);
	}
	
	// Update the floating Text;
	updateFloatingText();

	// update the control bar
	TheControlBar->update();

	updateIdleWorker();

	// update any random window layout that so requests
	for (std::list<WindowLayout *>::iterator it = m_windowLayouts.begin(); it != m_windowLayouts.end(); ++it)
	{
		WindowLayout *layout = *it;
		layout->runUpdate();
	}

	//
	// Handle keyboard camera rotations.  These used to apply one whole step per call and this
	// update runs once per RENDER frame, so with the renderer uncapped the camera swung as fast
	// as the machine happened to draw - the same key held for the same time went a different
	// distance on every PC, and on a fast one a tap threw the view right past what you wanted.
	// The step sizes are written for the 30Hz logic rate, so scale them by how many of those
	// frames the wall clock says went by.  Capped at four, so coming back from a hitch or an
	// alt-tab does not fling the camera across the map on the first frame.  See FINDINGS.md 7.4.
	//
	const UnsignedInt cameraNowMs = timeGetTime();

	// a watcher's camera is driven for him while it is not in his own hands (ObserverCamera.h)
	if( TheGameLogic->isInGame() && localPlayerWatching() )
		TheObserverCamera.update( cameraNowMs );

	Real cameraSteps = 1.0f;
	if( m_cameraKeyLastMs != 0 )
		cameraSteps = (Real)(cameraNowMs - m_cameraKeyLastMs) * (LOGICFRAMES_PER_SECOND / 1000.0f);
	m_cameraKeyLastMs = cameraNowMs;
	if( cameraSteps > 4.0f )
		cameraSteps = 4.0f;

	//
	// SnapCameraRotateTo45 makes the heading discrete rather than smoothed-then-settled: the camera
	// stands on an eighth and jumps to the next one, so there is no in-between heading to look at
	// and nothing left to settle when the key comes up.  The angle is therefore not accumulated per
	// frame at all - a press turns the view one eighth and holding the key repeats that on a fixed
	// interval, which is what the rotate keys do in Stronghold.
	//
	const UnsignedInt CAMERA_SNAP_REPEAT_MS = 250;
	if( TheGlobalData->m_snapCameraRotateTo45 )
	{
		if( (m_cameraRotatingLeft || m_cameraRotatingRight) && m_cameraRotatingLeft != m_cameraRotatingRight
				&& cameraNowMs - m_cameraSnapRepeatMs >= CAMERA_SNAP_REPEAT_MS )
		{
			m_cameraSnapRepeatMs = cameraNowMs;
			TheTacticalView->setAngle( View_stepAngleByEighths( TheTacticalView->getAngle(),
																													m_cameraRotatingRight ? 1 : -1 ) );
		}
	}
	else
	{
		if( m_cameraRotatingLeft && !m_cameraRotatingRight )
		{
			//Keyboard rotate left
			TheTacticalView->setAngle( TheTacticalView->getAngle() - TheGlobalData->m_keyboardCameraRotateSpeed * cameraSteps );
		}
		if( m_cameraRotatingRight && !m_cameraRotatingLeft )
		{
			//Keyboard rotate right
			TheTacticalView->setAngle( TheTacticalView->getAngle() + TheGlobalData->m_keyboardCameraRotateSpeed * cameraSteps );
		}
	}
	if( m_cameraZoomingIn && !m_cameraZoomingOut )
	{
		//Keyboard zoom in
		TheTacticalView->zoomIn( cameraSteps );
	}
	if( m_cameraZoomingOut && !m_cameraZoomingIn )
	{
		//Keyboard zoom out
		TheTacticalView->zoomOut( cameraSteps );
	}

	//
	// Where this machine's mouse is, for the allies, and where theirs were, for this screen.  Here
	// rather than in preDraw because an ally's cursor is drawn twice: the patch of light goes down
	// in the terrain pass and the pointer over the top of it afterwards, and preDraw runs between
	// the two - so easing there moved the pointer a frame ahead of its own light, which on a fast
	// drag is a smear trailing the marker by an inch.
	//
	sendLocalAllyCursor();
	updateAllyCursors();

	sampleEarnings();

}  // end update

//-------------------------------------------------------------------------------------------------
void InGameUI::registerWindowLayout( WindowLayout *layout )
{
	unregisterWindowLayout(layout); // sanity
	m_windowLayouts.push_back(layout);
}

//-------------------------------------------------------------------------------------------------
void InGameUI::unregisterWindowLayout( WindowLayout *layout )
{
	for (std::list<WindowLayout *>::iterator it = m_windowLayouts.begin(); it != m_windowLayouts.end(); ++it)
	{
		if (*it == layout)
		{
			m_windowLayouts.erase(it);
			return;
		}
	}
}

//-------------------------------------------------------------------------------------------------
/** Reset the in game user interface */
//-------------------------------------------------------------------------------------------------
void InGameUI::reset( void )
{
	m_isQuitMenuVisible = FALSE;
	m_scoreboardOpen = FALSE;
	m_scoreboardPageLoaded = FALSE;
	m_scoreboardHtml.clear();
	m_earnedReadingCount = 0;		// a new match and a loaded save both come through here
	m_controlBarPageLoaded = FALSE;
	m_netPageLoaded = FALSE;
	m_tooltipPageLoaded = FALSE;
	m_promotionPageLoaded = FALSE;
	m_quitMenuPageLoaded = FALSE;
	m_signalsWereShown = FALSE;
	m_spectatorPageLoaded = FALSE;
	TheObserverCamera.reset();
	m_spectatorFlipped.clear();
	m_spectatorPicked.clear();
	m_spectatorLists.clear();
	m_spectatorTotals.clear();
	m_spectatorSuperweapons.clear();
	m_feedPageLoaded = FALSE;
	m_chatPageLoaded = FALSE;
	m_chatLines.clear();
	clearSignalMarks();
	m_armedSignal = SIGNAL_KIND_COUNT;
	m_dozerCheckFrame = 0;
	for( Int index = 0; index < MAX_PLAYER_COUNT; index++ )
		m_hadDozer[ index ] = FALSE;
	m_inputEnabled = true;
	// reset the command bar
	TheControlBar->reset();

	TheTacticalView->setDefaultView(0.0f, 0.0f, 1.0f);

	ResetInGameChat();

	// stop any movie currently playing
	stopMovie();

	// remove any pending GUI command
	setGUICommand( NULL );

	// remove any build available status
	placeBuildAvailable( NULL, NULL );
	m_placeAngleOffset = 0.0f;
	m_placeAngleType = NULL;
	forgetPendingPlacements();

	// free any message resources allocated
	freeMessageResources();

	Int i;
	for (i=0; i<MAX_PLAYER_COUNT; ++i)
	{
		for (SuperweaponMap::iterator mapIt = m_superweapons[i].begin(); mapIt != m_superweapons[i].end(); ++mapIt)
		{
			for (SuperweaponList::iterator listIt = mapIt->second.begin(); listIt != mapIt->second.end(); ++listIt)
			{
				SuperweaponInfo *info = *listIt;
				info->deleteInstance();
			}
			mapIt->second.clear();
		}
		m_superweapons[i].clear();
	}

	for (NamedTimerMapIt timerIt = m_namedTimers.begin(); timerIt != m_namedTimers.end(); ++timerIt)
	{
		NamedTimerInfo *info = timerIt->second;
		TheDisplayStringManager->freeDisplayString(info->displayString);
		info->deleteInstance();
	}
	m_namedTimers.clear();
	m_namedTimerLastFlashFrame = 0;
	m_namedTimerUsedFlashColor = TRUE; // so next one is false
	m_showNamedTimers = TRUE;

	removeMilitarySubtitle();
	clearPopupMessageData();
	m_superweaponLastFlashFrame = 0;
	m_superweaponUsedFlashColor = TRUE; // so next one is false
	m_superweaponHiddenByScript = FALSE;

	clearFloatingText();
	clearWorldAnimations();
	resetIdleWorker();

	m_waypointMode			= false;
	m_forceAttackMode		= false;
	m_forceMoveToMode		= false;
	m_attackMoveToMode	= false;
	m_forceAttackArmed	= false;
	m_guardArmed				= false;
	m_orderKeyKeptByShift	= false;
	m_preferSelection		= false;
	m_isAttackCircling	= FALSE;
	clearAllyCursors();
	m_clientQuiet    = false;

	// TheSuperHackers @bugfix A key or button still held when the game ended left its camera
	// interaction latched on, and the shell then scrolled or spun on its own.
	setScrolling(false);
	setSelecting(false);
	setCameraRotateLeft(false);
	setCameraRotateRight(false);
	setCameraZoomIn(false);
	setCameraZoomOut(false);
	setCameraTrackingDrawable(false);

	m_windowLayouts.clear();

	m_tooltipsDisabledUntil = 0;

	UpdateDiplomacyBriefingText(AsciiString::TheEmptyString, TRUE);
}  // end reset

//-------------------------------------------------------------------------------------------------
/** Empty the event feed: a new match, and the score screen, start without the last one's lines */
//-------------------------------------------------------------------------------------------------
void InGameUI::freeMessageResources( void )
{
	m_feedLines.clear();
}  // end freeMessageResources

//-------------------------------------------------------------------------------------------------
/** Same as the unicode message method, but this takes an ascii string which is assumed
	* to me a string manager label */
//-------------------------------------------------------------------------------------------------
// srj sez: passing as const-ref screws up varargs for some reason. dunno why. just pass by value.
void InGameUI::message( AsciiString stringManagerLabel, ... )
{
	UnicodeString stringManagerString;
	UnicodeString formattedMessage;

	// fetch the string from the string manger
	stringManagerString = TheGameText->fetch( stringManagerLabel.str() );

	// construct the final text after formatting
	va_list args;
  va_start( args, stringManagerLabel );
	WideChar buf[ UnicodeString::MAX_FORMAT_BUF_LEN ];
  // truncate rather than throw: an uncaught engine exception aborts with 0xC0000409 and no log
  // at all, so an over-long chat or script message used to be a silent hard crash.
  if( _vsnwprintf(buf, sizeof( buf )/sizeof( WideChar ) - 1, stringManagerString.str(), args ) < 0 )
			DEBUG_LOG(("InGameUI::message - text truncated to %d characters\n", (Int)(sizeof( buf )/sizeof( WideChar ) - 1)));
	buf[ sizeof( buf )/sizeof( WideChar ) - 1 ] = 0;
	formattedMessage.set( buf );
  va_end(args);

	// add the text to the ui
	addMessageText( formattedMessage );

}  // end 

//-------------------------------------------------------------------------------------------------
/** Interface for display text messages to the user */
//-------------------------------------------------------------------------------------------------
// srj sez: passing as const-ref screws up varargs for some reason. dunno why. just pass by value.
void InGameUI::message( UnicodeString format, ... )
{
	UnicodeString formattedMessage;

	// construct the final text after formatting
	va_list args;
  va_start( args, format );
	WideChar buf[ UnicodeString::MAX_FORMAT_BUF_LEN ];
  // truncate rather than throw: an uncaught engine exception aborts with 0xC0000409 and no log
  // at all, so an over-long chat or script message used to be a silent hard crash.
  if( _vsnwprintf(buf, sizeof( buf )/sizeof( WideChar ) - 1, format.str(), args ) < 0 )
			DEBUG_LOG(("InGameUI::message - text truncated to %d characters\n", (Int)(sizeof( buf )/sizeof( WideChar ) - 1)));
	buf[ sizeof( buf )/sizeof( WideChar ) - 1 ] = 0;
	formattedMessage.set( buf );
  va_end(args);

	// add the text to the ui
	addMessageText( formattedMessage );

}  // end message

//-------------------------------------------------------------------------------------------------
/** A message is a line of the event feed over the radar, Window/Html/Feed.html, the way the kill
	* feed runs in Dota.  It used to be a column of text written over the battlefield's top left. */
//-------------------------------------------------------------------------------------------------
void InGameUI::addMessageText( const UnicodeString& formattedMessage )
{
	HtmlValues line;
	line[ "kind" ] = "note";
	line[ "before" ] = WideCharStringToMultiByte( formattedMessage.str() );
	addFeedLine( line );
}

//-------------------------------------------------------------------------------------------------
/** A message about one player: his general's flag at its head and his name in his colour where the
	* text says it.  A chat line, a player beaten, a player gone. */
//-------------------------------------------------------------------------------------------------
void InGameUI::playerMessage( Player *player, const UnicodeString &text )
{
	HtmlValues line = spectatorHead( player );
	line[ "kind" ] = "player";
	line[ "portrait" ] = line[ "image" ];
	const std::string whole = WideCharStringToMultiByte( text.str() );
	const std::string name = WideCharStringToMultiByte( player->getPlayerDisplayName().str() );
	const size_t at = name.empty() ? std::string::npos : whole.find( name );
	if( at == std::string::npos )
	{
		line[ "before" ] = whole;
		line[ "name" ] = "";
	}
	else
	{
		line[ "before" ] = whole.substr( 0, at );
		line[ "name" ] = name;
		line[ "after" ] = whole.substr( at + name.size() );
	}
	addFeedLine( line );
}

//-------------------------------------------------------------------------------------------------
/** Is the local screen told what `player` did where nobody else is?  A watcher is told everything,
	* a player what he or a mutual ally did: an enemy's promotion or his power going up is not his to
	* know. */
//-------------------------------------------------------------------------------------------------
static Bool feedShows( const Player *player )
{
	const Player *local = ThePlayerList->getLocalPlayer();
	if( !local->isPlayerActive() || player == local )
		return TRUE;
	return player->getRelationship( local->getDefaultTeam() ) == ALLIES &&
				 local->getRelationship( player->getDefaultTeam() ) == ALLIES;
}

//-------------------------------------------------------------------------------------------------
/** A command button's name, its hotkey marker taken out: "&A-10 Strike" is "A-10 Strike". */
//-------------------------------------------------------------------------------------------------
static std::string buttonName( const CommandButton *button )
{
	std::string name = WideCharStringToMultiByte( TheGameText->fetch( button->getTextLabel() ).str() );
	name.erase( std::remove( name.begin(), name.end(), '&' ), name.end() );
	return name;
}

//-------------------------------------------------------------------------------------------------
/** A line about something one player did: his flag and his name, the `cameo` it is pictured by,
	* `what` it is called and the string table's `label` for what happened, coloured by `tag`. */
//-------------------------------------------------------------------------------------------------
void InGameUI::feedAct( Player *player, const Image *cameo, const std::string &what, const char *tag, const char *label )
{
	HtmlValues line = spectatorHead( player );
	line[ "kind" ] = "act";
	line[ "portrait" ] = line[ "image" ];
	line[ "image" ] = cameo ? cameo->getName().str() : "";
	line[ "what" ] = what;
	line[ "tag" ] = tag;
	line[ "tagtext" ] = WideCharStringToMultiByte( TheGameText->fetch( label ).str() );
	addFeedLine( line );
}

//-------------------------------------------------------------------------------------------------
/** A special power fired.  A superweapon with a countdown on everybody's screen is told to everybody,
	* the way EVA tells them; a general's power, one a promotion bought, only where feedShows says.  A
	* unit's own ability is neither and writes nothing. */
//-------------------------------------------------------------------------------------------------
void InGameUI::feedSpecialPower( const Object *source, const AsciiString &powerName, const SpecialPowerTemplate *power )
{
	Player *owner = source->getControllingPlayer();
	const CommandButton *button = powerButton( power );
	const Image *cameo = button ? button->getButtonImage() : NULL;
	const SuperweaponInfo *info = findSWInfo( owner->getPlayerIndex(), powerName, source->getID(), power );
	if( info != NULL )
	{
		if( !info->m_hiddenByScript && !info->m_hiddenByScience )
			feedAct( owner, cameo, WideCharStringToMultiByte( source->getTemplate()->getDisplayName().str() ),
							 "launched", "GUI:HudSuperweaponLaunched" );
		return;
	}
	if( power->getRequiredScience() == SCIENCE_INVALID || button == NULL || !feedShows( owner ) )
		return;
	feedAct( owner, cameo, buttonName( button ), "used", "GUI:HudPowerUsed" );
}

//-------------------------------------------------------------------------------------------------
/** A superweapon or an advanced tech building going up, or finished.  A finished superweapon is told
	* to everybody, the way EVA tells them; the rest only where feedShows says. */
//-------------------------------------------------------------------------------------------------
void InGameUI::feedStructure( Object *structure, Bool finished )
{
	const Bool superweapon = structure->isKindOf( KINDOF_FS_SUPERWEAPON );
	if( !superweapon && !structure->isKindOf( KINDOF_FS_ADVANCED_TECH ) )
		return;

	Player *owner = structure->getControllingPlayer();
	if( !( finished && superweapon ) && !feedShows( owner ) )
		return;
	feedAct( owner, structure->getTemplate()->getButtonImage(),
					 WideCharStringToMultiByte( structure->getTemplate()->getDisplayName().str() ),
					 finished ? "built" : "started", finished ? "GUI:HudStructureBuilt" : "GUI:HudStructureStarted" );
}

//-------------------------------------------------------------------------------------------------
/** A promotion bought: the power or the unit it opens, by the promotion screen's own button. */
//-------------------------------------------------------------------------------------------------
void InGameUI::feedScience( Player *player, ScienceType science )
{
	if( !feedShows( player ) )
		return;

	UnicodeString name, description;
	TheScienceStore->getNameAndDescription( science, name, description );
	const CommandButton *button = scienceButton( science );
	feedAct( player, button ? button->getButtonImage() : NULL, WideCharStringToMultiByte( name.str() ),
					 "unlocked", "GUI:HudPromotionBought" );
}

//-------------------------------------------------------------------------------------------------
/** Once a second: a player whose last dozer, or last worker for the GLA, has just gone gets a line,
	* since without one he can build nothing more.  Only where feedShows says, and not for a player
	* already beaten, whose last dozer went with everything else. */
//-------------------------------------------------------------------------------------------------
void InGameUI::watchDozers( void )
{
	const UnsignedInt frame = TheGameLogic->getFrame();
	if( frame < m_dozerCheckFrame + LOGICFRAMES_PER_SECOND && frame >= m_dozerCheckFrame )
		return;
	m_dozerCheckFrame = frame;

	KindOfMaskType dozers;
	dozers.set( KINDOF_DOZER );
	for( Int index = 0; index < ThePlayerList->getPlayerCount() && index < MAX_PLAYER_COUNT; index++ )
	{
		Player *player = ThePlayerList->getNthPlayer( index );
		const Bool has = player->isPlayableSide() && player->countObjects( dozers, KINDOFMASK_NONE ) > 0;
		const Bool lost = m_hadDozer[ index ] && !has;
		m_hadDozer[ index ] = has;
		if( !lost || TheVictoryConditions->hasSinglePlayerBeenDefeated( player ) || !feedShows( player ) )
			continue;

		const Bool workers = player->getPlayerTemplate()->getSide().startsWith( "GLA" );
		UnicodeString text;
		text.format( TheGameText->fetch( workers ? "GUI:HudNoWorkerLeft" : "GUI:HudNoDozerLeft" ), player->getPlayerDisplayName().str() );
		playerMessage( player, text );
	}
}

//-------------------------------------------------------------------------------------------------
/** One more line at the bottom of the feed, up for FEED_LINE_FRAMES; past FEED_HISTORY_KEPT the oldest
	* goes.  Every line is logged too, so a run can be read for what the feed said. */
//-------------------------------------------------------------------------------------------------
void InGameUI::addFeedLine( HtmlValues line )
{
	// written out first: reading the names fills in the ones a kind leaves out, so every line held
	// has all of them and two of the same compare equal
	const std::string logged = line[ "kind" ] + ": " + line[ "before" ] + line[ "name" ] + line[ "after" ] + " " +
														 line[ "what" ] + " " + line[ "tagtext" ];

	// one power fired from several of its owner's buildings on the same frame is one line, not three
	const UnsignedInt until = TheGameLogic->getFrame() + FEED_LINE_FRAMES;
	for( size_t each = 0; each < m_feedLines.size(); each++ )
		if( m_feedLines[ each ].until == until && m_feedLines[ each ].values == line )
			return;

	DEBUG_LOG(( "FEED frame %u %s\n", TheGameLogic->getFrame(), logged.c_str() ));
	FeedLine added;
	added.values = line;
	added.until = until;
	m_feedLines.push_back( added );
	if( m_feedLines.size() > FEED_HISTORY_KEPT )
		m_feedLines.erase( m_feedLines.begin() );
}

//-------------------------------------------------------------------------------------------------
/** Where the command bar starts on screen, or the screen's bottom while the bar is hidden: it is
	* hidden while the game loads and in the observer views. */
//-------------------------------------------------------------------------------------------------
static Int controlBarTop( void )
{
	static NameKeyType controlBarKey = TheNameKeyGenerator->nameToKey( "ControlBar.wnd:ControlBarParent" );
	GameWindow *bar = TheWindowManager->winGetWindowFromId( NULL, controlBarKey );
	if( bar == NULL || bar->winIsHidden() )
		return TheDisplay->getHeight();

	ICoord2D barPos;
	bar->winGetScreenPosition( &barPos.x, &barPos.y );
	return barPos.y;
}

static const char *const FEED_PAGE = "Window\\Html\\Feed.html";

//-------------------------------------------------------------------------------------------------
/** Where the feed stands on screen: the radar's under-attack tab, or the production queue's row
	* while that is up over the tab. */
//-------------------------------------------------------------------------------------------------
Int InGameUI::feedFloor( void ) const
{
	// ponytail: with no bar page the old queue column climbs the same corner under the feed; the
	// page always ships, so that column's top is not measured
	const Int floor = m_controlBarPageShown ? m_feedFloor : controlBarTop();
	return m_productionStripThemed && m_productionStripCount > 0 ? min( floor, m_queueTrayTop ) : floor;
}

//-------------------------------------------------------------------------------------------------
/** The event feed, Window/Html/Feed.html, for a player and a watcher alike: the newest line at the
	* bottom, standing on the radar's under-attack tab, or on the production queue's row while that is
	* up over the tab.  The last FEED_LINES_KEPT lines are up for FEED_LINE_FRAMES each, and while the
	* chat is open every line held is, so Enter shows what was missed.  The messages switch
	* (toggleMessages) takes the whole feed away. */
//-------------------------------------------------------------------------------------------------
void InGameUI::drawFeed( void )
{
	// a frame going backwards is a loaded save, and a line from its future is not from this game
	const UnsignedInt frame = TheGameLogic->getFrame();
	for( size_t line = 0; line < m_feedLines.size(); )
		if( frame + FEED_LINE_FRAMES < m_feedLines[ line ].until )
			m_feedLines.erase( m_feedLines.begin() + line );
		else
			line++;

	const Bool history = IsInGameChatActive();
	const size_t shownFrom = history ? 0 : m_feedLines.size() - min( m_feedLines.size(), (size_t)FEED_LINES_KEPT );
	HtmlLists lists;
	std::vector< HtmlValues > &lines = lists[ "feed" ];
	for( size_t line = shownFrom; line < m_feedLines.size(); line++ )
		if( history || frame < m_feedLines[ line ].until )
			lines.push_back( m_feedLines[ line ].values );
	if( lines.empty() || !m_messagesOn || !TheGameLogic->isInGame() || TheGameLogic->isInShellGame() )
		return;

	if( !m_feedPageLoaded )
	{
		m_feedPageLoaded = TRUE;
		readHtmlPage( FEED_PAGE, m_feedPage );
	}
	if( m_feedPage.empty() )
		return;
	if( m_feedOverlay == NULL )
		m_feedOverlay = new HtmlOverlay( m_superweaponNormalFont );

	HtmlValues values;
	values[ "floor" ] = std::to_string( REAL_TO_INT_FLOOR( feedFloor() / ControlBarUniformScale() ) );
	m_feedOverlay->setPage( HtmlTemplate_expand( m_feedPage, values, lists, lookupGameText ) );
	m_feedOverlay->draw();
}

static const char *const CHAT_PAGE = "Window\\Html\\Chat.html";

enum
{
	CHAT_LINE_FRAMES		= LOGICFRAMES_PER_SECOND * 10,	///< how long a chat line stays up with the chat shut
	CHAT_FADE_FRAMES		= LOGICFRAMES_PER_SECOND * 3 / 2,	///< the last of that, over which the shut chat fades out
	OPAQUE_PAGE					= 255,	///< HtmlOverlay::setAlpha's full strength
	CHAT_LINES_KEPT			= 8,		///< the most lines held, all of them shown while the chat is open
	CHAT_WIDTH					= 360,	///< the chat's width, 800x600
	CHAT_BAR_HEIGHT			= 22,		///< the typing bar's height with its padding and border, 800x600
	FEED_LINE_HEIGHT		= 16,		///< a line of Feed.html with the pixel over it, 800x600
	FEED_FOOT						= 3,		///< Feed.html's gap under its newest line, 800x600
	CHAT_OVER_FEED			= 12,		///< the gap between the typing bar and the full feed under it, 800x600
	CHAT_CARET_FRAMES		= LOGICFRAMES_PER_SECOND / 2	///< the caret's blink, on and off
};

//-------------------------------------------------------------------------------------------------
/** A line of chat from `player`: his flag, his name in his colour, what he said.  It goes into the
	* chat under the middle of the screen, Dota's way, not into the feed. */
//-------------------------------------------------------------------------------------------------
void InGameUI::chatMessage( Player *player, const UnicodeString &text )
{
	FeedLine line;
	line.values = spectatorHead( player );
	line.values[ "text" ] = WideCharStringToMultiByte( text.str() );
	line.until = TheGameLogic->getFrame() + CHAT_LINE_FRAMES;
	DEBUG_LOG(( "CHAT frame %u %s: %s\n", TheGameLogic->getFrame(), line.values[ "name" ].c_str(), line.values[ "text" ].c_str() ));
	m_chatLines.push_back( line );
	if( m_chatLines.size() > CHAT_LINES_KEPT )
		m_chatLines.erase( m_chatLines.begin() );
}

//-------------------------------------------------------------------------------------------------
/** The chat, Window/Html/Chat.html, in the left corner over the feed, clear of it even with all
	* FEED_LINES_KEPT of its lines up so the chat does not move as the feed grows, and open, clear of
	* the feed's whole history under it too: shut, the talk round its newest line, fading out
	* together; open, the typing bar under every line held, the chat's own windows moved under it so
	* they take the keys and the clicks there and draw nothing. */
//-------------------------------------------------------------------------------------------------
void InGameUI::drawChat( void )
{
	if( !TheGameLogic->isInGame() || TheGameLogic->isInShellGame() )
		return;

	const Real scale = ControlBarUniformScale();
	const size_t feedLines = max( (size_t)FEED_LINES_KEPT, IsInGameChatActive() ? m_feedLines.size() : 0 );
	const Int bar = REAL_TO_INT_FLOOR( feedFloor() / scale ) - (Int)feedLines * FEED_LINE_HEIGHT - FEED_FOOT - CHAT_OVER_FEED -
									CHAT_BAR_HEIGHT;

	UnicodeString typed, audience;
	const Bool open = GetInGameChatEntry( typed, audience, 0, REAL_TO_INT_FLOOR( bar * scale ),
																				REAL_TO_INT_CEIL( CHAT_WIDTH * scale ), REAL_TO_INT_CEIL( CHAT_BAR_HEIGHT * scale ) );

	// shut, the chat is the talk around its newest line: every line that came within CHAT_LINE_FRAMES
	// before it, up until CHAT_LINE_FRAMES after it and fading out together over the last
	// CHAT_FADE_FRAMES of that
	const UnsignedInt frame = TheGameLogic->getFrame();
	const UnsignedInt newestUntil = m_chatLines.empty() ? 0 : m_chatLines.back().until;
	if( !open && ( frame >= newestUntil || frame + CHAT_LINE_FRAMES < newestUntil ) )
		return;
	HtmlLists lists;
	std::vector< HtmlValues > &lines = lists[ "chat" ];
	for( size_t line = 0; line < m_chatLines.size(); line++ )
		if( open || m_chatLines[ line ].until + CHAT_LINE_FRAMES >= newestUntil )
			lines.push_back( m_chatLines[ line ].values );

	if( !m_chatPageLoaded )
	{
		m_chatPageLoaded = TRUE;
		readHtmlPage( CHAT_PAGE, m_chatPage );
	}
	if( m_chatPage.empty() )
		return;
	if( m_chatOverlay == NULL )
		m_chatOverlay = new HtmlOverlay( m_superweaponNormalFont );

	HtmlValues values;
	values[ "bar" ] = std::to_string( bar );
	values[ "open" ] = open ? "open" : "";
	values[ "typed" ] = WideCharStringToMultiByte( typed.str() );
	values[ "caret" ] = frame / CHAT_CARET_FRAMES % 2 == 0 ? "lit" : "";
	values[ "audience" ] = WideCharStringToMultiByte( audience.str() );
	m_chatOverlay->setPage( HtmlTemplate_expand( m_chatPage, values, lists, lookupGameText ) );
	m_chatOverlay->setAlpha( open ? OPAQUE_PAGE : min( (Int)OPAQUE_PAGE, (Int)( newestUntil - frame ) * OPAQUE_PAGE / CHAT_FADE_FRAMES ) );
	m_chatOverlay->draw();
}

//-------------------------------------------------------------------------------------------------
/** An area selection is occurring, start graphical "hint". */
//-------------------------------------------------------------------------------------------------
void InGameUI::beginAreaSelectHint( const GameMessage *msg )
{
	m_isDragSelecting = true;
	m_dragSelectRegion = msg->getArgument( 0 )->pixelRegion;
}

//-------------------------------------------------------------------------------------------------
/** An area selection has occurred, finish graphical "hint". */
//-------------------------------------------------------------------------------------------------
void InGameUI::endAreaSelectHint( const GameMessage *msg )
{
	m_isDragSelecting = false;
}

//-------------------------------------------------------------------------------------------------
/** The player is dragging out the line a formation move will spread along. */
//-------------------------------------------------------------------------------------------------
void InGameUI::addFormationDragPoint( const ICoord2D& pt )
{
	m_isFormationDragging = TRUE;

	if( m_formationDragPoints.empty() )
	{
		m_formationDragSpacing = FORMATION_DRAG_MIN_SPACING;
		m_formationDragPoints.push_back( pt );
		updateFormationHints();
		return;
	}

	// The last point always follows the cursor, so the end of the line sits exactly where the hand
	// is.  A new point is only kept once the cursor has moved far enough for the curve to be saying
	// something; without that a slow drag stores one point a frame and fills the message with noise.
	Bool keep = TRUE;
	const Int keptIndex = (Int)m_formationDragPoints.size() - 2;
	if( keptIndex >= 0 )
	{
		const Int dx = pt.x - m_formationDragPoints[ keptIndex ].x;
		const Int dy = pt.y - m_formationDragPoints[ keptIndex ].y;
		keep = ( dx * dx + dy * dy >= m_formationDragSpacing * m_formationDragSpacing );
	}

	if( keep )
	{
		// A long drag would otherwise run out of points, and everything after that would collapse
		// into one straight rubber band from the last kept point to the cursor - which is the kink
		// that showed up in play.  Halve the resolution instead: throw away every second point and
		// double the spacing.  The shape survives at a coarser step and the line keeps growing for
		// as long as the player keeps dragging.
		if( (Int)m_formationDragPoints.size() >= MAX_FORMATION_DRAG_POINTS )
		{
			std::vector<ICoord2D> thinned;
			for( Int i = 0; i < (Int)m_formationDragPoints.size(); i += 2 )
				thinned.push_back( m_formationDragPoints[ i ] );
			m_formationDragPoints = thinned;
			m_formationDragSpacing *= 2;
		}

		m_formationDragPoints.push_back( pt );
	}
	else
	{
		m_formationDragPoints.back() = pt;
	}

	updateFormationHints();
}

//-------------------------------------------------------------------------------------------------
// Ally cursors.  Ten reports a second, on a network command of their own that carries two floats
// and is never acked, resent or written to a replay - the simulation cannot see any of this, so
// none of it can change what a match does.
//
// A cursor that has not been heard from for ALLY_CURSOR_HOLD_MS starts fading and is gone by
// ALLY_CURSOR_GONE_MS, which is what an ally alt-tabbing away or dropping out looks like.
//-------------------------------------------------------------------------------------------------
static const UnsignedInt ALLY_CURSOR_SEND_INTERVAL_MS = 100;		///< ten a second, which reads as continuous once eased
static const UnsignedInt ALLY_CURSOR_IDLE_RESEND_MS = 1000;		///< a still mouse still says so, or it would fade out
static const UnsignedInt ALLY_CURSOR_HOLD_MS = 2500;					///< heard from this recently, drawn at full strength
static const UnsignedInt ALLY_CURSOR_GONE_MS = 3500;					///< and faded out entirely by here
static const Real ALLY_CURSOR_MOVED_DIST = 1.0f;							///< world units a cursor has to have moved to be worth a packet
static const Real ALLY_CURSOR_EASE_MS = 110.0f;								///< how long the marker takes to catch up with a new report
static const Real ALLY_CURSOR_JUMP_DIST = 400.0f;							///< further than this and it snaps instead: a camera jump is not a mouse move

//-------------------------------------------------------------------------------------------------
void InGameUI::clearAllyCursors( void )
{
	for( Int i = 0; i < MAX_PLAYER_COUNT; ++i )
	{
		m_allyCursors[ i ].position.zero();
		m_allyCursors[ i ].shown.zero();
		m_allyCursors[ i ].heardMs = 0;
		m_allyCursors[ i ].known = FALSE;
	}
	m_allyCursorSentMs = 0;
	m_allyCursorSentPosition.zero();
	m_allyCursorEasedMs = 0;
}

//-------------------------------------------------------------------------------------------------
/** One ally's mouse, as they last reported it.  The height is not on the wire: the terrain both
	* machines loaded answers that, and reading it here means a cursor over a hill sits on the hill
	* rather than at whatever height the sender's own camera happened to make of it. */
//-------------------------------------------------------------------------------------------------
void InGameUI::noteAllyCursor( Int playerIndex, Real x, Real y )
{
	if( !isValidPlayerIndex( playerIndex ) )
		return;

	//
	// The sender addresses these to its allies, but the relay mask is not what decides who reads
	// one: the packet router processes every command that passes through it on its way to somebody
	// else, so without this the player holding that seat would watch the enemy's mouse.  The check
	// belongs here anyway - a machine that has been made to send its cursor to everyone gets the
	// same answer, because whether two players are allied is not the sender's to assert.
	//
	if( !isAllyOfLocalPlayer( playerIndex ) )
		return;

	AllyCursor& cursor = m_allyCursors[ playerIndex ];

	cursor.position.x = x;
	cursor.position.y = y;
	cursor.position.z = TheTerrainLogic->getGroundHeight( x, y );
	cursor.heardMs = timeGetTime();

	// the first report of a match arrives wherever that ally is looking, which is nowhere near the
	// origin the marker starts at - easing in from there would draw a line across the whole map
	if( cursor.known == FALSE )
	{
		cursor.shown = cursor.position;
		cursor.known = TRUE;
		DEBUG_LOG(("ALLYCURSOR: first report from player %d at %.1f,%.1f\n", playerIndex, x, y));
	}
}

//-------------------------------------------------------------------------------------------------
/** Is this player mutually allied with the one sitting at this machine.  One-sided alliances are
	* not a partnership: a player who has declared for somebody who has not declared back does not get
	* to watch them work. */
//-------------------------------------------------------------------------------------------------
Bool InGameUI::isAllyOfLocalPlayer( Int playerIndex ) const
{
	const Player *localPlayer = ThePlayerList->getLocalPlayer();
	const Player *player = ThePlayerList->getNthPlayer( playerIndex );
	if( localPlayer == NULL || player == NULL || player == localPlayer )
		return FALSE;

	if( !localPlayer->isPlayerActive() || !player->isPlayerActive() )
		return FALSE;

	return player->getRelationship( localPlayer->getDefaultTeam() ) == ALLIES &&
				 localPlayer->getRelationship( player->getDefaultTeam() ) == ALLIES;
}

//-------------------------------------------------------------------------------------------------
/** How strongly an ally's cursor should be drawn, 0 for one that should not be drawn at all. */
//-------------------------------------------------------------------------------------------------
Real InGameUI::getAllyCursorFade( Int playerIndex ) const
{
	if( !isValidPlayerIndex( playerIndex ) )
		return 0.0f;

	const AllyCursor& cursor = m_allyCursors[ playerIndex ];
	if( cursor.known == FALSE )
		return 0.0f;

	const UnsignedInt ageMs = timeGetTime() - cursor.heardMs;
	if( ageMs >= ALLY_CURSOR_GONE_MS )
		return 0.0f;
	if( ageMs <= ALLY_CURSOR_HOLD_MS )
		return 1.0f;

	return 1.0f - (Real)( ageMs - ALLY_CURSOR_HOLD_MS ) / (Real)( ALLY_CURSOR_GONE_MS - ALLY_CURSOR_HOLD_MS );
}

//-------------------------------------------------------------------------------------------------
/** Walk every marker towards the spot its owner last reported.  Ten reports a second drawn raw
	* would step; eased over about a tenth of a second they read as one hand moving.  Wall clock
	* rather than frames, because the picture is uncapped. */
//-------------------------------------------------------------------------------------------------
void InGameUI::updateAllyCursors( void )
{
	const UnsignedInt nowMs = timeGetTime();
	const UnsignedInt elapsedMs = ( m_allyCursorEasedMs == 0 ) ? 0 : ( nowMs - m_allyCursorEasedMs );
	m_allyCursorEasedMs = nowMs;

	Real step = (Real)elapsedMs / ALLY_CURSOR_EASE_MS;
	if( step > 1.0f )
		step = 1.0f;

	for( Int i = 0; i < MAX_PLAYER_COUNT; ++i )
	{
		AllyCursor& cursor = m_allyCursors[ i ];
		if( cursor.known == FALSE )
			continue;

		Coord3D delta = cursor.position;
		delta.sub( &cursor.shown );

		if( delta.length() > ALLY_CURSOR_JUMP_DIST )
		{
			cursor.shown = cursor.position;
			continue;
		}

		delta.scale( step );
		cursor.shown.add( &delta );
	}
}

//-------------------------------------------------------------------------------------------------
/** The slots this machine is mutually allied with, its own left out.  An observer is allied with
	* nobody, so the mask comes back empty and nothing is sent. */
//-------------------------------------------------------------------------------------------------
Int InGameUI::allyPlayerMask( void ) const
{
	Int mask = 0;
	for( Int slot = 0; slot < MAX_SLOTS; ++slot )
	{
		// the network's slot numbers and the player list's indices are two different numberings, and
		// "player<slot>" is the name that joins them - the same lookup in-game chat uses
		AsciiString playerName;
		playerName.format( "player%d", slot );
		const Player *player = ThePlayerList->findPlayerWithNameKey( TheNameKeyGenerator->nameToKey( playerName ) );
		if( player != NULL && isAllyOfLocalPlayer( player->getPlayerIndex() ) )
			mask |= ( 1 << slot );
	}

	return mask;
}

//-------------------------------------------------------------------------------------------------
/** Tell the allies where this machine's mouse is pointing, at most ten times a second, and only
	* while it is over the map.  A mouse that has not moved still reports once a second, or the
	* marker on the other screens would fade out while its owner sat thinking. */
//-------------------------------------------------------------------------------------------------
void InGameUI::sendLocalAllyCursor( void )
{
	if( !TheGlobalData->m_showAllyCursors )
		return;

	// TheNetwork is the whole test for "is this a game with other people in it"
	if( TheNetwork == NULL || !TheGameLogic->isInGame() || TheGameLogic->isInShellGame() )
		return;

	const UnsignedInt nowMs = timeGetTime();
	if( m_allyCursorSentMs != 0 && ( nowMs - m_allyCursorSentMs ) < ALLY_CURSOR_SEND_INTERVAL_MS )
		return;

	// a pointer over the command bar or off the top of the map has no map position, and a marker
	// parked on the last piece of ground it crossed on the way there would be a lie
	const MouseIO *mouse = TheMouse->getMouseStatus();
	Coord3D world;
	if( !TheTacticalView->screenToTerrain( &mouse->pos, &world ) )
		return;

	Coord3D moved = world;
	moved.sub( &m_allyCursorSentPosition );
	if( m_allyCursorSentMs != 0 && moved.length() < ALLY_CURSOR_MOVED_DIST &&
			( nowMs - m_allyCursorSentMs ) < ALLY_CURSOR_IDLE_RESEND_MS )
		return;

	const Int mask = allyPlayerMask();
	if( mask == 0 )
		return;

	TheNetwork->sendAllyCursor( world.x, world.y, mask );

	m_allyCursorSentMs = nowMs;
	m_allyCursorSentPosition = world;
}

//-------------------------------------------------------------------------------------------------
/** Work out who is going where, from the curve as it stands.  This is the same arithmetic the logic
	* runs when the button comes up - same curve, same filter, same total order - so what the player
	* is shown is what the units will do. */
//-------------------------------------------------------------------------------------------------
void InGameUI::updateFormationHints( void )
{
	// last frame's markers, kept only so this frame's can inherit their age (see addOrderHint)
	std::vector<OrderHint> previous;
	previous.swap( m_orderHints );

	if( m_formationDragPoints.size() < 2 )
		return;

	std::vector<Coord3D> path;
	for( std::vector<ICoord2D>::const_iterator pt = m_formationDragPoints.begin();
			 pt != m_formationDragPoints.end(); ++pt )
	{
		Coord3D world;
		TheTacticalView->screenToTerrain( &(*pt), &world );
		path.push_back( world );
	}

	std::vector<Real> arc;
	buildPathArcLengths( path, arc );

	const Real span = arc.back();
	if( span < 1.0f )
		return;

	std::vector<Object *> movers;
	for( DrawableList::const_iterator it = m_selectedDrawables.begin();
			 it != m_selectedDrawables.end(); ++it )
	{
		Object *obj = (*it)->getObject();
		if( !obj || obj->getControllingPlayer() != ThePlayerList->getLocalPlayer() )
			continue;
		if( obj->isKindOf( KINDOF_IMMOBILE ) || obj->getAIUpdateInterface() == NULL )
			continue;
		movers.push_back( obj );
	}

	if( movers.empty() )
		return;

	orderAlongPath( movers, path, arc );

	const Int count = movers.size();
	for( Int i = 0; i < count; i++ )
	{
		OrderHint hint;
		hint.kind = isInAttackMoveToMode() ? ORDER_HINT_ATTACK_MOVE
							: isForceAttackArmed() ? ORDER_HINT_ATTACK
							: isGuardArmed() ? ORDER_HINT_GUARD
							: ORDER_HINT_MOVE;
		hint.owner = movers[ i ]->getID();
		hint.from = *movers[ i ]->getPosition();
		pointAlongPath( path, arc, span * ((count == 1) ? 1.0f : ((Real)i / (Real)(count - 1))),
										&hint.to );
		hint.to.z = TheTerrainLogic->getGroundHeight( hint.to.x, hint.to.y );
		addOrderHint( hint, previous );
	}
}

//-------------------------------------------------------------------------------------------------
/** The order an aircraft has taken but not started.  A plane on the ground answers a move or an
	* attack by handing its state machine to the takeoff sequence and putting the order aside, so
	* neither the state id nor the goal names the thing the player pointed at until it is flying. */
//-------------------------------------------------------------------------------------------------
Bool InGameUI::getHeldAircraftOrder( const Object *obj, OrderHintKind& kind, Coord3D& to ) const
{
	const AIUpdateInterface *ai = obj->getAIUpdateInterface();
	const JetAIUpdate *jet = ai->getJetAIUpdate();
	if( jet == NULL )
		return FALSE;

	ObjectID targetID = INVALID_ID;
	Coord3D targetPos;
	targetPos.zero();

	switch( jet->friend_getHeldOrder( targetID, targetPos ) )
	{
		case AICMD_ATTACK_OBJECT:
			kind = ORDER_HINT_ATTACK;
			break;

		case AICMD_FORCE_ATTACK_OBJECT:
			kind = ORDER_HINT_FORCE_ATTACK;
			break;

		case AICMD_ATTACK_POSITION:
		case AICMD_ATTACK_AREA:
			kind = ORDER_HINT_ATTACK_GROUND;
			break;

		case AICMD_ATTACKMOVE_TO_POSITION:
			kind = ORDER_HINT_ATTACK_MOVE;
			break;

		case AICMD_ENTER:
		case AICMD_GET_REPAIRED:
			kind = ORDER_HINT_ENTER;
			break;

		// a queued path is held as a coordinate list the storage does not hand back, so a shift move
		// given to a parked plane stays invisible until it flies.  Single-point orders are the ones
		// worth drawing here
		case AICMD_MOVE_TO_POSITION:
		case AICMD_MOVE_TO_OBJECT:
			kind = ORDER_HINT_MOVE;
			break;

		default:
			// everything else the aircraft holds is its own housekeeping, not a player order
			return FALSE;
	}

	if( targetID != INVALID_ID )
	{
		const Object *target = TheGameLogic->findObjectByID( targetID );
		if( target == NULL )
			return FALSE;
		to = *target->getPosition();
	}
	else
	{
		to = targetPos;
	}

	return TRUE;
}

//-------------------------------------------------------------------------------------------------
/** The spot a guarding unit is holding.  A guard order clears the state machine before it starts,
	* so the machine's goal position is the origin and reading it drew every guard marker in the
	* bottom left corner of the map.  What the unit is guarding is kept on the AI itself. */
//-------------------------------------------------------------------------------------------------
static Bool getGuardedSpot( const AIUpdateInterface *ai, Coord3D& spot )
{
	switch( ai->getGuardTargetType() )
	{
		case GUARDTARGET_LOCATION:
			spot = *ai->getGuardLocation();
			return TRUE;

		case GUARDTARGET_OBJECT:
		{
			const Object *guarded = TheGameLogic->findObjectByID( ai->getGuardObject() );
			if( guarded == NULL )
				return FALSE;
			spot = *guarded->getPosition();
			return TRUE;
		}

		case GUARDTARGET_AREA:
		{
			const PolygonTrigger *area = ai->getAreaToGuard();
			if( area == NULL )
				return FALSE;
			area->getCenterPoint( &spot );
			return TRUE;
		}

		default:
			return FALSE;
	}
}

//-------------------------------------------------------------------------------------------------
/** Where each selected unit is going, asked of the unit rather than remembered from the order.  A
	* line therefore lives exactly as long as the order behind it: it appears the frame the unit
	* accepts the command and goes when the unit arrives, changes its mind or leaves the selection. */
//-------------------------------------------------------------------------------------------------
void InGameUI::updateOrderHints( void )
{
	// a formation line being drawn shows every unit's own station, which is the whole point of it
	if( m_isFormationDragging )
	{
		updateFormationHints();
		m_drawnOrderHints = m_orderHints;
		return;
	}

	collectOrderHints();
	numberOrderHints();
	bunchOrderHints();
}

//-------------------------------------------------------------------------------------------------
/** One hint per selected unit, and one more per queued point it still owes. */
//-------------------------------------------------------------------------------------------------
void InGameUI::collectOrderHints( void )
{
	// last frame's markers, kept only so this frame's can inherit their age (see addOrderHint)
	std::vector<OrderHint> previous;
	previous.swap( m_orderHints );

	Player *local = ThePlayerList->getLocalPlayer();
	for( DrawableList::const_iterator it = m_selectedDrawables.begin();
			 it != m_selectedDrawables.end(); ++it )
	{
		Object *obj = (*it)->getObject();
		if( !obj || obj->getControllingPlayer() != local )
			continue;

		// an angry mob's members each chase a spot round the nexus, which holds the real order, so
		// only the nexus draws: one line for the mob rather than one per rioter
		if( obj->isKindOf( KINDOF_IGNORED_IN_GUI ) )
			continue;

		AIUpdateInterface *ai = obj->getAIUpdateInterface();
		if( ai == NULL )
			continue;

		OrderHint hint;
		hint.owner = obj->getID();
		Coord3D resolvedGoal;
		Bool goalResolved = FALSE;
		Bool legsDrawn = FALSE;		// the order drew its own threads, and the shift list follows on from them
		switch( ai->getCurrentStateID() )
		{
			case AI_MOVE_TO:
			case AI_FOLLOW_PATH:
			case AI_FOLLOW_EXITPRODUCTION_PATH:
			case AI_MOVE_AND_EVACUATE:
			case AI_MOVE_AND_EVACUATE_AND_EXIT:
			case AI_MOVE_AND_DELETE:
			case AI_MOVE_AND_TIGHTEN:
			case AI_PICK_UP_CRATE:
				hint.kind = ORDER_HINT_MOVE;
				break;

			case AI_FOLLOW_WAYPOINT_PATH_AS_TEAM:
			case AI_FOLLOW_WAYPOINT_PATH_AS_INDIVIDUALS:
			case AI_FOLLOW_WAYPOINT_PATH_AS_TEAM_EXACT:
			case AI_FOLLOW_WAYPOINT_PATH_AS_INDIVIDUALS_EXACT:
				hint.kind = ORDER_HINT_WAYPOINT;
				break;

			// An attack move and a shot at the ground both end on the point that was clicked, which the
			// order keeps as its goal position.  The end of the unit's path is somewhere else: a free
			// cell next to it for a mover, the centre of a cell in range for a gun.  The attack move's
			// path is also thrown away every time the unit stops to fight, so reading the path left the
			// thread pointing at whatever the fight was about
			case AI_ATTACK_MOVE_TO:
				hint.kind = ORDER_HINT_ATTACK_MOVE;
				resolvedGoal = *ai->getGoalPosition();
				goalResolved = TRUE;
				break;

			case AI_ATTACKFOLLOW_WAYPOINT_PATH_AS_INDIVIDUALS:
			case AI_ATTACKFOLLOW_WAYPOINT_PATH_AS_TEAM:
			case AI_HUNT:
				hint.kind = ORDER_HINT_ATTACK_MOVE;
				break;

			case AI_ATTACK_OBJECT:
			case AI_ATTACK_AND_FOLLOW_OBJECT:
			case AI_ATTACK_SQUAD:
				hint.kind = ORDER_HINT_ATTACK;
				break;

			case AI_FORCE_ATTACK_OBJECT:
				hint.kind = ORDER_HINT_FORCE_ATTACK;
				break;

			case AI_ATTACK_POSITION:
				hint.kind = ORDER_HINT_ATTACK_GROUND;
				resolvedGoal = *ai->getGoalPosition();
				goalResolved = TRUE;
				break;

			case AI_ATTACK_AREA:
				hint.kind = ORDER_HINT_ATTACK_GROUND;
				break;

			case AI_ENTER:
			{
				// An order that is going through the tunnels is still the order it was: one thread in its
				// colour to the mouth it goes down and one from the mouth it comes up at to where it was
				// sent.  The far mouth is worked out the way the unit will work it out when it gets there.
				const Object *entrance = ai->getGoalObject();
				if( ai->hasTunnelTrip() && entrance != NULL )
				{
					hint.kind = ( ai->getTunnelTripEnd() == TUNNEL_TRIP_ATTACK_MOVE ) ? ORDER_HINT_ATTACK_MOVE : ORDER_HINT_MOVE;
					hint.from = *obj->getPosition();
					hint.to = *entrance->getPosition();
					addOrderHint( hint, previous );

					const Object *exit = local->getTunnelSystem()->findQuietTunnelNear( ai->getTunnelTripGoal() );
					hint.from = ( exit != NULL ) ? *exit->getPosition() : hint.to;
					hint.to = *ai->getTunnelTripGoal();
					addOrderHint( hint, previous );
					legsDrawn = TRUE;
					break;
				}
				hint.kind = ORDER_HINT_ENTER;
				break;
			}

			case AI_RAPPEL_INTO:
			case AI_COMBATDROP:
				hint.kind = ORDER_HINT_ENTER;
				break;

			case AI_DOCK:
				hint.kind = ORDER_HINT_DOCK;
				break;

			case AI_GET_REPAIRED:
				hint.kind = ORDER_HINT_GET_REPAIRED;
				break;

			case AI_HACK_INTERNET:
				hint.kind = ORDER_HINT_HACK;
				break;

			case AI_GUARD:
			case AI_GUARD_RETALIATE:
			case AI_GUARD_TUNNEL_NETWORK:
			{
				// A guard order clears the state machine on its way in, so the goal position it leaves
				// behind is the origin - and the marker landed in the bottom left corner of the map
				// every time.  The spot being guarded lives on the AI itself, so ask it there.
				hint.kind = ORDER_HINT_GUARD;
				if( !getGuardedSpot( ai, resolvedGoal ) )
					continue;
				goalResolved = TRUE;
				break;
			}

			default:
				// a parked aircraft is running its own takeoff state machine, not the order the player
				// gave it, and that order is held out of reach of the goal until the wheels are up.
				// Ask for it, or an air strike shows nothing at all during the seconds the plane spends
				// taxiing, which is exactly when the player wants to see where it is going
				if( !getHeldAircraftOrder( obj, hint.kind, resolvedGoal ) )
					continue;
				goalResolved = TRUE;
				break;
		}

		if( !legsDrawn )
		{
			hint.from = *obj->getPosition();

			// a queued path is shown point by point: one thread from the unit to its next point and one
			// from each point to the one after it, so the whole shift queue is on the ground at once and
			// stays there after the key is let go
			const Int pathSize = ai->friend_getWaypointGoalPathSize();
			const Int pathIndex = ai->friend_getCurrentGoalPathIndex();
			if( goalResolved )
			{
				hint.to = resolvedGoal;
			}
			else if( pathSize > 0 && pathIndex >= 0 && pathIndex < pathSize )
			{
				for( Int i = pathIndex; i < pathSize - 1; i++ )
				{
					hint.to = *ai->friend_getGoalPathPosition( i );
					addOrderHint( hint, previous );
					hint.from = hint.to;
				}
				hint.to = *ai->friend_getGoalPathPosition( pathSize - 1 );
			}
			else
			{
				// a goal object outranks the goal position: a unit chasing something is headed wherever that
				// thing is standing now, not where it stood when the order was given.  Without one the end of
				// the unit's own path is where it is really going: a group sent to one spot is spread over the
				// free cells round it, and the order's point is the same for every member.  A unit still
				// waiting for its path has only the order's point to show
				Object *goalObj = ai->getGoalObject();
				Path *path = ai->getPath();
				if( goalObj )
					hint.to = *goalObj->getPosition();
				else if( path )
					hint.to = *path->getLastNode()->getPosition();
				else
					hint.to = *ai->getGoalPosition();
			}

			addOrderHint( hint, previous );
		}

		// the rest of the unit's shift queue: every order still owed after this one, each drawn on from
		// where the last leaves off.  The list belongs to the whole chain rather than to one unit, so
		// each member's tail starts wherever that unit's own current order ends
		const OrderChain *chain = local->getOrderQueue()->findChain( obj->getID() );
		if( chain )
		{
			hint.from = hint.to;
			addQueuedOrderTail( hint, *chain, previous );
		}
	}

	//
	// A unit holding a list that drew nothing above has to draw it anyway.  One waiting for the rest
	// of its chain to catch up is idle, an aircraft on its way home for ammo has no goal the switch
	// recognises, and one parked on its airfield is not even in the selection - so the list went
	// blank for exactly the time the player most wanted to see it.  A chain with anything selected in
	// it draws for every member.
	//
	const OrderChainList& chains = local->getOrderQueue()->getChains();
	for( OrderChainList::const_iterator chain = chains.begin(); chain != chains.end(); ++chain )
	{
		Bool anySelected = FALSE;
		for( std::vector<ObjectID>::const_iterator id = chain->m_members.begin(); id != chain->m_members.end(); ++id )
		{
			const Object *member = TheGameLogic->findObjectByID( *id );
			const Drawable *draw = member ? member->getDrawable() : NULL;
			if( draw && draw->isSelected() )
			{
				anySelected = TRUE;
				break;
			}
		}

		if( !anySelected )
			continue;

		for( std::vector<ObjectID>::const_iterator id = chain->m_members.begin(); id != chain->m_members.end(); ++id )
		{
			Bool alreadyDrawn = FALSE;
			for( std::vector<OrderHint>::const_iterator drawn = m_orderHints.begin();
					 drawn != m_orderHints.end(); ++drawn )
			{
				if( drawn->owner == *id )
				{
					alreadyDrawn = TRUE;
					break;
				}
			}

			if( alreadyDrawn )
				continue;

			const Object *member = TheGameLogic->findObjectByID( *id );
			if( member == NULL || member->isEffectivelyDead() )
				continue;

			OrderHint hint;
			hint.owner = *id;
			hint.from = *member->getPosition();
			hint.to = hint.from;

			// the order the chain is on now is no longer in its list, so it is drawn from here
			if( getQueuedOrderHint( chain->m_active, hint ) )
			{
				addOrderHint( hint, previous );
				hint.from = hint.to;
			}

			addQueuedOrderTail( hint, *chain, previous );
		}
	}
}

//-------------------------------------------------------------------------------------------------
/** Units on the same kind of order that stand together and are headed to the same place draw one
	* thread between them, from their average position to their average destination.  Twenty tanks
	* sent across the map were twenty threads laid almost on top of each other; a selection spread
	* over the map still draws one thread per knot of units, so a flank sent separately stays
	* visible. */
//-------------------------------------------------------------------------------------------------
void InGameUI::bunchOrderHints( void )
{
	// about ten pathfinding cells: a knot of four or five tanks, or a flood of destinations round
	// one click
	const Real BUNCH_RADIUS = 100.0f;
	const Real bunchRadiusSqr = BUNCH_RADIUS * BUNCH_RADIUS;

	m_drawnOrderHints.clear();
	std::vector<Int> memberCounts;

	for( std::vector<OrderHint>::const_iterator hint = m_orderHints.begin();
			 hint != m_orderHints.end(); ++hint )
	{
		Bool joined = FALSE;
		for( size_t i = 0; i < m_drawnOrderHints.size(); ++i )
		{
			OrderHint& bunch = m_drawnOrderHints[ i ];
			if( bunch.kind != hint->kind || bunch.step != hint->step || bunch.icon != hint->icon )
				continue;
			const Real fromX = bunch.from.x - hint->from.x;
			const Real fromY = bunch.from.y - hint->from.y;
			if( fromX * fromX + fromY * fromY > bunchRadiusSqr )
				continue;
			const Real toX = bunch.to.x - hint->to.x;
			const Real toY = bunch.to.y - hint->to.y;
			if( toX * toX + toY * toY > bunchRadiusSqr )
				continue;

			// running averages, so the bunch is compared against its middle rather than its first unit
			const Real share = 1.0f / (Real)( ++memberCounts[ i ] );
			bunch.from.x += ( hint->from.x - bunch.from.x ) * share;
			bunch.from.y += ( hint->from.y - bunch.from.y ) * share;
			bunch.from.z += ( hint->from.z - bunch.from.z ) * share;
			bunch.to.x += ( hint->to.x - bunch.to.x ) * share;
			bunch.to.y += ( hint->to.y - bunch.to.y ) * share;
			bunch.to.z += ( hint->to.z - bunch.to.z ) * share;

			// the oldest member's age, so a unit joining a standing order does not slide the marker in again
			bunch.bornMs = min( bunch.bornMs, hint->bornMs );
			joined = TRUE;
			break;
		}

		if( !joined )
		{
			m_drawnOrderHints.push_back( *hint );
			memberCounts.push_back( 1 );
		}
	}
}

//-------------------------------------------------------------------------------------------------
/** Every order still owed, one thread from the last point to the next, starting wherever the hint
	* handed in leaves off. */
//-------------------------------------------------------------------------------------------------
void InGameUI::addQueuedOrderTail( OrderHint& hint, const OrderChain& chain, const std::vector<OrderHint>& previous )
{
	for( std::vector<QueuedOrder>::const_iterator order = chain.m_pending.begin();
			 order != chain.m_pending.end(); ++order )
	{
		if( !getQueuedOrderHint( *order, hint ) )
			continue;

		addOrderHint( hint, previous );
		hint.from = hint.to;
	}
}

//-------------------------------------------------------------------------------------------------
/** Which marker a queued order draws, and where.  A queued victim is drawn where it stands now rather
	* than where it stood when the player picked it, so the thread follows a target that is driving
	* away.  One that died while it waited its turn is drawn nowhere: the order will be skipped.  Nor
	* is one that has driven into the shroud since it was picked - the thread would otherwise trace it
	* through the fog, which is a look at the map you have not earned.
	*
	* An upgrade, or an ability that needs no target, is used wherever the step before it ends, which
	* is the hint.to handed in; it is left there, and the marker sits on that spot. */
//-------------------------------------------------------------------------------------------------
Bool InGameUI::getQueuedOrderHint( const QueuedOrder& order, OrderHint& hint ) const
{
	hint.icon = NULL;

	switch( order.getType() )
	{
		case GameMessage::MSG_DO_MOVETO:
		case GameMessage::MSG_DO_FORCEMOVETO:
		case GameMessage::MSG_DO_FORMATION_MOVETO:
		case GameMessage::MSG_DO_SALVAGE:
			hint.kind = ORDER_HINT_MOVE;
			break;

		case GameMessage::MSG_DO_ATTACKMOVETO:
		case GameMessage::MSG_DO_FORMATION_ATTACKMOVETO:
			hint.kind = ORDER_HINT_ATTACK_MOVE;
			break;

		case GameMessage::MSG_DO_ATTACK_OBJECT:
		case GameMessage::MSG_DO_WEAPON_AT_OBJECT:
			hint.kind = ORDER_HINT_ATTACK;
			break;

		case GameMessage::MSG_DO_FORCE_ATTACK_OBJECT:
			hint.kind = ORDER_HINT_FORCE_ATTACK;
			break;

		case GameMessage::MSG_DO_FORCE_ATTACK_GROUND:
		case GameMessage::MSG_DO_FORMATION_FORCEATTACK:
		case GameMessage::MSG_DO_WEAPON_AT_LOCATION:
		case GameMessage::MSG_DO_SPECIAL_POWER_AT_LOCATION:
			hint.kind = ORDER_HINT_ATTACK_GROUND;
			break;

		case GameMessage::MSG_DO_GUARD_POSITION:
		case GameMessage::MSG_DO_GUARD_OBJECT:
		case GameMessage::MSG_DO_FORMATION_GUARD:
			hint.kind = ORDER_HINT_GUARD;
			break;

		case GameMessage::MSG_ENTER:
		case GameMessage::MSG_COMBATDROP_AT_OBJECT:
		case GameMessage::MSG_COMBATDROP_AT_LOCATION:
			hint.kind = ORDER_HINT_ENTER;
			break;

		case GameMessage::MSG_DOCK:
			hint.kind = ORDER_HINT_DOCK;
			break;

		case GameMessage::MSG_GET_REPAIRED:
			hint.kind = ORDER_HINT_GET_REPAIRED;
			break;

		case GameMessage::MSG_GET_HEALED:
			hint.kind = ORDER_HINT_GET_HEALED;
			break;

		case GameMessage::MSG_DO_REPAIR:
			hint.kind = ORDER_HINT_DO_REPAIR;
			break;

		case GameMessage::MSG_DO_SPECIAL_POWER_AT_OBJECT:
		{
			// a capture and a hack have cursors of their own; the rest of what a unit does to one object
			// with an ability - a charge, a sniper round, a satchel - is an attack as far as the marker goes
			const SpecialPowerTemplate *power = TheSpecialPowerStore->findSpecialPowerTemplateByID( order.getArgument( 0 ).integer );
			switch( power ? power->getSpecialPowerType() : SPECIAL_INVALID )
			{
				case SPECIAL_INFANTRY_CAPTURE_BUILDING:
				case SPECIAL_BLACKLOTUS_CAPTURE_BUILDING:
					hint.kind = ORDER_HINT_CAPTURE;
					break;

				case SPECIAL_BLACKLOTUS_DISABLE_VEHICLE_HACK:
				case SPECIAL_BLACKLOTUS_STEAL_CASH_HACK:
				case SPECIAL_HACKER_DISABLE_BUILDING:
					hint.kind = ORDER_HINT_HACK;
					break;

				default:
					hint.kind = ORDER_HINT_ATTACK;
					break;
			}
			break;
		}

		case GameMessage::MSG_DO_SPECIAL_POWER:
			hint.kind = ORDER_HINT_ABILITY;
			return TRUE;

		case GameMessage::MSG_QUEUE_UPGRADE:
		{
			const UpgradeTemplate *upgrade = TheUpgradeCenter->findUpgradeByKey( (NameKeyType)order.getArgument( 1 ).integer );
			hint.kind = ORDER_HINT_UPGRADE;
			hint.icon = upgrade ? upgrade->getButtonImage() : NULL;
			return TRUE;
		}

		default:
			// a hold is wherever each unit happens to be standing, and a list that began behind an
			// unqueued order has nothing of its own to show for that order
			return FALSE;
	}

	const ObjectID targetID = order.getTargetID();
	if( targetID == INVALID_ID )
		return order.getDestination( &hint.to );

	const Object *target = TheGameLogic->findObjectByID( targetID );
	if( target == NULL || target->isEffectivelyDead() || isHiddenByShroud( target ) )
		return FALSE;

	hint.to = *target->getPosition();
	return TRUE;
}

//-------------------------------------------------------------------------------------------------
/** The hints of one unit sit together in the list, in the order it will get to them.  A unit with
	* more than one place to go carries a number on each, so a shift list reads first to last on the
	* ground; a unit with one does not, since a lone "1" says nothing. */
//-------------------------------------------------------------------------------------------------
void InGameUI::numberOrderHints( void )
{
	size_t first = 0;
	while( first < m_orderHints.size() )
	{
		size_t end = first + 1;
		while( end < m_orderHints.size() && m_orderHints[ end ].owner == m_orderHints[ first ].owner )
			++end;

		if( end - first > 1 )
		{
			for( size_t i = first; i < end; ++i )
				m_orderHints[ i ].step = (Int)( i - first ) + 1;
		}
		first = end;
	}
}

//-------------------------------------------------------------------------------------------------
/** Add a marker to this frame's list, carrying over when it first appeared.  The list is thrown
	* away and rebuilt from the units every frame, so without this every marker would be newborn on
	* every frame and none of them would ever finish sliding in.
	*
	* A marker is last frame's marker when it is the n-th one belonging to the same unit.  Matching
	* on the destination instead would restart the slide on every frame of a chase, and matching on
	* the unit alone would give a whole queue one age. */
//-------------------------------------------------------------------------------------------------
void InGameUI::addOrderHint( OrderHint& hint, const std::vector<OrderHint>& previous )
{
	Int ordinal = 0;
	for( std::vector<OrderHint>::const_iterator it = m_orderHints.begin();
			 it != m_orderHints.end(); ++it )
	{
		if( it->owner == hint.owner )
			ordinal++;
	}

	hint.bornMs = timeGetTime();

	Int seen = 0;
	for( std::vector<OrderHint>::const_iterator it = previous.begin();
			 it != previous.end(); ++it )
	{
		if( it->owner != hint.owner )
			continue;
		if( seen++ == ordinal )
		{
			hint.bornMs = it->bornMs;
			break;
		}
	}

	m_orderHints.push_back( hint );
}

//-------------------------------------------------------------------------------------------------
/** The attack key is armed and the player has pressed the left button: from here a drag draws a
	* circle rather than a selection box. */
//-------------------------------------------------------------------------------------------------
void InGameUI::beginAttackCircle( const ICoord2D& pt )
{
	m_isAttackCircling = TRUE;
	m_attackCircleAnchor = pt;
	m_attackCircleCursor = pt;
	DEBUG_LOG(("attack circle: begun at %d,%d with %d selected\n", pt.x, pt.y, getSelectCount()));
}

//-------------------------------------------------------------------------------------------------
void InGameUI::updateAttackCircle( const ICoord2D& pt )
{
	m_attackCircleCursor = pt;
}

//-------------------------------------------------------------------------------------------------
/** The circle the player is dragging, in world terms: the anchor is its centre and the cursor sits
	* on its rim.  Both the rim drawn on the screen and the wash laid on the ground ask this, so they
	* cannot disagree about where the circle is. */
//-------------------------------------------------------------------------------------------------
Bool InGameUI::getAttackCircleGround( Coord3D& center, Real& radius ) const
{
	if( !m_isAttackCircling )
		return FALSE;

	Coord3D rim;
	TheTacticalView->screenToTerrain( &m_attackCircleAnchor, &center );
	TheTacticalView->screenToTerrain( &m_attackCircleCursor, &rim );

	const Real dx = rim.x - center.x;
	const Real dy = rim.y - center.y;
	radius = (Real)sqrt( dx * dx + dy * dy );

	// a press that has not been dragged anywhere is a click, not a circle
	return radius >= 1.0f;
}

//-------------------------------------------------------------------------------------------------
/** Everything hostile standing in the circle becomes a list of attacks, nearest first, and the group
	* is put on the head of it.  Without shift the list replaces whatever the group was doing; with it,
	* the list goes on the end of the group's shift queue.  Shroud decides membership: a target the
	* player cannot see is not in the circle, whatever the partition manager knows about it.  The
	* targets go through the same queue a shift-clicked attack uses, so the whole list is drawn on the
	* ground as threads and markers instead of only the one target the group happens to be shooting
	* at. */
//-------------------------------------------------------------------------------------------------
Bool InGameUI::issueAttackCircle( void )
{
	Coord3D center;
	Real radius;
	const Bool wasDragged = getAttackCircleGround( center, radius );

	m_isAttackCircling = FALSE;

	// the button went down and came back up without going anywhere, so this was a plain attack
	// click and the order belongs to the command translator, not here
	if( !wasDragged )
		return FALSE;

	const Player *local = ThePlayerList->getLocalPlayer();

	ObjectIterator *iter = ThePartitionManager->iterateObjectsInRange( &center, radius,
																																		FROM_CENTER_2D, NULL,
																																		ITER_SORTED_NEAR_TO_FAR );
	MemoryPoolObjectHolder holder( iter );
	Int targetCount = 0;
	for( Object *obj = iter->first(); obj; obj = iter->next() )
	{
		if( !isAttackListTarget( obj, local ) )
			continue;

		const Bool startsList = targetCount == 0 && !isInWaypointMode();
		markNextOrderQueued( startsList ? ORDER_QUEUE_FRESH : ORDER_QUEUE_APPEND );
		GameMessage *attack = TheMessageStream->appendMessage( GameMessage::MSG_DO_ATTACK_OBJECT );
		attack->appendObjectIDArgument( obj->getID() );
		targetCount++;
	}

	DEBUG_LOG(("attack circle: radius %.0f, %d targets, %d selected\n", radius, targetCount,
						 getSelectCount()));
	return TRUE;
}

//-------------------------------------------------------------------------------------------------
/** Would the circle or the attack line put this on the target list? */
//-------------------------------------------------------------------------------------------------
Bool InGameUI::isAttackListTarget( const Object *obj, const Player *local ) const
{
	if( obj->isEffectivelyDead() )
		return FALSE;
	if( local->getRelationship( obj->getTeam() ) != ENEMIES )
		return FALSE;
	if( isHiddenByShroud( obj ) )
		return FALSE;

	//
	// Shroud is only half of invisible. A stealthed tank sitting in ground the player has
	// cleared passes the test above, so a circle swept over open country used to pick out every
	// hidden unit in it and open fire: a detector nobody built, and a way to read the map for
	// stealth by dragging a circle over it. Undetected means not in the circle.
	//
	if( obj->testStatus( OBJECT_STATUS_STEALTHED ) && !obj->testStatus( OBJECT_STATUS_DETECTED ) )
		return FALSE;

	//
	// Not everything hostile standing in the circle is something to shoot at.  A shell or a
	// missile in flight is an object on the enemy's team like any other, and the range query hands
	// them over the same way it hands over tanks; the rest of the engine drops them by kind
	// wherever it scans.  Each one that reached the list cost the group a two second stall on a
	// target that was about to stop existing.
	//
	return !obj->isKindOf( KINDOF_PROJECTILE ) && !obj->isKindOf( KINDOF_UNATTACKABLE );
}

//-------------------------------------------------------------------------------------------------
/** The line drawn with the attack key: every enemy it runs across goes on the target list, in the
	* order the line reaches it, so the direction it was drawn in is the direction the group fights
	* along.  "Across" is the line passing over the object's own footprint, give or take a few feet for
	* a hand that is not steady.  Returns how many targets went out; with none, the caller fires on
	* the ground along the line instead. */
//-------------------------------------------------------------------------------------------------
Int InGameUI::issueAttackLine( const std::vector<Coord3D>& line )
{
	// how far off the line a unit's edge may be and still count as under it
	const Real LINE_TOLERANCE = 10.0f;

	// the range query measures to centres, so it reaches this much further for a building whose
	// centre is off the line and whose walls are under it
	const Real LARGEST_FOOTPRINT = 150.0f;

	if( line.size() < 2 )
		return 0;

	// one query over the circle round the whole line, then each object is measured against it
	Region2D box;
	box.lo.x = box.hi.x = line[ 0 ].x;
	box.lo.y = box.hi.y = line[ 0 ].y;
	for( std::vector<Coord3D>::const_iterator point = line.begin(); point != line.end(); ++point )
	{
		box.lo.x = min( box.lo.x, point->x );
		box.lo.y = min( box.lo.y, point->y );
		box.hi.x = max( box.hi.x, point->x );
		box.hi.y = max( box.hi.y, point->y );
	}
	Coord3D center;
	center.x = ( box.lo.x + box.hi.x ) * 0.5f;
	center.y = ( box.lo.y + box.hi.y ) * 0.5f;
	center.z = 0.0f;
	const Real halfWidth = ( box.hi.x - box.lo.x ) * 0.5f;
	const Real halfHeight = ( box.hi.y - box.lo.y ) * 0.5f;
	const Real reach = sqrt( halfWidth * halfWidth + halfHeight * halfHeight ) + LINE_TOLERANCE + LARGEST_FOOTPRINT;

	const Player *local = ThePlayerList->getLocalPlayer();
	ObjectIterator *iter = ThePartitionManager->iterateObjectsInRange( &center, reach, FROM_CENTER_2D, NULL );
	MemoryPoolObjectHolder holder( iter );

	// how far along the line each target is, so they can go out in the order the line meets them
	std::vector< std::pair<Real, ObjectID> > targets;
	for( Object *obj = iter->first(); obj; obj = iter->next() )
	{
		if( !isAttackListTarget( obj, local ) )
			continue;

		const Coord3D *pos = obj->getPosition();
		const Real allowed = obj->getGeometryInfo().getBoundingCircleRadius() + LINE_TOLERANCE;
		Real bestDistanceSqr = allowed * allowed;
		Real along = -1.0f;
		Real walked = 0.0f;
		for( size_t i = 1; i < line.size(); ++i )
		{
			const Real dx = line[ i ].x - line[ i - 1 ].x;
			const Real dy = line[ i ].y - line[ i - 1 ].y;
			const Real lengthSqr = dx * dx + dy * dy;
			const Real length = sqrt( lengthSqr );
			Real t = 0.0f;
			if( lengthSqr > 0.0f )
				t = clamp( 0.0f, ( ( pos->x - line[ i - 1 ].x ) * dx + ( pos->y - line[ i - 1 ].y ) * dy ) / lengthSqr, 1.0f );
			const Real offX = line[ i - 1 ].x + dx * t - pos->x;
			const Real offY = line[ i - 1 ].y + dy * t - pos->y;
			const Real distanceSqr = offX * offX + offY * offY;
			if( distanceSqr <= bestDistanceSqr )
			{
				bestDistanceSqr = distanceSqr;
				along = walked + length * t;
			}
			walked += length;
		}

		if( along >= 0.0f )
			targets.push_back( std::make_pair( along, obj->getID() ) );
	}

	std::sort( targets.begin(), targets.end() );

	for( size_t i = 0; i < targets.size(); ++i )
	{
		const Bool startsList = i == 0 && !isInWaypointMode();
		markNextOrderQueued( startsList ? ORDER_QUEUE_FRESH : ORDER_QUEUE_APPEND );
		GameMessage *attack = TheMessageStream->appendMessage( GameMessage::MSG_DO_ATTACK_OBJECT );
		attack->appendObjectIDArgument( targets[ i ].second );
	}

	DEBUG_LOG(("attack line: %d points, %d targets, %d selected\n", (Int)line.size(), (Int)targets.size(),
						 getSelectCount()));
	return (Int)targets.size();
}

//-------------------------------------------------------------------------------------------------
/** Whether the shroud is over this object as far as the player at this machine is concerned, or the
	* player a watcher with fog on is looking through. */
//-------------------------------------------------------------------------------------------------
Bool InGameUI::isHiddenByShroud( const Object *obj ) const
{
	return obj->getShroudedStatus( TheObserverCamera.getShroudPlayerIndex() ) > OBJECTSHROUD_PARTIAL_CLEAR;
}

//-------------------------------------------------------------------------------------------------
void InGameUI::markNextOrderQueued( OrderQueueMode mode )
{
	GameMessage *msg = TheMessageStream->appendMessage( GameMessage::MSG_QUEUE_NEXT_ORDER );
	msg->appendIntegerArgument( mode );
}

//-------------------------------------------------------------------------------------------------
/** An attack command has occurred, start graphical "hint". */
//-------------------------------------------------------------------------------------------------
void InGameUI::createAttackHint( const GameMessage *msg )
{

}

//-------------------------------------------------------------------------------------------------
/** A force attack command has occurred, start graphical "hint". */
//-------------------------------------------------------------------------------------------------
void InGameUI::createForceAttackHint( const GameMessage *msg )
{

}

//-------------------------------------------------------------------------------------------------
/** An garrison command has occurred, start graphical "hint". */
//-------------------------------------------------------------------------------------------------
void InGameUI::createGarrisonHint( const GameMessage *msg )
{
	Drawable *draw = TheGameClient->findDrawableByID( msg->getArgument(0)->drawableID );
	if( draw )
	{
		draw->onSelected();
	}
}

#if defined(_DEBUG) || defined(_INTERNAL)
#define AI_DEBUG_TOOLTIPS		1

#ifdef AI_DEBUG_TOOLTIPS
#include "Common/StateMachine.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/AIPathfind.h"
#endif // AI_DEBUG_TOOLTIPS

#endif // defined(_DEBUG) || defined(_INTERNAL)

//-------------------------------------------------------------------------------------------------
/** Details of what is mouse hovered over right now are in this message.  Terrain might result
	* in just a tooltip.  An object might get a tooltip and show its hit points.
 */
//-------------------------------------------------------------------------------------------------
void InGameUI::createMouseoverHint( const GameMessage *msg )
{
	if (m_isScrolling || m_isSelecting)
		return; // no mouseover for you

	// a signal armed off the bar is where the next click goes, over a unit, the ground or the radar
	if( isSignalArmed() )
	{
		setMouseCursor( Mouse::CROSS );
		return;
	}

	GameWindow *window = NULL;
	const MouseIO *io = TheMouse->getMouseStatus();
	Bool underWindow = false;
	if (io && TheWindowManager)
		window = TheWindowManager->getWindowUnderCursor(io->pos.x, io->pos.y);

	while (window)
	{
		if (window->winGetInputFunc() == LeftHUDInput) {
			underWindow = false;
			break;
		}
		
		// check to see if it or any of its parents are opaque.  If so, we can't select anything.
		if (!BitTest( window->winGetStatus(), WIN_STATUS_SEE_THRU ))
		{
			underWindow = true;
			break;
		}

		window = window->winGetParent();
	}
	if (underWindow)
	{
		setMouseCursor(Mouse::ARROW); // regardless of m_mouseMode
		return;
	}

  



	DrawableID oldID = m_mousedOverDrawableID;

	if (msg->getType() == GameMessage::MSG_MOUSEOVER_DRAWABLE_HINT)
	{
		TheMouse->setCursorTooltip(UnicodeString::TheEmptyString );
		m_mousedOverDrawableID = INVALID_DRAWABLE_ID;
		const Drawable *draw = TheGameClient->findDrawableByID(msg->getArgument(0)->drawableID);
		const Object *obj = draw ? draw->getObject() : NULL;
		if( obj )
		{
			
 			//Ahh, here is a wierd exception: if the moused-over drawable is a mob-member
			//(e.g. AngryMob), Lets fool the UI into creating the hint for the NEXUS instead...
 			if (obj->isKindOf( KINDOF_IGNORED_IN_GUI ))
 			{
 				static NameKeyType key_MobMemberSlavedUpdate = NAMEKEY( "MobMemberSlavedUpdate" );
 				MobMemberSlavedUpdate *MMSUpdate = (MobMemberSlavedUpdate*)obj->findUpdateModule( key_MobMemberSlavedUpdate );
 				if( MMSUpdate )
 				{
 					Object *slaver = TheGameLogic->findObjectByID(MMSUpdate->getSlaverID());
 					if ( slaver )
 					{
 						Drawable *slaverDraw = slaver->getDrawable();
 						if ( slaverDraw )
 							m_mousedOverDrawableID = slaverDraw->getID();
 							// if this fails, not to worry... it has already defaulted to INVALID_DRAWABLE_ID, above
 					}
 				}
 			}
 			else
 				m_mousedOverDrawableID = draw->getID();

#if defined(_DEBUG) || defined(_INTERNAL) //Extra hacky, sorry, but I need to use this in constantdebug report
			if ( TheGlobalData->m_constantDebugUpdate == TRUE )
				m_mousedOverDrawableID = draw->getID();
#endif


			const Player* player = NULL;
			const ThingTemplate *thingTemplate = obj->getTemplate();

			ContainModuleInterface* contain = obj->getContain();
			if( contain )
				player = contain->getApparentControllingPlayer(ThePlayerList->getLocalPlayer());

			if (player == NULL)
				player = obj->getControllingPlayer();

			Bool disguised = false;
			if( obj->isKindOf( KINDOF_DISGUISER ) )
			{
				//Because we have support for disguised units pretending to be units from another
				//team, we need to intercept it here and make sure it's rendered appropriately
				//based on which client is rendering it.
        StealthUpdate *update = obj->getStealth();
				if( update )
				{
					if( update->isDisguised() )
					{
						Player *clientPlayer = ThePlayerList->getLocalPlayer();
						Player *disguisedPlayer = ThePlayerList->getNthPlayer( update->getDisguisedPlayerIndex() );
						if( player->getRelationship( clientPlayer->getDefaultTeam() ) != ALLIES && clientPlayer->isPlayerActive() )
						{
							//Neutrals and enemies will see this disguised unit as the team it's disguised as.
							player = disguisedPlayer;
							const ThingTemplate *disguisedTemplate = update->getDisguisedTemplate();
							if( disguisedTemplate )
							{
								thingTemplate = disguisedTemplate;
								disguised = true;
							}
						}
						//Otherwise, the color will show up as the team it really belongs to (already set above).
					}
				}
			}


			UnicodeString str = thingTemplate->getDisplayName();
			UnicodeString displayName = thingTemplate->getDisplayName();
			if( str.isEmpty() )
			{
				AsciiString txtTemp;
				txtTemp.format("ThingTemplate:%s", obj->getTemplate()->getName().str());
				str = TheGameText->fetch(txtTemp);
				//str.format(L"ThingTemplate:'%hs'", obj->getTemplate()->getName().str());
			}

#ifdef AI_DEBUG_TOOLTIPS
			if (TheGlobalData->m_debugAI) {
				const Team *team = obj->getTeam();
				AsciiString objName = obj->getName();
				AsciiString teamName;
				AsciiString stateName;
				
				AIUpdateInterface *ai = (AIUpdateInterface*)obj->getAI();
				if (ai) {
					if (ai->getPath()) {
						TheAI->pathfinder()->setDebugPath(ai->getPath());
					}
#ifdef STATE_MACHINE_DEBUG	
					stateName = ai->getCurrentStateName();
					if (ai->getAttackInfo()) {
						stateName.concat(" AttackPriority=");
						stateName.concat(ai->getAttackInfo()->getName());
					}
#endif
				}
				if( team )
				{
					teamName = team->getName();
				}
				if (!objName.isEmpty())
				{
					if (!teamName.isEmpty())
					{
						str.format(L"%hs(%hs): %s", teamName.str(), objName.str(), str.str());
					}
					else
					{
						str.format(L"%hs: %s", objName.str(), str.str());
					}
				}
				else
				{
					if (!teamName.isEmpty())
					{
						str.format(L"%hs: %s", teamName.str(), str.str());
					}
				}
				str.format(L"%s - %hs", str.str(), stateName.str());

			}
#endif
			UnicodeString warehouseFeedback;
			// Add on dollar amount of warehouse contents so people don't freak out until the art is hooked up
			static const NameKeyType warehouseModuleKey = TheNameKeyGenerator->nameToKey( "SupplyWarehouseDockUpdate" );
			SupplyWarehouseDockUpdate *warehouseModule = (SupplyWarehouseDockUpdate *)obj->findUpdateModule( warehouseModuleKey );
			if( warehouseModule != NULL )
			{
				Int boxes = warehouseModule->getBoxesStored();
				Int value = boxes * TheGlobalData->m_baseValuePerSupplyBox;
				warehouseFeedback.format(TheGameText->fetch("TOOLTIP:SupplyWarehouse"), value);
				str.concat(warehouseFeedback);
			}

      if (player)
			{
				UnicodeString tooltip;
				//if (TheRecorder->isMultiplayer() && player->getPlayerType() == PLAYER_HUMAN)
				if (TheRecorder->isMultiplayer() && player->isPlayableSide())
					tooltip.format(L"%s\n%s", str.str(), ((Player *)player)->getPlayerDisplayName().str());
				else
					tooltip = str;

				Int localPlayerIndex = ThePlayerList ? TheObserverCamera.getShroudPlayerIndex() : 0;

				Int x, y;
				ThePartitionManager->worldToCell(obj->getPosition()->x, obj->getPosition()->y, &x, &y);
				if( ThePartitionManager->getShroudStatusForPlayer(localPlayerIndex, x, y) == CELLSHROUD_CLEAR )
				{
					RGBColor rgb;
					if( disguised )
					{
						rgb.setFromInt( clientPlayerColor( player ) );
					}
					else
					{
						rgb.setFromInt( clientColor( draw->getObject()->getIndicatorColor() ) );

						// Unless this is a stealth garrisoned building, 
						// Let's not use the contained's housecolor
						const Object *obj = draw->getObject();
						if ( obj )
						{
							ContainModuleInterface *contain = obj->getContain();
							if ( contain && contain->isGarrisonable() )
							{
								const Player *play = contain->getApparentControllingPlayer( ThePlayerList->getLocalPlayer() );
								if ( play )
									rgb.setFromInt( clientPlayerColor( play ) );
							}
						}

					}

					//Object:Prop is a blank string... but we don't want to show
					//any popup box at all if that is the case!
					if( displayName.compare( TheGameText->fetch( "OBJECT:Prop" ) ) )
					{
	  				TheMouse->setCursorTooltip(tooltip, -1, &rgb );
					}
				}
			}
		}

	}
	else
	{
		m_mousedOverDrawableID = INVALID_DRAWABLE_ID;
	}

	if (oldID != m_mousedOverDrawableID)
	{
		//DEBUG_LOG(("Resetting tooltip delay\n"));
		TheMouse->resetTooltipDelay();
	}

	if (m_mouseMode == MOUSEMODE_DEFAULT && !m_isScrolling && !m_isSelecting && !TheInGameUI->getSelectCount() && (TheRecorder->getMode() != RECORDERMODETYPE_PLAYBACK || TheLookAtTranslator->hasMouseMovedRecently()))
	{
		if( m_mousedOverDrawableID != INVALID_DRAWABLE_ID )
		{
			Drawable *draw = TheGameClient->findDrawableByID(m_mousedOverDrawableID);
			
			//Add basic logic to determine if we can select a unit (or hint)
			const Object *obj = draw ? draw->getObject() : NULL;
			Bool drawSelectable = CanSelectDrawable(draw, FALSE);
			if( !obj )
			{
				drawSelectable = false;
			}

			if( drawSelectable && obj->isLocallyControlled() )
			{
				setMouseCursor(Mouse::SELECTING);
			}
			else
			{
				setMouseCursor(Mouse::ARROW);
			}
		}
		else
		{
			setMouseCursor(Mouse::ARROW);
		}
	}
	else if (m_mouseMode != MOUSEMODE_DEFAULT && m_mouseMode != MOUSEMODE_BUILD_PLACE )
	{
		setMouseCursor((Mouse::MouseCursor)m_mouseModeCursor);
	}
}

//-------------------------------------------------------------------------------------------------
/** A command would be given if a click were to happen, so give a preview hint of what it would be.
	* Changing the mouse cursor is an example
	*/
void InGameUI::createCommandHint( const GameMessage *msg )
{
	if (m_isScrolling || m_isSelecting || TheRecorder->getMode() == RECORDERMODETYPE_PLAYBACK)
		return;

	if( isSignalArmed() )
	{
		setMouseCursor( Mouse::CROSS );
		return;
	}

	const Drawable *draw = TheGameClient->findDrawableByID(m_mousedOverDrawableID);
	GameMessage::Type t = msg->getType();
//#ifdef DO_SHROUD_PROJECTION
	if( draw && (t == GameMessage::MSG_DO_ATTACK_OBJECT_HINT || t == GameMessage::MSG_DO_ATTACK_OBJECT_AFTER_MOVING_HINT) )
	{
		const Object* obj = draw->getObject();
		Int localPlayerIndex = ThePlayerList ? ThePlayerList->getLocalPlayer()->getPlayerIndex() : 0;
#if defined(_DEBUG) || defined(_INTERNAL)
		ObjectShroudStatus ss = (!obj || !TheGlobalData->m_shroudOn) ? OBJECTSHROUD_CLEAR : obj->getShroudedStatus(localPlayerIndex);
#else
		ObjectShroudStatus ss = (!obj) ? OBJECTSHROUD_CLEAR : obj->getShroudedStatus(localPlayerIndex);
#endif
		if (ss == OBJECTSHROUD_SHROUDED)
		{
			t = GameMessage::MSG_DO_MOVETO_HINT;	// if the object is hidden, switch to something innocuous
		}
	}
//#endif


	//
	// While a structure is on the cursor, handleBuildPlacements owns the radius cursor - it is the
	// range ring for the thing being placed. Wiping it here (this runs on every mouse hint) is what
	// made that ring flicker or never appear at all.
	//
	if( m_pendingPlaceType == NULL )
		setRadiusCursorNone();

  if ( TheGlobalData->m_doubleClickAttackMove )
  {
    if ( --m_duringDoubleClickAttackMoveGuardHintTimer > 0 )
    {
      setMouseCursor(Mouse::FORCE_ATTACK_GROUND);
		  setRadiusCursor(RADIUSCURSOR_GUARD_AREA, 
										  NULL,
										  PRIMARY_WEAPON);
      return;
    }
  }





	// set cursor to normal if there is a window under the cursor
	GameWindow *window = NULL;
	const MouseIO *io = TheMouse->getMouseStatus();
	Bool underWindow = false;
	if (io && TheWindowManager)
		window = TheWindowManager->getWindowUnderCursor(io->pos.x, io->pos.y);


	while (window)
	{
		if (window->winGetInputFunc() == LeftHUDInput) {
			underWindow = false;
			break;
		}
		
		// check to see if it or any of its parents are opaque.  If so, we can't select anything.
		if (!BitTest( window->winGetStatus(), WIN_STATUS_SEE_THRU ))
		{
			underWindow = true;
			break;
		}

		window = window->winGetParent();
	}

	//Add basic logic to determine if we can select a unit (or hint)
	const Object *obj = draw ? draw->getObject() : NULL;
	Bool drawSelectable = CanSelectDrawable(draw, FALSE);
	if( !obj )
	{
		drawSelectable = false;
	}

	// Note: These are only non-NULL if there is exactly one thing selected.
	const Drawable *srcDraw = NULL;
	const Object *srcObj = NULL;
	if (getSelectCount() == 1) {
		srcDraw = getAllSelectedDrawables()->front();
		srcObj = (srcDraw ? srcDraw->getObject() : NULL);
	}

	switch (m_mouseMode)
	{
		case MOUSEMODE_DEFAULT:
			{ 
				// This section of code only gets called when there is no specific cursor mode happening.
				if (underWindow || (srcObj && !srcObj->isLocallyControlled()))
				{
					setMouseCursor(Mouse::ARROW);
					return;
				}
				switch (t)
				{
					case GameMessage::MSG_DO_MOVETO_HINT:
					{
						if( !drawSelectable && srcObj && srcObj->isLocallyControlled() && srcObj->isKindOf(KINDOF_STRUCTURE))
							setMouseCursor( Mouse::GENERIC_INVALID );
						else if( drawSelectable && obj->isLocallyControlled() && !obj->isKindOf(KINDOF_MINE))
							setMouseCursor( Mouse::SELECTING );
						else if( TheRadar->isRadarWindow( window ) &&
										 TheRadar->isRadarForced() == FALSE &&
										 (TheRadar->isRadarHidden() || 
										 ThePlayerList->getLocalPlayer()->hasRadar() == FALSE) )
							setMouseCursor( Mouse::ARROW );
						else if( isGuardArmed() )
							setMouseCursor( Mouse::CROSS );	// the targeting cross, the cursor EA's own guard button arms
						else
							setMouseCursor( Mouse::MOVETO );
						break;
					}
					case GameMessage::MSG_DO_ATTACKMOVETO_HINT:
						if( drawSelectable && obj->isLocallyControlled()  )
							setMouseCursor( Mouse::SELECTING );
						else
							setMouseCursor( Mouse::ATTACKMOVETO );
						break;
					case GameMessage::MSG_ADD_WAYPOINT_HINT:
						setMouseCursor( Mouse::WAYPOINT );
						break;
					case GameMessage::MSG_DO_ATTACK_OBJECT_HINT:
						setMouseCursor( Mouse::ATTACK_OBJECT );
						break;
					case GameMessage::MSG_DO_ATTACK_OBJECT_AFTER_MOVING_HINT:
						setMouseCursor( Mouse::OUTRANGE );
						break;
					case GameMessage::MSG_DO_FORCE_ATTACK_OBJECT_HINT:
						setMouseCursor( Mouse::FORCE_ATTACK_OBJECT );
						break;
					case GameMessage::MSG_DO_FORCE_ATTACK_GROUND_HINT:
						setMouseCursor( Mouse::FORCE_ATTACK_GROUND );
						break;
					case GameMessage::MSG_GET_REPAIRED_HINT:
						setMouseCursor( Mouse::GET_REPAIRED );
						break;
					case GameMessage::MSG_DOCK_HINT:
						setMouseCursor( Mouse::DOCK );
						break;
					case GameMessage::MSG_GET_HEALED_HINT:
						setMouseCursor( Mouse::GET_HEALED );
						break;
					case GameMessage::MSG_DO_REPAIR_HINT:
						setMouseCursor( Mouse::DO_REPAIR );
						break;
					case GameMessage::MSG_RESUME_CONSTRUCTION_HINT:
						setMouseCursor( Mouse::RESUME_CONSTRUCTION );
						break;				
					case GameMessage::MSG_ENTER_HINT:
						setMouseCursor( Mouse::ENTER_FRIENDLY );
						break;
					case GameMessage::MSG_CONVERT_TO_CARBOMB_HINT:
					case GameMessage::MSG_HIJACK_HINT:
					case GameMessage::MSG_SABOTAGE_HINT:
						setMouseCursor( Mouse::ENTER_AGGRESSIVELY );
						break;
					case GameMessage::MSG_DEFECTOR_HINT:
						setMouseCursor( Mouse::DEFECTOR );
						break;
#ifdef ALLOW_SURRENDER
					case GameMessage::MSG_PICK_UP_PRISONER_HINT:
						setMouseCursor( Mouse::PICK_UP_PRISONER );
						break;
#endif
					case GameMessage::MSG_CAPTUREBUILDING_HINT:
						setMouseCursor( Mouse::CAPTUREBUILDING );
						break;
					case GameMessage::MSG_HACK_HINT:
						setMouseCursor( Mouse::HACK );
						break;
					case GameMessage::MSG_IMPOSSIBLE_ATTACK_HINT:
						setMouseCursor( Mouse::GENERIC_INVALID );
						break;
					case GameMessage::MSG_SET_RALLY_POINT_HINT:
						if ( !drawSelectable )
							setMouseCursor( Mouse::SET_RALLY_POINT );
						else
							setMouseCursor( Mouse::SELECTING );
						break;
					case GameMessage::MSG_DO_SPECIAL_POWER_OVERRIDE_DESTINATION_HINT:
						setMouseCursor( Mouse::PARTICLE_UPLINK_CANNON );
						break;
					case GameMessage::MSG_DO_SALVAGE_HINT:
						setMouseCursor( Mouse::MOVETO );
						break;
					case GameMessage::MSG_DO_INVALID_HINT:
						setMouseCursor( Mouse::GENERIC_INVALID );
						break;
				}
			}
			break;
		case MOUSEMODE_BUILD_PLACE:
			{
				if (underWindow)
				{
					setMouseCursor(Mouse::ARROW);
					return;
				}
				//
				// what is under the cursor does not matter while a structure is on it - whether the
				// spot can be built on does, and handleBuildPlacements already worked that out for
				// the tint on the ghost.  Anything the hint said would have left the pointer with no
				// art at all: see placementCursor.
				//
				setMouseCursor( placementCursor( m_placementLegal ) );
			}
			break;
		case MOUSEMODE_GUI_COMMAND:
			{
				if (underWindow)
				{
					setMouseCursor(Mouse::ARROW);
					return;
				}
				// set the mouse cursor for commands that need a targeting or to normal with no command
				if( m_pendingGUICommand )
				{
					if( m_pendingGUICommand->isContextCommand() || 
							m_pendingGUICommand->getCommandType() == GUI_COMMAND_SPECIAL_POWER ||
							m_pendingGUICommand->getCommandType() == GUI_COMMAND_SPECIAL_POWER_FROM_SHORTCUT )
					{
						//Here is the hook for when we are in a context sensitive command mode. We can
						//either do the specified command mode command or nothing! Whether or not the 
						//command is valid or not was determined in evaluateContextCommand which is
						//called first, and posts the appropriate message.
						AsciiString cursorName;	// empty by default
						switch( t )
						{
							case GameMessage::MSG_VALID_GUICOMMAND_HINT:
								cursorName = m_pendingGUICommand->getCursorName();
								break;
							case GameMessage::MSG_INVALID_GUICOMMAND_HINT:
							default:
								cursorName = m_pendingGUICommand->getInvalidCursorName();
								break;
						}

						Int index = TheMouse->getCursorIndex(cursorName);
						if( index != Mouse::INVALID_MOUSE_CURSOR )
						{
							setMouseCursor( (Mouse::MouseCursor)index );
						}
						else
						{
							setMouseCursor( Mouse::CROSS );
						}
						setRadiusCursor(m_pendingGUICommand->getRadiusCursorType(), //*****************************************************************
														m_pendingGUICommand->getSpecialPowerTemplate(),
														m_pendingGUICommand->getWeaponSlot());
					}
					else if( BitTest( m_pendingGUICommand->getOptions(), COMMAND_OPTION_NEED_TARGET ) )
					{
						Int index = TheMouse->getCursorIndex(m_pendingGUICommand->getCursorName());
						if (index != Mouse::INVALID_MOUSE_CURSOR)
							setMouseCursor( (Mouse::MouseCursor)index );
						else
							setMouseCursor( Mouse::CROSS );
						setRadiusCursor(m_pendingGUICommand->getRadiusCursorType(), //*****************************************************************
														m_pendingGUICommand->getSpecialPowerTemplate(),
														m_pendingGUICommand->getWeaponSlot());
					}
					else
					{
						setRadiusCursorNone();
					}
				}
			}
			break;
	}
}

//-------------------------------------------------------------------------------------------------
/** Ctrl held is how the game as shipped force fired, and Legacy is that game.  Modern keeps ctrl for
	* the shared pace on a move and force fires on the attack key alone. */
//-------------------------------------------------------------------------------------------------
Bool InGameUI_isForceFireOn( Bool forceAttackArmed, Bool ctrlHeld, Bool legacyInput )
{
	return forceAttackArmed || ( legacyInput && ctrlHeld );
}

Bool InGameUI::isForceFireOn( void ) const
{
	return InGameUI_isForceFireOn( m_forceAttackArmed, m_forceAttackMode, TheGlobalData->isLegacyInput() );
}

//-------------------------------------------------------------------------------------------------
/// Get drawable ID under cursor
//-------------------------------------------------------------------------------------------------
DrawableID InGameUI::getMousedOverDrawableID( void ) const
{

	return m_mousedOverDrawableID;

}

//-------------------------------------------------------------------------------------------------
/// set right-click scroll mode
//-------------------------------------------------------------------------------------------------
void InGameUI::setScrolling( Bool isScrolling, Bool moveCursor )
{
	if (m_isScrolling == isScrolling)
	{
		return;
	}

	if (isScrolling)
	{
		TheMouse->capture();
		if (moveCursor)
			setMouseCursor( Mouse::SCROLL );

		// break any camera locks
		TheTacticalView->setCameraLock( INVALID_ID );
		TheTacticalView->setCameraLockDrawable( NULL );
	}
	else
	{
		if (moveCursor)
			setMouseCursor( Mouse::ARROW );
		TheMouse->releaseCapture();
	}

	m_isScrolling = isScrolling;

}

//-------------------------------------------------------------------------------------------------
/// are we scrolling?
//-------------------------------------------------------------------------------------------------
Bool InGameUI::isScrolling( void )
{
	return m_isScrolling;
}

//-------------------------------------------------------------------------------------------------
/// set drag select mode
//-------------------------------------------------------------------------------------------------
void InGameUI::setSelecting( Bool isSelecting )
{
	if (m_isSelecting == isSelecting)
	{
		return;
	}

	//setMouseCursor( Mouse::SELECTING );
	m_isSelecting = isSelecting;
}

//-------------------------------------------------------------------------------------------------
/// are we selecting?
//-------------------------------------------------------------------------------------------------
Bool InGameUI::isSelecting( void )
{
	return m_isSelecting;
}

//-------------------------------------------------------------------------------------------------
/// get scroll amount
//-------------------------------------------------------------------------------------------------
void InGameUI::setScrollAmount( Coord2D amt )
{
	m_scrollAmt = amt;
}

//-------------------------------------------------------------------------------------------------
/// get scroll amount
//-------------------------------------------------------------------------------------------------
Coord2D InGameUI::getScrollAmount( void )
{
	return m_scrollAmt;
}

//-------------------------------------------------------------------------------------------------
/** Like the building "placement" mode, clicking on some buttons in the UI require us to
	* provide additional data by clicking on a target object/location in the world.  This
	* is where we enable that "mode" so that we can get the additional data needed for a
	* command from the user */
//-------------------------------------------------------------------------------------------------
void InGameUI::setGUICommand( const CommandButton *command )
{
	if (TheRecorder->getMode() == RECORDERMODETYPE_PLAYBACK)
		return;

	// sanity
	if( command )
	{

		if( BitTest( command->getOptions(), COMMAND_OPTION_NEED_TARGET ) == FALSE )
		{

			DEBUG_ASSERTCRASH( 0, ("setGUICommand: Command '%s' does not need additional user interaction\n",	
														command->getName().str()) );
			m_pendingGUICommand = NULL;
			m_mouseMode = MOUSEMODE_DEFAULT;
			return;

		}  // end if

		m_mouseMode = MOUSEMODE_GUI_COMMAND;

	}  // end if
	else
	{
		m_mouseMode = MOUSEMODE_DEFAULT;
	}

	// set the command
	m_pendingGUICommand = command;

	// set the mouse cursor for commands that need a targeting or to normal with no command
	if( command && BitTest( command->getOptions(), COMMAND_OPTION_NEED_TARGET ) && !command->isContextCommand() )
	{
		setMouseCursor( Mouse::ARROW );// This occurs on the mouse-up of a panel button, so make an arrow
		// the mouseoverhint code will take care of the cursor context, once the mouse leaves the panel
		// but we will set the radius cursor here, so you can see it bleeding out from beneath the panel

		setRadiusCursor(command->getRadiusCursorType(), //*****************************************************************
										command->getSpecialPowerTemplate(),
										command->getWeaponSlot());
	}
	else
	{
		if (TheMouse)
		{
			setMouseCursor( Mouse::ARROW );
		}
		setRadiusCursorNone();
	}

	m_mouseModeCursor = TheMouse->getMouseCursor();

}  // end setGUICommand

//-------------------------------------------------------------------------------------------------
/** Get the pending gui command */
//-------------------------------------------------------------------------------------------------
const CommandButton *InGameUI::getGUICommand( void ) const
{

	return m_pendingGUICommand;

} 

//-------------------------------------------------------------------------------------------------
/** Destroy any drawables we have in our placement icon array and set to NULL */
//-------------------------------------------------------------------------------------------------
void InGameUI::destroyPlacementIcons( void )
{
	Int i;

	for( i = 0; i < TheGlobalData->m_maxLineBuildObjects; ++i )
	{

		if( m_placeIcon[ i ] ) 
		{
			TheTerrainVisual->removeFactionBibDrawable(m_placeIcon[ i ]);
			TheGameClient->destroyDrawable( m_placeIcon[ i ] );
		}
		m_placeIcon[ i ] = NULL;

	}  // end for i
	TheTerrainVisual->removeAllBibs();

}  // end destroyPlacementIcons

//-------------------------------------------------------------------------------------------------
/** User has clicked on a built item that requires placement in the world.  We will 
	* record what that thing is so that the we can catch the next click in the world
	* and try to place the object there */
//-------------------------------------------------------------------------------------------------
void InGameUI::placeBuildAvailable( const ThingTemplate *build, Drawable *buildDrawable )
{

	// if building something, no radius cursor, thankew
	if (build != NULL)
		setRadiusCursorNone();
	m_placementRangeRingUp = FALSE;

	//
	// if we're setting another place available, but we're somehow already in the placement
	// mode, get out of it before we start a new one
	//
	if( m_pendingPlaceType != NULL && build != NULL )
		placeBuildAvailable( NULL, NULL );

	//
	// The wheeled/dragged heading is carried from one placement to the next so a row of walls or
	// bunkers goes down facing the same way. It belongs to that one structure though: turning a
	// bunker used to leave every later building - a supply centre, a war factory - wearing the same
	// offset off its own designed view angle, which reads as the building coming out backwards. A
	// different type starts square again.
	//
	if( build != NULL && build != m_placeAngleType )
	{
		m_placeAngleOffset = 0.0f;
		m_placeAngleType = build;
	}

	//
	// keep a record of what we are trying to place, if we are already trying to
	// place something, it is overwritten
	//
	m_pendingPlaceType = build;

	//Keep the prev pending place for left click deselection prevention in alternate mouse mode.
	//We want to keep our dozer selected after initiating construction.
	setPreventLeftClickDeselectionInAlternateMouseModeForOneClick( m_pendingPlaceSourceObjectID != INVALID_ID );
	m_pendingPlaceSourceObjectID = INVALID_ID;

	Object *sourceObject = NULL;
	if( buildDrawable )
		sourceObject = buildDrawable->getObject();
	if( sourceObject )
		m_pendingPlaceSourceObjectID = sourceObject->getID();

	//
	// hack, change our cursor to at least something different ... also note that it's
	// possible to not have the mouse yet, as some UI systems as part of initialization
	// make sure that there isn't anything valid for to "place build"
	//
	if( TheMouse )
	{

		if( build )
		{
			m_mouseMode = MOUSEMODE_BUILD_PLACE;
			m_placementLegal = TRUE;
			m_placementNudge.zero();
			m_mouseModeCursor = placementCursor( TRUE );

			Drawable *draw;

			// capture the mouse for our window, windows is lame and changes it if we don't
			TheMouse->capture();

			// hack for changing cursor
			setMouseCursor( (Mouse::MouseCursor)m_mouseModeCursor );

			// deselect all drawables, otherwise they move to the place we click
			///@ todo when message stream order more formalized eliminate this
//			TheInGameUI->deselectAllDrawables();

			// create a drawble of what we are building to be "attached" at the cursor
			draw = TheThingFactory->newDrawable( build, DRAWABLE_STATUS_NO_STATE_PARTICLES );
			if (sourceObject)
			{
				if (TheGlobalData->m_timeOfDay == TIME_OF_DAY_NIGHT)
					draw->setIndicatorColor(sourceObject->getControllingPlayer()->getPlayerNightColor());
				else
					draw->setIndicatorColor(sourceObject->getControllingPlayer()->getPlayerColor());
			}
			DEBUG_ASSERTCRASH( draw, ("Unable to create icon at cursor for placement '%s'\n",
												 build->getName().str()) );

			//
			// set the initial angle of the free floating building to the property from INI
			// we have this so we can have the "cool" face the user until they click and
			// pick an actual direction for placement
			//
			Real angle = build->getPlacementViewAngle();

			// don't forget to take into account the current view angle
			// angle += TheTacticalView->getAngle();	Don't do this - makes odd angled building placements.  jba.

			// carry over whatever heading the player wheeled to last time, so a row of walls or
			// bunkers can be laid down all facing the same way (see rotatePendingPlacement)
			angle = normalizeAngle( angle + m_placeAngleOffset );

			// set the angle in the icon we just created
			draw->setOrientation( angle );

			// set the build icon attached to the cursor to be "see-thru"
			dressPlacementPreview( draw );

			// set the "icon" in the icon array at the first index
			DEBUG_ASSERTCRASH( m_placeIcon[ 0 ] == NULL, ("placeBuildAvailable, build icon array is not empty!") );
			m_placeIcon[ 0 ] = draw;	

		}  // end if
		else
		{
			if (m_mouseMode == MOUSEMODE_BUILD_PLACE)
			{
				m_mouseMode = MOUSEMODE_DEFAULT;
				m_mouseModeCursor = Mouse::ARROW;
			}

			TheMouse->releaseCapture();
			setMouseCursor( Mouse::ARROW );
			setPlacementStart( NULL );

			// if we have a place icons destroy them
			destroyPlacementIcons();

			if( sourceObject )
			{
				ProductionUpdateInterface *puInterface = sourceObject->getProductionUpdateInterface();
				if( puInterface )
				{
					//Clear the special power mode for construction if we set it. Actually call it everytime
					//rather than checking if it's set before clearing (cheaper).
					puInterface->setSpecialPowerConstructionCommandButton( NULL );
				}
			}

		}  // end else

	}  // end if

}  // end placeBuildAvailable

//-------------------------------------------------------------------------------------------------
/** Return the thing we're attempting to place */
//-------------------------------------------------------------------------------------------------
const ThingTemplate *InGameUI::getPendingPlaceType( void )
{
	return m_pendingPlaceType;
}

//-------------------------------------------------------------------------------------------------
/** The see-thru structure the cursor is carrying, so the renderer can hang decoration off it -
	* the exit line, for one.  NULL whenever we are not in placement mode. */
//-------------------------------------------------------------------------------------------------
const Drawable *InGameUI::getPendingPlaceDrawable( void ) const
{
	if( m_pendingPlaceType == NULL )
		return NULL;

	return m_placeIcon[ 0 ];
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
const ObjectID InGameUI::getPendingPlaceSourceObjectID( void )
{

	return m_pendingPlaceSourceObjectID;

}  // end getPendingPlaceSourceObjectID

//-------------------------------------------------------------------------------------------------
/** Start the angle selection interface for selecting building angles when placing them */
//-------------------------------------------------------------------------------------------------
void InGameUI::setPlacementStart( const ICoord2D *start )
{

	// if we have a start point we turn "on" the interface, otherwise we turn it "off"
	if( start )
	{

		m_placeAnchorStart = *start;
		m_placeAnchorEnd = *start;
		m_placeAnchorInProgress = TRUE;

	}  // end if
	else
		m_placeAnchorInProgress = FALSE;

}  // end setPlacementStart

//-------------------------------------------------------------------------------------------------
/** Set the end anchor for the angle build interface */
//-------------------------------------------------------------------------------------------------
void InGameUI::setPlacementEnd( const ICoord2D *end )
{

	if( end )
		m_placeAnchorEnd = *end;

}  // end setPlacementEnd

//-------------------------------------------------------------------------------------------------
/** Is the angle selection interface for placing building at angles up? */
//-------------------------------------------------------------------------------------------------
Bool InGameUI::isPlacementAnchored( void )
{

	return m_placeAnchorInProgress;

}  // end isPlacementAnchored

//-------------------------------------------------------------------------------------------------
/** Get the start and end anchor points for the building angle selection interface */
//-------------------------------------------------------------------------------------------------
void InGameUI::getPlacementPoints( ICoord2D *start, ICoord2D *end )
{

	if( start )
		*start = m_placeAnchorStart;
	if( end )
		*end = m_placeAnchorEnd;

}  // end getPlacementPoints

//-------------------------------------------------------------------------------------------------
/** Return the angle of the drawable at the cursor if any */
//-------------------------------------------------------------------------------------------------
Real InGameUI::getPlacementAngle( void )
{

	if( m_placeIcon[ 0 ] )
		return m_placeIcon[ 0 ]->getOrientation();

	return 0.0f;

}  // end getPlacementAngle

//-------------------------------------------------------------------------------------------------
/** The heading a drag from 'start' to 'end' (both in screen pixels) aims a structure at.  The two
	* points are projected onto the terrain first, so the answer is a world heading and not a screen
	* one: dragging "up" the screen faces the building away from the camera whatever the camera is
	* turned to. */
//-------------------------------------------------------------------------------------------------
Real InGameUI::computePlacementAngle( const ICoord2D *start, const ICoord2D *end )
{
	Coord3D worldStart, worldEnd;

	TheTacticalView->screenToTerrain( start, &worldStart );
	TheTacticalView->screenToTerrain( end, &worldEnd );

	Coord2D v;
	v.x = worldEnd.x - worldStart.x;
	v.y = worldEnd.y - worldStart.y;

	Real angle = v.toAngle();

	// optional 45 degree snap (SnapBuildPlacementTo45 in Options.ini) - lines walls and
	// defenses up with the base instead of leaving them at whatever the drag produced.
	if( TheGlobalData->m_snapBuildPlacementTo45 )
		angle = snapAngleTo45( angle );

	return angle;

}  // end computePlacementAngle

//-------------------------------------------------------------------------------------------------
/** Experimental GridBuildPlacement (Options.ini): put the structure on the pathfinder's build grid
	* instead of wherever the cursor happened to be to the pixel.  Everything the pathfinder does is
	* in 10-unit cells (PATHFIND_CELL_SIZE), and a structure dropped a couple of units off that grid
	* blocks a strip of a cell it does not fill, which is what leaves the gaps you cannot walk a
	* soldier through between two buildings that look flush.  Snapping the footprint's edges to the
	* grid lines makes neighbours share an edge exactly, and a row of them come out straight.
	*
	* The footprint is the template's own geometry, turned by the heading it is being placed at: a
	* box is major along its facing and minor across it, and anything round is its bounding circle.
	* At 45 degrees the axis-aligned extents grow, which is right - that is the ground it covers. */
//-------------------------------------------------------------------------------------------------
static void placementHalfSizes( const ThingTemplate *what, Real *major, Real *minor )
{
	const GeometryInfo &geom = what->getTemplateGeometryInfo();
	*major = geom.getMajorRadius();
	*minor = geom.getMinorRadius();
	if( geom.getGeomType() != GEOMETRY_BOX )
		*major = *minor = geom.getBoundingCircleRadius();

	// The ground a structure really takes is not its collision box: BuildAssistant's own clearance
	// check grows both radii by the factory bib (isLocationClearOfObjects' myBounds), and the bib is
	// the concrete apron you can see under it.  Snapping the bare box left that apron hanging off
	// the grid by the bib's width, which is what makes a placed building look like it did not
	// quite sit down on its squares.
	*major += what->getFactoryExtraBibWidth();
	*minor += what->getFactoryExtraBibWidth();
}

static void placementHalfExtents( const ThingTemplate *what, Real angle, Real *halfX, Real *halfY )
{
	Real major, minor;
	placementHalfSizes( what, &major, &minor );

	const Real c = (Real)fabs( Cos( angle ) );
	const Real sn = (Real)fabs( Sin( angle ) );

	*halfX = major * c + minor * sn;
	*halfY = major * sn + minor * c;
}

void InGameUI::snapPlacementToGrid( Coord3D *world, const ThingTemplate *what, Real angle ) const
{
	if( world == NULL || what == NULL || TheGlobalData->m_gridBuildPlacement == FALSE )
		return;

	Real halfX, halfY;
	placementHalfExtents( what, angle, &halfX, &halfY );

	world->x = snapPlacementAxis( world->x, halfX );
	world->y = snapPlacementAxis( world->y, halfY );

}  // end snapPlacementToGrid

//-------------------------------------------------------------------------------------------------
/** Shift held on the drag: a wall already tiles from any drag, so it is left to do that. */
//-------------------------------------------------------------------------------------------------
Bool InGameUI::placesRow( void )
{
	return m_pendingPlaceType != NULL && TheKeyboard && TheKeyboard->isShift() &&
				 !TheBuildAssistant->isLineBuildTemplate( m_pendingPlaceType );
}

//-------------------------------------------------------------------------------------------------
/** Every piece faces 'angle', the heading on the ghost before the drag began: the drag is spent on
	* the row, so it cannot aim as well.  The row stops where the money does, which is what the logic
	* would do to the orders past it anyway (canMakeUnit per MSG_DOZER_CONSTRUCT); the player sees the
	* row that will go up.  Legality is not asked here - the ghost and the click each ask it per piece. */
//-------------------------------------------------------------------------------------------------
void InGameUI::computePlacementRow( const ThingTemplate *what, Real angle, const Coord3D *start,
																		const Coord3D *end, std::vector<Coord3D> *positions ) const
{
	//
	// A factory's door needs its lane clear of the next structure (isLocationClearOfObjects' exit
	// check), so a row running out of the door or into it leaves the lane between each pair.
	// Half the lane on each half-length: the pair's shared box grows by the whole of it.
	//
	Real major, minor;
	placementHalfSizes( what, &major, &minor );
	const Real halfFacing = major + what->getFactoryExitWidth() * 0.5f;

	Int most = TheGlobalData->m_maxLineBuildObjects;
	Player *player = ThePlayerList->getLocalPlayer();
	const Int cost = what->calcCostToBuild( player );
	if( cost > 0 )
	{
		const Int affordable = (Int)( player->getMoney()->countMoney() / cost );
		if( affordable < most )
			most = affordable;
	}

	Coord2D step;
	const Int count = placementRow( end->x - start->x, end->y - start->y, (Real)Cos( angle ),
																	(Real)Sin( angle ), halfFacing, minor,
																	TheGlobalData->m_gridBuildPlacement, most, &step );

	positions->clear();
	for( Int i = 0; i < count; i++ )
	{
		Coord3D pos;
		pos.x = start->x + step.x * i;
		pos.y = start->y + step.y * i;
		pos.z = TheTerrainLogic->getGroundHeight( pos.x, pos.y );
		positions->push_back( pos );
	}

}  // end computePlacementRow

//-------------------------------------------------------------------------------------------------
/** The legality question asked of the spot under the ghost, in one place - the nudge search asks it
	* of every candidate too.  IGNORE_STEALTHED is deliberate: a structure you cannot put down is a
	* place a stealthed unit is standing, and the placement cursor is not a detector.  The click
	* re-asks with FAIL_STEALTHED_WITHOUT_FEEDBACK, which is where that actually gets decided. */
//-------------------------------------------------------------------------------------------------
UnsignedInt InGameUI::placementCheckOptions( void )
{
	return BuildAssistant::USE_QUICK_PATHFIND |
				 BuildAssistant::TERRAIN_RESTRICTIONS |
				 BuildAssistant::CLEAR_PATH |
				 BuildAssistant::NO_OBJECT_OVERLAP |
				 BuildAssistant::SHROUD_REVEALED |
				 BuildAssistant::IGNORE_STEALTHED;

}  // end placementCheckOptions

//-------------------------------------------------------------------------------------------------
/** The ground a structure of this template would stand on, put down here at this heading.  The
	* geometry's own bounds, which for a box at any angle is the rectangle around the turned box:
	* two of those sharing ground is close enough to "on top of each other" for the client to refuse
	* a second click, and the logic side asks the real question afterwards anyway. */
//-------------------------------------------------------------------------------------------------
void InGameUI::placementFootprint( const ThingTemplate *what, const Coord3D *world, Real angle,
																	 Region2D *footprint )
{
	what->getTemplateGeometryInfo().get2DBounds( *world, angle, *footprint );
}

//-------------------------------------------------------------------------------------------------
/** Do two footprints share any ground?  Touching edge to edge does not count: structures are built
	* flush against each other on the build grid all game, and a row of them is not an overlap. */
//-------------------------------------------------------------------------------------------------
Bool InGameUI::footprintsOverlap( const Region2D *a, const Region2D *b )
{
	return a->lo.x < b->hi.x && b->lo.x < a->hi.x &&
				 a->lo.y < b->hi.y && b->lo.y < a->hi.y;

}  // end footprintsOverlap

//-------------------------------------------------------------------------------------------------
/** Remember a structure just ordered, so the click after it knows the ground is spoken for.  Round
	* a ring: the oldest goes, which is the one most likely to be standing by now. */
//-------------------------------------------------------------------------------------------------
void InGameUI::recordPendingPlacement( const ThingTemplate *what, const Coord3D *world, Real angle )
{
	if( what == NULL || world == NULL || TheGameLogic == NULL )
		return;

	PendingPlacement *pending = &m_pendingPlacement[ m_pendingPlacementAt ];
	m_pendingPlacementAt = ( m_pendingPlacementAt + 1 ) % PENDING_PLACEMENTS;

	placementFootprint( what, world, angle, &pending->footprint );
	pending->frame = TheGameLogic->getFrame();

	//
	// frame zero is a real frame and zero is how an empty slot is spelt, so an order placed on it
	// is remembered from frame one.  It costs one frame of one order in the first thirtieth of a
	// second of a match, when nothing is built yet.
	//
	if( pending->frame == 0 )
		pending->frame = 1;

}  // end recordPendingPlacement

//-------------------------------------------------------------------------------------------------
/** Would a structure put down here land on one already ordered?  Orders older than
	* PENDING_PLACEMENT_FRAMES are ignored rather than cleared: by then the structure is either
	* standing, where the ordinary check sees it, or the logic refused the order and the ground is
	* free again. */
//-------------------------------------------------------------------------------------------------
Bool InGameUI::overlapsPendingPlacement( const Coord3D *world, const ThingTemplate *what,
																				 Real angle ) const
{
	if( what == NULL || world == NULL || TheGameLogic == NULL )
		return FALSE;

	Region2D mine;
	placementFootprint( what, world, angle, &mine );

	const UnsignedInt now = TheGameLogic->getFrame();

	for( Int i = 0; i < PENDING_PLACEMENTS; i++ )
	{
		const PendingPlacement *pending = &m_pendingPlacement[ i ];

		if( pending->frame == 0 || now < pending->frame ||
				now - pending->frame > PENDING_PLACEMENT_FRAMES )
			continue;

		if( footprintsOverlap( &mine, &pending->footprint ) )
			return TRUE;
	}

	return FALSE;

}  // end overlapsPendingPlacement

//-------------------------------------------------------------------------------------------------
void InGameUI::forgetPendingPlacements( void )
{
	for( Int i = 0; i < PENDING_PLACEMENTS; i++ )
		m_pendingPlacement[ i ].frame = 0;

	m_pendingPlacementAt = 0;

}  // end forgetPendingPlacements

//-------------------------------------------------------------------------------------------------
/** NudgeBuildPlacement (Options.ini): the spot under the cursor is blocked - a rock, a neighbour's
	* bib, ground too steep by a hair - so find the nearest one that is not and put the structure
	* there instead of turning it red and leaving the player to hunt for the legal pixel by hand.
	*
	* Nearest is meant literally: candidates come out of placementNudgeOffset in distance order, so
	* the first one that fits is the closest one that fits.  A step is one pathfinder cell, which is
	* also what GridBuildPlacement snaps to, so a nudged structure stays on the same grid as the ones
	* already down.
	*
	* The cost is in the pathfind, not in the ground: EA's own comment on the caller says the check
	* is too expensive to run every frame, and that is the CLEAR_PATH half of it.  So the sweep asks
	* the cheap questions first - shroud, overlap, terrain - across all PLACEMENT_NUDGE_TRIES cells,
	* and only spends a pathfind on a cell that already passed those.
	*
	* ponytail: reaches PLACEMENT_NUDGE_RINGS cells (100 units) and gives up, and gives up sooner
	* after PLACEMENT_NUDGE_PATHFINDS unreachable candidates.  Both are flat numbers; scale them off
	* the structure's own footprint if a supply centre still ends up hunting too far. */
//-------------------------------------------------------------------------------------------------
Bool InGameUI::nudgePlacementToLegal( Coord3D *world, const ThingTemplate *what, Real angle,
																			Object *builderObject ) const
{
	if( world == NULL || what == NULL || TheGlobalData->m_nudgeBuildPlacement == FALSE )
		return FALSE;

	// a wall tiles from the two points you dragged between; sliding one end off that line is not help
	if( TheBuildAssistant->isLineBuildTemplate( what ) )
		return FALSE;

	// CLEAR_PATH is the pathfind, and USE_QUICK_PATHFIND only says which one it uses
	const UnsignedInt fullOptions = placementCheckOptions();
	const UnsignedInt groundOptions = fullOptions & ~BuildAssistant::CLEAR_PATH;
	Int pathfinds = 0;

	for( Int i = 0; i < PLACEMENT_NUDGE_TRIES; i++ )
	{
		Real dx, dy;
		placementNudgeOffset( i, (Real)PLACEMENT_CELL, &dx, &dy );

		Coord3D test = *world;
		test.x += dx;
		test.y += dy;
		test.z = TheTerrainLogic->getGroundHeight( test.x, test.y );

		// is there room at all?  cheap, and it throws out all but a handful of the candidates
		if( TheBuildAssistant->isLocationLegalToBuild( &test, what, angle, groundOptions,
																									 builderObject, NULL ) != LBC_OK )
			continue;

		// room a structure you have already ordered is on its way to is not room
		if( overlapsPendingPlacement( &test, what, angle ) )
			continue;

		// there is - can the builder actually walk to it?
		if( TheBuildAssistant->isLocationLegalToBuild( &test, what, angle, fullOptions,
																									 builderObject, NULL ) == LBC_OK )
		{
			*world = test;
			return TRUE;
		}

		//
		// Room the builder cannot reach - across a cliff, behind a wall.  A few of those are
		// normal near the edge of a base; a lot of them means the whole neighbourhood is cut off,
		// and there is no sense pathfinding to every cell of it twice a frame.
		//
		if( ++pathfinds >= PLACEMENT_NUDGE_PATHFINDS )
			break;

	}  // end for i

	return FALSE;

}  // end nudgePlacementToLegal


//-------------------------------------------------------------------------------------------------
/** Aim the structure sitting on the cursor at 'angle', and keep that heading for the placements
	* that follow - the same offset the wheel writes (see rotatePendingPlacement), so a wall aimed by
	* dragging carries on in the direction it was aimed instead of snapping back to the template's own
	* view angle on the next piece. */
//-------------------------------------------------------------------------------------------------
void InGameUI::setPlacementAngle( Real angle )
{
	if( m_pendingPlaceType == NULL )
		return;

	m_placeAngleOffset = normalizeAngle( angle - m_pendingPlaceType->getPlacementViewAngle() );

	for( Int i = 0; i < TheGlobalData->m_maxLineBuildObjects; i++ )
		if( m_placeIcon[ i ] )
			m_placeIcon[ i ]->setOrientation( angle );

}  // end setPlacementAngle

//-------------------------------------------------------------------------------------------------
/** Turn the structure sitting on the cursor by 'steps' eighths of a turn.  The chosen heading is
	* kept for the placements that follow, so a row of walls or a line of bunkers can be laid down
	* facing the same way without re-aiming each one - it is an offset rather than an absolute angle
	* so that each structure still starts from its own designed view angle.  Does nothing while the
	* drag-to-aim anchor is down: that interface recomputes the angle from the drag every frame and
	* would throw this away on the next one. */
//-------------------------------------------------------------------------------------------------
Bool InGameUI::rotatePendingPlacement( Int steps )
{
	const Real step = PI / 4.0f;

	if( steps == 0 || m_pendingPlaceType == NULL || m_placeIcon[ 0 ] == NULL || isPlacementAnchored() )
		return FALSE;

	m_placeAngleOffset = normalizeAngle( m_placeAngleOffset + steps * step );

	for( Int i = 0; i < TheGlobalData->m_maxLineBuildObjects; i++ )
		if( m_placeIcon[ i ] )
			m_placeIcon[ i ]->setOrientation( normalizeAngle( m_placeIcon[ i ]->getOrientation() + steps * step ) );

	return TRUE;

}  // end rotatePendingPlacement

//-------------------------------------------------------------------------------------------------
/** Mark given Drawable as "selected". */
//-------------------------------------------------------------------------------------------------
void InGameUI::selectDrawable( Drawable *draw )
{

	if( draw->isSelected() == FALSE )
	{

		m_frameSelectionChanged = TheGameLogic->getFrame();
		// set the selection in the drawable
		draw->friend_setSelected();

		// add to our selected list
		m_selectedDrawables.push_front( draw );

		// we now have one more selected drawable
		incrementSelectCount(); 


		// evaluate whether our selection consists of exactly one angry mob
		evaluateSoloNexus( draw );

		// the control needs to update its context sensitive display now
		TheControlBar->onDrawableSelected( draw );

	}  // end if

}  // end selectDrawable

//-------------------------------------------------------------------------------------------------
/** Clear "selected" status of Drawable. */
//-------------------------------------------------------------------------------------------------
void InGameUI::deselectDrawable( Drawable *draw )
{

	if( draw->isSelected() )
	{
		m_frameSelectionChanged = TheGameLogic->getFrame();
		// clear the selected bit out of the drawable
		draw->friend_clearSelected();

		// find the drawable entry in our list
		DrawableListIt findIt = std::find( m_selectedDrawables.begin(), 
																			 m_selectedDrawables.end(), 
																			 draw );

		// sanity
		DEBUG_ASSERTCRASH( findIt != m_selectedDrawables.end(),
											 ("deselectDrawable: Drawable not found in the selected drawable list '%s'\n",
											 draw->getTemplate()->getName().str()) );

		// remove it from the selected drawable list		
		m_selectedDrawables.erase( findIt );

		// keep out own internal count happy
		decrementSelectCount(); 

		// evaluate whether our selection consists of exactly one angry mob
		evaluateSoloNexus();

		// the control needs to update its context sensitive display now
		TheControlBar->onDrawableDeselected( draw );

	}  // end if

}  // end deselectDrawable

//-------------------------------------------------------------------------------------------------
/** Clear all drawables' "select" status */
//-------------------------------------------------------------------------------------------------
void InGameUI::deselectAllDrawables( void )
{
	const DrawableList *selected = TheInGameUI->getAllSelectedDrawables();
	const Bool hadSelectedDrawables = !selected->empty();

	// loop through all the selected drawables
	for ( DrawableListCIt it = selected->begin(); it != selected->end(); )
	{

		// get drawable and increment iterator, we will invalidate it as we deselect
		Drawable* draw = *it++;

		// do the deselection
		TheInGameUI->deselectDrawable( draw );

	}  // end while

	// keep our list all tidy
	m_selectedDrawables.clear();

	// a fresh selection is the player's choice, and a unit still underground is not in it
	m_tunnelTripRiders.clear();


	// our selection can no longer consist of exactly one angry mob
	m_soloNexusSelectedDrawableID = INVALID_DRAWABLE_ID;


	// TheSuperHackers @tweak Only send this when something actually was selected. The rest of the
	// spam is dropped in the stream, where a clear followed by a fresh selection is recognised.
	if( hadSelectedDrawables )
	{
		// The message carried one boolean that nothing on the receiving side ever read: the handler
		// deselects the whole group and always did.
		TheMessageStream->appendMessage( GameMessage::MSG_DESTROY_SELECTED_GROUP );
	}
}



//-------------------------------------------------------------------------------------------------
/** Return the list of all the currently selected Drawable pointers. */
//-------------------------------------------------------------------------------------------------
const DrawableList *InGameUI::getAllSelectedDrawables( void ) const
{
	return &m_selectedDrawables;
}

//-------------------------------------------------------------------------------------------------
/** A tunnel hides its passengers, and hiding a drawable deselects it. A player who never asked for
	* the tunnel, because a move order took it on its own, lost the unit from the selection on the way
	* through; remember it here so it comes back out selected. A trip ordered by clicking the tunnel
	* still deselects, as it always did. */
//-------------------------------------------------------------------------------------------------
void InGameUI::holdSelectionThroughTunnel( Drawable *draw )
{
	const Object *obj = draw->getObject();
	if( obj == NULL || obj->getAI() == NULL || !obj->getAI()->hasTunnelTrip() )
		return;
	if( obj->getControllingPlayer() != ThePlayerList->getLocalPlayer() )
		return;

	m_tunnelTripRiders.push_back( obj->getID() );
}

//-------------------------------------------------------------------------------------------------
void InGameUI::restoreSelectionAfterTunnel( Drawable *draw )
{
	const Object *obj = draw->getObject();
	if( obj == NULL )
		return;

	std::vector<ObjectID>::iterator rider = std::find( m_tunnelTripRiders.begin(), m_tunnelTripRiders.end(), obj->getID() );
	if( rider == m_tunnelTripRiders.end() )
		return;
	m_tunnelTripRiders.erase( rider );

	// the tunnel took it out of the logic side's group too, so it goes back in the way a click adds it
	GameMessage *groupMsg = TheMessageStream->appendMessage( GameMessage::MSG_CREATE_SELECTED_GROUP );
	groupMsg->appendBooleanArgument( FALSE );
	groupMsg->appendObjectIDArgument( obj->getID() );
	selectDrawable( draw );
}

//-------------------------------------------------------------------------------------------------
/** Return the list of all the currently selected Drawable pointers. */
//-------------------------------------------------------------------------------------------------
const DrawableList *InGameUI::getAllSelectedLocalDrawables( void )
{
	m_selectedLocalDrawables.clear();
	for (DrawableList::const_iterator it = m_selectedDrawables.begin(); it != m_selectedDrawables.end(); ++it)
	{
		Drawable *draw = (*it);
		if (draw && draw->getObject() && draw->getObject()->isLocallyControlled())
			m_selectedLocalDrawables.push_back( draw );
	}
	return &m_selectedLocalDrawables;
}

//-------------------------------------------------------------------------------------------------
/** Return poiner to the first selected drawable, if any */
//-------------------------------------------------------------------------------------------------
Drawable *InGameUI::getFirstSelectedDrawable( void )
{

	// sanity
	if( m_selectedDrawables.empty() )
		return NULL;  // this is valid, nothing is selected

	return m_selectedDrawables.front();

}  // end getFirstSelectedDrawable

//-------------------------------------------------------------------------------------------------
/** Return true if the selected ID is in the drawable list */
//-------------------------------------------------------------------------------------------------
Bool InGameUI::isDrawableSelected( DrawableID idToCheck ) const
{

	for( DrawableListCIt it = m_selectedDrawables.begin(); it != m_selectedDrawables.end(); ++it ) 
	{

		if( (*it)->getID() == idToCheck )
			return TRUE;

	}  // end for

	return FALSE;

}  // end isDrawableSelected

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
Bool InGameUI::isAnySelectedKindOf( KindOfType kindOf ) const
{
	Drawable *draw;

	for( DrawableListCIt it = m_selectedDrawables.begin();
			 it != m_selectedDrawables.end();
			 ++it )
	{

		/** @todo, it seems like we might want to keep a list of drawable pointers so we
		don't have to do this lookup ... it seems "tightly coupled" to me (CBD) */
		// get the drawable from the ID
		draw = *it;
		if( draw && draw->isKindOf( kindOf ) )
			return TRUE;

	}  // end for, it

	return FALSE;  // no selected objects are of the kind of type

}  // end isAnySelectedKindOf

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
Bool InGameUI::isAllSelectedKindOf( KindOfType kindOf ) const
{
	Drawable *draw;

	for( DrawableListCIt it = m_selectedDrawables.begin();
			 it != m_selectedDrawables.end();
			 ++it )
	{

		/** @todo, it seems like we might want to keep a list of drawable pointers so we
		don't have to do this lookup ... it seems "tightly coupled" to me (CBD) */
		// get the drawable from the ID
		draw = *it;
		if( draw && draw->isKindOf( kindOf ) == FALSE )
			return FALSE;  // not all objects are of the kind of type

	}  // end for, it

	return TRUE;  // all objects have this kindof bit set in them

}  // end isAllSelectedKindOf

//-------------------------------------------------------------------------------------------------
/** Set the input enabled/disabled */
//-------------------------------------------------------------------------------------------------
void InGameUI::setInputEnabled( Bool enable )
{
	if(!enable)
		setSelecting( FALSE );
	
	Bool wasEnabled = m_inputEnabled;

	m_inputEnabled = enable;
	
	if (wasEnabled && !enable)
	{
		clearModifierModes();
	}
}

//-------------------------------------------------------------------------------------------------
/** Forget every "mode" a held key puts us in.
	*
	* Two things can eat the key release that would normally end one of these.  A cinematic disables
	* input, so a ctrl held as it starts and let go during it is never seen released, and the game
	* still thinks you are force-attacking when it ends.  Losing the window's focus does the same
	* thing and is the one a player meets: alt-tab out with alt held - which is how you alt-tab -
	* and you come back in waypoint mode, laying a route with every click, until you press and
	* release alt once more.  WinMain calls this from both focus messages, next to the keyboard's
	* own reset. */
//-------------------------------------------------------------------------------------------------
void InGameUI::clearModifierModes( void )
{
	setForceAttackMode( false );			// CTRL
	setForceMoveMode( false );				// apparently unmapped in current CommandMap.ini
	setWaypointMode( false );					// ALT
	setPreferSelectionMode( false );	// SHIFT
	setCameraRotateLeft( false );			// KP4
	setCameraRotateRight( false );		// KP6
	setCameraZoomIn( false );					// KP8
	setCameraZoomOut( false );				// KP2
}

//-------------------------------------------------------------------------------------------------
/** Drawable is being destroyed, clean up any UI elements associated with it. */
//-------------------------------------------------------------------------------------------------
void InGameUI::disregardDrawable( Drawable *draw )
{

	// make sure drawable is no longer selected
	deselectDrawable( draw );		

}

//-------------------------------------------------------------------------------------------------
/** This is called after the UI has been drawn. */
//-------------------------------------------------------------------------------------------------
void InGameUI::postDraw( void )
{
	// drawHudOverlay is NOT called here - it goes on top of everything, see W3DInGameUI::draw
	drawProductionStrip();
	drawSkillStrip();			// the same shelf, the other end of it
	drawBlindSpots();
	drawPlacementReach();		// after the shade, so the outline stays bright over it
	drawSpectatorPage();
	drawFeed();
	drawChat();

	if( m_militarySubtitle )
	{
		ICoord2D pos;
		pos.x = m_militarySubtitle->position.x;
		pos.y = m_militarySubtitle->position.y;
		Color dropColor;
		UnsignedByte r, g, b, a;
		GameGetColorComponents( m_militarySubtitle->color, &r, &g, &b, &a );
		dropColor = GameMakeColor( 0, 0, 0, a );
		for(Int i = 0; i <= m_militarySubtitle->currentDisplayString; i++)
		{
			m_militarySubtitle->displayStrings[i]->draw(pos.x,pos.y, m_militarySubtitle->color,dropColor );
			Int height;
			m_militarySubtitle->displayStrings[i]->getSize(NULL, &height);
			pos.y += height;
		}
		if( m_militarySubtitle->blockDrawn )
		{
			ICoord2D size;
			size.y = m_militarySubtitle->displayStrings[m_militarySubtitle->currentDisplayString]->getFont()->height;
			size.x = size.y * 0.8f;
			TheDisplay->drawFillRect(m_militarySubtitle->blockPos.x, m_militarySubtitle->blockPos.y, size.x, size.y, m_militarySubtitle->color);
		}

	}

	// draw superweapon timers
  // Also responsible for Eva saying "Superweapon is ready for launch"
  //  IMPORTANT: Don't bail out of this block early just because you don't 
  //  want to display the timers -- Eva still needs to be checked
	if (TheGameLogic->getFrame() > 0 )
	{
		//
		// The countdowns are cameos in the top right, not a column of names and clocks running down
		// over the battlefield. A superweapon is recognised by the picture it is fired from, a name
		// and a colon and a clock is a paragraph to read at a glance, and eight of them was a wall
		// of text over the terrain. Everything the loop below finds is gathered into one list and
		// laid out at the end of it, soonest first.
		//
		m_superweaponIconCount = 0;
		m_superweaponIconTotal = 0;
		m_spectatorSuperweapons.clear();

		for (Int i=0; i<MAX_PLAYER_COUNT; ++i)
		{
			for (SuperweaponMap::iterator mapIt = m_superweapons[i].begin(); mapIt != m_superweapons[i].end(); ++mapIt)
			{
				for (SuperweaponList::iterator listIt = mapIt->second.begin(); listIt != mapIt->second.end(); ++listIt)
				{
					SuperweaponInfo *info = *listIt;
					DEBUG_ASSERTCRASH(info, ("No superweapon info!"));
					if (info && !info->m_hiddenByScript && !info->m_hiddenByScience)
					{
						Object * owningObject = TheGameLogic->findObjectByID(info->m_id);
						if (owningObject)
						{
							
							// We don't draw our timers until we are finished with construction.
							// It is important that let the SpecialPowerUpdate is add its timer in its contructor,,
							// since the science for it could be added before construction is finished,
							// And thus the timer set to READY before the timer is first drawn, here
							if ( owningObject->testStatus( OBJECT_STATUS_UNDER_CONSTRUCTION ))
								continue;

							SpecialPowerModuleInterface *module = owningObject->getSpecialPowerModule(info->getSpecialPowerTemplate());
							if (module)
							{
								// found one - draw it
 								Bool isReady = module->isReady();
 								Int readySecs;
 
								// IsReady includes disabledness, so if you have a 0 timer disabled super, you don't want 
 								// the UnsignedInt to wrap around to hundreds of millions of seconds.
 								if( module->getReadyFrame() < TheGameLogic->getFrame() )
									readySecs = 0;
 								else 
 									readySecs = ControlBar_secondsFromFrames( (Real)(module->getReadyFrame() - TheGameLogic->getFrame()) );
								// Yes, integer math.  We can't have float imprecision display 4:01 on a disabled superweapon.
 
                // Only if we actually changed the ready status do we want to play an Eva event.
                if ( isReady && !info->m_evaReadyPlayed )
                {
                  if ( TheGameLogic->getFrame() > 0 )
                  {
                    feedAct( owningObject->getControllingPlayer(), superweaponCameo( info->getSpecialPowerTemplate() ),
                             WideCharStringToMultiByte( owningObject->getTemplate()->getDisplayName().str() ),
                             "ready", "GUI:HudSuperweaponReady" );

                    SpecialPowerType type = module->getSpecialPowerTemplate()->getSpecialPowerType();
                  
                    Player *localPlayer = ThePlayerList->getLocalPlayer();
                  
                    if( type == SPECIAL_PARTICLE_UPLINK_CANNON || type == SUPW_SPECIAL_PARTICLE_UPLINK_CANNON || type == LAZR_SPECIAL_PARTICLE_UPLINK_CANNON )
                    {
                      if ( localPlayer == owningObject->getControllingPlayer() )
                      {
                        TheEva->setShouldPlay(EVA_SuperweaponReady_Own_ParticleCannon);
                      }
                      else if ( localPlayer->getRelationship(owningObject->getTeam()) != ENEMIES )
                      {
                        // Note: counting relationship NEUTRAL as ally. Not sure if this makes a difference???
                        TheEva->setShouldPlay(EVA_SuperweaponReady_Ally_ParticleCannon);
                      }
                      else
                      {
                        TheEva->setShouldPlay(EVA_SuperweaponReady_Enemy_ParticleCannon);
                      }
                    }
                    else if( type == SPECIAL_NEUTRON_MISSILE || type == NUKE_SPECIAL_NEUTRON_MISSILE || type == SUPW_SPECIAL_NEUTRON_MISSILE )
                    {
                      if ( localPlayer == owningObject->getControllingPlayer() )
                      {
                        TheEva->setShouldPlay(EVA_SuperweaponReady_Own_Nuke);
                      }
                      else if ( localPlayer->getRelationship(owningObject->getTeam()) != ENEMIES )
                      {
                        // Note: counting relationship NEUTRAL as ally. Not sure if this makes a difference???
                        TheEva->setShouldPlay(EVA_SuperweaponReady_Ally_Nuke);
                      }
                      else
                      {
                        TheEva->setShouldPlay(EVA_SuperweaponReady_Enemy_Nuke);
                      }
                    }
                    else if (type == SPECIAL_SCUD_STORM)
                    {
                      if ( localPlayer == owningObject->getControllingPlayer() )
                      {
                        TheEva->setShouldPlay(EVA_SuperweaponReady_Own_ScudStorm);
                      }
                      else if ( localPlayer->getRelationship(owningObject->getTeam()) != ENEMIES )
                      {
                        // Note: counting relationship NEUTRAL as ally. Not sure if this makes a difference???
                        TheEva->setShouldPlay(EVA_SuperweaponReady_Ally_ScudStorm);
                      }
                      else
                      {
                        TheEva->setShouldPlay(EVA_SuperweaponReady_Enemy_ScudStorm);
                      }
                    }
                  }
                  info->m_evaReadyPlayed = true;
                }
                else
                {
                  if ( !isReady )
                    info->m_evaReadyPlayed = false; // Reset Eva for next time
                }
              
                // hand it to the strip
                if ( !m_superweaponHiddenByScript )
                {
                  info->m_forceUpdateText = false;
                  info->m_ready = isReady;
                  info->m_timestamp = readySecs;

                  //
                  // How far through the charge it is, for the radial sweep. The module knows the
                  // frame it comes ready on and the template knows how long a whole charge takes,
                  // and the difference between the two is everything that has already been paid.
                  //
                  const UnsignedInt reload = info->getSpecialPowerTemplate()->getReloadTime();
                  Int percent = 100;
                  if ( !isReady && reload > 0 )
                  {
                    const UnsignedInt left = ( module->getReadyFrame() > TheGameLogic->getFrame() )
                                             ? module->getReadyFrame() - TheGameLogic->getFrame() : 0;
                    percent = ( left >= reload ) ? 0
                                                 : REAL_TO_INT( 100.0f * (Real)( reload - left ) / (Real)reload );
                  }

                  const CommandButton *button = powerButton( info->getSpecialPowerTemplate() );
                  const Image *cameo = button ? button->getButtonImage() : NULL;
                  addSuperweaponIcon( cameo, readySecs, percent, isReady, info->getColor() );
                  SpectatorSuperweapon listed = { i, cameo, readySecs, isReady, button };
                  m_spectatorSuperweapons.push_back( listed );
                }
                if (info->getSpecialPowerTemplate()->isSharedNSync())
                  break; // Wow, it is almost too easy!
                // This prevents redundant timers for shared powers/superweapons
                // No matter how many specialpowermodules register their timers with me,
                // I will only draw the timer of the first valid one in my list,
                // since they all have the same template, ans they all
                // use the Player::getReadyFrame() functions to stay in sync.
              }
						}
					}
				}
			}
		}

		drawSuperweaponStrip();
	}

	// draw named timers
	if (TheGameLogic->getFrame() > 0 && m_showNamedTimers)
	{
//		Int namedTimerCount = 0;
		Bool reverseXDir = (m_namedTimerPosition.x >= 0.5f);
		Int startX = (Int)(m_namedTimerPosition.x * TheDisplay->getWidth());
		Int startY = (Int)(m_namedTimerPosition.y * TheDisplay->getHeight());
		Color bgColor = GameMakeColor( 0, 0, 0, 255 );
		for (NamedTimerMapIt mapIt = m_namedTimers.begin(); mapIt != m_namedTimers.end(); ++mapIt)
		{
			AsciiString timerName = mapIt->first;
			NamedTimerInfo *info = mapIt->second;
			DEBUG_ASSERTCRASH(info, ("No namedTimer info!"));
			if (info)
			{
				// found one - draw it
				UnicodeString line;
				Int framesLeft = TheScriptEngine->getCounter(timerName)->value;
				UnsignedInt readyFrame = TheGameLogic->getFrame();
				if (framesLeft > 0)
					readyFrame += framesLeft;
				Int readySecs = ControlBar_secondsFromFrames( (Real)(readyFrame - TheGameLogic->getFrame()) );
				if ( (info->isCountdown && readySecs != info->timestamp) || (!info->isCountdown && framesLeft != info->timestamp) )
				{
					if (!readySecs && info->isCountdown)
					{
						// go bold - we're good to go
						info->displayString->setFont( TheFontLibrary->getFont( m_namedTimerReadyFont, 
							TheGlobalLanguageData->adjustFontSize(m_namedTimerReadyPointSize), m_namedTimerReadyBold ) );
					}
					else
					{
						// if we were at 0, we've just fired - kill the bold
						if (info->timestamp == 0 || info->isCountdown)
						{
							info->displayString->setFont( TheFontLibrary->getFont( m_namedTimerNormalFont, 
								TheGlobalLanguageData->adjustFontSize(m_namedTimerNormalPointSize), m_namedTimerNormalBold ) );
						}
					}

					info->timestamp = readySecs;
					Int min = readySecs/60;
					Int sec = readySecs - min*60;
					
					if (!info->isCountdown)
						line.format(L"%s %d", info->timerText.str(), framesLeft);
					else
					{
						if (sec >= 10)
							line.format(L"%s %d:%d", info->timerText.str(), min, sec);
						else
							line.format(L"%s %d:0%d", info->timerText.str(), min, sec);
					}
					info->displayString->setText(line);
				}

				// draw the text
				Int drawX = startX;
				if (reverseXDir)
					drawX -= info->displayString->getWidth();
				if (!readySecs && info->isCountdown)
				{
					if ( m_namedTimerFlashDuration != 0.0f )
					{
						if ( TheGameLogic->getFrame() >= m_namedTimerLastFlashFrame + (Int)(m_namedTimerFlashDuration) )
						{
							m_namedTimerUsedFlashColor = !m_namedTimerUsedFlashColor;
							m_namedTimerLastFlashFrame = TheGameLogic->getFrame();
						}
						info->displayString->draw( drawX, startY, (m_namedTimerUsedFlashColor)?info->color:m_namedTimerFlashColor, bgColor );
					}
					else
					{
						info->displayString->draw( drawX, startY, info->color, bgColor );
					}
				}
				else
				{
					info->displayString->draw( drawX, startY, info->color, bgColor );
				}

				// increment text spot to next location
				startY -= info->displayString->getFont()->height;
			}
		}
	}
	
	// draw the scroll anchor, which sits on the middle button now
	if (TheLookAtTranslator && m_drawRMBScrollAnchor)
	{
		const ICoord2D* anchor = TheLookAtTranslator->getScrollAnchor();
		if (anchor)
		{
			static const Int w = 2;
			static const Int h = 2;
			static const Int r = 4; // ratio
			static const Color mainColor = GameMakeColor(0, 255, 0, 255);
			static const Color dropColor = GameMakeColor(0, 0, 0, 255);
			TheDisplay->drawFillRect( anchor->x-w*r-1, anchor->y-h-1, w*2*r+3, h*2+3, dropColor );
			TheDisplay->drawFillRect( anchor->x-w-1, anchor->y-h*r-1, w*2+3, h*2*r+3, dropColor );
			TheDisplay->drawFillRect( anchor->x-w*r, anchor->y-h, w*2*r+1, h*2+1, mainColor );
			TheDisplay->drawFillRect( anchor->x-w, anchor->y-h*r, w*2+1, h*2*r+1, mainColor );
		}
	}

	//draw superweapon ready multipliers
	TheControlBar->drawSpecialPowerShortcutMultiplierText();

}  // end postDraw

//-------------------------------------------------------------------------------------------------
/** Create the control user interface GUI */
//-------------------------------------------------------------------------------------------------
void InGameUI::createControlBar( void )
{

	TheWindowManager->winCreateFromScript( AsciiString("ControlBar.wnd") );
	HideControlBar();
/*	
	// hide all windows created from this layout
	GameWindow *window = TheWindowManager->winGetWindowList();
	for( ; window; window = window->winGetPrev() )
		window->winHide( TRUE );
*/

}  // end createControlBar

//-------------------------------------------------------------------------------------------------
/** Create the replay control GUI */
//-------------------------------------------------------------------------------------------------
void InGameUI::createReplayControl( void )
{

	m_replayWindow = TheWindowManager->winCreateFromScript( AsciiString("ReplayControl.wnd") );

/*	
	// hide all windows created from this layout
	GameWindow *window = TheWindowManager->winGetWindowList();
	for( ; window; window = window->winGetPrev() )
		window->winHide( TRUE );
*/

}  // end createReplayControl

// ------------------------------------------------------------------------------------------------
// InGameUI::playMovie
// ------------------------------------------------------------------------------------------------
void InGameUI::playMovie( const AsciiString& movieName )
{

	stopMovie();

	m_videoStream = TheVideoPlayer->open( movieName );

	if ( m_videoStream == NULL )
	{
		return;
	}

	m_currentlyPlayingMovie = movieName;
	m_videoBuffer = TheDisplay->createVideoBuffer();

	if (	m_videoBuffer == NULL || 
				!m_videoBuffer->allocate(	m_videoStream->width(), 
													m_videoStream->height())
		)
	{
		stopMovie();
		return;
	}
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void InGameUI::stopMovie( void )
{
	delete m_videoBuffer;
	m_videoBuffer = NULL;

	if ( m_videoStream )
	{
		m_videoStream->close();
		m_videoStream = NULL;
	}

	if (!m_currentlyPlayingMovie.isEmpty()) {
		//TheScriptEngine->notifyOfCompletedVideo(m_currentlyPlayingMovie); // removing sync error source -MDC
		m_currentlyPlayingMovie = AsciiString::TheEmptyString;
	}
}

// ------------------------------------------------------------------------------------------------
// InGameUI::videoBuffer
// ------------------------------------------------------------------------------------------------
VideoBuffer* InGameUI::videoBuffer( void )
{
	return m_videoBuffer;
}

// ------------------------------------------------------------------------------------------------
// InGameUI::playMovie
// ------------------------------------------------------------------------------------------------
void InGameUI::playCameoMovie( const AsciiString& movieName )
{

	stopCameoMovie();

	m_cameoVideoStream = TheVideoPlayer->open( movieName );

	if ( m_cameoVideoStream == NULL )
	{
		return;
	}

	m_cameoVideoBuffer = TheDisplay->createVideoBuffer();

	if (	m_cameoVideoBuffer == NULL || 
				!m_cameoVideoBuffer->allocate(	m_cameoVideoStream->width(), 
													m_cameoVideoStream->height())
		)
	{
		stopCameoMovie();
		return;
	}
	GameWindow *window = TheWindowManager->winGetWindowFromId(NULL,TheNameKeyGenerator->nameToKey( AsciiString("ControlBar.wnd:RightHUD") ));
	WinInstanceData *winData = window->winGetInstanceData();
	winData->setVideoBuffer(m_cameoVideoBuffer);
//	window->winHide(FALSE);
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void InGameUI::stopCameoMovie( void )
{
//RightHUD
	//GameWindow *window = TheWindowManager->winGetWindowFromId(NULL,TheNameKeyGenerator->nameToKey( AsciiString("ControlBar.wnd:CameoMovieWindow") ));
	GameWindow *window = TheWindowManager->winGetWindowFromId(NULL,TheNameKeyGenerator->nameToKey( AsciiString("ControlBar.wnd:RightHUD") ));
//	window->winHide(FALSE);
	WinInstanceData *winData = window->winGetInstanceData();
	winData->setVideoBuffer(NULL);
	
	delete m_cameoVideoBuffer;
	m_cameoVideoBuffer = NULL;

	if ( m_cameoVideoStream )
	{
		m_cameoVideoStream->close();
		m_cameoVideoStream = NULL;
	}
	
}

// ------------------------------------------------------------------------------------------------
// InGameUI::videoBuffer
// ------------------------------------------------------------------------------------------------
VideoBuffer* InGameUI::cameoVideoBuffer( void )
{
	return m_cameoVideoBuffer;
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
void InGameUI::displayCantBuildMessage( LegalBuildCode lbc )
{

	switch( lbc )
	{

		//---------------------------------------------------------------------------------------------
		case LBC_RESTRICTED_TERRAIN:
			TheInGameUI->message( "GUI:CantBuildRestrictedTerrain" );
			break;

		//---------------------------------------------------------------------------------------------
		case LBC_NOT_FLAT_ENOUGH:
			TheInGameUI->message( "GUI:CantBuildNotFlatEnough" );
			break;

		//---------------------------------------------------------------------------------------------
		case LBC_OBJECTS_IN_THE_WAY:
			TheInGameUI->message( "GUI:CantBuildObjectsInTheWay" );
			break;

		//---------------------------------------------------------------------------------------------
		case LBC_TOO_CLOSE_TO_SUPPLIES:
			TheInGameUI->message( "GUI:CantBuildTooCloseToSupplies" );
			break;

		//---------------------------------------------------------------------------------------------
		case LBC_TOO_MANY_DERRICK_DEFENSES:
			TheInGameUI->message( "GUI:CantBuildTooManyDerrickDefenses" );
			break;

		//---------------------------------------------------------------------------------------------
		case LBC_TOO_CLOSE_TO_ENEMY:
			TheInGameUI->message( "GUI:CantBuildTooCloseToEnemy" );
			break;

		//---------------------------------------------------------------------------------------------
		case LBC_NO_CLEAR_PATH:
		  TheInGameUI->message( "GUI:CantBuildNoClearPath" );
			break;

		//---------------------------------------------------------------------------------------------
		case LBC_SHROUD:
			TheInGameUI->message( "GUI:CantBuildShroud" );
			break;

		//---------------------------------------------------------------------------------------------
		case LBC_GENERIC_FAILURE:
		default:

			TheInGameUI->message( "GUI:CantBuildThere" );
			break;

	}  // end switch

}  // end displayCantBuildMessage

// ------------------------------------------------------------------------------------------------
// InGameUI::militarySubtitle
// ------------------------------------------------------------------------------------------------
void InGameUI::militarySubtitle( const AsciiString& label, Int duration )
{
	// make sure we don't already have a subtitle up there
	removeMilitarySubtitle();

	// update our history
	UpdateDiplomacyBriefingText(label, FALSE);

	UnicodeString title = TheGameText->fetch(label);

	// make sure we actually will be displaying something
	if( title.isEmpty() || duration <= 0)
	{
		DEBUG_CRASH(("Trying to create a military subtitle but either title is empty (%ls) or duration is <= 0 (%d)",title.str(), duration));
		return;
	}

	// we need some frame info to set our timings
	UnsignedInt currLogicFrame = TheGameLogic->getFrame();
	const int messageTimeout = currLogicFrame + (Int)(((Real)LOGICFRAMES_PER_SECOND * duration)/1000.0f);

	// disable tooltips until this frame, cause we don't want to collide with the military subtitles.
	TheInGameUI->disableTooltipsUntil(messageTimeout);
	
	// calculate where this screen position should be since the position being passed in is based off 8x6
	Coord2D multiplier;
	multiplier.x = (float)TheDisplay->getWidth() / 800.0f;
	multiplier.y = (float)TheDisplay->getHeight() / 600.0f;
	
	// lets bring out the data structure!
	m_militarySubtitle = NEW MilitarySubtitleData;

	m_militarySubtitle->subtitle.set(title);
	m_militarySubtitle->blockDrawn = TRUE;
	m_militarySubtitle->blockBeginFrame = currLogicFrame;
	m_militarySubtitle->lifetime = messageTimeout;
	m_militarySubtitle->blockPos.x =  m_militarySubtitle->position.x = m_militaryCaptionPosition.x * multiplier.x;
	m_militarySubtitle->blockPos.y =  m_militarySubtitle->position.y = m_militaryCaptionPosition.y * multiplier.y;
	m_militarySubtitle->incrementOnFrame = currLogicFrame + (Int)(((Real)LOGICFRAMES_PER_SECOND * TheGlobalLanguageData->m_militaryCaptionDelayMS)/1000.0f);
	m_militarySubtitle->index = 0;
	for (int i = 1; i < MAX_SUBTITLE_LINES; i ++)
		m_militarySubtitle->displayStrings[i] = NULL;

	m_militarySubtitle->currentDisplayString = 0;
	m_militarySubtitle->displayStrings[0] = TheDisplayStringManager->newDisplayString();
	m_militarySubtitle->displayStrings[0]->reset();
	m_militarySubtitle->displayStrings[0]->setFont(	TheFontLibrary->getFont( m_militaryCaptionTitleFont, 
		TheGlobalLanguageData->adjustFontSize(m_militaryCaptionTitlePointSize), m_militaryCaptionTitleBold ) );
	m_militarySubtitle->color = GameMakeColor(m_militaryCaptionColor.red, m_militaryCaptionColor.green, m_militaryCaptionColor.blue, m_militaryCaptionColor.alpha);
}

// ------------------------------------------------------------------------------------------------
// InGameUI::removeMilitarySubtitle
// ------------------------------------------------------------------------------------------------
void InGameUI::removeMilitarySubtitle( void )
{
	// sanity (is there really such a thing in this world?)
	if(!m_militarySubtitle)
		return;

	TheInGameUI->clearTooltipsDisabled();

	// loop through and free up the display strings
	for(Int i = 0; i <= m_militarySubtitle->currentDisplayString; i ++)
	{
		TheDisplayStringManager->freeDisplayString(m_militarySubtitle->displayStrings[i]);
		m_militarySubtitle->displayStrings[i] = NULL;
	}

	//delete it man!
	delete m_militarySubtitle;
	m_militarySubtitle= NULL;

}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
Bool InGameUI::areSelectedObjectsControllable() const
{
	const DrawableList *selected = TheInGameUI->getAllSelectedDrawables();

	// loop through all the selected drawables
	const Drawable *draw;
	for( DrawableListCIt it = selected->begin(); it != selected->end(); ++it )
	{
		// get this drawable
		draw = *it;

		// All selected objects will have the same local controller, so 
		// simply return the first one.
		return draw->getObject()->isLocallyControlled();
	}

	// Nothing selected...
	return FALSE;
}

//------------------------------------------------------------------------------
//Resets the camera to default zoom and orientation.
//------------------------------------------------------------------------------
void InGameUI::resetCamera()
{
	ViewLocation currentView;
	TheTacticalView->getLocation( &currentView ); 
	TheTacticalView->resetCamera( &currentView.getPosition(), 1, 0.0f, 0.0f );
}

//------------------------------------------------------------------------------
//Checks to see if an object can interact with an object in a non-hostile manner. This is currently used by the selection 
//translator to determine whether to do something to an object or select it instead based on the context of what is currently
//selected.
//------------------------------------------------------------------------------
Bool InGameUI::canSelectedObjectsNonAttackInteractWithObject( const Object *objectToInteractWith, SelectionRules rule ) const
{
	for( int i = 1; i < NUM_ACTIONTYPES; i++ )
	{
		if( i != ACTIONTYPE_ATTACK_OBJECT )
		{
			if( canSelectedObjectsDoAction( (ActionType)i, objectToInteractWith, rule ) )
			{
				return TRUE;
			}
		}
	}
	return FALSE;
}

CanAttackResult InGameUI::getCanSelectedObjectsAttack( ActionType action, const Object *objectToInteractWith, SelectionRules rule, Bool additionalChecking ) const
{
	//Kris: Aug 16, 2003
	//John McDonald added this code back in Oct 09, 2002. 
	//Replaced it with palatable code.
	//if( (objectToInteractWith == NULL) != (action == ACTIONTYPE_SET_RALLY_POINT)) <---BAD CODE
	if( !objectToInteractWith && action != ACTIONTYPE_SET_RALLY_POINT || //No object to interact with (and not rally point mode)
			 objectToInteractWith && action == ACTIONTYPE_SET_RALLY_POINT )  //Object to interact with (and rally point mode)
	{
		//Sanity check OR can't set a rally point over an object.
		return ATTACKRESULT_NOT_POSSIBLE;
	}

	// get selected list of drawables
	const DrawableList *selected = TheInGameUI->getAllSelectedDrawables();

	// set up counters for rule checking
	Int count = 0;
	CanAttackResult bestResult = ATTACKRESULT_NOT_POSSIBLE;
	CanAttackResult worstResult = ATTACKRESULT_POSSIBLE;

	// loop through all the selected drawables
	Drawable *other;
	for( DrawableListCIt it = selected->begin(); it != selected->end(); ++it )
	{
	
		// get this drawable
		other = *it;
		count++;

		switch( action )
		{
			case ACTIONTYPE_ATTACK_OBJECT:
			{
				//additionalChecking is TRUE only if force attack mode is on.
				CanAttackResult result = 	TheActionManager->getCanAttackObject( other->getObject(), objectToInteractWith, CMD_FROM_PLAYER, 
									additionalChecking ? ATTACK_NEW_TARGET_FORCED : ATTACK_NEW_TARGET );

				if( result > bestResult )
				{
					//Best result is used for the rule: SELECTION_ANY
					bestResult = result;
				}
				if( result < worstResult )
				{
					//Worst result is used for the rule: SELECTION_ALL
					worstResult = result;
				}
				break;
			}

			case ACTIONTYPE_NONE:
			case ACTIONTYPE_GET_REPAIRED_AT:
			case ACTIONTYPE_DOCK_AT:
			case ACTIONTYPE_GET_HEALED_AT:
			case ACTIONTYPE_REPAIR_OBJECT:
			case ACTIONTYPE_RESUME_CONSTRUCTION:
			case ACTIONTYPE_COMBATDROP_INTO:
			case ACTIONTYPE_ENTER_OBJECT:
			case ACTIONTYPE_HIJACK_VEHICLE:
			case ACTIONTYPE_SABOTAGE_BUILDING:
			case ACTIONTYPE_CONVERT_OBJECT_TO_CARBOMB:
			case ACTIONTYPE_CAPTURE_BUILDING:
			case ACTIONTYPE_DISABLE_VEHICLE_VIA_HACKING:
#ifdef ALLOW_SURRENDER
			case ACTIONTYPE_PICK_UP_PRISONER:
#endif
			case ACTIONTYPE_STEAL_CASH_VIA_HACKING:
			case ACTIONTYPE_DISABLE_BUILDING_VIA_HACKING:
			case ACTIONTYPE_MAKE_DEFECTOR:
			case ACTIONTYPE_SET_RALLY_POINT:
			default:
				DEBUG_CRASH( ("Called InGameUI::getCanSelectedObjectsAttack() with actiontype %d. Only accepts attack types! Should you be calling InGameUI::canSelectedObjectsDoAction() instead?") );
				return ATTACKRESULT_INVALID_SHOT;

		}

	}  // end for

	if( count > 0 )
	{
		if( rule == SELECTION_ANY )
		{
			return bestResult;
		}
		return worstResult;
	}

	// no can do!
	return ATTACKRESULT_NOT_POSSIBLE;
}

//------------------------------------------------------------------------------
//Wrapper function that checks a specific action.
//------------------------------------------------------------------------------
Bool InGameUI::canSelectedObjectsDoAction( ActionType action, const Object *objectToInteractWith, SelectionRules rule, Bool additionalChecking ) const
{

	//Kris: Aug 16, 2003
	//John McDonald added this code back in Oct 09, 2002. This code is SO wrong that it should
	//be a firing offense. Strangely enough, this code has gone unnoticed for nearly a year
	//and nearly two projects. I'm fixing this now by moving it to the rally point code...
	//because it would be nice if a saboteur could actually sabotage a building via a 
	//commandbutton.
	//if( (objectToInteractWith == NULL) != (action == ACTIONTYPE_SET_RALLY_POINT))
	if( !objectToInteractWith && action != ACTIONTYPE_SET_RALLY_POINT || //No object to interact with (and not rally point mode)
			 objectToInteractWith && action == ACTIONTYPE_SET_RALLY_POINT )  //Object to interact with (and rally point mode)
	{
		//Sanity check OR can't set a rally point over an object.
		return FALSE;
	}

	// get selected list of drawables
	const DrawableList *selected = TheInGameUI->getAllSelectedDrawables();

	// set up counters for rule checking
	Int count = 0;
	Int qualify = 0;

	// loop through all the selected drawables
	Drawable *other;
	for( DrawableListCIt it = selected->begin(); it != selected->end(); ++it )
	{
	
		// get this drawable
		other = *it;
		count++;
		Bool success = FALSE;

		switch( action )
		{
			case ACTIONTYPE_NONE:
				//However strange this might be, it is always possible to do "nothing"
				//although I can't think of why this would be needed...
				return TRUE;
			case ACTIONTYPE_GET_REPAIRED_AT:
				success = TheActionManager->canGetRepairedAt( other->getObject(), objectToInteractWith, CMD_FROM_PLAYER );
				break;
			case ACTIONTYPE_DOCK_AT:
				success = TheActionManager->canDockAt( other->getObject(), objectToInteractWith, CMD_FROM_PLAYER );
				break;
			case ACTIONTYPE_GET_HEALED_AT:
				success = TheActionManager->canGetHealedAt( other->getObject(), objectToInteractWith, CMD_FROM_PLAYER );
				if( success )
				{
					ContainModuleInterface *contain = objectToInteractWith->getContain();
					if( contain && contain->isHealContain() )
					{
						//This container is only used for the purposes of healing and we cannot 
						//enter it normally -- this is NOT a transport!
						success = false;
					}
				}
				break;
			case ACTIONTYPE_REPAIR_OBJECT:
			{
				ObjectID currentRepairer = objectToInteractWith->getSoleHealingBenefactor(); 
				success = ( TheActionManager->canRepairObject( other->getObject(), objectToInteractWith, CMD_FROM_PLAYER ) 
										&& ( currentRepairer == INVALID_ID || currentRepairer == other->getObject()->getID() ) );
											// unless someone else is already healing it...
											// please note that this add'l test is left out of canRepairObject() since canRepairObject 
											// gets called from within the Dozer/WorkerAIUpdates' stateMachines as they continue the repair process.
											// This remains true.
				break;
			}
			case ACTIONTYPE_RESUME_CONSTRUCTION:
				success = TheActionManager->canResumeConstructionOf( other->getObject(), objectToInteractWith, CMD_FROM_PLAYER );
				break;
			case ACTIONTYPE_COMBATDROP_INTO:
				success = TheActionManager->canEnterObject( other->getObject(), objectToInteractWith, CMD_FROM_PLAYER, COMBATDROP_INTO );
				break;
			case ACTIONTYPE_ENTER_OBJECT:
				//additionalChecking is TRUE only if we want to check if transport is full first.
				success = TheActionManager->canEnterObject( other->getObject(), objectToInteractWith, CMD_FROM_PLAYER, additionalChecking ? CHECK_CAPACITY : DONT_CHECK_CAPACITY );
				break;
			case ACTIONTYPE_ATTACK_OBJECT:
				DEBUG_CRASH( ("Called InGameUI::canSelectedObjectsDoAction() with ACTIONTYPE_ATTACK_OBJECT. You must use InGameUI::getCanSelectedObjectsAttack() instead.") );
				return FALSE;
			case ACTIONTYPE_HIJACK_VEHICLE:
				success = TheActionManager->canHijackVehicle( other->getObject(), objectToInteractWith, CMD_FROM_PLAYER );
				break;
			case ACTIONTYPE_SABOTAGE_BUILDING:
				success = TheActionManager->canSabotageBuilding( other->getObject(), objectToInteractWith, CMD_FROM_PLAYER );
				break;
			case ACTIONTYPE_CONVERT_OBJECT_TO_CARBOMB:
				success = TheActionManager->canConvertObjectToCarBomb( other->getObject(), objectToInteractWith, CMD_FROM_PLAYER );
				break;
			case ACTIONTYPE_CAPTURE_BUILDING:
				success = TheActionManager->canCaptureBuilding( other->getObject(), objectToInteractWith, CMD_FROM_PLAYER );
				break;
			case ACTIONTYPE_DISABLE_VEHICLE_VIA_HACKING:
				success = TheActionManager->canDisableVehicleViaHacking( other->getObject(), objectToInteractWith, CMD_FROM_PLAYER );
				break;
#ifdef ALLOW_SURRENDER
			case ACTIONTYPE_PICK_UP_PRISONER:
				success = TheActionManager->canPickUpPrisoner( other->getObject(), objectToInteractWith, CMD_FROM_PLAYER );
				break;
#endif
			case ACTIONTYPE_STEAL_CASH_VIA_HACKING:
				success = TheActionManager->canStealCashViaHacking( other->getObject(), objectToInteractWith, CMD_FROM_PLAYER );
				break;
			case ACTIONTYPE_DISABLE_BUILDING_VIA_HACKING:
				success = TheActionManager->canDisableBuildingViaHacking( other->getObject(), objectToInteractWith, CMD_FROM_PLAYER );
				break;
			case ACTIONTYPE_MAKE_DEFECTOR:
				success = TheActionManager->canMakeObjectDefector( other->getObject(), objectToInteractWith, CMD_FROM_PLAYER );
				break;
			case ACTIONTYPE_SET_RALLY_POINT:
			{
				Object *obj = other->getObject();
				if (!obj) {
					success = false;
					break;
				}
				success = (obj->isKindOf(KINDOF_AUTO_RALLYPOINT) && obj->isLocallyControlled());
				break;
			}
		}

		if( success )
		{
			if( rule == SELECTION_ANY )
			{
				return TRUE;
			}

			++qualify;
		}
	}  // end for

	//If the rule is all must qualify, do the check now and return success
	//only if all the selected units qualified.
	if( rule == SELECTION_ALL && count > 0 && qualify == count )
	{
		return TRUE;
	}

	// no can do!
	return FALSE;
}

//------------------------------------------------------------------------------
Bool InGameUI::canSelectedObjectsDoSpecialPower( const CommandButton *command, const Object *objectToInteractWith, const Coord3D *position, SelectionRules rule, UnsignedInt commandOptions, Object* ignoreSelObj ) const
{
	//Get the special power template.
	const SpecialPowerTemplate *spTemplate = command->getSpecialPowerTemplate();

	//Order of precendence:
	//1) NO TARGET OR POS
	//2) COMMAND_OPTION_NEED_OBJECT_TARGET
	//3) NEED_TARGET_POS
	Bool doAtPosition = BitTest( command->getOptions(), NEED_TARGET_POS );
	Bool doAtObject = BitTest( command->getOptions(), COMMAND_OPTION_NEED_OBJECT_TARGET );

	//Sanity checks
	if( doAtObject && !objectToInteractWith )
	{
		return false;
	}
	if( doAtPosition && !position )
	{
		return false;		
	}

	// get selected list of drawables
	Drawable* ignoreSelDraw = ignoreSelObj ? ignoreSelObj->getDrawable() : NULL;

	DrawableList tmpList;
	if (ignoreSelDraw)
		tmpList.push_back(ignoreSelDraw);

	const DrawableList* selected = (tmpList.size() > 0) ? &tmpList : TheInGameUI->getAllSelectedDrawables();

	// set up counters for rule checking
	Int count = 0;
	Int qualify = 0;

	// loop through all the selected drawables
	for( DrawableListCIt it = selected->begin(); it != selected->end(); ++it )
	{
	
		// get this drawable
		Drawable* other = *it;
		count++;

		if( !doAtObject && !doAtPosition )
		{
			if( TheActionManager->canDoSpecialPower( other->getObject(), spTemplate, CMD_FROM_PLAYER, commandOptions ) )
			{
				//This is the no target version
				if( rule == SELECTION_ANY )
				{
					return true;
				}
				qualify++;
			}
		}
		else if( doAtObject )
		{
			if( TheActionManager->canDoSpecialPowerAtObject( other->getObject(), objectToInteractWith, CMD_FROM_PLAYER, spTemplate, commandOptions ) )
			{
				//This requires a object target
				if( rule == SELECTION_ANY )
				{
					return true;
				}
				qualify++;
			}
		}
		else if( doAtPosition )
		{
			if( TheActionManager->canDoSpecialPowerAtLocation( other->getObject(), position, CMD_FROM_PLAYER, spTemplate, objectToInteractWith, commandOptions ) )
			{
				//This requires a valid location.
				if( rule == SELECTION_ANY )
				{
					return true;
				}
				qualify++;
			}
		}
	}
	if( rule == SELECTION_ALL && count > 0 && qualify == count )
	{
		return true;
	}
	return false;
}

//------------------------------------------------------------------------------
Bool InGameUI::canSelectedObjectsOverrideSpecialPowerDestination( const Coord3D *loc, SelectionRules rule, SpecialPowerType spType ) const
{
	// set up counters for rule checking
	Int count = 0;
	Int qualify = 0;

	// get selected list of drawables
	const DrawableList *selected = TheInGameUI->getAllSelectedDrawables();

	// loop through all the selected drawables
	Drawable *other;
	for( DrawableListCIt it = selected->begin(); it != selected->end(); ++it )
	{
	
		// get this drawable
		other = *it;
		count++;

		if( TheActionManager->canOverrideSpecialPowerDestination( other->getObject(), loc, spType, CMD_FROM_PLAYER ) )
		{
			if( rule == SELECTION_ANY )
			{
				return true;
			}
			qualify++;
		}
	}
	if( rule == SELECTION_ALL && count > 0 && qualify == count )
	{
		return true;
	}
	return false;
}


//------------------------------------------------------------------------------
Bool InGameUI::canSelectedObjectsEffectivelyUseWeapon( const CommandButton *command, const Object *objectToInteractWith, const Coord3D *position, SelectionRules rule ) const
{
	//Get the special power template.
	WeaponSlotType slot = command->getWeaponSlot();

	//Order of precendence:
	//1) NO TARGET OR POS
	//2) COMMAND_OPTION_NEED_OBJECT_TARGET
	//3) NEED_TARGET_POS
	Bool doAtPosition = BitTest( command->getOptions(), NEED_TARGET_POS );
	Bool doAtObject = BitTest( command->getOptions(), COMMAND_OPTION_NEED_OBJECT_TARGET );

	//Sanity checks
	if( doAtObject && !objectToInteractWith )
	{
		return false;
	}
	if( doAtPosition && !position )
	{
		return false;		
	}

	// get selected list of drawables
	const DrawableList *selected = TheInGameUI->getAllSelectedDrawables();

	// set up counters for rule checking
	Int count = 0;
	Int qualify = 0;

	// loop through all the selected drawables
	Drawable *other;
	for( DrawableListCIt it = selected->begin(); it != selected->end(); ++it )
	{
	
		// get this drawable
		other = *it;
		count++;

		if( !doAtObject && !doAtPosition )
		{
			if( TheActionManager->canFireWeapon( other->getObject(), slot, CMD_FROM_PLAYER ) )
			{
				//This is the no target version
				if( rule == SELECTION_ANY )
				{
					return true;
				}
				qualify++;
			}
		}
		else if( doAtObject )
		{
			if( TheActionManager->canFireWeaponAtObject( other->getObject(), objectToInteractWith, CMD_FROM_PLAYER, slot ) )
			{
				//This requires a object target
				if( rule == SELECTION_ANY )
				{
					return true;
				}
				qualify++;
			}
		}
		else if( doAtPosition )
		{
			if( TheActionManager->canFireWeaponAtLocation( other->getObject(), position, CMD_FROM_PLAYER, slot, objectToInteractWith ) )
			{
				//This requires a valid location.
				if( rule == SELECTION_ANY )
				{
					return true;
				}
				qualify++;
			}
		}
	}
	if( rule == SELECTION_ALL && count > 0 && qualify == count )
	{
		return true;
	}
	return false;
}

// ------------------------------------------------------------------------------------------------
Int InGameUI::selectAllUnitsByTypeAcrossRegion( IRegion2D *region, KindOfMaskType mustBeSet, KindOfMaskType mustBeClear )
{
	KindOfSelectionData data;
	Int newSelectionCount = 0;
	Int oldSelectionCount = getAllSelectedDrawables()->size();

	data.m_mustbeSet = mustBeSet;
	data.m_mustbeClear = mustBeClear;

	if (region)
	{
		TheTacticalView->iterateDrawablesInRegion(region, kindOfUnitSelection, (void *)&data);
		newSelectionCount += data.newlySelectedDrawables.size();
	}
	else
	{
		// loop over the map
		Drawable *temp = TheGameClient->firstDrawable();
		while( temp )
		{
			if( kindOfUnitSelection( temp, (void *)&data) )
			{
				newSelectionCount ++;
			}

			temp = temp->getNextDrawable();
		}
	}
	setDisplayedMaxWarning( FALSE );

	if (newSelectionCount > 0)
	{
		// create selected message
		GameMessage *teamMsg = TheMessageStream->appendMessage( GameMessage::MSG_CREATE_SELECTED_GROUP );

		teamMsg->appendBooleanArgument( (oldSelectionCount == 0) ? TRUE : FALSE );

		const Drawable *draw;

		//Loop through each drawable add append it's objectID to the event.
		for( DrawableListCIt it = data.newlySelectedDrawables.begin(); it != data.newlySelectedDrawables.end(); ++it )
		{
			draw = *it;
			if( draw && draw->getObject() )
			{
				teamMsg->appendObjectIDArgument( draw->getObject()->getID() );
			}
		}
	}

	return newSelectionCount;
}

// ------------------------------------------------------------------------------------------------
/** Selects maching units on the screen */
// ------------------------------------------------------------------------------------------------
Int InGameUI::selectMatchingAcrossRegion( IRegion2D *region )
{
	const DrawableList *selected = getAllSelectedDrawables();

	/* loop through all the selected drawables and create a set of all the objects,
	   so that you only iterate once through each type of object
	*/

	const Drawable *draw;

	//std::set<AsciiString> drawableList;
	std::set<const ThingTemplate*> drawableList;
	Bool carBomb = FALSE;
	
	for( DrawableListCIt it = selected->begin(); it != selected->end(); ++it )
	{
		// get this drawable
		draw = *it;
		if( draw && draw->getObject() && draw->getObject()->isLocallyControlled() )
		{
			// Use the Object's thing template, doing so will prevent wierdness for disguised vehicles.
			drawableList.insert( draw->getObject()->getTemplate() );
			if( draw->getObject()->testStatus( OBJECT_STATUS_IS_CARBOMB ) )
			{
				carBomb = TRUE;
			}
		}
	}

	if (drawableList.size() == 0)
		return -1; // nothing useful selected to begin with - don't bother iterating

	std::set<const ThingTemplate*>::iterator iter;
	const ThingTemplate *templateName;

	// now use the list to select across screen
	MatchingUnitSelectionData data;
	Int newSelectionCount = 0;

	for( iter = drawableList.begin(); iter != drawableList.end(); ++iter )
	{
		// get this drawable
		templateName = *iter;

		data.templateToSelect = templateName;
		data.isCarBomb        = carBomb;
		if (region)
			newSelectionCount +=TheTacticalView->iterateDrawablesInRegion(region, similarUnitSelection, (void *)&data);
		else
		{
			// loop over the map
			Drawable *temp = TheGameClient->firstDrawable();
			while( temp )
			{
				newSelectionCount += similarUnitSelection( temp, (void *)&data);
				temp = temp->getNextDrawable();
			}
		}
		setDisplayedMaxWarning( FALSE );
	}

	if (newSelectionCount > 0)
	{
		// create selected message
		GameMessage *teamMsg = TheMessageStream->appendMessage( GameMessage::MSG_CREATE_SELECTED_GROUP_NO_SOUND );
		// not creating a new team so pass in false
		teamMsg->appendBooleanArgument( FALSE );

		//Loop through each drawable add append it's objectID to the event.
		for( DrawableListCIt it = data.newlySelectedDrawables.begin(); it != data.newlySelectedDrawables.end(); ++it )
		{
			draw = *it;
			if( draw && draw->getObject() )
			{
				teamMsg->appendObjectIDArgument( draw->getObject()->getID() );
			}
		}
	}

	return newSelectionCount;

}

// ------------------------------------------------------------------------------------------------
Int InGameUI::selectAllUnitsByTypeAcrossScreen(KindOfMaskType mustBeSet, KindOfMaskType mustBeClear)
{
	/// When implementing this, obey TheInGameUI->getMaxSelectCount() if it is > 0
			
	IRegion2D region;
	ICoord2D origin;
	ICoord2D size;
 
	TheTacticalView->getOrigin( &origin.x, &origin.y );
	size.x = TheTacticalView->getWidth();
	size.y = TheTacticalView->getHeight();
 
	buildRegion( &origin, &size, &region );

	Int numSelected = selectAllUnitsByTypeAcrossRegion(&region, mustBeSet, mustBeClear);
	if (numSelected == -1)
	{
		UnicodeString message = TheGameText->fetch( "GUI:NothingSelected" );
		TheInGameUI->message( message );
	}
	else if (numSelected == 0)
	{
	}
	else
	{
		UnicodeString message = TheGameText->fetch( "GUI:SelectedAcrossScreen" );
		TheInGameUI->message( message );
	}
	return numSelected;
}

// ------------------------------------------------------------------------------------------------
/** Selects maching units on the screen */
// ------------------------------------------------------------------------------------------------
Int InGameUI::selectMatchingAcrossScreen( void )
{
	/// When implementing this, obey TheInGameUI->getMaxSelectCount() if it is > 0
			
	IRegion2D region;
	ICoord2D origin;
	ICoord2D size;
 
	TheTacticalView->getOrigin( &origin.x, &origin.y );
	size.x = TheTacticalView->getWidth();
	size.y = TheTacticalView->getHeight();
 
	buildRegion( &origin, &size, &region );

	Int numSelected = selectMatchingAcrossRegion(&region);
	if (numSelected == -1)
	{
		UnicodeString message = TheGameText->fetch( "GUI:NothingSelected" );
		TheInGameUI->message( message );
	}
	else if (numSelected == 0)
	{
	}
	else
	{
		UnicodeString message = TheGameText->fetch( "GUI:SelectedAcrossScreen" );
		TheInGameUI->message( message );
	}
	return numSelected;
}

//-------------------------------------------------------------------------------------------------
Int InGameUI::selectAllUnitsByTypeAcrossMap(KindOfMaskType mustBeSet, KindOfMaskType mustBeClear)
{
	/// When implementing this, obey TheInGameUI->getMaxSelectCount() if it is > 0
	Int numSelected = selectAllUnitsByTypeAcrossRegion(NULL, mustBeSet, mustBeClear);
	if (numSelected == -1)
	{
		UnicodeString message = TheGameText->fetch( "GUI:NothingSelected" );
		TheInGameUI->message( message );
	}
	else if (numSelected == 0)
	{
		Drawable *draw = TheInGameUI->getFirstSelectedDrawable();
		if( !draw || !draw->getObject() || !draw->getObject()->isKindOf( KINDOF_STRUCTURE ) )
		{
			UnicodeString message = TheGameText->fetch( "GUI:SelectedAcrossMap" );
			TheInGameUI->message( message );
		}
	}
	else
	{
		UnicodeString message = TheGameText->fetch( "GUI:SelectedAcrossMap" );
		TheInGameUI->message( message );
	}
	return numSelected;
}

//-------------------------------------------------------------------------------------------------
/** Selects matching units across map */
//-------------------------------------------------------------------------------------------------
Int InGameUI::selectMatchingAcrossMap()
{
	/// When implementing this, obey TheInGameUI->getMaxSelectCount() if it is > 0
	Int numSelected = selectMatchingAcrossRegion(NULL);
	if (numSelected == -1)
	{
		UnicodeString message = TheGameText->fetch( "GUI:NothingSelected" );
		TheInGameUI->message( message );
	}
	else if (numSelected == 0)
	{
		Drawable *draw = TheInGameUI->getFirstSelectedDrawable();
		if( !draw || !draw->getObject() || !draw->getObject()->isKindOf( KINDOF_STRUCTURE ) )
		{
			UnicodeString message = TheGameText->fetch( "GUI:SelectedAcrossMap" );
			TheInGameUI->message( message );
		}
	}
	else
	{
		UnicodeString message = TheGameText->fetch( "GUI:SelectedAcrossMap" );
		TheInGameUI->message( message );
	}
	return numSelected;
}

//-------------------------------------------------------------------------------------------------
Int InGameUI::selectAllUnitsByType(KindOfMaskType mustBeSet, KindOfMaskType mustBeClear)
{
	/// When implementing this, obey TheInGameUI->getMaxSelectCount() if it is > 0
	Int numSelected = selectAllUnitsByTypeAcrossScreen(mustBeSet, mustBeClear);
	if (numSelected == -1)
	{
		return numSelected;
	}

	if (numSelected == 0)
	{
		Int numSelectedAcrossMap = selectAllUnitsByTypeAcrossMap(mustBeSet, mustBeClear);
		return numSelectedAcrossMap;
	}
	return numSelected;
}

//-------------------------------------------------------------------------------------------------
/** Selects matching units, either on screen or across map.  When called by pressing 'T',
    their is not a way to tell if the game is supposed to select across the screen, or
    across the map.  For mouse clicks, i.e. Alt + click or double click, we can directly call
    selectMatchingAcrossScreen or selectMatchingAcrossMap */
//-------------------------------------------------------------------------------------------------
Int InGameUI::selectUnitsMatchingCurrentSelection()
{
	/// When implementing this, obey TheInGameUI->getMaxSelectCount() if it is > 0
	Int numSelected = selectMatchingAcrossScreen();
	if (numSelected == -1)
		return numSelected;
	if (numSelected == 0)
	{
		Int numSelectedAcrossMap = selectMatchingAcrossMap();
		//if (numSelectedAcrossMap < 1)
		//{
			//UnicodeString message = TheGameText->fetch( "GUI:NothingSelected" );
			//TheInGameUI->message( message );
		//}
		return numSelectedAcrossMap;
	}
	return numSelected;

}

//-----------------------------------------------------------------------------
/**
 * Given an "anchor" point and the current mouse position (dest),
 * construct a valid 2D bounding region.
 */
//-----------------------------------------------------------------------------------
void InGameUI::buildRegion( const ICoord2D *anchor, const ICoord2D *dest, IRegion2D *region )
{
	// build rectangular region defined by the drag selection
	if (anchor->x < dest->x)
	{
		region->lo.x = anchor->x;
		region->hi.x = dest->x;
	}
	else
	{
		region->lo.x = dest->x;
		region->hi.x = anchor->x;
	}

	if (anchor->y < dest->y)
	{
		region->lo.y = anchor->y;
		region->hi.y = dest->y;
	}
	else
	{
		region->lo.y = dest->y;
		region->hi.y = anchor->y;
	}
}

//-------------------------------------------------------------------------------------------------
/** Add a new floating text to our list */
//-------------------------------------------------------------------------------------------------
FloatingTextData *InGameUI::addFloatingText(const UnicodeString& text,const Coord3D *pos, Color color)
{
	if( !TheGameLogic->getDrawIconUI() )
		return NULL;

	{
		FloatingTextData *newFTD = newInstance( FloatingTextData );
		newFTD->m_frameCount = 0;
		// the money a supply drop pays is drawn in its owner's colour, and the code that raises it
		// is in GameLogic, where the scheme does not exist
		newFTD->m_color = clientColor( color );
		newFTD->m_pos3D.x = pos->x;
		newFTD->m_pos3D.z = pos->z;
		newFTD->m_pos3D.y = pos->y;
		newFTD->m_text = text;
		newFTD->m_dString->setText(text);
		
			
		if(m_floatingTextTimeOut <= 0)
			newFTD->m_frameTimeOut = TheGameLogic->getFrame() +  DEFAULT_FLOATING_TEXT_TIMEOUT;
		else
			newFTD->m_frameTimeOut = TheGameLogic->getFrame() +  m_floatingTextTimeOut; 
		
		m_floatingTextList.push_front( newFTD ); // add to the list
		return newFTD;
	}
}

/** Each signal's mark on the ground, by SignalKind: Art/Textures/<name>.tga, drawn by
	* Tools/signal_marks.py, white where the sender's colour goes. */
static const char *const SIGNAL_MARK_TEXTURES[ SIGNAL_KIND_COUNT ] =
{
	"ReforgedSignalAttack", "ReforgedSignalDefend", "ReforgedSignalLook"
};

enum
{
	SIGNAL_MARK_SIZE						= 160,	///< across, in world units: 90 read as a coin from the default camera
	SIGNAL_MARK_OPACITY					= 230,	///< out of 255, so the ground under it still shows a little
	SIGNAL_MARK_FADE_OUT_FRAMES	= LOGICFRAMES_PER_SECOND * 2	///< the smoke's last steps, over which the mark fades
};

//-------------------------------------------------------------------------------------------------
/** The mark a signal lays on the ground: crossed swords, a shield or an eye in a ring, in `color`,
	* turned to this viewer's camera as it stands now so it reads upright. */
//-------------------------------------------------------------------------------------------------
void InGameUI::addSignalMark( SignalKind kind, const Coord3D &pos, Color color, ParticleSystemID smoke )
{
	Shadow::ShadowTypeInfo decalInfo;
	decalInfo.allowUpdates = FALSE;
	decalInfo.allowWorldAlign = TRUE;		// wrapped over the terrain it lands on
	// SHADOW_ALPHA_DECAL is the kind setColor and setOpacity reach, and removeShadow takes off
	decalInfo.m_type = SHADOW_ALPHA_DECAL;
	strcpy( decalInfo.m_ShadowName, SIGNAL_MARK_TEXTURES[ kind ] );
	decalInfo.m_sizeX = SIGNAL_MARK_SIZE;
	decalInfo.m_sizeY = SIGNAL_MARK_SIZE;
	decalInfo.m_offsetX = 0.0f;
	decalInfo.m_offsetY = 0.0f;

	SignalMark mark;
	mark.decal = TheProjectedShadowManager->addDecal( &decalInfo );
	mark.decal->setAngle( TheTacticalView->getAngle() );
	mark.decal->setColor( color );
	mark.decal->setOpacity( 0 );
	mark.decal->setPosition( pos.x, pos.y, pos.z );
	mark.smoke = smoke;
	m_signalMarks.push_back( mark );
}

//-------------------------------------------------------------------------------------------------
/** Each mark lasts as long as its smoke and fades out over the smoke's last steps.  Both can't
	* simply count logic frames: ParticleSystemManager::update steps once per pass that sees a new
	* logic frame, so a network game catching up several frames in one pass keeps its smoke longer
	* than the frame count says, and a mark on its own clock went a second early or late.  The smoke
	* has left in it what it still has to emit plus the life of its youngest puff.  Hidden while
	* scripts or -cinema hide the icons, like a radius decal. */
//-------------------------------------------------------------------------------------------------
void InGameUI::updateSignalMarks( void )
{
	const Bool shown = TheGameLogic->getDrawIconUI() && !CinemaDirector_hidesHud();
	for( size_t index = 0; index < m_signalMarks.size(); )
	{
		SignalMark &mark = m_signalMarks[ index ];
		const ParticleSystem *smoke = TheParticleSystemManager->findParticleSystem( mark.smoke );
		if( smoke == NULL )
		{
			mark.decal->release();
			m_signalMarks.erase( m_signalMarks.begin() + index );
			continue;
		}

		const Particle *youngest = smoke->getLastParticle();
		const UnsignedInt left = smoke->getSystemLifetimeLeft() + ( youngest ? youngest->getLifetimeLeft() : 0 );
		const UnsignedInt opacity = min( (UnsignedInt)SIGNAL_MARK_OPACITY, left * SIGNAL_MARK_OPACITY / SIGNAL_MARK_FADE_OUT_FRAMES );
		mark.decal->setOpacity( shown ? (Int)opacity : 0 );
		index++;
	}
}

void InGameUI::clearSignalMarks( void )
{
	for( size_t index = 0; index < m_signalMarks.size(); index++ )
		m_signalMarks[ index ].decal->release();
	m_signalMarks.clear();
}

//-------------------------------------------------------------------------------------------------
/** The same logic message as the Alt+Z/X/C keys, so the same throttle and the same allies see it. */
//-------------------------------------------------------------------------------------------------
Bool InGameUI::placeArmedSignal( const Coord3D &world )
{
	if( !isSignalArmed() )
		return FALSE;

	GameMessage *message = TheMessageStream->appendMessage( GameMessage::MSG_PLACE_SIGNAL );
	message->appendLocationArgument( world );
	message->appendIntegerArgument( m_armedSignal );
	disarmSignal();
	return TRUE;
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
#if defined(_DEBUG) || defined(_INTERNAL)
inline Bool isClose(Real a, Real b) { return fabs(a-b) <= 1.0f; }
inline Bool isClose(const Coord3D& a, const Coord3D& b) 
{
		return	isClose(a.x, b.x) && 
			isClose(a.y, b.y) && 
			isClose(a.z, b.z);
}
void InGameUI::DEBUG_addFloatingText(const AsciiString& text, const Coord3D * pos, Color color)
{
	const Int POINTSIZE = 8;
	const Int LEADING = 0;

	Coord3D posToUse = *pos;

try_again:
	for (FloatingTextListIt it = m_floatingTextList.begin(); it != m_floatingTextList.end(); ++it)
	{
		if (isClose((*it)->m_pos3D, posToUse))
		{
			posToUse.z -= (POINTSIZE + LEADING);
			goto try_again;
		}
	}

	FloatingTextData *newFTD = newInstance( FloatingTextData );
	newFTD->m_color = clientColor( color );
	newFTD->m_pos3D.x = posToUse.x;
	newFTD->m_pos3D.y = posToUse.y;
	newFTD->m_pos3D.z = posToUse.z;
	UnicodeString translate;
	translate.translate(text);
	newFTD->m_text = translate;
	newFTD->m_dString->setText(translate);
	newFTD->m_dString->setFont(TheWindowManager->winFindFont( AsciiString("Arial"), POINTSIZE, FALSE ));
				
	if(m_floatingTextTimeOut <= 0)
		newFTD->m_frameTimeOut = TheGameLogic->getFrame() +  DEFAULT_FLOATING_TEXT_TIMEOUT;
	else
		newFTD->m_frameTimeOut = TheGameLogic->getFrame() +  m_floatingTextTimeOut; 
	
	m_floatingTextList.push_front( newFTD ); // add to the list

	//DEBUG_LOG(("%s\n",text.str()));
}
#endif

//-------------------------------------------------------------------------------------------------
/** modify the position of our floating text */
//-------------------------------------------------------------------------------------------------
void InGameUI::updateFloatingText( void )
{
	FloatingTextData *ftd;		// pointer to our floating point data
	UnsignedInt currLogicFrame = TheGameLogic->getFrame();			// the current logic frame
	UnsignedByte r, g, b, a;	// we'll need to break apart our color so we can modify the alpha
	Int amount;								// The amout we'll change the alpha
	static UnsignedInt lastLogicFrameUpdate = currLogicFrame;		// We need to make sure our current frame is different then our last frame we updated.

	// only update the position if we're incrementing frames
	if(lastLogicFrameUpdate == currLogicFrame)
		return;
	
	lastLogicFrameUpdate = currLogicFrame;

	// Loop through our floating text list
	for(FloatingTextListIt it = m_floatingTextList.begin(); it != m_floatingTextList.end();)
	{
		ftd = *it;
		
		// move it up
		++ftd->m_frameCount;
		
		// fade the text
		if( currLogicFrame > ftd->m_frameTimeOut)
		{
			// modify the color
			GameGetColorComponents(ftd->m_color, &r, &g, &b, &a);		
			amount = REAL_TO_INT( (currLogicFrame - ftd->m_frameTimeOut) * m_floatingTextMoveVanishRate);
			if(a - amount < 0)
				a = 0;
			else
				a -= amount;
			ftd->m_color = GameMakeColor(r, g, b, a);
			// if we have 0 alpha delete it
			if( a <= 0)
			{
				it = m_floatingTextList.erase(it);
				ftd->deleteInstance();
				continue; // don't do the ++it below
			}

		}
		// increase our itterator
		++it;
	
	}

}

//-------------------------------------------------------------------------------------------------
/** Itterates through and draws each floating text */
//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
/** A one-line heads-up overlay: render rate and elapsed game time.
	* Off unless ShowHudOverlay is set in Options.ini.  Retail only ever showed the frame rate, and
	* only behind -displayDebug together with a screenful of engine internals. */
//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
/** How the last ten seconds of peace time animate, as fractions of one second. */
//-------------------------------------------------------------------------------------------------
static const Real PEACE_COUNTDOWN_POP_SCALE = 1.35f;	///< the size the digit lands at when the second turns
static const Real PEACE_COUNTDOWN_POP_TIME = 0.35f;		///< how long it takes to settle back to its own size
static const Real PEACE_COUNTDOWN_FADE_TIME = 0.30f;	///< how long it fades out for at the end of the second

//-------------------------------------------------------------------------------------------------
/** The one red the truce is written in, wherever it is written. */
//-------------------------------------------------------------------------------------------------
static Color peaceTimeColor( Int alpha )
{
	return GameMakeColor( 255, 48, 48, alpha );
}

//-------------------------------------------------------------------------------------------------
/** The lobby's peace time at the top of the screen, in the middle of it: the word PEACE in small
	* letters over the time left, both red and bold on one plate.  The top centre is where a player
	* already looks for the state of the match, and it keeps the corner free for the clock and the
	* superweapon timers.  It is not behind ShowHudOverlay - that switch is for a readout, and this
	* is a rule of the match.
	*
	* The plate goes away for the last ten seconds.  Those are counted out across the middle of the
	* screen instead, and two copies of the same number in two places is one of them asking to be
	* read and neither getting it.  Nothing draws once the truce runs out. */
//-------------------------------------------------------------------------------------------------
void InGameUI::drawPeaceTimer( void )
{
	if( TheGameLogic == NULL || !TheGameLogic->isInGame() || TheGameLogic->isInShellGame() )
		return;

	if( !TheGameLogic->isPeaceTime() )
		return;

	const UnsignedInt left = TheGameLogic->getPeaceTimeEndFrame() - TheGameLogic->getFrame();
	if( left <= PEACE_COUNTDOWN_SECONDS * LOGICFRAMES_PER_SECOND )
	{
		drawPeaceCountdown( left );
		return;
	}

	const UnsignedInt secs = ControlBar_secondsFromFrames( (Real)left );

	UnicodeString text;
	text.format( TheGameText->fetch( "GUI:PeaceTimeHud" ), secs / 60, secs % 60 );

	if( m_peaceTimeDisplayString == NULL )
	{
		m_peaceTimeDisplayString = TheDisplayStringManager->newDisplayString();
		m_peaceTimeDisplayString->setFont( TheFontLibrary->getFont( m_superweaponNormalFont,
										TheGlobalLanguageData->adjustFontSize( PEACE_TIMER_POINT_SIZE ),
										TRUE ) );
	}
	m_peaceTimeDisplayString->setText( text );

	// the label is a second string rather than a line of the first: the text renderer has no
	// concept of a newline and would draw one as a character
	if( m_peaceTimeLabelDisplayString == NULL )
	{
		m_peaceTimeLabelDisplayString = TheDisplayStringManager->newDisplayString();
		m_peaceTimeLabelDisplayString->setFont( TheFontLibrary->getFont( m_superweaponNormalFont,
										TheGlobalLanguageData->adjustFontSize( PEACE_TIMER_LABEL_POINT_SIZE ),
										TRUE ) );
	}
	m_peaceTimeLabelDisplayString->setText( TheGameText->fetch( "GUI:PeaceTimeHudLabel" ) );

	Int textWidth = 0, textHeight = 0;
	m_peaceTimeDisplayString->getSize( &textWidth, &textHeight );

	Int labelWidth = 0, labelHeight = 0;
	m_peaceTimeLabelDisplayString->getSize( &labelWidth, &labelHeight );

	// clear of the top edge rather than jammed against it, and the gap grows with the screen the
	// same way the command bar under it does
	const Int pad = 4;
	const Int plateWidth = (labelWidth > textWidth ? labelWidth : textWidth) + pad*2;
	const Int plateLeft = (TheDisplay->getWidth() - plateWidth) / 2;
	const Int top = stripPixels( PEACE_TIMER_TOP_PAD );

	TheDisplay->drawFillRect( plateLeft, top - 1, plateWidth, labelHeight + textHeight + 2,
														GameMakeColor( 0, 0, 0, 160 ) );

	m_peaceTimeLabelDisplayString->draw( (TheDisplay->getWidth() - labelWidth) / 2, top,
														peaceTimeColor( 255 ), GameMakeColor( 0, 0, 0, 255 ) );

	m_peaceTimeDisplayString->draw( (TheDisplay->getWidth() - textWidth) / 2, top + labelHeight,
														peaceTimeColor( 255 ), GameMakeColor( 0, 0, 0, 255 ) );
}

//-------------------------------------------------------------------------------------------------
/** The last ten seconds of that peace time, one digit at a time, with the plate at the top of the
	* screen taken down for them.  The word PEACE goes over the digit at a quarter of its size, on
	* the same line the plate's own word was on, so what happens at ten seconds is the time being
	* replaced by a number you cannot miss rather than the whole thing moving somewhere else.  The
	* middle of the screen is where the fighting you are about to do is, and a countdown sitting on
	* your own units is in the way of the thing it counts down to.
	*
	* Each second the digit lands at PEACE_COUNTDOWN_POP_SCALE of its size, settles to it over the
	* first third of the second, then fades out through the last third, so the movement is what
	* catches the eye rather than the number changing.  The word holds still and keeps its colour
	* through all of it, and the digit grows downwards from under it, so nothing but the number
	* moves.
	*
	* The animation is driven by the logic frame, not the wall clock, because the number it counts
	* is a logic frame: a paused or slowed game shows a paused countdown instead of one that has
	* run ahead of the truce it belongs to. */
//-------------------------------------------------------------------------------------------------
void InGameUI::drawPeaceCountdown( UnsignedInt framesLeft )
{
	const UnsignedInt secondsLeft = (framesLeft + LOGICFRAMES_PER_SECOND - 1) / LOGICFRAMES_PER_SECOND;
	const UnsignedInt framesLeftOfSecond = framesLeft - (secondsLeft - 1) * LOGICFRAMES_PER_SECOND;
	const Real secondElapsed = 1.0f - INT_TO_REAL( framesLeftOfSecond ) / INT_TO_REAL( LOGICFRAMES_PER_SECOND );

	Real scale = 1.0f;
	if( secondElapsed < PEACE_COUNTDOWN_POP_TIME )
		scale = PEACE_COUNTDOWN_POP_SCALE
					- (PEACE_COUNTDOWN_POP_SCALE - 1.0f) * (secondElapsed / PEACE_COUNTDOWN_POP_TIME);

	Real opacity = 1.0f;
	if( secondElapsed > 1.0f - PEACE_COUNTDOWN_FADE_TIME )
		opacity = (1.0f - secondElapsed) / PEACE_COUNTDOWN_FADE_TIME;

	// rounded to a step so a second's worth of scaling asks the font library for a few sizes, not thirty
	const Int wantedPointSize = REAL_TO_INT_CEIL( PEACE_COUNTDOWN_POINT_SIZE * scale );
	const Int pointSize = ((wantedPointSize + PEACE_COUNTDOWN_SIZE_STEP - 1) / PEACE_COUNTDOWN_SIZE_STEP)
											* PEACE_COUNTDOWN_SIZE_STEP;

	if( m_peaceCountdownDisplayString == NULL )
		m_peaceCountdownDisplayString = TheDisplayStringManager->newDisplayString();

	m_peaceCountdownDisplayString->setFont( TheFontLibrary->getFont( m_superweaponNormalFont,
									TheGlobalLanguageData->adjustFontSize( pointSize ),
									TRUE ) );

	UnicodeString text;
	text.format( L"%d", (Int)secondsLeft );
	m_peaceCountdownDisplayString->setText( text );

	// the same string the plate at the top uses, in its own size: the two are never up together
	if( m_peaceTimeLabelDisplayString == NULL )
		m_peaceTimeLabelDisplayString = TheDisplayStringManager->newDisplayString();

	m_peaceTimeLabelDisplayString->setFont( TheFontLibrary->getFont( m_superweaponNormalFont,
									TheGlobalLanguageData->adjustFontSize( PEACE_COUNTDOWN_POINT_SIZE
																												/ PEACE_COUNTDOWN_LABEL_SHARE ),
									TRUE ) );
	m_peaceTimeLabelDisplayString->setText( TheGameText->fetch( "GUI:PeaceTimeHudLabel" ) );

	Int textWidth = 0, textHeight = 0;
	m_peaceCountdownDisplayString->getSize( &textWidth, &textHeight );

	Int labelWidth = 0, labelHeight = 0;
	m_peaceTimeLabelDisplayString->getSize( &labelWidth, &labelHeight );

	// the same line the plate's word was on, so the word does not move when the plate goes
	const Int top = stripPixels( PEACE_TIMER_TOP_PAD );
	const Int alpha = REAL_TO_INT_CEIL( opacity * 255.0f );

	m_peaceTimeLabelDisplayString->draw( (TheDisplay->getWidth() - labelWidth) / 2, top,
									peaceTimeColor( 255 ), GameMakeColor( 0, 0, 0, 255 ) );

	m_peaceCountdownDisplayString->draw( (TheDisplay->getWidth() - textWidth) / 2, top + labelHeight,
									peaceTimeColor( alpha ), GameMakeColor( 0, 0, 0, alpha ) );
}

//-------------------------------------------------------------------------------------------------
void InGameUI::drawHudOverlay( void )
{
	// the corner is measured fresh every frame, and this plate is the first thing in it
	m_hudOverlayBottom = 0;

	// the command bar page's network box reads these whether the plate is switched on or not; the
	// plate itself stands down while the page is up, the box is where they are written then
	const Bool plate = TheGlobalData->m_showHudOverlay && !m_controlBarPageShown;

	// only once a real game is under way - not in the shell, and not on the menu's background map
	if( TheGameLogic == NULL || !TheGameLogic->isInGame() || TheGameLogic->isInShellGame() )
		return;

	if( TheGameLogic->getFrame() == 0 )
		return;

	//
	// Two rates, both sampled over half a second of wall clock so the numbers are readable, and
	// they are not the same thing. Client frames are the render rate. Logic frames are the
	// simulation, which is *supposed* to run at LOGICFRAMES_PER_SECOND and drops below it exactly
	// when a frame overruns its budget - so a reading under the nominal rate is the stutter,
	// measured, rather than something the render rate can be blamed for.
	//
	// NOT GameClient::getFrame() - that is the simulation frame number from the server, so the
	// "fps" reading was a second copy of the logic rate and moved with it. postDraw runs once per
	// rendered frame, so count the draws here instead.
	++m_hudDrawCount;
	const UnsignedInt clientFrame = m_hudDrawCount;
	const UnsignedInt logicFrame = TheGameLogic->getFrame();
	UnsignedInt nowMs = timeGetTime();
	// a replay wound back runs the frame number backwards, and the unsigned difference read as tens
	// of millions of logic frames a second; the reading starts over from there instead
	if( m_hudLastSampleFrame == 0 || logicFrame < m_hudLastSampleLogicFrame )
	{
		m_hudLastSampleMs = nowMs;
		m_hudLastSampleFrame = clientFrame;
		m_hudLastSampleLogicFrame = logicFrame;
	}
	else if( nowMs - m_hudLastSampleMs >= 500 )
	{
		const Real elapsed = (Real)(nowMs - m_hudLastSampleMs);
		m_hudFps.add( (clientFrame - m_hudLastSampleFrame) * 1000.0f / elapsed );
		m_hudLogicHz.add( (logicFrame - m_hudLastSampleLogicFrame) * 1000.0f / elapsed );
		m_hudLastSampleMs = nowMs;
		m_hudLastSampleFrame = clientFrame;
		m_hudLastSampleLogicFrame = logicFrame;
	}

	//
	// Game time is logic frames; real time is the wall clock. They start together and drift apart
	// by however much the simulation has fallen behind - which is the same stutter the two rates
	// above show, totalled. The base is set the first time the overlay draws rather than at frame
	// zero, so switching it on mid-game does not invent an hour of lag.
	//
	// A pause stops the logic clock and not the wall clock, and the gap between the two readings
	// is meant to be simulation lag - time spent in the menu is not lag. So the base is walked
	// forward by every paused frame's worth of wall clock and both readouts stand still.
	//
	UnsignedInt gameSecs = logicFrame / LOGICFRAMES_PER_SECOND;
	if( m_hudRealClockBaseMs == 0 )
		m_hudRealClockBaseMs = nowMs - gameSecs * 1000;
	else if( TheGameLogic->isGamePaused() )
		m_hudRealClockBaseMs += nowMs - m_hudLastDrawMs;
	m_hudLastDrawMs = nowMs;
	UnsignedInt realSecs = (nowMs - m_hudRealClockBaseMs) / 1000;

	// the machine's own clock first, for a player who wants to know when to stop
	SYSTEMTIME wallClock;
	GetLocalTime( &wallClock );

	UnicodeString text;
	text.format( L"%02d:%02d   %02d:%02d:%02d(%02d:%02d:%02d)   %dhz(%dfps) %s",
							 wallClock.wHour, wallClock.wMinute,
							 gameSecs / 3600, (gameSecs / 60) % 60, gameSecs % 60,
							 realSecs / 3600, (realSecs / 60) % 60, realSecs % 60,
							 m_hudLogicHz.shown, m_hudFps.shown,
							 TheDisplay->getRendererName() );

	UnicodeString frameText;
	frameText.format( L"   frame %d", (Int)logicFrame );
	text.concat( frameText );

	// in a network game, how far ahead the room can play without waiting on anybody, out of the
	// input delay it is running with, and the frame rate the slowest machine has set for everyone:
	// ready falling to 0 is the stall, seen before it is felt
	if( TheNetwork != NULL )
	{
		UnicodeString netText;
		netText.format( L"   ready %d/%d   room %dfps", (Int)TheNetwork->getFramesReady(),
										(Int)TheNetwork->getRunAhead(), (Int)TheNetwork->getFrameRate() );
		text.concat( netText );
	}

	// the lobby's unit limit, as this player's own share and not the match total: what stands and
	// what is queued, against the number the production queue refuses at
	const UnsignedInt unitCap = TheGameLogic->getUnitCap();
	const Player *localPlayer = ThePlayerList ? ThePlayerList->getLocalPlayer() : NULL;
	if( unitCap > 0 && localPlayer && !localPlayer->isPlayerObserver() )
	{
		UnicodeString units;
		units.format( L"   %d/%d units", localPlayer->countUnitsTowardCap(), unitCap );
		text.concat( units );
	}

	// the same readings, one value each, for the page
	char reading[ sizeof( "00:00:00" ) ];
	m_hudValues.clear();
	sprintf( reading, "%02d:%02d", wallClock.wHour, wallClock.wMinute );
	m_hudValues[ "net.clock" ] = reading;
	sprintf( reading, "%02u:%02u:%02u", gameSecs / 3600, ( gameSecs / 60 ) % 60, gameSecs % 60 );
	m_hudValues[ "net.game" ] = reading;
	sprintf( reading, "%02u:%02u:%02u", realSecs / 3600, ( realSecs / 60 ) % 60, realSecs % 60 );
	m_hudValues[ "net.real" ] = reading;
	m_hudValues[ "net.hz" ] = std::to_string( m_hudLogicHz.shown );
	m_hudValues[ "net.fps" ] = std::to_string( m_hudFps.shown );
	m_hudValues[ "net.renderer" ] = WideCharStringToMultiByte( TheDisplay->getRendererName() );
	// as of the last rate reading, twice a second: every frame laid the box out thirty times a second
	m_hudValues[ "net.frame" ] = std::to_string( m_hudLastSampleLogicFrame );
	if( TheNetwork != NULL )
	{
		enum { BYTES_PER_KILOBYTE = 1024 };
		m_hudValues[ "net.online" ] = "online";
		m_hudValues[ "net.ready" ] = std::to_string( TheNetwork->getFramesReady() );
		m_hudValues[ "net.runahead" ] = std::to_string( TheNetwork->getRunAhead() );
		m_hudValues[ "net.room" ] = std::to_string( TheNetwork->getFrameRate() );
		m_hudValues[ "net.in" ] = std::to_string( REAL_TO_INT( TheNetwork->getIncomingBytesPerSecond() / BYTES_PER_KILOBYTE ) );
		m_hudValues[ "net.out" ] = std::to_string( REAL_TO_INT( TheNetwork->getOutgoingBytesPerSecond() / BYTES_PER_KILOBYTE ) );
	}
	if( unitCap > 0 && localPlayer && !localPlayer->isPlayerObserver() )
	{
		m_hudValues[ "net.units" ] = std::to_string( localPlayer->countUnitsTowardCap() );
		m_hudValues[ "net.cap" ] = std::to_string( unitCap );
		m_hudValues[ "net.capped" ] = "capped";
	}

	if( !plate )
		return;

	if( m_hudDisplayString == NULL )
	{
		// bold at a small point size, because this plate is read at a glance rather than read
		m_hudDisplayString = TheDisplayStringManager->newDisplayString();
		m_hudDisplayString->setFont( TheFontLibrary->getFont( m_superweaponNormalFont,
										TheGlobalLanguageData->adjustFontSize( HUD_CLOCK_POINT_SIZE ),
										TRUE ) );
	}
	m_hudDisplayString->setText( text );

	Int textWidth = 0, textHeight = 0;
	m_hudDisplayString->getSize( &textWidth, &textHeight );

	// top right, clear of the radar and the superweapon timers, and first in that corner: the peace
	// time clock is drawn across the top middle of the screen and takes no room here.
	//
	// The three gaps are 800x600 numbers put through the command bar's own scale, like everything
	// else on this overlay.  Held at a flat pixel count the plate crept into the corner as the
	// screen grew: the lettering inside it scales and the margin around it did not, so a 1440-tall
	// shot and a 1080-tall one did not overlay however the text was sized.
	const Int pad = stripPixels( 2 );
	Int x = TheDisplay->getWidth() - textWidth - pad - stripPixels( 4 );
	Int y = stripPixels( 2 );

	// a plate behind it, so it stays legible over bright terrain
	TheDisplay->drawFillRect( x - pad, y - 1, textWidth + pad*2, textHeight + 2,
														GameMakeColor( 0, 0, 0, 140 ) );

	// the superweapon timers read this to start below the plate rather than behind it
	m_hudOverlayBottom = y - 1 + textHeight + 2;

	m_hudDisplayString->draw( x, y, GameMakeColor( 235, 235, 235, 255 ), GameMakeColor( 0, 0, 0, 255 ) );
}

//-------------------------------------------------------------------------------------------------
// The production strip: one cameo per queued item, drawn over the world in a column standing on
// the corner just above the control bar and growing upward, soonest to finish at the bottom - the
// item about to pop is always the cell nearest the bar, wherever in the base it is being built.
// That column is global - everything the local player has queued anywhere - and the buildings
// going up on the ground stand in a second column beside it. A column draws at most
// PRODUCTION_STRIP_ROW_MAX cameos and closes with a sixth cell wearing a "+N" for whatever else is
// queued. Units and upgrades wear the same two border colours the command bar uses. The item a
// building is actually working on wears a radial fill. A click takes the camera to the building an
// item is queued on; right-click (or Ctrl-click) cancels one queued item.
//-------------------------------------------------------------------------------------------------

//-------------------------------------------------------------------------------------------------
/** Append everything this object has queued, units and upgrades alike, into a list kept sorted by
	* how long each item still has to wait - soonest first. Everything is counted in *total; only
	* the 'max' items that finish first get a slot to be drawn in, whatever order they were met in.
	*
	* The wait is cumulative down a producer's queue: an entry only starts once everything ahead of
	* it has popped, so its remaining time is the time left on the entry in front plus its own build
	* time. Only the head of a queue carries progress, so this is what separates "arrives next" from
	* "arrives eventually" across a whole base's worth of buildings. A quantity modifier (the China
	* barracks' Red Guard pairs) does not multiply the wait - the entry overbuilds in place and all
	* of its units pop in the same handful of frames.
	*
	* 'leads' marks the building you have selected: its items go to the head of the row ahead of
	* everything else, sorted among themselves the same way. The building you are looking at is the
	* one you want to read, and hunting its cameos out of the whole base's queue by their border was
	* the job a second row used to do badly. */
//-------------------------------------------------------------------------------------------------
static void appendProducerQueue( Object *obj, InGameUI::ProductionStripSlot *slots,
																 Int *count, Int max, Int *total, Bool leads )
{
	if( obj == NULL )
		return;

	ProductionUpdateInterface *pu = obj->getProductionUpdateInterface();
	if( pu == NULL )
		return;

	Player *owner = obj->getControllingPlayer();
	Int ahead = 0;					///< frames the queue in front of the current entry still needs
	Int runAt = -1;					///< where this producer's last item went, so a run of the same thing folds into it

	for( const ProductionEntry *p = pu->firstProduction(); p; p = pu->nextProduction( p ) )
	{
		Int id = 0;
		Int typeKey = 0;
		Bool isUpgrade = FALSE;
		Int buildFrames = 0;

		if( p->getProductionType() == PRODUCTION_UNIT )
		{
			id = (Int)p->getProductionID();
			if( p->getProductionObject() )
			{
				buildFrames = p->getProductionObject()->calcTimeToBuild( owner );
				typeKey = (Int)p->getProductionObject()->getTemplateID();
			}
		}
		else if( p->getProductionType() == PRODUCTION_UPGRADE && p->getProductionUpgrade() )
		{
			isUpgrade = TRUE;
			id = (Int)p->getProductionUpgrade()->getUpgradeNameKey();
			typeKey = id;
			buildFrames = p->getProductionUpgrade()->calcTimeToBuild( owner );
		}
		else
			continue;

		Int left = buildFrames - REAL_TO_INT( buildFrames * p->getPercentComplete() / 100.0f );
		if( left < 0 )
			left = 0;					// an item that is overbuilding while it waits for a door
		const Int remaining = ahead + left;
		ahead = remaining;

		//
		// one cameo per queue entry, not per unit that entry will deliver. A quantity modifier
		// (ProductionUpdate's QuantityModifier - the China barracks builds Red Guards in pairs)
		// makes one order hand back several units, but it is still one order: it was paid for
		// once, the command bar's count badge counts it once, and one cancel takes all of it
		// away. Drawing it as several cameos said the player had queued more than they had, and
		// promised a cancel per cameo that does not exist.
		//
		(*total)++;

		//
		// Ten Red Guards queued back to back are one order repeated, not ten things to read: they
		// fold into the cameo the run started, which wears an "xN".  Only a run does - the same unit
		// queued again after something else is a second cameo, because that is what the queue looks
		// like - and only within one producer's queue, so the cameo still stands for a place you can
		// jump to and an item you can cancel.  The countdown stays the one the run's first item
		// carries: what the cameo says is when the next of these arrives.
		//
		if( runAt >= 0 && runAt < *count && slots[ runAt ].typeKey == typeKey
				&& slots[ runAt ].isUpgrade == isUpgrade && slots[ runAt ].producer == obj->getID() )
		{
			slots[ runAt ].quantity++;
			continue;
		}

		//
		// walk back over the items that finish later than this one and drop it in front of them.
		// Ties keep the order they were met in, so a base full of identical barracks stays put
		// instead of shuffling frame to frame.  The selected building's items are a block of their
		// own at the head of the row: one of them passes everything that is not one of them, and
		// nothing else ever passes one.
		//
		Int at = *count;
		while( at > 0 && InGameUI::stripSlotGoesBefore( leads, remaining,
																									 slots[ at - 1 ].leads,
																									 slots[ at - 1 ].remaining ) )
			at--;

		// something else has come between: whatever this producer queues next starts a new run
		runAt = -1;

		if( at >= max )
			continue;						// everything already kept finishes sooner: counted, but not drawn

		if( *count < max )
			(*count)++;

		for( Int j = *count - 1; j > at; j-- )
			slots[ j ] = slots[ j - 1 ];

		runAt = at;

		InGameUI::ProductionStripSlot *slot = &slots[ at ];
		slot->producer = obj->getID();
		slot->id = id;
		slot->typeKey = typeKey;
		slot->isUpgrade = isUpgrade;
		slot->isStructure = FALSE;
		slot->leads = leads;
		slot->remaining = remaining;
		slot->quantity = 1;
		slot->pos.x = 0;
		slot->pos.y = 0;
	}
}

//-------------------------------------------------------------------------------------------------
/** A building going up on the map, for the same column the queues go in.
	*
	* A factory's queue is the only production this strip used to know about, and a base spends half
	* its early game on the other kind: what a dozer or a worker is putting up is nowhere in a queue,
	* it is an object on the ground with a construction percentage on it.  It is sorted in among the
	* queued items on the one thing they share - how long it still has - and a click on one takes the
	* camera to the site, which is the question a half-built base actually raises: where is it. */
//-------------------------------------------------------------------------------------------------
static void appendStructureUnderConstruction( Object *obj, InGameUI::ProductionStripSlot *slots,
																							Int *count, Int max, Int *total )
{
	if( obj == NULL || !obj->testStatus( OBJECT_STATUS_UNDER_CONSTRUCTION ) )
		return;

	//
	// A rebuild hole wears the same status while it puts a GLA structure back up, and it has no
	// cameo of its own to draw - it would come through as an empty box that jumps the camera.
	//
	if( obj->isKindOf( KINDOF_REBUILD_HOLE ) || obj->getTemplate() == NULL
			|| obj->getTemplate()->getButtonImage() == NULL )
		return;

	const Real percent = obj->getConstructionPercent();
	const Int buildFrames = obj->getTemplate()->calcTimeToBuild( obj->getControllingPlayer() );

	Int remaining = REAL_TO_INT( buildFrames * ( 1.0f - percent / 100.0f ) );
	if( remaining < 0 )
		remaining = 0;

	(*total)++;

	// soonest first, ties in the order they were met - the same order the queue row is kept in
	Int at = *count;
	while( at > 0 && InGameUI::stripSlotGoesBefore( FALSE, remaining,
																								 slots[ at - 1 ].leads,
																								 slots[ at - 1 ].remaining ) )
		at--;

	if( at >= max )
		return;

	if( *count < max )
		(*count)++;

	for( Int j = *count - 1; j > at; j-- )
		slots[ j ] = slots[ j - 1 ];

	InGameUI::ProductionStripSlot *slot = &slots[ at ];
	slot->producer = obj->getID();
	slot->id = 0;
	slot->typeKey = 0;
	slot->isUpgrade = FALSE;
	slot->isStructure = TRUE;
	slot->leads = FALSE;
	slot->remaining = remaining;
	// each site is its own place on the map, so two of the same building stay two cameos
	slot->quantity = 1;
	slot->pos.x = 0;
	slot->pos.y = 0;
}

//-------------------------------------------------------------------------------------------------
/** What one Player::iterateObjects sweep fills in: one column of the strip. */
//-------------------------------------------------------------------------------------------------
struct ProductionStripGather
{
	InGameUI::ProductionStripSlot *slot;
	Int *count;
	Int *total;
	Int max;						///< cameos this column will draw; the rest are counted into the "+N"
	ObjectID skip;			///< a building already put into the column ahead of the sweep, so its queue
											///  is not gathered a second time when the sweep reaches it
};

//-------------------------------------------------------------------------------------------------
/** Player::iterateObjects callback: everything one player has coming, queued in a factory or going
	* up on the ground, in the one column.  Both answer the same question - when does the next thing
	* land - so they are sorted against each other rather than kept apart, and a base whose dozers are
	* busy no longer reads its own building sites in a second column off to the side.  Nothing is
	* counted twice: a building under construction produces nothing until it is finished. */
//-------------------------------------------------------------------------------------------------
static void gatherStripEverything( Object *obj, void *userData )
{
	ProductionStripGather *g = (ProductionStripGather *)userData;
	if( obj == NULL || obj->getID() != g->skip )
		appendProducerQueue( obj, g->slot, g->count, g->max, g->total, FALSE );
	appendStructureUnderConstruction( obj, g->slot, g->count, g->max, g->total );
}

//-------------------------------------------------------------------------------------------------
/** Find the queue entry a slot stands for. Re-read every frame rather than cached: production runs
	* on the logic clock and this draws on the render clock, so anything held is stale. */
//-------------------------------------------------------------------------------------------------
static const ProductionEntry *findStripEntry( ProductionUpdateInterface *pu,
																							const InGameUI::ProductionStripSlot *slot )
{
	if( pu == NULL )
		return NULL;

	for( const ProductionEntry *p = pu->firstProduction(); p; p = pu->nextProduction( p ) )
	{
		if( slot->isUpgrade )
		{
			if( p->getProductionType() == PRODUCTION_UPGRADE && p->getProductionUpgrade() &&
					(Int)p->getProductionUpgrade()->getUpgradeNameKey() == slot->id )
				return p;
		}
		else if( p->getProductionType() == PRODUCTION_UNIT && (Int)p->getProductionID() == slot->id )
			return p;
	}

	return NULL;
}

//-------------------------------------------------------------------------------------------------
/** The picture a slot is drawn with, given its producer and the queue entry found for it: the
	* building going up, or what the entry makes.  NULL once the producer or the entry is gone, or
	* for a template that carries no picture. */
//-------------------------------------------------------------------------------------------------
static const Image *stripSlotCameo( const Object *producer, const ProductionEntry *entry,
																		const InGameUI::ProductionStripSlot *slot )
{
	if( slot->isStructure )
		return producer ? producer->getTemplate()->getButtonImage() : NULL;
	if( entry == NULL )
		return NULL;
	if( slot->isUpgrade )
		return entry->getProductionUpgrade() ? entry->getProductionUpgrade()->getButtonImage() : NULL;
	return entry->getProductionObject() ? entry->getProductionObject()->getButtonImage() : NULL;
}

/** The command button that builds a unit or a building, the one its build card is filled in from. */
static const CommandButton *buildButtonForThing( const ThingTemplate *thing )
{
	for( const CommandButton *button = TheControlBar->getCommandButtons(); button; button = button->getNext() )
	{
		const GUICommandType type = button->getCommandType();
		if( ( type == GUI_COMMAND_UNIT_BUILD || type == GUI_COMMAND_DOZER_CONSTRUCT ) && button->getThingTemplate()
				&& button->getThingTemplate()->isEquivalentTo( thing ) )
			return button;
	}
	return NULL;
}

/** The command button that buys an upgrade. */
static const CommandButton *buildButtonForUpgrade( const UpgradeTemplate *upgrade )
{
	for( const CommandButton *button = TheControlBar->getCommandButtons(); button; button = button->getNext() )
	{
		const GUICommandType type = button->getCommandType();
		if( ( type == GUI_COMMAND_PLAYER_UPGRADE || type == GUI_COMMAND_OBJECT_UPGRADE ) && button->getUpgradeTemplate() == upgrade )
			return button;
	}
	return NULL;
}

/** A page's data-tip for a button on a player's row: the player's index, a space and the button's
	* name, which ControlBar::findCommandButton finds it by.  Button names are INI tokens and hold no
	* space.  Empty for no button. */
static std::string buttonTip( const CommandButton *button, const Player *owner )
{
	return button ? std::to_string( owner->getPlayerIndex() ) + " " + button->getName().str() : "";
}

/** The button that builds what stripSlotCameo draws, NULL where it draws nothing. */
static const CommandButton *stripSlotButton( const Object *producer, const ProductionEntry *entry,
																						 const InGameUI::ProductionStripSlot *slot )
{
	if( slot->isStructure )
		return producer ? buildButtonForThing( producer->getTemplate() ) : NULL;
	if( entry == NULL )
		return NULL;
	if( slot->isUpgrade )
		return entry->getProductionUpgrade() ? buildButtonForUpgrade( entry->getProductionUpgrade() ) : NULL;
	return entry->getProductionObject() ? buildButtonForThing( entry->getProductionObject() ) : NULL;
}

//-------------------------------------------------------------------------------------------------
/** The tray a queue cameo stands in: the general's power bar's own, this side's copy of it, turned
	* back to front.  That bar grows leftward out of the corner and its tray's heavy rail is on the
	* right hand edge; mirrored, the rail leads a row running the other way.
	*
	* Kept until the bar hands back a different tray - a side change, an observer picking a different
	* player out of the list, a mod's own bar - rather than rebuilt per cameo per frame.  NULL when
	* there is no bar and nothing to borrow one from. */
//-------------------------------------------------------------------------------------------------
const Image *InGameUI::productionStripTray( void )
{
	const Image *source = TheControlBar ? TheControlBar->getSpecialPowerTrayImage() : NULL;

	if( source != m_productionStripTraySource )
	{
		if( m_productionStripTray )
		{
			m_productionStripTray->deleteInstance();
			m_productionStripTray = NULL;
		}

		if( source )
			m_productionStripTray = newMirroredImage( source );

		m_productionStripTraySource = source;
	}

	return m_productionStripTray;
}

//-------------------------------------------------------------------------------------------------
/** How big a strip slot is and where the cameo sits in it - the general's power bar's own numbers,
	* at the size the loader gave that bar, so a slot here is a slot there at any resolution.  'hole'
	* is the inset the bar itself uses, measured from the tray's left edge; a strip that draws the
	* tray mirrored turns it round itself.
	*
	* Falls back to the 800x600 numbers that bar was authored with when there is no bar to measure -
	* a side whose template ships no shortcuts still gets a strip. */
//-------------------------------------------------------------------------------------------------
void InGameUI::stripTrayMetrics( ICoord2D *tray, ICoord2D *cameo, ICoord2D *hole, Int *step )
{
	if( !TheControlBar || !TheControlBar->getSpecialPowerTrayLayout( tray, cameo, hole, step ) )
	{
		tray->x = stripPixels( PRODUCTION_STRIP_TRAY_W );
		tray->y = stripPixels( PRODUCTION_STRIP_TRAY_H );
		cameo->x = stripPixels( PRODUCTION_STRIP_QUEUE_W );
		cameo->y = stripPixels( PRODUCTION_STRIP_QUEUE_H );
		hole->x = stripPixels( PRODUCTION_STRIP_TRAY_X );
		hole->y = stripPixels( PRODUCTION_STRIP_TRAY_Y );
	}

	//
	// Across a row the trays close up an eighth of a tray under each other, so a row reads as one
	// run of metal rather than a line of separate boxes.  The command bar's own power slots stopped
	// doing that because they are windows and the neighbour's rail was drawn over each cameo's edge;
	// every strip here lays all its trays down before any cameo, so the rails overlap each other and
	// never a picture.  The step never closes past a cameo's own width, so no two pictures touch.
	//
	const Int overlapped = tray->x - tray->x / STRIP_TRAY_OVERLAP_PARTS;
	const Int narrowest = cameo->x + stripPixels( PRODUCTION_STRIP_GAP );
	*step = overlapped > narrowest ? overlapped : narrowest;
}

//-------------------------------------------------------------------------------------------------
/** Where an item goes in a row: the selected building's items are one block at the head of it, and
	* inside a block the soonest to finish comes first.  Every row of the strip is kept in this order
	* as it is filled, so the front of the row is always the next thing to arrive out of the building
	* you are looking at, and the row behind it is the rest of the base. */
//-------------------------------------------------------------------------------------------------
Bool InGameUI::stripSlotGoesBefore( Bool leads, Int remaining,
																		Bool otherLeads, Int otherRemaining )
{
	if( leads != otherLeads )
		return leads;						// the selected building's block leads, whatever anything finishes in

	return otherRemaining > remaining;		// inside a block: soonest first, ties stay as they were met
}

//-------------------------------------------------------------------------------------------------
/** Write a countdown inside a cameo.  The radial sweep says how much of the whole is left, which
	* is a shape rather than a number: it answers "nearly" and never "eleven seconds".  Both strips
	* put the number in the bottom left corner of the box, over the sweep - the middle is where the
	* picture is, and a number sitting on it hid the one thing the cameo is there to show. */
//-------------------------------------------------------------------------------------------------
void InGameUI::drawStripSeconds( Int which, Int x, Int y, Int w, Int h, Int seconds )
{
	// its own string, kept between frames - see m_stripSecondsString
	DisplayString *&secondsString = m_stripSecondsString[ which ];

	if( secondsString == NULL )
	{
		secondsString = TheDisplayStringManager->newDisplayString();
		secondsString->setFont( TheFontLibrary->getFont( m_superweaponNormalFont,
										TheGlobalLanguageData->adjustFontSize( PRODUCTION_STRIP_SECS ),
										TRUE ) );
	}

	UnicodeString text;
	formatStripSeconds( &text, seconds );
	secondsString->setText( text );

	Int textWidth = 0, textHeight = 0;
	secondsString->getSize( &textWidth, &textHeight );

	const Int textX = x + 1;
	const Int textY = y + h - textHeight - 1;

	// a plate under it: down in the corner the number sits on whatever the picture happens to be
	// there, and a pale cameo swallowed the drop shadow along with the digits
	if( textWidth > 0 && textHeight > 0 )
		TheDisplay->drawFillRect( textX - 1, textY, textWidth + 2, textHeight,
															GameMakeColor( 0, 0, 0, 160 ) );

	secondsString->draw( textX, textY, GameMakeColor( 245, 245, 245, 255 ),
											 GameMakeColor( 0, 0, 0, 255 ) );
}

//-------------------------------------------------------------------------------------------------
/** The "xN" a folded run of the same item wears, in the cameo's top right corner - the corner the
	* command bar puts its own count badge in, and the corner the countdown in the middle leaves free.
	* Kept per cameo for the reason the countdowns are: a DisplayString rebuilds a font surface every
	* time its text changes, and this one changes only when the run does. */
//-------------------------------------------------------------------------------------------------
void InGameUI::drawStripQuantity( Int which, Int x, Int y, Int w, Int quantity )
{
	DisplayString *&quantityString = m_stripQuantityString[ which ];

	if( quantityString == NULL )
	{
		quantityString = TheDisplayStringManager->newDisplayString();
		quantityString->setFont( TheFontLibrary->getFont( m_superweaponNormalFont,
										TheGlobalLanguageData->adjustFontSize( PRODUCTION_STRIP_SECS ),
										TRUE ) );
	}

	UnicodeString text;
	text.format( L"x%d", quantity );
	quantityString->setText( text );

	Int textWidth = 0, textHeight = 0;
	quantityString->getSize( &textWidth, &textHeight );

	quantityString->draw( x + w - textWidth - 1, y + 1,
												GameMakeColor( 255, 255, 255, 255 ),
												GameMakeColor( 0, 0, 0, 255 ) );
}

//-------------------------------------------------------------------------------------------------
/** Keep one superweapon timer for the strip, in a list sorted by how long it still has to wait -
	* soonest first.  Everything live is counted; only the ones that will be drawn get a slot. */
//-------------------------------------------------------------------------------------------------
void InGameUI::addSuperweaponIcon( const Image *image, Int seconds, Int percent, Bool ready, Color color )
{
	m_superweaponIconTotal++;

	// walk back over the ones that come ready later than this and drop it in front of them; ties
	// keep the order they were met in, so a pair of identical silos stays put frame to frame
	Int at = m_superweaponIconCount;
	while( at > 0 && m_superweaponIcons[ at - 1 ].seconds > seconds )
		at--;

	if( at >= SUPERWEAPON_STRIP_MAX )
		return;						// everything already kept is sooner: counted into the "+N", not drawn

	if( m_superweaponIconCount < SUPERWEAPON_STRIP_MAX )
		m_superweaponIconCount++;

	for( Int j = m_superweaponIconCount - 1; j > at; j-- )
		m_superweaponIcons[ j ] = m_superweaponIcons[ j - 1 ];

	SuperweaponIconSlot *slot = &m_superweaponIcons[ at ];
	slot->image = image;
	slot->seconds = seconds;
	slot->percent = percent;
	slot->ready = ready;
	slot->color = color;
}

//-------------------------------------------------------------------------------------------------
/** The superweapon strip: the cameos gathered this frame, top right, under the clock plate.
	*
	* Rows fill from the right, because the right hand end is where the strip is anchored and the
	* one countdown that matters is the next one to land - it is always in the same place, however
	* many are behind it.  Three rows of six, and whatever is left over closes the last row as a
	* "+N", the same way the production strip's rows do. */
//-------------------------------------------------------------------------------------------------
void InGameUI::drawSuperweaponStrip( void )
{
	// watching, the spectator page's left panel lists the countdowns instead
	// playing under the bar's page, the countdowns are on the Tab scoreboard
	if( m_superweaponIconCount < 1 || stripSwitchedOff( &GlobalData::m_showSuperweaponStrip ) || m_spectatorPageShown ||
			( m_controlBarPageShown && !localPlayerWatching() ) )
		return;

	if( TheGameLogic == NULL || !TheGameLogic->isInGame() || TheGameLogic->isInShellGame() )
		return;

	//
	// The same tray the production strip stands its cameos in, and the same measurements off the
	// general's power bar - but this strip is not mirrored. It is anchored to the right hand edge
	// and grows leftwards, which is the direction that bar itself grows, so the artwork sits the
	// way it was drawn: the heavy rail leads the row at the right hand end.
	//
	ICoord2D traySize, cameoSize, trayHole;
	Int trayStep = 0;
	stripTrayMetrics( &traySize, &cameoSize, &trayHole, &trayStep );

	const Int trayW = traySize.x;
	const Int trayH = traySize.y;
	const Int cameoW = cameoSize.x;
	const Int cameoH = cameoSize.y;
	const Image *tray = TheControlBar ? TheControlBar->getSpecialPowerTrayImage() : NULL;

	const Int gap = stripPixels( PRODUCTION_STRIP_GAP );
	const Int more = stripPixels( PRODUCTION_STRIP_MORE );
	const Int plate = stripPixels( 3 );

	//
	// The corner clock plate owns the top right, so the strip starts under it whenever it is up -
	// a countdown drawn behind the readout is one nobody can read.
	//
	Int top = plate;
	if( m_hudOverlayBottom + plate > top )
		top = m_hudOverlayBottom + plate;

	//
	// one pulse for the whole strip rather than one per icon, so every charged superweapon breathes
	// together instead of each on its own clock.  SuperweaponCountdownFlashDuration is half a cycle
	// (dark to bright), so the INI knob still says how fast the strip blinks.
	//
	Real pulse = 1.0f;
	if( m_superweaponFlashDuration >= 1.0f )
	{
		const Real period = 2.0f * m_superweaponFlashDuration;
		const Real phase = (Real)( TheGameLogic->getFrame() % (UnsignedInt)period ) / period;
		pulse = 0.5f - 0.5f * (Real)cos( 2.0 * PI * phase );
	}

	// flush against the right hand edge: the tray's heavy rail is the edge of the strip, and an inset
	// leaves it hanging in the middle of nothing.  The production rows keep their inset because their
	// rail faces the other way, into the screen
	const Int right = TheDisplay->getWidth();
	const Int hidden = m_superweaponIconTotal - m_superweaponIconCount;

	// drawn as a batch, for the reason drawProductionStrip() gives - see Display::beginBatch2D
	TheDisplay->beginBatch2D();

	for( Int row = 0; row < SUPERWEAPON_STRIP_ROWS; row++ )
	{
		const Int first = row * SUPERWEAPON_STRIP_COLS;
		if( first >= m_superweaponIconCount )
			break;

		Int inRow = m_superweaponIconCount - first;
		if( inRow > SUPERWEAPON_STRIP_COLS )
			inRow = SUPERWEAPON_STRIP_COLS;

		const Bool lastRow = ( first + inRow >= m_superweaponIconCount );
		Int rowWidth = ( inRow - 1 ) * trayStep + trayW;
		if( hidden > 0 && lastRow )
			rowWidth += gap + more;

		const Int trayY = top + row * trayH;
		const Int y = trayY + trayHole.y;

		//
		// The trays go down first, all of them, and from the far end back, so the rightmost - the
		// countdown that lands next - is the one drawn last.
		//
		for( Int back = inRow - 1; back >= 0; back-- )
		{
			const Int backX = right - trayW - back * trayStep;
			if( tray )
				TheDisplay->drawImage( tray, backX, trayY, backX + trayW, trayY + trayH );
			else
				TheDisplay->drawFillRect( backX, trayY, trayW, trayH, GameMakeColor( 0, 0, 0, 130 ) );
		}

		//
		// The row goes down a piece at a time - every picture, then every sweep, then every border -
		// rather than an icon at a time, for the reason drawProductionStripRow() gives: pieces that
		// want the same thing of the renderer are one draw call when they follow each other and one
		// draw call each when they do not.  The icons do not overlap, so nothing changes on screen.
		//
		for( Int cameoSlot = 0; cameoSlot < inRow; cameoSlot++ )
		{
			const SuperweaponIconSlot *slot = &m_superweaponIcons[ first + cameoSlot ];
			const Int x = right - trayW + trayHole.x - cameoSlot * trayStep;

			if( slot->image )
				TheDisplay->drawImage( slot->image, x, y, x + cameoW, y + cameoH );
		}

		//
		// the same sweep the production cameos wear, and the same way round as the command bar's own
		// clock: the scrim covers what is still to be charged and is swept off as the charge runs
		//
		for( Int clockSlot = 0; clockSlot < inRow; clockSlot++ )
		{
			const SuperweaponIconSlot *slot = &m_superweaponIcons[ first + clockSlot ];
			const Int x = right - trayW + trayHole.x - clockSlot * trayStep;

			if( !slot->ready )
				TheDisplay->drawRemainingRectClock( x, y, cameoW, cameoH, slot->percent,
																						GameMakeColor( 0, 0, 0, 130 ) );
			else
			{
				//
				// Charged: no number at all - zero seconds is not information - and the cameo itself
				// breathes in the owning player's colour instead.  A ready superweapon is the one
				// thing on this strip that wants to be noticed rather than looked up, and a
				// translucent wash over the picture says whose it is in the same stroke.
				//
				UnsignedByte r, g, b, a;
				GameGetColorComponents( slot->color, &r, &g, &b, &a );
				const UnsignedByte washAlpha = (UnsignedByte)( 30.0f + 90.0f * pulse );
				TheDisplay->drawFillRect( x, y, cameoW, cameoH, GameMakeColor( r, g, b, washAlpha ) );
			}
		}

		//
		// bare seconds, however many there are: this strip is read against the other countdowns on
		// the screen, and m:ss is a number you have to convert first
		//
		for( Int secondsSlot = 0; secondsSlot < inRow; secondsSlot++ )
		{
			const SuperweaponIconSlot *slot = &m_superweaponIcons[ first + secondsSlot ];
			const Int x = right - trayW + trayHole.x - secondsSlot * trayStep;

			if( !slot->ready )
				drawStripSeconds( PRODUCTION_STRIP_ROW_MAX + first + secondsSlot,
													x, y, cameoW, cameoH, slot->seconds );
		}

		// the border is whose weapon it is - the colour the timer was registered with
		for( Int borderSlot = 0; borderSlot < inRow; borderSlot++ )
		{
			const SuperweaponIconSlot *slot = &m_superweaponIcons[ first + borderSlot ];
			const Int x = right - trayW + trayHole.x - borderSlot * trayStep;

			UnsignedByte r, g, b, a;
			GameGetColorComponents( slot->color, &r, &g, &b, &a );
			TheDisplay->drawOpenRect( x, y, cameoW, cameoH, 2.0f, GameMakeColor( r, g, b, 255 ) );
		}

		//
		// whatever did not fit closes the last row as a "+N", on the left hand end: the strip is
		// read from the right, so the overflow sits at the far end of it
		//
		if( hidden > 0 && lastRow )
		{
			// this strip's own "+N", kept apart from the production rows' - see m_stripSecondsString
			DisplayString *&overflow = m_productionStripOverflow[ STRIP_OVERFLOW_SUPERWEAPON ];

			if( overflow == NULL )
			{
				overflow = TheDisplayStringManager->newDisplayString();
				overflow->setFont( TheFontLibrary->getFont( m_superweaponNormalFont,
														TheGlobalLanguageData->adjustFontSize( HUD_OVERLAY_POINT_SIZE ),
														TRUE ) );
			}

			UnicodeString text;
			text.format( L"+%d", hidden );
			overflow->setText( text );

			Int textWidth = 0, textHeight = 0;
			overflow->getSize( &textWidth, &textHeight );

			overflow->draw( right - rowWidth + ( more - textWidth ) / 2,
											y + ( cameoH - textHeight ) / 2,
											GameMakeColor( 235, 235, 235, 255 ),
											GameMakeColor( 0, 0, 0, 255 ) );
		}
	}

	TheDisplay->endBatch2D();
}

//-------------------------------------------------------------------------------------------------
/** One bought promotion, and the cameo the promotion screen buys it from. */
struct BoughtSkill
{
	ScienceType science;
	const CommandButton *button;
};

/** Every science in one of a general's three promotion command sets that the player has actually
	* bought.  Appends to skills and hands back the new count, so the three sets fill one list in rank
	* order. */
//-------------------------------------------------------------------------------------------------
static Int gatherSkillCameos( const Player *player, const AsciiString &setName,
															BoughtSkill *skills, Int count, Int max )
{
	const CommandSet *set = TheControlBar->findCommandSet( setName );
	if( set == NULL )
		return count;

	for( Int i = 0; i < MAX_COMMANDS_PER_SET && count < max; i++ )
	{
		const CommandButton *button = set->getCommandButton( i );
		if( button == NULL || button->getScienceVec().empty() || button->getButtonImage() == NULL )
			continue;

		const ScienceType science = button->getScienceVec()[ 0 ];
		if( !player->hasScience( science ) || player->isScienceHidden( science ) )
			continue;

		skills[ count ].science = science;
		skills[ count ].button = button;
		count++;
	}

	return count;
}

//-------------------------------------------------------------------------------------------------
/** Everything one player has bought out of his three promotion sets, in rank order, as the
	* promotion screen's buttons appended to buttons, each with a picture.  Hands back the new count.
	*
	* A level that a later level of the same power has replaced is left out: Artillery Barrage 3 is
	* one cameo, not three of the same picture in a row.  "Replaced" is the science's own
	* prerequisite list, so the second level asking for the first is what hides the first. */
//-------------------------------------------------------------------------------------------------
static Int gatherPlayerSkills( const Player *player, const CommandButton **buttons, Int count, Int max )
{
	const PlayerTemplate *playerTemplate = player->getPlayerTemplate();
	if( playerTemplate == NULL )
		return count;

	enum { MOST_SKILLS = 3 * MAX_COMMANDS_PER_SET };
	BoughtSkill skills[ MOST_SKILLS ];
	Int bought = gatherSkillCameos( player, playerTemplate->getPurchaseScienceCommandSetRank1(),
																	skills, 0, MOST_SKILLS );
	bought = gatherSkillCameos( player, playerTemplate->getPurchaseScienceCommandSetRank3(),
															skills, bought, MOST_SKILLS );
	bought = gatherSkillCameos( player, playerTemplate->getPurchaseScienceCommandSetRank8(),
															skills, bought, MOST_SKILLS );

	for( Int i = 0; i < bought && count < max; i++ )
	{
		Bool replaced = FALSE;
		for( Int later = 0; later < bought && !replaced; later++ )
			replaced = TheScienceStore->isDirectPrereq( skills[ i ].science, skills[ later ].science );

		if( replaced )
			continue;

		buttons[ count ] = skills[ i ].button;
		count++;
	}

	return count;
}

//-------------------------------------------------------------------------------------------------
/** The skill strip: what the generals have spent their promotions on, down the right hand edge
	* under the superweapon countdowns.
	*
	* Watching a match, those choices decide half of what is about to happen on the field, and the
	* only place they were written down was a screen you had to open - which came up blank anyway,
	* because it was filled in with the watcher's own empty template.  The strip reads the way the
	* production rows on the other side of the screen do: with nothing selected it is every player
	* at once, one row each in his own colour, and a selected unit narrows the whole screen to his
	* owner - his row, and as many rows as his promotions need.  Playing, it is not drawn at all:
	* your own promotions are one key away and you bought them yourself. */
//-------------------------------------------------------------------------------------------------
void InGameUI::drawSkillStrip( void )
{
	// the spectator page's left panel lists them instead; the page's flag is the last frame's here
	if( stripSwitchedOff( &GlobalData::m_showSkillStrip ) || m_spectatorPageShown )
		return;
	if( TheGameLogic == NULL || !TheGameLogic->isInGame() || TheGameLogic->isInShellGame() )
		return;

	Player *local = ThePlayerList->getLocalPlayer();
	if( local == NULL || local->isPlayerActive() || TheControlBar == NULL )
		return;

	const CommandButton *skills[ SKILL_STRIP_MAX ];
	Color rowColor[ SKILL_STRIP_ROWS ];
	Int rowCount[ SKILL_STRIP_ROWS ];
	Int rows = 0;

	Player *selected = TheControlBar->getSelectedPlayer();
	if( selected )
	{
		const Int count = gatherPlayerSkills( selected, skills, 0, SKILL_STRIP_MAX );
		const Color color = clientPlayerColor( selected );

		while( rows * SKILL_STRIP_COLS < count && rows < SKILL_STRIP_ROWS )
		{
			const Int left = count - rows * SKILL_STRIP_COLS;
			rowCount[ rows ] = left > SKILL_STRIP_COLS ? SKILL_STRIP_COLS : left;
			rowColor[ rows ] = color;
			rows++;
		}
	}
	else
	{
		for( Int i = 0; i < ThePlayerList->getPlayerCount() && rows < SKILL_STRIP_ROWS; i++ )
		{
			Player *player = ThePlayerList->getNthPlayer( i );
			if( player == local || !player->isPlayerActive() || !player->isPlayableSide() )
				continue;

			// a row is one player's, so his own run stops at the end of it rather than running on
			const Int start = rows * SKILL_STRIP_COLS;
			const Int count = gatherPlayerSkills( player, skills, start, start + SKILL_STRIP_COLS );
			if( count == start )
				continue;						// nothing bought yet: no row rather than an empty one

			rowCount[ rows ] = count - start;
			rowColor[ rows ] = clientPlayerColor( player );
			rows++;
		}
	}

	if( rows < 1 )
		return;

	ICoord2D traySize, cameoSize, trayHole;
	Int trayStep = 0;
	stripTrayMetrics( &traySize, &cameoSize, &trayHole, &trayStep );

	const Int trayW = traySize.x;
	const Int trayH = traySize.y;
	const Int cameoW = cameoSize.x;
	const Int cameoH = cameoSize.y;
	const Image *tray = TheControlBar->getSpecialPowerTrayImage();

	//
	// Bottom right, standing on the control bar and growing upward - the production rows' own
	// corner, on the other side of the screen.  It was up under the superweapon countdowns to begin
	// with, which is where the eye is not: watching a match you read the bottom of the screen, and
	// the two strips now sit at either end of the same shelf.
	//
	const Int barTop = controlBarTop();

	const Int lowerY = barTop - ( trayH - trayHole.y ) - stripPixels( PRODUCTION_STRIP_LIFT );
	const Int right = TheDisplay->getWidth();

	// drawn as a batch, for the reason drawProductionStrip() gives - see Display::beginBatch2D
	TheDisplay->beginBatch2D();

	for( Int row = 0; row < rows; row++ )
	{
		const Int first = row * SKILL_STRIP_COLS;
		const Int inRow = rowCount[ row ];

		UnsignedByte red, green, blue, alpha;
		GameGetColorComponents( rowColor[ row ], &red, &green, &blue, &alpha );

		// the first row is the one on the bar, and the rest are piled over it
		const Int y = lowerY - row * trayH;
		const Int trayY = y - trayHole.y;
		if( trayY < 0 )
			break;

		for( Int back = inRow - 1; back >= 0; back-- )
		{
			const Int backX = right - trayW - back * trayStep;
			if( tray )
				TheDisplay->drawImage( tray, backX, trayY, backX + trayW, trayY + trayH );
			else
				TheDisplay->drawFillRect( backX, trayY, trayW, trayH, GameMakeColor( 0, 0, 0, 130 ) );
		}

		for( Int cameoSlot = 0; cameoSlot < inRow; cameoSlot++ )
		{
			const Int x = right - trayW + trayHole.x - cameoSlot * trayStep;
			TheDisplay->drawImage( skills[ first + cameoSlot ]->getButtonImage(), x, y, x + cameoW, y + cameoH );
		}

		// whose skills these are, in his own colour, the same border the superweapon cameos wear
		for( Int borderSlot = 0; borderSlot < inRow; borderSlot++ )
		{
			const Int x = right - trayW + trayHole.x - borderSlot * trayStep;
			TheDisplay->drawOpenRect( x, y, cameoW, cameoH, 2.0f, GameMakeColor( red, green, blue, 255 ) );
		}
	}

	TheDisplay->endBatch2D();
}

//-------------------------------------------------------------------------------------------------
/** The lobby's own team label, the one the diplomacy screen wore. */
//-------------------------------------------------------------------------------------------------
static UnicodeString scoreboardTeamLabel( const GameSlot *slot )
{
	AsciiString teamLabel;
	teamLabel.format( "Team:%d", slot->getTeamNumber() + 1 );
	if( slot->isAI() && slot->getTeamNumber() == -1 )
		teamLabel = "Team:AI";
	return TheGameText->fetch( teamLabel );
}

static const char *const SCOREBOARD_PAGE = "Window\\Html\\Scoreboard.html";
enum { SCOREBOARD_SKILLS_SHOWN = 7 };	///< promotions on one row, each once at the level it has reached

//-------------------------------------------------------------------------------------------------
/** Every player's money earned so far, read on the first pass of each game second.  Reading the
	* score keeper is all it does, so nothing here reaches the logic. */
//-------------------------------------------------------------------------------------------------
void InGameUI::sampleEarnings( void )
{
	if( !TheGameLogic->isInGame() || TheGameLogic->isInShellGame() )
		return;

	const UnsignedInt second = TheGameLogic->getFrame() / LOGICFRAMES_PER_SECOND;
	if( m_earnedReadingCount > 0 && second == m_earnedReadings[ m_earnedReadingCount - 1 ].second )
		return;

	// full, the oldest reading drops off the front
	if( m_earnedReadingCount == EARNINGS_READINGS )
		std::copy( m_earnedReadings + 1, m_earnedReadings + EARNINGS_READINGS, m_earnedReadings );
	else
		m_earnedReadingCount++;

	EarnedReading &newest = m_earnedReadings[ m_earnedReadingCount - 1 ];
	newest.second = second;
	for( Int index = 0; index < ThePlayerList->getPlayerCount(); index++ )
		newest.earned[ index ] = ThePlayerList->getNthPlayer( index )->getScoreKeeper()->getTotalMoneyEarned();
}

//-------------------------------------------------------------------------------------------------
/** Money a player earned a second between the oldest reading held and the newest, rounded; 0
	* until there are two.  It is shared over the whole half minute even before the readings reach
	* back that far, so the first truck of a match reads as a trickle rather than a fortune. */
//-------------------------------------------------------------------------------------------------
Int InGameUI::earnedPerSecond( Int playerIndex ) const
{
	if( m_earnedReadingCount < 2 )
		return 0;

	const EarnedReading &oldest = m_earnedReadings[ 0 ];
	const EarnedReading &newest = m_earnedReadings[ m_earnedReadingCount - 1 ];
	const Int seconds = max( (Int)( newest.second - oldest.second ), (Int)EARNINGS_WINDOW_SECONDS );
	return ( newest.earned[ playerIndex ] - oldest.earned[ playerIndex ] + seconds / 2 ) / seconds;
}

/** One seat on the scoreboard page.  `full` is whether the local player may see the numbers: his
	* own side, or everybody when watching.  An enemy is a name, a colour and a team.  `perSecond` is
	* what the seat has earned a second lately, beside {{income}}, its average over the match. */
static HtmlValues scoreboardSeat( Player *player, const GameSlot *slot, Bool full, Bool own, Int perSecond )
{
	const PlayerTemplate *side = player->getPlayerTemplate();
	const Image *portrait = side ? side->getEnabledImage() : NULL;

	HtmlValues seat;
	seat[ "kind" ] = "seat";
	seat[ "name" ] = WideCharStringToMultiByte( slot->getName().str() );
	seat[ "color" ] = cssColor( clientPlayerColor( player ) );
	seat[ "team" ] = WideCharStringToMultiByte( scoreboardTeamLabel( slot ).str() );
	seat[ "state" ] = std::string( full ? "full" : "hidden" ) + ( own ? " own" : "" )
									+ ( TheVictoryConditions->hasSinglePlayerBeenDefeated( player ) ? " defeated" : "" );
	if( !full )
		return seat;

	seat[ "portrait" ] = portrait ? portrait->getName().str() : "";
	seat[ "general" ] = side ? WideCharStringToMultiByte( side->getDisplayName().str() ) : "";

	ScoreKeeper *score = player->getScoreKeeper();
	const UnsignedInt frame = TheGameLogic->getFrame();
	seat[ "rank" ] = std::to_string( player->getRankLevel() );
	seat[ "cash" ] = std::to_string( player->getMoney()->countMoney() );
	seat[ "income" ] = std::to_string( frame > 0 ? (Int)( (Int64)score->getTotalMoneyEarned() * FRAMES_PER_MINUTE / frame ) : 0 );
	seat[ "persecond" ] = std::to_string( perSecond );
	seat[ "kills" ] = std::to_string( score->getTotalUnitsDestroyed() + score->getTotalBuildingsDestroyed() );
	seat[ "losses" ] = std::to_string( score->getTotalUnitsLost() + score->getTotalBuildingsLost() );

	const CommandButton *skills[ SCOREBOARD_SKILLS_SHOWN ];
	const Int skillCount = gatherPlayerSkills( player, skills, 0, SCOREBOARD_SKILLS_SHOWN );
	for( Int skill = 0; skill < skillCount; skill++ )
	{
		const std::string name = "skill" + std::to_string( skill );
		seat[ name ] = skills[ skill ]->getButtonImage()->getName().str();
		seat[ name + ".tip" ] = buttonTip( skills[ skill ], player );
	}

	const ThingTemplate *favourite = score->getMostBuiltUnit();
	if( favourite && favourite->getButtonImage() )
	{
		seat[ "favourite" ] = favourite->getButtonImage()->getName().str();
		seat[ "favouritename" ] = WideCharStringToMultiByte( favourite->getDisplayName().str() );
		seat[ "favourite.tip" ] = buttonTip( buildButtonForThing( favourite ), player );
	}
	return seat;
}

//-------------------------------------------------------------------------------------------------
/** One scoreboard seat's superweapons, the SEAT_SUPERWEAPONS soonest side by side as {{wN.image}}
	* {{wN.time}} and {{wN.state}}: "ready" once it can fire, "none" for an unused place.  The rest
	* are counted, {{more}} of them with {{moremark}} "off" when there are none, and {{moreready}}
	* of those ready with {{morereadymark}} "off" when none is.  {{armed}} is "armed" when the
	* player has any. */
//-------------------------------------------------------------------------------------------------
static void putSeatSuperweapons( HtmlValues &row, Int playerIndex, const std::vector< SpectatorSuperweapon > &weapons )
{
	enum { SEAT_SUPERWEAPONS = 5 };

	std::vector< SpectatorSuperweapon > owned;
	for( size_t weapon = 0; weapon < weapons.size(); weapon++ )
		if( weapons[ weapon ].playerIndex == playerIndex )
			owned.push_back( weapons[ weapon ] );
	std::stable_sort( owned.begin(), owned.end(),
										[]( const SpectatorSuperweapon &a, const SpectatorSuperweapon &b ) { return a.seconds < b.seconds; } );

	row[ "armed" ] = owned.empty() ? "" : "armed";
	for( Int place = 0; place < SEAT_SUPERWEAPONS; place++ )
	{
		const std::string name = "w" + std::to_string( place );
		if( place >= (Int)owned.size() )
		{
			row[ name + ".state" ] = "none";
			continue;
		}

		UnicodeString time;
		formatStripSeconds( &time, owned[ place ].seconds );
		row[ name + ".image" ] = owned[ place ].cameo ? owned[ place ].cameo->getName().str() : "";
		row[ name + ".time" ] = WideCharStringToMultiByte( time.str() );
		row[ name + ".state" ] = owned[ place ].ready ? "ready" : "";
		row[ name + ".tip" ] = buttonTip( owned[ place ].button, ThePlayerList->getNthPlayer( playerIndex ) );
	}

	Int more = 0;
	Int moreReady = 0;
	for( size_t weapon = SEAT_SUPERWEAPONS; weapon < owned.size(); weapon++ )
	{
		more++;
		if( owned[ weapon ].ready )
			moreReady++;
	}
	row[ "more" ] = std::to_string( more );
	row[ "moremark" ] = more > 0 ? "" : "off";
	row[ "moreready" ] = std::to_string( moreReady );
	row[ "morereadymark" ] = moreReady > 0 ? "" : "off";
}

//-------------------------------------------------------------------------------------------------
/** One scoreboard seat's production while watching, what the left edge of the screen used to carry:
	* the SEAT_JOBS soonest things coming, queued in a factory or going up on the ground, as
	* {{jN.image}} {{jN.time}} {{jN.count}} ("x5" for a run of the same thing) and {{jN.state}}, "none"
	* for an unused place.  {{jobsmore}} counts the rest, with {{jobsmoremark}} "off" when there are
	* none.  {{watching}} is "watching" on every seat, so the line keeps its place with nothing coming
	* and the superweapons after it stay put as a queue empties and fills. */
//-------------------------------------------------------------------------------------------------
static void putSeatQueue( HtmlValues &row, Player *player )
{
	enum { SEAT_JOBS = 3 };	///< the line's head; its superweapons follow on the same line

	InGameUI::ProductionStripSlot slots[ SEAT_JOBS ];
	Int count = 0;
	Int total = 0;
	ProductionStripGather gather;
	gather.slot = slots;
	gather.count = &count;
	gather.total = &total;
	gather.max = SEAT_JOBS;
	gather.skip = INVALID_ID;
	player->iterateObjects( gatherStripEverything, &gather );

	Int shown = 0;
	for( Int place = 0; place < SEAT_JOBS; place++ )
	{
		const std::string name = "j" + std::to_string( place );
		if( place >= count )
		{
			row[ name + ".state" ] = "none";
			continue;
		}

		const InGameUI::ProductionStripSlot *slot = &slots[ place ];
		Object *producer = TheGameLogic->findObjectByID( slot->producer );
		const ProductionEntry *entry = producer && !slot->isStructure
																	 ? findStripEntry( producer->getProductionUpdateInterface(), slot ) : NULL;
		const Image *cameo = stripSlotCameo( producer, entry, slot );
		UnicodeString time;
		formatStripSeconds( &time, ControlBar_secondsFromFrames( (Real)slot->remaining ) );
		row[ name + ".image" ] = cameo ? cameo->getName().str() : "";
		row[ name + ".time" ] = WideCharStringToMultiByte( time.str() );
		row[ name + ".count" ] = slot->quantity > 1 ? "x" + std::to_string( slot->quantity ) : "";
		row[ name + ".state" ] = "";
		row[ name + ".tip" ] = buttonTip( stripSlotButton( producer, entry, slot ), player );
		shown += slot->quantity;
	}
	row[ "jobsmore" ] = std::to_string( total - shown );
	row[ "jobsmoremark" ] = total > shown ? "" : "off";
	row[ "watching" ] = "watching";
}

//-------------------------------------------------------------------------------------------------
/** The scoreboard on Tab, Window/Html/Scoreboard.html, docked to the left the way Dota docks its
	* own.  A player sees two sections, his side in full and the enemy as names and teams, because
	* the enemy's general, money and promotions are for its own side to know.  A watcher, an observer
	* or a player knocked out who stayed, sees one section a team, every seat in full and with its
	* production, which is the left edge's queue column he no longer has.  Every section opens with a
	* band of kind "band" carrying its {{label}}, how many seats are {{standing}} of {{seats}}, and
	* {{side}} "allies", "enemies" or "team". */
//-------------------------------------------------------------------------------------------------
void InGameUI::drawScoreboard( void )
{
	TheControlBar->hideBoardCard();
	if( !m_scoreboardOpen || TheGameInfo == NULL )
		return;
	if( TheGameLogic == NULL || !TheGameLogic->isInGame() || TheGameLogic->isInShellGame() )
		return;

	if( !m_scoreboardPageLoaded )
	{
		m_scoreboardPageLoaded = TRUE;
		readHtmlPage( SCOREBOARD_PAGE, m_scoreboardPage );
	}
	if( m_scoreboardPage.empty() )
		return;
	if( m_scoreboardOverlay == NULL )
		m_scoreboardOverlay = new HtmlOverlay( m_superweaponNormalFont );

	// counted again twice a second, as the spectator page is: the money moves every frame, and every
	// change lays the whole page out again
	const UnsignedInt frame = TheGameLogic->getFrame();
	if( m_scoreboardHtml.empty() || frame < m_scoreboardHtmlFrame || frame >= m_scoreboardHtmlFrame + NET_WORTH_REFRESH_FRAMES )
	{
		m_scoreboardHtml = scoreboardHtml();
		m_scoreboardHtmlFrame = frame;
	}
	m_scoreboardOverlay->setPage( m_scoreboardHtml );
	// a promotion, a cameo or the favourite unit under the pointer gets the command bar's build card,
	// its health, damage and range; the board hides the battlefield's tooltips under it
	if( m_scoreboardOverlay->hover( TheMouse->getMouseStatus()->pos ) )
	{
		TheMouse->setCursorTooltip( UnicodeString::TheEmptyString );
		IRegion2D anchor;
		const std::string tip = m_scoreboardOverlay->tip( anchor );
		if( !tip.empty() )
		{
			// counted for the row's player: his prices, and his upgrades in the damage and the health
			const size_t space = tip.find( ' ' );
			TheControlBar->showBoardCard( TheControlBar->findCommandButton( AsciiString( tip.c_str() + space + 1 ) ),
																		ThePlayerList->getNthPlayer( atoi( tip.c_str() ) ), anchor );
		}
	}
	m_scoreboardOverlay->draw();
}

//-------------------------------------------------------------------------------------------------
/** Window/Html/Scoreboard.html filled in with every seat as it stands this frame. */
//-------------------------------------------------------------------------------------------------
std::string InGameUI::scoreboardHtml( void )
{
	Player *local = ThePlayerList->getLocalPlayer();
	const Bool watching = localPlayerWatching();

	struct Seat
	{
		Player *player;
		const GameSlot *slot;
		Int section;		///< 0 your side and 1 the enemy's, or the lobby team when watching
	};
	std::vector< Seat > seats;
	for( Int slotNum = 0; slotNum < MAX_SLOTS; slotNum++ )
	{
		const GameSlot *slot = TheGameInfo->getConstSlot( slotNum );
		if( !slot->isOccupied() )
			continue;

		AsciiString playerName;
		playerName.format( "player%d", slotNum );
		Player *player = ThePlayerList->findPlayerWithNameKey( NAMEKEY( playerName ) );
		if( player == NULL || player->isPlayerObserver() )
			continue;

		const Bool allied = player == local || local->getRelationship( player->getDefaultTeam() ) == ALLIES;
		Seat seat = { player, slot, watching ? slot->getTeamNumber() : ( allied ? 0 : 1 ) };
		seats.push_back( seat );
	}
	std::stable_sort( seats.begin(), seats.end(), []( const Seat &a, const Seat &b ) { return a.section < b.section; } );

	HtmlLists lists;
	std::vector< HtmlValues > &rows = lists[ "seats" ];
	for( size_t first = 0; first < seats.size(); )
	{
		size_t end = first;
		Int standing = 0;
		for( ; end < seats.size() && seats[ end ].section == seats[ first ].section; end++ )
			if( !TheVictoryConditions->hasSinglePlayerBeenDefeated( seats[ end ].player ) )
				standing++;

		HtmlValues band;
		band[ "kind" ] = "band";
		band[ "standing" ] = std::to_string( standing );
		band[ "seats" ] = std::to_string( end - first );
		if( watching )
		{
			// a free for all is one section of players with no team between them
			UnicodeString label = TheGameText->fetch( "GUI:ScoreboardPlayer" );
			if( seats[ first ].section >= 0 )
				label.format( L"%s %s", TheGameText->fetch( "GUI:ScoreboardTeam" ).str(), scoreboardTeamLabel( seats[ first ].slot ).str() );
			band[ "side" ] = "team";
			band[ "label" ] = WideCharStringToMultiByte( label.str() );
		}
		else
		{
			band[ "side" ] = seats[ first ].section == 0 ? "allies" : "enemies";
			band[ "label" ] = WideCharStringToMultiByte( TheGameText->fetch( seats[ first ].section == 0 ? "GUI:ScoreboardAllies"
																																																	: "GUI:ScoreboardEnemies" ).str() );
		}
		rows.push_back( band );

		for( ; first < end; first++ )
		{
			HtmlValues row = scoreboardSeat( seats[ first ].player, seats[ first ].slot, watching || seats[ first ].section == 0,
																			 seats[ first ].player == local, earnedPerSecond( seats[ first ].player->getPlayerIndex() ) );
			putSeatSuperweapons( row, seats[ first ].player->getPlayerIndex(), m_spectatorSuperweapons );
			if( watching )
				putSeatQueue( row, seats[ first ].player );
			rows.push_back( row );
		}
	}

	HtmlValues values;
	values[ "side" ] = spectatorSide();
	values[ "clock" ] = spectatorClock( TheGameLogic->getFrame() );
	return HtmlTemplate_expand( m_scoreboardPage, values, lists, lookupGameText );
}

static const char *const CONTROL_BAR_PAGE = "Window\\Html\\ControlBar.html";
static const char *const PROMOTION_PAGE = "Window\\Html\\Promotion.html";
static const char *const QUEUE_PAGE = "Window\\Html\\Queue.html";
static void standDownPromotionScreen( void );

/** The command bar's windows the page is told the place of, by their name in ControlBar.wnd. */
static const char *const CONTROL_BAR_WINDOWS[] =
{
	"LeftHUD", "RightHUD", "CameoWindow", "CommandWindow", "MoneyDisplay", "PowerWindow", "GeneralsExp",
	"ButtonGeneral", "ButtonLarge", "ButtonOptions", "ButtonIdleWorker", "PopupCommunicator",
	"WinUAttack"
};

/** The bar's windows the page draws instead of letting them paint themselves: the promotion and
	* minimise buttons and the radar's under-attack light.  They still take their clicks; only their
	* pictures are the page's. */
static const char *const CONTROL_BAR_CUSTOM[] =
{
	"ButtonGeneral", "ButtonLarge", "WinUAttack", "PowerWindow", "GeneralsExp", "ExpBarForeground", "RightHUD",
	"WinUnitSelected"
};

/** How far along its scale a power figure reaches, 0 to 1: the power bar's own logarithmic scale,
	* TheGlobalData's base and intervals, the one W3DPowerDraw measured with. */
static Real powerBarShare( Real power )
{
	if( power <= 1.0f || TheGlobalData->m_powerBarBase <= 1 || TheGlobalData->m_powerBarIntervals <= 0.0f )
		return 0.0f;
	const Real share = logf( power ) / logf( (Real)TheGlobalData->m_powerBarBase ) / TheGlobalData->m_powerBarIntervals;
	return share > 1.0f ? 1.0f : share;
}

//-------------------------------------------------------------------------------------------------
/** The power bar as the page draws it, filling its frame: {{power.fill}} the production and
	* {{power.needle}} the consumption, both percent of the frame, and {{power.state}} "green",
	* "yellow" or "red" by the bar's own rule - red once consumption passes production, yellow within
	* m_powerBarYellowRange of it.  Watching, it is the watched player's power.  `cells` is the same
	* production as a row of POWER_CELLS cells, each {{lit}} "lit" up to it, for a segmented bar. */
//-------------------------------------------------------------------------------------------------
static void putPowerBar( HtmlValues &values, std::vector< HtmlValues > &cells )
{
	enum
	{
		ONE_UNIT_NEEDLE_TENTHS	= 15,	///< a consumption of 1 is drawn as 1.5: log(1) is 0, and 1 is not nothing
		POWER_CELLS							= 40	///< the bar is drawn as this many cells, lit up to the production
	};

	Player *player = TheControlBar->isObserverControlBarOn() ? TheControlBar->getObserverLookAtPlayer()
																												 : ThePlayerList->getLocalPlayer();
	const Energy *energy = player ? player->getEnergy() : NULL;
	const Int production = energy ? energy->getProduction() : 0;
	const Int consumption = energy ? energy->getConsumption() : 0;
	const Real fill = powerBarShare( (Real)production );
	const Real needle = consumption == 1 ? ONE_UNIT_NEEDLE_TENTHS / 10.0f : (Real)consumption;
	values[ "power.fill" ] = std::to_string( REAL_TO_INT( fill * PERCENT ) );
	values[ "power.needle" ] = std::to_string( REAL_TO_INT( powerBarShare( needle ) * PERCENT ) );
	// with nothing drawing power the needle stood at nought, a white bar across the bar's head
	values[ "power.consumes" ] = consumption > 0 ? "" : "hidden";

	// each cell gets its place and width in the page's pixels, cut from the frame's inside so they
	// add up to it exactly: floated at a percentage each the page rounded them up and the fortieth
	// fell onto a second row, and a percentage of an absolutely placed box does not resolve at all
	enum { FRAME_LIP = 1 };
	const Int row = atoi( values[ "powerframe.w" ].c_str() ) - 2 * FRAME_LIP;
	cells.clear();
	const Int lit = REAL_TO_INT( fill * POWER_CELLS );
	for( Int cell = 0; cell < POWER_CELLS && row > 0; cell++ )
	{
		const Int left = cell * row / POWER_CELLS;
		HtmlValues entry;
		entry[ "lit" ] = cell < lit ? "lit" : "";
		entry[ "x" ] = std::to_string( left );
		entry[ "w" ] = std::to_string( ( cell + 1 ) * row / POWER_CELLS - left );
		cells.push_back( entry );
	}
	if( consumption > production )
		values[ "power.state" ] = "red";
	else if( consumption > production - TheGlobalData->m_powerBarYellowRange )
		values[ "power.state" ] = "yellow";
	else
		values[ "power.state" ] = "green";
}

/** How far `player` is from this rank to the next, 0 to 100.  A script can disable a level, which
	* leaves its points required at -1: a rank with no way on counts as full, where the bar's own
	* drawing divided by it. */
static Int experiencePercent( const Player *player )
{
	enum { FULL = 100 };
	const Int span = player->getSkillPointsLevelUp() - player->getSkillPointsLevelDown();
	const Int progress = span > 0 ? ( player->getSkillPoints() - player->getSkillPointsLevelDown() ) * FULL / span : FULL;
	return min( (Int)FULL, max( 0, progress ) );
}

//-------------------------------------------------------------------------------------------------
/** The general's experience as the page draws it, in the groove {{expframe.x}} ... puts down the
	* right panel: `cells` from the bottom up, each {{lit}} "lit" up to the way from this rank to the
	* next, at {{y}} and {{h}} pixels inside the lip, and `stars` one per rank, {{lit}} up to the rank
	* reached.  The groove's border goes on top of its size in the page, so expframe.w and .h are cut
	* to its inside here.  Watching, it is the watched player's. */
//-------------------------------------------------------------------------------------------------
static void putExperienceBar( HtmlValues &values, std::vector< HtmlValues > &cells, std::vector< HtmlValues > &stars )
{
	enum { FRAME_BORDER = 2, FRAME_LIP = 1, EXPERIENCE_CELLS = 10, FULL = 100 };

	const Int width = atoi( values[ "expframe.w" ].c_str() ) - 2 * FRAME_BORDER;
	const Int height = atoi( values[ "expframe.h" ].c_str() ) - 2 * FRAME_BORDER;
	values[ "expframe.w" ] = std::to_string( max( 0, width ) );
	values[ "expframe.h" ] = std::to_string( max( 0, height ) );
	values[ "expframe.innerw" ] = std::to_string( max( 0, width - 2 * FRAME_LIP ) );

	const Player *player = TheControlBar->isObserverControlBarOn() ? TheControlBar->getObserverLookAtPlayer()
																																 : ThePlayerList->getLocalPlayer();
	cells.clear();
	stars.clear();
	if( player == NULL )
		return;

	const Int lit = experiencePercent( player ) * EXPERIENCE_CELLS / FULL;
	const Int column = height - 2 * FRAME_LIP;
	for( Int cell = 0; cell < EXPERIENCE_CELLS && column > 0; cell++ )
	{
		// cell 0 is the bottom one; each is cut from the column so they add up to it exactly, and its
		// segment is a pixel shorter, the black line over it
		enum { CELL_GAP = 1 };
		const Int top = column - ( cell + 1 ) * column / EXPERIENCE_CELLS;
		const Int cellHeight = column - cell * column / EXPERIENCE_CELLS - top;
		HtmlValues entry;
		entry[ "lit" ] = cell < lit ? "lit" : "";
		entry[ "y" ] = std::to_string( top );
		entry[ "h" ] = std::to_string( cellHeight );
		entry[ "segh" ] = std::to_string( max( 0, cellHeight - CELL_GAP ) );
		cells.push_back( entry );
	}

	// the stars stand at pixel places centred on the key: centred as a line of text, the page measured
	// the star glyph wider than it drew it and the row sat to the left
	enum { RANK_KEY_WIDTH = 58, STAR_PITCH = 11, STAR_SIZE = 10 };
	const Int rankCount = TheRankInfoStore->getRankLevelCount();
	const Int firstStar = ( RANK_KEY_WIDTH - ( rankCount * STAR_PITCH - ( STAR_PITCH - STAR_SIZE ) ) ) / 2;
	for( Int rank = 1; rank <= rankCount; rank++ )
	{
		HtmlValues entry;
		entry[ "lit" ] = rank <= player->getRankLevel() ? "lit" : "";
		entry[ "x" ] = std::to_string( firstStar + ( rank - 1 ) * STAR_PITCH );
		stars.push_back( entry );
	}
}

/** The bar's windows the page stands down altogether.  The menu and idle worker buttons are the
	* page's own, pressed through data-click="press:Name" because they sit where the bar's frame takes
	* no clicks; the chat button is gone, and Enter still opens the chat; the minimise button is gone,
	* the bar is only as big as what it holds now. */
static const char *const CONTROL_BAR_STOOD_DOWN[] = { "ButtonOptions", "ButtonIdleWorker", "PopupCommunicator", "ButtonLarge" };
static const std::string PRESS_ACTION = "press:";
static const std::string SIGNAL_ACTION = "signal:";

/** A smoke signal may be sent: the rule the Alt+Z/X/C keys go by, a multiplayer game that is not a
	* replay, and a player still in it. */
static Bool signalsAllowed( void )
{
	const Player *local = ThePlayerList ? ThePlayerList->getLocalPlayer() : NULL;
	return TheGameLogic->isInMultiplayerGame() && !TheGameLogic->isInReplayGame() && local && local->isPlayerActive();
}

//-------------------------------------------------------------------------------------------------
/** data-click="signal:attack", "signal:defend" or "signal:look": arms the smoke the Alt+Z/X/C keys
	* send, and the next left click on the ground or the radar drops it there; the right button or
	* Escape takes it back. */
//-------------------------------------------------------------------------------------------------
static void armSignalFromPage( const std::string &kind )
{
	static const struct { const char *name; SignalKind kind; } KINDS[] =
	{
		{ "attack", SIGNAL_ATTACK }, { "defend", SIGNAL_DEFEND }, { "look", SIGNAL_ATTENTION }
	};

	if( !signalsAllowed() )
		return;
	for( Int each = 0; each < (Int)ARRAY_SIZE( KINDS ); each++ )
	{
		if( kind != KINDS[ each ].name )
			continue;

		TheInGameUI->armSignal( KINDS[ each ].kind );
		return;
	}
	DEBUG_LOG(( "Command bar page: data-click=\"signal:%s\" names no signal\n", kind.c_str() ));
}

/** The windows each panel is drawn round, so a panel is only as big as what it holds: the left one
	* the radar, the right one the portrait and the experience bar, the centre the command grid.  The
	* money stands on a step of its own over the centre, drawn from its window's rectangle, and the
	* power bar crosses the gap between the two in its groove with no steel of its own.  NULL ends
	* each list. */
static const char *const CONTROL_BAR_LEFT[] = { "LeftHUD", NULL };
static const char *const CONTROL_BAR_RIGHT[] = { "RightHUD", "GeneralsExp", "ExpBarForeground", NULL };
static const char *const CONTROL_BAR_CENTRE[] = { "ObserverPlayerListWindow", NULL };
static const char *const CONTROL_BAR_EXPERIENCE[] = { "GeneralsExp", "ExpBarForeground", NULL };
static const Int COMMAND_BUTTONS = 14;	///< ButtonCommand01 to 14, the grid a player sees
static const Int QUEUE_BUTTONS = 9;			///< ButtonQueue01 to 09, the production queue's three by three over the portrait's place

/** The pieces standing round the panels, 800x600 pixels. */
enum
{
	STARS_TAB_WIDTH				= 66,		///< the rank's stars, a key in a tab on the right panel's border, which is the promotion button
	STARS_TAB_HEIGHT			= 22,		///< the key 17 tall with its rim, three pixels down
	SKILL_GRID_GAP				= 12,		///< between the general's powers' tray and the stars' tab under it
	SKILL_TRAY_BORDER			= 6,		///< the tray's steel round its cells
	QUEUE_TRAY_BORDER			= 3,		///< the production queue's tray's, thinner, since the row runs over the battlefield
	SKILL_CELL_GAP				= 2,		///< the steel between two cells
	SKILL_CELL_PERCENT		= 75,		///< a cell's size against a command button's; the whole size stood too big over the portrait
	CELL_GAP_LEAST				= 2,		///< the least steel between two pictures of a grid, a bevel each side
	SIGNAL_BUTTON_SIZE		= 24,		///< each smoke signal button's height, a row of the column
	SIGNAL_BUTTON_WIDTH		= 40,		///< and its width, room for its picture
	SIGNAL_BUTTONS				= 3,		///< attack, defend, look
	ALERT_TAB_WIDTH				= 54,		///< the under-attack light, a lamp on the radar panel's border
	WORKER_TAB_WIDTH			= 39,		///< the idle worker's step, against the command grid panel's border, as wide as the signals' column
	WORKER_STEP_HEIGHT		= 30,		///< and its height, its key's bottom GRID_BOTTOM_GAP over the screen's bottom edge
	GRID_BOTTOM_GAP				= 6			///< the command buttons' bottom over the screen's bottom edge, level with that key's:
																		///< the grid's two pixel well and four of steel under it; at 4 the well's lit
																		///< bottom edge sat on the screen's last row and the grid looked cut off
};
static const UnsignedInt SIGNAL_RISE_MS = 360;					///< each smoke signal button's climb out of the screen's bottom edge
static const UnsignedInt SIGNAL_RISE_STAGGER_MS = 90;	///< between one button starting and the next

/** `signalN.drop`, N 0 to 2, how far below its place each smoke signal button still is, in page
	* pixels, `elapsedMs` after the column came up: each starts a stagger after the one above it and
	* slows as it arrives, the whole column's height to nothing. */
static void putSignalRise( HtmlValues &values, UnsignedInt elapsedMs )
{
	for( Int button = 0; button < SIGNAL_BUTTONS; button++ )
	{
		const Real started = (Real)elapsedMs - (Real)( button * SIGNAL_RISE_STAGGER_MS );
		const Real progress = min( 1.0f, max( 0.0f, started / SIGNAL_RISE_MS ) );
		const Real remaining = ( 1.0f - progress ) * ( 1.0f - progress ) * ( 1.0f - progress );
		values[ "signal" + std::to_string( button ) + ".drop" ] = std::to_string( REAL_TO_INT( remaining * SIGNAL_BUTTON_SIZE * SIGNAL_BUTTONS ) );
	}
}

static void drawNothing( GameWindow *window, WinInstanceData *instData )
{
}

static GameWindow *controlBarWindow( const std::string &name )
{
	return TheWindowManager->winGetWindowFromId( NULL, NAMEKEY( ( "ControlBar.wnd:" + name ).c_str() ) );
}

/** A window's screen rectangle, and FALSE when it is missing or hidden. */
static Bool controlBarWindowRect( GameWindow *window, IRegion2D &rect )
{
	rect.lo.x = rect.lo.y = rect.hi.x = rect.hi.y = 0;
	if( window == NULL || window->winIsHidden() )
		return FALSE;

	Int width = 0, height = 0;
	window->winGetScreenPosition( &rect.lo.x, &rect.lo.y );
	window->winGetSize( &width, &height );
	rect.hi.x = rect.lo.x + width;
	rect.hi.y = rect.lo.y + height;
	return TRUE;
}

/** "disabled", "hilite" and "pushed" as they apply, for a page to draw a button's state by. */
static std::string controlBarWindowState( GameWindow *window )
{
	std::string state;
	if( window == NULL )
		return state;
	if( !BitTest( window->winGetStatus(), WIN_STATUS_ENABLED ) )
		state += " disabled";
	const UnsignedInt flags = window->winGetInstanceData()->getState();
	if( BitTest( flags, WIN_STATE_HILITED ) )
		state += " hilite";
	if( BitTest( flags, WIN_STATE_SELECTED ) )
		state += " pushed";
	return state;
}

/** The box round every shown window of a NULL-ended list, and FALSE when none of them is shown. */
static Bool controlBarUnion( const char *const *names, IRegion2D &box )
{
	Bool found = FALSE;
	box.lo.x = box.lo.y = box.hi.x = box.hi.y = 0;
	for( ; *names; names++ )
	{
		IRegion2D rect;
		if( !controlBarWindowRect( controlBarWindow( *names ), rect ) )
			continue;
		if( !found )
			box = rect;
		box.lo.x = min( box.lo.x, rect.lo.x );
		box.lo.y = min( box.lo.y, rect.lo.y );
		box.hi.x = max( box.hi.x, rect.hi.x );
		box.hi.y = max( box.hi.y, rect.hi.y );
		found = TRUE;
	}
	return found;
}

/** `box` grown by so many 800x600 pixels on each side, in screen pixels. */
static IRegion2D grownBy( const IRegion2D &box, Int left, Int top, Int right, Int bottom )
{
	const Real scale = ControlBarUniformScale();
	IRegion2D grown;
	grown.lo.x = box.lo.x - REAL_TO_INT( left * scale );
	grown.lo.y = box.lo.y - REAL_TO_INT( top * scale );
	grown.hi.x = box.hi.x + REAL_TO_INT( right * scale );
	grown.hi.y = box.hi.y + REAL_TO_INT( bottom * scale );
	return grown;
}

static void putPageRect( HtmlValues &values, const std::string &name, const IRegion2D &rect, Bool shown );

/** How much steel each block of the centre stack shows round its window, 800x600 pixels. */
enum
{
	PANEL_BORDER				= 8,	///< a panel's border outside its container, on the sides facing the battlefield
	STACK_FRAME					= 3,	///< the power bar's frame and lip
	STACK_POWER_TOP			= 5,	///< the power bar's block over its frame
	STACK_MONEY_SIDE		= 8,
	STACK_MONEY_TOP			= 1,	///< over the money's line of text
	PANEL_TAB_HEIGHT		= 14	///< a tab or button standing on a border's top edge
};

/** The border round a container: `border` screen pixels on the sides facing the battlefield, out to
	* the screen's edge on the sides against it - the bottom always, the left or the right as asked. */
/** The screen's bottom edge as the bar stands on it: while the match's intro slides the bar up from
	* below, its frame is under where layoutPanels put it, and everything standing on the edge goes
	* down with it, or the panels waited at the bottom while only the buttons rose. */
static Int barBottom( void )
{
	Int frameX = 0, frameY = 0;
	TheControlBar->getMasterParent()->winGetScreenPosition( &frameX, &frameY );
	return TheDisplay->getHeight() + frameY - TheControlBar->getPanelOrigin()->y;
}

static IRegion2D framed( const IRegion2D &content, Int border, Bool againstLeft, Bool againstRight )
{
	IRegion2D box;
	box.lo.x = againstLeft ? 0 : content.lo.x - border;
	box.lo.y = content.lo.y - border;
	box.hi.x = againstRight ? TheDisplay->getWidth() : content.hi.x + border;
	box.hi.y = barBottom();
	return box;
}

/** A panel for the page: `name`.x and .y the border's outer corner, .w and .h the container, and
	* .bt .br .bb .bl the border's four widths, all in the page's pixels, every edge rounded once so
	* the container fits what it holds exactly; `name`.shown as putPageRect writes it. */
static void putFrame( HtmlValues &values, const std::string &name, const IRegion2D &content, const IRegion2D &box, Bool shown )
{
	const Real scale = ControlBarUniformScale();
	struct Edge { static Int page( Int screen, Real scale ) { return REAL_TO_INT_FLOOR( screen / scale + 0.5f ); } };
	const Int left = Edge::page( box.lo.x, scale ), top = Edge::page( box.lo.y, scale );
	const Int innerLeft = Edge::page( content.lo.x, scale ), innerTop = Edge::page( content.lo.y, scale );
	const Int innerRight = Edge::page( content.hi.x, scale ), innerBottom = Edge::page( content.hi.y, scale );
	const Int right = Edge::page( box.hi.x, scale ), bottom = Edge::page( box.hi.y, scale );

	values[ name + ".x" ] = std::to_string( left );
	values[ name + ".y" ] = std::to_string( top );
	values[ name + ".w" ] = std::to_string( shown ? max( 0, innerRight - innerLeft ) : 0 );
	values[ name + ".h" ] = std::to_string( shown ? max( 0, innerBottom - innerTop ) : 0 );
	values[ name + ".bl" ] = std::to_string( max( 0, innerLeft - left ) );
	values[ name + ".bt" ] = std::to_string( max( 0, innerTop - top ) );
	values[ name + ".br" ] = std::to_string( max( 0, right - innerRight ) );
	values[ name + ".bb" ] = std::to_string( max( 0, bottom - innerBottom ) );
	values[ name + ".shown" ] = shown ? "shown" : "hidden";
}

/** A tab of `width` by `height` 800x600 pixels standing on `box`'s top edge, from its left or its right. */
static IRegion2D tabOn( const IRegion2D &box, Int width, Int height, Bool fromRight )
{
	const Real scale = ControlBarUniformScale();
	IRegion2D tab;
	tab.hi.y = box.lo.y;
	tab.lo.y = tab.hi.y - REAL_TO_INT( height * scale );
	tab.lo.x = fromRight ? box.hi.x - REAL_TO_INT( width * scale ) : box.lo.x;
	tab.hi.x = fromRight ? box.hi.x : box.lo.x + REAL_TO_INT( width * scale );
	return tab;
}

//-------------------------------------------------------------------------------------------------
/** The centre of the bar as three blocks stacked flush, each narrower than the one under it: the
	* command grid's panel, the power bar in its frame on top of that, and the money's block on top of
	* the frame.  The windows stay where the bar put them; each block reaches down to the next, so
	* there is no gap between them, and a block whose window is hidden is left out and the one above
	* it sits on the one below.  The money's window alone is moved: it is cut to the height of its
	* line of text and set down on the block under it, so its block is no taller than the figure.
	* Written as centre, powerframe and moneyblock. */
//-------------------------------------------------------------------------------------------------
static GameWindow *numberedWindow( const char *prefix, Int number )
{
	char name[ 32 ];
	snprintf( name, sizeof( name ), "%s%02d", prefix, number );
	return controlBarWindow( name );
}

/** `place` grown by `across` screen pixels on the left and the right and `down` on the top and the
	* bottom. */
static IRegion2D reachedBy( const IRegion2D &place, Int across, Int down )
{
	IRegion2D cell = place;
	cell.lo.x -= across;
	cell.lo.y -= down;
	cell.hi.x += across;
	cell.hi.y += down;
	return cell;
}

/** The screen rectangle of one of the bar's windows as it stands now. */
static IRegion2D windowScreenRect( GameWindow *window )
{
	IRegion2D rect;
	Int width = 0, height = 0;
	window->winGetScreenPosition( &rect.lo.x, &rect.lo.y );
	window->winGetSize( &width, &height );
	rect.hi.x = rect.lo.x + width;
	rect.hi.y = rect.lo.y + height;
	return rect;
}

/** The screen rectangle of the bar's window `prefix` followed by `number` in two digits, shown or
	* not, as layoutPanels placed it, before gridGrowth grew it. */
static IRegion2D numberedWindowRect( const char *prefix, Int number )
{
	GameWindow *window = numberedWindow( prefix, number );
	const ICoord2D inset = TheControlBar->getPlacedInset( window );
	return reachedBy( windowScreenRect( window ), inset.x, inset.y );
}

/** The page's ring of steel round a picture with `gap` screen pixels to its neighbour: half of it in
	* the page's pixels, so two rings meet in the gap, and one at least, which is the bevel. */
static Int pageRing( Int gap )
{
	return max( 1, (Int)REAL_TO_INT_FLOOR( gap / ControlBarUniformScale() / 2 ) );
}

/** How far a grid's buttons grow into the gaps the layout left between them, and the ring the page
	* lays round each picture. */
struct GridGrowth
{
	ICoord2D grow;		///< screen pixels, across and down, on each side
	Int ring;					///< page pixels
};

/** The buttons grow until the gaps beside and below button `first` are both half the narrower of
	* the two, and never under `least` screen pixels. */
static GridGrowth gridGrowth( const char *prefix, Int first, Int beside, Int below, Int least )
{
	const IRegion2D place = numberedWindowRect( prefix, first );
	const Int across = numberedWindowRect( prefix, beside ).lo.x - place.hi.x;
	const Int down = numberedWindowRect( prefix, below ).lo.y - place.hi.y;
	const Int gap = max( least, min( across, down ) / 2 );
	GridGrowth growth;
	growth.grow.x = max( 0, ( across - gap ) / 2 );
	growth.grow.y = max( 0, ( down - gap ) / 2 );
	growth.ring = pageRing( min( across - 2 * growth.grow.x, down - 2 * growth.grow.y ) );
	return growth;
}

static void putPageRect( HtmlValues &values, const std::string &name, const IRegion2D &rect, Bool shown );

/** One cell of a grid for the page: the picture's hole as `cell` and the ring round it. */
static void putCell( HtmlValues &entry, const IRegion2D &picture, Int ring )
{
	putPageRect( entry, "cell", picture, TRUE );
	entry[ "ring" ] = std::to_string( ring );
}

/** ButtonCommandNN's screen rectangle, `button` 1 to COMMAND_BUTTONS, shown or not. */
static IRegion2D commandButtonRect( Int button )
{
	return numberedWindowRect( "ButtonCommand", button );
}

/** The box round the fourteen command buttons, shown or not. */
static IRegion2D commandButtonsBox( void )
{
	IRegion2D box;
	for( Int button = 1; button <= COMMAND_BUTTONS; button++ )
	{
		const IRegion2D place = commandButtonRect( button );
		if( button == 1 )
			box = place;
		box.lo.x = min( box.lo.x, place.lo.x );
		box.lo.y = min( box.lo.y, place.lo.y );
		box.hi.x = max( box.hi.x, place.hi.x );
		box.hi.y = max( box.hi.y, place.hi.y );
	}
	return box;
}

static void stackCentre( HtmlValues &values, Bool shown, const ICoord2D &grow, IRegion2D &centre )
{
	// the command buttons stand as low as the idle worker's key beside them, GRID_BOTTOM_GAP over the
	// screen's bottom edge, wherever the side's layout put them
	const Real scale = ControlBarUniformScale();
	const Int shift = barBottom() - REAL_TO_INT( GRID_BOTTOM_GAP * scale ) - commandButtonsBox().hi.y - grow.y;
	if( shift != 0 )
		TheControlBar->lowerPlacedWindow( controlBarWindow( "CommandWindow" ), shift );

	IRegion2D grid, power, money;
	const Bool othersFound = controlBarUnion( CONTROL_BAR_CENTRE, grid );

	// with nothing selected the bar hides the command grid, and the panel must not go with it: the
	// money and the power bar stood over bare battlefield.  The grid's place holds whether it is up,
	// and it is the fourteen buttons' place rather than CommandWindow's: the window reaches 34 pixels
	// further left, over where retail's beacon button stood, and left an empty strip in the panel
	// It is the cells', which reach `grow` past the buttons' places
	IRegion2D buttons = reachedBy( commandButtonsBox(), grow.x, grow.y );
	if( othersFound )
	{
		buttons.lo.x = min( buttons.lo.x, grid.lo.x );
		buttons.lo.y = min( buttons.lo.y, grid.lo.y );
		buttons.hi.x = max( buttons.hi.x, grid.hi.x );
		buttons.hi.y = max( buttons.hi.y, grid.hi.y );
	}
	grid = buttons;
	const Bool powerFound = controlBarWindowRect( controlBarWindow( "PowerWindow" ), power );

	// the grid's container is the grid, its border outside it running down to the screen's bottom.
	// The power bar is the page's own now, so its frame is free to stand on that border, as wide as
	// the grid: each side's layout puts the power window at a width of its own, and the bar changed
	// length with the side.  The money is set down on the frame, over the grid's middle
	centre = framed( grid, REAL_TO_INT( PANEL_BORDER * scale ), FALSE, FALSE );
	IRegion2D frame = grownBy( power, STACK_FRAME, STACK_FRAME, STACK_FRAME, STACK_FRAME );
	const Int frameHeight = frame.hi.y - frame.lo.y;
	frame.lo.x = grid.lo.x;
	frame.hi.x = grid.hi.x;
	frame.hi.y = centre.lo.y;
	frame.lo.y = frame.hi.y - frameHeight;

	// the power bar's block is the centre's bezel carried on upward, as wide as it, so the bar and
	// the grid read as two wells in one plate: the centre's top border runs between them
	IRegion2D powerBlock;
	powerBlock.lo.x = centre.lo.x;
	powerBlock.hi.x = centre.hi.x;
	powerBlock.hi.y = centre.lo.y;
	powerBlock.lo.y = frame.lo.y - REAL_TO_INT( STACK_POWER_TOP * scale );

	// each block stands on the top of the one under it
	Int floor = centre.lo.y;
	if( powerFound )
		floor = powerBlock.lo.y;

	GameWindow *moneyWindow = controlBarWindow( "MoneyDisplay" );
	Bool moneyFound = controlBarWindowRect( moneyWindow, money );
	if( moneyFound && moneyWindow->winGetFont() )
	{
		// the font's height is the line without the drop shadow and the accents over the capitals, and
		// a window that tall had the figure standing out over the block; half as much again holds it
		enum { MONEY_LINE_HALVES = 3 };
		const Int height = moneyWindow->winGetFont()->height * MONEY_LINE_HALVES / 2;
		Int parentX = 0, parentY = 0;
		if( moneyWindow->winGetParent() )
			moneyWindow->winGetParent()->winGetScreenPosition( &parentX, &parentY );
		const Int left = ( grid.lo.x + grid.hi.x - ( money.hi.x - money.lo.x ) ) / 2;
		moneyWindow->winSetPosition( left - parentX, floor - height - parentY );
		moneyWindow->winSetSize( money.hi.x - money.lo.x, height );
		moneyFound = controlBarWindowRect( moneyWindow, money );
	}
	IRegion2D block = grownBy( money, STACK_MONEY_SIDE, STACK_MONEY_TOP, STACK_MONEY_SIDE, 0 );
	block.hi.y = floor;

	putFrame( values, "centre", grid, centre, shown );
	putPageRect( values, "powerframe", frame, powerFound && shown );
	putPageRect( values, "powerblock", powerBlock, powerFound && shown );

	// the page lays boxes out content-box whatever box-sizing says, so the frame's border goes on top
	// of its width and height: hand it the content, and the height inside its lip for the cells, since
	// a percentage of a height set by top and bottom does not resolve in the page either
	enum { FRAME_BORDER = 2, FRAME_LIP = 1 };
	const Int width = atoi( values[ "powerframe.w" ].c_str() ) - 2 * FRAME_BORDER;
	const Int height = atoi( values[ "powerframe.h" ].c_str() ) - 2 * FRAME_BORDER;
	values[ "powerframe.w" ] = std::to_string( width > 0 ? width : 0 );
	values[ "powerframe.h" ] = std::to_string( height > 0 ? height : 0 );
	values[ "powerframe.inner" ] = std::to_string( height > 2 * FRAME_LIP ? height - 2 * FRAME_LIP : 0 );
	putPageRect( values, "moneyblock", block, moneyFound && shown );
}

/** `name`.x, .y, .w and .h in the page's pixels, and `name`.shown "shown" or "hidden". */
static void putPageRect( HtmlValues &values, const std::string &name, const IRegion2D &rect, Bool shown )
{
	// every edge is rounded once, on its own, so two boxes that share an edge on screen share it on
	// the page too: rounding a width and a height separately left the stacked blocks a pixel or two
	// apart
	const Real scale = ControlBarUniformScale();
	const Int left = REAL_TO_INT_FLOOR( rect.lo.x / scale + 0.5f );
	const Int top = REAL_TO_INT_FLOOR( rect.lo.y / scale + 0.5f );
	const Int right = REAL_TO_INT_FLOOR( rect.hi.x / scale + 0.5f );
	const Int bottom = REAL_TO_INT_FLOOR( rect.hi.y / scale + 0.5f );
	values[ name + ".x" ] = std::to_string( left );
	values[ name + ".y" ] = std::to_string( top );
	values[ name + ".w" ] = std::to_string( shown ? right - left : 0 );
	values[ name + ".h" ] = std::to_string( shown ? bottom - top : 0 );
	values[ name + ".shown" ] = shown ? "shown" : "hidden";
}

static void drawCommandGridFront( GameWindow *window, WinInstanceData *instData )
{
	TheInGameUI->drawCellGridFront( InGameUI::CELL_GRID_COMMAND );
}

static void drawQueueGridFront( GameWindow *window, WinInstanceData *instData )
{
	TheInGameUI->drawCellGridFront( InGameUI::CELL_GRID_QUEUE );
}

static void drawPowersGridFront( GameWindow *window, WinInstanceData *instData )
{
	TheInGameUI->drawCellGridFront( InGameUI::CELL_GRID_POWERS );
}

/** A window of no size and no input at the head of `parent`'s children, which draw from the tail,
	* so `draw` runs after every one of them; made the first time and kept at the head. */
static void putFrontWindow( GameWindow *parent, GameWinDrawFunc draw )
{
	GameWindow *front = parent->winGetChild();
	while( front && front->winGetDrawFunc() != draw )
		front = front->winGetNext();
	if( front == NULL )
	{
		front = TheWindowManager->winCreate( parent, WIN_STATUS_NO_INPUT, 0, 0, 0, 0, NULL );
		front->winSetDrawFunc( draw );
	}
	if( parent->winGetChild() != front )
		front->winBringToTop();
}

static const char *const NET_PAGE = "Window\\Html\\Net.html";

/** The network box in the screen's top right corner, drawHudOverlay's readings on a page of their
	* own: they change several times a second, and each change lays its page out again. */
void InGameUI::drawNetPage( void )
{
	if( !m_netPageLoaded )
	{
		m_netPageLoaded = TRUE;
		readHtmlPage( NET_PAGE, m_netPage );
	}
	if( m_netPage.empty() )
		return;
	if( m_netOverlay == NULL )
		m_netOverlay = new HtmlOverlay( m_superweaponNormalFont );

	HtmlValues values = m_hudValues;
	values[ "side" ] = spectatorSide();
	m_netOverlay->setPage( HtmlTemplate_expand( m_netPage, values, HtmlLists(), lookupGameText ) );
	m_netOverlay->draw();
}

void InGameUI::drawCellGridFront( Int grid )
{
	HtmlOverlay *&overlay = m_cellFrontOverlay[ grid ];
	if( overlay == NULL )
		overlay = new HtmlOverlay( m_superweaponNormalFont );

	HtmlValues values;
	HtmlLists lists;
	values[ "layer" ] = "front";
	values[ "side" ] = spectatorSide();
	lists[ "frontcells" ] = m_cellFrontCells[ grid ];
	overlay->setPage( HtmlTemplate_expand( m_controlBarPage, values, lists, lookupGameText ) );
	overlay->draw();
}

//-------------------------------------------------------------------------------------------------
/** The command bar's frame, drawn from Window/Html/ControlBar.html in the place of its three plates.
	* The buttons, the radar and the portrait are still the bar's own windows and paint over it; the
	* page only draws what sits round and under them, so it is told where they are: panel0 to panel2
	* are the plates' rectangles, sliding and minimising with the bar, and every name in
	* CONTROL_BAR_WINDOWS is its window's rectangle.  All of them in the page's 800x600 pixels. */
//-------------------------------------------------------------------------------------------------
Bool InGameUI::drawControlBarPage( const IRegion2D *panels, const Bool *shown, Int panelCount )
{
	m_controlBarPageShown = FALSE;
	if( TheGameLogic == NULL || !TheGameLogic->isInGame() || TheGameLogic->isInShellGame() )
	{
		TheControlBar->setPageSolids( NULL );
		return FALSE;
	}

	if( !m_controlBarPageLoaded )
	{
		m_controlBarPageLoaded = TRUE;
		readHtmlPage( CONTROL_BAR_PAGE, m_controlBarPage );
	}
	if( m_controlBarPage.empty() )
	{
		TheControlBar->setPageSolids( NULL );
		return FALSE;
	}
	if( m_controlBarOverlay == NULL )
		m_controlBarOverlay = new HtmlOverlay( m_superweaponNormalFont );

	HtmlValues values;
	values[ "layer" ] = "back";
	values[ "side" ] = spectatorSide();
	// a watcher has no promotions of his own to spend, whatever the bar's flash says
	const Bool watching = localPlayerWatching();
	values[ "promotion" ] = !watching && TheControlBar->isGeneralStarFlashing() ? "ready" : "";
	values[ "watching" ] = watching ? "watching" : "";
	// the page's clicks are whole, down and up at once, so a key pressed in under the pointer goes by
	// the mouse's own left button.  Only over the page: a changed value lays the whole page out again,
	// fifteen milliseconds, and every click on the battlefield did that twice
	values[ "held" ] = m_controlBarPageHovered && TheMouse->getMouseStatus()->leftState != MBS_Up ? "held" : "";
	values[ "blink" ] = !values[ "promotion" ].empty() && TheGameLogic->getFrame() % LOGICFRAMES_PER_SECOND > LOGICFRAMES_PER_SECOND / 2 ? "lit" : "";
	for( Int panel = 0; panel < panelCount; panel++ )
		putPageRect( values, "panel" + std::to_string( panel ), panels[ panel ], shown[ panel ] );

	// Every panel is a container exactly as big as what it holds, and a border outside it: steel on
	// the sides facing the battlefield, out to the screen's edge on the sides against it.  Everything
	// else - the tabs, the buttons, the signals - stands outside the border.
	const Real scale = ControlBarUniformScale();
	const Int border = REAL_TO_INT( PANEL_BORDER * scale );

	// the command and queue buttons grow into the gaps the layout left until the steel between two
	// pictures is half what it was, and the page's rings round the pictures meet in what is left.
	// The gaps are measured from the placed rectangles, whatever the buttons were last grown by
	const Int leastGap = REAL_TO_INT( CELL_GAP_LEAST * scale );
	const GridGrowth commandGrowth = gridGrowth( "ButtonCommand", 1, 3, 2, leastGap );
	const GridGrowth queueGrowth = gridGrowth( "ButtonQueue", 1, 2, 4, leastGap );
	ICoord2D grown;
	grown.x = -commandGrowth.grow.x;
	grown.y = -commandGrowth.grow.y;
	for( Int button = 1; button <= COMMAND_BUTTONS; button++ )
		TheControlBar->insetPlacedWindow( numberedWindow( "ButtonCommand", button ), grown );
	grown.x = -queueGrowth.grow.x;
	grown.y = -queueGrowth.grow.y;
	for( Int button = 1; button <= QUEUE_BUTTONS; button++ )
		TheControlBar->insetPlacedWindow( numberedWindow( "ButtonQueue", button ), grown );

	const Bool leftShown = panelCount > 0 && shown[ 0 ];
	const Bool rightShown = panelCount > 2 && shown[ 2 ];

	IRegion2D content;
	const Bool leftFound = controlBarUnion( CONTROL_BAR_LEFT, content );
	const IRegion2D leftBox = framed( content, border, TRUE, FALSE );
	putFrame( values, "left", content, leftBox, leftFound && leftShown );
	const IRegion2D alertTab = tabOn( leftBox, ALERT_TAB_WIDTH, PANEL_TAB_HEIGHT, FALSE );
	putPageRect( values, "alerttab", alertTab, leftFound && leftShown );
	// the event feed stands on that tab, or on the screen's bottom with the radar folded away
	m_feedFloor = leftFound && leftShown ? alertTab.lo.y : barBottom();

	// the three smoke signal buttons, a column standing on the screen's bottom edge against the left
	// panel's border; not there at all where the keys would do nothing either, a game with no allies
	// to see the smoke
	IRegion2D signalColumn;
	signalColumn.lo.x = leftBox.hi.x;
	signalColumn.hi.x = leftBox.hi.x + REAL_TO_INT( SIGNAL_BUTTON_WIDTH * scale );
	signalColumn.hi.y = barBottom();
	signalColumn.lo.y = signalColumn.hi.y - REAL_TO_INT( SIGNAL_BUTTON_SIZE * SIGNAL_BUTTONS * scale );
	const Bool signalsShown = leftFound && leftShown && signalsAllowed();
	putPageRect( values, "signals", signalColumn, signalsShown );
	const UnsignedInt nowMs = timeGetTime();
	if( signalsShown && !m_signalsWereShown )
		m_signalsRiseStartMs = nowMs;
	m_signalsWereShown = signalsShown;
	putSignalRise( values, nowMs - m_signalsRiseStartMs );

	IRegion2D centreBox;
	// the grid's container holds the pictures and the rings round them
	ICoord2D gridOutside;
	gridOutside.x = commandGrowth.grow.x + REAL_TO_INT( commandGrowth.ring * scale );
	gridOutside.y = commandGrowth.grow.y + REAL_TO_INT( commandGrowth.ring * scale );
	stackCentre( values, panelCount > 1 && shown[ 1 ], gridOutside, centreBox );
	// the idle worker's key, a smoke signal's size, in a step against the centre panel's border at its
	// bottom left, standing on the screen's bottom edge; on the top edge it stood under the power bar
	IRegion2D workerStep;
	workerStep.hi.x = centreBox.lo.x;
	workerStep.lo.x = workerStep.hi.x - REAL_TO_INT( WORKER_TAB_WIDTH * scale );
	workerStep.hi.y = barBottom();
	workerStep.lo.y = workerStep.hi.y - REAL_TO_INT( WORKER_STEP_HEIGHT * scale );
	putPageRect( values, "workertab", workerStep, panelCount > 1 && shown[ 1 ] );
	HtmlLists lists;
	putPowerBar( values, lists[ "powercells" ] );	// after the stack, whose frame it divides into cells

	// a well behind each of the fourteen command buttons, shown or not, so the grid reads as a grid
	// with the steel between its places, and an empty place is a hole in it rather than bare dark
	std::vector< HtmlValues > &commandCells = lists[ "commandcells" ];
	for( Int button = 1; button <= COMMAND_BUTTONS && panelCount > 1 && shown[ 1 ]; button++ )
	{
		HtmlValues entry;
		putCell( entry, windowScreenRect( numberedWindow( "ButtonCommand", button ) ), commandGrowth.ring );
		commandCells.push_back( entry );
	}

	const Bool rightFound = controlBarUnion( CONTROL_BAR_RIGHT, content );
	const IRegion2D rightBox = framed( content, border, FALSE, TRUE );
	putFrame( values, "right", content, rightBox, rightFound && rightShown );

	// the experience bar's groove down the column the bar's own window and its foreground picture
	// shared, the medal on top included, and the rank's stars in a tab on the panel's border at its
	// right hand end, the way the under-attack light stands on the radar's at its left
	IRegion2D experience;
	const Bool experienceFound = controlBarUnion( CONTROL_BAR_EXPERIENCE, experience );
	putPageRect( values, "expframe", experience, experienceFound && rightFound && rightShown );
	putExperienceBar( values, lists[ "expcells" ], lists[ "rankstars" ] );
	// a watcher, or a player beaten, has no promotions to buy, and the bar disables the button for him
	const IRegion2D starsTab = tabOn( rightBox, STARS_TAB_WIDTH, STARS_TAB_HEIGHT, TRUE );
	const Bool starsShown = rightFound && rightShown && ThePlayerList->getLocalPlayer()->isPlayerActive();
	putPageRect( values, "starstab", starsTab, starsShown );

	// the portrait's well is steel with a dark cell for each of the production queue's nine places,
	// the grid the side's RightHUD picture used to draw in the side's own colour, blue for America
	// While a unit is selected its portrait stands over four of those places and its upgrades in the
	// other five: the portrait's four are one cell then, the portrait's own
	std::vector< HtmlValues > &portraitCells = lists[ "portraitcells" ];
	GameWindow *portrait = controlBarWindow( "CameoWindow" );
	const Bool portraitShown = !portrait->winIsHidden() && !portrait->winGetParent()->winIsHidden();
	const IRegion2D portraitRect = windowScreenRect( portrait );
	if( portraitShown && rightFound && rightShown )
	{
		HtmlValues entry;
		putCell( entry, portraitRect, queueGrowth.ring );
		portraitCells.push_back( entry );
	}
	for( Int button = 1; button <= QUEUE_BUTTONS && rightFound && rightShown; button++ )
	{
		const IRegion2D place = windowScreenRect( numberedWindow( "ButtonQueue", button ) );
		if( portraitShown && place.lo.x >= portraitRect.lo.x && place.hi.x <= portraitRect.hi.x + 1 &&
				place.lo.y >= portraitRect.lo.y && place.hi.y <= portraitRect.hi.y + 1 )
			continue;
		HtmlValues entry;
		putCell( entry, place, queueGrowth.ring );
		portraitCells.push_back( entry );
	}

	// the general's powers ready to fire over the right panel, the first in the corner against the
	// screen's right edge, the row growing left as they come and wrapping upward past three, each
	// three quarters of a command button.  They sit in a tray of the panels' steel only as big as they are, and each
	// has a dark cell behind it the way the command grid's buttons do
	const Int trayBorder = REAL_TO_INT( SKILL_TRAY_BORDER * scale );
	ICoord2D corner;
	corner.x = TheDisplay->getWidth() - trayBorder;
	corner.y = starsTab.lo.y - REAL_TO_INT( SKILL_GRID_GAP * scale ) - trayBorder;
	const IRegion2D button = commandButtonRect( 1 );
	ICoord2D cell;
	cell.x = ( button.hi.x - button.lo.x ) * SKILL_CELL_PERCENT / 100;
	cell.y = ( button.hi.y - button.lo.y ) * SKILL_CELL_PERCENT / 100;
	const Int cellGap = REAL_TO_INT( SKILL_CELL_GAP * scale );
	const Int powersShown = TheControlBar->placeSpecialPowerShortcutGrid( rightFound && rightShown ? &corner : NULL, cell, cellGap );

	std::vector< HtmlValues > &places = lists[ "skillcells" ];
	IRegion2D powers;
	powers.lo = corner;
	powers.hi = corner;
	for( Int slot = 0; slot < powersShown; slot++ )
	{
		const Int column = slot % SPECIAL_POWER_SHORTCUT_COLS;
		const Int row = slot / SPECIAL_POWER_SHORTCUT_COLS;
		IRegion2D place;
		place.hi.x = corner.x - column * ( cell.x + cellGap );
		place.hi.y = corner.y - row * ( cell.y + cellGap );
		place.lo.x = place.hi.x - cell.x;
		place.lo.y = place.hi.y - cell.y;
		powers.lo.x = min( powers.lo.x, place.lo.x );
		powers.lo.y = min( powers.lo.y, place.lo.y );
		HtmlValues entry;
		putCell( entry, place, pageRing( cellGap ) );
		places.push_back( entry );
	}
	IRegion2D tray = powers;
	tray.lo.x -= trayBorder;
	tray.lo.y -= trayBorder;
	tray.hi.x = TheDisplay->getWidth();
	tray.hi.y += trayBorder;
	putFrame( values, "skilltray", powers, tray, powersShown > 0 );

	// each grid's frames again in front of its buttons, drawn by a window after them
	m_cellFrontCells[ CELL_GRID_COMMAND ] = commandCells;
	m_cellFrontCells[ CELL_GRID_QUEUE ] = portraitCells;
	m_cellFrontCells[ CELL_GRID_POWERS ] = places;
	putFrontWindow( controlBarWindow( "CommandWindow" ), drawCommandGridFront );
	// the portrait's grid over the queue, the portrait and its upgrades alike, all RightHUD's
	putFrontWindow( controlBarWindow( "RightHUD" ), drawQueueGridFront );
	GameWindow *powersParent = TheControlBar->getSpecialPowerShortcutParent();
	if( powersParent )
		putFrontWindow( powersParent, drawPowersGridFront );

	// the promotion button is the stars' tab: its window moves under the tab and takes the click that
	// opens the promotion screen.  With no tab drawn it goes too: a watcher who clicked a unit got the
	// button enabled again, an empty patch of screen with a tooltip that opened the screen
	GameWindow *promotion = controlBarWindow( "ButtonGeneral" );
	if( promotion )
		promotion->winHide( !starsShown );
	if( rightFound && promotion )
	{
		Int parentX = 0, parentY = 0;
		if( promotion->winGetParent() )
			promotion->winGetParent()->winGetScreenPosition( &parentX, &parentY );
		promotion->winSetPosition( starsTab.lo.x - parentX, starsTab.lo.y - parentY );
		promotion->winSetSize( starsTab.hi.x - starsTab.lo.x, starsTab.hi.y - starsTab.lo.y );
	}

	for( Int each = 0; each < (Int)ARRAY_SIZE( CONTROL_BAR_WINDOWS ); each++ )
	{
		const std::string name = CONTROL_BAR_WINDOWS[ each ];
		GameWindow *window = controlBarWindow( name );
		IRegion2D rect;
		putPageRect( values, name, rect, controlBarWindowRect( window, rect ) );
		values[ name + ".state" ] = controlBarWindowState( window );
	}

	for( Int each = 0; each < (Int)ARRAY_SIZE( CONTROL_BAR_CUSTOM ); each++ )
	{
		GameWindow *window = controlBarWindow( CONTROL_BAR_CUSTOM[ each ] );
		if( window )
			window->winSetDrawFunc( drawNothing );
	}
	for( Int each = 0; each < (Int)ARRAY_SIZE( CONTROL_BAR_STOOD_DOWN ); each++ )
	{
		GameWindow *window = controlBarWindow( CONTROL_BAR_STOOD_DOWN[ each ] );
		if( window && !window->winIsHidden() )
			window->winHide( TRUE );
	}

	// the promotion screen is its own layout, drawn over the bar; its page goes with this one
	if( !m_promotionPageLoaded )
	{
		m_promotionPageLoaded = TRUE;
		readHtmlPage( PROMOTION_PAGE, m_promotionPage );
	}
	if( !m_promotionPage.empty() )
		standDownPromotionScreen();

	m_controlBarOverlay->setPage( HtmlTemplate_expand( m_controlBarPage, values, lists, lookupGameText ) );
	m_controlBarPageHovered = m_controlBarOverlay->hover( TheMouse->getMouseStatus()->pos );
	m_controlBarOverlay->draw();
	drawNetPage();

	std::vector< IRegion2D > solids;
	m_controlBarOverlay->rectsOf( ".solid", solids );
	std::vector< IRegion2D > buttons;
	m_controlBarOverlay->rectsOf( "[data-click]", buttons );
	TheControlBar->setPageSolids( &solids, &buttons );
	m_controlBarPageShown = TRUE;
	return TRUE;
}

//-------------------------------------------------------------------------------------------------
/** The page's own buttons sit where the bar's frame takes no clicks, so their clicks arrive as clicks
	* on the world and are taken here.  data-click="press:Name" presses the bar's window Name, the
	* message a click on it would have sent, even though the window itself is stood down. */
//-------------------------------------------------------------------------------------------------
Bool InGameUI::handleControlBarPageClick( const ICoord2D *mouse, Bool act )
{
	if( !m_controlBarPageShown || m_controlBarOverlay == NULL || !m_controlBarOverlay->hover( *mouse ) )
		return FALSE;

	const std::string action = m_controlBarOverlay->click( *mouse );
	if( action.compare( 0, SIGNAL_ACTION.size(), SIGNAL_ACTION ) == 0 )
	{
		if( act )
			armSignalFromPage( action.substr( SIGNAL_ACTION.size() ) );
		return TRUE;
	}
	if( action.compare( 0, PRESS_ACTION.size(), PRESS_ACTION ) != 0 )
		return FALSE;
	if( !act )
		return TRUE;

	GameWindow *button = controlBarWindow( action.substr( PRESS_ACTION.size() ) );
	if( button == NULL || !BitTest( button->winGetStatus(), WIN_STATUS_ENABLED ) )
		return TRUE;

	TheWindowManager->winSendSystemMsg( button->winGetOwner(), GBM_SELECTED, (WindowMsgData)button, button->winGetWindowId() );
	AudioEventRTS buttonClick( "GUIClick" );
	if( TheAudio )
		TheAudio->addAudioEvent( &buttonClick );
	return TRUE;
}

static const char *const PROMOTION_PREFIX = "GeneralsExpPoints.wnd:";
static const char *const PROMOTION_BUTTON_PREFIX = "ButtonRank";

/** The promotion screen's three rows, each a run of ButtonRankNNumberM buttons under a heading. */
static const struct PromotionRow
{
	const char *buttons;	///< the buttons' name, before their number
	Int count;
	Int depth;						///< buttons to a column: the layout numbers the middle row's down each column
	const char *heading;	///< the string table label over the row
} PROMOTION_ROWS[] =
{
	{ "ButtonRank1Number", 4, 1, "GUI:Rank1Required" },
	{ "ButtonRank3Number", 15, 3, "GUI:Rank3Required" },
	{ "ButtonRank8Number", 4, 1, "GUI:Rank8Required" }
};

/** The compact screen's measures, 800x600 pixels; a cell is a command button's size. */
enum
{
	PROMOTION_COLUMNS		= 5,	///< the widest row, the middle one
	PROMOTION_MARGIN		= 4,	///< the plate round the wells, and between one well and the next
	PROMOTION_INSET			= 3,	///< a well round its heading and cells
	PROMOTION_CELL_GAP	= 2,
	PROMOTION_TITLE			= 14,	///< the rank's name's line, the points beside it
	PROMOTION_BAR				= 7,	///< the experience bar under it
	PROMOTION_BAR_GAP		= 3,
	PROMOTION_HEADING		= 12,	///< a row's heading over its first button
	PROMOTION_TOP				= 44	///< clear of the players' strip, a team's name under its flags included
};

/** One place in the screen's grids, screen pixels, and the promotion's button standing in it: NULL
	* for a place no promotion fills, which shows as a hole, and the close button in its own place. */
struct PromotionPlace
{
	IRegion2D rect;
	GameWindow *button;
};

/** Where layoutPromotionScreen put everything the page draws round the windows. */
struct PromotionLayout
{
	std::vector< PromotionPlace > places;
	std::vector< IRegion2D > headings;	///< one over each row, in PROMOTION_ROWS' order
	std::vector< IRegion2D > wells;			///< one round each row and its heading, the same order
};

static GameWindow *promotionWindow( const std::string &name )
{
	return TheWindowManager->winGetWindowFromId( NULL, NAMEKEY( ( PROMOTION_PREFIX + name ).c_str() ) );
}

/** Puts one of the screen's windows at `x`, `y` in its parent, `width` by `height`, screen pixels. */
static void placePromotionWindow( const std::string &name, Int x, Int y, Int width, Int height )
{
	GameWindow *window = promotionWindow( name );
	window->winSetPosition( x, y );
	window->winSetSize( width, height );
}

//-------------------------------------------------------------------------------------------------
/** The promotion screen laid out compact, every frame it draws, over the layout's own places: each
	* row a grid five places wide in command button cells with a hair of steel between them, the way
	* the command bar's grid is, in a well of its own with its heading, a plate's margin between one
	* well and the next; the rank's name and the points on one line with the bar under them, and the
	* close button in the last row's fifth place, which no side fills.  Centred across the screen, its
	* top under the players' strip.  `layout` gets every place, filled or not, the headings and the wells,
	* in screen pixels. */
//-------------------------------------------------------------------------------------------------
static void layoutPromotionScreen( GameWindow *parent, PromotionLayout &layout )
{
	const Real scale = ControlBarUniformScale();
	struct Page { static Int px( Int pagePixels, Real scale ) { return REAL_TO_INT( pagePixels * scale ); } };
	const IRegion2D button = commandButtonRect( 1 );
	const Int cellWidth = button.hi.x - button.lo.x;
	const Int cellHeight = button.hi.y - button.lo.y;
	const Int gap = Page::px( PROMOTION_CELL_GAP, scale );
	const Int margin = Page::px( PROMOTION_MARGIN, scale );
	const Int inset = Page::px( PROMOTION_INSET, scale );
	const Int heading = Page::px( PROMOTION_HEADING, scale );

	const Int gridWidth = PROMOTION_COLUMNS * cellWidth + ( PROMOTION_COLUMNS - 1 ) * gap;
	const Int width = gridWidth + 2 * ( margin + inset );
	const Int titleHeight = Page::px( PROMOTION_TITLE, scale );
	const Int pointsWidth = Page::px( PROMOTION_TITLE + PROMOTION_BAR, scale );
	placePromotionWindow( "StaticTextTitle", margin, margin, width - 2 * margin - pointsWidth, titleHeight );
	placePromotionWindow( "StaticTextRankPointsAvailable", width - margin - pointsWidth, margin, pointsWidth,
												titleHeight + Page::px( PROMOTION_BAR + PROMOTION_BAR_GAP, scale ) );
	placePromotionWindow( "ProgressBarExperience", margin, margin + titleHeight + Page::px( PROMOTION_BAR_GAP, scale ),
												width - 3 * margin - pointsWidth, Page::px( PROMOTION_BAR, scale ) );

	// the parent goes first, across the middle of the screen just under the players hanging from its
	// top edge, so the places can be handed out in screen pixels
	const Int parentX = ( TheDisplay->getWidth() - width ) / 2;
	const Int parentY = Page::px( PROMOTION_TOP, scale );
	parent->winSetPosition( parentX, parentY );

	// each row's well stands a margin under the one before, the first a margin under the bar and the
	// points, and the heading and the cells an inset inside it
	Int y = margin + titleHeight + Page::px( PROMOTION_BAR_GAP + PROMOTION_BAR, scale );
	const Int left = margin + inset;
	layout.places.clear();
	layout.headings.clear();
	layout.wells.clear();
	for( Int row = 0; row < (Int)ARRAY_SIZE( PROMOTION_ROWS ); row++ )
	{
		y += margin;
		IRegion2D well;
		well.lo.x = parentX + margin;
		well.hi.x = parentX + width - margin;
		well.lo.y = parentY + y;
		y += inset;
		IRegion2D title;
		title.lo.x = parentX + left;
		title.hi.x = title.lo.x + gridWidth;
		title.lo.y = parentY + y;
		title.hi.y = title.lo.y + heading - gap;
		layout.headings.push_back( title );
		y += heading;

		const Int depth = PROMOTION_ROWS[ row ].depth;
		const Bool lastRow = row == (Int)ARRAY_SIZE( PROMOTION_ROWS ) - 1;
		for( Int column = 0; column < PROMOTION_COLUMNS; column++ )
		{
			for( Int line = 0; line < depth; line++ )
			{
				const Int x = left + column * ( cellWidth + gap );
				const Int top = y + line * ( cellHeight + gap );
				const Int number = column * depth + line;
				PromotionPlace place;
				place.rect.lo.x = parentX + x;
				place.rect.lo.y = parentY + top;
				place.rect.hi.x = place.rect.lo.x + cellWidth;
				place.rect.hi.y = place.rect.lo.y + cellHeight;
				place.button = NULL;
				if( lastRow && column == PROMOTION_COLUMNS - 1 )
				{
					placePromotionWindow( "ButtonExit", x, top, cellWidth, cellHeight );
					place.button = promotionWindow( "ButtonExit" );
				}
				else if( number < PROMOTION_ROWS[ row ].count )
				{
					const std::string name = PROMOTION_ROWS[ row ].buttons + std::to_string( number );
					placePromotionWindow( name, x, top, cellWidth, cellHeight );
					place.button = promotionWindow( name );
				}
				layout.places.push_back( place );
			}
		}
		y += depth * ( cellHeight + gap ) - gap + inset;
		well.hi.y = parentY + y;
		layout.wells.push_back( well );
	}

	parent->winSetSize( width, y + margin );
}

/** The screen's picture, drawn by the page while it is there. */
static void drawPromotionScreen( GameWindow *window, WinInstanceData *instData )
{
	TheInGameUI->drawPromotionPage( window, FALSE );
}

/** The grid's frames over the promotions, drawn by the child that draws last. */
static void drawPromotionScreenFront( GameWindow *window, WinInstanceData *instData )
{
	TheInGameUI->drawPromotionPage( window->winGetParent(), TRUE );
}

/** The promotion screen hands its look to the page: the parent draws the page, and every child but
	* the promotions' own buttons draws nothing - the side's painting, the titles, the bar and its frame
	* and the close button are the page's.  They all keep their clicks.  The title is moved to the head
	* of the children, which draw from the tail, so it draws after the promotions and puts the grid's
	* frames over their edges; it stands clear of every button, so it takes no click from one. */
static void standDownPromotionScreen( void )
{
	GameWindow *parent = promotionWindow( "GenExpParent" );
	if( parent == NULL )
		return;

	parent->winSetDrawFunc( drawPromotionScreen );
	const std::string buttonName = std::string( PROMOTION_PREFIX ) + PROMOTION_BUTTON_PREFIX;
	for( GameWindow *child = parent->winGetChild(); child; child = child->winGetNext() )
	{
		if( !child->winGetInstanceData()->m_decoratedNameString.startsWith( buttonName.c_str() ) )
			child->winSetDrawFunc( drawNothing );
	}

	GameWindow *front = promotionWindow( "StaticTextTitle" );
	if( parent->winGetChild() != front )
		front->winBringToTop();
	front->winSetDrawFunc( drawPromotionScreenFront );
}

/** "owned", "ready" or "locked" for one promotion's button, by the state the bar left it in: enabled
	* when it can be bought, disabled in colour when it was, disabled in grey otherwise. */
static const char *promotionState( GameWindow *button )
{
	if( BitTest( button->winGetStatus(), WIN_STATUS_ENABLED ) )
		return "ready";
	return BitTest( button->winGetStatus(), WIN_STATUS_ALWAYS_COLOR ) ? "owned" : "locked";
}

//-------------------------------------------------------------------------------------------------
/** The general's promotion screen from Window/Html/Promotion.html, in the place of the side's
	* painting.  Everything is placed from the screen's own windows, so it goes where the layout and
	* the scheme put them; the promotions are the bar's buttons and paint over the back of the page,
	* and the `front` of it, the grid's frames, paints over them. */
//-------------------------------------------------------------------------------------------------
/** The promotion screen coming up the way the Esc menu does: the screen dims and the page fades in
	* over PROMOTION_FADE_MS of the wall clock from its first picture, never more than
	* PROMOTION_MOST_MS_A_PICTURE a picture, so a slow first picture does not skip the fade. */
static const Int PROMOTION_FADE_MS = 120;
static const Int PROMOTION_MOST_MS_A_PICTURE = 25;
static const Int PROMOTION_NOT_DRAWN = -1;

void InGameUI::openPromotionPage( void )
{
	m_promotionShownMs = PROMOTION_NOT_DRAWN;
}

void InGameUI::drawPromotionPage( GameWindow *parent, Bool front )
{
	enum
	{
		EXPERIENCE_RUNGS	= 20,
		FULL							= 100
	};

	HtmlOverlay *&overlay = front ? m_promotionFrontOverlay : m_promotionOverlay;
	if( overlay == NULL )
		overlay = new HtmlOverlay( m_superweaponNormalFont );

	// laid out in the parent's own draw, the back, so every child is in its place before it draws
	PromotionLayout layout;
	layoutPromotionScreen( parent, layout );

	HtmlValues values;
	HtmlLists lists;
	values[ "layer" ] = front ? "front" : "back";
	values[ "side" ] = spectatorSide();
	values[ "held" ] = TheMouse->getMouseStatus()->leftState != MBS_Up ? "held" : "";

	IRegion2D panel;
	controlBarWindowRect( parent, panel );
	putPageRect( values, "panel", panel, TRUE );
	IRegion2D screen;
	screen.lo.x = screen.lo.y = 0;
	screen.hi.x = TheDisplay->getWidth();
	screen.hi.y = TheDisplay->getHeight();
	putPageRect( values, "screen", screen, TRUE );

	// the back draws first each picture and moves the coming up on for both layers
	if( !front )
	{
		const UnsignedInt now = timeGetTime();
		if( m_promotionShownMs == PROMOTION_NOT_DRAWN )
			m_promotionShownMs = 0;
		else
			m_promotionShownMs = min( m_promotionShownMs + min( (Int)( now - m_promotionDrawnAt ), PROMOTION_MOST_MS_A_PICTURE ),
																PROMOTION_FADE_MS );
		m_promotionDrawnAt = now;
	}
	overlay->setAlpha( max( 0, m_promotionShownMs ) * OPAQUE_PAGE / PROMOTION_FADE_MS );

	static const char *const PLACED[] = { "StaticTextTitle", "ProgressBarExperience", "StaticTextRankPointsAvailable", "ButtonExit" };
	for( Int each = 0; each < (Int)ARRAY_SIZE( PLACED ); each++ )
	{
		GameWindow *window = promotionWindow( PLACED[ each ] );
		IRegion2D rect;
		putPageRect( values, PLACED[ each ], rect, controlBarWindowRect( window, rect ) );
		values[ std::string( PLACED[ each ] ) + ".state" ] = controlBarWindowState( window );
	}
	values[ "title" ] = WideCharStringToMultiByte( GadgetStaticTextGetText( promotionWindow( "StaticTextTitle" ) ).str() );
	values[ "points" ] = WideCharStringToMultiByte( GadgetStaticTextGetText( promotionWindow( "StaticTextRankPointsAvailable" ) ).str() );

	// the bar goes by the player the screen was opened for, the watched one while watching
	const Player *player = TheControlBar->isObserverControlBarOn() ? TheControlBar->getObserverLookAtPlayer()
																																 : ThePlayerList->getLocalPlayer();
	// its rungs cut from the bar's width in page pixels so they add up to it exactly, as the power
	// bar's cells are
	const Int lit = player ? experiencePercent( player ) * EXPERIENCE_RUNGS / FULL : 0;
	const Int barWidth = atoi( values[ "ProgressBarExperience.w" ].c_str() );
	std::vector< HtmlValues > &rungs = lists[ "exprungs" ];
	for( Int rung = 0; rung < EXPERIENCE_RUNGS && barWidth > 0; rung++ )
	{
		const Int left = rung * barWidth / EXPERIENCE_RUNGS;
		HtmlValues entry;
		entry[ "lit" ] = rung < lit ? "lit" : "";
		entry[ "x" ] = std::to_string( left );
		entry[ "w" ] = std::to_string( ( rung + 1 ) * barWidth / EXPERIENCE_RUNGS - left );
		rungs.push_back( entry );
	}

	// each row of promotions stands in a well of steel, a dark cell for every place, filled or not,
	// as the command grid's are, its heading over it
	std::vector< HtmlValues > &wells = lists[ "wells" ];
	for( size_t row = 0; row < layout.wells.size(); row++ )
	{
		HtmlValues entry;
		putPageRect( entry, "well", layout.wells[ row ], TRUE );
		wells.push_back( entry );
	}
	GameWindow *exit = promotionWindow( "ButtonExit" );
	std::vector< HtmlValues > &cells = lists[ "cells" ];
	for( size_t each = 0; each < layout.places.size(); each++ )
	{
		const PromotionPlace &place = layout.places[ each ];
		if( place.button == exit )
			continue;

		HtmlValues entry;
		putCell( entry, place.rect, pageRing( REAL_TO_INT( PROMOTION_CELL_GAP * ControlBarUniformScale() ) ) );
		entry[ "state" ] = place.button && !place.button->winIsHidden() ? promotionState( place.button ) : "empty";
		cells.push_back( entry );
	}
	std::vector< HtmlValues > &headings = lists[ "headings" ];
	for( size_t row = 0; row < layout.headings.size(); row++ )
	{
		HtmlValues entry;
		putPageRect( entry, "heading", layout.headings[ row ], TRUE );
		entry[ "label" ] = WideCharStringToMultiByte( TheGameText->fetch( PROMOTION_ROWS[ row ].heading ).str() );
		headings.push_back( entry );
	}

	overlay->setPage( HtmlTemplate_expand( m_promotionPage, values, lists, lookupGameText ) );
	overlay->hover( TheMouse->getMouseStatus()->pos );
	overlay->draw();
}

static const char *const QUIT_MENU_PAGE = "Window\\Html\\QuitMenu.html";

/** The Esc menu's keys top to bottom.  QuitMenu.wnd has them all, QuitNoSave.wnd, for network games
	* and replays, all but the first. */
static const char *const QUIT_MENU_KEYS[] = { "ButtonSaveLoad", "ButtonOptions", "ButtonRestart", "ButtonExit", "ButtonReturn" };

/** The game's logo over the keys, one name in each layout; the page stands it down. */
static const char *const QUIT_MENU_LOGOS[] = { "WinLoad", "WinLogo" };

/** The menu's measures, 800x600 pixels. */
enum
{
	QUIT_MENU_KEY_WIDTH		= 176,
	QUIT_MENU_KEY_HEIGHT	= 24,
	QUIT_MENU_KEY_GAP			= 6,
	QUIT_MENU_MARGIN			= 12	///< the plate round the keys' well
};

/** The menu coming up, in milliseconds of the wall clock: the dimmed screen and the plate fade in,
	* then the keys come in top to bottom, each fading in from nothing lit bright in the side's steel,
	* the way EA's keys flashed. */
enum
{
	QUIT_MENU_FADE_MS				= 120,
	QUIT_MENU_KEY_FIRST_MS	= 80,		///< the first key, after the plate is mostly there
	QUIT_MENU_KEY_STEP_MS		= 45,		///< each key after the one over it
	QUIT_MENU_KEY_FADE_MS		= 150,	///< a key from nothing to whole
	QUIT_MENU_KEY_FLASH_MS	= 220		///< how long a key stays lit when it comes in, its fade included
};

/** The most one picture moves the menu's coming up on.  A single-player game pausing under the menu
	* draws at ten frames a second for the first half second or so, and on the wall clock alone the
	* keys had all come in within four pictures: they seemed to pop up with no fade at all. */
static const Int QUIT_MENU_MOST_MS_A_PICTURE = 25;
static const Int QUIT_MENU_NOT_DRAWN = -1;	///< m_quitMenuShownMs until the menu's first picture

/** A window of the same layout as `parent`, by its name there. */
static GameWindow *quitMenuWindow( GameWindow *parent, const char *name )
{
	const AsciiString &parentName = parent->winGetInstanceData()->m_decoratedNameString;
	const std::string layout( parentName.str(), strchr( parentName.str(), ':' ) + 1 );
	return TheWindowManager->winGetWindowFromId( parent, NAMEKEY( ( layout + name ).c_str() ) );
}

static void drawQuitMenu( GameWindow *window, WinInstanceData *instData )
{
	TheInGameUI->drawQuitMenuPage( window );
}

void InGameUI::themeQuitMenu( GameWindow *parent )
{
	if( !m_quitMenuPageLoaded )
	{
		m_quitMenuPageLoaded = TRUE;
		readHtmlPage( QUIT_MENU_PAGE, m_quitMenuPage );
	}
	if( m_quitMenuPage.empty() )
		return;

	m_quitMenuShownMs = QUIT_MENU_NOT_DRAWN;
	parent->winSetDrawFunc( drawQuitMenu );
	for( Int key = 0; key < (Int)ARRAY_SIZE( QUIT_MENU_KEYS ); key++ )
	{
		GameWindow *button = quitMenuWindow( parent, QUIT_MENU_KEYS[ key ] );
		if( button )
			button->winSetDrawFunc( drawNothing );
	}
}

//-------------------------------------------------------------------------------------------------
/** The Esc menu laid out compact every frame it draws, before its keys draw: the logo stood down,
	* the keys the layout shows stacked in a well a plate's margin in from the edge, the plate only as
	* big as that and in the middle of the screen.  `well` gets the well, in screen pixels. */
//-------------------------------------------------------------------------------------------------
static void layoutQuitMenu( GameWindow *parent, IRegion2D &well )
{
	const Real scale = ControlBarUniformScale();
	const Int keyWidth = REAL_TO_INT( QUIT_MENU_KEY_WIDTH * scale );
	const Int keyHeight = REAL_TO_INT( QUIT_MENU_KEY_HEIGHT * scale );
	const Int gap = REAL_TO_INT( QUIT_MENU_KEY_GAP * scale );
	const Int margin = REAL_TO_INT( QUIT_MENU_MARGIN * scale );

	for( Int logo = 0; logo < (Int)ARRAY_SIZE( QUIT_MENU_LOGOS ); logo++ )
	{
		GameWindow *window = quitMenuWindow( parent, QUIT_MENU_LOGOS[ logo ] );
		if( window && !window->winIsHidden() )
			window->winHide( TRUE );
	}

	std::vector< GameWindow * > shown;
	for( Int key = 0; key < (Int)ARRAY_SIZE( QUIT_MENU_KEYS ); key++ )
	{
		GameWindow *button = quitMenuWindow( parent, QUIT_MENU_KEYS[ key ] );
		if( button && !button->winIsHidden() )
			shown.push_back( button );
	}

	const Int keysHeight = (Int)shown.size() * ( keyHeight + gap ) - gap;
	const Int width = keyWidth + 4 * margin;
	const Int height = keysHeight + 4 * margin;
	const Int parentX = ( TheDisplay->getWidth() - width ) / 2;
	const Int parentY = ( TheDisplay->getHeight() - height ) / 2;
	parent->winSetPosition( parentX, parentY );
	parent->winSetSize( width, height );
	for( size_t key = 0; key < shown.size(); key++ )
	{
		shown[ key ]->winSetPosition( 2 * margin, 2 * margin + (Int)key * ( keyHeight + gap ) );
		shown[ key ]->winSetSize( keyWidth, keyHeight );
	}

	well.lo.x = parentX + margin;
	well.lo.y = parentY + margin;
	well.hi.x = parentX + width - margin;
	well.hi.y = parentY + height - margin;
}

//-------------------------------------------------------------------------------------------------
/** The Esc menu from Window/Html/QuitMenu.html, drawn as the menu's plate: every key where its
	* button stands, with the button's own label, so the restart key reads Surrender or Restart Mission
	* as the menu relabelled it, and its state. */
//-------------------------------------------------------------------------------------------------
void InGameUI::drawQuitMenuPage( GameWindow *parent )
{
	if( m_quitMenuOverlay == NULL )
		m_quitMenuOverlay = new HtmlOverlay( m_superweaponNormalFont );

	IRegion2D well;
	layoutQuitMenu( parent, well );

	HtmlValues values;
	HtmlLists lists;
	values[ "side" ] = spectatorSide();
	IRegion2D screen;
	screen.lo.x = screen.lo.y = 0;
	screen.hi.x = TheDisplay->getWidth();
	screen.hi.y = TheDisplay->getHeight();
	putPageRect( values, "screen", screen, TRUE );
	IRegion2D panel;
	putPageRect( values, "panel", panel, controlBarWindowRect( parent, panel ) );
	putPageRect( values, "well", well, TRUE );

	// the coming up starts on the menu's first picture and moves on with the wall clock, but never
	// by more than QUIT_MENU_MOST_MS_A_PICTURE a picture
	const UnsignedInt now = timeGetTime();
	if( m_quitMenuShownMs == QUIT_MENU_NOT_DRAWN )
		m_quitMenuShownMs = 0;
	else
		m_quitMenuShownMs += min( (Int)( now - m_quitMenuDrawnAt ), QUIT_MENU_MOST_MS_A_PICTURE );
	m_quitMenuDrawnAt = now;
	const Int openMs = m_quitMenuShownMs;
	const Int pageAlpha = min( 255, openMs * 255 / QUIT_MENU_FADE_MS );

	// a key still fading in is drawn alone on the same page with the rest of it bare, the page having
	// no opacity of its own for one element; the keys already whole are on the menu's page
	struct FadingKey { HtmlValues entry; Int alpha; };
	std::vector< FadingKey > fading;
	std::vector< HtmlValues > &keys = lists[ "keys" ];
	Int shown = 0;
	for( Int key = 0; key < (Int)ARRAY_SIZE( QUIT_MENU_KEYS ); key++ )
	{
		GameWindow *button = quitMenuWindow( parent, QUIT_MENU_KEYS[ key ] );
		IRegion2D rect;
		if( !controlBarWindowRect( button, rect ) )
			continue;

		const Int keyMs = openMs - QUIT_MENU_KEY_FIRST_MS - shown * QUIT_MENU_KEY_STEP_MS;
		shown++;
		if( keyMs < 0 )
			continue;

		HtmlValues entry;
		putPageRect( entry, "key", rect, TRUE );
		entry[ "state" ] = keyMs < QUIT_MENU_KEY_FLASH_MS ? "flash" : controlBarWindowState( button );
		entry[ "label" ] = WideCharStringToMultiByte( button->winGetInstanceData()->getText().str() );
		if( keyMs < QUIT_MENU_KEY_FADE_MS )
		{
			FadingKey fade;
			fade.entry = entry;
			fade.alpha = keyMs * pageAlpha / QUIT_MENU_KEY_FADE_MS;
			fading.push_back( fade );
		}
		else
			keys.push_back( entry );
	}

	m_quitMenuOverlay->setPage( HtmlTemplate_expand( m_quitMenuPage, values, lists, lookupGameText ) );
	m_quitMenuOverlay->setAlpha( pageAlpha );
	m_quitMenuOverlay->draw();

	values[ "frame" ] = "bare";
	for( size_t each = 0; each < fading.size(); each++ )
	{
		if( m_quitMenuKeyOverlays.size() <= each )
			m_quitMenuKeyOverlays.push_back( new HtmlOverlay( m_superweaponNormalFont ) );
		keys.assign( 1, fading[ each ].entry );
		m_quitMenuKeyOverlays[ each ]->setPage( HtmlTemplate_expand( m_quitMenuPage, values, lists, lookupGameText ) );
		m_quitMenuKeyOverlays[ each ]->setAlpha( fading[ each ].alpha );
		m_quitMenuKeyOverlays[ each ]->draw();
	}
}

//-------------------------------------------------------------------------------------------------
/** Lay out and draw one run of cells, its left edge at left and the first cameo's top edge at
	* bottomY.
	*
	* Playing, the run is a column: it grows upward out of the corner instead of across the bottom of
	* the screen.  The soonest thing to arrive is the bottom cell - the one nearest the command bar
	* and nearest the eye - and everything behind it is stacked above.  Five cells, and whatever is
	* left over closes the column as a sixth wearing a "+N", so the strip's whole footprint is one
	* tray wide however much the base has queued. */
//-------------------------------------------------------------------------------------------------
void InGameUI::drawProductionStripColumn( Int left, Int bottomY )
{
	const Int count = m_productionStripCount;
	if( count < 1 )
		return;

	//
	// Every cameo stands in its own tray - the one the general's powers stand in, in the bottom
	// corner, and this side's own copy of it, read straight off that bar. Every measurement comes
	// off that bar too, at the size the loader gave it: a tray here is the size of a tray there at
	// any resolution, and it is never squeezed to fit anything - squeezed, its rail and its inner
	// frame collapse into a coloured smudge and none of it reads as that bar any more.
	//
	// A cell steps a whole tray whichever way the strip runs, so no cameo has the tray beside it or
	// above it lying over its edge.
	//
	ICoord2D traySize, cameoSize, trayHole;
	Int trayStep = 0;
	stripTrayMetrics( &traySize, &cameoSize, &trayHole, &trayStep );

	const Int trayW = traySize.x;
	const Int trayH = traySize.y;
	// under the bar's page the cameos stand in Queue.html's cells instead, in a row: `left` and
	// `bottomY` are then the first cameo's own corner
	const Int cameoW = m_productionStripThemed ? m_productionStripCameoW : cameoSize.x;
	const Int cameoH = m_productionStripThemed ? m_productionStripCameoH : cameoSize.y;
	// the tray is drawn mirrored, so its hole is mirrored with it: what that bar measures in from
	// the left is the same distance in from the right here
	const Int trayInsetX = trayW - trayHole.x - cameoW;
	const Int trayInsetY = trayHole.y;
	const Int trayX = left;
	const Int x = m_productionStripThemed ? left : left + trayInsetX;		///< where the first cameo starts

	//
	// Which way the cells run.  Either way they step a whole tray, so no cameo has a neighbouring
	// tray lying over its edge.
	//
	const Int cellStepX = m_productionStripThemed ? m_productionStripStep : 0;
	const Int cellStepY = m_productionStripThemed ? 0 : trayH;

	const Image *tray = productionStripTray();

	//
	// what the "+N" stands for is everything the column does not already show - and a cameo wearing
	// an "x5" shows five of them, so the run counts as five here or the same items are reported twice
	//
	Int shown = 0;
	for( Int shownSlot = 0; shownSlot < count; shownSlot++ )
		shown += m_productionStrip[ shownSlot ].quantity;

	const Int hidden = m_productionStripTotal - shown;
	const Int cells = count + ( hidden > 0 ? 1 : 0 );

	//
	// The trays go down first, all of them, and from the far end back: the near one carries the item
	// that arrives next, so it is the one drawn last and the one whose frame is whole.
	//
	// Without the art - a mod that ships no shortcut bar - a slot keeps the flat plate it used to
	// have rather than losing its backing.
	//
	for( Int back = cells - 1; back >= 0 && !m_productionStripThemed; back-- )
	{
		const Int backX = trayX + back * cellStepX;
		const Int backY = bottomY - back * cellStepY - trayInsetY;
		if( tray )
			TheDisplay->drawImage( tray, backX, backY, backX + trayW, backY + trayH );
		else
			TheDisplay->drawFillRect( backX, backY, trayW, trayH, GameMakeColor( 0, 0, 0, 130 ) );
	}

	//
	// What every cameo in the column is to be drawn with, worked out before any of it is drawn.  The
	// column then goes down a piece at a time - every picture, then every scrim, then every border -
	// rather than a cameo at a time, because a run of pieces that ask the renderer for the same
	// thing is one draw call and the same pieces shuffled together are one draw call each.  The
	// cameos do not overlap, so what lands on the screen is exactly what always did.
	//
	struct StripSlotDraw
	{
		Int x;										///< left edge of this cameo
		Int y;										///< top edge of this cameo
		const Image *cameo;				///< its picture, NULL when the template carries none
		Int percent;							///< how much of the scrim has been swept off, -1 for no scrim
		Int seconds;							///< the countdown written across it, -1 for none
		Int quantity;							///< how many of the same item it stands for, 1 for a lone one
		Color border;
		Bool cancelHover;					///< the cursor is on it with ctrl held: this is the one a click cancels
	};
	StripSlotDraw slots[ PRODUCTION_STRIP_ROW_MAX ];

	for( Int i = 0; i < count; i++ )
	{
		ProductionStripSlot *slot = &m_productionStrip[ i ];
		StripSlotDraw *draw = &slots[ i ];

		// the soonest is the near cell: the bottom of a column, the left hand end of a row
		const Int slotX = x + i * cellStepX;
		const Int y = bottomY - i * cellStepY;
		slot->pos.x = slotX;
		slot->pos.y = y;

		draw->x = slotX;
		draw->y = y;
		draw->percent = -1;
		draw->seconds = -1;
		draw->quantity = slot->quantity;
		draw->cancelHover = FALSE;

		Object *producer = TheGameLogic->findObjectByID( slot->producer );
		ProductionUpdateInterface *pu = producer ? producer->getProductionUpdateInterface() : NULL;
		const ProductionEntry *entry = slot->isStructure ? NULL : findStripEntry( pu, slot );
		draw->cameo = stripSlotCameo( producer, entry, slot );

		if( slot->isStructure && producer )
		{
			//
			// A building going up carries its own progress rather than a queue entry's: the same
			// sweep and the same countdown, read off the construction percentage on the object.
			//
			const Real done = producer->getConstructionPercent();
			draw->percent = REAL_TO_INT( done );

			const Int totalFrames =
				producer->getTemplate()->calcTimeToBuild( producer->getControllingPlayer() );
			if( totalFrames > 0 )
			{
				const Real left = totalFrames * ( 1.0f - done / 100.0f );
				draw->seconds = ControlBar_secondsFromFrames( left > 0.0f ? left : 0.0f );
			}
		}
		else if( entry )
		{
			//
			// The scrim starts covering the cameo and is swept off as the item is built, the same
			// way round as the command bar's own clock. Going the other way - filling up with black
			// as progress runs - meant a cameo that had just appeared was drawn clean, and
			// `drawRectClock` refuses to draw at all below one percent, so a fresh order and a
			// nearly finished one looked alike. Only the head of a building's queue has progress;
			// everything behind it is waiting and wears the full scrim.
			//
			const Bool building = ( pu->firstProduction() == entry );
			draw->percent = building ? REAL_TO_INT( entry->getPercentComplete() ) : 0;

			//
			// How long this one still has, written in the middle of it - but only for the item
			// actually being built. The ones behind it in the queue have no clock running: their
			// wait depends on everything in front of them, so a number there would be a guess.
			//
			if( building )
			{
				Player *player = producer->getControllingPlayer();
				Int totalFrames = 0;
				if( slot->isUpgrade )
				{
					if( entry->getProductionUpgrade() )
						totalFrames = entry->getProductionUpgrade()->calcTimeToBuild( player );
				}
				else if( entry->getProductionObject() )
					totalFrames = entry->getProductionObject()->calcTimeToBuild( player );

				if( totalFrames > 0 )
				{
					const Real left = totalFrames * ( 1.0f - entry->getPercentComplete() / 100.0f );
					draw->seconds = ControlBar_secondsFromFrames( left > 0.0f ? left : 0.0f );
				}
			}
		}

		//
		// ctrl is the strip's cancel modifier, so while it is held the cameo under the cursor says
		// so: it goes red and wears a minus. Without it a ctrl-click is a guess about which cameo
		// the cursor is really on, and an accidental cancel costs the whole item.
		//
		// a building already standing on the map is not cancelled from here - it is sold or blown up
		if( !slot->isStructure && TheKeyboard && TheKeyboard->isCtrl() && TheMouse )
		{
			const MouseIO *io = TheMouse->getMouseStatus();
			draw->cancelHover = io && io->pos.x >= draw->x && io->pos.x < draw->x + cameoW &&
													io->pos.y >= draw->y && io->pos.y < draw->y + cameoH;
		}

		// same border colours the command bar puts on its build and upgrade buttons
		draw->border = GameMakeColor( 160, 160, 160, 160 );
		if( draw->cancelHover )
			draw->border = GameMakeColor( 255, 80, 80, 255 );
		else if( TheControlBar )
			draw->border = slot->isUpgrade ? TheControlBar->getUpgradeBorderColor()
																		 : TheControlBar->getBuildBorderColor();
	}

	// the pictures
	for( Int cameoSlot = 0; cameoSlot < count; cameoSlot++ )
	{
		const StripSlotDraw *draw = &slots[ cameoSlot ];
		if( draw->cameo )
			TheDisplay->drawImage( draw->cameo, draw->x, draw->y,
														 draw->x + cameoW, draw->y + cameoH );
	}

	// the sweeps over them
	for( Int clockSlot = 0; clockSlot < count; clockSlot++ )
	{
		const StripSlotDraw *draw = &slots[ clockSlot ];
		if( draw->percent >= 0 )
			TheDisplay->drawRemainingRectClock( draw->x, draw->y, cameoW, cameoH,
																					draw->percent, GameMakeColor( 0, 0, 0, 100 ) );
	}

	// the countdowns written on them
	for( Int secondsSlot = 0; secondsSlot < count; secondsSlot++ )
	{
		const StripSlotDraw *draw = &slots[ secondsSlot ];
		if( draw->seconds >= 0 )
			drawStripSeconds( secondsSlot,
												draw->x, draw->y, cameoW, cameoH, draw->seconds );
	}

	// how many of the same thing each one stands for, in the corner the countdown leaves free
	for( Int quantitySlot = 0; quantitySlot < count; quantitySlot++ )
	{
		const StripSlotDraw *draw = &slots[ quantitySlot ];
		if( draw->quantity > 1 )
			drawStripQuantity( quantitySlot,
												 draw->x, draw->y, cameoW, draw->quantity );
	}

	// and the borders round them; the themed row's frames are Queue.html's
	for( Int borderSlot = 0; borderSlot < count && !m_productionStripThemed; borderSlot++ )
	{
		const StripSlotDraw *draw = &slots[ borderSlot ];
		TheDisplay->drawOpenRect( draw->x, draw->y, cameoW, cameoH, 1.0f, draw->border );
	}

	for( Int hoverSlot = 0; hoverSlot < count; hoverSlot++ )
	{
		const StripSlotDraw *draw = &slots[ hoverSlot ];
		if( draw->cancelHover )
		{
			const Int barInset = cameoW / 4;
			const Int barHeight = stripPixels( 4 );

			TheDisplay->drawFillRect( draw->x, draw->y, cameoW, cameoH,
																GameMakeColor( 190, 0, 0, 120 ) );
			TheDisplay->drawFillRect( draw->x + barInset, draw->y + ( cameoH - barHeight ) / 2,
																cameoW - 2 * barInset, barHeight,
																GameMakeColor( 255, 255, 255, 255 ) );
		}
	}

	//
	// whatever did not fit closes the run with a "+N" at its far end - the top of a column, the
	// right hand end of a row, which is the end each is read towards.  It is not clickable -
	// there is no one item behind it, and what it stands for is already reachable by selecting the
	// building.
	//
	if( hidden > 0 )
	{
		// the column's own "+N" - see m_stripSecondsString
		DisplayString *&overflow = m_productionStripOverflow[ STRIP_OVERFLOW_PRODUCTION ];

		if( overflow == NULL )
		{
			overflow = TheDisplayStringManager->newDisplayString();
			overflow->setFont( TheFontLibrary->getFont( m_superweaponNormalFont,
													TheGlobalLanguageData->adjustFontSize( HUD_OVERLAY_POINT_SIZE ),
													TRUE ) );
		}

		UnicodeString text;
		text.format( L"+%d", hidden );
		overflow->setText( text );

		Int textWidth = 0, textHeight = 0;
		overflow->getSize( &textWidth, &textHeight );

		const Int moreX = x + count * cellStepX;
		const Int moreY = bottomY - count * cellStepY;

		overflow->draw( moreX + ( cameoW - textWidth ) / 2,
										moreY + ( cameoH - textHeight ) / 2,
										GameMakeColor( 235, 235, 235, 255 ),
										GameMakeColor( 0, 0, 0, 255 ) );
	}
}

//-------------------------------------------------------------------------------------------------
/** Playing under the bar's page, the strip is a row in the page's steel, Window/Html/Queue.html: a
	* tray on the radar's side as the general's powers' is on the portrait's, standing over the
	* under-attack light's tab with its first cell against the screen's left edge and the row growing
	* right.  Each cell is a power's size, the soonest first, the "+N" in a cell of its own at the end,
	* and the page's frames are drawn again over the cameos' edges the way the command grid's are. */
//-------------------------------------------------------------------------------------------------
void InGameUI::drawQueueTray( void )
{
	const Real scale = ControlBarUniformScale();
	const IRegion2D button = commandButtonRect( 1 );
	ICoord2D cell;
	cell.x = ( button.hi.x - button.lo.x ) * SKILL_CELL_PERCENT / 100;
	cell.y = ( button.hi.y - button.lo.y ) * SKILL_CELL_PERCENT / 100;
	const Int gap = REAL_TO_INT( SKILL_CELL_GAP * scale );
	const Int trayBorder = REAL_TO_INT( QUEUE_TRAY_BORDER * scale );

	IRegion2D content;
	controlBarUnion( CONTROL_BAR_LEFT, content );
	const IRegion2D alertTab = tabOn( framed( content, REAL_TO_INT( PANEL_BORDER * scale ), TRUE, FALSE ),
																		ALERT_TAB_WIDTH, PANEL_TAB_HEIGHT, FALSE );

	// a cameo wearing an "x5" stands for five, and the "+N" is what none of them shows
	Int shown = 0;
	for( Int slot = 0; slot < m_productionStripCount; slot++ )
		shown += m_productionStrip[ slot ].quantity;
	const Int cells = m_productionStripCount + ( m_productionStripTotal > shown ? 1 : 0 );

	IRegion2D cellsBox;
	cellsBox.lo.x = trayBorder;
	cellsBox.hi.y = alertTab.lo.y - REAL_TO_INT( SKILL_GRID_GAP * scale ) - trayBorder;
	cellsBox.lo.y = cellsBox.hi.y - cell.y;
	cellsBox.hi.x = cellsBox.lo.x + cells * cell.x + ( cells - 1 ) * gap;
	IRegion2D tray = cellsBox;
	tray.lo.x = 0;
	tray.lo.y -= trayBorder;
	tray.hi.x += trayBorder;
	tray.hi.y += trayBorder;
	m_queueTrayTop = tray.lo.y;

	m_productionStripCameoW = cell.x;
	m_productionStripCameoH = cell.y;
	m_productionStripStep = cell.x + gap;

	HtmlValues values;
	HtmlLists lists;
	values[ "side" ] = spectatorSide();
	putFrame( values, "tray", cellsBox, tray, TRUE );
	std::vector< HtmlValues > &cellList = lists[ "cells" ];
	for( Int each = 0; each < cells; each++ )
	{
		IRegion2D place;
		place.lo.x = cellsBox.lo.x + each * m_productionStripStep;
		place.lo.y = cellsBox.lo.y;
		place.hi.x = place.lo.x + cell.x;
		place.hi.y = cellsBox.hi.y;
		HtmlValues entry;
		putCell( entry, place, pageRing( gap ) );
		cellList.push_back( entry );
	}

	if( m_queueOverlay == NULL )
	{
		m_queueOverlay = new HtmlOverlay( m_superweaponNormalFont );
		m_queueFrontOverlay = new HtmlOverlay( m_superweaponNormalFont );
	}
	values[ "layer" ] = "back";
	m_queueOverlay->setPage( HtmlTemplate_expand( m_queuePage, values, lists, lookupGameText ) );
	m_queueOverlay->draw();

	TheDisplay->beginBatch2D();
	drawProductionStripColumn( cellsBox.lo.x, cellsBox.lo.y );
	TheDisplay->endBatch2D();

	values[ "layer" ] = "front";
	m_queueFrontOverlay->setPage( HtmlTemplate_expand( m_queuePage, values, lists, lookupGameText ) );
	m_queueFrontOverlay->draw();
}

#ifdef DEBUG_LOGGING
extern Real TheStripGatherMS;
extern Real TheStripDrawMS;

static Real stripElapsedMS( const Int64 &from, const Int64 &to )
{
	Int64 freq;
	QueryPerformanceFrequency( (LARGE_INTEGER *)&freq );
	if( freq == 0 )
		return 0.0f;
	return (Real)((double)( to - from ) * 1000.0 / (double)freq );
}
#endif

//-------------------------------------------------------------------------------------------------
void InGameUI::drawProductionStrip( void )
{
#ifdef DEBUG_LOGGING
	Int64 tGatherStart, tGatherEnd, tDrawEnd;
	QueryPerformanceCounter( (LARGE_INTEGER *)&tGatherStart );
	tGatherEnd = tGatherStart;
	tDrawEnd = tGatherStart;
	TheStripGatherMS = 0.0f;
	TheStripDrawMS = 0.0f;
#endif

	m_productionStripCount = 0;
	m_productionStripTotal = 0;

	if( TheGameLogic == NULL || !TheGameLogic->isInGame() || TheGameLogic->isInShellGame() )
		return;

	Player *player = ThePlayerList ? ThePlayerList->getLocalPlayer() : NULL;
	if( player == NULL )
		return;

	// watching, everybody's queue is on his row of the Tab scoreboard, not down the left edge
	if( !player->isPlayerActive() )
		return;

	//
	// A single selected producer leads the column rather than getting one of its own: what the
	// building you are looking at is making is the front of the strip, and the rest of the base
	// follows it. It goes in before the sweep over everything else, and the sweep skips it, so
	// nothing is drawn or counted twice.
	//
	ObjectID selected = INVALID_ID;
	if( getSelectCount() == 1 && !m_selectedDrawables.empty() )
	{
		Object *sel = m_selectedDrawables.front()->getObject();
		if( sel && sel->getControllingPlayer() == player )
		{
			selected = sel->getID();
			appendProducerQueue( sel, m_productionStrip, &m_productionStripCount, PRODUCTION_STRIP_ROW_MAX,
													 &m_productionStripTotal, TRUE );
		}
	}

	//
	// One sweep, one column: the queues and the buildings going up are gathered into the same
	// cells and sorted against each other, so the strip is a single run of what the base has
	// coming.  A dozer raising a war factory now sits in the queue where its finishing time puts
	// it instead of standing in a column of its own beside it.
	//
	ProductionStripGather gather;
	gather.slot = m_productionStrip;
	gather.count = &m_productionStripCount;
	gather.total = &m_productionStripTotal;
	gather.max = PRODUCTION_STRIP_ROW_MAX;
	gather.skip = selected;
	player->iterateObjects( gatherStripEverything, &gather );

#ifdef DEBUG_LOGGING
	QueryPerformanceCounter( (LARGE_INTEGER *)&tGatherEnd );
	TheStripGatherMS = stripElapsedMS( tGatherStart, tGatherEnd );
#endif

	if( m_productionStripCount == 0 )
		return;

	//
	// sit on top of the control bar. If the bar is not up (it is hidden while the game is loading,
	// and in the observer views) fall back to the bottom of the screen.
	//
	const Int barTop = controlBarTop();

	//
	// A queue cameo is one cameo of the general's power bar, to the pixel: the row is built out of
	// that bar's own trays, so the pictures standing in them are the size of the pictures standing
	// in it.
	//
	ICoord2D traySize, cameoSize, trayHole;
	Int trayStep = 0;
	stripTrayMetrics( &traySize, &cameoSize, &trayHole, &trayStep );

	m_productionStripCameoW = cameoSize.x;
	m_productionStripCameoH = cameoSize.y;

	//
	// Playing, the queue is a column standing on the corner just above the control bar and growing
	// upward: the next thing to arrive is the bottom cell, always in the same place, and the rest
	// of the queue is stacked over it.  Six cells is the whole of it, so the strip is one tray wide
	// however deep the base's queue goes - across the bottom of the screen it used to run over the
	// battlefield instead.  Cells step a whole tray, so none of them is clipped by the one above.
	//
	const Int trayBelow = traySize.y - trayHole.y;
	const Int lowerY = barTop - trayBelow - stripPixels( PRODUCTION_STRIP_LIFT );

	// playing under the bar's page, the one run is a row in the page's steel instead
	if( !m_queuePageLoaded )
	{
		m_queuePageLoaded = TRUE;
		readHtmlPage( QUEUE_PAGE, m_queuePage );
	}
	m_productionStripThemed = m_controlBarPageShown && !m_queuePage.empty();
	if( m_productionStripThemed )
		drawQueueTray();
	else
	{
		//
		// The strip is the busiest thing on the screen that is drawn a quad at a time, so it is drawn
		// as a batch: the column hands the display its pieces in an order that lets it gather the
		// ones asking for the same state into single draw calls.  See Display::beginBatch2D.
		//
		TheDisplay->beginBatch2D();
		drawProductionStripColumn( 0, lowerY );
		TheDisplay->endBatch2D();
	}

#ifdef DEBUG_LOGGING
	QueryPerformanceCounter( (LARGE_INTEGER *)&tDrawEnd );
	TheStripDrawMS = stripElapsedMS( tGatherEnd, tDrawEnd );
#endif
}

//-------------------------------------------------------------------------------------------------
Bool InGameUI::handleProductionStripClick( const ICoord2D *mouse, Bool cancel )
{
	if( mouse == NULL )
		return FALSE;

	// the command bar's own buttons and the strip drop-down lie over the world the same way the strip
	// does, so their clicks come here too
	if( handleControlBarPageClick( mouse, !cancel ) )
		return TRUE;
	if( handleSpectatorPageClick( mouse, !cancel ) )
		return TRUE;

	for( Int i = 0; i < m_productionStripCount; i++ )
	{
		const ProductionStripSlot *slot = &m_productionStrip[ i ];

		if( mouse->x < slot->pos.x || mouse->x >= slot->pos.x + m_productionStripCameoW ||
				mouse->y < slot->pos.y || mouse->y >= slot->pos.y + m_productionStripCameoH )
			continue;

		Object *producer = TheGameLogic->findObjectByID( slot->producer );
		if( producer == NULL )
			return TRUE;				// the building died under the cursor - the click is still ours

		if( cancel && !slot->isStructure && producer->isLocallyControlled() )
		{
			//
			// the producer travels with the message: the strip cancels on buildings that are not
			// selected, so the logic cannot work out whose queue this is otherwise
			//
			GameMessage *msg = TheMessageStream->appendMessage( slot->isUpgrade
																												 ? GameMessage::MSG_CANCEL_UPGRADE
																												 : GameMessage::MSG_CANCEL_UNIT_CREATE );
			msg->appendIntegerArgument( slot->id );
			msg->appendObjectIDArgument( slot->producer );
		}
		else
		{
			TheTacticalView->lookAt( producer->getPosition() );
		}

		return TRUE;
	}

	return FALSE;
}

void InGameUI::drawFloatingText( void )
{
	FloatingTextData *ftd;
	// loop through and draw all the texts
	for(FloatingTextListIt it = m_floatingTextList.begin(); it != m_floatingTextList.end(); ++it)
	{
		ftd = *it;
		ICoord2D pos;
		// whose fog the screen is drawn in
		Int playerNdx = TheObserverCamera.getShroudPlayerIndex();

		// which PartitionManager cells are we looking at?
		Int pCX, pCY;
		ThePartitionManager->worldToCell(ftd->m_pos3D.x, ftd->m_pos3D.y, &pCX, &pCY);

		// translate it's 3d pos into a 2d screen pos
		if( TheTacticalView->worldToScreen(&ftd->m_pos3D, &pos)
			&& ftd->m_dString
			&& ThePartitionManager->getShroudStatusForPlayer(playerNdx, pCX, pCY) == CELLSHROUD_CLEAR )
		{
			Color dropColor;
			UnsignedByte r, g, b, a;
			Int width, height;

			// make drop color black, but use the alpha setting of the fill color specified (for fading)
			GameGetColorComponents( ftd->m_color, &r, &g, &b, &a );
			dropColor = GameMakeColor( 0, 0, 0, a );
			ftd->m_dString->getSize(&width, &height);

			pos.y -= ftd->m_frameCount * m_floatingTextMoveUpSpeed;
			// draw it!
			ftd->m_dString->draw(pos.x - (width / 2), pos.y, ftd->m_color,dropColor);
		}

	}
}

//-------------------------------------------------------------------------------------------------
/** ittereate through and clear out the list of floating text */
//-------------------------------------------------------------------------------------------------
void InGameUI::clearFloatingText( void )
{
	FloatingTextData *ftd;
	// loop through and draw all the texts
	for(FloatingTextListIt it = m_floatingTextList.begin(); it != m_floatingTextList.end();)
	{
		ftd = *it;
		it = m_floatingTextList.erase(it);
		ftd->deleteInstance();
	}
	
}

//-------------------------------------------------------------------------------------------------
/** If we want to use the default text color, then we call this function */
//-------------------------------------------------------------------------------------------------
void InGameUI::popupMessage( const AsciiString& message, Int x, Int y, Int width, Bool pause, Bool pauseMusic)
{
	popupMessage( message, x, y, width, m_popupMessageColor, pause, pauseMusic);
}

//-------------------------------------------------------------------------------------------------
/** initialize, and popup a message box to the user */
//-------------------------------------------------------------------------------------------------
void InGameUI::popupMessage( const AsciiString& identifier, Int x, Int y, Int width, Color textColor, Bool pause, Bool pauseMusic)
{
	if(m_popupMessageData)
		clearPopupMessageData();

	UpdateDiplomacyBriefingText(identifier, FALSE);

	UnicodeString message = TheGameText->fetch(identifier);

	m_popupMessageData = newInstance( PopupMessageData );	
	m_popupMessageData->message = message;
	// x and why are passed in as a percentage of the screen, convert to screen coords
	if( x > 100 )
		x = 100;
	if( x < 0 )
		x = 0;
	
	if( y > 100 )
		y = 100;
	if( y < 0 )
		y = 0;

	m_popupMessageData->x = TheDisplay->getWidth() * (INT_TO_REAL(x) / 100);
	m_popupMessageData->y = TheDisplay->getHeight() * (INT_TO_REAL(y) / 100);
	// cap the lower limit of the width
	if(width < 50)
		width = 50;
	m_popupMessageData->width = width;
	m_popupMessageData->textColor = textColor;
	m_popupMessageData->pause = pause;
	m_popupMessageData->pauseMusic = pauseMusic;

	if( pause )
		TheGameLogic->setGamePaused(TRUE, pauseMusic);

	m_popupMessageData->layout = TheWindowManager->winCreateLayout(AsciiString("InGamePopupMessage.wnd"));
	m_popupMessageData->layout->runInit();
}

//-------------------------------------------------------------------------------------------------
/** take care of the logic of clearing the popupMessageData */
//-------------------------------------------------------------------------------------------------
void InGameUI::clearPopupMessageData( void )
{
	if(!m_popupMessageData)
		return;
	if(m_popupMessageData->layout)
	{
		m_popupMessageData->layout->destroyWindows();
		m_popupMessageData->layout->deleteInstance();
		m_popupMessageData->layout = NULL;
	}
	if( m_popupMessageData->pause )
		TheGameLogic->setGamePaused(FALSE, m_popupMessageData->pauseMusic);
	m_popupMessageData->deleteInstance();
	m_popupMessageData = NULL;
	
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------


//-------------------------------------------------------------------------------------------------
/** Floating Text Constructor */
//-------------------------------------------------------------------------------------------------
FloatingTextData::FloatingTextData(void)
{
	// Added By Sadullah Nader
	// Initializations missing and needed
	m_color = 0;
	m_frameCount = 0;
	m_frameTimeOut = 0;
	m_pos3D.zero();
	m_text.clear();
	//
	m_dString = TheDisplayStringManager->newDisplayString();
}

//-------------------------------------------------------------------------------------------------
/** Floating Text Destructor */
//-------------------------------------------------------------------------------------------------
FloatingTextData::~FloatingTextData(void)
{
	if(m_dString)
		TheDisplayStringManager->freeDisplayString( m_dString );
	m_dString = NULL;
}

///////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////
// WORLD ANIMATION DATA ///////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
WorldAnimationData::WorldAnimationData( void )
{

	m_anim = NULL;
	m_worldPos.zero();
	m_expireFrame = 0;
	m_options = WORLD_ANIM_NO_OPTIONS;
	m_zRisePerSecond = 0.0f;

}  // end WorldAnimationData

// ------------------------------------------------------------------------------------------------
/** Add a 2D animation at a spot in the world */
// ------------------------------------------------------------------------------------------------
void InGameUI::addWorldAnimation( Anim2DTemplate *animTemplate,
																	const Coord3D *pos,
																	WorldAnimationOptions options,
																	Real durationInSeconds,
																	Real zRisePerSecond )
{

	// sanity
	if( animTemplate == NULL || pos == NULL || durationInSeconds <= 0.0f )
		return;

	// allocate a new world animation data struct
	// (huh huh, he said "wad")
	WorldAnimationData *wad = NEW WorldAnimationData;
	if( wad == NULL )
		return;		

	// allocate a new animation instance
	Anim2D *anim = newInstance(Anim2D)( animTemplate, TheAnim2DCollection );

	// assign all data
	wad->m_anim = anim;
	wad->m_expireFrame = TheGameLogic->getFrame() + (durationInSeconds * LOGICFRAMES_PER_SECOND);
	wad->m_options = options;
	wad->m_worldPos = *pos;
	wad->m_zRisePerSecond = zRisePerSecond;

	// add to list
	m_worldAnimationList.push_front( wad );

}  // end addWorldAnimation

// ------------------------------------------------------------------------------------------------
/** Delete all world animations */
// ------------------------------------------------------------------------------------------------
void InGameUI::clearWorldAnimations( void )
{
	WorldAnimationData *wad;

	// iterate through all entries and delete the animation data
	for( WorldAnimationListIterator it = m_worldAnimationList.begin();	
			 it != m_worldAnimationList.end(); /*empty*/ )
	{

		wad = *it;
		if( wad )
		{

			// delete the animation instance
			wad->m_anim->deleteInstance();

			// delete the world animation data
			delete wad;

		}  // end if

		it = m_worldAnimationList.erase( it );

	}  // end for
	
}  // end clearWorldAnimations

static const UnsignedInt FRAMES_BEFORE_EXPIRE_TO_FADE = LOGICFRAMES_PER_SECOND * 1;
// ------------------------------------------------------------------------------------------------
/** Update all world animations and draw the visible ones */
// ------------------------------------------------------------------------------------------------
void InGameUI::updateAndDrawWorldAnimations( void )
{
	WorldAnimationData *wad;

	// go through all animations
	for( WorldAnimationListIterator it = m_worldAnimationList.begin();
			 it != m_worldAnimationList.end(); /*empty*/ )
	{

		// get data
		wad = *it;

		// update portion ... only when the game is in motion
		if( wad && TheGameLogic->isGamePaused() == FALSE )
		{

			//
			// see if it's time to expire this animation based on animation type and options or
			// the expire frame
			//
			if( TheGameLogic->getFrame() >= wad->m_expireFrame ||
					(BitTest( wad->m_options, WORLD_ANIM_PLAY_ONCE_AND_DESTROY ) &&
					 BitTest( wad->m_anim->getStatus(), ANIM_2D_STATUS_COMPLETE )) )
			{

				// delete this element and continue
				wad->m_anim->deleteInstance();
				delete wad;
				it = m_worldAnimationList.erase( it );
				continue;

			}  // end if

			// update the Z value
			if( wad->m_zRisePerSecond )
				wad->m_worldPos.z += wad->m_zRisePerSecond / LOGICFRAMES_PER_SECOND;

		}  // end if

		//
		// don't bother going forward with the draw process if this location is shrouded for
		// the local player
		//
		Int playerIndex = TheObserverCamera.getShroudPlayerIndex();
		if( ThePartitionManager->getShroudStatusForPlayer( playerIndex, &wad->m_worldPos ) != CELLSHROUD_CLEAR )
		{

			++it;
			continue;

		}  // end if

		// update translucency value
		if( BitTest( wad->m_options, WORLD_ANIM_FADE_ON_EXPIRE ) )
		{

			// see if we should be setting the translucency value
			UnsignedInt framesTillExpire = wad->m_expireFrame - TheGameLogic->getFrame();
			if( framesTillExpire < FRAMES_BEFORE_EXPIRE_TO_FADE )
			{
				
				// compute alpha level so that we're totally gone by the expire frame
				Real alpha = INT_TO_REAL( framesTillExpire ) / INT_TO_REAL( FRAMES_BEFORE_EXPIRE_TO_FADE );
				wad->m_anim->setAlpha( alpha );
				
			}  // end if

		}  // end if

		// project the point to screen space
		ICoord2D screen;
		if( TheTacticalView->worldToScreen( &wad->m_worldPos, &screen ) == TRUE )
		{
			UnsignedInt width = wad->m_anim->getCurrentFrameWidth();
			UnsignedInt height = wad->m_anim->getCurrentFrameHeight();

			// scale the width and height given the camera zoom level.  The 1.3 was the old maximum
			// zoom value, which is gone; this number is the size the artwork was drawn against.
			const Real WORLD_ANIM_ZOOM_REFERENCE = 1.3f;
			Real zoomScale = WORLD_ANIM_ZOOM_REFERENCE / TheTacticalView->getZoom();
			width *= zoomScale;
			height *= zoomScale;

			// adjust the screen position to draw so the image is centered at the location
			screen.x -= width / 2;
			screen.y -= height / 2;

			// draw the animation
			wad->m_anim->draw( screen.x, screen.y, width, height );

		}  // end if

		// go to the next element in the list
		++it;

	}  // end for

}  // end updateAndDrawWorldAnimations


Object *InGameUI::findIdleWorker( Object *obj)
{
	if(!obj)
		return NULL;
	
	Int index = obj->getControllingPlayer()->getPlayerIndex();	
	if(m_idleWorkers[index].empty())
		return NULL;

	ObjectListIt it = m_idleWorkers[index].begin();
	while(it != m_idleWorkers[index].end())
	{
		Object *itObj = *it;
		if(itObj == obj)
		{
			return itObj;
			break;
		}
		++it;
	}
	return NULL;
}

void InGameUI::addIdleWorker( Object *obj )
{
	if(!obj)
		return;

	if(findIdleWorker(obj))
		return;

	Int index = obj->getControllingPlayer()->getPlayerIndex();
	m_idleWorkers[index].push_back(obj);
}

void InGameUI::removeIdleWorker( Object *obj, Int playerNumber )
{
	if(!obj)
		return;
	if(playerNumber < 0 || playerNumber >= MAX_PLAYER_COUNT)  // we're leaving the game, so this is all screwed
		return;
	
	if(m_idleWorkers[playerNumber].empty())
		return;

	
	ObjectListIt it = m_idleWorkers[playerNumber].begin();
	while(it != m_idleWorkers[playerNumber].end())
	{
		Object *itObj = *it;
		if(itObj == obj)
		{
			m_idleWorkers[playerNumber].erase(it);
			return;
		}
		++it;
	}
	return;
}

void InGameUI::selectNextIdleWorker( void )
{
	Int index = ThePlayerList->getLocalPlayer()->getPlayerIndex();
	if(m_idleWorkers[index].empty())
	{
		DEBUG_ASSERTCRASH(FALSE, ("InGameUI::selectNextIdleWorker We're trying to select a worker when our list is empty for player %ls", ThePlayerList->getLocalPlayer()->getPlayerDisplayName().str()));
		return;
	}
	Object *selectThisObject = NULL;
	
	if(getSelectCount() == 0 || getSelectCount() > 1)
	{
		selectThisObject = *m_idleWorkers[index].begin();
	}
	else
	{
		Drawable *selectedDrawable = TheInGameUI->getFirstSelectedDrawable();	
		
		ObjectListIt it = m_idleWorkers[index].begin();
		while(it != m_idleWorkers[index].end())
		{
			Object *itObj = *it;
			if(itObj == selectedDrawable->getObject())
			{
				++it;
				if(it != m_idleWorkers[index].end())
					selectThisObject = *it;
				else
					selectThisObject = *m_idleWorkers[index].begin();
				break;
			}
			++it;
		}
		// if we had something selected that wasn't a worker, we'll get here
		if(!selectThisObject)
			selectThisObject = *m_idleWorkers[index].begin();

	}
	DEBUG_ASSERTCRASH(selectThisObject, ("InGameUI::selectNextIdleWorker Could not select the next IDLE worker"));
	if(selectThisObject)
	{	
		
		//If our idle worker is contained by anything, we need to select the container instead.
		Object *containedBy = selectThisObject->getContainedBy();
		if( containedBy )
		{
			selectThisObject = containedBy;
		}

		deselectAllDrawables();
		GameMessage *teamMsg = TheMessageStream->appendMessage( GameMessage::MSG_CREATE_SELECTED_GROUP );


		//New group or add to group? Passed in value is true if we are creating a new group.
		teamMsg->appendBooleanArgument( TRUE );

		teamMsg->appendObjectIDArgument( selectThisObject->getID() );
		
		selectDrawable( selectThisObject->getDrawable() );

		/*// removed becuase we're already playing a select sound... left in, just in case i"m wrong.
		// play the units sound
				const AudioEventRTS *soundEvent = selectThisObject->getTemplate()->getVoiceSelect();
				if (soundEvent)
				{
					TheAudio->addAudioEvent( soundEvent );
				}*/
		
		// center on the unit
		TheTacticalView->lookAt(selectThisObject->getPosition());
	}
}

Int InGameUI::getIdleWorkerCount( void )
{
	Int index = ThePlayerList->getLocalPlayer()->getPlayerIndex();
	return m_idleWorkers[index].size();
}

void InGameUI::showIdleWorkerLayout( void )
{
	if (!m_idleWorkerWin)
	{
		m_idleWorkerWin = TheWindowManager->winGetWindowFromId(NULL, TheNameKeyGenerator->nameToKey("ControlBar.wnd:ButtonIdleWorker"));
		DEBUG_ASSERTCRASH(m_idleWorkerWin, ("InGameUI::showIdleWorkerLayout could not find IdleWorker.wnd to load "));
		return;
	}

	m_idleWorkerWin->winEnable(TRUE);

	m_currentIdleWorkerDisplay = getIdleWorkerCount();
	
//	if(m_currentIdleWorkerDisplay < 1)
//		GadgetButtonSetText(m_idleWorkerWin, UnicodeString::TheEmptyString);
//	else
//	{
//		UnicodeString number;
//		number.format(L"%d",m_currentIdleWorkerDisplay);
//		GadgetButtonSetText(m_idleWorkerWin, number);
//	}
}
void InGameUI::hideIdleWorkerLayout( void )
{
	if(!m_idleWorkerWin)
		return;
	GadgetButtonSetText(m_idleWorkerWin, UnicodeString::TheEmptyString);
	m_idleWorkerWin->winEnable(FALSE);
	m_currentIdleWorkerDisplay = -1;
}

void InGameUI::updateIdleWorker( void )
{
	Int idleCount = getIdleWorkerCount();

	if(idleCount > 0 && m_currentIdleWorkerDisplay != idleCount && getInputEnabled())
		showIdleWorkerLayout();

	if((idleCount <= 0 && m_idleWorkerWin) || !getInputEnabled())
		hideIdleWorkerLayout();
}

void InGameUI::resetIdleWorker( void )
{
	if(m_idleWorkerWin)
	{
		GadgetButtonSetText(m_idleWorkerWin, UnicodeString::TheEmptyString);
	}
	m_currentIdleWorkerDisplay = -1;
	for(Int i = 0; i < MAX_PLAYER_COUNT; ++i)
	{
		m_idleWorkers[i].clear();
	}

}

void InGameUI::recreateControlBar( void )
{
	//
	// deleteInstance() hands the block back to the pool without taking the window off the manager's
	// list and without touching its children, so the freed root stayed in m_windowList and the pool
	// then handed the same block to the root created two lines later.  The list was a ring of
	// half-freed windows after that, and the first winRepaint that reached one drew a push button
	// whose overlay image was whatever the block now held.  Out of a match nothing showed because
	// the bar is hidden and winRepaint skips hidden windows; in one it was the crash on changing
	// resolution.  winDestroy unlinks the whole tree and defers the free to the window manager's own
	// pass, which is also what keeps the pointers below readable until then.
	//
	// A window's id is the key of its full decorated name, and the bar's root is called
	// ControlBar.wnd:ControlBarParent - so a lookup on "ControlBar.wnd" matched nothing and the old
	// bar was never destroyed at all.  Every rebuild left its predecessor on the window list, drawn
	// and clickable at the size of the screen it was built for, and a resolution change makes two of
	// them in a row: the command bar stacked two and three deep over the battlefield.  Every root
	// with that id goes, not the first, so a run that already collected some is cleaned out.
	//
	// winDestroy rather than deleteInstance: deleteInstance hands the block back to the pool without
	// taking the window off the manager's list and without touching its children, so the freed root
	// stayed in m_windowList and the pool then handed the same block to the root created two lines
	// later.  The list was a ring of half-freed windows after that, and the first winRepaint that
	// reached one drew a push button whose overlay image was whatever the block now held.  winDestroy
	// unlinks the whole tree and defers the free to the window manager's own pass, which is also what
	// keeps the pointers below readable until then - and it relinks m_next into the destroy list, so
	// the walk has to take the next window before it destroys this one.
	//
	const NameKeyType controlBarRootID =
		TheNameKeyGenerator->nameToKey( AsciiString( "ControlBar.wnd:ControlBarParent" ) );

	GameWindow *nextWindow = NULL;
	for( GameWindow *window = TheWindowManager->winGetWindowList(); window; window = nextWindow )
	{
		nextWindow = window->winGetNext();
		if( window->winGetWindowId() == controlBarRootID )
			TheWindowManager->winDestroy( window );
	}

	m_idleWorkerWin = NULL;

	// the money gadget goes with those windows, and the new one carries whatever text its .wnd
	// ships with until update() is told the amount it is showing is not the amount the player has
	m_lastMoneyDisplayed = -1;

	createControlBar();

	//
	// The bar is rebuilt from its windows rather than replaced.  Replacing it re-parsed
	// CommandButton.ini and CommandSet.ini and handed out new CommandButton addresses, while every
	// const CommandButton * already held by something in the running game - a production queue, a
	// hunt update, the partition manager, the academy - went on pointing at the freed ones.
	//
	if(TheControlBar)
	{
		TheControlBar->initWindows();

		//
		// The two things init() cannot do for itself: the shortcut strip belongs to one general, and
		// the scheme is that general's artwork.  Both were applied to windows that no longer exist.
		//
		if(TheGameLogic->isInGame() && !TheGameLogic->isInShellGame())
		{
			Player *localPlayer = ThePlayerList->getLocalPlayer();
			TheControlBar->initSpecialPowershortcutBar(localPlayer);
			TheControlBar->setControlBarSchemeByPlayer(localPlayer);
		}
	}

}

//
// Everything a running match has on screen that is not the shell and not the command bar.  Each of
// these was stretched to the resolution it was created at and none of them is rebuilt by the shell
// going away, so after a mode change the diplomacy panel, the chat line and the replay controls
// were still wearing the old screen's geometry - drawn and clicked a proportion of a screen away
// from where they belong, or off the edge of a smaller one.  Diplomacy and chat are thrown away and
// come back the next time the player asks for them; the replay controls are put back here because
// nothing else builds them.
//
void InGameUI::notifyResolutionChange( void )
{
	recreateControlBar();

	ResetDiplomacy();
	ResetInGameChat();

	if( m_replayWindow )
	{
		const Bool wasHidden = m_replayWindow->winIsHidden();

		TheWindowManager->winDestroy( m_replayWindow );
		createReplayControl();
		m_replayWindow->winHide( wasHidden );
	}

	RecreateQuitMenu();
}

static const char *const TOOLTIP_PAGE = "Window\\Html\\Tooltip.html";

enum
{
	TOOLTIP_ANCHOR_GAP		= 6,		///< page pixels between the build tooltip and the button it describes
	TOOLTIP_LABEL_LIMIT		= 28,		///< a description line "Label: value" is a row when its label is no longer
	TOOLTIP_LAYOUT_PASSES	= 2,		///< laid out again once when the box came out another size than it was placed by
};

//-------------------------------------------------------------------------------------------------
/** A tooltip text as data-each="lines": {{kind}} "row" with {{label}} and {{value}} for a line that
	* reads "Label: value" - the string table's "Strong: infantry", "Energy Provided: 5" - and "text"
	* with {{text}} for any other.  A run of blank lines is one entry of kind "gap", and none opens or
	* closes the list. */
//-------------------------------------------------------------------------------------------------
static void putTooltipLines( const UnicodeString &text, std::vector< HtmlValues > &lines )
{
	const std::wstring whitespace = L" \t\r";
	const std::wstring whole = text.str();
	Bool gapOwed = FALSE;
	for( size_t start = 0; start <= whole.size(); )
	{
		size_t end = whole.find( L'\n', start );
		if( end == std::wstring::npos )
			end = whole.size();
		std::wstring line = whole.substr( start, end - start );
		start = end + 1;

		const size_t first = line.find_first_not_of( whitespace );
		if( first == std::wstring::npos )
		{
			gapOwed = !lines.empty();
			continue;
		}
		line = line.substr( first, line.find_last_not_of( whitespace ) - first + 1 );

		if( gapOwed )
		{
			HtmlValues gap;
			gap[ "kind" ] = "gap";
			lines.push_back( gap );
			gapOwed = FALSE;
		}

		HtmlValues entry;
		const size_t colon = line.find( L':' );
		const size_t valueStart = colon == std::wstring::npos ? colon : line.find_first_not_of( whitespace, colon + 1 );
		if( colon != std::wstring::npos && colon <= TOOLTIP_LABEL_LIMIT && valueStart != std::wstring::npos )
		{
			entry[ "kind" ] = "row";
			entry[ "label" ] = WideCharStringToMultiByte( line.substr( 0, colon ).c_str() );
			entry[ "value" ] = WideCharStringToMultiByte( line.substr( valueStart ).c_str() );
		}
		else
		{
			entry[ "kind" ] = "text";
			entry[ "text" ] = WideCharStringToMultiByte( line.c_str() );
		}
		lines.push_back( entry );
	}
}

//-------------------------------------------------------------------------------------------------
/** A button label as a name: the '&' that marks its hotkey letter taken out. */
//-------------------------------------------------------------------------------------------------
static std::string tooltipName( const UnicodeString &label )
{
	std::wstring name = label.str();
	const size_t marker = name.find( L'&' );
	if( marker != std::wstring::npos )
		name.erase( marker, 1 );
	return WideCharStringToMultiByte( name.c_str() );
}

//-------------------------------------------------------------------------------------------------
/** The build tooltip's values: {{name}}, {{cost}} with {{costkind}} "money" or "science" and
	* {{cost.shown}}, data-each="lines" out of the description, {{warning}} and {{requires}} each with
	* its .shown, the figures {{time}} {{health}} {{damage}} {{speed}} {{dps}} {{range}} with
	* {{stats.shown}}, {{health.shown}} and {{weapon.shown}}, and data-each="upgrades". */
//-------------------------------------------------------------------------------------------------
static void putBuildTooltipCard( const BuildTooltipCard &card, HtmlValues &values, HtmlLists &lists )
{
	values[ "kind" ] = "card";
	values[ "name" ] = tooltipName( card.name );
	values[ "cost" ] = std::to_string( card.cost );
	values[ "costkind" ] = card.costsScience ? "science" : "money";
	values[ "cost.shown" ] = card.cost > 0 ? "shown" : "hidden";
	putTooltipLines( card.description, lists[ "lines" ] );
	values[ "warning" ] = WideCharStringToMultiByte( card.warning.str() );
	values[ "warning.shown" ] = card.warning.isEmpty() ? "hidden" : "shown";
	values[ "requires" ] = WideCharStringToMultiByte( card.requires.str() );
	values[ "requires.shown" ] = card.requires.isEmpty() ? "hidden" : "shown";

	UnicodeString seconds;
	seconds.format( TheGameText->fetch( "TOOLTIP:StatSeconds" ), card.buildSeconds );
	values[ "time" ] = WideCharStringToMultiByte( seconds.str() );
	values[ "health" ] = std::to_string( card.health );
	values[ "damage" ] = std::to_string( card.damage );
	values[ "range" ] = std::to_string( card.range );
	char speed[ 32 ];
	snprintf( speed, sizeof( speed ), "%.2f", card.attacksPerSecond );
	values[ "speed" ] = speed + WideCharStringToMultiByte( TheGameText->fetch( "TOOLTIP:StatPerSecond" ).str() );
	values[ "dps" ] = std::to_string( card.damagePerSecond );
	values[ "stats.shown" ] = card.hasStats ? "shown" : "hidden";
	values[ "health.shown" ] = card.hasStats && card.health > 0 ? "shown" : "hidden";
	values[ "weapon.shown" ] = card.hasStats && card.damage > 0 ? "shown" : "hidden";

	// a unit's card lists its upgrades; an upgrade's card lists the units it changes
	values[ "upgrades.shown" ] = card.upgrades.empty() ? "hidden" : "shown";
	values[ "upgradeskind" ] = card.hasStats ? "unit" : "upgrade";
	std::vector< HtmlValues > &upgrades = lists[ "upgrades" ];
	for( size_t each = 0; each < card.upgrades.size(); ++each )
	{
		const BuildTooltipUpgrade &upgrade = card.upgrades[ each ];
		HtmlValues head;
		head[ "kind" ] = "upgrade";
		head[ "name" ] = tooltipName( upgrade.name );
		head[ "owned" ] = upgrade.owned ? "owned" : "";
		upgrades.push_back( head );
		for( size_t change = 0; change < upgrade.changes.size(); ++change )
		{
			HtmlValues line;
			line[ "kind" ] = upgrade.changes[ change ].from.empty() ? "remark" : "change";
			line[ "owned" ] = head[ "owned" ];
			line[ "label" ] = WideCharStringToMultiByte( TheGameText->fetch( upgrade.changes[ change ].label ).str() );
			line[ "from" ] = upgrade.changes[ change ].from;
			line[ "to" ] = upgrade.changes[ change ].to;
			upgrades.push_back( line );
		}
	}
}

//-------------------------------------------------------------------------------------------------
Bool InGameUI::isTooltipPageReady( void )
{
	if( TheGameLogic == NULL || !TheGameLogic->isInGame() || TheGameLogic->isInShellGame() )
		return FALSE;

	if( !m_tooltipPageLoaded )
	{
		m_tooltipPageLoaded = TRUE;
		readHtmlPage( TOOLTIP_PAGE, m_tooltipPage );
	}
	return !m_tooltipPage.empty();
}

//-------------------------------------------------------------------------------------------------
/** Both tooltips out of Window/Html/Tooltip.html, in the side's steel.  The box is placed by the
	* size it came out last time; when this one comes out another size it is placed and laid out once
	* more, so a new tooltip is never drawn a frame in the wrong place. */
//-------------------------------------------------------------------------------------------------
Bool InGameUI::drawTooltipPage( const UnicodeString &cursorText, const RGBColor *accent )
{
	if( !isTooltipPageReady() )
		return FALSE;

	const BuildTooltipCard *card = TheControlBar->getBuildTooltipCard();
	if( card == NULL && cursorText.isEmpty() )
		return TRUE;

	if( m_tooltipOverlay == NULL )
		m_tooltipOverlay = new HtmlOverlay( m_superweaponNormalFont );

	HtmlValues values;
	HtmlLists lists;
	values[ "side" ] = spectatorSide();
	if( card )
	{
		putBuildTooltipCard( *card, values, lists );
	}
	else
	{
		// the first line names what is under the pointer, the rest are its owner and the like
		std::vector< HtmlValues > &lines = lists[ "lines" ];
		putTooltipLines( cursorText, lines );
		values[ "kind" ] = "tip";
		if( !lines.empty() )
		{
			values[ "title" ] = lines.front()[ "kind" ] == "text" ? lines.front()[ "text" ]
																														: lines.front()[ "label" ] + ": " + lines.front()[ "value" ];
			lines.erase( lines.begin() );
		}
		values[ "accent" ] = accent ? cssColor( GameMakeColor( REAL_TO_INT( accent->red * 255.0f ), REAL_TO_INT( accent->green * 255.0f ),
																													 REAL_TO_INT( accent->blue * 255.0f ), 255 ) )
																: "transparent";
	}

	const Int screenWidth = TheDisplay->getWidth();
	const Int screenHeight = TheDisplay->getHeight();
	const ICoord2D &mouse = TheMouse->getMouseStatus()->pos;
	const Int gap = REAL_TO_INT( TOOLTIP_ANCHOR_GAP * ControlBarUniformScale() );
	for( Int pass = 0; pass < TOOLTIP_LAYOUT_PASSES; pass++ )
	{
		IRegion2D box;
		if( card )
		{
			// over the button, centred on it, and under it when there is no room above
			box.lo.x = ( card->anchor.lo.x + card->anchor.hi.x - m_tooltipSize.x ) / 2;
			box.lo.y = card->anchor.lo.y - gap - m_tooltipSize.y;
			if( box.lo.y < 0 )
				box.lo.y = card->anchor.hi.y + gap;
			box.lo.x = max( 0, min( box.lo.x, screenWidth - m_tooltipSize.x ) );
		}
		else
		{
			Mouse::placeTooltip( mouse.x, mouse.y, m_tooltipSize.x, m_tooltipSize.y, 0, 0, screenWidth, screenHeight,
													 &box.lo.x, &box.lo.y );
		}
		box.hi.x = box.lo.x + m_tooltipSize.x;
		box.hi.y = box.lo.y + m_tooltipSize.y;
		putPageRect( values, "box", box, TRUE );
		m_tooltipOverlay->setPage( HtmlTemplate_expand( m_tooltipPage, values, lists, lookupGameText ) );

		std::vector< IRegion2D > laidOut;
		m_tooltipOverlay->rectsOf( "#box", laidOut );
		DEBUG_ASSERTCRASH( laidOut.size() == 1, ( "%s has %d #box elements, wants one\n", TOOLTIP_PAGE, (Int)laidOut.size() ) );
		const ICoord2D size = { laidOut.front().hi.x - laidOut.front().lo.x, laidOut.front().hi.y - laidOut.front().lo.y };
		if( size.x == m_tooltipSize.x && size.y == m_tooltipSize.y )
			break;
		m_tooltipSize = size;
	}
	m_tooltipOverlay->draw();
	return TRUE;
}

void InGameUI::disableTooltipsUntil(UnsignedInt frameNum)
{
	if (frameNum > m_tooltipsDisabledUntil) 
		m_tooltipsDisabledUntil = frameNum;
}

void InGameUI::clearTooltipsDisabled()
{
	m_tooltipsDisabledUntil = 0;
}

Bool InGameUI::areTooltipsDisabled() const
{
	return (TheGameLogic->getFrame() < m_tooltipsDisabledUntil);
}


WindowMsgHandledType IdleWorkerSystem( GameWindow *window, UnsignedInt msg, 
																				WindowMsgData mData1, WindowMsgData mData2 )
{
	switch( msg ) 
	{
		//---------------------------------------------------------------------------------------------
		case GWM_INPUT_FOCUS:
		{	
			// if we're givin the opportunity to take the keyboard focus we must say we don't want it
			if( mData1 == TRUE )
				*(Bool *)mData2 = FALSE;
		}
		//---------------------------------------------------------------------------------------------
		case GBM_SELECTED:
		{
			GameWindow *control = (GameWindow *)mData1;
			static NameKeyType buttonSelectID = NAMEKEY( "IdleWorker.wnd:ButtonSelectNextIdleWorker" );
			if (control && control->winGetWindowId() == buttonSelectID)
			{
				TheInGameUI->selectNextIdleWorker( );
			}
			break;

		}  // end button selected

		//---------------------------------------------------------------------------------------------
		default:
			return MSG_IGNORED;

	}  // end switch( msg )

	return MSG_HANDLED;

}


