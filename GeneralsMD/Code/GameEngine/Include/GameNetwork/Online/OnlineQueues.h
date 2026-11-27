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
/* OnlineUpdate for an open connection only, dialling nothing: GameClient::update calls it every pass, so a
	 notice or the server's ping is read in a match too, where no online screen pumps. */
void OnlinePumpIfConnected( void );

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
/* welcome.accountUrl, the site's account page (the Skins tab sends the player there); empty when the
	 server sent none. */
AsciiString OnlineGetAccountUrl( void );
/* Players signed in, open custom games and players in the Quick Match queue, from welcome.counts and the
	 counts line the server sends a signed-in client every 30 s.  FALSE (all three 0) until a server that
	 sends them did, and again once the connection is dropped. */
Bool OnlineGetCounts( Int &players, Int &games, Int &queue );
/* welcome.server{name, city}, UTF-8 (convert before showing), and the round trip to the server in ms,
	 smoothed over our own ping{n} every 10 s from the welcome on; -1 until the first pong, and for good
	 from a server that does not know ping.  FALSE when the welcome carried no server object (an older
	 server, or one that has no name set); the names are empty then, the round trip still counts. */
Bool OnlineGetServerInfo( AsciiString &name, AsciiString &city, Int &pingMs );
/* The code of the last login the server refused (auth.fail: badlogin, throttled, expired...), and its
	 retry in seconds, 0 when it gave none.  Empty when the last login never reached a refusal (the
	 connection failed), and cleared by the next login.  `file` gets the install file a "files" refusal names,
	 `until` a maintenance or ban end as UTC text, `reason` a ban's reason. */
AsciiString OnlineGetLoginFailure( Int *retrySeconds, AsciiString *file = NULL, AsciiString *until = NULL, AsciiString *reason = NULL );

/* The server's notice (notice{kind,en,tr,until}, after the welcome and whenever it changes), shown as a
	 banner across the top of every screen.  en and tr are UTF-8 and until is as the server wrote it, usually
	 ISO 8601 in UTC.  FALSE with none: never sent, cleared by one with no words, or the connection gone.
	 Without one from the server, -testNotice maintenance|info stands in, for a proof shot with no server.
	 serial moves on with every change. */
struct OnlineNotice
{
	Bool maintenance;					///< kind maintenance; anything else is info
	AsciiString en, tr;
	AsciiString until;
	UnsignedInt serial;
};
Bool OnlineGetNotice( OnlineNotice &out );
/* The notice's words in a TextLanguageType: Turkish for Turkish, English for every other, and the other
	 language when that one is empty.  Pure, for the test. */
AsciiString OnlineNoticeWords( const OnlineNotice &notice, Int language );
/* An ISO 8601 time with its zone (2026-11-18T22:00:00Z, .000Z, +03:00) as Unix seconds; -1 for anything
	 else.  Pure, for the test. */
long long OnlineParseUtc( const char *iso );
/* until as the player reads it: the local time, 22:00 today, the date before it on another day; the text
	 itself when it is no time this reads; empty for none. */
UnicodeString OnlineUntilText( const AsciiString &until );
/* "Online play is down for maintenance", with when it should be back where until says. */
UnicodeString OnlineMaintenanceText( const AsciiString &until );
/* A new lobby, game, Quick Match or Play vs AI the server refused with code maintenance (error, qm.status
	 or ai.refused): TRUE once, with its until, for the screen whose request it was.  The next request of those
	 kinds drops one nobody took.  The host's start has its own, OnlineTakeStartRefusal. */
Bool OnlineTakeMaintenanceRefusal( AsciiString &until );
/* OnlineTakeMaintenanceRefusal in EA's message box, the maintenance sentence in it; FALSE, and no box, when
	 there was none (or no -online). */
Bool OnlineShowMaintenanceRefusal( void );
/* The host's Start went while maintenance is on: the server may refuse the match (error{for:game.start,
	 code:maintenance}), so the host launches on the server's game.start, which arrives as PEERRESPONSE_GAMESTART
	 the way a guest's does, and not on its click. */
Bool OnlineIsHostStartWaiting( void );
/* That start refused (error{for:game.start}: maintenance, nothost, throttled...): TRUE once, with the code and
	 until (the notice's when the line has none), for the staging screen that asked. */
