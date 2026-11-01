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


// FILE: W3DPushButton.cpp ////////////////////////////////////////////////////
//-----------------------------------------------------------------------------
//                                                                          
//                       Westwood Studios Pacific.                          
//                                                                          
//                       Confidential Information                           
//                Copyright (C) 2001 - All Rights Reserved                  
//                                                                          
//-----------------------------------------------------------------------------
//
// Project:   RTS3
//
// File name: W3DPushButton.cpp
//
// Created:   Colin Day, June 2001
//
// Desc:			W3D implementation for the push button control element
//
//-----------------------------------------------------------------------------
///////////////////////////////////////////////////////////////////////////////

// SYSTEM INCLUDES ////////////////////////////////////////////////////////////
#include <stdlib.h>

// USER INCLUDES //////////////////////////////////////////////////////////////
#include "GameClient/Gadget.h"
#include "GameClient/GameFont.h"
#include "GameClient/GameWindowGlobal.h"
#include "GameClient/GameWindowManager.h"
#include "GameClient/GadgetPushButton.h"
#include "GameClient/Display.h"
#include "GameClient/DisplayStringManager.h"
#include "GameClient/InGameUI.h"		// HudReadout_draw, the plate every corner marking stands on
#include "Common/GlobalData.h"
#include "Lib/Clock.h"
#include "W3DDevice/GameClient/W3DGameWindow.h"
#include "W3DDevice/GameClient/W3DDisplay.h"
#include "W3DDevice/GameClient/W3DGadget.h"


#ifdef _INTERNAL
// for occasional debugging...
//#pragma optimize("", off) 
//#pragma MESSAGE("************************************** WARNING, optimization disabled for debugging purposes")
#endif


// DEFINES ////////////////////////////////////////////////////////////////////

// PRIVATE TYPES //////////////////////////////////////////////////////////////

// PRIVATE DATA ///////////////////////////////////////////////////////////////

// PUBLIC DATA ////////////////////////////////////////////////////////////////

// PRIVATE PROTOTYPES /////////////////////////////////////////////////////////

void W3DGadgetPushButtonImageDrawThree(GameWindow *window, WinInstanceData *instData );
void W3DGadgetPushButtonImageDrawOne(GameWindow *window, WinInstanceData *instData );

// PRIVATE FUNCTIONS //////////////////////////////////////////////////////////

/** The bottom HUD's own scale, the one its buttons are laid out at, from ControlBar.cpp.  Declared rather than included: ControlBar.h
	* drags in the whole command-set machinery for one function that takes nothing and returns a
	* float. */
extern Real ControlBarHudScale( void );

/** Point size a corner marking wears on a command button at 800x600, which is the resolution the
	* command bar and its 50x44 buttons were drawn for.  Everything else is this times the scale the
	* bar itself is laid out at. */
static const Real BADGE_DESIGN_POINTS = 7.0f;

/** The smallest a marking is set: at 5 this font's figures run together. */
static const Int BADGE_LEAST_POINTS = 6;

/** The queue count is the one marking a player reads at a glance in the middle of a fight - how
	* many more of these are still coming - and at seven points against a busy cameo it was a smudge
	* nobody found without looking for it.  It gets its own size, and an opaque plate under it. */
static const Real COUNT_BADGE_DESIGN_POINTS = 11.0f;

/** The key letter, the only place the keys are shown. */
static const Real CLASSIC_KEY_BADGE_DESIGN_POINTS = 12.0f;

// getBadgeFont ===============================================================
/** The font the corner markings wear.
	*
	* A hotkey letter, a price, a countdown and a queue count are labels on a picture, not text to
	* be read at length: four of them crowd a button that is barely thirty pixels wide, and the
	* button stops reading as its own art.
	*
	* They have to keep the same size against that picture at every resolution, and the window's own
	* font does not do that.  The layout loader sizes a font by the screen's *width* over 800 (damped
	* by ResolutionFontAdjustment), while the command bar is laid out at the smaller of width over
	* 800 and height over 600 - so the wider the screen against its height, the faster the lettering
	* grew away from the buttons it sits on.  At 2560x1080 the markings came out half again too big.
	* One design size, times the bar's own scale, and a cameo looks the same on every monitor. */
//=============================================================================
static GameFont *getBadgeFont( GameWindow *window, Real designPoints = BADGE_DESIGN_POINTS )
{
	GameFont *font = window->winGetFont();
	if( font == NULL )
		return NULL;

	Int pointSize = REAL_TO_INT_FLOOR( designPoints * ControlBarHudScale() );
	if( pointSize < BADGE_LEAST_POINTS )
		pointSize = BADGE_LEAST_POINTS;

	// bold, because these are markings on a picture: at seven points against a busy cameo the light
	// weight reads as noise on the artwork rather than as a letter
	if( pointSize == font->pointSize && font->bold )
		return font;

	return TheFontLibrary->getFont( font->nameString, pointSize, TRUE );

}  // end getBadgeFont

