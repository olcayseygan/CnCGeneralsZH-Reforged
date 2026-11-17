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

// FILE: Display.cpp //////////////////////////////////////////////////////////
// The implementation of the Display class
// Author: Michael S. Booth, March 2001

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine
#include "Lib/Clock.h"
#include "Common/GlobalData.h"
#include "Common/OptionsCatalog.h"

#include "GameClient/Display.h"
#include "GameClient/Mouse.h"
#include "GameClient/VideoPlayer.h"
#include "GameClient/DisplayStringManager.h"
#include "GameClient/GameText.h"
#include "GameClient/GlobalLanguage.h"
//#include "GameLogic/ScriptEngine.h"
//#include "GameLogic/GameLogic.h"

/// The Display singleton instance.
Display *TheDisplay = NULL;

UIRect TheUIRect( void )
{
	if( TheDisplay == NULL )
	{
		UIRect none = { 0, 0, 800, 600 };
		return none;
	}
	return UIRectForScreen( TheDisplay->getWidth(), TheDisplay->getHeight() );
}

//-------------------------------------------------------------------------------------------------
static void setRegion( IRegion2D *r, Int x0, Int y0, Int x1, Int y1 )
{
	r->lo.x = x0;
	r->lo.y = y0;
	r->hi.x = x1;
	r->hi.y = y1;
}

//-------------------------------------------------------------------------------------------------
IRegion2D HudAreaForScreen( Int screenWidth, Int screenHeight, Int hudWidth )
{
	IRegion2D r;
	setRegion( &r, 0, 0, screenWidth > 0 ? screenWidth : 0, screenHeight > 0 ? screenHeight : 0 );
	if( screenWidth <= 0 || screenHeight <= 0 )
		return r;

	Int width = screenWidth;
	if( hudWidth == HUD_WIDTH_16_9 )
		width = ( screenHeight * 16 + 4 ) / 9;
	else if( hudWidth == HUD_WIDTH_21_9 )
		width = ( screenHeight * 43 + 9 ) / 18;
	if( width >= screenWidth )
		return r;

	r.lo.x = ( screenWidth - width ) / 2;
	r.hi.x = r.lo.x + width;
	return r;
}

//-------------------------------------------------------------------------------------------------
IRegion2D HudFitAspect( const IRegion2D &cell, Int w, Int h )
{
	const Int cw = cell.width(), ch = cell.height();
	if( cw <= 0 || ch <= 0 || w <= 0 || h <= 0 )
		return cell;
	Int fw = cw, fh = ( cw * h + w / 2 ) / w;
	if( fh > ch )
	{
		fh = ch;
		fw = MIN( cw, ( ch * w + h / 2 ) / h );
	}
	const Int x = cell.lo.x + ( cw - fw ) / 2, y = cell.lo.y + ( ch - fh ) / 2;
	IRegion2D r;
	setRegion( &r, x, y, x + fw, y + fh );
	return r;
}

//-------------------------------------------------------------------------------------------------
/** The design's coordinates (1920x1080) mapped by edge, never by size, so cells that touch in the
	* design touch on the screen and cells that do not cannot come to overlap by rounding.  Each group
	* maps from the edge it leans on: the sidebar from its left and the screen's top (its arrows and
	* the foot of the power rail from the bottom), the strip and the card from the area's left and the
	* screen's bottom, the clock from the strip's right, the powers window and the superweapon rows
	* from the sidebar's left. */
//-------------------------------------------------------------------------------------------------
namespace
{
	enum { DESIGN_W = 1920, DESIGN_H = 1080, DESIGN_SIDEBAR_X = 1600, DESIGN_STRIP_Y = 1016 };

	/// The design width the strip's 14 buttons, its clock and the sidebar need side by side: buttons
	/// to 900, 8 clear, the 96 clock, 4 to the edge, 320 of sidebar.
	enum { DESIGN_MIN_AREA_W = 1008 + 320 };

	struct HudMapper
	{
		Real k;
		Int height;
		Int sidebarX;		///< the sidebar's left edge on the screen
		Int areaX;			///< the area's left edge
		Int stripRight;	///< the strip's right edge (= sidebarX)

		Int len( Real design ) const { return (Int)( design * k + 0.5f ); }

