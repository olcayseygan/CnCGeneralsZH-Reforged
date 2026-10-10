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

// FILE: WinMain.cpp //////////////////////////////////////////////////////////
// 
// Entry point for game application
//
// Author: Colin Day, April 2001
//
///////////////////////////////////////////////////////////////////////////////

// SYSTEM INCLUDES ////////////////////////////////////////////////////////////
#define WIN32_LEAN_AND_MEAN  // only bare bones windows stuff wanted
#include <windows.h>
#include <stdlib.h>
#include <crtdbg.h>
#include <eh.h>
#include <ole2.h>
#include <dbt.h>

// USER INCLUDES //////////////////////////////////////////////////////////////
#include "WinMain.h"
#include "Lib/BaseType.h"
#include "stringex.h"
#include "Common/CopyProtection.h"
#include "Common/CriticalSection.h"
#include "Common/GlobalData.h"
#include "Common/GameEngine.h"
#include "Common/GameSounds.h"
#include "Common/Debug.h"
#include "Common/EarlyCommandLine.h"
#include "Common/EarlyOptions.h"
#include "Common/Errors.h"
#include "Common/Monitors.h"
#include "Common/GameMemory.h"
#include "Common/INIException.h"
#include "Common/SafeDisc/CdaPfn.h"
#include "Common/StackDump.h"
#include "Common/MessageStream.h"
#include "Common/PlayerList.h"
#include "Common/Registry.h"
#include "Common/Team.h"
#include "GameClient/InGameUI.h"
#include "GameClient/GameClient.h"
#include "GameLogic/GameLogic.h"  ///< @todo for demo, remove
#include "GameClient/Mouse.h"
#include "GameClient/IMEManager.h"
#include "GameClient/LookAtXlat.h"
#include "Win32Device/GameClient/Win32Mouse.h"
#include "Win32Device/Common/Win32GameEngine.h"
#include "Common/Version.h"
#include "BuildVersion.h"
#include "GeneratedVersion.h"
#include "Resource.h"

#include <rts/profile.h>

#ifdef _INTERNAL
// for occasional debugging...
//#pragma optimize("", off)
//#pragma message("************************************** WARNING, optimization disabled for debugging purposes")
#endif

// Ask the driver to put this process on the discrete GPU.  Adapter 0 is Intel UHD
// 630 on this machine; 16x MSAA on it TDRs.  The export is what NVIDIA Optimus and
// AMD PowerXpress read before WinMain.  DX8Wrapper still picks the discrete adapter
// by vendor id, which is what actually selects the device on a desktop.
extern "C"
{
	__declspec(dllexport) DWORD NvOptimusEnablement = 0x00000001;
	__declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
}

// GLOBALS ////////////////////////////////////////////////////////////////////
HINSTANCE ApplicationHInstance = NULL;  ///< our application instance
HWND ApplicationHWnd = NULL;  ///< our application window handle
Bool ApplicationIsWindowed = false;
Bool ApplicationIsBorderless = false;		///< -borderless: windowed, but with no caption or frame
static Bool ApplicationIsHeadless = false;	///< -headless: the window is created, but never shown
Win32Mouse *TheWin32Mouse= NULL;  ///< for the WndProc() only
DWORD TheMessageTime = 0;	///< For getting the time that a message was posted from Windows.

const Char *g_strFile = "data\\Generals.str";
const Char *g_csfFile = "data\\%s\\Generals.csf";
char *gAppPrefix = ""; /// So WB can have a different debug log file name.

static HANDLE GeneralsMutex = NULL;
#define GENERALS_GUID "685EAFF2-3216-4265-B047-251C5F4B82F3"
#define DEFAULT_XRESOLUTION 800
#define DEFAULT_YRESOLUTION 600

extern void Reset_D3D_Device(bool active);

static Bool gInitializing = false;
static Bool gDoPaint = true;
static Bool isWinMainActive = false;

// The fullscreen display follows activation from a posted message rather than from inside
// WM_ACTIVATEAPP.  Changing the mode and the window there let the change itself activate or
// deactivate the game again, nested inside the handler, and Alt+Tab back flipped between the game
// and the desktop until the window stayed minimized (issue #45).  The posted message applies
// whatever state the last activation left, once.  It starts out not shown: the device only puts
// the display on when the window is in the foreground, so a game started or left behind another
// window during the splash gets it from its first activation.  When the game was in front, that
// first apply finds the mode already set and changes nothing.
static const UINT WM_APP_APPLY_ACTIVATION = WM_APP + 1;
static Bool isDisplayShownActive = false;

// Whether the cursor is currently clipped to the window.  Activating the game by clicking the
// taskbar leaves the pointer wherever it was, and clipping right then snatched it into the window
// from across the desktop; the clip waits for the pointer to arrive on its own now.
static Bool isCursorClippedToWindow = false;

//-------------------------------------------------------------------------------------------------
/** True when the OS cursor is over our client area right now. */
//-------------------------------------------------------------------------------------------------
static Bool isCursorOverWindow( void )
{
	POINT cursor;
	RECT windowRect;

	if( ApplicationHWnd == NULL || !GetCursorPos( &cursor ) || !GetWindowRect( ApplicationHWnd, &windowRect ) )
		return false;

	return PtInRect( &windowRect, cursor ) != 0;
}

//-------------------------------------------------------------------------------------------------
/** A GameMessage stamps itself with the local player, so none can be made until the player list
	* exists.  A window closed while the game was still loading used to fault in that constructor. */
//-------------------------------------------------------------------------------------------------
static Bool canPostQuitMessage( void )
{
	return TheMessageStream != NULL && ThePlayerList != NULL && ThePlayerList->getLocalPlayer() != NULL;
}

static HBITMAP gLoadScreenBitmap = NULL;

//#define DEBUG_WINDOWS_MESSAGES