static void drawBadge( GameWindow *window, const UnicodeString &text, Real designPoints,
											 HudReadoutCorner corner, Color color );
static void drawClassicKeyBadge( GameWindow *window, const UnicodeString &text, Color color );

// drawButtonText =============================================================
/** Draw button text to the screen */
//=============================================================================
static void drawButtonText( GameWindow *window, WinInstanceData *instData )
{
	ICoord2D origin, size, textPos;
	Int width, height;
	Color textColor, dropColor;
	DisplayString *text = instData->getTextDisplayString();

	// sanity
	if( text == NULL || text->getTextLength() == 0 )
		return;

	// get window position and size
	window->winGetScreenPosition( &origin.x, &origin.y );
	window->winGetSize( &size.x, &size.y );

	// set whether or not we center the wrapped text
	text->setWordWrapCentered( BitTest( instData->getStatus(), WIN_STATUS_WRAP_CENTERED ));
	text->setWordWrap(size.x);
	// get the right text color
	if( BitTest( window->winGetStatus(), WIN_STATUS_ENABLED ) == FALSE )
	{
		textColor = window->winGetDisabledTextColor();
		dropColor = window->winGetDisabledTextBorderColor();
	}  // end if, disabled
	else if( BitTest( instData->getState(), WIN_STATE_HILITED ) )
	{
		textColor = window->winGetHiliteTextColor();
		dropColor = window->winGetHiliteTextBorderColor();
	}  // end else if, hilited
	else
	{
		textColor = window->winGetEnabledTextColor();
		dropColor = window->winGetEnabledTextBorderColor();
	}  // end enabled only

	// the shortcut letter is a corner marking in the top left corner.  There are no grid places to
	// learn the keys from, so the letter is the one way in: set larger, and in gold while the button
	// can be pressed
	if( BitTest( window->winGetStatus(), WIN_STATUS_SHORTCUT_BUTTON ) )
	{
		const Bool enabled = BitTest( window->winGetStatus(), WIN_STATUS_ENABLED );
		drawClassicKeyBadge( window, text->getText(), enabled ? GameMakeColor( 255, 210, 60, 255 ) : textColor );
		return;
	}

	// set our font to that of our parent if not the same
	GameFont *font = window->winGetFont();
	if( font != NULL && text->getFont() != font )
		text->setFont( font );

	// get text size
	text->getSize( &width, &height );

	// where to draw
	textPos.x = origin.x + (size.x / 2) - (width / 2);
	textPos.y = origin.y + (size.y / 2) - (height / 2);

	// draw it
	text->draw( textPos.x, textPos.y, textColor, dropColor );

}  // end drawButtonText

// badgeString ================================================================
/** The display string for one badge's text.  A display string keeps the texture
	* its text was built into, so each one is only ever handed text it already
	* holds: one shared string set to every button's number in turn rebuilt a text
	* texture for every badge on the bar every frame, about 33 of them, and on the
	* Direct3D 11 frame each of those was a texture made and copied as well. */
//=============================================================================
static const Int BADGE_STRING_SLOTS = 64;
static DisplayString *theBadgeStrings[ BADGE_STRING_SLOTS ] = { NULL };
static Int theNextBadgeSlot = 0;

static DisplayString *badgeString( const UnicodeString &text, GameFont *font )
{
	for( Int slot = 0; slot < BADGE_STRING_SLOTS; ++slot )
	{
		DisplayString *candidate = theBadgeStrings[ slot ];
		if( candidate != NULL && candidate->getFont() == font && candidate->peekText() == text )
			return candidate;
	}

	// ponytail: round-robin eviction; a bar never shows 64 different badges at once, a per-button
	// string would be the upgrade if one ever does
	DisplayString *&slotString = theBadgeStrings[ theNextBadgeSlot ];
	theNextBadgeSlot = ( theNextBadgeSlot + 1 ) % BADGE_STRING_SLOTS;
	if( slotString == NULL )
	{
		slotString = TheDisplayStringManager->newDisplayString();
		if( slotString == NULL )
			return NULL;
	}

	slotString->setText( text );
	if( font != NULL )
		slotString->setFont( font );
	return slotString;

}  // end badgeString

// drawCountBadge =============================================================
/** Draw a small number badge in the button's bottom right corner (queued unit
	* counts, selection sizes).  One shared DisplayString serves every button. */
