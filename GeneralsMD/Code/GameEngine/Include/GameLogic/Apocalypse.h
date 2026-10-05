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

// FILE: Apocalypse.h ////////////////////////////////////////////////////////////////////////////
//
// Apocalypse mode: every player in the lobby on one team, against a computer side that owns no
// base and no money and raises zombies in the fog round each player in turn until nobody is left.
// The lobby picks it (GameInfo's AP=, an ApocalypseMode), GameLogic::startNewGame puts the players
// on one team and adds the zombie side, and Apocalypse_tick brings the zombies in.
//
///////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#ifndef __APOCALYPSE_H_
#define __APOCALYPSE_H_

#include "Lib/BaseType.h"

class GameInfo;
class Player;
class Xfer;

/// the side the zombies play for; GameLogic::startNewGame adds it after the lobby's slots
#define APOCALYPSE_PLAYER_NAME "PlyrZombies"

/// the zombie side's colour, day and night: olive drab
#define APOCALYPSE_ZOMBIE_COLOR ((Int)0xFF6B8E23)

/// what spawns; an ObjectReskin in FixesReforged.ini
#define APOCALYPSE_ZOMBIE_TEMPLATE "ApocalypseZombie"

/** Forget the last match and start this one in the game's ApocalypseMode (off without a game), sized
	  by its non-observer seats.  Called from GameLogic::startNewGame on every start, a loaded save
	  included; the save's own state follows. */
extern void Apocalypse_newGame( const GameInfo *game );

/** The ApocalypseMode of the match in progress. */
extern Int Apocalypse_mode( void );

/** Bring in whatever is due on this logic frame.  Called from GameLogic::update. */
extern void Apocalypse_tick( void );

/** The wave counter, the next spawn and whose turn it is, for a save; gameLogicVersion is the
	  version of the GameLogic block it sits in, 18 or later. */
extern void Apocalypse_xfer( Xfer *xfer, UnsignedByte gameLogicVersion );

/** The zombie side is never defeated: it starts the match with nothing, and the match ends when it
	  is the last alliance standing. */
extern Bool Apocalypse_isZombiePlayer( const Player *player );

/** Tell the players how long they lasted.  Called once, on the frame the match is decided. */
extern void Apocalypse_announceEnd( UnsignedInt endFrame );

/** How many zombies wave n (from 1) sends against this many players. */
extern Int Apocalypse_waveSize( Int wave, Int players );

/** Frames between two zombies of the steady stream at level n (from 1) against this many players. */
extern UnsignedInt Apocalypse_streamInterval( Int level, Int players );

/** The most zombies alive at once against this many players; a spawn past it is skipped. */
extern Int Apocalypse_maxLive( Int players );

#endif // __APOCALYPSE_H_
