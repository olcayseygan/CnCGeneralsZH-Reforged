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

// FILE: ReplayControls.cpp ///////////////////////////////////////////////////////////////////////
// Author: Bryan Cleveland - December 2001
// Desc: GUI Control box for the playback controls
///////////////////////////////////////////////////////////////////////////////////////////////////

// INCLUDES ///////////////////////////////////////////////////////////////////////////////////////
#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "Common/GameEngine.h"
#include "Common/GlobalData.h"
#include "Common/NameKeyGenerator.h"
#include "Common/Recorder.h"
#include "GameClient/ControlBar.h"
#include "GameClient/Display.h"
#include "GameClient/DisplayString.h"
#include "GameClient/DisplayStringManager.h"
#include "GameClient/GameWindow.h"
#include "GameClient/GameWindowManager.h"
#include "GameClient/Gadget.h"
#include "GameClient/GameClient.h"
#include "GameClient/GameText.h"
#include "GameClient/GlobalLanguage.h"
#include "GameClient/GUICallbacks.h"
#include "GameClient/InGameUI.h"
#include "GameClient/Mouse.h"
#include "GameClient/UIStyle.h"
#include "GameLogic/GameLogic.h"

//-------------------------------------------------------------------------------------------------
// The replay strip: the time reached and the whole length, pause, five speeds and the timeline,
// drawn by ReplayControl.wnd's parent window itself, standing over the middle of the command bar.
// EA's children in that layout (Slower, Faster, Pause, NextFrame) stay hidden.  Every press goes
// through InGameUI::runSpectatorAction, the path the control socket takes, so a click and a script
// move the replay the same way.  The timeline jumps when the button is let go, not while it is
// dragged: a jump back loads a checkpoint, and a drag would load one for every pixel it crossed.
//-------------------------------------------------------------------------------------------------

/// percent of the rate the game was played at
static const Int REPLAY_SPEEDS[] = { 50, 100, 200, 400, 800 };
static const WideChar *const REPLAY_SPEED_LABELS[] = { u"0.5x", u"1x", u"2x", u"4x", u"8x" };
static const Int REPLAY_SPEED_COUNT = (Int)ARRAY_SIZE( REPLAY_SPEEDS );
/// the pause button first, then the speeds
static const Int REPLAY_BUTTONS = 1 + REPLAY_SPEED_COUNT;

/// the strip in the command bar's own units, ControlBarHudScale() pixels each
enum
{
	STRIP_WIDTH = 300, STRIP_HEIGHT = 40, STRIP_PAD = 5, STRIP_GAP = 4, ROW_HEIGHT = 14,
	PAUSE_WIDTH = 56, SPEED_WIDTH = 30, BUTTON_GAP = 2, TRACK_HEIGHT = 12, FONT_SIZE = 9
};

/// the window's own strings, freed with it
struct ReplayStripText
{
	DisplayString *clock;
	DisplayString *buttons[ REPLAY_BUTTONS ];
};

struct ReplayStripLayout
{
	IRegion2D strip;
	IRegion2D track;
	IRegion2D buttons[ REPLAY_BUTTONS ];
	ICoord2D clock;
	Real unit;
};

/// a press on the timeline not yet let go, and the frame under the pointer
static Bool TheReplayStripDragging = FALSE;
static UnsignedInt TheReplayStripDragFrame = 0;

/// The strip can be carried by its ground (not a button, not the timeline) anywhere on the screen,
/// and a double click there puts it back over the money readout.  Where it was put is kept in
/// screen pixels and held to the screen, so a resolution change cannot lose it off an edge.
static Bool TheReplayStripMoving = FALSE;
static Bool TheReplayStripPlaced = FALSE;
static ICoord2D TheReplayStripCorner = { 0, 0 };
static ICoord2D TheReplayStripGrab = { 0, 0 };		///< the pointer less the strip's corner at the press

/** 1x: the rate the game was played at.  A skirmish on its fast setting recorded 60. */
static Int replayNormalFramesPerSecond( void )
{
	const Int recorded = TheRecorder->getPlaybackFramesPerSecond();
	return recorded > 0 ? recorded : LOGICFRAMES_PER_SECOND;
}

/** The speed button lit now, -1 for a speed that is none of them. */
static Int replaySpeedIndex( void )
{
	const Int speed = TheGameEngine->getFramesPerSecondLimit() * 100 / replayNormalFramesPerSecond();
	for( Int each = 0; each < REPLAY_SPEED_COUNT; each++ )
		if( REPLAY_SPEEDS[ each ] == speed )
			return each;
	return -1;
}

