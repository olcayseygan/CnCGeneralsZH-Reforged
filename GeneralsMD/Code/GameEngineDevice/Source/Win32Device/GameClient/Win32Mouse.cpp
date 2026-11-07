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

////////////////////////////////////////////////////////////////////////////////
//																																						//
//  (c) 2001-2003 Electronic Arts Inc.																				//
//																																						//
////////////////////////////////////////////////////////////////////////////////

// FILE: Win32Mouse.cpp ///////////////////////////////////////////////////////////////////////////
// Created:    Colin Day, July 2001
// Desc:       Interface for the mouse using only the Win32 messages
///////////////////////////////////////////////////////////////////////////////////////////////////

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "Common/Debug.h"
#include "GameClient/Display.h"
#include "GameClient/GameClient.h"
#include "Win32Device/GameClient/Win32Mouse.h"
#include "WinMain.h"

#ifdef _INTERNAL
// for occasional debugging...
//#pragma optimize("", off)
//#pragma MESSAGE("************************************** WARNING, optimization disabled for debugging purposes")
#endif

// EXTERN /////////////////////////////////////////////////////////////////////////////////////////
extern Win32Mouse *TheWin32Mouse;
extern Bool ApplicationIsBorderless;

HCURSOR cursorResources[Mouse::NUM_MOUSE_CURSORS][MAX_2D_CURSOR_DIRECTIONS];
///////////////////////////////////////////////////////////////////////////////////////////////////
// PRIVATE FUNCTIONS //////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////

//-------------------------------------------------------------------------------------------------
/** Get a mouse event from the buffer if available, we need to translate
	* from the windows message meanings to our own internal mouse 
	* structure */
//-------------------------------------------------------------------------------------------------
UnsignedByte Win32Mouse::getMouseEvent( MouseIO *result, Bool flush )
{

	// if there is nothing here there is no event data to do
	if( m_eventBuffer[ m_nextGetIndex ].msg == 0 )
		return MOUSE_NONE;

	// translate the win32 mouse message to our own system
	translateEvent( m_nextGetIndex, result );

	// remove this event from the buffer by setting msg to zero
	m_eventBuffer[ m_nextGetIndex ].msg = 0;

	//
	// our next get index will now be advanced to the next index, wrapping at
	// the mad
	//
	m_nextGetIndex++;
	if( m_nextGetIndex >= Mouse::NUM_MOUSE_EVENTS )
		m_nextGetIndex = 0;

	// got event OK and all done with this one
	return MOUSE_OK;

}  // end getMouseEvent

//-------------------------------------------------------------------------------------------------
/** Windows reports the pointer in the window's pixels and the game works in its own.  They are the
	* same thing until a fullscreen picture smaller than the monitor is scaled up onto it, when the
	* window is the monitor's size (or the largest part of it of the picture's shape) and the picture
	* is not.  Scale by the two sizes, so every mode that does not scale comes out unchanged. */
//-------------------------------------------------------------------------------------------------
static void windowToGame( Int &x, Int &y )
{
	RECT client;
	if( TheDisplay == NULL || !::GetClientRect( ApplicationHWnd, &client ) || client.right <= 0 || client.bottom <= 0 )
		return;
	const Int width = TheDisplay->getWidth();
	const Int height = TheDisplay->getHeight();
	if( client.right != width )
		x = (Int)( ( (Int64)x * width ) / client.right );
	if( client.bottom != height )
		y = (Int)( ( (Int64)y * height ) / client.bottom );
}

static void gameToWindow( Int &x, Int &y )
{
	RECT client;
	if( TheDisplay == NULL || !::GetClientRect( ApplicationHWnd, &client ) )
		return;
	const Int width = TheDisplay->getWidth();
	const Int height = TheDisplay->getHeight();
	// rounded up, so windowToGame brings the point back to where it started while the window is the
	// larger of the two; the freecam re-centres the pointer every frame and reads the difference
	if( width > 0 && client.right != width )
		x = (Int)( ( (Int64)x * client.right + width - 1 ) / width );
	if( height > 0 && client.bottom != height )
		y = (Int)( ( (Int64)y * client.bottom + height - 1 ) / height );
}

