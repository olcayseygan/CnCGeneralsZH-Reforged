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

// FILE: ApocalypseDirector.cpp ///////////////////////////////////////////////////////////////////
//
// The Apocalypse mode; ApocalypseDirector.h says what it does.  Everything here is logic state: time
// is the logic frame in game seconds, every draw comes from the logic stream, the zombies are kept
// in the order they came out, and nothing reads the client.  The light at the bottom is the client's:
// the logic hands it a night amount after a tick and never looks at what it did with it.
//
///////////////////////////////////////////////////////////////////////////////////////////////////

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "Common/GlobalData.h"
#include "Common/LogicThread.h"
#include "Common/NameKeyGenerator.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/Team.h"
#include "Common/ThingFactory.h"
#include "Common/ThingTemplate.h"
#include "Common/Xfer.h"
#include "GameClient/Display.h"
#include "GameClient/Drawable.h"
#include "GameClient/GameClient.h"
#include "GameClient/InGameUI.h"
#include "GameLogic/AI.h"
#include "GameLogic/AIPathfind.h"
#include "GameLogic/ApocalypseDirector.h"
#include "GameLogic/Damage.h"
#include "GameLogic/Module/BodyModule.h"
#include "GameLogic/ExperienceTracker.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/LogicRandomValue.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/CreateModule.h"
#include "GameLogic/Module/SpawnBehavior.h"
#include "GameLogic/Object.h"
#include "GameLogic/PartitionManager.h"
#include "GameLogic/PolygonTrigger.h"
#include "GameClient/ParticleSys.h"
#include <algorithm>
#include <functional>
#include "GameLogic/TerrainLogic.h"
#include "GameLogic/Weapon.h"

//=================================================================================================
// The flow field
//=================================================================================================

// examineNeighboringCells' neighbour order and its corner rule: a diagonal needs one of its two sides open
static const Int FIELD_DX[ 8 ] = { 1, 0, -1, 0, 1, -1, -1, 1 };
static const Int FIELD_DY[ 8 ] = { 0, 1, 0, -1, 1, 1, -1, -1 };
static const Int FIELD_OPPOSITE[ 8 ] = { 2, 3, 0, 1, 6, 7, 4, 5 };
static const Int FIELD_ORTHOGONAL = 10;		// the search's COST_ORTHOGONAL and COST_DIAGONAL (AIPathfind.cpp)
static const Int FIELD_DIAGONAL = 14;

//-------------------------------------------------------------------------------------------------
ApocalypseFlowField::ApocalypseFlowField( void ) :
	m_width( 0 ), m_height( 0 ), m_phase( PHASE_IDLE ), m_cursor( 0 ), m_all( FALSE )
{
}

//-------------------------------------------------------------------------------------------------
void ApocalypseFlowField::clear( void )
{
	std::vector<UnsignedByte>().swap( m_ground );
	std::vector<ObjectID>().swap( m_source );
	std::vector<Int>().swap( m_dist );
	std::vector<UnsignedByte>().swap( m_step );
	std::vector<ObjectID>().swap( m_label );
	std::vector<Change>().swap( m_changes );
	std::vector<UnsignedByte>().swap( m_mark );
	std::vector<UnsignedByte>().swap( m_queued );
	std::vector<Int>().swap( m_region );
	std::vector<Int>().swap( m_cheaper );
	std::vector<Int>().swap( m_chain );
	std::vector<Open>().swap( m_open );
	m_width = m_height = 0;
	m_phase = PHASE_IDLE;
	m_cursor = 0;
	m_all = FALSE;
}

//-------------------------------------------------------------------------------------------------
void ApocalypseFlowField::setup( Int width, Int height )
{
	clear();
	if( width <= 0 || height <= 0 )
		return;
	m_width = width;
	m_height = height;
	const Int cells = m_width * m_height;
	m_ground.assign( cells, 0 );
	m_source.assign( cells, INVALID_ID );
	m_dist.assign( cells, UNREACHED );
	m_step.assign( cells, NO_STEP );
	m_label.assign( cells, INVALID_ID );
	m_mark.assign( cells, MARK_UNKNOWN );
	m_queued.assign( cells, 0 );
}

//-------------------------------------------------------------------------------------------------
Bool ApocalypseFlowField::walkable( Int x, Int y ) const
{
	return x >= 0 && y >= 0 && x < m_width && y < m_height && ( m_ground[ x * m_height + y ] & WALKABLE ) != 0;
}

//-------------------------------------------------------------------------------------------------
/// examineNeighboringCells' test, which reads the same from either end: a diagonal needs one of the two cells beside both open
Bool ApocalypseFlowField::stepAllowed( Int x, Int y, Int direction ) const
{
	if( !walkable( x + FIELD_DX[ direction ], y + FIELD_DY[ direction ] ) )
		return FALSE;
	if( direction < 4 )
		return TRUE;
	return walkable( x + FIELD_DX[ direction ], y ) || walkable( x, y + FIELD_DY[ direction ] );
}

//-------------------------------------------------------------------------------------------------
/// what stepping onto that cell costs: the step, and the pinched charge of costSoFar and of the expansion
Int ApocalypseFlowField::enterCost( Int index, Int direction ) const
{
	return ( direction >= 4 ? FIELD_DIAGONAL : FIELD_ORTHOGONAL ) + ( ( m_ground[ index ] & PINCHED ) ? FIELD_DIAGONAL + FIELD_ORTHOGONAL : 0 );
}

//-------------------------------------------------------------------------------------------------
void ApocalypseFlowField::clearCell( Int index )
{
	m_dist[ index ] = UNREACHED;
	m_step[ index ] = NO_STEP;
	m_label[ index ] = INVALID_ID;
}

//-------------------------------------------------------------------------------------------------
void ApocalypseFlowField::push( Int dist, Int index )
{
	Open open;
	open.dist = dist;
	open.index = index;
	m_open.push_back( open );
	std::push_heap( m_open.begin(), m_open.end(), std::greater<Open>() );
	m_queued[ index ] = 1;
}

//-------------------------------------------------------------------------------------------------
/** A cell whose distance is final takes its step and structure from its neighbours, which are final too
	  wherever they are nearer, then offers itself to them: a neighbour cheaper through it, or as cheap
	  through it from a direction earlier in the order, or stepping onto it under another structure's
	  name, is queued to settle again.  Settling in this order is what makes the field the same however
	  it was reached, a build or any run of repairs. */
void ApocalypseFlowField::settle( Int index )
{
	const Int x = cellX( index ), y = cellY( index );
	const Int dist = m_dist[ index ];
	if( m_source[ index ] != INVALID_ID )
	{
		m_step[ index ] = NO_STEP;
		m_label[ index ] = m_source[ index ];
	}
	else
	{
		for( Int i = 0; i < 8; ++i )
		{
			if( !stepAllowed( x, y, i ) )
				continue;
			const Int next = ( x + FIELD_DX[ i ] ) * m_height + y + FIELD_DY[ i ];
			if( m_dist[ next ] != UNREACHED && m_dist[ next ] + enterCost( next, i ) == dist )
			{
				m_step[ index ] = (UnsignedByte)i;
				m_label[ index ] = m_label[ next ];
				break;
			}
		}
	}
	for( Int i = 0; i < 8; ++i )
	{
		if( !stepAllowed( x, y, i ) )
			continue;
		const Int next = ( x + FIELD_DX[ i ] ) * m_height + y + FIELD_DY[ i ];
		if( m_source[ next ] != INVALID_ID )
			continue;		// a structure's side, nought by its own right
		const Int through = dist + enterCost( index, i );
		const Int back = FIELD_OPPOSITE[ i ];
		if( through < m_dist[ next ] )
		{
			m_dist[ next ] = through;
			push( through, next );
		}
		else if( through == m_dist[ next ] && ( back < m_step[ next ] || ( back == m_step[ next ] && m_label[ next ] != m_label[ index ] ) ) )
			push( through, next );
	}
}

//-------------------------------------------------------------------------------------------------
/// whether this cell's route runs over a cell marked in, marking every cell on the way the same
void ApocalypseFlowField::markChain( Int index )
{
	if( m_mark[ index ] != MARK_UNKNOWN )
		return;
	m_chain.clear();
	Int cell = index;
	UnsignedByte answer = MARK_OUT;
	for( ;; )
	{
		if( m_mark[ cell ] != MARK_UNKNOWN )
		{
			answer = m_mark[ cell ];
			break;
		}
		m_chain.push_back( cell );
		const Int s = m_step[ cell ];
		if( m_dist[ cell ] == UNREACHED || m_dist[ cell ] == 0 || s == NO_STEP )
			break;
		const Int nx = cellX( cell ) + FIELD_DX[ s ], ny = cellY( cell ) + FIELD_DY[ s ];
		if( nx < 0 || ny < 0 || nx >= m_width || ny >= m_height )
			break;
		const Int next = nx * m_height + ny;
		if( m_dist[ next ] >= m_dist[ cell ] )
			break;
		cell = next;
	}
	for( size_t k = 0; k < m_chain.size(); ++k )
	{
		m_mark[ m_chain[ k ] ] = answer;
		if( answer == MARK_IN )
			m_region.push_back( m_chain[ k ] );
	}
}

//-------------------------------------------------------------------------------------------------
void ApocalypseFlowField::beginBuild( void )
{
	if( !isStarted() )
		return;
	std::fill( m_dist.begin(), m_dist.end(), (Int)UNREACHED );
	std::fill( m_step.begin(), m_step.end(), (UnsignedByte)NO_STEP );
	std::fill( m_label.begin(), m_label.end(), (ObjectID)INVALID_ID );
	m_changes.clear();
	m_region.clear();
	m_cheaper.clear();
	m_open.clear();
	m_all = TRUE;
	m_phase = PHASE_SEED;
	m_cursor = 0;
}

//-------------------------------------------------------------------------------------------------
void ApocalypseFlowField::beginRepair( const std::vector<Change> &changes )
{
	if( !isStarted() || isRepairing() )
		return;
	m_all = FALSE;
	m_changes = changes;
	m_region.clear();
	m_cheaper.clear();
	m_open.clear();
	std::fill( m_mark.begin(), m_mark.end(), (UnsignedByte)MARK_UNKNOWN );
	for( size_t k = 0; k < changes.size(); ++k )
	{
		const Change &change = changes[ k ];
		m_ground[ change.cell ] = change.newGround;
		m_source[ change.cell ] = change.newSource;
		const Bool wasWalkable = ( change.oldGround & WALKABLE ) != 0;
		const Bool isWalkable = ( change.newGround & WALKABLE ) != 0;
		// closed, dearer to step onto, or no longer the side of the structure it was: every route over it goes;
		// freed ground has none yet and is filled the same way
		const Bool dearer = wasWalkable != isWalkable ||
			( isWalkable && !( change.oldGround & PINCHED ) && ( change.newGround & PINCHED ) ) ||
			( change.oldSource != INVALID_ID && change.oldSource != change.newSource );
		if( dearer )
		{
			if( m_mark[ change.cell ] != MARK_IN )
			{
				m_mark[ change.cell ] = MARK_IN;
				m_region.push_back( change.cell );
			}
		}
		else
			m_cheaper.push_back( change.cell );		// cheaper to step onto, or a structure's side now
		if( wasWalkable && !isWalkable )
		{
			// a closed cell can be the last open corner of a diagonal step beside it: the cells either side
			// of it that step diagonally lose that step, and their routes go with it
			const Int x = cellX( change.cell ), y = cellY( change.cell );
			for( Int i = 0; i < 4; ++i )
			{
				const Int nx = x + FIELD_DX[ i ], ny = y + FIELD_DY[ i ];
				if( nx < 0 || ny < 0 || nx >= m_width || ny >= m_height )
					continue;
				const Int beside = nx * m_height + ny;
				if( m_step[ beside ] >= 4 && m_step[ beside ] != NO_STEP && m_mark[ beside ] != MARK_IN )
				{
					m_mark[ beside ] = MARK_IN;
					m_region.push_back( beside );
				}
			}
		}
	}
	m_cursor = 0;
	m_phase = PHASE_MARK;
}

//-------------------------------------------------------------------------------------------------
Int ApocalypseFlowField::stepRepair( Int budget )
{
	Int spent = 0;
	const Int cells = m_width * m_height;
	while( spent < budget && m_phase != PHASE_IDLE )
	{
		if( m_phase == PHASE_MARK )
		{
			// every cell whose route runs over the dearer ground joins it; a plain walk, so many a unit
			const Int end = min( cells, m_cursor + ( budget - spent ) * SCAN_PER_CELL );
			const Int from = m_cursor;
			for( ; m_cursor < end; ++m_cursor )
				markChain( m_cursor );
			spent += ( m_cursor - from + SCAN_PER_CELL - 1 ) / SCAN_PER_CELL;
			if( m_cursor >= cells )
			{
				// only now, once every route has been read: the marked cells lose theirs
				for( size_t k = 0; k < m_region.size(); ++k )
					clearCell( m_region[ k ] );
				m_phase = PHASE_SEED;
				m_cursor = 0;
			}
		}
		else if( m_phase == PHASE_SEED )
		{
			// the sources, and what floods back in: round the cleared cells their settled rim, and the
			// changed cells that only got cheaper
			const Int cleared = m_all ? cells : (Int)m_region.size();
			const Int count = m_all ? cells : cleared + (Int)m_cheaper.size();
			for( ; m_cursor < count && spent < budget; ++m_cursor )
			{
				if( m_all )
				{
					if( ( m_cursor & ( SCAN_PER_CELL - 1 ) ) == 0 )
						++spent;
					if( m_source[ m_cursor ] != INVALID_ID && ( m_ground[ m_cursor ] & WALKABLE ) )
					{
						m_dist[ m_cursor ] = 0;
						push( 0, m_cursor );
					}
					continue;
				}
				++spent;
				const Bool isCleared = m_cursor < cleared;
				const Int index = isCleared ? m_region[ m_cursor ] : m_cheaper[ m_cursor - cleared ];
				if( m_source[ index ] != INVALID_ID && ( m_ground[ index ] & WALKABLE ) )
				{
					m_dist[ index ] = 0;
					push( 0, index );
				}
				else if( !isCleared && m_dist[ index ] != UNREACHED )
					push( m_dist[ index ], index );
				if( !isCleared )
					continue;
				const Int x = cellX( index ), y = cellY( index );
				for( Int i = 0; i < 8; ++i )
				{
					const Int nx = x + FIELD_DX[ i ], ny = y + FIELD_DY[ i ];
					if( nx < 0 || ny < 0 || nx >= m_width || ny >= m_height )
						continue;
					const Int rim = nx * m_height + ny;
					if( m_mark[ rim ] != MARK_IN && m_dist[ rim ] != UNREACHED )
						push( m_dist[ rim ], rim );
				}
			}
			if( m_cursor >= count )
				m_phase = PHASE_FLOOD;
		}
		else
		{
			while( !m_open.empty() && spent < budget )
			{
				std::pop_heap( m_open.begin(), m_open.end(), std::greater<Open>() );
				const Open open = m_open.back();
				m_open.pop_back();
				++spent;
				if( open.dist != m_dist[ open.index ] || !m_queued[ open.index ] )
					continue;		// a cheaper way in reached it since, or it settled at this distance already
				m_queued[ open.index ] = 0;
				settle( open.index );
			}
			if( m_open.empty() )
			{
				m_phase = PHASE_IDLE;
				m_all = FALSE;
				m_region.clear();
				m_cheaper.clear();
			}
		}
	}
	return spent;
}

//-------------------------------------------------------------------------------------------------
Bool ApocalypseFlowField::route( Int x, Int y, std::vector<Int> *cells ) const
{
	cells->clear();
	if( !isStarted() || x < 0 || y < 0 || x >= m_width || y >= m_height )
		return FALSE;
	Int index = x * m_height + y;
	// distances fall strictly along a route, so this ends; the bound is only a guard
	for( Int steps = 0; steps < m_width * m_height; ++steps )
	{
		if( m_dist[ index ] == UNREACHED || !walkable( cellX( index ), cellY( index ) ) )
			return FALSE;
		cells->push_back( index );
		if( m_dist[ index ] == 0 )
			return TRUE;
		const Int s = m_step[ index ];
		if( s == NO_STEP )
			return FALSE;
		const Int nx = cellX( index ) + FIELD_DX[ s ], ny = cellY( index ) + FIELD_DY[ s ];
		if( nx < 0 || ny < 0 || nx >= m_width || ny >= m_height )
			return FALSE;
		const Int next = nx * m_height + ny;
		if( m_dist[ next ] >= m_dist[ index ] )
			return FALSE;
		index = next;
	}
	return FALSE;
}

//=================================================================================================
// The director
//=================================================================================================

static ApocalypseDirector theApocalypseDirector;
ApocalypseDirector *TheApocalypseDirector = &theApocalypseDirector;

/** The roster, the walker first (docs/plans/APOCALYPSE-ZOMBIES.md).  A roll draws over the weights of
	  the kinds the phase and the night allow and the wave can still pay for, the walker taking the rest.
	  A weight is how often a roll lands on the kind, not how much of the points it takes: with the night's
	  walker 40, runner 30 and brute 30 the brutes spent 180 of every 280 points.  A new kind's day and dawn weight is half its night
	  weight; the walker, runner and brute keep the shares they had before the table. */
