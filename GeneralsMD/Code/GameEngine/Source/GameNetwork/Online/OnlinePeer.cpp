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

// FILE: OnlinePeer.cpp ///////////////////////////////////////////////////////////////////////////
// Desc:   The peer queue (login, chat rooms, game list, staging room, Quick Match) answered from the
//         online service instead of GameSpy, and the session every adapter shares: the connection,
//         the server's lines turned into PeerResponses, and OnlineQueues.h's calls.
///////////////////////////////////////////////////////////////////////////////////////////////////

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "Common/GlobalData.h"
#include "Common/MultiplayerSettings.h"
#include "Common/PlayerTemplate.h"
#include "Common/UserPreferences.h"
#include "Common/version.h"
#include "GameNetwork/GameInfo.h"
#include "GameNetwork/GameSpy/ThreadUtils.h"
#include "GameNetwork/Online/OnlineSession.h"
#include "zhio.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(_WIN32)
#include <shellapi.h>		// ShellExecuteA, OnlineOpenURL
#endif

// FirewallHelperClass::FIREWALL_TYPE_SIMPLE: every route goes through the relay, nothing to negotiate
static const Int ONLINE_NAT_SIMPLE = 1;
// The one chat room the login lists.  Its name is looked up as GUI:<name> for the lobby's combo box.
static const Int ONLINE_LOBBY_ROOM = 1;
static const char *ONLINE_LOBBY_NAME = "Lobby";

UnsignedInt OnlineHostToNet( UnsignedInt v )
{
	return ((v & 0xFF) << 24) | ((v & 0xFF00) << 8) | ((v >> 8) & 0xFF00) | (v >> 24);
}

/* Chat rooms and staging rooms share the server's "room" field, and both kinds of id start at 1.  So a
	 chat room goes out as the text "lobby<N>" and a staging room as its game id, a number: a lobby line
	 never reads as the staging room's, the way room "1" and game 1 used to. */
static std::string chatRoomName( Int room )
{
	char buf[32];
	snprintf( buf, sizeof(buf), "lobby%d", room );
	return buf;
}

static Int chatRoomNumber( const std::string &name )
{
	return name.compare( 0, 5, "lobby" ) == 0 ? atoi( name.c_str() + 5 ) : 0;
}

/* Whether a line's "room" is this staging room (a number) or this chat room (lobby<N> text). */
static Bool namesGame( const OnlineMessage &m, long long game )
{
	const OnlineValue *v = m.find( "room" );
	return game && v && v->kind == OnlineValue::NUMBER && atoll( v->text.c_str() ) == game;
}

static Bool namesChatRoom( const OnlineMessage &m, Int room )
{
	const OnlineValue *v = m.find( "room" );
	return room && v && v->kind == OnlineValue::STRING && chatRoomNumber( v->text ) == room;
}

static PeerResponse makePeer( Int type )
{
	PeerResponse r;
	memset( &r.stagingRoom, 0, sizeof(r.stagingRoom) );		// the union's two largest members
	memset( &r.qmStatus, 0, sizeof(r.qmStatus) );
	r.peerResponseType = (decltype(r.peerResponseType))type;
	return r;
}

Int OnlineSeatSide( long long side, Int numTemplates )
{
	if (side >= 0)
		return side < numTemplates ? (Int)side : (Int)PLAYERTEMPLATE_RANDOM;
	// below the pool marker IsValidPlayerTemplatePick reads only the mask, never the store
	return side < PLAYERTEMPLATE_RANDOM_POOL && side >= INT_MIN && IsValidPlayerTemplatePick( (Int)side ) ? (Int)side : (Int)PLAYERTEMPLATE_RANDOM;
}

Int OnlineSeatColor( long long color, Int numColors )
{
	return color >= 0 && color < numColors ? (Int)color : -1;
}

static Int seatSide( long long side )
{
	return OnlineSeatSide( side, ThePlayerTemplateStore ? ThePlayerTemplateStore->getPlayerTemplateCount() : 0 );
}

static Int seatColor( long long color )
{
	return OnlineSeatColor( color, TheMultiplayerSettings ? TheMultiplayerSettings->getNumColors() : 0 );
}

//-------------------------------------------------------------------------------------------------
// the session
//-------------------------------------------------------------------------------------------------

OnlineSession &OnlineSession::get()
{
	static OnlineSession s;
	return s;
}

OnlineSession::OnlineSession() :
	connection( NULL ), capture( NULL ), status( ONLINE_OFF ), relayPort( 0 ), testMode( FALSE ),
	peer( NULL ), buddy( NULL ), ps( NULL )
{
	forgetAccount();
}

void OnlineSession::forgetAccount()
{
	authed = FALSE;
	peerLoggedIn = FALSE;
	id = 0;
	nick.clear();
	token.clear();
	memset( ticket, 0, sizeof(ticket) );
	vip = 0;
	pendingAuth.clear();
	hasPendingAuth = FALSE;
	authWaiting = FALSE;
	groupRoom = 0;
	gameId = 0;
	joiningGame = 0;
	hosting = FALSE;
	hostInfo.clear();
	hostNick.clear();
	hostLeftGame = 0;
	hostLeftPumps = 0;
	vips.clear();
}