//-------------------------------------------------------------------------------------------------
/** Translate a win32 mouse event to our own event info */
//-------------------------------------------------------------------------------------------------
void Win32Mouse::translateEvent( UnsignedInt eventIndex, MouseIO *result )
{
	UINT msg = m_eventBuffer[ eventIndex ].msg;
	WPARAM wParam = m_eventBuffer[ eventIndex ].wParam;
	LPARAM lParam = m_eventBuffer[ eventIndex ].lParam;
	UnsignedInt frame;

	//
	// get the current input frame from the client, if we don't have
	// a client (like in the GUI editor) we just use frame 1 so it
	// registers with the system
	//
	if( TheGameClient )
		frame = TheGameClient->getFrame();
	else
		frame = 1;

	// set these to defaults
	result->leftState = result->middleState = result->rightState = MBS_Up;
	result->leftFrame = result->middleFrame = result->rightFrame = 0;
	result->pos.x = result->pos.y = result->wheelPos = result->sideButton = 0;

	// Time is the same for all events
	result->time = m_eventBuffer[ eventIndex ].time;
	
	switch( msg )
	{

		// ------------------------------------------------------------------------
		case WM_LBUTTONDOWN:
		{
			
			result->leftState = MBS_Down;
			result->leftFrame = frame;
			result->pos.x = LOWORD( lParam );
			result->pos.y = HIWORD( lParam );
			break;

		}  // end left button down

		// ------------------------------------------------------------------------
		case WM_LBUTTONUP:
		{

			result->leftState = MBS_Up;
			result->leftFrame = frame;
			result->pos.x = LOWORD( lParam );
			result->pos.y = HIWORD( lParam );
			break;

		}  // end left button up

		// ------------------------------------------------------------------------
		case WM_LBUTTONDBLCLK:
		{

			result->leftState = MBS_DoubleClick;
			result->leftFrame = frame;
			result->pos.x = LOWORD( lParam );
			result->pos.y = HIWORD( lParam );
			break;

		}  // end left button double click

		// ------------------------------------------------------------------------
		case WM_MBUTTONDOWN:
		{

			result->middleState = MBS_Down;
			result->middleFrame = frame;
			result->pos.x = LOWORD( lParam );
			result->pos.y = HIWORD( lParam );
			break;

		}  // end middle button down

		// ------------------------------------------------------------------------
		case WM_MBUTTONUP:
		{

			result->middleState = MBS_Up;
			result->middleFrame = frame;
			result->pos.x = LOWORD( lParam );
			result->pos.y = HIWORD( lParam );
			break;

		}  // end middle button up

		// ------------------------------------------------------------------------
		case WM_MBUTTONDBLCLK:
		{

			result->middleState = MBS_DoubleClick;
			result->middleFrame = frame;
			result->pos.x = LOWORD( lParam );
			result->pos.y = HIWORD( lParam );
			break;

		}  // end middle button double click

		// ------------------------------------------------------------------------
		case WM_RBUTTONDOWN:
		{

			result->rightState = MBS_Down;
			result->rightFrame = frame;
			result->pos.x = LOWORD( lParam );
			result->pos.y = HIWORD( lParam );
			break;

		}  // end right button down

		// ------------------------------------------------------------------------
		case WM_RBUTTONUP:
		{

			result->rightState = MBS_Up;
			result->rightFrame = frame;
			result->pos.x = LOWORD( lParam );
			result->pos.y = HIWORD( lParam );
			break;

		}  // end right button up

		// ------------------------------------------------------------------------
		case WM_RBUTTONDBLCLK:
		{

			result->rightState = MBS_DoubleClick;
			result->rightFrame = frame;
			result->pos.x = LOWORD( lParam );
			result->pos.y = HIWORD( lParam );
			break;

		}  // end right button double click

		// ------------------------------------------------------------------------
		case WM_MOUSEMOVE:
		{

			result->pos.x = LOWORD( lParam );
			result->pos.y = HIWORD( lParam );
			break;

		}  // end mouse move

		// ------------------------------------------------------------------------
		case 0x020A:	// WM_MOUSEWHEEL
		{
			POINT p;

			// translate the screen mouse position to be relative to the application window
			p.x = LOWORD( lParam );
			p.y = HIWORD( lParam );
			ScreenToClient( ApplicationHWnd, &p );
	
			// note the short cast here to keep signed information in tact
			result->wheelPos = (Short)HIWORD( wParam );
			result->pos.x = p.x;
			result->pos.y = p.y;
			break;

		}  // end mouse wheel

		// ------------------------------------------------------------------------
		case WM_XBUTTONDOWN:
		{

			result->sideButton = GET_XBUTTON_WPARAM( wParam ) == XBUTTON2 ? 1 : -1;
			result->pos.x = LOWORD( lParam );
			result->pos.y = HIWORD( lParam );
			break;

		}  // end side button down

		// ------------------------------------------------------------------------
		default:
		{

			DEBUG_CRASH(( "translateEvent: Unknown Win32 mouse event [%d,%d,%d]\n",
							 msg, wParam, lParam ));
			return;

		}  // end default

	}  // end switch on message at event index in buffer

	windowToGame( result->pos.x, result->pos.y );

}  // end translateEvent

