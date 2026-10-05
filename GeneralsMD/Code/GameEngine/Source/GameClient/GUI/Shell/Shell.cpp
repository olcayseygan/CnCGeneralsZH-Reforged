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

// FILE: Shell.cpp ////////////////////////////////////////////////////////////////////////////////
// Author: Colin Day, September 2001
// Description: Shell menu representations
///////////////////////////////////////////////////////////////////////////////////////////////////

// INCLUDES ///////////////////////////////////////////////////////////////////////////////////////
#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine
#include "Lib/Clock.h"

#include "Common/RandomValue.h"
#include "GameClient/Shell.h"
#include "GameClient/WindowLayout.h"
#include "GameClient/GameWindowManager.h"
#include "GameClient/GameWindowTransitions.h"
#include "GameClient/IMEManager.h"
#include "GameClient/AnimateWindowManager.h"
#include "GameClient/ShellMenuScheme.h"
#include "GameClient/GUICallbacks.h"
#include "GameLogic/GameLogic.h"
#include "GameNetwork/GameSpyOverlay.h"
#include "GameNetwork/GameSpy/PeerDefsImplementation.h"

#include <rts/profile.h>

// PUBLIC DATA ////////////////////////////////////////////////////////////////////////////////////
Shell *TheShell = NULL;  ///< the shell singleton definition

#ifdef _INTERNAL
// for occasional debugging...
//#pragma optimize("", off)
//#pragma MESSAGE("************************************** WARNING, optimization disabled for debugging purposes")
#endif

// PUBLIC FUNCTIONS ///////////////////////////////////////////////////////////////////////////////
//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
Shell::Shell( void )
{
	Int i;

	m_screenCount = 0;
	for( i = 0; i < MAX_SHELL_STACK; i++ )
		m_screenStack[ i ] = NULL;

	m_pendingPush = FALSE;
	m_pendingPop = FALSE;
	m_pendingPushName.set( "" );
	m_isShellActive = TRUE;
	m_shellMapOn = FALSE;
	m_background = NULL;
	m_clearBackground = FALSE;
	m_animateWindowManager = NEW AnimateWindowManager;
	m_schemeManager = NEW ShellMenuSchemeManager;
	m_saveLoadMenuLayout = NULL;
	m_popupReplayLayout = NULL;
	//Added By Sadullah Nader
	//Initializations
	m_optionsLayout = NULL;
	m_screenCount = 0;
	//

}  // end Shell

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
Shell::~Shell( void )
{
	destroyScreenStack();

	if(m_background)
	{
		m_background->destroyWindows();
		m_background->deleteInstance();
		m_background = NULL;
	}

	if(m_animateWindowManager)
		delete m_animateWindowManager;
	m_animateWindowManager = NULL;

	if(m_schemeManager)
		delete m_schemeManager;
	m_schemeManager = NULL;

	// delete the save/load menu if present
	if( m_saveLoadMenuLayout )
	{

		m_saveLoadMenuLayout->destroyWindows();
		m_saveLoadMenuLayout->deleteInstance();
		m_saveLoadMenuLayout = NULL;

	}  //end if

	// delete the replay save menu if present
	if( m_popupReplayLayout )
	{

		m_popupReplayLayout->destroyWindows();
		m_popupReplayLayout->deleteInstance();
		m_popupReplayLayout = NULL;

	}  //end if

	// delete the options menu if present.
	if (m_optionsLayout != NULL) {
		m_optionsLayout->destroyWindows();
		m_optionsLayout->deleteInstance();
		m_optionsLayout = NULL;
	}

}  // end ~Shell

//-------------------------------------------------------------------------------------------------
/** Tear every screen off the stack.  This is not a run of popImmediate(): that goes through
	* doPop(), which runs the init of whatever the pop uncovers - so emptying the stack one screen
	* at a time re-initialized every menu underneath the top one, in a shell that is being destroyed,
	* against subsystems that are already going away.  Here the screen is shut down, unlinked and
	* deleted, and nothing underneath it is woken up. */