void OnlineSession::beginTest( std::vector<OnlineMessage> *lines )
{
	disconnect();
	capture = lines;
	matchId.clear();
	reportedMatch.clear();
	resultLine.clear();
	motd.clear();
	config.clear();
	relayPort = 8094;
	testMode = FALSE;
	registerUrl.clear();
	status = lines ? ONLINE_WELCOMED : ONLINE_OFF;
}

void OnlineSession::send( const OnlineMessage &m )
{
	if (capture)
		capture->push_back( m );
	else if (connection && !connection->send( m ))
		DEBUG_LOG(("online: could not send %s\n", m.type().c_str()));
}

void OnlineSession::dial()
{
	if (capture || !OnlineIsEnabled())
		return;

	std::string ca;
	if (!TheGlobalData->m_onlineCAFile.isEmpty())
	{
		FILE *f = zh_fopen( TheGlobalData->m_onlineCAFile.str(), "rb" );
		if (f)
		{
			char buf[4096];
			size_t n;
			while ((n = fread( buf, 1, sizeof(buf), f )) > 0)
				ca.append( buf, n );
			fclose( f );
		}
		else
		{
			DEBUG_LOG(("online: cannot read -onlineca %s\n", TheGlobalData->m_onlineCAFile.str()));
		}
	}

	if (!connection)
		connection = new OnlineConnection;
	if (!connection->connect( TheGlobalData->m_onlineHost.str(), TheGlobalData->m_onlinePort, ca ))
	{
		DEBUG_LOG(("online: cannot connect: %s\n", connection->error().c_str()));
		status = ONLINE_FAILED;
		return;
	}
	status = ONLINE_CONNECTING;

	OnlineMessage hello;
	hello.setString( "t", "hello" );
	hello.setInt( "exe", TheGlobalData->m_exeCRC );
	hello.setInt( "ini", TheGlobalData->m_iniCRC );
	hello.setString( "ver", TheVersion ? TheVersion->getReleaseVersion().str() : "" );
	connection->send( hello );
}

void OnlineSession::disconnect()
{
	if (connection)
	{
		connection->close();
		delete connection;
		connection = NULL;
	}
	if (!capture)
		status = ONLINE_OFF;
	forgetAccount();
}

void OnlineSession::requestAuth( const OnlineMessage &m )
{
	authWaiting = TRUE;
	if (status == ONLINE_WELCOMED)
	{
		send( m );
		return;
	}
	pendingAuth = m;
	hasPendingAuth = TRUE;
	if (status == ONLINE_OFF || status == ONLINE_FAILED)
		dial();
	if (status == ONLINE_FAILED)
		loginFailed( DISCONNECT_COULDNOTCONNECT );
}

void OnlineSession::pump()
{
	if (status == ONLINE_OFF)
		dial();

	if (connection && !capture)
	{
		OnlineMessage m;
		while (connection->receive( m ))
			dispatch( m );

		if (connection->state() == OnlineConnection::CLOSED && status != ONLINE_FAILED)
		{
			DEBUG_LOG(("online: connection closed: %s\n", connection->error().c_str()));
			status = ONLINE_FAILED;
			Bool wasIn = authed;
			Bool wasTrying = authWaiting;
			forgetAccount();
			if (wasIn || wasTrying)
			{
				PeerResponse r = makePeer( PeerResponse::PEERRESPONSE_DISCONNECT );
				r.discon.reason = wasIn ? DISCONNECT_LOSTCON : DISCONNECT_COULDNOTCONNECT;
				peerResponse( r );
			}
		}
	}

	if (hostLeftGame && ++hostLeftPumps >= 2)
		resolveHostLeft();
}

void OnlineSession::peerResponse( const PeerResponse &r )
{
	if (peer)
		peer->addResponse( r );
}

void OnlineSession::loginFailed( DisconnectReason reason )
{
	authWaiting = FALSE;
	hasPendingAuth = FALSE;
	PeerResponse r = makePeer( PeerResponse::PEERRESPONSE_DISCONNECT );
	r.discon.reason = reason;
	peerResponse( r );
}

/* PEERRESPONSE_LOGIN waits for relay.ok: its addresses are the relay's virtual one, and a player whose UDP
	 never reaches the relay should find out at the login screen, not in a staging room. */
void OnlineSession::finishPeerLogin()
{
	peerLoggedIn = TRUE;

	PeerResponse r = makePeer( PeerResponse::PEERRESPONSE_LOGIN );
	r.nick = nick;
	r.player.profileID = (Int)id;
	r.player.internalIP = vip;
	r.player.externalIP = vip;
	peerResponse( r );

	PeerResponse room = makePeer( PeerResponse::PEERRESPONSE_GROUPROOM );
	room.groupRoom.id = ONLINE_LOBBY_ROOM;
	room.groupRoom.maxWaiting = 1000;
	room.groupRoomName = ONLINE_LOBBY_NAME;
	peerResponse( room );
	peerResponse( makePeer( PeerResponse::PEERRESPONSE_GROUPROOM ) );	// id 0: the end of the list, as GameSpy's

	OnlineMessage stats;
	stats.setString( "t", "stats.get" ).setInt( "id", id );
	send( stats );
}

