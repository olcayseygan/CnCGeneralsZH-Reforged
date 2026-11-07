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
// Modified 2026 by İlyas Akın for the macOS/Linux port: parts come from GeneralsMD/Code/GameEngineDevice/Source/Win32Device/GameClient/Win32Mouse.cpp, the rest is new code, copyright 2026 İlyas Akın; see NOTICE.md and the git history.

// SdlMouse.cpp: see SdlMouse.h.  Each function is Win32Mouse's of the same name, on SDL.

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "Common/file.h"
#include "Common/FileSystem.h"
#include "GameClient/Display.h"
#include "GameClient/GameClient.h"
#include "SdlDevice/GameClient/AniCursor.h"
#include "SdlDevice/GameClient/SdlInput.h"
#include "SdlDevice/GameClient/SdlMouse.h"

#include <SDL3/SDL.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include <vector>

SdlMouse *SdlMouse::s_active = NULL;

namespace {

SDL_Cursor *theCursors[ Mouse::NUM_MOUSE_CURSORS ][ MAX_2D_CURSOR_DIRECTIONS ];

SDL_Surface *surfaceFor( const AniCursorFrame &frame )
{
	SDL_Surface *surface = SDL_CreateSurface( frame.width, frame.height, SDL_PIXELFORMAT_RGBA32 );
	if (surface == NULL)
		return NULL;
	for (Int y = 0; y < frame.height; ++y)
		memcpy( (Uint8 *)surface->pixels + y * surface->pitch, &frame.rgba[ (size_t)y * frame.width * 4 ], (size_t)frame.width * 4 );
	return surface;
}

/// A file through the engine's file system, which finds Data\Cursors\X.ANI whatever its case on disk
Bool readWholeFile( const char *path, std::vector<UnsignedByte> &bytes )
{
	if (TheFileSystem == NULL)
		return FALSE;
	File *file = TheFileSystem->openFile( path, File::READ | File::BINARY );
	if (file == NULL)
		return FALSE;
	const Int size = file->size();
	bytes.resize( size > 0 ? size : 0 );
	const Bool ok = size > 0 && file->read( &bytes[0], size ) == size;
	file->close();
	return ok;
}

/* ZH_POINTER_CHECK=<seconds>: a check that clicks land where the pointer is, in the real window, with no
	 human and no accessibility permission.  That many seconds after the mouse is made, the pointer is warped
	 to five places in the window and a right click is pushed at each (right: the menus take no action on it).
	 stderr gets, for each, the place in window points and in the drawable's pixels, and the game pixel under
	 it worked out from the drawable (pixel * game size / drawable size), independently of
	 SdlInput_toGamePixels' points; then every move and click the mouse receives, in game pixels. */
Real thePointerCheckAt = -1.0f;		///< seconds, or negative when off or done
Int thePointerCheckEvents = 0;		///< events still to print

void runPointerCheck( SDL_Window *window )
{
	Int pointsW = 0, pointsH = 0, pixelsW = 0, pixelsH = 0;
	SDL_GetWindowSize( window, &pointsW, &pointsH );
	SDL_GetWindowSizeInPixels( window, &pixelsW, &pixelsH );
	const Int gameW = TheDisplay != NULL ? (Int)TheDisplay->getWidth() : 0;
	const Int gameH = TheDisplay != NULL ? (Int)TheDisplay->getHeight() : 0;
	fprintf( stderr, "pointer check: window %dx%d points, %dx%d pixels; the game %dx%d\n", pointsW, pointsH, pixelsW,
		pixelsH, gameW, gameH );
	if (pointsW <= 0 || pointsH <= 0 || pixelsW <= 0 || pixelsH <= 0 || gameW <= 0 || gameH <= 0)
		return;
	static const Real places[5][2] = { { 0.1f, 0.1f }, { 0.9f, 0.1f }, { 0.5f, 0.5f }, { 0.1f, 0.9f }, { 0.9f, 0.9f } };
	for (Int i = 0; i < 5; ++i)
	{
		const Real x = floorf( places[i][0] * pointsW ) + 0.5f, y = floorf( places[i][1] * pointsH ) + 0.5f;
		const Real px = x * pixelsW / pointsW, py = y * pixelsH / pointsH;
		fprintf( stderr, "pointer check: place %d at %.1f,%.1f points = %.1f,%.1f pixels; expected game %d,%d\n", i, x, y,
			px, py, (Int)floorf( px * gameW / pixelsW ), (Int)floorf( py * gameH / pixelsH ) );
		SDL_WarpMouseInWindow( window, x, y );
		for (Int down = 1; down >= 0; --down)
		{
			SDL_Event event;
			SDL_zero( event );
			event.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
			event.button.windowID = SDL_GetWindowID( window );
			event.button.button = SDL_BUTTON_RIGHT;
			event.button.down = down != 0;
			event.button.clicks = 1;
			event.button.x = x;
			event.button.y = y;
			event.button.timestamp = SDL_GetTicksNS();
			SDL_PushEvent( &event );
		}
	}
	thePointerCheckEvents = 40;
}

}  // namespace

