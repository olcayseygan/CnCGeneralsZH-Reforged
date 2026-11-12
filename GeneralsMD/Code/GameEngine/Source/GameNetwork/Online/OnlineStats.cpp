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

// FILE: OnlineStats.cpp //////////////////////////////////////////////////////////////////////////
// Desc:   The persistent-storage queue (player stats) answered from the online service instead of
//         GameSpy, and the match report.  The server owns every number: a client's stats writes are
//         dropped, and the result goes up as OnlineSendReport, once a match.
///////////////////////////////////////////////////////////////////////////////////////////////////

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "GameClient/GameText.h"
#include "GameNetwork/Online/OnlineSession.h"
#include "zhio.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

OnlinePSQueue::OnlinePSQueue() : m_running( FALSE )
{
	OnlineSession::get().ps = this;
}

OnlinePSQueue::~OnlinePSQueue()
{
	if (OnlineSession::get().ps == this)
		OnlineSession::get().ps = NULL;
}

void OnlinePSQueue::addRequest( const PSRequest& req )
{
	if (req.requestType != PSRequest::PSREQUEST_READPLAYERSTATS || req.player.id == 0)
		return;		// writes, locale, results, CD keys: the server keeps the numbers
	OnlineMessage m;
	m.setString( "t", "stats.get" ).setInt( "id", req.player.id );
	OnlineSession::get().send( m );
}

Bool OnlinePSQueue::getResponse( PSResponse& resp )
{
	OnlineSession::get().pump();
	if (m_responses.empty())
		return FALSE;
	resp = m_responses.front();
	m_responses.pop_front();
	return TRUE;
}

void OnlinePSQueue::trackPlayerStats( PSPlayerStats stats )
{
	std::map<Int, PSPlayerStats>::iterator it = m_stats.find( stats.id );
	if (it == m_stats.end())
		m_stats[stats.id] = stats;
	else
		it->second.incorporate( stats );
}

PSPlayerStats OnlinePSQueue::findPlayerStatsByID( Int id )
{
	std::map<Int, PSPlayerStats>::const_iterator it = m_stats.find( id );
	if (it != m_stats.end())
		return it->second;
	PSPlayerStats empty;
	empty.id = 0;
	return empty;
}

/* The server's kv is \nick\N\wins\W\losses\L\games\G\rating\R.  EA's parser keeps wins, losses and games
	 only per general (wins3, losses3) and drops a bare key, so the totals go under general 0: every
	 reader of them (CalculateRank, the player popup) sums over the generals. */
static std::string totalsUnderGeneralZero( const std::string &kv )
{
	std::string out;
	size_t at = 0;
	Bool isKey = TRUE;
	while (at < kv.size())
	{
		size_t next = kv.find( '\\', at + 1 );
		if (next == std::string::npos)
			next = kv.size();
		std::string field = kv.substr( at, next - at );		// "\name"
		out += field;
		if (isKey && (field == "\\wins" || field == "\\losses" || field == "\\games"))
			out += "0";
		isKey = !isKey;
		at = next;
	}
	return out;
}

void OnlinePSQueue::statsArrived( long long id, const std::string &kv )
{
	PSResponse r;
	r.responseType = PSResponse::PSRESPONSE_PLAYERSTATS;
	r.player = GameSpyPSMessageQueueInterface::parsePlayerKVPairs( totalsUnderGeneralZero( kv ) );
	r.player.id = (Int)id;
	r.preorder = FALSE;
	m_stats[r.player.id] = r.player;		// the server's numbers replace what was cached
	addResponse( r );
}

GameSpyPSMessageQueueInterface *OnlineCreatePSQueue( void )
{
	return new OnlinePSQueue;
}

//-------------------------------------------------------------------------------------------------
// the match report
//-------------------------------------------------------------------------------------------------

OnlineReport::OnlineReport() :
	seed( 0 ), endFrame( 0 ), winningTeam( -1 ), sawDesync( FALSE ), sawGameEnd( FALSE ), disconEnd( FALSE ),
	surrendered( FALSE ), duration( 0 ), numPlayers( 0 ), numCrcOdd( 0 )
{
	for (Int i = 0; i < MAX_SLOTS; ++i)
	{
		discon[i] = FALSE;
		blind[i] = 0;
		crcOdd[i] = 0;
	}
}