void OnlineSession::resolveHostLeft()
{
	if (!hostLeftGame)
		return;
	PeerResponse r = makePeer( PeerResponse::PEERRESPONSE_PLAYERLEFT );
	r.nick = hostNick;
	r.player.roomType = StagingRoom;
	hostLeftGame = 0;
	hostLeftPumps = 0;
	gameId = 0;
	peerResponse( r );
}

static DisconnectReason authFailReason( const std::string &code )
{
	if (code == "bademail" || code == "emailtaken")	return DISCONNECT_GP_LOGIN_BAD_EMAIL;
	if (code == "badnick")		return DISCONNECT_GP_NEWUSER_BAD_NICK;
	if (code == "nicktaken")	return DISCONNECT_NICKTAKEN;
	if (code == "badpass")		return DISCONNECT_GP_NEWUSER_BAD_PASSWORD;
	if (code == "badlogin" || code == "badtoken")	return DISCONNECT_GP_LOGIN_BAD_PASSWORD;
	if (code == "banned")			return DISCONNECT_SERIAL_BANNED;
	if (code == "elsewhere")	return DISCONNECT_LOSTCON;
	return DISCONNECT_COULDNOTCONNECT;		// reglimit, throttled, anything new
}

static Bool hexTicket( const std::string &hex, UnsignedByte out[16] )
{
	if (hex.size() != 32)
		return FALSE;
	for (Int i = 0; i < 16; ++i)
	{
		char pair[3] = { hex[i*2], hex[i*2+1], 0 };
		char *end = NULL;
		out[i] = (UnsignedByte)strtoul( pair, &end, 16 );
		if (end != pair + 2)
			return FALSE;
	}
	return TRUE;
}

/* A game{action:add|update}: the server's fields plus what the host put in game.opts (OnlinePeerQueue's
	 SETGAMEOPTIONS writes it as a line of its own), as the lobby's game list wants it. */
void OnlineSession::gameListEntry( const OnlineMessage &m )
{
	PeerResponse r = makePeer( PeerResponse::PEERRESPONSE_STAGINGROOM );
	r.stagingRoom.action = m.getString( "action" ) == "add" ? PEER_ADD : PEER_UPDATE;
	r.stagingRoom.isStaging = TRUE;
	r.stagingRoom.percentComplete = 100;
	r.stagingRoom.id = (Int)m.getInt( "id" );
	r.stagingServerName = MultiByteToWideCharSingleLine( m.getString( "name" ).c_str() );
	r.stagingRoom.numPlayers = (Int)m.getInt( "count" );
	r.stagingRoom.maxPlayers = (Int)m.getInt( "maxp", MAX_SLOTS );
	r.stagingRoom.useStats = TRUE;
	r.stagingRoomPlayerNames[0] = m.getString( "host" );

	OnlineMessage o;
	const std::string opts = m.getString( "opts" );
	if (!opts.empty() && OnlineMessage::parse( opts.data(), opts.size(), o ))
	{
		r.stagingRoom.exeCRC = (UnsignedInt)o.getInt( "exe" );
		r.stagingRoom.iniCRC = (UnsignedInt)o.getInt( "ini" );
		r.stagingRoom.version = (UnsignedInt)o.getInt( "ver" );
		r.stagingRoom.allowObservers = o.getBool( "obs" );
		r.stagingRoom.useStats = o.getBool( "stats", true );
		r.stagingRoom.ladderPort = (UnsignedShort)o.getInt( "ladPort" );
		r.stagingServerLadderIP = o.getString( "ladIP" );
		r.stagingServerPingString = o.getString( "ping" );
		r.stagingRoomMapName = o.getString( "map" );
		r.stagingRoom.numPlayers = (Int)o.getInt( "np", r.stagingRoom.numPlayers );
		r.stagingRoom.numObservers = (Int)o.getInt( "no" );
		r.stagingRoom.maxPlayers = (Int)o.getInt( "mp", r.stagingRoom.maxPlayers );
		const std::vector<std::string> names = o.getStrings( "names" );
		const std::vector<long long> wins = o.getInts( "wins" ), losses = o.getInts( "losses" ), pids = o.getInts( "pids" );
		const std::vector<long long> factions = o.getInts( "factions" ), colors = o.getInts( "colors" );
		for (Int i = 0; i < MAX_SLOTS; ++i)
		{
			if (i < (Int)names.size() && !names[i].empty())
				r.stagingRoomPlayerNames[i] = names[i];
			if (i < (Int)wins.size())			r.stagingRoom.wins[i] = (Int)wins[i];
			if (i < (Int)losses.size())		r.stagingRoom.losses[i] = (Int)losses[i];
			if (i < (Int)pids.size())			r.stagingRoom.profileID[i] = (Int)pids[i];
			if (i < (Int)factions.size())	r.stagingRoom.faction[i] = seatSide( factions[i] );
			if (i < (Int)colors.size())		r.stagingRoom.color[i] = seatColor( colors[i] );
		}
	}
	peerResponse( r );
}

static void playerResponse( OnlineSession &s, Int type, RoomType roomType, long long who, const std::string &nick )
{
	PeerResponse r = makePeer( type );
	r.nick = nick;
	r.player.roomType = roomType;
	r.player.profileID = (Int)who;
	std::map<long long, UnsignedInt>::const_iterator it = s.vips.find( who );
	if (it != s.vips.end())
		r.player.IP = OnlineHostToNet( it->second );		// GameSpy's player IP is in network order
	if (s.peer)
		s.peer->addResponse( r );
}

