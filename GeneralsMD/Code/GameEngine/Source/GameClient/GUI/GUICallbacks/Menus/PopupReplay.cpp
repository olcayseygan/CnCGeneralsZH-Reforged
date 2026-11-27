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
// Modified 2026 by İlyas Akın for the macOS/Linux port; see NOTICE.md and the git history.

////////////////////////////////////////////////////////////////////////////////
//																																						//
//  (c) 2001-2003 Electronic Arts Inc.																				//
//																																						//
////////////////////////////////////////////////////////////////////////////////

// FILE: PopupReplay.cpp /////////////////////////////////////////////////////////
//-----------------------------------------------------------------------------
//                                                                          
//                       Electronic Arts Pacific.                          
//                                                                          
//                       Confidential Information                           
//                Copyright (C) 2002 - All Rights Reserved                  
//                                                                          
//-----------------------------------------------------------------------------
//
// Project:   Generals
//
// File name: PopupReplay.cpp
//
// Created:   Matthew D. Campbell, November 2002
//
// Desc:      the Replay Save window control
//
//-----------------------------------------------------------------------------
///////////////////////////////////////////////////////////////////////////////

// INCLUDES ///////////////////////////////////////////////////////////////////////////////////////

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine
#include "Lib/Clock.h"

#include "Common/LocalFileSystem.h"
#include "Common/MessageStream.h"
#include "Common/Recorder.h"
#include "GameClient/GadgetListBox.h"
#include "GameClient/GadgetTextEntry.h"
#include "GameClient/GameText.h"
#include "GameClient/GameWindowManager.h"
#include "GameClient/GUICallbacks.h"
#include "GameClient/MessageBox.h"
#include "GameClient/Shell.h"
#include "GameClient/SinglePlayerParts.h"
#include "GameLogic/GameLogic.h"

#ifdef _INTERNAL
// for occasional debugging...
//#pragma optimize("", off)
//#pragma MESSAGE("************************************** WARNING, optimization disabled for debugging purposes")
#endif

// PRIVATE DATA ///////////////////////////////////////////////////////////////////////////////////
static NameKeyType buttonBackKey					= NAMEKEY_INVALID;
static NameKeyType buttonSaveKey					= NAMEKEY_INVALID;
static NameKeyType listboxGamesKey				= NAMEKEY_INVALID;
static NameKeyType textEntryReplayNameKey = NAMEKEY_INVALID;

static GameWindow *parent = NULL;
static GameWindow *replaySavedParent = NULL;

static time_t s_fileSavePopupStartTime = 0;
static const time_t s_fileSavePopupDuration = 1000;

// PUBLIC FUNCTIONS ///////////////////////////////////////////////////////////////////////////////
extern void PopulateReplayFileListbox(GameWindow *listbox);
extern void ScoreScreenEnableControls(Bool enable);
extern UnicodeString GetReplayFilenameFromListbox(GameWindow *listbox, Int index);
extern std::string LastReplayFileName;

//-------------------------------------------------------------------------------------------------
// The box in the menu language (docs/plans/MENU-REDESIGN.md, package 12): Save Replay over the end
// screen, NAME and its text box, the replays already saved, CANCEL and SAVE lit; "replay saved" is
// a one-line box over it.  Tools/boxes_layout.py puts the windows on SinglePlayerLayout.h's rects.
//-------------------------------------------------------------------------------------------------
namespace SPL = SinglePlayerLayout;
namespace SPP = SinglePlayerParts;

static MenuBox::TextPool popupReplayText;

static void popupReplayNothingDraw( GameWindow *, WinInstanceData * )
{
}