//=============================================================================
static void drawCountBadge( GameWindow *window, Int count )
{
	UnicodeString text;
	text.format( u"%d", count );
	drawBadge( window, text, COUNT_BADGE_DESIGN_POINTS, HUD_READOUT_BOTTOM_RIGHT, GameMakeColor( 255, 255, 255, 255 ) );

}  // end drawCountBadge

// drawBadge ==================================================================
/** One corner marking: `text` on its plate in `corner` of the button's inner
	* rectangle.  Text too wide for the button is set a point smaller until it
	* fits, down to BADGE_LEAST_POINTS. */
//=============================================================================
static void drawBadge( GameWindow *window, const UnicodeString &text, Real designPoints,
											 HudReadoutCorner corner, Color color )
{
	IRegion2D cell;
	ICoord2D size;
	window->winGetScreenPosition( &cell.lo.x, &cell.lo.y );
	window->winGetSize( &size.x, &size.y );
	cell.hi.x = cell.lo.x + size.x;
	cell.hi.y = cell.lo.y + size.y;

	GameFont *font = getBadgeFont( window, designPoints );
	DisplayString *badge = badgeString( text, font );
	if( badge == NULL )
		return;

	// each size tried is a string badgeString keeps, so a marking that had to shrink is looked up
	// the next frame, not lettered again
	while( font != NULL && font->pointSize > BADGE_LEAST_POINTS && !HudReadout_fits( badge, cell ) )
	{
		font = TheFontLibrary->getFont( font->nameString, font->pointSize - 1, TRUE );
		badge = badgeString( text, font );
	}

	HudReadout_draw( badge, cell, corner, color );

}  // end drawBadge

// drawClassicKeyBadge ========================================================
/** Classic's key letter on a square plate in the button's top left corner, the letter in the
	* middle of it.  The plate is the line's height square whatever the letter, so an "I" and an "M"
	* stand on the same plate and a row of buttons reads as one set of keys. */
//=============================================================================
static void drawClassicKeyBadge( GameWindow *window, const UnicodeString &text, Color color )
{
	ICoord2D origin, size;
	window->winGetScreenPosition( &origin.x, &origin.y );
	window->winGetSize( &size.x, &size.y );

	GameFont *font = getBadgeFont( window, CLASSIC_KEY_BADGE_DESIGN_POINTS );
	DisplayString *badge = badgeString( text, font );
	if( badge == NULL )
		return;

	Int textWidth = 0, textHeight = 0;
	badge->getSize( &textWidth, &textHeight );
	if( textWidth <= 0 || textHeight <= 0 )
		return;

	// one pixel in from the button's edge, never more than half the button high.  A letter sits
	// on a square of the line height so I and M share a plate.  A chord name (Ctrl on Force
	// Attack) is wider: grow the plate to the word, shrink the font if the button cannot hold it.
	const Int pad = 2;
	const Int maxW = size.x - 2;
	const Int maxH = min( size.x, size.y ) / 2;
	while( font != NULL && font->pointSize > BADGE_LEAST_POINTS && textWidth + pad > maxW )
	{
		font = TheFontLibrary->getFont( font->nameString, font->pointSize - 1, TRUE );
		badge = badgeString( text, font );
		if( badge == NULL )
			return;
		badge->getSize( &textWidth, &textHeight );
	}

	const Int plateH = min( textHeight, maxH );
	Int plateW = plateH;
	if( textWidth + pad > plateW )
		plateW = min( textWidth + pad, maxW );
	const Int left = origin.x + 1;
	const Int top = origin.y + 1;
	TheDisplay->drawFillRect( left, top, plateW, plateH, GameMakeColor( 2, 4, 5, 230 ) );
	TheDisplay->drawOpenRect( left, top, plateW, plateH, 1, GameMakeColor( 90, 90, 90, 255 ) );

	IRegion2D plate = { { left, top }, { left + plateW, top + plateH } };
	badge->setClipRegion( &plate );
	badge->draw( left + ( plateW - textWidth ) / 2, top + ( plateH - textHeight ) / 2, color, GameMakeColor( 0, 0, 0, 255 ) );

}  // end drawClassicKeyBadge

// drawRepeatMark =============================================================
/** The unit on this button is built on repeat: two arrows chasing each other round a ring, on the
	* key letter's square plate in the top right corner, turning once every REPEAT_TURN_MS.  Drawn in
	* lines rather than from art so it stays sharp at every HUD size, and turned by the wall clock,
	* which only the picture reads. */
//=============================================================================
static const UnsignedInt REPEAT_TURN_MS = 1200;