static void rememberVips( OnlineSession &s, const OnlineMessage &m )
{
	const std::vector<long long> ids = m.getInts( "ids" ), v = m.getInts( "vips" );
	for (size_t i = 0; i < ids.size() && i < v.size(); ++i)
		if (v[i])
			s.vips[ids[i]] = (UnsignedInt)v[i];
	if (m.has( "vip" ) && m.has( "id" ))
		s.vips[m.getInt( "id" )] = (UnsignedInt)m.getInt( "vip" );
}

void OnlineSession::dispatch( const OnlineMessage &m )
{
	const std::string t = m.type();

	// A game removed under a guest is the host leaving, unless it was removed because it started.
	if (hostLeftGame && t != "game.start")
		resolveHostLeft();

	if (t == "welcome")
	{
		motd = m.getString( "motd" );
		config = m.getString( "config" );
		relayPort = (UnsignedShort)m.getInt( "relayPort" );
		testMode = m.getBool( "testMode" );
		registerUrl = m.getString( "registerUrl" );
		status = ONLINE_WELCOMED;
		if (hasPendingAuth)
		{
			hasPendingAuth = FALSE;
			send( pendingAuth );
		}
	}
	else if (t == "auth.ok")
	{
		authWaiting = FALSE;
		authed = TRUE;
		peerLoggedIn = FALSE;
		id = m.getInt( "id" );
		nick = m.getString( "nick" );
		token = m.getString( "token" );
		vip = 0;
		if (!hexTicket( m.getString( "ticket" ), ticket ))
			DEBUG_LOG(("online: auth.ok carries no usable relay ticket\n"));

		if (buddy)
		{
			BuddyResponse b;
			memset( &b, 0, sizeof(b) );
			b.buddyResponseType = BuddyResponse::BUDDYRESPONSE_LOGIN;
			b.result = GP_NO_ERROR;
			b.profile = (GPProfile)id;
			buddy->addResponse( b );
		}
	}
	else if (t == "auth.fail")
	{
		const std::string code = m.getString( "code" );
		DEBUG_LOG(("online: auth.fail %s\n", code.c_str()));
		if (code == "already")
			return;
		Bool wasIn = authed;
		forgetAccount();
		if (code == "elsewhere" || wasIn)
		{
			PeerResponse r = makePeer( PeerResponse::PEERRESPONSE_DISCONNECT );
			r.discon.reason = DISCONNECT_LOSTCON;
			peerResponse( r );
		}
		else
			loginFailed( authFailReason( code ) );
	}
	else if (t == "relay.ok")
	{
		vip = (UnsignedInt)m.getInt( "vip" );
		vips[id] = vip;
		if (authed && !peerLoggedIn)
			finishPeerLogin();
	}
	else if (t == "error")
	{
		const std::string what = m.getString( "for" ), code = m.getString( "code" );
		DEBUG_LOG(("online: %s refused: %s\n", what.c_str(), code.c_str()));
		if (what == "room.join")
		{
			PeerResponse r = makePeer( PeerResponse::PEERRESPONSE_JOINGROUPROOM );
			r.joinGroupRoom.id = groupRoom;
			r.joinGroupRoom.ok = FALSE;
			groupRoom = 0;
			peerResponse( r );
		}
		else if (what == "game.host")
		{
			hosting = FALSE;
			PeerResponse r = makePeer( PeerResponse::PEERRESPONSE_CREATESTAGINGROOM );
			r.createStagingRoom.result = PEERJoinFailed;
			peerResponse( r );
		}
		else if (what == "game.join")
		{
			PeerResponse r = makePeer( PeerResponse::PEERRESPONSE_JOINSTAGINGROOM );
			r.joinStagingRoom.id = (Int)joiningGame;
			r.joinStagingRoom.ok = FALSE;
			r.joinStagingRoom.result = code == "full" ? PEERFullRoom : PEERJoinFailed;
			joiningGame = 0;
			peerResponse( r );
		}
		else if (what == "qm.join")
		{
			PeerResponse r = makePeer( PeerResponse::PEERRESPONSE_QUICKMATCHSTATUS );
			r.qmStatus.status = code == "norelay" ? QM_COULDNOTNEGOTIATEFIREWALLS : QM_COULDNOTFINDBOT;
			peerResponse( r );
		}
	}
	else if (t == "room")
	{
		groupRoom = chatRoomNumber( m.getString( "id" ) );
		rememberVips( *this, m );
		PeerResponse r = makePeer( PeerResponse::PEERRESPONSE_JOINGROUPROOM );
		r.joinGroupRoom.id = groupRoom;
		r.joinGroupRoom.ok = TRUE;
		peerResponse( r );
		const std::vector<long long> ids = m.getInts( "ids" );
		const std::vector<std::string> nicks = m.getStrings( "nicks" );
		for (size_t i = 0; i < ids.size() && i < nicks.size(); ++i)
			playerResponse( *this, PeerResponse::PEERRESPONSE_PLAYERJOIN, GroupRoom, ids[i], nicks[i] );
	}
	else if (t == "player" || t == "player.left")
	{
		rememberVips( *this, m );
		const Int type = t == "player" ? PeerResponse::PEERRESPONSE_PLAYERJOIN : PeerResponse::PEERRESPONSE_PLAYERLEFT;
		if (m.has( "game" ) && gameId && m.getInt( "game" ) == gameId)
			playerResponse( *this, type, StagingRoom, m.getInt( "id" ), m.getString( "nick" ) );
		else if (namesChatRoom( m, groupRoom ))
			playerResponse( *this, type, GroupRoom, m.getInt( "id" ), m.getString( "nick" ) );
	}
	else if (t == "said")
	{
		PeerResponse r = makePeer( PeerResponse::PEERRESPONSE_MESSAGE );
		r.nick = m.getString( "nick" );
		r.text = MultiByteToWideCharSingleLine( m.getString( "text" ).c_str() );
		r.message.isAction = m.getInt( "action" ) != 0 || m.getBool( "action" );
		r.message.profileID = (Int)m.getInt( "from" );
		if (m.has( "to" ))
			r.message.isPrivate = TRUE;
		else if (!namesGame( m, gameId ) && !namesChatRoom( m, groupRoom ))
			return;		// a room this player has left, said before the server took the room.leave
		peerResponse( r );
	}
	else if (t == "game")
	{
		const std::string action = m.getString( "action" );
		const long long g = m.getInt( "id" );
		if (action == "remove")
		{
			PeerResponse r = makePeer( PeerResponse::PEERRESPONSE_STAGINGROOM );
			r.stagingRoom.action = PEER_REMOVE;
			r.stagingRoom.isStaging = TRUE;
			r.stagingRoom.id = (Int)g;
			r.stagingRoom.percentComplete = 100;
			peerResponse( r );
			if (g == gameId && !hosting)
			{
				hostLeftGame = g;
				hostLeftPumps = 0;
			}
		}
		else
		{
			if (g == gameId)
				hostNick = m.getString( "host" );
			gameListEntry( m );
		}
	}
	else if (t == "game.joined")
	{
		gameId = m.getInt( "id" );
		joiningGame = 0;
		groupRoom = 0;
		hostNick = m.getString( "host" );
		hosting = m.getInt( "hostId" ) == id;
		rememberVips( *this, m );
		const std::vector<long long> ids = m.getInts( "ids" );
		const std::vector<std::string> nicks = m.getStrings( "nicks" );
		if (hosting)
		{
			PeerResponse r = makePeer( PeerResponse::PEERRESPONSE_CREATESTAGINGROOM );
			r.createStagingRoom.result = PEERJoinSuccess;
			peerResponse( r );
		}
		else
		{
			PeerResponse r = makePeer( PeerResponse::PEERRESPONSE_JOINSTAGINGROOM );
			r.joinStagingRoom.id = (Int)gameId;
			r.joinStagingRoom.ok = TRUE;
			r.joinStagingRoom.result = PEERJoinSuccess;
			for (size_t i = 0; i < nicks.size() && i < (size_t)MAX_SLOTS; ++i)
			{
				r.stagingRoomPlayerNames[i] = nicks[i];
				if (nicks[i] == hostNick)
					r.joinStagingRoom.isHostPresent = TRUE;
			}
			peerResponse( r );
		}
		/* Everyone in the room, ourselves included, into the screen's player list, which GameSpy filled from
			 each room's player keys.  Staging chat is shown only from a nick on that list, and the lobby's list
			 has lost the host by now (its room.leave), so without this the staging chat box stayed empty. */
		for (size_t i = 0; i < ids.size() && i < nicks.size(); ++i)
			playerResponse( *this, PeerResponse::PEERRESPONSE_PLAYERINFO, StagingRoom, ids[i], nicks[i] );
		for (size_t i = 0; i < ids.size(); ++i)
		{
			if (ids[i] == id)
				continue;
			OnlineMessage stats;
			stats.setString( "t", "stats.get" ).setInt( "id", ids[i] );
			send( stats );
		}
	}
	else if (t == "utm")
	{
		PeerResponse r = makePeer( PeerResponse::PEERRESPONSE_PLAYERUTM );
		r.nick = m.getString( "nick" );
		r.command = m.getString( "cmd" );
		// the screens send "SL/", "REQ/", "KICK/" and compare "SL", "REQ", "KICK": GameSpy's peer SDK
		// dropped the slash on the way, so a joiner never read the host's slot list and left after a minute
		if (!r.command.empty() && r.command[r.command.size() - 1] == '/')
			r.command.erase( r.command.size() - 1 );
		r.commandOptions = m.getString( "opts" );
		if (!m.has( "to" ))
		{
			if (!namesGame( m, gameId ))
				return;		// GameSpy's room UTMs only ever reached a staging room
			r.peerResponseType = PeerResponse::PEERRESPONSE_ROOMUTM;
		}
		peerResponse( r );
	}
	else if (t == "game.start")
	{
		matchId = m.getString( "match" );
		hostLeftGame = 0;
		hostLeftPumps = 0;
		rememberVips( *this, m );
		const Bool wasHost = hosting;
		gameId = 0;
		hosting = FALSE;
		if (!wasHost)
			peerResponse( makePeer( PeerResponse::PEERRESPONSE_GAMESTART ) );		// the host started on its own click
	}
	else if (t == "qm.status")
	{
		const std::string st = m.getString( "status" );
		PeerResponse r = makePeer( PeerResponse::PEERRESPONSE_QUICKMATCHSTATUS );
		r.qmStatus.poolSize = (Int)m.getInt( "pool" );
		if (st == "searching")
			r.qmStatus.status = QM_WORKING;
		else if (st == "left")
			r.qmStatus.status = QM_STOPPED;
		else
			r.qmStatus.status = QM_COULDNOTFINDBOT;		// locked: the leaver lock
		peerResponse( r );
	}
	else if (t == "qm.matched")
	{
		matchId = m.getString( "match" );
		rememberVips( *this, m );
		PeerResponse r = makePeer( PeerResponse::PEERRESPONSE_QUICKMATCHSTATUS );
		r.qmStatus.status = QM_MATCHED;
		r.qmStatus.seed = (Int)m.getInt( "seed" );
		r.qmStatus.mapIdx = (Int)m.getInt( "mapIdx" );
		const std::vector<std::string> nicks = m.getStrings( "nicks" );
		const std::vector<long long> v = m.getInts( "vips" ), sides = m.getInts( "sides" ), colors = m.getInts( "colors" );
		for (size_t i = 0; i < nicks.size() && i < (size_t)MAX_SLOTS; ++i)
		{
			r.stagingRoomPlayerNames[i] = nicks[i];
			r.qmStatus.IP[i] = i < v.size() ? (UnsignedInt)v[i] : 0;		// host order, as GameSpy's matchbot sent it
			r.qmStatus.side[i] = i < sides.size() ? seatSide( sides[i] ) : -1;
			r.qmStatus.color[i] = i < colors.size() ? seatColor( colors[i] ) : -1;
			r.qmStatus.nat[i] = ONLINE_NAT_SIMPLE;
		}
		peerResponse( r );
	}
	else if (t == "stats")
	{
		if (ps)
			ps->statsArrived( m.getInt( "id" ), m.getString( "kv" ) );
	}
	else if (t == "rated" || t == "unrated")
	{
		// what the end-to-end check greps; the numbers as the server spelled them
		const OnlineValue *delta = m.find( "delta" ), *rating = m.find( "rating" );
		if (t == "rated")
			resultLine = "ONLINE RATED delta=" + (delta ? delta->text : std::string()) + " rating=" + (rating ? rating->text : std::string());
		else
			resultLine = "ONLINE UNRATED reason=" + m.getString( "reason" );
		DEBUG_LOG(("%s match=%s\n", resultLine.c_str(), m.getString( "match" ).c_str()));
		if (authed)
		{
			OnlineMessage stats;
			stats.setString( "t", "stats.get" ).setInt( "id", id );
			send( stats );
		}
	}
	else
	{
		DEBUG_LOG(("online: ignored a %s line\n", t.c_str()));
	}
}

