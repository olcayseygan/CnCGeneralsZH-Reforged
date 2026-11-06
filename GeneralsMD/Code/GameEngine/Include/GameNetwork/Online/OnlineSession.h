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

	// the account
	Bool authed;						///< auth.ok arrived
	Bool peerLoggedIn;			///< PEERRESPONSE_LOGIN went out (it waits for relay.ok)
	long long id;
	std::string nick;
	std::string token;
	UnsignedByte ticket[16];
	UnsignedInt vip;
	OnlineMessage pendingAuth;		///< sent once the welcome arrives
	Bool hasPendingAuth;
	Bool authWaiting;				///< a login, register or resume is out (or pending) with no answer yet

	// where the player is
	Int groupRoom;					///< 0 outside a chat room; rooms are the server's ids as decimal text
	long long gameId;				///< the staging room, 0 outside one
	long long joiningGame;	///< game.join sent, game.joined not back yet
	Bool hosting;
	OnlineMessage hostInfo;	///< CREATESTAGINGROOM's CRCs and ladder, repeated in every game.opts
	std::string hostNick;
	long long hostLeftGame;	///< a game removed under a guest: host left, unless game.start follows
	Int hostLeftPumps;
	std::map<long long, UnsignedInt> vips;		///< account id -> relay address, as the server tells them
	std::string matchId;		///< the last game.start or qm.matched
	std::string reportedMatch;
	std::string resultLine;	///< the last ONLINE RATED / ONLINE UNRATED line logged

	OnlinePeerQueue *peer;
	OnlineBuddyQueue *buddy;
	OnlinePSQueue *ps;

	void dial();
	void forgetAccount();
	void peerResponse( const PeerResponse &r );
	void finishPeerLogin();
	void loginFailed( DisconnectReason reason );
	void gameListEntry( const OnlineMessage &m );
	void resolveHostLeft();

private:
	OnlineSession();
};

/* Bytes a server line's number field holds, for vips and ids. */
UnsignedInt OnlineHostToNet( UnsignedInt v );

/* A side or colour the server hands a seat (qm.matched, the game list), as the seat may take it.  A side
	 is a faction below numTemplates, Random or a Random pool; a colour is below numColors.  Anything else,
	 the observer included, is Random (-1): GameLogic dereferences the template and the colour a seat
	 names, so a hostile server's 999 was a crash at match start. */
Int OnlineSeatSide( long long side, Int numTemplates );
Int OnlineSeatColor( long long color, Int numColors );

#endif // _ONLINE_SESSION_H_
