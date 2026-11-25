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

////////////////////////////////////////////////////////////////////////////////
//																																						//
//  (c) 2001-2003 Electronic Arts Inc.																				//
//																																						//
////////////////////////////////////////////////////////////////////////////////

// FILE: MessageBox.cpp /////////////////////////////////////////////////////////
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
// File name: MessageBox.cpp
//
// Created:   Chris Huybregts, June 2001
//
// Desc:      the Message Box control
//
//-----------------------------------------------------------------------------
///////////////////////////////////////////////////////////////////////////////

// SYSTEM INCLUDES ////////////////////////////////////////////////////////////

// USER INCLUDES //////////////////////////////////////////////////////////////
#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "Common/GameEngine.h"
#include "Common/NameKeyGenerator.h"
#include "GameClient/WindowLayout.h"
#include "GameClient/Gadget.h"
#include "GameClient/Shell.h"
#include "GameClient/KeyDefs.h"
#include "GameClient/GameWindowManager.h"
#include "GameClient/MessageBox.h"
#include "GameClient/DisplayString.h"
#include "GameClient/DisplayStringManager.h"
#include "GameClient/GameText.h"
#include "GameClient/UIStyle.h"

//-------------------------------------------------------------------------------------------------
// The menu language's question box (docs/plans/MENU-REDESIGN.md, package 2; the boxes README's
// boxes-2-question-yes-no).  The window manager builds the box from the layout as EA's code always
// did, then MessageBoxDress lays it out on UIStyle's numbers and draws it: the title, the body
// wrapped to the box, the foot's hint and the keys, Enter's lit at the right.
//-------------------------------------------------------------------------------------------------

/// what the box draws beside its gadgets, on MessageBoxParent's user data
struct MenuBox
{
	DisplayString *title;
	DisplayString *hint;
	std::vector< DisplayString * > lines;
	GameWindow *escapeKey;
	GameWindow *enterKey;
	Bool escapeDown;		///< a key acts on its release only when the box saw it go down too, so the
	Bool enterDown;			///< Enter that opened a box from a text entry does not answer it at once
};

static Int measureLine( const UnicodeString &text, void *context )
{
	DisplayString *scratch = (DisplayString *)context;
	scratch->setText( text );
	return scratch->getWidth( -1 );
}

static void drawNothing( GameWindow *, WinInstanceData * )
{
}

static void menuBoxDraw( GameWindow *window, WinInstanceData * )
{
	MenuBox *box = (MenuBox *)window->winGetUserData();
	if( box == NULL )
		return;
	IRegion2D r;
	Int w, h;
	window->winGetScreenPosition( &r.lo.x, &r.lo.y );
	window->winGetSize( &w, &h );
	r.hi.x = r.lo.x + w;
	r.hi.y = r.lo.y + h;

	UIStyle::boxShell( r, box->title, FALSE );
	const Int x = r.lo.x + UIStyle::px( (Real)UIStyle::BOX_PAD );
	GameFont *body = UIStyle::menuFont( UIStyle::MENU_TEXT_BODY );
	for( size_t each = 0; each < box->lines.size(); each++ )
	{
		DisplayString *line = box->lines[ each ];
		if( line->getFont() != body )
			line->setFont( body );
		const Int top = r.lo.y + UIStyle::px( (Real)( UIStyle::BOX_HEAD + UIStyle::BOX_LINE * ( 1 + (Int)each ) ) );
		const Int bottom = r.lo.y + UIStyle::px( (Real)( UIStyle::BOX_HEAD + UIStyle::BOX_LINE * ( 2 + (Int)each ) ) );
		Int lw, lh;
		line->getSize( &lw, &lh );
		line->draw( x, ( top + bottom - lh ) / 2, UIStyle::INK, GameMakeColor( 0, 0, 0, 160 ) );
	}
	UIStyle::boxFootRule( r );
	if( box->hint && box->hint->getTextLength() )
	{
		GameFont *hintFont = UIStyle::menuFont( UIStyle::MENU_TEXT_HINT );
		if( box->hint->getFont() != hintFont )
			box->hint->setFont( hintFont );
		Int hw, hh;
		box->hint->getSize( &hw, &hh );
		const Int middle = r.hi.y - UIStyle::px( 44.0f );
		box->hint->draw( x, middle - hh / 2, UIStyle::MUTED, GameMakeColor( 0, 0, 0, 160 ) );
	}
}

