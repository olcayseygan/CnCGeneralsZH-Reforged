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

// FILE: DirectStrikeStats.cpp /////////////////////////////////////////////////////////////////////////
//
// A Direct Strike match's record and its end; DirectStrikeStats.h says what is kept and where it comes from.
// Logic state: it counts inside the logic frame, from the director's lists and the logic's own
// damage and kill paths, and reads nothing of the client.
//
///////////////////////////////////////////////////////////////////////////////////////////////////

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/ThingFactory.h"
#include "Common/ThingTemplate.h"
#include "Common/Xfer.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/DirectStrikeStats.h"
#include "GameLogic/Module/BodyModule.h"
#include "GameLogic/Object.h"

static DirectStrikeStats theDirectStrikeStats;
DirectStrikeStats *TheDirectStrikeStats = &theDirectStrikeStats;

static const char TEAM_LETTER[ DIRECTSTRIKE_TEAMS ] = { 'A', 'B' };

//-------------------------------------------------------------------------------------------------
Int DirectStrikeSeatStats::totalEarned( void ) const
{
	Int sum = 0;
	for( Int k = 0; k < DIRECTSTRIKE_EARN_KINDS; ++k )
		sum += earned[ k ];
	return sum;
}

Int DirectStrikeSeatStats::totalSpent( void ) const
{
	Int sum = 0;
	for( Int k = 0; k < DIRECTSTRIKE_SPEND_KINDS; ++k )
		sum += spent[ k ];
	return sum;
}

const ThingTemplate *DirectStrikeSeatStats::mvp( Int *count ) const
{
	const DirectStrikeTemplateCount *best = NULL;
	for( size_t i = 0; i < killsBy.size(); ++i )
		if( best == NULL || killsBy[ i ].count > best->count )
			best = &killsBy[ i ];
	if( count )
		*count = best ? best->count : 0;
	return best ? best->thing : NULL;
}

/** One more of the template, a new entry at the end the first time.  By name: a map.ini override
	  makes a second template of the same name, and a load finds the base one by its name. */
static void bump( std::vector<DirectStrikeTemplateCount> &list, const ThingTemplate *thing )
{
	if( thing == NULL )
		return;
	for( size_t i = 0; i < list.size(); ++i )
		if( list[ i ].thing != NULL && list[ i ].thing->getName() == thing->getName() )
		{
			++list[ i ].count;
			return;
		}
	DirectStrikeTemplateCount entry;
	entry.thing = thing;
	entry.count = 1;
	list.push_back( entry );
}

static Int healthOf( ObjectID id )
{
	const Object *obj = TheGameLogic->findObjectByID( id );
	if( obj == NULL || obj->isEffectivelyDead() || obj->getBodyModule() == NULL )
		return 0;
	return REAL_TO_INT_CEIL( obj->getBodyModule()->getHealth() );
}

//-------------------------------------------------------------------------------------------------
DirectStrikeStats::DirectStrikeStats( void )
{
	reset();
}

void DirectStrikeStats::reset( void )
{
	for( Int t = 0; t < DIRECTSTRIKE_TEAMS; ++t )
	{
		for( Int s = 0; s < DIRECTSTRIKE_SEATS; ++s )
		{
			DirectStrikeSeatStats &st = m_seats[ t ][ s ];
			st.waves = st.unitsSent = st.placed = st.kills = 0;
			st.placedBy.clear();
			st.killsBy.clear();
			for( Int k = 0; k < DIRECTSTRIKE_EARN_KINDS; ++k )
				st.earned[ k ] = 0;
			for( Int k = 0; k < DIRECTSTRIKE_SPEND_KINDS; ++k )
				st.spent[ k ] = 0;
			st.towerDamage = st.fortressDamage = 0.0f;
			st.middleFrames = 0;
		}
		m_teamMiddleFrames[ t ] = 0;
		m_downFrame[ t ] = 0;
		m_towerDownFrame[ t ][ 0 ] = m_towerDownFrame[ t ][ 1 ] = 0;
		for( Int s = 0; s < DIRECTSTRIKE_SEATS; ++s )
			m_seatLeftFrame[ t ][ s ] = 0;
		m_teamLeftFrame[ t ] = 0;
	}
	m_samples.clear();
	m_logged = FALSE;
}

