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

#include "Common/MultiplayerSettings.h"
#include "Common/PlayerTemplate.h"
#include "GameClient/GameText.h"
#include "GameLogic/AI.h"
#include "GameNetwork/GameSpyOverlay.h"
#include "GameNetwork/Online/OnlineSession.h"
#include "zhio.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

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
	surrendered( FALSE ), duration( 0 ), numPlayers( 0 ), numCrcOdd( 0 ), speed( RETAIL_GAME_SPEED_PERCENT )
{
	for (Int i = 0; i < MAX_SLOTS; ++i)
	{
		discon[i] = FALSE;
		blind[i] = 0;
		crcOdd[i] = 0;
	}
}

Bool OnlineSendReport( const OnlineReport &report )
{
	OnlineSession &s = OnlineSession::get();
	if (s.matchId.empty() || s.matchId == s.reportedMatch)
		return FALSE;		// no match, or the score screen's second call
	s.reportedMatch = s.matchId;

	OnlineMessage m;
	m.setString( "t", "report" ).setString( "match", s.matchId );
	m.setInt( "seed", report.seed ).setInt( "endFrame", report.endFrame ).setInt( "winningTeam", report.winningTeam );
	m.setBool( "sawDesync", report.sawDesync != 0 ).setBool( "sawGameEnd", report.sawGameEnd != 0 );
	m.setBool( "disconEnd", report.disconEnd != 0 ).setBool( "surrendered", report.surrendered != 0 );
	m.setInt( "duration", report.duration );
	m.setInt( "hz", LOGICFRAMES_PER_SECOND );	// logic frames a second, so the server can turn endFrame into time
	m.setInt( "speed", report.speed );			// the server settles a match unrated whose speed is not the one it set
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
	// the map's folder and file, as the AI maps are named: a generated map lives under the user folder, whose
	// path names the player's account on this machine and stays here.  A name toLine would refuse is left
	// out, not the whole report with it.
	const std::string path = report.map.str();
	const size_t file = path.find_last_of( "\\/" );
	const size_t folder = file == std::string::npos || file == 0 ? std::string::npos : path.find_last_of( "\\/", file - 1 );
	const std::string map = folder == std::string::npos ? path.substr( file == std::string::npos ? 0 : file + 1 )
		: "maps\\" + path.substr( folder + 1, file - folder - 1 ) + "\\" + path.substr( file + 1 );
	if (!map.empty() && map.size() <= 255 && OnlineIsUtf8( map.data(), map.size() ))
		m.setString( "map", map );
	s.send( m );
	return TRUE;
}

void OnlineCallOffQuickMatch( const char *reason )
{
	OnlineSession &s = OnlineSession::get();
	if (s.matchId.empty() || s.matchId == s.reportedMatch)
		return;
	s.quickMatch.clear();		// nothing was recorded, so no replay goes up under it
	OnlineMessage m;
	m.setString( "t", "qm.abort" ).setString( "match", s.matchId ).setString( "reason", reason );
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
	s.recordingMatch = s.quickMatch;
	s.quickMatch.clear();
	return id;
}

Bool OnlineIsSendingResults( void )
{
	const OnlineSession &s = OnlineSession::get();
	if (s.matchId.empty() || s.reportedMatch != s.matchId)
		return FALSE;		// no report went out for this match
	if (s.recordingMatch == s.matchId && s.replayedMatch != s.matchId)
		return TRUE;		// the recording has not closed: its replay is still to be queued
	if (s.serverAcks)
		return s.reportAnswered != s.matchId || (s.replaySentMatch == s.matchId && s.replayAnswered != s.matchId);
	return s.connection && s.connection->unsentLines() > 0;		// an older server: written is all we can know
}

void OnlineSendReplay( const char *match, const AsciiString &path )
{
	OnlineSession &s = OnlineSession::get();
	if (!match || !*match || s.replayedMatch == match)
		return;		// a custom game, or this match's replay went up (or was given up) already
	if (s.matchId != match && s.aiWonMatch != match)
		return;		// a later match or a logout replaced it, or an AI match that was not won
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
		m.setString( "t", "replay" ).setString( "match", match ).setInt( "seq", seq );
		m.setBool( "last", at + len == bytes.size() );
		m.setString( "data", base64( (const unsigned char *)bytes.data() + at, len ) );
		s.send( m );
	}
	s.replaySentMatch = match;
	DEBUG_LOG(("ONLINE REPLAY match=%s bytes=%d lines=%d\n", match, (Int)bytes.size(), seq));
}

