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

// FILE: MessageBox.h /////////////////////////////////////////////////////////////////////////////
// Author: Chris Huybregts, November 2001
// Description: Message Box file containing user aliases
///////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#ifndef __MESSAGEBOX_H_
#define __MESSAGEBOX_H_

#include "GameClient/GameWindowManager.h"

// lit is the key Enter presses, drawn lit: MSG_BOX_YES, or MSG_BOX_NO on a question that loses
// something (surrender, restart, delete), where Enter then answers No.
GameWindow *MessageBoxYesNo(UnicodeString titleString,UnicodeString bodyString,GameWinMsgBoxFunc yesCallback,GameWinMsgBoxFunc noCallback, UnsignedShort lit = MSG_BOX_YES);  ///< convenience function for displaying a Message box with Yes and No buttons
GameWindow *QuitMessageBoxYesNo(UnicodeString titleString,UnicodeString bodyString,GameWinMsgBoxFunc yesCallback,GameWinMsgBoxFunc noCallback, UnsignedShort lit = MSG_BOX_YES);  ///< convenience function for displaying a Message box with Yes and No buttons


GameWindow *MessageBoxYesNoCancel(UnicodeString titleString,UnicodeString bodyString, GameWinMsgBoxFunc yesCallback, GameWinMsgBoxFunc noCallback, GameWinMsgBoxFunc cancelCallback, UnsignedShort lit = MSG_BOX_YES);///< convenience function for displaying a Message box with Yes,No and Cancel buttons


GameWindow *MessageBoxOkCancel(UnicodeString titleString,UnicodeString bodyString,GameWinMsgBoxFunc okCallback,GameWinMsgBoxFunc cancelCallback);///< convenience function for displaying a Message box with Ok and Cancel buttons

GameWindow *MessageBoxOk(UnicodeString titleString,UnicodeString bodyString,GameWinMsgBoxFunc okCallback);///< convenience function for displaying a Message box with Ok button


GameWindow *MessageBoxCancel(UnicodeString titleString,UnicodeString bodyString,GameWinMsgBoxFunc cancelCallback);///< convenience function for displaying a Message box with Cancel button

/** Lays a box made from MessageBox.wnd or QuitMessageBox.wnd (root is the layout's top window) out
	* as the menu language's question box (UIStyle's box, 720 wide, growing 28 px a body line), draws
	* it in code and lets Escape and Enter answer it: Escape presses Cancel, else No, else OK; Enter
	* presses lit (nothing when lit is 0).  buttonFlags are the keys the box shows. */
void MessageBoxDress( GameWindow *root, Bool quitLayout, UnsignedShort buttonFlags,
	const UnicodeString &title, const UnicodeString &body, UnsignedShort lit );

#endif //__MESSAGEBOX_H_