/// MessageBoxParent's system: PassMessagesToParentSystem, but it frees what the box drew itself
static WindowMsgHandledType menuBoxParentSystem( GameWindow *window, UnsignedInt msg, WindowMsgData mData1, WindowMsgData mData2 )
{
	if( msg == GWM_DESTROY )
	{
		MenuBox *box = (MenuBox *)window->winGetUserData();
		if( box )
		{
			TheDisplayStringManager->freeDisplayString( box->title );
			TheDisplayStringManager->freeDisplayString( box->hint );
			for( size_t each = 0; each < box->lines.size(); each++ )
				TheDisplayStringManager->freeDisplayString( box->lines[ each ] );
			delete box;
			window->winSetUserData( NULL );
		}
		return MSG_HANDLED;
	}
	GameWindow *parent = window->winGetParent();
	return parent ? TheWindowManager->winSendSystemMsg( parent, msg, mData1, mData2 ) : MSG_IGNORED;
}

/// the box's MessageBoxParent under root
static GameWindow *menuBoxParent( GameWindow *root )
{
	static const NameKeyType plain = TheNameKeyGenerator->nameToKey( AsciiString( "MessageBox.wnd:MessageBoxParent" ) );
	static const NameKeyType quit = TheNameKeyGenerator->nameToKey( AsciiString( "QuitMessageBox.wnd:MessageBoxParent" ) );
	GameWindow *parent = TheWindowManager->winGetWindowFromId( root, plain );
	return parent ? parent : TheWindowManager->winGetWindowFromId( root, quit );
}

/// Escape and Enter, on the root: a key's release presses its key, when the box saw it go down
static WindowMsgHandledType menuBoxInput( GameWindow *window, UnsignedInt msg, WindowMsgData mData1, WindowMsgData mData2 )
{
	if( msg != GWM_CHAR )
		return MSG_IGNORED;
	const UnsignedByte key = (UnsignedByte)mData1;
	const UnsignedInt state = (UnsignedInt)mData2;
	const Bool escape = key == KEY_ESC;
	if( !escape && key != KEY_ENTER && key != KEY_KPENTER )
		return MSG_IGNORED;
	GameWindow *parent = menuBoxParent( window );
	MenuBox *box = parent ? (MenuBox *)parent->winGetUserData() : NULL;
	if( box == NULL )
		return MSG_IGNORED;
	Bool &down = escape ? box->escapeDown : box->enterDown;
	if( BitTest( state, KEY_STATE_DOWN ) )
	{
		down = TRUE;
		return MSG_HANDLED;
	}
	if( !BitTest( state, KEY_STATE_UP ) || !down )
		return MSG_HANDLED;
	down = FALSE;
	GameWindow *pressed = escape ? box->escapeKey : box->enterKey;
	if( pressed && !pressed->winIsHidden() && pressed->winGetEnabled() )
		TheWindowManager->winSendSystemMsg( window, GBM_SELECTED, (WindowMsgData)pressed, pressed->winGetWindowId() );
	return MSG_HANDLED;
}

/// the foot's hint: what Escape and Enter press
static const char *hintKey( UnsignedShort flags, UnsignedShort lit )
{
	const Bool cancel = BitTest( flags, MSG_BOX_CANCEL );
	if( BitTest( flags, MSG_BOX_YES ) )
	{
		if( lit == MSG_BOX_NO )
			return cancel ? "GUI:HintEscCancelEnterNo" : "GUI:HintEscEnterNo";
		return cancel ? "GUI:HintEscCancelEnterYes" : "GUI:HintEscNoEnterYes";
	}
	if( BitTest( flags, MSG_BOX_OK ) )
		return cancel ? "GUI:HintEscCancelEnterOk" : "GUI:HintEscEnterOk";
	return "GUI:HintEscCancel";
}