//-------------------------------------------------------------------------------------------------
// the peer queue
//-------------------------------------------------------------------------------------------------

OnlinePeerQueue::OnlinePeerQueue() : m_running( FALSE )
{
	OnlineSession::get().peer = this;
}

OnlinePeerQueue::~OnlinePeerQueue()
{
	if (OnlineSession::get().peer == this)
		OnlineSession::get().peer = NULL;
}

void OnlinePeerQueue::endThread( void )
{
	// TearDownGameSpy: the login ends with the queues
	m_running = FALSE;
	OnlineSession::get().disconnect();
}

Bool OnlinePeerQueue::isConnected( void )
{
	return OnlineSession::get().peerLoggedIn;
}

Bool OnlinePeerQueue::isConnecting( void )
{
	OnlineSession &s = OnlineSession::get();
	return s.authWaiting || (s.authed && !s.peerLoggedIn);
}

Bool OnlinePeerQueue::getResponse( PeerResponse& resp )
{
	OnlineSession::get().pump();
	if (m_responses.empty())
		return FALSE;
	resp = m_responses.front();
	m_responses.pop_front();
	return TRUE;
}

static void leaveGame( OnlineSession &s )
{
	if (s.gameId || s.hosting || s.joiningGame)
	{
		OnlineMessage m;
		m.setString( "t", "game.leave" );
		s.send( m );
	}
	s.gameId = 0;
	s.joiningGame = 0;
	s.hosting = FALSE;
	s.hostLeftGame = 0;
}

