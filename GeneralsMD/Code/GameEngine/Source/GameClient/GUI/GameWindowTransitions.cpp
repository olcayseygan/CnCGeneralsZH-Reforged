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

// FILE: GameWindowTransitions.cpp /////////////////////////////////////////////////
//-----------------------------------------------------------------------------
//                                                                          
//                       Electronic Arts Pacific.                          
//                                                                          
//                       Confidential Information                           
//                Copyright (C) 2002 - All Rights Reserved                  
//                                                                          
//-----------------------------------------------------------------------------
//
//	created:	Dec 2002
//
//	Filename: 	GameWindowTransitions.cpp
//
//	author:		Chris Huybregts
//	
//	purpose:	
//
//-----------------------------------------------------------------------------
///////////////////////////////////////////////////////////////////////////////

//-----------------------------------------------------------------------------
// SYSTEM INCLUDES ////////////////////////////////////////////////////////////
//-----------------------------------------------------------------------------
#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine
#include "Lib/Clock.h"
#ifdef _INTERNAL
// for occasional debugging...
//#pragma optimize("", off)
//#pragma MESSAGE("************************************** WARNING, optimization disabled for debugging purposes")
#endif

//-----------------------------------------------------------------------------
// USER INCLUDES //////////////////////////////////////////////////////////////
//-----------------------------------------------------------------------------
#include "GameLogic/GameLogic.h"
#include "GameClient/GameWindowTransitions.h"
#include "GameClient/GameWindow.h"
#include "GameClient/GameWindowManager.h"
#include "GameClient/UiAnimClock.h"
//-----------------------------------------------------------------------------
// DEFINES ////////////////////////////////////////////////////////////////////
//-----------------------------------------------------------------------------
GameWindowTransitionsHandler *TheTransitionHandler = NULL;
const FieldParse GameWindowTransitionsHandler::m_gameWindowTransitionsFieldParseTable[] = 
{

	{ "Window",		GameWindowTransitionsHandler::parseWindow,	NULL, NULL	},
	{ "FireOnce",	INI::parseBool,															NULL, offsetof( TransitionGroup, m_fireOnce) 	},
	
	{ NULL,										NULL,													NULL, 0 }  // keep this last

};

void INI::parseWindowTransitions( INI* ini )
{
	AsciiString name;
	TransitionGroup *g;

	// read the name
	const char* c = ini->getNextToken();
	name.set( c );	

	// find existing item if present
	
	DEBUG_ASSERTCRASH( TheTransitionHandler, ("parseWindowTransitions: TheTransitionHandler doesn't exist yet\n") );
	if( !TheTransitionHandler )
		return;

	// If we have a previously allocated control bar, this will return a cleared out pointer to it so we
	// can overwrite it	
	g = TheTransitionHandler->getNewGroup( name );

	// sanity
	DEBUG_ASSERTCRASH( g, ("parseWindowTransitions: Unable to allocate group '%s'\n", name.str()) );

	// parse the ini definition
	ini->initFromINI( g, TheTransitionHandler->getFieldParse() );


}
//-----------------------------------------------------------------------------
// PUBLIC FUNCTIONS ///////////////////////////////////////////////////////////
//-----------------------------------------------------------------------------
Transition *getTransitionForStyle( Int style )
{
	switch (style) {
	case TRANSITION_FLASH:
		return NEW FlashTransition;
	case BUTTON_TRANSITION_FLASH:
		return NEW ButtonFlashTransition;
	case WIN_FADE_TRANSITION:
		return NEW FadeTransition;
	case WIN_SCALE_UP_TRANSITION:
		return NEW ScaleUpTransition;
	case MAINMENU_SCALE_UP_TRANSITION:
		return NEW MainMenuScaleUpTransition;
	case TEXT_TYPE_TRANSITION:
		return NEW TextTypeTransition;
	case SCREEN_FADE_TRANSITION:
		return NEW ScreenFadeTransition;
	case COUNT_UP_TRANSITION:
		return NEW CountUpTransition;
	case FULL_FADE_TRANSITION:
		return NEW FullFadeTransition;
	case TEXT_ON_FRAME_TRANSITION:
		return NEW TextOnFrameTransition;
	case REVERSE_SOUND_TRANSITION:
		return NEW ReverseSoundTransition;

	case MAINMENU_MEDIUM_SCALE_UP_TRANSITION:
		return NEW MainMenuMediumScaleUpTransition;
	case MAINMENU_SMALL_SCALE_DOWN_TRANSITION:
		return NEW MainMenuSmallScaleDownTransition;
	case CONTROL_BAR_ARROW_TRANSITION:
		return NEW ControlBarArrowTransition;
	case SCORE_SCALE_UP_TRANSITION:
		return NEW ScoreScaleUpTransition;

	default:
		DEBUG_ASSERTCRASH(FALSE, ("getTransitionForStyle:: An invalid style was passed in. Style = %d", style));
		return NULL;
	}
	return NULL;
}