Bool OnlineTakeStartRefusal( AsciiString &code, AsciiString &until );
/* A login waits on the install files' hashes (the first start on a machine reads every file): the login
	 screen says it is checking them. */
Bool OnlineIsCheckingFiles( void );
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
	AsciiString map;							///< the map's path as the game holds it (a generated one's is under the user folder);
																///< only maps\<folder>\<file> goes up, for the match history, and nothing when that is
																///< over 255 bytes or not UTF-8
	Int speed;										///< the game speed the match played, percent of retail

	OnlineReport();
};
Bool OnlineSendReport( const OnlineReport &report );	///< TRUE when it went out, FALSE for no match or one already reported
/* The Quick Match the last game.start named was called off before it loaded (WOLQuickMatchMenu's map
	 check): qm.abort, which the server takes as leaving inside its grace, a no contest. Nothing for no
	 match or one already reported; an older server answers an error that is only logged. */
void OnlineCallOffQuickMatch( const char *reason );

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

/* TRUE while the last match's report went out and the server may not have it yet: the replay its
	 recording claimed is not queued yet, or, from a server whose auth.ok said acks, report.ok/err (and
	 replay.ok/err when this client sent the replay) has not come back for the match.  An older server
	 confirms nothing, and there it is the connection still holding lines it has not written.  FALSE with
	 no report for the match, and once the connection is gone (nothing left to wait for).  The score
	 screen holds a "sending results" box on it. */
Bool OnlineIsSendingResults( void );

/* What the server has said about a player, by nick: the round trip to the server from `player` events
	 (ping), the site's rating, tier and games from `player`, auth.ok and stats.  A number nobody sent is
	 -1 (rank 0); hasRating is FALSE until a rating arrived.  An older server sends none of it, and the
	 screens then show what they always showed. */
struct OnlinePlayerInfo
{
	Int ping;
	Bool hasRating;						///< a rating or an Elo arrived
	Int rating;								///< shown() rounded, for the screens that print %d
	Real exactRating;					///< the site's old-scale rating, two decimals (an older server sends it whole)
	Bool hasElo;							///< the server sent the ladder's Elo beside the rating; an older one does not
	Real elo;									///< the ladder's Elo, whole
	Real shown( void ) const { return hasElo ? elo : exactRating; }	///< the figure the screens show
	AsciiString tier;					///< the site's tier key ("major"); OnlineTierName says it in words
	Int games;
	Int wins;
	Int losses;
	Int rank;									///< ladder position, 0 for none
	Bool notOnline;						///< the last stats asked for this nick came back noplayer
	Bool hasStats;						///< a stats answer for this nick arrived (wins and losses are then known)
	Bool hasAIRating;					///< aiRating and aiGames arrived (auth.ok, stats, ai.rated)
	Real aiRating;						///< the Bot ELO, the rating against the AI (aiElo, or an older server's aiRating mapped onto Elo)
	Int aiGames;
	Int aiWins;
	UnsignedInt serial;				///< moves on whenever this player's numbers change: redraw on it, not on everyone's

	OnlinePlayerInfo();
};
Bool OnlineGetPlayerInfo( const AsciiString &nick, OnlinePlayerInfo &out );		///< FALSE: nothing heard about the nick (any case)
void OnlineRequestStats( const AsciiString &nick );		///< stats{nick}; the answer lands in OnlineGetPlayerInfo
UnsignedInt OnlinePlayerInfoSerial( void );						///< moves on whenever a player's numbers change
UnicodeString OnlineTierName( const AsciiString &tier );		///< GUI:OnlineTier_<key>, or the key itself
UnicodeString OnlineRatingText( const OnlinePlayerInfo &info );	///< "Major, ELO 1213", "ELO 1213" (or an older server's "rating 21"), "Major", or empty

