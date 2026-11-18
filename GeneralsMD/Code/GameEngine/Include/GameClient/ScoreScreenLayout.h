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

// FILE: ScoreScreenLayout.h //////////////////////////////////////////////////////////////////////
//
// The score screen's decisions that need no window, no font and no player list, so a test can walk
// every team split of two to eight players through them: who stands with whom, the order the
// standings run in, the "4v4" in the header, how tight the cards pack to fit, and the smoothed
// income the graphs draw.  ScoreScreenGraphs.cpp feeds them and draws what they decide.
////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#ifndef _SCORESCREENLAYOUT_H_
#define _SCORESCREENLAYOUT_H_

#include "Lib/BaseType.h"

#include <algorithm>
#include <string>
#include <vector>

/// a player that was still standing when the match ended
const UnsignedInt SCORE_STILL_STANDING = 0xFFFFFFFFu;

/// the standings' geometry on the 1920x1080 board, under the lobby's: its seats column, its row
/// pitch, its map column and the chat it keeps under the seats
const Real SCORE_LEFT = 124.0f;
const Real SCORE_LEFT_RIGHT = 1284.0f;
const Real SCORE_RIGHT_LEFT = 1308.0f;
const Real SCORE_RIGHT = 1796.0f;
const Real SCORE_TABS_TOP = 158.0f;
const Real SCORE_TABS_BOTTOM = 190.0f;
const Real SCORE_CONTENT_TOP = 196.0f;
const Real SCORE_CONTENT_BOTTOM = 916.0f;
const Real SCORE_CARDS_TOP = 244.0f;				///< under the column heads and their rule
const Real SCORE_CHAT_TOP = 622.0f;					///< the lobby's chat list, 16 under its last seat
const Real SCORE_CHAT_GAP = 16.0f;
const Real SCORE_CHAT_ENTRY_TOP = 872.0f;		///< the lobby's chat line and emote button, to 916
const Real SCORE_CHAT_LIST_BOTTOM = 864.0f;	///< the list's foot over the chat line; with no chat line it runs to 916
const Real SCORE_CHAT_MIN_LIST = 100.0f;		///< what the chat list keeps when the standings push it down: four lines
const Real SCORE_CARD_PITCH = 52.0f;				///< the lobby's ROW_PITCH
const Real SCORE_HEAD_PITCH = 36.0f;				///< a team's band over its cards
const Real SCORE_MIN_CARD_PITCH = 30.0f;		///< one line of a name, the side's line given up
const Real SCORE_MIN_HEAD_PITCH = 22.0f;
const Real SCORE_STRIP_MIN = 90.0f;					///< the match strip under the standings, or none
const Real SCORE_STRIP_MAX = 120.0f;				///< and no taller than this under its label
const Real SCORE_STRIP_BANDS = 48.0f;				///< the strip's bands never less: its marker lanes go first, then the strip

/// -online, after a Quick Match or Play vs AI: the rating card under the two seat cards (both are 1v1),
/// over the chat or the box on how the AI rating moves, which keep the chat's place from SCORE_CHAT_TOP
const Real SCORE_RATING_TOP = 362.0f;
const Real SCORE_RATING_BOTTOM = 590.0f;

/// how long the card's "sending" line holds Exit and the wide button once the send was first seen: a
/// second at least, so a small replay does not flash it, thirty at most, so a server that never answers
/// does not keep the player
const UnsignedInt SCORE_SENDING_MIN_MS = 1000;
const UnsignedInt SCORE_SENDING_MAX_MS = 30000;

/** Whether the buttons are still held shownMs after the send was first seen, sending while the result
	* and replay are on their way. */
inline Bool ScoreScreen_sendingHolds( Bool sending, UnsignedInt shownMs )
{
	return shownMs < SCORE_SENDING_MAX_MS && ( sending || shownMs < SCORE_SENDING_MIN_MS );
}