SdlMouse::SdlMouse( void )
{
	memset( &m_eventBuffer, 0, sizeof( m_eventBuffer ) );
	m_nextFreeIndex = 0;
	m_nextGetIndex = 0;
	m_currentSdlCursor = NONE;
	m_directionFrame = 0;		// points up
	m_lostFocus = FALSE;
	m_cursorInWindow = TRUE;
	m_positionReported = FALSE;
	m_cursorConfined = FALSE;
	s_active = this;
	SdlInput_install();
	const char *check = getenv( "ZH_POINTER_CHECK" );
	if (check != NULL && atof( check ) > 0.0)
		thePointerCheckAt = (Real)(SDL_GetTicks() / 1000.0 + atof( check ));
}

void SdlMouse_releaseCursors( void )
{
	for (Int cursor = 0; cursor < Mouse::NUM_MOUSE_CURSORS; ++cursor)
		for (Int direction = 0; direction < MAX_2D_CURSOR_DIRECTIONS; ++direction)
			if (theCursors[cursor][direction] != NULL)
			{
				SDL_DestroyCursor( theCursors[cursor][direction] );
				theCursors[cursor][direction] = NULL;
			}
}

SdlMouse::~SdlMouse( void )
{
	if (m_cursorConfined && SdlInput_gameWindow() != NULL)
		SDL_SetWindowMouseGrab( SdlInput_gameWindow(), false );
	if (s_active == this)
		s_active = NULL;
}

void SdlMouse::init( void )
{
	Mouse::init();
	m_inputMovesAbsolute = TRUE;	// events carry positions, not deltas, as window messages do
}

void SdlMouse::reset( void )
{
	Mouse::reset();
}

Bool SdlMouse::wantsConfinement( Bool coversScreen, Bool focused, Bool cursorInWindow )
{
	return coversScreen && focused && cursorInWindow;
}

void SdlMouse::update( void )
{
	SDL_Window *window = SdlInput_gameWindow();
	if (window != NULL)
	{
		if (thePointerCheckAt >= 0.0f && SDL_GetTicks() / 1000.0 >= thePointerCheckAt)
		{
			thePointerCheckAt = -1.0f;
			runPointerCheck( window );
		}
		/* WndProc's WM_ACTIVATEAPP: without the focus the pointer is left alone, and with it back the
			 game's cursor is put back.  SdlGameEngine's pump keeps the focus events, so ask SDL each frame. */
		const Bool hasFocus = (SDL_GetWindowFlags( window ) & SDL_WINDOW_INPUT_FOCUS) != 0;
		if (hasFocus == m_lostFocus)
		{
			lostFocus( !hasFocus );
			if (hasFocus)
				setCursor( getMouseCursor() );
		}

		// Win32Mouse::update: ask where the pointer is rather than trust the last event, until one comes
		m_cursorInWindow = SDL_GetMouseFocus() == window;
		if (!m_positionReported && m_cursorInWindow)
		{
			float wx = 0, wy = 0;
			SDL_GetMouseState( &wx, &wy );
			Int x, y;
			SdlInput_toGamePixels( wx, wy, x, y );
			setPosition( x, y );
		}

		// ...and hold the pointer in a window that covers its screen, which edge scrolling needs
		const SDL_WindowFlags flags = SDL_GetWindowFlags( window );
		Bool coversScreen = (flags & SDL_WINDOW_FULLSCREEN) != 0;
		if (!coversScreen && (flags & SDL_WINDOW_BORDERLESS) != 0)
		{
			SDL_Rect bounds;
			Int width = 0, height = 0;
			SDL_GetWindowSize( window, &width, &height );
			coversScreen = SDL_GetDisplayBounds( SDL_GetDisplayForWindow( window ), &bounds )
				&& width >= bounds.w && height >= bounds.h;
		}
		const Bool focused = (flags & SDL_WINDOW_INPUT_FOCUS) != 0;
		if (wantsConfinement( coversScreen, focused, m_cursorInWindow ))
		{
			if (!m_cursorConfined)
				m_cursorConfined = SDL_SetWindowMouseGrab( window, true );
		}
		else if (m_cursorConfined && !(coversScreen && focused))
		{
			SDL_SetWindowMouseGrab( window, false );
			m_cursorConfined = FALSE;
		}
	}
	else if (SDL_GetHintBoolean( "ZH_OFFSCREEN_FRAMES", false ))
	{
		/* -offscreen (SdlGameEngine::startOffscreen): no window, so as a hidden one is - never focused, the
			 pointer never in it.  Left as it starts, the pointer is in the window at (0,0), and edge scrolling
			 carries the view off to the map's corner. */
		if (!m_lostFocus)
			lostFocus( TRUE );
		m_cursorInWindow = FALSE;
	}
	Mouse::update();
}

