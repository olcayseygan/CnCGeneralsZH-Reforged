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
// Modified 2026 by Olcay Seygan for Zero Hour Reforged; see the git history.

////////////////////////////////////////////////////////////////////////////////
//																																						//
//  (c) 2001-2003 Electronic Arts Inc.																				//
//																																						//
////////////////////////////////////////////////////////////////////////////////

// FILE: CreditsMenu.cpp /////////////////////////////////////////////////
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
//	Filename: 	CreditsMenu.cpp
//
//	author:		Chris Huybregts
//	
//	purpose:	The credits screen...yay
//
//-----------------------------------------------------------------------------
///////////////////////////////////////////////////////////////////////////////

//-----------------------------------------------------------------------------
// SYSTEM INCLUDES ////////////////////////////////////////////////////////////
//-----------------------------------------------------------------------------
#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine
#ifdef _INTERNAL
// for occasional debugging...
//#pragma optimize("", off)
//#pragma message("************************************** WARNING, optimization disabled for debugging purposes")
#endif


//-----------------------------------------------------------------------------
// USER INCLUDES //////////////////////////////////////////////////////////////
//-----------------------------------------------------------------------------
#include "Common/GameAudio.h"
#include "Common/AudioEventRTS.h"
#include "Common/AudioHandleSpecialValues.h"

#include "GameClient/Credits.h"
#include "GameClient/WindowLayout.h"
#include "GameClient/Gadget.h"
#include "GameClient/Shell.h"
#include "GameClient/KeyDefs.h"
#include "GameClient/GameWindowManager.h"

#include "GameClient/Display.h"
#include "GameClient/DisplayString.h"
#include "GameClient/DisplayStringManager.h"
#include "GameClient/GadgetPushButton.h"
#include "GameClient/GameText.h"
#include "GameClient/UIStyle.h"
#include "Common/version.h"

//-----------------------------------------------------------------------------
// DEFINES ////////////////////////////////////////////////////////////////////
//-----------------------------------------------------------------------------
static NameKeyType parentMainMenuID = NAMEKEY_INVALID;

// window pointers --------------------------------------------------------------------------------
static GameWindow *parentMainMenu = NULL;
static GameWindow *buttonBack = NULL;

/* The credits in the menu language (docs/plans/MENU-REDESIGN.md, package 4): an 800 px column of
	 glass in the board's middle with frame A round it, over the shell map stepped back.  The title and
	 the version on its head, the roll between the head's rule and the foot's (CreditsManager draws it
	 there and fades it at both ends), Back at the foot's right with its Esc hint at the left.  Every
	 number at 1080 (experiments/proofs/2026-11-24/screens/main-menu/README-main-menu.md). */
enum
{
	CREDITS_LEFT = 560, CREDITS_TOP = 28, CREDITS_RIGHT = 1360, CREDITS_BOTTOM = 1052,
	CREDITS_PAD = 32, CREDITS_HEAD = 82, CREDITS_FOOT = 84,
	CREDITS_BACK_WIDTH = 220, CREDITS_BACK_TOP = 68, CREDITS_BACK_BOTTOM = 20,
};

static DisplayString *creditsTitle = NULL;
static DisplayString *creditsVersion = NULL;
static DisplayString *creditsHint = NULL;

static DisplayString *creditsText( DisplayString *&words, const UnicodeString &text, GameFont *font )
{
	if( words == NULL )
		words = TheDisplayStringManager->newDisplayString();
	if( words->getFont() != font )
		words->setFont( font );
	if( words->getText().compare( text ) != 0 )
		words->setText( text );
	return words;
}

static void freeCreditsText( DisplayString *&words )
{
	if( words )
		TheDisplayStringManager->freeDisplayString( words );
	words = NULL;
}

/// the screen's parent: EA filled it black over the shell map
static void drawCreditsNothing( GameWindow *, WinInstanceData * )
{
}