/** The replay's length, or the frame reached if the header never had it written. */
static UnsignedInt replayLength( void )
{
	return max( TheRecorder->getPlaybackFrameDuration(), TheGameLogic->getFrame() );
}

static UnicodeString replayClock( UnsignedInt frame )
{
	const Int seconds = (Int)( frame / LOGICFRAMES_PER_SECOND );
	UnicodeString text;
	if( seconds >= 60 * 60 )
		text.format( u"%d:%02d:%02d", seconds / ( 60 * 60 ), seconds / 60 % 60, seconds % 60 );
	else
		text.format( u"%d:%02d", seconds / 60, seconds % 60 );
	return text;
}

static GameFont *replayStripFont( Real unit )
{
	return TheWindowManager->winFindFont( TheGlobalLanguageData->m_defaultWindowFont.name, REAL_TO_INT_FLOOR( FONT_SIZE * unit + 0.5f ), TRUE );
}

/** The labels' widths in the strip's font, measured once a scale and language: the pause button's
	* wider word of Pause and Resume, every speed, and the clock at its widest. */
struct ReplayLabelWidths
{
	Real unit;
	Int language;
	Int pause;
	Int speed[ REPLAY_SPEED_COUNT ];
	Int clock;
};

static Int replayMeasure( DisplayString *string, const UnicodeString &text )
{
	string->setText( text );
	Int width = 0, height = 0;
	string->getSize( &width, &height );
	return width;
}

static const ReplayLabelWidths &replayLabelWidths( Real unit )
{
	static ReplayLabelWidths widths = { -1.0f, -1, 0, { 0 }, 0 };
	if( widths.unit == unit && widths.language == TheGlobalData->m_textLanguage )
		return widths;
	widths.unit = unit;
	widths.language = TheGlobalData->m_textLanguage;
	DisplayString *string = TheDisplayStringManager->newDisplayString();
	string->setFont( replayStripFont( unit ) );
	widths.pause = max( replayMeasure( string, TheGameText->fetch( "GUI:Pause" ) ), replayMeasure( string, TheGameText->fetch( "GUI:ReplayResume" ) ) );
	for( Int each = 0; each < REPLAY_SPEED_COUNT; each++ )
		widths.speed[ each ] = replayMeasure( string, UnicodeString( REPLAY_SPEED_LABELS[ each ] ) );
	widths.clock = replayMeasure( string, UnicodeString( u"00:00 / 00:00" ) );
	TheDisplayStringManager->freeDisplayString( string );
	return widths;
}

/** Centred over the money readout, the middle of the command bar, at the bar's own scale.  The
	* buttons are as wide as their labels at full text, the strip as wide as the clock and the buttons
	* need; only where the screen itself is too narrow does the pause button give way, its word then
	* abbreviated (the speeds never are). */
