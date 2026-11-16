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

// AIInfluenceMap.cpp

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "Common/Xfer.h"
#include "GameLogic/AIInfluenceMap.h"
#include "GameLogic/Weapon.h"

AIInfluenceMap::AIInfluenceMap() :
	m_originX( 0.0f ), m_originY( 0.0f ), m_cellSize( 1.0f ), m_cols( 0 ), m_rows( 0 ),
	m_built( FALSE ), m_builtFrame( 0 )
{
}

void AIInfluenceMap::reset( Real originX, Real originY, Real width, Real height, Real cellSize )
{
	m_originX = originX;
	m_originY = originY;
	m_cellSize = cellSize;
	m_cols = (width > 0.0f) ? REAL_TO_INT_CEIL( width / cellSize ) : 0;
	m_rows = (height > 0.0f) ? REAL_TO_INT_CEIL( height / cellSize ) : 0;
	m_enemy.assign( m_cols * m_rows * AITHREAT_COUNT, 0.0f );
	m_friend.assign( m_cols * m_rows, 0.0f );
	m_height.assign( m_cols * m_rows, 0.0f );
	m_built = FALSE;
}

void AIInfluenceMap::clear()
{
	m_enemy.assign( m_enemy.size(), 0.0f );
	m_friend.assign( m_friend.size(), 0.0f );
}

void AIInfluenceMap::cellCenter( Int col, Int row, Real *x, Real *y ) const
{
	*x = m_originX + (col + 0.5f) * m_cellSize;
	*y = m_originY + (row + 0.5f) * m_cellSize;
}

void AIInfluenceMap::setCellHeight( Int col, Int row, Real z )
{
	m_height[ row * m_cols + col ] = z;
}

Int AIInfluenceMap::cellIndexAt( Real x, Real y ) const
{
	const Int col = REAL_TO_INT_FLOOR( (x - m_originX) / m_cellSize );
	const Int row = REAL_TO_INT_FLOOR( (y - m_originY) / m_cellSize );
	if( col < 0 || row < 0 || col >= m_cols || row >= m_rows )
		return -1;
	return row * m_cols + col;
}

/** The high ground can at most double a range (ELEVATION_RANGE_CAP), so the square walked is twice
	* the range out, and each cell inside it is cut back to the reach its own height allows. */
template <class Visit>
void AIInfluenceMap::visitReach( Real x, Real y, Real z, Real range, Visit &visit ) const
{
	const Real farthest = range + Weapon_highGroundRangeBonus( range, FLT_MAX );
	const Int colLo = max<Int>( 0, REAL_TO_INT_FLOOR( (x - farthest - m_originX) / m_cellSize ) );
	const Int colHi = min<Int>( m_cols - 1, REAL_TO_INT_FLOOR( (x + farthest - m_originX) / m_cellSize ) );
	const Int rowLo = max<Int>( 0, REAL_TO_INT_FLOOR( (y - farthest - m_originY) / m_cellSize ) );
	const Int rowHi = min<Int>( m_rows - 1, REAL_TO_INT_FLOOR( (y + farthest - m_originY) / m_cellSize ) );
	const Real rangeSqr = sqr( range ), farthestSqr = sqr( farthest );
	for( Int row = rowLo; row <= rowHi; ++row )
	{
		const Real cy = m_originY + (row + 0.5f) * m_cellSize;
		for( Int col = colLo; col <= colHi; ++col )
		{
			const Real cx = m_originX + (col + 0.5f) * m_cellSize;
			const Real distSqr = sqr( cx - x ) + sqr( cy - y );
			// the bonus is never below 0 nor past the cap, so the height only decides the ring between
			if( distSqr > farthestSqr )
				continue;
			const Int index = row * m_cols + col;
			if( distSqr <= rangeSqr || distSqr <= sqr( range + Weapon_highGroundRangeBonus( range, z - m_height[ index ] ) ) )
				visit( index );
		}
	}
}

struct CollectCell
{
	std::vector<Int> *cells;
	void operator()( Int index ) { cells->push_back( index ); }
};

/** Adds to one slot of every cell: stride 1 and slot 0 for the friendly layer, AITHREAT_COUNT for the enemy's. */
struct AddPower
{
	Real *layer;
	Int stride;
	Int slot;
	Real power;
	void operator()( Int index ) { layer[ index * stride + slot ] += power; }
};

/** The vehicle and infantry slots of every cell a ground gun reaches. */
struct AddGroundPower
{
	Real *layer;
	Real vehicle;
	Real infantry;
	void operator()( Int index )
	{
		layer[ index * AITHREAT_COUNT + AITHREAT_VEHICLE ] += vehicle;
		layer[ index * AITHREAT_COUNT + AITHREAT_INFANTRY ] += infantry;
	}
};

/** The same cells in the same order as cellsReached, so a seat stamping alone adds what the seats sharing a
	* turn's shapes add. */
void AIInfluenceMap::stampEnemy( Real x, Real y, Real z, Real groundRange, Real airRange, const Real *power )
{
	if( m_cols <= 0 )
		return;
	if( groundRange > 0.0f && (power[ AITHREAT_VEHICLE ] > 0.0f || power[ AITHREAT_INFANTRY ] > 0.0f) )
	{
		AddGroundPower add = { &m_enemy[ 0 ], power[ AITHREAT_VEHICLE ], power[ AITHREAT_INFANTRY ] };
		visitReach( x, y, z, groundRange, add );
	}
	if( airRange > 0.0f && power[ AITHREAT_AIR ] > 0.0f )
	{
		AddPower add = { &m_enemy[ 0 ], AITHREAT_COUNT, AITHREAT_AIR, power[ AITHREAT_AIR ] };
		visitReach( x, y, z, airRange, add );
	}
}