//-------------------------------------------------------------------------------------------------
void Shell::destroyScreenStack( void )
{
	WindowLayout *screen = top();
	while( screen )
	{

		// do NOT set a pending pop, the screen is removed here and not by shutdownComplete()
		m_pendingPop = FALSE;

		Bool immediatePop = TRUE;
		screen->runShutdown( &immediatePop );

		unlinkScreen( screen );
		screen->destroyWindows();
		screen->deleteInstance();

		screen = top();

	}  // end while

	if (TheIMEManager)
		TheIMEManager->detatch();

}  // end destroyScreenStack

//-------------------------------------------------------------------------------------------------
/** Initialize the shell system */
//-------------------------------------------------------------------------------------------------
void Shell::init( void )
{
	INI ini;
	// Read from INI all the ShellMenuScheme
	ini.load( AsciiString( "Data\\INI\\Default\\ShellMenuScheme.ini" ), INI_LOAD_OVERWRITE, NULL );
	ini.load( AsciiString( "Data\\INI\\ShellMenuScheme.ini" ), INI_LOAD_OVERWRITE, NULL );
	
	if( m_schemeManager )
		m_schemeManager->init();

}  // end init

//-------------------------------------------------------------------------------------------------
/** Reset the shell system to a clean state just as though init had
	* just been called and ready to re-use */
//-------------------------------------------------------------------------------------------------
void Shell::reset( void )
{
	
	if (TheIMEManager)
		TheIMEManager->detatch();

	// pop all screens
	while( m_screenCount )
		pop();

	m_animateWindowManager->reset();

}  // end reset

//-------------------------------------------------------------------------------------------------
/** Update shell system cycle.  All windows are updated that are on the stack, starting
	* with the top layout and progressing to the bottom one */
//-------------------------------------------------------------------------------------------------
void Shell::update( void )
{
	static Int lastUpdate = Clock_Milliseconds();
	static const Int shellUpdateDelay = 30;  // try to update 30 frames a second
	Int now = Clock_Milliseconds();
	
	//
	// we keep the shell updates fixed in time so that we can write consitent animation
	// speeds during the screen update functions
	//
	if( now - lastUpdate >= ((1000.0f / shellUpdateDelay ) - 1) )
	{

		// run the updates for every window layout on the stack
		for( Int i = m_screenCount - 1; i >= 0; i-- )
		{

			DEBUG_ASSERTCRASH( m_screenStack[ i ], ("Top of shell stack is NULL!\n") );
			m_screenStack[ i ]->runUpdate( NULL );

		}  // end for i
		if(TheGlobalData->m_shellMapOn && m_shellMapOn &&m_background)
		{
			
			m_background->destroyWindows();
			m_background->deleteInstance();
			m_background = NULL;
			
		}
		
		// Update the animate window manager
		m_animateWindowManager->update();

		m_schemeManager->update();

		// mark last time we ran the updates
		lastUpdate = now;

	}  // end if

}  // end update

//-------------------------------------------------------------------------------------------------
/** Find a screen via the .wnd script filename loaded */
//-------------------------------------------------------------------------------------------------
WindowLayout *Shell::findScreenByFilename( AsciiString filename )
{

	if (filename.isEmpty())
		return NULL;

	// search screen list
	WindowLayout *screen;
	Int i;
	for( i = 0; i < MAX_SHELL_STACK; i++ )
	{

		screen = m_screenStack[ i ];
		if( screen && filename.compareNoCase(screen->getFilename()) == 0 )
			return screen;

	}  // end for i

	return NULL;

}  // end findScreenByFilename

//-------------------------------------------------------------------------------------------------
/** Hide or unhide all window layouts loaded */
//-------------------------------------------------------------------------------------------------
void Shell::hide( Bool hide )
{
	Int i;

	for( i = 0; i < MAX_SHELL_STACK; i++ )
		if( m_screenStack[ i ] )
			m_screenStack[ i ]->hide( hide );

	if (TheIMEManager)
		TheIMEManager->detatch();

}  // end hide