static void drawRepeatMark( GameWindow *window )
{
	ICoord2D origin, size;
	window->winGetScreenPosition( &origin.x, &origin.y );
	window->winGetSize( &size.x, &size.y );

	const Int side = max( 11, REAL_TO_INT( min( size.x, size.y ) * 0.4f ) );
	const Int left = origin.x + size.x - 1 - side;
	const Int top = origin.y + 1;
	TheDisplay->drawFillRect( left, top, side, side, GameMakeColor( 2, 4, 5, 230 ) );
	TheDisplay->drawOpenRect( left, top, side, side, 1, GameMakeColor( 90, 90, 90, 255 ) );

	const Color green = GameMakeColor( 110, 235, 90, 255 );
	const Real cx = left + side * 0.5f;
	const Real cy = top + side * 0.5f;
	const Real radius = side * 0.3f;
	const Real width = max( 1.5f, side * 0.1f );
	const Real head = side * 0.16f;
	const Real spin = ( Clock_Milliseconds() % REPEAT_TURN_MS ) * ( 2.0f * PI / REPEAT_TURN_MS );

	// two arcs of 130 degrees, half a turn apart, each with its head on the leading end
	const Int SEGMENTS = 6;
	const Real ARC = 130.0f * PI / 180.0f;
	for( Int arrow = 0; arrow < 2; ++arrow )
	{
		const Real start = spin + arrow * PI;
		for( Int s = 0; s < SEGMENTS; ++s )
		{
			const Real a0 = start + ARC * s / SEGMENTS;
			const Real a1 = start + ARC * ( s + 1 ) / SEGMENTS;
			TheDisplay->drawLine( REAL_TO_INT( cx + radius * cosf( a0 ) ), REAL_TO_INT( cy + radius * sinf( a0 ) ),
														REAL_TO_INT( cx + radius * cosf( a1 ) ), REAL_TO_INT( cy + radius * sinf( a1 ) ), width, green );
		}

		// the head: a point a little ahead along the ring, its barbs either side of the arc's end
		const Real end = start + ARC;
		const Real ex = cx + radius * cosf( end ), ey = cy + radius * sinf( end );
		const Real tx = -sinf( end ), ty = cosf( end );		// the way the ring turns
		const Real nx = cosf( end ), ny = sinf( end );		// out from the centre
		const Int px = REAL_TO_INT( ex + tx * head ), py = REAL_TO_INT( ey + ty * head );
		TheDisplay->drawLine( px, py, REAL_TO_INT( ex + nx * head ), REAL_TO_INT( ey + ny * head ), width, green );
		TheDisplay->drawLine( px, py, REAL_TO_INT( ex - nx * head ), REAL_TO_INT( ey - ny * head ), width, green );
	}

}  // end drawRepeatMark


///////////////////////////////////////////////////////////////////////////////
// PUBLIC FUNCTIONS ///////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