TransitionWindow::TransitionWindow( void )
{
	m_currentFrameDelay = m_frameDelay = 0;
	m_style = 0;

	m_winID = NAMEKEY_INVALID;
	m_win = NULL;
	m_transition = NULL;
}

TransitionWindow::~TransitionWindow( void )
{
	m_win = NULL;
	if(m_transition)
		delete m_transition;

	m_transition = NULL;
}

Bool TransitionWindow::init( void )
{
	m_winID = TheNameKeyGenerator->nameToKey(m_winName);
	m_win		= TheWindowManager->winGetWindowFromId(NULL, m_winID);
	m_currentFrameDelay = m_frameDelay;
//	DEBUG_ASSERTCRASH( m_win, ("TransitionWindow::init Failed to find window %s", m_winName.str()));
//	if( !m_win )
//		return FALSE;
	
	if(m_transition)
		delete m_transition;

	m_transition = getTransitionForStyle( m_style );
	m_transition->init(m_win);

	return TRUE;
}

void TransitionWindow::update( Int frame )
{
	if(!m_transition || frame < m_currentFrameDelay || frame > (m_currentFrameDelay + m_transition->getFrameLength()))
		return;

	m_transition->update( frame - m_currentFrameDelay);
}

//-----------------------------------------------------------------------------
/** A menu closed while its windows were still animating freed them, and the next
	* GameWindowManager::update ran the transition on the freed window straight after the destroy
	* pass.  The transition goes with its window; the group's next init builds it again from the
	* window found by name then. */
void TransitionWindow::forgetWindow( const GameWindow *win )
{
	if( m_transition && m_transition->usesWindow( win ) )
	{
		delete m_transition;
		m_transition = NULL;
		m_win = NULL;
	}
}

Bool TransitionWindow::isFinished( void )
{
	if(m_transition)
		return m_transition->isFinished();
	return TRUE;
}

void TransitionWindow::reverse( Int totalFrames )
{
	//m_currentFrameDelay = totalFrames - (m_transition->getFrameLength() + m_frameDelay);
	if(m_transition)
		m_transition->reverse();
}

void TransitionWindow::skip( void )
{
	if(m_transition)
		m_transition->skip();
}

void TransitionWindow::draw( void )
{
	if(m_transition)
		m_transition->draw();
}

Int TransitionWindow::getTotalFrames( void )
{
	if(m_transition)
	{
		return m_frameDelay + m_transition->getFrameLength();
	}

	return m_frameDelay;
}

//-----------------------------------------------------------------------------
TransitionGroup::TransitionGroup( void )
{
	m_currentFrame = 0;
	m_directionMultiplier = 1;
	m_fireOnce = FALSE;
	m_lastStepMs = 0;
	m_stepAccumMs = 0.0f;
}

TransitionGroup::~TransitionGroup( void )
{
	TransitionWindowList::iterator it = m_transitionWindowList.begin();
	while (it != m_transitionWindowList.end())
	{
		TransitionWindow *tWin = *it;
		delete tWin;
		tWin = NULL;
		it = m_transitionWindowList.erase(it);
	}
}

void TransitionGroup::init( void )
{
	m_currentFrame = 0;
	m_directionMultiplier = 1;
	m_lastStepMs = 0;
	m_stepAccumMs = 0.0f;
	TransitionWindowList::iterator it = m_transitionWindowList.begin();
	while (it != m_transitionWindowList.end())
	{
		TransitionWindow *tWin = *it;
		tWin->init();
		it++;
	}

}

void TransitionGroup::forgetWindow( const GameWindow *win )
{
	for( TransitionWindowList::iterator it = m_transitionWindowList.begin(); it != m_transitionWindowList.end(); ++it )
		(*it)->forgetWindow( win );
}

