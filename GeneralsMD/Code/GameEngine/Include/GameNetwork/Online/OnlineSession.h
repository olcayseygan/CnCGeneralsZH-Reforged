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

// FILE: OnlineSession.h //////////////////////////////////////////////////////////////////////////
// Desc:   Inside the online adapters: the one connection they share, what the server has told them,
//         and the three queues it answers into.  The game calls OnlineQueues.h; test_online calls this.
///////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#ifndef _ONLINE_SESSION_H_
#define _ONLINE_SESSION_H_

#include <deque>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "Lib/WideCharFns.h"
#include "GameNetwork/Online/OnlineConnection.h"
#include "GameNetwork/Online/OnlineQueues.h"
#include "GameNetwork/GameSpy/BuddyThread.h"
#include "GameNetwork/GameSpy/PeerThread.h"
#include "GameNetwork/GameSpy/PersistentStorageThread.h"

/* Everything runs on the main thread: a request becomes a line the moment it is added, and a line
	 becomes responses when a queue's getResponse (or OnlineUpdate) pumps.  OnlineConnection's own thread
	 is the only other one, behind its queues. */

class OnlinePeerQueue : public GameSpyPeerMessageQueueInterface
{
public:
	OnlinePeerQueue();
	virtual ~OnlinePeerQueue();
	virtual void startThread( void ) { m_running = TRUE; }
	virtual void endThread( void );
	virtual Bool isThreadRunning( void ) { return m_running; }
	virtual Bool isConnected( void );
	virtual Bool isConnecting( void );
	virtual void addRequest( const PeerRequest& req );
	virtual Bool getRequest( PeerRequest& req ) { return FALSE; }
	virtual void addResponse( const PeerResponse& resp ) { m_responses.push_back( resp ); }
	virtual Bool getResponse( PeerResponse& resp );
	virtual SerialAuthResult getSerialAuthResult( void ) { return SERIAL_OK; }

private:
	std::deque<PeerResponse> m_responses;
	Bool m_running;
};

class OnlineBuddyQueue : public GameSpyBuddyMessageQueueInterface
{
public:
	OnlineBuddyQueue();
	virtual ~OnlineBuddyQueue();
	virtual void startThread( void ) { m_running = TRUE; }
	virtual void endThread( void ) { m_running = FALSE; }
	virtual Bool isThreadRunning( void ) { return m_running; }
	virtual Bool isConnected( void );
	virtual Bool isConnecting( void );
	virtual void addRequest( const BuddyRequest& req );
	virtual Bool getRequest( BuddyRequest& req ) { return FALSE; }
	virtual void addResponse( const BuddyResponse& resp ) { m_responses.push_back( resp ); }
	virtual Bool getResponse( BuddyResponse& resp );
	virtual GPProfile getLocalProfileID( void );

private:
	std::deque<BuddyResponse> m_responses;
	Bool m_running;
};

class OnlinePSQueue : public GameSpyPSMessageQueueInterface
{
public:
	OnlinePSQueue();
	virtual ~OnlinePSQueue();
	virtual void startThread( void ) { m_running = TRUE; }
	virtual void endThread( void ) { m_running = FALSE; }
	virtual Bool isThreadRunning( void ) { return m_running; }
	virtual void addRequest( const PSRequest& req );
	virtual Bool getRequest( PSRequest& req ) { return FALSE; }
	virtual void addResponse( const PSResponse& resp ) { m_responses.push_back( resp ); }
	virtual Bool getResponse( PSResponse& resp );
	virtual void trackPlayerStats( PSPlayerStats stats );
	virtual PSPlayerStats findPlayerStatsByID( Int id );

	void statsArrived( long long id, const std::string &kv );		///< a stats line, as PSRESPONSE_PLAYERSTATS

private:
	std::deque<PSResponse> m_responses;
	std::map<Int, PSPlayerStats> m_stats;
	Bool m_running;
};