static const ApocalypseKind theKinds[] =
{
	//	template											points										night		day	dusk	night	dawn		group	flags
	{ "ApocalypseZombie",						APOCALYPSE_POINTS_WALKER,		0,		{	95,	65,		40,		85 },		1,		0 },
	{ "ApocalypseZombieRunner",			APOCALYPSE_POINTS_RUNNER,		1,		{	0,	20,		30,		10 },		1,		0 },
	{ "ApocalypseZombieBrute",			APOCALYPSE_POINTS_BRUTE,		1,		{	5,	15,		30,		5 },		1,		0 },
	// batch 1: air and walls
	{ "ApocalypseZombieSpitter",		4,													1,		{	5,	6,		10,		5 },		1,		0 },
	{ "ApocalypseZombieBloater",		5,													1,		{	4,	5,		8,		4 },		1,		0 },
	{ "ApocalypseZombieCarrionBat",	1,													1,		{	2,	3,		4,		2 },		6,		APOCALYPSE_KIND_FLYER },
	{ "ApocalypseZombieScreecher",	6,													2,		{	2,	2,		4,		2 },		1,		APOCALYPSE_KIND_FLYER },
	// batch 2: the tank front
	{ "ApocalypseZombieJuggernaut",		14,												2,		{	2,	2,		4,		2 },		1,		0 },
	{ "ApocalypseZombieShieldBearer",	10,												3,		{	2,	2,		4,		2 },		1,		0 },
	{ "ApocalypseZombieBoneMortar",		10,												4,		{	2,	2,		4,		2 },		1,		0 },
	{ "ApocalypseZombieSiegeRam",			16,												4,		{	0,	1,		2,		0 },		1,		APOCALYPSE_KIND_ELITE },
	{ "ApocalypseZombieSpineLancer",	8,												4,		{	3,	3,		6,		3 },		1,		0 },
	// batch 3: passes, garrisons and the weak front
	{ "ApocalypseZombieCrawler",			2,												1,		{	3,	4,		6,		3 },		1,		0 },
	{ "ApocalypseZombieHound",				3,												2,		{	2,	2,		4,		2 },		4,		APOCALYPSE_KIND_WEAKFRONT },
	{ "ApocalypseZombieGasbag",				6,												2,		{	2,	2,		4,		2 },		1,		0 },
	{ "ApocalypseZombieBileLobber",		6,												2,		{	2,	2,		4,		2 },		1,		0 },
	{ "ApocalypseZombieBurner",				4,												3,		{	2,	3,		5,		2 },		1,		0 },
	{ "ApocalypseZombieGasDiver",			5,												3,		{	2,	2,		4,		2 },		1,		APOCALYPSE_KIND_FLYER },
	// batch 4: the hidden and the helpers
	{ "ApocalypseZombieHowler",				8,												2,		{	2,	2,		4,		2 },		1,		0 },
	{ "ApocalypseZombieStalker",			5,												3,		{	3,	3,		6,		3 },		1,		0 },
	{ "ApocalypseZombieBurrower",			9,												4,		{	2,	2,		4,		2 },		1,		0 },
	{ "ApocalypseZombieRotmother",		10,												4,		{	1,	1,		2,		1 },		1,		0 },
	{ "ApocalypseZombieShocker",			8,												5,		{	2,	2,		4,		2 },		1,		0 },
	{ "ApocalypseZombieLeech",				7,												5,		{	2,	2,		4,		2 },		1,		0 },
	// batch 5: the hosts; the Bloat Sack's 12 pay for the ten walkers inside it
	{ "ApocalypseZombieBloatSack",		12,												4,		{	2,	2,		4,		2 },		1,		0 },
	{ "ApocalypseZombieAbomination",	30,												5,		{	0,	1,		2,		0 },		1,		APOCALYPSE_KIND_ELITE },
	{ "ApocalypseZombieBroodmother",	18,												5,		{	0,	1,		2,		0 },		1,		APOCALYPSE_KIND_ELITE },
	{ "ApocalypseZombieSporeDrifter",	4,												5,		{	2,	2,		4,		2 },		1,		APOCALYPSE_KIND_FLYER },
	{ "ApocalypseZombieBroodCarrier",	25,												6,		{	0,	1,		2,		0 },		1,		APOCALYPSE_KIND_ELITE | APOCALYPSE_KIND_FLYER },
	// a host's brood, never rolled (no weight): here so a kill of one is a zombie's kill and pays a
	// walker's points.  Slaves, so the stray walk passes them by; their host moves them.
	{ "ApocalypseZombieBroodling",		1,												5,		{	0,	0,		0,		0 },		1,		0 },
	{ "ApocalypseZombieBroodBat",			1,												6,		{	0,	0,		0,		0 },		1,		APOCALYPSE_KIND_FLYER },
	// batch 6: the bosses, last.  No roll draws them (weight 0 everywhere): a Blood Moon night's
	// peak wave sends its own (sendBosses), outside the wave's points
	{ "ApocalypseZombieColossus",			80,												3,		{	0,	0,		0,		0 },		1,		APOCALYPSE_KIND_BOSS },
	{ "ApocalypseZombieWyrm",					100,											6,		{	0,	0,		0,		0 },		1,		APOCALYPSE_KIND_BOSS | APOCALYPSE_KIND_FLYER },
	{ "ApocalypseZombiePlagueKing",		120,											9,		{	0,	0,		0,		0 },		1,		APOCALYPSE_KIND_BOSS },
};

/// the roster row of that template name, -1 for none
static Int kindNamed( const char *name )
{
	for( Int k = 0; k < (Int)ARRAY_SIZE( theKinds ); ++k )
		if( strcmp( theKinds[ k ].name, name ) == 0 )
			return k;
	return -1;
}

Int Apocalypse_kindCount( void )
{
	return (Int)ARRAY_SIZE( theKinds );
}

const ApocalypseKind &Apocalypse_kind( Int kind )
{
	return theKinds[ kind >= 0 && kind < Apocalypse_kindCount() ? kind : APOCALYPSE_KIND_WALKER ];
}

Bool Apocalypse_kindAllowed( Int kind, Int phase, UnsignedInt cycle )
{
	const ApocalypseKind &k = Apocalypse_kind( kind );
	const Bool dark = phase == APOCALYPSE_DUSK || phase == APOCALYPSE_NIGHT;
	if( !dark && ( k.flags & APOCALYPSE_KIND_ELITE ) )
		return FALSE;
	return (Int)( dark ? cycle + 1 : cycle ) >= k.firstNight;
}

Int Apocalypse_flyerSharePercent( Int phase, UnsignedInt cycle )
{
	const Bool dark = phase == APOCALYPSE_DUSK || phase == APOCALYPSE_NIGHT;
	return (Int)( dark ? cycle + 1 : cycle ) >= APOCALYPSE_FLYER_LATE_NIGHT ? APOCALYPSE_FLYER_SHARE_LATE : APOCALYPSE_FLYER_SHARE_EARLY;
}

Bool Apocalypse_isBloodMoon( Int phase, UnsignedInt cycle )
{
	return ( phase == APOCALYPSE_DUSK || phase == APOCALYPSE_NIGHT ) && ( cycle + 1 ) % APOCALYPSE_BLOOD_MOON_EVERY == 0;
}

Int Apocalypse_bossesOfNight( UnsignedInt night, Int skip, Int kinds[ 2 ] )
{
	if( night == 0 || night % APOCALYPSE_BLOOD_MOON_EVERY != 0 )
		return 0;
	const Int bosses[ 3 ] = { kindNamed( "ApocalypseZombieColossus" ), kindNamed( "ApocalypseZombieWyrm" ), kindNamed( "ApocalypseZombiePlagueKing" ) };
	if( night < APOCALYPSE_BOSS_PAIR_NIGHT )
	{
		kinds[ 0 ] = bosses[ night / APOCALYPSE_BLOOD_MOON_EVERY - 1 ];
		return kinds[ 0 ] >= 0 ? 1 : 0;
	}
	Int count = 0;
	for( Int b = 0; b < 3; ++b )
		if( b != skip && bosses[ b ] >= 0 && count < 2 )
			kinds[ count++ ] = bosses[ b ];
	return count;
}

Int Apocalypse_weakestGate( const Int *defence, UnsignedInt fronts, Int gates )
{
	Int best = -1;
	for( Int g = 0; g < gates && g < APOCALYPSE_SPAWNS_MOST; ++g )
		if( ( fronts & ( 1u << g ) ) && ( best < 0 || defence[ g ] < defence[ best ] ) )
			best = g;
	return best;
}

static const ApocalypsePhaseRules thePhaseRules[ APOCALYPSE_PHASES ] =
{
	//	seconds							wave every	points %	out a frame	walking at most	gates
	{ APOCALYPSE_DAY_SECONDS,		40,			100,			5,					450,						1, 2 },
	{ APOCALYPSE_DUSK_SECONDS,	30,			220,			6,					550,						2, 2 },
	{ APOCALYPSE_NIGHT_SECONDS,	25,			450,			10,					700,						2, 99 },
	{ APOCALYPSE_DAWN_SECONDS,	30,			200,			6,					550,						1, 2 },
};

const ApocalypsePhaseRules &Apocalypse_phaseRules( Int phase )
{
	return thePhaseRules[ phase >= 0 && phase < APOCALYPSE_PHASES ? phase : APOCALYPSE_DAY ];
}

const char *Apocalypse_phaseName( Int phase )
{
	static const char *const names[ APOCALYPSE_PHASES ] = { "day", "dusk", "night", "dawn" };
	return names[ phase >= 0 && phase < APOCALYPSE_PHASES ? phase : APOCALYPSE_DAY ];
}

Int Apocalypse_wavePoints( UnsignedInt wave, Int seats, Int phase, UnsignedInt cycle, Int adaptPercent )
{
	const Int plain = APOCALYPSE_WAVE_BASE + APOCALYPSE_WAVE_PER_SEAT * seats + APOCALYPSE_WAVE_GROWTH * (Int)( wave > 0 ? wave - 1 : 0 );
	Int points = plain * Apocalypse_phaseRules( phase ).sizePercent / 100 * ( 100 + APOCALYPSE_CYCLE_GROWTH_PERCENT * (Int)cycle ) / 100
		* adaptPercent / 100;
	if( Apocalypse_isBloodMoon( phase, cycle ) )
		points = points * APOCALYPSE_BLOOD_MOON_PERCENT / 100;
	return points < APOCALYPSE_WAVE_MOST ? points : APOCALYPSE_WAVE_MOST;
}

Int Apocalypse_adapt( Int adaptPercent, Bool reachedBases, Bool cleared )
{
	Int next = adaptPercent;
	if( reachedBases )
		next -= APOCALYPSE_ADAPT_DOWN;
	else
		next += cleared ? APOCALYPSE_ADAPT_UP : APOCALYPSE_ADAPT_SLOW;
	return next < APOCALYPSE_ADAPT_LEAST ? APOCALYPSE_ADAPT_LEAST : next > APOCALYPSE_ADAPT_MOST ? APOCALYPSE_ADAPT_MOST : next;
}

//-------------------------------------------------------------------------------------------------
// A wave's shape and theme, and the scatter of its spots in the fog
//-------------------------------------------------------------------------------------------------

const char *Apocalypse_themeName( Int theme )
{
	static const char *const names[ APOCALYPSE_THEMES ] = { "mixed", "horde", "hunt", "siege", "plague", "sky" };
	return names[ theme >= 0 && theme < APOCALYPSE_THEMES ? theme : APOCALYPSE_THEME_MIXED ];
}

UnsignedInt Apocalypse_themesOfKind( const char *name )
{
	enum { HORDE = 1u << APOCALYPSE_THEME_HORDE, HUNT = 1u << APOCALYPSE_THEME_HUNT, SIEGE = 1u << APOCALYPSE_THEME_SIEGE,
				 PLAGUE = 1u << APOCALYPSE_THEME_PLAGUE, SKY = 1u << APOCALYPSE_THEME_SKY };
	/* By the roles of docs/plans/APOCALYPSE-ZOMBIES.md.  Keyed on the template, not the roster row, so a
		 batch's rows can land in any order; a kind not named here leans on no theme.  The bosses and the
		 brood are never rolled and need none. */
	static const struct { const char *name; UnsignedInt themes; } roles[] =
	{
		{ "ApocalypseZombie",							HORDE },
		{ "ApocalypseZombieRunner",				HORDE | HUNT },
		{ "ApocalypseZombieBrute",				SIEGE },
		{ "ApocalypseZombieCrawler",			HORDE | HUNT },
		{ "ApocalypseZombieHound",				HUNT },
		{ "ApocalypseZombieJuggernaut",		SIEGE },
		{ "ApocalypseZombieShieldBearer",	SIEGE },
		{ "ApocalypseZombieSiegeRam",			SIEGE },
		{ "ApocalypseZombieAbomination",	SIEGE },
		{ "ApocalypseZombieBloater",			HORDE | SIEGE },
		{ "ApocalypseZombieGasbag",				HORDE | PLAGUE },
		{ "ApocalypseZombieBurner",				PLAGUE },
		{ "ApocalypseZombieShocker",			PLAGUE },
		{ "ApocalypseZombieSpitter",			PLAGUE },
		{ "ApocalypseZombieBileLobber",		PLAGUE },
		{ "ApocalypseZombieBoneMortar",		SIEGE | PLAGUE },
		{ "ApocalypseZombieSpineLancer",	PLAGUE },
		{ "ApocalypseZombieStalker",			HUNT },
		{ "ApocalypseZombieBurrower",			HUNT | SIEGE },
		{ "ApocalypseZombieLeech",				HUNT },
		{ "ApocalypseZombieHowler",				HUNT },
		{ "ApocalypseZombieRotmother",		SIEGE },
		{ "ApocalypseZombieBroodmother",	HORDE | PLAGUE },
		{ "ApocalypseZombieBloatSack",		HORDE | SIEGE },
		{ "ApocalypseZombieCarrionBat",		SKY },
		{ "ApocalypseZombieScreecher",		SKY },
		{ "ApocalypseZombieGasDiver",			SKY },
		{ "ApocalypseZombieSporeDrifter",	SKY | PLAGUE },
		{ "ApocalypseZombieBroodCarrier",	SKY },
	};
	for( Int r = 0; r < (Int)ARRAY_SIZE( roles ); ++r )
		if( strcmp( roles[ r ].name, name ) == 0 )
			return roles[ r ].themes;
	return 0;
}

Int Apocalypse_waveShapePercent( Int phase, UnsignedInt intoPhase, UnsignedInt phaseFrames, Bool bloodMoon )
{
	// (how far in, of 1000; percent) at the start, the turn and the end of each phase, straight lines between
	static const Int shapes[ APOCALYPSE_PHASES + 1 ][ 3 ][ 2 ] =
	{
		{ { 0, 80 },	{ 500, 100 },	{ 1000, 120 } },	// day: building towards dusk
		{ { 0, 100 },	{ 500, 118 },	{ 1000, 135 } },	// dusk: thickening
		{ { 0, 70 },	{ 650, 165 },	{ 1000, 95 } },		// night: a probe, the build, the peak two thirds in, a breath
		{ { 0, 120 },	{ 500, 90 },	{ 1000, 60 } },		// dawn: the last of them, falling away
		{ { 0, 80 },	{ 650, 200 },	{ 1000, 110 } },	// a Blood Moon's night: the same, to a higher peak
	};
	const Int row = phase == APOCALYPSE_NIGHT && bloodMoon ? APOCALYPSE_PHASES : phase >= 0 && phase < APOCALYPSE_PHASES ? phase : APOCALYPSE_DAY;
	const Int t = phaseFrames > 0 ? (Int)( (UnsignedInt)( intoPhase < phaseFrames ? intoPhase : phaseFrames ) * 1000u / phaseFrames ) : 1000;
	const Int at = t <= shapes[ row ][ 1 ][ 0 ] ? 0 : 1;
	const Int t0 = shapes[ row ][ at ][ 0 ], p0 = shapes[ row ][ at ][ 1 ];
	const Int t1 = shapes[ row ][ at + 1 ][ 0 ], p1 = shapes[ row ][ at + 1 ][ 1 ];
	return p0 + ( p1 - p0 ) * ( t - t0 ) / ( t1 - t0 );
}

Int Apocalypse_waveTheme( Int phase, UnsignedInt cycle, Int shapePercent, Int lastTheme, Int roll )
{
	if( shapePercent >= APOCALYPSE_PEAK_PERCENT )
		return APOCALYPSE_THEME_MIXED;
	if( phase == APOCALYPSE_DAY && cycle == 0 )
		return APOCALYPSE_THEME_HORDE;
	static const Int day[] = { APOCALYPSE_THEME_HORDE, APOCALYPSE_THEME_MIXED };
	static const Int dusk[] = { APOCALYPSE_THEME_HUNT, APOCALYPSE_THEME_HORDE, APOCALYPSE_THEME_SKY };
	static const Int night[] = { APOCALYPSE_THEME_HUNT, APOCALYPSE_THEME_SIEGE, APOCALYPSE_THEME_PLAGUE, APOCALYPSE_THEME_SKY };
	static const Int dawn[] = { APOCALYPSE_THEME_HORDE, APOCALYPSE_THEME_HUNT };
	const Int *list = phase == APOCALYPSE_DUSK ? dusk : phase == APOCALYPSE_NIGHT ? night : phase == APOCALYPSE_DAWN ? dawn : day;
	const Int size = phase == APOCALYPSE_DUSK ? (Int)ARRAY_SIZE( dusk ) : phase == APOCALYPSE_NIGHT ? (Int)ARRAY_SIZE( night )
		: phase == APOCALYPSE_DAWN ? (Int)ARRAY_SIZE( dawn ) : (Int)ARRAY_SIZE( day );
	// never the same theme twice running
	Int choices[ 4 ];
	Int count = 0;
	for( Int i = 0; i < size; ++i )
		if( list[ i ] != lastTheme )
			choices[ count++ ] = list[ i ];
	if( count == 0 )
		return list[ 0 ];
	return choices[ ( roll < 0 ? -roll : roll ) % count ];
}

/// a lattice corner's hash: the seed and both coordinates mixed (lowbias32's finaliser)
static UnsignedInt scatterHash( Int x, Int y, UnsignedInt seed )
{
	UnsignedInt h = seed ^ ( (UnsignedInt)x * 0x8DA6B343u ) ^ ( (UnsignedInt)y * 0xD8163841u );
	h ^= h >> 16;
	h *= 0x7FEB352Du;
	h ^= h >> 15;
	h *= 0x846CA68Bu;
	h ^= h >> 16;
	return h;
}

/// one octave of 2D Perlin gradient noise, about -0.7 to 0.7; the eight gradients are a fixed table, no trig
static Real scatterPerlin( Real x, Real y, UnsignedInt seed )
{
	static const Real gradients[ 8 ][ 2 ] =
	{
		{ 1.0f, 0.0f }, { -1.0f, 0.0f }, { 0.0f, 1.0f }, { 0.0f, -1.0f },
		{ 0.7071f, 0.7071f }, { -0.7071f, 0.7071f }, { 0.7071f, -0.7071f }, { -0.7071f, -0.7071f },
	};
	const Real fx = floorf( x ), fy = floorf( y );
	const Int ix = (Int)fx, iy = (Int)fy;
	const Real dx = x - fx, dy = y - fy;
	Real corner[ 2 ][ 2 ];
	for( Int cy = 0; cy < 2; ++cy )
		for( Int cx = 0; cx < 2; ++cx )
		{
			const Real *g = gradients[ scatterHash( ix + cx, iy + cy, seed ) & 7 ];
			corner[ cy ][ cx ] = g[ 0 ] * ( dx - (Real)cx ) + g[ 1 ] * ( dy - (Real)cy );
		}
	const Real u = dx * dx * dx * ( dx * ( dx * 6.0f - 15.0f ) + 10.0f );
	const Real v = dy * dy * dy * ( dy * ( dy * 6.0f - 15.0f ) + 10.0f );
	const Real a = corner[ 0 ][ 0 ] + u * ( corner[ 0 ][ 1 ] - corner[ 0 ][ 0 ] );
	const Real b = corner[ 1 ][ 0 ] + u * ( corner[ 1 ][ 1 ] - corner[ 1 ][ 0 ] );
	return a + v * ( b - a );
}

Real Apocalypse_scatterNoise( Real x, Real y, UnsignedInt seed )
{
	// a clump a cell wide, and a ragged edge on it at two fifths the size
	const Real cell = (Real)APOCALYPSE_SCATTER_CELL, fine = cell * 0.4f;
	const Real n = scatterPerlin( x / cell, y / cell, seed ) + 0.5f * scatterPerlin( x / fine, y / fine, seed ^ 0x9E3779B9u );
	const Real d = 0.5f + n * (Real)APOCALYPSE_SCATTER_CONTRAST_PERCENT / 100.0f;
	return d < 0.0f ? 0.0f : d > 1.0f ? 1.0f : d;
}

Int Apocalypse_scatterKeepPercent( Real density )
{
	const Real thin = (Real)APOCALYPSE_SCATTER_THIN_PERMILLE / 1000.0f;
	if( density <= thin )
		return APOCALYPSE_SCATTER_STRAGGLE;
	const Int keep = APOCALYPSE_SCATTER_STRAGGLE + (Int)( (Real)( 100 - APOCALYPSE_SCATTER_STRAGGLE ) * ( density - thin ) / ( 1.0f - thin ) );
	return keep < 100 ? keep : 100;
}

Real Apocalypse_nightAmount( Int phase, UnsignedInt intoPhase, UnsignedInt phaseFrames )
{
	const Real t = phaseFrames > 0 ? (Real)( intoPhase < phaseFrames ? intoPhase : phaseFrames ) / (Real)phaseFrames : 1.0f;
	switch( phase )
	{
		case APOCALYPSE_DUSK:		return t;
		case APOCALYPSE_NIGHT:	return 1.0f;
		case APOCALYPSE_DAWN:		return 1.0f - t;
		default:								return 0.0f;
	}
}