void OnlineSendReport( const OnlineReport &report )
{
	OnlineSession &s = OnlineSession::get();
	if (s.matchId.empty() || s.matchId == s.reportedMatch)
		return;		// no match, or the score screen's second call
	s.reportedMatch = s.matchId;

	OnlineMessage m;
	m.setString( "t", "report" ).setString( "match", s.matchId );
	m.setInt( "seed", report.seed ).setInt( "endFrame", report.endFrame ).setInt( "winningTeam", report.winningTeam );
	m.setBool( "sawDesync", report.sawDesync != 0 ).setBool( "sawGameEnd", report.sawGameEnd != 0 );
	m.setBool( "disconEnd", report.disconEnd != 0 ).setBool( "surrendered", report.surrendered != 0 );
	m.setInt( "duration", report.duration );
	m.setInt( "hz", LOGICFRAMES_PER_SECOND );	// logic frames a second, so the server can turn endFrame into time
	std::vector<long long> discon;
	std::vector<std::string> side;
	std::vector<long long> blind;
	for (Int i = 0; i < report.numPlayers && i < MAX_SLOTS; ++i)
	{
		discon.push_back( report.discon[i] ? 1 : 0 );
		side.push_back( report.side[i].str() );
		blind.push_back( report.blind[i] );
	}
	std::vector<long long> crcOdd;
	for (Int i = 0; i < report.numCrcOdd && i < MAX_SLOTS; ++i)
		crcOdd.push_back( report.crcOdd[i] );
	m.setInts( "discon", discon ).setStrings( "side", side );
	m.setInts( "blind", blind ).setInts( "crcOdd", crcOdd );
	s.send( m );
}

//-------------------------------------------------------------------------------------------------
// the replay
//-------------------------------------------------------------------------------------------------