/* The ladder, 20 rows a page: OnlineRequestLadder(offset) asks, OnlineGetLadder reads the last answer. */
struct OnlineLadderRow
{
	Int rank;									///< 0 when the server sent none
	AsciiString nick;
	Bool hasRating;						///< a rating or an Elo arrived
	Int rating;								///< shown() rounded, for the screens that print %d
	Real exactRating;					///< the old scale, two decimals
	Bool hasElo;							///< the row carried elo
	Real elo;
	Real shown( void ) const { return hasElo ? elo : exactRating; }	///< the figure the screens show
	AsciiString tier;
	Int wins;
	Int losses;
	Int side;									///< qm.matched's opponents only: a PlayerTemplate index, a Random pool below -1, or -1 random
	Int color;								///< qm.matched's opponents only: a MultiplayerColor index, or -1 random

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
	Bool hasChance;						///< qm.matched carried chance (an older server does not)
	Real chance;							///< the local player's win chance, 0 to 1
	/* The match speed, percent of retail: qm.matched's speed (100 from an older server), then qm.speed's.
		 When the picks differ the slower player is asked (speedAsk, the faster pick) while the faster one waits
		 (speedWait); speedFinal says qm.speed came, after which the reveal's countdown starts again. */
	Int speed;
	Int speedAsk;							///< the speed this player is asked to accept, 0 for none
	Int speedWait;						///< the speed the opponent is asked to accept, 0 for none
	Bool speedFinal;
	UnsignedInt serial;
};
Bool OnlineGetMatchReveal( OnlineMatchReveal &out );
/* The asked player's answer, qm.speedok{match, ok}.  The server declines by itself SPEED_ASK_MS after
	 qm.matched, so a box left open past ONLINE_SPEED_ASK_MS closes as a decline. */
enum { ONLINE_SPEED_ASK_MS = 15000 };
void OnlineAnswerSpeedAsk( Bool ok );
/// a speed off the wire: one of PLAYER_GAME_SPEED_PERCENTS, anything else (or none) retail's 100
Int OnlineMatchSpeed( long long percent );

/* The band of win chances the search accepts now, from the last qm.status's window: chanceLo and
	 chanceHi in percentage points, 0 to 100 (45 to 55 at join, 30 to 70 at the widest), not ratings and
	 not the reveal's 0-to-1 chance.  nextMs is the milliseconds left to the next widening, counted down
	 from the line's arrival (never below 0), and -1 at the widest.  FALSE when the last qm.status carried
	 no window (an older server, or no search since the last match). */
Bool OnlineGetQuickMatchChanceWindow( Real &chanceLo, Real &chanceHi, Int &nextMs );

/* The abandon lock's whole seconds left, counted down here, 0 when free: from auth.ok's and the player's
	 own card's lockSeconds and qm.status{locked}'s seconds.  It is also how the Quick Match page tells a
	 lock from its other refusals: qm.status locked arrives as QM_COULDNOTFINDBOT like busy and a server
	 error, and only a lock leaves this above 0 (at least 1 when the status says locked).  An older server
	 sends no seconds, and a lock from it reads as any other refusal. */
Int OnlineGetQuickMatchLockSeconds( void );

/* A player's recent matches: OnlineRequestHistory asks (id 0 is the signed-in player, limit 1 to 20;
	 nothing is sent while an ask for the same id and mode is still out), OnlineGetHistory reads the answer
	 kept for that id and mode, in the ladder's three states: WAITING while an ask is out (out still holds
	 the answer before it, if any), READY, FAILED for an error, no answer in ONLINE_HISTORY_TIMEOUT_MS
	 (20 s), or nothing asked.  arrived says whether out holds an answer at all.  OnlineGetHistorySerial is
	 the cheap per-frame check: it moves whenever that key's state or answer changes (a second answer
	 too), 0 for a key never asked; copy the rows only when it moved.  An army is a player template name,
	 random already resolved, as OnlineSendAIResult sends it. */
enum OnlineHistoryMode
{
	ONLINE_HISTORY_ALL,
	ONLINE_HISTORY_QM,
	ONLINE_HISTORY_AI,
};
struct OnlineHistoryRow
{
	Bool ai;									///< against the computer; else a Quick Match
	Bool won;
	AsciiString map;
	AsciiString opponent;			///< empty against the computer
	Real opponentRating;			///< against the computer, the Elo it played at (the site's engine-scale figure mapped onto Elo)
	AsciiString playerArmy;
	AsciiString opponentArmy;
	Int seconds;							///< the match's length
	Real change;							///< the player's rating change
	Int age;									///< seconds ago, when the server answered
	Bool hasElo;							///< a Quick Match row with opponentElo: opponentRating and change are then Elo

