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

#include <algorithm>
#include <deque>
#include <map>
#include <stdio.h>

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

	case BuddyRequest::BUDDYREQUEST_ADDBUDDY:
		{
			// EA names a profile id, the server a nick: a request from them is accepted by id, anyone else is
			// asked for by the nick a room or game told us.  A row already there (EA's OKADD follows itself with
			// an ADDBUDDY) asks nothing.
			const long long id = req.arg.addbuddy.id;
			std::map<long long, OnlineFriendRow>::const_iterator it = s.friendRows.find( id );
			if (it != s.friendRows.end())
			{
				if (it->second.status == "pending" && it->second.dir == "in")
					OnlineAnswerFriend( (Int)id, TRUE );
				return;
			}
			std::map<long long, std::string>::const_iterator n = s.nicksById.find( id );
			if (n != s.nicksById.end())
				OnlineAddFriend( n->second.c_str() );
			else
				DEBUG_LOG(("ONLINE FRIEND add id=%lld: no nick known\n", id));
		}
		return;

	case BuddyRequest::BUDDYREQUEST_OKADD:
		OnlineAnswerFriend( (Int)req.arg.profile.id, TRUE );
		return;

	case BuddyRequest::BUDDYREQUEST_DENYADD:
		OnlineAnswerFriend( (Int)req.arg.profile.id, FALSE );
		return;

	case BuddyRequest::BUDDYREQUEST_DELBUDDY:
		OnlineRemoveFriend( (Int)req.arg.profile.id );
		return;

	default:
		return;		// SETSTATUS: the server works presence out itself; RELOGIN has no saved password to use;
							// LOGINNEW is the site's /register page now (the login screen's Create Account opens it)
	}
}

//-------------------------------------------------------------------------------------------------
// friends and invites
//-------------------------------------------------------------------------------------------------

static void copyField( char *dst, size_t size, const std::string &src )
{
	snprintf( dst, size, "%s", src.c_str() );
}

Bool OnlineParseFriendRow( const OnlineMessage &m, OnlineFriendRow &out )
{
	const OnlineValue *v = m.find( "id" );
	if (v == NULL || v->kind != OnlineValue::NUMBER || m.getInt( "id" ) <= 0)
		return FALSE;
	out = OnlineFriendRow();
	out.id = m.getInt( "id" );
	out.nick = m.getString( "nick" );
	out.status = m.getString( "status" );
	out.dir = m.getString( "dir" );
	out.presence = m.getString( "presence", "offline" );
	if (out.status != "accepted" || out.presence.empty())
		out.presence = "offline";		// the server says presence only of a friend
	if (out.presence == "room")
	{
		out.game = m.getInt( "game" );
		out.name = m.getString( "name" );
		out.joinable = m.getBool( "joinable" );
		out.locked = m.getBool( "locked" );
	}
	return TRUE;
}

static GPEnum gpStatus( const std::string &presence, const char **word )
{
	if (presence == "online")	{ *word = "Online";		return GP_ONLINE; }
	if (presence == "lobby")	{ *word = "Chatting";	return GP_CHATTING; }
	if (presence == "room")		{ *word = "Staging";	return GP_STAGING; }
	if (presence == "match")	{ *word = "Playing";	return GP_PLAYING; }
	*word = "Offline";
	return GP_OFFLINE;
}

void OnlineSession::noteFriendRow( const OnlineFriendRow &row )
{
	std::map<long long, OnlineFriendRow>::iterator it = friendRows.find( row.id );
	const Bool had = it != friendRows.end();
	const OnlineFriendRow before = had ? it->second : OnlineFriendRow();
	const Bool wasRequest = had && before.status == "pending" && before.dir == "in";

	if (row.status != "accepted" && row.status != "pending" && row.status != "blocked")
	{
		// status none: gone, either way (a removal, a decline, a block the other side made reads the same)
		if (had)
		{
			friendRows.erase( it );
			++friendsSerial;
			DEBUG_LOG(("ONLINE FRIEND gone id=%lld nick=%s\n", row.id, row.nick.c_str()));
		}
		return;
	}
	if (had && before.status == row.status && before.dir == row.dir && before.presence == row.presence
		&& before.game == row.game && before.name == row.name && before.joinable == row.joinable
		&& before.locked == row.locked && before.nick == row.nick)
		return;
	friendRows[row.id] = row;
	++friendsSerial;
	DEBUG_LOG(("ONLINE FRIEND id=%lld nick=%s status=%s%s%s presence=%s game=%lld\n", row.id, row.nick.c_str(),
		row.status.c_str(), row.dir.empty() ? "" : " dir=", row.dir.c_str(), row.presence.c_str(), row.game));

	if (buddy == NULL)
		return;
	BuddyResponse b;
	memset( &b, 0, sizeof(b) );
	b.profile = (GPProfile)row.id;
	b.result = GP_NO_ERROR;
	if (row.status == "pending" && row.dir == "in" && !wasRequest)
	{
		b.buddyResponseType = BuddyResponse::BUDDYRESPONSE_REQUEST;
		copyField( b.arg.request.nick, sizeof(b.arg.request.nick), row.nick );
		buddy->addResponse( b );
	}
	else if (row.status == "accepted")
	{
		const char *word = "";
		b.buddyResponseType = BuddyResponse::BUDDYRESPONSE_STATUS;
		b.arg.status.status = gpStatus( row.presence, &word );
		copyField( b.arg.status.nick, sizeof(b.arg.status.nick), row.nick );
		copyField( b.arg.status.statusString, sizeof(b.arg.status.statusString), word );
		copyField( b.arg.status.location, sizeof(b.arg.status.location), row.name );
		buddy->addResponse( b );
	}
}