static void popupReplayBoxDraw( GameWindow *window, WinInstanceData * )
{
	using namespace UIStyle;
	popupReplayText.begin();
	const IRegion2D box = MenuBox::windowRect( window );
	boxShell( box, popupReplayText.get( capitals( TheGameText->fetch( "GUI:SaveReplay" ) ) ), FALSE );
	const IRegion2D entry = SPP::on( SPL::REPLAY_BOX_ENTRY );
	SPP::capitalsKey( "GUI:SPName", SPP::on( SPL::REPLAY_BOX_LIST ).lo.x, ( entry.lo.y + entry.hi.y ) / 2, MUTED, MenuBox::LEFT,
		entry.lo.x - SPP::on( SPL::REPLAY_BOX_LIST ).lo.x - SPP::px( 12.0f ) );
	static const char *heads[ SPL::RBOX_COLUMNS ] = { "GUI:SPName", "GUI:SPRecorded", "GUI:SPMap", "GUI:SPLength" };
	SPL::Column columns[ SPL::RBOX_COLUMNS ];
	UnicodeString labels[ SPL::RBOX_COLUMNS ];
	for( Int c = 0; c < SPL::RBOX_COLUMNS; c++ )
	{
		columns[ c ] = SPL::replayBoxColumn( c );
		labels[ c ] = TheGameText->fetch( heads[ c ] );
	}
	SPP::heads( columns, labels, SPL::RBOX_COLUMNS, SPL::RBOX_RECORDED, SPL::REPLAY_BOX_HEADS, SPL::REPLAY_BOX_LIST.lo.x,
		SPL::REPLAY_BOX_LIST.hi.x );
	boxFootRule( box );
	const Int keysMiddle = box.hi.y - SPP::px( 44.0f );
	SPP::wordsKey( popupReplayText, "GUI:SPHintEscEnterSaves", MENU_TEXT_HINT, FALSE, box.lo.x + SPP::px( (Real)BOX_PAD ), keysMiddle,
		MUTED, SPP::px( 560.0f ) );
}

/// the saved replays under the name: a click copies a name up to replace that file
static void popupReplayListDraw( GameWindow *window, WinInstanceData * )
{
	static const Int kinds[ SPL::RBOX_COLUMNS ] = { SPL::REPLAY_NAME, SPL::REPLAY_RECORDED, SPL::REPLAY_MAP, SPL::REPLAY_LENGTH };
	SPL::Column columns[ SPL::RBOX_COLUMNS ];
	for( Int c = 0; c < SPL::RBOX_COLUMNS; c++ )
		columns[ c ] = SPL::replayBoxColumn( c );
	DrawReplayRows( window, popupReplayText, columns, kinds, SPL::RBOX_COLUMNS, SPL::REPLAY_BOX_LIST.hi.x - 8 );
	SPP::scrollBar( window, SPP::point( (Real)( SPL::REPLAY_BOX_LIST.hi.x - 4 ), 0.0f ).x );
}

/// "replay saved", the one-line box over the Save Replay box until it closes
static void popupReplaySavedDraw( GameWindow *window, WinInstanceData * )
{
	using namespace UIStyle;
	const IRegion2D box = MenuBox::windowRect( window );
	boxShell( box, popupReplayText.get( capitals( TheGameText->fetch( "GUI:SaveReplay" ) ) ), FALSE );
	SPP::words( popupReplayText, TheGameText->fetch( "GUI:ReplaySaved" ), MENU_TEXT_BODY, FALSE, box.lo.x + SPP::px( (Real)BOX_PAD ),
		box.lo.y + SPP::px( (Real)( BOX_HEAD + BOX_LINE + BOX_LINE / 2 ) ), INK, box.hi.x - box.lo.x - 2 * SPP::px( (Real)BOX_PAD ) );
}

//-------------------------------------------------------------------------------------------------
/** Show or hide the "Replay Saved" popup */
//-------------------------------------------------------------------------------------------------
void ShowReplaySavedPopup(Bool show)
{
	if (replaySavedParent != NULL) {
		if (show) {
			replaySavedParent->winHide(FALSE);
		} else {
			replaySavedParent->winHide(TRUE);
		}
	}
}

// ------------------------------------------------------------------------------------------------
/** Close the save/load menu */
// ------------------------------------------------------------------------------------------------
static void closeSaveMenu( GameWindow *window )
{
	WindowLayout *layout = window->winGetLayout();

	if( layout )
		layout->hide( TRUE );

}  // end closeSaveMenu