//-------------------------------------------------------------------------------------------------
/** Push layout onto shell */
//-------------------------------------------------------------------------------------------------
void Shell::push( AsciiString filename, Bool shutdownImmediate )
{

	// sanity
	if( filename.isEmpty() )
		return;

	//
	// A push already waiting on the outgoing screen's shutdown animation owns this transition. The
	// button that started it stays live while that animation plays, so a second click ran the same
	// screen's shutdown a second time, and shutdownComplete then fired twice for one push: the
	// screen opened as two separate layouts, the extra copy stayed drawn over whatever came next,
	// and clicking that copy drove a layout that was no longer the top of the stack. Let a later
	// call change where we are going - that is what overwriting the name always did - but never
	// start the outgoing screen's shutdown twice.
	//
	if( m_pendingPush )
	{
		m_pendingPushName = filename;
		return;
	}

	if(TheGameSpyInfo)
			GameSpyCloseAllOverlays();


#ifdef DEBUG_LOGGING
	DEBUG_LOG(("Shell:push(%s) - stack was\n", filename.str()));
	for (Int i=0; i<m_screenCount; ++i)
	{
		DEBUG_LOG(("\t\t%s\n", m_screenStack[i]->getFilename().str()));
	}
#endif

	// make sure we have an available spot for another screen
	if( m_screenCount >= MAX_SHELL_STACK )
	{
	
		DEBUG_LOG(( "Unable to load screen '%s', max '%d' reached\n",
								filename.str(), MAX_SHELL_STACK ));
		return;

	}  // end if

	// set a push as pending with the layout name passed in
	m_pendingPush = TRUE;
	m_pendingPushName = filename;

	// get the top of the current stack
	WindowLayout *currentTop = top();

	//
	// if we have someting on the top of the stack we won't do the push
	// right now, we will instead shutdown the top, and when the top tells
	// us it's done shutting down (via the shutdownComplete() method) we do
	// the push then
	//
	if( currentTop && !currentTop->isHidden() )
	{

		// run the shutdown
		currentTop->runShutdown( &shutdownImmediate );

	}  // end if
	else
	{

		// just call shutdownComplete() which will immediately cause the push to happen
		shutdownComplete( NULL );

	}  // end else

//	if (TheIMEManager)
//		TheIMEManager->detatch();

}  // end push

//-------------------------------------------------------------------------------------------------
/** Pop top layout of the stack.  Note that we don't actually do the pop right here,
	* we instead run the layout shutdown.  That shutdown() in turn notifies the
	* shell when the shutdown is complete and at that point we do the actual pop */
//-------------------------------------------------------------------------------------------------
void Shell::pop( void )
{
	WindowLayout *screen = top();
	if(TheGameSpyInfo)
			GameSpyCloseAllOverlays();


	// sanity
	if( screen == NULL )
		return;

#ifdef DEBUG_LOGGING
	DEBUG_LOG(("Shell:pop() - stack was\n"));
	for (Int i=0; i<m_screenCount; ++i)
	{
		DEBUG_LOG(("\t\t%s\n", m_screenStack[i]->getFilename().str()));
	}
#endif

	// set a pop as pending
	m_pendingPop = TRUE;

	//
	// run the shutdown function for the screen, when it's actually shutdown it
	// will call Shell::shutdownComplete(), where the pending pop will be seen
	// and the actual pop will occur
	//
	Bool immediatePop = FALSE;
	screen->runShutdown( &immediatePop );

	if (TheIMEManager)
		TheIMEManager->detatch();

}  // end pop

//-------------------------------------------------------------------------------------------------
/** When you need to immediately pop a screen off the stack use this method.  It
	* gives the screen the opportunity to shutdown and tells the shutdown
	* method that an immediate pop is going to take place.  When control returns
	* from the shutdown() for the screen, it will be immediately popped off
	* the stack */
//-------------------------------------------------------------------------------------------------
void Shell::popImmediate( void )
{
	if( !shutdownTopForImmediatePop() )
		return;

	// pop the screen of the stack
	doPop( FALSE );

	if (TheIMEManager)
		TheIMEManager->detatch();

}  // end popImmediate