/// ticks a game second at this match's speed, read every time (never kept: the speed is per match)
static UnsignedInt gameSecond( void )
{
	const UnsignedInt frames = (UnsignedInt)LOGICFRAMES_PER_GAME_SECOND;
	return frames > 0 ? frames : 1;
}

void Apocalypse_phaseAt( UnsignedInt frame, Int *phase, UnsignedInt *intoPhase, UnsignedInt *phaseFrames, UnsignedInt *cycle )
{
	UnsignedInt cycleFrames = 0;
	for( Int p = 0; p < APOCALYPSE_PHASES; ++p )
		cycleFrames += Apocalypse_phaseRules( p ).seconds * gameSecond();
	*cycle = frame / cycleFrames;
	UnsignedInt into = frame % cycleFrames;
	for( Int p = 0; p < APOCALYPSE_PHASES; ++p )
	{
		const UnsignedInt length = Apocalypse_phaseRules( p ).seconds * gameSecond();
		if( into < length || p == APOCALYPSE_PHASES - 1 )
		{
			*phase = p;
			*intoPhase = into;
			*phaseFrames = length;
			return;
		}
		into -= length;
	}
}

/// a seat: a playable side that is not watching
static Bool isSeat( const Player *player )
{
	return player != NULL && player->isPlayableSide() && !player->isPlayerObserver();
}

//-------------------------------------------------------------------------------------------------
// The light, client side.  Only ever called through LogicThread::callAfterTick, so it runs on the
// main thread between two ticks.  It writes the client's light, which the logic never reads, and the
// drawables' NIGHT state, which it does read through launch bones: that one is set on every tick as a
// pure function of the frame (handLight).
//-------------------------------------------------------------------------------------------------

static GlobalData::TerrainLighting theBaseTerrainLight[ MAX_GLOBAL_LIGHTS ];
static GlobalData::TerrainLighting theBaseObjectLight[ MAX_GLOBAL_LIGHTS ];
static Real theBaseShadowStrength = 0.0f;
static Bool theBaseLightKept = FALSE;

/// the map's own light for its hour, as it loaded, before any tint, and the sun shadows' own darkness
static void apocalypseKeepBaseLight( void )
{
	const Int tod = TheGlobalData->m_timeOfDay;
	for( Int k = 0; k < MAX_GLOBAL_LIGHTS; ++k )
	{
		theBaseTerrainLight[ k ] = TheGlobalData->m_terrainLighting[ tod ][ k ];
		theBaseObjectLight[ k ] = TheGlobalData->m_terrainObjectsLighting[ tod ][ k ];
	}
	theBaseShadowStrength = TheGlobalData->m_shadowMapStrength;
	theBaseLightKept = TRUE;
}

/** The shadow darkness is a setting of the whole game, not the map's, so it goes back when the match
	  ends; the light tables come back with the next map's own. */
static void apocalypseRestoreShadow( void )
{
	if( !theBaseLightKept )
		return;
	TheWritableGlobalData->m_shadowMapStrength = theBaseShadowStrength;
	theBaseLightKept = FALSE;
}

static void tint( RGBColor &out, const RGBColor &base, const Real *day, const Real *night, Real amount )
{
	out.red = base.red * ( day[ 0 ] + ( night[ 0 ] - day[ 0 ] ) * amount );
	out.green = base.green * ( day[ 1 ] + ( night[ 1 ] - day[ 1 ] ) * amount );
	out.blue = base.blue * ( day[ 2 ] + ( night[ 2 ] - day[ 2 ] ) * amount );
}

/** Tint the map's light `level` steps of APOCALYPSE_LIGHT_STEPS toward the red night (a faint warm haze
	  at 0), and put every drawable in its NIGHT state or out of it.

	  The night takes the sun down to seven tenths of its red and under a third of the rest, and lifts
	  the ambient a tenth in red while keeping three fifths of the rest.  An ambient cut as hard as the
	  sun left every face turned away from it black, so a red zombie read as a silhouette; a sun at full
	  red over a cut ambient summed past white in red alone, and the darker bands of palm trunks and
	  fronds went black while the light ones went orange, tiger stripes out of the texture itself.

	  The Direct3D 11 sun shadow darkens a blocked pixel by a fixed strength, whatever the light, so
	  under a dimmed sun it stayed as hard as at noon; the night halves it.  The -d3d9 stencil
	  shadows keep the map's own colour.

	  The ground's full relight costs a 40 to 56 ms frame (TERRAIN FULL UPDATE), so the tables move
	  every APOCALYPSE_LIGHT_TINT_EVERY steps of the amount, eight times a dusk; the NIGHT state goes
	  with every step.

	  The ground reads m_terrainAmbient and m_terrainDiffuse, GlobalData::setTimeOfDay's copies of the
	  hour's terrain light, and never the table itself: the Direct3D 11 ground's sun every frame
	  (HeightMap.cpp) and the vertex light the -d3d9 ground bakes in staticLightingChanged.  Tinting the
	  table alone left the ground at noon under red units, so the copies are written here too, the
	  hour itself untouched (the logic reads m_timeOfDay). */
static void apocalypseApplyLight( Int level, Bool relight, Bool lightsOn, Bool bloodMoon )
{
	if( relight && theBaseLightKept )
	{
		static const Real HAZE[ 3 ] = { 1.03f, 0.99f, 0.93f };
		static const Real PLAIN_AMBIENT[ 3 ] = { 1.10f, 0.62f, 0.60f };
		static const Real PLAIN_DIFFUSE[ 3 ] = { 0.70f, 0.32f, 0.28f };
		// a Blood Moon's night: the same red, so it sums no further past white, over less of the rest
		static const Real BLOOD_AMBIENT[ 3 ] = { 1.10f, 0.46f, 0.42f };
		static const Real BLOOD_DIFFUSE[ 3 ] = { 0.72f, 0.19f, 0.15f };
		const Real *NIGHT_AMBIENT = bloodMoon ? BLOOD_AMBIENT : PLAIN_AMBIENT;
		const Real *NIGHT_DIFFUSE = bloodMoon ? BLOOD_DIFFUSE : PLAIN_DIFFUSE;
		const Real amount = (Real)level / (Real)APOCALYPSE_LIGHT_STEPS;
		const Int tod = TheGlobalData->m_timeOfDay;
		for( Int k = 0; k < MAX_GLOBAL_LIGHTS; ++k )
		{
			GlobalData::TerrainLighting &terrain = TheWritableGlobalData->m_terrainLighting[ tod ][ k ];
			GlobalData::TerrainLighting &objects = TheWritableGlobalData->m_terrainObjectsLighting[ tod ][ k ];
			tint( terrain.ambient, theBaseTerrainLight[ k ].ambient, HAZE, NIGHT_AMBIENT, amount );
			tint( terrain.diffuse, theBaseTerrainLight[ k ].diffuse, HAZE, NIGHT_DIFFUSE, amount );
			tint( objects.ambient, theBaseObjectLight[ k ].ambient, HAZE, NIGHT_AMBIENT, amount );
			tint( objects.diffuse, theBaseObjectLight[ k ].diffuse, HAZE, NIGHT_DIFFUSE, amount );
			TheWritableGlobalData->m_terrainAmbient[ k ] = terrain.ambient;
			TheWritableGlobalData->m_terrainDiffuse[ k ] = terrain.diffuse;
		}
		// ponytail: 0.45 and 0.52 mirror SHADOW_MAP_STRENGTH and SHADOW_MAP_REFORGED_STRENGTH
		// (W3DVolumetricShadow.cpp), the strengths a 0 here stands for; move them to GlobalData if they change
		const Real dayStrength = theBaseShadowStrength > 0.0f ? theBaseShadowStrength
			: TheGlobalData->m_lightingStyle == LIGHTING_STYLE_REFORGED ? 0.52f : 0.45f;
		TheWritableGlobalData->m_shadowMapStrength = level > 0 ? dayStrength * ( 1.0f - 0.5f * amount ) : theBaseShadowStrength;
		if( !TheGlobalData->m_headless && TheDisplay != NULL )
			TheDisplay->setTimeOfDay( (TimeOfDay)tod );
	}

	if( TheGameClient == NULL )
		return;
	for( Drawable *draw = TheGameClient->firstDrawable(); draw != NULL; draw = draw->getNextDrawable() )
	{
		if( ( draw->getModelConditionFlags().test( MODELCONDITION_NIGHT ) != 0 ) == ( lightsOn != 0 ) )
			continue;
		ModelConditionFlags flags = draw->getModelConditionFlags();
		flags.set( MODELCONDITION_NIGHT, lightsOn ? 1 : 0 );
		draw->replaceModelConditionFlags( flags );
	}
}

//-------------------------------------------------------------------------------------------------
// The fog ring's colour and the smoke's, client side, handed over the same way as the light and
// kept apart from it: the shroud's colour (GlobalData::m_shroudColor, a multiply over every fogged
// and shrouded cell) goes from a faint warm red by day to a deep one at night, and so does the tint
// over every smoke and dust particle (ParticleSystemManager::setSmokeTint).  Both are settings of the
// whole game, kept when the map loads and given back when the match ends.
//-------------------------------------------------------------------------------------------------

static RGBColor theBaseShroudColor;
static Bool theBaseFogKept = FALSE;

/* The heat in the fog, client side: how thick the dead stand in each block of APOCALYPSE_HEAT_BLOCK
	partition cells a side, counted by the director every APOCALYPSE_HEAT_SECONDS and handed over after
	the tick.  The ring's shroud reads it back per cell (Apocalypse_heatAt) and thins where it is hot,
	so the red fog glows over a gathering horde.  Never saved: the next count rebuilds it. */
static std::vector<UnsignedByte> theHeat;
static Int theHeatBlocksX = 0, theHeatBlocksY = 0;

static void apocalypseApplyHeat( const std::vector<UnsignedByte> &heat, Int blocksX, Int blocksY )
{
	theHeat = heat;
	theHeatBlocksX = blocksX;
	theHeatBlocksY = blocksY;
	ThePartitionManager->redrawShroudForLocalPlayer();
}

void Apocalypse_heatGrid( Int *blocksX, Int *blocksY )
{
	*blocksX = theHeatBlocksX;
	*blocksY = theHeatBlocksY;
}

Int Apocalypse_heatOfBlock( Int blockX, Int blockY )
{
	if( blockX < 0 || blockY < 0 || blockX >= theHeatBlocksX || blockY >= theHeatBlocksY )
		return 0;
	return theHeat[ blockY * theHeatBlocksX + blockX ];
}

Int Apocalypse_heatAt( Int cellX, Int cellY )
{
	if( theHeat.empty() )
		return 0;
	// between the four nearest blocks' middles, so a horde reads as a soft blob and not as squares
	const Real bx = ( (Real)cellX + 0.5f ) / (Real)APOCALYPSE_HEAT_BLOCK - 0.5f;
	const Real by = ( (Real)cellY + 0.5f ) / (Real)APOCALYPSE_HEAT_BLOCK - 0.5f;
	const Int x0 = (Int)floorf( bx ), y0 = (Int)floorf( by );
	const Real tx = bx - (Real)x0, ty = by - (Real)y0;
	const Real top = Apocalypse_heatOfBlock( x0, y0 ) + ( Apocalypse_heatOfBlock( x0 + 1, y0 ) - Apocalypse_heatOfBlock( x0, y0 ) ) * tx;
	const Real bottom = Apocalypse_heatOfBlock( x0, y0 + 1 ) + ( Apocalypse_heatOfBlock( x0 + 1, y0 + 1 ) - Apocalypse_heatOfBlock( x0, y0 + 1 ) ) * tx;
	return (Int)( top + ( bottom - top ) * ty + 0.5f );
}

static void apocalypseKeepFog( void )
{
	theBaseShroudColor = TheGlobalData->m_shroudColor;
	theBaseFogKept = TRUE;
}

static void apocalypseRestoreFog( void )
{
	theHeat.clear();
	theHeatBlocksX = theHeatBlocksY = 0;
	if( !theBaseFogKept )
		return;
	TheWritableGlobalData->m_shroudColor = theBaseShroudColor;
	RGBColor white;
	white.red = white.green = white.blue = 1.0f;
	if( TheParticleSystemManager != NULL )
		TheParticleSystemManager->setSmokeTint( white );
	theBaseFogKept = FALSE;
}

/// `level` of APOCALYPSE_LIGHT_STEPS into the night; a Blood Moon's night is a deeper red
static void apocalypseApplyFog( Int level, Bool bloodMoon )
{
	if( !theBaseFogKept )
		return;
	const Real amount = (Real)level / (Real)APOCALYPSE_LIGHT_STEPS;
	static const Real FOG_DAY[ 3 ] = { 1.00f, 0.62f, 0.56f };
	// red stays whole: the Direct3D 11 fog memory reads the brightest channel as the shroud's light
	static const Real FOG_PLAIN_NIGHT[ 3 ] = { 1.00f, 0.26f, 0.21f };
	static const Real FOG_BLOOD_NIGHT[ 3 ] = { 1.00f, 0.10f, 0.08f };
	static const Real SMOKE_DAY[ 3 ] = { 1.00f, 0.88f, 0.82f };
	static const Real SMOKE_PLAIN_NIGHT[ 3 ] = { 0.95f, 0.45f, 0.38f };
	static const Real SMOKE_BLOOD_NIGHT[ 3 ] = { 0.95f, 0.28f, 0.22f };
	const Real *FOG_NIGHT = bloodMoon ? FOG_BLOOD_NIGHT : FOG_PLAIN_NIGHT;
	const Real *SMOKE_NIGHT = bloodMoon ? SMOKE_BLOOD_NIGHT : SMOKE_PLAIN_NIGHT;
	RGBColor fog, smoke;
	fog.red = FOG_DAY[ 0 ] + ( FOG_NIGHT[ 0 ] - FOG_DAY[ 0 ] ) * amount;
	fog.green = FOG_DAY[ 1 ] + ( FOG_NIGHT[ 1 ] - FOG_DAY[ 1 ] ) * amount;
	fog.blue = FOG_DAY[ 2 ] + ( FOG_NIGHT[ 2 ] - FOG_DAY[ 2 ] ) * amount;
	smoke.red = SMOKE_DAY[ 0 ] + ( SMOKE_NIGHT[ 0 ] - SMOKE_DAY[ 0 ] ) * amount;
	smoke.green = SMOKE_DAY[ 1 ] + ( SMOKE_NIGHT[ 1 ] - SMOKE_DAY[ 1 ] ) * amount;
	smoke.blue = SMOKE_DAY[ 2 ] + ( SMOKE_NIGHT[ 2 ] - SMOKE_DAY[ 2 ] ) * amount;
	TheWritableGlobalData->m_shroudColor = fog;
	if( TheParticleSystemManager != NULL )
		TheParticleSystemManager->setSmokeTint( smoke );
	// the shroud texture holds the colour per cell: write every cell again, the display's copy only
	if( ThePartitionManager != NULL )
		ThePartitionManager->redrawShroudForLocalPlayer();
}

//-------------------------------------------------------------------------------------------------
ApocalypseDirector::ApocalypseDirector( void )
{
	m_active = FALSE;
	reset();
}

//-------------------------------------------------------------------------------------------------
void ApocalypseDirector::reset( void )
{
	if( m_active )
	{
		LogicThread::callAfterTick( []() { apocalypseRestoreShadow(); } );
		LogicThread::callAfterTick( []() { apocalypseRestoreFog(); } );
	}
	m_active = FALSE;
	m_centre.zero();
	m_reachRadius = 0.0f;
	m_spawns.clear();
	m_zombiePlayer = -1;
	m_seats = 0;
	m_kinds.clear();
	m_phase = APOCALYPSE_DAY;
	m_phaseStartFrame = 0;
	m_cycle = 0;
	m_wave = 0;
	m_nextWaveFrame = 0;
	m_nextSpawnFrame = 0;
	m_pending = 0;
	m_spawned = 0;
	m_kills = 0;
	m_reached = 0;
	m_zombies.clear();
	m_hasRing = FALSE;
	m_clearLo.x = m_clearLo.y = m_clearHi.x = m_clearHi.y = 0.0f;
	m_playLo = m_clearLo;
	m_playHi = m_clearLo;
	m_fogStays.clear();
	m_fogKills = 0;
	m_fieldRepairs = 0;
	m_field.clear();
	m_fieldWanted = FALSE;
	m_fieldSteps = 0;
	m_fieldLoaded = FALSE;
	m_fieldLoadedRepair = FALSE;
	m_fieldLoadedBuild = FALSE;
	m_groundChanges.clear();
	m_fieldAnswered = m_fieldRefused = m_fieldCells = 0;
	m_eliteRoom = 0;
	m_flyerRoom = 0;
	m_bossNight = 0;
	m_waveSeed = 0;
	m_waveTheme = APOCALYPSE_THEME_MIXED;
	m_kindThemes.clear();
	m_scatterLog.clear();
	m_scatterFallbacks = 0;
	m_waveShapeLogged = 100;
	m_colossusKind = -1;
	m_kingKind = -1;
	m_broodKind = -1;
	m_straysLogged = 0;
	m_kingRaisesLogged = 0;
	m_waveSectors = 0;
	m_warnSectors = 0;
	m_adapt = APOCALYPSE_ADAPT_START;
	m_frontTurn = 0;
	m_reachedAtWave = 0;
	m_rises.clear();
	m_infected = 0;
	m_fogLevel = -1;
	m_lightLevel = -1;
	m_lightsOn = FALSE;
	m_fallLogged = FALSE;
}