/// what the card's last line says
enum ScoreRatingLine
{
	SCORE_LINE_SENDING,			///< the result on its way, over a sliding track
	SCORE_LINE_NO_ANSWER,		///< no verdict SCORE_SENDING_MAX_MS after the screen opened
	SCORE_LINE_VERDICT,			///< rated or unrated, as the server said
};

/** The line shownMs after the screen opened, with or without the server's verdict and with the send
	* still going or done.  A verdict that comes after the no-answer line replaces it. */
inline ScoreRatingLine ScoreScreen_ratingLine( Bool verdict, Bool sending, UnsignedInt shownMs )
{
	if( verdict && !sending )
		return SCORE_LINE_VERDICT;
	return shownMs < SCORE_SENDING_MAX_MS ? SCORE_LINE_SENDING : SCORE_LINE_NO_ANSWER;
}

/// one player as the standings see him
struct ScoreSeat
{
	Int team;						///< from ScoreScreen_teamsFromAllies
	Int score;
	Bool winner;
	UnsignedInt fell;		///< the frame he was eliminated on, SCORE_STILL_STANDING if never
};

/// a team in standings order, its members in theirs; a player alone is a team of one
struct ScoreTeam
{
	Int team;
	std::vector< Int > members;		///< indexes into the seats
};

/** Every player's team, numbered from 0 in the order the teams first appear: a player joins the
	* first earlier player he and that player both call ALLIES, as the observer rack works it out.
	* allied is count x count, allied[ a * count + b ] when a calls b an ally. */
inline std::vector< Int > ScoreScreen_teamsFromAllies( Int count, const std::vector< Bool > &allied )
{
	std::vector< Int > team( count, -1 );
	Int teams = 0;
	for( Int a = 0; a < count; a++ )
	{
		for( Int b = 0; b < a && team[ a ] < 0; b++ )
		{
			if( allied[ a * count + b ] && allied[ b * count + a ] )
				team[ a ] = team[ b ];
		}
		if( team[ a ] < 0 )
			team[ a ] = teams++;
	}
	return team;
}

/** The standings' order.  The winning team first; then the rest by when their last member fell,
	* latest first, a team still standing above any that fell; then by the team's summed score.  With
	* no winner at all (the match was left early) the same order holds with nobody first.  Inside a
	* team, by score.  Ties keep the seats' own order. */
inline std::vector< ScoreTeam > ScoreScreen_order( const std::vector< ScoreSeat > &seats )
{
	std::vector< ScoreTeam > teams;
	for( Int each = 0; each < (Int)seats.size(); each++ )
	{
		size_t which = 0;
		while( which < teams.size() && teams[ which ].team != seats[ each ].team )
			which++;
		if( which == teams.size() )
		{
			ScoreTeam team;
			team.team = seats[ each ].team;
			teams.push_back( team );
		}
		teams[ which ].members.push_back( each );
	}

	struct Key
	{
		Bool winner;
		UnsignedInt fell;
		Int score;
	};
	std::vector< Key > keys( teams.size() );
	for( size_t t = 0; t < teams.size(); t++ )
	{
		ScoreTeam &team = teams[ t ];
		std::stable_sort( team.members.begin(), team.members.end(),
			[ &seats ]( Int a, Int b ) { return seats[ a ].score > seats[ b ].score; } );
		Key key = { FALSE, 0, 0 };
		for( size_t m = 0; m < team.members.size(); m++ )
		{
			const ScoreSeat &seat = seats[ team.members[ m ] ];
			key.winner = key.winner || seat.winner;
			key.fell = max( key.fell, seat.fell );
			key.score += seat.score;
		}
		keys[ t ] = key;
	}

	std::vector< Int > order( teams.size() );
	for( size_t t = 0; t < teams.size(); t++ )
		order[ t ] = (Int)t;
	std::stable_sort( order.begin(), order.end(), [ &keys ]( Int a, Int b )
	{
		if( keys[ a ].winner != keys[ b ].winner )
			return keys[ a ].winner;
		if( keys[ a ].fell != keys[ b ].fell )
			return keys[ a ].fell > keys[ b ].fell;
		return keys[ a ].score > keys[ b ].score;
	} );

	std::vector< ScoreTeam > ordered;
	for( size_t t = 0; t < order.size(); t++ )
		ordered.push_back( teams[ order[ t ] ] );
	return ordered;
}