void MessageBoxDress( GameWindow *root, Bool quitLayout, UnsignedShort buttonFlags,
	const UnicodeString &title, const UnicodeString &body, UnsignedShort lit )
{
	if( root == NULL )
		return;
	GameWindow *parent = menuBoxParent( root );
	if( parent == NULL )
		return;
	const AsciiString prefix = quitLayout ? "QuitMessageBox.wnd:" : "MessageBox.wnd:";
	GameWindow *keyOf[ 4 ];	// yes, no, ok, cancel
	static const char *const KEY_NAMES[ 4 ] = { "ButtonYes", "ButtonNo", "ButtonOk", "ButtonCancel" };
	static const UnsignedShort KEY_FLAGS[ 4 ] = { MSG_BOX_YES, MSG_BOX_NO, MSG_BOX_OK, MSG_BOX_CANCEL };
	for( Int each = 0; each < 4; each++ )
	{
		AsciiString name = prefix;
		name.concat( KEY_NAMES[ each ] );
		keyOf[ each ] = TheWindowManager->winGetWindowFromId( parent, TheNameKeyGenerator->nameToKey( name ) );
	}
	// the code draws the words; the layout's static texts and art stay for lookups only
	for( GameWindow *child = parent->winGetChild(); child; child = child->winGetNext() )
	{
		Bool isKey = FALSE;
		for( Int each = 0; each < 4; each++ )
			isKey = isKey || child == keyOf[ each ];
		if( !isKey )
			child->winHide( TRUE );
	}

	MenuBox *box = NEW MenuBox;
	box->escapeDown = box->enterDown = FALSE;
	box->title = TheDisplayStringManager->newDisplayString();
	box->title->setText( UIStyle::capitals( title ) );
	box->hint = TheDisplayStringManager->newDisplayString();
	box->hint->setText( TheGameText->fetch( hintKey( buttonFlags, lit ) ) );

	// the body, wrapped to the box's inside in body type
	std::vector< UnicodeString > words;
	DisplayString *scratch = TheDisplayStringManager->newDisplayString();
	scratch->setFont( UIStyle::menuFont( UIStyle::MENU_TEXT_BODY ) );
	UIStyle::wrapLines( body, UIStyle::px( (Real)( UIStyle::BOX_QUESTION - 2 * UIStyle::BOX_PAD ) ), measureLine, scratch, words );
	TheDisplayStringManager->freeDisplayString( scratch );
	while( !words.empty() && words.back().isEmpty() )
		words.pop_back();
	if( words.empty() )
		words.push_back( UnicodeString::TheEmptyString );
	if( (Int)words.size() > UIStyle::questionBoxMaxLines() )
		words.resize( UIStyle::questionBoxMaxLines() );
	for( size_t each = 0; each < words.size(); each++ )
	{
		DisplayString *line = TheDisplayStringManager->newDisplayString();
		line->setText( words[ each ] );
		box->lines.push_back( line );
	}

	// the box on the board, as tall as its lines
	const IRegion2D at1080 = UIStyle::boxRect1080( UIStyle::BOX_QUESTION, UIStyle::questionBoxHeight( (Int)words.size() ), FALSE );
	const IRegion2D onScreen = UIStyle::boardRect( at1080.lo.x, at1080.lo.y, at1080.hi.x, at1080.hi.y );
	Int rootX, rootY;
	root->winGetScreenPosition( &rootX, &rootY );
	parent->winSetPosition( onScreen.lo.x - rootX, onScreen.lo.y - rootY );
	parent->winSetSize( onScreen.hi.x - onScreen.lo.x, onScreen.hi.y - onScreen.lo.y );

	// the keys right to left, Enter's first: OK or Yes, then No, then Cancel
	static const Int SLOT_ORDER[ 4 ] = { 0, 2, 1, 3 };	// yes, ok, no, cancel
	Int slot = 0;
	for( Int order = 0; order < 4; order++ )
	{
		const Int each = SLOT_ORDER[ order ];
		GameWindow *key = keyOf[ each ];
		if( key == NULL )
			continue;
		if( !BitTest( buttonFlags, KEY_FLAGS[ each ] ) )
		{
			key->winHide( TRUE );
			continue;
		}
		const IRegion2D k = UIStyle::boxKeyRect1080( at1080, slot++ );
		const IRegion2D ks = UIStyle::boardRect( k.lo.x, k.lo.y, k.hi.x, k.hi.y );
		key->winSetPosition( ks.lo.x - onScreen.lo.x, ks.lo.y - onScreen.lo.y );
		key->winSetSize( ks.hi.x - ks.lo.x, ks.hi.y - ks.lo.y );
		key->winHide( FALSE );
		UIStyle::setMenuKey( key, KEY_FLAGS[ each ] == lit );
	}
	const Int escapeOrder[ 3 ] = { 3, 1, 2 };	// cancel, no, ok
	box->escapeKey = NULL;
	for( Int each = 0; each < 3 && box->escapeKey == NULL; each++ )
	{
		if( BitTest( buttonFlags, KEY_FLAGS[ escapeOrder[ each ] ] ) )
			box->escapeKey = keyOf[ escapeOrder[ each ] ];
	}
	box->enterKey = NULL;
	for( Int each = 0; each < 4; each++ )
	{
		if( lit != 0 && KEY_FLAGS[ each ] == lit && BitTest( buttonFlags, lit ) )
			box->enterKey = keyOf[ each ];
	}

	parent->winSetUserData( box );
	parent->winSetSystemFunc( menuBoxParentSystem );
	parent->winSetDrawFunc( menuBoxDraw );
	root->winSetDrawFunc( drawNothing );
	root->winSetInputFunc( menuBoxInput );
}