//-------------------------------------------------------------------------------------------------
/** Initialize the SaveLoad menu */
//-------------------------------------------------------------------------------------------------
void PopupReplayInit( WindowLayout *layout, void *userData )
{

  // get ids for our children controls
	buttonBackKey					 = NAMEKEY( "PopupReplay.wnd:ButtonBack" );
	buttonSaveKey					 = NAMEKEY( "PopupReplay.wnd:ButtonSave" );
	listboxGamesKey				 = NAMEKEY( "PopupReplay.wnd:ListboxGames" );
	textEntryReplayNameKey = NAMEKEY( "PopupReplay.wnd:TextEntryReplayName" );

	//set keyboard focus to main parent and set modal
	NameKeyType parentID = TheNameKeyGenerator->nameToKey("PopupReplay.wnd:PopupReplayMenu");
	parent = TheWindowManager->winGetWindowFromId( NULL, parentID );
	TheWindowManager->winSetFocus( parent );

	NameKeyType replaySavedParentID = TheNameKeyGenerator->nameToKey("PopupReplay.wnd:PopupReplaySaved");
	replaySavedParent = TheWindowManager->winGetWindowFromId( NULL, replaySavedParentID);
	if (replaySavedParent == NULL) {
		DEBUG_CRASH(("replaySavedParent == NULL"));
	}

	ShowReplaySavedPopup(FALSE);

	// enable the menu action buttons
	GameWindow *buttonFrame = TheWindowManager->winGetWindowFromId( parent, NAMEKEY( "PopupReplay.wnd:MenuButtonFrame" ) );
	buttonFrame->winEnable( TRUE );

	// get the listbox that will have the save games in it
	GameWindow *listboxGames = TheWindowManager->winGetWindowFromId( NULL, listboxGamesKey );
	DEBUG_ASSERTCRASH( listboxGames != NULL, ("PopupReplayInit - Unable to find games listbox\n") );

	// the menu language: the box draws itself, its list the rows, its keys outline only
	if( parent )
		parent->winSetDrawFunc( popupReplayNothingDraw );
	GameWindow *box = TheWindowManager->winGetWindowFromId( parent, NAMEKEY( "PopupReplay.wnd:PopupReplaySubMenu" ) );
	if( box )
		box->winSetDrawFunc( popupReplayBoxDraw );
	buttonFrame->winSetDrawFunc( popupReplayNothingDraw );
	if( replaySavedParent )
		replaySavedParent->winSetDrawFunc( popupReplaySavedDraw );
	SPP::takeOver( listboxGames, popupReplayListDraw );
	GadgetListBoxSetFont( listboxGames, UIStyle::menuFont( UIStyle::MENU_TEXT_LABEL ) );
	SPP::setEntry( TheWindowManager->winGetWindowFromId( parent, textEntryReplayNameKey ) );
	SPP::key( TheWindowManager->winGetWindowFromId( parent, buttonBackKey ), "GUI:Cancel", FALSE );
	SPP::key( TheWindowManager->winGetWindowFromId( parent, buttonSaveKey ), "GUI:Save", TRUE );

	// populate the listbox with the save games on disk
	PopulateReplayFileListbox(listboxGames);

	GameWindow *textEntryReplayName = TheWindowManager->winGetWindowFromId( parent, textEntryReplayNameKey );
	GadgetTextEntrySetText(textEntryReplayName, UnicodeString::TheEmptyString);
	TheWindowManager->winSetFocus( textEntryReplayName );

	//Disable the button immediately as the code above us starts off with an empty string.
	GameWindow *control = TheWindowManager->winGetWindowFromId( parent, buttonSaveKey );
	if( control )
	{
		control->winEnable( FALSE );
	}

}  // end SaveLoadMenuInit

//-------------------------------------------------------------------------------------------------
/** SaveLoad menu shutdown method */
//-------------------------------------------------------------------------------------------------
void PopupReplayShutdown( WindowLayout *layout, void *userData )
{
	parent = NULL;
	popupReplayText.clear();

}  // end SaveLoadMenuShutdown