///////////////////////////////////////////////////////////////////////////////////////////////////
// PUBLIC FUNCTIONS ///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
Win32Mouse::Win32Mouse( void )
{

	// zero our event list
	memset( &m_eventBuffer, 0, sizeof( m_eventBuffer ) );

	m_nextFreeIndex = 0;
	m_nextGetIndex = 0;
	m_currentWin32Cursor = NONE;
	for (Int i=0; i<NUM_MOUSE_CURSORS; i++)
		for (Int j=0; j<MAX_2D_CURSOR_DIRECTIONS; j++)
			cursorResources[i][j]=NULL;
	m_directionFrame=0; //points up.
	m_lostFocus = FALSE;
	m_cursorInWindow = TRUE;
	m_positionReported = FALSE;
	m_cursorClipped = FALSE;
}  // end Win32Mouse

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
Win32Mouse::~Win32Mouse( void )
{

	// remove our global reference that was for the WndProc() only
	TheWin32Mouse = NULL;

}  // end ~Win32Mouse

//-------------------------------------------------------------------------------------------------
/** Initialize our device */
//-------------------------------------------------------------------------------------------------
void Win32Mouse::init( void )
{

	// extending functionality
	Mouse::init();

	//
	// when we receive messages from a Windows message procedure, the mouse
	// moves report the current cursor position and not deltas, our mouse
	// needs to process those positions as absolute and not relative
	//
	m_inputMovesAbsolute = TRUE;

}  // end int

//-------------------------------------------------------------------------------------------------
/** Reset */
//-------------------------------------------------------------------------------------------------
void Win32Mouse::reset( void )
{

	// extend
	Mouse::reset();

}  // end reset

