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

#include "GameNetwork/Online/OnlineSession.h"

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
	surrendered( FALSE ), duration( 0 ), numPlayers( 0 )
{
	for (Int i = 0; i < MAX_SLOTS; ++i)
		discon[i] = FALSE;
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
	std::vector<long long> discon;
	std::vector<std::string> side;
	for (Int i = 0; i < report.numPlayers && i < MAX_SLOTS; ++i)
	{
		discon.push_back( report.discon[i] ? 1 : 0 );
		side.push_back( report.side[i].str() );
	}
	m.setInts( "discon", discon ).setStrings( "side", side );
	s.send( m );
}
