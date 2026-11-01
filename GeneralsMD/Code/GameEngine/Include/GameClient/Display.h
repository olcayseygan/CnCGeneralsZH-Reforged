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

// FILE: Display.h ////////////////////////////////////////////////////////////
// The graphics display singleton
// Author: Michael S. Booth, March 2001

#pragma once

#ifndef _GAME_DISPLAY_H_
#define _GAME_DISPLAY_H_

#include <stdio.h>
#include <functional>
#include <vector>
#include "Common/SubsystemInterface.h"
#include "View.h"
#include "GameClient/Color.h"
#include "GameClient/GameFont.h"

class View;

struct ShroudLevel
{
	Short m_currentShroud;		///< A Value of 1 means shrouded.  0 is not.  Negative is the count of people looking.
	Short m_activeShroudLevel;///< A Value of 0 means passive shroud.  Positive is the count of people shrouding.
};

class VideoBuffer;
class VideoStreamInterface;
class DebugDisplayInterface;
class Radar;
class Image;
class DisplayString;
enum StaticGameLODLevel : Int;
/**
 * The Display class implements the Display interface
 */
class Display : public SubsystemInterface
{

public:
	enum DrawImageMode
	{
		DRAW_IMAGE_SOLID,
		DRAW_IMAGE_GRAYSCALE,		//draw image without blending and ignoring alpha
		DRAW_IMAGE_ALPHA,		//alpha blend the image into frame buffer
		DRAW_IMAGE_ADDITIVE	//additive blend the image into frame buffer
	};

	// No default argument here: a function *type* cannot carry one, and nothing
	// ever calls through this pointer with fewer than three arguments.
	typedef void (DebugDisplayCallback)( DebugDisplayInterface *debugDisplay, void *userData, FILE *fp );

	Display();
	virtual ~Display();

	virtual void init( void ) { };																///< Initialize
	virtual void reset( void );																		///< Reset system
	virtual void update( void );																	///< Update system

	//---------------------------------------------------------------------------------------
	// Display attribute methods
	virtual void setWidth( UnsignedInt width );										///< Sets the width of the display		
	virtual void setHeight( UnsignedInt height );									///< Sets the height of the display
	virtual UnsignedInt getWidth( void ) { return m_width; }			///< Returns the width of the display
	virtual UnsignedInt getHeight( void ) { return m_height; }		///< Returns the height of the display
	virtual void setBitDepth( UnsignedInt bitDepth ) { m_bitDepth = bitDepth; }
	virtual UnsignedInt getBitDepth( void ) { return m_bitDepth; }
	virtual void setWindowed( Bool windowed ) { m_windowed = windowed; }  ///< set windowd/fullscreen flag
	virtual Bool getWindowed( void ) { return m_windowed; }				///< return widowed/fullscreen flag
	virtual Bool setDisplayMode( UnsignedInt xres, UnsignedInt yres, UnsignedInt bitdepth, Bool windowed );	///<sets screen resolution/mode
 	virtual void setGamma(Real gamma, Real bright, Real contrast, Bool calibrate) {};
	virtual Bool testMinSpecRequirements(Bool *videoPassed, Bool *cpuPassed, Bool *memPassed,StaticGameLODLevel *idealVideoLevel=NULL, Real *cpuTime=NULL) {*videoPassed=*cpuPassed=*memPassed=true; return true;}
	virtual void doSmartAssetPurgeAndPreload(const char* usageFileName) = 0;
#if defined(_DEBUG) || defined(_INTERNAL)
	virtual void dumpAssetUsage(const char* mapname) = 0;
#endif

	//---------------------------------------------------------------------------------------
	// View management
	virtual void attachView( View *view );												///< Attach the given view to the world
	virtual View *getFirstView( void ) { return m_viewList; }				///< Return the first view of the world
	virtual View *getNextView( View *view )
	{
		if( view )
			return view->getNextView();
		return NULL;
	}

	virtual void drawViews( void );																///< Render all views of the world
	virtual void updateViews ( void );															///< Updates state of world views

	virtual VideoBuffer*	createVideoBuffer( void ) = 0;							///< Create a video buffer that can be used for this display

	//---------------------------------------------------------------------------------------
	// Drawing management
	virtual void setClipRegion( IRegion2D *region ) = 0;	///< Set clip rectangle for 2D draw operations.
	virtual	Bool isClippingEnabled( void ) = 0;
	virtual	void enableClipping( Bool onoff ) = 0;