		// sidebar: x from its left, y from the top or from the bottom
		Int sx( Real x ) const { return sidebarX + len( x - DESIGN_SIDEBAR_X ); }
		Int ty( Real y ) const { return len( y ); }
		Int by( Real y ) const { return height - len( DESIGN_H - y ); }
		// strip and card: x from the area's left, y from the bottom
		Int ax( Real x ) const { return areaX + len( x ); }
		// the clock: x from the strip's right
		Int crx( Real x ) const { return stripRight - len( DESIGN_SIDEBAR_X - x ); }
		// leaning on the sidebar's left edge from the world side
		Int lx( Real x ) const { return sidebarX - len( DESIGN_SIDEBAR_X - x ); }

		void sidebarTop( IRegion2D *r, Real x, Real y, Real w, Real h ) const
		{
			setRegion( r, sx( x ), ty( y ), sx( x + w ), ty( y + h ) );
		}
		void sidebarBottom( IRegion2D *r, Real x, Real y, Real w, Real h ) const
		{
			setRegion( r, sx( x ), by( y ), sx( x + w ), by( y + h ) );
		}
		void areaBottom( IRegion2D *r, Real x, Real y, Real w, Real h ) const
		{
			setRegion( r, ax( x ), by( y ), ax( x + w ), by( y + h ) );
		}
	};
}