void TransitionGroup::update( void )
{
	//
	// One frame of transition per call was right when the frame rate was capped; rendering is
	// uncapped now, so a whole menu transition would otherwise play out in a few milliseconds.
	// Step off the wall clock at the rate the .ini frame counts were authored against instead.
	//
	// `MenuTransitionSpeed` in Options.ini scales this; 100 is the authored rate.
	if (!GameClient_isUiAnimStepDue(m_lastStepMs, m_stepAccumMs, Clock_Milliseconds(),
																	GameClient_menuAnimStepsPerSec()))
		return;

	m_currentFrame += m_directionMultiplier; // we go forward or backwards depending.
	TransitionWindowList::iterator it = m_transitionWindowList.begin();
	while (it != m_transitionWindowList.end())
	{
		TransitionWindow *tWin = *it;
		tWin->update(m_currentFrame);
		it++;
	}	
}

//-----------------------------------------------------------------------------
/** Play every remaining frame now, ignoring the clock: the group lands exactly where it would have
	* ended, in either direction.  isFinished() turns true once the frame passes the group's end, so
	* this stops. */
void TransitionGroup::finish( void )
{
	while( !isFinished() )
	{
		m_currentFrame += m_directionMultiplier;
		for( TransitionWindowList::iterator it = m_transitionWindowList.begin(); it != m_transitionWindowList.end(); ++it )
			(*it)->update( m_currentFrame );
	}
}

Bool TransitionGroup::isFinished( void )
{
	// Past its last frame (or, reversed, before its first) no window takes another update, so
	// nothing left in the group can still change.  A window that has not called itself finished by
	// then never will: a button the layout lacks (ButtonCustomMission, only in the 1.04 patch's
	// MainMenu.wnd) stops a flash short of its end.  Waiting on it held the group open for good, and
	// the main menu refuses every click while a group runs.
	if (m_directionMultiplier > 0 ? m_currentFrame > getTotalFrames() : m_currentFrame < 0)
		return TRUE;

	TransitionWindowList::iterator it = m_transitionWindowList.begin();
	while (it != m_transitionWindowList.end())
	{
		TransitionWindow *tWin = *it;
		if(tWin->isFinished() == FALSE)
			return FALSE;
		it++;
	}

	return TRUE;
}

Int TransitionGroup::getTotalFrames( void )
{
	Int totalFrames = 0;
	TransitionWindowList::iterator it = m_transitionWindowList.begin();
	while (it != m_transitionWindowList.end())
	{
		TransitionWindow *tWin = *it;
		Int winFrames = tWin->getTotalFrames();
		if(winFrames > totalFrames)
			totalFrames = winFrames;
		it++;
	}
	return totalFrames;
}

void TransitionGroup::reverse( void )
{
	Int totalFrames = getTotalFrames();
	m_directionMultiplier = -1;

	TransitionWindowList::iterator it = m_transitionWindowList.begin();
	while (it != m_transitionWindowList.end())
	{
		TransitionWindow *tWin = *it;
		tWin->reverse(totalFrames);
		it++;
	}
	m_currentFrame = totalFrames;
//	m_currentFrame ++;
}

Bool TransitionGroup::isReversed( void )
{
	if(m_directionMultiplier < 0)
		return TRUE;
	return FALSE;
}

void TransitionGroup::skip ( void )
{
	TransitionWindowList::iterator it = m_transitionWindowList.begin();
	while (it != m_transitionWindowList.end())
	{
		TransitionWindow *tWin = *it;
		tWin->skip();
		it++;
	}
}

void TransitionGroup::draw ( void )
{
	TransitionWindowList::iterator it = m_transitionWindowList.begin();
	while (it != m_transitionWindowList.end())
	{
		TransitionWindow *tWin = *it;
		tWin->draw();
		it++;
	}
}

void TransitionGroup::addWindow( TransitionWindow *transWin )
{
	if(!transWin)
		return;
	m_transitionWindowList.push_back(transWin);
}

void TransitionGroup::copyWindow( const AsciiString &from, const AsciiString &to )
{
	TransitionWindowList copies;
	for( TransitionWindowList::iterator it = m_transitionWindowList.begin(); it != m_transitionWindowList.end(); ++it )
	{
		if( (*it)->m_winName.compareNoCase( from ) != 0 )
			continue;
		TransitionWindow *copy = NEW TransitionWindow;
		copy->m_winName = to;
		copy->m_frameDelay = (*it)->m_frameDelay;
		copy->m_style = (*it)->m_style;
		copies.push_back( copy );
	}
	m_transitionWindowList.splice( m_transitionWindowList.end(), copies );
}

//-----------------------------------------------------------------------------