/* OnlinePlayerInfo in std::string, which outlives the engine's allocator the way the session does. */
struct OnlineCard
{
	long long ping, games, wins, losses, rank;
	double rating;					///< two decimals, as the site keeps it
	Bool hasRating, notOnline, hasStats;
	Bool hasElo;					///< elo arrived beside rating (a server with Elo sends both)
	double elo;						///< the ladder's Elo, whole
	std::string tier;
	Bool hasAIRating;			///< aiRating and aiGames arrived (auth.ok, ai.rated)
	double aiRating;				///< the Bot ELO, aiElo or an older server's aiRating mapped onto Elo
	long long aiGames, aiWins;
	UnsignedInt serial;		///< the session's cardSerial when this card last changed

	OnlineCard() : ping( -1 ), games( -1 ), wins( -1 ), losses( -1 ), rank( 0 ), rating( 0 ),
		hasRating( FALSE ), notOnline( FALSE ), hasStats( FALSE ), hasElo( FALSE ), elo( 0 ), hasAIRating( FALSE ), aiRating( 0 ), aiGames( 0 ), aiWins( 0 ),
		serial( 0 ) {}
};

/* The key cards are kept under: the nick in lower case.  The server finds a nick without regard to
	 case and answers with the spelling it stored. */
std::string OnlineNickKey( const std::string &nick );

/* Steady-clock milliseconds, which no time zone or clock change moves. */
long long OnlineNowMs( void );

enum { ONLINE_PING_MS = 10000 };		///< our own ping{n} to the server, after the welcome and this often

/* One history{id, mode}: the last answer, kept while a new ask is out, and the state of the last ask. */
typedef std::pair<long long, Int> OnlineHistoryKey;		///< account id, OnlineHistoryMode
enum { ONLINE_HISTORY_TIMEOUT_MS = 20000 };		///< an ask with no answer by then has failed
struct OnlineHistoryEntry
{
	Int state;							///< OnlineLadderState
	OnlineMessage reply;
	UnsignedInt serial;			///< the session's historySerial when state or reply last changed
	long long askedMs;			///< when the ask still out went (OnlineNowMs), 0 with none out

	OnlineHistoryEntry() : state( ONLINE_LADDER_FAILED ), serial( 0 ), askedMs( 0 ) {}
};

/* One friends row as the server keeps it for us (friends{rows[]}, friend{...}): status accepted, pending or
	 blocked, dir in or out for pending, presence offline, online, lobby, room or match, and the room's
	 game, name, joinable and locked while in one. */
struct OnlineFriendRow
{
	long long id;
	std::string nick, status, dir, presence;
	long long game;
	std::string name;
	Bool joinable, locked;

	OnlineFriendRow() : id( 0 ), game( 0 ), joinable( FALSE ), locked( FALSE ) {}
};

/* invite{from,nick,game,name,mode,map}, kept until invite.gone, an answer or ONLINE_INVITE_MS. */
enum { ONLINE_INVITE_MS = 120000 };
struct OnlineInviteEntry
{
	long long from, game;
	std::string nick, name, mode, map;
	long long atMs;					///< OnlineNowMs when it arrived

	OnlineInviteEntry() : from( 0 ), game( 0 ), atMs( 0 ) {}
};

/* A friends row off the wire; FALSE for a line without a usable id. */
Bool OnlineParseFriendRow( const OnlineMessage &m, OnlineFriendRow &out );

struct OnlineSession
{
	static OnlineSession &get();

	/* Tests: lines the adapters send go here instead of out, nothing dials, and the status is WELCOMED. */
	void beginTest( std::vector<OnlineMessage> *capture );

	void send( const OnlineMessage &m );
	void pump();													///< dial if due, then dispatch every line that arrived
	void dispatch( const OnlineMessage &m );			///< one server line into responses
	void disconnect();
	void requestAuth( const OnlineMessage &m );	///< login, register or resume: sent now, or once welcomed

