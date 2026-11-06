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

// FILE: OnlineQueues.h ///////////////////////////////////////////////////////////////////////////
// Desc:   EA's GameSpy queue interfaces answered from the online service.  What the game glue and the
//         WOL screens call; the adapters behind it are OnlinePeer, OnlineBuddy and OnlineStats.
///////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#ifndef _ONLINE_QUEUES_H_
#define _ONLINE_QUEUES_H_

#include "Lib/BaseType.h"
#include "Common/AsciiString.h"
#include "GameNetwork/NetworkDefs.h"

class GameSpyPeerMessageQueueInterface;
class GameSpyBuddyMessageQueueInterface;
class GameSpyPSMessageQueueInterface;
class PingerInterface;
class GameResultsInterface;

/* True when -online named a server.  Every other call here is harmless without it: the factories still
	 make working (empty) queues, the getters answer FALSE or empty, the senders do nothing. */
Bool OnlineIsEnabled( void );

/* One per GameSpy factory (PeerDefs.cpp's SetUpGameSpy, GameEngine.cpp's TheGameResultsQueue).  The
	 queues need no thread: startThread/startThreads only mark them running, requests go to the server
	 when added, and every getResponse pumps the connection first.  All of them share one connection. */
GameSpyPeerMessageQueueInterface *OnlineCreatePeerQueue( void );
GameSpyBuddyMessageQueueInterface *OnlineCreateBuddyQueue( void );
GameSpyPSMessageQueueInterface *OnlineCreatePSQueue( void );
PingerInterface *OnlineCreatePinger( void );					///< answers every ping at once, 1 ms: the relay is the only route
GameResultsInterface *OnlineCreateGameResults( void );	///< drops EA's results: the server rates from OnlineSendReport

/* The connection.  OnlineUpdate opens it on the first call (TLS to -online, then hello) and reads what
	 arrived; call it once a frame from the main thread while anything online is up.  Not needed while a
	 screen polls a queue (getResponse pumps too), but the welcome is waited for before any queue exists. */
void OnlineUpdate( void );

enum OnlineStatus
{
	ONLINE_OFF,					///< no -online, or OnlineDisconnect ran
	ONLINE_CONNECTING,	///< TLS or hello under way
	ONLINE_WELCOMED,		///< welcome arrived: OnlineGetMOTD/OnlineGetConfig hold it, login can be sent
	ONLINE_FAILED,			///< the connection failed or closed; the next login request (or OnlineDisconnect) dials again
};
OnlineStatus OnlineGetStatus( void );
AsciiString OnlineGetMOTD( void );				///< welcome.motd, for SetUpGameSpy's motdBuffer
AsciiString OnlineGetConfig( void );			///< welcome.config (<QMMaps>), for SetUpGameSpy's configBuffer
void OnlineDisconnect( void );						///< hangs up; the next OnlineUpdate dials again
/* The welcome said testMode, which a server sends only when started for a test.  Only then does the
	 -control socket run world commands in an online match: anywhere else they are a cheat. */
Bool OnlineIsTestServer( void );
/* welcome.registerUrl, the site's page where accounts are made (the game only logs in); empty when the
	 server sent none, and the login screen then hides Create Account. */
AsciiString OnlineGetRegisterUrl( void );
/* Opens an http or https address in the player's browser; FALSE when it is not one or nothing could open
	 it.  Windows asks the shell; elsewhere the platform layer's hook, SDL_OpenURL, set by SdlGameEngine. */
Bool OnlineOpenURL( const char *url );
#if !defined(_WIN32)
typedef bool (*OnlineOpenURLHook)( const char *url );
extern OnlineOpenURLHook TheOnlineOpenURLHook;
#endif

/* The relay.  Both are FALSE until a login succeeded (auth.ok).  The ticket goes into the UDP HELLO_AUTH,
	 which has to be sent right after login: the peer queue answers PEERREQUEST_LOGIN only once the server
	 says relay.ok, and puts that virtual IP in PEERRESPONSE_LOGIN's internalIP and externalIP. */
Bool OnlineGetRelayTicket( UnsignedByte out[16] );
Bool OnlineGetRelayEndpoint( AsciiString &host, UnsignedShort &port );		///< -online's host, welcome.relayPort
UnsignedInt OnlineGetRelayVip( void );		///< relay.ok's virtual IP, 0 before it

/* Session tokens replace EA's stored password.  After a login, OnlineGetSessionToken is the token the
	 server gave (64 hex characters, too long for BuddyRequest's password field); OnlineSaveToken keeps it
	 under the login name in GeneralsOnline/OnlineTokens.ini in the user data folder, an empty token forgets
	 it.  OnlineResume logs in with a saved token instead of BUDDYREQUEST_LOGIN; the answers are the same
	 (BUDDYRESPONSE_LOGIN, then PEERRESPONSE_LOGIN, or PEERRESPONSE_DISCONNECT on a refusal). */
AsciiString OnlineGetSessionToken( void );
AsciiString OnlineGetSavedToken( const AsciiString &login );
void OnlineSaveToken( const AsciiString &login, const AsciiString &token );
void OnlineResume( const AsciiString &token );

/* The match result, EA's ladder packet (StagingRoomGameInfo.cpp's generateLadderGameResultsPacket) plus
	 endFrame and surrendered.  Sent for the match the last game.start or qm.matched named, at most once per
	 match however often it is called, so the end-of-match frame and the score screen can both call it. */
struct OnlineReport
{
	Int seed;
	Int endFrame;							///< TheVictoryConditions->getEndFrame(); every report of a match must agree on it
	Int winningTeam;					///< index of the winner in the match's player order (qm.matched/game.start ids;
														///< a Quick Match's slot number), -1 for none.  Not EA's team number.
	Bool sawDesync;
	Bool sawGameEnd;
	Bool disconEnd;
	Bool surrendered;					///< the local player conceded
	Int duration;							///< EA's duration, which is the end frame again
	Int numPlayers;						///< how many of discon[] and side[] are filled
	Bool discon[MAX_SLOTS];
	AsciiString side[MAX_SLOTS];

	OnlineReport();
};
void OnlineSendReport( const OnlineReport &report );

#endif // _ONLINE_QUEUES_H_