//-------------------------------------------------------------------------------------------------
Int DirectStrikeStats::getEndPhase( void ) const
{
	const UnsignedInt end = getEndFrame();
	if( end == 0 )
		return DIRECTSTRIKE_END_PLAYING;
	return TheGameLogic->getFrame() >= end ? DIRECTSTRIKE_END_OVER : DIRECTSTRIKE_END_COLLAPSE;
}

UnsignedInt DirectStrikeStats::getEndFrame( void ) const
{
	UnsignedInt end = 0;
	for( Int t = 0; t < DIRECTSTRIKE_TEAMS; ++t )
	{
		if( m_downFrame[ t ] != 0 && ( end == 0 || m_downFrame[ t ] + DIRECTSTRIKE_COLLAPSE_FRAMES < end ) )
			end = m_downFrame[ t ] + DIRECTSTRIKE_COLLAPSE_FRAMES;
		if( m_teamLeftFrame[ t ] != 0 && ( end == 0 || m_teamLeftFrame[ t ] < end ) )
			end = m_teamLeftFrame[ t ];
	}
	return end;
}

/** Out on the end frame: every team whose fortress fell before it, or that left by it.  The end frame is
	  the first fall's collapse run out, so a fortress that falls while the other one is still coming down
	  takes its team out too, and the match is a draw; a team that leaves ends the match on that frame. */
Bool DirectStrikeStats::isOut( Int team, UnsignedInt end ) const
{
	return ( m_downFrame[ team ] != 0 && m_downFrame[ team ] < end ) || ( m_teamLeftFrame[ team ] != 0 && m_teamLeftFrame[ team ] <= end );
}

Int DirectStrikeStats::getWinner( void ) const
{
	const UnsignedInt end = getEndFrame();
	if( getEndPhase() != DIRECTSTRIKE_END_OVER || ( isOut( 0, end ) && isOut( 1, end ) ) )
		return -1;
	return isOut( 0, end ) ? 1 : 0;
}

Bool DirectStrikeStats::hasLost( const Player *player ) const
{
	Int team, seat;
	if( !TheDirectStrikeDirector->isActive() || !TheDirectStrikeDirector->seatOf( player, &team, &seat ) )
		return FALSE;
	return getEndPhase() == DIRECTSTRIKE_END_OVER && isOut( team, getEndFrame() );
}

//-------------------------------------------------------------------------------------------------
void DirectStrikeStats::onPlayerLeft( const Player *player )
{
	Int team, seat;
	if( !counting() || player == NULL || !TheDirectStrikeDirector->seatOf( player, &team, &seat ) || m_seatLeftFrame[ team ][ seat ] != 0 )
		return;
	const UnsignedInt now = TheGameLogic->getFrame();
	m_seatLeftFrame[ team ][ seat ] = now > 0 ? now : 1;

	// the first seated teammate still playing takes what the leaver held of the team's buildings
	Player *heir = NULL;
	for( Int s = 0; s < DIRECTSTRIKE_SEATS && heir == NULL; ++s )
		if( TheDirectStrikeDirector->getSeatPlayer( team, s ) >= 0 && m_seatLeftFrame[ team ][ s ] == 0 )
			heir = ThePlayerList->getNthPlayer( TheDirectStrikeDirector->getSeatPlayer( team, s ) );
	DEBUG_LOG(( "HEADLESS DIRECTSTRIKE END: frame %u player %d of team %c left the game, %s\n", now, (Int)player->getPlayerIndex(),
							TEAM_LETTER[ team ], heir ? "the team plays on" : "nobody of the team plays on" ));
	if( heir == NULL )
	{
		m_teamLeftFrame[ team ] = m_seatLeftFrame[ team ][ seat ];
		DEBUG_LOG(( "HEADLESS DIRECTSTRIKE END: frame %u team %c left the game, team %c wins\n", now, TEAM_LETTER[ team ], TEAM_LETTER[ 1 - team ] ));
		return;
	}
	const ObjectID buildings[] = { TheDirectStrikeDirector->getFortress( team ), TheDirectStrikeDirector->getTower( team, 0 ),
																 TheDirectStrikeDirector->getTower( team, 1 ) };
	for( Int i = 0; i < (Int)ARRAY_SIZE( buildings ); ++i )
	{
		Object *obj = TheGameLogic->findObjectByID( buildings[ i ] );
		if( obj != NULL && !obj->isEffectivelyDead() && obj->getControllingPlayer() == player )
			obj->setTeam( heir->getDefaultTeam() );
	}
}