void OnlineSession::expireInvites()
{
	const long long now = OnlineNowMs();
	while (!invites.empty() && now - invites.front().atMs >= ONLINE_INVITE_MS)
	{
		invites.pop_front();
		++invitesSerial;
	}
}

static Bool isFriendRequest( const std::string &what )
{
	return what == "friends.get" || what == "friend.add" || what == "friend.answer" || what == "friend.remove"
		|| what == "friend.block" || what == "invite" || what == "invite.answer";
}

Bool OnlineSession::friendLine( const OnlineMessage &m )
{
	const std::string t = m.type();
	if (t == "error")
	{
		const std::string what = m.getString( "for" );
		if (!isFriendRequest( what ))
			return FALSE;
		const std::string code = m.getString( "code", "unknown" );
		DEBUG_LOG(("ONLINE FRIEND refused %s: %s\n", what.c_str(), code.c_str()));
		if (code == "unknown")
			friendsOn = FALSE;		// a server that does not know them after all: as one that never said friends:1
		friendErrorFor = what;
		friendErrorCode = code;
		friendErrorLastFor = what;
		++friendErrorSerial;
		return TRUE;
	}
	if (t != "friends" && t != "friend" && t != "friend.request" && t != "invite" && t != "invite.gone")
		return FALSE;
	if (!friendsOn || !authed)
		return TRUE;		// nothing of friends from a server that did not say it has them

	if (t == "friends")
	{
		std::map<long long, OnlineFriendRow> was;
		was.swap( friendRows );
		const std::vector<OnlineMessage> rows = m.getObjects( "rows" );
		for (size_t i = 0; i < rows.size(); ++i)
		{
			OnlineFriendRow row;
			if (!OnlineParseFriendRow( rows[i], row ))
				continue;
			// a row the last list had keeps its place, so a request already answered on screen is not asked again
			std::map<long long, OnlineFriendRow>::iterator old = was.find( row.id );
			if (old != was.end())
				friendRows[row.id] = old->second;
			noteFriendRow( row );
		}
		++friendsSerial;		// the list arrived, even an empty one
	}
	else if (t == "friend")
	{
		OnlineFriendRow row;
		if (OnlineParseFriendRow( m, row ))
			noteFriendRow( row );
	}
	else if (t == "friend.request")
	{
		// the friend{} that follows carries the row; this only names who asked
		DEBUG_LOG(("ONLINE FRIEND request id=%lld nick=%s\n", m.getInt( "id" ), m.getString( "nick" ).c_str()));
	}
	else if (t == "invite")
	{
		OnlineInviteEntry e;
		e.from = m.getInt( "from" );
		e.game = m.getInt( "game" );
		e.nick = m.getString( "nick" );
		e.name = m.getString( "name" );
		e.mode = m.getString( "mode" );
		e.map = m.getString( "map" );
		e.atMs = OnlineNowMs();
		// game 0 is a Team vs AI party's (mode teamai), answered by OnlineAnswerInvite as a room's
		const Bool party = e.game == 0 && e.mode == "teamai" && teamOn;
		if (e.from <= 0 || (e.game <= 0 && !party))
			return TRUE;
		for (std::deque<OnlineInviteEntry>::iterator it = invites.begin(); it != invites.end(); ++it)
			if (it->from == e.from && it->game == e.game)
			{
				invites.erase( it );
				break;
			}
		invites.push_back( e );
		while (invites.size() > 20)		// the server's limit per sender in ten minutes; nobody needs more on screen
			invites.pop_front();
		++invitesSerial;
		DEBUG_LOG(("ONLINE INVITE from=%lld nick=%s game=%lld name=%s mode=%s\n", e.from, e.nick.c_str(), e.game,
			e.name.c_str(), e.mode.c_str()));
	}
	else		// invite.gone
	{
		const long long from = m.getInt( "from" ), game = m.getInt( "game" );
		if (joiningGame && joiningGame == game)
		{
			// the answer to our invite.answer{ok}: the room filled, started or went first; a join refused
			joiningGame = 0;
			PeerResponse r;
			memset( &r.stagingRoom, 0, sizeof(r.stagingRoom) );
			memset( &r.qmStatus, 0, sizeof(r.qmStatus) );
			r.peerResponseType = PeerResponse::PEERRESPONSE_JOINSTAGINGROOM;
			r.joinStagingRoom.id = (Int)game;
			r.joinStagingRoom.ok = FALSE;
			r.joinStagingRoom.result = PEERJoinFailed;
			peerResponse( r );
		}
		for (std::deque<OnlineInviteEntry>::iterator it = invites.begin(); it != invites.end(); ++it)
			if (it->game == game && (from == 0 || it->from == from))
			{
				invites.erase( it );
				++invitesSerial;
				DEBUG_LOG(("ONLINE INVITE gone from=%lld game=%lld\n", from, game));
				break;
			}
	}
	return TRUE;
}