/* Out of the chat room on the server too, as GameSpy left the group room for a staging room or Quick Match:
	 the lobby's other players then stop listing somebody who no longer reads it. */
static void leaveChatRoom( OnlineSession &s )
{
	if (s.groupRoom)
	{
		OnlineMessage m;
		m.setString( "t", "room.leave" );
		s.send( m );
	}
	s.groupRoom = 0;
}

void OnlinePeerQueue::addRequest( const PeerRequest& req )
{
	OnlineSession &s = OnlineSession::get();
	OnlineMessage m;
	switch (req.peerRequestType)
	{
	case PeerRequest::PEERREQUEST_LOGIN:
		// the buddy login does the work; this only asks for the answer GameSpy's chat login gave
		if (s.authed && s.vip && !s.peerLoggedIn)
			s.finishPeerLogin();
		return;

	case PeerRequest::PEERREQUEST_LOGOUT:
		s.disconnect();
		return;

	case PeerRequest::PEERREQUEST_MESSAGEPLAYER:
		m.setString( "t", "say" ).setString( "to", req.nick );
		break;

	case PeerRequest::PEERREQUEST_MESSAGEROOM:
		m.setString( "t", "say" );
		if (s.gameId)
			m.setInt( "room", s.gameId );
		else if (s.groupRoom)
			m.setString( "room", chatRoomName( s.groupRoom ) );
		else
			return;
		break;

	case PeerRequest::PEERREQUEST_JOINGROUPROOM:
		leaveGame( s );
		s.groupRoom = req.groupRoom.id;
		m.setString( "t", "room.join" ).setString( "id", chatRoomName( req.groupRoom.id ) );
		s.send( m );
		return;

	case PeerRequest::PEERREQUEST_LEAVEGROUPROOM:
		leaveChatRoom( s );
		return;

	case PeerRequest::PEERREQUEST_STARTGAMELIST:
		{
			PeerResponse clear = makePeer( PeerResponse::PEERRESPONSE_STAGINGROOM );
			clear.stagingRoom.action = PEER_CLEAR;
			clear.stagingRoom.isStaging = TRUE;
			addResponse( clear );
			// games.watch answers with every game at once and marks no end; the list is complete as it lands
			addResponse( makePeer( PeerResponse::PEERRESPONSE_STAGINGROOMLISTCOMPLETE ) );
			m.setString( "t", "games.watch" );
			s.send( m );
		}
		return;

	case PeerRequest::PEERREQUEST_CREATESTAGINGROOM:
		leaveGame( s );
		leaveChatRoom( s );
		s.hosting = TRUE;
		s.hostInfo.clear();
		s.hostInfo.setInt( "exe", req.stagingRoomCreation.exeCRC );
		s.hostInfo.setInt( "ini", req.stagingRoomCreation.iniCRC );
		s.hostInfo.setInt( "ver", req.stagingRoomCreation.gameVersion );
		s.hostInfo.setBool( "obs", req.stagingRoomCreation.allowObservers != 0 );
		s.hostInfo.setBool( "stats", req.stagingRoomCreation.useStats != 0 );
		s.hostInfo.setInt( "ladPort", req.stagingRoomCreation.ladPort );
		s.hostInfo.setString( "ladIP", req.ladderIP );
		s.hostInfo.setString( "ping", req.hostPingStr );
		// the map is chosen in the staging room, after this; game.opts carries it to the list
		m.setString( "t", "game.host" ).setString( "name", WideCharStringToMultiByte( req.text.c_str() ) );
		m.setString( "map", "-" ).setInt( "maxp", MAX_SLOTS );
		s.send( m );
		return;

	case PeerRequest::PEERREQUEST_SETGAMEOPTIONS:
		{
			if (!s.gameId || !s.hosting)
				return;
			OnlineMessage o = s.hostInfo;
			o.setString( "map", req.gameOptsMapName );
			o.setInt( "np", req.gameOptions.numPlayers );
			o.setInt( "no", req.gameOptions.numObservers );
			o.setInt( "mp", req.gameOptions.maxPlayers );
			std::vector<std::string> names;
			std::vector<long long> wins, losses, pids, factions, colors;
			for (Int i = 0; i < MAX_SLOTS; ++i)
			{
				names.push_back( req.gameOptsPlayerNames[i] );
				wins.push_back( req.gameOptions.wins[i] );
				losses.push_back( req.gameOptions.losses[i] );
				pids.push_back( req.gameOptions.profileID[i] );
				factions.push_back( req.gameOptions.faction[i] );
				colors.push_back( req.gameOptions.color[i] );
			}
			o.setStrings( "names", names ).setInts( "wins", wins ).setInts( "losses", losses );
			o.setInts( "pids", pids ).setInts( "factions", factions ).setInts( "colors", colors );
			std::string line;
			if (o.toLine( line ))
			{
				line.resize( line.size() - 1 );		// the newline
				m.setString( "t", "game.opts" ).setString( "opts", line );
				s.send( m );
			}
			// and the slot list to the room, as GameSpy's SL/ UTM
			OnlineMessage sl;
			sl.setString( "t", "utm" ).setInt( "room", s.gameId ).setString( "cmd", "SL/" ).setString( "opts", req.options );
			s.send( sl );
		}
		return;

	case PeerRequest::PEERREQUEST_JOINSTAGINGROOM:
		leaveGame( s );
		leaveChatRoom( s );
		s.joiningGame = req.stagingRoom.id;
		m.setString( "t", "game.join" ).setInt( "id", req.stagingRoom.id );
		s.send( m );
		return;

	case PeerRequest::PEERREQUEST_LEAVESTAGINGROOM:
		leaveGame( s );
		return;

	case PeerRequest::PEERREQUEST_UTMPLAYER:
		if (req.nick.empty())
			return;
		m.setString( "t", "utm" ).setString( "to", req.nick ).setString( "cmd", req.id ).setString( "opts", req.options );
		s.send( m );
		return;

	case PeerRequest::PEERREQUEST_UTMROOM:
		m.setString( "t", "utm" );
		if (req.UTM.isStagingRoom)
			m.setInt( "room", s.gameId );
		else
			m.setString( "room", chatRoomName( s.groupRoom ) );
		m.setString( "cmd", req.id ).setString( "opts", req.options );
		s.send( m );
		return;

	case PeerRequest::PEERREQUEST_STARTGAME:
		m.setString( "t", "game.start" );
		s.send( m );
		return;

	case PeerRequest::PEERREQUEST_STARTQUICKMATCH:
		{
			leaveGame( s );
			leaveChatRoom( s );
			std::vector<long long> maps;
			for (size_t i = 0; i < req.qmMaps.size(); ++i)
				if (req.qmMaps[i])
					maps.push_back( (long long)i );
			m.setString( "t", "qm.join" ).setInts( "maps", maps );
			m.setInt( "side", req.QM.side ).setInt( "color", req.QM.color );
			s.send( m );
		}
		return;

	case PeerRequest::PEERREQUEST_WIDENQUICKMATCHSEARCH:
		{
			m.setString( "t", "qm.widen" );
			s.send( m );
			PeerResponse r = makePeer( PeerResponse::PEERRESPONSE_QUICKMATCHSTATUS );
			r.qmStatus.status = QM_WIDENINGSEARCH;
			addResponse( r );
		}
		return;

	case PeerRequest::PEERREQUEST_STOPQUICKMATCH:
		m.setString( "t", "qm.leave" );
		s.send( m );
		return;

	default:
		return;		// PUSHSTATS (the server keeps stats), GETEXTENDEDSTAGINGROOMINFO, STOPGAMELIST
	}

	// the two says
	m.setString( "text", WideCharStringToMultiByte( req.text.c_str() ) );
	m.setBool( "action", req.message.isAction != 0 );
	s.send( m );
}