GameWindowTransitionsHandler::GameWindowTransitionsHandler(void)
{
	m_currentGroup = NULL;
	m_pendingGroup = NULL;
	m_drawGroup = NULL;
	m_secondaryDrawGroup = NULL;

}

GameWindowTransitionsHandler::~GameWindowTransitionsHandler( void )
{
	m_currentGroup = NULL;
	m_pendingGroup = NULL;
	m_drawGroup = NULL;
	m_secondaryDrawGroup = NULL;

	TransitionGroupList::iterator it = m_transitionGroupList.begin();
	while( it != m_transitionGroupList.end() )
	{
		TransitionGroup *g = *it;
		delete g;
		it = m_transitionGroupList.erase(it);
	}
}

void GameWindowTransitionsHandler::init(void )
{
	m_currentGroup = NULL;
	m_pendingGroup = NULL;
	m_drawGroup = NULL;
	m_secondaryDrawGroup = NULL;
}

void GameWindowTransitionsHandler::load(void )
{
	INI ini;
	// Read from INI all the ControlBarSchemes
	ini.load( AsciiString( "Data\\INI\\WindowTransitions.ini" ), INI_LOAD_OVERWRITE, NULL );
	// the fork's own groups, new names only (the main menu's campaign mission panel)
	ini.load( AsciiString( "Data\\INI\\WindowTransitionsReforged.ini" ), INI_LOAD_OVERWRITE, NULL );

	// The single player menu's Direct Strike and Apocalypse buttons (Tools/mainmenu_layout.py) sit under
	// Skirmish and flash in and out with it in each of EA's eleven groups that name Skirmish; a group
	// cannot be named twice, so this is the one way in short of copying all eleven into the fork's file.
	for( TransitionGroupList::iterator it = m_transitionGroupList.begin(); it != m_transitionGroupList.end(); ++it )
	{
		(*it)->copyWindow( AsciiString( "MainMenu.wnd:ButtonSkirmish" ), AsciiString( "MainMenu.wnd:ButtonDirectStrike" ) );
		(*it)->copyWindow( AsciiString( "MainMenu.wnd:ButtonSkirmish" ), AsciiString( "MainMenu.wnd:ButtonApocalypse" ) );
	}
}

void GameWindowTransitionsHandler::reset( void )
{
	m_currentGroup = NULL;
	m_pendingGroup = NULL;
	m_drawGroup = NULL;
	m_secondaryDrawGroup = NULL;

}

void GameWindowTransitionsHandler::update( void )
{
	if(m_drawGroup != m_currentGroup)
		m_secondaryDrawGroup = m_drawGroup;
	else
		m_secondaryDrawGroup = NULL;

	m_drawGroup = m_currentGroup;
	if(m_currentGroup && !m_currentGroup->isFinished())
		m_currentGroup->update();

	if(m_currentGroup && m_currentGroup->isFinished() && m_currentGroup->isFireOnce())
	{
		m_currentGroup = NULL;
	}

	if(m_currentGroup && m_pendingGroup && m_currentGroup->isFinished())
	{
		m_currentGroup = m_pendingGroup;
		m_pendingGroup = NULL;
	}

	if(!m_currentGroup && m_pendingGroup)
	{
		m_currentGroup = m_pendingGroup;
		m_pendingGroup = NULL;
	}

	if(m_currentGroup && m_currentGroup->isFinished() && m_currentGroup->isReversed())
		m_currentGroup = NULL;
}


//-----------------------------------------------------------------------------
/** Cut the running group, and the one queued behind it, to their last frames.  The hand-offs are
	* the ones update() makes, taken without waiting a frame between them. */
void GameWindowTransitionsHandler::finish( void )
{
	while( m_currentGroup || m_pendingGroup )
	{
		if( !m_currentGroup )
		{
			m_currentGroup = m_pendingGroup;
			m_pendingGroup = NULL;
		}
		m_currentGroup->finish();
		if( m_pendingGroup )
		{
			m_currentGroup = m_pendingGroup;
			m_pendingGroup = NULL;
			continue;
		}
		if( m_currentGroup->isFireOnce() || m_currentGroup->isReversed() )
			m_currentGroup = NULL;
		break;
	}
}

void GameWindowTransitionsHandler::draw( void )
{
//	if( TheGameLogic->getFrame() > 0 )//if( areTransitionsEnabled() ) //KRIS
	if(m_drawGroup)
			m_drawGroup->draw();
	if(m_secondaryDrawGroup)
		m_secondaryDrawGroup->draw();
}