	OnlineConnection *connection;
	std::vector<OnlineMessage> *capture;
	OnlineStatus status;
	std::string motd;				///< std::string, not AsciiString: this outlives the engine's allocator
	std::string config;
	UnsignedShort relayPort;
	Bool testMode;					///< welcome.testMode: OnlineIsTestServer
	std::string registerUrl;	///< welcome.registerUrl: OnlineGetRegisterUrl
	std::string resetUrl;		///< welcome.resetUrl: OnlineGetResetUrl
	std::string accountUrl;	///< welcome.accountUrl: OnlineGetAccountUrl
	OnlineMessage counts;		///< welcome.counts or the last counts line, empty once disconnected: OnlineGetCounts
	OnlineMessage serverInfo;	///< welcome.server{name, city}: OnlineGetServerInfo
	Bool hasServerInfo;
	// our own ping{n} to the server and its pong{n}: OnlineGetServerInfo's round trip
	Int pingN;							///< the n of the last ping sent
	Int pingHeardN;					///< the n of the last pong counted
	long long pingSentMs;		///< when the last ping went, 0 for none this connection
	double pingSmoothedMs;	///< the smoothed round trip, -1 before the first pong
	Bool pingRefused;				///< the server does not know ping (error{for:ping,code:unknown}): no more this connection
	std::string failCode;		///< the last auth.fail's code, cleared by the next requestAuth: OnlineGetLoginFailure
	Int failRetry;					///< its retry, in seconds; 0 when it gave none
	std::string failUntil;	///< its until: maintenance's as the server wrote it, banned's Unix ms as UTC text
	std::string failReason;	///< its reason (auth.fail{code:banned}), empty when the site gave none
	// the server's notice{kind,en,tr,until}: OnlineGetNotice.  Belongs to the connection: an empty one or a
	// sign-out (connectionGone) clears it
	std::string noticeKind, noticeEn, noticeTr, noticeUntil;
	UnsignedInt noticeSerial;	///< moves on whenever the notice changes or goes
	// a new room, game or match refused with code maintenance, until a screen takes it: OnlineTakeMaintenanceRefusal
	Bool maintenanceRefused;
	std::string maintenanceUntil;

	// the account
	Bool authed;						///< auth.ok arrived
	Bool peerLoggedIn;			///< PEERRESPONSE_LOGIN went out (it waits for relay.ok)
	long long id;
	std::string nick;
	std::string token;
	UnsignedByte ticket[16];
	UnsignedInt vip;
	OnlineMessage pendingAuth;		///< sent once the welcome arrives and the install files are hashed
	Bool hasPendingAuth;
	Bool filesSent;					///< this connection has had the install files' hashes (sendFiles)
	std::string failFile;		///< the last auth.fail{code:files}'s file, cleared with failCode
	Bool authWaiting;				///< a login, register or resume is out (or pending) with no answer yet

	// where the player is
	Int groupRoom;					///< 0 outside a chat room; rooms are the server's ids as decimal text
	long long gameId;				///< the staging room, 0 outside one
	long long joiningGame;	///< game.join sent, game.joined not back yet
	Bool hosting;
	Bool hostStartWaiting;	///< the host's game.start went under maintenance: it launches on the server's game.start
	std::string startRefusal;			///< the code of an error{for:game.start} the host waited on: OnlineTakeStartRefusal
	std::string startRefusalUntil;	///< its until, else the notice's
	OnlineMessage hostInfo;	///< CREATESTAGINGROOM's CRCs and ladder, repeated in every game.opts
	std::string hostNick;
	long long hostLeftGame;	///< a game removed under a guest: host left, unless game.start follows
	Int hostLeftPumps;
	std::map<long long, UnsignedInt> vips;		///< account id -> relay address, as the server tells them
	std::string matchId;		///< the last game.start or qm.matched
	std::string reportedMatch;
	std::string replayedMatch;	///< the match whose replay last went up, or was given up (OnlineSendReplay)
	std::string quickMatch;	///< qm.matched's id until a recording claims it (OnlineClaimQuickMatch) or it is dropped
	std::string recordingMatch;	///< what the last recording claimed: its replay goes up when the recording closes
	Bool serverAcks;				///< auth.ok's acks: the server answers a report and a replay (report.ok/err, replay.ok/err)
	std::string replaySentMatch;	///< the match whose replay lines actually went out (OnlineSendReplay)
	std::string reportAnswered;	///< the match of the last report.ok or report.err
	std::string replayAnswered;	///< the match of the last replay.ok or replay.err
	std::string resultLine;	///< the last ONLINE RATED / ONLINE UNRATED line logged
	OnlineMessage result;		///< the last rated or unrated line itself
	Bool relayTicketFresh;	///< a match.hold for us carried a new relay ticket, not yet taken: OnlineTakeRelayTicket
	// a dropped match the others still play: the sign-in's match.rejoinable, then the answer to match.rejoin
	OnlineMessage rejoinable;	///< empty for none: OnlineGetRejoinable
	long long rejoinableMs;	///< when it arrived, OnlineNowMs
	UnsignedInt rejoinSerial;
	Int rejoinState;				///< OnlineRejoinState
	OnlineMessage rejoinOk;	///< the match.rejoin.ok not yet taken
	std::string rejoinRefusal;	///< the match.refused code not yet taken