//-------------------------------------------------------------------------------------------------
/** SaveLoad menu update method */
//-------------------------------------------------------------------------------------------------
void PopupReplayUpdate( WindowLayout *layout, void *userData )
{

	if (s_fileSavePopupStartTime != 0) 
	{
		// the replay save confirmation popup is up
		// check to see if its time to take it down.
		if ((Clock_Milliseconds() - s_fileSavePopupStartTime) >= s_fileSavePopupDuration) 
		{
			ShowReplaySavedPopup(FALSE);

			// close the save/load menu
			closeSaveMenu( parent );
			ScoreScreenEnableControls(TRUE);

			// reset the timer to 0 cause we have to.
			s_fileSavePopupStartTime = 0;
		}
	}
}  // end SaveLoadMenuUpdate 

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
WindowMsgHandledType PopupReplayInput( GameWindow *window, UnsignedInt msg, WindowMsgData mData1, WindowMsgData mData2 )
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
						GameWindow *button = TheWindowManager->winGetWindowFromId( parent, buttonBackKey );
						TheWindowManager->winSendSystemMsg( window, GBM_SELECTED, 
																								(WindowMsgData)button, buttonBackKey );

					}  // end if

					// don't let key fall through anywhere else
					return MSG_HANDLED;

				}  // end escape

			}  // end switch( key )

		}  // end char

	}  // end switch( msg )

	return MSG_IGNORED;
}

static void reallySaveReplay(void);
static std::string replayPath;

// ------------------------------------------------------------------------------------------------
/** Save the replay */
// ------------------------------------------------------------------------------------------------
static GameWindow *messageBoxWin = NULL;

/** The name as the file gets it: a Turkish or Western accented letter as its plain one (c-cedilla c, g-breve g,
	* dotless i and dotted I as i and I, s-cedilla s, the umlauts and the rest of Latin-1 likewise), anything else past
	* ASCII as _.  AsciiString::translate keeps a character's low byte, so these went to the disk as other
	* characters (dotless i, 0x131, as 1; s-cedilla, 0x15F, as _). */
static UnicodeString foldReplayName( const UnicodeString &name )
{
	// U+00C0 to U+00FF in order, the multiplication and division signs as _
	static const char LATIN1[] = "AAAAAAACEEEEIIIIDNOOOOO_OUUUUYTsaaaaaaaceeeeiiiidnooooo_ouuuuyty";
	UnicodeString folded;
	for( Int i = 0; i < name.getLength(); i++ )
	{
		WideChar c = name.getCharAt( i );
		if( c >= 0xC0 && c <= 0xFF )
			c = (WideChar)LATIN1[ c - 0xC0 ];
		else if( c == 0x11E || c == 0x11F )
			c = c == 0x11E ? u'G' : u'g';
		else if( c == 0x130 || c == 0x131 )
			c = c == 0x130 ? u'I' : u'i';
		else if( c == 0x15E || c == 0x15F )
			c = c == 0x15E ? u'S' : u's';
		else if( c > 126 )
			c = u'_';
		folded.concat( c );
	}
	return folded;
}

/** A name Windows takes as a file, already folded to ASCII: none of / \ : * ? " < > | or a control character, not
	* ending in a dot, not a device name (CON, NUL, COM1 and the rest). */
static Bool isReplayFileName( const UnicodeString &name )
{
	const Int length = name.getLength();
	if( length == 0 || name.getCharAt( length - 1 ) == u'.' )
		return FALSE;
	for( Int i = 0; i < length; i++ )
	{
		const WideChar c = name.getCharAt( i );
		if( c < 32 || c > 126 || strchr( "/\\:*?\"<>|", (char)c ) != NULL )
			return FALSE;
	}
	AsciiString ascii;
	ascii.translate( name );
	const char *text = ascii.str();
	const char *dot = strchr( text, '.' );
	const Int stem = dot != NULL ? (Int)( dot - text ) : length;
	static const char *const DEVICES[] = { "CON", "PRN", "AUX", "NUL" };
	for( Int each = 0; each < ARRAY_SIZE( DEVICES ); each++ )
		if( stem == 3 && strncasecmp( text, DEVICES[ each ], 3 ) == 0 )
			return FALSE;
	if( stem == 4 && ( strncasecmp( text, "COM", 3 ) == 0 || strncasecmp( text, "LPT", 3 ) == 0 ) && text[ 3 ] >= '1' && text[ 3 ] <= '9' )
		return FALSE;
	return TRUE;
}