//-------------------------------------------------------------------------------------------------
void HudLayoutForScreen( Int screenWidth, Int screenHeight, Int layout, Int hudWidth, HudLayoutRects *out )
{
	if( out == NULL )
		return;
	memset( out, 0, sizeof( *out ) );
	if( screenWidth <= 0 || screenHeight <= 0 )
		return;

	out->layout = ( layout == HUD_LAYOUT_SIDEBAR ) ? HUD_LAYOUT_SIDEBAR : HUD_LAYOUT_CLASSIC;
	setRegion( &out->screen, 0, 0, screenWidth, screenHeight );
	out->world = out->screen;
	out->area = HudAreaForScreen( screenWidth, screenHeight, hudWidth );
	out->play = out->area;

	const Int areaW = out->area.width();
	Real k = (Real)screenHeight / DESIGN_H;
	if( (Real)areaW / DESIGN_MIN_AREA_W < k )
		k = (Real)areaW / DESIGN_MIN_AREA_W;		// narrower than 1328:1080: shrink so the strip still fits
	out->scale = k;
	if( out->layout == HUD_LAYOUT_CLASSIC )
		return;

	HudMapper m;
	m.k = k;
	m.height = screenHeight;
	m.areaX = out->area.lo.x;
	m.sidebarX = out->area.hi.x - m.len( DESIGN_W - DESIGN_SIDEBAR_X );
	m.stripRight = m.sidebarX;

	const Int stripTop = m.by( DESIGN_STRIP_Y );
	setRegion( &out->sidebar, m.sidebarX, 0, out->area.hi.x, screenHeight );
	setRegion( &out->strip, out->area.lo.x, stripTop, m.sidebarX, screenHeight );
	setRegion( &out->play, out->area.lo.x, 0, m.sidebarX, stripTop );

	// the sidebar, top down
	// down to the tabs one grid: four columns 70 wide, 8 apart, over the radar's 1608..1912; the
	// money and the General button take two columns each
	const Real COL_X = 1608, COL_W = 70, COL_PITCH = 78;
	auto column = [ & ]( IRegion2D *r, Int first, Int span, Real y, Real h )
	{
		m.sidebarTop( r, COL_X + COL_PITCH * first, y, COL_W + COL_PITCH * ( span - 1 ), h );
	};
	column( &out->menuButton, 0, 1, 8, 32 );
	column( &out->money, 1, 2, 8, 32 );
	column( &out->diplomacyButton, 3, 1, 8, 32 );
	m.sidebarTop( &out->radar, 1608, 56, 304, 304 );
	column( &out->sellButton, 0, 1, 368, 36 );
	column( &out->repairButton, 1, 1, 368, 36 );
	column( &out->generalButton, 2, 2, 368, 36 );
	for( Int i = 0; i < HUD_SIDEBAR_TABS; ++i )
		column( &out->tabs[ i ], i, 1, 420, 36 );
	// under the tabs the rail runs down the left from the picker's top to the arrows' foot
	setRegion( &out->powerRail, m.sx( 1608 ), m.ty( 459 ), m.sx( 1620 ), m.by( 1072 ) );
	// "All" and the four chips, one size, packed from the left 6 apart as the cameo wells are (pitch
	// 56, to 1898), 3 under the tabs; the cameo grid 4 under them.  Three wells of 92x75 6 apart fill
	// 1624..1912 and seven rows 4 apart reach 1040, 4 over the arrows: the cameo 3 inside a well is
	// 86x69, the art's 5:4 to a fifth of a pixel
	m.sidebarTop( &out->pickerAll, 1624, 459, 50, 28 );
	for( Int i = 0; i < HUD_SIDEBAR_PICKER_CHIPS; ++i )
		m.sidebarTop( &out->pickerChips[ i ], 1680.0f + 56.0f * i, 459, 50, 28 );
	for( Int row = 0; row < HUD_SIDEBAR_CAMEO_ROWS; ++row )
		for( Int col = 0; col < HUD_SIDEBAR_CAMEO_COLUMNS; ++col )
			m.sidebarTop( &out->cameos[ row * HUD_SIDEBAR_CAMEO_COLUMNS + col ],
										1624.0f + 98.0f * col, 491.0f + 79.0f * row, 92, 75 );
	m.sidebarBottom( &out->arrowUp, 1624, 1044, 140, 28 );
	m.sidebarBottom( &out->arrowDown, 1772, 1044, 140, 28 );

	// the strip: the buttons at the art's own 60x48, 8 in from the strip's top and foot as the clock is
	for( Int i = 0; i < HUD_STRIP_BUTTONS; ++i )
		m.areaBottom( &out->stripButtons[ i ], 8.0f + 64.0f * i, 1024, HUD_CAMEO_ART_W, HUD_CAMEO_ART_H );
	setRegion( &out->stripClock, m.crx( 1500 ), m.by( 1024 ), m.crx( 1596 ), m.by( 1072 ) );
	setRegion( &out->stripSpare, m.ax( 908 ), m.by( 1024 ), m.crx( 1492 ), m.by( 1072 ) );
	if( out->stripSpare.hi.x < out->stripSpare.lo.x )
		out->stripSpare.hi.x = out->stripSpare.lo.x;

	// the selection card
	// welded to the strip: its foot is the strip's top, its left the area's
	m.areaBottom( &out->card, 0, 896, 470, 120 );
	m.areaBottom( &out->medallion, 0, 896, 136, 120 );
	m.areaBottom( &out->portrait, 8, 904, 116, 90 );
	m.areaBottom( &out->rankRect, 10, 998, 112, 14 );
	m.areaBottom( &out->labelPlate, 136, 932, 334, 84 );
	m.areaBottom( &out->nameText, 146, 935, 274, 18 );
	m.areaBottom( &out->healthBar, 146, 956, 316, 6 );
	m.areaBottom( &out->tray, 140, 966, 326, 50 );
	for( Int i = 0; i < HUD_TRAY_CELLS; ++i )
		m.areaBottom( &out->trayCells[ i ], 146.0f + 53.0f * i, 970, 50, 42 );

	// against the sidebar's left edge
	setRegion( &out->powersWindow, m.lx( 1400 ), m.ty( 64 ), m.lx( 1592 ), m.ty( 304 ) );
	setRegion( &out->superweaponRow, m.lx( 1312 ), m.by( 968 ), m.lx( 1600 ), m.by( 1008 ) );
	out->superweaponRowPitch = m.len( 44 );
}


Display::Display()
{
	m_viewList = NULL;
	m_width = 0;
	m_height = 0;
	m_bitDepth = 0;
	m_windowed = FALSE;
	m_videoBuffer = NULL;
	m_videoStream = NULL;
	m_debugDisplayCallback = NULL;
	m_debugDisplayUserData = NULL;
	m_debugDisplay = NULL;
	m_letterBoxFadeLevel = 0;
	m_letterBoxEnabled = FALSE;
	m_letterBoxAspect = 16.0f / 9.0f;
	m_cinematicText = AsciiString::TheEmptyString;
	m_cinematicFont = NULL;
	m_cinematicTextFrames = 0;  
	m_movieHoldTime	= -1;
	m_copyrightHoldTime = -1;
	m_elapsedMovieTime = 0;
	m_elapsedCopywriteTime = 0;
	m_copyrightDisplayString = NULL;

	// Added by Sadullah Nader
	// Initializations missing and needed
	m_currentlyPlayingMovie.clear();
	m_letterBoxFadeStartTime = 0;
	// End Add
}

/**
 * Destructor for the Display.  Destroy all views attached to it.
 */