//-------------------------------------------------------------------------------------------------
/** The immediate pop for a game that is about to load.  popImmediate runs the init of whatever the
	* pop uncovers, and between two campaign missions that is the main menu: its init shows the
	* buttons, brings the layout forward and queues the shell map as a new game ahead of the next
	* mission, so the menu came up over the load.  Here the uncovered screen is left as it was, and
	* showShell wakes it when the game ends and the shell comes back. */
//-------------------------------------------------------------------------------------------------
void Shell::popImmediateIntoGame( void )
{
	if( !shutdownTopForImmediatePop() )
		return;

	doPop( TRUE );

}  // end popImmediateIntoGame

//-------------------------------------------------------------------------------------------------
Bool Shell::shutdownTopForImmediatePop( void )
{
	WindowLayout *screen = top();

	// sanity
	if( screen == NULL )
		return FALSE;

#ifdef DEBUG_LOGGING
	DEBUG_LOG(("Shell:popImmediate() - stack was\n"));
	for (Int i=0; i<m_screenCount; ++i)
	{
		DEBUG_LOG(("\t\t%s\n", m_screenStack[i]->getFilename().str()));
	}
#endif

	// do NOT set pending pop, we are going to force a pop after the shutdown is run
	m_pendingPop = FALSE;

	// run the shutdown
	Bool immediatePop = TRUE;
	screen->runShutdown( &immediatePop );
	return TRUE;

}  // end shutdownTopForImmediatePop

//-------------------------------------------------------------------------------------------------
/** Run the initialize function for the top of the stack just as though it was pushed
	* on the stack.  We want this behavior when we want to act like the top was just
	* pushed on the stack, but it's already there (ie going from in game back to the
	* pre-game shell menus */
//-------------------------------------------------------------------------------------------------
void Shell::showShell( Bool runInit )
{
	DEBUG_LOG(("Shell:showShell() - %s (%s)\n", TheGlobalData->m_initialFile.str(), (top())?top()->getFilename().str():"no top screen"));

	if(!TheGlobalData->m_initialFile.isEmpty())
	{
		return;
	}
	
	// runInit is used if we want show shell to run 
	if(runInit)
	{
		WindowLayout *layout = top();

		if( layout )
		{
			layout->runInit( NULL );
		//	layout->bringForward();
		}
	}
	// @todo remove this hack
//	TheGlobalData->m_inGame = FALSE;
	// add in the background stuff

//	if(TheGlobalData->m_shellMapOn)
	//	{
	//		if( top() )
	//			top()->hide(TRUE);
	//		m_background = TheWindowManager->winCreateLayout("Menus/BlankWindow.wnd");
	//		DEBUG_ASSERTCRASH(m_background,("We Couldn't Load Menus/BlankWindow.wnd"));
	//		m_background->hide(FALSE);
	//		m_background->bringForward();
	//		if (TheGameLogic->isInGame())
	//				TheMessageStream->appendMessage( GameMessage::MSG_CLEAR_GAME_DATA );
	//
	//		TheGlobalData->m_pendingFile = TheGlobalData->m_shellMapName;
	//		GameMessage *msg = TheMessageStream->appendMessage( GameMessage::MSG_NEW_GAME );
	//		msg->appendIntegerArgument(GAME_SHELL);
	//	}
	//	else
	//	{
	//		
	//		m_background = TheWindowManager->winCreateLayout("Menus/BlankWindow.wnd");
	//		
	//		DEBUG_ASSERTCRASH(m_background,("We Couldn't Load Menus/BlankWindow.wnd"));
	//		m_background->hide(FALSE);
	//		if (top())
	//			top()->bringForward();
	//		
	//	}
	

	if (!TheGlobalData->m_shellMapOn && m_screenCount == 0)
  {
#ifdef _PROFILE
    Profile::StopRange("init");
#endif
	//else
		TheShell->push( AsciiString("Menus/MainMenu.wnd") );
  }

	/* -lanlobby: go straight to the LAN screen, on top of the main menu that was just pushed, which
	   is what clicking Multiplayer would have done.  The flag is spent here rather than read again,
	   because showShell() also runs on the way back out of a game and the switch is about how this
	   copy of the game starts, not where it lands every time. */
	if( TheGlobalData->m_lanLobbyOnStart )
	{
		TheWritableGlobalData->m_lanLobbyOnStart = FALSE;
		TheShell->push( AsciiString("Menus/LanLobbyMenu.wnd") );
	}

	// -skirmishlobby: the same trick for the single-player staging room, spent the same way, and
	// -apocalypselobby: that room as the main menu's Apocalypse button opens it.
	if( TheGlobalData->m_skirmishLobbyOnStart || TheGlobalData->m_apocalypseLobby )
	{
		SkirmishApocalypse = TheGlobalData->m_apocalypseLobby;
		TheWritableGlobalData->m_skirmishLobbyOnStart = FALSE;
		TheWritableGlobalData->m_apocalypseLobby = FALSE;
		TheShell->push( AsciiString("Menus/SkirmishGameOptionsMenu.wnd") );
	}

	// -optionsmenu: the options layout over the main menu, opened the way the main menu's button opens it
	if( TheGlobalData->m_optionsMenuOnStart )
	{
		TheWritableGlobalData->m_optionsMenuOnStart = FALSE;
		WindowLayout *options = getOptionsLayout( TRUE );
		options->runInit();
		options->hide( FALSE );
		options->bringForward();
	}

	m_isShellActive = TRUE;
}  // end showShell