//-------------------------------------------------------------------------------------------------
// ratings, the ladder, the reveal and the verdict, for the screens
//-------------------------------------------------------------------------------------------------

OnlinePlayerInfo::OnlinePlayerInfo() :
	ping( -1 ), hasRating( FALSE ), rating( 0 ), exactRating( 0.0f ), hasElo( FALSE ), elo( 0.0f ), games( -1 ), wins( -1 ), losses( -1 ), rank( 0 ),
	notOnline( FALSE ), hasStats( FALSE ), hasAIRating( FALSE ), aiRating( 0.0f ), aiGames( 0 ), aiWins( 0 ), serial( 0 )
{
}

OnlineLadderRow::OnlineLadderRow() : rank( 0 ), hasRating( FALSE ), rating( 0 ), exactRating( 0.0f ), hasElo( FALSE ), elo( 0.0f ),
	wins( 0 ), losses( 0 ),
	side( -1 ), color( -1 )
{
}

// the whole number the %d screens print, from the two-decimal rating
static Int roundRating( double rating )
{
	return (Int)floor( rating + 0.5 );
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
	out.hasRating = c.hasRating || c.hasElo;
	out.exactRating = (Real)c.rating;
	out.hasElo = c.hasElo;
	out.elo = (Real)c.elo;
	out.rating = roundRating( c.hasElo ? c.elo : c.rating );
	out.tier = c.tier.c_str();
	out.games = (Int)c.games;
	out.wins = (Int)c.wins;
	out.losses = (Int)c.losses;
	out.rank = (Int)c.rank;
	out.notOnline = c.notOnline;
	out.hasStats = c.hasStats;
	out.hasAIRating = c.hasAIRating;
	out.aiRating = (Real)c.aiRating;
	out.aiGames = (Int)c.aiGames;
	out.aiWins = (Int)c.aiWins;
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
		text.format( TheGameText->fetch( info.hasElo ? "GUI:OnlineEloTier" : "GUI:OnlineRatingTier" ), OnlineTierName( info.tier ).str(), info.rating );
	else if (info.hasRating)
		text.format( TheGameText->fetch( info.hasElo ? "GUI:OnlineElo" : "GUI:OnlineRating" ), info.rating );
	else if (info.tier.isNotEmpty())
		text = OnlineTierName( info.tier );
	return text;
}

//-------------------------------------------------------------------------------------------------
// the notice's and a maintenance refusal's until
//-------------------------------------------------------------------------------------------------
long long OnlineParseUtc( const char *iso )
{
	int y, mo, d, h, mi, s = 0, used = 0;
	if (!iso || !isdigit( (unsigned char)iso[0] )
		|| sscanf( iso, "%4d-%2d-%2dT%2d:%2d%n", &y, &mo, &d, &h, &mi, &used ) != 5 || used != 16)
		return -1;
	const char *p = iso + used;
	if (*p == ':')
	{
		if (!isdigit( (unsigned char)p[1] ) || !isdigit( (unsigned char)p[2] ))
			return -1;
		s = (p[1] - '0') * 10 + (p[2] - '0');
		p += 3;
		if (*p == '.')
		{
			for (++p; isdigit( (unsigned char)*p ); ++p) {}
		}
	}
	long long offset = 0;
	if (*p == 'Z')
		++p;
	else if ((*p == '+' || *p == '-') && isdigit( (unsigned char)p[1] ) && isdigit( (unsigned char)p[2] ) && p[3] == ':'
		&& isdigit( (unsigned char)p[4] ) && isdigit( (unsigned char)p[5] ))
	{
		offset = (((p[1] - '0') * 10 + (p[2] - '0')) * 60 + (p[4] - '0') * 10 + (p[5] - '0')) * 60LL;
		if (*p == '-')
			offset = -offset;
		p += 6;
	}
	else
		return -1;		// a time with no zone is no time the server sends
	if (*p || mo < 1 || mo > 12 || d < 1 || d > 31 || h > 23 || mi > 59 || s > 60)
		return -1;
	// days since 1970-01-01 for a proleptic Gregorian date (Howard Hinnant's days_from_civil)
	const long long yy = mo <= 2 ? y - 1 : y;
	const long long era = (yy >= 0 ? yy : yy - 399) / 400;
	const long long yoe = yy - era * 400;
	const long long doy = (153 * (mo + (mo > 2 ? -3 : 9)) + 2) / 5 + d - 1;
	const long long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
	const long long days = era * 146097 + doe - 719468;
	return days * 86400 + h * 3600 + mi * 60 + s - offset;
}