//-------------------------------------------------------------------------------------------------
void ApocalypseDirector::newMap( Bool loadingSaveGame )
{
	reset();

	const Waypoint *centre = TheTerrainLogic->getWaypointByName( "Apocalypse_Centre" );
	if( centre == NULL )
		return;
	for( Int k = 1; ; ++k )
	{
		AsciiString name;
		name.format( "Apocalypse_Spawn_%d", k );
		const Waypoint *spawn = TheTerrainLogic->getWaypointByName( name );
		if( spawn == NULL )
			break;
		if( k > APOCALYPSE_SPAWNS_MOST )
		{
			DEBUG_LOG(( "HEADLESS APOCALYPSE: the map has more than %d spawns, the rest stay empty\n", APOCALYPSE_SPAWNS_MOST ));
			break;
		}
		m_spawns.push_back( *spawn->getLocation() );
	}
	if( m_spawns.empty() )
	{
		DEBUG_LOG(( "HEADLESS APOCALYPSE: the map has Apocalypse_Centre but no Apocalypse_Spawn_1, so the mode stays off\n" ));
		return;
	}
	Player *civilian = ThePlayerList->findPlayerWithNameKey( NAMEKEY( "PlyrCivilian" ) );
	if( civilian == NULL )
	{
		DEBUG_LOG(( "HEADLESS APOCALYPSE: the map has no PlyrCivilian to own the dead, so the mode stays off\n" ));
		m_spawns.clear();
		return;
	}

	m_active = TRUE;
	m_centre = *centre->getLocation();
	m_zombiePlayer = civilian->getPlayerIndex();
	Real farthest = 0.0f;
	for( Int k = 1; ; ++k )
	{
		AsciiString name;
		name.format( "Player_%d_Start", k );
		const Waypoint *start = TheTerrainLogic->getWaypointByName( name );
		if( start == NULL )
			break;
		const Real dx = start->getLocation()->x - m_centre.x, dy = start->getLocation()->y - m_centre.y;
		farthest = max( farthest, (Real)sqrtf( dx * dx + dy * dy ) );
	}
	m_reachRadius = farthest + (Real)APOCALYPSE_BASE_REACH;
	m_kinds.resize( Apocalypse_kindCount(), NULL );
	m_kindThemes.resize( Apocalypse_kindCount(), 0 );
	for( Int k = 0; k < Apocalypse_kindCount(); ++k )
	{
		m_kindThemes[ k ] = Apocalypse_themesOfKind( Apocalypse_kind( k ).name );
		m_kinds[ k ] = TheThingFactory->findTemplate( Apocalypse_kind( k ).name, FALSE );
		if( m_kinds[ k ] == NULL )
		{
			DEBUG_LOG(( "HEADLESS APOCALYPSE: no '%s' template in the data, %s\n", Apocalypse_kind( k ).name,
									k == APOCALYPSE_KIND_WALKER ? "so nothing will spawn" : "so walkers come in its place" ));
			DEBUG_CRASH(( "Apocalypse: no '%s' template", Apocalypse_kind( k ).name ));
		}
	}
	m_colossusKind = kindNamed( "ApocalypseZombieColossus" );
	m_kingKind = kindNamed( "ApocalypseZombiePlagueKing" );
	m_broodKind = kindNamed( "ApocalypseZombieBroodmother" );

	for( Int i = 0; i < ThePlayerList->getPlayerCount(); ++i )
		if( isSeat( ThePlayerList->getNthPlayer( i ) ) )
			++m_seats;
	// a co-op mode for four at the most: the waves never size for more, whatever the map was played with
	if( m_seats > APOCALYPSE_SEATS_MOST )
		m_seats = APOCALYPSE_SEATS_MOST;

	if( !loadingSaveGame )
	{
		// the map, not the lobby, makes the sides: every seat together, the dead against all of them
		for( Int i = 0; i < ThePlayerList->getPlayerCount(); ++i )
		{
			Player *player = ThePlayerList->getNthPlayer( i );
			if( !isSeat( player ) )
				continue;
			for( Int j = 0; j < ThePlayerList->getPlayerCount(); ++j )
			{
				Player *other = ThePlayerList->getNthPlayer( j );
				if( other != player && isSeat( other ) )
					player->setPlayerRelationship( other, ALLIES );
			}
			player->setPlayerRelationship( civilian, ENEMIES );
			civilian->setPlayerRelationship( player, ENEMIES );
		}
	}

	// the fog ring: everything between the clear area's outline and the map's edge, shrouded for good
	const PolygonTrigger *clear = TheTerrainLogic->getTriggerAreaByName( "Apocalypse_Clear" );
	if( clear != NULL && clear->getNumPoints() >= 3 )
	{
		ICoord3D lo = *clear->getPoint( 0 ), hi = lo;
		for( Int p = 1; p < clear->getNumPoints(); ++p )
		{
			const ICoord3D *pt = clear->getPoint( p );
			lo.x = min( lo.x, pt->x );
			lo.y = min( lo.y, pt->y );
			hi.x = max( hi.x, pt->x );
			hi.y = max( hi.y, pt->y );
		}
		Region3D extent;
		TheTerrainLogic->getExtent( &extent );
		m_playLo.x = extent.lo.x;
		m_playLo.y = extent.lo.y;
		m_playHi.x = extent.hi.x;
		m_playHi.y = extent.hi.y;
		m_clearLo.x = (Real)lo.x;
		m_clearLo.y = (Real)lo.y;
		m_clearHi.x = (Real)hi.x;
		m_clearHi.y = (Real)hi.y;
		m_hasRing = TRUE;
		// the cells outside the area's own outline, which is what inRing reads back: shroud, kill test and spawns agree
		const Int cells = ThePartitionManager->setAlwaysShroudedOutside( clear );
		DEBUG_LOG(( "HEADLESS APOCALYPSE: fog ring outside (%.0f,%.0f)-(%.0f,%.0f) of the map (%.0f,%.0f)-(%.0f,%.0f), %d cells shrouded for good\n",
								m_clearLo.x, m_clearLo.y, m_clearHi.x, m_clearHi.y, m_playLo.x, m_playLo.y, m_playHi.x, m_playHi.y, cells ));
	}
	LogicThread::callAfterTick( []() { apocalypseKeepFog(); } );

	m_phaseStartFrame = TheGameLogic->getFrame();
	m_nextWaveFrame = TheGameLogic->getFrame() + APOCALYPSE_FIRST_WAVE_SECONDS * gameSecond();
	m_fieldWanted = TRUE;		// on the first tick, once the map's structures are all in
	// the map has just put its own light in; a save load overwrites the clock below, not the light
	LogicThread::callAfterTick( []() { apocalypseKeepBaseLight(); } );
	DEBUG_LOG(( "HEADLESS APOCALYPSE: on; %d seats, %d spawns, centre (%.0f,%.0f), zombies of player %d, first wave at frame %u\n",
							m_seats, (Int)m_spawns.size(), m_centre.x, m_centre.y, m_zombiePlayer, m_nextWaveFrame ));
}

//-------------------------------------------------------------------------------------------------
Int ApocalypseDirector::seatsAlive( void ) const
{
	Int alive = 0;
	for( Int i = 0; i < ThePlayerList->getPlayerCount(); ++i )
	{
		const Player *player = ThePlayerList->getNthPlayer( i );
		if( isSeat( player ) && player->hasAnyObjects() )
			++alive;
	}
	return alive;
}

//-------------------------------------------------------------------------------------------------
Int ApocalypseDirector::kindOf( const Object *obj ) const
{
	for( Int k = 0; k < (Int)m_kinds.size(); ++k )
		if( m_kinds[ k ] != NULL && obj->getTemplate()->isEquivalentTo( m_kinds[ k ] ) )
			return k;
	return -1;
}

//-------------------------------------------------------------------------------------------------
Bool ApocalypseDirector::isZombie( const Object *obj ) const
{
	if( !m_active || obj == NULL || obj->getControllingPlayer() == NULL || obj->getControllingPlayer()->getPlayerIndex() != m_zombiePlayer )
		return FALSE;
	// a roster kind, or a vehicle a Leech took (only a Leech hijacks for the dead)
	return kindOf( obj ) >= 0 || obj->testStatus( OBJECT_STATUS_HIJACKED );
}

//-------------------------------------------------------------------------------------------------
Int ApocalypseDirector::skillPointsOf( const Object *zombie ) const
{
	const Int k = kindOf( zombie );
	// a Leech's prize pays what the vehicle pays anyone who kills it
	if( k < 0 )
		return zombie->testStatus( OBJECT_STATUS_HIJACKED ) ? zombie->getTemplate()->getSkillPointValue( zombie->getVeterancyLevel() ) : 0;
	const ApocalypseKind &kind = Apocalypse_kind( k );
	return kind.points * ( ( kind.flags & APOCALYPSE_KIND_BOSS ) ? APOCALYPSE_SKILL_PER_POINT_BOSS : APOCALYPSE_SKILL_PER_POINT );
}

//-------------------------------------------------------------------------------------------------
/** A plane flying a general's power in from off the map: a DeliverPayload transport (the Supply Drop
	  Zone's and the Supply Drop's cargo plane, the paradrops, the A-10, B-52 and carpet bomber strikes)
	  or the Spectre gunship.  The power, not the player, flies them, and they come in over the map's
	  edge, through the fog ring: the cargo plane flies 125 a game second, so the 4 s grace killed it
	  500 in with its cargo still aboard.  Every aircraft the player orders still dies there. */
static Bool isOffMapDelivery( const Object *obj )
{
	static const NameKeyType key_DeliverPayloadAIUpdate = NAMEKEY( "DeliverPayloadAIUpdate" );
	static const NameKeyType key_SpectreGunshipUpdate = NAMEKEY( "SpectreGunshipUpdate" );
	return obj->findUpdateModule( key_DeliverPayloadAIUpdate ) != NULL || obj->findUpdateModule( key_SpectreGunshipUpdate ) != NULL;
}

//-------------------------------------------------------------------------------------------------
/** An idle zombie goes down the flow field to the nearest structure of a seat's by the way there; one
	  already beside it, or off the field, at the nearest thing a seat has, or back to the middle when
	  nothing is left.  A Leech (HijackerUpdate) goes into the nearest vehicle it can take first, and is
	  left alone while it rides one: MASKED, kept on the vehicle by HijackerUpdate and out of the partition. */
void ApocalypseDirector::sendOn( Object *obj )
{
	AIUpdateInterface *ai = obj->getAI();
	if( ai == NULL || !ai->isIdle() || obj->testStatus( OBJECT_STATUS_MASKED ) )
		return;
	/* The Colossus walks for the costliest structure a seat has (its template's build cost; ties to the
		 lower ObjectID), whatever stands between: a superweapon or a war factory before a wall.  Walked in
		 object list order with integer costs. */
	if( m_colossusKind >= 0 && m_kinds[ m_colossusKind ] != NULL && obj->getTemplate()->isEquivalentTo( m_kinds[ m_colossusKind ] ) )
	{
		Object *dearest = NULL;
		Int dearestCost = -1;
		for( Object *other = TheGameLogic->getFirstObject(); other; other = other->getNextObject() )
		{
			if( other->isEffectivelyDead() || !other->isKindOf( KINDOF_STRUCTURE ) || !isSeat( other->getControllingPlayer() ) )
				continue;
			const Int cost = other->getTemplate()->friend_getBuildCost();
			if( cost > dearestCost || ( cost == dearestCost && dearest != NULL && other->getID() < dearest->getID() ) )
			{
				dearest = other;
				dearestCost = cost;
			}
		}
		if( dearest != NULL )
		{
			DEBUG_LOG(( "HEADLESS APOCALYPSE: frame %u the Colossus %d goes for '%s' %d of player %d, cost %d\n", TheGameLogic->getFrame(),
									(Int)obj->getID(), dearest->getTemplate()->getName().str(), (Int)dearest->getID(),
									dearest->getControllingPlayer()->getPlayerIndex(), dearestCost ));
			ai->aiAttackObject( dearest, NO_MAX_SHOTS_LIMIT, CMD_FROM_AI );
			return;
		}
	}
	PartitionFilterRelationship enemies( obj, PartitionFilterRelationship::ALLOW_ENEMIES );
	PartitionFilterAlive alive;
	static const NameKeyType key_HijackerUpdate = NAMEKEY( "HijackerUpdate" );
	if( obj->findUpdateModule( key_HijackerUpdate ) != NULL )
	{
		PartitionFilterPossibleToHijack takeable( obj, CMD_FROM_AI );
		PartitionFilter *hijack[] = { &enemies, &alive, &takeable, NULL };
		Object *ride = ThePartitionManager->getClosestObject( obj, 1.0e6f, FROM_CENTER_2D, hijack );
		if( ride != NULL )
		{
			ai->aiEnter( ride, CMD_FROM_AI );
			return;
		}
	}
	Coord3D goal;
	if( fieldGoal( obj, &goal ) )
	{
		ai->aiAttackMoveToPosition( &goal, NO_MAX_SHOTS_LIMIT, CMD_FROM_AI );
		return;
	}
	PartitionFilter *filters[] = { &enemies, &alive, NULL };
	const Object *target = ThePartitionManager->getClosestObject( obj, 1.0e6f, FROM_CENTER_2D, filters );
	ai->aiAttackMoveToPosition( target != NULL ? target->getPosition() : &m_centre, NO_MAX_SHOTS_LIMIT, CMD_FROM_AI );
}

//-------------------------------------------------------------------------------------------------
Int ApocalypseDirector::spawnOne( UnsignedInt now )
{
	/* The kind out of the phase's mix, drawn only over those the night allows, the data has, the points
		 left pay for (a whole flock), and the wave's elite and flyer room still takes.  The walker is
		 always among them, at a point. */
	const Int kinds = (Int)m_kinds.size();
	auto eligible = [&]( Int k ) -> Bool
	{
		const ApocalypseKind &row = Apocalypse_kind( k );
		const Int cost = row.points * row.group;
		return Apocalypse_kindAllowed( k, m_phase, m_cycle ) && m_kinds[ k ] != NULL && cost <= m_pending
			&& ( !( row.flags & APOCALYPSE_KIND_ELITE ) || m_eliteRoom > 0 )
			&& ( !( row.flags & APOCALYPSE_KIND_FLYER ) || cost <= m_flyerRoom );
	};
	// the wave's theme leans on its own kinds
	auto weightOf = [&]( Int k ) -> Int
	{
		const Int weight = Apocalypse_kind( k ).weight[ m_phase ];
		const Bool themed = k < (Int)m_kindThemes.size() && ( m_kindThemes[ k ] & ( 1u << m_waveTheme ) ) != 0;
		return themed ? weight * APOCALYPSE_THEME_BOOST : weight;
	};
	Int total = 0;
	for( Int k = 0; k < kinds; ++k )
		if( eligible( k ) )
			total += weightOf( k );
	Int kind = APOCALYPSE_KIND_WALKER;
	Int roll = GameLogicRandomValue( 0, total > 0 ? total - 1 : 0 );
	// from the last row up, the walker taking what is left over
	for( Int k = kinds - 1; k > APOCALYPSE_KIND_WALKER; --k )
	{
		if( !eligible( k ) )
			continue;
		roll -= weightOf( k );
		if( roll < 0 )
		{
			kind = k;
			break;
		}
	}
	const ApocalypseKind &row = Apocalypse_kind( kind );
	m_pending -= row.points * row.group;
	if( row.flags & APOCALYPSE_KIND_ELITE )
		--m_eliteRoom;
	if( row.flags & APOCALYPSE_KIND_FLYER )
		m_flyerRoom -= row.points * row.group;

	// one of the gates the wave gathered at; a pack that hunts the weak front takes that front's gate
	Int sector = ( row.flags & APOCALYPSE_KIND_WEAKFRONT ) ? weakestFront( now, row.name ) : -1;
	if( sector < 0 )
	{
		Int bits = 0;
		for( Int g = 0; g < (Int)m_spawns.size(); ++g )
			if( m_waveSectors & ( 1u << g ) )
				++bits;
		if( bits > 0 )
		{
			Int pick = GameLogicRandomValue( 0, bits - 1 );
			for( Int g = 0; g < (Int)m_spawns.size(); ++g )
				if( ( m_waveSectors & ( 1u << g ) ) && pick-- == 0 )
				{
					sector = g;
					break;
				}
		}
	}

	Team *team = ThePlayerList->getNthPlayer( m_zombiePlayer )->getDefaultTeam();
	Object *obj = TheThingFactory->newObject( m_kinds[ kind ], team );
	if( obj == NULL )
		return 0;
	obj->setOrientation( 0.0f );
	AIUpdateInterface *ai = obj->getAI();

	// in the fog ring before that gate; on a map without one, or where no draw found such a spot, at the
	// gate's spawn waypoint
	Coord3D pos;
	Int which = -1;
	if( !m_hasRing || !ringSpot( &pos, obj, sector ) )
	{
		which = sector >= 0 ? sector : GameLogicRandomValue( 0, (Int)m_spawns.size() - 1 );
		const Coord3D &spawn = m_spawns[ which ];
		pos = spawn;
		pos.x += GameLogicRandomValueReal( -(Real)APOCALYPSE_SPAWN_SPREAD, (Real)APOCALYPSE_SPAWN_SPREAD );
		pos.y += GameLogicRandomValueReal( -(Real)APOCALYPSE_SPAWN_SPREAD, (Real)APOCALYPSE_SPAWN_SPREAD );
		pos.z = TheTerrainLogic->getGroundHeight( pos.x, pos.y );
		// a tree or a rock where the spread put it: the spawn itself, which is on the clear trail
		if( ai != NULL && !TheAI->pathfinder()->validMovementPosition( obj->getCrusherLevel() > 0, obj->getLayer(), ai->getLocomotorSet(), &pos ) )
		{
			pos = spawn;
			pos.z = TheTerrainLogic->getGroundHeight( pos.x, pos.y );
		}
	}
	obj = makeZombie( kind, pos, now, obj );
	if( obj == NULL )
		return 0;
	if( which >= 0 )
		++m_scatterFallbacks;
	if( (Int)m_scatterLog.size() < APOCALYPSE_SCATTER_LOG_MOST )
	{
		Coord2D spot;
		spot.x = pos.x;
		spot.y = pos.y;
		m_scatterLog.push_back( spot );
	}
	const Bool logIt = m_spawned <= 3 || ( kind != APOCALYPSE_KIND_WALKER && m_spawned % 25 == 0 ) || row.group > 1;
	if( logIt )
	{
		AsciiString where;
		if( which >= 0 )
			where.format( "at spawn %d", which + 1 );
		else
			where.format( "in the fog before gate %d", sector + 1 );
		DEBUG_LOG(( "HEADLESS APOCALYPSE: frame %u '%s' %d out %s (%.0f,%.0f)%s\n", now,
								m_kinds[ kind ]->getName().str(), (Int)obj->getID(), where.str(), pos.x, pos.y, row.group > 1 ? ", the first of its group" : "" ));
	}

	/* The rest of a flock or a pack, loose round the first's spot: each drawn in a disc that grows with
		 the group and stays inside the ring spot's own margin off the fog's edge, so none of them stands
		 in sight.  Where one cannot stand (a ground kind on a rock, out of the fog), a ring spot of its own,
		 a straggler of the pack; the first's spot itself only when that finds none either. */
	Int made = 1;
	const Real radius = min( (Real)APOCALYPSE_GROUP_SPACING * (Real)( 1 + row.group ) * 0.5f, (Real)( APOCALYPSE_FOG_EDGE_IN - 6 ) );
	for( Int m = 1; m < row.group; ++m )
	{
		Coord3D at = pos;
		Real dx = 0.0f, dy = 0.0f;
		for( Int tryNo = 0; tryNo < 4; ++tryNo )
		{
			dx = GameLogicRandomValueReal( -radius, radius );
			dy = GameLogicRandomValueReal( -radius, radius );
			if( dx * dx + dy * dy <= radius * radius )
				break;
		}
		at.x += dx;
		at.y += dy;
		at.z = TheTerrainLogic->getGroundHeight( at.x, at.y );
		AIUpdateInterface *firstAI = obj->getAI();
		const Bool stands = ( which >= 0 || inRing( &at ) )
			&& ( firstAI == NULL || TheAI->pathfinder()->validMovementPosition( obj->getCrusherLevel() > 0, obj->getLayer(), firstAI->getLocomotorSet(), &at ) );
		if( !stands && ( which >= 0 || !ringSpot( &at, obj, sector ) ) )
			at = pos;
		if( makeZombie( kind, at, now ) != NULL )
		{
			++made;
			if( (Int)m_scatterLog.size() < APOCALYPSE_SCATTER_LOG_MOST )
			{
				Coord2D spot;
				spot.x = at.x;
				spot.y = at.y;
				m_scatterLog.push_back( spot );
			}
		}
	}
	return made;
}

//-------------------------------------------------------------------------------------------------
/** One of the dead of that kind standing at pos, built, ranked for the cycle, on the list and walking at
	  the middle; NULL when the data has no such template.  `made` is one already made of that kind
	  (spawnOne makes it first to ask where it may stand), NULL to make one here. */