// W3DGadgetPushButtonDraw ====================================================
/** Draw colored pushbutton using standard graphics */
//=============================================================================
void W3DGadgetPushButtonDraw( GameWindow *window, WinInstanceData *instData )
{
	Color color, border;
	ICoord2D origin, size, start, end;

	// get window position and size
	window->winGetScreenPosition( &origin.x, &origin.y );
	window->winGetSize( &size.x, &size.y );

	//
	// get pointer to image we want to draw depending on our state,
	// see GadgetPushButton.h for info
	//
	if( BitTest( window->winGetStatus(), WIN_STATUS_ENABLED ) == FALSE )
	{

		if( BitTest( instData->getState(), WIN_STATE_SELECTED ) )
		{
			color			= GadgetButtonGetDisabledSelectedColor( window );
			border		= GadgetButtonGetDisabledSelectedBorderColor( window );
		}
		else
		{
			color			= GadgetButtonGetDisabledColor( window );
			border		= GadgetButtonGetDisabledBorderColor( window );
		}

	}  // end if, disabled
	else if( BitTest( instData->getState(), WIN_STATE_HILITED ) )
	{

		if( BitTest( instData->getState(), WIN_STATE_SELECTED ) )
		{
			color			= GadgetButtonGetHiliteSelectedColor( window );
			border		= GadgetButtonGetHiliteSelectedBorderColor( window );
		}
		else
		{
			color			= GadgetButtonGetHiliteColor( window );
			border		= GadgetButtonGetHiliteBorderColor( window );
		}

	}  // end else if, hilited and enabled
	else
	{

		if( BitTest( instData->getState(), WIN_STATE_SELECTED ) )
		{
			color			= GadgetButtonGetEnabledSelectedColor( window );
			border		= GadgetButtonGetEnabledSelectedBorderColor( window );
		}
		else
		{
			color			= GadgetButtonGetEnabledColor( window );
			border		= GadgetButtonGetEnabledBorderColor( window );
		}

	}  // end else, enabled only

	// compute draw position
	start.x = origin.x;
	start.y = origin.y;
	end.x = start.x + size.x;
	end.y = start.y + size.y;

	// box and border
	if( border != WIN_COLOR_UNDEFINED )
	{

		TheWindowManager->winOpenRect( border, WIN_DRAW_LINE_WIDTH,
																	 start.x, start.y, end.x, end.y );

	}  // end if

	if( color != WIN_COLOR_UNDEFINED )
	{

		// draw inside border
		start.x++;
		start.y++;
		end.x--;
		end.y--;
		TheWindowManager->winFillRect( color, WIN_DRAW_LINE_WIDTH,
																	 start.x, start.y, end.x, end.y );

	}  // end if

	// the button text is drawn after the clock overlay, further down

	// if we have a video buffer, draw the video buffer
	if ( instData->m_videoBuffer )
	{
		TheDisplay->drawVideoBuffer( instData->m_videoBuffer, origin.x, origin.y, origin.x + size.x, origin.y + size.y );
	}
	
	PushButtonData *pData = (PushButtonData *)window->winGetUserData();
	if( pData )
	{
		if( pData->overlayImage )
		{
			//Render the overlay image now.
			TheDisplay->drawImage( pData->overlayImage, origin.x, origin.y, origin.x + size.x, origin.y + size.y );
		}

		if( pData->drawClock )
		{
			if( pData->drawClock == NORMAL_CLOCK )
			{
				TheDisplay->drawRectClock(origin.x, origin.y, size.x, size.y, pData->percentClock,pData->colorClock);
			}
			else if( pData->drawClock == INVERSE_CLOCK )
			{
				TheDisplay->drawRemainingRectClock( origin.x, origin.y, size.x, size.y, pData->percentClock,pData->colorClock );
			}
			// the clock is not consumed by drawing it - see GadgetButtonDrawClock
		}

		if( pData->drawBorder && pData->colorBorder != GAME_COLOR_UNDEFINED )
		{
			TheDisplay->drawOpenRect(origin.x -1, origin.y - 1, size.x + 2, size.y + 2,1 , pData->colorBorder);
		}

		if( pData->drawCount > 0 )
			drawCountBadge( window, pData->drawCount );

		if( pData->drawRepeat )
			drawRepeatMark( window );
	}

	// the text goes on last: the cooldown clock sweeps across the whole button and used to
	// bury the shortcut key letter under it
	if( instData->getTextLength() )
		drawButtonText( window, instData );

}  // end W3DGadgetPushButtonDraw




// W3DGadgetPushButtonImageDraw ===============================================
/** Draw pushbutton with user supplied images */
//=============================================================================
void W3DGadgetPushButtonImageDraw( GameWindow *window, 
																	 WinInstanceData *instData )
{
	// if we return NULL then we'll call the one picture drawing code, if we return a value
	// then we'll call the 3 picture drawing code
	if( GadgetButtonGetMiddleEnabledImage( window ) ) 
	{
		if( BitTest( instData->getState(), WIN_STATUS_USE_OVERLAY_STATES ) )
		{
			ICoord2D size, start;
			// get window position
			window->winGetScreenPosition( &start.x, &start.y );
			window->winGetSize( &size.x, &size.y );
			// offset position by image offset
			start.x += instData->m_imageOffset.x;
			start.y += instData->m_imageOffset.y;

			DEBUG_CRASH( ("Button at %d,%d is attempting to render with W3DGadgetPushButtonImageDrawThree(), but is using overlay states! Forcing the code to use W3DGadgetPushButtonImageDrawOne() instead.", start.x, start.y ) );
			W3DGadgetPushButtonImageDrawOne( window, instData );
		}
		else
		{
			W3DGadgetPushButtonImageDrawThree( window, instData );
		}
	}
	else
	{
		W3DGadgetPushButtonImageDrawOne( window, instData );
	}
}