static ReplayStripLayout layOutReplayStrip( void )
{
	ReplayStripLayout layout;
	layout.unit = ControlBarHudScale();
	const Real u = layout.unit;
	const ReplayLabelWidths &labels = replayLabelWidths( u );
	const Int textPad = max( REAL_TO_INT_FLOOR( 4 * u ), 2 );
	const Int gap = REAL_TO_INT_FLOOR( BUTTON_GAP * u );
	Int buttonWidth[ REPLAY_BUTTONS ];
	Int buttonsWidth = 0;
	for( Int each = 0; each < REPLAY_BUTTONS; each++ )
	{
		const Int design = REAL_TO_INT_FLOOR( ( each == 0 ? PAUSE_WIDTH : SPEED_WIDTH ) * u );
		buttonWidth[ each ] = max( design, ( each == 0 ? labels.pause : labels.speed[ each - 1 ] ) + 2 * textPad );
		buttonsWidth += buttonWidth[ each ] + ( each > 0 ? gap : 0 );
	}

	GameWindow *money = TheWindowManager->winGetWindowFromId( NULL,
		TheNameKeyGenerator->nameToKey( AsciiString( "ControlBar.wnd:ControlBarProBlockInputMoney" ) ) );
	ICoord2D above, aboveSize;
	money->winGetScreenPosition( &above.x, &above.y );
	money->winGetSize( &aboveSize.x, &aboveSize.y );

	const Int height = REAL_TO_INT_FLOOR( STRIP_HEIGHT * u );
	const Int pad = REAL_TO_INT_FLOOR( STRIP_PAD * u );
	Int width = max( REAL_TO_INT_FLOOR( STRIP_WIDTH * u ), pad * 2 + labels.clock + textPad * 2 + buttonsWidth );
	const Int screenWidth = (Int)TheDisplay->getWidth();
	if( width > screenWidth )
	{
		// the screen is narrower than the strip at full text: the pause button gives the difference
		const Int design = REAL_TO_INT_FLOOR( PAUSE_WIDTH * u );
		const Int give = min( width - screenWidth, max( buttonWidth[ 0 ] - design, 0 ) );
		buttonWidth[ 0 ] -= give;
		width -= give;
	}
	layout.strip.lo.x = above.x + aboveSize.x / 2 - width / 2;
	layout.strip.lo.y = above.y - REAL_TO_INT_FLOOR( STRIP_GAP * u ) - height;
	if( TheReplayStripPlaced )
	{
		layout.strip.lo.x = clamp( 0, TheReplayStripCorner.x, (Int)TheDisplay->getWidth() - width );
		layout.strip.lo.y = clamp( 0, TheReplayStripCorner.y, (Int)TheDisplay->getHeight() - height );
	}
	layout.strip.hi.x = layout.strip.lo.x + width;
	layout.strip.hi.y = layout.strip.lo.y + height;

	layout.clock.x = layout.strip.lo.x + pad;
	layout.clock.y = layout.strip.lo.y + pad;

	// the buttons right-aligned on the first row, the last speed at the right edge
	const Int rowTop = layout.strip.lo.y + pad;
	Int right = layout.strip.hi.x - pad;
	for( Int each = REPLAY_BUTTONS - 1; each >= 0; each-- )
	{
		IRegion2D &button = layout.buttons[ each ];
		button.hi.x = right;
		button.lo.x = right - buttonWidth[ each ];
		button.lo.y = rowTop;
		button.hi.y = rowTop + REAL_TO_INT_FLOOR( ROW_HEIGHT * u );
		right = button.lo.x - gap;
	}

	layout.track.lo.x = layout.strip.lo.x + pad;
	layout.track.hi.x = layout.strip.hi.x - pad;
	layout.track.lo.y = rowTop + REAL_TO_INT_FLOOR( ( ROW_HEIGHT + STRIP_GAP ) * u );
	layout.track.hi.y = layout.track.lo.y + REAL_TO_INT_FLOOR( TRACK_HEIGHT * u );
	return layout;
}

static Bool regionHolds( const IRegion2D &region, Int x, Int y )
{
	return x >= region.lo.x && x < region.hi.x && y >= region.lo.y && y < region.hi.y;
}

/** The frame under a pointer at this x on the timeline. */
static UnsignedInt replayFrameAt( const ReplayStripLayout &layout, Int x )
{
	const Real share = clamp( 0.0f, (Real)( x - layout.track.lo.x ) / (Real)( layout.track.hi.x - layout.track.lo.x ), 1.0f );
	return REAL_TO_UNSIGNEDINT( share * replayLength() );
}

static void endReplayStripDrag( void )
{
	TheReplayStripDragging = FALSE;
	TheInGameUI->runSpectatorAction( "replay:seek:" + std::to_string( TheReplayStripDragFrame ) );
}

static void setReplaySpeed( Int index )
{
	TheInGameUI->runSpectatorAction( "replay:speed:" + std::to_string( REPLAY_SPEEDS[ index ] ) );
}

//-------------------------------------------------------------------------------------------------
/** One speed up or down from the one lit, for the keys.  A speed that is none of the buttons' steps
	* from 1x. */
//-------------------------------------------------------------------------------------------------
void ReplayControlStepSpeed( Int step )
{
	const Int lit = replaySpeedIndex();
	const Int from = lit >= 0 ? lit : 1;
	setReplaySpeed( clamp( 0, from + step, REPLAY_SPEED_COUNT - 1 ) );
}

//-------------------------------------------------------------------------------------------------
/** The strip draws itself and moves itself onto the command bar every frame: the bar is laid out
	* again on a resolution change, and the strip has to follow it. */