void SdlMouse::addEvent( EventKind kind, Int x, Int y, Button button, Int clicks, Int wheelDelta, UnsignedInt timeMs )
{
	// a full ring drops the event, as Win32Mouse's does
	if (kind == EVENT_NONE || m_eventBuffer[ m_nextFreeIndex ].kind != EVENT_NONE)
		return;
	m_positionReported = TRUE;
	if (thePointerCheckEvents > 0)
	{
		--thePointerCheckEvents;
		static const char *const kinds[] = { "none", "move", "down", "up", "wheel" };
		fprintf( stderr, "pointer check: the mouse got %s (button %d) at game %d,%d\n", kinds[kind], (Int)button, x, y );
	}
	SdlMouseEvent &slot = m_eventBuffer[ m_nextFreeIndex ];
	slot.kind = kind;
	slot.x = x;
	slot.y = y;
	slot.button = button;
	slot.clicks = clicks;
	slot.wheelDelta = wheelDelta;
	slot.time = timeMs;
	m_nextFreeIndex++;
	if (m_nextFreeIndex >= Mouse::NUM_MOUSE_EVENTS)
		m_nextFreeIndex = 0;
}

UnsignedByte SdlMouse::getMouseEvent( MouseIO *result, Bool flush )
{
	if (m_eventBuffer[ m_nextGetIndex ].kind == EVENT_NONE)
		return MOUSE_NONE;
	translateEvent( m_nextGetIndex, result );
	m_eventBuffer[ m_nextGetIndex ].kind = EVENT_NONE;
	m_nextGetIndex++;
	if (m_nextGetIndex >= Mouse::NUM_MOUSE_EVENTS)
		m_nextGetIndex = 0;
	return MOUSE_OK;
}

void SdlMouse::translateEvent( UnsignedInt eventIndex, MouseIO *result )
{
	const SdlMouseEvent &event = m_eventBuffer[ eventIndex ];
	const UnsignedInt frame = TheGameClient != NULL ? TheGameClient->getFrame() : 1;

	result->leftState = result->middleState = result->rightState = MBS_Up;
	result->leftFrame = result->middleFrame = result->rightFrame = 0;
	result->pos.x = result->pos.y = result->wheelPos = result->sideButton = 0;
	result->time = event.time;
	result->pos.x = event.x;
	result->pos.y = event.y;

	switch (event.kind)
	{
		case EVENT_BUTTON_DOWN:
		case EVENT_BUTTON_UP:
		{
			/* Windows: down, up, double click, up, down, up...  SDL counts the clicks, by the system's
				 own interval and distance: the second of each pair is Windows' double click. */
			MouseButtonState state = MBS_Up;
			if (event.kind == EVENT_BUTTON_DOWN)
				state = (event.clicks >= 2 && event.clicks % 2 == 0) ? MBS_DoubleClick : MBS_Down;
			switch (event.button)
			{
				case BUTTON_LEFT:		result->leftState = state; result->leftFrame = frame; break;
				case BUTTON_MIDDLE:	result->middleState = state; result->middleFrame = frame; break;
				case BUTTON_RIGHT:	result->rightState = state; result->rightFrame = frame; break;
				// the side buttons are a step on the press alone, as Win32's WM_XBUTTONDOWN
				case BUTTON_X1:			result->sideButton = event.kind == EVENT_BUTTON_DOWN ? -1 : 0; break;
				case BUTTON_X2:			result->sideButton = event.kind == EVENT_BUTTON_DOWN ? 1 : 0; break;
			}
			break;
		}
		case EVENT_WHEEL:
			result->wheelPos = event.wheelDelta;
			break;
		default:
			break;		// a move carries only its position
	}
}