Object *ApocalypseDirector::makeZombie( Int kind, const Coord3D &pos, UnsignedInt now, Object *made )
{
	if( m_kinds[ kind ] == NULL )
		return NULL;
	Team *team = ThePlayerList->getNthPlayer( m_zombiePlayer )->getDefaultTeam();
	Object *obj = made != NULL ? made : TheThingFactory->newObject( m_kinds[ kind ], team );
	if( obj == NULL )
		return NULL;
	obj->setOrientation( 0.0f );
	obj->setPosition( &pos );
	for( BehaviorModule **m = obj->getBehaviorModules(); *m; ++m )
	{
		CreateModuleInterface *create = (*m)->getCreate();
		if( create )
			create->onBuildComplete();
	}
	const Int rank = Apocalypse_cycleRank( m_cycle );
	if( rank > LEVEL_REGULAR && obj->getExperienceTracker() != NULL )
		obj->getExperienceTracker()->setVeterancyLevel( (VeterancyLevel)rank, FALSE );
	team->setActive();
	TheAI->pathfinder()->addObjectToPathfindMap( obj );

	Zombie z;
	z.id = obj->getID();
	z.reached = FALSE;
	z.young = -1;
	m_zombies.push_back( z );
	++m_spawned;
	if( m_spawned == 1 )
		DEBUG_LOG(( "HEADLESS APOCALYPSE: frame %u first of the dead '%s' %d selectable %d\n", now,
								obj->getTemplate()->getName().str(), (Int)z.id, obj->isSelectable() ? 1 : 0 ));
	if( obj->getAI() != NULL )
	{
		// down the flow field when it reaches this far, else to the middle as before it was built
		Coord3D goal;
		if( !fieldGoal( obj, &goal ) )
			goal = m_centre;
		obj->getAI()->aiAttackMoveToPosition( &goal, NO_MAX_SHOTS_LIMIT, CMD_FROM_AI );
	}
	return obj;
}

//-------------------------------------------------------------------------------------------------
/// the gate whose bearing from the middle lies nearest that point's
Int ApocalypseDirector::sectorOf( Real x, Real y ) const
{
	const Real dx = x - m_centre.x, dy = y - m_centre.y;
	Int best = 0;
	Real bestDot = -1.0e30f;
	for( Int g = 0; g < (Int)m_spawns.size(); ++g )
	{
		const Real gx = m_spawns[ g ].x - m_centre.x, gy = m_spawns[ g ].y - m_centre.y;
		const Real length = sqrtf( gx * gx + gy * gy );
		const Real dot = length > 0.0f ? ( dx * gx + dy * gy ) / length : 0.0f;
		if( dot > bestDot )
		{
			bestDot = dot;
			best = g;
		}
	}
	return best;
}

//-------------------------------------------------------------------------------------------------
/** The front pick (the Hounds): the gate before the start of the living seat with the least defence,
	  the build cost of that seat's living structures that carry a damaging weapon within
	  APOCALYPSE_BASE_REACH of its start, summed in object list order in whole credits; ties to the lower
	  gate.  The map's pass N faces Player_N_Start's lobe, so a start's gate is its index.  -1 when no
	  living seat holds a start the map has a gate for.  Worked out when the pack comes, nothing kept. */
Int ApocalypseDirector::weakestFront( UnsignedInt now, const char *who ) const
{
	const Int gates = (Int)m_spawns.size();
	Int defence[ APOCALYPSE_SPAWNS_MOST ];
	Coord3D start[ APOCALYPSE_SPAWNS_MOST ];
	const Player *holder[ APOCALYPSE_SPAWNS_MOST ];
	UnsignedInt fronts = 0;
	for( Int g = 0; g < gates; ++g )
	{
		defence[ g ] = 0;
		start[ g ].zero();
		holder[ g ] = NULL;
	}
	for( Int i = 0; i < ThePlayerList->getPlayerCount(); ++i )
	{
		Player *player = ThePlayerList->getNthPlayer( i );
		const Int s = isSeat( player ) && player->hasAnyObjects() ? player->getMpStartIndex() : -1;
		if( s < 0 || s >= gates || ( fronts & ( 1u << s ) ) )
			continue;
		AsciiString name;
		name.format( "Player_%d_Start", s + 1 );
		const Waypoint *wp = TheTerrainLogic->getWaypointByName( name );
		if( wp == NULL )
			continue;
		start[ s ] = *wp->getLocation();
		holder[ s ] = player;
		fronts |= 1u << s;
	}
	if( fronts == 0 )
		return -1;

	const Real reach2 = (Real)APOCALYPSE_BASE_REACH * (Real)APOCALYPSE_BASE_REACH;
	for( Object *obj = TheGameLogic->getFirstObject(); obj; obj = obj->getNextObject() )
	{
		if( obj->isEffectivelyDead() || !obj->isKindOf( KINDOF_STRUCTURE ) || !obj->hasAnyDamageWeapon() )
			continue;
		const Player *owner = obj->getControllingPlayer();
		for( Int g = 0; g < gates; ++g )
		{
			if( !( fronts & ( 1u << g ) ) || holder[ g ] != owner )
				continue;
			const Real dx = obj->getPosition()->x - start[ g ].x, dy = obj->getPosition()->y - start[ g ].y;
			if( dx * dx + dy * dy <= reach2 )
				defence[ g ] += obj->getTemplate()->friend_getBuildCost();
		}
	}

	const Int best = Apocalypse_weakestGate( defence, fronts, gates );
	AsciiString others;
	for( Int g = 0; g < gates; ++g )
	{
		if( g == best || !( fronts & ( 1u << g ) ) )
			continue;
		AsciiString one;
		one.format( " gate %d %d", g + 1, defence[ g ] );
		others.concat( one );
	}
	DEBUG_LOG(( "HEADLESS APOCALYPSE: frame %u '%s' to gate %d, defence %d (others:%s)\n", now, who, best + 1,
							best >= 0 ? defence[ best ] : 0, others.isEmpty() ? " none" : others.str() ));
	return best;
}

//-------------------------------------------------------------------------------------------------
Bool ApocalypseDirector::isWarnedCell( Int cellX, Int cellY ) const
{
	if( m_warnSectors == 0 || ThePartitionManager == NULL )
		return FALSE;
	Real x, y;
	ThePartitionManager->getCellCenterPos( cellX, cellY, x, y );
	return ( m_warnSectors & ( 1u << sectorOf( x, y ) ) ) != 0;
}

//-------------------------------------------------------------------------------------------------
/// the gate of the n-th set bit (from 0) of a gate mask, in gate order; 0 when the mask has fewer
static Int nthGate( UnsignedInt mask, Int n )
{
	for( Int g = 0; g < APOCALYPSE_SPAWNS_MOST; ++g )
		if( ( mask & ( 1u << g ) ) && n-- == 0 )
			return g;
	return 0;
}

//-------------------------------------------------------------------------------------------------
/** The next wave picks its gates, the phase's count of them from the logic stream, and they glow for
	  APOCALYPSE_TELEGRAPH_SECONDS before it comes. */
void ApocalypseDirector::gatherWave( UnsignedInt now )
{
	const ApocalypsePhaseRules &rules = Apocalypse_phaseRules( m_phase );
	const Int gates = (Int)m_spawns.size();
	Int want = GameLogicRandomValue( rules.sectorsLeast, rules.sectorsMost < gates ? rules.sectorsMost : gates );
	// the peak comes at every gate the phase allows, by its shape when it is due
	const Bool peak = Apocalypse_waveShapePercent( m_phase, m_nextWaveFrame > m_phaseStartFrame ? m_nextWaveFrame - m_phaseStartFrame : 0,
		rules.seconds * gameSecond(), Apocalypse_isBloodMoon( m_phase, m_cycle ) ) >= APOCALYPSE_PEAK_PERCENT;
	if( peak )
		want = rules.sectorsMost;
	if( want > gates )
		want = gates;
	if( want < 1 )
		want = 1;
	UnsignedInt mask = 0;
	AsciiString names;
	/* Co-op: the map's pass N faces Player_N_Start's lobe, the passes past the starts are seams between two
		lobes.  The fronts are the passes of the starts a seat holds, so a player alone on start 2 is not
		left watching the waves walk into lobe 1; a start nobody holds is a seam like the rest.  The first
		gate is the next front in turn, in gate order, so every seat is hit; every third wave it is a seam
		instead, which two neighbours must hold together. */
	{
		UnsignedInt fronts = 0;
		for( Int i = 0; i < ThePlayerList->getPlayerCount(); ++i )
		{
			Player *player = ThePlayerList->getNthPlayer( i );
			const Int start = isSeat( player ) ? player->getMpStartIndex() : -1;
			if( start >= 0 && start < gates )
				fronts |= 1u << start;
		}
		// no seat on a start the map knows: the first passes, one a seat, as before
		for( Int g = 0; fronts == 0 && g < m_seats && g < gates; ++g )
			fronts |= 1u << g;
		Int frontCount = 0;
		for( Int g = 0; g < gates; ++g )
			if( fronts & ( 1u << g ) )
				++frontCount;
		const UnsignedInt allGates = gates >= 32 ? 0xFFFFFFFFu : ( 1u << gates ) - 1;
		Int first = frontCount > 0 ? nthGate( fronts, (Int)( m_frontTurn % (UnsignedInt)frontCount ) ) : 0;
		if( gates > frontCount && m_wave % 3 == 2 )
			first = nthGate( allGates & ~fronts, GameLogicRandomValue( 0, gates - frontCount - 1 ) );
		else
			++m_frontTurn;
		mask |= 1u << first;
		names.format( " %d", first + 1 );
	}
	for( Int taken = 1; taken < want; ++taken )
	{
		Int pick = GameLogicRandomValue( 0, gates - 1 - taken );
		for( Int g = 0; g < gates; ++g )
			if( !( mask & ( 1u << g ) ) && pick-- == 0 )
			{
				mask |= 1u << g;
				AsciiString one;
				one.format( " %d", g + 1 );
				names.concat( one );
				break;
			}
	}
	m_warnSectors = mask;
	DEBUG_LOG(( "HEADLESS APOCALYPSE: frame %u %s wave %u gathers at gate%s%s, due at frame %u%s\n", now,
							Apocalypse_phaseName( m_phase ), m_wave + 1, want > 1 ? "s" : "", names.str(), m_nextWaveFrame, peak ? ", the peak" : "" ));
	LogicThread::callAfterTick( []() { if( ThePartitionManager ) ThePartitionManager->redrawShroudForLocalPlayer(); } );
}

//-------------------------------------------------------------------------------------------------
/** The gathered wave comes: judged against the last one first (the adaptive share), then paid for in
	  threat points that come out as one mass at its gates.  Its shape sizes it and the gap after it, its
	  theme (one draw) leans its roll, and its scatter seed (one draw) lays out its spots in the fog. */
void ApocalypseDirector::sendWave( UnsignedInt now )
{
	logScatter( now );
	if( m_warnSectors == 0 )
		gatherWave( now );
	if( m_wave > 0 )
	{
		// the dead now walk at the nearest structure and a few always get there: a wave reached the bases
		// when a band of them did, as one zombie walking in to the middle used to be rare
		const Bool reached = m_reached >= m_reachedAtWave + APOCALYPSE_REACHED_LEAST;
		const Bool cleared = m_pending <= 0 && (Int)m_zombies.size() <= m_seats * 2;
		m_adapt = Apocalypse_adapt( m_adapt, reached, cleared );
	}
	++m_wave;
	const ApocalypsePhaseRules &rules = Apocalypse_phaseRules( m_phase );
	const Int shape = Apocalypse_waveShapePercent( m_phase, now - m_phaseStartFrame, rules.seconds * gameSecond(), Apocalypse_isBloodMoon( m_phase, m_cycle ) );
	Int points = Apocalypse_wavePoints( m_wave, m_seats, m_phase, m_cycle, m_adapt ) * shape / 100;
	if( points > APOCALYPSE_WAVE_MOST )
		points = APOCALYPSE_WAVE_MOST;
	if( points < 1 )
		points = 1;
	m_pending += points;
	m_waveTheme = Apocalypse_waveTheme( m_phase, m_cycle, shape, m_waveTheme, GameLogicRandomValue( 0, 999 ) );
	m_waveSeed = (UnsignedInt)GameLogicRandomValue( 0, 0x3FFFFFFF );
	m_waveShapeLogged = shape;
	// each wave brings its own room for elites and flyers, a Blood Moon twice the elites, a sky wave twice
	// the flyers (half its points at most); what the last one left unused goes
	m_eliteRoom = seatsAlive() * ( Apocalypse_isBloodMoon( m_phase, m_cycle ) ? 2 : 1 );
	m_flyerRoom = points * Apocalypse_flyerSharePercent( m_phase, m_cycle ) / 100;
	if( m_waveTheme == APOCALYPSE_THEME_SKY )
		m_flyerRoom = min( m_flyerRoom * 2, points / 2 );
	m_waveSectors = m_warnSectors;
	m_warnSectors = 0;
	m_reachedAtWave = m_reached;
	// a big wave leaves a longer breath after it, a small one less
	m_nextWaveFrame = now + (UnsignedInt)rules.waveSeconds * gameSecond() * (UnsignedInt)( 100 + shape ) / 200u;
	DEBUG_LOG(( "HEADLESS APOCALYPSE: frame %u %s wave %u of cycle %u brings %d points (shape %d%%, %s) at %d%% share, rank %d; %d walking, %d points to come, next at frame %u; room for %d elites, %d flyer points\n",
							now, Apocalypse_phaseName( m_phase ), m_wave, m_cycle, points, shape, Apocalypse_themeName( m_waveTheme ), m_adapt, Apocalypse_cycleRank( m_cycle ),
							(Int)m_zombies.size(), m_pending, m_nextWaveFrame, m_eliteRoom, m_flyerRoom ));
	LogicThread::callAfterTick( []() { if( ThePartitionManager ) ThePartitionManager->redrawShroudForLocalPlayer(); } );
	sendBosses( now, shape >= APOCALYPSE_PEAK_PERCENT );
}

//-------------------------------------------------------------------------------------------------
/** The spots the last wave came out on, measured gate by gate for the log: the mean distance to the
	  nearest other against an even random spread over the gate's box (Clark-Evans R: 1 random, under 1
	  clumped, toward 2 a lattice), how many stand in a clump (three others within half the even
	  spacing), how many alone (none within one and a half), and how many share an exact row or column
	  with their nearest, the tell of a grid.  A gate with fewer than 8 is left out.  Log only: the spots
	  are no saved state and nothing decides by them. */
void ApocalypseDirector::logScatter( UnsignedInt now )
{
	const Int n = (Int)m_scatterLog.size();
	std::vector<Int> gateOf( n );
	for( Int i = 0; i < n; ++i )
		gateOf[ i ] = sectorOf( m_scatterLog[ i ].x, m_scatterLog[ i ].y );
	Real sumNearest = 0.0f, sumEven = 0.0f;
	Int counted = 0, clumped = 0, alone = 0, lined = 0;
	for( Int g = 0; g < (Int)m_spawns.size(); ++g )
	{
		Int here = 0;
		Real loX = 1.0e30f, hiX = -1.0e30f, loY = 1.0e30f, hiY = -1.0e30f;
		for( Int i = 0; i < n; ++i )
		{
			if( gateOf[ i ] != g )
				continue;
			++here;
			loX = min( loX, m_scatterLog[ i ].x );
			hiX = max( hiX, m_scatterLog[ i ].x );
			loY = min( loY, m_scatterLog[ i ].y );
			hiY = max( hiY, m_scatterLog[ i ].y );
		}
		if( here < 8 )
			continue;
		const Real spacing2 = max( 1.0f, ( hiX - loX ) * ( hiY - loY ) ) / (Real)here;
		for( Int i = 0; i < n; ++i )
		{
			if( gateOf[ i ] != g )
				continue;
			Real best = 1.0e30f;
			Int nearest = i, crowd = 0;
			Bool company = FALSE;
			for( Int j = 0; j < n; ++j )
			{
				if( j == i || gateOf[ j ] != g )
					continue;
				const Real dx = m_scatterLog[ j ].x - m_scatterLog[ i ].x, dy = m_scatterLog[ j ].y - m_scatterLog[ i ].y;
				const Real d2 = dx * dx + dy * dy;
				if( d2 < best )
				{
					best = d2;
					nearest = j;
				}
				if( d2 <= 0.25f * spacing2 )
					++crowd;
				if( d2 <= 2.25f * spacing2 )
					company = TRUE;
			}
			++counted;
			sumNearest += sqrtf( best );
			sumEven += 0.5f * sqrtf( spacing2 );
			if( crowd >= 3 )
				++clumped;
			if( !company )
				++alone;
			if( fabs( m_scatterLog[ nearest ].x - m_scatterLog[ i ].x ) < 0.01f || fabs( m_scatterLog[ nearest ].y - m_scatterLog[ i ].y ) < 0.01f )
				++lined;
		}
	}
	if( counted > 0 )
		DEBUG_LOG(( "HEADLESS APOCALYPSE SCATTER: frame %u wave %u (shape %d%%, %s) %d out, %d at a spawn waypoint, %d measured; nearest %.1f, R %.2f; in clumps %d%%, alone %d%%, on a neighbour's line %d%%\n",
								now, m_wave, m_waveShapeLogged, Apocalypse_themeName( m_waveTheme ), n, m_scatterFallbacks, counted,
								sumNearest / (Real)counted, sumEven > 0.0f ? sumNearest / sumEven : 0.0f,
								clumped * 100 / counted, alone * 100 / counted, lined * 100 / counted ));
	m_scatterLog.clear();
	m_scatterFallbacks = 0;
}

//-------------------------------------------------------------------------------------------------
/** A boss's health times Apocalypse_bossHealthPercent for the seats alive now, the share of it the boss
	  has kept staying the same.  Read once when it comes and once when the Plague King stands again. */
void ApocalypseDirector::scaleBoss( Object *boss ) const
{
	BodyModuleInterface *body = boss->getBodyModule();
	const Int percent = Apocalypse_bossHealthPercent( seatsAlive() );
	if( body != NULL && percent != 100 )
		body->setMaxHealth( body->getMaxHealth() * (Real)percent / 100.0f, PRESERVE_RATIO );
}

//-------------------------------------------------------------------------------------------------
/** The peak wave of a Blood Moon night brings its boss with it (two from APOCALYPSE_BOSS_PAIR_NIGHT),
	  outside the wave's points and the cap, in the fog before the wave's first gate: the night's climax,
	  not its opening (the Blood Moon's night shape peaks from a third in to well past two thirds, wider
	  than the longest gap between two of its waves, so one always lands on it).  m_bossNight keeps a
	  load or the night's later waves from sending another.  The pair's draw is the only one, and it is
	  made only on such a night. */
void ApocalypseDirector::sendBosses( UnsignedInt now, Bool peak )
{
	const UnsignedInt night = m_cycle + 1;
	if( !Apocalypse_isBloodMoon( m_phase, m_cycle ) || m_phase != APOCALYPSE_NIGHT || !peak || m_bossNight >= night )
		return;
	m_bossNight = night;
	const Int skip = night >= APOCALYPSE_BOSS_PAIR_NIGHT ? GameLogicRandomValue( 0, 2 ) : 0;
	Int kinds[ 2 ];
	const Int count = Apocalypse_bossesOfNight( night, skip, kinds );
	Int gate = -1;
	for( Int g = 0; g < (Int)m_spawns.size() && gate < 0; ++g )
		if( m_waveSectors & ( 1u << g ) )
			gate = g;
	if( gate < 0 )
		gate = 0;
	for( Int b = 0; b < count; ++b )
	{
		const Int kind = kinds[ b ];
		if( m_kinds[ kind ] == NULL )
		{
			DEBUG_LOG(( "HEADLESS APOCALYPSE: frame %u night %u's boss '%s' is not in the data, none comes\n", now, night, Apocalypse_kind( kind ).name ));
			continue;
		}
		Team *team = ThePlayerList->getNthPlayer( m_zombiePlayer )->getDefaultTeam();
		Object *obj = TheThingFactory->newObject( m_kinds[ kind ], team );
		if( obj == NULL )
			continue;
		Coord3D pos;
		if( !m_hasRing || !ringSpot( &pos, obj, gate ) )
		{
			pos = m_spawns[ gate ];
			pos.z = TheTerrainLogic->getGroundHeight( pos.x, pos.y );
		}
		obj = makeZombie( kind, pos, now, obj );
		if( obj == NULL )
			continue;
		scaleBoss( obj );
		// the Colossus goes for the dearest structure at once, not down the field (sendOn)
		if( kind == m_colossusKind && obj->getAI() != NULL )
		{
			obj->getAI()->aiIdle( CMD_FROM_AI );
			sendOn( obj );
		}
		const BodyModuleInterface *body = obj->getBodyModule();
		DEBUG_LOG(( "HEADLESS APOCALYPSE: frame %u Blood Moon night %u boss '%s' %d comes before gate %d at (%.0f,%.0f), health %.0f for %d seats\n", now,
								night, m_kinds[ kind ]->getName().str(), (Int)obj->getID(), gate + 1, pos.x, pos.y,
								body != NULL ? body->getMaxHealth() : 0.0f, seatsAlive() ));
		// the players hear of it; the client's, nothing comes back.  A copy of its own, so the template's
		// shared string is never counted on two threads
		UnicodeString name;
		name.set( m_kinds[ kind ]->getDisplayName().str() );
		LogicThread::callAfterTick( [name]()
		{
			if( TheInGameUI != NULL && !TheGlobalData->m_headless )
				TheInGameUI->message( "GUI:ApocalypseBossComing", name.str() );
		} );
	}
}