//-------------------------------------------------------------------------------------------------
/** Update, called once per frame */
//-------------------------------------------------------------------------------------------------
void Win32Mouse::update( void )
{

	//
	// Windows reports the pointer to us only while it is over our client area, and only when it
	// moves.  Two silences come out of that and both of them scroll the camera by themselves.  A
	// pointer that walks out of a windowed game leaves m_currMouse.pos frozen on the edge it left
	// by, and the screen-edge scroll reads that position every frame afterwards.  A game that has
	// just started has been parked at (0,0) by GameClient::init and nobody has moved the mouse yet,
	// so the pointer is believed to be in the top left corner, which is also an edge.  Ask Windows
	// where the thing actually is instead of trusting the last thing it said.
	//
	POINT screenPos;
	POINT clientPos;
	RECT client;

	::GetCursorPos( &screenPos );
	clientPos = screenPos;
	::ScreenToClient( ApplicationHWnd, &clientPos );
	::GetClientRect( ApplicationHWnd, &client );

	// WindowFromPoint, not the rectangle alone: another application's window sitting over ours owns
	// the pointer that is inside our rectangle, and the map should not scroll under it.
	m_cursorInWindow = ::PtInRect( &client, clientPos ) && ::WindowFromPoint( screenPos ) == ApplicationHWnd;

	// until the first window message tells us where the pointer is, Windows is the only one who
	// knows, and the (0,0) we were given at init is a corner nobody put the mouse in.
	if( m_positionReported == FALSE && m_cursorInWindow )
	{
		Int x = clientPos.x, y = clientPos.y;
		windowToGame( x, y );
		setPosition( x, y );
	}

	//
	// A window that covers a screen, borderless or fullscreen, keeps the pointer while it has the
	// focus.  Screen-edge scrolling reads the edge of that screen, and with a second monitor beside it
	// the pointer walked straight over the edge onto the other one.  WndProc has asked the mouse for
	// this clip since 2003, but only the DirectInput mouse ever set one, and this is the mouse the
	// game makes.  The pointer is taken once it is over the game, so activating from the taskbar
	// does not snatch it across the desktop; the clip is checked every frame, so it follows the
	// window through a mode change and comes back if something else released it.  A plain window has
	// no edge scrolling and leaves the pointer free.
	//
	const Bool coversScreen = TheDisplay && ( !TheDisplay->getWindowed() || ApplicationIsBorderless );
	if( coversScreen && ::GetForegroundWindow() == ApplicationHWnd )
	{
		RECT window;
		RECT clip;
		::GetWindowRect( ApplicationHWnd, &window );
		::GetClipCursor( &clip );
		if( m_cursorInWindow && !::EqualRect( &window, &clip ) )
		{
			::ClipCursor( &window );
			m_cursorClipped = TRUE;
		}
	}
	else if( m_cursorClipped )
	{
		::ClipCursor( NULL );
		m_cursorClipped = FALSE;
	}

	// extend
	Mouse::update();

}  // end update

//-------------------------------------------------------------------------------------------------
/** Add a window message event along with its WPARAM and LPARAM parameters
	* to our input storage buffer */
//-------------------------------------------------------------------------------------------------
void Win32Mouse::addWin32Event( UINT msg, WPARAM wParam, LPARAM lParam, DWORD time )
{

	//
	// we can only add this event if our next free index does not already
	// have an event in it, if it does ... our buffer is full and this input
	// event will be lost
	//
	if( m_eventBuffer[ m_nextFreeIndex ].msg != 0 )
		return;

	// every one of these carries a position, so from here on the event stream owns where the
	// pointer is and update() stops seeding it from the OS
	m_positionReported = TRUE;

	// add to this index
	m_eventBuffer[ m_nextFreeIndex ].msg = msg;
	m_eventBuffer[ m_nextFreeIndex ].wParam = wParam;
	m_eventBuffer[ m_nextFreeIndex ].lParam = lParam;
	m_eventBuffer[ m_nextFreeIndex ].time = time;

	// wrap index at max
	m_nextFreeIndex++;
	if( m_nextFreeIndex >= Mouse::NUM_MOUSE_EVENTS )
		m_nextFreeIndex = 0;

}  // end addWin32Event

extern HINSTANCE ApplicationHInstance;

void Win32Mouse::setVisibility(Bool visible)
{
	//Extend
	Mouse::setVisibility(visible);
	//Maybe need to set cursor to force hiding of some cursors.
	Win32Mouse::setCursor(getMouseCursor());
}