Display::~Display()
{

	stopMovie();
	// delete all our views if present
	deleteViews();
	// before the string manager goes (GameClient's destructor deletes this first)
	freeDeferred();

}

/**
	* Delete all views in the Display
	*/
void Display::deleteViews( void )
{
	View *v, *next;

	for( v = m_viewList; v; v = next )
	{
		next = v->getNextView();
		delete v;
	}
	m_viewList = NULL;
}

/**
 * Attach the given view to the world
 * @todo Rethink the "attachView" notion...
 */
void Display::attachView( View *view )
{
	// prepend to head of list
	m_viewList = view->prependViewToList( m_viewList );
}

/**
 * The strings a recorded overlay freed (beginRecording), once nothing recorded can draw them
 */
void Display::freeDeferred( void )
{
	for( size_t i = 0; i < m_deferredFrees.size(); ++i )
		TheDisplayStringManager->freeDisplayString( m_deferredFrees[ i ] );
	m_deferredFrees.clear();
}

/**
 * Render all views of the world
 */
void Display::drawViews( void )
{

	for( View *v = m_viewList; v; v = v->getNextView() )
		v->drawView();

}

/**
 * Updates all views of the world.  This forces state variables
   to refresh without actually drawing anything.
 */
void Display::updateViews( void )
{

	for( View *v = m_viewList; v; v = v->getNextView() )
		v->updateView();

}

/// Redraw the entire display
void Display::draw( void )
{
	// redraw all views
	drawViews();
	
	// redraw the in-game user interface
	/// @todo Switch between in-game and shell interfaces

}

/** Sets screen resolution/mode*/
Bool Display::setDisplayMode( UnsignedInt xres, UnsignedInt yres, UnsignedInt bitdepth, Bool windowed )
{
	//Get old values
	UnsignedInt oldDisplayHeight=getHeight();
	UnsignedInt oldDisplayWidth=getWidth();
	Int oldViewWidth=TheTacticalView->getWidth();
	Int oldViewHeight=TheTacticalView->getHeight();
	Int oldViewOriginX,oldViewOriginY;
	TheTacticalView->getOrigin(&oldViewOriginX,&oldViewOriginY);

	setWidth(xres);
	setHeight(yres);

	//Adjust view to match previous proportions
	TheTacticalView->setWidth((Real)oldViewWidth/(Real)oldDisplayWidth*(Real)xres);
	TheTacticalView->setHeight((Real)oldViewHeight/(Real)oldDisplayHeight*(Real)yres);
	TheTacticalView->setOrigin((Real)oldViewOriginX/(Real)oldDisplayWidth*(Real)xres,
	(Real)oldViewOriginY/(Real)oldDisplayHeight*(Real)yres);
	return TRUE;
}

// Display::setWidth ==========================================================
/** Set the width of the display */
//=============================================================================
void Display::setWidth( UnsignedInt width )
{

	// set the new width
	m_width = width;

	// set the new mouse limits
	if( TheMouse )
		TheMouse->setMouseLimits();

}  // end setWidth

// Display::setHeight =========================================================
/** Set the height of the display */
//=============================================================================
void Display::setHeight( UnsignedInt height )
{

	// se the new height
	m_height = height;

	// set the new mouse limits
	if( TheMouse )
		TheMouse->setMouseLimits();

}  // end setHeight

//============================================================================
// Display::playLogoMovie
// minMovieLength is in milliseconds
// minCopyrightLength
//============================================================================

void Display::playLogoMovie( AsciiString movieName, Int minMovieLength, Int minCopyrightLength )
{
	
	stopMovie();

	m_videoStream = TheVideoPlayer->open( movieName );

	if ( m_videoStream == NULL )
	{
		return;
	}
	
	m_currentlyPlayingMovie = movieName;
	m_movieHoldTime = minMovieLength;
	m_copyrightHoldTime = minCopyrightLength;
	m_elapsedMovieTime = Clock_Milliseconds();  // we're using time get time becuase legal want's actual "Seconds"
	
	m_videoBuffer = createVideoBuffer();
	if (	m_videoBuffer == NULL || 
				!m_videoBuffer->allocate(	m_videoStream->width(), 
													m_videoStream->height())
		)
	{
		stopMovie();
		return;
	}
	
}

//============================================================================
// Display::playMovie
//============================================================================