static GameWindow *dressed( GameWindow *root, Bool quitLayout, UnsignedShort flags, const UnicodeString &title,
	const UnicodeString &body, UnsignedShort lit )
{
	MessageBoxDress( root, quitLayout, flags, title, body, lit );
	return root;
}

GameWindow *MessageBoxYesNo(UnicodeString titleString,UnicodeString bodyString,GameWinMsgBoxFunc yesCallback,GameWinMsgBoxFunc noCallback, UnsignedShort lit)  ///< convenience function for displaying a Message box with Yes and No buttons
{
	return dressed( TheWindowManager->gogoMessageBox(-1,-1,-1,-1,MSG_BOX_NO | MSG_BOX_YES , titleString, bodyString, yesCallback, noCallback, NULL, NULL),
		FALSE, MSG_BOX_NO | MSG_BOX_YES, titleString, bodyString, lit );
}
GameWindow *QuitMessageBoxYesNo(UnicodeString titleString,UnicodeString bodyString,GameWinMsgBoxFunc yesCallback,GameWinMsgBoxFunc noCallback, UnsignedShort lit)  ///< convenience function for displaying a Message box with Yes and No buttons
{
	return dressed( TheWindowManager->gogoMessageBox(-1,-1,-1,-1,MSG_BOX_NO | MSG_BOX_YES , titleString, bodyString, yesCallback, noCallback, NULL, NULL, TRUE),
		TRUE, MSG_BOX_NO | MSG_BOX_YES, titleString, bodyString, lit );
}


GameWindow *MessageBoxYesNoCancel(UnicodeString titleString,UnicodeString bodyString, GameWinMsgBoxFunc yesCallback, GameWinMsgBoxFunc noCallback, GameWinMsgBoxFunc cancelCallback, UnsignedShort lit)///< convenience function for displaying a Message box with Yes,No and Cancel buttons
{
	return dressed( TheWindowManager->gogoMessageBox(-1,-1,-1,-1,MSG_BOX_NO | MSG_BOX_YES | MSG_BOX_CANCEL , titleString, bodyString, yesCallback, noCallback, NULL, cancelCallback),
		FALSE, MSG_BOX_NO | MSG_BOX_YES | MSG_BOX_CANCEL, titleString, bodyString, lit );
}


