/*
**	Copyright 2026 İlyas Akın
**	Additional terms under GNU GPL section 7 apply: see LICENSE.md.
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

// FILE: IMEManagerPosix.h ////////////////////////////////////////////////////////////////////////
// Desc:   Text input off Windows: IMEManagerInterface over the platform's text events (C3).
///////////////////////////////////////////////////////////////////////////////////////////////////

/* On Windows every character typed into a text field reaches it through IMEManager, English as much
	 as Japanese: WndProc hands WM_CHAR to TheIMEManager, which sends it to the attached window as
	 GWM_IME_CHAR.  The keyboard's own GWM_CHAR carries only the editing keys (GadgetTextEntry takes
	 Backspace, Tab and the arrows from it and nothing printable).  Without an IME manager, then, no text
	 field receives a character - which is what off Windows had until C3.

	 This is the manager off Windows.  It knows nothing of SDL: the device layer calls commitText,
	 setComposition and enterPressed with what its text events carry, and installs DeviceHooks so that
	 attaching to a text field turns the platform's text input on, at that field, and detaching turns it
	 off - the platform's input method is then active only while a text field has the focus, and no
	 hotkey is ever taken by it.  The engine draws the composition in the field, as W3DTextEntry already
	 does from getCompositionString; the platform draws the candidate list, so there is none here. */

#pragma once

#ifndef __IMEMANAGERPOSIX_H
#define __IMEMANAGERPOSIX_H

#include "GameClient/IMEManager.h"
#include "Common/UnicodeString.h"

class PosixIMEManager : public IMEManagerInterface
{
public:

	/// What the device layer does when a text field gains or loses the manager
	struct DeviceHooks
	{
		void (*startTextInput)( Int x, Int y, Int width, Int height );	///< on, with the field's screen rectangle
		void (*stopTextInput)( void );
	};
	static void setDeviceHooks( const DeviceHooks &hooks );

	PosixIMEManager( void );
	virtual ~PosixIMEManager( void );

	virtual void init( void ) {}
	virtual void reset( void );
	virtual void update( void ) {}

	virtual void attach( GameWindow *window );
	virtual void detatch( void );
	virtual void enable( void );
	virtual void disable( void );
	virtual Bool isEnabled( void )																	{ return m_disabled == 0; }
	virtual Bool isAttachedTo( GameWindow *window )									{ return m_window == window; }
	virtual GameWindow *getWindow( void )														{ return m_window; }
	virtual Bool isComposing( void )																{ return m_composing; }
	virtual void getCompositionString( UnicodeString &string )			{ string = m_composition; }
	virtual Int getCompositionCursorPosition( void )								{ return m_compositionCursor; }
	virtual Int getIndexBase( void )																{ return 1; }
	virtual Int getCandidateCount()																	{ return 0; }
	virtual UnicodeString *getCandidate( Int )											{ return NULL; }
	virtual Int getSelectedCandidateIndex()													{ return 0; }
	virtual Int getCandidatePageSize()															{ return 0; }
	virtual Int getCandidatePageStart()															{ return 0; }
	virtual Bool serviceIMEMessage( void *, UnsignedInt, Int, Int )	{ return FALSE; }	// WndProc's; no Windows messages here
	virtual Int result( void )																			{ return 0; }

	// ---- what the device layer's text events carry ----------------------------------------------
	/** Finished text, UTF-8: to the attached window as GWM_IME_CHAR, one per UTF-16 unit, surrogates
		* included, as WM_CHAR delivers them - units below 32 dropped, IMEManager.cpp's own rule.  Ends a
		* composition. */
	void commitText( const char *utf8 );
	/// The text being composed, UTF-8, and the cursor in code points; empty text ends the composition
	void setComposition( const char *utf8, Int cursorCodePoints );
	/** Return or keypad Enter pressed: '\r' to the attached window, as WM_CHAR delivers it and
		* GadgetTextEntry ends an edit on - the platform's text events never carry it.  Nothing while
		* composing, where the key belongs to the input method. */
	void enterPressed( void );
	/// Refresh an active field's candidate rectangle without detaching or ending its composition.
	void refreshTextInputArea( void );

protected:
	/// GWM_IME_CHAR to the window, through the window manager
	virtual void sendChar( GameWindow *window, WideChar ch );
	/// The window's screen rectangle, for the platform's candidate list
	virtual void windowArea( GameWindow *window, Int &x, Int &y, Int &width, Int &height );

	void startOrStopTextInput( void );
	void endComposition( void );

	GameWindow *m_window;
	Int m_disabled;
	Bool m_textInputOn;
	Bool m_composing;
	UnicodeString m_composition;
	Int m_compositionCursor;		///< in WideChar units
};

#endif // __IMEMANAGERPOSIX_H