//-------------------------------------------------------------------------------------------------
/** The Plague King.  His UndeadBody turns his first death into a fall (BattleBusSlowDeathBehavior with
	  no throw), which leaves him held for good; the first look that finds him so lets him stand
	  APOCALYPSE_KING_RISE_SECONDS later with his second life's health scaled for the seats, the hold's
	  end frame kept by the object itself.  And the dead (any kind but a boss) found fallen within
	  APOCALYPSE_KING_REACH of a living King, their bodies still lying, rise as walkers
	  APOCALYPSE_INFECT_SECONDS later through m_rises, under the cap like the fallen soldiers.  Walked in
	  m_zombies order; m_rises kept sorted by ObjectID. */
void ApocalypseDirector::watchTheKings( UnsignedInt now )
{
	if( m_kingKind < 0 || m_kinds[ m_kingKind ] == NULL )
		return;
	std::vector<Object *> kings;
	for( UnsignedInt i = 0; i < m_zombies.size(); ++i )
	{
		Object *obj = TheGameLogic->findObjectByID( m_zombies[ i ].id );
		if( obj == NULL || obj->isEffectivelyDead() || !obj->getTemplate()->isEquivalentTo( m_kinds[ m_kingKind ] ) )
			continue;
		kings.push_back( obj );
		if( obj->isDisabledByType( DISABLED_HELD ) && obj->getDisabledUntil( DISABLED_HELD ) >= FOREVER )
		{
			// the raising's staff-up (SPECIAL_CHEERING, ZBKING_ATB) gives way to his fall
			obj->clearSpecialModelConditionStates();
			const UnsignedInt stands = now + APOCALYPSE_KING_RISE_SECONDS * gameSecond();
			obj->setDisabledUntil( DISABLED_HELD, stands );
			scaleBoss( obj );
			DEBUG_LOG(( "HEADLESS APOCALYPSE: frame %u the Plague King %d falls and stands again at frame %u, health %.0f\n", now,
									(Int)obj->getID(), stands, obj->getBodyModule() != NULL ? obj->getBodyModule()->getMaxHealth() : 0.0f ));
		}
	}
	if( kings.empty() )
		return;
	const Real reach2 = (Real)APOCALYPSE_KING_REACH * (Real)APOCALYPSE_KING_REACH;
	for( UnsignedInt i = 0; i < m_zombies.size(); ++i )
	{
		const Object *obj = TheGameLogic->findObjectByID( m_zombies[ i ].id );
		if( obj == NULL || !obj->isEffectivelyDead() )
			continue;
		const Int kind = kindOf( obj );
		if( kind >= 0 && ( Apocalypse_kind( kind ).flags & APOCALYPSE_KIND_BOSS ) )
			continue;
		Object *byKing = NULL;
		for( UnsignedInt k = 0; k < kings.size() && byKing == NULL; ++k )
		{
			const Real dx = obj->getPosition()->x - kings[ k ]->getPosition()->x, dy = obj->getPosition()->y - kings[ k ]->getPosition()->y;
			if( dx * dx + dy * dy <= reach2 )
				byKing = kings[ k ];
		}
		if( byKing == NULL )
			continue;
		Rise r;
		r.victim = obj->getID();
		std::vector<Rise>::iterator at = std::lower_bound( m_rises.begin(), m_rises.end(), r,
			[]( const Rise &a, const Rise &b ) { return a.victim < b.victim; } );
		if( at != m_rises.end() && at->victim == r.victim )
			continue;
		r.where = *obj->getPosition();
		r.frame = now + APOCALYPSE_INFECT_SECONDS * gameSecond();
		m_rises.insert( at, r );
		// he raises it, staff up (ZBKING_ATB), until it stands; held in his fall he does not
		if( !byKing->isDisabledByType( DISABLED_HELD ) )
			byKing->setSpecialModelConditionState( MODELCONDITION_SPECIAL_CHEERING, r.frame - now );
		++m_kingRaisesLogged;
		if( m_kingRaisesLogged <= 10 || m_kingRaisesLogged % 50 == 0 )
			DEBUG_LOG(( "HEADLESS APOCALYPSE: frame %u '%s' %d fell near the Plague King and rises at frame %u, %u so far\n", now,
									obj->getTemplate()->getName().str(), (Int)r.victim, r.frame, m_kingRaisesLogged ));
	}
}

//-------------------------------------------------------------------------------------------------
/** The dead that walk off the list (a Broodmother's six, a Carrier's eight, a flock's members, anything a
	  host made that is its master's to move): they walk and fight, so the alive cap counts them here.  The
	  dead player's objects in its own iteration order, each time it is asked, nothing kept.  At night 6 on
	  four seats they had stood 252 over a cap of 700, and counting the listed hosts' slaves alone still
	  left 174 (2026-11-26). */
struct OffListCount
{
	const std::vector<ObjectID> *listed;
	Int count;
};

static void countOffList( Object *obj, void *userData )
{
	OffListCount *c = (OffListCount *)userData;
	if( obj->isEffectivelyDead() || obj->getAI() == NULL || obj->isKindOf( KINDOF_STRUCTURE ) || obj->isKindOf( KINDOF_PROJECTILE )
			|| !obj->getTemplate()->getName().startsWith( "ApocalypseZombie" ) )
		return;
	if( !std::binary_search( c->listed->begin(), c->listed->end(), obj->getID() ) )
		++c->count;
}

Int ApocalypseDirector::deadOffTheList( void ) const
{
	const Player *dead = ThePlayerList->getNthPlayer( m_zombiePlayer );
	if( dead == NULL )
		return 0;
	std::vector<ObjectID> listed;
	listed.reserve( m_zombies.size() );
	for( UnsignedInt i = 0; i < m_zombies.size(); ++i )
		listed.push_back( m_zombies[ i ].id );
	std::sort( listed.begin(), listed.end() );
	OffListCount c;
	c.listed = &listed;
	c.count = 0;
	dead->iterateObjects( countOffList, &c );
	return c.count;
}

//-------------------------------------------------------------------------------------------------
/** Soldiers the dead killed rise as the dead: a body of a seat's infantry whose last hurt came from
	  the dead player is noted while it lies, and APOCALYPSE_INFECT_SECONDS later an ApocalypseZombie
	  stands where it fell, if the cap has room (it waits for room otherwise).  The same walk adopts the
	  dead's strays.  Walked in object list order; m_rises kept sorted by the victim's ObjectID. */
void ApocalypseDirector::watchTheFallen( UnsignedInt now )
{
	const Player *zombies = ThePlayerList->getNthPlayer( m_zombiePlayer );
	const Player *neutral = ThePlayerList->getNeutralPlayer();
	const PlayerMaskType zombieMask = zombies->getPlayerMask();
	const Int cap = Apocalypse_phaseRules( m_phase ).aliveCap - ( m_rises.empty() ? 0 : deadOffTheList() );

	// those due rise; a risen one is kept while his body lies, so it is not noted twice
	std::vector<Rise> kept;
	for( UnsignedInt i = 0; i < m_rises.size(); ++i )
	{
		Rise r = m_rises[ i ];
		if( r.frame != 0 && now >= r.frame && (Int)m_zombies.size() < cap )
		{
			const Object *made = makeZombie( APOCALYPSE_KIND_WALKER, r.where, now );
			if( made != NULL )
			{
				++m_infected;
				DEBUG_LOG(( "HEADLESS APOCALYPSE: frame %u fallen %d rises as zombie %d at (%.0f,%.0f), %u risen\n", now,
										(Int)r.victim, (Int)made->getID(), r.where.x, r.where.y, m_infected ));
			}
			r.frame = 0;
		}
		if( r.frame == 0 && TheGameLogic->findObjectByID( r.victim ) == NULL )
			continue;		// risen, and the body is gone: nothing left to tell apart
		kept.push_back( r );
	}
	m_rises.swap( kept );

	// who is on the list now, sorted, to tell a stray by: scratch for this pass, nothing kept
	std::vector<ObjectID> listed;
	listed.reserve( m_zombies.size() );
	for( UnsignedInt i = 0; i < m_zombies.size(); ++i )
		listed.push_back( m_zombies[ i ].id );
	std::sort( listed.begin(), listed.end() );

	static const NameKeyType key_SlavedUpdate = NAMEKEY( "SlavedUpdate" );
	static const NameKeyType key_MobMemberSlavedUpdate = NAMEKEY( "MobMemberSlavedUpdate" );
	for( Object *obj = TheGameLogic->getFirstObject(); obj; obj = obj->getNextObject() )
	{
		/* A stray: something of the dead's that walks and is not on the list (a spawn line's, what a
			 death leaves behind).  It goes on the end of the list, counted under the cap and sent on.  A
			 slave is its master's to move; a thrown spear has an AI too (MissileAIUpdate) and is no stray.
			 Only the dead's own: a roster kind, something one of them made (its producer on the list, or a
			 body of a roster kind still lying), or a vehicle a Leech took (HIJACKED; its producer is the
			 seat's factory), so a civilian placed on the map never marches. */
		const Object *maker = TheGameLogic->findObjectByID( obj->getProducerID() );
		if( !obj->isEffectivelyDead() && obj->getControllingPlayer() == zombies && obj->getAI() != NULL
				&& obj->getContainedBy() == NULL && !obj->isKindOf( KINDOF_STRUCTURE ) && !obj->isKindOf( KINDOF_PROJECTILE )
				&& obj->findUpdateModule( key_SlavedUpdate ) == NULL && obj->findUpdateModule( key_MobMemberSlavedUpdate ) == NULL
				&& !std::binary_search( listed.begin(), listed.end(), obj->getID() )
				&& ( kindOf( obj ) >= 0 || obj->testStatus( OBJECT_STATUS_HIJACKED ) || std::binary_search( listed.begin(), listed.end(), obj->getProducerID() )
					|| ( maker != NULL && maker->getControllingPlayer() == zombies && kindOf( maker ) >= 0 ) ) )
		{
			Zombie z;
			z.id = obj->getID();
			z.reached = FALSE;
			z.young = -1;
			m_zombies.push_back( z );
			++m_straysLogged;
			if( m_straysLogged <= 10 || m_straysLogged % 50 == 0 )
				DEBUG_LOG(( "HEADLESS APOCALYPSE: frame %u stray '%s' %d joins the dead, %u so far\n", now,
										obj->getTemplate()->getName().str(), (Int)z.id, m_straysLogged ));
			// a boss placed by a spawn line sizes for the seats as the Blood Moon's does
			const Int strayKind = kindOf( obj );
			if( strayKind >= 0 && ( Apocalypse_kind( strayKind ).flags & APOCALYPSE_KIND_BOSS ) )
				scaleBoss( obj );
			sendOn( obj );
			continue;
		}
		if( !obj->isEffectivelyDead() || !obj->isKindOf( KINDOF_INFANTRY ) )
			continue;
		const Player *owner = obj->getControllingPlayer();
		if( owner == NULL || owner == zombies || owner == neutral )
			continue;
		const BodyModuleInterface *body = obj->getBodyModule();
		const DamageInfo *last = body != NULL ? body->getLastDamageInfo() : NULL;
		if( last == NULL || ( last->in.m_sourcePlayerMask & zombieMask ) == 0 )
			continue;
		Rise r;
		r.victim = obj->getID();
		std::vector<Rise>::iterator at = std::lower_bound( m_rises.begin(), m_rises.end(), r,
			[]( const Rise &a, const Rise &b ) { return a.victim < b.victim; } );
		if( at != m_rises.end() && at->victim == r.victim )
			continue;
		r.where = *obj->getPosition();
		r.frame = now + APOCALYPSE_INFECT_SECONDS * gameSecond();
		m_rises.insert( at, r );
	}
}

//-------------------------------------------------------------------------------------------------
/** The dead in the fog, counted by heat block and handed to the client after the tick: a read of the
	  zombie list in its order, no random drawn and nothing kept, so the logic is the same with or
	  without anyone looking. */
void ApocalypseDirector::handHeat( void ) const
{
	const Int blocksX = ( ThePartitionManager->getCellCountX() + APOCALYPSE_HEAT_BLOCK - 1 ) / APOCALYPSE_HEAT_BLOCK;
	const Int blocksY = ( ThePartitionManager->getCellCountY() + APOCALYPSE_HEAT_BLOCK - 1 ) / APOCALYPSE_HEAT_BLOCK;
	std::vector<UnsignedByte> heat( blocksX * blocksY, 0 );
	for( UnsignedInt i = 0; i < m_zombies.size(); ++i )
	{
		const Object *obj = TheGameLogic->findObjectByID( m_zombies[ i ].id );
		if( obj == NULL || !inRing( obj->getPosition() ) )
			continue;
		Int cellX, cellY;
		ThePartitionManager->worldToCell( obj->getPosition()->x, obj->getPosition()->y, &cellX, &cellY );
		const Int block = ( cellY / APOCALYPSE_HEAT_BLOCK ) * blocksX + cellX / APOCALYPSE_HEAT_BLOCK;
		if( block >= 0 && block < (Int)heat.size() )
			heat[ block ] = (UnsignedByte)min( 255, (Int)heat[ block ] + APOCALYPSE_HEAT_PER_ZOMBIE );
	}
	LogicThread::callAfterTick( [heat, blocksX, blocksY]() { apocalypseApplyHeat( heat, blocksX, blocksY ); } );
}

//-------------------------------------------------------------------------------------------------
Bool ApocalypseDirector::inRing( const Coord3D *pos ) const
{
	if( !m_hasRing )
		return FALSE;
	Int cellX, cellY;
	ThePartitionManager->worldToCell( pos->x, pos->y, &cellX, &cellY );
	return ThePartitionManager->isCellAlwaysShrouded( cellX, cellY );
}

//-------------------------------------------------------------------------------------------------
/** A spot in the fog ring before that gate (anywhere in the ring for -1), off the map's edge and off the
	  clear area by APOCALYPSE_FOG_EDGE_IN, where the object can stand: round the gate's spawn waypoint,
	  a little in towards the middle, further out, and a quarter of its distance from the middle either
	  side.  The first APOCALYPSE_SCATTER_NOISE_TRIES spots drawn are kept by the wave's scatter noise
	  (Apocalypse_scatterKeepPercent of it, one more draw), so the dead stand in clumps with stragglers
	  between and no rows; the tries after take what falls.  Drawn from the logic stream, FALSE after
	  APOCALYPSE_FOG_TRIES misses. */
Bool ApocalypseDirector::ringSpot( Coord3D *pos, const Object *obj, Int sector )
{
	const Real in = (Real)APOCALYPSE_FOG_EDGE_IN;
	const AIUpdateInterface *ai = obj->getAI();
	for( Int tryNo = 0; tryNo < APOCALYPSE_FOG_TRIES; ++tryNo )
	{
		if( sector >= 0 && sector < (Int)m_spawns.size() )
		{
			const Real gx = m_spawns[ sector ].x - m_centre.x, gy = m_spawns[ sector ].y - m_centre.y;
			const Real reach = sqrtf( gx * gx + gy * gy );
			const Real ux = reach > 0.0f ? gx / reach : 1.0f, uy = reach > 0.0f ? gy / reach : 0.0f;
			// the fog's inner edge round the pass's spawn: a little in towards the clear ground, deeper out (the
			// far ones are the stragglers), wide either side
			const Real inward = GameLogicRandomValueReal( -0.18f * reach, 0.06f * reach );
			const Real aside = GameLogicRandomValueReal( -0.25f * reach, 0.25f * reach );
			pos->x = m_spawns[ sector ].x - ux * inward - uy * aside;
			pos->y = m_spawns[ sector ].y - uy * inward + ux * aside;
			if( pos->x < m_playLo.x + in || pos->x > m_playHi.x - in || pos->y < m_playLo.y + in || pos->y > m_playHi.y - in )
				continue;
		}
		else
		{
			pos->x = GameLogicRandomValueReal( m_playLo.x + in, m_playHi.x - in );
			pos->y = GameLogicRandomValueReal( m_playLo.y + in, m_playHi.y - in );
		}
		// thin ground keeps few: clumps where the noise is high, bare ground where it is low
		if( tryNo < APOCALYPSE_SCATTER_NOISE_TRIES
				&& GameLogicRandomValue( 0, 99 ) >= Apocalypse_scatterKeepPercent( Apocalypse_scatterNoise( pos->x, pos->y, m_waveSeed ) ) )
			continue;
		// in the fog, and as far from its edge as APOCALYPSE_FOG_EDGE_IN every way
		Coord3D probe = *pos;
		Bool inFog = inRing( pos );
		for( Int side = 0; side < 4 && inFog; ++side )
		{
			probe.x = pos->x + ( side == 0 ? in : side == 1 ? -in : 0.0f );
			probe.y = pos->y + ( side == 2 ? in : side == 3 ? -in : 0.0f );
			inFog = inRing( &probe );
		}
		if( !inFog )
			continue;
		pos->z = TheTerrainLogic->getGroundHeight( pos->x, pos->y );
		if( ai == NULL || TheAI->pathfinder()->validMovementPosition( obj->getCrusherLevel() > 0, obj->getLayer(), ai->getLocomotorSet(), pos ) )
			return TRUE;
	}
	return FALSE;
}

//-------------------------------------------------------------------------------------------------
/** Anything of the living (a unit of a seat or of anyone but the dead and the neutral map) that has
	  stood in the fog ring APOCALYPSE_FOG_GRACE_SECONDS dies there, save a power's plane from off the
	  map (isOffMapDelivery); what rides in one, a parachute and a crate are no unit of the ring's either.  Walked in object list order, the
	  stays kept sorted by ObjectID, so every machine kills the same thing on the same frame. */