GameWindow *MessageBoxOkCancel(UnicodeString titleString,UnicodeString bodyString,GameWinMsgBoxFunc okCallback,GameWinMsgBoxFunc cancelCallback)///< convenience function for displaying a Message box with Ok and Cancel buttons
{
	return dressed( TheWindowManager->gogoMessageBox(-1,-1,-1,-1,MSG_BOX_OK | MSG_BOX_CANCEL , titleString, bodyString, NULL, NULL, okCallback, cancelCallback),
		FALSE, MSG_BOX_OK | MSG_BOX_CANCEL, titleString, bodyString, MSG_BOX_OK );
}

GameWindow *MessageBoxOk(UnicodeString titleString,UnicodeString bodyString,GameWinMsgBoxFunc okCallback)///< convenience function for displaying a Message box with Ok button
{
	return dressed( TheWindowManager->gogoMessageBox(-1,-1,-1,-1,MSG_BOX_OK, titleString, bodyString, NULL, NULL, okCallback, NULL),
		FALSE, MSG_BOX_OK, titleString, bodyString, MSG_BOX_OK );
}


GameWindow *MessageBoxCancel(UnicodeString titleString,UnicodeString bodyString,GameWinMsgBoxFunc cancelCallback)///< convenience function for displaying a Message box with Cancel button
{
	// its one key backs out of something under way (a connection, a download): nothing lit, Enter idle
	return dressed( TheWindowManager->gogoMessageBox(-1,-1,-1,-1, MSG_BOX_CANCEL, titleString, bodyString, NULL, NULL, NULL, cancelCallback),
		FALSE, MSG_BOX_CANCEL, titleString, bodyString, 0 );
}


// PRIVATE DATA ///////////////////////////////////////////////////////////////////////////////////