void W3DGadgetPushButtonImageDrawOne( GameWindow *window, 
																	 WinInstanceData *instData )
{
	const Image *image = NULL;
	ICoord2D size, start, end;

	//
	// get pointer to image we want to draw depending on our state,
	// see GadgetPushButton.h for info
	//
	image = GadgetButtonGetEnabledImage( window );

	if( !BitTest( window->winGetStatus(), WIN_STATUS_USE_OVERLAY_STATES ) )
	{
		//Certain buttons have the option to specify specific images for
		//altered states. If they do, then we won't render the auto-overlay versions.
		if( BitTest( window->winGetStatus(), WIN_STATUS_ENABLED ) == FALSE )
		{

			if( BitTest( instData->getState(), WIN_STATE_SELECTED ) )
				image			= GadgetButtonGetDisabledSelectedImage( window );
			else
				image			= GadgetButtonGetDisabledImage( window );

		}  // end if, disabled
		else if( BitTest( instData->getState(), WIN_STATE_HILITED ) )
		{

			if( BitTest( instData->getState(), WIN_STATE_SELECTED ) )
				image			= GadgetButtonGetHiliteSelectedImage( window );
			else
				image			= GadgetButtonGetHiliteImage( window );

		}  // end else if, hilited and enabled
		else
		{

			if( BitTest( instData->getState(), WIN_STATE_SELECTED ) )
				image			= GadgetButtonGetHiliteSelectedImage( window );
		}  // end else, enabled only
	}


	// draw the image
	if( image )
	{

		// get window position
		window->winGetScreenPosition( &start.x, &start.y );
		window->winGetSize( &size.x, &size.y );


		// offset position by image offset
		start.x += instData->m_imageOffset.x;
		start.y += instData->m_imageOffset.y;

		// find end point
		end.x = start.x + size.x;
		end.y = start.y + size.y;

		Display::DrawImageMode	drawMode=Display::DRAW_IMAGE_ALPHA;
		Int colorMultiplier = 0xffffffff;

		if(BitTest( window->winGetStatus(), WIN_STATUS_USE_OVERLAY_STATES ) )
		{	
			//we're using a new drawing system which does "grayscale" disabled buttons using original color artwork.
			if( !BitTest( window->winGetStatus(), WIN_STATUS_ENABLED ) )
			{
				if( !BitTest( window->winGetStatus(), WIN_STATUS_NOT_READY ) )
				{
					//The button is disabled -- but if the button isn't "ready", we don't want to do this because
					//we want to show the button in color with just the clock overlay.
					if( !BitTest( window->winGetStatus(), WIN_STATUS_ALWAYS_COLOR ) )
					{
						drawMode=Display::DRAW_IMAGE_GRAYSCALE;
					}
					else
					{
						colorMultiplier = 0xff909090; //RGB values are 144/255 (90) -- Alpha is opaque (ff) --> ff909090;
					}
				}
			}
		}
		TheDisplay->drawImage( image, start.x, start.y, end.x, end.y, colorMultiplier, drawMode );
	}  // end if

	// the button text is drawn at the very end, after the clock and the state overlays

	// get window position
	window->winGetScreenPosition( &start.x, &start.y );
	window->winGetSize( &size.x, &size.y );


	// if we have a video buffer, draw the video buffer
	if ( instData->m_videoBuffer )
	{
		TheDisplay->drawVideoBuffer( instData->m_videoBuffer, start.x, start.y, start.x + size.x, start.y + size.y );
	}
	PushButtonData *pData = (PushButtonData *)window->winGetUserData();

	if( pData )
	{
		if( pData->overlayImage )
		{
			//Render the overlay image now.
			TheDisplay->drawImage( pData->overlayImage, start.x, start.y, start.x + size.x, start.y + size.y );
		}
		
		if( pData->drawClock )
		{
			if( pData->drawClock == NORMAL_CLOCK )
			{
				TheDisplay->drawRectClock(start.x, start.y, size.x, size.y, pData->percentClock,pData->colorClock);
			}
			else if( pData->drawClock == INVERSE_CLOCK )
			{
				TheDisplay->drawRemainingRectClock( start.x, start.y, size.x, size.y, pData->percentClock,pData->colorClock );
			}
			// the clock is not consumed by drawing it - see GadgetButtonDrawClock
		}
		
		if( pData->drawBorder && pData->colorBorder != GAME_COLOR_UNDEFINED )
		{

			TheDisplay->drawOpenRect(start.x - 1, start.y - 1, size.x + 2, size.y + 2, 1, pData->colorBorder);

		}

		if( pData->drawCount > 0 )
			drawCountBadge( window, pData->drawCount );

		if( pData->drawRepeat )
			drawRepeatMark( window );
	}

	//Now render overlays that pertain to the correct state.

	if( BitTest( window->winGetStatus(), WIN_STATUS_FLASHING ) )
	{
		//Handle cameo flashing (let the flashing stack with overlay states)
		static const Image *hilitedOverlayIcon = TheMappedImageCollection->findImageByName( "Cameo_push" );
		TheDisplay->drawImage( hilitedOverlayIcon, start.x, start.y, start.x + size.x, start.y + size.y );
	}
	
	if( BitTest( window->winGetStatus(), WIN_STATUS_USE_OVERLAY_STATES ) )
	{
		image = NULL;
		static const Image *pushedOverlayIcon	= TheMappedImageCollection->findImageByName( "Cameo_push" );
		static const Image *hilitedOverlayIcon = TheMappedImageCollection->findImageByName( "Cameo_hilited" );
		if( pushedOverlayIcon && hilitedOverlayIcon )
		{
			if(BitTest(window->winGetStatus(), WIN_STATUS_ENABLED))
			{
				if (BitTest( instData->getState(), WIN_STATE_HILITED ))
				{
					if( BitTest( instData->getState(), WIN_STATE_SELECTED ) )
					{
						//The button is hilited and pushed
						TheDisplay->drawImage( pushedOverlayIcon, start.x, start.y, start.x + size.x, start.y + size.y );
					}
					else
					{
						//The button is hilited
						TheDisplay->drawImage( hilitedOverlayIcon, start.x, start.y, start.x + size.x, start.y + size.y );
					}
				}
  			else if( BitTest( instData->getState(), WIN_STATE_SELECTED ) )
 				{
 					//The button appears to be pushed -- CHECK_LIKE buttons that are on.
 					TheDisplay->drawImage( pushedOverlayIcon, start.x, start.y, start.x + size.x, start.y + size.y );
  			}
			}
		}
	}

	// the text goes on last: the cooldown clock and the state overlays both sweep across the
	// whole button and used to bury the shortcut key letter under them
	if( instData->getTextLength() )
		drawButtonText( window, instData );

}  // end W3DGadgetPushButtonImageDraw