void SdlMouse::setVisibility( Bool visible )
{
	Mouse::setVisibility( visible );
	SdlMouse::setCursor( getMouseCursor() );
}

void SdlMouse::initCursorResources( void )
{
	/* Windows' LoadCursorFromFile needs no display, so a -headless run there loads the cursors too.  SDL's
		 cursors need SDL's video, which -headless never starts: every one would fail, each a
		 DEBUG_ASSERTCRASH in a debug build.  A headless run has no pointer to dress (C3b). */
	if (!SDL_WasInit( SDL_INIT_VIDEO ))
		return;
	// Win32's SM_CXCURSOR: 32 at a content scale of 1 (macOS's Retina included, its scale lives in the
	// pixel density), more where the desktop is scaled up.  The Reforged files carry a 48 and a 32.
	const float scale = SDL_GetDisplayContentScale( SDL_GetPrimaryDisplay() );
	const Int wanted = (Int)( 32.0f * (scale > 0.0f ? scale : 1.0f) + 0.5f );
	for (Int cursor = FIRST_CURSOR; cursor < NUM_MOUSE_CURSORS; cursor++)
	{
		for (Int direction = 0; direction < m_cursorInfo[cursor].numDirections; direction++)
		{
			if (theCursors[cursor][direction] != NULL || m_cursorInfo[cursor].textureName.isEmpty())
				continue;
			char path[256];
			if (m_cursorInfo[cursor].numDirections > 1)
				snprintf( path, ARRAY_SIZE( path ), "data\\cursors\\%s%d.ANI", m_cursorInfo[cursor].textureName.str(), direction );
			else
				snprintf( path, ARRAY_SIZE( path ), "data\\cursors\\%s.ANI", m_cursorInfo[cursor].textureName.str() );
			std::vector<UnsignedByte> bytes;
			AniCursor decoded;
			std::string why;
			if (readWholeFile( path, bytes ) && AniCursor_decode( &bytes[0], bytes.size(), decoded, &why, wanted ))
				theCursors[cursor][direction] = createCursor( decoded );
			DEBUG_ASSERTCRASH( theCursors[cursor][direction], ("MissingCursor %s %s\n", path, why.c_str()) );
		}
	}
}

SDL_Cursor *SdlMouse::createCursor( const AniCursor &cursor )
{
	if (cursor.frames.empty() || cursor.steps.empty())
		return NULL;
	const AniCursorFrame &first = cursor.frames[ cursor.steps[0].frame ];
	std::vector<SDL_Surface *> surfaces( cursor.frames.size(), (SDL_Surface *)NULL );
	for (size_t i = 0; i < cursor.frames.size(); ++i)
		surfaces[i] = surfaceFor( cursor.frames[i] );
	SDL_Cursor *made = NULL;
	if (cursor.steps.size() == 1)
		made = surfaces[ cursor.steps[0].frame ] != NULL
			? SDL_CreateColorCursor( surfaces[ cursor.steps[0].frame ], first.hotX, first.hotY ) : NULL;
	else
	{
		std::vector<SDL_CursorFrameInfo> steps( cursor.steps.size() );
		Bool complete = TRUE;
		for (size_t i = 0; i < cursor.steps.size(); ++i)
		{
			steps[i].surface = surfaces[ cursor.steps[i].frame ];
			steps[i].duration = cursor.steps[i].durationMs;
			complete = complete && steps[i].surface != NULL;
		}
		if (complete)
			made = SDL_CreateAnimatedCursor( &steps[0], (int)steps.size(), first.hotX, first.hotY );
	}
	for (size_t i = 0; i < surfaces.size(); ++i)
		SDL_DestroySurface( surfaces[i] );
	return made;
}

void SdlMouse::setCursor( MouseCursor cursor )
{
	Mouse::setCursor( cursor );
	if (m_lostFocus)
		return;		// Win32Mouse's rule: leave the pointer alone without the focus
	if (cursor == NONE || !m_visible)
		SDL_HideCursor();
	else
	{
		SDL_Cursor *made = theCursors[cursor][m_directionFrame];
		if (made != NULL)
			SDL_SetCursor( made );
		SDL_ShowCursor();
	}
	m_currentSdlCursor = m_currentCursor = cursor;
}

// Win32Mouse's are empty too (their SetCapture is commented out); SDL captures during a button press
void SdlMouse::capture( void )
{
}

void SdlMouse::releaseCapture( void )
{
}