void GameWindowTransitionsHandler::setGroup(AsciiString groupName, Bool immidiate )
{
	if(groupName.isEmpty() && immidiate)
		m_currentGroup = NULL;
	if(immidiate && m_currentGroup)
	{
		m_currentGroup->skip();
		m_currentGroup = findGroup(groupName);
		if(m_currentGroup)
			m_currentGroup->init();
		return;
	}
	
	if(m_currentGroup)
	{
		if(!m_currentGroup->isFireOnce() && !m_currentGroup->isReversed())
			m_currentGroup->reverse();
		m_pendingGroup = findGroup(groupName);
		if(m_pendingGroup)
			m_pendingGroup->init();
		return;
	}

	m_currentGroup = findGroup(groupName);
	if(m_currentGroup)
		m_currentGroup->init();

	
	
}

void GameWindowTransitionsHandler::reverse( AsciiString groupName )
{
	TransitionGroup *g = findGroup(groupName);
	if(!g)
		return;	// a group no WindowTransitions.ini names; with nothing playing this was a call through NULL
	if( m_currentGroup == g )
	{
		m_currentGroup->reverse();
		return;
	}
	if( m_pendingGroup == g)
	{
		m_pendingGroup = NULL;
		return;
	}
	if(m_currentGroup)
		m_currentGroup->skip();
	if(m_pendingGroup)
		m_pendingGroup->skip();

	m_currentGroup = g;
	m_currentGroup->init();
	m_currentGroup->skip();
	m_currentGroup->reverse();
	m_pendingGroup = NULL;
}

void GameWindowTransitionsHandler::forgetWindow( const GameWindow *win )
{
	for( TransitionGroupList::iterator it = m_transitionGroupList.begin(); it != m_transitionGroupList.end(); ++it )
		(*it)->forgetWindow( win );
}

void GameWindowTransitionsHandler::remove( AsciiString groupName,  Bool skipPending )
{
	TransitionGroup *g = findGroup(groupName);
	if(!g)
		return;
	if(m_pendingGroup == g)
	{
		if(skipPending)
			m_pendingGroup->skip();

		m_pendingGroup = NULL;
	}
	if(m_currentGroup == g)
	{
		m_currentGroup->skip();
		m_currentGroup = NULL;
		if(m_pendingGroup)
			m_currentGroup = m_pendingGroup;
	}
}

TransitionGroup *GameWindowTransitionsHandler::getNewGroup( AsciiString name )
{
	if(name.isEmpty())
		return NULL;

	// test to see if we're trying to add an already exisitng group.
	if(findGroup(name))
	{
		DEBUG_ASSERTCRASH(FALSE, ("GameWindowTransitionsHandler::getNewGroup - We already have a group %s", name.str()));
		return NULL;
	}
	TransitionGroup *g = NEW TransitionGroup;
	g->setName(name);
	m_transitionGroupList.push_back(g);
	return g;
}

Bool GameWindowTransitionsHandler::isFinished( void )
{
	if(m_currentGroup)
		return m_currentGroup->isFinished();
	return TRUE;
}

//-----------------------------------------------------------------------------
// PRIVATE FUNCTIONS //////////////////////////////////////////////////////////
//-----------------------------------------------------------------------------
TransitionGroup *GameWindowTransitionsHandler::findGroup( AsciiString groupName )
{
	if(groupName.isEmpty())
		return NULL;

	TransitionGroupList::iterator it = m_transitionGroupList.begin();
	while( it != m_transitionGroupList.end() )
	{
		TransitionGroup *g = *it;
		if(groupName.compareNoCase(g->getName()) == 0)
			return g;
		it++;
	}
	return NULL;
}

void GameWindowTransitionsHandler::parseWindow( INI* ini, void *instance, void *store, const void *userData )
{
	static const FieldParse myFieldParse[] = 
		{
			{ "WinName",				INI::parseAsciiString,		NULL,									offsetof( TransitionWindow, m_winName ) },
      { "Style",					INI::parseLookupList,			TransitionStyleNames,	offsetof( TransitionWindow, m_style ) },
			{ "FrameDelay",			INI::parseInt,						NULL,									offsetof( TransitionWindow, m_frameDelay ) },
			{ NULL,							NULL,											NULL, 0 }  // keep this last
		};
	TransitionWindow *transWin = NEW TransitionWindow;
	ini->initFromINI(transWin, myFieldParse);
	((TransitionGroup*)instance)->addWindow(transWin);
}