UnicodeString OnlineUntilText( const AsciiString &until )
{
	UnicodeString text;
	if (until.isEmpty())
		return text;
	const long long when = OnlineParseUtc( until.str() );
	const time_t t = (time_t)when;
	const struct tm *at = when >= 0 ? localtime( &t ) : NULL;
	if (!at)
	{
		text.translate( until );		// not a time this reads: the server's own words
		return text;
	}
	const struct tm local = *at;
	const time_t nowT = time( NULL );
	const struct tm *now = localtime( &nowT );
	if (now && now->tm_year == local.tm_year && now->tm_yday == local.tm_yday)
		text.format( u"%02d:%02d", local.tm_hour, local.tm_min );
	else if (TheGlobalData && TheGlobalData->m_textLanguage != TEXT_LANGUAGE_ENGLISH)
		text.format( u"%02d.%02d.%04d %02d:%02d", local.tm_mday, local.tm_mon + 1, local.tm_year + 1900, local.tm_hour, local.tm_min );
	else
		text.format( u"%04d-%02d-%02d %02d:%02d", local.tm_year + 1900, local.tm_mon + 1, local.tm_mday, local.tm_hour, local.tm_min );
	return text;
}

UnicodeString OnlineMaintenanceText( const AsciiString &until )
{
	UnicodeString text;
	if (!TheGameText)
		return text;
	const UnicodeString when = OnlineUntilText( until );
	if (when.isEmpty())
		return TheGameText->fetch( "GUI:OnlineMaintenance" );
	text.format( TheGameText->fetch( "GUI:OnlineMaintenanceUntil" ), when.str() );
	return text;
}

Bool OnlineShowMaintenanceRefusal( void )
{
	AsciiString until;
	if (!OnlineIsEnabled() || !OnlineTakeMaintenanceRefusal( until ))
		return FALSE;
	GSMessageBoxOk( TheGameText->fetch( "GUI:OnlineMaintenanceTitle" ), OnlineMaintenanceText( until ) );
	return TRUE;
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
	const OnlineValue *rating = m.find( "rating" ), *elo = m.find( "elo" );
	const Bool hasOld = rating && rating->kind == OnlineValue::NUMBER;
	r.exactRating = hasOld ? (Real)m.getNumber( "rating" ) : 0.0f;
	r.hasElo = elo && elo->kind == OnlineValue::NUMBER;
	r.elo = r.hasElo ? (Real)m.getNumber( "elo" ) : 0.0f;
	r.hasRating = hasOld || r.hasElo;
	r.rating = r.hasRating ? roundRating( r.hasElo ? m.getNumber( "elo" ) : m.getNumber( "rating" ) ) : 0;
	r.tier = m.getString( "tier" ).c_str();
	r.wins = (Int)m.getInt( "wins", 0 );
	r.losses = (Int)m.getInt( "losses", 0 );
	// qm.matched's opponents: the seat's pick, clamped as a seat clamps it, so a screen may index with it
	r.side = OnlineSeatSide( m.getInt( "side", -1 ), ThePlayerTemplateStore ? ThePlayerTemplateStore->getPlayerTemplateCount() : 0 );
	r.color = OnlineSeatColor( m.getInt( "color", -1 ), TheMultiplayerSettings ? TheMultiplayerSettings->getNumColors() : 0 );
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
	out.hasChance = FALSE;
	out.chance = 0.0f;
	out.speed = RETAIL_GAME_SPEED_PERCENT;
	out.speedAsk = out.speedWait = 0;
	out.speedFinal = FALSE;
	out.serial = s.revealSerial;
	if (!s.revealing)
		return FALSE;
	out.speed = OnlineMatchSpeed( s.reveal.getInt( "speed", RETAIL_GAME_SPEED_PERCENT ) );
	out.speedAsk = s.reveal.has( "speedAsk" ) ? OnlineMatchSpeed( s.reveal.getInt( "speedAsk" ) ) : 0;
	out.speedWait = s.reveal.has( "speedWait" ) ? OnlineMatchSpeed( s.reveal.getInt( "speedWait" ) ) : 0;
	out.speedFinal = s.reveal.getInt( "speedFinal" ) != 0;
	const std::vector<OnlineMessage> got = s.reveal.getObjects( "opponents" );
	for (size_t i = 0; i < got.size(); ++i)
		out.opponents.push_back( ladderRow( got[i] ) );
	out.server = s.reveal.getString( "server" ).c_str();
	out.revealMs = (Int)s.reveal.getInt( "revealMs", 0 );
	const OnlineValue *chance = s.reveal.find( "chance" );
	out.hasChance = chance && chance->kind == OnlineValue::NUMBER;
	out.chance = (Real)s.reveal.getNumber( "chance" );
	return TRUE;
}