/** The match's format for the header: the team sizes, largest first, joined by "v" ("4v4",
	* "2v2v1").  Three or more players each alone is a free-for-all, which the caller words itself;
	* freeForAll says so and the string is then empty. */
inline std::string ScoreScreen_format( const std::vector< ScoreTeam > &teams, Bool *freeForAll )
{
	std::vector< Int > sizes;
	Int players = 0;
	for( size_t t = 0; t < teams.size(); t++ )
	{
		sizes.push_back( (Int)teams[ t ].members.size() );
		players += (Int)teams[ t ].members.size();
	}
	std::sort( sizes.begin(), sizes.end(), []( Int a, Int b ) { return a > b; } );
	*freeForAll = players >= 3 && !sizes.empty() && sizes[ 0 ] == 1;
	std::string text;
	if( *freeForAll )
		return text;
	for( size_t each = 0; each < sizes.size(); each++ )
	{
		if( each > 0 )
			text += 'v';
		text += std::to_string( sizes[ each ] );
	}
	return text;
}

/** Whether the standings put a band over every team: in any match with a team of two or more, so a
	* player alone in a 2v2v1 reads as his own team and not as one more member of the team above him.
	* A free-for-all has no band at all. */
inline Bool ScoreScreen_banded( const std::vector< ScoreTeam > &teams )
{
	for( size_t t = 0; t < teams.size(); t++ )
	{
		if( teams[ t ].members.size() >= 2 )
			return TRUE;
	}
	return FALSE;
}

/// how many bands the standings carry
inline Int ScoreScreen_bandCount( const std::vector< ScoreTeam > &teams )
{
	return ScoreScreen_banded( teams ) ? (Int)teams.size() : 0;
}

/** The number each team is called by, from 1, the score screen's and the load board's "Team N".
	* sharedLobbyTeam[ t ] is the lobby team every member of team t shares, -1 where they share none
	* or have none; firstSlot[ t ] the lowest lobby seat in team t.  The teams are taken in seat order,
	* whatever order the caller lists them in, so the load board and the score screen's standings
	* call one team by one number.  A team takes its lobby number (lobby team + 1) when no team before
	* it took it; any other takes the lowest number still free, never past highest.  A team called by
	* its lobby number wears that lobby team's colour; the caller tells it by number ==
	* sharedLobbyTeam + 1. */
inline std::vector< Int > ScoreScreen_teamNumbers( const std::vector< Int > &sharedLobbyTeam, const std::vector< Int > &firstSlot, Int highest )
{
	std::vector< Int > seatOrder( sharedLobbyTeam.size() );
	for( size_t t = 0; t < seatOrder.size(); t++ )
		seatOrder[ t ] = (Int)t;
	std::stable_sort( seatOrder.begin(), seatOrder.end(),
		[ &firstSlot ]( Int a, Int b ) { return firstSlot[ a ] < firstSlot[ b ]; } );
	std::vector< Int > number( sharedLobbyTeam.size(), -1 );
	std::vector< Bool > taken( max( highest, 1 ) + 1, FALSE );
	for( size_t each = 0; each < seatOrder.size(); each++ )
	{
		const Int t = seatOrder[ each ];
		const Int lobby = sharedLobbyTeam[ t ];
		if( lobby >= 0 && lobby + 1 < (Int)taken.size() && !taken[ lobby + 1 ] )
		{
			number[ t ] = lobby + 1;
			taken[ lobby + 1 ] = TRUE;
		}
	}
	for( size_t each = 0; each < seatOrder.size(); each++ )
	{
		const Int t = seatOrder[ each ];
		if( number[ t ] > 0 )
			continue;
		Int lowest = 1;
		while( lowest < (Int)taken.size() - 1 && taken[ lowest ] )
			lowest++;
		taken[ lowest ] = TRUE;
		number[ t ] = lowest;
	}
	return number;
}