void OnlineSession::forgetFriends()
{
	if (!friendRows.empty())
		++friendsSerial;
	friendRows.clear();
	if (!invites.empty())
		++invitesSerial;
	invites.clear();
	friendErrorFor.clear();
	friendErrorCode.clear();
	nicksById.clear();
	// the server ends a party with the line
	if (partyLeader || !party.empty())
		++partySerial;
	partyLeader = 0;
	party.clear();
	teamAsk.clear();
	teamAskNick.clear();
}

void OnlineSession::sendFriend( const OnlineMessage &m )
{
	if (friendsOn && authed)
		send( m );
}

Bool OnlineFriendsAvailable( void )
{
	const OnlineSession &s = OnlineSession::get();
	return s.friendsOn && s.authed;
}

static Int friendOrder( const OnlineFriendRow &r )
{
	if (r.status == "accepted")	return 0;
	if (r.status == "pending")	return r.dir == "in" ? 1 : 2;
	return 3;
}

static Bool friendBefore( const OnlineFriendRow *a, const OnlineFriendRow *b )
{
	const Int oa = friendOrder( *a ), ob = friendOrder( *b );
	if (oa != ob)
		return oa < ob;
	const std::string ka = OnlineNickKey( a->nick ), kb = OnlineNickKey( b->nick );
	return ka != kb ? ka < kb : a->id < b->id;
}

UnsignedInt OnlineGetFriends( std::vector<OnlineFriend> &out )
{
	OnlineSession &s = OnlineSession::get();
	out.clear();
	std::vector<const OnlineFriendRow *> rows;
	for (std::map<long long, OnlineFriendRow>::const_iterator it = s.friendRows.begin(); it != s.friendRows.end(); ++it)
		rows.push_back( &it->second );
	std::sort( rows.begin(), rows.end(), friendBefore );
	for (size_t i = 0; i < rows.size(); ++i)
	{
		const OnlineFriendRow &r = *rows[i];
		OnlineFriend f;
		f.id = (Int)r.id;
		f.nick = r.nick.c_str();
		const Int order = friendOrder( r );
		f.status = order == 0 ? ONLINE_FRIEND_ACCEPTED : order == 1 ? ONLINE_FRIEND_PENDING_IN
			: order == 2 ? ONLINE_FRIEND_PENDING_OUT : ONLINE_FRIEND_BLOCKED;
		f.presence = r.presence == "online" ? ONLINE_PRESENCE_ONLINE : r.presence == "lobby" ? ONLINE_PRESENCE_LOBBY
			: r.presence == "room" ? ONLINE_PRESENCE_ROOM : r.presence == "match" ? ONLINE_PRESENCE_MATCH : ONLINE_PRESENCE_OFFLINE;
		f.game = (Int)r.game;
		f.roomName = UnicodeString( MultiByteToWideCharSingleLine( r.name.c_str() ).c_str() );
		f.joinable = r.joinable;
		f.locked = r.locked;
		out.push_back( f );
	}
	return s.friendsSerial;
}

Int OnlineCountFriendsOnline( void )
{
	const OnlineSession &s = OnlineSession::get();
	Int n = 0;
	for (std::map<long long, OnlineFriendRow>::const_iterator it = s.friendRows.begin(); it != s.friendRows.end(); ++it)
		if (it->second.status == "accepted" && it->second.presence != "offline")
			++n;
	return n;
}

void OnlineRequestFriends( void )
{
	OnlineMessage m;
	m.setString( "t", "friends.get" );
	OnlineSession::get().sendFriend( m );
}

