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

// FILE: BuddyDefs.h //////////////////////////////////////////////////////
// Generals GameSpy Buddy (GP) definitions
// Author: Matthew D. Campbell, July 2002

#pragma once

#ifndef __BUDDYDEFS_H__
#define __BUDDYDEFS_H__

void HandleBuddyResponses(void);
void PopulateOldBuddyMessages(void);

/* -online's friends on the screens around the overlay (WOLBuddyOverlay.cpp, menu redesign package F4).
	 FriendsCornerUpdate runs from every screen an invite can reach (HandleBuddyResponses calls it, so the
	 hub, Quick Match, the custom lobby and the room have it; the ladder and the end screen call it
	 themselves): the FRIENDS key on an online page's tab row, the invite toast under the head's right
	 end, and a JOIN carried from there to the custom lobby.  Nothing shows against a server without
	 friends. */
void FriendsCornerUpdate(void);

#endif // __BUDDYDEFS_H__