void ApocalypseDirector::killInFog( UnsignedInt now )
{
	const UnsignedInt grace = APOCALYPSE_FOG_GRACE_SECONDS * gameSecond();
	const Player *neutral = ThePlayerList->getNeutralPlayer();
	std::vector<FogStay> stays;
	std::vector<Object *> doomed;
	for( Object *obj = TheGameLogic->getFirstObject(); obj; obj = obj->getNextObject() )
	{
		const Player *owner = obj->getControllingPlayer();
		if( owner == NULL || owner == neutral || owner->getPlayerIndex() == m_zombiePlayer || obj->isEffectivelyDead()
				|| obj->getContainedBy() != NULL
				|| !( obj->isKindOf( KINDOF_INFANTRY ) || obj->isKindOf( KINDOF_VEHICLE ) || obj->isKindOf( KINDOF_AIRCRAFT ) )
				|| !inRing( obj->getPosition() ) || isOffMapDelivery( obj ) )
			continue;
		FogStay stay;
		stay.id = obj->getID();
		stay.since = now;
		std::vector<FogStay>::const_iterator was = std::lower_bound( m_fogStays.begin(), m_fogStays.end(), stay,
			[]( const FogStay &a, const FogStay &b ) { return a.id < b.id; } );
		if( was != m_fogStays.end() && was->id == stay.id )
			stay.since = was->since;
		if( now - stay.since >= grace )
			doomed.push_back( obj );
		else
			stays.push_back( stay );
	}
	std::sort( stays.begin(), stays.end(), []( const FogStay &a, const FogStay &b ) { return a.id < b.id; } );
	m_fogStays.swap( stays );
	for( UnsignedInt i = 0; i < doomed.size(); ++i )
	{
		Object *obj = doomed[ i ];
		++m_fogKills;
		DEBUG_LOG(( "HEADLESS APOCALYPSE: frame %u '%s' %d of player %d at (%.0f,%.0f) died in the fog, %u so far\n", now,
								obj->getTemplate()->getName().str(), (Int)obj->getID(), obj->getControllingPlayer()->getPlayerIndex(),
								obj->getPosition()->x, obj->getPosition()->y, m_fogKills ));
		obj->kill();
	}
}

//-------------------------------------------------------------------------------------------------
void ApocalypseDirector::advancePhase( UnsignedInt now )
{
	const UnsignedInt length = Apocalypse_phaseRules( m_phase ).seconds * gameSecond();
	if( now < m_phaseStartFrame + length )
		return;
	m_phase = ( m_phase + 1 ) % APOCALYPSE_PHASES;
	m_phaseStartFrame = now;
	if( m_phase == APOCALYPSE_DAY )
		++m_cycle;
	// the night does not wait out the day's wave clock: its first wave is told now and comes a telegraph on
	if( m_phase == APOCALYPSE_NIGHT )
	{
		const UnsignedInt falls = now + APOCALYPSE_TELEGRAPH_SECONDS * gameSecond();
		if( m_nextWaveFrame > falls )
		{
			m_nextWaveFrame = falls;
			m_warnSectors = 0;		// what the day had gathered gives way to the night's own
		}
	}
	DEBUG_LOG(( "HEADLESS APOCALYPSE: phase %s%s, cycle %u, frame %u, next wave at frame %u, %d walking\n",
							Apocalypse_phaseName( m_phase ), Apocalypse_isBloodMoon( m_phase, m_cycle ) ? " (Blood Moon)" : "", m_cycle, now, m_nextWaveFrame, (Int)m_zombies.size() ));
}

//-------------------------------------------------------------------------------------------------
/** Hand the client the night amount after every tick.  The drawables' NIGHT state is a pure function of
	  the frame, set on every one of them after every tick, because the logic does read it back: a
	  model's launch and exit bones come from its drawable's current state (Drawable::
	  getProjectileLaunchOffset), so a drawable made since the last hand-out, or one restored from a
	  save, must not keep another state than the straight game gives it.  Only the retint waits for
	  the amount to move a step (m_lightLevel, which nothing in the logic reads). */
void ApocalypseDirector::handLight( UnsignedInt now )
{
	const UnsignedInt length = Apocalypse_phaseRules( m_phase ).seconds * gameSecond();
	const Real amount = Apocalypse_nightAmount( m_phase, now - m_phaseStartFrame, length );
	const Int level = (Int)( amount * (Real)APOCALYPSE_LIGHT_STEPS + 0.5f );
	const Bool lightsOn = level * 2 >= APOCALYPSE_LIGHT_STEPS;
	// the tables move a quarter as often as the amount: the ground's relight is a long frame
	const Bool relight = m_lightLevel < 0 || level / APOCALYPSE_LIGHT_TINT_EVERY != m_lightLevel / APOCALYPSE_LIGHT_TINT_EVERY;
	if( lightsOn != m_lightsOn )
		DEBUG_LOG(( "HEADLESS APOCALYPSE: frame %u lights %s\n", now, lightsOn ? "on" : "off" ));
	m_lightLevel = level;
	m_lightsOn = lightsOn;
	// the fog's rule: a Blood Moon's whole cycle, its dusk deepening and its dawn fading back (0 by day)
	const Bool bloodMoon = ( m_cycle + 1 ) % APOCALYPSE_BLOOD_MOON_EVERY == 0;
	LogicThread::callAfterTick( [level, relight, lightsOn, bloodMoon]() { apocalypseApplyLight( level, relight, lightsOn, bloodMoon ); } );
}

//-------------------------------------------------------------------------------------------------
void ApocalypseDirector::update( void )
{
	if( !m_active )
		return;
	const UnsignedInt now = TheGameLogic->getFrame();
	const UnsignedInt second = gameSecond();

	advancePhase( now );
	stepField();
	const ApocalypsePhaseRules &rules = Apocalypse_phaseRules( m_phase );

	// the Plague King's fall, and the dead fallen near him noted to rise, before the walk below takes them off the list
	watchTheKings( now );

	/* A Broodmother lays: one more young than at the last look plays her laying clip (SPECIAL_CHEERING,
		 ZBBROD_SPA, 45 frames).  The count lives on her list entry and is not saved; after a load the first
		 look takes it as it finds it, so no clip plays for young laid before the save. */
	if( m_broodKind >= 0 && m_kinds[ m_broodKind ] != NULL )
	{
		for( UnsignedInt i = 0; i < m_zombies.size(); ++i )
		{
			Object *obj = TheGameLogic->findObjectByID( m_zombies[ i ].id );
			if( obj == NULL || obj->isEffectivelyDead() || !obj->getTemplate()->isEquivalentTo( m_kinds[ m_broodKind ] ) )
				continue;
			const SpawnBehaviorInterface *spawn = obj->getSpawnBehaviorInterface();
			const Int young = spawn != NULL ? spawn->getSlaveCount() : 0;
			if( m_zombies[ i ].young >= 0 && young > m_zombies[ i ].young )
				obj->setSpecialModelConditionState( MODELCONDITION_SPECIAL_CHEERING, 45 );
			m_zombies[ i ].young = young;
		}
	}

	// the dead come off the list, the living are checked for the middle and, now and then, sent on
	const UnsignedInt retarget = APOCALYPSE_RETARGET_SECONDS * second;
	UnsignedInt kept = 0;
	for( UnsignedInt i = 0; i < m_zombies.size(); ++i )
	{
		Zombie z = m_zombies[ i ];
		Object *obj = TheGameLogic->findObjectByID( z.id );
		if( obj == NULL || obj->isEffectivelyDead() )
		{
			++m_kills;
			continue;
		}
		if( !z.reached && hasReachedBases( obj ) )
		{
			z.reached = TRUE;
			++m_reached;
			if( m_reached <= 3 || m_reached % 50 == 0 )
				DEBUG_LOG(( "HEADLESS APOCALYPSE: frame %u zombie %d reached the bases, %u have\n", now, (Int)z.id, m_reached ));
		}
		if( ( now + (UnsignedInt)z.id ) % retarget == 0 )
			sendOn( obj );
		m_zombies[ kept++ ] = z;
	}
	m_zombies.resize( kept );

	// the next wave gathers at its gates a telegraph ahead, then comes as one mass
	if( !m_kinds.empty() && m_kinds[ APOCALYPSE_KIND_WALKER ] != NULL )
	{
		if( m_warnSectors == 0 && now + APOCALYPSE_TELEGRAPH_SECONDS * second >= m_nextWaveFrame )
			gatherWave( now );
		if( now >= m_nextWaveFrame )
			sendWave( now );
	}
	const Int slaves = m_pending > 0 ? deadOffTheList() : 0;
	for( Int out = 0; out < rules.spawnsPerFrame && m_pending > 0 && (Int)m_zombies.size() + slaves < rules.aliveCap; )
	{
		const Int made = spawnOne( now );
		out += made > 0 ? made : 1;
	}

	// the fallen rise, five times a game second
	if( now % max( second / 5, 1U ) == 0 )
		watchTheFallen( now );

	if( m_hasRing && now % ( APOCALYPSE_HEAT_SECONDS * second ) == 0 )
		handHeat();

	handLight( now );

	// the ring takes what lingers in it, twice a game second
	if( m_hasRing && now % max( second / 2, 1U ) == 0 )
		killInFog( now );

	// the fog ring's and the smoke's red, an eighth of the way at a time (each step redraws the shroud)
	{
		const UnsignedInt length = Apocalypse_phaseRules( m_phase ).seconds * second;
		const Real amount = Apocalypse_nightAmount( m_phase, now - m_phaseStartFrame, length );
		const Int level = (Int)( amount * (Real)APOCALYPSE_LIGHT_STEPS + 0.5f ) / APOCALYPSE_LIGHT_TINT_EVERY * APOCALYPSE_LIGHT_TINT_EVERY;
		// a Blood Moon's cycle tints deeper toward its night, its dawn fading back from the same red (by day the amount is 0)
		const Bool bloodMoon = ( m_cycle + 1 ) % APOCALYPSE_BLOOD_MOON_EVERY == 0;
		const Int fogKey = level + ( bloodMoon ? APOCALYPSE_LIGHT_STEPS + 1 : 0 );
		if( fogKey != m_fogLevel )
		{
			m_fogLevel = fogKey;
			LogicThread::callAfterTick( [level, bloodMoon]() { apocalypseApplyFog( level, bloodMoon ); } );
		}
	}

	if( now % ( APOCALYPSE_LOG_SECONDS * second ) == 0 )
	{
		const Int standing = seatsAlive();
		DEBUG_LOG(( "HEADLESS APOCALYPSE: frame %u %s cycle %u wave %u walking %d spawned %u kills %u reached %u to come %d seats standing %d of %d, fog took %u, risen %u, share %d%%\n",
								now, Apocalypse_phaseName( m_phase ), m_cycle, m_wave, (Int)m_zombies.size(), m_spawned, m_kills, m_reached,
								m_pending, standing, m_seats, m_fogKills, m_infected, m_adapt ));
		DEBUG_LOG(( "HEADLESS APOCALYPSE FIELD: frame %u repairs %u%s, routes %u, refused %u, cells %u, waiting changes %d\n",
								now, m_fieldRepairs, m_field.isRepairing() ? " (one running)" : "",
								m_fieldAnswered, m_fieldRefused, m_fieldCells, (Int)m_groundChanges.size() ));
		Int inFog = 0, idle = 0, onCliff = 0;
		// the ones out of the fog that have not reached the bases, by what they do: where a wave stalls
		Int outIdle = 0, outOnStructure = 0, outOnUnit = 0, outWalking = 0;
		const Object *front = NULL;
		Real front2 = 1.0e30f;
		for( UnsignedInt i = 0; i < m_zombies.size(); ++i )
		{
			Object *obj = TheGameLogic->findObjectByID( m_zombies[ i ].id );
			if( obj == NULL )
				continue;
			const Bool fogged = inRing( obj->getPosition() );
			if( fogged )
				++inFog;
			AIUpdateInterface *ai = obj->getAI();
			if( ai != NULL && ai->isIdle() )
				++idle;
			// on a cliff cell, climbing it: of what walks, only a CLIFF locomotor stands there, the Crawler's
			const PathfindCell *cell = obj->isKindOf( KINDOF_AIRCRAFT ) ? NULL : TheAI->pathfinder()->getCell( obj->getLayer(), obj->getPosition() );
			if( cell != NULL && cell->getType() == PathfindCell::CELL_CLIFF )
				++onCliff;
			if( fogged || m_zombies[ i ].reached )
				continue;
			const Object *victim = ai != NULL ? ai->getCurrentVictim() : NULL;
			if( ai == NULL || ai->isIdle() )
				++outIdle;
			else if( victim != NULL && victim->isKindOf( KINDOF_STRUCTURE ) )
				++outOnStructure;
			else if( victim != NULL )
				++outOnUnit;
			else
				++outWalking;
			const Real dx = obj->getPosition()->x - m_centre.x, dy = obj->getPosition()->y - m_centre.y;
			if( dx * dx + dy * dy < front2 )
			{
				front2 = dx * dx + dy * dy;
				front = obj;
			}
		}
		if( front != NULL )
		{
			const Object *victim = front->getAI() != NULL ? front->getAI()->getCurrentVictim() : NULL;
			DEBUG_LOG(( "HEADLESS APOCALYPSE: frame %u on the way in: %d idle, %d on a structure, %d on a unit, %d walking; the nearest at (%.0f,%.0f), %.0f from the middle against a reach of %.0f, on '%s'\n",
									now, outIdle, outOnStructure, outOnUnit, outWalking, front->getPosition()->x, front->getPosition()->y,
									sqrtf( front2 ), m_reachRadius, victim != NULL ? victim->getTemplate()->getName().str() : "nothing" ));
		}
		Int buildings = 0, units = 0, nearMiddle = 0;
		for( Object *obj = TheGameLogic->getFirstObject(); obj; obj = obj->getNextObject() )
		{
			if( obj->isEffectivelyDead() || !isSeat( obj->getControllingPlayer() ) )
				continue;
			if( obj->isKindOf( KINDOF_STRUCTURE ) )
				++buildings;
			else
				++units;
		}
		for( UnsignedInt i = 0; i < m_zombies.size(); ++i )
			if( m_zombies[ i ].reached )
				++nearMiddle;
		DEBUG_LOG(( "HEADLESS APOCALYPSE: frame %u the dead: %d still in the fog, %d idle, %d among the bases, %d on a cliff; the seats hold %d buildings, %d units\n",
								now, inFog, idle, nearMiddle, onCliff, buildings, units ));
		{
			// the living by kind, counted off the list now; nothing kept
			std::vector<Int> byKind( m_kinds.size() + 1, 0 );
			for( UnsignedInt i = 0; i < m_zombies.size(); ++i )
			{
				const Object *obj = TheGameLogic->findObjectByID( m_zombies[ i ].id );
				if( obj != NULL )
				{
					const Int k = kindOf( obj );
					++byKind[ k >= 0 ? k : m_kinds.size() ];
				}
			}
			AsciiString kinds;
			for( UnsignedInt k = 0; k < byKind.size(); ++k )
			{
				if( byKind[ k ] == 0 )
					continue;
				AsciiString one;
				one.format( " %s %d", k < m_kinds.size() ? Apocalypse_kind( (Int)k ).name : "other", byKind[ k ] );
				kinds.concat( one );
			}
			DEBUG_LOG(( "HEADLESS APOCALYPSE: frame %u alive by kind:%s\n", now, kinds.str() ));

			/* What the list and the tallies do not show, read only: the dead walking that are not on the list
				 the alive cap counts (a Broodmother's young, a Carrier's bats, a Sack's spill), the ones a
				 Howler has in a frenzy, and the hurt ones standing in a Rotmother's reach (her
				 PropagandaTowerBehavior heals them and says nothing). */
			std::vector<ObjectID> listed;
			listed.reserve( m_zombies.size() );
			for( UnsignedInt i = 0; i < m_zombies.size(); ++i )
				listed.push_back( m_zombies[ i ].id );
			std::sort( listed.begin(), listed.end() );
			std::vector<const Object *> mothers;
			Int offList = 0, frenzied = 0, tended = 0;
			for( const Object *obj = TheGameLogic->getFirstObject(); obj; obj = obj->getNextObject() )
			{
				if( obj->isEffectivelyDead() || obj->getControllingPlayer() == NULL
						|| obj->getControllingPlayer()->getPlayerIndex() != m_zombiePlayer
						|| !obj->getTemplate()->getName().startsWith( "ApocalypseZombie" ) )
					continue;
				if( !std::binary_search( listed.begin(), listed.end(), obj->getID() ) )
					++offList;
				if( obj->testWeaponBonusCondition( WEAPONBONUSCONDITION_FRENZY_ONE ) )
					++frenzied;
				if( obj->getTemplate()->getName() == "ApocalypseZombieRotmother" )
					mothers.push_back( obj );
			}
			const Real ROTMOTHER_REACH = 150.0f;	// her PropagandaTowerBehavior's Radius in ApocalypseReforged.ini
			for( const Object *obj = TheGameLogic->getFirstObject(); obj && !mothers.empty(); obj = obj->getNextObject() )
			{
				const BodyModuleInterface *body = obj->getBodyModule();
				if( obj->isEffectivelyDead() || body == NULL || body->getHealth() >= body->getMaxHealth()
						|| obj->getControllingPlayer() == NULL || obj->getControllingPlayer()->getPlayerIndex() != m_zombiePlayer
						|| !obj->getTemplate()->getName().startsWith( "ApocalypseZombie" ) )
					continue;
				for( UnsignedInt m = 0; m < mothers.size(); ++m )
				{
					if( mothers[ m ] == obj )
						continue;
					const Real dx = obj->getPosition()->x - mothers[ m ]->getPosition()->x;
					const Real dy = obj->getPosition()->y - mothers[ m ]->getPosition()->y;
					if( dx * dx + dy * dy <= ROTMOTHER_REACH * ROTMOTHER_REACH )
					{
						++tended;
						break;
					}
				}
			}
			DEBUG_LOG(( "HEADLESS APOCALYPSE: frame %u %u on the list against a cap of %d, %d more off it; %d in a Howler's frenzy, %d hurt in reach of %u Rotmothers\n",
									now, (UnsignedInt)m_zombies.size(), Apocalypse_phaseRules( m_phase ).aliveCap, offList, frenzied, tended, (UnsignedInt)mothers.size() ));
		}
		for( Int i = 0; i < ThePlayerList->getPlayerCount(); ++i )
		{
			Player *player = ThePlayerList->getNthPlayer( i );
			if( isSeat( player ) )
				DEBUG_LOG(( "HEADLESS APOCALYPSE: frame %u seat %d money %u rank %d skill %d\n", now, i,
										player->getMoney()->countMoney(), player->getRankLevel(), player->getSkillPoints() ));
		}
		if( standing == 0 && !m_fallLogged )
		{
			m_fallLogged = TRUE;
			DEBUG_LOG(( "HEADLESS APOCALYPSE: frame %u the last seat fell at %s of cycle %u, wave %u, after %u kills\n", now,
									Apocalypse_phaseName( m_phase ), m_cycle, m_wave, m_kills ));
		}
	}
}

//-------------------------------------------------------------------------------------------------
/** One cell of the pathfinder's ground layer as the field copies it: walkable for the dead (what
	  validLocomotorSurfacesForCellType gives GROUND or RUBBLE), pinched, and the first seat's structure
	  standing beside it in examineNeighboringCells' order. */
void ApocalypseDirector::readGround( Int x, Int y, UnsignedByte *ground, ObjectID *source ) const
{
	*ground = 0;
	*source = INVALID_ID;
	PathfindCell *cell = TheAI->pathfinder()->getCell( LAYER_GROUND, x, y );
	if( cell == NULL )
		return;
	const PathfindCell::CellType type = cell->getType();
	if( type != PathfindCell::CELL_CLEAR && type != PathfindCell::CELL_RUBBLE )
		return;
	*ground = ApocalypseFlowField::WALKABLE | ( cell->getPinched() ? ApocalypseFlowField::PINCHED : 0 );
	for( Int i = 0; i < 8; ++i )
	{
		const PathfindCell *beside = TheAI->pathfinder()->getCell( LAYER_GROUND, x + FIELD_DX[ i ], y + FIELD_DY[ i ] );
		if( beside == NULL || beside->getType() != PathfindCell::CELL_OBSTACLE )
			continue;
		const Object *structure = TheGameLogic->findObjectByID( beside->getObstacleID() );
		if( structure != NULL && !structure->isEffectivelyDead() && isSeat( structure->getControllingPlayer() ) )
		{
			*source = structure->getID();
			return;
		}
	}
}