//-------------------------------------------------------------------------------------------------
// OnlineQueues.h
//-------------------------------------------------------------------------------------------------

Bool OnlineIsEnabled( void )
{
	if (OnlineSession::get().capture)
		return TRUE;
	return TheGlobalData && !TheGlobalData->m_onlineHost.isEmpty() && TheGlobalData->m_onlinePort != 0;
}

GameSpyPeerMessageQueueInterface *OnlineCreatePeerQueue( void )
{
	return new OnlinePeerQueue;
}

void OnlineUpdate( void )
{
	if (OnlineIsEnabled())
		OnlineSession::get().pump();
}

OnlineStatus OnlineGetStatus( void )
{
	return OnlineIsEnabled() ? OnlineSession::get().status : ONLINE_OFF;
}

AsciiString OnlineGetMOTD( void )
{
	return OnlineSession::get().motd.c_str();
}

AsciiString OnlineGetConfig( void )
{
	return OnlineSession::get().config.c_str();
}

void OnlineDisconnect( void )
{
	OnlineSession::get().disconnect();
}

Bool OnlineIsTestServer( void )
{
	return OnlineIsEnabled() && OnlineSession::get().testMode;
}

AsciiString OnlineGetRegisterUrl( void )
{
	return OnlineSession::get().registerUrl.c_str();
}