void Display::playMovie( AsciiString movieName)
{
	
	stopMovie();



	m_videoStream = TheVideoPlayer->open( movieName );

	if ( m_videoStream == NULL )
	{
		return;
	}
	
	m_currentlyPlayingMovie = movieName;

	m_videoBuffer = createVideoBuffer();
	if (	m_videoBuffer == NULL || 
				!m_videoBuffer->allocate(	m_videoStream->width(), 
													m_videoStream->height())
		)
	{
		stopMovie();
		return;
	}
	
}

//============================================================================
// Display::stopMovie
//============================================================================

void Display::stopMovie( void )
{
	delete m_videoBuffer;
	m_videoBuffer = NULL;

	if ( m_videoStream )
	{
		m_videoStream->close();
		m_videoStream = NULL;
	}

	if (!m_currentlyPlayingMovie.isEmpty()) {
		//TheScriptEngine->notifyOfCompletedVideo(m_currentlyPlayingMovie); // Removing this sync-error cause MDC
		m_currentlyPlayingMovie = AsciiString::TheEmptyString;
	}
	if(m_copyrightDisplayString)
	{
		TheDisplayStringManager->freeDisplayString(m_copyrightDisplayString);
		m_copyrightDisplayString = NULL;
	}
	m_copyrightHoldTime = -1;
	m_movieHoldTime = -1;
}

//============================================================================
// Display::update
//============================================================================

void Display::update( void )
{
	if ( m_videoStream && m_videoBuffer )
	{
		if ( m_videoStream->isFrameReady())
		{
			m_videoStream->frameDecompress();
			m_videoStream->frameRender( m_videoBuffer );
			if( m_videoStream->frameIndex() != m_videoStream->frameCount() - 1)
				m_videoStream->frameNext();
			else if( m_copyrightHoldTime >= 0 ||m_movieHoldTime >= 0 )
			{
				if( m_elapsedCopywriteTime == 0 && m_elapsedCopywriteTime >= 0)
				{
					//display the copyrighttext;		
					if(m_copyrightDisplayString)
						m_copyrightDisplayString->deleteInstance();
					m_copyrightDisplayString = TheDisplayStringManager->newDisplayString();
					m_copyrightDisplayString->setText(TheGameText->fetch("GUI:EACopyright"));
					if (TheGlobalLanguageData && TheGlobalLanguageData->m_copyrightFont.name.isNotEmpty())
					{	FontDesc	*fontdesc=&TheGlobalLanguageData->m_copyrightFont;
						m_copyrightDisplayString->setFont(TheFontLibrary->getFont(fontdesc->name,
							TheGlobalLanguageData->adjustFontSize(fontdesc->size),
							fontdesc->bold));	
					}
					else
						m_copyrightDisplayString->setFont(TheFontLibrary->getFont("Courier", 
						TheGlobalLanguageData->adjustFontSize(12), TRUE));	
					m_elapsedCopywriteTime = Clock_Milliseconds();
				}
				if(m_movieHoldTime + m_elapsedMovieTime < Clock_Milliseconds() && 
						m_copyrightHoldTime + m_elapsedCopywriteTime < Clock_Milliseconds())
				{
					m_movieHoldTime = -1;
					m_elapsedMovieTime = 0;
					m_elapsedCopywriteTime = 0;
					m_copyrightHoldTime = -1;
				}
			}
			else
			{
				stopMovie();
			}
		}
	}
}

//============================================================================
// Display::reset
//============================================================================

void Display::reset()
{
	//Remove letterbox border that may have been enabled by a script
	m_letterBoxFadeLevel = 0;
	m_letterBoxEnabled = FALSE;
	stopMovie();
	freeDeferred();

	// Reset all views that need resetting
	for( View *v = m_viewList; v; v = v->getNextView() )
		v->reset();
}

//============================================================================
// Display::isMoviePlaying
//============================================================================

Bool Display::isMoviePlaying(void)
{
	return m_videoStream != NULL && m_videoBuffer != NULL;
}

//============================================================================
// Display::setDebugDisplayCallback
//============================================================================

void Display::setDebugDisplayCallback( DebugDisplayCallback *callback, void *userData )
{
	m_debugDisplayCallback = callback;
	m_debugDisplayUserData = userData;
}

//============================================================================
// Display::getDebugDisplayCallback
//============================================================================

Display::DebugDisplayCallback *Display::getDebugDisplayCallback()
{
	return m_debugDisplayCallback;
}