//-------------------------------------------------------------------------------------------------
void ReplayControlDraw( GameWindow *window, WinInstanceData *instData )
{
	// let go somewhere off the strip, where the window manager does not tell the strip
	if( TheReplayStripDragging && TheMouse->getMouseStatus()->leftState == MBS_Up )
		endReplayStripDrag();

	// carried from the pointer itself, so a quick move that leaves the strip behind still takes it
	if( TheReplayStripMoving )
	{
		const MouseIO *pointer = TheMouse->getMouseStatus();
		if( pointer->leftState == MBS_Up )
			TheReplayStripMoving = FALSE;
		else
		{
			// a press that has not moved leaves the strip home, still following the command bar
			const IRegion2D now = layOutReplayStrip().strip;
			const ICoord2D corner = { pointer->pos.x - TheReplayStripGrab.x, pointer->pos.y - TheReplayStripGrab.y };
			if( corner.x != now.lo.x || corner.y != now.lo.y )
			{
				TheReplayStripPlaced = TRUE;
				TheReplayStripCorner = corner;
			}
		}
	}

	const ReplayStripLayout layout = layOutReplayStrip();
	const IRegion2D &strip = layout.strip;
	window->winSetPosition( strip.lo.x, strip.lo.y );
	window->winSetSize( strip.hi.x - strip.lo.x, strip.hi.y - strip.lo.y );

	ReplayStripText *text = (ReplayStripText *)window->winGetUserData();
	if( text == NULL )
	{
		text = NEW ReplayStripText;
		text->clock = TheDisplayStringManager->newDisplayString();
		for( Int each = 0; each < REPLAY_BUTTONS; each++ )
			text->buttons[ each ] = TheDisplayStringManager->newDisplayString();
		window->winSetUserData( text );
	}
	GameFont *font = replayStripFont( layout.unit );

	const Color white = GameMakeColor( 255, 255, 255, 255 );
	const Color shadow = GameMakeColor( 0, 0, 0, 255 );
	const Color edge = GameMakeColor( 150, 150, 150, 200 );
	const Color lit = GameMakeColor( 170, 125, 30, 235 );
	const Color unlit = GameMakeColor( 55, 55, 55, 220 );

	TheDisplay->drawFillRect( strip.lo.x, strip.lo.y, strip.hi.x - strip.lo.x, strip.hi.y - strip.lo.y, GameMakeColor( 0, 0, 0, 170 ) );
	TheDisplay->drawOpenRect( strip.lo.x, strip.lo.y, strip.hi.x - strip.lo.x, strip.hi.y - strip.lo.y, 1.0f, edge );

	const UnsignedInt frame = TheGameLogic->getFrame();
	const UnsignedInt length = replayLength();
	UnicodeString clock;
	clock.format( u"%s / %s", replayClock( frame ).str(), replayClock( length ).str() );
	text->clock->setFont( font );
	text->clock->setText( clock );
	text->clock->draw( layout.clock.x, layout.clock.y, white, shadow );

	const Bool paused = TheGameLogic->isGamePaused();
	const Int speed = replaySpeedIndex();
	for( Int each = 0; each < REPLAY_BUTTONS; each++ )
	{
		const IRegion2D &button = layout.buttons[ each ];
		const Bool on = each == 0 ? paused : speed == each - 1;
		const Int w = button.hi.x - button.lo.x;
		const Int h = button.hi.y - button.lo.y;
		TheDisplay->drawFillRect( button.lo.x, button.lo.y, w, h, on ? lit : unlit );
		TheDisplay->drawOpenRect( button.lo.x, button.lo.y, w, h, 1.0f, edge );

		DisplayString *label = text->buttons[ each ];
		label->setFont( font );
		const UnicodeString words = each == 0 ? TheGameText->fetch( paused ? "GUI:ReplayResume" : "GUI:Pause" ) : UnicodeString( REPLAY_SPEED_LABELS[ each - 1 ] );
		label->setText( words );
		Int labelWidth, labelHeight;
		label->getSize( &labelWidth, &labelHeight );
		// the buttons are sized to their labels; only the pause word, on a screen too narrow for the
		// strip, is abbreviated (UIStyle::fitText), the speeds never.  The fitted form is kept while the
		// words and the width stay, so it is not worked out each frame
		const Int room = w - 4;
		if( each == 0 && labelWidth > room )
		{
			static UnicodeString fittedFrom[ REPLAY_BUTTONS ];
			static UnicodeString fitted[ REPLAY_BUTTONS ];
			static Int fittedRoom[ REPLAY_BUTTONS ];
			if( fittedRoom[ each ] != room || fittedFrom[ each ].compare( words ) != 0 )
			{
				fitted[ each ] = UIStyle::fitText( words, font, room );
				fittedFrom[ each ] = words;
				fittedRoom[ each ] = room;
			}
			label->setText( fitted[ each ] );
			label->getSize( &labelWidth, &labelHeight );
		}
		label->draw( button.lo.x + ( w - labelWidth ) / 2, button.lo.y + ( h - labelHeight ) / 2, white, shadow );
	}

	const IRegion2D &track = layout.track;
	const Int trackWidth = track.hi.x - track.lo.x;
	const Int trackHeight = track.hi.y - track.lo.y;
	TheDisplay->drawFillRect( track.lo.x, track.lo.y, trackWidth, trackHeight, GameMakeColor( 30, 30, 30, 230 ) );
	if( length > 0 )
		TheDisplay->drawFillRect( track.lo.x, track.lo.y, (Int)( (Int64)trackWidth * frame / length ), trackHeight, GameMakeColor( 220, 175, 60, 255 ) );
	TheDisplay->drawOpenRect( track.lo.x, track.lo.y, trackWidth, trackHeight, 1.0f, edge );
	// where the drag will jump to
	if( TheReplayStripDragging && length > 0 )
	{
		const Int marker = REAL_TO_INT_FLOOR( 2.0f * layout.unit );
		const Int at = track.lo.x + (Int)( (Int64)trackWidth * TheReplayStripDragFrame / length ) - marker / 2;
		TheDisplay->drawFillRect( at, track.lo.y - marker, marker, trackHeight + 2 * marker, white );
	}
}