/* Only a web page: the address comes from the server, and ShellExecute would run a program path or open a
	 file:// as readily as a page. */
Bool OnlineOpenURL( const char *url )
{
	if (!url || (strncmp( url, "https://", 8 ) != 0 && strncmp( url, "http://", 7 ) != 0))
		return FALSE;
#if defined(_WIN32)
	return (INT_PTR)ShellExecuteA( NULL, "open", url, NULL, NULL, SW_SHOWNORMAL ) > 32;
#else
	return TheOnlineOpenURLHook ? (Bool)TheOnlineOpenURLHook( url ) : FALSE;
#endif
}

#if !defined(_WIN32)
OnlineOpenURLHook TheOnlineOpenURLHook = NULL;
#endif

Bool OnlineGetRelayTicket( UnsignedByte out[16] )
{
	OnlineSession &s = OnlineSession::get();
	if (!s.authed)
		return FALSE;
	memcpy( out, s.ticket, sizeof(s.ticket) );
	return TRUE;
}

Bool OnlineGetRelayEndpoint( AsciiString &host, UnsignedShort &port )
{
	OnlineSession &s = OnlineSession::get();
	if (!s.authed || !s.relayPort || !TheGlobalData)
		return FALSE;
	host = TheGlobalData->m_onlineHost;
	port = s.relayPort;
	return TRUE;
}

UnsignedInt OnlineGetRelayVip( void )
{
	return OnlineSession::get().vip;
}

AsciiString OnlineGetSessionToken( void )
{
	return OnlineSession::get().token.c_str();
}

static const char *ONLINE_TOKEN_FILE = "GeneralsOnline\\OnlineTokens.ini";

AsciiString OnlineGetSavedToken( const AsciiString &login )
{
	AsciiString key = login;
	key.toLower();
	UserPreferences p;
	p.load( ONLINE_TOKEN_FILE );
	return p.getAsciiString( key, AsciiString::TheEmptyString );
}

void OnlineSaveToken( const AsciiString &login, const AsciiString &token )
{
	AsciiString key = login;
	key.toLower();
	UserPreferences p;
	p.load( ONLINE_TOKEN_FILE );
	if (token.isEmpty())
		p.erase( key );
	else
		p[key] = token;
	p.write();
}

void OnlineResume( const AsciiString &token )
{
	OnlineMessage m;
	m.setString( "t", "resume" ).setString( "token", token.str() );
	OnlineSession::get().requestAuth( m );
}