//-------------------------------------------------------------------------------------------------
/// a match is counted until it is decided; what the collapse's last seconds bring still counts
Bool DirectStrikeStats::counting( void ) const
{
	return TheDirectStrikeDirector->isActive() && getEndPhase() != DIRECTSTRIKE_END_OVER;
}

DirectStrikeSeatStats *DirectStrikeStats::seatStats( const Player *player )
{
	Int team, seat;
	if( player == NULL || !TheDirectStrikeDirector->seatOf( player, &team, &seat ) )
		return NULL;
	return &m_seats[ team ][ seat ];
}

/// a seated player's unit that is not one of his team's towers or its fortress
Bool DirectStrikeStats::isWaveUnit( const Object *obj, Int *team ) const
{
	Int seat;
	if( obj == NULL || !TheDirectStrikeDirector->seatOf( obj->getControllingPlayer(), team, &seat ) )
		return FALSE;
	const ObjectID id = obj->getID();
	return id != TheDirectStrikeDirector->getFortress( *team ) && id != TheDirectStrikeDirector->getTower( *team, 0 )
		&& id != TheDirectStrikeDirector->getTower( *team, 1 );
}

//-------------------------------------------------------------------------------------------------
void DirectStrikeStats::sample( UnsignedInt now, Int middle )
{
	DirectStrikeSample s;
	s.frame = now;
	s.middleHolder = TheDirectStrikeDirector->getMiddleHolder();
	s.middle = middle;
	for( Int t = 0; t < DIRECTSTRIKE_TEAMS; ++t )
	{
		s.fortress[ t ] = healthOf( TheDirectStrikeDirector->getFortress( t ) );
		for( Int k = 0; k < 2; ++k )
			s.tower[ t ][ k ] = healthOf( TheDirectStrikeDirector->getTower( t, k ) );
	}
	m_samples.push_back( s );
}