//-------------------------------------------------------------------------------------------------
/** Message Box window system callback */
//-------------------------------------------------------------------------------------------------
WindowMsgHandledType MessageBoxSystem( GameWindow *window, UnsignedInt msg, 
										 WindowMsgData mData1, WindowMsgData mData2 )
{
	
	
	switch( msg ) 
	{

		//---------------------------------------------------------------------------------------------
		case GWM_DESTROY:
		{
			delete (WindowMessageBoxData *)window->winGetUserData();
			window->winSetUserData( NULL );
			break;

		}  // end case

		// --------------------------------------------------------------------------------------------
		case GWM_INPUT_FOCUS:
		{

			// if we're givin the opportunity to take the keyboard focus we must say we want it
			if( mData1 == TRUE )
				*(Bool *)mData2 = TRUE;

			break;

		}  // end input

		//---------------------------------------------------------------------------------------------
		case GBM_SELECTED:
		{
			GameWindow *control = (GameWindow *)mData1;
			Int controlID = control->winGetWindowId();
			static NameKeyType buttonOkID = TheNameKeyGenerator->nameToKey( AsciiString( "MessageBox.wnd:ButtonOk" ) );
			static NameKeyType buttonYesID = TheNameKeyGenerator->nameToKey( AsciiString( "MessageBox.wnd:ButtonYes" ) );
			static NameKeyType buttonNoID = TheNameKeyGenerator->nameToKey( AsciiString( "MessageBox.wnd:ButtonNo" ) );
			static NameKeyType buttonCancelID = TheNameKeyGenerator->nameToKey( AsciiString( "MessageBox.wnd:ButtonCancel" ) );
			WindowMessageBoxData *MsgBoxCallbacks = (WindowMessageBoxData *)window->winGetUserData();
			
			if( controlID == buttonOkID )
			{
				//simple enough,if we have a callback, call it, if not, then just destroy the window
				if (MsgBoxCallbacks->okCallback)
					MsgBoxCallbacks->okCallback();
				
				TheWindowManager->winDestroy(window);

			}  // end if
			else if( controlID == buttonYesID )
			{
				if (MsgBoxCallbacks->yesCallback)
					MsgBoxCallbacks->yesCallback();
				TheWindowManager->winDestroy(window);
			}  // end else if
			else if( controlID == buttonNoID )
			{
				if (MsgBoxCallbacks->noCallback)
					MsgBoxCallbacks->noCallback();
				TheWindowManager->winDestroy(window);
			}  // end else if
			else if( controlID == buttonCancelID )
			{
				if (MsgBoxCallbacks->cancelCallback)
					MsgBoxCallbacks->cancelCallback();
				TheWindowManager->winDestroy(window);
			}  // end else if
			
			break;

		}  // end selected
		
		//---------------------------------------------------------------------------------------------
		default:
			return MSG_IGNORED;

	}  // end switch

	return MSG_HANDLED;

}  // end MessageBoxSystem
//-------------------------------------------------------------------------------------------------
/** Message Box window system callback */
//-------------------------------------------------------------------------------------------------
WindowMsgHandledType QuitMessageBoxSystem( GameWindow *window, UnsignedInt msg, 
										 WindowMsgData mData1, WindowMsgData mData2 )
{
	
	
	switch( msg ) 
	{

		//---------------------------------------------------------------------------------------------
		case GWM_DESTROY:
		{
			delete (WindowMessageBoxData *)window->winGetUserData();
			window->winSetUserData( NULL );
			break;

		}  // end case

		// --------------------------------------------------------------------------------------------
		case GWM_INPUT_FOCUS:
		{

			// if we're givin the opportunity to take the keyboard focus we must say we want it
			if( mData1 == TRUE )
				*(Bool *)mData2 = TRUE;

			break;

		}  // end input

		//---------------------------------------------------------------------------------------------
		case GBM_SELECTED:
		{
			GameWindow *control = (GameWindow *)mData1;
			Int controlID = control->winGetWindowId();
			static NameKeyType buttonOkID = TheNameKeyGenerator->nameToKey( AsciiString( "QuitMessageBox.wnd:ButtonOk" ) );
			static NameKeyType buttonYesID = TheNameKeyGenerator->nameToKey( AsciiString( "QuitMessageBox.wnd:ButtonYes" ) );
			static NameKeyType buttonNoID = TheNameKeyGenerator->nameToKey( AsciiString( "QuitMessageBox.wnd:ButtonNo" ) );
			static NameKeyType buttonCancelID = TheNameKeyGenerator->nameToKey( AsciiString( "QuitMessageBox.wnd:ButtonCancel" ) );
			WindowMessageBoxData *MsgBoxCallbacks = (WindowMessageBoxData *)window->winGetUserData();
			
			if( controlID == buttonOkID )
			{
				//simple enough,if we have a callback, call it, if not, then just destroy the window
				if (MsgBoxCallbacks->okCallback)
					MsgBoxCallbacks->okCallback();
				
				TheWindowManager->winDestroy(window);

			}  // end if
			else if( controlID == buttonYesID )
			{
				if (MsgBoxCallbacks->yesCallback)
					MsgBoxCallbacks->yesCallback();
				TheWindowManager->winDestroy(window);
			}  // end else if
			else if( controlID == buttonNoID )
			{
				if (MsgBoxCallbacks->noCallback)
					MsgBoxCallbacks->noCallback();
				TheWindowManager->winDestroy(window);
			}  // end else if
			else if( controlID == buttonCancelID )
			{
				if (MsgBoxCallbacks->cancelCallback)
					MsgBoxCallbacks->cancelCallback();
				TheWindowManager->winDestroy(window);
			}  // end else if
			
			break;

		}  // end selected
		
		//---------------------------------------------------------------------------------------------
		default:
			return MSG_IGNORED;

	}  // end switch

	return MSG_HANDLED;

}  // end MessageBoxSystem