	OnlineHistoryRow() : ai( FALSE ), won( FALSE ), opponentRating( 0.0f ), seconds( 0 ), change( 0.0f ), age( 0 ), hasElo( FALSE ) {}
};
struct OnlineHistoryArmy
{
	AsciiString army;
	Int games;
	Int wins;
};
struct OnlineHistory
{
	Bool arrived;
	Int id;										///< the account the answer is for
	Int games;								///< in this mode, all of them, not only the rows
	Int wins;
	std::vector<OnlineHistoryRow> rows;		///< in the server's order, newest first
	std::vector<OnlineHistoryArmy> byArmy;				///< the player's own army
	std::vector<OnlineHistoryArmy> byOpponentArmy;

	OnlineHistory() : arrived( FALSE ), id( 0 ), games( 0 ), wins( 0 ) {}
};
void OnlineRequestHistory( Int id, OnlineHistoryMode mode, Int limit = 5 );
OnlineLadderState OnlineGetHistory( Int id, OnlineHistoryMode mode, OnlineHistory &out );
UnsignedInt OnlineGetHistorySerial( Int id, OnlineHistoryMode mode );

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

/* A seat of an online match that went silent is held for its player to come back instead of dropped after
	 NetworkSilentPlayerDropTime: the match stays frozen on the disconnect screen, at most this long counted
	 from the seat's last packet, and a unanimous vote still drops it at once (DisconnectManager).  The
	 server's match.hold and match.resume about other seats are only logged: each peer holds a silent seat on
	 its own clock.  OnlineTakeRelayTicket: TRUE once when a match.hold naming us carried a new relay ticket
	 (the relay lost our session while our line stayed up), which Transport::pumpRelay sends in a HELLO_AUTH. */
enum { ONLINE_RECONNECT_HOLD_MS = 4 * 60 * 1000 };
Bool OnlineTakeRelayTicket( UnsignedByte out[16] );

/* A match this account dropped out of that the others still play, kept bound to the account by the
	 server until it settles.  OnlineGetRejoinable reads the sign-in's match.rejoinable{match, ranked, map,
	 players[{id, nick, side, present}], elapsed, logPeer} for the hub's card; FALSE when none came, or once
	 the match was rated, unrated or refused as settled.  map is a custom game's map path, empty for a Quick
	 Match (made from the seed); side is the seat's Quick Match pick, -1 random or unknown; elapsedSeconds
	 counts on from the line's arrival.  serial moves with every change.
	 OnlineRequestRejoin sends match.rejoin{match} for it; OnlineGetRejoin reads the answer, WAITING while it
	 is out, then READY with match.rejoin.ok as a RejoinInfo, or REFUSED with match.refused's code (crc,
	 settled, nomatch, norelay; signedout with no line, none with no match offered), each handed out once,
	 after which it reads IDLE.  READY also makes the match the session's own (reports, replays, holds). */
struct OnlineRejoinSeat
{
	long long id;
	AsciiString nick;
	Int side;
	Bool present;		///< still in the match: no report, no leave
};
struct OnlineRejoinable
{
	AsciiString match;
	Bool ranked;
	AsciiString map;
	std::vector<OnlineRejoinSeat> players;
	Int elapsedSeconds;
	long long logPeer;		///< a present player's account id, the one to fetch the match so far from
	UnsignedInt serial;
};
Bool OnlineGetRejoinable( OnlineRejoinable &out );

/* match.rejoin.ok as the game takes it: the seats in game.start's order, this one at me with its new relay
	 vip, the build the match started on, and its setup (seed, sides, colors, skins for a Quick Match; map and
	 opts, the host's game options string, for a custom game).  vips are the server's numbers, as game.start's. */
struct RejoinInfo
{
	AsciiString match;
	Bool ranked;
	std::vector<long long> ids;
	std::vector<AsciiString> nicks;
	std::vector<UnsignedInt> vips;
	Int me;
	UnsignedInt exe, ini;
	long long logPeer;
	Int elapsedSeconds;
	Int seed;
	std::vector<Int> sides, colors;		///< clamped as a Quick Match seat's
	std::vector<AsciiString> skins;		///< each seat's "K1.3", empty for none
	AsciiString map, opts;
	Int speed;		///< the match's game speed as the server set it up, 0 from a server that sends none

	RejoinInfo() : ranked( FALSE ), me( -1 ), exe( 0 ), ini( 0 ), logPeer( 0 ), elapsedSeconds( 0 ), seed( 0 ), speed( 0 ) {}
};
enum OnlineRejoinState
{
	ONLINE_REJOIN_IDLE,
	ONLINE_REJOIN_WAITING,
	ONLINE_REJOIN_READY,
	ONLINE_REJOIN_REFUSED,
};
void OnlineRequestRejoin( void );
OnlineRejoinState OnlineGetRejoin( RejoinInfo &out, AsciiString &refusal );