void OnlineAnswerSpeedAsk( Bool ok )
{
	OnlineSession &s = OnlineSession::get();
	if (!s.revealing || !s.reveal.has( "speedAsk" ) || s.reveal.getInt( "speedFinal" ) != 0)
		return;		// no ask open: the reveal went, or the server settled it already
	OnlineMessage m;
	m.setString( "t", "qm.speedok" ).setString( "match", s.reveal.getString( "match" ) ).setInt( "ok", ok ? 1 : 0 );
	s.send( m );
	DEBUG_LOG(("ONLINE SPEED ANSWER match=%s ok=%d\n", s.reveal.getString( "match" ).c_str(), ok ? 1 : 0));
}

static const char *const HISTORY_MODES[] = { "all", "qm", "ai" };		// by OnlineHistoryMode

// a mode's name as OnlineHistoryMode, -1 for one this build does not know
static Int historyMode( const std::string &name )
{
	for (Int k = ONLINE_HISTORY_ALL; k <= ONLINE_HISTORY_AI; ++k)
		if (name == HISTORY_MODES[k])
			return k;
	return -1;
}

void OnlineRequestHistory( Int id, OnlineHistoryMode mode, Int limit )
{
	OnlineSession &s = OnlineSession::get();
	if (!s.authed || mode < ONLINE_HISTORY_ALL || mode > ONLINE_HISTORY_AI)
		return;
	s.expireHistory();
	const OnlineHistoryKey key( id ? id : s.id, mode );
	OnlineHistoryEntry &e = s.history[key];
	if (e.askedMs)
		return;		// one ask per key: its answer, its error or its timeout comes first
	e.state = ONLINE_LADDER_WAITING;
	e.askedMs = OnlineNowMs();
	e.serial = ++s.historySerial;
	OnlineMessage m;
	m.setString( "t", "history" );
	if (id)
		m.setInt( "id", id );
	m.setString( "mode", HISTORY_MODES[mode] ).setInt( "limit", limit < 1 ? 1 : limit > 20 ? 20 : limit );
	s.send( m );
	s.historyAsked.push_back( key );
}

void OnlineSession::historySettled( const OnlineHistoryKey &key, Int state )
{
	OnlineHistoryEntry &e = history[key];
	e.state = state;
	e.askedMs = 0;
	e.serial = ++historySerial;
	for (std::deque<OnlineHistoryKey>::iterator it = historyAsked.begin(); it != historyAsked.end(); ++it)
	{
		if (*it == key)
		{
			historyAsked.erase( it );
			break;
		}
	}
}

void OnlineSession::noteHistory( const OnlineMessage &m )
{
	const Int k = historyMode( m.getString( "mode" ) );
	if (k < 0)
	{
		DEBUG_LOG(("online: ignored a history line for mode %s\n", m.getString( "mode" ).c_str()));
		return;
	}
	const OnlineHistoryKey key( m.getInt( "id", id ), k );
	history[key].reply = m;
	historySettled( key, ONLINE_LADDER_READY );
}

void OnlineSession::historyRefused( const OnlineMessage &m )
{
	// a server that names the ask in its error (id, mode) is matched on them, since a throttled ask is
	// answered out of order; an older one names nothing, and its errors come in the order of the asks
	const Int k = historyMode( m.getString( "mode" ) );
	if (k >= 0)
		historySettled( OnlineHistoryKey( m.getInt( "id", id ), k ), ONLINE_LADDER_FAILED );
	else if (!historyAsked.empty())
		historySettled( historyAsked.front(), ONLINE_LADDER_FAILED );
}

void OnlineSession::expireHistory()
{
	// a server that never answers (a handler that threw) must not leave a key waiting for ever
	const long long now = OnlineNowMs();
	for (std::map<OnlineHistoryKey, OnlineHistoryEntry>::iterator it = history.begin(); it != history.end(); ++it)
		if (it->second.askedMs && now - it->second.askedMs > ONLINE_HISTORY_TIMEOUT_MS)
			historySettled( it->first, ONLINE_LADDER_FAILED );		// an existing key: no insert, the iterator holds
}