void Shell::showShellMap(Bool useShellMap )
{
	// we don't want any of this to show if we're loading straight into a file
	if(TheGlobalData->m_initialFile.isNotEmpty() || !TheGameLogic )
		return;
	if(useShellMap && TheGlobalData->m_shellMapOn)
	{
		// we're already in a shell game, return
		if(TheGameLogic->isInGame() && TheGameLogic->getGameMode() == GAME_SHELL)
			return;
		// we're in some other kind of game, clear it out foo!
		if(TheGameLogic->isInGame())
			TheMessageStream->appendMessage( GameMessage::MSG_CLEAR_GAME_DATA );

		TheWritableGlobalData->m_pendingFile = TheGlobalData->m_shellMapName;
		InitRandom(0);
		GameMessage *msg = TheMessageStream->appendMessage( GameMessage::MSG_NEW_GAME );
		msg->appendIntegerArgument(GAME_SHELL);
		m_shellMapOn = TRUE;
	}
	else
	{
		// we're in a shell game, stop it!
		if(TheGameLogic->isInGame() && TheGameLogic->getGameMode() == GAME_SHELL)
			TheMessageStream->appendMessage( GameMessage::MSG_CLEAR_GAME_DATA );

		// if the shell is active,we need a background
		if(!m_isShellActive)
			return;
		if(!m_background)
			m_background = TheWindowManager->winCreateLayout("Menus/BlankWindow.wnd");
		
		DEBUG_ASSERTCRASH(m_background,("We Couldn't Load Menus/BlankWindow.wnd"));
		m_background->getFirstWindow()->winSetStatus(WIN_STATUS_IMAGE);
		m_background->hide(FALSE);
		if (top())
			top()->bringForward();
		m_shellMapOn = FALSE;
		m_clearBackground = FALSE;
	}
}

//-------------------------------------------------------------------------------------------------
/** Run the shutdown() function for the top of the stack just like we're going to pop
	* it off but DO NOT pop it off the stack.  We want this behavior when leaving the
	* pre-game menus and entering the game and want the shell to still exist and contain
	* the stack information but don't want it to go away */
//-------------------------------------------------------------------------------------------------
void Shell::hideShell( void )
{
	// If we have the 3d background running, mark it to close
	m_clearBackground = TRUE;

	DEBUG_LOG(("Shell:hideShell() - %s\n", (top())?top()->getFilename().str():"no top screen"));

	WindowLayout *layout = top();

	if( layout )
	{
		Bool immediatePop = TRUE;

		layout->runShutdown( &immediatePop );

	}  // end if

	if (TheIMEManager)
		TheIMEManager->detatch();

	// Mark that the shell is no longer up.
	m_isShellActive = FALSE;

}  // end hideShell

//-------------------------------------------------------------------------------------------------
/** Return the top layout on the stack */
//-------------------------------------------------------------------------------------------------
WindowLayout *Shell::top( void )
{

	// emtpy stack
	if( m_screenCount == 0 )
		return NULL;

	// top layout is at count index
	return m_screenStack[ m_screenCount - 1 ];

}  // end top

