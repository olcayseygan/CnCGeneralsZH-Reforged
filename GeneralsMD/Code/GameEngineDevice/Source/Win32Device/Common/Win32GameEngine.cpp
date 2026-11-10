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

// FILE: W3DGameEngine.cpp ////////////////////////////////////////////////////////////////////////
// Author: Colin Day, April 2001
// Description:
//   Implementation of the Win32 game engine, this is the highest level of 
//   the game application, it creates all the devices we will use for the game
///////////////////////////////////////////////////////////////////////////////////////////////////

#include <windows.h>
#include "Win32Device/Common/Win32GameEngine.h"	// MSS/MSS.h through MilesAudioManager.h
#include "Common/PerfTimer.h"

#include "GameNetwork/LANAPICallbacks.h"

extern DWORD TheMessageTime;

class GameSpyInfoInterface;
extern GameSpyInfoInterface *TheGameSpyInfo;	// GameNetwork/GameSpy/PeerDefs.h, not NULL while logged in online

//-------------------------------------------------------------------------------------------------
/** Constructor for Win32GameEngine */
//-------------------------------------------------------------------------------------------------
Win32GameEngine::Win32GameEngine()
{
	// Stop blue screen
	m_previousErrorMode = SetErrorMode( SEM_FAILCRITICALERRORS );
}

//-------------------------------------------------------------------------------------------------
/** Destructor for Win32GameEngine */
//-------------------------------------------------------------------------------------------------
Win32GameEngine::~Win32GameEngine()
{
	// restore it (this isn't really necessary, but feels good.)
	SetErrorMode( m_previousErrorMode );
}


//-------------------------------------------------------------------------------------------------
/** Initialize the game engine */
//-------------------------------------------------------------------------------------------------
void Win32GameEngine::init( void )
{

	// extending functionality
	GameEngine::init();

}  // end init

//-------------------------------------------------------------------------------------------------
/** Reset the system */
//-------------------------------------------------------------------------------------------------
void Win32GameEngine::reset( void )
{

	// extending functionality
	GameEngine::reset();

}  // end reset

//-------------------------------------------------------------------------------------------------
/** Update the game engine by updating the GameClient and 
	* GameLogic singletons. */
//-------------------------------------------------------------------------------------------------
void Win32GameEngine::update( void )
{


	// call the engine normal update
	GameEngine::update();

	extern HWND ApplicationHWnd;
	if (ApplicationHWnd && ::IsIconic(ApplicationHWnd)) {
		while (ApplicationHWnd && ::IsIconic(ApplicationHWnd)) {
			// We are alt-tabbed out here.  Sleep a bit, & process windows
			// so that we can become un-alt-tabbed out.
			Sleep(5);
			serviceWindowsOS();

			if (TheLAN != NULL) {
				// BGC - need to update TheLAN so we can process and respond to other
				// people's messages who may not be alt-tabbed out like we are.
				TheLAN->setIsActive(isActive());
				TheLAN->update();
			}

			// If we are running a multiplayer game, keep running the logic.
			// There is code in the client to skip client redraw if we are
			// iconic.  jba.
			// The same goes for a lobby or staging room, online or LAN: their menus answer joins, chat
			// and slot changes from their own update, and only TheLAN above was kept alive.  A host
			// minimized in an online room left nobody able to join until the window came back, when
			// everything that had queued up played out at once.
			if (TheGameEngine->getQuitting() || TheGameLogic->isInInternetGame() || TheGameLogic->isInLanGame() ||
					TheLAN != NULL || TheGameSpyInfo != NULL) {
				break; // keep running.
			}
		}
    
    // When we are alt-tabbed out... the MilesAudioManager seems to go into a coma sometimes
    // and not regain focus properly when we come back. This seems to wake it up nicely.
    AudioAffect aa = (AudioAffect)0x10;
		TheAudio->setVolume(TheAudio->getVolume( aa ), aa );

	}

	// The regular pump is GameEngine::execute's, after the picture cap's wait and before the next pass.

}  // end update

//-------------------------------------------------------------------------------------------------
/** This function may be called from within this application to let
  * Microsoft Windows do its message processing and dispatching.  Presumeably
	* we would call this at least once each time around the game loop to keep
	* Windows services from backing up */
//-------------------------------------------------------------------------------------------------
void Win32GameEngine::serviceWindowsOS( void )
{
	MSG msg;
  Int returnValue;

	//
	// see if we have any messages to process, a NULL window handle tells the
	// OS to look at the main window associated with the calling thread, us!
	//
	while( PeekMessage( &msg, NULL, 0, 0, PM_NOREMOVE ) )
	{

		// get the message
		returnValue = GetMessage( &msg, NULL, 0, 0 );

		// this is one possible way to check for quitting conditions as a message
		// of WM_QUIT will cause GetMessage() to return 0
/*
		if( returnValue == 0 )
		{

			setQuitting( true );
			break;

		}
*/

		TheMessageTime = msg.time;
		// translate and dispatch the message
		TranslateMessage( &msg );
		DispatchMessage( &msg );
		TheMessageTime = 0;
			
	}  // end while

}  // end ServiceWindowsOS

//-------------------------------------------------------------------------------------------------
/** The audio backend's endpoints (miles_xaudio2.cpp), and the one it plays on */
//-------------------------------------------------------------------------------------------------
static const Int AUDIO_DEVICE_ID_BYTES = 256;
static const Int AUDIO_DEVICE_NAME_CHARS = 128;

Bool Win32GameEngine::getAudioOutputDevice( Int index, AsciiString &id, UnicodeString &name )
{
	char deviceId[AUDIO_DEVICE_ID_BYTES];
	U16 deviceName[AUDIO_DEVICE_NAME_CHARS];
	if (!AIL_ex_enumerate_output_devices(index, deviceId, AUDIO_DEVICE_ID_BYTES, deviceName, AUDIO_DEVICE_NAME_CHARS)) {
		return FALSE;
	}
	id = deviceId;
	name = UnicodeString((const WideChar *)deviceName);
	return TRUE;
}

void Win32GameEngine::setAudioOutputDevice( const AsciiString &id )
{
	AIL_ex_set_output_device(id.str());
}

