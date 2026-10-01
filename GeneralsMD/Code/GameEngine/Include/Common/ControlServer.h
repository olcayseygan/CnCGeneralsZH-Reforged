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

// FILE: ControlServer.h /////////////////////////////////////////////////////////////////////////
//
// -control [port]: a WebSocket on 127.0.0.1 that drives the game from outside it.
//
// The scenario file answers "play this match the same way twice".  This answers the other half:
// poke the running game and ask it what happened.  Start a skirmish, spawn a worker, send a
// selection somewhere, take a picture, read the frame number and everybody's money back - from
// Python, from a browser console, from anything that speaks WebSocket.
//
// It listens on the loopback address only.  There is no authentication and none is wanted: the
// socket can start a match and create units, so it has to be unreachable from anywhere but this
// machine, and binding to 127.0.0.1 is what makes that true rather than a promise.
//
// Commands are one line of text per frame, and world commands are the same grammar the scenario
// files use, minus the leading frame number:
//
//   spawn <slot> <template> <count> <x> <y> [spacing]
//   move|attackmove <slot> <selector> <x> <y>
//   attack <slot> <selector> <targetSlot> <targetSelector>
//   stop <slot> <selector>
//   teleport <slot> <selector> <x> <y>
//
// plus a handful the files have no use for: ping, status, screenshot, skirmish, quit,
//
//   units <slot> <selector>
//
// which answers at once with the id, template, position and health of everything of that seat the
// selector names, and whether it is built or still going up,
//
//   canbuild <slot> <template> <x> <y> [<x> <y> ...]
//
// which answers at once, for each point, the ground check the logic makes when a placement click
// lands there (0 when the structure would go down),
//
//   step <n>
//
// which hands the logic clock to the client: from the first step on, logic frames run only while a
// step has frames left, and the reply comes once the n-th of them has run.  A training loop reads
// the world, queues its orders, steps, and reads again, and the frames between two reads no longer
// depend on how fast anybody drew.  The clock goes back to the wall when the client disconnects.
//
//   follow <objectId>
//
// locks the camera on one object by the id units gave, so somebody can watch a training run, and
//
//   overlay [text]
//
// writes the text in the top left corner, one line per line of the frame, a line starting '>'
// highlighted, over everything and even while -cinema has the rest of the interface off.  Bare
// overlay takes it down.  And
//
//   key <KEY_name> [ALT] [CTRL] [SHIFT]
//
// which presses and releases one key through the message stream, the way the keyboard does.  It
// runs where it arrives, on the render pass, because that is where a real key arrives too.
//
// Replies are one JSON object per frame.  Every reply carries "ok", and a failed one carries
// "error" saying what was wrong with the command rather than dropping it.
//
// World commands do not take effect where they arrive.  Reading a socket happens on a render pass
// and creating an object has to happen inside a logic frame, so a command is queued by
// ControlServer_poll and carried out by ControlServer_runCommands on the next logic frame.  That is
// also what keeps the game deterministic while something is driving it.
//
///////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#ifndef __CONTROLSERVER_H_
#define __CONTROLSERVER_H_

#include "Lib/BaseType.h"

/** Accept connections, read commands, send replies.  Called once per engine pass. */
extern void ControlServer_poll( void );

/** Carry out the world commands that arrived since the last logic frame.  Called from
	  GameLogic::update, which is the only place it is safe to make an object. */
extern void ControlServer_runCommands( void );

/** Whether a client has taken the logic clock with step.  The engine then runs a logic frame every
	  pass, as -headless does, for as long as the step lasts. */
extern Bool ControlServer_isStepping( void );

/** Whether a stepping client's frames are all spent, so the logic has to wait for the next step. */
extern Bool ControlServer_holdsLogic( void );

/** Close the socket.  Called when the engine shuts down. */
extern void ControlServer_shutdown( void );

/** The WebSocket handshake reply for a client key: SHA-1 of the key and the protocol's own GUID,
	  base64'd.  Public because it is the one piece of this file that can be tested without a socket,
	  and RFC 6455 ships a worked example to test it against. */
extern Bool ControlServer_computeAcceptKey( const char *clientKey, char *out, Int outSize );

#endif // __CONTROLSERVER_H_