void DirectStrikeStats::update( void )
{
	if( !TheDirectStrikeDirector->isActive() )
		return;
	const UnsignedInt now = TheGameLogic->getFrame();

	// who holds the middle this frame, counted as the director counts it on a tick
	Int pastLine[ DIRECTSTRIKE_TEAMS ] = { 0, 0 };
	Bool past[ DIRECTSTRIKE_TEAMS ][ DIRECTSTRIKE_SEATS ] = { { FALSE } };
	const Coord3D *middle = TheDirectStrikeDirector->getMiddle();
	for( Int i = 0; i < TheDirectStrikeDirector->getCopyCount(); ++i )
	{
		const Object *obj = TheGameLogic->findObjectByID( TheDirectStrikeDirector->getCopyID( i ) );
		Int team, seat;
		if( obj == NULL || obj->isEffectivelyDead() || !TheDirectStrikeDirector->seatOf( obj->getControllingPlayer(), &team, &seat ) )
			continue;
		if( ( obj->getPosition()->x - middle->x ) * TheDirectStrikeDirector->towardEnemy( team ) > 0.0f )
		{
			++pastLine[ team ];
			past[ team ][ seat ] = TRUE;
		}
	}
	const Int pastBoth = pastLine[ 0 ] + pastLine[ 1 ];
	const Int share = pastBoth > 0 ? ( pastLine[ 0 ] - pastLine[ 1 ] ) * 100 / pastBoth : 0;

	if( m_samples.empty() )
		sample( now, share );

	// the director marks a fortress down on the frame after it died; the collapse's time starts there
	if( counting() )
		for( Int t = 0; t < DIRECTSTRIKE_TEAMS; ++t )
		{
			for( Int k = 0; k < 2; ++k )
				if( m_towerDownFrame[ t ][ k ] == 0 && TheDirectStrikeDirector->getTower( t, k ) != INVALID_ID
						&& healthOf( TheDirectStrikeDirector->getTower( t, k ) ) == 0 )
				{
					m_towerDownFrame[ t ][ k ] = now;
					DEBUG_LOG(( "HEADLESS DIRECTSTRIKE END: frame %u tower %c%d down\n", now, TEAM_LETTER[ t ], k + 1 ));
				}
			if( m_downFrame[ t ] == 0 && TheDirectStrikeDirector->isFortressDown( t ) )
			{
				m_downFrame[ t ] = now;
				sample( now, share );
				DEBUG_LOG(( "HEADLESS DIRECTSTRIKE END: frame %u fortress %c down, its collapse plays until frame %u, then team %c is out\n",
										now, TEAM_LETTER[ t ], now + DIRECTSTRIKE_COLLAPSE_FRAMES, TEAM_LETTER[ t ] ));
			}
		}

	if( counting() )
	{
		const Int holder = DirectStrike_middleHolder( pastLine[ 0 ], pastLine[ 1 ] );
		if( holder >= 0 )
		{
			++m_teamMiddleFrames[ holder ];
			for( Int s = 0; s < DIRECTSTRIKE_SEATS; ++s )
				if( past[ holder ][ s ] )
					++m_seats[ holder ][ s ].middleFrames;
		}
		// ponytail: every wall second for the whole match, about 1000 samples in 16 minutes, all of them in
		// every CRC; thin them out if the CRC's time shows
		if( now % LOGICFRAMES_PER_SECOND == 0 )
			sample( now, share );
	}

	if( !m_logged && getEndPhase() == DIRECTSTRIKE_END_OVER )
	{
		m_logged = TRUE;
		sample( now, share );
		log();
	}
}

//-------------------------------------------------------------------------------------------------
void DirectStrikeStats::onPlace( const Player *player, const ThingTemplate *tmpl, Int cost )
{
	DirectStrikeSeatStats *st = counting() ? seatStats( player ) : NULL;
	if( st == NULL )
		return;
	++st->placed;
	st->spent[ DIRECTSTRIKE_SPEND_UNITS ] += cost;
	bump( st->placedBy, tmpl );
}

void DirectStrikeStats::onOrder( const Player *player, Int kind, UnsignedInt moneyBefore, UnsignedInt moneyAfter )
{
	DirectStrikeSeatStats *st = counting() && kind != DIRECTSTRIKE_ORDER_PLACE ? seatStats( player ) : NULL;
	if( st == NULL || moneyBefore == moneyAfter )
		return;
	if( moneyAfter > moneyBefore )
	{
		st->earned[ DIRECTSTRIKE_EARN_REFUNDS ] += (Int)( moneyAfter - moneyBefore );
		return;
	}
	const Int spendKind = kind == DIRECTSTRIKE_ORDER_TIER ? DIRECTSTRIKE_SPEND_TIERS
											: kind == DIRECTSTRIKE_ORDER_VETERANCY ? DIRECTSTRIKE_SPEND_RANKS
											: kind == DIRECTSTRIKE_ORDER_GARRISON ? DIRECTSTRIKE_SPEND_PASSENGERS
											: kind == DIRECTSTRIKE_ORDER_UPGRADE || kind == DIRECTSTRIKE_ORDER_UNIT_UPGRADE || kind == DIRECTSTRIKE_ORDER_REVEAL ? DIRECTSTRIKE_SPEND_UPGRADES
											: DIRECTSTRIKE_SPEND_UNITS;
	st->spent[ spendKind ] += (Int)( moneyBefore - moneyAfter );
}