/// how tight the standings pack
struct ScoreRowFit
{
	Int cardPitch;
	Int headPitch;
	Int used;					///< cards * cardPitch + heads * headPitch
	Bool fits;				///< FALSE only when even the smallest pitches overflow
};

/** The cards' and the team bands' pitch inside available pixels: the lobby's own when they fit,
	* otherwise both pressed down together, the bands in proportion, until they fit or reach their
	* smallest; nothing is ever dropped. */
inline ScoreRowFit ScoreScreen_fitRows( Int cards, Int heads, Int available, Int cardPitch, Int headPitch, Int minCardPitch, Int minHeadPitch )
{
	ScoreRowFit fit;
	fit.fits = TRUE;
	for( Int pitch = cardPitch; pitch >= minCardPitch; pitch-- )
	{
		fit.cardPitch = pitch;
		const Int scaled = headPitch * pitch / max( cardPitch, 1 );
		fit.headPitch = max( minHeadPitch, scaled );
		fit.used = cards * fit.cardPitch + heads * fit.headPitch;
		if( fit.used <= available )
			return fit;
	}
	fit.cardPitch = minCardPitch;
	fit.headPitch = minHeadPitch;
	fit.used = cards * minCardPitch + heads * minHeadPitch;
	fit.fits = fit.used <= available;
	return fit;
}

/** Where the network chat's list starts on the 1080 board, given how the standings fit above the
	* room's own place for it: there when they fit, else just under the cards, but never so low the list
	* keeps less than SCORE_CHAT_MIN_LIST above listBottom.  pixelsPerBoard is the screen's scale. */
inline Real ScoreScreen_chatListTop( const ScoreRowFit &fit, Real pixelsPerBoard, Real listBottom )
{
	if( fit.fits )
		return SCORE_CHAT_TOP;
	const Real underCards = SCORE_CARDS_TOP + fit.used / pixelsPerBoard + SCORE_CHAT_GAP;
	return min( underCards, listBottom - SCORE_CHAT_MIN_LIST );
}

/** The frame the score screen's charts and header end on, read once as the screen opens: the frame
	* one alliance was left on, else the logic's frame then, else the last sample.  The logic's frame
	* reads 0 once the match is torn down under the open screen, which a later look must not take. */
inline UnsignedInt ScoreScreen_endFrame( UnsignedInt decided, UnsignedInt logicFrame, UnsignedInt lastSample )
{
	if( decided > 0 )
		return decided;
	return max( max( logicFrame, lastSample ), (UnsignedInt)1 );
}

/** How many of the samples (frames, oldest first) a chart shows: those up to end, at least the
	* first, none of none. */
inline Int ScoreScreen_shownSamples( const std::vector< UnsignedInt > &frames, UnsignedInt end )
{
	Int count = (Int)frames.size();
	while( count > 1 && frames[ count - 1 ] > end )
		count--;
	return count;
}

/** Income per minute at every sample, from the money earned so far: what came in over the last
	* window frames (at least one sample back) at framesPerMinute.  The first sample has none. */
inline std::vector< Int > ScoreScreen_incomePerMinute( const std::vector< UnsignedInt > &frames, const std::vector< Int > &earned,
	UnsignedInt window, UnsignedInt framesPerMinute )
{
	std::vector< Int > income( frames.size(), 0 );
	for( size_t each = 1; each < frames.size(); each++ )
	{
		size_t from = each - 1;
		while( from > 0 && frames[ each ] - frames[ from - 1 ] <= window )
			from--;
		const UnsignedInt span = frames[ each ] - frames[ from ];
		if( span > 0 )
		{
			const Int came = earned[ each ] - earned[ from ];
			income[ each ] = (Int)( (Int64)max( came, 0 ) * framesPerMinute / span );
		}
	}
	return income;
}

#endif // _SCORESCREENLAYOUT_H_