// PRIVATE FUNCTIONS //////////////////////////////////////////////////////////////////////////////
//-------------------------------------------------------------------------------------------------
/** Add screen to our list */
//-------------------------------------------------------------------------------------------------
void Shell::linkScreen( WindowLayout *screen )
{

	// sanity
	if( screen == NULL )
		return;

	// check to see if at top already
	if( m_screenCount == MAX_SHELL_STACK )
	{

		DEBUG_CRASH(( "No room in shell stack for screen\n" ));
		return;

	}  // end if

	// add to array at top index
	m_screenStack[ m_screenCount++ ] = screen;

}  // end linkScreen

//-------------------------------------------------------------------------------------------------
/** Remove screen from our list */
//-------------------------------------------------------------------------------------------------
void Shell::unlinkScreen( WindowLayout *screen )
{
	
	// sanity
	if( screen == NULL )
		return;

	DEBUG_ASSERTCRASH( m_screenStack[ m_screenCount - 1 ] == screen, 
										 ("Screen not on top of stack\n") );

	// remove reference to screen and decrease count
	if( m_screenStack[ m_screenCount - 1 ] == screen )
		m_screenStack[ --m_screenCount ] = NULL;

}  // end unlinkScreen

//-------------------------------------------------------------------------------------------------
/** Actually do the work for a push */
//-------------------------------------------------------------------------------------------------
void Shell::doPush( AsciiString layoutFile )
{
	if(TheGameSpyInfo)
			GameSpyCloseAllOverlays();
	WindowLayout *newScreen;
	
	// create new layout and load from window manager
	newScreen = TheWindowManager->winCreateLayout( layoutFile );
	DEBUG_ASSERTCRASH( newScreen != NULL, ("Shell unable to load pending push layout\n") );

	// link screen to the top
	linkScreen( newScreen );

	if (TheIMEManager)
		TheIMEManager->detatch();

	// run the init function automatically
	newScreen->runInit( NULL );
	newScreen->bringForward();


}  // end doPush

//-------------------------------------------------------------------------------------------------
/** Actually do the work for a pop */
//-------------------------------------------------------------------------------------------------
void Shell::doPop( Bool impendingPush )
{
	WindowLayout *currentTop = top();

	// there better be a top of the stack since we're popping
	DEBUG_ASSERTCRASH( currentTop, ("Shell: No top of stack and we want to pop!\n") );
		
	// remove this screen from our list
	unlinkScreen( currentTop );

	// delete all the windows in the screen
	currentTop->destroyWindows();

	// release the screen object back to the memory pool
	currentTop->deleteInstance();

	// run the init for the new top of the stack if present
	WindowLayout *newTop = top();
	if( newTop && !impendingPush )
	{
		newTop->runInit( NULL );
		//newTop->bringForward();
	}

	if (TheIMEManager)
		TheIMEManager->detatch();

}  // end doPop

//-------------------------------------------------------------------------------------------------
/** This is called when a layout has finished its shutdown process.  Layouts are
	* shutdown when a new screen is being pushed on the stack, or when we are
	* popping the current screen off the top of the stack.  It is here that we
	* can look for any pending push or pop operations and actually do them
	*
	* NOTE: It is possible for the screen parameter to be NULL when we are
	*       short circuiting the shutdown logic because there is no layout
	*				to actually shutdown (ie, the stack is empty and we push) */
//-------------------------------------------------------------------------------------------------
void Shell::shutdownComplete( WindowLayout *screen, Bool impendingPush )
{

	// there should never be a pending push AND pop operation
	DEBUG_ASSERTCRASH( m_pendingPush == FALSE || m_pendingPop == FALSE,
										 ("There is a pending push AND pop in the shell.  Not allowed!\n") );	

	// Reset the AnimateWindowManager
	m_animateWindowManager->reset();

	// check for pending push or pop
	if( m_pendingPush )
	{

		// do the push
		doPush( m_pendingPushName );

		// no more pending pushy for you!
		m_pendingPush = FALSE;
		m_pendingPushName.set( "" );

	}  // end if
	else if( m_pendingPop )
	{

		// do the pop
		doPop( impendingPush );
				
		// no more pending pop for you!
		m_pendingPop = FALSE;

	}  // end else if
	
	if(m_clearBackground)
	{
		if(m_background)
		{
			m_background->destroyWindows();
			m_background->deleteInstance();
			m_background = NULL;
			m_clearBackground = FALSE;
		}
			
	}

}  // end shutdownComplete