void DirectStrikeStats::onIncome( Int playerIndex, UnsignedInt pay, UnsignedInt bonus )
{
	DirectStrikeSeatStats *st = counting() ? seatStats( ThePlayerList->getNthPlayer( playerIndex ) ) : NULL;
	if( st == NULL )
		return;
	st->earned[ DIRECTSTRIKE_EARN_INCOME ] += (Int)( pay - bonus );
	st->earned[ DIRECTSTRIKE_EARN_MIDDLE ] += (Int)bonus;
}

void DirectStrikeStats::onWave( Int playerIndex, Int units )
{
	DirectStrikeSeatStats *st = counting() && units > 0 ? seatStats( ThePlayerList->getNthPlayer( playerIndex ) ) : NULL;
	if( st == NULL )
		return;
	++st->waves;
	st->unitsSent += units;
}

void DirectStrikeStats::onKill( const Object *killer, const Object *victim )
{
	Int team;
	if( !counting() || victim == NULL || !isWaveUnit( killer, &team ) )
		return;
	DirectStrikeSeatStats *st = seatStats( killer->getControllingPlayer() );
	++st->kills;
	bump( st->killsBy, killer->getTemplate() );
}

void DirectStrikeStats::onDamage( const Object *damager, const Object *victim, Real amount )
{
	Int team;
	if( !counting() || victim == NULL || amount <= 0.0f || !isWaveUnit( damager, &team ) )
		return;
	const ObjectID id = victim->getID();
	const Int enemy = 1 - team;
	DirectStrikeSeatStats *st = seatStats( damager->getControllingPlayer() );
	const Real before = st->towerDamage + st->fortressDamage;
	if( id == TheDirectStrikeDirector->getFortress( enemy ) )
		st->fortressDamage += amount;
	else if( id == TheDirectStrikeDirector->getTower( enemy, 0 ) || id == TheDirectStrikeDirector->getTower( enemy, 1 ) )
		st->towerDamage += amount;
	else
		return;

	/* Pillage (2026-11-24, the owner's lever for match length): health taken off an enemy tower or fortress
		 pays the player whose unit took it DIRECTSTRIKE_PILLAGE_PERCENT of it.  Paid off the running total this
		 record already saves, so a hit's fraction carries to the next and no state is added. */
	const Int paid = DirectStrike_pillageOf( before );
	const Int owed = DirectStrike_pillageOf( st->towerDamage + st->fortressDamage );
	if( owed > paid )
	{
		Player *player = damager->getControllingPlayer();
		player->getMoney()->deposit( (UnsignedInt)( owed - paid ), FALSE );
		onIncome( (Int)player->getPlayerIndex(), (UnsignedInt)( owed - paid ), 0 );
	}
}