void AIInfluenceMap::stampFriend( Real x, Real y, Real z, Real range, Real power )
{
	if( m_cols <= 0 || power <= 0.0f || range <= 0.0f )
		return;
	AddPower add = { &m_friend[ 0 ], 1, 0, power };
	visitReach( x, y, z, range, add );
}

void AIInfluenceMap::cellsReached( Real x, Real y, Real z, Real range, std::vector<Int> &cells ) const
{
	if( m_cols <= 0 || range <= 0.0f )
		return;
	CollectCell collect = { &cells };
	visitReach( x, y, z, range, collect );
}

/** The powers are whole quarters (the cost map's whole units and ranks of a quarter), so every sum is exact
	* below 2^22 and the order the seats add in cannot change a cell. */
static const Real EXACT_SUM_LIMIT = 4194304.0f;

void AIInfluenceMap::addEnemy( const Int *cells, Int count, Real vehicle, Real infantry )
{
	for( Int i = 0; i < count; ++i )
	{
		Real *cell = &m_enemy[ cells[ i ] * AITHREAT_COUNT ];
		cell[ AITHREAT_VEHICLE ] += vehicle;
		cell[ AITHREAT_INFANTRY ] += infantry;
		DEBUG_ASSERTCRASH( cell[ AITHREAT_VEHICLE ] < EXACT_SUM_LIMIT && cell[ AITHREAT_INFANTRY ] < EXACT_SUM_LIMIT, ("influence sum past exact quarters") );
	}
}

void AIInfluenceMap::addEnemyAir( const Int *cells, Int count, Real air )
{
	for( Int i = 0; i < count; ++i )
	{
		m_enemy[ cells[ i ] * AITHREAT_COUNT + AITHREAT_AIR ] += air;
		DEBUG_ASSERTCRASH( m_enemy[ cells[ i ] * AITHREAT_COUNT + AITHREAT_AIR ] < EXACT_SUM_LIMIT, ("influence sum past exact quarters") );
	}
}

void AIInfluenceMap::addFriend( const Int *cells, Int count, Real power )
{
	for( Int i = 0; i < count; ++i )
		m_friend[ cells[ i ] ] += power;
}

Bool AIInfluenceMap::sameGrid( Int cols, Int rows, Real originX, Real originY, Real cellSize ) const
{
	return m_cols == cols && m_rows == rows && m_originX == originX && m_originY == originY && m_cellSize == cellSize;
}

Real AIInfluenceMap::enemyAt( Real x, Real y, const AIThreatMix &mix ) const
{
	const Int index = cellIndexAt( x, y );
	if( index < 0 )
		return 0.0f;
	const Real *cell = &m_enemy[ index * AITHREAT_COUNT ];
	// a weight of 0 times a finite sum adds an exact 0, so AITHREAT_COST_MIX is the vehicle slot to the bit
	return mix.w[ AITHREAT_VEHICLE ] * cell[ AITHREAT_VEHICLE ] + mix.w[ AITHREAT_INFANTRY ] * cell[ AITHREAT_INFANTRY ] +
		mix.w[ AITHREAT_AIR ] * cell[ AITHREAT_AIR ];
}

Real AIInfluenceMap::friendAt( Real x, Real y ) const
{
	const Int index = cellIndexAt( x, y );
	return (index < 0) ? 0.0f : m_friend[ index ];
}

void AIInfluenceMap::xfer( Xfer *xfer, UnsignedByte version )
{
	xfer->xferReal( &m_originX );
	xfer->xferReal( &m_originY );
	xfer->xferReal( &m_cellSize );
	xfer->xferInt( &m_cols );
	xfer->xferInt( &m_rows );
	xfer->xferBool( &m_built );
	xfer->xferUnsignedInt( &m_builtFrame );
	const size_t cells = (size_t)m_cols * (size_t)m_rows;
	if( xfer->getXferMode() == XFER_LOAD )
	{
		m_enemy.assign( cells * AITHREAT_COUNT, 0.0f );
		m_friend.assign( cells, 0.0f );
		m_height.assign( cells, 0.0f );
	}
	for( size_t i = 0; i < cells; ++i )
	{
		Real *enemy = &m_enemy[ i * AITHREAT_COUNT ];
		if( version < 37 )
		{
			// one figure, the build cost of what reached the cell: every reader sees it until the next round
			xfer->xferReal( &enemy[ AITHREAT_VEHICLE ] );
			enemy[ AITHREAT_INFANTRY ] = enemy[ AITHREAT_AIR ] = enemy[ AITHREAT_VEHICLE ];
		}
		else
		{
			for( Int c = 0; c < AITHREAT_COUNT; ++c )
				xfer->xferReal( &enemy[ c ] );
		}
		xfer->xferReal( &m_friend[ i ] );
		xfer->xferReal( &m_height[ i ] );
	}
}

Bool AIKite_standoffDistance( Real myRange, Real enemyRange, Real margin, Real *distance )
{
	if( myRange < enemyRange + 2.0f * margin )
		return FALSE;
	// the far edge of its own reach: the most room between it and the gun closing on it, so the step
	// that follows is a long way off.  Stepping to just past the enemy's reach left a vehicle turning
	// round inside it by the time the turn was done.
	*distance = myRange - margin;
	return TRUE;
}