	virtual void draw( void );																		///< Redraw the entire display
	virtual void setTimeOfDay( TimeOfDay tod ) = 0;								///< Set the time of day for this display
	virtual void createLightPulse( const Coord3D *pos, const RGBColor *color, Real innerRadius,Real attenuationWidth, 
																 UnsignedInt increaseFrameTime, UnsignedInt decayFrameTime//, Bool donut = FALSE
																 ) = 0;
	virtual void stepDynamicLights( void ) { }	///< a pass that skips the draw: fade the light pulses and park the spent ones, as the draw does

	/// draw a line on the display in pixel coordinates with the specified color
	virtual void drawLine( Int startX, Int startY, Int endX, Int endY, 
												 Real lineWidth, UnsignedInt lineColor ) = 0;
	/// draw a line on the display in pixel coordinates with the specified 2 colors
	virtual void drawLine( Int startX, Int startY, Int endX, Int endY, 
												 Real lineWidth, UnsignedInt lineColor1, UnsignedInt lineColor2 ) = 0;
	/// draw a rect border on the display in pixel coordinates with the specified color
	virtual void drawOpenRect( Int startX, Int startY, Int width, Int height,
														 Real lineWidth, UnsignedInt lineColor ) = 0;
	/// draw a filled rect on the display in pixel coords with the specified color
	virtual void drawFillRect( Int startX, Int startY, Int width, Int height,
														 UnsignedInt color ) = 0;
	
	/// Draw a percentage of a rectange, much like a clock
	virtual void drawRectClock(Int startX, Int startY, Int width, Int height, Int percent, UnsignedInt color) = 0;
	virtual void drawRemainingRectClock(Int startX, Int startY, Int width, Int height, Int percent, UnsignedInt color) = 0;

	/// draw an image fit within the screen coordinates
	virtual void drawImage( const Image *image, Int startX, Int startY, 
													Int endX, Int endY, Color color = 0xFFFFFFFF, DrawImageMode mode=DRAW_IMAGE_ALPHA) = 0;

	/// draw a video buffer fit within the screen coordinates
	virtual void drawVideoBuffer( VideoBuffer *buffer, Int startX, Int startY,
													Int endX, Int endY ) = 0;

	/** Every 2D call above is its own draw call at the device: it sets the renderer up, hands it one
		* quad and draws it.  For a handful of pieces that is nothing; for a strip of cameos with a
		* tray, a scrim and a border each it is hundreds of draw calls a frame, which the driver
		* charges for one at a time.  Between beginBatch2D() and endBatch2D() a run of calls that
		* wants the same state - the same image, or none - is gathered and drawn once.  The order
		* everything is drawn in is untouched: a call that needs different state draws what is waiting
		* first.  Displays that do not batch simply draw as they always did. */
	virtual void beginBatch2D( void ) { }
	virtual void endBatch2D( void ) { }

	/** Draw whatever a batch is holding, now.  Anything that puts pixels on the screen without going
		* through the calls above - text, which draws through its own renderer - has to call this
		* first, or the batch it did not join lands on top of it when it is finally drawn. */
	virtual void flushBatch2D( void ) { }

	/** LOGIC-THREAD.md phase 3.  The overlays drawn over the world read logic as they draw: the health
		* bar asks the object its health, the superweapon clock asks the module its frame.  Those run at
		* the sync point instead, recording what they draw: between beginRecording and endRecording every
		* 2D call above and every DisplayString draw goes into the list with its arguments, and replay
		* draws the list where the overlay used to be drawn, inside the render, in the same order.  A
		* string freed while recording is freed by freeDeferred, at the next sync point, after the replay
		* that draws it or the pass that skipped the render. */
	typedef std::vector< std::function<void()> > DrawCalls;
	void beginRecording( DrawCalls *into ) { into->clear(); m_recording = into; }
	void endRecording( void ) { m_recording = NULL; }
	Bool isRecording( void ) const { return m_recording != NULL; }
	void record( std::function<void()> call ) { m_recording->push_back( std::move( call ) ); }
	/// kept for another replay: LOGIC-THREAD.md phase 4 draws the same pass's picture again; the next recording clears it
	static void replay( DrawCalls &calls ) { for( size_t i = 0; i < calls.size(); ++i ) calls[ i ](); }
	/// replay moved by (dx, dy) pixels, for a list recorded under a camera that has scrolled since
	virtual void replayShifted( DrawCalls &calls, Int dx, Int dy ) { replay( calls ); }
	/// the shift replayShifted is drawing with; a recorded string adds it to where it draws
	const ICoord2D &getReplayShift( void ) const { return m_replayShift; }
	void deferFree( DisplayString *string ) { m_deferredFrees.push_back( string ); }
	void freeDeferred( void );