UnsignedInt OnlineGetHistorySerial( Int id, OnlineHistoryMode mode )
{
	OnlineSession &s = OnlineSession::get();
	s.expireHistory();
	std::map<OnlineHistoryKey, OnlineHistoryEntry>::const_iterator it = s.history.find( OnlineHistoryKey( id ? id : s.id, mode ) );
	return it == s.history.end() ? 0 : it->second.serial;
}

static void historyArmies( const OnlineMessage &m, const char *key, std::vector<OnlineHistoryArmy> &out )
{
	const std::vector<OnlineMessage> got = m.getObjects( key );
	for (size_t i = 0; i < got.size(); ++i)
	{
		OnlineHistoryArmy a;
		a.army = got[i].getString( "army" ).c_str();
		a.games = (Int)got[i].getInt( "games" );
		a.wins = (Int)got[i].getInt( "wins" );
		out.push_back( a );
	}
}

OnlineLadderState OnlineGetHistory( Int id, OnlineHistoryMode mode, OnlineHistory &out )
{
	OnlineSession &s = OnlineSession::get();
	s.expireHistory();
	out = OnlineHistory();
	std::map<OnlineHistoryKey, OnlineHistoryEntry>::const_iterator it = s.history.find( OnlineHistoryKey( id ? id : s.id, mode ) );
	if (it == s.history.end())
		return ONLINE_LADDER_FAILED;
	const OnlineMessage &m = it->second.reply;
	out.arrived = m.size() > 0;
	out.id = (Int)m.getInt( "id", it->first.first );
	out.games = (Int)m.getInt( "games" );
	out.wins = (Int)m.getInt( "wins" );
	const std::vector<OnlineMessage> rows = m.getObjects( "rows" );
	for (size_t i = 0; i < rows.size(); ++i)
	{
		OnlineHistoryRow r;
		r.ai = rows[i].getString( "mode" ) == "ai";
		r.won = rows[i].getString( "result" ) == "win";
		r.map = rows[i].getString( "map" ).c_str();
		r.opponent = rows[i].getString( "opponent" ).c_str();
		r.opponentRating = (Real)rows[i].getNumber( "opponentRating" );
		// the bot's strength is on the engine's scale before Elo; the AI's figures are Elo now
		if (r.ai)
			r.opponentRating = aiRatingOfLegacy( r.opponentRating );
		r.playerArmy = rows[i].getString( "playerArmy" ).c_str();
		r.opponentArmy = rows[i].getString( "opponentArmy" ).c_str();
		r.seconds = (Int)rows[i].getInt( "seconds" );
		r.change = (Real)rows[i].getNumber( "change" );
		// a Quick Match row from a server with Elo: the plain opponentElo and eloChange beside the old scale's
		const OnlineValue *opponentElo = rows[i].find( "opponentElo" );
		if (!r.ai && opponentElo && opponentElo->kind == OnlineValue::NUMBER)
		{
			r.hasElo = TRUE;
			r.opponentRating = (Real)rows[i].getNumber( "opponentElo" );
			r.change = (Real)rows[i].getNumber( "eloChange" );
		}
		r.age = (Int)rows[i].getInt( "age" );
		out.rows.push_back( r );
	}
	historyArmies( m, "byArmy", out.byArmy );
	historyArmies( m, "byOpponentArmy", out.byOpponentArmy );
	return (OnlineLadderState)it->second.state;
}

Bool OnlineGetMatchResult( Bool &rated, Real &delta, Real &rating, AsciiString &reason, Bool *elo )
{
	const OnlineSession &s = OnlineSession::get();
	if (!s.result.size() || s.matchId.empty() || s.result.getString( "match" ) != s.matchId)
		return FALSE;
	rated = s.result.type() == "rated";
	// delta and rating, or before and after from a server that sends those instead
	const double after = s.result.getNumber( "after" );
	delta = (Real)s.result.getNumber( "delta", after - s.result.getNumber( "before", after ) );
	rating = (Real)s.result.getNumber( "rating", after );
	reason = s.result.getString( "reason" ).c_str();
	if (elo)
	{
		const OnlineValue *eloAfter = s.result.find( "eloAfter" );
		*elo = eloAfter && eloAfter->kind == OnlineValue::NUMBER;
		if (*elo)
		{
			const double now = s.result.getNumber( "eloAfter" );
			delta = (Real)s.result.getNumber( "eloDelta", now - s.result.getNumber( "eloBefore", now ) );
			rating = (Real)now;
		}
	}
	return TRUE;
}