/// the column, drawn by the background window under the roll
static void drawCreditsColumn( GameWindow *, WinInstanceData * )
{
	using namespace UIStyle;
	veil( 90 );
	const IRegion2D column = boardRect( CREDITS_LEFT, CREDITS_TOP, CREDITS_RIGHT, CREDITS_BOTTOM );
	glass( column, 168 );
	frameA( column );

	DisplayString *title = creditsText( creditsTitle, capitals( TheGameText->fetch( "GUI:Credits" ) ), menuFont( MENU_TEXT_BOX_TITLE, TRUE ) );
	const IRegion2D head = boardRect( CREDITS_LEFT + CREDITS_PAD, CREDITS_TOP + 24, CREDITS_RIGHT - CREDITS_PAD, CREDITS_TOP + 64 );
	Int w, h;
	title->getSize( &w, &h );
	title->draw( head.lo.x, ( head.lo.y + head.hi.y - h ) / 2, INK, GameMakeColor( 0, 0, 0, 160 ) );
	UnicodeString version;
	if( TheVersion )
		version.translate( TheVersion->getReleaseVersion() );
	DisplayString *mark = creditsText( creditsVersion, version, menuFont( MENU_TEXT_LABEL ) );
	mark->getSize( &w, &h );
	mark->draw( head.hi.x - w, ( head.lo.y + head.hi.y - h ) / 2, MUTED, GameMakeColor( 0, 0, 0, 160 ) );
	const IRegion2D headRule = boardRect( CREDITS_LEFT + 2, CREDITS_TOP + CREDITS_HEAD - 2, CREDITS_RIGHT - 2, CREDITS_TOP + CREDITS_HEAD );
	litLine( headRule.lo.x, headRule.lo.y, headRule.hi.x - headRule.lo.x, max( 1, headRule.hi.y - headRule.lo.y ), FRAME );

	const IRegion2D footRule = boardRect( CREDITS_LEFT + CREDITS_PAD, CREDITS_BOTTOM - CREDITS_FOOT, CREDITS_RIGHT - CREDITS_PAD, CREDITS_BOTTOM - CREDITS_FOOT );
	TheDisplay->drawFillRect( footRule.lo.x, footRule.lo.y, footRule.hi.x - footRule.lo.x, 1, withAlpha( FRAME, 110 ) );
	DisplayString *hint = creditsText( creditsHint, TheGameText->fetch( "GUI:HintEscBack" ), menuFont( MENU_TEXT_HINT ) );
	const IRegion2D foot = boardRect( CREDITS_LEFT + CREDITS_PAD, CREDITS_BOTTOM - CREDITS_BACK_TOP, CREDITS_RIGHT, CREDITS_BOTTOM - CREDITS_BACK_BOTTOM );
	hint->getSize( &w, &h );
	hint->draw( foot.lo.x, ( foot.lo.y + foot.hi.y - h ) / 2, MUTED, GameMakeColor( 0, 0, 0, 160 ) );
}

//-----------------------------------------------------------------------------
// PUBLIC FUNCTIONS ///////////////////////////////////////////////////////////
//-----------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
/** Initialize the single player menu */
//-------------------------------------------------------------------------------------------------
void CreditsMenuInit( WindowLayout *layout, void *userData )
{
	// the shell map plays on behind the column, stepped back; EA stopped it for a black screen
	if(TheCredits)
		delete TheCredits;
	TheCredits = new CreditsManager;
	TheCredits->load();
	TheCredits->init();
	TheCredits->setRegion( UIStyle::boardRect( CREDITS_LEFT + 2, CREDITS_TOP + CREDITS_HEAD, CREDITS_RIGHT - 2, CREDITS_BOTTOM - CREDITS_FOOT ) );

	parentMainMenuID = TheNameKeyGenerator->nameToKey( AsciiString("CreditsMenu.wnd:ParentCreditsWindow") );
	parentMainMenu = TheWindowManager->winGetWindowFromId( NULL, parentMainMenuID );
	GameWindow *background = TheWindowManager->winGetWindowFromId( parentMainMenu, TheNameKeyGenerator->nameToKey( "CreditsMenu.wnd:BackgroundWindow" ) );
	if( background )
		background->winSetDrawFunc( drawCreditsColumn );
	if( parentMainMenu )
		parentMainMenu->winSetDrawFunc( drawCreditsNothing );
	buttonBack = TheWindowManager->winGetWindowFromId( parentMainMenu, TheNameKeyGenerator->nameToKey( "CreditsMenu.wnd:ButtonBack" ) );
	if( buttonBack )
	{
		const IRegion2D key = UIStyle::boardRect( CREDITS_RIGHT - CREDITS_PAD - CREDITS_BACK_WIDTH, CREDITS_BOTTOM - CREDITS_BACK_TOP,
			CREDITS_RIGHT - CREDITS_PAD, CREDITS_BOTTOM - CREDITS_BACK_BOTTOM );
		buttonBack->winSetPosition( key.lo.x, key.lo.y );
		buttonBack->winSetSize( key.hi.x - key.lo.x, key.hi.y - key.lo.y );
		buttonBack->winSetTooltip( UnicodeString::TheEmptyString );
		GadgetButtonSetText( buttonBack, UIStyle::capitals( TheGameText->fetch( "GUI:Back" ) ) );
		UIStyle::setMenuKey( buttonBack, TRUE );
	}


	// show menu
	layout->hide( FALSE );

	// set keyboard focus to main parent
	TheWindowManager->winSetFocus( parentMainMenu );



	TheAudio->removeAudioEvent( AHSV_StopTheMusicFade );
	AudioEventRTS event( AsciiString( "Credits" ) );
	event.setShouldFade( TRUE );
	TheAudio->addAudioEvent( &event );


}  // end CreditsMenuInit

