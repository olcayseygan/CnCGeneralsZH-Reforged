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

// FILE: OnlineBuddy.cpp //////////////////////////////////////////////////////////////////////////
// Desc:   The buddy queue answered from the online service instead of GameSpy: the login and account
//         creation, and private messages.  With it the two queues that need no server at all: the
//         pinger (the relay is the only route) and the game results (the server rates from reports).
///////////////////////////////////////////////////////////////////////////////////////////////////

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "GameNetwork/GameSpy/GameResultsThread.h"
#include "GameNetwork/GameSpy/PingThread.h"
#include "GameNetwork/GameSpy/ThreadUtils.h"
#include "GameNetwork/Online/OnlineSession.h"

#include <deque>
#include <map>

//-------------------------------------------------------------------------------------------------
// the buddy queue
//-------------------------------------------------------------------------------------------------

OnlineBuddyQueue::OnlineBuddyQueue() : m_running( FALSE )
{
	OnlineSession::get().buddy = this;
}

OnlineBuddyQueue::~OnlineBuddyQueue()
{
	if (OnlineSession::get().buddy == this)
		OnlineSession::get().buddy = NULL;
}

Bool OnlineBuddyQueue::isConnected( void )
{
	return OnlineSession::get().authed;
}

Bool OnlineBuddyQueue::isConnecting( void )
{
	return OnlineSession::get().authWaiting;
}

GPProfile OnlineBuddyQueue::getLocalProfileID( void )
{
	return (GPProfile)OnlineSession::get().id;
}

Bool OnlineBuddyQueue::getResponse( BuddyResponse& resp )
{
	OnlineSession::get().pump();
	if (m_responses.empty())
		return FALSE;
	resp = m_responses.front();
	m_responses.pop_front();
	return TRUE;
}

void OnlineBuddyQueue::addRequest( const BuddyRequest& req )
{
	OnlineSession &s = OnlineSession::get();
	OnlineMessage m;
	switch (req.buddyRequestType)
	{
	case BuddyRequest::BUDDYREQUEST_LOGIN:
		// the server takes a nick in "email" too; the login screen always fills the address
		m.setString( "t", "login" ).setString( "email", req.arg.login.email ).setString( "pass", req.arg.login.password );
		s.requestAuth( m );
		return;

	case BuddyRequest::BUDDYREQUEST_LOGOUT:
		s.disconnect();
		return;

	case BuddyRequest::BUDDYREQUEST_MESSAGE:
		m.setString( "t", "say" ).setInt( "to", req.arg.message.recipient );
		m.setString( "text", WideCharStringToMultiByte( req.arg.message.text ) );
		s.send( m );
		return;

	default:
		return;		// buddy lists and status come later; RELOGIN has no saved password to use; LOGINNEW
							// is the site's /register page now (the login screen's Create Account opens it)
	}
}

GameSpyBuddyMessageQueueInterface *OnlineCreateBuddyQueue( void )
{
	return new OnlineBuddyQueue;
}

//-------------------------------------------------------------------------------------------------
// the pinger: every ping answered at once
//-------------------------------------------------------------------------------------------------

class OnlinePinger : public PingerInterface
{
public:
	OnlinePinger() : m_running( FALSE ) {}
	virtual void startThreads( void ) { m_running = TRUE; }
	virtual void endThreads( void ) { m_running = FALSE; }
	virtual Bool areThreadsRunning( void ) { return m_running; }

	virtual void addRequest( const PingRequest& req )
	{
		m_pings[req.hostname] = 1;
		PingResponse r;
		r.hostname = req.hostname;
		r.avgPing = 1;
		r.repetitions = req.repetitions;
		m_responses.push_back( r );
	}
	virtual Bool getRequest( PingRequest& req ) { return FALSE; }
	virtual void addResponse( const PingResponse& resp ) { m_responses.push_back( resp ); }
	virtual Bool getResponse( PingResponse& resp )
	{
		if (m_responses.empty())
			return FALSE;
		resp = m_responses.front();
		m_responses.pop_front();
		return TRUE;
	}

	virtual Bool arePingsInProgress( void ) { return FALSE; }
	virtual Int getPing( AsciiString hostname )
	{
		std::map<std::string, Int>::const_iterator it = m_pings.find( hostname.str() );
		return it == m_pings.end() ? -1 : it->second;
	}
	virtual void clearPingMap( void ) { m_pings.clear(); }
	virtual AsciiString getPingString( Int timeout )
	{
		// GameSpy's format: two hex digits a server, the ping scaled to the timeout
		AsciiString s;
		for (size_t i = 0; i < m_pings.size(); ++i)
			s.concat( "00" );
		return s;
	}

private:
	std::map<std::string, Int> m_pings;
	std::deque<PingResponse> m_responses;
	Bool m_running;
};

PingerInterface *OnlineCreatePinger( void )
{
	return new OnlinePinger;
}

//-------------------------------------------------------------------------------------------------
// game results: dropped, the server rates from OnlineSendReport
//-------------------------------------------------------------------------------------------------

class OnlineGameResults : public GameResultsInterface
{
public:
	OnlineGameResults() : m_running( FALSE ) {}
	virtual void init() {}
	virtual void reset() {}
	virtual void update() {}
	virtual void startThreads( void ) { m_running = TRUE; }
	virtual void endThreads( void ) { m_running = FALSE; }
	virtual Bool areThreadsRunning( void ) { return m_running; }
	virtual void addRequest( const GameResultsRequest& req ) {}
	virtual Bool getRequest( GameResultsRequest& req ) { return FALSE; }
	virtual void addResponse( const GameResultsResponse& resp ) {}
	virtual Bool getResponse( GameResultsResponse& resp ) { return FALSE; }
	virtual Bool areGameResultsBeingSent( void ) { return FALSE; }

private:
	Bool m_running;
};

GameResultsInterface *OnlineCreateGameResults( void )
{
	return new OnlineGameResults;
}