	// what the server says about players, and who is in the room the screens show
	std::map<std::string, OnlineCard> cards;		///< by OnlineNickKey
	UnsignedInt cardSerial;
	std::deque<std::string> statsAsked;		///< OnlineNickKey of each stats{nick} sent, oldest first: an error{for:stats} names none
	std::set<long long> present;		///< ids in the current chat or staging room: a `player` for one is an update, not a join

	// the ladder's last answer
	Int ladderState;				///< OnlineLadderState
	OnlineMessage ladder;

	// the armies' last answer
	Int armiesState;				///< OnlineLadderState
	OnlineMessage armies;

	// recent matches, by account and mode; at most one ask out per key
	std::map<OnlineHistoryKey, OnlineHistoryEntry> history;
	std::deque<OnlineHistoryKey> historyAsked;		///< the asks out, oldest first: an older server's error{for:history} names none
	UnsignedInt historySerial;		///< moves on with every change to any key; never reset, so a key forgotten and asked again reads new

	// the abandon lock, from auth.ok's and the player's own card's lockSeconds and qm.status{locked}
	long long qmLockUntilMs;	///< OnlineNowMs when it ends, 0 when free

	// the last qm.status's window{lo, hi, nextMs} (lo and hi in win-chance points, 0 to 100), and when it
	// arrived in steady-clock milliseconds
	OnlineMessage qmWindow;
	long long qmWindowMs;
	Bool qmLeaveSent;				///< qm.leave went and no qm.status has answered it yet

	// the account's skins: what it owns (bit n for id n); what it wears is TankSkin_localLoadout
	UnsignedInt64 skinsOwned;
	Int skinsState;				///< OnlineSkinsState of the last skins.get or skins.set
	std::string skinsError;	///< the last skins.err's code

	// a Quick Match between qm.matched and its game.start
	OnlineMessage reveal;
	Bool revealing;
	UnsignedInt revealSerial;

	// Play vs AI (OnlineRequestAIMatch): the last ai.start's answer, then the match it named
	Int aiState;						///< OnlineAIMatchState
	OnlineMessage aiReady;	///< the ai.ready not yet taken
	std::string aiRefusal;	///< the ai.refused reason not yet taken
	std::string aiMatch;		///< the match taken from ai.ready, until its ai.result goes
	long long aiReadyMs;		///< when that ai.ready arrived, in steady-clock milliseconds
	Int aiSpeed;						///< the speed that match plays, ai.ready's echo; its ai.result says it again
	long long aiTickMs;			///< when the last ai.tick went
	std::string aiWonMatch;	///< the match whose ai.result said win: only its replay goes up
	std::string aiSentMatch;	///< the match of the last ai.result sent, kept after aiMatch clears: OnlineGetLastAIMatchId
	OnlineMessage aiVerdict;	///< the last ai.rated or ai.unrated
	UnsignedInt aiVerdictSerial;

	// friends and invites (OnlineBuddy.cpp), only once the welcome said friends:1
	Bool friendsOn;					///< welcome.friends; an error{for:friends.get,code:unknown} takes it back
	std::map<long long, OnlineFriendRow> friendRows;		///< by account id
	UnsignedInt friendsSerial;	///< moves on with every change to friendRows
	std::deque<OnlineInviteEntry> invites;		///< oldest first, one per inviter and game
	UnsignedInt invitesSerial;
	std::string friendErrorFor, friendErrorCode;	///< the last error{for} of a friend or invite request, until taken
	UnsignedInt friendErrorSerial;		///< moves on with every such error, taken or not (OnlinePeekFriendError)
	std::string friendErrorLastFor;		///< the last one's for, kept after the take
	std::map<long long, std::string> nicksById;	///< every id and nick a room or game told us: ADDBUDDY names an id, friend.add a nick