//-------------------------------------------------------------------------------------------------
/** single player menu shutdown method */
//-------------------------------------------------------------------------------------------------
void CreditsMenuShutdown( WindowLayout *layout, void *userData )
{
	TheCredits->reset();
	delete TheCredits;
	TheCredits = NULL;
	freeCreditsText( creditsTitle );
	freeCreditsText( creditsVersion );
	freeCreditsText( creditsHint );
	TheShell->showShellMap(TRUE);

	// hide menu
	layout->hide( TRUE );

	// our shutdown is complete
	TheShell->shutdownComplete( layout );

	TheAudio->removeAudioEvent( AHSV_StopTheMusicFade );

}  // end CreditsMenuShutdown

//-------------------------------------------------------------------------------------------------
/** single player menu update method */
//-------------------------------------------------------------------------------------------------
void CreditsMenuUpdate( WindowLayout *layout, void *userData )
{

	if(TheCredits)
	{
		TheWindowManager->winSetFocus( parentMainMenu );
		TheCredits->update();
		if(TheCredits->isFinished())
			TheShell->pop();
	}
	else
		TheShell->pop();

}  // end CreditsMenuUpdate

//-------------------------------------------------------------------------------------------------
/** Replay menu input callback */
//-------------------------------------------------------------------------------------------------
WindowMsgHandledType CreditsMenuInput( GameWindow *window, UnsignedInt msg,
																						WindowMsgData mData1, WindowMsgData mData2 )
{

	switch( msg ) 
	{

		// --------------------------------------------------------------------------------------------
		case GWM_CHAR:
		{
			UnsignedByte key = mData1;
			UnsignedByte state = mData2;

			switch( key )
			{

				// ----------------------------------------------------------------------------------------
				case KEY_ESC:
				{
					
					//
					// send a simulated selected event to the parent window of the
					// back/exit button
					//
					if( BitTest( state, KEY_STATE_UP ) )
					{

						TheShell->pop();

					}  // end if

					// don't let key fall through anywhere else
					return MSG_HANDLED;

				}  // end escape

			}  // end switch( key )

		}  // end char

	}  // end switch( msg )

	return MSG_IGNORED;

}  // end CreditsMenuInput

//-------------------------------------------------------------------------------------------------
/** single player menu window system callback */
//-------------------------------------------------------------------------------------------------
WindowMsgHandledType CreditsMenuSystem( GameWindow *window, UnsignedInt msg, 
														 WindowMsgData mData1, WindowMsgData mData2 )
{
	
	switch( msg ) 
	{

		// --------------------------------------------------------------------------------------------
		case GWM_CREATE:
		{

			
			break;

		}  // end create

		//---------------------------------------------------------------------------------------------
		case GWM_DESTROY:
		{

			break;

		}  // end case

		// --------------------------------------------------------------------------------------------
		case GWM_INPUT_FOCUS:
		{

			// if we're givin the opportunity to take the keyboard focus we must say we want it
			if( mData1 == TRUE )
				*(Bool *)mData2 = TRUE;

			return MSG_HANDLED;

		}  // end input
		//---------------------------------------------------------------------------------------------
		case GBM_SELECTED:
		{
			if( buttonBack && (GameWindow *)mData1 == buttonBack )
				TheShell->pop();
			break;
		}  // end selected

		default:
			return MSG_IGNORED;
	}  // end switch

	return MSG_HANDLED;
}  // end CreditsMenuSystem

//-----------------------------------------------------------------------------
// PRIVATE FUNCTIONS //////////////////////////////////////////////////////////
//-----------------------------------------------------------------------------