static void saveReplay( UnicodeString filename )
{
	AsciiString translated;
	filename.trim();
	if (filename == TheGameText->fetch("GUI:LastReplay"))
	{
		translated = TheRecorder->getLastReplayFileName();
	}
	else
	{
		const UnicodeString folded = foldReplayName(filename);
		if (!isReplayFileName(folded))
		{
			// said in the player's words before anything touches the disk, where Windows' own error used to come up
			MessageBoxOk(TheGameText->fetch("GUI:Error"), TheGameText->fetch("GUI:ReplayBadName"), NULL);
			return;
		}
		translated.translate(folded);
	}

	AsciiString fullPath = TheRecorder->getReplayDir();
	fullPath.concat(translated);
	fullPath.concat(TheRecorder->getReplayExtention());

	replayPath = fullPath.str();
	messageBoxWin = NULL;
	if (TheLocalFileSystem->doesFileExist(fullPath.str()))
	{
		messageBoxWin = MessageBoxOkCancel(TheGameText->fetch("GUI:OverwriteReplayTitle"), TheGameText->fetch("GUI:OverwriteReplay"), reallySaveReplay, NULL);		
	}
	else
	{
		reallySaveReplay();
	}
}
void reallySaveReplay(void)
{
	AsciiString filename = replayPath.c_str();

	AsciiString oldFilename;
	oldFilename = TheRecorder->getReplayDir();
	oldFilename.concat(LastReplayFileName.c_str());
	oldFilename.concat(TheRecorder->getReplayExtention());

	if (oldFilename == filename)
		return;

	if (TheLocalFileSystem->doesFileExist(filename.str()))
	{
		if(!TheLocalFileSystem->deleteFile(filename.str()))
		{
#if defined(_WIN32)
			// Win32 only: WideChar and WCHAR are the same two bytes there, which is what makes the cast
			// honest.  The size is in characters, as FormatMessageW takes it; it used to be
			// sizeof(buffer), twice that.
			WideChar buffer[1024];
			FormatMessageW( FORMAT_MESSAGE_FROM_SYSTEM, NULL, GetLastError(), 0, reinterpret_cast<LPWSTR>( buffer ),
				sizeof( buffer ) / sizeof( buffer[0] ), NULL );
			UnicodeString errorStr;
			errorStr.set(buffer);
#else
			// The system's reason, as the Windows branch shows it.  strerror is ASCII in the "C" locale
			// the game keeps (see the port's locale rule), so translate() is exact here.
			UnicodeString errorStr;
			errorStr.translate(AsciiString(strerror(errno)));
#endif
			errorStr.trim();
			if(messageBoxWin)
			{
				TheWindowManager->winUnsetModal(messageBoxWin);
				messageBoxWin = NULL;
			}
			MessageBoxOk(TheGameText->fetch("GUI:Error"),errorStr, NULL);

			// get the listbox that will have the save games in it
			GameWindow *listboxGames = TheWindowManager->winGetWindowFromId( parent, listboxGamesKey );
			DEBUG_ASSERTCRASH( listboxGames != NULL, ("reallySaveReplay - Unable to find games listbox\n") );

			// populate the listbox with the save games on disk
			PopulateReplayFileListbox(listboxGames);
			return;
		}
	}

	// copy the replay to the right place
	if(!TheLocalFileSystem->copyFile(oldFilename.str(),filename.str(), FALSE))
	{
#if defined(_WIN32)
		// Win32 only: WideChar and WCHAR are the same two bytes there, which is what makes the cast
		// honest.  The size is in characters, as FormatMessageW takes it; it used to be
		// sizeof(buffer), twice that.
		WideChar buffer[1024];
		FormatMessageW( FORMAT_MESSAGE_FROM_SYSTEM, NULL, GetLastError(), 0, reinterpret_cast<LPWSTR>( buffer ),
			sizeof( buffer ) / sizeof( buffer[0] ), NULL );
		UnicodeString errorStr;
		errorStr.set(buffer);
#else
		UnicodeString errorStr;
		errorStr.translate(AsciiString(strerror(errno)));		// as the delete above
#endif
		errorStr.trim();
		if(messageBoxWin)
		{
			TheWindowManager->winUnsetModal(messageBoxWin);
			messageBoxWin = NULL;
		}
		MessageBoxOk(TheGameText->fetch("GUI:Error"),errorStr, NULL);
		return;
	}

	// get the listbox that will have the save games in it
	GameWindow *listboxGames = TheWindowManager->winGetWindowFromId( parent, listboxGamesKey );
	DEBUG_ASSERTCRASH( listboxGames != NULL, ("reallySaveReplay - Unable to find games listbox\n") );

	// populate the listbox with the save games on disk
	PopulateReplayFileListbox(listboxGames);

	ShowReplaySavedPopup(TRUE);
	s_fileSavePopupStartTime = Clock_Milliseconds();
}