	/// FullScreen video playback
	virtual void playLogoMovie( AsciiString movieName, Int minMovieLength, Int minCopyrightLength );
	virtual void playMovie( AsciiString movieName );
	virtual void stopMovie( void );
	virtual Bool isMoviePlaying(void);

	/// Register debug display callback
	virtual void setDebugDisplayCallback( DebugDisplayCallback *callback, void *userData = NULL  );
	virtual DebugDisplayCallback *getDebugDisplayCallback();

	virtual void setShroudLevel(Int x, Int y, CellShroudStatus setting ) = 0;	  ///< set shroud
	virtual void clearShroud() = 0;														///< empty the entire shroud
	virtual void setBorderShroudLevel(UnsignedByte level) = 0;	///<color that will appear in unused border terrain.

#if defined(_DEBUG) || defined(_INTERNAL)
	virtual void dumpModelAssets(const char *path) = 0;	///< dump all used models/textures to a file.
#endif
	virtual void preloadModelAssets( AsciiString model ) = 0;	///< preload model asset
	virtual void preloadTextureAssets( AsciiString texture ) = 0;	///< preload texture asset
	/// preload a texture drawImage draws: one level, never reduced, the way drawImage asks for it
	virtual void preloadImageTexture( AsciiString texture ) {}

	virtual void takeScreenShot(void) = 0;										///< saves screenshot to a file
	virtual void toggleMovieCapture(void) = 0;							///< starts saving frames to an avi or frame sequence
	virtual void toggleLetterBox(void) = 0;										///< enabled letter-boxed display
	virtual void enableLetterBox(Bool enable) = 0;						///< forces letter-boxed display on/off
	virtual Bool isLetterBoxFading( void ) { return FALSE; }	///< returns true while letterbox fades in/out
	virtual Bool isLetterBoxed( void ) { return FALSE; }	//WST 10/2/2002. Added query interface
	void setLetterBoxAspect( Real widthOverHeight ) { m_letterBoxAspect = widthOverHeight; }	///< the picture the bars leave; 16:9 is the game's, and draws nothing on a 16:9 screen
	virtual const WideChar *getRendererName( void ) const = 0;	///< the graphics API the picture is presented through, for the HUD corner

	virtual void setCinematicText( AsciiString string ) { m_cinematicText = string; }
	virtual void setCinematicFont( GameFont *font ) { m_cinematicFont = font; }
	virtual void setCinematicTextFrames( Int frames ) { m_cinematicTextFrames = frames; }

	virtual Real getAverageFPS( void ) = 0;	///< returns the average FPS.
	virtual Int getLastFrameDrawCalls( void ) = 0;  ///< returns the number of draw calls issued in the previous frame

protected:

	virtual void deleteViews( void );   ///< delete all views
	UnsignedInt m_width, m_height;			///< Dimensions of the display
	UnsignedInt m_bitDepth;							///< bit depth of the display
	Bool m_windowed;										///< TRUE when windowed, FALSE when fullscreen
	View *m_viewList;										///< All of the views into the world

	// Cinematic text data
	AsciiString m_cinematicText;        ///< string of the cinematic text that should be displayed
	GameFont *m_cinematicFont;           ///< font for cinematic text
	Int m_cinematicTextFrames;          ///< count of how long the cinematic text should be displayed

	// Video playback data
	VideoBuffer						*m_videoBuffer;						///< Video playback buffer
	VideoStreamInterface	*m_videoStream;						///< Video stream;
	AsciiString						 m_currentlyPlayingMovie;	///< The currently playing video. Used to notify TheScriptEngine of completed videos.