#ifdef DEBUG_WINDOWS_MESSAGES
static const char *messageToString(unsigned int message)
{	
	static char name[32];

	switch (message)
	{
	case WM_NULL: return "WM_NULL";                     
	case WM_CREATE: return  "WM_CREATE";               
	case WM_DESTROY: return  "WM_DESTROY";            
	case WM_MOVE: return  "WM_MOVE";               
	case WM_SIZE: return  "WM_SIZE";                 
	case WM_ACTIVATE: return  "WM_ACTIVATE";             
	case WM_SETFOCUS: return  "WM_SETFOCUS";             
	case WM_KILLFOCUS: return  "WM_KILLFOCUS";            
	case WM_ENABLE: return  "WM_ENABLE";               
	case WM_SETREDRAW: return  "WM_SETREDRAW";            
	case WM_SETTEXT: return  "WM_SETTEXT";              
	case WM_GETTEXT: return  "WM_GETTEXT";              
	case WM_GETTEXTLENGTH: return  "WM_GETTEXTLENGTH";        
	case WM_PAINT: return  "WM_PAINT";                
	case WM_CLOSE: return  "WM_CLOSE";                
	case WM_QUERYENDSESSION: return  "WM_QUERYENDSESSION";      
	case WM_QUIT: return  "WM_QUIT";                 
	case WM_QUERYOPEN: return  "WM_QUERYOPEN";            
	case WM_ERASEBKGND: return  "WM_ERASEBKGND";           
	case WM_SYSCOLORCHANGE: return  "WM_SYSCOLORCHANGE";       
	case WM_ENDSESSION: return  "WM_ENDSESSION";           
	case WM_SHOWWINDOW: return  "WM_SHOWWINDOW";           
	case WM_WININICHANGE: return "WM_WININICHANGE";
	case WM_DEVMODECHANGE: return  "WM_DEVMODECHANGE";        
	case WM_ACTIVATEAPP: return  "WM_ACTIVATEAPP";          
	case WM_FONTCHANGE: return  "WM_FONTCHANGE";           
	case WM_TIMECHANGE: return  "WM_TIMECHANGE";           
	case WM_CANCELMODE: return  "WM_CANCELMODE";           
	case WM_SETCURSOR: return  "WM_SETCURSOR";            
	case WM_MOUSEACTIVATE: return  "WM_MOUSEACTIVATE";        
	case WM_CHILDACTIVATE: return  "WM_CHILDACTIVATE";        
	case WM_QUEUESYNC: return  "WM_QUEUESYNC";            
	case WM_GETMINMAXINFO: return  "WM_GETMINMAXINFO";        
	case WM_PAINTICON: return  "WM_PAINTICON";            
	case WM_ICONERASEBKGND: return  "WM_ICONERASEBKGND";       
	case WM_NEXTDLGCTL: return  "WM_NEXTDLGCTL";           
	case WM_SPOOLERSTATUS: return  "WM_SPOOLERSTATUS";        
	case WM_DRAWITEM: return  "WM_DRAWITEM";             
	case WM_MEASUREITEM: return  "WM_MEASUREITEM";          
	case WM_DELETEITEM: return  "WM_DELETEITEM";           
	case WM_VKEYTOITEM: return  "WM_VKEYTOITEM";           
	case WM_CHARTOITEM: return  "WM_CHARTOITEM";           
	case WM_SETFONT: return  "WM_SETFONT";              
	case WM_GETFONT: return  "WM_GETFONT";              
	case WM_SETHOTKEY: return  "WM_SETHOTKEY";            
	case WM_GETHOTKEY: return  "WM_GETHOTKEY";            
	case WM_QUERYDRAGICON: return  "WM_QUERYDRAGICON";        
	case WM_COMPAREITEM: return  "WM_COMPAREITEM";          
	case WM_COMPACTING: return  "WM_COMPACTING";           
	case WM_COMMNOTIFY: return  "WM_COMMNOTIFY";
	case WM_WINDOWPOSCHANGING: return  "WM_WINDOWPOSCHANGING";    
	case WM_WINDOWPOSCHANGED: return  "WM_WINDOWPOSCHANGED";     
	case WM_POWER: return  "WM_POWER";                
	case WM_COPYDATA: return  "WM_COPYDATA";             
	case WM_CANCELJOURNAL: return  "WM_CANCELJOURNAL";        
	case WM_NOTIFY: return  "WM_NOTIFY";               
	case WM_INPUTLANGCHANGEREQUEST: return  "WM_INPUTLANGCHANGEREQUES";
	case WM_INPUTLANGCHANGE: return  "WM_INPUTLANGCHANGE";      
	case WM_TCARD: return  "WM_TCARD";                
	case WM_HELP: return  "WM_HELP";                 
	case WM_USERCHANGED: return  "WM_USERCHANGED";          
	case WM_NOTIFYFORMAT: return  "WM_NOTIFYFORMAT";         
	case WM_CONTEXTMENU: return  "WM_CONTEXTMENU";          
	case WM_STYLECHANGING: return  "WM_STYLECHANGING";        
	case WM_STYLECHANGED: return  "WM_STYLECHANGED";         
	case WM_DISPLAYCHANGE: return  "WM_DISPLAYCHANGE";        
	case WM_GETICON: return  "WM_GETICON";              
	case WM_SETICON: return  "WM_SETICON";              
	case WM_NCCREATE: return  "WM_NCCREATE";             
	case WM_NCDESTROY: return  "WM_NCDESTROY";            
	case WM_NCCALCSIZE: return  "WM_NCCALCSIZE";           
	case WM_NCHITTEST: return  "WM_NCHITTEST";            
	case WM_NCPAINT: return  "WM_NCPAINT";              
	case WM_NCACTIVATE: return  "WM_NCACTIVATE";           
	case WM_GETDLGCODE: return  "WM_GETDLGCODE";           
	case WM_SYNCPAINT: return  "WM_SYNCPAINT";            
	case WM_NCMOUSEMOVE: return  "WM_NCMOUSEMOVE";          
	case WM_NCLBUTTONDOWN: return  "WM_NCLBUTTONDOWN";        
	case WM_NCLBUTTONUP: return  "WM_NCLBUTTONUP";          
	case WM_NCLBUTTONDBLCLK: return  "WM_NCLBUTTONDBLCLK";      
	case WM_NCRBUTTONDOWN: return  "WM_NCRBUTTONDOWN";        
	case WM_NCRBUTTONUP: return  "WM_NCRBUTTONUP";          
	case WM_NCRBUTTONDBLCLK: return  "WM_NCRBUTTONDBLCLK";      
	case WM_NCMBUTTONDOWN: return  "WM_NCMBUTTONDOWN";        
	case WM_NCMBUTTONUP: return  "WM_NCMBUTTONUP";          
	case WM_NCMBUTTONDBLCLK: return  "WM_NCMBUTTONDBLCLK";      
	case WM_KEYDOWN: return  "WM_KEYDOWN";              
	case WM_KEYUP: return  "WM_KEYUP";                
	case WM_CHAR: return  "WM_CHAR";                 
	case WM_DEADCHAR: return  "WM_DEADCHAR";             
	case WM_SYSKEYDOWN: return  "WM_SYSKEYDOWN";           
	case WM_SYSKEYUP: return  "WM_SYSKEYUP";             
	case WM_SYSCHAR: return  "WM_SYSCHAR";              
	case WM_SYSDEADCHAR: return  "WM_SYSDEADCHAR";          
	case WM_KEYLAST: return  "WM_KEYLAST";              
	case WM_IME_STARTCOMPOSITION: return  "WM_IME_STARTCOMPOSITION"; 
	case WM_IME_ENDCOMPOSITION: return  "WM_IME_ENDCOMPOSITION";   
	case WM_IME_COMPOSITION: return  "WM_IME_COMPOSITION";      
	case WM_INITDIALOG: return  "WM_INITDIALOG";           
	case WM_COMMAND: return  "WM_COMMAND";              
	case WM_SYSCOMMAND: return  "WM_SYSCOMMAND";           
	case WM_TIMER: return  "WM_TIMER";                
	case WM_HSCROLL: return  "WM_HSCROLL";              
	case WM_VSCROLL: return  "WM_VSCROLL";              
	case WM_INITMENU: return  "WM_INITMENU";             
	case WM_INITMENUPOPUP: return  "WM_INITMENUPOPUP";        
	case WM_MENUSELECT: return  "WM_MENUSELECT";           
	case WM_MENUCHAR: return  "WM_MENUCHAR";             
	case WM_ENTERIDLE: return  "WM_ENTERIDLE";            
	case WM_CTLCOLORMSGBOX: return  "WM_CTLCOLORMSGBOX";       
	case WM_CTLCOLOREDIT: return  "WM_CTLCOLOREDIT";         
	case WM_CTLCOLORLISTBOX: return  "WM_CTLCOLORLISTBOX";      
	case WM_CTLCOLORBTN: return  "WM_CTLCOLORBTN";          
	case WM_CTLCOLORDLG: return  "WM_CTLCOLORDLG";          
	case WM_CTLCOLORSCROLLBAR: return  "WM_CTLCOLORSCROLLBAR";    
	case WM_CTLCOLORSTATIC: return  "WM_CTLCOLORSTATIC";       
	case WM_MOUSEMOVE: return  "WM_MOUSEMOVE";            
	case WM_LBUTTONDOWN: return  "WM_LBUTTONDOWN";          
	case WM_LBUTTONUP: return  "WM_LBUTTONUP";            
	case WM_LBUTTONDBLCLK: return  "WM_LBUTTONDBLCLK";        
	case WM_RBUTTONDOWN: return  "WM_RBUTTONDOWN";          
	case WM_RBUTTONUP: return  "WM_RBUTTONUP";            
	case WM_RBUTTONDBLCLK: return  "WM_RBUTTONDBLCLK";        
	case WM_MBUTTONDOWN: return  "WM_MBUTTONDOWN";          
	case WM_MBUTTONUP: return  "WM_MBUTTONUP";            
	case WM_MBUTTONDBLCLK: return  "WM_MBUTTONDBLCLK";        
//	case WM_MOUSEWHEEL: return  "WM_MOUSEWHEEL";           
	case WM_PARENTNOTIFY: return  "WM_PARENTNOTIFY";         
	case WM_ENTERMENULOOP: return  "WM_ENTERMENULOOP";        
	case WM_EXITMENULOOP: return  "WM_EXITMENULOOP";         
	case WM_NEXTMENU: return  "WM_NEXTMENU";             
	case WM_SIZING: return  "WM_SIZING";               
	case WM_CAPTURECHANGED: return  "WM_CAPTURECHANGED";       
	case WM_MOVING: return  "WM_MOVING";               
	case WM_POWERBROADCAST: return  "WM_POWERBROADCAST";
	case WM_DEVICECHANGE: return  "WM_DEVICECHANGE";         
	case WM_MDICREATE: return  "WM_MDICREATE";            
	case WM_MDIDESTROY: return  "WM_MDIDESTROY";           
	case WM_MDIACTIVATE: return  "WM_MDIACTIVATE";          
	case WM_MDIRESTORE: return  "WM_MDIRESTORE";           
	case WM_MDINEXT: return  "WM_MDINEXT";              
	case WM_MDIMAXIMIZE: return  "WM_MDIMAXIMIZE";          
	case WM_MDITILE: return  "WM_MDITILE";              
	case WM_MDICASCADE: return  "WM_MDICASCADE";           
	case WM_MDIICONARRANGE: return  "WM_MDIICONARRANGE";       
	case WM_MDIGETACTIVE: return  "WM_MDIGETACTIVE";         
	case WM_MDISETMENU: return  "WM_MDISETMENU";           
	case WM_ENTERSIZEMOVE: return  "WM_ENTERSIZEMOVE";        
	case WM_EXITSIZEMOVE: return  "WM_EXITSIZEMOVE";         
	case WM_DROPFILES: return  "WM_DROPFILES";            
	case WM_MDIREFRESHMENU: return  "WM_MDIREFRESHMENU";       
	case WM_IME_SETCONTEXT: return  "WM_IME_SETCONTEXT";       
	case WM_IME_NOTIFY: return  "WM_IME_NOTIFY";           
	case WM_IME_CONTROL: return  "WM_IME_CONTROL";          
	case WM_IME_COMPOSITIONFULL: return  "WM_IME_COMPOSITIONFULL";  
	case WM_IME_SELECT: return  "WM_IME_SELECT";           
	case WM_IME_CHAR: return  "WM_IME_CHAR";             
	case WM_IME_KEYDOWN: return  "WM_IME_KEYDOWN";          
	case WM_IME_KEYUP: return  "WM_IME_KEYUP";            
//	case WM_MOUSEHOVER: return  "WM_MOUSEHOVER";           
//	case WM_MOUSELEAVE: return  "WM_MOUSELEAVE";           
	case WM_CUT: return  "WM_CUT";                  
	case WM_COPY: return  "WM_COPY";                 
	case WM_PASTE: return  "WM_PASTE";                
	case WM_CLEAR: return  "WM_CLEAR";                
	case WM_UNDO: return  "WM_UNDO";                 
	case WM_RENDERFORMAT: return  "WM_RENDERFORMAT";         
	case WM_RENDERALLFORMATS: return  "WM_RENDERALLFORMATS";     
	case WM_DESTROYCLIPBOARD: return  "WM_DESTROYCLIPBOARD";     
	case WM_DRAWCLIPBOARD: return  "WM_DRAWCLIPBOARD";        
	case WM_PAINTCLIPBOARD: return  "WM_PAINTCLIPBOARD";       
	case WM_VSCROLLCLIPBOARD: return  "WM_VSCROLLCLIPBOARD";     
	case WM_SIZECLIPBOARD: return  "WM_SIZECLIPBOARD";        
	case WM_ASKCBFORMATNAME: return  "WM_ASKCBFORMATNAME";      
	case WM_CHANGECBCHAIN: return  "WM_CHANGECBCHAIN";        
	case WM_HSCROLLCLIPBOARD: return  "WM_HSCROLLCLIPBOARD";     
	case WM_QUERYNEWPALETTE: return  "WM_QUERYNEWPALETTE";      
	case WM_PALETTEISCHANGING: return  "WM_PALETTEISCHANGING";    
	case WM_PALETTECHANGED: return  "WM_PALETTECHANGED";       
	case WM_HOTKEY: return  "WM_HOTKEY";               
	case WM_PRINT: return  "WM_PRINT";                
	case WM_PRINTCLIENT: return  "WM_PRINTCLIENT";          
	case WM_HANDHELDFIRST: return  "WM_HANDHELDFIRST";        
	case WM_HANDHELDLAST: return  "WM_HANDHELDLAST";         
	case WM_AFXFIRST: return  "WM_AFXFIRST";             
	case WM_AFXLAST: return  "WM_AFXLAST";              
	case WM_PENWINFIRST: return  "WM_PENWINFIRST";          
	case WM_PENWINLAST: return  "WM_PENWINLAST";
	default: return "WM_UNKNOWN";
	};
}
#endif

