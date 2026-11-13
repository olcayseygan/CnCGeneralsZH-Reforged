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

#include <vector>

#include "Lib/BaseType.h"
#include "Common/AsciiString.h"
#include "Common/UnicodeString.h"
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
/* welcome.resetUrl, the site's forgotten-password page; empty when the server sent none, and the login
	 screen then hides Forgot Password. */
AsciiString OnlineGetResetUrl( void );
/* The code of the last login the server refused (auth.fail: badlogin, throttled, expired...), and its
	 retry in seconds, 0 when it gave none.  Empty when the last login never reached a refusal (the
	 connection failed), and cleared by the next login. */
AsciiString OnlineGetLoginFailure( Int *retrySeconds );
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
	Int blind[MAX_SLOTS];					///< orders player n gave at enemy objects it never saw, by this machine's game
	Int crcOdd[MAX_SLOTS];				///< players (indexed as side[]) off the strict majority's CRC at the first mismatch
	Int numCrcOdd;								///< how many of crcOdd[] are filled; 0 with no mismatch or no majority

	OnlineReport();
};
void OnlineSendReport( const OnlineReport &report );

/* Only a Quick Match's replay goes up: the server keeps no other.  An internet game's recording calls
	 OnlineClaimQuickMatch as it starts and gets the qm.matched id waiting for a recording, or empty for a
	 custom game.  The claim takes the id, so it attaches to one recording at most; a custom game's
	 game.start, a staging room, a new search, a reveal that ended and a logout drop an unclaimed one.
	 OnlineSendReplay sends the closed file under the claimed id: "replay" lines of at most
	 ONLINE_REPLAY_CHUNK raw bytes each, base64, seq from 0, the last one last:true, and no answer comes
	 back.  Nothing is sent for an empty id or path (a desynced recording), when not logged in, a second
	 time for one id, for an id an earlier login held, or for a file over ONLINE_REPLAY_MAX bytes or one
	 that cannot be read. */
enum { ONLINE_REPLAY_CHUNK = 4096, ONLINE_REPLAY_MAX = 2 * 1024 * 1024 };
AsciiString OnlineClaimQuickMatch( void );
void OnlineSendReplay( const char *match, const AsciiString &path );

/* What the server has said about a player, by nick: the round trip to the server from `player` events
	 (ping), the site's rating, tier and games from `player`, auth.ok and stats.  A number nobody sent is
	 -1 (rank 0); hasRating is FALSE until a rating arrived.  An older server sends none of it, and the
	 screens then show what they always showed. */
struct OnlinePlayerInfo
{
	Int ping;
	Bool hasRating;
	Int rating;
	AsciiString tier;					///< the site's tier key ("major"); OnlineTierName says it in words
	Int games;
	Int wins;
	Int losses;
	Int rank;									///< ladder position, 0 for none
	Bool notOnline;						///< the last stats asked for this nick came back noplayer
	Bool hasStats;						///< a stats answer for this nick arrived (wins and losses are then known)
	UnsignedInt serial;				///< moves on whenever this player's numbers change: redraw on it, not on everyone's

	OnlinePlayerInfo();
};
Bool OnlineGetPlayerInfo( const AsciiString &nick, OnlinePlayerInfo &out );		///< FALSE: nothing heard about the nick (any case)
void OnlineRequestStats( const AsciiString &nick );		///< stats{nick}; the answer lands in OnlineGetPlayerInfo
UnsignedInt OnlinePlayerInfoSerial( void );						///< moves on whenever a player's numbers change
UnicodeString OnlineTierName( const AsciiString &tier );		///< GUI:OnlineTier_<key>, or the key itself
UnicodeString OnlineRatingText( const OnlinePlayerInfo &info );	///< "Major, rating 21", "Rating 21", "Major", or empty

/* The ladder, 20 rows a page: OnlineRequestLadder(offset) asks, OnlineGetLadder reads the last answer. */
struct OnlineLadderRow
{
	Int rank;									///< 0 when the server sent none
	AsciiString nick;
	Bool hasRating;
	Int rating;
	AsciiString tier;
	Int wins;
	Int losses;

	OnlineLadderRow();
};
enum OnlineLadderState
{
	ONLINE_LADDER_WAITING,		///< asked, no answer yet
	ONLINE_LADDER_READY,
	ONLINE_LADDER_FAILED,			///< error{for:ladder}
};
void OnlineRequestLadder( Int offset );
OnlineLadderState OnlineGetLadder( Int &offset, std::vector<OnlineLadderRow> &rows, OnlineLadderRow &me, Bool &hasMe );

/* Games and wins per army, for the hub's win percentages: OnlineRequestArmies asks, OnlineGetArmies reads
	 the last answer, in the ladder's three states.  side is the PlayerTemplate side ("America"). */
struct OnlineArmyRow
{
	AsciiString side;
	Int games;
	Int wins;
};
void OnlineRequestArmies( void );
OnlineLadderState OnlineGetArmies( std::vector<OnlineArmyRow> &rows );

/* A Quick Match found, between qm.matched and the game.start that launches it (revealMs apart).  A
	 qm.status that follows instead (the opponent dropped, or we left) ends it.  serial tells one reveal from
	 the next. */
struct OnlineMatchReveal
{
	std::vector<OnlineLadderRow> opponents;		///< nick, rating and tier as qm.matched gave them
	AsciiString server;
	Int revealMs;
	UnsignedInt serial;
};
Bool OnlineGetMatchReveal( OnlineMatchReveal &out );

/* The account's skins.  OnlineGetSkins gives what it owns as a SkinLoadout (bit n for skin id n; what it
	 wears is TankSkin_localLoadout), from auth.ok and every skins.ok, and the state of the last ask:
	 OnlineRequestSkins (skins.get, for a code redeemed on the site while signed in) or OnlineSaveSkins
	 (skins.set, the whole loadout, 0 for Default everywhere).  Their skins.ok sets the local loadout too, so
	 the next room or Quick Match seats it; a skins.err changes neither, and error is its code (not_owned,
	 two_for_unit, throttled, unknown, unavailable).  Not signed in: FAILED with "signedout", nothing sent. */
enum OnlineSkinsState
{
	ONLINE_SKINS_READY,
	ONLINE_SKINS_WAITING,
	ONLINE_SKINS_FAILED,
};
void OnlineRequestSkins( void );
void OnlineSaveSkins( UnsignedInt64 wear );
OnlineSkinsState OnlineGetSkins( UnsignedInt64 &owned, AsciiString &error );

/* The server's verdict on the match the last game.start or qm.matched named: rated (delta, rating) or
	 unrated (reason).  FALSE until it arrives. */
Bool OnlineGetMatchResult( Bool &rated, Real &delta, Real &rating, AsciiString &reason );

#endif // _ONLINE_QUEUES_H_