/** The freecam's mouse look puts the pointer back in the middle of the window every frame.  Never
		while another window is in front: the pointer is that window's then.  Nor where the middle is off
		the monitor, as it is for a window bigger than the monitor: SetCursorPos stops the pointer at the
		edge and still says yes, so every frame read a turn from the middle to the edge, and a 3840x2160
		-freecam take on a 1080 monitor spun round looking straight up into black. */
Bool Win32Mouse::warpCursor( Int x, Int y )
{
	if( ::GetForegroundWindow() != ApplicationHWnd )
		return FALSE;
	Int windowX = x, windowY = y;
	gameToWindow( windowX, windowY );
	POINT screen = { windowX, windowY };
	if( !::ClientToScreen( ApplicationHWnd, &screen ) || !::SetCursorPos( screen.x, screen.y ) )
		return FALSE;
	POINT landed;
	if( !::GetCursorPos( &landed ) || landed.x != screen.x || landed.y != screen.y )
		return FALSE;
	setPosition( x, y );
	return TRUE;
}

/**Preload all the cursors we may need during the game.  This must be done before the D3D device
is created to avoid cursor corruption on buggy ATI Radeon cards. */
void Win32Mouse::initCursorResources(void)
{
	for (Int cursor=FIRST_CURSOR; cursor<NUM_MOUSE_CURSORS; cursor++)
	{
		for (Int direction=0; direction<m_cursorInfo[cursor].numDirections; direction++)
		{	if (!cursorResources[cursor][direction] && !m_cursorInfo[cursor].textureName.isEmpty())
			{	//this cursor has never been loaded before.
				char resourcePath[256];
				//Check if this is a directional cursor
				if (m_cursorInfo[cursor].numDirections > 1)
					snprintf(resourcePath, ARRAY_SIZE(resourcePath), "data\\cursors\\%s%d.ANI",m_cursorInfo[cursor].textureName.str(),direction);
				else
					snprintf(resourcePath, ARRAY_SIZE(resourcePath), "data\\cursors\\%s.ANI",m_cursorInfo[cursor].textureName.str());

				// LR_DEFAULTSIZE takes the entry nearest SM_CXCURSOR, the system's cursor size: 32 at 100 %
				// DPI, more only where Windows asks for it.  The Reforged files hold a 48 and a 32, each with
				// its own hotspot; without the flag LoadImage kept the first, 48, and every cursor drew half
				// again too big.  EA's are 32 only and load as before.
				cursorResources[cursor][direction]=(HCURSOR)LoadImageA(NULL, resourcePath, IMAGE_CURSOR, 0, 0, LR_LOADFROMFILE | LR_DEFAULTSIZE);
				DEBUG_ASSERTCRASH(cursorResources[cursor][direction], ("MissingCursor %s\n",resourcePath));
			}
		}
//		SetCursor(cursorResources[cursor][m_directionFrame]);
	}
}

//-------------------------------------------------------------------------------------------------
/** Super basic simplistic cursor */
//-------------------------------------------------------------------------------------------------
void Win32Mouse::setCursor( MouseCursor cursor )
{
	// extend
	Mouse::setCursor( cursor );

	if (m_lostFocus)
		return;	//stop messing with mouse cursor if we don't have focus.

	if (cursor == NONE || !m_visible)
		SetCursor( NULL );
	else
	{
		SetCursor(cursorResources[cursor][m_directionFrame]);
	}  // end switch

	// save current cursor
	m_currentWin32Cursor=m_currentCursor = cursor;
	
}  // end setCursor

//-------------------------------------------------------------------------------------------------
/** Capture the mouse to our application */
//-------------------------------------------------------------------------------------------------
void Win32Mouse::capture( void )
{

//	SetCapture( ApplicationHWnd );

}  // end capture

//-------------------------------------------------------------------------------------------------
/** Release the mouse capture for our app window */
//-------------------------------------------------------------------------------------------------
void Win32Mouse::releaseCapture( void )
{

//	ReleaseCapture();

}  // end releaseCapture








