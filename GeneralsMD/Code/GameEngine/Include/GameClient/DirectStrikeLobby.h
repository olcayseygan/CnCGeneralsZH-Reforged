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

// FILE: DirectStrikeLobby.h //////////////////////////////////////////////////////////////////////
//
// The Direct Strike room, README-lobby.md in experiments/proofs/2026-11-21/ds-lobby: three seat cards a
// team facing each other, the island between them (the chat with it in a network room), Back and
// Start.  One screen for the skirmish, LAN and online rooms, drawn on DirectStrikeTheme over the room's
// own layout.
//
// The room's windows stay and keep doing the work: the lobby hides every one of them but the chat
// entry, draws in their place from the room's parent window, and a choice made on it is made on the
// hidden box it stands for (the seat, army and colour boxes, Start, Back, the chat's Enter), so the
// room's own handlers send the LAN or online request exactly as before.  Nothing here writes a seat.
//
///////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#ifndef __DIRECTSTRIKELOBBY_H_
#define __DIRECTSTRIKELOBBY_H_

class GameInfo;
class GameWindow;

/// put the lobby over a room (SkirmishGameOptionsMenu, LanGameOptionsMenu or GameSpyGameOptionsMenu.wnd);
/// a room calls it after every slot list repaint, and a second call only points it at the game again
void DirectStrikeLobby_attach( GameWindow *parent, const char *layoutFilename, const GameInfo *game );
/// give the room its own draw and input back, before its windows go
void DirectStrikeLobby_detach( void );

#endif // __DIRECTSTRIKELOBBY_H_