	// Team vs AI (TEAM-VS-AI.md), only once the welcome said teamai:1: the party and the ask of a member's ai.start
	Bool teamOn;						///< welcome.teamai
	Bool partyNickOn;				///< welcome.partyNick: a party invite may name anyone by nick
	long long partyLeader;	///< 0 out of a party
	std::vector< std::pair<long long, std::string> > party;	///< id and nick, the leader first
	UnsignedInt partySerial;
	std::string teamAsk;		///< ai.team.ask's matchId, until the hub answers it
	std::string teamAskNick;
	Bool teamReady;					///< the ai.ready of a team match waits for the hub, asked for or not
	Int teamQueueSize;			///< ai.team.queued's size, 0 out of the queue
	std::string teamMatch;	///< the team match the last team ai.ready started, until its report goes (OnlineIsTeamAIMatch)
	Bool partyScriptInvited, partyScriptStarted;	///< -onlineparty's steps, once each

	OnlinePeerQueue *peer;
	OnlineBuddyQueue *buddy;
	OnlinePSQueue *ps;

	void dial();
	void connectionGone();		///< what belonged to the connection: counts, the server's name, the round trip
	void forgetAccount();
	void pingServer();				///< ping{n} once ONLINE_PING_MS passed since the last, while welcomed
	void noteLock( long long seconds );		///< the abandon lock runs this many seconds more, 0 for free
	void peerResponse( const PeerResponse &r );
	void finishPeerLogin();
	void loginFailed( DisconnectReason reason );
	void flushAuth();					///< the pending login, once welcomed and the install files are hashed
	void sendFiles();					///< files{seq,last,paths[],hashes[]}, once a connection
	void gameListEntry( const OnlineMessage &m );
	void resolveHostLeft();
	void noteCard( const OnlineMessage &m );			///< a line's rating, tier, games and ping under its nick
	void matched( const OnlineMessage &m );			///< qm.matched as QM_MATCHED
	void noteSkins( const OnlineMessage &m );		///< auth.ok's or skins.ok's owned[] and skins[]
	void noteMaintenance( const OnlineMessage &m );	///< a request refused for maintenance, with its until
	// history (OnlineStats.cpp)
	void noteHistory( const OnlineMessage &m );			///< an answer under its id and mode
	void historyRefused( const OnlineMessage &m );	///< error{for:history}: the key it names, else the oldest ask
	void historySettled( const OnlineHistoryKey &key, Int state );		///< the key's ask is over: state, serial, asks
	void expireHistory();														///< asks out longer than ONLINE_HISTORY_TIMEOUT_MS fail
	// friends (OnlineBuddy.cpp)
	Bool friendLine( const OnlineMessage &m );		///< TRUE when m was a friends or invite line, handled
	void forgetFriends();												///< the account's rows, invites and error
	void noteFriendRow( const OnlineFriendRow &row );	///< one row in, its buddy response out
	void expireInvites();												///< invites older than ONLINE_INVITE_MS go
	void sendFriend( const OnlineMessage &m );	///< m, when the server knows friends; nothing otherwise

private:
	OnlineSession();
};

/* The first line on a new connection: the exe and INI CRCs, the version and the protocol (proto 2), and
	 notice:1, which asks the server for its notices. */
OnlineMessage OnlineHelloLine( UnsignedInt exe, UnsignedInt ini, const char *ver );

/* Bytes a server line's number field holds, for vips and ids. */
UnsignedInt OnlineHostToNet( UnsignedInt v );

/* A side or colour the server hands a seat (qm.matched, the game list), as the seat may take it.  A side
	 is a faction below numTemplates, Random or a Random pool; a colour is below numColors.  Anything else,
	 the observer included, is Random (-1): GameLogic dereferences the template and the colour a seat
	 names, so a hostile server's 999 was a crash at match start. */
Int OnlineSeatSide( long long side, Int numTemplates );
Int OnlineSeatColor( long long color, Int numColors );

#endif // _ONLINE_SESSION_H_