//-------------------------------------------------------------------------------------------------
/// the ground copy read whole, once, and its build begun
void ApocalypseDirector::startField( void )
{
	m_fieldWanted = FALSE;
	m_groundChanges.clear();
	const ICoord2D *extent = TheAI->pathfinder()->getExtent();
	m_field.setup( extent->x + 1, extent->y + 1 );
	if( !m_field.isStarted() )
		return;
	for( Int x = 0; x < m_field.getWidth(); ++x )
		for( Int y = 0; y < m_field.getHeight(); ++y )
		{
			UnsignedByte ground;
			ObjectID source;
			readGround( x, y, &ground, &source );
			m_field.setGround( x * m_field.getHeight() + y, ground, source );
		}
	m_field.beginBuild();
	m_fieldSteps = 0;
}

//-------------------------------------------------------------------------------------------------
/// a slice of the running build or repair; the next repair, with every change since, once it is done
void ApocalypseDirector::stepField( void )
{
	if( m_fieldWanted )
		startField();
	if( !m_field.isStarted() )
		return;
	if( !m_field.isRepairing() && !m_groundChanges.empty() )
	{
		// the copy against the pathfinder over the changed ground; a cell in two areas is compared once
		std::vector<ApocalypseFlowField::Change> changes;
		const Int height = m_field.getHeight();
		for( size_t a = 0; a < m_groundChanges.size(); ++a )
		{
			const IRegion2D &area = m_groundChanges[ a ];
			for( Int x = max( area.lo.x, (Int)0 ); x <= min( area.hi.x, m_field.getWidth() - 1 ); ++x )
				for( Int y = max( area.lo.y, (Int)0 ); y <= min( area.hi.y, height - 1 ); ++y )
				{
					Bool seen = FALSE;
					for( size_t b = 0; b < a && !seen; ++b )
						seen = x >= m_groundChanges[ b ].lo.x && x <= m_groundChanges[ b ].hi.x &&
							y >= m_groundChanges[ b ].lo.y && y <= m_groundChanges[ b ].hi.y;
					if( seen )
						continue;
					const Int cell = x * height + y;
					ApocalypseFlowField::Change change;
					change.cell = cell;
					change.oldGround = m_field.groundOf( cell );
					change.oldSource = m_field.sourceOf( cell );
					readGround( x, y, &change.newGround, &change.newSource );
					if( change.newGround != change.oldGround || change.newSource != change.oldSource )
						changes.push_back( change );
				}
		}
		m_groundChanges.clear();
		if( !changes.empty() )
		{
			m_field.beginRepair( changes );
			m_fieldSteps = 0;
		}
	}
	if( m_field.isRepairing() )
	{
		m_fieldCells += (UnsignedInt)m_field.stepRepair( APOCALYPSE_FIELD_CELLS_PER_FRAME );
		++m_fieldSteps;
		if( !m_field.isRepairing() )
			++m_fieldRepairs;
	}
}

//-------------------------------------------------------------------------------------------------
void ApocalypseDirector::noteGroundChange( const IRegion2D &cells )
{
	if( !m_active || !m_field.isStarted() )
		return;		// a build to come reads the ground as it is then
	m_groundChanges.push_back( cells );
}

//-------------------------------------------------------------------------------------------------
/// one of the dead standing beside a seat's structure, by the field's ground copy, or hitting one
Bool ApocalypseDirector::hasReachedBases( const Object *obj ) const
{
	if( m_field.isStarted() )
	{
		const Int x = REAL_TO_INT_FLOOR( obj->getPosition()->x / PATHFIND_CELL_SIZE_F );
		const Int y = REAL_TO_INT_FLOOR( obj->getPosition()->y / PATHFIND_CELL_SIZE_F );
		if( x >= 0 && y >= 0 && x < m_field.getWidth() && y < m_field.getHeight() &&
				m_field.sourceOf( x * m_field.getHeight() + y ) != INVALID_ID )
			return TRUE;
	}
	const AIUpdateInterface *ai = obj->getAI();
	const Object *victim = ai != NULL ? ai->getCurrentVictim() : NULL;
	if( victim == NULL || !victim->isKindOf( KINDOF_STRUCTURE ) || !isSeat( victim->getControllingPlayer() ) )
		return FALSE;
	// in reach of its claws: the victim's edge within a cell
	const Real dx = victim->getPosition()->x - obj->getPosition()->x, dy = victim->getPosition()->y - obj->getPosition()->y;
	const Real reach = victim->getGeometryInfo().getBoundingCircleRadius() + PATHFIND_CELL_SIZE_F;
	return dx * dx + dy * dy <= reach * reach;
}

//-------------------------------------------------------------------------------------------------
Bool ApocalypseDirector::fieldRoute( const Object *obj, const Coord3D *from, std::vector<Int> *cells ) const
{
	if( !isZombie( obj ) || obj->getLayer() != LAYER_GROUND )
		return FALSE;
	return m_field.route( REAL_TO_INT_FLOOR( from->x / PATHFIND_CELL_SIZE_F ), REAL_TO_INT_FLOOR( from->y / PATHFIND_CELL_SIZE_F ), cells );
}

//-------------------------------------------------------------------------------------------------
Bool ApocalypseDirector::fieldGoal( const Object *obj, Coord3D *goal ) const
{
	std::vector<Int> cells;
	if( !fieldRoute( obj, obj->getPosition(), &cells ) )
		return FALSE;
	const ApocalypseFlowField &field = m_field;
	const Int first = cells.front(), last = cells.back();
	// beside it already: the old rule picks what to hit
	if( abs( field.cellX( last ) - field.cellX( first ) ) < 2 && abs( field.cellY( last ) - field.cellY( first ) ) < 2 )
		return FALSE;
	goal->x = ( (Real)field.cellX( last ) + 0.5f ) * PATHFIND_CELL_SIZE_F;
	goal->y = ( (Real)field.cellY( last ) + 0.5f ) * PATHFIND_CELL_SIZE_F;
	goal->z = TheTerrainLogic->getGroundHeight( goal->x, goal->y );
	return TRUE;
}

//-------------------------------------------------------------------------------------------------
/** The route as the search would have handed it over (buildActualPath, prependCells): a node a cell from
	  the goal back to the one after the unit's own, the unit's spot first, then Path::optimize.  NULL when
	  the field has no route from there, or its route does not end at `to`, the goal the zombie walks to. */
Path *ApocalypseDirector::fieldPath( const Object *obj, const Coord3D *from, const Coord3D *to )
{
	if( !m_active || obj == NULL || from == NULL || to == NULL || obj->getAI() == NULL )
		return NULL;
	std::vector<Int> cells;
	if( !fieldRoute( obj, from, &cells ) || cells.size() < 2 )
		return NULL;
	const ApocalypseFlowField &field = m_field;
	const Int last = cells.back();
	// give or take the cell a move order snaps its goal to
	if( abs( field.cellX( last ) - REAL_TO_INT_FLOOR( to->x / PATHFIND_CELL_SIZE_F ) ) > 1 ||
			abs( field.cellY( last ) - REAL_TO_INT_FLOOR( to->y / PATHFIND_CELL_SIZE_F ) ) > 1 )
	{
		++m_fieldRefused;
		return NULL;
	}
	Int radius;
	Bool center;
	TheAI->pathfinder()->getRadiusAndCenter( obj, radius, center );
	const Real offset = center ? 0.5f : 0.05f;		// adjustCoordToCell
	Path *path = newInstance( Path );
	for( Int n = (Int)cells.size() - 1; n >= 1; --n )
	{
		Coord3D pos;
		pos.x = ( (Real)field.cellX( cells[ n ] ) + offset ) * PATHFIND_CELL_SIZE_F;
		pos.y = ( (Real)field.cellY( cells[ n ] ) + offset ) * PATHFIND_CELL_SIZE_F;
		pos.z = TheTerrainLogic->getLayerHeight( pos.x, pos.y, LAYER_GROUND );
		path->prependNode( &pos, LAYER_GROUND );
		path->getFirstNode()->setCanOptimize( TRUE );		// a node is born unoptimizable; prependCells says so for each
	}
	if( from->x != path->getFirstNode()->getPosition()->x || from->y != path->getFirstNode()->getPosition()->y )
		path->prependNode( from, LAYER_GROUND );
	path->optimize( obj, obj->getAI()->getLocomotorSet().getValidSurfaces(), FALSE );
	++m_fieldAnswered;
	return path;
}

//-------------------------------------------------------------------------------------------------
/** The field is not in the save, only its ground copy and the repair in hand, which make it again here
	  as it stood: the field that repair began from is a build of the copy as it was before the repair's
	  changes, and the repair is begun again on it and given as many slices as it had had. */
void ApocalypseDirector::loadPostProcess( void )
{
	if( !m_active || !m_fieldLoaded )
		return;		// a save from before the field: the next tick builds it from the map
	m_fieldLoaded = FALSE;
	if( !m_field.isStarted() )
		return;
	if( m_fieldLoadedRepair && !m_fieldLoadedBuild )
		for( Int k = (Int)m_fieldLoadedChanges.size() - 1; k >= 0; --k )
			m_field.setGround( m_fieldLoadedChanges[ k ].cell, m_fieldLoadedChanges[ k ].oldGround, m_fieldLoadedChanges[ k ].oldSource );
	m_field.beginBuild();
	if( m_fieldLoadedRepair && m_fieldLoadedBuild )
	{
		for( UnsignedInt s = 0; s < m_fieldSteps; ++s )
			m_field.stepRepair( APOCALYPSE_FIELD_CELLS_PER_FRAME );
	}
	else
	{
		while( m_field.isRepairing() )
			m_field.stepRepair( APOCALYPSE_FIELD_CELLS_PER_FRAME * 64 );
		if( m_fieldLoadedRepair )
		{
			m_field.beginRepair( m_fieldLoadedChanges );
			for( UnsignedInt s = 0; s < m_fieldSteps; ++s )
				m_field.stepRepair( APOCALYPSE_FIELD_CELLS_PER_FRAME );
		}
	}
	m_fieldLoadedChanges.clear();
}

//-------------------------------------------------------------------------------------------------
void ApocalypseDirector::crc( Xfer *xfer )
{
	ApocalypseDirector::xfer( xfer );
}

//-------------------------------------------------------------------------------------------------
/** Version 1: the day's clock, the wave clock, the counts and the living zombies.  The centre, the
	  spawns, the owner and the templates come from the map and the data on every load, and the light
	  is handed to the client again on the first tick after it.  Version 6 adds the elite and flyer room,
	  version 7 the last Blood Moon night whose boss came, version 8 the waves' scatter seed and theme. */
void ApocalypseDirector::xfer( Xfer *xfer )
{
	XferVersion currentVersion = 8;
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );

	xfer->xferInt( &m_phase );
	xfer->xferUnsignedInt( &m_phaseStartFrame );
	xfer->xferUnsignedInt( &m_cycle );
	xfer->xferUnsignedInt( &m_wave );
	xfer->xferUnsignedInt( &m_nextWaveFrame );
	xfer->xferUnsignedInt( &m_nextSpawnFrame );
	xfer->xferInt( &m_pending );
	xfer->xferUnsignedInt( &m_spawned );
	xfer->xferUnsignedInt( &m_kills );
	xfer->xferUnsignedInt( &m_reached );

	UnsignedInt count = (UnsignedInt)m_zombies.size();
	xfer->xferUnsignedInt( &count );
	if( xfer->getXferMode() == XFER_LOAD )
		m_zombies.resize( count );
	for( UnsignedInt i = 0; i < count; ++i )
	{
		xfer->xferObjectID( &m_zombies[ i ].id );
		xfer->xferBool( &m_zombies[ i ].reached );
		if( xfer->getXferMode() == XFER_LOAD )
			m_zombies[ i ].young = -1;		// not saved: the first look after a load takes the count as it finds it
	}

	if( version >= 2 )
	{
		xfer->xferUnsignedInt( &m_fogKills );
		count = (UnsignedInt)m_fogStays.size();
		xfer->xferUnsignedInt( &count );
		if( xfer->getXferMode() == XFER_LOAD )
			m_fogStays.resize( count );
		for( UnsignedInt i = 0; i < count; ++i )
		{
			xfer->xferObjectID( &m_fogStays[ i ].id );
			xfer->xferUnsignedInt( &m_fogStays[ i ].since );
		}
	}

	if( version >= 3 )
	{
		xfer->xferUnsignedInt( &m_waveSectors );
		xfer->xferUnsignedInt( &m_warnSectors );
		xfer->xferInt( &m_adapt );
		xfer->xferUnsignedInt( &m_reachedAtWave );
		xfer->xferUnsignedInt( &m_infected );
		count = (UnsignedInt)m_rises.size();
		xfer->xferUnsignedInt( &count );
		if( xfer->getXferMode() == XFER_LOAD )
			m_rises.resize( count );
		for( UnsignedInt i = 0; i < count; ++i )
		{
			xfer->xferObjectID( &m_rises[ i ].victim );
			xfer->xferCoord3D( &m_rises[ i ].where );
			xfer->xferUnsignedInt( &m_rises[ i ].frame );
		}
	}

	if( version >= 4 )
		xfer->xferUnsignedInt( &m_frontTurn );

	// version 5: the flow field's ground copy, the repair in hand and the changes waiting; loadPostProcess
	// makes the field again from these, as it stood (a save from before has none: the next tick builds it)
	if( version >= 5 )
	{
		xfer->xferUnsignedInt( &m_fieldRepairs );
		xfer->xferBool( &m_fieldWanted );
		xfer->xferUnsignedInt( &m_fieldSteps );
		Bool started = m_field.isStarted();
		xfer->xferBool( &started );
		Bool repairing = m_field.isRepairing();
		xfer->xferBool( &repairing );
		Bool building = m_field.isBuilding();
		xfer->xferBool( &building );
		count = (UnsignedInt)m_groundChanges.size();
		xfer->xferUnsignedInt( &count );
		if( xfer->getXferMode() == XFER_LOAD )
			m_groundChanges.resize( count );
		for( UnsignedInt i = 0; i < count; ++i )
		{
			xfer->xferInt( &m_groundChanges[ i ].lo.x );
			xfer->xferInt( &m_groundChanges[ i ].lo.y );
			xfer->xferInt( &m_groundChanges[ i ].hi.x );
			xfer->xferInt( &m_groundChanges[ i ].hi.y );
		}
		// the ground copy and the repair's changes are the size of the map: saved, not checksummed every frame
		if( started && xfer->getXferMode() != XFER_CRC )
			xferField( xfer, repairing && !building );
		if( xfer->getXferMode() != XFER_CRC )
			DEBUG_LOG(( "HEADLESS APOCALYPSE FIELD %s: frame %u, %s, %u slices in, %d changes, %d changed areas waiting\n",
									xfer->getXferMode() == XFER_SAVE ? "SAVE" : "LOAD", TheGameLogic->getFrame(),
									!started ? "not built" : building ? "building" : repairing ? "repairing" : "idle", m_fieldSteps,
									repairing && !building ? (Int)m_field.repairChanges().size() + (Int)m_fieldLoadedChanges.size() : 0, (Int)m_groundChanges.size() ));
		if( xfer->getXferMode() == XFER_LOAD )
		{
			m_fieldLoaded = started;
			m_fieldLoadedRepair = repairing;
			m_fieldLoadedBuild = building;
		}
	}
	// version 6: the waves' room for elites and flyers; an older save had neither and loads them empty
	if( version >= 6 )
	{
		xfer->xferInt( &m_eliteRoom );
		xfer->xferInt( &m_flyerRoom );
	}
	else if( xfer->getXferMode() == XFER_LOAD )
	{
		m_eliteRoom = 0;
		m_flyerRoom = 0;
	}

	// version 7: an older save sent no boss and loads with none come, so a Blood Moon under way sends its own
	if( version >= 7 )
		xfer->xferUnsignedInt( &m_bossNight );
	else if( xfer->getXferMode() == XFER_LOAD )
		m_bossNight = 0;

	// version 8: the waves out now's scatter seed and theme; an older save's come out on seed 0, mixed
	if( version >= 8 )
	{
		xfer->xferUnsignedInt( &m_waveSeed );
		xfer->xferInt( &m_waveTheme );
		if( m_waveTheme < 0 || m_waveTheme >= APOCALYPSE_THEMES )
			m_waveTheme = APOCALYPSE_THEME_MIXED;
	}
	else if( xfer->getXferMode() == XFER_LOAD )
	{
		m_waveSeed = 0;
		m_waveTheme = APOCALYPSE_THEME_MIXED;
	}
}

//-------------------------------------------------------------------------------------------------
/// the field's ground copy, run-length coded, its structures' sides and the running repair's changes
void ApocalypseDirector::xferField( Xfer *xfer, Bool withChanges )
{
	Int width = m_field.getWidth(), height = m_field.getHeight();
	xfer->xferInt( &width );
	xfer->xferInt( &height );
	if( xfer->getXferMode() == XFER_LOAD )
		m_field.setup( width, height );
	const Int cells = width * height;

	// runs of equal ground bytes
	UnsignedInt runs = 0;
	if( xfer->getXferMode() != XFER_LOAD )
		for( Int c = 0; c < cells; ++c )
			if( c == 0 || m_field.groundOf( c ) != m_field.groundOf( c - 1 ) )
				++runs;
	xfer->xferUnsignedInt( &runs );
	Int at = 0;
	for( UnsignedInt r = 0; r < runs; ++r )
	{
		UnsignedByte ground = 0;
		Int length = 0;
		if( xfer->getXferMode() != XFER_LOAD )
		{
			ground = m_field.groundOf( at );
			while( at + length < cells && m_field.groundOf( at + length ) == ground )
				++length;
		}
		xfer->xferUnsignedByte( &ground );
		xfer->xferInt( &length );
		if( xfer->getXferMode() == XFER_LOAD )
			for( Int c = at; c < at + length && c < cells; ++c )
				m_field.setGround( c, ground, INVALID_ID );
		at += length;
	}

	// the cells beside a seat's structure
	UnsignedInt sides = 0;
	if( xfer->getXferMode() != XFER_LOAD )
		for( Int c = 0; c < cells; ++c )
			if( m_field.sourceOf( c ) != INVALID_ID )
				++sides;
	xfer->xferUnsignedInt( &sides );
	Int c = 0;
	for( UnsignedInt s = 0; s < sides; ++s )
	{
		Int cell = 0;
		ObjectID source = INVALID_ID;
		if( xfer->getXferMode() != XFER_LOAD )
		{
			while( m_field.sourceOf( c ) == INVALID_ID )
				++c;
			cell = c;
			source = m_field.sourceOf( c );
			++c;
		}
		xfer->xferInt( &cell );
		xfer->xferObjectID( &source );
		if( xfer->getXferMode() == XFER_LOAD && cell >= 0 && cell < cells )
			m_field.setGround( cell, m_field.groundOf( cell ), source );
	}

	// the running repair's changes, applied already to the copy above
	if( !withChanges )
		return;
	if( xfer->getXferMode() != XFER_LOAD )
		m_fieldLoadedChanges = m_field.repairChanges();
	UnsignedInt changes = (UnsignedInt)m_fieldLoadedChanges.size();
	xfer->xferUnsignedInt( &changes );
	if( xfer->getXferMode() == XFER_LOAD )
		m_fieldLoadedChanges.resize( changes );
	for( UnsignedInt k = 0; k < changes; ++k )
	{
		ApocalypseFlowField::Change &change = m_fieldLoadedChanges[ k ];
		xfer->xferInt( &change.cell );
		xfer->xferUnsignedByte( &change.oldGround );
		xfer->xferUnsignedByte( &change.newGround );
		xfer->xferObjectID( &change.oldSource );
		xfer->xferObjectID( &change.newSource );
	}
	if( xfer->getXferMode() != XFER_LOAD )
		m_fieldLoadedChanges.clear();
}