//-------------------------------------------------------------------------------------------------
void DirectStrikeStats::log( void ) const
{
	const Int winner = getWinner();
	DEBUG_LOG(( "HEADLESS DIRECTSTRIKE STATS: frame %u, decided on %u, winner %s, fortress A down at %u, B at %u, team A left at %u, B at %u, "
							"middle held A %u B %u frames, %d samples\n",
							TheGameLogic->getFrame(), getEndFrame(), winner < 0 ? "none" : winner == 0 ? "A" : "B", m_downFrame[ 0 ], m_downFrame[ 1 ],
							m_teamLeftFrame[ 0 ], m_teamLeftFrame[ 1 ], m_teamMiddleFrames[ 0 ], m_teamMiddleFrames[ 1 ], (Int)m_samples.size() ));
	for( Int t = 0; t < DIRECTSTRIKE_TEAMS; ++t )
		for( Int s = 0; s < DIRECTSTRIKE_SEATS; ++s )
		{
			if( TheDirectStrikeDirector->getSeatPlayer( t, s ) < 0 )
				continue;
			const DirectStrikeSeatStats &st = m_seats[ t ][ s ];
			Int mvpKills = 0;
			const ThingTemplate *mvp = st.mvp( &mvpKills );
			DEBUG_LOG(( "HEADLESS DIRECTSTRIKE STATS: seat %c%d player %d: %d waves, %d copies sent, %d placed, earned %d (income %d, middle %d, refunds %d), "
									"spent %d (units %d, tiers %d, upgrades %d, ranks %d, passengers %d), %d kills, damage towers %.0f fortress %.0f, "
									"middle %u frames, mvp '%s' %d kills\n",
									TEAM_LETTER[ t ], s + 1, TheDirectStrikeDirector->getSeatPlayer( t, s ), st.waves, st.unitsSent, st.placed,
									st.totalEarned(), st.earned[ DIRECTSTRIKE_EARN_INCOME ], st.earned[ DIRECTSTRIKE_EARN_MIDDLE ], st.earned[ DIRECTSTRIKE_EARN_REFUNDS ],
									st.totalSpent(), st.spent[ DIRECTSTRIKE_SPEND_UNITS ], st.spent[ DIRECTSTRIKE_SPEND_TIERS ], st.spent[ DIRECTSTRIKE_SPEND_UPGRADES ],
									st.spent[ DIRECTSTRIKE_SPEND_RANKS ], st.spent[ DIRECTSTRIKE_SPEND_PASSENGERS ], st.kills, st.towerDamage, st.fortressDamage,
									st.middleFrames, mvp ? mvp->getName().str() : "-", mvpKills ));
			for( size_t i = 0; i < st.placedBy.size(); ++i )
				DEBUG_LOG(( "HEADLESS DIRECTSTRIKE STATS: seat %c%d placed '%s' %d\n", TEAM_LETTER[ t ], s + 1,
										st.placedBy[ i ].thing ? st.placedBy[ i ].thing->getName().str() : "?", st.placedBy[ i ].count ));
			for( size_t i = 0; i < st.killsBy.size(); ++i )
				DEBUG_LOG(( "HEADLESS DIRECTSTRIKE STATS: seat %c%d kills by '%s' %d\n", TEAM_LETTER[ t ], s + 1,
										st.killsBy[ i ].thing ? st.killsBy[ i ].thing->getName().str() : "?", st.killsBy[ i ].count ));
		}
	DEBUG_LOG(( "HEADLESS DIRECTSTRIKE STATS: towers down A %u %u B %u %u\n", m_towerDownFrame[ 0 ][ 0 ], m_towerDownFrame[ 0 ][ 1 ],
							m_towerDownFrame[ 1 ][ 0 ], m_towerDownFrame[ 1 ][ 1 ] ));
	// the timeline in steps of half a minute, and its last sample
	for( size_t i = 0; i < m_samples.size(); ++i )
	{
		if( i % 30 != 0 && i + 1 != m_samples.size() )
			continue;
		const DirectStrikeSample &p = m_samples[ i ];
		DEBUG_LOG(( "HEADLESS DIRECTSTRIKE SAMPLE: frame %u paid %s middle %d fortress A %d B %d towers A %d %d B %d %d\n", p.frame,
								p.middleHolder < 0 ? "-" : p.middleHolder == 0 ? "A" : "B", p.middle, p.fortress[ 0 ], p.fortress[ 1 ],
								p.tower[ 0 ][ 0 ], p.tower[ 0 ][ 1 ], p.tower[ 1 ][ 0 ], p.tower[ 1 ][ 1 ] ));
	}
}