//-------------------------------------------------------------------------------------------------
/** SaveLoad menu system callback */
//-------------------------------------------------------------------------------------------------
WindowMsgHandledType PopupReplaySystem( GameWindow *window, UnsignedInt msg, 
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

    //----------------------------------------------------------------------------------------------
    case GWM_INPUT_FOCUS:
		{

			// if we're givin the opportunity to take the keyboard focus we must say we want it
			if( mData1 == TRUE )
				*(Bool *)mData2 = TRUE;

			break;

		}  // end input

		// --------------------------------------------------------------------------------------------
		case GLM_SELECTED:
		{
			GameWindow *control = (GameWindow *)mData1;

			GameWindow *listboxGames = TheWindowManager->winGetWindowFromId( window, listboxGamesKey );
			DEBUG_ASSERTCRASH( listboxGames != NULL, ("PopupReplaySystem - Unable to find games listbox\n") );

			//
			// handle games listbox, when certain items are selected in the listbox only some
			// commands are available
			//
			if( control == listboxGames )
			{
				int rowSelected = mData2;
				if (rowSelected >= 0)
				{
					UnicodeString filename;
					filename = GadgetListBoxGetText(listboxGames, rowSelected);
					GameWindow *textEntryReplayName = TheWindowManager->winGetWindowFromId( window, textEntryReplayNameKey );
					DEBUG_ASSERTCRASH( textEntryReplayName != NULL, ("PopupReplaySystem - Unable to find text entry\n") );
					GadgetTextEntrySetText(textEntryReplayName, filename);
				}
			}

			break;

		}  // end selected

    //---------------------------------------------------------------------------------------------
		case GEM_EDIT_DONE:
		{
			GameWindow *control = (GameWindow *)mData1;
			Int controlID = control->winGetWindowId();

      if( controlID == textEntryReplayNameKey )
      {
				UnicodeString filename = GadgetTextEntryGetText( control );
				if (filename.isEmpty())
					break;

				saveReplay(filename);

      }
	
			break;

		}  // end selected
    //---------------------------------------------------------------------------------------------
		case GBM_SELECTED:
		{
			GameWindow *control = (GameWindow *)mData1;
			Int controlID = control->winGetWindowId();

      if( controlID == buttonSaveKey )
      {
				// get the filename, and see if we are overwriting
				GameWindow *textEntryReplayName = TheWindowManager->winGetWindowFromId( window, textEntryReplayNameKey );
				DEBUG_ASSERTCRASH( textEntryReplayName != NULL, ("PopupReplaySystem - Unable to find text entry\n") );

				UnicodeString filename = GadgetTextEntryGetText( textEntryReplayName );
				if (filename.isEmpty())
					break;

				saveReplay(filename);

      }
			else if( controlID == buttonBackKey )
			{

				// close the save/load menu
				closeSaveMenu( window );
				ScoreScreenEnableControls(TRUE);

			}  // end if
	
			break;

		}  // end selected

		case GEM_UPDATE_TEXT:
		{
			//Kris:
			//Enable or disable the save button -- disabled when empty.
			GameWindow *control = TheWindowManager->winGetWindowFromId( parent, textEntryReplayNameKey );
			if( control )
			{
				UnicodeString filename;
				filename.set( GadgetTextEntryGetText( control ) );
				control = TheWindowManager->winGetWindowFromId( parent, buttonSaveKey );
				if( control )
				{
					if( filename.isEmpty() )
					{
						control->winEnable( FALSE );
					}
					else
					{
						control->winEnable( TRUE );
					}
				}
			}
		}

		default:
			return MSG_IGNORED;

	}  // end switch

	return MSG_HANDLED;

}