/* The game side of a rejoin: load the match info describes, fetch its frames from logPeer and catch up
	 while the others wait.  The hub calls it once with match.rejoin.ok's info.  A stub that only logs until
	 the netcode lands (OnlinePeer.cpp). */
void OnlineStartRejoin( const RejoinInfo &info );

/* The server's verdict on the match the last game.start or qm.matched named: rated (delta, rating, or
	 before and after, which give the same two) or unrated (reason).  FALSE until it arrives.  With elo
	 given, a verdict that carries eloAfter (a server with Elo) gives eloDelta and eloAfter instead and sets
	 it TRUE; one without leaves the old scale's and sets it FALSE. */
Bool OnlineGetMatchResult( Bool &rated, Real &delta, Real &rating, AsciiString &reason, Bool *elo = NULL );

/* Play vs AI, a 1v1 skirmish the server rates on Elo (the card's aiRating, its aiElo).  Every AI figure
	 here is an Elo: the bot's is ai.ready's botElo, the verdict's eloBefore and eloAfter; from a server
	 that sends only the old 0 to 30 aiRating, before and after, they are mapped onto Elo (aiRatingOfLegacy).
	 OnlineRequestAIMatch sends ai.start{army, exe, ini, speed}; OnlineGetAIMatch reads the answer and hands it out
	 once: READY with ai.ready's match, the AI seat's rating and the map and seed the server picked, then
	 the match is live; or REFUSED with ai.refused's reason ("build": this exe or its data is not the
	 release's), "unavailable" from a server without it and "signedout" when the login went.
	 While live, OnlineSendAITick sends ai.tick{match, frame} every ONLINE_AI_TICK_SECONDS of wall time and
	 OnlineSendAIResult sends ai.result{match, result, seconds since ai.ready, endFrame, blind, playerArmy,
	 aiArmy, speed, stats{...}} once, which ends it.  OnlineDropAIMatch ends it with nothing sent (it could not start).  A win's recording goes up
	 as a Quick Match's does: OnlineClaimAIMatch as it starts, OnlineSendReplay as it closes, and a loss's
	 is never sent.  A logout ends it too; the server settles a match it never heard the end of.
	 OnlineGetAIVerdict reads the last ai.rated{before, after, games}, or ai.unrated{reason} or a result's
	 ai.refused{matchId, reason}, both unrated; ai.rated also moves the card's aiRating, aiGames and aiWins
	 (its wins when it carries them, else one more after a win this client reported). */
enum OnlineAIMatchState
{
	ONLINE_AI_IDLE,
	ONLINE_AI_WAITING,		///< ai.start is out
	ONLINE_AI_READY,
	ONLINE_AI_REFUSED,
};
enum { ONLINE_AI_TICK_SECONDS = 30, ONLINE_TEAM_SEATS = 4 };	///< ONLINE_TEAM_SEATS: the humans of a 4v4
struct OnlineAIMatch
{
	AsciiString match;
	Real aiRating;				///< the AI seat's Elo, the strength it plays at
	AsciiString map;
	Int seed;
	Int speed;						///< the speed ai.ready echoed, 100 from an older server
	AsciiString reason;		///< REFUSED only
	/// Team vs AI: humans > 1, every human seat's nick, relay address and army (a player template name or
	/// Random) in the server's order, all on one team, and bots seats after them on the other at aiRating
	Int humans, bots;
	AsciiString nick[ ONLINE_TEAM_SEATS ];
	UnsignedInt vip[ ONLINE_TEAM_SEATS ];
	AsciiString army[ ONLINE_TEAM_SEATS ];
};
void OnlineRequestAIMatch( const AsciiString &army, Int speed = 100 );
/* READY or REFUSED is handed out once.  A Team vs AI match's READY comes to every member of the party, the
	 one who pressed Play and the others alike, so the hub reads it whether it asked or not. */