//-------------------------------------------------------------------------------------------------
static void xferCounts( Xfer *xfer, std::vector<DirectStrikeTemplateCount> &list )
{
	UnsignedInt count = (UnsignedInt)list.size();
	xfer->xferUnsignedInt( &count );
	if( xfer->getXferMode() == XFER_LOAD )
		list.resize( count );
	for( UnsignedInt i = 0; i < count; ++i )
	{
		AsciiString name = xfer->getXferMode() != XFER_LOAD && list[ i ].thing != NULL ? list[ i ].thing->getName() : AsciiString::TheEmptyString;
		xfer->xferAsciiString( &name );
		if( xfer->getXferMode() == XFER_LOAD )
			list[ i ].thing = TheThingFactory->findTemplate( name, FALSE );
		xfer->xferInt( &list[ i ].count );
	}
}

void DirectStrikeStats::crc( Xfer *xfer )
{
	DirectStrikeStats::xfer( xfer );
}

/** Version 1: every seat's record, the team middle counts, the frames the fortresses and towers fell,
	  the timeline and whether the record was logged.
	  Version 2: the frames each seat and each team left the game. */
void DirectStrikeStats::xfer( Xfer *xfer )
{
	XferVersion currentVersion = 2;
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );

	for( Int t = 0; t < DIRECTSTRIKE_TEAMS; ++t )
	{
		for( Int s = 0; s < DIRECTSTRIKE_SEATS; ++s )
		{
			DirectStrikeSeatStats &st = m_seats[ t ][ s ];
			xfer->xferInt( &st.waves );
			xfer->xferInt( &st.unitsSent );
			xfer->xferInt( &st.placed );
			xferCounts( xfer, st.placedBy );
			for( Int k = 0; k < DIRECTSTRIKE_EARN_KINDS; ++k )
				xfer->xferInt( &st.earned[ k ] );
			for( Int k = 0; k < DIRECTSTRIKE_SPEND_KINDS; ++k )
				xfer->xferInt( &st.spent[ k ] );
			xfer->xferInt( &st.kills );
			xferCounts( xfer, st.killsBy );
			xfer->xferReal( &st.towerDamage );
			xfer->xferReal( &st.fortressDamage );
			xfer->xferUnsignedInt( &st.middleFrames );
		}
		xfer->xferUnsignedInt( &m_teamMiddleFrames[ t ] );
		xfer->xferUnsignedInt( &m_downFrame[ t ] );
		xfer->xferUnsignedInt( &m_towerDownFrame[ t ][ 0 ] );
		xfer->xferUnsignedInt( &m_towerDownFrame[ t ][ 1 ] );
	}

	UnsignedInt count = (UnsignedInt)m_samples.size();
	xfer->xferUnsignedInt( &count );
	if( xfer->getXferMode() == XFER_LOAD )
		m_samples.resize( count );
	for( UnsignedInt i = 0; i < count; ++i )
	{
		DirectStrikeSample &p = m_samples[ i ];
		xfer->xferUnsignedInt( &p.frame );
		xfer->xferInt( &p.middleHolder );
		xfer->xferInt( &p.middle );
		for( Int t = 0; t < DIRECTSTRIKE_TEAMS; ++t )
		{
			xfer->xferInt( &p.fortress[ t ] );
			xfer->xferInt( &p.tower[ t ][ 0 ] );
			xfer->xferInt( &p.tower[ t ][ 1 ] );
		}
	}
	xfer->xferBool( &m_logged );

	if( version >= 2 )
		for( Int t = 0; t < DIRECTSTRIKE_TEAMS; ++t )
		{
			for( Int s = 0; s < DIRECTSTRIKE_SEATS; ++s )
				xfer->xferUnsignedInt( &m_seatLeftFrame[ t ][ s ] );
			xfer->xferUnsignedInt( &m_teamLeftFrame[ t ] );
		}
}