void Shell::registerWithAnimateManager( GameWindow *win, AnimTypes animType, Bool needsToFinish, UnsignedInt delayMS)
{
	if(!m_animateWindowManager)
	{
		DEBUG_CRASH(("We called registerWithAnimateManager and we don't have an Animate Manager created"));
		return;
	}
	if (TheGlobalData->m_animateWindows)
		m_animateWindowManager->registerGameWindow(win,animType,needsToFinish, 500,delayMS);
}

Bool Shell::isAnimFinished( void )
{
	// check the new way also.
	if (!TheTransitionHandler->isFinished())
		return FALSE;

	if(!m_animateWindowManager)
	{
		DEBUG_CRASH(("We called registerWithAnimateManager and we don't have an Animate Manager created"));
		return TRUE;
	}
	if (TheGlobalData->m_animateWindows)
		return m_animateWindowManager->isFinished();
	else
		return TRUE;
}

void Shell::reverseAnimatewindow( void )
{
	if(!m_animateWindowManager)
	{
		DEBUG_CRASH(("We called registerWithAnimateManager and we don't have an Animate Manager created"));
		return;
	}
	if (TheGlobalData->m_animateWindows)
		m_animateWindowManager->reverseAnimateWindow();
}

Bool Shell::isAnimReversed( void )
{
	if(!m_animateWindowManager)
	{
		DEBUG_CRASH(("We called registerWithAnimateManager and we don't have an Animate Manager created"));
		return TRUE;
	}
	if (TheGlobalData->m_animateWindows)
		return m_animateWindowManager->isReversed();
	else
		return TRUE;
}


void Shell::loadScheme( AsciiString name )
{
	if(!m_schemeManager)
		return;

	m_schemeManager->setShellMenuScheme( name );
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
WindowLayout *Shell::getSaveLoadMenuLayout( void )
{
	
	// if layout has not been created, create it now
	if( m_saveLoadMenuLayout == NULL )
   m_saveLoadMenuLayout = TheWindowManager->winCreateLayout( AsciiString( "Menus/PopupSaveLoad.wnd" ) );

	// sanity
	DEBUG_ASSERTCRASH( m_saveLoadMenuLayout, ("Unable to create save/load menu layout\n") );

	// return the layout
	return m_saveLoadMenuLayout;

}  // end getSaveLoadMenuLayout

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
WindowLayout *Shell::getPopupReplayLayout( void )
{
	
	// if layout has not been created, create it now
	if( m_popupReplayLayout == NULL )
   m_popupReplayLayout = TheWindowManager->winCreateLayout( AsciiString( "Menus/PopupReplay.wnd" ) );

	// sanity
	DEBUG_ASSERTCRASH( m_popupReplayLayout, ("Unable to create replay save menu layout\n") );

	// return the layout
	return m_popupReplayLayout;

}  // end getSaveLoadMenuLayout

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
WindowLayout *Shell::getOptionsLayout( Bool create )
{
	// if layout has not been created, create it now
	if ((m_optionsLayout == NULL) && (create == TRUE))
	{
		m_optionsLayout = TheWindowManager->winCreateLayout( AsciiString( "Menus/OptionsMenu.wnd" ) );

		// sanity
		DEBUG_ASSERTCRASH( m_optionsLayout, ("Unable to create options menu layout\n") );
	}

	// return the layout
	return m_optionsLayout;
} // end getOptionsLayout

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void Shell::destroyOptionsLayout() {
	if (m_optionsLayout != NULL) {
		m_optionsLayout->destroyWindows();
		m_optionsLayout->deleteInstance();
		m_optionsLayout = NULL;
	}
}