void OnlineAddFriend( const AsciiString &nick )
{
	if (nick.isEmpty())
		return;
	OnlineMessage m;
	m.setString( "t", "friend.add" ).setString( "nick", nick.str() );
	OnlineSession::get().sendFriend( m );
}

void OnlineAnswerFriend( Int id, Bool ok )
{
	OnlineSession &s = OnlineSession::get();
	if (!OnlineFriendsAvailable())
		return;
	OnlineMessage m;
	m.setString( "t", "friend.answer" ).setInt( "id", id ).setBool( "ok", ok != FALSE );
	s.send( m );
	// answered: the row is the friend the server will confirm, or nothing; an ADDBUDDY right behind asks nothing
	std::map<long long, OnlineFriendRow>::iterator it = s.friendRows.find( id );
	if (it != s.friendRows.end() && it->second.dir == "in")
	{
		if (ok)
		{
			it->second.status = "accepted";
			it->second.dir.clear();
		}
		else
			s.friendRows.erase( it );
		++s.friendsSerial;
	}
}

void OnlineRemoveFriend( Int id )
{
	OnlineMessage m;
	m.setString( "t", "friend.remove" ).setInt( "id", id );
	OnlineSession::get().sendFriend( m );
}

void OnlineBlockPlayer( Int id )
{
	OnlineMessage m;
	m.setString( "t", "friend.block" ).setInt( "id", id );
	OnlineSession::get().sendFriend( m );
}

UnsignedInt OnlineGetInvites( std::vector<OnlineInvite> &out )
{
	OnlineSession &s = OnlineSession::get();
	s.expireInvites();
	out.clear();
	const long long now = OnlineNowMs();
	for (size_t i = 0; i < s.invites.size(); ++i)
	{
		const OnlineInviteEntry &e = s.invites[i];
		OnlineInvite v;
		v.from = (Int)e.from;
		v.nick = e.nick.c_str();
		v.game = (Int)e.game;
		v.roomName = UnicodeString( MultiByteToWideCharSingleLine( e.name.c_str() ).c_str() );
		v.mode = e.mode.c_str();
		v.map = e.map.c_str();
		v.msLeft = (Int)(ONLINE_INVITE_MS - (now - e.atMs));
		out.push_back( v );
	}
	return s.invitesSerial;
}

void OnlineInviteFriend( Int to )
{
	OnlineSession &s = OnlineSession::get();
	OnlineMessage m;
	m.setString( "t", "invite" ).setInt( "to", to );
	if (s.gameId)
		m.setInt( "game", s.gameId );
	else if (!s.teamOn)
		return;		// outside a room only a server with Team vs AI takes it: into our party
	s.sendFriend( m );
	DEBUG_LOG(("ONLINE INVITE to=%d game=%lld\n", to, s.gameId));
}

UnsignedInt OnlinePeekFriendError( AsciiString &forWhat )
{
	const OnlineSession &s = OnlineSession::get();
	forWhat = s.friendErrorLastFor.c_str();
	return s.friendErrorSerial;
}

void OnlineInviteToParty( const AsciiString &nick )
{
	OnlineSession &s = OnlineSession::get();
	if (nick.isEmpty() || s.gameId || !s.teamOn)
		return;		// a party is asked into from outside a room, on a server with Team vs AI
	OnlineMessage m;
	m.setString( "t", "invite" ).setString( "nick", nick.str() );
	s.sendFriend( m );
	DEBUG_LOG(("ONLINE INVITE party nick=%s\n", nick.str()));
}

void OnlineAnswerInvite( Int from, Int game, Bool ok )
{
	OnlineSession &s = OnlineSession::get();
	if (!OnlineFriendsAvailable())
		return;
	for (std::deque<OnlineInviteEntry>::iterator it = s.invites.begin(); it != s.invites.end(); ++it)
		if (it->from == from && it->game == game)
		{
			s.invites.erase( it );
			++s.invitesSerial;
			break;
		}
	OnlineMessage m;
	m.setString( "t", "invite.answer" ).setInt( "from", from ).setInt( "game", game ).setBool( "ok", ok != FALSE );
	if (ok && game != 0)		// game 0: a party's, answered with party{...}
	{
		// the server answers as it answers a game.join
		s.quickMatch.clear();
		s.joiningGame = game;
	}
	s.send( m );
}

Bool OnlineTakeFriendError( AsciiString &forWhat, AsciiString &code )
{
	OnlineSession &s = OnlineSession::get();
	if (s.friendErrorFor.empty())
		return FALSE;
	forWhat = s.friendErrorFor.c_str();
	code = s.friendErrorCode.c_str();
	s.friendErrorFor.clear();
	s.friendErrorCode.clear();
	return TRUE;
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