OnlineAIMatchState OnlineGetAIMatch( OnlineAIMatch &out );
Bool OnlineIsTeamAIMatchReady( void );		///< a team match's ai.ready waits: the hub of a member who did not press Play takes it too
/* Team vs AI (TEAM-VS-AI.md).  A party is made by OnlineInviteFriend from outside a room (an invite of mode
	 teamai and game 0, answered by OnlineAnswerInvite as any), and lasts while two members are signed in.
	 OnlineGetParty gives its members, the leader first (empty out of one), and a serial as OnlineGetFriends'.
	 While in one, Play vs AI (OnlineRequestAIMatch) starts the party against as many bots: the server asks
	 every other member's hub first, which OnlineTakeTeamAsk reads once and OnlineAnswerTeamAsk answers with
	 the army that seat plays.  OnlineStartTeamAIMatch builds and launches the network match of a READY with
	 humans > 1; FALSE, with nothing started, when the map is not in this game. */
struct OnlinePartyMember
{
	Int id;
	AsciiString nick;
};
Bool OnlineTeamAIAvailable( void );
/* The match is a team's from a team ai.ready, until OnlineEndTeamAIMatch: the score screen sends no report for
	 a match with computer seats (EA's rule), so GameClient's updateOnline sends a leaver's itself. */
Bool OnlineIsTeamAIMatch( void );
void OnlineEndTeamAIMatch( void );
UnsignedInt OnlineGetParty( std::vector<OnlinePartyMember> &out );
void OnlineLeaveParty( void );
Bool OnlineTakeTeamAsk( AsciiString &match, AsciiString &fromNick );
void OnlineAnswerTeamAsk( const AsciiString &match, const AsciiString &army );
Bool OnlineStartTeamAIMatch( const OnlineAIMatch &ai );
/* The Team vs AI queue: ai.team.queue{size 2..4, army, exe, ini, speed} queues us, our whole party with us,
	 for a team of size humans the server builds by team-AI Elo; its match's READY comes to the hub as a party's
	 does.  OnlineGetTeamAIQueue is the size we wait for, 0 out of the queue (ai.team.queued{size}). */
void OnlineQueueTeamAI( Int size, const AsciiString &army, Int speed );
void OnlineLeaveTeamAIQueue( void );
Int OnlineGetTeamAIQueue( void );
Bool OnlineIsAIMatchLive( void );
void OnlineDropAIMatch( void );
void OnlineSendAITick( Int frame );
struct OnlineAIResult
{
	Bool won;
	Int endFrame;
	Int blind;									///< the player's orders at objects he could not see, as a Quick Match report counts them
	AsciiString playerArmy;			///< player template names, random already resolved
	AsciiString aiArmy;
	Int unitsBuilt, unitsLost, unitsKilled;		///< the player's numbers on the score screen
	Int structuresBuilt, structuresLost, structuresKilled;
	Int moneyEarned;

	OnlineAIResult() : won( FALSE ), endFrame( 0 ), blind( 0 ), unitsBuilt( 0 ), unitsLost( 0 ), unitsKilled( 0 ),
		structuresBuilt( 0 ), structuresLost( 0 ), structuresKilled( 0 ), moneyEarned( 0 ) {}
};
void OnlineSendAIResult( const OnlineAIResult &result );
AsciiString OnlineClaimAIMatch( void );
struct OnlineAIVerdict
{
	AsciiString match;
	Bool rated;
	Real before, after;
	Int games;
	AsciiString reason;		///< unrated only
	UnsignedInt serial;		///< moves on with every verdict
};
Bool OnlineGetAIVerdict( OnlineAIVerdict &out );		///< FALSE before the first this sign-in
/* The match of the last ai.result this client sent (the server's 16 hex digits, so text, not a number),
	 kept after the match ends; empty before one this sign-in.  The score screen shows a verdict only when
	 its match equals this. */
AsciiString OnlineGetLastAIMatchId( void );
/* The server's id of the internet match this client plays or comes back to (game.start, qm.matched,
	 match.rejoin.ok), empty for none.  A seat's rejoin checkpoints are filed under it, so the copy that
	 comes back finds them before any replay names the match. */
AsciiString OnlineCurrentMatchId( void );

/* The mode the custom lobby's Host button was answered with is TheHostGameMode (GUIUtil.h).  It goes out
	 in game.host, the host popup leaves Limit Armies out under Direct Strike, and the staging room the host
	 opens stands on that mode's map from the start.  A joiner reads the mode off the host's map instead
	 (IsDirectStrikeMap, IsApocalypseMap), and every game.opts the host sends names the mode and seed that
	 map carries ("classic", "directstrike" or "apocalypse"), for the server's records and room list only. */