static std::string base64( const unsigned char *data, size_t length )
{
	static const char digits[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	std::string out;
	out.reserve( (length + 2) / 3 * 4 );
	for (size_t i = 0; i < length; i += 3)
	{
		const UnsignedInt b = (UnsignedInt)data[i] << 16
			| (i + 1 < length ? (UnsignedInt)data[i + 1] << 8 : 0)
			| (i + 2 < length ? (UnsignedInt)data[i + 2] : 0);
		out += digits[(b >> 18) & 63];
		out += digits[(b >> 12) & 63];
		out += i + 1 < length ? digits[(b >> 6) & 63] : '=';
		out += i + 2 < length ? digits[b & 63] : '=';
	}
	return out;
}

AsciiString OnlineClaimQuickMatch( void )
{
	OnlineSession &s = OnlineSession::get();
	AsciiString id = s.quickMatch.c_str();
	s.quickMatch.clear();
	return id;
}

void OnlineSendReplay( const char *match, const AsciiString &path )
{
	OnlineSession &s = OnlineSession::get();
	if (!match || !*match || s.replayedMatch == match)
		return;		// a custom game, or this match's replay went up (or was given up) already
	if (s.matchId != match)
		return;		// a later match or a logout replaced it
	s.replayedMatch = match;
	if (path.isEmpty() || !s.authed || s.status != ONLINE_WELCOMED)
		return;

	// one read on the main thread: a replay is tens to a few hundred KB, and the lines only queue
	FILE *f = zh_fopen( path.str(), "rb" );
	if (!f)
		return;
	const size_t chunk = ONLINE_REPLAY_CHUNK, most = ONLINE_REPLAY_MAX;
	std::string bytes;
	char buf[ONLINE_REPLAY_CHUNK];
	size_t n;
	while ((n = fread( buf, 1, sizeof(buf), f )) > 0 && bytes.size() <= most)
		bytes.append( buf, n );
	const Bool readAll = !ferror( f );
	fclose( f );
	if (!readAll || bytes.empty() || bytes.size() > most)
		return;		// over the cap: not sent at all

	Int seq = 0;
	for (size_t at = 0; at < bytes.size(); at += chunk, ++seq)
	{
		const size_t len = bytes.size() - at < chunk ? bytes.size() - at : chunk;
		OnlineMessage m;
		m.setString( "t", "replay" ).setString( "match", s.matchId ).setInt( "seq", seq );
		m.setBool( "last", at + len == bytes.size() );
		m.setString( "data", base64( (const unsigned char *)bytes.data() + at, len ) );
		s.send( m );
	}
	DEBUG_LOG(("ONLINE REPLAY match=%s bytes=%d lines=%d\n", s.matchId.c_str(), (Int)bytes.size(), seq));
}

//-------------------------------------------------------------------------------------------------
// ratings, the ladder, the reveal and the verdict, for the screens
//-------------------------------------------------------------------------------------------------

OnlinePlayerInfo::OnlinePlayerInfo() :
	ping( -1 ), hasRating( FALSE ), rating( 0 ), games( -1 ), wins( -1 ), losses( -1 ), rank( 0 ),
	notOnline( FALSE ), hasStats( FALSE ), serial( 0 )
{
}

OnlineLadderRow::OnlineLadderRow() : rank( 0 ), hasRating( FALSE ), rating( 0 ), wins( 0 ), losses( 0 )
{
}

Bool OnlineGetPlayerInfo( const AsciiString &nick, OnlinePlayerInfo &out )
{
	out = OnlinePlayerInfo();
	const std::map<std::string, OnlineCard> &cards = OnlineSession::get().cards;
	std::map<std::string, OnlineCard>::const_iterator it = cards.find( OnlineNickKey( nick.str() ) );
	if (it == cards.end())
		return FALSE;
	const OnlineCard &c = it->second;
	out.serial = c.serial;
	out.ping = (Int)c.ping;
	out.hasRating = c.hasRating;
	out.rating = (Int)c.rating;
	out.tier = c.tier.c_str();
	out.games = (Int)c.games;
	out.wins = (Int)c.wins;
	out.losses = (Int)c.losses;
	out.rank = (Int)c.rank;
	out.notOnline = c.notOnline;
	out.hasStats = c.hasStats;
	return TRUE;
}

void OnlineRequestStats( const AsciiString &nick )
{
	OnlineSession &s = OnlineSession::get();
	if (nick.isEmpty() || !s.authed)
		return;
	OnlineMessage m;
	m.setString( "t", "stats" ).setString( "nick", nick.str() );
	s.send( m );
	s.statsAsked.push_back( OnlineNickKey( nick.str() ) );
}

UnsignedInt OnlinePlayerInfoSerial( void )
{
	return OnlineSession::get().cardSerial;
}

UnicodeString OnlineTierName( const AsciiString &tier )
{
	if (tier.isEmpty())
		return UnicodeString::TheEmptyString;
	if (TheGameText)
	{
		AsciiString label;
		label.format( "GUI:OnlineTier_%s", tier.str() );
		Bool exists = FALSE;
		UnicodeString name = TheGameText->fetch( label, &exists );
		if (exists)
			return name;
	}
	// a tier this build has no words for: the key itself, capitalised
	std::string shown = tier.str();
	shown[0] = (char)toupper( (unsigned char)shown[0] );
	UnicodeString name;
	name.translate( AsciiString( shown.c_str() ) );
	return name;
}

UnicodeString OnlineRatingText( const OnlinePlayerInfo &info )
{
	UnicodeString text;
	if (!TheGameText)
		return text;
	if (info.hasRating && info.tier.isNotEmpty())
		text.format( TheGameText->fetch( "GUI:OnlineRatingTier" ), OnlineTierName( info.tier ).str(), info.rating );
	else if (info.hasRating)
		text.format( TheGameText->fetch( "GUI:OnlineRating" ), info.rating );
	else if (info.tier.isNotEmpty())
		text = OnlineTierName( info.tier );
	return text;
}

void OnlineRequestLadder( Int offset )
{
	OnlineSession &s = OnlineSession::get();
	if (!s.authed)
	{
		s.ladderState = ONLINE_LADDER_FAILED;
		return;
	}
	s.ladderState = ONLINE_LADDER_WAITING;
	OnlineMessage m;
	m.setString( "t", "ladder" ).setInt( "offset", offset < 0 ? 0 : offset );
	s.send( m );
}

static OnlineLadderRow ladderRow( const OnlineMessage &m )
{
	OnlineLadderRow r;
	r.rank = (Int)m.getInt( "rank", 0 );
	r.nick = m.getString( "nick" ).c_str();
	const OnlineValue *rating = m.find( "rating" );
	r.hasRating = rating && rating->kind == OnlineValue::NUMBER;
	r.rating = r.hasRating ? (Int)floor( m.getNumber( "rating" ) + 0.5 ) : 0;
	r.tier = m.getString( "tier" ).c_str();
	r.wins = (Int)m.getInt( "wins", 0 );
	r.losses = (Int)m.getInt( "losses", 0 );
	return r;
}

OnlineLadderState OnlineGetLadder( Int &offset, std::vector<OnlineLadderRow> &rows, OnlineLadderRow &me, Bool &hasMe )
{
	const OnlineSession &s = OnlineSession::get();
	rows.clear();
	me = OnlineLadderRow();
	hasMe = FALSE;
	offset = (Int)s.ladder.getInt( "offset", 0 );
	if (s.ladderState != ONLINE_LADDER_READY)
		return (OnlineLadderState)s.ladderState;
	const std::vector<OnlineMessage> got = s.ladder.getObjects( "rows" );
	for (size_t i = 0; i < got.size(); ++i)
		rows.push_back( ladderRow( got[i] ) );
	OnlineMessage mine;
	if (s.ladder.getObject( "me", mine ))
	{
		me = ladderRow( mine );
		hasMe = TRUE;
	}
	return ONLINE_LADDER_READY;
}

void OnlineRequestArmies( void )
{
	OnlineSession &s = OnlineSession::get();
	if (!s.authed)
	{
		s.armiesState = ONLINE_LADDER_FAILED;
		return;
	}
	s.armiesState = ONLINE_LADDER_WAITING;
	OnlineMessage m;
	m.setString( "t", "armies" );
	s.send( m );
}

OnlineLadderState OnlineGetArmies( std::vector<OnlineArmyRow> &rows )
{
	const OnlineSession &s = OnlineSession::get();
	rows.clear();
	if (s.armiesState != ONLINE_LADDER_READY)
		return (OnlineLadderState)s.armiesState;
	const std::vector<OnlineMessage> got = s.armies.getObjects( "sides" );
	for (size_t i = 0; i < got.size(); ++i)
	{
		OnlineArmyRow r;
		r.side = got[i].getString( "side" ).c_str();
		r.games = (Int)got[i].getInt( "games", 0 );
		r.wins = (Int)got[i].getInt( "wins", 0 );
		rows.push_back( r );
	}
	return ONLINE_LADDER_READY;
}

Bool OnlineGetMatchReveal( OnlineMatchReveal &out )
{
	const OnlineSession &s = OnlineSession::get();
	out.opponents.clear();
	out.server.clear();
	out.revealMs = 0;
	out.serial = s.revealSerial;
	if (!s.revealing)
		return FALSE;
	const std::vector<OnlineMessage> got = s.reveal.getObjects( "opponents" );
	for (size_t i = 0; i < got.size(); ++i)
		out.opponents.push_back( ladderRow( got[i] ) );
	out.server = s.reveal.getString( "server" ).c_str();
	out.revealMs = (Int)s.reveal.getInt( "revealMs", 0 );
	return TRUE;
}

Bool OnlineGetMatchResult( Bool &rated, Real &delta, Real &rating, AsciiString &reason )
{
	const OnlineSession &s = OnlineSession::get();
	if (!s.result.size() || s.matchId.empty() || s.result.getString( "match" ) != s.matchId)
		return FALSE;
	rated = s.result.type() == "rated";
	delta = (Real)s.result.getNumber( "delta" );
	rating = (Real)s.result.getNumber( "rating" );
	reason = s.result.getString( "reason" ).c_str();
	return TRUE;
}