//-------------------------------------------------------------------------------------------------
/** Every press on the strip is the strip's, so none becomes a click on the world under it. */
//-------------------------------------------------------------------------------------------------
WindowMsgHandledType ReplayControlInput( GameWindow *window, UnsignedInt msg,
																			WindowMsgData mData1, WindowMsgData mData2 )
{
	const Int x = (Int)( mData1 & 0xFFFF );
	const Int y = (Int)( mData1 >> 16 );
	switch( msg )
	{
		case GWM_LEFT_DOWN:
		case GWM_LEFT_DOUBLE_CLICK:
		{
			const ReplayStripLayout layout = layOutReplayStrip();
			if( regionHolds( layout.track, x, y ) )
			{
				TheReplayStripDragging = TRUE;
				TheReplayStripDragFrame = replayFrameAt( layout, x );
				break;
			}
			if( regionHolds( layout.buttons[ 0 ], x, y ) )
			{
				TheInGameUI->runSpectatorAction( "replay:pause" );
				break;
			}
			for( Int each = 1; each < REPLAY_BUTTONS; each++ )
				if( regionHolds( layout.buttons[ each ], x, y ) )
				{
					setReplaySpeed( each - 1 );
					return MSG_HANDLED;
				}

			// the strip's ground: a press carries it, a double click sends it home
			if( msg == GWM_LEFT_DOUBLE_CLICK )
			{
				TheReplayStripMoving = FALSE;
				TheReplayStripPlaced = FALSE;
				break;
			}
			TheReplayStripMoving = TRUE;
			TheReplayStripGrab.x = x - layout.strip.lo.x;
			TheReplayStripGrab.y = y - layout.strip.lo.y;
			break;
		}

		case GWM_LEFT_DRAG:
			if( TheReplayStripDragging )
				TheReplayStripDragFrame = replayFrameAt( layOutReplayStrip(), x );
			break;

		case GWM_LEFT_UP:
			if( TheReplayStripDragging )
			{
				TheReplayStripDragFrame = replayFrameAt( layOutReplayStrip(), x );
				endReplayStripDrag();
			}
			break;

		case GWM_RIGHT_DOWN:
		case GWM_RIGHT_UP:
		case GWM_MIDDLE_DOWN:
		case GWM_MIDDLE_UP:
			break;

		default:
			return MSG_IGNORED;
	}

	return MSG_HANDLED;

}  // end ReplayControlInput

//-------------------------------------------------------------------------------------------------
/** System callback for the control bar parent */
//-------------------------------------------------------------------------------------------------
WindowMsgHandledType ReplayControlSystem( GameWindow *window, UnsignedInt msg, 
																			 WindowMsgData mData1, WindowMsgData mData2 )
{
	switch( msg ) 
	{

		//---------------------------------------------------------------------------------------------
		case GBM_SELECTED:
		{

			break;

		}  // end button selected

		//---------------------------------------------------------------------------------------------
		case GWM_DESTROY:
		{
			ReplayStripText *text = (ReplayStripText *)window->winGetUserData();
			if( text == NULL )
				break;
			TheDisplayStringManager->freeDisplayString( text->clock );
			for( Int each = 0; each < REPLAY_BUTTONS; each++ )
				TheDisplayStringManager->freeDisplayString( text->buttons[ each ] );
			delete text;
			window->winSetUserData( NULL );
			break;
		}

		//---------------------------------------------------------------------------------------------
		default:
			return MSG_IGNORED;

	}  // end switch( msg )

	return MSG_HANDLED;

}  // end ControlBarSystem