	// Debug display data
	DebugDisplayInterface *m_debugDisplay;					///< Actual debug display
	DebugDisplayCallback	*m_debugDisplayCallback;	///< Code to update the debug display
	void									*m_debugDisplayUserData;	///< Data for debug display update handler
	Real	m_letterBoxFadeLevel;	///<tracks the current alpha level for fading letter-boxed mode in/out.
	Bool	m_letterBoxEnabled;		///<current state of letterbox
	Real	m_letterBoxAspect;		///< width over height of the picture between the bars
	UnsignedInt	m_letterBoxFadeStartTime;		///< time of letterbox fade start
	Int		m_movieHoldTime;									///< time that we hold on the last frame of the movie
	Int		m_copyrightHoldTime;							///< time that the copyright must be on the screen
	UnsignedInt m_elapsedMovieTime;					///< used to make sure we show the stuff long enough
	UnsignedInt m_elapsedCopywriteTime;			///< Hold on the last frame until both have expired
	DisplayString *m_copyrightDisplayString;///< this'll hold the display string
	DrawCalls *m_recording = NULL;					///< see beginRecording
	ICoord2D m_replayShift = { 0, 0 };				///< see replayShifted
	std::vector< DisplayString * > m_deferredFrees;	///< see beginRecording
};

// the singleton
extern Display *TheDisplay;

//-------------------------------------------------------------------------------------------------
/** How much bigger this screen is than the 800x600 every window layout and every hand-placed HUD
	* pixel in the game was drawn for.  The .wnd rectangles are stretched by exactly this
	* (parseScreenRect), so anything drawn beside them in raw pixels - a production strip cameo, a
	* health bar - has to be multiplied by it too, or it keeps its 2003 size while the screen around
	* it triples.  Never below 1: nothing shrinks under 800 wide.
	*
	* The smaller of the two axes, which is what the layouts themselves are stretched by
	* (ControlBarUniformScaleFor, same number, spelt out again here because Display.h cannot see
	* ControlBar.h).  It used to be the width alone: on a 5120x1440 screen that says 6.4 where the
	* height says 2.4, and a health bar came out two and a half times the width of the tank it was
	* sitting over, because the tank's own size on screen follows the height. */
//-------------------------------------------------------------------------------------------------
inline Real UIScaleForScreen( Int screenWidth, Int screenHeight )
{
	if( screenWidth <= 0 || screenHeight <= 0 )
		return 1.0f;

	Real scale = screenWidth / 800.0f;
	const Real byHeight = screenHeight / 600.0f;
	if( byHeight < scale )
		scale = byHeight;

	return scale < 1.0f ? 1.0f : scale;
}

//-------------------------------------------------------------------------------------------------
/** The Classic interface's box: the 800x600 layouts at one scale, the smaller of the two, centred.
	* On a screen wider than 4:3 it is the full height with the world showing down both sides; at
	* 4:3 it is the screen.  This is the rectangle parseScreenRect fits a layout into, except on a
	* screen taller than 4:3 (1280x1024), where it stands on the bottom edge as the command bar does
	* and the fitted dialogs stay centred. */
//-------------------------------------------------------------------------------------------------
struct UIRect { Int x, y, w, h; };

inline UIRect UIRectForScreen( Int screenWidth, Int screenHeight )
{
	UIRect r = { 0, 0, screenWidth, screenHeight };
	if( screenWidth <= 0 || screenHeight <= 0 )
		return r;
	Real scale = screenWidth / 800.0f;
	if( screenHeight / 600.0f < scale )
		scale = screenHeight / 600.0f;
	r.w = (Int)( 800.0f * scale + 0.5f );
	r.h = (Int)( 600.0f * scale + 0.5f );
	r.x = ( screenWidth - r.w ) / 2;
	r.y = screenHeight - r.h;
	return r;
}

/// Where the 2D interface lives: the 4:3 box in the Classic interface, the whole screen in Reforged.
UIRect TheUIRect( void );

//-------------------------------------------------------------------------------------------------
inline Real TheUIScale( void )
{
	if( TheDisplay == NULL )
		return 1.0f;
	return UIScaleForScreen( TheDisplay->getWidth(), TheDisplay->getHeight() );
}

extern void StatDebugDisplay( DebugDisplayInterface *dd, void *, FILE *fp = NULL );

//Added By Saad
//Necessary for display resolution confirmation dialog box
//Holds the previous and current display settings

typedef struct _DisplaySettings
{
	Int xRes;  //Resolution width
	Int yRes;  //Resolution height
	Int bitDepth; //Color Depth
	Bool windowed; //Window mode TRUE: we're windowed, FALSE: we're not windowed
} DisplaySettings;


#endif // _GAME_DISPLAY_H_