/* Friends and invites.  The hello says friends:1; a server that echoes it in the welcome has them.  Against
	 any other server OnlineFriendsAvailable is FALSE, every call below sends nothing, the lists stay empty
	 and the buddy queue answers nothing beyond the login and private messages, as before friends.
	 EA's buddy queue is served too: ADDBUDDY (by profile id), DELBUDDY, OKADD and DENYADD become friend
	 lines, an accepted friend's presence arrives as BUDDYRESPONSE_STATUS (offline GP_OFFLINE "Offline",
	 online GP_ONLINE "Online", lobby GP_CHATTING "Chatting", room GP_STAGING "Staging" with the room's name
	 as location, match GP_PLAYING "Playing") and a request to us as BUDDYRESPONSE_REQUEST.  A removal and an
	 invite reach only the calls below.  SETSTATUS sends nothing: the server works presence out itself. */
Bool OnlineFriendsAvailable( void );
enum OnlineFriendStatus { ONLINE_FRIEND_ACCEPTED, ONLINE_FRIEND_PENDING_IN, ONLINE_FRIEND_PENDING_OUT, ONLINE_FRIEND_BLOCKED };
enum OnlinePresence { ONLINE_PRESENCE_OFFLINE, ONLINE_PRESENCE_ONLINE, ONLINE_PRESENCE_LOBBY, ONLINE_PRESENCE_ROOM, ONLINE_PRESENCE_MATCH };
struct OnlineFriend
{
	Int id;
	AsciiString nick;
	OnlineFriendStatus status;
	OnlinePresence presence;	///< offline for anything but an accepted friend
	Int game;									///< in a room: its game id, else 0
	UnicodeString roomName;
	Bool joinable;						///< the room has a free seat and has not started
	Bool locked;							///< the room has a password (an invite waives it)
};
/* Every row, accepted friends first, then requests to us, ours out, blocked; by nick inside each.  Returns
	 a serial that moves on with every change (0 before any list this sign-in). */
UnsignedInt OnlineGetFriends( std::vector<OnlineFriend> &out );
Int OnlineCountFriendsOnline( void );						///< accepted friends not offline: the FRIENDS key's number
void OnlineRequestFriends( void );							///< friends.get (the sign-in already brings the list)
void OnlineAddFriend( const AsciiString &nick );	///< friend.add; a request back to one waiting from them accepts it
void OnlineAnswerFriend( Int id, Bool ok );			///< friend.answer to a request to us
void OnlineRemoveFriend( Int id );							///< friend.remove: a friend, a request either way, or our block
void OnlineBlockPlayer( Int id );								///< friend.block, listed or not
/* An invite to a custom room, live for ONLINE_INVITE_MS from its arrival unless the room fills, starts or
	 goes first (invite.gone). */
struct OnlineInvite
{
	Int from;
	AsciiString nick;
	Int game;
	UnicodeString roomName;
	AsciiString mode;					///< classic, directstrike or apocalypse
	AsciiString map;
	Int msLeft;
};
UnsignedInt OnlineGetInvites( std::vector<OnlineInvite> &out );	///< oldest first; a serial as OnlineGetFriends'
void OnlineInviteToParty( const AsciiString &nick );	///< invite{nick} into our Team vs AI party, friend or not; outside a room only
void OnlineInviteFriend( Int to );					///< invite{to,game} from the staging room we sit in, invite{to} into our Team vs AI party outside one
/* Answers an invite and forgets it.  ok joins that room as a game.join does, the password waived: the
	 answers are game.join's (PEERRESPONSE_JOINSTAGINGROOM, a refusal as its failure), so the screen that
	 accepts leaves its own room first, as before any join.  An invite already gone answers invite.gone. */
void OnlineAnswerInvite( Int from, Int game, Bool ok );
/* The last refused friend or invite request, once: forWhat friend.add, friend.answer, friend.remove,
	 friend.block, invite, invite.answer or friends.get, code the server's (not_found, already, full, rate,
	 throttled, blocked as not_found, offline, inmatch, nogame, unavailable). */
Bool OnlineTakeFriendError( AsciiString &forWhat, AsciiString &code );

#endif // _ONLINE_QUEUES_H_