void W3DGadgetPushButtonImageDrawThree(GameWindow *window, WinInstanceData *instData )
{

	const Image *leftImage, *rightImage, *centerImage;
	ICoord2D origin, size, start, end;
	Int xOffset, yOffset;
	Int i;

	// get screen position and size
	window->winGetScreenPosition( &origin.x, &origin.y );
	window->winGetSize( &size.x, &size.y );

	// get image offset
	xOffset = instData->m_imageOffset.x;
	yOffset = instData->m_imageOffset.y;


	//
	// get pointer to image we want to draw depending on our state,
	// see GadgetPushButton.h for info
	//
	if( BitTest( window->winGetStatus(), WIN_STATUS_ENABLED ) == FALSE )
	{

		if( BitTest( instData->getState(), WIN_STATE_SELECTED ) )
		{
			leftImage					= GadgetButtonGetLeftDisabledSelectedImage( window );
			rightImage				= GadgetButtonGetRightDisabledSelectedImage( window );
			centerImage				= GadgetButtonGetMiddleDisabledSelectedImage( window );
		}
		else
		{

			leftImage					= GadgetButtonGetLeftDisabledImage( window );
			rightImage				= GadgetButtonGetRightDisabledImage( window );
			centerImage				= GadgetButtonGetMiddleDisabledImage( window );

		}

	}  // end if, disabled
	else if( BitTest( instData->getState(), WIN_STATE_HILITED ) )
	{

		if( BitTest( instData->getState(), WIN_STATE_SELECTED ) )
		{
			leftImage					= GadgetButtonGetLeftHiliteSelectedImage( window );
			rightImage				= GadgetButtonGetRightHiliteSelectedImage( window );
			centerImage				= GadgetButtonGetMiddleHiliteSelectedImage( window );
		}
		else
		{

			leftImage					= GadgetButtonGetLeftHiliteImage( window );
			rightImage				= GadgetButtonGetRightHiliteImage( window );
			centerImage				= GadgetButtonGetMiddleHiliteImage( window );

		}

	}  // end else if, hilited and enabled
	else
	{

		if( BitTest( instData->getState(), WIN_STATE_SELECTED ) )
		{
			leftImage					= GadgetButtonGetLeftEnabledSelectedImage( window );
			rightImage				= GadgetButtonGetRightEnabledSelectedImage( window );
			centerImage				= GadgetButtonGetMiddleEnabledSelectedImage( window );
		}
		else
		{

			leftImage					= GadgetButtonGetLeftEnabledImage( window );
			rightImage				= GadgetButtonGetRightEnabledImage( window );
			centerImage				= GadgetButtonGetMiddleEnabledImage( window );

		}

	}  // end else, enabled only

	// sanity, we need to have these images to make it look right
	if( leftImage == NULL || rightImage == NULL || 
			centerImage == NULL )
		return;

	// get image sizes for the ends
	ICoord2D leftSize, rightSize;
	leftSize.x = leftImage->getImageWidth();
	leftSize.y = leftImage->getImageHeight();
	rightSize.x = rightImage->getImageWidth();
	rightSize.y = rightImage->getImageHeight();

	// get two key points used in the end drawing
	ICoord2D leftEnd, rightStart;
	leftEnd.x = origin.x + leftSize.x + xOffset;
	leftEnd.y = origin.y + size.y + yOffset;
	rightStart.x = origin.x + size.x - rightSize.x + xOffset;
	rightStart.y = origin.y + yOffset;

	// draw the center repeating bar
	Int centerWidth, pieces;

	// get width we have to draw our repeating center in
	centerWidth = rightStart.x - leftEnd.x;
	
	if( centerWidth <= 0)
	{
		// draw left end
		start.x = origin.x + xOffset;
		start.y = origin.y + yOffset;
		end.y = leftEnd.y;
		end.x = origin.x + xOffset + size.x/2;
		TheWindowManager->winDrawImage(leftImage, start.x, start.y, end.x, end.y);

		// draw right end
		start.y = rightStart.y;
		start.x = end.x;
		end.x = origin.x + size.x;
		end.y = start.y + size.y;
		TheWindowManager->winDrawImage(rightImage, start.x, start.y, end.x, end.y);
	}
	else
	{
		
		// how many whole repeating pieces will fit in that width
		pieces = centerWidth / centerImage->getImageWidth();

		// draw the pieces
		start.x = leftEnd.x;
		start.y = origin.y + yOffset;
		end.y = start.y + size.y + yOffset; //centerImage->getImageHeight() + yOffset;
		for( i = 0; i < pieces; i++ )
		{

			end.x = start.x + centerImage->getImageWidth();
			TheWindowManager->winDrawImage( centerImage, 
																			start.x, start.y,
																			end.x, end.y );
			start.x += centerImage->getImageWidth();

		}  // end for i

		// we will draw the image but clip the parts we don't want to show
		IRegion2D reg;
		reg.lo.x = start.x;
		reg.lo.y = start.y;
		reg.hi.x = rightStart.x;
		reg.hi.y = end.y;
		centerWidth = rightStart.x - start.x;
		if( centerWidth > 0)
		{
			TheDisplay->setClipRegion(&reg);
			end.x = start.x + centerImage->getImageWidth();
			TheWindowManager->winDrawImage( centerImage,
																			start.x, start.y,
																			end.x, end.y );
			TheDisplay->enableClipping(FALSE);
		}

		// draw left end
		start.x = origin.x + xOffset;
		start.y = origin.y + yOffset;
		end = leftEnd;
		TheWindowManager->winDrawImage(leftImage, start.x, start.y, end.x, end.y);

		// draw right end
		start = rightStart;
		end.x = start.x + rightSize.x;
		end.y = start.y + size.y;
		TheWindowManager->winDrawImage(rightImage, start.x, start.y, end.x, end.y);
	}

	// the button text is drawn at the very end, after the clock overlay

	// get window position
	window->winGetScreenPosition( &start.x, &start.y );
	window->winGetSize( &size.x, &size.y );


	// if we have a video buffer, draw the video buffer
	if ( instData->m_videoBuffer )
	{
		TheDisplay->drawVideoBuffer( instData->m_videoBuffer, start.x, start.y, start.x + size.x, start.y + size.y );
	}
	PushButtonData *pData = (PushButtonData *)window->winGetUserData();

	if( pData )
	{
		if( pData->overlayImage )
		{
			//Render the overlay image now.
			TheDisplay->drawImage( pData->overlayImage, origin.x, origin.y, origin.x + size.x, origin.y + size.y );
		}

		if( pData->drawClock )
		{
			if( pData->drawClock == NORMAL_CLOCK )
			{
				TheDisplay->drawRectClock(start.x, start.y, size.x, size.y, pData->percentClock,pData->colorClock);
			}
			else if( pData->drawClock == INVERSE_CLOCK )
			{
				TheDisplay->drawRemainingRectClock( start.x, start.y, size.x, size.y, pData->percentClock,pData->colorClock );
			}
			// the clock is not consumed by drawing it - see GadgetButtonDrawClock
		}
		
		if( pData->drawBorder && pData->colorBorder != GAME_COLOR_UNDEFINED )
		{
			TheDisplay->drawOpenRect(start.x - 1, start.y - 1, size.x + 2, size.y + 2, 1, pData->colorBorder);
		}

		if( pData->drawCount > 0 )
			drawCountBadge( window, pData->drawCount );

		if( pData->drawRepeat )
			drawRepeatMark( window );
	}

	// the text goes on last: the cooldown clock sweeps across the whole button and used to
	// bury the shortcut key letter under it
	if( instData->getTextLength() )
		drawButtonText( window, instData );
}
