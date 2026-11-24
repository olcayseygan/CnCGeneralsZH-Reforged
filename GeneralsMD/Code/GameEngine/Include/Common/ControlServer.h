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
//
// plus a handful the files have no use for: ping, status, screenshot, skirmish, quit, and
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
// The verbs a reinforcement learner drives an offline skirmish with (a build order against the AI):
//
//   step <frames>          lockstep.  The first step holds the match: from then on the logic runs
//                          only the frames a step asks for (1 to 300), as fast as the machine goes,
//                          and the reply {"ok":true,"cmd":"step","frame":F} comes once they have run.
//                          One step at a time.  Refused in a network match, a replay and a guarded
//                          match; let go by the end of the match.  Under -headless nothing is drawn.
//   step release           back to the wall clock: {"ok":true,"cmd":"step","released":true,"frame":F}
//   objects <slot>         read now: {"ok":true,"cmd":"objects","frame":F,"slot":S,"money":M,
//                          "power":{"produced":P,"consumed":C},"defeated":false,"objects":[{"template",
//                          "id","x","y","kind":"structure|builder|unit","underConstruction","built",
//                          "queue":[names],"headPercent"}, a builder also "idle" and "task":
//                          "idle|build|repair|fortify|supplies|busy"],"count":N,"truncated":false}.
//                          Slot -1 is the map's own structures (civilian and neutral).
//   produce <slot> <building> <unit> <count>
//                          the building a template (each unit to the one of them with the shortest
//                          queue that takes it) or an object id.  {"ok":true,"cmd":"produce",
//                          "queued":N,"producer":id[,"refused":"why the rest did not"]}, or ok false
//                          with "error": the cost and the money, the missing prerequisites by name,
//                          a full queue, an unfinished factory, the unit limit.
//   construct <slot> <template> <x> <y>
//                          an idle builder only, the one nearest the site, named to the dispatcher by
//                          the ObjectID MSG_DOZER_CONSTRUCT takes, which is handed the message inside
//                          the frame for that seat.  {"ok":true,"cmd":"construct","id":site,"builder":
//                          id,"template":"...","cost":C}, or "error": money, prerequisites, the ground
//                          and why ("illegal location (x,y): objects in the way"), or "every builder
//                          is busy: id:task ...".  The -scenario file's construct is unchanged.
//   buildspot <slot> <template> <x> <y> <radius>
//                          read now: the legal spot nearest (x,y) within radius by construct's own
//                          test, {"ok":true,"cmd":"buildspot","x":..,"y":..}, or ok false.
//
// produce and construct reply after the logic frame that ran them, not where they arrive.  In
// lockstep that frame is the first of the next step, so their replies come inside that step, ahead
// of the step's own, in the order sent: a client sends its orders, then step, and reads until the
// reply whose "cmd" is "step".  Like spawn, they act outside the recorded command stream, so a match
// driven this way does not replay; a -scenario file and every replay play exactly as before.
//
// A new match without a new process: leave back to the shell (it lets lockstep go, since its message
// needs a logic frame to land), wait for status to say inGame false, then skirmish.
// -side 0 FactionAmerica -side 1 <faction> -aidiff easy on the command line set the seats it makes.
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

/** Whether a step holds the match in lockstep: GameEngine then runs logic frames only while
	  ControlServer_stepFramesOwed says some are owed, back to back. */
extern Bool ControlServer_isLockstep( void );

/** How many logic frames the running step still owes, 0 between steps. */
extern Int ControlServer_stepFramesOwed( void );

/** Close the socket.  Called when the engine shuts down. */
extern void ControlServer_shutdown( void );

/** The WebSocket handshake reply for a client key: SHA-1 of the key and the protocol's own GUID,
	  base64'd.  Public because it is the one piece of this file that can be tested without a socket,
	  and RFC 6455 ships a worked example to test it against. */
extern Bool ControlServer_computeAcceptKey( const char *clientKey, char *out, Int outSize );

/** Whether an upgrade request may have the socket: no Origin (a script) or a page on this machine.
	  Anything else, "null" included, is a web page reaching for ws://127.0.0.1 from the player's browser. */
extern Bool ControlServer_originAllowed( const char *request );

#endif // __CONTROLSERVER_H_