// WndProc ====================================================================
/** Window Procedure */
//=============================================================================
LRESULT CALLBACK WndProc( HWND hWnd, UINT message, 
													WPARAM wParam, LPARAM lParam )
{

	try
	{
		// First let the IME manager do it's stuff. 
		if ( TheIMEManager )
		{
			if ( TheIMEManager->serviceIMEMessage( hWnd, message, wParam, lParam ) )
			{
				// The manager intercepted an IME message so return the result
				return TheIMEManager->result();
			}
		}
		
#ifdef DO_COPY_PROTECTION
		// Check for messages from the launcher
		CopyProtect::checkForMessage(message, lParam);
#endif

#ifdef	DEBUG_WINDOWS_MESSAGES
		static msgCount=0;
		char testString[256];
		sprintf(testString,"\n%d: %s (%X,%X)", msgCount++,messageToString(message), wParam, lParam); 
		OutputDebugString(testString);
#endif

		// handle all window messages
		switch( message ) 
		{
			//-------------------------------------------------------------------------
			case WM_NCHITTEST:
			// Prevent the user from selecting the menu in fullscreen mode
            if( !ApplicationIsWindowed )
                return HTCLIENT;
            break;

			//-------------------------------------------------------------------------
			case WM_POWERBROADCAST:
            switch( wParam )
            {
                #ifndef PBT_APMQUERYSUSPEND
                    #define PBT_APMQUERYSUSPEND 0x0000
                #endif
                case PBT_APMQUERYSUSPEND:
                    // At this point, the app should save any data for open
                    // network connections, files, etc., and prepare to go into
                    // a suspended mode.
                    return TRUE;

                #ifndef PBT_APMRESUMESUSPEND
                    #define PBT_APMRESUMESUSPEND 0x0007
                #endif
                case PBT_APMRESUMESUSPEND:
                    // At this point, the app should recover any data, network
                    // connections, files, etc., and resume running from when
                    // the app was suspended.
                    return TRUE;
            }
            break;
			//-------------------------------------------------------------------------
			case WM_SYSCOMMAND:
            // Prevent moving/sizing and power loss in fullscreen mode
            switch( wParam )
            {
                // a bare left Alt opens the system menu and its modal loop stalls the game, so
                // swallow it in windowed mode too (Alt is also the waypoint modifier).
                case SC_KEYMENU:
                    return 1;

                case SC_MOVE:
                case SC_SIZE:
                case SC_MAXIMIZE:
                case SC_MONITORPOWER:
                    if( FALSE == ApplicationIsWindowed )
                        return 1;
                    break;
            }
            break;

			case WM_QUERYENDSESSION:
			{
				// guarded like the focus handlers below: these messages can arrive before the
				// engine is up and, more often, while it is tearing down - a close or end-session
				// that lands after TheMessageStream is gone used to fault right here.
				if (canPostQuitMessage())
					TheMessageStream->appendMessage(GameMessage::MSG_META_DEMO_INSTANT_QUIT);
				return 0;	//don't allow Windows to shutdown while game is running.
			}

			// ------------------------------------------------------------------------
			case WM_CLOSE:
			if (TheGameEngine && !TheGameEngine->getQuitting())
			{
				//user is exiting without using the menus

				// Closed while still loading: nothing can carry a message yet, so the engine is
				// told to stop and execute() never starts its loop.
				if (!canPostQuitMessage())
				{
					TheGameEngine->setQuitting(TRUE);
					return 0;
				}

				//This method didn't work in cinematics because we don't process messages.
				//But it's the cleanest way to exit that's similar to using menus.
				TheMessageStream->appendMessage(GameMessage::MSG_META_DEMO_INSTANT_QUIT);

				//This method used to disable quitting.  We just put up the options screen instead.
				//TheMessageStream->appendMessage(GameMessage::MSG_META_OPTIONS);

				//This method works everywhere but isn't as clean at shutting down.
				//TheGameEngine->checkAbnormalQuitting();	//old way to log disconnections for ALT-F4
				//TheGameEngine->reset();
				//TheGameEngine->setQuitting(TRUE);
				//_exit(EXIT_SUCCESS);
				return 0;
			}
			// No engine to ask, or it is already on its way out: let DefWindowProc take
			// the window down. Falling through into WM_SETFOCUS is what used to happen.
			break;

			// ------------------------------------------------------------------------
			case WM_SETFOCUS:
			{

				//
				// reset the state of our keyboard cause we haven't been paying
				// attention to the keys while focus was away
				//
				if( TheKeyboard )
					TheKeyboard->resetKeys();

				//
				// ...and whatever those keys were driving.  resetKeys clears the keyboard device; it
				// has no way to know that an arrow key held when focus went away left the camera
				// scrolling, and no key-up is ever coming for it.
				//
				if( TheLookAtTranslator )
					TheLookAtTranslator->resetModes();
				if( TheInGameUI )
					TheInGameUI->clearModifierModes();

				if (TheWin32Mouse)
					TheWin32Mouse->lostFocus(FALSE);

				break;

			}  // end set focus

			//-------------------------------------------------------------------------
			case WM_SIZE:
				// When W3D initializes, it resizes the window.  So stop repainting.
				if (!gInitializing)
				{
					gDoPaint = false;
					//
					// That resize is also the moment borderless becomes fullscreen.  The window is born
					// small and centred, so the splash sits on the desktop the way it always has instead
					// of a screen of black; W3D then grows it to the back buffer, which is the desktop
					// resolution.  It grows it with SWP_NOMOVE, keeping the top left corner where the
					// small window was, which would hang the thing off the bottom right of the screen.
					// Put it at the origin instead.
					//
					if (ApplicationIsBorderless)
					{
						const RECT screen = findMonitor(TheGlobalData ? TheGlobalData->m_monitor.str() : "").rect;
						::SetWindowPos(hWnd, NULL, screen.left, screen.top, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
					}
				}
				break;

			//-------------------------------------------------------------------------
			case WM_KILLFOCUS:
			{
				if (TheKeyboard )
					TheKeyboard->resetKeys();
				if( TheLookAtTranslator )
					TheLookAtTranslator->resetModes();
				if( TheInGameUI )
					TheInGameUI->clearModifierModes();		// see WM_SETFOCUS above
				if (TheWin32Mouse)
					TheWin32Mouse->lostFocus(TRUE);

				break;
			}

			//-------------------------------------------------------------------------
			case WM_ACTIVATEAPP:
			{
//				DWORD threadId=GetCurrentThreadId();
				if ((bool) wParam != isWinMainActive)
				{	isWinMainActive = (BOOL) wParam;
					
					if (TheGameEngine)
						TheGameEngine->setIsActive(isWinMainActive);

					::PostMessage(hWnd, WM_APP_APPLY_ACTIVATION, 0, 0);
					if (isWinMainActive)
					{	//restore mouse cursor to our custom version.
						if (TheWin32Mouse)
							TheWin32Mouse->setCursor(TheWin32Mouse->getMouseCursor());
					}
				}
				return 0;
			}

			case WM_APP_APPLY_ACTIVATION:
			{
				if (isDisplayShownActive != isWinMainActive)
				{
					isDisplayShownActive = isWinMainActive;
					Reset_D3D_Device(isWinMainActive);
					// A fullscreen game is minimized while away, and Alt+Tab activates it while it is
					// still minimized, when DefWindowProc gives an active window no keyboard focus.
					// The restore above activates nothing, so the window came back active with no
					// focus: every key arrived as WM_SYSKEYDOWN and beeped, and no WM_SETFOCUS came to
					// reset the keyboard or take the mouse out of its lost-focus state, where it
					// leaves the Windows arrow on screen (issue #54).
					if (isWinMainActive)
						::SetFocus(hWnd);
				}
				return 0;
			}
			//-------------------------------------------------------------------------
			case WM_ACTIVATE:
			{
				Int active = LOWORD( wParam );

				//
				// when window is becoming deactivated we must release mouse cursor
				// locks on our region, otherwise set the mouse limit region again
				// which will clip the cursor to our window
				//
				if( active == WA_INACTIVE )
				{

					ClipCursor( NULL );
					isCursorClippedToWindow = false;
					// A -wav run is heard by its file, not a person: muting it here silenced the
					// capture whenever another window took the focus.
					if (TheAudio && TheGlobalData->m_wavEndFrame <= 0)
						TheAudio->loseFocus();
				}  // end if
				else
				{
					// Only take the cursor if it is already over us.  See isCursorOverWindow.
					if( TheMouse && isCursorOverWindow() )
					{
						TheMouse->setMouseLimits();
						isCursorClippedToWindow = true;
					}

					if (TheAudio)
						TheAudio->regainFocus();

				}  // end else
				break;

			}  // end case activate

			//-------------------------------------------------------------------------
			case WM_KEYDOWN:
			{
				Int key = (Int)wParam;

				switch( key )
				{

					//---------------------------------------------------------------------
					case VK_ESCAPE:
					{

						PostQuitMessage( 0 );
						break;

					}  // end VK_ESCAPE


				}  // end switch

				return 0;

			}  // end WM_KEYDOWN

			//-------------------------------------------------------------------------
			case WM_LBUTTONDOWN:
			case WM_LBUTTONUP:
			case WM_LBUTTONDBLCLK:

			case WM_MBUTTONDOWN:
			case WM_MBUTTONUP:
			case WM_MBUTTONDBLCLK:

			case WM_RBUTTONDOWN:
			case WM_RBUTTONUP:
			case WM_RBUTTONDBLCLK:
			{

				if( TheWin32Mouse )
					TheWin32Mouse->addWin32Event( message, wParam, lParam, TheMessageTime );

				return 0;

			}  // end WM_LBUTTONDOWN

			//-------------------------------------------------------------------------
			case 0x020A: // WM_MOUSEWHEEL
			{
				long x = (long) LOWORD(lParam);
				long y = (long) HIWORD(lParam);
				RECT rect;

				// ignore when outside of client area
				GetWindowRect( ApplicationHWnd, &rect );
				if( x < rect.left || x > rect.right || y < rect.top || y > rect.bottom )
					return 0;

				if( TheWin32Mouse )
					TheWin32Mouse->addWin32Event( message, wParam, lParam, TheMessageTime );

				return 0;

			}  // end WM_MOUSEWHEEL


			//-------------------------------------------------------------------------
			case WM_MOUSEMOVE:
			{
				Int x = (Int)LOWORD( lParam );
				Int y = (Int)HIWORD( lParam );
				RECT rect;
//				Int keys = wParam;

				// ignore when outside of client area
				GetClientRect( ApplicationHWnd, &rect );
				if( x < rect.left || x > rect.right || y < rect.top || y > rect.bottom )
					return 0;

				// The pointer has walked in of its own accord, so it is fair to hold it now.
				if( isWinMainActive && !isCursorClippedToWindow && TheMouse )
				{
					TheMouse->setMouseLimits();
					isCursorClippedToWindow = true;
				}

				if( TheWin32Mouse )
					TheWin32Mouse->addWin32Event( message, wParam, lParam, TheMessageTime );

				return 0;

			}  // end WM_MOUSEMOVE

			//-------------------------------------------------------------------------
			case WM_SETCURSOR:
			{
				if (TheWin32Mouse && (HWND)wParam == ApplicationHWnd)
					TheWin32Mouse->setCursor(TheWin32Mouse->getMouseCursor());
				return TRUE;	//tell Windows not to reset mouse cursor image to default.
			}

			case WM_PAINT:
			{
				if (gDoPaint) {
					PAINTSTRUCT paint;
					HDC dc = ::BeginPaint(hWnd, &paint);
#if 0  
					::SetTextColor(dc, RGB(255,255,255));
					::SetBkColor(dc, RGB(0,0,0));
					::TextOut(dc, 30, 30, "Loading Command & Conquer Generals...", 37);
#endif
					if (gLoadScreenBitmap!=NULL) {
						Int savContext = ::SaveDC(dc);
						HDC tmpDC = ::CreateCompatibleDC(dc);
						HBITMAP savBitmap = (HBITMAP)::SelectObject(tmpDC, gLoadScreenBitmap);
						::BitBlt(dc, 0, 0, DEFAULT_XRESOLUTION, DEFAULT_YRESOLUTION, tmpDC, 0, 0, SRCCOPY);
						::SelectObject(tmpDC, savBitmap);
						::DeleteDC(tmpDC);
						::RestoreDC(dc, savContext);
					}
					::EndPaint(hWnd, &paint);
					return TRUE;
				}
				break;
			}

			case WM_ERASEBKGND:
			{
				if (!gDoPaint) 
					return TRUE;	//we don't need to erase the background because we always draw entire window.
				break;
			}

// Well, it was a nice idea, but we don't get a message for an ejection. 
// (Really unforunate, actually.) I'm leaving this in in-case some one wants
// to trap a different device change (for instance, removal of a mouse) - jkmcd
#if 0
			case WM_DEVICECHANGE: 
			{
				if (((UINT) wParam) == DBT_DEVICEREMOVEPENDING) 
				{
					DEV_BROADCAST_HDR *hdr = (DEV_BROADCAST_HDR*) lParam;
					if (!hdr) {
						break;
					}

					if (hdr->dbch_devicetype != DBT_DEVTYP_VOLUME)  {
						break;
					}

					// Lets discuss how Windows is a flaming pile of poo. I'm now casting the header
					// directly into the structure, because its the one I want, and this is just how
					// its done. I hate Windows. - jkmcd
					DEV_BROADCAST_VOLUME *vol = (DEV_BROADCAST_VOLUME*) (hdr);

					// @todo - Yikes. This could cause us all kinds of pain. I don't really want 
					// to even think about the stink this could cause us.
					TheFileSystem->unloadMusicFilesFromCD(vol->dbcv_unitmask);
					return TRUE;
				}
				break;
			}
#endif
		}  // end switch

	}
	catch (...)
	{
		RELEASE_CRASH(("Uncaught exception in Main::WndProc... probably should not happen\n"));
		// no rethrow
	}

//In full-screen mode, only pass these messages onto the default windows handler.
//Appears to fix issues with dual monitor systems but doesn't seem safe?
///@todo: Look into proper support for dual monitor systems.
/*	if (!ApplicationIsWindowed)
	switch (message)
	{
		case WM_PAINT:
		case WM_NCCREATE:
		case WM_NCDESTROY:
		case WM_NCCALCSIZE:
		case WM_NCPAINT:
				return DefWindowProc( hWnd, message, wParam, lParam );
	}
	return 0;*/

	return DefWindowProc( hWnd, message, wParam, lParam );

}  // end WndProc

// initializeAppWindows =======================================================
/** Register windows class and create application windows. */
//=============================================================================
static Bool initializeAppWindows( HINSTANCE hInstance, Int nCmdShow, Bool runWindowed )
{
	DWORD windowStyle;
	Int startWidth = DEFAULT_XRESOLUTION,
			startHeight = DEFAULT_YRESOLUTION;

	// register the window class

  WNDCLASS wndClass = { CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS, WndProc, 0, 0, hInstance,
                       LoadIcon (hInstance, MAKEINTRESOURCE(IDI_ApplicationIcon)),
                       NULL/*LoadCursor(NULL, IDC_ARROW)*/, 
                       (HBRUSH)GetStockObject(BLACK_BRUSH), NULL,
	                     TEXT("Game Window") };
  RegisterClass( &wndClass );

   // Create our main window
	// a headless run has no picture at all, so there is nothing for borderless to mean
	if (ApplicationIsHeadless)
		ApplicationIsBorderless = false;

	windowStyle =  WS_POPUP|WS_VISIBLE;
	if (ApplicationIsHeadless)
		windowStyle = WS_POPUP;		// born hidden, no frame, and stays that way
	else if (ApplicationIsBorderless)
		windowStyle |= WS_SYSMENU;	// no caption, no frame; system menu so alt+F4 still closes it
	else if (runWindowed)
		// WS_MINIMIZEBOX: a windowed game should have the button every other window has.  Without it
		// the only way out of the window was alt+tab, and the system menu offered a Minimise entry
		// that did nothing.
		windowStyle |= WS_DLGFRAME | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
	else
		windowStyle |= WS_EX_TOPMOST | WS_SYSMENU;

	//
	// Borderless is born at the splash's own 800x600, centred, with the desktop still showing round
	// it, and only becomes fullscreen when W3D resizes it to the back buffer - which parseBorderless
	// in CommandLine.cpp has already set to the desktop resolution.  WM_SIZE puts it at the origin
	// when that happens.  Filling the screen with black for the several seconds the game takes to
	// load buys nothing and hides the desktop for no reason.
	//
	RECT rect;
	rect.left = 0;
	rect.top = 0;
	rect.right = startWidth;
	rect.bottom = startHeight;
	AdjustWindowRect (&rect, windowStyle, FALSE);
	if (runWindowed) {
		// Makes the normal debug 800x600 window center in the screen.
		startWidth = DEFAULT_XRESOLUTION;
		startHeight= DEFAULT_YRESOLUTION;
	}

	gInitializing = true;

	// Born in the middle of the monitor Options.ini names, so the splash and then the game are on it
	// from the first frame.  The engine cannot say which that is yet.
	char savedMonitor[CCHDEVICENAME];
	findEarlyOptionValue( "Monitor", savedMonitor, sizeof( savedMonitor ) );
	const RECT screen = findMonitor( savedMonitor ).rect;

  HWND hWnd = CreateWindow( TEXT("Game Window"),
                            TEXT("Command and Conquer Generals"),
                            windowStyle,
														(screen.left + screen.right) / 2 - (startWidth / 2), // original position X
														(screen.top + screen.bottom) / 2 - (startHeight / 2),// original position Y
														// Lorenzen nudged the window higher
														// so the constantdebug report would 
														// not get obliterated by assert windows, thank you.
														//(GetSystemMetrics( SM_CXSCREEN ) / 2) - (startWidth / 2),   //this works with any screen res
														//(GetSystemMetrics( SM_CYSCREEN ) / 25) - (startHeight / 25),//this works with any screen res
														rect.right-rect.left,
														rect.bottom-rect.top,
														0L, 
														0L, 
														hInstance, 
														0L );


	//
	// A headless run has a window because W3D wants a device and a device wants a window, but
	// nothing is ever presented into it - so it is never raised, never focused and never shown.
	// Every call below either puts it on screen or takes the focus off whatever the machine was
	// doing, which is exactly what a batch of unattended matches must not do.
	//
	if (!ApplicationIsHeadless)
	{
		if (!runWindowed)
		{	SetWindowPos(hWnd, HWND_TOPMOST, 0, 0, 0, 0,SWP_NOSIZE |SWP_NOMOVE);
		}
		else
			SetWindowPos(hWnd, HWND_TOP, 0, 0, 0, 0,SWP_NOSIZE |SWP_NOMOVE);

		SetFocus(hWnd);

		SetForegroundWindow(hWnd);
		// A drawing game has to be on the screen. nCmdShow from a service or a job is often
		// SW_HIDE, which is already what -headless asked for above; honouring it here takes
		// the picture off the screen and the device with it.
		ShowWindow( hWnd, SW_SHOWNORMAL );
		UpdateWindow( hWnd );
	}

	// save our application instance and window handle for future use
	ApplicationHInstance = hInstance;
	ApplicationHWnd = hWnd;
	gInitializing = false;
	if (!runWindowed) {
		gDoPaint = false;
	}

	return true;  // success

}  // end initializeAppWindows

void munkeeFunc(void);
CDAPFN_DECLARE_GLOBAL(munkeeFunc, CDAPFN_OVERHEAD_L5, CDAPFN_CONSTRAINT_NONE);
void munkeeFunc(void)
{
	CDAPFN_ENDMARK(munkeeFunc);
}

void checkProtection(void)
{
#ifdef _INTERNAL
	__try
	{
		munkeeFunc();
	}
	__except(EXCEPTION_EXECUTE_HANDLER)
	{
		exit(0); // someone is messing with us.
	}
#endif
}

// strtrim ====================================================================
/** Trim leading and trailing whitespace from a character string (in place). */
//=============================================================================
static char* strtrim(char* buffer)
{
	if (buffer != NULL) {
		//	Strip leading white space from the string.
		char * source = buffer;
		while ((*source != 0) && ((unsigned char)*source <= 32))
		{
			source++;
		}

		if (source != buffer)
		{
			strcpy(buffer, source);
		}

		//	Clip trailing white space from the string.
		for (int index = strlen(buffer)-1; index >= 0; index--)
		{
			if ((*source != 0) && ((unsigned char)buffer[index] <= 32))
			{
				buffer[index] = '\0';
			}
			else
			{
				break;
			}
		}
	}

	return buffer;
}

char *nextParam(char *newSource, char *seps)
{
	static char *source = NULL;
	if (newSource)
	{
		source = newSource;
	}
	if (!source)
	{
		return NULL;
	}

	// find first separator
	char *first = source;//strpbrk(source, seps);
	if (first)
	{
		// go past separator
		char *firstSep = strpbrk(first, seps);
		char firstChar[2] = {0,0};
		if (firstSep == first)
		{
			firstChar[0] = *first;
			while (*first == firstChar[0]) first++;
		}

		// find end
		char *end;
		if (firstChar[0])
			end = strpbrk(first, firstChar);
		else
			end = strpbrk(first, seps);

		// trim string & save next start pos
		if (end)
		{
			source = end+1;
			*end = 0;

			if (!*source)
				source = NULL;
		}
		else
		{
			source = NULL;
		}

		if (first && !*first)
			first = NULL;
	}

	return first;
}

// Necessary to allow memory managers and such to have useful critical sections
static CriticalSection critSec1, critSec2, critSec3, critSec4, critSec5;

// _set_se_translator below only covers the thread that installs it, so a fault on one
// of the engine's worker threads (WW3D's texture loader, the GameSpy threads) took the
// process down with nothing in the log at all.  The unhandled-exception filter is
// process-wide and is handed the same EXCEPTION_POINTERS, so it feeds the same dump.
//
// The dump only ever went to the log, and ReleaseCrashInfo.txt is the file the launcher sends, so a
// worker thread's fault reached nobody unless the player saved the report by hand.  ReleaseCrash
// writes that file and ends the process.  The dump's stack walk is guarded because it has already
// faulted inside this filter on a player's machine; the registers are in g_LastErrorDump before it.
static LONG WINAPI dumpUnhandledException( EXCEPTION_POINTERS *e_info )
{
	DEBUG_LOG(("Unhandled exception on thread %d\n", GetCurrentThreadId()));
	__try
	{
		DumpExceptionInfo( e_info->ExceptionRecord->ExceptionCode, e_info );
	}
	__except( EXCEPTION_EXECUTE_HANDLER )
	{
		DEBUG_LOG(("The exception dump faulted part way through\n"));
	}
	ReleaseCrash( "Uncaught exception on a worker thread" );
	return EXCEPTION_EXECUTE_HANDLER;
}

/** One argument on the end of a command line, in quotes when it holds a space. */
static void appendArgument( char *line, size_t size, const char *argument )
{
	const Bool spaced = strchr( argument, ' ' ) != NULL;
	strlcat( line, spaced ? " \"" : " ", size );
	strlcat( line, argument, size );
	if (spaced)
		strlcat( line, "\"", size );
}

/** -directorrecord films a match the director has already seen.  Before this process makes its
	window it plays the same match in a copy of itself, -headless and as fast as the machine goes,
	with -directorscout naming a file for what the copy saw: where each fight begins, when each
	superweapon fires, and the logic's CRC every 30 seconds.  Then this process plays the match for the
	camera with -directortimeline naming that file.  The same switches and the same seed play the same
	match, so a skirmish with no -seed is given one here, the same to both.  The copy is in a job that
	dies with this process, so a script that stops this one by its id stops both. */
static void runDirectorScout( int &argc, char **argv, int capacity )
{
	Bool recording = FALSE;
	Bool seeded = FALSE;
	for (int index = 1; index < argc; ++index)
	{
		if (strcasecmp( argv[index], "-directorrecord" ) == 0)
			recording = TRUE;
		if (strcasecmp( argv[index], "-seed" ) == 0 || strcasecmp( argv[index], "-replay" ) == 0)
			seeded = TRUE;
		if (strcasecmp( argv[index], "-headless" ) == 0 || strcasecmp( argv[index], "-directortimeline" ) == 0)
			return;
	}
	if (!recording)
		return;
	const int addedArguments = 4;
	if (argc + addedArguments > capacity)
	{
		DEBUG_LOG(("-directorrecord: no room on the command line for the scouting pass, the director films live\n"));
		return;
	}

	static char seedSwitch[] = "-seed";
	static char seedText[ 16 ];
	if (!seeded)
	{
		const DWORD seedRange = 1000000;	// well inside the Int -seed is read into
		snprintf( seedText, sizeof( seedText ), "%lu", GetTickCount() % seedRange );
		argv[argc++] = seedSwitch;
		argv[argc++] = seedText;
	}

	char exePath[ _MAX_PATH ];
	GetModuleFileNameA( NULL, exePath, sizeof( exePath ) );
	char logPrefix[ 32 ] = "";
	findEarlyCommandLineValue( L"-logPrefix", logPrefix, sizeof( logPrefix ) );
	char scoutPrefix[ 32 ];
	snprintf( scoutPrefix, sizeof( scoutPrefix ), "scout_%s", logPrefix );
	char tempDirectory[ _MAX_PATH ];
	GetTempPathA( sizeof( tempDirectory ), tempDirectory );
	static char timelinePath[ _MAX_PATH ];
	snprintf( timelinePath, sizeof( timelinePath ), "%szhr_directorscout_%lu.txt", tempDirectory, GetCurrentProcessId() );

	static char commandLine[ 32768 ];
	snprintf( commandLine, sizeof( commandLine ), "\"%s\"", exePath );
	for (int index = 1; index < argc; ++index)
	{
		// the copy films nothing, and its log is its own
		if (strcasecmp( argv[index], "-directorrecord" ) == 0 || strcasecmp( argv[index], "-logPrefix" ) == 0)
		{
			if (index + 1 < argc && argv[index + 1][0] != '-')
				++index;
			continue;
		}
		appendArgument( commandLine, sizeof( commandLine ), argv[index] );
	}
	// -headless, -multiInstance and -logPrefix are read word by word off the raw line, so never quoted
	strlcat( commandLine, " -observer -headless -multiInstance -logPrefix ", sizeof( commandLine ) );
	strlcat( commandLine, scoutPrefix, sizeof( commandLine ) );
	strlcat( commandLine, " -directorscout", sizeof( commandLine ) );
	appendArgument( commandLine, sizeof( commandLine ), timelinePath );

	STARTUPINFOA startup;
	memset( &startup, 0, sizeof( startup ) );
	startup.cb = sizeof( startup );
	PROCESS_INFORMATION process;
	const DWORD started = GetTickCount();
	if (!CreateProcessA( exePath, commandLine, NULL, NULL, FALSE, CREATE_SUSPENDED, NULL, NULL, &startup, &process ))
	{
		DEBUG_LOG(("-directorrecord: the scouting pass did not start, error %lu, the director films live\n", GetLastError()));
		return;
	}
	HANDLE job = CreateJobObjectA( NULL, NULL );
	JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits;
	memset( &limits, 0, sizeof( limits ) );
	limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
	SetInformationJobObject( job, JobObjectExtendedLimitInformation, &limits, sizeof( limits ) );
	if (!AssignProcessToJobObject( job, process.hProcess ))
		DEBUG_LOG(("-directorrecord: the scouting pass is not in a job, error %lu: it outlives this process if this one is stopped\n", GetLastError()));
	ResumeThread( process.hThread );
	DEBUG_LOG(("-directorrecord: scouting pass %lu started: %s\n", process.dwProcessId, commandLine));
	WaitForSingleObject( process.hProcess, INFINITE );
	DWORD exitCode = 0;
	GetExitCodeProcess( process.hProcess, &exitCode );
	CloseHandle( process.hThread );
	CloseHandle( process.hProcess );
	CloseHandle( job );
	DEBUG_LOG(("-directorrecord: scouting pass took %lu ms, exit code %lu, its log is %sDebugLogFile.txt\n",
		GetTickCount() - started, exitCode, scoutPrefix));

	static char timelineSwitch[] = "-directortimeline";
	argv[argc++] = timelineSwitch;
	argv[argc++] = timelinePath;
}

// WinMain ====================================================================
/** Application entry point */
//=============================================================================
Int APIENTRY WinMain( HINSTANCE hInstance, HINSTANCE hPrevInstance,
                      LPSTR lpCmdLine, Int nCmdShow )
{
	checkProtection();

	// The x64 C runtime answers log, exp, pow and the trig functions from an FMA3 routine on a CPU
	// that has FMA3 and a plain SSE2 one on a CPU that does not, and the two differ in the last bit.
	// Logic routes its trig through DetTrig, but the computer player's matchup score takes a log(),
	// and one bit there is a different unit bought and a network game that falls apart.  One path
	// for every machine; v1.1.4 was a 32-bit build and never had the choice.  Windows on Arm's
	// runtime has no FMA3 routine to choose: whether its libm answers that log() as x64 does is E1's
	// question, as it is off Windows (PosixMain.cpp).
#if defined(_M_X64)
	_set_FMA3_enable( 0 );
#endif

	// Without this Windows scales the whole window by the display's scaling setting, so at 125% a
	// 1920x1080 game on a 1920x1080 screen is drawn 2400x1350 and hangs off the bottom right.  The
	// game sizes everything in real pixels, which is what DPI awareness hands it.
	::SetProcessDPIAware();

	if (findEarlyCommandLineOption( L"-rk7" ) == NULL &&
			findEarlyCommandLineOption( L"-multiInstance" ) == NULL &&
			!isUnattendedProcess())
	{
		::MessageBoxA( NULL, "Please start Zero Hour Reforged from its launcher.", "Zero Hour Reforged", MB_OK | MB_ICONINFORMATION );
		return 1;
	}

#ifdef _PROFILE
  Profile::StartRange("init");
#endif

	try {

		_set_se_translator( DumpExceptionInfo ); // Hook that allows stack trace.
		SetUnhandledExceptionFilter( dumpUnhandledException ); // ...on every other thread too.
		//
		// there is something about checkin in and out the .dsp and .dsw files 
		// that blows the working directory information away on each of the 
		// developers machines so we're going to hack it for a while and set our
		// working directory to the directory with the .exe since that's not the
		// default in a DevStudio project
		//

		TheUnicodeStringCriticalSection = &critSec2;
		TheDmaCriticalSection = &critSec3;
		TheMemoryPoolCriticalSection = &critSec4;
		TheDebugLogCriticalSection = &critSec5;

		/// @todo remove this force set of working directory later
		Char buffer[ _MAX_PATH ];
		GetModuleFileName( NULL, buffer, sizeof( buffer ) );
		Char *pEnd = buffer + strlen( buffer );
		while( pEnd != buffer ) 
		{
			if( *pEnd == '\\' ) 
			{
				*pEnd = 0;
				break;
			}
			pEnd--;
		}
		::SetCurrentDirectory(buffer);


		// The window style is settled by CreateWindow below, which happens before the engine and
		// therefore before anything has read a preferences file, so the saved window mode is read
		// straight out of Options.ini here.  The command line is parsed after this and overrides it,
		// which is the same order the engine uses for every other setting.
		{
			const int savedMode = getEarlyOptionInt( "WindowMode", WINDOW_MODE_FULLSCREEN,
																							 0, WINDOW_MODE_COUNT - 1 );
			ApplicationIsBorderless = (savedMode == WINDOW_MODE_BORDERLESS);
			ApplicationIsWindowed = (savedMode != WINDOW_MODE_FULLSCREEN);
		}

		/*
		** Convert WinMain arguments to simple main argc and argv
		*/
		/*
		** EA's array held 32 and the loop stopped there without saying so, which is a switch that
		** does nothing and logs nothing: an unattended run reaches this easily, since every one of
		** them is driven entirely from the command line.  A -scenario at position 31 of 35 was
		** simply gone, and the only clue was that the log's own echo of the arguments ended in the
		** middle.  128 is past anything the scripts here build, and hitting it now says so.
		*/
		const int MAXIMUM_ARGUMENTS = 128;
		int argc = 1;
		char * argv[MAXIMUM_ARGUMENTS];
		argv[0] = NULL;

		char *token;
		token = nextParam(lpCmdLine, "\" ");
		while (argc < MAXIMUM_ARGUMENTS && token != NULL) {
			argv[argc++] = strtrim(token);
			//added a preparse step for this flag because it affects window creation style
			if (strcasecmp(token,"-win")==0)
			{
				ApplicationIsWindowed=true;
				ApplicationIsBorderless=false;	// an explicit -win beats a borderless Options.ini
			}
			if (strcasecmp(token,"-fullscreen")==0)
			{
				ApplicationIsWindowed=false;
				ApplicationIsBorderless=false;
			}
			// same reason: -borderless is borderless fullscreen - a windowed device with no caption
			// or frame, covering the display at the desktop resolution - so it implies -win.  Parsed
			// here rather than from Options.ini because the window exists long before the engine's
			// preferences do.
			if (strcasecmp(token,"-borderless")==0)
			{
				ApplicationIsWindowed=true;
				ApplicationIsBorderless=true;
			}
			// same again: -headless draws nothing, so it has no business owning the display mode.
			// The device is created windowed (parseHeadless) and dx8wrapper then shrinks this
			// window to the headless resolution - it only has to be born windowed. And since
			// nothing is ever drawn into it, it is never shown either: a batch of matches used to
			// throw a hundred little windows on the desktop and steal the focus off whatever the
			// machine was really doing.
			if (strcasecmp(token,"-headless")==0)
			{
				ApplicationIsWindowed=true;
				ApplicationIsHeadless=true;
			}
			token = nextParam(NULL, "\" ");
		}

		if (argc == MAXIMUM_ARGUMENTS && token != NULL)
		{
			DEBUG_LOG(("command line: more than %d arguments, everything from '%s' onward was "
				"dropped\n", MAXIMUM_ARGUMENTS - 1, token));
		}

		if (argc>2 && strcmp(argv[1],"-DX")==0) {
			Int i;
			DEBUG_LOG(("\n--- DX STACK DUMP\n"));
			for (i=2; i<argc; i++) {
				unsigned long long pc;
				pc = 0;
				sscanf(argv[i], "%llx",  &pc);
				char name[_MAX_PATH], file[_MAX_PATH];
				unsigned int line;
				unsigned int addr;
				GetFunctionDetails((void*)(uintptr_t)pc, name, ARRAY_SIZE(name), file, ARRAY_SIZE(file), &line, &addr);
				DEBUG_LOG(("0x%llx - %s, %s, line %d address 0x%x\n", pc, name, file, line, addr));
			}
			DEBUG_LOG(("\n--- END OF DX STACK DUMP\n"));
			return 0;
		}

		runDirectorScout( argc, argv, MAXIMUM_ARGUMENTS );

		#ifdef _DEBUG
			// Turn on Memory heap tracking
			int tmpFlag = _CrtSetDbgFlag( _CRTDBG_REPORT_FLAG );
			tmpFlag |= (_CRTDBG_LEAK_CHECK_DF|_CRTDBG_ALLOC_MEM_DF);
			tmpFlag &= ~_CRTDBG_CHECK_CRT_DF;
			_CrtSetDbgFlag( tmpFlag );
		#endif



		// install debug callbacks
	//	WWDebug_Install_Message_Handler(WWDebug_Message_Callback);
	//	WWDebug_Install_Assert_Handler(WWAssert_Callback);


// Force "splash image" to be loaded from a file, not a resource so same exe can be used in different localizations.
#if defined _DEBUG || defined _INTERNAL || defined _PROFILE

			// check both localized directory and root dir
		char filePath[_MAX_PATH];
		char *fileName = "Install_Final.bmp";
		static const char *localizedPathFormat = "Data/%s/";
		snprintf(filePath, ARRAY_SIZE(filePath), localizedPathFormat, GetRegistryLanguage().str());
		strlcat( filePath, fileName, ARRAY_SIZE(filePath) );
		FILE *fileImage = fopen(filePath, "r");
		if (fileImage) {
			fclose(fileImage);
			gLoadScreenBitmap = (HBITMAP)LoadImage(hInstance, filePath, IMAGE_BITMAP, 0, 0, LR_SHARED|LR_LOADFROMFILE);
		}
		else {
			gLoadScreenBitmap = (HBITMAP)LoadImage(hInstance, fileName, IMAGE_BITMAP, 0, 0, LR_SHARED|LR_LOADFROMFILE);
		}
#else
		
		// in release, the file only ever lives in the root dir
		gLoadScreenBitmap = (HBITMAP)LoadImage(hInstance, "Install_Final.bmp", IMAGE_BITMAP, 0, 0, LR_SHARED|LR_LOADFROMFILE);
#endif


		// register windows class and create application window
		if( initializeAppWindows( hInstance, nCmdShow, ApplicationIsWindowed) == false )
			return 0;

		if (gLoadScreenBitmap!=NULL) {
			::DeleteObject(gLoadScreenBitmap);
			gLoadScreenBitmap = NULL;
		}


		// BGC - initialize COM
	//	OleInitialize(NULL);

		// start the log
		DEBUG_INIT(DEBUG_FLAGS_DEFAULT);
		initMemoryManager();

 
		// Set up version info
		TheVersion = NEW Version;
		TheVersion->setVersion(VERSION_MAJOR, VERSION_MINOR, VERSION_BUILDNUM, VERSION_LOCALBUILDNUM,
			AsciiString(VERSION_BUILDUSER), AsciiString(VERSION_BUILDLOC),
			AsciiString(__TIME__), AsciiString(__DATE__));

#ifdef DO_COPY_PROTECTION
		if (!CopyProtect::isLauncherRunning())
		{
			DEBUG_LOG(("Launcher is not running - about to bail\n"));
			delete TheVersion;
			TheVersion = NULL;
			shutdownMemoryManager();
			DEBUG_SHUTDOWN();
			return 0;
		}
#endif


		//Create a mutex with a unique name to Generals in order to determine if
		//our app is already running.
		//WARNING: DO NOT use this number for any other application except Generals.
		/* -multiInstance lets a second copy start.  One copy at a time is right for a player and
			 wrong for a test: a network game needs two machines, and on this one that means two
			 processes, each bound to its own address out of 127.0.0.0/8. */
		const Bool oneCopyIsEnough = (findEarlyCommandLineOption( L"-multiInstance" ) == NULL);

		GeneralsMutex = CreateMutex(NULL, FALSE, GENERALS_GUID);
		if (oneCopyIsEnough && GetLastError() == ERROR_ALREADY_EXISTS)
		{
			HWND ccwindow = FindWindow(GENERALS_GUID, NULL);
			if (ccwindow)
			{
				SetForegroundWindow(ccwindow);
				ShowWindow(ccwindow, SW_RESTORE);
			}
			if (GeneralsMutex != NULL)
			{
				CloseHandle(GeneralsMutex);
				GeneralsMutex = NULL;
			}

			DEBUG_LOG(("Generals is already running...Bail!\n"));
			delete TheVersion;
			TheVersion = NULL;
			shutdownMemoryManager();
			DEBUG_SHUTDOWN();
			return 0;
		}
		DEBUG_LOG(("Create GeneralsMutex okay.\n"));

#ifdef DO_COPY_PROTECTION
		if (!CopyProtect::notifyLauncher())
		{
			DEBUG_LOG(("Could not talk to the launcher - about to bail\n"));
			delete TheVersion;
			TheVersion = NULL;
			shutdownMemoryManager();
			DEBUG_SHUTDOWN();
			return 0;
		}
#endif

		DEBUG_LOG(("CRC message is %d\n", GameMessage::MSG_LOGIC_CRC));

		// run the game main loop
		GameMain(argc, argv);

#ifdef DO_COPY_PROTECTION
		// Clean up copy protection
		CopyProtect::shutdown();
#endif

		delete TheVersion;
		TheVersion = NULL;

	#ifdef MEMORYPOOL_DEBUG
		TheMemoryPoolFactory->debugMemoryReport(REPORT_POOLINFO | REPORT_POOL_OVERFLOW | REPORT_SIMPLE_LEAKS, 0, 0);
	#endif
	#if defined(_DEBUG) || defined(_INTERNAL)
		TheMemoryPoolFactory->memoryPoolUsageReport("AAAMemStats");
	#endif

		// close the log
		shutdownMemoryManager();
		DEBUG_SHUTDOWN();

		// BGC - shut down COM
	//	OleUninitialize();
	}
	/* Whatever escaped the engine used to end in an empty catch here, and WinMain returned as if the
		 player had quit.  The CRT then ran the static destructors while WW3D's threads were still
		 running: the thread list was destroyed first, and the mouse or texture loader thread faulted
		 taking itself off it (Except.cpp, Unregister_Thread_ID), which is all a player's report ever
		 showed.  Name what was thrown, and leave through ReleaseCrash's _exit so no destructor runs. */
	catch (INIException e)
	{
		RELEASE_CRASH((e.mFailureMessage ? e.mFailureMessage : "Uncaught INI exception in WinMain"));
	}
	catch (ErrorCode ec)
	{
		char why[ 64 ];
		snprintf( why, sizeof(why), "Uncaught ErrorCode 0x%08x in WinMain", (UnsignedInt)ec );
		RELEASE_CRASH((why));
	}
	catch (...)
	{
		RELEASE_CRASH(("Uncaught exception in WinMain"));
	}

	TheUnicodeStringCriticalSection = NULL;
	TheDmaCriticalSection = NULL;
	TheMemoryPoolCriticalSection = NULL;

	return 0;

}  // end WinMain

// CreateGameEngine ===========================================================
/** Create the Win32 game engine we're going to use */
//=============================================================================
GameEngine *CreateGameEngine( void )
{
	Win32GameEngine *engine;

	engine = NEW Win32GameEngine;
	//game engine may not have existed when app got focus so make sure it
	//knows about current focus state.
	engine->setIsActive(isWinMainActive);

	return engine;

}  // end CreateGameEngine
